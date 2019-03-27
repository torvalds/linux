//===-- MipsInstrFPU.td - Mips FPU Instruction Information -*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file describes the Mips FPU instruction set.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Floating Point Instructions
// ------------------------
// * 64bit fp:
//    - 32 64-bit registers (default mode)
//    - 16 even 32-bit registers (32-bit compatible mode) for
//      single and double access.
// * 32bit fp:
//    - 16 even 32-bit registers - single and double (aliased)
//    - 32 32-bit registers (within single-only mode)
//===----------------------------------------------------------------------===//

// Floating Point Compare and Branch
def SDT_MipsFPBrcond : SDTypeProfile<0, 3, [SDTCisInt<0>,
                                            SDTCisVT<1, i32>,
                                            SDTCisVT<2, OtherVT>]>;
def SDT_MipsFPCmp : SDTypeProfile<0, 3, [SDTCisSameAs<0, 1>, SDTCisFP<1>,
                                         SDTCisVT<2, i32>]>;
def SDT_MipsCMovFP : SDTypeProfile<1, 3, [SDTCisSameAs<0, 1>, SDTCisVT<2, i32>,
                                          SDTCisSameAs<1, 3>]>;
def SDT_MipsTruncIntFP : SDTypeProfile<1, 1, [SDTCisFP<0>, SDTCisFP<1>]>;
def SDT_MipsBuildPairF64 : SDTypeProfile<1, 2, [SDTCisVT<0, f64>,
                                                SDTCisVT<1, i32>,
                                                SDTCisSameAs<1, 2>]>;
def SDT_MipsExtractElementF64 : SDTypeProfile<1, 2, [SDTCisVT<0, i32>,
                                                     SDTCisVT<1, f64>,
                                                     SDTCisVT<2, i32>]>;

def SDT_MipsMTC1_D64 : SDTypeProfile<1, 1, [SDTCisVT<0, f64>,
                                            SDTCisVT<1, i32>]>;

def MipsFPCmp : SDNode<"MipsISD::FPCmp", SDT_MipsFPCmp, [SDNPOutGlue]>;
def MipsCMovFP_T : SDNode<"MipsISD::CMovFP_T", SDT_MipsCMovFP, [SDNPInGlue]>;
def MipsCMovFP_F : SDNode<"MipsISD::CMovFP_F", SDT_MipsCMovFP, [SDNPInGlue]>;
def MipsFPBrcond : SDNode<"MipsISD::FPBrcond", SDT_MipsFPBrcond,
                          [SDNPHasChain, SDNPOptInGlue]>;
def MipsTruncIntFP : SDNode<"MipsISD::TruncIntFP", SDT_MipsTruncIntFP>;
def MipsBuildPairF64 : SDNode<"MipsISD::BuildPairF64", SDT_MipsBuildPairF64>;
def MipsExtractElementF64 : SDNode<"MipsISD::ExtractElementF64",
                                   SDT_MipsExtractElementF64>;

def MipsMTC1_D64 : SDNode<"MipsISD::MTC1_D64", SDT_MipsMTC1_D64>;

// Operand for printing out a condition code.
let PrintMethod = "printFCCOperand", DecoderMethod = "DecodeCondCode" in
  def condcode : Operand<i32>;

//===----------------------------------------------------------------------===//
// Feature predicates.
//===----------------------------------------------------------------------===//

def IsFP64bit        : Predicate<"Subtarget->isFP64bit()">,
                       AssemblerPredicate<"FeatureFP64Bit">;
def NotFP64bit       : Predicate<"!Subtarget->isFP64bit()">,
                       AssemblerPredicate<"!FeatureFP64Bit">;
def IsSingleFloat    : Predicate<"Subtarget->isSingleFloat()">,
                       AssemblerPredicate<"FeatureSingleFloat">;
def IsNotSingleFloat : Predicate<"!Subtarget->isSingleFloat()">,
                       AssemblerPredicate<"!FeatureSingleFloat">;
def IsNotSoftFloat   : Predicate<"!Subtarget->useSoftFloat()">,
                       AssemblerPredicate<"!FeatureSoftFloat">;

//===----------------------------------------------------------------------===//
// Mips FGR size adjectives.
// They are mutually exclusive.
//===----------------------------------------------------------------------===//

class FGR_32 { list<Predicate> FGRPredicates = [NotFP64bit]; }
class FGR_64 { list<Predicate> FGRPredicates = [IsFP64bit]; }
class HARDFLOAT { list<Predicate> HardFloatPredicate = [IsNotSoftFloat]; }

//===----------------------------------------------------------------------===//

// FP immediate patterns.
def fpimm0 : PatLeaf<(fpimm), [{
  return N->isExactlyValue(+0.0);
}]>;

def fpimm0neg : PatLeaf<(fpimm), [{
  return N->isExactlyValue(-0.0);
}]>;

//===----------------------------------------------------------------------===//
// Instruction Class Templates
//
// A set of multiclasses is used to address the register usage.
//
// S32 - single precision in 16 32bit even fp registers
//       single precision in 32 32bit fp registers in SingleOnly mode
// S64 - single precision in 32 64bit fp registers (In64BitMode)
// D32 - double precision in 16 32bit even fp registers
// D64 - double precision in 32 64bit fp registers (In64BitMode)
//
// Only S32 and D32 are supported right now.
//===----------------------------------------------------------------------===//
class ADDS_FT<string opstr, RegisterOperand RC, InstrItinClass Itin, bit IsComm,
              SDPatternOperator OpNode= null_frag> :
  InstSE<(outs RC:$fd), (ins RC:$fs, RC:$ft),
         !strconcat(opstr, "\t$fd, $fs, $ft"),
         [(set RC:$fd, (OpNode RC:$fs, RC:$ft))], Itin, FrmFR, opstr>,
  HARDFLOAT {
  let isCommutable = IsComm;
}

multiclass ADDS_M<string opstr, InstrItinClass Itin, bit IsComm,
                  SDPatternOperator OpNode = null_frag> {
  def _D32 : MMRel, ADDS_FT<opstr, AFGR64Opnd, Itin, IsComm, OpNode>, FGR_32;
  def _D64 : ADDS_FT<opstr, FGR64Opnd, Itin, IsComm, OpNode>, FGR_64 {
    string DecoderNamespace = "MipsFP64";
  }
}

class ABSS_FT<string opstr, RegisterOperand DstRC, RegisterOperand SrcRC,
              InstrItinClass Itin, SDPatternOperator OpNode= null_frag> :
  InstSE<(outs DstRC:$fd), (ins SrcRC:$fs), !strconcat(opstr, "\t$fd, $fs"),
         [(set DstRC:$fd, (OpNode SrcRC:$fs))], Itin, FrmFR, opstr>,
  HARDFLOAT,
  NeverHasSideEffects;

class CVT_PS_S_FT<string opstr, RegisterOperand DstRC, RegisterOperand SrcRC, InstrItinClass Itin, bit IsComm,
              SDPatternOperator OpNode= null_frag> :
  InstSE<(outs DstRC:$fd), (ins SrcRC:$fs, SrcRC:$ft),
         !strconcat(opstr, "\t$fd, $fs, $ft"),
         [(set DstRC:$fd, (OpNode SrcRC:$fs, SrcRC:$ft))], Itin, FrmFR, opstr>,
  HARDFLOAT {
  let isCommutable = IsComm;
}

multiclass ABSS_M<string opstr, InstrItinClass Itin,
                  SDPatternOperator OpNode= null_frag> {
  def _D32 : MMRel, ABSS_FT<opstr, AFGR64Opnd, AFGR64Opnd, Itin, OpNode>,
             FGR_32;
  def _D64 : ABSS_FT<opstr, FGR64Opnd, FGR64Opnd, Itin, OpNode>, FGR_64 {
    string DecoderNamespace = "MipsFP64";
  }
}

