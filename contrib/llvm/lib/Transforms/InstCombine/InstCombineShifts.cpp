//===- InstCombineShifts.cpp ----------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the visitShl, visitLShr, and visitAShr functions.
//
//===----------------------------------------------------------------------===//

#include "InstCombineInternal.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/PatternMatch.h"
using namespace llvm;
using namespace PatternMatch;

#define DEBUG_TYPE "instcombine"

Instruction *InstCombiner::commonShiftTransforms(BinaryOperator &I) {
  Value *Op0 = I.getOperand(0), *Op1 = I.getOperand(1);
  assert(Op0->getType() == Op1->getType());

  // See if we can fold away this shift.
  if (SimplifyDemandedInstructionBits(I))
    return &I;

  // Try to fold constant and into select arguments.
  if (isa<Constant>(Op0))
    if (SelectInst *SI = dyn_cast<SelectInst>(Op1))
      if (Instruction *R = FoldOpIntoSelect(I, SI))
        return R;

  if (Constant *CUI = dyn_cast<Constant>(Op1))
    if (Instruction *Res = FoldShiftByConstant(Op0, CUI, I))
      return Res;

  // (C1 shift (A add C2)) -> (C1 shift C2) shift A)
  // iff A and C2 are both positive.
  Value *A;
  Constant *C;
  if (match(Op0, m_Constant()) && match(Op1, m_Add(m_Value(A), m_Constant(C))))
    if (isKnownNonNegative(A, DL, 0, &AC, &I, &DT) &&
        isKnownNonNegative(C, DL, 0, &AC, &I, &DT))
      return BinaryOperator::Create(
          I.getOpcode(), Builder.CreateBinOp(I.getOpcode(), Op0, C), A);

  // X shift (A srem B) -> X shift (A and B-1) iff B is a power of 2.
  // Because shifts by negative values (which could occur if A were negative)
  // are undefined.
  const APInt *B;
  if (Op1->hasOneUse() && match(Op1, m_SRem(m_Value(A), m_Power2(B)))) {
    // FIXME: Should this get moved into SimplifyDemandedBits by saying we don't
    // demand the sign bit (and many others) here??
    Value *Rem = Builder.CreateAnd(A, ConstantInt::get(I.getType(), *B - 1),
                                   Op1->getName());
    I.setOperand(1, Rem);
    return &I;
  }

  return nullptr;
}

/// Return true if we can simplify two logical (either left or right) shifts
/// that have constant shift amounts: OuterShift (InnerShift X, C1), C2.
static bool canEvaluateShiftedShift(unsigned OuterShAmt, bool IsOuterShl,
                                    Instruction *InnerShift, InstCombiner &IC,
                                    Instruction *CxtI) {
  assert(InnerShift->isLogicalShift() && "Unexpected instruction type");

  // We need constant scalar or constant splat shifts.
  const APInt *InnerShiftConst;
  if (!match(InnerShift->getOperand(1), m_APInt(InnerShiftConst)))
    return false;

  // Two logical shifts in the same direction:
  // shl (shl X, C1), C2 -->  shl X, C1 + C2
  // lshr (lshr X, C1), C2 --> lshr X, C1 + C2
  bool IsInnerShl = InnerShift->getOpcode() == Instruction::Shl;
  if (IsInnerShl == IsOuterShl)
    return true;

  // Equal shift amounts in opposite directions become bitwise 'and':
  // lshr (shl X, C), C --> and X, C'
  // shl (lshr X, C), C --> and X, C'
  if (*InnerShiftConst == OuterShAmt)
    return true;

  // If the 2nd shift is bigger than the 1st, we can fold:
  // lshr (shl X, C1), C2 -->  and (shl X, C1 - C2), C3
  // shl (lshr X, C1), C2 --> and (lshr X, C1 - C2), C3
  // but it isn't profitable unless we know the and'd out bits are already zero.
  // Also, check that the inner shift is valid (less than the type width) or
  // we'll crash trying to produce the bit mask for the 'and'.
  unsigned TypeWidth = InnerShift->getType()->getScalarSizeInBits();
  if (InnerShiftConst->ugt(OuterShAmt) && InnerShiftConst->ult(TypeWidth)) {
    unsigned InnerShAmt = InnerShiftConst->getZExtValue();
    unsigned MaskShift =
        IsInnerShl ? TypeWidth - InnerShAmt : InnerShAmt - OuterShAmt;
    APInt Mask = APInt::getLowBitsSet(TypeWidth, OuterShAmt) << MaskShift;
    if (IC.MaskedValueIsZero(InnerShift->getOperand(0), Mask, 0, CxtI))
      return true;
  }

  return false;
}

