//===-- NVPTXRegisterInfo.td - NVPTX Register defs ---------*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//  Declarations that describe the PTX register file
//===----------------------------------------------------------------------===//

class NVPTXReg<string n> : Register<n> {
  let Namespace = "NVPTX";
}

class NVPTXRegClass<list<ValueType> regTypes, int alignment, dag regList>
     : RegisterClass <"NVPTX", regTypes, alignment, regList>;

//===----------------------------------------------------------------------===//
//  Registers
//===----------------------------------------------------------------------===//

// Special Registers used as stack pointer
def VRFrame         : NVPTXReg<"%SP">;
def VRFrameLocal    : NVPTXReg<"%SPL">;

// Special Registers used as the stack
def VRDepot  : NVPTXReg<"%Depot">;

// We use virtual registers, but define a few physical registers here to keep
// SDAG and the MachineInstr layers happy.
foreach i = 0-4 in {
  def P#i  : NVPTXReg<"%p"#i>;  // Predicate
  def RS#i : NVPTXReg<"%rs"#i>; // 16-bit
  def R#i  : NVPTXReg<"%r"#i>;  // 32-bit
  def RL#i : NVPTXReg<"%rd"#i>; // 64-bit
  def H#i  : NVPTXReg<"%h"#i>;  // 16-bit float
  def HH#i : NVPTXReg<"%hh"#i>; // 2x16-bit float
  def F#i  : NVPTXReg<"%f"#i>;  // 32-bit float
  def FL#i : NVPTXReg<"%fd"#i>; // 64-bit float

  // Arguments
  def ia#i : NVPTXReg<"%ia"#i>;
  def la#i : NVPTXReg<"%la"#i>;
  def fa#i : NVPTXReg<"%fa"#i>;
  def da#i : NVPTXReg<"%da"#i>;
}

foreach i = 0-31 in {
  def ENVREG#i : NVPTXReg<"%envreg"#i>;
}

//===----------------------------------------------------------------------===//
//  Register classes
//===----------------------------------------------------------------------===//
def Int1Regs : NVPTXRegClass<[i1], 8, (add (sequence "P%u", 0, 4))>;
def Int16Regs : NVPTXRegClass<[i16], 16, (add (sequence "RS%u", 0, 4))>;
def Int32Regs : NVPTXRegClass<[i32], 32, (add (sequence "R%u", 0, 4))>;
def Int64Regs : NVPTXRegClass<[i64], 64, (add (sequence "RL%u", 0, 4))>;
def Float16Regs : NVPTXRegClass<[f16], 16, (add (sequence "H%u", 0, 4))>;
def Float16x2Regs : NVPTXRegClass<[v2f16], 32, (add (sequence "HH%u", 0, 4))>;
def Float32Regs : NVPTXRegClass<[f32], 32, (add (sequence "F%u", 0, 4))>;
def Float64Regs : NVPTXRegClass<[f64], 64, (add (sequence "FL%u", 0, 4))>;
def Int32ArgRegs : NVPTXRegClass<[i32], 32, (add (sequence "ia%u", 0, 4))>;
def Int64ArgRegs : NVPTXRegClass<[i64], 64, (add (sequence "la%u", 0, 4))>;
def Float32ArgRegs : NVPTXRegClass<[f32], 32, (add (sequence "fa%u", 0, 4))>;
def Float64ArgRegs : NVPTXRegClass<[f64], 64, (add (sequence "da%u", 0, 4))>;

// Read NVPTXRegisterInfo.cpp to see how VRFrame and VRDepot are used.
def SpecialRegs : NVPTXRegClass<[i32], 32, (add VRFrame, VRFrameLocal, VRDepot,
                                            (sequence "ENVREG%u", 0, 31))>;
