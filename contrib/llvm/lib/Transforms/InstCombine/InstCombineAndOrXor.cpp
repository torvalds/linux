//===- InstCombineAndOrXor.cpp --------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the visitAnd, visitOr, and visitXor functions.
//
//===----------------------------------------------------------------------===//

#include "InstCombineInternal.h"
#include "llvm/Analysis/CmpInstAnalysis.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/PatternMatch.h"
using namespace llvm;
using namespace PatternMatch;

#define DEBUG_TYPE "instcombine"

/// Similar to getICmpCode but for FCmpInst. This encodes a fcmp predicate into
/// a four bit mask.
static unsigned getFCmpCode(FCmpInst::Predicate CC) {
  assert(FCmpInst::FCMP_FALSE <= CC && CC <= FCmpInst::FCMP_TRUE &&
         "Unexpected FCmp predicate!");
  // Take advantage of the bit pattern of FCmpInst::Predicate here.
  //                                                 U L G E
  static_assert(FCmpInst::FCMP_FALSE ==  0, "");  // 0 0 0 0
  static_assert(FCmpInst::FCMP_OEQ   ==  1, "");  // 0 0 0 1
  static_assert(FCmpInst::FCMP_OGT   ==  2, "");  // 0 0 1 0
  static_assert(FCmpInst::FCMP_OGE   ==  3, "");  // 0 0 1 1
  static_assert(FCmpInst::FCMP_OLT   ==  4, "");  // 0 1 0 0
  static_assert(FCmpInst::FCMP_OLE   ==  5, "");  // 0 1 0 1
  static_assert(FCmpInst::FCMP_ONE   ==  6, "");  // 0 1 1 0
  static_assert(FCmpInst::FCMP_ORD   ==  7, "");  // 0 1 1 1
  static_assert(FCmpInst::FCMP_UNO   ==  8, "");  // 1 0 0 0
  static_assert(FCmpInst::FCMP_UEQ   ==  9, "");  // 1 0 0 1
  static_assert(FCmpInst::FCMP_UGT   == 10, "");  // 1 0 1 0
  static_assert(FCmpInst::FCMP_UGE   == 11, "");  // 1 0 1 1
  static_assert(FCmpInst::FCMP_ULT   == 12, "");  // 1 1 0 0
  static_assert(FCmpInst::FCMP_ULE   == 13, "");  // 1 1 0 1
  static_assert(FCmpInst::FCMP_UNE   == 14, "");  // 1 1 1 0
  static_assert(FCmpInst::FCMP_TRUE  == 15, "");  // 1 1 1 1
  return CC;
}

/// This is the complement of getICmpCode, which turns an opcode and two
/// operands into either a constant true or false, or a brand new ICmp
/// instruction. The sign is passed in to determine which kind of predicate to
/// use in the new icmp instruction.
static Value *getNewICmpValue(unsigned Code, bool Sign, Value *LHS, Value *RHS,
                              InstCombiner::BuilderTy &Builder) {
  ICmpInst::Predicate NewPred;
  if (Constant *TorF = getPredForICmpCode(Code, Sign, LHS->getType(), NewPred))
    return TorF;
  return Builder.CreateICmp(NewPred, LHS, RHS);
}

/// This is the complement of getFCmpCode, which turns an opcode and two
/// operands into either a FCmp instruction, or a true/false constant.
static Value *getFCmpValue(unsigned Code, Value *LHS, Value *RHS,
                           InstCombiner::BuilderTy &Builder) {
  const auto Pred = static_cast<FCmpInst::Predicate>(Code);
  assert(FCmpInst::FCMP_FALSE <= Pred && Pred <= FCmpInst::FCMP_TRUE &&
         "Unexpected FCmp predicate!");
  if (Pred == FCmpInst::FCMP_FALSE)
    return ConstantInt::get(CmpInst::makeCmpResultType(LHS->getType()), 0);
  if (Pred == FCmpInst::FCMP_TRUE)
    return ConstantInt::get(CmpInst::makeCmpResultType(LHS->getType()), 1);
  return Builder.CreateFCmp(Pred, LHS, RHS);
}

/// Transform BITWISE_OP(BSWAP(A),BSWAP(B)) or
/// BITWISE_OP(BSWAP(A), Constant) to BSWAP(BITWISE_OP(A, B))
/// \param I Binary operator to transform.
/// \return Pointer to node that must replace the original binary operator, or
///         null pointer if no transformation was made.
static Value *SimplifyBSwap(BinaryOperator &I,
                            InstCombiner::BuilderTy &Builder) {
  assert(I.isBitwiseLogicOp() && "Unexpected opcode for bswap simplifying");

  Value *OldLHS = I.getOperand(0);
  Value *OldRHS = I.getOperand(1);

  Value *NewLHS;
  if (!match(OldLHS, m_BSwap(m_Value(NewLHS))))
    return nullptr;

  Value *NewRHS;
  const APInt *C;

  if (match(OldRHS, m_BSwap(m_Value(NewRHS)))) {
    // OP( BSWAP(x), BSWAP(y) ) -> BSWAP( OP(x, y) )
    if (!OldLHS->hasOneUse() && !OldRHS->hasOneUse())
      return nullptr;
    // NewRHS initialized by the matcher.
  } else if (match(OldRHS, m_APInt(C))) {
    // OP( BSWAP(x), CONSTANT ) -> BSWAP( OP(x, BSWAP(CONSTANT) ) )
    if (!OldLHS->hasOneUse())
      return nullptr;
    NewRHS = ConstantInt::get(I.getType(), C->byteSwap());
  } else
    return nullptr;

  Value *BinOp = Builder.CreateBinOp(I.getOpcode(), NewLHS, NewRHS);
  Function *F = Intrinsic::getDeclaration(I.getModule(), Intrinsic::bswap,
                                          I.getType());
  return Builder.CreateCall(F, BinOp);
}

/// This handles expressions of the form ((val OP C1) & C2).  Where
/// the Op parameter is 'OP', OpRHS is 'C1', and AndRHS is 'C2'.
Instruction *InstCombiner::OptAndOp(BinaryOperator *Op,
                                    ConstantInt *OpRHS,
                                    ConstantInt *AndRHS,
                                    BinaryOperator &TheAnd) {
  Value *X = Op->getOperand(0);

  switch (Op->getOpcode()) {
  default: break;
  case Instruction::Add:
    if (Op->hasOneUse()) {
      // Adding a one to a single bit bit-field should be turned into an XOR
      // of the bit.  First thing to check is to see if this AND is with a
      // single bit constant.
      const APInt &AndRHSV = AndRHS->getValue();

      // If there is only one bit set.
      if (AndRHSV.isPowerOf2()) {
        // Ok, at this point, we know that we are masking the result of the
        // ADD down to exactly one bit.  If the constant we are adding has
        // no bits set below this bit, then we can eliminate the ADD.
        const APInt& AddRHS = OpRHS->getValue();

        // Check to see if any bits below the one bit set in AndRHSV are set.
        if ((AddRHS & (AndRHSV - 1)).isNullValue()) {
          // If not, the only thing that can effect the output of the AND is
          // the bit specified by AndRHSV.  If that bit is set, the effect of
          // the XOR is to toggle the bit.  If it is clear, then the ADD has
          // no effect.
          if ((AddRHS & AndRHSV).isNullValue()) { // Bit is not set, noop
            TheAnd.setOperand(0, X);
            return &TheAnd;
          } else {
            // Pull the XOR out of the AND.
            Value *NewAnd = Builder.CreateAnd(X, AndRHS);
            NewAnd->takeName(Op);
            return BinaryOperator::CreateXor(NewAnd, AndRHS);
          }
        }
      }
    }
    break;
  }
  return nullptr;
}

/// Emit a computation of: (V >= Lo && V < Hi) if Inside is true, otherwise
/// (V < Lo || V >= Hi). This method expects that Lo <= Hi. IsSigned indicates
/// whether to treat V, Lo, and Hi as signed or not.
Value *InstCombiner::insertRangeTest(Value *V, const APInt &Lo, const APInt &Hi,
                                     bool isSigned, bool Inside) {
  assert((isSigned ? Lo.sle(Hi) : Lo.ule(Hi)) &&
         "Lo is not <= Hi in range emission code!");

  Type *Ty = V->getType();
  if (Lo == Hi)
    return Inside ? ConstantInt::getFalse(Ty) : ConstantInt::getTrue(Ty);

  // V >= Min && V <  Hi --> V <  Hi
  // V <  Min || V >= Hi --> V >= Hi
  ICmpInst::Predicate Pred = Inside ? ICmpInst::ICMP_ULT : ICmpInst::ICMP_UGE;
  if (isSigned ? Lo.isMinSignedValue() : Lo.isMinValue()) {
    Pred = isSigned ? ICmpInst::getSignedPredicate(Pred) : Pred;
    return Builder.CreateICmp(Pred, V, ConstantInt::get(Ty, Hi));
  }

  // V >= Lo && V <  Hi --> V - Lo u<  Hi - Lo
  // V <  Lo || V >= Hi --> V - Lo u>= Hi - Lo
  Value *VMinusLo =
      Builder.CreateSub(V, ConstantInt::get(Ty, Lo), V->getName() + ".off");
  Constant *HiMinusLo = ConstantInt::get(Ty, Hi - Lo);
  return Builder.CreateICmp(Pred, VMinusLo, HiMinusLo);
}

/// Classify (icmp eq (A & B), C) and (icmp ne (A & B), C) as matching patterns
/// that can be simplified.
/// One of A and B is considered the mask. The other is the value. This is
/// described as the "AMask" or "BMask" part of the enum. If the enum contains
/// only "Mask", then both A and B can be considered masks. If A is the mask,
/// then it was proven that (A & C) == C. This is trivial if C == A or C == 0.
/// If both A and C are constants, this proof is also easy.
/// For the following explanations, we assume that A is the mask.
///
/// "AllOnes" declares that the comparison is true only if (A & B) == A or all
/// bits of A are set in B.
///   Example: (icmp eq (A & 3), 3) -> AMask_AllOnes
///
/// "AllZeros" declares that the comparison is true only if (A & B) == 0 or all
/// bits of A are cleared in B.
///   Example: (icmp eq (A & 3), 0) -> Mask_AllZeroes
///
/// "Mixed" declares that (A & B) == C and C might or might not contain any
/// number of one bits and zero bits.
///   Example: (icmp eq (A & 3), 1) -> AMask_Mixed
///
/// "Not" means that in above descriptions "==" should be replaced by "!=".
///   Example: (icmp ne (A & 3), 3) -> AMask_NotAllOnes
///
/// If the mask A contains a single bit, then the following is equivalent:
///    (icmp eq (A & B), A) equals (icmp ne (A & B), 0)
///    (icmp ne (A & B), A) equals (icmp eq (A & B), 0)
enum MaskedICmpType {
  AMask_AllOnes           =     1,
  AMask_NotAllOnes        =     2,
  BMask_AllOnes           =     4,
  BMask_NotAllOnes        =     8,
  Mask_AllZeros           =    16,
  Mask_NotAllZeros        =    32,
  AMask_Mixed             =    64,
  AMask_NotMixed          =   128,
  BMask_Mixed             =   256,
  BMask_NotMixed          =   512
};

/// Return the set of patterns (from MaskedICmpType) that (icmp SCC (A & B), C)
/// satisfies.
static unsigned getMaskedICmpType(Value *A, Value *B, Value *C,
                                  ICmpInst::Predicate Pred) {
  ConstantInt *ACst = dyn_cast<ConstantInt>(A);
  ConstantInt *BCst = dyn_cast<ConstantInt>(B);
  ConstantInt *CCst = dyn_cast<ConstantInt>(C);
  bool IsEq = (Pred == ICmpInst::ICMP_EQ);
  bool IsAPow2 = (ACst && !ACst->isZero() && ACst->getValue().isPowerOf2());
  bool IsBPow2 = (BCst && !BCst->isZero() && BCst->getValue().isPowerOf2());
  unsigned MaskVal = 0;
  if (CCst && CCst->isZero()) {
    // if C is zero, then both A and B qualify as mask
    MaskVal |= (IsEq ? (Mask_AllZeros | AMask_Mixed | BMask_Mixed)
                     : (Mask_NotAllZeros | AMask_NotMixed | BMask_NotMixed));
    if (IsAPow2)
      MaskVal |= (IsEq ? (AMask_NotAllOnes | AMask_NotMixed)
                       : (AMask_AllOnes | AMask_Mixed));
    if (IsBPow2)
      MaskVal |= (IsEq ? (BMask_NotAllOnes | BMask_NotMixed)
                       : (BMask_AllOnes | BMask_Mixed));
    return MaskVal;
  }

  if (A == C) {
    MaskVal |= (IsEq ? (AMask_AllOnes | AMask_Mixed)
                     : (AMask_NotAllOnes | AMask_NotMixed));
    if (IsAPow2)
      MaskVal |= (IsEq ? (Mask_NotAllZeros | AMask_NotMixed)
                       : (Mask_AllZeros | AMask_Mixed));
  } else if (ACst && CCst && ConstantExpr::getAnd(ACst, CCst) == CCst) {
    MaskVal |= (IsEq ? AMask_Mixed : AMask_NotMixed);
  }

  if (B == C) {
    MaskVal |= (IsEq ? (BMask_AllOnes | BMask_Mixed)
                     : (BMask_NotAllOnes | BMask_NotMixed));
    if (IsBPow2)
      MaskVal |= (IsEq ? (Mask_NotAllZeros | BMask_NotMixed)
                       : (Mask_AllZeros | BMask_Mixed));
  } else if (BCst && CCst && ConstantExpr::getAnd(BCst, CCst) == CCst) {
    MaskVal |= (IsEq ? BMask_Mixed : BMask_NotMixed);
  }

  return MaskVal;
}

/// Convert an analysis of a masked ICmp into its equivalent if all boolean
/// operations had the opposite sense. Since each "NotXXX" flag (recording !=)
/// is adjacent to the corresponding normal flag (recording ==), this just
/// involves swapping those bits over.
static unsigned conjugateICmpMask(unsigned Mask) {
  unsigned NewMask;
  NewMask = (Mask & (AMask_AllOnes | BMask_AllOnes | Mask_AllZeros |
                     AMask_Mixed | BMask_Mixed))
            << 1;

  NewMask |= (Mask & (AMask_NotAllOnes | BMask_NotAllOnes | Mask_NotAllZeros |
                      AMask_NotMixed | BMask_NotMixed))
             >> 1;

  return NewMask;
}

// Adapts the external decomposeBitTestICmp for local use.
static bool decomposeBitTestICmp(Value *LHS, Value *RHS, CmpInst::Predicate &Pred,
                                 Value *&X, Value *&Y, Value *&Z) {
  APInt Mask;
  if (!llvm::decomposeBitTestICmp(LHS, RHS, Pred, X, Mask))
    return false;

  Y = ConstantInt::get(X->getType(), Mask);
  Z = ConstantInt::get(X->getType(), 0);
  return true;
}

/// Handle (icmp(A & B) ==/!= C) &/| (icmp(A & D) ==/!= E).
/// Return the pattern classes (from MaskedICmpType) for the left hand side and
/// the right hand side as a pair.
/// LHS and RHS are the left hand side and the right hand side ICmps and PredL
/// and PredR are their predicates, respectively.
static
Optional<std::pair<unsigned, unsigned>>
getMaskedTypeForICmpPair(Value *&A, Value *&B, Value *&C,
                         Value *&D, Value *&E, ICmpInst *LHS,
                         ICmpInst *RHS,
                         ICmpInst::Predicate &PredL,
                         ICmpInst::Predicate &PredR) {
  // vectors are not (yet?) supported. Don't support pointers either.
  if (!LHS->getOperand(0)->getType()->isIntegerTy() ||
      !RHS->getOperand(0)->getType()->isIntegerTy())
    return None;

  // Here comes the tricky part:
  // LHS might be of the form L11 & L12 == X, X == L21 & L22,
  // and L11 & L12 == L21 & L22. The same goes for RHS.
  // Now we must find those components L** and R**, that are equal, so
  // that we can extract the parameters A, B, C, D, and E for the canonical
  // above.
  Value *L1 = LHS->getOperand(0);
  Value *L2 = LHS->getOperand(1);
  Value *L11, *L12, *L21, *L22;
  // Check whether the icmp can be decomposed into a bit test.
  if (decomposeBitTestICmp(L1, L2, PredL, L11, L12, L2)) {
    L21 = L22 = L1 = nullptr;
  } else {
    // Look for ANDs in the LHS icmp.
    if (!match(L1, m_And(m_Value(L11), m_Value(L12)))) {
      // Any icmp can be viewed as being trivially masked; if it allows us to
      // remove one, it's worth it.
      L11 = L1;
      L12 = Constant::getAllOnesValue(L1->getType());
    }

    if (!match(L2, m_And(m_Value(L21), m_Value(L22)))) {
      L21 = L2;
      L22 = Constant::getAllOnesValue(L2->getType());
    }
  }

  // Bail if LHS was a icmp that can't be decomposed into an equality.
  if (!ICmpInst::isEquality(PredL))
    return None;

  Value *R1 = RHS->getOperand(0);
  Value *R2 = RHS->getOperand(1);
  Value *R11, *R12;
  bool Ok = false;
  if (decomposeBitTestICmp(R1, R2, PredR, R11, R12, R2)) {
    if (R11 == L11 || R11 == L12 || R11 == L21 || R11 == L22) {
      A = R11;
      D = R12;
    } else if (R12 == L11 || R12 == L12 || R12 == L21 || R12 == L22) {
      A = R12;
      D = R11;
    } else {
      return None;
    }
    E = R2;
    R1 = nullptr;
    Ok = true;
  } else {
    if (!match(R1, m_And(m_Value(R11), m_Value(R12)))) {
      // As before, model no mask as a trivial mask if it'll let us do an
      // optimization.
      R11 = R1;
      R12 = Constant::getAllOnesValue(R1->getType());
    }

    if (R11 == L11 || R11 == L12 || R11 == L21 || R11 == L22) {
      A = R11;
      D = R12;
      E = R2;
      Ok = true;
    } else if (R12 == L11 || R12 == L12 || R12 == L21 || R12 == L22) {
      A = R12;
      D = R11;
      E = R2;
      Ok = true;
    }
  }

  // Bail if RHS was a icmp that can't be decomposed into an equality.
  if (!ICmpInst::isEquality(PredR))
    return None;

  // Look for ANDs on the right side of the RHS icmp.
  if (!Ok) {
    if (!match(R2, m_And(m_Value(R11), m_Value(R12)))) {
      R11 = R2;
      R12 = Constant::getAllOnesValue(R2->getType());
    }

    if (R11 == L11 || R11 == L12 || R11 == L21 || R11 == L22) {
      A = R11;
      D = R12;
      E = R1;
      Ok = true;
    } else if (R12 == L11 || R12 == L12 || R12 == L21 || R12 == L22) {
      A = R12;
      D = R11;
      E = R1;
      Ok = true;
    } else {
      return None;
    }
  }
  if (!Ok)
    return None;

  if (L11 == A) {
    B = L12;
    C = L2;
  } else if (L12 == A) {
    B = L11;
    C = L2;
  } else if (L21 == A) {
    B = L22;
    C = L1;
  } else if (L22 == A) {
    B = L21;
    C = L1;
  }

  unsigned LeftType = getMaskedICmpType(A, B, C, PredL);
  unsigned RightType = getMaskedICmpType(A, D, E, PredR);
  return Optional<std::pair<unsigned, unsigned>>(std::make_pair(LeftType, RightType));
}

