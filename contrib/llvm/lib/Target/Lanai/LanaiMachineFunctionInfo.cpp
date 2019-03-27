//===-- LanaiMachineFuctionInfo.cpp - Lanai machine function info ---===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "LanaiMachineFunctionInfo.h"

using namespace llvm;

void LanaiMachineFunctionInfo::anchor() {}

unsigned LanaiMachineFunctionInfo::getGlobalBaseReg() {
  // Return if it has already been initialized.
  if (GlobalBaseReg)
    return GlobalBaseReg;

  return GlobalBaseReg =
             MF.getRegInfo().createVirtualRegister(&Lanai::GPRRegClass);
}
