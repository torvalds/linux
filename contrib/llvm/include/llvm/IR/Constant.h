//===-- llvm/Constant.h - Constant class definition -------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the Constant class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_CONSTANT_H
#define LLVM_IR_CONSTANT_H

#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"

namespace llvm {

class APInt;

/// This is an important base class in LLVM. It provides the common facilities
/// of all constant values in an LLVM program. A constant is a value that is
/// immutable at runtime. Functions are constants because their address is
/// immutable. Same with global variables.
///
/// All constants share the capabilities provided in this class. All constants
/// can have a null value. They can have an operand list. Constants can be
/// simple (integer and floating point values), complex (arrays and structures),
/// or expression based (computations yielding a constant value composed of
/// only certain operators and other constant values).
///
/// Note that Constants are immutable (once created they never change)
/// and are fully shared by structural equivalence.  This means that two
/// structurally equivalent constants will always have the same address.
/// Constants are created on demand as needed and never deleted: thus clients
/// don't have to worry about the lifetime of the objects.
/// LLVM Constant Representation
class Constant : public User {
protected:
  Constant(Type *ty, ValueTy vty, Use *Ops, unsigned NumOps)
    : User(ty, vty, Ops, NumOps) {}

public:
  void operator=(const Constant &) = delete;
  Constant(const Constant &) = delete;

  /// Return true if this is the value that would be returned by getNullValue.
  bool isNullValue() const;

  /// Returns true if the value is one.
  bool isOneValue() const;

  /// Return true if this is the value that would be returned by
  /// getAllOnesValue.
  bool isAllOnesValue() const;

  /// Return true if the value is what would be returned by
  /// getZeroValueForNegation.
  bool isNegativeZeroValue() const;

  /// Return true if the value is negative zero or null value.
  bool isZeroValue() const;

  /// Return true if the value is not the smallest signed value.
  bool isNotMinSignedValue() const;

  /// Return true if the value is the smallest signed value.
  bool isMinSignedValue() const;

  /// Return true if this is a finite and non-zero floating-point scalar
  /// constant or a vector constant with all finite and non-zero elements.
  bool isFiniteNonZeroFP() const;

  /// Return true if this is a normal (as opposed to denormal) floating-point
  /// scalar constant or a vector constant with all normal elements.
  bool isNormalFP() const;

  /// Return true if this scalar has an exact multiplicative inverse or this
  /// vector has an exact multiplicative inverse for each element in the vector.
  bool hasExactInverseFP() const;

  /// Return true if this is a floating-point NaN constant or a vector
  /// floating-point constant with all NaN elements.
  bool isNaN() const;

  /// Return true if this is a vector constant that includes any undefined
  /// elements.
  bool containsUndefElement() const;

  /// Return true if evaluation of this constant could trap. This is true for
  /// things like constant expressions that could divide by zero.
  bool canTrap() const;

  /// Return true if the value can vary between threads.
  bool isThreadDependent() const;

  /// Return true if the value is dependent on a dllimport variable.
  bool isDLLImportDependent() const;

  /// Return true if the constant has users other than constant expressions and
  /// other dangling things.
  bool isConstantUsed() const;

  /// This method classifies the entry according to whether or not it may
  /// generate a relocation entry.  This must be conservative, so if it might
  /// codegen to a relocatable entry, it should say so.
  ///
  /// FIXME: This really should not be in IR.
  bool needsRelocation() const;

  /// For aggregates (struct/array/vector) return the constant that corresponds
  /// to the specified element if possible, or null if not. This can return null
  /// if the element index is a ConstantExpr, if 'this' is a constant expr or
  /// if the constant does not fit into an uint64_t.
  Constant *getAggregateElement(unsigned Elt) const;
  Constant *getAggregateElement(Constant *Elt) const;

  /// If this is a splat vector constant, meaning that all of the elements have
  /// the same value, return that value. Otherwise return 0.
  Constant *getSplatValue() const;

  /// If C is a constant integer then return its value, otherwise C must be a
  /// vector of constant integers, all equal, and the common value is returned.
  const APInt &getUniqueInteger() const;

  /// Called if some element of this constant is no longer valid.
  /// At this point only other constants may be on the use_list for this
  /// constant.  Any constants on our Use list must also be destroy'd.  The
  /// implementation must be sure to remove the constant from the list of
  /// available cached constants.  Implementations should implement
  /// destroyConstantImpl to remove constants from any pools/maps they are
  /// contained it.
  void destroyConstant();

  //// Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Value *V) {
    static_assert(ConstantFirstVal == 0, "V->getValueID() >= ConstantFirstVal always succeeds");
    return V->getValueID() <= ConstantLastVal;
  }

  /// This method is a special form of User::replaceUsesOfWith
  /// (which does not work on constants) that does work
  /// on constants.  Basically this method goes through the trouble of building
  /// a new constant that is equivalent to the current one, with all uses of
  /// From replaced with uses of To.  After this construction is completed, all
  /// of the users of 'this' are replaced to use the new constant, and then
  /// 'this' is deleted.  In general, you should not call this method, instead,
  /// use Value::replaceAllUsesWith, which automatically dispatches to this
  /// method as needed.
  ///
  void handleOperandChange(Value *, Value *);

  static Constant *getNullValue(Type* Ty);

  /// @returns the value for an integer or vector of integer constant of the
  /// given type that has all its bits set to true.
  /// Get the all ones value
  static Constant *getAllOnesValue(Type* Ty);

  /// Return the value for an integer or pointer constant, or a vector thereof,
  /// with the given scalar value.
  static Constant *getIntegerValue(Type *Ty, const APInt &V);

  /// If there are any dead constant users dangling off of this constant, remove
  /// them. This method is useful for clients that want to check to see if a
  /// global is unused, but don't want to deal with potentially dead constants
  /// hanging off of the globals.
  void removeDeadConstantUsers() const;

  const Constant *stripPointerCasts() const {
    return cast<Constant>(Value::stripPointerCasts());
  }

  Constant *stripPointerCasts() {
    return const_cast<Constant*>(
                      static_cast<const Constant *>(this)->stripPointerCasts());
  }
};

} // end namespace llvm

#endif // LLVM_IR_CONSTANT_H
