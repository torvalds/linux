//== llvm/Support/LowLevelTypeImpl.h --------------------------- -*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// Implement a low-level type suitable for MachineInstr level instruction
/// selection.
///
/// For a type attached to a MachineInstr, we only care about 2 details: total
/// size and the number of vector lanes (if any). Accordingly, there are 4
/// possible valid type-kinds:
///
///    * `sN` for scalars and aggregates
///    * `<N x sM>` for vectors, which must have at least 2 elements.
///    * `pN` for pointers
///
/// Other information required for correct selection is expected to be carried
/// by the opcode, or non-type flags. For example the distinction between G_ADD
/// and G_FADD for int/float or fast-math flags.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_LOWLEVELTYPEIMPL_H
#define LLVM_SUPPORT_LOWLEVELTYPEIMPL_H

#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/Support/MachineValueType.h"
#include <cassert>

namespace llvm {

class DataLayout;
class Type;
class raw_ostream;

class LLT {
public:
  /// Get a low-level scalar or aggregate "bag of bits".
  static LLT scalar(unsigned SizeInBits) {
    assert(SizeInBits > 0 && "invalid scalar size");
    return LLT{/*isPointer=*/false, /*isVector=*/false, /*NumElements=*/0,
               SizeInBits, /*AddressSpace=*/0};
  }

  /// Get a low-level pointer in the given address space (defaulting to 0).
  static LLT pointer(uint16_t AddressSpace, unsigned SizeInBits) {
    assert(SizeInBits > 0 && "invalid pointer size");
    return LLT{/*isPointer=*/true, /*isVector=*/false, /*NumElements=*/0,
               SizeInBits, AddressSpace};
  }

  /// Get a low-level vector of some number of elements and element width.
  /// \p NumElements must be at least 2.
  static LLT vector(uint16_t NumElements, unsigned ScalarSizeInBits) {
    assert(NumElements > 1 && "invalid number of vector elements");
    assert(ScalarSizeInBits > 0 && "invalid vector element size");
    return LLT{/*isPointer=*/false, /*isVector=*/true, NumElements,
               ScalarSizeInBits, /*AddressSpace=*/0};
  }

  /// Get a low-level vector of some number of elements and element type.
  static LLT vector(uint16_t NumElements, LLT ScalarTy) {
    assert(NumElements > 1 && "invalid number of vector elements");
    assert(!ScalarTy.isVector() && "invalid vector element type");
    return LLT{ScalarTy.isPointer(), /*isVector=*/true, NumElements,
               ScalarTy.getSizeInBits(),
               ScalarTy.isPointer() ? ScalarTy.getAddressSpace() : 0};
  }

  explicit LLT(bool isPointer, bool isVector, uint16_t NumElements,
               unsigned SizeInBits, unsigned AddressSpace) {
    init(isPointer, isVector, NumElements, SizeInBits, AddressSpace);
  }
  explicit LLT() : IsPointer(false), IsVector(false), RawData(0) {}

  explicit LLT(MVT VT);

  bool isValid() const { return RawData != 0; }

  bool isScalar() const { return isValid() && !IsPointer && !IsVector; }

  bool isPointer() const { return isValid() && IsPointer && !IsVector; }

  bool isVector() const { return isValid() && IsVector; }

  /// Returns the number of elements in a vector LLT. Must only be called on
  /// vector types.
  uint16_t getNumElements() const {
    assert(IsVector && "cannot get number of elements on scalar/aggregate");
    if (!IsPointer)
      return getFieldValue(VectorElementsFieldInfo);
    else
      return getFieldValue(PointerVectorElementsFieldInfo);
  }

  /// Returns the total size of the type. Must only be called on sized types.
  unsigned getSizeInBits() const {
    if (isPointer() || isScalar())
      return getScalarSizeInBits();
    return getScalarSizeInBits() * getNumElements();
  }

  unsigned getScalarSizeInBits() const {
    assert(RawData != 0 && "Invalid Type");
    if (!IsVector) {
      if (!IsPointer)
        return getFieldValue(ScalarSizeFieldInfo);
      else
        return getFieldValue(PointerSizeFieldInfo);
    } else {
      if (!IsPointer)
        return getFieldValue(VectorSizeFieldInfo);
      else
        return getFieldValue(PointerVectorSizeFieldInfo);
    }
  }

  unsigned getAddressSpace() const {
    assert(RawData != 0 && "Invalid Type");
    assert(IsPointer && "cannot get address space of non-pointer type");
    if (!IsVector)
      return getFieldValue(PointerAddressSpaceFieldInfo);
    else
      return getFieldValue(PointerVectorAddressSpaceFieldInfo);
  }

  /// Returns the vector's element type. Only valid for vector types.
  LLT getElementType() const {
    assert(isVector() && "cannot get element type of scalar/aggregate");
    if (IsPointer)
      return pointer(getAddressSpace(), getScalarSizeInBits());
    else
      return scalar(getScalarSizeInBits());
  }

  void print(raw_ostream &OS) const;

  bool operator==(const LLT &RHS) const {
    return IsPointer == RHS.IsPointer && IsVector == RHS.IsVector &&
           RHS.RawData == RawData;
  }

  bool operator!=(const LLT &RHS) const { return !(*this == RHS); }

