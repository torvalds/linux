//===- LanaiRegisterInfo.td - Lanai Register defs ------------*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//  Declarations that describe the Lanai register file
//===----------------------------------------------------------------------===//

// Registers are identified with 5-bit ID numbers.
class LanaiReg<bits<5> num, string n, list<Register> subregs = [],
               list<string> altNames = []> : Register<n, altNames> {
  field bits<5> Num;
  let Num = num;
  let Namespace = "Lanai";
  let SubRegs = subregs;
}

let Namespace = "Lanai" in {
  def sub_32 : SubRegIndex<32>;
}

// Integer registers
foreach i = 0-31 in {
  def R#i : LanaiReg<i, "r"#i>, DwarfRegNum<[i]>;
}

// Register aliases
let SubRegIndices = [sub_32] in {
  def PC  : LanaiReg< 2,  "pc",  [R2]>,  DwarfRegAlias<R2>;
  def SP  : LanaiReg< 4,  "sp",  [R4]>,  DwarfRegAlias<R4>;
  def FP  : LanaiReg< 5,  "fp",  [R5]>,  DwarfRegAlias<R5>;
  def RV  : LanaiReg< 8,  "rv",  [R8]>,  DwarfRegAlias<R8>;
  def RR1 : LanaiReg<10, "rr1", [R10]>, DwarfRegAlias<R10>;
  def RR2 : LanaiReg<11, "rr2", [R11]>, DwarfRegAlias<R11>;
  def RCA : LanaiReg<15, "rca", [R15]>, DwarfRegAlias<R15>;
}

// Define a status register to capture the dependencies between the set flag
// and setcc instructions
def SR : LanaiReg< 0, "sw">;

// Register classes.
def GPR : RegisterClass<"Lanai", [i32], 32,
    (add R3, R9, R12, R13, R14, R16, R17,
     (sequence "R%i", 20, 31),
     R6, R7, R18, R19, // registers for passing arguments
     R15, RCA, // register for constant addresses
     R10, RR1, R11, RR2, // programmer controlled registers
     R8,  RV,  // return value
     R5,  FP,  // frame pointer
     R4,  SP,  // stack pointer
     R2,  PC,  // program counter
     R1,       // all 1s (0xffffffff)
     R0        // constant 0
    )>;

// Condition code register class
def CCR : RegisterClass<"Lanai", [i32], 32, (add SR)> {
  let CopyCost = -1; // Don't allow copying of status registers
  let isAllocatable = 0;
}
