//===- InstCombineVectorOps.cpp -------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements instcombine for ExtractElement, InsertElement and
// ShuffleVector.
//
//===----------------------------------------------------------------------===//

#include "InstCombineInternal.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Transforms/InstCombine/InstCombineWorklist.h"
#include <cassert>
#include <cstdint>
#include <iterator>
#include <utility>

using namespace llvm;
using namespace PatternMatch;

#define DEBUG_TYPE "instcombine"

/// Return true if the value is cheaper to scalarize than it is to leave as a
/// vector operation. IsConstantExtractIndex indicates whether we are extracting
/// one known element from a vector constant.
///
/// FIXME: It's possible to create more instructions than previously existed.
static bool cheapToScalarize(Value *V, bool IsConstantExtractIndex) {
  // If we can pick a scalar constant value out of a vector, that is free.
  if (auto *C = dyn_cast<Constant>(V))
    return IsConstantExtractIndex || C->getSplatValue();

  // An insertelement to the same constant index as our extract will simplify
  // to the scalar inserted element. An insertelement to a different constant
  // index is irrelevant to our extract.
  if (match(V, m_InsertElement(m_Value(), m_Value(), m_ConstantInt())))
    return IsConstantExtractIndex;

  if (match(V, m_OneUse(m_Load(m_Value()))))
    return true;

  Value *V0, *V1;
  if (match(V, m_OneUse(m_BinOp(m_Value(V0), m_Value(V1)))))
    if (cheapToScalarize(V0, IsConstantExtractIndex) ||
        cheapToScalarize(V1, IsConstantExtractIndex))
      return true;

  CmpInst::Predicate UnusedPred;
  if (match(V, m_OneUse(m_Cmp(UnusedPred, m_Value(V0), m_Value(V1)))))
    if (cheapToScalarize(V0, IsConstantExtractIndex) ||
        cheapToScalarize(V1, IsConstantExtractIndex))
      return true;

  return false;
}

// If we have a PHI node with a vector type that is only used to feed
// itself and be an operand of extractelement at a constant location,
// try to replace the PHI of the vector type with a PHI of a scalar type.
Instruction *InstCombiner::scalarizePHI(ExtractElementInst &EI, PHINode *PN) {
  SmallVector<Instruction *, 2> Extracts;
  // The users we want the PHI to have are:
  // 1) The EI ExtractElement (we already know this)
  // 2) Possibly more ExtractElements with the same index.
  // 3) Another operand, which will feed back into the PHI.
  Instruction *PHIUser = nullptr;
  for (auto U : PN->users()) {
    if (ExtractElementInst *EU = dyn_cast<ExtractElementInst>(U)) {
      if (EI.getIndexOperand() == EU->getIndexOperand())
        Extracts.push_back(EU);
      else
        return nullptr;
    } else if (!PHIUser) {
      PHIUser = cast<Instruction>(U);
    } else {
      return nullptr;
    }
  }

  if (!PHIUser)
    return nullptr;

  // Verify that this PHI user has one use, which is the PHI itself,
  // and that it is a binary operation which is cheap to scalarize.
  // otherwise return nullptr.
  if (!PHIUser->hasOneUse() || !(PHIUser->user_back() == PN) ||
      !(isa<BinaryOperator>(PHIUser)) || !cheapToScalarize(PHIUser, true))
    return nullptr;

  // Create a scalar PHI node that will replace the vector PHI node
  // just before the current PHI node.
  PHINode *scalarPHI = cast<PHINode>(InsertNewInstWith(
      PHINode::Create(EI.getType(), PN->getNumIncomingValues(), ""), *PN));
  // Scalarize each PHI operand.
  for (unsigned i = 0; i < PN->getNumIncomingValues(); i++) {
    Value *PHIInVal = PN->getIncomingValue(i);
    BasicBlock *inBB = PN->getIncomingBlock(i);
    Value *Elt = EI.getIndexOperand();
    // If the operand is the PHI induction variable:
    if (PHIInVal == PHIUser) {
      // Scalarize the binary operation. Its first operand is the
      // scalar PHI, and the second operand is extracted from the other
      // vector operand.
      BinaryOperator *B0 = cast<BinaryOperator>(PHIUser);
      unsigned opId = (B0->getOperand(0) == PN) ? 1 : 0;
      Value *Op = InsertNewInstWith(
          ExtractElementInst::Create(B0->getOperand(opId), Elt,
                                     B0->getOperand(opId)->getName() + ".Elt"),
          *B0);
      Value *newPHIUser = InsertNewInstWith(
          BinaryOperator::CreateWithCopiedFlags(B0->getOpcode(),
                                                scalarPHI, Op, B0), *B0);
      scalarPHI->addIncoming(newPHIUser, inBB);
    } else {
      // Scalarize PHI input:
      Instruction *newEI = ExtractElementInst::Create(PHIInVal, Elt, "");
      // Insert the new instruction into the predecessor basic block.
      Instruction *pos = dyn_cast<Instruction>(PHIInVal);
      BasicBlock::iterator InsertPos;
      if (pos && !isa<PHINode>(pos)) {
        InsertPos = ++pos->getIterator();
      } else {
        InsertPos = inBB->getFirstInsertionPt();
      }

      InsertNewInstWith(newEI, *InsertPos);

      scalarPHI->addIncoming(newEI, inBB);
    }
  }

  for (auto E : Extracts)
    replaceInstUsesWith(*E, scalarPHI);

  return &EI;
}

static Instruction *foldBitcastExtElt(ExtractElementInst &Ext,
                                      InstCombiner::BuilderTy &Builder,
                                      bool IsBigEndian) {
  Value *X;
  uint64_t ExtIndexC;
  if (!match(Ext.getVectorOperand(), m_BitCast(m_Value(X))) ||
      !X->getType()->isVectorTy() ||
      !match(Ext.getIndexOperand(), m_ConstantInt(ExtIndexC)))
    return nullptr;

  // If this extractelement is using a bitcast from a vector of the same number
  // of elements, see if we can find the source element from the source vector:
  // extelt (bitcast VecX), IndexC --> bitcast X[IndexC]
  Type *SrcTy = X->getType();
  Type *DestTy = Ext.getType();
  unsigned NumSrcElts = SrcTy->getVectorNumElements();
  unsigned NumElts = Ext.getVectorOperandType()->getNumElements();
  if (NumSrcElts == NumElts)
    if (Value *Elt = findScalarElement(X, ExtIndexC))
      return new BitCastInst(Elt, DestTy);

  // If the source elements are wider than the destination, try to shift and
  // truncate a subset of scalar bits of an insert op.
  if (NumSrcElts < NumElts) {
    Value *Scalar;
    uint64_t InsIndexC;
    if (!match(X, m_InsertElement(m_Value(), m_Value(Scalar),
                                  m_ConstantInt(InsIndexC))))
      return nullptr;

    // The extract must be from the subset of vector elements that we inserted
    // into. Example: if we inserted element 1 of a <2 x i64> and we are
    // extracting an i16 (narrowing ratio = 4), then this extract must be from 1
    // of elements 4-7 of the bitcasted vector.
    unsigned NarrowingRatio = NumElts / NumSrcElts;
    if (ExtIndexC / NarrowingRatio != InsIndexC)
      return nullptr;

    // We are extracting part of the original scalar. How that scalar is
    // inserted into the vector depends on the endian-ness. Example:
    //              Vector Byte Elt Index:    0  1  2  3  4  5  6  7
    //                                       +--+--+--+--+--+--+--+--+
    // inselt <2 x i32> V, <i32> S, 1:       |V0|V1|V2|V3|S0|S1|S2|S3|
    // extelt <4 x i16> V', 3:               |                 |S2|S3|
    //                                       +--+--+--+--+--+--+--+--+
    // If this is little-endian, S2|S3 are the MSB of the 32-bit 'S' value.
    // If this is big-endian, S2|S3 are the LSB of the 32-bit 'S' value.
    // In this example, we must right-shift little-endian. Big-endian is just a
    // truncate.
    unsigned Chunk = ExtIndexC % NarrowingRatio;
    if (IsBigEndian)
      Chunk = NarrowingRatio - 1 - Chunk;

    // Bail out if this is an FP vector to FP vector sequence. That would take
    // more instructions than we started with unless there is no shift, and it
    // may not be handled as well in the backend.
    bool NeedSrcBitcast = SrcTy->getScalarType()->isFloatingPointTy();
    bool NeedDestBitcast = DestTy->isFloatingPointTy();
    if (NeedSrcBitcast && NeedDestBitcast)
      return nullptr;

    unsigned SrcWidth = SrcTy->getScalarSizeInBits();
    unsigned DestWidth = DestTy->getPrimitiveSizeInBits();
    unsigned ShAmt = Chunk * DestWidth;

    // TODO: This limitation is more strict than necessary. We could sum the
    // number of new instructions and subtract the number eliminated to know if
    // we can proceed.
    if (!X->hasOneUse() || !Ext.getVectorOperand()->hasOneUse())
      if (NeedSrcBitcast || NeedDestBitcast)
        return nullptr;

    if (NeedSrcBitcast) {
      Type *SrcIntTy = IntegerType::getIntNTy(Scalar->getContext(), SrcWidth);
      Scalar = Builder.CreateBitCast(Scalar, SrcIntTy);
    }

    if (ShAmt) {
      // Bail out if we could end with more instructions than we started with.
      if (!Ext.getVectorOperand()->hasOneUse())
        return nullptr;
      Scalar = Builder.CreateLShr(Scalar, ShAmt);
    }

    if (NeedDestBitcast) {
      Type *DestIntTy = IntegerType::getIntNTy(Scalar->getContext(), DestWidth);
      return new BitCastInst(Builder.CreateTrunc(Scalar, DestIntTy), DestTy);
    }
    return new TruncInst(Scalar, DestTy);
  }

  return nullptr;
}

