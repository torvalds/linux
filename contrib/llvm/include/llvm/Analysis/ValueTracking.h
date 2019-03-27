//===- llvm/Analysis/ValueTracking.h - Walk computations --------*- C++ -*-===//
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

#ifndef LLVM_ANALYSIS_VALUETRACKING_H
#define LLVM_ANALYSIS_VALUETRACKING_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/Optional.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Intrinsics.h"
#include <cassert>
#include <cstdint>

namespace llvm {

class AddOperator;
class APInt;
class AssumptionCache;
class DataLayout;
class DominatorTree;
class GEPOperator;
class IntrinsicInst;
struct KnownBits;
class Loop;
class LoopInfo;
class MDNode;
class OptimizationRemarkEmitter;
class StringRef;
class TargetLibraryInfo;
class Value;

  /// Determine which bits of V are known to be either zero or one and return
  /// them in the KnownZero/KnownOne bit sets.
  ///
  /// This function is defined on values with integer type, values with pointer
  /// type, and vectors of integers.  In the case
  /// where V is a vector, the known zero and known one values are the
  /// same width as the vector element, and the bit is set only if it is true
  /// for all of the elements in the vector.
  void computeKnownBits(const Value *V, KnownBits &Known,
                        const DataLayout &DL, unsigned Depth = 0,
                        AssumptionCache *AC = nullptr,
                        const Instruction *CxtI = nullptr,
                        const DominatorTree *DT = nullptr,
                        OptimizationRemarkEmitter *ORE = nullptr,
                        bool UseInstrInfo = true);

  /// Returns the known bits rather than passing by reference.
  KnownBits computeKnownBits(const Value *V, const DataLayout &DL,
                             unsigned Depth = 0, AssumptionCache *AC = nullptr,
                             const Instruction *CxtI = nullptr,
                             const DominatorTree *DT = nullptr,
                             OptimizationRemarkEmitter *ORE = nullptr,
                             bool UseInstrInfo = true);

  /// Compute known bits from the range metadata.
  /// \p KnownZero the set of bits that are known to be zero
  /// \p KnownOne the set of bits that are known to be one
  void computeKnownBitsFromRangeMetadata(const MDNode &Ranges,
                                         KnownBits &Known);

  /// Return true if LHS and RHS have no common bits set.
  bool haveNoCommonBitsSet(const Value *LHS, const Value *RHS,
                           const DataLayout &DL,
                           AssumptionCache *AC = nullptr,
                           const Instruction *CxtI = nullptr,
                           const DominatorTree *DT = nullptr,
                           bool UseInstrInfo = true);

  /// Return true if the given value is known to have exactly one bit set when
  /// defined. For vectors return true if every element is known to be a power
  /// of two when defined. Supports values with integer or pointer type and
  /// vectors of integers. If 'OrZero' is set, then return true if the given
  /// value is either a power of two or zero.
  bool isKnownToBeAPowerOfTwo(const Value *V, const DataLayout &DL,
                              bool OrZero = false, unsigned Depth = 0,
                              AssumptionCache *AC = nullptr,
                              const Instruction *CxtI = nullptr,
                              const DominatorTree *DT = nullptr,
                              bool UseInstrInfo = true);

  bool isOnlyUsedInZeroEqualityComparison(const Instruction *CxtI);

  /// Return true if the given value is known to be non-zero when defined. For
  /// vectors, return true if every element is known to be non-zero when
  /// defined. For pointers, if the context instruction and dominator tree are
  /// specified, perform context-sensitive analysis and return true if the
  /// pointer couldn't possibly be null at the specified instruction.
  /// Supports values with integer or pointer type and vectors of integers.
  bool isKnownNonZero(const Value *V, const DataLayout &DL, unsigned Depth = 0,
                      AssumptionCache *AC = nullptr,
                      const Instruction *CxtI = nullptr,
                      const DominatorTree *DT = nullptr,
                      bool UseInstrInfo = true);

