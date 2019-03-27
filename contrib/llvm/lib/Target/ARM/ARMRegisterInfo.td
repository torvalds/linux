//===-- ARMRegisterInfo.td - ARM Register defs -------------*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

include "ARMSystemRegister.td"

//===----------------------------------------------------------------------===//
//  Declarations that describe the ARM register file
//===----------------------------------------------------------------------===//

// Registers are identified with 4-bit ID numbers.
class ARMReg<bits<16> Enc, string n, list<Register> subregs = []> : Register<n> {
  let HWEncoding = Enc;
  let Namespace = "ARM";
  let SubRegs = subregs;
  // All bits of ARM registers with sub-registers are covered by sub-registers.
  let CoveredBySubRegs = 1;
}

class ARMFReg<bits<16> Enc, string n> : Register<n> {
  let HWEncoding = Enc;
  let Namespace = "ARM";
}

// Subregister indices.
let Namespace = "ARM" in {
def qqsub_0 : SubRegIndex<256>;
def qqsub_1 : SubRegIndex<256, 256>;

// Note: Code depends on these having consecutive numbers.
def qsub_0 : SubRegIndex<128>;
def qsub_1 : SubRegIndex<128, 128>;
def qsub_2 : ComposedSubRegIndex<qqsub_1, qsub_0>;
def qsub_3 : ComposedSubRegIndex<qqsub_1, qsub_1>;

def dsub_0 : SubRegIndex<64>;
def dsub_1 : SubRegIndex<64, 64>;
def dsub_2 : ComposedSubRegIndex<qsub_1, dsub_0>;
def dsub_3 : ComposedSubRegIndex<qsub_1, dsub_1>;
def dsub_4 : ComposedSubRegIndex<qsub_2, dsub_0>;
def dsub_5 : ComposedSubRegIndex<qsub_2, dsub_1>;
def dsub_6 : ComposedSubRegIndex<qsub_3, dsub_0>;
def dsub_7 : ComposedSubRegIndex<qsub_3, dsub_1>;

def ssub_0  : SubRegIndex<32>;
def ssub_1  : SubRegIndex<32, 32>;
def ssub_2  : ComposedSubRegIndex<dsub_1, ssub_0>;
def ssub_3  : ComposedSubRegIndex<dsub_1, ssub_1>;
def ssub_4  : ComposedSubRegIndex<dsub_2, ssub_0>;
def ssub_5  : ComposedSubRegIndex<dsub_2, ssub_1>;
def ssub_6  : ComposedSubRegIndex<dsub_3, ssub_0>;
def ssub_7  : ComposedSubRegIndex<dsub_3, ssub_1>;
def ssub_8  : ComposedSubRegIndex<dsub_4, ssub_0>;
def ssub_9  : ComposedSubRegIndex<dsub_4, ssub_1>;
def ssub_10 : ComposedSubRegIndex<dsub_5, ssub_0>;
def ssub_11 : ComposedSubRegIndex<dsub_5, ssub_1>;
def ssub_12 : ComposedSubRegIndex<dsub_6, ssub_0>;
def ssub_13 : ComposedSubRegIndex<dsub_6, ssub_1>;

def gsub_0 : SubRegIndex<32>;
def gsub_1 : SubRegIndex<32, 32>;
// Let TableGen synthesize the remaining 12 ssub_* indices.
// We don't need to name them.
}

// Integer registers
def R0  : ARMReg< 0, "r0">,  DwarfRegNum<[0]>;
def R1  : ARMReg< 1, "r1">,  DwarfRegNum<[1]>;
def R2  : ARMReg< 2, "r2">,  DwarfRegNum<[2]>;
def R3  : ARMReg< 3, "r3">,  DwarfRegNum<[3]>;
def R4  : ARMReg< 4, "r4">,  DwarfRegNum<[4]>;
def R5  : ARMReg< 5, "r5">,  DwarfRegNum<[5]>;
def R6  : ARMReg< 6, "r6">,  DwarfRegNum<[6]>;
def R7  : ARMReg< 7, "r7">,  DwarfRegNum<[7]>;
// These require 32-bit instructions.
let CostPerUse = 1 in {
def R8  : ARMReg< 8, "r8">,  DwarfRegNum<[8]>;
def R9  : ARMReg< 9, "r9">,  DwarfRegNum<[9]>;
def R10 : ARMReg<10, "r10">, DwarfRegNum<[10]>;
def R11 : ARMReg<11, "r11">, DwarfRegNum<[11]>;
def R12 : ARMReg<12, "r12">, DwarfRegNum<[12]>;
def SP  : ARMReg<13, "sp">,  DwarfRegNum<[13]>;
def LR  : ARMReg<14, "lr">,  DwarfRegNum<[14]>;
def PC  : ARMReg<15, "pc">,  DwarfRegNum<[15]>;
}

