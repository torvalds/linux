//===----------- ValueTypes.cpp - Implementation of EVT methods -----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/ErrorHandling.h"
using namespace llvm;

EVT EVT::changeExtendedTypeToInteger() const {
  LLVMContext &Context = LLVMTy->getContext();
  return getIntegerVT(Context, getSizeInBits());
}

EVT EVT::changeExtendedVectorElementTypeToInteger() const {
  LLVMContext &Context = LLVMTy->getContext();
  EVT IntTy = getIntegerVT(Context, getScalarSizeInBits());
  return getVectorVT(Context, IntTy, getVectorNumElements());
}

EVT EVT::getExtendedIntegerVT(LLVMContext &Context, unsigned BitWidth) {
  EVT VT;
  VT.LLVMTy = IntegerType::get(Context, BitWidth);
  assert(VT.isExtended() && "Type is not extended!");
  return VT;
}

EVT EVT::getExtendedVectorVT(LLVMContext &Context, EVT VT,
                             unsigned NumElements) {
  EVT ResultVT;
  ResultVT.LLVMTy = VectorType::get(VT.getTypeForEVT(Context), NumElements);
  assert(ResultVT.isExtended() && "Type is not extended!");
  return ResultVT;
}

bool EVT::isExtendedFloatingPoint() const {
  assert(isExtended() && "Type is not extended!");
  return LLVMTy->isFPOrFPVectorTy();
}

bool EVT::isExtendedInteger() const {
  assert(isExtended() && "Type is not extended!");
  return LLVMTy->isIntOrIntVectorTy();
}

bool EVT::isExtendedScalarInteger() const {
  assert(isExtended() && "Type is not extended!");
  return LLVMTy->isIntegerTy();
}

bool EVT::isExtendedVector() const {
  assert(isExtended() && "Type is not extended!");
  return LLVMTy->isVectorTy();
}

bool EVT::isExtended16BitVector() const {
  return isExtendedVector() && getExtendedSizeInBits() == 16;
}

bool EVT::isExtended32BitVector() const {
  return isExtendedVector() && getExtendedSizeInBits() == 32;
}

bool EVT::isExtended64BitVector() const {
  return isExtendedVector() && getExtendedSizeInBits() == 64;
}

bool EVT::isExtended128BitVector() const {
  return isExtendedVector() && getExtendedSizeInBits() == 128;
}

bool EVT::isExtended256BitVector() const {
  return isExtendedVector() && getExtendedSizeInBits() == 256;
}

bool EVT::isExtended512BitVector() const {
  return isExtendedVector() && getExtendedSizeInBits() == 512;
}

bool EVT::isExtended1024BitVector() const {
  return isExtendedVector() && getExtendedSizeInBits() == 1024;
}

bool EVT::isExtended2048BitVector() const {
  return isExtendedVector() && getExtendedSizeInBits() == 2048;
}

EVT EVT::getExtendedVectorElementType() const {
  assert(isExtended() && "Type is not extended!");
  return EVT::getEVT(cast<VectorType>(LLVMTy)->getElementType());
}

unsigned EVT::getExtendedVectorNumElements() const {
  assert(isExtended() && "Type is not extended!");
  return cast<VectorType>(LLVMTy)->getNumElements();
}

unsigned EVT::getExtendedSizeInBits() const {
  assert(isExtended() && "Type is not extended!");
  if (IntegerType *ITy = dyn_cast<IntegerType>(LLVMTy))
    return ITy->getBitWidth();
  if (VectorType *VTy = dyn_cast<VectorType>(LLVMTy))
    return VTy->getBitWidth();
  llvm_unreachable("Unrecognized extended type!");
}

