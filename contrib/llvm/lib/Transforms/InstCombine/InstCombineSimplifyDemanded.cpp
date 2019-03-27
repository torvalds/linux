//===- InstCombineSimplifyDemanded.cpp ------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains logic for simplifying instructions based on information
// about how they are used.
//
//===----------------------------------------------------------------------===//

#include "InstCombineInternal.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/Support/KnownBits.h"

using namespace llvm;
using namespace llvm::PatternMatch;

#define DEBUG_TYPE "instcombine"

namespace {

struct AMDGPUImageDMaskIntrinsic {
  unsigned Intr;
};

#define GET_AMDGPUImageDMaskIntrinsicTable_IMPL
#include "InstCombineTables.inc"

} // end anonymous namespace

/// Check to see if the specified operand of the specified instruction is a
/// constant integer. If so, check to see if there are any bits set in the
/// constant that are not demanded. If so, shrink the constant and return true.
static bool ShrinkDemandedConstant(Instruction *I, unsigned OpNo,
                                   const APInt &Demanded) {
  assert(I && "No instruction?");
  assert(OpNo < I->getNumOperands() && "Operand index too large");

  // The operand must be a constant integer or splat integer.
  Value *Op = I->getOperand(OpNo);
  const APInt *C;
  if (!match(Op, m_APInt(C)))
    return false;

  // If there are no bits set that aren't demanded, nothing to do.
  if (C->isSubsetOf(Demanded))
    return false;

  // This instruction is producing bits that are not demanded. Shrink the RHS.
  I->setOperand(OpNo, ConstantInt::get(Op->getType(), *C & Demanded));

  return true;
}



/// Inst is an integer instruction that SimplifyDemandedBits knows about. See if
/// the instruction has any properties that allow us to simplify its operands.
bool InstCombiner::SimplifyDemandedInstructionBits(Instruction &Inst) {
  unsigned BitWidth = Inst.getType()->getScalarSizeInBits();
  KnownBits Known(BitWidth);
  APInt DemandedMask(APInt::getAllOnesValue(BitWidth));

  Value *V = SimplifyDemandedUseBits(&Inst, DemandedMask, Known,
                                     0, &Inst);
  if (!V) return false;
  if (V == &Inst) return true;
  replaceInstUsesWith(Inst, V);
  return true;
}

/// This form of SimplifyDemandedBits simplifies the specified instruction
/// operand if possible, updating it in place. It returns true if it made any
/// change and false otherwise.
bool InstCombiner::SimplifyDemandedBits(Instruction *I, unsigned OpNo,
                                        const APInt &DemandedMask,
                                        KnownBits &Known,
                                        unsigned Depth) {
  Use &U = I->getOperandUse(OpNo);
  Value *NewVal = SimplifyDemandedUseBits(U.get(), DemandedMask, Known,
                                          Depth, I);
  if (!NewVal) return false;
  U = NewVal;
  return true;
}