  /// Return true if the two given values are negation.
  /// Currently can recoginze Value pair:
  /// 1: <X, Y> if X = sub (0, Y) or Y = sub (0, X)
  /// 2: <X, Y> if X = sub (A, B) and Y = sub (B, A)
  bool isKnownNegation(const Value *X, const Value *Y, bool NeedNSW = false);

  /// Returns true if the give value is known to be non-negative.
  bool isKnownNonNegative(const Value *V, const DataLayout &DL,
                          unsigned Depth = 0,
                          AssumptionCache *AC = nullptr,
                          const Instruction *CxtI = nullptr,
                          const DominatorTree *DT = nullptr,
                          bool UseInstrInfo = true);

  /// Returns true if the given value is known be positive (i.e. non-negative
  /// and non-zero).
  bool isKnownPositive(const Value *V, const DataLayout &DL, unsigned Depth = 0,
                       AssumptionCache *AC = nullptr,
                       const Instruction *CxtI = nullptr,
                       const DominatorTree *DT = nullptr,
                       bool UseInstrInfo = true);

  /// Returns true if the given value is known be negative (i.e. non-positive
  /// and non-zero).
  bool isKnownNegative(const Value *V, const DataLayout &DL, unsigned Depth = 0,
                       AssumptionCache *AC = nullptr,
                       const Instruction *CxtI = nullptr,
                       const DominatorTree *DT = nullptr,
                       bool UseInstrInfo = true);

  /// Return true if the given values are known to be non-equal when defined.
  /// Supports scalar integer types only.
  bool isKnownNonEqual(const Value *V1, const Value *V2, const DataLayout &DL,
                       AssumptionCache *AC = nullptr,
                       const Instruction *CxtI = nullptr,
                       const DominatorTree *DT = nullptr,
                       bool UseInstrInfo = true);

  /// Return true if 'V & Mask' is known to be zero. We use this predicate to
  /// simplify operations downstream. Mask is known to be zero for bits that V
  /// cannot have.
  ///
  /// This function is defined on values with integer type, values with pointer
  /// type, and vectors of integers.  In the case
  /// where V is a vector, the mask, known zero, and known one values are the
  /// same width as the vector element, and the bit is set only if it is true
  /// for all of the elements in the vector.
  bool MaskedValueIsZero(const Value *V, const APInt &Mask,
                         const DataLayout &DL,
                         unsigned Depth = 0, AssumptionCache *AC = nullptr,
                         const Instruction *CxtI = nullptr,
                         const DominatorTree *DT = nullptr,
                         bool UseInstrInfo = true);

  /// Return the number of times the sign bit of the register is replicated into
  /// the other bits. We know that at least 1 bit is always equal to the sign
  /// bit (itself), but other cases can give us information. For example,
  /// immediately after an "ashr X, 2", we know that the top 3 bits are all
  /// equal to each other, so we return 3. For vectors, return the number of
  /// sign bits for the vector element with the mininum number of known sign
  /// bits.
  unsigned ComputeNumSignBits(const Value *Op, const DataLayout &DL,
                              unsigned Depth = 0, AssumptionCache *AC = nullptr,
                              const Instruction *CxtI = nullptr,
                              const DominatorTree *DT = nullptr,
                              bool UseInstrInfo = true);

  /// This function computes the integer multiple of Base that equals V. If
  /// successful, it returns true and returns the multiple in Multiple. If
  /// unsuccessful, it returns false. Also, if V can be simplified to an
  /// integer, then the simplified V is returned in Val. Look through sext only
  /// if LookThroughSExt=true.
  bool ComputeMultiple(Value *V, unsigned Base, Value *&Multiple,
                       bool LookThroughSExt = false,
                       unsigned Depth = 0);

  /// Map a call instruction to an intrinsic ID.  Libcalls which have equivalent
  /// intrinsics are treated as-if they were intrinsics.
  Intrinsic::ID getIntrinsicForCallSite(ImmutableCallSite ICS,
                                        const TargetLibraryInfo *TLI);