/// Try to fold (icmp(A & B) ==/!= C) &/| (icmp(A & D) ==/!= E) into a single
/// (icmp(A & X) ==/!= Y), where the left-hand side is of type Mask_NotAllZeros
/// and the right hand side is of type BMask_Mixed. For example,
/// (icmp (A & 12) != 0) & (icmp (A & 15) == 8) -> (icmp (A & 15) == 8).
static Value * foldLogOpOfMaskedICmps_NotAllZeros_BMask_Mixed(
    ICmpInst *LHS, ICmpInst *RHS, bool IsAnd,
    Value *A, Value *B, Value *C, Value *D, Value *E,
    ICmpInst::Predicate PredL, ICmpInst::Predicate PredR,
    llvm::InstCombiner::BuilderTy &Builder) {
  // We are given the canonical form:
  //   (icmp ne (A & B), 0) & (icmp eq (A & D), E).
  // where D & E == E.
  //
  // If IsAnd is false, we get it in negated form:
  //   (icmp eq (A & B), 0) | (icmp ne (A & D), E) ->
  //      !((icmp ne (A & B), 0) & (icmp eq (A & D), E)).
  //
  // We currently handle the case of B, C, D, E are constant.
  //
  ConstantInt *BCst = dyn_cast<ConstantInt>(B);
  if (!BCst)
    return nullptr;
  ConstantInt *CCst = dyn_cast<ConstantInt>(C);
  if (!CCst)
    return nullptr;
  ConstantInt *DCst = dyn_cast<ConstantInt>(D);
  if (!DCst)
    return nullptr;
  ConstantInt *ECst = dyn_cast<ConstantInt>(E);
  if (!ECst)
    return nullptr;

  ICmpInst::Predicate NewCC = IsAnd ? ICmpInst::ICMP_EQ : ICmpInst::ICMP_NE;

  // Update E to the canonical form when D is a power of two and RHS is
  // canonicalized as,
  // (icmp ne (A & D), 0) -> (icmp eq (A & D), D) or
  // (icmp ne (A & D), D) -> (icmp eq (A & D), 0).
  if (PredR != NewCC)
    ECst = cast<ConstantInt>(ConstantExpr::getXor(DCst, ECst));

  // If B or D is zero, skip because if LHS or RHS can be trivially folded by
  // other folding rules and this pattern won't apply any more.
  if (BCst->getValue() == 0 || DCst->getValue() == 0)
    return nullptr;

  // If B and D don't intersect, ie. (B & D) == 0, no folding because we can't
  // deduce anything from it.
  // For example,
  // (icmp ne (A & 12), 0) & (icmp eq (A & 3), 1) -> no folding.
  if ((BCst->getValue() & DCst->getValue()) == 0)
    return nullptr;

  // If the following two conditions are met:
  //
  // 1. mask B covers only a single bit that's not covered by mask D, that is,
  // (B & (B ^ D)) is a power of 2 (in other words, B minus the intersection of
  // B and D has only one bit set) and,
  //
  // 2. RHS (and E) indicates that the rest of B's bits are zero (in other
  // words, the intersection of B and D is zero), that is, ((B & D) & E) == 0
  //
  // then that single bit in B must be one and thus the whole expression can be
  // folded to
  //   (A & (B | D)) == (B & (B ^ D)) | E.
  //
  // For example,
  // (icmp ne (A & 12), 0) & (icmp eq (A & 7), 1) -> (icmp eq (A & 15), 9)
  // (icmp ne (A & 15), 0) & (icmp eq (A & 7), 0) -> (icmp eq (A & 15), 8)
  if ((((BCst->getValue() & DCst->getValue()) & ECst->getValue()) == 0) &&
      (BCst->getValue() & (BCst->getValue() ^ DCst->getValue())).isPowerOf2()) {
    APInt BorD = BCst->getValue() | DCst->getValue();
    APInt BandBxorDorE = (BCst->getValue() & (BCst->getValue() ^ DCst->getValue())) |
        ECst->getValue();
    Value *NewMask = ConstantInt::get(BCst->getType(), BorD);
    Value *NewMaskedValue = ConstantInt::get(BCst->getType(), BandBxorDorE);
    Value *NewAnd = Builder.CreateAnd(A, NewMask);
    return Builder.CreateICmp(NewCC, NewAnd, NewMaskedValue);
  }

  auto IsSubSetOrEqual = [](ConstantInt *C1, ConstantInt *C2) {
    return (C1->getValue() & C2->getValue()) == C1->getValue();
  };
  auto IsSuperSetOrEqual = [](ConstantInt *C1, ConstantInt *C2) {
    return (C1->getValue() & C2->getValue()) == C2->getValue();
  };

  // In the following, we consider only the cases where B is a superset of D, B
  // is a subset of D, or B == D because otherwise there's at least one bit
  // covered by B but not D, in which case we can't deduce much from it, so
  // no folding (aside from the single must-be-one bit case right above.)
  // For example,
  // (icmp ne (A & 14), 0) & (icmp eq (A & 3), 1) -> no folding.
  if (!IsSubSetOrEqual(BCst, DCst) && !IsSuperSetOrEqual(BCst, DCst))
    return nullptr;

  // At this point, either B is a superset of D, B is a subset of D or B == D.

  // If E is zero, if B is a subset of (or equal to) D, LHS and RHS contradict
  // and the whole expression becomes false (or true if negated), otherwise, no
  // folding.
  // For example,
  // (icmp ne (A & 3), 0) & (icmp eq (A & 7), 0) -> false.
  // (icmp ne (A & 15), 0) & (icmp eq (A & 3), 0) -> no folding.
  if (ECst->isZero()) {
    if (IsSubSetOrEqual(BCst, DCst))
      return ConstantInt::get(LHS->getType(), !IsAnd);
    return nullptr;
  }

  // At this point, B, D, E aren't zero and (B & D) == B, (B & D) == D or B ==
  // D. If B is a superset of (or equal to) D, since E is not zero, LHS is
  // subsumed by RHS (RHS implies LHS.) So the whole expression becomes
  // RHS. For example,
  // (icmp ne (A & 255), 0) & (icmp eq (A & 15), 8) -> (icmp eq (A & 15), 8).
  // (icmp ne (A & 15), 0) & (icmp eq (A & 15), 8) -> (icmp eq (A & 15), 8).
  if (IsSuperSetOrEqual(BCst, DCst))
    return RHS;
  // Otherwise, B is a subset of D. If B and E have a common bit set,
  // ie. (B & E) != 0, then LHS is subsumed by RHS. For example.
  // (icmp ne (A & 12), 0) & (icmp eq (A & 15), 8) -> (icmp eq (A & 15), 8).
  assert(IsSubSetOrEqual(BCst, DCst) && "Precondition due to above code");
  if ((BCst->getValue() & ECst->getValue()) != 0)
    return RHS;
  // Otherwise, LHS and RHS contradict and the whole expression becomes false
  // (or true if negated.) For example,
  // (icmp ne (A & 7), 0) & (icmp eq (A & 15), 8) -> false.
  // (icmp ne (A & 6), 0) & (icmp eq (A & 15), 8) -> false.
  return ConstantInt::get(LHS->getType(), !IsAnd);
}

/// Try to fold (icmp(A & B) ==/!= 0) &/| (icmp(A & D) ==/!= E) into a single
/// (icmp(A & X) ==/!= Y), where the left-hand side and the right hand side
/// aren't of the common mask pattern type.
static Value *foldLogOpOfMaskedICmpsAsymmetric(
    ICmpInst *LHS, ICmpInst *RHS, bool IsAnd,
    Value *A, Value *B, Value *C, Value *D, Value *E,
    ICmpInst::Predicate PredL, ICmpInst::Predicate PredR,
    unsigned LHSMask, unsigned RHSMask,
    llvm::InstCombiner::BuilderTy &Builder) {
  assert(ICmpInst::isEquality(PredL) && ICmpInst::isEquality(PredR) &&
         "Expected equality predicates for masked type of icmps.");
  // Handle Mask_NotAllZeros-BMask_Mixed cases.
  // (icmp ne/eq (A & B), C) &/| (icmp eq/ne (A & D), E), or
  // (icmp eq/ne (A & B), C) &/| (icmp ne/eq (A & D), E)
  //    which gets swapped to
  //    (icmp ne/eq (A & D), E) &/| (icmp eq/ne (A & B), C).
  if (!IsAnd) {
    LHSMask = conjugateICmpMask(LHSMask);
    RHSMask = conjugateICmpMask(RHSMask);
  }
  if ((LHSMask & Mask_NotAllZeros) && (RHSMask & BMask_Mixed)) {
    if (Value *V = foldLogOpOfMaskedICmps_NotAllZeros_BMask_Mixed(
            LHS, RHS, IsAnd, A, B, C, D, E,
            PredL, PredR, Builder)) {
      return V;
    }
  } else if ((LHSMask & BMask_Mixed) && (RHSMask & Mask_NotAllZeros)) {
    if (Value *V = foldLogOpOfMaskedICmps_NotAllZeros_BMask_Mixed(
            RHS, LHS, IsAnd, A, D, E, B, C,
            PredR, PredL, Builder)) {
      return V;
    }
  }
  return nullptr;
}

/// Try to fold (icmp(A & B) ==/!= C) &/| (icmp(A & D) ==/!= E)
/// into a single (icmp(A & X) ==/!= Y).
static Value *foldLogOpOfMaskedICmps(ICmpInst *LHS, ICmpInst *RHS, bool IsAnd,
                                     llvm::InstCombiner::BuilderTy &Builder) {
  Value *A = nullptr, *B = nullptr, *C = nullptr, *D = nullptr, *E = nullptr;
  ICmpInst::Predicate PredL = LHS->getPredicate(), PredR = RHS->getPredicate();
  Optional<std::pair<unsigned, unsigned>> MaskPair =
      getMaskedTypeForICmpPair(A, B, C, D, E, LHS, RHS, PredL, PredR);
  if (!MaskPair)
    return nullptr;
  assert(ICmpInst::isEquality(PredL) && ICmpInst::isEquality(PredR) &&
         "Expected equality predicates for masked type of icmps.");
  unsigned LHSMask = MaskPair->first;
  unsigned RHSMask = MaskPair->second;
  unsigned Mask = LHSMask & RHSMask;
  if (Mask == 0) {
    // Even if the two sides don't share a common pattern, check if folding can
    // still happen.
    if (Value *V = foldLogOpOfMaskedICmpsAsymmetric(
            LHS, RHS, IsAnd, A, B, C, D, E, PredL, PredR, LHSMask, RHSMask,
            Builder))
      return V;
    return nullptr;
  }

  // In full generality:
  //     (icmp (A & B) Op C) | (icmp (A & D) Op E)
  // ==  ![ (icmp (A & B) !Op C) & (icmp (A & D) !Op E) ]
  //
  // If the latter can be converted into (icmp (A & X) Op Y) then the former is
  // equivalent to (icmp (A & X) !Op Y).
  //
  // Therefore, we can pretend for the rest of this function that we're dealing
  // with the conjunction, provided we flip the sense of any comparisons (both
  // input and output).

  // In most cases we're going to produce an EQ for the "&&" case.
  ICmpInst::Predicate NewCC = IsAnd ? ICmpInst::ICMP_EQ : ICmpInst::ICMP_NE;
  if (!IsAnd) {
    // Convert the masking analysis into its equivalent with negated
    // comparisons.
    Mask = conjugateICmpMask(Mask);
  }

  if (Mask & Mask_AllZeros) {
    // (icmp eq (A & B), 0) & (icmp eq (A & D), 0)
    // -> (icmp eq (A & (B|D)), 0)
    Value *NewOr = Builder.CreateOr(B, D);
    Value *NewAnd = Builder.CreateAnd(A, NewOr);
    // We can't use C as zero because we might actually handle
    //   (icmp ne (A & B), B) & (icmp ne (A & D), D)
    // with B and D, having a single bit set.
    Value *Zero = Constant::getNullValue(A->getType());
    return Builder.CreateICmp(NewCC, NewAnd, Zero);
  }
  if (Mask & BMask_AllOnes) {
    // (icmp eq (A & B), B) & (icmp eq (A & D), D)
    // -> (icmp eq (A & (B|D)), (B|D))
    Value *NewOr = Builder.CreateOr(B, D);
    Value *NewAnd = Builder.CreateAnd(A, NewOr);
    return Builder.CreateICmp(NewCC, NewAnd, NewOr);
  }
  if (Mask & AMask_AllOnes) {
    // (icmp eq (A & B), A) & (icmp eq (A & D), A)
    // -> (icmp eq (A & (B&D)), A)
    Value *NewAnd1 = Builder.CreateAnd(B, D);
    Value *NewAnd2 = Builder.CreateAnd(A, NewAnd1);
    return Builder.CreateICmp(NewCC, NewAnd2, A);
  }

  // Remaining cases assume at least that B and D are constant, and depend on
  // their actual values. This isn't strictly necessary, just a "handle the
  // easy cases for now" decision.
  ConstantInt *BCst = dyn_cast<ConstantInt>(B);
  if (!BCst)
    return nullptr;
  ConstantInt *DCst = dyn_cast<ConstantInt>(D);
  if (!DCst)
    return nullptr;

  if (Mask & (Mask_NotAllZeros | BMask_NotAllOnes)) {
    // (icmp ne (A & B), 0) & (icmp ne (A & D), 0) and
    // (icmp ne (A & B), B) & (icmp ne (A & D), D)
    //     -> (icmp ne (A & B), 0) or (icmp ne (A & D), 0)
    // Only valid if one of the masks is a superset of the other (check "B&D" is
    // the same as either B or D).
    APInt NewMask = BCst->getValue() & DCst->getValue();

    if (NewMask == BCst->getValue())
      return LHS;
    else if (NewMask == DCst->getValue())
      return RHS;
  }

  if (Mask & AMask_NotAllOnes) {
    // (icmp ne (A & B), B) & (icmp ne (A & D), D)
    //     -> (icmp ne (A & B), A) or (icmp ne (A & D), A)
    // Only valid if one of the masks is a superset of the other (check "B|D" is
    // the same as either B or D).
    APInt NewMask = BCst->getValue() | DCst->getValue();

    if (NewMask == BCst->getValue())
      return LHS;
    else if (NewMask == DCst->getValue())
      return RHS;
  }

  if (Mask & BMask_Mixed) {
    // (icmp eq (A & B), C) & (icmp eq (A & D), E)
    // We already know that B & C == C && D & E == E.
    // If we can prove that (B & D) & (C ^ E) == 0, that is, the bits of
    // C and E, which are shared by both the mask B and the mask D, don't
    // contradict, then we can transform to
    // -> (icmp eq (A & (B|D)), (C|E))
    // Currently, we only handle the case of B, C, D, and E being constant.
    // We can't simply use C and E because we might actually handle
    //   (icmp ne (A & B), B) & (icmp eq (A & D), D)
    // with B and D, having a single bit set.
    ConstantInt *CCst = dyn_cast<ConstantInt>(C);
    if (!CCst)
      return nullptr;
    ConstantInt *ECst = dyn_cast<ConstantInt>(E);
    if (!ECst)
      return nullptr;
    if (PredL != NewCC)
      CCst = cast<ConstantInt>(ConstantExpr::getXor(BCst, CCst));
    if (PredR != NewCC)
      ECst = cast<ConstantInt>(ConstantExpr::getXor(DCst, ECst));

    // If there is a conflict, we should actually return a false for the
    // whole construct.
    if (((BCst->getValue() & DCst->getValue()) &
         (CCst->getValue() ^ ECst->getValue())).getBoolValue())
      return ConstantInt::get(LHS->getType(), !IsAnd);

    Value *NewOr1 = Builder.CreateOr(B, D);
    Value *NewOr2 = ConstantExpr::getOr(CCst, ECst);
    Value *NewAnd = Builder.CreateAnd(A, NewOr1);
    return Builder.CreateICmp(NewCC, NewAnd, NewOr2);
  }

  return nullptr;
}

