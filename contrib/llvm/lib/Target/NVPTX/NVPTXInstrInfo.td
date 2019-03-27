//===- NVPTXInstrInfo.td - NVPTX Instruction defs -------------*- tblgen-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file describes the PTX instructions in TableGen format.
//
//===----------------------------------------------------------------------===//

include "NVPTXInstrFormats.td"

// A NOP instruction
let hasSideEffects = 0 in {
  def NOP : NVPTXInst<(outs), (ins), "", []>;
}

let OperandType = "OPERAND_IMMEDIATE" in {
  def f16imm : Operand<f16>;
}

// List of vector specific properties
def isVecLD      : VecInstTypeEnum<1>;
def isVecST      : VecInstTypeEnum<2>;
def isVecBuild   : VecInstTypeEnum<3>;
def isVecShuffle : VecInstTypeEnum<4>;
def isVecExtract : VecInstTypeEnum<5>;
def isVecInsert  : VecInstTypeEnum<6>;
def isVecDest    : VecInstTypeEnum<7>;
def isVecOther   : VecInstTypeEnum<15>;

//===----------------------------------------------------------------------===//
// NVPTX Operand Definitions.
//===----------------------------------------------------------------------===//

def brtarget    : Operand<OtherVT>;

// CVT conversion modes
// These must match the enum in NVPTX.h
def CvtNONE : PatLeaf<(i32 0x0)>;
def CvtRNI  : PatLeaf<(i32 0x1)>;
def CvtRZI  : PatLeaf<(i32 0x2)>;
def CvtRMI  : PatLeaf<(i32 0x3)>;
def CvtRPI  : PatLeaf<(i32 0x4)>;
def CvtRN   : PatLeaf<(i32 0x5)>;
def CvtRZ   : PatLeaf<(i32 0x6)>;
def CvtRM   : PatLeaf<(i32 0x7)>;
def CvtRP   : PatLeaf<(i32 0x8)>;

def CvtNONE_FTZ : PatLeaf<(i32 0x10)>;
def CvtRNI_FTZ  : PatLeaf<(i32 0x11)>;
def CvtRZI_FTZ  : PatLeaf<(i32 0x12)>;
def CvtRMI_FTZ  : PatLeaf<(i32 0x13)>;
def CvtRPI_FTZ  : PatLeaf<(i32 0x14)>;
def CvtRN_FTZ   : PatLeaf<(i32 0x15)>;
def CvtRZ_FTZ   : PatLeaf<(i32 0x16)>;
def CvtRM_FTZ   : PatLeaf<(i32 0x17)>;
def CvtRP_FTZ   : PatLeaf<(i32 0x18)>;

def CvtSAT      : PatLeaf<(i32 0x20)>;
def CvtSAT_FTZ  : PatLeaf<(i32 0x30)>;

def CvtMode : Operand<i32> {
  let PrintMethod = "printCvtMode";
}

// Compare modes
// These must match the enum in NVPTX.h
def CmpEQ   : PatLeaf<(i32 0)>;
def CmpNE   : PatLeaf<(i32 1)>;
def CmpLT   : PatLeaf<(i32 2)>;
def CmpLE   : PatLeaf<(i32 3)>;
def CmpGT   : PatLeaf<(i32 4)>;
def CmpGE   : PatLeaf<(i32 5)>;
def CmpEQU  : PatLeaf<(i32 10)>;
def CmpNEU  : PatLeaf<(i32 11)>;
def CmpLTU  : PatLeaf<(i32 12)>;
def CmpLEU  : PatLeaf<(i32 13)>;
def CmpGTU  : PatLeaf<(i32 14)>;
def CmpGEU  : PatLeaf<(i32 15)>;
def CmpNUM  : PatLeaf<(i32 16)>;
def CmpNAN  : PatLeaf<(i32 17)>;

def CmpEQ_FTZ   : PatLeaf<(i32 0x100)>;
def CmpNE_FTZ   : PatLeaf<(i32 0x101)>;
def CmpLT_FTZ   : PatLeaf<(i32 0x102)>;
def CmpLE_FTZ   : PatLeaf<(i32 0x103)>;
def CmpGT_FTZ   : PatLeaf<(i32 0x104)>;
def CmpGE_FTZ   : PatLeaf<(i32 0x105)>;
def CmpEQU_FTZ  : PatLeaf<(i32 0x10A)>;
def CmpNEU_FTZ  : PatLeaf<(i32 0x10B)>;
def CmpLTU_FTZ  : PatLeaf<(i32 0x10C)>;
def CmpLEU_FTZ  : PatLeaf<(i32 0x10D)>;
def CmpGTU_FTZ  : PatLeaf<(i32 0x10E)>;
def CmpGEU_FTZ  : PatLeaf<(i32 0x10F)>;
def CmpNUM_FTZ  : PatLeaf<(i32 0x110)>;
def CmpNAN_FTZ  : PatLeaf<(i32 0x111)>;

def CmpMode : Operand<i32> {
  let PrintMethod = "printCmpMode";
}
def VecElement : Operand<i32> {
  let PrintMethod = "printVecElement";
}

//===----------------------------------------------------------------------===//
// NVPTX Instruction Predicate Definitions
//===----------------------------------------------------------------------===//


def hasAtomAddF64 : Predicate<"Subtarget->hasAtomAddF64()">;
def hasAtomScope : Predicate<"Subtarget->hasAtomScope()">;
def hasAtomBitwise64 : Predicate<"Subtarget->hasAtomBitwise64()">;
def hasAtomMinMax64 : Predicate<"Subtarget->hasAtomMinMax64()">;
def hasVote : Predicate<"Subtarget->hasVote()">;
def hasDouble : Predicate<"Subtarget->hasDouble()">;
def hasLDG : Predicate<"Subtarget->hasLDG()">;
def hasLDU : Predicate<"Subtarget->hasLDU()">;

def doF32FTZ : Predicate<"useF32FTZ()">;
def doNoF32FTZ : Predicate<"!useF32FTZ()">;

def doMulWide      : Predicate<"doMulWide">;

def allowFMA : Predicate<"allowFMA()">;
def noFMA : Predicate<"!allowFMA()">;
def allowUnsafeFPMath : Predicate<"allowUnsafeFPMath()">;

def do_DIVF32_APPROX : Predicate<"getDivF32Level()==0">;
def do_DIVF32_FULL : Predicate<"getDivF32Level()==1">;

def do_SQRTF32_APPROX : Predicate<"!usePrecSqrtF32()">;
def do_SQRTF32_RN : Predicate<"usePrecSqrtF32()">;

def hasHWROT32 : Predicate<"Subtarget->hasHWROT32()">;
def noHWROT32 : Predicate<"!Subtarget->hasHWROT32()">;

def true : Predicate<"true">;

def hasPTX31 : Predicate<"Subtarget->getPTXVersion() >= 31">;
def hasPTX60 : Predicate<"Subtarget->getPTXVersion() >= 60">;
def hasPTX61 : Predicate<"Subtarget->getPTXVersion() >= 61">;

def hasSM30 : Predicate<"Subtarget->getSmVersion() >= 30">;
def hasSM70 : Predicate<"Subtarget->getSmVersion() >= 70">;

def useShortPtr : Predicate<"useShortPointers()">;
def useFP16Math: Predicate<"Subtarget->allowFP16Math()">;

//===----------------------------------------------------------------------===//
// Some Common Instruction Class Templates
//===----------------------------------------------------------------------===//

// Template for instructions which take three int64, int32, or int16 args.
// The instructions are named "<OpcStr><Width>" (e.g. "add.s64").
multiclass I3<string OpcStr, SDNode OpNode> {
  def i64rr :
    NVPTXInst<(outs Int64Regs:$dst), (ins Int64Regs:$a, Int64Regs:$b),
              !strconcat(OpcStr, "64 \t$dst, $a, $b;"),
              [(set Int64Regs:$dst, (OpNode Int64Regs:$a, Int64Regs:$b))]>;
  def i64ri :
    NVPTXInst<(outs Int64Regs:$dst), (ins Int64Regs:$a, i64imm:$b),
              !strconcat(OpcStr, "64 \t$dst, $a, $b;"),
              [(set Int64Regs:$dst, (OpNode Int64Regs:$a, imm:$b))]>;
  def i32rr :
    NVPTXInst<(outs Int32Regs:$dst), (ins Int32Regs:$a, Int32Regs:$b),
              !strconcat(OpcStr, "32 \t$dst, $a, $b;"),
              [(set Int32Regs:$dst, (OpNode Int32Regs:$a, Int32Regs:$b))]>;
  def i32ri :
    NVPTXInst<(outs Int32Regs:$dst), (ins Int32Regs:$a, i32imm:$b),
              !strconcat(OpcStr, "32 \t$dst, $a, $b;"),
              [(set Int32Regs:$dst, (OpNode Int32Regs:$a, imm:$b))]>;
  def i16rr :
    NVPTXInst<(outs Int16Regs:$dst), (ins Int16Regs:$a, Int16Regs:$b),
              !strconcat(OpcStr, "16 \t$dst, $a, $b;"),
              [(set Int16Regs:$dst, (OpNode Int16Regs:$a, Int16Regs:$b))]>;
  def i16ri :
    NVPTXInst<(outs Int16Regs:$dst), (ins Int16Regs:$a, i16imm:$b),
              !strconcat(OpcStr, "16 \t$dst, $a, $b;"),
              [(set Int16Regs:$dst, (OpNode Int16Regs:$a, (imm):$b))]>;
}

// Template for instructions which take 3 int32 args.  The instructions are
// named "<OpcStr>.s32" (e.g. "addc.cc.s32").
multiclass ADD_SUB_INT_32<string OpcStr, SDNode OpNode> {
   def i32rr :
     NVPTXInst<(outs Int32Regs:$dst), (ins Int32Regs:$a, Int32Regs:$b),
               !strconcat(OpcStr, ".s32 \t$dst, $a, $b;"),
               [(set Int32Regs:$dst, (OpNode Int32Regs:$a, Int32Regs:$b))]>;
   def i32ri :
     NVPTXInst<(outs Int32Regs:$dst), (ins Int32Regs:$a, i32imm:$b),
               !strconcat(OpcStr, ".s32 \t$dst, $a, $b;"),
               [(set Int32Regs:$dst, (OpNode Int32Regs:$a, imm:$b))]>;
}

// Template for instructions which take three fp64 or fp32 args.  The
// instructions are named "<OpcStr>.f<Width>" (e.g. "min.f64").
//
// Also defines ftz (flush subnormal inputs and results to sign-preserving
// zero) variants for fp32 functions.
//
// This multiclass should be used for nodes that cannot be folded into FMAs.
// For nodes that can be folded into FMAs (i.e. adds and muls), use
// F3_fma_component.
multiclass F3<string OpcStr, SDNode OpNode> {
   def f64rr :
     NVPTXInst<(outs Float64Regs:$dst),
               (ins Float64Regs:$a, Float64Regs:$b),
               !strconcat(OpcStr, ".f64 \t$dst, $a, $b;"),
               [(set Float64Regs:$dst, (OpNode Float64Regs:$a, Float64Regs:$b))]>;
   def f64ri :
     NVPTXInst<(outs Float64Regs:$dst),
               (ins Float64Regs:$a, f64imm:$b),
               !strconcat(OpcStr, ".f64 \t$dst, $a, $b;"),
               [(set Float64Regs:$dst, (OpNode Float64Regs:$a, fpimm:$b))]>;
   def f32rr_ftz :
     NVPTXInst<(outs Float32Regs:$dst),
               (ins Float32Regs:$a, Float32Regs:$b),
               !strconcat(OpcStr, ".ftz.f32 \t$dst, $a, $b;"),
               [(set Float32Regs:$dst, (OpNode Float32Regs:$a, Float32Regs:$b))]>,
               Requires<[doF32FTZ]>;
   def f32ri_ftz :
     NVPTXInst<(outs Float32Regs:$dst),
               (ins Float32Regs:$a, f32imm:$b),
               !strconcat(OpcStr, ".ftz.f32 \t$dst, $a, $b;"),
               [(set Float32Regs:$dst, (OpNode Float32Regs:$a, fpimm:$b))]>,
               Requires<[doF32FTZ]>;
   def f32rr :
     NVPTXInst<(outs Float32Regs:$dst),
               (ins Float32Regs:$a, Float32Regs:$b),
               !strconcat(OpcStr, ".f32 \t$dst, $a, $b;"),
               [(set Float32Regs:$dst, (OpNode Float32Regs:$a, Float32Regs:$b))]>;
   def f32ri :
     NVPTXInst<(outs Float32Regs:$dst),
               (ins Float32Regs:$a, f32imm:$b),
               !strconcat(OpcStr, ".f32 \t$dst, $a, $b;"),
               [(set Float32Regs:$dst, (OpNode Float32Regs:$a, fpimm:$b))]>;
}

// Template for instructions which take three FP args.  The
// instructions are named "<OpcStr>.f<Width>" (e.g. "add.f64").
//
// Also defines ftz (flush subnormal inputs and results to sign-preserving
// zero) variants for fp32/fp16 functions.
//
// This multiclass should be used for nodes that can be folded to make fma ops.
// In this case, we use the ".rn" variant when FMA is disabled, as this behaves
// just like the non ".rn" op, but prevents ptxas from creating FMAs.
multiclass F3_fma_component<string OpcStr, SDNode OpNode> {
   def f64rr :
     NVPTXInst<(outs Float64Regs:$dst),
               (ins Float64Regs:$a, Float64Regs:$b),
               !strconcat(OpcStr, ".f64 \t$dst, $a, $b;"),
               [(set Float64Regs:$dst, (OpNode Float64Regs:$a, Float64Regs:$b))]>,
               Requires<[allowFMA]>;
   def f64ri :
     NVPTXInst<(outs Float64Regs:$dst),
               (ins Float64Regs:$a, f64imm:$b),
               !strconcat(OpcStr, ".f64 \t$dst, $a, $b;"),
               [(set Float64Regs:$dst, (OpNode Float64Regs:$a, fpimm:$b))]>,
               Requires<[allowFMA]>;
   def f32rr_ftz :
     NVPTXInst<(outs Float32Regs:$dst),
               (ins Float32Regs:$a, Float32Regs:$b),
               !strconcat(OpcStr, ".ftz.f32 \t$dst, $a, $b;"),
               [(set Float32Regs:$dst, (OpNode Float32Regs:$a, Float32Regs:$b))]>,
               Requires<[allowFMA, doF32FTZ]>;
   def f32ri_ftz :
     NVPTXInst<(outs Float32Regs:$dst),
               (ins Float32Regs:$a, f32imm:$b),
               !strconcat(OpcStr, ".ftz.f32 \t$dst, $a, $b;"),
               [(set Float32Regs:$dst, (OpNode Float32Regs:$a, fpimm:$b))]>,
               Requires<[allowFMA, doF32FTZ]>;
   def f32rr :
     NVPTXInst<(outs Float32Regs:$dst),
               (ins Float32Regs:$a, Float32Regs:$b),
               !strconcat(OpcStr, ".f32 \t$dst, $a, $b;"),
               [(set Float32Regs:$dst, (OpNode Float32Regs:$a, Float32Regs:$b))]>,
               Requires<[allowFMA]>;
   def f32ri :
     NVPTXInst<(outs Float32Regs:$dst),
               (ins Float32Regs:$a, f32imm:$b),
               !strconcat(OpcStr, ".f32 \t$dst, $a, $b;"),
               [(set Float32Regs:$dst, (OpNode Float32Regs:$a, fpimm:$b))]>,
               Requires<[allowFMA]>;

   def f16rr_ftz :
     NVPTXInst<(outs Float16Regs:$dst),
               (ins Float16Regs:$a, Float16Regs:$b),
               !strconcat(OpcStr, ".ftz.f16 \t$dst, $a, $b;"),
               [(set Float16Regs:$dst, (OpNode Float16Regs:$a, Float16Regs:$b))]>,
               Requires<[useFP16Math, allowFMA, doF32FTZ]>;
   def f16rr :
     NVPTXInst<(outs Float16Regs:$dst),
               (ins Float16Regs:$a, Float16Regs:$b),
               !strconcat(OpcStr, ".f16 \t$dst, $a, $b;"),
               [(set Float16Regs:$dst, (OpNode Float16Regs:$a, Float16Regs:$b))]>,
               Requires<[useFP16Math, allowFMA]>;

   def f16x2rr_ftz :
     NVPTXInst<(outs Float16x2Regs:$dst),
               (ins Float16x2Regs:$a, Float16x2Regs:$b),
               !strconcat(OpcStr, ".ftz.f16x2 \t$dst, $a, $b;"),
               [(set Float16x2Regs:$dst, (OpNode Float16x2Regs:$a, Float16x2Regs:$b))]>,
               Requires<[useFP16Math, allowFMA, doF32FTZ]>;
   def f16x2rr :
     NVPTXInst<(outs Float16x2Regs:$dst),
               (ins Float16x2Regs:$a, Float16x2Regs:$b),
               !strconcat(OpcStr, ".f16x2 \t$dst, $a, $b;"),
               [(set Float16x2Regs:$dst, (OpNode Float16x2Regs:$a, Float16x2Regs:$b))]>,
               Requires<[useFP16Math, allowFMA]>;

   // These have strange names so we don't perturb existing mir tests.
   def _rnf64rr :
     NVPTXInst<(outs Float64Regs:$dst),
               (ins Float64Regs:$a, Float64Regs:$b),
               !strconcat(OpcStr, ".rn.f64 \t$dst, $a, $b;"),
               [(set Float64Regs:$dst, (OpNode Float64Regs:$a, Float64Regs:$b))]>,
               Requires<[noFMA]>;
   def _rnf64ri :
     NVPTXInst<(outs Float64Regs:$dst),
               (ins Float64Regs:$a, f64imm:$b),
               !strconcat(OpcStr, ".rn.f64 \t$dst, $a, $b;"),
               [(set Float64Regs:$dst, (OpNode Float64Regs:$a, fpimm:$b))]>,
               Requires<[noFMA]>;
   def _rnf32rr_ftz :
     NVPTXInst<(outs Float32Regs:$dst),
               (ins Float32Regs:$a, Float32Regs:$b),
               !strconcat(OpcStr, ".rn.ftz.f32 \t$dst, $a, $b;"),
               [(set Float32Regs:$dst, (OpNode Float32Regs:$a, Float32Regs:$b))]>,
               Requires<[noFMA, doF32FTZ]>;
   def _rnf32ri_ftz :
     NVPTXInst<(outs Float32Regs:$dst),
               (ins Float32Regs:$a, f32imm:$b),
               !strconcat(OpcStr, ".rn.ftz.f32 \t$dst, $a, $b;"),
               [(set Float32Regs:$dst, (OpNode Float32Regs:$a, fpimm:$b))]>,
               Requires<[noFMA, doF32FTZ]>;
   def _rnf32rr :
     NVPTXInst<(outs Float32Regs:$dst),
               (ins Float32Regs:$a, Float32Regs:$b),
               !strconcat(OpcStr, ".rn.f32 \t$dst, $a, $b;"),
               [(set Float32Regs:$dst, (OpNode Float32Regs:$a, Float32Regs:$b))]>,
               Requires<[noFMA]>;
   def _rnf32ri :
     NVPTXInst<(outs Float32Regs:$dst),
               (ins Float32Regs:$a, f32imm:$b),
               !strconcat(OpcStr, ".rn.f32 \t$dst, $a, $b;"),
               [(set Float32Regs:$dst, (OpNode Float32Regs:$a, fpimm:$b))]>,
               Requires<[noFMA]>;
   def _rnf16rr_ftz :
     NVPTXInst<(outs Float16Regs:$dst),
               (ins Float16Regs:$a, Float16Regs:$b),
               !strconcat(OpcStr, ".rn.ftz.f16 \t$dst, $a, $b;"),
               [(set Float16Regs:$dst, (OpNode Float16Regs:$a, Float16Regs:$b))]>,
               Requires<[useFP16Math, noFMA, doF32FTZ]>;
   def _rnf16rr :
     NVPTXInst<(outs Float16Regs:$dst),
               (ins Float16Regs:$a, Float16Regs:$b),
               !strconcat(OpcStr, ".rn.f16 \t$dst, $a, $b;"),
               [(set Float16Regs:$dst, (OpNode Float16Regs:$a, Float16Regs:$b))]>,
               Requires<[useFP16Math, noFMA]>;
   def _rnf16x2rr_ftz :
     NVPTXInst<(outs Float16x2Regs:$dst),
               (ins Float16x2Regs:$a, Float16x2Regs:$b),
               !strconcat(OpcStr, ".rn.ftz.f16x2 \t$dst, $a, $b;"),
               [(set Float16x2Regs:$dst, (OpNode Float16x2Regs:$a, Float16x2Regs:$b))]>,
               Requires<[useFP16Math, noFMA, doF32FTZ]>;
   def _rnf16x2rr :
     NVPTXInst<(outs Float16x2Regs:$dst),
               (ins Float16x2Regs:$a, Float16x2Regs:$b),
               !strconcat(OpcStr, ".rn.f16x2 \t$dst, $a, $b;"),
               [(set Float16x2Regs:$dst, (OpNode Float16x2Regs:$a, Float16x2Regs:$b))]>,
               Requires<[useFP16Math, noFMA]>;
}

// Template for operations which take two f32 or f64 operands.  Provides three
// instructions: <OpcStr>.f64, <OpcStr>.f32, and <OpcStr>.ftz.f32 (flush
// subnormal inputs and results to zero).
multiclass F2<string OpcStr, SDNode OpNode> {
   def f64 :     NVPTXInst<(outs Float64Regs:$dst), (ins Float64Regs:$a),
                           !strconcat(OpcStr, ".f64 \t$dst, $a;"),
                           [(set Float64Regs:$dst, (OpNode Float64Regs:$a))]>;
   def f32_ftz : NVPTXInst<(outs Float32Regs:$dst), (ins Float32Regs:$a),
                           !strconcat(OpcStr, ".ftz.f32 \t$dst, $a;"),
                           [(set Float32Regs:$dst, (OpNode Float32Regs:$a))]>,
                           Requires<[doF32FTZ]>;
   def f32 :     NVPTXInst<(outs Float32Regs:$dst), (ins Float32Regs:$a),
                           !strconcat(OpcStr, ".f32 \t$dst, $a;"),
                           [(set Float32Regs:$dst, (OpNode Float32Regs:$a))]>;
}

//===----------------------------------------------------------------------===//
// NVPTX Instructions.
//===----------------------------------------------------------------------===//

//-----------------------------------
// Type Conversion
//-----------------------------------