multiclass ROUND_M<string opstr, InstrItinClass Itin> {
  def _D32 : MMRel, ABSS_FT<opstr, FGR32Opnd, AFGR64Opnd, Itin>, FGR_32;
  def _D64 : StdMMR6Rel, ABSS_FT<opstr, FGR32Opnd, FGR64Opnd, Itin>, FGR_64 {
    let DecoderNamespace = "MipsFP64";
  }
}

class MFC1_FT<string opstr, RegisterOperand DstRC, RegisterOperand SrcRC,
              InstrItinClass Itin, SDPatternOperator OpNode= null_frag> :
  InstSE<(outs DstRC:$rt), (ins SrcRC:$fs), !strconcat(opstr, "\t$rt, $fs"),
         [(set DstRC:$rt, (OpNode SrcRC:$fs))], Itin, FrmFR, opstr>, HARDFLOAT {
  let isMoveReg = 1;
}

class MTC1_FT<string opstr, RegisterOperand DstRC, RegisterOperand SrcRC,
              InstrItinClass Itin, SDPatternOperator OpNode= null_frag> :
  InstSE<(outs DstRC:$fs), (ins SrcRC:$rt), !strconcat(opstr, "\t$rt, $fs"),
         [(set DstRC:$fs, (OpNode SrcRC:$rt))], Itin, FrmFR, opstr>, HARDFLOAT {
  let isMoveReg = 1;
}

class MTC1_64_FT<string opstr, RegisterOperand DstRC, RegisterOperand SrcRC,
                 InstrItinClass Itin> :
  InstSE<(outs DstRC:$fs), (ins DstRC:$fs_in, SrcRC:$rt),
         !strconcat(opstr, "\t$rt, $fs"), [], Itin, FrmFR, opstr>, HARDFLOAT {
  // $fs_in is part of a white lie to work around a widespread bug in the FPU
  // implementation. See expandBuildPairF64 for details.
  let Constraints = "$fs = $fs_in";
}

class LW_FT<string opstr, RegisterOperand RC, DAGOperand MO,
            InstrItinClass Itin, SDPatternOperator OpNode = null_frag> :
  InstSE<(outs RC:$rt), (ins MO:$addr), !strconcat(opstr, "\t$rt, $addr"),
         [(set RC:$rt, (OpNode addrDefault:$addr))], Itin, FrmFI, opstr>,
  HARDFLOAT {
  let DecoderMethod = "DecodeFMem";
  let mayLoad = 1;
}

class SW_FT<string opstr, RegisterOperand RC, DAGOperand MO,
            InstrItinClass Itin, SDPatternOperator OpNode = null_frag> :
  InstSE<(outs), (ins RC:$rt, MO:$addr), !strconcat(opstr, "\t$rt, $addr"),
         [(OpNode RC:$rt, addrDefault:$addr)], Itin, FrmFI, opstr>, HARDFLOAT {
  let DecoderMethod = "DecodeFMem";
  let mayStore = 1;
}

class MADDS_FT<string opstr, RegisterOperand RC, InstrItinClass Itin,
               SDPatternOperator OpNode = null_frag> :
  InstSE<(outs RC:$fd), (ins RC:$fr, RC:$fs, RC:$ft),
         !strconcat(opstr, "\t$fd, $fr, $fs, $ft"),
         [(set RC:$fd, (OpNode (fmul RC:$fs, RC:$ft), RC:$fr))], Itin,
         FrmFR, opstr>, HARDFLOAT;

class NMADDS_FT<string opstr, RegisterOperand RC, InstrItinClass Itin,
                SDPatternOperator OpNode = null_frag> :
  InstSE<(outs RC:$fd), (ins RC:$fr, RC:$fs, RC:$ft),
         !strconcat(opstr, "\t$fd, $fr, $fs, $ft"),
         [(set RC:$fd, (fsub fpimm0, (OpNode (fmul RC:$fs, RC:$ft), RC:$fr)))],
         Itin, FrmFR, opstr>, HARDFLOAT;

class LWXC1_FT<string opstr, RegisterOperand DRC,
               InstrItinClass Itin, SDPatternOperator OpNode = null_frag> :
  InstSE<(outs DRC:$fd), (ins PtrRC:$base, PtrRC:$index),
         !strconcat(opstr, "\t$fd, ${index}(${base})"),
         [(set DRC:$fd, (OpNode (add iPTR:$base, iPTR:$index)))], Itin,
         FrmFI, opstr>, HARDFLOAT {
  let AddedComplexity = 20;
}

class SWXC1_FT<string opstr, RegisterOperand DRC,
               InstrItinClass Itin, SDPatternOperator OpNode = null_frag> :
  InstSE<(outs), (ins DRC:$fs, PtrRC:$base, PtrRC:$index),
         !strconcat(opstr, "\t$fs, ${index}(${base})"),
         [(OpNode DRC:$fs, (add iPTR:$base, iPTR:$index))], Itin,
         FrmFI, opstr>, HARDFLOAT {
  let AddedComplexity = 20;
}

class BC1F_FT<string opstr, DAGOperand opnd, InstrItinClass Itin,
              SDPatternOperator Op = null_frag> :
  InstSE<(outs), (ins FCCRegsOpnd:$fcc, opnd:$offset),
         !strconcat(opstr, "\t$fcc, $offset"),
         [(MipsFPBrcond Op, FCCRegsOpnd:$fcc, bb:$offset)], Itin,
         FrmFI, opstr>, HARDFLOAT {
  let isBranch = 1;
  let isTerminator = 1;
  let hasDelaySlot = 1;
  let Defs = [AT];
  let hasFCCRegOperand = 1;
}

class BC1XL_FT<string opstr, DAGOperand opnd, InstrItinClass Itin> :
  InstSE<(outs), (ins FCCRegsOpnd:$fcc, opnd:$offset),
         !strconcat(opstr, "\t$fcc, $offset"), [], Itin,
         FrmFI, opstr>, HARDFLOAT {
  let isBranch = 1;
  let isTerminator = 1;
  let hasDelaySlot = 1;
  let Defs = [AT];
  let hasFCCRegOperand = 1;
}

class CEQS_FT<string typestr, RegisterClass RC, InstrItinClass Itin,
              SDPatternOperator OpNode = null_frag>  :
  InstSE<(outs), (ins RC:$fs, RC:$ft, condcode:$cond),
         !strconcat("c.$cond.", typestr, "\t$fs, $ft"),
         [(OpNode RC:$fs, RC:$ft, imm:$cond)], Itin, FrmFR,
         !strconcat("c.$cond.", typestr)>, HARDFLOAT {
  let Defs = [FCC0];
  let isCodeGenOnly = 1;
  let hasFCCRegOperand = 1;
}


// Note: MIPS-IV introduced $fcc1-$fcc7 and renamed FCSR31[23] $fcc0. Rather
//       duplicating the instruction definition for MIPS1 - MIPS3, we expand
//       c.cond.ft if necessary, and reject it after constructing the
//       instruction if the ISA doesn't support it.
class C_COND_FT<string CondStr, string Typestr, RegisterOperand RC,
                InstrItinClass itin>  :
   InstSE<(outs FCCRegsOpnd:$fcc), (ins RC:$fs, RC:$ft),
          !strconcat("c.", CondStr, ".", Typestr, "\t$fcc, $fs, $ft"), [], itin,
          FrmFR>, HARDFLOAT {
  let isCompare = 1;
  let hasFCCRegOperand = 1;
}


