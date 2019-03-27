//===- llvm/Attributes.h - Container for Attributes -------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file contains the simple types necessary to represent the
/// attributes associated with functions and their calls.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_ATTRIBUTES_H
#define LLVM_IR_ATTRIBUTES_H

#include "llvm-c/Types.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/PointerLikeTypeTraits.h"
#include <bitset>
#include <cassert>
#include <cstdint>
#include <map>
#include <string>
#include <utility>

namespace llvm {

class AttrBuilder;
class AttributeImpl;
class AttributeListImpl;
class AttributeSetNode;
template<typename T> struct DenseMapInfo;
class Function;
class LLVMContext;
class Type;

//===----------------------------------------------------------------------===//
/// \class
/// Functions, function parameters, and return types can have attributes
/// to indicate how they should be treated by optimizations and code
/// generation. This class represents one of those attributes. It's light-weight
/// and should be passed around by-value.
class Attribute {
public:
  /// This enumeration lists the attributes that can be associated with
  /// parameters, function results, or the function itself.
  ///
  /// Note: The `uwtable' attribute is about the ABI or the user mandating an
  /// entry in the unwind table. The `nounwind' attribute is about an exception
  /// passing by the function.
  ///
  /// In a theoretical system that uses tables for profiling and SjLj for
  /// exceptions, they would be fully independent. In a normal system that uses
  /// tables for both, the semantics are:
  ///
  /// nil                = Needs an entry because an exception might pass by.
  /// nounwind           = No need for an entry
  /// uwtable            = Needs an entry because the ABI says so and because
  ///                      an exception might pass by.
  /// uwtable + nounwind = Needs an entry because the ABI says so.

  enum AttrKind {
    // IR-Level Attributes
    None,                  ///< No attributes have been set
    #define GET_ATTR_ENUM
    #include "llvm/IR/Attributes.inc"
    EndAttrKinds           ///< Sentinal value useful for loops
  };

private:
  AttributeImpl *pImpl = nullptr;

  Attribute(AttributeImpl *A) : pImpl(A) {}

public:
  Attribute() = default;

  //===--------------------------------------------------------------------===//
  // Attribute Construction
  //===--------------------------------------------------------------------===//

  /// Return a uniquified Attribute object.
  static Attribute get(LLVMContext &Context, AttrKind Kind, uint64_t Val = 0);
  static Attribute get(LLVMContext &Context, StringRef Kind,
                       StringRef Val = StringRef());

  /// Return a uniquified Attribute object that has the specific
  /// alignment set.
  static Attribute getWithAlignment(LLVMContext &Context, uint64_t Align);
  static Attribute getWithStackAlignment(LLVMContext &Context, uint64_t Align);
  static Attribute getWithDereferenceableBytes(LLVMContext &Context,
                                              uint64_t Bytes);
  static Attribute getWithDereferenceableOrNullBytes(LLVMContext &Context,
                                                     uint64_t Bytes);
  static Attribute getWithAllocSizeArgs(LLVMContext &Context,
                                        unsigned ElemSizeArg,
                                        const Optional<unsigned> &NumElemsArg);

  //===--------------------------------------------------------------------===//
  // Attribute Accessors
  //===--------------------------------------------------------------------===//

  /// Return true if the attribute is an Attribute::AttrKind type.
  bool isEnumAttribute() const;

  /// Return true if the attribute is an integer attribute.
  bool isIntAttribute() const;

  /// Return true if the attribute is a string (target-dependent)
  /// attribute.
  bool isStringAttribute() const;

  /// Return true if the attribute is present.
  bool hasAttribute(AttrKind Val) const;

  /// Return true if the target-dependent attribute is present.
  bool hasAttribute(StringRef Val) const;

  /// Return the attribute's kind as an enum (Attribute::AttrKind). This
  /// requires the attribute to be an enum or integer attribute.
  Attribute::AttrKind getKindAsEnum() const;

  /// Return the attribute's value as an integer. This requires that the
  /// attribute be an integer attribute.
  uint64_t getValueAsInt() const;

  /// Return the attribute's kind as a string. This requires the
  /// attribute to be a string attribute.
  StringRef getKindAsString() const;