let hasSideEffects = 0 in {
  // Generate a cvt to the given type from all possible types.  Each instance
  // takes a CvtMode immediate that defines the conversion mode to use.  It can
  // be CvtNONE to omit a conversion mode.
  multiclass CVT_FROM_ALL<string FromName, RegisterClass RC> {
    def _s8 :
      NVPTXInst<(outs RC:$dst),
                (ins Int16Regs:$src, CvtMode:$mode),
                !strconcat("cvt${mode:base}${mode:ftz}${mode:sat}.",
                FromName, ".s8 \t$dst, $src;"), []>;
    def _u8 :
      NVPTXInst<(outs RC:$dst),
                (ins Int16Regs:$src, CvtMode:$mode),
                !strconcat("cvt${mode:base}${mode:ftz}${mode:sat}.",
                FromName, ".u8 \t$dst, $src;"), []>;
    def _s16 :
      NVPTXInst<(outs RC:$dst),
                (ins Int16Regs:$src, CvtMode:$mode),
                !strconcat("cvt${mode:base}${mode:ftz}${mode:sat}.",
                FromName, ".s16 \t$dst, $src;"), []>;
    def _u16 :
      NVPTXInst<(outs RC:$dst),
                (ins Int16Regs:$src, CvtMode:$mode),
                !strconcat("cvt${mode:base}${mode:ftz}${mode:sat}.",
                FromName, ".u16 \t$dst, $src;"), []>;
    def _s32 :
      NVPTXInst<(outs RC:$dst),
                (ins Int32Regs:$src, CvtMode:$mode),
                !strconcat("cvt${mode:base}${mode:ftz}${mode:sat}.",
                FromName, ".s32 \t$dst, $src;"), []>;
    def _u32 :
      NVPTXInst<(outs RC:$dst),
                (ins Int32Regs:$src, CvtMode:$mode),
                !strconcat("cvt${mode:base}${mode:ftz}${mode:sat}.",
                FromName, ".u32 \t$dst, $src;"), []>;
    def _s64 :
      NVPTXInst<(outs RC:$dst),
                (ins Int64Regs:$src, CvtMode:$mode),
                !strconcat("cvt${mode:base}${mode:ftz}${mode:sat}.",
                FromName, ".s64 \t$dst, $src;"), []>;
    def _u64 :
      NVPTXInst<(outs RC:$dst),
                (ins Int64Regs:$src, CvtMode:$mode),
                !strconcat("cvt${mode:base}${mode:ftz}${mode:sat}.",
                FromName, ".u64 \t$dst, $src;"), []>;
    def _f16 :
      NVPTXInst<(outs RC:$dst),
                (ins Float16Regs:$src, CvtMode:$mode),
                !strconcat("cvt${mode:base}${mode:ftz}${mode:sat}.",
                FromName, ".f16 \t$dst, $src;"), []>;
    def _f32 :
      NVPTXInst<(outs RC:$dst),
                (ins Float32Regs:$src, CvtMode:$mode),
                !strconcat("cvt${mode:base}${mode:ftz}${mode:sat}.",
                FromName, ".f32 \t$dst, $src;"), []>;
    def _f64 :
      NVPTXInst<(outs RC:$dst),
                (ins Float64Regs:$src, CvtMode:$mode),
                !strconcat("cvt${mode:base}${mode:ftz}${mode:sat}.",
                FromName, ".f64 \t$dst, $src;"), []>;
  }

  // Generate cvts from all types to all types.
  defm CVT_s8  : CVT_FROM_ALL<"s8",  Int16Regs>;
  defm CVT_u8  : CVT_FROM_ALL<"u8",  Int16Regs>;
  defm CVT_s16 : CVT_FROM_ALL<"s16", Int16Regs>;
  defm CVT_u16 : CVT_FROM_ALL<"u16", Int16Regs>;
  defm CVT_s32 : CVT_FROM_ALL<"s32", Int32Regs>;
  defm CVT_u32 : CVT_FROM_ALL<"u32", Int32Regs>;
  defm CVT_s64 : CVT_FROM_ALL<"s64", Int64Regs>;
  defm CVT_u64 : CVT_FROM_ALL<"u64", Int64Regs>;
  defm CVT_f16 : CVT_FROM_ALL<"f16", Float16Regs>;
  defm CVT_f32 : CVT_FROM_ALL<"f32", Float32Regs>;
  defm CVT_f64 : CVT_FROM_ALL<"f64", Float64Regs>;

  // These cvts are different from those above: The source and dest registers
  // are of the same type.
  def CVT_INREG_s16_s8 :  NVPTXInst<(outs Int16Regs:$dst), (ins Int16Regs:$src),
                                    "cvt.s16.s8 \t$dst, $src;", []>;
  def CVT_INREG_s32_s8 :  NVPTXInst<(outs Int32Regs:$dst), (ins Int32Regs:$src),
                                    "cvt.s32.s8 \t$dst, $src;", []>;
  def CVT_INREG_s32_s16 : NVPTXInst<(outs Int32Regs:$dst), (ins Int32Regs:$src),
                                    "cvt.s32.s16 \t$dst, $src;", []>;
  def CVT_INREG_s64_s8 :  NVPTXInst<(outs Int64Regs:$dst), (ins Int64Regs:$src),
                                    "cvt.s64.s8 \t$dst, $src;", []>;
  def CVT_INREG_s64_s16 : NVPTXInst<(outs Int64Regs:$dst), (ins Int64Regs:$src),
                                    "cvt.s64.s16 \t$dst, $src;", []>;
  def CVT_INREG_s64_s32 : NVPTXInst<(outs Int64Regs:$dst), (ins Int64Regs:$src),
                                    "cvt.s64.s32 \t$dst, $src;", []>;
}

//-----------------------------------
// Integer Arithmetic
//-----------------------------------

// Template for xor masquerading as int1 arithmetic.
multiclass ADD_SUB_i1<SDNode OpNode> {
   def _rr: NVPTXInst<(outs Int1Regs:$dst), (ins Int1Regs:$a, Int1Regs:$b),
                      "xor.pred \t$dst, $a, $b;",
                      [(set Int1Regs:$dst, (OpNode Int1Regs:$a, Int1Regs:$b))]>;
   def _ri: NVPTXInst<(outs Int1Regs:$dst), (ins Int1Regs:$a, i1imm:$b),
                      "xor.pred \t$dst, $a, $b;",
                      [(set Int1Regs:$dst, (OpNode Int1Regs:$a, (imm):$b))]>;
}

// int1 addition and subtraction are both just xor.
defm ADD_i1 : ADD_SUB_i1<add>;
defm SUB_i1 : ADD_SUB_i1<sub>;

// int16, int32, and int64 signed addition.  Since nvptx is 2's complement, we
// also use these for unsigned arithmetic.
defm ADD : I3<"add.s", add>;
defm SUB : I3<"sub.s", sub>;

// int32 addition and subtraction with carry-out.
// FIXME: PTX 4.3 adds a 64-bit add.cc (and maybe also 64-bit addc.cc?).
defm ADDCC : ADD_SUB_INT_32<"add.cc", addc>;
defm SUBCC : ADD_SUB_INT_32<"sub.cc", subc>;

// int32 addition and subtraction with carry-in and carry-out.
defm ADDCCC : ADD_SUB_INT_32<"addc.cc", adde>;
defm SUBCCC : ADD_SUB_INT_32<"subc.cc", sube>;

defm MULT : I3<"mul.lo.s", mul>;

defm MULTHS : I3<"mul.hi.s", mulhs>;
defm MULTHU : I3<"mul.hi.u", mulhu>;

defm SDIV : I3<"div.s", sdiv>;
defm UDIV : I3<"div.u", udiv>;

// The ri versions of rem.s and rem.u won't be selected; DAGCombiner::visitSREM
// will lower it.
defm SREM : I3<"rem.s", srem>;
defm UREM : I3<"rem.u", urem>;

// Integer absolute value.  NumBits should be one minus the bit width of RC.
// This idiom implements the algorithm at
// http://graphics.stanford.edu/~seander/bithacks.html#IntegerAbs.
multiclass ABS<RegisterClass RC, string SizeName> {
  def : NVPTXInst<(outs RC:$dst), (ins RC:$a),
                  !strconcat("abs", SizeName, " \t$dst, $a;"),
                  [(set RC:$dst, (abs RC:$a))]>;
}
defm ABS_16 : ABS<Int16Regs, ".s16">;
defm ABS_32 : ABS<Int32Regs, ".s32">;
defm ABS_64 : ABS<Int64Regs, ".s64">;

// Integer min/max.
defm SMAX : I3<"max.s", smax>;
defm UMAX : I3<"max.u", umax>;
defm SMIN : I3<"min.s", smin>;
defm UMIN : I3<"min.u", umin>;

//
// Wide multiplication
//
def MULWIDES64 :
  NVPTXInst<(outs Int64Regs:$dst), (ins Int32Regs:$a, Int32Regs:$b),
            "mul.wide.s32 \t$dst, $a, $b;", []>;
def MULWIDES64Imm :
  NVPTXInst<(outs Int64Regs:$dst), (ins Int32Regs:$a, i32imm:$b),
            "mul.wide.s32 \t$dst, $a, $b;", []>;
def MULWIDES64Imm64 :
  NVPTXInst<(outs Int64Regs:$dst), (ins Int32Regs:$a, i64imm:$b),
            "mul.wide.s32 \t$dst, $a, $b;", []>;

def MULWIDEU64 :
  NVPTXInst<(outs Int64Regs:$dst), (ins Int32Regs:$a, Int32Regs:$b),
            "mul.wide.u32 \t$dst, $a, $b;", []>;
def MULWIDEU64Imm :
  NVPTXInst<(outs Int64Regs:$dst), (ins Int32Regs:$a, i32imm:$b),
            "mul.wide.u32 \t$dst, $a, $b;", []>;
def MULWIDEU64Imm64 :
  NVPTXInst<(outs Int64Regs:$dst), (ins Int32Regs:$a, i64imm:$b),
            "mul.wide.u32 \t$dst, $a, $b;", []>;

def MULWIDES32 :
  NVPTXInst<(outs Int32Regs:$dst), (ins Int16Regs:$a, Int16Regs:$b),
            "mul.wide.s16 \t$dst, $a, $b;", []>;
def MULWIDES32Imm :
  NVPTXInst<(outs Int32Regs:$dst), (ins Int16Regs:$a, i16imm:$b),
            "mul.wide.s16 \t$dst, $a, $b;", []>;
def MULWIDES32Imm32 :
  NVPTXInst<(outs Int32Regs:$dst), (ins Int16Regs:$a, i32imm:$b),
            "mul.wide.s16 \t$dst, $a, $b;", []>;

def MULWIDEU32 :
  NVPTXInst<(outs Int32Regs:$dst), (ins Int16Regs:$a, Int16Regs:$b),
            "mul.wide.u16 \t$dst, $a, $b;", []>;
def MULWIDEU32Imm :
  NVPTXInst<(outs Int32Regs:$dst), (ins Int16Regs:$a, i16imm:$b),
            "mul.wide.u16 \t$dst, $a, $b;", []>;
def MULWIDEU32Imm32 :
  NVPTXInst<(outs Int32Regs:$dst), (ins Int16Regs:$a, i32imm:$b),
            "mul.wide.u16 \t$dst, $a, $b;", []>;

def SDTMulWide : SDTypeProfile<1, 2, [SDTCisSameAs<1, 2>]>;
def mul_wide_signed : SDNode<"NVPTXISD::MUL_WIDE_SIGNED", SDTMulWide>;
def mul_wide_unsigned : SDNode<"NVPTXISD::MUL_WIDE_UNSIGNED", SDTMulWide>;

// Matchers for signed, unsigned mul.wide ISD nodes.
def : Pat<(i32 (mul_wide_signed Int16Regs:$a, Int16Regs:$b)),
          (MULWIDES32 Int16Regs:$a, Int16Regs:$b)>,
      Requires<[doMulWide]>;
def : Pat<(i32 (mul_wide_signed Int16Regs:$a, imm:$b)),
          (MULWIDES32Imm Int16Regs:$a, imm:$b)>,
      Requires<[doMulWide]>;
def : Pat<(i32 (mul_wide_unsigned Int16Regs:$a, Int16Regs:$b)),
          (MULWIDEU32 Int16Regs:$a, Int16Regs:$b)>,
      Requires<[doMulWide]>;
def : Pat<(i32 (mul_wide_unsigned Int16Regs:$a, imm:$b)),
          (MULWIDEU32Imm Int16Regs:$a, imm:$b)>,
      Requires<[doMulWide]>;

def : Pat<(i64 (mul_wide_signed Int32Regs:$a, Int32Regs:$b)),
          (MULWIDES64 Int32Regs:$a, Int32Regs:$b)>,
      Requires<[doMulWide]>;
def : Pat<(i64 (mul_wide_signed Int32Regs:$a, imm:$b)),
          (MULWIDES64Imm Int32Regs:$a, imm:$b)>,
      Requires<[doMulWide]>;
def : Pat<(i64 (mul_wide_unsigned Int32Regs:$a, Int32Regs:$b)),
          (MULWIDEU64 Int32Regs:$a, Int32Regs:$b)>,
      Requires<[doMulWide]>;
def : Pat<(i64 (mul_wide_unsigned Int32Regs:$a, imm:$b)),
          (MULWIDEU64Imm Int32Regs:$a, imm:$b)>,
      Requires<[doMulWide]>;

// Predicates used for converting some patterns to mul.wide.
def SInt32Const : PatLeaf<(imm), [{
  const APInt &v = N->getAPIntValue();
  return v.isSignedIntN(32);
}]>;

def UInt32Const : PatLeaf<(imm), [{
  const APInt &v = N->getAPIntValue();
  return v.isIntN(32);
}]>;

def SInt16Const : PatLeaf<(imm), [{
  const APInt &v = N->getAPIntValue();
  return v.isSignedIntN(16);
}]>;

def UInt16Const : PatLeaf<(imm), [{
  const APInt &v = N->getAPIntValue();
  return v.isIntN(16);
}]>;

def Int5Const : PatLeaf<(imm), [{
  // Check if 0 <= v < 32; only then will the result of (x << v) be an int32.
  const APInt &v = N->getAPIntValue();
  return v.sge(0) && v.slt(32);
}]>;

def Int4Const : PatLeaf<(imm), [{
  // Check if 0 <= v < 16; only then will the result of (x << v) be an int16.
  const APInt &v = N->getAPIntValue();
  return v.sge(0) && v.slt(16);
}]>;

def SHL2MUL32 : SDNodeXForm<imm, [{
  const APInt &v = N->getAPIntValue();
  APInt temp(32, 1);
  return CurDAG->getTargetConstant(temp.shl(v), SDLoc(N), MVT::i32);
}]>;

def SHL2MUL16 : SDNodeXForm<imm, [{
  const APInt &v = N->getAPIntValue();
  APInt temp(16, 1);
  return CurDAG->getTargetConstant(temp.shl(v), SDLoc(N), MVT::i16);
}]>;

// Convert "sign/zero-extend, then shift left by an immediate" to mul.wide.
def : Pat<(shl (sext Int32Regs:$a), (i32 Int5Const:$b)),
          (MULWIDES64Imm Int32Regs:$a, (SHL2MUL32 node:$b))>,
      Requires<[doMulWide]>;
def : Pat<(shl (zext Int32Regs:$a), (i32 Int5Const:$b)),
          (MULWIDEU64Imm Int32Regs:$a, (SHL2MUL32 node:$b))>,
      Requires<[doMulWide]>;

def : Pat<(shl (sext Int16Regs:$a), (i16 Int4Const:$b)),
          (MULWIDES32Imm Int16Regs:$a, (SHL2MUL16 node:$b))>,
      Requires<[doMulWide]>;
def : Pat<(shl (zext Int16Regs:$a), (i16 Int4Const:$b)),
          (MULWIDEU32Imm Int16Regs:$a, (SHL2MUL16 node:$b))>,
      Requires<[doMulWide]>;

// Convert "sign/zero-extend then multiply" to mul.wide.
def : Pat<(mul (sext Int32Regs:$a), (sext Int32Regs:$b)),
          (MULWIDES64 Int32Regs:$a, Int32Regs:$b)>,
      Requires<[doMulWide]>;
def : Pat<(mul (sext Int32Regs:$a), (i64 SInt32Const:$b)),
          (MULWIDES64Imm64 Int32Regs:$a, (i64 SInt32Const:$b))>,
      Requires<[doMulWide]>;

def : Pat<(mul (zext Int32Regs:$a), (zext Int32Regs:$b)),
          (MULWIDEU64 Int32Regs:$a, Int32Regs:$b)>,
      Requires<[doMulWide]>;
def : Pat<(mul (zext Int32Regs:$a), (i64 UInt32Const:$b)),
          (MULWIDEU64Imm64 Int32Regs:$a, (i64 UInt32Const:$b))>,
      Requires<[doMulWide]>;

def : Pat<(mul (sext Int16Regs:$a), (sext Int16Regs:$b)),
          (MULWIDES32 Int16Regs:$a, Int16Regs:$b)>,
      Requires<[doMulWide]>;
def : Pat<(mul (sext Int16Regs:$a), (i32 SInt16Const:$b)),
          (MULWIDES32Imm32 Int16Regs:$a, (i32 SInt16Const:$b))>,
      Requires<[doMulWide]>;

def : Pat<(mul (zext Int16Regs:$a), (zext Int16Regs:$b)),
          (MULWIDEU32 Int16Regs:$a, Int16Regs:$b)>,
      Requires<[doMulWide]>;
def : Pat<(mul (zext Int16Regs:$a), (i32 UInt16Const:$b)),
          (MULWIDEU32Imm32 Int16Regs:$a, (i32 UInt16Const:$b))>,
      Requires<[doMulWide]>;

//
// Integer multiply-add
//
def SDTIMAD :
  SDTypeProfile<1, 3, [SDTCisSameAs<0, 1>, SDTCisInt<0>, SDTCisInt<2>,
                       SDTCisSameAs<0, 2>, SDTCisSameAs<0, 3>]>;
def imad : SDNode<"NVPTXISD::IMAD", SDTIMAD>;

def MAD16rrr :
  NVPTXInst<(outs Int16Regs:$dst),
            (ins Int16Regs:$a, Int16Regs:$b, Int16Regs:$c),
            "mad.lo.s16 \t$dst, $a, $b, $c;",
            [(set Int16Regs:$dst, (imad Int16Regs:$a, Int16Regs:$b, Int16Regs:$c))]>;
def MAD16rri :
  NVPTXInst<(outs Int16Regs:$dst),
            (ins Int16Regs:$a, Int16Regs:$b, i16imm:$c),
            "mad.lo.s16 \t$dst, $a, $b, $c;",
            [(set Int16Regs:$dst, (imad Int16Regs:$a, Int16Regs:$b, imm:$c))]>;
def MAD16rir :
  NVPTXInst<(outs Int16Regs:$dst),
            (ins Int16Regs:$a, i16imm:$b, Int16Regs:$c),
            "mad.lo.s16 \t$dst, $a, $b, $c;",
            [(set Int16Regs:$dst, (imad Int16Regs:$a, imm:$b, Int16Regs:$c))]>;
def MAD16rii :
  NVPTXInst<(outs Int16Regs:$dst),
            (ins Int16Regs:$a, i16imm:$b, i16imm:$c),
            "mad.lo.s16 \t$dst, $a, $b, $c;",
            [(set Int16Regs:$dst, (imad Int16Regs:$a, imm:$b, imm:$c))]>;

def MAD32rrr :
  NVPTXInst<(outs Int32Regs:$dst),
            (ins Int32Regs:$a, Int32Regs:$b, Int32Regs:$c),
            "mad.lo.s32 \t$dst, $a, $b, $c;",
            [(set Int32Regs:$dst, (imad Int32Regs:$a, Int32Regs:$b, Int32Regs:$c))]>;
def MAD32rri :
  NVPTXInst<(outs Int32Regs:$dst),
            (ins Int32Regs:$a, Int32Regs:$b, i32imm:$c),
            "mad.lo.s32 \t$dst, $a, $b, $c;",
            [(set Int32Regs:$dst, (imad Int32Regs:$a, Int32Regs:$b, imm:$c))]>;
def MAD32rir :
  NVPTXInst<(outs Int32Regs:$dst),
            (ins Int32Regs:$a, i32imm:$b, Int32Regs:$c),
            "mad.lo.s32 \t$dst, $a, $b, $c;",
            [(set Int32Regs:$dst, (imad Int32Regs:$a, imm:$b, Int32Regs:$c))]>;
def MAD32rii :
  NVPTXInst<(outs Int32Regs:$dst),
            (ins Int32Regs:$a, i32imm:$b, i32imm:$c),
            "mad.lo.s32 \t$dst, $a, $b, $c;",
            [(set Int32Regs:$dst, (imad Int32Regs:$a, imm:$b, imm:$c))]>;

def MAD64rrr :
  NVPTXInst<(outs Int64Regs:$dst),
            (ins Int64Regs:$a, Int64Regs:$b, Int64Regs:$c),
            "mad.lo.s64 \t$dst, $a, $b, $c;",
            [(set Int64Regs:$dst, (imad Int64Regs:$a, Int64Regs:$b, Int64Regs:$c))]>;
def MAD64rri :
  NVPTXInst<(outs Int64Regs:$dst),
            (ins Int64Regs:$a, Int64Regs:$b, i64imm:$c),
            "mad.lo.s64 \t$dst, $a, $b, $c;",
            [(set Int64Regs:$dst, (imad Int64Regs:$a, Int64Regs:$b, imm:$c))]>;
def MAD64rir :
  NVPTXInst<(outs Int64Regs:$dst),
            (ins Int64Regs:$a, i64imm:$b, Int64Regs:$c),
            "mad.lo.s64 \t$dst, $a, $b, $c;",
            [(set Int64Regs:$dst, (imad Int64Regs:$a, imm:$b, Int64Regs:$c))]>;
def MAD64rii :
  NVPTXInst<(outs Int64Regs:$dst),
            (ins Int64Regs:$a, i64imm:$b, i64imm:$c),
            "mad.lo.s64 \t$dst, $a, $b, $c;",
            [(set Int64Regs:$dst, (imad Int64Regs:$a, imm:$b, imm:$c))]>;

def INEG16 :
  NVPTXInst<(outs Int16Regs:$dst), (ins Int16Regs:$src),
            "neg.s16 \t$dst, $src;",
            [(set Int16Regs:$dst, (ineg Int16Regs:$src))]>;
def INEG32 :
  NVPTXInst<(outs Int32Regs:$dst), (ins Int32Regs:$src),
            "neg.s32 \t$dst, $src;",
            [(set Int32Regs:$dst, (ineg Int32Regs:$src))]>;
def INEG64 :
  NVPTXInst<(outs Int64Regs:$dst), (ins Int64Regs:$src),
            "neg.s64 \t$dst, $src;",
            [(set Int64Regs:$dst, (ineg Int64Regs:$src))]>;

//-----------------------------------
// Floating Point Arithmetic
//-----------------------------------

// Constant 1.0f
def FloatConst1 : PatLeaf<(fpimm), [{
  return &N->getValueAPF().getSemantics() == &llvm::APFloat::IEEEsingle() &&
         N->getValueAPF().convertToFloat() == 1.0f;
}]>;
// Constant 1.0 (double)
def DoubleConst1 : PatLeaf<(fpimm), [{
  return &N->getValueAPF().getSemantics() == &llvm::APFloat::IEEEdouble() &&
         N->getValueAPF().convertToDouble() == 1.0;
}]>;

// Loads FP16 constant into a register.
//
// ptxas does not have hex representation for fp16, so we can't use
// fp16 immediate values in .f16 instructions. Instead we have to load
// the constant into a register using mov.b16.
def LOAD_CONST_F16 :
  NVPTXInst<(outs Float16Regs:$dst), (ins f16imm:$a),
            "mov.b16 \t$dst, $a;", []>;

defm FADD : F3_fma_component<"add", fadd>;
defm FSUB : F3_fma_component<"sub", fsub>;
defm FMUL : F3_fma_component<"mul", fmul>;

defm FMIN : F3<"min", fminnum>;
defm FMAX : F3<"max", fmaxnum>;

defm FABS  : F2<"abs", fabs>;
defm FNEG  : F2<"neg", fneg>;
defm FSQRT : F2<"sqrt.rn", fsqrt>;

//
// F64 division
//
def FDIV641r :
  NVPTXInst<(outs Float64Regs:$dst),
            (ins f64imm:$a, Float64Regs:$b),
            "rcp.rn.f64 \t$dst, $b;",
            [(set Float64Regs:$dst, (fdiv DoubleConst1:$a, Float64Regs:$b))]>;
def FDIV64rr :
  NVPTXInst<(outs Float64Regs:$dst),
            (ins Float64Regs:$a, Float64Regs:$b),
            "div.rn.f64 \t$dst, $a, $b;",
            [(set Float64Regs:$dst, (fdiv Float64Regs:$a, Float64Regs:$b))]>;
