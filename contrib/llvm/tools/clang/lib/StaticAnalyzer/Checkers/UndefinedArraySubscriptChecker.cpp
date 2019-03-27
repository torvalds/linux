//===--- UndefinedArraySubscriptChecker.h ----------------------*- C++ -*--===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This defines UndefinedArraySubscriptChecker, a builtin check in ExprEngine
// that performs checks for undefined array subscripts.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/AST/DeclCXX.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"

using namespace clang;
using namespace ento;

namespace {
class UndefinedArraySubscriptChecker
  : public Checker< check::PreStmt<ArraySubscriptExpr> > {
  mutable std::unique_ptr<BugType> BT;

public:
  void checkPreStmt(const ArraySubscriptExpr *A, CheckerContext &C) const;
};
} // end anonymous namespace

void
UndefinedArraySubscriptChecker::checkPreStmt(const ArraySubscriptExpr *A,
                                             CheckerContext &C) const {
  const Expr *Index = A->getIdx();
  if (!C.getSVal(Index).isUndef())
    return;

  // Sema generates anonymous array variables for copying array struct fields.
  // Don't warn if we're in an implicitly-generated constructor.
  const Decl *D = C.getLocationContext()->getDecl();
  if (const CXXConstructorDecl *Ctor = dyn_cast<CXXConstructorDecl>(D))
    if (Ctor->isDefaulted())
      return;

  ExplodedNode *N = C.generateErrorNode();
  if (!N)
    return;
  if (!BT)
    BT.reset(new BuiltinBug(this, "Array subscript is undefined"));

  // Generate a report for this bug.
  auto R = llvm::make_unique<BugReport>(*BT, BT->getName(), N);
  R->addRange(A->getIdx()->getSourceRange());
  bugreporter::trackExpressionValue(N, A->getIdx(), *R);
  C.emitReport(std::move(R));
}

void ento::registerUndefinedArraySubscriptChecker(CheckerManager &mgr) {
  mgr.registerChecker<UndefinedArraySubscriptChecker>();
}
