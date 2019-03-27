//===- ARCRegisterInfo.td - ARC Register defs --------------*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//  Declarations that describe the ARC register file 
//===----------------------------------------------------------------------===//

class ARCReg<string n, list<string> altNames> : Register<n, altNames> {
  field bits<6> HwEncoding;
  let Namespace = "ARC";
}

// Registers are identified with 6-bit ID numbers.
// Core - 32-bit core registers
class Core<int num, string n, list<string>altNames=[]> : ARCReg<n, altNames> {
  let HWEncoding = num;
}

class Status<string n> : ARCReg<n, []> {
}

// Integer registers
def R0 : Core< 0, "%r0">, DwarfRegNum<[0]>;
def R1 : Core< 1, "%r1">, DwarfRegNum<[1]>;
def R2 : Core< 2, "%r2">, DwarfRegNum<[2]>; 
def R3 : Core< 3, "%r3">, DwarfRegNum<[3]>;
let CostPerUse=1 in {
def R4 : Core< 4, "%r4">, DwarfRegNum<[4]>;
def R5 : Core< 5, "%r5">, DwarfRegNum<[5]>; 
def R6 : Core< 6, "%r6">, DwarfRegNum<[6]>;
def R7 : Core< 7, "%r7">, DwarfRegNum<[7]>;
def R8 : Core< 8, "%r8">, DwarfRegNum<[8]>;
def R9 : Core< 9, "%r9">, DwarfRegNum<[9]>;
def R10 : Core<10, "%r10">, DwarfRegNum<[10]>; 
def R11 : Core<11, "%r11">, DwarfRegNum<[11]>;
}
def R12 : Core<12, "%r12">, DwarfRegNum<[12]>;
def R13 : Core<13, "%r13">, DwarfRegNum<[13]>; 
def R14 : Core<14, "%r14">, DwarfRegNum<[14]>;
def R15 : Core<15, "%r15">, DwarfRegNum<[15]>;

let CostPerUse=1 in {
def R16 : Core<16, "%r16">, DwarfRegNum<[16]>;
def R17 : Core<17, "%r17">, DwarfRegNum<[17]>;
def R18 : Core<18, "%r18">, DwarfRegNum<[18]>; 
def R19 : Core<19, "%r19">, DwarfRegNum<[19]>;
def R20 : Core<20, "%r20">, DwarfRegNum<[20]>;
def R21 : Core<21, "%r21">, DwarfRegNum<[21]>; 
def R22 : Core<22, "%r22">, DwarfRegNum<[22]>;
def R23 : Core<23, "%r23">, DwarfRegNum<[23]>;
def R24 : Core<24, "%r24">, DwarfRegNum<[24]>;
def R25 : Core<25, "%r25">, DwarfRegNum<[25]>;
def GP : Core<26, "%gp",["%r26"]>, DwarfRegNum<[26]>; 
def FP : Core<27, "%fp", ["%r27"]>, DwarfRegNum<[27]>;
def SP : Core<28, "%sp", ["%r28"]>, DwarfRegNum<[28]>;
def ILINK : Core<29, "%ilink">, DwarfRegNum<[29]>;
def R30 : Core<30, "%r30">, DwarfRegNum<[30]>;
def BLINK: Core<31, "%blink">, DwarfRegNum<[31]>;

def STATUS32 : Status<"status32">, DwarfRegNum<[32]>;
}

// Register classes.
//
def GPR32: RegisterClass<"ARC", [i32], 32,
  (add R0, R1, R2, R3,
  R4, R5, R6, R7, R8, R9, R10, R11, R12, R13, R14, R15, R16, R17, R18, R19,
  R20, R21, R22, R23, R24, R25, GP, FP, SP, ILINK, R30, BLINK)>;

def SREG : RegisterClass<"ARC", [i32], 1, (add STATUS32)>;

def GPR_S : RegisterClass<"ARC", [i32], 8,
  (add R0, R1, R2, R3, R12, R13, R14, R15)>;