def FDIV64ri :
  NVPTXInst<(outs Float64Regs:$dst),
            (ins Float64Regs:$a, f64imm:$b),
            "div.rn.f64 \t$dst, $a, $b;",
            [(set Float64Regs:$dst, (fdiv Float64Regs:$a, fpimm:$b))]>;

//
// F32 Approximate reciprocal
//
def FDIV321r_ftz :
  NVPTXInst<(outs Float32Regs:$dst),
            (ins f32imm:$a, Float32Regs:$b),
            "rcp.approx.ftz.f32 \t$dst, $b;",
            [(set Float32Regs:$dst, (fdiv FloatConst1:$a, Float32Regs:$b))]>,
            Requires<[do_DIVF32_APPROX, doF32FTZ]>;
def FDIV321r :
  NVPTXInst<(outs Float32Regs:$dst),
            (ins f32imm:$a, Float32Regs:$b),
            "rcp.approx.f32 \t$dst, $b;",
            [(set Float32Regs:$dst, (fdiv FloatConst1:$a, Float32Regs:$b))]>,
            Requires<[do_DIVF32_APPROX]>;
//
// F32 Approximate division
//
def FDIV32approxrr_ftz :
  NVPTXInst<(outs Float32Regs:$dst),
            (ins Float32Regs:$a, Float32Regs:$b),
            "div.approx.ftz.f32 \t$dst, $a, $b;",
            [(set Float32Regs:$dst, (fdiv Float32Regs:$a, Float32Regs:$b))]>,
            Requires<[do_DIVF32_APPROX, doF32FTZ]>;
def FDIV32approxri_ftz :
  NVPTXInst<(outs Float32Regs:$dst),
            (ins Float32Regs:$a, f32imm:$b),
            "div.approx.ftz.f32 \t$dst, $a, $b;",
            [(set Float32Regs:$dst, (fdiv Float32Regs:$a, fpimm:$b))]>,
            Requires<[do_DIVF32_APPROX, doF32FTZ]>;
def FDIV32approxrr :
  NVPTXInst<(outs Float32Regs:$dst),
            (ins Float32Regs:$a, Float32Regs:$b),
            "div.approx.f32 \t$dst, $a, $b;",
            [(set Float32Regs:$dst, (fdiv Float32Regs:$a, Float32Regs:$b))]>,
            Requires<[do_DIVF32_APPROX]>;
def FDIV32approxri :
  NVPTXInst<(outs Float32Regs:$dst),
            (ins Float32Regs:$a, f32imm:$b),
            "div.approx.f32 \t$dst, $a, $b;",
            [(set Float32Regs:$dst, (fdiv Float32Regs:$a, fpimm:$b))]>,
            Requires<[do_DIVF32_APPROX]>;
//
// F32 Semi-accurate reciprocal
//
// rcp.approx gives the same result as div.full(1.0f, a) and is faster.
//
def FDIV321r_approx_ftz :
  NVPTXInst<(outs Float32Regs:$dst),
            (ins f32imm:$a, Float32Regs:$b),
            "rcp.approx.ftz.f32 \t$dst, $b;",
            [(set Float32Regs:$dst, (fdiv FloatConst1:$a, Float32Regs:$b))]>,
            Requires<[do_DIVF32_FULL, doF32FTZ]>;
def FDIV321r_approx :
  NVPTXInst<(outs Float32Regs:$dst),
            (ins f32imm:$a, Float32Regs:$b),
            "rcp.approx.f32 \t$dst, $b;",
            [(set Float32Regs:$dst, (fdiv FloatConst1:$a, Float32Regs:$b))]>,
            Requires<[do_DIVF32_FULL]>;
//
// F32 Semi-accurate division
//
def FDIV32rr_ftz :
  NVPTXInst<(outs Float32Regs:$dst),
            (ins Float32Regs:$a, Float32Regs:$b),
            "div.full.ftz.f32 \t$dst, $a, $b;",
            [(set Float32Regs:$dst, (fdiv Float32Regs:$a, Float32Regs:$b))]>,
            Requires<[do_DIVF32_FULL, doF32FTZ]>;
def FDIV32ri_ftz :
  NVPTXInst<(outs Float32Regs:$dst),
            (ins Float32Regs:$a, f32imm:$b),
            "div.full.ftz.f32 \t$dst, $a, $b;",
            [(set Float32Regs:$dst, (fdiv Float32Regs:$a, fpimm:$b))]>,
            Requires<[do_DIVF32_FULL, doF32FTZ]>;
def FDIV32rr :
  NVPTXInst<(outs Float32Regs:$dst),
            (ins Float32Regs:$a, Float32Regs:$b),
            "div.full.f32 \t$dst, $a, $b;",
            [(set Float32Regs:$dst, (fdiv Float32Regs:$a, Float32Regs:$b))]>,
            Requires<[do_DIVF32_FULL]>;
def FDIV32ri :
  NVPTXInst<(outs Float32Regs:$dst),
            (ins Float32Regs:$a, f32imm:$b),
            "div.full.f32 \t$dst, $a, $b;",
            [(set Float32Regs:$dst, (fdiv Float32Regs:$a, fpimm:$b))]>,
            Requires<[do_DIVF32_FULL]>;
//
// F32 Accurate reciprocal
//
def FDIV321r_prec_ftz :
  NVPTXInst<(outs Float32Regs:$dst),
            (ins f32imm:$a, Float32Regs:$b),
            "rcp.rn.ftz.f32 \t$dst, $b;",
            [(set Float32Regs:$dst, (fdiv FloatConst1:$a, Float32Regs:$b))]>,
            Requires<[doF32FTZ]>;
def FDIV321r_prec :
  NVPTXInst<(outs Float32Regs:$dst),
            (ins f32imm:$a, Float32Regs:$b),
            "rcp.rn.f32 \t$dst, $b;",
            [(set Float32Regs:$dst, (fdiv FloatConst1:$a, Float32Regs:$b))]>;
//
// F32 Accurate division
//
def FDIV32rr_prec_ftz :
  NVPTXInst<(outs Float32Regs:$dst),
            (ins Float32Regs:$a, Float32Regs:$b),
            "div.rn.ftz.f32 \t$dst, $a, $b;",
            [(set Float32Regs:$dst, (fdiv Float32Regs:$a, Float32Regs:$b))]>,
            Requires<[doF32FTZ]>;
def FDIV32ri_prec_ftz :
  NVPTXInst<(outs Float32Regs:$dst),
            (ins Float32Regs:$a, f32imm:$b),
            "div.rn.ftz.f32 \t$dst, $a, $b;",
            [(set Float32Regs:$dst, (fdiv Float32Regs:$a, fpimm:$b))]>,
            Requires<[doF32FTZ]>;
def FDIV32rr_prec :
  NVPTXInst<(outs Float32Regs:$dst),
            (ins Float32Regs:$a, Float32Regs:$b),
            "div.rn.f32 \t$dst, $a, $b;",
            [(set Float32Regs:$dst, (fdiv Float32Regs:$a, Float32Regs:$b))]>;
def FDIV32ri_prec :
  NVPTXInst<(outs Float32Regs:$dst),
            (ins Float32Regs:$a, f32imm:$b),
            "div.rn.f32 \t$dst, $a, $b;",
            [(set Float32Regs:$dst, (fdiv Float32Regs:$a, fpimm:$b))]>;

//
// FMA
//

multiclass FMA<string OpcStr, RegisterClass RC, Operand ImmCls, Predicate Pred> {
   def rrr : NVPTXInst<(outs RC:$dst), (ins RC:$a, RC:$b, RC:$c),
                       !strconcat(OpcStr, " \t$dst, $a, $b, $c;"),
                       [(set RC:$dst, (fma RC:$a, RC:$b, RC:$c))]>,
                       Requires<[Pred]>;
   def rri : NVPTXInst<(outs RC:$dst),
                       (ins RC:$a, RC:$b, ImmCls:$c),
                       !strconcat(OpcStr, " \t$dst, $a, $b, $c;"),
                       [(set RC:$dst, (fma RC:$a, RC:$b, fpimm:$c))]>,
                       Requires<[Pred]>;
   def rir : NVPTXInst<(outs RC:$dst),
                       (ins RC:$a, ImmCls:$b, RC:$c),
                       !strconcat(OpcStr, " \t$dst, $a, $b, $c;"),
                       [(set RC:$dst, (fma RC:$a, fpimm:$b, RC:$c))]>,
                       Requires<[Pred]>;
   def rii : NVPTXInst<(outs RC:$dst),
                       (ins RC:$a, ImmCls:$b, ImmCls:$c),
                       !strconcat(OpcStr, " \t$dst, $a, $b, $c;"),
                       [(set RC:$dst, (fma RC:$a, fpimm:$b, fpimm:$c))]>,
                       Requires<[Pred]>;
}

multiclass FMA_F16<string OpcStr, RegisterClass RC, Predicate Pred> {
   def rrr : NVPTXInst<(outs RC:$dst), (ins RC:$a, RC:$b, RC:$c),
                       !strconcat(OpcStr, " \t$dst, $a, $b, $c;"),
                       [(set RC:$dst, (fma RC:$a, RC:$b, RC:$c))]>,
                       Requires<[useFP16Math, Pred]>;
}

defm FMA16_ftz : FMA_F16<"fma.rn.ftz.f16", Float16Regs, doF32FTZ>;
defm FMA16     : FMA_F16<"fma.rn.f16", Float16Regs, true>;
defm FMA16x2_ftz : FMA_F16<"fma.rn.ftz.f16x2", Float16x2Regs, doF32FTZ>;
defm FMA16x2     : FMA_F16<"fma.rn.f16x2", Float16x2Regs, true>;
defm FMA32_ftz : FMA<"fma.rn.ftz.f32", Float32Regs, f32imm, doF32FTZ>;
defm FMA32     : FMA<"fma.rn.f32", Float32Regs, f32imm, true>;
defm FMA64     : FMA<"fma.rn.f64", Float64Regs, f64imm, true>;

// sin/cos
def SINF:  NVPTXInst<(outs Float32Regs:$dst), (ins Float32Regs:$src),
                      "sin.approx.f32 \t$dst, $src;",
                      [(set Float32Regs:$dst, (fsin Float32Regs:$src))]>,
                      Requires<[allowUnsafeFPMath]>;
def COSF:  NVPTXInst<(outs Float32Regs:$dst), (ins Float32Regs:$src),
                      "cos.approx.f32 \t$dst, $src;",
                      [(set Float32Regs:$dst, (fcos Float32Regs:$src))]>,
                      Requires<[allowUnsafeFPMath]>;

// Lower (frem x, y) into (sub x, (mul (floor (div x, y)) y)),
// i.e. "poor man's fmod()"

// frem - f32 FTZ
def : Pat<(frem Float32Regs:$x, Float32Regs:$y),
          (FSUBf32rr_ftz Float32Regs:$x, (FMULf32rr_ftz (CVT_f32_f32
            (FDIV32rr_prec_ftz Float32Regs:$x, Float32Regs:$y), CvtRMI_FTZ),
             Float32Regs:$y))>,
          Requires<[doF32FTZ]>;
def : Pat<(frem Float32Regs:$x, fpimm:$y),
          (FSUBf32rr_ftz Float32Regs:$x, (FMULf32ri_ftz (CVT_f32_f32
            (FDIV32ri_prec_ftz Float32Regs:$x, fpimm:$y), CvtRMI_FTZ),
             fpimm:$y))>,
          Requires<[doF32FTZ]>;

// frem - f32
def : Pat<(frem Float32Regs:$x, Float32Regs:$y),
          (FSUBf32rr Float32Regs:$x, (FMULf32rr (CVT_f32_f32
            (FDIV32rr_prec Float32Regs:$x, Float32Regs:$y), CvtRMI),
             Float32Regs:$y))>;
def : Pat<(frem Float32Regs:$x, fpimm:$y),
          (FSUBf32rr Float32Regs:$x, (FMULf32ri (CVT_f32_f32
            (FDIV32ri_prec Float32Regs:$x, fpimm:$y), CvtRMI),
             fpimm:$y))>;

// frem - f64
def : Pat<(frem Float64Regs:$x, Float64Regs:$y),
          (FSUBf64rr Float64Regs:$x, (FMULf64rr (CVT_f64_f64
            (FDIV64rr Float64Regs:$x, Float64Regs:$y), CvtRMI),
             Float64Regs:$y))>;
def : Pat<(frem Float64Regs:$x, fpimm:$y),
          (FSUBf64rr Float64Regs:$x, (FMULf64ri (CVT_f64_f64
            (FDIV64ri Float64Regs:$x, fpimm:$y), CvtRMI),
             fpimm:$y))>;

//-----------------------------------
// Bitwise operations
//-----------------------------------

// Template for three-arg bitwise operations.  Takes three args, Creates .b16,
// .b32, .b64, and .pred (predicate registers -- i.e., i1) versions of OpcStr.
multiclass BITWISE<string OpcStr, SDNode OpNode> {
  def b1rr :
    NVPTXInst<(outs Int1Regs:$dst), (ins Int1Regs:$a, Int1Regs:$b),
              !strconcat(OpcStr, ".pred  \t$dst, $a, $b;"),
              [(set Int1Regs:$dst, (OpNode Int1Regs:$a, Int1Regs:$b))]>;
  def b1ri :
    NVPTXInst<(outs Int1Regs:$dst), (ins Int1Regs:$a, i1imm:$b),
              !strconcat(OpcStr, ".pred  \t$dst, $a, $b;"),
              [(set Int1Regs:$dst, (OpNode Int1Regs:$a, imm:$b))]>;
  def b16rr :
    NVPTXInst<(outs Int16Regs:$dst), (ins Int16Regs:$a, Int16Regs:$b),
              !strconcat(OpcStr, ".b16  \t$dst, $a, $b;"),
              [(set Int16Regs:$dst, (OpNode Int16Regs:$a, Int16Regs:$b))]>;
  def b16ri :
    NVPTXInst<(outs Int16Regs:$dst), (ins Int16Regs:$a, i16imm:$b),
              !strconcat(OpcStr, ".b16  \t$dst, $a, $b;"),
              [(set Int16Regs:$dst, (OpNode Int16Regs:$a, imm:$b))]>;
  def b32rr :
    NVPTXInst<(outs Int32Regs:$dst), (ins Int32Regs:$a, Int32Regs:$b),
              !strconcat(OpcStr, ".b32  \t$dst, $a, $b;"),
              [(set Int32Regs:$dst, (OpNode Int32Regs:$a, Int32Regs:$b))]>;
  def b32ri :
    NVPTXInst<(outs Int32Regs:$dst), (ins Int32Regs:$a, i32imm:$b),
              !strconcat(OpcStr, ".b32  \t$dst, $a, $b;"),
              [(set Int32Regs:$dst, (OpNode Int32Regs:$a, imm:$b))]>;
  def b64rr :
    NVPTXInst<(outs Int64Regs:$dst), (ins Int64Regs:$a, Int64Regs:$b),
              !strconcat(OpcStr, ".b64  \t$dst, $a, $b;"),
              [(set Int64Regs:$dst, (OpNode Int64Regs:$a, Int64Regs:$b))]>;
  def b64ri :
    NVPTXInst<(outs Int64Regs:$dst), (ins Int64Regs:$a, i64imm:$b),
              !strconcat(OpcStr, ".b64  \t$dst, $a, $b;"),
              [(set Int64Regs:$dst, (OpNode Int64Regs:$a, imm:$b))]>;
}

defm OR  : BITWISE<"or", or>;
defm AND : BITWISE<"and", and>;
defm XOR : BITWISE<"xor", xor>;

def NOT1  : NVPTXInst<(outs Int1Regs:$dst), (ins Int1Regs:$src),
                      "not.pred \t$dst, $src;",
                      [(set Int1Regs:$dst, (not Int1Regs:$src))]>;
def NOT16 : NVPTXInst<(outs Int16Regs:$dst), (ins Int16Regs:$src),
                      "not.b16 \t$dst, $src;",
                      [(set Int16Regs:$dst, (not Int16Regs:$src))]>;
def NOT32 : NVPTXInst<(outs Int32Regs:$dst), (ins Int32Regs:$src),
                      "not.b32 \t$dst, $src;",
                      [(set Int32Regs:$dst, (not Int32Regs:$src))]>;
def NOT64 : NVPTXInst<(outs Int64Regs:$dst), (ins Int64Regs:$src),
                       "not.b64 \t$dst, $src;",
                       [(set Int64Regs:$dst, (not Int64Regs:$src))]>;

// Template for left/right shifts.  Takes three operands,
//   [dest (reg), src (reg), shift (reg or imm)].
// dest and src may be int64, int32, or int16, but shift is always int32.
//
// This template also defines a 32-bit shift (imm, imm) instruction.
multiclass SHIFT<string OpcStr, SDNode OpNode> {
   def i64rr :
     NVPTXInst<(outs Int64Regs:$dst), (ins Int64Regs:$a, Int32Regs:$b),
               !strconcat(OpcStr, "64 \t$dst, $a, $b;"),
               [(set Int64Regs:$dst, (OpNode Int64Regs:$a, Int32Regs:$b))]>;
   def i64ri :
     NVPTXInst<(outs Int64Regs:$dst), (ins Int64Regs:$a, i32imm:$b),
               !strconcat(OpcStr, "64 \t$dst, $a, $b;"),
               [(set Int64Regs:$dst, (OpNode Int64Regs:$a, (i32 imm:$b)))]>;
   def i32rr :
     NVPTXInst<(outs Int32Regs:$dst), (ins Int32Regs:$a, Int32Regs:$b),
               !strconcat(OpcStr, "32 \t$dst, $a, $b;"),
               [(set Int32Regs:$dst, (OpNode Int32Regs:$a, Int32Regs:$b))]>;
   def i32ri :
     NVPTXInst<(outs Int32Regs:$dst), (ins Int32Regs:$a, i32imm:$b),
               !strconcat(OpcStr, "32 \t$dst, $a, $b;"),
               [(set Int32Regs:$dst, (OpNode Int32Regs:$a, (i32 imm:$b)))]>;
   def i32ii :
     NVPTXInst<(outs Int32Regs:$dst), (ins i32imm:$a, i32imm:$b),
               !strconcat(OpcStr, "32 \t$dst, $a, $b;"),
               [(set Int32Regs:$dst, (OpNode (i32 imm:$a), (i32 imm:$b)))]>;
   def i16rr :
     NVPTXInst<(outs Int16Regs:$dst), (ins Int16Regs:$a, Int32Regs:$b),
               !strconcat(OpcStr, "16 \t$dst, $a, $b;"),
               [(set Int16Regs:$dst, (OpNode Int16Regs:$a, Int32Regs:$b))]>;
   def i16ri :
     NVPTXInst<(outs Int16Regs:$dst), (ins Int16Regs:$a, i32imm:$b),
               !strconcat(OpcStr, "16 \t$dst, $a, $b;"),
               [(set Int16Regs:$dst, (OpNode Int16Regs:$a, (i32 imm:$b)))]>;
}

defm SHL : SHIFT<"shl.b", shl>;
defm SRA : SHIFT<"shr.s", sra>;
defm SRL : SHIFT<"shr.u", srl>;

// Bit-reverse
def BREV32 :
  NVPTXInst<(outs Int32Regs:$dst), (ins Int32Regs:$a),
             "brev.b32 \t$dst, $a;",
             [(set Int32Regs:$dst, (bitreverse Int32Regs:$a))]>;
def BREV64 :
  NVPTXInst<(outs Int64Regs:$dst), (ins Int64Regs:$a),
             "brev.b64 \t$dst, $a;",
             [(set Int64Regs:$dst, (bitreverse Int64Regs:$a))]>;

//
// Rotate: Use ptx shf instruction if available.
//

// 32 bit r2 = rotl r1, n
//    =>
//        r2 = shf.l r1, r1, n
def ROTL32imm_hw :
  NVPTXInst<(outs Int32Regs:$dst), (ins Int32Regs:$src, i32imm:$amt),
            "shf.l.wrap.b32 \t$dst, $src, $src, $amt;",
            [(set Int32Regs:$dst, (rotl Int32Regs:$src, (i32 imm:$amt)))]>,
           Requires<[hasHWROT32]>;

def ROTL32reg_hw :
  NVPTXInst<(outs Int32Regs:$dst), (ins Int32Regs:$src, Int32Regs:$amt),
            "shf.l.wrap.b32 \t$dst, $src, $src, $amt;",
            [(set Int32Regs:$dst, (rotl Int32Regs:$src, Int32Regs:$amt))]>,
           Requires<[hasHWROT32]>;

// 32 bit r2 = rotr r1, n
//    =>
//        r2 = shf.r r1, r1, n
def ROTR32imm_hw :
  NVPTXInst<(outs Int32Regs:$dst), (ins Int32Regs:$src, i32imm:$amt),
            "shf.r.wrap.b32 \t$dst, $src, $src, $amt;",
            [(set Int32Regs:$dst, (rotr Int32Regs:$src, (i32 imm:$amt)))]>,
           Requires<[hasHWROT32]>;

def ROTR32reg_hw :
  NVPTXInst<(outs Int32Regs:$dst), (ins Int32Regs:$src, Int32Regs:$amt),
            "shf.r.wrap.b32 \t$dst, $src, $src, $amt;",
            [(set Int32Regs:$dst, (rotr Int32Regs:$src, Int32Regs:$amt))]>,
           Requires<[hasHWROT32]>;

// 32-bit software rotate by immediate.  $amt2 should equal 32 - $amt1.
def ROT32imm_sw :
  NVPTXInst<(outs Int32Regs:$dst),
            (ins Int32Regs:$src, i32imm:$amt1, i32imm:$amt2),
            "{{\n\t"
            ".reg .b32 %lhs;\n\t"
            ".reg .b32 %rhs;\n\t"
            "shl.b32 \t%lhs, $src, $amt1;\n\t"
            "shr.b32 \t%rhs, $src, $amt2;\n\t"
            "add.u32 \t$dst, %lhs, %rhs;\n\t"
            "}}",
            []>;

def SUB_FRM_32 : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(32 - N->getZExtValue(), SDLoc(N), MVT::i32);
}]>;

def : Pat<(rotl Int32Regs:$src, (i32 imm:$amt)),
          (ROT32imm_sw Int32Regs:$src, imm:$amt, (SUB_FRM_32 node:$amt))>,
      Requires<[noHWROT32]>;
def : Pat<(rotr Int32Regs:$src, (i32 imm:$amt)),
          (ROT32imm_sw Int32Regs:$src, (SUB_FRM_32 node:$amt), imm:$amt)>,
      Requires<[noHWROT32]>;

// 32-bit software rotate left by register.
def ROTL32reg_sw :
  NVPTXInst<(outs Int32Regs:$dst), (ins Int32Regs:$src, Int32Regs:$amt),
            "{{\n\t"
            ".reg .b32 %lhs;\n\t"
            ".reg .b32 %rhs;\n\t"
            ".reg .b32 %amt2;\n\t"
            "shl.b32 \t%lhs, $src, $amt;\n\t"
            "sub.s32 \t%amt2, 32, $amt;\n\t"
            "shr.b32 \t%rhs, $src, %amt2;\n\t"
            "add.u32 \t$dst, %lhs, %rhs;\n\t"
            "}}",
            [(set Int32Regs:$dst, (rotl Int32Regs:$src, Int32Regs:$amt))]>,
           Requires<[noHWROT32]>;

