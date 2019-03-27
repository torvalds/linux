//===-- AMDGPURegisterInfo.cpp - AMDGPU Register Information -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Parent TargetRegisterInfo class common to all hw codegen targets.
//
//===----------------------------------------------------------------------===//

#include "AMDGPURegisterInfo.h"
#include "AMDGPUTargetMachine.h"
#include "SIMachineFunctionInfo.h"
#include "SIRegisterInfo.h"
#include "MCTargetDesc/AMDGPUMCTargetDesc.h"

using namespace llvm;

AMDGPURegisterInfo::AMDGPURegisterInfo() : AMDGPUGenRegisterInfo(0) {}

//===----------------------------------------------------------------------===//
// Function handling callbacks - Functions are a seldom used feature of GPUS, so
// they are not supported at this time.
//===----------------------------------------------------------------------===//

unsigned AMDGPURegisterInfo::getSubRegFromChannel(unsigned Channel) {
  static const unsigned SubRegs[] = {
    AMDGPU::sub0, AMDGPU::sub1, AMDGPU::sub2, AMDGPU::sub3, AMDGPU::sub4,
    AMDGPU::sub5, AMDGPU::sub6, AMDGPU::sub7, AMDGPU::sub8, AMDGPU::sub9,
    AMDGPU::sub10, AMDGPU::sub11, AMDGPU::sub12, AMDGPU::sub13, AMDGPU::sub14,
    AMDGPU::sub15
  };

  assert(Channel < array_lengthof(SubRegs));
  return SubRegs[Channel];
}

void AMDGPURegisterInfo::reserveRegisterTuples(BitVector &Reserved, unsigned Reg) const {
  MCRegAliasIterator R(Reg, this, true);

  for (; R.isValid(); ++R)
    Reserved.set(*R);
}

#define GET_REGINFO_TARGET_DESC
#include "AMDGPUGenRegisterInfo.inc"

// Forced to be here by one .inc
const MCPhysReg *SIRegisterInfo::getCalleeSavedRegs(
  const MachineFunction *MF) const {
  CallingConv::ID CC = MF->getFunction().getCallingConv();
  switch (CC) {
  case CallingConv::C:
  case CallingConv::Fast:
  case CallingConv::Cold:
    return CSR_AMDGPU_HighRegs_SaveList;
  default: {
    // Dummy to not crash RegisterClassInfo.
    static const MCPhysReg NoCalleeSavedReg = AMDGPU::NoRegister;
    return &NoCalleeSavedReg;
  }
  }
}

const MCPhysReg *
SIRegisterInfo::getCalleeSavedRegsViaCopy(const MachineFunction *MF) const {
  return nullptr;
}

const uint32_t *SIRegisterInfo::getCallPreservedMask(const MachineFunction &MF,
                                                     CallingConv::ID CC) const {
  switch (CC) {
  case CallingConv::C:
  case CallingConv::Fast:
  case CallingConv::Cold:
    return CSR_AMDGPU_HighRegs_RegMask;
  default:
    return nullptr;
  }
}

unsigned SIRegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  const SIMachineFunctionInfo *FuncInfo = MF.getInfo<SIMachineFunctionInfo>();
  return FuncInfo->getFrameOffsetReg();
}