/// This function attempts to replace V with a simpler value based on the
/// demanded bits. When this function is called, it is known that only the bits
/// set in DemandedMask of the result of V are ever used downstream.
/// Consequently, depending on the mask and V, it may be possible to replace V
/// with a constant or one of its operands. In such cases, this function does
/// the replacement and returns true. In all other cases, it returns false after
/// analyzing the expression and setting KnownOne and known to be one in the
/// expression. Known.Zero contains all the bits that are known to be zero in
/// the expression. These are provided to potentially allow the caller (which
/// might recursively be SimplifyDemandedBits itself) to simplify the
/// expression.
/// Known.One and Known.Zero always follow the invariant that:
///   Known.One & Known.Zero == 0.
/// That is, a bit can't be both 1 and 0. Note that the bits in Known.One and
/// Known.Zero may only be accurate for those bits set in DemandedMask. Note
/// also that the bitwidth of V, DemandedMask, Known.Zero and Known.One must all
/// be the same.
///
/// This returns null if it did not change anything and it permits no
/// simplification.  This returns V itself if it did some simplification of V's
/// operands based on the information about what bits are demanded. This returns
/// some other non-null value if it found out that V is equal to another value
/// in the context where the specified bits are demanded, but not for all users.
Value *InstCombiner::SimplifyDemandedUseBits(Value *V, APInt DemandedMask,
                                             KnownBits &Known, unsigned Depth,
                                             Instruction *CxtI) {
  assert(V != nullptr && "Null pointer of Value???");
  assert(Depth <= 6 && "Limit Search Depth");
  uint32_t BitWidth = DemandedMask.getBitWidth();
  Type *VTy = V->getType();
  assert(
      (!VTy->isIntOrIntVectorTy() || VTy->getScalarSizeInBits() == BitWidth) &&
      Known.getBitWidth() == BitWidth &&
      "Value *V, DemandedMask and Known must have same BitWidth");

  if (isa<Constant>(V)) {
    computeKnownBits(V, Known, Depth, CxtI);
    return nullptr;
  }

  Known.resetAll();
  if (DemandedMask.isNullValue())     // Not demanding any bits from V.
    return UndefValue::get(VTy);

  if (Depth == 6)        // Limit search depth.
    return nullptr;

  Instruction *I = dyn_cast<Instruction>(V);
  if (!I) {
    computeKnownBits(V, Known, Depth, CxtI);
    return nullptr;        // Only analyze instructions.
  }

  // If there are multiple uses of this value and we aren't at the root, then
  // we can't do any simplifications of the operands, because DemandedMask
  // only reflects the bits demanded by *one* of the users.
  if (Depth != 0 && !I->hasOneUse())
    return SimplifyMultipleUseDemandedBits(I, DemandedMask, Known, Depth, CxtI);

  KnownBits LHSKnown(BitWidth), RHSKnown(BitWidth);

  // If this is the root being simplified, allow it to have multiple uses,
  // just set the DemandedMask to all bits so that we can try to simplify the
  // operands.  This allows visitTruncInst (for example) to simplify the
  // operand of a trunc without duplicating all the logic below.
  if (Depth == 0 && !V->hasOneUse())
    DemandedMask.setAllBits();

  switch (I->getOpcode()) {
  default:
    computeKnownBits(I, Known, Depth, CxtI);
    break;
  case Instruction::And: {
    // If either the LHS or the RHS are Zero, the result is zero.
    if (SimplifyDemandedBits(I, 1, DemandedMask, RHSKnown, Depth + 1) ||
        SimplifyDemandedBits(I, 0, DemandedMask & ~RHSKnown.Zero, LHSKnown,
                             Depth + 1))
      return I;
    assert(!RHSKnown.hasConflict() && "Bits known to be one AND zero?");
    assert(!LHSKnown.hasConflict() && "Bits known to be one AND zero?");

    // Output known-0 are known to be clear if zero in either the LHS | RHS.
    APInt IKnownZero = RHSKnown.Zero | LHSKnown.Zero;
    // Output known-1 bits are only known if set in both the LHS & RHS.
    APInt IKnownOne = RHSKnown.One & LHSKnown.One;

    // If the client is only demanding bits that we know, return the known
    // constant.
    if (DemandedMask.isSubsetOf(IKnownZero|IKnownOne))
      return Constant::getIntegerValue(VTy, IKnownOne);

    // If all of the demanded bits are known 1 on one side, return the other.
    // These bits cannot contribute to the result of the 'and'.
    if (DemandedMask.isSubsetOf(LHSKnown.Zero | RHSKnown.One))
      return I->getOperand(0);
    if (DemandedMask.isSubsetOf(RHSKnown.Zero | LHSKnown.One))
      return I->getOperand(1);

    // If the RHS is a constant, see if we can simplify it.
    if (ShrinkDemandedConstant(I, 1, DemandedMask & ~LHSKnown.Zero))
      return I;

    Known.Zero = std::move(IKnownZero);
    Known.One  = std::move(IKnownOne);
    break;
  }
  case Instruction::Or: {
    // If either the LHS or the RHS are One, the result is One.
    if (SimplifyDemandedBits(I, 1, DemandedMask, RHSKnown, Depth + 1) ||
        SimplifyDemandedBits(I, 0, DemandedMask & ~RHSKnown.One, LHSKnown,
                             Depth + 1))
      return I;
    assert(!RHSKnown.hasConflict() && "Bits known to be one AND zero?");
    assert(!LHSKnown.hasConflict() && "Bits known to be one AND zero?");

    // Output known-0 bits are only known if clear in both the LHS & RHS.
    APInt IKnownZero = RHSKnown.Zero & LHSKnown.Zero;
    // Output known-1 are known. to be set if s.et in either the LHS | RHS.
    APInt IKnownOne = RHSKnown.One | LHSKnown.One;

    // If the client is only demanding bits that we know, return the known
    // constant.
    if (DemandedMask.isSubsetOf(IKnownZero|IKnownOne))
      return Constant::getIntegerValue(VTy, IKnownOne);

    // If all of the demanded bits are known zero on one side, return the other.
    // These bits cannot contribute to the result of the 'or'.
    if (DemandedMask.isSubsetOf(LHSKnown.One | RHSKnown.Zero))
      return I->getOperand(0);
    if (DemandedMask.isSubsetOf(RHSKnown.One | LHSKnown.Zero))
      return I->getOperand(1);

    // If the RHS is a constant, see if we can simplify it.
    if (ShrinkDemandedConstant(I, 1, DemandedMask))
      return I;

    Known.Zero = std::move(IKnownZero);
    Known.One  = std::move(IKnownOne);
    break;
  }
  case Instruction::Xor: {
    if (SimplifyDemandedBits(I, 1, DemandedMask, RHSKnown, Depth + 1) ||
        SimplifyDemandedBits(I, 0, DemandedMask, LHSKnown, Depth + 1))
      return I;
    assert(!RHSKnown.hasConflict() && "Bits known to be one AND zero?");
    assert(!LHSKnown.hasConflict() && "Bits known to be one AND zero?");

    // Output known-0 bits are known if clear or set in both the LHS & RHS.
    APInt IKnownZero = (RHSKnown.Zero & LHSKnown.Zero) |
                       (RHSKnown.One & LHSKnown.One);
    // Output known-1 are known to be set if set in only one of the LHS, RHS.
    APInt IKnownOne =  (RHSKnown.Zero & LHSKnown.One) |
                       (RHSKnown.One & LHSKnown.Zero);

    // If the client is only demanding bits that we know, return the known
    // constant.
    if (DemandedMask.isSubsetOf(IKnownZero|IKnownOne))
      return Constant::getIntegerValue(VTy, IKnownOne);

    // If all of the demanded bits are known zero on one side, return the other.
    // These bits cannot contribute to the result of the 'xor'.
    if (DemandedMask.isSubsetOf(RHSKnown.Zero))
      return I->getOperand(0);
    if (DemandedMask.isSubsetOf(LHSKnown.Zero))
      return I->getOperand(1);

    // If all of the demanded bits are known to be zero on one side or the
    // other, turn this into an *inclusive* or.
    //    e.g. (A & C1)^(B & C2) -> (A & C1)|(B & C2) iff C1&C2 == 0
    if (DemandedMask.isSubsetOf(RHSKnown.Zero | LHSKnown.Zero)) {
      Instruction *Or =
        BinaryOperator::CreateOr(I->getOperand(0), I->getOperand(1),
                                 I->getName());
      return InsertNewInstWith(Or, *I);
    }

    // If all of the demanded bits on one side are known, and all of the set
    // bits on that side are also known to be set on the other side, turn this
    // into an AND, as we know the bits will be cleared.
    //    e.g. (X | C1) ^ C2 --> (X | C1) & ~C2 iff (C1&C2) == C2
    if (DemandedMask.isSubsetOf(RHSKnown.Zero|RHSKnown.One) &&
        RHSKnown.One.isSubsetOf(LHSKnown.One)) {
      Constant *AndC = Constant::getIntegerValue(VTy,
                                                 ~RHSKnown.One & DemandedMask);
      Instruction *And = BinaryOperator::CreateAnd(I->getOperand(0), AndC);
      return InsertNewInstWith(And, *I);
    }

    // If the RHS is a constant, see if we can simplify it.
    // FIXME: for XOR, we prefer to force bits to 1 if they will make a -1.
    if (ShrinkDemandedConstant(I, 1, DemandedMask))
      return I;

    // If our LHS is an 'and' and if it has one use, and if any of the bits we
    // are flipping are known to be set, then the xor is just resetting those
    // bits to zero.  We can just knock out bits from the 'and' and the 'xor',
    // simplifying both of them.
    if (Instruction *LHSInst = dyn_cast<Instruction>(I->getOperand(0)))
      if (LHSInst->getOpcode() == Instruction::And && LHSInst->hasOneUse() &&
          isa<ConstantInt>(I->getOperand(1)) &&
          isa<ConstantInt>(LHSInst->getOperand(1)) &&
          (LHSKnown.One & RHSKnown.One & DemandedMask) != 0) {
        ConstantInt *AndRHS = cast<ConstantInt>(LHSInst->getOperand(1));
        ConstantInt *XorRHS = cast<ConstantInt>(I->getOperand(1));
        APInt NewMask = ~(LHSKnown.One & RHSKnown.One & DemandedMask);

        Constant *AndC =
          ConstantInt::get(I->getType(), NewMask & AndRHS->getValue());
        Instruction *NewAnd = BinaryOperator::CreateAnd(I->getOperand(0), AndC);
        InsertNewInstWith(NewAnd, *I);

        Constant *XorC =
          ConstantInt::get(I->getType(), NewMask & XorRHS->getValue());
        Instruction *NewXor = BinaryOperator::CreateXor(NewAnd, XorC);
        return InsertNewInstWith(NewXor, *I);
      }

    // Output known-0 bits are known if clear or set in both the LHS & RHS.
    Known.Zero = std::move(IKnownZero);
    // Output known-1 are known to be set if set in only one of the LHS, RHS.
    Known.One  = std::move(IKnownOne);
    break;
  }
  case Instruction::Select: {
    Value *LHS, *RHS;
    SelectPatternFlavor SPF = matchSelectPattern(I, LHS, RHS).Flavor;
    if (SPF == SPF_UMAX) {
      // UMax(A, C) == A if ...
      // The lowest non-zero bit of DemandMask is higher than the highest
      // non-zero bit of C.
      const APInt *C;
      unsigned CTZ = DemandedMask.countTrailingZeros();
      if (match(RHS, m_APInt(C)) && CTZ >= C->getActiveBits())
        return LHS;
    } else if (SPF == SPF_UMIN) {
      // UMin(A, C) == A if ...
      // The lowest non-zero bit of DemandMask is higher than the highest
      // non-one bit of C.
      // This comes from using DeMorgans on the above umax example.
      const APInt *C;
      unsigned CTZ = DemandedMask.countTrailingZeros();
      if (match(RHS, m_APInt(C)) &&
          CTZ >= C->getBitWidth() - C->countLeadingOnes())
        return LHS;
    }

    // If this is a select as part of any other min/max pattern, don't simplify
    // any further in case we break the structure.
    if (SPF != SPF_UNKNOWN)
      return nullptr;

    if (SimplifyDemandedBits(I, 2, DemandedMask, RHSKnown, Depth + 1) ||
        SimplifyDemandedBits(I, 1, DemandedMask, LHSKnown, Depth + 1))
      return I;
    assert(!RHSKnown.hasConflict() && "Bits known to be one AND zero?");
    assert(!LHSKnown.hasConflict() && "Bits known to be one AND zero?");

    // If the operands are constants, see if we can simplify them.
    if (ShrinkDemandedConstant(I, 1, DemandedMask) ||
        ShrinkDemandedConstant(I, 2, DemandedMask))
      return I;

    // Only known if known in both the LHS and RHS.
    Known.One = RHSKnown.One & LHSKnown.One;
    Known.Zero = RHSKnown.Zero & LHSKnown.Zero;
    break;
  }
  case Instruction::ZExt:
  case Instruction::Trunc: {
    unsigned SrcBitWidth = I->getOperand(0)->getType()->getScalarSizeInBits();

    APInt InputDemandedMask = DemandedMask.zextOrTrunc(SrcBitWidth);
    KnownBits InputKnown(SrcBitWidth);
    if (SimplifyDemandedBits(I, 0, InputDemandedMask, InputKnown, Depth + 1))
      return I;
    Known = InputKnown.zextOrTrunc(BitWidth);
    // Any top bits are known to be zero.
    if (BitWidth > SrcBitWidth)
      Known.Zero.setBitsFrom(SrcBitWidth);
    assert(!Known.hasConflict() && "Bits known to be one AND zero?");
    break;
  }
  case Instruction::BitCast:
    if (!I->getOperand(0)->getType()->isIntOrIntVectorTy())
      return nullptr;  // vector->int or fp->int?

    if (VectorType *DstVTy = dyn_cast<VectorType>(I->getType())) {
      if (VectorType *SrcVTy =
            dyn_cast<VectorType>(I->getOperand(0)->getType())) {
        if (DstVTy->getNumElements() != SrcVTy->getNumElements())
          // Don't touch a bitcast between vectors of different element counts.
          return nullptr;
      } else
        // Don't touch a scalar-to-vector bitcast.
        return nullptr;
    } else if (I->getOperand(0)->getType()->isVectorTy())
      // Don't touch a vector-to-scalar bitcast.
      return nullptr;

    if (SimplifyDemandedBits(I, 0, DemandedMask, Known, Depth + 1))
      return I;
    assert(!Known.hasConflict() && "Bits known to be one AND zero?");
    break;
  case Instruction::SExt: {
    // Compute the bits in the result that are not present in the input.
    unsigned SrcBitWidth = I->getOperand(0)->getType()->getScalarSizeInBits();

    APInt InputDemandedBits = DemandedMask.trunc(SrcBitWidth);

    // If any of the sign extended bits are demanded, we know that the sign
    // bit is demanded.
    if (DemandedMask.getActiveBits() > SrcBitWidth)
      InputDemandedBits.setBit(SrcBitWidth-1);

    KnownBits InputKnown(SrcBitWidth);
    if (SimplifyDemandedBits(I, 0, InputDemandedBits, InputKnown, Depth + 1))
      return I;

    // If the input sign bit is known zero, or if the NewBits are not demanded
    // convert this into a zero extension.
    if (InputKnown.isNonNegative() ||
        DemandedMask.getActiveBits() <= SrcBitWidth) {
      // Convert to ZExt cast.
      CastInst *NewCast = new ZExtInst(I->getOperand(0), VTy, I->getName());
      return InsertNewInstWith(NewCast, *I);
     }

    // If the sign bit of the input is known set or clear, then we know the
    // top bits of the result.
    Known = InputKnown.sext(BitWidth);
    assert(!Known.hasConflict() && "Bits known to be one AND zero?");
    break;
  }
  case Instruction::Add:
  case Instruction::Sub: {
    /// If the high-bits of an ADD/SUB are not demanded, then we do not care
    /// about the high bits of the operands.
    unsigned NLZ = DemandedMask.countLeadingZeros();
    // Right fill the mask of bits for this ADD/SUB to demand the most
    // significant bit and all those below it.
    APInt DemandedFromOps(APInt::getLowBitsSet(BitWidth, BitWidth-NLZ));
    if (ShrinkDemandedConstant(I, 0, DemandedFromOps) ||
        SimplifyDemandedBits(I, 0, DemandedFromOps, LHSKnown, Depth + 1) ||
        ShrinkDemandedConstant(I, 1, DemandedFromOps) ||
        SimplifyDemandedBits(I, 1, DemandedFromOps, RHSKnown, Depth + 1)) {
      if (NLZ > 0) {
        // Disable the nsw and nuw flags here: We can no longer guarantee that
        // we won't wrap after simplification. Removing the nsw/nuw flags is
        // legal here because the top bit is not demanded.
        BinaryOperator &BinOP = *cast<BinaryOperator>(I);
        BinOP.setHasNoSignedWrap(false);
        BinOP.setHasNoUnsignedWrap(false);
      }
      return I;
    }

    // If we are known to be adding/subtracting zeros to every bit below
    // the highest demanded bit, we just return the other side.
    if (DemandedFromOps.isSubsetOf(RHSKnown.Zero))
      return I->getOperand(0);
    // We can't do this with the LHS for subtraction, unless we are only
    // demanding the LSB.
    if ((I->getOpcode() == Instruction::Add ||
         DemandedFromOps.isOneValue()) &&
        DemandedFromOps.isSubsetOf(LHSKnown.Zero))
      return I->getOperand(1);

    // Otherwise just compute the known bits of the result.
    bool NSW = cast<OverflowingBinaryOperator>(I)->hasNoSignedWrap();
    Known = KnownBits::computeForAddSub(I->getOpcode() == Instruction::Add,
                                        NSW, LHSKnown, RHSKnown);
    break;
  }
  case Instruction::Shl: {
    const APInt *SA;
    if (match(I->getOperand(1), m_APInt(SA))) {
      const APInt *ShrAmt;
      if (match(I->getOperand(0), m_Shr(m_Value(), m_APInt(ShrAmt))))
        if (Instruction *Shr = dyn_cast<Instruction>(I->getOperand(0)))
          if (Value *R = simplifyShrShlDemandedBits(Shr, *ShrAmt, I, *SA,
                                                    DemandedMask, Known))
            return R;

      uint64_t ShiftAmt = SA->getLimitedValue(BitWidth-1);
      APInt DemandedMaskIn(DemandedMask.lshr(ShiftAmt));

      // If the shift is NUW/NSW, then it does demand the high bits.
      ShlOperator *IOp = cast<ShlOperator>(I);
      if (IOp->hasNoSignedWrap())
        DemandedMaskIn.setHighBits(ShiftAmt+1);
      else if (IOp->hasNoUnsignedWrap())
        DemandedMaskIn.setHighBits(ShiftAmt);

      if (SimplifyDemandedBits(I, 0, DemandedMaskIn, Known, Depth + 1))
        return I;
      assert(!Known.hasConflict() && "Bits known to be one AND zero?");
      Known.Zero <<= ShiftAmt;
      Known.One  <<= ShiftAmt;
      // low bits known zero.
      if (ShiftAmt)
        Known.Zero.setLowBits(ShiftAmt);
    }
    break;
  }
  case Instruction::LShr: {
    const APInt *SA;
    if (match(I->getOperand(1), m_APInt(SA))) {
      uint64_t ShiftAmt = SA->getLimitedValue(BitWidth-1);

      // Unsigned shift right.
      APInt DemandedMaskIn(DemandedMask.shl(ShiftAmt));

      // If the shift is exact, then it does demand the low bits (and knows that
      // they are zero).
      if (cast<LShrOperator>(I)->isExact())
        DemandedMaskIn.setLowBits(ShiftAmt);

      if (SimplifyDemandedBits(I, 0, DemandedMaskIn, Known, Depth + 1))
        return I;
      assert(!Known.hasConflict() && "Bits known to be one AND zero?");
      Known.Zero.lshrInPlace(ShiftAmt);
      Known.One.lshrInPlace(ShiftAmt);
      if (ShiftAmt)
        Known.Zero.setHighBits(ShiftAmt);  // high bits known zero.
    }
    break;
  }
  case Instruction::AShr: {
    // If this is an arithmetic shift right and only the low-bit is set, we can
    // always convert this into a logical shr, even if the shift amount is
    // variable.  The low bit of the shift cannot be an input sign bit unless
    // the shift amount is >= the size of the datatype, which is undefined.
    if (DemandedMask.isOneValue()) {
      // Perform the logical shift right.
      Instruction *NewVal = BinaryOperator::CreateLShr(
                        I->getOperand(0), I->getOperand(1), I->getName());
      return InsertNewInstWith(NewVal, *I);
    }

    // If the sign bit is the only bit demanded by this ashr, then there is no
    // need to do it, the shift doesn't change the high bit.
    if (DemandedMask.isSignMask())
      return I->getOperand(0);

    const APInt *SA;
    if (match(I->getOperand(1), m_APInt(SA))) {
      uint32_t ShiftAmt = SA->getLimitedValue(BitWidth-1);

      // Signed shift right.
      APInt DemandedMaskIn(DemandedMask.shl(ShiftAmt));
      // If any of the high bits are demanded, we should set the sign bit as
      // demanded.
      if (DemandedMask.countLeadingZeros() <= ShiftAmt)
        DemandedMaskIn.setSignBit();

      // If the shift is exact, then it does demand the low bits (and knows that
      // they are zero).
      if (cast<AShrOperator>(I)->isExact())
        DemandedMaskIn.setLowBits(ShiftAmt);

      if (SimplifyDemandedBits(I, 0, DemandedMaskIn, Known, Depth + 1))
        return I;

      unsigned SignBits = ComputeNumSignBits(I->getOperand(0), Depth + 1, CxtI);

      assert(!Known.hasConflict() && "Bits known to be one AND zero?");
      // Compute the new bits that are at the top now plus sign bits.
      APInt HighBits(APInt::getHighBitsSet(
          BitWidth, std::min(SignBits + ShiftAmt - 1, BitWidth)));
      Known.Zero.lshrInPlace(ShiftAmt);
      Known.One.lshrInPlace(ShiftAmt);

      // If the input sign bit is known to be zero, or if none of the top bits
      // are demanded, turn this into an unsigned shift right.
      assert(BitWidth > ShiftAmt && "Shift amount not saturated?");
      if (Known.Zero[BitWidth-ShiftAmt-1] ||
          !DemandedMask.intersects(HighBits)) {
        BinaryOperator *LShr = BinaryOperator::CreateLShr(I->getOperand(0),
                                                          I->getOperand(1));
        LShr->setIsExact(cast<BinaryOperator>(I)->isExact());
        return InsertNewInstWith(LShr, *I);
      } else if (Known.One[BitWidth-ShiftAmt-1]) { // New bits are known one.
        Known.One |= HighBits;
      }
    }
    break;
  }
  case Instruction::UDiv: {
    // UDiv doesn't demand low bits that are zero in the divisor.
    const APInt *SA;
    if (match(I->getOperand(1), m_APInt(SA))) {
      // If the shift is exact, then it does demand the low bits.
      if (cast<UDivOperator>(I)->isExact())
        break;

      // FIXME: Take the demanded mask of the result into account.
      unsigned RHSTrailingZeros = SA->countTrailingZeros();
      APInt DemandedMaskIn =
          APInt::getHighBitsSet(BitWidth, BitWidth - RHSTrailingZeros);
      if (SimplifyDemandedBits(I, 0, DemandedMaskIn, LHSKnown, Depth + 1))
        return I;

      // Propagate zero bits from the input.
      Known.Zero.setHighBits(std::min(
          BitWidth, LHSKnown.Zero.countLeadingOnes() + RHSTrailingZeros));
    }
    break;
  }
  case Instruction::SRem:
    if (ConstantInt *Rem = dyn_cast<ConstantInt>(I->getOperand(1))) {
      // X % -1 demands all the bits because we don't want to introduce
      // INT_MIN % -1 (== undef) by accident.
      if (Rem->isMinusOne())
        break;
      APInt RA = Rem->getValue().abs();
      if (RA.isPowerOf2()) {
        if (DemandedMask.ult(RA))    // srem won't affect demanded bits
          return I->getOperand(0);

        APInt LowBits = RA - 1;
        APInt Mask2 = LowBits | APInt::getSignMask(BitWidth);
        if (SimplifyDemandedBits(I, 0, Mask2, LHSKnown, Depth + 1))
          return I;

        // The low bits of LHS are unchanged by the srem.
        Known.Zero = LHSKnown.Zero & LowBits;
        Known.One = LHSKnown.One & LowBits;

        // If LHS is non-negative or has all low bits zero, then the upper bits
        // are all zero.
        if (LHSKnown.isNonNegative() || LowBits.isSubsetOf(LHSKnown.Zero))
          Known.Zero |= ~LowBits;

        // If LHS is negative and not all low bits are zero, then the upper bits
        // are all one.
        if (LHSKnown.isNegative() && LowBits.intersects(LHSKnown.One))
          Known.One |= ~LowBits;

        assert(!Known.hasConflict() && "Bits known to be one AND zero?");
        break;
      }
    }

    // The sign bit is the LHS's sign bit, except when the result of the
    // remainder is zero.
    if (DemandedMask.isSignBitSet()) {
      computeKnownBits(I->getOperand(0), LHSKnown, Depth + 1, CxtI);
      // If it's known zero, our sign bit is also zero.
      if (LHSKnown.isNonNegative())
        Known.makeNonNegative();
    }
    break;
  case Instruction::URem: {
    KnownBits Known2(BitWidth);
    APInt AllOnes = APInt::getAllOnesValue(BitWidth);
    if (SimplifyDemandedBits(I, 0, AllOnes, Known2, Depth + 1) ||
        SimplifyDemandedBits(I, 1, AllOnes, Known2, Depth + 1))
      return I;

    unsigned Leaders = Known2.countMinLeadingZeros();
    Known.Zero = APInt::getHighBitsSet(BitWidth, Leaders) & DemandedMask;
    break;
  }
  case Instruction::Call:
    if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(I)) {
      switch (II->getIntrinsicID()) {
      default: break;
      case Intrinsic::bswap: {
        // If the only bits demanded come from one byte of the bswap result,
        // just shift the input byte into position to eliminate the bswap.
        unsigned NLZ = DemandedMask.countLeadingZeros();
        unsigned NTZ = DemandedMask.countTrailingZeros();

        // Round NTZ down to the next byte.  If we have 11 trailing zeros, then
        // we need all the bits down to bit 8.  Likewise, round NLZ.  If we
        // have 14 leading zeros, round to 8.
        NLZ &= ~7;
        NTZ &= ~7;
        // If we need exactly one byte, we can do this transformation.
        if (BitWidth-NLZ-NTZ == 8) {
          unsigned ResultBit = NTZ;
          unsigned InputBit = BitWidth-NTZ-8;

          // Replace this with either a left or right shift to get the byte into
          // the right place.
          Instruction *NewVal;
          if (InputBit > ResultBit)
            NewVal = BinaryOperator::CreateLShr(II->getArgOperand(0),
                    ConstantInt::get(I->getType(), InputBit-ResultBit));
          else
            NewVal = BinaryOperator::CreateShl(II->getArgOperand(0),
                    ConstantInt::get(I->getType(), ResultBit-InputBit));
          NewVal->takeName(I);
          return InsertNewInstWith(NewVal, *I);
        }

        // TODO: Could compute known zero/one bits based on the input.
        break;
      }
      case Intrinsic::fshr:
      case Intrinsic::fshl: {
        const APInt *SA;
        if (!match(I->getOperand(2), m_APInt(SA)))
          break;

        // Normalize to funnel shift left. APInt shifts of BitWidth are well-
        // defined, so no need to special-case zero shifts here.
        uint64_t ShiftAmt = SA->urem(BitWidth);
        if (II->getIntrinsicID() == Intrinsic::fshr)
          ShiftAmt = BitWidth - ShiftAmt;

        APInt DemandedMaskLHS(DemandedMask.lshr(ShiftAmt));
        APInt DemandedMaskRHS(DemandedMask.shl(BitWidth - ShiftAmt));
        if (SimplifyDemandedBits(I, 0, DemandedMaskLHS, LHSKnown, Depth + 1) ||
            SimplifyDemandedBits(I, 1, DemandedMaskRHS, RHSKnown, Depth + 1))
          return I;

        Known.Zero = LHSKnown.Zero.shl(ShiftAmt) |
                     RHSKnown.Zero.lshr(BitWidth - ShiftAmt);
        Known.One = LHSKnown.One.shl(ShiftAmt) |
                    RHSKnown.One.lshr(BitWidth - ShiftAmt);
        break;
      }
      case Intrinsic::x86_mmx_pmovmskb:
      case Intrinsic::x86_sse_movmsk_ps:
      case Intrinsic::x86_sse2_movmsk_pd:
      case Intrinsic::x86_sse2_pmovmskb_128:
      case Intrinsic::x86_avx_movmsk_ps_256:
      case Intrinsic::x86_avx_movmsk_pd_256:
      case Intrinsic::x86_avx2_pmovmskb: {
        // MOVMSK copies the vector elements' sign bits to the low bits
        // and zeros the high bits.
        unsigned ArgWidth;
        if (II->getIntrinsicID() == Intrinsic::x86_mmx_pmovmskb) {
          ArgWidth = 8; // Arg is x86_mmx, but treated as <8 x i8>.
        } else {
          auto Arg = II->getArgOperand(0);
          auto ArgType = cast<VectorType>(Arg->getType());
          ArgWidth = ArgType->getNumElements();
        }

        // If we don't need any of low bits then return zero,
        // we know that DemandedMask is non-zero already.
        APInt DemandedElts = DemandedMask.zextOrTrunc(ArgWidth);
        if (DemandedElts.isNullValue())
          return ConstantInt::getNullValue(VTy);

        // We know that the upper bits are set to zero.
        Known.Zero.setBitsFrom(ArgWidth);
        return nullptr;
      }
      case Intrinsic::x86_sse42_crc32_64_64:
        Known.Zero.setBitsFrom(32);
        return nullptr;
      }
    }
    computeKnownBits(V, Known, Depth, CxtI);
    break;
  }

  // If the client is only demanding bits that we know, return the known
  // constant.
  if (DemandedMask.isSubsetOf(Known.Zero|Known.One))
    return Constant::getIntegerValue(VTy, Known.One);
  return nullptr;
}

