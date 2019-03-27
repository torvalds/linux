//===-- RISCVRegisterInfo.td - RISC-V Register defs --------*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//  Declarations that describe the RISC-V register files
//===----------------------------------------------------------------------===//

let Namespace = "RISCV" in {
class RISCVReg<bits<5> Enc, string n, list<string> alt = []> : Register<n> {
  let HWEncoding{4-0} = Enc;
  let AltNames = alt;
}

class RISCVReg32<bits<5> Enc, string n, list<string> alt = []> : Register<n> {
  let HWEncoding{4-0} = Enc;
  let AltNames = alt;
}

// Because RISCVReg64 register have AsmName and AltNames that alias with their
// 32-bit sub-register, RISCVAsmParser will need to coerce a register number
// from a RISCVReg32 to the equivalent RISCVReg64 when appropriate.
def sub_32 : SubRegIndex<32>;
class RISCVReg64<RISCVReg32 subreg> : Register<""> {
  let HWEncoding{4-0} = subreg.HWEncoding{4-0};
  let SubRegs = [subreg];
  let SubRegIndices = [sub_32];
  let AsmName = subreg.AsmName;
  let AltNames = subreg.AltNames;
}

def ABIRegAltName : RegAltNameIndex;
} // Namespace = "RISCV"

// Integer registers
// CostPerUse is set higher for registers that may not be compressible as they
// are not part of GPRC, the most restrictive register class used by the
// compressed instruction set. This will influence the greedy register
// allocator to reduce the use of registers that can't be encoded in 16 bit
// instructions. This affects register allocation even when compressed
// instruction isn't targeted, we see no major negative codegen impact.

let RegAltNameIndices = [ABIRegAltName] in {
  def X0  : RISCVReg<0, "x0", ["zero"]>, DwarfRegNum<[0]>;
  let CostPerUse = 1 in {
  def X1  : RISCVReg<1, "x1", ["ra"]>, DwarfRegNum<[1]>;
  def X2  : RISCVReg<2, "x2", ["sp"]>, DwarfRegNum<[2]>;
  def X3  : RISCVReg<3, "x3", ["gp"]>, DwarfRegNum<[3]>;
  def X4  : RISCVReg<4, "x4", ["tp"]>, DwarfRegNum<[4]>;
  def X5  : RISCVReg<5, "x5", ["t0"]>, DwarfRegNum<[5]>;
  def X6  : RISCVReg<6, "x6", ["t1"]>, DwarfRegNum<[6]>;
  def X7  : RISCVReg<7, "x7", ["t2"]>, DwarfRegNum<[7]>;
  }
  def X8  : RISCVReg<8, "x8", ["s0"]>, DwarfRegNum<[8]>;
  def X9  : RISCVReg<9, "x9", ["s1"]>, DwarfRegNum<[9]>;
  def X10 : RISCVReg<10,"x10", ["a0"]>, DwarfRegNum<[10]>;
  def X11 : RISCVReg<11,"x11", ["a1"]>, DwarfRegNum<[11]>;
  def X12 : RISCVReg<12,"x12", ["a2"]>, DwarfRegNum<[12]>;
  def X13 : RISCVReg<13,"x13", ["a3"]>, DwarfRegNum<[13]>;
  def X14 : RISCVReg<14,"x14", ["a4"]>, DwarfRegNum<[14]>;
  def X15 : RISCVReg<15,"x15", ["a5"]>, DwarfRegNum<[15]>;
  let CostPerUse = 1 in {
  def X16 : RISCVReg<16,"x16", ["a6"]>, DwarfRegNum<[16]>;
  def X17 : RISCVReg<17,"x17", ["a7"]>, DwarfRegNum<[17]>;
  def X18 : RISCVReg<18,"x18", ["s2"]>, DwarfRegNum<[18]>;
  def X19 : RISCVReg<19,"x19", ["s3"]>, DwarfRegNum<[19]>;
  def X20 : RISCVReg<20,"x20", ["s4"]>, DwarfRegNum<[20]>;
  def X21 : RISCVReg<21,"x21", ["s5"]>, DwarfRegNum<[21]>;
  def X22 : RISCVReg<22,"x22", ["s6"]>, DwarfRegNum<[22]>;
  def X23 : RISCVReg<23,"x23", ["s7"]>, DwarfRegNum<[23]>;
  def X24 : RISCVReg<24,"x24", ["s8"]>, DwarfRegNum<[24]>;
  def X25 : RISCVReg<25,"x25", ["s9"]>, DwarfRegNum<[25]>;
  def X26 : RISCVReg<26,"x26", ["s10"]>, DwarfRegNum<[26]>;
  def X27 : RISCVReg<27,"x27", ["s11"]>, DwarfRegNum<[27]>;
  def X28 : RISCVReg<28,"x28", ["t3"]>, DwarfRegNum<[28]>;
  def X29 : RISCVReg<29,"x29", ["t4"]>, DwarfRegNum<[29]>;
  def X30 : RISCVReg<30,"x30", ["t5"]>, DwarfRegNum<[30]>;
  def X31 : RISCVReg<31,"x31", ["t6"]>, DwarfRegNum<[31]>;
  }
}

