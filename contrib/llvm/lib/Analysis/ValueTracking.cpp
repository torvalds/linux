//===- ValueTracking.cpp - Walk computations to compute properties --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains routines that help analyze properties that chains of
// computations have.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/ValueTracking.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/GuardUtils.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/Loads.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/KnownBits.h"
#include "llvm/Support/MathExtras.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <utility>

using namespace llvm;
using namespace llvm::PatternMatch;

const unsigned MaxDepth = 6;

// Controls the number of uses of the value searched for possible
// dominating comparisons.
static cl::opt<unsigned> DomConditionsMaxUses("dom-conditions-max-uses",
                                              cl::Hidden, cl::init(20));

/// Returns the bitwidth of the given scalar or pointer type. For vector types,
/// returns the element type's bitwidth.
static unsigned getBitWidth(Type *Ty, const DataLayout &DL) {
  if (unsigned BitWidth = Ty->getScalarSizeInBits())
    return BitWidth;

  return DL.getIndexTypeSizeInBits(Ty);
}

namespace {

// Simplifying using an assume can only be done in a particular control-flow
// context (the context instruction provides that context). If an assume and
// the context instruction are not in the same block then the DT helps in
// figuring out if we can use it.
struct Query {
  const DataLayout &DL;
  AssumptionCache *AC;
  const Instruction *CxtI;
  const DominatorTree *DT;

  // Unlike the other analyses, this may be a nullptr because not all clients
  // provide it currently.
  OptimizationRemarkEmitter *ORE;

  /// Set of assumptions that should be excluded from further queries.
  /// This is because of the potential for mutual recursion to cause
  /// computeKnownBits to repeatedly visit the same assume intrinsic. The
  /// classic case of this is assume(x = y), which will attempt to determine
  /// bits in x from bits in y, which will attempt to determine bits in y from
  /// bits in x, etc. Regarding the mutual recursion, computeKnownBits can call
  /// isKnownNonZero, which calls computeKnownBits and isKnownToBeAPowerOfTwo
  /// (all of which can call computeKnownBits), and so on.
  std::array<const Value *, MaxDepth> Excluded;

  /// If true, it is safe to use metadata during simplification.
  InstrInfoQuery IIQ;

  unsigned NumExcluded = 0;

  Query(const DataLayout &DL, AssumptionCache *AC, const Instruction *CxtI,
        const DominatorTree *DT, bool UseInstrInfo,
        OptimizationRemarkEmitter *ORE = nullptr)
      : DL(DL), AC(AC), CxtI(CxtI), DT(DT), ORE(ORE), IIQ(UseInstrInfo) {}

  Query(const Query &Q, const Value *NewExcl)
      : DL(Q.DL), AC(Q.AC), CxtI(Q.CxtI), DT(Q.DT), ORE(Q.ORE), IIQ(Q.IIQ),
        NumExcluded(Q.NumExcluded) {
    Excluded = Q.Excluded;
    Excluded[NumExcluded++] = NewExcl;
    assert(NumExcluded <= Excluded.size());
  }

  bool isExcluded(const Value *Value) const {
    if (NumExcluded == 0)
      return false;
    auto End = Excluded.begin() + NumExcluded;
    return std::find(Excluded.begin(), End, Value) != End;
  }
};

} // end anonymous namespace

// Given the provided Value and, potentially, a context instruction, return
// the preferred context instruction (if any).
static const Instruction *safeCxtI(const Value *V, const Instruction *CxtI) {
  // If we've been provided with a context instruction, then use that (provided
  // it has been inserted).
  if (CxtI && CxtI->getParent())
    return CxtI;

  // If the value is really an already-inserted instruction, then use that.
  CxtI = dyn_cast<Instruction>(V);
  if (CxtI && CxtI->getParent())
    return CxtI;

  return nullptr;
}

static void computeKnownBits(const Value *V, KnownBits &Known,
                             unsigned Depth, const Query &Q);

void llvm::computeKnownBits(const Value *V, KnownBits &Known,
                            const DataLayout &DL, unsigned Depth,
                            AssumptionCache *AC, const Instruction *CxtI,
                            const DominatorTree *DT,
                            OptimizationRemarkEmitter *ORE, bool UseInstrInfo) {
  ::computeKnownBits(V, Known, Depth,
                     Query(DL, AC, safeCxtI(V, CxtI), DT, UseInstrInfo, ORE));
}

static KnownBits computeKnownBits(const Value *V, unsigned Depth,
                                  const Query &Q);

KnownBits llvm::computeKnownBits(const Value *V, const DataLayout &DL,
                                 unsigned Depth, AssumptionCache *AC,
                                 const Instruction *CxtI,
                                 const DominatorTree *DT,
                                 OptimizationRemarkEmitter *ORE,
                                 bool UseInstrInfo) {
  return ::computeKnownBits(
      V, Depth, Query(DL, AC, safeCxtI(V, CxtI), DT, UseInstrInfo, ORE));
}

bool llvm::haveNoCommonBitsSet(const Value *LHS, const Value *RHS,
                               const DataLayout &DL, AssumptionCache *AC,
                               const Instruction *CxtI, const DominatorTree *DT,
                               bool UseInstrInfo) {
  assert(LHS->getType() == RHS->getType() &&
         "LHS and RHS should have the same type");
  assert(LHS->getType()->isIntOrIntVectorTy() &&
         "LHS and RHS should be integers");
  // Look for an inverted mask: (X & ~M) op (Y & M).
  Value *M;
  if (match(LHS, m_c_And(m_Not(m_Value(M)), m_Value())) &&
      match(RHS, m_c_And(m_Specific(M), m_Value())))
    return true;
  if (match(RHS, m_c_And(m_Not(m_Value(M)), m_Value())) &&
      match(LHS, m_c_And(m_Specific(M), m_Value())))
    return true;
  IntegerType *IT = cast<IntegerType>(LHS->getType()->getScalarType());
  KnownBits LHSKnown(IT->getBitWidth());
  KnownBits RHSKnown(IT->getBitWidth());
  computeKnownBits(LHS, LHSKnown, DL, 0, AC, CxtI, DT, nullptr, UseInstrInfo);
  computeKnownBits(RHS, RHSKnown, DL, 0, AC, CxtI, DT, nullptr, UseInstrInfo);
  return (LHSKnown.Zero | RHSKnown.Zero).isAllOnesValue();
}

bool llvm::isOnlyUsedInZeroEqualityComparison(const Instruction *CxtI) {
  for (const User *U : CxtI->users()) {
    if (const ICmpInst *IC = dyn_cast<ICmpInst>(U))
      if (IC->isEquality())
        if (Constant *C = dyn_cast<Constant>(IC->getOperand(1)))
          if (C->isNullValue())
            continue;
    return false;
  }
  return true;
}

static bool isKnownToBeAPowerOfTwo(const Value *V, bool OrZero, unsigned Depth,
                                   const Query &Q);

bool llvm::isKnownToBeAPowerOfTwo(const Value *V, const DataLayout &DL,
                                  bool OrZero, unsigned Depth,
                                  AssumptionCache *AC, const Instruction *CxtI,
                                  const DominatorTree *DT, bool UseInstrInfo) {
  return ::isKnownToBeAPowerOfTwo(
      V, OrZero, Depth, Query(DL, AC, safeCxtI(V, CxtI), DT, UseInstrInfo));
}

static bool isKnownNonZero(const Value *V, unsigned Depth, const Query &Q);

bool llvm::isKnownNonZero(const Value *V, const DataLayout &DL, unsigned Depth,
                          AssumptionCache *AC, const Instruction *CxtI,
                          const DominatorTree *DT, bool UseInstrInfo) {
  return ::isKnownNonZero(V, Depth,
                          Query(DL, AC, safeCxtI(V, CxtI), DT, UseInstrInfo));
}

bool llvm::isKnownNonNegative(const Value *V, const DataLayout &DL,
                              unsigned Depth, AssumptionCache *AC,
                              const Instruction *CxtI, const DominatorTree *DT,
                              bool UseInstrInfo) {
  KnownBits Known =
      computeKnownBits(V, DL, Depth, AC, CxtI, DT, nullptr, UseInstrInfo);
  return Known.isNonNegative();
}

bool llvm::isKnownPositive(const Value *V, const DataLayout &DL, unsigned Depth,
                           AssumptionCache *AC, const Instruction *CxtI,
                           const DominatorTree *DT, bool UseInstrInfo) {
  if (auto *CI = dyn_cast<ConstantInt>(V))
    return CI->getValue().isStrictlyPositive();

  // TODO: We'd doing two recursive queries here.  We should factor this such
  // that only a single query is needed.
  return isKnownNonNegative(V, DL, Depth, AC, CxtI, DT, UseInstrInfo) &&
         isKnownNonZero(V, DL, Depth, AC, CxtI, DT, UseInstrInfo);
}

bool llvm::isKnownNegative(const Value *V, const DataLayout &DL, unsigned Depth,
                           AssumptionCache *AC, const Instruction *CxtI,
                           const DominatorTree *DT, bool UseInstrInfo) {
  KnownBits Known =
      computeKnownBits(V, DL, Depth, AC, CxtI, DT, nullptr, UseInstrInfo);
  return Known.isNegative();
}

static bool isKnownNonEqual(const Value *V1, const Value *V2, const Query &Q);

bool llvm::isKnownNonEqual(const Value *V1, const Value *V2,
                           const DataLayout &DL, AssumptionCache *AC,
                           const Instruction *CxtI, const DominatorTree *DT,
                           bool UseInstrInfo) {
  return ::isKnownNonEqual(V1, V2,
                           Query(DL, AC, safeCxtI(V1, safeCxtI(V2, CxtI)), DT,
                                 UseInstrInfo, /*ORE=*/nullptr));
}

static bool MaskedValueIsZero(const Value *V, const APInt &Mask, unsigned Depth,
                              const Query &Q);

bool llvm::MaskedValueIsZero(const Value *V, const APInt &Mask,
                             const DataLayout &DL, unsigned Depth,
                             AssumptionCache *AC, const Instruction *CxtI,
                             const DominatorTree *DT, bool UseInstrInfo) {
  return ::MaskedValueIsZero(
      V, Mask, Depth, Query(DL, AC, safeCxtI(V, CxtI), DT, UseInstrInfo));
}

static unsigned ComputeNumSignBits(const Value *V, unsigned Depth,
                                   const Query &Q);

unsigned llvm::ComputeNumSignBits(const Value *V, const DataLayout &DL,
                                  unsigned Depth, AssumptionCache *AC,
                                  const Instruction *CxtI,
                                  const DominatorTree *DT, bool UseInstrInfo) {
  return ::ComputeNumSignBits(
      V, Depth, Query(DL, AC, safeCxtI(V, CxtI), DT, UseInstrInfo));
}

static void computeKnownBitsAddSub(bool Add, const Value *Op0, const Value *Op1,
                                   bool NSW,
                                   KnownBits &KnownOut, KnownBits &Known2,
                                   unsigned Depth, const Query &Q) {
  unsigned BitWidth = KnownOut.getBitWidth();

  // If an initial sequence of bits in the result is not needed, the
  // corresponding bits in the operands are not needed.
  KnownBits LHSKnown(BitWidth);
  computeKnownBits(Op0, LHSKnown, Depth + 1, Q);
  computeKnownBits(Op1, Known2, Depth + 1, Q);

  KnownOut = KnownBits::computeForAddSub(Add, NSW, LHSKnown, Known2);
}

static void computeKnownBitsMul(const Value *Op0, const Value *Op1, bool NSW,
                                KnownBits &Known, KnownBits &Known2,
                                unsigned Depth, const Query &Q) {
  unsigned BitWidth = Known.getBitWidth();
  computeKnownBits(Op1, Known, Depth + 1, Q);
  computeKnownBits(Op0, Known2, Depth + 1, Q);

  bool isKnownNegative = false;
  bool isKnownNonNegative = false;
  // If the multiplication is known not to overflow, compute the sign bit.
  if (NSW) {
    if (Op0 == Op1) {
      // The product of a number with itself is non-negative.
      isKnownNonNegative = true;
    } else {
      bool isKnownNonNegativeOp1 = Known.isNonNegative();
      bool isKnownNonNegativeOp0 = Known2.isNonNegative();
      bool isKnownNegativeOp1 = Known.isNegative();
      bool isKnownNegativeOp0 = Known2.isNegative();
      // The product of two numbers with the same sign is non-negative.
      isKnownNonNegative = (isKnownNegativeOp1 && isKnownNegativeOp0) ||
        (isKnownNonNegativeOp1 && isKnownNonNegativeOp0);
      // The product of a negative number and a non-negative number is either
      // negative or zero.
      if (!isKnownNonNegative)
        isKnownNegative = (isKnownNegativeOp1 && isKnownNonNegativeOp0 &&
                           isKnownNonZero(Op0, Depth, Q)) ||
                          (isKnownNegativeOp0 && isKnownNonNegativeOp1 &&
                           isKnownNonZero(Op1, Depth, Q));
    }
  }

  assert(!Known.hasConflict() && !Known2.hasConflict());
  // Compute a conservative estimate for high known-0 bits.
  unsigned LeadZ =  std::max(Known.countMinLeadingZeros() +
                             Known2.countMinLeadingZeros(),
                             BitWidth) - BitWidth;
  LeadZ = std::min(LeadZ, BitWidth);

  // The result of the bottom bits of an integer multiply can be
  // inferred by looking at the bottom bits of both operands and
  // multiplying them together.
  // We can infer at least the minimum number of known trailing bits
  // of both operands. Depending on number of trailing zeros, we can
  // infer more bits, because (a*b) <=> ((a/m) * (b/n)) * (m*n) assuming
  // a and b are divisible by m and n respectively.
  // We then calculate how many of those bits are inferrable and set
  // the output. For example, the i8 mul:
  //  a = XXXX1100 (12)
  //  b = XXXX1110 (14)
  // We know the bottom 3 bits are zero since the first can be divided by
  // 4 and the second by 2, thus having ((12/4) * (14/2)) * (2*4).
  // Applying the multiplication to the trimmed arguments gets:
  //    XX11 (3)
  //    X111 (7)
  // -------
  //    XX11
  //   XX11
  //  XX11
  // XX11
  // -------
  // XXXXX01
  // Which allows us to infer the 2 LSBs. Since we're multiplying the result
  // by 8, the bottom 3 bits will be 0, so we can infer a total of 5 bits.
  // The proof for this can be described as:
  // Pre: (C1 >= 0) && (C1 < (1 << C5)) && (C2 >= 0) && (C2 < (1 << C6)) &&
  //      (C7 == (1 << (umin(countTrailingZeros(C1), C5) +
  //                    umin(countTrailingZeros(C2), C6) +
  //                    umin(C5 - umin(countTrailingZeros(C1), C5),
  //                         C6 - umin(countTrailingZeros(C2), C6)))) - 1)
  // %aa = shl i8 %a, C5
  // %bb = shl i8 %b, C6
  // %aaa = or i8 %aa, C1
  // %bbb = or i8 %bb, C2
  // %mul = mul i8 %aaa, %bbb
  // %mask = and i8 %mul, C7
  //   =>
  // %mask = i8 ((C1*C2)&C7)
  // Where C5, C6 describe the known bits of %a, %b
  // C1, C2 describe the known bottom bits of %a, %b.
  // C7 describes the mask of the known bits of the result.
  APInt Bottom0 = Known.One;
  APInt Bottom1 = Known2.One;

  // How many times we'd be able to divide each argument by 2 (shr by 1).
  // This gives us the number of trailing zeros on the multiplication result.
  unsigned TrailBitsKnown0 = (Known.Zero | Known.One).countTrailingOnes();
  unsigned TrailBitsKnown1 = (Known2.Zero | Known2.One).countTrailingOnes();
  unsigned TrailZero0 = Known.countMinTrailingZeros();
  unsigned TrailZero1 = Known2.countMinTrailingZeros();
  unsigned TrailZ = TrailZero0 + TrailZero1;

  // Figure out the fewest known-bits operand.
  unsigned SmallestOperand = std::min(TrailBitsKnown0 - TrailZero0,
                                      TrailBitsKnown1 - TrailZero1);
  unsigned ResultBitsKnown = std::min(SmallestOperand + TrailZ, BitWidth);

  APInt BottomKnown = Bottom0.getLoBits(TrailBitsKnown0) *
                      Bottom1.getLoBits(TrailBitsKnown1);

  Known.resetAll();
  Known.Zero.setHighBits(LeadZ);
  Known.Zero |= (~BottomKnown).getLoBits(ResultBitsKnown);
  Known.One |= BottomKnown.getLoBits(ResultBitsKnown);

  // Only make use of no-wrap flags if we failed to compute the sign bit
  // directly.  This matters if the multiplication always overflows, in
  // which case we prefer to follow the result of the direct computation,
  // though as the program is invoking undefined behaviour we can choose
  // whatever we like here.
  if (isKnownNonNegative && !Known.isNegative())
    Known.makeNonNegative();
  else if (isKnownNegative && !Known.isNonNegative())
    Known.makeNegative();
}

void llvm::computeKnownBitsFromRangeMetadata(const MDNode &Ranges,
                                             KnownBits &Known) {
  unsigned BitWidth = Known.getBitWidth();
  unsigned NumRanges = Ranges.getNumOperands() / 2;
  assert(NumRanges >= 1);

  Known.Zero.setAllBits();
  Known.One.setAllBits();

  for (unsigned i = 0; i < NumRanges; ++i) {
    ConstantInt *Lower =
        mdconst::extract<ConstantInt>(Ranges.getOperand(2 * i + 0));
    ConstantInt *Upper =
        mdconst::extract<ConstantInt>(Ranges.getOperand(2 * i + 1));
    ConstantRange Range(Lower->getValue(), Upper->getValue());

    // The first CommonPrefixBits of all values in Range are equal.
    unsigned CommonPrefixBits =
        (Range.getUnsignedMax() ^ Range.getUnsignedMin()).countLeadingZeros();

    APInt Mask = APInt::getHighBitsSet(BitWidth, CommonPrefixBits);
    Known.One &= Range.getUnsignedMax() & Mask;
    Known.Zero &= ~Range.getUnsignedMax() & Mask;
  }
}

static bool isEphemeralValueOf(const Instruction *I, const Value *E) {
  SmallVector<const Value *, 16> WorkSet(1, I);
  SmallPtrSet<const Value *, 32> Visited;
  SmallPtrSet<const Value *, 16> EphValues;

  // The instruction defining an assumption's condition itself is always
  // considered ephemeral to that assumption (even if it has other
  // non-ephemeral users). See r246696's test case for an example.
  if (is_contained(I->operands(), E))
    return true;

  while (!WorkSet.empty()) {
    const Value *V = WorkSet.pop_back_val();
    if (!Visited.insert(V).second)
      continue;

    // If all uses of this value are ephemeral, then so is this value.
    if (llvm::all_of(V->users(), [&](const User *U) {
                                   return EphValues.count(U);
                                 })) {
      if (V == E)
        return true;

      if (V == I || isSafeToSpeculativelyExecute(V)) {
       EphValues.insert(V);
       if (const User *U = dyn_cast<User>(V))
         for (User::const_op_iterator J = U->op_begin(), JE = U->op_end();
              J != JE; ++J)
           WorkSet.push_back(*J);
      }
    }
  }

  return false;
}

// Is this an intrinsic that cannot be speculated but also cannot trap?
bool llvm::isAssumeLikeIntrinsic(const Instruction *I) {
  if (const CallInst *CI = dyn_cast<CallInst>(I))
    if (Function *F = CI->getCalledFunction())
      switch (F->getIntrinsicID()) {
      default: break;
      // FIXME: This list is repeated from NoTTI::getIntrinsicCost.
      case Intrinsic::assume:
      case Intrinsic::sideeffect:
      case Intrinsic::dbg_declare:
      case Intrinsic::dbg_value:
      case Intrinsic::dbg_label:
      case Intrinsic::invariant_start:
      case Intrinsic::invariant_end:
      case Intrinsic::lifetime_start:
      case Intrinsic::lifetime_end:
      case Intrinsic::objectsize:
      case Intrinsic::ptr_annotation:
      case Intrinsic::var_annotation:
        return true;
      }

  return false;
}

bool llvm::isValidAssumeForContext(const Instruction *Inv,
                                   const Instruction *CxtI,
                                   const DominatorTree *DT) {
  // There are two restrictions on the use of an assume:
  //  1. The assume must dominate the context (or the control flow must
  //     reach the assume whenever it reaches the context).
  //  2. The context must not be in the assume's set of ephemeral values
  //     (otherwise we will use the assume to prove that the condition
  //     feeding the assume is trivially true, thus causing the removal of
  //     the assume).

  if (DT) {
    if (DT->dominates(Inv, CxtI))
      return true;
  } else if (Inv->getParent() == CxtI->getParent()->getSinglePredecessor()) {
    // We don't have a DT, but this trivially dominates.
    return true;
  }

  // With or without a DT, the only remaining case we will check is if the
  // instructions are in the same BB.  Give up if that is not the case.
  if (Inv->getParent() != CxtI->getParent())
    return false;

  // If we have a dom tree, then we now know that the assume doesn't dominate
  // the other instruction.  If we don't have a dom tree then we can check if
  // the assume is first in the BB.
  if (!DT) {
    // Search forward from the assume until we reach the context (or the end
    // of the block); the common case is that the assume will come first.
    for (auto I = std::next(BasicBlock::const_iterator(Inv)),
         IE = Inv->getParent()->end(); I != IE; ++I)
      if (&*I == CxtI)
        return true;
  }

  // The context comes first, but they're both in the same block. Make sure
  // there is nothing in between that might interrupt the control flow.
  for (BasicBlock::const_iterator I =
         std::next(BasicBlock::const_iterator(CxtI)), IE(Inv);
       I != IE; ++I)
    if (!isSafeToSpeculativelyExecute(&*I) && !isAssumeLikeIntrinsic(&*I))
      return false;

  return !isEphemeralValueOf(Inv, CxtI);
}

