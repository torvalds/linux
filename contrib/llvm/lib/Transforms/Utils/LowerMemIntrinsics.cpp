//===- LowerMemIntrinsics.cpp ----------------------------------*- C++ -*--===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/LowerMemIntrinsics.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

using namespace llvm;

static unsigned getLoopOperandSizeInBytes(Type *Type) {
  if (VectorType *VTy = dyn_cast<VectorType>(Type)) {
    return VTy->getBitWidth() / 8;
  }

  return Type->getPrimitiveSizeInBits() / 8;
}

void llvm::createMemCpyLoopKnownSize(Instruction *InsertBefore, Value *SrcAddr,
                                     Value *DstAddr, ConstantInt *CopyLen,
                                     unsigned SrcAlign, unsigned DestAlign,
                                     bool SrcIsVolatile, bool DstIsVolatile,
                                     const TargetTransformInfo &TTI) {
  // No need to expand zero length copies.
  if (CopyLen->isZero())
    return;

  BasicBlock *PreLoopBB = InsertBefore->getParent();
  BasicBlock *PostLoopBB = nullptr;
  Function *ParentFunc = PreLoopBB->getParent();
  LLVMContext &Ctx = PreLoopBB->getContext();

  Type *TypeOfCopyLen = CopyLen->getType();
  Type *LoopOpType =
      TTI.getMemcpyLoopLoweringType(Ctx, CopyLen, SrcAlign, DestAlign);

  unsigned LoopOpSize = getLoopOperandSizeInBytes(LoopOpType);
  uint64_t LoopEndCount = CopyLen->getZExtValue() / LoopOpSize;

  unsigned SrcAS = cast<PointerType>(SrcAddr->getType())->getAddressSpace();
  unsigned DstAS = cast<PointerType>(DstAddr->getType())->getAddressSpace();

  if (LoopEndCount != 0) {
    // Split
    PostLoopBB = PreLoopBB->splitBasicBlock(InsertBefore, "memcpy-split");
    BasicBlock *LoopBB =
        BasicBlock::Create(Ctx, "load-store-loop", ParentFunc, PostLoopBB);
    PreLoopBB->getTerminator()->setSuccessor(0, LoopBB);

    IRBuilder<> PLBuilder(PreLoopBB->getTerminator());

    // Cast the Src and Dst pointers to pointers to the loop operand type (if
    // needed).
    PointerType *SrcOpType = PointerType::get(LoopOpType, SrcAS);
    PointerType *DstOpType = PointerType::get(LoopOpType, DstAS);
    if (SrcAddr->getType() != SrcOpType) {
      SrcAddr = PLBuilder.CreateBitCast(SrcAddr, SrcOpType);
    }
    if (DstAddr->getType() != DstOpType) {
      DstAddr = PLBuilder.CreateBitCast(DstAddr, DstOpType);
    }

    IRBuilder<> LoopBuilder(LoopBB);
    PHINode *LoopIndex = LoopBuilder.CreatePHI(TypeOfCopyLen, 2, "loop-index");
    LoopIndex->addIncoming(ConstantInt::get(TypeOfCopyLen, 0U), PreLoopBB);
    // Loop Body
    Value *SrcGEP =
        LoopBuilder.CreateInBoundsGEP(LoopOpType, SrcAddr, LoopIndex);
    Value *Load = LoopBuilder.CreateLoad(SrcGEP, SrcIsVolatile);
    Value *DstGEP =
        LoopBuilder.CreateInBoundsGEP(LoopOpType, DstAddr, LoopIndex);
    LoopBuilder.CreateStore(Load, DstGEP, DstIsVolatile);

    Value *NewIndex =
        LoopBuilder.CreateAdd(LoopIndex, ConstantInt::get(TypeOfCopyLen, 1U));
    LoopIndex->addIncoming(NewIndex, LoopBB);

    // Create the loop branch condition.
    Constant *LoopEndCI = ConstantInt::get(TypeOfCopyLen, LoopEndCount);
    LoopBuilder.CreateCondBr(LoopBuilder.CreateICmpULT(NewIndex, LoopEndCI),
                             LoopBB, PostLoopBB);
  }

  uint64_t BytesCopied = LoopEndCount * LoopOpSize;
  uint64_t RemainingBytes = CopyLen->getZExtValue() - BytesCopied;
  if (RemainingBytes) {
    IRBuilder<> RBuilder(PostLoopBB ? PostLoopBB->getFirstNonPHI()
                                    : InsertBefore);

    // Update the alignment based on the copy size used in the loop body.
    SrcAlign = std::min(SrcAlign, LoopOpSize);
    DestAlign = std::min(DestAlign, LoopOpSize);

    SmallVector<Type *, 5> RemainingOps;
    TTI.getMemcpyLoopResidualLoweringType(RemainingOps, Ctx, RemainingBytes,
                                          SrcAlign, DestAlign);

    for (auto OpTy : RemainingOps) {
      // Calaculate the new index
      unsigned OperandSize = getLoopOperandSizeInBytes(OpTy);
      uint64_t GepIndex = BytesCopied / OperandSize;
      assert(GepIndex * OperandSize == BytesCopied &&
             "Division should have no Remainder!");
      // Cast source to operand type and load
      PointerType *SrcPtrType = PointerType::get(OpTy, SrcAS);
      Value *CastedSrc = SrcAddr->getType() == SrcPtrType
                             ? SrcAddr
                             : RBuilder.CreateBitCast(SrcAddr, SrcPtrType);
      Value *SrcGEP = RBuilder.CreateInBoundsGEP(
          OpTy, CastedSrc, ConstantInt::get(TypeOfCopyLen, GepIndex));
      Value *Load = RBuilder.CreateLoad(SrcGEP, SrcIsVolatile);

      // Cast destination to operand type and store.
      PointerType *DstPtrType = PointerType::get(OpTy, DstAS);
      Value *CastedDst = DstAddr->getType() == DstPtrType
                             ? DstAddr
                             : RBuilder.CreateBitCast(DstAddr, DstPtrType);
      Value *DstGEP = RBuilder.CreateInBoundsGEP(
          OpTy, CastedDst, ConstantInt::get(TypeOfCopyLen, GepIndex));
      RBuilder.CreateStore(Load, DstGEP, DstIsVolatile);

      BytesCopied += OperandSize;
    }
  }
  assert(BytesCopied == CopyLen->getZExtValue() &&
         "Bytes copied should match size in the call!");
}