Instruction *InstCombiner::visitExtractElementInst(ExtractElementInst &EI) {
  Value *SrcVec = EI.getVectorOperand();
  Value *Index = EI.getIndexOperand();
  if (Value *V = SimplifyExtractElementInst(SrcVec, Index,
                                            SQ.getWithInstruction(&EI)))
    return replaceInstUsesWith(EI, V);

  // If extracting a specified index from the vector, see if we can recursively
  // find a previously computed scalar that was inserted into the vector.
  auto *IndexC = dyn_cast<ConstantInt>(Index);
  if (IndexC) {
    unsigned NumElts = EI.getVectorOperandType()->getNumElements();

    // InstSimplify should handle cases where the index is invalid.
    if (!IndexC->getValue().ule(NumElts))
      return nullptr;

    // This instruction only demands the single element from the input vector.
    // If the input vector has a single use, simplify it based on this use
    // property.
    if (SrcVec->hasOneUse() && NumElts != 1) {
      APInt UndefElts(NumElts, 0);
      APInt DemandedElts(NumElts, 0);
      DemandedElts.setBit(IndexC->getZExtValue());
      if (Value *V = SimplifyDemandedVectorElts(SrcVec, DemandedElts,
                                                UndefElts)) {
        EI.setOperand(0, V);
        return &EI;
      }
    }

    if (Instruction *I = foldBitcastExtElt(EI, Builder, DL.isBigEndian()))
      return I;

    // If there's a vector PHI feeding a scalar use through this extractelement
    // instruction, try to scalarize the PHI.
    if (auto *Phi = dyn_cast<PHINode>(SrcVec))
      if (Instruction *ScalarPHI = scalarizePHI(EI, Phi))
        return ScalarPHI;
  }

  BinaryOperator *BO;
  if (match(SrcVec, m_BinOp(BO)) && cheapToScalarize(SrcVec, IndexC)) {
    // extelt (binop X, Y), Index --> binop (extelt X, Index), (extelt Y, Index)
    Value *X = BO->getOperand(0), *Y = BO->getOperand(1);
    Value *E0 = Builder.CreateExtractElement(X, Index);
    Value *E1 = Builder.CreateExtractElement(Y, Index);
    return BinaryOperator::CreateWithCopiedFlags(BO->getOpcode(), E0, E1, BO);
  }

  Value *X, *Y;
  CmpInst::Predicate Pred;
  if (match(SrcVec, m_Cmp(Pred, m_Value(X), m_Value(Y))) &&
      cheapToScalarize(SrcVec, IndexC)) {
    // extelt (cmp X, Y), Index --> cmp (extelt X, Index), (extelt Y, Index)
    Value *E0 = Builder.CreateExtractElement(X, Index);
    Value *E1 = Builder.CreateExtractElement(Y, Index);
    return CmpInst::Create(cast<CmpInst>(SrcVec)->getOpcode(), Pred, E0, E1);
  }

  if (auto *I = dyn_cast<Instruction>(SrcVec)) {
    if (auto *IE = dyn_cast<InsertElementInst>(I)) {
      // Extracting the inserted element?
      if (IE->getOperand(2) == Index)
        return replaceInstUsesWith(EI, IE->getOperand(1));
      // If the inserted and extracted elements are constants, they must not
      // be the same value, extract from the pre-inserted value instead.
      if (isa<Constant>(IE->getOperand(2)) && IndexC) {
        Worklist.AddValue(SrcVec);
        EI.setOperand(0, IE->getOperand(0));
        return &EI;
      }
    } else if (auto *SVI = dyn_cast<ShuffleVectorInst>(I)) {
      // If this is extracting an element from a shufflevector, figure out where
      // it came from and extract from the appropriate input element instead.
      if (auto *Elt = dyn_cast<ConstantInt>(Index)) {
        int SrcIdx = SVI->getMaskValue(Elt->getZExtValue());
        Value *Src;
        unsigned LHSWidth =
          SVI->getOperand(0)->getType()->getVectorNumElements();

        if (SrcIdx < 0)
          return replaceInstUsesWith(EI, UndefValue::get(EI.getType()));
        if (SrcIdx < (int)LHSWidth)
          Src = SVI->getOperand(0);
        else {
          SrcIdx -= LHSWidth;
          Src = SVI->getOperand(1);
        }
        Type *Int32Ty = Type::getInt32Ty(EI.getContext());
        return ExtractElementInst::Create(Src,
                                          ConstantInt::get(Int32Ty,
                                                           SrcIdx, false));
      }
    } else if (auto *CI = dyn_cast<CastInst>(I)) {
      // Canonicalize extractelement(cast) -> cast(extractelement).
      // Bitcasts can change the number of vector elements, and they cost
      // nothing.
      if (CI->hasOneUse() && (CI->getOpcode() != Instruction::BitCast)) {
        Value *EE = Builder.CreateExtractElement(CI->getOperand(0), Index);
        Worklist.AddValue(EE);
        return CastInst::Create(CI->getOpcode(), EE, EI.getType());
      }
    }
  }
  return nullptr;
}

/// If V is a shuffle of values that ONLY returns elements from either LHS or
/// RHS, return the shuffle mask and true. Otherwise, return false.
static bool collectSingleShuffleElements(Value *V, Value *LHS, Value *RHS,
                                         SmallVectorImpl<Constant*> &Mask) {
  assert(LHS->getType() == RHS->getType() &&
         "Invalid CollectSingleShuffleElements");
  unsigned NumElts = V->getType()->getVectorNumElements();

  if (isa<UndefValue>(V)) {
    Mask.assign(NumElts, UndefValue::get(Type::getInt32Ty(V->getContext())));
    return true;
  }

  if (V == LHS) {
    for (unsigned i = 0; i != NumElts; ++i)
      Mask.push_back(ConstantInt::get(Type::getInt32Ty(V->getContext()), i));
    return true;
  }

  if (V == RHS) {
    for (unsigned i = 0; i != NumElts; ++i)
      Mask.push_back(ConstantInt::get(Type::getInt32Ty(V->getContext()),
                                      i+NumElts));
    return true;
  }

  if (InsertElementInst *IEI = dyn_cast<InsertElementInst>(V)) {
    // If this is an insert of an extract from some other vector, include it.
    Value *VecOp    = IEI->getOperand(0);
    Value *ScalarOp = IEI->getOperand(1);
    Value *IdxOp    = IEI->getOperand(2);

    if (!isa<ConstantInt>(IdxOp))
      return false;
    unsigned InsertedIdx = cast<ConstantInt>(IdxOp)->getZExtValue();

    if (isa<UndefValue>(ScalarOp)) {  // inserting undef into vector.
      // We can handle this if the vector we are inserting into is
      // transitively ok.
      if (collectSingleShuffleElements(VecOp, LHS, RHS, Mask)) {
        // If so, update the mask to reflect the inserted undef.
        Mask[InsertedIdx] = UndefValue::get(Type::getInt32Ty(V->getContext()));
        return true;
      }
    } else if (ExtractElementInst *EI = dyn_cast<ExtractElementInst>(ScalarOp)){
      if (isa<ConstantInt>(EI->getOperand(1))) {
        unsigned ExtractedIdx =
        cast<ConstantInt>(EI->getOperand(1))->getZExtValue();
        unsigned NumLHSElts = LHS->getType()->getVectorNumElements();

        // This must be extracting from either LHS or RHS.
        if (EI->getOperand(0) == LHS || EI->getOperand(0) == RHS) {
          // We can handle this if the vector we are inserting into is
          // transitively ok.
          if (collectSingleShuffleElements(VecOp, LHS, RHS, Mask)) {
            // If so, update the mask to reflect the inserted value.
            if (EI->getOperand(0) == LHS) {
              Mask[InsertedIdx % NumElts] =
              ConstantInt::get(Type::getInt32Ty(V->getContext()),
                               ExtractedIdx);
            } else {
              assert(EI->getOperand(0) == RHS);
              Mask[InsertedIdx % NumElts] =
              ConstantInt::get(Type::getInt32Ty(V->getContext()),
                               ExtractedIdx + NumLHSElts);
            }
            return true;
          }
        }
      }
    }
  }

  return false;
}

/// If we have insertion into a vector that is wider than the vector that we
/// are extracting from, try to widen the source vector to allow a single
/// shufflevector to replace one or more insert/extract pairs.
static void replaceExtractElements(InsertElementInst *InsElt,
                                   ExtractElementInst *ExtElt,
                                   InstCombiner &IC) {
  VectorType *InsVecType = InsElt->getType();
  VectorType *ExtVecType = ExtElt->getVectorOperandType();
  unsigned NumInsElts = InsVecType->getVectorNumElements();
  unsigned NumExtElts = ExtVecType->getVectorNumElements();

  // The inserted-to vector must be wider than the extracted-from vector.
  if (InsVecType->getElementType() != ExtVecType->getElementType() ||
      NumExtElts >= NumInsElts)
    return;

  // Create a shuffle mask to widen the extended-from vector using undefined
  // values. The mask selects all of the values of the original vector followed
  // by as many undefined values as needed to create a vector of the same length
  // as the inserted-to vector.
  SmallVector<Constant *, 16> ExtendMask;
  IntegerType *IntType = Type::getInt32Ty(InsElt->getContext());
  for (unsigned i = 0; i < NumExtElts; ++i)
    ExtendMask.push_back(ConstantInt::get(IntType, i));
  for (unsigned i = NumExtElts; i < NumInsElts; ++i)
    ExtendMask.push_back(UndefValue::get(IntType));

  Value *ExtVecOp = ExtElt->getVectorOperand();
  auto *ExtVecOpInst = dyn_cast<Instruction>(ExtVecOp);
  BasicBlock *InsertionBlock = (ExtVecOpInst && !isa<PHINode>(ExtVecOpInst))
                                   ? ExtVecOpInst->getParent()
                                   : ExtElt->getParent();

  // TODO: This restriction matches the basic block check below when creating
  // new extractelement instructions. If that limitation is removed, this one
  // could also be removed. But for now, we just bail out to ensure that we
  // will replace the extractelement instruction that is feeding our
  // insertelement instruction. This allows the insertelement to then be
  // replaced by a shufflevector. If the insertelement is not replaced, we can
  // induce infinite looping because there's an optimization for extractelement
  // that will delete our widening shuffle. This would trigger another attempt
  // here to create that shuffle, and we spin forever.
  if (InsertionBlock != InsElt->getParent())
    return;

  // TODO: This restriction matches the check in visitInsertElementInst() and
  // prevents an infinite loop caused by not turning the extract/insert pair
  // into a shuffle. We really should not need either check, but we're lacking
  // folds for shufflevectors because we're afraid to generate shuffle masks
  // that the backend can't handle.
  if (InsElt->hasOneUse() && isa<InsertElementInst>(InsElt->user_back()))
    return;

  auto *WideVec = new ShuffleVectorInst(ExtVecOp, UndefValue::get(ExtVecType),
                                        ConstantVector::get(ExtendMask));

  // Insert the new shuffle after the vector operand of the extract is defined
  // (as long as it's not a PHI) or at the start of the basic block of the
  // extract, so any subsequent extracts in the same basic block can use it.
  // TODO: Insert before the earliest ExtractElementInst that is replaced.
  if (ExtVecOpInst && !isa<PHINode>(ExtVecOpInst))
    WideVec->insertAfter(ExtVecOpInst);
  else
    IC.InsertNewInstWith(WideVec, *ExtElt->getParent()->getFirstInsertionPt());

  // Replace extracts from the original narrow vector with extracts from the new
  // wide vector.
  for (User *U : ExtVecOp->users()) {
    ExtractElementInst *OldExt = dyn_cast<ExtractElementInst>(U);
    if (!OldExt || OldExt->getParent() != WideVec->getParent())
      continue;
    auto *NewExt = ExtractElementInst::Create(WideVec, OldExt->getOperand(1));
    NewExt->insertAfter(OldExt);
    IC.replaceInstUsesWith(*OldExt, NewExt);
  }
}

