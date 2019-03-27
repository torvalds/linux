//===-- PPCRegisterInfo.td - The PowerPC Register File -----*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

let Namespace = "PPC" in {
def sub_lt : SubRegIndex<1>;
def sub_gt : SubRegIndex<1, 1>;
def sub_eq : SubRegIndex<1, 2>;
def sub_un : SubRegIndex<1, 3>;
def sub_32 : SubRegIndex<32>;
def sub_64 : SubRegIndex<64>;
}


class PPCReg<string n> : Register<n> {
  let Namespace = "PPC";
}

// We identify all our registers with a 5-bit ID, for consistency's sake.

// GPR - One of the 32 32-bit general-purpose registers
class GPR<bits<5> num, string n> : PPCReg<n> {
  let HWEncoding{4-0} = num;
}

// GP8 - One of the 32 64-bit general-purpose registers
class GP8<GPR SubReg, string n> : PPCReg<n> {
  let HWEncoding = SubReg.HWEncoding;
  let SubRegs = [SubReg];
  let SubRegIndices = [sub_32];
}

// SPE - One of the 32 64-bit general-purpose registers (SPE)
class SPE<GPR SubReg, string n> : PPCReg<n> {
  let HWEncoding = SubReg.HWEncoding;
  let SubRegs = [SubReg];
  let SubRegIndices = [sub_32];
}

// SPR - One of the 32-bit special-purpose registers
class SPR<bits<10> num, string n> : PPCReg<n> {
  let HWEncoding{9-0} = num;
}

// FPR - One of the 32 64-bit floating-point registers
class FPR<bits<5> num, string n> : PPCReg<n> {
  let HWEncoding{4-0} = num;
}

// QFPR - One of the 32 256-bit floating-point vector registers (used for QPX)
class QFPR<FPR SubReg, string n> : PPCReg<n> {
  let HWEncoding = SubReg.HWEncoding;
  let SubRegs = [SubReg];
  let SubRegIndices = [sub_64];
}

// VF - One of the 32 64-bit floating-point subregisters of the vector
// registers (used by VSX).
class VF<bits<5> num, string n> : PPCReg<n> {
  let HWEncoding{4-0} = num;
  let HWEncoding{5} = 1;
}

// VR - One of the 32 128-bit vector registers
class VR<VF SubReg, string n> : PPCReg<n> {
  let HWEncoding{4-0} = SubReg.HWEncoding{4-0};
  let HWEncoding{5} = 0;
  let SubRegs = [SubReg];
  let SubRegIndices = [sub_64];
}

// VSRL - One of the 32 128-bit VSX registers that overlap with the scalar
// floating-point registers.
class VSRL<FPR SubReg, string n> : PPCReg<n> {
  let HWEncoding = SubReg.HWEncoding;
  let SubRegs = [SubReg];
  let SubRegIndices = [sub_64];
}

// VSXReg - One of the VSX registers in the range vs32-vs63 with numbering
// and encoding to match.
class VSXReg<bits<6> num, string n> : PPCReg<n> {
  let HWEncoding{5-0} = num;
}

// CR - One of the 8 4-bit condition registers
class CR<bits<3> num, string n, list<Register> subregs> : PPCReg<n> {
  let HWEncoding{2-0} = num;
  let SubRegs = subregs;
}

// CRBIT - One of the 32 1-bit condition register fields
class CRBIT<bits<5> num, string n> : PPCReg<n> {
  let HWEncoding{4-0} = num;
}

// General-purpose registers
foreach Index = 0-31 in {
  def R#Index : GPR<Index, "r"#Index>, DwarfRegNum<[-2, Index]>;
}

// 64-bit General-purpose registers
foreach Index = 0-31 in {
  def X#Index : GP8<!cast<GPR>("R"#Index), "r"#Index>,
                    DwarfRegNum<[Index, -2]>;
}

// SPE registers
foreach Index = 0-31 in {
  def S#Index : SPE<!cast<GPR>("R"#Index), "r"#Index>,
                    DwarfRegNum<[!add(Index, 1200), !add(Index, 1200)]>;
}

// Floating-point registers
foreach Index = 0-31 in {
  def F#Index : FPR<Index, "f"#Index>,
                DwarfRegNum<[!add(Index, 32), !add(Index, 32)]>;
}

// 64-bit Floating-point subregisters of Altivec registers
// Note: the register names are v0-v31 or vs32-vs63 depending on the use.
//       Custom C++ code is used to produce the correct name and encoding.
foreach Index = 0-31 in {
  def VF#Index : VF<Index, "v" #Index>,
                 DwarfRegNum<[!add(Index, 77), !add(Index, 77)]>;
}

