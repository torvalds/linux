//===- DivRemPairs.cpp - Hoist/decompose division and remainder -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass hoists and/or decomposes integer division and remainder
// instructions to enable CFG improvements and better codegen.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/DivRemPairs.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/Pass.h"
#include "llvm/Support/DebugCounter.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils/BypassSlowDivision.h"
using namespace llvm;

#define DEBUG_TYPE "div-rem-pairs"
STATISTIC(NumPairs, "Number of div/rem pairs");
STATISTIC(NumHoisted, "Number of instructions hoisted");
STATISTIC(NumDecomposed, "Number of instructions decomposed");
DEBUG_COUNTER(DRPCounter, "div-rem-pairs-transform",
              "Controls transformations in div-rem-pairs pass");

/// Find matching pairs of integer div/rem ops (they have the same numerator,
/// denominator, and signedness). If they exist in different basic blocks, bring
/// them together by hoisting or replace the common division operation that is
/// implicit in the remainder:
/// X % Y <--> X - ((X / Y) * Y).
///
/// We can largely ignore the normal safety and cost constraints on speculation
/// of these ops when we find a matching pair. This is because we are already
/// guaranteed that any exceptions and most cost are already incurred by the
/// first member of the pair.
///
/// Note: This transform could be an oddball enhancement to EarlyCSE, GVN, or
/// SimplifyCFG, but it's split off on its own because it's different enough
/// that it doesn't quite match the stated objectives of those passes.
static bool optimizeDivRem(Function &F, const TargetTransformInfo &TTI,
                           const DominatorTree &DT) {
  bool Changed = false;

  // Insert all divide and remainder instructions into maps keyed by their
  // operands and opcode (signed or unsigned).
  DenseMap<DivRemMapKey, Instruction *> DivMap;
  // Use a MapVector for RemMap so that instructions are moved/inserted in a
  // deterministic order.
  MapVector<DivRemMapKey, Instruction *> RemMap;
  for (auto &BB : F) {
    for (auto &I : BB) {
      if (I.getOpcode() == Instruction::SDiv)
        DivMap[DivRemMapKey(true, I.getOperand(0), I.getOperand(1))] = &I;
      else if (I.getOpcode() == Instruction::UDiv)
        DivMap[DivRemMapKey(false, I.getOperand(0), I.getOperand(1))] = &I;
      else if (I.getOpcode() == Instruction::SRem)
        RemMap[DivRemMapKey(true, I.getOperand(0), I.getOperand(1))] = &I;
      else if (I.getOpcode() == Instruction::URem)
        RemMap[DivRemMapKey(false, I.getOperand(0), I.getOperand(1))] = &I;
    }
  }

  // We can iterate over either map because we are only looking for matched
  // pairs. Choose remainders for efficiency because they are usually even more
  // rare than division.
  for (auto &RemPair : RemMap) {
    // Find the matching division instruction from the division map.
    Instruction *DivInst = DivMap[RemPair.first];
    if (!DivInst)
      continue;

    // We have a matching pair of div/rem instructions. If one dominates the
    // other, hoist and/or replace one.
    NumPairs++;
    Instruction *RemInst = RemPair.second;
    bool IsSigned = DivInst->getOpcode() == Instruction::SDiv;
    bool HasDivRemOp = TTI.hasDivRemOp(DivInst->getType(), IsSigned);

    // If the target supports div+rem and the instructions are in the same block
    // already, there's nothing to do. The backend should handle this. If the
    // target does not support div+rem, then we will decompose the rem.
    if (HasDivRemOp && RemInst->getParent() == DivInst->getParent())
      continue;

    bool DivDominates = DT.dominates(DivInst, RemInst);
    if (!DivDominates && !DT.dominates(RemInst, DivInst))
      continue;

    if (!DebugCounter::shouldExecute(DRPCounter))
      continue;

    if (HasDivRemOp) {
      // The target has a single div/rem operation. Hoist the lower instruction
      // to make the matched pair visible to the backend.
      if (DivDominates)
        RemInst->moveAfter(DivInst);
      else
        DivInst->moveAfter(RemInst);
      NumHoisted++;
    } else {
      // The target does not have a single div/rem operation. Decompose the
      // remainder calculation as:
      // X % Y --> X - ((X / Y) * Y).
      Value *X = RemInst->getOperand(0);
      Value *Y = RemInst->getOperand(1);
      Instruction *Mul = BinaryOperator::CreateMul(DivInst, Y);
      Instruction *Sub = BinaryOperator::CreateSub(X, Mul);

      // If the remainder dominates, then hoist the division up to that block:
      //
      // bb1:
      //   %rem = srem %x, %y
      // bb2:
      //   %div = sdiv %x, %y
      // -->
      // bb1:
      //   %div = sdiv %x, %y
      //   %mul = mul %div, %y
      //   %rem = sub %x, %mul
      //
      // If the division dominates, it's already in the right place. The mul+sub
      // will be in a different block because we don't assume that they are
      // cheap to speculatively execute:
      //
      // bb1:
      //   %div = sdiv %x, %y
      // bb2:
      //   %rem = srem %x, %y
      // -->
      // bb1:
      //   %div = sdiv %x, %y
      // bb2:
      //   %mul = mul %div, %y
      //   %rem = sub %x, %mul
      //
      // If the div and rem are in the same block, we do the same transform,
      // but any code movement would be within the same block.

      if (!DivDominates)
        DivInst->moveBefore(RemInst);
      Mul->insertAfter(RemInst);
      Sub->insertAfter(Mul);

      // Now kill the explicit remainder. We have replaced it with:
      // (sub X, (mul (div X, Y), Y)
      RemInst->replaceAllUsesWith(Sub);
      RemInst->eraseFromParent();
      NumDecomposed++;
    }
    Changed = true;
  }

  return Changed;
}