/// Try to fold a signed range checked with lower bound 0 to an unsigned icmp.
/// Example: (icmp sge x, 0) & (icmp slt x, n) --> icmp ult x, n
/// If \p Inverted is true then the check is for the inverted range, e.g.
/// (icmp slt x, 0) | (icmp sgt x, n) --> icmp ugt x, n
Value *InstCombiner::simplifyRangeCheck(ICmpInst *Cmp0, ICmpInst *Cmp1,
                                        bool Inverted) {
  // Check the lower range comparison, e.g. x >= 0
  // InstCombine already ensured that if there is a constant it's on the RHS.
  ConstantInt *RangeStart = dyn_cast<ConstantInt>(Cmp0->getOperand(1));
  if (!RangeStart)
    return nullptr;

  ICmpInst::Predicate Pred0 = (Inverted ? Cmp0->getInversePredicate() :
                               Cmp0->getPredicate());

  // Accept x > -1 or x >= 0 (after potentially inverting the predicate).
  if (!((Pred0 == ICmpInst::ICMP_SGT && RangeStart->isMinusOne()) ||
        (Pred0 == ICmpInst::ICMP_SGE && RangeStart->isZero())))
    return nullptr;

  ICmpInst::Predicate Pred1 = (Inverted ? Cmp1->getInversePredicate() :
                               Cmp1->getPredicate());

  Value *Input = Cmp0->getOperand(0);
  Value *RangeEnd;
  if (Cmp1->getOperand(0) == Input) {
    // For the upper range compare we have: icmp x, n
    RangeEnd = Cmp1->getOperand(1);
  } else if (Cmp1->getOperand(1) == Input) {
    // For the upper range compare we have: icmp n, x
    RangeEnd = Cmp1->getOperand(0);
    Pred1 = ICmpInst::getSwappedPredicate(Pred1);
  } else {
    return nullptr;
  }

  // Check the upper range comparison, e.g. x < n
  ICmpInst::Predicate NewPred;
  switch (Pred1) {
    case ICmpInst::ICMP_SLT: NewPred = ICmpInst::ICMP_ULT; break;
    case ICmpInst::ICMP_SLE: NewPred = ICmpInst::ICMP_ULE; break;
    default: return nullptr;
  }

  // This simplification is only valid if the upper range is not negative.
  KnownBits Known = computeKnownBits(RangeEnd, /*Depth=*/0, Cmp1);
  if (!Known.isNonNegative())
    return nullptr;

  if (Inverted)
    NewPred = ICmpInst::getInversePredicate(NewPred);

  return Builder.CreateICmp(NewPred, Input, RangeEnd);
}

static Value *
foldAndOrOfEqualityCmpsWithConstants(ICmpInst *LHS, ICmpInst *RHS,
                                     bool JoinedByAnd,
                                     InstCombiner::BuilderTy &Builder) {
  Value *X = LHS->getOperand(0);
  if (X != RHS->getOperand(0))
    return nullptr;

  const APInt *C1, *C2;
  if (!match(LHS->getOperand(1), m_APInt(C1)) ||
      !match(RHS->getOperand(1), m_APInt(C2)))
    return nullptr;

  // We only handle (X != C1 && X != C2) and (X == C1 || X == C2).
  ICmpInst::Predicate Pred = LHS->getPredicate();
  if (Pred !=  RHS->getPredicate())
    return nullptr;
  if (JoinedByAnd && Pred != ICmpInst::ICMP_NE)
    return nullptr;
  if (!JoinedByAnd && Pred != ICmpInst::ICMP_EQ)
    return nullptr;

  // The larger unsigned constant goes on the right.
  if (C1->ugt(*C2))
    std::swap(C1, C2);

  APInt Xor = *C1 ^ *C2;
  if (Xor.isPowerOf2()) {
    // If LHSC and RHSC differ by only one bit, then set that bit in X and
    // compare against the larger constant:
    // (X == C1 || X == C2) --> (X | (C1 ^ C2)) == C2
    // (X != C1 && X != C2) --> (X | (C1 ^ C2)) != C2
    // We choose an 'or' with a Pow2 constant rather than the inverse mask with
    // 'and' because that may lead to smaller codegen from a smaller constant.
    Value *Or = Builder.CreateOr(X, ConstantInt::get(X->getType(), Xor));
    return Builder.CreateICmp(Pred, Or, ConstantInt::get(X->getType(), *C2));
  }

  // Special case: get the ordering right when the values wrap around zero.
  // Ie, we assumed the constants were unsigned when swapping earlier.
  if (C1->isNullValue() && C2->isAllOnesValue())
    std::swap(C1, C2);

  if (*C1 == *C2 - 1) {
    // (X == 13 || X == 14) --> X - 13 <=u 1
    // (X != 13 && X != 14) --> X - 13  >u 1
    // An 'add' is the canonical IR form, so favor that over a 'sub'.
    Value *Add = Builder.CreateAdd(X, ConstantInt::get(X->getType(), -(*C1)));
    auto NewPred = JoinedByAnd ? ICmpInst::ICMP_UGT : ICmpInst::ICMP_ULE;
    return Builder.CreateICmp(NewPred, Add, ConstantInt::get(X->getType(), 1));
  }

  return nullptr;
}

// Fold (iszero(A & K1) | iszero(A & K2)) -> (A & (K1 | K2)) != (K1 | K2)
// Fold (!iszero(A & K1) & !iszero(A & K2)) -> (A & (K1 | K2)) == (K1 | K2)
Value *InstCombiner::foldAndOrOfICmpsOfAndWithPow2(ICmpInst *LHS, ICmpInst *RHS,
                                                   bool JoinedByAnd,
                                                   Instruction &CxtI) {
  ICmpInst::Predicate Pred = LHS->getPredicate();
  if (Pred != RHS->getPredicate())
    return nullptr;
  if (JoinedByAnd && Pred != ICmpInst::ICMP_NE)
    return nullptr;
  if (!JoinedByAnd && Pred != ICmpInst::ICMP_EQ)
    return nullptr;

  // TODO support vector splats
  ConstantInt *LHSC = dyn_cast<ConstantInt>(LHS->getOperand(1));
  ConstantInt *RHSC = dyn_cast<ConstantInt>(RHS->getOperand(1));
  if (!LHSC || !RHSC || !LHSC->isZero() || !RHSC->isZero())
    return nullptr;

  Value *A, *B, *C, *D;
  if (match(LHS->getOperand(0), m_And(m_Value(A), m_Value(B))) &&
      match(RHS->getOperand(0), m_And(m_Value(C), m_Value(D)))) {
    if (A == D || B == D)
      std::swap(C, D);
    if (B == C)
      std::swap(A, B);

    if (A == C &&
        isKnownToBeAPowerOfTwo(B, false, 0, &CxtI) &&
        isKnownToBeAPowerOfTwo(D, false, 0, &CxtI)) {
      Value *Mask = Builder.CreateOr(B, D);
      Value *Masked = Builder.CreateAnd(A, Mask);
      auto NewPred = JoinedByAnd ? ICmpInst::ICMP_EQ : ICmpInst::ICMP_NE;
      return Builder.CreateICmp(NewPred, Masked, Mask);
    }
  }

  return nullptr;
}

/// General pattern:
///   X & Y
///
/// Where Y is checking that all the high bits (covered by a mask 4294967168)
/// are uniform, i.e.  %arg & 4294967168  can be either  4294967168  or  0
/// Pattern can be one of:
///   %t = add        i32 %arg,    128
///   %r = icmp   ult i32 %t,      256
/// Or
///   %t0 = shl       i32 %arg,    24
///   %t1 = ashr      i32 %t0,     24
///   %r  = icmp  eq  i32 %t1,     %arg
/// Or
///   %t0 = trunc     i32 %arg  to i8
///   %t1 = sext      i8  %t0   to i32
///   %r  = icmp  eq  i32 %t1,     %arg
/// This pattern is a signed truncation check.
///
/// And X is checking that some bit in that same mask is zero.
/// I.e. can be one of:
///   %r = icmp sgt i32   %arg,    -1
/// Or
///   %t = and      i32   %arg,    2147483648
///   %r = icmp eq  i32   %t,      0
///
/// Since we are checking that all the bits in that mask are the same,
/// and a particular bit is zero, what we are really checking is that all the
/// masked bits are zero.
/// So this should be transformed to:
///   %r = icmp ult i32 %arg, 128
static Value *foldSignedTruncationCheck(ICmpInst *ICmp0, ICmpInst *ICmp1,
                                        Instruction &CxtI,
                                        InstCombiner::BuilderTy &Builder) {
  assert(CxtI.getOpcode() == Instruction::And);

  // Match  icmp ult (add %arg, C01), C1   (C1 == C01 << 1; powers of two)
  auto tryToMatchSignedTruncationCheck = [](ICmpInst *ICmp, Value *&X,
                                            APInt &SignBitMask) -> bool {
    CmpInst::Predicate Pred;
    const APInt *I01, *I1; // powers of two; I1 == I01 << 1
    if (!(match(ICmp,
                m_ICmp(Pred, m_Add(m_Value(X), m_Power2(I01)), m_Power2(I1))) &&
          Pred == ICmpInst::ICMP_ULT && I1->ugt(*I01) && I01->shl(1) == *I1))
      return false;
    // Which bit is the new sign bit as per the 'signed truncation' pattern?
    SignBitMask = *I01;
    return true;
  };

  // One icmp needs to be 'signed truncation check'.
  // We need to match this first, else we will mismatch commutative cases.
  Value *X1;
  APInt HighestBit;
  ICmpInst *OtherICmp;
  if (tryToMatchSignedTruncationCheck(ICmp1, X1, HighestBit))
    OtherICmp = ICmp0;
  else if (tryToMatchSignedTruncationCheck(ICmp0, X1, HighestBit))
    OtherICmp = ICmp1;
  else
    return nullptr;

  assert(HighestBit.isPowerOf2() && "expected to be power of two (non-zero)");

  // Try to match/decompose into:  icmp eq (X & Mask), 0
  auto tryToDecompose = [](ICmpInst *ICmp, Value *&X,
                           APInt &UnsetBitsMask) -> bool {
    CmpInst::Predicate Pred = ICmp->getPredicate();
    // Can it be decomposed into  icmp eq (X & Mask), 0  ?
    if (llvm::decomposeBitTestICmp(ICmp->getOperand(0), ICmp->getOperand(1),
                                   Pred, X, UnsetBitsMask,
                                   /*LookThruTrunc=*/false) &&
        Pred == ICmpInst::ICMP_EQ)
      return true;
    // Is it  icmp eq (X & Mask), 0  already?
    const APInt *Mask;
    if (match(ICmp, m_ICmp(Pred, m_And(m_Value(X), m_APInt(Mask)), m_Zero())) &&
        Pred == ICmpInst::ICMP_EQ) {
      UnsetBitsMask = *Mask;
      return true;
    }
    return false;
  };

  // And the other icmp needs to be decomposable into a bit test.
  Value *X0;
  APInt UnsetBitsMask;
  if (!tryToDecompose(OtherICmp, X0, UnsetBitsMask))
    return nullptr;

  assert(!UnsetBitsMask.isNullValue() && "empty mask makes no sense.");

  // Are they working on the same value?
  Value *X;
  if (X1 == X0) {
    // Ok as is.
    X = X1;
  } else if (match(X0, m_Trunc(m_Specific(X1)))) {
    UnsetBitsMask = UnsetBitsMask.zext(X1->getType()->getScalarSizeInBits());
    X = X1;
  } else
    return nullptr;

  // So which bits should be uniform as per the 'signed truncation check'?
  // (all the bits starting with (i.e. including) HighestBit)
  APInt SignBitsMask = ~(HighestBit - 1U);

  // UnsetBitsMask must have some common bits with SignBitsMask,
  if (!UnsetBitsMask.intersects(SignBitsMask))
    return nullptr;

  // Does UnsetBitsMask contain any bits outside of SignBitsMask?
  if (!UnsetBitsMask.isSubsetOf(SignBitsMask)) {
    APInt OtherHighestBit = (~UnsetBitsMask) + 1U;
    if (!OtherHighestBit.isPowerOf2())
      return nullptr;
    HighestBit = APIntOps::umin(HighestBit, OtherHighestBit);
  }
  // Else, if it does not, then all is ok as-is.

  // %r = icmp ult %X, SignBit
  return Builder.CreateICmpULT(X, ConstantInt::get(X->getType(), HighestBit),
                               CxtI.getName() + ".simplified");
}

/// Fold (icmp)&(icmp) if possible.
Value *InstCombiner::foldAndOfICmps(ICmpInst *LHS, ICmpInst *RHS,
                                    Instruction &CxtI) {
  // Fold (!iszero(A & K1) & !iszero(A & K2)) ->  (A & (K1 | K2)) == (K1 | K2)
  // if K1 and K2 are a one-bit mask.
  if (Value *V = foldAndOrOfICmpsOfAndWithPow2(LHS, RHS, true, CxtI))
    return V;

  ICmpInst::Predicate PredL = LHS->getPredicate(), PredR = RHS->getPredicate();

  // (icmp1 A, B) & (icmp2 A, B) --> (icmp3 A, B)
  if (predicatesFoldable(PredL, PredR)) {
    if (LHS->getOperand(0) == RHS->getOperand(1) &&
        LHS->getOperand(1) == RHS->getOperand(0))
      LHS->swapOperands();
    if (LHS->getOperand(0) == RHS->getOperand(0) &&
        LHS->getOperand(1) == RHS->getOperand(1)) {
      Value *Op0 = LHS->getOperand(0), *Op1 = LHS->getOperand(1);
      unsigned Code = getICmpCode(LHS) & getICmpCode(RHS);
      bool IsSigned = LHS->isSigned() || RHS->isSigned();
      return getNewICmpValue(Code, IsSigned, Op0, Op1, Builder);
    }
  }

  // handle (roughly):  (icmp eq (A & B), C) & (icmp eq (A & D), E)
  if (Value *V = foldLogOpOfMaskedICmps(LHS, RHS, true, Builder))
    return V;

  // E.g. (icmp sge x, 0) & (icmp slt x, n) --> icmp ult x, n
  if (Value *V = simplifyRangeCheck(LHS, RHS, /*Inverted=*/false))
    return V;

  // E.g. (icmp slt x, n) & (icmp sge x, 0) --> icmp ult x, n
  if (Value *V = simplifyRangeCheck(RHS, LHS, /*Inverted=*/false))
    return V;

  if (Value *V = foldAndOrOfEqualityCmpsWithConstants(LHS, RHS, true, Builder))
    return V;

  if (Value *V = foldSignedTruncationCheck(LHS, RHS, CxtI, Builder))
    return V;

  // This only handles icmp of constants: (icmp1 A, C1) & (icmp2 B, C2).
  Value *LHS0 = LHS->getOperand(0), *RHS0 = RHS->getOperand(0);
  ConstantInt *LHSC = dyn_cast<ConstantInt>(LHS->getOperand(1));
  ConstantInt *RHSC = dyn_cast<ConstantInt>(RHS->getOperand(1));
  if (!LHSC || !RHSC)
    return nullptr;

  if (LHSC == RHSC && PredL == PredR) {
    // (icmp ult A, C) & (icmp ult B, C) --> (icmp ult (A|B), C)
    // where C is a power of 2 or
    // (icmp eq A, 0) & (icmp eq B, 0) --> (icmp eq (A|B), 0)
    if ((PredL == ICmpInst::ICMP_ULT && LHSC->getValue().isPowerOf2()) ||
        (PredL == ICmpInst::ICMP_EQ && LHSC->isZero())) {
      Value *NewOr = Builder.CreateOr(LHS0, RHS0);
      return Builder.CreateICmp(PredL, NewOr, LHSC);
    }
  }

  // (trunc x) == C1 & (and x, CA) == C2 -> (and x, CA|CMAX) == C1|C2
  // where CMAX is the all ones value for the truncated type,
  // iff the lower bits of C2 and CA are zero.
  if (PredL == ICmpInst::ICMP_EQ && PredL == PredR && LHS->hasOneUse() &&
      RHS->hasOneUse()) {
    Value *V;
    ConstantInt *AndC, *SmallC = nullptr, *BigC = nullptr;

    // (trunc x) == C1 & (and x, CA) == C2
    // (and x, CA) == C2 & (trunc x) == C1
    if (match(RHS0, m_Trunc(m_Value(V))) &&
        match(LHS0, m_And(m_Specific(V), m_ConstantInt(AndC)))) {
      SmallC = RHSC;
      BigC = LHSC;
    } else if (match(LHS0, m_Trunc(m_Value(V))) &&
               match(RHS0, m_And(m_Specific(V), m_ConstantInt(AndC)))) {
      SmallC = LHSC;
      BigC = RHSC;
    }

    if (SmallC && BigC) {
      unsigned BigBitSize = BigC->getType()->getBitWidth();
      unsigned SmallBitSize = SmallC->getType()->getBitWidth();

      // Check that the low bits are zero.
      APInt Low = APInt::getLowBitsSet(BigBitSize, SmallBitSize);
      if ((Low & AndC->getValue()).isNullValue() &&
          (Low & BigC->getValue()).isNullValue()) {
        Value *NewAnd = Builder.CreateAnd(V, Low | AndC->getValue());
        APInt N = SmallC->getValue().zext(BigBitSize) | BigC->getValue();
        Value *NewVal = ConstantInt::get(AndC->getType()->getContext(), N);
        return Builder.CreateICmp(PredL, NewAnd, NewVal);
      }
    }
  }

  // From here on, we only handle:
  //    (icmp1 A, C1) & (icmp2 A, C2) --> something simpler.
  if (LHS0 != RHS0)
    return nullptr;

  // ICMP_[US][GL]E X, C is folded to ICMP_[US][GL]T elsewhere.
  if (PredL == ICmpInst::ICMP_UGE || PredL == ICmpInst::ICMP_ULE ||
      PredR == ICmpInst::ICMP_UGE || PredR == ICmpInst::ICMP_ULE ||
      PredL == ICmpInst::ICMP_SGE || PredL == ICmpInst::ICMP_SLE ||
      PredR == ICmpInst::ICMP_SGE || PredR == ICmpInst::ICMP_SLE)
    return nullptr;

  // We can't fold (ugt x, C) & (sgt x, C2).
  if (!predicatesFoldable(PredL, PredR))
    return nullptr;

  // Ensure that the larger constant is on the RHS.
  bool ShouldSwap;
  if (CmpInst::isSigned(PredL) ||
      (ICmpInst::isEquality(PredL) && CmpInst::isSigned(PredR)))
    ShouldSwap = LHSC->getValue().sgt(RHSC->getValue());
  else
    ShouldSwap = LHSC->getValue().ugt(RHSC->getValue());

  if (ShouldSwap) {
    std::swap(LHS, RHS);
    std::swap(LHSC, RHSC);
    std::swap(PredL, PredR);
  }

  // At this point, we know we have two icmp instructions
  // comparing a value against two constants and and'ing the result
  // together.  Because of the above check, we know that we only have
  // icmp eq, icmp ne, icmp [su]lt, and icmp [SU]gt here. We also know
  // (from the icmp folding check above), that the two constants
  // are not equal and that the larger constant is on the RHS
  assert(LHSC != RHSC && "Compares not folded above?");

  switch (PredL) {
  default:
    llvm_unreachable("Unknown integer condition code!");
  case ICmpInst::ICMP_NE:
    switch (PredR) {
    default:
      llvm_unreachable("Unknown integer condition code!");
    case ICmpInst::ICMP_ULT:
      if (LHSC == SubOne(RHSC)) // (X != 13 & X u< 14) -> X < 13
        return Builder.CreateICmpULT(LHS0, LHSC);
      if (LHSC->isZero()) // (X !=  0 & X u< 14) -> X-1 u< 13
        return insertRangeTest(LHS0, LHSC->getValue() + 1, RHSC->getValue(),
                               false, true);
      break; // (X != 13 & X u< 15) -> no change
    case ICmpInst::ICMP_SLT:
      if (LHSC == SubOne(RHSC)) // (X != 13 & X s< 14) -> X < 13
        return Builder.CreateICmpSLT(LHS0, LHSC);
      break;                 // (X != 13 & X s< 15) -> no change
    case ICmpInst::ICMP_NE:
      // Potential folds for this case should already be handled.
      break;
    }
    break;
  case ICmpInst::ICMP_UGT:
    switch (PredR) {
    default:
      llvm_unreachable("Unknown integer condition code!");
    case ICmpInst::ICMP_NE:
      if (RHSC == AddOne(LHSC)) // (X u> 13 & X != 14) -> X u> 14
        return Builder.CreateICmp(PredL, LHS0, RHSC);
      break;                 // (X u> 13 & X != 15) -> no change
    case ICmpInst::ICMP_ULT: // (X u> 13 & X u< 15) -> (X-14) <u 1
      return insertRangeTest(LHS0, LHSC->getValue() + 1, RHSC->getValue(),
                             false, true);
    }
    break;
  case ICmpInst::ICMP_SGT:
    switch (PredR) {
    default:
      llvm_unreachable("Unknown integer condition code!");
    case ICmpInst::ICMP_NE:
      if (RHSC == AddOne(LHSC)) // (X s> 13 & X != 14) -> X s> 14
        return Builder.CreateICmp(PredL, LHS0, RHSC);
      break;                 // (X s> 13 & X != 15) -> no change
    case ICmpInst::ICMP_SLT: // (X s> 13 & X s< 15) -> (X-14) s< 1
      return insertRangeTest(LHS0, LHSC->getValue() + 1, RHSC->getValue(), true,
                             true);
    }
    break;
  }

  return nullptr;
}