def XLenVT : ValueTypeByHwMode<[RV32, RV64, DefaultMode],
                               [i32,  i64,  i32]>;

// The order of registers represents the preferred allocation sequence.
// Registers are listed in the order caller-save, callee-save, specials.
def GPR : RegisterClass<"RISCV", [XLenVT], 32, (add
    (sequence "X%u", 10, 17),
    (sequence "X%u", 5, 7),
    (sequence "X%u", 28, 31),
    (sequence "X%u", 8, 9),
    (sequence "X%u", 18, 27),
    (sequence "X%u", 0, 4)
  )> {
  let RegInfos = RegInfoByHwMode<
      [RV32,              RV64,              DefaultMode],
      [RegInfo<32,32,32>, RegInfo<64,64,64>, RegInfo<32,32,32>]>;
}

// The order of registers represents the preferred allocation sequence.
// Registers are listed in the order caller-save, callee-save, specials.
def GPRNoX0 : RegisterClass<"RISCV", [XLenVT], 32, (add
    (sequence "X%u", 10, 17),
    (sequence "X%u", 5, 7),
    (sequence "X%u", 28, 31),
    (sequence "X%u", 8, 9),
    (sequence "X%u", 18, 27),
    (sequence "X%u", 1, 4)
  )> {
  let RegInfos = RegInfoByHwMode<
      [RV32,              RV64,              DefaultMode],
      [RegInfo<32,32,32>, RegInfo<64,64,64>, RegInfo<32,32,32>]>;
}

def GPRNoX0X2 : RegisterClass<"RISCV", [XLenVT], 32, (add
    (sequence "X%u", 10, 17),
    (sequence "X%u", 5, 7),
    (sequence "X%u", 28, 31),
    (sequence "X%u", 8, 9),
    (sequence "X%u", 18, 27),
    X1, X3, X4
  )> {
  let RegInfos = RegInfoByHwMode<
      [RV32,              RV64,              DefaultMode],
      [RegInfo<32,32,32>, RegInfo<64,64,64>, RegInfo<32,32,32>]>;
}

def GPRC : RegisterClass<"RISCV", [XLenVT], 32, (add
    (sequence "X%u", 10, 15),
    (sequence "X%u", 8, 9)
  )> {
  let RegInfos = RegInfoByHwMode<
      [RV32,              RV64,              DefaultMode],
      [RegInfo<32,32,32>, RegInfo<64,64,64>, RegInfo<32,32,32>]>;
}

// For indirect tail calls, we can't use callee-saved registers, as they are
// restored to the saved value before the tail call, which would clobber a call
// address.
def GPRTC : RegisterClass<"RISCV", [XLenVT], 32, (add
    (sequence "X%u", 5, 7),
    (sequence "X%u", 10, 17),
    (sequence "X%u", 28, 31)
  )> {
  let RegInfos = RegInfoByHwMode<
      [RV32,              RV64,              DefaultMode],
      [RegInfo<32,32,32>, RegInfo<64,64,64>, RegInfo<32,32,32>]>;
}

def SP : RegisterClass<"RISCV", [XLenVT], 32, (add X2)> {
  let RegInfos = RegInfoByHwMode<
      [RV32,              RV64,              DefaultMode],
      [RegInfo<32,32,32>, RegInfo<64,64,64>, RegInfo<32,32,32>]>;
}

