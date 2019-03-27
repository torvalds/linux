//===- X86InstrFPStack.td - FPU Instruction Set ------------*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file describes the X86 x87 FPU instruction set, defining the
// instructions, and properties of the instructions which are needed for code
// generation, machine code emission, and analysis.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// FPStack specific DAG Nodes.
//===----------------------------------------------------------------------===//

def SDTX86FpGet2    : SDTypeProfile<2, 0, [SDTCisVT<0, f80>,
                                           SDTCisVT<1, f80>]>;
def SDTX86Fld       : SDTypeProfile<1, 2, [SDTCisFP<0>,
                                           SDTCisPtrTy<1>,
                                           SDTCisVT<2, OtherVT>]>;
def SDTX86Fst       : SDTypeProfile<0, 3, [SDTCisFP<0>,
                                           SDTCisPtrTy<1>,
                                           SDTCisVT<2, OtherVT>]>;
def SDTX86Fild      : SDTypeProfile<1, 2, [SDTCisFP<0>, SDTCisPtrTy<1>,
                                           SDTCisVT<2, OtherVT>]>;
def SDTX86Fnstsw    : SDTypeProfile<1, 1, [SDTCisVT<0, i16>, SDTCisVT<1, i16>]>;
def SDTX86FpToIMem  : SDTypeProfile<0, 2, [SDTCisFP<0>, SDTCisPtrTy<1>]>;

def SDTX86CwdStore  : SDTypeProfile<0, 1, [SDTCisPtrTy<0>]>;

def X86fld          : SDNode<"X86ISD::FLD", SDTX86Fld,
                             [SDNPHasChain, SDNPMayLoad, SDNPMemOperand]>;
def X86fst          : SDNode<"X86ISD::FST", SDTX86Fst,
                             [SDNPHasChain, SDNPInGlue, SDNPMayStore,
                              SDNPMemOperand]>;
def X86fild         : SDNode<"X86ISD::FILD", SDTX86Fild,
                             [SDNPHasChain, SDNPMayLoad, SDNPMemOperand]>;
def X86fildflag     : SDNode<"X86ISD::FILD_FLAG", SDTX86Fild,
                             [SDNPHasChain, SDNPOutGlue, SDNPMayLoad,
                              SDNPMemOperand]>;
def X86fp_stsw      : SDNode<"X86ISD::FNSTSW16r", SDTX86Fnstsw>;
def X86fp_to_i16mem : SDNode<"X86ISD::FP_TO_INT16_IN_MEM", SDTX86FpToIMem,
                             [SDNPHasChain, SDNPMayStore, SDNPMemOperand]>;
def X86fp_to_i32mem : SDNode<"X86ISD::FP_TO_INT32_IN_MEM", SDTX86FpToIMem,
                             [SDNPHasChain, SDNPMayStore, SDNPMemOperand]>;
def X86fp_to_i64mem : SDNode<"X86ISD::FP_TO_INT64_IN_MEM", SDTX86FpToIMem,
                             [SDNPHasChain, SDNPMayStore, SDNPMemOperand]>;
def X86fp_cwd_get16 : SDNode<"X86ISD::FNSTCW16m",          SDTX86CwdStore,
                             [SDNPHasChain, SDNPMayStore, SDNPSideEffect,
                              SDNPMemOperand]>;

//===----------------------------------------------------------------------===//
// FPStack pattern fragments
//===----------------------------------------------------------------------===//

def fpimm0 : FPImmLeaf<fAny, [{
  return Imm.isExactlyValue(+0.0);
}]>;

def fpimmneg0 : FPImmLeaf<fAny, [{
  return Imm.isExactlyValue(-0.0);
}]>;

def fpimm1 : FPImmLeaf<fAny, [{
  return Imm.isExactlyValue(+1.0);
}]>;

def fpimmneg1 : FPImmLeaf<fAny, [{
  return Imm.isExactlyValue(-1.0);
}]>;

// Some 'special' instructions - expanded after instruction selection.
let usesCustomInserter = 1, hasNoSchedulingInfo = 1 in {
  def FP32_TO_INT16_IN_MEM : PseudoI<(outs), (ins i16mem:$dst, RFP32:$src),
                              [(X86fp_to_i16mem RFP32:$src, addr:$dst)]>;
  def FP32_TO_INT32_IN_MEM : PseudoI<(outs), (ins i32mem:$dst, RFP32:$src),
                              [(X86fp_to_i32mem RFP32:$src, addr:$dst)]>;
  def FP32_TO_INT64_IN_MEM : PseudoI<(outs), (ins i64mem:$dst, RFP32:$src),
                              [(X86fp_to_i64mem RFP32:$src, addr:$dst)]>;
  def FP64_TO_INT16_IN_MEM : PseudoI<(outs), (ins i16mem:$dst, RFP64:$src),
                              [(X86fp_to_i16mem RFP64:$src, addr:$dst)]>;
  def FP64_TO_INT32_IN_MEM : PseudoI<(outs), (ins i32mem:$dst, RFP64:$src),
                              [(X86fp_to_i32mem RFP64:$src, addr:$dst)]>;
  def FP64_TO_INT64_IN_MEM : PseudoI<(outs), (ins i64mem:$dst, RFP64:$src),
                              [(X86fp_to_i64mem RFP64:$src, addr:$dst)]>;
  def FP80_TO_INT16_IN_MEM : PseudoI<(outs), (ins i16mem:$dst, RFP80:$src),
                              [(X86fp_to_i16mem RFP80:$src, addr:$dst)]>;
  def FP80_TO_INT32_IN_MEM : PseudoI<(outs), (ins i32mem:$dst, RFP80:$src),
                              [(X86fp_to_i32mem RFP80:$src, addr:$dst)]>;
  def FP80_TO_INT64_IN_MEM : PseudoI<(outs), (ins i64mem:$dst, RFP80:$src),
                              [(X86fp_to_i64mem RFP80:$src, addr:$dst)]>;
}

// All FP Stack operations are represented with four instructions here.  The
// first three instructions, generated by the instruction selector, use "RFP32"
// "RFP64" or "RFP80" registers: traditional register files to reference 32-bit,
// 64-bit or 80-bit floating point values.  These sizes apply to the values,
// not the registers, which are always 80 bits; RFP32, RFP64 and RFP80 can be
// copied to each other without losing information.  These instructions are all
// pseudo instructions and use the "_Fp" suffix.
// In some cases there are additional variants with a mixture of different
// register sizes.
// The second instruction is defined with FPI, which is the actual instruction
// emitted by the assembler.  These use "RST" registers, although frequently
// the actual register(s) used are implicit.  These are always 80 bits.
// The FP stackifier pass converts one to the other after register allocation
// occurs.
//
// Note that the FpI instruction should have instruction selection info (e.g.
// a pattern) and the FPI instruction should have emission info (e.g. opcode
// encoding and asm printing info).

