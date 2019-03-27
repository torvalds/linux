//===-- SparcRegisterInfo.td - Sparc Register defs ---------*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//  Declarations that describe the Sparc register file
//===----------------------------------------------------------------------===//

class SparcReg<bits<16> Enc, string n> : Register<n> {
  let HWEncoding = Enc;
  let Namespace = "SP";
}

class SparcCtrlReg<bits<16> Enc, string n>: Register<n> {
  let HWEncoding = Enc;
  let Namespace = "SP";
}

let Namespace = "SP" in {
def sub_even : SubRegIndex<32>;
def sub_odd  : SubRegIndex<32, 32>;
def sub_even64 : SubRegIndex<64>;
def sub_odd64  : SubRegIndex<64, 64>;
}

// Registers are identified with 5-bit ID numbers.
// Ri - 32-bit integer registers
class Ri<bits<16> Enc, string n> : SparcReg<Enc, n>;

// Rdi - pairs of 32-bit integer registers
class Rdi<bits<16> Enc, string n, list<Register> subregs> : SparcReg<Enc, n> {
  let SubRegs = subregs;
  let SubRegIndices = [sub_even, sub_odd];
  let CoveredBySubRegs = 1;
}
// Rf - 32-bit floating-point registers
class Rf<bits<16> Enc, string n> : SparcReg<Enc, n>;

// Rd - Slots in the FP register file for 64-bit floating-point values.
class Rd<bits<16> Enc, string n, list<Register> subregs> : SparcReg<Enc, n> {
  let SubRegs = subregs;
  let SubRegIndices = [sub_even, sub_odd];
  let CoveredBySubRegs = 1;
}

// Rq - Slots in the FP register file for 128-bit floating-point values.
class Rq<bits<16> Enc, string n, list<Register> subregs> : SparcReg<Enc, n> {
  let SubRegs = subregs;
  let SubRegIndices = [sub_even64, sub_odd64];
  let CoveredBySubRegs = 1;
}

// Control Registers
def ICC : SparcCtrlReg<0, "ICC">; // This represents icc and xcc in 64-bit code.
foreach I = 0-3 in
  def FCC#I : SparcCtrlReg<I, "FCC"#I>;

def FSR : SparcCtrlReg<0, "FSR">; // Floating-point state register.

def FQ : SparcCtrlReg<0, "FQ">; // Floating-point deferred-trap queue.

def CPSR : SparcCtrlReg<0, "CPSR">; // Co-processor state register.

def CPQ : SparcCtrlReg<0, "CPQ">; // Co-processor queue.

// Y register
def Y : SparcCtrlReg<0, "Y">, DwarfRegNum<[64]>;
// Ancillary state registers (implementation defined)
def ASR1 : SparcCtrlReg<1, "ASR1">;
def ASR2 : SparcCtrlReg<2, "ASR2">;
def ASR3 : SparcCtrlReg<3, "ASR3">;
def ASR4 : SparcCtrlReg<4, "ASR4">;
def ASR5 : SparcCtrlReg<5, "ASR5">;
def ASR6 : SparcCtrlReg<6, "ASR6">;
def ASR7 : SparcCtrlReg<7, "ASR7">;
def ASR8 : SparcCtrlReg<8, "ASR8">;
def ASR9 : SparcCtrlReg<9, "ASR9">;
def ASR10 : SparcCtrlReg<10, "ASR10">;
def ASR11 : SparcCtrlReg<11, "ASR11">;
def ASR12 : SparcCtrlReg<12, "ASR12">;
def ASR13 : SparcCtrlReg<13, "ASR13">;
def ASR14 : SparcCtrlReg<14, "ASR14">;
def ASR15 : SparcCtrlReg<15, "ASR15">;
def ASR16 : SparcCtrlReg<16, "ASR16">;
def ASR17 : SparcCtrlReg<17, "ASR17">;
def ASR18 : SparcCtrlReg<18, "ASR18">;
def ASR19 : SparcCtrlReg<19, "ASR19">;
def ASR20 : SparcCtrlReg<20, "ASR20">;
def ASR21 : SparcCtrlReg<21, "ASR21">;
def ASR22 : SparcCtrlReg<22, "ASR22">;
def ASR23 : SparcCtrlReg<23, "ASR23">;
def ASR24 : SparcCtrlReg<24, "ASR24">;
def ASR25 : SparcCtrlReg<25, "ASR25">;
def ASR26 : SparcCtrlReg<26, "ASR26">;
def ASR27 : SparcCtrlReg<27, "ASR27">;
def ASR28 : SparcCtrlReg<28, "ASR28">;
def ASR29 : SparcCtrlReg<29, "ASR29">;
def ASR30 : SparcCtrlReg<30, "ASR30">;
def ASR31 : SparcCtrlReg<31, "ASR31">;

