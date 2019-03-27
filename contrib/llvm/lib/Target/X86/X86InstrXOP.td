//===-- X86InstrXOP.td - XOP Instruction Set ---------------*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file describes XOP (eXtended OPerations)
//
//===----------------------------------------------------------------------===//

multiclass xop2op<bits<8> opc, string OpcodeStr, Intrinsic Int> {
  def rr : IXOP<opc, MRMSrcReg, (outs VR128:$dst), (ins VR128:$src),
           !strconcat(OpcodeStr, "\t{$src, $dst|$dst, $src}"),
           [(set VR128:$dst, (Int VR128:$src))]>, XOP, Sched<[SchedWritePHAdd.XMM]>;
  def rm : IXOP<opc, MRMSrcMem, (outs VR128:$dst), (ins i128mem:$src),
           !strconcat(OpcodeStr, "\t{$src, $dst|$dst, $src}"),
           [(set VR128:$dst, (Int (load addr:$src)))]>, XOP,
           Sched<[SchedWritePHAdd.XMM.Folded, SchedWritePHAdd.XMM.ReadAfterFold]>;
}

let ExeDomain = SSEPackedInt in {
  defm VPHSUBWD  : xop2op<0xE2, "vphsubwd", int_x86_xop_vphsubwd>;
  defm VPHSUBDQ  : xop2op<0xE3, "vphsubdq", int_x86_xop_vphsubdq>;
  defm VPHSUBBW  : xop2op<0xE1, "vphsubbw", int_x86_xop_vphsubbw>;
  defm VPHADDWQ  : xop2op<0xC7, "vphaddwq", int_x86_xop_vphaddwq>;
  defm VPHADDWD  : xop2op<0xC6, "vphaddwd", int_x86_xop_vphaddwd>;
  defm VPHADDUWQ : xop2op<0xD7, "vphadduwq", int_x86_xop_vphadduwq>;
  defm VPHADDUWD : xop2op<0xD6, "vphadduwd", int_x86_xop_vphadduwd>;
  defm VPHADDUDQ : xop2op<0xDB, "vphaddudq", int_x86_xop_vphaddudq>;
  defm VPHADDUBW : xop2op<0xD1, "vphaddubw", int_x86_xop_vphaddubw>;
  defm VPHADDUBQ : xop2op<0xD3, "vphaddubq", int_x86_xop_vphaddubq>;
  defm VPHADDUBD : xop2op<0xD2, "vphaddubd", int_x86_xop_vphaddubd>;
  defm VPHADDDQ  : xop2op<0xCB, "vphadddq", int_x86_xop_vphadddq>;
  defm VPHADDBW  : xop2op<0xC1, "vphaddbw", int_x86_xop_vphaddbw>;
  defm VPHADDBQ  : xop2op<0xC3, "vphaddbq", int_x86_xop_vphaddbq>;
  defm VPHADDBD  : xop2op<0xC2, "vphaddbd", int_x86_xop_vphaddbd>;
}

// Scalar load 2 addr operand instructions
multiclass xop2opsld<bits<8> opc, string OpcodeStr, Intrinsic Int,
                     Operand memop, ComplexPattern mem_cpat,
                     X86FoldableSchedWrite sched> {
  def rr : IXOP<opc, MRMSrcReg, (outs VR128:$dst), (ins VR128:$src),
           !strconcat(OpcodeStr, "\t{$src, $dst|$dst, $src}"),
           [(set VR128:$dst, (Int VR128:$src))]>, XOP, Sched<[sched]>;
  def rm : IXOP<opc, MRMSrcMem, (outs VR128:$dst), (ins memop:$src),
           !strconcat(OpcodeStr, "\t{$src, $dst|$dst, $src}"),
           [(set VR128:$dst, (Int mem_cpat:$src))]>, XOP,
           Sched<[sched.Folded, sched.ReadAfterFold]>;
}

