//===-- SystemZCallingConv.cpp - Calling conventions for SystemZ ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "SystemZCallingConv.h"
#include "SystemZRegisterInfo.h"

using namespace llvm;

const MCPhysReg SystemZ::ArgGPRs[SystemZ::NumArgGPRs] = {
  SystemZ::R2D, SystemZ::R3D, SystemZ::R4D, SystemZ::R5D, SystemZ::R6D
};

const MCPhysReg SystemZ::ArgFPRs[SystemZ::NumArgFPRs] = {
  SystemZ::F0D, SystemZ::F2D, SystemZ::F4D, SystemZ::F6D
};