// Note that PSR, WIM, and TBR don't exist on the SparcV9, only the V8.
def PSR : SparcCtrlReg<0, "PSR">;
def WIM : SparcCtrlReg<0, "WIM">;
def TBR : SparcCtrlReg<0, "TBR">;

def TPC : SparcCtrlReg<0, "TPC">;
def TNPC : SparcCtrlReg<1, "TNPC">;
def TSTATE : SparcCtrlReg<2, "TSTATE">;
def TT : SparcCtrlReg<3, "TT">;
def TICK : SparcCtrlReg<4, "TICK">;
def TBA : SparcCtrlReg<5, "TBA">;
def PSTATE : SparcCtrlReg<6, "PSTATE">;
def TL : SparcCtrlReg<7, "TL">;
def PIL : SparcCtrlReg<8, "PIL">;
def CWP : SparcCtrlReg<9, "CWP">;
def CANSAVE : SparcCtrlReg<10, "CANSAVE">;
def CANRESTORE : SparcCtrlReg<11, "CANRESTORE">;
def CLEANWIN : SparcCtrlReg<12, "CLEANWIN">;
def OTHERWIN : SparcCtrlReg<13, "OTHERWIN">;
def WSTATE : SparcCtrlReg<14, "WSTATE">;

// Integer registers
def G0 : Ri< 0, "G0">, DwarfRegNum<[0]>;
def G1 : Ri< 1, "G1">, DwarfRegNum<[1]>;
def G2 : Ri< 2, "G2">, DwarfRegNum<[2]>;
def G3 : Ri< 3, "G3">, DwarfRegNum<[3]>;
def G4 : Ri< 4, "G4">, DwarfRegNum<[4]>;
def G5 : Ri< 5, "G5">, DwarfRegNum<[5]>;
def G6 : Ri< 6, "G6">, DwarfRegNum<[6]>;
def G7 : Ri< 7, "G7">, DwarfRegNum<[7]>;
def O0 : Ri< 8, "O0">, DwarfRegNum<[8]>;
def O1 : Ri< 9, "O1">, DwarfRegNum<[9]>;
def O2 : Ri<10, "O2">, DwarfRegNum<[10]>;
def O3 : Ri<11, "O3">, DwarfRegNum<[11]>;
def O4 : Ri<12, "O4">, DwarfRegNum<[12]>;
def O5 : Ri<13, "O5">, DwarfRegNum<[13]>;
def O6 : Ri<14, "SP">, DwarfRegNum<[14]>;
def O7 : Ri<15, "O7">, DwarfRegNum<[15]>;
def L0 : Ri<16, "L0">, DwarfRegNum<[16]>;
def L1 : Ri<17, "L1">, DwarfRegNum<[17]>;
def L2 : Ri<18, "L2">, DwarfRegNum<[18]>;
def L3 : Ri<19, "L3">, DwarfRegNum<[19]>;
def L4 : Ri<20, "L4">, DwarfRegNum<[20]>;
def L5 : Ri<21, "L5">, DwarfRegNum<[21]>;
def L6 : Ri<22, "L6">, DwarfRegNum<[22]>;
def L7 : Ri<23, "L7">, DwarfRegNum<[23]>;
def I0 : Ri<24, "I0">, DwarfRegNum<[24]>;
def I1 : Ri<25, "I1">, DwarfRegNum<[25]>;
def I2 : Ri<26, "I2">, DwarfRegNum<[26]>;
def I3 : Ri<27, "I3">, DwarfRegNum<[27]>;
def I4 : Ri<28, "I4">, DwarfRegNum<[28]>;
def I5 : Ri<29, "I5">, DwarfRegNum<[29]>;
def I6 : Ri<30, "FP">, DwarfRegNum<[30]>;
def I7 : Ri<31, "I7">, DwarfRegNum<[31]>;