// 32-bit software rotate right by register.
def ROTR32reg_sw :
  NVPTXInst<(outs Int32Regs:$dst), (ins Int32Regs:$src, Int32Regs:$amt),
            "{{\n\t"
            ".reg .b32 %lhs;\n\t"
            ".reg .b32 %rhs;\n\t"
            ".reg .b32 %amt2;\n\t"
            "shr.b32 \t%lhs, $src, $amt;\n\t"
            "sub.s32 \t%amt2, 32, $amt;\n\t"
            "shl.b32 \t%rhs, $src, %amt2;\n\t"
            "add.u32 \t$dst, %lhs, %rhs;\n\t"
            "}}",
            [(set Int32Regs:$dst, (rotr Int32Regs:$src, Int32Regs:$amt))]>,
           Requires<[noHWROT32]>;

// 64-bit software rotate by immediate.  $amt2 should equal 64 - $amt1.
def ROT64imm_sw :
  NVPTXInst<(outs Int64Regs:$dst),
            (ins Int64Regs:$src, i32imm:$amt1, i32imm:$amt2),
            "{{\n\t"
            ".reg .b64 %lhs;\n\t"
            ".reg .b64 %rhs;\n\t"
            "shl.b64 \t%lhs, $src, $amt1;\n\t"
            "shr.b64 \t%rhs, $src, $amt2;\n\t"
            "add.u64 \t$dst, %lhs, %rhs;\n\t"
            "}}",
            []>;

def SUB_FRM_64 : SDNodeXForm<imm, [{
    return CurDAG->getTargetConstant(64-N->getZExtValue(), SDLoc(N), MVT::i32);
}]>;

def : Pat<(rotl Int64Regs:$src, (i32 imm:$amt)),
          (ROT64imm_sw Int64Regs:$src, imm:$amt, (SUB_FRM_64 node:$amt))>;
def : Pat<(rotr Int64Regs:$src, (i32 imm:$amt)),
          (ROT64imm_sw Int64Regs:$src, (SUB_FRM_64 node:$amt), imm:$amt)>;

// 64-bit software rotate left by register.
def ROTL64reg_sw :
  NVPTXInst<(outs Int64Regs:$dst), (ins Int64Regs:$src, Int32Regs:$amt),
            "{{\n\t"
            ".reg .b64 %lhs;\n\t"
            ".reg .b64 %rhs;\n\t"
            ".reg .u32 %amt2;\n\t"
            "shl.b64 \t%lhs, $src, $amt;\n\t"
            "sub.u32 \t%amt2, 64, $amt;\n\t"
            "shr.b64 \t%rhs, $src, %amt2;\n\t"
            "add.u64 \t$dst, %lhs, %rhs;\n\t"
            "}}",
            [(set Int64Regs:$dst, (rotl Int64Regs:$src, Int32Regs:$amt))]>;

def ROTR64reg_sw :
  NVPTXInst<(outs Int64Regs:$dst), (ins Int64Regs:$src, Int32Regs:$amt),
            "{{\n\t"
            ".reg .b64 %lhs;\n\t"
            ".reg .b64 %rhs;\n\t"
            ".reg .u32 %amt2;\n\t"
            "shr.b64 \t%lhs, $src, $amt;\n\t"
            "sub.u32 \t%amt2, 64, $amt;\n\t"
            "shl.b64 \t%rhs, $src, %amt2;\n\t"
            "add.u64 \t$dst, %lhs, %rhs;\n\t"
            "}}",
            [(set Int64Regs:$dst, (rotr Int64Regs:$src, Int32Regs:$amt))]>;

//
// Funnnel shift in clamp mode
//

// Create SDNodes so they can be used in the DAG code, e.g.
// NVPTXISelLowering (LowerShiftLeftParts and LowerShiftRightParts)
def FUN_SHFL_CLAMP : SDNode<"NVPTXISD::FUN_SHFL_CLAMP", SDTIntShiftDOp, []>;
def FUN_SHFR_CLAMP : SDNode<"NVPTXISD::FUN_SHFR_CLAMP", SDTIntShiftDOp, []>;

def FUNSHFLCLAMP :
  NVPTXInst<(outs Int32Regs:$dst),
            (ins Int32Regs:$lo, Int32Regs:$hi, Int32Regs:$amt),
            "shf.l.clamp.b32 \t$dst, $lo, $hi, $amt;",
            [(set Int32Regs:$dst,
              (FUN_SHFL_CLAMP Int32Regs:$lo, Int32Regs:$hi, Int32Regs:$amt))]>;

def FUNSHFRCLAMP :
  NVPTXInst<(outs Int32Regs:$dst),
            (ins Int32Regs:$lo, Int32Regs:$hi, Int32Regs:$amt),
            "shf.r.clamp.b32 \t$dst, $lo, $hi, $amt;",
            [(set Int32Regs:$dst,
             (FUN_SHFR_CLAMP Int32Regs:$lo, Int32Regs:$hi, Int32Regs:$amt))]>;

//
// BFE - bit-field extract
//

// Template for BFE instructions.  Takes four args,
//   [dest (reg), src (reg), start (reg or imm), end (reg or imm)].
// Start may be an imm only if end is also an imm.  FIXME: Is this a
// restriction in PTX?
//
// dest and src may be int32 or int64, but start and end are always int32.
multiclass BFE<string TyStr, RegisterClass RC> {
  def rrr
    : NVPTXInst<(outs RC:$d),
                (ins RC:$a, Int32Regs:$b, Int32Regs:$c),
                !strconcat("bfe.", TyStr, " \t$d, $a, $b, $c;"), []>;
  def rri
    : NVPTXInst<(outs RC:$d),
                (ins RC:$a, Int32Regs:$b, i32imm:$c),
                !strconcat("bfe.", TyStr, " \t$d, $a, $b, $c;"), []>;
  def rii
    : NVPTXInst<(outs RC:$d),
                (ins RC:$a, i32imm:$b, i32imm:$c),
                !strconcat("bfe.", TyStr, " \t$d, $a, $b, $c;"), []>;
}

let hasSideEffects = 0 in {
  defm BFE_S32 : BFE<"s32", Int32Regs>;
  defm BFE_U32 : BFE<"u32", Int32Regs>;
  defm BFE_S64 : BFE<"s64", Int64Regs>;
  defm BFE_U64 : BFE<"u64", Int64Regs>;
}

//-----------------------------------
// Comparison instructions (setp, set)
//-----------------------------------

// FIXME: This doesn't cover versions of set and setp that combine with a
// boolean predicate, e.g. setp.eq.and.b16.

let hasSideEffects = 0 in {
  multiclass SETP<string TypeStr, RegisterClass RC, Operand ImmCls> {
    def rr :
      NVPTXInst<(outs Int1Regs:$dst), (ins RC:$a, RC:$b, CmpMode:$cmp),
                !strconcat("setp${cmp:base}${cmp:ftz}.", TypeStr,
                           " \t$dst, $a, $b;"), []>;
    def ri :
      NVPTXInst<(outs Int1Regs:$dst), (ins RC:$a, ImmCls:$b, CmpMode:$cmp),
                !strconcat("setp${cmp:base}${cmp:ftz}.", TypeStr,
                           " \t$dst, $a, $b;"), []>;
    def ir :
      NVPTXInst<(outs Int1Regs:$dst), (ins ImmCls:$a, RC:$b, CmpMode:$cmp),
                !strconcat("setp${cmp:base}${cmp:ftz}.", TypeStr,
                           " \t$dst, $a, $b;"), []>;
  }
}

defm SETP_b16 : SETP<"b16", Int16Regs, i16imm>;
defm SETP_s16 : SETP<"s16", Int16Regs, i16imm>;
defm SETP_u16 : SETP<"u16", Int16Regs, i16imm>;
defm SETP_b32 : SETP<"b32", Int32Regs, i32imm>;
defm SETP_s32 : SETP<"s32", Int32Regs, i32imm>;
defm SETP_u32 : SETP<"u32", Int32Regs, i32imm>;
defm SETP_b64 : SETP<"b64", Int64Regs, i64imm>;
defm SETP_s64 : SETP<"s64", Int64Regs, i64imm>;
defm SETP_u64 : SETP<"u64", Int64Regs, i64imm>;
defm SETP_f32 : SETP<"f32", Float32Regs, f32imm>;
defm SETP_f64 : SETP<"f64", Float64Regs, f64imm>;
def SETP_f16rr :
      NVPTXInst<(outs Int1Regs:$dst),
                (ins Float16Regs:$a, Float16Regs:$b, CmpMode:$cmp),
                "setp${cmp:base}${cmp:ftz}.f16 \t$dst, $a, $b;",
                []>, Requires<[useFP16Math]>;

def SETP_f16x2rr :
      NVPTXInst<(outs Int1Regs:$p, Int1Regs:$q),
                (ins Float16x2Regs:$a, Float16x2Regs:$b, CmpMode:$cmp),
                "setp${cmp:base}${cmp:ftz}.f16x2 \t$p|$q, $a, $b;",
                []>,
                Requires<[useFP16Math]>;


// FIXME: This doesn't appear to be correct.  The "set" mnemonic has the form
// "set.CmpOp{.ftz}.dtype.stype", where dtype is the type of the destination
// reg, either u32, s32, or f32.  Anyway these aren't used at the moment.

let hasSideEffects = 0 in {
  multiclass SET<string TypeStr, RegisterClass RC, Operand ImmCls> {
    def rr : NVPTXInst<(outs Int32Regs:$dst),
                       (ins RC:$a, RC:$b, CmpMode:$cmp),
                       !strconcat("set$cmp.", TypeStr, " \t$dst, $a, $b;"), []>;
    def ri : NVPTXInst<(outs Int32Regs:$dst),
                       (ins RC:$a, ImmCls:$b, CmpMode:$cmp),
                       !strconcat("set$cmp.", TypeStr, " \t$dst, $a, $b;"), []>;
    def ir : NVPTXInst<(outs Int32Regs:$dst),
                       (ins ImmCls:$a, RC:$b, CmpMode:$cmp),
                       !strconcat("set$cmp.", TypeStr, " \t$dst, $a, $b;"), []>;
  }
}

defm SET_b16 : SET<"b16", Int16Regs, i16imm>;
defm SET_s16 : SET<"s16", Int16Regs, i16imm>;
defm SET_u16 : SET<"u16", Int16Regs, i16imm>;
defm SET_b32 : SET<"b32", Int32Regs, i32imm>;
defm SET_s32 : SET<"s32", Int32Regs, i32imm>;
defm SET_u32 : SET<"u32", Int32Regs, i32imm>;
defm SET_b64 : SET<"b64", Int64Regs, i64imm>;
defm SET_s64 : SET<"s64", Int64Regs, i64imm>;
defm SET_u64 : SET<"u64", Int64Regs, i64imm>;
defm SET_f16 : SET<"f16", Float16Regs, f16imm>;
defm SET_f32 : SET<"f32", Float32Regs, f32imm>;
defm SET_f64 : SET<"f64", Float64Regs, f64imm>;

//-----------------------------------
// Selection instructions (selp)
//-----------------------------------

// FIXME: Missing slct

// selp instructions that don't have any pattern matches; we explicitly use
// them within this file.
let hasSideEffects = 0 in {
  multiclass SELP<string TypeStr, RegisterClass RC, Operand ImmCls> {
    def rr : NVPTXInst<(outs RC:$dst),
                       (ins RC:$a, RC:$b, Int1Regs:$p),
                       !strconcat("selp.", TypeStr, " \t$dst, $a, $b, $p;"), []>;
    def ri : NVPTXInst<(outs RC:$dst),
                       (ins RC:$a, ImmCls:$b, Int1Regs:$p),
                       !strconcat("selp.", TypeStr, " \t$dst, $a, $b, $p;"), []>;
    def ir : NVPTXInst<(outs RC:$dst),
                       (ins ImmCls:$a, RC:$b, Int1Regs:$p),
                       !strconcat("selp.", TypeStr, " \t$dst, $a, $b, $p;"), []>;
    def ii : NVPTXInst<(outs RC:$dst),
                       (ins ImmCls:$a, ImmCls:$b, Int1Regs:$p),
                       !strconcat("selp.", TypeStr, " \t$dst, $a, $b, $p;"), []>;
  }

  multiclass SELP_PATTERN<string TypeStr, RegisterClass RC, Operand ImmCls,
                          SDNode ImmNode> {
    def rr :
      NVPTXInst<(outs RC:$dst),
                (ins RC:$a, RC:$b, Int1Regs:$p),
                !strconcat("selp.", TypeStr, " \t$dst, $a, $b, $p;"),
                [(set RC:$dst, (select Int1Regs:$p, RC:$a, RC:$b))]>;
    def ri :
      NVPTXInst<(outs RC:$dst),
                (ins RC:$a, ImmCls:$b, Int1Regs:$p),
                !strconcat("selp.", TypeStr, " \t$dst, $a, $b, $p;"),
                [(set RC:$dst, (select Int1Regs:$p, RC:$a, ImmNode:$b))]>;
    def ir :
      NVPTXInst<(outs RC:$dst),
                (ins ImmCls:$a, RC:$b, Int1Regs:$p),
                !strconcat("selp.", TypeStr, " \t$dst, $a, $b, $p;"),
                [(set RC:$dst, (select Int1Regs:$p, ImmNode:$a, RC:$b))]>;
    def ii :
      NVPTXInst<(outs RC:$dst),
                (ins ImmCls:$a, ImmCls:$b, Int1Regs:$p),
                !strconcat("selp.", TypeStr, " \t$dst, $a, $b, $p;"),
                [(set RC:$dst, (select Int1Regs:$p, ImmNode:$a, ImmNode:$b))]>;
  }
}

// Don't pattern match on selp.{s,u}{16,32,64} -- selp.b{16,32,64} is just as
// good.
defm SELP_b16 : SELP_PATTERN<"b16", Int16Regs, i16imm, imm>;
defm SELP_s16 : SELP<"s16", Int16Regs, i16imm>;
defm SELP_u16 : SELP<"u16", Int16Regs, i16imm>;
defm SELP_b32 : SELP_PATTERN<"b32", Int32Regs, i32imm, imm>;
defm SELP_s32 : SELP<"s32", Int32Regs, i32imm>;
defm SELP_u32 : SELP<"u32", Int32Regs, i32imm>;
defm SELP_b64 : SELP_PATTERN<"b64", Int64Regs, i64imm, imm>;
defm SELP_s64 : SELP<"s64", Int64Regs, i64imm>;
defm SELP_u64 : SELP<"u64", Int64Regs, i64imm>;
defm SELP_f16 : SELP_PATTERN<"b16", Float16Regs, f16imm, fpimm>;
defm SELP_f32 : SELP_PATTERN<"f32", Float32Regs, f32imm, fpimm>;
defm SELP_f64 : SELP_PATTERN<"f64", Float64Regs, f64imm, fpimm>;

def SELP_f16x2rr :
    NVPTXInst<(outs Float16x2Regs:$dst),
              (ins Float16x2Regs:$a, Float16x2Regs:$b, Int1Regs:$p),
              "selp.b32 \t$dst, $a, $b, $p;",
              [(set Float16x2Regs:$dst,
                    (select Int1Regs:$p, Float16x2Regs:$a, Float16x2Regs:$b))]>;

//-----------------------------------
// Data Movement (Load / Store, Move)
//-----------------------------------

def ADDRri : ComplexPattern<i32, 2, "SelectADDRri", [frameindex],
                            [SDNPWantRoot]>;
def ADDRri64 : ComplexPattern<i64, 2, "SelectADDRri64", [frameindex],
                              [SDNPWantRoot]>;
def ADDRvar : ComplexPattern<iPTR, 1, "SelectDirectAddr", [], []>;

def MEMri : Operand<i32> {
  let PrintMethod = "printMemOperand";
  let MIOperandInfo = (ops Int32Regs, i32imm);
}
def MEMri64 : Operand<i64> {
  let PrintMethod = "printMemOperand";
  let MIOperandInfo = (ops Int64Regs, i64imm);
}

def imem : Operand<iPTR> {
  let PrintMethod = "printOperand";
}

def imemAny : Operand<iPTRAny> {
  let PrintMethod = "printOperand";
}

def LdStCode : Operand<i32> {
  let PrintMethod = "printLdStCode";
}

def SDTWrapper : SDTypeProfile<1, 1, [SDTCisSameAs<0, 1>, SDTCisPtrTy<0>]>;
def Wrapper    : SDNode<"NVPTXISD::Wrapper", SDTWrapper>;

// Load a memory address into a u32 or u64 register.
def MOV_ADDR : NVPTXInst<(outs Int32Regs:$dst), (ins imem:$a),
                         "mov.u32 \t$dst, $a;",
                         [(set Int32Regs:$dst, (Wrapper tglobaladdr:$a))]>;
def MOV_ADDR64 : NVPTXInst<(outs Int64Regs:$dst), (ins imem:$a),
                           "mov.u64 \t$dst, $a;",
                           [(set Int64Regs:$dst, (Wrapper tglobaladdr:$a))]>;

// Get pointer to local stack.
let hasSideEffects = 0 in {
  def MOV_DEPOT_ADDR :    NVPTXInst<(outs Int32Regs:$d), (ins i32imm:$num),
                                     "mov.u32 \t$d, __local_depot$num;", []>;
  def MOV_DEPOT_ADDR_64 : NVPTXInst<(outs Int64Regs:$d), (ins i32imm:$num),
                                    "mov.u64 \t$d, __local_depot$num;", []>;
}


// copyPhysreg is hard-coded in NVPTXInstrInfo.cpp
let IsSimpleMove=1, hasSideEffects=0 in {
  def IMOV1rr :  NVPTXInst<(outs Int1Regs:$dst), (ins Int1Regs:$sss),
                           "mov.pred \t$dst, $sss;", []>;
  def IMOV16rr : NVPTXInst<(outs Int16Regs:$dst), (ins Int16Regs:$sss),
                           "mov.u16 \t$dst, $sss;", []>;
  def IMOV32rr : NVPTXInst<(outs Int32Regs:$dst), (ins Int32Regs:$sss),
                           "mov.u32 \t$dst, $sss;", []>;
  def IMOV64rr : NVPTXInst<(outs Int64Regs:$dst), (ins Int64Regs:$sss),
                           "mov.u64 \t$dst, $sss;", []>;

  def FMOV16rr : NVPTXInst<(outs Float16Regs:$dst), (ins Float16Regs:$src),
                           // We have to use .b16 here as there's no mov.f16.
                           "mov.b16 \t$dst, $src;", []>;
  def FMOV32rr : NVPTXInst<(outs Float32Regs:$dst), (ins Float32Regs:$src),
                           "mov.f32 \t$dst, $src;", []>;
  def FMOV64rr : NVPTXInst<(outs Float64Regs:$dst), (ins Float64Regs:$src),
                           "mov.f64 \t$dst, $src;", []>;
}

def IMOV1ri : NVPTXInst<(outs Int1Regs:$dst), (ins i1imm:$src),
                        "mov.pred \t$dst, $src;",
                        [(set Int1Regs:$dst, imm:$src)]>;
def IMOV16ri : NVPTXInst<(outs Int16Regs:$dst), (ins i16imm:$src),
                         "mov.u16 \t$dst, $src;",
                         [(set Int16Regs:$dst, imm:$src)]>;
def IMOV32ri : NVPTXInst<(outs Int32Regs:$dst), (ins i32imm:$src),
                         "mov.u32 \t$dst, $src;",
                         [(set Int32Regs:$dst, imm:$src)]>;
def IMOV64i : NVPTXInst<(outs Int64Regs:$dst), (ins i64imm:$src),
                        "mov.u64 \t$dst, $src;",
                        [(set Int64Regs:$dst, imm:$src)]>;

def FMOV32ri : NVPTXInst<(outs Float32Regs:$dst), (ins f32imm:$src),
                         "mov.f32 \t$dst, $src;",
                         [(set Float32Regs:$dst, fpimm:$src)]>;
def FMOV64ri : NVPTXInst<(outs Float64Regs:$dst), (ins f64imm:$src),
                         "mov.f64 \t$dst, $src;",
                         [(set Float64Regs:$dst, fpimm:$src)]>;

def : Pat<(i32 (Wrapper texternalsym:$dst)), (IMOV32ri texternalsym:$dst)>;

//---- Copy Frame Index ----
def LEA_ADDRi :   NVPTXInst<(outs Int32Regs:$dst), (ins MEMri:$addr),
                            "add.u32 \t$dst, ${addr:add};",
                            [(set Int32Regs:$dst, ADDRri:$addr)]>;
def LEA_ADDRi64 : NVPTXInst<(outs Int64Regs:$dst), (ins MEMri64:$addr),
                            "add.u64 \t$dst, ${addr:add};",
                            [(set Int64Regs:$dst, ADDRri64:$addr)]>;

//-----------------------------------
// Comparison and Selection
//-----------------------------------

multiclass ISET_FORMAT<PatFrag OpNode, PatLeaf Mode,
                       Instruction setp_16rr,
                       Instruction setp_16ri,
                       Instruction setp_16ir,
                       Instruction setp_32rr,
                       Instruction setp_32ri,
                       Instruction setp_32ir,
                       Instruction setp_64rr,
                       Instruction setp_64ri,
                       Instruction setp_64ir,
                       Instruction set_16rr,
                       Instruction set_16ri,
                       Instruction set_16ir,
                       Instruction set_32rr,
                       Instruction set_32ri,
                       Instruction set_32ir,
                       Instruction set_64rr,
                       Instruction set_64ri,
                       Instruction set_64ir> {
  // i16 -> pred
  def : Pat<(i1 (OpNode Int16Regs:$a, Int16Regs:$b)),
            (setp_16rr Int16Regs:$a, Int16Regs:$b, Mode)>;
  def : Pat<(i1 (OpNode Int16Regs:$a, imm:$b)),
            (setp_16ri Int16Regs:$a, imm:$b, Mode)>;
  def : Pat<(i1 (OpNode imm:$a, Int16Regs:$b)),
            (setp_16ir imm:$a, Int16Regs:$b, Mode)>;
  // i32 -> pred
  def : Pat<(i1 (OpNode Int32Regs:$a, Int32Regs:$b)),
            (setp_32rr Int32Regs:$a, Int32Regs:$b, Mode)>;
  def : Pat<(i1 (OpNode Int32Regs:$a, imm:$b)),
            (setp_32ri Int32Regs:$a, imm:$b, Mode)>;
  def : Pat<(i1 (OpNode imm:$a, Int32Regs:$b)),
            (setp_32ir imm:$a, Int32Regs:$b, Mode)>;
  // i64 -> pred
  def : Pat<(i1 (OpNode Int64Regs:$a, Int64Regs:$b)),
            (setp_64rr Int64Regs:$a, Int64Regs:$b, Mode)>;
  def : Pat<(i1 (OpNode Int64Regs:$a, imm:$b)),
            (setp_64ri Int64Regs:$a, imm:$b, Mode)>;
  def : Pat<(i1 (OpNode imm:$a, Int64Regs:$b)),
            (setp_64ir imm:$a, Int64Regs:$b, Mode)>;

  // i16 -> i32
  def : Pat<(i32 (OpNode Int16Regs:$a, Int16Regs:$b)),
            (set_16rr Int16Regs:$a, Int16Regs:$b, Mode)>;
  def : Pat<(i32 (OpNode Int16Regs:$a, imm:$b)),
            (set_16ri Int16Regs:$a, imm:$b, Mode)>;
  def : Pat<(i32 (OpNode imm:$a, Int16Regs:$b)),
            (set_16ir imm:$a, Int16Regs:$b, Mode)>;
  // i32 -> i32
  def : Pat<(i32 (OpNode Int32Regs:$a, Int32Regs:$b)),
            (set_32rr Int32Regs:$a, Int32Regs:$b, Mode)>;
  def : Pat<(i32 (OpNode Int32Regs:$a, imm:$b)),
            (set_32ri Int32Regs:$a, imm:$b, Mode)>;
  def : Pat<(i32 (OpNode imm:$a, Int32Regs:$b)),
            (set_32ir imm:$a, Int32Regs:$b, Mode)>;
  // i64 -> i32
  def : Pat<(i32 (OpNode Int64Regs:$a, Int64Regs:$b)),
            (set_64rr Int64Regs:$a, Int64Regs:$b, Mode)>;
  def : Pat<(i32 (OpNode Int64Regs:$a, imm:$b)),
            (set_64ri Int64Regs:$a, imm:$b, Mode)>;
  def : Pat<(i32 (OpNode imm:$a, Int64Regs:$b)),
            (set_64ir imm:$a, Int64Regs:$b, Mode)>;
}

