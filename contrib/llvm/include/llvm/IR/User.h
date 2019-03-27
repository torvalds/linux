//===- llvm/User.h - User class definition ----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This class defines the interface that one who uses a Value must implement.
// Each instance of the Value class keeps track of what User's have handles
// to it.
//
//  * Instructions are the largest class of Users.
//  * Constants may be users of other constants (think arrays and stuff)
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_USER_H
#define LLVM_IR_USER_H

#include "llvm/ADT/iterator.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>

namespace llvm {

template <typename T> class ArrayRef;
template <typename T> class MutableArrayRef;

/// Compile-time customization of User operands.
///
/// Customizes operand-related allocators and accessors.
template <class>
struct OperandTraits;

class User : public Value {
  template <unsigned>
  friend struct HungoffOperandTraits;

  LLVM_ATTRIBUTE_ALWAYS_INLINE inline static void *
  allocateFixedOperandUser(size_t, unsigned, unsigned);

protected:
  /// Allocate a User with an operand pointer co-allocated.
  ///
  /// This is used for subclasses which need to allocate a variable number
  /// of operands, ie, 'hung off uses'.
  void *operator new(size_t Size);

  /// Allocate a User with the operands co-allocated.
  ///
  /// This is used for subclasses which have a fixed number of operands.
  void *operator new(size_t Size, unsigned Us);

  /// Allocate a User with the operands co-allocated.  If DescBytes is non-zero
  /// then allocate an additional DescBytes bytes before the operands. These
  /// bytes can be accessed by calling getDescriptor.
  ///
  /// DescBytes needs to be divisible by sizeof(void *).  The allocated
  /// descriptor, if any, is aligned to sizeof(void *) bytes.
  ///
  /// This is used for subclasses which have a fixed number of operands.
  void *operator new(size_t Size, unsigned Us, unsigned DescBytes);

  User(Type *ty, unsigned vty, Use *, unsigned NumOps)
      : Value(ty, vty) {
    assert(NumOps < (1u << NumUserOperandsBits) && "Too many operands");
    NumUserOperands = NumOps;
    // If we have hung off uses, then the operand list should initially be
    // null.
    assert((!HasHungOffUses || !getOperandList()) &&
           "Error in initializing hung off uses for User");
  }

  /// Allocate the array of Uses, followed by a pointer
  /// (with bottom bit set) to the User.
  /// \param IsPhi identifies callers which are phi nodes and which need
  /// N BasicBlock* allocated along with N
  void allocHungoffUses(unsigned N, bool IsPhi = false);

  /// Grow the number of hung off uses.  Note that allocHungoffUses
  /// should be called if there are no uses.
  void growHungoffUses(unsigned N, bool IsPhi = false);

protected:
  ~User() = default; // Use deleteValue() to delete a generic Instruction.

public:
  User(const User &) = delete;

  /// Free memory allocated for User and Use objects.
  void operator delete(void *Usr);
  /// Placement delete - required by std, called if the ctor throws.
  void operator delete(void *Usr, unsigned) {
    // Note: If a subclass manipulates the information which is required to calculate the
    // Usr memory pointer, e.g. NumUserOperands, the operator delete of that subclass has
    // to restore the changed information to the original value, since the dtor of that class
    // is not called if the ctor fails.
    User::operator delete(Usr);

#ifndef LLVM_ENABLE_EXCEPTIONS
    llvm_unreachable("Constructor throws?");
#endif
  }
  /// Placement delete - required by std, called if the ctor throws.
  void operator delete(void *Usr, unsigned, bool) {
    // Note: If a subclass manipulates the information which is required to calculate the
    // Usr memory pointer, e.g. NumUserOperands, the operator delete of that subclass has
    // to restore the changed information to the original value, since the dtor of that class
    // is not called if the ctor fails.
    User::operator delete(Usr);

#ifndef LLVM_ENABLE_EXCEPTIONS
    llvm_unreachable("Constructor throws?");
#endif
  }

protected:
  template <int Idx, typename U> static Use &OpFrom(const U *that) {
    return Idx < 0
      ? OperandTraits<U>::op_end(const_cast<U*>(that))[Idx]
      : OperandTraits<U>::op_begin(const_cast<U*>(that))[Idx];
  }

  template <int Idx> Use &Op() {
    return OpFrom<Idx>(this);
  }
  template <int Idx> const Use &Op() const {
    return OpFrom<Idx>(this);
  }

private:
  const Use *getHungOffOperands() const {
    return *(reinterpret_cast<const Use *const *>(this) - 1);
  }

  Use *&getHungOffOperands() { return *(reinterpret_cast<Use **>(this) - 1); }

  const Use *getIntrusiveOperands() const {
    return reinterpret_cast<const Use *>(this) - NumUserOperands;
  }

  Use *getIntrusiveOperands() {
    return reinterpret_cast<Use *>(this) - NumUserOperands;
  }

  void setOperandList(Use *NewList) {
    assert(HasHungOffUses &&
           "Setting operand list only required for hung off uses");
    getHungOffOperands() = NewList;
  }

public:
  const Use *getOperandList() const {
    return HasHungOffUses ? getHungOffOperands() : getIntrusiveOperands();
  }
  Use *getOperandList() {
    return const_cast<Use *>(static_cast<const User *>(this)->getOperandList());
  }

  Value *getOperand(unsigned i) const {
    assert(i < NumUserOperands && "getOperand() out of range!");
    return getOperandList()[i];
  }