// Floating-point registers
def F0  : Rf< 0,  "F0">, DwarfRegNum<[32]>;
def F1  : Rf< 1,  "F1">, DwarfRegNum<[33]>;
def F2  : Rf< 2,  "F2">, DwarfRegNum<[34]>;
def F3  : Rf< 3,  "F3">, DwarfRegNum<[35]>;
def F4  : Rf< 4,  "F4">, DwarfRegNum<[36]>;
def F5  : Rf< 5,  "F5">, DwarfRegNum<[37]>;
def F6  : Rf< 6,  "F6">, DwarfRegNum<[38]>;
def F7  : Rf< 7,  "F7">, DwarfRegNum<[39]>;
def F8  : Rf< 8,  "F8">, DwarfRegNum<[40]>;
def F9  : Rf< 9,  "F9">, DwarfRegNum<[41]>;
def F10 : Rf<10, "F10">, DwarfRegNum<[42]>;
def F11 : Rf<11, "F11">, DwarfRegNum<[43]>;
def F12 : Rf<12, "F12">, DwarfRegNum<[44]>;
def F13 : Rf<13, "F13">, DwarfRegNum<[45]>;
def F14 : Rf<14, "F14">, DwarfRegNum<[46]>;
def F15 : Rf<15, "F15">, DwarfRegNum<[47]>;
def F16 : Rf<16, "F16">, DwarfRegNum<[48]>;
def F17 : Rf<17, "F17">, DwarfRegNum<[49]>;
def F18 : Rf<18, "F18">, DwarfRegNum<[50]>;
def F19 : Rf<19, "F19">, DwarfRegNum<[51]>;
def F20 : Rf<20, "F20">, DwarfRegNum<[52]>;
def F21 : Rf<21, "F21">, DwarfRegNum<[53]>;
def F22 : Rf<22, "F22">, DwarfRegNum<[54]>;
def F23 : Rf<23, "F23">, DwarfRegNum<[55]>;
def F24 : Rf<24, "F24">, DwarfRegNum<[56]>;
def F25 : Rf<25, "F25">, DwarfRegNum<[57]>;
def F26 : Rf<26, "F26">, DwarfRegNum<[58]>;
def F27 : Rf<27, "F27">, DwarfRegNum<[59]>;
def F28 : Rf<28, "F28">, DwarfRegNum<[60]>;
def F29 : Rf<29, "F29">, DwarfRegNum<[61]>;
def F30 : Rf<30, "F30">, DwarfRegNum<[62]>;
def F31 : Rf<31, "F31">, DwarfRegNum<[63]>;

// Aliases of the F* registers used to hold 64-bit fp values (doubles)
def D0  : Rd< 0,  "F0", [F0,   F1]>, DwarfRegNum<[72]>;
def D1  : Rd< 2,  "F2", [F2,   F3]>, DwarfRegNum<[73]>;
def D2  : Rd< 4,  "F4", [F4,   F5]>, DwarfRegNum<[74]>;
def D3  : Rd< 6,  "F6", [F6,   F7]>, DwarfRegNum<[75]>;
def D4  : Rd< 8,  "F8", [F8,   F9]>, DwarfRegNum<[76]>;
def D5  : Rd<10, "F10", [F10, F11]>, DwarfRegNum<[77]>;
def D6  : Rd<12, "F12", [F12, F13]>, DwarfRegNum<[78]>;
def D7  : Rd<14, "F14", [F14, F15]>, DwarfRegNum<[79]>;
def D8  : Rd<16, "F16", [F16, F17]>, DwarfRegNum<[80]>;
def D9  : Rd<18, "F18", [F18, F19]>, DwarfRegNum<[81]>;
def D10 : Rd<20, "F20", [F20, F21]>, DwarfRegNum<[82]>;
def D11 : Rd<22, "F22", [F22, F23]>, DwarfRegNum<[83]>;
def D12 : Rd<24, "F24", [F24, F25]>, DwarfRegNum<[84]>;
def D13 : Rd<26, "F26", [F26, F27]>, DwarfRegNum<[85]>;
def D14 : Rd<28, "F28", [F28, F29]>, DwarfRegNum<[86]>;
def D15 : Rd<30, "F30", [F30, F31]>, DwarfRegNum<[87]>;