/// We are building a shuffle to create V, which is a sequence of insertelement,
/// extractelement pairs. If PermittedRHS is set, then we must either use it or
/// not rely on the second vector source. Return a std::pair containing the
/// left and right vectors of the proposed shuffle (or 0), and set the Mask
/// parameter as required.
///
/// Note: we intentionally don't try to fold earlier shuffles since they have
/// often been chosen carefully to be efficiently implementable on the target.
using ShuffleOps = std::pair<Value *, Value *>;

static ShuffleOps collectShuffleElements(Value *V,
                                         SmallVectorImpl<Constant *> &Mask,
                                         Value *PermittedRHS,
                                         InstCombiner &IC) {
  assert(V->getType()->isVectorTy() && "Invalid shuffle!");
  unsigned NumElts = V->getType()->getVectorNumElements();

  if (isa<UndefValue>(V)) {
    Mask.assign(NumElts, UndefValue::get(Type::getInt32Ty(V->getContext())));
    return std::make_pair(
        PermittedRHS ? UndefValue::get(PermittedRHS->getType()) : V, nullptr);
  }

  if (isa<ConstantAggregateZero>(V)) {
    Mask.assign(NumElts, ConstantInt::get(Type::getInt32Ty(V->getContext()),0));
    return std::make_pair(V, nullptr);
  }

  if (InsertElementInst *IEI = dyn_cast<InsertElementInst>(V)) {
    // If this is an insert of an extract from some other vector, include it.
    Value *VecOp    = IEI->getOperand(0);
    Value *ScalarOp = IEI->getOperand(1);
    Value *IdxOp    = IEI->getOperand(2);

    if (ExtractElementInst *EI = dyn_cast<ExtractElementInst>(ScalarOp)) {
      if (isa<ConstantInt>(EI->getOperand(1)) && isa<ConstantInt>(IdxOp)) {
        unsigned ExtractedIdx =
          cast<ConstantInt>(EI->getOperand(1))->getZExtValue();
        unsigned InsertedIdx = cast<ConstantInt>(IdxOp)->getZExtValue();

        // Either the extracted from or inserted into vector must be RHSVec,
        // otherwise we'd end up with a shuffle of three inputs.
        if (EI->getOperand(0) == PermittedRHS || PermittedRHS == nullptr) {
          Value *RHS = EI->getOperand(0);
          ShuffleOps LR = collectShuffleElements(VecOp, Mask, RHS, IC);
          assert(LR.second == nullptr || LR.second == RHS);

          if (LR.first->getType() != RHS->getType()) {
            // Although we are giving up for now, see if we can create extracts
            // that match the inserts for another round of combining.
            replaceExtractElements(IEI, EI, IC);

            // We tried our best, but we can't find anything compatible with RHS
            // further up the chain. Return a trivial shuffle.
            for (unsigned i = 0; i < NumElts; ++i)
              Mask[i] = ConstantInt::get(Type::getInt32Ty(V->getContext()), i);
            return std::make_pair(V, nullptr);
          }

          unsigned NumLHSElts = RHS->getType()->getVectorNumElements();
          Mask[InsertedIdx % NumElts] =
            ConstantInt::get(Type::getInt32Ty(V->getContext()),
                             NumLHSElts+ExtractedIdx);
          return std::make_pair(LR.first, RHS);
        }

        if (VecOp == PermittedRHS) {
          // We've gone as far as we can: anything on the other side of the
          // extractelement will already have been converted into a shuffle.
          unsigned NumLHSElts =
              EI->getOperand(0)->getType()->getVectorNumElements();
          for (unsigned i = 0; i != NumElts; ++i)
            Mask.push_back(ConstantInt::get(
                Type::getInt32Ty(V->getContext()),
                i == InsertedIdx ? ExtractedIdx : NumLHSElts + i));
          return std::make_pair(EI->getOperand(0), PermittedRHS);
        }

        // If this insertelement is a chain that comes from exactly these two
        // vectors, return the vector and the effective shuffle.
        if (EI->getOperand(0)->getType() == PermittedRHS->getType() &&
            collectSingleShuffleElements(IEI, EI->getOperand(0), PermittedRHS,
                                         Mask))
          return std::make_pair(EI->getOperand(0), PermittedRHS);
      }
    }
  }

  // Otherwise, we can't do anything fancy. Return an identity vector.
  for (unsigned i = 0; i != NumElts; ++i)
    Mask.push_back(ConstantInt::get(Type::getInt32Ty(V->getContext()), i));
  return std::make_pair(V, nullptr);
}

/// Try to find redundant insertvalue instructions, like the following ones:
///  %0 = insertvalue { i8, i32 } undef, i8 %x, 0
///  %1 = insertvalue { i8, i32 } %0,    i8 %y, 0
/// Here the second instruction inserts values at the same indices, as the
/// first one, making the first one redundant.
/// It should be transformed to:
///  %0 = insertvalue { i8, i32 } undef, i8 %y, 0
Instruction *InstCombiner::visitInsertValueInst(InsertValueInst &I) {
  bool IsRedundant = false;
  ArrayRef<unsigned int> FirstIndices = I.getIndices();

  // If there is a chain of insertvalue instructions (each of them except the
  // last one has only one use and it's another insertvalue insn from this
  // chain), check if any of the 'children' uses the same indices as the first
  // instruction. In this case, the first one is redundant.
  Value *V = &I;
  unsigned Depth = 0;
  while (V->hasOneUse() && Depth < 10) {
    User *U = V->user_back();
    auto UserInsInst = dyn_cast<InsertValueInst>(U);
    if (!UserInsInst || U->getOperand(0) != V)
      break;
    if (UserInsInst->getIndices() == FirstIndices) {
      IsRedundant = true;
      break;
    }
    V = UserInsInst;
    Depth++;
  }

  if (IsRedundant)
    return replaceInstUsesWith(I, I.getOperand(0));
  return nullptr;
}

static bool isShuffleEquivalentToSelect(ShuffleVectorInst &Shuf) {
  int MaskSize = Shuf.getMask()->getType()->getVectorNumElements();
  int VecSize = Shuf.getOperand(0)->getType()->getVectorNumElements();

  // A vector select does not change the size of the operands.
  if (MaskSize != VecSize)
    return false;

  // Each mask element must be undefined or choose a vector element from one of
  // the source operands without crossing vector lanes.
  for (int i = 0; i != MaskSize; ++i) {
    int Elt = Shuf.getMaskValue(i);
    if (Elt != -1 && Elt != i && Elt != i + VecSize)
      return false;
  }

  return true;
}

// Turn a chain of inserts that splats a value into a canonical insert + shuffle
// splat. That is:
// insertelt(insertelt(insertelt(insertelt X, %k, 0), %k, 1), %k, 2) ... ->
// shufflevector(insertelt(X, %k, 0), undef, zero)
static Instruction *foldInsSequenceIntoBroadcast(InsertElementInst &InsElt) {
  // We are interested in the last insert in a chain. So, if this insert
  // has a single user, and that user is an insert, bail.
  if (InsElt.hasOneUse() && isa<InsertElementInst>(InsElt.user_back()))
    return nullptr;

  VectorType *VT = cast<VectorType>(InsElt.getType());
  int NumElements = VT->getNumElements();

  // Do not try to do this for a one-element vector, since that's a nop,
  // and will cause an inf-loop.
  if (NumElements == 1)
    return nullptr;

  Value *SplatVal = InsElt.getOperand(1);
  InsertElementInst *CurrIE = &InsElt;
  SmallVector<bool, 16> ElementPresent(NumElements, false);
  InsertElementInst *FirstIE = nullptr;

  // Walk the chain backwards, keeping track of which indices we inserted into,
  // until we hit something that isn't an insert of the splatted value.
  while (CurrIE) {
    auto *Idx = dyn_cast<ConstantInt>(CurrIE->getOperand(2));
    if (!Idx || CurrIE->getOperand(1) != SplatVal)
      return nullptr;

    auto *NextIE = dyn_cast<InsertElementInst>(CurrIE->getOperand(0));
    // Check none of the intermediate steps have any additional uses, except
    // for the root insertelement instruction, which can be re-used, if it
    // inserts at position 0.
    if (CurrIE != &InsElt &&
        (!CurrIE->hasOneUse() && (NextIE != nullptr || !Idx->isZero())))
      return nullptr;

    ElementPresent[Idx->getZExtValue()] = true;
    FirstIE = CurrIE;
    CurrIE = NextIE;
  }

  // Make sure we've seen an insert into every element.
  if (llvm::any_of(ElementPresent, [](bool Present) { return !Present; }))
    return nullptr;

  // All right, create the insert + shuffle.
  Instruction *InsertFirst;
  if (cast<ConstantInt>(FirstIE->getOperand(2))->isZero())
    InsertFirst = FirstIE;
  else
    InsertFirst = InsertElementInst::Create(
        UndefValue::get(VT), SplatVal,
        ConstantInt::get(Type::getInt32Ty(InsElt.getContext()), 0),
        "", &InsElt);

  Constant *ZeroMask = ConstantAggregateZero::get(
      VectorType::get(Type::getInt32Ty(InsElt.getContext()), NumElements));

  return new ShuffleVectorInst(InsertFirst, UndefValue::get(VT), ZeroMask);
}

/// If we have an insertelement instruction feeding into another insertelement
/// and the 2nd is inserting a constant into the vector, canonicalize that
/// constant insertion before the insertion of a variable:
///
/// insertelement (insertelement X, Y, IdxC1), ScalarC, IdxC2 -->
/// insertelement (insertelement X, ScalarC, IdxC2), Y, IdxC1
///
/// This has the potential of eliminating the 2nd insertelement instruction
/// via constant folding of the scalar constant into a vector constant.
static Instruction *hoistInsEltConst(InsertElementInst &InsElt2,
                                     InstCombiner::BuilderTy &Builder) {
  auto *InsElt1 = dyn_cast<InsertElementInst>(InsElt2.getOperand(0));
  if (!InsElt1 || !InsElt1->hasOneUse())
    return nullptr;

  Value *X, *Y;
  Constant *ScalarC;
  ConstantInt *IdxC1, *IdxC2;
  if (match(InsElt1->getOperand(0), m_Value(X)) &&
      match(InsElt1->getOperand(1), m_Value(Y)) && !isa<Constant>(Y) &&
      match(InsElt1->getOperand(2), m_ConstantInt(IdxC1)) &&
      match(InsElt2.getOperand(1), m_Constant(ScalarC)) &&
      match(InsElt2.getOperand(2), m_ConstantInt(IdxC2)) && IdxC1 != IdxC2) {
    Value *NewInsElt1 = Builder.CreateInsertElement(X, ScalarC, IdxC2);
    return InsertElementInst::Create(NewInsElt1, Y, IdxC1);
  }

  return nullptr;
}

