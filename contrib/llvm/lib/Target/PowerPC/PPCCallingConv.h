//=== PPCCallingConv.h - PPC Custom Calling Convention Routines -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the custom routines for the PPC Calling Convention that
// aren't done by tablegen.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_PPC_PPCCALLINGCONV_H
#define LLVM_LIB_TARGET_PPC_PPCCALLINGCONV_H

#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/IR/CallingConv.h"

namespace llvm {

inline bool CC_PPC_AnyReg_Error(unsigned &, MVT &, MVT &,
                                CCValAssign::LocInfo &, ISD::ArgFlagsTy &,
                                CCState &) {
  llvm_unreachable("The AnyReg calling convention is only supported by the " \
                   "stackmap and patchpoint intrinsics.");
  // gracefully fallback to PPC C calling convention on Release builds.
  return false;
}

} // End llvm namespace

#endif

