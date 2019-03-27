//=- ARMRegisterBank.td - Describe the AArch64 Banks ---------*- tablegen -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

def GPRRegBank : RegisterBank<"GPRB", [GPR, GPRwithAPSR]>;
def FPRRegBank : RegisterBank<"FPRB", [HPR, SPR, DPR, QPR]>;
