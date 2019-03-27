//===- LoopTransformWarning.cpp -  ----------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Emit warnings if forced code transformations have not been performed.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/WarnMissedTransforms.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Transforms/Utils/LoopUtils.h"

using namespace llvm;

#define DEBUG_TYPE "transform-warning"

/// Emit warnings for forced (i.e. user-defined) loop transformations which have
/// still not been performed.
static void warnAboutLeftoverTransformations(Loop *L,
                                             OptimizationRemarkEmitter *ORE) {
  if (hasUnrollTransformation(L) == TM_ForcedByUser) {
    LLVM_DEBUG(dbgs() << "Leftover unroll transformation\n");
    ORE->emit(
        DiagnosticInfoOptimizationFailure(DEBUG_TYPE,
                                          "FailedRequestedUnrolling",
                                          L->getStartLoc(), L->getHeader())
        << "loop not unrolled: the optimizer was unable to perform the "
           "requested transformation; the transformation might be disabled or "
           "specified as part of an unsupported transformation ordering");
  }

  if (hasUnrollAndJamTransformation(L) == TM_ForcedByUser) {
    LLVM_DEBUG(dbgs() << "Leftover unroll-and-jam transformation\n");
    ORE->emit(
        DiagnosticInfoOptimizationFailure(DEBUG_TYPE,
                                          "FailedRequestedUnrollAndJamming",
                                          L->getStartLoc(), L->getHeader())
        << "loop not unroll-and-jammed: the optimizer was unable to perform "
           "the requested transformation; the transformation might be disabled "
           "or specified as part of an unsupported transformation ordering");
  }

  if (hasVectorizeTransformation(L) == TM_ForcedByUser) {
    LLVM_DEBUG(dbgs() << "Leftover vectorization transformation\n");
    Optional<int> VectorizeWidth =
        getOptionalIntLoopAttribute(L, "llvm.loop.vectorize.width");
    Optional<int> InterleaveCount =
        getOptionalIntLoopAttribute(L, "llvm.loop.interleave.count");

    if (VectorizeWidth.getValueOr(0) != 1)
      ORE->emit(
          DiagnosticInfoOptimizationFailure(DEBUG_TYPE,
                                            "FailedRequestedVectorization",
                                            L->getStartLoc(), L->getHeader())
          << "loop not vectorized: the optimizer was unable to perform the "
             "requested transformation; the transformation might be disabled "
             "or specified as part of an unsupported transformation ordering");
    else if (InterleaveCount.getValueOr(0) != 1)
      ORE->emit(
          DiagnosticInfoOptimizationFailure(DEBUG_TYPE,
                                            "FailedRequestedInterleaving",
                                            L->getStartLoc(), L->getHeader())
          << "loop not interleaved: the optimizer was unable to perform the "
             "requested transformation; the transformation might be disabled "
             "or specified as part of an unsupported transformation ordering");
  }

  if (hasDistributeTransformation(L) == TM_ForcedByUser) {
    LLVM_DEBUG(dbgs() << "Leftover distribute transformation\n");
    ORE->emit(
        DiagnosticInfoOptimizationFailure(DEBUG_TYPE,
                                          "FailedRequestedDistribution",
                                          L->getStartLoc(), L->getHeader())
        << "loop not distributed: the optimizer was unable to perform the "
           "requested transformation; the transformation might be disabled or "
           "specified as part of an unsupported transformation ordering");
  }
}

static void warnAboutLeftoverTransformations(Function *F, LoopInfo *LI,
                                             OptimizationRemarkEmitter *ORE) {
  for (auto *L : LI->getLoopsInPreorder())
    warnAboutLeftoverTransformations(L, ORE);
}

// New pass manager boilerplate
PreservedAnalyses
WarnMissedTransformationsPass::run(Function &F, FunctionAnalysisManager &AM) {
  // Do not warn about not applied transformations if optimizations are
  // disabled.
  if (F.hasFnAttribute(Attribute::OptimizeNone))
    return PreservedAnalyses::all();

  auto &ORE = AM.getResult<OptimizationRemarkEmitterAnalysis>(F);
  auto &LI = AM.getResult<LoopAnalysis>(F);

  warnAboutLeftoverTransformations(&F, &LI, &ORE);

  return PreservedAnalyses::all();
}

// Legacy pass manager boilerplate
namespace {
class WarnMissedTransformationsLegacy : public FunctionPass {
public:
  static char ID;

  explicit WarnMissedTransformationsLegacy() : FunctionPass(ID) {
    initializeWarnMissedTransformationsLegacyPass(
        *PassRegistry::getPassRegistry());
  }

  bool runOnFunction(Function &F) override {
    if (skipFunction(F))
      return false;

    auto &ORE = getAnalysis<OptimizationRemarkEmitterWrapperPass>().getORE();
    auto &LI = getAnalysis<LoopInfoWrapperPass>().getLoopInfo();

    warnAboutLeftoverTransformations(&F, &LI, &ORE);
    return false;
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<OptimizationRemarkEmitterWrapperPass>();
    AU.addRequired<LoopInfoWrapperPass>();

    AU.setPreservesAll();
  }
};
} // end anonymous namespace

char WarnMissedTransformationsLegacy::ID = 0;

INITIALIZE_PASS_BEGIN(WarnMissedTransformationsLegacy, "transform-warning",
                      "Warn about non-applied transformations", false, false)
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(OptimizationRemarkEmitterWrapperPass)
INITIALIZE_PASS_END(WarnMissedTransformationsLegacy, "transform-warning",
                    "Warn about non-applied transformations", false, false)

Pass *llvm::createWarnMissedTransformationsPass() {
  return new WarnMissedTransformationsLegacy();
}