  /// Return the attribute's value as a string. This requires the
  /// attribute to be a string attribute.
  StringRef getValueAsString() const;

  /// Returns the alignment field of an attribute as a byte alignment
  /// value.
  unsigned getAlignment() const;

  /// Returns the stack alignment field of an attribute as a byte
  /// alignment value.
  unsigned getStackAlignment() const;

  /// Returns the number of dereferenceable bytes from the
  /// dereferenceable attribute.
  uint64_t getDereferenceableBytes() const;

  /// Returns the number of dereferenceable_or_null bytes from the
  /// dereferenceable_or_null attribute.
  uint64_t getDereferenceableOrNullBytes() const;

  /// Returns the argument numbers for the allocsize attribute (or pair(0, 0)
  /// if not known).
  std::pair<unsigned, Optional<unsigned>> getAllocSizeArgs() const;

  /// The Attribute is converted to a string of equivalent mnemonic. This
  /// is, presumably, for writing out the mnemonics for the assembly writer.
  std::string getAsString(bool InAttrGrp = false) const;

  /// Equality and non-equality operators.
  bool operator==(Attribute A) const { return pImpl == A.pImpl; }
  bool operator!=(Attribute A) const { return pImpl != A.pImpl; }

  /// Less-than operator. Useful for sorting the attributes list.
  bool operator<(Attribute A) const;

  void Profile(FoldingSetNodeID &ID) const {
    ID.AddPointer(pImpl);
  }

  /// Return a raw pointer that uniquely identifies this attribute.
  void *getRawPointer() const {
    return pImpl;
  }

  /// Get an attribute from a raw pointer created by getRawPointer.
  static Attribute fromRawPointer(void *RawPtr) {
    return Attribute(reinterpret_cast<AttributeImpl*>(RawPtr));
  }
};

// Specialized opaque value conversions.
inline LLVMAttributeRef wrap(Attribute Attr) {
  return reinterpret_cast<LLVMAttributeRef>(Attr.getRawPointer());
}

// Specialized opaque value conversions.
inline Attribute unwrap(LLVMAttributeRef Attr) {
  return Attribute::fromRawPointer(Attr);
}

//===----------------------------------------------------------------------===//
/// \class
/// This class holds the attributes for a particular argument, parameter,
/// function, or return value. It is an immutable value type that is cheap to
/// copy. Adding and removing enum attributes is intended to be fast, but adding
/// and removing string or integer attributes involves a FoldingSet lookup.
class AttributeSet {
  friend AttributeListImpl;
  template <typename Ty> friend struct DenseMapInfo;

  // TODO: Extract AvailableAttrs from AttributeSetNode and store them here.
  // This will allow an efficient implementation of addAttribute and
  // removeAttribute for enum attrs.

  /// Private implementation pointer.
  AttributeSetNode *SetNode = nullptr;

private:
  explicit AttributeSet(AttributeSetNode *ASN) : SetNode(ASN) {}

public:
  /// AttributeSet is a trivially copyable value type.
  AttributeSet() = default;
  AttributeSet(const AttributeSet &) = default;
  ~AttributeSet() = default;

  static AttributeSet get(LLVMContext &C, const AttrBuilder &B);
  static AttributeSet get(LLVMContext &C, ArrayRef<Attribute> Attrs);

  bool operator==(const AttributeSet &O) const { return SetNode == O.SetNode; }
  bool operator!=(const AttributeSet &O) const { return !(*this == O); }

  /// Add an argument attribute. Returns a new set because attribute sets are
  /// immutable.
  LLVM_NODISCARD AttributeSet addAttribute(LLVMContext &C,
                                           Attribute::AttrKind Kind) const;

  /// Add a target-dependent attribute. Returns a new set because attribute sets
  /// are immutable.
  LLVM_NODISCARD AttributeSet addAttribute(LLVMContext &C, StringRef Kind,
                                           StringRef Value = StringRef()) const;

  /// Add attributes to the attribute set. Returns a new set because attribute
  /// sets are immutable.
  LLVM_NODISCARD AttributeSet addAttributes(LLVMContext &C,
                                            AttributeSet AS) const;

