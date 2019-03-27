//===---- BDCE.cpp - Bit-tracking dead code elimination -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the Bit-Tracking Dead Code Elimination pass. Some
// instructions (shifts, some ands, ors, etc.) kill some of their input bits.
// We track these dead bits and remove instructions that compute only these
// dead bits.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/BDCE.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/DemandedBits.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Scalar.h"
using namespace llvm;

#define DEBUG_TYPE "bdce"

STATISTIC(NumRemoved, "Number of instructions removed (unused)");
STATISTIC(NumSimplified, "Number of instructions trivialized (dead bits)");

/// If an instruction is trivialized (dead), then the chain of users of that
/// instruction may need to be cleared of assumptions that can no longer be
/// guaranteed correct.
static void clearAssumptionsOfUsers(Instruction *I, DemandedBits &DB) {
  assert(I->getType()->isIntOrIntVectorTy() &&
         "Trivializing a non-integer value?");

  // Initialize the worklist with eligible direct users.
  SmallVector<Instruction *, 16> WorkList;
  for (User *JU : I->users()) {
    // If all bits of a user are demanded, then we know that nothing below that
    // in the def-use chain needs to be changed.
    auto *J = dyn_cast<Instruction>(JU);
    if (J && J->getType()->isIntOrIntVectorTy() &&
        !DB.getDemandedBits(J).isAllOnesValue())
      WorkList.push_back(J);

    // Note that we need to check for non-int types above before asking for
    // demanded bits. Normally, the only way to reach an instruction with an
    // non-int type is via an instruction that has side effects (or otherwise
    // will demand its input bits). However, if we have a readnone function
    // that returns an unsized type (e.g., void), we must avoid asking for the
    // demanded bits of the function call's return value. A void-returning
    // readnone function is always dead (and so we can stop walking the use/def
    // chain here), but the check is necessary to avoid asserting.
  }

  // DFS through subsequent users while tracking visits to avoid cycles.
  SmallPtrSet<Instruction *, 16> Visited;
  while (!WorkList.empty()) {
    Instruction *J = WorkList.pop_back_val();

    // NSW, NUW, and exact are based on operands that might have changed.
    J->dropPoisonGeneratingFlags();

    // We do not have to worry about llvm.assume or range metadata:
    // 1. llvm.assume demands its operand, so trivializing can't change it.
    // 2. range metadata only applies to memory accesses which demand all bits.

    Visited.insert(J);

    for (User *KU : J->users()) {
      // If all bits of a user are demanded, then we know that nothing below
      // that in the def-use chain needs to be changed.
      auto *K = dyn_cast<Instruction>(KU);
      if (K && !Visited.count(K) && K->getType()->isIntOrIntVectorTy() &&
          !DB.getDemandedBits(K).isAllOnesValue())
        WorkList.push_back(K);
    }
  }
}

static bool bitTrackingDCE(Function &F, DemandedBits &DB) {
  SmallVector<Instruction*, 128> Worklist;
  bool Changed = false;
  for (Instruction &I : instructions(F)) {
    // If the instruction has side effects and no non-dbg uses,
    // skip it. This way we avoid computing known bits on an instruction
    // that will not help us.
    if (I.mayHaveSideEffects() && I.use_empty())
      continue;

    // Remove instructions that are dead, either because they were not reached
    // during analysis or have no demanded bits.
    if (DB.isInstructionDead(&I) ||
        (I.getType()->isIntOrIntVectorTy() &&
         DB.getDemandedBits(&I).isNullValue() &&
         wouldInstructionBeTriviallyDead(&I))) {
      salvageDebugInfo(I);
      Worklist.push_back(&I);
      I.dropAllReferences();
      Changed = true;
      continue;
    }

    for (Use &U : I.operands()) {
      // DemandedBits only detects dead integer uses.
      if (!U->getType()->isIntOrIntVectorTy())
        continue;

      if (!isa<Instruction>(U) && !isa<Argument>(U))
        continue;

      if (!DB.isUseDead(&U))
        continue;

      LLVM_DEBUG(dbgs() << "BDCE: Trivializing: " << U << " (all bits dead)\n");

      clearAssumptionsOfUsers(&I, DB);

      // FIXME: In theory we could substitute undef here instead of zero.
      // This should be reconsidered once we settle on the semantics of
      // undef, poison, etc.
      U.set(ConstantInt::get(U->getType(), 0));
      ++NumSimplified;
      Changed = true;
    }
  }

  for (Instruction *&I : Worklist) {
    ++NumRemoved;
    I->eraseFromParent();
  }

  return Changed;
}

PreservedAnalyses BDCEPass::run(Function &F, FunctionAnalysisManager &AM) {
  auto &DB = AM.getResult<DemandedBitsAnalysis>(F);
  if (!bitTrackingDCE(F, DB))
    return PreservedAnalyses::all();

  PreservedAnalyses PA;
  PA.preserveSet<CFGAnalyses>();
  PA.preserve<GlobalsAA>();
  return PA;
}

namespace {
struct BDCELegacyPass : public FunctionPass {
  static char ID; // Pass identification, replacement for typeid
  BDCELegacyPass() : FunctionPass(ID) {
    initializeBDCELegacyPassPass(*PassRegistry::getPassRegistry());
  }

  bool runOnFunction(Function &F) override {
    if (skipFunction(F))
      return false;
    auto &DB = getAnalysis<DemandedBitsWrapperPass>().getDemandedBits();
    return bitTrackingDCE(F, DB);
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<DemandedBitsWrapperPass>();
    AU.addPreserved<GlobalsAAWrapperPass>();
  }
};
}

char BDCELegacyPass::ID = 0;
INITIALIZE_PASS_BEGIN(BDCELegacyPass, "bdce",
                      "Bit-Tracking Dead Code Elimination", false, false)
INITIALIZE_PASS_DEPENDENCY(DemandedBitsWrapperPass)
INITIALIZE_PASS_END(BDCELegacyPass, "bdce",
                    "Bit-Tracking Dead Code Elimination", false, false)

FunctionPass *llvm::createBitTrackingDCEPass() { return new BDCELegacyPass(); }