static void computeKnownBitsFromAssume(const Value *V, KnownBits &Known,
                                       unsigned Depth, const Query &Q) {
  // Use of assumptions is context-sensitive. If we don't have a context, we
  // cannot use them!
  if (!Q.AC || !Q.CxtI)
    return;

  unsigned BitWidth = Known.getBitWidth();

  // Note that the patterns below need to be kept in sync with the code
  // in AssumptionCache::updateAffectedValues.

  for (auto &AssumeVH : Q.AC->assumptionsFor(V)) {
    if (!AssumeVH)
      continue;
    CallInst *I = cast<CallInst>(AssumeVH);
    assert(I->getParent()->getParent() == Q.CxtI->getParent()->getParent() &&
           "Got assumption for the wrong function!");
    if (Q.isExcluded(I))
      continue;

    // Warning: This loop can end up being somewhat performance sensitive.
    // We're running this loop for once for each value queried resulting in a
    // runtime of ~O(#assumes * #values).

    assert(I->getCalledFunction()->getIntrinsicID() == Intrinsic::assume &&
           "must be an assume intrinsic");

    Value *Arg = I->getArgOperand(0);

    if (Arg == V && isValidAssumeForContext(I, Q.CxtI, Q.DT)) {
      assert(BitWidth == 1 && "assume operand is not i1?");
      Known.setAllOnes();
      return;
    }
    if (match(Arg, m_Not(m_Specific(V))) &&
        isValidAssumeForContext(I, Q.CxtI, Q.DT)) {
      assert(BitWidth == 1 && "assume operand is not i1?");
      Known.setAllZero();
      return;
    }

    // The remaining tests are all recursive, so bail out if we hit the limit.
    if (Depth == MaxDepth)
      continue;

    Value *A, *B;
    auto m_V = m_CombineOr(m_Specific(V),
                           m_CombineOr(m_PtrToInt(m_Specific(V)),
                           m_BitCast(m_Specific(V))));

    CmpInst::Predicate Pred;
    uint64_t C;
    // assume(v = a)
    if (match(Arg, m_c_ICmp(Pred, m_V, m_Value(A))) &&
        Pred == ICmpInst::ICMP_EQ && isValidAssumeForContext(I, Q.CxtI, Q.DT)) {
      KnownBits RHSKnown(BitWidth);
      computeKnownBits(A, RHSKnown, Depth+1, Query(Q, I));
      Known.Zero |= RHSKnown.Zero;
      Known.One  |= RHSKnown.One;
    // assume(v & b = a)
    } else if (match(Arg,
                     m_c_ICmp(Pred, m_c_And(m_V, m_Value(B)), m_Value(A))) &&
               Pred == ICmpInst::ICMP_EQ &&
               isValidAssumeForContext(I, Q.CxtI, Q.DT)) {
      KnownBits RHSKnown(BitWidth);
      computeKnownBits(A, RHSKnown, Depth+1, Query(Q, I));
      KnownBits MaskKnown(BitWidth);
      computeKnownBits(B, MaskKnown, Depth+1, Query(Q, I));

      // For those bits in the mask that are known to be one, we can propagate
      // known bits from the RHS to V.
      Known.Zero |= RHSKnown.Zero & MaskKnown.One;
      Known.One  |= RHSKnown.One  & MaskKnown.One;
    // assume(~(v & b) = a)
    } else if (match(Arg, m_c_ICmp(Pred, m_Not(m_c_And(m_V, m_Value(B))),
                                   m_Value(A))) &&
               Pred == ICmpInst::ICMP_EQ &&
               isValidAssumeForContext(I, Q.CxtI, Q.DT)) {
      KnownBits RHSKnown(BitWidth);
      computeKnownBits(A, RHSKnown, Depth+1, Query(Q, I));
      KnownBits MaskKnown(BitWidth);
      computeKnownBits(B, MaskKnown, Depth+1, Query(Q, I));

      // For those bits in the mask that are known to be one, we can propagate
      // inverted known bits from the RHS to V.
      Known.Zero |= RHSKnown.One  & MaskKnown.One;
      Known.One  |= RHSKnown.Zero & MaskKnown.One;
    // assume(v | b = a)
    } else if (match(Arg,
                     m_c_ICmp(Pred, m_c_Or(m_V, m_Value(B)), m_Value(A))) &&
               Pred == ICmpInst::ICMP_EQ &&
               isValidAssumeForContext(I, Q.CxtI, Q.DT)) {
      KnownBits RHSKnown(BitWidth);
      computeKnownBits(A, RHSKnown, Depth+1, Query(Q, I));
      KnownBits BKnown(BitWidth);
      computeKnownBits(B, BKnown, Depth+1, Query(Q, I));

      // For those bits in B that are known to be zero, we can propagate known
      // bits from the RHS to V.
      Known.Zero |= RHSKnown.Zero & BKnown.Zero;
      Known.One  |= RHSKnown.One  & BKnown.Zero;
    // assume(~(v | b) = a)
    } else if (match(Arg, m_c_ICmp(Pred, m_Not(m_c_Or(m_V, m_Value(B))),
                                   m_Value(A))) &&
               Pred == ICmpInst::ICMP_EQ &&
               isValidAssumeForContext(I, Q.CxtI, Q.DT)) {
      KnownBits RHSKnown(BitWidth);
      computeKnownBits(A, RHSKnown, Depth+1, Query(Q, I));
      KnownBits BKnown(BitWidth);
      computeKnownBits(B, BKnown, Depth+1, Query(Q, I));

      // For those bits in B that are known to be zero, we can propagate
      // inverted known bits from the RHS to V.
      Known.Zero |= RHSKnown.One  & BKnown.Zero;
      Known.One  |= RHSKnown.Zero & BKnown.Zero;
    // assume(v ^ b = a)
    } else if (match(Arg,
                     m_c_ICmp(Pred, m_c_Xor(m_V, m_Value(B)), m_Value(A))) &&
               Pred == ICmpInst::ICMP_EQ &&
               isValidAssumeForContext(I, Q.CxtI, Q.DT)) {
      KnownBits RHSKnown(BitWidth);
      computeKnownBits(A, RHSKnown, Depth+1, Query(Q, I));
      KnownBits BKnown(BitWidth);
      computeKnownBits(B, BKnown, Depth+1, Query(Q, I));

      // For those bits in B that are known to be zero, we can propagate known
      // bits from the RHS to V. For those bits in B that are known to be one,
      // we can propagate inverted known bits from the RHS to V.
      Known.Zero |= RHSKnown.Zero & BKnown.Zero;
      Known.One  |= RHSKnown.One  & BKnown.Zero;
      Known.Zero |= RHSKnown.One  & BKnown.One;
      Known.One  |= RHSKnown.Zero & BKnown.One;
    // assume(~(v ^ b) = a)
    } else if (match(Arg, m_c_ICmp(Pred, m_Not(m_c_Xor(m_V, m_Value(B))),
                                   m_Value(A))) &&
               Pred == ICmpInst::ICMP_EQ &&
               isValidAssumeForContext(I, Q.CxtI, Q.DT)) {
      KnownBits RHSKnown(BitWidth);
      computeKnownBits(A, RHSKnown, Depth+1, Query(Q, I));
      KnownBits BKnown(BitWidth);
      computeKnownBits(B, BKnown, Depth+1, Query(Q, I));

      // For those bits in B that are known to be zero, we can propagate
      // inverted known bits from the RHS to V. For those bits in B that are
      // known to be one, we can propagate known bits from the RHS to V.
      Known.Zero |= RHSKnown.One  & BKnown.Zero;
      Known.One  |= RHSKnown.Zero & BKnown.Zero;
      Known.Zero |= RHSKnown.Zero & BKnown.One;
      Known.One  |= RHSKnown.One  & BKnown.One;
    // assume(v << c = a)
    } else if (match(Arg, m_c_ICmp(Pred, m_Shl(m_V, m_ConstantInt(C)),
                                   m_Value(A))) &&
               Pred == ICmpInst::ICMP_EQ &&
               isValidAssumeForContext(I, Q.CxtI, Q.DT) &&
               C < BitWidth) {
      KnownBits RHSKnown(BitWidth);
      computeKnownBits(A, RHSKnown, Depth+1, Query(Q, I));
      // For those bits in RHS that are known, we can propagate them to known
      // bits in V shifted to the right by C.
      RHSKnown.Zero.lshrInPlace(C);
      Known.Zero |= RHSKnown.Zero;
      RHSKnown.One.lshrInPlace(C);
      Known.One  |= RHSKnown.One;
    // assume(~(v << c) = a)
    } else if (match(Arg, m_c_ICmp(Pred, m_Not(m_Shl(m_V, m_ConstantInt(C))),
                                   m_Value(A))) &&
               Pred == ICmpInst::ICMP_EQ &&
               isValidAssumeForContext(I, Q.CxtI, Q.DT) &&
               C < BitWidth) {
      KnownBits RHSKnown(BitWidth);
      computeKnownBits(A, RHSKnown, Depth+1, Query(Q, I));
      // For those bits in RHS that are known, we can propagate them inverted
      // to known bits in V shifted to the right by C.
      RHSKnown.One.lshrInPlace(C);
      Known.Zero |= RHSKnown.One;
      RHSKnown.Zero.lshrInPlace(C);
      Known.One  |= RHSKnown.Zero;
    // assume(v >> c = a)
    } else if (match(Arg,
                     m_c_ICmp(Pred, m_Shr(m_V, m_ConstantInt(C)),
                              m_Value(A))) &&
               Pred == ICmpInst::ICMP_EQ &&
               isValidAssumeForContext(I, Q.CxtI, Q.DT) &&
               C < BitWidth) {
      KnownBits RHSKnown(BitWidth);
      computeKnownBits(A, RHSKnown, Depth+1, Query(Q, I));
      // For those bits in RHS that are known, we can propagate them to known
      // bits in V shifted to the right by C.
      Known.Zero |= RHSKnown.Zero << C;
      Known.One  |= RHSKnown.One  << C;
    // assume(~(v >> c) = a)
    } else if (match(Arg, m_c_ICmp(Pred, m_Not(m_Shr(m_V, m_ConstantInt(C))),
                                   m_Value(A))) &&
               Pred == ICmpInst::ICMP_EQ &&
               isValidAssumeForContext(I, Q.CxtI, Q.DT) &&
               C < BitWidth) {
      KnownBits RHSKnown(BitWidth);
      computeKnownBits(A, RHSKnown, Depth+1, Query(Q, I));
      // For those bits in RHS that are known, we can propagate them inverted
      // to known bits in V shifted to the right by C.
      Known.Zero |= RHSKnown.One  << C;
      Known.One  |= RHSKnown.Zero << C;
    // assume(v >=_s c) where c is non-negative
    } else if (match(Arg, m_ICmp(Pred, m_V, m_Value(A))) &&
               Pred == ICmpInst::ICMP_SGE &&
               isValidAssumeForContext(I, Q.CxtI, Q.DT)) {
      KnownBits RHSKnown(BitWidth);
      computeKnownBits(A, RHSKnown, Depth+1, Query(Q, I));

      if (RHSKnown.isNonNegative()) {
        // We know that the sign bit is zero.
        Known.makeNonNegative();
      }
    // assume(v >_s c) where c is at least -1.
    } else if (match(Arg, m_ICmp(Pred, m_V, m_Value(A))) &&
               Pred == ICmpInst::ICMP_SGT &&
               isValidAssumeForContext(I, Q.CxtI, Q.DT)) {
      KnownBits RHSKnown(BitWidth);
      computeKnownBits(A, RHSKnown, Depth+1, Query(Q, I));

      if (RHSKnown.isAllOnes() || RHSKnown.isNonNegative()) {
        // We know that the sign bit is zero.
        Known.makeNonNegative();
      }
    // assume(v <=_s c) where c is negative
    } else if (match(Arg, m_ICmp(Pred, m_V, m_Value(A))) &&
               Pred == ICmpInst::ICMP_SLE &&
               isValidAssumeForContext(I, Q.CxtI, Q.DT)) {
      KnownBits RHSKnown(BitWidth);
      computeKnownBits(A, RHSKnown, Depth+1, Query(Q, I));

      if (RHSKnown.isNegative()) {
        // We know that the sign bit is one.
        Known.makeNegative();
      }
    // assume(v <_s c) where c is non-positive
    } else if (match(Arg, m_ICmp(Pred, m_V, m_Value(A))) &&
               Pred == ICmpInst::ICMP_SLT &&
               isValidAssumeForContext(I, Q.CxtI, Q.DT)) {
      KnownBits RHSKnown(BitWidth);
      computeKnownBits(A, RHSKnown, Depth+1, Query(Q, I));

      if (RHSKnown.isZero() || RHSKnown.isNegative()) {
        // We know that the sign bit is one.
        Known.makeNegative();
      }
    // assume(v <=_u c)
    } else if (match(Arg, m_ICmp(Pred, m_V, m_Value(A))) &&
               Pred == ICmpInst::ICMP_ULE &&
               isValidAssumeForContext(I, Q.CxtI, Q.DT)) {
      KnownBits RHSKnown(BitWidth);
      computeKnownBits(A, RHSKnown, Depth+1, Query(Q, I));

      // Whatever high bits in c are zero are known to be zero.
      Known.Zero.setHighBits(RHSKnown.countMinLeadingZeros());
      // assume(v <_u c)
    } else if (match(Arg, m_ICmp(Pred, m_V, m_Value(A))) &&
               Pred == ICmpInst::ICMP_ULT &&
               isValidAssumeForContext(I, Q.CxtI, Q.DT)) {
      KnownBits RHSKnown(BitWidth);
      computeKnownBits(A, RHSKnown, Depth+1, Query(Q, I));

      // If the RHS is known zero, then this assumption must be wrong (nothing
      // is unsigned less than zero). Signal a conflict and get out of here.
      if (RHSKnown.isZero()) {
        Known.Zero.setAllBits();
        Known.One.setAllBits();
        break;
      }

      // Whatever high bits in c are zero are known to be zero (if c is a power
      // of 2, then one more).
      if (isKnownToBeAPowerOfTwo(A, false, Depth + 1, Query(Q, I)))
        Known.Zero.setHighBits(RHSKnown.countMinLeadingZeros() + 1);
      else
        Known.Zero.setHighBits(RHSKnown.countMinLeadingZeros());
    }
  }

  // If assumptions conflict with each other or previous known bits, then we
  // have a logical fallacy. It's possible that the assumption is not reachable,
  // so this isn't a real bug. On the other hand, the program may have undefined
  // behavior, or we might have a bug in the compiler. We can't assert/crash, so
  // clear out the known bits, try to warn the user, and hope for the best.
  if (Known.Zero.intersects(Known.One)) {
    Known.resetAll();

    if (Q.ORE)
      Q.ORE->emit([&]() {
        auto *CxtI = const_cast<Instruction *>(Q.CxtI);
        return OptimizationRemarkAnalysis("value-tracking", "BadAssumption",
                                          CxtI)
               << "Detected conflicting code assumptions. Program may "
                  "have undefined behavior, or compiler may have "
                  "internal error.";
      });
  }
}

/// Compute known bits from a shift operator, including those with a
/// non-constant shift amount. Known is the output of this function. Known2 is a
/// pre-allocated temporary with the same bit width as Known. KZF and KOF are
/// operator-specific functions that, given the known-zero or known-one bits
/// respectively, and a shift amount, compute the implied known-zero or
/// known-one bits of the shift operator's result respectively for that shift
/// amount. The results from calling KZF and KOF are conservatively combined for
/// all permitted shift amounts.
static void computeKnownBitsFromShiftOperator(
    const Operator *I, KnownBits &Known, KnownBits &Known2,
    unsigned Depth, const Query &Q,
    function_ref<APInt(const APInt &, unsigned)> KZF,
    function_ref<APInt(const APInt &, unsigned)> KOF) {
  unsigned BitWidth = Known.getBitWidth();

  if (auto *SA = dyn_cast<ConstantInt>(I->getOperand(1))) {
    unsigned ShiftAmt = SA->getLimitedValue(BitWidth-1);

    computeKnownBits(I->getOperand(0), Known, Depth + 1, Q);
    Known.Zero = KZF(Known.Zero, ShiftAmt);
    Known.One  = KOF(Known.One, ShiftAmt);
    // If the known bits conflict, this must be an overflowing left shift, so
    // the shift result is poison. We can return anything we want. Choose 0 for
    // the best folding opportunity.
    if (Known.hasConflict())
      Known.setAllZero();

    return;
  }

  computeKnownBits(I->getOperand(1), Known, Depth + 1, Q);

  // If the shift amount could be greater than or equal to the bit-width of the
  // LHS, the value could be poison, but bail out because the check below is
  // expensive. TODO: Should we just carry on?
  if ((~Known.Zero).uge(BitWidth)) {
    Known.resetAll();
    return;
  }

  // Note: We cannot use Known.Zero.getLimitedValue() here, because if
  // BitWidth > 64 and any upper bits are known, we'll end up returning the
  // limit value (which implies all bits are known).
  uint64_t ShiftAmtKZ = Known.Zero.zextOrTrunc(64).getZExtValue();
  uint64_t ShiftAmtKO = Known.One.zextOrTrunc(64).getZExtValue();

  // It would be more-clearly correct to use the two temporaries for this
  // calculation. Reusing the APInts here to prevent unnecessary allocations.
  Known.resetAll();

  // If we know the shifter operand is nonzero, we can sometimes infer more
  // known bits. However this is expensive to compute, so be lazy about it and
  // only compute it when absolutely necessary.
  Optional<bool> ShifterOperandIsNonZero;

  // Early exit if we can't constrain any well-defined shift amount.
  if (!(ShiftAmtKZ & (PowerOf2Ceil(BitWidth) - 1)) &&
      !(ShiftAmtKO & (PowerOf2Ceil(BitWidth) - 1))) {
    ShifterOperandIsNonZero = isKnownNonZero(I->getOperand(1), Depth + 1, Q);
    if (!*ShifterOperandIsNonZero)
      return;
  }

  computeKnownBits(I->getOperand(0), Known2, Depth + 1, Q);

  Known.Zero.setAllBits();
  Known.One.setAllBits();
  for (unsigned ShiftAmt = 0; ShiftAmt < BitWidth; ++ShiftAmt) {
    // Combine the shifted known input bits only for those shift amounts
    // compatible with its known constraints.
    if ((ShiftAmt & ~ShiftAmtKZ) != ShiftAmt)
      continue;
    if ((ShiftAmt | ShiftAmtKO) != ShiftAmt)
      continue;
    // If we know the shifter is nonzero, we may be able to infer more known
    // bits. This check is sunk down as far as possible to avoid the expensive
    // call to isKnownNonZero if the cheaper checks above fail.
    if (ShiftAmt == 0) {
      if (!ShifterOperandIsNonZero.hasValue())
        ShifterOperandIsNonZero =
            isKnownNonZero(I->getOperand(1), Depth + 1, Q);
      if (*ShifterOperandIsNonZero)
        continue;
    }

    Known.Zero &= KZF(Known2.Zero, ShiftAmt);
    Known.One  &= KOF(Known2.One, ShiftAmt);
  }

  // If the known bits conflict, the result is poison. Return a 0 and hope the
  // caller can further optimize that.
  if (Known.hasConflict())
    Known.setAllZero();
}

static void computeKnownBitsFromOperator(const Operator *I, KnownBits &Known,
                                         unsigned Depth, const Query &Q) {
  unsigned BitWidth = Known.getBitWidth();

  KnownBits Known2(Known);
  switch (I->getOpcode()) {
  default: break;
  case Instruction::Load:
    if (MDNode *MD =
            Q.IIQ.getMetadata(cast<LoadInst>(I), LLVMContext::MD_range))
      computeKnownBitsFromRangeMetadata(*MD, Known);
    break;
  case Instruction::And: {
    // If either the LHS or the RHS are Zero, the result is zero.
    computeKnownBits(I->getOperand(1), Known, Depth + 1, Q);
    computeKnownBits(I->getOperand(0), Known2, Depth + 1, Q);

    // Output known-1 bits are only known if set in both the LHS & RHS.
    Known.One &= Known2.One;
    // Output known-0 are known to be clear if zero in either the LHS | RHS.
    Known.Zero |= Known2.Zero;

    // and(x, add (x, -1)) is a common idiom that always clears the low bit;
    // here we handle the more general case of adding any odd number by
    // matching the form add(x, add(x, y)) where y is odd.
    // TODO: This could be generalized to clearing any bit set in y where the
    // following bit is known to be unset in y.
    Value *X = nullptr, *Y = nullptr;
    if (!Known.Zero[0] && !Known.One[0] &&
        match(I, m_c_BinOp(m_Value(X), m_Add(m_Deferred(X), m_Value(Y))))) {
      Known2.resetAll();
      computeKnownBits(Y, Known2, Depth + 1, Q);
      if (Known2.countMinTrailingOnes() > 0)
        Known.Zero.setBit(0);
    }
    break;
  }
  case Instruction::Or:
    computeKnownBits(I->getOperand(1), Known, Depth + 1, Q);
    computeKnownBits(I->getOperand(0), Known2, Depth + 1, Q);

    // Output known-0 bits are only known if clear in both the LHS & RHS.
    Known.Zero &= Known2.Zero;
    // Output known-1 are known to be set if set in either the LHS | RHS.
    Known.One |= Known2.One;
    break;
  case Instruction::Xor: {
    computeKnownBits(I->getOperand(1), Known, Depth + 1, Q);
    computeKnownBits(I->getOperand(0), Known2, Depth + 1, Q);

    // Output known-0 bits are known if clear or set in both the LHS & RHS.
    APInt KnownZeroOut = (Known.Zero & Known2.Zero) | (Known.One & Known2.One);
    // Output known-1 are known to be set if set in only one of the LHS, RHS.
    Known.One = (Known.Zero & Known2.One) | (Known.One & Known2.Zero);
    Known.Zero = std::move(KnownZeroOut);
    break;
  }
  case Instruction::Mul: {
    bool NSW = Q.IIQ.hasNoSignedWrap(cast<OverflowingBinaryOperator>(I));
    computeKnownBitsMul(I->getOperand(0), I->getOperand(1), NSW, Known,
                        Known2, Depth, Q);
    break;
  }
  case Instruction::UDiv: {
    // For the purposes of computing leading zeros we can conservatively
    // treat a udiv as a logical right shift by the power of 2 known to
    // be less than the denominator.
    computeKnownBits(I->getOperand(0), Known2, Depth + 1, Q);
    unsigned LeadZ = Known2.countMinLeadingZeros();

    Known2.resetAll();
    computeKnownBits(I->getOperand(1), Known2, Depth + 1, Q);
    unsigned RHSMaxLeadingZeros = Known2.countMaxLeadingZeros();
    if (RHSMaxLeadingZeros != BitWidth)
      LeadZ = std::min(BitWidth, LeadZ + BitWidth - RHSMaxLeadingZeros - 1);

    Known.Zero.setHighBits(LeadZ);
    break;
  }
  case Instruction::Select: {
    const Value *LHS, *RHS;
    SelectPatternFlavor SPF = matchSelectPattern(I, LHS, RHS).Flavor;
    if (SelectPatternResult::isMinOrMax(SPF)) {
      computeKnownBits(RHS, Known, Depth + 1, Q);
      computeKnownBits(LHS, Known2, Depth + 1, Q);
    } else {
      computeKnownBits(I->getOperand(2), Known, Depth + 1, Q);
      computeKnownBits(I->getOperand(1), Known2, Depth + 1, Q);
    }

    unsigned MaxHighOnes = 0;
    unsigned MaxHighZeros = 0;
    if (SPF == SPF_SMAX) {
      // If both sides are negative, the result is negative.
      if (Known.isNegative() && Known2.isNegative())
        // We can derive a lower bound on the result by taking the max of the
        // leading one bits.
        MaxHighOnes =
            std::max(Known.countMinLeadingOnes(), Known2.countMinLeadingOnes());
      // If either side is non-negative, the result is non-negative.
      else if (Known.isNonNegative() || Known2.isNonNegative())
        MaxHighZeros = 1;
    } else if (SPF == SPF_SMIN) {
      // If both sides are non-negative, the result is non-negative.
      if (Known.isNonNegative() && Known2.isNonNegative())
        // We can derive an upper bound on the result by taking the max of the
        // leading zero bits.
        MaxHighZeros = std::max(Known.countMinLeadingZeros(),
                                Known2.countMinLeadingZeros());
      // If either side is negative, the result is negative.
      else if (Known.isNegative() || Known2.isNegative())
        MaxHighOnes = 1;
    } else if (SPF == SPF_UMAX) {
      // We can derive a lower bound on the result by taking the max of the
      // leading one bits.
      MaxHighOnes =
          std::max(Known.countMinLeadingOnes(), Known2.countMinLeadingOnes());
    } else if (SPF == SPF_UMIN) {
      // We can derive an upper bound on the result by taking the max of the
      // leading zero bits.
      MaxHighZeros =
          std::max(Known.countMinLeadingZeros(), Known2.countMinLeadingZeros());
    } else if (SPF == SPF_ABS) {
      // RHS from matchSelectPattern returns the negation part of abs pattern.
      // If the negate has an NSW flag we can assume the sign bit of the result
      // will be 0 because that makes abs(INT_MIN) undefined.
      if (Q.IIQ.hasNoSignedWrap(cast<Instruction>(RHS)))
        MaxHighZeros = 1;
    }

    // Only known if known in both the LHS and RHS.
    Known.One &= Known2.One;
    Known.Zero &= Known2.Zero;
    if (MaxHighOnes > 0)
      Known.One.setHighBits(MaxHighOnes);
    if (MaxHighZeros > 0)
      Known.Zero.setHighBits(MaxHighZeros);
    break;
  }
  case Instruction::FPTrunc:
  case Instruction::FPExt:
  case Instruction::FPToUI:
  case Instruction::FPToSI:
  case Instruction::SIToFP:
  case Instruction::UIToFP:
    break; // Can't work with floating point.
  case Instruction::PtrToInt:
  case Instruction::IntToPtr:
    // Fall through and handle them the same as zext/trunc.
    LLVM_FALLTHROUGH;
  case Instruction::ZExt:
  case Instruction::Trunc: {
    Type *SrcTy = I->getOperand(0)->getType();

    unsigned SrcBitWidth;
    // Note that we handle pointer operands here because of inttoptr/ptrtoint
    // which fall through here.
    Type *ScalarTy = SrcTy->getScalarType();
    SrcBitWidth = ScalarTy->isPointerTy() ?
      Q.DL.getIndexTypeSizeInBits(ScalarTy) :
      Q.DL.getTypeSizeInBits(ScalarTy);

    assert(SrcBitWidth && "SrcBitWidth can't be zero");
    Known = Known.zextOrTrunc(SrcBitWidth);
    computeKnownBits(I->getOperand(0), Known, Depth + 1, Q);
    Known = Known.zextOrTrunc(BitWidth);
    // Any top bits are known to be zero.
    if (BitWidth > SrcBitWidth)
      Known.Zero.setBitsFrom(SrcBitWidth);
    break;
  }
  case Instruction::BitCast: {
    Type *SrcTy = I->getOperand(0)->getType();
    if (SrcTy->isIntOrPtrTy() &&
        // TODO: For now, not handling conversions like:
        // (bitcast i64 %x to <2 x i32>)
        !I->getType()->isVectorTy()) {
      computeKnownBits(I->getOperand(0), Known, Depth + 1, Q);
      break;
    }
    break;
  }
  case Instruction::SExt: {
    // Compute the bits in the result that are not present in the input.
    unsigned SrcBitWidth = I->getOperand(0)->getType()->getScalarSizeInBits();

    Known = Known.trunc(SrcBitWidth);
    computeKnownBits(I->getOperand(0), Known, Depth + 1, Q);
    // If the sign bit of the input is known set or clear, then we know the
    // top bits of the result.
    Known = Known.sext(BitWidth);
    break;
  }
  case Instruction::Shl: {
    // (shl X, C1) & C2 == 0   iff   (X & C2 >>u C1) == 0
    bool NSW = Q.IIQ.hasNoSignedWrap(cast<OverflowingBinaryOperator>(I));
    auto KZF = [NSW](const APInt &KnownZero, unsigned ShiftAmt) {
      APInt KZResult = KnownZero << ShiftAmt;
      KZResult.setLowBits(ShiftAmt); // Low bits known 0.
      // If this shift has "nsw" keyword, then the result is either a poison
      // value or has the same sign bit as the first operand.
      if (NSW && KnownZero.isSignBitSet())
        KZResult.setSignBit();
      return KZResult;
    };

    auto KOF = [NSW](const APInt &KnownOne, unsigned ShiftAmt) {
      APInt KOResult = KnownOne << ShiftAmt;
      if (NSW && KnownOne.isSignBitSet())
        KOResult.setSignBit();
      return KOResult;
    };

    computeKnownBitsFromShiftOperator(I, Known, Known2, Depth, Q, KZF, KOF);
    break;
  }
  case Instruction::LShr: {
    // (lshr X, C1) & C2 == 0   iff  (-1 >> C1) & C2 == 0
    auto KZF = [](const APInt &KnownZero, unsigned ShiftAmt) {
      APInt KZResult = KnownZero.lshr(ShiftAmt);
      // High bits known zero.
      KZResult.setHighBits(ShiftAmt);
      return KZResult;
    };

    auto KOF = [](const APInt &KnownOne, unsigned ShiftAmt) {
      return KnownOne.lshr(ShiftAmt);
    };

    computeKnownBitsFromShiftOperator(I, Known, Known2, Depth, Q, KZF, KOF);
    break;
  }
  case Instruction::AShr: {
    // (ashr X, C1) & C2 == 0   iff  (-1 >> C1) & C2 == 0
    auto KZF = [](const APInt &KnownZero, unsigned ShiftAmt) {
      return KnownZero.ashr(ShiftAmt);
    };

    auto KOF = [](const APInt &KnownOne, unsigned ShiftAmt) {
      return KnownOne.ashr(ShiftAmt);
    };

    computeKnownBitsFromShiftOperator(I, Known, Known2, Depth, Q, KZF, KOF);
    break;
  }
  case Instruction::Sub: {
    bool NSW = Q.IIQ.hasNoSignedWrap(cast<OverflowingBinaryOperator>(I));
    computeKnownBitsAddSub(false, I->getOperand(0), I->getOperand(1), NSW,
                           Known, Known2, Depth, Q);
    break;
  }
  case Instruction::Add: {
    bool NSW = Q.IIQ.hasNoSignedWrap(cast<OverflowingBinaryOperator>(I));
    computeKnownBitsAddSub(true, I->getOperand(0), I->getOperand(1), NSW,
                           Known, Known2, Depth, Q);
    break;
  }
  case Instruction::SRem:
    if (ConstantInt *Rem = dyn_cast<ConstantInt>(I->getOperand(1))) {
      APInt RA = Rem->getValue().abs();
      if (RA.isPowerOf2()) {
        APInt LowBits = RA - 1;
        computeKnownBits(I->getOperand(0), Known2, Depth + 1, Q);

        // The low bits of the first operand are unchanged by the srem.
        Known.Zero = Known2.Zero & LowBits;
        Known.One = Known2.One & LowBits;

        // If the first operand is non-negative or has all low bits zero, then
        // the upper bits are all zero.
        if (Known2.isNonNegative() || LowBits.isSubsetOf(Known2.Zero))
          Known.Zero |= ~LowBits;

        // If the first operand is negative and not all low bits are zero, then
        // the upper bits are all one.
        if (Known2.isNegative() && LowBits.intersects(Known2.One))
          Known.One |= ~LowBits;

        assert((Known.Zero & Known.One) == 0 && "Bits known to be one AND zero?");
        break;
      }
    }

    // The sign bit is the LHS's sign bit, except when the result of the
    // remainder is zero.
    computeKnownBits(I->getOperand(0), Known2, Depth + 1, Q);
    // If it's known zero, our sign bit is also zero.
    if (Known2.isNonNegative())
      Known.makeNonNegative();

    break;
  case Instruction::URem: {
    if (ConstantInt *Rem = dyn_cast<ConstantInt>(I->getOperand(1))) {
      const APInt &RA = Rem->getValue();
      if (RA.isPowerOf2()) {
        APInt LowBits = (RA - 1);
        computeKnownBits(I->getOperand(0), Known, Depth + 1, Q);
        Known.Zero |= ~LowBits;
        Known.One &= LowBits;
        break;
      }
    }

    // Since the result is less than or equal to either operand, any leading
    // zero bits in either operand must also exist in the result.
    computeKnownBits(I->getOperand(0), Known, Depth + 1, Q);
    computeKnownBits(I->getOperand(1), Known2, Depth + 1, Q);

    unsigned Leaders =
        std::max(Known.countMinLeadingZeros(), Known2.countMinLeadingZeros());
    Known.resetAll();
    Known.Zero.setHighBits(Leaders);
    break;
  }

  case Instruction::Alloca: {
    const AllocaInst *AI = cast<AllocaInst>(I);
    unsigned Align = AI->getAlignment();
    if (Align == 0)
      Align = Q.DL.getABITypeAlignment(AI->getAllocatedType());

    if (Align > 0)
      Known.Zero.setLowBits(countTrailingZeros(Align));
    break;
  }
  case Instruction::GetElementPtr: {
    // Analyze all of the subscripts of this getelementptr instruction
    // to determine if we can prove known low zero bits.
    KnownBits LocalKnown(BitWidth);
    computeKnownBits(I->getOperand(0), LocalKnown, Depth + 1, Q);
    unsigned TrailZ = LocalKnown.countMinTrailingZeros();

    gep_type_iterator GTI = gep_type_begin(I);
    for (unsigned i = 1, e = I->getNumOperands(); i != e; ++i, ++GTI) {
      Value *Index = I->getOperand(i);
      if (StructType *STy = GTI.getStructTypeOrNull()) {
        // Handle struct member offset arithmetic.

        // Handle case when index is vector zeroinitializer
        Constant *CIndex = cast<Constant>(Index);
        if (CIndex->isZeroValue())
          continue;

        if (CIndex->getType()->isVectorTy())
          Index = CIndex->getSplatValue();

        unsigned Idx = cast<ConstantInt>(Index)->getZExtValue();
        const StructLayout *SL = Q.DL.getStructLayout(STy);
        uint64_t Offset = SL->getElementOffset(Idx);
        TrailZ = std::min<unsigned>(TrailZ,
                                    countTrailingZeros(Offset));
      } else {
        // Handle array index arithmetic.
        Type *IndexedTy = GTI.getIndexedType();
        if (!IndexedTy->isSized()) {
          TrailZ = 0;
          break;
        }
        unsigned GEPOpiBits = Index->getType()->getScalarSizeInBits();
        uint64_t TypeSize = Q.DL.getTypeAllocSize(IndexedTy);
        LocalKnown.Zero = LocalKnown.One = APInt(GEPOpiBits, 0);
        computeKnownBits(Index, LocalKnown, Depth + 1, Q);
        TrailZ = std::min(TrailZ,
                          unsigned(countTrailingZeros(TypeSize) +
                                   LocalKnown.countMinTrailingZeros()));
      }
    }

    Known.Zero.setLowBits(TrailZ);
    break;
  }
  case Instruction::PHI: {
    const PHINode *P = cast<PHINode>(I);
    // Handle the case of a simple two-predecessor recurrence PHI.
    // There's a lot more that could theoretically be done here, but
    // this is sufficient to catch some interesting cases.
    if (P->getNumIncomingValues() == 2) {
      for (unsigned i = 0; i != 2; ++i) {
        Value *L = P->getIncomingValue(i);
        Value *R = P->getIncomingValue(!i);
        Operator *LU = dyn_cast<Operator>(L);
        if (!LU)
          continue;
        unsigned Opcode = LU->getOpcode();
        // Check for operations that have the property that if
        // both their operands have low zero bits, the result
        // will have low zero bits.
        if (Opcode == Instruction::Add ||
            Opcode == Instruction::Sub ||
            Opcode == Instruction::And ||
            Opcode == Instruction::Or ||
            Opcode == Instruction::Mul) {
          Value *LL = LU->getOperand(0);
          Value *LR = LU->getOperand(1);
          // Find a recurrence.
          if (LL == I)
            L = LR;
          else if (LR == I)
            L = LL;
          else
            break;
          // Ok, we have a PHI of the form L op= R. Check for low
          // zero bits.
          computeKnownBits(R, Known2, Depth + 1, Q);

          // We need to take the minimum number of known bits
          KnownBits Known3(Known);
          computeKnownBits(L, Known3, Depth + 1, Q);

          Known.Zero.setLowBits(std::min(Known2.countMinTrailingZeros(),
                                         Known3.countMinTrailingZeros()));

          auto *OverflowOp = dyn_cast<OverflowingBinaryOperator>(LU);
          if (OverflowOp && Q.IIQ.hasNoSignedWrap(OverflowOp)) {
            // If initial value of recurrence is nonnegative, and we are adding
            // a nonnegative number with nsw, the result can only be nonnegative
            // or poison value regardless of the number of times we execute the
            // add in phi recurrence. If initial value is negative and we are
            // adding a negative number with nsw, the result can only be
            // negative or poison value. Similar arguments apply to sub and mul.
            //
            // (add non-negative, non-negative) --> non-negative
            // (add negative, negative) --> negative
            if (Opcode == Instruction::Add) {
              if (Known2.isNonNegative() && Known3.isNonNegative())
                Known.makeNonNegative();
              else if (Known2.isNegative() && Known3.isNegative())
                Known.makeNegative();
            }

            // (sub nsw non-negative, negative) --> non-negative
            // (sub nsw negative, non-negative) --> negative
            else if (Opcode == Instruction::Sub && LL == I) {
              if (Known2.isNonNegative() && Known3.isNegative())
                Known.makeNonNegative();
              else if (Known2.isNegative() && Known3.isNonNegative())
                Known.makeNegative();
            }

            // (mul nsw non-negative, non-negative) --> non-negative
            else if (Opcode == Instruction::Mul && Known2.isNonNegative() &&
                     Known3.isNonNegative())
              Known.makeNonNegative();
          }

          break;
        }
      }
    }

    // Unreachable blocks may have zero-operand PHI nodes.
    if (P->getNumIncomingValues() == 0)
      break;

    // Otherwise take the unions of the known bit sets of the operands,
    // taking conservative care to avoid excessive recursion.
    if (Depth < MaxDepth - 1 && !Known.Zero && !Known.One) {
      // Skip if every incoming value references to ourself.
      if (dyn_cast_or_null<UndefValue>(P->hasConstantValue()))
        break;

      Known.Zero.setAllBits();
      Known.One.setAllBits();
      for (Value *IncValue : P->incoming_values()) {
        // Skip direct self references.
        if (IncValue == P) continue;

        Known2 = KnownBits(BitWidth);
        // Recurse, but cap the recursion to one level, because we don't
        // want to waste time spinning around in loops.
        computeKnownBits(IncValue, Known2, MaxDepth - 1, Q);
        Known.Zero &= Known2.Zero;
        Known.One &= Known2.One;
        // If all bits have been ruled out, there's no need to check
        // more operands.
        if (!Known.Zero && !Known.One)
          break;
      }
    }
    break;
  }
  case Instruction::Call:
  case Instruction::Invoke:
    // If range metadata is attached to this call, set known bits from that,
    // and then intersect with known bits based on other properties of the
    // function.
    if (MDNode *MD =
            Q.IIQ.getMetadata(cast<Instruction>(I), LLVMContext::MD_range))
      computeKnownBitsFromRangeMetadata(*MD, Known);
    if (const Value *RV = ImmutableCallSite(I).getReturnedArgOperand()) {
      computeKnownBits(RV, Known2, Depth + 1, Q);
      Known.Zero |= Known2.Zero;
      Known.One |= Known2.One;
    }
    if (const IntrinsicInst *II = dyn_cast<IntrinsicInst>(I)) {
      switch (II->getIntrinsicID()) {
      default: break;
      case Intrinsic::bitreverse:
        computeKnownBits(I->getOperand(0), Known2, Depth + 1, Q);
        Known.Zero |= Known2.Zero.reverseBits();
        Known.One |= Known2.One.reverseBits();
        break;
      case Intrinsic::bswap:
        computeKnownBits(I->getOperand(0), Known2, Depth + 1, Q);
        Known.Zero |= Known2.Zero.byteSwap();
        Known.One |= Known2.One.byteSwap();
        break;
      case Intrinsic::ctlz: {
        computeKnownBits(I->getOperand(0), Known2, Depth + 1, Q);
        // If we have a known 1, its position is our upper bound.
        unsigned PossibleLZ = Known2.One.countLeadingZeros();
        // If this call is undefined for 0, the result will be less than 2^n.
        if (II->getArgOperand(1) == ConstantInt::getTrue(II->getContext()))
          PossibleLZ = std::min(PossibleLZ, BitWidth - 1);
        unsigned LowBits = Log2_32(PossibleLZ)+1;
        Known.Zero.setBitsFrom(LowBits);
        break;
      }
      case Intrinsic::cttz: {
        computeKnownBits(I->getOperand(0), Known2, Depth + 1, Q);
        // If we have a known 1, its position is our upper bound.
        unsigned PossibleTZ = Known2.One.countTrailingZeros();
        // If this call is undefined for 0, the result will be less than 2^n.
        if (II->getArgOperand(1) == ConstantInt::getTrue(II->getContext()))
          PossibleTZ = std::min(PossibleTZ, BitWidth - 1);
        unsigned LowBits = Log2_32(PossibleTZ)+1;
        Known.Zero.setBitsFrom(LowBits);
        break;
      }
      case Intrinsic::ctpop: {
        computeKnownBits(I->getOperand(0), Known2, Depth + 1, Q);
        // We can bound the space the count needs.  Also, bits known to be zero
        // can't contribute to the population.
        unsigned BitsPossiblySet = Known2.countMaxPopulation();
        unsigned LowBits = Log2_32(BitsPossiblySet)+1;
        Known.Zero.setBitsFrom(LowBits);
        // TODO: we could bound KnownOne using the lower bound on the number
        // of bits which might be set provided by popcnt KnownOne2.
        break;
      }
      case Intrinsic::fshr:
      case Intrinsic::fshl: {
        const APInt *SA;
        if (!match(I->getOperand(2), m_APInt(SA)))
          break;

        // Normalize to funnel shift left.
        uint64_t ShiftAmt = SA->urem(BitWidth);
        if (II->getIntrinsicID() == Intrinsic::fshr)
          ShiftAmt = BitWidth - ShiftAmt;

        KnownBits Known3(Known);
        computeKnownBits(I->getOperand(0), Known2, Depth + 1, Q);
        computeKnownBits(I->getOperand(1), Known3, Depth + 1, Q);

        Known.Zero =
            Known2.Zero.shl(ShiftAmt) | Known3.Zero.lshr(BitWidth - ShiftAmt);
        Known.One =
            Known2.One.shl(ShiftAmt) | Known3.One.lshr(BitWidth - ShiftAmt);
        break;
      }
      case Intrinsic::x86_sse42_crc32_64_64:
        Known.Zero.setBitsFrom(32);
        break;
      }
    }
    break;
  case Instruction::ExtractElement:
    // Look through extract element. At the moment we keep this simple and skip
    // tracking the specific element. But at least we might find information
    // valid for all elements of the vector (for example if vector is sign
    // extended, shifted, etc).
    computeKnownBits(I->getOperand(0), Known, Depth + 1, Q);
    break;
  case Instruction::ExtractValue:
    if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(I->getOperand(0))) {
      const ExtractValueInst *EVI = cast<ExtractValueInst>(I);
      if (EVI->getNumIndices() != 1) break;
      if (EVI->getIndices()[0] == 0) {
        switch (II->getIntrinsicID()) {
        default: break;
        case Intrinsic::uadd_with_overflow:
        case Intrinsic::sadd_with_overflow:
          computeKnownBitsAddSub(true, II->getArgOperand(0),
                                 II->getArgOperand(1), false, Known, Known2,
                                 Depth, Q);
          break;
        case Intrinsic::usub_with_overflow:
        case Intrinsic::ssub_with_overflow:
          computeKnownBitsAddSub(false, II->getArgOperand(0),
                                 II->getArgOperand(1), false, Known, Known2,
                                 Depth, Q);
          break;
        case Intrinsic::umul_with_overflow:
        case Intrinsic::smul_with_overflow:
          computeKnownBitsMul(II->getArgOperand(0), II->getArgOperand(1), false,
                              Known, Known2, Depth, Q);
          break;
        }
      }
    }
  }
}

