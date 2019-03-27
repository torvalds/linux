//===- InstCombineCompares.cpp --------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the visitICmp and visitFCmp functions.
//
//===----------------------------------------------------------------------===//

#include "InstCombineInternal.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/KnownBits.h"

using namespace llvm;
using namespace PatternMatch;

#define DEBUG_TYPE "instcombine"

// How many times is a select replaced by one of its operands?
STATISTIC(NumSel, "Number of select opts");


/// Compute Result = In1+In2, returning true if the result overflowed for this
/// type.
static bool addWithOverflow(APInt &Result, const APInt &In1,
                            const APInt &In2, bool IsSigned = false) {
  bool Overflow;
  if (IsSigned)
    Result = In1.sadd_ov(In2, Overflow);
  else
    Result = In1.uadd_ov(In2, Overflow);

  return Overflow;
}

/// Compute Result = In1-In2, returning true if the result overflowed for this
/// type.
static bool subWithOverflow(APInt &Result, const APInt &In1,
                            const APInt &In2, bool IsSigned = false) {
  bool Overflow;
  if (IsSigned)
    Result = In1.ssub_ov(In2, Overflow);
  else
    Result = In1.usub_ov(In2, Overflow);

  return Overflow;
}

/// Given an icmp instruction, return true if any use of this comparison is a
/// branch on sign bit comparison.
static bool hasBranchUse(ICmpInst &I) {
  for (auto *U : I.users())
    if (isa<BranchInst>(U))
      return true;
  return false;
}

/// Given an exploded icmp instruction, return true if the comparison only
/// checks the sign bit. If it only checks the sign bit, set TrueIfSigned if the
/// result of the comparison is true when the input value is signed.
static bool isSignBitCheck(ICmpInst::Predicate Pred, const APInt &RHS,
                           bool &TrueIfSigned) {
  switch (Pred) {
  case ICmpInst::ICMP_SLT:   // True if LHS s< 0
    TrueIfSigned = true;
    return RHS.isNullValue();
  case ICmpInst::ICMP_SLE:   // True if LHS s<= RHS and RHS == -1
    TrueIfSigned = true;
    return RHS.isAllOnesValue();
  case ICmpInst::ICMP_SGT:   // True if LHS s> -1
    TrueIfSigned = false;
    return RHS.isAllOnesValue();
  case ICmpInst::ICMP_UGT:
    // True if LHS u> RHS and RHS == high-bit-mask - 1
    TrueIfSigned = true;
    return RHS.isMaxSignedValue();
  case ICmpInst::ICMP_UGE:
    // True if LHS u>= RHS and RHS == high-bit-mask (2^7, 2^15, 2^31, etc)
    TrueIfSigned = true;
    return RHS.isSignMask();
  default:
    return false;
  }
}

/// Returns true if the exploded icmp can be expressed as a signed comparison
/// to zero and updates the predicate accordingly.
/// The signedness of the comparison is preserved.
/// TODO: Refactor with decomposeBitTestICmp()?
static bool isSignTest(ICmpInst::Predicate &Pred, const APInt &C) {
  if (!ICmpInst::isSigned(Pred))
    return false;

  if (C.isNullValue())
    return ICmpInst::isRelational(Pred);

  if (C.isOneValue()) {
    if (Pred == ICmpInst::ICMP_SLT) {
      Pred = ICmpInst::ICMP_SLE;
      return true;
    }
  } else if (C.isAllOnesValue()) {
    if (Pred == ICmpInst::ICMP_SGT) {
      Pred = ICmpInst::ICMP_SGE;
      return true;
    }
  }

  return false;
}

/// Given a signed integer type and a set of known zero and one bits, compute
/// the maximum and minimum values that could have the specified known zero and
/// known one bits, returning them in Min/Max.
/// TODO: Move to method on KnownBits struct?
static void computeSignedMinMaxValuesFromKnownBits(const KnownBits &Known,
                                                   APInt &Min, APInt &Max) {
  assert(Known.getBitWidth() == Min.getBitWidth() &&
         Known.getBitWidth() == Max.getBitWidth() &&
         "KnownZero, KnownOne and Min, Max must have equal bitwidth.");
  APInt UnknownBits = ~(Known.Zero|Known.One);

  // The minimum value is when all unknown bits are zeros, EXCEPT for the sign
  // bit if it is unknown.
  Min = Known.One;
  Max = Known.One|UnknownBits;

  if (UnknownBits.isNegative()) { // Sign bit is unknown
    Min.setSignBit();
    Max.clearSignBit();
  }
}

/// Given an unsigned integer type and a set of known zero and one bits, compute
/// the maximum and minimum values that could have the specified known zero and
/// known one bits, returning them in Min/Max.
/// TODO: Move to method on KnownBits struct?
static void computeUnsignedMinMaxValuesFromKnownBits(const KnownBits &Known,
                                                     APInt &Min, APInt &Max) {
  assert(Known.getBitWidth() == Min.getBitWidth() &&
         Known.getBitWidth() == Max.getBitWidth() &&
         "Ty, KnownZero, KnownOne and Min, Max must have equal bitwidth.");
  APInt UnknownBits = ~(Known.Zero|Known.One);

  // The minimum value is when the unknown bits are all zeros.
  Min = Known.One;
  // The maximum value is when the unknown bits are all ones.
  Max = Known.One|UnknownBits;
}

/// This is called when we see this pattern:
///   cmp pred (load (gep GV, ...)), cmpcst
/// where GV is a global variable with a constant initializer. Try to simplify
/// this into some simple computation that does not need the load. For example
/// we can optimize "icmp eq (load (gep "foo", 0, i)), 0" into "icmp eq i, 3".
///
/// If AndCst is non-null, then the loaded value is masked with that constant
/// before doing the comparison. This handles cases like "A[i]&4 == 0".
Instruction *InstCombiner::foldCmpLoadFromIndexedGlobal(GetElementPtrInst *GEP,
                                                        GlobalVariable *GV,
                                                        CmpInst &ICI,
                                                        ConstantInt *AndCst) {
  Constant *Init = GV->getInitializer();
  if (!isa<ConstantArray>(Init) && !isa<ConstantDataArray>(Init))
    return nullptr;

  uint64_t ArrayElementCount = Init->getType()->getArrayNumElements();
  // Don't blow up on huge arrays.
  if (ArrayElementCount > MaxArraySizeForCombine)
    return nullptr;

  // There are many forms of this optimization we can handle, for now, just do
  // the simple index into a single-dimensional array.
  //
  // Require: GEP GV, 0, i {{, constant indices}}
  if (GEP->getNumOperands() < 3 ||
      !isa<ConstantInt>(GEP->getOperand(1)) ||
      !cast<ConstantInt>(GEP->getOperand(1))->isZero() ||
      isa<Constant>(GEP->getOperand(2)))
    return nullptr;

  // Check that indices after the variable are constants and in-range for the
  // type they index.  Collect the indices.  This is typically for arrays of
  // structs.
  SmallVector<unsigned, 4> LaterIndices;

  Type *EltTy = Init->getType()->getArrayElementType();
  for (unsigned i = 3, e = GEP->getNumOperands(); i != e; ++i) {
    ConstantInt *Idx = dyn_cast<ConstantInt>(GEP->getOperand(i));
    if (!Idx) return nullptr;  // Variable index.

    uint64_t IdxVal = Idx->getZExtValue();
    if ((unsigned)IdxVal != IdxVal) return nullptr; // Too large array index.

    if (StructType *STy = dyn_cast<StructType>(EltTy))
      EltTy = STy->getElementType(IdxVal);
    else if (ArrayType *ATy = dyn_cast<ArrayType>(EltTy)) {
      if (IdxVal >= ATy->getNumElements()) return nullptr;
      EltTy = ATy->getElementType();
    } else {
      return nullptr; // Unknown type.
    }

    LaterIndices.push_back(IdxVal);
  }

  enum { Overdefined = -3, Undefined = -2 };

  // Variables for our state machines.

  // FirstTrueElement/SecondTrueElement - Used to emit a comparison of the form
  // "i == 47 | i == 87", where 47 is the first index the condition is true for,
  // and 87 is the second (and last) index.  FirstTrueElement is -2 when
  // undefined, otherwise set to the first true element.  SecondTrueElement is
  // -2 when undefined, -3 when overdefined and >= 0 when that index is true.
  int FirstTrueElement = Undefined, SecondTrueElement = Undefined;

  // FirstFalseElement/SecondFalseElement - Used to emit a comparison of the
  // form "i != 47 & i != 87".  Same state transitions as for true elements.
  int FirstFalseElement = Undefined, SecondFalseElement = Undefined;

  /// TrueRangeEnd/FalseRangeEnd - In conjunction with First*Element, these
  /// define a state machine that triggers for ranges of values that the index
  /// is true or false for.  This triggers on things like "abbbbc"[i] == 'b'.
  /// This is -2 when undefined, -3 when overdefined, and otherwise the last
  /// index in the range (inclusive).  We use -2 for undefined here because we
  /// use relative comparisons and don't want 0-1 to match -1.
  int TrueRangeEnd = Undefined, FalseRangeEnd = Undefined;

  // MagicBitvector - This is a magic bitvector where we set a bit if the
  // comparison is true for element 'i'.  If there are 64 elements or less in
  // the array, this will fully represent all the comparison results.
  uint64_t MagicBitvector = 0;

  // Scan the array and see if one of our patterns matches.
  Constant *CompareRHS = cast<Constant>(ICI.getOperand(1));
  for (unsigned i = 0, e = ArrayElementCount; i != e; ++i) {
    Constant *Elt = Init->getAggregateElement(i);
    if (!Elt) return nullptr;

    // If this is indexing an array of structures, get the structure element.
    if (!LaterIndices.empty())
      Elt = ConstantExpr::getExtractValue(Elt, LaterIndices);

    // If the element is masked, handle it.
    if (AndCst) Elt = ConstantExpr::getAnd(Elt, AndCst);

    // Find out if the comparison would be true or false for the i'th element.
    Constant *C = ConstantFoldCompareInstOperands(ICI.getPredicate(), Elt,
                                                  CompareRHS, DL, &TLI);
    // If the result is undef for this element, ignore it.
    if (isa<UndefValue>(C)) {
      // Extend range state machines to cover this element in case there is an
      // undef in the middle of the range.
      if (TrueRangeEnd == (int)i-1)
        TrueRangeEnd = i;
      if (FalseRangeEnd == (int)i-1)
        FalseRangeEnd = i;
      continue;
    }

    // If we can't compute the result for any of the elements, we have to give
    // up evaluating the entire conditional.
    if (!isa<ConstantInt>(C)) return nullptr;

    // Otherwise, we know if the comparison is true or false for this element,
    // update our state machines.
    bool IsTrueForElt = !cast<ConstantInt>(C)->isZero();

    // State machine for single/double/range index comparison.
    if (IsTrueForElt) {
      // Update the TrueElement state machine.
      if (FirstTrueElement == Undefined)
        FirstTrueElement = TrueRangeEnd = i;  // First true element.
      else {
        // Update double-compare state machine.
        if (SecondTrueElement == Undefined)
          SecondTrueElement = i;
        else
          SecondTrueElement = Overdefined;

        // Update range state machine.
        if (TrueRangeEnd == (int)i-1)
          TrueRangeEnd = i;
        else
          TrueRangeEnd = Overdefined;
      }
    } else {
      // Update the FalseElement state machine.
      if (FirstFalseElement == Undefined)
        FirstFalseElement = FalseRangeEnd = i; // First false element.
      else {
        // Update double-compare state machine.
        if (SecondFalseElement == Undefined)
          SecondFalseElement = i;
        else
          SecondFalseElement = Overdefined;

        // Update range state machine.
        if (FalseRangeEnd == (int)i-1)
          FalseRangeEnd = i;
        else
          FalseRangeEnd = Overdefined;
      }
    }

    // If this element is in range, update our magic bitvector.
    if (i < 64 && IsTrueForElt)
      MagicBitvector |= 1ULL << i;

    // If all of our states become overdefined, bail out early.  Since the
    // predicate is expensive, only check it every 8 elements.  This is only
    // really useful for really huge arrays.
    if ((i & 8) == 0 && i >= 64 && SecondTrueElement == Overdefined &&
        SecondFalseElement == Overdefined && TrueRangeEnd == Overdefined &&
        FalseRangeEnd == Overdefined)
      return nullptr;
  }

  // Now that we've scanned the entire array, emit our new comparison(s).  We
  // order the state machines in complexity of the generated code.
  Value *Idx = GEP->getOperand(2);

  // If the index is larger than the pointer size of the target, truncate the
  // index down like the GEP would do implicitly.  We don't have to do this for
  // an inbounds GEP because the index can't be out of range.
  if (!GEP->isInBounds()) {
    Type *IntPtrTy = DL.getIntPtrType(GEP->getType());
    unsigned PtrSize = IntPtrTy->getIntegerBitWidth();
    if (Idx->getType()->getPrimitiveSizeInBits() > PtrSize)
      Idx = Builder.CreateTrunc(Idx, IntPtrTy);
  }

  // If the comparison is only true for one or two elements, emit direct
  // comparisons.
  if (SecondTrueElement != Overdefined) {
    // None true -> false.
    if (FirstTrueElement == Undefined)
      return replaceInstUsesWith(ICI, Builder.getFalse());

    Value *FirstTrueIdx = ConstantInt::get(Idx->getType(), FirstTrueElement);

    // True for one element -> 'i == 47'.
    if (SecondTrueElement == Undefined)
      return new ICmpInst(ICmpInst::ICMP_EQ, Idx, FirstTrueIdx);

    // True for two elements -> 'i == 47 | i == 72'.
    Value *C1 = Builder.CreateICmpEQ(Idx, FirstTrueIdx);
    Value *SecondTrueIdx = ConstantInt::get(Idx->getType(), SecondTrueElement);
    Value *C2 = Builder.CreateICmpEQ(Idx, SecondTrueIdx);
    return BinaryOperator::CreateOr(C1, C2);
  }

  // If the comparison is only false for one or two elements, emit direct
  // comparisons.
  if (SecondFalseElement != Overdefined) {
    // None false -> true.
    if (FirstFalseElement == Undefined)
      return replaceInstUsesWith(ICI, Builder.getTrue());

    Value *FirstFalseIdx = ConstantInt::get(Idx->getType(), FirstFalseElement);

    // False for one element -> 'i != 47'.
    if (SecondFalseElement == Undefined)
      return new ICmpInst(ICmpInst::ICMP_NE, Idx, FirstFalseIdx);

    // False for two elements -> 'i != 47 & i != 72'.
    Value *C1 = Builder.CreateICmpNE(Idx, FirstFalseIdx);
    Value *SecondFalseIdx = ConstantInt::get(Idx->getType(),SecondFalseElement);
    Value *C2 = Builder.CreateICmpNE(Idx, SecondFalseIdx);
    return BinaryOperator::CreateAnd(C1, C2);
  }

  // If the comparison can be replaced with a range comparison for the elements
  // where it is true, emit the range check.
  if (TrueRangeEnd != Overdefined) {
    assert(TrueRangeEnd != FirstTrueElement && "Should emit single compare");

    // Generate (i-FirstTrue) <u (TrueRangeEnd-FirstTrue+1).
    if (FirstTrueElement) {
      Value *Offs = ConstantInt::get(Idx->getType(), -FirstTrueElement);
      Idx = Builder.CreateAdd(Idx, Offs);
    }

    Value *End = ConstantInt::get(Idx->getType(),
                                  TrueRangeEnd-FirstTrueElement+1);
    return new ICmpInst(ICmpInst::ICMP_ULT, Idx, End);
  }

  // False range check.
  if (FalseRangeEnd != Overdefined) {
    assert(FalseRangeEnd != FirstFalseElement && "Should emit single compare");
    // Generate (i-FirstFalse) >u (FalseRangeEnd-FirstFalse).
    if (FirstFalseElement) {
      Value *Offs = ConstantInt::get(Idx->getType(), -FirstFalseElement);
      Idx = Builder.CreateAdd(Idx, Offs);
    }

    Value *End = ConstantInt::get(Idx->getType(),
                                  FalseRangeEnd-FirstFalseElement);
    return new ICmpInst(ICmpInst::ICMP_UGT, Idx, End);
  }

  // If a magic bitvector captures the entire comparison state
  // of this load, replace it with computation that does:
  //   ((magic_cst >> i) & 1) != 0
  {
    Type *Ty = nullptr;

    // Look for an appropriate type:
    // - The type of Idx if the magic fits
    // - The smallest fitting legal type
    if (ArrayElementCount <= Idx->getType()->getIntegerBitWidth())
      Ty = Idx->getType();
    else
      Ty = DL.getSmallestLegalIntType(Init->getContext(), ArrayElementCount);

    if (Ty) {
      Value *V = Builder.CreateIntCast(Idx, Ty, false);
      V = Builder.CreateLShr(ConstantInt::get(Ty, MagicBitvector), V);
      V = Builder.CreateAnd(ConstantInt::get(Ty, 1), V);
      return new ICmpInst(ICmpInst::ICMP_NE, V, ConstantInt::get(Ty, 0));
    }
  }

  return nullptr;
}

/// Return a value that can be used to compare the *offset* implied by a GEP to
/// zero. For example, if we have &A[i], we want to return 'i' for
/// "icmp ne i, 0". Note that, in general, indices can be complex, and scales
/// are involved. The above expression would also be legal to codegen as
/// "icmp ne (i*4), 0" (assuming A is a pointer to i32).
/// This latter form is less amenable to optimization though, and we are allowed
/// to generate the first by knowing that pointer arithmetic doesn't overflow.
///
/// If we can't emit an optimized form for this expression, this returns null.
///
static Value *evaluateGEPOffsetExpression(User *GEP, InstCombiner &IC,
                                          const DataLayout &DL) {
  gep_type_iterator GTI = gep_type_begin(GEP);

  // Check to see if this gep only has a single variable index.  If so, and if
  // any constant indices are a multiple of its scale, then we can compute this
  // in terms of the scale of the variable index.  For example, if the GEP
  // implies an offset of "12 + i*4", then we can codegen this as "3 + i",
  // because the expression will cross zero at the same point.
  unsigned i, e = GEP->getNumOperands();
  int64_t Offset = 0;
  for (i = 1; i != e; ++i, ++GTI) {
    if (ConstantInt *CI = dyn_cast<ConstantInt>(GEP->getOperand(i))) {
      // Compute the aggregate offset of constant indices.
      if (CI->isZero()) continue;

      // Handle a struct index, which adds its field offset to the pointer.
      if (StructType *STy = GTI.getStructTypeOrNull()) {
        Offset += DL.getStructLayout(STy)->getElementOffset(CI->getZExtValue());
      } else {
        uint64_t Size = DL.getTypeAllocSize(GTI.getIndexedType());
        Offset += Size*CI->getSExtValue();
      }
    } else {
      // Found our variable index.
      break;
    }
  }

  // If there are no variable indices, we must have a constant offset, just
  // evaluate it the general way.
  if (i == e) return nullptr;

  Value *VariableIdx = GEP->getOperand(i);
  // Determine the scale factor of the variable element.  For example, this is
  // 4 if the variable index is into an array of i32.
  uint64_t VariableScale = DL.getTypeAllocSize(GTI.getIndexedType());

  // Verify that there are no other variable indices.  If so, emit the hard way.
  for (++i, ++GTI; i != e; ++i, ++GTI) {
    ConstantInt *CI = dyn_cast<ConstantInt>(GEP->getOperand(i));
    if (!CI) return nullptr;

    // Compute the aggregate offset of constant indices.
    if (CI->isZero()) continue;

    // Handle a struct index, which adds its field offset to the pointer.
    if (StructType *STy = GTI.getStructTypeOrNull()) {
      Offset += DL.getStructLayout(STy)->getElementOffset(CI->getZExtValue());
    } else {
      uint64_t Size = DL.getTypeAllocSize(GTI.getIndexedType());
      Offset += Size*CI->getSExtValue();
    }
  }

  // Okay, we know we have a single variable index, which must be a
  // pointer/array/vector index.  If there is no offset, life is simple, return
  // the index.
  Type *IntPtrTy = DL.getIntPtrType(GEP->getOperand(0)->getType());
  unsigned IntPtrWidth = IntPtrTy->getIntegerBitWidth();
  if (Offset == 0) {
    // Cast to intptrty in case a truncation occurs.  If an extension is needed,
    // we don't need to bother extending: the extension won't affect where the
    // computation crosses zero.
    if (VariableIdx->getType()->getPrimitiveSizeInBits() > IntPtrWidth) {
      VariableIdx = IC.Builder.CreateTrunc(VariableIdx, IntPtrTy);
    }
    return VariableIdx;
  }

  // Otherwise, there is an index.  The computation we will do will be modulo
  // the pointer size.
  Offset = SignExtend64(Offset, IntPtrWidth);
  VariableScale = SignExtend64(VariableScale, IntPtrWidth);

  // To do this transformation, any constant index must be a multiple of the
  // variable scale factor.  For example, we can evaluate "12 + 4*i" as "3 + i",
  // but we can't evaluate "10 + 3*i" in terms of i.  Check that the offset is a
  // multiple of the variable scale.
  int64_t NewOffs = Offset / (int64_t)VariableScale;
  if (Offset != NewOffs*(int64_t)VariableScale)
    return nullptr;

  // Okay, we can do this evaluation.  Start by converting the index to intptr.
  if (VariableIdx->getType() != IntPtrTy)
    VariableIdx = IC.Builder.CreateIntCast(VariableIdx, IntPtrTy,
                                            true /*Signed*/);
  Constant *OffsetVal = ConstantInt::get(IntPtrTy, NewOffs);
  return IC.Builder.CreateAdd(VariableIdx, OffsetVal, "offset");
}

/// Returns true if we can rewrite Start as a GEP with pointer Base
/// and some integer offset. The nodes that need to be re-written
/// for this transformation will be added to Explored.
static bool canRewriteGEPAsOffset(Value *Start, Value *Base,
                                  const DataLayout &DL,
                                  SetVector<Value *> &Explored) {
  SmallVector<Value *, 16> WorkList(1, Start);
  Explored.insert(Base);

  // The following traversal gives us an order which can be used
  // when doing the final transformation. Since in the final
  // transformation we create the PHI replacement instructions first,
  // we don't have to get them in any particular order.
  //
  // However, for other instructions we will have to traverse the
  // operands of an instruction first, which means that we have to
  // do a post-order traversal.
  while (!WorkList.empty()) {
    SetVector<PHINode *> PHIs;

    while (!WorkList.empty()) {
      if (Explored.size() >= 100)
        return false;

      Value *V = WorkList.back();

      if (Explored.count(V) != 0) {
        WorkList.pop_back();
        continue;
      }

      if (!isa<IntToPtrInst>(V) && !isa<PtrToIntInst>(V) &&
          !isa<GetElementPtrInst>(V) && !isa<PHINode>(V))
        // We've found some value that we can't explore which is different from
        // the base. Therefore we can't do this transformation.
        return false;

      if (isa<IntToPtrInst>(V) || isa<PtrToIntInst>(V)) {
        auto *CI = dyn_cast<CastInst>(V);
        if (!CI->isNoopCast(DL))
          return false;

        if (Explored.count(CI->getOperand(0)) == 0)
          WorkList.push_back(CI->getOperand(0));
      }

      if (auto *GEP = dyn_cast<GEPOperator>(V)) {
        // We're limiting the GEP to having one index. This will preserve
        // the original pointer type. We could handle more cases in the
        // future.
        if (GEP->getNumIndices() != 1 || !GEP->isInBounds() ||
            GEP->getType() != Start->getType())
          return false;

        if (Explored.count(GEP->getOperand(0)) == 0)
          WorkList.push_back(GEP->getOperand(0));
      }

      if (WorkList.back() == V) {
        WorkList.pop_back();
        // We've finished visiting this node, mark it as such.
        Explored.insert(V);
      }

      if (auto *PN = dyn_cast<PHINode>(V)) {
        // We cannot transform PHIs on unsplittable basic blocks.
        if (isa<CatchSwitchInst>(PN->getParent()->getTerminator()))
          return false;
        Explored.insert(PN);
        PHIs.insert(PN);
      }
    }

    // Explore the PHI nodes further.
    for (auto *PN : PHIs)
      for (Value *Op : PN->incoming_values())
        if (Explored.count(Op) == 0)
          WorkList.push_back(Op);
  }

  // Make sure that we can do this. Since we can't insert GEPs in a basic
  // block before a PHI node, we can't easily do this transformation if
  // we have PHI node users of transformed instructions.
  for (Value *Val : Explored) {
    for (Value *Use : Val->uses()) {

      auto *PHI = dyn_cast<PHINode>(Use);
      auto *Inst = dyn_cast<Instruction>(Val);

      if (Inst == Base || Inst == PHI || !Inst || !PHI ||
          Explored.count(PHI) == 0)
        continue;

      if (PHI->getParent() == Inst->getParent())
        return false;
    }
  }
  return true;
}

// Sets the appropriate insert point on Builder where we can add
// a replacement Instruction for V (if that is possible).
static void setInsertionPoint(IRBuilder<> &Builder, Value *V,
                              bool Before = true) {
  if (auto *PHI = dyn_cast<PHINode>(V)) {
    Builder.SetInsertPoint(&*PHI->getParent()->getFirstInsertionPt());
    return;
  }
  if (auto *I = dyn_cast<Instruction>(V)) {
    if (!Before)
      I = &*std::next(I->getIterator());
    Builder.SetInsertPoint(I);
    return;
  }
  if (auto *A = dyn_cast<Argument>(V)) {
    // Set the insertion point in the entry block.
    BasicBlock &Entry = A->getParent()->getEntryBlock();
    Builder.SetInsertPoint(&*Entry.getFirstInsertionPt());
    return;
  }
  // Otherwise, this is a constant and we don't need to set a new
  // insertion point.
  assert(isa<Constant>(V) && "Setting insertion point for unknown value!");
}

/// Returns a re-written value of Start as an indexed GEP using Base as a
/// pointer.
static Value *rewriteGEPAsOffset(Value *Start, Value *Base,
                                 const DataLayout &DL,
                                 SetVector<Value *> &Explored) {
  // Perform all the substitutions. This is a bit tricky because we can
  // have cycles in our use-def chains.
  // 1. Create the PHI nodes without any incoming values.
  // 2. Create all the other values.
  // 3. Add the edges for the PHI nodes.
  // 4. Emit GEPs to get the original pointers.
  // 5. Remove the original instructions.
  Type *IndexType = IntegerType::get(
      Base->getContext(), DL.getIndexTypeSizeInBits(Start->getType()));

  DenseMap<Value *, Value *> NewInsts;
  NewInsts[Base] = ConstantInt::getNullValue(IndexType);

  // Create the new PHI nodes, without adding any incoming values.
  for (Value *Val : Explored) {
    if (Val == Base)
      continue;
    // Create empty phi nodes. This avoids cyclic dependencies when creating
    // the remaining instructions.
    if (auto *PHI = dyn_cast<PHINode>(Val))
      NewInsts[PHI] = PHINode::Create(IndexType, PHI->getNumIncomingValues(),
                                      PHI->getName() + ".idx", PHI);
  }
  IRBuilder<> Builder(Base->getContext());

  // Create all the other instructions.
  for (Value *Val : Explored) {

    if (NewInsts.find(Val) != NewInsts.end())
      continue;

    if (auto *CI = dyn_cast<CastInst>(Val)) {
      NewInsts[CI] = NewInsts[CI->getOperand(0)];
      continue;
    }
    if (auto *GEP = dyn_cast<GEPOperator>(Val)) {
      Value *Index = NewInsts[GEP->getOperand(1)] ? NewInsts[GEP->getOperand(1)]
                                                  : GEP->getOperand(1);
      setInsertionPoint(Builder, GEP);
      // Indices might need to be sign extended. GEPs will magically do
      // this, but we need to do it ourselves here.
      if (Index->getType()->getScalarSizeInBits() !=
          NewInsts[GEP->getOperand(0)]->getType()->getScalarSizeInBits()) {
        Index = Builder.CreateSExtOrTrunc(
            Index, NewInsts[GEP->getOperand(0)]->getType(),
            GEP->getOperand(0)->getName() + ".sext");
      }

      auto *Op = NewInsts[GEP->getOperand(0)];
      if (isa<ConstantInt>(Op) && cast<ConstantInt>(Op)->isZero())
        NewInsts[GEP] = Index;
      else
        NewInsts[GEP] = Builder.CreateNSWAdd(
            Op, Index, GEP->getOperand(0)->getName() + ".add");
      continue;
    }
    if (isa<PHINode>(Val))
      continue;

    llvm_unreachable("Unexpected instruction type");
  }

  // Add the incoming values to the PHI nodes.
  for (Value *Val : Explored) {
    if (Val == Base)
      continue;
    // All the instructions have been created, we can now add edges to the
    // phi nodes.
    if (auto *PHI = dyn_cast<PHINode>(Val)) {
      PHINode *NewPhi = static_cast<PHINode *>(NewInsts[PHI]);
      for (unsigned I = 0, E = PHI->getNumIncomingValues(); I < E; ++I) {
        Value *NewIncoming = PHI->getIncomingValue(I);

        if (NewInsts.find(NewIncoming) != NewInsts.end())
          NewIncoming = NewInsts[NewIncoming];

        NewPhi->addIncoming(NewIncoming, PHI->getIncomingBlock(I));
      }
    }
  }

  for (Value *Val : Explored) {
    if (Val == Base)
      continue;

    // Depending on the type, for external users we have to emit
    // a GEP or a GEP + ptrtoint.
    setInsertionPoint(Builder, Val, false);

    // If required, create an inttoptr instruction for Base.
    Value *NewBase = Base;
    if (!Base->getType()->isPointerTy())
      NewBase = Builder.CreateBitOrPointerCast(Base, Start->getType(),
                                               Start->getName() + "to.ptr");

    Value *GEP = Builder.CreateInBoundsGEP(
        Start->getType()->getPointerElementType(), NewBase,
        makeArrayRef(NewInsts[Val]), Val->getName() + ".ptr");

    if (!Val->getType()->isPointerTy()) {
      Value *Cast = Builder.CreatePointerCast(GEP, Val->getType(),
                                              Val->getName() + ".conv");
      GEP = Cast;
    }
    Val->replaceAllUsesWith(GEP);
  }

  return NewInsts[Start];
}

/// Looks through GEPs, IntToPtrInsts and PtrToIntInsts in order to express
/// the input Value as a constant indexed GEP. Returns a pair containing
/// the GEPs Pointer and Index.
static std::pair<Value *, Value *>
getAsConstantIndexedAddress(Value *V, const DataLayout &DL) {
  Type *IndexType = IntegerType::get(V->getContext(),
                                     DL.getIndexTypeSizeInBits(V->getType()));

  Constant *Index = ConstantInt::getNullValue(IndexType);
  while (true) {
    if (GEPOperator *GEP = dyn_cast<GEPOperator>(V)) {
      // We accept only inbouds GEPs here to exclude the possibility of
      // overflow.
      if (!GEP->isInBounds())
        break;
      if (GEP->hasAllConstantIndices() && GEP->getNumIndices() == 1 &&
          GEP->getType() == V->getType()) {
        V = GEP->getOperand(0);
        Constant *GEPIndex = static_cast<Constant *>(GEP->getOperand(1));
        Index = ConstantExpr::getAdd(
            Index, ConstantExpr::getSExtOrBitCast(GEPIndex, IndexType));
        continue;
      }
      break;
    }
    if (auto *CI = dyn_cast<IntToPtrInst>(V)) {
      if (!CI->isNoopCast(DL))
        break;
      V = CI->getOperand(0);
      continue;
    }
    if (auto *CI = dyn_cast<PtrToIntInst>(V)) {
      if (!CI->isNoopCast(DL))
        break;
      V = CI->getOperand(0);
      continue;
    }
    break;
  }
  return {V, Index};
}

