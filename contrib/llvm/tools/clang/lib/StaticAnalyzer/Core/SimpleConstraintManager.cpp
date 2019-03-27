//== SimpleConstraintManager.cpp --------------------------------*- C++ -*--==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines SimpleConstraintManager, a class that provides a
//  simplified constraint manager interface, compared to ConstraintManager.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Core/PathSensitive/SimpleConstraintManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/APSIntType.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ExprEngine.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState.h"

namespace clang {

namespace ento {

SimpleConstraintManager::~SimpleConstraintManager() {}

ProgramStateRef SimpleConstraintManager::assume(ProgramStateRef State,
                                                DefinedSVal Cond,
                                                bool Assumption) {
  // If we have a Loc value, cast it to a bool NonLoc first.
  if (Optional<Loc> LV = Cond.getAs<Loc>()) {
    SValBuilder &SVB = State->getStateManager().getSValBuilder();
    QualType T;
    const MemRegion *MR = LV->getAsRegion();
    if (const TypedRegion *TR = dyn_cast_or_null<TypedRegion>(MR))
      T = TR->getLocationType();
    else
      T = SVB.getContext().VoidPtrTy;

    Cond = SVB.evalCast(*LV, SVB.getContext().BoolTy, T).castAs<DefinedSVal>();
  }

  return assume(State, Cond.castAs<NonLoc>(), Assumption);
}

ProgramStateRef SimpleConstraintManager::assume(ProgramStateRef State,
                                                NonLoc Cond, bool Assumption) {
  State = assumeAux(State, Cond, Assumption);
  if (NotifyAssumeClients && SU)
    return SU->processAssume(State, Cond, Assumption);
  return State;
}

ProgramStateRef SimpleConstraintManager::assumeAux(ProgramStateRef State,
                                                   NonLoc Cond,
                                                   bool Assumption) {

  // We cannot reason about SymSymExprs, and can only reason about some
  // SymIntExprs.
  if (!canReasonAbout(Cond)) {
    // Just add the constraint to the expression without trying to simplify.
    SymbolRef Sym = Cond.getAsSymExpr();
    assert(Sym);
    return assumeSymUnsupported(State, Sym, Assumption);
  }

  switch (Cond.getSubKind()) {
  default:
    llvm_unreachable("'Assume' not implemented for this NonLoc");

  case nonloc::SymbolValKind: {
    nonloc::SymbolVal SV = Cond.castAs<nonloc::SymbolVal>();
    SymbolRef Sym = SV.getSymbol();
    assert(Sym);
    return assumeSym(State, Sym, Assumption);
  }

  case nonloc::ConcreteIntKind: {
    bool b = Cond.castAs<nonloc::ConcreteInt>().getValue() != 0;
    bool isFeasible = b ? Assumption : !Assumption;
    return isFeasible ? State : nullptr;
  }

  case nonloc::PointerToMemberKind: {
    bool IsNull = !Cond.castAs<nonloc::PointerToMember>().isNullMemberPointer();
    bool IsFeasible = IsNull ? Assumption : !Assumption;
    return IsFeasible ? State : nullptr;
  }

  case nonloc::LocAsIntegerKind:
    return assume(State, Cond.castAs<nonloc::LocAsInteger>().getLoc(),
                  Assumption);
  } // end switch
}

ProgramStateRef SimpleConstraintManager::assumeInclusiveRange(
    ProgramStateRef State, NonLoc Value, const llvm::APSInt &From,
    const llvm::APSInt &To, bool InRange) {

  assert(From.isUnsigned() == To.isUnsigned() &&
         From.getBitWidth() == To.getBitWidth() &&
         "Values should have same types!");

  if (!canReasonAbout(Value)) {
    // Just add the constraint to the expression without trying to simplify.
    SymbolRef Sym = Value.getAsSymExpr();
    assert(Sym);
    return assumeSymInclusiveRange(State, Sym, From, To, InRange);
  }

  switch (Value.getSubKind()) {
  default:
    llvm_unreachable("'assumeInclusiveRange' is not implemented"
                     "for this NonLoc");

  case nonloc::LocAsIntegerKind:
  case nonloc::SymbolValKind: {
    if (SymbolRef Sym = Value.getAsSymbol())
      return assumeSymInclusiveRange(State, Sym, From, To, InRange);
    return State;
  } // end switch

  case nonloc::ConcreteIntKind: {
    const llvm::APSInt &IntVal = Value.castAs<nonloc::ConcreteInt>().getValue();
    bool IsInRange = IntVal >= From && IntVal <= To;
    bool isFeasible = (IsInRange == InRange);
    return isFeasible ? State : nullptr;
  }
  } // end switch
}

} // end of namespace ento

} // end of namespace clang