/// Determine which bits of V are known to be either zero or one and return
/// them.
KnownBits computeKnownBits(const Value *V, unsigned Depth, const Query &Q) {
  KnownBits Known(getBitWidth(V->getType(), Q.DL));
  computeKnownBits(V, Known, Depth, Q);
  return Known;
}

/// Determine which bits of V are known to be either zero or one and return
/// them in the Known bit set.
///
/// NOTE: we cannot consider 'undef' to be "IsZero" here.  The problem is that
/// we cannot optimize based on the assumption that it is zero without changing
/// it to be an explicit zero.  If we don't change it to zero, other code could
/// optimized based on the contradictory assumption that it is non-zero.
/// Because instcombine aggressively folds operations with undef args anyway,
/// this won't lose us code quality.
///
/// This function is defined on values with integer type, values with pointer
/// type, and vectors of integers.  In the case
/// where V is a vector, known zero, and known one values are the
/// same width as the vector element, and the bit is set only if it is true
/// for all of the elements in the vector.
void computeKnownBits(const Value *V, KnownBits &Known, unsigned Depth,
                      const Query &Q) {
  assert(V && "No Value?");
  assert(Depth <= MaxDepth && "Limit Search Depth");
  unsigned BitWidth = Known.getBitWidth();

  assert((V->getType()->isIntOrIntVectorTy(BitWidth) ||
          V->getType()->isPtrOrPtrVectorTy()) &&
         "Not integer or pointer type!");

  Type *ScalarTy = V->getType()->getScalarType();
  unsigned ExpectedWidth = ScalarTy->isPointerTy() ?
    Q.DL.getIndexTypeSizeInBits(ScalarTy) : Q.DL.getTypeSizeInBits(ScalarTy);
  assert(ExpectedWidth == BitWidth && "V and Known should have same BitWidth");
  (void)BitWidth;
  (void)ExpectedWidth;

  const APInt *C;
  if (match(V, m_APInt(C))) {
    // We know all of the bits for a scalar constant or a splat vector constant!
    Known.One = *C;
    Known.Zero = ~Known.One;
    return;
  }
  // Null and aggregate-zero are all-zeros.
  if (isa<ConstantPointerNull>(V) || isa<ConstantAggregateZero>(V)) {
    Known.setAllZero();
    return;
  }
  // Handle a constant vector by taking the intersection of the known bits of
  // each element.
  if (const ConstantDataSequential *CDS = dyn_cast<ConstantDataSequential>(V)) {
    // We know that CDS must be a vector of integers. Take the intersection of
    // each element.
    Known.Zero.setAllBits(); Known.One.setAllBits();
    for (unsigned i = 0, e = CDS->getNumElements(); i != e; ++i) {
      APInt Elt = CDS->getElementAsAPInt(i);
      Known.Zero &= ~Elt;
      Known.One &= Elt;
    }
    return;
  }

  if (const auto *CV = dyn_cast<ConstantVector>(V)) {
    // We know that CV must be a vector of integers. Take the intersection of
    // each element.
    Known.Zero.setAllBits(); Known.One.setAllBits();
    for (unsigned i = 0, e = CV->getNumOperands(); i != e; ++i) {
      Constant *Element = CV->getAggregateElement(i);
      auto *ElementCI = dyn_cast_or_null<ConstantInt>(Element);
      if (!ElementCI) {
        Known.resetAll();
        return;
      }
      const APInt &Elt = ElementCI->getValue();
      Known.Zero &= ~Elt;
      Known.One &= Elt;
    }
    return;
  }

  // Start out not knowing anything.
  Known.resetAll();

  // We can't imply anything about undefs.
  if (isa<UndefValue>(V))
    return;

  // There's no point in looking through other users of ConstantData for
  // assumptions.  Confirm that we've handled them all.
  assert(!isa<ConstantData>(V) && "Unhandled constant data!");

  // Limit search depth.
  // All recursive calls that increase depth must come after this.
  if (Depth == MaxDepth)
    return;

  // A weak GlobalAlias is totally unknown. A non-weak GlobalAlias has
  // the bits of its aliasee.
  if (const GlobalAlias *GA = dyn_cast<GlobalAlias>(V)) {
    if (!GA->isInterposable())
      computeKnownBits(GA->getAliasee(), Known, Depth + 1, Q);
    return;
  }

  if (const Operator *I = dyn_cast<Operator>(V))
    computeKnownBitsFromOperator(I, Known, Depth, Q);

  // Aligned pointers have trailing zeros - refine Known.Zero set
  if (V->getType()->isPointerTy()) {
    unsigned Align = V->getPointerAlignment(Q.DL);
    if (Align)
      Known.Zero.setLowBits(countTrailingZeros(Align));
  }

  // computeKnownBitsFromAssume strictly refines Known.
  // Therefore, we run them after computeKnownBitsFromOperator.

  // Check whether a nearby assume intrinsic can determine some known bits.
  computeKnownBitsFromAssume(V, Known, Depth, Q);

  assert((Known.Zero & Known.One) == 0 && "Bits known to be one AND zero?");
}

/// Return true if the given value is known to have exactly one
/// bit set when defined. For vectors return true if every element is known to
/// be a power of two when defined. Supports values with integer or pointer
/// types and vectors of integers.
bool isKnownToBeAPowerOfTwo(const Value *V, bool OrZero, unsigned Depth,
                            const Query &Q) {
  assert(Depth <= MaxDepth && "Limit Search Depth");

  // Attempt to match against constants.
  if (OrZero && match(V, m_Power2OrZero()))
      return true;
  if (match(V, m_Power2()))
      return true;

  // 1 << X is clearly a power of two if the one is not shifted off the end.  If
  // it is shifted off the end then the result is undefined.
  if (match(V, m_Shl(m_One(), m_Value())))
    return true;

  // (signmask) >>l X is clearly a power of two if the one is not shifted off
  // the bottom.  If it is shifted off the bottom then the result is undefined.
  if (match(V, m_LShr(m_SignMask(), m_Value())))
    return true;

  // The remaining tests are all recursive, so bail out if we hit the limit.
  if (Depth++ == MaxDepth)
    return false;

  Value *X = nullptr, *Y = nullptr;
  // A shift left or a logical shift right of a power of two is a power of two
  // or zero.
  if (OrZero && (match(V, m_Shl(m_Value(X), m_Value())) ||
                 match(V, m_LShr(m_Value(X), m_Value()))))
    return isKnownToBeAPowerOfTwo(X, /*OrZero*/ true, Depth, Q);

  if (const ZExtInst *ZI = dyn_cast<ZExtInst>(V))
    return isKnownToBeAPowerOfTwo(ZI->getOperand(0), OrZero, Depth, Q);

  if (const SelectInst *SI = dyn_cast<SelectInst>(V))
    return isKnownToBeAPowerOfTwo(SI->getTrueValue(), OrZero, Depth, Q) &&
           isKnownToBeAPowerOfTwo(SI->getFalseValue(), OrZero, Depth, Q);

  if (OrZero && match(V, m_And(m_Value(X), m_Value(Y)))) {
    // A power of two and'd with anything is a power of two or zero.
    if (isKnownToBeAPowerOfTwo(X, /*OrZero*/ true, Depth, Q) ||
        isKnownToBeAPowerOfTwo(Y, /*OrZero*/ true, Depth, Q))
      return true;
    // X & (-X) is always a power of two or zero.
    if (match(X, m_Neg(m_Specific(Y))) || match(Y, m_Neg(m_Specific(X))))
      return true;
    return false;
  }

  // Adding a power-of-two or zero to the same power-of-two or zero yields
  // either the original power-of-two, a larger power-of-two or zero.
  if (match(V, m_Add(m_Value(X), m_Value(Y)))) {
    const OverflowingBinaryOperator *VOBO = cast<OverflowingBinaryOperator>(V);
    if (OrZero || Q.IIQ.hasNoUnsignedWrap(VOBO) ||
        Q.IIQ.hasNoSignedWrap(VOBO)) {
      if (match(X, m_And(m_Specific(Y), m_Value())) ||
          match(X, m_And(m_Value(), m_Specific(Y))))
        if (isKnownToBeAPowerOfTwo(Y, OrZero, Depth, Q))
          return true;
      if (match(Y, m_And(m_Specific(X), m_Value())) ||
          match(Y, m_And(m_Value(), m_Specific(X))))
        if (isKnownToBeAPowerOfTwo(X, OrZero, Depth, Q))
          return true;

      unsigned BitWidth = V->getType()->getScalarSizeInBits();
      KnownBits LHSBits(BitWidth);
      computeKnownBits(X, LHSBits, Depth, Q);

      KnownBits RHSBits(BitWidth);
      computeKnownBits(Y, RHSBits, Depth, Q);
      // If i8 V is a power of two or zero:
      //  ZeroBits: 1 1 1 0 1 1 1 1
      // ~ZeroBits: 0 0 0 1 0 0 0 0
      if ((~(LHSBits.Zero & RHSBits.Zero)).isPowerOf2())
        // If OrZero isn't set, we cannot give back a zero result.
        // Make sure either the LHS or RHS has a bit set.
        if (OrZero || RHSBits.One.getBoolValue() || LHSBits.One.getBoolValue())
          return true;
    }
  }

  // An exact divide or right shift can only shift off zero bits, so the result
  // is a power of two only if the first operand is a power of two and not
  // copying a sign bit (sdiv int_min, 2).
  if (match(V, m_Exact(m_LShr(m_Value(), m_Value()))) ||
      match(V, m_Exact(m_UDiv(m_Value(), m_Value())))) {
    return isKnownToBeAPowerOfTwo(cast<Operator>(V)->getOperand(0), OrZero,
                                  Depth, Q);
  }

  return false;
}

/// Test whether a GEP's result is known to be non-null.
///
/// Uses properties inherent in a GEP to try to determine whether it is known
/// to be non-null.
///
/// Currently this routine does not support vector GEPs.
static bool isGEPKnownNonNull(const GEPOperator *GEP, unsigned Depth,
                              const Query &Q) {
  const Function *F = nullptr;
  if (const Instruction *I = dyn_cast<Instruction>(GEP))
    F = I->getFunction();

  if (!GEP->isInBounds() ||
      NullPointerIsDefined(F, GEP->getPointerAddressSpace()))
    return false;

  // FIXME: Support vector-GEPs.
  assert(GEP->getType()->isPointerTy() && "We only support plain pointer GEP");

  // If the base pointer is non-null, we cannot walk to a null address with an
  // inbounds GEP in address space zero.
  if (isKnownNonZero(GEP->getPointerOperand(), Depth, Q))
    return true;

  // Walk the GEP operands and see if any operand introduces a non-zero offset.
  // If so, then the GEP cannot produce a null pointer, as doing so would
  // inherently violate the inbounds contract within address space zero.
  for (gep_type_iterator GTI = gep_type_begin(GEP), GTE = gep_type_end(GEP);
       GTI != GTE; ++GTI) {
    // Struct types are easy -- they must always be indexed by a constant.
    if (StructType *STy = GTI.getStructTypeOrNull()) {
      ConstantInt *OpC = cast<ConstantInt>(GTI.getOperand());
      unsigned ElementIdx = OpC->getZExtValue();
      const StructLayout *SL = Q.DL.getStructLayout(STy);
      uint64_t ElementOffset = SL->getElementOffset(ElementIdx);
      if (ElementOffset > 0)
        return true;
      continue;
    }

    // If we have a zero-sized type, the index doesn't matter. Keep looping.
    if (Q.DL.getTypeAllocSize(GTI.getIndexedType()) == 0)
      continue;

    // Fast path the constant operand case both for efficiency and so we don't
    // increment Depth when just zipping down an all-constant GEP.
    if (ConstantInt *OpC = dyn_cast<ConstantInt>(GTI.getOperand())) {
      if (!OpC->isZero())
        return true;
      continue;
    }

    // We post-increment Depth here because while isKnownNonZero increments it
    // as well, when we pop back up that increment won't persist. We don't want
    // to recurse 10k times just because we have 10k GEP operands. We don't
    // bail completely out because we want to handle constant GEPs regardless
    // of depth.
    if (Depth++ >= MaxDepth)
      continue;

    if (isKnownNonZero(GTI.getOperand(), Depth, Q))
      return true;
  }

  return false;
}

static bool isKnownNonNullFromDominatingCondition(const Value *V,
                                                  const Instruction *CtxI,
                                                  const DominatorTree *DT) {
  assert(V->getType()->isPointerTy() && "V must be pointer type");
  assert(!isa<ConstantData>(V) && "Did not expect ConstantPointerNull");

  if (!CtxI || !DT)
    return false;

  unsigned NumUsesExplored = 0;
  for (auto *U : V->users()) {
    // Avoid massive lists
    if (NumUsesExplored >= DomConditionsMaxUses)
      break;
    NumUsesExplored++;

    // If the value is used as an argument to a call or invoke, then argument
    // attributes may provide an answer about null-ness.
    if (auto CS = ImmutableCallSite(U))
      if (auto *CalledFunc = CS.getCalledFunction())
        for (const Argument &Arg : CalledFunc->args())
          if (CS.getArgOperand(Arg.getArgNo()) == V &&
              Arg.hasNonNullAttr() && DT->dominates(CS.getInstruction(), CtxI))
            return true;

    // Consider only compare instructions uniquely controlling a branch
    CmpInst::Predicate Pred;
    if (!match(const_cast<User *>(U),
               m_c_ICmp(Pred, m_Specific(V), m_Zero())) ||
        (Pred != ICmpInst::ICMP_EQ && Pred != ICmpInst::ICMP_NE))
      continue;

    SmallVector<const User *, 4> WorkList;
    SmallPtrSet<const User *, 4> Visited;
    for (auto *CmpU : U->users()) {
      assert(WorkList.empty() && "Should be!");
      if (Visited.insert(CmpU).second)
        WorkList.push_back(CmpU);

      while (!WorkList.empty()) {
        auto *Curr = WorkList.pop_back_val();

        // If a user is an AND, add all its users to the work list. We only
        // propagate "pred != null" condition through AND because it is only
        // correct to assume that all conditions of AND are met in true branch.
        // TODO: Support similar logic of OR and EQ predicate?
        if (Pred == ICmpInst::ICMP_NE)
          if (auto *BO = dyn_cast<BinaryOperator>(Curr))
            if (BO->getOpcode() == Instruction::And) {
              for (auto *BOU : BO->users())
                if (Visited.insert(BOU).second)
                  WorkList.push_back(BOU);
              continue;
            }

        if (const BranchInst *BI = dyn_cast<BranchInst>(Curr)) {
          assert(BI->isConditional() && "uses a comparison!");

          BasicBlock *NonNullSuccessor =
              BI->getSuccessor(Pred == ICmpInst::ICMP_EQ ? 1 : 0);
          BasicBlockEdge Edge(BI->getParent(), NonNullSuccessor);
          if (Edge.isSingleEdge() && DT->dominates(Edge, CtxI->getParent()))
            return true;
        } else if (Pred == ICmpInst::ICMP_NE && isGuard(Curr) &&
                   DT->dominates(cast<Instruction>(Curr), CtxI)) {
          return true;
        }
      }
    }
  }

  return false;
}

/// Does the 'Range' metadata (which must be a valid MD_range operand list)
/// ensure that the value it's attached to is never Value?  'RangeType' is
/// is the type of the value described by the range.
static bool rangeMetadataExcludesValue(const MDNode* Ranges, const APInt& Value) {
  const unsigned NumRanges = Ranges->getNumOperands() / 2;
  assert(NumRanges >= 1);
  for (unsigned i = 0; i < NumRanges; ++i) {
    ConstantInt *Lower =
        mdconst::extract<ConstantInt>(Ranges->getOperand(2 * i + 0));
    ConstantInt *Upper =
        mdconst::extract<ConstantInt>(Ranges->getOperand(2 * i + 1));
    ConstantRange Range(Lower->getValue(), Upper->getValue());
    if (Range.contains(Value))
      return false;
  }
  return true;
}

