//===-- HexagonRegisterInfo.td - Hexagon Register defs -----*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//  Declarations that describe the Hexagon register file.
//===----------------------------------------------------------------------===//

let Namespace = "Hexagon" in {

  class HexagonReg<bits<5> num, string n, list<string> alt = [],
                   list<Register> alias = []> : Register<n, alt> {
    let Aliases = alias;
    let HWEncoding{4-0} = num;
  }

  class HexagonDoubleReg<bits<5> num, string n, list<Register> subregs,
                         list<string> alt = []> :
        RegisterWithSubRegs<n, subregs> {
    let AltNames = alt;
    let HWEncoding{4-0} = num;
  }

  // Registers are identified with 5-bit ID numbers.
  // Ri - 32-bit integer registers.
  class Ri<bits<5> num, string n, list<string> alt = []> :
        HexagonReg<num, n, alt>;

  // Rf - 32-bit floating-point registers.
  class Rf<bits<5> num, string n> : HexagonReg<num, n>;

  // Rd - 64-bit registers.
  class Rd<bits<5> num, string n, list<Register> subregs,
           list<string> alt = []> :
        HexagonDoubleReg<num, n, subregs, alt> {
    let SubRegs = subregs;
  }

  // Rp - predicate registers
  class Rp<bits<5> num, string n> : HexagonReg<num, n>;


  // Rq - vector predicate registers
  class Rq<bits<3> num, string n> : Register<n, []> {
    let HWEncoding{2-0} = num;
  }

  // Rc - control registers
  class Rc<bits<5> num, string n,
           list<string> alt = [], list<Register> alias = []> :
        HexagonReg<num, n, alt, alias>;

  // Rcc - 64-bit control registers.
  class Rcc<bits<5> num, string n, list<Register> subregs,
            list<string> alt = []> :
        HexagonDoubleReg<num, n, subregs, alt> {
    let SubRegs = subregs;
  }

  // Mx - address modifier registers
  class Mx<bits<1> num, string n> : Register<n, []> {
    let HWEncoding{0} = num;
  }

  // Rg - Guest/Hypervisor registers
  class Rg<bits<5> num, string n,
           list<string> alt = [], list<Register> alias = []> :
        HexagonReg<num, n, alt, alias>;

  // Rgg - 64-bit Guest/Hypervisor registers
  class Rgg<bits<5> num, string n, list<Register> subregs> :
        HexagonDoubleReg<num, n, subregs> {
    let SubRegs = subregs;
  }

  def isub_lo  : SubRegIndex<32>;
  def isub_hi  : SubRegIndex<32, 32>;
  def vsub_lo  : SubRegIndex<512>;
  def vsub_hi  : SubRegIndex<512, 512>;
  def wsub_lo  : SubRegIndex<1024>;
  def wsub_hi  : SubRegIndex<1024, 1024>;
  def subreg_overflow : SubRegIndex<1, 0>;

  // Integer registers.
  foreach i = 0-28 in {
    def R#i  : Ri<i, "r"#i>,  DwarfRegNum<[i]>;
  }
  def R29 : Ri<29, "r29", ["sp"]>, DwarfRegNum<[29]>;
  def R30 : Ri<30, "r30", ["fp"]>, DwarfRegNum<[30]>;
  def R31 : Ri<31, "r31", ["lr"]>, DwarfRegNum<[31]>;

  // Aliases of the R* registers used to hold 64-bit int values (doubles).
  let SubRegIndices = [isub_lo, isub_hi], CoveredBySubRegs = 1 in {
  def D0  : Rd< 0,  "r1:0",  [R0,  R1]>,  DwarfRegNum<[32]>;
  def D1  : Rd< 2,  "r3:2",  [R2,  R3]>,  DwarfRegNum<[34]>;
  def D2  : Rd< 4,  "r5:4",  [R4,  R5]>,  DwarfRegNum<[36]>;
  def D3  : Rd< 6,  "r7:6",  [R6,  R7]>,  DwarfRegNum<[38]>;
  def D4  : Rd< 8,  "r9:8",  [R8,  R9]>,  DwarfRegNum<[40]>;
  def D5  : Rd<10, "r11:10", [R10, R11]>, DwarfRegNum<[42]>;
  def D6  : Rd<12, "r13:12", [R12, R13]>, DwarfRegNum<[44]>;
  def D7  : Rd<14, "r15:14", [R14, R15]>, DwarfRegNum<[46]>;
  def D8  : Rd<16, "r17:16", [R16, R17]>, DwarfRegNum<[48]>;
  def D9  : Rd<18, "r19:18", [R18, R19]>, DwarfRegNum<[50]>;
  def D10 : Rd<20, "r21:20", [R20, R21]>, DwarfRegNum<[52]>;
  def D11 : Rd<22, "r23:22", [R22, R23]>, DwarfRegNum<[54]>;
  def D12 : Rd<24, "r25:24", [R24, R25]>, DwarfRegNum<[56]>;
  def D13 : Rd<26, "r27:26", [R26, R27]>, DwarfRegNum<[58]>;
  def D14 : Rd<28, "r29:28", [R28, R29]>, DwarfRegNum<[60]>;
  def D15 : Rd<30, "r31:30", [R30, R31], ["lr:fp"]>, DwarfRegNum<[62]>;
  }

  // Predicate registers.
  def P0 : Rp<0, "p0">, DwarfRegNum<[63]>;
  def P1 : Rp<1, "p1">, DwarfRegNum<[64]>;
  def P2 : Rp<2, "p2">, DwarfRegNum<[65]>;
  def P3 : Rp<3, "p3">, DwarfRegNum<[66]>;

  // Fake register to represent USR.OVF bit. Artihmetic/saturating instruc-
  // tions modify this bit, and multiple such instructions are allowed in the
  // same packet. We need to ignore output dependencies on this bit, but not
  // on the entire USR.
  def USR_OVF : Rc<?, "usr.ovf">;

  def USR  : Rc<8,  "usr",       ["c8"]>,   DwarfRegNum<[75]> {
    let SubRegIndices = [subreg_overflow];
    let SubRegs = [USR_OVF];
  }

  // Control registers.
  def SA0:        Rc<0,  "sa0",        ["c0"]>,    DwarfRegNum<[67]>;
  def LC0:        Rc<1,  "lc0",        ["c1"]>,    DwarfRegNum<[68]>;
  def SA1:        Rc<2,  "sa1",        ["c2"]>,    DwarfRegNum<[69]>;
  def LC1:        Rc<3,  "lc1",        ["c3"]>,    DwarfRegNum<[70]>;
  def P3_0:       Rc<4,  "p3:0",       ["c4"], [P0, P1, P2, P3]>,
                                                   DwarfRegNum<[71]>;
  // When defining more Cn registers, make sure to explicitly mark them
  // as reserved in HexagonRegisterInfo.cpp.
  def C5:         Rc<5,  "c5",         ["c5"]>,    DwarfRegNum<[72]>;
  def M0:         Rc<6,  "m0",         ["c6"]>,    DwarfRegNum<[73]>;
  def M1:         Rc<7,  "m1",         ["c7"]>,    DwarfRegNum<[74]>;
  // Define C8 separately and make it aliased with USR.
  // The problem is that USR has subregisters (e.g. overflow). If USR was
  // specified as a subregister of C9_8, it would imply that subreg_overflow
  // and isub_lo can be composed, which leads to all kinds of issues
  // with lane masks.
  def C8:         Rc<8,  "c8",         [], [USR]>, DwarfRegNum<[75]>;
  def PC:         Rc<9,  "pc",         ["c9"]>,    DwarfRegNum<[76]>;
  def UGP:        Rc<10, "ugp",        ["c10"]>,   DwarfRegNum<[77]>;
  def GP:         Rc<11, "gp",         ["c11"]>,   DwarfRegNum<[78]>;
  def CS0:        Rc<12, "cs0",        ["c12"]>,   DwarfRegNum<[79]>;
  def CS1:        Rc<13, "cs1",        ["c13"]>,   DwarfRegNum<[80]>;
  def UPCYCLELO:  Rc<14, "upcyclelo",  ["c14"]>,   DwarfRegNum<[81]>;
  def UPCYCLEHI:  Rc<15, "upcyclehi",  ["c15"]>,   DwarfRegNum<[82]>;
  def FRAMELIMIT: Rc<16, "framelimit", ["c16"]>,   DwarfRegNum<[83]>;
  def FRAMEKEY:   Rc<17, "framekey",   ["c17"]>,   DwarfRegNum<[84]>;
  def PKTCOUNTLO: Rc<18, "pktcountlo", ["c18"]>,   DwarfRegNum<[85]>;
  def PKTCOUNTHI: Rc<19, "pktcounthi", ["c19"]>,   DwarfRegNum<[86]>;
  def UTIMERLO:   Rc<30, "utimerlo",   ["c30"]>,   DwarfRegNum<[97]>;
  def UTIMERHI:   Rc<31, "utimerhi",   ["c31"]>,   DwarfRegNum<[98]>;

  // Control registers pairs.
  let SubRegIndices = [isub_lo, isub_hi], CoveredBySubRegs = 1 in {
    def C1_0   : Rcc<0,   "c1:0",  [SA0, LC0], ["lc0:sa0"]>, DwarfRegNum<[67]>;
    def C3_2   : Rcc<2,   "c3:2",  [SA1, LC1], ["lc1:sa1"]>, DwarfRegNum<[69]>;
    def C5_4   : Rcc<4,   "c5:4",  [P3_0, C5]>,              DwarfRegNum<[71]>;
    def C7_6   : Rcc<6,   "c7:6",  [M0, M1],   ["m1:0"]>,    DwarfRegNum<[72]>;
    // Use C8 instead of USR as a subregister of C9_8.
    def C9_8   : Rcc<8,   "c9:8",  [C8, PC]>,                DwarfRegNum<[74]>;
    def C11_10 : Rcc<10, "c11:10", [UGP, GP]>,               DwarfRegNum<[76]>;
    def CS     : Rcc<12, "c13:12", [CS0, CS1], ["cs1:0"]>,   DwarfRegNum<[78]>;
    def UPCYCLE: Rcc<14, "c15:14", [UPCYCLELO, UPCYCLEHI], ["upcycle"]>,
                                                              DwarfRegNum<[80]>;
    def C17_16 : Rcc<16, "c17:16", [FRAMELIMIT, FRAMEKEY]>,  DwarfRegNum<[83]>;
    def PKTCOUNT : Rcc<18, "c19:18", [PKTCOUNTLO, PKTCOUNTHI], ["pktcount"]>,
                                                              DwarfRegNum<[85]>;
    def UTIMER :  Rcc<30, "c31:30", [UTIMERLO, UTIMERHI], ["utimer"]>,
                                                              DwarfRegNum<[97]>;
  }

  foreach i = 0-31 in {
    def V#i  : Ri<i, "v"#i>,  DwarfRegNum<[!add(i, 99)]>;
  }
  def VTMP : Ri<0, "vtmp">, DwarfRegNum<[131]>;

  // Aliases of the V* registers used to hold double vec values.
  let SubRegIndices = [vsub_lo, vsub_hi], CoveredBySubRegs = 1 in {
  def W0  : Rd< 0,  "v1:0",  [V0,  V1]>,  DwarfRegNum<[99]>;
  def W1  : Rd< 2,  "v3:2",  [V2,  V3]>,  DwarfRegNum<[101]>;
  def W2  : Rd< 4,  "v5:4",  [V4,  V5]>,  DwarfRegNum<[103]>;
  def W3  : Rd< 6,  "v7:6",  [V6,  V7]>,  DwarfRegNum<[105]>;
  def W4  : Rd< 8,  "v9:8",  [V8,  V9]>,  DwarfRegNum<[107]>;
  def W5  : Rd<10, "v11:10", [V10, V11]>, DwarfRegNum<[109]>;
  def W6  : Rd<12, "v13:12", [V12, V13]>, DwarfRegNum<[111]>;
  def W7  : Rd<14, "v15:14", [V14, V15]>, DwarfRegNum<[113]>;
  def W8  : Rd<16, "v17:16", [V16, V17]>, DwarfRegNum<[115]>;
  def W9  : Rd<18, "v19:18", [V18, V19]>, DwarfRegNum<[117]>;
  def W10 : Rd<20, "v21:20", [V20, V21]>, DwarfRegNum<[119]>;
  def W11 : Rd<22, "v23:22", [V22, V23]>, DwarfRegNum<[121]>;
  def W12 : Rd<24, "v25:24", [V24, V25]>, DwarfRegNum<[123]>;
  def W13 : Rd<26, "v27:26", [V26, V27]>, DwarfRegNum<[125]>;
  def W14 : Rd<28, "v29:28", [V28, V29]>, DwarfRegNum<[127]>;
  def W15 : Rd<30, "v31:30", [V30, V31]>, DwarfRegNum<[129]>;
  }

  // Aliases of the V* registers used to hold quad vec values.
  let SubRegIndices = [wsub_lo, wsub_hi], CoveredBySubRegs = 1 in {
  def VQ0  : Rd< 0, "v3:0",   [W0,  W1]>,  DwarfRegNum<[252]>;
  def VQ1  : Rd< 4, "v7:4",   [W2,  W3]>,  DwarfRegNum<[253]>;
  def VQ2  : Rd< 8, "v11:8",  [W4,  W5]>,  DwarfRegNum<[254]>;
  def VQ3  : Rd<12, "v15:12", [W6,  W7]>,  DwarfRegNum<[255]>;
  def VQ4  : Rd<16, "v19:16", [W8,  W9]>,  DwarfRegNum<[256]>;
  def VQ5  : Rd<20, "v23:20", [W10, W11]>, DwarfRegNum<[257]>;
  def VQ6  : Rd<24, "v27:24", [W12, W13]>, DwarfRegNum<[258]>;
  def VQ7  : Rd<28, "v31:28", [W14, W15]>, DwarfRegNum<[259]>;
  }

  // Vector Predicate registers.
  def Q0 : Rq<0, "q0">, DwarfRegNum<[131]>;
  def Q1 : Rq<1, "q1">, DwarfRegNum<[132]>;
  def Q2 : Rq<2, "q2">, DwarfRegNum<[133]>;
  def Q3 : Rq<3, "q3">, DwarfRegNum<[134]>;

  // Guest Registers
  def GELR:      Rg<0,  "gelr", ["g0"]>,       DwarfRegNum<[220]>;
  def GSR:       Rg<1,  "gsr", ["g1"]>,        DwarfRegNum<[221]>;
  def GOSP:      Rg<2,  "gosp", ["g2"]>,       DwarfRegNum<[222]>;
  def G3:        Rg<3,  "gbadva", ["g3"]>,     DwarfRegNum<[223]>;
  def G4:        Rg<4,  "g4">,                 DwarfRegNum<[224]>;
  def G5:        Rg<5,  "g5">,                 DwarfRegNum<[225]>;
  def G6:        Rg<6,  "g6">,                 DwarfRegNum<[226]>;
  def G7:        Rg<7,  "g7">,                 DwarfRegNum<[227]>;
  def G8:        Rg<8,  "g8">,                 DwarfRegNum<[228]>;
  def G9:        Rg<9,  "g9">,                 DwarfRegNum<[229]>;
  def G10:       Rg<10, "g10">,                DwarfRegNum<[230]>;
  def G11:       Rg<11, "g11">,                DwarfRegNum<[231]>;
  def G12:       Rg<12, "g12">,                DwarfRegNum<[232]>;
  def G13:       Rg<13, "g13">,                DwarfRegNum<[233]>;
  def G14:       Rg<14, "g14">,                DwarfRegNum<[234]>;
  def G15:       Rg<15, "g15">,                DwarfRegNum<[235]>;
  def GPMUCNT4:  Rg<16, "gpmucnt4", ["g16"]>,  DwarfRegNum<[236]>;
  def GPMUCNT5:  Rg<17, "gpmucnt5", ["g17"]>,  DwarfRegNum<[237]>;
  def GPMUCNT6:  Rg<18, "gpmucnt6", ["g18"]>,  DwarfRegNum<[238]>;
  def GPMUCNT7:  Rg<19, "gpmucnt7", ["g19"]>,  DwarfRegNum<[239]>;
  def G20:       Rg<20, "g20">,                DwarfRegNum<[240]>;
  def G21:       Rg<21, "g21">,                DwarfRegNum<[241]>;
  def G22:       Rg<22, "g22">,                DwarfRegNum<[242]>;
  def G23:       Rg<23, "g23">,                DwarfRegNum<[243]>;
  def GPCYCLELO: Rg<24, "gpcyclelo", ["g24"]>, DwarfRegNum<[244]>;
  def GPCYCLEHI: Rg<25, "gpcyclehi", ["g25"]>, DwarfRegNum<[245]>;
  def GPMUCNT0:  Rg<26, "gpmucnt0",  ["g26"]>, DwarfRegNum<[246]>;
  def GPMUCNT1:  Rg<27, "gpmucnt1",  ["g27"]>, DwarfRegNum<[247]>;
  def GPMUCNT2:  Rg<28, "gpmucnt2",  ["g28"]>, DwarfRegNum<[248]>;
  def GPMUCNT3:  Rg<29, "gpmucnt3",  ["g29"]>, DwarfRegNum<[249]>;
  def G30:       Rg<30, "g30">,                DwarfRegNum<[250]>;
  def G31:       Rg<31, "g31">,                DwarfRegNum<[251]>;

  // Guest Register Pairs
  let SubRegIndices = [isub_lo, isub_hi], CoveredBySubRegs = 1 in {
    def G1_0   : Rgg<0,  "g1:0",   [GELR, GSR]>,            DwarfRegNum<[220]>;
    def G3_2   : Rgg<2,  "g3:2",   [GOSP, G3]>,             DwarfRegNum<[222]>;
    def G5_4   : Rgg<4,  "g5:4",   [G4, G5]>,               DwarfRegNum<[224]>;
    def G7_6   : Rgg<6,  "g7:6",   [G6, G7]>,               DwarfRegNum<[226]>;
    def G9_8   : Rgg<8,  "g9:8",   [G8, G9]>,               DwarfRegNum<[228]>;
    def G11_10 : Rgg<10, "g11:10", [G10, G11]>,             DwarfRegNum<[230]>;
    def G13_12 : Rgg<12, "g13:12", [G12, G13]>,             DwarfRegNum<[232]>;
    def G15_14 : Rgg<14, "g15:14", [G14, G15]>,             DwarfRegNum<[234]>;
    def G17_16 : Rgg<16, "g17:16", [GPMUCNT4, GPMUCNT5]>,   DwarfRegNum<[236]>;
    def G19_18 : Rgg<18, "g19:18", [GPMUCNT6, GPMUCNT7]>,   DwarfRegNum<[238]>;
    def G21_20 : Rgg<20, "g21:20", [G20, G21]>,             DwarfRegNum<[240]>;
    def G23_22 : Rgg<22, "g23:22", [G22, G23]>,             DwarfRegNum<[242]>;
    def G25_24 : Rgg<24, "g25:24", [GPCYCLELO, GPCYCLEHI]>, DwarfRegNum<[244]>;
    def G27_26 : Rgg<26, "g27:26", [GPMUCNT0, GPMUCNT1]>,   DwarfRegNum<[246]>;
    def G29_28 : Rgg<28, "g29:28", [GPMUCNT2, GPMUCNT3]>,   DwarfRegNum<[248]>;
    def G31_30 : Rgg<30, "g31:30", [G30, G31]>,             DwarfRegNum<[250]>;
  }

}