/// Converts (CMP GEPLHS, RHS) if this change would make RHS a constant.
/// We can look through PHIs, GEPs and casts in order to determine a common base
/// between GEPLHS and RHS.
static Instruction *transformToIndexedCompare(GEPOperator *GEPLHS, Value *RHS,
                                              ICmpInst::Predicate Cond,
                                              const DataLayout &DL) {
  if (!GEPLHS->hasAllConstantIndices())
    return nullptr;

  // Make sure the pointers have the same type.
  if (GEPLHS->getType() != RHS->getType())
    return nullptr;

  Value *PtrBase, *Index;
  std::tie(PtrBase, Index) = getAsConstantIndexedAddress(GEPLHS, DL);

  // The set of nodes that will take part in this transformation.
  SetVector<Value *> Nodes;

  if (!canRewriteGEPAsOffset(RHS, PtrBase, DL, Nodes))
    return nullptr;

  // We know we can re-write this as
  //  ((gep Ptr, OFFSET1) cmp (gep Ptr, OFFSET2)
  // Since we've only looked through inbouds GEPs we know that we
  // can't have overflow on either side. We can therefore re-write
  // this as:
  //   OFFSET1 cmp OFFSET2
  Value *NewRHS = rewriteGEPAsOffset(RHS, PtrBase, DL, Nodes);

  // RewriteGEPAsOffset has replaced RHS and all of its uses with a re-written
  // GEP having PtrBase as the pointer base, and has returned in NewRHS the
  // offset. Since Index is the offset of LHS to the base pointer, we will now
  // compare the offsets instead of comparing the pointers.
  return new ICmpInst(ICmpInst::getSignedPredicate(Cond), Index, NewRHS);
}

/// Fold comparisons between a GEP instruction and something else. At this point
/// we know that the GEP is on the LHS of the comparison.
Instruction *InstCombiner::foldGEPICmp(GEPOperator *GEPLHS, Value *RHS,
                                       ICmpInst::Predicate Cond,
                                       Instruction &I) {
  // Don't transform signed compares of GEPs into index compares. Even if the
  // GEP is inbounds, the final add of the base pointer can have signed overflow
  // and would change the result of the icmp.
  // e.g. "&foo[0] <s &foo[1]" can't be folded to "true" because "foo" could be
  // the maximum signed value for the pointer type.
  if (ICmpInst::isSigned(Cond))
    return nullptr;

  // Look through bitcasts and addrspacecasts. We do not however want to remove
  // 0 GEPs.
  if (!isa<GetElementPtrInst>(RHS))
    RHS = RHS->stripPointerCasts();

  Value *PtrBase = GEPLHS->getOperand(0);
  if (PtrBase == RHS && GEPLHS->isInBounds()) {
    // ((gep Ptr, OFFSET) cmp Ptr)   ---> (OFFSET cmp 0).
    // This transformation (ignoring the base and scales) is valid because we
    // know pointers can't overflow since the gep is inbounds.  See if we can
    // output an optimized form.
    Value *Offset = evaluateGEPOffsetExpression(GEPLHS, *this, DL);

    // If not, synthesize the offset the hard way.
    if (!Offset)
      Offset = EmitGEPOffset(GEPLHS);
    return new ICmpInst(ICmpInst::getSignedPredicate(Cond), Offset,
                        Constant::getNullValue(Offset->getType()));
  } else if (GEPOperator *GEPRHS = dyn_cast<GEPOperator>(RHS)) {
    // If the base pointers are different, but the indices are the same, just
    // compare the base pointer.
    if (PtrBase != GEPRHS->getOperand(0)) {
      bool IndicesTheSame = GEPLHS->getNumOperands()==GEPRHS->getNumOperands();
      IndicesTheSame &= GEPLHS->getOperand(0)->getType() ==
                        GEPRHS->getOperand(0)->getType();
      if (IndicesTheSame)
        for (unsigned i = 1, e = GEPLHS->getNumOperands(); i != e; ++i)
          if (GEPLHS->getOperand(i) != GEPRHS->getOperand(i)) {
            IndicesTheSame = false;
            break;
          }

      // If all indices are the same, just compare the base pointers.
      Type *BaseType = GEPLHS->getOperand(0)->getType();
      if (IndicesTheSame && CmpInst::makeCmpResultType(BaseType) == I.getType())
        return new ICmpInst(Cond, GEPLHS->getOperand(0), GEPRHS->getOperand(0));

      // If we're comparing GEPs with two base pointers that only differ in type
      // and both GEPs have only constant indices or just one use, then fold
      // the compare with the adjusted indices.
      if (GEPLHS->isInBounds() && GEPRHS->isInBounds() &&
          (GEPLHS->hasAllConstantIndices() || GEPLHS->hasOneUse()) &&
          (GEPRHS->hasAllConstantIndices() || GEPRHS->hasOneUse()) &&
          PtrBase->stripPointerCasts() ==
              GEPRHS->getOperand(0)->stripPointerCasts()) {
        Value *LOffset = EmitGEPOffset(GEPLHS);
        Value *ROffset = EmitGEPOffset(GEPRHS);

        // If we looked through an addrspacecast between different sized address
        // spaces, the LHS and RHS pointers are different sized
        // integers. Truncate to the smaller one.
        Type *LHSIndexTy = LOffset->getType();
        Type *RHSIndexTy = ROffset->getType();
        if (LHSIndexTy != RHSIndexTy) {
          if (LHSIndexTy->getPrimitiveSizeInBits() <
              RHSIndexTy->getPrimitiveSizeInBits()) {
            ROffset = Builder.CreateTrunc(ROffset, LHSIndexTy);
          } else
            LOffset = Builder.CreateTrunc(LOffset, RHSIndexTy);
        }

        Value *Cmp = Builder.CreateICmp(ICmpInst::getSignedPredicate(Cond),
                                        LOffset, ROffset);
        return replaceInstUsesWith(I, Cmp);
      }

      // Otherwise, the base pointers are different and the indices are
      // different. Try convert this to an indexed compare by looking through
      // PHIs/casts.
      return transformToIndexedCompare(GEPLHS, RHS, Cond, DL);
    }

    // If one of the GEPs has all zero indices, recurse.
    if (GEPLHS->hasAllZeroIndices())
      return foldGEPICmp(GEPRHS, GEPLHS->getOperand(0),
                         ICmpInst::getSwappedPredicate(Cond), I);

    // If the other GEP has all zero indices, recurse.
    if (GEPRHS->hasAllZeroIndices())
      return foldGEPICmp(GEPLHS, GEPRHS->getOperand(0), Cond, I);

    bool GEPsInBounds = GEPLHS->isInBounds() && GEPRHS->isInBounds();
    if (GEPLHS->getNumOperands() == GEPRHS->getNumOperands()) {
      // If the GEPs only differ by one index, compare it.
      unsigned NumDifferences = 0;  // Keep track of # differences.
      unsigned DiffOperand = 0;     // The operand that differs.
      for (unsigned i = 1, e = GEPRHS->getNumOperands(); i != e; ++i)
        if (GEPLHS->getOperand(i) != GEPRHS->getOperand(i)) {
          if (GEPLHS->getOperand(i)->getType()->getPrimitiveSizeInBits() !=
                   GEPRHS->getOperand(i)->getType()->getPrimitiveSizeInBits()) {
            // Irreconcilable differences.
            NumDifferences = 2;
            break;
          } else {
            if (NumDifferences++) break;
            DiffOperand = i;
          }
        }

      if (NumDifferences == 0)   // SAME GEP?
        return replaceInstUsesWith(I, // No comparison is needed here.
          ConstantInt::get(I.getType(), ICmpInst::isTrueWhenEqual(Cond)));

      else if (NumDifferences == 1 && GEPsInBounds) {
        Value *LHSV = GEPLHS->getOperand(DiffOperand);
        Value *RHSV = GEPRHS->getOperand(DiffOperand);
        // Make sure we do a signed comparison here.
        return new ICmpInst(ICmpInst::getSignedPredicate(Cond), LHSV, RHSV);
      }
    }

    // Only lower this if the icmp is the only user of the GEP or if we expect
    // the result to fold to a constant!
    if (GEPsInBounds && (isa<ConstantExpr>(GEPLHS) || GEPLHS->hasOneUse()) &&
        (isa<ConstantExpr>(GEPRHS) || GEPRHS->hasOneUse())) {
      // ((gep Ptr, OFFSET1) cmp (gep Ptr, OFFSET2)  --->  (OFFSET1 cmp OFFSET2)
      Value *L = EmitGEPOffset(GEPLHS);
      Value *R = EmitGEPOffset(GEPRHS);
      return new ICmpInst(ICmpInst::getSignedPredicate(Cond), L, R);
    }
  }

  // Try convert this to an indexed compare by looking through PHIs/casts as a
  // last resort.
  return transformToIndexedCompare(GEPLHS, RHS, Cond, DL);
}

Instruction *InstCombiner::foldAllocaCmp(ICmpInst &ICI,
                                         const AllocaInst *Alloca,
                                         const Value *Other) {
  assert(ICI.isEquality() && "Cannot fold non-equality comparison.");

  // It would be tempting to fold away comparisons between allocas and any
  // pointer not based on that alloca (e.g. an argument). However, even
  // though such pointers cannot alias, they can still compare equal.
  //
  // But LLVM doesn't specify where allocas get their memory, so if the alloca
  // doesn't escape we can argue that it's impossible to guess its value, and we
  // can therefore act as if any such guesses are wrong.
  //
  // The code below checks that the alloca doesn't escape, and that it's only
  // used in a comparison once (the current instruction). The
  // single-comparison-use condition ensures that we're trivially folding all
  // comparisons against the alloca consistently, and avoids the risk of
  // erroneously folding a comparison of the pointer with itself.

  unsigned MaxIter = 32; // Break cycles and bound to constant-time.

  SmallVector<const Use *, 32> Worklist;
  for (const Use &U : Alloca->uses()) {
    if (Worklist.size() >= MaxIter)
      return nullptr;
    Worklist.push_back(&U);
  }

  unsigned NumCmps = 0;
  while (!Worklist.empty()) {
    assert(Worklist.size() <= MaxIter);
    const Use *U = Worklist.pop_back_val();
    const Value *V = U->getUser();
    --MaxIter;

    if (isa<BitCastInst>(V) || isa<GetElementPtrInst>(V) || isa<PHINode>(V) ||
        isa<SelectInst>(V)) {
      // Track the uses.
    } else if (isa<LoadInst>(V)) {
      // Loading from the pointer doesn't escape it.
      continue;
    } else if (const auto *SI = dyn_cast<StoreInst>(V)) {
      // Storing *to* the pointer is fine, but storing the pointer escapes it.
      if (SI->getValueOperand() == U->get())
        return nullptr;
      continue;
    } else if (isa<ICmpInst>(V)) {
      if (NumCmps++)
        return nullptr; // Found more than one cmp.
      continue;
    } else if (const auto *Intrin = dyn_cast<IntrinsicInst>(V)) {
      switch (Intrin->getIntrinsicID()) {
        // These intrinsics don't escape or compare the pointer. Memset is safe
        // because we don't allow ptrtoint. Memcpy and memmove are safe because
        // we don't allow stores, so src cannot point to V.
        case Intrinsic::lifetime_start: case Intrinsic::lifetime_end:
        case Intrinsic::memcpy: case Intrinsic::memmove: case Intrinsic::memset:
          continue;
        default:
          return nullptr;
      }
    } else {
      return nullptr;
    }
    for (const Use &U : V->uses()) {
      if (Worklist.size() >= MaxIter)
        return nullptr;
      Worklist.push_back(&U);
    }
  }

  Type *CmpTy = CmpInst::makeCmpResultType(Other->getType());
  return replaceInstUsesWith(
      ICI,
      ConstantInt::get(CmpTy, !CmpInst::isTrueWhenEqual(ICI.getPredicate())));
}

/// Fold "icmp pred (X+C), X".
Instruction *InstCombiner::foldICmpAddOpConst(Value *X, const APInt &C,
                                              ICmpInst::Predicate Pred) {
  // From this point on, we know that (X+C <= X) --> (X+C < X) because C != 0,
  // so the values can never be equal.  Similarly for all other "or equals"
  // operators.
  assert(!!C && "C should not be zero!");

  // (X+1) <u X        --> X >u (MAXUINT-1)        --> X == 255
  // (X+2) <u X        --> X >u (MAXUINT-2)        --> X > 253
  // (X+MAXUINT) <u X  --> X >u (MAXUINT-MAXUINT)  --> X != 0
  if (Pred == ICmpInst::ICMP_ULT || Pred == ICmpInst::ICMP_ULE) {
    Constant *R = ConstantInt::get(X->getType(),
                                   APInt::getMaxValue(C.getBitWidth()) - C);
    return new ICmpInst(ICmpInst::ICMP_UGT, X, R);
  }

  // (X+1) >u X        --> X <u (0-1)        --> X != 255
  // (X+2) >u X        --> X <u (0-2)        --> X <u 254
  // (X+MAXUINT) >u X  --> X <u (0-MAXUINT)  --> X <u 1  --> X == 0
  if (Pred == ICmpInst::ICMP_UGT || Pred == ICmpInst::ICMP_UGE)
    return new ICmpInst(ICmpInst::ICMP_ULT, X,
                        ConstantInt::get(X->getType(), -C));

  APInt SMax = APInt::getSignedMaxValue(C.getBitWidth());

  // (X+ 1) <s X       --> X >s (MAXSINT-1)          --> X == 127
  // (X+ 2) <s X       --> X >s (MAXSINT-2)          --> X >s 125
  // (X+MAXSINT) <s X  --> X >s (MAXSINT-MAXSINT)    --> X >s 0
  // (X+MINSINT) <s X  --> X >s (MAXSINT-MINSINT)    --> X >s -1
  // (X+ -2) <s X      --> X >s (MAXSINT- -2)        --> X >s 126
  // (X+ -1) <s X      --> X >s (MAXSINT- -1)        --> X != 127
  if (Pred == ICmpInst::ICMP_SLT || Pred == ICmpInst::ICMP_SLE)
    return new ICmpInst(ICmpInst::ICMP_SGT, X,
                        ConstantInt::get(X->getType(), SMax - C));

  // (X+ 1) >s X       --> X <s (MAXSINT-(1-1))       --> X != 127
  // (X+ 2) >s X       --> X <s (MAXSINT-(2-1))       --> X <s 126
  // (X+MAXSINT) >s X  --> X <s (MAXSINT-(MAXSINT-1)) --> X <s 1
  // (X+MINSINT) >s X  --> X <s (MAXSINT-(MINSINT-1)) --> X <s -2
  // (X+ -2) >s X      --> X <s (MAXSINT-(-2-1))      --> X <s -126
  // (X+ -1) >s X      --> X <s (MAXSINT-(-1-1))      --> X == -128

  assert(Pred == ICmpInst::ICMP_SGT || Pred == ICmpInst::ICMP_SGE);
  return new ICmpInst(ICmpInst::ICMP_SLT, X,
                      ConstantInt::get(X->getType(), SMax - (C - 1)));
}

/// Handle "(icmp eq/ne (ashr/lshr AP2, A), AP1)" ->
/// (icmp eq/ne A, Log2(AP2/AP1)) ->
/// (icmp eq/ne A, Log2(AP2) - Log2(AP1)).
Instruction *InstCombiner::foldICmpShrConstConst(ICmpInst &I, Value *A,
                                                 const APInt &AP1,
                                                 const APInt &AP2) {
  assert(I.isEquality() && "Cannot fold icmp gt/lt");

  auto getICmp = [&I](CmpInst::Predicate Pred, Value *LHS, Value *RHS) {
    if (I.getPredicate() == I.ICMP_NE)
      Pred = CmpInst::getInversePredicate(Pred);
    return new ICmpInst(Pred, LHS, RHS);
  };

  // Don't bother doing any work for cases which InstSimplify handles.
  if (AP2.isNullValue())
    return nullptr;

  bool IsAShr = isa<AShrOperator>(I.getOperand(0));
  if (IsAShr) {
    if (AP2.isAllOnesValue())
      return nullptr;
    if (AP2.isNegative() != AP1.isNegative())
      return nullptr;
    if (AP2.sgt(AP1))
      return nullptr;
  }

  if (!AP1)
    // 'A' must be large enough to shift out the highest set bit.
    return getICmp(I.ICMP_UGT, A,
                   ConstantInt::get(A->getType(), AP2.logBase2()));

  if (AP1 == AP2)
    return getICmp(I.ICMP_EQ, A, ConstantInt::getNullValue(A->getType()));

  int Shift;
  if (IsAShr && AP1.isNegative())
    Shift = AP1.countLeadingOnes() - AP2.countLeadingOnes();
  else
    Shift = AP1.countLeadingZeros() - AP2.countLeadingZeros();

  if (Shift > 0) {
    if (IsAShr && AP1 == AP2.ashr(Shift)) {
      // There are multiple solutions if we are comparing against -1 and the LHS
      // of the ashr is not a power of two.
      if (AP1.isAllOnesValue() && !AP2.isPowerOf2())
        return getICmp(I.ICMP_UGE, A, ConstantInt::get(A->getType(), Shift));
      return getICmp(I.ICMP_EQ, A, ConstantInt::get(A->getType(), Shift));
    } else if (AP1 == AP2.lshr(Shift)) {
      return getICmp(I.ICMP_EQ, A, ConstantInt::get(A->getType(), Shift));
    }
  }

  // Shifting const2 will never be equal to const1.
  // FIXME: This should always be handled by InstSimplify?
  auto *TorF = ConstantInt::get(I.getType(), I.getPredicate() == I.ICMP_NE);
  return replaceInstUsesWith(I, TorF);
}

/// Handle "(icmp eq/ne (shl AP2, A), AP1)" ->
/// (icmp eq/ne A, TrailingZeros(AP1) - TrailingZeros(AP2)).
Instruction *InstCombiner::foldICmpShlConstConst(ICmpInst &I, Value *A,
                                                 const APInt &AP1,
                                                 const APInt &AP2) {
  assert(I.isEquality() && "Cannot fold icmp gt/lt");

  auto getICmp = [&I](CmpInst::Predicate Pred, Value *LHS, Value *RHS) {
    if (I.getPredicate() == I.ICMP_NE)
      Pred = CmpInst::getInversePredicate(Pred);
    return new ICmpInst(Pred, LHS, RHS);
  };

  // Don't bother doing any work for cases which InstSimplify handles.
  if (AP2.isNullValue())
    return nullptr;

  unsigned AP2TrailingZeros = AP2.countTrailingZeros();

  if (!AP1 && AP2TrailingZeros != 0)
    return getICmp(
        I.ICMP_UGE, A,
        ConstantInt::get(A->getType(), AP2.getBitWidth() - AP2TrailingZeros));

  if (AP1 == AP2)
    return getICmp(I.ICMP_EQ, A, ConstantInt::getNullValue(A->getType()));

  // Get the distance between the lowest bits that are set.
  int Shift = AP1.countTrailingZeros() - AP2TrailingZeros;

  if (Shift > 0 && AP2.shl(Shift) == AP1)
    return getICmp(I.ICMP_EQ, A, ConstantInt::get(A->getType(), Shift));

  // Shifting const2 will never be equal to const1.
  // FIXME: This should always be handled by InstSimplify?
  auto *TorF = ConstantInt::get(I.getType(), I.getPredicate() == I.ICMP_NE);
  return replaceInstUsesWith(I, TorF);
}

/// The caller has matched a pattern of the form:
///   I = icmp ugt (add (add A, B), CI2), CI1
/// If this is of the form:
///   sum = a + b
///   if (sum+128 >u 255)
/// Then replace it with llvm.sadd.with.overflow.i8.
///
static Instruction *processUGT_ADDCST_ADD(ICmpInst &I, Value *A, Value *B,
                                          ConstantInt *CI2, ConstantInt *CI1,
                                          InstCombiner &IC) {
  // The transformation we're trying to do here is to transform this into an
  // llvm.sadd.with.overflow.  To do this, we have to replace the original add
  // with a narrower add, and discard the add-with-constant that is part of the
  // range check (if we can't eliminate it, this isn't profitable).

  // In order to eliminate the add-with-constant, the compare can be its only
  // use.
  Instruction *AddWithCst = cast<Instruction>(I.getOperand(0));
  if (!AddWithCst->hasOneUse())
    return nullptr;

  // If CI2 is 2^7, 2^15, 2^31, then it might be an sadd.with.overflow.
  if (!CI2->getValue().isPowerOf2())
    return nullptr;
  unsigned NewWidth = CI2->getValue().countTrailingZeros();
  if (NewWidth != 7 && NewWidth != 15 && NewWidth != 31)
    return nullptr;

  // The width of the new add formed is 1 more than the bias.
  ++NewWidth;

  // Check to see that CI1 is an all-ones value with NewWidth bits.
  if (CI1->getBitWidth() == NewWidth ||
      CI1->getValue() != APInt::getLowBitsSet(CI1->getBitWidth(), NewWidth))
    return nullptr;

  // This is only really a signed overflow check if the inputs have been
  // sign-extended; check for that condition. For example, if CI2 is 2^31 and
  // the operands of the add are 64 bits wide, we need at least 33 sign bits.
  unsigned NeededSignBits = CI1->getBitWidth() - NewWidth + 1;
  if (IC.ComputeNumSignBits(A, 0, &I) < NeededSignBits ||
      IC.ComputeNumSignBits(B, 0, &I) < NeededSignBits)
    return nullptr;

  // In order to replace the original add with a narrower
  // llvm.sadd.with.overflow, the only uses allowed are the add-with-constant
  // and truncates that discard the high bits of the add.  Verify that this is
  // the case.
  Instruction *OrigAdd = cast<Instruction>(AddWithCst->getOperand(0));
  for (User *U : OrigAdd->users()) {
    if (U == AddWithCst)
      continue;

    // Only accept truncates for now.  We would really like a nice recursive
    // predicate like SimplifyDemandedBits, but which goes downwards the use-def
    // chain to see which bits of a value are actually demanded.  If the
    // original add had another add which was then immediately truncated, we
    // could still do the transformation.
    TruncInst *TI = dyn_cast<TruncInst>(U);
    if (!TI || TI->getType()->getPrimitiveSizeInBits() > NewWidth)
      return nullptr;
  }

  // If the pattern matches, truncate the inputs to the narrower type and
  // use the sadd_with_overflow intrinsic to efficiently compute both the
  // result and the overflow bit.
  Type *NewType = IntegerType::get(OrigAdd->getContext(), NewWidth);
  Value *F = Intrinsic::getDeclaration(I.getModule(),
                                       Intrinsic::sadd_with_overflow, NewType);

  InstCombiner::BuilderTy &Builder = IC.Builder;

  // Put the new code above the original add, in case there are any uses of the
  // add between the add and the compare.
  Builder.SetInsertPoint(OrigAdd);

  Value *TruncA = Builder.CreateTrunc(A, NewType, A->getName() + ".trunc");
  Value *TruncB = Builder.CreateTrunc(B, NewType, B->getName() + ".trunc");
  CallInst *Call = Builder.CreateCall(F, {TruncA, TruncB}, "sadd");
  Value *Add = Builder.CreateExtractValue(Call, 0, "sadd.result");
  Value *ZExt = Builder.CreateZExt(Add, OrigAdd->getType());

  // The inner add was the result of the narrow add, zero extended to the
  // wider type.  Replace it with the result computed by the intrinsic.
  IC.replaceInstUsesWith(*OrigAdd, ZExt);

  // The original icmp gets replaced with the overflow value.
  return ExtractValueInst::Create(Call, 1, "sadd.overflow");
}

// Handle (icmp sgt smin(PosA, B) 0) -> (icmp sgt B 0)
Instruction *InstCombiner::foldICmpWithZero(ICmpInst &Cmp) {
  CmpInst::Predicate Pred = Cmp.getPredicate();
  Value *X = Cmp.getOperand(0);

  if (match(Cmp.getOperand(1), m_Zero()) && Pred == ICmpInst::ICMP_SGT) {
    Value *A, *B;
    SelectPatternResult SPR = matchSelectPattern(X, A, B);
    if (SPR.Flavor == SPF_SMIN) {
      if (isKnownPositive(A, DL, 0, &AC, &Cmp, &DT))
        return new ICmpInst(Pred, B, Cmp.getOperand(1));
      if (isKnownPositive(B, DL, 0, &AC, &Cmp, &DT))
        return new ICmpInst(Pred, A, Cmp.getOperand(1));
    }
  }
  return nullptr;
}

/// Fold icmp Pred X, C.
/// TODO: This code structure does not make sense. The saturating add fold
/// should be moved to some other helper and extended as noted below (it is also
/// possible that code has been made unnecessary - do we canonicalize IR to
/// overflow/saturating intrinsics or not?).
Instruction *InstCombiner::foldICmpWithConstant(ICmpInst &Cmp) {
  // Match the following pattern, which is a common idiom when writing
  // overflow-safe integer arithmetic functions. The source performs an addition
  // in wider type and explicitly checks for overflow using comparisons against
  // INT_MIN and INT_MAX. Simplify by using the sadd_with_overflow intrinsic.
  //
  // TODO: This could probably be generalized to handle other overflow-safe
  // operations if we worked out the formulas to compute the appropriate magic
  // constants.
  //
  // sum = a + b
  // if (sum+128 >u 255)  ...  -> llvm.sadd.with.overflow.i8
  CmpInst::Predicate Pred = Cmp.getPredicate();
  Value *Op0 = Cmp.getOperand(0), *Op1 = Cmp.getOperand(1);
  Value *A, *B;
  ConstantInt *CI, *CI2; // I = icmp ugt (add (add A, B), CI2), CI
  if (Pred == ICmpInst::ICMP_UGT && match(Op1, m_ConstantInt(CI)) &&
      match(Op0, m_Add(m_Add(m_Value(A), m_Value(B)), m_ConstantInt(CI2))))
    if (Instruction *Res = processUGT_ADDCST_ADD(Cmp, A, B, CI2, CI, *this))
      return Res;

  return nullptr;
}

/// Canonicalize icmp instructions based on dominating conditions.
Instruction *InstCombiner::foldICmpWithDominatingICmp(ICmpInst &Cmp) {
  // This is a cheap/incomplete check for dominance - just match a single
  // predecessor with a conditional branch.
  BasicBlock *CmpBB = Cmp.getParent();
  BasicBlock *DomBB = CmpBB->getSinglePredecessor();
  if (!DomBB)
    return nullptr;

  Value *DomCond;
  BasicBlock *TrueBB, *FalseBB;
  if (!match(DomBB->getTerminator(), m_Br(m_Value(DomCond), TrueBB, FalseBB)))
    return nullptr;

  assert((TrueBB == CmpBB || FalseBB == CmpBB) &&
         "Predecessor block does not point to successor?");

  // The branch should get simplified. Don't bother simplifying this condition.
  if (TrueBB == FalseBB)
    return nullptr;

  // Try to simplify this compare to T/F based on the dominating condition.
  Optional<bool> Imp = isImpliedCondition(DomCond, &Cmp, DL, TrueBB == CmpBB);
  if (Imp)
    return replaceInstUsesWith(Cmp, ConstantInt::get(Cmp.getType(), *Imp));

  CmpInst::Predicate Pred = Cmp.getPredicate();
  Value *X = Cmp.getOperand(0), *Y = Cmp.getOperand(1);
  ICmpInst::Predicate DomPred;
  const APInt *C, *DomC;
  if (match(DomCond, m_ICmp(DomPred, m_Specific(X), m_APInt(DomC))) &&
      match(Y, m_APInt(C))) {
    // We have 2 compares of a variable with constants. Calculate the constant
    // ranges of those compares to see if we can transform the 2nd compare:
    // DomBB:
    //   DomCond = icmp DomPred X, DomC
    //   br DomCond, CmpBB, FalseBB
    // CmpBB:
    //   Cmp = icmp Pred X, C
    ConstantRange CR = ConstantRange::makeAllowedICmpRegion(Pred, *C);
    ConstantRange DominatingCR =
        (CmpBB == TrueBB) ? ConstantRange::makeExactICmpRegion(DomPred, *DomC)
                          : ConstantRange::makeExactICmpRegion(
                                CmpInst::getInversePredicate(DomPred), *DomC);
    ConstantRange Intersection = DominatingCR.intersectWith(CR);
    ConstantRange Difference = DominatingCR.difference(CR);
    if (Intersection.isEmptySet())
      return replaceInstUsesWith(Cmp, Builder.getFalse());
    if (Difference.isEmptySet())
      return replaceInstUsesWith(Cmp, Builder.getTrue());

    // Canonicalizing a sign bit comparison that gets used in a branch,
    // pessimizes codegen by generating branch on zero instruction instead
    // of a test and branch. So we avoid canonicalizing in such situations
    // because test and branch instruction has better branch displacement
    // than compare and branch instruction.
    bool UnusedBit;
    bool IsSignBit = isSignBitCheck(Pred, *C, UnusedBit);
    if (Cmp.isEquality() || (IsSignBit && hasBranchUse(Cmp)))
      return nullptr;

    if (const APInt *EqC = Intersection.getSingleElement())
      return new ICmpInst(ICmpInst::ICMP_EQ, X, Builder.getInt(*EqC));
    if (const APInt *NeC = Difference.getSingleElement())
      return new ICmpInst(ICmpInst::ICMP_NE, X, Builder.getInt(*NeC));
  }

  return nullptr;
}

/// Fold icmp (trunc X, Y), C.
Instruction *InstCombiner::foldICmpTruncConstant(ICmpInst &Cmp,
                                                 TruncInst *Trunc,
                                                 const APInt &C) {
  ICmpInst::Predicate Pred = Cmp.getPredicate();
  Value *X = Trunc->getOperand(0);
  if (C.isOneValue() && C.getBitWidth() > 1) {
    // icmp slt trunc(signum(V)) 1 --> icmp slt V, 1
    Value *V = nullptr;
    if (Pred == ICmpInst::ICMP_SLT && match(X, m_Signum(m_Value(V))))
      return new ICmpInst(ICmpInst::ICMP_SLT, V,
                          ConstantInt::get(V->getType(), 1));
  }

  if (Cmp.isEquality() && Trunc->hasOneUse()) {
    // Simplify icmp eq (trunc x to i8), 42 -> icmp eq x, 42|highbits if all
    // of the high bits truncated out of x are known.
    unsigned DstBits = Trunc->getType()->getScalarSizeInBits(),
             SrcBits = X->getType()->getScalarSizeInBits();
    KnownBits Known = computeKnownBits(X, 0, &Cmp);

    // If all the high bits are known, we can do this xform.
    if ((Known.Zero | Known.One).countLeadingOnes() >= SrcBits - DstBits) {
      // Pull in the high bits from known-ones set.
      APInt NewRHS = C.zext(SrcBits);
      NewRHS |= Known.One & APInt::getHighBitsSet(SrcBits, SrcBits - DstBits);
      return new ICmpInst(Pred, X, ConstantInt::get(X->getType(), NewRHS));
    }
  }

  return nullptr;
}

