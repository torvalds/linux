//===- CodeGen/ValueTypes.h - Low-Level Target independ. types --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the set of low-level target independent types which various
// values in the code generator are.  This allows the target specific behavior
// of instructions to be described to target independent passes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_VALUETYPES_H
#define LLVM_CODEGEN_VALUETYPES_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/MachineValueType.h"
#include "llvm/Support/MathExtras.h"
#include <cassert>
#include <cstdint>
#include <string>

namespace llvm {

  class LLVMContext;
  class Type;

  /// Extended Value Type. Capable of holding value types which are not native
  /// for any processor (such as the i12345 type), as well as the types an MVT
  /// can represent.
  struct EVT {
  private:
    MVT V = MVT::INVALID_SIMPLE_VALUE_TYPE;
    Type *LLVMTy = nullptr;

  public:
    constexpr EVT() = default;
    constexpr EVT(MVT::SimpleValueType SVT) : V(SVT) {}
    constexpr EVT(MVT S) : V(S) {}

    bool operator==(EVT VT) const {
      return !(*this != VT);
    }
    bool operator!=(EVT VT) const {
      if (V.SimpleTy != VT.V.SimpleTy)
        return true;
      if (V.SimpleTy == MVT::INVALID_SIMPLE_VALUE_TYPE)
        return LLVMTy != VT.LLVMTy;
      return false;
    }

    /// Returns the EVT that represents a floating-point type with the given
    /// number of bits. There are two floating-point types with 128 bits - this
    /// returns f128 rather than ppcf128.
    static EVT getFloatingPointVT(unsigned BitWidth) {
      return MVT::getFloatingPointVT(BitWidth);
    }

    /// Returns the EVT that represents an integer with the given number of
    /// bits.
    static EVT getIntegerVT(LLVMContext &Context, unsigned BitWidth) {
      MVT M = MVT::getIntegerVT(BitWidth);
      if (M.SimpleTy != MVT::INVALID_SIMPLE_VALUE_TYPE)
        return M;
      return getExtendedIntegerVT(Context, BitWidth);
    }

    /// Returns the EVT that represents a vector NumElements in length, where
    /// each element is of type VT.
    static EVT getVectorVT(LLVMContext &Context, EVT VT, unsigned NumElements,
                           bool IsScalable = false) {
      MVT M = MVT::getVectorVT(VT.V, NumElements, IsScalable);
      if (M.SimpleTy != MVT::INVALID_SIMPLE_VALUE_TYPE)
        return M;

      assert(!IsScalable && "We don't support extended scalable types yet");
      return getExtendedVectorVT(Context, VT, NumElements);
    }

    /// Returns the EVT that represents a vector EC.Min elements in length,
    /// where each element is of type VT.
    static EVT getVectorVT(LLVMContext &Context, EVT VT, MVT::ElementCount EC) {
      MVT M = MVT::getVectorVT(VT.V, EC);
      if (M.SimpleTy != MVT::INVALID_SIMPLE_VALUE_TYPE)
        return M;
      assert (!EC.Scalable && "We don't support extended scalable types yet");
      return getExtendedVectorVT(Context, VT, EC.Min);
    }

    /// Return a vector with the same number of elements as this vector, but
    /// with the element type converted to an integer type with the same
    /// bitwidth.
    EVT changeVectorElementTypeToInteger() const {
      if (!isSimple()) {
        assert (!isScalableVector() &&
                "We don't support extended scalable types yet");
        return changeExtendedVectorElementTypeToInteger();
      }
      MVT EltTy = getSimpleVT().getVectorElementType();
      unsigned BitWidth = EltTy.getSizeInBits();
      MVT IntTy = MVT::getIntegerVT(BitWidth);
      MVT VecTy = MVT::getVectorVT(IntTy, getVectorNumElements(),
                                   isScalableVector());
      assert(VecTy.SimpleTy != MVT::INVALID_SIMPLE_VALUE_TYPE &&
             "Simple vector VT not representable by simple integer vector VT!");
      return VecTy;
    }

