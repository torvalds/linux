//=- AMDGPURegisterBank.td - Describe the AMDGPU Banks -------*- tablegen -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

def SGPRRegBank : RegisterBank<"SGPR",
  [SReg_32, SReg_64, SReg_128, SReg_256, SReg_512]
>;

def VGPRRegBank : RegisterBank<"VGPR",
  [VGPR_32, VReg_64, VReg_96, VReg_128, VReg_256, VReg_512]
>;

def SCCRegBank : RegisterBank <"SCC", [SCC_CLASS]>;

// It is helpful to distinguish conditions from ordinary SGPRs.
def VCCRegBank : RegisterBank <"VCC", [SReg_64]>;