/// insertelt (shufflevector X, CVec, Mask|insertelt X, C1, CIndex1), C, CIndex
/// --> shufflevector X, CVec', Mask'
static Instruction *foldConstantInsEltIntoShuffle(InsertElementInst &InsElt) {
  auto *Inst = dyn_cast<Instruction>(InsElt.getOperand(0));
  // Bail out if the parent has more than one use. In that case, we'd be
  // replacing the insertelt with a shuffle, and that's not a clear win.
  if (!Inst || !Inst->hasOneUse())
    return nullptr;
  if (auto *Shuf = dyn_cast<ShuffleVectorInst>(InsElt.getOperand(0))) {
    // The shuffle must have a constant vector operand. The insertelt must have
    // a constant scalar being inserted at a constant position in the vector.
    Constant *ShufConstVec, *InsEltScalar;
    uint64_t InsEltIndex;
    if (!match(Shuf->getOperand(1), m_Constant(ShufConstVec)) ||
        !match(InsElt.getOperand(1), m_Constant(InsEltScalar)) ||
        !match(InsElt.getOperand(2), m_ConstantInt(InsEltIndex)))
      return nullptr;

    // Adding an element to an arbitrary shuffle could be expensive, but a
    // shuffle that selects elements from vectors without crossing lanes is
    // assumed cheap.
    // If we're just adding a constant into that shuffle, it will still be
    // cheap.
    if (!isShuffleEquivalentToSelect(*Shuf))
      return nullptr;

    // From the above 'select' check, we know that the mask has the same number
    // of elements as the vector input operands. We also know that each constant
    // input element is used in its lane and can not be used more than once by
    // the shuffle. Therefore, replace the constant in the shuffle's constant
    // vector with the insertelt constant. Replace the constant in the shuffle's
    // mask vector with the insertelt index plus the length of the vector
    // (because the constant vector operand of a shuffle is always the 2nd
    // operand).
    Constant *Mask = Shuf->getMask();
    unsigned NumElts = Mask->getType()->getVectorNumElements();
    SmallVector<Constant *, 16> NewShufElts(NumElts);
    SmallVector<Constant *, 16> NewMaskElts(NumElts);
    for (unsigned I = 0; I != NumElts; ++I) {
      if (I == InsEltIndex) {
        NewShufElts[I] = InsEltScalar;
        Type *Int32Ty = Type::getInt32Ty(Shuf->getContext());
        NewMaskElts[I] = ConstantInt::get(Int32Ty, InsEltIndex + NumElts);
      } else {
        // Copy over the existing values.
        NewShufElts[I] = ShufConstVec->getAggregateElement(I);
        NewMaskElts[I] = Mask->getAggregateElement(I);
      }
    }

    // Create new operands for a shuffle that includes the constant of the
    // original insertelt. The old shuffle will be dead now.
    return new ShuffleVectorInst(Shuf->getOperand(0),
                                 ConstantVector::get(NewShufElts),
                                 ConstantVector::get(NewMaskElts));
  } else if (auto *IEI = dyn_cast<InsertElementInst>(Inst)) {
    // Transform sequences of insertelements ops with constant data/indexes into
    // a single shuffle op.
    unsigned NumElts = InsElt.getType()->getNumElements();

    uint64_t InsertIdx[2];
    Constant *Val[2];
    if (!match(InsElt.getOperand(2), m_ConstantInt(InsertIdx[0])) ||
        !match(InsElt.getOperand(1), m_Constant(Val[0])) ||
        !match(IEI->getOperand(2), m_ConstantInt(InsertIdx[1])) ||
        !match(IEI->getOperand(1), m_Constant(Val[1])))
      return nullptr;
    SmallVector<Constant *, 16> Values(NumElts);
    SmallVector<Constant *, 16> Mask(NumElts);
    auto ValI = std::begin(Val);
    // Generate new constant vector and mask.
    // We have 2 values/masks from the insertelements instructions. Insert them
    // into new value/mask vectors.
    for (uint64_t I : InsertIdx) {
      if (!Values[I]) {
        assert(!Mask[I]);
        Values[I] = *ValI;
        Mask[I] = ConstantInt::get(Type::getInt32Ty(InsElt.getContext()),
                                   NumElts + I);
      }
      ++ValI;
    }
    // Remaining values are filled with 'undef' values.
    for (unsigned I = 0; I < NumElts; ++I) {
      if (!Values[I]) {
        assert(!Mask[I]);
        Values[I] = UndefValue::get(InsElt.getType()->getElementType());
        Mask[I] = ConstantInt::get(Type::getInt32Ty(InsElt.getContext()), I);
      }
    }
    // Create new operands for a shuffle that includes the constant of the
    // original insertelt.
    return new ShuffleVectorInst(IEI->getOperand(0),
                                 ConstantVector::get(Values),
                                 ConstantVector::get(Mask));
  }
  return nullptr;
}

Instruction *InstCombiner::visitInsertElementInst(InsertElementInst &IE) {
  Value *VecOp    = IE.getOperand(0);
  Value *ScalarOp = IE.getOperand(1);
  Value *IdxOp    = IE.getOperand(2);

  if (auto *V = SimplifyInsertElementInst(
          VecOp, ScalarOp, IdxOp, SQ.getWithInstruction(&IE)))
    return replaceInstUsesWith(IE, V);

  // Inserting an undef or into an undefined place, remove this.
  if (isa<UndefValue>(ScalarOp) || isa<UndefValue>(IdxOp))
    replaceInstUsesWith(IE, VecOp);

  // If the inserted element was extracted from some other vector and both
  // indexes are constant, try to turn this into a shuffle.
  uint64_t InsertedIdx, ExtractedIdx;
  Value *ExtVecOp;
  if (match(IdxOp, m_ConstantInt(InsertedIdx)) &&
      match(ScalarOp, m_ExtractElement(m_Value(ExtVecOp),
                                       m_ConstantInt(ExtractedIdx)))) {
    unsigned NumInsertVectorElts = IE.getType()->getNumElements();
    unsigned NumExtractVectorElts = ExtVecOp->getType()->getVectorNumElements();
    if (ExtractedIdx >= NumExtractVectorElts) // Out of range extract.
      return replaceInstUsesWith(IE, VecOp);

    if (InsertedIdx >= NumInsertVectorElts)  // Out of range insert.
      return replaceInstUsesWith(IE, UndefValue::get(IE.getType()));

    // If we are extracting a value from a vector, then inserting it right
    // back into the same place, just use the input vector.
    if (ExtVecOp == VecOp && ExtractedIdx == InsertedIdx)
      return replaceInstUsesWith(IE, VecOp);

    // TODO: Looking at the user(s) to determine if this insert is a
    // fold-to-shuffle opportunity does not match the usual instcombine
    // constraints. We should decide if the transform is worthy based only
    // on this instruction and its operands, but that may not work currently.
    //
    // Here, we are trying to avoid creating shuffles before reaching
    // the end of a chain of extract-insert pairs. This is complicated because
    // we do not generally form arbitrary shuffle masks in instcombine
    // (because those may codegen poorly), but collectShuffleElements() does
    // exactly that.
    //
    // The rules for determining what is an acceptable target-independent
    // shuffle mask are fuzzy because they evolve based on the backend's
    // capabilities and real-world impact.
    auto isShuffleRootCandidate = [](InsertElementInst &Insert) {
      if (!Insert.hasOneUse())
        return true;
      auto *InsertUser = dyn_cast<InsertElementInst>(Insert.user_back());
      if (!InsertUser)
        return true;
      return false;
    };

    // Try to form a shuffle from a chain of extract-insert ops.
    if (isShuffleRootCandidate(IE)) {
      SmallVector<Constant*, 16> Mask;
      ShuffleOps LR = collectShuffleElements(&IE, Mask, nullptr, *this);

      // The proposed shuffle may be trivial, in which case we shouldn't
      // perform the combine.
      if (LR.first != &IE && LR.second != &IE) {
        // We now have a shuffle of LHS, RHS, Mask.
        if (LR.second == nullptr)
          LR.second = UndefValue::get(LR.first->getType());
        return new ShuffleVectorInst(LR.first, LR.second,
                                     ConstantVector::get(Mask));
      }
    }
  }

  unsigned VWidth = VecOp->getType()->getVectorNumElements();
  APInt UndefElts(VWidth, 0);
  APInt AllOnesEltMask(APInt::getAllOnesValue(VWidth));
  if (Value *V = SimplifyDemandedVectorElts(&IE, AllOnesEltMask, UndefElts)) {
    if (V != &IE)
      return replaceInstUsesWith(IE, V);
    return &IE;
  }

  if (Instruction *Shuf = foldConstantInsEltIntoShuffle(IE))
    return Shuf;

  if (Instruction *NewInsElt = hoistInsEltConst(IE, Builder))
    return NewInsElt;

  // Turn a sequence of inserts that broadcasts a scalar into a single
  // insert + shufflevector.
  if (Instruction *Broadcast = foldInsSequenceIntoBroadcast(IE))
    return Broadcast;

  return nullptr;
}