/// Fold icmp (xor X, Y), C.
Instruction *InstCombiner::foldICmpXorConstant(ICmpInst &Cmp,
                                               BinaryOperator *Xor,
                                               const APInt &C) {
  Value *X = Xor->getOperand(0);
  Value *Y = Xor->getOperand(1);
  const APInt *XorC;
  if (!match(Y, m_APInt(XorC)))
    return nullptr;

  // If this is a comparison that tests the signbit (X < 0) or (x > -1),
  // fold the xor.
  ICmpInst::Predicate Pred = Cmp.getPredicate();
  bool TrueIfSigned = false;
  if (isSignBitCheck(Cmp.getPredicate(), C, TrueIfSigned)) {

    // If the sign bit of the XorCst is not set, there is no change to
    // the operation, just stop using the Xor.
    if (!XorC->isNegative()) {
      Cmp.setOperand(0, X);
      Worklist.Add(Xor);
      return &Cmp;
    }

    // Emit the opposite comparison.
    if (TrueIfSigned)
      return new ICmpInst(ICmpInst::ICMP_SGT, X,
                          ConstantInt::getAllOnesValue(X->getType()));
    else
      return new ICmpInst(ICmpInst::ICMP_SLT, X,
                          ConstantInt::getNullValue(X->getType()));
  }

  if (Xor->hasOneUse()) {
    // (icmp u/s (xor X SignMask), C) -> (icmp s/u X, (xor C SignMask))
    if (!Cmp.isEquality() && XorC->isSignMask()) {
      Pred = Cmp.isSigned() ? Cmp.getUnsignedPredicate()
                            : Cmp.getSignedPredicate();
      return new ICmpInst(Pred, X, ConstantInt::get(X->getType(), C ^ *XorC));
    }

    // (icmp u/s (xor X ~SignMask), C) -> (icmp s/u X, (xor C ~SignMask))
    if (!Cmp.isEquality() && XorC->isMaxSignedValue()) {
      Pred = Cmp.isSigned() ? Cmp.getUnsignedPredicate()
                            : Cmp.getSignedPredicate();
      Pred = Cmp.getSwappedPredicate(Pred);
      return new ICmpInst(Pred, X, ConstantInt::get(X->getType(), C ^ *XorC));
    }
  }

  // Mask constant magic can eliminate an 'xor' with unsigned compares.
  if (Pred == ICmpInst::ICMP_UGT) {
    // (xor X, ~C) >u C --> X <u ~C (when C+1 is a power of 2)
    if (*XorC == ~C && (C + 1).isPowerOf2())
      return new ICmpInst(ICmpInst::ICMP_ULT, X, Y);
    // (xor X, C) >u C --> X >u C (when C+1 is a power of 2)
    if (*XorC == C && (C + 1).isPowerOf2())
      return new ICmpInst(ICmpInst::ICMP_UGT, X, Y);
  }
  if (Pred == ICmpInst::ICMP_ULT) {
    // (xor X, -C) <u C --> X >u ~C (when C is a power of 2)
    if (*XorC == -C && C.isPowerOf2())
      return new ICmpInst(ICmpInst::ICMP_UGT, X,
                          ConstantInt::get(X->getType(), ~C));
    // (xor X, C) <u C --> X >u ~C (when -C is a power of 2)
    if (*XorC == C && (-C).isPowerOf2())
      return new ICmpInst(ICmpInst::ICMP_UGT, X,
                          ConstantInt::get(X->getType(), ~C));
  }
  return nullptr;
}

/// Fold icmp (and (sh X, Y), C2), C1.
Instruction *InstCombiner::foldICmpAndShift(ICmpInst &Cmp, BinaryOperator *And,
                                            const APInt &C1, const APInt &C2) {
  BinaryOperator *Shift = dyn_cast<BinaryOperator>(And->getOperand(0));
  if (!Shift || !Shift->isShift())
    return nullptr;

  // If this is: (X >> C3) & C2 != C1 (where any shift and any compare could
  // exist), turn it into (X & (C2 << C3)) != (C1 << C3). This happens a LOT in
  // code produced by the clang front-end, for bitfield access.
  // This seemingly simple opportunity to fold away a shift turns out to be
  // rather complicated. See PR17827 for details.
  unsigned ShiftOpcode = Shift->getOpcode();
  bool IsShl = ShiftOpcode == Instruction::Shl;
  const APInt *C3;
  if (match(Shift->getOperand(1), m_APInt(C3))) {
    bool CanFold = false;
    if (ShiftOpcode == Instruction::Shl) {
      // For a left shift, we can fold if the comparison is not signed. We can
      // also fold a signed comparison if the mask value and comparison value
      // are not negative. These constraints may not be obvious, but we can
      // prove that they are correct using an SMT solver.
      if (!Cmp.isSigned() || (!C2.isNegative() && !C1.isNegative()))
        CanFold = true;
    } else {
      bool IsAshr = ShiftOpcode == Instruction::AShr;
      // For a logical right shift, we can fold if the comparison is not signed.
      // We can also fold a signed comparison if the shifted mask value and the
      // shifted comparison value are not negative. These constraints may not be
      // obvious, but we can prove that they are correct using an SMT solver.
      // For an arithmetic shift right we can do the same, if we ensure
      // the And doesn't use any bits being shifted in. Normally these would
      // be turned into lshr by SimplifyDemandedBits, but not if there is an
      // additional user.
      if (!IsAshr || (C2.shl(*C3).lshr(*C3) == C2)) {
        if (!Cmp.isSigned() ||
            (!C2.shl(*C3).isNegative() && !C1.shl(*C3).isNegative()))
          CanFold = true;
      }
    }

    if (CanFold) {
      APInt NewCst = IsShl ? C1.lshr(*C3) : C1.shl(*C3);
      APInt SameAsC1 = IsShl ? NewCst.shl(*C3) : NewCst.lshr(*C3);
      // Check to see if we are shifting out any of the bits being compared.
      if (SameAsC1 != C1) {
        // If we shifted bits out, the fold is not going to work out. As a
        // special case, check to see if this means that the result is always
        // true or false now.
        if (Cmp.getPredicate() == ICmpInst::ICMP_EQ)
          return replaceInstUsesWith(Cmp, ConstantInt::getFalse(Cmp.getType()));
        if (Cmp.getPredicate() == ICmpInst::ICMP_NE)
          return replaceInstUsesWith(Cmp, ConstantInt::getTrue(Cmp.getType()));
      } else {
        Cmp.setOperand(1, ConstantInt::get(And->getType(), NewCst));
        APInt NewAndCst = IsShl ? C2.lshr(*C3) : C2.shl(*C3);
        And->setOperand(1, ConstantInt::get(And->getType(), NewAndCst));
        And->setOperand(0, Shift->getOperand(0));
        Worklist.Add(Shift); // Shift is dead.
        return &Cmp;
      }
    }
  }

  // Turn ((X >> Y) & C2) == 0  into  (X & (C2 << Y)) == 0.  The latter is
  // preferable because it allows the C2 << Y expression to be hoisted out of a
  // loop if Y is invariant and X is not.
  if (Shift->hasOneUse() && C1.isNullValue() && Cmp.isEquality() &&
      !Shift->isArithmeticShift() && !isa<Constant>(Shift->getOperand(0))) {
    // Compute C2 << Y.
    Value *NewShift =
        IsShl ? Builder.CreateLShr(And->getOperand(1), Shift->getOperand(1))
              : Builder.CreateShl(And->getOperand(1), Shift->getOperand(1));

    // Compute X & (C2 << Y).
    Value *NewAnd = Builder.CreateAnd(Shift->getOperand(0), NewShift);
    Cmp.setOperand(0, NewAnd);
    return &Cmp;
  }

  return nullptr;
}

/// Fold icmp (and X, C2), C1.
Instruction *InstCombiner::foldICmpAndConstConst(ICmpInst &Cmp,
                                                 BinaryOperator *And,
                                                 const APInt &C1) {
  // For vectors: icmp ne (and X, 1), 0 --> trunc X to N x i1
  // TODO: We canonicalize to the longer form for scalars because we have
  // better analysis/folds for icmp, and codegen may be better with icmp.
  if (Cmp.getPredicate() == CmpInst::ICMP_NE && Cmp.getType()->isVectorTy() &&
      C1.isNullValue() && match(And->getOperand(1), m_One()))
    return new TruncInst(And->getOperand(0), Cmp.getType());

  const APInt *C2;
  if (!match(And->getOperand(1), m_APInt(C2)))
    return nullptr;

  if (!And->hasOneUse())
    return nullptr;

  // If the LHS is an 'and' of a truncate and we can widen the and/compare to
  // the input width without changing the value produced, eliminate the cast:
  //
  // icmp (and (trunc W), C2), C1 -> icmp (and W, C2'), C1'
  //
  // We can do this transformation if the constants do not have their sign bits
  // set or if it is an equality comparison. Extending a relational comparison
  // when we're checking the sign bit would not work.
  Value *W;
  if (match(And->getOperand(0), m_OneUse(m_Trunc(m_Value(W)))) &&
      (Cmp.isEquality() || (!C1.isNegative() && !C2->isNegative()))) {
    // TODO: Is this a good transform for vectors? Wider types may reduce
    // throughput. Should this transform be limited (even for scalars) by using
    // shouldChangeType()?
    if (!Cmp.getType()->isVectorTy()) {
      Type *WideType = W->getType();
      unsigned WideScalarBits = WideType->getScalarSizeInBits();
      Constant *ZextC1 = ConstantInt::get(WideType, C1.zext(WideScalarBits));
      Constant *ZextC2 = ConstantInt::get(WideType, C2->zext(WideScalarBits));
      Value *NewAnd = Builder.CreateAnd(W, ZextC2, And->getName());
      return new ICmpInst(Cmp.getPredicate(), NewAnd, ZextC1);
    }
  }

  if (Instruction *I = foldICmpAndShift(Cmp, And, C1, *C2))
    return I;

  // (icmp pred (and (or (lshr A, B), A), 1), 0) -->
  // (icmp pred (and A, (or (shl 1, B), 1), 0))
  //
  // iff pred isn't signed
  if (!Cmp.isSigned() && C1.isNullValue() && And->getOperand(0)->hasOneUse() &&
      match(And->getOperand(1), m_One())) {
    Constant *One = cast<Constant>(And->getOperand(1));
    Value *Or = And->getOperand(0);
    Value *A, *B, *LShr;
    if (match(Or, m_Or(m_Value(LShr), m_Value(A))) &&
        match(LShr, m_LShr(m_Specific(A), m_Value(B)))) {
      unsigned UsesRemoved = 0;
      if (And->hasOneUse())
        ++UsesRemoved;
      if (Or->hasOneUse())
        ++UsesRemoved;
      if (LShr->hasOneUse())
        ++UsesRemoved;

      // Compute A & ((1 << B) | 1)
      Value *NewOr = nullptr;
      if (auto *C = dyn_cast<Constant>(B)) {
        if (UsesRemoved >= 1)
          NewOr = ConstantExpr::getOr(ConstantExpr::getNUWShl(One, C), One);
      } else {
        if (UsesRemoved >= 3)
          NewOr = Builder.CreateOr(Builder.CreateShl(One, B, LShr->getName(),
                                                     /*HasNUW=*/true),
                                   One, Or->getName());
      }
      if (NewOr) {
        Value *NewAnd = Builder.CreateAnd(A, NewOr, And->getName());
        Cmp.setOperand(0, NewAnd);
        return &Cmp;
      }
    }
  }

  return nullptr;
}

/// Fold icmp (and X, Y), C.
Instruction *InstCombiner::foldICmpAndConstant(ICmpInst &Cmp,
                                               BinaryOperator *And,
                                               const APInt &C) {
  if (Instruction *I = foldICmpAndConstConst(Cmp, And, C))
    return I;

  // TODO: These all require that Y is constant too, so refactor with the above.

  // Try to optimize things like "A[i] & 42 == 0" to index computations.
  Value *X = And->getOperand(0);
  Value *Y = And->getOperand(1);
  if (auto *LI = dyn_cast<LoadInst>(X))
    if (auto *GEP = dyn_cast<GetElementPtrInst>(LI->getOperand(0)))
      if (auto *GV = dyn_cast<GlobalVariable>(GEP->getOperand(0)))
        if (GV->isConstant() && GV->hasDefinitiveInitializer() &&
            !LI->isVolatile() && isa<ConstantInt>(Y)) {
          ConstantInt *C2 = cast<ConstantInt>(Y);
          if (Instruction *Res = foldCmpLoadFromIndexedGlobal(GEP, GV, Cmp, C2))
            return Res;
        }

  if (!Cmp.isEquality())
    return nullptr;

  // X & -C == -C -> X >  u ~C
  // X & -C != -C -> X <= u ~C
  //   iff C is a power of 2
  if (Cmp.getOperand(1) == Y && (-C).isPowerOf2()) {
    auto NewPred = Cmp.getPredicate() == CmpInst::ICMP_EQ ? CmpInst::ICMP_UGT
                                                          : CmpInst::ICMP_ULE;
    return new ICmpInst(NewPred, X, SubOne(cast<Constant>(Cmp.getOperand(1))));
  }

  // (X & C2) == 0 -> (trunc X) >= 0
  // (X & C2) != 0 -> (trunc X) <  0
  //   iff C2 is a power of 2 and it masks the sign bit of a legal integer type.
  const APInt *C2;
  if (And->hasOneUse() && C.isNullValue() && match(Y, m_APInt(C2))) {
    int32_t ExactLogBase2 = C2->exactLogBase2();
    if (ExactLogBase2 != -1 && DL.isLegalInteger(ExactLogBase2 + 1)) {
      Type *NTy = IntegerType::get(Cmp.getContext(), ExactLogBase2 + 1);
      if (And->getType()->isVectorTy())
        NTy = VectorType::get(NTy, And->getType()->getVectorNumElements());
      Value *Trunc = Builder.CreateTrunc(X, NTy);
      auto NewPred = Cmp.getPredicate() == CmpInst::ICMP_EQ ? CmpInst::ICMP_SGE
                                                            : CmpInst::ICMP_SLT;
      return new ICmpInst(NewPred, Trunc, Constant::getNullValue(NTy));
    }
  }

  return nullptr;
}

/// Fold icmp (or X, Y), C.
Instruction *InstCombiner::foldICmpOrConstant(ICmpInst &Cmp, BinaryOperator *Or,
                                              const APInt &C) {
  ICmpInst::Predicate Pred = Cmp.getPredicate();
  if (C.isOneValue()) {
    // icmp slt signum(V) 1 --> icmp slt V, 1
    Value *V = nullptr;
    if (Pred == ICmpInst::ICMP_SLT && match(Or, m_Signum(m_Value(V))))
      return new ICmpInst(ICmpInst::ICMP_SLT, V,
                          ConstantInt::get(V->getType(), 1));
  }

  // X | C == C --> X <=u C
  // X | C != C --> X  >u C
  //   iff C+1 is a power of 2 (C is a bitmask of the low bits)
  if (Cmp.isEquality() && Cmp.getOperand(1) == Or->getOperand(1) &&
      (C + 1).isPowerOf2()) {
    Pred = (Pred == CmpInst::ICMP_EQ) ? CmpInst::ICMP_ULE : CmpInst::ICMP_UGT;
    return new ICmpInst(Pred, Or->getOperand(0), Or->getOperand(1));
  }

  if (!Cmp.isEquality() || !C.isNullValue() || !Or->hasOneUse())
    return nullptr;

  Value *P, *Q;
  if (match(Or, m_Or(m_PtrToInt(m_Value(P)), m_PtrToInt(m_Value(Q))))) {
    // Simplify icmp eq (or (ptrtoint P), (ptrtoint Q)), 0
    // -> and (icmp eq P, null), (icmp eq Q, null).
    Value *CmpP =
        Builder.CreateICmp(Pred, P, ConstantInt::getNullValue(P->getType()));
    Value *CmpQ =
        Builder.CreateICmp(Pred, Q, ConstantInt::getNullValue(Q->getType()));
    auto BOpc = Pred == CmpInst::ICMP_EQ ? Instruction::And : Instruction::Or;
    return BinaryOperator::Create(BOpc, CmpP, CmpQ);
  }

  // Are we using xors to bitwise check for a pair of (in)equalities? Convert to
  // a shorter form that has more potential to be folded even further.
  Value *X1, *X2, *X3, *X4;
  if (match(Or->getOperand(0), m_OneUse(m_Xor(m_Value(X1), m_Value(X2)))) &&
      match(Or->getOperand(1), m_OneUse(m_Xor(m_Value(X3), m_Value(X4))))) {
    // ((X1 ^ X2) || (X3 ^ X4)) == 0 --> (X1 == X2) && (X3 == X4)
    // ((X1 ^ X2) || (X3 ^ X4)) != 0 --> (X1 != X2) || (X3 != X4)
    Value *Cmp12 = Builder.CreateICmp(Pred, X1, X2);
    Value *Cmp34 = Builder.CreateICmp(Pred, X3, X4);
    auto BOpc = Pred == CmpInst::ICMP_EQ ? Instruction::And : Instruction::Or;
    return BinaryOperator::Create(BOpc, Cmp12, Cmp34);
  }

  return nullptr;
}

/// Fold icmp (mul X, Y), C.
Instruction *InstCombiner::foldICmpMulConstant(ICmpInst &Cmp,
                                               BinaryOperator *Mul,
                                               const APInt &C) {
  const APInt *MulC;
  if (!match(Mul->getOperand(1), m_APInt(MulC)))
    return nullptr;

  // If this is a test of the sign bit and the multiply is sign-preserving with
  // a constant operand, use the multiply LHS operand instead.
  ICmpInst::Predicate Pred = Cmp.getPredicate();
  if (isSignTest(Pred, C) && Mul->hasNoSignedWrap()) {
    if (MulC->isNegative())
      Pred = ICmpInst::getSwappedPredicate(Pred);
    return new ICmpInst(Pred, Mul->getOperand(0),
                        Constant::getNullValue(Mul->getType()));
  }

  return nullptr;
}

/// Fold icmp (shl 1, Y), C.
static Instruction *foldICmpShlOne(ICmpInst &Cmp, Instruction *Shl,
                                   const APInt &C) {
  Value *Y;
  if (!match(Shl, m_Shl(m_One(), m_Value(Y))))
    return nullptr;

  Type *ShiftType = Shl->getType();
  unsigned TypeBits = C.getBitWidth();
  bool CIsPowerOf2 = C.isPowerOf2();
  ICmpInst::Predicate Pred = Cmp.getPredicate();
  if (Cmp.isUnsigned()) {
    // (1 << Y) pred C -> Y pred Log2(C)
    if (!CIsPowerOf2) {
      // (1 << Y) <  30 -> Y <= 4
      // (1 << Y) <= 30 -> Y <= 4
      // (1 << Y) >= 30 -> Y >  4
      // (1 << Y) >  30 -> Y >  4
      if (Pred == ICmpInst::ICMP_ULT)
        Pred = ICmpInst::ICMP_ULE;
      else if (Pred == ICmpInst::ICMP_UGE)
        Pred = ICmpInst::ICMP_UGT;
    }

    // (1 << Y) >= 2147483648 -> Y >= 31 -> Y == 31
    // (1 << Y) <  2147483648 -> Y <  31 -> Y != 31
    unsigned CLog2 = C.logBase2();
    if (CLog2 == TypeBits - 1) {
      if (Pred == ICmpInst::ICMP_UGE)
        Pred = ICmpInst::ICMP_EQ;
      else if (Pred == ICmpInst::ICMP_ULT)
        Pred = ICmpInst::ICMP_NE;
    }
    return new ICmpInst(Pred, Y, ConstantInt::get(ShiftType, CLog2));
  } else if (Cmp.isSigned()) {
    Constant *BitWidthMinusOne = ConstantInt::get(ShiftType, TypeBits - 1);
    if (C.isAllOnesValue()) {
      // (1 << Y) <= -1 -> Y == 31
      if (Pred == ICmpInst::ICMP_SLE)
        return new ICmpInst(ICmpInst::ICMP_EQ, Y, BitWidthMinusOne);

      // (1 << Y) >  -1 -> Y != 31
      if (Pred == ICmpInst::ICMP_SGT)
        return new ICmpInst(ICmpInst::ICMP_NE, Y, BitWidthMinusOne);
    } else if (!C) {
      // (1 << Y) <  0 -> Y == 31
      // (1 << Y) <= 0 -> Y == 31
      if (Pred == ICmpInst::ICMP_SLT || Pred == ICmpInst::ICMP_SLE)
        return new ICmpInst(ICmpInst::ICMP_EQ, Y, BitWidthMinusOne);

      // (1 << Y) >= 0 -> Y != 31
      // (1 << Y) >  0 -> Y != 31
      if (Pred == ICmpInst::ICMP_SGT || Pred == ICmpInst::ICMP_SGE)
        return new ICmpInst(ICmpInst::ICMP_NE, Y, BitWidthMinusOne);
    }
  } else if (Cmp.isEquality() && CIsPowerOf2) {
    return new ICmpInst(Pred, Y, ConstantInt::get(ShiftType, C.logBase2()));
  }

  return nullptr;
}

/// Fold icmp (shl X, Y), C.
Instruction *InstCombiner::foldICmpShlConstant(ICmpInst &Cmp,
                                               BinaryOperator *Shl,
                                               const APInt &C) {
  const APInt *ShiftVal;
  if (Cmp.isEquality() && match(Shl->getOperand(0), m_APInt(ShiftVal)))
    return foldICmpShlConstConst(Cmp, Shl->getOperand(1), C, *ShiftVal);

  const APInt *ShiftAmt;
  if (!match(Shl->getOperand(1), m_APInt(ShiftAmt)))
    return foldICmpShlOne(Cmp, Shl, C);

  // Check that the shift amount is in range. If not, don't perform undefined
  // shifts. When the shift is visited, it will be simplified.
  unsigned TypeBits = C.getBitWidth();
  if (ShiftAmt->uge(TypeBits))
    return nullptr;

  ICmpInst::Predicate Pred = Cmp.getPredicate();
  Value *X = Shl->getOperand(0);
  Type *ShType = Shl->getType();

  // NSW guarantees that we are only shifting out sign bits from the high bits,
  // so we can ASHR the compare constant without needing a mask and eliminate
  // the shift.
  if (Shl->hasNoSignedWrap()) {
    if (Pred == ICmpInst::ICMP_SGT) {
      // icmp Pred (shl nsw X, ShiftAmt), C --> icmp Pred X, (C >>s ShiftAmt)
      APInt ShiftedC = C.ashr(*ShiftAmt);
      return new ICmpInst(Pred, X, ConstantInt::get(ShType, ShiftedC));
    }
    if ((Pred == ICmpInst::ICMP_EQ || Pred == ICmpInst::ICMP_NE) &&
        C.ashr(*ShiftAmt).shl(*ShiftAmt) == C) {
      APInt ShiftedC = C.ashr(*ShiftAmt);
      return new ICmpInst(Pred, X, ConstantInt::get(ShType, ShiftedC));
    }
    if (Pred == ICmpInst::ICMP_SLT) {
      // SLE is the same as above, but SLE is canonicalized to SLT, so convert:
      // (X << S) <=s C is equiv to X <=s (C >> S) for all C
      // (X << S) <s (C + 1) is equiv to X <s (C >> S) + 1 if C <s SMAX
      // (X << S) <s C is equiv to X <s ((C - 1) >> S) + 1 if C >s SMIN
      assert(!C.isMinSignedValue() && "Unexpected icmp slt");
      APInt ShiftedC = (C - 1).ashr(*ShiftAmt) + 1;
      return new ICmpInst(Pred, X, ConstantInt::get(ShType, ShiftedC));
    }
    // If this is a signed comparison to 0 and the shift is sign preserving,
    // use the shift LHS operand instead; isSignTest may change 'Pred', so only
    // do that if we're sure to not continue on in this function.
    if (isSignTest(Pred, C))
      return new ICmpInst(Pred, X, Constant::getNullValue(ShType));
  }

  // NUW guarantees that we are only shifting out zero bits from the high bits,
  // so we can LSHR the compare constant without needing a mask and eliminate
  // the shift.
  if (Shl->hasNoUnsignedWrap()) {
    if (Pred == ICmpInst::ICMP_UGT) {
      // icmp Pred (shl nuw X, ShiftAmt), C --> icmp Pred X, (C >>u ShiftAmt)
      APInt ShiftedC = C.lshr(*ShiftAmt);
      return new ICmpInst(Pred, X, ConstantInt::get(ShType, ShiftedC));
    }
    if ((Pred == ICmpInst::ICMP_EQ || Pred == ICmpInst::ICMP_NE) &&
        C.lshr(*ShiftAmt).shl(*ShiftAmt) == C) {
      APInt ShiftedC = C.lshr(*ShiftAmt);
      return new ICmpInst(Pred, X, ConstantInt::get(ShType, ShiftedC));
    }
    if (Pred == ICmpInst::ICMP_ULT) {
      // ULE is the same as above, but ULE is canonicalized to ULT, so convert:
      // (X << S) <=u C is equiv to X <=u (C >> S) for all C
      // (X << S) <u (C + 1) is equiv to X <u (C >> S) + 1 if C <u ~0u
      // (X << S) <u C is equiv to X <u ((C - 1) >> S) + 1 if C >u 0
      assert(C.ugt(0) && "ult 0 should have been eliminated");
      APInt ShiftedC = (C - 1).lshr(*ShiftAmt) + 1;
      return new ICmpInst(Pred, X, ConstantInt::get(ShType, ShiftedC));
    }
  }

  if (Cmp.isEquality() && Shl->hasOneUse()) {
    // Strength-reduce the shift into an 'and'.
    Constant *Mask = ConstantInt::get(
        ShType,
        APInt::getLowBitsSet(TypeBits, TypeBits - ShiftAmt->getZExtValue()));
    Value *And = Builder.CreateAnd(X, Mask, Shl->getName() + ".mask");
    Constant *LShrC = ConstantInt::get(ShType, C.lshr(*ShiftAmt));
    return new ICmpInst(Pred, And, LShrC);
  }

  // Otherwise, if this is a comparison of the sign bit, simplify to and/test.
  bool TrueIfSigned = false;
  if (Shl->hasOneUse() && isSignBitCheck(Pred, C, TrueIfSigned)) {
    // (X << 31) <s 0  --> (X & 1) != 0
    Constant *Mask = ConstantInt::get(
        ShType,
        APInt::getOneBitSet(TypeBits, TypeBits - ShiftAmt->getZExtValue() - 1));
    Value *And = Builder.CreateAnd(X, Mask, Shl->getName() + ".mask");
    return new ICmpInst(TrueIfSigned ? ICmpInst::ICMP_NE : ICmpInst::ICMP_EQ,
                        And, Constant::getNullValue(ShType));
  }

  // Transform (icmp pred iM (shl iM %v, N), C)
  // -> (icmp pred i(M-N) (trunc %v iM to i(M-N)), (trunc (C>>N))
  // Transform the shl to a trunc if (trunc (C>>N)) has no loss and M-N.
  // This enables us to get rid of the shift in favor of a trunc that may be
  // free on the target. It has the additional benefit of comparing to a
  // smaller constant that may be more target-friendly.
  unsigned Amt = ShiftAmt->getLimitedValue(TypeBits - 1);
  if (Shl->hasOneUse() && Amt != 0 && C.countTrailingZeros() >= Amt &&
      DL.isLegalInteger(TypeBits - Amt)) {
    Type *TruncTy = IntegerType::get(Cmp.getContext(), TypeBits - Amt);
    if (ShType->isVectorTy())
      TruncTy = VectorType::get(TruncTy, ShType->getVectorNumElements());
    Constant *NewC =
        ConstantInt::get(TruncTy, C.ashr(*ShiftAmt).trunc(TypeBits - Amt));
    return new ICmpInst(Pred, Builder.CreateTrunc(X, TruncTy), NewC);
  }

  return nullptr;
}

/// Fold icmp ({al}shr X, Y), C.
Instruction *InstCombiner::foldICmpShrConstant(ICmpInst &Cmp,
                                               BinaryOperator *Shr,
                                               const APInt &C) {
  // An exact shr only shifts out zero bits, so:
  // icmp eq/ne (shr X, Y), 0 --> icmp eq/ne X, 0
  Value *X = Shr->getOperand(0);
  CmpInst::Predicate Pred = Cmp.getPredicate();
  if (Cmp.isEquality() && Shr->isExact() && Shr->hasOneUse() &&
      C.isNullValue())
    return new ICmpInst(Pred, X, Cmp.getOperand(1));

  const APInt *ShiftVal;
  if (Cmp.isEquality() && match(Shr->getOperand(0), m_APInt(ShiftVal)))
    return foldICmpShrConstConst(Cmp, Shr->getOperand(1), C, *ShiftVal);

  const APInt *ShiftAmt;
  if (!match(Shr->getOperand(1), m_APInt(ShiftAmt)))
    return nullptr;

  // Check that the shift amount is in range. If not, don't perform undefined
  // shifts. When the shift is visited it will be simplified.
  unsigned TypeBits = C.getBitWidth();
  unsigned ShAmtVal = ShiftAmt->getLimitedValue(TypeBits);
  if (ShAmtVal >= TypeBits || ShAmtVal == 0)
    return nullptr;

  bool IsAShr = Shr->getOpcode() == Instruction::AShr;
  bool IsExact = Shr->isExact();
  Type *ShrTy = Shr->getType();
  // TODO: If we could guarantee that InstSimplify would handle all of the
  // constant-value-based preconditions in the folds below, then we could assert
  // those conditions rather than checking them. This is difficult because of
  // undef/poison (PR34838).
  if (IsAShr) {
    if (Pred == CmpInst::ICMP_SLT || (Pred == CmpInst::ICMP_SGT && IsExact)) {
      // icmp slt (ashr X, ShAmtC), C --> icmp slt X, (C << ShAmtC)
      // icmp sgt (ashr exact X, ShAmtC), C --> icmp sgt X, (C << ShAmtC)
      APInt ShiftedC = C.shl(ShAmtVal);
      if (ShiftedC.ashr(ShAmtVal) == C)
        return new ICmpInst(Pred, X, ConstantInt::get(ShrTy, ShiftedC));
    }
    if (Pred == CmpInst::ICMP_SGT) {
      // icmp sgt (ashr X, ShAmtC), C --> icmp sgt X, ((C + 1) << ShAmtC) - 1
      APInt ShiftedC = (C + 1).shl(ShAmtVal) - 1;
      if (!C.isMaxSignedValue() && !(C + 1).shl(ShAmtVal).isMinSignedValue() &&
          (ShiftedC + 1).ashr(ShAmtVal) == (C + 1))
        return new ICmpInst(Pred, X, ConstantInt::get(ShrTy, ShiftedC));
    }
  } else {
    if (Pred == CmpInst::ICMP_ULT || (Pred == CmpInst::ICMP_UGT && IsExact)) {
      // icmp ult (lshr X, ShAmtC), C --> icmp ult X, (C << ShAmtC)
      // icmp ugt (lshr exact X, ShAmtC), C --> icmp ugt X, (C << ShAmtC)
      APInt ShiftedC = C.shl(ShAmtVal);
      if (ShiftedC.lshr(ShAmtVal) == C)
        return new ICmpInst(Pred, X, ConstantInt::get(ShrTy, ShiftedC));
    }
    if (Pred == CmpInst::ICMP_UGT) {
      // icmp ugt (lshr X, ShAmtC), C --> icmp ugt X, ((C + 1) << ShAmtC) - 1
      APInt ShiftedC = (C + 1).shl(ShAmtVal) - 1;
      if ((ShiftedC + 1).lshr(ShAmtVal) == (C + 1))
        return new ICmpInst(Pred, X, ConstantInt::get(ShrTy, ShiftedC));
    }
  }

  if (!Cmp.isEquality())
    return nullptr;

  // Handle equality comparisons of shift-by-constant.

  // If the comparison constant changes with the shift, the comparison cannot
  // succeed (bits of the comparison constant cannot match the shifted value).
  // This should be known by InstSimplify and already be folded to true/false.
  assert(((IsAShr && C.shl(ShAmtVal).ashr(ShAmtVal) == C) ||
          (!IsAShr && C.shl(ShAmtVal).lshr(ShAmtVal) == C)) &&
         "Expected icmp+shr simplify did not occur.");

  // If the bits shifted out are known zero, compare the unshifted value:
  //  (X & 4) >> 1 == 2  --> (X & 4) == 4.
  if (Shr->isExact())
    return new ICmpInst(Pred, X, ConstantInt::get(ShrTy, C << ShAmtVal));

  if (Shr->hasOneUse()) {
    // Canonicalize the shift into an 'and':
    // icmp eq/ne (shr X, ShAmt), C --> icmp eq/ne (and X, HiMask), (C << ShAmt)
    APInt Val(APInt::getHighBitsSet(TypeBits, TypeBits - ShAmtVal));
    Constant *Mask = ConstantInt::get(ShrTy, Val);
    Value *And = Builder.CreateAnd(X, Mask, Shr->getName() + ".mask");
    return new ICmpInst(Pred, And, ConstantInt::get(ShrTy, C << ShAmtVal));
  }

  return nullptr;
}