/// getEVTString - This function returns value type as a string, e.g. "i32".
std::string EVT::getEVTString() const {
  switch (V.SimpleTy) {
  default:
    if (isVector())
      return "v" + utostr(getVectorNumElements()) +
             getVectorElementType().getEVTString();
    if (isInteger())
      return "i" + utostr(getSizeInBits());
    llvm_unreachable("Invalid EVT!");
  case MVT::i1:      return "i1";
  case MVT::i8:      return "i8";
  case MVT::i16:     return "i16";
  case MVT::i32:     return "i32";
  case MVT::i64:     return "i64";
  case MVT::i128:    return "i128";
  case MVT::f16:     return "f16";
  case MVT::f32:     return "f32";
  case MVT::f64:     return "f64";
  case MVT::f80:     return "f80";
  case MVT::f128:    return "f128";
  case MVT::ppcf128: return "ppcf128";
  case MVT::isVoid:  return "isVoid";
  case MVT::Other:   return "ch";
  case MVT::Glue:    return "glue";
  case MVT::x86mmx:  return "x86mmx";
  case MVT::v1i1:    return "v1i1";
  case MVT::v2i1:    return "v2i1";
  case MVT::v4i1:    return "v4i1";
  case MVT::v8i1:    return "v8i1";
  case MVT::v16i1:   return "v16i1";
  case MVT::v32i1:   return "v32i1";
  case MVT::v64i1:   return "v64i1";
  case MVT::v128i1:  return "v128i1";
  case MVT::v512i1:  return "v512i1";
  case MVT::v1024i1: return "v1024i1";
  case MVT::v1i8:    return "v1i8";
  case MVT::v2i8:    return "v2i8";
  case MVT::v4i8:    return "v4i8";
  case MVT::v8i8:    return "v8i8";
  case MVT::v16i8:   return "v16i8";
  case MVT::v32i8:   return "v32i8";
  case MVT::v64i8:   return "v64i8";
  case MVT::v128i8:  return "v128i8";
  case MVT::v256i8:  return "v256i8";
  case MVT::v1i16:   return "v1i16";
  case MVT::v2i16:   return "v2i16";
  case MVT::v4i16:   return "v4i16";
  case MVT::v8i16:   return "v8i16";
  case MVT::v16i16:  return "v16i16";
  case MVT::v32i16:  return "v32i16";
  case MVT::v64i16:  return "v64i16";
  case MVT::v128i16: return "v128i16";
  case MVT::v1i32:   return "v1i32";
  case MVT::v2i32:   return "v2i32";
  case MVT::v4i32:   return "v4i32";
  case MVT::v8i32:   return "v8i32";
  case MVT::v16i32:  return "v16i32";
  case MVT::v32i32:  return "v32i32";
  case MVT::v64i32:  return "v64i32";
  case MVT::v1i64:   return "v1i64";
  case MVT::v2i64:   return "v2i64";
  case MVT::v4i64:   return "v4i64";
  case MVT::v8i64:   return "v8i64";
  case MVT::v16i64:  return "v16i64";
  case MVT::v32i64:  return "v32i64";
  case MVT::v1i128:  return "v1i128";
  case MVT::v1f32:   return "v1f32";
  case MVT::v2f32:   return "v2f32";
  case MVT::v2f16:   return "v2f16";
  case MVT::v4f16:   return "v4f16";
  case MVT::v8f16:   return "v8f16";
  case MVT::v4f32:   return "v4f32";
  case MVT::v8f32:   return "v8f32";
  case MVT::v16f32:  return "v16f32";
  case MVT::v1f64:   return "v1f64";
  case MVT::v2f64:   return "v2f64";
  case MVT::v4f64:   return "v4f64";
  case MVT::v8f64:   return "v8f64";
  case MVT::Metadata:return "Metadata";
  case MVT::Untyped: return "Untyped";
  case MVT::ExceptRef: return "ExceptRef";
  }
}