/// Return true if the given value is known to be non-zero when defined. For
/// vectors, return true if every element is known to be non-zero when
/// defined. For pointers, if the context instruction and dominator tree are
/// specified, perform context-sensitive analysis and return true if the
/// pointer couldn't possibly be null at the specified instruction.
/// Supports values with integer or pointer type and vectors of integers.
bool isKnownNonZero(const Value *V, unsigned Depth, const Query &Q) {
  if (auto *C = dyn_cast<Constant>(V)) {
    if (C->isNullValue())
      return false;
    if (isa<ConstantInt>(C))
      // Must be non-zero due to null test above.
      return true;

    // For constant vectors, check that all elements are undefined or known
    // non-zero to determine that the whole vector is known non-zero.
    if (auto *VecTy = dyn_cast<VectorType>(C->getType())) {
      for (unsigned i = 0, e = VecTy->getNumElements(); i != e; ++i) {
        Constant *Elt = C->getAggregateElement(i);
        if (!Elt || Elt->isNullValue())
          return false;
        if (!isa<UndefValue>(Elt) && !isa<ConstantInt>(Elt))
          return false;
      }
      return true;
    }

    // A global variable in address space 0 is non null unless extern weak
    // or an absolute symbol reference. Other address spaces may have null as a
    // valid address for a global, so we can't assume anything.
    if (const GlobalValue *GV = dyn_cast<GlobalValue>(V)) {
      if (!GV->isAbsoluteSymbolRef() && !GV->hasExternalWeakLinkage() &&
          GV->getType()->getAddressSpace() == 0)
        return true;
    } else
      return false;
  }

  if (auto *I = dyn_cast<Instruction>(V)) {
    if (MDNode *Ranges = Q.IIQ.getMetadata(I, LLVMContext::MD_range)) {
      // If the possible ranges don't contain zero, then the value is
      // definitely non-zero.
      if (auto *Ty = dyn_cast<IntegerType>(V->getType())) {
        const APInt ZeroValue(Ty->getBitWidth(), 0);
        if (rangeMetadataExcludesValue(Ranges, ZeroValue))
          return true;
      }
    }
  }

  // Some of the tests below are recursive, so bail out if we hit the limit.
  if (Depth++ >= MaxDepth)
    return false;

  // Check for pointer simplifications.
  if (V->getType()->isPointerTy()) {
    // Alloca never returns null, malloc might.
    if (isa<AllocaInst>(V) && Q.DL.getAllocaAddrSpace() == 0)
      return true;

    // A byval, inalloca, or nonnull argument is never null.
    if (const Argument *A = dyn_cast<Argument>(V))
      if (A->hasByValOrInAllocaAttr() || A->hasNonNullAttr())
        return true;

    // A Load tagged with nonnull metadata is never null.
    if (const LoadInst *LI = dyn_cast<LoadInst>(V))
      if (Q.IIQ.getMetadata(LI, LLVMContext::MD_nonnull))
        return true;

    if (const auto *Call = dyn_cast<CallBase>(V)) {
      if (Call->isReturnNonNull())
        return true;
      if (const auto *RP = getArgumentAliasingToReturnedPointer(Call))
        return isKnownNonZero(RP, Depth, Q);
    }
  }


  // Check for recursive pointer simplifications.
  if (V->getType()->isPointerTy()) {
    if (isKnownNonNullFromDominatingCondition(V, Q.CxtI, Q.DT))
      return true;

    if (const GEPOperator *GEP = dyn_cast<GEPOperator>(V))
      if (isGEPKnownNonNull(GEP, Depth, Q))
        return true;
  }

  unsigned BitWidth = getBitWidth(V->getType()->getScalarType(), Q.DL);

  // X | Y != 0 if X != 0 or Y != 0.
  Value *X = nullptr, *Y = nullptr;
  if (match(V, m_Or(m_Value(X), m_Value(Y))))
    return isKnownNonZero(X, Depth, Q) || isKnownNonZero(Y, Depth, Q);

  // ext X != 0 if X != 0.
  if (isa<SExtInst>(V) || isa<ZExtInst>(V))
    return isKnownNonZero(cast<Instruction>(V)->getOperand(0), Depth, Q);

  // shl X, Y != 0 if X is odd.  Note that the value of the shift is undefined
  // if the lowest bit is shifted off the end.
  if (match(V, m_Shl(m_Value(X), m_Value(Y)))) {
    // shl nuw can't remove any non-zero bits.
    const OverflowingBinaryOperator *BO = cast<OverflowingBinaryOperator>(V);
    if (Q.IIQ.hasNoUnsignedWrap(BO))
      return isKnownNonZero(X, Depth, Q);

    KnownBits Known(BitWidth);
    computeKnownBits(X, Known, Depth, Q);
    if (Known.One[0])
      return true;
  }
  // shr X, Y != 0 if X is negative.  Note that the value of the shift is not
  // defined if the sign bit is shifted off the end.
  else if (match(V, m_Shr(m_Value(X), m_Value(Y)))) {
    // shr exact can only shift out zero bits.
    const PossiblyExactOperator *BO = cast<PossiblyExactOperator>(V);
    if (BO->isExact())
      return isKnownNonZero(X, Depth, Q);

    KnownBits Known = computeKnownBits(X, Depth, Q);
    if (Known.isNegative())
      return true;

    // If the shifter operand is a constant, and all of the bits shifted
    // out are known to be zero, and X is known non-zero then at least one
    // non-zero bit must remain.
    if (ConstantInt *Shift = dyn_cast<ConstantInt>(Y)) {
      auto ShiftVal = Shift->getLimitedValue(BitWidth - 1);
      // Is there a known one in the portion not shifted out?
      if (Known.countMaxLeadingZeros() < BitWidth - ShiftVal)
        return true;
      // Are all the bits to be shifted out known zero?
      if (Known.countMinTrailingZeros() >= ShiftVal)
        return isKnownNonZero(X, Depth, Q);
    }
  }
  // div exact can only produce a zero if the dividend is zero.
  else if (match(V, m_Exact(m_IDiv(m_Value(X), m_Value())))) {
    return isKnownNonZero(X, Depth, Q);
  }
  // X + Y.
  else if (match(V, m_Add(m_Value(X), m_Value(Y)))) {
    KnownBits XKnown = computeKnownBits(X, Depth, Q);
    KnownBits YKnown = computeKnownBits(Y, Depth, Q);

    // If X and Y are both non-negative (as signed values) then their sum is not
    // zero unless both X and Y are zero.
    if (XKnown.isNonNegative() && YKnown.isNonNegative())
      if (isKnownNonZero(X, Depth, Q) || isKnownNonZero(Y, Depth, Q))
        return true;

    // If X and Y are both negative (as signed values) then their sum is not
    // zero unless both X and Y equal INT_MIN.
    if (XKnown.isNegative() && YKnown.isNegative()) {
      APInt Mask = APInt::getSignedMaxValue(BitWidth);
      // The sign bit of X is set.  If some other bit is set then X is not equal
      // to INT_MIN.
      if (XKnown.One.intersects(Mask))
        return true;
      // The sign bit of Y is set.  If some other bit is set then Y is not equal
      // to INT_MIN.
      if (YKnown.One.intersects(Mask))
        return true;
    }

    // The sum of a non-negative number and a power of two is not zero.
    if (XKnown.isNonNegative() &&
        isKnownToBeAPowerOfTwo(Y, /*OrZero*/ false, Depth, Q))
      return true;
    if (YKnown.isNonNegative() &&
        isKnownToBeAPowerOfTwo(X, /*OrZero*/ false, Depth, Q))
      return true;
  }
  // X * Y.
  else if (match(V, m_Mul(m_Value(X), m_Value(Y)))) {
    const OverflowingBinaryOperator *BO = cast<OverflowingBinaryOperator>(V);
    // If X and Y are non-zero then so is X * Y as long as the multiplication
    // does not overflow.
    if ((Q.IIQ.hasNoSignedWrap(BO) || Q.IIQ.hasNoUnsignedWrap(BO)) &&
        isKnownNonZero(X, Depth, Q) && isKnownNonZero(Y, Depth, Q))
      return true;
  }
  // (C ? X : Y) != 0 if X != 0 and Y != 0.
  else if (const SelectInst *SI = dyn_cast<SelectInst>(V)) {
    if (isKnownNonZero(SI->getTrueValue(), Depth, Q) &&
        isKnownNonZero(SI->getFalseValue(), Depth, Q))
      return true;
  }
  // PHI
  else if (const PHINode *PN = dyn_cast<PHINode>(V)) {
    // Try and detect a recurrence that monotonically increases from a
    // starting value, as these are common as induction variables.
    if (PN->getNumIncomingValues() == 2) {
      Value *Start = PN->getIncomingValue(0);
      Value *Induction = PN->getIncomingValue(1);
      if (isa<ConstantInt>(Induction) && !isa<ConstantInt>(Start))
        std::swap(Start, Induction);
      if (ConstantInt *C = dyn_cast<ConstantInt>(Start)) {
        if (!C->isZero() && !C->isNegative()) {
          ConstantInt *X;
          if (Q.IIQ.UseInstrInfo &&
              (match(Induction, m_NSWAdd(m_Specific(PN), m_ConstantInt(X))) ||
               match(Induction, m_NUWAdd(m_Specific(PN), m_ConstantInt(X)))) &&
              !X->isNegative())
            return true;
        }
      }
    }
    // Check if all incoming values are non-zero constant.
    bool AllNonZeroConstants = llvm::all_of(PN->operands(), [](Value *V) {
      return isa<ConstantInt>(V) && !cast<ConstantInt>(V)->isZero();
    });
    if (AllNonZeroConstants)
      return true;
  }

  KnownBits Known(BitWidth);
  computeKnownBits(V, Known, Depth, Q);
  return Known.One != 0;
}

/// Return true if V2 == V1 + X, where X is known non-zero.
static bool isAddOfNonZero(const Value *V1, const Value *V2, const Query &Q) {
  const BinaryOperator *BO = dyn_cast<BinaryOperator>(V1);
  if (!BO || BO->getOpcode() != Instruction::Add)
    return false;
  Value *Op = nullptr;
  if (V2 == BO->getOperand(0))
    Op = BO->getOperand(1);
  else if (V2 == BO->getOperand(1))
    Op = BO->getOperand(0);
  else
    return false;
  return isKnownNonZero(Op, 0, Q);
}

/// Return true if it is known that V1 != V2.
static bool isKnownNonEqual(const Value *V1, const Value *V2, const Query &Q) {
  if (V1 == V2)
    return false;
  if (V1->getType() != V2->getType())
    // We can't look through casts yet.
    return false;
  if (isAddOfNonZero(V1, V2, Q) || isAddOfNonZero(V2, V1, Q))
    return true;

  if (V1->getType()->isIntOrIntVectorTy()) {
    // Are any known bits in V1 contradictory to known bits in V2? If V1
    // has a known zero where V2 has a known one, they must not be equal.
    KnownBits Known1 = computeKnownBits(V1, 0, Q);
    KnownBits Known2 = computeKnownBits(V2, 0, Q);

    if (Known1.Zero.intersects(Known2.One) ||
        Known2.Zero.intersects(Known1.One))
      return true;
  }
  return false;
}

/// Return true if 'V & Mask' is known to be zero.  We use this predicate to
/// simplify operations downstream. Mask is known to be zero for bits that V
/// cannot have.
///
/// This function is defined on values with integer type, values with pointer
/// type, and vectors of integers.  In the case
/// where V is a vector, the mask, known zero, and known one values are the
/// same width as the vector element, and the bit is set only if it is true
/// for all of the elements in the vector.
bool MaskedValueIsZero(const Value *V, const APInt &Mask, unsigned Depth,
                       const Query &Q) {
  KnownBits Known(Mask.getBitWidth());
  computeKnownBits(V, Known, Depth, Q);
  return Mask.isSubsetOf(Known.Zero);
}

// Match a signed min+max clamp pattern like smax(smin(In, CHigh), CLow).
// Returns the input and lower/upper bounds.
static bool isSignedMinMaxClamp(const Value *Select, const Value *&In,
                                const APInt *&CLow, const APInt *&CHigh) {
  assert(isa<Operator>(Select) &&
         cast<Operator>(Select)->getOpcode() == Instruction::Select &&
         "Input should be a Select!");

  const Value *LHS, *RHS, *LHS2, *RHS2;
  SelectPatternFlavor SPF = matchSelectPattern(Select, LHS, RHS).Flavor;
  if (SPF != SPF_SMAX && SPF != SPF_SMIN)
    return false;

  if (!match(RHS, m_APInt(CLow)))
    return false;

  SelectPatternFlavor SPF2 = matchSelectPattern(LHS, LHS2, RHS2).Flavor;
  if (getInverseMinMaxFlavor(SPF) != SPF2)
    return false;

  if (!match(RHS2, m_APInt(CHigh)))
    return false;

  if (SPF == SPF_SMIN)
    std::swap(CLow, CHigh);

  In = LHS2;
  return CLow->sle(*CHigh);
}

/// For vector constants, loop over the elements and find the constant with the
/// minimum number of sign bits. Return 0 if the value is not a vector constant
/// or if any element was not analyzed; otherwise, return the count for the
/// element with the minimum number of sign bits.
static unsigned computeNumSignBitsVectorConstant(const Value *V,
                                                 unsigned TyBits) {
  const auto *CV = dyn_cast<Constant>(V);
  if (!CV || !CV->getType()->isVectorTy())
    return 0;

  unsigned MinSignBits = TyBits;
  unsigned NumElts = CV->getType()->getVectorNumElements();
  for (unsigned i = 0; i != NumElts; ++i) {
    // If we find a non-ConstantInt, bail out.
    auto *Elt = dyn_cast_or_null<ConstantInt>(CV->getAggregateElement(i));
    if (!Elt)
      return 0;

    MinSignBits = std::min(MinSignBits, Elt->getValue().getNumSignBits());
  }

  return MinSignBits;
}

static unsigned ComputeNumSignBitsImpl(const Value *V, unsigned Depth,
                                       const Query &Q);

static unsigned ComputeNumSignBits(const Value *V, unsigned Depth,
                                   const Query &Q) {
  unsigned Result = ComputeNumSignBitsImpl(V, Depth, Q);
  assert(Result > 0 && "At least one sign bit needs to be present!");
  return Result;
}

/// Return the number of times the sign bit of the register is replicated into
/// the other bits. We know that at least 1 bit is always equal to the sign bit
/// (itself), but other cases can give us information. For example, immediately
/// after an "ashr X, 2", we know that the top 3 bits are all equal to each
/// other, so we return 3. For vectors, return the number of sign bits for the
/// vector element with the minimum number of known sign bits.
static unsigned ComputeNumSignBitsImpl(const Value *V, unsigned Depth,
                                       const Query &Q) {
  assert(Depth <= MaxDepth && "Limit Search Depth");

  // We return the minimum number of sign bits that are guaranteed to be present
  // in V, so for undef we have to conservatively return 1.  We don't have the
  // same behavior for poison though -- that's a FIXME today.

  Type *ScalarTy = V->getType()->getScalarType();
  unsigned TyBits = ScalarTy->isPointerTy() ?
    Q.DL.getIndexTypeSizeInBits(ScalarTy) :
    Q.DL.getTypeSizeInBits(ScalarTy);

  unsigned Tmp, Tmp2;
  unsigned FirstAnswer = 1;

  // Note that ConstantInt is handled by the general computeKnownBits case
  // below.

  if (Depth == MaxDepth)
    return 1;  // Limit search depth.

  const Operator *U = dyn_cast<Operator>(V);
  switch (Operator::getOpcode(V)) {
  default: break;
  case Instruction::SExt:
    Tmp = TyBits - U->getOperand(0)->getType()->getScalarSizeInBits();
    return ComputeNumSignBits(U->getOperand(0), Depth + 1, Q) + Tmp;

  case Instruction::SDiv: {
    const APInt *Denominator;
    // sdiv X, C -> adds log(C) sign bits.
    if (match(U->getOperand(1), m_APInt(Denominator))) {

      // Ignore non-positive denominator.
      if (!Denominator->isStrictlyPositive())
        break;

      // Calculate the incoming numerator bits.
      unsigned NumBits = ComputeNumSignBits(U->getOperand(0), Depth + 1, Q);

      // Add floor(log(C)) bits to the numerator bits.
      return std::min(TyBits, NumBits + Denominator->logBase2());
    }
    break;
  }

  case Instruction::SRem: {
    const APInt *Denominator;
    // srem X, C -> we know that the result is within [-C+1,C) when C is a
    // positive constant.  This let us put a lower bound on the number of sign
    // bits.
    if (match(U->getOperand(1), m_APInt(Denominator))) {

      // Ignore non-positive denominator.
      if (!Denominator->isStrictlyPositive())
        break;

      // Calculate the incoming numerator bits. SRem by a positive constant
      // can't lower the number of sign bits.
      unsigned NumrBits =
          ComputeNumSignBits(U->getOperand(0), Depth + 1, Q);

      // Calculate the leading sign bit constraints by examining the
      // denominator.  Given that the denominator is positive, there are two
      // cases:
      //
      //  1. the numerator is positive.  The result range is [0,C) and [0,C) u<
      //     (1 << ceilLogBase2(C)).
      //
      //  2. the numerator is negative.  Then the result range is (-C,0] and
      //     integers in (-C,0] are either 0 or >u (-1 << ceilLogBase2(C)).
      //
      // Thus a lower bound on the number of sign bits is `TyBits -
      // ceilLogBase2(C)`.

      unsigned ResBits = TyBits - Denominator->ceilLogBase2();
      return std::max(NumrBits, ResBits);
    }
    break;
  }

  case Instruction::AShr: {
    Tmp = ComputeNumSignBits(U->getOperand(0), Depth + 1, Q);
    // ashr X, C   -> adds C sign bits.  Vectors too.
    const APInt *ShAmt;
    if (match(U->getOperand(1), m_APInt(ShAmt))) {
      if (ShAmt->uge(TyBits))
        break;  // Bad shift.
      unsigned ShAmtLimited = ShAmt->getZExtValue();
      Tmp += ShAmtLimited;
      if (Tmp > TyBits) Tmp = TyBits;
    }
    return Tmp;
  }
  case Instruction::Shl: {
    const APInt *ShAmt;
    if (match(U->getOperand(1), m_APInt(ShAmt))) {
      // shl destroys sign bits.
      Tmp = ComputeNumSignBits(U->getOperand(0), Depth + 1, Q);
      if (ShAmt->uge(TyBits) ||      // Bad shift.
          ShAmt->uge(Tmp)) break;    // Shifted all sign bits out.
      Tmp2 = ShAmt->getZExtValue();
      return Tmp - Tmp2;
    }
    break;
  }
  case Instruction::And:
  case Instruction::Or:
  case Instruction::Xor:    // NOT is handled here.
    // Logical binary ops preserve the number of sign bits at the worst.
    Tmp = ComputeNumSignBits(U->getOperand(0), Depth + 1, Q);
    if (Tmp != 1) {
      Tmp2 = ComputeNumSignBits(U->getOperand(1), Depth + 1, Q);
      FirstAnswer = std::min(Tmp, Tmp2);
      // We computed what we know about the sign bits as our first
      // answer. Now proceed to the generic code that uses
      // computeKnownBits, and pick whichever answer is better.
    }
    break;

  case Instruction::Select: {
    // If we have a clamp pattern, we know that the number of sign bits will be
    // the minimum of the clamp min/max range.
    const Value *X;
    const APInt *CLow, *CHigh;
    if (isSignedMinMaxClamp(U, X, CLow, CHigh))
      return std::min(CLow->getNumSignBits(), CHigh->getNumSignBits());

    Tmp = ComputeNumSignBits(U->getOperand(1), Depth + 1, Q);
    if (Tmp == 1) break;
    Tmp2 = ComputeNumSignBits(U->getOperand(2), Depth + 1, Q);
    return std::min(Tmp, Tmp2);
  }

  case Instruction::Add:
    // Add can have at most one carry bit.  Thus we know that the output
    // is, at worst, one more bit than the inputs.
    Tmp = ComputeNumSignBits(U->getOperand(0), Depth + 1, Q);
    if (Tmp == 1) break;

    // Special case decrementing a value (ADD X, -1):
    if (const auto *CRHS = dyn_cast<Constant>(U->getOperand(1)))
      if (CRHS->isAllOnesValue()) {
        KnownBits Known(TyBits);
        computeKnownBits(U->getOperand(0), Known, Depth + 1, Q);

        // If the input is known to be 0 or 1, the output is 0/-1, which is all
        // sign bits set.
        if ((Known.Zero | 1).isAllOnesValue())
          return TyBits;

        // If we are subtracting one from a positive number, there is no carry
        // out of the result.
        if (Known.isNonNegative())
          return Tmp;
      }

    Tmp2 = ComputeNumSignBits(U->getOperand(1), Depth + 1, Q);
    if (Tmp2 == 1) break;
    return std::min(Tmp, Tmp2)-1;

  case Instruction::Sub:
    Tmp2 = ComputeNumSignBits(U->getOperand(1), Depth + 1, Q);
    if (Tmp2 == 1) break;

    // Handle NEG.
    if (const auto *CLHS = dyn_cast<Constant>(U->getOperand(0)))
      if (CLHS->isNullValue()) {
        KnownBits Known(TyBits);
        computeKnownBits(U->getOperand(1), Known, Depth + 1, Q);
        // If the input is known to be 0 or 1, the output is 0/-1, which is all
        // sign bits set.
        if ((Known.Zero | 1).isAllOnesValue())
          return TyBits;

        // If the input is known to be positive (the sign bit is known clear),
        // the output of the NEG has the same number of sign bits as the input.
        if (Known.isNonNegative())
          return Tmp2;

        // Otherwise, we treat this like a SUB.
      }

    // Sub can have at most one carry bit.  Thus we know that the output
    // is, at worst, one more bit than the inputs.
    Tmp = ComputeNumSignBits(U->getOperand(0), Depth + 1, Q);
    if (Tmp == 1) break;
    return std::min(Tmp, Tmp2)-1;

  case Instruction::Mul: {
    // The output of the Mul can be at most twice the valid bits in the inputs.
    unsigned SignBitsOp0 = ComputeNumSignBits(U->getOperand(0), Depth + 1, Q);
    if (SignBitsOp0 == 1) break;
    unsigned SignBitsOp1 = ComputeNumSignBits(U->getOperand(1), Depth + 1, Q);
    if (SignBitsOp1 == 1) break;
    unsigned OutValidBits =
        (TyBits - SignBitsOp0 + 1) + (TyBits - SignBitsOp1 + 1);
    return OutValidBits > TyBits ? 1 : TyBits - OutValidBits + 1;
  }

  case Instruction::PHI: {
    const PHINode *PN = cast<PHINode>(U);
    unsigned NumIncomingValues = PN->getNumIncomingValues();
    // Don't analyze large in-degree PHIs.
    if (NumIncomingValues > 4) break;
    // Unreachable blocks may have zero-operand PHI nodes.
    if (NumIncomingValues == 0) break;

    // Take the minimum of all incoming values.  This can't infinitely loop
    // because of our depth threshold.
    Tmp = ComputeNumSignBits(PN->getIncomingValue(0), Depth + 1, Q);
    for (unsigned i = 1, e = NumIncomingValues; i != e; ++i) {
      if (Tmp == 1) return Tmp;
      Tmp = std::min(
          Tmp, ComputeNumSignBits(PN->getIncomingValue(i), Depth + 1, Q));
    }
    return Tmp;
  }

  case Instruction::Trunc:
    // FIXME: it's tricky to do anything useful for this, but it is an important
    // case for targets like X86.
    break;

  case Instruction::ExtractElement:
    // Look through extract element. At the moment we keep this simple and skip
    // tracking the specific element. But at least we might find information
    // valid for all elements of the vector (for example if vector is sign
    // extended, shifted, etc).
    return ComputeNumSignBits(U->getOperand(0), Depth + 1, Q);

  case Instruction::ShuffleVector: {
    // TODO: This is copied almost directly from the SelectionDAG version of
    //       ComputeNumSignBits. It would be better if we could share common
    //       code. If not, make sure that changes are translated to the DAG.

    // Collect the minimum number of sign bits that are shared by every vector
    // element referenced by the shuffle.
    auto *Shuf = cast<ShuffleVectorInst>(U);
    int NumElts = Shuf->getOperand(0)->getType()->getVectorNumElements();
    int NumMaskElts = Shuf->getMask()->getType()->getVectorNumElements();
    APInt DemandedLHS(NumElts, 0), DemandedRHS(NumElts, 0);
    for (int i = 0; i != NumMaskElts; ++i) {
      int M = Shuf->getMaskValue(i);
      assert(M < NumElts * 2 && "Invalid shuffle mask constant");
      // For undef elements, we don't know anything about the common state of
      // the shuffle result.
      if (M == -1)
        return 1;
      if (M < NumElts)
        DemandedLHS.setBit(M % NumElts);
      else
        DemandedRHS.setBit(M % NumElts);
    }
    Tmp = std::numeric_limits<unsigned>::max();
    if (!!DemandedLHS)
      Tmp = ComputeNumSignBits(Shuf->getOperand(0), Depth + 1, Q);
    if (!!DemandedRHS) {
      Tmp2 = ComputeNumSignBits(Shuf->getOperand(1), Depth + 1, Q);
      Tmp = std::min(Tmp, Tmp2);
    }
    // If we don't know anything, early out and try computeKnownBits fall-back.
    if (Tmp == 1)
      break;
    assert(Tmp <= V->getType()->getScalarSizeInBits() &&
           "Failed to determine minimum sign bits");
    return Tmp;
  }
  }

  // Finally, if we can prove that the top bits of the result are 0's or 1's,
  // use this information.

  // If we can examine all elements of a vector constant successfully, we're
  // done (we can't do any better than that). If not, keep trying.
  if (unsigned VecSignBits = computeNumSignBitsVectorConstant(V, TyBits))
    return VecSignBits;

  KnownBits Known(TyBits);
  computeKnownBits(V, Known, Depth, Q);

  // If we know that the sign bit is either zero or one, determine the number of
  // identical bits in the top of the input value.
  return std::max(FirstAnswer, Known.countMinSignBits());
}

/// This function computes the integer multiple of Base that equals V.
/// If successful, it returns true and returns the multiple in
/// Multiple. If unsuccessful, it returns false. It looks
/// through SExt instructions only if LookThroughSExt is true.
bool llvm::ComputeMultiple(Value *V, unsigned Base, Value *&Multiple,
                           bool LookThroughSExt, unsigned Depth) {
  const unsigned MaxDepth = 6;

  assert(V && "No Value?");
  assert(Depth <= MaxDepth && "Limit Search Depth");
  assert(V->getType()->isIntegerTy() && "Not integer or pointer type!");

  Type *T = V->getType();

  ConstantInt *CI = dyn_cast<ConstantInt>(V);

  if (Base == 0)
    return false;

  if (Base == 1) {
    Multiple = V;
    return true;
  }

  ConstantExpr *CO = dyn_cast<ConstantExpr>(V);
  Constant *BaseVal = ConstantInt::get(T, Base);
  if (CO && CO == BaseVal) {
    // Multiple is 1.
    Multiple = ConstantInt::get(T, 1);
    return true;
  }

  if (CI && CI->getZExtValue() % Base == 0) {
    Multiple = ConstantInt::get(T, CI->getZExtValue() / Base);
    return true;
  }

  if (Depth == MaxDepth) return false;  // Limit search depth.

  Operator *I = dyn_cast<Operator>(V);
  if (!I) return false;

  switch (I->getOpcode()) {
  default: break;
  case Instruction::SExt:
    if (!LookThroughSExt) return false;
    // otherwise fall through to ZExt
    LLVM_FALLTHROUGH;
  case Instruction::ZExt:
    return ComputeMultiple(I->getOperand(0), Base, Multiple,
                           LookThroughSExt, Depth+1);
  case Instruction::Shl:
  case Instruction::Mul: {
    Value *Op0 = I->getOperand(0);
    Value *Op1 = I->getOperand(1);

    if (I->getOpcode() == Instruction::Shl) {
      ConstantInt *Op1CI = dyn_cast<ConstantInt>(Op1);
      if (!Op1CI) return false;
      // Turn Op0 << Op1 into Op0 * 2^Op1
      APInt Op1Int = Op1CI->getValue();
      uint64_t BitToSet = Op1Int.getLimitedValue(Op1Int.getBitWidth() - 1);
      APInt API(Op1Int.getBitWidth(), 0);
      API.setBit(BitToSet);
      Op1 = ConstantInt::get(V->getContext(), API);
    }

    Value *Mul0 = nullptr;
    if (ComputeMultiple(Op0, Base, Mul0, LookThroughSExt, Depth+1)) {
      if (Constant *Op1C = dyn_cast<Constant>(Op1))
        if (Constant *MulC = dyn_cast<Constant>(Mul0)) {
          if (Op1C->getType()->getPrimitiveSizeInBits() <
              MulC->getType()->getPrimitiveSizeInBits())
            Op1C = ConstantExpr::getZExt(Op1C, MulC->getType());
          if (Op1C->getType()->getPrimitiveSizeInBits() >
              MulC->getType()->getPrimitiveSizeInBits())
            MulC = ConstantExpr::getZExt(MulC, Op1C->getType());

          // V == Base * (Mul0 * Op1), so return (Mul0 * Op1)
          Multiple = ConstantExpr::getMul(MulC, Op1C);
          return true;
        }

      if (ConstantInt *Mul0CI = dyn_cast<ConstantInt>(Mul0))
        if (Mul0CI->getValue() == 1) {
          // V == Base * Op1, so return Op1
          Multiple = Op1;
          return true;
        }
    }

    Value *Mul1 = nullptr;
    if (ComputeMultiple(Op1, Base, Mul1, LookThroughSExt, Depth+1)) {
      if (Constant *Op0C = dyn_cast<Constant>(Op0))
        if (Constant *MulC = dyn_cast<Constant>(Mul1)) {
          if (Op0C->getType()->getPrimitiveSizeInBits() <
              MulC->getType()->getPrimitiveSizeInBits())
            Op0C = ConstantExpr::getZExt(Op0C, MulC->getType());
          if (Op0C->getType()->getPrimitiveSizeInBits() >
              MulC->getType()->getPrimitiveSizeInBits())
            MulC = ConstantExpr::getZExt(MulC, Op0C->getType());

          // V == Base * (Mul1 * Op0), so return (Mul1 * Op0)
          Multiple = ConstantExpr::getMul(MulC, Op0C);
          return true;
        }

      if (ConstantInt *Mul1CI = dyn_cast<ConstantInt>(Mul1))
        if (Mul1CI->getValue() == 1) {
          // V == Base * Op0, so return Op0
          Multiple = Op0;
          return true;
        }
    }
  }
  }

  // We could not determine if V is a multiple of Base.
  return false;
}