// HVX types

def VecI1:   ValueTypeByHwMode<[Hvx64,  Hvx128,  DefaultMode],
                               [v512i1, v1024i1, v512i1]>;
def VecI8:   ValueTypeByHwMode<[Hvx64,  Hvx128,  DefaultMode],
                               [v64i8,  v128i8,  v64i8]>;
def VecI16:  ValueTypeByHwMode<[Hvx64,  Hvx128,  DefaultMode],
                               [v32i16, v64i16,  v32i16]>;
def VecI32:  ValueTypeByHwMode<[Hvx64,  Hvx128,  DefaultMode],
                               [v16i32, v32i32,  v16i32]>;

def VecPI8:  ValueTypeByHwMode<[Hvx64,  Hvx128,  DefaultMode],
                               [v128i8, v256i8,  v128i8]>;
def VecPI16: ValueTypeByHwMode<[Hvx64,  Hvx128,  DefaultMode],
                               [v64i16, v128i16, v64i16]>;
def VecPI32: ValueTypeByHwMode<[Hvx64,  Hvx128,  DefaultMode],
                               [v32i32, v64i32,  v32i32]>;

def VecQ8:   ValueTypeByHwMode<[Hvx64,  Hvx128,  DefaultMode],
                               [v64i1,  v128i1,  v64i1]>;