/// Fold icmp (udiv X, Y), C.
Instruction *InstCombiner::foldICmpUDivConstant(ICmpInst &Cmp,
                                                BinaryOperator *UDiv,
                                                const APInt &C) {
  const APInt *C2;
  if (!match(UDiv->getOperand(0), m_APInt(C2)))
    return nullptr;

  assert(*C2 != 0 && "udiv 0, X should have been simplified already.");

  // (icmp ugt (udiv C2, Y), C) -> (icmp ule Y, C2/(C+1))
  Value *Y = UDiv->getOperand(1);
  if (Cmp.getPredicate() == ICmpInst::ICMP_UGT) {
    assert(!C.isMaxValue() &&
           "icmp ugt X, UINT_MAX should have been simplified already.");
    return new ICmpInst(ICmpInst::ICMP_ULE, Y,
                        ConstantInt::get(Y->getType(), C2->udiv(C + 1)));
  }

  // (icmp ult (udiv C2, Y), C) -> (icmp ugt Y, C2/C)
  if (Cmp.getPredicate() == ICmpInst::ICMP_ULT) {
    assert(C != 0 && "icmp ult X, 0 should have been simplified already.");
    return new ICmpInst(ICmpInst::ICMP_UGT, Y,
                        ConstantInt::get(Y->getType(), C2->udiv(C)));
  }

  return nullptr;
}

/// Fold icmp ({su}div X, Y), C.
Instruction *InstCombiner::foldICmpDivConstant(ICmpInst &Cmp,
                                               BinaryOperator *Div,
                                               const APInt &C) {
  // Fold: icmp pred ([us]div X, C2), C -> range test
  // Fold this div into the comparison, producing a range check.
  // Determine, based on the divide type, what the range is being
  // checked.  If there is an overflow on the low or high side, remember
  // it, otherwise compute the range [low, hi) bounding the new value.
  // See: InsertRangeTest above for the kinds of replacements possible.
  const APInt *C2;
  if (!match(Div->getOperand(1), m_APInt(C2)))
    return nullptr;

  // FIXME: If the operand types don't match the type of the divide
  // then don't attempt this transform. The code below doesn't have the
  // logic to deal with a signed divide and an unsigned compare (and
  // vice versa). This is because (x /s C2) <s C  produces different
  // results than (x /s C2) <u C or (x /u C2) <s C or even
  // (x /u C2) <u C.  Simply casting the operands and result won't
  // work. :(  The if statement below tests that condition and bails
  // if it finds it.
  bool DivIsSigned = Div->getOpcode() == Instruction::SDiv;
  if (!Cmp.isEquality() && DivIsSigned != Cmp.isSigned())
    return nullptr;

  // The ProdOV computation fails on divide by 0 and divide by -1. Cases with
  // INT_MIN will also fail if the divisor is 1. Although folds of all these
  // division-by-constant cases should be present, we can not assert that they
  // have happened before we reach this icmp instruction.
  if (C2->isNullValue() || C2->isOneValue() ||
      (DivIsSigned && C2->isAllOnesValue()))
    return nullptr;

  // Compute Prod = C * C2. We are essentially solving an equation of
  // form X / C2 = C. We solve for X by multiplying C2 and C.
  // By solving for X, we can turn this into a range check instead of computing
  // a divide.
  APInt Prod = C * *C2;

  // Determine if the product overflows by seeing if the product is not equal to
  // the divide. Make sure we do the same kind of divide as in the LHS
  // instruction that we're folding.
  bool ProdOV = (DivIsSigned ? Prod.sdiv(*C2) : Prod.udiv(*C2)) != C;

  ICmpInst::Predicate Pred = Cmp.getPredicate();

  // If the division is known to be exact, then there is no remainder from the
  // divide, so the covered range size is unit, otherwise it is the divisor.
  APInt RangeSize = Div->isExact() ? APInt(C2->getBitWidth(), 1) : *C2;

  // Figure out the interval that is being checked.  For example, a comparison
  // like "X /u 5 == 0" is really checking that X is in the interval [0, 5).
  // Compute this interval based on the constants involved and the signedness of
  // the compare/divide.  This computes a half-open interval, keeping track of
  // whether either value in the interval overflows.  After analysis each
  // overflow variable is set to 0 if it's corresponding bound variable is valid
  // -1 if overflowed off the bottom end, or +1 if overflowed off the top end.
  int LoOverflow = 0, HiOverflow = 0;
  APInt LoBound, HiBound;

  if (!DivIsSigned) {  // udiv
    // e.g. X/5 op 3  --> [15, 20)
    LoBound = Prod;
    HiOverflow = LoOverflow = ProdOV;
    if (!HiOverflow) {
      // If this is not an exact divide, then many values in the range collapse
      // to the same result value.
      HiOverflow = addWithOverflow(HiBound, LoBound, RangeSize, false);
    }
  } else if (C2->isStrictlyPositive()) { // Divisor is > 0.
    if (C.isNullValue()) {       // (X / pos) op 0
      // Can't overflow.  e.g.  X/2 op 0 --> [-1, 2)
      LoBound = -(RangeSize - 1);
      HiBound = RangeSize;
    } else if (C.isStrictlyPositive()) {   // (X / pos) op pos
      LoBound = Prod;     // e.g.   X/5 op 3 --> [15, 20)
      HiOverflow = LoOverflow = ProdOV;
      if (!HiOverflow)
        HiOverflow = addWithOverflow(HiBound, Prod, RangeSize, true);
    } else {                       // (X / pos) op neg
      // e.g. X/5 op -3  --> [-15-4, -15+1) --> [-19, -14)
      HiBound = Prod + 1;
      LoOverflow = HiOverflow = ProdOV ? -1 : 0;
      if (!LoOverflow) {
        APInt DivNeg = -RangeSize;
        LoOverflow = addWithOverflow(LoBound, HiBound, DivNeg, true) ? -1 : 0;
      }
    }
  } else if (C2->isNegative()) { // Divisor is < 0.
    if (Div->isExact())
      RangeSize.negate();
    if (C.isNullValue()) { // (X / neg) op 0
      // e.g. X/-5 op 0  --> [-4, 5)
      LoBound = RangeSize + 1;
      HiBound = -RangeSize;
      if (HiBound == *C2) {        // -INTMIN = INTMIN
        HiOverflow = 1;            // [INTMIN+1, overflow)
        HiBound = APInt();         // e.g. X/INTMIN = 0 --> X > INTMIN
      }
    } else if (C.isStrictlyPositive()) {   // (X / neg) op pos
      // e.g. X/-5 op 3  --> [-19, -14)
      HiBound = Prod + 1;
      HiOverflow = LoOverflow = ProdOV ? -1 : 0;
      if (!LoOverflow)
        LoOverflow = addWithOverflow(LoBound, HiBound, RangeSize, true) ? -1:0;
    } else {                       // (X / neg) op neg
      LoBound = Prod;       // e.g. X/-5 op -3  --> [15, 20)
      LoOverflow = HiOverflow = ProdOV;
      if (!HiOverflow)
        HiOverflow = subWithOverflow(HiBound, Prod, RangeSize, true);
    }

    // Dividing by a negative swaps the condition.  LT <-> GT
    Pred = ICmpInst::getSwappedPredicate(Pred);
  }

  Value *X = Div->getOperand(0);
  switch (Pred) {
    default: llvm_unreachable("Unhandled icmp opcode!");
    case ICmpInst::ICMP_EQ:
      if (LoOverflow && HiOverflow)
        return replaceInstUsesWith(Cmp, Builder.getFalse());
      if (HiOverflow)
        return new ICmpInst(DivIsSigned ? ICmpInst::ICMP_SGE :
                            ICmpInst::ICMP_UGE, X,
                            ConstantInt::get(Div->getType(), LoBound));
      if (LoOverflow)
        return new ICmpInst(DivIsSigned ? ICmpInst::ICMP_SLT :
                            ICmpInst::ICMP_ULT, X,
                            ConstantInt::get(Div->getType(), HiBound));
      return replaceInstUsesWith(
          Cmp, insertRangeTest(X, LoBound, HiBound, DivIsSigned, true));
    case ICmpInst::ICMP_NE:
      if (LoOverflow && HiOverflow)
        return replaceInstUsesWith(Cmp, Builder.getTrue());
      if (HiOverflow)
        return new ICmpInst(DivIsSigned ? ICmpInst::ICMP_SLT :
                            ICmpInst::ICMP_ULT, X,
                            ConstantInt::get(Div->getType(), LoBound));
      if (LoOverflow)
        return new ICmpInst(DivIsSigned ? ICmpInst::ICMP_SGE :
                            ICmpInst::ICMP_UGE, X,
                            ConstantInt::get(Div->getType(), HiBound));
      return replaceInstUsesWith(Cmp,
                                 insertRangeTest(X, LoBound, HiBound,
                                                 DivIsSigned, false));
    case ICmpInst::ICMP_ULT:
    case ICmpInst::ICMP_SLT:
      if (LoOverflow == +1)   // Low bound is greater than input range.
        return replaceInstUsesWith(Cmp, Builder.getTrue());
      if (LoOverflow == -1)   // Low bound is less than input range.
        return replaceInstUsesWith(Cmp, Builder.getFalse());
      return new ICmpInst(Pred, X, ConstantInt::get(Div->getType(), LoBound));
    case ICmpInst::ICMP_UGT:
    case ICmpInst::ICMP_SGT:
      if (HiOverflow == +1)       // High bound greater than input range.
        return replaceInstUsesWith(Cmp, Builder.getFalse());
      if (HiOverflow == -1)       // High bound less than input range.
        return replaceInstUsesWith(Cmp, Builder.getTrue());
      if (Pred == ICmpInst::ICMP_UGT)
        return new ICmpInst(ICmpInst::ICMP_UGE, X,
                            ConstantInt::get(Div->getType(), HiBound));
      return new ICmpInst(ICmpInst::ICMP_SGE, X,
                          ConstantInt::get(Div->getType(), HiBound));
  }

  return nullptr;
}

/// Fold icmp (sub X, Y), C.
Instruction *InstCombiner::foldICmpSubConstant(ICmpInst &Cmp,
                                               BinaryOperator *Sub,
                                               const APInt &C) {
  Value *X = Sub->getOperand(0), *Y = Sub->getOperand(1);
  ICmpInst::Predicate Pred = Cmp.getPredicate();

  // The following transforms are only worth it if the only user of the subtract
  // is the icmp.
  if (!Sub->hasOneUse())
    return nullptr;

  if (Sub->hasNoSignedWrap()) {
    // (icmp sgt (sub nsw X, Y), -1) -> (icmp sge X, Y)
    if (Pred == ICmpInst::ICMP_SGT && C.isAllOnesValue())
      return new ICmpInst(ICmpInst::ICMP_SGE, X, Y);

    // (icmp sgt (sub nsw X, Y), 0) -> (icmp sgt X, Y)
    if (Pred == ICmpInst::ICMP_SGT && C.isNullValue())
      return new ICmpInst(ICmpInst::ICMP_SGT, X, Y);

    // (icmp slt (sub nsw X, Y), 0) -> (icmp slt X, Y)
    if (Pred == ICmpInst::ICMP_SLT && C.isNullValue())
      return new ICmpInst(ICmpInst::ICMP_SLT, X, Y);

    // (icmp slt (sub nsw X, Y), 1) -> (icmp sle X, Y)
    if (Pred == ICmpInst::ICMP_SLT && C.isOneValue())
      return new ICmpInst(ICmpInst::ICMP_SLE, X, Y);
  }

  const APInt *C2;
  if (!match(X, m_APInt(C2)))
    return nullptr;

  // C2 - Y <u C -> (Y | (C - 1)) == C2
  //   iff (C2 & (C - 1)) == C - 1 and C is a power of 2
  if (Pred == ICmpInst::ICMP_ULT && C.isPowerOf2() &&
      (*C2 & (C - 1)) == (C - 1))
    return new ICmpInst(ICmpInst::ICMP_EQ, Builder.CreateOr(Y, C - 1), X);

  // C2 - Y >u C -> (Y | C) != C2
  //   iff C2 & C == C and C + 1 is a power of 2
  if (Pred == ICmpInst::ICMP_UGT && (C + 1).isPowerOf2() && (*C2 & C) == C)
    return new ICmpInst(ICmpInst::ICMP_NE, Builder.CreateOr(Y, C), X);

  return nullptr;
}

/// Fold icmp (add X, Y), C.
Instruction *InstCombiner::foldICmpAddConstant(ICmpInst &Cmp,
                                               BinaryOperator *Add,
                                               const APInt &C) {
  Value *Y = Add->getOperand(1);
  const APInt *C2;
  if (Cmp.isEquality() || !match(Y, m_APInt(C2)))
    return nullptr;

  // Fold icmp pred (add X, C2), C.
  Value *X = Add->getOperand(0);
  Type *Ty = Add->getType();
  CmpInst::Predicate Pred = Cmp.getPredicate();

  if (!Add->hasOneUse())
    return nullptr;

  // If the add does not wrap, we can always adjust the compare by subtracting
  // the constants. Equality comparisons are handled elsewhere. SGE/SLE/UGE/ULE
  // are canonicalized to SGT/SLT/UGT/ULT.
  if ((Add->hasNoSignedWrap() &&
       (Pred == ICmpInst::ICMP_SGT || Pred == ICmpInst::ICMP_SLT)) ||
      (Add->hasNoUnsignedWrap() &&
       (Pred == ICmpInst::ICMP_UGT || Pred == ICmpInst::ICMP_ULT))) {
    bool Overflow;
    APInt NewC =
        Cmp.isSigned() ? C.ssub_ov(*C2, Overflow) : C.usub_ov(*C2, Overflow);
    // If there is overflow, the result must be true or false.
    // TODO: Can we assert there is no overflow because InstSimplify always
    // handles those cases?
    if (!Overflow)
      // icmp Pred (add nsw X, C2), C --> icmp Pred X, (C - C2)
      return new ICmpInst(Pred, X, ConstantInt::get(Ty, NewC));
  }

  auto CR = ConstantRange::makeExactICmpRegion(Pred, C).subtract(*C2);
  const APInt &Upper = CR.getUpper();
  const APInt &Lower = CR.getLower();
  if (Cmp.isSigned()) {
    if (Lower.isSignMask())
      return new ICmpInst(ICmpInst::ICMP_SLT, X, ConstantInt::get(Ty, Upper));
    if (Upper.isSignMask())
      return new ICmpInst(ICmpInst::ICMP_SGE, X, ConstantInt::get(Ty, Lower));
  } else {
    if (Lower.isMinValue())
      return new ICmpInst(ICmpInst::ICMP_ULT, X, ConstantInt::get(Ty, Upper));
    if (Upper.isMinValue())
      return new ICmpInst(ICmpInst::ICMP_UGE, X, ConstantInt::get(Ty, Lower));
  }

  // X+C <u C2 -> (X & -C2) == C
  //   iff C & (C2-1) == 0
  //       C2 is a power of 2
  if (Pred == ICmpInst::ICMP_ULT && C.isPowerOf2() && (*C2 & (C - 1)) == 0)
    return new ICmpInst(ICmpInst::ICMP_EQ, Builder.CreateAnd(X, -C),
                        ConstantExpr::getNeg(cast<Constant>(Y)));

  // X+C >u C2 -> (X & ~C2) != C
  //   iff C & C2 == 0
  //       C2+1 is a power of 2
  if (Pred == ICmpInst::ICMP_UGT && (C + 1).isPowerOf2() && (*C2 & C) == 0)
    return new ICmpInst(ICmpInst::ICMP_NE, Builder.CreateAnd(X, ~C),
                        ConstantExpr::getNeg(cast<Constant>(Y)));

  return nullptr;
}

bool InstCombiner::matchThreeWayIntCompare(SelectInst *SI, Value *&LHS,
                                           Value *&RHS, ConstantInt *&Less,
                                           ConstantInt *&Equal,
                                           ConstantInt *&Greater) {
  // TODO: Generalize this to work with other comparison idioms or ensure
  // they get canonicalized into this form.

  // select i1 (a == b), i32 Equal, i32 (select i1 (a < b), i32 Less, i32
  // Greater), where Equal, Less and Greater are placeholders for any three
  // constants.
  ICmpInst::Predicate PredA, PredB;
  if (match(SI->getTrueValue(), m_ConstantInt(Equal)) &&
      match(SI->getCondition(), m_ICmp(PredA, m_Value(LHS), m_Value(RHS))) &&
      PredA == ICmpInst::ICMP_EQ &&
      match(SI->getFalseValue(),
            m_Select(m_ICmp(PredB, m_Specific(LHS), m_Specific(RHS)),
                     m_ConstantInt(Less), m_ConstantInt(Greater))) &&
      PredB == ICmpInst::ICMP_SLT) {
    return true;
  }
  return false;
}

Instruction *InstCombiner::foldICmpSelectConstant(ICmpInst &Cmp,
                                                  SelectInst *Select,
                                                  ConstantInt *C) {

  assert(C && "Cmp RHS should be a constant int!");
  // If we're testing a constant value against the result of a three way
  // comparison, the result can be expressed directly in terms of the
  // original values being compared.  Note: We could possibly be more
  // aggressive here and remove the hasOneUse test. The original select is
  // really likely to simplify or sink when we remove a test of the result.
  Value *OrigLHS, *OrigRHS;
  ConstantInt *C1LessThan, *C2Equal, *C3GreaterThan;
  if (Cmp.hasOneUse() &&
      matchThreeWayIntCompare(Select, OrigLHS, OrigRHS, C1LessThan, C2Equal,
                              C3GreaterThan)) {
    assert(C1LessThan && C2Equal && C3GreaterThan);

    bool TrueWhenLessThan =
        ConstantExpr::getCompare(Cmp.getPredicate(), C1LessThan, C)
            ->isAllOnesValue();
    bool TrueWhenEqual =
        ConstantExpr::getCompare(Cmp.getPredicate(), C2Equal, C)
            ->isAllOnesValue();
    bool TrueWhenGreaterThan =
        ConstantExpr::getCompare(Cmp.getPredicate(), C3GreaterThan, C)
            ->isAllOnesValue();

    // This generates the new instruction that will replace the original Cmp
    // Instruction. Instead of enumerating the various combinations when
    // TrueWhenLessThan, TrueWhenEqual and TrueWhenGreaterThan are true versus
    // false, we rely on chaining of ORs and future passes of InstCombine to
    // simplify the OR further (i.e. a s< b || a == b becomes a s<= b).

    // When none of the three constants satisfy the predicate for the RHS (C),
    // the entire original Cmp can be simplified to a false.
    Value *Cond = Builder.getFalse();
    if (TrueWhenLessThan)
      Cond = Builder.CreateOr(Cond, Builder.CreateICmp(ICmpInst::ICMP_SLT, OrigLHS, OrigRHS));
    if (TrueWhenEqual)
      Cond = Builder.CreateOr(Cond, Builder.CreateICmp(ICmpInst::ICMP_EQ, OrigLHS, OrigRHS));
    if (TrueWhenGreaterThan)
      Cond = Builder.CreateOr(Cond, Builder.CreateICmp(ICmpInst::ICMP_SGT, OrigLHS, OrigRHS));

    return replaceInstUsesWith(Cmp, Cond);
  }
  return nullptr;
}

Instruction *InstCombiner::foldICmpBitCastConstant(ICmpInst &Cmp,
                                                   BitCastInst *Bitcast,
                                                   const APInt &C) {
  // Folding: icmp <pred> iN X, C
  //  where X = bitcast <M x iK> (shufflevector <M x iK> %vec, undef, SC)) to iN
  //    and C is a splat of a K-bit pattern
  //    and SC is a constant vector = <C', C', C', ..., C'>
  // Into:
  //   %E = extractelement <M x iK> %vec, i32 C'
  //   icmp <pred> iK %E, trunc(C)
  if (!Bitcast->getType()->isIntegerTy() ||
      !Bitcast->getSrcTy()->isIntOrIntVectorTy())
    return nullptr;

  Value *BCIOp = Bitcast->getOperand(0);
  Value *Vec = nullptr;     // 1st vector arg of the shufflevector
  Constant *Mask = nullptr; // Mask arg of the shufflevector
  if (match(BCIOp,
            m_ShuffleVector(m_Value(Vec), m_Undef(), m_Constant(Mask)))) {
    // Check whether every element of Mask is the same constant
    if (auto *Elem = dyn_cast_or_null<ConstantInt>(Mask->getSplatValue())) {
      auto *VecTy = cast<VectorType>(BCIOp->getType());
      auto *EltTy = cast<IntegerType>(VecTy->getElementType());
      auto Pred = Cmp.getPredicate();
      if (C.isSplat(EltTy->getBitWidth())) {
        // Fold the icmp based on the value of C
        // If C is M copies of an iK sized bit pattern,
        // then:
        //   =>  %E = extractelement <N x iK> %vec, i32 Elem
        //       icmp <pred> iK %SplatVal, <pattern>
        Value *Extract = Builder.CreateExtractElement(Vec, Elem);
        Value *NewC = ConstantInt::get(EltTy, C.trunc(EltTy->getBitWidth()));
        return new ICmpInst(Pred, Extract, NewC);
      }
    }
  }
  return nullptr;
}

/// Try to fold integer comparisons with a constant operand: icmp Pred X, C
/// where X is some kind of instruction.
Instruction *InstCombiner::foldICmpInstWithConstant(ICmpInst &Cmp) {
  const APInt *C;
  if (!match(Cmp.getOperand(1), m_APInt(C)))
    return nullptr;

  if (auto *BO = dyn_cast<BinaryOperator>(Cmp.getOperand(0))) {
    switch (BO->getOpcode()) {
    case Instruction::Xor:
      if (Instruction *I = foldICmpXorConstant(Cmp, BO, *C))
        return I;
      break;
    case Instruction::And:
      if (Instruction *I = foldICmpAndConstant(Cmp, BO, *C))
        return I;
      break;
    case Instruction::Or:
      if (Instruction *I = foldICmpOrConstant(Cmp, BO, *C))
        return I;
      break;
    case Instruction::Mul:
      if (Instruction *I = foldICmpMulConstant(Cmp, BO, *C))
        return I;
      break;
    case Instruction::Shl:
      if (Instruction *I = foldICmpShlConstant(Cmp, BO, *C))
        return I;
      break;
    case Instruction::LShr:
    case Instruction::AShr:
      if (Instruction *I = foldICmpShrConstant(Cmp, BO, *C))
        return I;
      break;
    case Instruction::UDiv:
      if (Instruction *I = foldICmpUDivConstant(Cmp, BO, *C))
        return I;
      LLVM_FALLTHROUGH;
    case Instruction::SDiv:
      if (Instruction *I = foldICmpDivConstant(Cmp, BO, *C))
        return I;
      break;
    case Instruction::Sub:
      if (Instruction *I = foldICmpSubConstant(Cmp, BO, *C))
        return I;
      break;
    case Instruction::Add:
      if (Instruction *I = foldICmpAddConstant(Cmp, BO, *C))
        return I;
      break;
    default:
      break;
    }
    // TODO: These folds could be refactored to be part of the above calls.
    if (Instruction *I = foldICmpBinOpEqualityWithConstant(Cmp, BO, *C))
      return I;
  }

  // Match against CmpInst LHS being instructions other than binary operators.

  if (auto *SI = dyn_cast<SelectInst>(Cmp.getOperand(0))) {
    // For now, we only support constant integers while folding the
    // ICMP(SELECT)) pattern. We can extend this to support vector of integers
    // similar to the cases handled by binary ops above.
    if (ConstantInt *ConstRHS = dyn_cast<ConstantInt>(Cmp.getOperand(1)))
      if (Instruction *I = foldICmpSelectConstant(Cmp, SI, ConstRHS))
        return I;
  }

  if (auto *TI = dyn_cast<TruncInst>(Cmp.getOperand(0))) {
    if (Instruction *I = foldICmpTruncConstant(Cmp, TI, *C))
      return I;
  }

  if (auto *BCI = dyn_cast<BitCastInst>(Cmp.getOperand(0))) {
    if (Instruction *I = foldICmpBitCastConstant(Cmp, BCI, *C))
      return I;
  }

  if (Instruction *I = foldICmpIntrinsicWithConstant(Cmp, *C))
    return I;

  return nullptr;
}

/// Fold an icmp equality instruction with binary operator LHS and constant RHS:
/// icmp eq/ne BO, C.
Instruction *InstCombiner::foldICmpBinOpEqualityWithConstant(ICmpInst &Cmp,
                                                             BinaryOperator *BO,
                                                             const APInt &C) {
  // TODO: Some of these folds could work with arbitrary constants, but this
  // function is limited to scalar and vector splat constants.
  if (!Cmp.isEquality())
    return nullptr;

  ICmpInst::Predicate Pred = Cmp.getPredicate();
  bool isICMP_NE = Pred == ICmpInst::ICMP_NE;
  Constant *RHS = cast<Constant>(Cmp.getOperand(1));
  Value *BOp0 = BO->getOperand(0), *BOp1 = BO->getOperand(1);

  switch (BO->getOpcode()) {
  case Instruction::SRem:
    // If we have a signed (X % (2^c)) == 0, turn it into an unsigned one.
    if (C.isNullValue() && BO->hasOneUse()) {
      const APInt *BOC;
      if (match(BOp1, m_APInt(BOC)) && BOC->sgt(1) && BOC->isPowerOf2()) {
        Value *NewRem = Builder.CreateURem(BOp0, BOp1, BO->getName());
        return new ICmpInst(Pred, NewRem,
                            Constant::getNullValue(BO->getType()));
      }
    }
    break;
  case Instruction::Add: {
    // Replace ((add A, B) != C) with (A != C-B) if B & C are constants.
    const APInt *BOC;
    if (match(BOp1, m_APInt(BOC))) {
      if (BO->hasOneUse()) {
        Constant *SubC = ConstantExpr::getSub(RHS, cast<Constant>(BOp1));
        return new ICmpInst(Pred, BOp0, SubC);
      }
    } else if (C.isNullValue()) {
      // Replace ((add A, B) != 0) with (A != -B) if A or B is
      // efficiently invertible, or if the add has just this one use.
      if (Value *NegVal = dyn_castNegVal(BOp1))
        return new ICmpInst(Pred, BOp0, NegVal);
      if (Value *NegVal = dyn_castNegVal(BOp0))
        return new ICmpInst(Pred, NegVal, BOp1);
      if (BO->hasOneUse()) {
        Value *Neg = Builder.CreateNeg(BOp1);
        Neg->takeName(BO);
        return new ICmpInst(Pred, BOp0, Neg);
      }
    }
    break;
  }
  case Instruction::Xor:
    if (BO->hasOneUse()) {
      if (Constant *BOC = dyn_cast<Constant>(BOp1)) {
        // For the xor case, we can xor two constants together, eliminating
        // the explicit xor.
        return new ICmpInst(Pred, BOp0, ConstantExpr::getXor(RHS, BOC));
      } else if (C.isNullValue()) {
        // Replace ((xor A, B) != 0) with (A != B)
        return new ICmpInst(Pred, BOp0, BOp1);
      }
    }
    break;
  case Instruction::Sub:
    if (BO->hasOneUse()) {
      const APInt *BOC;
      if (match(BOp0, m_APInt(BOC))) {
        // Replace ((sub BOC, B) != C) with (B != BOC-C).
        Constant *SubC = ConstantExpr::getSub(cast<Constant>(BOp0), RHS);
        return new ICmpInst(Pred, BOp1, SubC);
      } else if (C.isNullValue()) {
        // Replace ((sub A, B) != 0) with (A != B).
        return new ICmpInst(Pred, BOp0, BOp1);
      }
    }
    break;
  case Instruction::Or: {
    const APInt *BOC;
    if (match(BOp1, m_APInt(BOC)) && BO->hasOneUse() && RHS->isAllOnesValue()) {
      // Comparing if all bits outside of a constant mask are set?
      // Replace (X | C) == -1 with (X & ~C) == ~C.
      // This removes the -1 constant.
      Constant *NotBOC = ConstantExpr::getNot(cast<Constant>(BOp1));
      Value *And = Builder.CreateAnd(BOp0, NotBOC);
      return new ICmpInst(Pred, And, NotBOC);
    }
    break;
  }
  case Instruction::And: {
    const APInt *BOC;
    if (match(BOp1, m_APInt(BOC))) {
      // If we have ((X & C) == C), turn it into ((X & C) != 0).
      if (C == *BOC && C.isPowerOf2())
        return new ICmpInst(isICMP_NE ? ICmpInst::ICMP_EQ : ICmpInst::ICMP_NE,
                            BO, Constant::getNullValue(RHS->getType()));

      // Don't perform the following transforms if the AND has multiple uses
      if (!BO->hasOneUse())
        break;

      // Replace (and X, (1 << size(X)-1) != 0) with x s< 0
      if (BOC->isSignMask()) {
        Constant *Zero = Constant::getNullValue(BOp0->getType());
        auto NewPred = isICMP_NE ? ICmpInst::ICMP_SLT : ICmpInst::ICMP_SGE;
        return new ICmpInst(NewPred, BOp0, Zero);
      }

      // ((X & ~7) == 0) --> X < 8
      if (C.isNullValue() && (~(*BOC) + 1).isPowerOf2()) {
        Constant *NegBOC = ConstantExpr::getNeg(cast<Constant>(BOp1));
        auto NewPred = isICMP_NE ? ICmpInst::ICMP_UGE : ICmpInst::ICMP_ULT;
        return new ICmpInst(NewPred, BOp0, NegBOC);
      }
    }
    break;
  }
  case Instruction::Mul:
    if (C.isNullValue() && BO->hasNoSignedWrap()) {
      const APInt *BOC;
      if (match(BOp1, m_APInt(BOC)) && !BOC->isNullValue()) {
        // The trivial case (mul X, 0) is handled by InstSimplify.
        // General case : (mul X, C) != 0 iff X != 0
        //                (mul X, C) == 0 iff X == 0
        return new ICmpInst(Pred, BOp0, Constant::getNullValue(RHS->getType()));
      }
    }
    break;
  case Instruction::UDiv:
    if (C.isNullValue()) {
      // (icmp eq/ne (udiv A, B), 0) -> (icmp ugt/ule i32 B, A)
      auto NewPred = isICMP_NE ? ICmpInst::ICMP_ULE : ICmpInst::ICMP_UGT;
      return new ICmpInst(NewPred, BOp1, BOp0);
    }
    break;
  default:
    break;
  }
  return nullptr;
}

