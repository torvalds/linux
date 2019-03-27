//===- llvm/DerivedTypes.h - Classes for handling data types ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the declarations of classes that represent "derived
// types".  These are things like "arrays of x" or "structure of x, y, z" or
// "function returning x taking (y,z) as parameters", etc...
//
// The implementations of these classes live in the Type.cpp file.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_DERIVEDTYPES_H
#define LLVM_IR_DERIVEDTYPES_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include <cassert>
#include <cstdint>

namespace llvm {

class Value;
class APInt;
class LLVMContext;

/// Class to represent integer types. Note that this class is also used to
/// represent the built-in integer types: Int1Ty, Int8Ty, Int16Ty, Int32Ty and
/// Int64Ty.
/// Integer representation type
class IntegerType : public Type {
  friend class LLVMContextImpl;

protected:
  explicit IntegerType(LLVMContext &C, unsigned NumBits) : Type(C, IntegerTyID){
    setSubclassData(NumBits);
  }

public:
  /// This enum is just used to hold constants we need for IntegerType.
  enum {
    MIN_INT_BITS = 1,        ///< Minimum number of bits that can be specified
    MAX_INT_BITS = (1<<24)-1 ///< Maximum number of bits that can be specified
      ///< Note that bit width is stored in the Type classes SubclassData field
      ///< which has 24 bits. This yields a maximum bit width of 16,777,215
      ///< bits.
  };

  /// This static method is the primary way of constructing an IntegerType.
  /// If an IntegerType with the same NumBits value was previously instantiated,
  /// that instance will be returned. Otherwise a new one will be created. Only
  /// one instance with a given NumBits value is ever created.
  /// Get or create an IntegerType instance.
  static IntegerType *get(LLVMContext &C, unsigned NumBits);

  /// Get the number of bits in this IntegerType
  unsigned getBitWidth() const { return getSubclassData(); }

  /// Return a bitmask with ones set for all of the bits that can be set by an
  /// unsigned version of this type. This is 0xFF for i8, 0xFFFF for i16, etc.
  uint64_t getBitMask() const {
    return ~uint64_t(0UL) >> (64-getBitWidth());
  }

  /// Return a uint64_t with just the most significant bit set (the sign bit, if
  /// the value is treated as a signed number).
  uint64_t getSignBit() const {
    return 1ULL << (getBitWidth()-1);
  }

  /// For example, this is 0xFF for an 8 bit integer, 0xFFFF for i16, etc.
  /// @returns a bit mask with ones set for all the bits of this type.
  /// Get a bit mask for this type.
  APInt getMask() const;

  /// This method determines if the width of this IntegerType is a power-of-2
  /// in terms of 8 bit bytes.
  /// @returns true if this is a power-of-2 byte width.
  /// Is this a power-of-2 byte-width IntegerType ?
  bool isPowerOf2ByteWidth() const;

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  static bool classof(const Type *T) {
    return T->getTypeID() == IntegerTyID;
  }
};

unsigned Type::getIntegerBitWidth() const {
  return cast<IntegerType>(this)->getBitWidth();
}

/// Class to represent function types
///
class FunctionType : public Type {
  FunctionType(Type *Result, ArrayRef<Type*> Params, bool IsVarArgs);

public:
  FunctionType(const FunctionType &) = delete;
  FunctionType &operator=(const FunctionType &) = delete;

  /// This static method is the primary way of constructing a FunctionType.
  static FunctionType *get(Type *Result,
                           ArrayRef<Type*> Params, bool isVarArg);

  /// Create a FunctionType taking no parameters.
  static FunctionType *get(Type *Result, bool isVarArg);

  /// Return true if the specified type is valid as a return type.
  static bool isValidReturnType(Type *RetTy);

  /// Return true if the specified type is valid as an argument type.
  static bool isValidArgumentType(Type *ArgTy);

  bool isVarArg() const { return getSubclassData()!=0; }
  Type *getReturnType() const { return ContainedTys[0]; }

  using param_iterator = Type::subtype_iterator;

  param_iterator param_begin() const { return ContainedTys + 1; }
  param_iterator param_end() const { return &ContainedTys[NumContainedTys]; }
  ArrayRef<Type *> params() const {
    return makeArrayRef(param_begin(), param_end());
  }

  /// Parameter type accessors.
  Type *getParamType(unsigned i) const { return ContainedTys[i+1]; }