def VecQ16:  ValueTypeByHwMode<[Hvx64,  Hvx128,  DefaultMode],
                               [v32i1,  v64i1,   v32i1]>;
def VecQ32:  ValueTypeByHwMode<[Hvx64,  Hvx128,  DefaultMode],
                               [v16i1,  v32i1,   v16i1]>;

// HVX register classes

def HvxVR : RegisterClass<"Hexagon", [VecI8, VecI16, VecI32], 512,
  (add (sequence "V%u", 0, 31), VTMP)> {
  let RegInfos = RegInfoByHwMode<[Hvx64, Hvx128, DefaultMode],
    [RegInfo<512,512,512>, RegInfo<1024,1024,1024>, RegInfo<512,512,512>]>;
}

def HvxWR : RegisterClass<"Hexagon", [VecPI8, VecPI16, VecPI32], 1024,
  (add (sequence "W%u", 0, 15))> {
  let RegInfos = RegInfoByHwMode<[Hvx64, Hvx128, DefaultMode],
    [RegInfo<1024,1024,1024>, RegInfo<2048,2048,2048>, RegInfo<1024,1024,1024>]>;
}

def HvxQR : RegisterClass<"Hexagon", [VecI1, VecQ8, VecQ16, VecQ32], 512,
  (add Q0, Q1, Q2, Q3)> {
  let RegInfos = RegInfoByHwMode<[Hvx64, Hvx128, DefaultMode],
    [RegInfo<512,512,512>, RegInfo<1024,1024,1024>, RegInfo<512,512,512>]>;
}

