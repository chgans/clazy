/*
   This file is part of the clazy static checker.

  Copyright (C) 2015 Klarälvdalens Datakonsult AB, a KDAB Group company, info@kdab.com
  Author: Sérgio Martins <sergio.martins@kdab.com>

  Copyright (C) 2015 Sergio Martins <smartins@kde.org>

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Library General Public
  License as published by the Free Software Foundation; either
  version 2 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Library General Public License for more details.

  You should have received a copy of the GNU Library General Public License
  along with this library; see the file COPYING.LIB.  If not, write to
  the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
  Boston, MA 02110-1301, USA.
*/

#include "checkbase.h"
#include "StringUtils.h"

#include <clang/AST/Decl.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/ParentMap.h>
#include <clang/Rewrite/Frontend/FixItRewriter.h>

#include <vector>

using namespace clang;
using namespace std;

CheckBase::CheckBase(const string &name, const CompilerInstance &ci)
    : m_ci(ci)
    , m_name(name)
    , m_lastDecl(nullptr)
    , m_enabledFixits(0)
{
    ASTContext &context = m_ci.getASTContext();
    m_tu = context.getTranslationUnitDecl();
    m_lastMethodDecl = nullptr;
}

CheckBase::~CheckBase()
{
}

void CheckBase::VisitStatement(Stmt *stm)
{
    if (!shouldIgnoreFile(stm->getLocStart())) {
        VisitStmt(stm);
    }
}

void CheckBase::VisitDeclaration(Decl *decl)
{
    if (shouldIgnoreFile(decl->getLocStart()))
        return;

    m_lastDecl = decl;
    auto mdecl = dyn_cast<CXXMethodDecl>(decl);
    if (mdecl)
        m_lastMethodDecl = mdecl;

    VisitDecl(decl);
}

string CheckBase::name() const
{
    return m_name;
}

void CheckBase::setParentMap(ParentMap *parentMap)
{
    m_parentMap = parentMap;
}

void CheckBase::VisitStmt(Stmt *)
{
}

void CheckBase::VisitDecl(Decl *)
{
}

bool CheckBase::shouldIgnoreFile(SourceLocation loc) const
{
    if (!loc.isValid() || sm().isInSystemHeader(loc))
        return true;

    string filename = sm().getFilename(loc);

    return clazy_std::any_of(filesToIgnore(), [filename](const std::string &ignored) {
        return clazy_std::contains(filename, ignored);
    });
}

std::vector<std::string> CheckBase::filesToIgnore() const
{
    return {};
}

void CheckBase::emitWarning(clang::SourceLocation loc, std::string error, bool printWarningTag)
{
    emitWarning(loc, error, {}, printWarningTag);
}

void CheckBase::emitWarning(clang::SourceLocation loc, std::string error, const vector<FixItHint> &fixits, bool printWarningTag)
{
    if (loc.isMacroID()) {
        if (warningAlreadyEmitted(loc))
            return; // For warnings in macro arguments we get a warning in each place the argument is used within the expanded macro, so filter all the dups
        m_emittedWarningsInMacro.push_back(loc.getRawEncoding());
    }

    const string tag = " [-Wclazy-" + name() + ']';
    if (printWarningTag)
        error += tag;

    reallyEmitWarning(loc, error, fixits);

    for (const auto& l : m_queuedManualInterventionWarnings) {
        string msg = string("FixIt failed, requires manual intervention: ");
        if (!l.second.empty())
            msg += ' ' + l.second;

        reallyEmitWarning(l.first, msg + tag, {});
    }

    m_queuedManualInterventionWarnings.clear();
}

void CheckBase::reallyEmitWarning(clang::SourceLocation loc, const std::string &error, const vector<FixItHint> &fixits)
{
    FullSourceLoc full(loc, sm());
    unsigned id = m_ci.getDiagnostics().getDiagnosticIDs()->getCustomDiagID(DiagnosticIDs::Warning, error.c_str());
    DiagnosticBuilder B = m_ci.getDiagnostics().Report(full, id);
    for (const FixItHint& fixit : fixits) {
        if (!fixit.isNull())
            B.AddFixItHint(fixit);
    }
}

void CheckBase::queueManualFixitWarning(clang::SourceLocation loc, int fixitType, const string &message)
{
    if (isFixitEnabled(fixitType) && !manualFixitAlreadyQueued(loc)) {
        m_queuedManualInterventionWarnings.push_back({loc, message});
        m_emittedManualFixItsWarningsInMacro.push_back(loc.getRawEncoding());
    }
}

bool CheckBase::warningAlreadyEmitted(SourceLocation loc) const
{
    PresumedLoc ploc = sm().getPresumedLoc(loc);
    for (auto rawLoc : m_emittedWarningsInMacro) {
        SourceLocation l = SourceLocation::getFromRawEncoding(rawLoc);
        PresumedLoc p = sm().getPresumedLoc(l);
        if (Utils::presumedLocationsEqual(p, ploc))
            return true;
    }

    return false;
}

bool CheckBase::manualFixitAlreadyQueued(SourceLocation loc) const
{
    PresumedLoc ploc = sm().getPresumedLoc(loc);
    for (auto loc : m_emittedManualFixItsWarningsInMacro) {
        SourceLocation l = SourceLocation::getFromRawEncoding(loc);
        PresumedLoc p = sm().getPresumedLoc(l);
        if (Utils::presumedLocationsEqual(p, ploc))
            return true;
    }

    return false;
}

std::vector<string> CheckBase::supportedOptions() const
{
    return {};
}

bool CheckBase::isOptionSet(const std::string &optionName) const
{
    const string qualifiedName = name() + '-' + optionName;
    return CheckManager::instance()->isOptionSet(qualifiedName);
}

void CheckBase::setEnabledFixits(int fixits)
{
    m_enabledFixits = fixits;
}

bool CheckBase::isFixitEnabled(int fixit) const
{
    return (m_enabledFixits & fixit) || CheckManager::instance()->allFixitsEnabled();
}
