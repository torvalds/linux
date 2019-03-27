//=- MipsScheduleGeneric.td - Generic Scheduling Definitions -*- tablegen -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file describes the interAptiv processor in a manner of speaking. It
// describes a hypothetical version of the in-order MIPS32R2 interAptiv with all
// branches of the MIPS ISAs, ASEs and ISA variants. The itinerary lists are
// broken down into per ISA lists, so that this file can be used to rapidly
// develop new schedule models.
//
//===----------------------------------------------------------------------===//
def MipsGenericModel : SchedMachineModel {
  int IssueWidth = 1;
  int MicroOpBufferSize = 0;

  // These figures assume an L1 hit.
  int LoadLatency = 2;
  int MispredictPenalty = 4;

  int HighLatency = 37;
  list<Predicate> UnsupportedFeatures = [];

  let CompleteModel = 0;
  let PostRAScheduler = 1;

  // FIXME: Remove when all errors have been fixed.
  let FullInstRWOverlapCheck = 0;
}

let SchedModel = MipsGenericModel in {

// ALU Pipeline
// ============

def GenericALU : ProcResource<1> { let BufferSize = 1; }
def GenericIssueALU : ProcResource<1> { let Super = GenericALU; }

def GenericWriteALU : SchedWriteRes<[GenericIssueALU]>;

// and, lui, nor, or, slti, sltiu, sub, subu, xor
// add, addi, addiu, addu, andi, ori, rotr, se[bh], sllv?, sr[al]v?, slt, sltu,
// xori
def : ItinRW<[GenericWriteALU], [II_ADD, II_ADDU, II_ADDI, II_ADDIU, II_ANDI,
                                 II_AND, II_ANDI, II_CLO, II_CLZ, II_EXT,
                                 II_INS, II_LUI, II_MULT, II_MULTU, II_NOR,
                                 II_ORI, II_OR, II_ROTR, II_ROTRV, II_SEB,
                                 II_SEH, II_SLTI_SLTIU, II_SLT_SLTU, II_SLL,
                                 II_SRA, II_SRL, II_SLLV, II_SRAV, II_SRLV,
                                 II_SSNOP, II_SUB, II_SUBU, II_WSBH, II_XOR,
                                 II_XORI]>;

def : InstRW<[GenericWriteALU], (instrs COPY)>;

def GenericMDU : ProcResource<1> { let BufferSize = 1; }
def GenericIssueMDU : ProcResource<1> { let Super = GenericALU; }
def GenericIssueDIV : ProcResource<1> { let Super = GenericMDU; }
def GenericWriteHILO : SchedWriteRes<[GenericIssueMDU]>;
def GenericWriteALULong : SchedWriteRes<[GenericIssueALU]> { let Latency = 5; }
def GenericWriteMove : SchedWriteRes<[GenericIssueALU]> { let Latency = 2; }

def : ItinRW<[GenericWriteHILO], [II_MADD, II_MADDU, II_MSUB, II_MSUBU]>;

def GenericWriteMDUtoGPR : SchedWriteRes<[GenericIssueMDU]> {
  let Latency = 5;
}

def : ItinRW<[GenericWriteMDUtoGPR], [II_MUL]>;

def GenericWriteDIV : SchedWriteRes<[GenericIssueDIV]> {
  // Estimated worst case
  let Latency = 33;
  let ResourceCycles = [33];
}
def GenericWriteDIVU : SchedWriteRes<[GenericIssueDIV]> {
  // Estimated worst case
  let Latency = 31;
  let ResourceCycles = [31];
}

def : ItinRW<[GenericWriteDIV], [II_DIV]>;

def : ItinRW<[GenericWriteDIVU], [II_DIVU]>;

// MIPS64
// ======

def : ItinRW<[GenericWriteALU], [II_DADDIU, II_DADDU, II_DADDI, II_DADD,
                                 II_DCLO, II_DCLZ, II_DROTR, II_DROTR32,
                                 II_DROTRV, II_DSBH, II_DSHD, II_DSLL,
                                 II_DSLL32, II_DSLLV, II_DSRA, II_DSRA32,
                                 II_DSRAV, II_DSRL, II_DSRL32, II_DSRLV,
                                 II_DSUBU, II_DSUB]>;

def : ItinRW<[GenericWriteDIV], [II_DDIV]>;

def : ItinRW<[GenericWriteDIVU], [II_DDIVU]>;

def : ItinRW<[GenericWriteMDUtoGPR], [II_DMUL]>;

def : ItinRW<[GenericWriteHILO], [II_DMULU, II_DMULT, II_DMULTU]>;

// MIPS16e
// =======

def : ItinRW<[GenericWriteALU], [IIM16Alu, IIPseudo]>;

// microMIPS
// =========

def : ItinRW<[GenericWriteALU], [II_MOVE, II_LI, II_NOT]>;

// MIPSR6
// ======

def GenericWriteMul : SchedWriteRes<[GenericIssueMDU]> { let Latency = 4; }
def : ItinRW<[GenericWriteMul], [II_MUH, II_MUHU, II_MULU]>;

def : ItinRW<[GenericWriteDIV], [II_MOD, II_MODU]>;

def : ItinRW<[GenericWriteALU], [II_ADDIUPC, II_ALIGN, II_ALUIPC, II_AUI,
                                 II_AUIPC, II_BITSWAP, II_LSA, II_SELCCZ]>;

// MIPS64R6
// ========

def : ItinRW<[GenericWriteALU], [II_DALIGN, II_DAHI, II_DATI, II_DAUI,
                               II_DBITSWAP, II_DLSA]>;

def : ItinRW<[GenericWriteMDUtoGPR], [II_DMUH, II_DMUHU]>;
def : ItinRW<[GenericWriteDIV], [II_DMOD, II_DMODU]>;

// clo, clz, di, mfhi, mflo
def : ItinRW<[GenericWriteALULong], [II_MFHI_MFLO]>;
def : ItinRW<[GenericWriteALU], [II_MOVN, II_MOVZ]>;
def : ItinRW<[GenericWriteMove], [II_MTHI_MTLO, II_RDHWR]>;


// CTISTD Pipeline
// ---------------

def GenericIssueCTISTD : ProcResource<1> { let Super = GenericALU; }

def GenericLDST : ProcResource<1> { let BufferSize = 1; }
def GenericIssueLDST : ProcResource<1> { let Super = GenericLDST; }

def GenericWriteJump : SchedWriteRes<[GenericIssueCTISTD]>;
def GenericWriteJumpAndLink : SchedWriteRes<[GenericIssueCTISTD]> {
  let Latency = 2;
}

// b, beq, beql, bg[et]z, bl[et]z, bne, bnel, j, syscall, jal, bltzal, jalx,
// jalr, jr.hb, jr, jalr.hb, jarlc, jialc
def : ItinRW<[GenericWriteJump], [II_B, II_BCC, II_BCCZ, II_BCCZAL, II_J,
                                  II_JR, II_JR_HB, II_ERET, II_ERETNC,
                                  II_DERET]>;

def : ItinRW<[GenericWriteJumpAndLink], [II_JAL, II_JALR, II_JALR_HB,
                                         II_BC2CCZ]>;

def : ItinRW<[GenericWriteJump], [II_JRC, II_JRADDIUSP]>;

def : ItinRW<[GenericWriteJumpAndLink], [II_BCCZALS, II_JALS, II_JALRS]>;

// MIPSR6
// ======

def : ItinRW<[GenericWriteJumpAndLink], [II_BALC, II_JALRC, II_JIALC]>;

def : ItinRW<[GenericWriteJump], [II_JIC, II_BC, II_BCCC, II_BCCZC]>;


def GenericWriteTrap : SchedWriteRes<[GenericIssueCTISTD]>;

def : ItinRW<[GenericWriteTrap], [II_BREAK, II_SYSCALL, II_TEQ, II_TEQI,
                                  II_TGE, II_TGEI, II_TGEIU, II_TGEU, II_TNE,
                                  II_TNEI, II_TLT, II_TLTI, II_TLTU, II_TTLTIU,
                                  II_TRAP, II_SDBBP, II_SIGRIE]>;

// COP0 Pipeline
// =============

def GenericCOP0 : ProcResource<1> { let BufferSize = 1; }

def GenericIssueCOP0 : ProcResource<1> { let Super = GenericCOP0; }
def GenericWriteCOP0TLB : SchedWriteRes<[GenericIssueCOP0]> { let Latency = 4; }
def GenericWriteCOP0 : SchedWriteRes<[GenericIssueCOP0]> { let Latency = 3; }
def GenericReadCOP0 : SchedWriteRes<[GenericIssueCOP0]> { let Latency = 2; }
def GenericReadWritePGPR : SchedWriteRes<[GenericIssueCOP0]>;
def GenericReadWriteCOP0Long : SchedWriteRes<[GenericIssueCOP0]> {
  let Latency = 5;
}
def GenericWriteCOP0Short : SchedWriteRes<[GenericIssueCOP0]>;

def : ItinRW<[GenericWriteCOP0TLB], [II_TLBP, II_TLBR, II_TLBWI, II_TLBWR]>;
def : ItinRW<[GenericWriteCOP0TLB], [II_TLBINV, II_TLBINVF]>;

def : ItinRW<[GenericReadCOP0], [II_MFC0]>;
def : ItinRW<[GenericWriteCOP0], [II_MTC0]>;

def : ItinRW<[GenericWriteCOP0], [II_EVP, II_DVP]>;

// MIPSR5
// ======
def : ItinRW<[GenericReadCOP0], [II_MFHC0]>;
def : ItinRW<[GenericWriteCOP0], [II_MTHC0]>;

// MIPS64
// ======

def : ItinRW<[GenericReadCOP0], [II_DMFC0]>;
def : ItinRW<[GenericWriteCOP0], [II_DMTC0]>;

def : ItinRW<[GenericWriteCOP0], [II_RDPGPR, II_WRPGPR]>;

def : ItinRW<[GenericWriteCOP0], [II_DI, II_EI]>;

def : ItinRW<[GenericWriteCOP0], [II_EHB, II_PAUSE, II_WAIT]>;

def GenericCOP2 : ProcResource<1> { let BufferSize = 1; }
def GenericWriteCOPOther : SchedWriteRes<[GenericCOP2]>;

def : ItinRW<[GenericWriteCOPOther], [II_MFC2, II_MTC2, II_DMFC2, II_DMTC2]>;

// LDST Pipeline
// -------------

def GenericWriteLoad : SchedWriteRes<[GenericIssueLDST]> {
  let Latency = 2;
}

def GenericWritePref : SchedWriteRes<[GenericIssueLDST]>;
def GenericWriteSync : SchedWriteRes<[GenericIssueLDST]>;
def GenericWriteCache : SchedWriteRes<[GenericIssueLDST]> { let Latency = 5; }

def GenericWriteStore : SchedWriteRes<[GenericIssueLDST]>;
def GenericWriteStoreSC : SchedWriteRes<[GenericIssueLDST]> { let Latency = 2; }

def GenericWriteGPRFromBypass : SchedWriteRes<[GenericIssueLDST]> {
  let Latency = 2;
}

def GenericWriteStoreFromOtherUnits : SchedWriteRes<[GenericIssueLDST]>;
def GenericWriteLoadToOtherUnits : SchedWriteRes<[GenericIssueLDST]> {
  let Latency = 0;
}

// l[bhw], l[bh]u, ll
def : ItinRW<[GenericWriteLoad], [II_LB, II_LBU, II_LH, II_LHU, II_LW, II_LL,
                                  II_LWC2, II_LWC3, II_LDC2, II_LDC3]>;

// lw[lr]
def : ItinRW<[GenericWriteLoad], [II_LWL, II_LWR]>;

// MIPS64 loads
def : ItinRW<[GenericWriteLoad], [II_LD, II_LLD, II_LWU]>;

// ld[lr]
def : ItinRW<[GenericWriteLoad], [II_LDL, II_LDR]>;

// MIPS32 EVA
def : ItinRW<[GenericWriteLoad], [II_LBE, II_LBUE, II_LHE, II_LHUE, II_LWE,
                                  II_LLE]>;

def : ItinRW<[GenericWriteLoad], [II_LWLE, II_LWRE]>;

// MIPS MT instructions
// ====================

def : ItinRW<[GenericWriteMove], [II_DMT, II_DVPE, II_EMT, II_EVPE, II_MFTR,
                                  II_MTTR]>;

def : ItinRW<[GenericReadWriteCOP0Long], [II_YIELD]>;

def : ItinRW<[GenericWriteCOP0Short], [II_FORK]>;

// MIPS32R6 and MIPS16e
// ====================

def : ItinRW<[GenericWriteLoad], [II_LWPC]>;

// MIPS64R6
// ====================

def : ItinRW<[GenericWriteLoad], [II_LWUPC, II_LDPC]>;


// s[bhw], sc, s[dw]c[23]
def : ItinRW<[GenericWriteStore], [II_SB, II_SH, II_SW, II_SWC2, II_SWC3,
                                   II_SDC2, II_SDC3]>;

def : ItinRW<[GenericWriteStoreSC], [II_SC]>;

// PreMIPSR6 sw[lr]
def : ItinRW<[GenericWriteStore], [II_SWL, II_SWR]>;

// EVA ASE stores
def : ItinRW<[GenericWriteStore], [II_SBE, II_SHE, II_SWE, II_SCE]>;

def : ItinRW<[GenericWriteStore], [II_SWLE, II_SWRE]>;

// MIPS64
// ======

def : ItinRW<[GenericWriteStore], [II_SD, II_SCD]>;

// PreMIPSR6 stores
// ================

def : ItinRW<[GenericWriteStore], [II_SDL, II_SDR]>;

// MIPS16e
// =======

def : ItinRW<[GenericWriteLoad], [II_RESTORE]>;

def : ItinRW<[GenericWriteStore], [II_SAVE]>;

// microMIPS
// =========

def : ItinRW<[GenericWriteLoad], [II_LWM, II_LWP, II_LWXS]>;

def : ItinRW<[GenericWriteStore], [II_SWM, II_SWP]>;

// pref
def : ItinRW<[GenericWritePref], [II_PREF]>;

def : ItinRW<[GenericWritePref], [II_PREFE]>;

// cache
def : ItinRW<[GenericWriteCache], [II_CACHE]>;

def : ItinRW<[GenericWriteCache], [II_CACHEE]>;

// sync
def : ItinRW<[GenericWriteSync], [II_SYNC]>;

def : ItinRW<[GenericWriteSync], [II_SYNCI]>;

// FPU Pipelines
// =============

def GenericFPQ : ProcResource<1> { let BufferSize = 1; }
def GenericIssueFPUS : ProcResource<1> { let Super = GenericFPQ; }
def GenericIssueFPUL : ProcResource<1> { let Super = GenericFPQ; }
def GenericIssueFPULoad : ProcResource<1> { let Super = GenericFPQ; }
def GenericIssueFPUStore : ProcResource<1> { let Super = GenericFPQ; }
def GenericIssueFPUMove : ProcResource<1> { let Super = GenericFPQ; }
def GenericFPUDivSqrt : ProcResource<1> { let Super = GenericFPQ; }

// The floating point compare of the 24k series including interAptiv has a
// listed latency of 1-2. Using the higher latency here.

def GenericWriteFPUCmp : SchedWriteRes<[GenericIssueFPUS]> { let Latency = 2; }
def GenericWriteFPUS : SchedWriteRes<[GenericIssueFPUS]> { let Latency = 4; }
def GenericWriteFPUL : SchedWriteRes<[GenericIssueFPUL]> { let Latency = 5; }
def GenericWriteFPUStore : SchedWriteRes<[GenericIssueFPUStore]> { let
  Latency = 1;
}
def GenericWriteFPULoad : SchedWriteRes<[GenericIssueFPULoad]> {
  let Latency = 2;
}
def GenericWriteFPUMoveFP : SchedWriteRes<[GenericIssueFPUMove]> {
  let Latency = 4;
}
def GenericWriteFPUMoveGPRFPU : SchedWriteRes<[GenericIssueFPUMove]> {
  let Latency = 2;
}
def GenericWriteFPUDivS : SchedWriteRes<[GenericFPUDivSqrt]> {
  let Latency = 17;
  let ResourceCycles = [ 14 ];
}
def GenericWriteFPUDivD : SchedWriteRes<[GenericFPUDivSqrt]> {
  let Latency = 32;
  let ResourceCycles = [ 29 ];
}
def GenericWriteFPURcpS : SchedWriteRes<[GenericFPUDivSqrt]> {
  let Latency = 13;
  let ResourceCycles = [ 10 ];
}
def GenericWriteFPURcpD : SchedWriteRes<[GenericFPUDivSqrt]> {
  let Latency = 25;
  let ResourceCycles = [ 21 ];
}
def GenericWriteFPURsqrtS : SchedWriteRes<[GenericFPUDivSqrt]> {
  let Latency = 17;
  let ResourceCycles = [ 14 ];
}
def GenericWriteFPURsqrtD : SchedWriteRes<[GenericFPUDivSqrt]> {
  let Latency = 32;
  let ResourceCycles = [ 29 ];
}
def GenericWriteFPUSqrtS : SchedWriteRes<[GenericFPUDivSqrt]> {
  let Latency = 17;
  let ResourceCycles = [ 14 ];
}
def GenericWriteFPUSqrtD : SchedWriteRes<[GenericFPUDivSqrt]> {
  let Latency = 29;
  let ResourceCycles = [ 29 ];
}

// Floating point compare and branch
// ---------------------------------
//
// c.<cc>.[ds], bc1[tf], bc1[tf]l
def : ItinRW<[GenericWriteFPUCmp], [II_C_CC_D, II_C_CC_S, II_BC1F, II_BC1T,
                                    II_BC1FL, II_BC1TL]>;

def : ItinRW<[GenericWriteFPUCmp], [II_CMP_CC_D, II_CMP_CC_S]>;

// Short Pipe
// ----------
//
// abs.[ds], abs.ps, add.[ds], neg.[ds], neg.ps, madd.s, msub.s, nmadd,s
// nmsub.s, sub.[ds], mul.s

def : ItinRW<[GenericWriteFPUS], [II_ABS, II_ADD_D, II_ADD_S, II_MADD_S,
                                  II_MSUB_S, II_MUL_S, II_NEG, II_NMADD_S,
                                  II_NMSUB_S, II_SUB_S, II_SUB_D]>;
// mov[tf].[ds]

def : ItinRW<[GenericWriteFPUS], [II_MOVF_S, II_MOVF_D, II_MOVT_S, II_MOVT_D]>;

// MIPSR6
// ------
//
// sel(eq|ne).[ds], max.[ds], maxa.[ds], min.[ds], mina.[ds], class.[ds]
def : ItinRW<[GenericWriteFPUS], [II_SELCCZ_S, II_SELCCZ_D, II_MAX_S,
                                  II_MAX_D, II_MAXA_S, II_MAXA_D, II_MIN_S,
                                  II_MIN_D, II_MINA_S, II_MINA_D, II_CLASS_S,
                                  II_CLASS_D]>;

// Long Pipe
// ----------
//
// nmadd.d, nmsub.d, mul.[ds], mul.ps, ceil.[wl].[sd], cvt.d.[sw], cvt.s.[dw],
// cvt.w.[sd], cvt.[sw].ps, trunc.w.[ds], trunc.w.ps, floor.[ds],
// round.[lw].[ds], floor.[lw].ds

// madd.d, msub.dm mul.d, mul.ps, nmadd.d, nmsub.d, ceil.[wl].[sd], cvt.d.[sw],
// cvt.s.[dw], cvt.w.[sd], cvt.[sw].ps, round.[lw].[ds], floor.[lw].ds,
// trunc.w.[ds], trunc.w.ps,
def : ItinRW<[GenericWriteFPUL], [II_MADD_D, II_MSUB_D, II_MUL_D, II_NMADD_D,
                                  II_NMSUB_D, II_CEIL, II_CVT,
                                  II_FLOOR, II_ROUND, II_TRUNC]>;

// div.[ds], div.ps
def : ItinRW<[GenericWriteFPUDivS], [II_DIV_S]>;
def : ItinRW<[GenericWriteFPUDivD], [II_DIV_D]>;

// sqrt.[ds], sqrt.ps
def : ItinRW<[GenericWriteFPUSqrtS], [II_SQRT_S]>;
def : ItinRW<[GenericWriteFPUSqrtD], [II_SQRT_D]>;

// rsqrt.[ds], recip.[ds]
def : ItinRW<[GenericWriteFPURcpS], [II_RECIP_S, II_RSQRT_S]>;
def : ItinRW<[GenericWriteFPURcpD], [II_RECIP_D, II_RSQRT_D]>;

// MIPSR6
// ======
//
// rint.[ds]
def : ItinRW<[GenericWriteFPUL], [II_RINT_S, II_RINT_D]>;

// Load Pipe
// ---------

// ctc1, mtc1, mthc1, cfc1, mfc1, mfhc1
def : ItinRW<[GenericWriteFPUMoveGPRFPU], [II_CFC1, II_CTC1, II_MFC1, II_MFHC1,
                                           II_MTC1, II_MTHC1]>;

// swc1, swxc1
def : ItinRW<[GenericWriteFPUStore], [II_SDC1, II_SDXC1, II_SUXC1, II_SWC1,
                                      II_SWXC1]>;

// movn.[ds], movz.[ds]
def : ItinRW<[GenericWriteFPUMoveFP], [II_MOV_D, II_MOV_S, II_MOVF, II_MOVT,
                                       II_MOVN_D, II_MOVN_S, II_MOVZ_D,
                                       II_MOVZ_S]>;

// l[dw]x?c1
def : ItinRW<[GenericWriteFPULoad], [II_LDC1, II_LDXC1, II_LUXC1, II_LWC1,
                                     II_LWXC1]>;

// MIPS64
// ======

def : ItinRW<[GenericWriteFPUMoveGPRFPU], [II_DMFC1, II_DMTC1]>;

// MIPSR6
// ======

def : ItinRW<[GenericWriteFPUS], [II_MADDF_S, II_MSUBF_S]>;

def : ItinRW<[GenericWriteFPUS], [II_MADDF_D, II_MSUBF_D]>;

def : ItinRW<[GenericWriteFPUCmp], [II_BC1CCZ, II_SEL_D, II_SEL_S]>;

// Cavium Networks MIPS (cnMIPS) - Octeon, HasCnMips
// =================================================

def : ItinRW<[GenericWriteALU], [II_SEQ_SNE, II_SEQI_SNEI, II_POP, II_BADDU,
                                 II_BBIT]>;

// MIPS DSP ASE, HasDSP
// ====================

def GenericDSP : ProcResource<1> { let BufferSize = 1; }
def GenericDSPShort : SchedWriteRes<[GenericDSP]> { let Latency = 2; }
def GenericDSPLong : SchedWriteRes<[GenericDSP]> { let Latency = 6; }
def GenericDSPBypass : SchedWriteRes<[GenericDSP]> { let Latency = 1; }
def GenericDSPMTHILO : SchedWriteRes<[GenericDSP]> { let Latency = 5; }
def GenericDSPLoad : SchedWriteRes<[GenericDSP]> { let Latency = 4; }
def GenericDSPMTHLIP : SchedWriteRes<[GenericDSP]> { let Latency = 5; }

def : InstRW<[GenericDSPLong], (instregex "^EXTRV_RS_W$")>;
def : InstRW<[GenericDSPLong], (instregex "^EXTRV_R_W$")>;
def : InstRW<[GenericDSPLong], (instregex "^EXTRV_S_H$")>;
def : InstRW<[GenericDSPLong], (instregex "^EXTRV_W$")>;
def : InstRW<[GenericDSPLong], (instregex "^EXTR_RS_W$")>;
def : InstRW<[GenericDSPLong], (instregex "^EXTR_R_W$")>;
def : InstRW<[GenericDSPLong], (instregex "^EXTR_S_H$")>;
def : InstRW<[GenericDSPLong], (instregex "^EXTR_W$")>;
def : InstRW<[GenericDSPLong], (instregex "^INSV$")>;

def : InstRW<[GenericDSPMTHLIP], (instregex "^MTHLIP$")>;
def : InstRW<[GenericDSPMTHILO], (instregex "^MTHI_DSP$")>;
def : InstRW<[GenericDSPMTHILO], (instregex "^MTLO_DSP$")>;

def : InstRW<[GenericDSPShort], (instregex "^ABSQ_S_PH$")>;
def : InstRW<[GenericDSPShort], (instregex "^ABSQ_S_W$")>;
def : InstRW<[GenericDSPShort], (instregex "^ADDQ_PH$")>;
def : InstRW<[GenericDSPShort], (instregex "^ADDQ_S_PH$")>;
def : InstRW<[GenericDSPShort], (instregex "^ADDQ_S_W$")>;
def : InstRW<[GenericDSPShort], (instregex "^ADDSC$")>;
def : InstRW<[GenericDSPShort], (instregex "^ADDU_QB$")>;
def : InstRW<[GenericDSPShort], (instregex "^ADDU_S_QB$")>;
def : InstRW<[GenericDSPShort], (instregex "^ADDWC$")>;
def : InstRW<[GenericDSPShort], (instregex "^BITREV$")>;
def : InstRW<[GenericDSPShort], (instregex "^BPOSGE32$")>;
def : InstRW<[GenericDSPShort], (instregex "^CMPGU_EQ_QB$")>;
def : InstRW<[GenericDSPShort], (instregex "^CMPGU_LE_QB$")>;
def : InstRW<[GenericDSPShort], (instregex "^CMPGU_LT_QB$")>;
def : InstRW<[GenericDSPShort], (instregex "^CMPU_EQ_QB$")>;
def : InstRW<[GenericDSPShort], (instregex "^CMPU_LE_QB$")>;
def : InstRW<[GenericDSPShort], (instregex "^CMPU_LT_QB$")>;
def : InstRW<[GenericDSPShort], (instregex "^CMP_EQ_PH$")>;
def : InstRW<[GenericDSPShort], (instregex "^CMP_LE_PH$")>;
def : InstRW<[GenericDSPShort], (instregex "^CMP_LT_PH$")>;
def : InstRW<[GenericDSPShort], (instregex "^DPAQ_SA_L_W$")>;
def : InstRW<[GenericDSPShort], (instregex "^DPAQ_S_W_PH$")>;
def : InstRW<[GenericDSPShort], (instregex "^DPAU_H_QBL$")>;
def : InstRW<[GenericDSPShort], (instregex "^DPAU_H_QBR$")>;
def : InstRW<[GenericDSPShort], (instregex "^DPSQ_SA_L_W$")>;
def : InstRW<[GenericDSPShort], (instregex "^DPSQ_S_W_PH$")>;
def : InstRW<[GenericDSPShort], (instregex "^DPSU_H_QBL$")>;
def : InstRW<[GenericDSPShort], (instregex "^DPSU_H_QBR$")>;
def : InstRW<[GenericDSPShort], (instregex "^EXTPDPV$")>;
def : InstRW<[GenericDSPShort], (instregex "^EXTPDP$")>;
def : InstRW<[GenericDSPShort], (instregex "^EXTPV$")>;
def : InstRW<[GenericDSPShort], (instregex "^EXTP$")>;
def : InstRW<[GenericDSPShort], (instregex "^LBUX$")>;
def : InstRW<[GenericDSPShort], (instregex "^LHX$")>;
def : InstRW<[GenericDSPShort], (instregex "^LWX$")>;
def : InstRW<[GenericDSPShort], (instregex "^MADDU_DSP$")>;
def : InstRW<[GenericDSPShort], (instregex "^MADD_DSP$")>;
def : InstRW<[GenericDSPShort], (instregex "^MAQ_SA_W_PHL$")>;
def : InstRW<[GenericDSPShort], (instregex "^MAQ_SA_W_PHR$")>;
def : InstRW<[GenericDSPShort], (instregex "^MAQ_S_W_PHL$")>;
def : InstRW<[GenericDSPShort], (instregex "^MAQ_S_W_PHR$")>;
def : InstRW<[GenericDSPShort], (instregex "^MFHI_DSP$")>;
def : InstRW<[GenericDSPShort], (instregex "^MFLO_DSP$")>;
def : InstRW<[GenericDSPShort], (instregex "^MODSUB$")>;
def : InstRW<[GenericDSPShort], (instregex "^MSUBU_DSP$")>;
def : InstRW<[GenericDSPShort], (instregex "^MSUB_DSP$")>;
def : InstRW<[GenericDSPShort], (instregex "^MULEQ_S_W_PHL$")>;
def : InstRW<[GenericDSPShort], (instregex "^MULEQ_S_W_PHR$")>;
def : InstRW<[GenericDSPShort], (instregex "^MULEU_S_PH_QBL$")>;
def : InstRW<[GenericDSPShort], (instregex "^MULEU_S_PH_QBR$")>;
def : InstRW<[GenericDSPShort], (instregex "^MULQ_RS_PH$")>;
def : InstRW<[GenericDSPShort], (instregex "^MULSAQ_S_W_PH$")>;
def : InstRW<[GenericDSPShort], (instregex "^MULTU_DSP$")>;
def : InstRW<[GenericDSPShort], (instregex "^MULT_DSP$")>;
def : InstRW<[GenericDSPShort], (instregex "^PACKRL_PH$")>;
def : InstRW<[GenericDSPShort], (instregex "^PICK_PH$")>;
def : InstRW<[GenericDSPShort], (instregex "^PICK_QB$")>;
def : InstRW<[GenericDSPShort], (instregex "^PRECEQU_PH_QBLA$")>;
def : InstRW<[GenericDSPShort], (instregex "^PRECEQU_PH_QBL$")>;
def : InstRW<[GenericDSPShort], (instregex "^PRECEQU_PH_QBRA$")>;
def : InstRW<[GenericDSPShort], (instregex "^PRECEQU_PH_QBR$")>;
def : InstRW<[GenericDSPShort], (instregex "^PRECEQ_W_PHL$")>;
def : InstRW<[GenericDSPShort], (instregex "^PRECEQ_W_PHR$")>;
def : InstRW<[GenericDSPShort], (instregex "^PRECEU_PH_QBLA$")>;
def : InstRW<[GenericDSPShort], (instregex "^PRECEU_PH_QBL$")>;
def : InstRW<[GenericDSPShort], (instregex "^PRECEU_PH_QBRA$")>;
def : InstRW<[GenericDSPShort], (instregex "^PRECEU_PH_QBR$")>;
def : InstRW<[GenericDSPShort], (instregex "^PRECRQU_S_QB_PH$")>;
def : InstRW<[GenericDSPShort], (instregex "^PRECRQ_PH_W$")>;
def : InstRW<[GenericDSPShort], (instregex "^PRECRQ_QB_PH$")>;
def : InstRW<[GenericDSPShort], (instregex "^PRECRQ_RS_PH_W$")>;
def : InstRW<[GenericDSPShort], (instregex "^RADDU_W_QB$")>;
def : InstRW<[GenericDSPShort], (instregex "^RDDSP$")>;
def : InstRW<[GenericDSPShort], (instregex "^REPLV_PH$")>;
def : InstRW<[GenericDSPShort], (instregex "^REPLV_QB$")>;
def : InstRW<[GenericDSPShort], (instregex "^REPL_PH$")>;
def : InstRW<[GenericDSPShort], (instregex "^REPL_QB$")>;
def : InstRW<[GenericDSPShort], (instregex "^SHILOV$")>;
def : InstRW<[GenericDSPShort], (instregex "^SHILO$")>;
def : InstRW<[GenericDSPShort], (instregex "^SHLLV_PH$")>;
def : InstRW<[GenericDSPShort], (instregex "^SHLLV_QB$")>;
def : InstRW<[GenericDSPShort], (instregex "^SHLLV_S_PH$")>;
def : InstRW<[GenericDSPShort], (instregex "^SHLLV_S_W$")>;
def : InstRW<[GenericDSPShort], (instregex "^SHLL_PH$")>;
def : InstRW<[GenericDSPShort], (instregex "^SHLL_QB$")>;
def : InstRW<[GenericDSPShort], (instregex "^SHLL_S_PH$")>;
def : InstRW<[GenericDSPShort], (instregex "^SHLL_S_W$")>;
def : InstRW<[GenericDSPShort], (instregex "^SHRAV_PH$")>;
def : InstRW<[GenericDSPShort], (instregex "^SHRAV_R_PH$")>;
def : InstRW<[GenericDSPShort], (instregex "^SHRAV_R_W$")>;
def : InstRW<[GenericDSPShort], (instregex "^SHRA_PH$")>;
def : InstRW<[GenericDSPShort], (instregex "^SHRA_R_PH$")>;
def : InstRW<[GenericDSPShort], (instregex "^SHRA_R_W$")>;
def : InstRW<[GenericDSPShort], (instregex "^SHRLV_QB$")>;
def : InstRW<[GenericDSPShort], (instregex "^SHRL_QB$")>;
def : InstRW<[GenericDSPShort], (instregex "^SUBQ_PH$")>;
def : InstRW<[GenericDSPShort], (instregex "^SUBQ_S_PH$")>;
def : InstRW<[GenericDSPShort], (instregex "^SUBQ_S_W$")>;
def : InstRW<[GenericDSPShort], (instregex "^SUBU_QB$")>;
def : InstRW<[GenericDSPShort], (instregex "^SUBU_S_QB$")>;
def : InstRW<[GenericDSPShort], (instregex "^WRDSP$")>;

// MIPS DSP R2 - hasDSP, HasDSPR2, InMicroMips
// ===========================================

def : InstRW<[GenericDSPShort], (instregex "^ABSQ_S_QB$")>;
def : InstRW<[GenericDSPShort], (instregex "^ADDQH_PH$")>;
def : InstRW<[GenericDSPShort], (instregex "^ADDQH_R_PH$")>;
def : InstRW<[GenericDSPShort], (instregex "^ADDQH_R_W$")>;
def : InstRW<[GenericDSPShort], (instregex "^ADDQH_W$")>;
def : InstRW<[GenericDSPShort], (instregex "^ADDUH_QB$")>;
def : InstRW<[GenericDSPShort], (instregex "^ADDUH_R_QB$")>;
def : InstRW<[GenericDSPShort], (instregex "^ADDU_PH$")>;
def : InstRW<[GenericDSPShort], (instregex "^ADDU_S_PH$")>;
def : InstRW<[GenericDSPShort], (instregex "^APPEND$")>;
def : InstRW<[GenericDSPShort], (instregex "^BALIGN$")>;
def : InstRW<[GenericDSPShort], (instregex "^CMPGDU_EQ_QB$")>;
def : InstRW<[GenericDSPShort], (instregex "^CMPGDU_LE_QB$")>;
def : InstRW<[GenericDSPShort], (instregex "^CMPGDU_LT_QB$")>;
def : InstRW<[GenericDSPShort], (instregex "^DPA_W_PH$")>;
def : InstRW<[GenericDSPShort], (instregex "^DPAQX_SA_W_PH$")>;
def : InstRW<[GenericDSPShort], (instregex "^DPAQX_S_W_PH$")>;
def : InstRW<[GenericDSPShort], (instregex "^DPAX_W_PH$")>;
def : InstRW<[GenericDSPShort], (instregex "^DPS_W_PH$")>;
def : InstRW<[GenericDSPShort], (instregex "^DPSQX_S_W_PH$")>;
def : InstRW<[GenericDSPShort], (instregex "^DPSQX_SA_W_PH$")>;
def : InstRW<[GenericDSPShort], (instregex "^DPSX_W_PH$")>;
def : InstRW<[GenericDSPShort], (instregex "^MUL_PH$")>;
def : InstRW<[GenericDSPShort], (instregex "^MUL_S_PH$")>;
def : InstRW<[GenericDSPShort], (instregex "^MULQ_RS_W$")>;
def : InstRW<[GenericDSPShort], (instregex "^MULQ_S_PH$")>;
def : InstRW<[GenericDSPShort], (instregex "^MULQ_S_W$")>;
def : InstRW<[GenericDSPShort], (instregex "^MULSA_W_PH$")>;
def : InstRW<[GenericDSPShort], (instregex "^PRECR_QB_PH$")>;
def : InstRW<[GenericDSPShort], (instregex "^PRECR_SRA_PH_W$")>;
def : InstRW<[GenericDSPShort], (instregex "^PRECR_SRA_R_PH_W$")>;
def : InstRW<[GenericDSPShort], (instregex "^PREPEND$")>;
def : InstRW<[GenericDSPShort], (instregex "^SHRA_QB$")>;
def : InstRW<[GenericDSPShort], (instregex "^SHRA_R_QB$")>;
def : InstRW<[GenericDSPShort], (instregex "^SHRAV_QB$")>;
def : InstRW<[GenericDSPShort], (instregex "^SHRAV_R_QB$")>;
def : InstRW<[GenericDSPShort], (instregex "^SHRL_PH$")>;
def : InstRW<[GenericDSPShort], (instregex "^SHRLV_PH$")>;
def : InstRW<[GenericDSPShort], (instregex "^SUBQH_PH$")>;
def : InstRW<[GenericDSPShort], (instregex "^SUBQH_R_PH$")>;
def : InstRW<[GenericDSPShort], (instregex "^SUBQH_W$")>;
def : InstRW<[GenericDSPShort], (instregex "^SUBQH_R_W$")>;
def : InstRW<[GenericDSPShort], (instregex "^SUBU_PH$")>;
def : InstRW<[GenericDSPShort], (instregex "^SUBU_S_PH$")>;
def : InstRW<[GenericDSPShort], (instregex "^SUBUH_QB$")>;
def : InstRW<[GenericDSPShort], (instregex "^SUBUH_R_QB$")>;

// microMIPS DSP R1 - HasDSP, InMicroMips
// ======================================

def : InstRW<[GenericDSPShort], (instregex "^ABSQ_S_PH_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^ABSQ_S_W_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^ADDQ_PH_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^ADDQ_S_PH_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^ADDQ_S_W_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^ADDSC_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^ADDU_QB_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^ADDU_S_QB_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^ADDWC_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^BITREV_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^BPOSGE32_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^CMPGU_EQ_QB_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^CMPGU_LE_QB_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^CMPGU_LT_QB_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^CMPU_EQ_QB_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^CMPU_LE_QB_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^CMPU_LT_QB_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^CMP_EQ_PH_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^CMP_LE_PH_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^CMP_LT_PH_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^DPAQ_SA_L_W_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^DPAQ_S_W_PH_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^DPAU_H_QBL_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^DPAU_H_QBR_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^DPSQ_SA_L_W_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^DPSQ_S_W_PH_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^DPSU_H_QBL_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^DPSU_H_QBR_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^EXTPDPV_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^EXTPDP_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^EXTPV_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^EXTP_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^EXTRV_RS_W_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^EXTRV_R_W_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^EXTRV_S_H_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^EXTRV_W_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^EXTR_RS_W_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^EXTR_R_W_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^EXTR_S_H_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^EXTR_W_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^INSV_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^LBUX_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^LHX_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^LWX_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^MADDU_DSP_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^MADD_DSP_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^MAQ_SA_W_PHL_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^MAQ_SA_W_PHR_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^MAQ_S_W_PHL_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^MAQ_S_W_PHR_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^MFHI_DSP_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^MFLO_DSP_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^MODSUB_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^MOVEP_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^MOVEP_MMR6$")>;
def : InstRW<[GenericDSPShort], (instregex "^MOVN_I_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^MOVZ_I_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^MSUBU_DSP_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^MSUB_DSP_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^MTHI_DSP_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^MTHLIP_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^MTLO_DSP_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^MULEQ_S_W_PHL_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^MULEQ_S_W_PHR_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^MULEU_S_PH_QBL_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^MULEU_S_PH_QBR_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^MULQ_RS_PH_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^MULSAQ_S_W_PH_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^MULTU_DSP_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^MULT_DSP_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^PACKRL_PH_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^PICK_PH_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^PICK_QB_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^PRECEQU_PH_QBLA_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^PRECEQU_PH_QBL_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^PRECEQU_PH_QBRA_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^PRECEQU_PH_QBR_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^PRECEQ_W_PHL_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^PRECEQ_W_PHR_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^PRECEU_PH_QBLA_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^PRECEU_PH_QBL_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^PRECEU_PH_QBRA_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^PRECEU_PH_QBR_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^PRECRQU_S_QB_PH_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^PRECRQ_PH_W_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^PRECRQ_QB_PH_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^PRECRQ_RS_PH_W_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^RADDU_W_QB_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^RDDSP_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^REPLV_PH_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^REPLV_QB_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^REPL_PH_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^REPL_QB_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^SHILOV_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^SHILO_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^SHLLV_PH_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^SHLLV_QB_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^SHLLV_S_PH_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^SHLLV_S_W_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^SHLL_PH_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^SHLL_QB_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^SHLL_S_PH_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^SHLL_S_W_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^SHRAV_PH_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^SHRAV_R_PH_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^SHRAV_R_W_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^SHRA_PH_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^SHRA_R_PH_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^SHRA_R_W_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^SHRLV_QB_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^SHRL_QB_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^SUBQ_PH_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^SUBQ_S_PH_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^SUBQ_S_W_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^SUBU_QB_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^SUBU_S_QB_MM$")>;
def : InstRW<[GenericDSPShort], (instregex "^WRDSP_MM$")>;


// microMIPS DSP R2 - hasDSP, HasDSPR2, InMicroMips
// ================================================

def : InstRW<[GenericDSPShort], (instregex "^ABSQ_S_QB_MMR2$")>;
def : InstRW<[GenericDSPShort], (instregex "^ADDQH_PH_MMR2$")>;
def : InstRW<[GenericDSPShort], (instregex "^ADDQH_R_PH_MMR2$")>;
def : InstRW<[GenericDSPShort], (instregex "^ADDQH_R_W_MMR2$")>;
def : InstRW<[GenericDSPShort], (instregex "^ADDQH_W_MMR2$")>;
def : InstRW<[GenericDSPShort], (instregex "^ADDUH_QB_MMR2$")>;
def : InstRW<[GenericDSPShort], (instregex "^ADDUH_R_QB_MMR2$")>;
def : InstRW<[GenericDSPShort], (instregex "^ADDU_PH_MMR2$")>;
def : InstRW<[GenericDSPShort], (instregex "^ADDU_S_PH_MMR2$")>;
def : InstRW<[GenericDSPShort], (instregex "^APPEND_MMR2$")>;
def : InstRW<[GenericDSPShort], (instregex "^BALIGN_MMR2$")>;
def : InstRW<[GenericDSPShort], (instregex "^CMPGDU_EQ_QB_MMR2$")>;
def : InstRW<[GenericDSPShort], (instregex "^CMPGDU_LE_QB_MMR2$")>;
def : InstRW<[GenericDSPShort], (instregex "^CMPGDU_LT_QB_MMR2$")>;
def : InstRW<[GenericDSPShort], (instregex "^DPA_W_PH_MMR2$")>;
def : InstRW<[GenericDSPShort], (instregex "^DPAQX_SA_W_PH_MMR2$")>;
def : InstRW<[GenericDSPShort], (instregex "^DPAQX_S_W_PH_MMR2$")>;
def : InstRW<[GenericDSPShort], (instregex "^DPAX_W_PH_MMR2$")>;
def : InstRW<[GenericDSPShort], (instregex "^DPS_W_PH_MMR2$")>;
def : InstRW<[GenericDSPShort], (instregex "^DPSQX_S_W_PH_MMR2$")>;
def : InstRW<[GenericDSPShort], (instregex "^DPSQX_SA_W_PH_MMR2$")>;
def : InstRW<[GenericDSPShort], (instregex "^DPSX_W_PH_MMR2$")>;
def : InstRW<[GenericDSPShort], (instregex "^MUL_PH_MMR2$")>;
def : InstRW<[GenericDSPShort], (instregex "^MUL_S_PH_MMR2$")>;
def : InstRW<[GenericDSPShort], (instregex "^MULQ_RS_W_MMR2$")>;
def : InstRW<[GenericDSPShort], (instregex "^MULQ_S_PH_MMR2$")>;
def : InstRW<[GenericDSPShort], (instregex "^MULQ_S_W_MMR2$")>;
def : InstRW<[GenericDSPShort], (instregex "^MULSA_W_PH_MMR2$")>;
def : InstRW<[GenericDSPShort], (instregex "^PRECR_QB_PH_MMR2$")>;
def : InstRW<[GenericDSPShort], (instregex "^PRECR_SRA_PH_W_MMR2$")>;
def : InstRW<[GenericDSPShort], (instregex "^PRECR_SRA_R_PH_W_MMR2$")>;
def : InstRW<[GenericDSPShort], (instregex "^PREPEND_MMR2$")>;
def : InstRW<[GenericDSPShort], (instregex "^SHRA_QB_MMR2$")>;
def : InstRW<[GenericDSPShort], (instregex "^SHRA_R_QB_MMR2$")>;
def : InstRW<[GenericDSPShort], (instregex "^SHRAV_QB_MMR2$")>;
def : InstRW<[GenericDSPShort], (instregex "^SHRAV_R_QB_MMR2$")>;
def : InstRW<[GenericDSPShort], (instregex "^SHRL_PH_MMR2$")>;
def : InstRW<[GenericDSPShort], (instregex "^SHRLV_PH_MMR2$")>;
def : InstRW<[GenericDSPShort], (instregex "^SUBQH_PH_MMR2$")>;
def : InstRW<[GenericDSPShort], (instregex "^SUBQH_R_PH_MMR2$")>;
def : InstRW<[GenericDSPShort], (instregex "^SUBQH_W_MMR2$")>;
def : InstRW<[GenericDSPShort], (instregex "^SUBQH_R_W_MMR2$")>;
def : InstRW<[GenericDSPShort], (instregex "^SUBU_PH_MMR2$")>;
def : InstRW<[GenericDSPShort], (instregex "^SUBU_S_PH_MMR2$")>;
def : InstRW<[GenericDSPShort], (instregex "^SUBUH_QB_MMR2$")>;
def : InstRW<[GenericDSPShort], (instregex "^SUBUH_R_QB_MMR2$")>;

// microMIPS DSP R3 - hasDSP, hasDSPR2, hasDSPR3, InMicroMips
// ==========================================================

def : InstRW<[GenericDSPShort], (instregex "^BPOSGE32C_MMR3$")>;

// MIPS MSA ASE - hasMSA
// =====================

def GenericWriteMSAShortLogic : SchedWriteRes<[GenericIssueFPUS]>;
def GenericWriteMSAShortInt : SchedWriteRes<[GenericIssueFPUS]> {
let Latency = 2;
}
def GenericWriteMoveOtherUnitsToFPU : SchedWriteRes<[GenericIssueFPUS]>;
def GenericWriteMSAOther3 : SchedWriteRes<[GenericIssueFPUS]> {
let Latency = 3;
}
def GenericWriteMSALongInt : SchedWriteRes<[GenericIssueFPUS]> {
let Latency = 5;
}
def GenericWriteFPUDivI : SchedWriteRes<[GenericFPQ]> {
  let Latency = 33;
  let ResourceCycles = [ 33 ];
}

// FPUS is also used in moves from floating point and MSA registers to general
// purpose registers.
def GenericWriteMoveFPUSToOtherUnits : SchedWriteRes<[GenericIssueFPUS]> {
  let Latency = 0;
}

// FPUL is also used in moves from floating point and MSA registers to general
// purpose registers.
def GenericWriteMoveFPULToOtherUnits : SchedWriteRes<[GenericIssueFPUL]>;


// adds_a.[bhwd], adds_[asu].[bhwd], addvi?.[bhwd], asub_[us].[bhwd],
// aver?_[us].[bhwd]
def : InstRW<[GenericWriteMSAShortInt], (instregex "^ADD_A_[BHWD]$")>;
def : InstRW<[GenericWriteMSAShortInt], (instregex "^ADDS_[ASU]_[BHWD]$")>;

// TODO: ADDVI_[BHW] might be 1 cycle latency rather than 2. Need to confirm it.
// add.[bhwd], addvi.[bhwd], asub_[us].[bhwd], ave.[bhwd], aver.[bhwd]
def : InstRW<[GenericWriteMSAShortInt], (instregex "^ADDVI?_[BHWD]$")>;
def : InstRW<[GenericWriteMSAShortInt], (instregex "^ASUB_[US].[BHWD]$")>;
def : InstRW<[GenericWriteMSAShortInt], (instregex "^AVER?_[US].[BHWD]$")>;

// and.v, andi.b, move.v, ldi.[bhwd], xor.v, nor.v, xori.b, nori.b
def : InstRW<[GenericWriteMSAShortLogic], (instregex "^MOVE_V$")>;
def : InstRW<[GenericWriteMSAShortLogic], (instregex "^LDI_[BHWD]$")>;
def : InstRW<[GenericWriteMSAShortLogic], (instregex "^(AND|OR|[XN]OR)_V$")>;
def : InstRW<[GenericWriteMSAShortLogic], (instregex "^(AND|OR|[XN]OR)I_B$")>;
def : InstRW<[GenericWriteMSAShortLogic], (instregex "^(AND|OR|[XN]OR)I_B$")>;

// vshf.[bhwd], binsl.[bhwd], binsr.[bhwd], insert.[bhwd], sld?.[bhwd],
// bset.[bhwd], bclr.[bhwd], bneg.[bhwd], bsel_v, bseli_b
def : InstRW<[GenericWriteMSAShortInt], (instregex "^VSHF_[BHWD]$")>;
def : InstRW<[GenericWriteMSAShortInt], (instregex "^(BINSL|BINSLI)_[BHWD]$")>;
def : InstRW<[GenericWriteMSAShortInt], (instregex "^(BINSR|BINSRI)_[BHWD]$")>;
def : InstRW<[GenericWriteMSAShortInt], (instregex "^INSERT_[BHWD]$")>;
def : InstRW<[GenericWriteMSAShortInt], (instregex "^(SLD|SLDI)_[BHWD]$")>;
def : InstRW<[GenericWriteMSAShortInt], (instregex "^(BSET|BSETI)_[BHWD]$")>;
def : InstRW<[GenericWriteMSAShortInt], (instregex "^(BCLR|BCLRI)_[BHWD]$")>;
def : InstRW<[GenericWriteMSAShortInt], (instregex "^(BNEG|BNEGI)_[BHWD]$")>;
def : InstRW<[GenericWriteMSAShortInt], (instregex "^(BSEL_V|BSELI_B)$")>;
def : InstRW<[GenericWriteMSAShortInt], (instregex "^BMN*Z.*$")>;

// pcnt.[bhwd], sat_s.[bhwd], sat_u.bhwd]
def : InstRW<[GenericWriteMSAOther3], (instregex "^PCNT_[BHWD]$")>;
def : InstRW<[GenericWriteMSAOther3], (instregex "^SAT_(S|U)_[BHWD]$")>;

// bnz.[bhwdv], cfcmsa, ctcmsa
def : InstRW<[GenericWriteMSAShortLogic], (instregex "^(BNZ|BZ)_[BHWDV]$")>;
def : InstRW<[GenericWriteMSAShortLogic], (instregex "^C(F|T)CMSA$")>;

// shf.[bhw], fill[bhwd], splat?.[bhwd]
def : InstRW<[GenericWriteMSAShortInt], (instregex "^SHF_[BHW]$")>;
def : InstRW<[GenericWriteMSAShortInt], (instregex "^FILL_[BHWD]$")>;
def : InstRW<[GenericWriteMSAShortInt], (instregex "^(SPLAT|SPLATI)_[BHWD]$")>;

// pcnt.[bhwd], sat_s.[bhwd], sat_u.bhwd]
def : InstRW<[GenericWriteMSAOther3], (instregex "^PCNT_[BHWD]$")>;
def : InstRW<[GenericWriteMSAOther3], (instregex "^SAT_(S|U)_[BHWD]$")>;

// fexp2_w, fexp2_d
def : InstRW<[GenericWriteFPUS], (instregex "^FEXP2_(W|D)$")>;

// compare, converts, round to int, floating point truncate.
def : InstRW<[GenericWriteFPUS], (instregex "^(CLT|CLTI)_(S|U)_[BHWD]$")>;
def : InstRW<[GenericWriteFPUS], (instregex "^(CLE|CLEI)_(S|U)_[BHWD]$")>;
def : InstRW<[GenericWriteFPUS], (instregex "^(CEQ|CEQI)_[BHWD]$")>;
def : InstRW<[GenericWriteFPUS], (instregex "^CMP_UN_(S|D)$")>;
def : InstRW<[GenericWriteFPUS], (instregex "^CMP_UEQ_(S|D)$")>;
def : InstRW<[GenericWriteFPUS], (instregex "^CMP_EQ_(S|D)$")>;
def : InstRW<[GenericWriteFPUS], (instregex "^CMP_LT_(S|D)$")>;
def : InstRW<[GenericWriteFPUS], (instregex "^CMP_ULT_(S|D)$")>;
def : InstRW<[GenericWriteFPUS], (instregex "^CMP_LE_(S|D)$")>;
def : InstRW<[GenericWriteFPUS], (instregex "^CMP_ULE_(S|D)$")>;
def : InstRW<[GenericWriteFPUS], (instregex "^FS(AF|EQ|LT|LE|NE|OR)_(W|D)$")>;
def : InstRW<[GenericWriteFPUS], (instregex "^FSUEQ_(W|D)$")>;
def : InstRW<[GenericWriteFPUS], (instregex "^FSULE_(W|D)$")>;
def : InstRW<[GenericWriteFPUS], (instregex "^FSULT_(W|D)$")>;
def : InstRW<[GenericWriteFPUS], (instregex "^FSUNE_(W|D)$")>;
def : InstRW<[GenericWriteFPUS], (instregex "^FSUN_(W|D)$")>;
def : InstRW<[GenericWriteFPUS], (instregex "^FCAF_(W|D)$")>;
def : InstRW<[GenericWriteFPUS], (instregex "^FCEQ_(W|D)$")>;
def : InstRW<[GenericWriteFPUS], (instregex "^FCLE_(W|D)$")>;
def : InstRW<[GenericWriteFPUS], (instregex "^FCLT_(W|D)$")>;
def : InstRW<[GenericWriteFPUS], (instregex "^FCNE_(W|D)$")>;
def : InstRW<[GenericWriteFPUS], (instregex "^FCOR_(W|D)$")>;
def : InstRW<[GenericWriteFPUS], (instregex "^FCUEQ_(W|D)$")>;
def : InstRW<[GenericWriteFPUS], (instregex "^FCULE_(W|D)$")>;
def : InstRW<[GenericWriteFPUS], (instregex "^FCULT_(W|D)$")>;
def : InstRW<[GenericWriteFPUS], (instregex "^FCUNE_(W|D)$")>;
def : InstRW<[GenericWriteFPUS], (instregex "^FCUN_(W|D)$")>;
def : InstRW<[GenericWriteFPUS], (instregex "^FABS_(W|D)$")>;
def : InstRW<[GenericWriteFPUS], (instregex "^FFINT_(U|S)_(W|D)$")>;
def : InstRW<[GenericWriteFPUS], (instregex "^FFQL_(W|D)$")>;
def : InstRW<[GenericWriteFPUS], (instregex "^FFQR_(W|D)$")>;
def : InstRW<[GenericWriteFPUS], (instregex "^FTINT_(U|S)_(W|D)$")>;
def : InstRW<[GenericWriteFPUS], (instregex "^FRINT_(W|D)$")>;
def : InstRW<[GenericWriteFPUS], (instregex "^FTQ_(H|W)$")>;
def : InstRW<[GenericWriteFPUS], (instregex "^FTRUNC_(U|S)_(W|D)$")>;

// fexdo.[hw], fexupl.[wd], fexupr.[wd]
def : InstRW<[GenericWriteFPUS], (instregex "^FEXDO_(H|W)$")>;
def : InstRW<[GenericWriteFPUS], (instregex "^FEXUPL_(W|D)$")>;
def : InstRW<[GenericWriteFPUS], (instregex "^FEXUPR_(W|D)$")>;

// fclass.[wd], fmax.[wd], fmax_a.[wd], fmin.[wd], fmin_a.[wd], flog2.[wd]
def : InstRW<[GenericWriteFPUS], (instregex "^FCLASS_(W|D)$")>;
def : InstRW<[GenericWriteFPUS], (instregex "^FMAX_A_(W|D)$")>;
def : InstRW<[GenericWriteFPUS], (instregex "^FMAX_(W|D)$")>;
def : InstRW<[GenericWriteFPUS], (instregex "^FMIN_A_(W|D)$")>;
def : InstRW<[GenericWriteFPUS], (instregex "^FMIN_(W|D)$")>;
def : InstRW<[GenericWriteFPUS], (instregex "^FLOG2_(W|D)$")>;

// interleave right/left, interleave even/odd, insert
def : InstRW<[GenericWriteMSAShortLogic], (instregex "^(ILVR|ILVL)_[BHWD]$")>;
def : InstRW<[GenericWriteMSAShortLogic], (instregex "^(ILVEV|ILVOD)_[BHWD]$")>;
def : InstRW<[GenericWriteMSAShortLogic], (instregex "^INSVE_[BHWD]$")>;

// subs_?.[bhwd], subsus_?.[bhwd], subsuu_?.[bhwd], subvi.[bhwd], subv.[bhwd],
def : InstRW<[GenericWriteMSAShortInt], (instregex "^SUBS_(S|U)_[BHWD]$")>;
def : InstRW<[GenericWriteMSAShortInt], (instregex "^SUBSUS_(S|U)_[BHWD]$")>;
def : InstRW<[GenericWriteMSAShortInt], (instregex "^SUBSUU_(S|U)_[BHWD]$")>;
def : InstRW<[GenericWriteMSAShortInt], (instregex "^SUBVI_[BHWD]$")>;
def : InstRW<[GenericWriteMSAShortInt], (instregex "^SUBV_[BHWD]$")>;

// mod_[su].[bhwd], div_[su].[bhwd]
def : InstRW<[GenericWriteFPUDivI], (instregex "^MOD_(S|U)_[BHWD]$")>;
def : InstRW<[GenericWriteFPUDivI], (instregex "^DIV_(S|U)_[BHWD]$")>;

// hadd_[su].[bhwd], hsub_[su].[bhwd], max_[sua].[bhwd], min_[sua].[bhwd],
// maxi_[su].[bhwd], mini_[su].[bhwd], sra?.[bhwd], srar?.[bhwd], srlr.[bhwd],
// sll?.[bhwd], pckev.[bhwd], pckod.[bhwd], nloc.[bhwd], nlzc.[bhwd],
// insve.[bhwd]
def : InstRW<[GenericWriteMSAShortLogic], (instregex "^HADD_(S|U)_[BHWD]$")>;
def : InstRW<[GenericWriteMSAShortLogic], (instregex "^HSUB_(S|U)_[BHWD]$")>;
def : InstRW<[GenericWriteMSAShortLogic], (instregex "^(MAX|MIN)_S_[BHWD]$")>;
def : InstRW<[GenericWriteMSAShortLogic], (instregex "^(MAX|MIN)_U_[BHWD]$")>;
def : InstRW<[GenericWriteMSAShortLogic], (instregex "^(MAX|MIN)_A_[BHWD]$")>;
def : InstRW<[GenericWriteMSAShortLogic],
             (instregex "^(MAXI|MINI)_(S|U)_[BHWD]$")>;
def : InstRW<[GenericWriteMSAShortLogic], (instregex "^(SRA|SRAI)_[BHWD]$")>;
def : InstRW<[GenericWriteMSAShortLogic], (instregex "^(SRL|SRLI)_[BHWD]$")>;
def : InstRW<[GenericWriteMSAShortLogic], (instregex "^(SRAR|SRARI)_[BHWD]$")>;
def : InstRW<[GenericWriteMSAShortLogic], (instregex "^(SRLR|SRLRI)_[BHWD]$")>;
def : InstRW<[GenericWriteMSAShortLogic], (instregex "^(SLL|SLLI)_[BHWD]$")>;
def : InstRW<[GenericWriteMSAShortLogic], (instregex "^(PCKEV|PCKOD)_[BHWD]$")>;
def : InstRW<[GenericWriteMSAShortLogic], (instregex "^(NLOC|NLZC)_[BHWD]$")>;
def : InstRW<[GenericWriteMSAShortLogic], (instregex "^INSVE_[BHWD]$")>;

// dpadd_?.[bhwd], dpsub_?.[bhwd], dotp_?.[bhwd], msubv.[bhwd], maddv.[bhwd]
// mulv.[bhwd].
def : InstRW<[GenericWriteMSALongInt], (instregex "^DPADD_(S|U)_[HWD]$")>;
def : InstRW<[GenericWriteMSALongInt], (instregex "^DPSUB_(S|U)_[HWD]$")>;
def : InstRW<[GenericWriteMSALongInt], (instregex "^DOTP_(S|U)_[HWD]$")>;
def : InstRW<[GenericWriteMSALongInt], (instregex "^MSUBV_[BHWD]$")>;
def : InstRW<[GenericWriteMSALongInt], (instregex "^MADDV_[BHWD]$")>;
def : InstRW<[GenericWriteMSALongInt], (instregex "^MULV_[BHWD]$")>;

// madd?.q.[hw], msub?.q.[hw], mul?.q.[hw]
def : InstRW<[GenericWriteMSALongInt], (instregex "^MADDR_Q_[HW]$")>;
def : InstRW<[GenericWriteMSALongInt], (instregex "^MADD_Q_[HW]$")>;
def : InstRW<[GenericWriteMSALongInt], (instregex "^MSUBR_Q_[HW]$")>;
def : InstRW<[GenericWriteMSALongInt], (instregex "^MSUB_Q_[HW]$")>;
def : InstRW<[GenericWriteMSALongInt], (instregex "^MULR_Q_[HW]$")>;
def : InstRW<[GenericWriteMSALongInt], (instregex "^MUL_Q_[HW]$")>;

// fadd.[dw], fmadd.[dw], fmul.[dw], frcp.[dw], frsqrt.[dw], fsqrt.[dw]
// fsub.[dw], fdiv.[dw]
def : InstRW<[GenericWriteFPUL], (instregex "^FADD_[DW]$")>;
def : InstRW<[GenericWriteFPUL], (instregex "^FMADD_[DW]$")>;
def : InstRW<[GenericWriteFPUL], (instregex "^FMSUB_[DW]$")>;
def : InstRW<[GenericWriteFPUL], (instregex "^FMUL_[DW]$")>;
def : InstRW<[GenericWriteFPUL], (instregex "^FRCP_[DW]$")>;
def : InstRW<[GenericWriteFPUL], (instregex "^FRSQRT_[DW]$")>;
def : InstRW<[GenericWriteFPUL], (instregex "^FSQRT_[DW]$")>;
def : InstRW<[GenericWriteFPUL], (instregex "^FSUB_[DW]$")>;
def : InstRW<[GenericWriteFPUL], (instregex "^FDIV_[DW]$")>;

// copy.[su]_[bhwd]
def : InstRW<[GenericWriteFPUMoveGPRFPU], (instregex "^COPY_U_[BHW]$")>;
def : InstRW<[GenericWriteFPUMoveGPRFPU], (instregex "^COPY_S_[BHWD]$")>;

def : InstRW<[GenericWriteFPUStore], (instregex "^ST_[BHWD]$")>;
def : InstRW<[GenericWriteFPULoad], (instregex "^LD_[BHWD]$")>;
}
