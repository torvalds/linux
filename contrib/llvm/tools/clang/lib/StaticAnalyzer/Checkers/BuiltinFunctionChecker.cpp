//=== BuiltinFunctionChecker.cpp --------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This checker evaluates clang builtin functions.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/Basic/Builtins.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"

using namespace clang;
using namespace ento;

namespace {

class BuiltinFunctionChecker : public Checker<eval::Call> {
public:
  bool evalCall(const CallExpr *CE, CheckerContext &C) const;
};

}

bool BuiltinFunctionChecker::evalCall(const CallExpr *CE,
                                      CheckerContext &C) const {
  ProgramStateRef state = C.getState();
  const FunctionDecl *FD = C.getCalleeDecl(CE);
  const LocationContext *LCtx = C.getLocationContext();
  if (!FD)
    return false;

  switch (FD->getBuiltinID()) {
  default:
    return false;

  case Builtin::BI__builtin_assume: {
    assert (CE->arg_begin() != CE->arg_end());
    SVal ArgSVal = C.getSVal(CE->getArg(0));
    if (ArgSVal.isUndef())
      return true; // Return true to model purity.

    state = state->assume(ArgSVal.castAs<DefinedOrUnknownSVal>(), true);
    // FIXME: do we want to warn here? Not right now. The most reports might
    // come from infeasible paths, thus being false positives.
    if (!state) {
      C.generateSink(C.getState(), C.getPredecessor());
      return true;
    }

    C.addTransition(state);
    return true;
  }

  case Builtin::BI__builtin_unpredictable:
  case Builtin::BI__builtin_expect:
  case Builtin::BI__builtin_assume_aligned:
  case Builtin::BI__builtin_addressof: {
    // For __builtin_unpredictable, __builtin_expect, and
    // __builtin_assume_aligned, just return the value of the subexpression.
    // __builtin_addressof is going from a reference to a pointer, but those
    // are represented the same way in the analyzer.
    assert (CE->arg_begin() != CE->arg_end());
    SVal X = C.getSVal(*(CE->arg_begin()));
    C.addTransition(state->BindExpr(CE, LCtx, X));
    return true;
  }

  case Builtin::BI__builtin_alloca_with_align:
  case Builtin::BI__builtin_alloca: {
    // FIXME: Refactor into StoreManager itself?
    MemRegionManager& RM = C.getStoreManager().getRegionManager();
    const AllocaRegion* R =
      RM.getAllocaRegion(CE, C.blockCount(), C.getLocationContext());

    // Set the extent of the region in bytes. This enables us to use the
    // SVal of the argument directly. If we save the extent in bits, we
    // cannot represent values like symbol*8.
    auto Size = C.getSVal(*(CE->arg_begin())).castAs<DefinedOrUnknownSVal>();

    SValBuilder& svalBuilder = C.getSValBuilder();
    DefinedOrUnknownSVal Extent = R->getExtent(svalBuilder);
    DefinedOrUnknownSVal extentMatchesSizeArg =
      svalBuilder.evalEQ(state, Extent, Size);
    state = state->assume(extentMatchesSizeArg, true);
    assert(state && "The region should not have any previous constraints");

    C.addTransition(state->BindExpr(CE, LCtx, loc::MemRegionVal(R)));
    return true;
  }

  case Builtin::BI__builtin_object_size:
  case Builtin::BI__builtin_constant_p: {
    // This must be resolvable at compile time, so we defer to the constant
    // evaluator for a value.
    SVal V = UnknownVal();
    Expr::EvalResult EVResult;
    if (CE->EvaluateAsInt(EVResult, C.getASTContext(), Expr::SE_NoSideEffects)) {
      // Make sure the result has the correct type.
      llvm::APSInt Result = EVResult.Val.getInt();
      SValBuilder &SVB = C.getSValBuilder();
      BasicValueFactory &BVF = SVB.getBasicValueFactory();
      BVF.getAPSIntType(CE->getType()).apply(Result);
      V = SVB.makeIntVal(Result);
    }

    C.addTransition(state->BindExpr(CE, LCtx, V));
    return true;
  }
  }
}

void ento::registerBuiltinFunctionChecker(CheckerManager &mgr) {
  mgr.registerChecker<BuiltinFunctionChecker>();
}