Intrinsic::ID llvm::getIntrinsicForCallSite(ImmutableCallSite ICS,
                                            const TargetLibraryInfo *TLI) {
  const Function *F = ICS.getCalledFunction();
  if (!F)
    return Intrinsic::not_intrinsic;

  if (F->isIntrinsic())
    return F->getIntrinsicID();

  if (!TLI)
    return Intrinsic::not_intrinsic;

  LibFunc Func;
  // We're going to make assumptions on the semantics of the functions, check
  // that the target knows that it's available in this environment and it does
  // not have local linkage.
  if (!F || F->hasLocalLinkage() || !TLI->getLibFunc(*F, Func))
    return Intrinsic::not_intrinsic;

  if (!ICS.onlyReadsMemory())
    return Intrinsic::not_intrinsic;

  // Otherwise check if we have a call to a function that can be turned into a
  // vector intrinsic.
  switch (Func) {
  default:
    break;
  case LibFunc_sin:
  case LibFunc_sinf:
  case LibFunc_sinl:
    return Intrinsic::sin;
  case LibFunc_cos:
  case LibFunc_cosf:
  case LibFunc_cosl:
    return Intrinsic::cos;
  case LibFunc_exp:
  case LibFunc_expf:
  case LibFunc_expl:
    return Intrinsic::exp;
  case LibFunc_exp2:
  case LibFunc_exp2f:
  case LibFunc_exp2l:
    return Intrinsic::exp2;
  case LibFunc_log:
  case LibFunc_logf:
  case LibFunc_logl:
    return Intrinsic::log;
  case LibFunc_log10:
  case LibFunc_log10f:
  case LibFunc_log10l:
    return Intrinsic::log10;
  case LibFunc_log2:
  case LibFunc_log2f:
  case LibFunc_log2l:
    return Intrinsic::log2;
  case LibFunc_fabs:
  case LibFunc_fabsf:
  case LibFunc_fabsl:
    return Intrinsic::fabs;
  case LibFunc_fmin:
  case LibFunc_fminf:
  case LibFunc_fminl:
    return Intrinsic::minnum;
  case LibFunc_fmax:
  case LibFunc_fmaxf:
  case LibFunc_fmaxl:
    return Intrinsic::maxnum;
  case LibFunc_copysign:
  case LibFunc_copysignf:
  case LibFunc_copysignl:
    return Intrinsic::copysign;
  case LibFunc_floor:
  case LibFunc_floorf:
  case LibFunc_floorl:
    return Intrinsic::floor;
  case LibFunc_ceil:
  case LibFunc_ceilf:
  case LibFunc_ceill:
    return Intrinsic::ceil;
  case LibFunc_trunc:
  case LibFunc_truncf:
  case LibFunc_truncl:
    return Intrinsic::trunc;
  case LibFunc_rint:
  case LibFunc_rintf:
  case LibFunc_rintl:
    return Intrinsic::rint;
  case LibFunc_nearbyint:
  case LibFunc_nearbyintf:
  case LibFunc_nearbyintl:
    return Intrinsic::nearbyint;
  case LibFunc_round:
  case LibFunc_roundf:
  case LibFunc_roundl:
    return Intrinsic::round;
  case LibFunc_pow:
  case LibFunc_powf:
  case LibFunc_powl:
    return Intrinsic::pow;
  case LibFunc_sqrt:
  case LibFunc_sqrtf:
  case LibFunc_sqrtl:
    return Intrinsic::sqrt;
  }

  return Intrinsic::not_intrinsic;
}

/// Return true if we can prove that the specified FP value is never equal to
/// -0.0.
///
/// NOTE: this function will need to be revisited when we support non-default
/// rounding modes!
bool llvm::CannotBeNegativeZero(const Value *V, const TargetLibraryInfo *TLI,
                                unsigned Depth) {
  if (auto *CFP = dyn_cast<ConstantFP>(V))
    return !CFP->getValueAPF().isNegZero();

  // Limit search depth.
  if (Depth == MaxDepth)
    return false;

  auto *Op = dyn_cast<Operator>(V);
  if (!Op)
    return false;

  // Check if the nsz fast-math flag is set.
  if (auto *FPO = dyn_cast<FPMathOperator>(Op))
    if (FPO->hasNoSignedZeros())
      return true;

  // (fadd x, 0.0) is guaranteed to return +0.0, not -0.0.
  if (match(Op, m_FAdd(m_Value(), m_PosZeroFP())))
    return true;

  // sitofp and uitofp turn into +0.0 for zero.
  if (isa<SIToFPInst>(Op) || isa<UIToFPInst>(Op))
    return true;

  if (auto *Call = dyn_cast<CallInst>(Op)) {
    Intrinsic::ID IID = getIntrinsicForCallSite(Call, TLI);
    switch (IID) {
    default:
      break;
    // sqrt(-0.0) = -0.0, no other negative results are possible.
    case Intrinsic::sqrt:
    case Intrinsic::canonicalize:
      return CannotBeNegativeZero(Call->getArgOperand(0), TLI, Depth + 1);
    // fabs(x) != -0.0
    case Intrinsic::fabs:
      return true;
    }
  }

  return false;
}

/// If \p SignBitOnly is true, test for a known 0 sign bit rather than a
/// standard ordered compare. e.g. make -0.0 olt 0.0 be true because of the sign
/// bit despite comparing equal.
static bool cannotBeOrderedLessThanZeroImpl(const Value *V,
                                            const TargetLibraryInfo *TLI,
                                            bool SignBitOnly,
                                            unsigned Depth) {
  // TODO: This function does not do the right thing when SignBitOnly is true
  // and we're lowering to a hypothetical IEEE 754-compliant-but-evil platform
  // which flips the sign bits of NaNs.  See
  // https://llvm.org/bugs/show_bug.cgi?id=31702.

  if (const ConstantFP *CFP = dyn_cast<ConstantFP>(V)) {
    return !CFP->getValueAPF().isNegative() ||
           (!SignBitOnly && CFP->getValueAPF().isZero());
  }

  // Handle vector of constants.
  if (auto *CV = dyn_cast<Constant>(V)) {
    if (CV->getType()->isVectorTy()) {
      unsigned NumElts = CV->getType()->getVectorNumElements();
      for (unsigned i = 0; i != NumElts; ++i) {
        auto *CFP = dyn_cast_or_null<ConstantFP>(CV->getAggregateElement(i));
        if (!CFP)
          return false;
        if (CFP->getValueAPF().isNegative() &&
            (SignBitOnly || !CFP->getValueAPF().isZero()))
          return false;
      }

      // All non-negative ConstantFPs.
      return true;
    }
  }

  if (Depth == MaxDepth)
    return false; // Limit search depth.

  const Operator *I = dyn_cast<Operator>(V);
  if (!I)
    return false;

  switch (I->getOpcode()) {
  default:
    break;
  // Unsigned integers are always nonnegative.
  case Instruction::UIToFP:
    return true;
  case Instruction::FMul:
    // x*x is always non-negative or a NaN.
    if (I->getOperand(0) == I->getOperand(1) &&
        (!SignBitOnly || cast<FPMathOperator>(I)->hasNoNaNs()))
      return true;

    LLVM_FALLTHROUGH;
  case Instruction::FAdd:
  case Instruction::FDiv:
  case Instruction::FRem:
    return cannotBeOrderedLessThanZeroImpl(I->getOperand(0), TLI, SignBitOnly,
                                           Depth + 1) &&
           cannotBeOrderedLessThanZeroImpl(I->getOperand(1), TLI, SignBitOnly,
                                           Depth + 1);
  case Instruction::Select:
    return cannotBeOrderedLessThanZeroImpl(I->getOperand(1), TLI, SignBitOnly,
                                           Depth + 1) &&
           cannotBeOrderedLessThanZeroImpl(I->getOperand(2), TLI, SignBitOnly,
                                           Depth + 1);
  case Instruction::FPExt:
  case Instruction::FPTrunc:
    // Widening/narrowing never change sign.
    return cannotBeOrderedLessThanZeroImpl(I->getOperand(0), TLI, SignBitOnly,
                                           Depth + 1);
  case Instruction::ExtractElement:
    // Look through extract element. At the moment we keep this simple and skip
    // tracking the specific element. But at least we might find information
    // valid for all elements of the vector.
    return cannotBeOrderedLessThanZeroImpl(I->getOperand(0), TLI, SignBitOnly,
                                           Depth + 1);
  case Instruction::Call:
    const auto *CI = cast<CallInst>(I);
    Intrinsic::ID IID = getIntrinsicForCallSite(CI, TLI);
    switch (IID) {
    default:
      break;
    case Intrinsic::maxnum:
      return (isKnownNeverNaN(I->getOperand(0), TLI) &&
              cannotBeOrderedLessThanZeroImpl(I->getOperand(0), TLI,
                                              SignBitOnly, Depth + 1)) ||
            (isKnownNeverNaN(I->getOperand(1), TLI) &&
              cannotBeOrderedLessThanZeroImpl(I->getOperand(1), TLI,
                                              SignBitOnly, Depth + 1));

    case Intrinsic::maximum:
      return cannotBeOrderedLessThanZeroImpl(I->getOperand(0), TLI, SignBitOnly,
                                             Depth + 1) ||
             cannotBeOrderedLessThanZeroImpl(I->getOperand(1), TLI, SignBitOnly,
                                             Depth + 1);
    case Intrinsic::minnum:
    case Intrinsic::minimum:
      return cannotBeOrderedLessThanZeroImpl(I->getOperand(0), TLI, SignBitOnly,
                                             Depth + 1) &&
             cannotBeOrderedLessThanZeroImpl(I->getOperand(1), TLI, SignBitOnly,
                                             Depth + 1);
    case Intrinsic::exp:
    case Intrinsic::exp2:
    case Intrinsic::fabs:
      return true;

    case Intrinsic::sqrt:
      // sqrt(x) is always >= -0 or NaN.  Moreover, sqrt(x) == -0 iff x == -0.
      if (!SignBitOnly)
        return true;
      return CI->hasNoNaNs() && (CI->hasNoSignedZeros() ||
                                 CannotBeNegativeZero(CI->getOperand(0), TLI));

    case Intrinsic::powi:
      if (ConstantInt *Exponent = dyn_cast<ConstantInt>(I->getOperand(1))) {
        // powi(x,n) is non-negative if n is even.
        if (Exponent->getBitWidth() <= 64 && Exponent->getSExtValue() % 2u == 0)
          return true;
      }
      // TODO: This is not correct.  Given that exp is an integer, here are the
      // ways that pow can return a negative value:
      //
      //   pow(x, exp)    --> negative if exp is odd and x is negative.
      //   pow(-0, exp)   --> -inf if exp is negative odd.
      //   pow(-0, exp)   --> -0 if exp is positive odd.
      //   pow(-inf, exp) --> -0 if exp is negative odd.
      //   pow(-inf, exp) --> -inf if exp is positive odd.
      //
      // Therefore, if !SignBitOnly, we can return true if x >= +0 or x is NaN,
      // but we must return false if x == -0.  Unfortunately we do not currently
      // have a way of expressing this constraint.  See details in
      // https://llvm.org/bugs/show_bug.cgi?id=31702.
      return cannotBeOrderedLessThanZeroImpl(I->getOperand(0), TLI, SignBitOnly,
                                             Depth + 1);

    case Intrinsic::fma:
    case Intrinsic::fmuladd:
      // x*x+y is non-negative if y is non-negative.
      return I->getOperand(0) == I->getOperand(1) &&
             (!SignBitOnly || cast<FPMathOperator>(I)->hasNoNaNs()) &&
             cannotBeOrderedLessThanZeroImpl(I->getOperand(2), TLI, SignBitOnly,
                                             Depth + 1);
    }
    break;
  }
  return false;
}

bool llvm::CannotBeOrderedLessThanZero(const Value *V,
                                       const TargetLibraryInfo *TLI) {
  return cannotBeOrderedLessThanZeroImpl(V, TLI, false, 0);
}

bool llvm::SignBitMustBeZero(const Value *V, const TargetLibraryInfo *TLI) {
  return cannotBeOrderedLessThanZeroImpl(V, TLI, true, 0);
}

bool llvm::isKnownNeverNaN(const Value *V, const TargetLibraryInfo *TLI,
                           unsigned Depth) {
  assert(V->getType()->isFPOrFPVectorTy() && "Querying for NaN on non-FP type");

  // If we're told that NaNs won't happen, assume they won't.
  if (auto *FPMathOp = dyn_cast<FPMathOperator>(V))
    if (FPMathOp->hasNoNaNs())
      return true;

  // Handle scalar constants.
  if (auto *CFP = dyn_cast<ConstantFP>(V))
    return !CFP->isNaN();

  if (Depth == MaxDepth)
    return false;

  if (auto *Inst = dyn_cast<Instruction>(V)) {
    switch (Inst->getOpcode()) {
    case Instruction::FAdd:
    case Instruction::FMul:
    case Instruction::FSub:
    case Instruction::FDiv:
    case Instruction::FRem: {
      // TODO: Need isKnownNeverInfinity
      return false;
    }
    case Instruction::Select: {
      return isKnownNeverNaN(Inst->getOperand(1), TLI, Depth + 1) &&
             isKnownNeverNaN(Inst->getOperand(2), TLI, Depth + 1);
    }
    case Instruction::SIToFP:
    case Instruction::UIToFP:
      return true;
    case Instruction::FPTrunc:
    case Instruction::FPExt:
      return isKnownNeverNaN(Inst->getOperand(0), TLI, Depth + 1);
    default:
      break;
    }
  }

  if (const auto *II = dyn_cast<IntrinsicInst>(V)) {
    switch (II->getIntrinsicID()) {
    case Intrinsic::canonicalize:
    case Intrinsic::fabs:
    case Intrinsic::copysign:
    case Intrinsic::exp:
    case Intrinsic::exp2:
    case Intrinsic::floor:
    case Intrinsic::ceil:
    case Intrinsic::trunc:
    case Intrinsic::rint:
    case Intrinsic::nearbyint:
    case Intrinsic::round:
      return isKnownNeverNaN(II->getArgOperand(0), TLI, Depth + 1);
    case Intrinsic::sqrt:
      return isKnownNeverNaN(II->getArgOperand(0), TLI, Depth + 1) &&
             CannotBeOrderedLessThanZero(II->getArgOperand(0), TLI);
    default:
      return false;
    }
  }

  // Bail out for constant expressions, but try to handle vector constants.
  if (!V->getType()->isVectorTy() || !isa<Constant>(V))
    return false;

  // For vectors, verify that each element is not NaN.
  unsigned NumElts = V->getType()->getVectorNumElements();
  for (unsigned i = 0; i != NumElts; ++i) {
    Constant *Elt = cast<Constant>(V)->getAggregateElement(i);
    if (!Elt)
      return false;
    if (isa<UndefValue>(Elt))
      continue;
    auto *CElt = dyn_cast<ConstantFP>(Elt);
    if (!CElt || CElt->isNaN())
      return false;
  }
  // All elements were confirmed not-NaN or undefined.
  return true;
}

Value *llvm::isBytewiseValue(Value *V) {

  // All byte-wide stores are splatable, even of arbitrary variables.
  if (V->getType()->isIntegerTy(8))
    return V;

  LLVMContext &Ctx = V->getContext();

  // Undef don't care.
  auto *UndefInt8 = UndefValue::get(Type::getInt8Ty(Ctx));
  if (isa<UndefValue>(V))
    return UndefInt8;

  Constant *C = dyn_cast<Constant>(V);
  if (!C) {
    // Conceptually, we could handle things like:
    //   %a = zext i8 %X to i16
    //   %b = shl i16 %a, 8
    //   %c = or i16 %a, %b
    // but until there is an example that actually needs this, it doesn't seem
    // worth worrying about.
    return nullptr;
  }

  // Handle 'null' ConstantArrayZero etc.
  if (C->isNullValue())
    return Constant::getNullValue(Type::getInt8Ty(Ctx));

  // Constant floating-point values can be handled as integer values if the
  // corresponding integer value is "byteable".  An important case is 0.0.
  if (ConstantFP *CFP = dyn_cast<ConstantFP>(C)) {
    Type *Ty = nullptr;
    if (CFP->getType()->isHalfTy())
      Ty = Type::getInt16Ty(Ctx);
    else if (CFP->getType()->isFloatTy())
      Ty = Type::getInt32Ty(Ctx);
    else if (CFP->getType()->isDoubleTy())
      Ty = Type::getInt64Ty(Ctx);
    // Don't handle long double formats, which have strange constraints.
    return Ty ? isBytewiseValue(ConstantExpr::getBitCast(CFP, Ty)) : nullptr;
  }

  // We can handle constant integers that are multiple of 8 bits.
  if (ConstantInt *CI = dyn_cast<ConstantInt>(C)) {
    if (CI->getBitWidth() % 8 == 0) {
      assert(CI->getBitWidth() > 8 && "8 bits should be handled above!");
      if (!CI->getValue().isSplat(8))
        return nullptr;
      return ConstantInt::get(Ctx, CI->getValue().trunc(8));
    }
  }

  auto Merge = [&](Value *LHS, Value *RHS) -> Value * {
    if (LHS == RHS)
      return LHS;
    if (!LHS || !RHS)
      return nullptr;
    if (LHS == UndefInt8)
      return RHS;
    if (RHS == UndefInt8)
      return LHS;
    return nullptr;
  };

  if (ConstantDataSequential *CA = dyn_cast<ConstantDataSequential>(C)) {
    Value *Val = UndefInt8;
    for (unsigned I = 0, E = CA->getNumElements(); I != E; ++I)
      if (!(Val = Merge(Val, isBytewiseValue(CA->getElementAsConstant(I)))))
        return nullptr;
    return Val;
  }

  if (isa<ConstantVector>(C)) {
    Constant *Splat = cast<ConstantVector>(C)->getSplatValue();
    return Splat ? isBytewiseValue(Splat) : nullptr;
  }

  if (isa<ConstantArray>(C) || isa<ConstantStruct>(C)) {
    Value *Val = UndefInt8;
    for (unsigned I = 0, E = C->getNumOperands(); I != E; ++I)
      if (!(Val = Merge(Val, isBytewiseValue(C->getOperand(I)))))
        return nullptr;
    return Val;
  }

  // Don't try to handle the handful of other constants.
  return nullptr;
}

// This is the recursive version of BuildSubAggregate. It takes a few different
// arguments. Idxs is the index within the nested struct From that we are
// looking at now (which is of type IndexedType). IdxSkip is the number of
// indices from Idxs that should be left out when inserting into the resulting
// struct. To is the result struct built so far, new insertvalue instructions
// build on that.
static Value *BuildSubAggregate(Value *From, Value* To, Type *IndexedType,
                                SmallVectorImpl<unsigned> &Idxs,
                                unsigned IdxSkip,
                                Instruction *InsertBefore) {
  StructType *STy = dyn_cast<StructType>(IndexedType);
  if (STy) {
    // Save the original To argument so we can modify it
    Value *OrigTo = To;
    // General case, the type indexed by Idxs is a struct
    for (unsigned i = 0, e = STy->getNumElements(); i != e; ++i) {
      // Process each struct element recursively
      Idxs.push_back(i);
      Value *PrevTo = To;
      To = BuildSubAggregate(From, To, STy->getElementType(i), Idxs, IdxSkip,
                             InsertBefore);
      Idxs.pop_back();
      if (!To) {
        // Couldn't find any inserted value for this index? Cleanup
        while (PrevTo != OrigTo) {
          InsertValueInst* Del = cast<InsertValueInst>(PrevTo);
          PrevTo = Del->getAggregateOperand();
          Del->eraseFromParent();
        }
        // Stop processing elements
        break;
      }
    }
    // If we successfully found a value for each of our subaggregates
    if (To)
      return To;
  }
  // Base case, the type indexed by SourceIdxs is not a struct, or not all of
  // the struct's elements had a value that was inserted directly. In the latter
  // case, perhaps we can't determine each of the subelements individually, but
  // we might be able to find the complete struct somewhere.

  // Find the value that is at that particular spot
  Value *V = FindInsertedValue(From, Idxs);

  if (!V)
    return nullptr;

  // Insert the value in the new (sub) aggregate
  return InsertValueInst::Create(To, V, makeArrayRef(Idxs).slice(IdxSkip),
                                 "tmp", InsertBefore);
}

// This helper takes a nested struct and extracts a part of it (which is again a
// struct) into a new value. For example, given the struct:
// { a, { b, { c, d }, e } }
// and the indices "1, 1" this returns
// { c, d }.
//
// It does this by inserting an insertvalue for each element in the resulting
// struct, as opposed to just inserting a single struct. This will only work if
// each of the elements of the substruct are known (ie, inserted into From by an
// insertvalue instruction somewhere).
//
// All inserted insertvalue instructions are inserted before InsertBefore
static Value *BuildSubAggregate(Value *From, ArrayRef<unsigned> idx_range,
                                Instruction *InsertBefore) {
  assert(InsertBefore && "Must have someplace to insert!");
  Type *IndexedType = ExtractValueInst::getIndexedType(From->getType(),
                                                             idx_range);
  Value *To = UndefValue::get(IndexedType);
  SmallVector<unsigned, 10> Idxs(idx_range.begin(), idx_range.end());
  unsigned IdxSkip = Idxs.size();

  return BuildSubAggregate(From, To, IndexedType, Idxs, IdxSkip, InsertBefore);
}

/// Given an aggregate and a sequence of indices, see if the scalar value
/// indexed is already around as a register, for example if it was inserted
/// directly into the aggregate.
///
/// If InsertBefore is not null, this function will duplicate (modified)
/// insertvalues when a part of a nested struct is extracted.
Value *llvm::FindInsertedValue(Value *V, ArrayRef<unsigned> idx_range,
                               Instruction *InsertBefore) {
  // Nothing to index? Just return V then (this is useful at the end of our
  // recursion).
  if (idx_range.empty())
    return V;
  // We have indices, so V should have an indexable type.
  assert((V->getType()->isStructTy() || V->getType()->isArrayTy()) &&
         "Not looking at a struct or array?");
  assert(ExtractValueInst::getIndexedType(V->getType(), idx_range) &&
         "Invalid indices for type?");

  if (Constant *C = dyn_cast<Constant>(V)) {
    C = C->getAggregateElement(idx_range[0]);
    if (!C) return nullptr;
    return FindInsertedValue(C, idx_range.slice(1), InsertBefore);
  }

  if (InsertValueInst *I = dyn_cast<InsertValueInst>(V)) {
    // Loop the indices for the insertvalue instruction in parallel with the
    // requested indices
    const unsigned *req_idx = idx_range.begin();
    for (const unsigned *i = I->idx_begin(), *e = I->idx_end();
         i != e; ++i, ++req_idx) {
      if (req_idx == idx_range.end()) {
        // We can't handle this without inserting insertvalues
        if (!InsertBefore)
          return nullptr;

        // The requested index identifies a part of a nested aggregate. Handle
        // this specially. For example,
        // %A = insertvalue { i32, {i32, i32 } } undef, i32 10, 1, 0
        // %B = insertvalue { i32, {i32, i32 } } %A, i32 11, 1, 1
        // %C = extractvalue {i32, { i32, i32 } } %B, 1
        // This can be changed into
        // %A = insertvalue {i32, i32 } undef, i32 10, 0
        // %C = insertvalue {i32, i32 } %A, i32 11, 1
        // which allows the unused 0,0 element from the nested struct to be
        // removed.
        return BuildSubAggregate(V, makeArrayRef(idx_range.begin(), req_idx),
                                 InsertBefore);
      }

      // This insert value inserts something else than what we are looking for.
      // See if the (aggregate) value inserted into has the value we are
      // looking for, then.
      if (*req_idx != *i)
        return FindInsertedValue(I->getAggregateOperand(), idx_range,
                                 InsertBefore);
    }
    // If we end up here, the indices of the insertvalue match with those
    // requested (though possibly only partially). Now we recursively look at
    // the inserted value, passing any remaining indices.
    return FindInsertedValue(I->getInsertedValueOperand(),
                             makeArrayRef(req_idx, idx_range.end()),
                             InsertBefore);
  }

  if (ExtractValueInst *I = dyn_cast<ExtractValueInst>(V)) {
    // If we're extracting a value from an aggregate that was extracted from
    // something else, we can extract from that something else directly instead.
    // However, we will need to chain I's indices with the requested indices.

    // Calculate the number of indices required
    unsigned size = I->getNumIndices() + idx_range.size();
    // Allocate some space to put the new indices in
    SmallVector<unsigned, 5> Idxs;
    Idxs.reserve(size);
    // Add indices from the extract value instruction
    Idxs.append(I->idx_begin(), I->idx_end());

    // Add requested indices
    Idxs.append(idx_range.begin(), idx_range.end());

    assert(Idxs.size() == size
           && "Number of indices added not correct?");

    return FindInsertedValue(I->getAggregateOperand(), Idxs, InsertBefore);
  }
  // Otherwise, we don't know (such as, extracting from a function return value
  // or load instruction)
  return nullptr;
}

/// Analyze the specified pointer to see if it can be expressed as a base
/// pointer plus a constant offset. Return the base and offset to the caller.
Value *llvm::GetPointerBaseWithConstantOffset(Value *Ptr, int64_t &Offset,
                                              const DataLayout &DL) {
  unsigned BitWidth = DL.getIndexTypeSizeInBits(Ptr->getType());
  APInt ByteOffset(BitWidth, 0);

  // We walk up the defs but use a visited set to handle unreachable code. In
  // that case, we stop after accumulating the cycle once (not that it
  // matters).
  SmallPtrSet<Value *, 16> Visited;
  while (Visited.insert(Ptr).second) {
    if (Ptr->getType()->isVectorTy())
      break;

    if (GEPOperator *GEP = dyn_cast<GEPOperator>(Ptr)) {
      // If one of the values we have visited is an addrspacecast, then
      // the pointer type of this GEP may be different from the type
      // of the Ptr parameter which was passed to this function.  This
      // means when we construct GEPOffset, we need to use the size
      // of GEP's pointer type rather than the size of the original
      // pointer type.
      APInt GEPOffset(DL.getIndexTypeSizeInBits(Ptr->getType()), 0);
      if (!GEP->accumulateConstantOffset(DL, GEPOffset))
        break;

      APInt OrigByteOffset(ByteOffset);
      ByteOffset += GEPOffset.sextOrTrunc(ByteOffset.getBitWidth());
      if (ByteOffset.getMinSignedBits() > 64) {
        // Stop traversal if the pointer offset wouldn't fit into int64_t
        // (this should be removed if Offset is updated to an APInt)
        ByteOffset = OrigByteOffset;
        break;
      }

      Ptr = GEP->getPointerOperand();
    } else if (Operator::getOpcode(Ptr) == Instruction::BitCast ||
               Operator::getOpcode(Ptr) == Instruction::AddrSpaceCast) {
      Ptr = cast<Operator>(Ptr)->getOperand(0);
    } else if (GlobalAlias *GA = dyn_cast<GlobalAlias>(Ptr)) {
      if (GA->isInterposable())
        break;
      Ptr = GA->getAliasee();
    } else {
      break;
    }
  }
  Offset = ByteOffset.getSExtValue();
  return Ptr;
}

bool llvm::isGEPBasedOnPointerToString(const GEPOperator *GEP,
                                       unsigned CharSize) {
  // Make sure the GEP has exactly three arguments.
  if (GEP->getNumOperands() != 3)
    return false;

  // Make sure the index-ee is a pointer to array of \p CharSize integers.
  // CharSize.
  ArrayType *AT = dyn_cast<ArrayType>(GEP->getSourceElementType());
  if (!AT || !AT->getElementType()->isIntegerTy(CharSize))
    return false;

  // Check to make sure that the first operand of the GEP is an integer and
  // has value 0 so that we are sure we're indexing into the initializer.
  const ConstantInt *FirstIdx = dyn_cast<ConstantInt>(GEP->getOperand(1));
  if (!FirstIdx || !FirstIdx->isZero())
    return false;

  return true;
}