// Float registers
def S0  : ARMFReg< 0, "s0">;  def S1  : ARMFReg< 1, "s1">;
def S2  : ARMFReg< 2, "s2">;  def S3  : ARMFReg< 3, "s3">;
def S4  : ARMFReg< 4, "s4">;  def S5  : ARMFReg< 5, "s5">;
def S6  : ARMFReg< 6, "s6">;  def S7  : ARMFReg< 7, "s7">;
def S8  : ARMFReg< 8, "s8">;  def S9  : ARMFReg< 9, "s9">;
def S10 : ARMFReg<10, "s10">; def S11 : ARMFReg<11, "s11">;
def S12 : ARMFReg<12, "s12">; def S13 : ARMFReg<13, "s13">;
def S14 : ARMFReg<14, "s14">; def S15 : ARMFReg<15, "s15">;
def S16 : ARMFReg<16, "s16">; def S17 : ARMFReg<17, "s17">;
def S18 : ARMFReg<18, "s18">; def S19 : ARMFReg<19, "s19">;
def S20 : ARMFReg<20, "s20">; def S21 : ARMFReg<21, "s21">;
def S22 : ARMFReg<22, "s22">; def S23 : ARMFReg<23, "s23">;
def S24 : ARMFReg<24, "s24">; def S25 : ARMFReg<25, "s25">;
def S26 : ARMFReg<26, "s26">; def S27 : ARMFReg<27, "s27">;
def S28 : ARMFReg<28, "s28">; def S29 : ARMFReg<29, "s29">;
def S30 : ARMFReg<30, "s30">; def S31 : ARMFReg<31, "s31">;

// Aliases of the F* registers used to hold 64-bit fp values (doubles)
let SubRegIndices = [ssub_0, ssub_1] in {
def D0  : ARMReg< 0,  "d0", [S0,   S1]>, DwarfRegNum<[256]>;
def D1  : ARMReg< 1,  "d1", [S2,   S3]>, DwarfRegNum<[257]>;
def D2  : ARMReg< 2,  "d2", [S4,   S5]>, DwarfRegNum<[258]>;
def D3  : ARMReg< 3,  "d3", [S6,   S7]>, DwarfRegNum<[259]>;
def D4  : ARMReg< 4,  "d4", [S8,   S9]>, DwarfRegNum<[260]>;
def D5  : ARMReg< 5,  "d5", [S10, S11]>, DwarfRegNum<[261]>;
def D6  : ARMReg< 6,  "d6", [S12, S13]>, DwarfRegNum<[262]>;
def D7  : ARMReg< 7,  "d7", [S14, S15]>, DwarfRegNum<[263]>;
def D8  : ARMReg< 8,  "d8", [S16, S17]>, DwarfRegNum<[264]>;
def D9  : ARMReg< 9,  "d9", [S18, S19]>, DwarfRegNum<[265]>;
def D10 : ARMReg<10, "d10", [S20, S21]>, DwarfRegNum<[266]>;
def D11 : ARMReg<11, "d11", [S22, S23]>, DwarfRegNum<[267]>;
def D12 : ARMReg<12, "d12", [S24, S25]>, DwarfRegNum<[268]>;
def D13 : ARMReg<13, "d13", [S26, S27]>, DwarfRegNum<[269]>;
def D14 : ARMReg<14, "d14", [S28, S29]>, DwarfRegNum<[270]>;
def D15 : ARMReg<15, "d15", [S30, S31]>, DwarfRegNum<[271]>;
}

