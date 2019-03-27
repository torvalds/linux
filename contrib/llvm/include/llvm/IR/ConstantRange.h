//===- ConstantRange.h - Represent a range ----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Represent a range of possible values that may occur when the program is run
// for an integral value.  This keeps track of a lower and upper bound for the
// constant, which MAY wrap around the end of the numeric range.  To do this, it
// keeps track of a [lower, upper) bound, which specifies an interval just like
// STL iterators.  When used with boolean values, the following are important
// ranges: :
//
//  [F, F) = {}     = Empty set
//  [T, F) = {T}
//  [F, T) = {F}
//  [T, T) = {F, T} = Full set
//
// The other integral ranges use min/max values for special range values. For
// example, for 8-bit types, it uses:
// [0, 0)     = {}       = Empty set
// [255, 255) = {0..255} = Full Set
//
// Note that ConstantRange can be used to represent either signed or
// unsigned ranges.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_CONSTANTRANGE_H
#define LLVM_IR_CONSTANTRANGE_H

#include "llvm/ADT/APInt.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/Support/Compiler.h"
#include <cstdint>

namespace llvm {

class MDNode;
class raw_ostream;

/// This class represents a range of values.
class LLVM_NODISCARD ConstantRange {
  APInt Lower, Upper;

public:
  /// Initialize a full (the default) or empty set for the specified bit width.
  explicit ConstantRange(uint32_t BitWidth, bool isFullSet = true);

  /// Initialize a range to hold the single specified value.
  ConstantRange(APInt Value);

  /// Initialize a range of values explicitly. This will assert out if
  /// Lower==Upper and Lower != Min or Max value for its type. It will also
  /// assert out if the two APInt's are not the same bit width.
  ConstantRange(APInt Lower, APInt Upper);

  /// Produce the smallest range such that all values that may satisfy the given
  /// predicate with any value contained within Other is contained in the
  /// returned range.  Formally, this returns a superset of
  /// 'union over all y in Other . { x : icmp op x y is true }'.  If the exact
  /// answer is not representable as a ConstantRange, the return value will be a
  /// proper superset of the above.
  ///
  /// Example: Pred = ult and Other = i8 [2, 5) returns Result = [0, 4)
  static ConstantRange makeAllowedICmpRegion(CmpInst::Predicate Pred,
                                             const ConstantRange &Other);

  /// Produce the largest range such that all values in the returned range
  /// satisfy the given predicate with all values contained within Other.
  /// Formally, this returns a subset of
  /// 'intersection over all y in Other . { x : icmp op x y is true }'.  If the
  /// exact answer is not representable as a ConstantRange, the return value
  /// will be a proper subset of the above.
  ///
  /// Example: Pred = ult and Other = i8 [2, 5) returns [0, 2)
  static ConstantRange makeSatisfyingICmpRegion(CmpInst::Predicate Pred,
                                                const ConstantRange &Other);

  /// Produce the exact range such that all values in the returned range satisfy
  /// the given predicate with any value contained within Other. Formally, this
  /// returns the exact answer when the superset of 'union over all y in Other
  /// is exactly same as the subset of intersection over all y in Other.
  /// { x : icmp op x y is true}'.
  ///
  /// Example: Pred = ult and Other = i8 3 returns [0, 3)
  static ConstantRange makeExactICmpRegion(CmpInst::Predicate Pred,
                                           const APInt &Other);

  /// Return the largest range containing all X such that "X BinOpC Y" is
  /// guaranteed not to wrap (overflow) for all Y in Other.
  ///
  /// NB! The returned set does *not* contain **all** possible values of X for
  /// which "X BinOpC Y" does not wrap -- some viable values of X may be
  /// missing, so you cannot use this to constrain X's range.  E.g. in the
  /// fourth example, "(-2) + 1" is both nsw and nuw (so the "X" could be -2),
  /// but (-2) is not in the set returned.
  ///
  /// Examples:
  ///  typedef OverflowingBinaryOperator OBO;
  ///  #define MGNR makeGuaranteedNoWrapRegion
  ///  MGNR(Add, [i8 1, 2), OBO::NoSignedWrap) == [-128, 127)
  ///  MGNR(Add, [i8 1, 2), OBO::NoUnsignedWrap) == [0, -1)
  ///  MGNR(Add, [i8 0, 1), OBO::NoUnsignedWrap) == Full Set
  ///  MGNR(Add, [i8 1, 2), OBO::NoUnsignedWrap | OBO::NoSignedWrap)
  ///    == [0,INT_MAX)
  ///  MGNR(Add, [i8 -1, 6), OBO::NoSignedWrap) == [INT_MIN+1, INT_MAX-4)
  ///  MGNR(Sub, [i8 1, 2), OBO::NoSignedWrap) == [-127, 128)
  ///  MGNR(Sub, [i8 1, 2), OBO::NoUnsignedWrap) == [1, 0)
  ///  MGNR(Sub, [i8 1, 2), OBO::NoUnsignedWrap | OBO::NoSignedWrap)
  ///    == [1,INT_MAX)
  static ConstantRange makeGuaranteedNoWrapRegion(Instruction::BinaryOps BinOp,
                                                  const ConstantRange &Other,
                                                  unsigned NoWrapKind);