/// getTypeForEVT - This method returns an LLVM type corresponding to the
/// specified EVT.  For integer types, this returns an unsigned type.  Note
/// that this will abort for types that cannot be represented.
Type *EVT::getTypeForEVT(LLVMContext &Context) const {
  switch (V.SimpleTy) {
  default:
    assert(isExtended() && "Type is not extended!");
    return LLVMTy;
  case MVT::isVoid:  return Type::getVoidTy(Context);
  case MVT::i1:      return Type::getInt1Ty(Context);
  case MVT::i8:      return Type::getInt8Ty(Context);
  case MVT::i16:     return Type::getInt16Ty(Context);
  case MVT::i32:     return Type::getInt32Ty(Context);
  case MVT::i64:     return Type::getInt64Ty(Context);
  case MVT::i128:    return IntegerType::get(Context, 128);
  case MVT::f16:     return Type::getHalfTy(Context);
  case MVT::f32:     return Type::getFloatTy(Context);
  case MVT::f64:     return Type::getDoubleTy(Context);
  case MVT::f80:     return Type::getX86_FP80Ty(Context);
  case MVT::f128:    return Type::getFP128Ty(Context);
  case MVT::ppcf128: return Type::getPPC_FP128Ty(Context);
  case MVT::x86mmx:  return Type::getX86_MMXTy(Context);
  case MVT::v1i1:    return VectorType::get(Type::getInt1Ty(Context), 1);
  case MVT::v2i1:    return VectorType::get(Type::getInt1Ty(Context), 2);
  case MVT::v4i1:    return VectorType::get(Type::getInt1Ty(Context), 4);
  case MVT::v8i1:    return VectorType::get(Type::getInt1Ty(Context), 8);
  case MVT::v16i1:   return VectorType::get(Type::getInt1Ty(Context), 16);
  case MVT::v32i1:   return VectorType::get(Type::getInt1Ty(Context), 32);
  case MVT::v64i1:   return VectorType::get(Type::getInt1Ty(Context), 64);
  case MVT::v128i1:  return VectorType::get(Type::getInt1Ty(Context), 128);
  case MVT::v512i1:  return VectorType::get(Type::getInt1Ty(Context), 512);
  case MVT::v1024i1: return VectorType::get(Type::getInt1Ty(Context), 1024);
  case MVT::v1i8:    return VectorType::get(Type::getInt8Ty(Context), 1);
  case MVT::v2i8:    return VectorType::get(Type::getInt8Ty(Context), 2);
  case MVT::v4i8:    return VectorType::get(Type::getInt8Ty(Context), 4);
  case MVT::v8i8:    return VectorType::get(Type::getInt8Ty(Context), 8);
  case MVT::v16i8:   return VectorType::get(Type::getInt8Ty(Context), 16);
  case MVT::v32i8:   return VectorType::get(Type::getInt8Ty(Context), 32);
  case MVT::v64i8:   return VectorType::get(Type::getInt8Ty(Context), 64);
  case MVT::v128i8:  return VectorType::get(Type::getInt8Ty(Context), 128);
  case MVT::v256i8:  return VectorType::get(Type::getInt8Ty(Context), 256);
  case MVT::v1i16:   return VectorType::get(Type::getInt16Ty(Context), 1);
  case MVT::v2i16:   return VectorType::get(Type::getInt16Ty(Context), 2);
  case MVT::v4i16:   return VectorType::get(Type::getInt16Ty(Context), 4);
  case MVT::v8i16:   return VectorType::get(Type::getInt16Ty(Context), 8);
  case MVT::v16i16:  return VectorType::get(Type::getInt16Ty(Context), 16);
  case MVT::v32i16:  return VectorType::get(Type::getInt16Ty(Context), 32);
  case MVT::v64i16:  return VectorType::get(Type::getInt16Ty(Context), 64);
  case MVT::v128i16: return VectorType::get(Type::getInt16Ty(Context), 128);
  case MVT::v1i32:   return VectorType::get(Type::getInt32Ty(Context), 1);
  case MVT::v2i32:   return VectorType::get(Type::getInt32Ty(Context), 2);
  case MVT::v4i32:   return VectorType::get(Type::getInt32Ty(Context), 4);
  case MVT::v8i32:   return VectorType::get(Type::getInt32Ty(Context), 8);
  case MVT::v16i32:  return VectorType::get(Type::getInt32Ty(Context), 16);
  case MVT::v32i32:  return VectorType::get(Type::getInt32Ty(Context), 32);
  case MVT::v64i32:  return VectorType::get(Type::getInt32Ty(Context), 64);
  case MVT::v1i64:   return VectorType::get(Type::getInt64Ty(Context), 1);
  case MVT::v2i64:   return VectorType::get(Type::getInt64Ty(Context), 2);
  case MVT::v4i64:   return VectorType::get(Type::getInt64Ty(Context), 4);
  case MVT::v8i64:   return VectorType::get(Type::getInt64Ty(Context), 8);
  case MVT::v16i64:  return VectorType::get(Type::getInt64Ty(Context), 16);
  case MVT::v32i64:  return VectorType::get(Type::getInt64Ty(Context), 32);
  case MVT::v1i128:  return VectorType::get(Type::getInt128Ty(Context), 1);
  case MVT::v2f16:   return VectorType::get(Type::getHalfTy(Context), 2);
  case MVT::v4f16:   return VectorType::get(Type::getHalfTy(Context), 4);
  case MVT::v8f16:   return VectorType::get(Type::getHalfTy(Context), 8);
  case MVT::v1f32:   return VectorType::get(Type::getFloatTy(Context), 1);
  case MVT::v2f32:   return VectorType::get(Type::getFloatTy(Context), 2);
  case MVT::v4f32:   return VectorType::get(Type::getFloatTy(Context), 4);
  case MVT::v8f32:   return VectorType::get(Type::getFloatTy(Context), 8);
  case MVT::v16f32:   return VectorType::get(Type::getFloatTy(Context), 16);
  case MVT::v1f64:   return VectorType::get(Type::getDoubleTy(Context), 1);
  case MVT::v2f64:   return VectorType::get(Type::getDoubleTy(Context), 2);
  case MVT::v4f64:   return VectorType::get(Type::getDoubleTy(Context), 4);
  case MVT::v8f64:   return VectorType::get(Type::getDoubleTy(Context), 8);
  case MVT::Metadata: return Type::getMetadataTy(Context);
 }
}