multiclass ISET_FORMAT_SIGNED<PatFrag OpNode, PatLeaf Mode>
  : ISET_FORMAT<OpNode, Mode,
                SETP_s16rr, SETP_s16ri, SETP_s16ir,
                SETP_s32rr, SETP_s32ri, SETP_s32ir,
                SETP_s64rr, SETP_s64ri, SETP_s64ir,
                SET_s16rr, SET_s16ri, SET_s16ir,
                SET_s32rr, SET_s32ri, SET_s32ir,
                SET_s64rr, SET_s64ri, SET_s64ir> {
  // TableGen doesn't like empty multiclasses.
  def : PatLeaf<(i32 0)>;
}

multiclass ISET_FORMAT_UNSIGNED<PatFrag OpNode, PatLeaf Mode>
  : ISET_FORMAT<OpNode, Mode,
                SETP_u16rr, SETP_u16ri, SETP_u16ir,
                SETP_u32rr, SETP_u32ri, SETP_u32ir,
                SETP_u64rr, SETP_u64ri, SETP_u64ir,
                SET_u16rr, SET_u16ri, SET_u16ir,
                SET_u32rr, SET_u32ri, SET_u32ir,
                SET_u64rr, SET_u64ri, SET_u64ir> {
  // TableGen doesn't like empty multiclasses.
  def : PatLeaf<(i32 0)>;
}

defm : ISET_FORMAT_SIGNED<setgt, CmpGT>;
defm : ISET_FORMAT_SIGNED<setlt, CmpLT>;
defm : ISET_FORMAT_SIGNED<setge, CmpGE>;
defm : ISET_FORMAT_SIGNED<setle, CmpLE>;
defm : ISET_FORMAT_SIGNED<seteq, CmpEQ>;
defm : ISET_FORMAT_SIGNED<setne, CmpNE>;
defm : ISET_FORMAT_UNSIGNED<setugt, CmpGT>;
defm : ISET_FORMAT_UNSIGNED<setult, CmpLT>;
defm : ISET_FORMAT_UNSIGNED<setuge, CmpGE>;
defm : ISET_FORMAT_UNSIGNED<setule, CmpLE>;
defm : ISET_FORMAT_UNSIGNED<setueq, CmpEQ>;
defm : ISET_FORMAT_UNSIGNED<setune, CmpNE>;

// i1 compares
def : Pat<(setne Int1Regs:$a, Int1Regs:$b),
          (XORb1rr Int1Regs:$a, Int1Regs:$b)>;
def : Pat<(setune Int1Regs:$a, Int1Regs:$b),
          (XORb1rr Int1Regs:$a, Int1Regs:$b)>;

def : Pat<(seteq Int1Regs:$a, Int1Regs:$b),
          (NOT1 (XORb1rr Int1Regs:$a, Int1Regs:$b))>;
def : Pat<(setueq Int1Regs:$a, Int1Regs:$b),
          (NOT1 (XORb1rr Int1Regs:$a, Int1Regs:$b))>;

// i1 compare -> i32
def : Pat<(i32 (setne Int1Regs:$a, Int1Regs:$b)),
          (SELP_u32ii -1, 0, (XORb1rr Int1Regs:$a, Int1Regs:$b))>;
def : Pat<(i32 (setne Int1Regs:$a, Int1Regs:$b)),
          (SELP_u32ii 0, -1, (XORb1rr Int1Regs:$a, Int1Regs:$b))>;



multiclass FSET_FORMAT<PatFrag OpNode, PatLeaf Mode, PatLeaf ModeFTZ> {
  // f16 -> pred
  def : Pat<(i1 (OpNode Float16Regs:$a, Float16Regs:$b)),
            (SETP_f16rr Float16Regs:$a, Float16Regs:$b, ModeFTZ)>,
        Requires<[useFP16Math,doF32FTZ]>;
  def : Pat<(i1 (OpNode Float16Regs:$a, Float16Regs:$b)),
            (SETP_f16rr Float16Regs:$a, Float16Regs:$b, Mode)>,
        Requires<[useFP16Math]>;
  def : Pat<(i1 (OpNode Float16Regs:$a, fpimm:$b)),
            (SETP_f16rr Float16Regs:$a, (LOAD_CONST_F16 fpimm:$b), ModeFTZ)>,
        Requires<[useFP16Math,doF32FTZ]>;
  def : Pat<(i1 (OpNode Float16Regs:$a, fpimm:$b)),
            (SETP_f16rr Float16Regs:$a, (LOAD_CONST_F16 fpimm:$b), Mode)>,
        Requires<[useFP16Math]>;
  def : Pat<(i1 (OpNode fpimm:$a, Float16Regs:$b)),
            (SETP_f16rr (LOAD_CONST_F16 fpimm:$a), Float16Regs:$b, ModeFTZ)>,
        Requires<[useFP16Math,doF32FTZ]>;
  def : Pat<(i1 (OpNode fpimm:$a, Float16Regs:$b)),
            (SETP_f16rr (LOAD_CONST_F16 fpimm:$a), Float16Regs:$b, Mode)>,
        Requires<[useFP16Math]>;

  // f32 -> pred
  def : Pat<(i1 (OpNode Float32Regs:$a, Float32Regs:$b)),
            (SETP_f32rr Float32Regs:$a, Float32Regs:$b, ModeFTZ)>,
        Requires<[doF32FTZ]>;
  def : Pat<(i1 (OpNode Float32Regs:$a, Float32Regs:$b)),
            (SETP_f32rr Float32Regs:$a, Float32Regs:$b, Mode)>;
  def : Pat<(i1 (OpNode Float32Regs:$a, fpimm:$b)),
            (SETP_f32ri Float32Regs:$a, fpimm:$b, ModeFTZ)>,
        Requires<[doF32FTZ]>;
  def : Pat<(i1 (OpNode Float32Regs:$a, fpimm:$b)),
            (SETP_f32ri Float32Regs:$a, fpimm:$b, Mode)>;
  def : Pat<(i1 (OpNode fpimm:$a, Float32Regs:$b)),
            (SETP_f32ir fpimm:$a, Float32Regs:$b, ModeFTZ)>,
        Requires<[doF32FTZ]>;
  def : Pat<(i1 (OpNode fpimm:$a, Float32Regs:$b)),
            (SETP_f32ir fpimm:$a, Float32Regs:$b, Mode)>;

  // f64 -> pred
  def : Pat<(i1 (OpNode Float64Regs:$a, Float64Regs:$b)),
            (SETP_f64rr Float64Regs:$a, Float64Regs:$b, Mode)>;
  def : Pat<(i1 (OpNode Float64Regs:$a, fpimm:$b)),
            (SETP_f64ri Float64Regs:$a, fpimm:$b, Mode)>;
  def : Pat<(i1 (OpNode fpimm:$a, Float64Regs:$b)),
            (SETP_f64ir fpimm:$a, Float64Regs:$b, Mode)>;

  // f16 -> i32
  def : Pat<(i32 (OpNode Float16Regs:$a, Float16Regs:$b)),
            (SET_f16rr Float16Regs:$a, Float16Regs:$b, ModeFTZ)>,
        Requires<[useFP16Math, doF32FTZ]>;
  def : Pat<(i32 (OpNode Float16Regs:$a, Float16Regs:$b)),
            (SET_f16rr Float16Regs:$a, Float16Regs:$b, Mode)>,
        Requires<[useFP16Math]>;
  def : Pat<(i32 (OpNode Float16Regs:$a, fpimm:$b)),
            (SET_f16rr Float16Regs:$a, (LOAD_CONST_F16 fpimm:$b), ModeFTZ)>,
        Requires<[useFP16Math, doF32FTZ]>;
  def : Pat<(i32 (OpNode Float16Regs:$a, fpimm:$b)),
            (SET_f16rr Float16Regs:$a, (LOAD_CONST_F16 fpimm:$b), Mode)>,
        Requires<[useFP16Math]>;
  def : Pat<(i32 (OpNode fpimm:$a, Float16Regs:$b)),
            (SET_f16ir (LOAD_CONST_F16 fpimm:$a), Float16Regs:$b, ModeFTZ)>,
        Requires<[useFP16Math, doF32FTZ]>;
  def : Pat<(i32 (OpNode fpimm:$a, Float16Regs:$b)),
            (SET_f16ir (LOAD_CONST_F16 fpimm:$a), Float16Regs:$b, Mode)>,
        Requires<[useFP16Math]>;

  // f32 -> i32
  def : Pat<(i32 (OpNode Float32Regs:$a, Float32Regs:$b)),
            (SET_f32rr Float32Regs:$a, Float32Regs:$b, ModeFTZ)>,
        Requires<[doF32FTZ]>;
  def : Pat<(i32 (OpNode Float32Regs:$a, Float32Regs:$b)),
            (SET_f32rr Float32Regs:$a, Float32Regs:$b, Mode)>;
  def : Pat<(i32 (OpNode Float32Regs:$a, fpimm:$b)),
            (SET_f32ri Float32Regs:$a, fpimm:$b, ModeFTZ)>,
        Requires<[doF32FTZ]>;
  def : Pat<(i32 (OpNode Float32Regs:$a, fpimm:$b)),
            (SET_f32ri Float32Regs:$a, fpimm:$b, Mode)>;
  def : Pat<(i32 (OpNode fpimm:$a, Float32Regs:$b)),
            (SET_f32ir fpimm:$a, Float32Regs:$b, ModeFTZ)>,
        Requires<[doF32FTZ]>;
  def : Pat<(i32 (OpNode fpimm:$a, Float32Regs:$b)),
            (SET_f32ir fpimm:$a, Float32Regs:$b, Mode)>;

  // f64 -> i32
  def : Pat<(i32 (OpNode Float64Regs:$a, Float64Regs:$b)),
            (SET_f64rr Float64Regs:$a, Float64Regs:$b, Mode)>;
  def : Pat<(i32 (OpNode Float64Regs:$a, fpimm:$b)),
            (SET_f64ri Float64Regs:$a, fpimm:$b, Mode)>;
  def : Pat<(i32 (OpNode fpimm:$a, Float64Regs:$b)),
            (SET_f64ir fpimm:$a, Float64Regs:$b, Mode)>;
}

defm FSetOGT : FSET_FORMAT<setogt, CmpGT, CmpGT_FTZ>;
defm FSetOLT : FSET_FORMAT<setolt, CmpLT, CmpLT_FTZ>;
defm FSetOGE : FSET_FORMAT<setoge, CmpGE, CmpGE_FTZ>;
defm FSetOLE : FSET_FORMAT<setole, CmpLE, CmpLE_FTZ>;
defm FSetOEQ : FSET_FORMAT<setoeq, CmpEQ, CmpEQ_FTZ>;
defm FSetONE : FSET_FORMAT<setone, CmpNE, CmpNE_FTZ>;

defm FSetUGT : FSET_FORMAT<setugt, CmpGTU, CmpGTU_FTZ>;
defm FSetULT : FSET_FORMAT<setult, CmpLTU, CmpLTU_FTZ>;
defm FSetUGE : FSET_FORMAT<setuge, CmpGEU, CmpGEU_FTZ>;
defm FSetULE : FSET_FORMAT<setule, CmpLEU, CmpLEU_FTZ>;
defm FSetUEQ : FSET_FORMAT<setueq, CmpEQU, CmpEQU_FTZ>;
defm FSetUNE : FSET_FORMAT<setune, CmpNEU, CmpNEU_FTZ>;

defm FSetGT : FSET_FORMAT<setgt, CmpGT, CmpGT_FTZ>;
defm FSetLT : FSET_FORMAT<setlt, CmpLT, CmpLT_FTZ>;
defm FSetGE : FSET_FORMAT<setge, CmpGE, CmpGE_FTZ>;
defm FSetLE : FSET_FORMAT<setle, CmpLE, CmpLE_FTZ>;
defm FSetEQ : FSET_FORMAT<seteq, CmpEQ, CmpEQ_FTZ>;
defm FSetNE : FSET_FORMAT<setne, CmpNE, CmpNE_FTZ>;

defm FSetNUM : FSET_FORMAT<seto, CmpNUM, CmpNUM_FTZ>;
defm FSetNAN : FSET_FORMAT<setuo, CmpNAN, CmpNAN_FTZ>;

// FIXME: What is this doing here?  Can it be deleted?
// def ld_param         : SDNode<"NVPTXISD::LOAD_PARAM", SDTLoad,
//                         [SDNPHasChain, SDNPMayLoad, SDNPMemOperand]>;

def SDTDeclareParamProfile :
  SDTypeProfile<0, 3, [SDTCisInt<0>, SDTCisInt<1>, SDTCisInt<2>]>;
def SDTDeclareScalarParamProfile :
  SDTypeProfile<0, 3, [SDTCisInt<0>, SDTCisInt<1>, SDTCisInt<2>]>;
def SDTLoadParamProfile : SDTypeProfile<1, 2, [SDTCisInt<1>, SDTCisInt<2>]>;
def SDTLoadParamV2Profile : SDTypeProfile<2, 2, [SDTCisSameAs<0, 1>, SDTCisInt<2>, SDTCisInt<3>]>;
def SDTLoadParamV4Profile : SDTypeProfile<4, 2, [SDTCisInt<4>, SDTCisInt<5>]>;
def SDTPrintCallProfile : SDTypeProfile<0, 1, [SDTCisInt<0>]>;
def SDTPrintCallUniProfile : SDTypeProfile<0, 1, [SDTCisInt<0>]>;
def SDTStoreParamProfile : SDTypeProfile<0, 3, [SDTCisInt<0>, SDTCisInt<1>]>;
def SDTStoreParamV2Profile : SDTypeProfile<0, 4, [SDTCisInt<0>, SDTCisInt<1>]>;
def SDTStoreParamV4Profile : SDTypeProfile<0, 6, [SDTCisInt<0>, SDTCisInt<1>]>;
def SDTStoreParam32Profile : SDTypeProfile<0, 3, [SDTCisInt<0>, SDTCisInt<1>]>;
def SDTCallArgProfile : SDTypeProfile<0, 2, [SDTCisInt<0>]>;
def SDTCallArgMarkProfile : SDTypeProfile<0, 0, []>;
def SDTCallVoidProfile : SDTypeProfile<0, 1, []>;
def SDTCallValProfile : SDTypeProfile<1, 0, []>;
def SDTMoveParamProfile : SDTypeProfile<1, 1, []>;
def SDTStoreRetvalProfile : SDTypeProfile<0, 2, [SDTCisInt<0>]>;
def SDTStoreRetvalV2Profile : SDTypeProfile<0, 3, [SDTCisInt<0>]>;
def SDTStoreRetvalV4Profile : SDTypeProfile<0, 5, [SDTCisInt<0>]>;
def SDTPseudoUseParamProfile : SDTypeProfile<0, 1, []>;
def SDTProxyRegProfile : SDTypeProfile<1, 1, []>;

def DeclareParam :
  SDNode<"NVPTXISD::DeclareParam", SDTDeclareParamProfile,
         [SDNPHasChain, SDNPOutGlue, SDNPInGlue, SDNPSideEffect]>;
def DeclareScalarParam :
  SDNode<"NVPTXISD::DeclareScalarParam", SDTDeclareScalarParamProfile,
         [SDNPHasChain, SDNPOutGlue, SDNPInGlue, SDNPSideEffect]>;
def DeclareRetParam :
  SDNode<"NVPTXISD::DeclareRetParam", SDTDeclareParamProfile,
         [SDNPHasChain, SDNPOutGlue, SDNPInGlue, SDNPSideEffect]>;
def DeclareRet :
  SDNode<"NVPTXISD::DeclareRet", SDTDeclareScalarParamProfile,
         [SDNPHasChain, SDNPOutGlue, SDNPInGlue, SDNPSideEffect]>;
def LoadParam :
  SDNode<"NVPTXISD::LoadParam", SDTLoadParamProfile,
         [SDNPHasChain, SDNPMayLoad, SDNPOutGlue, SDNPInGlue]>;
def LoadParamV2 :
  SDNode<"NVPTXISD::LoadParamV2", SDTLoadParamV2Profile,
         [SDNPHasChain, SDNPMayLoad, SDNPOutGlue, SDNPInGlue]>;
def LoadParamV4 :
  SDNode<"NVPTXISD::LoadParamV4", SDTLoadParamV4Profile,
         [SDNPHasChain, SDNPMayLoad, SDNPOutGlue, SDNPInGlue]>;
def PrintCall :
  SDNode<"NVPTXISD::PrintCall", SDTPrintCallProfile,
         [SDNPHasChain, SDNPOutGlue, SDNPInGlue, SDNPSideEffect]>;
def PrintConvergentCall :
  SDNode<"NVPTXISD::PrintConvergentCall", SDTPrintCallProfile,
         [SDNPHasChain, SDNPOutGlue, SDNPInGlue, SDNPSideEffect]>;
def PrintCallUni :
  SDNode<"NVPTXISD::PrintCallUni", SDTPrintCallUniProfile,
         [SDNPHasChain, SDNPOutGlue, SDNPInGlue, SDNPSideEffect]>;
def PrintConvergentCallUni :
  SDNode<"NVPTXISD::PrintConvergentCallUni", SDTPrintCallUniProfile,
         [SDNPHasChain, SDNPOutGlue, SDNPInGlue, SDNPSideEffect]>;
def StoreParam :
  SDNode<"NVPTXISD::StoreParam", SDTStoreParamProfile,
         [SDNPHasChain, SDNPOutGlue, SDNPInGlue, SDNPSideEffect]>;
def StoreParamV2 :
  SDNode<"NVPTXISD::StoreParamV2", SDTStoreParamV2Profile,
         [SDNPHasChain, SDNPOutGlue, SDNPInGlue, SDNPSideEffect]>;
def StoreParamV4 :
  SDNode<"NVPTXISD::StoreParamV4", SDTStoreParamV4Profile,
         [SDNPHasChain, SDNPOutGlue, SDNPInGlue, SDNPSideEffect]>;
def StoreParamU32 :
  SDNode<"NVPTXISD::StoreParamU32", SDTStoreParam32Profile,
         [SDNPHasChain, SDNPOutGlue, SDNPInGlue, SDNPSideEffect]>;
def StoreParamS32 :
  SDNode<"NVPTXISD::StoreParamS32", SDTStoreParam32Profile,
         [SDNPHasChain, SDNPOutGlue, SDNPInGlue, SDNPSideEffect]>;
def CallArgBegin :
  SDNode<"NVPTXISD::CallArgBegin", SDTCallArgMarkProfile,
         [SDNPHasChain, SDNPOutGlue, SDNPInGlue, SDNPSideEffect]>;
def CallArg :
  SDNode<"NVPTXISD::CallArg", SDTCallArgProfile,
         [SDNPHasChain, SDNPOutGlue, SDNPInGlue, SDNPSideEffect]>;
def LastCallArg :
  SDNode<"NVPTXISD::LastCallArg", SDTCallArgProfile,
         [SDNPHasChain, SDNPOutGlue, SDNPInGlue, SDNPSideEffect]>;
def CallArgEnd :
  SDNode<"NVPTXISD::CallArgEnd", SDTCallVoidProfile,
         [SDNPHasChain, SDNPOutGlue, SDNPInGlue, SDNPSideEffect]>;
def CallVoid :
  SDNode<"NVPTXISD::CallVoid", SDTCallVoidProfile,
         [SDNPHasChain, SDNPOutGlue, SDNPInGlue, SDNPSideEffect]>;
def Prototype :
  SDNode<"NVPTXISD::Prototype", SDTCallVoidProfile,
         [SDNPHasChain, SDNPOutGlue, SDNPInGlue, SDNPSideEffect]>;
def CallVal :
  SDNode<"NVPTXISD::CallVal", SDTCallValProfile,
         [SDNPHasChain, SDNPOutGlue, SDNPInGlue, SDNPSideEffect]>;
def MoveParam :
  SDNode<"NVPTXISD::MoveParam", SDTMoveParamProfile, []>;
def StoreRetval :
  SDNode<"NVPTXISD::StoreRetval", SDTStoreRetvalProfile,
         [SDNPHasChain, SDNPSideEffect]>;
def StoreRetvalV2 :
  SDNode<"NVPTXISD::StoreRetvalV2", SDTStoreRetvalV2Profile,
         [SDNPHasChain, SDNPSideEffect]>;
def StoreRetvalV4 :
  SDNode<"NVPTXISD::StoreRetvalV4", SDTStoreRetvalV4Profile,
         [SDNPHasChain, SDNPSideEffect]>;
def PseudoUseParam :
  SDNode<"NVPTXISD::PseudoUseParam", SDTPseudoUseParamProfile,
         [SDNPHasChain, SDNPOutGlue, SDNPInGlue, SDNPSideEffect]>;
def RETURNNode :
  SDNode<"NVPTXISD::RETURN", SDTCallArgMarkProfile,
         [SDNPHasChain, SDNPSideEffect]>;
def ProxyReg :
  SDNode<"NVPTXISD::ProxyReg", SDTProxyRegProfile,
         [SDNPHasChain, SDNPOutGlue, SDNPInGlue, SDNPSideEffect]>;

let mayLoad = 1 in {
  class LoadParamMemInst<NVPTXRegClass regclass, string opstr> :
        NVPTXInst<(outs regclass:$dst), (ins i32imm:$b),
                  !strconcat("ld.param", opstr, " \t$dst, [retval0+$b];"),
                  []>;

  class LoadParamV2MemInst<NVPTXRegClass regclass, string opstr> :
        NVPTXInst<(outs regclass:$dst, regclass:$dst2), (ins i32imm:$b),
                  !strconcat("ld.param.v2", opstr,
                             " \t{{$dst, $dst2}}, [retval0+$b];"), []>;

  class LoadParamV4MemInst<NVPTXRegClass regclass, string opstr> :
        NVPTXInst<(outs regclass:$dst, regclass:$dst2, regclass:$dst3,
                        regclass:$dst4),
                  (ins i32imm:$b),
                  !strconcat("ld.param.v4", opstr,
                             " \t{{$dst, $dst2, $dst3, $dst4}}, [retval0+$b];"),
                  []>;
}

class LoadParamRegInst<NVPTXRegClass regclass, string opstr> :
      NVPTXInst<(outs regclass:$dst), (ins i32imm:$b),
                !strconcat("mov", opstr, " \t$dst, retval$b;"),
                [(set regclass:$dst, (LoadParam (i32 0), (i32 imm:$b)))]>;

