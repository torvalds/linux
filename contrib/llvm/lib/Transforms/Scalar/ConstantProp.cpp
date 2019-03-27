//===- ConstantProp.cpp - Code to perform Simple Constant Propagation -----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements constant propagation and merging:
//
// Specifically, this:
//   * Converts instructions like "add int 1, 2" into 3
//
// Notice that:
//   * This pass has a habit of making definitions be dead.  It is a good idea
//     to run a DIE pass sometime after running this pass.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instruction.h"
#include "llvm/Pass.h"
#include "llvm/Support/DebugCounter.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils/Local.h"
using namespace llvm;

#define DEBUG_TYPE "constprop"

STATISTIC(NumInstKilled, "Number of instructions killed");
DEBUG_COUNTER(CPCounter, "constprop-transform",
              "Controls which instructions are killed");

namespace {
  struct ConstantPropagation : public FunctionPass {
    static char ID; // Pass identification, replacement for typeid
    ConstantPropagation() : FunctionPass(ID) {
      initializeConstantPropagationPass(*PassRegistry::getPassRegistry());
    }

    bool runOnFunction(Function &F) override;

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.setPreservesCFG();
      AU.addRequired<TargetLibraryInfoWrapperPass>();
    }
  };
}

char ConstantPropagation::ID = 0;
INITIALIZE_PASS_BEGIN(ConstantPropagation, "constprop",
                "Simple constant propagation", false, false)
INITIALIZE_PASS_DEPENDENCY(TargetLibraryInfoWrapperPass)
INITIALIZE_PASS_END(ConstantPropagation, "constprop",
                "Simple constant propagation", false, false)

FunctionPass *llvm::createConstantPropagationPass() {
  return new ConstantPropagation();
}

bool ConstantPropagation::runOnFunction(Function &F) {
  if (skipFunction(F))
    return false;

  // Initialize the worklist to all of the instructions ready to process...
  SmallPtrSet<Instruction *, 16> WorkList;
  // The SmallVector of WorkList ensures that we do iteration at stable order.
  // We use two containers rather than one SetVector, since remove is
  // linear-time, and we don't care enough to remove from Vec.
  SmallVector<Instruction *, 16> WorkListVec;
  for (Instruction &I : instructions(&F)) {
    WorkList.insert(&I);
    WorkListVec.push_back(&I);
  }

  bool Changed = false;
  const DataLayout &DL = F.getParent()->getDataLayout();
  TargetLibraryInfo *TLI =
      &getAnalysis<TargetLibraryInfoWrapperPass>().getTLI();

  while (!WorkList.empty()) {
    SmallVector<Instruction*, 16> NewWorkListVec;
    for (auto *I : WorkListVec) {
      WorkList.erase(I); // Remove element from the worklist...

      if (!I->use_empty()) // Don't muck with dead instructions...
        if (Constant *C = ConstantFoldInstruction(I, DL, TLI)) {
          if (!DebugCounter::shouldExecute(CPCounter))
            continue;

          // Add all of the users of this instruction to the worklist, they might
          // be constant propagatable now...
          for (User *U : I->users()) {
            // If user not in the set, then add it to the vector.
            if (WorkList.insert(cast<Instruction>(U)).second)
              NewWorkListVec.push_back(cast<Instruction>(U));
          }

          // Replace all of the uses of a variable with uses of the constant.
          I->replaceAllUsesWith(C);

          if (isInstructionTriviallyDead(I, TLI)) {
            I->eraseFromParent();
            ++NumInstKilled;
          }

          // We made a change to the function...
          Changed = true;
        }
    }
    WorkListVec = std::move(NewWorkListVec);
  }
  return Changed;
}