// FpIf32, FpIf64 - Floating Point Pseudo Instruction template.
// f32 instructions can use SSE1 and are predicated on FPStackf32 == !SSE1.
// f64 instructions can use SSE2 and are predicated on FPStackf64 == !SSE2.
// f80 instructions cannot use SSE and use neither of these.
class FpIf32<dag outs, dag ins, FPFormat fp, list<dag> pattern> :
             FpI_<outs, ins, fp, pattern>, Requires<[FPStackf32]>;
class FpIf64<dag outs, dag ins, FPFormat fp, list<dag> pattern> :
             FpI_<outs, ins, fp, pattern>, Requires<[FPStackf64]>;

// Factoring for arithmetic.
multiclass FPBinary_rr<SDNode OpNode> {
// Register op register -> register
// These are separated out because they have no reversed form.
def _Fp32 : FpIf32<(outs RFP32:$dst), (ins RFP32:$src1, RFP32:$src2), TwoArgFP,
                [(set RFP32:$dst, (OpNode RFP32:$src1, RFP32:$src2))]>;
def _Fp64 : FpIf64<(outs RFP64:$dst), (ins RFP64:$src1, RFP64:$src2), TwoArgFP,
                [(set RFP64:$dst, (OpNode RFP64:$src1, RFP64:$src2))]>;
def _Fp80 : FpI_<(outs RFP80:$dst), (ins RFP80:$src1, RFP80:$src2), TwoArgFP,
                [(set RFP80:$dst, (OpNode RFP80:$src1, RFP80:$src2))]>;
}
// The FopST0 series are not included here because of the irregularities
// in where the 'r' goes in assembly output.
// These instructions cannot address 80-bit memory.
multiclass FPBinary<SDNode OpNode, Format fp, string asmstring,
                    bit Forward = 1> {
let mayLoad = 1, hasSideEffects = 1 in {
// ST(0) = ST(0) + [mem]
def _Fp32m  : FpIf32<(outs RFP32:$dst),
                     (ins RFP32:$src1, f32mem:$src2), OneArgFPRW,
                  [!if(Forward,
                       (set RFP32:$dst,
                        (OpNode RFP32:$src1, (loadf32 addr:$src2))),
                       (set RFP32:$dst,
                        (OpNode (loadf32 addr:$src2), RFP32:$src1)))]>;
def _Fp64m  : FpIf64<(outs RFP64:$dst),
                     (ins RFP64:$src1, f64mem:$src2), OneArgFPRW,
                  [!if(Forward,
                       (set RFP64:$dst,
                        (OpNode RFP64:$src1, (loadf64 addr:$src2))),
                       (set RFP64:$dst,
                        (OpNode (loadf64 addr:$src2), RFP64:$src1)))]>;
def _Fp64m32: FpIf64<(outs RFP64:$dst),
                     (ins RFP64:$src1, f32mem:$src2), OneArgFPRW,
                  [!if(Forward,
                       (set RFP64:$dst,
                        (OpNode RFP64:$src1, (f64 (extloadf32 addr:$src2)))),
                       (set RFP64:$dst,
                        (OpNode (f64 (extloadf32 addr:$src2)), RFP64:$src1)))]>;
def _Fp80m32: FpI_<(outs RFP80:$dst),
                   (ins RFP80:$src1, f32mem:$src2), OneArgFPRW,
                  [!if(Forward,
                       (set RFP80:$dst,
                        (OpNode RFP80:$src1, (f80 (extloadf32 addr:$src2)))),
                       (set RFP80:$dst,
                        (OpNode (f80 (extloadf32 addr:$src2)), RFP80:$src1)))]>;
def _Fp80m64: FpI_<(outs RFP80:$dst),
                   (ins RFP80:$src1, f64mem:$src2), OneArgFPRW,
                  [!if(Forward,
                       (set RFP80:$dst,
                        (OpNode RFP80:$src1, (f80 (extloadf64 addr:$src2)))),
                       (set RFP80:$dst,
                        (OpNode (f80 (extloadf64 addr:$src2)), RFP80:$src1)))]>;
def _F32m  : FPI<0xD8, fp, (outs), (ins f32mem:$src),
                 !strconcat("f", asmstring, "{s}\t$src")>;
def _F64m  : FPI<0xDC, fp, (outs), (ins f64mem:$src),
                 !strconcat("f", asmstring, "{l}\t$src")>;
// ST(0) = ST(0) + [memint]
def _FpI16m32 : FpIf32<(outs RFP32:$dst), (ins RFP32:$src1, i16mem:$src2),
                       OneArgFPRW,
                       [!if(Forward,
                            (set RFP32:$dst,
                             (OpNode RFP32:$src1, (X86fild addr:$src2, i16))),
                            (set RFP32:$dst,
                             (OpNode (X86fild addr:$src2, i16), RFP32:$src1)))]>;
def _FpI32m32 : FpIf32<(outs RFP32:$dst), (ins RFP32:$src1, i32mem:$src2),
                       OneArgFPRW,
                       [!if(Forward,
                            (set RFP32:$dst,
                             (OpNode RFP32:$src1, (X86fild addr:$src2, i32))),
                            (set RFP32:$dst,
                             (OpNode (X86fild addr:$src2, i32), RFP32:$src1)))]>;
def _FpI16m64 : FpIf64<(outs RFP64:$dst), (ins RFP64:$src1, i16mem:$src2),
                       OneArgFPRW,
                       [!if(Forward,
                            (set RFP64:$dst,
                             (OpNode RFP64:$src1, (X86fild addr:$src2, i16))),
                            (set RFP64:$dst,
                             (OpNode (X86fild addr:$src2, i16), RFP64:$src1)))]>;
def _FpI32m64 : FpIf64<(outs RFP64:$dst), (ins RFP64:$src1, i32mem:$src2),
                       OneArgFPRW,
                       [!if(Forward,
                            (set RFP64:$dst,
                             (OpNode RFP64:$src1, (X86fild addr:$src2, i32))),
                            (set RFP64:$dst,
                             (OpNode (X86fild addr:$src2, i32), RFP64:$src1)))]>;
def _FpI16m80 : FpI_<(outs RFP80:$dst), (ins RFP80:$src1, i16mem:$src2),
                     OneArgFPRW,
                     [!if(Forward,
                          (set RFP80:$dst,
                           (OpNode RFP80:$src1, (X86fild addr:$src2, i16))),
                          (set RFP80:$dst,
                           (OpNode (X86fild addr:$src2, i16), RFP80:$src1)))]>;
def _FpI32m80 : FpI_<(outs RFP80:$dst), (ins RFP80:$src1, i32mem:$src2),
                     OneArgFPRW,
                     [!if(Forward,
                          (set RFP80:$dst,
                           (OpNode RFP80:$src1, (X86fild addr:$src2, i32))),
                          (set RFP80:$dst,
                           (OpNode (X86fild addr:$src2, i32), RFP80:$src1)))]>;
def _FI16m  : FPI<0xDE, fp, (outs), (ins i16mem:$src),
                  !strconcat("fi", asmstring, "{s}\t$src")>;
def _FI32m  : FPI<0xDA, fp, (outs), (ins i32mem:$src),
                  !strconcat("fi", asmstring, "{l}\t$src")>;
} // mayLoad = 1, hasSideEffects = 1
}