let mayStore = 1 in {
  class StoreParamInst<NVPTXRegClass regclass, string opstr> :
        NVPTXInst<(outs), (ins regclass:$val, i32imm:$a, i32imm:$b),
                  !strconcat("st.param", opstr, " \t[param$a+$b], $val;"),
                  []>;

  class StoreParamV2Inst<NVPTXRegClass regclass, string opstr> :
        NVPTXInst<(outs), (ins regclass:$val, regclass:$val2,
                               i32imm:$a, i32imm:$b),
                  !strconcat("st.param.v2", opstr,
                             " \t[param$a+$b], {{$val, $val2}};"),
                  []>;

  class StoreParamV4Inst<NVPTXRegClass regclass, string opstr> :
        NVPTXInst<(outs), (ins regclass:$val, regclass:$val2, regclass:$val3,
                               regclass:$val4, i32imm:$a,
                               i32imm:$b),
                  !strconcat("st.param.v4", opstr,
                             " \t[param$a+$b], {{$val, $val2, $val3, $val4}};"),
                  []>;

  class StoreRetvalInst<NVPTXRegClass regclass, string opstr> :
        NVPTXInst<(outs), (ins regclass:$val, i32imm:$a),
                  !strconcat("st.param", opstr, " \t[func_retval0+$a], $val;"),
                  []>;

  class StoreRetvalV2Inst<NVPTXRegClass regclass, string opstr> :
        NVPTXInst<(outs), (ins regclass:$val, regclass:$val2, i32imm:$a),
                  !strconcat("st.param.v2", opstr,
                             " \t[func_retval0+$a], {{$val, $val2}};"),
                  []>;

  class StoreRetvalV4Inst<NVPTXRegClass regclass, string opstr> :
        NVPTXInst<(outs),
                  (ins regclass:$val, regclass:$val2, regclass:$val3,
                       regclass:$val4, i32imm:$a),
                  !strconcat("st.param.v4", opstr,
                             " \t[func_retval0+$a], {{$val, $val2, $val3, $val4}};"),
                  []>;
}

let isCall=1 in {
  multiclass CALL<string OpcStr, SDNode OpNode> {
     def PrintCallNoRetInst : NVPTXInst<(outs), (ins),
       !strconcat(OpcStr, " "), [(OpNode (i32 0))]>;
     def PrintCallRetInst1 : NVPTXInst<(outs), (ins),
       !strconcat(OpcStr, " (retval0), "), [(OpNode (i32 1))]>;
     def PrintCallRetInst2 : NVPTXInst<(outs), (ins),
       !strconcat(OpcStr, " (retval0, retval1), "), [(OpNode (i32 2))]>;
     def PrintCallRetInst3 : NVPTXInst<(outs), (ins),
       !strconcat(OpcStr, " (retval0, retval1, retval2), "), [(OpNode (i32 3))]>;
     def PrintCallRetInst4 : NVPTXInst<(outs), (ins),
       !strconcat(OpcStr, " (retval0, retval1, retval2, retval3), "),
       [(OpNode (i32 4))]>;
     def PrintCallRetInst5 : NVPTXInst<(outs), (ins),
       !strconcat(OpcStr, " (retval0, retval1, retval2, retval3, retval4), "),
       [(OpNode (i32 5))]>;
     def PrintCallRetInst6 : NVPTXInst<(outs), (ins),
       !strconcat(OpcStr, " (retval0, retval1, retval2, retval3, retval4, "
                            "retval5), "),
       [(OpNode (i32 6))]>;
     def PrintCallRetInst7 : NVPTXInst<(outs), (ins),
       !strconcat(OpcStr, " (retval0, retval1, retval2, retval3, retval4, "
                            "retval5, retval6), "),
       [(OpNode (i32 7))]>;
     def PrintCallRetInst8 : NVPTXInst<(outs), (ins),
       !strconcat(OpcStr, " (retval0, retval1, retval2, retval3, retval4, "
                            "retval5, retval6, retval7), "),
       [(OpNode (i32 8))]>;
  }
}

defm Call : CALL<"call", PrintCall>;
defm CallUni : CALL<"call.uni", PrintCallUni>;

// Convergent call instructions.  These are identical to regular calls, except
// they have the isConvergent bit set.
let isConvergent=1 in {
  defm ConvergentCall : CALL<"call", PrintConvergentCall>;
  defm ConvergentCallUni : CALL<"call.uni", PrintConvergentCallUni>;
}

def LoadParamMemI64    : LoadParamMemInst<Int64Regs, ".b64">;
def LoadParamMemI32    : LoadParamMemInst<Int32Regs, ".b32">;
def LoadParamMemI16    : LoadParamMemInst<Int16Regs, ".b16">;
def LoadParamMemI8     : LoadParamMemInst<Int16Regs, ".b8">;
def LoadParamMemV2I64  : LoadParamV2MemInst<Int64Regs, ".b64">;
def LoadParamMemV2I32  : LoadParamV2MemInst<Int32Regs, ".b32">;
def LoadParamMemV2I16  : LoadParamV2MemInst<Int16Regs, ".b16">;
def LoadParamMemV2I8   : LoadParamV2MemInst<Int16Regs, ".b8">;
def LoadParamMemV4I32  : LoadParamV4MemInst<Int32Regs, ".b32">;
def LoadParamMemV4I16  : LoadParamV4MemInst<Int16Regs, ".b16">;
def LoadParamMemV4I8   : LoadParamV4MemInst<Int16Regs, ".b8">;
def LoadParamMemF16    : LoadParamMemInst<Float16Regs, ".b16">;
def LoadParamMemF16x2  : LoadParamMemInst<Float16x2Regs, ".b32">;
def LoadParamMemF32    : LoadParamMemInst<Float32Regs, ".f32">;
def LoadParamMemF64    : LoadParamMemInst<Float64Regs, ".f64">;
def LoadParamMemV2F16  : LoadParamV2MemInst<Float16Regs, ".b16">;
def LoadParamMemV2F16x2: LoadParamV2MemInst<Float16x2Regs, ".b32">;
def LoadParamMemV2F32  : LoadParamV2MemInst<Float32Regs, ".f32">;
def LoadParamMemV2F64  : LoadParamV2MemInst<Float64Regs, ".f64">;
def LoadParamMemV4F16  : LoadParamV4MemInst<Float16Regs, ".b16">;
def LoadParamMemV4F16x2: LoadParamV4MemInst<Float16x2Regs, ".b32">;
def LoadParamMemV4F32  : LoadParamV4MemInst<Float32Regs, ".f32">;

def StoreParamI64    : StoreParamInst<Int64Regs, ".b64">;
def StoreParamI32    : StoreParamInst<Int32Regs, ".b32">;

def StoreParamI16    : StoreParamInst<Int16Regs, ".b16">;
def StoreParamI8     : StoreParamInst<Int16Regs, ".b8">;
def StoreParamV2I64  : StoreParamV2Inst<Int64Regs, ".b64">;
def StoreParamV2I32  : StoreParamV2Inst<Int32Regs, ".b32">;
def StoreParamV2I16  : StoreParamV2Inst<Int16Regs, ".b16">;
def StoreParamV2I8   : StoreParamV2Inst<Int16Regs, ".b8">;

def StoreParamV4I32  : StoreParamV4Inst<Int32Regs, ".b32">;
def StoreParamV4I16  : StoreParamV4Inst<Int16Regs, ".b16">;
def StoreParamV4I8   : StoreParamV4Inst<Int16Regs, ".b8">;

def StoreParamF16      : StoreParamInst<Float16Regs, ".b16">;
def StoreParamF16x2    : StoreParamInst<Float16x2Regs, ".b32">;
def StoreParamF32      : StoreParamInst<Float32Regs, ".f32">;
def StoreParamF64      : StoreParamInst<Float64Regs, ".f64">;
def StoreParamV2F16    : StoreParamV2Inst<Float16Regs, ".b16">;
def StoreParamV2F16x2  : StoreParamV2Inst<Float16x2Regs, ".b32">;
def StoreParamV2F32    : StoreParamV2Inst<Float32Regs, ".f32">;
def StoreParamV2F64    : StoreParamV2Inst<Float64Regs, ".f64">;
def StoreParamV4F16    : StoreParamV4Inst<Float16Regs, ".b16">;
def StoreParamV4F16x2  : StoreParamV4Inst<Float16x2Regs, ".b32">;
def StoreParamV4F32    : StoreParamV4Inst<Float32Regs, ".f32">;

def StoreRetvalI64    : StoreRetvalInst<Int64Regs, ".b64">;
def StoreRetvalI32    : StoreRetvalInst<Int32Regs, ".b32">;
def StoreRetvalI16    : StoreRetvalInst<Int16Regs, ".b16">;
def StoreRetvalI8     : StoreRetvalInst<Int16Regs, ".b8">;
def StoreRetvalV2I64  : StoreRetvalV2Inst<Int64Regs, ".b64">;
def StoreRetvalV2I32  : StoreRetvalV2Inst<Int32Regs, ".b32">;
def StoreRetvalV2I16  : StoreRetvalV2Inst<Int16Regs, ".b16">;
def StoreRetvalV2I8   : StoreRetvalV2Inst<Int16Regs, ".b8">;
def StoreRetvalV4I32  : StoreRetvalV4Inst<Int32Regs, ".b32">;
def StoreRetvalV4I16  : StoreRetvalV4Inst<Int16Regs, ".b16">;
def StoreRetvalV4I8   : StoreRetvalV4Inst<Int16Regs, ".b8">;

def StoreRetvalF64    : StoreRetvalInst<Float64Regs, ".f64">;
def StoreRetvalF32    : StoreRetvalInst<Float32Regs, ".f32">;
def StoreRetvalF16    : StoreRetvalInst<Float16Regs, ".b16">;
def StoreRetvalF16x2  : StoreRetvalInst<Float16x2Regs, ".b32">;
def StoreRetvalV2F64  : StoreRetvalV2Inst<Float64Regs, ".f64">;
def StoreRetvalV2F32  : StoreRetvalV2Inst<Float32Regs, ".f32">;
def StoreRetvalV2F16  : StoreRetvalV2Inst<Float16Regs, ".b16">;
def StoreRetvalV2F16x2: StoreRetvalV2Inst<Float16x2Regs, ".b32">;
def StoreRetvalV4F32  : StoreRetvalV4Inst<Float32Regs, ".f32">;
def StoreRetvalV4F16  : StoreRetvalV4Inst<Float16Regs, ".b16">;
def StoreRetvalV4F16x2: StoreRetvalV4Inst<Float16x2Regs, ".b32">;

def CallArgBeginInst : NVPTXInst<(outs), (ins), "(", [(CallArgBegin)]>;
def CallArgEndInst1  : NVPTXInst<(outs), (ins), ");", [(CallArgEnd (i32 1))]>;
def CallArgEndInst0  : NVPTXInst<(outs), (ins), ")", [(CallArgEnd (i32 0))]>;
def RETURNInst       : NVPTXInst<(outs), (ins), "ret;", [(RETURNNode)]>;

class CallArgInst<NVPTXRegClass regclass> :
  NVPTXInst<(outs), (ins regclass:$a), "$a, ",
            [(CallArg (i32 0), regclass:$a)]>;

class LastCallArgInst<NVPTXRegClass regclass> :
  NVPTXInst<(outs), (ins regclass:$a), "$a",
            [(LastCallArg (i32 0), regclass:$a)]>;

def CallArgI64     : CallArgInst<Int64Regs>;
def CallArgI32     : CallArgInst<Int32Regs>;
def CallArgI16     : CallArgInst<Int16Regs>;
def CallArgF64     : CallArgInst<Float64Regs>;
def CallArgF32     : CallArgInst<Float32Regs>;

def LastCallArgI64 : LastCallArgInst<Int64Regs>;
def LastCallArgI32 : LastCallArgInst<Int32Regs>;
def LastCallArgI16 : LastCallArgInst<Int16Regs>;
def LastCallArgF64 : LastCallArgInst<Float64Regs>;
def LastCallArgF32 : LastCallArgInst<Float32Regs>;

def CallArgI32imm : NVPTXInst<(outs), (ins i32imm:$a), "$a, ",
                              [(CallArg (i32 0), (i32 imm:$a))]>;
def LastCallArgI32imm : NVPTXInst<(outs), (ins i32imm:$a), "$a",
                                  [(LastCallArg (i32 0), (i32 imm:$a))]>;

def CallArgParam : NVPTXInst<(outs), (ins i32imm:$a), "param$a, ",
                             [(CallArg (i32 1), (i32 imm:$a))]>;
def LastCallArgParam : NVPTXInst<(outs), (ins i32imm:$a), "param$a",
                                 [(LastCallArg (i32 1), (i32 imm:$a))]>;

def CallVoidInst :      NVPTXInst<(outs), (ins imem:$addr), "$addr, ",
                                  [(CallVoid (Wrapper tglobaladdr:$addr))]>;
def CallVoidInstReg :   NVPTXInst<(outs), (ins Int32Regs:$addr), "$addr, ",
                                  [(CallVoid Int32Regs:$addr)]>;
def CallVoidInstReg64 : NVPTXInst<(outs), (ins Int64Regs:$addr), "$addr, ",
                                  [(CallVoid Int64Regs:$addr)]>;
def PrototypeInst :     NVPTXInst<(outs), (ins i32imm:$val), ", prototype_$val;",
                                  [(Prototype (i32 imm:$val))]>;

def DeclareRetMemInst :
  NVPTXInst<(outs), (ins i32imm:$align, i32imm:$size, i32imm:$num),
            ".param .align $align .b8 retval$num[$size];",
            [(DeclareRetParam (i32 imm:$align), (i32 imm:$size), (i32 imm:$num))]>;
def DeclareRetScalarInst :
  NVPTXInst<(outs), (ins i32imm:$size, i32imm:$num),
            ".param .b$size retval$num;",
            [(DeclareRet (i32 1), (i32 imm:$size), (i32 imm:$num))]>;
def DeclareRetRegInst :
  NVPTXInst<(outs), (ins i32imm:$size, i32imm:$num),
            ".reg .b$size retval$num;",
            [(DeclareRet (i32 2), (i32 imm:$size), (i32 imm:$num))]>;

def DeclareParamInst :
  NVPTXInst<(outs), (ins i32imm:$align, i32imm:$a, i32imm:$size),
            ".param .align $align .b8 param$a[$size];",
            [(DeclareParam (i32 imm:$align), (i32 imm:$a), (i32 imm:$size))]>;
def DeclareScalarParamInst :
  NVPTXInst<(outs), (ins i32imm:$a, i32imm:$size),
            ".param .b$size param$a;",
            [(DeclareScalarParam (i32 imm:$a), (i32 imm:$size), (i32 0))]>;
def DeclareScalarRegInst :
  NVPTXInst<(outs), (ins i32imm:$a, i32imm:$size),
            ".reg .b$size param$a;",
            [(DeclareScalarParam (i32 imm:$a), (i32 imm:$size), (i32 1))]>;

class MoveParamInst<NVPTXRegClass regclass, string asmstr> :
  NVPTXInst<(outs regclass:$dst), (ins regclass:$src),
            !strconcat("mov", asmstr, " \t$dst, $src;"),
            [(set regclass:$dst, (MoveParam regclass:$src))]>;

def MoveParamI64 : MoveParamInst<Int64Regs, ".b64">;
def MoveParamI32 : MoveParamInst<Int32Regs, ".b32">;
def MoveParamI16 :
  NVPTXInst<(outs Int16Regs:$dst), (ins Int16Regs:$src),
            "cvt.u16.u32 \t$dst, $src;",
            [(set Int16Regs:$dst, (MoveParam Int16Regs:$src))]>;
def MoveParamF64 : MoveParamInst<Float64Regs, ".f64">;
def MoveParamF32 : MoveParamInst<Float32Regs, ".f32">;
def MoveParamF16 : MoveParamInst<Float16Regs, ".f16">;

class PseudoUseParamInst<NVPTXRegClass regclass> :
  NVPTXInst<(outs), (ins regclass:$src),
            "// Pseudo use of $src",
            [(PseudoUseParam regclass:$src)]>;

def PseudoUseParamI64 : PseudoUseParamInst<Int64Regs>;
def PseudoUseParamI32 : PseudoUseParamInst<Int32Regs>;
def PseudoUseParamI16 : PseudoUseParamInst<Int16Regs>;
def PseudoUseParamF64 : PseudoUseParamInst<Float64Regs>;
def PseudoUseParamF32 : PseudoUseParamInst<Float32Regs>;

class ProxyRegInst<string SzStr, NVPTXRegClass regclass> :
  NVPTXInst<(outs regclass:$dst), (ins regclass:$src),
            !strconcat("mov.", SzStr, " \t$dst, $src;"),
            [(set regclass:$dst, (ProxyReg regclass:$src))]>;

let isCodeGenOnly=1, isPseudo=1 in {
  def ProxyRegI1    : ProxyRegInst<"pred", Int1Regs>;
  def ProxyRegI16   : ProxyRegInst<"b16",  Int16Regs>;
  def ProxyRegI32   : ProxyRegInst<"b32",  Int32Regs>;
  def ProxyRegI64   : ProxyRegInst<"b64",  Int64Regs>;
  def ProxyRegF16   : ProxyRegInst<"b16",  Float16Regs>;
  def ProxyRegF32   : ProxyRegInst<"f32",  Float32Regs>;
  def ProxyRegF64   : ProxyRegInst<"f64",  Float64Regs>;
  def ProxyRegF16x2 : ProxyRegInst<"b32",  Float16x2Regs>;
}

//
// Load / Store Handling
//
multiclass LD<NVPTXRegClass regclass> {
  def _avar : NVPTXInst<
    (outs regclass:$dst),
    (ins LdStCode:$isVol, LdStCode:$addsp, LdStCode:$Vec, LdStCode:$Sign,
         i32imm:$fromWidth, imem:$addr),
    "ld${isVol:volatile}${addsp:addsp}${Vec:vec}.${Sign:sign}$fromWidth "
    "\t$dst, [$addr];", []>;
  def _areg : NVPTXInst<
    (outs regclass:$dst),
    (ins LdStCode:$isVol, LdStCode:$addsp, LdStCode:$Vec, LdStCode:$Sign,
         i32imm:$fromWidth, Int32Regs:$addr),
    "ld${isVol:volatile}${addsp:addsp}${Vec:vec}.${Sign:sign}$fromWidth "
    "\t$dst, [$addr];", []>;
  def _areg_64 : NVPTXInst<
    (outs regclass:$dst),
    (ins LdStCode:$isVol, LdStCode:$addsp, LdStCode:$Vec, LdStCode:$Sign,
         i32imm:$fromWidth, Int64Regs:$addr),
    "ld${isVol:volatile}${addsp:addsp}${Vec:vec}.${Sign:sign}$fromWidth "
    "\t$dst, [$addr];", []>;
  def _ari : NVPTXInst<
    (outs regclass:$dst),
    (ins LdStCode:$isVol, LdStCode:$addsp, LdStCode:$Vec, LdStCode:$Sign,
         i32imm:$fromWidth, Int32Regs:$addr, i32imm:$offset),
    "ld${isVol:volatile}${addsp:addsp}${Vec:vec}.${Sign:sign}$fromWidth "
    "\t$dst, [$addr+$offset];", []>;
  def _ari_64 : NVPTXInst<
    (outs regclass:$dst),
    (ins LdStCode:$isVol, LdStCode:$addsp, LdStCode:$Vec,
         LdStCode:$Sign, i32imm:$fromWidth, Int64Regs:$addr, i32imm:$offset),
    "ld${isVol:volatile}${addsp:addsp}${Vec:vec}.${Sign:sign}$fromWidth "
    "\t$dst, [$addr+$offset];", []>;
  def _asi : NVPTXInst<
    (outs regclass:$dst),
    (ins LdStCode:$isVol, LdStCode:$addsp, LdStCode:$Vec,
         LdStCode:$Sign, i32imm:$fromWidth, imem:$addr, i32imm:$offset),
    "ld${isVol:volatile}${addsp:addsp}${Vec:vec}.${Sign:sign}$fromWidth "
    "\t$dst, [$addr+$offset];", []>;
}

let mayLoad=1, hasSideEffects=0 in {
  defm LD_i8  : LD<Int16Regs>;
  defm LD_i16 : LD<Int16Regs>;
  defm LD_i32 : LD<Int32Regs>;
  defm LD_i64 : LD<Int64Regs>;
  defm LD_f16 : LD<Float16Regs>;
  defm LD_f16x2 : LD<Float16x2Regs>;
  defm LD_f32 : LD<Float32Regs>;
  defm LD_f64 : LD<Float64Regs>;
}

multiclass ST<NVPTXRegClass regclass> {
  def _avar : NVPTXInst<
    (outs),
    (ins regclass:$src, LdStCode:$isVol, LdStCode:$addsp, LdStCode:$Vec,
         LdStCode:$Sign, i32imm:$toWidth, imem:$addr),
    "st${isVol:volatile}${addsp:addsp}${Vec:vec}.${Sign:sign}$toWidth"
    " \t[$addr], $src;", []>;
  def _areg : NVPTXInst<
    (outs),
    (ins regclass:$src, LdStCode:$isVol, LdStCode:$addsp,
         LdStCode:$Vec, LdStCode:$Sign, i32imm:$toWidth, Int32Regs:$addr),
    "st${isVol:volatile}${addsp:addsp}${Vec:vec}.${Sign:sign}$toWidth"
    " \t[$addr], $src;", []>;
  def _areg_64 : NVPTXInst<
    (outs),
    (ins regclass:$src, LdStCode:$isVol, LdStCode:$addsp, LdStCode:$Vec,
         LdStCode:$Sign, i32imm:$toWidth, Int64Regs:$addr),
    "st${isVol:volatile}${addsp:addsp}${Vec:vec}.${Sign:sign}$toWidth"
    " \t[$addr], $src;", []>;
  def _ari : NVPTXInst<
    (outs),
    (ins regclass:$src, LdStCode:$isVol, LdStCode:$addsp, LdStCode:$Vec,
         LdStCode:$Sign, i32imm:$toWidth, Int32Regs:$addr, i32imm:$offset),
    "st${isVol:volatile}${addsp:addsp}${Vec:vec}.${Sign:sign}$toWidth"
    " \t[$addr+$offset], $src;", []>;
  def _ari_64 : NVPTXInst<
    (outs),
    (ins regclass:$src, LdStCode:$isVol, LdStCode:$addsp, LdStCode:$Vec,
         LdStCode:$Sign, i32imm:$toWidth, Int64Regs:$addr, i32imm:$offset),
    "st${isVol:volatile}${addsp:addsp}${Vec:vec}.${Sign:sign}$toWidth"
    " \t[$addr+$offset], $src;", []>;
  def _asi : NVPTXInst<
    (outs),
    (ins regclass:$src, LdStCode:$isVol, LdStCode:$addsp, LdStCode:$Vec,
         LdStCode:$Sign, i32imm:$toWidth, imem:$addr, i32imm:$offset),
    "st${isVol:volatile}${addsp:addsp}${Vec:vec}.${Sign:sign}$toWidth"
    " \t[$addr+$offset], $src;", []>;
}

let mayStore=1, hasSideEffects=0 in {
  defm ST_i8  : ST<Int16Regs>;
  defm ST_i16 : ST<Int16Regs>;
  defm ST_i32 : ST<Int32Regs>;
  defm ST_i64 : ST<Int64Regs>;
  defm ST_f16 : ST<Float16Regs>;
  defm ST_f16x2 : ST<Float16x2Regs>;
  defm ST_f32 : ST<Float32Regs>;
  defm ST_f64 : ST<Float64Regs>;
}

