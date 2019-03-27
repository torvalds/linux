//===-- XCoreRegisterInfo.td - XCore Register defs ---------*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//  Declarations that describe the XCore register file 
//===----------------------------------------------------------------------===//

class XCoreReg<string n> : Register<n> {
  field bits<4> Num;
  let Namespace = "XCore";
}

// Registers are identified with 4-bit ID numbers.
// Ri - 32-bit integer registers
class Ri<bits<4> num, string n> : XCoreReg<n> {
  let Num = num;
}

// CPU registers
def R0  : Ri< 0, "r0">, DwarfRegNum<[0]>;
def R1  : Ri< 1, "r1">, DwarfRegNum<[1]>;
def R2  : Ri< 2, "r2">, DwarfRegNum<[2]>; 
def R3  : Ri< 3, "r3">, DwarfRegNum<[3]>;
def R4  : Ri< 4, "r4">, DwarfRegNum<[4]>;
def R5  : Ri< 5, "r5">, DwarfRegNum<[5]>; 
def R6  : Ri< 6, "r6">, DwarfRegNum<[6]>;
def R7  : Ri< 7, "r7">, DwarfRegNum<[7]>;
def R8  : Ri< 8, "r8">, DwarfRegNum<[8]>;
def R9  : Ri< 9, "r9">, DwarfRegNum<[9]>; 
def R10 : Ri<10, "r10">, DwarfRegNum<[10]>;
def R11 : Ri<11, "r11">, DwarfRegNum<[11]>;
def CP : Ri<12, "cp">, DwarfRegNum<[12]>; 
def DP : Ri<13, "dp">, DwarfRegNum<[13]>;
def SP : Ri<14, "sp">, DwarfRegNum<[14]>;
def LR : Ri<15, "lr">, DwarfRegNum<[15]>;

// Register classes.
//
def GRRegs : RegisterClass<"XCore", [i32], 32,
  // Return values and arguments
  (add R0, R1, R2, R3,
  // Callee save
  R4, R5, R6, R7, R8, R9, R10,
  // Not preserved across procedure calls
  R11)>;

// Reserved
def RRegs : RegisterClass<"XCore", [i32], 32,
  (add R0, R1, R2, R3,
   R4, R5, R6, R7, R8, R9, R10,
   R11, CP, DP, SP, LR)> {
  let isAllocatable = 0;
}