  /// Return true if we can prove that the specified FP value is never equal to
  /// -0.0.
  bool CannotBeNegativeZero(const Value *V, const TargetLibraryInfo *TLI,
                            unsigned Depth = 0);

  /// Return true if we can prove that the specified FP value is either NaN or
  /// never less than -0.0.
  ///
  ///      NaN --> true
  ///       +0 --> true
  ///       -0 --> true
  ///   x > +0 --> true
  ///   x < -0 --> false
  bool CannotBeOrderedLessThanZero(const Value *V, const TargetLibraryInfo *TLI);

  /// Return true if the floating-point scalar value is not a NaN or if the
  /// floating-point vector value has no NaN elements. Return false if a value
  /// could ever be NaN.
  bool isKnownNeverNaN(const Value *V, const TargetLibraryInfo *TLI,
                       unsigned Depth = 0);

  /// Return true if we can prove that the specified FP value's sign bit is 0.
  ///
  ///      NaN --> true/false (depending on the NaN's sign bit)
  ///       +0 --> true
  ///       -0 --> false
  ///   x > +0 --> true
  ///   x < -0 --> false
  bool SignBitMustBeZero(const Value *V, const TargetLibraryInfo *TLI);

  /// If the specified value can be set by repeating the same byte in memory,
  /// return the i8 value that it is represented with. This is true for all i8
  /// values obviously, but is also true for i32 0, i32 -1, i16 0xF0F0, double
  /// 0.0 etc. If the value can't be handled with a repeated byte store (e.g.
  /// i16 0x1234), return null. If the value is entirely undef and padding,
  /// return undef.
  Value *isBytewiseValue(Value *V);

  /// Given an aggregrate and an sequence of indices, see if the scalar value
  /// indexed is already around as a register, for example if it were inserted
  /// directly into the aggregrate.
  ///
  /// If InsertBefore is not null, this function will duplicate (modified)
  /// insertvalues when a part of a nested struct is extracted.
  Value *FindInsertedValue(Value *V,
                           ArrayRef<unsigned> idx_range,
                           Instruction *InsertBefore = nullptr);

  /// Analyze the specified pointer to see if it can be expressed as a base
  /// pointer plus a constant offset. Return the base and offset to the caller.
  Value *GetPointerBaseWithConstantOffset(Value *Ptr, int64_t &Offset,
                                          const DataLayout &DL);
  inline const Value *GetPointerBaseWithConstantOffset(const Value *Ptr,
                                                       int64_t &Offset,
                                                       const DataLayout &DL) {
    return GetPointerBaseWithConstantOffset(const_cast<Value *>(Ptr), Offset,
                                            DL);
  }

  /// Returns true if the GEP is based on a pointer to a string (array of
  // \p CharSize integers) and is indexing into this string.
  bool isGEPBasedOnPointerToString(const GEPOperator *GEP,
                                   unsigned CharSize = 8);

  /// Represents offset+length into a ConstantDataArray.
  struct ConstantDataArraySlice {
    /// ConstantDataArray pointer. nullptr indicates a zeroinitializer (a valid
    /// initializer, it just doesn't fit the ConstantDataArray interface).
    const ConstantDataArray *Array;

    /// Slice starts at this Offset.
    uint64_t Offset;

    /// Length of the slice.
    uint64_t Length;

    /// Moves the Offset and adjusts Length accordingly.
    void move(uint64_t Delta) {
      assert(Delta < Length);
      Offset += Delta;
      Length -= Delta;
    }

    /// Convenience accessor for elements in the slice.
    uint64_t operator[](unsigned I) const {
      return Array==nullptr ? 0 : Array->getElementAsInteger(I + Offset);
    }
  };

  /// Returns true if the value \p V is a pointer into a ConstantDataArray.
  /// If successful \p Slice will point to a ConstantDataArray info object
  /// with an appropriate offset.
  bool getConstantDataArrayInfo(const Value *V, ConstantDataArraySlice &Slice,
                                unsigned ElementSize, uint64_t Offset = 0);