    /// Return the type converted to an equivalently sized integer or vector
    /// with integer element type. Similar to changeVectorElementTypeToInteger,
    /// but also handles scalars.
    EVT changeTypeToInteger() {
      if (isVector())
        return changeVectorElementTypeToInteger();

      if (isSimple())
        return MVT::getIntegerVT(getSizeInBits());

      return changeExtendedTypeToInteger();
    }

    /// Test if the given EVT is simple (as opposed to being extended).
    bool isSimple() const {
      return V.SimpleTy != MVT::INVALID_SIMPLE_VALUE_TYPE;
    }

    /// Test if the given EVT is extended (as opposed to being simple).
    bool isExtended() const {
      return !isSimple();
    }

    /// Return true if this is a FP or a vector FP type.
    bool isFloatingPoint() const {
      return isSimple() ? V.isFloatingPoint() : isExtendedFloatingPoint();
    }

    /// Return true if this is an integer or a vector integer type.
    bool isInteger() const {
      return isSimple() ? V.isInteger() : isExtendedInteger();
    }

    /// Return true if this is an integer, but not a vector.
    bool isScalarInteger() const {
      return isSimple() ? V.isScalarInteger() : isExtendedScalarInteger();
    }

    /// Return true if this is a vector value type.
    bool isVector() const {
      return isSimple() ? V.isVector() : isExtendedVector();
    }

    /// Return true if this is a vector type where the runtime
    /// length is machine dependent
    bool isScalableVector() const {
      // FIXME: We don't support extended scalable types yet, because the
      // matching IR type doesn't exist. Once it has been added, this can
      // be changed to call isExtendedScalableVector.
      if (!isSimple())
        return false;
      return V.isScalableVector();
    }

    /// Return true if this is a 16-bit vector type.
    bool is16BitVector() const {
      return isSimple() ? V.is16BitVector() : isExtended16BitVector();
    }

    /// Return true if this is a 32-bit vector type.
    bool is32BitVector() const {
      return isSimple() ? V.is32BitVector() : isExtended32BitVector();
    }

    /// Return true if this is a 64-bit vector type.
    bool is64BitVector() const {
      return isSimple() ? V.is64BitVector() : isExtended64BitVector();
    }

    /// Return true if this is a 128-bit vector type.
    bool is128BitVector() const {
      return isSimple() ? V.is128BitVector() : isExtended128BitVector();
    }

    /// Return true if this is a 256-bit vector type.
    bool is256BitVector() const {
      return isSimple() ? V.is256BitVector() : isExtended256BitVector();
    }

    /// Return true if this is a 512-bit vector type.
    bool is512BitVector() const {
      return isSimple() ? V.is512BitVector() : isExtended512BitVector();
    }

    /// Return true if this is a 1024-bit vector type.
    bool is1024BitVector() const {
      return isSimple() ? V.is1024BitVector() : isExtended1024BitVector();
    }

    /// Return true if this is a 2048-bit vector type.
    bool is2048BitVector() const {
      return isSimple() ? V.is2048BitVector() : isExtended2048BitVector();
    }

    /// Return true if this is an overloaded type for TableGen.
    bool isOverloaded() const {
      return (V==MVT::iAny || V==MVT::fAny || V==MVT::vAny || V==MVT::iPTRAny);
    }

    /// Return true if the bit size is a multiple of 8.
    bool isByteSized() const {
      return (getSizeInBits() & 7) == 0;
    }

    /// Return true if the size is a power-of-two number of bytes.
    bool isRound() const {
      unsigned BitSize = getSizeInBits();
      return BitSize >= 8 && !(BitSize & (BitSize - 1));
    }

    /// Return true if this has the same number of bits as VT.
    bool bitsEq(EVT VT) const {
      if (EVT::operator==(VT)) return true;
      return getSizeInBits() == VT.getSizeInBits();
    }

    /// Return true if this has more bits than VT.
    bool bitsGT(EVT VT) const {
      if (EVT::operator==(VT)) return false;
      return getSizeInBits() > VT.getSizeInBits();
    }