  /// Return the number of fixed parameters this function type requires.
  /// This does not consider varargs.
  unsigned getNumParams() const { return NumContainedTys - 1; }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  static bool classof(const Type *T) {
    return T->getTypeID() == FunctionTyID;
  }
};
static_assert(alignof(FunctionType) >= alignof(Type *),
              "Alignment sufficient for objects appended to FunctionType");

bool Type::isFunctionVarArg() const {
  return cast<FunctionType>(this)->isVarArg();
}

Type *Type::getFunctionParamType(unsigned i) const {
  return cast<FunctionType>(this)->getParamType(i);
}

unsigned Type::getFunctionNumParams() const {
  return cast<FunctionType>(this)->getNumParams();
}

/// Common super class of ArrayType, StructType and VectorType.
class CompositeType : public Type {
protected:
  explicit CompositeType(LLVMContext &C, TypeID tid) : Type(C, tid) {}

public:
  /// Given an index value into the type, return the type of the element.
  Type *getTypeAtIndex(const Value *V) const;
  Type *getTypeAtIndex(unsigned Idx) const;
  bool indexValid(const Value *V) const;
  bool indexValid(unsigned Idx) const;

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  static bool classof(const Type *T) {
    return T->getTypeID() == ArrayTyID ||
           T->getTypeID() == StructTyID ||
           T->getTypeID() == VectorTyID;
  }
};

/// Class to represent struct types. There are two different kinds of struct
/// types: Literal structs and Identified structs.
///
/// Literal struct types (e.g. { i32, i32 }) are uniqued structurally, and must
/// always have a body when created.  You can get one of these by using one of
/// the StructType::get() forms.
///
/// Identified structs (e.g. %foo or %42) may optionally have a name and are not
/// uniqued.  The names for identified structs are managed at the LLVMContext
/// level, so there can only be a single identified struct with a given name in
/// a particular LLVMContext.  Identified structs may also optionally be opaque
/// (have no body specified).  You get one of these by using one of the
/// StructType::create() forms.
///
/// Independent of what kind of struct you have, the body of a struct type are
/// laid out in memory consecutively with the elements directly one after the
/// other (if the struct is packed) or (if not packed) with padding between the
/// elements as defined by DataLayout (which is required to match what the code
/// generator for a target expects).
///
class StructType : public CompositeType {
  StructType(LLVMContext &C) : CompositeType(C, StructTyID) {}

  enum {
    /// This is the contents of the SubClassData field.
    SCDB_HasBody = 1,
    SCDB_Packed = 2,
    SCDB_IsLiteral = 4,
    SCDB_IsSized = 8
  };

  /// For a named struct that actually has a name, this is a pointer to the
  /// symbol table entry (maintained by LLVMContext) for the struct.
  /// This is null if the type is an literal struct or if it is a identified
  /// type that has an empty name.
  void *SymbolTableEntry = nullptr;

public:
  StructType(const StructType &) = delete;
  StructType &operator=(const StructType &) = delete;

  /// This creates an identified struct.
  static StructType *create(LLVMContext &Context, StringRef Name);
  static StructType *create(LLVMContext &Context);

  static StructType *create(ArrayRef<Type *> Elements, StringRef Name,
                            bool isPacked = false);
  static StructType *create(ArrayRef<Type *> Elements);
  static StructType *create(LLVMContext &Context, ArrayRef<Type *> Elements,
                            StringRef Name, bool isPacked = false);
  static StructType *create(LLVMContext &Context, ArrayRef<Type *> Elements);
  template <class... Tys>
  static typename std::enable_if<are_base_of<Type, Tys...>::value,
                                 StructType *>::type
  create(StringRef Name, Type *elt1, Tys *... elts) {
    assert(elt1 && "Cannot create a struct type with no elements with this");
    SmallVector<llvm::Type *, 8> StructFields({elt1, elts...});
    return create(StructFields, Name);
  }

  /// This static method is the primary way to create a literal StructType.
  static StructType *get(LLVMContext &Context, ArrayRef<Type*> Elements,
                         bool isPacked = false);

  /// Create an empty structure type.
  static StructType *get(LLVMContext &Context, bool isPacked = false);

  /// This static method is a convenience method for creating structure types by
  /// specifying the elements as arguments. Note that this method always returns
  /// a non-packed struct, and requires at least one element type.
  template <class... Tys>
  static typename std::enable_if<are_base_of<Type, Tys...>::value,
                                 StructType *>::type
  get(Type *elt1, Tys *... elts) {
    assert(elt1 && "Cannot create a struct type with no elements with this");
    LLVMContext &Ctx = elt1->getContext();
    SmallVector<llvm::Type *, 8> StructFields({elt1, elts...});
    return llvm::StructType::get(Ctx, StructFields);
  }