/// See if we can compute the specified value, but shifted logically to the left
/// or right by some number of bits. This should return true if the expression
/// can be computed for the same cost as the current expression tree. This is
/// used to eliminate extraneous shifting from things like:
///      %C = shl i128 %A, 64
///      %D = shl i128 %B, 96
///      %E = or i128 %C, %D
///      %F = lshr i128 %E, 64
/// where the client will ask if E can be computed shifted right by 64-bits. If
/// this succeeds, getShiftedValue() will be called to produce the value.
static bool canEvaluateShifted(Value *V, unsigned NumBits, bool IsLeftShift,
                               InstCombiner &IC, Instruction *CxtI) {
  // We can always evaluate constants shifted.
  if (isa<Constant>(V))
    return true;

  Instruction *I = dyn_cast<Instruction>(V);
  if (!I) return false;

  // If this is the opposite shift, we can directly reuse the input of the shift
  // if the needed bits are already zero in the input.  This allows us to reuse
  // the value which means that we don't care if the shift has multiple uses.
  //  TODO:  Handle opposite shift by exact value.
  ConstantInt *CI = nullptr;
  if ((IsLeftShift && match(I, m_LShr(m_Value(), m_ConstantInt(CI)))) ||
      (!IsLeftShift && match(I, m_Shl(m_Value(), m_ConstantInt(CI))))) {
    if (CI->getValue() == NumBits) {
      // TODO: Check that the input bits are already zero with MaskedValueIsZero
#if 0
      // If this is a truncate of a logical shr, we can truncate it to a smaller
      // lshr iff we know that the bits we would otherwise be shifting in are
      // already zeros.
      uint32_t OrigBitWidth = OrigTy->getScalarSizeInBits();
      uint32_t BitWidth = Ty->getScalarSizeInBits();
      if (MaskedValueIsZero(I->getOperand(0),
            APInt::getHighBitsSet(OrigBitWidth, OrigBitWidth-BitWidth)) &&
          CI->getLimitedValue(BitWidth) < BitWidth) {
        return CanEvaluateTruncated(I->getOperand(0), Ty);
      }
#endif

    }
  }

  // We can't mutate something that has multiple uses: doing so would
  // require duplicating the instruction in general, which isn't profitable.
  if (!I->hasOneUse()) return false;

  switch (I->getOpcode()) {
  default: return false;
  case Instruction::And:
  case Instruction::Or:
  case Instruction::Xor:
    // Bitwise operators can all arbitrarily be arbitrarily evaluated shifted.
    return canEvaluateShifted(I->getOperand(0), NumBits, IsLeftShift, IC, I) &&
           canEvaluateShifted(I->getOperand(1), NumBits, IsLeftShift, IC, I);

  case Instruction::Shl:
  case Instruction::LShr:
    return canEvaluateShiftedShift(NumBits, IsLeftShift, I, IC, CxtI);

  case Instruction::Select: {
    SelectInst *SI = cast<SelectInst>(I);
    Value *TrueVal = SI->getTrueValue();
    Value *FalseVal = SI->getFalseValue();
    return canEvaluateShifted(TrueVal, NumBits, IsLeftShift, IC, SI) &&
           canEvaluateShifted(FalseVal, NumBits, IsLeftShift, IC, SI);
  }
  case Instruction::PHI: {
    // We can change a phi if we can change all operands.  Note that we never
    // get into trouble with cyclic PHIs here because we only consider
    // instructions with a single use.
    PHINode *PN = cast<PHINode>(I);
    for (Value *IncValue : PN->incoming_values())
      if (!canEvaluateShifted(IncValue, NumBits, IsLeftShift, IC, PN))
        return false;
    return true;
  }
  }
}

/// Fold OuterShift (InnerShift X, C1), C2.
/// See canEvaluateShiftedShift() for the constraints on these instructions.
static Value *foldShiftedShift(BinaryOperator *InnerShift, unsigned OuterShAmt,
                               bool IsOuterShl,
                               InstCombiner::BuilderTy &Builder) {
  bool IsInnerShl = InnerShift->getOpcode() == Instruction::Shl;
  Type *ShType = InnerShift->getType();
  unsigned TypeWidth = ShType->getScalarSizeInBits();

  // We only accept shifts-by-a-constant in canEvaluateShifted().
  const APInt *C1;
  match(InnerShift->getOperand(1), m_APInt(C1));
  unsigned InnerShAmt = C1->getZExtValue();

  // Change the shift amount and clear the appropriate IR flags.
  auto NewInnerShift = [&](unsigned ShAmt) {
    InnerShift->setOperand(1, ConstantInt::get(ShType, ShAmt));
    if (IsInnerShl) {
      InnerShift->setHasNoUnsignedWrap(false);
      InnerShift->setHasNoSignedWrap(false);
    } else {
      InnerShift->setIsExact(false);
    }
    return InnerShift;
  };

  // Two logical shifts in the same direction:
  // shl (shl X, C1), C2 -->  shl X, C1 + C2
  // lshr (lshr X, C1), C2 --> lshr X, C1 + C2
  if (IsInnerShl == IsOuterShl) {
    // If this is an oversized composite shift, then unsigned shifts get 0.
    if (InnerShAmt + OuterShAmt >= TypeWidth)
      return Constant::getNullValue(ShType);

    return NewInnerShift(InnerShAmt + OuterShAmt);
  }

  // Equal shift amounts in opposite directions become bitwise 'and':
  // lshr (shl X, C), C --> and X, C'
  // shl (lshr X, C), C --> and X, C'
  if (InnerShAmt == OuterShAmt) {
    APInt Mask = IsInnerShl
                     ? APInt::getLowBitsSet(TypeWidth, TypeWidth - OuterShAmt)
                     : APInt::getHighBitsSet(TypeWidth, TypeWidth - OuterShAmt);
    Value *And = Builder.CreateAnd(InnerShift->getOperand(0),
                                   ConstantInt::get(ShType, Mask));
    if (auto *AndI = dyn_cast<Instruction>(And)) {
      AndI->moveBefore(InnerShift);
      AndI->takeName(InnerShift);
    }
    return And;
  }

  assert(InnerShAmt > OuterShAmt &&
         "Unexpected opposite direction logical shift pair");

  // In general, we would need an 'and' for this transform, but
  // canEvaluateShiftedShift() guarantees that the masked-off bits are not used.
  // lshr (shl X, C1), C2 -->  shl X, C1 - C2
  // shl (lshr X, C1), C2 --> lshr X, C1 - C2
  return NewInnerShift(InnerShAmt - OuterShAmt);
}