void llvm::createMemCpyLoopUnknownSize(Instruction *InsertBefore,
                                       Value *SrcAddr, Value *DstAddr,
                                       Value *CopyLen, unsigned SrcAlign,
                                       unsigned DestAlign, bool SrcIsVolatile,
                                       bool DstIsVolatile,
                                       const TargetTransformInfo &TTI) {
  BasicBlock *PreLoopBB = InsertBefore->getParent();
  BasicBlock *PostLoopBB =
      PreLoopBB->splitBasicBlock(InsertBefore, "post-loop-memcpy-expansion");

  Function *ParentFunc = PreLoopBB->getParent();
  LLVMContext &Ctx = PreLoopBB->getContext();

  Type *LoopOpType =
      TTI.getMemcpyLoopLoweringType(Ctx, CopyLen, SrcAlign, DestAlign);
  unsigned LoopOpSize = getLoopOperandSizeInBytes(LoopOpType);

  IRBuilder<> PLBuilder(PreLoopBB->getTerminator());

  unsigned SrcAS = cast<PointerType>(SrcAddr->getType())->getAddressSpace();
  unsigned DstAS = cast<PointerType>(DstAddr->getType())->getAddressSpace();
  PointerType *SrcOpType = PointerType::get(LoopOpType, SrcAS);
  PointerType *DstOpType = PointerType::get(LoopOpType, DstAS);
  if (SrcAddr->getType() != SrcOpType) {
    SrcAddr = PLBuilder.CreateBitCast(SrcAddr, SrcOpType);
  }
  if (DstAddr->getType() != DstOpType) {
    DstAddr = PLBuilder.CreateBitCast(DstAddr, DstOpType);
  }

  // Calculate the loop trip count, and remaining bytes to copy after the loop.
  Type *CopyLenType = CopyLen->getType();
  IntegerType *ILengthType = dyn_cast<IntegerType>(CopyLenType);
  assert(ILengthType &&
         "expected size argument to memcpy to be an integer type!");
  Type *Int8Type = Type::getInt8Ty(Ctx);
  bool LoopOpIsInt8 = LoopOpType == Int8Type;
  ConstantInt *CILoopOpSize = ConstantInt::get(ILengthType, LoopOpSize);
  Value *RuntimeLoopCount = LoopOpIsInt8 ?
                            CopyLen :
                            PLBuilder.CreateUDiv(CopyLen, CILoopOpSize);
  BasicBlock *LoopBB =
      BasicBlock::Create(Ctx, "loop-memcpy-expansion", ParentFunc, PostLoopBB);
  IRBuilder<> LoopBuilder(LoopBB);

  PHINode *LoopIndex = LoopBuilder.CreatePHI(CopyLenType, 2, "loop-index");
  LoopIndex->addIncoming(ConstantInt::get(CopyLenType, 0U), PreLoopBB);

  Value *SrcGEP = LoopBuilder.CreateInBoundsGEP(LoopOpType, SrcAddr, LoopIndex);
  Value *Load = LoopBuilder.CreateLoad(SrcGEP, SrcIsVolatile);
  Value *DstGEP = LoopBuilder.CreateInBoundsGEP(LoopOpType, DstAddr, LoopIndex);
  LoopBuilder.CreateStore(Load, DstGEP, DstIsVolatile);

  Value *NewIndex =
      LoopBuilder.CreateAdd(LoopIndex, ConstantInt::get(CopyLenType, 1U));
  LoopIndex->addIncoming(NewIndex, LoopBB);

  if (!LoopOpIsInt8) {
   // Add in the
   Value *RuntimeResidual = PLBuilder.CreateURem(CopyLen, CILoopOpSize);
   Value *RuntimeBytesCopied = PLBuilder.CreateSub(CopyLen, RuntimeResidual);

    // Loop body for the residual copy.
    BasicBlock *ResLoopBB = BasicBlock::Create(Ctx, "loop-memcpy-residual",
                                               PreLoopBB->getParent(),
                                               PostLoopBB);
    // Residual loop header.
    BasicBlock *ResHeaderBB = BasicBlock::Create(
        Ctx, "loop-memcpy-residual-header", PreLoopBB->getParent(), nullptr);

    // Need to update the pre-loop basic block to branch to the correct place.
    // branch to the main loop if the count is non-zero, branch to the residual
    // loop if the copy size is smaller then 1 iteration of the main loop but
    // non-zero and finally branch to after the residual loop if the memcpy
    //  size is zero.
    ConstantInt *Zero = ConstantInt::get(ILengthType, 0U);
    PLBuilder.CreateCondBr(PLBuilder.CreateICmpNE(RuntimeLoopCount, Zero),
                           LoopBB, ResHeaderBB);
    PreLoopBB->getTerminator()->eraseFromParent();

    LoopBuilder.CreateCondBr(
        LoopBuilder.CreateICmpULT(NewIndex, RuntimeLoopCount), LoopBB,
        ResHeaderBB);

    // Determine if we need to branch to the residual loop or bypass it.
    IRBuilder<> RHBuilder(ResHeaderBB);
    RHBuilder.CreateCondBr(RHBuilder.CreateICmpNE(RuntimeResidual, Zero),
                           ResLoopBB, PostLoopBB);

    // Copy the residual with single byte load/store loop.
    IRBuilder<> ResBuilder(ResLoopBB);
    PHINode *ResidualIndex =
        ResBuilder.CreatePHI(CopyLenType, 2, "residual-loop-index");
    ResidualIndex->addIncoming(Zero, ResHeaderBB);

    Value *SrcAsInt8 =
        ResBuilder.CreateBitCast(SrcAddr, PointerType::get(Int8Type, SrcAS));
    Value *DstAsInt8 =
        ResBuilder.CreateBitCast(DstAddr, PointerType::get(Int8Type, DstAS));
    Value *FullOffset = ResBuilder.CreateAdd(RuntimeBytesCopied, ResidualIndex);
    Value *SrcGEP =
        ResBuilder.CreateInBoundsGEP(Int8Type, SrcAsInt8, FullOffset);
    Value *Load = ResBuilder.CreateLoad(SrcGEP, SrcIsVolatile);
    Value *DstGEP =
        ResBuilder.CreateInBoundsGEP(Int8Type, DstAsInt8, FullOffset);
    ResBuilder.CreateStore(Load, DstGEP, DstIsVolatile);

    Value *ResNewIndex =
        ResBuilder.CreateAdd(ResidualIndex, ConstantInt::get(CopyLenType, 1U));
    ResidualIndex->addIncoming(ResNewIndex, ResLoopBB);

    // Create the loop branch condition.
    ResBuilder.CreateCondBr(
        ResBuilder.CreateICmpULT(ResNewIndex, RuntimeResidual), ResLoopBB,
        PostLoopBB);
  } else {
    // In this case the loop operand type was a byte, and there is no need for a
    // residual loop to copy the remaining memory after the main loop.
    // We do however need to patch up the control flow by creating the
    // terminators for the preloop block and the memcpy loop.
    ConstantInt *Zero = ConstantInt::get(ILengthType, 0U);
    PLBuilder.CreateCondBr(PLBuilder.CreateICmpNE(RuntimeLoopCount, Zero),
                           LoopBB, PostLoopBB);
    PreLoopBB->getTerminator()->eraseFromParent();
    LoopBuilder.CreateCondBr(
        LoopBuilder.CreateICmpULT(NewIndex, RuntimeLoopCount), LoopBB,
        PostLoopBB);
  }
}

