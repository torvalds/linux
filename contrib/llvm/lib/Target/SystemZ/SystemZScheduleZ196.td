//=- SystemZScheduleZ196.td - SystemZ Scheduling Definitions ---*- tblgen -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the machine model for Z196 to support instruction
// scheduling and other instruction cost heuristics.
//
// Pseudos expanded right after isel do not need to be modelled here.
//
//===----------------------------------------------------------------------===//

def Z196Model : SchedMachineModel {

    let UnsupportedFeatures = Arch9UnsupportedFeatures.List;
    
    let IssueWidth = 3;
    let MicroOpBufferSize = 40;     // Issue queues
    let LoadLatency = 1;            // Optimistic load latency.

    let PostRAScheduler = 1;

    // Extra cycles for a mispredicted branch.
    let MispredictPenalty = 16;
}

let SchedModel = Z196Model in  {
// These definitions need the SchedModel value. They could be put in a
// subtarget common include file, but it seems the include system in Tablegen
// currently (2016) rejects multiple includes of same file.

// Decoder grouping rules
let NumMicroOps = 1 in {
  def : WriteRes<NormalGr, []>;
  def : WriteRes<BeginGroup, []> { let BeginGroup  = 1; }
  def : WriteRes<EndGroup, []>   { let EndGroup    = 1; }
}
def : WriteRes<GroupAlone, []> {
  let NumMicroOps = 3;
  let BeginGroup  = 1;
  let EndGroup    = 1;
}
def : WriteRes<GroupAlone2, []> {
  let NumMicroOps = 6;
  let BeginGroup  = 1;
  let EndGroup    = 1;
}
def : WriteRes<GroupAlone3, []> {
  let NumMicroOps = 9;
  let BeginGroup  = 1;
  let EndGroup    = 1;
}

// Incoming latency removed from the register operand which is used together
// with a memory operand by the instruction.
def : ReadAdvance<RegReadAdv, 4>;

// LoadLatency (above) is not used for instructions in this file. This is
// instead the role of LSULatency, which is the latency value added to the
// result of loads and instructions with folded memory operands.
def : WriteRes<LSULatency, []> { let Latency = 4; let NumMicroOps = 0; }

let NumMicroOps = 0 in {
  foreach L = 1-30 in {
    def : WriteRes<!cast<SchedWrite>("WLat"#L), []> { let Latency = L; }
  }
}

// Execution units.
def Z196_FXUnit : ProcResource<2>;
def Z196_LSUnit : ProcResource<2>;
def Z196_FPUnit : ProcResource<1>;
def Z196_DFUnit : ProcResource<1>;
def Z196_MCD    : ProcResource<1>;

// Subtarget specific definitions of scheduling resources.
let NumMicroOps = 0 in {
  def : WriteRes<FXU, [Z196_FXUnit]>;
  def : WriteRes<LSU, [Z196_LSUnit]>;
  def : WriteRes<FPU, [Z196_FPUnit]>;
  def : WriteRes<DFU, [Z196_DFUnit]>;
  foreach Num = 2-6 in { let ResourceCycles = [Num] in {
    def : WriteRes<!cast<SchedWrite>("FXU"#Num), [Z196_FXUnit]>;
    def : WriteRes<!cast<SchedWrite>("LSU"#Num), [Z196_LSUnit]>;
    def : WriteRes<!cast<SchedWrite>("FPU"#Num), [Z196_FPUnit]>;
    def : WriteRes<!cast<SchedWrite>("DFU"#Num), [Z196_DFUnit]>;
  }}
}

def : WriteRes<MCD, [Z196_MCD]> { let NumMicroOps = 3;
                                  let BeginGroup  = 1;
                                  let EndGroup    = 1; }

// -------------------------- INSTRUCTIONS ---------------------------------- //

// InstRW constructs have been used in order to preserve the
// readability of the InstrInfo files.

// For each instruction, as matched by a regexp, provide a list of
// resources that it needs. These will be combined into a SchedClass.

//===----------------------------------------------------------------------===//
// Stack allocation
//===----------------------------------------------------------------------===//

def : InstRW<[WLat1, FXU, NormalGr], (instregex "ADJDYNALLOC$")>; // Pseudo -> LA / LAY

//===----------------------------------------------------------------------===//
// Branch instructions
//===----------------------------------------------------------------------===//

// Branch
def : InstRW<[WLat1, LSU, EndGroup], (instregex "(Call)?BRC(L)?(Asm.*)?$")>;
def : InstRW<[WLat1, LSU, EndGroup], (instregex "(Call)?J(G)?(Asm.*)?$")>;
def : InstRW<[WLat1, LSU, EndGroup], (instregex "(Call)?BC(R)?(Asm.*)?$")>;
def : InstRW<[WLat1, LSU, EndGroup], (instregex "(Call)?B(R)?(Asm.*)?$")>;
def : InstRW<[WLat1, FXU, LSU, GroupAlone], (instregex "BRCT(G|H)?$")>;
def : InstRW<[WLat1, FXU, LSU, GroupAlone], (instregex "BCT(G)?(R)?$")>;
def : InstRW<[WLat1, FXU3, LSU, GroupAlone2],
             (instregex "B(R)?X(H|L).*$")>;

// Compare and branch
def : InstRW<[WLat1, FXU, LSU, GroupAlone],
             (instregex "C(L)?(G)?(I|R)J(Asm.*)?$")>;
def : InstRW<[WLat1, FXU, LSU, GroupAlone],
             (instregex "C(L)?(G)?(I|R)B(Call|Return|Asm.*)?$")>;

//===----------------------------------------------------------------------===//
// Trap instructions
//===----------------------------------------------------------------------===//

// Trap
def : InstRW<[WLat1, LSU, EndGroup], (instregex "(Cond)?Trap$")>;

// Compare and trap
def : InstRW<[WLat1, FXU, NormalGr], (instregex "C(G)?(I|R)T(Asm.*)?$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "CL(G)?RT(Asm.*)?$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "CL(F|G)IT(Asm.*)?$")>;

//===----------------------------------------------------------------------===//
// Call and return instructions
//===----------------------------------------------------------------------===//

// Call
def : InstRW<[WLat1, LSU, FXU2, GroupAlone], (instregex "(Call)?BRAS$")>;
def : InstRW<[WLat1, LSU, FXU2, GroupAlone], (instregex "(Call)?BRASL$")>;
def : InstRW<[WLat1, LSU, FXU2, GroupAlone], (instregex "(Call)?BAS(R)?$")>;
def : InstRW<[WLat1, LSU, FXU2, GroupAlone], (instregex "TLS_(G|L)DCALL$")>;

// Return
def : InstRW<[WLat1, LSU, EndGroup], (instregex "Return$")>;
def : InstRW<[WLat1, LSU, EndGroup], (instregex "CondReturn$")>;

//===----------------------------------------------------------------------===//
// Move instructions
//===----------------------------------------------------------------------===//

// Moves
def : InstRW<[WLat1, FXU, LSU, NormalGr], (instregex "MV(G|H)?HI$")>;
def : InstRW<[WLat1, FXU, LSU, NormalGr], (instregex "MVI(Y)?$")>;

// Move character
def : InstRW<[WLat1, FXU, LSU3, GroupAlone], (instregex "MVC$")>;
def : InstRW<[WLat30, WLat30, WLat30, MCD], (instregex "MVCL(E|U)?$")>;

// Pseudo -> reg move
def : InstRW<[WLat1, FXU, NormalGr], (instregex "COPY(_TO_REGCLASS)?$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "EXTRACT_SUBREG$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "INSERT_SUBREG$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "REG_SEQUENCE$")>;

// Loads
def : InstRW<[LSULatency, LSU, NormalGr], (instregex "L(Y|FH|RL|Mux)?$")>;
def : InstRW<[LSULatency, LSU, NormalGr], (instregex "LG(RL)?$")>;
def : InstRW<[LSULatency, LSU, NormalGr], (instregex "L128$")>;

def : InstRW<[WLat1, FXU, NormalGr], (instregex "LLIH(F|H|L)$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "LLIL(F|H|L)$")>;

def : InstRW<[WLat1, FXU, NormalGr], (instregex "LG(F|H)I$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "LHI(Mux)?$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "LR(Mux)?$")>;

// Load and test
def : InstRW<[WLat1LSU, WLat1LSU, LSU, FXU, NormalGr], (instregex "LT(G)?$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "LT(G)?R$")>;

// Stores
def : InstRW<[WLat1, FXU, LSU, NormalGr], (instregex "STG(RL)?$")>;
def : InstRW<[WLat1, FXU, LSU, NormalGr], (instregex "ST128$")>;
def : InstRW<[WLat1, FXU, LSU, NormalGr], (instregex "ST(Y|FH|RL|Mux)?$")>;

// String moves.
def : InstRW<[WLat30, WLat30, WLat30, MCD], (instregex "MVST$")>;

//===----------------------------------------------------------------------===//
// Conditional move instructions
//===----------------------------------------------------------------------===//

def : InstRW<[WLat2, FXU, EndGroup], (instregex "LOC(G)?R(Asm.*)?$")>;
def : InstRW<[WLat2LSU, RegReadAdv, FXU, LSU, EndGroup],
             (instregex "LOC(G)?(Asm.*)?$")>;
def : InstRW<[WLat1, FXU, LSU, EndGroup], (instregex "STOC(G)?(Asm.*)?$")>;

//===----------------------------------------------------------------------===//
// Sign extensions
//===----------------------------------------------------------------------===//

def : InstRW<[WLat1, FXU, NormalGr], (instregex "L(B|H|G)R$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "LG(B|H|F)R$")>;

def : InstRW<[WLat1LSU, WLat1LSU, FXU, LSU, NormalGr], (instregex "LTGF$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "LTGFR$")>;

def : InstRW<[WLat1LSU, FXU, LSU, NormalGr], (instregex "LB(H|Mux)?$")>;
def : InstRW<[WLat1LSU, FXU, LSU, NormalGr], (instregex "LH(Y)?$")>;
def : InstRW<[WLat1LSU, FXU, LSU, NormalGr], (instregex "LH(H|Mux|RL)$")>;
def : InstRW<[WLat1LSU, FXU, LSU, NormalGr], (instregex "LG(B|H|F)$")>;
def : InstRW<[WLat1LSU, FXU, LSU, NormalGr], (instregex "LG(H|F)RL$")>;

//===----------------------------------------------------------------------===//
// Zero extensions
//===----------------------------------------------------------------------===//

def : InstRW<[WLat1, FXU, NormalGr], (instregex "LLCR(Mux)?$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "LLHR(Mux)?$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "LLG(C|H|F|T)R$")>;
def : InstRW<[LSULatency, LSU, NormalGr], (instregex "LLC(Mux)?$")>;
def : InstRW<[LSULatency, LSU, NormalGr], (instregex "LLH(Mux)?$")>;
def : InstRW<[WLat1LSU, FXU, LSU, NormalGr], (instregex "LL(C|H)H$")>;
def : InstRW<[LSULatency, LSU, NormalGr], (instregex "LLHRL$")>;
def : InstRW<[LSULatency, LSU, NormalGr], (instregex "LLG(C|H|F|T|HRL|FRL)$")>;

//===----------------------------------------------------------------------===//
// Truncations
//===----------------------------------------------------------------------===//

def : InstRW<[WLat1, FXU, LSU, NormalGr], (instregex "STC(H|Y|Mux)?$")>;
def : InstRW<[WLat1, FXU, LSU, NormalGr], (instregex "STH(H|Y|RL|Mux)?$")>;
def : InstRW<[WLat1, FXU, LSU, NormalGr], (instregex "STCM(H|Y)?$")>;

//===----------------------------------------------------------------------===//
// Multi-register moves
//===----------------------------------------------------------------------===//

// Load multiple (estimated average of 5 ops)
def : InstRW<[WLat10, WLat10, LSU5, GroupAlone], (instregex "LM(H|Y|G)?$")>;

// Load multiple disjoint
def : InstRW<[WLat30, WLat30, MCD], (instregex "LMD$")>;

// Store multiple (estimated average of 3 ops)
def : InstRW<[WLat1, LSU2, FXU5, GroupAlone], (instregex "STM(H|Y|G)?$")>;

//===----------------------------------------------------------------------===//
// Byte swaps
//===----------------------------------------------------------------------===//

def : InstRW<[WLat1, FXU, NormalGr], (instregex "LRV(G)?R$")>;
def : InstRW<[WLat1LSU, FXU, LSU, NormalGr], (instregex "LRV(G|H)?$")>;
def : InstRW<[WLat1, FXU, LSU, NormalGr], (instregex "STRV(G|H)?$")>;
def : InstRW<[WLat30, MCD], (instregex "MVCIN$")>;

//===----------------------------------------------------------------------===//
// Load address instructions
//===----------------------------------------------------------------------===//

def : InstRW<[WLat1, FXU, NormalGr], (instregex "LA(Y|RL)?$")>;

// Load the Global Offset Table address
def : InstRW<[WLat1, FXU, NormalGr], (instregex "GOT$")>;

//===----------------------------------------------------------------------===//
// Absolute and Negation
//===----------------------------------------------------------------------===//

def : InstRW<[WLat2, WLat2, FXU, NormalGr], (instregex "LP(G)?R$")>;
def : InstRW<[WLat3, WLat3, FXU2, GroupAlone], (instregex "L(N|P)GFR$")>;
def : InstRW<[WLat2, WLat2, FXU, NormalGr], (instregex "LN(R|GR)$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "LC(R|GR)$")>;
def : InstRW<[WLat2, WLat2, FXU2, GroupAlone], (instregex "LCGFR$")>;

//===----------------------------------------------------------------------===//
// Insertion
//===----------------------------------------------------------------------===//

def : InstRW<[WLat1LSU, RegReadAdv, FXU, LSU, NormalGr], (instregex "IC(Y)?$")>;
def : InstRW<[WLat1LSU, RegReadAdv, FXU, LSU, NormalGr],
             (instregex "IC32(Y)?$")>;
def : InstRW<[WLat1LSU, WLat1LSU, RegReadAdv, FXU, LSU, NormalGr],
             (instregex "ICM(H|Y)?$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "II(F|H|L)Mux$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "IIHF(64)?$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "IIHH(64)?$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "IIHL(64)?$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "IILF(64)?$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "IILH(64)?$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "IILL(64)?$")>;

//===----------------------------------------------------------------------===//
// Addition
//===----------------------------------------------------------------------===//

def : InstRW<[WLat1LSU, WLat1LSU, RegReadAdv, FXU, LSU, NormalGr],
             (instregex "A(L)?(Y)?$")>;
def : InstRW<[WLat1LSU, WLat1LSU, FXU, LSU, NormalGr], (instregex "A(L)?SI$")>;
def : InstRW<[WLat2LSU, WLat2LSU, RegReadAdv, FXU2, LSU, GroupAlone],
             (instregex "AH(Y)?$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "AIH$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "AFI(Mux)?$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "AGFI$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "AGHI(K)?$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "AGR(K)?$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "AHI(K)?$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "AHIMux(K)?$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "AL(FI|HSIK)$")>;
def : InstRW<[WLat1LSU, WLat1LSU, RegReadAdv, FXU, LSU, NormalGr],
             (instregex "ALGF$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "ALGHSIK$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "ALGF(I|R)$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "ALGR(K)?$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "ALR(K)?$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "AR(K)?$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "A(L)?HHHR$")>;
def : InstRW<[WLat2, WLat2, FXU2, GroupAlone], (instregex "A(L)?HHLR$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "ALSIH(N)?$")>;
def : InstRW<[WLat1LSU, WLat1LSU, RegReadAdv, FXU, LSU, NormalGr],
             (instregex "A(L)?G$")>;
def : InstRW<[WLat1LSU, FXU, LSU, NormalGr], (instregex "A(L)?GSI$")>;

// Logical addition with carry
def : InstRW<[WLat2LSU, WLat2LSU, RegReadAdv, FXU, LSU, GroupAlone],
             (instregex "ALC(G)?$")>;
def : InstRW<[WLat2, WLat2, FXU, GroupAlone], (instregex "ALC(G)?R$")>;

// Add with sign extension (32 -> 64)
def : InstRW<[WLat2LSU, WLat2LSU, RegReadAdv, FXU2, LSU, GroupAlone],
             (instregex "AGF$")>;
def : InstRW<[WLat2, WLat2, FXU2, GroupAlone], (instregex "AGFR$")>;

//===----------------------------------------------------------------------===//
// Subtraction
//===----------------------------------------------------------------------===//

def : InstRW<[WLat1LSU, WLat1LSU, RegReadAdv, FXU, LSU, NormalGr],
             (instregex "S(G|Y)?$")>;
def : InstRW<[WLat2LSU, WLat2LSU, RegReadAdv, FXU2, LSU, GroupAlone],
             (instregex "SH(Y)?$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "SGR(K)?$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "SLFI$")>;
def : InstRW<[WLat1LSU, WLat1LSU, RegReadAdv, FXU, LSU, NormalGr],
             (instregex "SL(G|GF|Y)?$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "SLGF(I|R)$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "SLGR(K)?$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "SLR(K)?$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "SR(K)?$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "S(L)?HHHR$")>;
def : InstRW<[WLat2, WLat2, FXU2, GroupAlone], (instregex "S(L)?HHLR$")>;

// Subtraction with borrow
def : InstRW<[WLat2LSU, WLat2LSU, RegReadAdv, FXU, LSU, GroupAlone],
             (instregex "SLB(G)?$")>;
def : InstRW<[WLat2, WLat2, FXU, GroupAlone], (instregex "SLB(G)?R$")>;

// Subtraction with sign extension (32 -> 64)
def : InstRW<[WLat2LSU, WLat2LSU, RegReadAdv, FXU2, LSU, GroupAlone],
             (instregex "SGF$")>;
def : InstRW<[WLat2, WLat2, FXU2, GroupAlone], (instregex "SGFR$")>;

//===----------------------------------------------------------------------===//
// AND
//===----------------------------------------------------------------------===//

def : InstRW<[WLat1LSU, WLat1LSU, RegReadAdv, FXU, LSU, NormalGr],
             (instregex "N(G|Y)?$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "NGR(K)?$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "NI(FMux|HMux|LMux)$")>;
def : InstRW<[WLat1LSU, FXU, LSU, NormalGr], (instregex "NI(Y)?$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "NIHF(64)?$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "NIHH(64)?$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "NIHL(64)?$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "NILF(64)?$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "NILH(64)?$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "NILL(64)?$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "NR(K)?$")>;
def : InstRW<[WLat5LSU, LSU2, FXU, GroupAlone], (instregex "NC$")>;

//===----------------------------------------------------------------------===//
// OR
//===----------------------------------------------------------------------===//

def : InstRW<[WLat1LSU, WLat1LSU, RegReadAdv, FXU, LSU, NormalGr],
             (instregex "O(G|Y)?$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "OGR(K)?$")>;
def : InstRW<[WLat1LSU, FXU, LSU, NormalGr], (instregex "OI(Y)?$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "OI(FMux|HMux|LMux)$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "OIHF(64)?$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "OIHH(64)?$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "OIHL(64)?$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "OILF(64)?$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "OILH(64)?$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "OILL(64)?$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "OR(K)?$")>;
def : InstRW<[WLat5LSU, LSU2, FXU, GroupAlone], (instregex "OC$")>;

//===----------------------------------------------------------------------===//
// XOR
//===----------------------------------------------------------------------===//

def : InstRW<[WLat1LSU, WLat1LSU, RegReadAdv, FXU, LSU, NormalGr],
             (instregex "X(G|Y)?$")>;
def : InstRW<[WLat1LSU, FXU, LSU, NormalGr], (instregex "XI(Y)?$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "XIFMux$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "XGR(K)?$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "XIHF(64)?$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "XILF(64)?$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "XR(K)?$")>;
def : InstRW<[WLat5LSU, LSU2, FXU, GroupAlone], (instregex "XC$")>;

//===----------------------------------------------------------------------===//
// Multiplication
//===----------------------------------------------------------------------===//

def : InstRW<[WLat6LSU, RegReadAdv, FXU, LSU, NormalGr],
             (instregex "MS(GF|Y)?$")>;
def : InstRW<[WLat6, FXU, NormalGr], (instregex "MS(R|FI)$")>;
def : InstRW<[WLat8LSU, RegReadAdv, FXU, LSU, NormalGr], (instregex "MSG$")>;
def : InstRW<[WLat8, FXU, NormalGr], (instregex "MSGR$")>;
def : InstRW<[WLat6, FXU, NormalGr], (instregex "MSGF(I|R)$")>;
def : InstRW<[WLat11LSU, RegReadAdv, FXU2, LSU, GroupAlone],
             (instregex "MLG$")>;
def : InstRW<[WLat9, FXU2, GroupAlone], (instregex "MLGR$")>;
def : InstRW<[WLat5, FXU, NormalGr], (instregex "MGHI$")>;
def : InstRW<[WLat5, FXU, NormalGr], (instregex "MHI$")>;
def : InstRW<[WLat5LSU, RegReadAdv, FXU, LSU, NormalGr], (instregex "MH(Y)?$")>;
def : InstRW<[WLat7, FXU2, GroupAlone], (instregex "M(L)?R$")>;
def : InstRW<[WLat7LSU, RegReadAdv, FXU2, LSU, GroupAlone],
             (instregex "M(FY|L)?$")>;

//===----------------------------------------------------------------------===//
// Division and remainder
//===----------------------------------------------------------------------===//

def : InstRW<[WLat30, FPU4, FXU5, GroupAlone3], (instregex "DR$")>;
def : InstRW<[WLat30, RegReadAdv, FPU4, LSU, FXU4, GroupAlone3],
             (instregex "D$")>;
def : InstRW<[WLat30, FPU4, FXU4, GroupAlone3], (instregex "DSG(F)?R$")>;
def : InstRW<[WLat30, RegReadAdv, FPU4, LSU, FXU3, GroupAlone3],
             (instregex "DSG(F)?$")>;
def : InstRW<[WLat30, FPU4, FXU5, GroupAlone3], (instregex "DL(G)?R$")>;
def : InstRW<[WLat30, RegReadAdv, FPU4, LSU, FXU4, GroupAlone3],
             (instregex "DL(G)?$")>;

//===----------------------------------------------------------------------===//
// Shifts
//===----------------------------------------------------------------------===//

def : InstRW<[WLat1, FXU, NormalGr], (instregex "SLL(G|K)?$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "SRL(G|K)?$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "SRA(G|K)?$")>;
def : InstRW<[WLat2, WLat2, FXU, NormalGr], (instregex "SLA(G|K)?$")>;
def : InstRW<[WLat5LSU, WLat5LSU, FXU4, LSU, GroupAlone2],
             (instregex "S(L|R)D(A|L)$")>;

// Rotate
def : InstRW<[WLat2LSU, FXU, LSU, NormalGr], (instregex "RLL(G)?$")>;

// Rotate and insert
def : InstRW<[WLat1, FXU, NormalGr], (instregex "RISBG(32)?$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "RISBH(G|H|L)$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "RISBL(G|H|L)$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "RISBMux$")>;

// Rotate and Select
def : InstRW<[WLat3, WLat3, FXU2, GroupAlone], (instregex "R(N|O|X)SBG$")>;

//===----------------------------------------------------------------------===//
// Comparison
//===----------------------------------------------------------------------===//

def : InstRW<[WLat1LSU, RegReadAdv, FXU, LSU, NormalGr], (instregex "C(G|Y|Mux|RL)?$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "C(F|H)I(Mux)?$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "CG(F|H)I$")>;
def : InstRW<[WLat1LSU, FXU, LSU, NormalGr], (instregex "CG(HSI|RL)$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "C(G)?R$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "CIH$")>;
def : InstRW<[WLat1LSU, RegReadAdv, FXU, LSU, NormalGr], (instregex "CHF$")>;
def : InstRW<[WLat1LSU, FXU, LSU, NormalGr], (instregex "CHSI$")>;
def : InstRW<[WLat1LSU, RegReadAdv, FXU, LSU, NormalGr],
             (instregex "CL(Y|Mux)?$")>;
def : InstRW<[WLat1LSU, FXU, LSU, NormalGr], (instregex "CLFHSI$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "CLFI(Mux)?$")>;
def : InstRW<[WLat1LSU, RegReadAdv, FXU, LSU, NormalGr], (instregex "CLG$")>;
def : InstRW<[WLat1LSU, FXU, LSU, NormalGr], (instregex "CLG(HRL|HSI)$")>;
def : InstRW<[WLat1LSU, RegReadAdv, FXU, LSU, NormalGr], (instregex "CLGF$")>;
def : InstRW<[WLat1LSU, FXU, LSU, NormalGr], (instregex "CLGFRL$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "CLGF(I|R)$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "CLGR$")>;
def : InstRW<[WLat1LSU, FXU, LSU, NormalGr], (instregex "CLGRL$")>;
def : InstRW<[WLat1LSU, RegReadAdv, FXU, LSU, NormalGr], (instregex "CLHF$")>;
def : InstRW<[WLat1LSU, FXU, LSU, NormalGr], (instregex "CLH(RL|HSI)$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "CLIH$")>;
def : InstRW<[WLat1LSU, FXU, LSU, NormalGr], (instregex "CLI(Y)?$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "CLR$")>;
def : InstRW<[WLat1LSU, FXU, LSU, NormalGr], (instregex "CLRL$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "C(L)?HHR$")>;
def : InstRW<[WLat2, FXU2, GroupAlone], (instregex "C(L)?HLR$")>;

// Compare halfword
def : InstRW<[WLat2LSU, RegReadAdv, FXU2, LSU, GroupAlone],
             (instregex "CH(Y)?$")>;
def : InstRW<[WLat2LSU, FXU2, LSU, GroupAlone], (instregex "CHRL$")>;
def : InstRW<[WLat2LSU, RegReadAdv, FXU2, LSU, GroupAlone], (instregex "CGH$")>;
def : InstRW<[WLat2LSU, FXU2, LSU, GroupAlone], (instregex "CGHRL$")>;
def : InstRW<[WLat2LSU, FXU2, LSU, GroupAlone], (instregex "CHHSI$")>;

// Compare with sign extension (32 -> 64)
def : InstRW<[WLat2LSU, RegReadAdv, FXU2, LSU, GroupAlone], (instregex "CGF$")>;
def : InstRW<[WLat2LSU, FXU2, LSU, GroupAlone], (instregex "CGFRL$")>;
def : InstRW<[WLat2, FXU2, GroupAlone], (instregex "CGFR$")>;

// Compare logical character
def : InstRW<[WLat9, FXU, LSU2, GroupAlone], (instregex "CLC$")>;
def : InstRW<[WLat30, WLat30, WLat30, MCD], (instregex "CLCL(E|U)?$")>;
def : InstRW<[WLat30, WLat30, WLat30, MCD], (instregex "CLST$")>;

// Test under mask
def : InstRW<[WLat1LSU, FXU, LSU, NormalGr], (instregex "TM(Y)?$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "TM(H|L)Mux$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "TMHH(64)?$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "TMHL(64)?$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "TMLH(64)?$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "TMLL(64)?$")>;

// Compare logical characters under mask
def : InstRW<[WLat2LSU, RegReadAdv, FXU2, LSU, GroupAlone],
             (instregex "CLM(H|Y)?$")>;

//===----------------------------------------------------------------------===//
// Prefetch
//===----------------------------------------------------------------------===//

def : InstRW<[WLat1, LSU, GroupAlone], (instregex "PFD(RL)?$")>;

//===----------------------------------------------------------------------===//
// Atomic operations
//===----------------------------------------------------------------------===//

def : InstRW<[WLat1, LSU, EndGroup], (instregex "Serialize$")>;

def : InstRW<[WLat1LSU, WLat1LSU, FXU, LSU, NormalGr], (instregex "LAA(G)?$")>;
def : InstRW<[WLat1LSU, WLat1LSU, FXU, LSU, NormalGr], (instregex "LAAL(G)?$")>;
def : InstRW<[WLat1LSU, WLat1LSU, FXU, LSU, NormalGr], (instregex "LAN(G)?$")>;
def : InstRW<[WLat1LSU, WLat1LSU, FXU, LSU, NormalGr], (instregex "LAO(G)?$")>;
def : InstRW<[WLat1LSU, WLat1LSU, FXU, LSU, NormalGr], (instregex "LAX(G)?$")>;

// Test and set
def : InstRW<[WLat1LSU, FXU, LSU, EndGroup], (instregex "TS$")>;

// Compare and swap
def : InstRW<[WLat2LSU, WLat2LSU, FXU2, LSU, GroupAlone],
             (instregex "CS(G|Y)?$")>;

// Compare double and swap
def : InstRW<[WLat5LSU, WLat5LSU, FXU5, LSU, GroupAlone2],
             (instregex "CDS(Y)?$")>;
def : InstRW<[WLat12, WLat12, FXU6, LSU2, GroupAlone],
             (instregex "CDSG$")>;

// Compare and swap and store
def : InstRW<[WLat30, MCD], (instregex "CSST$")>;

// Perform locked operation
def : InstRW<[WLat30, MCD], (instregex "PLO$")>;

// Load/store pair from/to quadword
def : InstRW<[WLat4LSU, LSU2, GroupAlone], (instregex "LPQ$")>;
def : InstRW<[WLat1, FXU2, LSU2, GroupAlone], (instregex "STPQ$")>;

// Load pair disjoint
def : InstRW<[WLat2LSU, WLat2LSU, LSU2, GroupAlone], (instregex "LPD(G)?$")>;

//===----------------------------------------------------------------------===//
// Translate and convert
//===----------------------------------------------------------------------===//

def : InstRW<[WLat30, MCD], (instregex "TR$")>;
def : InstRW<[WLat30, WLat30, WLat30, MCD], (instregex "TRT$")>;
def : InstRW<[WLat30, WLat30, WLat30, MCD], (instregex "TRTR$")>;
def : InstRW<[WLat30, WLat30, MCD], (instregex "TRE$")>;
def : InstRW<[WLat30, WLat30, WLat30, MCD], (instregex "TRT(R)?E(Opt)?$")>;
def : InstRW<[WLat30, WLat30, WLat30, MCD], (instregex "TR(T|O)(T|O)(Opt)?$")>;
def : InstRW<[WLat30, WLat30, WLat30, MCD],
             (instregex "CU(12|14|21|24|41|42)(Opt)?$")>;
def : InstRW<[WLat30, WLat30, WLat30, MCD], (instregex "(CUUTF|CUTFU)(Opt)?$")>;

//===----------------------------------------------------------------------===//
// Message-security assist
//===----------------------------------------------------------------------===//

def : InstRW<[WLat30, WLat30, WLat30, WLat30, MCD],
             (instregex "KM(C|F|O|CTR)?$")>;
def : InstRW<[WLat30, WLat30, WLat30, MCD], (instregex "(KIMD|KLMD|KMAC|PCC)$")>;

//===----------------------------------------------------------------------===//
// Decimal arithmetic
//===----------------------------------------------------------------------===//

def : InstRW<[WLat30, RegReadAdv, FXU, DFU2, LSU2, GroupAlone2],
             (instregex "CVBG$")>;
def : InstRW<[WLat20, RegReadAdv, FXU, DFU, LSU, GroupAlone2],
             (instregex "CVB(Y)?$")>;
def : InstRW<[WLat1, FXU3, DFU4, LSU, GroupAlone3], (instregex "CVDG$")>;
def : InstRW<[WLat1, FXU2, DFU, LSU, GroupAlone3], (instregex "CVD(Y)?$")>;
def : InstRW<[WLat1, LSU5, GroupAlone], (instregex "MV(N|O|Z)$")>;
def : InstRW<[WLat1, LSU5, GroupAlone], (instregex "(PACK|PKA|PKU)$")>;
def : InstRW<[WLat10, LSU5, GroupAlone], (instregex "UNPK(A|U)$")>;
def : InstRW<[WLat1, LSU5, GroupAlone], (instregex "UNPK$")>;

def : InstRW<[WLat11LSU, FXU, DFU4, LSU2, GroupAlone],
             (instregex "(A|S|ZA)P$")>;
def : InstRW<[WLat1, FXU, DFU4, LSU2, GroupAlone], (instregex "(M|D)P$")>;
def : InstRW<[WLat15, FXU2, DFU4, LSU3, GroupAlone], (instregex "SRP$")>;
def : InstRW<[WLat11, DFU4, LSU2, GroupAlone], (instregex "CP$")>;
def : InstRW<[WLat5LSU, DFU2, LSU2, GroupAlone], (instregex "TP$")>;
def : InstRW<[WLat30, MCD], (instregex "ED(MK)?$")>;

//===----------------------------------------------------------------------===//
// Access registers
//===----------------------------------------------------------------------===//

// Extract/set/copy access register
def : InstRW<[WLat3, LSU, NormalGr], (instregex "(EAR|SAR|CPYA)$")>;

// Load address extended
def : InstRW<[WLat5, LSU, FXU, GroupAlone], (instregex "LAE(Y)?$")>;

// Load/store access multiple (not modeled precisely)
def : InstRW<[WLat10, WLat10, LSU5, GroupAlone], (instregex "LAM(Y)?$")>;
def : InstRW<[WLat1, FXU5, LSU5, GroupAlone], (instregex "STAM(Y)?$")>;

//===----------------------------------------------------------------------===//
// Program mask and addressing mode
//===----------------------------------------------------------------------===//

// Insert Program Mask
def : InstRW<[WLat3, FXU, EndGroup], (instregex "IPM$")>;

// Set Program Mask
def : InstRW<[WLat3, LSU, EndGroup], (instregex "SPM$")>;

// Branch and link
def : InstRW<[WLat1, FXU2, LSU, GroupAlone], (instregex "BAL(R)?$")>;

// Test addressing mode
def : InstRW<[WLat1, FXU, NormalGr], (instregex "TAM$")>;

// Set addressing mode
def : InstRW<[WLat1, LSU, EndGroup], (instregex "SAM(24|31|64)$")>;

// Branch (and save) and set mode.
def : InstRW<[WLat1, FXU, LSU, GroupAlone], (instregex "BSM$")>;
def : InstRW<[WLat1, FXU2, LSU, GroupAlone], (instregex "BASSM$")>;

//===----------------------------------------------------------------------===//
// Miscellaneous Instructions.
//===----------------------------------------------------------------------===//

// Find leftmost one
def : InstRW<[WLat7, WLat7, FXU2, GroupAlone], (instregex "FLOGR$")>;

// Population count
def : InstRW<[WLat3, WLat3, FXU, NormalGr], (instregex "POPCNT$")>;

// String instructions
def : InstRW<[WLat30, WLat30, WLat30, MCD], (instregex "SRST(U)?$")>;
def : InstRW<[WLat30, WLat30, WLat30, MCD], (instregex "CUSE$")>;

// Various complex instructions
def : InstRW<[WLat30, WLat30, WLat30, WLat30, MCD], (instregex "CFC$")>;
def : InstRW<[WLat30, WLat30, WLat30, WLat30, WLat30, WLat30, MCD],
             (instregex "UPT$")>;
def : InstRW<[WLat30, WLat30, WLat30, MCD], (instregex "CKSM$")>;
def : InstRW<[WLat30, WLat30, WLat30, WLat30, MCD], (instregex "CMPSC$")>;

// Execute
def : InstRW<[LSU, GroupAlone], (instregex "EX(RL)?$")>;

//===----------------------------------------------------------------------===//
// .insn directive instructions
//===----------------------------------------------------------------------===//

// An "empty" sched-class will be assigned instead of the "invalid sched-class".
// getNumDecoderSlots() will then return 1 instead of 0.
def : InstRW<[], (instregex "Insn.*")>;


// ----------------------------- Floating point ----------------------------- //

//===----------------------------------------------------------------------===//
// FP: Move instructions
//===----------------------------------------------------------------------===//

// Load zero
def : InstRW<[WLat1, FXU, NormalGr], (instregex "LZ(DR|ER)$")>;
def : InstRW<[WLat2, FXU2, GroupAlone2], (instregex "LZXR$")>;

// Load
def : InstRW<[WLat1, FXU, NormalGr], (instregex "LER$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "LD(R|R32|GR)$")>;
def : InstRW<[WLat3, FXU, NormalGr], (instregex "LGDR$")>;
def : InstRW<[WLat2, FXU2, GroupAlone2], (instregex "LXR$")>;

// Load and Test
def : InstRW<[WLat9, WLat9, FPU, NormalGr], (instregex "LT(E|D)BR$")>;
def : InstRW<[WLat9, FPU, NormalGr], (instregex "LT(E|D)BRCompare$")>;
def : InstRW<[WLat10, WLat10, FPU4, GroupAlone], (instregex "LTXBR(Compare)?$")>;

// Copy sign
def : InstRW<[WLat5, FXU2, GroupAlone], (instregex "CPSDR(d|s)(d|s)$")>;

//===----------------------------------------------------------------------===//
// FP: Load instructions
//===----------------------------------------------------------------------===//

def : InstRW<[LSULatency, LSU, NormalGr], (instregex "L(E|D)(Y|E32)?$")>;
def : InstRW<[LSULatency, LSU, NormalGr], (instregex "LX$")>;

//===----------------------------------------------------------------------===//
// FP: Store instructions
//===----------------------------------------------------------------------===//

def : InstRW<[WLat1, FXU, LSU, NormalGr], (instregex "ST(E|D)(Y)?$")>;
def : InstRW<[WLat1, FXU, LSU, NormalGr], (instregex "STX$")>;

//===----------------------------------------------------------------------===//
// FP: Conversion instructions
//===----------------------------------------------------------------------===//

// Load rounded
def : InstRW<[WLat7, FPU, NormalGr], (instregex "LEDBR(A)?$")>;
def : InstRW<[WLat9, FPU2, NormalGr], (instregex "L(E|D)XBR(A)?$")>;

// Load lengthened
def : InstRW<[WLat7LSU, FPU, LSU, NormalGr], (instregex "LDEB$")>;
def : InstRW<[WLat7, FPU, NormalGr], (instregex "LDEBR$")>;
def : InstRW<[WLat11LSU, FPU4, LSU, GroupAlone], (instregex "LX(E|D)B$")>;
def : InstRW<[WLat10, FPU4, GroupAlone], (instregex "LX(E|D)BR$")>;

// Convert from fixed / logical
def : InstRW<[WLat8, FXU, FPU, GroupAlone], (instregex "C(E|D)(F|G)BR(A)?$")>;
def : InstRW<[WLat11, FXU, FPU4, GroupAlone2], (instregex "CX(F|G)BR(A?)$")>;
def : InstRW<[WLat8, FXU, FPU, GroupAlone], (instregex "CEL(F|G)BR$")>;
def : InstRW<[WLat8, FXU, FPU, GroupAlone], (instregex "CDL(F|G)BR$")>;
def : InstRW<[WLat11, FXU, FPU4, GroupAlone2], (instregex "CXL(F|G)BR$")>;

// Convert to fixed / logical
def : InstRW<[WLat12, WLat12, FXU, FPU, GroupAlone],
             (instregex "C(F|G)(E|D)BR(A?)$")>;
def : InstRW<[WLat12, WLat12, FXU, FPU2, GroupAlone],
             (instregex "C(F|G)XBR(A?)$")>;
def : InstRW<[WLat12, WLat12, FXU, FPU, GroupAlone],
             (instregex "CL(F|G)(E|D)BR$")>;
def : InstRW<[WLat12, WLat12, FXU, FPU2, GroupAlone], (instregex "CL(F|G)XBR$")>;

//===----------------------------------------------------------------------===//
// FP: Unary arithmetic
//===----------------------------------------------------------------------===//

// Load Complement / Negative / Positive
def : InstRW<[WLat7, WLat7, FPU, NormalGr], (instregex "L(C|N|P)(E|D)BR$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "L(C|N|P)DFR(_32)?$")>;
def : InstRW<[WLat10, WLat10, FPU4, GroupAlone], (instregex "L(C|N|P)XBR$")>;

// Square root
def : InstRW<[WLat30, FPU, LSU, NormalGr], (instregex "SQ(E|D)B$")>;
def : InstRW<[WLat30, FPU, NormalGr], (instregex "SQ(E|D)BR$")>;
def : InstRW<[WLat30, FPU4, GroupAlone], (instregex "SQXBR$")>;

// Load FP integer
def : InstRW<[WLat7, FPU, NormalGr], (instregex "FI(E|D)BR(A)?$")>;
def : InstRW<[WLat15, FPU4, GroupAlone], (instregex "FIXBR(A)?$")>;

//===----------------------------------------------------------------------===//
// FP: Binary arithmetic
//===----------------------------------------------------------------------===//

// Addition
def : InstRW<[WLat7LSU, WLat7LSU, RegReadAdv, FPU, LSU, NormalGr],
             (instregex "A(E|D)B$")>;
def : InstRW<[WLat7, WLat7, FPU, NormalGr], (instregex "A(E|D)BR$")>;
def : InstRW<[WLat20, WLat20, FPU4, GroupAlone], (instregex "AXBR$")>;

// Subtraction
def : InstRW<[WLat7LSU, WLat7LSU, RegReadAdv, FPU, LSU, NormalGr],
             (instregex "S(E|D)B$")>;
def : InstRW<[WLat7, WLat7, FPU, NormalGr], (instregex "S(E|D)BR$")>;
def : InstRW<[WLat20, WLat20, FPU4, GroupAlone], (instregex "SXBR$")>;

// Multiply
def : InstRW<[WLat7LSU, RegReadAdv, FPU, LSU, NormalGr],
             (instregex "M(D|DE|EE)B$")>;
def : InstRW<[WLat7, FPU, NormalGr], (instregex "M(D|DE|EE)BR$")>;
def : InstRW<[WLat11LSU, RegReadAdv, FPU4, LSU, GroupAlone],
             (instregex "MXDB$")>;
def : InstRW<[WLat10, FPU4, GroupAlone], (instregex "MXDBR$")>;
def : InstRW<[WLat30, FPU4, GroupAlone], (instregex "MXBR$")>;

// Multiply and add / subtract
def : InstRW<[WLat7LSU, RegReadAdv, RegReadAdv, FPU2, LSU, GroupAlone],
             (instregex "M(A|S)EB$")>;
def : InstRW<[WLat7, FPU, GroupAlone], (instregex "M(A|S)EBR$")>;
def : InstRW<[WLat7LSU, RegReadAdv, RegReadAdv, FPU2, LSU, GroupAlone],
             (instregex "M(A|S)DB$")>;
def : InstRW<[WLat7, FPU, GroupAlone], (instregex "M(A|S)DBR$")>;

// Division
def : InstRW<[WLat30, RegReadAdv, FPU, LSU, NormalGr], (instregex "D(E|D)B$")>;
def : InstRW<[WLat30, FPU, NormalGr], (instregex "D(E|D)BR$")>;
def : InstRW<[WLat30, FPU4, GroupAlone], (instregex "DXBR$")>;

// Divide to integer
def : InstRW<[WLat30, WLat30, WLat30, MCD], (instregex "DI(E|D)BR$")>;

//===----------------------------------------------------------------------===//
// FP: Comparisons
//===----------------------------------------------------------------------===//

// Compare
def : InstRW<[WLat11LSU, RegReadAdv, FPU, LSU, NormalGr],
             (instregex "(K|C)(E|D)B$")>;
def : InstRW<[WLat9, FPU, NormalGr], (instregex "(K|C)(E|D)BR$")>;
def : InstRW<[WLat30, FPU2, NormalGr], (instregex "(K|C)XBR$")>;

// Test Data Class
def : InstRW<[WLat15, FPU, LSU, NormalGr], (instregex "TC(E|D)B$")>;
def : InstRW<[WLat15, FPU4, LSU, GroupAlone], (instregex "TCXB$")>;

//===----------------------------------------------------------------------===//
// FP: Floating-point control register instructions
//===----------------------------------------------------------------------===//

def : InstRW<[WLat4, FXU, LSU, GroupAlone], (instregex "EFPC$")>;
def : InstRW<[WLat1, FXU, LSU, GroupAlone], (instregex "STFPC$")>;
def : InstRW<[WLat1, LSU, GroupAlone], (instregex "SFPC$")>;
def : InstRW<[WLat1, LSU2, GroupAlone], (instregex "LFPC$")>;
def : InstRW<[WLat30, MCD], (instregex "SFASR$")>;
def : InstRW<[WLat30, MCD], (instregex "LFAS$")>;
def : InstRW<[WLat2, FXU, GroupAlone], (instregex "SRNM(B|T)?$")>;


// --------------------- Hexadecimal floating point ------------------------- //

//===----------------------------------------------------------------------===//
// HFP: Move instructions
//===----------------------------------------------------------------------===//

// Load and Test
def : InstRW<[WLat9, WLat9, FPU, NormalGr], (instregex "LT(E|D)R$")>;
def : InstRW<[WLat9, WLat9, FPU4, GroupAlone], (instregex "LTXR$")>;

//===----------------------------------------------------------------------===//
// HFP: Conversion instructions
//===----------------------------------------------------------------------===//

// Load rounded
def : InstRW<[WLat7, FPU, NormalGr], (instregex "(LEDR|LRER)$")>;
def : InstRW<[WLat7, FPU, NormalGr], (instregex "LEXR$")>;
def : InstRW<[WLat9, FPU, NormalGr], (instregex "(LDXR|LRDR)$")>;

// Load lengthened
def : InstRW<[LSULatency, LSU, NormalGr], (instregex "LDE$")>;
def : InstRW<[WLat1, FXU, NormalGr], (instregex "LDER$")>;
def : InstRW<[WLat11LSU, FPU4, LSU, GroupAlone], (instregex "LX(E|D)$")>;
def : InstRW<[WLat9, FPU4, GroupAlone], (instregex "LX(E|D)R$")>;

// Convert from fixed
def : InstRW<[WLat8, FXU, FPU, GroupAlone], (instregex "C(E|D)(F|G)R$")>;
def : InstRW<[WLat10, FXU, FPU4, GroupAlone2], (instregex "CX(F|G)R$")>;

// Convert to fixed
def : InstRW<[WLat12, WLat12, FXU, FPU, GroupAlone],
             (instregex "C(F|G)(E|D)R$")>;
def : InstRW<[WLat30, WLat30, FXU, FPU2, GroupAlone], (instregex "C(F|G)XR$")>;

// Convert BFP to HFP / HFP to BFP.
def : InstRW<[WLat7, WLat7, FPU, NormalGr], (instregex "THD(E)?R$")>;
def : InstRW<[WLat7, WLat7, FPU, NormalGr], (instregex "TB(E)?DR$")>;

//===----------------------------------------------------------------------===//
// HFP: Unary arithmetic
//===----------------------------------------------------------------------===//

// Load Complement / Negative / Positive
def : InstRW<[WLat7, WLat7, FPU, NormalGr], (instregex "L(C|N|P)(E|D)R$")>;
def : InstRW<[WLat9, WLat9, FPU4, GroupAlone], (instregex "L(C|N|P)XR$")>;

// Halve
def : InstRW<[WLat7, FPU, NormalGr], (instregex "H(E|D)R$")>;

// Square root
def : InstRW<[WLat30, FPU, LSU, NormalGr], (instregex "SQ(E|D)$")>;
def : InstRW<[WLat30, FPU, NormalGr], (instregex "SQ(E|D)R$")>;
def : InstRW<[WLat30, FPU4, GroupAlone], (instregex "SQXR$")>;

// Load FP integer
def : InstRW<[WLat7, FPU, NormalGr], (instregex "FI(E|D)R$")>;
def : InstRW<[WLat15, FPU4, GroupAlone], (instregex "FIXR$")>;

//===----------------------------------------------------------------------===//
// HFP: Binary arithmetic
//===----------------------------------------------------------------------===//

// Addition
def : InstRW<[WLat7LSU, WLat7LSU, RegReadAdv, FPU, LSU, NormalGr],
             (instregex "A(E|D|U|W)$")>;
def : InstRW<[WLat7, WLat7, FPU, NormalGr], (instregex "A(E|D|U|W)R$")>;
def : InstRW<[WLat15, WLat15, FPU4, GroupAlone], (instregex "AXR$")>;

// Subtraction
def : InstRW<[WLat7LSU, WLat7LSU, RegReadAdv, FPU, LSU, NormalGr],
             (instregex "S(E|D|U|W)$")>;
def : InstRW<[WLat7, WLat7, FPU, NormalGr], (instregex "S(E|D|U|W)R$")>;
def : InstRW<[WLat15, WLat15, FPU4, GroupAlone], (instregex "SXR$")>;

// Multiply
def : InstRW<[WLat7LSU, RegReadAdv, FPU, LSU, NormalGr], (instregex "M(D|EE)$")>;
def : InstRW<[WLat8LSU, RegReadAdv, FPU, LSU, NormalGr], (instregex "M(DE|E)$")>;
def : InstRW<[WLat7, FPU, NormalGr], (instregex "M(D|EE)R$")>;
def : InstRW<[WLat8, FPU, NormalGr], (instregex "M(DE|E)R$")>;
def : InstRW<[WLat11LSU, RegReadAdv, FPU4, LSU, GroupAlone], (instregex "MXD$")>;
def : InstRW<[WLat10, FPU4, GroupAlone], (instregex "MXDR$")>;
def : InstRW<[WLat30, FPU4, GroupAlone], (instregex "MXR$")>;
def : InstRW<[WLat11LSU, RegReadAdv, FPU4, LSU, GroupAlone], (instregex "MY$")>;
def : InstRW<[WLat7LSU, RegReadAdv, FPU2, LSU, GroupAlone],
             (instregex "MY(H|L)$")>;
def : InstRW<[WLat10, FPU4, GroupAlone], (instregex "MYR$")>;
def : InstRW<[WLat7, FPU, GroupAlone], (instregex "MY(H|L)R$")>;

// Multiply and add / subtract
def : InstRW<[WLat7LSU, RegReadAdv, RegReadAdv, FPU2, LSU, GroupAlone],
             (instregex "M(A|S)(E|D)$")>;
def : InstRW<[WLat7, FPU, GroupAlone], (instregex "M(A|S)(E|D)R$")>;
def : InstRW<[WLat11LSU, RegReadAdv, RegReadAdv, FPU4, LSU, GroupAlone],
             (instregex "MAY$")>;
def : InstRW<[WLat7LSU, RegReadAdv, RegReadAdv, FPU2, LSU, GroupAlone],
             (instregex "MAY(H|L)$")>;
def : InstRW<[WLat10, FPU4, GroupAlone], (instregex "MAYR$")>;
def : InstRW<[WLat7, FPU, GroupAlone], (instregex "MAY(H|L)R$")>;

// Division
def : InstRW<[WLat30, RegReadAdv, FPU, LSU, NormalGr], (instregex "D(E|D)$")>;
def : InstRW<[WLat30, FPU, NormalGr], (instregex "D(E|D)R$")>;
def : InstRW<[WLat30, FPU4, GroupAlone], (instregex "DXR$")>;

//===----------------------------------------------------------------------===//
// HFP: Comparisons
//===----------------------------------------------------------------------===//

// Compare
def : InstRW<[WLat11LSU, RegReadAdv, FPU, LSU, NormalGr], (instregex "C(E|D)$")>;
def : InstRW<[WLat9, FPU, NormalGr], (instregex "C(E|D)R$")>;
def : InstRW<[WLat15, FPU2, NormalGr], (instregex "CXR$")>;


// ------------------------ Decimal floating point -------------------------- //

//===----------------------------------------------------------------------===//
// DFP: Move instructions
//===----------------------------------------------------------------------===//

// Load and Test
def : InstRW<[WLat4, WLat4, DFU, NormalGr], (instregex "LTDTR$")>;
def : InstRW<[WLat6, WLat6, DFU4, GroupAlone], (instregex "LTXTR$")>;

//===----------------------------------------------------------------------===//
// DFP: Conversion instructions
//===----------------------------------------------------------------------===//

// Load rounded
def : InstRW<[WLat30, DFU, NormalGr], (instregex "LEDTR$")>;
def : InstRW<[WLat30, DFU2, NormalGr], (instregex "LDXTR$")>;

// Load lengthened
def : InstRW<[WLat7, DFU, NormalGr], (instregex "LDETR$")>;
def : InstRW<[WLat6, DFU4, GroupAlone], (instregex "LXDTR$")>;

// Convert from fixed / logical
def : InstRW<[WLat9, FXU, DFU, GroupAlone], (instregex "CDFTR$")>;
def : InstRW<[WLat30, FXU, DFU, GroupAlone], (instregex "CDGTR(A)?$")>;
def : InstRW<[WLat5, FXU, DFU4, GroupAlone2], (instregex "CXFTR(A)?$")>;
def : InstRW<[WLat30, FXU, DFU4, GroupAlone2], (instregex "CXGTR(A)?$")>;
def : InstRW<[WLat9, FXU, DFU, GroupAlone], (instregex "CDL(F|G)TR$")>;
def : InstRW<[WLat9, FXU, DFU4, GroupAlone2], (instregex "CXLFTR$")>;
def : InstRW<[WLat5, FXU, DFU4, GroupAlone2], (instregex "CXLGTR$")>;

// Convert to fixed / logical
def : InstRW<[WLat11, WLat11, FXU, DFU, GroupAlone], (instregex "CFDTR(A)?$")>;
def : InstRW<[WLat30, WLat30, FXU, DFU, GroupAlone], (instregex "CGDTR(A)?$")>;
def : InstRW<[WLat7, WLat7, FXU, DFU2, GroupAlone], (instregex "CFXTR$")>;
def : InstRW<[WLat30, WLat30, FXU, DFU2, GroupAlone], (instregex "CGXTR(A)?$")>;
def : InstRW<[WLat11, WLat11, FXU, DFU, GroupAlone], (instregex "CL(F|G)DTR$")>;
def : InstRW<[WLat7, WLat7, FXU, DFU2, GroupAlone], (instregex "CL(F|G)XTR$")>;

// Convert from / to signed / unsigned packed
def : InstRW<[WLat5, FXU, DFU, GroupAlone], (instregex "CD(S|U)TR$")>;
def : InstRW<[WLat8, FXU2, DFU4, GroupAlone2], (instregex "CX(S|U)TR$")>;
def : InstRW<[WLat7, FXU, DFU, GroupAlone], (instregex "C(S|U)DTR$")>;
def : InstRW<[WLat12, FXU2, DFU4, GroupAlone2], (instregex "C(S|U)XTR$")>;

// Perform floating-point operation
def : InstRW<[WLat30, WLat30, WLat30, MCD], (instregex "PFPO$")>;

//===----------------------------------------------------------------------===//
// DFP: Unary arithmetic
//===----------------------------------------------------------------------===//

// Load FP integer
def : InstRW<[WLat8, DFU, NormalGr], (instregex "FIDTR$")>;
def : InstRW<[WLat10, DFU4, GroupAlone], (instregex "FIXTR$")>;

// Extract biased exponent
def : InstRW<[WLat7, FXU, DFU, GroupAlone], (instregex "EEDTR$")>;
def : InstRW<[WLat8, FXU, DFU2, GroupAlone], (instregex "EEXTR$")>;

// Extract significance
def : InstRW<[WLat7, FXU, DFU, GroupAlone], (instregex "ESDTR$")>;
def : InstRW<[WLat8, FXU, DFU2, GroupAlone], (instregex "ESXTR$")>;

//===----------------------------------------------------------------------===//
// DFP: Binary arithmetic
//===----------------------------------------------------------------------===//

// Addition
def : InstRW<[WLat9, WLat9, DFU, NormalGr], (instregex "ADTR(A)?$")>;
def : InstRW<[WLat30, WLat30, DFU4, GroupAlone], (instregex "AXTR(A)?$")>;

// Subtraction
def : InstRW<[WLat9, WLat9, DFU, NormalGr], (instregex "SDTR(A)?$")>;
def : InstRW<[WLat30, WLat30, DFU4, GroupAlone], (instregex "SXTR(A)?$")>;

// Multiply
def : InstRW<[WLat30, DFU, NormalGr], (instregex "MDTR(A)?$")>;
def : InstRW<[WLat30, DFU4, GroupAlone], (instregex "MXTR(A)?$")>;

// Division
def : InstRW<[WLat30, DFU, NormalGr], (instregex "DDTR(A)?$")>;
def : InstRW<[WLat30, DFU4, GroupAlone], (instregex "DXTR(A)?$")>;

// Quantize
def : InstRW<[WLat8, WLat8, DFU, NormalGr], (instregex "QADTR$")>;
def : InstRW<[WLat10, WLat10, DFU4, GroupAlone], (instregex "QAXTR$")>;

// Reround
def : InstRW<[WLat11, WLat11, FXU, DFU, GroupAlone], (instregex "RRDTR$")>;
def : InstRW<[WLat30, WLat30, FXU, DFU4, GroupAlone2], (instregex "RRXTR$")>;

// Shift significand left/right
def : InstRW<[WLat7LSU, LSU, DFU, GroupAlone], (instregex "S(L|R)DT$")>;
def : InstRW<[WLat11LSU, LSU, DFU4, GroupAlone], (instregex "S(L|R)XT$")>;

// Insert biased exponent
def : InstRW<[WLat5, FXU, DFU, GroupAlone], (instregex "IEDTR$")>;
def : InstRW<[WLat7, FXU, DFU4, GroupAlone2], (instregex "IEXTR$")>;

//===----------------------------------------------------------------------===//
// DFP: Comparisons
//===----------------------------------------------------------------------===//

// Compare
def : InstRW<[WLat9, DFU, NormalGr], (instregex "(K|C)DTR$")>;
def : InstRW<[WLat10, DFU2, NormalGr], (instregex "(K|C)XTR$")>;

// Compare biased exponent
def : InstRW<[WLat4, DFU, NormalGr], (instregex "CEDTR$")>;
def : InstRW<[WLat5, DFU2, NormalGr], (instregex "CEXTR$")>;

// Test Data Class/Group
def : InstRW<[WLat9, LSU, DFU, NormalGr], (instregex "TD(C|G)DT$")>;
def : InstRW<[WLat10, LSU, DFU, NormalGr], (instregex "TD(C|G)ET$")>;
def : InstRW<[WLat10, LSU, DFU2, NormalGr], (instregex "TD(C|G)XT$")>;


// -------------------------------- System ---------------------------------- //

//===----------------------------------------------------------------------===//
// System: Program-Status Word Instructions
//===----------------------------------------------------------------------===//

def : InstRW<[WLat30, WLat30, MCD], (instregex "EPSW$")>;
def : InstRW<[WLat30, MCD], (instregex "LPSW(E)?$")>;
def : InstRW<[WLat3, FXU, GroupAlone], (instregex "IPK$")>;
def : InstRW<[WLat1, LSU, EndGroup], (instregex "SPKA$")>;
def : InstRW<[WLat1, LSU, EndGroup], (instregex "SSM$")>;
def : InstRW<[WLat1, FXU, LSU, GroupAlone], (instregex "ST(N|O)SM$")>;
def : InstRW<[WLat3, FXU, NormalGr], (instregex "IAC$")>;
def : InstRW<[WLat1, LSU, EndGroup], (instregex "SAC(F)?$")>;

//===----------------------------------------------------------------------===//
// System: Control Register Instructions
//===----------------------------------------------------------------------===//

def : InstRW<[WLat10, WLat10, LSU2, GroupAlone], (instregex "LCTL(G)?$")>;
def : InstRW<[WLat1, FXU5, LSU5, GroupAlone], (instregex "STCT(L|G)$")>;
def : InstRW<[LSULatency, LSU, NormalGr], (instregex "E(P|S)A(I)?R$")>;
def : InstRW<[WLat30, MCD], (instregex "SSA(I)?R$")>;
def : InstRW<[WLat30, MCD], (instregex "ESEA$")>;

//===----------------------------------------------------------------------===//
// System: Prefix-Register Instructions
//===----------------------------------------------------------------------===//

def : InstRW<[WLat30, MCD], (instregex "S(T)?PX$")>;

//===----------------------------------------------------------------------===//
// System: Storage-Key and Real Memory Instructions
//===----------------------------------------------------------------------===//

def : InstRW<[WLat30, MCD], (instregex "ISKE$")>;
def : InstRW<[WLat30, MCD], (instregex "IVSK$")>;
def : InstRW<[WLat30, MCD], (instregex "SSKE(Opt)?$")>;
def : InstRW<[WLat30, MCD], (instregex "RRB(E|M)$")>;
def : InstRW<[WLat30, MCD], (instregex "PFMF$")>;
def : InstRW<[WLat30, WLat30, MCD], (instregex "TB$")>;
def : InstRW<[WLat30, MCD], (instregex "PGIN$")>;
def : InstRW<[WLat30, MCD], (instregex "PGOUT$")>;

//===----------------------------------------------------------------------===//
// System: Dynamic-Address-Translation Instructions
//===----------------------------------------------------------------------===//

def : InstRW<[WLat30, MCD], (instregex "IPTE(Opt)?(Opt)?$")>;
def : InstRW<[WLat30, MCD], (instregex "IDTE(Opt)?$")>;
def : InstRW<[WLat30, MCD], (instregex "PTLB$")>;
def : InstRW<[WLat30, WLat30, MCD], (instregex "CSP(G)?$")>;
def : InstRW<[WLat30, WLat30, WLat30, MCD], (instregex "LPTEA$")>;
def : InstRW<[WLat30, WLat30, MCD], (instregex "LRA(Y|G)?$")>;
def : InstRW<[WLat30, MCD], (instregex "STRAG$")>;
def : InstRW<[WLat30, MCD], (instregex "LURA(G)?$")>;
def : InstRW<[WLat30, MCD], (instregex "STUR(A|G)$")>;
def : InstRW<[WLat30, MCD], (instregex "TPROT$")>;

//===----------------------------------------------------------------------===//
// System: Memory-move Instructions
//===----------------------------------------------------------------------===//

def : InstRW<[WLat30, MCD], (instregex "MVC(K|P|S)$")>;
def : InstRW<[WLat30, MCD], (instregex "MVC(S|D)K$")>;
def : InstRW<[WLat30, MCD], (instregex "MVCOS$")>;
def : InstRW<[WLat30, MCD], (instregex "MVPG$")>;

//===----------------------------------------------------------------------===//
// System: Address-Space Instructions
//===----------------------------------------------------------------------===//

def : InstRW<[WLat30, MCD], (instregex "LASP$")>;
def : InstRW<[WLat1, LSU, GroupAlone], (instregex "PALB$")>;
def : InstRW<[WLat30, MCD], (instregex "PC$")>;
def : InstRW<[WLat30, MCD], (instregex "PR$")>;
def : InstRW<[WLat30, MCD], (instregex "PT(I)?$")>;
def : InstRW<[WLat30, MCD], (instregex "RP$")>;
def : InstRW<[WLat30, MCD], (instregex "BS(G|A)$")>;
def : InstRW<[WLat30, MCD], (instregex "TAR$")>;

//===----------------------------------------------------------------------===//
// System: Linkage-Stack Instructions
//===----------------------------------------------------------------------===//

def : InstRW<[WLat30, MCD], (instregex "BAKR$")>;
def : InstRW<[WLat30, MCD], (instregex "EREG(G)?$")>;
def : InstRW<[WLat30, WLat30, MCD], (instregex "(E|M)STA$")>;

//===----------------------------------------------------------------------===//
// System: Time-Related Instructions
//===----------------------------------------------------------------------===//

def : InstRW<[WLat30, MCD], (instregex "PTFF$")>;
def : InstRW<[WLat30, MCD], (instregex "SCK$")>;
def : InstRW<[WLat30, MCD], (instregex "SCKPF$")>;
def : InstRW<[WLat30, MCD], (instregex "SCKC$")>;
def : InstRW<[WLat30, MCD], (instregex "SPT$")>;
def : InstRW<[WLat30, MCD], (instregex "STCK(F)?$")>;
def : InstRW<[WLat30, MCD], (instregex "STCKE$")>;
def : InstRW<[WLat30, MCD], (instregex "STCKC$")>;
def : InstRW<[WLat30, MCD], (instregex "STPT$")>;

//===----------------------------------------------------------------------===//
// System: CPU-Related Instructions
//===----------------------------------------------------------------------===//

def : InstRW<[WLat30, MCD], (instregex "STAP$")>;
def : InstRW<[WLat30, MCD], (instregex "STIDP$")>;
def : InstRW<[WLat30, WLat30, MCD], (instregex "STSI$")>;
def : InstRW<[WLat30, WLat30, MCD], (instregex "STFL(E)?$")>;
def : InstRW<[WLat30, MCD], (instregex "ECAG$")>;
def : InstRW<[WLat30, WLat30, MCD], (instregex "ECTG$")>;
def : InstRW<[WLat30, MCD], (instregex "PTF$")>;
def : InstRW<[WLat30, MCD], (instregex "PCKMO$")>;

//===----------------------------------------------------------------------===//
// System: Miscellaneous Instructions
//===----------------------------------------------------------------------===//

def : InstRW<[WLat30, MCD], (instregex "SVC$")>;
def : InstRW<[WLat1, FXU, GroupAlone], (instregex "MC$")>;
def : InstRW<[WLat30, MCD], (instregex "DIAG$")>;
def : InstRW<[WLat30, MCD], (instregex "TRAC(E|G)$")>;
def : InstRW<[WLat30, MCD], (instregex "TRAP(2|4)$")>;
def : InstRW<[WLat30, MCD], (instregex "SIG(P|A)$")>;
def : InstRW<[WLat30, MCD], (instregex "SIE$")>;

//===----------------------------------------------------------------------===//
// System: CPU-Measurement Facility Instructions
//===----------------------------------------------------------------------===//

def : InstRW<[WLat1, FXU, NormalGr], (instregex "LPP$")>;
def : InstRW<[WLat30, WLat30, MCD], (instregex "ECPGA$")>;
def : InstRW<[WLat30, WLat30, MCD], (instregex "E(C|P)CTR$")>;
def : InstRW<[WLat30, MCD], (instregex "LCCTL$")>;
def : InstRW<[WLat30, MCD], (instregex "L(P|S)CTL$")>;
def : InstRW<[WLat30, MCD], (instregex "Q(S|CTR)I$")>;
def : InstRW<[WLat30, MCD], (instregex "S(C|P)CTR$")>;

//===----------------------------------------------------------------------===//
// System: I/O Instructions
//===----------------------------------------------------------------------===//

def : InstRW<[WLat30, MCD], (instregex "(C|H|R|X)SCH$")>;
def : InstRW<[WLat30, MCD], (instregex "(M|S|ST|T)SCH$")>;
def : InstRW<[WLat30, MCD], (instregex "RCHP$")>;
def : InstRW<[WLat30, MCD], (instregex "SCHM$")>;
def : InstRW<[WLat30, MCD], (instregex "STC(PS|RW)$")>;
def : InstRW<[WLat30, MCD], (instregex "TPI$")>;
def : InstRW<[WLat30, MCD], (instregex "SAL$")>;

}

