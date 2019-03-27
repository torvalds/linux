//===-- llvm/Argument.h - Definition of the Argument class ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares the Argument class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_ARGUMENT_H
#define LLVM_IR_ARGUMENT_H

#include "llvm/ADT/Twine.h"
#include "llvm/ADT/ilist_node.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Value.h"

namespace llvm {

/// This class represents an incoming formal argument to a Function. A formal
/// argument, since it is ``formal'', does not contain an actual value but
/// instead represents the type, argument number, and attributes of an argument
/// for a specific function. When used in the body of said function, the
/// argument of course represents the value of the actual argument that the
/// function was called with.
class Argument final : public Value {
  Function *Parent;
  unsigned ArgNo;

  friend class Function;
  void setParent(Function *parent);

public:
  /// Argument constructor.
  explicit Argument(Type *Ty, const Twine &Name = "", Function *F = nullptr,
                    unsigned ArgNo = 0);

  inline const Function *getParent() const { return Parent; }
  inline       Function *getParent()       { return Parent; }

  /// Return the index of this formal argument in its containing function.
  ///
  /// For example in "void foo(int a, float b)" a is 0 and b is 1.
  unsigned getArgNo() const {
    assert(Parent && "can't get number of unparented arg");
    return ArgNo;
  }

  /// Return true if this argument has the nonnull attribute. Also returns true
  /// if at least one byte is known to be dereferenceable and the pointer is in
  /// addrspace(0).
  bool hasNonNullAttr() const;

  /// If this argument has the dereferenceable attribute, return the number of
  /// bytes known to be dereferenceable. Otherwise, zero is returned.
  uint64_t getDereferenceableBytes() const;

  /// If this argument has the dereferenceable_or_null attribute, return the
  /// number of bytes known to be dereferenceable. Otherwise, zero is returned.
  uint64_t getDereferenceableOrNullBytes() const;

  /// Return true if this argument has the byval attribute.
  bool hasByValAttr() const;

  /// Return true if this argument has the swiftself attribute.
  bool hasSwiftSelfAttr() const;

  /// Return true if this argument has the swifterror attribute.
  bool hasSwiftErrorAttr() const;

  /// Return true if this argument has the byval attribute or inalloca
  /// attribute. These attributes represent arguments being passed by value.
  bool hasByValOrInAllocaAttr() const;

  /// If this is a byval or inalloca argument, return its alignment.
  unsigned getParamAlignment() const;

  /// Return true if this argument has the nest attribute.
  bool hasNestAttr() const;

  /// Return true if this argument has the noalias attribute.
  bool hasNoAliasAttr() const;

  /// Return true if this argument has the nocapture attribute.
  bool hasNoCaptureAttr() const;

  /// Return true if this argument has the sret attribute.
  bool hasStructRetAttr() const;

  /// Return true if this argument has the returned attribute.
  bool hasReturnedAttr() const;

  /// Return true if this argument has the readonly or readnone attribute.
  bool onlyReadsMemory() const;

  /// Return true if this argument has the inalloca attribute.
  bool hasInAllocaAttr() const;

  /// Return true if this argument has the zext attribute.
  bool hasZExtAttr() const;

  /// Return true if this argument has the sext attribute.
  bool hasSExtAttr() const;

  /// Add attributes to an argument.
  void addAttrs(AttrBuilder &B);

  void addAttr(Attribute::AttrKind Kind);

  void addAttr(Attribute Attr);

  /// Remove attributes from an argument.
  void removeAttr(Attribute::AttrKind Kind);

  /// Check if an argument has a given attribute.
  bool hasAttribute(Attribute::AttrKind Kind) const;

  /// Method for support type inquiry through isa, cast, and dyn_cast.
  static bool classof(const Value *V) {
    return V->getValueID() == ArgumentVal;
  }
};

} // End llvm namespace

#endif