Value *InstCombiner::foldLogicOfFCmps(FCmpInst *LHS, FCmpInst *RHS, bool IsAnd) {
  Value *LHS0 = LHS->getOperand(0), *LHS1 = LHS->getOperand(1);
  Value *RHS0 = RHS->getOperand(0), *RHS1 = RHS->getOperand(1);
  FCmpInst::Predicate PredL = LHS->getPredicate(), PredR = RHS->getPredicate();

  if (LHS0 == RHS1 && RHS0 == LHS1) {
    // Swap RHS operands to match LHS.
    PredR = FCmpInst::getSwappedPredicate(PredR);
    std::swap(RHS0, RHS1);
  }

  // Simplify (fcmp cc0 x, y) & (fcmp cc1 x, y).
  // Suppose the relation between x and y is R, where R is one of
  // U(1000), L(0100), G(0010) or E(0001), and CC0 and CC1 are the bitmasks for
  // testing the desired relations.
  //
  // Since (R & CC0) and (R & CC1) are either R or 0, we actually have this:
  //    bool(R & CC0) && bool(R & CC1)
  //  = bool((R & CC0) & (R & CC1))
  //  = bool(R & (CC0 & CC1)) <= by re-association, commutation, and idempotency
  //
  // Since (R & CC0) and (R & CC1) are either R or 0, we actually have this:
  //    bool(R & CC0) || bool(R & CC1)
  //  = bool((R & CC0) | (R & CC1))
  //  = bool(R & (CC0 | CC1)) <= by reversed distribution (contribution? ;)
  if (LHS0 == RHS0 && LHS1 == RHS1) {
    unsigned FCmpCodeL = getFCmpCode(PredL);
    unsigned FCmpCodeR = getFCmpCode(PredR);
    unsigned NewPred = IsAnd ? FCmpCodeL & FCmpCodeR : FCmpCodeL | FCmpCodeR;
    return getFCmpValue(NewPred, LHS0, LHS1, Builder);
  }

  if ((PredL == FCmpInst::FCMP_ORD && PredR == FCmpInst::FCMP_ORD && IsAnd) ||
      (PredL == FCmpInst::FCMP_UNO && PredR == FCmpInst::FCMP_UNO && !IsAnd)) {
    if (LHS0->getType() != RHS0->getType())
      return nullptr;

    // FCmp canonicalization ensures that (fcmp ord/uno X, X) and
    // (fcmp ord/uno X, C) will be transformed to (fcmp X, +0.0).
    if (match(LHS1, m_PosZeroFP()) && match(RHS1, m_PosZeroFP()))
      // Ignore the constants because they are obviously not NANs:
      // (fcmp ord x, 0.0) & (fcmp ord y, 0.0)  -> (fcmp ord x, y)
      // (fcmp uno x, 0.0) | (fcmp uno y, 0.0)  -> (fcmp uno x, y)
      return Builder.CreateFCmp(PredL, LHS0, RHS0);
  }

  return nullptr;
}

/// Match De Morgan's Laws:
/// (~A & ~B) == (~(A | B))
/// (~A | ~B) == (~(A & B))
static Instruction *matchDeMorgansLaws(BinaryOperator &I,
                                       InstCombiner::BuilderTy &Builder) {
  auto Opcode = I.getOpcode();
  assert((Opcode == Instruction::And || Opcode == Instruction::Or) &&
         "Trying to match De Morgan's Laws with something other than and/or");

  // Flip the logic operation.
  Opcode = (Opcode == Instruction::And) ? Instruction::Or : Instruction::And;

  Value *A, *B;
  if (match(I.getOperand(0), m_OneUse(m_Not(m_Value(A)))) &&
      match(I.getOperand(1), m_OneUse(m_Not(m_Value(B)))) &&
      !IsFreeToInvert(A, A->hasOneUse()) &&
      !IsFreeToInvert(B, B->hasOneUse())) {
    Value *AndOr = Builder.CreateBinOp(Opcode, A, B, I.getName() + ".demorgan");
    return BinaryOperator::CreateNot(AndOr);
  }

  return nullptr;
}

bool InstCombiner::shouldOptimizeCast(CastInst *CI) {
  Value *CastSrc = CI->getOperand(0);

  // Noop casts and casts of constants should be eliminated trivially.
  if (CI->getSrcTy() == CI->getDestTy() || isa<Constant>(CastSrc))
    return false;

  // If this cast is paired with another cast that can be eliminated, we prefer
  // to have it eliminated.
  if (const auto *PrecedingCI = dyn_cast<CastInst>(CastSrc))
    if (isEliminableCastPair(PrecedingCI, CI))
      return false;

  return true;
}

/// Fold {and,or,xor} (cast X), C.
static Instruction *foldLogicCastConstant(BinaryOperator &Logic, CastInst *Cast,
                                          InstCombiner::BuilderTy &Builder) {
  Constant *C = dyn_cast<Constant>(Logic.getOperand(1));
  if (!C)
    return nullptr;

  auto LogicOpc = Logic.getOpcode();
  Type *DestTy = Logic.getType();
  Type *SrcTy = Cast->getSrcTy();

  // Move the logic operation ahead of a zext or sext if the constant is
  // unchanged in the smaller source type. Performing the logic in a smaller
  // type may provide more information to later folds, and the smaller logic
  // instruction may be cheaper (particularly in the case of vectors).
  Value *X;
  if (match(Cast, m_OneUse(m_ZExt(m_Value(X))))) {
    Constant *TruncC = ConstantExpr::getTrunc(C, SrcTy);
    Constant *ZextTruncC = ConstantExpr::getZExt(TruncC, DestTy);
    if (ZextTruncC == C) {
      // LogicOpc (zext X), C --> zext (LogicOpc X, C)
      Value *NewOp = Builder.CreateBinOp(LogicOpc, X, TruncC);
      return new ZExtInst(NewOp, DestTy);
    }
  }

  if (match(Cast, m_OneUse(m_SExt(m_Value(X))))) {
    Constant *TruncC = ConstantExpr::getTrunc(C, SrcTy);
    Constant *SextTruncC = ConstantExpr::getSExt(TruncC, DestTy);
    if (SextTruncC == C) {
      // LogicOpc (sext X), C --> sext (LogicOpc X, C)
      Value *NewOp = Builder.CreateBinOp(LogicOpc, X, TruncC);
      return new SExtInst(NewOp, DestTy);
    }
  }

  return nullptr;
}

/// Fold {and,or,xor} (cast X), Y.
Instruction *InstCombiner::foldCastedBitwiseLogic(BinaryOperator &I) {
  auto LogicOpc = I.getOpcode();
  assert(I.isBitwiseLogicOp() && "Unexpected opcode for bitwise logic folding");

  Value *Op0 = I.getOperand(0), *Op1 = I.getOperand(1);
  CastInst *Cast0 = dyn_cast<CastInst>(Op0);
  if (!Cast0)
    return nullptr;

  // This must be a cast from an integer or integer vector source type to allow
  // transformation of the logic operation to the source type.
  Type *DestTy = I.getType();
  Type *SrcTy = Cast0->getSrcTy();
  if (!SrcTy->isIntOrIntVectorTy())
    return nullptr;

  if (Instruction *Ret = foldLogicCastConstant(I, Cast0, Builder))
    return Ret;

  CastInst *Cast1 = dyn_cast<CastInst>(Op1);
  if (!Cast1)
    return nullptr;

  // Both operands of the logic operation are casts. The casts must be of the
  // same type for reduction.
  auto CastOpcode = Cast0->getOpcode();
  if (CastOpcode != Cast1->getOpcode() || SrcTy != Cast1->getSrcTy())
    return nullptr;

  Value *Cast0Src = Cast0->getOperand(0);
  Value *Cast1Src = Cast1->getOperand(0);

  // fold logic(cast(A), cast(B)) -> cast(logic(A, B))
  if (shouldOptimizeCast(Cast0) && shouldOptimizeCast(Cast1)) {
    Value *NewOp = Builder.CreateBinOp(LogicOpc, Cast0Src, Cast1Src,
                                        I.getName());
    return CastInst::Create(CastOpcode, NewOp, DestTy);
  }

  // For now, only 'and'/'or' have optimizations after this.
  if (LogicOpc == Instruction::Xor)
    return nullptr;

  // If this is logic(cast(icmp), cast(icmp)), try to fold this even if the
  // cast is otherwise not optimizable.  This happens for vector sexts.
  ICmpInst *ICmp0 = dyn_cast<ICmpInst>(Cast0Src);
  ICmpInst *ICmp1 = dyn_cast<ICmpInst>(Cast1Src);
  if (ICmp0 && ICmp1) {
    Value *Res = LogicOpc == Instruction::And ? foldAndOfICmps(ICmp0, ICmp1, I)
                                              : foldOrOfICmps(ICmp0, ICmp1, I);
    if (Res)
      return CastInst::Create(CastOpcode, Res, DestTy);
    return nullptr;
  }

  // If this is logic(cast(fcmp), cast(fcmp)), try to fold this even if the
  // cast is otherwise not optimizable.  This happens for vector sexts.
  FCmpInst *FCmp0 = dyn_cast<FCmpInst>(Cast0Src);
  FCmpInst *FCmp1 = dyn_cast<FCmpInst>(Cast1Src);
  if (FCmp0 && FCmp1)
    if (Value *R = foldLogicOfFCmps(FCmp0, FCmp1, LogicOpc == Instruction::And))
      return CastInst::Create(CastOpcode, R, DestTy);

  return nullptr;
}

static Instruction *foldAndToXor(BinaryOperator &I,
                                 InstCombiner::BuilderTy &Builder) {
  assert(I.getOpcode() == Instruction::And);
  Value *Op0 = I.getOperand(0);
  Value *Op1 = I.getOperand(1);
  Value *A, *B;

  // Operand complexity canonicalization guarantees that the 'or' is Op0.
  // (A | B) & ~(A & B) --> A ^ B
  // (A | B) & ~(B & A) --> A ^ B
  if (match(&I, m_BinOp(m_Or(m_Value(A), m_Value(B)),
                        m_Not(m_c_And(m_Deferred(A), m_Deferred(B))))))
    return BinaryOperator::CreateXor(A, B);

  // (A | ~B) & (~A | B) --> ~(A ^ B)
  // (A | ~B) & (B | ~A) --> ~(A ^ B)
  // (~B | A) & (~A | B) --> ~(A ^ B)
  // (~B | A) & (B | ~A) --> ~(A ^ B)
  if (Op0->hasOneUse() || Op1->hasOneUse())
    if (match(&I, m_BinOp(m_c_Or(m_Value(A), m_Not(m_Value(B))),
                          m_c_Or(m_Not(m_Deferred(A)), m_Deferred(B)))))
      return BinaryOperator::CreateNot(Builder.CreateXor(A, B));

  return nullptr;
}

static Instruction *foldOrToXor(BinaryOperator &I,
                                InstCombiner::BuilderTy &Builder) {
  assert(I.getOpcode() == Instruction::Or);
  Value *Op0 = I.getOperand(0);
  Value *Op1 = I.getOperand(1);
  Value *A, *B;

  // Operand complexity canonicalization guarantees that the 'and' is Op0.
  // (A & B) | ~(A | B) --> ~(A ^ B)
  // (A & B) | ~(B | A) --> ~(A ^ B)
  if (Op0->hasOneUse() || Op1->hasOneUse())
    if (match(Op0, m_And(m_Value(A), m_Value(B))) &&
        match(Op1, m_Not(m_c_Or(m_Specific(A), m_Specific(B)))))
      return BinaryOperator::CreateNot(Builder.CreateXor(A, B));

  // (A & ~B) | (~A & B) --> A ^ B
  // (A & ~B) | (B & ~A) --> A ^ B
  // (~B & A) | (~A & B) --> A ^ B
  // (~B & A) | (B & ~A) --> A ^ B
  if (match(Op0, m_c_And(m_Value(A), m_Not(m_Value(B)))) &&
      match(Op1, m_c_And(m_Not(m_Specific(A)), m_Specific(B))))
    return BinaryOperator::CreateXor(A, B);

  return nullptr;
}

/// Return true if a constant shift amount is always less than the specified
/// bit-width. If not, the shift could create poison in the narrower type.
static bool canNarrowShiftAmt(Constant *C, unsigned BitWidth) {
  if (auto *ScalarC = dyn_cast<ConstantInt>(C))
    return ScalarC->getZExtValue() < BitWidth;

  if (C->getType()->isVectorTy()) {
    // Check each element of a constant vector.
    unsigned NumElts = C->getType()->getVectorNumElements();
    for (unsigned i = 0; i != NumElts; ++i) {
      Constant *Elt = C->getAggregateElement(i);
      if (!Elt)
        return false;
      if (isa<UndefValue>(Elt))
        continue;
      auto *CI = dyn_cast<ConstantInt>(Elt);
      if (!CI || CI->getZExtValue() >= BitWidth)
        return false;
    }
    return true;
  }

  // The constant is a constant expression or unknown.
  return false;
}

/// Try to use narrower ops (sink zext ops) for an 'and' with binop operand and
/// a common zext operand: and (binop (zext X), C), (zext X).
Instruction *InstCombiner::narrowMaskedBinOp(BinaryOperator &And) {
  // This transform could also apply to {or, and, xor}, but there are better
  // folds for those cases, so we don't expect those patterns here. AShr is not
  // handled because it should always be transformed to LShr in this sequence.
  // The subtract transform is different because it has a constant on the left.
  // Add/mul commute the constant to RHS; sub with constant RHS becomes add.
  Value *Op0 = And.getOperand(0), *Op1 = And.getOperand(1);
  Constant *C;
  if (!match(Op0, m_OneUse(m_Add(m_Specific(Op1), m_Constant(C)))) &&
      !match(Op0, m_OneUse(m_Mul(m_Specific(Op1), m_Constant(C)))) &&
      !match(Op0, m_OneUse(m_LShr(m_Specific(Op1), m_Constant(C)))) &&
      !match(Op0, m_OneUse(m_Shl(m_Specific(Op1), m_Constant(C)))) &&
      !match(Op0, m_OneUse(m_Sub(m_Constant(C), m_Specific(Op1)))))
    return nullptr;

  Value *X;
  if (!match(Op1, m_ZExt(m_Value(X))) || Op1->hasNUsesOrMore(3))
    return nullptr;

  Type *Ty = And.getType();
  if (!isa<VectorType>(Ty) && !shouldChangeType(Ty, X->getType()))
    return nullptr;

  // If we're narrowing a shift, the shift amount must be safe (less than the
  // width) in the narrower type. If the shift amount is greater, instsimplify
  // usually handles that case, but we can't guarantee/assert it.
  Instruction::BinaryOps Opc = cast<BinaryOperator>(Op0)->getOpcode();
  if (Opc == Instruction::LShr || Opc == Instruction::Shl)
    if (!canNarrowShiftAmt(C, X->getType()->getScalarSizeInBits()))
      return nullptr;

  // and (sub C, (zext X)), (zext X) --> zext (and (sub C', X), X)
  // and (binop (zext X), C), (zext X) --> zext (and (binop X, C'), X)
  Value *NewC = ConstantExpr::getTrunc(C, X->getType());
  Value *NewBO = Opc == Instruction::Sub ? Builder.CreateBinOp(Opc, NewC, X)
                                         : Builder.CreateBinOp(Opc, X, NewC);
  return new ZExtInst(Builder.CreateAnd(NewBO, X), Ty);
}