// Lower memmove to IR. memmove is required to correctly copy overlapping memory
// regions; therefore, it has to check the relative positions of the source and
// destination pointers and choose the copy direction accordingly.
//
// The code below is an IR rendition of this C function:
//
// void* memmove(void* dst, const void* src, size_t n) {
//   unsigned char* d = dst;
//   const unsigned char* s = src;
//   if (s < d) {
//     // copy backwards
//     while (n--) {
//       d[n] = s[n];
//     }
//   } else {
//     // copy forward
//     for (size_t i = 0; i < n; ++i) {
//       d[i] = s[i];
//     }
//   }
//   return dst;
// }
static void createMemMoveLoop(Instruction *InsertBefore,
                              Value *SrcAddr, Value *DstAddr, Value *CopyLen,
                              unsigned SrcAlign, unsigned DestAlign,
                              bool SrcIsVolatile, bool DstIsVolatile) {
  Type *TypeOfCopyLen = CopyLen->getType();
  BasicBlock *OrigBB = InsertBefore->getParent();
  Function *F = OrigBB->getParent();

  // Create the a comparison of src and dst, based on which we jump to either
  // the forward-copy part of the function (if src >= dst) or the backwards-copy
  // part (if src < dst).
  // SplitBlockAndInsertIfThenElse conveniently creates the basic if-then-else
  // structure. Its block terminators (unconditional branches) are replaced by
  // the appropriate conditional branches when the loop is built.
  ICmpInst *PtrCompare = new ICmpInst(InsertBefore, ICmpInst::ICMP_ULT,
                                      SrcAddr, DstAddr, "compare_src_dst");
  Instruction *ThenTerm, *ElseTerm;
  SplitBlockAndInsertIfThenElse(PtrCompare, InsertBefore, &ThenTerm,
                                &ElseTerm);

  // Each part of the function consists of two blocks:
  //   copy_backwards:        used to skip the loop when n == 0
  //   copy_backwards_loop:   the actual backwards loop BB
  //   copy_forward:          used to skip the loop when n == 0
  //   copy_forward_loop:     the actual forward loop BB
  BasicBlock *CopyBackwardsBB = ThenTerm->getParent();
  CopyBackwardsBB->setName("copy_backwards");
  BasicBlock *CopyForwardBB = ElseTerm->getParent();
  CopyForwardBB->setName("copy_forward");
  BasicBlock *ExitBB = InsertBefore->getParent();
  ExitBB->setName("memmove_done");

  // Initial comparison of n == 0 that lets us skip the loops altogether. Shared
  // between both backwards and forward copy clauses.
  ICmpInst *CompareN =
      new ICmpInst(OrigBB->getTerminator(), ICmpInst::ICMP_EQ, CopyLen,
                   ConstantInt::get(TypeOfCopyLen, 0), "compare_n_to_0");

  // Copying backwards.
  BasicBlock *LoopBB =
    BasicBlock::Create(F->getContext(), "copy_backwards_loop", F, CopyForwardBB);
  IRBuilder<> LoopBuilder(LoopBB);
  PHINode *LoopPhi = LoopBuilder.CreatePHI(TypeOfCopyLen, 0);
  Value *IndexPtr = LoopBuilder.CreateSub(
      LoopPhi, ConstantInt::get(TypeOfCopyLen, 1), "index_ptr");
  Value *Element = LoopBuilder.CreateLoad(
      LoopBuilder.CreateInBoundsGEP(SrcAddr, IndexPtr), "element");
  LoopBuilder.CreateStore(Element,
                          LoopBuilder.CreateInBoundsGEP(DstAddr, IndexPtr));
  LoopBuilder.CreateCondBr(
      LoopBuilder.CreateICmpEQ(IndexPtr, ConstantInt::get(TypeOfCopyLen, 0)),
      ExitBB, LoopBB);
  LoopPhi->addIncoming(IndexPtr, LoopBB);
  LoopPhi->addIncoming(CopyLen, CopyBackwardsBB);
  BranchInst::Create(ExitBB, LoopBB, CompareN, ThenTerm);
  ThenTerm->eraseFromParent();

  // Copying forward.
  BasicBlock *FwdLoopBB =
    BasicBlock::Create(F->getContext(), "copy_forward_loop", F, ExitBB);
  IRBuilder<> FwdLoopBuilder(FwdLoopBB);
  PHINode *FwdCopyPhi = FwdLoopBuilder.CreatePHI(TypeOfCopyLen, 0, "index_ptr");
  Value *FwdElement = FwdLoopBuilder.CreateLoad(
      FwdLoopBuilder.CreateInBoundsGEP(SrcAddr, FwdCopyPhi), "element");
  FwdLoopBuilder.CreateStore(
      FwdElement, FwdLoopBuilder.CreateInBoundsGEP(DstAddr, FwdCopyPhi));
  Value *FwdIndexPtr = FwdLoopBuilder.CreateAdd(
      FwdCopyPhi, ConstantInt::get(TypeOfCopyLen, 1), "index_increment");
  FwdLoopBuilder.CreateCondBr(FwdLoopBuilder.CreateICmpEQ(FwdIndexPtr, CopyLen),
                              ExitBB, FwdLoopBB);
  FwdCopyPhi->addIncoming(FwdIndexPtr, FwdLoopBB);
  FwdCopyPhi->addIncoming(ConstantInt::get(TypeOfCopyLen, 0), CopyForwardBB);

  BranchInst::Create(ExitBB, FwdLoopBB, CompareN, ElseTerm);
  ElseTerm->eraseFromParent();
}