/// Return true if we can evaluate the specified expression tree if the vector
/// elements were shuffled in a different order.
static bool canEvaluateShuffled(Value *V, ArrayRef<int> Mask,
                                unsigned Depth = 5) {
  // We can always reorder the elements of a constant.
  if (isa<Constant>(V))
    return true;

  // We won't reorder vector arguments. No IPO here.
  Instruction *I = dyn_cast<Instruction>(V);
  if (!I) return false;

  // Two users may expect different orders of the elements. Don't try it.
  if (!I->hasOneUse())
    return false;

  if (Depth == 0) return false;

  switch (I->getOpcode()) {
    case Instruction::Add:
    case Instruction::FAdd:
    case Instruction::Sub:
    case Instruction::FSub:
    case Instruction::Mul:
    case Instruction::FMul:
    case Instruction::UDiv:
    case Instruction::SDiv:
    case Instruction::FDiv:
    case Instruction::URem:
    case Instruction::SRem:
    case Instruction::FRem:
    case Instruction::Shl:
    case Instruction::LShr:
    case Instruction::AShr:
    case Instruction::And:
    case Instruction::Or:
    case Instruction::Xor:
    case Instruction::ICmp:
    case Instruction::FCmp:
    case Instruction::Trunc:
    case Instruction::ZExt:
    case Instruction::SExt:
    case Instruction::FPToUI:
    case Instruction::FPToSI:
    case Instruction::UIToFP:
    case Instruction::SIToFP:
    case Instruction::FPTrunc:
    case Instruction::FPExt:
    case Instruction::GetElementPtr: {
      // Bail out if we would create longer vector ops. We could allow creating
      // longer vector ops, but that may result in more expensive codegen. We
      // would also need to limit the transform to avoid undefined behavior for
      // integer div/rem.
      Type *ITy = I->getType();
      if (ITy->isVectorTy() && Mask.size() > ITy->getVectorNumElements())
        return false;
      for (Value *Operand : I->operands()) {
        if (!canEvaluateShuffled(Operand, Mask, Depth - 1))
          return false;
      }
      return true;
    }
    case Instruction::InsertElement: {
      ConstantInt *CI = dyn_cast<ConstantInt>(I->getOperand(2));
      if (!CI) return false;
      int ElementNumber = CI->getLimitedValue();

      // Verify that 'CI' does not occur twice in Mask. A single 'insertelement'
      // can't put an element into multiple indices.
      bool SeenOnce = false;
      for (int i = 0, e = Mask.size(); i != e; ++i) {
        if (Mask[i] == ElementNumber) {
          if (SeenOnce)
            return false;
          SeenOnce = true;
        }
      }
      return canEvaluateShuffled(I->getOperand(0), Mask, Depth - 1);
    }
  }
  return false;
}

/// Rebuild a new instruction just like 'I' but with the new operands given.
/// In the event of type mismatch, the type of the operands is correct.
static Value *buildNew(Instruction *I, ArrayRef<Value*> NewOps) {
  // We don't want to use the IRBuilder here because we want the replacement
  // instructions to appear next to 'I', not the builder's insertion point.
  switch (I->getOpcode()) {
    case Instruction::Add:
    case Instruction::FAdd:
    case Instruction::Sub:
    case Instruction::FSub:
    case Instruction::Mul:
    case Instruction::FMul:
    case Instruction::UDiv:
    case Instruction::SDiv:
    case Instruction::FDiv:
    case Instruction::URem:
    case Instruction::SRem:
    case Instruction::FRem:
    case Instruction::Shl:
    case Instruction::LShr:
    case Instruction::AShr:
    case Instruction::And:
    case Instruction::Or:
    case Instruction::Xor: {
      BinaryOperator *BO = cast<BinaryOperator>(I);
      assert(NewOps.size() == 2 && "binary operator with #ops != 2");
      BinaryOperator *New =
          BinaryOperator::Create(cast<BinaryOperator>(I)->getOpcode(),
                                 NewOps[0], NewOps[1], "", BO);
      if (isa<OverflowingBinaryOperator>(BO)) {
        New->setHasNoUnsignedWrap(BO->hasNoUnsignedWrap());
        New->setHasNoSignedWrap(BO->hasNoSignedWrap());
      }
      if (isa<PossiblyExactOperator>(BO)) {
        New->setIsExact(BO->isExact());
      }
      if (isa<FPMathOperator>(BO))
        New->copyFastMathFlags(I);
      return New;
    }
    case Instruction::ICmp:
      assert(NewOps.size() == 2 && "icmp with #ops != 2");
      return new ICmpInst(I, cast<ICmpInst>(I)->getPredicate(),
                          NewOps[0], NewOps[1]);
    case Instruction::FCmp:
      assert(NewOps.size() == 2 && "fcmp with #ops != 2");
      return new FCmpInst(I, cast<FCmpInst>(I)->getPredicate(),
                          NewOps[0], NewOps[1]);
    case Instruction::Trunc:
    case Instruction::ZExt:
    case Instruction::SExt:
    case Instruction::FPToUI:
    case Instruction::FPToSI:
    case Instruction::UIToFP:
    case Instruction::SIToFP:
    case Instruction::FPTrunc:
    case Instruction::FPExt: {
      // It's possible that the mask has a different number of elements from
      // the original cast. We recompute the destination type to match the mask.
      Type *DestTy =
          VectorType::get(I->getType()->getScalarType(),
                          NewOps[0]->getType()->getVectorNumElements());
      assert(NewOps.size() == 1 && "cast with #ops != 1");
      return CastInst::Create(cast<CastInst>(I)->getOpcode(), NewOps[0], DestTy,
                              "", I);
    }
    case Instruction::GetElementPtr: {
      Value *Ptr = NewOps[0];
      ArrayRef<Value*> Idx = NewOps.slice(1);
      GetElementPtrInst *GEP = GetElementPtrInst::Create(
          cast<GetElementPtrInst>(I)->getSourceElementType(), Ptr, Idx, "", I);
      GEP->setIsInBounds(cast<GetElementPtrInst>(I)->isInBounds());
      return GEP;
    }
  }
  llvm_unreachable("failed to rebuild vector instructions");
}

static Value *evaluateInDifferentElementOrder(Value *V, ArrayRef<int> Mask) {
  // Mask.size() does not need to be equal to the number of vector elements.

  assert(V->getType()->isVectorTy() && "can't reorder non-vector elements");
  Type *EltTy = V->getType()->getScalarType();
  Type *I32Ty = IntegerType::getInt32Ty(V->getContext());
  if (isa<UndefValue>(V))
    return UndefValue::get(VectorType::get(EltTy, Mask.size()));

  if (isa<ConstantAggregateZero>(V))
    return ConstantAggregateZero::get(VectorType::get(EltTy, Mask.size()));

  if (Constant *C = dyn_cast<Constant>(V)) {
    SmallVector<Constant *, 16> MaskValues;
    for (int i = 0, e = Mask.size(); i != e; ++i) {
      if (Mask[i] == -1)
        MaskValues.push_back(UndefValue::get(I32Ty));
      else
        MaskValues.push_back(ConstantInt::get(I32Ty, Mask[i]));
    }
    return ConstantExpr::getShuffleVector(C, UndefValue::get(C->getType()),
                                          ConstantVector::get(MaskValues));
  }

  Instruction *I = cast<Instruction>(V);
  switch (I->getOpcode()) {
    case Instruction::Add:
    case Instruction::FAdd:
    case Instruction::Sub:
    case Instruction::FSub:
    case Instruction::Mul:
    case Instruction::FMul:
    case Instruction::UDiv:
    case Instruction::SDiv:
    case Instruction::FDiv:
    case Instruction::URem:
    case Instruction::SRem:
    case Instruction::FRem:
    case Instruction::Shl:
    case Instruction::LShr:
    case Instruction::AShr:
    case Instruction::And:
    case Instruction::Or:
    case Instruction::Xor:
    case Instruction::ICmp:
    case Instruction::FCmp:
    case Instruction::Trunc:
    case Instruction::ZExt:
    case Instruction::SExt:
    case Instruction::FPToUI:
    case Instruction::FPToSI:
    case Instruction::UIToFP:
    case Instruction::SIToFP:
    case Instruction::FPTrunc:
    case Instruction::FPExt:
    case Instruction::Select:
    case Instruction::GetElementPtr: {
      SmallVector<Value*, 8> NewOps;
      bool NeedsRebuild = (Mask.size() != I->getType()->getVectorNumElements());
      for (int i = 0, e = I->getNumOperands(); i != e; ++i) {
        Value *V = evaluateInDifferentElementOrder(I->getOperand(i), Mask);
        NewOps.push_back(V);
        NeedsRebuild |= (V != I->getOperand(i));
      }
      if (NeedsRebuild) {
        return buildNew(I, NewOps);
      }
      return I;
    }
    case Instruction::InsertElement: {
      int Element = cast<ConstantInt>(I->getOperand(2))->getLimitedValue();

      // The insertelement was inserting at Element. Figure out which element
      // that becomes after shuffling. The answer is guaranteed to be unique
      // by CanEvaluateShuffled.
      bool Found = false;
      int Index = 0;
      for (int e = Mask.size(); Index != e; ++Index) {
        if (Mask[Index] == Element) {
          Found = true;
          break;
        }
      }

      // If element is not in Mask, no need to handle the operand 1 (element to
      // be inserted). Just evaluate values in operand 0 according to Mask.
      if (!Found)
        return evaluateInDifferentElementOrder(I->getOperand(0), Mask);

      Value *V = evaluateInDifferentElementOrder(I->getOperand(0), Mask);
      return InsertElementInst::Create(V, I->getOperand(1),
                                       ConstantInt::get(I32Ty, Index), "", I);
    }
  }
  llvm_unreachable("failed to reorder elements of vector instruction!");
}

static void recognizeIdentityMask(const SmallVectorImpl<int> &Mask,
                                  bool &isLHSID, bool &isRHSID) {
  isLHSID = isRHSID = true;

  for (unsigned i = 0, e = Mask.size(); i != e; ++i) {
    if (Mask[i] < 0) continue;  // Ignore undef values.
    // Is this an identity shuffle of the LHS value?
    isLHSID &= (Mask[i] == (int)i);

    // Is this an identity shuffle of the RHS value?
    isRHSID &= (Mask[i]-e == i);
  }
}

// Returns true if the shuffle is extracting a contiguous range of values from
// LHS, for example:
//                 +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
//   Input:        |AA|BB|CC|DD|EE|FF|GG|HH|II|JJ|KK|LL|MM|NN|OO|PP|
//   Shuffles to:  |EE|FF|GG|HH|
//                 +--+--+--+--+
static bool isShuffleExtractingFromLHS(ShuffleVectorInst &SVI,
                                       SmallVector<int, 16> &Mask) {
  unsigned LHSElems = SVI.getOperand(0)->getType()->getVectorNumElements();
  unsigned MaskElems = Mask.size();
  unsigned BegIdx = Mask.front();
  unsigned EndIdx = Mask.back();
  if (BegIdx > EndIdx || EndIdx >= LHSElems || EndIdx - BegIdx != MaskElems - 1)
    return false;
  for (unsigned I = 0; I != MaskElems; ++I)
    if (static_cast<unsigned>(Mask[I]) != BegIdx + I)
      return false;
  return true;
}

/// These are the ingredients in an alternate form binary operator as described
/// below.
struct BinopElts {
  BinaryOperator::BinaryOps Opcode;
  Value *Op0;
  Value *Op1;
  BinopElts(BinaryOperator::BinaryOps Opc = (BinaryOperator::BinaryOps)0,
            Value *V0 = nullptr, Value *V1 = nullptr) :
      Opcode(Opc), Op0(V0), Op1(V1) {}
  operator bool() const { return Opcode != 0; }
};

