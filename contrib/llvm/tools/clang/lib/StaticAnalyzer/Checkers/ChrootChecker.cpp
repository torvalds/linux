//===- Chrootchecker.cpp -------- Basic security checks ---------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines chroot checker, which checks improper use of chroot.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramStateTrait.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SymbolManager.h"

using namespace clang;
using namespace ento;

namespace {

// enum value that represent the jail state
enum Kind { NO_CHROOT, ROOT_CHANGED, JAIL_ENTERED };

bool isRootChanged(intptr_t k) { return k == ROOT_CHANGED; }
//bool isJailEntered(intptr_t k) { return k == JAIL_ENTERED; }

// This checker checks improper use of chroot.
// The state transition:
// NO_CHROOT ---chroot(path)--> ROOT_CHANGED ---chdir(/) --> JAIL_ENTERED
//                                  |                               |
//         ROOT_CHANGED<--chdir(..)--      JAIL_ENTERED<--chdir(..)--
//                                  |                               |
//                      bug<--foo()--          JAIL_ENTERED<--foo()--
class ChrootChecker : public Checker<eval::Call, check::PreStmt<CallExpr> > {
  mutable IdentifierInfo *II_chroot, *II_chdir;
  // This bug refers to possibly break out of a chroot() jail.
  mutable std::unique_ptr<BuiltinBug> BT_BreakJail;

public:
  ChrootChecker() : II_chroot(nullptr), II_chdir(nullptr) {}

  static void *getTag() {
    static int x;
    return &x;
  }

  bool evalCall(const CallExpr *CE, CheckerContext &C) const;
  void checkPreStmt(const CallExpr *CE, CheckerContext &C) const;

private:
  void Chroot(CheckerContext &C, const CallExpr *CE) const;
  void Chdir(CheckerContext &C, const CallExpr *CE) const;
};

} // end anonymous namespace

bool ChrootChecker::evalCall(const CallExpr *CE, CheckerContext &C) const {
  const FunctionDecl *FD = C.getCalleeDecl(CE);
  if (!FD)
    return false;

  ASTContext &Ctx = C.getASTContext();
  if (!II_chroot)
    II_chroot = &Ctx.Idents.get("chroot");
  if (!II_chdir)
    II_chdir = &Ctx.Idents.get("chdir");

  if (FD->getIdentifier() == II_chroot) {
    Chroot(C, CE);
    return true;
  }
  if (FD->getIdentifier() == II_chdir) {
    Chdir(C, CE);
    return true;
  }

  return false;
}

void ChrootChecker::Chroot(CheckerContext &C, const CallExpr *CE) const {
  ProgramStateRef state = C.getState();
  ProgramStateManager &Mgr = state->getStateManager();

  // Once encouter a chroot(), set the enum value ROOT_CHANGED directly in
  // the GDM.
  state = Mgr.addGDM(state, ChrootChecker::getTag(), (void*) ROOT_CHANGED);
  C.addTransition(state);
}

void ChrootChecker::Chdir(CheckerContext &C, const CallExpr *CE) const {
  ProgramStateRef state = C.getState();
  ProgramStateManager &Mgr = state->getStateManager();

  // If there are no jail state in the GDM, just return.
  const void *k = state->FindGDM(ChrootChecker::getTag());
  if (!k)
    return;

  // After chdir("/"), enter the jail, set the enum value JAIL_ENTERED.
  const Expr *ArgExpr = CE->getArg(0);
  SVal ArgVal = C.getSVal(ArgExpr);

  if (const MemRegion *R = ArgVal.getAsRegion()) {
    R = R->StripCasts();
    if (const StringRegion* StrRegion= dyn_cast<StringRegion>(R)) {
      const StringLiteral* Str = StrRegion->getStringLiteral();
      if (Str->getString() == "/")
        state = Mgr.addGDM(state, ChrootChecker::getTag(),
                           (void*) JAIL_ENTERED);
    }
  }

  C.addTransition(state);
}

// Check the jail state before any function call except chroot and chdir().
void ChrootChecker::checkPreStmt(const CallExpr *CE, CheckerContext &C) const {
  const FunctionDecl *FD = C.getCalleeDecl(CE);
  if (!FD)
    return;

  ASTContext &Ctx = C.getASTContext();
  if (!II_chroot)
    II_chroot = &Ctx.Idents.get("chroot");
  if (!II_chdir)
    II_chdir = &Ctx.Idents.get("chdir");

  // Ignore chroot and chdir.
  if (FD->getIdentifier() == II_chroot || FD->getIdentifier() == II_chdir)
    return;

  // If jail state is ROOT_CHANGED, generate BugReport.
  void *const* k = C.getState()->FindGDM(ChrootChecker::getTag());
  if (k)
    if (isRootChanged((intptr_t) *k))
      if (ExplodedNode *N = C.generateNonFatalErrorNode()) {
        if (!BT_BreakJail)
          BT_BreakJail.reset(new BuiltinBug(
              this, "Break out of jail", "No call of chdir(\"/\") immediately "
                                         "after chroot"));
        C.emitReport(llvm::make_unique<BugReport>(
            *BT_BreakJail, BT_BreakJail->getDescription(), N));
      }
}

void ento::registerChrootChecker(CheckerManager &mgr) {
  mgr.registerChecker<ChrootChecker>();
}
