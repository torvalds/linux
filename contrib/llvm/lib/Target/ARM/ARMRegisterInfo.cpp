//===-- ARMRegisterInfo.cpp - ARM Register Information --------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the ARM implementation of the TargetRegisterInfo class.
//
//===----------------------------------------------------------------------===//

#include "ARMRegisterInfo.h"
using namespace llvm;

void ARMRegisterInfo::anchor() { }

ARMRegisterInfo::ARMRegisterInfo() : ARMBaseRegisterInfo() {}