let Defs = [FPSW], Uses = [FPCW] in {
// FPBinary_rr just defines pseudo-instructions, no need to set a scheduling
// resources.
let hasNoSchedulingInfo = 1 in {
defm ADD : FPBinary_rr<fadd>;
defm SUB : FPBinary_rr<fsub>;
defm MUL : FPBinary_rr<fmul>;
defm DIV : FPBinary_rr<fdiv>;
}

// Sets the scheduling resources for the actual NAME#_F<size>m defintions.
let SchedRW = [WriteFAddLd] in {
defm ADD : FPBinary<fadd, MRM0m, "add">;
defm SUB : FPBinary<fsub, MRM4m, "sub">;
defm SUBR: FPBinary<fsub ,MRM5m, "subr", 0>;
}

let SchedRW = [WriteFMulLd] in {
defm MUL : FPBinary<fmul, MRM1m, "mul">;
}

let SchedRW = [WriteFDivLd] in {
defm DIV : FPBinary<fdiv, MRM6m, "div">;
defm DIVR: FPBinary<fdiv, MRM7m, "divr", 0>;
}
} // Defs = [FPSW]

class FPST0rInst<Format fp, string asm>
  : FPI<0xD8, fp, (outs), (ins RSTi:$op), asm>;
class FPrST0Inst<Format fp, string asm>
  : FPI<0xDC, fp, (outs), (ins RSTi:$op), asm>;
class FPrST0PInst<Format fp, string asm>
  : FPI<0xDE, fp, (outs), (ins RSTi:$op), asm>;

// NOTE: GAS and apparently all other AT&T style assemblers have a broken notion
// of some of the 'reverse' forms of the fsub and fdiv instructions.  As such,
// we have to put some 'r's in and take them out of weird places.
let SchedRW = [WriteFAdd], Defs = [FPSW], Uses = [FPCW] in {
def ADD_FST0r   : FPST0rInst <MRM0r, "fadd\t{$op, %st|st, $op}">;
def ADD_FrST0   : FPrST0Inst <MRM0r, "fadd\t{%st, $op|$op, st}">;
def ADD_FPrST0  : FPrST0PInst<MRM0r, "faddp\t{%st, $op|$op, st}">;
def SUBR_FST0r  : FPST0rInst <MRM5r, "fsubr\t{$op, %st|st, $op}">;
def SUB_FrST0   : FPrST0Inst <MRM5r, "fsub{r}\t{%st, $op|$op, st}">;
def SUB_FPrST0  : FPrST0PInst<MRM5r, "fsub{r}p\t{%st, $op|$op, st}">;
def SUB_FST0r   : FPST0rInst <MRM4r, "fsub\t{$op, %st|st, $op}">;
def SUBR_FrST0  : FPrST0Inst <MRM4r, "fsub{|r}\t{%st, $op|$op, st}">;
def SUBR_FPrST0 : FPrST0PInst<MRM4r, "fsub{|r}p\t{%st, $op|$op, st}">;
} // SchedRW
let SchedRW = [WriteFCom], Defs = [FPSW], Uses = [FPCW] in {
def COM_FST0r   : FPST0rInst <MRM2r, "fcom\t$op">;
def COMP_FST0r  : FPST0rInst <MRM3r, "fcomp\t$op">;
} // SchedRW
let SchedRW = [WriteFMul], Defs = [FPSW], Uses = [FPCW] in {
def MUL_FST0r   : FPST0rInst <MRM1r, "fmul\t{$op, %st|st, $op}">;
def MUL_FrST0   : FPrST0Inst <MRM1r, "fmul\t{%st, $op|$op, st}">;
def MUL_FPrST0  : FPrST0PInst<MRM1r, "fmulp\t{%st, $op|$op, st}">;
} // SchedRW
let SchedRW = [WriteFDiv], Defs = [FPSW], Uses = [FPCW] in {
def DIVR_FST0r  : FPST0rInst <MRM7r, "fdivr\t{$op, %st|st, $op}">;
def DIV_FrST0   : FPrST0Inst <MRM7r, "fdiv{r}\t{%st, $op|$op, st}">;
def DIV_FPrST0  : FPrST0PInst<MRM7r, "fdiv{r}p\t{%st, $op|$op, st}">;
def DIV_FST0r   : FPST0rInst <MRM6r, "fdiv\t{$op, %st|st, $op}">;
def DIVR_FrST0  : FPrST0Inst <MRM6r, "fdiv{|r}\t{%st, $op|$op, st}">;
def DIVR_FPrST0 : FPrST0PInst<MRM6r, "fdiv{|r}p\t{%st, $op|$op, st}">;
} // SchedRW

// Unary operations.
multiclass FPUnary<SDNode OpNode, Format fp, string asmstring> {
def _Fp32  : FpIf32<(outs RFP32:$dst), (ins RFP32:$src), OneArgFPRW,
                 [(set RFP32:$dst, (OpNode RFP32:$src))]>;
def _Fp64  : FpIf64<(outs RFP64:$dst), (ins RFP64:$src), OneArgFPRW,
                 [(set RFP64:$dst, (OpNode RFP64:$src))]>;
def _Fp80  : FpI_<(outs RFP80:$dst), (ins RFP80:$src), OneArgFPRW,
                 [(set RFP80:$dst, (OpNode RFP80:$src))]>;
def _F     : FPI<0xD9, fp, (outs), (ins), asmstring>;
}