  /// Set up \p Pred and \p RHS such that
  /// ConstantRange::makeExactICmpRegion(Pred, RHS) == *this.  Return true if
  /// successful.
  bool getEquivalentICmp(CmpInst::Predicate &Pred, APInt &RHS) const;

  /// Return the lower value for this range.
  const APInt &getLower() const { return Lower; }

  /// Return the upper value for this range.
  const APInt &getUpper() const { return Upper; }

  /// Get the bit width of this ConstantRange.
  uint32_t getBitWidth() const { return Lower.getBitWidth(); }

  /// Return true if this set contains all of the elements possible
  /// for this data-type.
  bool isFullSet() const;

  /// Return true if this set contains no members.
  bool isEmptySet() const;

  /// Return true if this set wraps around the top of the range.
  /// For example: [100, 8).
  bool isWrappedSet() const;

  /// Return true if this set wraps around the INT_MIN of
  /// its bitwidth. For example: i8 [120, 140).
  bool isSignWrappedSet() const;

  /// Return true if the specified value is in the set.
  bool contains(const APInt &Val) const;

  /// Return true if the other range is a subset of this one.
  bool contains(const ConstantRange &CR) const;

  /// If this set contains a single element, return it, otherwise return null.
  const APInt *getSingleElement() const {
    if (Upper == Lower + 1)
      return &Lower;
    return nullptr;
  }

  /// If this set contains all but a single element, return it, otherwise return
  /// null.
  const APInt *getSingleMissingElement() const {
    if (Lower == Upper + 1)
      return &Upper;
    return nullptr;
  }

  /// Return true if this set contains exactly one member.
  bool isSingleElement() const { return getSingleElement() != nullptr; }

  /// Return the number of elements in this set.
  APInt getSetSize() const;

  /// Compare set size of this range with the range CR.
  bool isSizeStrictlySmallerThan(const ConstantRange &CR) const;

  // Compare set size of this range with Value.
  bool isSizeLargerThan(uint64_t MaxSize) const;

  /// Return the largest unsigned value contained in the ConstantRange.
  APInt getUnsignedMax() const;

  /// Return the smallest unsigned value contained in the ConstantRange.
  APInt getUnsignedMin() const;

  /// Return the largest signed value contained in the ConstantRange.
  APInt getSignedMax() const;

  /// Return the smallest signed value contained in the ConstantRange.
  APInt getSignedMin() const;

  /// Return true if this range is equal to another range.
  bool operator==(const ConstantRange &CR) const {
    return Lower == CR.Lower && Upper == CR.Upper;
  }
  bool operator!=(const ConstantRange &CR) const {
    return !operator==(CR);
  }

  /// Subtract the specified constant from the endpoints of this constant range.
  ConstantRange subtract(const APInt &CI) const;

  /// Subtract the specified range from this range (aka relative complement of
  /// the sets).
  ConstantRange difference(const ConstantRange &CR) const;

  /// Return the range that results from the intersection of
  /// this range with another range.  The resultant range is guaranteed to
  /// include all elements contained in both input ranges, and to have the
  /// smallest possible set size that does so.  Because there may be two
  /// intersections with the same set size, A.intersectWith(B) might not
  /// be equal to B.intersectWith(A).
  ConstantRange intersectWith(const ConstantRange &CR) const;

  /// Return the range that results from the union of this range
  /// with another range.  The resultant range is guaranteed to include the
  /// elements of both sets, but may contain more.  For example, [3, 9) union
  /// [12,15) is [3, 15), which includes 9, 10, and 11, which were not included
  /// in either set before.
  ConstantRange unionWith(const ConstantRange &CR) const;

  /// Return a new range representing the possible values resulting
  /// from an application of the specified cast operator to this range. \p
  /// BitWidth is the target bitwidth of the cast.  For casts which don't
  /// change bitwidth, it must be the same as the source bitwidth.  For casts
  /// which do change bitwidth, the bitwidth must be consistent with the
  /// requested cast and source bitwidth.
  ConstantRange castOp(Instruction::CastOps CastOp,
                       uint32_t BitWidth) const;