/// When canEvaluateShifted() returns true for an expression, this function
/// inserts the new computation that produces the shifted value.
static Value *getShiftedValue(Value *V, unsigned NumBits, bool isLeftShift,
                              InstCombiner &IC, const DataLayout &DL) {
  // We can always evaluate constants shifted.
  if (Constant *C = dyn_cast<Constant>(V)) {
    if (isLeftShift)
      V = IC.Builder.CreateShl(C, NumBits);
    else
      V = IC.Builder.CreateLShr(C, NumBits);
    // If we got a constantexpr back, try to simplify it with TD info.
    if (auto *C = dyn_cast<Constant>(V))
      if (auto *FoldedC =
              ConstantFoldConstant(C, DL, &IC.getTargetLibraryInfo()))
        V = FoldedC;
    return V;
  }

  Instruction *I = cast<Instruction>(V);
  IC.Worklist.Add(I);

  switch (I->getOpcode()) {
  default: llvm_unreachable("Inconsistency with CanEvaluateShifted");
  case Instruction::And:
  case Instruction::Or:
  case Instruction::Xor:
    // Bitwise operators can all arbitrarily be arbitrarily evaluated shifted.
    I->setOperand(
        0, getShiftedValue(I->getOperand(0), NumBits, isLeftShift, IC, DL));
    I->setOperand(
        1, getShiftedValue(I->getOperand(1), NumBits, isLeftShift, IC, DL));
    return I;

  case Instruction::Shl:
  case Instruction::LShr:
    return foldShiftedShift(cast<BinaryOperator>(I), NumBits, isLeftShift,
                            IC.Builder);

  case Instruction::Select:
    I->setOperand(
        1, getShiftedValue(I->getOperand(1), NumBits, isLeftShift, IC, DL));
    I->setOperand(
        2, getShiftedValue(I->getOperand(2), NumBits, isLeftShift, IC, DL));
    return I;
  case Instruction::PHI: {
    // We can change a phi if we can change all operands.  Note that we never
    // get into trouble with cyclic PHIs here because we only consider
    // instructions with a single use.
    PHINode *PN = cast<PHINode>(I);
    for (unsigned i = 0, e = PN->getNumIncomingValues(); i != e; ++i)
      PN->setIncomingValue(i, getShiftedValue(PN->getIncomingValue(i), NumBits,
                                              isLeftShift, IC, DL));
    return PN;
  }
  }
}

// If this is a bitwise operator or add with a constant RHS we might be able
// to pull it through a shift.
static bool canShiftBinOpWithConstantRHS(BinaryOperator &Shift,
                                         BinaryOperator *BO,
                                         const APInt &C) {
  bool IsValid = true;     // Valid only for And, Or Xor,
  bool HighBitSet = false; // Transform ifhigh bit of constant set?

  switch (BO->getOpcode()) {
  default: IsValid = false; break;   // Do not perform transform!
  case Instruction::Add:
    IsValid = Shift.getOpcode() == Instruction::Shl;
    break;
  case Instruction::Or:
  case Instruction::Xor:
    HighBitSet = false;
    break;
  case Instruction::And:
    HighBitSet = true;
    break;
  }

  // If this is a signed shift right, and the high bit is modified
  // by the logical operation, do not perform the transformation.
  // The HighBitSet boolean indicates the value of the high bit of
  // the constant which would cause it to be modified for this
  // operation.
  //
  if (IsValid && Shift.getOpcode() == Instruction::AShr)
    IsValid = C.isNegative() == HighBitSet;

  return IsValid;
}