// FIXME: We use commutative matchers (m_c_*) for some, but not all, matches
// here. We should standardize that construct where it is needed or choose some
// other way to ensure that commutated variants of patterns are not missed.
Instruction *InstCombiner::visitAnd(BinaryOperator &I) {
  if (Value *V = SimplifyAndInst(I.getOperand(0), I.getOperand(1),
                                 SQ.getWithInstruction(&I)))
    return replaceInstUsesWith(I, V);

  if (SimplifyAssociativeOrCommutative(I))
    return &I;

  if (Instruction *X = foldVectorBinop(I))
    return X;

  // See if we can simplify any instructions used by the instruction whose sole
  // purpose is to compute bits we don't care about.
  if (SimplifyDemandedInstructionBits(I))
    return &I;

  // Do this before using distributive laws to catch simple and/or/not patterns.
  if (Instruction *Xor = foldAndToXor(I, Builder))
    return Xor;

  // (A|B)&(A|C) -> A|(B&C) etc
  if (Value *V = SimplifyUsingDistributiveLaws(I))
    return replaceInstUsesWith(I, V);

  if (Value *V = SimplifyBSwap(I, Builder))
    return replaceInstUsesWith(I, V);

  Value *Op0 = I.getOperand(0), *Op1 = I.getOperand(1);
  const APInt *C;
  if (match(Op1, m_APInt(C))) {
    Value *X, *Y;
    if (match(Op0, m_OneUse(m_LogicalShift(m_One(), m_Value(X)))) &&
        C->isOneValue()) {
      // (1 << X) & 1 --> zext(X == 0)
      // (1 >> X) & 1 --> zext(X == 0)
      Value *IsZero = Builder.CreateICmpEQ(X, ConstantInt::get(I.getType(), 0));
      return new ZExtInst(IsZero, I.getType());
    }

    const APInt *XorC;
    if (match(Op0, m_OneUse(m_Xor(m_Value(X), m_APInt(XorC))))) {
      // (X ^ C1) & C2 --> (X & C2) ^ (C1&C2)
      Constant *NewC = ConstantInt::get(I.getType(), *C & *XorC);
      Value *And = Builder.CreateAnd(X, Op1);
      And->takeName(Op0);
      return BinaryOperator::CreateXor(And, NewC);
    }

    const APInt *OrC;
    if (match(Op0, m_OneUse(m_Or(m_Value(X), m_APInt(OrC))))) {
      // (X | C1) & C2 --> (X & C2^(C1&C2)) | (C1&C2)
      // NOTE: This reduces the number of bits set in the & mask, which
      // can expose opportunities for store narrowing for scalars.
      // NOTE: SimplifyDemandedBits should have already removed bits from C1
      // that aren't set in C2. Meaning we can replace (C1&C2) with C1 in
      // above, but this feels safer.
      APInt Together = *C & *OrC;
      Value *And = Builder.CreateAnd(X, ConstantInt::get(I.getType(),
                                                         Together ^ *C));
      And->takeName(Op0);
      return BinaryOperator::CreateOr(And, ConstantInt::get(I.getType(),
                                                            Together));
    }

    // If the mask is only needed on one incoming arm, push the 'and' op up.
    if (match(Op0, m_OneUse(m_Xor(m_Value(X), m_Value(Y)))) ||
        match(Op0, m_OneUse(m_Or(m_Value(X), m_Value(Y))))) {
      APInt NotAndMask(~(*C));
      BinaryOperator::BinaryOps BinOp = cast<BinaryOperator>(Op0)->getOpcode();
      if (MaskedValueIsZero(X, NotAndMask, 0, &I)) {
        // Not masking anything out for the LHS, move mask to RHS.
        // and ({x}or X, Y), C --> {x}or X, (and Y, C)
        Value *NewRHS = Builder.CreateAnd(Y, Op1, Y->getName() + ".masked");
        return BinaryOperator::Create(BinOp, X, NewRHS);
      }
      if (!isa<Constant>(Y) && MaskedValueIsZero(Y, NotAndMask, 0, &I)) {
        // Not masking anything out for the RHS, move mask to LHS.
        // and ({x}or X, Y), C --> {x}or (and X, C), Y
        Value *NewLHS = Builder.CreateAnd(X, Op1, X->getName() + ".masked");
        return BinaryOperator::Create(BinOp, NewLHS, Y);
      }
    }

  }

  if (ConstantInt *AndRHS = dyn_cast<ConstantInt>(Op1)) {
    const APInt &AndRHSMask = AndRHS->getValue();

    // Optimize a variety of ((val OP C1) & C2) combinations...
    if (BinaryOperator *Op0I = dyn_cast<BinaryOperator>(Op0)) {
      // ((C1 OP zext(X)) & C2) -> zext((C1-X) & C2) if C2 fits in the bitwidth
      // of X and OP behaves well when given trunc(C1) and X.
      switch (Op0I->getOpcode()) {
      default:
        break;
      case Instruction::Xor:
      case Instruction::Or:
      case Instruction::Mul:
      case Instruction::Add:
      case Instruction::Sub:
        Value *X;
        ConstantInt *C1;
        if (match(Op0I, m_c_BinOp(m_ZExt(m_Value(X)), m_ConstantInt(C1)))) {
          if (AndRHSMask.isIntN(X->getType()->getScalarSizeInBits())) {
            auto *TruncC1 = ConstantExpr::getTrunc(C1, X->getType());
            Value *BinOp;
            Value *Op0LHS = Op0I->getOperand(0);
            if (isa<ZExtInst>(Op0LHS))
              BinOp = Builder.CreateBinOp(Op0I->getOpcode(), X, TruncC1);
            else
              BinOp = Builder.CreateBinOp(Op0I->getOpcode(), TruncC1, X);
            auto *TruncC2 = ConstantExpr::getTrunc(AndRHS, X->getType());
            auto *And = Builder.CreateAnd(BinOp, TruncC2);
            return new ZExtInst(And, I.getType());
          }
        }
      }

      if (ConstantInt *Op0CI = dyn_cast<ConstantInt>(Op0I->getOperand(1)))
        if (Instruction *Res = OptAndOp(Op0I, Op0CI, AndRHS, I))
          return Res;
    }

    // If this is an integer truncation, and if the source is an 'and' with
    // immediate, transform it.  This frequently occurs for bitfield accesses.
    {
      Value *X = nullptr; ConstantInt *YC = nullptr;
      if (match(Op0, m_Trunc(m_And(m_Value(X), m_ConstantInt(YC))))) {
        // Change: and (trunc (and X, YC) to T), C2
        // into  : and (trunc X to T), trunc(YC) & C2
        // This will fold the two constants together, which may allow
        // other simplifications.
        Value *NewCast = Builder.CreateTrunc(X, I.getType(), "and.shrunk");
        Constant *C3 = ConstantExpr::getTrunc(YC, I.getType());
        C3 = ConstantExpr::getAnd(C3, AndRHS);
        return BinaryOperator::CreateAnd(NewCast, C3);
      }
    }
  }

  if (Instruction *Z = narrowMaskedBinOp(I))
    return Z;

  if (Instruction *FoldedLogic = foldBinOpIntoSelectOrPhi(I))
    return FoldedLogic;

  if (Instruction *DeMorgan = matchDeMorgansLaws(I, Builder))
    return DeMorgan;

  {
    Value *A, *B, *C;
    // A & (A ^ B) --> A & ~B
    if (match(Op1, m_OneUse(m_c_Xor(m_Specific(Op0), m_Value(B)))))
      return BinaryOperator::CreateAnd(Op0, Builder.CreateNot(B));
    // (A ^ B) & A --> A & ~B
    if (match(Op0, m_OneUse(m_c_Xor(m_Specific(Op1), m_Value(B)))))
      return BinaryOperator::CreateAnd(Op1, Builder.CreateNot(B));

    // (A ^ B) & ((B ^ C) ^ A) -> (A ^ B) & ~C
    if (match(Op0, m_Xor(m_Value(A), m_Value(B))))
      if (match(Op1, m_Xor(m_Xor(m_Specific(B), m_Value(C)), m_Specific(A))))
        if (Op1->hasOneUse() || IsFreeToInvert(C, C->hasOneUse()))
          return BinaryOperator::CreateAnd(Op0, Builder.CreateNot(C));

    // ((A ^ C) ^ B) & (B ^ A) -> (B ^ A) & ~C
    if (match(Op0, m_Xor(m_Xor(m_Value(A), m_Value(C)), m_Value(B))))
      if (match(Op1, m_Xor(m_Specific(B), m_Specific(A))))
        if (Op0->hasOneUse() || IsFreeToInvert(C, C->hasOneUse()))
          return BinaryOperator::CreateAnd(Op1, Builder.CreateNot(C));

    // (A | B) & ((~A) ^ B) -> (A & B)
    // (A | B) & (B ^ (~A)) -> (A & B)
    // (B | A) & ((~A) ^ B) -> (A & B)
    // (B | A) & (B ^ (~A)) -> (A & B)
    if (match(Op1, m_c_Xor(m_Not(m_Value(A)), m_Value(B))) &&
        match(Op0, m_c_Or(m_Specific(A), m_Specific(B))))
      return BinaryOperator::CreateAnd(A, B);

    // ((~A) ^ B) & (A | B) -> (A & B)
    // ((~A) ^ B) & (B | A) -> (A & B)
    // (B ^ (~A)) & (A | B) -> (A & B)
    // (B ^ (~A)) & (B | A) -> (A & B)
    if (match(Op0, m_c_Xor(m_Not(m_Value(A)), m_Value(B))) &&
        match(Op1, m_c_Or(m_Specific(A), m_Specific(B))))
      return BinaryOperator::CreateAnd(A, B);
  }

  {
    ICmpInst *LHS = dyn_cast<ICmpInst>(Op0);
    ICmpInst *RHS = dyn_cast<ICmpInst>(Op1);
    if (LHS && RHS)
      if (Value *Res = foldAndOfICmps(LHS, RHS, I))
        return replaceInstUsesWith(I, Res);

    // TODO: Make this recursive; it's a little tricky because an arbitrary
    // number of 'and' instructions might have to be created.
    Value *X, *Y;
    if (LHS && match(Op1, m_OneUse(m_And(m_Value(X), m_Value(Y))))) {
      if (auto *Cmp = dyn_cast<ICmpInst>(X))
        if (Value *Res = foldAndOfICmps(LHS, Cmp, I))
          return replaceInstUsesWith(I, Builder.CreateAnd(Res, Y));
      if (auto *Cmp = dyn_cast<ICmpInst>(Y))
        if (Value *Res = foldAndOfICmps(LHS, Cmp, I))
          return replaceInstUsesWith(I, Builder.CreateAnd(Res, X));
    }
    if (RHS && match(Op0, m_OneUse(m_And(m_Value(X), m_Value(Y))))) {
      if (auto *Cmp = dyn_cast<ICmpInst>(X))
        if (Value *Res = foldAndOfICmps(Cmp, RHS, I))
          return replaceInstUsesWith(I, Builder.CreateAnd(Res, Y));
      if (auto *Cmp = dyn_cast<ICmpInst>(Y))
        if (Value *Res = foldAndOfICmps(Cmp, RHS, I))
          return replaceInstUsesWith(I, Builder.CreateAnd(Res, X));
    }
  }

  if (FCmpInst *LHS = dyn_cast<FCmpInst>(I.getOperand(0)))
    if (FCmpInst *RHS = dyn_cast<FCmpInst>(I.getOperand(1)))
      if (Value *Res = foldLogicOfFCmps(LHS, RHS, true))
        return replaceInstUsesWith(I, Res);

  if (Instruction *CastedAnd = foldCastedBitwiseLogic(I))
    return CastedAnd;

  // and(sext(A), B) / and(B, sext(A)) --> A ? B : 0, where A is i1 or <N x i1>.
  Value *A;
  if (match(Op0, m_OneUse(m_SExt(m_Value(A)))) &&
      A->getType()->isIntOrIntVectorTy(1))
    return SelectInst::Create(A, Op1, Constant::getNullValue(I.getType()));
  if (match(Op1, m_OneUse(m_SExt(m_Value(A)))) &&
      A->getType()->isIntOrIntVectorTy(1))
    return SelectInst::Create(A, Op0, Constant::getNullValue(I.getType()));

  return nullptr;
}

Instruction *InstCombiner::matchBSwap(BinaryOperator &Or) {
  assert(Or.getOpcode() == Instruction::Or && "bswap requires an 'or'");
  Value *Op0 = Or.getOperand(0), *Op1 = Or.getOperand(1);

  // Look through zero extends.
  if (Instruction *Ext = dyn_cast<ZExtInst>(Op0))
    Op0 = Ext->getOperand(0);

  if (Instruction *Ext = dyn_cast<ZExtInst>(Op1))
    Op1 = Ext->getOperand(0);

  // (A | B) | C  and  A | (B | C)                  -> bswap if possible.
  bool OrOfOrs = match(Op0, m_Or(m_Value(), m_Value())) ||
                 match(Op1, m_Or(m_Value(), m_Value()));

  // (A >> B) | (C << D)  and  (A << B) | (B >> C)  -> bswap if possible.
  bool OrOfShifts = match(Op0, m_LogicalShift(m_Value(), m_Value())) &&
                    match(Op1, m_LogicalShift(m_Value(), m_Value()));

  // (A & B) | (C & D)                              -> bswap if possible.
  bool OrOfAnds = match(Op0, m_And(m_Value(), m_Value())) &&
                  match(Op1, m_And(m_Value(), m_Value()));

  // (A << B) | (C & D)                              -> bswap if possible.
  // The bigger pattern here is ((A & C1) << C2) | ((B >> C2) & C1), which is a
  // part of the bswap idiom for specific values of C1, C2 (e.g. C1 = 16711935,
  // C2 = 8 for i32).
  // This pattern can occur when the operands of the 'or' are not canonicalized
  // for some reason (not having only one use, for example).
  bool OrOfAndAndSh = (match(Op0, m_LogicalShift(m_Value(), m_Value())) &&
                       match(Op1, m_And(m_Value(), m_Value()))) ||
                      (match(Op0, m_And(m_Value(), m_Value())) &&
                       match(Op1, m_LogicalShift(m_Value(), m_Value())));

  if (!OrOfOrs && !OrOfShifts && !OrOfAnds && !OrOfAndAndSh)
    return nullptr;

  SmallVector<Instruction*, 4> Insts;
  if (!recognizeBSwapOrBitReverseIdiom(&Or, true, false, Insts))
    return nullptr;
  Instruction *LastInst = Insts.pop_back_val();
  LastInst->removeFromParent();

  for (auto *Inst : Insts)
    Worklist.Add(Inst);
  return LastInst;
}

/// Transform UB-safe variants of bitwise rotate to the funnel shift intrinsic.
static Instruction *matchRotate(Instruction &Or) {
  // TODO: Can we reduce the code duplication between this and the related
  // rotate matching code under visitSelect and visitTrunc?
  unsigned Width = Or.getType()->getScalarSizeInBits();
  if (!isPowerOf2_32(Width))
    return nullptr;

  // First, find an or'd pair of opposite shifts with the same shifted operand:
  // or (lshr ShVal, ShAmt0), (shl ShVal, ShAmt1)
  Value *Or0 = Or.getOperand(0), *Or1 = Or.getOperand(1);
  Value *ShVal, *ShAmt0, *ShAmt1;
  if (!match(Or0, m_OneUse(m_LogicalShift(m_Value(ShVal), m_Value(ShAmt0)))) ||
      !match(Or1, m_OneUse(m_LogicalShift(m_Specific(ShVal), m_Value(ShAmt1)))))
    return nullptr;

  auto ShiftOpcode0 = cast<BinaryOperator>(Or0)->getOpcode();
  auto ShiftOpcode1 = cast<BinaryOperator>(Or1)->getOpcode();
  if (ShiftOpcode0 == ShiftOpcode1)
    return nullptr;

  // Match the shift amount operands for a rotate pattern. This always matches
  // a subtraction on the R operand.
  auto matchShiftAmount = [](Value *L, Value *R, unsigned Width) -> Value * {
    // The shift amount may be masked with negation:
    // (shl ShVal, (X & (Width - 1))) | (lshr ShVal, ((-X) & (Width - 1)))
    Value *X;
    unsigned Mask = Width - 1;
    if (match(L, m_And(m_Value(X), m_SpecificInt(Mask))) &&
        match(R, m_And(m_Neg(m_Specific(X)), m_SpecificInt(Mask))))
      return X;

    return nullptr;
  };

  Value *ShAmt = matchShiftAmount(ShAmt0, ShAmt1, Width);
  bool SubIsOnLHS = false;
  if (!ShAmt) {
    ShAmt = matchShiftAmount(ShAmt1, ShAmt0, Width);
    SubIsOnLHS = true;
  }
  if (!ShAmt)
    return nullptr;

  bool IsFshl = (!SubIsOnLHS && ShiftOpcode0 == BinaryOperator::Shl) ||
                (SubIsOnLHS && ShiftOpcode1 == BinaryOperator::Shl);
  Intrinsic::ID IID = IsFshl ? Intrinsic::fshl : Intrinsic::fshr;
  Function *F = Intrinsic::getDeclaration(Or.getModule(), IID, Or.getType());
  return IntrinsicInst::Create(F, { ShVal, ShVal, ShAmt });
}

/// If all elements of two constant vectors are 0/-1 and inverses, return true.
static bool areInverseVectorBitmasks(Constant *C1, Constant *C2) {
  unsigned NumElts = C1->getType()->getVectorNumElements();
  for (unsigned i = 0; i != NumElts; ++i) {
    Constant *EltC1 = C1->getAggregateElement(i);
    Constant *EltC2 = C2->getAggregateElement(i);
    if (!EltC1 || !EltC2)
      return false;

    // One element must be all ones, and the other must be all zeros.
    if (!((match(EltC1, m_Zero()) && match(EltC2, m_AllOnes())) ||
          (match(EltC2, m_Zero()) && match(EltC1, m_AllOnes()))))
      return false;
  }
  return true;
}