// The following is used only in and after vector elementizations.  Vector
// elementization happens at the machine instruction level, so the following
// instructions never appear in the DAG.
multiclass LD_VEC<NVPTXRegClass regclass> {
  def _v2_avar : NVPTXInst<
    (outs regclass:$dst1, regclass:$dst2),
    (ins LdStCode:$isVol, LdStCode:$addsp, LdStCode:$Vec, LdStCode:$Sign,
         i32imm:$fromWidth, imem:$addr),
    "ld${isVol:volatile}${addsp:addsp}${Vec:vec}.${Sign:sign}$fromWidth "
    "\t{{$dst1, $dst2}}, [$addr];", []>;
  def _v2_areg : NVPTXInst<
    (outs regclass:$dst1, regclass:$dst2),
    (ins LdStCode:$isVol, LdStCode:$addsp, LdStCode:$Vec, LdStCode:$Sign,
         i32imm:$fromWidth, Int32Regs:$addr),
    "ld${isVol:volatile}${addsp:addsp}${Vec:vec}.${Sign:sign}$fromWidth "
    "\t{{$dst1, $dst2}}, [$addr];", []>;
  def _v2_areg_64 : NVPTXInst<
    (outs regclass:$dst1, regclass:$dst2),
    (ins LdStCode:$isVol, LdStCode:$addsp, LdStCode:$Vec, LdStCode:$Sign,
         i32imm:$fromWidth, Int64Regs:$addr),
    "ld${isVol:volatile}${addsp:addsp}${Vec:vec}.${Sign:sign}$fromWidth "
    "\t{{$dst1, $dst2}}, [$addr];", []>;
  def _v2_ari : NVPTXInst<
    (outs regclass:$dst1, regclass:$dst2),
    (ins LdStCode:$isVol, LdStCode:$addsp, LdStCode:$Vec, LdStCode:$Sign,
         i32imm:$fromWidth, Int32Regs:$addr, i32imm:$offset),
    "ld${isVol:volatile}${addsp:addsp}${Vec:vec}.${Sign:sign}$fromWidth "
    "\t{{$dst1, $dst2}}, [$addr+$offset];", []>;
  def _v2_ari_64 : NVPTXInst<
    (outs regclass:$dst1, regclass:$dst2),
    (ins LdStCode:$isVol, LdStCode:$addsp, LdStCode:$Vec, LdStCode:$Sign,
         i32imm:$fromWidth, Int64Regs:$addr, i32imm:$offset),
    "ld${isVol:volatile}${addsp:addsp}${Vec:vec}.${Sign:sign}$fromWidth "
    "\t{{$dst1, $dst2}}, [$addr+$offset];", []>;
  def _v2_asi : NVPTXInst<
    (outs regclass:$dst1, regclass:$dst2),
    (ins LdStCode:$isVol, LdStCode:$addsp, LdStCode:$Vec, LdStCode:$Sign,
         i32imm:$fromWidth, imem:$addr, i32imm:$offset),
    "ld${isVol:volatile}${addsp:addsp}${Vec:vec}.${Sign:sign}$fromWidth "
    "\t{{$dst1, $dst2}}, [$addr+$offset];", []>;
  def _v4_avar : NVPTXInst<
    (outs regclass:$dst1, regclass:$dst2, regclass:$dst3, regclass:$dst4),
    (ins LdStCode:$isVol, LdStCode:$addsp, LdStCode:$Vec, LdStCode:$Sign,
         i32imm:$fromWidth, imem:$addr),
    "ld${isVol:volatile}${addsp:addsp}${Vec:vec}.${Sign:sign}$fromWidth "
    "\t{{$dst1, $dst2, $dst3, $dst4}}, [$addr];", []>;
  def _v4_areg : NVPTXInst<
    (outs regclass:$dst1, regclass:$dst2, regclass:$dst3, regclass:$dst4),
    (ins LdStCode:$isVol, LdStCode:$addsp, LdStCode:$Vec, LdStCode:$Sign,
         i32imm:$fromWidth, Int32Regs:$addr),
    "ld${isVol:volatile}${addsp:addsp}${Vec:vec}.${Sign:sign}$fromWidth "
    "\t{{$dst1, $dst2, $dst3, $dst4}}, [$addr];", []>;
  def _v4_areg_64 : NVPTXInst<
    (outs regclass:$dst1, regclass:$dst2, regclass:$dst3, regclass:$dst4),
    (ins LdStCode:$isVol, LdStCode:$addsp, LdStCode:$Vec, LdStCode:$Sign,
         i32imm:$fromWidth, Int64Regs:$addr),
    "ld${isVol:volatile}${addsp:addsp}${Vec:vec}.${Sign:sign}$fromWidth "
    "\t{{$dst1, $dst2, $dst3, $dst4}}, [$addr];", []>;
  def _v4_ari : NVPTXInst<
    (outs regclass:$dst1, regclass:$dst2, regclass:$dst3, regclass:$dst4),
    (ins LdStCode:$isVol, LdStCode:$addsp, LdStCode:$Vec, LdStCode:$Sign,
         i32imm:$fromWidth, Int32Regs:$addr, i32imm:$offset),
    "ld${isVol:volatile}${addsp:addsp}${Vec:vec}.${Sign:sign}$fromWidth "
    "\t{{$dst1, $dst2, $dst3, $dst4}}, [$addr+$offset];", []>;
  def _v4_ari_64 : NVPTXInst<
    (outs regclass:$dst1, regclass:$dst2, regclass:$dst3, regclass:$dst4),
    (ins LdStCode:$isVol, LdStCode:$addsp, LdStCode:$Vec, LdStCode:$Sign,
         i32imm:$fromWidth, Int64Regs:$addr, i32imm:$offset),
    "ld${isVol:volatile}${addsp:addsp}${Vec:vec}.${Sign:sign}$fromWidth "
    "\t{{$dst1, $dst2, $dst3, $dst4}}, [$addr+$offset];", []>;
  def _v4_asi : NVPTXInst<
    (outs regclass:$dst1, regclass:$dst2, regclass:$dst3, regclass:$dst4),
    (ins LdStCode:$isVol, LdStCode:$addsp, LdStCode:$Vec, LdStCode:$Sign,
         i32imm:$fromWidth, imem:$addr, i32imm:$offset),
    "ld${isVol:volatile}${addsp:addsp}${Vec:vec}.${Sign:sign}$fromWidth "
    "\t{{$dst1, $dst2, $dst3, $dst4}}, [$addr+$offset];", []>;
}
let mayLoad=1, hasSideEffects=0 in {
  defm LDV_i8  : LD_VEC<Int16Regs>;
  defm LDV_i16 : LD_VEC<Int16Regs>;
  defm LDV_i32 : LD_VEC<Int32Regs>;
  defm LDV_i64 : LD_VEC<Int64Regs>;
  defm LDV_f16 : LD_VEC<Float16Regs>;
  defm LDV_f16x2 : LD_VEC<Float16x2Regs>;
  defm LDV_f32 : LD_VEC<Float32Regs>;
  defm LDV_f64 : LD_VEC<Float64Regs>;
}

multiclass ST_VEC<NVPTXRegClass regclass> {
  def _v2_avar : NVPTXInst<
    (outs),
    (ins regclass:$src1, regclass:$src2, LdStCode:$isVol, LdStCode:$addsp,
         LdStCode:$Vec, LdStCode:$Sign, i32imm:$fromWidth, imem:$addr),
    "st${isVol:volatile}${addsp:addsp}${Vec:vec}.${Sign:sign}$fromWidth "
    "\t[$addr], {{$src1, $src2}};", []>;
  def _v2_areg : NVPTXInst<
    (outs),
    (ins regclass:$src1, regclass:$src2, LdStCode:$isVol, LdStCode:$addsp,
         LdStCode:$Vec, LdStCode:$Sign, i32imm:$fromWidth, Int32Regs:$addr),
    "st${isVol:volatile}${addsp:addsp}${Vec:vec}.${Sign:sign}$fromWidth "
    "\t[$addr], {{$src1, $src2}};", []>;
  def _v2_areg_64 : NVPTXInst<
    (outs),
    (ins regclass:$src1, regclass:$src2, LdStCode:$isVol, LdStCode:$addsp,
         LdStCode:$Vec, LdStCode:$Sign, i32imm:$fromWidth, Int64Regs:$addr),
    "st${isVol:volatile}${addsp:addsp}${Vec:vec}.${Sign:sign}$fromWidth "
    "\t[$addr], {{$src1, $src2}};", []>;
  def _v2_ari : NVPTXInst<
    (outs),
    (ins regclass:$src1, regclass:$src2, LdStCode:$isVol, LdStCode:$addsp,
         LdStCode:$Vec, LdStCode:$Sign, i32imm:$fromWidth, Int32Regs:$addr,
         i32imm:$offset),
    "st${isVol:volatile}${addsp:addsp}${Vec:vec}.${Sign:sign}$fromWidth "
    "\t[$addr+$offset], {{$src1, $src2}};", []>;
  def _v2_ari_64 : NVPTXInst<
    (outs),
    (ins regclass:$src1, regclass:$src2, LdStCode:$isVol, LdStCode:$addsp,
         LdStCode:$Vec, LdStCode:$Sign, i32imm:$fromWidth, Int64Regs:$addr,
         i32imm:$offset),
    "st${isVol:volatile}${addsp:addsp}${Vec:vec}.${Sign:sign}$fromWidth "
    "\t[$addr+$offset], {{$src1, $src2}};", []>;
  def _v2_asi : NVPTXInst<
    (outs),
    (ins regclass:$src1, regclass:$src2, LdStCode:$isVol, LdStCode:$addsp,
         LdStCode:$Vec, LdStCode:$Sign, i32imm:$fromWidth, imem:$addr,
         i32imm:$offset),
    "st${isVol:volatile}${addsp:addsp}${Vec:vec}.${Sign:sign}$fromWidth "
    "\t[$addr+$offset], {{$src1, $src2}};", []>;
  def _v4_avar : NVPTXInst<
    (outs),
    (ins regclass:$src1, regclass:$src2, regclass:$src3, regclass:$src4,
         LdStCode:$isVol, LdStCode:$addsp, LdStCode:$Vec, LdStCode:$Sign,
         i32imm:$fromWidth, imem:$addr),
    "st${isVol:volatile}${addsp:addsp}${Vec:vec}.${Sign:sign}$fromWidth "
    "\t[$addr], {{$src1, $src2, $src3, $src4}};", []>;
  def _v4_areg : NVPTXInst<
    (outs),
    (ins regclass:$src1, regclass:$src2, regclass:$src3, regclass:$src4,
         LdStCode:$isVol, LdStCode:$addsp, LdStCode:$Vec, LdStCode:$Sign,
         i32imm:$fromWidth, Int32Regs:$addr),
    "st${isVol:volatile}${addsp:addsp}${Vec:vec}.${Sign:sign}$fromWidth "
    "\t[$addr], {{$src1, $src2, $src3, $src4}};", []>;
  def _v4_areg_64 : NVPTXInst<
    (outs),
    (ins regclass:$src1, regclass:$src2, regclass:$src3, regclass:$src4,
         LdStCode:$isVol, LdStCode:$addsp, LdStCode:$Vec, LdStCode:$Sign,
         i32imm:$fromWidth, Int64Regs:$addr),
    "st${isVol:volatile}${addsp:addsp}${Vec:vec}.${Sign:sign}$fromWidth "
    "\t[$addr], {{$src1, $src2, $src3, $src4}};", []>;
  def _v4_ari : NVPTXInst<
    (outs),
    (ins regclass:$src1, regclass:$src2, regclass:$src3, regclass:$src4,
         LdStCode:$isVol, LdStCode:$addsp, LdStCode:$Vec, LdStCode:$Sign,
         i32imm:$fromWidth, Int32Regs:$addr, i32imm:$offset),
    "st${isVol:volatile}${addsp:addsp}${Vec:vec}.${Sign:sign}$fromWidth "
    "\t[$addr+$offset], {{$src1, $src2, $src3, $src4}};", []>;
  def _v4_ari_64 : NVPTXInst<
    (outs),
    (ins regclass:$src1, regclass:$src2, regclass:$src3, regclass:$src4,
         LdStCode:$isVol, LdStCode:$addsp, LdStCode:$Vec, LdStCode:$Sign,
         i32imm:$fromWidth, Int64Regs:$addr, i32imm:$offset),
    "st${isVol:volatile}${addsp:addsp}${Vec:vec}.${Sign:sign}$fromWidth "
    "\t[$addr+$offset], {{$src1, $src2, $src3, $src4}};", []>;
  def _v4_asi : NVPTXInst<
    (outs),
    (ins regclass:$src1, regclass:$src2, regclass:$src3, regclass:$src4,
         LdStCode:$isVol, LdStCode:$addsp, LdStCode:$Vec, LdStCode:$Sign,
         i32imm:$fromWidth, imem:$addr, i32imm:$offset),
    "st${isVol:volatile}${addsp:addsp}${Vec:vec}.${Sign:sign}"
    "$fromWidth \t[$addr+$offset], {{$src1, $src2, $src3, $src4}};", []>;
}

let mayStore=1, hasSideEffects=0 in {
  defm STV_i8  : ST_VEC<Int16Regs>;
  defm STV_i16 : ST_VEC<Int16Regs>;
  defm STV_i32 : ST_VEC<Int32Regs>;
  defm STV_i64 : ST_VEC<Int64Regs>;
  defm STV_f16 : ST_VEC<Float16Regs>;
  defm STV_f16x2 : ST_VEC<Float16x2Regs>;
  defm STV_f32 : ST_VEC<Float32Regs>;
  defm STV_f64 : ST_VEC<Float64Regs>;
}

//---- Conversion ----

class F_BITCONVERT<string SzStr, NVPTXRegClass regclassIn,
  NVPTXRegClass regclassOut> :
           NVPTXInst<(outs regclassOut:$d), (ins regclassIn:$a),
           !strconcat("mov.b", SzStr, " \t$d, $a;"),
     [(set regclassOut:$d, (bitconvert regclassIn:$a))]>;

def BITCONVERT_16_I2F : F_BITCONVERT<"16", Int16Regs, Float16Regs>;
def BITCONVERT_16_F2I : F_BITCONVERT<"16", Float16Regs, Int16Regs>;
def BITCONVERT_32_I2F : F_BITCONVERT<"32", Int32Regs, Float32Regs>;
def BITCONVERT_32_F2I : F_BITCONVERT<"32", Float32Regs, Int32Regs>;
def BITCONVERT_64_I2F : F_BITCONVERT<"64", Int64Regs, Float64Regs>;
def BITCONVERT_64_F2I : F_BITCONVERT<"64", Float64Regs, Int64Regs>;
def BITCONVERT_32_I2F16x2 : F_BITCONVERT<"32", Int32Regs, Float16x2Regs>;
def BITCONVERT_32_F16x22I : F_BITCONVERT<"32", Float16x2Regs, Int32Regs>;

// NOTE: pred->fp are currently sub-optimal due to an issue in TableGen where
// we cannot specify floating-point literals in isel patterns.  Therefore, we
// use an integer selp to select either 1 or 0 and then cvt to floating-point.

// sint -> f16
def : Pat<(f16 (sint_to_fp Int1Regs:$a)),
          (CVT_f16_s32 (SELP_u32ii 1, 0, Int1Regs:$a), CvtRN)>;
def : Pat<(f16 (sint_to_fp Int16Regs:$a)),
          (CVT_f16_s16 Int16Regs:$a, CvtRN)>;
def : Pat<(f16 (sint_to_fp Int32Regs:$a)),
          (CVT_f16_s32 Int32Regs:$a, CvtRN)>;
def : Pat<(f16 (sint_to_fp Int64Regs:$a)),
          (CVT_f16_s64 Int64Regs:$a, CvtRN)>;

// uint -> f16
def : Pat<(f16 (uint_to_fp Int1Regs:$a)),
          (CVT_f16_u32 (SELP_u32ii 1, 0, Int1Regs:$a), CvtRN)>;
def : Pat<(f16 (uint_to_fp Int16Regs:$a)),
          (CVT_f16_u16 Int16Regs:$a, CvtRN)>;
def : Pat<(f16 (uint_to_fp Int32Regs:$a)),
          (CVT_f16_u32 Int32Regs:$a, CvtRN)>;
def : Pat<(f16 (uint_to_fp Int64Regs:$a)),
          (CVT_f16_u64 Int64Regs:$a, CvtRN)>;

// sint -> f32
def : Pat<(f32 (sint_to_fp Int1Regs:$a)),
          (CVT_f32_s32 (SELP_u32ii 1, 0, Int1Regs:$a), CvtRN)>;
def : Pat<(f32 (sint_to_fp Int16Regs:$a)),
          (CVT_f32_s16 Int16Regs:$a, CvtRN)>;
def : Pat<(f32 (sint_to_fp Int32Regs:$a)),
          (CVT_f32_s32 Int32Regs:$a, CvtRN)>;
def : Pat<(f32 (sint_to_fp Int64Regs:$a)),
          (CVT_f32_s64 Int64Regs:$a, CvtRN)>;

// uint -> f32
def : Pat<(f32 (uint_to_fp Int1Regs:$a)),
          (CVT_f32_u32 (SELP_u32ii 1, 0, Int1Regs:$a), CvtRN)>;
def : Pat<(f32 (uint_to_fp Int16Regs:$a)),
          (CVT_f32_u16 Int16Regs:$a, CvtRN)>;
def : Pat<(f32 (uint_to_fp Int32Regs:$a)),
          (CVT_f32_u32 Int32Regs:$a, CvtRN)>;
def : Pat<(f32 (uint_to_fp Int64Regs:$a)),
          (CVT_f32_u64 Int64Regs:$a, CvtRN)>;

// sint -> f64
def : Pat<(f64 (sint_to_fp Int1Regs:$a)),
          (CVT_f64_s32 (SELP_u32ii 1, 0, Int1Regs:$a), CvtRN)>;
def : Pat<(f64 (sint_to_fp Int16Regs:$a)),
          (CVT_f64_s16 Int16Regs:$a, CvtRN)>;
def : Pat<(f64 (sint_to_fp Int32Regs:$a)),
          (CVT_f64_s32 Int32Regs:$a, CvtRN)>;
def : Pat<(f64 (sint_to_fp Int64Regs:$a)),
          (CVT_f64_s64 Int64Regs:$a, CvtRN)>;

// uint -> f64
def : Pat<(f64 (uint_to_fp Int1Regs:$a)),
          (CVT_f64_u32 (SELP_u32ii 1, 0, Int1Regs:$a), CvtRN)>;
def : Pat<(f64 (uint_to_fp Int16Regs:$a)),
          (CVT_f64_u16 Int16Regs:$a, CvtRN)>;
def : Pat<(f64 (uint_to_fp Int32Regs:$a)),
          (CVT_f64_u32 Int32Regs:$a, CvtRN)>;
def : Pat<(f64 (uint_to_fp Int64Regs:$a)),
          (CVT_f64_u64 Int64Regs:$a, CvtRN)>;


// f16 -> sint
def : Pat<(i1 (fp_to_sint Float16Regs:$a)),
          (SETP_b16ri (BITCONVERT_16_F2I Float16Regs:$a), 0, CmpEQ)>;
def : Pat<(i16 (fp_to_sint Float16Regs:$a)),
          (CVT_s16_f16 Float16Regs:$a, CvtRZI)>;
def : Pat<(i32 (fp_to_sint Float16Regs:$a)),
          (CVT_s32_f16 Float16Regs:$a, CvtRZI)>;
def : Pat<(i64 (fp_to_sint Float16Regs:$a)),
          (CVT_s64_f16 Float16Regs:$a, CvtRZI)>;

// f16 -> uint
def : Pat<(i1 (fp_to_uint Float16Regs:$a)),
          (SETP_b16ri (BITCONVERT_16_F2I Float16Regs:$a), 0, CmpEQ)>;
def : Pat<(i16 (fp_to_uint Float16Regs:$a)),
          (CVT_u16_f16 Float16Regs:$a, CvtRZI)>;
def : Pat<(i32 (fp_to_uint Float16Regs:$a)),
          (CVT_u32_f16 Float16Regs:$a, CvtRZI)>;
def : Pat<(i64 (fp_to_uint Float16Regs:$a)),
          (CVT_u64_f16 Float16Regs:$a, CvtRZI)>;

// f32 -> sint
def : Pat<(i1 (fp_to_sint Float32Regs:$a)),
          (SETP_b32ri (BITCONVERT_32_F2I Float32Regs:$a), 0, CmpEQ)>;
def : Pat<(i16 (fp_to_sint Float32Regs:$a)),
          (CVT_s16_f32 Float32Regs:$a, CvtRZI_FTZ)>, Requires<[doF32FTZ]>;
def : Pat<(i16 (fp_to_sint Float32Regs:$a)),
          (CVT_s16_f32 Float32Regs:$a, CvtRZI)>;
def : Pat<(i32 (fp_to_sint Float32Regs:$a)),
          (CVT_s32_f32 Float32Regs:$a, CvtRZI_FTZ)>, Requires<[doF32FTZ]>;
def : Pat<(i32 (fp_to_sint Float32Regs:$a)),
          (CVT_s32_f32 Float32Regs:$a, CvtRZI)>;
def : Pat<(i64 (fp_to_sint Float32Regs:$a)),
          (CVT_s64_f32 Float32Regs:$a, CvtRZI_FTZ)>, Requires<[doF32FTZ]>;
def : Pat<(i64 (fp_to_sint Float32Regs:$a)),
          (CVT_s64_f32 Float32Regs:$a, CvtRZI)>;

// f32 -> uint
def : Pat<(i1 (fp_to_uint Float32Regs:$a)),
          (SETP_b32ri (BITCONVERT_32_F2I Float32Regs:$a), 0, CmpEQ)>;
def : Pat<(i16 (fp_to_uint Float32Regs:$a)),
          (CVT_u16_f32 Float32Regs:$a, CvtRZI_FTZ)>, Requires<[doF32FTZ]>;
def : Pat<(i16 (fp_to_uint Float32Regs:$a)),
          (CVT_u16_f32 Float32Regs:$a, CvtRZI)>;
def : Pat<(i32 (fp_to_uint Float32Regs:$a)),
          (CVT_u32_f32 Float32Regs:$a, CvtRZI_FTZ)>, Requires<[doF32FTZ]>;
def : Pat<(i32 (fp_to_uint Float32Regs:$a)),
          (CVT_u32_f32 Float32Regs:$a, CvtRZI)>;
def : Pat<(i64 (fp_to_uint Float32Regs:$a)),
          (CVT_u64_f32 Float32Regs:$a, CvtRZI_FTZ)>, Requires<[doF32FTZ]>;
def : Pat<(i64 (fp_to_uint Float32Regs:$a)),
          (CVT_u64_f32 Float32Regs:$a, CvtRZI)>;

// f64 -> sint
def : Pat<(i1 (fp_to_sint Float64Regs:$a)),
          (SETP_b64ri (BITCONVERT_64_F2I Float64Regs:$a), 0, CmpEQ)>;
def : Pat<(i16 (fp_to_sint Float64Regs:$a)),
          (CVT_s16_f64 Float64Regs:$a, CvtRZI)>;
def : Pat<(i32 (fp_to_sint Float64Regs:$a)),
          (CVT_s32_f64 Float64Regs:$a, CvtRZI)>;
def : Pat<(i64 (fp_to_sint Float64Regs:$a)),
          (CVT_s64_f64 Float64Regs:$a, CvtRZI)>;

// f64 -> uint
def : Pat<(i1 (fp_to_uint Float64Regs:$a)),
          (SETP_b64ri (BITCONVERT_64_F2I Float64Regs:$a), 0, CmpEQ)>;
def : Pat<(i16 (fp_to_uint Float64Regs:$a)),
          (CVT_u16_f64 Float64Regs:$a, CvtRZI)>;
def : Pat<(i32 (fp_to_uint Float64Regs:$a)),
          (CVT_u32_f64 Float64Regs:$a, CvtRZI)>;
def : Pat<(i64 (fp_to_uint Float64Regs:$a)),
          (CVT_u64_f64 Float64Regs:$a, CvtRZI)>;

// sext i1
def : Pat<(i16 (sext Int1Regs:$a)),
          (SELP_s16ii -1, 0, Int1Regs:$a)>;
def : Pat<(i32 (sext Int1Regs:$a)),
          (SELP_s32ii -1, 0, Int1Regs:$a)>;
def : Pat<(i64 (sext Int1Regs:$a)),
          (SELP_s64ii -1, 0, Int1Regs:$a)>;

// zext i1
def : Pat<(i16 (zext Int1Regs:$a)),
          (SELP_u16ii 1, 0, Int1Regs:$a)>;