// Pass manager boilerplate below here.

namespace {
struct DivRemPairsLegacyPass : public FunctionPass {
  static char ID;
  DivRemPairsLegacyPass() : FunctionPass(ID) {
    initializeDivRemPairsLegacyPassPass(*PassRegistry::getPassRegistry());
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<DominatorTreeWrapperPass>();
    AU.addRequired<TargetTransformInfoWrapperPass>();
    AU.setPreservesCFG();
    AU.addPreserved<DominatorTreeWrapperPass>();
    AU.addPreserved<GlobalsAAWrapperPass>();
    FunctionPass::getAnalysisUsage(AU);
  }

  bool runOnFunction(Function &F) override {
    if (skipFunction(F))
      return false;
    auto &TTI = getAnalysis<TargetTransformInfoWrapperPass>().getTTI(F);
    auto &DT = getAnalysis<DominatorTreeWrapperPass>().getDomTree();
    return optimizeDivRem(F, TTI, DT);
  }
};
}

char DivRemPairsLegacyPass::ID = 0;
INITIALIZE_PASS_BEGIN(DivRemPairsLegacyPass, "div-rem-pairs",
                      "Hoist/decompose integer division and remainder", false,
                      false)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_END(DivRemPairsLegacyPass, "div-rem-pairs",
                    "Hoist/decompose integer division and remainder", false,
                    false)
FunctionPass *llvm::createDivRemPairsPass() {
  return new DivRemPairsLegacyPass();
}

PreservedAnalyses DivRemPairsPass::run(Function &F,
                                       FunctionAnalysisManager &FAM) {
  TargetTransformInfo &TTI = FAM.getResult<TargetIRAnalysis>(F);
  DominatorTree &DT = FAM.getResult<DominatorTreeAnalysis>(F);
  if (!optimizeDivRem(F, TTI, DT))
    return PreservedAnalyses::all();
  // TODO: This pass just hoists/replaces math ops - all analyses are preserved?
  PreservedAnalyses PA;
  PA.preserveSet<CFGAnalyses>();
  PA.preserve<GlobalsAA>();
  return PA;
}
