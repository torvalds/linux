//===- LoopRotation.cpp - Loop Rotation Pass ------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements Loop Rotation Pass.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/LoopRotation.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/Analysis/MemorySSAUpdater.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/LoopPassManager.h"
#include "llvm/Transforms/Utils/LoopRotationUtils.h"
#include "llvm/Transforms/Utils/LoopUtils.h"
using namespace llvm;

#define DEBUG_TYPE "loop-rotate"

static cl::opt<unsigned> DefaultRotationThreshold(
    "rotation-max-header-size", cl::init(16), cl::Hidden,
    cl::desc("The default maximum header size for automatic loop rotation"));

LoopRotatePass::LoopRotatePass(bool EnableHeaderDuplication)
    : EnableHeaderDuplication(EnableHeaderDuplication) {}

PreservedAnalyses LoopRotatePass::run(Loop &L, LoopAnalysisManager &AM,
                                      LoopStandardAnalysisResults &AR,
                                      LPMUpdater &) {
  int Threshold = EnableHeaderDuplication ? DefaultRotationThreshold : 0;
  const DataLayout &DL = L.getHeader()->getModule()->getDataLayout();
  const SimplifyQuery SQ = getBestSimplifyQuery(AR, DL);

  Optional<MemorySSAUpdater> MSSAU;
  if (AR.MSSA)
    MSSAU = MemorySSAUpdater(AR.MSSA);
  bool Changed = LoopRotation(&L, &AR.LI, &AR.TTI, &AR.AC, &AR.DT, &AR.SE,
                              MSSAU.hasValue() ? MSSAU.getPointer() : nullptr,
                              SQ, false, Threshold, false);

  if (!Changed)
    return PreservedAnalyses::all();

  if (AR.MSSA && VerifyMemorySSA)
    AR.MSSA->verifyMemorySSA();

  return getLoopPassPreservedAnalyses();
}

namespace {

class LoopRotateLegacyPass : public LoopPass {
  unsigned MaxHeaderSize;

public:
  static char ID; // Pass ID, replacement for typeid
  LoopRotateLegacyPass(int SpecifiedMaxHeaderSize = -1) : LoopPass(ID) {
    initializeLoopRotateLegacyPassPass(*PassRegistry::getPassRegistry());
    if (SpecifiedMaxHeaderSize == -1)
      MaxHeaderSize = DefaultRotationThreshold;
    else
      MaxHeaderSize = unsigned(SpecifiedMaxHeaderSize);
  }

  // LCSSA form makes instruction renaming easier.
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<AssumptionCacheTracker>();
    AU.addRequired<TargetTransformInfoWrapperPass>();
    if (EnableMSSALoopDependency) {
      AU.addRequired<MemorySSAWrapperPass>();
      AU.addPreserved<MemorySSAWrapperPass>();
    }
    getLoopAnalysisUsage(AU);
  }

  bool runOnLoop(Loop *L, LPPassManager &LPM) override {
    if (skipLoop(L))
      return false;
    Function &F = *L->getHeader()->getParent();

    auto *LI = &getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
    const auto *TTI = &getAnalysis<TargetTransformInfoWrapperPass>().getTTI(F);
    auto *AC = &getAnalysis<AssumptionCacheTracker>().getAssumptionCache(F);
    auto *DTWP = getAnalysisIfAvailable<DominatorTreeWrapperPass>();
    auto *DT = DTWP ? &DTWP->getDomTree() : nullptr;
    auto *SEWP = getAnalysisIfAvailable<ScalarEvolutionWrapperPass>();
    auto *SE = SEWP ? &SEWP->getSE() : nullptr;
    const SimplifyQuery SQ = getBestSimplifyQuery(*this, F);
    Optional<MemorySSAUpdater> MSSAU;
    if (EnableMSSALoopDependency) {
      MemorySSA *MSSA = &getAnalysis<MemorySSAWrapperPass>().getMSSA();
      MSSAU = MemorySSAUpdater(MSSA);
    }
    return LoopRotation(L, LI, TTI, AC, DT, SE,
                        MSSAU.hasValue() ? MSSAU.getPointer() : nullptr, SQ,
                        false, MaxHeaderSize, false);
  }
};
}

char LoopRotateLegacyPass::ID = 0;
INITIALIZE_PASS_BEGIN(LoopRotateLegacyPass, "loop-rotate", "Rotate Loops",
                      false, false)
INITIALIZE_PASS_DEPENDENCY(AssumptionCacheTracker)
INITIALIZE_PASS_DEPENDENCY(LoopPass)
INITIALIZE_PASS_DEPENDENCY(TargetTransformInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(MemorySSAWrapperPass)
INITIALIZE_PASS_END(LoopRotateLegacyPass, "loop-rotate", "Rotate Loops", false,
                    false)

Pass *llvm::createLoopRotatePass(int MaxHeaderSize) {
  return new LoopRotateLegacyPass(MaxHeaderSize);
}
