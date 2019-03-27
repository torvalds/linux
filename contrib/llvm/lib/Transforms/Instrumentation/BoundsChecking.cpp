//===- BoundsChecking.cpp - Instrumentation for run-time bounds checking --===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Instrumentation/BoundsChecking.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/TargetFolder.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Value.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdint>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "bounds-checking"

static cl::opt<bool> SingleTrapBB("bounds-checking-single-trap",
                                  cl::desc("Use one trap block per function"));

STATISTIC(ChecksAdded, "Bounds checks added");
STATISTIC(ChecksSkipped, "Bounds checks skipped");
STATISTIC(ChecksUnable, "Bounds checks unable to add");

using BuilderTy = IRBuilder<TargetFolder>;

/// Gets the conditions under which memory accessing instructions will overflow.
///
/// \p Ptr is the pointer that will be read/written, and \p InstVal is either
/// the result from the load or the value being stored. It is used to determine
/// the size of memory block that is touched.
///
/// Returns the condition under which the access will overflow.
static Value *getBoundsCheckCond(Value *Ptr, Value *InstVal,
                                 const DataLayout &DL, TargetLibraryInfo &TLI,
                                 ObjectSizeOffsetEvaluator &ObjSizeEval,
                                 BuilderTy &IRB, ScalarEvolution &SE) {
  uint64_t NeededSize = DL.getTypeStoreSize(InstVal->getType());
  LLVM_DEBUG(dbgs() << "Instrument " << *Ptr << " for " << Twine(NeededSize)
                    << " bytes\n");

  SizeOffsetEvalType SizeOffset = ObjSizeEval.compute(Ptr);

  if (!ObjSizeEval.bothKnown(SizeOffset)) {
    ++ChecksUnable;
    return nullptr;
  }

  Value *Size   = SizeOffset.first;
  Value *Offset = SizeOffset.second;
  ConstantInt *SizeCI = dyn_cast<ConstantInt>(Size);

  Type *IntTy = DL.getIntPtrType(Ptr->getType());
  Value *NeededSizeVal = ConstantInt::get(IntTy, NeededSize);

  auto SizeRange = SE.getUnsignedRange(SE.getSCEV(Size));
  auto OffsetRange = SE.getUnsignedRange(SE.getSCEV(Offset));
  auto NeededSizeRange = SE.getUnsignedRange(SE.getSCEV(NeededSizeVal));

  // three checks are required to ensure safety:
  // . Offset >= 0  (since the offset is given from the base ptr)
  // . Size >= Offset  (unsigned)
  // . Size - Offset >= NeededSize  (unsigned)
  //
  // optimization: if Size >= 0 (signed), skip 1st check
  // FIXME: add NSW/NUW here?  -- we dont care if the subtraction overflows
  Value *ObjSize = IRB.CreateSub(Size, Offset);
  Value *Cmp2 = SizeRange.getUnsignedMin().uge(OffsetRange.getUnsignedMax())
                    ? ConstantInt::getFalse(Ptr->getContext())
                    : IRB.CreateICmpULT(Size, Offset);
  Value *Cmp3 = SizeRange.sub(OffsetRange)
                        .getUnsignedMin()
                        .uge(NeededSizeRange.getUnsignedMax())
                    ? ConstantInt::getFalse(Ptr->getContext())
                    : IRB.CreateICmpULT(ObjSize, NeededSizeVal);
  Value *Or = IRB.CreateOr(Cmp2, Cmp3);
  if ((!SizeCI || SizeCI->getValue().slt(0)) &&
      !SizeRange.getSignedMin().isNonNegative()) {
    Value *Cmp1 = IRB.CreateICmpSLT(Offset, ConstantInt::get(IntTy, 0));
    Or = IRB.CreateOr(Cmp1, Or);
  }

  return Or;
}

/// Adds run-time bounds checks to memory accessing instructions.
///
/// \p Or is the condition that should guard the trap.
///
/// \p GetTrapBB is a callable that returns the trap BB to use on failure.
template <typename GetTrapBBT>
static void insertBoundsCheck(Value *Or, BuilderTy IRB, GetTrapBBT GetTrapBB) {
  // check if the comparison is always false
  ConstantInt *C = dyn_cast_or_null<ConstantInt>(Or);
  if (C) {
    ++ChecksSkipped;
    // If non-zero, nothing to do.
    if (!C->getZExtValue())
      return;
  }
  ++ChecksAdded;

  BasicBlock::iterator SplitI = IRB.GetInsertPoint();
  BasicBlock *OldBB = SplitI->getParent();
  BasicBlock *Cont = OldBB->splitBasicBlock(SplitI);
  OldBB->getTerminator()->eraseFromParent();

  if (C) {
    // If we have a constant zero, unconditionally branch.
    // FIXME: We should really handle this differently to bypass the splitting
    // the block.
    BranchInst::Create(GetTrapBB(IRB), OldBB);
    return;
  }

  // Create the conditional branch.
  BranchInst::Create(GetTrapBB(IRB), Cont, Or, OldBB);
}

