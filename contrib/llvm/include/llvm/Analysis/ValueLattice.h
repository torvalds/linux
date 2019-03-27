//===- ValueLattice.h - Value constraint analysis ---------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_VALUELATTICE_H
#define LLVM_ANALYSIS_VALUELATTICE_H

#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/Constants.h"
//
//===----------------------------------------------------------------------===//
//                               ValueLatticeElement
//===----------------------------------------------------------------------===//

/// This class represents lattice values for constants.
///
/// FIXME: This is basically just for bringup, this can be made a lot more rich
/// in the future.
///

namespace llvm {
class ValueLatticeElement {
  enum ValueLatticeElementTy {
    /// This Value has no known value yet.  As a result, this implies the
    /// producing instruction is dead.  Caution: We use this as the starting
    /// state in our local meet rules.  In this usage, it's taken to mean
    /// "nothing known yet".
    undefined,

    /// This Value has a specific constant value.  (For constant integers,
    /// constantrange is used instead.  Integer typed constantexprs can appear
    /// as constant.)
    constant,

    /// This Value is known to not have the specified value.  (For constant
    /// integers, constantrange is used instead.  As above, integer typed
    /// constantexprs can appear here.)
    notconstant,

    /// The Value falls within this range. (Used only for integer typed values.)
    constantrange,

    /// We can not precisely model the dynamic values this value might take.
    overdefined
  };

  ValueLatticeElementTy Tag;

  /// The union either stores a pointer to a constant or a constant range,
  /// associated to the lattice element. We have to ensure that Range is
  /// initialized or destroyed when changing state to or from constantrange.
  union {
    Constant *ConstVal;
    ConstantRange Range;
  };

public:
  // Const and Range are initialized on-demand.
  ValueLatticeElement() : Tag(undefined) {}

  /// Custom destructor to ensure Range is properly destroyed, when the object
  /// is deallocated.
  ~ValueLatticeElement() {
    switch (Tag) {
    case overdefined:
    case undefined:
    case constant:
    case notconstant:
      break;
    case constantrange:
      Range.~ConstantRange();
      break;
    };
  }

  /// Custom copy constructor, to ensure Range gets initialized when
  /// copying a constant range lattice element.
  ValueLatticeElement(const ValueLatticeElement &Other) : Tag(undefined) {
    *this = Other;
  }

  /// Custom assignment operator, to ensure Range gets initialized when
  /// assigning a constant range lattice element.
  ValueLatticeElement &operator=(const ValueLatticeElement &Other) {
    // If we change the state of this from constant range to non constant range,
    // destroy Range.
    if (isConstantRange() && !Other.isConstantRange())
      Range.~ConstantRange();

    // If we change the state of this from a valid ConstVal to another a state
    // without a valid ConstVal, zero the pointer.
    if ((isConstant() || isNotConstant()) && !Other.isConstant() &&
        !Other.isNotConstant())
      ConstVal = nullptr;

    switch (Other.Tag) {
    case constantrange:
      if (!isConstantRange())
        new (&Range) ConstantRange(Other.Range);
      else
        Range = Other.Range;
      break;
    case constant:
    case notconstant:
      ConstVal = Other.ConstVal;
      break;
    case overdefined:
    case undefined:
      break;
    }
    Tag = Other.Tag;
    return *this;
  }

  static ValueLatticeElement get(Constant *C) {
    ValueLatticeElement Res;
    if (!isa<UndefValue>(C))
      Res.markConstant(C);
    return Res;
  }
  static ValueLatticeElement getNot(Constant *C) {
    ValueLatticeElement Res;
    if (!isa<UndefValue>(C))
      Res.markNotConstant(C);
    return Res;
  }
  static ValueLatticeElement getRange(ConstantRange CR) {
    ValueLatticeElement Res;
    Res.markConstantRange(std::move(CR));
    return Res;
  }
  static ValueLatticeElement getOverdefined() {
    ValueLatticeElement Res;
    Res.markOverdefined();
    return Res;
  }

  bool isUndefined() const { return Tag == undefined; }
  bool isConstant() const { return Tag == constant; }
  bool isNotConstant() const { return Tag == notconstant; }
  bool isConstantRange() const { return Tag == constantrange; }
  bool isOverdefined() const { return Tag == overdefined; }

  Constant *getConstant() const {
    assert(isConstant() && "Cannot get the constant of a non-constant!");
    return ConstVal;
  }

  Constant *getNotConstant() const {
    assert(isNotConstant() && "Cannot get the constant of a non-notconstant!");
    return ConstVal;
  }

  const ConstantRange &getConstantRange() const {
    assert(isConstantRange() &&
           "Cannot get the constant-range of a non-constant-range!");
    return Range;
  }