/// Fold an icmp with LLVM intrinsic and constant operand: icmp Pred II, C.
Instruction *InstCombiner::foldICmpIntrinsicWithConstant(ICmpInst &Cmp,
                                                         const APInt &C) {
  IntrinsicInst *II = dyn_cast<IntrinsicInst>(Cmp.getOperand(0));
  if (!II || !Cmp.isEquality())
    return nullptr;

  // Handle icmp {eq|ne} <intrinsic>, Constant.
  Type *Ty = II->getType();
  unsigned BitWidth = C.getBitWidth();
  switch (II->getIntrinsicID()) {
  case Intrinsic::bswap:
    Worklist.Add(II);
    Cmp.setOperand(0, II->getArgOperand(0));
    Cmp.setOperand(1, ConstantInt::get(Ty, C.byteSwap()));
    return &Cmp;

  case Intrinsic::ctlz:
  case Intrinsic::cttz: {
    // ctz(A) == bitwidth(A)  ->  A == 0 and likewise for !=
    if (C == BitWidth) {
      Worklist.Add(II);
      Cmp.setOperand(0, II->getArgOperand(0));
      Cmp.setOperand(1, ConstantInt::getNullValue(Ty));
      return &Cmp;
    }

    // ctz(A) == C -> A & Mask1 == Mask2, where Mask2 only has bit C set
    // and Mask1 has bits 0..C+1 set. Similar for ctl, but for high bits.
    // Limit to one use to ensure we don't increase instruction count.
    unsigned Num = C.getLimitedValue(BitWidth);
    if (Num != BitWidth && II->hasOneUse()) {
      bool IsTrailing = II->getIntrinsicID() == Intrinsic::cttz;
      APInt Mask1 = IsTrailing ? APInt::getLowBitsSet(BitWidth, Num + 1)
                               : APInt::getHighBitsSet(BitWidth, Num + 1);
      APInt Mask2 = IsTrailing
        ? APInt::getOneBitSet(BitWidth, Num)
        : APInt::getOneBitSet(BitWidth, BitWidth - Num - 1);
      Cmp.setOperand(0, Builder.CreateAnd(II->getArgOperand(0), Mask1));
      Cmp.setOperand(1, ConstantInt::get(Ty, Mask2));
      Worklist.Add(II);
      return &Cmp;
    }
    break;
  }

  case Intrinsic::ctpop: {
    // popcount(A) == 0  ->  A == 0 and likewise for !=
    // popcount(A) == bitwidth(A)  ->  A == -1 and likewise for !=
    bool IsZero = C.isNullValue();
    if (IsZero || C == BitWidth) {
      Worklist.Add(II);
      Cmp.setOperand(0, II->getArgOperand(0));
      auto *NewOp =
          IsZero ? Constant::getNullValue(Ty) : Constant::getAllOnesValue(Ty);
      Cmp.setOperand(1, NewOp);
      return &Cmp;
    }
    break;
  }
  default:
    break;
  }

  return nullptr;
}

/// Handle icmp with constant (but not simple integer constant) RHS.
Instruction *InstCombiner::foldICmpInstWithConstantNotInt(ICmpInst &I) {
  Value *Op0 = I.getOperand(0), *Op1 = I.getOperand(1);
  Constant *RHSC = dyn_cast<Constant>(Op1);
  Instruction *LHSI = dyn_cast<Instruction>(Op0);
  if (!RHSC || !LHSI)
    return nullptr;

  switch (LHSI->getOpcode()) {
  case Instruction::GetElementPtr:
    // icmp pred GEP (P, int 0, int 0, int 0), null -> icmp pred P, null
    if (RHSC->isNullValue() &&
        cast<GetElementPtrInst>(LHSI)->hasAllZeroIndices())
      return new ICmpInst(
          I.getPredicate(), LHSI->getOperand(0),
          Constant::getNullValue(LHSI->getOperand(0)->getType()));
    break;
  case Instruction::PHI:
    // Only fold icmp into the PHI if the phi and icmp are in the same
    // block.  If in the same block, we're encouraging jump threading.  If
    // not, we are just pessimizing the code by making an i1 phi.
    if (LHSI->getParent() == I.getParent())
      if (Instruction *NV = foldOpIntoPhi(I, cast<PHINode>(LHSI)))
        return NV;
    break;
  case Instruction::Select: {
    // If either operand of the select is a constant, we can fold the
    // comparison into the select arms, which will cause one to be
    // constant folded and the select turned into a bitwise or.
    Value *Op1 = nullptr, *Op2 = nullptr;
    ConstantInt *CI = nullptr;
    if (Constant *C = dyn_cast<Constant>(LHSI->getOperand(1))) {
      Op1 = ConstantExpr::getICmp(I.getPredicate(), C, RHSC);
      CI = dyn_cast<ConstantInt>(Op1);
    }
    if (Constant *C = dyn_cast<Constant>(LHSI->getOperand(2))) {
      Op2 = ConstantExpr::getICmp(I.getPredicate(), C, RHSC);
      CI = dyn_cast<ConstantInt>(Op2);
    }

    // We only want to perform this transformation if it will not lead to
    // additional code. This is true if either both sides of the select
    // fold to a constant (in which case the icmp is replaced with a select
    // which will usually simplify) or this is the only user of the
    // select (in which case we are trading a select+icmp for a simpler
    // select+icmp) or all uses of the select can be replaced based on
    // dominance information ("Global cases").
    bool Transform = false;
    if (Op1 && Op2)
      Transform = true;
    else if (Op1 || Op2) {
      // Local case
      if (LHSI->hasOneUse())
        Transform = true;
      // Global cases
      else if (CI && !CI->isZero())
        // When Op1 is constant try replacing select with second operand.
        // Otherwise Op2 is constant and try replacing select with first
        // operand.
        Transform =
            replacedSelectWithOperand(cast<SelectInst>(LHSI), &I, Op1 ? 2 : 1);
    }
    if (Transform) {
      if (!Op1)
        Op1 = Builder.CreateICmp(I.getPredicate(), LHSI->getOperand(1), RHSC,
                                 I.getName());
      if (!Op2)
        Op2 = Builder.CreateICmp(I.getPredicate(), LHSI->getOperand(2), RHSC,
                                 I.getName());
      return SelectInst::Create(LHSI->getOperand(0), Op1, Op2);
    }
    break;
  }
  case Instruction::IntToPtr:
    // icmp pred inttoptr(X), null -> icmp pred X, 0
    if (RHSC->isNullValue() &&
        DL.getIntPtrType(RHSC->getType()) == LHSI->getOperand(0)->getType())
      return new ICmpInst(
          I.getPredicate(), LHSI->getOperand(0),
          Constant::getNullValue(LHSI->getOperand(0)->getType()));
    break;

  case Instruction::Load:
    // Try to optimize things like "A[i] > 4" to index computations.
    if (GetElementPtrInst *GEP =
            dyn_cast<GetElementPtrInst>(LHSI->getOperand(0))) {
      if (GlobalVariable *GV = dyn_cast<GlobalVariable>(GEP->getOperand(0)))
        if (GV->isConstant() && GV->hasDefinitiveInitializer() &&
            !cast<LoadInst>(LHSI)->isVolatile())
          if (Instruction *Res = foldCmpLoadFromIndexedGlobal(GEP, GV, I))
            return Res;
    }
    break;
  }

  return nullptr;
}

/// Some comparisons can be simplified.
/// In this case, we are looking for comparisons that look like
/// a check for a lossy truncation.
/// Folds:
///   icmp SrcPred (x & Mask), x    to    icmp DstPred x, Mask
/// Where Mask is some pattern that produces all-ones in low bits:
///    (-1 >> y)
///    ((-1 << y) >> y)     <- non-canonical, has extra uses
///   ~(-1 << y)
///    ((1 << y) + (-1))    <- non-canonical, has extra uses
/// The Mask can be a constant, too.
/// For some predicates, the operands are commutative.
/// For others, x can only be on a specific side.
static Value *foldICmpWithLowBitMaskedVal(ICmpInst &I,
                                          InstCombiner::BuilderTy &Builder) {
  ICmpInst::Predicate SrcPred;
  Value *X, *M, *Y;
  auto m_VariableMask = m_CombineOr(
      m_CombineOr(m_Not(m_Shl(m_AllOnes(), m_Value())),
                  m_Add(m_Shl(m_One(), m_Value()), m_AllOnes())),
      m_CombineOr(m_LShr(m_AllOnes(), m_Value()),
                  m_LShr(m_Shl(m_AllOnes(), m_Value(Y)), m_Deferred(Y))));
  auto m_Mask = m_CombineOr(m_VariableMask, m_LowBitMask());
  if (!match(&I, m_c_ICmp(SrcPred,
                          m_c_And(m_CombineAnd(m_Mask, m_Value(M)), m_Value(X)),
                          m_Deferred(X))))
    return nullptr;

  ICmpInst::Predicate DstPred;
  switch (SrcPred) {
  case ICmpInst::Predicate::ICMP_EQ:
    //  x & (-1 >> y) == x    ->    x u<= (-1 >> y)
    DstPred = ICmpInst::Predicate::ICMP_ULE;
    break;
  case ICmpInst::Predicate::ICMP_NE:
    //  x & (-1 >> y) != x    ->    x u> (-1 >> y)
    DstPred = ICmpInst::Predicate::ICMP_UGT;
    break;
  case ICmpInst::Predicate::ICMP_UGT:
    //  x u> x & (-1 >> y)    ->    x u> (-1 >> y)
    assert(X == I.getOperand(0) && "instsimplify took care of commut. variant");
    DstPred = ICmpInst::Predicate::ICMP_UGT;
    break;
  case ICmpInst::Predicate::ICMP_UGE:
    //  x & (-1 >> y) u>= x    ->    x u<= (-1 >> y)
    assert(X == I.getOperand(1) && "instsimplify took care of commut. variant");
    DstPred = ICmpInst::Predicate::ICMP_ULE;
    break;
  case ICmpInst::Predicate::ICMP_ULT:
    //  x & (-1 >> y) u< x    ->    x u> (-1 >> y)
    assert(X == I.getOperand(1) && "instsimplify took care of commut. variant");
    DstPred = ICmpInst::Predicate::ICMP_UGT;
    break;
  case ICmpInst::Predicate::ICMP_ULE:
    //  x u<= x & (-1 >> y)    ->    x u<= (-1 >> y)
    assert(X == I.getOperand(0) && "instsimplify took care of commut. variant");
    DstPred = ICmpInst::Predicate::ICMP_ULE;
    break;
  case ICmpInst::Predicate::ICMP_SGT:
    //  x s> x & (-1 >> y)    ->    x s> (-1 >> y)
    if (X != I.getOperand(0)) // X must be on LHS of comparison!
      return nullptr;         // Ignore the other case.
    DstPred = ICmpInst::Predicate::ICMP_SGT;
    break;
  case ICmpInst::Predicate::ICMP_SGE:
    //  x & (-1 >> y) s>= x    ->    x s<= (-1 >> y)
    if (X != I.getOperand(1)) // X must be on RHS of comparison!
      return nullptr;         // Ignore the other case.
    if (!match(M, m_Constant())) // Can not do this fold with non-constant.
      return nullptr;
    if (!match(M, m_NonNegative())) // Must not have any -1 vector elements.
      return nullptr;
    DstPred = ICmpInst::Predicate::ICMP_SLE;
    break;
  case ICmpInst::Predicate::ICMP_SLT:
    //  x & (-1 >> y) s< x    ->    x s> (-1 >> y)
    if (X != I.getOperand(1)) // X must be on RHS of comparison!
      return nullptr;         // Ignore the other case.
    if (!match(M, m_Constant())) // Can not do this fold with non-constant.
      return nullptr;
    if (!match(M, m_NonNegative())) // Must not have any -1 vector elements.
      return nullptr;
    DstPred = ICmpInst::Predicate::ICMP_SGT;
    break;
  case ICmpInst::Predicate::ICMP_SLE:
    //  x s<= x & (-1 >> y)    ->    x s<= (-1 >> y)
    if (X != I.getOperand(0)) // X must be on LHS of comparison!
      return nullptr;         // Ignore the other case.
    DstPred = ICmpInst::Predicate::ICMP_SLE;
    break;
  default:
    llvm_unreachable("All possible folds are handled.");
  }

  return Builder.CreateICmp(DstPred, X, M);
}

/// Some comparisons can be simplified.
/// In this case, we are looking for comparisons that look like
/// a check for a lossy signed truncation.
/// Folds:   (MaskedBits is a constant.)
///   ((%x << MaskedBits) a>> MaskedBits) SrcPred %x
/// Into:
///   (add %x, (1 << (KeptBits-1))) DstPred (1 << KeptBits)
/// Where  KeptBits = bitwidth(%x) - MaskedBits
static Value *
foldICmpWithTruncSignExtendedVal(ICmpInst &I,
                                 InstCombiner::BuilderTy &Builder) {
  ICmpInst::Predicate SrcPred;
  Value *X;
  const APInt *C0, *C1; // FIXME: non-splats, potentially with undef.
  // We are ok with 'shl' having multiple uses, but 'ashr' must be one-use.
  if (!match(&I, m_c_ICmp(SrcPred,
                          m_OneUse(m_AShr(m_Shl(m_Value(X), m_APInt(C0)),
                                          m_APInt(C1))),
                          m_Deferred(X))))
    return nullptr;

  // Potential handling of non-splats: for each element:
  //  * if both are undef, replace with constant 0.
  //    Because (1<<0) is OK and is 1, and ((1<<0)>>1) is also OK and is 0.
  //  * if both are not undef, and are different, bailout.
  //  * else, only one is undef, then pick the non-undef one.

  // The shift amount must be equal.
  if (*C0 != *C1)
    return nullptr;
  const APInt &MaskedBits = *C0;
  assert(MaskedBits != 0 && "shift by zero should be folded away already.");

  ICmpInst::Predicate DstPred;
  switch (SrcPred) {
  case ICmpInst::Predicate::ICMP_EQ:
    // ((%x << MaskedBits) a>> MaskedBits) == %x
    //   =>
    // (add %x, (1 << (KeptBits-1))) u< (1 << KeptBits)
    DstPred = ICmpInst::Predicate::ICMP_ULT;
    break;
  case ICmpInst::Predicate::ICMP_NE:
    // ((%x << MaskedBits) a>> MaskedBits) != %x
    //   =>
    // (add %x, (1 << (KeptBits-1))) u>= (1 << KeptBits)
    DstPred = ICmpInst::Predicate::ICMP_UGE;
    break;
  // FIXME: are more folds possible?
  default:
    return nullptr;
  }

  auto *XType = X->getType();
  const unsigned XBitWidth = XType->getScalarSizeInBits();
  const APInt BitWidth = APInt(XBitWidth, XBitWidth);
  assert(BitWidth.ugt(MaskedBits) && "shifts should leave some bits untouched");

  // KeptBits = bitwidth(%x) - MaskedBits
  const APInt KeptBits = BitWidth - MaskedBits;
  assert(KeptBits.ugt(0) && KeptBits.ult(BitWidth) && "unreachable");
  // ICmpCst = (1 << KeptBits)
  const APInt ICmpCst = APInt(XBitWidth, 1).shl(KeptBits);
  assert(ICmpCst.isPowerOf2());
  // AddCst = (1 << (KeptBits-1))
  const APInt AddCst = ICmpCst.lshr(1);
  assert(AddCst.ult(ICmpCst) && AddCst.isPowerOf2());

  // T0 = add %x, AddCst
  Value *T0 = Builder.CreateAdd(X, ConstantInt::get(XType, AddCst));
  // T1 = T0 DstPred ICmpCst
  Value *T1 = Builder.CreateICmp(DstPred, T0, ConstantInt::get(XType, ICmpCst));

  return T1;
}

/// Try to fold icmp (binop), X or icmp X, (binop).
/// TODO: A large part of this logic is duplicated in InstSimplify's
/// simplifyICmpWithBinOp(). We should be able to share that and avoid the code
/// duplication.
Instruction *InstCombiner::foldICmpBinOp(ICmpInst &I) {
  Value *Op0 = I.getOperand(0), *Op1 = I.getOperand(1);

  // Special logic for binary operators.
  BinaryOperator *BO0 = dyn_cast<BinaryOperator>(Op0);
  BinaryOperator *BO1 = dyn_cast<BinaryOperator>(Op1);
  if (!BO0 && !BO1)
    return nullptr;

  const CmpInst::Predicate Pred = I.getPredicate();
  Value *X;

  // Convert add-with-unsigned-overflow comparisons into a 'not' with compare.
  // (Op1 + X) <u Op1 --> ~Op1 <u X
  // Op0 >u (Op0 + X) --> X >u ~Op0
  if (match(Op0, m_OneUse(m_c_Add(m_Specific(Op1), m_Value(X)))) &&
      Pred == ICmpInst::ICMP_ULT)
    return new ICmpInst(Pred, Builder.CreateNot(Op1), X);
  if (match(Op1, m_OneUse(m_c_Add(m_Specific(Op0), m_Value(X)))) &&
      Pred == ICmpInst::ICMP_UGT)
    return new ICmpInst(Pred, X, Builder.CreateNot(Op0));

  bool NoOp0WrapProblem = false, NoOp1WrapProblem = false;
  if (BO0 && isa<OverflowingBinaryOperator>(BO0))
    NoOp0WrapProblem =
        ICmpInst::isEquality(Pred) ||
        (CmpInst::isUnsigned(Pred) && BO0->hasNoUnsignedWrap()) ||
        (CmpInst::isSigned(Pred) && BO0->hasNoSignedWrap());
  if (BO1 && isa<OverflowingBinaryOperator>(BO1))
    NoOp1WrapProblem =
        ICmpInst::isEquality(Pred) ||
        (CmpInst::isUnsigned(Pred) && BO1->hasNoUnsignedWrap()) ||
        (CmpInst::isSigned(Pred) && BO1->hasNoSignedWrap());

  // Analyze the case when either Op0 or Op1 is an add instruction.
  // Op0 = A + B (or A and B are null); Op1 = C + D (or C and D are null).
  Value *A = nullptr, *B = nullptr, *C = nullptr, *D = nullptr;
  if (BO0 && BO0->getOpcode() == Instruction::Add) {
    A = BO0->getOperand(0);
    B = BO0->getOperand(1);
  }
  if (BO1 && BO1->getOpcode() == Instruction::Add) {
    C = BO1->getOperand(0);
    D = BO1->getOperand(1);
  }

  // icmp (X+Y), X -> icmp Y, 0 for equalities or if there is no overflow.
  if ((A == Op1 || B == Op1) && NoOp0WrapProblem)
    return new ICmpInst(Pred, A == Op1 ? B : A,
                        Constant::getNullValue(Op1->getType()));

  // icmp X, (X+Y) -> icmp 0, Y for equalities or if there is no overflow.
  if ((C == Op0 || D == Op0) && NoOp1WrapProblem)
    return new ICmpInst(Pred, Constant::getNullValue(Op0->getType()),
                        C == Op0 ? D : C);

  // icmp (X+Y), (X+Z) -> icmp Y, Z for equalities or if there is no overflow.
  if (A && C && (A == C || A == D || B == C || B == D) && NoOp0WrapProblem &&
      NoOp1WrapProblem &&
      // Try not to increase register pressure.
      BO0->hasOneUse() && BO1->hasOneUse()) {
    // Determine Y and Z in the form icmp (X+Y), (X+Z).
    Value *Y, *Z;
    if (A == C) {
      // C + B == C + D  ->  B == D
      Y = B;
      Z = D;
    } else if (A == D) {
      // D + B == C + D  ->  B == C
      Y = B;
      Z = C;
    } else if (B == C) {
      // A + C == C + D  ->  A == D
      Y = A;
      Z = D;
    } else {
      assert(B == D);
      // A + D == C + D  ->  A == C
      Y = A;
      Z = C;
    }
    return new ICmpInst(Pred, Y, Z);
  }

  // icmp slt (X + -1), Y -> icmp sle X, Y
  if (A && NoOp0WrapProblem && Pred == CmpInst::ICMP_SLT &&
      match(B, m_AllOnes()))
    return new ICmpInst(CmpInst::ICMP_SLE, A, Op1);

  // icmp sge (X + -1), Y -> icmp sgt X, Y
  if (A && NoOp0WrapProblem && Pred == CmpInst::ICMP_SGE &&
      match(B, m_AllOnes()))
    return new ICmpInst(CmpInst::ICMP_SGT, A, Op1);

  // icmp sle (X + 1), Y -> icmp slt X, Y
  if (A && NoOp0WrapProblem && Pred == CmpInst::ICMP_SLE && match(B, m_One()))
    return new ICmpInst(CmpInst::ICMP_SLT, A, Op1);

  // icmp sgt (X + 1), Y -> icmp sge X, Y
  if (A && NoOp0WrapProblem && Pred == CmpInst::ICMP_SGT && match(B, m_One()))
    return new ICmpInst(CmpInst::ICMP_SGE, A, Op1);

  // icmp sgt X, (Y + -1) -> icmp sge X, Y
  if (C && NoOp1WrapProblem && Pred == CmpInst::ICMP_SGT &&
      match(D, m_AllOnes()))
    return new ICmpInst(CmpInst::ICMP_SGE, Op0, C);

  // icmp sle X, (Y + -1) -> icmp slt X, Y
  if (C && NoOp1WrapProblem && Pred == CmpInst::ICMP_SLE &&
      match(D, m_AllOnes()))
    return new ICmpInst(CmpInst::ICMP_SLT, Op0, C);

  // icmp sge X, (Y + 1) -> icmp sgt X, Y
  if (C && NoOp1WrapProblem && Pred == CmpInst::ICMP_SGE && match(D, m_One()))
    return new ICmpInst(CmpInst::ICMP_SGT, Op0, C);

  // icmp slt X, (Y + 1) -> icmp sle X, Y
  if (C && NoOp1WrapProblem && Pred == CmpInst::ICMP_SLT && match(D, m_One()))
    return new ICmpInst(CmpInst::ICMP_SLE, Op0, C);

  // TODO: The subtraction-related identities shown below also hold, but
  // canonicalization from (X -nuw 1) to (X + -1) means that the combinations
  // wouldn't happen even if they were implemented.
  //
  // icmp ult (X - 1), Y -> icmp ule X, Y
  // icmp uge (X - 1), Y -> icmp ugt X, Y
  // icmp ugt X, (Y - 1) -> icmp uge X, Y
  // icmp ule X, (Y - 1) -> icmp ult X, Y

  // icmp ule (X + 1), Y -> icmp ult X, Y
  if (A && NoOp0WrapProblem && Pred == CmpInst::ICMP_ULE && match(B, m_One()))
    return new ICmpInst(CmpInst::ICMP_ULT, A, Op1);

  // icmp ugt (X + 1), Y -> icmp uge X, Y
  if (A && NoOp0WrapProblem && Pred == CmpInst::ICMP_UGT && match(B, m_One()))
    return new ICmpInst(CmpInst::ICMP_UGE, A, Op1);

  // icmp uge X, (Y + 1) -> icmp ugt X, Y
  if (C && NoOp1WrapProblem && Pred == CmpInst::ICMP_UGE && match(D, m_One()))
    return new ICmpInst(CmpInst::ICMP_UGT, Op0, C);

  // icmp ult X, (Y + 1) -> icmp ule X, Y
  if (C && NoOp1WrapProblem && Pred == CmpInst::ICMP_ULT && match(D, m_One()))
    return new ICmpInst(CmpInst::ICMP_ULE, Op0, C);

  // if C1 has greater magnitude than C2:
  //  icmp (X + C1), (Y + C2) -> icmp (X + C3), Y
  //  s.t. C3 = C1 - C2
  //
  // if C2 has greater magnitude than C1:
  //  icmp (X + C1), (Y + C2) -> icmp X, (Y + C3)
  //  s.t. C3 = C2 - C1
  if (A && C && NoOp0WrapProblem && NoOp1WrapProblem &&
      (BO0->hasOneUse() || BO1->hasOneUse()) && !I.isUnsigned())
    if (ConstantInt *C1 = dyn_cast<ConstantInt>(B))
      if (ConstantInt *C2 = dyn_cast<ConstantInt>(D)) {
        const APInt &AP1 = C1->getValue();
        const APInt &AP2 = C2->getValue();
        if (AP1.isNegative() == AP2.isNegative()) {
          APInt AP1Abs = C1->getValue().abs();
          APInt AP2Abs = C2->getValue().abs();
          if (AP1Abs.uge(AP2Abs)) {
            ConstantInt *C3 = Builder.getInt(AP1 - AP2);
            Value *NewAdd = Builder.CreateNSWAdd(A, C3);
            return new ICmpInst(Pred, NewAdd, C);
          } else {
            ConstantInt *C3 = Builder.getInt(AP2 - AP1);
            Value *NewAdd = Builder.CreateNSWAdd(C, C3);
            return new ICmpInst(Pred, A, NewAdd);
          }
        }
      }

  // Analyze the case when either Op0 or Op1 is a sub instruction.
  // Op0 = A - B (or A and B are null); Op1 = C - D (or C and D are null).
  A = nullptr;
  B = nullptr;
  C = nullptr;
  D = nullptr;
  if (BO0 && BO0->getOpcode() == Instruction::Sub) {
    A = BO0->getOperand(0);
    B = BO0->getOperand(1);
  }
  if (BO1 && BO1->getOpcode() == Instruction::Sub) {
    C = BO1->getOperand(0);
    D = BO1->getOperand(1);
  }

  // icmp (X-Y), X -> icmp 0, Y for equalities or if there is no overflow.
  if (A == Op1 && NoOp0WrapProblem)
    return new ICmpInst(Pred, Constant::getNullValue(Op1->getType()), B);
  // icmp X, (X-Y) -> icmp Y, 0 for equalities or if there is no overflow.
  if (C == Op0 && NoOp1WrapProblem)
    return new ICmpInst(Pred, D, Constant::getNullValue(Op0->getType()));

  // (A - B) >u A --> A <u B
  if (A == Op1 && Pred == ICmpInst::ICMP_UGT)
    return new ICmpInst(ICmpInst::ICMP_ULT, A, B);
  // C <u (C - D) --> C <u D
  if (C == Op0 && Pred == ICmpInst::ICMP_ULT)
    return new ICmpInst(ICmpInst::ICMP_ULT, C, D);

  // icmp (Y-X), (Z-X) -> icmp Y, Z for equalities or if there is no overflow.
  if (B && D && B == D && NoOp0WrapProblem && NoOp1WrapProblem &&
      // Try not to increase register pressure.
      BO0->hasOneUse() && BO1->hasOneUse())
    return new ICmpInst(Pred, A, C);
  // icmp (X-Y), (X-Z) -> icmp Z, Y for equalities or if there is no overflow.
  if (A && C && A == C && NoOp0WrapProblem && NoOp1WrapProblem &&
      // Try not to increase register pressure.
      BO0->hasOneUse() && BO1->hasOneUse())
    return new ICmpInst(Pred, D, B);

  // icmp (0-X) < cst --> x > -cst
  if (NoOp0WrapProblem && ICmpInst::isSigned(Pred)) {
    Value *X;
    if (match(BO0, m_Neg(m_Value(X))))
      if (Constant *RHSC = dyn_cast<Constant>(Op1))
        if (RHSC->isNotMinSignedValue())
          return new ICmpInst(I.getSwappedPredicate(), X,
                              ConstantExpr::getNeg(RHSC));
  }

  BinaryOperator *SRem = nullptr;
  // icmp (srem X, Y), Y
  if (BO0 && BO0->getOpcode() == Instruction::SRem && Op1 == BO0->getOperand(1))
    SRem = BO0;
  // icmp Y, (srem X, Y)
  else if (BO1 && BO1->getOpcode() == Instruction::SRem &&
           Op0 == BO1->getOperand(1))
    SRem = BO1;
  if (SRem) {
    // We don't check hasOneUse to avoid increasing register pressure because
    // the value we use is the same value this instruction was already using.
    switch (SRem == BO0 ? ICmpInst::getSwappedPredicate(Pred) : Pred) {
    default:
      break;
    case ICmpInst::ICMP_EQ:
      return replaceInstUsesWith(I, ConstantInt::getFalse(I.getType()));
    case ICmpInst::ICMP_NE:
      return replaceInstUsesWith(I, ConstantInt::getTrue(I.getType()));
    case ICmpInst::ICMP_SGT:
    case ICmpInst::ICMP_SGE:
      return new ICmpInst(ICmpInst::ICMP_SGT, SRem->getOperand(1),
                          Constant::getAllOnesValue(SRem->getType()));
    case ICmpInst::ICMP_SLT:
    case ICmpInst::ICMP_SLE:
      return new ICmpInst(ICmpInst::ICMP_SLT, SRem->getOperand(1),
                          Constant::getNullValue(SRem->getType()));
    }
  }

  if (BO0 && BO1 && BO0->getOpcode() == BO1->getOpcode() && BO0->hasOneUse() &&
      BO1->hasOneUse() && BO0->getOperand(1) == BO1->getOperand(1)) {
    switch (BO0->getOpcode()) {
    default:
      break;
    case Instruction::Add:
    case Instruction::Sub:
    case Instruction::Xor: {
      if (I.isEquality()) // a+x icmp eq/ne b+x --> a icmp b
        return new ICmpInst(Pred, BO0->getOperand(0), BO1->getOperand(0));

      const APInt *C;
      if (match(BO0->getOperand(1), m_APInt(C))) {
        // icmp u/s (a ^ signmask), (b ^ signmask) --> icmp s/u a, b
        if (C->isSignMask()) {
          ICmpInst::Predicate NewPred =
              I.isSigned() ? I.getUnsignedPredicate() : I.getSignedPredicate();
          return new ICmpInst(NewPred, BO0->getOperand(0), BO1->getOperand(0));
        }

        // icmp u/s (a ^ maxsignval), (b ^ maxsignval) --> icmp s/u' a, b
        if (BO0->getOpcode() == Instruction::Xor && C->isMaxSignedValue()) {
          ICmpInst::Predicate NewPred =
              I.isSigned() ? I.getUnsignedPredicate() : I.getSignedPredicate();
          NewPred = I.getSwappedPredicate(NewPred);
          return new ICmpInst(NewPred, BO0->getOperand(0), BO1->getOperand(0));
        }
      }
      break;
    }
    case Instruction::Mul: {
      if (!I.isEquality())
        break;

      const APInt *C;
      if (match(BO0->getOperand(1), m_APInt(C)) && !C->isNullValue() &&
          !C->isOneValue()) {
        // icmp eq/ne (X * C), (Y * C) --> icmp (X & Mask), (Y & Mask)
        // Mask = -1 >> count-trailing-zeros(C).
        if (unsigned TZs = C->countTrailingZeros()) {
          Constant *Mask = ConstantInt::get(
              BO0->getType(),
              APInt::getLowBitsSet(C->getBitWidth(), C->getBitWidth() - TZs));
          Value *And1 = Builder.CreateAnd(BO0->getOperand(0), Mask);
          Value *And2 = Builder.CreateAnd(BO1->getOperand(0), Mask);
          return new ICmpInst(Pred, And1, And2);
        }
        // If there are no trailing zeros in the multiplier, just eliminate
        // the multiplies (no masking is needed):
        // icmp eq/ne (X * C), (Y * C) --> icmp eq/ne X, Y
        return new ICmpInst(Pred, BO0->getOperand(0), BO1->getOperand(0));
      }
      break;
    }
    case Instruction::UDiv:
    case Instruction::LShr:
      if (I.isSigned() || !BO0->isExact() || !BO1->isExact())
        break;
      return new ICmpInst(Pred, BO0->getOperand(0), BO1->getOperand(0));

    case Instruction::SDiv:
      if (!I.isEquality() || !BO0->isExact() || !BO1->isExact())
        break;
      return new ICmpInst(Pred, BO0->getOperand(0), BO1->getOperand(0));

    case Instruction::AShr:
      if (!BO0->isExact() || !BO1->isExact())
        break;
      return new ICmpInst(Pred, BO0->getOperand(0), BO1->getOperand(0));

    case Instruction::Shl: {
      bool NUW = BO0->hasNoUnsignedWrap() && BO1->hasNoUnsignedWrap();
      bool NSW = BO0->hasNoSignedWrap() && BO1->hasNoSignedWrap();
      if (!NUW && !NSW)
        break;
      if (!NSW && I.isSigned())
        break;
      return new ICmpInst(Pred, BO0->getOperand(0), BO1->getOperand(0));
    }
    }
  }

  if (BO0) {
    // Transform  A & (L - 1) `ult` L --> L != 0
    auto LSubOne = m_Add(m_Specific(Op1), m_AllOnes());
    auto BitwiseAnd = m_c_And(m_Value(), LSubOne);

    if (match(BO0, BitwiseAnd) && Pred == ICmpInst::ICMP_ULT) {
      auto *Zero = Constant::getNullValue(BO0->getType());
      return new ICmpInst(ICmpInst::ICMP_NE, Op1, Zero);
    }
  }

  if (Value *V = foldICmpWithLowBitMaskedVal(I, Builder))
    return replaceInstUsesWith(I, V);

  if (Value *V = foldICmpWithTruncSignExtendedVal(I, Builder))
    return replaceInstUsesWith(I, V);

  return nullptr;
}