bool llvm::getConstantDataArrayInfo(const Value *V,
                                    ConstantDataArraySlice &Slice,
                                    unsigned ElementSize, uint64_t Offset) {
  assert(V);

  // Look through bitcast instructions and geps.
  V = V->stripPointerCasts();

  // If the value is a GEP instruction or constant expression, treat it as an
  // offset.
  if (const GEPOperator *GEP = dyn_cast<GEPOperator>(V)) {
    // The GEP operator should be based on a pointer to string constant, and is
    // indexing into the string constant.
    if (!isGEPBasedOnPointerToString(GEP, ElementSize))
      return false;

    // If the second index isn't a ConstantInt, then this is a variable index
    // into the array.  If this occurs, we can't say anything meaningful about
    // the string.
    uint64_t StartIdx = 0;
    if (const ConstantInt *CI = dyn_cast<ConstantInt>(GEP->getOperand(2)))
      StartIdx = CI->getZExtValue();
    else
      return false;
    return getConstantDataArrayInfo(GEP->getOperand(0), Slice, ElementSize,
                                    StartIdx + Offset);
  }

  // The GEP instruction, constant or instruction, must reference a global
  // variable that is a constant and is initialized. The referenced constant
  // initializer is the array that we'll use for optimization.
  const GlobalVariable *GV = dyn_cast<GlobalVariable>(V);
  if (!GV || !GV->isConstant() || !GV->hasDefinitiveInitializer())
    return false;

  const ConstantDataArray *Array;
  ArrayType *ArrayTy;
  if (GV->getInitializer()->isNullValue()) {
    Type *GVTy = GV->getValueType();
    if ( (ArrayTy = dyn_cast<ArrayType>(GVTy)) ) {
      // A zeroinitializer for the array; there is no ConstantDataArray.
      Array = nullptr;
    } else {
      const DataLayout &DL = GV->getParent()->getDataLayout();
      uint64_t SizeInBytes = DL.getTypeStoreSize(GVTy);
      uint64_t Length = SizeInBytes / (ElementSize / 8);
      if (Length <= Offset)
        return false;

      Slice.Array = nullptr;
      Slice.Offset = 0;
      Slice.Length = Length - Offset;
      return true;
    }
  } else {
    // This must be a ConstantDataArray.
    Array = dyn_cast<ConstantDataArray>(GV->getInitializer());
    if (!Array)
      return false;
    ArrayTy = Array->getType();
  }
  if (!ArrayTy->getElementType()->isIntegerTy(ElementSize))
    return false;

  uint64_t NumElts = ArrayTy->getArrayNumElements();
  if (Offset > NumElts)
    return false;

  Slice.Array = Array;
  Slice.Offset = Offset;
  Slice.Length = NumElts - Offset;
  return true;
}

/// This function computes the length of a null-terminated C string pointed to
/// by V. If successful, it returns true and returns the string in Str.
/// If unsuccessful, it returns false.
bool llvm::getConstantStringInfo(const Value *V, StringRef &Str,
                                 uint64_t Offset, bool TrimAtNul) {
  ConstantDataArraySlice Slice;
  if (!getConstantDataArrayInfo(V, Slice, 8, Offset))
    return false;

  if (Slice.Array == nullptr) {
    if (TrimAtNul) {
      Str = StringRef();
      return true;
    }
    if (Slice.Length == 1) {
      Str = StringRef("", 1);
      return true;
    }
    // We cannot instantiate a StringRef as we do not have an appropriate string
    // of 0s at hand.
    return false;
  }

  // Start out with the entire array in the StringRef.
  Str = Slice.Array->getAsString();
  // Skip over 'offset' bytes.
  Str = Str.substr(Slice.Offset);

  if (TrimAtNul) {
    // Trim off the \0 and anything after it.  If the array is not nul
    // terminated, we just return the whole end of string.  The client may know
    // some other way that the string is length-bound.
    Str = Str.substr(0, Str.find('\0'));
  }
  return true;
}

// These next two are very similar to the above, but also look through PHI
// nodes.
// TODO: See if we can integrate these two together.

/// If we can compute the length of the string pointed to by
/// the specified pointer, return 'len+1'.  If we can't, return 0.
static uint64_t GetStringLengthH(const Value *V,
                                 SmallPtrSetImpl<const PHINode*> &PHIs,
                                 unsigned CharSize) {
  // Look through noop bitcast instructions.
  V = V->stripPointerCasts();

  // If this is a PHI node, there are two cases: either we have already seen it
  // or we haven't.
  if (const PHINode *PN = dyn_cast<PHINode>(V)) {
    if (!PHIs.insert(PN).second)
      return ~0ULL;  // already in the set.

    // If it was new, see if all the input strings are the same length.
    uint64_t LenSoFar = ~0ULL;
    for (Value *IncValue : PN->incoming_values()) {
      uint64_t Len = GetStringLengthH(IncValue, PHIs, CharSize);
      if (Len == 0) return 0; // Unknown length -> unknown.

      if (Len == ~0ULL) continue;

      if (Len != LenSoFar && LenSoFar != ~0ULL)
        return 0;    // Disagree -> unknown.
      LenSoFar = Len;
    }

    // Success, all agree.
    return LenSoFar;
  }

  // strlen(select(c,x,y)) -> strlen(x) ^ strlen(y)
  if (const SelectInst *SI = dyn_cast<SelectInst>(V)) {
    uint64_t Len1 = GetStringLengthH(SI->getTrueValue(), PHIs, CharSize);
    if (Len1 == 0) return 0;
    uint64_t Len2 = GetStringLengthH(SI->getFalseValue(), PHIs, CharSize);
    if (Len2 == 0) return 0;
    if (Len1 == ~0ULL) return Len2;
    if (Len2 == ~0ULL) return Len1;
    if (Len1 != Len2) return 0;
    return Len1;
  }

  // Otherwise, see if we can read the string.
  ConstantDataArraySlice Slice;
  if (!getConstantDataArrayInfo(V, Slice, CharSize))
    return 0;

  if (Slice.Array == nullptr)
    return 1;

  // Search for nul characters
  unsigned NullIndex = 0;
  for (unsigned E = Slice.Length; NullIndex < E; ++NullIndex) {
    if (Slice.Array->getElementAsInteger(Slice.Offset + NullIndex) == 0)
      break;
  }

  return NullIndex + 1;
}

/// If we can compute the length of the string pointed to by
/// the specified pointer, return 'len+1'.  If we can't, return 0.
uint64_t llvm::GetStringLength(const Value *V, unsigned CharSize) {
  if (!V->getType()->isPointerTy())
    return 0;

  SmallPtrSet<const PHINode*, 32> PHIs;
  uint64_t Len = GetStringLengthH(V, PHIs, CharSize);
  // If Len is ~0ULL, we had an infinite phi cycle: this is dead code, so return
  // an empty string as a length.
  return Len == ~0ULL ? 1 : Len;
}

const Value *llvm::getArgumentAliasingToReturnedPointer(const CallBase *Call) {
  assert(Call &&
         "getArgumentAliasingToReturnedPointer only works on nonnull calls");
  if (const Value *RV = Call->getReturnedArgOperand())
    return RV;
  // This can be used only as a aliasing property.
  if (isIntrinsicReturningPointerAliasingArgumentWithoutCapturing(Call))
    return Call->getArgOperand(0);
  return nullptr;
}

bool llvm::isIntrinsicReturningPointerAliasingArgumentWithoutCapturing(
    const CallBase *Call) {
  return Call->getIntrinsicID() == Intrinsic::launder_invariant_group ||
         Call->getIntrinsicID() == Intrinsic::strip_invariant_group;
}

/// \p PN defines a loop-variant pointer to an object.  Check if the
/// previous iteration of the loop was referring to the same object as \p PN.
static bool isSameUnderlyingObjectInLoop(const PHINode *PN,
                                         const LoopInfo *LI) {
  // Find the loop-defined value.
  Loop *L = LI->getLoopFor(PN->getParent());
  if (PN->getNumIncomingValues() != 2)
    return true;

  // Find the value from previous iteration.
  auto *PrevValue = dyn_cast<Instruction>(PN->getIncomingValue(0));
  if (!PrevValue || LI->getLoopFor(PrevValue->getParent()) != L)
    PrevValue = dyn_cast<Instruction>(PN->getIncomingValue(1));
  if (!PrevValue || LI->getLoopFor(PrevValue->getParent()) != L)
    return true;

  // If a new pointer is loaded in the loop, the pointer references a different
  // object in every iteration.  E.g.:
  //    for (i)
  //       int *p = a[i];
  //       ...
  if (auto *Load = dyn_cast<LoadInst>(PrevValue))
    if (!L->isLoopInvariant(Load->getPointerOperand()))
      return false;
  return true;
}

Value *llvm::GetUnderlyingObject(Value *V, const DataLayout &DL,
                                 unsigned MaxLookup) {
  if (!V->getType()->isPointerTy())
    return V;
  for (unsigned Count = 0; MaxLookup == 0 || Count < MaxLookup; ++Count) {
    if (GEPOperator *GEP = dyn_cast<GEPOperator>(V)) {
      V = GEP->getPointerOperand();
    } else if (Operator::getOpcode(V) == Instruction::BitCast ||
               Operator::getOpcode(V) == Instruction::AddrSpaceCast) {
      V = cast<Operator>(V)->getOperand(0);
    } else if (GlobalAlias *GA = dyn_cast<GlobalAlias>(V)) {
      if (GA->isInterposable())
        return V;
      V = GA->getAliasee();
    } else if (isa<AllocaInst>(V)) {
      // An alloca can't be further simplified.
      return V;
    } else {
      if (auto *Call = dyn_cast<CallBase>(V)) {
        // CaptureTracking can know about special capturing properties of some
        // intrinsics like launder.invariant.group, that can't be expressed with
        // the attributes, but have properties like returning aliasing pointer.
        // Because some analysis may assume that nocaptured pointer is not
        // returned from some special intrinsic (because function would have to
        // be marked with returns attribute), it is crucial to use this function
        // because it should be in sync with CaptureTracking. Not using it may
        // cause weird miscompilations where 2 aliasing pointers are assumed to
        // noalias.
        if (auto *RP = getArgumentAliasingToReturnedPointer(Call)) {
          V = RP;
          continue;
        }
      }

      // See if InstructionSimplify knows any relevant tricks.
      if (Instruction *I = dyn_cast<Instruction>(V))
        // TODO: Acquire a DominatorTree and AssumptionCache and use them.
        if (Value *Simplified = SimplifyInstruction(I, {DL, I})) {
          V = Simplified;
          continue;
        }

      return V;
    }
    assert(V->getType()->isPointerTy() && "Unexpected operand type!");
  }
  return V;
}

void llvm::GetUnderlyingObjects(Value *V, SmallVectorImpl<Value *> &Objects,
                                const DataLayout &DL, LoopInfo *LI,
                                unsigned MaxLookup) {
  SmallPtrSet<Value *, 4> Visited;
  SmallVector<Value *, 4> Worklist;
  Worklist.push_back(V);
  do {
    Value *P = Worklist.pop_back_val();
    P = GetUnderlyingObject(P, DL, MaxLookup);

    if (!Visited.insert(P).second)
      continue;

    if (SelectInst *SI = dyn_cast<SelectInst>(P)) {
      Worklist.push_back(SI->getTrueValue());
      Worklist.push_back(SI->getFalseValue());
      continue;
    }

    if (PHINode *PN = dyn_cast<PHINode>(P)) {
      // If this PHI changes the underlying object in every iteration of the
      // loop, don't look through it.  Consider:
      //   int **A;
      //   for (i) {
      //     Prev = Curr;     // Prev = PHI (Prev_0, Curr)
      //     Curr = A[i];
      //     *Prev, *Curr;
      //
      // Prev is tracking Curr one iteration behind so they refer to different
      // underlying objects.
      if (!LI || !LI->isLoopHeader(PN->getParent()) ||
          isSameUnderlyingObjectInLoop(PN, LI))
        for (Value *IncValue : PN->incoming_values())
          Worklist.push_back(IncValue);
      continue;
    }

    Objects.push_back(P);
  } while (!Worklist.empty());
}

/// This is the function that does the work of looking through basic
/// ptrtoint+arithmetic+inttoptr sequences.
static const Value *getUnderlyingObjectFromInt(const Value *V) {
  do {
    if (const Operator *U = dyn_cast<Operator>(V)) {
      // If we find a ptrtoint, we can transfer control back to the
      // regular getUnderlyingObjectFromInt.
      if (U->getOpcode() == Instruction::PtrToInt)
        return U->getOperand(0);
      // If we find an add of a constant, a multiplied value, or a phi, it's
      // likely that the other operand will lead us to the base
      // object. We don't have to worry about the case where the
      // object address is somehow being computed by the multiply,
      // because our callers only care when the result is an
      // identifiable object.
      if (U->getOpcode() != Instruction::Add ||
          (!isa<ConstantInt>(U->getOperand(1)) &&
           Operator::getOpcode(U->getOperand(1)) != Instruction::Mul &&
           !isa<PHINode>(U->getOperand(1))))
        return V;
      V = U->getOperand(0);
    } else {
      return V;
    }
    assert(V->getType()->isIntegerTy() && "Unexpected operand type!");
  } while (true);
}

/// This is a wrapper around GetUnderlyingObjects and adds support for basic
/// ptrtoint+arithmetic+inttoptr sequences.
/// It returns false if unidentified object is found in GetUnderlyingObjects.
bool llvm::getUnderlyingObjectsForCodeGen(const Value *V,
                          SmallVectorImpl<Value *> &Objects,
                          const DataLayout &DL) {
  SmallPtrSet<const Value *, 16> Visited;
  SmallVector<const Value *, 4> Working(1, V);
  do {
    V = Working.pop_back_val();

    SmallVector<Value *, 4> Objs;
    GetUnderlyingObjects(const_cast<Value *>(V), Objs, DL);

    for (Value *V : Objs) {
      if (!Visited.insert(V).second)
        continue;
      if (Operator::getOpcode(V) == Instruction::IntToPtr) {
        const Value *O =
          getUnderlyingObjectFromInt(cast<User>(V)->getOperand(0));
        if (O->getType()->isPointerTy()) {
          Working.push_back(O);
          continue;
        }
      }
      // If GetUnderlyingObjects fails to find an identifiable object,
      // getUnderlyingObjectsForCodeGen also fails for safety.
      if (!isIdentifiedObject(V)) {
        Objects.clear();
        return false;
      }
      Objects.push_back(const_cast<Value *>(V));
    }
  } while (!Working.empty());
  return true;
}

/// Return true if the only users of this pointer are lifetime markers.
bool llvm::onlyUsedByLifetimeMarkers(const Value *V) {
  for (const User *U : V->users()) {
    const IntrinsicInst *II = dyn_cast<IntrinsicInst>(U);
    if (!II) return false;

    if (!II->isLifetimeStartOrEnd())
      return false;
  }
  return true;
}

bool llvm::isSafeToSpeculativelyExecute(const Value *V,
                                        const Instruction *CtxI,
                                        const DominatorTree *DT) {
  const Operator *Inst = dyn_cast<Operator>(V);
  if (!Inst)
    return false;

  for (unsigned i = 0, e = Inst->getNumOperands(); i != e; ++i)
    if (Constant *C = dyn_cast<Constant>(Inst->getOperand(i)))
      if (C->canTrap())
        return false;

  switch (Inst->getOpcode()) {
  default:
    return true;
  case Instruction::UDiv:
  case Instruction::URem: {
    // x / y is undefined if y == 0.
    const APInt *V;
    if (match(Inst->getOperand(1), m_APInt(V)))
      return *V != 0;
    return false;
  }
  case Instruction::SDiv:
  case Instruction::SRem: {
    // x / y is undefined if y == 0 or x == INT_MIN and y == -1
    const APInt *Numerator, *Denominator;
    if (!match(Inst->getOperand(1), m_APInt(Denominator)))
      return false;
    // We cannot hoist this division if the denominator is 0.
    if (*Denominator == 0)
      return false;
    // It's safe to hoist if the denominator is not 0 or -1.
    if (*Denominator != -1)
      return true;
    // At this point we know that the denominator is -1.  It is safe to hoist as
    // long we know that the numerator is not INT_MIN.
    if (match(Inst->getOperand(0), m_APInt(Numerator)))
      return !Numerator->isMinSignedValue();
    // The numerator *might* be MinSignedValue.
    return false;
  }
  case Instruction::Load: {
    const LoadInst *LI = cast<LoadInst>(Inst);
    if (!LI->isUnordered() ||
        // Speculative load may create a race that did not exist in the source.
        LI->getFunction()->hasFnAttribute(Attribute::SanitizeThread) ||
        // Speculative load may load data from dirty regions.
        LI->getFunction()->hasFnAttribute(Attribute::SanitizeAddress) ||
        LI->getFunction()->hasFnAttribute(Attribute::SanitizeHWAddress))
      return false;
    const DataLayout &DL = LI->getModule()->getDataLayout();
    return isDereferenceableAndAlignedPointer(LI->getPointerOperand(),
                                              LI->getAlignment(), DL, CtxI, DT);
  }
  case Instruction::Call: {
    auto *CI = cast<const CallInst>(Inst);
    const Function *Callee = CI->getCalledFunction();

    // The called function could have undefined behavior or side-effects, even
    // if marked readnone nounwind.
    return Callee && Callee->isSpeculatable();
  }
  case Instruction::VAArg:
  case Instruction::Alloca:
  case Instruction::Invoke:
  case Instruction::PHI:
  case Instruction::Store:
  case Instruction::Ret:
  case Instruction::Br:
  case Instruction::IndirectBr:
  case Instruction::Switch:
  case Instruction::Unreachable:
  case Instruction::Fence:
  case Instruction::AtomicRMW:
  case Instruction::AtomicCmpXchg:
  case Instruction::LandingPad:
  case Instruction::Resume:
  case Instruction::CatchSwitch:
  case Instruction::CatchPad:
  case Instruction::CatchRet:
  case Instruction::CleanupPad:
  case Instruction::CleanupRet:
    return false; // Misc instructions which have effects
  }
}

bool llvm::mayBeMemoryDependent(const Instruction &I) {
  return I.mayReadOrWriteMemory() || !isSafeToSpeculativelyExecute(&I);
}

OverflowResult llvm::computeOverflowForUnsignedMul(
    const Value *LHS, const Value *RHS, const DataLayout &DL,
    AssumptionCache *AC, const Instruction *CxtI, const DominatorTree *DT,
    bool UseInstrInfo) {
  // Multiplying n * m significant bits yields a result of n + m significant
  // bits. If the total number of significant bits does not exceed the
  // result bit width (minus 1), there is no overflow.
  // This means if we have enough leading zero bits in the operands
  // we can guarantee that the result does not overflow.
  // Ref: "Hacker's Delight" by Henry Warren
  unsigned BitWidth = LHS->getType()->getScalarSizeInBits();
  KnownBits LHSKnown(BitWidth);
  KnownBits RHSKnown(BitWidth);
  computeKnownBits(LHS, LHSKnown, DL, /*Depth=*/0, AC, CxtI, DT, nullptr,
                   UseInstrInfo);
  computeKnownBits(RHS, RHSKnown, DL, /*Depth=*/0, AC, CxtI, DT, nullptr,
                   UseInstrInfo);
  // Note that underestimating the number of zero bits gives a more
  // conservative answer.
  unsigned ZeroBits = LHSKnown.countMinLeadingZeros() +
                      RHSKnown.countMinLeadingZeros();
  // First handle the easy case: if we have enough zero bits there's
  // definitely no overflow.
  if (ZeroBits >= BitWidth)
    return OverflowResult::NeverOverflows;

  // Get the largest possible values for each operand.
  APInt LHSMax = ~LHSKnown.Zero;
  APInt RHSMax = ~RHSKnown.Zero;

  // We know the multiply operation doesn't overflow if the maximum values for
  // each operand will not overflow after we multiply them together.
  bool MaxOverflow;
  (void)LHSMax.umul_ov(RHSMax, MaxOverflow);
  if (!MaxOverflow)
    return OverflowResult::NeverOverflows;

  // We know it always overflows if multiplying the smallest possible values for
  // the operands also results in overflow.
  bool MinOverflow;
  (void)LHSKnown.One.umul_ov(RHSKnown.One, MinOverflow);
  if (MinOverflow)
    return OverflowResult::AlwaysOverflows;

  return OverflowResult::MayOverflow;
}

OverflowResult
llvm::computeOverflowForSignedMul(const Value *LHS, const Value *RHS,
                                  const DataLayout &DL, AssumptionCache *AC,
                                  const Instruction *CxtI,
                                  const DominatorTree *DT, bool UseInstrInfo) {
  // Multiplying n * m significant bits yields a result of n + m significant
  // bits. If the total number of significant bits does not exceed the
  // result bit width (minus 1), there is no overflow.
  // This means if we have enough leading sign bits in the operands
  // we can guarantee that the result does not overflow.
  // Ref: "Hacker's Delight" by Henry Warren
  unsigned BitWidth = LHS->getType()->getScalarSizeInBits();

  // Note that underestimating the number of sign bits gives a more
  // conservative answer.
  unsigned SignBits = ComputeNumSignBits(LHS, DL, 0, AC, CxtI, DT) +
                      ComputeNumSignBits(RHS, DL, 0, AC, CxtI, DT);

  // First handle the easy case: if we have enough sign bits there's
  // definitely no overflow.
  if (SignBits > BitWidth + 1)
    return OverflowResult::NeverOverflows;

  // There are two ambiguous cases where there can be no overflow:
  //   SignBits == BitWidth + 1    and
  //   SignBits == BitWidth
  // The second case is difficult to check, therefore we only handle the
  // first case.
  if (SignBits == BitWidth + 1) {
    // It overflows only when both arguments are negative and the true
    // product is exactly the minimum negative number.
    // E.g. mul i16 with 17 sign bits: 0xff00 * 0xff80 = 0x8000
    // For simplicity we just check if at least one side is not negative.
    KnownBits LHSKnown = computeKnownBits(LHS, DL, /*Depth=*/0, AC, CxtI, DT,
                                          nullptr, UseInstrInfo);
    KnownBits RHSKnown = computeKnownBits(RHS, DL, /*Depth=*/0, AC, CxtI, DT,
                                          nullptr, UseInstrInfo);
    if (LHSKnown.isNonNegative() || RHSKnown.isNonNegative())
      return OverflowResult::NeverOverflows;
  }
  return OverflowResult::MayOverflow;
}

OverflowResult llvm::computeOverflowForUnsignedAdd(
    const Value *LHS, const Value *RHS, const DataLayout &DL,
    AssumptionCache *AC, const Instruction *CxtI, const DominatorTree *DT,
    bool UseInstrInfo) {
  KnownBits LHSKnown = computeKnownBits(LHS, DL, /*Depth=*/0, AC, CxtI, DT,
                                        nullptr, UseInstrInfo);
  if (LHSKnown.isNonNegative() || LHSKnown.isNegative()) {
    KnownBits RHSKnown = computeKnownBits(RHS, DL, /*Depth=*/0, AC, CxtI, DT,
                                          nullptr, UseInstrInfo);

    if (LHSKnown.isNegative() && RHSKnown.isNegative()) {
      // The sign bit is set in both cases: this MUST overflow.
      return OverflowResult::AlwaysOverflows;
    }

    if (LHSKnown.isNonNegative() && RHSKnown.isNonNegative()) {
      // The sign bit is clear in both cases: this CANNOT overflow.
      return OverflowResult::NeverOverflows;
    }
  }

  return OverflowResult::MayOverflow;
}

/// Return true if we can prove that adding the two values of the
/// knownbits will not overflow.
/// Otherwise return false.
static bool checkRippleForSignedAdd(const KnownBits &LHSKnown,
                                    const KnownBits &RHSKnown) {
  // Addition of two 2's complement numbers having opposite signs will never
  // overflow.
  if ((LHSKnown.isNegative() && RHSKnown.isNonNegative()) ||
      (LHSKnown.isNonNegative() && RHSKnown.isNegative()))
    return true;

  // If either of the values is known to be non-negative, adding them can only
  // overflow if the second is also non-negative, so we can assume that.
  // Two non-negative numbers will only overflow if there is a carry to the
  // sign bit, so we can check if even when the values are as big as possible
  // there is no overflow to the sign bit.
  if (LHSKnown.isNonNegative() || RHSKnown.isNonNegative()) {
    APInt MaxLHS = ~LHSKnown.Zero;
    MaxLHS.clearSignBit();
    APInt MaxRHS = ~RHSKnown.Zero;
    MaxRHS.clearSignBit();
    APInt Result = std::move(MaxLHS) + std::move(MaxRHS);
    return Result.isSignBitClear();
  }

  // If either of the values is known to be negative, adding them can only
  // overflow if the second is also negative, so we can assume that.
  // Two negative number will only overflow if there is no carry to the sign
  // bit, so we can check if even when the values are as small as possible
  // there is overflow to the sign bit.
  if (LHSKnown.isNegative() || RHSKnown.isNegative()) {
    APInt MinLHS = LHSKnown.One;
    MinLHS.clearSignBit();
    APInt MinRHS = RHSKnown.One;
    MinRHS.clearSignBit();
    APInt Result = std::move(MinLHS) + std::move(MinRHS);
    return Result.isSignBitSet();
  }

  // If we reached here it means that we know nothing about the sign bits.
  // In this case we can't know if there will be an overflow, since by
  // changing the sign bits any two values can be made to overflow.
  return false;
}

static OverflowResult computeOverflowForSignedAdd(const Value *LHS,
                                                  const Value *RHS,
                                                  const AddOperator *Add,
                                                  const DataLayout &DL,
                                                  AssumptionCache *AC,
                                                  const Instruction *CxtI,
                                                  const DominatorTree *DT) {
  if (Add && Add->hasNoSignedWrap()) {
    return OverflowResult::NeverOverflows;
  }

  // If LHS and RHS each have at least two sign bits, the addition will look
  // like
  //
  // XX..... +
  // YY.....
  //
  // If the carry into the most significant position is 0, X and Y can't both
  // be 1 and therefore the carry out of the addition is also 0.
  //
  // If the carry into the most significant position is 1, X and Y can't both
  // be 0 and therefore the carry out of the addition is also 1.
  //
  // Since the carry into the most significant position is always equal to
  // the carry out of the addition, there is no signed overflow.
  if (ComputeNumSignBits(LHS, DL, 0, AC, CxtI, DT) > 1 &&
      ComputeNumSignBits(RHS, DL, 0, AC, CxtI, DT) > 1)
    return OverflowResult::NeverOverflows;

  KnownBits LHSKnown = computeKnownBits(LHS, DL, /*Depth=*/0, AC, CxtI, DT);
  KnownBits RHSKnown = computeKnownBits(RHS, DL, /*Depth=*/0, AC, CxtI, DT);

  if (checkRippleForSignedAdd(LHSKnown, RHSKnown))
    return OverflowResult::NeverOverflows;

  // The remaining code needs Add to be available. Early returns if not so.
  if (!Add)
    return OverflowResult::MayOverflow;

  // If the sign of Add is the same as at least one of the operands, this add
  // CANNOT overflow. This is particularly useful when the sum is
  // @llvm.assume'ed non-negative rather than proved so from analyzing its
  // operands.
  bool LHSOrRHSKnownNonNegative =
      (LHSKnown.isNonNegative() || RHSKnown.isNonNegative());
  bool LHSOrRHSKnownNegative =
      (LHSKnown.isNegative() || RHSKnown.isNegative());
  if (LHSOrRHSKnownNonNegative || LHSOrRHSKnownNegative) {
    KnownBits AddKnown = computeKnownBits(Add, DL, /*Depth=*/0, AC, CxtI, DT);
    if ((AddKnown.isNonNegative() && LHSOrRHSKnownNonNegative) ||
        (AddKnown.isNegative() && LHSOrRHSKnownNegative)) {
      return OverflowResult::NeverOverflows;
    }
  }

  return OverflowResult::MayOverflow;
}

OverflowResult llvm::computeOverflowForUnsignedSub(const Value *LHS,
                                                   const Value *RHS,
                                                   const DataLayout &DL,
                                                   AssumptionCache *AC,
                                                   const Instruction *CxtI,
                                                   const DominatorTree *DT) {
  KnownBits LHSKnown = computeKnownBits(LHS, DL, /*Depth=*/0, AC, CxtI, DT);
  if (LHSKnown.isNonNegative() || LHSKnown.isNegative()) {
    KnownBits RHSKnown = computeKnownBits(RHS, DL, /*Depth=*/0, AC, CxtI, DT);

    // If the LHS is negative and the RHS is non-negative, no unsigned wrap.
    if (LHSKnown.isNegative() && RHSKnown.isNonNegative())
      return OverflowResult::NeverOverflows;

    // If the LHS is non-negative and the RHS negative, we always wrap.
    if (LHSKnown.isNonNegative() && RHSKnown.isNegative())
      return OverflowResult::AlwaysOverflows;
  }

  return OverflowResult::MayOverflow;
}

