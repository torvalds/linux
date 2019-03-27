//== DivZeroChecker.cpp - Division by zero checker --------------*- C++ -*--==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This defines DivZeroChecker, a builtin check in ExprEngine that performs
// checks for division by zeros.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"

using namespace clang;
using namespace ento;

namespace {
class DivZeroChecker : public Checker< check::PreStmt<BinaryOperator> > {
  mutable std::unique_ptr<BuiltinBug> BT;
  void reportBug(const char *Msg, ProgramStateRef StateZero, CheckerContext &C,
                 std::unique_ptr<BugReporterVisitor> Visitor = nullptr) const;

public:
  void checkPreStmt(const BinaryOperator *B, CheckerContext &C) const;
};
} // end anonymous namespace

static const Expr *getDenomExpr(const ExplodedNode *N) {
  const Stmt *S = N->getLocationAs<PreStmt>()->getStmt();
  if (const auto *BE = dyn_cast<BinaryOperator>(S))
    return BE->getRHS();
  return nullptr;
}

void DivZeroChecker::reportBug(
    const char *Msg, ProgramStateRef StateZero, CheckerContext &C,
    std::unique_ptr<BugReporterVisitor> Visitor) const {
  if (ExplodedNode *N = C.generateErrorNode(StateZero)) {
    if (!BT)
      BT.reset(new BuiltinBug(this, "Division by zero"));

    auto R = llvm::make_unique<BugReport>(*BT, Msg, N);
    R->addVisitor(std::move(Visitor));
    bugreporter::trackExpressionValue(N, getDenomExpr(N), *R);
    C.emitReport(std::move(R));
  }
}

void DivZeroChecker::checkPreStmt(const BinaryOperator *B,
                                  CheckerContext &C) const {
  BinaryOperator::Opcode Op = B->getOpcode();
  if (Op != BO_Div &&
      Op != BO_Rem &&
      Op != BO_DivAssign &&
      Op != BO_RemAssign)
    return;

  if (!B->getRHS()->getType()->isScalarType())
    return;

  SVal Denom = C.getSVal(B->getRHS());
  Optional<DefinedSVal> DV = Denom.getAs<DefinedSVal>();

  // Divide-by-undefined handled in the generic checking for uses of
  // undefined values.
  if (!DV)
    return;

  // Check for divide by zero.
  ConstraintManager &CM = C.getConstraintManager();
  ProgramStateRef stateNotZero, stateZero;
  std::tie(stateNotZero, stateZero) = CM.assumeDual(C.getState(), *DV);

  if (!stateNotZero) {
    assert(stateZero);
    reportBug("Division by zero", stateZero, C);
    return;
  }

  bool TaintedD = C.getState()->isTainted(*DV);
  if ((stateNotZero && stateZero && TaintedD)) {
    reportBug("Division by a tainted value, possibly zero", stateZero, C,
              llvm::make_unique<TaintBugVisitor>(*DV));
    return;
  }

  // If we get here, then the denom should not be zero. We abandon the implicit
  // zero denom case for now.
  C.addTransition(stateNotZero);
}

void ento::registerDivZeroChecker(CheckerManager &mgr) {
  mgr.registerChecker<DivZeroChecker>();
}