let Defs = [FPSW], Uses = [FPCW] in {

let SchedRW = [WriteFSign] in {
defm CHS : FPUnary<fneg, MRM_E0, "fchs">;
defm ABS : FPUnary<fabs, MRM_E1, "fabs">;
}

let SchedRW = [WriteFSqrt80] in
defm SQRT: FPUnary<fsqrt,MRM_FA, "fsqrt">;

let SchedRW = [WriteMicrocoded] in {
defm SIN : FPUnary<fsin, MRM_FE, "fsin">;
defm COS : FPUnary<fcos, MRM_FF, "fcos">;
}

let SchedRW = [WriteFCom] in {
let hasSideEffects = 0 in {
def TST_Fp32  : FpIf32<(outs), (ins RFP32:$src), OneArgFP, []>;
def TST_Fp64  : FpIf64<(outs), (ins RFP64:$src), OneArgFP, []>;
def TST_Fp80  : FpI_<(outs), (ins RFP80:$src), OneArgFP, []>;
} // hasSideEffects

def TST_F  : FPI<0xD9, MRM_E4, (outs), (ins), "ftst">;
} // SchedRW
} // Defs = [FPSW]

// Versions of FP instructions that take a single memory operand.  Added for the
//   disassembler; remove as they are included with patterns elsewhere.
let SchedRW = [WriteFComLd], Defs = [FPSW], Uses = [FPCW] in {
def FCOM32m  : FPI<0xD8, MRM2m, (outs), (ins f32mem:$src), "fcom{s}\t$src">;
def FCOMP32m : FPI<0xD8, MRM3m, (outs), (ins f32mem:$src), "fcomp{s}\t$src">;

def FCOM64m  : FPI<0xDC, MRM2m, (outs), (ins f64mem:$src), "fcom{l}\t$src">;
def FCOMP64m : FPI<0xDC, MRM3m, (outs), (ins f64mem:$src), "fcomp{l}\t$src">;

def FICOM16m : FPI<0xDE, MRM2m, (outs), (ins i16mem:$src), "ficom{s}\t$src">;
def FICOMP16m: FPI<0xDE, MRM3m, (outs), (ins i16mem:$src), "ficomp{s}\t$src">;

def FICOM32m : FPI<0xDA, MRM2m, (outs), (ins i32mem:$src), "ficom{l}\t$src">;
def FICOMP32m: FPI<0xDA, MRM3m, (outs), (ins i32mem:$src), "ficomp{l}\t$src">;
} // SchedRW

let SchedRW = [WriteMicrocoded] in {
def FLDENVm  : FPI<0xD9, MRM4m, (outs), (ins f32mem:$src), "fldenv\t$src">;
def FSTENVm  : FPI<0xD9, MRM6m, (outs), (ins f32mem:$dst), "fnstenv\t$dst">;

def FRSTORm  : FPI<0xDD, MRM4m, (outs), (ins f32mem:$dst), "frstor\t$dst">;
def FSAVEm   : FPI<0xDD, MRM6m, (outs), (ins f32mem:$dst), "fnsave\t$dst">;
def FNSTSWm  : FPI<0xDD, MRM7m, (outs), (ins i16mem:$dst), "fnstsw\t$dst">;

def FBLDm    : FPI<0xDF, MRM4m, (outs), (ins f80mem:$src), "fbld\t$src">;
def FBSTPm   : FPI<0xDF, MRM6m, (outs), (ins f80mem:$dst), "fbstp\t$dst">;
} // SchedRW

// Floating point cmovs.
class FpIf32CMov<dag outs, dag ins, FPFormat fp, list<dag> pattern> :
  FpI_<outs, ins, fp, pattern>, Requires<[FPStackf32, HasCMov]>;
class FpIf64CMov<dag outs, dag ins, FPFormat fp, list<dag> pattern> :
  FpI_<outs, ins, fp, pattern>, Requires<[FPStackf64, HasCMov]>;

multiclass FPCMov<PatLeaf cc> {
  def _Fp32  : FpIf32CMov<(outs RFP32:$dst), (ins RFP32:$src1, RFP32:$src2),
                       CondMovFP,
                     [(set RFP32:$dst, (X86cmov RFP32:$src1, RFP32:$src2,
                                        cc, EFLAGS))]>;
  def _Fp64  : FpIf64CMov<(outs RFP64:$dst), (ins RFP64:$src1, RFP64:$src2),
                       CondMovFP,
                     [(set RFP64:$dst, (X86cmov RFP64:$src1, RFP64:$src2,
                                        cc, EFLAGS))]>;
  def _Fp80  : FpI_<(outs RFP80:$dst), (ins RFP80:$src1, RFP80:$src2),
                     CondMovFP,
                     [(set RFP80:$dst, (X86cmov RFP80:$src1, RFP80:$src2,
                                        cc, EFLAGS))]>,
                                        Requires<[HasCMov]>;
}