  /// Return a new range in the specified integer type, which must
  /// be strictly larger than the current type.  The returned range will
  /// correspond to the possible range of values if the source range had been
  /// zero extended to BitWidth.
  ConstantRange zeroExtend(uint32_t BitWidth) const;

  /// Return a new range in the specified integer type, which must
  /// be strictly larger than the current type.  The returned range will
  /// correspond to the possible range of values if the source range had been
  /// sign extended to BitWidth.
  ConstantRange signExtend(uint32_t BitWidth) const;

  /// Return a new range in the specified integer type, which must be
  /// strictly smaller than the current type.  The returned range will
  /// correspond to the possible range of values if the source range had been
  /// truncated to the specified type.
  ConstantRange truncate(uint32_t BitWidth) const;

  /// Make this range have the bit width given by \p BitWidth. The
  /// value is zero extended, truncated, or left alone to make it that width.
  ConstantRange zextOrTrunc(uint32_t BitWidth) const;

  /// Make this range have the bit width given by \p BitWidth. The
  /// value is sign extended, truncated, or left alone to make it that width.
  ConstantRange sextOrTrunc(uint32_t BitWidth) const;

  /// Return a new range representing the possible values resulting
  /// from an application of the specified binary operator to an left hand side
  /// of this range and a right hand side of \p Other.
  ConstantRange binaryOp(Instruction::BinaryOps BinOp,
                         const ConstantRange &Other) const;

  /// Return a new range representing the possible values resulting
  /// from an addition of a value in this range and a value in \p Other.
  ConstantRange add(const ConstantRange &Other) const;

  /// Return a new range representing the possible values resulting from a
  /// known NSW addition of a value in this range and \p Other constant.
  ConstantRange addWithNoSignedWrap(const APInt &Other) const;

  /// Return a new range representing the possible values resulting
  /// from a subtraction of a value in this range and a value in \p Other.
  ConstantRange sub(const ConstantRange &Other) const;

  /// Return a new range representing the possible values resulting
  /// from a multiplication of a value in this range and a value in \p Other,
  /// treating both this and \p Other as unsigned ranges.
  ConstantRange multiply(const ConstantRange &Other) const;

  /// Return a new range representing the possible values resulting
  /// from a signed maximum of a value in this range and a value in \p Other.
  ConstantRange smax(const ConstantRange &Other) const;

  /// Return a new range representing the possible values resulting
  /// from an unsigned maximum of a value in this range and a value in \p Other.
  ConstantRange umax(const ConstantRange &Other) const;

  /// Return a new range representing the possible values resulting
  /// from a signed minimum of a value in this range and a value in \p Other.
  ConstantRange smin(const ConstantRange &Other) const;

  /// Return a new range representing the possible values resulting
  /// from an unsigned minimum of a value in this range and a value in \p Other.
  ConstantRange umin(const ConstantRange &Other) const;

  /// Return a new range representing the possible values resulting
  /// from an unsigned division of a value in this range and a value in
  /// \p Other.
  ConstantRange udiv(const ConstantRange &Other) const;

  /// Return a new range representing the possible values resulting
  /// from a binary-and of a value in this range by a value in \p Other.
  ConstantRange binaryAnd(const ConstantRange &Other) const;

  /// Return a new range representing the possible values resulting
  /// from a binary-or of a value in this range by a value in \p Other.
  ConstantRange binaryOr(const ConstantRange &Other) const;

  /// Return a new range representing the possible values resulting
  /// from a left shift of a value in this range by a value in \p Other.
  /// TODO: This isn't fully implemented yet.
  ConstantRange shl(const ConstantRange &Other) const;

  /// Return a new range representing the possible values resulting from a
  /// logical right shift of a value in this range and a value in \p Other.
  ConstantRange lshr(const ConstantRange &Other) const;

  /// Return a new range representing the possible values resulting from a
  /// arithmetic right shift of a value in this range and a value in \p Other.
  ConstantRange ashr(const ConstantRange &Other) const;

  /// Return a new range that is the logical not of the current set.
  ConstantRange inverse() const;

  /// Print out the bounds to a stream.
  void print(raw_ostream &OS) const;

  /// Allow printing from a debugger easily.
  void dump() const;
};

inline raw_ostream &operator<<(raw_ostream &OS, const ConstantRange &CR) {
  CR.print(OS);
  return OS;
}

/// Parse out a conservative ConstantRange from !range metadata.
///
/// E.g. if RangeMD is !{i32 0, i32 10, i32 15, i32 20} then return [0, 20).
ConstantRange getConstantRangeFromMetadata(const MDNode &RangeMD);

} // end namespace llvm

#endif // LLVM_IR_CONSTANTRANGE_H