// VFP3 defines 16 additional double registers
def D16 : ARMFReg<16, "d16">, DwarfRegNum<[272]>;
def D17 : ARMFReg<17, "d17">, DwarfRegNum<[273]>;
def D18 : ARMFReg<18, "d18">, DwarfRegNum<[274]>;
def D19 : ARMFReg<19, "d19">, DwarfRegNum<[275]>;
def D20 : ARMFReg<20, "d20">, DwarfRegNum<[276]>;
def D21 : ARMFReg<21, "d21">, DwarfRegNum<[277]>;
def D22 : ARMFReg<22, "d22">, DwarfRegNum<[278]>;
def D23 : ARMFReg<23, "d23">, DwarfRegNum<[279]>;
def D24 : ARMFReg<24, "d24">, DwarfRegNum<[280]>;
def D25 : ARMFReg<25, "d25">, DwarfRegNum<[281]>;
def D26 : ARMFReg<26, "d26">, DwarfRegNum<[282]>;
def D27 : ARMFReg<27, "d27">, DwarfRegNum<[283]>;
def D28 : ARMFReg<28, "d28">, DwarfRegNum<[284]>;
def D29 : ARMFReg<29, "d29">, DwarfRegNum<[285]>;
def D30 : ARMFReg<30, "d30">, DwarfRegNum<[286]>;
def D31 : ARMFReg<31, "d31">, DwarfRegNum<[287]>;

// Advanced SIMD (NEON) defines 16 quad-word aliases
let SubRegIndices = [dsub_0, dsub_1] in {
def Q0  : ARMReg< 0,  "q0", [D0,   D1]>;
def Q1  : ARMReg< 1,  "q1", [D2,   D3]>;
def Q2  : ARMReg< 2,  "q2", [D4,   D5]>;
def Q3  : ARMReg< 3,  "q3", [D6,   D7]>;
def Q4  : ARMReg< 4,  "q4", [D8,   D9]>;
def Q5  : ARMReg< 5,  "q5", [D10, D11]>;
def Q6  : ARMReg< 6,  "q6", [D12, D13]>;
def Q7  : ARMReg< 7,  "q7", [D14, D15]>;
}
let SubRegIndices = [dsub_0, dsub_1] in {
def Q8  : ARMReg< 8,  "q8", [D16, D17]>;
def Q9  : ARMReg< 9,  "q9", [D18, D19]>;
def Q10 : ARMReg<10, "q10", [D20, D21]>;
def Q11 : ARMReg<11, "q11", [D22, D23]>;
def Q12 : ARMReg<12, "q12", [D24, D25]>;
def Q13 : ARMReg<13, "q13", [D26, D27]>;
def Q14 : ARMReg<14, "q14", [D28, D29]>;
def Q15 : ARMReg<15, "q15", [D30, D31]>;
}

// Current Program Status Register.
// We model fpscr with two registers: FPSCR models the control bits and will be
// reserved. FPSCR_NZCV models the flag bits and will be unreserved. APSR_NZCV
// models the APSR when it's accessed by some special instructions. In such cases
// it has the same encoding as PC.
def CPSR       : ARMReg<0,  "cpsr">;
def APSR       : ARMReg<1,  "apsr">;
def APSR_NZCV  : ARMReg<15, "apsr_nzcv">;
def SPSR       : ARMReg<2,  "spsr">;
def FPSCR      : ARMReg<3,  "fpscr">;
def FPSCR_NZCV : ARMReg<3,  "fpscr_nzcv"> {
  let Aliases = [FPSCR];
}
def ITSTATE    : ARMReg<4, "itstate">;

// Special Registers - only available in privileged mode.
def FPSID   : ARMReg<0,  "fpsid">;
def MVFR2   : ARMReg<5,  "mvfr2">;
def MVFR1   : ARMReg<6,  "mvfr1">;
def MVFR0   : ARMReg<7,  "mvfr0">;
def FPEXC   : ARMReg<8,  "fpexc">;
def FPINST  : ARMReg<9,  "fpinst">;
def FPINST2 : ARMReg<10, "fpinst2">;

// Register classes.
//
// pc  == Program Counter
// lr  == Link Register
// sp  == Stack Pointer
// r12 == ip (scratch)
// r7  == Frame Pointer (thumb-style backtraces)
// r9  == May be reserved as Thread Register
// r11 == Frame Pointer (arm-style backtraces)
// r10 == Stack Limit
//
def GPR : RegisterClass<"ARM", [i32], 32, (add (sequence "R%u", 0, 12),
                                               SP, LR, PC)> {
  // Allocate LR as the first CSR since it is always saved anyway.
  // For Thumb1 mode, we don't want to allocate hi regs at all, as we don't
  // know how to spill them. If we make our prologue/epilogue code smarter at
  // some point, we can go back to using the above allocation orders for the
  // Thumb1 instructions that know how to use hi regs.
  let AltOrders = [(add LR, GPR), (trunc GPR, 8)];
  let AltOrderSelect = [{
      return 1 + MF.getSubtarget<ARMSubtarget>().isThumb1Only();
  }];
  let DiagnosticString = "operand must be a register in range [r0, r15]";
}

