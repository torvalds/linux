//===- llvm/Use.h - Definition of the Use class -----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This defines the Use class.  The Use class represents the operand of an
/// instruction or some other User instance which refers to a Value.  The Use
/// class keeps the "use list" of the referenced value up to date.
///
/// Pointer tagging is used to efficiently find the User corresponding to a Use
/// without having to store a User pointer in every Use. A User is preceded in
/// memory by all the Uses corresponding to its operands, and the low bits of
/// one of the fields (Prev) of the Use class are used to encode offsets to be
/// able to find that User given a pointer to any Use. For details, see:
///
///   http://www.llvm.org/docs/ProgrammersManual.html#UserLayout
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_USE_H
#define LLVM_IR_USE_H

#include "llvm-c/Types.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/Support/CBindingWrapping.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

template <typename> struct simplify_type;
class User;
class Value;

/// A Use represents the edge between a Value definition and its users.
///
/// This is notionally a two-dimensional linked list. It supports traversing
/// all of the uses for a particular value definition. It also supports jumping
/// directly to the used value when we arrive from the User's operands, and
/// jumping directly to the User when we arrive from the Value's uses.
///
/// The pointer to the used Value is explicit, and the pointer to the User is
/// implicit. The implicit pointer is found via a waymarking algorithm
/// described in the programmer's manual:
///
///   http://www.llvm.org/docs/ProgrammersManual.html#the-waymarking-algorithm
///
/// This is essentially the single most memory intensive object in LLVM because
/// of the number of uses in the system. At the same time, the constant time
/// operations it allows are essential to many optimizations having reasonable
/// time complexity.
class Use {
public:
  Use(const Use &U) = delete;

  /// Provide a fast substitute to std::swap<Use>
  /// that also works with less standard-compliant compilers
  void swap(Use &RHS);

  /// Pointer traits for the UserRef PointerIntPair. This ensures we always
  /// use the LSB regardless of pointer alignment on different targets.
  struct UserRefPointerTraits {
    static inline void *getAsVoidPointer(User *P) { return P; }

    static inline User *getFromVoidPointer(void *P) {
      return (User *)P;
    }

    enum { NumLowBitsAvailable = 1 };
  };

  // A type for the word following an array of hung-off Uses in memory, which is
  // a pointer back to their User with the bottom bit set.
  using UserRef = PointerIntPair<User *, 1, unsigned, UserRefPointerTraits>;

  /// Pointer traits for the Prev PointerIntPair. This ensures we always use
  /// the two LSBs regardless of pointer alignment on different targets.
  struct PrevPointerTraits {
    static inline void *getAsVoidPointer(Use **P) { return P; }

    static inline Use **getFromVoidPointer(void *P) {
      return (Use **)P;
    }

    enum { NumLowBitsAvailable = 2 };
  };

private:
  /// Destructor - Only for zap()
  ~Use() {
    if (Val)
      removeFromList();
  }

  enum PrevPtrTag { zeroDigitTag, oneDigitTag, stopTag, fullStopTag };

  /// Constructor
  Use(PrevPtrTag tag) { Prev.setInt(tag); }

public:
  friend class Value;

  operator Value *() const { return Val; }
  Value *get() const { return Val; }

  /// Returns the User that contains this Use.
  ///
  /// For an instruction operand, for example, this will return the
  /// instruction.
  User *getUser() const LLVM_READONLY;

  inline void set(Value *Val);

  inline Value *operator=(Value *RHS);
  inline const Use &operator=(const Use &RHS);

  Value *operator->() { return Val; }
  const Value *operator->() const { return Val; }

  Use *getNext() const { return Next; }

  /// Return the operand # of this use in its User.
  unsigned getOperandNo() const;

  /// Initializes the waymarking tags on an array of Uses.
  ///
  /// This sets up the array of Uses such that getUser() can find the User from
  /// any of those Uses.
  static Use *initTags(Use *Start, Use *Stop);

  /// Destroys Use operands when the number of operands of
  /// a User changes.
  static void zap(Use *Start, const Use *Stop, bool del = false);

private:
  const Use *getImpliedUser() const LLVM_READONLY;

  Value *Val = nullptr;
  Use *Next;
  PointerIntPair<Use **, 2, PrevPtrTag, PrevPointerTraits> Prev;

  void setPrev(Use **NewPrev) { Prev.setPointer(NewPrev); }

  void addToList(Use **List) {
    Next = *List;
    if (Next)
      Next->setPrev(&Next);
    setPrev(List);
    *List = this;
  }

  void removeFromList() {
    Use **StrippedPrev = Prev.getPointer();
    *StrippedPrev = Next;
    if (Next)
      Next->setPrev(StrippedPrev);
  }
};

/// Allow clients to treat uses just like values when using
/// casting operators.
template <> struct simplify_type<Use> {
  using SimpleType = Value *;

  static SimpleType getSimplifiedValue(Use &Val) { return Val.get(); }
};
template <> struct simplify_type<const Use> {
  using SimpleType = /*const*/ Value *;

  static SimpleType getSimplifiedValue(const Use &Val) { return Val.get(); }
};

// Create wrappers for C Binding types (see CBindingWrapping.h).
DEFINE_SIMPLE_CONVERSION_FUNCTIONS(Use, LLVMUseRef)

} // end namespace llvm

#endif // LLVM_IR_USE_H