// Co-processor registers
def C0 : Ri< 0, "C0">;
def C1 : Ri< 1, "C1">;
def C2 : Ri< 2, "C2">;
def C3 : Ri< 3, "C3">;
def C4 : Ri< 4, "C4">;
def C5 : Ri< 5, "C5">;
def C6 : Ri< 6, "C6">;
def C7 : Ri< 7, "C7">;
def C8 : Ri< 8, "C8">;
def C9 : Ri< 9, "C9">;
def C10 : Ri< 10, "C10">;
def C11 : Ri< 11, "C11">;
def C12 : Ri< 12, "C12">;
def C13 : Ri< 13, "C13">;
def C14 : Ri< 14, "C14">;
def C15 : Ri< 15, "C15">;
def C16 : Ri< 16, "C16">;
def C17 : Ri< 17, "C17">;
def C18 : Ri< 18, "C18">;
def C19 : Ri< 19, "C19">;
def C20 : Ri< 20, "C20">;
def C21 : Ri< 21, "C21">;
def C22 : Ri< 22, "C22">;
def C23 : Ri< 23, "C23">;
def C24 : Ri< 24, "C24">;
def C25 : Ri< 25, "C25">;
def C26 : Ri< 26, "C26">;
def C27 : Ri< 27, "C27">;
def C28 : Ri< 28, "C28">;
def C29 : Ri< 29, "C29">;
def C30 : Ri< 30, "C30">;
def C31 : Ri< 31, "C31">;

// Unaliased double precision floating point registers.
// FIXME: Define DwarfRegNum for these registers.
def D16 : SparcReg< 1, "F32">;
def D17 : SparcReg< 3, "F34">;
def D18 : SparcReg< 5, "F36">;
def D19 : SparcReg< 7, "F38">;
def D20 : SparcReg< 9, "F40">;
def D21 : SparcReg<11, "F42">;
def D22 : SparcReg<13, "F44">;
def D23 : SparcReg<15, "F46">;
def D24 : SparcReg<17, "F48">;
def D25 : SparcReg<19, "F50">;
def D26 : SparcReg<21, "F52">;
def D27 : SparcReg<23, "F54">;
def D28 : SparcReg<25, "F56">;
def D29 : SparcReg<27, "F58">;
def D30 : SparcReg<29, "F60">;
def D31 : SparcReg<31, "F62">;

// Aliases of the F* registers used to hold 128-bit for values (long doubles).
def Q0  : Rq< 0,  "F0", [D0,   D1]>;
def Q1  : Rq< 4,  "F4", [D2,   D3]>;
def Q2  : Rq< 8,  "F8", [D4,   D5]>;
def Q3  : Rq<12, "F12", [D6,   D7]>;
def Q4  : Rq<16, "F16", [D8,   D9]>;
def Q5  : Rq<20, "F20", [D10, D11]>;
def Q6  : Rq<24, "F24", [D12, D13]>;
def Q7  : Rq<28, "F28", [D14, D15]>;
def Q8  : Rq< 1, "F32", [D16, D17]>;
def Q9  : Rq< 5, "F36", [D18, D19]>;
def Q10 : Rq< 9, "F40", [D20, D21]>;
def Q11 : Rq<13, "F44", [D22, D23]>;
def Q12 : Rq<17, "F48", [D24, D25]>;
def Q13 : Rq<21, "F52", [D26, D27]>;
def Q14 : Rq<25, "F56", [D28, D29]>;
def Q15 : Rq<29, "F60", [D30, D31]>;

// Aliases of the integer registers used for LDD/STD double-word operations
def G0_G1 : Rdi<0, "G0", [G0, G1]>;
def G2_G3 : Rdi<2, "G2", [G2, G3]>;
def G4_G5 : Rdi<4, "G4", [G4, G5]>;
def G6_G7 : Rdi<6, "G6", [G6, G7]>;
def O0_O1 : Rdi<8, "O0", [O0, O1]>;
def O2_O3 : Rdi<10, "O2", [O2, O3]>;
def O4_O5 : Rdi<12, "O4", [O4, O5]>;
def O6_O7 : Rdi<14, "O6", [O6, O7]>;
def L0_L1 : Rdi<16, "L0", [L0, L1]>;
def L2_L3 : Rdi<18, "L2", [L2, L3]>;
def L4_L5 : Rdi<20, "L4", [L4, L5]>;
def L6_L7 : Rdi<22, "L6", [L6, L7]>;
def I0_I1 : Rdi<24, "I0", [I0, I1]>;
def I2_I3 : Rdi<26, "I2", [I2, I3]>;
def I4_I5 : Rdi<28, "I4", [I4, I5]>;
def I6_I7 : Rdi<30, "I6", [I6, I7]>;