/// Binops may be transformed into binops with different opcodes and operands.
/// Reverse the usual canonicalization to enable folds with the non-canonical
/// form of the binop. If a transform is possible, return the elements of the
/// new binop. If not, return invalid elements.
static BinopElts getAlternateBinop(BinaryOperator *BO, const DataLayout &DL) {
  Value *BO0 = BO->getOperand(0), *BO1 = BO->getOperand(1);
  Type *Ty = BO->getType();
  switch (BO->getOpcode()) {
    case Instruction::Shl: {
      // shl X, C --> mul X, (1 << C)
      Constant *C;
      if (match(BO1, m_Constant(C))) {
        Constant *ShlOne = ConstantExpr::getShl(ConstantInt::get(Ty, 1), C);
        return { Instruction::Mul, BO0, ShlOne };
      }
      break;
    }
    case Instruction::Or: {
      // or X, C --> add X, C (when X and C have no common bits set)
      const APInt *C;
      if (match(BO1, m_APInt(C)) && MaskedValueIsZero(BO0, *C, DL))
        return { Instruction::Add, BO0, BO1 };
      break;
    }
    default:
      break;
  }
  return {};
}

static Instruction *foldSelectShuffleWith1Binop(ShuffleVectorInst &Shuf) {
  assert(Shuf.isSelect() && "Must have select-equivalent shuffle");

  // Are we shuffling together some value and that same value after it has been
  // modified by a binop with a constant?
  Value *Op0 = Shuf.getOperand(0), *Op1 = Shuf.getOperand(1);
  Constant *C;
  bool Op0IsBinop;
  if (match(Op0, m_BinOp(m_Specific(Op1), m_Constant(C))))
    Op0IsBinop = true;
  else if (match(Op1, m_BinOp(m_Specific(Op0), m_Constant(C))))
    Op0IsBinop = false;
  else
    return nullptr;

  // The identity constant for a binop leaves a variable operand unchanged. For
  // a vector, this is a splat of something like 0, -1, or 1.
  // If there's no identity constant for this binop, we're done.
  auto *BO = cast<BinaryOperator>(Op0IsBinop ? Op0 : Op1);
  BinaryOperator::BinaryOps BOpcode = BO->getOpcode();
  Constant *IdC = ConstantExpr::getBinOpIdentity(BOpcode, Shuf.getType(), true);
  if (!IdC)
    return nullptr;

  // Shuffle identity constants into the lanes that return the original value.
  // Example: shuf (mul X, {-1,-2,-3,-4}), X, {0,5,6,3} --> mul X, {-1,1,1,-4}
  // Example: shuf X, (add X, {-1,-2,-3,-4}), {0,1,6,7} --> add X, {0,0,-3,-4}
  // The existing binop constant vector remains in the same operand position.
  Constant *Mask = Shuf.getMask();
  Constant *NewC = Op0IsBinop ? ConstantExpr::getShuffleVector(C, IdC, Mask) :
                                ConstantExpr::getShuffleVector(IdC, C, Mask);

  bool MightCreatePoisonOrUB =
      Mask->containsUndefElement() &&
      (Instruction::isIntDivRem(BOpcode) || Instruction::isShift(BOpcode));
  if (MightCreatePoisonOrUB)
    NewC = getSafeVectorConstantForBinop(BOpcode, NewC, true);

  // shuf (bop X, C), X, M --> bop X, C'
  // shuf X, (bop X, C), M --> bop X, C'
  Value *X = Op0IsBinop ? Op1 : Op0;
  Instruction *NewBO = BinaryOperator::Create(BOpcode, X, NewC);
  NewBO->copyIRFlags(BO);

  // An undef shuffle mask element may propagate as an undef constant element in
  // the new binop. That would produce poison where the original code might not.
  // If we already made a safe constant, then there's no danger.
  if (Mask->containsUndefElement() && !MightCreatePoisonOrUB)
    NewBO->dropPoisonGeneratingFlags();
  return NewBO;
}

/// Try to fold shuffles that are the equivalent of a vector select.
static Instruction *foldSelectShuffle(ShuffleVectorInst &Shuf,
                                      InstCombiner::BuilderTy &Builder,
                                      const DataLayout &DL) {
  if (!Shuf.isSelect())
    return nullptr;

  if (Instruction *I = foldSelectShuffleWith1Binop(Shuf))
    return I;

  BinaryOperator *B0, *B1;
  if (!match(Shuf.getOperand(0), m_BinOp(B0)) ||
      !match(Shuf.getOperand(1), m_BinOp(B1)))
    return nullptr;

  Value *X, *Y;
  Constant *C0, *C1;
  bool ConstantsAreOp1;
  if (match(B0, m_BinOp(m_Value(X), m_Constant(C0))) &&
      match(B1, m_BinOp(m_Value(Y), m_Constant(C1))))
    ConstantsAreOp1 = true;
  else if (match(B0, m_BinOp(m_Constant(C0), m_Value(X))) &&
           match(B1, m_BinOp(m_Constant(C1), m_Value(Y))))
    ConstantsAreOp1 = false;
  else
    return nullptr;

  // We need matching binops to fold the lanes together.
  BinaryOperator::BinaryOps Opc0 = B0->getOpcode();
  BinaryOperator::BinaryOps Opc1 = B1->getOpcode();
  bool DropNSW = false;
  if (ConstantsAreOp1 && Opc0 != Opc1) {
    // TODO: We drop "nsw" if shift is converted into multiply because it may
    // not be correct when the shift amount is BitWidth - 1. We could examine
    // each vector element to determine if it is safe to keep that flag.
    if (Opc0 == Instruction::Shl || Opc1 == Instruction::Shl)
      DropNSW = true;
    if (BinopElts AltB0 = getAlternateBinop(B0, DL)) {
      assert(isa<Constant>(AltB0.Op1) && "Expecting constant with alt binop");
      Opc0 = AltB0.Opcode;
      C0 = cast<Constant>(AltB0.Op1);
    } else if (BinopElts AltB1 = getAlternateBinop(B1, DL)) {
      assert(isa<Constant>(AltB1.Op1) && "Expecting constant with alt binop");
      Opc1 = AltB1.Opcode;
      C1 = cast<Constant>(AltB1.Op1);
    }
  }

  if (Opc0 != Opc1)
    return nullptr;

  // The opcodes must be the same. Use a new name to make that clear.
  BinaryOperator::BinaryOps BOpc = Opc0;

  // Select the constant elements needed for the single binop.
  Constant *Mask = Shuf.getMask();
  Constant *NewC = ConstantExpr::getShuffleVector(C0, C1, Mask);

  // We are moving a binop after a shuffle. When a shuffle has an undefined
  // mask element, the result is undefined, but it is not poison or undefined
  // behavior. That is not necessarily true for div/rem/shift.
  bool MightCreatePoisonOrUB =
      Mask->containsUndefElement() &&
      (Instruction::isIntDivRem(BOpc) || Instruction::isShift(BOpc));
  if (MightCreatePoisonOrUB)
    NewC = getSafeVectorConstantForBinop(BOpc, NewC, ConstantsAreOp1);

  Value *V;
  if (X == Y) {
    // Remove a binop and the shuffle by rearranging the constant:
    // shuffle (op V, C0), (op V, C1), M --> op V, C'
    // shuffle (op C0, V), (op C1, V), M --> op C', V
    V = X;
  } else {
    // If there are 2 different variable operands, we must create a new shuffle
    // (select) first, so check uses to ensure that we don't end up with more
    // instructions than we started with.
    if (!B0->hasOneUse() && !B1->hasOneUse())
      return nullptr;

    // If we use the original shuffle mask and op1 is *variable*, we would be
    // putting an undef into operand 1 of div/rem/shift. This is either UB or
    // poison. We do not have to guard against UB when *constants* are op1
    // because safe constants guarantee that we do not overflow sdiv/srem (and
    // there's no danger for other opcodes).
    // TODO: To allow this case, create a new shuffle mask with no undefs.
    if (MightCreatePoisonOrUB && !ConstantsAreOp1)
      return nullptr;

    // Note: In general, we do not create new shuffles in InstCombine because we
    // do not know if a target can lower an arbitrary shuffle optimally. In this
    // case, the shuffle uses the existing mask, so there is no additional risk.

    // Select the variable vectors first, then perform the binop:
    // shuffle (op X, C0), (op Y, C1), M --> op (shuffle X, Y, M), C'
    // shuffle (op C0, X), (op C1, Y), M --> op C', (shuffle X, Y, M)
    V = Builder.CreateShuffleVector(X, Y, Mask);
  }

  Instruction *NewBO = ConstantsAreOp1 ? BinaryOperator::Create(BOpc, V, NewC) :
                                         BinaryOperator::Create(BOpc, NewC, V);

  // Flags are intersected from the 2 source binops. But there are 2 exceptions:
  // 1. If we changed an opcode, poison conditions might have changed.
  // 2. If the shuffle had undef mask elements, the new binop might have undefs
  //    where the original code did not. But if we already made a safe constant,
  //    then there's no danger.
  NewBO->copyIRFlags(B0);
  NewBO->andIRFlags(B1);
  if (DropNSW)
    NewBO->setHasNoSignedWrap(false);
  if (Mask->containsUndefElement() && !MightCreatePoisonOrUB)
    NewBO->dropPoisonGeneratingFlags();
  return NewBO;
}

/// Match a shuffle-select-shuffle pattern where the shuffles are widening and
/// narrowing (concatenating with undef and extracting back to the original
/// length). This allows replacing the wide select with a narrow select.
static Instruction *narrowVectorSelect(ShuffleVectorInst &Shuf,
                                       InstCombiner::BuilderTy &Builder) {
  // This must be a narrowing identity shuffle. It extracts the 1st N elements
  // of the 1st vector operand of a shuffle.
  if (!match(Shuf.getOperand(1), m_Undef()) || !Shuf.isIdentityWithExtract())
    return nullptr;

  // The vector being shuffled must be a vector select that we can eliminate.
  // TODO: The one-use requirement could be eased if X and/or Y are constants.
  Value *Cond, *X, *Y;
  if (!match(Shuf.getOperand(0),
             m_OneUse(m_Select(m_Value(Cond), m_Value(X), m_Value(Y)))))
    return nullptr;

  // We need a narrow condition value. It must be extended with undef elements
  // and have the same number of elements as this shuffle.
  unsigned NarrowNumElts = Shuf.getType()->getVectorNumElements();
  Value *NarrowCond;
  if (!match(Cond, m_OneUse(m_ShuffleVector(m_Value(NarrowCond), m_Undef(),
                                            m_Constant()))) ||
      NarrowCond->getType()->getVectorNumElements() != NarrowNumElts ||
      !cast<ShuffleVectorInst>(Cond)->isIdentityWithPadding())
    return nullptr;

  // shuf (sel (shuf NarrowCond, undef, WideMask), X, Y), undef, NarrowMask) -->
  // sel NarrowCond, (shuf X, undef, NarrowMask), (shuf Y, undef, NarrowMask)
  Value *Undef = UndefValue::get(X->getType());
  Value *NarrowX = Builder.CreateShuffleVector(X, Undef, Shuf.getMask());
  Value *NarrowY = Builder.CreateShuffleVector(Y, Undef, Shuf.getMask());
  return SelectInst::Create(NarrowCond, NarrowX, NarrowY);
}