multiclass xop2op128<bits<8> opc, string OpcodeStr, Intrinsic Int,
                     X86FoldableSchedWrite sched> {
  def rr : IXOP<opc, MRMSrcReg, (outs VR128:$dst), (ins VR128:$src),
           !strconcat(OpcodeStr, "\t{$src, $dst|$dst, $src}"),
           [(set VR128:$dst, (Int VR128:$src))]>, XOP, Sched<[sched]>;
  def rm : IXOP<opc, MRMSrcMem, (outs VR128:$dst), (ins f128mem:$src),
           !strconcat(OpcodeStr, "\t{$src, $dst|$dst, $src}"),
           [(set VR128:$dst, (Int (load addr:$src)))]>, XOP,
           Sched<[sched.Folded, sched.ReadAfterFold]>;
}

multiclass xop2op256<bits<8> opc, string OpcodeStr, Intrinsic Int,
                     X86FoldableSchedWrite sched> {
  def Yrr : IXOP<opc, MRMSrcReg, (outs VR256:$dst), (ins VR256:$src),
           !strconcat(OpcodeStr, "\t{$src, $dst|$dst, $src}"),
           [(set VR256:$dst, (Int VR256:$src))]>, XOP, VEX_L, Sched<[sched]>;
  def Yrm : IXOP<opc, MRMSrcMem, (outs VR256:$dst), (ins f256mem:$src),
           !strconcat(OpcodeStr, "\t{$src, $dst|$dst, $src}"),
           [(set VR256:$dst, (Int (load addr:$src)))]>, XOP, VEX_L,
           Sched<[sched.Folded, sched.ReadAfterFold]>;
}

let ExeDomain = SSEPackedSingle in {
  defm VFRCZSS : xop2opsld<0x82, "vfrczss", int_x86_xop_vfrcz_ss,
                           ssmem, sse_load_f32, SchedWriteFRnd.Scl>;
  defm VFRCZPS : xop2op128<0x80, "vfrczps", int_x86_xop_vfrcz_ps,
                           SchedWriteFRnd.XMM>;
  defm VFRCZPS : xop2op256<0x80, "vfrczps", int_x86_xop_vfrcz_ps_256,
                           SchedWriteFRnd.YMM>;
}

let ExeDomain = SSEPackedDouble in {
  defm VFRCZSD : xop2opsld<0x83, "vfrczsd", int_x86_xop_vfrcz_sd,
                           sdmem, sse_load_f64, SchedWriteFRnd.Scl>;
  defm VFRCZPD : xop2op128<0x81, "vfrczpd", int_x86_xop_vfrcz_pd,
                           SchedWriteFRnd.XMM>;
  defm VFRCZPD : xop2op256<0x81, "vfrczpd", int_x86_xop_vfrcz_pd_256,
                           SchedWriteFRnd.YMM>;
}

multiclass xop3op<bits<8> opc, string OpcodeStr, SDNode OpNode,
                  ValueType vt128, X86FoldableSchedWrite sched> {
  def rr : IXOP<opc, MRMSrcReg4VOp3, (outs VR128:$dst),
           (ins VR128:$src1, VR128:$src2),
           !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
           [(set VR128:$dst,
              (vt128 (OpNode (vt128 VR128:$src1), (vt128 VR128:$src2))))]>,
           XOP, Sched<[sched]>;
  def rm : IXOP<opc, MRMSrcMem, (outs VR128:$dst),
           (ins VR128:$src1, i128mem:$src2),
           !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
           [(set VR128:$dst,
              (vt128 (OpNode (vt128 VR128:$src1),
                             (vt128 (load addr:$src2)))))]>,
           XOP_4V, VEX_W, Sched<[sched.Folded, sched.ReadAfterFold]>;
  def mr : IXOP<opc, MRMSrcMem4VOp3, (outs VR128:$dst),
           (ins i128mem:$src1, VR128:$src2),
           !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
           [(set VR128:$dst,
              (vt128 (OpNode (vt128 (load addr:$src1)),
                             (vt128 VR128:$src2))))]>,
             XOP, Sched<[sched.Folded, sched.ReadAfterFold]>;
  // For disassembler
  let isCodeGenOnly = 1, ForceDisassemble = 1, hasSideEffects = 0 in
  def rr_REV : IXOP<opc, MRMSrcReg, (outs VR128:$dst),
               (ins VR128:$src1, VR128:$src2),
               !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
               []>,
               XOP_4V, VEX_W, Sched<[sched]>, FoldGenData<NAME#rr>;
}

