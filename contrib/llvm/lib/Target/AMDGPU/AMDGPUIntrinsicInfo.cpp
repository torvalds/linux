//===- AMDGPUIntrinsicInfo.cpp - AMDGPU Intrinsic Information ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//==-----------------------------------------------------------------------===//
//
/// \file
/// AMDGPU Implementation of the IntrinsicInfo class.
//
//===-----------------------------------------------------------------------===//

#include "AMDGPUIntrinsicInfo.h"
#include "AMDGPUSubtarget.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Module.h"

using namespace llvm;

AMDGPUIntrinsicInfo::AMDGPUIntrinsicInfo()
    : TargetIntrinsicInfo() {}

static const char *const IntrinsicNameTable[] = {
#define GET_INTRINSIC_NAME_TABLE
#include "AMDGPUGenIntrinsicImpl.inc"
#undef GET_INTRINSIC_NAME_TABLE
};

namespace {
#define GET_INTRINSIC_ATTRIBUTES
#include "AMDGPUGenIntrinsicImpl.inc"
#undef GET_INTRINSIC_ATTRIBUTES
}

StringRef AMDGPUIntrinsicInfo::getName(unsigned IntrID,
                                       ArrayRef<Type *> Tys) const {
  if (IntrID < Intrinsic::num_intrinsics)
    return StringRef();

  assert(IntrID < SIIntrinsic::num_AMDGPU_intrinsics &&
         "Invalid intrinsic ID");

  return IntrinsicNameTable[IntrID - Intrinsic::num_intrinsics];
}

std::string AMDGPUIntrinsicInfo::getName(unsigned IntrID, Type **Tys,
                                         unsigned NumTys) const {
  return getName(IntrID, makeArrayRef(Tys, NumTys)).str();
}

FunctionType *AMDGPUIntrinsicInfo::getType(LLVMContext &Context, unsigned ID,
                                           ArrayRef<Type*> Tys) const {
  // FIXME: Re-use Intrinsic::getType machinery
  llvm_unreachable("unhandled intrinsic");
}

unsigned AMDGPUIntrinsicInfo::lookupName(const char *NameData,
                                         unsigned Len) const {
  StringRef Name(NameData, Len);
  if (!Name.startswith("llvm."))
    return 0; // All intrinsics start with 'llvm.'

  // Look for a name match in our table.  If the intrinsic is not overloaded,
  // require an exact match. If it is overloaded, require a prefix match. The
  // AMDGPU enum enum starts at Intrinsic::num_intrinsics.
  int Idx = Intrinsic::lookupLLVMIntrinsicByName(IntrinsicNameTable, Name);
  if (Idx >= 0) {
    bool IsPrefixMatch = Name.size() > strlen(IntrinsicNameTable[Idx]);
    return IsPrefixMatch == isOverloaded(Idx + 1)
               ? Intrinsic::num_intrinsics + Idx
               : 0;
  }

  return 0;
}

bool AMDGPUIntrinsicInfo::isOverloaded(unsigned id) const {
// Overload Table
#define GET_INTRINSIC_OVERLOAD_TABLE
#include "AMDGPUGenIntrinsicImpl.inc"
#undef GET_INTRINSIC_OVERLOAD_TABLE
}

Function *AMDGPUIntrinsicInfo::getDeclaration(Module *M, unsigned IntrID,
                                              ArrayRef<Type *> Tys) const {
  FunctionType *FTy = getType(M->getContext(), IntrID, Tys);
  Function *F
    = cast<Function>(M->getOrInsertFunction(getName(IntrID, Tys), FTy));

  AttributeList AS =
      getAttributes(M->getContext(), static_cast<SIIntrinsic::ID>(IntrID));
  F->setAttributes(AS);
  return F;
}

Function *AMDGPUIntrinsicInfo::getDeclaration(Module *M, unsigned IntrID,
                                              Type **Tys,
                                              unsigned NumTys) const {
  return getDeclaration(M, IntrID, makeArrayRef(Tys, NumTys));
}