  /// This function computes the length of a null-terminated C string pointed to
  /// by V. If successful, it returns true and returns the string in Str. If
  /// unsuccessful, it returns false. This does not include the trailing null
  /// character by default. If TrimAtNul is set to false, then this returns any
  /// trailing null characters as well as any other characters that come after
  /// it.
  bool getConstantStringInfo(const Value *V, StringRef &Str,
                             uint64_t Offset = 0, bool TrimAtNul = true);

  /// If we can compute the length of the string pointed to by the specified
  /// pointer, return 'len+1'.  If we can't, return 0.
  uint64_t GetStringLength(const Value *V, unsigned CharSize = 8);

  /// This function returns call pointer argument that is considered the same by
  /// aliasing rules. You CAN'T use it to replace one value with another.
  const Value *getArgumentAliasingToReturnedPointer(const CallBase *Call);
  inline Value *getArgumentAliasingToReturnedPointer(CallBase *Call) {
    return const_cast<Value *>(getArgumentAliasingToReturnedPointer(
        const_cast<const CallBase *>(Call)));
  }

  // {launder,strip}.invariant.group returns pointer that aliases its argument,
  // and it only captures pointer by returning it.
  // These intrinsics are not marked as nocapture, because returning is
  // considered as capture. The arguments are not marked as returned neither,
  // because it would make it useless.
  bool isIntrinsicReturningPointerAliasingArgumentWithoutCapturing(
      const CallBase *Call);

  /// This method strips off any GEP address adjustments and pointer casts from
  /// the specified value, returning the original object being addressed. Note
  /// that the returned value has pointer type if the specified value does. If
  /// the MaxLookup value is non-zero, it limits the number of instructions to
  /// be stripped off.
  Value *GetUnderlyingObject(Value *V, const DataLayout &DL,
                             unsigned MaxLookup = 6);
  inline const Value *GetUnderlyingObject(const Value *V, const DataLayout &DL,
                                          unsigned MaxLookup = 6) {
    return GetUnderlyingObject(const_cast<Value *>(V), DL, MaxLookup);
  }

  /// This method is similar to GetUnderlyingObject except that it can
  /// look through phi and select instructions and return multiple objects.
  ///
  /// If LoopInfo is passed, loop phis are further analyzed.  If a pointer
  /// accesses different objects in each iteration, we don't look through the
  /// phi node. E.g. consider this loop nest:
  ///
  ///   int **A;
  ///   for (i)
  ///     for (j) {
  ///        A[i][j] = A[i-1][j] * B[j]
  ///     }
  ///
  /// This is transformed by Load-PRE to stash away A[i] for the next iteration
  /// of the outer loop:
  ///
  ///   Curr = A[0];          // Prev_0
  ///   for (i: 1..N) {
  ///     Prev = Curr;        // Prev = PHI (Prev_0, Curr)
  ///     Curr = A[i];
  ///     for (j: 0..N) {
  ///        Curr[j] = Prev[j] * B[j]
  ///     }
  ///   }
  ///
  /// Since A[i] and A[i-1] are independent pointers, getUnderlyingObjects
  /// should not assume that Curr and Prev share the same underlying object thus
  /// it shouldn't look through the phi above.
  void GetUnderlyingObjects(Value *V, SmallVectorImpl<Value *> &Objects,
                            const DataLayout &DL, LoopInfo *LI = nullptr,
                            unsigned MaxLookup = 6);

  /// This is a wrapper around GetUnderlyingObjects and adds support for basic
  /// ptrtoint+arithmetic+inttoptr sequences.
  bool getUnderlyingObjectsForCodeGen(const Value *V,
                            SmallVectorImpl<Value *> &Objects,
                            const DataLayout &DL);

  /// Return true if the only users of this pointer are lifetime markers.
  bool onlyUsedByLifetimeMarkers(const Value *V);