  /// Remove the specified attribute from this set. Returns a new set because
  /// attribute sets are immutable.
  LLVM_NODISCARD AttributeSet removeAttribute(LLVMContext &C,
                                              Attribute::AttrKind Kind) const;

  /// Remove the specified attribute from this set. Returns a new set because
  /// attribute sets are immutable.
  LLVM_NODISCARD AttributeSet removeAttribute(LLVMContext &C,
                                              StringRef Kind) const;

  /// Remove the specified attributes from this set. Returns a new set because
  /// attribute sets are immutable.
  LLVM_NODISCARD AttributeSet
  removeAttributes(LLVMContext &C, const AttrBuilder &AttrsToRemove) const;

  /// Return the number of attributes in this set.
  unsigned getNumAttributes() const;

  /// Return true if attributes exists in this set.
  bool hasAttributes() const { return SetNode != nullptr; }

  /// Return true if the attribute exists in this set.
  bool hasAttribute(Attribute::AttrKind Kind) const;

  /// Return true if the attribute exists in this set.
  bool hasAttribute(StringRef Kind) const;

  /// Return the attribute object.
  Attribute getAttribute(Attribute::AttrKind Kind) const;

  /// Return the target-dependent attribute object.
  Attribute getAttribute(StringRef Kind) const;

  unsigned getAlignment() const;
  unsigned getStackAlignment() const;
  uint64_t getDereferenceableBytes() const;
  uint64_t getDereferenceableOrNullBytes() const;
  std::pair<unsigned, Optional<unsigned>> getAllocSizeArgs() const;
  std::string getAsString(bool InAttrGrp = false) const;

  using iterator = const Attribute *;

  iterator begin() const;
  iterator end() const;
#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  void dump() const;
#endif
};

//===----------------------------------------------------------------------===//
/// \class
/// Provide DenseMapInfo for AttributeSet.
template <> struct DenseMapInfo<AttributeSet> {
  static AttributeSet getEmptyKey() {
    auto Val = static_cast<uintptr_t>(-1);
    Val <<= PointerLikeTypeTraits<void *>::NumLowBitsAvailable;
    return AttributeSet(reinterpret_cast<AttributeSetNode *>(Val));
  }

  static AttributeSet getTombstoneKey() {
    auto Val = static_cast<uintptr_t>(-2);
    Val <<= PointerLikeTypeTraits<void *>::NumLowBitsAvailable;
    return AttributeSet(reinterpret_cast<AttributeSetNode *>(Val));
  }

  static unsigned getHashValue(AttributeSet AS) {
    return (unsigned((uintptr_t)AS.SetNode) >> 4) ^
           (unsigned((uintptr_t)AS.SetNode) >> 9);
  }

  static bool isEqual(AttributeSet LHS, AttributeSet RHS) { return LHS == RHS; }
};

//===----------------------------------------------------------------------===//
/// \class
/// This class holds the attributes for a function, its return value, and
/// its parameters. You access the attributes for each of them via an index into
/// the AttributeList object. The function attributes are at index
/// `AttributeList::FunctionIndex', the return value is at index
/// `AttributeList::ReturnIndex', and the attributes for the parameters start at
/// index `AttributeList::FirstArgIndex'.
class AttributeList {
public:
  enum AttrIndex : unsigned {
    ReturnIndex = 0U,
    FunctionIndex = ~0U,
    FirstArgIndex = 1,
  };

private:
  friend class AttrBuilder;
  friend class AttributeListImpl;
  friend class AttributeSet;
  friend class AttributeSetNode;
  template <typename Ty> friend struct DenseMapInfo;

  /// The attributes that we are managing. This can be null to represent
  /// the empty attributes list.
  AttributeListImpl *pImpl = nullptr;

public:
  /// Create an AttributeList with the specified parameters in it.
  static AttributeList get(LLVMContext &C,
                           ArrayRef<std::pair<unsigned, Attribute>> Attrs);
  static AttributeList get(LLVMContext &C,
                           ArrayRef<std::pair<unsigned, AttributeSet>> Attrs);