let ExeDomain = SSEPackedInt in {
  defm VPROTB : xop3op<0x90, "vprotb", rotl, v16i8, SchedWriteVarVecShift.XMM>;
  defm VPROTD : xop3op<0x92, "vprotd", rotl, v4i32, SchedWriteVarVecShift.XMM>;
  defm VPROTQ : xop3op<0x93, "vprotq", rotl, v2i64, SchedWriteVarVecShift.XMM>;
  defm VPROTW : xop3op<0x91, "vprotw", rotl, v8i16, SchedWriteVarVecShift.XMM>;
  defm VPSHAB : xop3op<0x98, "vpshab", X86vpsha, v16i8, SchedWriteVarVecShift.XMM>;
  defm VPSHAD : xop3op<0x9A, "vpshad", X86vpsha, v4i32, SchedWriteVarVecShift.XMM>;
  defm VPSHAQ : xop3op<0x9B, "vpshaq", X86vpsha, v2i64, SchedWriteVarVecShift.XMM>;
  defm VPSHAW : xop3op<0x99, "vpshaw", X86vpsha, v8i16, SchedWriteVarVecShift.XMM>;
  defm VPSHLB : xop3op<0x94, "vpshlb", X86vpshl, v16i8, SchedWriteVarVecShift.XMM>;
  defm VPSHLD : xop3op<0x96, "vpshld", X86vpshl, v4i32, SchedWriteVarVecShift.XMM>;
  defm VPSHLQ : xop3op<0x97, "vpshlq", X86vpshl, v2i64, SchedWriteVarVecShift.XMM>;
  defm VPSHLW : xop3op<0x95, "vpshlw", X86vpshl, v8i16, SchedWriteVarVecShift.XMM>;
}

multiclass xop3opimm<bits<8> opc, string OpcodeStr, SDNode OpNode,
                     ValueType vt128, X86FoldableSchedWrite sched> {
  def ri : IXOPi8<opc, MRMSrcReg, (outs VR128:$dst),
           (ins VR128:$src1, u8imm:$src2),
           !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
           [(set VR128:$dst,
              (vt128 (OpNode (vt128 VR128:$src1), imm:$src2)))]>,
           XOP, Sched<[sched]>;
  def mi : IXOPi8<opc, MRMSrcMem, (outs VR128:$dst),
           (ins i128mem:$src1, u8imm:$src2),
           !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
           [(set VR128:$dst,
              (vt128 (OpNode (vt128 (load addr:$src1)), imm:$src2)))]>,
           XOP, Sched<[sched.Folded, sched.ReadAfterFold]>;
}

let ExeDomain = SSEPackedInt in {
  defm VPROTB : xop3opimm<0xC0, "vprotb", X86vrotli, v16i8,
                          SchedWriteVecShiftImm.XMM>;
  defm VPROTD : xop3opimm<0xC2, "vprotd", X86vrotli, v4i32,
                          SchedWriteVecShiftImm.XMM>;
  defm VPROTQ : xop3opimm<0xC3, "vprotq", X86vrotli, v2i64,
                          SchedWriteVecShiftImm.XMM>;
  defm VPROTW : xop3opimm<0xC1, "vprotw", X86vrotli, v8i16,
                          SchedWriteVecShiftImm.XMM>;
}

// Instruction where second source can be memory, but third must be register
multiclass xop4opm2<bits<8> opc, string OpcodeStr, Intrinsic Int,
                    X86FoldableSchedWrite sched> {
  let isCommutable = 1 in
  def rr : IXOPi8Reg<opc, MRMSrcReg, (outs VR128:$dst),
           (ins VR128:$src1, VR128:$src2, VR128:$src3),
           !strconcat(OpcodeStr,
           "\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}"),
           [(set VR128:$dst,
              (Int VR128:$src1, VR128:$src2, VR128:$src3))]>, XOP_4V,
           Sched<[sched]>;
  def rm : IXOPi8Reg<opc, MRMSrcMem, (outs VR128:$dst),
           (ins VR128:$src1, i128mem:$src2, VR128:$src3),
           !strconcat(OpcodeStr,
           "\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}"),
           [(set VR128:$dst,
              (Int VR128:$src1, (load addr:$src2),
              VR128:$src3))]>, XOP_4V, Sched<[sched.Folded, sched.ReadAfterFold]>;
}