// Aliases of the co-processor registers used for LDD/STD double-word operations
def C0_C1 : Rdi<0, "C0", [C0, C1]>;
def C2_C3 : Rdi<2, "C2", [C2, C3]>;
def C4_C5 : Rdi<4, "C4", [C4, C5]>;
def C6_C7 : Rdi<6, "C6", [C6, C7]>;
def C8_C9 : Rdi<8, "C8", [C8, C9]>;
def C10_C11 : Rdi<10, "C10", [C10, C11]>;
def C12_C13 : Rdi<12, "C12", [C12, C13]>;
def C14_C15 : Rdi<14, "C14", [C14, C15]>;
def C16_C17 : Rdi<16, "C16", [C16, C17]>;
def C18_C19 : Rdi<18, "C18", [C18, C19]>;
def C20_C21 : Rdi<20, "C20", [C20, C21]>;
def C22_C23 : Rdi<22, "C22", [C22, C23]>;
def C24_C25 : Rdi<24, "C24", [C24, C25]>;
def C26_C27 : Rdi<26, "C26", [C26, C27]>;
def C28_C29 : Rdi<28, "C28", [C28, C29]>;
def C30_C31 : Rdi<30, "C30", [C30, C31]>;

// Register classes.
//
// FIXME: the register order should be defined in terms of the preferred
// allocation order...
//
// This register class should not be used to hold i64 values, use the I64Regs
// register class for that. The i64 type is included here to allow i64 patterns
// using the integer instructions.
def IntRegs : RegisterClass<"SP", [i32, i64], 32,
                            (add (sequence "I%u", 0, 7),
                                 (sequence "G%u", 0, 7),
                                 (sequence "L%u", 0, 7),
                                 (sequence "O%u", 0, 7))>;

// Should be in the same order as IntRegs.
def IntPair : RegisterClass<"SP", [v2i32], 64,
    (add I0_I1, I2_I3, I4_I5, I6_I7,
         G0_G1, G2_G3, G4_G5, G6_G7,
         L0_L1, L2_L3, L4_L5, L6_L7,
         O0_O1, O2_O3, O4_O5, O6_O7)>;

// Register class for 64-bit mode, with a 64-bit spill slot size.
// These are the same as the 32-bit registers, so TableGen will consider this
// to be a sub-class of IntRegs. That works out because requiring a 64-bit
// spill slot is a stricter constraint than only requiring a 32-bit spill slot.
def I64Regs : RegisterClass<"SP", [i64], 64, (add IntRegs)>;

// Floating point register classes.
def FPRegs : RegisterClass<"SP", [f32], 32, (sequence "F%u", 0, 31)>;
def DFPRegs : RegisterClass<"SP", [f64], 64, (sequence "D%u", 0, 31)>;
def QFPRegs : RegisterClass<"SP", [f128], 128, (sequence "Q%u", 0, 15)>;

// The Low?FPRegs classes are used only for inline-asm constraints.
def LowDFPRegs : RegisterClass<"SP", [f64], 64, (sequence "D%u", 0, 15)>;
def LowQFPRegs : RegisterClass<"SP", [f128], 128, (sequence "Q%u", 0, 7)>;

// Floating point control register classes.
def FCCRegs : RegisterClass<"SP", [i1], 1, (sequence "FCC%u", 0, 3)>;

let isAllocatable = 0 in {
  // Ancillary state registers
  def ASRRegs : RegisterClass<"SP", [i32], 32,
                              (add Y, (sequence "ASR%u", 1, 31))>;
                            
  // This register class should not be used to hold i64 values.
  def CoprocRegs : RegisterClass<"SP", [i32], 32,
                                (add (sequence "C%u", 0, 31))>;

  // Should be in the same order as CoprocRegs.
  def CoprocPair : RegisterClass<"SP", [v2i32], 64,
    (add C0_C1,   C2_C3,   C4_C5,   C6_C7,   
         C8_C9,   C10_C11, C12_C13, C14_C15,
         C16_C17, C18_C19, C20_C21, C22_C23,
         C24_C25, C26_C27, C28_C29, C30_C31)>;
}

// Privileged Registers
def PRRegs : RegisterClass<"SP", [i64], 64,
    (add TPC, TNPC, TSTATE, TT, TICK, TBA, PSTATE, TL, PIL, CWP,
         CANSAVE, CANRESTORE, CLEANWIN, OTHERWIN, WSTATE)>;