// GPRs without the PC.  Some ARM instructions do not allow the PC in
// certain operand slots, particularly as the destination.  Primarily
// useful for disassembly.
def GPRnopc : RegisterClass<"ARM", [i32], 32, (sub GPR, PC)> {
  let AltOrders = [(add LR, GPRnopc), (trunc GPRnopc, 8)];
  let AltOrderSelect = [{
      return 1 + MF.getSubtarget<ARMSubtarget>().isThumb1Only();
  }];
  let DiagnosticString = "operand must be a register in range [r0, r14]";
}

// GPRs without the PC but with APSR. Some instructions allow accessing the
// APSR, while actually encoding PC in the register field. This is useful
// for assembly and disassembly only.
def GPRwithAPSR : RegisterClass<"ARM", [i32], 32, (add (sub GPR, PC), APSR_NZCV)> {
  let AltOrders = [(add LR, GPRnopc), (trunc GPRnopc, 8)];
  let AltOrderSelect = [{
      return 1 + MF.getSubtarget<ARMSubtarget>().isThumb1Only();
  }];
  let DiagnosticString = "operand must be a register in range [r0, r14] or apsr_nzcv";
}

// GPRsp - Only the SP is legal. Used by Thumb1 instructions that want the
// implied SP argument list.
// FIXME: It would be better to not use this at all and refactor the
// instructions to not have SP an an explicit argument. That makes
// frame index resolution a bit trickier, though.
def GPRsp : RegisterClass<"ARM", [i32], 32, (add SP)> {
  let DiagnosticString = "operand must be a register sp";
}

// restricted GPR register class. Many Thumb2 instructions allow the full
// register range for operands, but have undefined behaviours when PC
// or SP (R13 or R15) are used. The ARM ISA refers to these operands
// via the BadReg() pseudo-code description.
def rGPR : RegisterClass<"ARM", [i32], 32, (sub GPR, SP, PC)> {
  let AltOrders = [(add LR, rGPR), (trunc rGPR, 8)];
  let AltOrderSelect = [{
      return 1 + MF.getSubtarget<ARMSubtarget>().isThumb1Only();
  }];
  let DiagnosticType = "rGPR";
}

// Thumb registers are R0-R7 normally. Some instructions can still use
// the general GPR register class above (MOV, e.g.)
def tGPR : RegisterClass<"ARM", [i32], 32, (trunc GPR, 8)> {
  let DiagnosticString = "operand must be a register in range [r0, r7]";
}

// Thumb registers R0-R7 and the PC. Some instructions like TBB or THH allow
// the PC to be used as a destination operand as well.
def tGPRwithpc : RegisterClass<"ARM", [i32], 32, (add tGPR, PC)>;

// The high registers in thumb mode, R8-R15.
def hGPR : RegisterClass<"ARM", [i32], 32, (sub GPR, tGPR)> {
  let DiagnosticString = "operand must be a register in range [r8, r15]";
}

// For tail calls, we can't use callee-saved registers, as they are restored
// to the saved value before the tail call, which would clobber a call address.
// Note, getMinimalPhysRegClass(R0) returns tGPR because of the names of
// this class and the preceding one(!)  This is what we want.
def tcGPR : RegisterClass<"ARM", [i32], 32, (add R0, R1, R2, R3, R12)> {
  let AltOrders = [(and tcGPR, tGPR)];
  let AltOrderSelect = [{
      return MF.getSubtarget<ARMSubtarget>().isThumb1Only();
  }];
}

// Condition code registers.
def CCR : RegisterClass<"ARM", [i32], 32, (add CPSR)> {
  let CopyCost = -1;  // Don't allow copying of status registers.
  let isAllocatable = 0;
}

// Scalar single precision floating point register class..
// FIXME: Allocation order changed to s0, s2, ... or s0, s4, ... as a quick hack
// to avoid partial-write dependencies on D or Q (depending on platform)
// registers (S registers are renamed as portions of D/Q registers).
def SPR : RegisterClass<"ARM", [f32], 32, (sequence "S%u", 0, 31)> {
  let AltOrders = [(add (decimate SPR, 2), SPR),
                   (add (decimate SPR, 4),
                        (decimate SPR, 2),
                        (decimate (rotl SPR, 1), 4),
                        (decimate (rotl SPR, 1), 2))];
  let AltOrderSelect = [{
    return 1 + MF.getSubtarget<ARMSubtarget>().useStride4VFPs(MF);
  }];
  let DiagnosticString = "operand must be a register in range [s0, s31]";
}