def HvxVQR : RegisterClass<"Hexagon", [untyped], 2048,
  (add (sequence "VQ%u", 0, 7))> {
  let RegInfos = RegInfoByHwMode<[Hvx64, Hvx128, DefaultMode],
    [RegInfo<2048,2048,2048>, RegInfo<4096,4096,4096>, RegInfo<2048,2048,2048>]>;
}

// Core register classes

def IntRegs : RegisterClass<"Hexagon", [i32, f32, v4i8, v2i16], 32,
  (add (sequence "R%u", 0, 9), (sequence "R%u", 12, 28),
       R10, R11, R29, R30, R31)>;

// Registers are listed in reverse order for allocation preference reasons.
def GeneralSubRegs : RegisterClass<"Hexagon", [i32], 32,
  (add R23, R22, R21, R20, R19, R18, R17, R16,
       R7, R6, R5, R4, R3, R2, R1, R0)>;

def IntRegsLow8 : RegisterClass<"Hexagon", [i32], 32,
  (add R7, R6, R5, R4, R3, R2, R1, R0)> ;

def DoubleRegs : RegisterClass<"Hexagon", [i64, f64, v8i8, v4i16, v2i32], 64,
  (add (sequence "D%u", 0, 4), (sequence "D%u", 6, 13), D5, D14, D15)>;