// QPX Floating-point registers
foreach Index = 0-31 in {
  def QF#Index : QFPR<!cast<FPR>("F"#Index), "q"#Index>,
                 DwarfRegNum<[!add(Index, 32), !add(Index, 32)]>;
}

// Vector registers
foreach Index = 0-31 in {
  def V#Index : VR<!cast<VF>("VF"#Index), "v"#Index>,
                DwarfRegNum<[!add(Index, 77), !add(Index, 77)]>;
}

// VSX registers
foreach Index = 0-31 in {
  def VSL#Index : VSRL<!cast<FPR>("F"#Index), "vs"#Index>,
                  DwarfRegAlias<!cast<FPR>("F"#Index)>;
}

// Dummy VSX registers, this defines string: "vs32"-"vs63", and is only used for
// asm printing.
foreach Index = 32-63 in {
  def VSX#Index : VSXReg<Index, "vs"#Index>;
}

// The reprsentation of r0 when treated as the constant 0.
def ZERO  : GPR<0, "0">,    DwarfRegAlias<R0>;
def ZERO8 : GP8<ZERO, "0">, DwarfRegAlias<X0>;

// Representations of the frame pointer used by ISD::FRAMEADDR.
def FP   : GPR<0 /* arbitrary */, "**FRAME POINTER**">;
def FP8  : GP8<FP, "**FRAME POINTER**">;

// Representations of the base pointer used by setjmp.
def BP   : GPR<0 /* arbitrary */, "**BASE POINTER**">;
def BP8  : GP8<BP, "**BASE POINTER**">;

// Condition register bits
def CR0LT : CRBIT< 0, "0">;
def CR0GT : CRBIT< 1, "1">;
def CR0EQ : CRBIT< 2, "2">;
def CR0UN : CRBIT< 3, "3">;
def CR1LT : CRBIT< 4, "4">;
def CR1GT : CRBIT< 5, "5">;
def CR1EQ : CRBIT< 6, "6">;
def CR1UN : CRBIT< 7, "7">;
def CR2LT : CRBIT< 8, "8">;
def CR2GT : CRBIT< 9, "9">;
def CR2EQ : CRBIT<10, "10">;
def CR2UN : CRBIT<11, "11">;
def CR3LT : CRBIT<12, "12">;
def CR3GT : CRBIT<13, "13">;
def CR3EQ : CRBIT<14, "14">;
def CR3UN : CRBIT<15, "15">;
def CR4LT : CRBIT<16, "16">;
def CR4GT : CRBIT<17, "17">;
def CR4EQ : CRBIT<18, "18">;
def CR4UN : CRBIT<19, "19">;
def CR5LT : CRBIT<20, "20">;
def CR5GT : CRBIT<21, "21">;
def CR5EQ : CRBIT<22, "22">;
def CR5UN : CRBIT<23, "23">;
def CR6LT : CRBIT<24, "24">;
def CR6GT : CRBIT<25, "25">;
def CR6EQ : CRBIT<26, "26">;
def CR6UN : CRBIT<27, "27">;
def CR7LT : CRBIT<28, "28">;
def CR7GT : CRBIT<29, "29">;
def CR7EQ : CRBIT<30, "30">;
def CR7UN : CRBIT<31, "31">;

// Condition registers
let SubRegIndices = [sub_lt, sub_gt, sub_eq, sub_un] in {
def CR0 : CR<0, "cr0", [CR0LT, CR0GT, CR0EQ, CR0UN]>, DwarfRegNum<[68, 68]>;
def CR1 : CR<1, "cr1", [CR1LT, CR1GT, CR1EQ, CR1UN]>, DwarfRegNum<[69, 69]>;
def CR2 : CR<2, "cr2", [CR2LT, CR2GT, CR2EQ, CR2UN]>, DwarfRegNum<[70, 70]>;
def CR3 : CR<3, "cr3", [CR3LT, CR3GT, CR3EQ, CR3UN]>, DwarfRegNum<[71, 71]>;
def CR4 : CR<4, "cr4", [CR4LT, CR4GT, CR4EQ, CR4UN]>, DwarfRegNum<[72, 72]>;
def CR5 : CR<5, "cr5", [CR5LT, CR5GT, CR5EQ, CR5UN]>, DwarfRegNum<[73, 73]>;
def CR6 : CR<6, "cr6", [CR6LT, CR6GT, CR6EQ, CR6UN]>, DwarfRegNum<[74, 74]>;
def CR7 : CR<7, "cr7", [CR7LT, CR7GT, CR7EQ, CR7UN]>, DwarfRegNum<[75, 75]>;
}

// Link register
def LR  : SPR<8, "lr">, DwarfRegNum<[-2, 65]>;
//let Aliases = [LR] in
def LR8 : SPR<8, "lr">, DwarfRegNum<[65, -2]>;

// Count register
def CTR  : SPR<9, "ctr">, DwarfRegNum<[-2, 66]>;
def CTR8 : SPR<9, "ctr">, DwarfRegNum<[66, -2]>;

// VRsave register
def VRSAVE: SPR<256, "vrsave">, DwarfRegNum<[109]>;

// SPE extra registers
// SPE Accumulator for multiply-accumulate SPE operations.  Never directly
// accessed, so there's no real encoding for it.
def SPEACC: DwarfRegNum<[99, 111]>;
def SPEFSCR: SPR<512, "spefscr">, DwarfRegNum<[612, 112]>;

def XER: SPR<1, "xer">, DwarfRegNum<[76]>;

// Carry bit.  In the architecture this is really bit 0 of the XER register
// (which really is SPR register 1);  this is the only bit interesting to a
// compiler.
def CARRY: SPR<1, "xer">, DwarfRegNum<[76]> {
  let Aliases = [XER];
}

// FP rounding mode:  bits 30 and 31 of the FP status and control register
// This is not allocated as a normal register; it appears only in
// Uses and Defs.  The ABI says it needs to be preserved by a function,
// but this is not achieved by saving and restoring it as with
// most registers, it has to be done in code; to make this work all the
// return and call instructions are described as Uses of RM, so instructions
// that do nothing but change RM will not get deleted.
def RM: PPCReg<"**ROUNDING MODE**">;

/// Register classes
// Allocate volatiles first
// then nonvolatiles in reverse order since stmw/lmw save from rN to r31
def GPRC : RegisterClass<"PPC", [i32], 32, (add (sequence "R%u", 2, 12),
                                                (sequence "R%u", 30, 13),
                                                R31, R0, R1, FP, BP)> {
  // On non-Darwin PPC64 systems, R2 can be allocated, but must be restored, so
  // put it at the end of the list.
  let AltOrders = [(add (sub GPRC, R2), R2)];
  let AltOrderSelect = [{
    const PPCSubtarget &S = MF.getSubtarget<PPCSubtarget>();
    return S.isPPC64() && S.isSVR4ABI();
  }];
}

def G8RC : RegisterClass<"PPC", [i64], 64, (add (sequence "X%u", 2, 12),
                                                (sequence "X%u", 30, 14),
                                                X31, X13, X0, X1, FP8, BP8)> {
  // On non-Darwin PPC64 systems, R2 can be allocated, but must be restored, so
  // put it at the end of the list.
  let AltOrders = [(add (sub G8RC, X2), X2)];
  let AltOrderSelect = [{
    const PPCSubtarget &S = MF.getSubtarget<PPCSubtarget>();
    return S.isPPC64() && S.isSVR4ABI();
  }];
}

// For some instructions r0 is special (representing the value 0 instead of
// the value in the r0 register), and we use these register subclasses to
// prevent r0 from being allocated for use by those instructions.
def GPRC_NOR0 : RegisterClass<"PPC", [i32], 32, (add (sub GPRC, R0), ZERO)> {
  // On non-Darwin PPC64 systems, R2 can be allocated, but must be restored, so
  // put it at the end of the list.
  let AltOrders = [(add (sub GPRC_NOR0, R2), R2)];
  let AltOrderSelect = [{
    const PPCSubtarget &S = MF.getSubtarget<PPCSubtarget>();
    return S.isPPC64() && S.isSVR4ABI();
  }];
}

def G8RC_NOX0 : RegisterClass<"PPC", [i64], 64, (add (sub G8RC, X0), ZERO8)> {
  // On non-Darwin PPC64 systems, R2 can be allocated, but must be restored, so
  // put it at the end of the list.
  let AltOrders = [(add (sub G8RC_NOX0, X2), X2)];
  let AltOrderSelect = [{
    const PPCSubtarget &S = MF.getSubtarget<PPCSubtarget>();
    return S.isPPC64() && S.isSVR4ABI();
  }];
}

def SPERC : RegisterClass<"PPC", [f64], 64, (add (sequence "S%u", 2, 12),
                                                (sequence "S%u", 30, 13),
                                                S31, S0, S1)>;

def SPE4RC : RegisterClass<"PPC", [f32], 32, (add GPRC)>;

// Allocate volatiles first, then non-volatiles in reverse order. With the SVR4
// ABI the size of the Floating-point register save area is determined by the
// allocated non-volatile register with the lowest register number, as FP
// register N is spilled to offset 8 * (32 - N) below the back chain word of the
// previous stack frame. By allocating non-volatiles in reverse order we make
// sure that the Floating-point register save area is always as small as
// possible because there aren't any unused spill slots.
def F8RC : RegisterClass<"PPC", [f64], 64, (add (sequence "F%u", 0, 13),
                                                (sequence "F%u", 31, 14))>;
def F4RC : RegisterClass<"PPC", [f32], 32, (add F8RC)>;

def VRRC : RegisterClass<"PPC",
                         [v16i8,v8i16,v4i32,v2i64,v1i128,v4f32,v2f64, f128],
                         128,
                         (add V2, V3, V4, V5, V0, V1, V6, V7, V8, V9, V10, V11,
                             V12, V13, V14, V15, V16, V17, V18, V19, V31, V30,
                             V29, V28, V27, V26, V25, V24, V23, V22, V21, V20)>;

// VSX register classes (the allocation order mirrors that of the corresponding
// subregister classes).
def VSLRC : RegisterClass<"PPC", [v4i32,v4f32,v2f64,v2i64], 128,
                          (add (sequence "VSL%u", 0, 13),
                               (sequence "VSL%u", 31, 14))>;
def VSRC  : RegisterClass<"PPC", [v4i32,v4f32,v2f64,v2i64], 128,
                          (add VSLRC, VRRC)>;

// Register classes for the 64-bit "scalar" VSX subregisters.
def VFRC :  RegisterClass<"PPC", [f64], 64,
                          (add VF2, VF3, VF4, VF5, VF0, VF1, VF6, VF7,
                               VF8, VF9, VF10, VF11, VF12, VF13, VF14,
                               VF15, VF16, VF17, VF18, VF19, VF31, VF30,
                               VF29, VF28, VF27, VF26, VF25, VF24, VF23,
                               VF22, VF21, VF20)>;
def VSFRC : RegisterClass<"PPC", [f64], 64, (add F8RC, VFRC)>;

// Allow spilling GPR's into caller-saved VSR's.
def SPILLTOVSRRC : RegisterClass<"PPC", [i64, f64], 64, (add G8RC, (sub VSFRC,
				(sequence "VF%u", 31, 20),
				(sequence "F%u", 31, 14)))>;

// Register class for single precision scalars in VSX registers
def VSSRC : RegisterClass<"PPC", [f32], 32, (add VSFRC)>;

// For QPX
def QFRC : RegisterClass<"PPC", [v4f64], 256, (add (sequence "QF%u", 0, 13),
                                                (sequence "QF%u", 31, 14))>;
def QSRC : RegisterClass<"PPC", [v4f32], 128, (add QFRC)>;
def QBRC : RegisterClass<"PPC", [v4i1], 256, (add QFRC)> {
  // These are actually stored as floating-point values where a positive
  // number is true and anything else (including NaN) is false.
  let Size = 256;
}

def CRBITRC : RegisterClass<"PPC", [i1], 32,
  (add CR2LT, CR2GT, CR2EQ, CR2UN,
       CR3LT, CR3GT, CR3EQ, CR3UN,
       CR4LT, CR4GT, CR4EQ, CR4UN,
       CR5LT, CR5GT, CR5EQ, CR5UN,
       CR6LT, CR6GT, CR6EQ, CR6UN,
       CR7LT, CR7GT, CR7EQ, CR7UN,
       CR1LT, CR1GT, CR1EQ, CR1UN,
       CR0LT, CR0GT, CR0EQ, CR0UN)> {
  let Size = 32;
}

def CRRC : RegisterClass<"PPC", [i32], 32, (add CR0, CR1, CR5, CR6,
                                                CR7, CR2, CR3, CR4)>;

def CRRC0 : RegisterClass<"PPC", [i32], 32, (add CR0)>;

// The CTR registers are not allocatable because they're used by the
// decrement-and-branch instructions, and thus need to stay live across
// multiple basic blocks.
def CTRRC : RegisterClass<"PPC", [i32], 32, (add CTR)> {
  let isAllocatable = 0;
}
def CTRRC8 : RegisterClass<"PPC", [i64], 64, (add CTR8)> {
  let isAllocatable = 0;
}

def VRSAVERC : RegisterClass<"PPC", [i32], 32, (add VRSAVE)>;
def CARRYRC : RegisterClass<"PPC", [i32], 32, (add CARRY, XER)> {
  let CopyCost = -1;
}

