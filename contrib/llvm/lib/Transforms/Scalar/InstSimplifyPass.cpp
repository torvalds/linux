//===- InstSimplifyPass.cpp -----------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/InstSimplifyPass.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Type.h"
#include "llvm/Pass.h"
#include "llvm/Transforms/Utils.h"
#include "llvm/Transforms/Utils/Local.h"
using namespace llvm;

#define DEBUG_TYPE "instsimplify"

STATISTIC(NumSimplified, "Number of redundant instructions removed");

static bool runImpl(Function &F, const SimplifyQuery &SQ,
                    OptimizationRemarkEmitter *ORE) {
  SmallPtrSet<const Instruction *, 8> S1, S2, *ToSimplify = &S1, *Next = &S2;
  bool Changed = false;

  do {
    for (BasicBlock *BB : depth_first(&F.getEntryBlock())) {
      // Here be subtlety: the iterator must be incremented before the loop
      // body (not sure why), so a range-for loop won't work here.
      for (BasicBlock::iterator BI = BB->begin(), BE = BB->end(); BI != BE;) {
        Instruction *I = &*BI++;
        // The first time through the loop ToSimplify is empty and we try to
        // simplify all instructions.  On later iterations ToSimplify is not
        // empty and we only bother simplifying instructions that are in it.
        if (!ToSimplify->empty() && !ToSimplify->count(I))
          continue;

        // Don't waste time simplifying unused instructions.
        if (!I->use_empty()) {
          if (Value *V = SimplifyInstruction(I, SQ, ORE)) {
            // Mark all uses for resimplification next time round the loop.
            for (User *U : I->users())
              Next->insert(cast<Instruction>(U));
            I->replaceAllUsesWith(V);
            ++NumSimplified;
            Changed = true;
          }
        }
        if (RecursivelyDeleteTriviallyDeadInstructions(I, SQ.TLI)) {
          // RecursivelyDeleteTriviallyDeadInstruction can remove more than one
          // instruction, so simply incrementing the iterator does not work.
          // When instructions get deleted re-iterate instead.
          BI = BB->begin();
          BE = BB->end();
          Changed = true;
        }
      }
    }

    // Place the list of instructions to simplify on the next loop iteration
    // into ToSimplify.
    std::swap(ToSimplify, Next);
    Next->clear();
  } while (!ToSimplify->empty());

  return Changed;
}

namespace {
struct InstSimplifyLegacyPass : public FunctionPass {
  static char ID; // Pass identification, replacement for typeid
  InstSimplifyLegacyPass() : FunctionPass(ID) {
    initializeInstSimplifyLegacyPassPass(*PassRegistry::getPassRegistry());
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<DominatorTreeWrapperPass>();
    AU.addRequired<AssumptionCacheTracker>();
    AU.addRequired<TargetLibraryInfoWrapperPass>();
    AU.addRequired<OptimizationRemarkEmitterWrapperPass>();
  }

  /// runOnFunction - Remove instructions that simplify.
  bool runOnFunction(Function &F) override {
    if (skipFunction(F))
      return false;

    const DominatorTree *DT =
        &getAnalysis<DominatorTreeWrapperPass>().getDomTree();
    const TargetLibraryInfo *TLI =
        &getAnalysis<TargetLibraryInfoWrapperPass>().getTLI();
    AssumptionCache *AC =
        &getAnalysis<AssumptionCacheTracker>().getAssumptionCache(F);
    OptimizationRemarkEmitter *ORE =
        &getAnalysis<OptimizationRemarkEmitterWrapperPass>().getORE();
    const DataLayout &DL = F.getParent()->getDataLayout();
    const SimplifyQuery SQ(DL, TLI, DT, AC);
    return runImpl(F, SQ, ORE);
  }
};
} // namespace

char InstSimplifyLegacyPass::ID = 0;
INITIALIZE_PASS_BEGIN(InstSimplifyLegacyPass, "instsimplify",
                      "Remove redundant instructions", false, false)
INITIALIZE_PASS_DEPENDENCY(AssumptionCacheTracker)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(TargetLibraryInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(OptimizationRemarkEmitterWrapperPass)
INITIALIZE_PASS_END(InstSimplifyLegacyPass, "instsimplify",
                    "Remove redundant instructions", false, false)

// Public interface to the simplify instructions pass.
FunctionPass *llvm::createInstSimplifyLegacyPass() {
  return new InstSimplifyLegacyPass();
}

PreservedAnalyses InstSimplifyPass::run(Function &F,
                                        FunctionAnalysisManager &AM) {
  auto &DT = AM.getResult<DominatorTreeAnalysis>(F);
  auto &TLI = AM.getResult<TargetLibraryAnalysis>(F);
  auto &AC = AM.getResult<AssumptionAnalysis>(F);
  auto &ORE = AM.getResult<OptimizationRemarkEmitterAnalysis>(F);
  const DataLayout &DL = F.getParent()->getDataLayout();
  const SimplifyQuery SQ(DL, &TLI, &DT, &AC);
  bool Changed = runImpl(F, SQ, &ORE);
  if (!Changed)
    return PreservedAnalyses::all();

  PreservedAnalyses PA;
  PA.preserveSet<CFGAnalyses>();
  return PA;
}