  bool isPacked() const { return (getSubclassData() & SCDB_Packed) != 0; }

  /// Return true if this type is uniqued by structural equivalence, false if it
  /// is a struct definition.
  bool isLiteral() const { return (getSubclassData() & SCDB_IsLiteral) != 0; }

  /// Return true if this is a type with an identity that has no body specified
  /// yet. These prints as 'opaque' in .ll files.
  bool isOpaque() const { return (getSubclassData() & SCDB_HasBody) == 0; }

  /// isSized - Return true if this is a sized type.
  bool isSized(SmallPtrSetImpl<Type *> *Visited = nullptr) const;

  /// Return true if this is a named struct that has a non-empty name.
  bool hasName() const { return SymbolTableEntry != nullptr; }

  /// Return the name for this struct type if it has an identity.
  /// This may return an empty string for an unnamed struct type.  Do not call
  /// this on an literal type.
  StringRef getName() const;

  /// Change the name of this type to the specified name, or to a name with a
  /// suffix if there is a collision. Do not call this on an literal type.
  void setName(StringRef Name);

  /// Specify a body for an opaque identified type.
  void setBody(ArrayRef<Type*> Elements, bool isPacked = false);

  template <typename... Tys>
  typename std::enable_if<are_base_of<Type, Tys...>::value, void>::type
  setBody(Type *elt1, Tys *... elts) {
    assert(elt1 && "Cannot create a struct type with no elements with this");
    SmallVector<llvm::Type *, 8> StructFields({elt1, elts...});
    setBody(StructFields);
  }

  /// Return true if the specified type is valid as a element type.
  static bool isValidElementType(Type *ElemTy);

  // Iterator access to the elements.
  using element_iterator = Type::subtype_iterator;

  element_iterator element_begin() const { return ContainedTys; }
  element_iterator element_end() const { return &ContainedTys[NumContainedTys];}
  ArrayRef<Type *> const elements() const {
    return makeArrayRef(element_begin(), element_end());
  }

  /// Return true if this is layout identical to the specified struct.
  bool isLayoutIdentical(StructType *Other) const;

  /// Random access to the elements
  unsigned getNumElements() const { return NumContainedTys; }
  Type *getElementType(unsigned N) const {
    assert(N < NumContainedTys && "Element number out of range!");
    return ContainedTys[N];
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  static bool classof(const Type *T) {
    return T->getTypeID() == StructTyID;
  }
};

StringRef Type::getStructName() const {
  return cast<StructType>(this)->getName();
}

unsigned Type::getStructNumElements() const {
  return cast<StructType>(this)->getNumElements();
}

Type *Type::getStructElementType(unsigned N) const {
  return cast<StructType>(this)->getElementType(N);
}

/// This is the superclass of the array and vector type classes. Both of these
/// represent "arrays" in memory. The array type represents a specifically sized
/// array, and the vector type represents a specifically sized array that allows
/// for use of SIMD instructions. SequentialType holds the common features of
/// both, which stem from the fact that both lay their components out in memory
/// identically.
class SequentialType : public CompositeType {
  Type *ContainedType;               ///< Storage for the single contained type.
  uint64_t NumElements;

protected:
  SequentialType(TypeID TID, Type *ElType, uint64_t NumElements)
    : CompositeType(ElType->getContext(), TID), ContainedType(ElType),
      NumElements(NumElements) {
    ContainedTys = &ContainedType;
    NumContainedTys = 1;
  }

public:
  SequentialType(const SequentialType &) = delete;
  SequentialType &operator=(const SequentialType &) = delete;

  uint64_t getNumElements() const { return NumElements; }
  Type *getElementType() const { return ContainedType; }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  static bool classof(const Type *T) {
    return T->getTypeID() == ArrayTyID || T->getTypeID() == VectorTyID;
  }
};

/// Class to represent array types.
class ArrayType : public SequentialType {
  ArrayType(Type *ElType, uint64_t NumEl);

public:
  ArrayType(const ArrayType &) = delete;
  ArrayType &operator=(const ArrayType &) = delete;

  /// This static method is the primary way to construct an ArrayType
  static ArrayType *get(Type *ElementType, uint64_t NumElements);