def GeneralDoubleLow8Regs : RegisterClass<"Hexagon", [i64], 64,
  (add D11, D10, D9, D8, D3, D2, D1, D0)>;

let Size = 32 in
def PredRegs : RegisterClass<"Hexagon",
  [i1, v2i1, v4i1, v8i1, v4i8, v2i16, i32], 32, (add P0, P1, P2, P3)>;

let Size = 32 in
def ModRegs : RegisterClass<"Hexagon", [i32], 32, (add M0, M1)>;

let Size = 32, isAllocatable = 0 in
def CtrRegs : RegisterClass<"Hexagon", [i32], 32,
  (add LC0, SA0, LC1, SA1, P3_0, C5, C8, PC, UGP, GP, CS0, CS1,
       UPCYCLELO, UPCYCLEHI,
       FRAMELIMIT, FRAMEKEY, PKTCOUNTLO, PKTCOUNTHI, UTIMERLO, UTIMERHI,
       M0, M1, USR)>;

let isAllocatable = 0 in
def UsrBits : RegisterClass<"Hexagon", [i1], 0, (add USR_OVF)>;

let Size = 64, isAllocatable = 0 in
def CtrRegs64 : RegisterClass<"Hexagon", [i64], 64,
  (add C1_0, C3_2, C5_4, C7_6, C9_8, C11_10, CS, UPCYCLE, C17_16,
       PKTCOUNT, UTIMER)>;