/// We have an expression of the form (A & C) | (B & D). If A is a scalar or
/// vector composed of all-zeros or all-ones values and is the bitwise 'not' of
/// B, it can be used as the condition operand of a select instruction.
Value *InstCombiner::getSelectCondition(Value *A, Value *B) {
  // Step 1: We may have peeked through bitcasts in the caller.
  // Exit immediately if we don't have (vector) integer types.
  Type *Ty = A->getType();
  if (!Ty->isIntOrIntVectorTy() || !B->getType()->isIntOrIntVectorTy())
    return nullptr;

  // Step 2: We need 0 or all-1's bitmasks.
  if (ComputeNumSignBits(A) != Ty->getScalarSizeInBits())
    return nullptr;

  // Step 3: If B is the 'not' value of A, we have our answer.
  if (match(A, m_Not(m_Specific(B)))) {
    // If these are scalars or vectors of i1, A can be used directly.
    if (Ty->isIntOrIntVectorTy(1))
      return A;
    return Builder.CreateTrunc(A, CmpInst::makeCmpResultType(Ty));
  }

  // If both operands are constants, see if the constants are inverse bitmasks.
  Constant *AConst, *BConst;
  if (match(A, m_Constant(AConst)) && match(B, m_Constant(BConst)))
    if (AConst == ConstantExpr::getNot(BConst))
      return Builder.CreateZExtOrTrunc(A, CmpInst::makeCmpResultType(Ty));

  // Look for more complex patterns. The 'not' op may be hidden behind various
  // casts. Look through sexts and bitcasts to find the booleans.
  Value *Cond;
  Value *NotB;
  if (match(A, m_SExt(m_Value(Cond))) &&
      Cond->getType()->isIntOrIntVectorTy(1) &&
      match(B, m_OneUse(m_Not(m_Value(NotB))))) {
    NotB = peekThroughBitcast(NotB, true);
    if (match(NotB, m_SExt(m_Specific(Cond))))
      return Cond;
  }

  // All scalar (and most vector) possibilities should be handled now.
  // Try more matches that only apply to non-splat constant vectors.
  if (!Ty->isVectorTy())
    return nullptr;

  // If both operands are xor'd with constants using the same sexted boolean
  // operand, see if the constants are inverse bitmasks.
  // TODO: Use ConstantExpr::getNot()?
  if (match(A, (m_Xor(m_SExt(m_Value(Cond)), m_Constant(AConst)))) &&
      match(B, (m_Xor(m_SExt(m_Specific(Cond)), m_Constant(BConst)))) &&
      Cond->getType()->isIntOrIntVectorTy(1) &&
      areInverseVectorBitmasks(AConst, BConst)) {
    AConst = ConstantExpr::getTrunc(AConst, CmpInst::makeCmpResultType(Ty));
    return Builder.CreateXor(Cond, AConst);
  }
  return nullptr;
}

/// We have an expression of the form (A & C) | (B & D). Try to simplify this
/// to "A' ? C : D", where A' is a boolean or vector of booleans.
Value *InstCombiner::matchSelectFromAndOr(Value *A, Value *C, Value *B,
                                          Value *D) {
  // The potential condition of the select may be bitcasted. In that case, look
  // through its bitcast and the corresponding bitcast of the 'not' condition.
  Type *OrigType = A->getType();
  A = peekThroughBitcast(A, true);
  B = peekThroughBitcast(B, true);
  if (Value *Cond = getSelectCondition(A, B)) {
    // ((bc Cond) & C) | ((bc ~Cond) & D) --> bc (select Cond, (bc C), (bc D))
    // The bitcasts will either all exist or all not exist. The builder will
    // not create unnecessary casts if the types already match.
    Value *BitcastC = Builder.CreateBitCast(C, A->getType());
    Value *BitcastD = Builder.CreateBitCast(D, A->getType());
    Value *Select = Builder.CreateSelect(Cond, BitcastC, BitcastD);
    return Builder.CreateBitCast(Select, OrigType);
  }

  return nullptr;
}

/// Fold (icmp)|(icmp) if possible.
Value *InstCombiner::foldOrOfICmps(ICmpInst *LHS, ICmpInst *RHS,
                                   Instruction &CxtI) {
  // Fold (iszero(A & K1) | iszero(A & K2)) ->  (A & (K1 | K2)) != (K1 | K2)
  // if K1 and K2 are a one-bit mask.
  if (Value *V = foldAndOrOfICmpsOfAndWithPow2(LHS, RHS, false, CxtI))
    return V;

  ICmpInst::Predicate PredL = LHS->getPredicate(), PredR = RHS->getPredicate();

  ConstantInt *LHSC = dyn_cast<ConstantInt>(LHS->getOperand(1));
  ConstantInt *RHSC = dyn_cast<ConstantInt>(RHS->getOperand(1));

  // Fold (icmp ult/ule (A + C1), C3) | (icmp ult/ule (A + C2), C3)
  //                   -->  (icmp ult/ule ((A & ~(C1 ^ C2)) + max(C1, C2)), C3)
  // The original condition actually refers to the following two ranges:
  // [MAX_UINT-C1+1, MAX_UINT-C1+1+C3] and [MAX_UINT-C2+1, MAX_UINT-C2+1+C3]
  // We can fold these two ranges if:
  // 1) C1 and C2 is unsigned greater than C3.
  // 2) The two ranges are separated.
  // 3) C1 ^ C2 is one-bit mask.
  // 4) LowRange1 ^ LowRange2 and HighRange1 ^ HighRange2 are one-bit mask.
  // This implies all values in the two ranges differ by exactly one bit.

  if ((PredL == ICmpInst::ICMP_ULT || PredL == ICmpInst::ICMP_ULE) &&
      PredL == PredR && LHSC && RHSC && LHS->hasOneUse() && RHS->hasOneUse() &&
      LHSC->getType() == RHSC->getType() &&
      LHSC->getValue() == (RHSC->getValue())) {

    Value *LAdd = LHS->getOperand(0);
    Value *RAdd = RHS->getOperand(0);

    Value *LAddOpnd, *RAddOpnd;
    ConstantInt *LAddC, *RAddC;
    if (match(LAdd, m_Add(m_Value(LAddOpnd), m_ConstantInt(LAddC))) &&
        match(RAdd, m_Add(m_Value(RAddOpnd), m_ConstantInt(RAddC))) &&
        LAddC->getValue().ugt(LHSC->getValue()) &&
        RAddC->getValue().ugt(LHSC->getValue())) {

      APInt DiffC = LAddC->getValue() ^ RAddC->getValue();
      if (LAddOpnd == RAddOpnd && DiffC.isPowerOf2()) {
        ConstantInt *MaxAddC = nullptr;
        if (LAddC->getValue().ult(RAddC->getValue()))
          MaxAddC = RAddC;
        else
          MaxAddC = LAddC;

        APInt RRangeLow = -RAddC->getValue();
        APInt RRangeHigh = RRangeLow + LHSC->getValue();
        APInt LRangeLow = -LAddC->getValue();
        APInt LRangeHigh = LRangeLow + LHSC->getValue();
        APInt LowRangeDiff = RRangeLow ^ LRangeLow;
        APInt HighRangeDiff = RRangeHigh ^ LRangeHigh;
        APInt RangeDiff = LRangeLow.sgt(RRangeLow) ? LRangeLow - RRangeLow
                                                   : RRangeLow - LRangeLow;

        if (LowRangeDiff.isPowerOf2() && LowRangeDiff == HighRangeDiff &&
            RangeDiff.ugt(LHSC->getValue())) {
          Value *MaskC = ConstantInt::get(LAddC->getType(), ~DiffC);

          Value *NewAnd = Builder.CreateAnd(LAddOpnd, MaskC);
          Value *NewAdd = Builder.CreateAdd(NewAnd, MaxAddC);
          return Builder.CreateICmp(LHS->getPredicate(), NewAdd, LHSC);
        }
      }
    }
  }

  // (icmp1 A, B) | (icmp2 A, B) --> (icmp3 A, B)
  if (predicatesFoldable(PredL, PredR)) {
    if (LHS->getOperand(0) == RHS->getOperand(1) &&
        LHS->getOperand(1) == RHS->getOperand(0))
      LHS->swapOperands();
    if (LHS->getOperand(0) == RHS->getOperand(0) &&
        LHS->getOperand(1) == RHS->getOperand(1)) {
      Value *Op0 = LHS->getOperand(0), *Op1 = LHS->getOperand(1);
      unsigned Code = getICmpCode(LHS) | getICmpCode(RHS);
      bool IsSigned = LHS->isSigned() || RHS->isSigned();
      return getNewICmpValue(Code, IsSigned, Op0, Op1, Builder);
    }
  }

  // handle (roughly):
  // (icmp ne (A & B), C) | (icmp ne (A & D), E)
  if (Value *V = foldLogOpOfMaskedICmps(LHS, RHS, false, Builder))
    return V;

  Value *LHS0 = LHS->getOperand(0), *RHS0 = RHS->getOperand(0);
  if (LHS->hasOneUse() || RHS->hasOneUse()) {
    // (icmp eq B, 0) | (icmp ult A, B) -> (icmp ule A, B-1)
    // (icmp eq B, 0) | (icmp ugt B, A) -> (icmp ule A, B-1)
    Value *A = nullptr, *B = nullptr;
    if (PredL == ICmpInst::ICMP_EQ && LHSC && LHSC->isZero()) {
      B = LHS0;
      if (PredR == ICmpInst::ICMP_ULT && LHS0 == RHS->getOperand(1))
        A = RHS0;
      else if (PredR == ICmpInst::ICMP_UGT && LHS0 == RHS0)
        A = RHS->getOperand(1);
    }
    // (icmp ult A, B) | (icmp eq B, 0) -> (icmp ule A, B-1)
    // (icmp ugt B, A) | (icmp eq B, 0) -> (icmp ule A, B-1)
    else if (PredR == ICmpInst::ICMP_EQ && RHSC && RHSC->isZero()) {
      B = RHS0;
      if (PredL == ICmpInst::ICMP_ULT && RHS0 == LHS->getOperand(1))
        A = LHS0;
      else if (PredL == ICmpInst::ICMP_UGT && LHS0 == RHS0)
        A = LHS->getOperand(1);
    }
    if (A && B)
      return Builder.CreateICmp(
          ICmpInst::ICMP_UGE,
          Builder.CreateAdd(B, ConstantInt::getSigned(B->getType(), -1)), A);
  }

  // E.g. (icmp slt x, 0) | (icmp sgt x, n) --> icmp ugt x, n
  if (Value *V = simplifyRangeCheck(LHS, RHS, /*Inverted=*/true))
    return V;

  // E.g. (icmp sgt x, n) | (icmp slt x, 0) --> icmp ugt x, n
  if (Value *V = simplifyRangeCheck(RHS, LHS, /*Inverted=*/true))
    return V;

  if (Value *V = foldAndOrOfEqualityCmpsWithConstants(LHS, RHS, false, Builder))
    return V;

  // This only handles icmp of constants: (icmp1 A, C1) | (icmp2 B, C2).
  if (!LHSC || !RHSC)
    return nullptr;

  if (LHSC == RHSC && PredL == PredR) {
    // (icmp ne A, 0) | (icmp ne B, 0) --> (icmp ne (A|B), 0)
    if (PredL == ICmpInst::ICMP_NE && LHSC->isZero()) {
      Value *NewOr = Builder.CreateOr(LHS0, RHS0);
      return Builder.CreateICmp(PredL, NewOr, LHSC);
    }
  }

  // (icmp ult (X + CA), C1) | (icmp eq X, C2) -> (icmp ule (X + CA), C1)
  //   iff C2 + CA == C1.
  if (PredL == ICmpInst::ICMP_ULT && PredR == ICmpInst::ICMP_EQ) {
    ConstantInt *AddC;
    if (match(LHS0, m_Add(m_Specific(RHS0), m_ConstantInt(AddC))))
      if (RHSC->getValue() + AddC->getValue() == LHSC->getValue())
        return Builder.CreateICmpULE(LHS0, LHSC);
  }

  // From here on, we only handle:
  //    (icmp1 A, C1) | (icmp2 A, C2) --> something simpler.
  if (LHS0 != RHS0)
    return nullptr;

  // ICMP_[US][GL]E X, C is folded to ICMP_[US][GL]T elsewhere.
  if (PredL == ICmpInst::ICMP_UGE || PredL == ICmpInst::ICMP_ULE ||
      PredR == ICmpInst::ICMP_UGE || PredR == ICmpInst::ICMP_ULE ||
      PredL == ICmpInst::ICMP_SGE || PredL == ICmpInst::ICMP_SLE ||
      PredR == ICmpInst::ICMP_SGE || PredR == ICmpInst::ICMP_SLE)
    return nullptr;

  // We can't fold (ugt x, C) | (sgt x, C2).
  if (!predicatesFoldable(PredL, PredR))
    return nullptr;

  // Ensure that the larger constant is on the RHS.
  bool ShouldSwap;
  if (CmpInst::isSigned(PredL) ||
      (ICmpInst::isEquality(PredL) && CmpInst::isSigned(PredR)))
    ShouldSwap = LHSC->getValue().sgt(RHSC->getValue());
  else
    ShouldSwap = LHSC->getValue().ugt(RHSC->getValue());

  if (ShouldSwap) {
    std::swap(LHS, RHS);
    std::swap(LHSC, RHSC);
    std::swap(PredL, PredR);
  }

  // At this point, we know we have two icmp instructions
  // comparing a value against two constants and or'ing the result
  // together.  Because of the above check, we know that we only have
  // ICMP_EQ, ICMP_NE, ICMP_LT, and ICMP_GT here. We also know (from the
  // icmp folding check above), that the two constants are not
  // equal.
  assert(LHSC != RHSC && "Compares not folded above?");

  switch (PredL) {
  default:
    llvm_unreachable("Unknown integer condition code!");
  case ICmpInst::ICMP_EQ:
    switch (PredR) {
    default:
      llvm_unreachable("Unknown integer condition code!");
    case ICmpInst::ICMP_EQ:
      // Potential folds for this case should already be handled.
      break;
    case ICmpInst::ICMP_UGT: // (X == 13 | X u> 14) -> no change
    case ICmpInst::ICMP_SGT: // (X == 13 | X s> 14) -> no change
      break;
    }
    break;
  case ICmpInst::ICMP_ULT:
    switch (PredR) {
    default:
      llvm_unreachable("Unknown integer condition code!");
    case ICmpInst::ICMP_EQ: // (X u< 13 | X == 14) -> no change
      break;
    case ICmpInst::ICMP_UGT: // (X u< 13 | X u> 15) -> (X-13) u> 2
      assert(!RHSC->isMaxValue(false) && "Missed icmp simplification");
      return insertRangeTest(LHS0, LHSC->getValue(), RHSC->getValue() + 1,
                             false, false);
    }
    break;
  case ICmpInst::ICMP_SLT:
    switch (PredR) {
    default:
      llvm_unreachable("Unknown integer condition code!");
    case ICmpInst::ICMP_EQ: // (X s< 13 | X == 14) -> no change
      break;
    case ICmpInst::ICMP_SGT: // (X s< 13 | X s> 15) -> (X-13) s> 2
      assert(!RHSC->isMaxValue(true) && "Missed icmp simplification");
      return insertRangeTest(LHS0, LHSC->getValue(), RHSC->getValue() + 1, true,
                             false);
    }
    break;
  }
  return nullptr;
}