  /// Return true if the instruction does not have any effects besides
  /// calculating the result and does not have undefined behavior.
  ///
  /// This method never returns true for an instruction that returns true for
  /// mayHaveSideEffects; however, this method also does some other checks in
  /// addition. It checks for undefined behavior, like dividing by zero or
  /// loading from an invalid pointer (but not for undefined results, like a
  /// shift with a shift amount larger than the width of the result). It checks
  /// for malloc and alloca because speculatively executing them might cause a
  /// memory leak. It also returns false for instructions related to control
  /// flow, specifically terminators and PHI nodes.
  ///
  /// If the CtxI is specified this method performs context-sensitive analysis
  /// and returns true if it is safe to execute the instruction immediately
  /// before the CtxI.
  ///
  /// If the CtxI is NOT specified this method only looks at the instruction
  /// itself and its operands, so if this method returns true, it is safe to
  /// move the instruction as long as the correct dominance relationships for
  /// the operands and users hold.
  ///
  /// This method can return true for instructions that read memory;
  /// for such instructions, moving them may change the resulting value.
  bool isSafeToSpeculativelyExecute(const Value *V,
                                    const Instruction *CtxI = nullptr,
                                    const DominatorTree *DT = nullptr);

  /// Returns true if the result or effects of the given instructions \p I
  /// depend on or influence global memory.
  /// Memory dependence arises for example if the instruction reads from
  /// memory or may produce effects or undefined behaviour. Memory dependent
  /// instructions generally cannot be reorderd with respect to other memory
  /// dependent instructions or moved into non-dominated basic blocks.
  /// Instructions which just compute a value based on the values of their
  /// operands are not memory dependent.
  bool mayBeMemoryDependent(const Instruction &I);

  /// Return true if it is an intrinsic that cannot be speculated but also
  /// cannot trap.
  bool isAssumeLikeIntrinsic(const Instruction *I);

  /// Return true if it is valid to use the assumptions provided by an
  /// assume intrinsic, I, at the point in the control-flow identified by the
  /// context instruction, CxtI.
  bool isValidAssumeForContext(const Instruction *I, const Instruction *CxtI,
                               const DominatorTree *DT = nullptr);

  enum class OverflowResult { AlwaysOverflows, MayOverflow, NeverOverflows };

  OverflowResult computeOverflowForUnsignedMul(const Value *LHS,
                                               const Value *RHS,
                                               const DataLayout &DL,
                                               AssumptionCache *AC,
                                               const Instruction *CxtI,
                                               const DominatorTree *DT,
                                               bool UseInstrInfo = true);
  OverflowResult computeOverflowForSignedMul(const Value *LHS, const Value *RHS,
                                             const DataLayout &DL,
                                             AssumptionCache *AC,
                                             const Instruction *CxtI,
                                             const DominatorTree *DT,
                                             bool UseInstrInfo = true);
  OverflowResult computeOverflowForUnsignedAdd(const Value *LHS,
                                               const Value *RHS,
                                               const DataLayout &DL,
                                               AssumptionCache *AC,
                                               const Instruction *CxtI,
                                               const DominatorTree *DT,
                                               bool UseInstrInfo = true);
  OverflowResult computeOverflowForSignedAdd(const Value *LHS, const Value *RHS,
                                             const DataLayout &DL,
                                             AssumptionCache *AC = nullptr,
                                             const Instruction *CxtI = nullptr,
                                             const DominatorTree *DT = nullptr);
  /// This version also leverages the sign bit of Add if known.
  OverflowResult computeOverflowForSignedAdd(const AddOperator *Add,
                                             const DataLayout &DL,
                                             AssumptionCache *AC = nullptr,
                                             const Instruction *CxtI = nullptr,
                                             const DominatorTree *DT = nullptr);
  OverflowResult computeOverflowForUnsignedSub(const Value *LHS, const Value *RHS,
                                               const DataLayout &DL,
                                               AssumptionCache *AC,
                                               const Instruction *CxtI,
                                               const DominatorTree *DT);
  OverflowResult computeOverflowForSignedSub(const Value *LHS, const Value *RHS,
                                             const DataLayout &DL,
                                             AssumptionCache *AC,
                                             const Instruction *CxtI,
                                             const DominatorTree *DT);