let Size = 32, isAllocatable = 0 in
def GuestRegs : RegisterClass<"Hexagon", [i32], 32,
  (add GELR, GSR, GOSP,
       (sequence "G%u", 3, 15),
       GPMUCNT4, GPMUCNT5, GPMUCNT6, GPMUCNT7,
       G20, G21, G22, G23,
       GPCYCLELO, GPCYCLEHI, GPMUCNT0, GPMUCNT1,
       GPMUCNT2,  GPMUCNT3,
       G30, G31)>;

let Size = 64, isAllocatable = 0 in
def GuestRegs64 : RegisterClass<"Hexagon", [i64], 64,
  (add G1_0, G3_2,
       G5_4, G7_6, G9_8, G11_10, G13_12, G15_14,
       G17_16, G19_18,
       G21_20, G23_22,
       G25_24, G27_26, G29_28,
       G31_30)>;

// These registers are new for v62 and onward.
// The function RegisterMatchesArch() uses this list for validation.
let isAllocatable = 0 in
def V62Regs : RegisterClass<"Hexagon", [i32], 32,
  (add FRAMELIMIT, FRAMEKEY,   C17_16, PKTCOUNTLO, PKTCOUNTHI, PKTCOUNT,
       UTIMERLO,   UTIMERHI,   UTIMER)>;

// These registers are new for v65 and onward.
let Size = 32, isAllocatable = 0 in
def V65Regs : RegisterClass<"Hexagon", [i32], 32, (add VTMP)>;


def HexagonCSR
  : CalleeSavedRegs<(add R16, R17, R18, R19, R20, R21, R22, R23,
                         R24, R25, R26, R27)>;
