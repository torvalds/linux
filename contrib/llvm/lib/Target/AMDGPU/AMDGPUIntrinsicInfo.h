//===- AMDGPUIntrinsicInfo.h - AMDGPU Intrinsic Information ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//==-----------------------------------------------------------------------===//
//
/// \file
/// Interface for the AMDGPU Implementation of the Intrinsic Info class.
//
//===-----------------------------------------------------------------------===//
#ifndef LLVM_LIB_TARGET_AMDGPU_AMDGPUINTRINSICINFO_H
#define LLVM_LIB_TARGET_AMDGPU_AMDGPUINTRINSICINFO_H

#include "llvm/IR/Intrinsics.h"
#include "llvm/Target/TargetIntrinsicInfo.h"

namespace llvm {
class TargetMachine;

namespace SIIntrinsic {
enum ID {
  last_non_AMDGPU_intrinsic = Intrinsic::num_intrinsics - 1,
#define GET_INTRINSIC_ENUM_VALUES
#include "AMDGPUGenIntrinsicEnums.inc"
#undef GET_INTRINSIC_ENUM_VALUES
      , num_AMDGPU_intrinsics
};

} // end namespace AMDGPUIntrinsic

class AMDGPUIntrinsicInfo final : public TargetIntrinsicInfo {
public:
  AMDGPUIntrinsicInfo();

  StringRef getName(unsigned IntrId, ArrayRef<Type *> Tys = None) const;

  std::string getName(unsigned IntrId, Type **Tys = nullptr,
                      unsigned NumTys = 0) const override;

  unsigned lookupName(const char *Name, unsigned Len) const override;
  bool isOverloaded(unsigned IID) const override;
  Function *getDeclaration(Module *M, unsigned ID,
                           Type **Tys = nullptr,
                           unsigned NumTys = 0) const override;

  Function *getDeclaration(Module *M, unsigned ID,
                           ArrayRef<Type *> = None) const;

  FunctionType *getType(LLVMContext &Context, unsigned ID,
                        ArrayRef<Type*> Tys = None) const;
};

} // end namespace llvm

#endif
