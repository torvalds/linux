//===-- AMDGPURegisterInfo.td - AMDGPU register info -------*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Tablegen register definitions common to all hw codegen targets.
//
//===----------------------------------------------------------------------===//

let Namespace = "AMDGPU" in {

foreach Index = 0-15 in {
  def sub#Index : SubRegIndex<32, !shl(Index, 5)>;
}

}

include "SIRegisterInfo.td"