let ExeDomain = SSEPackedInt in {
  defm VPMADCSWD  : xop4opm2<0xB6, "vpmadcswd",
                             int_x86_xop_vpmadcswd, SchedWriteVecIMul.XMM>;
  defm VPMADCSSWD : xop4opm2<0xA6, "vpmadcsswd",
                             int_x86_xop_vpmadcsswd, SchedWriteVecIMul.XMM>;
  defm VPMACSWW   : xop4opm2<0x95, "vpmacsww",
                             int_x86_xop_vpmacsww, SchedWriteVecIMul.XMM>;
  defm VPMACSWD   : xop4opm2<0x96, "vpmacswd",
                             int_x86_xop_vpmacswd, SchedWriteVecIMul.XMM>;
  defm VPMACSSWW  : xop4opm2<0x85, "vpmacssww",
                             int_x86_xop_vpmacssww, SchedWriteVecIMul.XMM>;
  defm VPMACSSWD  : xop4opm2<0x86, "vpmacsswd",
                             int_x86_xop_vpmacsswd, SchedWriteVecIMul.XMM>;
  defm VPMACSSDQL : xop4opm2<0x87, "vpmacssdql",
                             int_x86_xop_vpmacssdql, SchedWritePMULLD.XMM>;
  defm VPMACSSDQH : xop4opm2<0x8F, "vpmacssdqh",
                             int_x86_xop_vpmacssdqh, SchedWritePMULLD.XMM>;
  defm VPMACSSDD  : xop4opm2<0x8E, "vpmacssdd",
                             int_x86_xop_vpmacssdd, SchedWritePMULLD.XMM>;
  defm VPMACSDQL  : xop4opm2<0x97, "vpmacsdql",
                             int_x86_xop_vpmacsdql, SchedWritePMULLD.XMM>;
  defm VPMACSDQH  : xop4opm2<0x9F, "vpmacsdqh",
                             int_x86_xop_vpmacsdqh, SchedWritePMULLD.XMM>;
  defm VPMACSDD   : xop4opm2<0x9E, "vpmacsdd",
                             int_x86_xop_vpmacsdd, SchedWritePMULLD.XMM>;
}

// IFMA patterns - for cases where we can safely ignore the overflow bits from
// the multiply or easily match with existing intrinsics.
let Predicates = [HasXOP] in {
  def : Pat<(v8i16 (add (mul (v8i16 VR128:$src1), (v8i16 VR128:$src2)),
                        (v8i16 VR128:$src3))),
            (VPMACSWWrr VR128:$src1, VR128:$src2, VR128:$src3)>;
  def : Pat<(v4i32 (add (mul (v4i32 VR128:$src1), (v4i32 VR128:$src2)),
                        (v4i32 VR128:$src3))),
            (VPMACSDDrr VR128:$src1, VR128:$src2, VR128:$src3)>;
  def : Pat<(v2i64 (add (X86pmuldq (bc_v2i64 (X86PShufd (v4i32 VR128:$src1), (i8 -11))),
                                   (bc_v2i64 (X86PShufd (v4i32 VR128:$src2), (i8 -11)))),
                        (v2i64 VR128:$src3))),
            (VPMACSDQHrr VR128:$src1, VR128:$src2, VR128:$src3)>;
  def : Pat<(v2i64 (add (X86pmuldq (v2i64 VR128:$src1), (v2i64 VR128:$src2)),
                        (v2i64 VR128:$src3))),
            (VPMACSDQLrr VR128:$src1, VR128:$src2, VR128:$src3)>;
  def : Pat<(v4i32 (add (X86vpmaddwd (v8i16 VR128:$src1), (v8i16 VR128:$src2)),
                        (v4i32 VR128:$src3))),
            (VPMADCSWDrr VR128:$src1, VR128:$src2, VR128:$src3)>;
}