let Defs = [FPSW] in {
let SchedRW = [WriteFCMOV] in {
let Uses = [EFLAGS], Constraints = "$src1 = $dst" in {
defm CMOVB  : FPCMov<X86_COND_B>;
defm CMOVBE : FPCMov<X86_COND_BE>;
defm CMOVE  : FPCMov<X86_COND_E>;
defm CMOVP  : FPCMov<X86_COND_P>;
defm CMOVNB : FPCMov<X86_COND_AE>;
defm CMOVNBE: FPCMov<X86_COND_A>;
defm CMOVNE : FPCMov<X86_COND_NE>;
defm CMOVNP : FPCMov<X86_COND_NP>;
} // Uses = [EFLAGS], Constraints = "$src1 = $dst"

let Predicates = [HasCMov] in {
// These are not factored because there's no clean way to pass DA/DB.
def CMOVB_F  : FPI<0xDA, MRM0r, (outs), (ins RSTi:$op),
                  "fcmovb\t{$op, %st|st, $op}">;
def CMOVBE_F : FPI<0xDA, MRM2r, (outs), (ins RSTi:$op),
                  "fcmovbe\t{$op, %st|st, $op}">;
def CMOVE_F  : FPI<0xDA, MRM1r, (outs), (ins RSTi:$op),
                  "fcmove\t{$op, %st|st, $op}">;
def CMOVP_F  : FPI<0xDA, MRM3r, (outs), (ins RSTi:$op),
                  "fcmovu\t{$op, %st|st, $op}">;
def CMOVNB_F : FPI<0xDB, MRM0r, (outs), (ins RSTi:$op),
                  "fcmovnb\t{$op, %st|st, $op}">;
def CMOVNBE_F: FPI<0xDB, MRM2r, (outs), (ins RSTi:$op),
                  "fcmovnbe\t{$op, %st|st, $op}">;
def CMOVNE_F : FPI<0xDB, MRM1r, (outs), (ins RSTi:$op),
                  "fcmovne\t{$op, %st|st, $op}">;
def CMOVNP_F : FPI<0xDB, MRM3r, (outs), (ins RSTi:$op),
                  "fcmovnu\t{$op, %st|st, $op}">;
} // Predicates = [HasCMov]
} // SchedRW

// Floating point loads & stores.
let SchedRW = [WriteLoad] in {
let canFoldAsLoad = 1 in {
def LD_Fp32m   : FpIf32<(outs RFP32:$dst), (ins f32mem:$src), ZeroArgFP,
                  [(set RFP32:$dst, (loadf32 addr:$src))]>;
let isReMaterializable = 1 in
  def LD_Fp64m : FpIf64<(outs RFP64:$dst), (ins f64mem:$src), ZeroArgFP,
                  [(set RFP64:$dst, (loadf64 addr:$src))]>;
def LD_Fp80m   : FpI_<(outs RFP80:$dst), (ins f80mem:$src), ZeroArgFP,
                  [(set RFP80:$dst, (loadf80 addr:$src))]>;
} // canFoldAsLoad
def LD_Fp32m64 : FpIf64<(outs RFP64:$dst), (ins f32mem:$src), ZeroArgFP,
                  [(set RFP64:$dst, (f64 (extloadf32 addr:$src)))]>;
def LD_Fp64m80 : FpI_<(outs RFP80:$dst), (ins f64mem:$src), ZeroArgFP,
                  [(set RFP80:$dst, (f80 (extloadf64 addr:$src)))]>;
def LD_Fp32m80 : FpI_<(outs RFP80:$dst), (ins f32mem:$src), ZeroArgFP,
                  [(set RFP80:$dst, (f80 (extloadf32 addr:$src)))]>;
def ILD_Fp16m32: FpIf32<(outs RFP32:$dst), (ins i16mem:$src), ZeroArgFP,
                  [(set RFP32:$dst, (X86fild addr:$src, i16))]>;
def ILD_Fp32m32: FpIf32<(outs RFP32:$dst), (ins i32mem:$src), ZeroArgFP,
                  [(set RFP32:$dst, (X86fild addr:$src, i32))]>;
def ILD_Fp64m32: FpIf32<(outs RFP32:$dst), (ins i64mem:$src), ZeroArgFP,
                  [(set RFP32:$dst, (X86fild addr:$src, i64))]>;
def ILD_Fp16m64: FpIf64<(outs RFP64:$dst), (ins i16mem:$src), ZeroArgFP,
                  [(set RFP64:$dst, (X86fild addr:$src, i16))]>;
def ILD_Fp32m64: FpIf64<(outs RFP64:$dst), (ins i32mem:$src), ZeroArgFP,
                  [(set RFP64:$dst, (X86fild addr:$src, i32))]>;
def ILD_Fp64m64: FpIf64<(outs RFP64:$dst), (ins i64mem:$src), ZeroArgFP,
                  [(set RFP64:$dst, (X86fild addr:$src, i64))]>;
def ILD_Fp16m80: FpI_<(outs RFP80:$dst), (ins i16mem:$src), ZeroArgFP,
                  [(set RFP80:$dst, (X86fild addr:$src, i16))]>;
def ILD_Fp32m80: FpI_<(outs RFP80:$dst), (ins i32mem:$src), ZeroArgFP,
                  [(set RFP80:$dst, (X86fild addr:$src, i32))]>;
def ILD_Fp64m80: FpI_<(outs RFP80:$dst), (ins i64mem:$src), ZeroArgFP,
                  [(set RFP80:$dst, (X86fild addr:$src, i64))]>;
} // SchedRW

let SchedRW = [WriteStore], Uses = [FPCW] in {
def ST_Fp32m   : FpIf32<(outs), (ins f32mem:$op, RFP32:$src), OneArgFP,
                  [(store RFP32:$src, addr:$op)]>;
def ST_Fp64m32 : FpIf64<(outs), (ins f32mem:$op, RFP64:$src), OneArgFP,
                  [(truncstoref32 RFP64:$src, addr:$op)]>;
def ST_Fp64m   : FpIf64<(outs), (ins f64mem:$op, RFP64:$src), OneArgFP,
                  [(store RFP64:$src, addr:$op)]>;
def ST_Fp80m32 : FpI_<(outs), (ins f32mem:$op, RFP80:$src), OneArgFP,
                  [(truncstoref32 RFP80:$src, addr:$op)]>;
def ST_Fp80m64 : FpI_<(outs), (ins f64mem:$op, RFP80:$src), OneArgFP,
                  [(truncstoref64 RFP80:$src, addr:$op)]>;
// FST does not support 80-bit memory target; FSTP must be used.

let mayStore = 1, hasSideEffects = 0 in {
def ST_FpP32m    : FpIf32<(outs), (ins f32mem:$op, RFP32:$src), OneArgFP, []>;
def ST_FpP64m32  : FpIf64<(outs), (ins f32mem:$op, RFP64:$src), OneArgFP, []>;
def ST_FpP64m    : FpIf64<(outs), (ins f64mem:$op, RFP64:$src), OneArgFP, []>;
def ST_FpP80m32  : FpI_<(outs), (ins f32mem:$op, RFP80:$src), OneArgFP, []>;
def ST_FpP80m64  : FpI_<(outs), (ins f64mem:$op, RFP80:$src), OneArgFP, []>;
} // mayStore

def ST_FpP80m    : FpI_<(outs), (ins f80mem:$op, RFP80:$src), OneArgFP,
                    [(store RFP80:$src, addr:$op)]>;

let mayStore = 1, hasSideEffects = 0 in {
def IST_Fp16m32  : FpIf32<(outs), (ins i16mem:$op, RFP32:$src), OneArgFP, []>;
def IST_Fp32m32  : FpIf32<(outs), (ins i32mem:$op, RFP32:$src), OneArgFP, []>;
def IST_Fp64m32  : FpIf32<(outs), (ins i64mem:$op, RFP32:$src), OneArgFP, []>;
def IST_Fp16m64  : FpIf64<(outs), (ins i16mem:$op, RFP64:$src), OneArgFP, []>;
def IST_Fp32m64  : FpIf64<(outs), (ins i32mem:$op, RFP64:$src), OneArgFP, []>;
def IST_Fp64m64  : FpIf64<(outs), (ins i64mem:$op, RFP64:$src), OneArgFP, []>;
def IST_Fp16m80  : FpI_<(outs), (ins i16mem:$op, RFP80:$src), OneArgFP, []>;
def IST_Fp32m80  : FpI_<(outs), (ins i32mem:$op, RFP80:$src), OneArgFP, []>;
def IST_Fp64m80  : FpI_<(outs), (ins i64mem:$op, RFP80:$src), OneArgFP, []>;
} // mayStore
} // SchedRW, Uses = [FPCW]

let mayLoad = 1, SchedRW = [WriteLoad] in {
def LD_F32m   : FPI<0xD9, MRM0m, (outs), (ins f32mem:$src), "fld{s}\t$src">;
def LD_F64m   : FPI<0xDD, MRM0m, (outs), (ins f64mem:$src), "fld{l}\t$src">;
def LD_F80m   : FPI<0xDB, MRM5m, (outs), (ins f80mem:$src), "fld{t}\t$src">;
def ILD_F16m  : FPI<0xDF, MRM0m, (outs), (ins i16mem:$src), "fild{s}\t$src">;
def ILD_F32m  : FPI<0xDB, MRM0m, (outs), (ins i32mem:$src), "fild{l}\t$src">;
def ILD_F64m  : FPI<0xDF, MRM5m, (outs), (ins i64mem:$src), "fild{ll}\t$src">;
}
let mayStore = 1, SchedRW = [WriteStore], Uses = [FPCW] in {
def ST_F32m   : FPI<0xD9, MRM2m, (outs), (ins f32mem:$dst), "fst{s}\t$dst">;
def ST_F64m   : FPI<0xDD, MRM2m, (outs), (ins f64mem:$dst), "fst{l}\t$dst">;
def ST_FP32m  : FPI<0xD9, MRM3m, (outs), (ins f32mem:$dst), "fstp{s}\t$dst">;
def ST_FP64m  : FPI<0xDD, MRM3m, (outs), (ins f64mem:$dst), "fstp{l}\t$dst">;
def ST_FP80m  : FPI<0xDB, MRM7m, (outs), (ins f80mem:$dst), "fstp{t}\t$dst">;
def IST_F16m  : FPI<0xDF, MRM2m, (outs), (ins i16mem:$dst), "fist{s}\t$dst">;
def IST_F32m  : FPI<0xDB, MRM2m, (outs), (ins i32mem:$dst), "fist{l}\t$dst">;
def IST_FP16m : FPI<0xDF, MRM3m, (outs), (ins i16mem:$dst), "fistp{s}\t$dst">;
def IST_FP32m : FPI<0xDB, MRM3m, (outs), (ins i32mem:$dst), "fistp{l}\t$dst">;
def IST_FP64m : FPI<0xDF, MRM7m, (outs), (ins i64mem:$dst), "fistp{ll}\t$dst">;
}

// FISTTP requires SSE3 even though it's a FPStack op.
let Predicates = [HasSSE3], SchedRW = [WriteStore], Uses = [FPCW] in {
def ISTT_Fp16m32 : FpI_<(outs), (ins i16mem:$op, RFP32:$src), OneArgFP,
                    [(X86fp_to_i16mem RFP32:$src, addr:$op)]>;
def ISTT_Fp32m32 : FpI_<(outs), (ins i32mem:$op, RFP32:$src), OneArgFP,
                    [(X86fp_to_i32mem RFP32:$src, addr:$op)]>;
def ISTT_Fp64m32 : FpI_<(outs), (ins i64mem:$op, RFP32:$src), OneArgFP,
                    [(X86fp_to_i64mem RFP32:$src, addr:$op)]>;
def ISTT_Fp16m64 : FpI_<(outs), (ins i16mem:$op, RFP64:$src), OneArgFP,
                    [(X86fp_to_i16mem RFP64:$src, addr:$op)]>;
def ISTT_Fp32m64 : FpI_<(outs), (ins i32mem:$op, RFP64:$src), OneArgFP,
                    [(X86fp_to_i32mem RFP64:$src, addr:$op)]>;
def ISTT_Fp64m64 : FpI_<(outs), (ins i64mem:$op, RFP64:$src), OneArgFP,
                    [(X86fp_to_i64mem RFP64:$src, addr:$op)]>;
def ISTT_Fp16m80 : FpI_<(outs), (ins i16mem:$op, RFP80:$src), OneArgFP,
                    [(X86fp_to_i16mem RFP80:$src, addr:$op)]>;
def ISTT_Fp32m80 : FpI_<(outs), (ins i32mem:$op, RFP80:$src), OneArgFP,
                    [(X86fp_to_i32mem RFP80:$src, addr:$op)]>;
def ISTT_Fp64m80 : FpI_<(outs), (ins i64mem:$op, RFP80:$src), OneArgFP,
                    [(X86fp_to_i64mem RFP80:$src, addr:$op)]>;
} // Predicates = [HasSSE3]

let mayStore = 1, SchedRW = [WriteStore], Uses = [FPCW] in {
def ISTT_FP16m : FPI<0xDF, MRM1m, (outs), (ins i16mem:$dst), "fisttp{s}\t$dst">;
def ISTT_FP32m : FPI<0xDB, MRM1m, (outs), (ins i32mem:$dst), "fisttp{l}\t$dst">;
def ISTT_FP64m : FPI<0xDD, MRM1m, (outs), (ins i64mem:$dst), "fisttp{ll}\t$dst">;
}

// FP Stack manipulation instructions.
let SchedRW = [WriteMove] in {
def LD_Frr   : FPI<0xD9, MRM0r, (outs), (ins RSTi:$op), "fld\t$op">;
def ST_Frr   : FPI<0xDD, MRM2r, (outs), (ins RSTi:$op), "fst\t$op">;
def ST_FPrr  : FPI<0xDD, MRM3r, (outs), (ins RSTi:$op), "fstp\t$op">;
def XCH_F    : FPI<0xD9, MRM1r, (outs), (ins RSTi:$op), "fxch\t$op">;
}

// Floating point constant loads.
let isReMaterializable = 1, SchedRW = [WriteZero] in {
def LD_Fp032 : FpIf32<(outs RFP32:$dst), (ins), ZeroArgFP,
                [(set RFP32:$dst, fpimm0)]>;
def LD_Fp132 : FpIf32<(outs RFP32:$dst), (ins), ZeroArgFP,
                [(set RFP32:$dst, fpimm1)]>;
def LD_Fp064 : FpIf64<(outs RFP64:$dst), (ins), ZeroArgFP,
                [(set RFP64:$dst, fpimm0)]>;
def LD_Fp164 : FpIf64<(outs RFP64:$dst), (ins), ZeroArgFP,
                [(set RFP64:$dst, fpimm1)]>;
def LD_Fp080 : FpI_<(outs RFP80:$dst), (ins), ZeroArgFP,
                [(set RFP80:$dst, fpimm0)]>;
def LD_Fp180 : FpI_<(outs RFP80:$dst), (ins), ZeroArgFP,
                [(set RFP80:$dst, fpimm1)]>;
}

let SchedRW = [WriteFLD0] in
def LD_F0 : FPI<0xD9, MRM_EE, (outs), (ins), "fldz">;

let SchedRW = [WriteFLD1] in
def LD_F1 : FPI<0xD9, MRM_E8, (outs), (ins), "fld1">;

let SchedRW = [WriteFLDC] in {
def FLDL2T : I<0xD9, MRM_E9, (outs), (ins), "fldl2t", []>;
def FLDL2E : I<0xD9, MRM_EA, (outs), (ins), "fldl2e", []>;
def FLDPI : I<0xD9, MRM_EB, (outs), (ins), "fldpi", []>;
def FLDLG2 : I<0xD9, MRM_EC, (outs), (ins), "fldlg2", []>;
def FLDLN2 : I<0xD9, MRM_ED, (outs), (ins), "fldln2", []>;
} // SchedRW

// Floating point compares.
let SchedRW = [WriteFCom], Uses = [FPCW] in {
def UCOM_Fpr32 : FpIf32<(outs), (ins RFP32:$lhs, RFP32:$rhs), CompareFP,
                        [(set FPSW, (trunc (X86cmp RFP32:$lhs, RFP32:$rhs)))]>;
def UCOM_Fpr64 : FpIf64<(outs), (ins RFP64:$lhs, RFP64:$rhs), CompareFP,
                        [(set FPSW, (trunc (X86cmp RFP64:$lhs, RFP64:$rhs)))]>;
def UCOM_Fpr80 : FpI_  <(outs), (ins RFP80:$lhs, RFP80:$rhs), CompareFP,
                        [(set FPSW, (trunc (X86cmp RFP80:$lhs, RFP80:$rhs)))]>;
} // SchedRW
} // Defs = [FPSW]