static bool addBoundsChecking(Function &F, TargetLibraryInfo &TLI,
                              ScalarEvolution &SE) {
  const DataLayout &DL = F.getParent()->getDataLayout();
  ObjectSizeOffsetEvaluator ObjSizeEval(DL, &TLI, F.getContext(),
                                           /*RoundToAlign=*/true);

  // check HANDLE_MEMORY_INST in include/llvm/Instruction.def for memory
  // touching instructions
  SmallVector<std::pair<Instruction *, Value *>, 4> TrapInfo;
  for (Instruction &I : instructions(F)) {
    Value *Or = nullptr;
    BuilderTy IRB(I.getParent(), BasicBlock::iterator(&I), TargetFolder(DL));
    if (LoadInst *LI = dyn_cast<LoadInst>(&I)) {
      Or = getBoundsCheckCond(LI->getPointerOperand(), LI, DL, TLI,
                              ObjSizeEval, IRB, SE);
    } else if (StoreInst *SI = dyn_cast<StoreInst>(&I)) {
      Or = getBoundsCheckCond(SI->getPointerOperand(), SI->getValueOperand(),
                              DL, TLI, ObjSizeEval, IRB, SE);
    } else if (AtomicCmpXchgInst *AI = dyn_cast<AtomicCmpXchgInst>(&I)) {
      Or = getBoundsCheckCond(AI->getPointerOperand(), AI->getCompareOperand(),
                              DL, TLI, ObjSizeEval, IRB, SE);
    } else if (AtomicRMWInst *AI = dyn_cast<AtomicRMWInst>(&I)) {
      Or = getBoundsCheckCond(AI->getPointerOperand(), AI->getValOperand(), DL,
                              TLI, ObjSizeEval, IRB, SE);
    }
    if (Or)
      TrapInfo.push_back(std::make_pair(&I, Or));
  }

  // Create a trapping basic block on demand using a callback. Depending on
  // flags, this will either create a single block for the entire function or
  // will create a fresh block every time it is called.
  BasicBlock *TrapBB = nullptr;
  auto GetTrapBB = [&TrapBB](BuilderTy &IRB) {
    if (TrapBB && SingleTrapBB)
      return TrapBB;

    Function *Fn = IRB.GetInsertBlock()->getParent();
    // FIXME: This debug location doesn't make a lot of sense in the
    // `SingleTrapBB` case.
    auto DebugLoc = IRB.getCurrentDebugLocation();
    IRBuilder<>::InsertPointGuard Guard(IRB);
    TrapBB = BasicBlock::Create(Fn->getContext(), "trap", Fn);
    IRB.SetInsertPoint(TrapBB);

    auto *F = Intrinsic::getDeclaration(Fn->getParent(), Intrinsic::trap);
    CallInst *TrapCall = IRB.CreateCall(F, {});
    TrapCall->setDoesNotReturn();
    TrapCall->setDoesNotThrow();
    TrapCall->setDebugLoc(DebugLoc);
    IRB.CreateUnreachable();

    return TrapBB;
  };

  // Add the checks.
  for (const auto &Entry : TrapInfo) {
    Instruction *Inst = Entry.first;
    BuilderTy IRB(Inst->getParent(), BasicBlock::iterator(Inst), TargetFolder(DL));
    insertBoundsCheck(Entry.second, IRB, GetTrapBB);
  }

  return !TrapInfo.empty();
}

PreservedAnalyses BoundsCheckingPass::run(Function &F, FunctionAnalysisManager &AM) {
  auto &TLI = AM.getResult<TargetLibraryAnalysis>(F);
  auto &SE = AM.getResult<ScalarEvolutionAnalysis>(F);

  if (!addBoundsChecking(F, TLI, SE))
    return PreservedAnalyses::all();

  return PreservedAnalyses::none();
}

namespace {
struct BoundsCheckingLegacyPass : public FunctionPass {
  static char ID;

  BoundsCheckingLegacyPass() : FunctionPass(ID) {
    initializeBoundsCheckingLegacyPassPass(*PassRegistry::getPassRegistry());
  }

  bool runOnFunction(Function &F) override {
    auto &TLI = getAnalysis<TargetLibraryInfoWrapperPass>().getTLI();
    auto &SE = getAnalysis<ScalarEvolutionWrapperPass>().getSE();
    return addBoundsChecking(F, TLI, SE);
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<TargetLibraryInfoWrapperPass>();
    AU.addRequired<ScalarEvolutionWrapperPass>();
  }
};
} // namespace

char BoundsCheckingLegacyPass::ID = 0;
INITIALIZE_PASS_BEGIN(BoundsCheckingLegacyPass, "bounds-checking",
                      "Run-time bounds checking", false, false)
INITIALIZE_PASS_DEPENDENCY(TargetLibraryInfoWrapperPass)
INITIALIZE_PASS_END(BoundsCheckingLegacyPass, "bounds-checking",
                    "Run-time bounds checking", false, false)

FunctionPass *llvm::createBoundsCheckingLegacyPass() {
  return new BoundsCheckingLegacyPass();
}
