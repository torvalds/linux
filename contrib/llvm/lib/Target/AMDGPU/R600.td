//===-- R600.td - R600 Tablegen files ----------------------*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

include "llvm/Target/Target.td"

def R600InstrInfo : InstrInfo {
  let guessInstructionProperties = 1;
  let noNamedPositionallyEncodedOperands = 1;
}

def R600 : Target {
  let InstructionSet = R600InstrInfo;
  let AllowRegisterRenaming = 1;
}

let Namespace = "R600" in {

foreach Index = 0-15 in {
  def sub#Index : SubRegIndex<32, !shl(Index, 5)>;
}

include "R600RegisterInfo.td"

}

def NullALU : InstrItinClass;
def ALU_NULL : FuncUnit;

include "AMDGPUFeatures.td"
include "R600Schedule.td"
include "R600Processors.td"
include "AMDGPUInstrInfo.td"
include "AMDGPUInstructions.td"
include "R600Instructions.td"
include "R700Instructions.td"
include "EvergreenInstructions.td"
include "CaymanInstructions.td"

// Calling convention for R600
def CC_R600 : CallingConv<[
  CCIfInReg<CCIfType<[v4f32, v4i32] , CCAssignToReg<[
    T0_XYZW, T1_XYZW, T2_XYZW, T3_XYZW, T4_XYZW, T5_XYZW, T6_XYZW, T7_XYZW,
    T8_XYZW, T9_XYZW, T10_XYZW, T11_XYZW, T12_XYZW, T13_XYZW, T14_XYZW, T15_XYZW,
    T16_XYZW, T17_XYZW, T18_XYZW, T19_XYZW, T20_XYZW, T21_XYZW, T22_XYZW,
    T23_XYZW, T24_XYZW, T25_XYZW, T26_XYZW, T27_XYZW, T28_XYZW, T29_XYZW,
    T30_XYZW, T31_XYZW, T32_XYZW
  ]>>>
]>;