let SchedRW = [WriteFCom] in {
// CC = ST(0) cmp ST(i)
let Defs = [EFLAGS, FPSW], Uses = [FPCW] in {
def UCOM_FpIr32: FpI_<(outs), (ins RFP32:$lhs, RFP32:$rhs), CompareFP,
                  [(set EFLAGS, (X86cmp RFP32:$lhs, RFP32:$rhs))]>,
                  Requires<[FPStackf32, HasCMov]>;
def UCOM_FpIr64: FpI_<(outs), (ins RFP64:$lhs, RFP64:$rhs), CompareFP,
                  [(set EFLAGS, (X86cmp RFP64:$lhs, RFP64:$rhs))]>,
                  Requires<[FPStackf64, HasCMov]>;
def UCOM_FpIr80: FpI_<(outs), (ins RFP80:$lhs, RFP80:$rhs), CompareFP,
                  [(set EFLAGS, (X86cmp RFP80:$lhs, RFP80:$rhs))]>,
                  Requires<[HasCMov]>;
}

let Defs = [FPSW], Uses = [ST0, FPCW] in {
def UCOM_Fr    : FPI<0xDD, MRM4r,    // FPSW = cmp ST(0) with ST(i)
                    (outs), (ins RSTi:$reg), "fucom\t$reg">;
def UCOM_FPr   : FPI<0xDD, MRM5r,    // FPSW = cmp ST(0) with ST(i), pop
                    (outs), (ins RSTi:$reg), "fucomp\t$reg">;
def UCOM_FPPr  : FPI<0xDA, MRM_E9,       // cmp ST(0) with ST(1), pop, pop
                    (outs), (ins), "fucompp">;
}

let Defs = [EFLAGS, FPSW], Uses = [ST0, FPCW] in {
def UCOM_FIr   : FPI<0xDB, MRM5r,     // CC = cmp ST(0) with ST(i)
                    (outs), (ins RSTi:$reg), "fucomi\t{$reg, %st|st, $reg}">;
def UCOM_FIPr  : FPI<0xDF, MRM5r,     // CC = cmp ST(0) with ST(i), pop
                    (outs), (ins RSTi:$reg), "fucompi\t{$reg, %st|st, $reg}">;

def COM_FIr : FPI<0xDB, MRM6r, (outs), (ins RSTi:$reg),
                  "fcomi\t{$reg, %st|st, $reg}">;
def COM_FIPr : FPI<0xDF, MRM6r, (outs), (ins RSTi:$reg),
                   "fcompi\t{$reg, %st|st, $reg}">;
}
} // SchedRW