Instruction *InstCombiner::FoldShiftByConstant(Value *Op0, Constant *Op1,
                                               BinaryOperator &I) {
  bool isLeftShift = I.getOpcode() == Instruction::Shl;

  const APInt *Op1C;
  if (!match(Op1, m_APInt(Op1C)))
    return nullptr;

  // See if we can propagate this shift into the input, this covers the trivial
  // cast of lshr(shl(x,c1),c2) as well as other more complex cases.
  if (I.getOpcode() != Instruction::AShr &&
      canEvaluateShifted(Op0, Op1C->getZExtValue(), isLeftShift, *this, &I)) {
    LLVM_DEBUG(
        dbgs() << "ICE: GetShiftedValue propagating shift through expression"
                  " to eliminate shift:\n  IN: "
               << *Op0 << "\n  SH: " << I << "\n");

    return replaceInstUsesWith(
        I, getShiftedValue(Op0, Op1C->getZExtValue(), isLeftShift, *this, DL));
  }

  // See if we can simplify any instructions used by the instruction whose sole
  // purpose is to compute bits we don't care about.
  unsigned TypeBits = Op0->getType()->getScalarSizeInBits();

  assert(!Op1C->uge(TypeBits) &&
         "Shift over the type width should have been removed already");

  if (Instruction *FoldedShift = foldBinOpIntoSelectOrPhi(I))
    return FoldedShift;

  // Fold shift2(trunc(shift1(x,c1)), c2) -> trunc(shift2(shift1(x,c1),c2))
  if (TruncInst *TI = dyn_cast<TruncInst>(Op0)) {
    Instruction *TrOp = dyn_cast<Instruction>(TI->getOperand(0));
    // If 'shift2' is an ashr, we would have to get the sign bit into a funny
    // place.  Don't try to do this transformation in this case.  Also, we
    // require that the input operand is a shift-by-constant so that we have
    // confidence that the shifts will get folded together.  We could do this
    // xform in more cases, but it is unlikely to be profitable.
    if (TrOp && I.isLogicalShift() && TrOp->isShift() &&
        isa<ConstantInt>(TrOp->getOperand(1))) {
      // Okay, we'll do this xform.  Make the shift of shift.
      Constant *ShAmt =
          ConstantExpr::getZExt(cast<Constant>(Op1), TrOp->getType());
      // (shift2 (shift1 & 0x00FF), c2)
      Value *NSh = Builder.CreateBinOp(I.getOpcode(), TrOp, ShAmt, I.getName());

      // For logical shifts, the truncation has the effect of making the high
      // part of the register be zeros.  Emulate this by inserting an AND to
      // clear the top bits as needed.  This 'and' will usually be zapped by
      // other xforms later if dead.
      unsigned SrcSize = TrOp->getType()->getScalarSizeInBits();
      unsigned DstSize = TI->getType()->getScalarSizeInBits();
      APInt MaskV(APInt::getLowBitsSet(SrcSize, DstSize));

      // The mask we constructed says what the trunc would do if occurring
      // between the shifts.  We want to know the effect *after* the second
      // shift.  We know that it is a logical shift by a constant, so adjust the
      // mask as appropriate.
      if (I.getOpcode() == Instruction::Shl)
        MaskV <<= Op1C->getZExtValue();
      else {
        assert(I.getOpcode() == Instruction::LShr && "Unknown logical shift");
        MaskV.lshrInPlace(Op1C->getZExtValue());
      }

      // shift1 & 0x00FF
      Value *And = Builder.CreateAnd(NSh,
                                     ConstantInt::get(I.getContext(), MaskV),
                                     TI->getName());

      // Return the value truncated to the interesting size.
      return new TruncInst(And, I.getType());
    }
  }

  if (Op0->hasOneUse()) {
    if (BinaryOperator *Op0BO = dyn_cast<BinaryOperator>(Op0)) {
      // Turn ((X >> C) + Y) << C  ->  (X + (Y << C)) & (~0 << C)
      Value *V1, *V2;
      ConstantInt *CC;
      switch (Op0BO->getOpcode()) {
      default: break;
      case Instruction::Add:
      case Instruction::And:
      case Instruction::Or:
      case Instruction::Xor: {
        // These operators commute.
        // Turn (Y + (X >> C)) << C  ->  (X + (Y << C)) & (~0 << C)
        if (isLeftShift && Op0BO->getOperand(1)->hasOneUse() &&
            match(Op0BO->getOperand(1), m_Shr(m_Value(V1),
                  m_Specific(Op1)))) {
          Value *YS =         // (Y << C)
            Builder.CreateShl(Op0BO->getOperand(0), Op1, Op0BO->getName());
          // (X + (Y << C))
          Value *X = Builder.CreateBinOp(Op0BO->getOpcode(), YS, V1,
                                         Op0BO->getOperand(1)->getName());
          unsigned Op1Val = Op1C->getLimitedValue(TypeBits);

          APInt Bits = APInt::getHighBitsSet(TypeBits, TypeBits - Op1Val);
          Constant *Mask = ConstantInt::get(I.getContext(), Bits);
          if (VectorType *VT = dyn_cast<VectorType>(X->getType()))
            Mask = ConstantVector::getSplat(VT->getNumElements(), Mask);
          return BinaryOperator::CreateAnd(X, Mask);
        }

        // Turn (Y + ((X >> C) & CC)) << C  ->  ((X & (CC << C)) + (Y << C))
        Value *Op0BOOp1 = Op0BO->getOperand(1);
        if (isLeftShift && Op0BOOp1->hasOneUse() &&
            match(Op0BOOp1,
                  m_And(m_OneUse(m_Shr(m_Value(V1), m_Specific(Op1))),
                        m_ConstantInt(CC)))) {
          Value *YS =   // (Y << C)
            Builder.CreateShl(Op0BO->getOperand(0), Op1, Op0BO->getName());
          // X & (CC << C)
          Value *XM = Builder.CreateAnd(V1, ConstantExpr::getShl(CC, Op1),
                                        V1->getName()+".mask");
          return BinaryOperator::Create(Op0BO->getOpcode(), YS, XM);
        }
        LLVM_FALLTHROUGH;
      }

      case Instruction::Sub: {
        // Turn ((X >> C) + Y) << C  ->  (X + (Y << C)) & (~0 << C)
        if (isLeftShift && Op0BO->getOperand(0)->hasOneUse() &&
            match(Op0BO->getOperand(0), m_Shr(m_Value(V1),
                  m_Specific(Op1)))) {
          Value *YS =  // (Y << C)
            Builder.CreateShl(Op0BO->getOperand(1), Op1, Op0BO->getName());
          // (X + (Y << C))
          Value *X = Builder.CreateBinOp(Op0BO->getOpcode(), V1, YS,
                                         Op0BO->getOperand(0)->getName());
          unsigned Op1Val = Op1C->getLimitedValue(TypeBits);

          APInt Bits = APInt::getHighBitsSet(TypeBits, TypeBits - Op1Val);
          Constant *Mask = ConstantInt::get(I.getContext(), Bits);
          if (VectorType *VT = dyn_cast<VectorType>(X->getType()))
            Mask = ConstantVector::getSplat(VT->getNumElements(), Mask);
          return BinaryOperator::CreateAnd(X, Mask);
        }

        // Turn (((X >> C)&CC) + Y) << C  ->  (X + (Y << C)) & (CC << C)
        if (isLeftShift && Op0BO->getOperand(0)->hasOneUse() &&
            match(Op0BO->getOperand(0),
                  m_And(m_OneUse(m_Shr(m_Value(V1), m_Value(V2))),
                        m_ConstantInt(CC))) && V2 == Op1) {
          Value *YS = // (Y << C)
            Builder.CreateShl(Op0BO->getOperand(1), Op1, Op0BO->getName());
          // X & (CC << C)
          Value *XM = Builder.CreateAnd(V1, ConstantExpr::getShl(CC, Op1),
                                        V1->getName()+".mask");

          return BinaryOperator::Create(Op0BO->getOpcode(), XM, YS);
        }

        break;
      }
      }


      // If the operand is a bitwise operator with a constant RHS, and the
      // shift is the only use, we can pull it out of the shift.
      const APInt *Op0C;
      if (match(Op0BO->getOperand(1), m_APInt(Op0C))) {
        if (canShiftBinOpWithConstantRHS(I, Op0BO, *Op0C)) {
          Constant *NewRHS = ConstantExpr::get(I.getOpcode(),
                                     cast<Constant>(Op0BO->getOperand(1)), Op1);

          Value *NewShift =
            Builder.CreateBinOp(I.getOpcode(), Op0BO->getOperand(0), Op1);
          NewShift->takeName(Op0BO);

          return BinaryOperator::Create(Op0BO->getOpcode(), NewShift,
                                        NewRHS);
        }
      }

      // If the operand is a subtract with a constant LHS, and the shift
      // is the only use, we can pull it out of the shift.
      // This folds (shl (sub C1, X), C2) -> (sub (C1 << C2), (shl X, C2))
      if (isLeftShift && Op0BO->getOpcode() == Instruction::Sub &&
          match(Op0BO->getOperand(0), m_APInt(Op0C))) {
        Constant *NewRHS = ConstantExpr::get(I.getOpcode(),
                                   cast<Constant>(Op0BO->getOperand(0)), Op1);

        Value *NewShift = Builder.CreateShl(Op0BO->getOperand(1), Op1);
        NewShift->takeName(Op0BO);

        return BinaryOperator::CreateSub(NewRHS, NewShift);
      }
    }

    // If we have a select that conditionally executes some binary operator,
    // see if we can pull it the select and operator through the shift.
    //
    // For example, turning:
    //   shl (select C, (add X, C1), X), C2
    // Into:
    //   Y = shl X, C2
    //   select C, (add Y, C1 << C2), Y
    Value *Cond;
    BinaryOperator *TBO;
    Value *FalseVal;
    if (match(Op0, m_Select(m_Value(Cond), m_OneUse(m_BinOp(TBO)),
                            m_Value(FalseVal)))) {
      const APInt *C;
      if (!isa<Constant>(FalseVal) && TBO->getOperand(0) == FalseVal &&
          match(TBO->getOperand(1), m_APInt(C)) &&
          canShiftBinOpWithConstantRHS(I, TBO, *C)) {
        Constant *NewRHS = ConstantExpr::get(I.getOpcode(),
                                       cast<Constant>(TBO->getOperand(1)), Op1);

        Value *NewShift =
          Builder.CreateBinOp(I.getOpcode(), FalseVal, Op1);
        Value *NewOp = Builder.CreateBinOp(TBO->getOpcode(), NewShift,
                                           NewRHS);
        return SelectInst::Create(Cond, NewOp, NewShift);
      }
    }

    BinaryOperator *FBO;
    Value *TrueVal;
    if (match(Op0, m_Select(m_Value(Cond), m_Value(TrueVal),
                            m_OneUse(m_BinOp(FBO))))) {
      const APInt *C;
      if (!isa<Constant>(TrueVal) && FBO->getOperand(0) == TrueVal &&
          match(FBO->getOperand(1), m_APInt(C)) &&
          canShiftBinOpWithConstantRHS(I, FBO, *C)) {
        Constant *NewRHS = ConstantExpr::get(I.getOpcode(),
                                       cast<Constant>(FBO->getOperand(1)), Op1);

        Value *NewShift =
          Builder.CreateBinOp(I.getOpcode(), TrueVal, Op1);
        Value *NewOp = Builder.CreateBinOp(FBO->getOpcode(), NewShift,
                                           NewRHS);
        return SelectInst::Create(Cond, NewShift, NewOp);
      }
    }
  }

  return nullptr;
}