    /// Return true if this has no less bits than VT.
    bool bitsGE(EVT VT) const {
      if (EVT::operator==(VT)) return true;
      return getSizeInBits() >= VT.getSizeInBits();
    }

    /// Return true if this has less bits than VT.
    bool bitsLT(EVT VT) const {
      if (EVT::operator==(VT)) return false;
      return getSizeInBits() < VT.getSizeInBits();
    }

    /// Return true if this has no more bits than VT.
    bool bitsLE(EVT VT) const {
      if (EVT::operator==(VT)) return true;
      return getSizeInBits() <= VT.getSizeInBits();
    }

    /// Return the SimpleValueType held in the specified simple EVT.
    MVT getSimpleVT() const {
      assert(isSimple() && "Expected a SimpleValueType!");
      return V;
    }

    /// If this is a vector type, return the element type, otherwise return
    /// this.
    EVT getScalarType() const {
      return isVector() ? getVectorElementType() : *this;
    }

    /// Given a vector type, return the type of each element.
    EVT getVectorElementType() const {
      assert(isVector() && "Invalid vector type!");
      if (isSimple())
        return V.getVectorElementType();
      return getExtendedVectorElementType();
    }

    /// Given a vector type, return the number of elements it contains.
    unsigned getVectorNumElements() const {
      assert(isVector() && "Invalid vector type!");
      if (isSimple())
        return V.getVectorNumElements();
      return getExtendedVectorNumElements();
    }

    // Given a (possibly scalable) vector type, return the ElementCount
    MVT::ElementCount getVectorElementCount() const {
      assert((isVector()) && "Invalid vector type!");
      if (isSimple())
        return V.getVectorElementCount();

      assert(!isScalableVector() &&
             "We don't support extended scalable types yet");
      return {getExtendedVectorNumElements(), false};
    }

    /// Return the size of the specified value type in bits.
    unsigned getSizeInBits() const {
      if (isSimple())
        return V.getSizeInBits();
      return getExtendedSizeInBits();
    }

    unsigned getScalarSizeInBits() const {
      return getScalarType().getSizeInBits();
    }

    /// Return the number of bytes overwritten by a store of the specified value
    /// type.
    unsigned getStoreSize() const {
      return (getSizeInBits() + 7) / 8;
    }

    /// Return the number of bits overwritten by a store of the specified value
    /// type.
    unsigned getStoreSizeInBits() const {
      return getStoreSize() * 8;
    }

    /// Rounds the bit-width of the given integer EVT up to the nearest power of
    /// two (and at least to eight), and returns the integer EVT with that
    /// number of bits.
    EVT getRoundIntegerType(LLVMContext &Context) const {
      assert(isInteger() && !isVector() && "Invalid integer type!");
      unsigned BitWidth = getSizeInBits();
      if (BitWidth <= 8)
        return EVT(MVT::i8);
      return getIntegerVT(Context, 1 << Log2_32_Ceil(BitWidth));
    }

    /// Finds the smallest simple value type that is greater than or equal to
    /// half the width of this EVT. If no simple value type can be found, an
    /// extended integer value type of half the size (rounded up) is returned.
    EVT getHalfSizedIntegerVT(LLVMContext &Context) const {
      assert(isInteger() && !isVector() && "Invalid integer type!");
      unsigned EVTSize = getSizeInBits();
      for (unsigned IntVT = MVT::FIRST_INTEGER_VALUETYPE;
          IntVT <= MVT::LAST_INTEGER_VALUETYPE; ++IntVT) {
        EVT HalfVT = EVT((MVT::SimpleValueType)IntVT);
        if (HalfVT.getSizeInBits() * 2 >= EVTSize)
          return HalfVT;
      }
      return getIntegerVT(Context, (EVTSize + 1) / 2);
    }