  void setOperand(unsigned i, Value *Val) {
    assert(i < NumUserOperands && "setOperand() out of range!");
    assert((!isa<Constant>((const Value*)this) ||
            isa<GlobalValue>((const Value*)this)) &&
           "Cannot mutate a constant with setOperand!");
    getOperandList()[i] = Val;
  }

  const Use &getOperandUse(unsigned i) const {
    assert(i < NumUserOperands && "getOperandUse() out of range!");
    return getOperandList()[i];
  }
  Use &getOperandUse(unsigned i) {
    assert(i < NumUserOperands && "getOperandUse() out of range!");
    return getOperandList()[i];
  }

  unsigned getNumOperands() const { return NumUserOperands; }

  /// Returns the descriptor co-allocated with this User instance.
  ArrayRef<const uint8_t> getDescriptor() const;

  /// Returns the descriptor co-allocated with this User instance.
  MutableArrayRef<uint8_t> getDescriptor();

  /// Set the number of operands on a GlobalVariable.
  ///
  /// GlobalVariable always allocates space for a single operands, but
  /// doesn't always use it.
  ///
  /// FIXME: As that the number of operands is used to find the start of
  /// the allocated memory in operator delete, we need to always think we have
  /// 1 operand before delete.
  void setGlobalVariableNumOperands(unsigned NumOps) {
    assert(NumOps <= 1 && "GlobalVariable can only have 0 or 1 operands");
    NumUserOperands = NumOps;
  }

  /// Subclasses with hung off uses need to manage the operand count
  /// themselves.  In these instances, the operand count isn't used to find the
  /// OperandList, so there's no issue in having the operand count change.
  void setNumHungOffUseOperands(unsigned NumOps) {
    assert(HasHungOffUses && "Must have hung off uses to use this method");
    assert(NumOps < (1u << NumUserOperandsBits) && "Too many operands");
    NumUserOperands = NumOps;
  }

  // ---------------------------------------------------------------------------
  // Operand Iterator interface...
  //
  using op_iterator = Use*;
  using const_op_iterator = const Use*;
  using op_range = iterator_range<op_iterator>;
  using const_op_range = iterator_range<const_op_iterator>;

  op_iterator       op_begin()       { return getOperandList(); }
  const_op_iterator op_begin() const { return getOperandList(); }
  op_iterator       op_end()         {
    return getOperandList() + NumUserOperands;
  }
  const_op_iterator op_end()   const {
    return getOperandList() + NumUserOperands;
  }
  op_range operands() {
    return op_range(op_begin(), op_end());
  }
  const_op_range operands() const {
    return const_op_range(op_begin(), op_end());
  }

  /// Iterator for directly iterating over the operand Values.
  struct value_op_iterator
      : iterator_adaptor_base<value_op_iterator, op_iterator,
                              std::random_access_iterator_tag, Value *,
                              ptrdiff_t, Value *, Value *> {
    explicit value_op_iterator(Use *U = nullptr) : iterator_adaptor_base(U) {}

    Value *operator*() const { return *I; }
    Value *operator->() const { return operator*(); }
  };

  value_op_iterator value_op_begin() {
    return value_op_iterator(op_begin());
  }
  value_op_iterator value_op_end() {
    return value_op_iterator(op_end());
  }
  iterator_range<value_op_iterator> operand_values() {
    return make_range(value_op_begin(), value_op_end());
  }

  struct const_value_op_iterator
      : iterator_adaptor_base<const_value_op_iterator, const_op_iterator,
                              std::random_access_iterator_tag, const Value *,
                              ptrdiff_t, const Value *, const Value *> {
    explicit const_value_op_iterator(const Use *U = nullptr) :
      iterator_adaptor_base(U) {}

    const Value *operator*() const { return *I; }
    const Value *operator->() const { return operator*(); }
  };

  const_value_op_iterator value_op_begin() const {
    return const_value_op_iterator(op_begin());
  }
  const_value_op_iterator value_op_end() const {
    return const_value_op_iterator(op_end());
  }
  iterator_range<const_value_op_iterator> operand_values() const {
    return make_range(value_op_begin(), value_op_end());
  }

  /// Drop all references to operands.
  ///
  /// This function is in charge of "letting go" of all objects that this User
  /// refers to.  This allows one to 'delete' a whole class at a time, even
  /// though there may be circular references...  First all references are
  /// dropped, and all use counts go to zero.  Then everything is deleted for
  /// real.  Note that no operations are valid on an object that has "dropped
  /// all references", except operator delete.
  void dropAllReferences() {
    for (Use &U : operands())
      U.set(nullptr);
  }

  /// Replace uses of one Value with another.
  ///
  /// Replaces all references to the "From" definition with references to the
  /// "To" definition.
  void replaceUsesOfWith(Value *From, Value *To);

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Value *V) {
    return isa<Instruction>(V) || isa<Constant>(V);
  }
};

// Either Use objects, or a Use pointer can be prepended to User.
static_assert(alignof(Use) >= alignof(User),
              "Alignment is insufficient after objects prepended to User");
static_assert(alignof(Use *) >= alignof(User),
              "Alignment is insufficient after objects prepended to User");

template<> struct simplify_type<User::op_iterator> {
  using SimpleType = Value*;

  static SimpleType getSimplifiedValue(User::op_iterator &Val) {
    return Val->get();
  }
};
template<> struct simplify_type<User::const_op_iterator> {
  using SimpleType = /*const*/ Value*;

  static SimpleType getSimplifiedValue(User::const_op_iterator &Val) {
    return Val->get();
  }
};

} // end namespace llvm

#endif // LLVM_IR_USER_H