/// Try to combine 2 shuffles into 1 shuffle by concatenating a shuffle mask.
static Instruction *foldIdentityExtractShuffle(ShuffleVectorInst &Shuf) {
  Value *Op0 = Shuf.getOperand(0), *Op1 = Shuf.getOperand(1);
  if (!Shuf.isIdentityWithExtract() || !isa<UndefValue>(Op1))
    return nullptr;

  Value *X, *Y;
  Constant *Mask;
  if (!match(Op0, m_ShuffleVector(m_Value(X), m_Value(Y), m_Constant(Mask))))
    return nullptr;

  // We are extracting a subvector from a shuffle. Remove excess elements from
  // the 1st shuffle mask to eliminate the extract.
  //
  // This transform is conservatively limited to identity extracts because we do
  // not allow arbitrary shuffle mask creation as a target-independent transform
  // (because we can't guarantee that will lower efficiently).
  //
  // If the extracting shuffle has an undef mask element, it transfers to the
  // new shuffle mask. Otherwise, copy the original mask element. Example:
  //   shuf (shuf X, Y, <C0, C1, C2, undef, C4>), undef, <0, undef, 2, 3> -->
  //   shuf X, Y, <C0, undef, C2, undef>
  unsigned NumElts = Shuf.getType()->getVectorNumElements();
  SmallVector<Constant *, 16> NewMask(NumElts);
  assert(NumElts < Mask->getType()->getVectorNumElements() &&
         "Identity with extract must have less elements than its inputs");

  for (unsigned i = 0; i != NumElts; ++i) {
    Constant *ExtractMaskElt = Shuf.getMask()->getAggregateElement(i);
    Constant *MaskElt = Mask->getAggregateElement(i);
    NewMask[i] = isa<UndefValue>(ExtractMaskElt) ? ExtractMaskElt : MaskElt;
  }
  return new ShuffleVectorInst(X, Y, ConstantVector::get(NewMask));
}

/// Try to replace a shuffle with an insertelement.
static Instruction *foldShuffleWithInsert(ShuffleVectorInst &Shuf) {
  Value *V0 = Shuf.getOperand(0), *V1 = Shuf.getOperand(1);
  SmallVector<int, 16> Mask = Shuf.getShuffleMask();

  // The shuffle must not change vector sizes.
  // TODO: This restriction could be removed if the insert has only one use
  //       (because the transform would require a new length-changing shuffle).
  int NumElts = Mask.size();
  if (NumElts != (int)(V0->getType()->getVectorNumElements()))
    return nullptr;

  // shuffle (insert ?, Scalar, IndexC), V1, Mask --> insert V1, Scalar, IndexC'
  auto isShufflingScalarIntoOp1 = [&](Value *&Scalar, ConstantInt *&IndexC) {
    // We need an insertelement with a constant index.
    if (!match(V0, m_InsertElement(m_Value(), m_Value(Scalar),
                                   m_ConstantInt(IndexC))))
      return false;

    // Test the shuffle mask to see if it splices the inserted scalar into the
    // operand 1 vector of the shuffle.
    int NewInsIndex = -1;
    for (int i = 0; i != NumElts; ++i) {
      // Ignore undef mask elements.
      if (Mask[i] == -1)
        continue;

      // The shuffle takes elements of operand 1 without lane changes.
      if (Mask[i] == NumElts + i)
        continue;

      // The shuffle must choose the inserted scalar exactly once.
      if (NewInsIndex != -1 || Mask[i] != IndexC->getSExtValue())
        return false;

      // The shuffle is placing the inserted scalar into element i.
      NewInsIndex = i;
    }

    assert(NewInsIndex != -1 && "Did not fold shuffle with unused operand?");

    // Index is updated to the potentially translated insertion lane.
    IndexC = ConstantInt::get(IndexC->getType(), NewInsIndex);
    return true;
  };

  // If the shuffle is unnecessary, insert the scalar operand directly into
  // operand 1 of the shuffle. Example:
  // shuffle (insert ?, S, 1), V1, <1, 5, 6, 7> --> insert V1, S, 0
  Value *Scalar;
  ConstantInt *IndexC;
  if (isShufflingScalarIntoOp1(Scalar, IndexC))
    return InsertElementInst::Create(V1, Scalar, IndexC);

  // Try again after commuting shuffle. Example:
  // shuffle V0, (insert ?, S, 0), <0, 1, 2, 4> -->
  // shuffle (insert ?, S, 0), V0, <4, 5, 6, 0> --> insert V0, S, 3
  std::swap(V0, V1);
  ShuffleVectorInst::commuteShuffleMask(Mask, NumElts);
  if (isShufflingScalarIntoOp1(Scalar, IndexC))
    return InsertElementInst::Create(V1, Scalar, IndexC);

  return nullptr;
}