    /// Return a VT for an integer vector type with the size of the
    /// elements doubled. The typed returned may be an extended type.
    EVT widenIntegerVectorElementType(LLVMContext &Context) const {
      EVT EltVT = getVectorElementType();
      EltVT = EVT::getIntegerVT(Context, 2 * EltVT.getSizeInBits());
      return EVT::getVectorVT(Context, EltVT, getVectorElementCount());
    }

    // Return a VT for a vector type with the same element type but
    // half the number of elements. The type returned may be an
    // extended type.
    EVT getHalfNumVectorElementsVT(LLVMContext &Context) const {
      EVT EltVT = getVectorElementType();
      auto EltCnt = getVectorElementCount();
      assert(!(EltCnt.Min & 1) && "Splitting vector, but not in half!");
      return EVT::getVectorVT(Context, EltVT, EltCnt / 2);
    }

    /// Returns true if the given vector is a power of 2.
    bool isPow2VectorType() const {
      unsigned NElts = getVectorNumElements();
      return !(NElts & (NElts - 1));
    }

    /// Widens the length of the given vector EVT up to the nearest power of 2
    /// and returns that type.
    EVT getPow2VectorType(LLVMContext &Context) const {
      if (!isPow2VectorType()) {
        unsigned NElts = getVectorNumElements();
        unsigned Pow2NElts = 1 <<  Log2_32_Ceil(NElts);
        return EVT::getVectorVT(Context, getVectorElementType(), Pow2NElts,
                                isScalableVector());
      }
      else {
        return *this;
      }
    }

    /// This function returns value type as a string, e.g. "i32".
    std::string getEVTString() const;

    /// This method returns an LLVM type corresponding to the specified EVT.
    /// For integer types, this returns an unsigned type. Note that this will
    /// abort for types that cannot be represented.
    Type *getTypeForEVT(LLVMContext &Context) const;

    /// Return the value type corresponding to the specified type.
    /// This returns all pointers as iPTR.  If HandleUnknown is true, unknown
    /// types are returned as Other, otherwise they are invalid.
    static EVT getEVT(Type *Ty, bool HandleUnknown = false);

    intptr_t getRawBits() const {
      if (isSimple())
        return V.SimpleTy;
      else
        return (intptr_t)(LLVMTy);
    }

    /// A meaningless but well-behaved order, useful for constructing
    /// containers.
    struct compareRawBits {
      bool operator()(EVT L, EVT R) const {
        if (L.V.SimpleTy == R.V.SimpleTy)
          return L.LLVMTy < R.LLVMTy;
        else
          return L.V.SimpleTy < R.V.SimpleTy;
      }
    };

  private:
    // Methods for handling the Extended-type case in functions above.
    // These are all out-of-line to prevent users of this header file
    // from having a dependency on Type.h.
    EVT changeExtendedTypeToInteger() const;
    EVT changeExtendedVectorElementTypeToInteger() const;
    static EVT getExtendedIntegerVT(LLVMContext &C, unsigned BitWidth);
    static EVT getExtendedVectorVT(LLVMContext &C, EVT VT,
                                   unsigned NumElements);
    bool isExtendedFloatingPoint() const LLVM_READONLY;
    bool isExtendedInteger() const LLVM_READONLY;
    bool isExtendedScalarInteger() const LLVM_READONLY;
    bool isExtendedVector() const LLVM_READONLY;
    bool isExtended16BitVector() const LLVM_READONLY;
    bool isExtended32BitVector() const LLVM_READONLY;
    bool isExtended64BitVector() const LLVM_READONLY;
    bool isExtended128BitVector() const LLVM_READONLY;
    bool isExtended256BitVector() const LLVM_READONLY;
    bool isExtended512BitVector() const LLVM_READONLY;
    bool isExtended1024BitVector() const LLVM_READONLY;
    bool isExtended2048BitVector() const LLVM_READONLY;
    EVT getExtendedVectorElementType() const;
    unsigned getExtendedVectorNumElements() const LLVM_READONLY;
    unsigned getExtendedSizeInBits() const LLVM_READONLY;
  };

} // end namespace llvm

#endif // LLVM_CODEGEN_VALUETYPES_H