/// Helper routine of SimplifyDemandedUseBits. It computes Known
/// bits. It also tries to handle simplifications that can be done based on
/// DemandedMask, but without modifying the Instruction.
Value *InstCombiner::SimplifyMultipleUseDemandedBits(Instruction *I,
                                                     const APInt &DemandedMask,
                                                     KnownBits &Known,
                                                     unsigned Depth,
                                                     Instruction *CxtI) {
  unsigned BitWidth = DemandedMask.getBitWidth();
  Type *ITy = I->getType();

  KnownBits LHSKnown(BitWidth);
  KnownBits RHSKnown(BitWidth);

  // Despite the fact that we can't simplify this instruction in all User's
  // context, we can at least compute the known bits, and we can
  // do simplifications that apply to *just* the one user if we know that
  // this instruction has a simpler value in that context.
  switch (I->getOpcode()) {
  case Instruction::And: {
    // If either the LHS or the RHS are Zero, the result is zero.
    computeKnownBits(I->getOperand(1), RHSKnown, Depth + 1, CxtI);
    computeKnownBits(I->getOperand(0), LHSKnown, Depth + 1,
                     CxtI);

    // Output known-0 are known to be clear if zero in either the LHS | RHS.
    APInt IKnownZero = RHSKnown.Zero | LHSKnown.Zero;
    // Output known-1 bits are only known if set in both the LHS & RHS.
    APInt IKnownOne = RHSKnown.One & LHSKnown.One;

    // If the client is only demanding bits that we know, return the known
    // constant.
    if (DemandedMask.isSubsetOf(IKnownZero|IKnownOne))
      return Constant::getIntegerValue(ITy, IKnownOne);

    // If all of the demanded bits are known 1 on one side, return the other.
    // These bits cannot contribute to the result of the 'and' in this
    // context.
    if (DemandedMask.isSubsetOf(LHSKnown.Zero | RHSKnown.One))
      return I->getOperand(0);
    if (DemandedMask.isSubsetOf(RHSKnown.Zero | LHSKnown.One))
      return I->getOperand(1);

    Known.Zero = std::move(IKnownZero);
    Known.One  = std::move(IKnownOne);
    break;
  }
  case Instruction::Or: {
    // We can simplify (X|Y) -> X or Y in the user's context if we know that
    // only bits from X or Y are demanded.

    // If either the LHS or the RHS are One, the result is One.
    computeKnownBits(I->getOperand(1), RHSKnown, Depth + 1, CxtI);
    computeKnownBits(I->getOperand(0), LHSKnown, Depth + 1,
                     CxtI);

    // Output known-0 bits are only known if clear in both the LHS & RHS.
    APInt IKnownZero = RHSKnown.Zero & LHSKnown.Zero;
    // Output known-1 are known to be set if set in either the LHS | RHS.
    APInt IKnownOne = RHSKnown.One | LHSKnown.One;

    // If the client is only demanding bits that we know, return the known
    // constant.
    if (DemandedMask.isSubsetOf(IKnownZero|IKnownOne))
      return Constant::getIntegerValue(ITy, IKnownOne);

    // If all of the demanded bits are known zero on one side, return the
    // other.  These bits cannot contribute to the result of the 'or' in this
    // context.
    if (DemandedMask.isSubsetOf(LHSKnown.One | RHSKnown.Zero))
      return I->getOperand(0);
    if (DemandedMask.isSubsetOf(RHSKnown.One | LHSKnown.Zero))
      return I->getOperand(1);

    Known.Zero = std::move(IKnownZero);
    Known.One  = std::move(IKnownOne);
    break;
  }
  case Instruction::Xor: {
    // We can simplify (X^Y) -> X or Y in the user's context if we know that
    // only bits from X or Y are demanded.

    computeKnownBits(I->getOperand(1), RHSKnown, Depth + 1, CxtI);
    computeKnownBits(I->getOperand(0), LHSKnown, Depth + 1,
                     CxtI);

    // Output known-0 bits are known if clear or set in both the LHS & RHS.
    APInt IKnownZero = (RHSKnown.Zero & LHSKnown.Zero) |
                       (RHSKnown.One & LHSKnown.One);
    // Output known-1 are known to be set if set in only one of the LHS, RHS.
    APInt IKnownOne =  (RHSKnown.Zero & LHSKnown.One) |
                       (RHSKnown.One & LHSKnown.Zero);

    // If the client is only demanding bits that we know, return the known
    // constant.
    if (DemandedMask.isSubsetOf(IKnownZero|IKnownOne))
      return Constant::getIntegerValue(ITy, IKnownOne);

    // If all of the demanded bits are known zero on one side, return the
    // other.
    if (DemandedMask.isSubsetOf(RHSKnown.Zero))
      return I->getOperand(0);
    if (DemandedMask.isSubsetOf(LHSKnown.Zero))
      return I->getOperand(1);

    // Output known-0 bits are known if clear or set in both the LHS & RHS.
    Known.Zero = std::move(IKnownZero);
    // Output known-1 are known to be set if set in only one of the LHS, RHS.
    Known.One  = std::move(IKnownOne);
    break;
  }
  default:
    // Compute the Known bits to simplify things downstream.
    computeKnownBits(I, Known, Depth, CxtI);

    // If this user is only demanding bits that we know, return the known
    // constant.
    if (DemandedMask.isSubsetOf(Known.Zero|Known.One))
      return Constant::getIntegerValue(ITy, Known.One);

    break;
  }

  return nullptr;
}