  /// Create an AttributeList from attribute sets for a function, its
  /// return value, and all of its arguments.
  static AttributeList get(LLVMContext &C, AttributeSet FnAttrs,
                           AttributeSet RetAttrs,
                           ArrayRef<AttributeSet> ArgAttrs);

private:
  explicit AttributeList(AttributeListImpl *LI) : pImpl(LI) {}

  static AttributeList getImpl(LLVMContext &C, ArrayRef<AttributeSet> AttrSets);

public:
  AttributeList() = default;

  //===--------------------------------------------------------------------===//
  // AttributeList Construction and Mutation
  //===--------------------------------------------------------------------===//

  /// Return an AttributeList with the specified parameters in it.
  static AttributeList get(LLVMContext &C, ArrayRef<AttributeList> Attrs);
  static AttributeList get(LLVMContext &C, unsigned Index,
                           ArrayRef<Attribute::AttrKind> Kinds);
  static AttributeList get(LLVMContext &C, unsigned Index,
                           ArrayRef<StringRef> Kind);
  static AttributeList get(LLVMContext &C, unsigned Index,
                           const AttrBuilder &B);

  /// Add an attribute to the attribute set at the given index.
  /// Returns a new list because attribute lists are immutable.
  LLVM_NODISCARD AttributeList addAttribute(LLVMContext &C, unsigned Index,
                                            Attribute::AttrKind Kind) const;

  /// Add an attribute to the attribute set at the given index.
  /// Returns a new list because attribute lists are immutable.
  LLVM_NODISCARD AttributeList
  addAttribute(LLVMContext &C, unsigned Index, StringRef Kind,
               StringRef Value = StringRef()) const;

  /// Add an attribute to the attribute set at the given index.
  /// Returns a new list because attribute lists are immutable.
  LLVM_NODISCARD AttributeList addAttribute(LLVMContext &C, unsigned Index,
                                            Attribute A) const;

  /// Add attributes to the attribute set at the given index.
  /// Returns a new list because attribute lists are immutable.
  LLVM_NODISCARD AttributeList addAttributes(LLVMContext &C, unsigned Index,
                                             const AttrBuilder &B) const;

  /// Add an argument attribute to the list. Returns a new list because
  /// attribute lists are immutable.
  LLVM_NODISCARD AttributeList addParamAttribute(
      LLVMContext &C, unsigned ArgNo, Attribute::AttrKind Kind) const {
    return addAttribute(C, ArgNo + FirstArgIndex, Kind);
  }

  /// Add an argument attribute to the list. Returns a new list because
  /// attribute lists are immutable.
  LLVM_NODISCARD AttributeList
  addParamAttribute(LLVMContext &C, unsigned ArgNo, StringRef Kind,
                    StringRef Value = StringRef()) const {
    return addAttribute(C, ArgNo + FirstArgIndex, Kind, Value);
  }

  /// Add an attribute to the attribute list at the given arg indices. Returns a
  /// new list because attribute lists are immutable.
  LLVM_NODISCARD AttributeList addParamAttribute(LLVMContext &C,
                                                 ArrayRef<unsigned> ArgNos,
                                                 Attribute A) const;

  /// Add an argument attribute to the list. Returns a new list because
  /// attribute lists are immutable.
  LLVM_NODISCARD AttributeList addParamAttributes(LLVMContext &C,
                                                  unsigned ArgNo,
                                                  const AttrBuilder &B) const {
    return addAttributes(C, ArgNo + FirstArgIndex, B);
  }

  /// Remove the specified attribute at the specified index from this
  /// attribute list. Returns a new list because attribute lists are immutable.
  LLVM_NODISCARD AttributeList removeAttribute(LLVMContext &C, unsigned Index,
                                               Attribute::AttrKind Kind) const;

  /// Remove the specified attribute at the specified index from this
  /// attribute list. Returns a new list because attribute lists are immutable.
  LLVM_NODISCARD AttributeList removeAttribute(LLVMContext &C, unsigned Index,
                                               StringRef Kind) const;

  /// Remove the specified attributes at the specified index from this
  /// attribute list. Returns a new list because attribute lists are immutable.
  LLVM_NODISCARD AttributeList removeAttributes(
      LLVMContext &C, unsigned Index, const AttrBuilder &AttrsToRemove) const;