Instruction *InstCombiner::visitShl(BinaryOperator &I) {
  if (Value *V = SimplifyShlInst(I.getOperand(0), I.getOperand(1),
                                 I.hasNoSignedWrap(), I.hasNoUnsignedWrap(),
                                 SQ.getWithInstruction(&I)))
    return replaceInstUsesWith(I, V);

  if (Instruction *X = foldVectorBinop(I))
    return X;

  if (Instruction *V = commonShiftTransforms(I))
    return V;

  Value *Op0 = I.getOperand(0), *Op1 = I.getOperand(1);
  Type *Ty = I.getType();
  const APInt *ShAmtAPInt;
  if (match(Op1, m_APInt(ShAmtAPInt))) {
    unsigned ShAmt = ShAmtAPInt->getZExtValue();
    unsigned BitWidth = Ty->getScalarSizeInBits();

    // shl (zext X), ShAmt --> zext (shl X, ShAmt)
    // This is only valid if X would have zeros shifted out.
    Value *X;
    if (match(Op0, m_ZExt(m_Value(X)))) {
      unsigned SrcWidth = X->getType()->getScalarSizeInBits();
      if (ShAmt < SrcWidth &&
          MaskedValueIsZero(X, APInt::getHighBitsSet(SrcWidth, ShAmt), 0, &I))
        return new ZExtInst(Builder.CreateShl(X, ShAmt), Ty);
    }

    // (X >> C) << C --> X & (-1 << C)
    if (match(Op0, m_Shr(m_Value(X), m_Specific(Op1)))) {
      APInt Mask(APInt::getHighBitsSet(BitWidth, BitWidth - ShAmt));
      return BinaryOperator::CreateAnd(X, ConstantInt::get(Ty, Mask));
    }

    // FIXME: we do not yet transform non-exact shr's. The backend (DAGCombine)
    // needs a few fixes for the rotate pattern recognition first.
    const APInt *ShOp1;
    if (match(Op0, m_Exact(m_Shr(m_Value(X), m_APInt(ShOp1))))) {
      unsigned ShrAmt = ShOp1->getZExtValue();
      if (ShrAmt < ShAmt) {
        // If C1 < C2: (X >>?,exact C1) << C2 --> X << (C2 - C1)
        Constant *ShiftDiff = ConstantInt::get(Ty, ShAmt - ShrAmt);
        auto *NewShl = BinaryOperator::CreateShl(X, ShiftDiff);
        NewShl->setHasNoUnsignedWrap(I.hasNoUnsignedWrap());
        NewShl->setHasNoSignedWrap(I.hasNoSignedWrap());
        return NewShl;
      }
      if (ShrAmt > ShAmt) {
        // If C1 > C2: (X >>?exact C1) << C2 --> X >>?exact (C1 - C2)
        Constant *ShiftDiff = ConstantInt::get(Ty, ShrAmt - ShAmt);
        auto *NewShr = BinaryOperator::Create(
            cast<BinaryOperator>(Op0)->getOpcode(), X, ShiftDiff);
        NewShr->setIsExact(true);
        return NewShr;
      }
    }

    if (match(Op0, m_Shl(m_Value(X), m_APInt(ShOp1)))) {
      unsigned AmtSum = ShAmt + ShOp1->getZExtValue();
      // Oversized shifts are simplified to zero in InstSimplify.
      if (AmtSum < BitWidth)
        // (X << C1) << C2 --> X << (C1 + C2)
        return BinaryOperator::CreateShl(X, ConstantInt::get(Ty, AmtSum));
    }

    // If the shifted-out value is known-zero, then this is a NUW shift.
    if (!I.hasNoUnsignedWrap() &&
        MaskedValueIsZero(Op0, APInt::getHighBitsSet(BitWidth, ShAmt), 0, &I)) {
      I.setHasNoUnsignedWrap();
      return &I;
    }

    // If the shifted-out value is all signbits, then this is a NSW shift.
    if (!I.hasNoSignedWrap() && ComputeNumSignBits(Op0, 0, &I) > ShAmt) {
      I.setHasNoSignedWrap();
      return &I;
    }
  }

  // Transform  (x >> y) << y  to  x & (-1 << y)
  // Valid for any type of right-shift.
  Value *X;
  if (match(Op0, m_OneUse(m_Shr(m_Value(X), m_Specific(Op1))))) {
    Constant *AllOnes = ConstantInt::getAllOnesValue(Ty);
    Value *Mask = Builder.CreateShl(AllOnes, Op1);
    return BinaryOperator::CreateAnd(Mask, X);
  }

  Constant *C1;
  if (match(Op1, m_Constant(C1))) {
    Constant *C2;
    Value *X;
    // (C2 << X) << C1 --> (C2 << C1) << X
    if (match(Op0, m_OneUse(m_Shl(m_Constant(C2), m_Value(X)))))
      return BinaryOperator::CreateShl(ConstantExpr::getShl(C2, C1), X);

    // (X * C2) << C1 --> X * (C2 << C1)
    if (match(Op0, m_Mul(m_Value(X), m_Constant(C2))))
      return BinaryOperator::CreateMul(X, ConstantExpr::getShl(C2, C1));
  }

  return nullptr;
}