multiclass C_COND_M<string TypeStr, RegisterOperand RC, bits<5> fmt,
                    InstrItinClass itin> {
  def C_F_#NAME : MMRel, C_COND_FT<"f", TypeStr, RC, itin>,
                  C_COND_FM<fmt, 0> {
    let BaseOpcode = "c.f."#NAME;
    let isCommutable = 1;
  }
  def C_UN_#NAME : MMRel, C_COND_FT<"un", TypeStr, RC, itin>,
                   C_COND_FM<fmt, 1> {
    let BaseOpcode = "c.un."#NAME;
    let isCommutable = 1;
  }
  def C_EQ_#NAME : MMRel, C_COND_FT<"eq", TypeStr, RC, itin>,
                   C_COND_FM<fmt, 2> {
    let BaseOpcode = "c.eq."#NAME;
    let isCommutable = 1;
  }
  def C_UEQ_#NAME : MMRel, C_COND_FT<"ueq", TypeStr, RC, itin>,
                    C_COND_FM<fmt, 3> {
    let BaseOpcode = "c.ueq."#NAME;
    let isCommutable = 1;
  }
  def C_OLT_#NAME : MMRel, C_COND_FT<"olt", TypeStr, RC, itin>,
                    C_COND_FM<fmt, 4> {
    let BaseOpcode = "c.olt."#NAME;
  }
  def C_ULT_#NAME : MMRel, C_COND_FT<"ult", TypeStr, RC, itin>,
                    C_COND_FM<fmt, 5> {
    let BaseOpcode = "c.ult."#NAME;
  }
  def C_OLE_#NAME : MMRel, C_COND_FT<"ole", TypeStr, RC, itin>,
                    C_COND_FM<fmt, 6> {
    let BaseOpcode = "c.ole."#NAME;
  }
  def C_ULE_#NAME : MMRel, C_COND_FT<"ule", TypeStr, RC, itin>,
                     C_COND_FM<fmt, 7> {
    let BaseOpcode = "c.ule."#NAME;
  }
  def C_SF_#NAME : MMRel, C_COND_FT<"sf", TypeStr, RC, itin>,
                   C_COND_FM<fmt, 8> {
    let BaseOpcode = "c.sf."#NAME;
    let isCommutable = 1;
  }
  def C_NGLE_#NAME : MMRel, C_COND_FT<"ngle", TypeStr, RC, itin>,
                     C_COND_FM<fmt, 9> {
    let BaseOpcode = "c.ngle."#NAME;
  }
  def C_SEQ_#NAME : MMRel, C_COND_FT<"seq", TypeStr, RC, itin>,
                    C_COND_FM<fmt, 10> {
    let BaseOpcode = "c.seq."#NAME;
    let isCommutable = 1;
  }
  def C_NGL_#NAME : MMRel, C_COND_FT<"ngl", TypeStr, RC, itin>,
                    C_COND_FM<fmt, 11> {
    let BaseOpcode = "c.ngl."#NAME;
  }
  def C_LT_#NAME : MMRel, C_COND_FT<"lt", TypeStr, RC, itin>,
                   C_COND_FM<fmt, 12> {
    let BaseOpcode = "c.lt."#NAME;
  }
  def C_NGE_#NAME : MMRel, C_COND_FT<"nge", TypeStr, RC, itin>,
                    C_COND_FM<fmt, 13> {
    let BaseOpcode = "c.nge."#NAME;
  }
  def C_LE_#NAME : MMRel, C_COND_FT<"le", TypeStr, RC, itin>,
                   C_COND_FM<fmt, 14> {
    let BaseOpcode = "c.le."#NAME;
  }
  def C_NGT_#NAME : MMRel, C_COND_FT<"ngt", TypeStr, RC, itin>,
                    C_COND_FM<fmt, 15> {
    let BaseOpcode = "c.ngt."#NAME;
  }
}

let AdditionalPredicates = [NotInMicroMips] in {
defm S : C_COND_M<"s", FGR32Opnd, 16, II_C_CC_S>, ISA_MIPS1_NOT_32R6_64R6;
defm D32 : C_COND_M<"d", AFGR64Opnd, 17, II_C_CC_D>, ISA_MIPS1_NOT_32R6_64R6,
           FGR_32;
let DecoderNamespace = "MipsFP64" in
defm D64 : C_COND_M<"d", FGR64Opnd, 17, II_C_CC_D>, ISA_MIPS1_NOT_32R6_64R6,
           FGR_64;
}
//===----------------------------------------------------------------------===//
// Floating Point Instructions
//===----------------------------------------------------------------------===//
let AdditionalPredicates = [NotInMicroMips] in {
  def ROUND_W_S  : MMRel, StdMMR6Rel, ABSS_FT<"round.w.s", FGR32Opnd, FGR32Opnd, II_ROUND>,
                   ABSS_FM<0xc, 16>, ISA_MIPS2;
  defm ROUND_W : ROUND_M<"round.w.d", II_ROUND>, ABSS_FM<0xc, 17>, ISA_MIPS2;
  def TRUNC_W_S  : MMRel, StdMMR6Rel, ABSS_FT<"trunc.w.s", FGR32Opnd, FGR32Opnd, II_TRUNC>,
                   ABSS_FM<0xd, 16>, ISA_MIPS2;
  def CEIL_W_S   : MMRel, StdMMR6Rel, ABSS_FT<"ceil.w.s", FGR32Opnd, FGR32Opnd, II_CEIL>,
                   ABSS_FM<0xe, 16>, ISA_MIPS2;
  def FLOOR_W_S  : MMRel, StdMMR6Rel, ABSS_FT<"floor.w.s", FGR32Opnd, FGR32Opnd, II_FLOOR>,
                   ABSS_FM<0xf, 16>, ISA_MIPS2;
  def CVT_W_S    : MMRel, ABSS_FT<"cvt.w.s", FGR32Opnd, FGR32Opnd, II_CVT>,
                   ABSS_FM<0x24, 16>, ISA_MIPS1;

  defm TRUNC_W : ROUND_M<"trunc.w.d", II_TRUNC>, ABSS_FM<0xd, 17>, ISA_MIPS2;
  defm CEIL_W  : ROUND_M<"ceil.w.d", II_CEIL>, ABSS_FM<0xe, 17>, ISA_MIPS2;
  defm FLOOR_W : ROUND_M<"floor.w.d", II_FLOOR>, ABSS_FM<0xf, 17>, ISA_MIPS2;
  defm CVT_W   : ROUND_M<"cvt.w.d", II_CVT>, ABSS_FM<0x24, 17>, ISA_MIPS1;
}

let AdditionalPredicates = [NotInMicroMips] in {
  def RECIP_S : MMRel, ABSS_FT<"recip.s", FGR32Opnd, FGR32Opnd, II_RECIP_S>,
                ABSS_FM<0b010101, 0x10>, INSN_MIPS4_32R2;
  def RECIP_D32 : MMRel, ABSS_FT<"recip.d", AFGR64Opnd, AFGR64Opnd, II_RECIP_D>,
                  ABSS_FM<0b010101, 0x11>, INSN_MIPS4_32R2, FGR_32 {
    let BaseOpcode = "RECIP_D32";
  }
  let DecoderNamespace = "MipsFP64" in
    def RECIP_D64 : MMRel, ABSS_FT<"recip.d", FGR64Opnd, FGR64Opnd,
                                   II_RECIP_D>, ABSS_FM<0b010101, 0x11>,
                    INSN_MIPS4_32R2, FGR_64;
  def RSQRT_S : MMRel, ABSS_FT<"rsqrt.s", FGR32Opnd, FGR32Opnd, II_RSQRT_S>,
                ABSS_FM<0b010110, 0x10>, INSN_MIPS4_32R2;
  def RSQRT_D32 : MMRel, ABSS_FT<"rsqrt.d", AFGR64Opnd, AFGR64Opnd, II_RSQRT_D>,
                  ABSS_FM<0b010110, 0x11>, INSN_MIPS4_32R2, FGR_32 {
    let BaseOpcode = "RSQRT_D32";
  }
  let DecoderNamespace = "MipsFP64" in
    def RSQRT_D64 : MMRel, ABSS_FT<"rsqrt.d", FGR64Opnd, FGR64Opnd,
                                   II_RSQRT_D>, ABSS_FM<0b010110, 0x11>,
                    INSN_MIPS4_32R2, FGR_64;
}
let DecoderNamespace = "MipsFP64" in {
  let AdditionalPredicates = [NotInMicroMips] in {
  def ROUND_L_S : ABSS_FT<"round.l.s", FGR64Opnd, FGR32Opnd, II_ROUND>,
                  ABSS_FM<0x8, 16>, ISA_MIPS2, FGR_64;
  def ROUND_L_D64 : ABSS_FT<"round.l.d", FGR64Opnd, FGR64Opnd, II_ROUND>,
                    ABSS_FM<0x8, 17>, INSN_MIPS3_32, FGR_64;
  def TRUNC_L_S : ABSS_FT<"trunc.l.s", FGR64Opnd, FGR32Opnd, II_TRUNC>,
                  ABSS_FM<0x9, 16>, ISA_MIPS2, FGR_64;
  def TRUNC_L_D64 : ABSS_FT<"trunc.l.d", FGR64Opnd, FGR64Opnd, II_TRUNC>,
                    ABSS_FM<0x9, 17>, INSN_MIPS3_32, FGR_64;
  def CEIL_L_S  : ABSS_FT<"ceil.l.s", FGR64Opnd, FGR32Opnd, II_CEIL>,
                  ABSS_FM<0xa, 16>, ISA_MIPS2, FGR_64;
  def CEIL_L_D64 : ABSS_FT<"ceil.l.d", FGR64Opnd, FGR64Opnd, II_CEIL>,
                   ABSS_FM<0xa, 17>, INSN_MIPS3_32, FGR_64;
  def FLOOR_L_S : ABSS_FT<"floor.l.s", FGR64Opnd, FGR32Opnd, II_FLOOR>,
                  ABSS_FM<0xb, 16>, ISA_MIPS2, FGR_64;
  def FLOOR_L_D64 : ABSS_FT<"floor.l.d", FGR64Opnd, FGR64Opnd, II_FLOOR>,
                    ABSS_FM<0xb, 17>, INSN_MIPS3_32, FGR_64;
  }
}