// Floating point registers
let RegAltNameIndices = [ABIRegAltName] in {
  def F0_32  : RISCVReg32<0, "f0", ["ft0"]>, DwarfRegNum<[32]>;
  def F1_32  : RISCVReg32<1, "f1", ["ft1"]>, DwarfRegNum<[33]>;
  def F2_32  : RISCVReg32<2, "f2", ["ft2"]>, DwarfRegNum<[34]>;
  def F3_32  : RISCVReg32<3, "f3", ["ft3"]>, DwarfRegNum<[35]>;
  def F4_32  : RISCVReg32<4, "f4", ["ft4"]>, DwarfRegNum<[36]>;
  def F5_32  : RISCVReg32<5, "f5", ["ft5"]>, DwarfRegNum<[37]>;
  def F6_32  : RISCVReg32<6, "f6", ["ft6"]>, DwarfRegNum<[38]>;
  def F7_32  : RISCVReg32<7, "f7", ["ft7"]>, DwarfRegNum<[39]>;
  def F8_32  : RISCVReg32<8, "f8", ["fs0"]>, DwarfRegNum<[40]>;
  def F9_32  : RISCVReg32<9, "f9", ["fs1"]>, DwarfRegNum<[41]>;
  def F10_32 : RISCVReg32<10,"f10", ["fa0"]>, DwarfRegNum<[42]>;
  def F11_32 : RISCVReg32<11,"f11", ["fa1"]>, DwarfRegNum<[43]>;
  def F12_32 : RISCVReg32<12,"f12", ["fa2"]>, DwarfRegNum<[44]>;
  def F13_32 : RISCVReg32<13,"f13", ["fa3"]>, DwarfRegNum<[45]>;
  def F14_32 : RISCVReg32<14,"f14", ["fa4"]>, DwarfRegNum<[46]>;
  def F15_32 : RISCVReg32<15,"f15", ["fa5"]>, DwarfRegNum<[47]>;
  def F16_32 : RISCVReg32<16,"f16", ["fa6"]>, DwarfRegNum<[48]>;
  def F17_32 : RISCVReg32<17,"f17", ["fa7"]>, DwarfRegNum<[49]>;
  def F18_32 : RISCVReg32<18,"f18", ["fs2"]>, DwarfRegNum<[50]>;
  def F19_32 : RISCVReg32<19,"f19", ["fs3"]>, DwarfRegNum<[51]>;
  def F20_32 : RISCVReg32<20,"f20", ["fs4"]>, DwarfRegNum<[52]>;
  def F21_32 : RISCVReg32<21,"f21", ["fs5"]>, DwarfRegNum<[53]>;
  def F22_32 : RISCVReg32<22,"f22", ["fs6"]>, DwarfRegNum<[54]>;
  def F23_32 : RISCVReg32<23,"f23", ["fs7"]>, DwarfRegNum<[55]>;
  def F24_32 : RISCVReg32<24,"f24", ["fs8"]>, DwarfRegNum<[56]>;
  def F25_32 : RISCVReg32<25,"f25", ["fs9"]>, DwarfRegNum<[57]>;
  def F26_32 : RISCVReg32<26,"f26", ["fs10"]>, DwarfRegNum<[58]>;
  def F27_32 : RISCVReg32<27,"f27", ["fs11"]>, DwarfRegNum<[59]>;
  def F28_32 : RISCVReg32<28,"f28", ["ft8"]>, DwarfRegNum<[60]>;
  def F29_32 : RISCVReg32<29,"f29", ["ft9"]>, DwarfRegNum<[61]>;
  def F30_32 : RISCVReg32<30,"f30", ["ft10"]>, DwarfRegNum<[62]>;
  def F31_32 : RISCVReg32<31,"f31", ["ft11"]>, DwarfRegNum<[63]>;

  foreach Index = 0-31 in {
    def F#Index#_64 : RISCVReg64<!cast<RISCVReg32>("F"#Index#"_32")>,
      DwarfRegNum<[!add(Index, 32)]>;
  }
}

// The order of registers represents the preferred allocation sequence,
// meaning caller-save regs are listed before callee-save.
def FPR32 : RegisterClass<"RISCV", [f32], 32, (add
    (sequence "F%u_32", 0, 7),
    (sequence "F%u_32", 10, 17),
    (sequence "F%u_32", 28, 31),
    (sequence "F%u_32", 8, 9),
    (sequence "F%u_32", 18, 27)
)>;

def FPR32C : RegisterClass<"RISCV", [f32], 32, (add
  (sequence "F%u_32", 10, 15),
  (sequence "F%u_32", 8, 9)
)>;

// The order of registers represents the preferred allocation sequence,
// meaning caller-save regs are listed before callee-save.
def FPR64 : RegisterClass<"RISCV", [f64], 64, (add
    (sequence "F%u_64", 0, 7),
    (sequence "F%u_64", 10, 17),
    (sequence "F%u_64", 28, 31),
    (sequence "F%u_64", 8, 9),
    (sequence "F%u_64", 18, 27)
)>;

def FPR64C : RegisterClass<"RISCV", [f64], 64, (add
  (sequence "F%u_64", 10, 15),
  (sequence "F%u_64", 8, 9)
)>;
