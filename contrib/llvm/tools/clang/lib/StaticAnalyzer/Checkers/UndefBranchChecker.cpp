//=== UndefBranchChecker.cpp -----------------------------------*- C++ -*--===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines UndefBranchChecker, which checks for undefined branch
// condition.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include <utility>

using namespace clang;
using namespace ento;

namespace {

class UndefBranchChecker : public Checker<check::BranchCondition> {
  mutable std::unique_ptr<BuiltinBug> BT;

  struct FindUndefExpr {
    ProgramStateRef St;
    const LocationContext *LCtx;

    FindUndefExpr(ProgramStateRef S, const LocationContext *L)
        : St(std::move(S)), LCtx(L) {}

    const Expr *FindExpr(const Expr *Ex) {
      if (!MatchesCriteria(Ex))
        return nullptr;

      for (const Stmt *SubStmt : Ex->children())
        if (const Expr *ExI = dyn_cast_or_null<Expr>(SubStmt))
          if (const Expr *E2 = FindExpr(ExI))
            return E2;

      return Ex;
    }

    bool MatchesCriteria(const Expr *Ex) {
      return St->getSVal(Ex, LCtx).isUndef();
    }
  };

public:
  void checkBranchCondition(const Stmt *Condition, CheckerContext &Ctx) const;
};

}

void UndefBranchChecker::checkBranchCondition(const Stmt *Condition,
                                              CheckerContext &Ctx) const {
  SVal X = Ctx.getSVal(Condition);
  if (X.isUndef()) {
    // Generate a sink node, which implicitly marks both outgoing branches as
    // infeasible.
    ExplodedNode *N = Ctx.generateErrorNode();
    if (N) {
      if (!BT)
        BT.reset(new BuiltinBug(
            this, "Branch condition evaluates to a garbage value"));

      // What's going on here: we want to highlight the subexpression of the
      // condition that is the most likely source of the "uninitialized
      // branch condition."  We do a recursive walk of the condition's
      // subexpressions and roughly look for the most nested subexpression
      // that binds to Undefined.  We then highlight that expression's range.

      // Get the predecessor node and check if is a PostStmt with the Stmt
      // being the terminator condition.  We want to inspect the state
      // of that node instead because it will contain main information about
      // the subexpressions.

      // Note: any predecessor will do.  They should have identical state,
      // since all the BlockEdge did was act as an error sink since the value
      // had to already be undefined.
      assert (!N->pred_empty());
      const Expr *Ex = cast<Expr>(Condition);
      ExplodedNode *PrevN = *N->pred_begin();
      ProgramPoint P = PrevN->getLocation();
      ProgramStateRef St = N->getState();

      if (Optional<PostStmt> PS = P.getAs<PostStmt>())
        if (PS->getStmt() == Ex)
          St = PrevN->getState();

      FindUndefExpr FindIt(St, Ctx.getLocationContext());
      Ex = FindIt.FindExpr(Ex);

      // Emit the bug report.
      auto R = llvm::make_unique<BugReport>(*BT, BT->getDescription(), N);
      bugreporter::trackExpressionValue(N, Ex, *R);
      R->addRange(Ex->getSourceRange());

      Ctx.emitReport(std::move(R));
    }
  }
}

void ento::registerUndefBranchChecker(CheckerManager &mgr) {
  mgr.registerChecker<UndefBranchChecker>();
}