let AdditionalPredicates = [NotInMicroMips] in{
  def CVT_S_W : MMRel, ABSS_FT<"cvt.s.w", FGR32Opnd, FGR32Opnd, II_CVT>,
                ABSS_FM<0x20, 20>, ISA_MIPS1;
  def CVT_L_S : MMRel, ABSS_FT<"cvt.l.s", FGR64Opnd, FGR32Opnd, II_CVT>,
                ABSS_FM<0x25, 16>, INSN_MIPS3_32R2;
  def CVT_L_D64: MMRel, ABSS_FT<"cvt.l.d", FGR64Opnd, FGR64Opnd, II_CVT>,
                 ABSS_FM<0x25, 17>, INSN_MIPS3_32R2;
}

let AdditionalPredicates = [NotInMicroMips] in {
  def CVT_S_D32 : MMRel, ABSS_FT<"cvt.s.d", FGR32Opnd, AFGR64Opnd, II_CVT>,
                  ABSS_FM<0x20, 17>, ISA_MIPS1, FGR_32;
  def CVT_D32_S : MMRel, ABSS_FT<"cvt.d.s", AFGR64Opnd, FGR32Opnd, II_CVT>,
                  ABSS_FM<0x21, 16>, ISA_MIPS1, FGR_32;
  def CVT_D32_W : MMRel, ABSS_FT<"cvt.d.w", AFGR64Opnd, FGR32Opnd, II_CVT>,
                  ABSS_FM<0x21, 20>, ISA_MIPS1, FGR_32;
}

let DecoderNamespace = "MipsFP64" in {
  let AdditionalPredicates = [NotInMicroMips] in {
    def PLL_PS64    : ADDS_FT<"pll.ps", FGR64Opnd, II_CVT, 0>,
                      ADDS_FM<0x2C, 22>,
                      ISA_MIPS32R2_NOT_32R6_64R6, FGR_64;
    def PLU_PS64    : ADDS_FT<"plu.ps", FGR64Opnd, II_CVT, 0>,
                      ADDS_FM<0x2D, 22>,
                      ISA_MIPS32R2_NOT_32R6_64R6, FGR_64;

    def CVT_S_PU64  : ABSS_FT<"cvt.s.pu", FGR32Opnd, FGR64Opnd, II_CVT>,
                      ABSS_FM<0x20, 22>,
                      ISA_MIPS32R2_NOT_32R6_64R6, FGR_64;
    def CVT_S_PL64  : ABSS_FT<"cvt.s.pl", FGR32Opnd, FGR64Opnd, II_CVT>,
                      ABSS_FM<0x28, 22>,
                      ISA_MIPS32R2_NOT_32R6_64R6, FGR_64;

    def CVT_PS_S64  : CVT_PS_S_FT<"cvt.ps.s", FGR64Opnd, FGR32Opnd, II_CVT, 0>,
                      ADDS_FM<0x26, 16>,
                      ISA_MIPS32R2_NOT_32R6_64R6, FGR_64;
  }
}

let DecoderNamespace = "MipsFP64" in {
  let AdditionalPredicates = [NotInMicroMips] in {
    def CVT_S_L   : ABSS_FT<"cvt.s.l", FGR32Opnd, FGR64Opnd, II_CVT>,
                    ABSS_FM<0x20, 21>, INSN_MIPS3_32R2, FGR_64;
    def CVT_S_D64 : ABSS_FT<"cvt.s.d", FGR32Opnd, FGR64Opnd, II_CVT>,
                    ABSS_FM<0x20, 17>, ISA_MIPS1, FGR_64;
    def CVT_D64_W : ABSS_FT<"cvt.d.w", FGR64Opnd, FGR32Opnd, II_CVT>,
                    ABSS_FM<0x21, 20>, ISA_MIPS1, FGR_64;
    def CVT_D64_S : ABSS_FT<"cvt.d.s", FGR64Opnd, FGR32Opnd, II_CVT>,
                    ABSS_FM<0x21, 16>, ISA_MIPS1, FGR_64;
    def CVT_D64_L : ABSS_FT<"cvt.d.l", FGR64Opnd, FGR64Opnd, II_CVT>,
                    ABSS_FM<0x21, 21>, INSN_MIPS3_32R2, FGR_64;
  }
}

let isPseudo = 1, isCodeGenOnly = 1 in {
  def PseudoCVT_S_W : ABSS_FT<"", FGR32Opnd, GPR32Opnd, II_CVT>;
  def PseudoCVT_D32_W : ABSS_FT<"", AFGR64Opnd, GPR32Opnd, II_CVT>;
  def PseudoCVT_S_L : ABSS_FT<"", FGR64Opnd, GPR64Opnd, II_CVT>;
  def PseudoCVT_D64_W : ABSS_FT<"", FGR64Opnd, GPR32Opnd, II_CVT>;
  def PseudoCVT_D64_L : ABSS_FT<"", FGR64Opnd, GPR64Opnd, II_CVT>;
}

let AdditionalPredicates = [NotInMicroMips] in {
  def FABS_S : MMRel, ABSS_FT<"abs.s", FGR32Opnd, FGR32Opnd, II_ABS, fabs>,
               ABSS_FM<0x5, 16>, ISA_MIPS1;
  defm FABS : ABSS_M<"abs.d", II_ABS, fabs>, ABSS_FM<0x5, 17>, ISA_MIPS1;
}

def FNEG_S : MMRel, ABSS_FT<"neg.s", FGR32Opnd, FGR32Opnd, II_NEG, fneg>,
             ABSS_FM<0x7, 16>, ISA_MIPS1;
let AdditionalPredicates = [NotInMicroMips] in {
  defm FNEG : ABSS_M<"neg.d", II_NEG, fneg>, ABSS_FM<0x7, 17>, ISA_MIPS1;
}

let AdditionalPredicates = [NotInMicroMips] in {
  def FSQRT_S : MMRel, StdMMR6Rel, ABSS_FT<"sqrt.s", FGR32Opnd, FGR32Opnd,
                II_SQRT_S, fsqrt>, ABSS_FM<0x4, 16>, ISA_MIPS2;
  defm FSQRT : ABSS_M<"sqrt.d", II_SQRT_D, fsqrt>, ABSS_FM<0x4, 17>, ISA_MIPS2;
}