  friend struct DenseMapInfo<LLT>;
  friend class GISelInstProfileBuilder;

private:
  /// LLT is packed into 64 bits as follows:
  /// isPointer : 1
  /// isVector  : 1
  /// with 62 bits remaining for Kind-specific data, packed in bitfields
  /// as described below. As there isn't a simple portable way to pack bits
  /// into bitfields, here the different fields in the packed structure is
  /// described in static const *Field variables. Each of these variables
  /// is a 2-element array, with the first element describing the bitfield size
  /// and the second element describing the bitfield offset.
  typedef int BitFieldInfo[2];
  ///
  /// This is how the bitfields are packed per Kind:
  /// * Invalid:
  ///   gets encoded as RawData == 0, as that is an invalid encoding, since for
  ///   valid encodings, SizeInBits/SizeOfElement must be larger than 0.
  /// * Non-pointer scalar (isPointer == 0 && isVector == 0):
  ///   SizeInBits: 32;
  static const constexpr BitFieldInfo ScalarSizeFieldInfo{32, 0};
  /// * Pointer (isPointer == 1 && isVector == 0):
  ///   SizeInBits: 16;
  ///   AddressSpace: 23;
  static const constexpr BitFieldInfo PointerSizeFieldInfo{16, 0};
  static const constexpr BitFieldInfo PointerAddressSpaceFieldInfo{
      23, PointerSizeFieldInfo[0] + PointerSizeFieldInfo[1]};
  /// * Vector-of-non-pointer (isPointer == 0 && isVector == 1):
  ///   NumElements: 16;
  ///   SizeOfElement: 32;
  static const constexpr BitFieldInfo VectorElementsFieldInfo{16, 0};
  static const constexpr BitFieldInfo VectorSizeFieldInfo{
      32, VectorElementsFieldInfo[0] + VectorElementsFieldInfo[1]};
  /// * Vector-of-pointer (isPointer == 1 && isVector == 1):
  ///   NumElements: 16;
  ///   SizeOfElement: 16;
  ///   AddressSpace: 23;
  static const constexpr BitFieldInfo PointerVectorElementsFieldInfo{16, 0};
  static const constexpr BitFieldInfo PointerVectorSizeFieldInfo{
      16,
      PointerVectorElementsFieldInfo[1] + PointerVectorElementsFieldInfo[0]};
  static const constexpr BitFieldInfo PointerVectorAddressSpaceFieldInfo{
      23, PointerVectorSizeFieldInfo[1] + PointerVectorSizeFieldInfo[0]};

  uint64_t IsPointer : 1;
  uint64_t IsVector : 1;
  uint64_t RawData : 62;

  static uint64_t getMask(const BitFieldInfo FieldInfo) {
    const int FieldSizeInBits = FieldInfo[0];
    return (((uint64_t)1) << FieldSizeInBits) - 1;
  }
  static uint64_t maskAndShift(uint64_t Val, uint64_t Mask, uint8_t Shift) {
    assert(Val <= Mask && "Value too large for field");
    return (Val & Mask) << Shift;
  }
  static uint64_t maskAndShift(uint64_t Val, const BitFieldInfo FieldInfo) {
    return maskAndShift(Val, getMask(FieldInfo), FieldInfo[1]);
  }
  uint64_t getFieldValue(const BitFieldInfo FieldInfo) const {
    return getMask(FieldInfo) & (RawData >> FieldInfo[1]);
  }

  void init(bool IsPointer, bool IsVector, uint16_t NumElements,
            unsigned SizeInBits, unsigned AddressSpace) {
    this->IsPointer = IsPointer;
    this->IsVector = IsVector;
    if (!IsVector) {
      if (!IsPointer)
        RawData = maskAndShift(SizeInBits, ScalarSizeFieldInfo);
      else
        RawData = maskAndShift(SizeInBits, PointerSizeFieldInfo) |
                  maskAndShift(AddressSpace, PointerAddressSpaceFieldInfo);
    } else {
      assert(NumElements > 1 && "invalid number of vector elements");
      if (!IsPointer)
        RawData = maskAndShift(NumElements, VectorElementsFieldInfo) |
                  maskAndShift(SizeInBits, VectorSizeFieldInfo);
      else
        RawData =
            maskAndShift(NumElements, PointerVectorElementsFieldInfo) |
            maskAndShift(SizeInBits, PointerVectorSizeFieldInfo) |
            maskAndShift(AddressSpace, PointerVectorAddressSpaceFieldInfo);
    }
  }

  uint64_t getUniqueRAWLLTData() const {
    return ((uint64_t)RawData) << 2 | ((uint64_t)IsPointer) << 1 |
           ((uint64_t)IsVector);
  }
};

inline raw_ostream& operator<<(raw_ostream &OS, const LLT &Ty) {
  Ty.print(OS);
  return OS;
}

template<> struct DenseMapInfo<LLT> {
  static inline LLT getEmptyKey() {
    LLT Invalid;
    Invalid.IsPointer = true;
    return Invalid;
  }
  static inline LLT getTombstoneKey() {
    LLT Invalid;
    Invalid.IsVector = true;
    return Invalid;
  }
  static inline unsigned getHashValue(const LLT &Ty) {
    uint64_t Val = Ty.getUniqueRAWLLTData();
    return DenseMapInfo<uint64_t>::getHashValue(Val);
  }
  static bool isEqual(const LLT &LHS, const LLT &RHS) {
    return LHS == RHS;
  }
};

}

#endif // LLVM_SUPPORT_LOWLEVELTYPEIMPL_H