def HPR : RegisterClass<"ARM", [f16], 32, (sequence "S%u", 0, 31)> {
  let AltOrders = [(add (decimate HPR, 2), SPR),
                   (add (decimate HPR, 4),
                        (decimate HPR, 2),
                        (decimate (rotl HPR, 1), 4),
                        (decimate (rotl HPR, 1), 2))];
  let AltOrderSelect = [{
    return 1 + MF.getSubtarget<ARMSubtarget>().useStride4VFPs(MF);
  }];
  let DiagnosticString = "operand must be a register in range [s0, s31]";
}

// Subset of SPR which can be used as a source of NEON scalars for 16-bit
// operations
def SPR_8 : RegisterClass<"ARM", [f32], 32, (sequence "S%u", 0, 15)> {
  let DiagnosticString = "operand must be a register in range [s0, s15]";
}

// Scalar double precision floating point / generic 64-bit vector register
// class.
// ARM requires only word alignment for double. It's more performant if it
// is double-word alignment though.
def DPR : RegisterClass<"ARM", [f64, v8i8, v4i16, v2i32, v1i64, v2f32, v4f16], 64,
                        (sequence "D%u", 0, 31)> {
  // Allocate non-VFP2 registers D16-D31 first, and prefer even registers on
  // Darwin platforms.
  let AltOrders = [(rotl DPR, 16),
                   (add (decimate (rotl DPR, 16), 2), (rotl DPR, 16))];
  let AltOrderSelect = [{
    return 1 + MF.getSubtarget<ARMSubtarget>().useStride4VFPs(MF);
  }];
  let DiagnosticType = "DPR";
}

// Subset of DPR that are accessible with VFP2 (and so that also have
// 32-bit SPR subregs).
def DPR_VFP2 : RegisterClass<"ARM", [f64, v8i8, v4i16, v2i32, v1i64, v2f32, v4f16], 64,
                             (trunc DPR, 16)> {
  let DiagnosticString = "operand must be a register in range [d0, d15]";
}

// Subset of DPR which can be used as a source of NEON scalars for 16-bit
// operations
def DPR_8 : RegisterClass<"ARM", [f64, v8i8, v4i16, v2i32, v1i64, v2f32, v4f16], 64,
                          (trunc DPR, 8)> {
  let DiagnosticString = "operand must be a register in range [d0, d7]";
}

// Generic 128-bit vector register class.
def QPR : RegisterClass<"ARM", [v16i8, v8i16, v4i32, v2i64, v4f32, v2f64, v8f16], 128,
                        (sequence "Q%u", 0, 15)> {
  // Allocate non-VFP2 aliases Q8-Q15 first.
  let AltOrders = [(rotl QPR, 8)];
  let AltOrderSelect = [{ return 1; }];
  let DiagnosticString = "operand must be a register in range [q0, q15]";
}

// Subset of QPR that have 32-bit SPR subregs.
def QPR_VFP2 : RegisterClass<"ARM", [v16i8, v8i16, v4i32, v2i64, v4f32, v2f64],
                             128, (trunc QPR, 8)> {
  let DiagnosticString = "operand must be a register in range [q0, q7]";
}

// Subset of QPR that have DPR_8 and SPR_8 subregs.
def QPR_8 : RegisterClass<"ARM", [v16i8, v8i16, v4i32, v2i64, v4f32, v2f64],
                           128, (trunc QPR, 4)> {
  let DiagnosticString = "operand must be a register in range [q0, q3]";
}

// Pseudo-registers representing odd-even pairs of D registers. The even-odd
// pairs are already represented by the Q registers.
// These are needed by NEON instructions requiring two consecutive D registers.
// There is no D31_D0 register as that is always an UNPREDICTABLE encoding.
def TuplesOE2D : RegisterTuples<[dsub_0, dsub_1],
                                [(decimate (shl DPR, 1), 2),
                                 (decimate (shl DPR, 2), 2)]>;

// Register class representing a pair of consecutive D registers.
// Use the Q registers for the even-odd pairs.
def DPair : RegisterClass<"ARM", [v16i8, v8i16, v4i32, v2i64, v4f32, v2f64],
                          128, (interleave QPR, TuplesOE2D)> {
  // Allocate starting at non-VFP2 registers D16-D31 first.
  // Prefer even-odd pairs as they are easier to copy.
  let AltOrders = [(add (rotl QPR, 8), (rotl DPair, 16))];
  let AltOrderSelect = [{ return 1; }];
}