  /// Return true if the specified type is valid as a element type.
  static bool isValidElementType(Type *ElemTy);

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  static bool classof(const Type *T) {
    return T->getTypeID() == ArrayTyID;
  }
};

uint64_t Type::getArrayNumElements() const {
  return cast<ArrayType>(this)->getNumElements();
}

/// Class to represent vector types.
class VectorType : public SequentialType {
  VectorType(Type *ElType, unsigned NumEl);

public:
  VectorType(const VectorType &) = delete;
  VectorType &operator=(const VectorType &) = delete;

  /// This static method is the primary way to construct an VectorType.
  static VectorType *get(Type *ElementType, unsigned NumElements);

  /// This static method gets a VectorType with the same number of elements as
  /// the input type, and the element type is an integer type of the same width
  /// as the input element type.
  static VectorType *getInteger(VectorType *VTy) {
    unsigned EltBits = VTy->getElementType()->getPrimitiveSizeInBits();
    assert(EltBits && "Element size must be of a non-zero size");
    Type *EltTy = IntegerType::get(VTy->getContext(), EltBits);
    return VectorType::get(EltTy, VTy->getNumElements());
  }

  /// This static method is like getInteger except that the element types are
  /// twice as wide as the elements in the input type.
  static VectorType *getExtendedElementVectorType(VectorType *VTy) {
    unsigned EltBits = VTy->getElementType()->getPrimitiveSizeInBits();
    Type *EltTy = IntegerType::get(VTy->getContext(), EltBits * 2);
    return VectorType::get(EltTy, VTy->getNumElements());
  }

  /// This static method is like getInteger except that the element types are
  /// half as wide as the elements in the input type.
  static VectorType *getTruncatedElementVectorType(VectorType *VTy) {
    unsigned EltBits = VTy->getElementType()->getPrimitiveSizeInBits();
    assert((EltBits & 1) == 0 &&
           "Cannot truncate vector element with odd bit-width");
    Type *EltTy = IntegerType::get(VTy->getContext(), EltBits / 2);
    return VectorType::get(EltTy, VTy->getNumElements());
  }

  /// This static method returns a VectorType with half as many elements as the
  /// input type and the same element type.
  static VectorType *getHalfElementsVectorType(VectorType *VTy) {
    unsigned NumElts = VTy->getNumElements();
    assert ((NumElts & 1) == 0 &&
            "Cannot halve vector with odd number of elements.");
    return VectorType::get(VTy->getElementType(), NumElts/2);
  }

  /// This static method returns a VectorType with twice as many elements as the
  /// input type and the same element type.
  static VectorType *getDoubleElementsVectorType(VectorType *VTy) {
    unsigned NumElts = VTy->getNumElements();
    return VectorType::get(VTy->getElementType(), NumElts*2);
  }

  /// Return true if the specified type is valid as a element type.
  static bool isValidElementType(Type *ElemTy);

  /// Return the number of bits in the Vector type.
  /// Returns zero when the vector is a vector of pointers.
  unsigned getBitWidth() const {
    return getNumElements() * getElementType()->getPrimitiveSizeInBits();
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  static bool classof(const Type *T) {
    return T->getTypeID() == VectorTyID;
  }
};

unsigned Type::getVectorNumElements() const {
  return cast<VectorType>(this)->getNumElements();
}

/// Class to represent pointers.
class PointerType : public Type {
  explicit PointerType(Type *ElType, unsigned AddrSpace);

  Type *PointeeTy;

public:
  PointerType(const PointerType &) = delete;
  PointerType &operator=(const PointerType &) = delete;

  /// This constructs a pointer to an object of the specified type in a numbered
  /// address space.
  static PointerType *get(Type *ElementType, unsigned AddressSpace);

  /// This constructs a pointer to an object of the specified type in the
  /// generic address space (address space zero).
  static PointerType *getUnqual(Type *ElementType) {
    return PointerType::get(ElementType, 0);
  }

  Type *getElementType() const { return PointeeTy; }

  /// Return true if the specified type is valid as a element type.
  static bool isValidElementType(Type *ElemTy);

  /// Return true if we can load or store from a pointer to this type.
  static bool isLoadableOrStorableType(Type *ElemTy);

  /// Return the address space of the Pointer type.
  inline unsigned getAddressSpace() const { return getSubclassData(); }

  /// Implement support type inquiry through isa, cast, and dyn_cast.
  static bool classof(const Type *T) {
    return T->getTypeID() == PointerTyID;
  }
};

unsigned Type::getPointerAddressSpace() const {
  return cast<PointerType>(getScalarType())->getAddressSpace();
}

} // end namespace llvm

#endif // LLVM_IR_DERIVEDTYPES_H