// The odd-numbered registers are only referenced when doing loads,
// stores, and moves between floating-point and integer registers.
// When defining instructions, we reference all 32-bit registers,
// regardless of register aliasing.

/// Move Control Registers From/To CPU Registers
let AdditionalPredicates = [NotInMicroMips] in {
  def CFC1 : MMRel, MFC1_FT<"cfc1", GPR32Opnd, CCROpnd, II_CFC1>, MFC1_FM<2>,
             ISA_MIPS1;
  def CTC1 : MMRel, MTC1_FT<"ctc1", CCROpnd, GPR32Opnd, II_CTC1>, MFC1_FM<6>,
             ISA_MIPS1;

  def MFC1 : MMRel, StdMMR6Rel, MFC1_FT<"mfc1", GPR32Opnd, FGR32Opnd, II_MFC1,
                                        bitconvert>, MFC1_FM<0>, ISA_MIPS1;
  def MFC1_D64 : MFC1_FT<"mfc1", GPR32Opnd, FGR64Opnd, II_MFC1>, MFC1_FM<0>,
                 ISA_MIPS1, FGR_64 {
    let DecoderNamespace = "MipsFP64";
  }
  def MTC1 : MMRel, StdMMR6Rel, MTC1_FT<"mtc1", FGR32Opnd, GPR32Opnd, II_MTC1,
                                        bitconvert>, MFC1_FM<4>, ISA_MIPS1;
  def MTC1_D64 : MTC1_FT<"mtc1", FGR64Opnd, GPR32Opnd, II_MTC1>, MFC1_FM<4>,
                 ISA_MIPS1, FGR_64 {
    let DecoderNamespace = "MipsFP64";
  }

  def MFHC1_D32 : MMRel, MFC1_FT<"mfhc1", GPR32Opnd, AFGR64Opnd, II_MFHC1>,
                  MFC1_FM<3>, ISA_MIPS32R2, FGR_32;
  def MFHC1_D64 : MFC1_FT<"mfhc1", GPR32Opnd, FGR64Opnd, II_MFHC1>,
                  MFC1_FM<3>, ISA_MIPS32R2, FGR_64 {
    let DecoderNamespace = "MipsFP64";
  }

  def MTHC1_D32 : MMRel, StdMMR6Rel, MTC1_64_FT<"mthc1", AFGR64Opnd, GPR32Opnd, II_MTHC1>,
                  MFC1_FM<7>, ISA_MIPS32R2, FGR_32;
  def MTHC1_D64 : MTC1_64_FT<"mthc1", FGR64Opnd, GPR32Opnd, II_MTHC1>,
                  MFC1_FM<7>, ISA_MIPS32R2, FGR_64 {
    let DecoderNamespace = "MipsFP64";
  }

  def DMTC1 : MTC1_FT<"dmtc1", FGR64Opnd, GPR64Opnd, II_DMTC1,
              bitconvert>, MFC1_FM<5>, ISA_MIPS3;
  def DMFC1 : MFC1_FT<"dmfc1", GPR64Opnd, FGR64Opnd, II_DMFC1,
                      bitconvert>, MFC1_FM<1>, ISA_MIPS3;
  let isMoveReg = 1 in {
    def FMOV_S   : MMRel, ABSS_FT<"mov.s", FGR32Opnd, FGR32Opnd, II_MOV_S>,
                   ABSS_FM<0x6, 16>, ISA_MIPS1;
    def FMOV_D32 : MMRel, ABSS_FT<"mov.d", AFGR64Opnd, AFGR64Opnd, II_MOV_D>,
                   ABSS_FM<0x6, 17>, ISA_MIPS1, FGR_32;
    def FMOV_D64 : ABSS_FT<"mov.d", FGR64Opnd, FGR64Opnd, II_MOV_D>,
                   ABSS_FM<0x6, 17>, ISA_MIPS1, FGR_64 {
                     let DecoderNamespace = "MipsFP64";
    }
  } // isMoveReg
}

/// Floating Point Memory Instructions
let AdditionalPredicates = [NotInMicroMips] in {
  def LWC1 : MMRel, LW_FT<"lwc1", FGR32Opnd, mem_simm16, II_LWC1, load>,
             LW_FM<0x31>, ISA_MIPS1;
  def SWC1 : MMRel, SW_FT<"swc1", FGR32Opnd, mem_simm16, II_SWC1, store>,
             LW_FM<0x39>, ISA_MIPS1;
}

let DecoderNamespace = "MipsFP64", AdditionalPredicates = [NotInMicroMips] in {
  def LDC164 : StdMMR6Rel, LW_FT<"ldc1", FGR64Opnd, mem_simm16, II_LDC1, load>,
               LW_FM<0x35>, ISA_MIPS2, FGR_64 {
    let BaseOpcode = "LDC164";
  }
  def SDC164 : StdMMR6Rel, SW_FT<"sdc1", FGR64Opnd, mem_simm16, II_SDC1, store>,
               LW_FM<0x3d>, ISA_MIPS2, FGR_64;
}

let AdditionalPredicates = [NotInMicroMips] in {
  def LDC1 : MMRel, StdMMR6Rel, LW_FT<"ldc1", AFGR64Opnd, mem_simm16, II_LDC1,
                                      load>, LW_FM<0x35>, ISA_MIPS2, FGR_32 {
    let BaseOpcode = "LDC132";
  }
  def SDC1 : MMRel, SW_FT<"sdc1", AFGR64Opnd, mem_simm16, II_SDC1, store>,
             LW_FM<0x3d>, ISA_MIPS2, FGR_32;
}

// Indexed loads and stores.
// Base register + offset register addressing mode (indicated by "x" in the
// instruction mnemonic) is disallowed under NaCl.
let AdditionalPredicates = [IsNotNaCl] in {
  def LWXC1 : MMRel, LWXC1_FT<"lwxc1", FGR32Opnd, II_LWXC1, load>, LWXC1_FM<0>,
              INSN_MIPS4_32R2_NOT_32R6_64R6;
  def SWXC1 : MMRel, SWXC1_FT<"swxc1", FGR32Opnd, II_SWXC1, store>, SWXC1_FM<8>,
              INSN_MIPS4_32R2_NOT_32R6_64R6;
}

let AdditionalPredicates = [NotInMicroMips, IsNotNaCl] in {
  def LDXC1 : LWXC1_FT<"ldxc1", AFGR64Opnd, II_LDXC1, load>, LWXC1_FM<1>,
              INSN_MIPS4_32R2_NOT_32R6_64R6, FGR_32;
  def SDXC1 : SWXC1_FT<"sdxc1", AFGR64Opnd, II_SDXC1, store>, SWXC1_FM<9>,
              INSN_MIPS4_32R2_NOT_32R6_64R6, FGR_32;
}

let DecoderNamespace="MipsFP64" in {
  def LDXC164 : LWXC1_FT<"ldxc1", FGR64Opnd, II_LDXC1, load>, LWXC1_FM<1>,
                INSN_MIPS4_32R2_NOT_32R6_64R6, FGR_64;
  def SDXC164 : SWXC1_FT<"sdxc1", FGR64Opnd, II_SDXC1, store>, SWXC1_FM<9>,
                INSN_MIPS4_32R2_NOT_32R6_64R6, FGR_64;
}

// Load/store doubleword indexed unaligned.
// FIXME: This instruction should not be defined for FGR_32.
let AdditionalPredicates = [IsNotNaCl, NotInMicroMips] in {
  def LUXC1 : MMRel, LWXC1_FT<"luxc1", AFGR64Opnd, II_LUXC1>, LWXC1_FM<0x5>,
              INSN_MIPS5_32R2_NOT_32R6_64R6, FGR_32;
  def SUXC1 : MMRel, SWXC1_FT<"suxc1", AFGR64Opnd, II_SUXC1>, SWXC1_FM<0xd>,
              INSN_MIPS5_32R2_NOT_32R6_64R6, FGR_32;
}