// Pseudo-registers representing even-odd pairs of GPRs from R1 to R13/SP.
// These are needed by instructions (e.g. ldrexd/strexd) requiring even-odd GPRs.
def Tuples2R : RegisterTuples<[gsub_0, gsub_1],
                              [(add R0, R2, R4, R6, R8, R10, R12),
                               (add R1, R3, R5, R7, R9, R11, SP)]>;

// Register class representing a pair of even-odd GPRs.
def GPRPair : RegisterClass<"ARM", [untyped], 64, (add Tuples2R)> {
  let Size = 64; // 2 x 32 bits, we have no predefined type of that size.
}

// Pseudo-registers representing 3 consecutive D registers.
def Tuples3D : RegisterTuples<[dsub_0, dsub_1, dsub_2],
                              [(shl DPR, 0),
                               (shl DPR, 1),
                               (shl DPR, 2)]>;

// 3 consecutive D registers.
def DTriple : RegisterClass<"ARM", [untyped], 64, (add Tuples3D)> {
  let Size = 192; // 3 x 64 bits, we have no predefined type of that size.
}

// Pseudo 256-bit registers to represent pairs of Q registers. These should
// never be present in the emitted code.
// These are used for NEON load / store instructions, e.g., vld4, vst3.
def Tuples2Q : RegisterTuples<[qsub_0, qsub_1], [(shl QPR, 0), (shl QPR, 1)]>;

// Pseudo 256-bit vector register class to model pairs of Q registers
// (4 consecutive D registers).
def QQPR : RegisterClass<"ARM", [v4i64], 256, (add Tuples2Q)> {
  // Allocate non-VFP2 aliases first.
  let AltOrders = [(rotl QQPR, 8)];
  let AltOrderSelect = [{ return 1; }];
}

// Tuples of 4 D regs that isn't also a pair of Q regs.
def TuplesOE4D : RegisterTuples<[dsub_0, dsub_1, dsub_2, dsub_3],
                                [(decimate (shl DPR, 1), 2),
                                 (decimate (shl DPR, 2), 2),
                                 (decimate (shl DPR, 3), 2),
                                 (decimate (shl DPR, 4), 2)]>;

// 4 consecutive D registers.
def DQuad : RegisterClass<"ARM", [v4i64], 256,
                          (interleave Tuples2Q, TuplesOE4D)>;

// Pseudo 512-bit registers to represent four consecutive Q registers.
def Tuples2QQ : RegisterTuples<[qqsub_0, qqsub_1],
                               [(shl QQPR, 0), (shl QQPR, 2)]>;

// Pseudo 512-bit vector register class to model 4 consecutive Q registers
// (8 consecutive D registers).
def QQQQPR : RegisterClass<"ARM", [v8i64], 256, (add Tuples2QQ)> {
  // Allocate non-VFP2 aliases first.
  let AltOrders = [(rotl QQQQPR, 8)];
  let AltOrderSelect = [{ return 1; }];
}


// Pseudo-registers representing 2-spaced consecutive D registers.
def Tuples2DSpc : RegisterTuples<[dsub_0, dsub_2],
                                 [(shl DPR, 0),
                                  (shl DPR, 2)]>;

// Spaced pairs of D registers.
def DPairSpc : RegisterClass<"ARM", [v2i64], 64, (add Tuples2DSpc)>;

def Tuples3DSpc : RegisterTuples<[dsub_0, dsub_2, dsub_4],
                                 [(shl DPR, 0),
                                  (shl DPR, 2),
                                  (shl DPR, 4)]>;

// Spaced triples of D registers.
def DTripleSpc : RegisterClass<"ARM", [untyped], 64, (add Tuples3DSpc)> {
  let Size = 192; // 3 x 64 bits, we have no predefined type of that size.
}

def Tuples4DSpc : RegisterTuples<[dsub_0, dsub_2, dsub_4, dsub_6],
                                 [(shl DPR, 0),
                                  (shl DPR, 2),
                                  (shl DPR, 4),
                                  (shl DPR, 6)]>;

// Spaced quads of D registers.
def DQuadSpc : RegisterClass<"ARM", [v4i64], 64, (add Tuples3DSpc)>;