OverflowResult llvm::computeOverflowForSignedSub(const Value *LHS,
                                                 const Value *RHS,
                                                 const DataLayout &DL,
                                                 AssumptionCache *AC,
                                                 const Instruction *CxtI,
                                                 const DominatorTree *DT) {
  // If LHS and RHS each have at least two sign bits, the subtraction
  // cannot overflow.
  if (ComputeNumSignBits(LHS, DL, 0, AC, CxtI, DT) > 1 &&
      ComputeNumSignBits(RHS, DL, 0, AC, CxtI, DT) > 1)
    return OverflowResult::NeverOverflows;

  KnownBits LHSKnown = computeKnownBits(LHS, DL, 0, AC, CxtI, DT);

  KnownBits RHSKnown = computeKnownBits(RHS, DL, 0, AC, CxtI, DT);

  // Subtraction of two 2's complement numbers having identical signs will
  // never overflow.
  if ((LHSKnown.isNegative() && RHSKnown.isNegative()) ||
      (LHSKnown.isNonNegative() && RHSKnown.isNonNegative()))
    return OverflowResult::NeverOverflows;

  // TODO: implement logic similar to checkRippleForAdd
  return OverflowResult::MayOverflow;
}

bool llvm::isOverflowIntrinsicNoWrap(const IntrinsicInst *II,
                                     const DominatorTree &DT) {
#ifndef NDEBUG
  auto IID = II->getIntrinsicID();
  assert((IID == Intrinsic::sadd_with_overflow ||
          IID == Intrinsic::uadd_with_overflow ||
          IID == Intrinsic::ssub_with_overflow ||
          IID == Intrinsic::usub_with_overflow ||
          IID == Intrinsic::smul_with_overflow ||
          IID == Intrinsic::umul_with_overflow) &&
         "Not an overflow intrinsic!");
#endif

  SmallVector<const BranchInst *, 2> GuardingBranches;
  SmallVector<const ExtractValueInst *, 2> Results;

  for (const User *U : II->users()) {
    if (const auto *EVI = dyn_cast<ExtractValueInst>(U)) {
      assert(EVI->getNumIndices() == 1 && "Obvious from CI's type");

      if (EVI->getIndices()[0] == 0)
        Results.push_back(EVI);
      else {
        assert(EVI->getIndices()[0] == 1 && "Obvious from CI's type");

        for (const auto *U : EVI->users())
          if (const auto *B = dyn_cast<BranchInst>(U)) {
            assert(B->isConditional() && "How else is it using an i1?");
            GuardingBranches.push_back(B);
          }
      }
    } else {
      // We are using the aggregate directly in a way we don't want to analyze
      // here (storing it to a global, say).
      return false;
    }
  }

  auto AllUsesGuardedByBranch = [&](const BranchInst *BI) {
    BasicBlockEdge NoWrapEdge(BI->getParent(), BI->getSuccessor(1));
    if (!NoWrapEdge.isSingleEdge())
      return false;

    // Check if all users of the add are provably no-wrap.
    for (const auto *Result : Results) {
      // If the extractvalue itself is not executed on overflow, the we don't
      // need to check each use separately, since domination is transitive.
      if (DT.dominates(NoWrapEdge, Result->getParent()))
        continue;

      for (auto &RU : Result->uses())
        if (!DT.dominates(NoWrapEdge, RU))
          return false;
    }

    return true;
  };

  return llvm::any_of(GuardingBranches, AllUsesGuardedByBranch);
}


OverflowResult llvm::computeOverflowForSignedAdd(const AddOperator *Add,
                                                 const DataLayout &DL,
                                                 AssumptionCache *AC,
                                                 const Instruction *CxtI,
                                                 const DominatorTree *DT) {
  return ::computeOverflowForSignedAdd(Add->getOperand(0), Add->getOperand(1),
                                       Add, DL, AC, CxtI, DT);
}

OverflowResult llvm::computeOverflowForSignedAdd(const Value *LHS,
                                                 const Value *RHS,
                                                 const DataLayout &DL,
                                                 AssumptionCache *AC,
                                                 const Instruction *CxtI,
                                                 const DominatorTree *DT) {
  return ::computeOverflowForSignedAdd(LHS, RHS, nullptr, DL, AC, CxtI, DT);
}

bool llvm::isGuaranteedToTransferExecutionToSuccessor(const Instruction *I) {
  // A memory operation returns normally if it isn't volatile. A volatile
  // operation is allowed to trap.
  //
  // An atomic operation isn't guaranteed to return in a reasonable amount of
  // time because it's possible for another thread to interfere with it for an
  // arbitrary length of time, but programs aren't allowed to rely on that.
  if (const LoadInst *LI = dyn_cast<LoadInst>(I))
    return !LI->isVolatile();
  if (const StoreInst *SI = dyn_cast<StoreInst>(I))
    return !SI->isVolatile();
  if (const AtomicCmpXchgInst *CXI = dyn_cast<AtomicCmpXchgInst>(I))
    return !CXI->isVolatile();
  if (const AtomicRMWInst *RMWI = dyn_cast<AtomicRMWInst>(I))
    return !RMWI->isVolatile();
  if (const MemIntrinsic *MII = dyn_cast<MemIntrinsic>(I))
    return !MII->isVolatile();

  // If there is no successor, then execution can't transfer to it.
  if (const auto *CRI = dyn_cast<CleanupReturnInst>(I))
    return !CRI->unwindsToCaller();
  if (const auto *CatchSwitch = dyn_cast<CatchSwitchInst>(I))
    return !CatchSwitch->unwindsToCaller();
  if (isa<ResumeInst>(I))
    return false;
  if (isa<ReturnInst>(I))
    return false;
  if (isa<UnreachableInst>(I))
    return false;

  // Calls can throw, or contain an infinite loop, or kill the process.
  if (auto CS = ImmutableCallSite(I)) {
    // Call sites that throw have implicit non-local control flow.
    if (!CS.doesNotThrow())
      return false;

    // Non-throwing call sites can loop infinitely, call exit/pthread_exit
    // etc. and thus not return.  However, LLVM already assumes that
    //
    //  - Thread exiting actions are modeled as writes to memory invisible to
    //    the program.
    //
    //  - Loops that don't have side effects (side effects are volatile/atomic
    //    stores and IO) always terminate (see http://llvm.org/PR965).
    //    Furthermore IO itself is also modeled as writes to memory invisible to
    //    the program.
    //
    // We rely on those assumptions here, and use the memory effects of the call
    // target as a proxy for checking that it always returns.

    // FIXME: This isn't aggressive enough; a call which only writes to a global
    // is guaranteed to return.
    return CS.onlyReadsMemory() || CS.onlyAccessesArgMemory() ||
           match(I, m_Intrinsic<Intrinsic::assume>()) ||
           match(I, m_Intrinsic<Intrinsic::sideeffect>());
  }

  // Other instructions return normally.
  return true;
}

bool llvm::isGuaranteedToTransferExecutionToSuccessor(const BasicBlock *BB) {
  // TODO: This is slightly consdervative for invoke instruction since exiting
  // via an exception *is* normal control for them.
  for (auto I = BB->begin(), E = BB->end(); I != E; ++I)
    if (!isGuaranteedToTransferExecutionToSuccessor(&*I))
      return false;
  return true;
}

bool llvm::isGuaranteedToExecuteForEveryIteration(const Instruction *I,
                                                  const Loop *L) {
  // The loop header is guaranteed to be executed for every iteration.
  //
  // FIXME: Relax this constraint to cover all basic blocks that are
  // guaranteed to be executed at every iteration.
  if (I->getParent() != L->getHeader()) return false;

  for (const Instruction &LI : *L->getHeader()) {
    if (&LI == I) return true;
    if (!isGuaranteedToTransferExecutionToSuccessor(&LI)) return false;
  }
  llvm_unreachable("Instruction not contained in its own parent basic block.");
}

bool llvm::propagatesFullPoison(const Instruction *I) {
  switch (I->getOpcode()) {
  case Instruction::Add:
  case Instruction::Sub:
  case Instruction::Xor:
  case Instruction::Trunc:
  case Instruction::BitCast:
  case Instruction::AddrSpaceCast:
  case Instruction::Mul:
  case Instruction::Shl:
  case Instruction::GetElementPtr:
    // These operations all propagate poison unconditionally. Note that poison
    // is not any particular value, so xor or subtraction of poison with
    // itself still yields poison, not zero.
    return true;

  case Instruction::AShr:
  case Instruction::SExt:
    // For these operations, one bit of the input is replicated across
    // multiple output bits. A replicated poison bit is still poison.
    return true;

  case Instruction::ICmp:
    // Comparing poison with any value yields poison.  This is why, for
    // instance, x s< (x +nsw 1) can be folded to true.
    return true;

  default:
    return false;
  }
}

const Value *llvm::getGuaranteedNonFullPoisonOp(const Instruction *I) {
  switch (I->getOpcode()) {
    case Instruction::Store:
      return cast<StoreInst>(I)->getPointerOperand();

    case Instruction::Load:
      return cast<LoadInst>(I)->getPointerOperand();

    case Instruction::AtomicCmpXchg:
      return cast<AtomicCmpXchgInst>(I)->getPointerOperand();

    case Instruction::AtomicRMW:
      return cast<AtomicRMWInst>(I)->getPointerOperand();

    case Instruction::UDiv:
    case Instruction::SDiv:
    case Instruction::URem:
    case Instruction::SRem:
      return I->getOperand(1);

    default:
      return nullptr;
  }
}

bool llvm::programUndefinedIfFullPoison(const Instruction *PoisonI) {
  // We currently only look for uses of poison values within the same basic
  // block, as that makes it easier to guarantee that the uses will be
  // executed given that PoisonI is executed.
  //
  // FIXME: Expand this to consider uses beyond the same basic block. To do
  // this, look out for the distinction between post-dominance and strong
  // post-dominance.
  const BasicBlock *BB = PoisonI->getParent();

  // Set of instructions that we have proved will yield poison if PoisonI
  // does.
  SmallSet<const Value *, 16> YieldsPoison;
  SmallSet<const BasicBlock *, 4> Visited;
  YieldsPoison.insert(PoisonI);
  Visited.insert(PoisonI->getParent());

  BasicBlock::const_iterator Begin = PoisonI->getIterator(), End = BB->end();

  unsigned Iter = 0;
  while (Iter++ < MaxDepth) {
    for (auto &I : make_range(Begin, End)) {
      if (&I != PoisonI) {
        const Value *NotPoison = getGuaranteedNonFullPoisonOp(&I);
        if (NotPoison != nullptr && YieldsPoison.count(NotPoison))
          return true;
        if (!isGuaranteedToTransferExecutionToSuccessor(&I))
          return false;
      }

      // Mark poison that propagates from I through uses of I.
      if (YieldsPoison.count(&I)) {
        for (const User *User : I.users()) {
          const Instruction *UserI = cast<Instruction>(User);
          if (propagatesFullPoison(UserI))
            YieldsPoison.insert(User);
        }
      }
    }

    if (auto *NextBB = BB->getSingleSuccessor()) {
      if (Visited.insert(NextBB).second) {
        BB = NextBB;
        Begin = BB->getFirstNonPHI()->getIterator();
        End = BB->end();
        continue;
      }
    }

    break;
  }
  return false;
}

static bool isKnownNonNaN(const Value *V, FastMathFlags FMF) {
  if (FMF.noNaNs())
    return true;

  if (auto *C = dyn_cast<ConstantFP>(V))
    return !C->isNaN();

  if (auto *C = dyn_cast<ConstantDataVector>(V)) {
    if (!C->getElementType()->isFloatingPointTy())
      return false;
    for (unsigned I = 0, E = C->getNumElements(); I < E; ++I) {
      if (C->getElementAsAPFloat(I).isNaN())
        return false;
    }
    return true;
  }

  return false;
}

static bool isKnownNonZero(const Value *V) {
  if (auto *C = dyn_cast<ConstantFP>(V))
    return !C->isZero();

  if (auto *C = dyn_cast<ConstantDataVector>(V)) {
    if (!C->getElementType()->isFloatingPointTy())
      return false;
    for (unsigned I = 0, E = C->getNumElements(); I < E; ++I) {
      if (C->getElementAsAPFloat(I).isZero())
        return false;
    }
    return true;
  }

  return false;
}

/// Match clamp pattern for float types without care about NaNs or signed zeros.
/// Given non-min/max outer cmp/select from the clamp pattern this
/// function recognizes if it can be substitued by a "canonical" min/max
/// pattern.
static SelectPatternResult matchFastFloatClamp(CmpInst::Predicate Pred,
                                               Value *CmpLHS, Value *CmpRHS,
                                               Value *TrueVal, Value *FalseVal,
                                               Value *&LHS, Value *&RHS) {
  // Try to match
  //   X < C1 ? C1 : Min(X, C2) --> Max(C1, Min(X, C2))
  //   X > C1 ? C1 : Max(X, C2) --> Min(C1, Max(X, C2))
  // and return description of the outer Max/Min.

  // First, check if select has inverse order:
  if (CmpRHS == FalseVal) {
    std::swap(TrueVal, FalseVal);
    Pred = CmpInst::getInversePredicate(Pred);
  }

  // Assume success now. If there's no match, callers should not use these anyway.
  LHS = TrueVal;
  RHS = FalseVal;

  const APFloat *FC1;
  if (CmpRHS != TrueVal || !match(CmpRHS, m_APFloat(FC1)) || !FC1->isFinite())
    return {SPF_UNKNOWN, SPNB_NA, false};

  const APFloat *FC2;
  switch (Pred) {
  case CmpInst::FCMP_OLT:
  case CmpInst::FCMP_OLE:
  case CmpInst::FCMP_ULT:
  case CmpInst::FCMP_ULE:
    if (match(FalseVal,
              m_CombineOr(m_OrdFMin(m_Specific(CmpLHS), m_APFloat(FC2)),
                          m_UnordFMin(m_Specific(CmpLHS), m_APFloat(FC2)))) &&
        FC1->compare(*FC2) == APFloat::cmpResult::cmpLessThan)
      return {SPF_FMAXNUM, SPNB_RETURNS_ANY, false};
    break;
  case CmpInst::FCMP_OGT:
  case CmpInst::FCMP_OGE:
  case CmpInst::FCMP_UGT:
  case CmpInst::FCMP_UGE:
    if (match(FalseVal,
              m_CombineOr(m_OrdFMax(m_Specific(CmpLHS), m_APFloat(FC2)),
                          m_UnordFMax(m_Specific(CmpLHS), m_APFloat(FC2)))) &&
        FC1->compare(*FC2) == APFloat::cmpResult::cmpGreaterThan)
      return {SPF_FMINNUM, SPNB_RETURNS_ANY, false};
    break;
  default:
    break;
  }

  return {SPF_UNKNOWN, SPNB_NA, false};
}

/// Recognize variations of:
///   CLAMP(v,l,h) ==> ((v) < (l) ? (l) : ((v) > (h) ? (h) : (v)))
static SelectPatternResult matchClamp(CmpInst::Predicate Pred,
                                      Value *CmpLHS, Value *CmpRHS,
                                      Value *TrueVal, Value *FalseVal) {
  // Swap the select operands and predicate to match the patterns below.
  if (CmpRHS != TrueVal) {
    Pred = ICmpInst::getSwappedPredicate(Pred);
    std::swap(TrueVal, FalseVal);
  }
  const APInt *C1;
  if (CmpRHS == TrueVal && match(CmpRHS, m_APInt(C1))) {
    const APInt *C2;
    // (X <s C1) ? C1 : SMIN(X, C2) ==> SMAX(SMIN(X, C2), C1)
    if (match(FalseVal, m_SMin(m_Specific(CmpLHS), m_APInt(C2))) &&
        C1->slt(*C2) && Pred == CmpInst::ICMP_SLT)
      return {SPF_SMAX, SPNB_NA, false};

    // (X >s C1) ? C1 : SMAX(X, C2) ==> SMIN(SMAX(X, C2), C1)
    if (match(FalseVal, m_SMax(m_Specific(CmpLHS), m_APInt(C2))) &&
        C1->sgt(*C2) && Pred == CmpInst::ICMP_SGT)
      return {SPF_SMIN, SPNB_NA, false};

    // (X <u C1) ? C1 : UMIN(X, C2) ==> UMAX(UMIN(X, C2), C1)
    if (match(FalseVal, m_UMin(m_Specific(CmpLHS), m_APInt(C2))) &&
        C1->ult(*C2) && Pred == CmpInst::ICMP_ULT)
      return {SPF_UMAX, SPNB_NA, false};

    // (X >u C1) ? C1 : UMAX(X, C2) ==> UMIN(UMAX(X, C2), C1)
    if (match(FalseVal, m_UMax(m_Specific(CmpLHS), m_APInt(C2))) &&
        C1->ugt(*C2) && Pred == CmpInst::ICMP_UGT)
      return {SPF_UMIN, SPNB_NA, false};
  }
  return {SPF_UNKNOWN, SPNB_NA, false};
}

/// Recognize variations of:
///   a < c ? min(a,b) : min(b,c) ==> min(min(a,b),min(b,c))
static SelectPatternResult matchMinMaxOfMinMax(CmpInst::Predicate Pred,
                                               Value *CmpLHS, Value *CmpRHS,
                                               Value *TVal, Value *FVal,
                                               unsigned Depth) {
  // TODO: Allow FP min/max with nnan/nsz.
  assert(CmpInst::isIntPredicate(Pred) && "Expected integer comparison");

  Value *A, *B;
  SelectPatternResult L = matchSelectPattern(TVal, A, B, nullptr, Depth + 1);
  if (!SelectPatternResult::isMinOrMax(L.Flavor))
    return {SPF_UNKNOWN, SPNB_NA, false};

  Value *C, *D;
  SelectPatternResult R = matchSelectPattern(FVal, C, D, nullptr, Depth + 1);
  if (L.Flavor != R.Flavor)
    return {SPF_UNKNOWN, SPNB_NA, false};

  // We have something like: x Pred y ? min(a, b) : min(c, d).
  // Try to match the compare to the min/max operations of the select operands.
  // First, make sure we have the right compare predicate.
  switch (L.Flavor) {
  case SPF_SMIN:
    if (Pred == ICmpInst::ICMP_SGT || Pred == ICmpInst::ICMP_SGE) {
      Pred = ICmpInst::getSwappedPredicate(Pred);
      std::swap(CmpLHS, CmpRHS);
    }
    if (Pred == ICmpInst::ICMP_SLT || Pred == ICmpInst::ICMP_SLE)
      break;
    return {SPF_UNKNOWN, SPNB_NA, false};
  case SPF_SMAX:
    if (Pred == ICmpInst::ICMP_SLT || Pred == ICmpInst::ICMP_SLE) {
      Pred = ICmpInst::getSwappedPredicate(Pred);
      std::swap(CmpLHS, CmpRHS);
    }
    if (Pred == ICmpInst::ICMP_SGT || Pred == ICmpInst::ICMP_SGE)
      break;
    return {SPF_UNKNOWN, SPNB_NA, false};
  case SPF_UMIN:
    if (Pred == ICmpInst::ICMP_UGT || Pred == ICmpInst::ICMP_UGE) {
      Pred = ICmpInst::getSwappedPredicate(Pred);
      std::swap(CmpLHS, CmpRHS);
    }
    if (Pred == ICmpInst::ICMP_ULT || Pred == ICmpInst::ICMP_ULE)
      break;
    return {SPF_UNKNOWN, SPNB_NA, false};
  case SPF_UMAX:
    if (Pred == ICmpInst::ICMP_ULT || Pred == ICmpInst::ICMP_ULE) {
      Pred = ICmpInst::getSwappedPredicate(Pred);
      std::swap(CmpLHS, CmpRHS);
    }
    if (Pred == ICmpInst::ICMP_UGT || Pred == ICmpInst::ICMP_UGE)
      break;
    return {SPF_UNKNOWN, SPNB_NA, false};
  default:
    return {SPF_UNKNOWN, SPNB_NA, false};
  }

  // If there is a common operand in the already matched min/max and the other
  // min/max operands match the compare operands (either directly or inverted),
  // then this is min/max of the same flavor.

  // a pred c ? m(a, b) : m(c, b) --> m(m(a, b), m(c, b))
  // ~c pred ~a ? m(a, b) : m(c, b) --> m(m(a, b), m(c, b))
  if (D == B) {
    if ((CmpLHS == A && CmpRHS == C) || (match(C, m_Not(m_Specific(CmpLHS))) &&
                                         match(A, m_Not(m_Specific(CmpRHS)))))
      return {L.Flavor, SPNB_NA, false};
  }
  // a pred d ? m(a, b) : m(b, d) --> m(m(a, b), m(b, d))
  // ~d pred ~a ? m(a, b) : m(b, d) --> m(m(a, b), m(b, d))
  if (C == B) {
    if ((CmpLHS == A && CmpRHS == D) || (match(D, m_Not(m_Specific(CmpLHS))) &&
                                         match(A, m_Not(m_Specific(CmpRHS)))))
      return {L.Flavor, SPNB_NA, false};
  }
  // b pred c ? m(a, b) : m(c, a) --> m(m(a, b), m(c, a))
  // ~c pred ~b ? m(a, b) : m(c, a) --> m(m(a, b), m(c, a))
  if (D == A) {
    if ((CmpLHS == B && CmpRHS == C) || (match(C, m_Not(m_Specific(CmpLHS))) &&
                                         match(B, m_Not(m_Specific(CmpRHS)))))
      return {L.Flavor, SPNB_NA, false};
  }
  // b pred d ? m(a, b) : m(a, d) --> m(m(a, b), m(a, d))
  // ~d pred ~b ? m(a, b) : m(a, d) --> m(m(a, b), m(a, d))
  if (C == A) {
    if ((CmpLHS == B && CmpRHS == D) || (match(D, m_Not(m_Specific(CmpLHS))) &&
                                         match(B, m_Not(m_Specific(CmpRHS)))))
      return {L.Flavor, SPNB_NA, false};
  }

  return {SPF_UNKNOWN, SPNB_NA, false};
}

/// Match non-obvious integer minimum and maximum sequences.
static SelectPatternResult matchMinMax(CmpInst::Predicate Pred,
                                       Value *CmpLHS, Value *CmpRHS,
                                       Value *TrueVal, Value *FalseVal,
                                       Value *&LHS, Value *&RHS,
                                       unsigned Depth) {
  // Assume success. If there's no match, callers should not use these anyway.
  LHS = TrueVal;
  RHS = FalseVal;

  SelectPatternResult SPR = matchClamp(Pred, CmpLHS, CmpRHS, TrueVal, FalseVal);
  if (SPR.Flavor != SelectPatternFlavor::SPF_UNKNOWN)
    return SPR;

  SPR = matchMinMaxOfMinMax(Pred, CmpLHS, CmpRHS, TrueVal, FalseVal, Depth);
  if (SPR.Flavor != SelectPatternFlavor::SPF_UNKNOWN)
    return SPR;

  if (Pred != CmpInst::ICMP_SGT && Pred != CmpInst::ICMP_SLT)
    return {SPF_UNKNOWN, SPNB_NA, false};

  // Z = X -nsw Y
  // (X >s Y) ? 0 : Z ==> (Z >s 0) ? 0 : Z ==> SMIN(Z, 0)
  // (X <s Y) ? 0 : Z ==> (Z <s 0) ? 0 : Z ==> SMAX(Z, 0)
  if (match(TrueVal, m_Zero()) &&
      match(FalseVal, m_NSWSub(m_Specific(CmpLHS), m_Specific(CmpRHS))))
    return {Pred == CmpInst::ICMP_SGT ? SPF_SMIN : SPF_SMAX, SPNB_NA, false};

  // Z = X -nsw Y
  // (X >s Y) ? Z : 0 ==> (Z >s 0) ? Z : 0 ==> SMAX(Z, 0)
  // (X <s Y) ? Z : 0 ==> (Z <s 0) ? Z : 0 ==> SMIN(Z, 0)
  if (match(FalseVal, m_Zero()) &&
      match(TrueVal, m_NSWSub(m_Specific(CmpLHS), m_Specific(CmpRHS))))
    return {Pred == CmpInst::ICMP_SGT ? SPF_SMAX : SPF_SMIN, SPNB_NA, false};

  const APInt *C1;
  if (!match(CmpRHS, m_APInt(C1)))
    return {SPF_UNKNOWN, SPNB_NA, false};

  // An unsigned min/max can be written with a signed compare.
  const APInt *C2;
  if ((CmpLHS == TrueVal && match(FalseVal, m_APInt(C2))) ||
      (CmpLHS == FalseVal && match(TrueVal, m_APInt(C2)))) {
    // Is the sign bit set?
    // (X <s 0) ? X : MAXVAL ==> (X >u MAXVAL) ? X : MAXVAL ==> UMAX
    // (X <s 0) ? MAXVAL : X ==> (X >u MAXVAL) ? MAXVAL : X ==> UMIN
    if (Pred == CmpInst::ICMP_SLT && C1->isNullValue() &&
        C2->isMaxSignedValue())
      return {CmpLHS == TrueVal ? SPF_UMAX : SPF_UMIN, SPNB_NA, false};

    // Is the sign bit clear?
    // (X >s -1) ? MINVAL : X ==> (X <u MINVAL) ? MINVAL : X ==> UMAX
    // (X >s -1) ? X : MINVAL ==> (X <u MINVAL) ? X : MINVAL ==> UMIN
    if (Pred == CmpInst::ICMP_SGT && C1->isAllOnesValue() &&
        C2->isMinSignedValue())
      return {CmpLHS == FalseVal ? SPF_UMAX : SPF_UMIN, SPNB_NA, false};
  }

  // Look through 'not' ops to find disguised signed min/max.
  // (X >s C) ? ~X : ~C ==> (~X <s ~C) ? ~X : ~C ==> SMIN(~X, ~C)
  // (X <s C) ? ~X : ~C ==> (~X >s ~C) ? ~X : ~C ==> SMAX(~X, ~C)
  if (match(TrueVal, m_Not(m_Specific(CmpLHS))) &&
      match(FalseVal, m_APInt(C2)) && ~(*C1) == *C2)
    return {Pred == CmpInst::ICMP_SGT ? SPF_SMIN : SPF_SMAX, SPNB_NA, false};

  // (X >s C) ? ~C : ~X ==> (~X <s ~C) ? ~C : ~X ==> SMAX(~C, ~X)
  // (X <s C) ? ~C : ~X ==> (~X >s ~C) ? ~C : ~X ==> SMIN(~C, ~X)
  if (match(FalseVal, m_Not(m_Specific(CmpLHS))) &&
      match(TrueVal, m_APInt(C2)) && ~(*C1) == *C2)
    return {Pred == CmpInst::ICMP_SGT ? SPF_SMAX : SPF_SMIN, SPNB_NA, false};

  return {SPF_UNKNOWN, SPNB_NA, false};
}

bool llvm::isKnownNegation(const Value *X, const Value *Y, bool NeedNSW) {
  assert(X && Y && "Invalid operand");

  // X = sub (0, Y) || X = sub nsw (0, Y)
  if ((!NeedNSW && match(X, m_Sub(m_ZeroInt(), m_Specific(Y)))) ||
      (NeedNSW && match(X, m_NSWSub(m_ZeroInt(), m_Specific(Y)))))
    return true;

  // Y = sub (0, X) || Y = sub nsw (0, X)
  if ((!NeedNSW && match(Y, m_Sub(m_ZeroInt(), m_Specific(X)))) ||
      (NeedNSW && match(Y, m_NSWSub(m_ZeroInt(), m_Specific(X)))))
    return true;

  // X = sub (A, B), Y = sub (B, A) || X = sub nsw (A, B), Y = sub nsw (B, A)
  Value *A, *B;
  return (!NeedNSW && (match(X, m_Sub(m_Value(A), m_Value(B))) &&
                        match(Y, m_Sub(m_Specific(B), m_Specific(A))))) ||
         (NeedNSW && (match(X, m_NSWSub(m_Value(A), m_Value(B))) &&
                       match(Y, m_NSWSub(m_Specific(B), m_Specific(A)))));
}