// Transforms to swizzle an immediate to help matching memory operand in first
// operand.
def CommuteVPCOMCC : SDNodeXForm<imm, [{
  uint8_t Imm = N->getZExtValue() & 0x7;
  Imm = X86::getSwappedVPCOMImm(Imm);
  return getI8Imm(Imm, SDLoc(N));
}]>;

// Instruction where second source can be memory, third must be imm8
multiclass xopvpcom<bits<8> opc, string Suffix, SDNode OpNode, ValueType vt128,
                    X86FoldableSchedWrite sched> {
  let ExeDomain = SSEPackedInt in { // SSE integer instructions
    let isCommutable = 1 in
    def ri : IXOPi8<opc, MRMSrcReg, (outs VR128:$dst),
             (ins VR128:$src1, VR128:$src2, XOPCC:$cc),
             !strconcat("vpcom${cc}", Suffix,
             "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
             [(set VR128:$dst,
                (vt128 (OpNode (vt128 VR128:$src1), (vt128 VR128:$src2),
                               imm:$cc)))]>,
             XOP_4V, Sched<[sched]>;
    def mi : IXOPi8<opc, MRMSrcMem, (outs VR128:$dst),
             (ins VR128:$src1, i128mem:$src2, XOPCC:$cc),
             !strconcat("vpcom${cc}", Suffix,
             "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
             [(set VR128:$dst,
                (vt128 (OpNode (vt128 VR128:$src1),
                               (vt128 (load addr:$src2)),
                                imm:$cc)))]>,
             XOP_4V, Sched<[sched.Folded, sched.ReadAfterFold]>;
    let isAsmParserOnly = 1, hasSideEffects = 0 in {
      def ri_alt : IXOPi8<opc, MRMSrcReg, (outs VR128:$dst),
                   (ins VR128:$src1, VR128:$src2, u8imm:$src3),
                   !strconcat("vpcom", Suffix,
                   "\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}"),
                   []>, XOP_4V, Sched<[sched]>, NotMemoryFoldable;
      let mayLoad = 1 in
      def mi_alt : IXOPi8<opc, MRMSrcMem, (outs VR128:$dst),
                   (ins VR128:$src1, i128mem:$src2, u8imm:$src3),
                   !strconcat("vpcom", Suffix,
                   "\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}"),
                   []>, XOP_4V, Sched<[sched.Folded, sched.ReadAfterFold]>,
                   NotMemoryFoldable;
    }
  }

  def : Pat<(OpNode (load addr:$src2),
                    (vt128 VR128:$src1), imm:$cc),
            (!cast<Instruction>(NAME#"mi") VR128:$src1, addr:$src2,
                                           (CommuteVPCOMCC imm:$cc))>;
}

defm VPCOMB  : xopvpcom<0xCC, "b", X86vpcom, v16i8, SchedWriteVecALU.XMM>;
defm VPCOMW  : xopvpcom<0xCD, "w", X86vpcom, v8i16, SchedWriteVecALU.XMM>;
defm VPCOMD  : xopvpcom<0xCE, "d", X86vpcom, v4i32, SchedWriteVecALU.XMM>;
defm VPCOMQ  : xopvpcom<0xCF, "q", X86vpcom, v2i64, SchedWriteVecALU.XMM>;
defm VPCOMUB : xopvpcom<0xEC, "ub", X86vpcomu, v16i8, SchedWriteVecALU.XMM>;
defm VPCOMUW : xopvpcom<0xED, "uw", X86vpcomu, v8i16, SchedWriteVecALU.XMM>;
defm VPCOMUD : xopvpcom<0xEE, "ud", X86vpcomu, v4i32, SchedWriteVecALU.XMM>;
defm VPCOMUQ : xopvpcom<0xEF, "uq", X86vpcomu, v2i64, SchedWriteVecALU.XMM>;

multiclass xop4op<bits<8> opc, string OpcodeStr, SDNode OpNode,
                  ValueType vt128, X86FoldableSchedWrite sched> {
  def rrr : IXOPi8Reg<opc, MRMSrcReg, (outs VR128:$dst),
            (ins VR128:$src1, VR128:$src2, VR128:$src3),
            !strconcat(OpcodeStr,
            "\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}"),
            [(set VR128:$dst,
              (vt128 (OpNode (vt128 VR128:$src1), (vt128 VR128:$src2),
                             (vt128 VR128:$src3))))]>,
            XOP_4V, Sched<[sched]>;
  def rrm : IXOPi8Reg<opc, MRMSrcMemOp4, (outs VR128:$dst),
            (ins VR128:$src1, VR128:$src2, i128mem:$src3),
            !strconcat(OpcodeStr,
            "\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}"),
            [(set VR128:$dst,
              (vt128 (OpNode (vt128 VR128:$src1), (vt128 VR128:$src2),
                             (vt128 (load addr:$src3)))))]>,
            XOP_4V, VEX_W, Sched<[sched.Folded, sched.ReadAfterFold, sched.ReadAfterFold]>;
  def rmr : IXOPi8Reg<opc, MRMSrcMem, (outs VR128:$dst),
            (ins VR128:$src1, i128mem:$src2, VR128:$src3),
            !strconcat(OpcodeStr,
            "\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}"),
            [(set VR128:$dst,
              (v16i8 (OpNode (vt128 VR128:$src1), (vt128 (load addr:$src2)),
                             (vt128 VR128:$src3))))]>,
            XOP_4V, Sched<[sched.Folded, sched.ReadAfterFold,
                           // 128mem:$src2
                           ReadDefault, ReadDefault, ReadDefault, ReadDefault,
                           ReadDefault,
                           // VR128:$src3
                           sched.ReadAfterFold]>;
  // For disassembler
  let isCodeGenOnly = 1, ForceDisassemble = 1, hasSideEffects = 0 in
  def rrr_REV : IXOPi8Reg<opc, MRMSrcRegOp4, (outs VR128:$dst),
                (ins VR128:$src1, VR128:$src2, VR128:$src3),
                !strconcat(OpcodeStr,
                "\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}"),
                []>, XOP_4V, VEX_W, Sched<[sched]>, FoldGenData<NAME#rrr>;
}

let ExeDomain = SSEPackedInt in {
  defm VPPERM : xop4op<0xA3, "vpperm", X86vpperm, v16i8,
                       SchedWriteVarShuffle.XMM>;
}

// Instruction where either second or third source can be memory
multiclass xop4op_int<bits<8> opc, string OpcodeStr, RegisterClass RC,
                      X86MemOperand x86memop, ValueType VT,
                      X86FoldableSchedWrite sched> {
  def rrr : IXOPi8Reg<opc, MRMSrcReg, (outs RC:$dst),
            (ins RC:$src1, RC:$src2, RC:$src3),
            !strconcat(OpcodeStr,
            "\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}"),
            [(set RC:$dst, (VT (or (and RC:$src3, RC:$src1),
                                   (X86andnp RC:$src3, RC:$src2))))]>, XOP_4V,
            Sched<[sched]>;
  // FIXME: This pattern can't match.
  def rrm : IXOPi8Reg<opc, MRMSrcMemOp4, (outs RC:$dst),
            (ins RC:$src1, RC:$src2, x86memop:$src3),
            !strconcat(OpcodeStr,
            "\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}"),
            [(set RC:$dst, (VT (or (and (load addr:$src3), RC:$src1),
                                   (X86andnp (load addr:$src3), RC:$src2))))]>,
            XOP_4V, VEX_W, Sched<[sched.Folded, sched.ReadAfterFold, sched.ReadAfterFold]>;
  def rmr : IXOPi8Reg<opc, MRMSrcMem, (outs RC:$dst),
            (ins RC:$src1, x86memop:$src2, RC:$src3),
            !strconcat(OpcodeStr,
            "\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}"),
            [(set RC:$dst, (VT (or (and RC:$src3, RC:$src1),
                                   (X86andnp RC:$src3, (load addr:$src2)))))]>,
            XOP_4V, Sched<[sched.Folded, sched.ReadAfterFold,
                           // x86memop:$src2
                           ReadDefault, ReadDefault, ReadDefault, ReadDefault,
                           ReadDefault,
                           // RC::$src3
                           sched.ReadAfterFold]>;
  // For disassembler
  let isCodeGenOnly = 1, ForceDisassemble = 1, hasSideEffects = 0 in
  def rrr_REV : IXOPi8Reg<opc, MRMSrcRegOp4, (outs RC:$dst),
            (ins RC:$src1, RC:$src2, RC:$src3),
            !strconcat(OpcodeStr,
            "\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}"),
            []>, XOP_4V, VEX_W, Sched<[sched]>, FoldGenData<NAME#rrr>;
}

let ExeDomain = SSEPackedInt in {
  defm VPCMOV : xop4op_int<0xA2, "vpcmov", VR128, i128mem, v2i64,
                           SchedWriteShuffle.XMM>;
  defm VPCMOVY : xop4op_int<0xA2, "vpcmov", VR256, i256mem, v4i64,
                            SchedWriteShuffle.YMM>, VEX_L;
}

let Predicates = [HasXOP] in {
  def : Pat<(v16i8 (or (and VR128:$src3, VR128:$src1),
                   (X86andnp VR128:$src3, VR128:$src2))),
            (VPCMOVrrr VR128:$src1, VR128:$src2, VR128:$src3)>;
  def : Pat<(v8i16 (or (and VR128:$src3, VR128:$src1),
                   (X86andnp VR128:$src3, VR128:$src2))),
            (VPCMOVrrr VR128:$src1, VR128:$src2, VR128:$src3)>;
  def : Pat<(v4i32 (or (and VR128:$src3, VR128:$src1),
                   (X86andnp VR128:$src3, VR128:$src2))),
            (VPCMOVrrr VR128:$src1, VR128:$src2, VR128:$src3)>;

  def : Pat<(or (and VR128:$src3, VR128:$src1),
                (X86andnp VR128:$src3, (bc_v16i8 (loadv2i64 addr:$src2)))),
            (VPCMOVrmr VR128:$src1, addr:$src2, VR128:$src3)>;
  def : Pat<(or (and VR128:$src3, VR128:$src1),
                (X86andnp VR128:$src3, (bc_v8i16 (loadv2i64 addr:$src2)))),
            (VPCMOVrmr VR128:$src1, addr:$src2, VR128:$src3)>;
  def : Pat<(or (and VR128:$src3, VR128:$src1),
                (X86andnp VR128:$src3, (bc_v4i32 (loadv2i64 addr:$src2)))),
            (VPCMOVrmr VR128:$src1, addr:$src2, VR128:$src3)>;

  def : Pat<(v32i8 (or (and VR256:$src3, VR256:$src1),
                   (X86andnp VR256:$src3, VR256:$src2))),
            (VPCMOVYrrr VR256:$src1, VR256:$src2, VR256:$src3)>;
  def : Pat<(v16i16 (or (and VR256:$src3, VR256:$src1),
                    (X86andnp VR256:$src3, VR256:$src2))),
            (VPCMOVYrrr VR256:$src1, VR256:$src2, VR256:$src3)>;
  def : Pat<(v8i32 (or (and VR256:$src3, VR256:$src1),
                   (X86andnp VR256:$src3, VR256:$src2))),
            (VPCMOVYrrr VR256:$src1, VR256:$src2, VR256:$src3)>;

  def : Pat<(or (and VR256:$src3, VR256:$src1),
                (X86andnp VR256:$src3, (bc_v32i8 (loadv4i64 addr:$src2)))),
            (VPCMOVYrmr VR256:$src1, addr:$src2, VR256:$src3)>;
  def : Pat<(or (and VR256:$src3, VR256:$src1),
                (X86andnp VR256:$src3, (bc_v16i16 (loadv4i64 addr:$src2)))),
            (VPCMOVYrmr VR256:$src1, addr:$src2, VR256:$src3)>;
  def : Pat<(or (and VR256:$src3, VR256:$src1),
                (X86andnp VR256:$src3, (bc_v8i32 (loadv4i64 addr:$src2)))),
            (VPCMOVYrmr VR256:$src1, addr:$src2, VR256:$src3)>;
}

multiclass xop_vpermil2<bits<8> Opc, string OpcodeStr, RegisterClass RC,
                        X86MemOperand intmemop, X86MemOperand fpmemop,
                        ValueType VT, PatFrag FPLdFrag, PatFrag IntLdFrag,
                        X86FoldableSchedWrite sched> {
  def rr : IXOP5<Opc, MRMSrcReg, (outs RC:$dst),
        (ins RC:$src1, RC:$src2, RC:$src3, u8imm:$src4),
        !strconcat(OpcodeStr,
        "\t{$src4, $src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3, $src4}"),
        [(set RC:$dst,
           (VT (X86vpermil2 RC:$src1, RC:$src2, RC:$src3, (i8 imm:$src4))))]>,
        Sched<[sched]>;
  def rm : IXOP5<Opc, MRMSrcMemOp4, (outs RC:$dst),
        (ins RC:$src1, RC:$src2, intmemop:$src3, u8imm:$src4),
        !strconcat(OpcodeStr,
        "\t{$src4, $src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3, $src4}"),
        [(set RC:$dst,
          (VT (X86vpermil2 RC:$src1, RC:$src2, (IntLdFrag addr:$src3),
                           (i8 imm:$src4))))]>, VEX_W,
        Sched<[sched.Folded, sched.ReadAfterFold, sched.ReadAfterFold]>;
  def mr : IXOP5<Opc, MRMSrcMem, (outs RC:$dst),
        (ins RC:$src1, fpmemop:$src2, RC:$src3, u8imm:$src4),
        !strconcat(OpcodeStr,
        "\t{$src4, $src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3, $src4}"),
        [(set RC:$dst,
          (VT (X86vpermil2 RC:$src1, (FPLdFrag addr:$src2),
                           RC:$src3, (i8 imm:$src4))))]>,
        Sched<[sched.Folded, sched.ReadAfterFold,
               // fpmemop:$src2
               ReadDefault, ReadDefault, ReadDefault, ReadDefault, ReadDefault,
               // RC:$src3
               sched.ReadAfterFold]>;
  // For disassembler
  let isCodeGenOnly = 1, ForceDisassemble = 1, hasSideEffects = 0 in
  def rr_REV : IXOP5<Opc, MRMSrcRegOp4, (outs RC:$dst),
        (ins RC:$src1, RC:$src2, RC:$src3, u8imm:$src4),
        !strconcat(OpcodeStr,
        "\t{$src4, $src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3, $src4}"),
        []>, VEX_W, Sched<[sched]>, FoldGenData<NAME#rr>;
}

let ExeDomain = SSEPackedDouble in {
  defm VPERMIL2PD : xop_vpermil2<0x49, "vpermil2pd", VR128, i128mem, f128mem,
                                 v2f64, loadv2f64, loadv2i64,
                                 SchedWriteFVarShuffle.XMM>;
  defm VPERMIL2PDY : xop_vpermil2<0x49, "vpermil2pd", VR256, i256mem, f256mem,
                                  v4f64, loadv4f64, loadv4i64,
                                  SchedWriteFVarShuffle.YMM>, VEX_L;
}

let ExeDomain = SSEPackedSingle in {
  defm VPERMIL2PS : xop_vpermil2<0x48, "vpermil2ps", VR128, i128mem, f128mem,
                                 v4f32, loadv4f32, loadv4i32,
                                 SchedWriteFVarShuffle.XMM>;
  defm VPERMIL2PSY : xop_vpermil2<0x48, "vpermil2ps", VR256, i256mem, f256mem,
                                  v8f32, loadv8f32, loadv8i32,
                                  SchedWriteFVarShuffle.YMM>, VEX_L;
}