/// Helper routine of SimplifyDemandedUseBits. It tries to simplify
/// "E1 = (X lsr C1) << C2", where the C1 and C2 are constant, into
/// "E2 = X << (C2 - C1)" or "E2 = X >> (C1 - C2)", depending on the sign
/// of "C2-C1".
///
/// Suppose E1 and E2 are generally different in bits S={bm, bm+1,
/// ..., bn}, without considering the specific value X is holding.
/// This transformation is legal iff one of following conditions is hold:
///  1) All the bit in S are 0, in this case E1 == E2.
///  2) We don't care those bits in S, per the input DemandedMask.
///  3) Combination of 1) and 2). Some bits in S are 0, and we don't care the
///     rest bits.
///
/// Currently we only test condition 2).
///
/// As with SimplifyDemandedUseBits, it returns NULL if the simplification was
/// not successful.
Value *
InstCombiner::simplifyShrShlDemandedBits(Instruction *Shr, const APInt &ShrOp1,
                                         Instruction *Shl, const APInt &ShlOp1,
                                         const APInt &DemandedMask,
                                         KnownBits &Known) {
  if (!ShlOp1 || !ShrOp1)
    return nullptr; // No-op.

  Value *VarX = Shr->getOperand(0);
  Type *Ty = VarX->getType();
  unsigned BitWidth = Ty->getScalarSizeInBits();
  if (ShlOp1.uge(BitWidth) || ShrOp1.uge(BitWidth))
    return nullptr; // Undef.

  unsigned ShlAmt = ShlOp1.getZExtValue();
  unsigned ShrAmt = ShrOp1.getZExtValue();

  Known.One.clearAllBits();
  Known.Zero.setLowBits(ShlAmt - 1);
  Known.Zero &= DemandedMask;

  APInt BitMask1(APInt::getAllOnesValue(BitWidth));
  APInt BitMask2(APInt::getAllOnesValue(BitWidth));

  bool isLshr = (Shr->getOpcode() == Instruction::LShr);
  BitMask1 = isLshr ? (BitMask1.lshr(ShrAmt) << ShlAmt) :
                      (BitMask1.ashr(ShrAmt) << ShlAmt);

  if (ShrAmt <= ShlAmt) {
    BitMask2 <<= (ShlAmt - ShrAmt);
  } else {
    BitMask2 = isLshr ? BitMask2.lshr(ShrAmt - ShlAmt):
                        BitMask2.ashr(ShrAmt - ShlAmt);
  }

  // Check if condition-2 (see the comment to this function) is satified.
  if ((BitMask1 & DemandedMask) == (BitMask2 & DemandedMask)) {
    if (ShrAmt == ShlAmt)
      return VarX;

    if (!Shr->hasOneUse())
      return nullptr;

    BinaryOperator *New;
    if (ShrAmt < ShlAmt) {
      Constant *Amt = ConstantInt::get(VarX->getType(), ShlAmt - ShrAmt);
      New = BinaryOperator::CreateShl(VarX, Amt);
      BinaryOperator *Orig = cast<BinaryOperator>(Shl);
      New->setHasNoSignedWrap(Orig->hasNoSignedWrap());
      New->setHasNoUnsignedWrap(Orig->hasNoUnsignedWrap());
    } else {
      Constant *Amt = ConstantInt::get(VarX->getType(), ShrAmt - ShlAmt);
      New = isLshr ? BinaryOperator::CreateLShr(VarX, Amt) :
                     BinaryOperator::CreateAShr(VarX, Amt);
      if (cast<BinaryOperator>(Shr)->isExact())
        New->setIsExact(true);
    }

    return InsertNewInstWith(New, *Shl);
  }

  return nullptr;
}