// Floating point flag ops.
let SchedRW = [WriteALU] in {
let Defs = [AX], Uses = [FPSW] in
def FNSTSW16r : I<0xDF, MRM_E0,                  // AX = fp flags
                  (outs), (ins), "fnstsw\t{%ax|ax}",
                  [(set AX, (X86fp_stsw FPSW))]>;
let Defs = [FPSW], Uses = [FPCW] in
def FNSTCW16m : I<0xD9, MRM7m,                   // [mem16] = X87 control world
                  (outs), (ins i16mem:$dst), "fnstcw\t$dst",
                  [(X86fp_cwd_get16 addr:$dst)]>;
} // SchedRW
let Defs = [FPSW,FPCW], mayLoad = 1 in
def FLDCW16m  : I<0xD9, MRM5m,                   // X87 control world = [mem16]
                  (outs), (ins i16mem:$dst), "fldcw\t$dst", []>,
                Sched<[WriteLoad]>;

// FPU control instructions
let SchedRW = [WriteMicrocoded] in {
let Defs = [FPSW] in {
def FNINIT : I<0xDB, MRM_E3, (outs), (ins), "fninit", []>;
def FFREE : FPI<0xDD, MRM0r, (outs), (ins RSTi:$reg), "ffree\t$reg">;
def FFREEP : FPI<0xDF, MRM0r, (outs), (ins RSTi:$reg), "ffreep\t$reg">;

// Clear exceptions
def FNCLEX : I<0xDB, MRM_E2, (outs), (ins), "fnclex", []>;
} // Defs = [FPSW]
} // SchedRW

