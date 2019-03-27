//===-- R600MachineFunctionInfo.h - R600 Machine Function Info ----*- C++ -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPU_R600MACHINEFUNCTIONINFO_H
#define LLVM_LIB_TARGET_AMDGPU_R600MACHINEFUNCTIONINFO_H

#include "AMDGPUMachineFunction.h"

namespace llvm {

class R600MachineFunctionInfo final : public AMDGPUMachineFunction {
public:
  R600MachineFunctionInfo(const MachineFunction &MF);
  unsigned CFStackSize;
};

} // End llvm namespace

#endif