let AdditionalPredicates = [IsNotNaCl, NotInMicroMips],
    DecoderNamespace="MipsFP64" in {
  def LUXC164 : LWXC1_FT<"luxc1", FGR64Opnd, II_LUXC1>, LWXC1_FM<0x5>,
                INSN_MIPS5_32R2_NOT_32R6_64R6, FGR_64;
  def SUXC164 : SWXC1_FT<"suxc1", FGR64Opnd, II_SUXC1>, SWXC1_FM<0xd>,
                INSN_MIPS5_32R2_NOT_32R6_64R6, FGR_64;
}

/// Floating-point Aritmetic
let AdditionalPredicates = [NotInMicroMips] in {
  def FADD_S : MMRel, ADDS_FT<"add.s", FGR32Opnd, II_ADD_S, 1, fadd>,
               ADDS_FM<0x00, 16>, ISA_MIPS1;
  defm FADD :  ADDS_M<"add.d", II_ADD_D, 1, fadd>, ADDS_FM<0x00, 17>,
               ISA_MIPS1;
  def FDIV_S : MMRel, ADDS_FT<"div.s", FGR32Opnd, II_DIV_S, 0, fdiv>,
               ADDS_FM<0x03, 16>, ISA_MIPS1;
  defm FDIV :  ADDS_M<"div.d", II_DIV_D, 0, fdiv>, ADDS_FM<0x03, 17>,
               ISA_MIPS1;
  def FMUL_S : MMRel, ADDS_FT<"mul.s", FGR32Opnd, II_MUL_S, 1, fmul>,
               ADDS_FM<0x02, 16>, ISA_MIPS1;
  defm FMUL :  ADDS_M<"mul.d", II_MUL_D, 1, fmul>, ADDS_FM<0x02, 17>,
               ISA_MIPS1;
  def FSUB_S : MMRel, ADDS_FT<"sub.s", FGR32Opnd, II_SUB_S, 0, fsub>,
               ADDS_FM<0x01, 16>, ISA_MIPS1;
  defm FSUB :  ADDS_M<"sub.d", II_SUB_D, 0, fsub>, ADDS_FM<0x01, 17>,
               ISA_MIPS1;
}

let AdditionalPredicates = [NotInMicroMips, HasMadd4] in {
  def MADD_S : MMRel, MADDS_FT<"madd.s", FGR32Opnd, II_MADD_S, fadd>,
               MADDS_FM<4, 0>, INSN_MIPS4_32R2_NOT_32R6_64R6;
  def MSUB_S : MMRel, MADDS_FT<"msub.s", FGR32Opnd, II_MSUB_S, fsub>,
               MADDS_FM<5, 0>, INSN_MIPS4_32R2_NOT_32R6_64R6;

  def MADD_D32 : MMRel, MADDS_FT<"madd.d", AFGR64Opnd, II_MADD_D, fadd>,
                 MADDS_FM<4, 1>, INSN_MIPS4_32R2_NOT_32R6_64R6, FGR_32;
  def MSUB_D32 : MMRel, MADDS_FT<"msub.d", AFGR64Opnd, II_MSUB_D, fsub>,
                 MADDS_FM<5, 1>, INSN_MIPS4_32R2_NOT_32R6_64R6, FGR_32;

  let DecoderNamespace = "MipsFP64" in {
    def MADD_D64 : MADDS_FT<"madd.d", FGR64Opnd, II_MADD_D, fadd>,
                   MADDS_FM<4, 1>, INSN_MIPS4_32R2_NOT_32R6_64R6, FGR_64;
    def MSUB_D64 : MADDS_FT<"msub.d", FGR64Opnd, II_MSUB_D, fsub>,
                   MADDS_FM<5, 1>, INSN_MIPS4_32R2_NOT_32R6_64R6, FGR_64;
  }
}

let AdditionalPredicates = [NoNaNsFPMath, HasMadd4, NotInMicroMips] in {
  def NMADD_S : MMRel, NMADDS_FT<"nmadd.s", FGR32Opnd, II_NMADD_S, fadd>,
                MADDS_FM<6, 0>, INSN_MIPS4_32R2_NOT_32R6_64R6;
  def NMSUB_S : MMRel, NMADDS_FT<"nmsub.s", FGR32Opnd, II_NMSUB_S, fsub>,
                MADDS_FM<7, 0>, INSN_MIPS4_32R2_NOT_32R6_64R6;

  def NMADD_D32 : MMRel, NMADDS_FT<"nmadd.d", AFGR64Opnd, II_NMADD_D, fadd>,
                  MADDS_FM<6, 1>, INSN_MIPS4_32R2_NOT_32R6_64R6, FGR_32;
  def NMSUB_D32 : MMRel, NMADDS_FT<"nmsub.d", AFGR64Opnd, II_NMSUB_D, fsub>,
                  MADDS_FM<7, 1>, INSN_MIPS4_32R2_NOT_32R6_64R6, FGR_32;

  let DecoderNamespace = "MipsFP64" in {
    def NMADD_D64 : NMADDS_FT<"nmadd.d", FGR64Opnd, II_NMADD_D, fadd>,
                    MADDS_FM<6, 1>, INSN_MIPS4_32R2_NOT_32R6_64R6, FGR_64;
    def NMSUB_D64 : NMADDS_FT<"nmsub.d", FGR64Opnd, II_NMSUB_D, fsub>,
                    MADDS_FM<7, 1>, INSN_MIPS4_32R2_NOT_32R6_64R6, FGR_64;
  }
}
//===----------------------------------------------------------------------===//
// Floating Point Branch Codes
//===----------------------------------------------------------------------===//
// Mips branch codes. These correspond to condcode in MipsInstrInfo.h.
// They must be kept in synch.
def MIPS_BRANCH_F  : PatLeaf<(i32 0)>;
def MIPS_BRANCH_T  : PatLeaf<(i32 1)>;

let AdditionalPredicates = [NotInMicroMips] in {
  def BC1F : MMRel, BC1F_FT<"bc1f", brtarget, II_BC1F, MIPS_BRANCH_F>,
             BC1F_FM<0, 0>, ISA_MIPS1_NOT_32R6_64R6;
  def BC1FL : MMRel, BC1XL_FT<"bc1fl", brtarget, II_BC1FL>,
              BC1F_FM<1, 0>, ISA_MIPS2_NOT_32R6_64R6;
  def BC1T : MMRel, BC1F_FT<"bc1t", brtarget, II_BC1T, MIPS_BRANCH_T>,
             BC1F_FM<0, 1>, ISA_MIPS1_NOT_32R6_64R6;
  def BC1TL : MMRel, BC1XL_FT<"bc1tl", brtarget, II_BC1TL>,
              BC1F_FM<1, 1>, ISA_MIPS2_NOT_32R6_64R6;

/// Floating Point Compare
  def FCMP_S32 : MMRel, CEQS_FT<"s", FGR32, II_C_CC_S, MipsFPCmp>, CEQS_FM<16>,
                 ISA_MIPS1_NOT_32R6_64R6 {

  // FIXME: This is a required to work around the fact that these instructions
  //        only use $fcc0. Ideally, MipsFPCmp nodes could be removed and the
  //        fcc register set is used directly.
  bits<3> fcc = 0;
  }
  def FCMP_D32 : MMRel, CEQS_FT<"d", AFGR64, II_C_CC_D, MipsFPCmp>, CEQS_FM<17>,
                 ISA_MIPS1_NOT_32R6_64R6, FGR_32 {
  // FIXME: This is a required to work around the fact that these instructions
  //        only use $fcc0. Ideally, MipsFPCmp nodes could be removed and the
  //        fcc register set is used directly.
  bits<3> fcc = 0;
  }
}
let DecoderNamespace = "MipsFP64" in
def FCMP_D64 : CEQS_FT<"d", FGR64, II_C_CC_D, MipsFPCmp>, CEQS_FM<17>,
               ISA_MIPS1_NOT_32R6_64R6, FGR_64 {
  // FIXME: This is a required to work around the fact that thiese instructions
  //        only use $fcc0. Ideally, MipsFPCmp nodes could be removed and the
  //        fcc register set is used directly.
  bits<3> fcc = 0;
}