// Operand-less floating-point instructions for the disassembler.
def FNOP : I<0xD9, MRM_D0, (outs), (ins), "fnop", []>, Sched<[WriteNop]>;

let SchedRW = [WriteMicrocoded] in {
let Defs = [FPSW] in {
def WAIT : I<0x9B, RawFrm, (outs), (ins), "wait", []>;
def FXAM : I<0xD9, MRM_E5, (outs), (ins), "fxam", []>;
def F2XM1 : I<0xD9, MRM_F0, (outs), (ins), "f2xm1", []>;
def FYL2X : I<0xD9, MRM_F1, (outs), (ins), "fyl2x", []>;
def FPTAN : I<0xD9, MRM_F2, (outs), (ins), "fptan", []>;
def FPATAN : I<0xD9, MRM_F3, (outs), (ins), "fpatan", []>;
def FXTRACT : I<0xD9, MRM_F4, (outs), (ins), "fxtract", []>;
def FPREM1 : I<0xD9, MRM_F5, (outs), (ins), "fprem1", []>;
def FDECSTP : I<0xD9, MRM_F6, (outs), (ins), "fdecstp", []>;
def FINCSTP : I<0xD9, MRM_F7, (outs), (ins), "fincstp", []>;
def FPREM : I<0xD9, MRM_F8, (outs), (ins), "fprem", []>;
def FYL2XP1 : I<0xD9, MRM_F9, (outs), (ins), "fyl2xp1", []>;
def FSINCOS : I<0xD9, MRM_FB, (outs), (ins), "fsincos", []>;
def FRNDINT : I<0xD9, MRM_FC, (outs), (ins), "frndint", []>;
def FSCALE : I<0xD9, MRM_FD, (outs), (ins), "fscale", []>;
def FCOMPP : I<0xDE, MRM_D9, (outs), (ins), "fcompp", []>;
} // Defs = [FPSW]

def FXSAVE : I<0xAE, MRM0m, (outs), (ins opaquemem:$dst),
             "fxsave\t$dst", [(int_x86_fxsave addr:$dst)]>, TB,
             Requires<[HasFXSR]>;
def FXSAVE64 : RI<0xAE, MRM0m, (outs), (ins opaquemem:$dst),
               "fxsave64\t$dst", [(int_x86_fxsave64 addr:$dst)]>,
               TB, Requires<[HasFXSR, In64BitMode]>;
def FXRSTOR : I<0xAE, MRM1m, (outs), (ins opaquemem:$src),
              "fxrstor\t$src", [(int_x86_fxrstor addr:$src)]>,
              TB, Requires<[HasFXSR]>;
def FXRSTOR64 : RI<0xAE, MRM1m, (outs), (ins opaquemem:$src),
                "fxrstor64\t$src", [(int_x86_fxrstor64 addr:$src)]>,
                TB, Requires<[HasFXSR, In64BitMode]>;
} // SchedRW

//===----------------------------------------------------------------------===//
// Non-Instruction Patterns
//===----------------------------------------------------------------------===//

// Required for RET of f32 / f64 / f80 values.
def : Pat<(X86fld addr:$src, f32), (LD_Fp32m addr:$src)>;
def : Pat<(X86fld addr:$src, f64), (LD_Fp64m addr:$src)>;
def : Pat<(X86fld addr:$src, f80), (LD_Fp80m addr:$src)>;

// Required for CALL which return f32 / f64 / f80 values.
def : Pat<(X86fst RFP32:$src, addr:$op, f32), (ST_Fp32m addr:$op, RFP32:$src)>;
def : Pat<(X86fst RFP64:$src, addr:$op, f32), (ST_Fp64m32 addr:$op,
                                                          RFP64:$src)>;
def : Pat<(X86fst RFP64:$src, addr:$op, f64), (ST_Fp64m addr:$op, RFP64:$src)>;
def : Pat<(X86fst RFP80:$src, addr:$op, f32), (ST_Fp80m32 addr:$op,
                                                          RFP80:$src)>;
def : Pat<(X86fst RFP80:$src, addr:$op, f64), (ST_Fp80m64 addr:$op,
                                                          RFP80:$src)>;
def : Pat<(X86fst RFP80:$src, addr:$op, f80), (ST_FpP80m addr:$op,
                                                         RFP80:$src)>;

// Floating point constant -0.0 and -1.0
def : Pat<(f32 fpimmneg0), (CHS_Fp32 (LD_Fp032))>, Requires<[FPStackf32]>;
def : Pat<(f32 fpimmneg1), (CHS_Fp32 (LD_Fp132))>, Requires<[FPStackf32]>;
def : Pat<(f64 fpimmneg0), (CHS_Fp64 (LD_Fp064))>, Requires<[FPStackf64]>;
def : Pat<(f64 fpimmneg1), (CHS_Fp64 (LD_Fp164))>, Requires<[FPStackf64]>;
def : Pat<(f80 fpimmneg0), (CHS_Fp80 (LD_Fp080))>;
def : Pat<(f80 fpimmneg1), (CHS_Fp80 (LD_Fp180))>;

// Used to conv. i64 to f64 since there isn't a SSE version.
def : Pat<(X86fildflag addr:$src, i64), (ILD_Fp64m64 addr:$src)>;

// FP extensions map onto simple pseudo-value conversions if they are to/from
// the FP stack.
def : Pat<(f64 (fpextend RFP32:$src)), (COPY_TO_REGCLASS RFP32:$src, RFP64)>,
          Requires<[FPStackf32]>;
def : Pat<(f80 (fpextend RFP32:$src)), (COPY_TO_REGCLASS RFP32:$src, RFP80)>,
           Requires<[FPStackf32]>;
def : Pat<(f80 (fpextend RFP64:$src)), (COPY_TO_REGCLASS RFP64:$src, RFP80)>,
           Requires<[FPStackf64]>;

// FP truncations map onto simple pseudo-value conversions if they are to/from
// the FP stack.  We have validated that only value-preserving truncations make
// it through isel.
def : Pat<(f32 (fpround RFP64:$src)), (COPY_TO_REGCLASS RFP64:$src, RFP32)>,
          Requires<[FPStackf32]>;
def : Pat<(f32 (fpround RFP80:$src)), (COPY_TO_REGCLASS RFP80:$src, RFP32)>,
           Requires<[FPStackf32]>;
def : Pat<(f64 (fpround RFP80:$src)), (COPY_TO_REGCLASS RFP80:$src, RFP64)>,
           Requires<[FPStackf64]>;
