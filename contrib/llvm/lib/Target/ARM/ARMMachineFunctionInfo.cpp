//===-- ARMMachineFunctionInfo.cpp - ARM machine function info ------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "ARMMachineFunctionInfo.h"
#include "ARMSubtarget.h"

using namespace llvm;

void ARMFunctionInfo::anchor() {}

ARMFunctionInfo::ARMFunctionInfo(MachineFunction &MF)
    : isThumb(MF.getSubtarget<ARMSubtarget>().isThumb()),
      hasThumb2(MF.getSubtarget<ARMSubtarget>().hasThumb2()) {}
