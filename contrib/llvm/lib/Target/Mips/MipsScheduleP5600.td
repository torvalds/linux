//==- MipsScheduleP5600.td - P5600 Scheduling Definitions --*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

def MipsP5600Model : SchedMachineModel {
  int IssueWidth = 2; // 2x dispatched per cycle
  int MicroOpBufferSize = 48; // min(48, 48, 64)
  int LoadLatency = 4;
  int MispredictPenalty = 8; // TODO: Estimated

  let CompleteModel = 0;
  let FullInstRWOverlapCheck = 1;

  list<Predicate> UnsupportedFeatures = [HasMips32r6, HasMips64r6,
                                         HasMips3, HasMips64r2, HasCnMips,
                                         InMicroMips, InMips16Mode,
                                         HasDSP, HasDSPR2, HasMT, HasCRC];
}

let SchedModel = MipsP5600Model in {

// ALQ Pipelines
// =============

def P5600ALQ : ProcResource<1> { let BufferSize = 16; }
def P5600IssueALU : ProcResource<1> { let Super = P5600ALQ; }

// ALU Pipeline
// ------------

def P5600WriteALU : SchedWriteRes<[P5600IssueALU]>;

// and, lui, nor, or, slti, sltiu, sub, subu, xor
def : InstRW<[P5600WriteALU], (instrs AND, LUi, NOR, OR, SLTi, SLTiu, SUB,
                               SUBu, XOR)>;

// AGQ Pipelines
// =============

def P5600AGQ : ProcResource<3> { let BufferSize = 16; }
def P5600IssueAL2 : ProcResource<1> { let Super = P5600AGQ; }
def P5600IssueCTISTD : ProcResource<1> { let Super = P5600AGQ; }
def P5600IssueLDST : ProcResource<1> { let Super = P5600AGQ; }

def P5600AL2Div : ProcResource<1>;
// Pseudo-resource used to block CTISTD when handling multi-pipeline splits.
def P5600CTISTD : ProcResource<1>;

// CTISTD Pipeline
// ---------------

def P5600WriteJump : SchedWriteRes<[P5600IssueCTISTD, P5600CTISTD]>;
def P5600WriteJumpAndLink : SchedWriteRes<[P5600IssueCTISTD, P5600CTISTD]> {
  let Latency = 2;
}

// b, beq, beql, bg[et]z, bl[et]z, bne, bnel, j, syscall, jal, bltzal,
// jalr, jr.hb, jr
def : InstRW<[P5600WriteJump], (instrs B, BAL, BAL_BR, BEQ, BEQL, BGEZ, BGEZAL,
                                BGEZALL, BGEZL, BGTZ, BGTZL, BLEZ, BLEZL, BLTZ,
                                BLTZAL, BLTZALL, BLTZL, BNE, BNEL, BREAK,
                                DERET, ERET, ERETNC, J, JR, JR_HB,
                                PseudoIndirectBranch,
                                PseudoIndirectHazardBranch, PseudoReturn,
                                SDBBP, SSNOP, SYSCALL, TAILCALL, TAILCALLREG,
                                TAILCALLREGHB, TEQ, TEQI, TGE, TGEI, TGEIU,
                                TGEU, TLT, TLTI, TLTU, TNE, TNEI, TRAP,
                                TTLTIU, WAIT, PAUSE)>;

def : InstRW<[P5600WriteJumpAndLink], (instrs JAL, JALR, JALRHBPseudo,
                                       JALRPseudo, JALR_HB)>;

def : InstRW<[P5600WriteJumpAndLink], (instrs JALX)> {
  let Unsupported = 1;
}

def P5600COP0 : SchedWriteRes<[P5600IssueCTISTD, P5600CTISTD]>;

def : InstRW<[P5600COP0], (instrs TLBINV, TLBINVF, TLBP, TLBR, TLBWI, TLBWR,
                           MFC0, MTC0)>;

def P5600COP2 : SchedWriteRes<[P5600IssueCTISTD, P5600CTISTD]>;

def : InstRW<[P5600COP2], (instrs MFC2, MTC2)> {
  let Unsupported = 1;
}

// LDST Pipeline
// -------------

def P5600WriteLoad : SchedWriteRes<[P5600IssueLDST]> {
  let Latency = 4;
}

def P5600WriteLoadShifted : SchedWriteRes<[P5600IssueLDST, P5600CTISTD]> {
  let Latency = 4;
}

def P5600WriteCache : SchedWriteRes<[P5600IssueLDST]>;

def P5600WriteStore : SchedWriteRes<[P5600IssueLDST, P5600CTISTD]> {
  // FIXME: This is a bit pessimistic. P5600CTISTD is only used during cycle 2
  //        not during 0, 1, and 2.
  let ResourceCycles = [ 1, 3 ];
}

def P5600WriteGPRFromBypass : SchedWriteRes<[P5600IssueLDST]> {
  let Latency = 2;
}

def P5600WriteStoreFromOtherUnits : SchedWriteRes<[P5600IssueLDST]>;
def P5600WriteLoadToOtherUnits : SchedWriteRes<[P5600IssueLDST]> {
  let Latency = 0;
}

// l[bhw], l[bh]u, ll
def : InstRW<[P5600WriteLoad], (instrs LB, LBu, LH, LHu, LW, LL, LWC2, LWC3,
                                LDC2, LDC3, LBE, LBuE, LHE, LHuE, LWE, LLE,
                                LWPC)>;

// lw[lr]
def : InstRW<[P5600WriteLoadShifted], (instrs LWL, LWR, LWLE, LWRE)>;

// s[bhw], sw[lr]
def : InstRW<[P5600WriteStore], (instrs SB, SH, SW, SWC2, SWC3, SDC2, SDC3, SC,
                                 SBE, SHE, SWE, SCE, SWL, SWR, SWLE, SWRE)>;

// pref, cache, sync, synci
def : InstRW<[P5600WriteCache], (instrs PREF, PREFE, CACHE, CACHEE, SYNC,
                                 SYNCI)>;

// LDST is also used in moves from general purpose registers to floating point
// and MSA.
def P5600WriteMoveGPRToOtherUnits : SchedWriteRes<[P5600IssueLDST]> {
  let Latency = 0;
}

// AL2 Pipeline
// ------------

def P5600WriteAL2 : SchedWriteRes<[P5600IssueAL2]>;
def P5600WriteAL2BitExt : SchedWriteRes<[P5600IssueAL2]> { let Latency = 2; }
def P5600WriteAL2ShadowMov : SchedWriteRes<[P5600IssueAL2]> { let Latency = 2; }
def P5600WriteAL2CondMov : SchedWriteRes<[P5600IssueAL2, P5600CTISTD]> {
  let Latency = 2;
}
def P5600WriteAL2Div : SchedWriteRes<[P5600IssueAL2, P5600AL2Div]> {
  // Estimated worst case
  let Latency = 34;
  let ResourceCycles = [1, 34];
}
def P5600WriteAL2DivU : SchedWriteRes<[P5600IssueAL2, P5600AL2Div]> {
  // Estimated worst case
  let Latency = 34;
  let ResourceCycles = [1, 34];
}
def P5600WriteAL2Mul : SchedWriteRes<[P5600IssueAL2]> { let Latency = 3; }
def P5600WriteAL2Mult: SchedWriteRes<[P5600IssueAL2]> { let Latency = 5; }
def P5600WriteAL2MAdd: SchedWriteRes<[P5600IssueAL2, P5600CTISTD]> {
  let Latency = 5;
}

// clo, clz, di, ei, mfhi, mflo
def : InstRW<[P5600WriteAL2], (instrs CLO, CLZ, DI, EI, MFHI, MFLO,
                               PseudoMFHI, PseudoMFLO)>;

// ehb, rdhwr, rdpgpr, wrpgpr, wsbh
def : InstRW<[P5600WriteAL2ShadowMov], (instrs EHB, RDHWR, WSBH)>;

// mov[nz]
def : InstRW<[P5600WriteAL2CondMov], (instrs MOVN_I_I, MOVZ_I_I)>;

// divu?
def : InstRW<[P5600WriteAL2Div], (instrs DIV, PseudoSDIV, SDIV)>;
def : InstRW<[P5600WriteAL2DivU], (instrs DIVU, PseudoUDIV, UDIV)>;

// mul
def : InstRW<[P5600WriteAL2Mul], (instrs MUL)>;
// multu?, multu?
def : InstRW<[P5600WriteAL2Mult], (instrs MULT, MULTu, PseudoMULT,
                                   PseudoMULTu)>;
// maddu?, msubu?, mthi, mtlo
def : InstRW<[P5600WriteAL2MAdd], (instrs MADD, MADDU, MSUB, MSUBU,
                                   MTHI, MTLO, PseudoMADD, PseudoMADDU,
                                   PseudoMSUB, PseudoMSUBU, PseudoMTLOHI)>;

// ext, ins
def : InstRW<[P5600WriteAL2BitExt], (instrs EXT, INS)>;

// Either ALU or AL2 Pipelines
// ---------------------------
//
// Some instructions can choose between ALU and AL2, but once dispatched to
// ALQ or AGQ respectively they are committed to that path.
// The decision is based on the outcome of the most recent selection when the
// choice was last available. For now, we assume ALU is always chosen.

def P5600WriteEitherALU : SchedWriteVariant<
  // FIXME: Implement selection predicate
  [SchedVar<SchedPredicate<[{1}]>, [P5600WriteALU]>,
   SchedVar<SchedPredicate<[{0}]>, [P5600WriteAL2]>
  ]>;

// add, addi, addiu, addu, andi, ori, rotr, se[bh], sllv?, sr[al]v?, slt, sltu,
// xori
def : InstRW<[P5600WriteEitherALU], (instrs ADD, ADDi, ADDiu, ANDi, ORi, ROTR,
                                     SEB, SEH, SLT, SLTu, SLL, SRA, SRL, XORi,
                                     ADDu, SLLV, SRAV, SRLV, LSA, COPY)>;

// FPU Pipelines
// =============

def P5600FPQ : ProcResource<3> { let BufferSize = 16; }
def P5600IssueFPUS : ProcResource<1> { let Super = P5600FPQ; }
def P5600IssueFPUL : ProcResource<1> { let Super = P5600FPQ; }
def P5600IssueFPULoad : ProcResource<1> { let Super = P5600FPQ; }

def P5600FPUDivSqrt : ProcResource<2>;

def P5600WriteFPUS : SchedWriteRes<[P5600IssueFPUS]>;
def P5600WriteFPUL : SchedWriteRes<[P5600IssueFPUL]> { let Latency = 4; }
def P5600WriteFPUL_MADDSUB : SchedWriteRes<[P5600IssueFPUL]> { let Latency = 6; }
def P5600WriteFPUDivI : SchedWriteRes<[P5600IssueFPUL, P5600FPUDivSqrt]> {
  // Best/Common/Worst case = 7 / 23 / 27
  let Latency = 23; // Using common case
  let ResourceCycles = [ 1, 23 ];
}
def P5600WriteFPUDivS : SchedWriteRes<[P5600IssueFPUL, P5600FPUDivSqrt]> {
  // Best/Common/Worst case = 7 / 23 / 27
  let Latency = 23; // Using common case
  let ResourceCycles = [ 1, 23 ];
}
def P5600WriteFPUDivD : SchedWriteRes<[P5600IssueFPUL, P5600FPUDivSqrt]> {
  // Best/Common/Worst case = 7 / 31 / 35
  let Latency = 31; // Using common case
  let ResourceCycles = [ 1, 31 ];
}
def P5600WriteFPURcpS : SchedWriteRes<[P5600IssueFPUL, P5600FPUDivSqrt]> {
  // Best/Common/Worst case = 7 / 19 / 23
  let Latency = 19; // Using common case
  let ResourceCycles = [ 1, 19 ];
}
def P5600WriteFPURcpD : SchedWriteRes<[P5600IssueFPUL, P5600FPUDivSqrt]> {
  // Best/Common/Worst case = 7 / 27 / 31
  let Latency = 27; // Using common case
  let ResourceCycles = [ 1, 27 ];
}
def P5600WriteFPURsqrtS : SchedWriteRes<[P5600IssueFPUL, P5600FPUDivSqrt]> {
  // Best/Common/Worst case = 7 / 27 / 27
  let Latency = 27; // Using common case
  let ResourceCycles = [ 1, 27 ];
}
def P5600WriteFPURsqrtD : SchedWriteRes<[P5600IssueFPUL, P5600FPUDivSqrt]> {
  // Best/Common/Worst case = 7 / 27 / 31
  let Latency = 27; // Using common case
  let ResourceCycles = [ 1, 27 ];
}
def P5600WriteFPUSqrtS : SchedWriteRes<[P5600IssueFPUL, P5600FPUDivSqrt]> {
  // Best/Common/Worst case = 7 / 27 / 31
  let Latency = 27; // Using common case
  let ResourceCycles = [ 1, 27 ];
}
def P5600WriteFPUSqrtD : SchedWriteRes<[P5600IssueFPUL, P5600FPUDivSqrt]> {
  // Best/Common/Worst case = 7 / 35 / 39
  let Latency = 35; // Using common case
  let ResourceCycles = [ 1, 35 ];
}
def P5600WriteMSAShortLogic : SchedWriteRes<[P5600IssueFPUS]>;
def P5600WriteMSAShortInt : SchedWriteRes<[P5600IssueFPUS]> { let Latency = 2; }
def P5600WriteMoveOtherUnitsToFPU : SchedWriteRes<[P5600IssueFPUS]>;
def P5600WriteMSAOther3 : SchedWriteRes<[P5600IssueFPUS]> { let Latency = 3; }
def P5600WriteMSALongInt : SchedWriteRes<[P5600IssueFPUS]> { let Latency = 5; }

// vshf.[bhwd], binsl.[bhwd], binsr.[bhwd], insert.[bhwd], sld?.[bhwd],
// bset.[bhwd], bclr.[bhwd], bneg.[bhwd], bsel_v, bseli_b
def : InstRW<[P5600WriteMSAShortInt], (instregex "^VSHF_[BHWD]$")>;
def : InstRW<[P5600WriteMSAShortInt], (instregex "^(BINSL|BINSLI)_[BHWD]$")>;
def : InstRW<[P5600WriteMSAShortInt], (instregex "^(BINSR|BINSRI)_[BHWD]$")>;
def : InstRW<[P5600WriteMSAShortInt], (instregex "^INSERT_[BHWD]$")>;
def : InstRW<[P5600WriteMSAShortInt], (instregex "^(SLD|SLDI)_[BHWD]$")>;
def : InstRW<[P5600WriteMSAShortInt], (instregex "^(BSET|BSETI)_[BHWD]$")>;
def : InstRW<[P5600WriteMSAShortInt], (instregex "^(BCLR|BCLRI)_[BHWD]$")>;
def : InstRW<[P5600WriteMSAShortInt], (instregex "^(BNEG|BNEGI)_[BHWD]$")>;
def : InstRW<[P5600WriteMSAShortInt], (instregex "^(BSEL_V|BSELI_B)$")>;
def : InstRW<[P5600WriteMSAShortInt], (instregex "^BMN*Z.*$")>;

// pcnt.[bhwd], sat_s.[bhwd], sat_u.bhwd]
def : InstRW<[P5600WriteMSAOther3], (instregex "^PCNT_[BHWD]$")>;
def : InstRW<[P5600WriteMSAOther3], (instregex "^SAT_(S|U)_[BHWD]$")>;

// bnz.[bhwdv], cfcmsa, ctcmsa
def : InstRW<[P5600WriteMSAShortLogic], (instregex "^(BNZ|BZ)_[BHWDV]$")>;
def : InstRW<[P5600WriteMSAShortLogic], (instregex "^C(F|T)CMSA$")>;

// FPUS is also used in moves from floating point and MSA registers to general
// purpose registers.
def P5600WriteMoveFPUSToOtherUnits : SchedWriteRes<[P5600IssueFPUS]> {
  let Latency = 0;
}

// FPUL is also used in moves from floating point and MSA registers to general
// purpose registers.
def P5600WriteMoveFPULToOtherUnits : SchedWriteRes<[P5600IssueFPUL]>;

// Short Pipe
// ----------
//
// abs.[ds], abs.ps, bc1[tf]l?, mov[tf].[ds], mov[tf], mov.[ds], [cm][ft]c1,
// m[ft]hc1, neg.[ds], neg.ps, nor.v, nori.b, or.v, ori.b, xor.v, xori.b,
// sdxc1, sdc1, st.[bhwd], swc1, swxc1
def : InstRW<[P5600WriteFPUS], (instrs FABS_S, FABS_D32, FABS_D64, MOVF_D32,
                                MOVF_D64, MOVF_S, MOVT_D32, MOVT_D64,
                                MOVT_S, FMOV_D32, FMOV_D64, FMOV_S, FNEG_S,
                                FNEG_D32, FNEG_D64)>;

// adds_a.[bhwd], adds_[asu].[bhwd], addvi?.[bhwd], asub_[us].[bhwd],
// aver?_[us].[bhwd], shf.[bhw], fill[bhwd], splat?.[bhwd]
def : InstRW<[P5600WriteMSAShortInt], (instregex "^ADD_A_[BHWD]$")>;
def : InstRW<[P5600WriteMSAShortInt], (instregex "^ADDS_[ASU]_[BHWD]$")>;
// TODO: ADDVI_[BHW] might be 1 cycle latency rather than 2. Need to confirm it.
def : InstRW<[P5600WriteMSAShortInt], (instregex "^ADDVI?_[BHWD]$")>;
def : InstRW<[P5600WriteMSAShortInt], (instregex "^ASUB_[US].[BHWD]$")>;
def : InstRW<[P5600WriteMSAShortInt], (instregex "^AVER?_[US].[BHWD]$")>;
def : InstRW<[P5600WriteMSAShortInt], (instregex "^SHF_[BHW]$")>;
def : InstRW<[P5600WriteMSAShortInt], (instregex "^FILL_[BHWD]$")>;
def : InstRW<[P5600WriteMSAShortInt], (instregex "^(SPLAT|SPLATI)_[BHWD]$")>;

// and.v, andi.b, move.v, ldi.[bhwd]
def : InstRW<[P5600WriteMSAShortLogic], (instregex "^MOVE_V$")>;
def : InstRW<[P5600WriteMSAShortLogic], (instregex "^LDI_[BHWD]$")>;
def : InstRW<[P5600WriteMSAShortLogic], (instregex "^(AND|OR|[XN]OR)_V$")>;
def : InstRW<[P5600WriteMSAShortLogic], (instregex "^(AND|OR|[XN]OR)I_B$")>;

// fexp2_w, fexp2_d
def : InstRW<[P5600WriteFPUS], (instregex "^FEXP2_(W|D)$")>;

// compare, converts, round to int, floating point truncate.
def : InstRW<[P5600WriteFPUS], (instregex "^(CLT|CLTI)_(S|U)_[BHWD]$")>;
def : InstRW<[P5600WriteFPUS], (instregex "^(CLE|CLEI)_(S|U)_[BHWD]$")>;
def : InstRW<[P5600WriteFPUS], (instregex "^(CEQ|CEQI)_[BHWD]$")>;
def : InstRW<[P5600WriteFPUS], (instregex "^CMP_UN_(S|D)$")>;
def : InstRW<[P5600WriteFPUS], (instregex "^CMP_UEQ_(S|D)$")>;
def : InstRW<[P5600WriteFPUS], (instregex "^CMP_EQ_(S|D)$")>;
def : InstRW<[P5600WriteFPUS], (instregex "^CMP_LT_(S|D)$")>;
def : InstRW<[P5600WriteFPUS], (instregex "^CMP_ULT_(S|D)$")>;
def : InstRW<[P5600WriteFPUS], (instregex "^CMP_LE_(S|D)$")>;
def : InstRW<[P5600WriteFPUS], (instregex "^CMP_ULE_(S|D)$")>;
def : InstRW<[P5600WriteFPUS], (instregex "^FS(AF|EQ|LT|LE|NE|OR)_(W|D)$")>;
def : InstRW<[P5600WriteFPUS], (instregex "^FSUEQ_(W|D)$")>;
def : InstRW<[P5600WriteFPUS], (instregex "^FSULE_(W|D)$")>;
def : InstRW<[P5600WriteFPUS], (instregex "^FSULT_(W|D)$")>;
def : InstRW<[P5600WriteFPUS], (instregex "^FSUNE_(W|D)$")>;
def : InstRW<[P5600WriteFPUS], (instregex "^FSUN_(W|D)$")>;
def : InstRW<[P5600WriteFPUS], (instregex "^FCAF_(W|D)$")>;
def : InstRW<[P5600WriteFPUS], (instregex "^FCEQ_(W|D)$")>;
def : InstRW<[P5600WriteFPUS], (instregex "^FCLE_(W|D)$")>;
def : InstRW<[P5600WriteFPUS], (instregex "^FCLT_(W|D)$")>;
def : InstRW<[P5600WriteFPUS], (instregex "^FCNE_(W|D)$")>;
def : InstRW<[P5600WriteFPUS], (instregex "^FCOR_(W|D)$")>;
def : InstRW<[P5600WriteFPUS], (instregex "^FCUEQ_(W|D)$")>;
def : InstRW<[P5600WriteFPUS], (instregex "^FCULE_(W|D)$")>;
def : InstRW<[P5600WriteFPUS], (instregex "^FCULT_(W|D)$")>;
def : InstRW<[P5600WriteFPUS], (instregex "^FCUNE_(W|D)$")>;
def : InstRW<[P5600WriteFPUS], (instregex "^FCUN_(W|D)$")>;
def : InstRW<[P5600WriteFPUS], (instregex "^FABS_(W|D)$")>;
def : InstRW<[P5600WriteFPUS], (instregex "^FFINT_(U|S)_(W|D)$")>;
def : InstRW<[P5600WriteFPUS], (instregex "^FFQL_(W|D)$")>;
def : InstRW<[P5600WriteFPUS], (instregex "^FFQR_(W|D)$")>;
def : InstRW<[P5600WriteFPUS], (instregex "^FTINT_(U|S)_(W|D)$")>;
def : InstRW<[P5600WriteFPUS], (instregex "^FRINT_(W|D)$")>;
def : InstRW<[P5600WriteFPUS], (instregex "^FTQ_(H|W)$")>;
def : InstRW<[P5600WriteFPUS], (instregex "^FTRUNC_(U|S)_(W|D)$")>;

// fexdo.[hw], fexupl.[wd], fexupr.[wd]
def : InstRW<[P5600WriteFPUS], (instregex "^FEXDO_(H|W)$")>;
def : InstRW<[P5600WriteFPUS], (instregex "^FEXUPL_(W|D)$")>;
def : InstRW<[P5600WriteFPUS], (instregex "^FEXUPR_(W|D)$")>;

// fclass.[wd], fmax.[wd], fmax_a.[wd], fmin.[wd], fmin_a.[wd], flog2.[wd]
def : InstRW<[P5600WriteFPUS], (instregex "^FCLASS_(W|D)$")>;
def : InstRW<[P5600WriteFPUS], (instregex "^FMAX_A_(W|D)$")>;
def : InstRW<[P5600WriteFPUS], (instregex "^FMAX_(W|D)$")>;
def : InstRW<[P5600WriteFPUS], (instregex "^FMIN_A_(W|D)$")>;
def : InstRW<[P5600WriteFPUS], (instregex "^FMIN_(W|D)$")>;
def : InstRW<[P5600WriteFPUS], (instregex "^FLOG2_(W|D)$")>;

// interleave right/left, interleave even/odd, insert
def : InstRW<[P5600WriteMSAShortLogic], (instregex "^(ILVR|ILVL)_[BHWD]$")>;
def : InstRW<[P5600WriteMSAShortLogic], (instregex "^(ILVEV|ILVOD)_[BHWD]$")>;
def : InstRW<[P5600WriteMSAShortLogic], (instregex "^INSVE_[BHWD]$")>;

// subs_?.[bhwd], subsus_?.[bhwd], subsuu_?.[bhwd], subvi.[bhwd], subv.[bhwd],
def : InstRW<[P5600WriteMSAShortInt], (instregex "^SUBS_(S|U)_[BHWD]$")>;
def : InstRW<[P5600WriteMSAShortInt], (instregex "^SUBSUS_(S|U)_[BHWD]$")>;
def : InstRW<[P5600WriteMSAShortInt], (instregex "^SUBSUU_(S|U)_[BHWD]$")>;
def : InstRW<[P5600WriteMSAShortInt], (instregex "^SUBVI_[BHWD]$")>;
def : InstRW<[P5600WriteMSAShortInt], (instregex "^SUBV_[BHWD]$")>;

// mod_[su].[bhwd], div_[su].[bhwd]
def : InstRW<[P5600WriteFPUDivI], (instregex "^MOD_(S|U)_[BHWD]$")>;
def : InstRW<[P5600WriteFPUDivI], (instregex "^DIV_(S|U)_[BHWD]$")>;

// hadd_[su].[bhwd], hsub_[su].[bhwd], max_[sua].[bhwd], min_[sua].[bhwd],
// maxi_[su].[bhwd], mini_[su].[bhwd], sra?.[bhwd], srar?.[bhwd], srlr.[bhwd],
// sll?.[bhwd], pckev.[bhwd], pckod.[bhwd], nloc.[bhwd], nlzc.[bhwd],
// insve.[bhwd]
def : InstRW<[P5600WriteMSAShortLogic], (instregex "^HADD_(S|U)_[BHWD]$")>;
def : InstRW<[P5600WriteMSAShortLogic], (instregex "^HSUB_(S|U)_[BHWD]$")>;
def : InstRW<[P5600WriteMSAShortLogic], (instregex "^(MAX|MIN)_S_[BHWD]$")>;
def : InstRW<[P5600WriteMSAShortLogic], (instregex "^(MAX|MIN)_U_[BHWD]$")>;
def : InstRW<[P5600WriteMSAShortLogic], (instregex "^(MAX|MIN)_A_[BHWD]$")>;
def : InstRW<[P5600WriteMSAShortLogic], (instregex "^(MAXI|MINI)_(S|U)_[BHWD]$")>;
def : InstRW<[P5600WriteMSAShortLogic], (instregex "^(SRA|SRAI)_[BHWD]$")>;
def : InstRW<[P5600WriteMSAShortLogic], (instregex "^(SRL|SRLI)_[BHWD]$")>;
def : InstRW<[P5600WriteMSAShortLogic], (instregex "^(SRAR|SRARI)_[BHWD]$")>;
def : InstRW<[P5600WriteMSAShortLogic], (instregex "^(SRLR|SRLRI)_[BHWD]$")>;
def : InstRW<[P5600WriteMSAShortLogic], (instregex "^(SLL|SLLI)_[BHWD]$")>;
def : InstRW<[P5600WriteMSAShortLogic], (instregex "^(PCKEV|PCKOD)_[BHWD]$")>;
def : InstRW<[P5600WriteMSAShortLogic], (instregex "^(NLOC|NLZC)_[BHWD]$")>;

// Long Pipe
// ----------
//
// add.[ds], add.ps, cvt.d.[sw], cvt.s.[dw], cvt.w.[sd], cvt.[sw].ps,
// cvt.ps.[sw], c.<cc>.[ds], c.<cc>.ps, mul.[ds], mul.ps, sub.[ds], sub.ps,
// trunc.w.[ds], trunc.w.ps
def : InstRW<[P5600WriteFPUL],
             (instrs FADD_D32, FADD_D64, FADD_S, FMUL_D32, FMUL_D64, FMUL_S,
              FSUB_D32, FSUB_D64, FSUB_S)>;
def : InstRW<[P5600WriteFPUL], (instregex "^TRUNC_(L|W)_(S|D32|D64)$")>;
def : InstRW<[P5600WriteFPUL],
             (instregex "^CVT_(S|D32|D64|L|W)_(S|D32|D64|L|W)$")>;
def : InstRW<[P5600WriteFPUL], (instregex "^C_[A-Z]+_(S|D32|D64)$")>;
def : InstRW<[P5600WriteFPUL], (instregex "^FCMP_(S32|D32|D64)$")>;
def : InstRW<[P5600WriteFPUL], (instregex "^PseudoCVT_(S|D32|D64)_(L|W)$")>;

// div.[ds], div.ps
def : InstRW<[P5600WriteFPUDivS], (instrs FDIV_S)>;
def : InstRW<[P5600WriteFPUDivD], (instrs FDIV_D32, FDIV_D64)>;

// sqrt.[ds], sqrt.ps
def : InstRW<[P5600WriteFPUSqrtS], (instrs FSQRT_S)>;
def : InstRW<[P5600WriteFPUSqrtD], (instrs FSQRT_D32, FSQRT_D64)>;

// frcp.[wd], frsqrt.[wd]
def : InstRW<[P5600WriteFPURsqrtD], (instregex "^FRCP_(W|D)$")>;
def : InstRW<[P5600WriteFPURsqrtD], (instregex "^FRSQRT_(W|D)$")>;

def : InstRW<[P5600WriteFPURsqrtD], (instrs RECIP_D32, RECIP_D64, RSQRT_D32,
                                     RSQRT_D64)>;
def : InstRW<[P5600WriteFPURsqrtS], (instrs RECIP_S, RSQRT_S)>;

// fmadd.[wd], fmsubb.[wd], fdiv.[wd], fsqrt.[wd], fmul.[wd], fadd.[wd],
// fsub.[wd]
def : InstRW<[P5600WriteFPUL_MADDSUB], (instregex "^FMADD_(W|D)$")>;
def : InstRW<[P5600WriteFPUL_MADDSUB], (instregex "^FMSUB_(W|D)$")>;
def : InstRW<[P5600WriteFPUDivS], (instregex "^FDIV_W$")>;
def : InstRW<[P5600WriteFPUDivD], (instregex "^FDIV_D$")>;
def : InstRW<[P5600WriteFPUSqrtS], (instregex "^FSQRT_W$")>;
def : InstRW<[P5600WriteFPUSqrtD], (instregex "^FSQRT_D$")>;
def : InstRW<[P5600WriteFPUL], (instregex "^FMUL_(W|D)$")>;
def : InstRW<[P5600WriteFPUL], (instregex "^FADD_(W|D)$")>;
def : InstRW<[P5600WriteFPUL], (instregex "^FSUB_(W|D)$")>;

// dpadd_?.[bhwd], dpsub_?.[bhwd], dotp_?.[bhwd], msubv.[bhwd], maddv.[bhwd]
// mulv.[bhwd].
def : InstRW<[P5600WriteMSALongInt], (instregex "^DPADD_(S|U)_[HWD]$")>;
def : InstRW<[P5600WriteMSALongInt], (instregex "^DPSUB_(S|U)_[HWD]$")>;
def : InstRW<[P5600WriteMSALongInt], (instregex "^DOTP_(S|U)_[HWD]$")>;
def : InstRW<[P5600WriteMSALongInt], (instregex "^MSUBV_[BHWD]$")>;
def : InstRW<[P5600WriteMSALongInt], (instregex "^MADDV_[BHWD]$")>;
def : InstRW<[P5600WriteMSALongInt], (instregex "^MULV_[BHWD]$")>;

def : InstRW<[P5600WriteMSALongInt], (instregex "^MADDR_Q_[HW]$")>;
def : InstRW<[P5600WriteMSALongInt], (instregex "^MADD_Q_[HW]$")>;
def : InstRW<[P5600WriteMSALongInt], (instregex "^MSUBR_Q_[HW]$")>;
def : InstRW<[P5600WriteMSALongInt], (instregex "^MSUB_Q_[HW]$")>;
def : InstRW<[P5600WriteMSALongInt], (instregex "^MULR_Q_[HW]$")>;
def : InstRW<[P5600WriteMSALongInt], (instregex "^MUL_Q_[HW]$")>;

// madd.[ds], msub.[ds], nmadd.[ds], nmsub.[ds],
// Operand 0 is read on cycle 5. All other operands are read on operand 0.
def : InstRW<[SchedReadAdvance<5>, P5600WriteFPUL_MADDSUB],
             (instrs MADD_D32, MADD_D64, MADD_S, MSUB_D32, MSUB_D64, MSUB_S,
              NMADD_D32, NMADD_D64, NMADD_S, NMSUB_D32, NMSUB_D64, NMSUB_S)>;

// madd.ps, msub.ps, nmadd.ps, nmsub.ps
// Operand 0 and 1 are read on cycle 5. All others are read on operand 0.
// (none of these instructions exist in the backend yet)

// Load Pipe
// ---------
//
// This is typically used in conjunction with the load pipeline under the AGQ
// All the instructions are in the 'Tricky Instructions' section.

def P5600WriteLoadOtherUnitsToFPU : SchedWriteRes<[P5600IssueFPULoad]> {
  let Latency = 4;
}

// Tricky Instructions
// ===================
//
// These instructions are split across multiple uops (in different pipelines)
// that must cooperate to complete the operation

// FIXME: This isn't quite right since the implementation of WriteSequence
//        current aggregates the resources and ignores the exact cycle they are
//        used.
def P5600WriteMoveGPRToFPU : WriteSequence<[P5600WriteMoveGPRToOtherUnits,
                                            P5600WriteMoveOtherUnitsToFPU]>;

// FIXME: This isn't quite right since the implementation of WriteSequence
//        current aggregates the resources and ignores the exact cycle they are
//        used.
def P5600WriteMoveFPUToGPR : WriteSequence<[P5600WriteMoveFPUSToOtherUnits,
                                            P5600WriteGPRFromBypass]>;

// FIXME: This isn't quite right since the implementation of WriteSequence
//        current aggregates the resources and ignores the exact cycle they are
//        used.
def P5600WriteStoreFPUS : WriteSequence<[P5600WriteMoveFPUSToOtherUnits,
                                         P5600WriteStoreFromOtherUnits]>;

// FIXME: This isn't quite right since the implementation of WriteSequence
//        current aggregates the resources and ignores the exact cycle they are
//        used.
def P5600WriteStoreFPUL : WriteSequence<[P5600WriteMoveFPULToOtherUnits,
                                         P5600WriteStoreFromOtherUnits]>;

// FIXME: This isn't quite right since the implementation of WriteSequence
//        current aggregates the resources and ignores the exact cycle they are
//        used.
def P5600WriteLoadFPU : WriteSequence<[P5600WriteLoadToOtherUnits,
                                       P5600WriteLoadOtherUnitsToFPU]>;

// ctc1, mtc1, mthc1
def : InstRW<[P5600WriteMoveGPRToFPU], (instrs CTC1, MTC1, MTC1_D64, MTHC1_D32,
                                        MTHC1_D64, BuildPairF64,
                                        BuildPairF64_64)>;

// copy.[su]_[bhwd]
def : InstRW<[P5600WriteMoveFPUToGPR], (instregex "^COPY_U_[BHW]$")>;
def : InstRW<[P5600WriteMoveFPUToGPR], (instregex "^COPY_S_[BHWD]$")>;

// bc1[ft], cfc1, mfc1, mfhc1, movf, movt
def : InstRW<[P5600WriteMoveFPUToGPR], (instrs BC1F, BC1FL, BC1T, BC1TL, CFC1,
                                        MFC1, MFC1_D64, MFHC1_D32, MFHC1_D64,
                                        MOVF_I, MOVT_I, ExtractElementF64,
                                        ExtractElementF64_64)>;

// swc1, swxc1, st.[bhwd]
def : InstRW<[P5600WriteStoreFPUS], (instrs SDC1, SDXC1, SUXC1, SWC1, SWXC1)>;
def : InstRW<[P5600WriteStoreFPUS], (instregex "^ST_[BHWD]$")>;

// movn.[ds], movz.[ds]
def : InstRW<[P5600WriteStoreFPUL], (instrs MOVN_I_D32, MOVN_I_D64, MOVN_I_S,
                                     MOVZ_I_D32, MOVZ_I_D64, MOVZ_I_S)>;

// l[dw]x?c1, ld.[bhwd]
def : InstRW<[P5600WriteLoadFPU], (instrs LDC1, LDXC1, LWC1, LWXC1, LUXC1)>;
def : InstRW<[P5600WriteLoadFPU], (instregex "LD_[BHWD]")>;

// Unsupported Instructions
// ========================
//
// The following instruction classes are never valid on P5600.
//   II_DADDIU, II_DADDU, II_DMFC1, II_DMTC1, II_DMULT, II_DMULTU, II_DROTR,
//   II_DROTR32, II_DROTRV, II_DDIV, II_DSLL, II_DSLL32, II_DSLLV, II_DSRA,
//   II_DSRA32, II_DSRAV, II_DSRL, II_DSRL32, II_DSRLV, II_DSUBU, II_DDIVU,
//   II_JALRC, II_LD, II_LD[LR], II_RESTORE, II_SAVE, II_SD, II_SDC1, II_SD[LR]
//
// The following instructions are never valid on P5600.
//   addq.ph, repl.ph, repl.qb, subq.ph, subu_s.qb
//
// Guesswork
// =========
//
// This section is largely temporary guesswork.

// ceil.[lw].[ds], floor.[lw].[ds]
// Reason behind guess: trunc.[lw].ds and the various cvt's are in FPUL
def : InstRW<[P5600WriteFPUL], (instregex "^CEIL_(L|W)_(S|D32|D64)$")>;
def : InstRW<[P5600WriteFPUL], (instregex "^FLOOR_(L|W)_(S|D32|D64)$")>;
def : InstRW<[P5600WriteFPUL], (instregex "^ROUND_(L|W)_(S|D32|D64)$")>;

// rotrv
// Reason behind guess: rotr is in the same category and the two register forms
//                      generally follow the immediate forms in this category
def : InstRW<[P5600WriteEitherALU], (instrs ROTRV)>;
}