  /// Remove all attributes at the specified index from this
  /// attribute list. Returns a new list because attribute lists are immutable.
  LLVM_NODISCARD AttributeList removeAttributes(LLVMContext &C,
                                                unsigned Index) const;

  /// Remove the specified attribute at the specified arg index from this
  /// attribute list. Returns a new list because attribute lists are immutable.
  LLVM_NODISCARD AttributeList removeParamAttribute(
      LLVMContext &C, unsigned ArgNo, Attribute::AttrKind Kind) const {
    return removeAttribute(C, ArgNo + FirstArgIndex, Kind);
  }

  /// Remove the specified attribute at the specified arg index from this
  /// attribute list. Returns a new list because attribute lists are immutable.
  LLVM_NODISCARD AttributeList removeParamAttribute(LLVMContext &C,
                                                    unsigned ArgNo,
                                                    StringRef Kind) const {
    return removeAttribute(C, ArgNo + FirstArgIndex, Kind);
  }

  /// Remove the specified attribute at the specified arg index from this
  /// attribute list. Returns a new list because attribute lists are immutable.
  LLVM_NODISCARD AttributeList removeParamAttributes(
      LLVMContext &C, unsigned ArgNo, const AttrBuilder &AttrsToRemove) const {
    return removeAttributes(C, ArgNo + FirstArgIndex, AttrsToRemove);
  }

  /// Remove all attributes at the specified arg index from this
  /// attribute list. Returns a new list because attribute lists are immutable.
  LLVM_NODISCARD AttributeList removeParamAttributes(LLVMContext &C,
                                                     unsigned ArgNo) const {
    return removeAttributes(C, ArgNo + FirstArgIndex);
  }

  /// \brief Add the dereferenceable attribute to the attribute set at the given
  /// index. Returns a new list because attribute lists are immutable.
  LLVM_NODISCARD AttributeList addDereferenceableAttr(LLVMContext &C,
                                                      unsigned Index,
                                                      uint64_t Bytes) const;

  /// \brief Add the dereferenceable attribute to the attribute set at the given
  /// arg index. Returns a new list because attribute lists are immutable.
  LLVM_NODISCARD AttributeList addDereferenceableParamAttr(
      LLVMContext &C, unsigned ArgNo, uint64_t Bytes) const {
    return addDereferenceableAttr(C, ArgNo + FirstArgIndex, Bytes);
  }

  /// Add the dereferenceable_or_null attribute to the attribute set at
  /// the given index. Returns a new list because attribute lists are immutable.
  LLVM_NODISCARD AttributeList addDereferenceableOrNullAttr(
      LLVMContext &C, unsigned Index, uint64_t Bytes) const;

  /// Add the dereferenceable_or_null attribute to the attribute set at
  /// the given arg index. Returns a new list because attribute lists are
  /// immutable.
  LLVM_NODISCARD AttributeList addDereferenceableOrNullParamAttr(
      LLVMContext &C, unsigned ArgNo, uint64_t Bytes) const {
    return addDereferenceableOrNullAttr(C, ArgNo + FirstArgIndex, Bytes);
  }

  /// Add the allocsize attribute to the attribute set at the given index.
  /// Returns a new list because attribute lists are immutable.
  LLVM_NODISCARD AttributeList
  addAllocSizeAttr(LLVMContext &C, unsigned Index, unsigned ElemSizeArg,
                   const Optional<unsigned> &NumElemsArg);

  /// Add the allocsize attribute to the attribute set at the given arg index.
  /// Returns a new list because attribute lists are immutable.
  LLVM_NODISCARD AttributeList
  addAllocSizeParamAttr(LLVMContext &C, unsigned ArgNo, unsigned ElemSizeArg,
                        const Optional<unsigned> &NumElemsArg) {
    return addAllocSizeAttr(C, ArgNo + FirstArgIndex, ElemSizeArg, NumElemsArg);
  }

  //===--------------------------------------------------------------------===//
  // AttributeList Accessors
  //===--------------------------------------------------------------------===//

  /// Retrieve the LLVM context.
  LLVMContext &getContext() const;

  /// The attributes for the specified index are returned.
  AttributeSet getAttributes(unsigned Index) const;