static void createMemSetLoop(Instruction *InsertBefore,
                             Value *DstAddr, Value *CopyLen, Value *SetValue,
                             unsigned Align, bool IsVolatile) {
  Type *TypeOfCopyLen = CopyLen->getType();
  BasicBlock *OrigBB = InsertBefore->getParent();
  Function *F = OrigBB->getParent();
  BasicBlock *NewBB =
      OrigBB->splitBasicBlock(InsertBefore, "split");
  BasicBlock *LoopBB
    = BasicBlock::Create(F->getContext(), "loadstoreloop", F, NewBB);

  IRBuilder<> Builder(OrigBB->getTerminator());

  // Cast pointer to the type of value getting stored
  unsigned dstAS = cast<PointerType>(DstAddr->getType())->getAddressSpace();
  DstAddr = Builder.CreateBitCast(DstAddr,
                                  PointerType::get(SetValue->getType(), dstAS));

  Builder.CreateCondBr(
      Builder.CreateICmpEQ(ConstantInt::get(TypeOfCopyLen, 0), CopyLen), NewBB,
      LoopBB);
  OrigBB->getTerminator()->eraseFromParent();

  IRBuilder<> LoopBuilder(LoopBB);
  PHINode *LoopIndex = LoopBuilder.CreatePHI(TypeOfCopyLen, 0);
  LoopIndex->addIncoming(ConstantInt::get(TypeOfCopyLen, 0), OrigBB);

  LoopBuilder.CreateStore(
      SetValue,
      LoopBuilder.CreateInBoundsGEP(SetValue->getType(), DstAddr, LoopIndex),
      IsVolatile);

  Value *NewIndex =
      LoopBuilder.CreateAdd(LoopIndex, ConstantInt::get(TypeOfCopyLen, 1));
  LoopIndex->addIncoming(NewIndex, LoopBB);

  LoopBuilder.CreateCondBr(LoopBuilder.CreateICmpULT(NewIndex, CopyLen), LoopBB,
                           NewBB);
}