//===----------------------------------------------------------------------===//
// Floating Point Pseudo-Instructions
//===----------------------------------------------------------------------===//

// This pseudo instr gets expanded into 2 mtc1 instrs after register
// allocation.
class BuildPairF64Base<RegisterOperand RO> :
  PseudoSE<(outs RO:$dst), (ins GPR32Opnd:$lo, GPR32Opnd:$hi),
           [(set RO:$dst, (MipsBuildPairF64 GPR32Opnd:$lo, GPR32Opnd:$hi))],
           II_MTC1>;

def BuildPairF64 : BuildPairF64Base<AFGR64Opnd>, FGR_32, HARDFLOAT;
def BuildPairF64_64 : BuildPairF64Base<FGR64Opnd>, FGR_64, HARDFLOAT;

// This pseudo instr gets expanded into 2 mfc1 instrs after register
// allocation.
// if n is 0, lower part of src is extracted.
// if n is 1, higher part of src is extracted.
// This node has associated scheduling information as the pre RA scheduler
// asserts otherwise.
class ExtractElementF64Base<RegisterOperand RO> :
  PseudoSE<(outs GPR32Opnd:$dst), (ins RO:$src, i32imm:$n),
           [(set GPR32Opnd:$dst, (MipsExtractElementF64 RO:$src, imm:$n))],
           II_MFC1>;

def ExtractElementF64 : ExtractElementF64Base<AFGR64Opnd>, FGR_32, HARDFLOAT;
def ExtractElementF64_64 : ExtractElementF64Base<FGR64Opnd>, FGR_64, HARDFLOAT;

def PseudoTRUNC_W_S : MipsAsmPseudoInst<(outs FGR32Opnd:$fd),
                                        (ins FGR32Opnd:$fs, GPR32Opnd:$rs),
                                        "trunc.w.s\t$fd, $fs, $rs">;

def PseudoTRUNC_W_D32 : MipsAsmPseudoInst<(outs FGR32Opnd:$fd),
                                          (ins AFGR64Opnd:$fs, GPR32Opnd:$rs),
                                          "trunc.w.d\t$fd, $fs, $rs">,
                        FGR_32, HARDFLOAT;

def PseudoTRUNC_W_D : MipsAsmPseudoInst<(outs FGR32Opnd:$fd),
                                        (ins FGR64Opnd:$fs, GPR32Opnd:$rs),
                                        "trunc.w.d\t$fd, $fs, $rs">,
                      FGR_64, HARDFLOAT;

def LoadImmSingleGPR : MipsAsmPseudoInst<(outs GPR32Opnd:$rd),
                                         (ins imm64:$fpimm),
                                         "li.s\t$rd, $fpimm">;

def LoadImmSingleFGR : MipsAsmPseudoInst<(outs StrictlyFGR32Opnd:$rd),
                                         (ins imm64:$fpimm),
                                         "li.s\t$rd, $fpimm">,
                       HARDFLOAT;

def LoadImmDoubleGPR : MipsAsmPseudoInst<(outs GPR32Opnd:$rd),
                                         (ins imm64:$fpimm),
                                         "li.d\t$rd, $fpimm">;

def LoadImmDoubleFGR_32 : MipsAsmPseudoInst<(outs StrictlyAFGR64Opnd:$rd),
                                            (ins imm64:$fpimm),
                                            "li.d\t$rd, $fpimm">,
                          FGR_32, HARDFLOAT;

def LoadImmDoubleFGR : MipsAsmPseudoInst<(outs StrictlyFGR64Opnd:$rd),
                                         (ins imm64:$fpimm),
                                         "li.d\t$rd, $fpimm">,
                       FGR_64, HARDFLOAT;

//===----------------------------------------------------------------------===//
// InstAliases.
//===----------------------------------------------------------------------===//
def : MipsInstAlias
        <"s.s $fd, $addr", (SWC1 FGR32Opnd:$fd, mem_simm16:$addr), 0>,
      ISA_MIPS2, HARDFLOAT;
def : MipsInstAlias
        <"s.d $fd, $addr", (SDC1 AFGR64Opnd:$fd, mem_simm16:$addr), 0>,
      FGR_32, ISA_MIPS2, HARDFLOAT;
def : MipsInstAlias
        <"s.d $fd, $addr", (SDC164 FGR64Opnd:$fd, mem_simm16:$addr), 0>,
      FGR_64, ISA_MIPS2, HARDFLOAT;

def : MipsInstAlias
        <"l.s $fd, $addr", (LWC1 FGR32Opnd:$fd, mem_simm16:$addr), 0>,
      ISA_MIPS2, HARDFLOAT;
def : MipsInstAlias
        <"l.d $fd, $addr", (LDC1 AFGR64Opnd:$fd, mem_simm16:$addr), 0>,
      FGR_32, ISA_MIPS2, HARDFLOAT;
def : MipsInstAlias
        <"l.d $fd, $addr", (LDC164 FGR64Opnd:$fd, mem_simm16:$addr), 0>,
      FGR_64, ISA_MIPS2, HARDFLOAT;