  /// The attributes for the argument or parameter at the given index are
  /// returned.
  AttributeSet getParamAttributes(unsigned ArgNo) const;

  /// The attributes for the ret value are returned.
  AttributeSet getRetAttributes() const;

  /// The function attributes are returned.
  AttributeSet getFnAttributes() const;

  /// Return true if the attribute exists at the given index.
  bool hasAttribute(unsigned Index, Attribute::AttrKind Kind) const;

  /// Return true if the attribute exists at the given index.
  bool hasAttribute(unsigned Index, StringRef Kind) const;

  /// Return true if attribute exists at the given index.
  bool hasAttributes(unsigned Index) const;

  /// Return true if the attribute exists for the given argument
  bool hasParamAttr(unsigned ArgNo, Attribute::AttrKind Kind) const {
    return hasAttribute(ArgNo + FirstArgIndex, Kind);
  }

  /// Return true if the attribute exists for the given argument
  bool hasParamAttr(unsigned ArgNo, StringRef Kind) const {
    return hasAttribute(ArgNo + FirstArgIndex, Kind);
  }

  /// Return true if attributes exists for the given argument
  bool hasParamAttrs(unsigned ArgNo) const {
    return hasAttributes(ArgNo + FirstArgIndex);
  }

  /// Equivalent to hasAttribute(AttributeList::FunctionIndex, Kind) but
  /// may be faster.
  bool hasFnAttribute(Attribute::AttrKind Kind) const;

  /// Equivalent to hasAttribute(AttributeList::FunctionIndex, Kind) but
  /// may be faster.
  bool hasFnAttribute(StringRef Kind) const;

  /// Equivalent to hasAttribute(ArgNo + FirstArgIndex, Kind).
  bool hasParamAttribute(unsigned ArgNo, Attribute::AttrKind Kind) const;

  /// Return true if the specified attribute is set for at least one
  /// parameter or for the return value. If Index is not nullptr, the index
  /// of a parameter with the specified attribute is provided.
  bool hasAttrSomewhere(Attribute::AttrKind Kind,
                        unsigned *Index = nullptr) const;

  /// Return the attribute object that exists at the given index.
  Attribute getAttribute(unsigned Index, Attribute::AttrKind Kind) const;

  /// Return the attribute object that exists at the given index.
  Attribute getAttribute(unsigned Index, StringRef Kind) const;

  /// Return the attribute object that exists at the arg index.
  Attribute getParamAttr(unsigned ArgNo, Attribute::AttrKind Kind) const {
    return getAttribute(ArgNo + FirstArgIndex, Kind);
  }

  /// Return the attribute object that exists at the given index.
  Attribute getParamAttr(unsigned ArgNo, StringRef Kind) const {
    return getAttribute(ArgNo + FirstArgIndex, Kind);
  }

  /// Return the alignment of the return value.
  unsigned getRetAlignment() const;

  /// Return the alignment for the specified function parameter.
  unsigned getParamAlignment(unsigned ArgNo) const;

  /// Get the stack alignment.
  unsigned getStackAlignment(unsigned Index) const;

  /// Get the number of dereferenceable bytes (or zero if unknown).
  uint64_t getDereferenceableBytes(unsigned Index) const;

  /// Get the number of dereferenceable bytes (or zero if unknown) of an
  /// arg.
  uint64_t getParamDereferenceableBytes(unsigned ArgNo) const {
    return getDereferenceableBytes(ArgNo + FirstArgIndex);
  }

  /// Get the number of dereferenceable_or_null bytes (or zero if
  /// unknown).
  uint64_t getDereferenceableOrNullBytes(unsigned Index) const;

  /// Get the number of dereferenceable_or_null bytes (or zero if
  /// unknown) of an arg.
  uint64_t getParamDereferenceableOrNullBytes(unsigned ArgNo) const {
    return getDereferenceableOrNullBytes(ArgNo + FirstArgIndex);
  }

  /// Get the allocsize argument numbers (or pair(0, 0) if unknown).
  std::pair<unsigned, Optional<unsigned>>
  getAllocSizeArgs(unsigned Index) const;