  /// Returns true if the arithmetic part of the \p II 's result is
  /// used only along the paths control dependent on the computation
  /// not overflowing, \p II being an <op>.with.overflow intrinsic.
  bool isOverflowIntrinsicNoWrap(const IntrinsicInst *II,
                                 const DominatorTree &DT);

  /// Return true if this function can prove that the instruction I will
  /// always transfer execution to one of its successors (including the next
  /// instruction that follows within a basic block). E.g. this is not
  /// guaranteed for function calls that could loop infinitely.
  ///
  /// In other words, this function returns false for instructions that may
  /// transfer execution or fail to transfer execution in a way that is not
  /// captured in the CFG nor in the sequence of instructions within a basic
  /// block.
  ///
  /// Undefined behavior is assumed not to happen, so e.g. division is
  /// guaranteed to transfer execution to the following instruction even
  /// though division by zero might cause undefined behavior.
  bool isGuaranteedToTransferExecutionToSuccessor(const Instruction *I);

  /// Returns true if this block does not contain a potential implicit exit.
  /// This is equivelent to saying that all instructions within the basic block
  /// are guaranteed to transfer execution to their successor within the basic
  /// block. This has the same assumptions w.r.t. undefined behavior as the
  /// instruction variant of this function.
  bool isGuaranteedToTransferExecutionToSuccessor(const BasicBlock *BB);

  /// Return true if this function can prove that the instruction I
  /// is executed for every iteration of the loop L.
  ///
  /// Note that this currently only considers the loop header.
  bool isGuaranteedToExecuteForEveryIteration(const Instruction *I,
                                              const Loop *L);

  /// Return true if this function can prove that I is guaranteed to yield
  /// full-poison (all bits poison) if at least one of its operands are
  /// full-poison (all bits poison).
  ///
  /// The exact rules for how poison propagates through instructions have
  /// not been settled as of 2015-07-10, so this function is conservative
  /// and only considers poison to be propagated in uncontroversial
  /// cases. There is no attempt to track values that may be only partially
  /// poison.
  bool propagatesFullPoison(const Instruction *I);

  /// Return either nullptr or an operand of I such that I will trigger
  /// undefined behavior if I is executed and that operand has a full-poison
  /// value (all bits poison).
  const Value *getGuaranteedNonFullPoisonOp(const Instruction *I);

  /// Return true if this function can prove that if PoisonI is executed
  /// and yields a full-poison value (all bits poison), then that will
  /// trigger undefined behavior.
  ///
  /// Note that this currently only considers the basic block that is
  /// the parent of I.
  bool programUndefinedIfFullPoison(const Instruction *PoisonI);

  /// Specific patterns of select instructions we can match.
  enum SelectPatternFlavor {
    SPF_UNKNOWN = 0,
    SPF_SMIN,                   /// Signed minimum
    SPF_UMIN,                   /// Unsigned minimum
    SPF_SMAX,                   /// Signed maximum
    SPF_UMAX,                   /// Unsigned maximum
    SPF_FMINNUM,                /// Floating point minnum
    SPF_FMAXNUM,                /// Floating point maxnum
    SPF_ABS,                    /// Absolute value
    SPF_NABS                    /// Negated absolute value
  };

  /// Behavior when a floating point min/max is given one NaN and one
  /// non-NaN as input.
  enum SelectPatternNaNBehavior {
    SPNB_NA = 0,                /// NaN behavior not applicable.
    SPNB_RETURNS_NAN,           /// Given one NaN input, returns the NaN.
    SPNB_RETURNS_OTHER,         /// Given one NaN input, returns the non-NaN.
    SPNB_RETURNS_ANY            /// Given one NaN input, can return either (or
                                /// it has been determined that no operands can
                                /// be NaN).
  };

