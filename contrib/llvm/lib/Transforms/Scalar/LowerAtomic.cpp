//===- LowerAtomic.cpp - Lower atomic intrinsics --------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass lowers atomic intrinsics to non-atomic form for use in a known
// non-preemptible environment.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/LowerAtomic.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Pass.h"
#include "llvm/Transforms/Scalar.h"
using namespace llvm;

#define DEBUG_TYPE "loweratomic"

static bool LowerAtomicCmpXchgInst(AtomicCmpXchgInst *CXI) {
  IRBuilder<> Builder(CXI);
  Value *Ptr = CXI->getPointerOperand();
  Value *Cmp = CXI->getCompareOperand();
  Value *Val = CXI->getNewValOperand();

  LoadInst *Orig = Builder.CreateLoad(Ptr);
  Value *Equal = Builder.CreateICmpEQ(Orig, Cmp);
  Value *Res = Builder.CreateSelect(Equal, Val, Orig);
  Builder.CreateStore(Res, Ptr);

  Res = Builder.CreateInsertValue(UndefValue::get(CXI->getType()), Orig, 0);
  Res = Builder.CreateInsertValue(Res, Equal, 1);

  CXI->replaceAllUsesWith(Res);
  CXI->eraseFromParent();
  return true;
}

static bool LowerAtomicRMWInst(AtomicRMWInst *RMWI) {
  IRBuilder<> Builder(RMWI);
  Value *Ptr = RMWI->getPointerOperand();
  Value *Val = RMWI->getValOperand();

  LoadInst *Orig = Builder.CreateLoad(Ptr);
  Value *Res = nullptr;

  switch (RMWI->getOperation()) {
  default: llvm_unreachable("Unexpected RMW operation");
  case AtomicRMWInst::Xchg:
    Res = Val;
    break;
  case AtomicRMWInst::Add:
    Res = Builder.CreateAdd(Orig, Val);
    break;
  case AtomicRMWInst::Sub:
    Res = Builder.CreateSub(Orig, Val);
    break;
  case AtomicRMWInst::And:
    Res = Builder.CreateAnd(Orig, Val);
    break;
  case AtomicRMWInst::Nand:
    Res = Builder.CreateNot(Builder.CreateAnd(Orig, Val));
    break;
  case AtomicRMWInst::Or:
    Res = Builder.CreateOr(Orig, Val);
    break;
  case AtomicRMWInst::Xor:
    Res = Builder.CreateXor(Orig, Val);
    break;
  case AtomicRMWInst::Max:
    Res = Builder.CreateSelect(Builder.CreateICmpSLT(Orig, Val),
                               Val, Orig);
    break;
  case AtomicRMWInst::Min:
    Res = Builder.CreateSelect(Builder.CreateICmpSLT(Orig, Val),
                               Orig, Val);
    break;
  case AtomicRMWInst::UMax:
    Res = Builder.CreateSelect(Builder.CreateICmpULT(Orig, Val),
                               Val, Orig);
    break;
  case AtomicRMWInst::UMin:
    Res = Builder.CreateSelect(Builder.CreateICmpULT(Orig, Val),
                               Orig, Val);
    break;
  }
  Builder.CreateStore(Res, Ptr);
  RMWI->replaceAllUsesWith(Orig);
  RMWI->eraseFromParent();
  return true;
}

static bool LowerFenceInst(FenceInst *FI) {
  FI->eraseFromParent();
  return true;
}

static bool LowerLoadInst(LoadInst *LI) {
  LI->setAtomic(AtomicOrdering::NotAtomic);
  return true;
}

static bool LowerStoreInst(StoreInst *SI) {
  SI->setAtomic(AtomicOrdering::NotAtomic);
  return true;
}

static bool runOnBasicBlock(BasicBlock &BB) {
  bool Changed = false;
  for (BasicBlock::iterator DI = BB.begin(), DE = BB.end(); DI != DE;) {
    Instruction *Inst = &*DI++;
    if (FenceInst *FI = dyn_cast<FenceInst>(Inst))
      Changed |= LowerFenceInst(FI);
    else if (AtomicCmpXchgInst *CXI = dyn_cast<AtomicCmpXchgInst>(Inst))
      Changed |= LowerAtomicCmpXchgInst(CXI);
    else if (AtomicRMWInst *RMWI = dyn_cast<AtomicRMWInst>(Inst))
      Changed |= LowerAtomicRMWInst(RMWI);
    else if (LoadInst *LI = dyn_cast<LoadInst>(Inst)) {
      if (LI->isAtomic())
        LowerLoadInst(LI);
    } else if (StoreInst *SI = dyn_cast<StoreInst>(Inst)) {
      if (SI->isAtomic())
        LowerStoreInst(SI);
    }
  }
  return Changed;
}

static bool lowerAtomics(Function &F) {
  bool Changed = false;
  for (BasicBlock &BB : F) {
    Changed |= runOnBasicBlock(BB);
  }
  return Changed;
}

PreservedAnalyses LowerAtomicPass::run(Function &F, FunctionAnalysisManager &) {
  if (lowerAtomics(F))
    return PreservedAnalyses::none();
  return PreservedAnalyses::all();
}

namespace {
class LowerAtomicLegacyPass : public FunctionPass {
public:
  static char ID;

  LowerAtomicLegacyPass() : FunctionPass(ID) {
    initializeLowerAtomicLegacyPassPass(*PassRegistry::getPassRegistry());
  }

  bool runOnFunction(Function &F) override {
    // Don't skip optnone functions; atomics still need to be lowered.
    FunctionAnalysisManager DummyFAM;
    auto PA = Impl.run(F, DummyFAM);
    return !PA.areAllPreserved();
  }

private:
  LowerAtomicPass Impl;
  };
}

char LowerAtomicLegacyPass::ID = 0;
INITIALIZE_PASS(LowerAtomicLegacyPass, "loweratomic",
                "Lower atomic intrinsics to non-atomic form", false, false)

Pass *llvm::createLowerAtomicPass() { return new LowerAtomicLegacyPass(); }