Instruction *InstCombiner::visitLShr(BinaryOperator &I) {
  if (Value *V = SimplifyLShrInst(I.getOperand(0), I.getOperand(1), I.isExact(),
                                  SQ.getWithInstruction(&I)))
    return replaceInstUsesWith(I, V);

  if (Instruction *X = foldVectorBinop(I))
    return X;

  if (Instruction *R = commonShiftTransforms(I))
    return R;

  Value *Op0 = I.getOperand(0), *Op1 = I.getOperand(1);
  Type *Ty = I.getType();
  const APInt *ShAmtAPInt;
  if (match(Op1, m_APInt(ShAmtAPInt))) {
    unsigned ShAmt = ShAmtAPInt->getZExtValue();
    unsigned BitWidth = Ty->getScalarSizeInBits();
    auto *II = dyn_cast<IntrinsicInst>(Op0);
    if (II && isPowerOf2_32(BitWidth) && Log2_32(BitWidth) == ShAmt &&
        (II->getIntrinsicID() == Intrinsic::ctlz ||
         II->getIntrinsicID() == Intrinsic::cttz ||
         II->getIntrinsicID() == Intrinsic::ctpop)) {
      // ctlz.i32(x)>>5  --> zext(x == 0)
      // cttz.i32(x)>>5  --> zext(x == 0)
      // ctpop.i32(x)>>5 --> zext(x == -1)
      bool IsPop = II->getIntrinsicID() == Intrinsic::ctpop;
      Constant *RHS = ConstantInt::getSigned(Ty, IsPop ? -1 : 0);
      Value *Cmp = Builder.CreateICmpEQ(II->getArgOperand(0), RHS);
      return new ZExtInst(Cmp, Ty);
    }

    Value *X;
    const APInt *ShOp1;
    if (match(Op0, m_Shl(m_Value(X), m_APInt(ShOp1))) && ShOp1->ult(BitWidth)) {
      if (ShOp1->ult(ShAmt)) {
        unsigned ShlAmt = ShOp1->getZExtValue();
        Constant *ShiftDiff = ConstantInt::get(Ty, ShAmt - ShlAmt);
        if (cast<BinaryOperator>(Op0)->hasNoUnsignedWrap()) {
          // (X <<nuw C1) >>u C2 --> X >>u (C2 - C1)
          auto *NewLShr = BinaryOperator::CreateLShr(X, ShiftDiff);
          NewLShr->setIsExact(I.isExact());
          return NewLShr;
        }
        // (X << C1) >>u C2  --> (X >>u (C2 - C1)) & (-1 >> C2)
        Value *NewLShr = Builder.CreateLShr(X, ShiftDiff, "", I.isExact());
        APInt Mask(APInt::getLowBitsSet(BitWidth, BitWidth - ShAmt));
        return BinaryOperator::CreateAnd(NewLShr, ConstantInt::get(Ty, Mask));
      }
      if (ShOp1->ugt(ShAmt)) {
        unsigned ShlAmt = ShOp1->getZExtValue();
        Constant *ShiftDiff = ConstantInt::get(Ty, ShlAmt - ShAmt);
        if (cast<BinaryOperator>(Op0)->hasNoUnsignedWrap()) {
          // (X <<nuw C1) >>u C2 --> X <<nuw (C1 - C2)
          auto *NewShl = BinaryOperator::CreateShl(X, ShiftDiff);
          NewShl->setHasNoUnsignedWrap(true);
          return NewShl;
        }
        // (X << C1) >>u C2  --> X << (C1 - C2) & (-1 >> C2)
        Value *NewShl = Builder.CreateShl(X, ShiftDiff);
        APInt Mask(APInt::getLowBitsSet(BitWidth, BitWidth - ShAmt));
        return BinaryOperator::CreateAnd(NewShl, ConstantInt::get(Ty, Mask));
      }
      assert(*ShOp1 == ShAmt);
      // (X << C) >>u C --> X & (-1 >>u C)
      APInt Mask(APInt::getLowBitsSet(BitWidth, BitWidth - ShAmt));
      return BinaryOperator::CreateAnd(X, ConstantInt::get(Ty, Mask));
    }

    if (match(Op0, m_OneUse(m_ZExt(m_Value(X)))) &&
        (!Ty->isIntegerTy() || shouldChangeType(Ty, X->getType()))) {
      assert(ShAmt < X->getType()->getScalarSizeInBits() &&
             "Big shift not simplified to zero?");
      // lshr (zext iM X to iN), C --> zext (lshr X, C) to iN
      Value *NewLShr = Builder.CreateLShr(X, ShAmt);
      return new ZExtInst(NewLShr, Ty);
    }

    if (match(Op0, m_SExt(m_Value(X))) &&
        (!Ty->isIntegerTy() || shouldChangeType(Ty, X->getType()))) {
      // Are we moving the sign bit to the low bit and widening with high zeros?
      unsigned SrcTyBitWidth = X->getType()->getScalarSizeInBits();
      if (ShAmt == BitWidth - 1) {
        // lshr (sext i1 X to iN), N-1 --> zext X to iN
        if (SrcTyBitWidth == 1)
          return new ZExtInst(X, Ty);

        // lshr (sext iM X to iN), N-1 --> zext (lshr X, M-1) to iN
        if (Op0->hasOneUse()) {
          Value *NewLShr = Builder.CreateLShr(X, SrcTyBitWidth - 1);
          return new ZExtInst(NewLShr, Ty);
        }
      }

      // lshr (sext iM X to iN), N-M --> zext (ashr X, min(N-M, M-1)) to iN
      if (ShAmt == BitWidth - SrcTyBitWidth && Op0->hasOneUse()) {
        // The new shift amount can't be more than the narrow source type.
        unsigned NewShAmt = std::min(ShAmt, SrcTyBitWidth - 1);
        Value *AShr = Builder.CreateAShr(X, NewShAmt);
        return new ZExtInst(AShr, Ty);
      }
    }

    if (match(Op0, m_LShr(m_Value(X), m_APInt(ShOp1)))) {
      unsigned AmtSum = ShAmt + ShOp1->getZExtValue();
      // Oversized shifts are simplified to zero in InstSimplify.
      if (AmtSum < BitWidth)
        // (X >>u C1) >>u C2 --> X >>u (C1 + C2)
        return BinaryOperator::CreateLShr(X, ConstantInt::get(Ty, AmtSum));
    }

    // If the shifted-out value is known-zero, then this is an exact shift.
    if (!I.isExact() &&
        MaskedValueIsZero(Op0, APInt::getLowBitsSet(BitWidth, ShAmt), 0, &I)) {
      I.setIsExact();
      return &I;
    }
  }

  // Transform  (x << y) >> y  to  x & (-1 >> y)
  Value *X;
  if (match(Op0, m_OneUse(m_Shl(m_Value(X), m_Specific(Op1))))) {
    Constant *AllOnes = ConstantInt::getAllOnesValue(Ty);
    Value *Mask = Builder.CreateLShr(AllOnes, Op1);
    return BinaryOperator::CreateAnd(Mask, X);
  }

  return nullptr;
}