  struct SelectPatternResult {
    SelectPatternFlavor Flavor;
    SelectPatternNaNBehavior NaNBehavior; /// Only applicable if Flavor is
                                          /// SPF_FMINNUM or SPF_FMAXNUM.
    bool Ordered;               /// When implementing this min/max pattern as
                                /// fcmp; select, does the fcmp have to be
                                /// ordered?

    /// Return true if \p SPF is a min or a max pattern.
    static bool isMinOrMax(SelectPatternFlavor SPF) {
      return SPF != SPF_UNKNOWN && SPF != SPF_ABS && SPF != SPF_NABS;
    }
  };

  /// Pattern match integer [SU]MIN, [SU]MAX and ABS idioms, returning the kind
  /// and providing the out parameter results if we successfully match.
  ///
  /// For ABS/NABS, LHS will be set to the input to the abs idiom. RHS will be
  /// the negation instruction from the idiom.
  ///
  /// If CastOp is not nullptr, also match MIN/MAX idioms where the type does
  /// not match that of the original select. If this is the case, the cast
  /// operation (one of Trunc,SExt,Zext) that must be done to transform the
  /// type of LHS and RHS into the type of V is returned in CastOp.
  ///
  /// For example:
  ///   %1 = icmp slt i32 %a, i32 4
  ///   %2 = sext i32 %a to i64
  ///   %3 = select i1 %1, i64 %2, i64 4
  ///
  /// -> LHS = %a, RHS = i32 4, *CastOp = Instruction::SExt
  ///
  SelectPatternResult matchSelectPattern(Value *V, Value *&LHS, Value *&RHS,
                                         Instruction::CastOps *CastOp = nullptr,
                                         unsigned Depth = 0);
  inline SelectPatternResult
  matchSelectPattern(const Value *V, const Value *&LHS, const Value *&RHS,
                     Instruction::CastOps *CastOp = nullptr) {
    Value *L = const_cast<Value*>(LHS);
    Value *R = const_cast<Value*>(RHS);
    auto Result = matchSelectPattern(const_cast<Value*>(V), L, R);
    LHS = L;
    RHS = R;
    return Result;
  }

  /// Return the canonical comparison predicate for the specified
  /// minimum/maximum flavor.
  CmpInst::Predicate getMinMaxPred(SelectPatternFlavor SPF,
                                   bool Ordered = false);

  /// Return the inverse minimum/maximum flavor of the specified flavor.
  /// For example, signed minimum is the inverse of signed maximum.
  SelectPatternFlavor getInverseMinMaxFlavor(SelectPatternFlavor SPF);

  /// Return the canonical inverse comparison predicate for the specified
  /// minimum/maximum flavor.
  CmpInst::Predicate getInverseMinMaxPred(SelectPatternFlavor SPF);

  /// Return true if RHS is known to be implied true by LHS.  Return false if
  /// RHS is known to be implied false by LHS.  Otherwise, return None if no
  /// implication can be made.
  /// A & B must be i1 (boolean) values or a vector of such values. Note that
  /// the truth table for implication is the same as <=u on i1 values (but not
  /// <=s!).  The truth table for both is:
  ///    | T | F (B)
  ///  T | T | F
  ///  F | T | T
  /// (A)
  Optional<bool> isImpliedCondition(const Value *LHS, const Value *RHS,
                                    const DataLayout &DL, bool LHSIsTrue = true,
                                    unsigned Depth = 0);

  /// Return the boolean condition value in the context of the given instruction
  /// if it is known based on dominating conditions.
  Optional<bool> isImpliedByDomCondition(const Value *Cond,
                                         const Instruction *ContextI,
                                         const DataLayout &DL);
} // end namespace llvm

#endif // LLVM_ANALYSIS_VALUETRACKING_H
