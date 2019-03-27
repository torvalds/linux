//===--- TransProtectedScope.cpp - Transformations to ARC mode ------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Adds brackets in case statements that "contain" initialization of retaining
// variable, thus emitting the "switch case is in protected scope" error.
//
//===----------------------------------------------------------------------===//

#include "Transforms.h"
#include "Internals.h"
#include "clang/AST/ASTContext.h"
#include "clang/Sema/SemaDiagnostic.h"

using namespace clang;
using namespace arcmt;
using namespace trans;

namespace {

class LocalRefsCollector : public RecursiveASTVisitor<LocalRefsCollector> {
  SmallVectorImpl<DeclRefExpr *> &Refs;

public:
  LocalRefsCollector(SmallVectorImpl<DeclRefExpr *> &refs)
    : Refs(refs) { }

  bool VisitDeclRefExpr(DeclRefExpr *E) {
    if (ValueDecl *D = E->getDecl())
      if (D->getDeclContext()->getRedeclContext()->isFunctionOrMethod())
        Refs.push_back(E);
    return true;
  }
};

struct CaseInfo {
  SwitchCase *SC;
  SourceRange Range;
  enum {
    St_Unchecked,
    St_CannotFix,
    St_Fixed
  } State;

  CaseInfo() : SC(nullptr), State(St_Unchecked) {}
  CaseInfo(SwitchCase *S, SourceRange Range)
    : SC(S), Range(Range), State(St_Unchecked) {}
};

class CaseCollector : public RecursiveASTVisitor<CaseCollector> {
  ParentMap &PMap;
  SmallVectorImpl<CaseInfo> &Cases;

public:
  CaseCollector(ParentMap &PMap, SmallVectorImpl<CaseInfo> &Cases)
    : PMap(PMap), Cases(Cases) { }

  bool VisitSwitchStmt(SwitchStmt *S) {
    SwitchCase *Curr = S->getSwitchCaseList();
    if (!Curr)
      return true;
    Stmt *Parent = getCaseParent(Curr);
    Curr = Curr->getNextSwitchCase();
    // Make sure all case statements are in the same scope.
    while (Curr) {
      if (getCaseParent(Curr) != Parent)
        return true;
      Curr = Curr->getNextSwitchCase();
    }

    SourceLocation NextLoc = S->getEndLoc();
    Curr = S->getSwitchCaseList();
    // We iterate over case statements in reverse source-order.
    while (Curr) {
      Cases.push_back(
          CaseInfo(Curr, SourceRange(Curr->getBeginLoc(), NextLoc)));
      NextLoc = Curr->getBeginLoc();
      Curr = Curr->getNextSwitchCase();
    }
    return true;
  }

  Stmt *getCaseParent(SwitchCase *S) {
    Stmt *Parent = PMap.getParent(S);
    while (Parent && (isa<SwitchCase>(Parent) || isa<LabelStmt>(Parent)))
      Parent = PMap.getParent(Parent);
    return Parent;
  }
};

class ProtectedScopeFixer {
  MigrationPass &Pass;
  SourceManager &SM;
  SmallVector<CaseInfo, 16> Cases;
  SmallVector<DeclRefExpr *, 16> LocalRefs;

public:
  ProtectedScopeFixer(BodyContext &BodyCtx)
    : Pass(BodyCtx.getMigrationContext().Pass),
      SM(Pass.Ctx.getSourceManager()) {

    CaseCollector(BodyCtx.getParentMap(), Cases)
        .TraverseStmt(BodyCtx.getTopStmt());
    LocalRefsCollector(LocalRefs).TraverseStmt(BodyCtx.getTopStmt());

    SourceRange BodyRange = BodyCtx.getTopStmt()->getSourceRange();
    const CapturedDiagList &DiagList = Pass.getDiags();
    // Copy the diagnostics so we don't have to worry about invaliding iterators
    // from the diagnostic list.
    SmallVector<StoredDiagnostic, 16> StoredDiags;
    StoredDiags.append(DiagList.begin(), DiagList.end());
    SmallVectorImpl<StoredDiagnostic>::iterator
        I = StoredDiags.begin(), E = StoredDiags.end();
    while (I != E) {
      if (I->getID() == diag::err_switch_into_protected_scope &&
          isInRange(I->getLocation(), BodyRange)) {
        handleProtectedScopeError(I, E);
        continue;
      }
      ++I;
    }
  }

  void handleProtectedScopeError(
                             SmallVectorImpl<StoredDiagnostic>::iterator &DiagI,
                             SmallVectorImpl<StoredDiagnostic>::iterator DiagE){
    Transaction Trans(Pass.TA);
    assert(DiagI->getID() == diag::err_switch_into_protected_scope);
    SourceLocation ErrLoc = DiagI->getLocation();
    bool handledAllNotes = true;
    ++DiagI;
    for (; DiagI != DiagE && DiagI->getLevel() == DiagnosticsEngine::Note;
         ++DiagI) {
      if (!handleProtectedNote(*DiagI))
        handledAllNotes = false;
    }

    if (handledAllNotes)
      Pass.TA.clearDiagnostic(diag::err_switch_into_protected_scope, ErrLoc);
  }

  bool handleProtectedNote(const StoredDiagnostic &Diag) {
    assert(Diag.getLevel() == DiagnosticsEngine::Note);

    for (unsigned i = 0; i != Cases.size(); i++) {
      CaseInfo &info = Cases[i];
      if (isInRange(Diag.getLocation(), info.Range)) {

        if (info.State == CaseInfo::St_Unchecked)
          tryFixing(info);
        assert(info.State != CaseInfo::St_Unchecked);

        if (info.State == CaseInfo::St_Fixed) {
          Pass.TA.clearDiagnostic(Diag.getID(), Diag.getLocation());
          return true;
        }
        return false;
      }
    }

    return false;
  }

  void tryFixing(CaseInfo &info) {
    assert(info.State == CaseInfo::St_Unchecked);
    if (hasVarReferencedOutside(info)) {
      info.State = CaseInfo::St_CannotFix;
      return;
    }

    Pass.TA.insertAfterToken(info.SC->getColonLoc(), " {");
    Pass.TA.insert(info.Range.getEnd(), "}\n");
    info.State = CaseInfo::St_Fixed;
  }

  bool hasVarReferencedOutside(CaseInfo &info) {
    for (unsigned i = 0, e = LocalRefs.size(); i != e; ++i) {
      DeclRefExpr *DRE = LocalRefs[i];
      if (isInRange(DRE->getDecl()->getLocation(), info.Range) &&
          !isInRange(DRE->getLocation(), info.Range))
        return true;
    }
    return false;
  }

  bool isInRange(SourceLocation Loc, SourceRange R) {
    if (Loc.isInvalid())
      return false;
    return !SM.isBeforeInTranslationUnit(Loc, R.getBegin()) &&
            SM.isBeforeInTranslationUnit(Loc, R.getEnd());
  }
};

} // anonymous namespace

void ProtectedScopeTraverser::traverseBody(BodyContext &BodyCtx) {
  ProtectedScopeFixer Fix(BodyCtx);
}