/// Return the value type corresponding to the specified type.  This returns all
/// pointers as MVT::iPTR.  If HandleUnknown is true, unknown types are returned
/// as Other, otherwise they are invalid.
MVT MVT::getVT(Type *Ty, bool HandleUnknown){
  switch (Ty->getTypeID()) {
  default:
    if (HandleUnknown) return MVT(MVT::Other);
    llvm_unreachable("Unknown type!");
  case Type::VoidTyID:
    return MVT::isVoid;
  case Type::IntegerTyID:
    return getIntegerVT(cast<IntegerType>(Ty)->getBitWidth());
  case Type::HalfTyID:      return MVT(MVT::f16);
  case Type::FloatTyID:     return MVT(MVT::f32);
  case Type::DoubleTyID:    return MVT(MVT::f64);
  case Type::X86_FP80TyID:  return MVT(MVT::f80);
  case Type::X86_MMXTyID:   return MVT(MVT::x86mmx);
  case Type::FP128TyID:     return MVT(MVT::f128);
  case Type::PPC_FP128TyID: return MVT(MVT::ppcf128);
  case Type::PointerTyID:   return MVT(MVT::iPTR);
  case Type::VectorTyID: {
    VectorType *VTy = cast<VectorType>(Ty);
    return getVectorVT(
      getVT(VTy->getElementType(), false), VTy->getNumElements());
  }
  }
}

/// getEVT - Return the value type corresponding to the specified type.  This
/// returns all pointers as MVT::iPTR.  If HandleUnknown is true, unknown types
/// are returned as Other, otherwise they are invalid.
EVT EVT::getEVT(Type *Ty, bool HandleUnknown){
  switch (Ty->getTypeID()) {
  default:
    return MVT::getVT(Ty, HandleUnknown);
  case Type::IntegerTyID:
    return getIntegerVT(Ty->getContext(), cast<IntegerType>(Ty)->getBitWidth());
  case Type::VectorTyID: {
    VectorType *VTy = cast<VectorType>(Ty);
    return getVectorVT(Ty->getContext(), getEVT(VTy->getElementType(), false),
                       VTy->getNumElements());
  }
  }
}