// FIXME: We use commutative matchers (m_c_*) for some, but not all, matches
// here. We should standardize that construct where it is needed or choose some
// other way to ensure that commutated variants of patterns are not missed.
Instruction *InstCombiner::visitOr(BinaryOperator &I) {
  if (Value *V = SimplifyOrInst(I.getOperand(0), I.getOperand(1),
                                SQ.getWithInstruction(&I)))
    return replaceInstUsesWith(I, V);

  if (SimplifyAssociativeOrCommutative(I))
    return &I;

  if (Instruction *X = foldVectorBinop(I))
    return X;

  // See if we can simplify any instructions used by the instruction whose sole
  // purpose is to compute bits we don't care about.
  if (SimplifyDemandedInstructionBits(I))
    return &I;

  // Do this before using distributive laws to catch simple and/or/not patterns.
  if (Instruction *Xor = foldOrToXor(I, Builder))
    return Xor;

  // (A&B)|(A&C) -> A&(B|C) etc
  if (Value *V = SimplifyUsingDistributiveLaws(I))
    return replaceInstUsesWith(I, V);

  if (Value *V = SimplifyBSwap(I, Builder))
    return replaceInstUsesWith(I, V);

  if (Instruction *FoldedLogic = foldBinOpIntoSelectOrPhi(I))
    return FoldedLogic;

  if (Instruction *BSwap = matchBSwap(I))
    return BSwap;

  if (Instruction *Rotate = matchRotate(I))
    return Rotate;

  Value *X, *Y;
  const APInt *CV;
  if (match(&I, m_c_Or(m_OneUse(m_Xor(m_Value(X), m_APInt(CV))), m_Value(Y))) &&
      !CV->isAllOnesValue() && MaskedValueIsZero(Y, *CV, 0, &I)) {
    // (X ^ C) | Y -> (X | Y) ^ C iff Y & C == 0
    // The check for a 'not' op is for efficiency (if Y is known zero --> ~X).
    Value *Or = Builder.CreateOr(X, Y);
    return BinaryOperator::CreateXor(Or, ConstantInt::get(I.getType(), *CV));
  }

  // (A & C)|(B & D)
  Value *Op0 = I.getOperand(0), *Op1 = I.getOperand(1);
  Value *A, *B, *C, *D;
  if (match(Op0, m_And(m_Value(A), m_Value(C))) &&
      match(Op1, m_And(m_Value(B), m_Value(D)))) {
    ConstantInt *C1 = dyn_cast<ConstantInt>(C);
    ConstantInt *C2 = dyn_cast<ConstantInt>(D);
    if (C1 && C2) {  // (A & C1)|(B & C2)
      Value *V1 = nullptr, *V2 = nullptr;
      if ((C1->getValue() & C2->getValue()).isNullValue()) {
        // ((V | N) & C1) | (V & C2) --> (V|N) & (C1|C2)
        // iff (C1&C2) == 0 and (N&~C1) == 0
        if (match(A, m_Or(m_Value(V1), m_Value(V2))) &&
            ((V1 == B &&
              MaskedValueIsZero(V2, ~C1->getValue(), 0, &I)) || // (V|N)
             (V2 == B &&
              MaskedValueIsZero(V1, ~C1->getValue(), 0, &I))))  // (N|V)
          return BinaryOperator::CreateAnd(A,
                                Builder.getInt(C1->getValue()|C2->getValue()));
        // Or commutes, try both ways.
        if (match(B, m_Or(m_Value(V1), m_Value(V2))) &&
            ((V1 == A &&
              MaskedValueIsZero(V2, ~C2->getValue(), 0, &I)) || // (V|N)
             (V2 == A &&
              MaskedValueIsZero(V1, ~C2->getValue(), 0, &I))))  // (N|V)
          return BinaryOperator::CreateAnd(B,
                                 Builder.getInt(C1->getValue()|C2->getValue()));

        // ((V|C3)&C1) | ((V|C4)&C2) --> (V|C3|C4)&(C1|C2)
        // iff (C1&C2) == 0 and (C3&~C1) == 0 and (C4&~C2) == 0.
        ConstantInt *C3 = nullptr, *C4 = nullptr;
        if (match(A, m_Or(m_Value(V1), m_ConstantInt(C3))) &&
            (C3->getValue() & ~C1->getValue()).isNullValue() &&
            match(B, m_Or(m_Specific(V1), m_ConstantInt(C4))) &&
            (C4->getValue() & ~C2->getValue()).isNullValue()) {
          V2 = Builder.CreateOr(V1, ConstantExpr::getOr(C3, C4), "bitfield");
          return BinaryOperator::CreateAnd(V2,
                                 Builder.getInt(C1->getValue()|C2->getValue()));
        }
      }

      if (C1->getValue() == ~C2->getValue()) {
        Value *X;

        // ((X|B)&C1)|(B&C2) -> (X&C1) | B iff C1 == ~C2
        if (match(A, m_c_Or(m_Value(X), m_Specific(B))))
          return BinaryOperator::CreateOr(Builder.CreateAnd(X, C1), B);
        // (A&C2)|((X|A)&C1) -> (X&C2) | A iff C1 == ~C2
        if (match(B, m_c_Or(m_Specific(A), m_Value(X))))
          return BinaryOperator::CreateOr(Builder.CreateAnd(X, C2), A);

        // ((X^B)&C1)|(B&C2) -> (X&C1) ^ B iff C1 == ~C2
        if (match(A, m_c_Xor(m_Value(X), m_Specific(B))))
          return BinaryOperator::CreateXor(Builder.CreateAnd(X, C1), B);
        // (A&C2)|((X^A)&C1) -> (X&C2) ^ A iff C1 == ~C2
        if (match(B, m_c_Xor(m_Specific(A), m_Value(X))))
          return BinaryOperator::CreateXor(Builder.CreateAnd(X, C2), A);
      }
    }

    // Don't try to form a select if it's unlikely that we'll get rid of at
    // least one of the operands. A select is generally more expensive than the
    // 'or' that it is replacing.
    if (Op0->hasOneUse() || Op1->hasOneUse()) {
      // (Cond & C) | (~Cond & D) -> Cond ? C : D, and commuted variants.
      if (Value *V = matchSelectFromAndOr(A, C, B, D))
        return replaceInstUsesWith(I, V);
      if (Value *V = matchSelectFromAndOr(A, C, D, B))
        return replaceInstUsesWith(I, V);
      if (Value *V = matchSelectFromAndOr(C, A, B, D))
        return replaceInstUsesWith(I, V);
      if (Value *V = matchSelectFromAndOr(C, A, D, B))
        return replaceInstUsesWith(I, V);
      if (Value *V = matchSelectFromAndOr(B, D, A, C))
        return replaceInstUsesWith(I, V);
      if (Value *V = matchSelectFromAndOr(B, D, C, A))
        return replaceInstUsesWith(I, V);
      if (Value *V = matchSelectFromAndOr(D, B, A, C))
        return replaceInstUsesWith(I, V);
      if (Value *V = matchSelectFromAndOr(D, B, C, A))
        return replaceInstUsesWith(I, V);
    }
  }

  // (A ^ B) | ((B ^ C) ^ A) -> (A ^ B) | C
  if (match(Op0, m_Xor(m_Value(A), m_Value(B))))
    if (match(Op1, m_Xor(m_Xor(m_Specific(B), m_Value(C)), m_Specific(A))))
      return BinaryOperator::CreateOr(Op0, C);

  // ((A ^ C) ^ B) | (B ^ A) -> (B ^ A) | C
  if (match(Op0, m_Xor(m_Xor(m_Value(A), m_Value(C)), m_Value(B))))
    if (match(Op1, m_Xor(m_Specific(B), m_Specific(A))))
      return BinaryOperator::CreateOr(Op1, C);

  // ((B | C) & A) | B -> B | (A & C)
  if (match(Op0, m_And(m_Or(m_Specific(Op1), m_Value(C)), m_Value(A))))
    return BinaryOperator::CreateOr(Op1, Builder.CreateAnd(A, C));

  if (Instruction *DeMorgan = matchDeMorgansLaws(I, Builder))
    return DeMorgan;

  // Canonicalize xor to the RHS.
  bool SwappedForXor = false;
  if (match(Op0, m_Xor(m_Value(), m_Value()))) {
    std::swap(Op0, Op1);
    SwappedForXor = true;
  }

  // A | ( A ^ B) -> A |  B
  // A | (~A ^ B) -> A | ~B
  // (A & B) | (A ^ B)
  if (match(Op1, m_Xor(m_Value(A), m_Value(B)))) {
    if (Op0 == A || Op0 == B)
      return BinaryOperator::CreateOr(A, B);

    if (match(Op0, m_And(m_Specific(A), m_Specific(B))) ||
        match(Op0, m_And(m_Specific(B), m_Specific(A))))
      return BinaryOperator::CreateOr(A, B);

    if (Op1->hasOneUse() && match(A, m_Not(m_Specific(Op0)))) {
      Value *Not = Builder.CreateNot(B, B->getName() + ".not");
      return BinaryOperator::CreateOr(Not, Op0);
    }
    if (Op1->hasOneUse() && match(B, m_Not(m_Specific(Op0)))) {
      Value *Not = Builder.CreateNot(A, A->getName() + ".not");
      return BinaryOperator::CreateOr(Not, Op0);
    }
  }

  // A | ~(A | B) -> A | ~B
  // A | ~(A ^ B) -> A | ~B
  if (match(Op1, m_Not(m_Value(A))))
    if (BinaryOperator *B = dyn_cast<BinaryOperator>(A))
      if ((Op0 == B->getOperand(0) || Op0 == B->getOperand(1)) &&
          Op1->hasOneUse() && (B->getOpcode() == Instruction::Or ||
                               B->getOpcode() == Instruction::Xor)) {
        Value *NotOp = Op0 == B->getOperand(0) ? B->getOperand(1) :
                                                 B->getOperand(0);
        Value *Not = Builder.CreateNot(NotOp, NotOp->getName() + ".not");
        return BinaryOperator::CreateOr(Not, Op0);
      }

  if (SwappedForXor)
    std::swap(Op0, Op1);

  {
    ICmpInst *LHS = dyn_cast<ICmpInst>(Op0);
    ICmpInst *RHS = dyn_cast<ICmpInst>(Op1);
    if (LHS && RHS)
      if (Value *Res = foldOrOfICmps(LHS, RHS, I))
        return replaceInstUsesWith(I, Res);

    // TODO: Make this recursive; it's a little tricky because an arbitrary
    // number of 'or' instructions might have to be created.
    Value *X, *Y;
    if (LHS && match(Op1, m_OneUse(m_Or(m_Value(X), m_Value(Y))))) {
      if (auto *Cmp = dyn_cast<ICmpInst>(X))
        if (Value *Res = foldOrOfICmps(LHS, Cmp, I))
          return replaceInstUsesWith(I, Builder.CreateOr(Res, Y));
      if (auto *Cmp = dyn_cast<ICmpInst>(Y))
        if (Value *Res = foldOrOfICmps(LHS, Cmp, I))
          return replaceInstUsesWith(I, Builder.CreateOr(Res, X));
    }
    if (RHS && match(Op0, m_OneUse(m_Or(m_Value(X), m_Value(Y))))) {
      if (auto *Cmp = dyn_cast<ICmpInst>(X))
        if (Value *Res = foldOrOfICmps(Cmp, RHS, I))
          return replaceInstUsesWith(I, Builder.CreateOr(Res, Y));
      if (auto *Cmp = dyn_cast<ICmpInst>(Y))
        if (Value *Res = foldOrOfICmps(Cmp, RHS, I))
          return replaceInstUsesWith(I, Builder.CreateOr(Res, X));
    }
  }

  if (FCmpInst *LHS = dyn_cast<FCmpInst>(I.getOperand(0)))
    if (FCmpInst *RHS = dyn_cast<FCmpInst>(I.getOperand(1)))
      if (Value *Res = foldLogicOfFCmps(LHS, RHS, false))
        return replaceInstUsesWith(I, Res);

  if (Instruction *CastedOr = foldCastedBitwiseLogic(I))
    return CastedOr;

  // or(sext(A), B) / or(B, sext(A)) --> A ? -1 : B, where A is i1 or <N x i1>.
  if (match(Op0, m_OneUse(m_SExt(m_Value(A)))) &&
      A->getType()->isIntOrIntVectorTy(1))
    return SelectInst::Create(A, ConstantInt::getSigned(I.getType(), -1), Op1);
  if (match(Op1, m_OneUse(m_SExt(m_Value(A)))) &&
      A->getType()->isIntOrIntVectorTy(1))
    return SelectInst::Create(A, ConstantInt::getSigned(I.getType(), -1), Op0);

  // Note: If we've gotten to the point of visiting the outer OR, then the
  // inner one couldn't be simplified.  If it was a constant, then it won't
  // be simplified by a later pass either, so we try swapping the inner/outer
  // ORs in the hopes that we'll be able to simplify it this way.
  // (X|C) | V --> (X|V) | C
  ConstantInt *CI;
  if (Op0->hasOneUse() && !isa<ConstantInt>(Op1) &&
      match(Op0, m_Or(m_Value(A), m_ConstantInt(CI)))) {
    Value *Inner = Builder.CreateOr(A, Op1);
    Inner->takeName(Op0);
    return BinaryOperator::CreateOr(Inner, CI);
  }

  // Change (or (bool?A:B),(bool?C:D)) --> (bool?(or A,C):(or B,D))
  // Since this OR statement hasn't been optimized further yet, we hope
  // that this transformation will allow the new ORs to be optimized.
  {
    Value *X = nullptr, *Y = nullptr;
    if (Op0->hasOneUse() && Op1->hasOneUse() &&
        match(Op0, m_Select(m_Value(X), m_Value(A), m_Value(B))) &&
        match(Op1, m_Select(m_Value(Y), m_Value(C), m_Value(D))) && X == Y) {
      Value *orTrue = Builder.CreateOr(A, C);
      Value *orFalse = Builder.CreateOr(B, D);
      return SelectInst::Create(X, orTrue, orFalse);
    }
  }

  return nullptr;
}

/// A ^ B can be specified using other logic ops in a variety of patterns. We
/// can fold these early and efficiently by morphing an existing instruction.
static Instruction *foldXorToXor(BinaryOperator &I,
                                 InstCombiner::BuilderTy &Builder) {
  assert(I.getOpcode() == Instruction::Xor);
  Value *Op0 = I.getOperand(0);
  Value *Op1 = I.getOperand(1);
  Value *A, *B;

  // There are 4 commuted variants for each of the basic patterns.

  // (A & B) ^ (A | B) -> A ^ B
  // (A & B) ^ (B | A) -> A ^ B
  // (A | B) ^ (A & B) -> A ^ B
  // (A | B) ^ (B & A) -> A ^ B
  if (match(&I, m_c_Xor(m_And(m_Value(A), m_Value(B)),
                        m_c_Or(m_Deferred(A), m_Deferred(B))))) {
    I.setOperand(0, A);
    I.setOperand(1, B);
    return &I;
  }

  // (A | ~B) ^ (~A | B) -> A ^ B
  // (~B | A) ^ (~A | B) -> A ^ B
  // (~A | B) ^ (A | ~B) -> A ^ B
  // (B | ~A) ^ (A | ~B) -> A ^ B
  if (match(&I, m_Xor(m_c_Or(m_Value(A), m_Not(m_Value(B))),
                      m_c_Or(m_Not(m_Deferred(A)), m_Deferred(B))))) {
    I.setOperand(0, A);
    I.setOperand(1, B);
    return &I;
  }

  // (A & ~B) ^ (~A & B) -> A ^ B
  // (~B & A) ^ (~A & B) -> A ^ B
  // (~A & B) ^ (A & ~B) -> A ^ B
  // (B & ~A) ^ (A & ~B) -> A ^ B
  if (match(&I, m_Xor(m_c_And(m_Value(A), m_Not(m_Value(B))),
                      m_c_And(m_Not(m_Deferred(A)), m_Deferred(B))))) {
    I.setOperand(0, A);
    I.setOperand(1, B);
    return &I;
  }

  // For the remaining cases we need to get rid of one of the operands.
  if (!Op0->hasOneUse() && !Op1->hasOneUse())
    return nullptr;

  // (A | B) ^ ~(A & B) -> ~(A ^ B)
  // (A | B) ^ ~(B & A) -> ~(A ^ B)
  // (A & B) ^ ~(A | B) -> ~(A ^ B)
  // (A & B) ^ ~(B | A) -> ~(A ^ B)
  // Complexity sorting ensures the not will be on the right side.
  if ((match(Op0, m_Or(m_Value(A), m_Value(B))) &&
       match(Op1, m_Not(m_c_And(m_Specific(A), m_Specific(B))))) ||
      (match(Op0, m_And(m_Value(A), m_Value(B))) &&
       match(Op1, m_Not(m_c_Or(m_Specific(A), m_Specific(B))))))
    return BinaryOperator::CreateNot(Builder.CreateXor(A, B));

  return nullptr;
}

Value *InstCombiner::foldXorOfICmps(ICmpInst *LHS, ICmpInst *RHS) {
  if (predicatesFoldable(LHS->getPredicate(), RHS->getPredicate())) {
    if (LHS->getOperand(0) == RHS->getOperand(1) &&
        LHS->getOperand(1) == RHS->getOperand(0))
      LHS->swapOperands();
    if (LHS->getOperand(0) == RHS->getOperand(0) &&
        LHS->getOperand(1) == RHS->getOperand(1)) {
      // (icmp1 A, B) ^ (icmp2 A, B) --> (icmp3 A, B)
      Value *Op0 = LHS->getOperand(0), *Op1 = LHS->getOperand(1);
      unsigned Code = getICmpCode(LHS) ^ getICmpCode(RHS);
      bool IsSigned = LHS->isSigned() || RHS->isSigned();
      return getNewICmpValue(Code, IsSigned, Op0, Op1, Builder);
    }
  }

  // TODO: This can be generalized to compares of non-signbits using
  // decomposeBitTestICmp(). It could be enhanced more by using (something like)
  // foldLogOpOfMaskedICmps().
  ICmpInst::Predicate PredL = LHS->getPredicate(), PredR = RHS->getPredicate();
  Value *LHS0 = LHS->getOperand(0), *LHS1 = LHS->getOperand(1);
  Value *RHS0 = RHS->getOperand(0), *RHS1 = RHS->getOperand(1);
  if ((LHS->hasOneUse() || RHS->hasOneUse()) &&
      LHS0->getType() == RHS0->getType() &&
      LHS0->getType()->isIntOrIntVectorTy()) {
    // (X > -1) ^ (Y > -1) --> (X ^ Y) < 0
    // (X <  0) ^ (Y <  0) --> (X ^ Y) < 0
    if ((PredL == CmpInst::ICMP_SGT && match(LHS1, m_AllOnes()) &&
         PredR == CmpInst::ICMP_SGT && match(RHS1, m_AllOnes())) ||
        (PredL == CmpInst::ICMP_SLT && match(LHS1, m_Zero()) &&
         PredR == CmpInst::ICMP_SLT && match(RHS1, m_Zero()))) {
      Value *Zero = ConstantInt::getNullValue(LHS0->getType());
      return Builder.CreateICmpSLT(Builder.CreateXor(LHS0, RHS0), Zero);
    }
    // (X > -1) ^ (Y <  0) --> (X ^ Y) > -1
    // (X <  0) ^ (Y > -1) --> (X ^ Y) > -1
    if ((PredL == CmpInst::ICMP_SGT && match(LHS1, m_AllOnes()) &&
         PredR == CmpInst::ICMP_SLT && match(RHS1, m_Zero())) ||
        (PredL == CmpInst::ICMP_SLT && match(LHS1, m_Zero()) &&
         PredR == CmpInst::ICMP_SGT && match(RHS1, m_AllOnes()))) {
      Value *MinusOne = ConstantInt::getAllOnesValue(LHS0->getType());
      return Builder.CreateICmpSGT(Builder.CreateXor(LHS0, RHS0), MinusOne);
    }
  }

  // Instead of trying to imitate the folds for and/or, decompose this 'xor'
  // into those logic ops. That is, try to turn this into an and-of-icmps
  // because we have many folds for that pattern.
  //
  // This is based on a truth table definition of xor:
  // X ^ Y --> (X | Y) & !(X & Y)
  if (Value *OrICmp = SimplifyBinOp(Instruction::Or, LHS, RHS, SQ)) {
    // TODO: If OrICmp is true, then the definition of xor simplifies to !(X&Y).
    // TODO: If OrICmp is false, the whole thing is false (InstSimplify?).
    if (Value *AndICmp = SimplifyBinOp(Instruction::And, LHS, RHS, SQ)) {
      // TODO: Independently handle cases where the 'and' side is a constant.
      if (OrICmp == LHS && AndICmp == RHS && RHS->hasOneUse()) {
        // (LHS | RHS) & !(LHS & RHS) --> LHS & !RHS
        RHS->setPredicate(RHS->getInversePredicate());
        return Builder.CreateAnd(LHS, RHS);
      }
      if (OrICmp == RHS && AndICmp == LHS && LHS->hasOneUse()) {
        // !(LHS & RHS) & (LHS | RHS) --> !LHS & RHS
        LHS->setPredicate(LHS->getInversePredicate());
        return Builder.CreateAnd(LHS, RHS);
      }
    }
  }

  return nullptr;
}

/// If we have a masked merge, in the canonical form of:
/// (assuming that A only has one use.)
///   |        A  |  |B|
///   ((x ^ y) & M) ^ y
///    |  D  |
/// * If M is inverted:
///      |  D  |
///     ((x ^ y) & ~M) ^ y
///   We can canonicalize by swapping the final xor operand
///   to eliminate the 'not' of the mask.
///     ((x ^ y) & M) ^ x
/// * If M is a constant, and D has one use, we transform to 'and' / 'or' ops
///   because that shortens the dependency chain and improves analysis:
///     (x & M) | (y & ~M)
static Instruction *visitMaskedMerge(BinaryOperator &I,
                                     InstCombiner::BuilderTy &Builder) {
  Value *B, *X, *D;
  Value *M;
  if (!match(&I, m_c_Xor(m_Value(B),
                         m_OneUse(m_c_And(
                             m_CombineAnd(m_c_Xor(m_Deferred(B), m_Value(X)),
                                          m_Value(D)),
                             m_Value(M))))))
    return nullptr;

  Value *NotM;
  if (match(M, m_Not(m_Value(NotM)))) {
    // De-invert the mask and swap the value in B part.
    Value *NewA = Builder.CreateAnd(D, NotM);
    return BinaryOperator::CreateXor(NewA, X);
  }

  Constant *C;
  if (D->hasOneUse() && match(M, m_Constant(C))) {
    // Unfold.
    Value *LHS = Builder.CreateAnd(X, C);
    Value *NotC = Builder.CreateNot(C);
    Value *RHS = Builder.CreateAnd(B, NotC);
    return BinaryOperator::CreateOr(LHS, RHS);
  }

  return nullptr;
}