Instruction *InstCombiner::visitAShr(BinaryOperator &I) {
  if (Value *V = SimplifyAShrInst(I.getOperand(0), I.getOperand(1), I.isExact(),
                                  SQ.getWithInstruction(&I)))
    return replaceInstUsesWith(I, V);

  if (Instruction *X = foldVectorBinop(I))
    return X;

  if (Instruction *R = commonShiftTransforms(I))
    return R;

  Value *Op0 = I.getOperand(0), *Op1 = I.getOperand(1);
  Type *Ty = I.getType();
  unsigned BitWidth = Ty->getScalarSizeInBits();
  const APInt *ShAmtAPInt;
  if (match(Op1, m_APInt(ShAmtAPInt)) && ShAmtAPInt->ult(BitWidth)) {
    unsigned ShAmt = ShAmtAPInt->getZExtValue();

    // If the shift amount equals the difference in width of the destination
    // and source scalar types:
    // ashr (shl (zext X), C), C --> sext X
    Value *X;
    if (match(Op0, m_Shl(m_ZExt(m_Value(X)), m_Specific(Op1))) &&
        ShAmt == BitWidth - X->getType()->getScalarSizeInBits())
      return new SExtInst(X, Ty);

    // We can't handle (X << C1) >>s C2. It shifts arbitrary bits in. However,
    // we can handle (X <<nsw C1) >>s C2 since it only shifts in sign bits.
    const APInt *ShOp1;
    if (match(Op0, m_NSWShl(m_Value(X), m_APInt(ShOp1))) &&
        ShOp1->ult(BitWidth)) {
      unsigned ShlAmt = ShOp1->getZExtValue();
      if (ShlAmt < ShAmt) {
        // (X <<nsw C1) >>s C2 --> X >>s (C2 - C1)
        Constant *ShiftDiff = ConstantInt::get(Ty, ShAmt - ShlAmt);
        auto *NewAShr = BinaryOperator::CreateAShr(X, ShiftDiff);
        NewAShr->setIsExact(I.isExact());
        return NewAShr;
      }
      if (ShlAmt > ShAmt) {
        // (X <<nsw C1) >>s C2 --> X <<nsw (C1 - C2)
        Constant *ShiftDiff = ConstantInt::get(Ty, ShlAmt - ShAmt);
        auto *NewShl = BinaryOperator::Create(Instruction::Shl, X, ShiftDiff);
        NewShl->setHasNoSignedWrap(true);
        return NewShl;
      }
    }

    if (match(Op0, m_AShr(m_Value(X), m_APInt(ShOp1))) &&
        ShOp1->ult(BitWidth)) {
      unsigned AmtSum = ShAmt + ShOp1->getZExtValue();
      // Oversized arithmetic shifts replicate the sign bit.
      AmtSum = std::min(AmtSum, BitWidth - 1);
      // (X >>s C1) >>s C2 --> X >>s (C1 + C2)
      return BinaryOperator::CreateAShr(X, ConstantInt::get(Ty, AmtSum));
    }

    if (match(Op0, m_OneUse(m_SExt(m_Value(X)))) &&
        (Ty->isVectorTy() || shouldChangeType(Ty, X->getType()))) {
      // ashr (sext X), C --> sext (ashr X, C')
      Type *SrcTy = X->getType();
      ShAmt = std::min(ShAmt, SrcTy->getScalarSizeInBits() - 1);
      Value *NewSh = Builder.CreateAShr(X, ConstantInt::get(SrcTy, ShAmt));
      return new SExtInst(NewSh, Ty);
    }

    // If the shifted-out value is known-zero, then this is an exact shift.
    if (!I.isExact() &&
        MaskedValueIsZero(Op0, APInt::getLowBitsSet(BitWidth, ShAmt), 0, &I)) {
      I.setIsExact();
      return &I;
    }
  }

  // See if we can turn a signed shr into an unsigned shr.
  if (MaskedValueIsZero(Op0, APInt::getSignMask(BitWidth), 0, &I))
    return BinaryOperator::CreateLShr(Op0, Op1);

  return nullptr;
}
