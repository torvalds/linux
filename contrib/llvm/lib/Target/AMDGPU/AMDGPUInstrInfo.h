//===-- AMDGPUInstrInfo.h - AMDGPU Instruction Information ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Contains the definition of a TargetInstrInfo class that is common
/// to all AMD GPUs.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPU_AMDGPUINSTRINFO_H
#define LLVM_LIB_TARGET_AMDGPU_AMDGPUINSTRINFO_H

#include "AMDGPU.h"
#include "Utils/AMDGPUBaseInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"

namespace llvm {

class GCNSubtarget;
class MachineFunction;
class MachineInstr;
class MachineInstrBuilder;

class AMDGPUInstrInfo {
public:
  explicit AMDGPUInstrInfo(const GCNSubtarget &st);

  static bool isUniformMMO(const MachineMemOperand *MMO);
};

namespace AMDGPU {

struct RsrcIntrinsic {
  unsigned Intr;
  uint8_t RsrcArg;
  bool IsImage;
};
const RsrcIntrinsic *lookupRsrcIntrinsic(unsigned Intr);

struct D16ImageDimIntrinsic {
  unsigned Intr;
  unsigned D16HelperIntr;
};
const D16ImageDimIntrinsic *lookupD16ImageDimIntrinsic(unsigned Intr);

struct ImageDimIntrinsicInfo {
  unsigned Intr;
  unsigned BaseOpcode;
  MIMGDim Dim;
};
const ImageDimIntrinsicInfo *getImageDimIntrinsicInfo(unsigned Intr);

} // end AMDGPU namespace
} // End llvm namespace

#endif