/// Implement SimplifyDemandedVectorElts for amdgcn buffer and image intrinsics.
Value *InstCombiner::simplifyAMDGCNMemoryIntrinsicDemanded(IntrinsicInst *II,
                                                           APInt DemandedElts,
                                                           int DMaskIdx,
                                                           int TFCIdx) {
  unsigned VWidth = II->getType()->getVectorNumElements();
  if (VWidth == 1)
    return nullptr;

  // Need to change to new instruction format
  ConstantInt *TFC = nullptr;
  bool TFELWEEnabled = false;
  if (TFCIdx > 0) {
    TFC = dyn_cast<ConstantInt>(II->getArgOperand(TFCIdx));
    TFELWEEnabled =    TFC->getZExtValue() & 0x1  // TFE
                    || TFC->getZExtValue() & 0x2; // LWE
  }

  if (TFELWEEnabled)
    return nullptr; // TFE not yet supported

  ConstantInt *NewDMask = nullptr;

  if (DMaskIdx < 0) {
    // Pretend that a prefix of elements is demanded to simplify the code
    // below.
    DemandedElts = (1 << DemandedElts.getActiveBits()) - 1;
  } else {
    ConstantInt *DMask = dyn_cast<ConstantInt>(II->getArgOperand(DMaskIdx));
    if (!DMask)
      return nullptr; // non-constant dmask is not supported by codegen

    unsigned DMaskVal = DMask->getZExtValue() & 0xf;

    // Mask off values that are undefined because the dmask doesn't cover them
    DemandedElts &= (1 << countPopulation(DMaskVal)) - 1;

    unsigned NewDMaskVal = 0;
    unsigned OrigLoadIdx = 0;
    for (unsigned SrcIdx = 0; SrcIdx < 4; ++SrcIdx) {
      const unsigned Bit = 1 << SrcIdx;
      if (!!(DMaskVal & Bit)) {
        if (!!DemandedElts[OrigLoadIdx])
          NewDMaskVal |= Bit;
        OrigLoadIdx++;
      }
    }

    if (DMaskVal != NewDMaskVal)
      NewDMask = ConstantInt::get(DMask->getType(), NewDMaskVal);
  }

  // TODO: Handle 3 vectors when supported in code gen.
  unsigned NewNumElts = PowerOf2Ceil(DemandedElts.countPopulation());
  if (!NewNumElts)
    return UndefValue::get(II->getType());

  if (NewNumElts >= VWidth && DemandedElts.isMask()) {
    if (NewDMask)
      II->setArgOperand(DMaskIdx, NewDMask);
    return nullptr;
  }

  // Determine the overload types of the original intrinsic.
  auto IID = II->getIntrinsicID();
  SmallVector<Intrinsic::IITDescriptor, 16> Table;
  getIntrinsicInfoTableEntries(IID, Table);
  ArrayRef<Intrinsic::IITDescriptor> TableRef = Table;

  FunctionType *FTy = II->getCalledFunction()->getFunctionType();
  SmallVector<Type *, 6> OverloadTys;
  Intrinsic::matchIntrinsicType(FTy->getReturnType(), TableRef, OverloadTys);
  for (unsigned i = 0, e = FTy->getNumParams(); i != e; ++i)
    Intrinsic::matchIntrinsicType(FTy->getParamType(i), TableRef, OverloadTys);

  // Get the new return type overload of the intrinsic.
  Module *M = II->getParent()->getParent()->getParent();
  Type *EltTy = II->getType()->getVectorElementType();
  Type *NewTy = (NewNumElts == 1) ? EltTy : VectorType::get(EltTy, NewNumElts);

  OverloadTys[0] = NewTy;
  Function *NewIntrin = Intrinsic::getDeclaration(M, IID, OverloadTys);

  SmallVector<Value *, 16> Args;
  for (unsigned I = 0, E = II->getNumArgOperands(); I != E; ++I)
    Args.push_back(II->getArgOperand(I));

  if (NewDMask)
    Args[DMaskIdx] = NewDMask;

  IRBuilderBase::InsertPointGuard Guard(Builder);
  Builder.SetInsertPoint(II);

  CallInst *NewCall = Builder.CreateCall(NewIntrin, Args);
  NewCall->takeName(II);
  NewCall->copyMetadata(*II);

  if (NewNumElts == 1) {
    return Builder.CreateInsertElement(UndefValue::get(II->getType()), NewCall,
                                       DemandedElts.countTrailingZeros());
  }

  SmallVector<uint32_t, 8> EltMask;
  unsigned NewLoadIdx = 0;
  for (unsigned OrigLoadIdx = 0; OrigLoadIdx < VWidth; ++OrigLoadIdx) {
    if (!!DemandedElts[OrigLoadIdx])
      EltMask.push_back(NewLoadIdx++);
    else
      EltMask.push_back(NewNumElts);
  }

  Value *Shuffle =
      Builder.CreateShuffleVector(NewCall, UndefValue::get(NewTy), EltMask);

  return Shuffle;
}

