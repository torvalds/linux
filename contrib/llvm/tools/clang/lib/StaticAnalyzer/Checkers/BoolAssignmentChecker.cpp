//== BoolAssignmentChecker.cpp - Boolean assignment checker -----*- C++ -*--==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This defines BoolAssignmentChecker, a builtin check in ExprEngine that
// performs checks for assignment of non-Boolean values to Boolean variables.
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
  class BoolAssignmentChecker : public Checker< check::Bind > {
    mutable std::unique_ptr<BuiltinBug> BT;
    void emitReport(ProgramStateRef state, CheckerContext &C) const;
  public:
    void checkBind(SVal loc, SVal val, const Stmt *S, CheckerContext &C) const;
  };
} // end anonymous namespace

void BoolAssignmentChecker::emitReport(ProgramStateRef state,
                                       CheckerContext &C) const {
  if (ExplodedNode *N = C.generateNonFatalErrorNode(state)) {
    if (!BT)
      BT.reset(new BuiltinBug(this, "Assignment of a non-Boolean value"));
    C.emitReport(llvm::make_unique<BugReport>(*BT, BT->getDescription(), N));
  }
}

static bool isBooleanType(QualType Ty) {
  if (Ty->isBooleanType()) // C++ or C99
    return true;

  if (const TypedefType *TT = Ty->getAs<TypedefType>())
    return TT->getDecl()->getName() == "BOOL"   || // Objective-C
           TT->getDecl()->getName() == "_Bool"  || // stdbool.h < C99
           TT->getDecl()->getName() == "Boolean";  // MacTypes.h

  return false;
}

void BoolAssignmentChecker::checkBind(SVal loc, SVal val, const Stmt *S,
                                      CheckerContext &C) const {

  // We are only interested in stores into Booleans.
  const TypedValueRegion *TR =
    dyn_cast_or_null<TypedValueRegion>(loc.getAsRegion());

  if (!TR)
    return;

  QualType valTy = TR->getValueType();

  if (!isBooleanType(valTy))
    return;

  // Get the value of the right-hand side.  We only care about values
  // that are defined (UnknownVals and UndefinedVals are handled by other
  // checkers).
  Optional<DefinedSVal> DV = val.getAs<DefinedSVal>();
  if (!DV)
    return;

  // Check if the assigned value meets our criteria for correctness.  It must
  // be a value that is either 0 or 1.  One way to check this is to see if
  // the value is possibly < 0 (for a negative value) or greater than 1.
  ProgramStateRef state = C.getState();
  SValBuilder &svalBuilder = C.getSValBuilder();
  ConstraintManager &CM = C.getConstraintManager();

  // First, ensure that the value is >= 0.
  DefinedSVal zeroVal = svalBuilder.makeIntVal(0, valTy);
  SVal greaterThanOrEqualToZeroVal =
    svalBuilder.evalBinOp(state, BO_GE, *DV, zeroVal,
                          svalBuilder.getConditionType());

  Optional<DefinedSVal> greaterThanEqualToZero =
      greaterThanOrEqualToZeroVal.getAs<DefinedSVal>();

  if (!greaterThanEqualToZero) {
    // The SValBuilder cannot construct a valid SVal for this condition.
    // This means we cannot properly reason about it.
    return;
  }

  ProgramStateRef stateLT, stateGE;
  std::tie(stateGE, stateLT) = CM.assumeDual(state, *greaterThanEqualToZero);

  // Is it possible for the value to be less than zero?
  if (stateLT) {
    // It is possible for the value to be less than zero.  We only
    // want to emit a warning, however, if that value is fully constrained.
    // If it it possible for the value to be >= 0, then essentially the
    // value is underconstrained and there is nothing left to be done.
    if (!stateGE)
      emitReport(stateLT, C);

    // In either case, we are done.
    return;
  }

  // If we reach here, it must be the case that the value is constrained
  // to only be >= 0.
  assert(stateGE == state);

  // At this point we know that the value is >= 0.
  // Now check to ensure that the value is <= 1.
  DefinedSVal OneVal = svalBuilder.makeIntVal(1, valTy);
  SVal lessThanEqToOneVal =
    svalBuilder.evalBinOp(state, BO_LE, *DV, OneVal,
                          svalBuilder.getConditionType());

  Optional<DefinedSVal> lessThanEqToOne =
      lessThanEqToOneVal.getAs<DefinedSVal>();

  if (!lessThanEqToOne) {
    // The SValBuilder cannot construct a valid SVal for this condition.
    // This means we cannot properly reason about it.
    return;
  }

  ProgramStateRef stateGT, stateLE;
  std::tie(stateLE, stateGT) = CM.assumeDual(state, *lessThanEqToOne);

  // Is it possible for the value to be greater than one?
  if (stateGT) {
    // It is possible for the value to be greater than one.  We only
    // want to emit a warning, however, if that value is fully constrained.
    // If it is possible for the value to be <= 1, then essentially the
    // value is underconstrained and there is nothing left to be done.
    if (!stateLE)
      emitReport(stateGT, C);

    // In either case, we are done.
    return;
  }

  // If we reach here, it must be the case that the value is constrained
  // to only be <= 1.
  assert(stateLE == state);
}

void ento::registerBoolAssignmentChecker(CheckerManager &mgr) {
    mgr.registerChecker<BoolAssignmentChecker>();
}