  Optional<APInt> asConstantInteger() const {
    if (isConstant() && isa<ConstantInt>(getConstant())) {
      return cast<ConstantInt>(getConstant())->getValue();
    } else if (isConstantRange() && getConstantRange().isSingleElement()) {
      return *getConstantRange().getSingleElement();
    }
    return None;
  }

private:
  void markOverdefined() {
    if (isOverdefined())
      return;
    if (isConstant() || isNotConstant())
      ConstVal = nullptr;
    if (isConstantRange())
      Range.~ConstantRange();
    Tag = overdefined;
  }

  void markConstant(Constant *V) {
    assert(V && "Marking constant with NULL");
    if (ConstantInt *CI = dyn_cast<ConstantInt>(V)) {
      markConstantRange(ConstantRange(CI->getValue()));
      return;
    }
    if (isa<UndefValue>(V))
      return;

    assert((!isConstant() || getConstant() == V) &&
           "Marking constant with different value");
    assert(isUndefined());
    Tag = constant;
    ConstVal = V;
  }

  void markNotConstant(Constant *V) {
    assert(V && "Marking constant with NULL");
    if (ConstantInt *CI = dyn_cast<ConstantInt>(V)) {
      markConstantRange(ConstantRange(CI->getValue() + 1, CI->getValue()));
      return;
    }
    if (isa<UndefValue>(V))
      return;

    assert((!isConstant() || getConstant() != V) &&
           "Marking constant !constant with same value");
    assert((!isNotConstant() || getNotConstant() == V) &&
           "Marking !constant with different value");
    assert(isUndefined() || isConstant());
    Tag = notconstant;
    ConstVal = V;
  }

  void markConstantRange(ConstantRange NewR) {
    if (isConstantRange()) {
      if (NewR.isEmptySet())
        markOverdefined();
      else {
        Range = std::move(NewR);
      }
      return;
    }

    assert(isUndefined());
    if (NewR.isEmptySet())
      markOverdefined();
    else {
      Tag = constantrange;
      new (&Range) ConstantRange(std::move(NewR));
    }
  }

public:
  /// Updates this object to approximate both this object and RHS. Returns
  /// true if this object has been changed.
  bool mergeIn(const ValueLatticeElement &RHS, const DataLayout &DL) {
    if (RHS.isUndefined() || isOverdefined())
      return false;
    if (RHS.isOverdefined()) {
      markOverdefined();
      return true;
    }

    if (isUndefined()) {
      *this = RHS;
      return !RHS.isUndefined();
    }

    if (isConstant()) {
      if (RHS.isConstant() && getConstant() == RHS.getConstant())
        return false;
      markOverdefined();
      return true;
    }

    if (isNotConstant()) {
      if (RHS.isNotConstant() && getNotConstant() == RHS.getNotConstant())
        return false;
      markOverdefined();
      return true;
    }

    assert(isConstantRange() && "New ValueLattice type?");
    if (!RHS.isConstantRange()) {
      // We can get here if we've encountered a constantexpr of integer type
      // and merge it with a constantrange.
      markOverdefined();
      return true;
    }
    ConstantRange NewR = getConstantRange().unionWith(RHS.getConstantRange());
    if (NewR.isFullSet())
      markOverdefined();
    else if (NewR == getConstantRange())
      return false;
    else
      markConstantRange(std::move(NewR));
    return true;
  }

  ConstantInt *getConstantInt() const {
    assert(isConstant() && isa<ConstantInt>(getConstant()) &&
           "No integer constant");
    return cast<ConstantInt>(getConstant());
  }

  /// Compares this symbolic value with Other using Pred and returns either
  /// true, false or undef constants, or nullptr if the comparison cannot be
  /// evaluated.
  Constant *getCompare(CmpInst::Predicate Pred, Type *Ty,
                       const ValueLatticeElement &Other) const {
    if (isUndefined() || Other.isUndefined())
      return UndefValue::get(Ty);

    if (isConstant() && Other.isConstant())
      return ConstantExpr::getCompare(Pred, getConstant(), Other.getConstant());

    // Integer constants are represented as ConstantRanges with single
    // elements.
    if (!isConstantRange() || !Other.isConstantRange())
      return nullptr;

    const auto &CR = getConstantRange();
    const auto &OtherCR = Other.getConstantRange();
    if (ConstantRange::makeSatisfyingICmpRegion(Pred, OtherCR).contains(CR))
      return ConstantInt::getTrue(Ty);
    if (ConstantRange::makeSatisfyingICmpRegion(
            CmpInst::getInversePredicate(Pred), OtherCR)
            .contains(CR))
      return ConstantInt::getFalse(Ty);

    return nullptr;
  }
};

raw_ostream &operator<<(raw_ostream &OS, const ValueLatticeElement &Val);

} // end namespace llvm
#endif