def : Pat<(i32 (zext Int1Regs:$a)),
          (SELP_u32ii 1, 0, Int1Regs:$a)>;
def : Pat<(i64 (zext Int1Regs:$a)),
          (SELP_u64ii 1, 0, Int1Regs:$a)>;

// anyext i1
def : Pat<(i16 (anyext Int1Regs:$a)),
          (SELP_u16ii -1, 0, Int1Regs:$a)>;
def : Pat<(i32 (anyext Int1Regs:$a)),
          (SELP_u32ii -1, 0, Int1Regs:$a)>;
def : Pat<(i64 (anyext Int1Regs:$a)),
          (SELP_u64ii -1, 0, Int1Regs:$a)>;

// sext i16
def : Pat<(i32 (sext Int16Regs:$a)),
          (CVT_s32_s16 Int16Regs:$a, CvtNONE)>;
def : Pat<(i64 (sext Int16Regs:$a)),
          (CVT_s64_s16 Int16Regs:$a, CvtNONE)>;

// zext i16
def : Pat<(i32 (zext Int16Regs:$a)),
          (CVT_u32_u16 Int16Regs:$a, CvtNONE)>;
def : Pat<(i64 (zext Int16Regs:$a)),
          (CVT_u64_u16 Int16Regs:$a, CvtNONE)>;

// anyext i16
def : Pat<(i32 (anyext Int16Regs:$a)),
          (CVT_u32_u16 Int16Regs:$a, CvtNONE)>;
def : Pat<(i64 (anyext Int16Regs:$a)),
          (CVT_u64_u16 Int16Regs:$a, CvtNONE)>;

// sext i32
def : Pat<(i64 (sext Int32Regs:$a)),
          (CVT_s64_s32 Int32Regs:$a, CvtNONE)>;

// zext i32
def : Pat<(i64 (zext Int32Regs:$a)),
          (CVT_u64_u32 Int32Regs:$a, CvtNONE)>;

// anyext i32
def : Pat<(i64 (anyext Int32Regs:$a)),
          (CVT_u64_u32 Int32Regs:$a, CvtNONE)>;


// truncate i64
def : Pat<(i32 (trunc Int64Regs:$a)),
          (CVT_u32_u64 Int64Regs:$a, CvtNONE)>;
def : Pat<(i16 (trunc Int64Regs:$a)),
          (CVT_u16_u64 Int64Regs:$a, CvtNONE)>;
def : Pat<(i1 (trunc Int64Regs:$a)),
          (SETP_b64ri (ANDb64ri Int64Regs:$a, 1), 1, CmpEQ)>;

// truncate i32
def : Pat<(i16 (trunc Int32Regs:$a)),
          (CVT_u16_u32 Int32Regs:$a, CvtNONE)>;
def : Pat<(i1 (trunc Int32Regs:$a)),
          (SETP_b32ri (ANDb32ri Int32Regs:$a, 1), 1, CmpEQ)>;

// truncate i16
def : Pat<(i1 (trunc Int16Regs:$a)),
          (SETP_b16ri (ANDb16ri Int16Regs:$a, 1), 1, CmpEQ)>;

// sext_inreg
def : Pat<(sext_inreg Int16Regs:$a, i8), (CVT_INREG_s16_s8 Int16Regs:$a)>;
def : Pat<(sext_inreg Int32Regs:$a, i8), (CVT_INREG_s32_s8 Int32Regs:$a)>;
def : Pat<(sext_inreg Int32Regs:$a, i16), (CVT_INREG_s32_s16 Int32Regs:$a)>;
def : Pat<(sext_inreg Int64Regs:$a, i8), (CVT_INREG_s64_s8 Int64Regs:$a)>;
def : Pat<(sext_inreg Int64Regs:$a, i16), (CVT_INREG_s64_s16 Int64Regs:$a)>;
def : Pat<(sext_inreg Int64Regs:$a, i32), (CVT_INREG_s64_s32 Int64Regs:$a)>;


// Select instructions with 32-bit predicates
def : Pat<(select Int32Regs:$pred, Int16Regs:$a, Int16Regs:$b),
          (SELP_b16rr Int16Regs:$a, Int16Regs:$b,
          (SETP_b32ri (ANDb32ri Int32Regs:$pred, 1), 1, CmpEQ))>;
def : Pat<(select Int32Regs:$pred, Int32Regs:$a, Int32Regs:$b),
          (SELP_b32rr Int32Regs:$a, Int32Regs:$b,
          (SETP_b32ri (ANDb32ri Int32Regs:$pred, 1), 1, CmpEQ))>;
def : Pat<(select Int32Regs:$pred, Int64Regs:$a, Int64Regs:$b),
          (SELP_b64rr Int64Regs:$a, Int64Regs:$b,
          (SETP_b32ri (ANDb32ri Int32Regs:$pred, 1), 1, CmpEQ))>;
def : Pat<(select Int32Regs:$pred, Float16Regs:$a, Float16Regs:$b),
          (SELP_f16rr Float16Regs:$a, Float16Regs:$b,
          (SETP_b32ri (ANDb32ri Int32Regs:$pred, 1), 1, CmpEQ))>;
def : Pat<(select Int32Regs:$pred, Float32Regs:$a, Float32Regs:$b),
          (SELP_f32rr Float32Regs:$a, Float32Regs:$b,
          (SETP_b32ri (ANDb32ri Int32Regs:$pred, 1), 1, CmpEQ))>;
def : Pat<(select Int32Regs:$pred, Float64Regs:$a, Float64Regs:$b),
          (SELP_f64rr Float64Regs:$a, Float64Regs:$b,
          (SETP_b32ri (ANDb32ri Int32Regs:$pred, 1), 1, CmpEQ))>;


let hasSideEffects = 0 in {
  // pack a set of smaller int registers to a larger int register
  def V4I16toI64 : NVPTXInst<(outs Int64Regs:$d),
                             (ins Int16Regs:$s1, Int16Regs:$s2,
                                  Int16Regs:$s3, Int16Regs:$s4),
                             "mov.b64 \t$d, {{$s1, $s2, $s3, $s4}};", []>;
  def V2I16toI32 : NVPTXInst<(outs Int32Regs:$d),
                             (ins Int16Regs:$s1, Int16Regs:$s2),
                             "mov.b32 \t$d, {{$s1, $s2}};", []>;
  def V2I32toI64 : NVPTXInst<(outs Int64Regs:$d),
                             (ins Int32Regs:$s1, Int32Regs:$s2),
                             "mov.b64 \t$d, {{$s1, $s2}};", []>;
  def V2F32toF64 : NVPTXInst<(outs Float64Regs:$d),
                             (ins Float32Regs:$s1, Float32Regs:$s2),
                             "mov.b64 \t$d, {{$s1, $s2}};", []>;

  // unpack a larger int register to a set of smaller int registers
  def I64toV4I16 : NVPTXInst<(outs Int16Regs:$d1, Int16Regs:$d2,
                                   Int16Regs:$d3, Int16Regs:$d4),
                             (ins Int64Regs:$s),
                             "mov.b64 \t{{$d1, $d2, $d3, $d4}}, $s;", []>;
  def I32toV2I16 : NVPTXInst<(outs Int16Regs:$d1, Int16Regs:$d2),
                             (ins Int32Regs:$s),
                             "mov.b32 \t{{$d1, $d2}}, $s;", []>;
  def I64toV2I32 : NVPTXInst<(outs Int32Regs:$d1, Int32Regs:$d2),
                             (ins Int64Regs:$s),
                             "mov.b64 \t{{$d1, $d2}}, $s;", []>;
  def F64toV2F32 : NVPTXInst<(outs Float32Regs:$d1, Float32Regs:$d2),
                             (ins Float64Regs:$s),
                             "mov.b64 \t{{$d1, $d2}}, $s;", []>;

}

let hasSideEffects = 0 in {
  // Extract element of f16x2 register. PTX does not provide any way
  // to access elements of f16x2 vector directly, so we need to
  // extract it using a temporary register.
  def F16x2toF16_0 : NVPTXInst<(outs Float16Regs:$dst),
                               (ins Float16x2Regs:$src),
                               "{{ .reg .b16 \t%tmp_hi;\n\t"
                               "  mov.b32 \t{$dst, %tmp_hi}, $src; }}",
                               [(set Float16Regs:$dst,
                                 (extractelt (v2f16 Float16x2Regs:$src), 0))]>;
  def F16x2toF16_1 : NVPTXInst<(outs Float16Regs:$dst),
                               (ins Float16x2Regs:$src),
                               "{{ .reg .b16 \t%tmp_lo;\n\t"
                               "  mov.b32 \t{%tmp_lo, $dst}, $src; }}",
                               [(set Float16Regs:$dst,
                                 (extractelt (v2f16 Float16x2Regs:$src), 1))]>;

  // Coalesce two f16 registers into f16x2
  def BuildF16x2 : NVPTXInst<(outs Float16x2Regs:$dst),
                             (ins Float16Regs:$a, Float16Regs:$b),
                             "mov.b32 \t$dst, {{$a, $b}};",
                             [(set Float16x2Regs:$dst,
                               (build_vector (f16 Float16Regs:$a), (f16 Float16Regs:$b)))]>;

  // Directly initializing underlying the b32 register is one less SASS
  // instruction than than vector-packing move.
  def BuildF16x2i : NVPTXInst<(outs Float16x2Regs:$dst), (ins i32imm:$src),
                              "mov.b32 \t$dst, $src;",
                              []>;

  // Split f16x2 into two f16 registers.
  def SplitF16x2  : NVPTXInst<(outs Float16Regs:$lo, Float16Regs:$hi),
                              (ins Float16x2Regs:$src),
                              "mov.b32 \t{{$lo, $hi}}, $src;",
                              []>;
  // Split an i32 into two f16
  def SplitI32toF16x2  : NVPTXInst<(outs Float16Regs:$lo, Float16Regs:$hi),
                                   (ins Int32Regs:$src),
                                   "mov.b32 \t{{$lo, $hi}}, $src;",
                                   []>;
}

// Count leading zeros
let hasSideEffects = 0 in {
  def CLZr32 : NVPTXInst<(outs Int32Regs:$d), (ins Int32Regs:$a),
                         "clz.b32 \t$d, $a;", []>;
  def CLZr64 : NVPTXInst<(outs Int32Regs:$d), (ins Int64Regs:$a),
                         "clz.b64 \t$d, $a;", []>;
}

// 32-bit has a direct PTX instruction
def : Pat<(ctlz Int32Regs:$a), (CLZr32 Int32Regs:$a)>;

// The return type of the ctlz ISD node is the same as its input, but the PTX
// ctz instruction always returns a 32-bit value.  For ctlz.i64, convert the
// ptx value to 64 bits to match the ISD node's semantics, unless we know we're
// truncating back down to 32 bits.
def : Pat<(ctlz Int64Regs:$a), (CVT_u64_u32 (CLZr64 Int64Regs:$a), CvtNONE)>;
def : Pat<(i32 (trunc (ctlz Int64Regs:$a))), (CLZr64 Int64Regs:$a)>;

// For 16-bit ctlz, we zero-extend to 32-bit, perform the count, then trunc the
// result back to 16-bits if necessary.  We also need to subtract 16 because
// the high-order 16 zeros were counted.
//
// TODO: NVPTX has a mov.b32 b32reg, {imm, b16reg} instruction, which we could
// use to save one SASS instruction (on sm_35 anyway):
//
//   mov.b32 $tmp, {0xffff, $a}
//   ctlz.b32 $result, $tmp
//
// That is, instead of zero-extending the input to 32 bits, we'd "one-extend"
// and then ctlz that value.  This way we don't have to subtract 16 from the
// result.  Unfortunately today we don't have a way to generate
// "mov b32reg, {b16imm, b16reg}", so we don't do this optimization.
def : Pat<(ctlz Int16Regs:$a),
          (SUBi16ri (CVT_u16_u32
           (CLZr32 (CVT_u32_u16 Int16Regs:$a, CvtNONE)), CvtNONE), 16)>;
def : Pat<(i32 (zext (ctlz Int16Regs:$a))),
          (SUBi32ri (CLZr32 (CVT_u32_u16 Int16Regs:$a, CvtNONE)), 16)>;

// Population count
let hasSideEffects = 0 in {
  def POPCr32 : NVPTXInst<(outs Int32Regs:$d), (ins Int32Regs:$a),
                          "popc.b32 \t$d, $a;", []>;
  def POPCr64 : NVPTXInst<(outs Int32Regs:$d), (ins Int64Regs:$a),
                          "popc.b64 \t$d, $a;", []>;
}

// 32-bit has a direct PTX instruction
def : Pat<(ctpop Int32Regs:$a), (POPCr32 Int32Regs:$a)>;

// For 64-bit, the result in PTX is actually 32-bit so we zero-extend to 64-bit
// to match the LLVM semantics.  Just as with ctlz.i64, we provide a second
// pattern that avoids the type conversion if we're truncating the result to
// i32 anyway.
def : Pat<(ctpop Int64Regs:$a), (CVT_u64_u32 (POPCr64 Int64Regs:$a), CvtNONE)>;
def : Pat<(i32 (trunc (ctpop Int64Regs:$a))), (POPCr64 Int64Regs:$a)>;

// For 16-bit, we zero-extend to 32-bit, then trunc the result back to 16-bits.
// If we know that we're storing into an i32, we can avoid the final trunc.
def : Pat<(ctpop Int16Regs:$a),
          (CVT_u16_u32 (POPCr32 (CVT_u32_u16 Int16Regs:$a, CvtNONE)), CvtNONE)>;
def : Pat<(i32 (zext (ctpop Int16Regs:$a))),
          (POPCr32 (CVT_u32_u16 Int16Regs:$a, CvtNONE))>;

// fpround f32 -> f16
def : Pat<(f16 (fpround Float32Regs:$a)),
          (CVT_f16_f32 Float32Regs:$a, CvtRN)>;

// fpround f64 -> f16
def : Pat<(f16 (fpround Float64Regs:$a)),
          (CVT_f16_f64 Float64Regs:$a, CvtRN)>;

// fpround f64 -> f32
def : Pat<(f32 (fpround Float64Regs:$a)),
          (CVT_f32_f64 Float64Regs:$a, CvtRN_FTZ)>, Requires<[doF32FTZ]>;
def : Pat<(f32 (fpround Float64Regs:$a)),
          (CVT_f32_f64 Float64Regs:$a, CvtRN)>;

// fpextend f16 -> f32
def : Pat<(f32 (fpextend Float16Regs:$a)),
          (CVT_f32_f16 Float16Regs:$a, CvtNONE_FTZ)>, Requires<[doF32FTZ]>;
def : Pat<(f32 (fpextend Float16Regs:$a)),
          (CVT_f32_f16 Float16Regs:$a, CvtNONE)>;

// fpextend f16 -> f64
def : Pat<(f64 (fpextend Float16Regs:$a)),
          (CVT_f64_f16 Float16Regs:$a, CvtNONE)>;

// fpextend f32 -> f64
def : Pat<(f64 (fpextend Float32Regs:$a)),
          (CVT_f64_f32 Float32Regs:$a, CvtNONE_FTZ)>, Requires<[doF32FTZ]>;
def : Pat<(f64 (fpextend Float32Regs:$a)),
          (CVT_f64_f32 Float32Regs:$a, CvtNONE)>;

def retflag : SDNode<"NVPTXISD::RET_FLAG", SDTNone,
                     [SDNPHasChain, SDNPOptInGlue]>;

// fceil, ffloor, fround, ftrunc.

def : Pat<(fceil Float16Regs:$a),
          (CVT_f16_f16 Float16Regs:$a, CvtRPI)>;
def : Pat<(fceil Float32Regs:$a),
          (CVT_f32_f32 Float32Regs:$a, CvtRPI_FTZ)>, Requires<[doF32FTZ]>;
def : Pat<(fceil Float32Regs:$a),
          (CVT_f32_f32 Float32Regs:$a, CvtRPI)>, Requires<[doNoF32FTZ]>;
def : Pat<(fceil Float64Regs:$a),
          (CVT_f64_f64 Float64Regs:$a, CvtRPI)>;

def : Pat<(ffloor Float16Regs:$a),
          (CVT_f16_f16 Float16Regs:$a, CvtRMI)>;
def : Pat<(ffloor Float32Regs:$a),
          (CVT_f32_f32 Float32Regs:$a, CvtRMI_FTZ)>, Requires<[doF32FTZ]>;
def : Pat<(ffloor Float32Regs:$a),
          (CVT_f32_f32 Float32Regs:$a, CvtRMI)>, Requires<[doNoF32FTZ]>;
def : Pat<(ffloor Float64Regs:$a),
          (CVT_f64_f64 Float64Regs:$a, CvtRMI)>;

def : Pat<(f16 (fround Float16Regs:$a)),
          (CVT_f16_f16 Float16Regs:$a, CvtRNI)>;
def : Pat<(fround Float32Regs:$a),
          (CVT_f32_f32 Float32Regs:$a, CvtRNI_FTZ)>, Requires<[doF32FTZ]>;
def : Pat<(f32 (fround Float32Regs:$a)),
          (CVT_f32_f32 Float32Regs:$a, CvtRNI)>, Requires<[doNoF32FTZ]>;
def : Pat<(f64 (fround Float64Regs:$a)),
          (CVT_f64_f64 Float64Regs:$a, CvtRNI)>;

def : Pat<(ftrunc Float16Regs:$a),
          (CVT_f16_f16 Float16Regs:$a, CvtRZI)>;
def : Pat<(ftrunc Float32Regs:$a),
          (CVT_f32_f32 Float32Regs:$a, CvtRZI_FTZ)>, Requires<[doF32FTZ]>;
def : Pat<(ftrunc Float32Regs:$a),
          (CVT_f32_f32 Float32Regs:$a, CvtRZI)>, Requires<[doNoF32FTZ]>;
def : Pat<(ftrunc Float64Regs:$a),
          (CVT_f64_f64 Float64Regs:$a, CvtRZI)>;

// nearbyint and rint are implemented as rounding to nearest even.  This isn't
// strictly correct, because it causes us to ignore the rounding mode.  But it
// matches what CUDA's "libm" does.

def : Pat<(fnearbyint Float16Regs:$a),
          (CVT_f16_f16 Float16Regs:$a, CvtRNI)>;
def : Pat<(fnearbyint Float32Regs:$a),
          (CVT_f32_f32 Float32Regs:$a, CvtRNI_FTZ)>, Requires<[doF32FTZ]>;
def : Pat<(fnearbyint Float32Regs:$a),
          (CVT_f32_f32 Float32Regs:$a, CvtRNI)>, Requires<[doNoF32FTZ]>;
def : Pat<(fnearbyint Float64Regs:$a),
          (CVT_f64_f64 Float64Regs:$a, CvtRNI)>;

def : Pat<(frint Float16Regs:$a),
          (CVT_f16_f16 Float16Regs:$a, CvtRNI)>;
def : Pat<(frint Float32Regs:$a),
          (CVT_f32_f32 Float32Regs:$a, CvtRNI_FTZ)>, Requires<[doF32FTZ]>;
def : Pat<(frint Float32Regs:$a),
          (CVT_f32_f32 Float32Regs:$a, CvtRNI)>, Requires<[doNoF32FTZ]>;
def : Pat<(frint Float64Regs:$a),
          (CVT_f64_f64 Float64Regs:$a, CvtRNI)>;


//-----------------------------------
// Control-flow
//-----------------------------------

let isTerminator=1 in {
   let isReturn=1, isBarrier=1 in
      def Return : NVPTXInst<(outs), (ins), "ret;", [(retflag)]>;

   let isBranch=1 in
      def CBranch : NVPTXInst<(outs), (ins Int1Regs:$a, brtarget:$target),
                              "@$a bra \t$target;",
                              [(brcond Int1Regs:$a, bb:$target)]>;
   let isBranch=1 in
      def CBranchOther : NVPTXInst<(outs), (ins Int1Regs:$a, brtarget:$target),
                                   "@!$a bra \t$target;", []>;

   let isBranch=1, isBarrier=1 in
      def GOTO : NVPTXInst<(outs), (ins brtarget:$target),
                           "bra.uni \t$target;", [(br bb:$target)]>;
}

def : Pat<(brcond Int32Regs:$a, bb:$target),
          (CBranch (SETP_u32ri Int32Regs:$a, 0, CmpNE), bb:$target)>;

// SelectionDAGBuilder::visitSWitchCase() will invert the condition of a
// conditional branch if the target block is the next block so that the code
// can fall through to the target block.  The invertion is done by 'xor
// condition, 1', which will be translated to (setne condition, -1).  Since ptx
// supports '@!pred bra target', we should use it.
def : Pat<(brcond (i1 (setne Int1Regs:$a, -1)), bb:$target),
          (CBranchOther Int1Regs:$a, bb:$target)>;

// Call
def SDT_NVPTXCallSeqStart : SDCallSeqStart<[SDTCisVT<0, i32>,
                                            SDTCisVT<1, i32>]>;
def SDT_NVPTXCallSeqEnd   : SDCallSeqEnd<[SDTCisVT<0, i32>, SDTCisVT<1, i32>]>;

def callseq_start : SDNode<"ISD::CALLSEQ_START", SDT_NVPTXCallSeqStart,
                           [SDNPHasChain, SDNPOutGlue, SDNPSideEffect]>;
def callseq_end   : SDNode<"ISD::CALLSEQ_END", SDT_NVPTXCallSeqEnd,
                           [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue,
                            SDNPSideEffect]>;

def SDT_NVPTXCall : SDTypeProfile<0, 1, [SDTCisVT<0, i32>]>;
def call          : SDNode<"NVPTXISD::CALL", SDT_NVPTXCall,
                           [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue]>;
def calltarget : Operand<i32>;
let isCall=1 in {
   def CALL : NVPTXInst<(outs), (ins calltarget:$dst), "call \t$dst, (1);", []>;
}

def : Pat<(call tglobaladdr:$dst), (CALL tglobaladdr:$dst)>;
def : Pat<(call texternalsym:$dst), (CALL texternalsym:$dst)>;

// Pseudo instructions.
class Pseudo<dag outs, dag ins, string asmstr, list<dag> pattern>
   : NVPTXInst<outs, ins, asmstr, pattern>;

def Callseq_Start :
  NVPTXInst<(outs), (ins i32imm:$amt1, i32imm:$amt2),
            "\\{ // callseq $amt1, $amt2\n"
            "\t.reg .b32 temp_param_reg;",
            [(callseq_start timm:$amt1, timm:$amt2)]>;
def Callseq_End :
  NVPTXInst<(outs), (ins i32imm:$amt1, i32imm:$amt2),
            "\\} // callseq $amt1",
            [(callseq_end timm:$amt1, timm:$amt2)]>;

// trap instruction
def trapinst : NVPTXInst<(outs), (ins), "trap;", [(trap)]>;

// Call prototype wrapper
def SDTCallPrototype : SDTypeProfile<0, 1, [SDTCisInt<0>]>;
def CallPrototype :
  SDNode<"NVPTXISD::CallPrototype", SDTCallPrototype,
         [SDNPHasChain, SDNPOutGlue, SDNPInGlue, SDNPSideEffect]>;
def ProtoIdent : Operand<i32> {
  let PrintMethod = "printProtoIdent";
}
def CALL_PROTOTYPE :
  NVPTXInst<(outs), (ins ProtoIdent:$ident),
            "$ident", [(CallPrototype (i32 texternalsym:$ident))]>;


include "NVPTXIntrinsics.td"


//-----------------------------------
// Notes
//-----------------------------------
// BSWAP is currently expanded. The following is a more efficient
// - for < sm_20, use vector scalar mov, as tesla support native 16-bit register
// - for sm_20, use pmpt (use vector scalar mov to get the pack and
//   unpack). sm_20 supports native 32-bit register, but not native 16-bit
// register.