// Transform
//   ~(x ^ y)
// into:
//   (~x) ^ y
// or into
//   x ^ (~y)
static Instruction *sinkNotIntoXor(BinaryOperator &I,
                                   InstCombiner::BuilderTy &Builder) {
  Value *X, *Y;
  // FIXME: one-use check is not needed in general, but currently we are unable
  // to fold 'not' into 'icmp', if that 'icmp' has multiple uses. (D35182)
  if (!match(&I, m_Not(m_OneUse(m_Xor(m_Value(X), m_Value(Y))))))
    return nullptr;

  // We only want to do the transform if it is free to do.
  if (IsFreeToInvert(X, X->hasOneUse())) {
    // Ok, good.
  } else if (IsFreeToInvert(Y, Y->hasOneUse())) {
    std::swap(X, Y);
  } else
    return nullptr;

  Value *NotX = Builder.CreateNot(X, X->getName() + ".not");
  return BinaryOperator::CreateXor(NotX, Y, I.getName() + ".demorgan");
}

// FIXME: We use commutative matchers (m_c_*) for some, but not all, matches
// here. We should standardize that construct where it is needed or choose some
// other way to ensure that commutated variants of patterns are not missed.
Instruction *InstCombiner::visitXor(BinaryOperator &I) {
  if (Value *V = SimplifyXorInst(I.getOperand(0), I.getOperand(1),
                                 SQ.getWithInstruction(&I)))
    return replaceInstUsesWith(I, V);

  if (SimplifyAssociativeOrCommutative(I))
    return &I;

  if (Instruction *X = foldVectorBinop(I))
    return X;

  if (Instruction *NewXor = foldXorToXor(I, Builder))
    return NewXor;

  // (A&B)^(A&C) -> A&(B^C) etc
  if (Value *V = SimplifyUsingDistributiveLaws(I))
    return replaceInstUsesWith(I, V);

  // See if we can simplify any instructions used by the instruction whose sole
  // purpose is to compute bits we don't care about.
  if (SimplifyDemandedInstructionBits(I))
    return &I;

  if (Value *V = SimplifyBSwap(I, Builder))
    return replaceInstUsesWith(I, V);

  Value *Op0 = I.getOperand(0), *Op1 = I.getOperand(1);

  // Fold (X & M) ^ (Y & ~M) -> (X & M) | (Y & ~M)
  // This it a special case in haveNoCommonBitsSet, but the computeKnownBits
  // calls in there are unnecessary as SimplifyDemandedInstructionBits should
  // have already taken care of those cases.
  Value *M;
  if (match(&I, m_c_Xor(m_c_And(m_Not(m_Value(M)), m_Value()),
                        m_c_And(m_Deferred(M), m_Value()))))
    return BinaryOperator::CreateOr(Op0, Op1);

  // Apply DeMorgan's Law for 'nand' / 'nor' logic with an inverted operand.
  Value *X, *Y;

  // We must eliminate the and/or (one-use) for these transforms to not increase
  // the instruction count.
  // ~(~X & Y) --> (X | ~Y)
  // ~(Y & ~X) --> (X | ~Y)
  if (match(&I, m_Not(m_OneUse(m_c_And(m_Not(m_Value(X)), m_Value(Y)))))) {
    Value *NotY = Builder.CreateNot(Y, Y->getName() + ".not");
    return BinaryOperator::CreateOr(X, NotY);
  }
  // ~(~X | Y) --> (X & ~Y)
  // ~(Y | ~X) --> (X & ~Y)
  if (match(&I, m_Not(m_OneUse(m_c_Or(m_Not(m_Value(X)), m_Value(Y)))))) {
    Value *NotY = Builder.CreateNot(Y, Y->getName() + ".not");
    return BinaryOperator::CreateAnd(X, NotY);
  }

  if (Instruction *Xor = visitMaskedMerge(I, Builder))
    return Xor;

  // Is this a 'not' (~) fed by a binary operator?
  BinaryOperator *NotVal;
  if (match(&I, m_Not(m_BinOp(NotVal)))) {
    if (NotVal->getOpcode() == Instruction::And ||
        NotVal->getOpcode() == Instruction::Or) {
      // Apply DeMorgan's Law when inverts are free:
      // ~(X & Y) --> (~X | ~Y)
      // ~(X | Y) --> (~X & ~Y)
      if (IsFreeToInvert(NotVal->getOperand(0),
                         NotVal->getOperand(0)->hasOneUse()) &&
          IsFreeToInvert(NotVal->getOperand(1),
                         NotVal->getOperand(1)->hasOneUse())) {
        Value *NotX = Builder.CreateNot(NotVal->getOperand(0), "notlhs");
        Value *NotY = Builder.CreateNot(NotVal->getOperand(1), "notrhs");
        if (NotVal->getOpcode() == Instruction::And)
          return BinaryOperator::CreateOr(NotX, NotY);
        return BinaryOperator::CreateAnd(NotX, NotY);
      }
    }

    // ~(X - Y) --> ~X + Y
    if (match(NotVal, m_Sub(m_Value(X), m_Value(Y))))
      if (isa<Constant>(X) || NotVal->hasOneUse())
        return BinaryOperator::CreateAdd(Builder.CreateNot(X), Y);

    // ~(~X >>s Y) --> (X >>s Y)
    if (match(NotVal, m_AShr(m_Not(m_Value(X)), m_Value(Y))))
      return BinaryOperator::CreateAShr(X, Y);

    // If we are inverting a right-shifted constant, we may be able to eliminate
    // the 'not' by inverting the constant and using the opposite shift type.
    // Canonicalization rules ensure that only a negative constant uses 'ashr',
    // but we must check that in case that transform has not fired yet.

    // ~(C >>s Y) --> ~C >>u Y (when inverting the replicated sign bits)
    Constant *C;
    if (match(NotVal, m_AShr(m_Constant(C), m_Value(Y))) &&
        match(C, m_Negative()))
      return BinaryOperator::CreateLShr(ConstantExpr::getNot(C), Y);

    // ~(C >>u Y) --> ~C >>s Y (when inverting the replicated sign bits)
    if (match(NotVal, m_LShr(m_Constant(C), m_Value(Y))) &&
        match(C, m_NonNegative()))
      return BinaryOperator::CreateAShr(ConstantExpr::getNot(C), Y);

    // ~(X + C) --> -(C + 1) - X
    if (match(Op0, m_Add(m_Value(X), m_Constant(C))))
      return BinaryOperator::CreateSub(ConstantExpr::getNeg(AddOne(C)), X);
  }

  // Use DeMorgan and reassociation to eliminate a 'not' op.
  Constant *C1;
  if (match(Op1, m_Constant(C1))) {
    Constant *C2;
    if (match(Op0, m_OneUse(m_Or(m_Not(m_Value(X)), m_Constant(C2))))) {
      // (~X | C2) ^ C1 --> ((X & ~C2) ^ -1) ^ C1 --> (X & ~C2) ^ ~C1
      Value *And = Builder.CreateAnd(X, ConstantExpr::getNot(C2));
      return BinaryOperator::CreateXor(And, ConstantExpr::getNot(C1));
    }
    if (match(Op0, m_OneUse(m_And(m_Not(m_Value(X)), m_Constant(C2))))) {
      // (~X & C2) ^ C1 --> ((X | ~C2) ^ -1) ^ C1 --> (X | ~C2) ^ ~C1
      Value *Or = Builder.CreateOr(X, ConstantExpr::getNot(C2));
      return BinaryOperator::CreateXor(Or, ConstantExpr::getNot(C1));
    }
  }

  // not (cmp A, B) = !cmp A, B
  CmpInst::Predicate Pred;
  if (match(&I, m_Not(m_OneUse(m_Cmp(Pred, m_Value(), m_Value()))))) {
    cast<CmpInst>(Op0)->setPredicate(CmpInst::getInversePredicate(Pred));
    return replaceInstUsesWith(I, Op0);
  }

  {
    const APInt *RHSC;
    if (match(Op1, m_APInt(RHSC))) {
      Value *X;
      const APInt *C;
      if (RHSC->isSignMask() && match(Op0, m_Sub(m_APInt(C), m_Value(X)))) {
        // (C - X) ^ signmask -> (C + signmask - X)
        Constant *NewC = ConstantInt::get(I.getType(), *C + *RHSC);
        return BinaryOperator::CreateSub(NewC, X);
      }
      if (RHSC->isSignMask() && match(Op0, m_Add(m_Value(X), m_APInt(C)))) {
        // (X + C) ^ signmask -> (X + C + signmask)
        Constant *NewC = ConstantInt::get(I.getType(), *C + *RHSC);
        return BinaryOperator::CreateAdd(X, NewC);
      }

      // (X|C1)^C2 -> X^(C1^C2) iff X&~C1 == 0
      if (match(Op0, m_Or(m_Value(X), m_APInt(C))) &&
          MaskedValueIsZero(X, *C, 0, &I)) {
        Constant *NewC = ConstantInt::get(I.getType(), *C ^ *RHSC);
        Worklist.Add(cast<Instruction>(Op0));
        I.setOperand(0, X);
        I.setOperand(1, NewC);
        return &I;
      }
    }
  }

  if (ConstantInt *RHSC = dyn_cast<ConstantInt>(Op1)) {
    if (BinaryOperator *Op0I = dyn_cast<BinaryOperator>(Op0)) {
      if (ConstantInt *Op0CI = dyn_cast<ConstantInt>(Op0I->getOperand(1))) {
        if (Op0I->getOpcode() == Instruction::LShr) {
          // ((X^C1) >> C2) ^ C3 -> (X>>C2) ^ ((C1>>C2)^C3)
          // E1 = "X ^ C1"
          BinaryOperator *E1;
          ConstantInt *C1;
          if (Op0I->hasOneUse() &&
              (E1 = dyn_cast<BinaryOperator>(Op0I->getOperand(0))) &&
              E1->getOpcode() == Instruction::Xor &&
              (C1 = dyn_cast<ConstantInt>(E1->getOperand(1)))) {
            // fold (C1 >> C2) ^ C3
            ConstantInt *C2 = Op0CI, *C3 = RHSC;
            APInt FoldConst = C1->getValue().lshr(C2->getValue());
            FoldConst ^= C3->getValue();
            // Prepare the two operands.
            Value *Opnd0 = Builder.CreateLShr(E1->getOperand(0), C2);
            Opnd0->takeName(Op0I);
            cast<Instruction>(Opnd0)->setDebugLoc(I.getDebugLoc());
            Value *FoldVal = ConstantInt::get(Opnd0->getType(), FoldConst);

            return BinaryOperator::CreateXor(Opnd0, FoldVal);
          }
        }
      }
    }
  }

  if (Instruction *FoldedLogic = foldBinOpIntoSelectOrPhi(I))
    return FoldedLogic;

  // Y ^ (X | Y) --> X & ~Y
  // Y ^ (Y | X) --> X & ~Y
  if (match(Op1, m_OneUse(m_c_Or(m_Value(X), m_Specific(Op0)))))
    return BinaryOperator::CreateAnd(X, Builder.CreateNot(Op0));
  // (X | Y) ^ Y --> X & ~Y
  // (Y | X) ^ Y --> X & ~Y
  if (match(Op0, m_OneUse(m_c_Or(m_Value(X), m_Specific(Op1)))))
    return BinaryOperator::CreateAnd(X, Builder.CreateNot(Op1));

  // Y ^ (X & Y) --> ~X & Y
  // Y ^ (Y & X) --> ~X & Y
  if (match(Op1, m_OneUse(m_c_And(m_Value(X), m_Specific(Op0)))))
    return BinaryOperator::CreateAnd(Op0, Builder.CreateNot(X));
  // (X & Y) ^ Y --> ~X & Y
  // (Y & X) ^ Y --> ~X & Y
  // Canonical form is (X & C) ^ C; don't touch that.
  // TODO: A 'not' op is better for analysis and codegen, but demanded bits must
  //       be fixed to prefer that (otherwise we get infinite looping).
  if (!match(Op1, m_Constant()) &&
      match(Op0, m_OneUse(m_c_And(m_Value(X), m_Specific(Op1)))))
    return BinaryOperator::CreateAnd(Op1, Builder.CreateNot(X));

  Value *A, *B, *C;
  // (A ^ B) ^ (A | C) --> (~A & C) ^ B -- There are 4 commuted variants.
  if (match(&I, m_c_Xor(m_OneUse(m_Xor(m_Value(A), m_Value(B))),
                        m_OneUse(m_c_Or(m_Deferred(A), m_Value(C))))))
      return BinaryOperator::CreateXor(
          Builder.CreateAnd(Builder.CreateNot(A), C), B);

  // (A ^ B) ^ (B | C) --> (~B & C) ^ A -- There are 4 commuted variants.
  if (match(&I, m_c_Xor(m_OneUse(m_Xor(m_Value(A), m_Value(B))),
                        m_OneUse(m_c_Or(m_Deferred(B), m_Value(C))))))
      return BinaryOperator::CreateXor(
          Builder.CreateAnd(Builder.CreateNot(B), C), A);

  // (A & B) ^ (A ^ B) -> (A | B)
  if (match(Op0, m_And(m_Value(A), m_Value(B))) &&
      match(Op1, m_c_Xor(m_Specific(A), m_Specific(B))))
    return BinaryOperator::CreateOr(A, B);
  // (A ^ B) ^ (A & B) -> (A | B)
  if (match(Op0, m_Xor(m_Value(A), m_Value(B))) &&
      match(Op1, m_c_And(m_Specific(A), m_Specific(B))))
    return BinaryOperator::CreateOr(A, B);

  // (A & ~B) ^ ~A -> ~(A & B)
  // (~B & A) ^ ~A -> ~(A & B)
  if (match(Op0, m_c_And(m_Value(A), m_Not(m_Value(B)))) &&
      match(Op1, m_Not(m_Specific(A))))
    return BinaryOperator::CreateNot(Builder.CreateAnd(A, B));

  if (auto *LHS = dyn_cast<ICmpInst>(I.getOperand(0)))
    if (auto *RHS = dyn_cast<ICmpInst>(I.getOperand(1)))
      if (Value *V = foldXorOfICmps(LHS, RHS))
        return replaceInstUsesWith(I, V);

  if (Instruction *CastedXor = foldCastedBitwiseLogic(I))
    return CastedXor;

  // Canonicalize a shifty way to code absolute value to the common pattern.
  // There are 4 potential commuted variants. Move the 'ashr' candidate to Op1.
  // We're relying on the fact that we only do this transform when the shift has
  // exactly 2 uses and the add has exactly 1 use (otherwise, we might increase
  // instructions).
  if (Op0->hasNUses(2))
    std::swap(Op0, Op1);

  const APInt *ShAmt;
  Type *Ty = I.getType();
  if (match(Op1, m_AShr(m_Value(A), m_APInt(ShAmt))) &&
      Op1->hasNUses(2) && *ShAmt == Ty->getScalarSizeInBits() - 1 &&
      match(Op0, m_OneUse(m_c_Add(m_Specific(A), m_Specific(Op1))))) {
    // B = ashr i32 A, 31 ; smear the sign bit
    // xor (add A, B), B  ; add -1 and flip bits if negative
    // --> (A < 0) ? -A : A
    Value *Cmp = Builder.CreateICmpSLT(A, ConstantInt::getNullValue(Ty));
    // Copy the nuw/nsw flags from the add to the negate.
    auto *Add = cast<BinaryOperator>(Op0);
    Value *Neg = Builder.CreateNeg(A, "", Add->hasNoUnsignedWrap(),
                                   Add->hasNoSignedWrap());
    return SelectInst::Create(Cmp, Neg, A);
  }

  // Eliminate a bitwise 'not' op of 'not' min/max by inverting the min/max:
  //
  //   %notx = xor i32 %x, -1
  //   %cmp1 = icmp sgt i32 %notx, %y
  //   %smax = select i1 %cmp1, i32 %notx, i32 %y
  //   %res = xor i32 %smax, -1
  // =>
  //   %noty = xor i32 %y, -1
  //   %cmp2 = icmp slt %x, %noty
  //   %res = select i1 %cmp2, i32 %x, i32 %noty
  //
  // Same is applicable for smin/umax/umin.
  if (match(Op1, m_AllOnes()) && Op0->hasOneUse()) {
    Value *LHS, *RHS;
    SelectPatternFlavor SPF = matchSelectPattern(Op0, LHS, RHS).Flavor;
    if (SelectPatternResult::isMinOrMax(SPF)) {
      // It's possible we get here before the not has been simplified, so make
      // sure the input to the not isn't freely invertible.
      if (match(LHS, m_Not(m_Value(X))) && !IsFreeToInvert(X, X->hasOneUse())) {
        Value *NotY = Builder.CreateNot(RHS);
        return SelectInst::Create(
            Builder.CreateICmp(getInverseMinMaxPred(SPF), X, NotY), X, NotY);
      }

      // It's possible we get here before the not has been simplified, so make
      // sure the input to the not isn't freely invertible.
      if (match(RHS, m_Not(m_Value(Y))) && !IsFreeToInvert(Y, Y->hasOneUse())) {
        Value *NotX = Builder.CreateNot(LHS);
        return SelectInst::Create(
            Builder.CreateICmp(getInverseMinMaxPred(SPF), NotX, Y), NotX, Y);
      }

      // If both sides are freely invertible, then we can get rid of the xor
      // completely.
      if (IsFreeToInvert(LHS, !LHS->hasNUsesOrMore(3)) &&
          IsFreeToInvert(RHS, !RHS->hasNUsesOrMore(3))) {
        Value *NotLHS = Builder.CreateNot(LHS);
        Value *NotRHS = Builder.CreateNot(RHS);
        return SelectInst::Create(
            Builder.CreateICmp(getInverseMinMaxPred(SPF), NotLHS, NotRHS),
            NotLHS, NotRHS);
      }
    }
  }

  if (Instruction *NewXor = sinkNotIntoXor(I, Builder))
    return NewXor;

  return nullptr;
}