  /// Return the attributes at the index as a string.
  std::string getAsString(unsigned Index, bool InAttrGrp = false) const;

  //===--------------------------------------------------------------------===//
  // AttributeList Introspection
  //===--------------------------------------------------------------------===//

  using iterator = const AttributeSet *;

  iterator begin() const;
  iterator end() const;

  unsigned getNumAttrSets() const;

  /// Use these to iterate over the valid attribute indices.
  unsigned index_begin() const { return AttributeList::FunctionIndex; }
  unsigned index_end() const { return getNumAttrSets() - 1; }

  /// operator==/!= - Provide equality predicates.
  bool operator==(const AttributeList &RHS) const { return pImpl == RHS.pImpl; }
  bool operator!=(const AttributeList &RHS) const { return pImpl != RHS.pImpl; }

  /// Return a raw pointer that uniquely identifies this attribute list.
  void *getRawPointer() const {
    return pImpl;
  }

  /// Return true if there are no attributes.
  bool isEmpty() const { return pImpl == nullptr; }

  void dump() const;
};

//===----------------------------------------------------------------------===//
/// \class
/// Provide DenseMapInfo for AttributeList.
template <> struct DenseMapInfo<AttributeList> {
  static AttributeList getEmptyKey() {
    auto Val = static_cast<uintptr_t>(-1);
    Val <<= PointerLikeTypeTraits<void*>::NumLowBitsAvailable;
    return AttributeList(reinterpret_cast<AttributeListImpl *>(Val));
  }

  static AttributeList getTombstoneKey() {
    auto Val = static_cast<uintptr_t>(-2);
    Val <<= PointerLikeTypeTraits<void*>::NumLowBitsAvailable;
    return AttributeList(reinterpret_cast<AttributeListImpl *>(Val));
  }

  static unsigned getHashValue(AttributeList AS) {
    return (unsigned((uintptr_t)AS.pImpl) >> 4) ^
           (unsigned((uintptr_t)AS.pImpl) >> 9);
  }

  static bool isEqual(AttributeList LHS, AttributeList RHS) {
    return LHS == RHS;
  }
};

//===----------------------------------------------------------------------===//
/// \class
/// This class is used in conjunction with the Attribute::get method to
/// create an Attribute object. The object itself is uniquified. The Builder's
/// value, however, is not. So this can be used as a quick way to test for
/// equality, presence of attributes, etc.
class AttrBuilder {
  std::bitset<Attribute::EndAttrKinds> Attrs;
  std::map<std::string, std::string> TargetDepAttrs;
  uint64_t Alignment = 0;
  uint64_t StackAlignment = 0;
  uint64_t DerefBytes = 0;
  uint64_t DerefOrNullBytes = 0;
  uint64_t AllocSizeArgs = 0;

public:
  AttrBuilder() = default;

  AttrBuilder(const Attribute &A) {
    addAttribute(A);
  }

  AttrBuilder(AttributeList AS, unsigned Idx);
  AttrBuilder(AttributeSet AS);

  void clear();

  /// Add an attribute to the builder.
  AttrBuilder &addAttribute(Attribute::AttrKind Val);

  /// Add the Attribute object to the builder.
  AttrBuilder &addAttribute(Attribute A);

  /// Add the target-dependent attribute to the builder.
  AttrBuilder &addAttribute(StringRef A, StringRef V = StringRef());

  /// Remove an attribute from the builder.
  AttrBuilder &removeAttribute(Attribute::AttrKind Val);

  /// Remove the attributes from the builder.
  AttrBuilder &removeAttributes(AttributeList A, uint64_t WithoutIndex);

  /// Remove the target-dependent attribute to the builder.
  AttrBuilder &removeAttribute(StringRef A);

  /// Add the attributes from the builder.
  AttrBuilder &merge(const AttrBuilder &B);

  /// Remove the attributes from the builder.
  AttrBuilder &remove(const AttrBuilder &B);

  /// Return true if the builder has any attribute that's in the
  /// specified builder.
  bool overlaps(const AttrBuilder &B) const;

  /// Return true if the builder has the specified attribute.
  bool contains(Attribute::AttrKind A) const {
    assert((unsigned)A < Attribute::EndAttrKinds && "Attribute out of range!");
    return Attrs[A];
  }

