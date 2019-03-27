//===-- ARMBaseInfo.cpp - ARM Base encoding information------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file provides basic encoding and assembly information for ARM.
//
//===----------------------------------------------------------------------===//
#include "ARMBaseInfo.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"

using namespace llvm;
namespace llvm {
namespace ARMSysReg {

// lookup system register using 12-bit SYSm value.
// Note: the search is uniqued using M1 mask
const MClassSysReg *lookupMClassSysRegBy12bitSYSmValue(unsigned SYSm) {
  return lookupMClassSysRegByM1Encoding12(SYSm);
}

// returns APSR with _<bits> qualifier.
// Note: ARMv7-M deprecates using MSR APSR without a _<bits> qualifier
const MClassSysReg *lookupMClassSysRegAPSRNonDeprecated(unsigned SYSm) {
  return lookupMClassSysRegByM2M3Encoding8((1<<9)|(SYSm & 0xFF));
}

// lookup system registers using 8-bit SYSm value
const MClassSysReg *lookupMClassSysRegBy8bitSYSmValue(unsigned SYSm) {
  return ARMSysReg::lookupMClassSysRegByM2M3Encoding8((1<<8)|(SYSm & 0xFF));
}

#define GET_MCLASSSYSREG_IMPL
#include "ARMGenSystemRegister.inc"

} // end namespace ARMSysReg

namespace ARMBankedReg {
#define GET_BANKEDREG_IMPL
#include "ARMGenSystemRegister.inc"
} // end namespce ARMSysReg
} // end namespace llvm
