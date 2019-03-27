//=- X86RegisterBank.td - Describe the AArch64 Banks -----*- tablegen -*-=//
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

/// General Purpose Registers: RAX, RCX,...
def GPRRegBank : RegisterBank<"GPR", [GR64]>;

/// Floating Point/Vector Registers
def VECRRegBank : RegisterBank<"VECR", [VR512]>;