/// Fold icmp Pred min|max(X, Y), X.
static Instruction *foldICmpWithMinMax(ICmpInst &Cmp) {
  ICmpInst::Predicate Pred = Cmp.getPredicate();
  Value *Op0 = Cmp.getOperand(0);
  Value *X = Cmp.getOperand(1);

  // Canonicalize minimum or maximum operand to LHS of the icmp.
  if (match(X, m_c_SMin(m_Specific(Op0), m_Value())) ||
      match(X, m_c_SMax(m_Specific(Op0), m_Value())) ||
      match(X, m_c_UMin(m_Specific(Op0), m_Value())) ||
      match(X, m_c_UMax(m_Specific(Op0), m_Value()))) {
    std::swap(Op0, X);
    Pred = Cmp.getSwappedPredicate();
  }

  Value *Y;
  if (match(Op0, m_c_SMin(m_Specific(X), m_Value(Y)))) {
    // smin(X, Y)  == X --> X s<= Y
    // smin(X, Y) s>= X --> X s<= Y
    if (Pred == CmpInst::ICMP_EQ || Pred == CmpInst::ICMP_SGE)
      return new ICmpInst(ICmpInst::ICMP_SLE, X, Y);

    // smin(X, Y) != X --> X s> Y
    // smin(X, Y) s< X --> X s> Y
    if (Pred == CmpInst::ICMP_NE || Pred == CmpInst::ICMP_SLT)
      return new ICmpInst(ICmpInst::ICMP_SGT, X, Y);

    // These cases should be handled in InstSimplify:
    // smin(X, Y) s<= X --> true
    // smin(X, Y) s> X --> false
    return nullptr;
  }

  if (match(Op0, m_c_SMax(m_Specific(X), m_Value(Y)))) {
    // smax(X, Y)  == X --> X s>= Y
    // smax(X, Y) s<= X --> X s>= Y
    if (Pred == CmpInst::ICMP_EQ || Pred == CmpInst::ICMP_SLE)
      return new ICmpInst(ICmpInst::ICMP_SGE, X, Y);

    // smax(X, Y) != X --> X s< Y
    // smax(X, Y) s> X --> X s< Y
    if (Pred == CmpInst::ICMP_NE || Pred == CmpInst::ICMP_SGT)
      return new ICmpInst(ICmpInst::ICMP_SLT, X, Y);

    // These cases should be handled in InstSimplify:
    // smax(X, Y) s>= X --> true
    // smax(X, Y) s< X --> false
    return nullptr;
  }

  if (match(Op0, m_c_UMin(m_Specific(X), m_Value(Y)))) {
    // umin(X, Y)  == X --> X u<= Y
    // umin(X, Y) u>= X --> X u<= Y
    if (Pred == CmpInst::ICMP_EQ || Pred == CmpInst::ICMP_UGE)
      return new ICmpInst(ICmpInst::ICMP_ULE, X, Y);

    // umin(X, Y) != X --> X u> Y
    // umin(X, Y) u< X --> X u> Y
    if (Pred == CmpInst::ICMP_NE || Pred == CmpInst::ICMP_ULT)
      return new ICmpInst(ICmpInst::ICMP_UGT, X, Y);

    // These cases should be handled in InstSimplify:
    // umin(X, Y) u<= X --> true
    // umin(X, Y) u> X --> false
    return nullptr;
  }

  if (match(Op0, m_c_UMax(m_Specific(X), m_Value(Y)))) {
    // umax(X, Y)  == X --> X u>= Y
    // umax(X, Y) u<= X --> X u>= Y
    if (Pred == CmpInst::ICMP_EQ || Pred == CmpInst::ICMP_ULE)
      return new ICmpInst(ICmpInst::ICMP_UGE, X, Y);

    // umax(X, Y) != X --> X u< Y
    // umax(X, Y) u> X --> X u< Y
    if (Pred == CmpInst::ICMP_NE || Pred == CmpInst::ICMP_UGT)
      return new ICmpInst(ICmpInst::ICMP_ULT, X, Y);

    // These cases should be handled in InstSimplify:
    // umax(X, Y) u>= X --> true
    // umax(X, Y) u< X --> false
    return nullptr;
  }

  return nullptr;
}

Instruction *InstCombiner::foldICmpEquality(ICmpInst &I) {
  if (!I.isEquality())
    return nullptr;

  Value *Op0 = I.getOperand(0), *Op1 = I.getOperand(1);
  const CmpInst::Predicate Pred = I.getPredicate();
  Value *A, *B, *C, *D;
  if (match(Op0, m_Xor(m_Value(A), m_Value(B)))) {
    if (A == Op1 || B == Op1) { // (A^B) == A  ->  B == 0
      Value *OtherVal = A == Op1 ? B : A;
      return new ICmpInst(Pred, OtherVal, Constant::getNullValue(A->getType()));
    }

    if (match(Op1, m_Xor(m_Value(C), m_Value(D)))) {
      // A^c1 == C^c2 --> A == C^(c1^c2)
      ConstantInt *C1, *C2;
      if (match(B, m_ConstantInt(C1)) && match(D, m_ConstantInt(C2)) &&
          Op1->hasOneUse()) {
        Constant *NC = Builder.getInt(C1->getValue() ^ C2->getValue());
        Value *Xor = Builder.CreateXor(C, NC);
        return new ICmpInst(Pred, A, Xor);
      }

      // A^B == A^D -> B == D
      if (A == C)
        return new ICmpInst(Pred, B, D);
      if (A == D)
        return new ICmpInst(Pred, B, C);
      if (B == C)
        return new ICmpInst(Pred, A, D);
      if (B == D)
        return new ICmpInst(Pred, A, C);
    }
  }

  if (match(Op1, m_Xor(m_Value(A), m_Value(B))) && (A == Op0 || B == Op0)) {
    // A == (A^B)  ->  B == 0
    Value *OtherVal = A == Op0 ? B : A;
    return new ICmpInst(Pred, OtherVal, Constant::getNullValue(A->getType()));
  }

  // (X&Z) == (Y&Z) -> (X^Y) & Z == 0
  if (match(Op0, m_OneUse(m_And(m_Value(A), m_Value(B)))) &&
      match(Op1, m_OneUse(m_And(m_Value(C), m_Value(D))))) {
    Value *X = nullptr, *Y = nullptr, *Z = nullptr;

    if (A == C) {
      X = B;
      Y = D;
      Z = A;
    } else if (A == D) {
      X = B;
      Y = C;
      Z = A;
    } else if (B == C) {
      X = A;
      Y = D;
      Z = B;
    } else if (B == D) {
      X = A;
      Y = C;
      Z = B;
    }

    if (X) { // Build (X^Y) & Z
      Op1 = Builder.CreateXor(X, Y);
      Op1 = Builder.CreateAnd(Op1, Z);
      I.setOperand(0, Op1);
      I.setOperand(1, Constant::getNullValue(Op1->getType()));
      return &I;
    }
  }

  // Transform (zext A) == (B & (1<<X)-1) --> A == (trunc B)
  // and       (B & (1<<X)-1) == (zext A) --> A == (trunc B)
  ConstantInt *Cst1;
  if ((Op0->hasOneUse() && match(Op0, m_ZExt(m_Value(A))) &&
       match(Op1, m_And(m_Value(B), m_ConstantInt(Cst1)))) ||
      (Op1->hasOneUse() && match(Op0, m_And(m_Value(B), m_ConstantInt(Cst1))) &&
       match(Op1, m_ZExt(m_Value(A))))) {
    APInt Pow2 = Cst1->getValue() + 1;
    if (Pow2.isPowerOf2() && isa<IntegerType>(A->getType()) &&
        Pow2.logBase2() == cast<IntegerType>(A->getType())->getBitWidth())
      return new ICmpInst(Pred, A, Builder.CreateTrunc(B, A->getType()));
  }

  // (A >> C) == (B >> C) --> (A^B) u< (1 << C)
  // For lshr and ashr pairs.
  if ((match(Op0, m_OneUse(m_LShr(m_Value(A), m_ConstantInt(Cst1)))) &&
       match(Op1, m_OneUse(m_LShr(m_Value(B), m_Specific(Cst1))))) ||
      (match(Op0, m_OneUse(m_AShr(m_Value(A), m_ConstantInt(Cst1)))) &&
       match(Op1, m_OneUse(m_AShr(m_Value(B), m_Specific(Cst1)))))) {
    unsigned TypeBits = Cst1->getBitWidth();
    unsigned ShAmt = (unsigned)Cst1->getLimitedValue(TypeBits);
    if (ShAmt < TypeBits && ShAmt != 0) {
      ICmpInst::Predicate NewPred =
          Pred == ICmpInst::ICMP_NE ? ICmpInst::ICMP_UGE : ICmpInst::ICMP_ULT;
      Value *Xor = Builder.CreateXor(A, B, I.getName() + ".unshifted");
      APInt CmpVal = APInt::getOneBitSet(TypeBits, ShAmt);
      return new ICmpInst(NewPred, Xor, Builder.getInt(CmpVal));
    }
  }

  // (A << C) == (B << C) --> ((A^B) & (~0U >> C)) == 0
  if (match(Op0, m_OneUse(m_Shl(m_Value(A), m_ConstantInt(Cst1)))) &&
      match(Op1, m_OneUse(m_Shl(m_Value(B), m_Specific(Cst1))))) {
    unsigned TypeBits = Cst1->getBitWidth();
    unsigned ShAmt = (unsigned)Cst1->getLimitedValue(TypeBits);
    if (ShAmt < TypeBits && ShAmt != 0) {
      Value *Xor = Builder.CreateXor(A, B, I.getName() + ".unshifted");
      APInt AndVal = APInt::getLowBitsSet(TypeBits, TypeBits - ShAmt);
      Value *And = Builder.CreateAnd(Xor, Builder.getInt(AndVal),
                                      I.getName() + ".mask");
      return new ICmpInst(Pred, And, Constant::getNullValue(Cst1->getType()));
    }
  }

  // Transform "icmp eq (trunc (lshr(X, cst1)), cst" to
  // "icmp (and X, mask), cst"
  uint64_t ShAmt = 0;
  if (Op0->hasOneUse() &&
      match(Op0, m_Trunc(m_OneUse(m_LShr(m_Value(A), m_ConstantInt(ShAmt))))) &&
      match(Op1, m_ConstantInt(Cst1)) &&
      // Only do this when A has multiple uses.  This is most important to do
      // when it exposes other optimizations.
      !A->hasOneUse()) {
    unsigned ASize = cast<IntegerType>(A->getType())->getPrimitiveSizeInBits();

    if (ShAmt < ASize) {
      APInt MaskV =
          APInt::getLowBitsSet(ASize, Op0->getType()->getPrimitiveSizeInBits());
      MaskV <<= ShAmt;

      APInt CmpV = Cst1->getValue().zext(ASize);
      CmpV <<= ShAmt;

      Value *Mask = Builder.CreateAnd(A, Builder.getInt(MaskV));
      return new ICmpInst(Pred, Mask, Builder.getInt(CmpV));
    }
  }

  // If both operands are byte-swapped or bit-reversed, just compare the
  // original values.
  // TODO: Move this to a function similar to foldICmpIntrinsicWithConstant()
  // and handle more intrinsics.
  if ((match(Op0, m_BSwap(m_Value(A))) && match(Op1, m_BSwap(m_Value(B)))) ||
      (match(Op0, m_BitReverse(m_Value(A))) &&
       match(Op1, m_BitReverse(m_Value(B)))))
    return new ICmpInst(Pred, A, B);

  return nullptr;
}

/// Handle icmp (cast x to y), (cast/cst). We only handle extending casts so
/// far.
Instruction *InstCombiner::foldICmpWithCastAndCast(ICmpInst &ICmp) {
  const CastInst *LHSCI = cast<CastInst>(ICmp.getOperand(0));
  Value *LHSCIOp        = LHSCI->getOperand(0);
  Type *SrcTy     = LHSCIOp->getType();
  Type *DestTy    = LHSCI->getType();
  Value *RHSCIOp;

  // Turn icmp (ptrtoint x), (ptrtoint/c) into a compare of the input if the
  // integer type is the same size as the pointer type.
  const auto& CompatibleSizes = [&](Type* SrcTy, Type* DestTy) -> bool {
    if (isa<VectorType>(SrcTy)) {
      SrcTy = cast<VectorType>(SrcTy)->getElementType();
      DestTy = cast<VectorType>(DestTy)->getElementType();
    }
    return DL.getPointerTypeSizeInBits(SrcTy) == DestTy->getIntegerBitWidth();
  };
  if (LHSCI->getOpcode() == Instruction::PtrToInt &&
      CompatibleSizes(SrcTy, DestTy)) {
    Value *RHSOp = nullptr;
    if (auto *RHSC = dyn_cast<PtrToIntOperator>(ICmp.getOperand(1))) {
      Value *RHSCIOp = RHSC->getOperand(0);
      if (RHSCIOp->getType()->getPointerAddressSpace() ==
          LHSCIOp->getType()->getPointerAddressSpace()) {
        RHSOp = RHSC->getOperand(0);
        // If the pointer types don't match, insert a bitcast.
        if (LHSCIOp->getType() != RHSOp->getType())
          RHSOp = Builder.CreateBitCast(RHSOp, LHSCIOp->getType());
      }
    } else if (auto *RHSC = dyn_cast<Constant>(ICmp.getOperand(1))) {
      RHSOp = ConstantExpr::getIntToPtr(RHSC, SrcTy);
    }

    if (RHSOp)
      return new ICmpInst(ICmp.getPredicate(), LHSCIOp, RHSOp);
  }

  // The code below only handles extension cast instructions, so far.
  // Enforce this.
  if (LHSCI->getOpcode() != Instruction::ZExt &&
      LHSCI->getOpcode() != Instruction::SExt)
    return nullptr;

  bool isSignedExt = LHSCI->getOpcode() == Instruction::SExt;
  bool isSignedCmp = ICmp.isSigned();

  if (auto *CI = dyn_cast<CastInst>(ICmp.getOperand(1))) {
    // Not an extension from the same type?
    RHSCIOp = CI->getOperand(0);
    if (RHSCIOp->getType() != LHSCIOp->getType())
      return nullptr;

    // If the signedness of the two casts doesn't agree (i.e. one is a sext
    // and the other is a zext), then we can't handle this.
    if (CI->getOpcode() != LHSCI->getOpcode())
      return nullptr;

    // Deal with equality cases early.
    if (ICmp.isEquality())
      return new ICmpInst(ICmp.getPredicate(), LHSCIOp, RHSCIOp);

    // A signed comparison of sign extended values simplifies into a
    // signed comparison.
    if (isSignedCmp && isSignedExt)
      return new ICmpInst(ICmp.getPredicate(), LHSCIOp, RHSCIOp);

    // The other three cases all fold into an unsigned comparison.
    return new ICmpInst(ICmp.getUnsignedPredicate(), LHSCIOp, RHSCIOp);
  }

  // If we aren't dealing with a constant on the RHS, exit early.
  auto *C = dyn_cast<Constant>(ICmp.getOperand(1));
  if (!C)
    return nullptr;

  // Compute the constant that would happen if we truncated to SrcTy then
  // re-extended to DestTy.
  Constant *Res1 = ConstantExpr::getTrunc(C, SrcTy);
  Constant *Res2 = ConstantExpr::getCast(LHSCI->getOpcode(), Res1, DestTy);

  // If the re-extended constant didn't change...
  if (Res2 == C) {
    // Deal with equality cases early.
    if (ICmp.isEquality())
      return new ICmpInst(ICmp.getPredicate(), LHSCIOp, Res1);

    // A signed comparison of sign extended values simplifies into a
    // signed comparison.
    if (isSignedExt && isSignedCmp)
      return new ICmpInst(ICmp.getPredicate(), LHSCIOp, Res1);

    // The other three cases all fold into an unsigned comparison.
    return new ICmpInst(ICmp.getUnsignedPredicate(), LHSCIOp, Res1);
  }

  // The re-extended constant changed, partly changed (in the case of a vector),
  // or could not be determined to be equal (in the case of a constant
  // expression), so the constant cannot be represented in the shorter type.
  // Consequently, we cannot emit a simple comparison.
  // All the cases that fold to true or false will have already been handled
  // by SimplifyICmpInst, so only deal with the tricky case.

  if (isSignedCmp || !isSignedExt || !isa<ConstantInt>(C))
    return nullptr;

  // Evaluate the comparison for LT (we invert for GT below). LE and GE cases
  // should have been folded away previously and not enter in here.

  // We're performing an unsigned comp with a sign extended value.
  // This is true if the input is >= 0. [aka >s -1]
  Constant *NegOne = Constant::getAllOnesValue(SrcTy);
  Value *Result = Builder.CreateICmpSGT(LHSCIOp, NegOne, ICmp.getName());

  // Finally, return the value computed.
  if (ICmp.getPredicate() == ICmpInst::ICMP_ULT)
    return replaceInstUsesWith(ICmp, Result);

  assert(ICmp.getPredicate() == ICmpInst::ICMP_UGT && "ICmp should be folded!");
  return BinaryOperator::CreateNot(Result);
}

bool InstCombiner::OptimizeOverflowCheck(OverflowCheckFlavor OCF, Value *LHS,
                                         Value *RHS, Instruction &OrigI,
                                         Value *&Result, Constant *&Overflow) {
  if (OrigI.isCommutative() && isa<Constant>(LHS) && !isa<Constant>(RHS))
    std::swap(LHS, RHS);

  auto SetResult = [&](Value *OpResult, Constant *OverflowVal, bool ReuseName) {
    Result = OpResult;
    Overflow = OverflowVal;
    if (ReuseName)
      Result->takeName(&OrigI);
    return true;
  };

  // If the overflow check was an add followed by a compare, the insertion point
  // may be pointing to the compare.  We want to insert the new instructions
  // before the add in case there are uses of the add between the add and the
  // compare.
  Builder.SetInsertPoint(&OrigI);

  switch (OCF) {
  case OCF_INVALID:
    llvm_unreachable("bad overflow check kind!");

  case OCF_UNSIGNED_ADD: {
    OverflowResult OR = computeOverflowForUnsignedAdd(LHS, RHS, &OrigI);
    if (OR == OverflowResult::NeverOverflows)
      return SetResult(Builder.CreateNUWAdd(LHS, RHS), Builder.getFalse(),
                       true);

    if (OR == OverflowResult::AlwaysOverflows)
      return SetResult(Builder.CreateAdd(LHS, RHS), Builder.getTrue(), true);

    // Fall through uadd into sadd
    LLVM_FALLTHROUGH;
  }
  case OCF_SIGNED_ADD: {
    // X + 0 -> {X, false}
    if (match(RHS, m_Zero()))
      return SetResult(LHS, Builder.getFalse(), false);

    // We can strength reduce this signed add into a regular add if we can prove
    // that it will never overflow.
    if (OCF == OCF_SIGNED_ADD)
      if (willNotOverflowSignedAdd(LHS, RHS, OrigI))
        return SetResult(Builder.CreateNSWAdd(LHS, RHS), Builder.getFalse(),
                         true);
    break;
  }

  case OCF_UNSIGNED_SUB:
  case OCF_SIGNED_SUB: {
    // X - 0 -> {X, false}
    if (match(RHS, m_Zero()))
      return SetResult(LHS, Builder.getFalse(), false);

    if (OCF == OCF_SIGNED_SUB) {
      if (willNotOverflowSignedSub(LHS, RHS, OrigI))
        return SetResult(Builder.CreateNSWSub(LHS, RHS), Builder.getFalse(),
                         true);
    } else {
      if (willNotOverflowUnsignedSub(LHS, RHS, OrigI))
        return SetResult(Builder.CreateNUWSub(LHS, RHS), Builder.getFalse(),
                         true);
    }
    break;
  }

  case OCF_UNSIGNED_MUL: {
    OverflowResult OR = computeOverflowForUnsignedMul(LHS, RHS, &OrigI);
    if (OR == OverflowResult::NeverOverflows)
      return SetResult(Builder.CreateNUWMul(LHS, RHS), Builder.getFalse(),
                       true);
    if (OR == OverflowResult::AlwaysOverflows)
      return SetResult(Builder.CreateMul(LHS, RHS), Builder.getTrue(), true);
    LLVM_FALLTHROUGH;
  }
  case OCF_SIGNED_MUL:
    // X * undef -> undef
    if (isa<UndefValue>(RHS))
      return SetResult(RHS, UndefValue::get(Builder.getInt1Ty()), false);

    // X * 0 -> {0, false}
    if (match(RHS, m_Zero()))
      return SetResult(RHS, Builder.getFalse(), false);

    // X * 1 -> {X, false}
    if (match(RHS, m_One()))
      return SetResult(LHS, Builder.getFalse(), false);

    if (OCF == OCF_SIGNED_MUL)
      if (willNotOverflowSignedMul(LHS, RHS, OrigI))
        return SetResult(Builder.CreateNSWMul(LHS, RHS), Builder.getFalse(),
                         true);
    break;
  }

  return false;
}

/// Recognize and process idiom involving test for multiplication
/// overflow.
///
/// The caller has matched a pattern of the form:
///   I = cmp u (mul(zext A, zext B), V
/// The function checks if this is a test for overflow and if so replaces
/// multiplication with call to 'mul.with.overflow' intrinsic.
///
/// \param I Compare instruction.
/// \param MulVal Result of 'mult' instruction.  It is one of the arguments of
///               the compare instruction.  Must be of integer type.
/// \param OtherVal The other argument of compare instruction.
/// \returns Instruction which must replace the compare instruction, NULL if no
///          replacement required.
static Instruction *processUMulZExtIdiom(ICmpInst &I, Value *MulVal,
                                         Value *OtherVal, InstCombiner &IC) {
  // Don't bother doing this transformation for pointers, don't do it for
  // vectors.
  if (!isa<IntegerType>(MulVal->getType()))
    return nullptr;

  assert(I.getOperand(0) == MulVal || I.getOperand(1) == MulVal);
  assert(I.getOperand(0) == OtherVal || I.getOperand(1) == OtherVal);
  auto *MulInstr = dyn_cast<Instruction>(MulVal);
  if (!MulInstr)
    return nullptr;
  assert(MulInstr->getOpcode() == Instruction::Mul);

  auto *LHS = cast<ZExtOperator>(MulInstr->getOperand(0)),
       *RHS = cast<ZExtOperator>(MulInstr->getOperand(1));
  assert(LHS->getOpcode() == Instruction::ZExt);
  assert(RHS->getOpcode() == Instruction::ZExt);
  Value *A = LHS->getOperand(0), *B = RHS->getOperand(0);

  // Calculate type and width of the result produced by mul.with.overflow.
  Type *TyA = A->getType(), *TyB = B->getType();
  unsigned WidthA = TyA->getPrimitiveSizeInBits(),
           WidthB = TyB->getPrimitiveSizeInBits();
  unsigned MulWidth;
  Type *MulType;
  if (WidthB > WidthA) {
    MulWidth = WidthB;
    MulType = TyB;
  } else {
    MulWidth = WidthA;
    MulType = TyA;
  }

  // In order to replace the original mul with a narrower mul.with.overflow,
  // all uses must ignore upper bits of the product.  The number of used low
  // bits must be not greater than the width of mul.with.overflow.
  if (MulVal->hasNUsesOrMore(2))
    for (User *U : MulVal->users()) {
      if (U == &I)
        continue;
      if (TruncInst *TI = dyn_cast<TruncInst>(U)) {
        // Check if truncation ignores bits above MulWidth.
        unsigned TruncWidth = TI->getType()->getPrimitiveSizeInBits();
        if (TruncWidth > MulWidth)
          return nullptr;
      } else if (BinaryOperator *BO = dyn_cast<BinaryOperator>(U)) {
        // Check if AND ignores bits above MulWidth.
        if (BO->getOpcode() != Instruction::And)
          return nullptr;
        if (ConstantInt *CI = dyn_cast<ConstantInt>(BO->getOperand(1))) {
          const APInt &CVal = CI->getValue();
          if (CVal.getBitWidth() - CVal.countLeadingZeros() > MulWidth)
            return nullptr;
        } else {
          // In this case we could have the operand of the binary operation
          // being defined in another block, and performing the replacement
          // could break the dominance relation.
          return nullptr;
        }
      } else {
        // Other uses prohibit this transformation.
        return nullptr;
      }
    }

  // Recognize patterns
  switch (I.getPredicate()) {
  case ICmpInst::ICMP_EQ:
  case ICmpInst::ICMP_NE:
    // Recognize pattern:
    //   mulval = mul(zext A, zext B)
    //   cmp eq/neq mulval, zext trunc mulval
    if (ZExtInst *Zext = dyn_cast<ZExtInst>(OtherVal))
      if (Zext->hasOneUse()) {
        Value *ZextArg = Zext->getOperand(0);
        if (TruncInst *Trunc = dyn_cast<TruncInst>(ZextArg))
          if (Trunc->getType()->getPrimitiveSizeInBits() == MulWidth)
            break; //Recognized
      }

    // Recognize pattern:
    //   mulval = mul(zext A, zext B)
    //   cmp eq/neq mulval, and(mulval, mask), mask selects low MulWidth bits.
    ConstantInt *CI;
    Value *ValToMask;
    if (match(OtherVal, m_And(m_Value(ValToMask), m_ConstantInt(CI)))) {
      if (ValToMask != MulVal)
        return nullptr;
      const APInt &CVal = CI->getValue() + 1;
      if (CVal.isPowerOf2()) {
        unsigned MaskWidth = CVal.logBase2();
        if (MaskWidth == MulWidth)
          break; // Recognized
      }
    }
    return nullptr;

  case ICmpInst::ICMP_UGT:
    // Recognize pattern:
    //   mulval = mul(zext A, zext B)
    //   cmp ugt mulval, max
    if (ConstantInt *CI = dyn_cast<ConstantInt>(OtherVal)) {
      APInt MaxVal = APInt::getMaxValue(MulWidth);
      MaxVal = MaxVal.zext(CI->getBitWidth());
      if (MaxVal.eq(CI->getValue()))
        break; // Recognized
    }
    return nullptr;

  case ICmpInst::ICMP_UGE:
    // Recognize pattern:
    //   mulval = mul(zext A, zext B)
    //   cmp uge mulval, max+1
    if (ConstantInt *CI = dyn_cast<ConstantInt>(OtherVal)) {
      APInt MaxVal = APInt::getOneBitSet(CI->getBitWidth(), MulWidth);
      if (MaxVal.eq(CI->getValue()))
        break; // Recognized
    }
    return nullptr;

  case ICmpInst::ICMP_ULE:
    // Recognize pattern:
    //   mulval = mul(zext A, zext B)
    //   cmp ule mulval, max
    if (ConstantInt *CI = dyn_cast<ConstantInt>(OtherVal)) {
      APInt MaxVal = APInt::getMaxValue(MulWidth);
      MaxVal = MaxVal.zext(CI->getBitWidth());
      if (MaxVal.eq(CI->getValue()))
        break; // Recognized
    }
    return nullptr;

  case ICmpInst::ICMP_ULT:
    // Recognize pattern:
    //   mulval = mul(zext A, zext B)
    //   cmp ule mulval, max + 1
    if (ConstantInt *CI = dyn_cast<ConstantInt>(OtherVal)) {
      APInt MaxVal = APInt::getOneBitSet(CI->getBitWidth(), MulWidth);
      if (MaxVal.eq(CI->getValue()))
        break; // Recognized
    }
    return nullptr;

  default:
    return nullptr;
  }

  InstCombiner::BuilderTy &Builder = IC.Builder;
  Builder.SetInsertPoint(MulInstr);

  // Replace: mul(zext A, zext B) --> mul.with.overflow(A, B)
  Value *MulA = A, *MulB = B;
  if (WidthA < MulWidth)
    MulA = Builder.CreateZExt(A, MulType);
  if (WidthB < MulWidth)
    MulB = Builder.CreateZExt(B, MulType);
  Value *F = Intrinsic::getDeclaration(I.getModule(),
                                       Intrinsic::umul_with_overflow, MulType);
  CallInst *Call = Builder.CreateCall(F, {MulA, MulB}, "umul");
  IC.Worklist.Add(MulInstr);

  // If there are uses of mul result other than the comparison, we know that
  // they are truncation or binary AND. Change them to use result of
  // mul.with.overflow and adjust properly mask/size.
  if (MulVal->hasNUsesOrMore(2)) {
    Value *Mul = Builder.CreateExtractValue(Call, 0, "umul.value");
    for (auto UI = MulVal->user_begin(), UE = MulVal->user_end(); UI != UE;) {
      User *U = *UI++;
      if (U == &I || U == OtherVal)
        continue;
      if (TruncInst *TI = dyn_cast<TruncInst>(U)) {
        if (TI->getType()->getPrimitiveSizeInBits() == MulWidth)
          IC.replaceInstUsesWith(*TI, Mul);
        else
          TI->setOperand(0, Mul);
      } else if (BinaryOperator *BO = dyn_cast<BinaryOperator>(U)) {
        assert(BO->getOpcode() == Instruction::And);
        // Replace (mul & mask) --> zext (mul.with.overflow & short_mask)
        ConstantInt *CI = cast<ConstantInt>(BO->getOperand(1));
        APInt ShortMask = CI->getValue().trunc(MulWidth);
        Value *ShortAnd = Builder.CreateAnd(Mul, ShortMask);
        Instruction *Zext =
            cast<Instruction>(Builder.CreateZExt(ShortAnd, BO->getType()));
        IC.Worklist.Add(Zext);
        IC.replaceInstUsesWith(*BO, Zext);
      } else {
        llvm_unreachable("Unexpected Binary operation");
      }
      IC.Worklist.Add(cast<Instruction>(U));
    }
  }
  if (isa<Instruction>(OtherVal))
    IC.Worklist.Add(cast<Instruction>(OtherVal));

  // The original icmp gets replaced with the overflow value, maybe inverted
  // depending on predicate.
  bool Inverse = false;
  switch (I.getPredicate()) {
  case ICmpInst::ICMP_NE:
    break;
  case ICmpInst::ICMP_EQ:
    Inverse = true;
    break;
  case ICmpInst::ICMP_UGT:
  case ICmpInst::ICMP_UGE:
    if (I.getOperand(0) == MulVal)
      break;
    Inverse = true;
    break;
  case ICmpInst::ICMP_ULT:
  case ICmpInst::ICMP_ULE:
    if (I.getOperand(1) == MulVal)
      break;
    Inverse = true;
    break;
  default:
    llvm_unreachable("Unexpected predicate");
  }
  if (Inverse) {
    Value *Res = Builder.CreateExtractValue(Call, 1);
    return BinaryOperator::CreateNot(Res);
  }

  return ExtractValueInst::Create(Call, 1);
}

/// When performing a comparison against a constant, it is possible that not all
/// the bits in the LHS are demanded. This helper method computes the mask that
/// IS demanded.
static APInt getDemandedBitsLHSMask(ICmpInst &I, unsigned BitWidth) {
  const APInt *RHS;
  if (!match(I.getOperand(1), m_APInt(RHS)))
    return APInt::getAllOnesValue(BitWidth);

  // If this is a normal comparison, it demands all bits. If it is a sign bit
  // comparison, it only demands the sign bit.
  bool UnusedBit;
  if (isSignBitCheck(I.getPredicate(), *RHS, UnusedBit))
    return APInt::getSignMask(BitWidth);

  switch (I.getPredicate()) {
  // For a UGT comparison, we don't care about any bits that
  // correspond to the trailing ones of the comparand.  The value of these
  // bits doesn't impact the outcome of the comparison, because any value
  // greater than the RHS must differ in a bit higher than these due to carry.
  case ICmpInst::ICMP_UGT:
    return APInt::getBitsSetFrom(BitWidth, RHS->countTrailingOnes());

  // Similarly, for a ULT comparison, we don't care about the trailing zeros.
  // Any value less than the RHS must differ in a higher bit because of carries.
  case ICmpInst::ICMP_ULT:
    return APInt::getBitsSetFrom(BitWidth, RHS->countTrailingZeros());

  default:
    return APInt::getAllOnesValue(BitWidth);
  }
}