static SelectPatternResult matchSelectPattern(CmpInst::Predicate Pred,
                                              FastMathFlags FMF,
                                              Value *CmpLHS, Value *CmpRHS,
                                              Value *TrueVal, Value *FalseVal,
                                              Value *&LHS, Value *&RHS,
                                              unsigned Depth) {
  if (CmpInst::isFPPredicate(Pred)) {
    // IEEE-754 ignores the sign of 0.0 in comparisons. So if the select has one
    // 0.0 operand, set the compare's 0.0 operands to that same value for the
    // purpose of identifying min/max. Disregard vector constants with undefined
    // elements because those can not be back-propagated for analysis.
    Value *OutputZeroVal = nullptr;
    if (match(TrueVal, m_AnyZeroFP()) && !match(FalseVal, m_AnyZeroFP()) &&
        !cast<Constant>(TrueVal)->containsUndefElement())
      OutputZeroVal = TrueVal;
    else if (match(FalseVal, m_AnyZeroFP()) && !match(TrueVal, m_AnyZeroFP()) &&
             !cast<Constant>(FalseVal)->containsUndefElement())
      OutputZeroVal = FalseVal;

    if (OutputZeroVal) {
      if (match(CmpLHS, m_AnyZeroFP()))
        CmpLHS = OutputZeroVal;
      if (match(CmpRHS, m_AnyZeroFP()))
        CmpRHS = OutputZeroVal;
    }
  }

  LHS = CmpLHS;
  RHS = CmpRHS;

  // Signed zero may return inconsistent results between implementations.
  //  (0.0 <= -0.0) ? 0.0 : -0.0 // Returns 0.0
  //  minNum(0.0, -0.0)          // May return -0.0 or 0.0 (IEEE 754-2008 5.3.1)
  // Therefore, we behave conservatively and only proceed if at least one of the
  // operands is known to not be zero or if we don't care about signed zero.
  switch (Pred) {
  default: break;
  // FIXME: Include OGT/OLT/UGT/ULT.
  case CmpInst::FCMP_OGE: case CmpInst::FCMP_OLE:
  case CmpInst::FCMP_UGE: case CmpInst::FCMP_ULE:
    if (!FMF.noSignedZeros() && !isKnownNonZero(CmpLHS) &&
        !isKnownNonZero(CmpRHS))
      return {SPF_UNKNOWN, SPNB_NA, false};
  }

  SelectPatternNaNBehavior NaNBehavior = SPNB_NA;
  bool Ordered = false;

  // When given one NaN and one non-NaN input:
  //   - maxnum/minnum (C99 fmaxf()/fminf()) return the non-NaN input.
  //   - A simple C99 (a < b ? a : b) construction will return 'b' (as the
  //     ordered comparison fails), which could be NaN or non-NaN.
  // so here we discover exactly what NaN behavior is required/accepted.
  if (CmpInst::isFPPredicate(Pred)) {
    bool LHSSafe = isKnownNonNaN(CmpLHS, FMF);
    bool RHSSafe = isKnownNonNaN(CmpRHS, FMF);

    if (LHSSafe && RHSSafe) {
      // Both operands are known non-NaN.
      NaNBehavior = SPNB_RETURNS_ANY;
    } else if (CmpInst::isOrdered(Pred)) {
      // An ordered comparison will return false when given a NaN, so it
      // returns the RHS.
      Ordered = true;
      if (LHSSafe)
        // LHS is non-NaN, so if RHS is NaN then NaN will be returned.
        NaNBehavior = SPNB_RETURNS_NAN;
      else if (RHSSafe)
        NaNBehavior = SPNB_RETURNS_OTHER;
      else
        // Completely unsafe.
        return {SPF_UNKNOWN, SPNB_NA, false};
    } else {
      Ordered = false;
      // An unordered comparison will return true when given a NaN, so it
      // returns the LHS.
      if (LHSSafe)
        // LHS is non-NaN, so if RHS is NaN then non-NaN will be returned.
        NaNBehavior = SPNB_RETURNS_OTHER;
      else if (RHSSafe)
        NaNBehavior = SPNB_RETURNS_NAN;
      else
        // Completely unsafe.
        return {SPF_UNKNOWN, SPNB_NA, false};
    }
  }

  if (TrueVal == CmpRHS && FalseVal == CmpLHS) {
    std::swap(CmpLHS, CmpRHS);
    Pred = CmpInst::getSwappedPredicate(Pred);
    if (NaNBehavior == SPNB_RETURNS_NAN)
      NaNBehavior = SPNB_RETURNS_OTHER;
    else if (NaNBehavior == SPNB_RETURNS_OTHER)
      NaNBehavior = SPNB_RETURNS_NAN;
    Ordered = !Ordered;
  }

  // ([if]cmp X, Y) ? X : Y
  if (TrueVal == CmpLHS && FalseVal == CmpRHS) {
    switch (Pred) {
    default: return {SPF_UNKNOWN, SPNB_NA, false}; // Equality.
    case ICmpInst::ICMP_UGT:
    case ICmpInst::ICMP_UGE: return {SPF_UMAX, SPNB_NA, false};
    case ICmpInst::ICMP_SGT:
    case ICmpInst::ICMP_SGE: return {SPF_SMAX, SPNB_NA, false};
    case ICmpInst::ICMP_ULT:
    case ICmpInst::ICMP_ULE: return {SPF_UMIN, SPNB_NA, false};
    case ICmpInst::ICMP_SLT:
    case ICmpInst::ICMP_SLE: return {SPF_SMIN, SPNB_NA, false};
    case FCmpInst::FCMP_UGT:
    case FCmpInst::FCMP_UGE:
    case FCmpInst::FCMP_OGT:
    case FCmpInst::FCMP_OGE: return {SPF_FMAXNUM, NaNBehavior, Ordered};
    case FCmpInst::FCMP_ULT:
    case FCmpInst::FCMP_ULE:
    case FCmpInst::FCMP_OLT:
    case FCmpInst::FCMP_OLE: return {SPF_FMINNUM, NaNBehavior, Ordered};
    }
  }

  if (isKnownNegation(TrueVal, FalseVal)) {
    // Sign-extending LHS does not change its sign, so TrueVal/FalseVal can
    // match against either LHS or sext(LHS).
    auto MaybeSExtCmpLHS =
        m_CombineOr(m_Specific(CmpLHS), m_SExt(m_Specific(CmpLHS)));
    auto ZeroOrAllOnes = m_CombineOr(m_ZeroInt(), m_AllOnes());
    auto ZeroOrOne = m_CombineOr(m_ZeroInt(), m_One());
    if (match(TrueVal, MaybeSExtCmpLHS)) {
      // Set the return values. If the compare uses the negated value (-X >s 0),
      // swap the return values because the negated value is always 'RHS'.
      LHS = TrueVal;
      RHS = FalseVal;
      if (match(CmpLHS, m_Neg(m_Specific(FalseVal))))
        std::swap(LHS, RHS);

      // (X >s 0) ? X : -X or (X >s -1) ? X : -X --> ABS(X)
      // (-X >s 0) ? -X : X or (-X >s -1) ? -X : X --> ABS(X)
      if (Pred == ICmpInst::ICMP_SGT && match(CmpRHS, ZeroOrAllOnes))
        return {SPF_ABS, SPNB_NA, false};

      // (X <s 0) ? X : -X or (X <s 1) ? X : -X --> NABS(X)
      // (-X <s 0) ? -X : X or (-X <s 1) ? -X : X --> NABS(X)
      if (Pred == ICmpInst::ICMP_SLT && match(CmpRHS, ZeroOrOne))
        return {SPF_NABS, SPNB_NA, false};
    }
    else if (match(FalseVal, MaybeSExtCmpLHS)) {
      // Set the return values. If the compare uses the negated value (-X >s 0),
      // swap the return values because the negated value is always 'RHS'.
      LHS = FalseVal;
      RHS = TrueVal;
      if (match(CmpLHS, m_Neg(m_Specific(TrueVal))))
        std::swap(LHS, RHS);

      // (X >s 0) ? -X : X or (X >s -1) ? -X : X --> NABS(X)
      // (-X >s 0) ? X : -X or (-X >s -1) ? X : -X --> NABS(X)
      if (Pred == ICmpInst::ICMP_SGT && match(CmpRHS, ZeroOrAllOnes))
        return {SPF_NABS, SPNB_NA, false};

      // (X <s 0) ? -X : X or (X <s 1) ? -X : X --> ABS(X)
      // (-X <s 0) ? X : -X or (-X <s 1) ? X : -X --> ABS(X)
      if (Pred == ICmpInst::ICMP_SLT && match(CmpRHS, ZeroOrOne))
        return {SPF_ABS, SPNB_NA, false};
    }
  }

  if (CmpInst::isIntPredicate(Pred))
    return matchMinMax(Pred, CmpLHS, CmpRHS, TrueVal, FalseVal, LHS, RHS, Depth);

  // According to (IEEE 754-2008 5.3.1), minNum(0.0, -0.0) and similar
  // may return either -0.0 or 0.0, so fcmp/select pair has stricter
  // semantics than minNum. Be conservative in such case.
  if (NaNBehavior != SPNB_RETURNS_ANY ||
      (!FMF.noSignedZeros() && !isKnownNonZero(CmpLHS) &&
       !isKnownNonZero(CmpRHS)))
    return {SPF_UNKNOWN, SPNB_NA, false};

  return matchFastFloatClamp(Pred, CmpLHS, CmpRHS, TrueVal, FalseVal, LHS, RHS);
}

/// Helps to match a select pattern in case of a type mismatch.
///
/// The function processes the case when type of true and false values of a
/// select instruction differs from type of the cmp instruction operands because
/// of a cast instruction. The function checks if it is legal to move the cast
/// operation after "select". If yes, it returns the new second value of
/// "select" (with the assumption that cast is moved):
/// 1. As operand of cast instruction when both values of "select" are same cast
/// instructions.
/// 2. As restored constant (by applying reverse cast operation) when the first
/// value of the "select" is a cast operation and the second value is a
/// constant.
/// NOTE: We return only the new second value because the first value could be
/// accessed as operand of cast instruction.
static Value *lookThroughCast(CmpInst *CmpI, Value *V1, Value *V2,
                              Instruction::CastOps *CastOp) {
  auto *Cast1 = dyn_cast<CastInst>(V1);
  if (!Cast1)
    return nullptr;

  *CastOp = Cast1->getOpcode();
  Type *SrcTy = Cast1->getSrcTy();
  if (auto *Cast2 = dyn_cast<CastInst>(V2)) {
    // If V1 and V2 are both the same cast from the same type, look through V1.
    if (*CastOp == Cast2->getOpcode() && SrcTy == Cast2->getSrcTy())
      return Cast2->getOperand(0);
    return nullptr;
  }

  auto *C = dyn_cast<Constant>(V2);
  if (!C)
    return nullptr;

  Constant *CastedTo = nullptr;
  switch (*CastOp) {
  case Instruction::ZExt:
    if (CmpI->isUnsigned())
      CastedTo = ConstantExpr::getTrunc(C, SrcTy);
    break;
  case Instruction::SExt:
    if (CmpI->isSigned())
      CastedTo = ConstantExpr::getTrunc(C, SrcTy, true);
    break;
  case Instruction::Trunc:
    Constant *CmpConst;
    if (match(CmpI->getOperand(1), m_Constant(CmpConst)) &&
        CmpConst->getType() == SrcTy) {
      // Here we have the following case:
      //
      //   %cond = cmp iN %x, CmpConst
      //   %tr = trunc iN %x to iK
      //   %narrowsel = select i1 %cond, iK %t, iK C
      //
      // We can always move trunc after select operation:
      //
      //   %cond = cmp iN %x, CmpConst
      //   %widesel = select i1 %cond, iN %x, iN CmpConst
      //   %tr = trunc iN %widesel to iK
      //
      // Note that C could be extended in any way because we don't care about
      // upper bits after truncation. It can't be abs pattern, because it would
      // look like:
      //
      //   select i1 %cond, x, -x.
      //
      // So only min/max pattern could be matched. Such match requires widened C
      // == CmpConst. That is why set widened C = CmpConst, condition trunc
      // CmpConst == C is checked below.
      CastedTo = CmpConst;
    } else {
      CastedTo = ConstantExpr::getIntegerCast(C, SrcTy, CmpI->isSigned());
    }
    break;
  case Instruction::FPTrunc:
    CastedTo = ConstantExpr::getFPExtend(C, SrcTy, true);
    break;
  case Instruction::FPExt:
    CastedTo = ConstantExpr::getFPTrunc(C, SrcTy, true);
    break;
  case Instruction::FPToUI:
    CastedTo = ConstantExpr::getUIToFP(C, SrcTy, true);
    break;
  case Instruction::FPToSI:
    CastedTo = ConstantExpr::getSIToFP(C, SrcTy, true);
    break;
  case Instruction::UIToFP:
    CastedTo = ConstantExpr::getFPToUI(C, SrcTy, true);
    break;
  case Instruction::SIToFP:
    CastedTo = ConstantExpr::getFPToSI(C, SrcTy, true);
    break;
  default:
    break;
  }

  if (!CastedTo)
    return nullptr;

  // Make sure the cast doesn't lose any information.
  Constant *CastedBack =
      ConstantExpr::getCast(*CastOp, CastedTo, C->getType(), true);
  if (CastedBack != C)
    return nullptr;

  return CastedTo;
}

SelectPatternResult llvm::matchSelectPattern(Value *V, Value *&LHS, Value *&RHS,
                                             Instruction::CastOps *CastOp,
                                             unsigned Depth) {
  if (Depth >= MaxDepth)
    return {SPF_UNKNOWN, SPNB_NA, false};

  SelectInst *SI = dyn_cast<SelectInst>(V);
  if (!SI) return {SPF_UNKNOWN, SPNB_NA, false};

  CmpInst *CmpI = dyn_cast<CmpInst>(SI->getCondition());
  if (!CmpI) return {SPF_UNKNOWN, SPNB_NA, false};

  CmpInst::Predicate Pred = CmpI->getPredicate();
  Value *CmpLHS = CmpI->getOperand(0);
  Value *CmpRHS = CmpI->getOperand(1);
  Value *TrueVal = SI->getTrueValue();
  Value *FalseVal = SI->getFalseValue();
  FastMathFlags FMF;
  if (isa<FPMathOperator>(CmpI))
    FMF = CmpI->getFastMathFlags();

  // Bail out early.
  if (CmpI->isEquality())
    return {SPF_UNKNOWN, SPNB_NA, false};

  // Deal with type mismatches.
  if (CastOp && CmpLHS->getType() != TrueVal->getType()) {
    if (Value *C = lookThroughCast(CmpI, TrueVal, FalseVal, CastOp)) {
      // If this is a potential fmin/fmax with a cast to integer, then ignore
      // -0.0 because there is no corresponding integer value.
      if (*CastOp == Instruction::FPToSI || *CastOp == Instruction::FPToUI)
        FMF.setNoSignedZeros();
      return ::matchSelectPattern(Pred, FMF, CmpLHS, CmpRHS,
                                  cast<CastInst>(TrueVal)->getOperand(0), C,
                                  LHS, RHS, Depth);
    }
    if (Value *C = lookThroughCast(CmpI, FalseVal, TrueVal, CastOp)) {
      // If this is a potential fmin/fmax with a cast to integer, then ignore
      // -0.0 because there is no corresponding integer value.
      if (*CastOp == Instruction::FPToSI || *CastOp == Instruction::FPToUI)
        FMF.setNoSignedZeros();
      return ::matchSelectPattern(Pred, FMF, CmpLHS, CmpRHS,
                                  C, cast<CastInst>(FalseVal)->getOperand(0),
                                  LHS, RHS, Depth);
    }
  }
  return ::matchSelectPattern(Pred, FMF, CmpLHS, CmpRHS, TrueVal, FalseVal,
                              LHS, RHS, Depth);
}

CmpInst::Predicate llvm::getMinMaxPred(SelectPatternFlavor SPF, bool Ordered) {
  if (SPF == SPF_SMIN) return ICmpInst::ICMP_SLT;
  if (SPF == SPF_UMIN) return ICmpInst::ICMP_ULT;
  if (SPF == SPF_SMAX) return ICmpInst::ICMP_SGT;
  if (SPF == SPF_UMAX) return ICmpInst::ICMP_UGT;
  if (SPF == SPF_FMINNUM)
    return Ordered ? FCmpInst::FCMP_OLT : FCmpInst::FCMP_ULT;
  if (SPF == SPF_FMAXNUM)
    return Ordered ? FCmpInst::FCMP_OGT : FCmpInst::FCMP_UGT;
  llvm_unreachable("unhandled!");
}

SelectPatternFlavor llvm::getInverseMinMaxFlavor(SelectPatternFlavor SPF) {
  if (SPF == SPF_SMIN) return SPF_SMAX;
  if (SPF == SPF_UMIN) return SPF_UMAX;
  if (SPF == SPF_SMAX) return SPF_SMIN;
  if (SPF == SPF_UMAX) return SPF_UMIN;
  llvm_unreachable("unhandled!");
}

CmpInst::Predicate llvm::getInverseMinMaxPred(SelectPatternFlavor SPF) {
  return getMinMaxPred(getInverseMinMaxFlavor(SPF));
}

/// Return true if "icmp Pred LHS RHS" is always true.
static bool isTruePredicate(CmpInst::Predicate Pred, const Value *LHS,
                            const Value *RHS, const DataLayout &DL,
                            unsigned Depth) {
  assert(!LHS->getType()->isVectorTy() && "TODO: extend to handle vectors!");
  if (ICmpInst::isTrueWhenEqual(Pred) && LHS == RHS)
    return true;

  switch (Pred) {
  default:
    return false;

  case CmpInst::ICMP_SLE: {
    const APInt *C;

    // LHS s<= LHS +_{nsw} C   if C >= 0
    if (match(RHS, m_NSWAdd(m_Specific(LHS), m_APInt(C))))
      return !C->isNegative();
    return false;
  }

  case CmpInst::ICMP_ULE: {
    const APInt *C;

    // LHS u<= LHS +_{nuw} C   for any C
    if (match(RHS, m_NUWAdd(m_Specific(LHS), m_APInt(C))))
      return true;

    // Match A to (X +_{nuw} CA) and B to (X +_{nuw} CB)
    auto MatchNUWAddsToSameValue = [&](const Value *A, const Value *B,
                                       const Value *&X,
                                       const APInt *&CA, const APInt *&CB) {
      if (match(A, m_NUWAdd(m_Value(X), m_APInt(CA))) &&
          match(B, m_NUWAdd(m_Specific(X), m_APInt(CB))))
        return true;

      // If X & C == 0 then (X | C) == X +_{nuw} C
      if (match(A, m_Or(m_Value(X), m_APInt(CA))) &&
          match(B, m_Or(m_Specific(X), m_APInt(CB)))) {
        KnownBits Known(CA->getBitWidth());
        computeKnownBits(X, Known, DL, Depth + 1, /*AC*/ nullptr,
                         /*CxtI*/ nullptr, /*DT*/ nullptr);
        if (CA->isSubsetOf(Known.Zero) && CB->isSubsetOf(Known.Zero))
          return true;
      }

      return false;
    };

    const Value *X;
    const APInt *CLHS, *CRHS;
    if (MatchNUWAddsToSameValue(LHS, RHS, X, CLHS, CRHS))
      return CLHS->ule(*CRHS);

    return false;
  }
  }
}

/// Return true if "icmp Pred BLHS BRHS" is true whenever "icmp Pred
/// ALHS ARHS" is true.  Otherwise, return None.
static Optional<bool>
isImpliedCondOperands(CmpInst::Predicate Pred, const Value *ALHS,
                      const Value *ARHS, const Value *BLHS, const Value *BRHS,
                      const DataLayout &DL, unsigned Depth) {
  switch (Pred) {
  default:
    return None;

  case CmpInst::ICMP_SLT:
  case CmpInst::ICMP_SLE:
    if (isTruePredicate(CmpInst::ICMP_SLE, BLHS, ALHS, DL, Depth) &&
        isTruePredicate(CmpInst::ICMP_SLE, ARHS, BRHS, DL, Depth))
      return true;
    return None;

  case CmpInst::ICMP_ULT:
  case CmpInst::ICMP_ULE:
    if (isTruePredicate(CmpInst::ICMP_ULE, BLHS, ALHS, DL, Depth) &&
        isTruePredicate(CmpInst::ICMP_ULE, ARHS, BRHS, DL, Depth))
      return true;
    return None;
  }
}

/// Return true if the operands of the two compares match.  IsSwappedOps is true
/// when the operands match, but are swapped.
static bool isMatchingOps(const Value *ALHS, const Value *ARHS,
                          const Value *BLHS, const Value *BRHS,
                          bool &IsSwappedOps) {

  bool IsMatchingOps = (ALHS == BLHS && ARHS == BRHS);
  IsSwappedOps = (ALHS == BRHS && ARHS == BLHS);
  return IsMatchingOps || IsSwappedOps;
}

/// Return true if "icmp1 APred X, Y" implies "icmp2 BPred X, Y" is true.
/// Return false if "icmp1 APred X, Y" implies "icmp2 BPred X, Y" is false.
/// Otherwise, return None if we can't infer anything.
static Optional<bool> isImpliedCondMatchingOperands(CmpInst::Predicate APred,
                                                    CmpInst::Predicate BPred,
                                                    bool AreSwappedOps) {
  // Canonicalize the predicate as if the operands were not commuted.
  if (AreSwappedOps)
    BPred = ICmpInst::getSwappedPredicate(BPred);

  if (CmpInst::isImpliedTrueByMatchingCmp(APred, BPred))
    return true;
  if (CmpInst::isImpliedFalseByMatchingCmp(APred, BPred))
    return false;

  return None;
}

/// Return true if "icmp APred X, C1" implies "icmp BPred X, C2" is true.
/// Return false if "icmp APred X, C1" implies "icmp BPred X, C2" is false.
/// Otherwise, return None if we can't infer anything.
static Optional<bool>
isImpliedCondMatchingImmOperands(CmpInst::Predicate APred,
                                 const ConstantInt *C1,
                                 CmpInst::Predicate BPred,
                                 const ConstantInt *C2) {
  ConstantRange DomCR =
      ConstantRange::makeExactICmpRegion(APred, C1->getValue());
  ConstantRange CR =
      ConstantRange::makeAllowedICmpRegion(BPred, C2->getValue());
  ConstantRange Intersection = DomCR.intersectWith(CR);
  ConstantRange Difference = DomCR.difference(CR);
  if (Intersection.isEmptySet())
    return false;
  if (Difference.isEmptySet())
    return true;
  return None;
}

/// Return true if LHS implies RHS is true.  Return false if LHS implies RHS is
/// false.  Otherwise, return None if we can't infer anything.
static Optional<bool> isImpliedCondICmps(const ICmpInst *LHS,
                                         const ICmpInst *RHS,
                                         const DataLayout &DL, bool LHSIsTrue,
                                         unsigned Depth) {
  Value *ALHS = LHS->getOperand(0);
  Value *ARHS = LHS->getOperand(1);
  // The rest of the logic assumes the LHS condition is true.  If that's not the
  // case, invert the predicate to make it so.
  ICmpInst::Predicate APred =
      LHSIsTrue ? LHS->getPredicate() : LHS->getInversePredicate();

  Value *BLHS = RHS->getOperand(0);
  Value *BRHS = RHS->getOperand(1);
  ICmpInst::Predicate BPred = RHS->getPredicate();

  // Can we infer anything when the two compares have matching operands?
  bool AreSwappedOps;
  if (isMatchingOps(ALHS, ARHS, BLHS, BRHS, AreSwappedOps)) {
    if (Optional<bool> Implication = isImpliedCondMatchingOperands(
            APred, BPred, AreSwappedOps))
      return Implication;
    // No amount of additional analysis will infer the second condition, so
    // early exit.
    return None;
  }

  // Can we infer anything when the LHS operands match and the RHS operands are
  // constants (not necessarily matching)?
  if (ALHS == BLHS && isa<ConstantInt>(ARHS) && isa<ConstantInt>(BRHS)) {
    if (Optional<bool> Implication = isImpliedCondMatchingImmOperands(
            APred, cast<ConstantInt>(ARHS), BPred, cast<ConstantInt>(BRHS)))
      return Implication;
    // No amount of additional analysis will infer the second condition, so
    // early exit.
    return None;
  }

  if (APred == BPred)
    return isImpliedCondOperands(APred, ALHS, ARHS, BLHS, BRHS, DL, Depth);
  return None;
}

/// Return true if LHS implies RHS is true.  Return false if LHS implies RHS is
/// false.  Otherwise, return None if we can't infer anything.  We expect the
/// RHS to be an icmp and the LHS to be an 'and' or an 'or' instruction.
static Optional<bool> isImpliedCondAndOr(const BinaryOperator *LHS,
                                         const ICmpInst *RHS,
                                         const DataLayout &DL, bool LHSIsTrue,
                                         unsigned Depth) {
  // The LHS must be an 'or' or an 'and' instruction.
  assert((LHS->getOpcode() == Instruction::And ||
          LHS->getOpcode() == Instruction::Or) &&
         "Expected LHS to be 'and' or 'or'.");

  assert(Depth <= MaxDepth && "Hit recursion limit");

  // If the result of an 'or' is false, then we know both legs of the 'or' are
  // false.  Similarly, if the result of an 'and' is true, then we know both
  // legs of the 'and' are true.
  Value *ALHS, *ARHS;
  if ((!LHSIsTrue && match(LHS, m_Or(m_Value(ALHS), m_Value(ARHS)))) ||
      (LHSIsTrue && match(LHS, m_And(m_Value(ALHS), m_Value(ARHS))))) {
    // FIXME: Make this non-recursion.
    if (Optional<bool> Implication =
            isImpliedCondition(ALHS, RHS, DL, LHSIsTrue, Depth + 1))
      return Implication;
    if (Optional<bool> Implication =
            isImpliedCondition(ARHS, RHS, DL, LHSIsTrue, Depth + 1))
      return Implication;
    return None;
  }
  return None;
}

Optional<bool> llvm::isImpliedCondition(const Value *LHS, const Value *RHS,
                                        const DataLayout &DL, bool LHSIsTrue,
                                        unsigned Depth) {
  // Bail out when we hit the limit.
  if (Depth == MaxDepth)
    return None;

  // A mismatch occurs when we compare a scalar cmp to a vector cmp, for
  // example.
  if (LHS->getType() != RHS->getType())
    return None;

  Type *OpTy = LHS->getType();
  assert(OpTy->isIntOrIntVectorTy(1) && "Expected integer type only!");

  // LHS ==> RHS by definition
  if (LHS == RHS)
    return LHSIsTrue;

  // FIXME: Extending the code below to handle vectors.
  if (OpTy->isVectorTy())
    return None;

  assert(OpTy->isIntegerTy(1) && "implied by above");

  // Both LHS and RHS are icmps.
  const ICmpInst *LHSCmp = dyn_cast<ICmpInst>(LHS);
  const ICmpInst *RHSCmp = dyn_cast<ICmpInst>(RHS);
  if (LHSCmp && RHSCmp)
    return isImpliedCondICmps(LHSCmp, RHSCmp, DL, LHSIsTrue, Depth);

  // The LHS should be an 'or' or an 'and' instruction.  We expect the RHS to be
  // an icmp. FIXME: Add support for and/or on the RHS.
  const BinaryOperator *LHSBO = dyn_cast<BinaryOperator>(LHS);
  if (LHSBO && RHSCmp) {
    if ((LHSBO->getOpcode() == Instruction::And ||
         LHSBO->getOpcode() == Instruction::Or))
      return isImpliedCondAndOr(LHSBO, RHSCmp, DL, LHSIsTrue, Depth);
  }
  return None;
}

Optional<bool> llvm::isImpliedByDomCondition(const Value *Cond,
                                             const Instruction *ContextI,
                                             const DataLayout &DL) {
  assert(Cond->getType()->isIntOrIntVectorTy(1) && "Condition must be bool");
  if (!ContextI || !ContextI->getParent())
    return None;

  // TODO: This is a poor/cheap way to determine dominance. Should we use a
  // dominator tree (eg, from a SimplifyQuery) instead?
  const BasicBlock *ContextBB = ContextI->getParent();
  const BasicBlock *PredBB = ContextBB->getSinglePredecessor();
  if (!PredBB)
    return None;

  // We need a conditional branch in the predecessor.
  Value *PredCond;
  BasicBlock *TrueBB, *FalseBB;
  if (!match(PredBB->getTerminator(), m_Br(m_Value(PredCond), TrueBB, FalseBB)))
    return None;

  // The branch should get simplified. Don't bother simplifying this condition.
  if (TrueBB == FalseBB)
    return None;

  assert((TrueBB == ContextBB || FalseBB == ContextBB) &&
         "Predecessor block does not point to successor?");

  // Is this condition implied by the predecessor condition?
  bool CondIsTrue = TrueBB == ContextBB;
  return isImpliedCondition(PredCond, Cond, DL, CondIsTrue);
}