Instruction *InstCombiner::visitShuffleVectorInst(ShuffleVectorInst &SVI) {
  Value *LHS = SVI.getOperand(0);
  Value *RHS = SVI.getOperand(1);
  if (auto *V = SimplifyShuffleVectorInst(
          LHS, RHS, SVI.getMask(), SVI.getType(), SQ.getWithInstruction(&SVI)))
    return replaceInstUsesWith(SVI, V);

  if (Instruction *I = foldSelectShuffle(SVI, Builder, DL))
    return I;

  if (Instruction *I = narrowVectorSelect(SVI, Builder))
    return I;

  unsigned VWidth = SVI.getType()->getVectorNumElements();
  APInt UndefElts(VWidth, 0);
  APInt AllOnesEltMask(APInt::getAllOnesValue(VWidth));
  if (Value *V = SimplifyDemandedVectorElts(&SVI, AllOnesEltMask, UndefElts)) {
    if (V != &SVI)
      return replaceInstUsesWith(SVI, V);
    return &SVI;
  }

  if (Instruction *I = foldIdentityExtractShuffle(SVI))
    return I;

  // This transform has the potential to lose undef knowledge, so it is
  // intentionally placed after SimplifyDemandedVectorElts().
  if (Instruction *I = foldShuffleWithInsert(SVI))
    return I;

  SmallVector<int, 16> Mask = SVI.getShuffleMask();
  Type *Int32Ty = Type::getInt32Ty(SVI.getContext());
  unsigned LHSWidth = LHS->getType()->getVectorNumElements();
  bool MadeChange = false;

  // Canonicalize shuffle(x    ,x,mask) -> shuffle(x, undef,mask')
  // Canonicalize shuffle(undef,x,mask) -> shuffle(x, undef,mask').
  if (LHS == RHS || isa<UndefValue>(LHS)) {
    // Remap any references to RHS to use LHS.
    SmallVector<Constant*, 16> Elts;
    for (unsigned i = 0, e = LHSWidth; i != VWidth; ++i) {
      if (Mask[i] < 0) {
        Elts.push_back(UndefValue::get(Int32Ty));
        continue;
      }

      if ((Mask[i] >= (int)e && isa<UndefValue>(RHS)) ||
          (Mask[i] <  (int)e && isa<UndefValue>(LHS))) {
        Mask[i] = -1;     // Turn into undef.
        Elts.push_back(UndefValue::get(Int32Ty));
      } else {
        Mask[i] = Mask[i] % e;  // Force to LHS.
        Elts.push_back(ConstantInt::get(Int32Ty, Mask[i]));
      }
    }
    SVI.setOperand(0, SVI.getOperand(1));
    SVI.setOperand(1, UndefValue::get(RHS->getType()));
    SVI.setOperand(2, ConstantVector::get(Elts));
    LHS = SVI.getOperand(0);
    RHS = SVI.getOperand(1);
    MadeChange = true;
  }

  if (VWidth == LHSWidth) {
    // Analyze the shuffle, are the LHS or RHS and identity shuffles?
    bool isLHSID, isRHSID;
    recognizeIdentityMask(Mask, isLHSID, isRHSID);

    // Eliminate identity shuffles.
    if (isLHSID) return replaceInstUsesWith(SVI, LHS);
    if (isRHSID) return replaceInstUsesWith(SVI, RHS);
  }

  if (isa<UndefValue>(RHS) && canEvaluateShuffled(LHS, Mask)) {
    Value *V = evaluateInDifferentElementOrder(LHS, Mask);
    return replaceInstUsesWith(SVI, V);
  }

  // SROA generates shuffle+bitcast when the extracted sub-vector is bitcast to
  // a non-vector type. We can instead bitcast the original vector followed by
  // an extract of the desired element:
  //
  //   %sroa = shufflevector <16 x i8> %in, <16 x i8> undef,
  //                         <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  //   %1 = bitcast <4 x i8> %sroa to i32
  // Becomes:
  //   %bc = bitcast <16 x i8> %in to <4 x i32>
  //   %ext = extractelement <4 x i32> %bc, i32 0
  //
  // If the shuffle is extracting a contiguous range of values from the input
  // vector then each use which is a bitcast of the extracted size can be
  // replaced. This will work if the vector types are compatible, and the begin
  // index is aligned to a value in the casted vector type. If the begin index
  // isn't aligned then we can shuffle the original vector (keeping the same
  // vector type) before extracting.
  //
  // This code will bail out if the target type is fundamentally incompatible
  // with vectors of the source type.
  //
  // Example of <16 x i8>, target type i32:
  // Index range [4,8):         v-----------v Will work.
  //                +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
  //     <16 x i8>: |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
  //     <4 x i32>: |           |           |           |           |
  //                +-----------+-----------+-----------+-----------+
  // Index range [6,10):              ^-----------^ Needs an extra shuffle.
  // Target type i40:           ^--------------^ Won't work, bail.
  if (isShuffleExtractingFromLHS(SVI, Mask)) {
    Value *V = LHS;
    unsigned MaskElems = Mask.size();
    VectorType *SrcTy = cast<VectorType>(V->getType());
    unsigned VecBitWidth = SrcTy->getBitWidth();
    unsigned SrcElemBitWidth = DL.getTypeSizeInBits(SrcTy->getElementType());
    assert(SrcElemBitWidth && "vector elements must have a bitwidth");
    unsigned SrcNumElems = SrcTy->getNumElements();
    SmallVector<BitCastInst *, 8> BCs;
    DenseMap<Type *, Value *> NewBCs;
    for (User *U : SVI.users())
      if (BitCastInst *BC = dyn_cast<BitCastInst>(U))
        if (!BC->use_empty())
          // Only visit bitcasts that weren't previously handled.
          BCs.push_back(BC);
    for (BitCastInst *BC : BCs) {
      unsigned BegIdx = Mask.front();
      Type *TgtTy = BC->getDestTy();
      unsigned TgtElemBitWidth = DL.getTypeSizeInBits(TgtTy);
      if (!TgtElemBitWidth)
        continue;
      unsigned TgtNumElems = VecBitWidth / TgtElemBitWidth;
      bool VecBitWidthsEqual = VecBitWidth == TgtNumElems * TgtElemBitWidth;
      bool BegIsAligned = 0 == ((SrcElemBitWidth * BegIdx) % TgtElemBitWidth);
      if (!VecBitWidthsEqual)
        continue;
      if (!VectorType::isValidElementType(TgtTy))
        continue;
      VectorType *CastSrcTy = VectorType::get(TgtTy, TgtNumElems);
      if (!BegIsAligned) {
        // Shuffle the input so [0,NumElements) contains the output, and
        // [NumElems,SrcNumElems) is undef.
        SmallVector<Constant *, 16> ShuffleMask(SrcNumElems,
                                                UndefValue::get(Int32Ty));
        for (unsigned I = 0, E = MaskElems, Idx = BegIdx; I != E; ++Idx, ++I)
          ShuffleMask[I] = ConstantInt::get(Int32Ty, Idx);
        V = Builder.CreateShuffleVector(V, UndefValue::get(V->getType()),
                                        ConstantVector::get(ShuffleMask),
                                        SVI.getName() + ".extract");
        BegIdx = 0;
      }
      unsigned SrcElemsPerTgtElem = TgtElemBitWidth / SrcElemBitWidth;
      assert(SrcElemsPerTgtElem);
      BegIdx /= SrcElemsPerTgtElem;
      bool BCAlreadyExists = NewBCs.find(CastSrcTy) != NewBCs.end();
      auto *NewBC =
          BCAlreadyExists
              ? NewBCs[CastSrcTy]
              : Builder.CreateBitCast(V, CastSrcTy, SVI.getName() + ".bc");
      if (!BCAlreadyExists)
        NewBCs[CastSrcTy] = NewBC;
      auto *Ext = Builder.CreateExtractElement(
          NewBC, ConstantInt::get(Int32Ty, BegIdx), SVI.getName() + ".extract");
      // The shufflevector isn't being replaced: the bitcast that used it
      // is. InstCombine will visit the newly-created instructions.
      replaceInstUsesWith(*BC, Ext);
      MadeChange = true;
    }
  }

  // If the LHS is a shufflevector itself, see if we can combine it with this
  // one without producing an unusual shuffle.
  // Cases that might be simplified:
  // 1.
  // x1=shuffle(v1,v2,mask1)
  //  x=shuffle(x1,undef,mask)
  //        ==>
  //  x=shuffle(v1,undef,newMask)
  // newMask[i] = (mask[i] < x1.size()) ? mask1[mask[i]] : -1
  // 2.
  // x1=shuffle(v1,undef,mask1)
  //  x=shuffle(x1,x2,mask)
  // where v1.size() == mask1.size()
  //        ==>
  //  x=shuffle(v1,x2,newMask)
  // newMask[i] = (mask[i] < x1.size()) ? mask1[mask[i]] : mask[i]
  // 3.
  // x2=shuffle(v2,undef,mask2)
  //  x=shuffle(x1,x2,mask)
  // where v2.size() == mask2.size()
  //        ==>
  //  x=shuffle(x1,v2,newMask)
  // newMask[i] = (mask[i] < x1.size())
  //              ? mask[i] : mask2[mask[i]-x1.size()]+x1.size()
  // 4.
  // x1=shuffle(v1,undef,mask1)
  // x2=shuffle(v2,undef,mask2)
  //  x=shuffle(x1,x2,mask)
  // where v1.size() == v2.size()
  //        ==>
  //  x=shuffle(v1,v2,newMask)
  // newMask[i] = (mask[i] < x1.size())
  //              ? mask1[mask[i]] : mask2[mask[i]-x1.size()]+v1.size()
  //
  // Here we are really conservative:
  // we are absolutely afraid of producing a shuffle mask not in the input
  // program, because the code gen may not be smart enough to turn a merged
  // shuffle into two specific shuffles: it may produce worse code.  As such,
  // we only merge two shuffles if the result is either a splat or one of the
  // input shuffle masks.  In this case, merging the shuffles just removes
  // one instruction, which we know is safe.  This is good for things like
  // turning: (splat(splat)) -> splat, or
  // merge(V[0..n], V[n+1..2n]) -> V[0..2n]
  ShuffleVectorInst* LHSShuffle = dyn_cast<ShuffleVectorInst>(LHS);
  ShuffleVectorInst* RHSShuffle = dyn_cast<ShuffleVectorInst>(RHS);
  if (LHSShuffle)
    if (!isa<UndefValue>(LHSShuffle->getOperand(1)) && !isa<UndefValue>(RHS))
      LHSShuffle = nullptr;
  if (RHSShuffle)
    if (!isa<UndefValue>(RHSShuffle->getOperand(1)))
      RHSShuffle = nullptr;
  if (!LHSShuffle && !RHSShuffle)
    return MadeChange ? &SVI : nullptr;

  Value* LHSOp0 = nullptr;
  Value* LHSOp1 = nullptr;
  Value* RHSOp0 = nullptr;
  unsigned LHSOp0Width = 0;
  unsigned RHSOp0Width = 0;
  if (LHSShuffle) {
    LHSOp0 = LHSShuffle->getOperand(0);
    LHSOp1 = LHSShuffle->getOperand(1);
    LHSOp0Width = LHSOp0->getType()->getVectorNumElements();
  }
  if (RHSShuffle) {
    RHSOp0 = RHSShuffle->getOperand(0);
    RHSOp0Width = RHSOp0->getType()->getVectorNumElements();
  }
  Value* newLHS = LHS;
  Value* newRHS = RHS;
  if (LHSShuffle) {
    // case 1
    if (isa<UndefValue>(RHS)) {
      newLHS = LHSOp0;
      newRHS = LHSOp1;
    }
    // case 2 or 4
    else if (LHSOp0Width == LHSWidth) {
      newLHS = LHSOp0;
    }
  }
  // case 3 or 4
  if (RHSShuffle && RHSOp0Width == LHSWidth) {
    newRHS = RHSOp0;
  }
  // case 4
  if (LHSOp0 == RHSOp0) {
    newLHS = LHSOp0;
    newRHS = nullptr;
  }

  if (newLHS == LHS && newRHS == RHS)
    return MadeChange ? &SVI : nullptr;

  SmallVector<int, 16> LHSMask;
  SmallVector<int, 16> RHSMask;
  if (newLHS != LHS)
    LHSMask = LHSShuffle->getShuffleMask();
  if (RHSShuffle && newRHS != RHS)
    RHSMask = RHSShuffle->getShuffleMask();

  unsigned newLHSWidth = (newLHS != LHS) ? LHSOp0Width : LHSWidth;
  SmallVector<int, 16> newMask;
  bool isSplat = true;
  int SplatElt = -1;
  // Create a new mask for the new ShuffleVectorInst so that the new
  // ShuffleVectorInst is equivalent to the original one.
  for (unsigned i = 0; i < VWidth; ++i) {
    int eltMask;
    if (Mask[i] < 0) {
      // This element is an undef value.
      eltMask = -1;
    } else if (Mask[i] < (int)LHSWidth) {
      // This element is from left hand side vector operand.
      //
      // If LHS is going to be replaced (case 1, 2, or 4), calculate the
      // new mask value for the element.
      if (newLHS != LHS) {
        eltMask = LHSMask[Mask[i]];
        // If the value selected is an undef value, explicitly specify it
        // with a -1 mask value.
        if (eltMask >= (int)LHSOp0Width && isa<UndefValue>(LHSOp1))
          eltMask = -1;
      } else
        eltMask = Mask[i];
    } else {
      // This element is from right hand side vector operand
      //
      // If the value selected is an undef value, explicitly specify it
      // with a -1 mask value. (case 1)
      if (isa<UndefValue>(RHS))
        eltMask = -1;
      // If RHS is going to be replaced (case 3 or 4), calculate the
      // new mask value for the element.
      else if (newRHS != RHS) {
        eltMask = RHSMask[Mask[i]-LHSWidth];
        // If the value selected is an undef value, explicitly specify it
        // with a -1 mask value.
        if (eltMask >= (int)RHSOp0Width) {
          assert(isa<UndefValue>(RHSShuffle->getOperand(1))
                 && "should have been check above");
          eltMask = -1;
        }
      } else
        eltMask = Mask[i]-LHSWidth;

      // If LHS's width is changed, shift the mask value accordingly.
      // If newRHS == nullptr, i.e. LHSOp0 == RHSOp0, we want to remap any
      // references from RHSOp0 to LHSOp0, so we don't need to shift the mask.
      // If newRHS == newLHS, we want to remap any references from newRHS to
      // newLHS so that we can properly identify splats that may occur due to
      // obfuscation across the two vectors.
      if (eltMask >= 0 && newRHS != nullptr && newLHS != newRHS)
        eltMask += newLHSWidth;
    }

    // Check if this could still be a splat.
    if (eltMask >= 0) {
      if (SplatElt >= 0 && SplatElt != eltMask)
        isSplat = false;
      SplatElt = eltMask;
    }

    newMask.push_back(eltMask);
  }

  // If the result mask is equal to one of the original shuffle masks,
  // or is a splat, do the replacement.
  if (isSplat || newMask == LHSMask || newMask == RHSMask || newMask == Mask) {
    SmallVector<Constant*, 16> Elts;
    for (unsigned i = 0, e = newMask.size(); i != e; ++i) {
      if (newMask[i] < 0) {
        Elts.push_back(UndefValue::get(Int32Ty));
      } else {
        Elts.push_back(ConstantInt::get(Int32Ty, newMask[i]));
      }
    }
    if (!newRHS)
      newRHS = UndefValue::get(newLHS->getType());
    return new ShuffleVectorInst(newLHS, newRHS, ConstantVector::get(Elts));
  }

  // If the result mask is an identity, replace uses of this instruction with
  // corresponding argument.
  bool isLHSID, isRHSID;
  recognizeIdentityMask(newMask, isLHSID, isRHSID);
  if (isLHSID && VWidth == LHSOp0Width) return replaceInstUsesWith(SVI, newLHS);
  if (isRHSID && VWidth == RHSOp0Width) return replaceInstUsesWith(SVI, newRHS);

  return MadeChange ? &SVI : nullptr;
}