void llvm::expandMemCpyAsLoop(MemCpyInst *Memcpy,
                              const TargetTransformInfo &TTI) {
  if (ConstantInt *CI = dyn_cast<ConstantInt>(Memcpy->getLength())) {
    createMemCpyLoopKnownSize(/* InsertBefore */ Memcpy,
                              /* SrcAddr */ Memcpy->getRawSource(),
                              /* DstAddr */ Memcpy->getRawDest(),
                              /* CopyLen */ CI,
                              /* SrcAlign */ Memcpy->getSourceAlignment(),
                              /* DestAlign */ Memcpy->getDestAlignment(),
                              /* SrcIsVolatile */ Memcpy->isVolatile(),
                              /* DstIsVolatile */ Memcpy->isVolatile(),
                              /* TargetTransformInfo */ TTI);
  } else {
    createMemCpyLoopUnknownSize(/* InsertBefore */ Memcpy,
                                /* SrcAddr */ Memcpy->getRawSource(),
                                /* DstAddr */ Memcpy->getRawDest(),
                                /* CopyLen */ Memcpy->getLength(),
                                /* SrcAlign */ Memcpy->getSourceAlignment(),
                                /* DestAlign */ Memcpy->getDestAlignment(),
                                /* SrcIsVolatile */ Memcpy->isVolatile(),
                                /* DstIsVolatile */ Memcpy->isVolatile(),
                                /* TargetTransfomrInfo */ TTI);
  }
}

void llvm::expandMemMoveAsLoop(MemMoveInst *Memmove) {
  createMemMoveLoop(/* InsertBefore */ Memmove,
                    /* SrcAddr */ Memmove->getRawSource(),
                    /* DstAddr */ Memmove->getRawDest(),
                    /* CopyLen */ Memmove->getLength(),
                    /* SrcAlign */ Memmove->getSourceAlignment(),
                    /* DestAlign */ Memmove->getDestAlignment(),
                    /* SrcIsVolatile */ Memmove->isVolatile(),
                    /* DstIsVolatile */ Memmove->isVolatile());
}

void llvm::expandMemSetAsLoop(MemSetInst *Memset) {
  createMemSetLoop(/* InsertBefore */ Memset,
                   /* DstAddr */ Memset->getRawDest(),
                   /* CopyLen */ Memset->getLength(),
                   /* SetValue */ Memset->getValue(),
                   /* Alignment */ Memset->getDestAlignment(),
                   Memset->isVolatile());
}