multiclass C_COND_ALIASES<string TypeStr, RegisterOperand RC> {
  def : MipsInstAlias<!strconcat("c.f.", TypeStr, " $fs, $ft"),
                      (!cast<Instruction>("C_F_"#NAME) FCC0,
                                                       RC:$fs, RC:$ft), 1>;
  def : MipsInstAlias<!strconcat("c.un.", TypeStr, " $fs, $ft"),
                      (!cast<Instruction>("C_UN_"#NAME) FCC0,
                                                        RC:$fs, RC:$ft), 1>;
  def : MipsInstAlias<!strconcat("c.eq.", TypeStr, " $fs, $ft"),
                      (!cast<Instruction>("C_EQ_"#NAME) FCC0,
                                                        RC:$fs, RC:$ft), 1>;
  def : MipsInstAlias<!strconcat("c.ueq.", TypeStr, " $fs, $ft"),
                      (!cast<Instruction>("C_UEQ_"#NAME) FCC0,
                                                         RC:$fs, RC:$ft), 1>;
  def : MipsInstAlias<!strconcat("c.olt.", TypeStr, " $fs, $ft"),
                      (!cast<Instruction>("C_OLT_"#NAME) FCC0,
                                                         RC:$fs, RC:$ft), 1>;
  def : MipsInstAlias<!strconcat("c.ult.", TypeStr, " $fs, $ft"),
                      (!cast<Instruction>("C_ULT_"#NAME) FCC0,
                                                         RC:$fs, RC:$ft), 1>;
  def : MipsInstAlias<!strconcat("c.ole.", TypeStr, " $fs, $ft"),
                      (!cast<Instruction>("C_OLE_"#NAME) FCC0,
                                                         RC:$fs, RC:$ft), 1>;
  def : MipsInstAlias<!strconcat("c.ule.", TypeStr, " $fs, $ft"),
                      (!cast<Instruction>("C_ULE_"#NAME) FCC0,
                                                         RC:$fs, RC:$ft), 1>;
  def : MipsInstAlias<!strconcat("c.sf.", TypeStr, " $fs, $ft"),
                      (!cast<Instruction>("C_SF_"#NAME) FCC0,
                                                        RC:$fs, RC:$ft), 1>;
  def : MipsInstAlias<!strconcat("c.ngle.", TypeStr, " $fs, $ft"),
                      (!cast<Instruction>("C_NGLE_"#NAME) FCC0,
                                                          RC:$fs, RC:$ft), 1>;
  def : MipsInstAlias<!strconcat("c.seq.", TypeStr, " $fs, $ft"),
                      (!cast<Instruction>("C_SEQ_"#NAME) FCC0,
                                                         RC:$fs, RC:$ft), 1>;
  def : MipsInstAlias<!strconcat("c.ngl.", TypeStr, " $fs, $ft"),
                      (!cast<Instruction>("C_NGL_"#NAME) FCC0,
                                                         RC:$fs, RC:$ft), 1>;
  def : MipsInstAlias<!strconcat("c.lt.", TypeStr, " $fs, $ft"),
                      (!cast<Instruction>("C_LT_"#NAME) FCC0,
                                                        RC:$fs, RC:$ft), 1>;
  def : MipsInstAlias<!strconcat("c.nge.", TypeStr, " $fs, $ft"),
                      (!cast<Instruction>("C_NGE_"#NAME) FCC0,
                                                         RC:$fs, RC:$ft), 1>;
  def : MipsInstAlias<!strconcat("c.le.", TypeStr, " $fs, $ft"),
                      (!cast<Instruction>("C_LE_"#NAME) FCC0,
                                                        RC:$fs, RC:$ft), 1>;
  def : MipsInstAlias<!strconcat("c.ngt.", TypeStr, " $fs, $ft"),
                      (!cast<Instruction>("C_NGT_"#NAME) FCC0,
                                                         RC:$fs, RC:$ft), 1>;
}

multiclass BC1_ALIASES<Instruction BCTrue, string BCTrueString,
                       Instruction BCFalse, string BCFalseString> {
  def : MipsInstAlias<!strconcat(BCTrueString, " $offset"),
                                (BCTrue FCC0, brtarget:$offset), 1>;

  def : MipsInstAlias<!strconcat(BCFalseString, " $offset"),
                                (BCFalse FCC0, brtarget:$offset), 1>;
}

let AdditionalPredicates = [NotInMicroMips] in {
  defm S   : C_COND_ALIASES<"s", FGR32Opnd>, HARDFLOAT,
             ISA_MIPS1_NOT_32R6_64R6;
  defm D32 : C_COND_ALIASES<"d", AFGR64Opnd>, HARDFLOAT,
             ISA_MIPS1_NOT_32R6_64R6, FGR_32;
  defm D64 : C_COND_ALIASES<"d", FGR64Opnd>, HARDFLOAT,
             ISA_MIPS1_NOT_32R6_64R6, FGR_64;

  defm : BC1_ALIASES<BC1T, "bc1t", BC1F, "bc1f">, ISA_MIPS1_NOT_32R6_64R6,
         HARDFLOAT;
  defm : BC1_ALIASES<BC1TL, "bc1tl", BC1FL, "bc1fl">, ISA_MIPS2_NOT_32R6_64R6,
         HARDFLOAT;
}
//===----------------------------------------------------------------------===//
// Floating Point Patterns
//===----------------------------------------------------------------------===//
def : MipsPat<(f32 fpimm0), (MTC1 ZERO)>, ISA_MIPS1;
def : MipsPat<(f32 fpimm0neg), (FNEG_S (MTC1 ZERO))>, ISA_MIPS1;

def : MipsPat<(f32 (sint_to_fp GPR32Opnd:$src)),
              (PseudoCVT_S_W GPR32Opnd:$src)>;
def : MipsPat<(MipsTruncIntFP FGR32Opnd:$src),
              (TRUNC_W_S FGR32Opnd:$src)>, ISA_MIPS1;

def : MipsPat<(MipsMTC1_D64 GPR32Opnd:$src),
              (MTC1_D64 GPR32Opnd:$src)>, ISA_MIPS1, FGR_64;

def : MipsPat<(f64 (sint_to_fp GPR32Opnd:$src)),
              (PseudoCVT_D32_W GPR32Opnd:$src)>, FGR_32;
let AdditionalPredicates = [NotInMicroMips] in {
  def : MipsPat<(MipsTruncIntFP AFGR64Opnd:$src),
                (TRUNC_W_D32 AFGR64Opnd:$src)>, ISA_MIPS2, FGR_32;
  def : MipsPat<(f32 (fpround AFGR64Opnd:$src)),
                (CVT_S_D32 AFGR64Opnd:$src)>, ISA_MIPS1, FGR_32;
  def : MipsPat<(f64 (fpextend FGR32Opnd:$src)),
                (CVT_D32_S FGR32Opnd:$src)>, ISA_MIPS1, FGR_32;
}

def : MipsPat<(f64 fpimm0), (DMTC1 ZERO_64)>, ISA_MIPS3, GPR_64, FGR_64;
def : MipsPat<(f64 fpimm0neg), (FNEG_D64 (DMTC1 ZERO_64))>, ISA_MIPS3, GPR_64,
      FGR_64;

def : MipsPat<(f64 (sint_to_fp GPR32Opnd:$src)),
              (PseudoCVT_D64_W GPR32Opnd:$src)>, FGR_64;
def : MipsPat<(f32 (sint_to_fp GPR64Opnd:$src)),
              (EXTRACT_SUBREG (PseudoCVT_S_L GPR64Opnd:$src), sub_lo)>, FGR_64;
def : MipsPat<(f64 (sint_to_fp GPR64Opnd:$src)),
              (PseudoCVT_D64_L GPR64Opnd:$src)>, FGR_64;

def : MipsPat<(MipsTruncIntFP FGR64Opnd:$src),
              (TRUNC_W_D64 FGR64Opnd:$src)>, ISA_MIPS2, FGR_64;
def : MipsPat<(MipsTruncIntFP FGR32Opnd:$src),
              (TRUNC_L_S FGR32Opnd:$src)>, ISA_MIPS2, FGR_64;
def : MipsPat<(MipsTruncIntFP FGR64Opnd:$src),
              (TRUNC_L_D64 FGR64Opnd:$src)>, ISA_MIPS2, FGR_64;

let AdditionalPredicates = [NotInMicroMips] in {
  def : MipsPat<(f32 (fpround FGR64Opnd:$src)),
                (CVT_S_D64 FGR64Opnd:$src)>, ISA_MIPS1, FGR_64;
  def : MipsPat<(f64 (fpextend FGR32Opnd:$src)),
                (CVT_D64_S FGR32Opnd:$src)>, ISA_MIPS1, FGR_64;
}

// To generate NMADD and NMSUB instructions when fneg node is present
multiclass NMADD_NMSUB<Instruction Nmadd, Instruction Nmsub, RegisterOperand RC> {
  def : MipsPat<(fneg (fadd (fmul RC:$fs, RC:$ft), RC:$fr)),
                (Nmadd RC:$fr, RC:$fs, RC:$ft)>;
  def : MipsPat<(fneg (fsub (fmul RC:$fs, RC:$ft), RC:$fr)),
                (Nmsub RC:$fr, RC:$fs, RC:$ft)>;
}

let AdditionalPredicates = [NoNaNsFPMath, HasMadd4, NotInMicroMips] in {
  defm : NMADD_NMSUB<NMADD_S, NMSUB_S, FGR32Opnd>, INSN_MIPS4_32R2_NOT_32R6_64R6;
  defm : NMADD_NMSUB<NMADD_D32, NMSUB_D32, AFGR64Opnd>, FGR_32, INSN_MIPS4_32R2_NOT_32R6_64R6;
  defm : NMADD_NMSUB<NMADD_D64, NMSUB_D64, FGR64Opnd>, FGR_64, INSN_MIPS4_32R2_NOT_32R6_64R6;
}

// Patterns for loads/stores with a reg+imm operand.
let AdditionalPredicates = [NotInMicroMips] in {
  let AddedComplexity = 40 in {
    def : LoadRegImmPat<LWC1, f32, load>, ISA_MIPS1;
    def : StoreRegImmPat<SWC1, f32>, ISA_MIPS1;

    def : LoadRegImmPat<LDC164, f64, load>, ISA_MIPS1, FGR_64;
    def : StoreRegImmPat<SDC164, f64>, ISA_MIPS1, FGR_64;

    def : LoadRegImmPat<LDC1, f64, load>, ISA_MIPS1, FGR_32;
    def : StoreRegImmPat<SDC1, f64>, ISA_MIPS1, FGR_32;
  }
}