/// The specified value produces a vector with any number of elements.
/// DemandedElts contains the set of elements that are actually used by the
/// caller. This method analyzes which elements of the operand are undef and
/// returns that information in UndefElts.
///
/// If the information about demanded elements can be used to simplify the
/// operation, the operation is simplified, then the resultant value is
/// returned.  This returns null if no change was made.
Value *InstCombiner::SimplifyDemandedVectorElts(Value *V, APInt DemandedElts,
                                                APInt &UndefElts,
                                                unsigned Depth) {
  unsigned VWidth = V->getType()->getVectorNumElements();
  APInt EltMask(APInt::getAllOnesValue(VWidth));
  assert((DemandedElts & ~EltMask) == 0 && "Invalid DemandedElts!");

  if (isa<UndefValue>(V)) {
    // If the entire vector is undefined, just return this info.
    UndefElts = EltMask;
    return nullptr;
  }

  if (DemandedElts.isNullValue()) { // If nothing is demanded, provide undef.
    UndefElts = EltMask;
    return UndefValue::get(V->getType());
  }

  UndefElts = 0;

  if (auto *C = dyn_cast<Constant>(V)) {
    // Check if this is identity. If so, return 0 since we are not simplifying
    // anything.
    if (DemandedElts.isAllOnesValue())
      return nullptr;

    Type *EltTy = cast<VectorType>(V->getType())->getElementType();
    Constant *Undef = UndefValue::get(EltTy);
    SmallVector<Constant*, 16> Elts;
    for (unsigned i = 0; i != VWidth; ++i) {
      if (!DemandedElts[i]) {   // If not demanded, set to undef.
        Elts.push_back(Undef);
        UndefElts.setBit(i);
        continue;
      }

      Constant *Elt = C->getAggregateElement(i);
      if (!Elt) return nullptr;

      if (isa<UndefValue>(Elt)) {   // Already undef.
        Elts.push_back(Undef);
        UndefElts.setBit(i);
      } else {                               // Otherwise, defined.
        Elts.push_back(Elt);
      }
    }

    // If we changed the constant, return it.
    Constant *NewCV = ConstantVector::get(Elts);
    return NewCV != C ? NewCV : nullptr;
  }

  // Limit search depth.
  if (Depth == 10)
    return nullptr;

  // If multiple users are using the root value, proceed with
  // simplification conservatively assuming that all elements
  // are needed.
  if (!V->hasOneUse()) {
    // Quit if we find multiple users of a non-root value though.
    // They'll be handled when it's their turn to be visited by
    // the main instcombine process.
    if (Depth != 0)
      // TODO: Just compute the UndefElts information recursively.
      return nullptr;

    // Conservatively assume that all elements are needed.
    DemandedElts = EltMask;
  }

  Instruction *I = dyn_cast<Instruction>(V);
  if (!I) return nullptr;        // Only analyze instructions.

  bool MadeChange = false;
  auto simplifyAndSetOp = [&](Instruction *Inst, unsigned OpNum,
                              APInt Demanded, APInt &Undef) {
    auto *II = dyn_cast<IntrinsicInst>(Inst);
    Value *Op = II ? II->getArgOperand(OpNum) : Inst->getOperand(OpNum);
    if (Value *V = SimplifyDemandedVectorElts(Op, Demanded, Undef, Depth + 1)) {
      if (II)
        II->setArgOperand(OpNum, V);
      else
        Inst->setOperand(OpNum, V);
      MadeChange = true;
    }
  };

  APInt UndefElts2(VWidth, 0);
  APInt UndefElts3(VWidth, 0);
  switch (I->getOpcode()) {
  default: break;

  case Instruction::InsertElement: {
    // If this is a variable index, we don't know which element it overwrites.
    // demand exactly the same input as we produce.
    ConstantInt *Idx = dyn_cast<ConstantInt>(I->getOperand(2));
    if (!Idx) {
      // Note that we can't propagate undef elt info, because we don't know
      // which elt is getting updated.
      simplifyAndSetOp(I, 0, DemandedElts, UndefElts2);
      break;
    }

    // The element inserted overwrites whatever was there, so the input demanded
    // set is simpler than the output set.
    unsigned IdxNo = Idx->getZExtValue();
    APInt PreInsertDemandedElts = DemandedElts;
    if (IdxNo < VWidth)
      PreInsertDemandedElts.clearBit(IdxNo);

    simplifyAndSetOp(I, 0, PreInsertDemandedElts, UndefElts);

    // If this is inserting an element that isn't demanded, remove this
    // insertelement.
    if (IdxNo >= VWidth || !DemandedElts[IdxNo]) {
      Worklist.Add(I);
      return I->getOperand(0);
    }

    // The inserted element is defined.
    UndefElts.clearBit(IdxNo);
    break;
  }
  case Instruction::ShuffleVector: {
    ShuffleVectorInst *Shuffle = cast<ShuffleVectorInst>(I);
    unsigned LHSVWidth =
      Shuffle->getOperand(0)->getType()->getVectorNumElements();
    APInt LeftDemanded(LHSVWidth, 0), RightDemanded(LHSVWidth, 0);
    for (unsigned i = 0; i < VWidth; i++) {
      if (DemandedElts[i]) {
        unsigned MaskVal = Shuffle->getMaskValue(i);
        if (MaskVal != -1u) {
          assert(MaskVal < LHSVWidth * 2 &&
                 "shufflevector mask index out of range!");
          if (MaskVal < LHSVWidth)
            LeftDemanded.setBit(MaskVal);
          else
            RightDemanded.setBit(MaskVal - LHSVWidth);
        }
      }
    }

    APInt LHSUndefElts(LHSVWidth, 0);
    simplifyAndSetOp(I, 0, LeftDemanded, LHSUndefElts);

    APInt RHSUndefElts(LHSVWidth, 0);
    simplifyAndSetOp(I, 1, RightDemanded, RHSUndefElts);

    bool NewUndefElts = false;
    unsigned LHSIdx = -1u, LHSValIdx = -1u;
    unsigned RHSIdx = -1u, RHSValIdx = -1u;
    bool LHSUniform = true;
    bool RHSUniform = true;
    for (unsigned i = 0; i < VWidth; i++) {
      unsigned MaskVal = Shuffle->getMaskValue(i);
      if (MaskVal == -1u) {
        UndefElts.setBit(i);
      } else if (!DemandedElts[i]) {
        NewUndefElts = true;
        UndefElts.setBit(i);
      } else if (MaskVal < LHSVWidth) {
        if (LHSUndefElts[MaskVal]) {
          NewUndefElts = true;
          UndefElts.setBit(i);
        } else {
          LHSIdx = LHSIdx == -1u ? i : LHSVWidth;
          LHSValIdx = LHSValIdx == -1u ? MaskVal : LHSVWidth;
          LHSUniform = LHSUniform && (MaskVal == i);
        }
      } else {
        if (RHSUndefElts[MaskVal - LHSVWidth]) {
          NewUndefElts = true;
          UndefElts.setBit(i);
        } else {
          RHSIdx = RHSIdx == -1u ? i : LHSVWidth;
          RHSValIdx = RHSValIdx == -1u ? MaskVal - LHSVWidth : LHSVWidth;
          RHSUniform = RHSUniform && (MaskVal - LHSVWidth == i);
        }
      }
    }

    // Try to transform shuffle with constant vector and single element from
    // this constant vector to single insertelement instruction.
    // shufflevector V, C, <v1, v2, .., ci, .., vm> ->
    // insertelement V, C[ci], ci-n
    if (LHSVWidth == Shuffle->getType()->getNumElements()) {
      Value *Op = nullptr;
      Constant *Value = nullptr;
      unsigned Idx = -1u;

      // Find constant vector with the single element in shuffle (LHS or RHS).
      if (LHSIdx < LHSVWidth && RHSUniform) {
        if (auto *CV = dyn_cast<ConstantVector>(Shuffle->getOperand(0))) {
          Op = Shuffle->getOperand(1);
          Value = CV->getOperand(LHSValIdx);
          Idx = LHSIdx;
        }
      }
      if (RHSIdx < LHSVWidth && LHSUniform) {
        if (auto *CV = dyn_cast<ConstantVector>(Shuffle->getOperand(1))) {
          Op = Shuffle->getOperand(0);
          Value = CV->getOperand(RHSValIdx);
          Idx = RHSIdx;
        }
      }
      // Found constant vector with single element - convert to insertelement.
      if (Op && Value) {
        Instruction *New = InsertElementInst::Create(
            Op, Value, ConstantInt::get(Type::getInt32Ty(I->getContext()), Idx),
            Shuffle->getName());
        InsertNewInstWith(New, *Shuffle);
        return New;
      }
    }
    if (NewUndefElts) {
      // Add additional discovered undefs.
      SmallVector<Constant*, 16> Elts;
      for (unsigned i = 0; i < VWidth; ++i) {
        if (UndefElts[i])
          Elts.push_back(UndefValue::get(Type::getInt32Ty(I->getContext())));
        else
          Elts.push_back(ConstantInt::get(Type::getInt32Ty(I->getContext()),
                                          Shuffle->getMaskValue(i)));
      }
      I->setOperand(2, ConstantVector::get(Elts));
      MadeChange = true;
    }
    break;
  }
  case Instruction::Select: {
    // If this is a vector select, try to transform the select condition based
    // on the current demanded elements.
    SelectInst *Sel = cast<SelectInst>(I);
    if (Sel->getCondition()->getType()->isVectorTy()) {
      // TODO: We are not doing anything with UndefElts based on this call.
      // It is overwritten below based on the other select operands. If an
      // element of the select condition is known undef, then we are free to
      // choose the output value from either arm of the select. If we know that
      // one of those values is undef, then the output can be undef.
      simplifyAndSetOp(I, 0, DemandedElts, UndefElts);
    }

    // Next, see if we can transform the arms of the select.
    APInt DemandedLHS(DemandedElts), DemandedRHS(DemandedElts);
    if (auto *CV = dyn_cast<ConstantVector>(Sel->getCondition())) {
      for (unsigned i = 0; i < VWidth; i++) {
        // isNullValue() always returns false when called on a ConstantExpr.
        // Skip constant expressions to avoid propagating incorrect information.
        Constant *CElt = CV->getAggregateElement(i);
        if (isa<ConstantExpr>(CElt))
          continue;
        // TODO: If a select condition element is undef, we can demand from
        // either side. If one side is known undef, choosing that side would
        // propagate undef.
        if (CElt->isNullValue())
          DemandedLHS.clearBit(i);
        else
          DemandedRHS.clearBit(i);
      }
    }

    simplifyAndSetOp(I, 1, DemandedLHS, UndefElts2);
    simplifyAndSetOp(I, 2, DemandedRHS, UndefElts3);

    // Output elements are undefined if the element from each arm is undefined.
    // TODO: This can be improved. See comment in select condition handling.
    UndefElts = UndefElts2 & UndefElts3;
    break;
  }
  case Instruction::BitCast: {
    // Vector->vector casts only.
    VectorType *VTy = dyn_cast<VectorType>(I->getOperand(0)->getType());
    if (!VTy) break;
    unsigned InVWidth = VTy->getNumElements();
    APInt InputDemandedElts(InVWidth, 0);
    UndefElts2 = APInt(InVWidth, 0);
    unsigned Ratio;

    if (VWidth == InVWidth) {
      // If we are converting from <4 x i32> -> <4 x f32>, we demand the same
      // elements as are demanded of us.
      Ratio = 1;
      InputDemandedElts = DemandedElts;
    } else if ((VWidth % InVWidth) == 0) {
      // If the number of elements in the output is a multiple of the number of
      // elements in the input then an input element is live if any of the
      // corresponding output elements are live.
      Ratio = VWidth / InVWidth;
      for (unsigned OutIdx = 0; OutIdx != VWidth; ++OutIdx)
        if (DemandedElts[OutIdx])
          InputDemandedElts.setBit(OutIdx / Ratio);
    } else if ((InVWidth % VWidth) == 0) {
      // If the number of elements in the input is a multiple of the number of
      // elements in the output then an input element is live if the
      // corresponding output element is live.
      Ratio = InVWidth / VWidth;
      for (unsigned InIdx = 0; InIdx != InVWidth; ++InIdx)
        if (DemandedElts[InIdx / Ratio])
          InputDemandedElts.setBit(InIdx);
    } else {
      // Unsupported so far.
      break;
    }

    simplifyAndSetOp(I, 0, InputDemandedElts, UndefElts2);

    if (VWidth == InVWidth) {
      UndefElts = UndefElts2;
    } else if ((VWidth % InVWidth) == 0) {
      // If the number of elements in the output is a multiple of the number of
      // elements in the input then an output element is undef if the
      // corresponding input element is undef.
      for (unsigned OutIdx = 0; OutIdx != VWidth; ++OutIdx)
        if (UndefElts2[OutIdx / Ratio])
          UndefElts.setBit(OutIdx);
    } else if ((InVWidth % VWidth) == 0) {
      // If the number of elements in the input is a multiple of the number of
      // elements in the output then an output element is undef if all of the
      // corresponding input elements are undef.
      for (unsigned OutIdx = 0; OutIdx != VWidth; ++OutIdx) {
        APInt SubUndef = UndefElts2.lshr(OutIdx * Ratio).zextOrTrunc(Ratio);
        if (SubUndef.countPopulation() == Ratio)
          UndefElts.setBit(OutIdx);
      }
    } else {
      llvm_unreachable("Unimp");
    }
    break;
  }
  case Instruction::FPTrunc:
  case Instruction::FPExt:
    simplifyAndSetOp(I, 0, DemandedElts, UndefElts);
    break;

  case Instruction::Call: {
    IntrinsicInst *II = dyn_cast<IntrinsicInst>(I);
    if (!II) break;
    switch (II->getIntrinsicID()) {
    case Intrinsic::x86_xop_vfrcz_ss:
    case Intrinsic::x86_xop_vfrcz_sd:
      // The instructions for these intrinsics are speced to zero upper bits not
      // pass them through like other scalar intrinsics. So we shouldn't just
      // use Arg0 if DemandedElts[0] is clear like we do for other intrinsics.
      // Instead we should return a zero vector.
      if (!DemandedElts[0]) {
        Worklist.Add(II);
        return ConstantAggregateZero::get(II->getType());
      }

      // Only the lower element is used.
      DemandedElts = 1;
      simplifyAndSetOp(II, 0, DemandedElts, UndefElts);

      // Only the lower element is undefined. The high elements are zero.
      UndefElts = UndefElts[0];
      break;

    // Unary scalar-as-vector operations that work column-wise.
    case Intrinsic::x86_sse_rcp_ss:
    case Intrinsic::x86_sse_rsqrt_ss:
      simplifyAndSetOp(II, 0, DemandedElts, UndefElts);

      // If lowest element of a scalar op isn't used then use Arg0.
      if (!DemandedElts[0]) {
        Worklist.Add(II);
        return II->getArgOperand(0);
      }
      // TODO: If only low elt lower SQRT to FSQRT (with rounding/exceptions
      // checks).
      break;

    // Binary scalar-as-vector operations that work column-wise. The high
    // elements come from operand 0. The low element is a function of both
    // operands.
    case Intrinsic::x86_sse_min_ss:
    case Intrinsic::x86_sse_max_ss:
    case Intrinsic::x86_sse_cmp_ss:
    case Intrinsic::x86_sse2_min_sd:
    case Intrinsic::x86_sse2_max_sd:
    case Intrinsic::x86_sse2_cmp_sd: {
      simplifyAndSetOp(II, 0, DemandedElts, UndefElts);

      // If lowest element of a scalar op isn't used then use Arg0.
      if (!DemandedElts[0]) {
        Worklist.Add(II);
        return II->getArgOperand(0);
      }

      // Only lower element is used for operand 1.
      DemandedElts = 1;
      simplifyAndSetOp(II, 1, DemandedElts, UndefElts2);

      // Lower element is undefined if both lower elements are undefined.
      // Consider things like undef&0.  The result is known zero, not undef.
      if (!UndefElts2[0])
        UndefElts.clearBit(0);

      break;
    }

    // Binary scalar-as-vector operations that work column-wise. The high
    // elements come from operand 0 and the low element comes from operand 1.
    case Intrinsic::x86_sse41_round_ss:
    case Intrinsic::x86_sse41_round_sd: {
      // Don't use the low element of operand 0.
      APInt DemandedElts2 = DemandedElts;
      DemandedElts2.clearBit(0);
      simplifyAndSetOp(II, 0, DemandedElts2, UndefElts);

      // If lowest element of a scalar op isn't used then use Arg0.
      if (!DemandedElts[0]) {
        Worklist.Add(II);
        return II->getArgOperand(0);
      }

      // Only lower element is used for operand 1.
      DemandedElts = 1;
      simplifyAndSetOp(II, 1, DemandedElts, UndefElts2);

      // Take the high undef elements from operand 0 and take the lower element
      // from operand 1.
      UndefElts.clearBit(0);
      UndefElts |= UndefElts2[0];
      break;
    }

    // Three input scalar-as-vector operations that work column-wise. The high
    // elements come from operand 0 and the low element is a function of all
    // three inputs.
    case Intrinsic::x86_avx512_mask_add_ss_round:
    case Intrinsic::x86_avx512_mask_div_ss_round:
    case Intrinsic::x86_avx512_mask_mul_ss_round:
    case Intrinsic::x86_avx512_mask_sub_ss_round:
    case Intrinsic::x86_avx512_mask_max_ss_round:
    case Intrinsic::x86_avx512_mask_min_ss_round:
    case Intrinsic::x86_avx512_mask_add_sd_round:
    case Intrinsic::x86_avx512_mask_div_sd_round:
    case Intrinsic::x86_avx512_mask_mul_sd_round:
    case Intrinsic::x86_avx512_mask_sub_sd_round:
    case Intrinsic::x86_avx512_mask_max_sd_round:
    case Intrinsic::x86_avx512_mask_min_sd_round:
      simplifyAndSetOp(II, 0, DemandedElts, UndefElts);

      // If lowest element of a scalar op isn't used then use Arg0.
      if (!DemandedElts[0]) {
        Worklist.Add(II);
        return II->getArgOperand(0);
      }

      // Only lower element is used for operand 1 and 2.
      DemandedElts = 1;
      simplifyAndSetOp(II, 1, DemandedElts, UndefElts2);
      simplifyAndSetOp(II, 2, DemandedElts, UndefElts3);

      // Lower element is undefined if all three lower elements are undefined.
      // Consider things like undef&0.  The result is known zero, not undef.
      if (!UndefElts2[0] || !UndefElts3[0])
        UndefElts.clearBit(0);

      break;

    case Intrinsic::x86_sse2_packssdw_128:
    case Intrinsic::x86_sse2_packsswb_128:
    case Intrinsic::x86_sse2_packuswb_128:
    case Intrinsic::x86_sse41_packusdw:
    case Intrinsic::x86_avx2_packssdw:
    case Intrinsic::x86_avx2_packsswb:
    case Intrinsic::x86_avx2_packusdw:
    case Intrinsic::x86_avx2_packuswb:
    case Intrinsic::x86_avx512_packssdw_512:
    case Intrinsic::x86_avx512_packsswb_512:
    case Intrinsic::x86_avx512_packusdw_512:
    case Intrinsic::x86_avx512_packuswb_512: {
      auto *Ty0 = II->getArgOperand(0)->getType();
      unsigned InnerVWidth = Ty0->getVectorNumElements();
      assert(VWidth == (InnerVWidth * 2) && "Unexpected input size");

      unsigned NumLanes = Ty0->getPrimitiveSizeInBits() / 128;
      unsigned VWidthPerLane = VWidth / NumLanes;
      unsigned InnerVWidthPerLane = InnerVWidth / NumLanes;

      // Per lane, pack the elements of the first input and then the second.
      // e.g.
      // v8i16 PACK(v4i32 X, v4i32 Y) - (X[0..3],Y[0..3])
      // v32i8 PACK(v16i16 X, v16i16 Y) - (X[0..7],Y[0..7]),(X[8..15],Y[8..15])
      for (int OpNum = 0; OpNum != 2; ++OpNum) {
        APInt OpDemandedElts(InnerVWidth, 0);
        for (unsigned Lane = 0; Lane != NumLanes; ++Lane) {
          unsigned LaneIdx = Lane * VWidthPerLane;
          for (unsigned Elt = 0; Elt != InnerVWidthPerLane; ++Elt) {
            unsigned Idx = LaneIdx + Elt + InnerVWidthPerLane * OpNum;
            if (DemandedElts[Idx])
              OpDemandedElts.setBit((Lane * InnerVWidthPerLane) + Elt);
          }
        }

        // Demand elements from the operand.
        APInt OpUndefElts(InnerVWidth, 0);
        simplifyAndSetOp(II, OpNum, OpDemandedElts, OpUndefElts);

        // Pack the operand's UNDEF elements, one lane at a time.
        OpUndefElts = OpUndefElts.zext(VWidth);
        for (unsigned Lane = 0; Lane != NumLanes; ++Lane) {
          APInt LaneElts = OpUndefElts.lshr(InnerVWidthPerLane * Lane);
          LaneElts = LaneElts.getLoBits(InnerVWidthPerLane);
          LaneElts <<= InnerVWidthPerLane * (2 * Lane + OpNum);
          UndefElts |= LaneElts;
        }
      }
      break;
    }

    // PSHUFB
    case Intrinsic::x86_ssse3_pshuf_b_128:
    case Intrinsic::x86_avx2_pshuf_b:
    case Intrinsic::x86_avx512_pshuf_b_512:
    // PERMILVAR
    case Intrinsic::x86_avx_vpermilvar_ps:
    case Intrinsic::x86_avx_vpermilvar_ps_256:
    case Intrinsic::x86_avx512_vpermilvar_ps_512:
    case Intrinsic::x86_avx_vpermilvar_pd:
    case Intrinsic::x86_avx_vpermilvar_pd_256:
    case Intrinsic::x86_avx512_vpermilvar_pd_512:
    // PERMV
    case Intrinsic::x86_avx2_permd:
    case Intrinsic::x86_avx2_permps: {
      simplifyAndSetOp(II, 1, DemandedElts, UndefElts);
      break;
    }

    // SSE4A instructions leave the upper 64-bits of the 128-bit result
    // in an undefined state.
    case Intrinsic::x86_sse4a_extrq:
    case Intrinsic::x86_sse4a_extrqi:
    case Intrinsic::x86_sse4a_insertq:
    case Intrinsic::x86_sse4a_insertqi:
      UndefElts.setHighBits(VWidth / 2);
      break;
    case Intrinsic::amdgcn_buffer_load:
    case Intrinsic::amdgcn_buffer_load_format:
    case Intrinsic::amdgcn_raw_buffer_load:
    case Intrinsic::amdgcn_raw_buffer_load_format:
    case Intrinsic::amdgcn_struct_buffer_load:
    case Intrinsic::amdgcn_struct_buffer_load_format:
      return simplifyAMDGCNMemoryIntrinsicDemanded(II, DemandedElts);
    default: {
      if (getAMDGPUImageDMaskIntrinsic(II->getIntrinsicID()))
        return simplifyAMDGCNMemoryIntrinsicDemanded(
            II, DemandedElts, 0, II->getNumArgOperands() - 2);

      break;
    }
    } // switch on IntrinsicID
    break;
  } // case Call
  } // switch on Opcode

  // TODO: We bail completely on integer div/rem and shifts because they have
  // UB/poison potential, but that should be refined.
  BinaryOperator *BO;
  if (match(I, m_BinOp(BO)) && !BO->isIntDivRem() && !BO->isShift()) {
    simplifyAndSetOp(I, 0, DemandedElts, UndefElts);
    simplifyAndSetOp(I, 1, DemandedElts, UndefElts2);

    // Any change to an instruction with potential poison must clear those flags
    // because we can not guarantee those constraints now. Other analysis may
    // determine that it is safe to re-apply the flags.
    if (MadeChange)
      BO->dropPoisonGeneratingFlags();

    // Output elements are undefined if both are undefined. Consider things
    // like undef & 0. The result is known zero, not undef.
    UndefElts &= UndefElts2;
  }

  return MadeChange ? I : nullptr;
}