  /// Return true if the builder has the specified target-dependent
  /// attribute.
  bool contains(StringRef A) const;

  /// Return true if the builder has IR-level attributes.
  bool hasAttributes() const;

  /// Return true if the builder has any attribute that's in the
  /// specified attribute.
  bool hasAttributes(AttributeList A, uint64_t Index) const;

  /// Return true if the builder has an alignment attribute.
  bool hasAlignmentAttr() const;

  /// Retrieve the alignment attribute, if it exists.
  uint64_t getAlignment() const { return Alignment; }

  /// Retrieve the stack alignment attribute, if it exists.
  uint64_t getStackAlignment() const { return StackAlignment; }

  /// Retrieve the number of dereferenceable bytes, if the
  /// dereferenceable attribute exists (zero is returned otherwise).
  uint64_t getDereferenceableBytes() const { return DerefBytes; }

  /// Retrieve the number of dereferenceable_or_null bytes, if the
  /// dereferenceable_or_null attribute exists (zero is returned otherwise).
  uint64_t getDereferenceableOrNullBytes() const { return DerefOrNullBytes; }

  /// Retrieve the allocsize args, if the allocsize attribute exists.  If it
  /// doesn't exist, pair(0, 0) is returned.
  std::pair<unsigned, Optional<unsigned>> getAllocSizeArgs() const;

  /// This turns an int alignment (which must be a power of 2) into the
  /// form used internally in Attribute.
  AttrBuilder &addAlignmentAttr(unsigned Align);

  /// This turns an int stack alignment (which must be a power of 2) into
  /// the form used internally in Attribute.
  AttrBuilder &addStackAlignmentAttr(unsigned Align);

  /// This turns the number of dereferenceable bytes into the form used
  /// internally in Attribute.
  AttrBuilder &addDereferenceableAttr(uint64_t Bytes);

  /// This turns the number of dereferenceable_or_null bytes into the
  /// form used internally in Attribute.
  AttrBuilder &addDereferenceableOrNullAttr(uint64_t Bytes);

  /// This turns one (or two) ints into the form used internally in Attribute.
  AttrBuilder &addAllocSizeAttr(unsigned ElemSizeArg,
                                const Optional<unsigned> &NumElemsArg);

  /// Add an allocsize attribute, using the representation returned by
  /// Attribute.getIntValue().
  AttrBuilder &addAllocSizeAttrFromRawRepr(uint64_t RawAllocSizeRepr);

  /// Return true if the builder contains no target-independent
  /// attributes.
  bool empty() const { return Attrs.none(); }

  // Iterators for target-dependent attributes.
  using td_type = std::pair<std::string, std::string>;
  using td_iterator = std::map<std::string, std::string>::iterator;
  using td_const_iterator = std::map<std::string, std::string>::const_iterator;
  using td_range = iterator_range<td_iterator>;
  using td_const_range = iterator_range<td_const_iterator>;

  td_iterator td_begin() { return TargetDepAttrs.begin(); }
  td_iterator td_end() { return TargetDepAttrs.end(); }

  td_const_iterator td_begin() const { return TargetDepAttrs.begin(); }
  td_const_iterator td_end() const { return TargetDepAttrs.end(); }

  td_range td_attrs() { return td_range(td_begin(), td_end()); }

  td_const_range td_attrs() const {
    return td_const_range(td_begin(), td_end());
  }

  bool td_empty() const { return TargetDepAttrs.empty(); }

  bool operator==(const AttrBuilder &B);
  bool operator!=(const AttrBuilder &B) {
    return !(*this == B);
  }
};

namespace AttributeFuncs {

/// Which attributes cannot be applied to a type.
AttrBuilder typeIncompatible(Type *Ty);

/// \returns Return true if the two functions have compatible target-independent
/// attributes for inlining purposes.
bool areInlineCompatible(const Function &Caller, const Function &Callee);

/// Merge caller's and callee's attributes.
void mergeAttributesForInlining(Function &Caller, const Function &Callee);

} // end namespace AttributeFuncs

} // end namespace llvm

#endif // LLVM_IR_ATTRIBUTES_H