/// Check if the order of \p Op0 and \p Op1 as operands in an ICmpInst
/// should be swapped.
/// The decision is based on how many times these two operands are reused
/// as subtract operands and their positions in those instructions.
/// The rationale is that several architectures use the same instruction for
/// both subtract and cmp. Thus, it is better if the order of those operands
/// match.
/// \return true if Op0 and Op1 should be swapped.
static bool swapMayExposeCSEOpportunities(const Value *Op0, const Value *Op1) {
  // Filter out pointer values as those cannot appear directly in subtract.
  // FIXME: we may want to go through inttoptrs or bitcasts.
  if (Op0->getType()->isPointerTy())
    return false;
  // If a subtract already has the same operands as a compare, swapping would be
  // bad. If a subtract has the same operands as a compare but in reverse order,
  // then swapping is good.
  int GoodToSwap = 0;
  for (const User *U : Op0->users()) {
    if (match(U, m_Sub(m_Specific(Op1), m_Specific(Op0))))
      GoodToSwap++;
    else if (match(U, m_Sub(m_Specific(Op0), m_Specific(Op1))))
      GoodToSwap--;
  }
  return GoodToSwap > 0;
}

/// Check that one use is in the same block as the definition and all
/// other uses are in blocks dominated by a given block.
///
/// \param DI Definition
/// \param UI Use
/// \param DB Block that must dominate all uses of \p DI outside
///           the parent block
/// \return true when \p UI is the only use of \p DI in the parent block
/// and all other uses of \p DI are in blocks dominated by \p DB.
///
bool InstCombiner::dominatesAllUses(const Instruction *DI,
                                    const Instruction *UI,
                                    const BasicBlock *DB) const {
  assert(DI && UI && "Instruction not defined\n");
  // Ignore incomplete definitions.
  if (!DI->getParent())
    return false;
  // DI and UI must be in the same block.
  if (DI->getParent() != UI->getParent())
    return false;
  // Protect from self-referencing blocks.
  if (DI->getParent() == DB)
    return false;
  for (const User *U : DI->users()) {
    auto *Usr = cast<Instruction>(U);
    if (Usr != UI && !DT.dominates(DB, Usr->getParent()))
      return false;
  }
  return true;
}

/// Return true when the instruction sequence within a block is select-cmp-br.
static bool isChainSelectCmpBranch(const SelectInst *SI) {
  const BasicBlock *BB = SI->getParent();
  if (!BB)
    return false;
  auto *BI = dyn_cast_or_null<BranchInst>(BB->getTerminator());
  if (!BI || BI->getNumSuccessors() != 2)
    return false;
  auto *IC = dyn_cast<ICmpInst>(BI->getCondition());
  if (!IC || (IC->getOperand(0) != SI && IC->getOperand(1) != SI))
    return false;
  return true;
}

/// True when a select result is replaced by one of its operands
/// in select-icmp sequence. This will eventually result in the elimination
/// of the select.
///
/// \param SI    Select instruction
/// \param Icmp  Compare instruction
/// \param SIOpd Operand that replaces the select
///
/// Notes:
/// - The replacement is global and requires dominator information
/// - The caller is responsible for the actual replacement
///
/// Example:
///
/// entry:
///  %4 = select i1 %3, %C* %0, %C* null
///  %5 = icmp eq %C* %4, null
///  br i1 %5, label %9, label %7
///  ...
///  ; <label>:7                                       ; preds = %entry
///  %8 = getelementptr inbounds %C* %4, i64 0, i32 0
///  ...
///
/// can be transformed to
///
///  %5 = icmp eq %C* %0, null
///  %6 = select i1 %3, i1 %5, i1 true
///  br i1 %6, label %9, label %7
///  ...
///  ; <label>:7                                       ; preds = %entry
///  %8 = getelementptr inbounds %C* %0, i64 0, i32 0  // replace by %0!
///
/// Similar when the first operand of the select is a constant or/and
/// the compare is for not equal rather than equal.
///
/// NOTE: The function is only called when the select and compare constants
/// are equal, the optimization can work only for EQ predicates. This is not a
/// major restriction since a NE compare should be 'normalized' to an equal
/// compare, which usually happens in the combiner and test case
/// select-cmp-br.ll checks for it.
bool InstCombiner::replacedSelectWithOperand(SelectInst *SI,
                                             const ICmpInst *Icmp,
                                             const unsigned SIOpd) {
  assert((SIOpd == 1 || SIOpd == 2) && "Invalid select operand!");
  if (isChainSelectCmpBranch(SI) && Icmp->getPredicate() == ICmpInst::ICMP_EQ) {
    BasicBlock *Succ = SI->getParent()->getTerminator()->getSuccessor(1);
    // The check for the single predecessor is not the best that can be
    // done. But it protects efficiently against cases like when SI's
    // home block has two successors, Succ and Succ1, and Succ1 predecessor
    // of Succ. Then SI can't be replaced by SIOpd because the use that gets
    // replaced can be reached on either path. So the uniqueness check
    // guarantees that the path all uses of SI (outside SI's parent) are on
    // is disjoint from all other paths out of SI. But that information
    // is more expensive to compute, and the trade-off here is in favor
    // of compile-time. It should also be noticed that we check for a single
    // predecessor and not only uniqueness. This to handle the situation when
    // Succ and Succ1 points to the same basic block.
    if (Succ->getSinglePredecessor() && dominatesAllUses(SI, Icmp, Succ)) {
      NumSel++;
      SI->replaceUsesOutsideBlock(SI->getOperand(SIOpd), SI->getParent());
      return true;
    }
  }
  return false;
}

/// Try to fold the comparison based on range information we can get by checking
/// whether bits are known to be zero or one in the inputs.
Instruction *InstCombiner::foldICmpUsingKnownBits(ICmpInst &I) {
  Value *Op0 = I.getOperand(0), *Op1 = I.getOperand(1);
  Type *Ty = Op0->getType();
  ICmpInst::Predicate Pred = I.getPredicate();

  // Get scalar or pointer size.
  unsigned BitWidth = Ty->isIntOrIntVectorTy()
                          ? Ty->getScalarSizeInBits()
                          : DL.getIndexTypeSizeInBits(Ty->getScalarType());

  if (!BitWidth)
    return nullptr;

  KnownBits Op0Known(BitWidth);
  KnownBits Op1Known(BitWidth);

  if (SimplifyDemandedBits(&I, 0,
                           getDemandedBitsLHSMask(I, BitWidth),
                           Op0Known, 0))
    return &I;

  if (SimplifyDemandedBits(&I, 1, APInt::getAllOnesValue(BitWidth),
                           Op1Known, 0))
    return &I;

  // Given the known and unknown bits, compute a range that the LHS could be
  // in.  Compute the Min, Max and RHS values based on the known bits. For the
  // EQ and NE we use unsigned values.
  APInt Op0Min(BitWidth, 0), Op0Max(BitWidth, 0);
  APInt Op1Min(BitWidth, 0), Op1Max(BitWidth, 0);
  if (I.isSigned()) {
    computeSignedMinMaxValuesFromKnownBits(Op0Known, Op0Min, Op0Max);
    computeSignedMinMaxValuesFromKnownBits(Op1Known, Op1Min, Op1Max);
  } else {
    computeUnsignedMinMaxValuesFromKnownBits(Op0Known, Op0Min, Op0Max);
    computeUnsignedMinMaxValuesFromKnownBits(Op1Known, Op1Min, Op1Max);
  }

  // If Min and Max are known to be the same, then SimplifyDemandedBits figured
  // out that the LHS or RHS is a constant. Constant fold this now, so that
  // code below can assume that Min != Max.
  if (!isa<Constant>(Op0) && Op0Min == Op0Max)
    return new ICmpInst(Pred, ConstantExpr::getIntegerValue(Ty, Op0Min), Op1);
  if (!isa<Constant>(Op1) && Op1Min == Op1Max)
    return new ICmpInst(Pred, Op0, ConstantExpr::getIntegerValue(Ty, Op1Min));

  // Based on the range information we know about the LHS, see if we can
  // simplify this comparison.  For example, (x&4) < 8 is always true.
  switch (Pred) {
  default:
    llvm_unreachable("Unknown icmp opcode!");
  case ICmpInst::ICMP_EQ:
  case ICmpInst::ICMP_NE: {
    if (Op0Max.ult(Op1Min) || Op0Min.ugt(Op1Max)) {
      return Pred == CmpInst::ICMP_EQ
                 ? replaceInstUsesWith(I, ConstantInt::getFalse(I.getType()))
                 : replaceInstUsesWith(I, ConstantInt::getTrue(I.getType()));
    }

    // If all bits are known zero except for one, then we know at most one bit
    // is set. If the comparison is against zero, then this is a check to see if
    // *that* bit is set.
    APInt Op0KnownZeroInverted = ~Op0Known.Zero;
    if (Op1Known.isZero()) {
      // If the LHS is an AND with the same constant, look through it.
      Value *LHS = nullptr;
      const APInt *LHSC;
      if (!match(Op0, m_And(m_Value(LHS), m_APInt(LHSC))) ||
          *LHSC != Op0KnownZeroInverted)
        LHS = Op0;

      Value *X;
      if (match(LHS, m_Shl(m_One(), m_Value(X)))) {
        APInt ValToCheck = Op0KnownZeroInverted;
        Type *XTy = X->getType();
        if (ValToCheck.isPowerOf2()) {
          // ((1 << X) & 8) == 0 -> X != 3
          // ((1 << X) & 8) != 0 -> X == 3
          auto *CmpC = ConstantInt::get(XTy, ValToCheck.countTrailingZeros());
          auto NewPred = ICmpInst::getInversePredicate(Pred);
          return new ICmpInst(NewPred, X, CmpC);
        } else if ((++ValToCheck).isPowerOf2()) {
          // ((1 << X) & 7) == 0 -> X >= 3
          // ((1 << X) & 7) != 0 -> X  < 3
          auto *CmpC = ConstantInt::get(XTy, ValToCheck.countTrailingZeros());
          auto NewPred =
              Pred == CmpInst::ICMP_EQ ? CmpInst::ICMP_UGE : CmpInst::ICMP_ULT;
          return new ICmpInst(NewPred, X, CmpC);
        }
      }

      // Check if the LHS is 8 >>u x and the result is a power of 2 like 1.
      const APInt *CI;
      if (Op0KnownZeroInverted.isOneValue() &&
          match(LHS, m_LShr(m_Power2(CI), m_Value(X)))) {
        // ((8 >>u X) & 1) == 0 -> X != 3
        // ((8 >>u X) & 1) != 0 -> X == 3
        unsigned CmpVal = CI->countTrailingZeros();
        auto NewPred = ICmpInst::getInversePredicate(Pred);
        return new ICmpInst(NewPred, X, ConstantInt::get(X->getType(), CmpVal));
      }
    }
    break;
  }
  case ICmpInst::ICMP_ULT: {
    if (Op0Max.ult(Op1Min)) // A <u B -> true if max(A) < min(B)
      return replaceInstUsesWith(I, ConstantInt::getTrue(I.getType()));
    if (Op0Min.uge(Op1Max)) // A <u B -> false if min(A) >= max(B)
      return replaceInstUsesWith(I, ConstantInt::getFalse(I.getType()));
    if (Op1Min == Op0Max) // A <u B -> A != B if max(A) == min(B)
      return new ICmpInst(ICmpInst::ICMP_NE, Op0, Op1);

    const APInt *CmpC;
    if (match(Op1, m_APInt(CmpC))) {
      // A <u C -> A == C-1 if min(A)+1 == C
      if (*CmpC == Op0Min + 1)
        return new ICmpInst(ICmpInst::ICMP_EQ, Op0,
                            ConstantInt::get(Op1->getType(), *CmpC - 1));
      // X <u C --> X == 0, if the number of zero bits in the bottom of X
      // exceeds the log2 of C.
      if (Op0Known.countMinTrailingZeros() >= CmpC->ceilLogBase2())
        return new ICmpInst(ICmpInst::ICMP_EQ, Op0,
                            Constant::getNullValue(Op1->getType()));
    }
    break;
  }
  case ICmpInst::ICMP_UGT: {
    if (Op0Min.ugt(Op1Max)) // A >u B -> true if min(A) > max(B)
      return replaceInstUsesWith(I, ConstantInt::getTrue(I.getType()));
    if (Op0Max.ule(Op1Min)) // A >u B -> false if max(A) <= max(B)
      return replaceInstUsesWith(I, ConstantInt::getFalse(I.getType()));
    if (Op1Max == Op0Min) // A >u B -> A != B if min(A) == max(B)
      return new ICmpInst(ICmpInst::ICMP_NE, Op0, Op1);

    const APInt *CmpC;
    if (match(Op1, m_APInt(CmpC))) {
      // A >u C -> A == C+1 if max(a)-1 == C
      if (*CmpC == Op0Max - 1)
        return new ICmpInst(ICmpInst::ICMP_EQ, Op0,
                            ConstantInt::get(Op1->getType(), *CmpC + 1));
      // X >u C --> X != 0, if the number of zero bits in the bottom of X
      // exceeds the log2 of C.
      if (Op0Known.countMinTrailingZeros() >= CmpC->getActiveBits())
        return new ICmpInst(ICmpInst::ICMP_NE, Op0,
                            Constant::getNullValue(Op1->getType()));
    }
    break;
  }
  case ICmpInst::ICMP_SLT: {
    if (Op0Max.slt(Op1Min)) // A <s B -> true if max(A) < min(C)
      return replaceInstUsesWith(I, ConstantInt::getTrue(I.getType()));
    if (Op0Min.sge(Op1Max)) // A <s B -> false if min(A) >= max(C)
      return replaceInstUsesWith(I, ConstantInt::getFalse(I.getType()));
    if (Op1Min == Op0Max) // A <s B -> A != B if max(A) == min(B)
      return new ICmpInst(ICmpInst::ICMP_NE, Op0, Op1);
    const APInt *CmpC;
    if (match(Op1, m_APInt(CmpC))) {
      if (*CmpC == Op0Min + 1) // A <s C -> A == C-1 if min(A)+1 == C
        return new ICmpInst(ICmpInst::ICMP_EQ, Op0,
                            ConstantInt::get(Op1->getType(), *CmpC - 1));
    }
    break;
  }
  case ICmpInst::ICMP_SGT: {
    if (Op0Min.sgt(Op1Max)) // A >s B -> true if min(A) > max(B)
      return replaceInstUsesWith(I, ConstantInt::getTrue(I.getType()));
    if (Op0Max.sle(Op1Min)) // A >s B -> false if max(A) <= min(B)
      return replaceInstUsesWith(I, ConstantInt::getFalse(I.getType()));
    if (Op1Max == Op0Min) // A >s B -> A != B if min(A) == max(B)
      return new ICmpInst(ICmpInst::ICMP_NE, Op0, Op1);
    const APInt *CmpC;
    if (match(Op1, m_APInt(CmpC))) {
      if (*CmpC == Op0Max - 1) // A >s C -> A == C+1 if max(A)-1 == C
        return new ICmpInst(ICmpInst::ICMP_EQ, Op0,
                            ConstantInt::get(Op1->getType(), *CmpC + 1));
    }
    break;
  }
  case ICmpInst::ICMP_SGE:
    assert(!isa<ConstantInt>(Op1) && "ICMP_SGE with ConstantInt not folded!");
    if (Op0Min.sge(Op1Max)) // A >=s B -> true if min(A) >= max(B)
      return replaceInstUsesWith(I, ConstantInt::getTrue(I.getType()));
    if (Op0Max.slt(Op1Min)) // A >=s B -> false if max(A) < min(B)
      return replaceInstUsesWith(I, ConstantInt::getFalse(I.getType()));
    if (Op1Min == Op0Max) // A >=s B -> A == B if max(A) == min(B)
      return new ICmpInst(ICmpInst::ICMP_EQ, Op0, Op1);
    break;
  case ICmpInst::ICMP_SLE:
    assert(!isa<ConstantInt>(Op1) && "ICMP_SLE with ConstantInt not folded!");
    if (Op0Max.sle(Op1Min)) // A <=s B -> true if max(A) <= min(B)
      return replaceInstUsesWith(I, ConstantInt::getTrue(I.getType()));
    if (Op0Min.sgt(Op1Max)) // A <=s B -> false if min(A) > max(B)
      return replaceInstUsesWith(I, ConstantInt::getFalse(I.getType()));
    if (Op1Max == Op0Min) // A <=s B -> A == B if min(A) == max(B)
      return new ICmpInst(ICmpInst::ICMP_EQ, Op0, Op1);
    break;
  case ICmpInst::ICMP_UGE:
    assert(!isa<ConstantInt>(Op1) && "ICMP_UGE with ConstantInt not folded!");
    if (Op0Min.uge(Op1Max)) // A >=u B -> true if min(A) >= max(B)
      return replaceInstUsesWith(I, ConstantInt::getTrue(I.getType()));
    if (Op0Max.ult(Op1Min)) // A >=u B -> false if max(A) < min(B)
      return replaceInstUsesWith(I, ConstantInt::getFalse(I.getType()));
    if (Op1Min == Op0Max) // A >=u B -> A == B if max(A) == min(B)
      return new ICmpInst(ICmpInst::ICMP_EQ, Op0, Op1);
    break;
  case ICmpInst::ICMP_ULE:
    assert(!isa<ConstantInt>(Op1) && "ICMP_ULE with ConstantInt not folded!");
    if (Op0Max.ule(Op1Min)) // A <=u B -> true if max(A) <= min(B)
      return replaceInstUsesWith(I, ConstantInt::getTrue(I.getType()));
    if (Op0Min.ugt(Op1Max)) // A <=u B -> false if min(A) > max(B)
      return replaceInstUsesWith(I, ConstantInt::getFalse(I.getType()));
    if (Op1Max == Op0Min) // A <=u B -> A == B if min(A) == max(B)
      return new ICmpInst(ICmpInst::ICMP_EQ, Op0, Op1);
    break;
  }

  // Turn a signed comparison into an unsigned one if both operands are known to
  // have the same sign.
  if (I.isSigned() &&
      ((Op0Known.Zero.isNegative() && Op1Known.Zero.isNegative()) ||
       (Op0Known.One.isNegative() && Op1Known.One.isNegative())))
    return new ICmpInst(I.getUnsignedPredicate(), Op0, Op1);

  return nullptr;
}

/// If we have an icmp le or icmp ge instruction with a constant operand, turn
/// it into the appropriate icmp lt or icmp gt instruction. This transform
/// allows them to be folded in visitICmpInst.
static ICmpInst *canonicalizeCmpWithConstant(ICmpInst &I) {
  ICmpInst::Predicate Pred = I.getPredicate();
  if (Pred != ICmpInst::ICMP_SLE && Pred != ICmpInst::ICMP_SGE &&
      Pred != ICmpInst::ICMP_ULE && Pred != ICmpInst::ICMP_UGE)
    return nullptr;

  Value *Op0 = I.getOperand(0);
  Value *Op1 = I.getOperand(1);
  auto *Op1C = dyn_cast<Constant>(Op1);
  if (!Op1C)
    return nullptr;

  // Check if the constant operand can be safely incremented/decremented without
  // overflowing/underflowing. For scalars, SimplifyICmpInst has already handled
  // the edge cases for us, so we just assert on them. For vectors, we must
  // handle the edge cases.
  Type *Op1Type = Op1->getType();
  bool IsSigned = I.isSigned();
  bool IsLE = (Pred == ICmpInst::ICMP_SLE || Pred == ICmpInst::ICMP_ULE);
  auto *CI = dyn_cast<ConstantInt>(Op1C);
  if (CI) {
    // A <= MAX -> TRUE ; A >= MIN -> TRUE
    assert(IsLE ? !CI->isMaxValue(IsSigned) : !CI->isMinValue(IsSigned));
  } else if (Op1Type->isVectorTy()) {
    // TODO? If the edge cases for vectors were guaranteed to be handled as they
    // are for scalar, we could remove the min/max checks. However, to do that,
    // we would have to use insertelement/shufflevector to replace edge values.
    unsigned NumElts = Op1Type->getVectorNumElements();
    for (unsigned i = 0; i != NumElts; ++i) {
      Constant *Elt = Op1C->getAggregateElement(i);
      if (!Elt)
        return nullptr;

      if (isa<UndefValue>(Elt))
        continue;

      // Bail out if we can't determine if this constant is min/max or if we
      // know that this constant is min/max.
      auto *CI = dyn_cast<ConstantInt>(Elt);
      if (!CI || (IsLE ? CI->isMaxValue(IsSigned) : CI->isMinValue(IsSigned)))
        return nullptr;
    }
  } else {
    // ConstantExpr?
    return nullptr;
  }

  // Increment or decrement the constant and set the new comparison predicate:
  // ULE -> ULT ; UGE -> UGT ; SLE -> SLT ; SGE -> SGT
  Constant *OneOrNegOne = ConstantInt::get(Op1Type, IsLE ? 1 : -1, true);
  CmpInst::Predicate NewPred = IsLE ? ICmpInst::ICMP_ULT: ICmpInst::ICMP_UGT;
  NewPred = IsSigned ? ICmpInst::getSignedPredicate(NewPred) : NewPred;
  return new ICmpInst(NewPred, Op0, ConstantExpr::getAdd(Op1C, OneOrNegOne));
}

/// Integer compare with boolean values can always be turned into bitwise ops.
static Instruction *canonicalizeICmpBool(ICmpInst &I,
                                         InstCombiner::BuilderTy &Builder) {
  Value *A = I.getOperand(0), *B = I.getOperand(1);
  assert(A->getType()->isIntOrIntVectorTy(1) && "Bools only");

  // A boolean compared to true/false can be simplified to Op0/true/false in
  // 14 out of the 20 (10 predicates * 2 constants) possible combinations.
  // Cases not handled by InstSimplify are always 'not' of Op0.
  if (match(B, m_Zero())) {
    switch (I.getPredicate()) {
      case CmpInst::ICMP_EQ:  // A ==   0 -> !A
      case CmpInst::ICMP_ULE: // A <=u  0 -> !A
      case CmpInst::ICMP_SGE: // A >=s  0 -> !A
        return BinaryOperator::CreateNot(A);
      default:
        llvm_unreachable("ICmp i1 X, C not simplified as expected.");
    }
  } else if (match(B, m_One())) {
    switch (I.getPredicate()) {
      case CmpInst::ICMP_NE:  // A !=  1 -> !A
      case CmpInst::ICMP_ULT: // A <u  1 -> !A
      case CmpInst::ICMP_SGT: // A >s -1 -> !A
        return BinaryOperator::CreateNot(A);
      default:
        llvm_unreachable("ICmp i1 X, C not simplified as expected.");
    }
  }

  switch (I.getPredicate()) {
  default:
    llvm_unreachable("Invalid icmp instruction!");
  case ICmpInst::ICMP_EQ:
    // icmp eq i1 A, B -> ~(A ^ B)
    return BinaryOperator::CreateNot(Builder.CreateXor(A, B));

  case ICmpInst::ICMP_NE:
    // icmp ne i1 A, B -> A ^ B
    return BinaryOperator::CreateXor(A, B);

  case ICmpInst::ICMP_UGT:
    // icmp ugt -> icmp ult
    std::swap(A, B);
    LLVM_FALLTHROUGH;
  case ICmpInst::ICMP_ULT:
    // icmp ult i1 A, B -> ~A & B
    return BinaryOperator::CreateAnd(Builder.CreateNot(A), B);

  case ICmpInst::ICMP_SGT:
    // icmp sgt -> icmp slt
    std::swap(A, B);
    LLVM_FALLTHROUGH;
  case ICmpInst::ICMP_SLT:
    // icmp slt i1 A, B -> A & ~B
    return BinaryOperator::CreateAnd(Builder.CreateNot(B), A);

  case ICmpInst::ICMP_UGE:
    // icmp uge -> icmp ule
    std::swap(A, B);
    LLVM_FALLTHROUGH;
  case ICmpInst::ICMP_ULE:
    // icmp ule i1 A, B -> ~A | B
    return BinaryOperator::CreateOr(Builder.CreateNot(A), B);

  case ICmpInst::ICMP_SGE:
    // icmp sge -> icmp sle
    std::swap(A, B);
    LLVM_FALLTHROUGH;
  case ICmpInst::ICMP_SLE:
    // icmp sle i1 A, B -> A | ~B
    return BinaryOperator::CreateOr(Builder.CreateNot(B), A);
  }
}

// Transform pattern like:
//   (1 << Y) u<= X  or  ~(-1 << Y) u<  X  or  ((1 << Y)+(-1)) u<  X
//   (1 << Y) u>  X  or  ~(-1 << Y) u>= X  or  ((1 << Y)+(-1)) u>= X
// Into:
//   (X l>> Y) != 0
//   (X l>> Y) == 0
static Instruction *foldICmpWithHighBitMask(ICmpInst &Cmp,
                                            InstCombiner::BuilderTy &Builder) {
  ICmpInst::Predicate Pred, NewPred;
  Value *X, *Y;
  if (match(&Cmp,
            m_c_ICmp(Pred, m_OneUse(m_Shl(m_One(), m_Value(Y))), m_Value(X)))) {
    // We want X to be the icmp's second operand, so swap predicate if it isn't.
    if (Cmp.getOperand(0) == X)
      Pred = Cmp.getSwappedPredicate();

    switch (Pred) {
    case ICmpInst::ICMP_ULE:
      NewPred = ICmpInst::ICMP_NE;
      break;
    case ICmpInst::ICMP_UGT:
      NewPred = ICmpInst::ICMP_EQ;
      break;
    default:
      return nullptr;
    }
  } else if (match(&Cmp, m_c_ICmp(Pred,
                                  m_OneUse(m_CombineOr(
                                      m_Not(m_Shl(m_AllOnes(), m_Value(Y))),
                                      m_Add(m_Shl(m_One(), m_Value(Y)),
                                            m_AllOnes()))),
                                  m_Value(X)))) {
    // The variant with 'add' is not canonical, (the variant with 'not' is)
    // we only get it because it has extra uses, and can't be canonicalized,

    // We want X to be the icmp's second operand, so swap predicate if it isn't.
    if (Cmp.getOperand(0) == X)
      Pred = Cmp.getSwappedPredicate();

    switch (Pred) {
    case ICmpInst::ICMP_ULT:
      NewPred = ICmpInst::ICMP_NE;
      break;
    case ICmpInst::ICMP_UGE:
      NewPred = ICmpInst::ICMP_EQ;
      break;
    default:
      return nullptr;
    }
  } else
    return nullptr;

  Value *NewX = Builder.CreateLShr(X, Y, X->getName() + ".highbits");
  Constant *Zero = Constant::getNullValue(NewX->getType());
  return CmpInst::Create(Instruction::ICmp, NewPred, NewX, Zero);
}

static Instruction *foldVectorCmp(CmpInst &Cmp,
                                  InstCombiner::BuilderTy &Builder) {
  // If both arguments of the cmp are shuffles that use the same mask and
  // shuffle within a single vector, move the shuffle after the cmp.
  Value *LHS = Cmp.getOperand(0), *RHS = Cmp.getOperand(1);
  Value *V1, *V2;
  Constant *M;
  if (match(LHS, m_ShuffleVector(m_Value(V1), m_Undef(), m_Constant(M))) &&
      match(RHS, m_ShuffleVector(m_Value(V2), m_Undef(), m_Specific(M))) &&
      V1->getType() == V2->getType() &&
      (LHS->hasOneUse() || RHS->hasOneUse())) {
    // cmp (shuffle V1, M), (shuffle V2, M) --> shuffle (cmp V1, V2), M
    CmpInst::Predicate P = Cmp.getPredicate();
    Value *NewCmp = isa<ICmpInst>(Cmp) ? Builder.CreateICmp(P, V1, V2)
                                       : Builder.CreateFCmp(P, V1, V2);
    return new ShuffleVectorInst(NewCmp, UndefValue::get(NewCmp->getType()), M);
  }
  return nullptr;
}

Instruction *InstCombiner::visitICmpInst(ICmpInst &I) {
  bool Changed = false;
  Value *Op0 = I.getOperand(0), *Op1 = I.getOperand(1);
  unsigned Op0Cplxity = getComplexity(Op0);
  unsigned Op1Cplxity = getComplexity(Op1);

  /// Orders the operands of the compare so that they are listed from most
  /// complex to least complex.  This puts constants before unary operators,
  /// before binary operators.
  if (Op0Cplxity < Op1Cplxity ||
      (Op0Cplxity == Op1Cplxity && swapMayExposeCSEOpportunities(Op0, Op1))) {
    I.swapOperands();
    std::swap(Op0, Op1);
    Changed = true;
  }

  if (Value *V = SimplifyICmpInst(I.getPredicate(), Op0, Op1,
                                  SQ.getWithInstruction(&I)))
    return replaceInstUsesWith(I, V);

  // Comparing -val or val with non-zero is the same as just comparing val
  // ie, abs(val) != 0 -> val != 0
  if (I.getPredicate() == ICmpInst::ICMP_NE && match(Op1, m_Zero())) {
    Value *Cond, *SelectTrue, *SelectFalse;
    if (match(Op0, m_Select(m_Value(Cond), m_Value(SelectTrue),
                            m_Value(SelectFalse)))) {
      if (Value *V = dyn_castNegVal(SelectTrue)) {
        if (V == SelectFalse)
          return CmpInst::Create(Instruction::ICmp, I.getPredicate(), V, Op1);
      }
      else if (Value *V = dyn_castNegVal(SelectFalse)) {
        if (V == SelectTrue)
          return CmpInst::Create(Instruction::ICmp, I.getPredicate(), V, Op1);
      }
    }
  }

  if (Op0->getType()->isIntOrIntVectorTy(1))
    if (Instruction *Res = canonicalizeICmpBool(I, Builder))
      return Res;

  if (ICmpInst *NewICmp = canonicalizeCmpWithConstant(I))
    return NewICmp;

  if (Instruction *Res = foldICmpWithConstant(I))
    return Res;

  if (Instruction *Res = foldICmpWithDominatingICmp(I))
    return Res;

  if (Instruction *Res = foldICmpUsingKnownBits(I))
    return Res;

  // Test if the ICmpInst instruction is used exclusively by a select as
  // part of a minimum or maximum operation. If so, refrain from doing
  // any other folding. This helps out other analyses which understand
  // non-obfuscated minimum and maximum idioms, such as ScalarEvolution
  // and CodeGen. And in this case, at least one of the comparison
  // operands has at least one user besides the compare (the select),
  // which would often largely negate the benefit of folding anyway.
  //
  // Do the same for the other patterns recognized by matchSelectPattern.
  if (I.hasOneUse())
    if (SelectInst *SI = dyn_cast<SelectInst>(I.user_back())) {
      Value *A, *B;
      SelectPatternResult SPR = matchSelectPattern(SI, A, B);
      if (SPR.Flavor != SPF_UNKNOWN)
        return nullptr;
    }

  // Do this after checking for min/max to prevent infinite looping.
  if (Instruction *Res = foldICmpWithZero(I))
    return Res;

  // FIXME: We only do this after checking for min/max to prevent infinite
  // looping caused by a reverse canonicalization of these patterns for min/max.
  // FIXME: The organization of folds is a mess. These would naturally go into
  // canonicalizeCmpWithConstant(), but we can't move all of the above folds
  // down here after the min/max restriction.
  ICmpInst::Predicate Pred = I.getPredicate();
  const APInt *C;
  if (match(Op1, m_APInt(C))) {
    // For i32: x >u 2147483647 -> x <s 0  -> true if sign bit set
    if (Pred == ICmpInst::ICMP_UGT && C->isMaxSignedValue()) {
      Constant *Zero = Constant::getNullValue(Op0->getType());
      return new ICmpInst(ICmpInst::ICMP_SLT, Op0, Zero);
    }

    // For i32: x <u 2147483648 -> x >s -1  -> true if sign bit clear
    if (Pred == ICmpInst::ICMP_ULT && C->isMinSignedValue()) {
      Constant *AllOnes = Constant::getAllOnesValue(Op0->getType());
      return new ICmpInst(ICmpInst::ICMP_SGT, Op0, AllOnes);
    }
  }

  if (Instruction *Res = foldICmpInstWithConstant(I))
    return Res;

  if (Instruction *Res = foldICmpInstWithConstantNotInt(I))
    return Res;

  // If we can optimize a 'icmp GEP, P' or 'icmp P, GEP', do so now.
  if (GEPOperator *GEP = dyn_cast<GEPOperator>(Op0))
    if (Instruction *NI = foldGEPICmp(GEP, Op1, I.getPredicate(), I))
      return NI;
  if (GEPOperator *GEP = dyn_cast<GEPOperator>(Op1))
    if (Instruction *NI = foldGEPICmp(GEP, Op0,
                           ICmpInst::getSwappedPredicate(I.getPredicate()), I))
      return NI;

  // Try to optimize equality comparisons against alloca-based pointers.
  if (Op0->getType()->isPointerTy() && I.isEquality()) {
    assert(Op1->getType()->isPointerTy() && "Comparing pointer with non-pointer?");
    if (auto *Alloca = dyn_cast<AllocaInst>(GetUnderlyingObject(Op0, DL)))
      if (Instruction *New = foldAllocaCmp(I, Alloca, Op1))
        return New;
    if (auto *Alloca = dyn_cast<AllocaInst>(GetUnderlyingObject(Op1, DL)))
      if (Instruction *New = foldAllocaCmp(I, Alloca, Op0))
        return New;
  }

  // Zero-equality and sign-bit checks are preserved through sitofp + bitcast.
  Value *X;
  if (match(Op0, m_BitCast(m_SIToFP(m_Value(X))))) {
    // icmp  eq (bitcast (sitofp X)), 0 --> icmp  eq X, 0
    // icmp  ne (bitcast (sitofp X)), 0 --> icmp  ne X, 0
    // icmp slt (bitcast (sitofp X)), 0 --> icmp slt X, 0
    // icmp sgt (bitcast (sitofp X)), 0 --> icmp sgt X, 0
    if ((Pred == ICmpInst::ICMP_EQ || Pred == ICmpInst::ICMP_SLT ||
         Pred == ICmpInst::ICMP_NE || Pred == ICmpInst::ICMP_SGT) &&
        match(Op1, m_Zero()))
      return new ICmpInst(Pred, X, ConstantInt::getNullValue(X->getType()));

    // icmp slt (bitcast (sitofp X)), 1 --> icmp slt X, 1
    if (Pred == ICmpInst::ICMP_SLT && match(Op1, m_One()))
      return new ICmpInst(Pred, X, ConstantInt::get(X->getType(), 1));

    // icmp sgt (bitcast (sitofp X)), -1 --> icmp sgt X, -1
    if (Pred == ICmpInst::ICMP_SGT && match(Op1, m_AllOnes()))
      return new ICmpInst(Pred, X, ConstantInt::getAllOnesValue(X->getType()));
  }

  // Zero-equality checks are preserved through unsigned floating-point casts:
  // icmp eq (bitcast (uitofp X)), 0 --> icmp eq X, 0
  // icmp ne (bitcast (uitofp X)), 0 --> icmp ne X, 0
  if (match(Op0, m_BitCast(m_UIToFP(m_Value(X)))))
    if (I.isEquality() && match(Op1, m_Zero()))
      return new ICmpInst(Pred, X, ConstantInt::getNullValue(X->getType()));

  // Test to see if the operands of the icmp are casted versions of other
  // values.  If the ptr->ptr cast can be stripped off both arguments, we do so
  // now.
  if (BitCastInst *CI = dyn_cast<BitCastInst>(Op0)) {
    if (Op0->getType()->isPointerTy() &&
        (isa<Constant>(Op1) || isa<BitCastInst>(Op1))) {
      // We keep moving the cast from the left operand over to the right
      // operand, where it can often be eliminated completely.
      Op0 = CI->getOperand(0);

      // If operand #1 is a bitcast instruction, it must also be a ptr->ptr cast
      // so eliminate it as well.
      if (BitCastInst *CI2 = dyn_cast<BitCastInst>(Op1))
        Op1 = CI2->getOperand(0);

      // If Op1 is a constant, we can fold the cast into the constant.
      if (Op0->getType() != Op1->getType()) {
        if (Constant *Op1C = dyn_cast<Constant>(Op1)) {
          Op1 = ConstantExpr::getBitCast(Op1C, Op0->getType());
        } else {
          // Otherwise, cast the RHS right before the icmp
          Op1 = Builder.CreateBitCast(Op1, Op0->getType());
        }
      }
      return new ICmpInst(I.getPredicate(), Op0, Op1);
    }
  }

  if (isa<CastInst>(Op0)) {
    // Handle the special case of: icmp (cast bool to X), <cst>
    // This comes up when you have code like
    //   int X = A < B;
    //   if (X) ...
    // For generality, we handle any zero-extension of any operand comparison
    // with a constant or another cast from the same type.
    if (isa<Constant>(Op1) || isa<CastInst>(Op1))
      if (Instruction *R = foldICmpWithCastAndCast(I))
        return R;
  }

  if (Instruction *Res = foldICmpBinOp(I))
    return Res;

  if (Instruction *Res = foldICmpWithMinMax(I))
    return Res;

  {
    Value *A, *B;
    // Transform (A & ~B) == 0 --> (A & B) != 0
    // and       (A & ~B) != 0 --> (A & B) == 0
    // if A is a power of 2.
    if (match(Op0, m_And(m_Value(A), m_Not(m_Value(B)))) &&
        match(Op1, m_Zero()) &&
        isKnownToBeAPowerOfTwo(A, false, 0, &I) && I.isEquality())
      return new ICmpInst(I.getInversePredicate(), Builder.CreateAnd(A, B),
                          Op1);

    // ~X < ~Y --> Y < X
    // ~X < C -->  X > ~C
    if (match(Op0, m_Not(m_Value(A)))) {
      if (match(Op1, m_Not(m_Value(B))))
        return new ICmpInst(I.getPredicate(), B, A);

      const APInt *C;
      if (match(Op1, m_APInt(C)))
        return new ICmpInst(I.getSwappedPredicate(), A,
                            ConstantInt::get(Op1->getType(), ~(*C)));
    }

    Instruction *AddI = nullptr;
    if (match(&I, m_UAddWithOverflow(m_Value(A), m_Value(B),
                                     m_Instruction(AddI))) &&
        isa<IntegerType>(A->getType())) {
      Value *Result;
      Constant *Overflow;
      if (OptimizeOverflowCheck(OCF_UNSIGNED_ADD, A, B, *AddI, Result,
                                Overflow)) {
        replaceInstUsesWith(*AddI, Result);
        return replaceInstUsesWith(I, Overflow);
      }
    }

    // (zext a) * (zext b)  --> llvm.umul.with.overflow.
    if (match(Op0, m_Mul(m_ZExt(m_Value(A)), m_ZExt(m_Value(B))))) {
      if (Instruction *R = processUMulZExtIdiom(I, Op0, Op1, *this))
        return R;
    }
    if (match(Op1, m_Mul(m_ZExt(m_Value(A)), m_ZExt(m_Value(B))))) {
      if (Instruction *R = processUMulZExtIdiom(I, Op1, Op0, *this))
        return R;
    }
  }

  if (Instruction *Res = foldICmpEquality(I))
    return Res;

  // The 'cmpxchg' instruction returns an aggregate containing the old value and
  // an i1 which indicates whether or not we successfully did the swap.
  //
  // Replace comparisons between the old value and the expected value with the
  // indicator that 'cmpxchg' returns.
  //
  // N.B.  This transform is only valid when the 'cmpxchg' is not permitted to
  // spuriously fail.  In those cases, the old value may equal the expected
  // value but it is possible for the swap to not occur.
  if (I.getPredicate() == ICmpInst::ICMP_EQ)
    if (auto *EVI = dyn_cast<ExtractValueInst>(Op0))
      if (auto *ACXI = dyn_cast<AtomicCmpXchgInst>(EVI->getAggregateOperand()))
        if (EVI->getIndices()[0] == 0 && ACXI->getCompareOperand() == Op1 &&
            !ACXI->isWeak())
          return ExtractValueInst::Create(ACXI, 1);

  {
    Value *X;
    const APInt *C;
    // icmp X+Cst, X
    if (match(Op0, m_Add(m_Value(X), m_APInt(C))) && Op1 == X)
      return foldICmpAddOpConst(X, *C, I.getPredicate());

    // icmp X, X+Cst
    if (match(Op1, m_Add(m_Value(X), m_APInt(C))) && Op0 == X)
      return foldICmpAddOpConst(X, *C, I.getSwappedPredicate());
  }

  if (Instruction *Res = foldICmpWithHighBitMask(I, Builder))
    return Res;

  if (I.getType()->isVectorTy())
    if (Instruction *Res = foldVectorCmp(I, Builder))
      return Res;

  return Changed ? &I : nullptr;
}

/// Fold fcmp ([us]itofp x, cst) if possible.
Instruction *InstCombiner::foldFCmpIntToFPConst(FCmpInst &I, Instruction *LHSI,
                                                Constant *RHSC) {
  if (!isa<ConstantFP>(RHSC)) return nullptr;
  const APFloat &RHS = cast<ConstantFP>(RHSC)->getValueAPF();

  // Get the width of the mantissa.  We don't want to hack on conversions that
  // might lose information from the integer, e.g. "i64 -> float"
  int MantissaWidth = LHSI->getType()->getFPMantissaWidth();
  if (MantissaWidth == -1) return nullptr;  // Unknown.

  IntegerType *IntTy = cast<IntegerType>(LHSI->getOperand(0)->getType());

  bool LHSUnsigned = isa<UIToFPInst>(LHSI);

  if (I.isEquality()) {
    FCmpInst::Predicate P = I.getPredicate();
    bool IsExact = false;
    APSInt RHSCvt(IntTy->getBitWidth(), LHSUnsigned);
    RHS.convertToInteger(RHSCvt, APFloat::rmNearestTiesToEven, &IsExact);

    // If the floating point constant isn't an integer value, we know if we will
    // ever compare equal / not equal to it.
    if (!IsExact) {
      // TODO: Can never be -0.0 and other non-representable values
      APFloat RHSRoundInt(RHS);
      RHSRoundInt.roundToIntegral(APFloat::rmNearestTiesToEven);
      if (RHS.compare(RHSRoundInt) != APFloat::cmpEqual) {
        if (P == FCmpInst::FCMP_OEQ || P == FCmpInst::FCMP_UEQ)
          return replaceInstUsesWith(I, Builder.getFalse());

        assert(P == FCmpInst::FCMP_ONE || P == FCmpInst::FCMP_UNE);
        return replaceInstUsesWith(I, Builder.getTrue());
      }
    }

    // TODO: If the constant is exactly representable, is it always OK to do
    // equality compares as integer?
  }

  // Check to see that the input is converted from an integer type that is small
  // enough that preserves all bits.  TODO: check here for "known" sign bits.
  // This would allow us to handle (fptosi (x >>s 62) to float) if x is i64 f.e.
  unsigned InputSize = IntTy->getScalarSizeInBits();

  // Following test does NOT adjust InputSize downwards for signed inputs,
  // because the most negative value still requires all the mantissa bits
  // to distinguish it from one less than that value.
  if ((int)InputSize > MantissaWidth) {
    // Conversion would lose accuracy. Check if loss can impact comparison.
    int Exp = ilogb(RHS);
    if (Exp == APFloat::IEK_Inf) {
      int MaxExponent = ilogb(APFloat::getLargest(RHS.getSemantics()));
      if (MaxExponent < (int)InputSize - !LHSUnsigned)
        // Conversion could create infinity.
        return nullptr;
    } else {
      // Note that if RHS is zero or NaN, then Exp is negative
      // and first condition is trivially false.
      if (MantissaWidth <= Exp && Exp <= (int)InputSize - !LHSUnsigned)
        // Conversion could affect comparison.
        return nullptr;
    }
  }

  // Otherwise, we can potentially simplify the comparison.  We know that it
  // will always come through as an integer value and we know the constant is
  // not a NAN (it would have been previously simplified).
  assert(!RHS.isNaN() && "NaN comparison not already folded!");

  ICmpInst::Predicate Pred;
  switch (I.getPredicate()) {
  default: llvm_unreachable("Unexpected predicate!");
  case FCmpInst::FCMP_UEQ:
  case FCmpInst::FCMP_OEQ:
    Pred = ICmpInst::ICMP_EQ;
    break;
  case FCmpInst::FCMP_UGT:
  case FCmpInst::FCMP_OGT:
    Pred = LHSUnsigned ? ICmpInst::ICMP_UGT : ICmpInst::ICMP_SGT;
    break;
  case FCmpInst::FCMP_UGE:
  case FCmpInst::FCMP_OGE:
    Pred = LHSUnsigned ? ICmpInst::ICMP_UGE : ICmpInst::ICMP_SGE;
    break;
  case FCmpInst::FCMP_ULT:
  case FCmpInst::FCMP_OLT:
    Pred = LHSUnsigned ? ICmpInst::ICMP_ULT : ICmpInst::ICMP_SLT;
    break;
  case FCmpInst::FCMP_ULE:
  case FCmpInst::FCMP_OLE:
    Pred = LHSUnsigned ? ICmpInst::ICMP_ULE : ICmpInst::ICMP_SLE;
    break;
  case FCmpInst::FCMP_UNE:
  case FCmpInst::FCMP_ONE:
    Pred = ICmpInst::ICMP_NE;
    break;
  case FCmpInst::FCMP_ORD:
    return replaceInstUsesWith(I, Builder.getTrue());
  case FCmpInst::FCMP_UNO:
    return replaceInstUsesWith(I, Builder.getFalse());
  }

  // Now we know that the APFloat is a normal number, zero or inf.

  // See if the FP constant is too large for the integer.  For example,
  // comparing an i8 to 300.0.
  unsigned IntWidth = IntTy->getScalarSizeInBits();

  if (!LHSUnsigned) {
    // If the RHS value is > SignedMax, fold the comparison.  This handles +INF
    // and large values.
    APFloat SMax(RHS.getSemantics());
    SMax.convertFromAPInt(APInt::getSignedMaxValue(IntWidth), true,
                          APFloat::rmNearestTiesToEven);
    if (SMax.compare(RHS) == APFloat::cmpLessThan) {  // smax < 13123.0
      if (Pred == ICmpInst::ICMP_NE  || Pred == ICmpInst::ICMP_SLT ||
          Pred == ICmpInst::ICMP_SLE)
        return replaceInstUsesWith(I, Builder.getTrue());
      return replaceInstUsesWith(I, Builder.getFalse());
    }
  } else {
    // If the RHS value is > UnsignedMax, fold the comparison. This handles
    // +INF and large values.
    APFloat UMax(RHS.getSemantics());
    UMax.convertFromAPInt(APInt::getMaxValue(IntWidth), false,
                          APFloat::rmNearestTiesToEven);
    if (UMax.compare(RHS) == APFloat::cmpLessThan) {  // umax < 13123.0
      if (Pred == ICmpInst::ICMP_NE  || Pred == ICmpInst::ICMP_ULT ||
          Pred == ICmpInst::ICMP_ULE)
        return replaceInstUsesWith(I, Builder.getTrue());
      return replaceInstUsesWith(I, Builder.getFalse());
    }
  }

  if (!LHSUnsigned) {
    // See if the RHS value is < SignedMin.
    APFloat SMin(RHS.getSemantics());
    SMin.convertFromAPInt(APInt::getSignedMinValue(IntWidth), true,
                          APFloat::rmNearestTiesToEven);
    if (SMin.compare(RHS) == APFloat::cmpGreaterThan) { // smin > 12312.0
      if (Pred == ICmpInst::ICMP_NE || Pred == ICmpInst::ICMP_SGT ||
          Pred == ICmpInst::ICMP_SGE)
        return replaceInstUsesWith(I, Builder.getTrue());
      return replaceInstUsesWith(I, Builder.getFalse());
    }
  } else {
    // See if the RHS value is < UnsignedMin.
    APFloat SMin(RHS.getSemantics());
    SMin.convertFromAPInt(APInt::getMinValue(IntWidth), true,
                          APFloat::rmNearestTiesToEven);
    if (SMin.compare(RHS) == APFloat::cmpGreaterThan) { // umin > 12312.0
      if (Pred == ICmpInst::ICMP_NE || Pred == ICmpInst::ICMP_UGT ||
          Pred == ICmpInst::ICMP_UGE)
        return replaceInstUsesWith(I, Builder.getTrue());
      return replaceInstUsesWith(I, Builder.getFalse());
    }
  }

  // Okay, now we know that the FP constant fits in the range [SMIN, SMAX] or
  // [0, UMAX], but it may still be fractional.  See if it is fractional by
  // casting the FP value to the integer value and back, checking for equality.
  // Don't do this for zero, because -0.0 is not fractional.
  Constant *RHSInt = LHSUnsigned
    ? ConstantExpr::getFPToUI(RHSC, IntTy)
    : ConstantExpr::getFPToSI(RHSC, IntTy);
  if (!RHS.isZero()) {
    bool Equal = LHSUnsigned
      ? ConstantExpr::getUIToFP(RHSInt, RHSC->getType()) == RHSC
      : ConstantExpr::getSIToFP(RHSInt, RHSC->getType()) == RHSC;
    if (!Equal) {
      // If we had a comparison against a fractional value, we have to adjust
      // the compare predicate and sometimes the value.  RHSC is rounded towards
      // zero at this point.
      switch (Pred) {
      default: llvm_unreachable("Unexpected integer comparison!");
      case ICmpInst::ICMP_NE:  // (float)int != 4.4   --> true
        return replaceInstUsesWith(I, Builder.getTrue());
      case ICmpInst::ICMP_EQ:  // (float)int == 4.4   --> false
        return replaceInstUsesWith(I, Builder.getFalse());
      case ICmpInst::ICMP_ULE:
        // (float)int <= 4.4   --> int <= 4
        // (float)int <= -4.4  --> false
        if (RHS.isNegative())
          return replaceInstUsesWith(I, Builder.getFalse());
        break;
      case ICmpInst::ICMP_SLE:
        // (float)int <= 4.4   --> int <= 4
        // (float)int <= -4.4  --> int < -4
        if (RHS.isNegative())
          Pred = ICmpInst::ICMP_SLT;
        break;
      case ICmpInst::ICMP_ULT:
        // (float)int < -4.4   --> false
        // (float)int < 4.4    --> int <= 4
        if (RHS.isNegative())
          return replaceInstUsesWith(I, Builder.getFalse());
        Pred = ICmpInst::ICMP_ULE;
        break;
      case ICmpInst::ICMP_SLT:
        // (float)int < -4.4   --> int < -4
        // (float)int < 4.4    --> int <= 4
        if (!RHS.isNegative())
          Pred = ICmpInst::ICMP_SLE;
        break;
      case ICmpInst::ICMP_UGT:
        // (float)int > 4.4    --> int > 4
        // (float)int > -4.4   --> true
        if (RHS.isNegative())
          return replaceInstUsesWith(I, Builder.getTrue());
        break;
      case ICmpInst::ICMP_SGT:
        // (float)int > 4.4    --> int > 4
        // (float)int > -4.4   --> int >= -4
        if (RHS.isNegative())
          Pred = ICmpInst::ICMP_SGE;
        break;
      case ICmpInst::ICMP_UGE:
        // (float)int >= -4.4   --> true
        // (float)int >= 4.4    --> int > 4
        if (RHS.isNegative())
          return replaceInstUsesWith(I, Builder.getTrue());
        Pred = ICmpInst::ICMP_UGT;
        break;
      case ICmpInst::ICMP_SGE:
        // (float)int >= -4.4   --> int >= -4
        // (float)int >= 4.4    --> int > 4
        if (!RHS.isNegative())
          Pred = ICmpInst::ICMP_SGT;
        break;
      }
    }
  }

  // Lower this FP comparison into an appropriate integer version of the
  // comparison.
  return new ICmpInst(Pred, LHSI->getOperand(0), RHSInt);
}

/// Fold (C / X) < 0.0 --> X < 0.0 if possible. Swap predicate if necessary.
static Instruction *foldFCmpReciprocalAndZero(FCmpInst &I, Instruction *LHSI,
                                              Constant *RHSC) {
  // When C is not 0.0 and infinities are not allowed:
  // (C / X) < 0.0 is a sign-bit test of X
  // (C / X) < 0.0 --> X < 0.0 (if C is positive)
  // (C / X) < 0.0 --> X > 0.0 (if C is negative, swap the predicate)
  //
  // Proof:
  // Multiply (C / X) < 0.0 by X * X / C.
  // - X is non zero, if it is the flag 'ninf' is violated.
  // - C defines the sign of X * X * C. Thus it also defines whether to swap
  //   the predicate. C is also non zero by definition.
  //
  // Thus X * X / C is non zero and the transformation is valid. [qed]

  FCmpInst::Predicate Pred = I.getPredicate();

  // Check that predicates are valid.
  if ((Pred != FCmpInst::FCMP_OGT) && (Pred != FCmpInst::FCMP_OLT) &&
      (Pred != FCmpInst::FCMP_OGE) && (Pred != FCmpInst::FCMP_OLE))
    return nullptr;

  // Check that RHS operand is zero.
  if (!match(RHSC, m_AnyZeroFP()))
    return nullptr;

  // Check fastmath flags ('ninf').
  if (!LHSI->hasNoInfs() || !I.hasNoInfs())
    return nullptr;

  // Check the properties of the dividend. It must not be zero to avoid a
  // division by zero (see Proof).
  const APFloat *C;
  if (!match(LHSI->getOperand(0), m_APFloat(C)))
    return nullptr;

  if (C->isZero())
    return nullptr;

  // Get swapped predicate if necessary.
  if (C->isNegative())
    Pred = I.getSwappedPredicate();

  return new FCmpInst(Pred, LHSI->getOperand(1), RHSC, "", &I);
}

/// Optimize fabs(X) compared with zero.
static Instruction *foldFabsWithFcmpZero(FCmpInst &I) {
  Value *X;
  if (!match(I.getOperand(0), m_Intrinsic<Intrinsic::fabs>(m_Value(X))) ||
      !match(I.getOperand(1), m_PosZeroFP()))
    return nullptr;

  auto replacePredAndOp0 = [](FCmpInst *I, FCmpInst::Predicate P, Value *X) {
    I->setPredicate(P);
    I->setOperand(0, X);
    return I;
  };

  switch (I.getPredicate()) {
  case FCmpInst::FCMP_UGE:
  case FCmpInst::FCMP_OLT:
    // fabs(X) >= 0.0 --> true
    // fabs(X) <  0.0 --> false
    llvm_unreachable("fcmp should have simplified");

  case FCmpInst::FCMP_OGT:
    // fabs(X) > 0.0 --> X != 0.0
    return replacePredAndOp0(&I, FCmpInst::FCMP_ONE, X);

  case FCmpInst::FCMP_UGT:
    // fabs(X) u> 0.0 --> X u!= 0.0
    return replacePredAndOp0(&I, FCmpInst::FCMP_UNE, X);

  case FCmpInst::FCMP_OLE:
    // fabs(X) <= 0.0 --> X == 0.0
    return replacePredAndOp0(&I, FCmpInst::FCMP_OEQ, X);

  case FCmpInst::FCMP_ULE:
    // fabs(X) u<= 0.0 --> X u== 0.0
    return replacePredAndOp0(&I, FCmpInst::FCMP_UEQ, X);

  case FCmpInst::FCMP_OGE:
    // fabs(X) >= 0.0 --> !isnan(X)
    assert(!I.hasNoNaNs() && "fcmp should have simplified");
    return replacePredAndOp0(&I, FCmpInst::FCMP_ORD, X);

  case FCmpInst::FCMP_ULT:
    // fabs(X) u< 0.0 --> isnan(X)
    assert(!I.hasNoNaNs() && "fcmp should have simplified");
    return replacePredAndOp0(&I, FCmpInst::FCMP_UNO, X);

  case FCmpInst::FCMP_OEQ:
  case FCmpInst::FCMP_UEQ:
  case FCmpInst::FCMP_ONE:
  case FCmpInst::FCMP_UNE:
  case FCmpInst::FCMP_ORD:
  case FCmpInst::FCMP_UNO:
    // Look through the fabs() because it doesn't change anything but the sign.
    // fabs(X) == 0.0 --> X == 0.0,
    // fabs(X) != 0.0 --> X != 0.0
    // isnan(fabs(X)) --> isnan(X)
    // !isnan(fabs(X) --> !isnan(X)
    return replacePredAndOp0(&I, I.getPredicate(), X);

  default:
    return nullptr;
  }
}

Instruction *InstCombiner::visitFCmpInst(FCmpInst &I) {
  bool Changed = false;

  /// Orders the operands of the compare so that they are listed from most
  /// complex to least complex.  This puts constants before unary operators,
  /// before binary operators.
  if (getComplexity(I.getOperand(0)) < getComplexity(I.getOperand(1))) {
    I.swapOperands();
    Changed = true;
  }

  const CmpInst::Predicate Pred = I.getPredicate();
  Value *Op0 = I.getOperand(0), *Op1 = I.getOperand(1);
  if (Value *V = SimplifyFCmpInst(Pred, Op0, Op1, I.getFastMathFlags(),
                                  SQ.getWithInstruction(&I)))
    return replaceInstUsesWith(I, V);

  // Simplify 'fcmp pred X, X'
  if (Op0 == Op1) {
    switch (Pred) {
      default: break;
    case FCmpInst::FCMP_UNO:    // True if unordered: isnan(X) | isnan(Y)
    case FCmpInst::FCMP_ULT:    // True if unordered or less than
    case FCmpInst::FCMP_UGT:    // True if unordered or greater than
    case FCmpInst::FCMP_UNE:    // True if unordered or not equal
      // Canonicalize these to be 'fcmp uno %X, 0.0'.
      I.setPredicate(FCmpInst::FCMP_UNO);
      I.setOperand(1, Constant::getNullValue(Op0->getType()));
      return &I;

    case FCmpInst::FCMP_ORD:    // True if ordered (no nans)
    case FCmpInst::FCMP_OEQ:    // True if ordered and equal
    case FCmpInst::FCMP_OGE:    // True if ordered and greater than or equal
    case FCmpInst::FCMP_OLE:    // True if ordered and less than or equal
      // Canonicalize these to be 'fcmp ord %X, 0.0'.
      I.setPredicate(FCmpInst::FCMP_ORD);
      I.setOperand(1, Constant::getNullValue(Op0->getType()));
      return &I;
    }
  }

  // If we're just checking for a NaN (ORD/UNO) and have a non-NaN operand,
  // then canonicalize the operand to 0.0.
  if (Pred == CmpInst::FCMP_ORD || Pred == CmpInst::FCMP_UNO) {
    if (!match(Op0, m_PosZeroFP()) && isKnownNeverNaN(Op0, &TLI)) {
      I.setOperand(0, ConstantFP::getNullValue(Op0->getType()));
      return &I;
    }
    if (!match(Op1, m_PosZeroFP()) && isKnownNeverNaN(Op1, &TLI)) {
      I.setOperand(1, ConstantFP::getNullValue(Op0->getType()));
      return &I;
    }
  }

  // Test if the FCmpInst instruction is used exclusively by a select as
  // part of a minimum or maximum operation. If so, refrain from doing
  // any other folding. This helps out other analyses which understand
  // non-obfuscated minimum and maximum idioms, such as ScalarEvolution
  // and CodeGen. And in this case, at least one of the comparison
  // operands has at least one user besides the compare (the select),
  // which would often largely negate the benefit of folding anyway.
  if (I.hasOneUse())
    if (SelectInst *SI = dyn_cast<SelectInst>(I.user_back())) {
      Value *A, *B;
      SelectPatternResult SPR = matchSelectPattern(SI, A, B);
      if (SPR.Flavor != SPF_UNKNOWN)
        return nullptr;
    }

  // The sign of 0.0 is ignored by fcmp, so canonicalize to +0.0:
  // fcmp Pred X, -0.0 --> fcmp Pred X, 0.0
  if (match(Op1, m_AnyZeroFP()) && !match(Op1, m_PosZeroFP())) {
    I.setOperand(1, ConstantFP::getNullValue(Op1->getType()));
    return &I;
  }

  // Handle fcmp with instruction LHS and constant RHS.
  Instruction *LHSI;
  Constant *RHSC;
  if (match(Op0, m_Instruction(LHSI)) && match(Op1, m_Constant(RHSC))) {
    switch (LHSI->getOpcode()) {
    case Instruction::PHI:
      // Only fold fcmp into the PHI if the phi and fcmp are in the same
      // block.  If in the same block, we're encouraging jump threading.  If
      // not, we are just pessimizing the code by making an i1 phi.
      if (LHSI->getParent() == I.getParent())
        if (Instruction *NV = foldOpIntoPhi(I, cast<PHINode>(LHSI)))
          return NV;
      break;
    case Instruction::SIToFP:
    case Instruction::UIToFP:
      if (Instruction *NV = foldFCmpIntToFPConst(I, LHSI, RHSC))
        return NV;
      break;
    case Instruction::FDiv:
      if (Instruction *NV = foldFCmpReciprocalAndZero(I, LHSI, RHSC))
        return NV;
      break;
    case Instruction::Load:
      if (auto *GEP = dyn_cast<GetElementPtrInst>(LHSI->getOperand(0)))
        if (auto *GV = dyn_cast<GlobalVariable>(GEP->getOperand(0)))
          if (GV->isConstant() && GV->hasDefinitiveInitializer() &&
              !cast<LoadInst>(LHSI)->isVolatile())
            if (Instruction *Res = foldCmpLoadFromIndexedGlobal(GEP, GV, I))
              return Res;
      break;
  }
  }

  if (Instruction *R = foldFabsWithFcmpZero(I))
    return R;

  Value *X, *Y;
  if (match(Op0, m_FNeg(m_Value(X)))) {
    // fcmp pred (fneg X), (fneg Y) -> fcmp swap(pred) X, Y
    if (match(Op1, m_FNeg(m_Value(Y))))
      return new FCmpInst(I.getSwappedPredicate(), X, Y, "", &I);

    // fcmp pred (fneg X), C --> fcmp swap(pred) X, -C
    Constant *C;
    if (match(Op1, m_Constant(C))) {
      Constant *NegC = ConstantExpr::getFNeg(C);
      return new FCmpInst(I.getSwappedPredicate(), X, NegC, "", &I);
    }
  }

  if (match(Op0, m_FPExt(m_Value(X)))) {
    // fcmp (fpext X), (fpext Y) -> fcmp X, Y
    if (match(Op1, m_FPExt(m_Value(Y))) && X->getType() == Y->getType())
      return new FCmpInst(Pred, X, Y, "", &I);

    // fcmp (fpext X), C -> fcmp X, (fptrunc C) if fptrunc is lossless
    const APFloat *C;
    if (match(Op1, m_APFloat(C))) {
      const fltSemantics &FPSem =
          X->getType()->getScalarType()->getFltSemantics();
      bool Lossy;
      APFloat TruncC = *C;
      TruncC.convert(FPSem, APFloat::rmNearestTiesToEven, &Lossy);

      // Avoid lossy conversions and denormals.
      // Zero is a special case that's OK to convert.
      APFloat Fabs = TruncC;
      Fabs.clearSign();
      if (!Lossy &&
          ((Fabs.compare(APFloat::getSmallestNormalized(FPSem)) !=
            APFloat::cmpLessThan) || Fabs.isZero())) {
        Constant *NewC = ConstantFP::get(X->getType(), TruncC);
        return new FCmpInst(Pred, X, NewC, "", &I);
      }
    }
  }

  if (I.getType()->isVectorTy())
    if (Instruction *Res = foldVectorCmp(I, Builder))
      return Res;

  return Changed ? &I : nullptr;
}
