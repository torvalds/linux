//===-- X86InstrSSE.td - SSE Instruction Set ---------------*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file describes the X86 SSE instruction set, defining the instructions,
// and properties of the instructions which are needed for code generation,
// machine code emission, and analysis.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// SSE 1 & 2 Instructions Classes
//===----------------------------------------------------------------------===//

/// sse12_fp_scalar - SSE 1 & 2 scalar instructions class
multiclass sse12_fp_scalar<bits<8> opc, string OpcodeStr, SDNode OpNode,
                           RegisterClass RC, X86MemOperand x86memop,
                           Domain d, X86FoldableSchedWrite sched,
                           bit Is2Addr = 1> {
  let isCommutable = 1 in {
    def rr : SI<opc, MRMSrcReg, (outs RC:$dst), (ins RC:$src1, RC:$src2),
       !if(Is2Addr,
           !strconcat(OpcodeStr, "\t{$src2, $dst|$dst, $src2}"),
           !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}")),
       [(set RC:$dst, (OpNode RC:$src1, RC:$src2))], d>,
       Sched<[sched]>;
  }
  def rm : SI<opc, MRMSrcMem, (outs RC:$dst), (ins RC:$src1, x86memop:$src2),
       !if(Is2Addr,
           !strconcat(OpcodeStr, "\t{$src2, $dst|$dst, $src2}"),
           !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}")),
       [(set RC:$dst, (OpNode RC:$src1, (load addr:$src2)))], d>,
       Sched<[sched.Folded, sched.ReadAfterFold]>;
}

/// sse12_fp_scalar_int - SSE 1 & 2 scalar instructions intrinsics class
multiclass sse12_fp_scalar_int<bits<8> opc, string OpcodeStr,
                               SDPatternOperator OpNode, RegisterClass RC,
                               ValueType VT, string asm, Operand memopr,
                               ComplexPattern mem_cpat, Domain d,
                               X86FoldableSchedWrite sched, bit Is2Addr = 1> {
let isCodeGenOnly = 1, hasSideEffects = 0 in {
  def rr_Int : SI_Int<opc, MRMSrcReg, (outs RC:$dst), (ins RC:$src1, RC:$src2),
       !if(Is2Addr,
           !strconcat(asm, "\t{$src2, $dst|$dst, $src2}"),
           !strconcat(asm, "\t{$src2, $src1, $dst|$dst, $src1, $src2}")),
       [(set RC:$dst, (VT (OpNode RC:$src1, RC:$src2)))], d>,
       Sched<[sched]>;
  let mayLoad = 1 in
  def rm_Int : SI_Int<opc, MRMSrcMem, (outs RC:$dst), (ins RC:$src1, memopr:$src2),
       !if(Is2Addr,
           !strconcat(asm, "\t{$src2, $dst|$dst, $src2}"),
           !strconcat(asm, "\t{$src2, $src1, $dst|$dst, $src1, $src2}")),
       [(set RC:$dst, (VT (OpNode RC:$src1, mem_cpat:$src2)))], d>,
       Sched<[sched.Folded, sched.ReadAfterFold]>;
}
}

/// sse12_fp_packed - SSE 1 & 2 packed instructions class
multiclass sse12_fp_packed<bits<8> opc, string OpcodeStr, SDNode OpNode,
                           RegisterClass RC, ValueType vt,
                           X86MemOperand x86memop, PatFrag mem_frag,
                           Domain d, X86FoldableSchedWrite sched,
                           bit Is2Addr = 1> {
  let isCommutable = 1 in
    def rr : PI<opc, MRMSrcReg, (outs RC:$dst), (ins RC:$src1, RC:$src2),
       !if(Is2Addr,
           !strconcat(OpcodeStr, "\t{$src2, $dst|$dst, $src2}"),
           !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}")),
       [(set RC:$dst, (vt (OpNode RC:$src1, RC:$src2)))], d>,
       Sched<[sched]>;
  let mayLoad = 1 in
    def rm : PI<opc, MRMSrcMem, (outs RC:$dst), (ins RC:$src1, x86memop:$src2),
       !if(Is2Addr,
           !strconcat(OpcodeStr, "\t{$src2, $dst|$dst, $src2}"),
           !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}")),
       [(set RC:$dst, (OpNode RC:$src1, (mem_frag addr:$src2)))],
          d>,
       Sched<[sched.Folded, sched.ReadAfterFold]>;
}

/// sse12_fp_packed_logical_rm - SSE 1 & 2 packed instructions class
multiclass sse12_fp_packed_logical_rm<bits<8> opc, RegisterClass RC, Domain d,
                                      string OpcodeStr, X86MemOperand x86memop,
                                      X86FoldableSchedWrite sched,
                                      list<dag> pat_rr, list<dag> pat_rm,
                                      bit Is2Addr = 1> {
  let isCommutable = 1, hasSideEffects = 0 in
    def rr : PI<opc, MRMSrcReg, (outs RC:$dst), (ins RC:$src1, RC:$src2),
       !if(Is2Addr,
           !strconcat(OpcodeStr, "\t{$src2, $dst|$dst, $src2}"),
           !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}")),
       pat_rr, d>,
       Sched<[sched]>;
  let hasSideEffects = 0, mayLoad = 1 in
  def rm : PI<opc, MRMSrcMem, (outs RC:$dst), (ins RC:$src1, x86memop:$src2),
       !if(Is2Addr,
           !strconcat(OpcodeStr, "\t{$src2, $dst|$dst, $src2}"),
           !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}")),
       pat_rm, d>,
       Sched<[sched.Folded, sched.ReadAfterFold]>;
}


// Alias instructions that map fld0 to xorps for sse or vxorps for avx.
// This is expanded by ExpandPostRAPseudos.
let isReMaterializable = 1, isAsCheapAsAMove = 1, canFoldAsLoad = 1,
    isPseudo = 1, SchedRW = [WriteZero] in {
  def FsFLD0SS : I<0, Pseudo, (outs FR32:$dst), (ins), "",
                   [(set FR32:$dst, fp32imm0)]>, Requires<[HasSSE1, NoAVX512]>;
  def FsFLD0SD : I<0, Pseudo, (outs FR64:$dst), (ins), "",
                   [(set FR64:$dst, fpimm0)]>, Requires<[HasSSE2, NoAVX512]>;
}

//===----------------------------------------------------------------------===//
// AVX & SSE - Zero/One Vectors
//===----------------------------------------------------------------------===//

// Alias instruction that maps zero vector to pxor / xorp* for sse.
// This is expanded by ExpandPostRAPseudos to an xorps / vxorps, and then
// swizzled by ExecutionDomainFix to pxor.
// We set canFoldAsLoad because this can be converted to a constant-pool
// load of an all-zeros value if folding it would be beneficial.
let isReMaterializable = 1, isAsCheapAsAMove = 1, canFoldAsLoad = 1,
    isPseudo = 1, SchedRW = [WriteZero] in {
def V_SET0 : I<0, Pseudo, (outs VR128:$dst), (ins), "",
               [(set VR128:$dst, (v4f32 immAllZerosV))]>;
}

let Predicates = [NoAVX512] in
def : Pat<(v4i32 immAllZerosV), (V_SET0)>;


// The same as done above but for AVX.  The 256-bit AVX1 ISA doesn't support PI,
// and doesn't need it because on sandy bridge the register is set to zero
// at the rename stage without using any execution unit, so SET0PSY
// and SET0PDY can be used for vector int instructions without penalty
let isReMaterializable = 1, isAsCheapAsAMove = 1, canFoldAsLoad = 1,
    isPseudo = 1, Predicates = [NoAVX512], SchedRW = [WriteZero] in {
def AVX_SET0 : I<0, Pseudo, (outs VR256:$dst), (ins), "",
                 [(set VR256:$dst, (v8i32 immAllZerosV))]>;
}

// We set canFoldAsLoad because this can be converted to a constant-pool
// load of an all-ones value if folding it would be beneficial.
let isReMaterializable = 1, isAsCheapAsAMove = 1, canFoldAsLoad = 1,
    isPseudo = 1, SchedRW = [WriteZero] in {
  def V_SETALLONES : I<0, Pseudo, (outs VR128:$dst), (ins), "",
                       [(set VR128:$dst, (v4i32 immAllOnesV))]>;
  let Predicates = [HasAVX1Only, OptForMinSize] in {
  def AVX1_SETALLONES: I<0, Pseudo, (outs VR256:$dst), (ins), "",
                          [(set VR256:$dst, (v8i32 immAllOnesV))]>;
  }
  let Predicates = [HasAVX2] in
  def AVX2_SETALLONES : I<0, Pseudo, (outs VR256:$dst), (ins), "",
                          [(set VR256:$dst, (v8i32 immAllOnesV))]>;
}

//===----------------------------------------------------------------------===//
// SSE 1 & 2 - Move FP Scalar Instructions
//
// Move Instructions. Register-to-register movss/movsd is not used for FR32/64
// register copies because it's a partial register update; Register-to-register
// movss/movsd is not modeled as an INSERT_SUBREG because INSERT_SUBREG requires
// that the insert be implementable in terms of a copy, and just mentioned, we
// don't use movss/movsd for copies.
//===----------------------------------------------------------------------===//

multiclass sse12_move_rr<SDNode OpNode, ValueType vt,
                         X86MemOperand x86memop, string base_opc,
                         string asm_opr, Domain d, string Name> {
  let isCommutable = 1 in
  def rr : SI<0x10, MRMSrcReg, (outs VR128:$dst),
              (ins VR128:$src1, VR128:$src2),
              !strconcat(base_opc, asm_opr),
              [(set VR128:$dst, (vt (OpNode VR128:$src1, VR128:$src2)))], d>,
              Sched<[SchedWriteFShuffle.XMM]>;

  // For the disassembler
  let isCodeGenOnly = 1, ForceDisassemble = 1, hasSideEffects = 0 in
  def rr_REV : SI<0x11, MRMDestReg, (outs VR128:$dst),
                  (ins VR128:$src1, VR128:$src2),
                  !strconcat(base_opc, asm_opr), []>,
                  Sched<[SchedWriteFShuffle.XMM]>, FoldGenData<Name#rr>;
}

multiclass sse12_move<RegisterClass RC, SDNode OpNode, ValueType vt,
                      X86MemOperand x86memop, string OpcodeStr,
                      Domain d, string Name, Predicate pred> {
  // AVX
  let Predicates = [UseAVX, OptForSize] in
  defm V#NAME : sse12_move_rr<OpNode, vt, x86memop, OpcodeStr,
                              "\t{$src2, $src1, $dst|$dst, $src1, $src2}", d,
                              "V"#Name>,
                              VEX_4V, VEX_LIG, VEX_WIG;

  def V#NAME#mr : SI<0x11, MRMDestMem, (outs), (ins x86memop:$dst, RC:$src),
                     !strconcat(OpcodeStr, "\t{$src, $dst|$dst, $src}"),
                     [(store RC:$src, addr:$dst)], d>,
                     VEX, VEX_LIG, Sched<[WriteFStore]>, VEX_WIG;
  // SSE1 & 2
  let Constraints = "$src1 = $dst" in {
    let Predicates = [pred, NoSSE41_Or_OptForSize] in
    defm NAME : sse12_move_rr<OpNode, vt, x86memop, OpcodeStr,
                              "\t{$src2, $dst|$dst, $src2}", d, Name>;
  }

  def NAME#mr   : SI<0x11, MRMDestMem, (outs), (ins x86memop:$dst, RC:$src),
                     !strconcat(OpcodeStr, "\t{$src, $dst|$dst, $src}"),
                     [(store RC:$src, addr:$dst)], d>,
                     Sched<[WriteFStore]>;

  def : InstAlias<"v"#OpcodeStr#".s\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                  (!cast<Instruction>("V"#NAME#"rr_REV")
                   VR128:$dst, VR128:$src1, VR128:$src2), 0>;
  def : InstAlias<OpcodeStr#".s\t{$src2, $dst|$dst, $src2}",
                  (!cast<Instruction>(NAME#"rr_REV")
                   VR128:$dst, VR128:$src2), 0>;
}

// Loading from memory automatically zeroing upper bits.
multiclass sse12_move_rm<RegisterClass RC, X86MemOperand x86memop,
                         PatFrag mem_pat, string OpcodeStr, Domain d> {
  def V#NAME#rm : SI<0x10, MRMSrcMem, (outs RC:$dst), (ins x86memop:$src),
                     !strconcat(OpcodeStr, "\t{$src, $dst|$dst, $src}"),
                     [(set RC:$dst, (mem_pat addr:$src))], d>,
                     VEX, VEX_LIG, Sched<[WriteFLoad]>, VEX_WIG;
  def NAME#rm   : SI<0x10, MRMSrcMem, (outs RC:$dst), (ins x86memop:$src),
                     !strconcat(OpcodeStr, "\t{$src, $dst|$dst, $src}"),
                     [(set RC:$dst, (mem_pat addr:$src))], d>,
                     Sched<[WriteFLoad]>;
}

defm MOVSS : sse12_move<FR32, X86Movss, v4f32, f32mem, "movss",
                        SSEPackedSingle, "MOVSS", UseSSE1>, XS;
defm MOVSD : sse12_move<FR64, X86Movsd, v2f64, f64mem, "movsd",
                        SSEPackedDouble, "MOVSD", UseSSE2>, XD;

let canFoldAsLoad = 1, isReMaterializable = 1 in {
  defm MOVSS : sse12_move_rm<FR32, f32mem, loadf32, "movss",
                             SSEPackedSingle>, XS;
  defm MOVSD : sse12_move_rm<FR64, f64mem, loadf64, "movsd",
                             SSEPackedDouble>, XD;
}

// Patterns
let Predicates = [UseAVX] in {
  // MOVSSrm zeros the high parts of the register; represent this
  // with SUBREG_TO_REG. The AVX versions also write: DST[255:128] <- 0
  def : Pat<(v4f32 (X86vzmovl (v4f32 (scalar_to_vector (loadf32 addr:$src))))),
            (COPY_TO_REGCLASS (VMOVSSrm addr:$src), VR128)>;
  def : Pat<(v4f32 (X86vzmovl (loadv4f32 addr:$src))),
            (COPY_TO_REGCLASS (VMOVSSrm addr:$src), VR128)>;
  def : Pat<(v4f32 (X86vzload addr:$src)),
            (COPY_TO_REGCLASS (VMOVSSrm addr:$src), VR128)>;

  // MOVSDrm zeros the high parts of the register; represent this
  // with SUBREG_TO_REG. The AVX versions also write: DST[255:128] <- 0
  def : Pat<(v2f64 (X86vzmovl (v2f64 (scalar_to_vector (loadf64 addr:$src))))),
            (COPY_TO_REGCLASS (VMOVSDrm addr:$src), VR128)>;
  def : Pat<(v2f64 (X86vzmovl (loadv2f64 addr:$src))),
            (COPY_TO_REGCLASS (VMOVSDrm addr:$src), VR128)>;
  def : Pat<(v2f64 (X86vzload addr:$src)),
            (COPY_TO_REGCLASS (VMOVSDrm addr:$src), VR128)>;

  // Represent the same patterns above but in the form they appear for
  // 256-bit types
  def : Pat<(v8f32 (X86vzmovl (insert_subvector undef,
                   (v4f32 (scalar_to_vector (loadf32 addr:$src))), (iPTR 0)))),
            (SUBREG_TO_REG (i32 0), (VMOVSSrm addr:$src), sub_xmm)>;
  def : Pat<(v8f32 (X86vzload addr:$src)),
            (SUBREG_TO_REG (i32 0), (VMOVSSrm addr:$src), sub_xmm)>;
  def : Pat<(v4f64 (X86vzmovl (insert_subvector undef,
                   (v2f64 (scalar_to_vector (loadf64 addr:$src))), (iPTR 0)))),
            (SUBREG_TO_REG (i32 0), (VMOVSDrm addr:$src), sub_xmm)>;
  def : Pat<(v4f64 (X86vzload addr:$src)),
            (SUBREG_TO_REG (i32 0), (VMOVSDrm addr:$src), sub_xmm)>;

  // Extract and store.
  def : Pat<(store (f32 (extractelt (v4f32 VR128:$src), (iPTR 0))),
                   addr:$dst),
            (VMOVSSmr addr:$dst, (COPY_TO_REGCLASS (v4f32 VR128:$src), FR32))>;
}

let Predicates = [UseAVX, OptForSize] in {
  // Move scalar to XMM zero-extended, zeroing a VR128 then do a
  // MOVSS to the lower bits.
  def : Pat<(v4f32 (X86vzmovl (v4f32 VR128:$src))),
            (VMOVSSrr (v4f32 (V_SET0)), VR128:$src)>;
  def : Pat<(v4i32 (X86vzmovl (v4i32 VR128:$src))),
            (VMOVSSrr (v4i32 (V_SET0)), VR128:$src)>;

  // Move low f32 and clear high bits.
  def : Pat<(v8f32 (X86vzmovl (v8f32 VR256:$src))),
            (SUBREG_TO_REG (i32 0),
             (v4f32 (VMOVSSrr (v4f32 (V_SET0)),
              (v4f32 (EXTRACT_SUBREG (v8f32 VR256:$src), sub_xmm)))), sub_xmm)>;
  def : Pat<(v8i32 (X86vzmovl (v8i32 VR256:$src))),
            (SUBREG_TO_REG (i32 0),
             (v4i32 (VMOVSSrr (v4i32 (V_SET0)),
              (v4i32 (EXTRACT_SUBREG (v8i32 VR256:$src), sub_xmm)))), sub_xmm)>;

  def : Pat<(v4f64 (X86vzmovl (v4f64 VR256:$src))),
            (SUBREG_TO_REG (i32 0),
             (v2f64 (VMOVSDrr (v2f64 (V_SET0)),
                       (v2f64 (EXTRACT_SUBREG (v4f64 VR256:$src), sub_xmm)))),
             sub_xmm)>;
  def : Pat<(v4i64 (X86vzmovl (v4i64 VR256:$src))),
            (SUBREG_TO_REG (i32 0),
             (v2i64 (VMOVSDrr (v2i64 (V_SET0)),
                       (v2i64 (EXTRACT_SUBREG (v4i64 VR256:$src), sub_xmm)))),
             sub_xmm)>;
}

let Predicates = [UseSSE1] in {
  let Predicates = [UseSSE1, NoSSE41_Or_OptForSize] in {
  // Move scalar to XMM zero-extended, zeroing a VR128 then do a
  // MOVSS to the lower bits.
  def : Pat<(v4f32 (X86vzmovl (v4f32 VR128:$src))),
            (MOVSSrr (v4f32 (V_SET0)), VR128:$src)>;
  def : Pat<(v4i32 (X86vzmovl (v4i32 VR128:$src))),
            (MOVSSrr (v4i32 (V_SET0)), VR128:$src)>;
  }

  // MOVSSrm already zeros the high parts of the register.
  def : Pat<(v4f32 (X86vzmovl (v4f32 (scalar_to_vector (loadf32 addr:$src))))),
            (COPY_TO_REGCLASS (MOVSSrm addr:$src), VR128)>;
  def : Pat<(v4f32 (X86vzmovl (loadv4f32 addr:$src))),
            (COPY_TO_REGCLASS (MOVSSrm addr:$src), VR128)>;
  def : Pat<(v4f32 (X86vzload addr:$src)),
            (COPY_TO_REGCLASS (MOVSSrm addr:$src), VR128)>;

  // Extract and store.
  def : Pat<(store (f32 (extractelt (v4f32 VR128:$src), (iPTR 0))),
                   addr:$dst),
            (MOVSSmr addr:$dst, (COPY_TO_REGCLASS VR128:$src, FR32))>;
}

let Predicates = [UseSSE2] in {
  // MOVSDrm already zeros the high parts of the register.
  def : Pat<(v2f64 (X86vzmovl (v2f64 (scalar_to_vector (loadf64 addr:$src))))),
            (COPY_TO_REGCLASS (MOVSDrm addr:$src), VR128)>;
  def : Pat<(v2f64 (X86vzmovl (loadv2f64 addr:$src))),
            (COPY_TO_REGCLASS (MOVSDrm addr:$src), VR128)>;
  def : Pat<(v2f64 (X86vzload addr:$src)),
            (COPY_TO_REGCLASS (MOVSDrm addr:$src), VR128)>;
}

// Aliases to help the assembler pick two byte VEX encodings by swapping the
// operands relative to the normal instructions to use VEX.R instead of VEX.B.
def : InstAlias<"vmovss\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                (VMOVSSrr_REV VR128L:$dst, VR128:$src1, VR128H:$src2), 0>;
def : InstAlias<"vmovsd\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                (VMOVSDrr_REV VR128L:$dst, VR128:$src1, VR128H:$src2), 0>;

//===----------------------------------------------------------------------===//
// SSE 1 & 2 - Move Aligned/Unaligned FP Instructions
//===----------------------------------------------------------------------===//

multiclass sse12_mov_packed<bits<8> opc, RegisterClass RC,
                            X86MemOperand x86memop, PatFrag ld_frag,
                            string asm, Domain d,
                            X86SchedWriteMoveLS sched> {
let hasSideEffects = 0, isMoveReg = 1 in
  def rr : PI<opc, MRMSrcReg, (outs RC:$dst), (ins RC:$src),
              !strconcat(asm, "\t{$src, $dst|$dst, $src}"), [], d>,
           Sched<[sched.RR]>;
let canFoldAsLoad = 1, isReMaterializable = 1 in
  def rm : PI<opc, MRMSrcMem, (outs RC:$dst), (ins x86memop:$src),
              !strconcat(asm, "\t{$src, $dst|$dst, $src}"),
                   [(set RC:$dst, (ld_frag addr:$src))], d>,
           Sched<[sched.RM]>;
}

let Predicates = [HasAVX, NoVLX] in {
defm VMOVAPS : sse12_mov_packed<0x28, VR128, f128mem, alignedloadv4f32, "movaps",
                                SSEPackedSingle, SchedWriteFMoveLS.XMM>,
                                PS, VEX, VEX_WIG;
defm VMOVAPD : sse12_mov_packed<0x28, VR128, f128mem, alignedloadv2f64, "movapd",
                                SSEPackedDouble, SchedWriteFMoveLS.XMM>,
                                PD, VEX, VEX_WIG;
defm VMOVUPS : sse12_mov_packed<0x10, VR128, f128mem, loadv4f32, "movups",
                                SSEPackedSingle, SchedWriteFMoveLS.XMM>,
                                PS, VEX, VEX_WIG;
defm VMOVUPD : sse12_mov_packed<0x10, VR128, f128mem, loadv2f64, "movupd",
                                SSEPackedDouble, SchedWriteFMoveLS.XMM>,
                                PD, VEX, VEX_WIG;

defm VMOVAPSY : sse12_mov_packed<0x28, VR256, f256mem, alignedloadv8f32, "movaps",
                                 SSEPackedSingle, SchedWriteFMoveLS.YMM>,
                                 PS, VEX, VEX_L, VEX_WIG;
defm VMOVAPDY : sse12_mov_packed<0x28, VR256, f256mem, alignedloadv4f64, "movapd",
                                 SSEPackedDouble, SchedWriteFMoveLS.YMM>,
                                 PD, VEX, VEX_L, VEX_WIG;
defm VMOVUPSY : sse12_mov_packed<0x10, VR256, f256mem, loadv8f32, "movups",
                                 SSEPackedSingle, SchedWriteFMoveLS.YMM>,
                                 PS, VEX, VEX_L, VEX_WIG;
defm VMOVUPDY : sse12_mov_packed<0x10, VR256, f256mem, loadv4f64, "movupd", 
                                 SSEPackedDouble, SchedWriteFMoveLS.YMM>,
                                 PD, VEX, VEX_L, VEX_WIG;
}

let Predicates = [UseSSE1] in {
defm MOVAPS : sse12_mov_packed<0x28, VR128, f128mem, alignedloadv4f32, "movaps",
                               SSEPackedSingle, SchedWriteFMoveLS.XMM>,
                               PS;
defm MOVUPS : sse12_mov_packed<0x10, VR128, f128mem, loadv4f32, "movups",
                               SSEPackedSingle, SchedWriteFMoveLS.XMM>,
                               PS;
}
let Predicates = [UseSSE2] in {
defm MOVAPD : sse12_mov_packed<0x28, VR128, f128mem, alignedloadv2f64, "movapd",
                               SSEPackedDouble, SchedWriteFMoveLS.XMM>,
                               PD;
defm MOVUPD : sse12_mov_packed<0x10, VR128, f128mem, loadv2f64, "movupd",
                               SSEPackedDouble, SchedWriteFMoveLS.XMM>,
                               PD;
}

let Predicates = [HasAVX, NoVLX]  in {
let SchedRW = [SchedWriteFMoveLS.XMM.MR] in {
def VMOVAPSmr : VPSI<0x29, MRMDestMem, (outs), (ins f128mem:$dst, VR128:$src),
                   "movaps\t{$src, $dst|$dst, $src}",
                   [(alignedstore (v4f32 VR128:$src), addr:$dst)]>,
                   VEX, VEX_WIG;
def VMOVAPDmr : VPDI<0x29, MRMDestMem, (outs), (ins f128mem:$dst, VR128:$src),
                   "movapd\t{$src, $dst|$dst, $src}",
                   [(alignedstore (v2f64 VR128:$src), addr:$dst)]>,
                   VEX, VEX_WIG;
def VMOVUPSmr : VPSI<0x11, MRMDestMem, (outs), (ins f128mem:$dst, VR128:$src),
                   "movups\t{$src, $dst|$dst, $src}",
                   [(store (v4f32 VR128:$src), addr:$dst)]>,
                   VEX, VEX_WIG;
def VMOVUPDmr : VPDI<0x11, MRMDestMem, (outs), (ins f128mem:$dst, VR128:$src),
                   "movupd\t{$src, $dst|$dst, $src}",
                   [(store (v2f64 VR128:$src), addr:$dst)]>,
                   VEX, VEX_WIG;
} // SchedRW

let SchedRW = [SchedWriteFMoveLS.YMM.MR] in {
def VMOVAPSYmr : VPSI<0x29, MRMDestMem, (outs), (ins f256mem:$dst, VR256:$src),
                   "movaps\t{$src, $dst|$dst, $src}",
                   [(alignedstore (v8f32 VR256:$src), addr:$dst)]>,
                   VEX, VEX_L, VEX_WIG;
def VMOVAPDYmr : VPDI<0x29, MRMDestMem, (outs), (ins f256mem:$dst, VR256:$src),
                   "movapd\t{$src, $dst|$dst, $src}",
                   [(alignedstore (v4f64 VR256:$src), addr:$dst)]>,
                   VEX, VEX_L, VEX_WIG;
def VMOVUPSYmr : VPSI<0x11, MRMDestMem, (outs), (ins f256mem:$dst, VR256:$src),
                   "movups\t{$src, $dst|$dst, $src}",
                   [(store (v8f32 VR256:$src), addr:$dst)]>,
                   VEX, VEX_L, VEX_WIG;
def VMOVUPDYmr : VPDI<0x11, MRMDestMem, (outs), (ins f256mem:$dst, VR256:$src),
                   "movupd\t{$src, $dst|$dst, $src}",
                   [(store (v4f64 VR256:$src), addr:$dst)]>,
                   VEX, VEX_L, VEX_WIG;
} // SchedRW
} // Predicate

// For disassembler
let isCodeGenOnly = 1, ForceDisassemble = 1, hasSideEffects = 0,
    isMoveReg = 1 in {
let SchedRW = [SchedWriteFMoveLS.XMM.RR] in {
  def VMOVAPSrr_REV : VPSI<0x29, MRMDestReg, (outs VR128:$dst),
                          (ins VR128:$src),
                          "movaps\t{$src, $dst|$dst, $src}", []>,
                          VEX, VEX_WIG, FoldGenData<"VMOVAPSrr">;
  def VMOVAPDrr_REV : VPDI<0x29, MRMDestReg, (outs VR128:$dst),
                           (ins VR128:$src),
                           "movapd\t{$src, $dst|$dst, $src}", []>,
                           VEX, VEX_WIG, FoldGenData<"VMOVAPDrr">;
  def VMOVUPSrr_REV : VPSI<0x11, MRMDestReg, (outs VR128:$dst),
                           (ins VR128:$src),
                           "movups\t{$src, $dst|$dst, $src}", []>,
                           VEX, VEX_WIG, FoldGenData<"VMOVUPSrr">;
  def VMOVUPDrr_REV : VPDI<0x11, MRMDestReg, (outs VR128:$dst),
                           (ins VR128:$src),
                           "movupd\t{$src, $dst|$dst, $src}", []>,
                           VEX, VEX_WIG, FoldGenData<"VMOVUPDrr">;
} // SchedRW

let SchedRW = [SchedWriteFMoveLS.YMM.RR] in {
  def VMOVAPSYrr_REV : VPSI<0x29, MRMDestReg, (outs VR256:$dst),
                            (ins VR256:$src),
                            "movaps\t{$src, $dst|$dst, $src}", []>,
                            VEX, VEX_L, VEX_WIG, FoldGenData<"VMOVAPSYrr">;
  def VMOVAPDYrr_REV : VPDI<0x29, MRMDestReg, (outs VR256:$dst),
                            (ins VR256:$src),
                            "movapd\t{$src, $dst|$dst, $src}", []>,
                            VEX, VEX_L, VEX_WIG, FoldGenData<"VMOVAPDYrr">;
  def VMOVUPSYrr_REV : VPSI<0x11, MRMDestReg, (outs VR256:$dst),
                            (ins VR256:$src),
                            "movups\t{$src, $dst|$dst, $src}", []>,
                            VEX, VEX_L, VEX_WIG, FoldGenData<"VMOVUPSYrr">;
  def VMOVUPDYrr_REV : VPDI<0x11, MRMDestReg, (outs VR256:$dst),
                            (ins VR256:$src),
                            "movupd\t{$src, $dst|$dst, $src}", []>,
                            VEX, VEX_L, VEX_WIG, FoldGenData<"VMOVUPDYrr">;
} // SchedRW
} // Predicate

// Aliases to help the assembler pick two byte VEX encodings by swapping the
// operands relative to the normal instructions to use VEX.R instead of VEX.B.
def : InstAlias<"vmovaps\t{$src, $dst|$dst, $src}",
                (VMOVAPSrr_REV VR128L:$dst, VR128H:$src), 0>;
def : InstAlias<"vmovapd\t{$src, $dst|$dst, $src}",
                (VMOVAPDrr_REV VR128L:$dst, VR128H:$src), 0>;
def : InstAlias<"vmovups\t{$src, $dst|$dst, $src}",
                (VMOVUPSrr_REV VR128L:$dst, VR128H:$src), 0>;
def : InstAlias<"vmovupd\t{$src, $dst|$dst, $src}",
                (VMOVUPDrr_REV VR128L:$dst, VR128H:$src), 0>;
def : InstAlias<"vmovaps\t{$src, $dst|$dst, $src}",
                (VMOVAPSYrr_REV VR256L:$dst, VR256H:$src), 0>;
def : InstAlias<"vmovapd\t{$src, $dst|$dst, $src}",
                (VMOVAPDYrr_REV VR256L:$dst, VR256H:$src), 0>;
def : InstAlias<"vmovups\t{$src, $dst|$dst, $src}",
                (VMOVUPSYrr_REV VR256L:$dst, VR256H:$src), 0>;
def : InstAlias<"vmovupd\t{$src, $dst|$dst, $src}",
                (VMOVUPDYrr_REV VR256L:$dst, VR256H:$src), 0>;

// Reversed version with ".s" suffix for GAS compatibility.
def : InstAlias<"vmovaps.s\t{$src, $dst|$dst, $src}",
                (VMOVAPSrr_REV VR128:$dst, VR128:$src), 0>;
def : InstAlias<"vmovapd.s\t{$src, $dst|$dst, $src}",
                (VMOVAPDrr_REV VR128:$dst, VR128:$src), 0>;
def : InstAlias<"vmovups.s\t{$src, $dst|$dst, $src}",
                (VMOVUPSrr_REV VR128:$dst, VR128:$src), 0>;
def : InstAlias<"vmovupd.s\t{$src, $dst|$dst, $src}",
                (VMOVUPDrr_REV VR128:$dst, VR128:$src), 0>;
def : InstAlias<"vmovaps.s\t{$src, $dst|$dst, $src}",
                (VMOVAPSYrr_REV VR256:$dst, VR256:$src), 0>;
def : InstAlias<"vmovapd.s\t{$src, $dst|$dst, $src}",
                (VMOVAPDYrr_REV VR256:$dst, VR256:$src), 0>;
def : InstAlias<"vmovups.s\t{$src, $dst|$dst, $src}",
                (VMOVUPSYrr_REV VR256:$dst, VR256:$src), 0>;
def : InstAlias<"vmovupd.s\t{$src, $dst|$dst, $src}",
                (VMOVUPDYrr_REV VR256:$dst, VR256:$src), 0>;

let SchedRW = [SchedWriteFMoveLS.XMM.MR] in {
def MOVAPSmr : PSI<0x29, MRMDestMem, (outs), (ins f128mem:$dst, VR128:$src),
                   "movaps\t{$src, $dst|$dst, $src}",
                   [(alignedstore (v4f32 VR128:$src), addr:$dst)]>;
def MOVAPDmr : PDI<0x29, MRMDestMem, (outs), (ins f128mem:$dst, VR128:$src),
                   "movapd\t{$src, $dst|$dst, $src}",
                   [(alignedstore (v2f64 VR128:$src), addr:$dst)]>;
def MOVUPSmr : PSI<0x11, MRMDestMem, (outs), (ins f128mem:$dst, VR128:$src),
                   "movups\t{$src, $dst|$dst, $src}",
                   [(store (v4f32 VR128:$src), addr:$dst)]>;
def MOVUPDmr : PDI<0x11, MRMDestMem, (outs), (ins f128mem:$dst, VR128:$src),
                   "movupd\t{$src, $dst|$dst, $src}",
                   [(store (v2f64 VR128:$src), addr:$dst)]>;
} // SchedRW

// For disassembler
let isCodeGenOnly = 1, ForceDisassemble = 1, hasSideEffects = 0,
    isMoveReg = 1, SchedRW = [SchedWriteFMoveLS.XMM.RR] in {
  def MOVAPSrr_REV : PSI<0x29, MRMDestReg, (outs VR128:$dst), (ins VR128:$src),
                         "movaps\t{$src, $dst|$dst, $src}", []>,
                         FoldGenData<"MOVAPSrr">;
  def MOVAPDrr_REV : PDI<0x29, MRMDestReg, (outs VR128:$dst), (ins VR128:$src),
                         "movapd\t{$src, $dst|$dst, $src}", []>,
                         FoldGenData<"MOVAPDrr">;
  def MOVUPSrr_REV : PSI<0x11, MRMDestReg, (outs VR128:$dst), (ins VR128:$src),
                         "movups\t{$src, $dst|$dst, $src}", []>,
                         FoldGenData<"MOVUPSrr">;
  def MOVUPDrr_REV : PDI<0x11, MRMDestReg, (outs VR128:$dst), (ins VR128:$src),
                         "movupd\t{$src, $dst|$dst, $src}", []>,
                         FoldGenData<"MOVUPDrr">;
}

// Reversed version with ".s" suffix for GAS compatibility.
def : InstAlias<"movaps.s\t{$src, $dst|$dst, $src}",
                (MOVAPSrr_REV VR128:$dst, VR128:$src), 0>;
def : InstAlias<"movapd.s\t{$src, $dst|$dst, $src}",
                (MOVAPDrr_REV VR128:$dst, VR128:$src), 0>;
def : InstAlias<"movups.s\t{$src, $dst|$dst, $src}",
                (MOVUPSrr_REV VR128:$dst, VR128:$src), 0>;
def : InstAlias<"movupd.s\t{$src, $dst|$dst, $src}",
                (MOVUPDrr_REV VR128:$dst, VR128:$src), 0>;

let Predicates = [HasAVX, NoVLX] in {
  // 256-bit load/store need to use floating point load/store in case we don't
  // have AVX2. Execution domain fixing will convert to integer if AVX2 is
  // available and changing the domain is beneficial.
  def : Pat<(alignedloadv4i64 addr:$src),
            (VMOVAPSYrm addr:$src)>;
  def : Pat<(alignedloadv8i32 addr:$src),
            (VMOVAPSYrm addr:$src)>;
  def : Pat<(alignedloadv16i16 addr:$src),
            (VMOVAPSYrm addr:$src)>;
  def : Pat<(alignedloadv32i8 addr:$src),
            (VMOVAPSYrm addr:$src)>;
  def : Pat<(loadv4i64 addr:$src),
            (VMOVUPSYrm addr:$src)>;
  def : Pat<(loadv8i32 addr:$src),
            (VMOVUPSYrm addr:$src)>;
  def : Pat<(loadv16i16 addr:$src),
            (VMOVUPSYrm addr:$src)>;
  def : Pat<(loadv32i8 addr:$src),
            (VMOVUPSYrm addr:$src)>;

  def : Pat<(alignedstore (v4i64 VR256:$src), addr:$dst),
            (VMOVAPSYmr addr:$dst, VR256:$src)>;
  def : Pat<(alignedstore (v8i32 VR256:$src), addr:$dst),
            (VMOVAPSYmr addr:$dst, VR256:$src)>;
  def : Pat<(alignedstore (v16i16 VR256:$src), addr:$dst),
            (VMOVAPSYmr addr:$dst, VR256:$src)>;
  def : Pat<(alignedstore (v32i8 VR256:$src), addr:$dst),
            (VMOVAPSYmr addr:$dst, VR256:$src)>;
  def : Pat<(store (v4i64 VR256:$src), addr:$dst),
            (VMOVUPSYmr addr:$dst, VR256:$src)>;
  def : Pat<(store (v8i32 VR256:$src), addr:$dst),
            (VMOVUPSYmr addr:$dst, VR256:$src)>;
  def : Pat<(store (v16i16 VR256:$src), addr:$dst),
            (VMOVUPSYmr addr:$dst, VR256:$src)>;
  def : Pat<(store (v32i8 VR256:$src), addr:$dst),
            (VMOVUPSYmr addr:$dst, VR256:$src)>;
}

// Use movaps / movups for SSE integer load / store (one byte shorter).
// The instructions selected below are then converted to MOVDQA/MOVDQU
// during the SSE domain pass.
let Predicates = [UseSSE1] in {
  def : Pat<(alignedloadv2i64 addr:$src),
            (MOVAPSrm addr:$src)>;
  def : Pat<(alignedloadv4i32 addr:$src),
            (MOVAPSrm addr:$src)>;
  def : Pat<(alignedloadv8i16 addr:$src),
            (MOVAPSrm addr:$src)>;
  def : Pat<(alignedloadv16i8 addr:$src),
            (MOVAPSrm addr:$src)>;
  def : Pat<(loadv2i64 addr:$src),
            (MOVUPSrm addr:$src)>;
  def : Pat<(loadv4i32 addr:$src),
            (MOVUPSrm addr:$src)>;
  def : Pat<(loadv8i16 addr:$src),
            (MOVUPSrm addr:$src)>;
  def : Pat<(loadv16i8 addr:$src),
            (MOVUPSrm addr:$src)>;

  def : Pat<(alignedstore (v2i64 VR128:$src), addr:$dst),
            (MOVAPSmr addr:$dst, VR128:$src)>;
  def : Pat<(alignedstore (v4i32 VR128:$src), addr:$dst),
            (MOVAPSmr addr:$dst, VR128:$src)>;
  def : Pat<(alignedstore (v8i16 VR128:$src), addr:$dst),
            (MOVAPSmr addr:$dst, VR128:$src)>;
  def : Pat<(alignedstore (v16i8 VR128:$src), addr:$dst),
            (MOVAPSmr addr:$dst, VR128:$src)>;
  def : Pat<(store (v2i64 VR128:$src), addr:$dst),
            (MOVUPSmr addr:$dst, VR128:$src)>;
  def : Pat<(store (v4i32 VR128:$src), addr:$dst),
            (MOVUPSmr addr:$dst, VR128:$src)>;
  def : Pat<(store (v8i16 VR128:$src), addr:$dst),
            (MOVUPSmr addr:$dst, VR128:$src)>;
  def : Pat<(store (v16i8 VR128:$src), addr:$dst),
            (MOVUPSmr addr:$dst, VR128:$src)>;
}

//===----------------------------------------------------------------------===//
// SSE 1 & 2 - Move Low packed FP Instructions
//===----------------------------------------------------------------------===//

multiclass sse12_mov_hilo_packed_base<bits<8>opc,  SDNode pdnode,
                                      string base_opc, string asm_opr> {
  // No pattern as they need be special cased between high and low.
  let hasSideEffects = 0, mayLoad = 1 in
  def PSrm : PI<opc, MRMSrcMem,
                (outs VR128:$dst), (ins VR128:$src1, f64mem:$src2),
                !strconcat(base_opc, "s", asm_opr),
                [], SSEPackedSingle>, PS,
                Sched<[SchedWriteFShuffle.XMM.Folded, SchedWriteFShuffle.XMM.ReadAfterFold]>;

  def PDrm : PI<opc, MRMSrcMem,
         (outs VR128:$dst), (ins VR128:$src1, f64mem:$src2),
         !strconcat(base_opc, "d", asm_opr),
     [(set VR128:$dst, (v2f64 (pdnode VR128:$src1,
                              (scalar_to_vector (loadf64 addr:$src2)))))],
              SSEPackedDouble>, PD,
     Sched<[SchedWriteFShuffle.XMM.Folded, SchedWriteFShuffle.XMM.ReadAfterFold]>;
}

multiclass sse12_mov_hilo_packed<bits<8>opc, SDPatternOperator pdnode,
                                 string base_opc> {
  let Predicates = [UseAVX] in
    defm V#NAME : sse12_mov_hilo_packed_base<opc, pdnode, base_opc,
                                    "\t{$src2, $src1, $dst|$dst, $src1, $src2}">,
                                    VEX_4V, VEX_WIG;

  let Constraints = "$src1 = $dst" in
    defm NAME : sse12_mov_hilo_packed_base<opc,  pdnode, base_opc,
                                    "\t{$src2, $dst|$dst, $src2}">;
}

defm MOVL : sse12_mov_hilo_packed<0x12, X86Movsd, "movlp">;

let SchedRW = [WriteFStore] in {
let Predicates = [UseAVX] in {
def VMOVLPSmr : VPSI<0x13, MRMDestMem, (outs), (ins f64mem:$dst, VR128:$src),
                     "movlps\t{$src, $dst|$dst, $src}",
                     [(store (f64 (extractelt (bc_v2f64 (v4f32 VR128:$src)),
                                   (iPTR 0))), addr:$dst)]>,
                     VEX, VEX_WIG;
def VMOVLPDmr : VPDI<0x13, MRMDestMem, (outs), (ins f64mem:$dst, VR128:$src),
                     "movlpd\t{$src, $dst|$dst, $src}",
                     [(store (f64 (extractelt (v2f64 VR128:$src),
                                   (iPTR 0))), addr:$dst)]>,
                     VEX, VEX_WIG;
}// UseAVX
def MOVLPSmr : PSI<0x13, MRMDestMem, (outs), (ins f64mem:$dst, VR128:$src),
                   "movlps\t{$src, $dst|$dst, $src}",
                   [(store (f64 (extractelt (bc_v2f64 (v4f32 VR128:$src)),
                                 (iPTR 0))), addr:$dst)]>;
def MOVLPDmr : PDI<0x13, MRMDestMem, (outs), (ins f64mem:$dst, VR128:$src),
                   "movlpd\t{$src, $dst|$dst, $src}",
                   [(store (f64 (extractelt (v2f64 VR128:$src),
                                 (iPTR 0))), addr:$dst)]>;
} // SchedRW

let Predicates = [UseSSE1] in {
  // (store (vector_shuffle (load addr), v2, <4, 5, 2, 3>), addr) using MOVLPS
  def : Pat<(store (i64 (extractelt (bc_v2i64 (v4f32 VR128:$src2)),
                                 (iPTR 0))), addr:$src1),
            (MOVLPSmr addr:$src1, VR128:$src2)>;

  // This pattern helps select MOVLPS on SSE1 only targets. With SSE2 we'll
  // end up with a movsd or blend instead of shufp.
  // No need for aligned load, we're only loading 64-bits.
  def : Pat<(X86Shufp (loadv4f32 addr:$src2), VR128:$src1, (i8 -28)),
            (MOVLPSrm VR128:$src1, addr:$src2)>;
}

//===----------------------------------------------------------------------===//
// SSE 1 & 2 - Move Hi packed FP Instructions
//===----------------------------------------------------------------------===//

defm MOVH : sse12_mov_hilo_packed<0x16, X86Unpckl, "movhp">;

let SchedRW = [WriteFStore] in {
// v2f64 extract element 1 is always custom lowered to unpack high to low
// and extract element 0 so the non-store version isn't too horrible.
let Predicates = [UseAVX] in {
def VMOVHPSmr : VPSI<0x17, MRMDestMem, (outs), (ins f64mem:$dst, VR128:$src),
                   "movhps\t{$src, $dst|$dst, $src}",
                   [(store (f64 (extractelt
                                 (X86Unpckh (bc_v2f64 (v4f32 VR128:$src)),
                                            (bc_v2f64 (v4f32 VR128:$src))),
                                 (iPTR 0))), addr:$dst)]>, VEX, VEX_WIG;
def VMOVHPDmr : VPDI<0x17, MRMDestMem, (outs), (ins f64mem:$dst, VR128:$src),
                   "movhpd\t{$src, $dst|$dst, $src}",
                   [(store (f64 (extractelt
                                 (v2f64 (X86Unpckh VR128:$src, VR128:$src)),
                                 (iPTR 0))), addr:$dst)]>, VEX, VEX_WIG;
} // UseAVX
def MOVHPSmr : PSI<0x17, MRMDestMem, (outs), (ins f64mem:$dst, VR128:$src),
                   "movhps\t{$src, $dst|$dst, $src}",
                   [(store (f64 (extractelt
                                 (X86Unpckh (bc_v2f64 (v4f32 VR128:$src)),
                                            (bc_v2f64 (v4f32 VR128:$src))),
                                 (iPTR 0))), addr:$dst)]>;
def MOVHPDmr : PDI<0x17, MRMDestMem, (outs), (ins f64mem:$dst, VR128:$src),
                   "movhpd\t{$src, $dst|$dst, $src}",
                   [(store (f64 (extractelt
                                 (v2f64 (X86Unpckh VR128:$src, VR128:$src)),
                                 (iPTR 0))), addr:$dst)]>;
} // SchedRW

let Predicates = [UseAVX] in {
  // Also handle an i64 load because that may get selected as a faster way to
  // load the data.
  def : Pat<(v2f64 (X86Unpckl VR128:$src1,
                      (bc_v2f64 (v2i64 (scalar_to_vector (loadi64 addr:$src2)))))),
            (VMOVHPDrm VR128:$src1, addr:$src2)>;

  def : Pat<(store (f64 (extractelt
                          (v2f64 (X86VPermilpi VR128:$src, (i8 1))),
                          (iPTR 0))), addr:$dst),
            (VMOVHPDmr addr:$dst, VR128:$src)>;
}

let Predicates = [UseSSE1] in {
  // This pattern helps select MOVHPS on SSE1 only targets. With SSE2 we'll
  // end up with a movsd or blend instead of shufp.
  // No need for aligned load, we're only loading 64-bits.
  def : Pat<(X86Movlhps VR128:$src1, (loadv4f32 addr:$src2)),
            (MOVHPSrm VR128:$src1, addr:$src2)>;
}

let Predicates = [UseSSE2] in {
  // MOVHPD patterns

  // Also handle an i64 load because that may get selected as a faster way to
  // load the data.
  def : Pat<(v2f64 (X86Unpckl VR128:$src1,
                      (bc_v2f64 (v2i64 (scalar_to_vector (loadi64 addr:$src2)))))),
            (MOVHPDrm VR128:$src1, addr:$src2)>;

  def : Pat<(store (f64 (extractelt
                          (v2f64 (X86Shufp VR128:$src, VR128:$src, (i8 1))),
                          (iPTR 0))), addr:$dst),
            (MOVHPDmr addr:$dst, VR128:$src)>;
}

//===----------------------------------------------------------------------===//
// SSE 1 & 2 - Move Low to High and High to Low packed FP Instructions
//===----------------------------------------------------------------------===//

let Predicates = [UseAVX] in {
  def VMOVLHPSrr : VPSI<0x16, MRMSrcReg, (outs VR128:$dst),
                                       (ins VR128:$src1, VR128:$src2),
                      "movlhps\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                      [(set VR128:$dst,
                        (v4f32 (X86Movlhps VR128:$src1, VR128:$src2)))]>,
                      VEX_4V, Sched<[SchedWriteFShuffle.XMM]>, VEX_WIG;
  let isCommutable = 1 in
  def VMOVHLPSrr : VPSI<0x12, MRMSrcReg, (outs VR128:$dst),
                                       (ins VR128:$src1, VR128:$src2),
                      "movhlps\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                      [(set VR128:$dst,
                        (v4f32 (X86Movhlps VR128:$src1, VR128:$src2)))]>,
                      VEX_4V, Sched<[SchedWriteFShuffle.XMM]>, VEX_WIG,
                      NotMemoryFoldable;
}
let Constraints = "$src1 = $dst" in {
  def MOVLHPSrr : PSI<0x16, MRMSrcReg, (outs VR128:$dst),
                                       (ins VR128:$src1, VR128:$src2),
                      "movlhps\t{$src2, $dst|$dst, $src2}",
                      [(set VR128:$dst,
                        (v4f32 (X86Movlhps VR128:$src1, VR128:$src2)))]>,
                      Sched<[SchedWriteFShuffle.XMM]>;
  let isCommutable = 1 in
  def MOVHLPSrr : PSI<0x12, MRMSrcReg, (outs VR128:$dst),
                                       (ins VR128:$src1, VR128:$src2),
                      "movhlps\t{$src2, $dst|$dst, $src2}",
                      [(set VR128:$dst,
                        (v4f32 (X86Movhlps VR128:$src1, VR128:$src2)))]>,
                      Sched<[SchedWriteFShuffle.XMM]>, NotMemoryFoldable;
}

//===----------------------------------------------------------------------===//
// SSE 1 & 2 - Conversion Instructions
//===----------------------------------------------------------------------===//

multiclass sse12_cvt_s<bits<8> opc, RegisterClass SrcRC, RegisterClass DstRC,
                     SDNode OpNode, X86MemOperand x86memop, PatFrag ld_frag,
                     string asm, X86FoldableSchedWrite sched> {
  def rr : SI<opc, MRMSrcReg, (outs DstRC:$dst), (ins SrcRC:$src), asm,
                        [(set DstRC:$dst, (OpNode SrcRC:$src))]>,
                        Sched<[sched]>;
  def rm : SI<opc, MRMSrcMem, (outs DstRC:$dst), (ins x86memop:$src), asm,
                        [(set DstRC:$dst, (OpNode (ld_frag addr:$src)))]>,
                        Sched<[sched.Folded]>;
}

multiclass sse12_cvt_p<bits<8> opc, RegisterClass RC, X86MemOperand x86memop,
                       ValueType DstTy, ValueType SrcTy, PatFrag ld_frag,
                       string asm, Domain d, X86FoldableSchedWrite sched> {
let hasSideEffects = 0 in {
  def rr : I<opc, MRMSrcReg, (outs RC:$dst), (ins RC:$src), asm,
             [(set RC:$dst, (DstTy (sint_to_fp (SrcTy RC:$src))))], d>,
             Sched<[sched]>;
  let mayLoad = 1 in
  def rm : I<opc, MRMSrcMem, (outs RC:$dst), (ins x86memop:$src), asm,
             [(set RC:$dst, (DstTy (sint_to_fp
                                    (SrcTy (ld_frag addr:$src)))))], d>,
             Sched<[sched.Folded]>;
}
}

multiclass sse12_vcvt_avx<bits<8> opc, RegisterClass SrcRC, RegisterClass DstRC,
                          X86MemOperand x86memop, string asm,
                          X86FoldableSchedWrite sched> {
let hasSideEffects = 0, Predicates = [UseAVX] in {
  def rr : SI<opc, MRMSrcReg, (outs DstRC:$dst), (ins DstRC:$src1, SrcRC:$src),
              !strconcat(asm,"\t{$src, $src1, $dst|$dst, $src1, $src}"), []>,
              Sched<[sched]>;
  let mayLoad = 1 in
  def rm : SI<opc, MRMSrcMem, (outs DstRC:$dst),
              (ins DstRC:$src1, x86memop:$src),
              !strconcat(asm,"\t{$src, $src1, $dst|$dst, $src1, $src}"), []>,
           Sched<[sched.Folded, sched.ReadAfterFold]>;
} // hasSideEffects = 0
}

let Predicates = [UseAVX] in {
defm VCVTTSS2SI   : sse12_cvt_s<0x2C, FR32, GR32, fp_to_sint, f32mem, loadf32,
                                "cvttss2si\t{$src, $dst|$dst, $src}",
                                WriteCvtSS2I>,
                                XS, VEX, VEX_LIG;
defm VCVTTSS2SI64 : sse12_cvt_s<0x2C, FR32, GR64, fp_to_sint, f32mem, loadf32,
                                "cvttss2si\t{$src, $dst|$dst, $src}",
                                WriteCvtSS2I>,
                                XS, VEX, VEX_W, VEX_LIG;
defm VCVTTSD2SI   : sse12_cvt_s<0x2C, FR64, GR32, fp_to_sint, f64mem, loadf64,
                                "cvttsd2si\t{$src, $dst|$dst, $src}",
                                WriteCvtSD2I>,
                                XD, VEX, VEX_LIG;
defm VCVTTSD2SI64 : sse12_cvt_s<0x2C, FR64, GR64, fp_to_sint, f64mem, loadf64,
                                "cvttsd2si\t{$src, $dst|$dst, $src}",
                                WriteCvtSD2I>,
                                XD, VEX, VEX_W, VEX_LIG;

def : InstAlias<"vcvttss2si{l}\t{$src, $dst|$dst, $src}",
                (VCVTTSS2SIrr GR32:$dst, FR32:$src), 0, "att">;
def : InstAlias<"vcvttss2si{l}\t{$src, $dst|$dst, $src}",
                (VCVTTSS2SIrm GR32:$dst, f32mem:$src), 0, "att">;
def : InstAlias<"vcvttsd2si{l}\t{$src, $dst|$dst, $src}",
                (VCVTTSD2SIrr GR32:$dst, FR64:$src), 0, "att">;
def : InstAlias<"vcvttsd2si{l}\t{$src, $dst|$dst, $src}",
                (VCVTTSD2SIrm GR32:$dst, f64mem:$src), 0, "att">;
def : InstAlias<"vcvttss2si{q}\t{$src, $dst|$dst, $src}",
                (VCVTTSS2SI64rr GR64:$dst, FR32:$src), 0, "att">;
def : InstAlias<"vcvttss2si{q}\t{$src, $dst|$dst, $src}",
                (VCVTTSS2SI64rm GR64:$dst, f32mem:$src), 0, "att">;
def : InstAlias<"vcvttsd2si{q}\t{$src, $dst|$dst, $src}",
                (VCVTTSD2SI64rr GR64:$dst, FR64:$src), 0, "att">;
def : InstAlias<"vcvttsd2si{q}\t{$src, $dst|$dst, $src}",
                (VCVTTSD2SI64rm GR64:$dst, f64mem:$src), 0, "att">;
}
// The assembler can recognize rr 64-bit instructions by seeing a rxx
// register, but the same isn't true when only using memory operands,
// provide other assembly "l" and "q" forms to address this explicitly
// where appropriate to do so.
defm VCVTSI2SS   : sse12_vcvt_avx<0x2A, GR32, FR32, i32mem, "cvtsi2ss{l}",
                                  WriteCvtI2SS>, XS, VEX_4V, VEX_LIG;
defm VCVTSI642SS : sse12_vcvt_avx<0x2A, GR64, FR32, i64mem, "cvtsi2ss{q}",
                                  WriteCvtI2SS>, XS, VEX_4V, VEX_W, VEX_LIG;
defm VCVTSI2SD   : sse12_vcvt_avx<0x2A, GR32, FR64, i32mem, "cvtsi2sd{l}",
                                  WriteCvtI2SD>, XD, VEX_4V, VEX_LIG;
defm VCVTSI642SD : sse12_vcvt_avx<0x2A, GR64, FR64, i64mem, "cvtsi2sd{q}",
                                  WriteCvtI2SD>, XD, VEX_4V, VEX_W, VEX_LIG;

let Predicates = [UseAVX] in {
  def : InstAlias<"vcvtsi2ss\t{$src, $src1, $dst|$dst, $src1, $src}",
                (VCVTSI2SSrm FR64:$dst, FR64:$src1, i32mem:$src), 0, "att">;
  def : InstAlias<"vcvtsi2sd\t{$src, $src1, $dst|$dst, $src1, $src}",
                (VCVTSI2SDrm FR64:$dst, FR64:$src1, i32mem:$src), 0, "att">;

  def : Pat<(f32 (sint_to_fp (loadi32 addr:$src))),
            (VCVTSI2SSrm (f32 (IMPLICIT_DEF)), addr:$src)>;
  def : Pat<(f32 (sint_to_fp (loadi64 addr:$src))),
            (VCVTSI642SSrm (f32 (IMPLICIT_DEF)), addr:$src)>;
  def : Pat<(f64 (sint_to_fp (loadi32 addr:$src))),
            (VCVTSI2SDrm (f64 (IMPLICIT_DEF)), addr:$src)>;
  def : Pat<(f64 (sint_to_fp (loadi64 addr:$src))),
            (VCVTSI642SDrm (f64 (IMPLICIT_DEF)), addr:$src)>;

  def : Pat<(f32 (sint_to_fp GR32:$src)),
            (VCVTSI2SSrr (f32 (IMPLICIT_DEF)), GR32:$src)>;
  def : Pat<(f32 (sint_to_fp GR64:$src)),
            (VCVTSI642SSrr (f32 (IMPLICIT_DEF)), GR64:$src)>;
  def : Pat<(f64 (sint_to_fp GR32:$src)),
            (VCVTSI2SDrr (f64 (IMPLICIT_DEF)), GR32:$src)>;
  def : Pat<(f64 (sint_to_fp GR64:$src)),
            (VCVTSI642SDrr (f64 (IMPLICIT_DEF)), GR64:$src)>;
}

defm CVTTSS2SI : sse12_cvt_s<0x2C, FR32, GR32, fp_to_sint, f32mem, loadf32,
                      "cvttss2si\t{$src, $dst|$dst, $src}",
                      WriteCvtSS2I>, XS;
defm CVTTSS2SI64 : sse12_cvt_s<0x2C, FR32, GR64, fp_to_sint, f32mem, loadf32,
                      "cvttss2si\t{$src, $dst|$dst, $src}",
                      WriteCvtSS2I>, XS, REX_W;
defm CVTTSD2SI : sse12_cvt_s<0x2C, FR64, GR32, fp_to_sint, f64mem, loadf64,
                      "cvttsd2si\t{$src, $dst|$dst, $src}",
                      WriteCvtSD2I>, XD;
defm CVTTSD2SI64 : sse12_cvt_s<0x2C, FR64, GR64, fp_to_sint, f64mem, loadf64,
                      "cvttsd2si\t{$src, $dst|$dst, $src}",
                      WriteCvtSD2I>, XD, REX_W;
defm CVTSI2SS  : sse12_cvt_s<0x2A, GR32, FR32, sint_to_fp, i32mem, loadi32,
                      "cvtsi2ss{l}\t{$src, $dst|$dst, $src}",
                      WriteCvtI2SS>, XS;
defm CVTSI642SS : sse12_cvt_s<0x2A, GR64, FR32, sint_to_fp, i64mem, loadi64,
                      "cvtsi2ss{q}\t{$src, $dst|$dst, $src}",
                      WriteCvtI2SS>, XS, REX_W;
defm CVTSI2SD  : sse12_cvt_s<0x2A, GR32, FR64, sint_to_fp, i32mem, loadi32,
                      "cvtsi2sd{l}\t{$src, $dst|$dst, $src}",
                      WriteCvtI2SD>, XD;
defm CVTSI642SD : sse12_cvt_s<0x2A, GR64, FR64, sint_to_fp, i64mem, loadi64,
                      "cvtsi2sd{q}\t{$src, $dst|$dst, $src}",
                      WriteCvtI2SD>, XD, REX_W;

def : InstAlias<"cvttss2si{l}\t{$src, $dst|$dst, $src}",
                (CVTTSS2SIrr GR32:$dst, FR32:$src), 0, "att">;
def : InstAlias<"cvttss2si{l}\t{$src, $dst|$dst, $src}",
                (CVTTSS2SIrm GR32:$dst, f32mem:$src), 0, "att">;
def : InstAlias<"cvttsd2si{l}\t{$src, $dst|$dst, $src}",
                (CVTTSD2SIrr GR32:$dst, FR64:$src), 0, "att">;
def : InstAlias<"cvttsd2si{l}\t{$src, $dst|$dst, $src}",
                (CVTTSD2SIrm GR32:$dst, f64mem:$src), 0, "att">;
def : InstAlias<"cvttss2si{q}\t{$src, $dst|$dst, $src}",
                (CVTTSS2SI64rr GR64:$dst, FR32:$src), 0, "att">;
def : InstAlias<"cvttss2si{q}\t{$src, $dst|$dst, $src}",
                (CVTTSS2SI64rm GR64:$dst, f32mem:$src), 0, "att">;
def : InstAlias<"cvttsd2si{q}\t{$src, $dst|$dst, $src}",
                (CVTTSD2SI64rr GR64:$dst, FR64:$src), 0, "att">;
def : InstAlias<"cvttsd2si{q}\t{$src, $dst|$dst, $src}",
                (CVTTSD2SI64rm GR64:$dst, f64mem:$src), 0, "att">;

def : InstAlias<"cvtsi2ss\t{$src, $dst|$dst, $src}",
                (CVTSI2SSrm FR64:$dst, i32mem:$src), 0, "att">;
def : InstAlias<"cvtsi2sd\t{$src, $dst|$dst, $src}",
                (CVTSI2SDrm FR64:$dst, i32mem:$src), 0, "att">;

// Conversion Instructions Intrinsics - Match intrinsics which expect MM
// and/or XMM operand(s).

multiclass sse12_cvt_sint<bits<8> opc, RegisterClass SrcRC, RegisterClass DstRC,
                          ValueType DstVT, ValueType SrcVT, SDNode OpNode,
                          Operand memop, ComplexPattern mem_cpat, string asm,
                          X86FoldableSchedWrite sched> {
  def rr_Int : SI<opc, MRMSrcReg, (outs DstRC:$dst), (ins SrcRC:$src),
                  !strconcat(asm, "\t{$src, $dst|$dst, $src}"),
                  [(set DstRC:$dst, (DstVT (OpNode (SrcVT SrcRC:$src))))]>,
               Sched<[sched]>;
  def rm_Int : SI<opc, MRMSrcMem, (outs DstRC:$dst), (ins memop:$src),
                  !strconcat(asm, "\t{$src, $dst|$dst, $src}"),
                  [(set DstRC:$dst, (DstVT (OpNode (SrcVT mem_cpat:$src))))]>,
               Sched<[sched.Folded]>;
}

multiclass sse12_cvt_sint_3addr<bits<8> opc, RegisterClass SrcRC,
                    RegisterClass DstRC, X86MemOperand x86memop,
                    string asm, X86FoldableSchedWrite sched,
                    bit Is2Addr = 1> {
let hasSideEffects = 0 in {
  def rr_Int : SI<opc, MRMSrcReg, (outs DstRC:$dst), (ins DstRC:$src1, SrcRC:$src2),
                  !if(Is2Addr,
                      !strconcat(asm, "\t{$src2, $dst|$dst, $src2}"),
                      !strconcat(asm, "\t{$src2, $src1, $dst|$dst, $src1, $src2}")),
                  []>, Sched<[sched]>;
  let mayLoad = 1 in
  def rm_Int : SI<opc, MRMSrcMem, (outs DstRC:$dst),
                  (ins DstRC:$src1, x86memop:$src2),
                  !if(Is2Addr,
                      !strconcat(asm, "\t{$src2, $dst|$dst, $src2}"),
                      !strconcat(asm, "\t{$src2, $src1, $dst|$dst, $src1, $src2}")),
                  []>, Sched<[sched.Folded, sched.ReadAfterFold]>;
}
}

let Predicates = [UseAVX] in {
defm VCVTSD2SI : sse12_cvt_sint<0x2D, VR128, GR32, i32, v2f64,
                  X86cvts2si, sdmem, sse_load_f64, "cvtsd2si",
                  WriteCvtSD2I>, XD, VEX, VEX_LIG;
defm VCVTSD2SI64 : sse12_cvt_sint<0x2D, VR128, GR64, i64, v2f64,
                    X86cvts2si, sdmem, sse_load_f64, "cvtsd2si",
                    WriteCvtSD2I>, XD, VEX, VEX_W, VEX_LIG;
}
defm CVTSD2SI : sse12_cvt_sint<0x2D, VR128, GR32, i32, v2f64, X86cvts2si,
                 sdmem, sse_load_f64, "cvtsd2si", WriteCvtSD2I>, XD;
defm CVTSD2SI64 : sse12_cvt_sint<0x2D, VR128, GR64, i64, v2f64, X86cvts2si,
                   sdmem, sse_load_f64, "cvtsd2si", WriteCvtSD2I>, XD, REX_W;


let isCodeGenOnly = 1 in {
  let Predicates = [UseAVX] in {
  defm VCVTSI2SS : sse12_cvt_sint_3addr<0x2A, GR32, VR128,
            i32mem, "cvtsi2ss{l}", WriteCvtI2SS, 0>, XS, VEX_4V;
  defm VCVTSI642SS : sse12_cvt_sint_3addr<0x2A, GR64, VR128,
            i64mem, "cvtsi2ss{q}", WriteCvtI2SS, 0>, XS, VEX_4V, VEX_W;
  defm VCVTSI2SD : sse12_cvt_sint_3addr<0x2A, GR32, VR128,
            i32mem, "cvtsi2sd{l}", WriteCvtI2SD, 0>, XD, VEX_4V;
  defm VCVTSI642SD : sse12_cvt_sint_3addr<0x2A, GR64, VR128,
            i64mem, "cvtsi2sd{q}", WriteCvtI2SD, 0>, XD, VEX_4V, VEX_W;
  }
  let Constraints = "$src1 = $dst" in {
    defm CVTSI2SS : sse12_cvt_sint_3addr<0x2A, GR32, VR128,
                          i32mem, "cvtsi2ss{l}", WriteCvtI2SS>, XS;
    defm CVTSI642SS : sse12_cvt_sint_3addr<0x2A, GR64, VR128,
                          i64mem, "cvtsi2ss{q}", WriteCvtI2SS>, XS, REX_W;
    defm CVTSI2SD : sse12_cvt_sint_3addr<0x2A, GR32, VR128,
                          i32mem, "cvtsi2sd{l}", WriteCvtI2SD>, XD;
    defm CVTSI642SD : sse12_cvt_sint_3addr<0x2A, GR64, VR128,
                          i64mem, "cvtsi2sd{q}", WriteCvtI2SD>, XD, REX_W;
  }
} // isCodeGenOnly = 1

/// SSE 1 Only

// Aliases for intrinsics
let isCodeGenOnly = 1 in {
let Predicates = [UseAVX] in {
defm VCVTTSS2SI : sse12_cvt_sint<0x2C, VR128, GR32, i32, v4f32, X86cvtts2Int,
                                ssmem, sse_load_f32, "cvttss2si",
                                WriteCvtSS2I>, XS, VEX;
defm VCVTTSS2SI64 : sse12_cvt_sint<0x2C, VR128, GR64, i64, v4f32,
                               X86cvtts2Int, ssmem, sse_load_f32,
                               "cvttss2si", WriteCvtSS2I>,
                               XS, VEX, VEX_W;
defm VCVTTSD2SI : sse12_cvt_sint<0x2C, VR128, GR32, i32, v2f64, X86cvtts2Int,
                                sdmem, sse_load_f64, "cvttsd2si",
                                WriteCvtSS2I>, XD, VEX;
defm VCVTTSD2SI64 : sse12_cvt_sint<0x2C, VR128, GR64, i64, v2f64,
                              X86cvtts2Int, sdmem, sse_load_f64,
                              "cvttsd2si", WriteCvtSS2I>,
                              XD, VEX, VEX_W;
}
defm CVTTSS2SI : sse12_cvt_sint<0x2C, VR128, GR32, i32, v4f32, X86cvtts2Int,
                                    ssmem, sse_load_f32, "cvttss2si",
                                    WriteCvtSS2I>, XS;
defm CVTTSS2SI64 : sse12_cvt_sint<0x2C, VR128, GR64, i64, v4f32,
                                   X86cvtts2Int, ssmem, sse_load_f32,
                                   "cvttss2si", WriteCvtSS2I>, XS, REX_W;
defm CVTTSD2SI : sse12_cvt_sint<0x2C, VR128, GR32, i32, v2f64, X86cvtts2Int,
                                    sdmem, sse_load_f64, "cvttsd2si",
                                    WriteCvtSD2I>, XD;
defm CVTTSD2SI64 : sse12_cvt_sint<0x2C, VR128, GR64, i64, v2f64,
                                  X86cvtts2Int, sdmem, sse_load_f64,
                                  "cvttsd2si", WriteCvtSD2I>, XD, REX_W;
} // isCodeGenOnly = 1

let Predicates = [UseAVX] in {
defm VCVTSS2SI   : sse12_cvt_sint<0x2D, VR128, GR32, i32, v4f32, X86cvts2si,
                                  ssmem, sse_load_f32, "cvtss2si",
                                  WriteCvtSS2I>, XS, VEX, VEX_LIG;
defm VCVTSS2SI64 : sse12_cvt_sint<0x2D, VR128, GR64, i64, v4f32, X86cvts2si,
                                  ssmem, sse_load_f32, "cvtss2si",
                                  WriteCvtSS2I>, XS, VEX, VEX_W, VEX_LIG;
}
defm CVTSS2SI : sse12_cvt_sint<0x2D, VR128, GR32, i32, v4f32, X86cvts2si,
                               ssmem, sse_load_f32, "cvtss2si",
                               WriteCvtSS2I>, XS;
defm CVTSS2SI64 : sse12_cvt_sint<0x2D, VR128, GR64, i64, v4f32, X86cvts2si,
                                 ssmem, sse_load_f32, "cvtss2si",
                                 WriteCvtSS2I>, XS, REX_W;

defm VCVTDQ2PS   : sse12_cvt_p<0x5B, VR128, i128mem, v4f32, v4i32, load,
                               "vcvtdq2ps\t{$src, $dst|$dst, $src}",
                               SSEPackedSingle, WriteCvtI2PS>,
                               PS, VEX, Requires<[HasAVX, NoVLX]>, VEX_WIG;
defm VCVTDQ2PSY  : sse12_cvt_p<0x5B, VR256, i256mem, v8f32, v8i32, load,
                               "vcvtdq2ps\t{$src, $dst|$dst, $src}",
                               SSEPackedSingle, WriteCvtI2PSY>,
                               PS, VEX, VEX_L, Requires<[HasAVX, NoVLX]>, VEX_WIG;

defm CVTDQ2PS : sse12_cvt_p<0x5B, VR128, i128mem, v4f32, v4i32, memop,
                            "cvtdq2ps\t{$src, $dst|$dst, $src}",
                            SSEPackedSingle, WriteCvtI2PS>,
                            PS, Requires<[UseSSE2]>;

let Predicates = [UseAVX] in {
def : InstAlias<"vcvtss2si{l}\t{$src, $dst|$dst, $src}",
                (VCVTSS2SIrr_Int GR32:$dst, VR128:$src), 0, "att">;
def : InstAlias<"vcvtss2si{l}\t{$src, $dst|$dst, $src}",
                (VCVTSS2SIrm_Int GR32:$dst, ssmem:$src), 0, "att">;
def : InstAlias<"vcvtsd2si{l}\t{$src, $dst|$dst, $src}",
                (VCVTSD2SIrr_Int GR32:$dst, VR128:$src), 0, "att">;
def : InstAlias<"vcvtsd2si{l}\t{$src, $dst|$dst, $src}",
                (VCVTSD2SIrm_Int GR32:$dst, sdmem:$src), 0, "att">;
def : InstAlias<"vcvtss2si{q}\t{$src, $dst|$dst, $src}",
                (VCVTSS2SI64rr_Int GR64:$dst, VR128:$src), 0, "att">;
def : InstAlias<"vcvtss2si{q}\t{$src, $dst|$dst, $src}",
                (VCVTSS2SI64rm_Int GR64:$dst, ssmem:$src), 0, "att">;
def : InstAlias<"vcvtsd2si{q}\t{$src, $dst|$dst, $src}",
                (VCVTSD2SI64rr_Int GR64:$dst, VR128:$src), 0, "att">;
def : InstAlias<"vcvtsd2si{q}\t{$src, $dst|$dst, $src}",
                (VCVTSD2SI64rm_Int GR64:$dst, sdmem:$src), 0, "att">;
}

def : InstAlias<"cvtss2si{l}\t{$src, $dst|$dst, $src}",
                (CVTSS2SIrr_Int GR32:$dst, VR128:$src), 0, "att">;
def : InstAlias<"cvtss2si{l}\t{$src, $dst|$dst, $src}",
                (CVTSS2SIrm_Int GR32:$dst, ssmem:$src), 0, "att">;
def : InstAlias<"cvtsd2si{l}\t{$src, $dst|$dst, $src}",
                (CVTSD2SIrr_Int GR32:$dst, VR128:$src), 0, "att">;
def : InstAlias<"cvtsd2si{l}\t{$src, $dst|$dst, $src}",
                (CVTSD2SIrm_Int GR32:$dst, sdmem:$src), 0, "att">;
def : InstAlias<"cvtss2si{q}\t{$src, $dst|$dst, $src}",
                (CVTSS2SI64rr_Int GR64:$dst, VR128:$src), 0, "att">;
def : InstAlias<"cvtss2si{q}\t{$src, $dst|$dst, $src}",
                (CVTSS2SI64rm_Int GR64:$dst, ssmem:$src), 0, "att">;
def : InstAlias<"cvtsd2si{q}\t{$src, $dst|$dst, $src}",
                (CVTSD2SI64rr_Int GR64:$dst, VR128:$src), 0, "att">;
def : InstAlias<"cvtsd2si{q}\t{$src, $dst|$dst, $src}",
                (CVTSD2SI64rm_Int GR64:$dst, sdmem:$src), 0, "att">;

/// SSE 2 Only

// Convert scalar double to scalar single
let hasSideEffects = 0, Predicates = [UseAVX] in {
def VCVTSD2SSrr  : VSDI<0x5A, MRMSrcReg, (outs FR32:$dst),
                        (ins FR32:$src1, FR64:$src2),
                        "cvtsd2ss\t{$src2, $src1, $dst|$dst, $src1, $src2}", []>,
                        VEX_4V, VEX_LIG, VEX_WIG,
                        Sched<[WriteCvtSD2SS]>;
let mayLoad = 1 in
def VCVTSD2SSrm  : I<0x5A, MRMSrcMem, (outs FR32:$dst),
                     (ins FR32:$src1, f64mem:$src2),
                     "vcvtsd2ss\t{$src2, $src1, $dst|$dst, $src1, $src2}", []>,
                     XD, VEX_4V, VEX_LIG, VEX_WIG,
                     Sched<[WriteCvtSD2SS.Folded, WriteCvtSD2SS.ReadAfterFold]>;
}

def : Pat<(f32 (fpround FR64:$src)),
            (VCVTSD2SSrr (f32 (IMPLICIT_DEF)), FR64:$src)>,
          Requires<[UseAVX]>;

def CVTSD2SSrr  : SDI<0x5A, MRMSrcReg, (outs FR32:$dst), (ins FR64:$src),
                      "cvtsd2ss\t{$src, $dst|$dst, $src}",
                      [(set FR32:$dst, (fpround FR64:$src))]>,
                      Sched<[WriteCvtSD2SS]>;
def CVTSD2SSrm  : I<0x5A, MRMSrcMem, (outs FR32:$dst), (ins f64mem:$src),
                    "cvtsd2ss\t{$src, $dst|$dst, $src}",
                    [(set FR32:$dst, (fpround (loadf64 addr:$src)))]>,
                    XD, Requires<[UseSSE2, OptForSize]>,
                    Sched<[WriteCvtSD2SS.Folded]>;

let isCodeGenOnly = 1 in {
def VCVTSD2SSrr_Int: I<0x5A, MRMSrcReg,
                       (outs VR128:$dst), (ins VR128:$src1, VR128:$src2),
                       "vcvtsd2ss\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                       [(set VR128:$dst,
                         (int_x86_sse2_cvtsd2ss VR128:$src1, VR128:$src2))]>,
                       XD, VEX_4V, VEX_WIG, Requires<[HasAVX]>,
                       Sched<[WriteCvtSD2SS]>;
def VCVTSD2SSrm_Int: I<0x5A, MRMSrcMem,
                       (outs VR128:$dst), (ins VR128:$src1, sdmem:$src2),
                       "vcvtsd2ss\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                       [(set VR128:$dst, (int_x86_sse2_cvtsd2ss
                                          VR128:$src1, sse_load_f64:$src2))]>,
                       XD, VEX_4V, VEX_WIG, Requires<[HasAVX]>,
                       Sched<[WriteCvtSD2SS.Folded, WriteCvtSD2SS.ReadAfterFold]>;
let Constraints = "$src1 = $dst" in {
def CVTSD2SSrr_Int: I<0x5A, MRMSrcReg,
                       (outs VR128:$dst), (ins VR128:$src1, VR128:$src2),
                       "cvtsd2ss\t{$src2, $dst|$dst, $src2}",
                       [(set VR128:$dst,
                         (int_x86_sse2_cvtsd2ss VR128:$src1, VR128:$src2))]>,
                       XD, Requires<[UseSSE2]>, Sched<[WriteCvtSD2SS]>;
def CVTSD2SSrm_Int: I<0x5A, MRMSrcMem,
                       (outs VR128:$dst), (ins VR128:$src1, sdmem:$src2),
                       "cvtsd2ss\t{$src2, $dst|$dst, $src2}",
                       [(set VR128:$dst, (int_x86_sse2_cvtsd2ss
                                          VR128:$src1, sse_load_f64:$src2))]>,
                       XD, Requires<[UseSSE2]>,
                       Sched<[WriteCvtSD2SS.Folded, WriteCvtSD2SS.ReadAfterFold]>;
}
} // isCodeGenOnly = 1

// Convert scalar single to scalar double
// SSE2 instructions with XS prefix
let hasSideEffects = 0 in {
def VCVTSS2SDrr : I<0x5A, MRMSrcReg, (outs FR64:$dst),
                    (ins FR64:$src1, FR32:$src2),
                    "vcvtss2sd\t{$src2, $src1, $dst|$dst, $src1, $src2}", []>,
                    XS, VEX_4V, VEX_LIG, VEX_WIG,
                    Sched<[WriteCvtSS2SD]>, Requires<[UseAVX]>;
let mayLoad = 1 in
def VCVTSS2SDrm : I<0x5A, MRMSrcMem, (outs FR64:$dst),
                    (ins FR64:$src1, f32mem:$src2),
                    "vcvtss2sd\t{$src2, $src1, $dst|$dst, $src1, $src2}", []>,
                    XS, VEX_4V, VEX_LIG, VEX_WIG,
                    Sched<[WriteCvtSS2SD.Folded, WriteCvtSS2SD.ReadAfterFold]>,
                    Requires<[UseAVX, OptForSize]>;
}

def : Pat<(f64 (fpextend FR32:$src)),
    (VCVTSS2SDrr (f64 (IMPLICIT_DEF)), FR32:$src)>, Requires<[UseAVX]>;
def : Pat<(fpextend (loadf32 addr:$src)),
    (VCVTSS2SDrm (f64 (IMPLICIT_DEF)), addr:$src)>, Requires<[UseAVX, OptForSize]>;

def : Pat<(extloadf32 addr:$src),
    (VCVTSS2SDrm (f64 (IMPLICIT_DEF)), addr:$src)>,
    Requires<[UseAVX, OptForSize]>;
def : Pat<(extloadf32 addr:$src),
    (VCVTSS2SDrr (f64 (IMPLICIT_DEF)), (VMOVSSrm addr:$src))>,
    Requires<[UseAVX, OptForSpeed]>;

def CVTSS2SDrr : I<0x5A, MRMSrcReg, (outs FR64:$dst), (ins FR32:$src),
                   "cvtss2sd\t{$src, $dst|$dst, $src}",
                   [(set FR64:$dst, (fpextend FR32:$src))]>,
                   XS, Requires<[UseSSE2]>, Sched<[WriteCvtSS2SD]>;
def CVTSS2SDrm : I<0x5A, MRMSrcMem, (outs FR64:$dst), (ins f32mem:$src),
                   "cvtss2sd\t{$src, $dst|$dst, $src}",
                   [(set FR64:$dst, (extloadf32 addr:$src))]>,
                   XS, Requires<[UseSSE2, OptForSize]>,
                   Sched<[WriteCvtSS2SD.Folded]>;

// extload f32 -> f64.  This matches load+fpextend because we have a hack in
// the isel (PreprocessForFPConvert) that can introduce loads after dag
// combine.
// Since these loads aren't folded into the fpextend, we have to match it
// explicitly here.
def : Pat<(fpextend (loadf32 addr:$src)),
          (CVTSS2SDrm addr:$src)>, Requires<[UseSSE2, OptForSize]>;
def : Pat<(extloadf32 addr:$src),
          (CVTSS2SDrr (MOVSSrm addr:$src))>, Requires<[UseSSE2, OptForSpeed]>;

let isCodeGenOnly = 1, hasSideEffects = 0 in {
def VCVTSS2SDrr_Int: I<0x5A, MRMSrcReg,
                      (outs VR128:$dst), (ins VR128:$src1, VR128:$src2),
                    "vcvtss2sd\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                    []>, XS, VEX_4V, VEX_WIG,
                    Requires<[HasAVX]>, Sched<[WriteCvtSS2SD]>;
let mayLoad = 1 in
def VCVTSS2SDrm_Int: I<0x5A, MRMSrcMem,
                      (outs VR128:$dst), (ins VR128:$src1, ssmem:$src2),
                    "vcvtss2sd\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                    []>, XS, VEX_4V, VEX_WIG, Requires<[HasAVX]>,
                    Sched<[WriteCvtSS2SD.Folded, WriteCvtSS2SD.ReadAfterFold]>;
let Constraints = "$src1 = $dst" in { // SSE2 instructions with XS prefix
def CVTSS2SDrr_Int: I<0x5A, MRMSrcReg,
                      (outs VR128:$dst), (ins VR128:$src1, VR128:$src2),
                    "cvtss2sd\t{$src2, $dst|$dst, $src2}",
                    []>, XS, Requires<[UseSSE2]>,
                    Sched<[WriteCvtSS2SD]>;
let mayLoad = 1 in
def CVTSS2SDrm_Int: I<0x5A, MRMSrcMem,
                      (outs VR128:$dst), (ins VR128:$src1, ssmem:$src2),
                    "cvtss2sd\t{$src2, $dst|$dst, $src2}",
                    []>, XS, Requires<[UseSSE2]>,
                    Sched<[WriteCvtSS2SD.Folded, WriteCvtSS2SD.ReadAfterFold]>;
}
} // isCodeGenOnly = 1

// Patterns used for matching (v)cvtsi2ss, (v)cvtsi2sd, (v)cvtsd2ss and
// (v)cvtss2sd intrinsic sequences from clang which produce unnecessary
// vmovs{s,d} instructions
let Predicates = [UseAVX] in {
def : Pat<(v4f32 (X86Movss
                   (v4f32 VR128:$dst),
                   (v4f32 (scalar_to_vector
                     (f32 (fpround (f64 (extractelt VR128:$src, (iPTR 0))))))))),
          (VCVTSD2SSrr_Int VR128:$dst, VR128:$src)>;

def : Pat<(v2f64 (X86Movsd
                   (v2f64 VR128:$dst),
                   (v2f64 (scalar_to_vector
                     (f64 (fpextend (f32 (extractelt VR128:$src, (iPTR 0))))))))),
          (VCVTSS2SDrr_Int VR128:$dst, VR128:$src)>;

def : Pat<(v4f32 (X86Movss
                   (v4f32 VR128:$dst),
                   (v4f32 (scalar_to_vector (f32 (sint_to_fp GR64:$src)))))),
          (VCVTSI642SSrr_Int VR128:$dst, GR64:$src)>;

def : Pat<(v4f32 (X86Movss
                   (v4f32 VR128:$dst),
                   (v4f32 (scalar_to_vector (f32 (sint_to_fp (loadi64 addr:$src))))))),
          (VCVTSI642SSrm_Int VR128:$dst, addr:$src)>;

def : Pat<(v4f32 (X86Movss
                   (v4f32 VR128:$dst),
                   (v4f32 (scalar_to_vector (f32 (sint_to_fp GR32:$src)))))),
          (VCVTSI2SSrr_Int VR128:$dst, GR32:$src)>;

def : Pat<(v4f32 (X86Movss
                   (v4f32 VR128:$dst),
                   (v4f32 (scalar_to_vector (f32 (sint_to_fp (loadi32 addr:$src))))))),
          (VCVTSI2SSrm_Int VR128:$dst, addr:$src)>;

def : Pat<(v2f64 (X86Movsd
                   (v2f64 VR128:$dst),
                   (v2f64 (scalar_to_vector (f64 (sint_to_fp GR64:$src)))))),
          (VCVTSI642SDrr_Int VR128:$dst, GR64:$src)>;

def : Pat<(v2f64 (X86Movsd
                   (v2f64 VR128:$dst),
                   (v2f64 (scalar_to_vector (f64 (sint_to_fp (loadi64 addr:$src))))))),
          (VCVTSI642SDrm_Int VR128:$dst, addr:$src)>;

def : Pat<(v2f64 (X86Movsd
                   (v2f64 VR128:$dst),
                   (v2f64 (scalar_to_vector (f64 (sint_to_fp GR32:$src)))))),
          (VCVTSI2SDrr_Int VR128:$dst, GR32:$src)>;

def : Pat<(v2f64 (X86Movsd
                   (v2f64 VR128:$dst),
                   (v2f64 (scalar_to_vector (f64 (sint_to_fp (loadi32 addr:$src))))))),
          (VCVTSI2SDrm_Int VR128:$dst, addr:$src)>;
} // Predicates = [UseAVX]

let Predicates = [UseSSE2] in {
def : Pat<(v4f32 (X86Movss
                   (v4f32 VR128:$dst),
                   (v4f32 (scalar_to_vector
                     (f32 (fpround (f64 (extractelt VR128:$src, (iPTR 0))))))))),
          (CVTSD2SSrr_Int VR128:$dst, VR128:$src)>;

def : Pat<(v2f64 (X86Movsd
                   (v2f64 VR128:$dst),
                   (v2f64 (scalar_to_vector
                     (f64 (fpextend (f32 (extractelt VR128:$src, (iPTR 0))))))))),
          (CVTSS2SDrr_Int VR128:$dst, VR128:$src)>;

def : Pat<(v2f64 (X86Movsd
                   (v2f64 VR128:$dst),
                   (v2f64 (scalar_to_vector (f64 (sint_to_fp GR64:$src)))))),
          (CVTSI642SDrr_Int VR128:$dst, GR64:$src)>;

def : Pat<(v2f64 (X86Movsd
                   (v2f64 VR128:$dst),
                   (v2f64 (scalar_to_vector (f64 (sint_to_fp (loadi64 addr:$src))))))),
          (CVTSI642SDrm_Int VR128:$dst, addr:$src)>;

def : Pat<(v2f64 (X86Movsd
                   (v2f64 VR128:$dst),
                   (v2f64 (scalar_to_vector (f64 (sint_to_fp GR32:$src)))))),
          (CVTSI2SDrr_Int VR128:$dst, GR32:$src)>;

def : Pat<(v2f64 (X86Movsd
                   (v2f64 VR128:$dst),
                   (v2f64 (scalar_to_vector (f64 (sint_to_fp (loadi32 addr:$src))))))),
          (CVTSI2SDrm_Int VR128:$dst, addr:$src)>;
} // Predicates = [UseSSE2]

let Predicates = [UseSSE1] in {
def : Pat<(v4f32 (X86Movss
                   (v4f32 VR128:$dst),
                   (v4f32 (scalar_to_vector (f32 (sint_to_fp GR64:$src)))))),
          (CVTSI642SSrr_Int VR128:$dst, GR64:$src)>;

def : Pat<(v4f32 (X86Movss
                   (v4f32 VR128:$dst),
                   (v4f32 (scalar_to_vector (f32 (sint_to_fp (loadi64 addr:$src))))))),
          (CVTSI642SSrm_Int VR128:$dst, addr:$src)>;

def : Pat<(v4f32 (X86Movss
                   (v4f32 VR128:$dst),
                   (v4f32 (scalar_to_vector (f32 (sint_to_fp GR32:$src)))))),
          (CVTSI2SSrr_Int VR128:$dst, GR32:$src)>;

def : Pat<(v4f32 (X86Movss
                   (v4f32 VR128:$dst),
                   (v4f32 (scalar_to_vector (f32 (sint_to_fp (loadi32 addr:$src))))))),
          (CVTSI2SSrm_Int VR128:$dst, addr:$src)>;
} // Predicates = [UseSSE1]

let Predicates = [HasAVX, NoVLX] in {
// Convert packed single/double fp to doubleword
def VCVTPS2DQrr : VPDI<0x5B, MRMSrcReg, (outs VR128:$dst), (ins VR128:$src),
                       "cvtps2dq\t{$src, $dst|$dst, $src}",
                       [(set VR128:$dst, (v4i32 (X86cvtp2Int (v4f32 VR128:$src))))]>,
                       VEX, Sched<[WriteCvtPS2I]>, VEX_WIG;
def VCVTPS2DQrm : VPDI<0x5B, MRMSrcMem, (outs VR128:$dst), (ins f128mem:$src),
                       "cvtps2dq\t{$src, $dst|$dst, $src}",
                       [(set VR128:$dst,
                         (v4i32 (X86cvtp2Int (loadv4f32 addr:$src))))]>,
                       VEX, Sched<[WriteCvtPS2ILd]>, VEX_WIG;
def VCVTPS2DQYrr : VPDI<0x5B, MRMSrcReg, (outs VR256:$dst), (ins VR256:$src),
                        "cvtps2dq\t{$src, $dst|$dst, $src}",
                        [(set VR256:$dst,
                          (v8i32 (X86cvtp2Int (v8f32 VR256:$src))))]>,
                        VEX, VEX_L, Sched<[WriteCvtPS2IY]>, VEX_WIG;
def VCVTPS2DQYrm : VPDI<0x5B, MRMSrcMem, (outs VR256:$dst), (ins f256mem:$src),
                        "cvtps2dq\t{$src, $dst|$dst, $src}",
                        [(set VR256:$dst,
                          (v8i32 (X86cvtp2Int (loadv8f32 addr:$src))))]>,
                        VEX, VEX_L, Sched<[WriteCvtPS2IYLd]>, VEX_WIG;
}
def CVTPS2DQrr : PDI<0x5B, MRMSrcReg, (outs VR128:$dst), (ins VR128:$src),
                     "cvtps2dq\t{$src, $dst|$dst, $src}",
                     [(set VR128:$dst, (v4i32 (X86cvtp2Int (v4f32 VR128:$src))))]>,
                     Sched<[WriteCvtPS2I]>;
def CVTPS2DQrm : PDI<0x5B, MRMSrcMem, (outs VR128:$dst), (ins f128mem:$src),
                     "cvtps2dq\t{$src, $dst|$dst, $src}",
                     [(set VR128:$dst,
                       (v4i32 (X86cvtp2Int (memopv4f32 addr:$src))))]>,
                     Sched<[WriteCvtPS2ILd]>;


// Convert Packed Double FP to Packed DW Integers
let Predicates = [HasAVX, NoVLX] in {
// The assembler can recognize rr 256-bit instructions by seeing a ymm
// register, but the same isn't true when using memory operands instead.
// Provide other assembly rr and rm forms to address this explicitly.
def VCVTPD2DQrr  : SDI<0xE6, MRMSrcReg, (outs VR128:$dst), (ins VR128:$src),
                       "vcvtpd2dq\t{$src, $dst|$dst, $src}",
                       [(set VR128:$dst,
                         (v4i32 (X86cvtp2Int (v2f64 VR128:$src))))]>,
                       VEX, Sched<[WriteCvtPD2I]>, VEX_WIG;

// XMM only
def : InstAlias<"vcvtpd2dqx\t{$src, $dst|$dst, $src}",
                (VCVTPD2DQrr VR128:$dst, VR128:$src), 0>;
def VCVTPD2DQrm : SDI<0xE6, MRMSrcMem, (outs VR128:$dst), (ins f128mem:$src),
                      "vcvtpd2dq{x}\t{$src, $dst|$dst, $src}",
                      [(set VR128:$dst,
                        (v4i32 (X86cvtp2Int (loadv2f64 addr:$src))))]>, VEX,
                      Sched<[WriteCvtPD2ILd]>, VEX_WIG;
def : InstAlias<"vcvtpd2dqx\t{$src, $dst|$dst, $src}",
                (VCVTPD2DQrm VR128:$dst, f128mem:$src), 0, "intel">;

// YMM only
def VCVTPD2DQYrr : SDI<0xE6, MRMSrcReg, (outs VR128:$dst), (ins VR256:$src),
                       "vcvtpd2dq\t{$src, $dst|$dst, $src}",
                       [(set VR128:$dst,
                         (v4i32 (X86cvtp2Int (v4f64 VR256:$src))))]>,
                       VEX, VEX_L, Sched<[WriteCvtPD2IY]>, VEX_WIG;
def VCVTPD2DQYrm : SDI<0xE6, MRMSrcMem, (outs VR128:$dst), (ins f256mem:$src),
                       "vcvtpd2dq{y}\t{$src, $dst|$dst, $src}",
                       [(set VR128:$dst,
                         (v4i32 (X86cvtp2Int (loadv4f64 addr:$src))))]>,
                       VEX, VEX_L, Sched<[WriteCvtPD2IYLd]>, VEX_WIG;
def : InstAlias<"vcvtpd2dqy\t{$src, $dst|$dst, $src}",
                (VCVTPD2DQYrr VR128:$dst, VR256:$src), 0>;
def : InstAlias<"vcvtpd2dqy\t{$src, $dst|$dst, $src}",
                (VCVTPD2DQYrm VR128:$dst, f256mem:$src), 0, "intel">;
}

def CVTPD2DQrm  : SDI<0xE6, MRMSrcMem, (outs VR128:$dst), (ins f128mem:$src),
                      "cvtpd2dq\t{$src, $dst|$dst, $src}",
                      [(set VR128:$dst,
                        (v4i32 (X86cvtp2Int (memopv2f64 addr:$src))))]>,
                      Sched<[WriteCvtPD2ILd]>;
def CVTPD2DQrr  : SDI<0xE6, MRMSrcReg, (outs VR128:$dst), (ins VR128:$src),
                      "cvtpd2dq\t{$src, $dst|$dst, $src}",
                      [(set VR128:$dst,
                        (v4i32 (X86cvtp2Int (v2f64 VR128:$src))))]>,
                      Sched<[WriteCvtPD2I]>;

// Convert with truncation packed single/double fp to doubleword
// SSE2 packed instructions with XS prefix
let Predicates = [HasAVX, NoVLX] in {
def VCVTTPS2DQrr : VS2SI<0x5B, MRMSrcReg, (outs VR128:$dst), (ins VR128:$src),
                         "cvttps2dq\t{$src, $dst|$dst, $src}",
                         [(set VR128:$dst,
                           (v4i32 (X86cvttp2si (v4f32 VR128:$src))))]>,
                         VEX, Sched<[WriteCvtPS2I]>, VEX_WIG;
def VCVTTPS2DQrm : VS2SI<0x5B, MRMSrcMem, (outs VR128:$dst), (ins f128mem:$src),
                         "cvttps2dq\t{$src, $dst|$dst, $src}",
                         [(set VR128:$dst,
                           (v4i32 (X86cvttp2si (loadv4f32 addr:$src))))]>,
                         VEX, Sched<[WriteCvtPS2ILd]>, VEX_WIG;
def VCVTTPS2DQYrr : VS2SI<0x5B, MRMSrcReg, (outs VR256:$dst), (ins VR256:$src),
                          "cvttps2dq\t{$src, $dst|$dst, $src}",
                          [(set VR256:$dst,
                            (v8i32 (X86cvttp2si (v8f32 VR256:$src))))]>,
                          VEX, VEX_L, Sched<[WriteCvtPS2IY]>, VEX_WIG;
def VCVTTPS2DQYrm : VS2SI<0x5B, MRMSrcMem, (outs VR256:$dst), (ins f256mem:$src),
                          "cvttps2dq\t{$src, $dst|$dst, $src}",
                          [(set VR256:$dst,
                            (v8i32 (X86cvttp2si (loadv8f32 addr:$src))))]>,
                          VEX, VEX_L,
                          Sched<[WriteCvtPS2IYLd]>, VEX_WIG;
}

let Predicates = [HasAVX, NoVLX] in {
  def : Pat<(v4i32 (fp_to_sint (v4f32 VR128:$src))),
            (VCVTTPS2DQrr VR128:$src)>;
  def : Pat<(v4i32 (fp_to_sint (loadv4f32 addr:$src))),
            (VCVTTPS2DQrm addr:$src)>;
  def : Pat<(v8i32 (fp_to_sint (v8f32 VR256:$src))),
            (VCVTTPS2DQYrr VR256:$src)>;
  def : Pat<(v8i32 (fp_to_sint (loadv8f32 addr:$src))),
            (VCVTTPS2DQYrm addr:$src)>;
}

def CVTTPS2DQrr : S2SI<0x5B, MRMSrcReg, (outs VR128:$dst), (ins VR128:$src),
                       "cvttps2dq\t{$src, $dst|$dst, $src}",
                       [(set VR128:$dst,
                         (v4i32 (X86cvttp2si (v4f32 VR128:$src))))]>,
                       Sched<[WriteCvtPS2I]>;
def CVTTPS2DQrm : S2SI<0x5B, MRMSrcMem, (outs VR128:$dst), (ins f128mem:$src),
                       "cvttps2dq\t{$src, $dst|$dst, $src}",
                       [(set VR128:$dst,
                         (v4i32 (X86cvttp2si (memopv4f32 addr:$src))))]>,
                       Sched<[WriteCvtPS2ILd]>;

let Predicates = [UseSSE2] in {
  def : Pat<(v4i32 (fp_to_sint (v4f32 VR128:$src))),
            (CVTTPS2DQrr VR128:$src)>;
  def : Pat<(v4i32 (fp_to_sint (memopv4f32 addr:$src))),
            (CVTTPS2DQrm addr:$src)>;
}

let Predicates = [HasAVX, NoVLX] in
def VCVTTPD2DQrr : VPDI<0xE6, MRMSrcReg, (outs VR128:$dst), (ins VR128:$src),
                        "cvttpd2dq\t{$src, $dst|$dst, $src}",
                        [(set VR128:$dst,
                          (v4i32 (X86cvttp2si (v2f64 VR128:$src))))]>,
                        VEX, Sched<[WriteCvtPD2I]>, VEX_WIG;

// The assembler can recognize rr 256-bit instructions by seeing a ymm
// register, but the same isn't true when using memory operands instead.
// Provide other assembly rr and rm forms to address this explicitly.

// XMM only
def : InstAlias<"vcvttpd2dqx\t{$src, $dst|$dst, $src}",
                (VCVTTPD2DQrr VR128:$dst, VR128:$src), 0>;

let Predicates = [HasAVX, NoVLX] in
def VCVTTPD2DQrm : VPDI<0xE6, MRMSrcMem, (outs VR128:$dst), (ins f128mem:$src),
                        "cvttpd2dq{x}\t{$src, $dst|$dst, $src}",
                        [(set VR128:$dst,
                          (v4i32 (X86cvttp2si (loadv2f64 addr:$src))))]>,
                        VEX, Sched<[WriteCvtPD2ILd]>, VEX_WIG;
def : InstAlias<"vcvttpd2dqx\t{$src, $dst|$dst, $src}",
                (VCVTTPD2DQrm VR128:$dst, f128mem:$src), 0, "intel">;

// YMM only
let Predicates = [HasAVX, NoVLX] in {
def VCVTTPD2DQYrr : VPDI<0xE6, MRMSrcReg, (outs VR128:$dst), (ins VR256:$src),
                         "cvttpd2dq\t{$src, $dst|$dst, $src}",
                         [(set VR128:$dst,
                           (v4i32 (X86cvttp2si (v4f64 VR256:$src))))]>,
                         VEX, VEX_L, Sched<[WriteCvtPD2IY]>, VEX_WIG;
def VCVTTPD2DQYrm : VPDI<0xE6, MRMSrcMem, (outs VR128:$dst), (ins f256mem:$src),
                         "cvttpd2dq{y}\t{$src, $dst|$dst, $src}",
                         [(set VR128:$dst,
                           (v4i32 (X86cvttp2si (loadv4f64 addr:$src))))]>,
                         VEX, VEX_L, Sched<[WriteCvtPD2IYLd]>, VEX_WIG;
}
def : InstAlias<"vcvttpd2dqy\t{$src, $dst|$dst, $src}",
                (VCVTTPD2DQYrr VR128:$dst, VR256:$src), 0>;
def : InstAlias<"vcvttpd2dqy\t{$src, $dst|$dst, $src}",
                (VCVTTPD2DQYrm VR128:$dst, f256mem:$src), 0, "intel">;

let Predicates = [HasAVX, NoVLX] in {
  def : Pat<(v4i32 (fp_to_sint (v4f64 VR256:$src))),
            (VCVTTPD2DQYrr VR256:$src)>;
  def : Pat<(v4i32 (fp_to_sint (loadv4f64 addr:$src))),
            (VCVTTPD2DQYrm addr:$src)>;
}

let Predicates = [HasAVX, NoVLX] in {
  def : Pat<(X86vzmovl (v2i64 (bitconvert
                               (v4i32 (X86cvtp2Int (v2f64 VR128:$src)))))),
            (VCVTPD2DQrr VR128:$src)>;
  def : Pat<(X86vzmovl (v2i64 (bitconvert
                               (v4i32 (X86cvtp2Int (loadv2f64 addr:$src)))))),
            (VCVTPD2DQrm addr:$src)>;
  def : Pat<(X86vzmovl (v2i64 (bitconvert
                               (v4i32 (X86cvttp2si (v2f64 VR128:$src)))))),
            (VCVTTPD2DQrr VR128:$src)>;
  def : Pat<(X86vzmovl (v2i64 (bitconvert
                               (v4i32 (X86cvttp2si (loadv2f64 addr:$src)))))),
            (VCVTTPD2DQrm addr:$src)>;
} // Predicates = [HasAVX, NoVLX]

def CVTTPD2DQrr : PDI<0xE6, MRMSrcReg, (outs VR128:$dst), (ins VR128:$src),
                      "cvttpd2dq\t{$src, $dst|$dst, $src}",
                      [(set VR128:$dst,
                        (v4i32 (X86cvttp2si (v2f64 VR128:$src))))]>,
                      Sched<[WriteCvtPD2I]>;
def CVTTPD2DQrm : PDI<0xE6, MRMSrcMem, (outs VR128:$dst),(ins f128mem:$src),
                      "cvttpd2dq\t{$src, $dst|$dst, $src}",
                      [(set VR128:$dst,
                        (v4i32 (X86cvttp2si (memopv2f64 addr:$src))))]>,
                      Sched<[WriteCvtPD2ILd]>;

let Predicates = [UseSSE2] in {
  def : Pat<(X86vzmovl (v2i64 (bitconvert
                               (v4i32 (X86cvtp2Int (v2f64 VR128:$src)))))),
            (CVTPD2DQrr VR128:$src)>;
  def : Pat<(X86vzmovl (v2i64 (bitconvert
                               (v4i32 (X86cvtp2Int (memopv2f64 addr:$src)))))),
            (CVTPD2DQrm addr:$src)>;
  def : Pat<(X86vzmovl (v2i64 (bitconvert
                               (v4i32 (X86cvttp2si (v2f64 VR128:$src)))))),
            (CVTTPD2DQrr VR128:$src)>;
  def : Pat<(X86vzmovl (v2i64 (bitconvert
                               (v4i32 (X86cvttp2si (memopv2f64 addr:$src)))))),
            (CVTTPD2DQrm addr:$src)>;
} // Predicates = [UseSSE2]

// Convert packed single to packed double
let Predicates = [HasAVX, NoVLX] in {
                  // SSE2 instructions without OpSize prefix
def VCVTPS2PDrr : I<0x5A, MRMSrcReg, (outs VR128:$dst), (ins VR128:$src),
                    "vcvtps2pd\t{$src, $dst|$dst, $src}",
                    [(set VR128:$dst, (v2f64 (X86vfpext (v4f32 VR128:$src))))]>,
                    PS, VEX, Sched<[WriteCvtPS2PD]>, VEX_WIG;
def VCVTPS2PDrm : I<0x5A, MRMSrcMem, (outs VR128:$dst), (ins f64mem:$src),
                    "vcvtps2pd\t{$src, $dst|$dst, $src}",
                    [(set VR128:$dst, (v2f64 (extloadv2f32 addr:$src)))]>,
                    PS, VEX, Sched<[WriteCvtPS2PD.Folded]>, VEX_WIG;
def VCVTPS2PDYrr : I<0x5A, MRMSrcReg, (outs VR256:$dst), (ins VR128:$src),
                     "vcvtps2pd\t{$src, $dst|$dst, $src}",
                     [(set VR256:$dst, (v4f64 (fpextend (v4f32 VR128:$src))))]>,
                     PS, VEX, VEX_L, Sched<[WriteCvtPS2PDY]>, VEX_WIG;
def VCVTPS2PDYrm : I<0x5A, MRMSrcMem, (outs VR256:$dst), (ins f128mem:$src),
                     "vcvtps2pd\t{$src, $dst|$dst, $src}",
                     [(set VR256:$dst, (v4f64 (extloadv4f32 addr:$src)))]>,
                     PS, VEX, VEX_L, Sched<[WriteCvtPS2PDY.Folded]>, VEX_WIG;
}

let Predicates = [UseSSE2] in {
def CVTPS2PDrr : I<0x5A, MRMSrcReg, (outs VR128:$dst), (ins VR128:$src),
                   "cvtps2pd\t{$src, $dst|$dst, $src}",
                   [(set VR128:$dst, (v2f64 (X86vfpext (v4f32 VR128:$src))))]>,
                   PS, Sched<[WriteCvtPS2PD]>;
def CVTPS2PDrm : I<0x5A, MRMSrcMem, (outs VR128:$dst), (ins f64mem:$src),
                   "cvtps2pd\t{$src, $dst|$dst, $src}",
                   [(set VR128:$dst, (v2f64 (extloadv2f32 addr:$src)))]>,
                   PS, Sched<[WriteCvtPS2PD.Folded]>;
}

// Convert Packed DW Integers to Packed Double FP
let Predicates = [HasAVX, NoVLX] in {
let hasSideEffects = 0, mayLoad = 1 in
def VCVTDQ2PDrm  : S2SI<0xE6, MRMSrcMem, (outs VR128:$dst), (ins i64mem:$src),
                        "vcvtdq2pd\t{$src, $dst|$dst, $src}",
                        [(set VR128:$dst,
                          (v2f64 (X86VSintToFP (loadv4i32 addr:$src))))]>,
                        VEX, Sched<[WriteCvtI2PDLd]>, VEX_WIG;
def VCVTDQ2PDrr  : S2SI<0xE6, MRMSrcReg, (outs VR128:$dst), (ins VR128:$src),
                        "vcvtdq2pd\t{$src, $dst|$dst, $src}",
                        [(set VR128:$dst,
                          (v2f64 (X86VSintToFP (v4i32 VR128:$src))))]>,
                        VEX, Sched<[WriteCvtI2PD]>, VEX_WIG;
def VCVTDQ2PDYrm  : S2SI<0xE6, MRMSrcMem, (outs VR256:$dst), (ins i128mem:$src),
                         "vcvtdq2pd\t{$src, $dst|$dst, $src}",
                         [(set VR256:$dst,
                           (v4f64 (sint_to_fp (loadv4i32 addr:$src))))]>,
                         VEX, VEX_L, Sched<[WriteCvtI2PDYLd]>,
                         VEX_WIG;
def VCVTDQ2PDYrr  : S2SI<0xE6, MRMSrcReg, (outs VR256:$dst), (ins VR128:$src),
                         "vcvtdq2pd\t{$src, $dst|$dst, $src}",
                         [(set VR256:$dst,
                           (v4f64 (sint_to_fp (v4i32 VR128:$src))))]>,
                         VEX, VEX_L, Sched<[WriteCvtI2PDY]>, VEX_WIG;
}

let hasSideEffects = 0, mayLoad = 1 in
def CVTDQ2PDrm  : S2SI<0xE6, MRMSrcMem, (outs VR128:$dst), (ins i64mem:$src),
                       "cvtdq2pd\t{$src, $dst|$dst, $src}",
                       [(set VR128:$dst,
                         (v2f64 (X86VSintToFP (loadv4i32 addr:$src))))]>,
                       Sched<[WriteCvtI2PDLd]>;
def CVTDQ2PDrr  : S2SI<0xE6, MRMSrcReg, (outs VR128:$dst), (ins VR128:$src),
                       "cvtdq2pd\t{$src, $dst|$dst, $src}",
                       [(set VR128:$dst,
                         (v2f64 (X86VSintToFP (v4i32 VR128:$src))))]>,
                       Sched<[WriteCvtI2PD]>;

// AVX register conversion intrinsics
let Predicates = [HasAVX, NoVLX] in {
  def : Pat<(v2f64 (X86VSintToFP (bc_v4i32 (v2i64 (scalar_to_vector (loadi64 addr:$src)))))),
            (VCVTDQ2PDrm addr:$src)>;
  def : Pat<(v2f64 (X86VSintToFP (bc_v4i32 (v2i64 (X86vzload addr:$src))))),
            (VCVTDQ2PDrm addr:$src)>;
} // Predicates = [HasAVX, NoVLX]

// SSE2 register conversion intrinsics
let Predicates = [UseSSE2] in {
  def : Pat<(v2f64 (X86VSintToFP (bc_v4i32 (v2i64 (scalar_to_vector (loadi64 addr:$src)))))),
            (CVTDQ2PDrm addr:$src)>;
  def : Pat<(v2f64 (X86VSintToFP (bc_v4i32 (v2i64 (X86vzload addr:$src))))),
            (CVTDQ2PDrm addr:$src)>;
} // Predicates = [UseSSE2]

// Convert packed double to packed single
// The assembler can recognize rr 256-bit instructions by seeing a ymm
// register, but the same isn't true when using memory operands instead.
// Provide other assembly rr and rm forms to address this explicitly.
let Predicates = [HasAVX, NoVLX] in
def VCVTPD2PSrr : VPDI<0x5A, MRMSrcReg, (outs VR128:$dst), (ins VR128:$src),
                       "cvtpd2ps\t{$src, $dst|$dst, $src}",
                       [(set VR128:$dst, (X86vfpround (v2f64 VR128:$src)))]>,
                       VEX, Sched<[WriteCvtPD2PS]>, VEX_WIG;

// XMM only
def : InstAlias<"vcvtpd2psx\t{$src, $dst|$dst, $src}",
                (VCVTPD2PSrr VR128:$dst, VR128:$src), 0>;
let Predicates = [HasAVX, NoVLX] in
def VCVTPD2PSrm : VPDI<0x5A, MRMSrcMem, (outs VR128:$dst), (ins f128mem:$src),
                       "cvtpd2ps{x}\t{$src, $dst|$dst, $src}",
                       [(set VR128:$dst, (X86vfpround (loadv2f64 addr:$src)))]>,
                       VEX, Sched<[WriteCvtPD2PS.Folded]>, VEX_WIG;
def : InstAlias<"vcvtpd2psx\t{$src, $dst|$dst, $src}",
                (VCVTPD2PSrm VR128:$dst, f128mem:$src), 0, "intel">;

// YMM only
let Predicates = [HasAVX, NoVLX] in {
def VCVTPD2PSYrr : VPDI<0x5A, MRMSrcReg, (outs VR128:$dst), (ins VR256:$src),
                        "cvtpd2ps\t{$src, $dst|$dst, $src}",
                        [(set VR128:$dst, (fpround VR256:$src))]>,
                        VEX, VEX_L, Sched<[WriteCvtPD2PSY]>, VEX_WIG;
def VCVTPD2PSYrm : VPDI<0x5A, MRMSrcMem, (outs VR128:$dst), (ins f256mem:$src),
                        "cvtpd2ps{y}\t{$src, $dst|$dst, $src}",
                        [(set VR128:$dst, (fpround (loadv4f64 addr:$src)))]>,
                        VEX, VEX_L, Sched<[WriteCvtPD2PSY.Folded]>, VEX_WIG;
}
def : InstAlias<"vcvtpd2psy\t{$src, $dst|$dst, $src}",
                (VCVTPD2PSYrr VR128:$dst, VR256:$src), 0>;
def : InstAlias<"vcvtpd2psy\t{$src, $dst|$dst, $src}",
                (VCVTPD2PSYrm VR128:$dst, f256mem:$src), 0, "intel">;

def CVTPD2PSrr : PDI<0x5A, MRMSrcReg, (outs VR128:$dst), (ins VR128:$src),
                     "cvtpd2ps\t{$src, $dst|$dst, $src}",
                     [(set VR128:$dst, (X86vfpround (v2f64 VR128:$src)))]>,
                     Sched<[WriteCvtPD2PS]>;
def CVTPD2PSrm : PDI<0x5A, MRMSrcMem, (outs VR128:$dst), (ins f128mem:$src),
                     "cvtpd2ps\t{$src, $dst|$dst, $src}",
                     [(set VR128:$dst, (X86vfpround (memopv2f64 addr:$src)))]>,
                     Sched<[WriteCvtPD2PS.Folded]>;

// AVX 256-bit register conversion intrinsics
// FIXME: Migrate SSE conversion intrinsics matching to use patterns as below
// whenever possible to avoid declaring two versions of each one.

let Predicates = [HasAVX, NoVLX] in {
  // Match fpround and fpextend for 128/256-bit conversions
  def : Pat<(X86vzmovl (v2f64 (bitconvert
                               (v4f32 (X86vfpround (v2f64 VR128:$src)))))),
            (VCVTPD2PSrr VR128:$src)>;
  def : Pat<(X86vzmovl (v2f64 (bitconvert
                               (v4f32 (X86vfpround (loadv2f64 addr:$src)))))),
            (VCVTPD2PSrm addr:$src)>;
}

let Predicates = [UseSSE2] in {
  // Match fpround and fpextend for 128 conversions
  def : Pat<(X86vzmovl (v2f64 (bitconvert
                               (v4f32 (X86vfpround (v2f64 VR128:$src)))))),
            (CVTPD2PSrr VR128:$src)>;
  def : Pat<(X86vzmovl (v2f64 (bitconvert
                               (v4f32 (X86vfpround (memopv2f64 addr:$src)))))),
            (CVTPD2PSrm addr:$src)>;
}

//===----------------------------------------------------------------------===//
// SSE 1 & 2 - Compare Instructions
//===----------------------------------------------------------------------===//

// sse12_cmp_scalar - sse 1 & 2 compare scalar instructions
multiclass sse12_cmp_scalar<RegisterClass RC, X86MemOperand x86memop,
                            Operand CC, SDNode OpNode, ValueType VT,
                            PatFrag ld_frag, string asm, string asm_alt,
                            X86FoldableSchedWrite sched> {
  let isCommutable = 1 in
  def rr : SIi8<0xC2, MRMSrcReg,
                (outs RC:$dst), (ins RC:$src1, RC:$src2, CC:$cc), asm,
                [(set RC:$dst, (OpNode (VT RC:$src1), RC:$src2, imm:$cc))]>,
                Sched<[sched]>;
  def rm : SIi8<0xC2, MRMSrcMem,
                (outs RC:$dst), (ins RC:$src1, x86memop:$src2, CC:$cc), asm,
                [(set RC:$dst, (OpNode (VT RC:$src1),
                                         (ld_frag addr:$src2), imm:$cc))]>,
                Sched<[sched.Folded, sched.ReadAfterFold]>;

  // Accept explicit immediate argument form instead of comparison code.
  let isAsmParserOnly = 1, hasSideEffects = 0 in {
    def rr_alt : SIi8<0xC2, MRMSrcReg, (outs RC:$dst),
                      (ins RC:$src1, RC:$src2, u8imm:$cc), asm_alt, []>,
                      Sched<[sched]>, NotMemoryFoldable;
    let mayLoad = 1 in
    def rm_alt : SIi8<0xC2, MRMSrcMem, (outs RC:$dst),
                      (ins RC:$src1, x86memop:$src2, u8imm:$cc), asm_alt, []>,
                      Sched<[sched.Folded, sched.ReadAfterFold]>, NotMemoryFoldable;
  }
}

let ExeDomain = SSEPackedSingle in
defm VCMPSS : sse12_cmp_scalar<FR32, f32mem, AVXCC, X86cmps, f32, loadf32,
                 "cmp${cc}ss\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                 "cmpss\t{$cc, $src2, $src1, $dst|$dst, $src1, $src2, $cc}",
                 SchedWriteFCmpSizes.PS.Scl>, XS, VEX_4V, VEX_LIG, VEX_WIG;
let ExeDomain = SSEPackedDouble in
defm VCMPSD : sse12_cmp_scalar<FR64, f64mem, AVXCC, X86cmps, f64, loadf64,
                 "cmp${cc}sd\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                 "cmpsd\t{$cc, $src2, $src1, $dst|$dst, $src1, $src2, $cc}",
                 SchedWriteFCmpSizes.PD.Scl>,
                 XD, VEX_4V, VEX_LIG, VEX_WIG;

let Constraints = "$src1 = $dst" in {
  let ExeDomain = SSEPackedSingle in
  defm CMPSS : sse12_cmp_scalar<FR32, f32mem, SSECC, X86cmps, f32, loadf32,
                  "cmp${cc}ss\t{$src2, $dst|$dst, $src2}",
                  "cmpss\t{$cc, $src2, $dst|$dst, $src2, $cc}",
                  SchedWriteFCmpSizes.PS.Scl>, XS;
  let ExeDomain = SSEPackedDouble in
  defm CMPSD : sse12_cmp_scalar<FR64, f64mem, SSECC, X86cmps, f64, loadf64,
                  "cmp${cc}sd\t{$src2, $dst|$dst, $src2}",
                  "cmpsd\t{$cc, $src2, $dst|$dst, $src2, $cc}",
                  SchedWriteFCmpSizes.PD.Scl>, XD;
}

multiclass sse12_cmp_scalar_int<Operand memop, Operand CC,
                         Intrinsic Int, string asm, X86FoldableSchedWrite sched,
                         ComplexPattern mem_cpat> {
  def rr_Int : SIi8<0xC2, MRMSrcReg, (outs VR128:$dst),
                      (ins VR128:$src1, VR128:$src, CC:$cc), asm,
                        [(set VR128:$dst, (Int VR128:$src1,
                                               VR128:$src, imm:$cc))]>,
           Sched<[sched]>;
let mayLoad = 1 in
  def rm_Int : SIi8<0xC2, MRMSrcMem, (outs VR128:$dst),
                      (ins VR128:$src1, memop:$src, CC:$cc), asm,
                        [(set VR128:$dst, (Int VR128:$src1,
                                               mem_cpat:$src, imm:$cc))]>,
           Sched<[sched.Folded, sched.ReadAfterFold]>;
}

let isCodeGenOnly = 1 in {
  // Aliases to match intrinsics which expect XMM operand(s).
  let ExeDomain = SSEPackedSingle in
  defm VCMPSS  : sse12_cmp_scalar_int<ssmem, AVXCC, int_x86_sse_cmp_ss,
                       "cmp${cc}ss\t{$src, $src1, $dst|$dst, $src1, $src}",
                       SchedWriteFCmpSizes.PS.Scl, sse_load_f32>, XS, VEX_4V;
  let ExeDomain = SSEPackedDouble in
  defm VCMPSD  : sse12_cmp_scalar_int<sdmem, AVXCC, int_x86_sse2_cmp_sd,
                       "cmp${cc}sd\t{$src, $src1, $dst|$dst, $src1, $src}",
                       SchedWriteFCmpSizes.PD.Scl, sse_load_f64>,
                       XD, VEX_4V;
  let Constraints = "$src1 = $dst" in {
    let ExeDomain = SSEPackedSingle in
    defm CMPSS  : sse12_cmp_scalar_int<ssmem, SSECC, int_x86_sse_cmp_ss,
                         "cmp${cc}ss\t{$src, $dst|$dst, $src}",
                         SchedWriteFCmpSizes.PS.Scl, sse_load_f32>, XS;
    let ExeDomain = SSEPackedDouble in
    defm CMPSD  : sse12_cmp_scalar_int<sdmem, SSECC, int_x86_sse2_cmp_sd,
                         "cmp${cc}sd\t{$src, $dst|$dst, $src}",
                         SchedWriteFCmpSizes.PD.Scl, sse_load_f64>, XD;
}
}


// sse12_ord_cmp - Unordered/Ordered scalar fp compare and set EFLAGS
multiclass sse12_ord_cmp<bits<8> opc, RegisterClass RC, SDNode OpNode,
                         ValueType vt, X86MemOperand x86memop,
                         PatFrag ld_frag, string OpcodeStr,
                         X86FoldableSchedWrite sched> {
let hasSideEffects = 0 in {
  def rr: SI<opc, MRMSrcReg, (outs), (ins RC:$src1, RC:$src2),
                     !strconcat(OpcodeStr, "\t{$src2, $src1|$src1, $src2}"),
                     [(set EFLAGS, (OpNode (vt RC:$src1), RC:$src2))]>,
          Sched<[sched]>;
let mayLoad = 1 in
  def rm: SI<opc, MRMSrcMem, (outs), (ins RC:$src1, x86memop:$src2),
                     !strconcat(OpcodeStr, "\t{$src2, $src1|$src1, $src2}"),
                     [(set EFLAGS, (OpNode (vt RC:$src1),
                                           (ld_frag addr:$src2)))]>,
          Sched<[sched.Folded, sched.ReadAfterFold]>;
}
}

// sse12_ord_cmp_int - Intrinsic version of sse12_ord_cmp
multiclass sse12_ord_cmp_int<bits<8> opc, RegisterClass RC, SDNode OpNode,
                             ValueType vt, Operand memop,
                             ComplexPattern mem_cpat, string OpcodeStr,
                             X86FoldableSchedWrite sched> {
  def rr_Int: SI<opc, MRMSrcReg, (outs), (ins RC:$src1, RC:$src2),
                     !strconcat(OpcodeStr, "\t{$src2, $src1|$src1, $src2}"),
                     [(set EFLAGS, (OpNode (vt RC:$src1), RC:$src2))]>,
          Sched<[sched]>;
let mayLoad = 1 in
  def rm_Int: SI<opc, MRMSrcMem, (outs), (ins RC:$src1, memop:$src2),
                     !strconcat(OpcodeStr, "\t{$src2, $src1|$src1, $src2}"),
                     [(set EFLAGS, (OpNode (vt RC:$src1),
                                           mem_cpat:$src2))]>,
          Sched<[sched.Folded, sched.ReadAfterFold]>;
}

let Defs = [EFLAGS] in {
  defm VUCOMISS : sse12_ord_cmp<0x2E, FR32, X86cmp, f32, f32mem, loadf32,
                               "ucomiss", WriteFCom>, PS, VEX, VEX_LIG, VEX_WIG;
  defm VUCOMISD : sse12_ord_cmp<0x2E, FR64, X86cmp, f64, f64mem, loadf64,
                               "ucomisd", WriteFCom>, PD, VEX, VEX_LIG, VEX_WIG;
  let Pattern = []<dag> in {
    defm VCOMISS  : sse12_ord_cmp<0x2F, FR32, undef, f32, f32mem, loadf32,
                                "comiss", WriteFCom>, PS, VEX, VEX_LIG, VEX_WIG;
    defm VCOMISD  : sse12_ord_cmp<0x2F, FR64, undef, f64, f64mem, loadf64,
                                "comisd", WriteFCom>, PD, VEX, VEX_LIG, VEX_WIG;
  }

  let isCodeGenOnly = 1 in {
    defm VUCOMISS  : sse12_ord_cmp_int<0x2E, VR128, X86ucomi, v4f32, ssmem,
                      sse_load_f32, "ucomiss", WriteFCom>, PS, VEX, VEX_WIG;
    defm VUCOMISD  : sse12_ord_cmp_int<0x2E, VR128, X86ucomi, v2f64, sdmem,
                      sse_load_f64, "ucomisd", WriteFCom>, PD, VEX, VEX_WIG;

    defm VCOMISS  : sse12_ord_cmp_int<0x2F, VR128, X86comi, v4f32, ssmem,
                       sse_load_f32, "comiss", WriteFCom>, PS, VEX, VEX_WIG;
    defm VCOMISD  : sse12_ord_cmp_int<0x2F, VR128, X86comi, v2f64, sdmem,
                       sse_load_f64, "comisd", WriteFCom>, PD, VEX, VEX_WIG;
  }
  defm UCOMISS  : sse12_ord_cmp<0x2E, FR32, X86cmp, f32, f32mem, loadf32,
                                  "ucomiss", WriteFCom>, PS;
  defm UCOMISD  : sse12_ord_cmp<0x2E, FR64, X86cmp, f64, f64mem, loadf64,
                                  "ucomisd", WriteFCom>, PD;

  let Pattern = []<dag> in {
    defm COMISS  : sse12_ord_cmp<0x2F, FR32, undef, f32, f32mem, loadf32,
                                    "comiss", WriteFCom>, PS;
    defm COMISD  : sse12_ord_cmp<0x2F, FR64, undef, f64, f64mem, loadf64,
                                    "comisd", WriteFCom>, PD;
  }

  let isCodeGenOnly = 1 in {
    defm UCOMISS  : sse12_ord_cmp_int<0x2E, VR128, X86ucomi, v4f32, ssmem,
                            sse_load_f32, "ucomiss", WriteFCom>, PS;
    defm UCOMISD  : sse12_ord_cmp_int<0x2E, VR128, X86ucomi, v2f64, sdmem,
                            sse_load_f64, "ucomisd", WriteFCom>, PD;

    defm COMISS  : sse12_ord_cmp_int<0x2F, VR128, X86comi, v4f32, ssmem,
                                sse_load_f32, "comiss", WriteFCom>, PS;
    defm COMISD  : sse12_ord_cmp_int<0x2F, VR128, X86comi, v2f64, sdmem,
                                    sse_load_f64, "comisd", WriteFCom>, PD;
  }
} // Defs = [EFLAGS]

// sse12_cmp_packed - sse 1 & 2 compare packed instructions
multiclass sse12_cmp_packed<RegisterClass RC, X86MemOperand x86memop,
                            Operand CC,  ValueType VT, string asm,
                            string asm_alt, X86FoldableSchedWrite sched,
                            Domain d, PatFrag ld_frag> {
  let isCommutable = 1 in
  def rri : PIi8<0xC2, MRMSrcReg,
             (outs RC:$dst), (ins RC:$src1, RC:$src2, CC:$cc), asm,
             [(set RC:$dst, (VT (X86cmpp RC:$src1, RC:$src2, imm:$cc)))], d>,
            Sched<[sched]>;
  def rmi : PIi8<0xC2, MRMSrcMem,
             (outs RC:$dst), (ins RC:$src1, x86memop:$src2, CC:$cc), asm,
             [(set RC:$dst,
               (VT (X86cmpp RC:$src1, (ld_frag addr:$src2), imm:$cc)))], d>,
            Sched<[sched.Folded, sched.ReadAfterFold]>;

  // Accept explicit immediate argument form instead of comparison code.
  let isAsmParserOnly = 1, hasSideEffects = 0 in {
    def rri_alt : PIi8<0xC2, MRMSrcReg,
               (outs RC:$dst), (ins RC:$src1, RC:$src2, u8imm:$cc),
               asm_alt, [], d>, Sched<[sched]>, NotMemoryFoldable;
    let mayLoad = 1 in
    def rmi_alt : PIi8<0xC2, MRMSrcMem,
               (outs RC:$dst), (ins RC:$src1, x86memop:$src2, u8imm:$cc),
               asm_alt, [], d>, Sched<[sched.Folded, sched.ReadAfterFold]>,
               NotMemoryFoldable;
  }
}

defm VCMPPS : sse12_cmp_packed<VR128, f128mem, AVXCC, v4f32,
               "cmp${cc}ps\t{$src2, $src1, $dst|$dst, $src1, $src2}",
               "cmpps\t{$cc, $src2, $src1, $dst|$dst, $src1, $src2, $cc}",
               SchedWriteFCmpSizes.PS.XMM, SSEPackedSingle, loadv4f32>, PS, VEX_4V, VEX_WIG;
defm VCMPPD : sse12_cmp_packed<VR128, f128mem, AVXCC, v2f64,
               "cmp${cc}pd\t{$src2, $src1, $dst|$dst, $src1, $src2}",
               "cmppd\t{$cc, $src2, $src1, $dst|$dst, $src1, $src2, $cc}",
               SchedWriteFCmpSizes.PD.XMM, SSEPackedDouble, loadv2f64>, PD, VEX_4V, VEX_WIG;
defm VCMPPSY : sse12_cmp_packed<VR256, f256mem, AVXCC, v8f32,
               "cmp${cc}ps\t{$src2, $src1, $dst|$dst, $src1, $src2}",
               "cmpps\t{$cc, $src2, $src1, $dst|$dst, $src1, $src2, $cc}",
               SchedWriteFCmpSizes.PS.YMM, SSEPackedSingle, loadv8f32>, PS, VEX_4V, VEX_L, VEX_WIG;
defm VCMPPDY : sse12_cmp_packed<VR256, f256mem, AVXCC, v4f64,
               "cmp${cc}pd\t{$src2, $src1, $dst|$dst, $src1, $src2}",
               "cmppd\t{$cc, $src2, $src1, $dst|$dst, $src1, $src2, $cc}",
               SchedWriteFCmpSizes.PD.YMM, SSEPackedDouble, loadv4f64>, PD, VEX_4V, VEX_L, VEX_WIG;
let Constraints = "$src1 = $dst" in {
  defm CMPPS : sse12_cmp_packed<VR128, f128mem, SSECC, v4f32,
                 "cmp${cc}ps\t{$src2, $dst|$dst, $src2}",
                 "cmpps\t{$cc, $src2, $dst|$dst, $src2, $cc}",
                 SchedWriteFCmpSizes.PS.XMM, SSEPackedSingle, memopv4f32>, PS;
  defm CMPPD : sse12_cmp_packed<VR128, f128mem, SSECC, v2f64,
                 "cmp${cc}pd\t{$src2, $dst|$dst, $src2}",
                 "cmppd\t{$cc, $src2, $dst|$dst, $src2, $cc}",
                 SchedWriteFCmpSizes.PD.XMM, SSEPackedDouble, memopv2f64>, PD;
}

def CommutableCMPCC : PatLeaf<(imm), [{
  uint64_t Imm = N->getZExtValue() & 0x7;
  return (Imm == 0x00 || Imm == 0x03 || Imm == 0x04 || Imm == 0x07);
}]>;

// Patterns to select compares with loads in first operand.
let Predicates = [HasAVX] in {
  def : Pat<(v4f64 (X86cmpp (loadv4f64 addr:$src2), VR256:$src1,
                            CommutableCMPCC:$cc)),
            (VCMPPDYrmi VR256:$src1, addr:$src2, imm:$cc)>;

  def : Pat<(v8f32 (X86cmpp (loadv8f32 addr:$src2), VR256:$src1,
                            CommutableCMPCC:$cc)),
            (VCMPPSYrmi VR256:$src1, addr:$src2, imm:$cc)>;

  def : Pat<(v2f64 (X86cmpp (loadv2f64 addr:$src2), VR128:$src1,
                            CommutableCMPCC:$cc)),
            (VCMPPDrmi VR128:$src1, addr:$src2, imm:$cc)>;

  def : Pat<(v4f32 (X86cmpp (loadv4f32 addr:$src2), VR128:$src1,
                            CommutableCMPCC:$cc)),
            (VCMPPSrmi VR128:$src1, addr:$src2, imm:$cc)>;

  def : Pat<(f64 (X86cmps (loadf64 addr:$src2), FR64:$src1,
                          CommutableCMPCC:$cc)),
            (VCMPSDrm FR64:$src1, addr:$src2, imm:$cc)>;

  def : Pat<(f32 (X86cmps (loadf32 addr:$src2), FR32:$src1,
                          CommutableCMPCC:$cc)),
            (VCMPSSrm FR32:$src1, addr:$src2, imm:$cc)>;
}

let Predicates = [UseSSE2] in {
  def : Pat<(v2f64 (X86cmpp (memopv2f64 addr:$src2), VR128:$src1,
                            CommutableCMPCC:$cc)),
            (CMPPDrmi VR128:$src1, addr:$src2, imm:$cc)>;

  def : Pat<(f64 (X86cmps (loadf64 addr:$src2), FR64:$src1,
                          CommutableCMPCC:$cc)),
            (CMPSDrm FR64:$src1, addr:$src2, imm:$cc)>;
}

let Predicates = [UseSSE1] in {
  def : Pat<(v4f32 (X86cmpp (memopv4f32 addr:$src2), VR128:$src1,
                            CommutableCMPCC:$cc)),
            (CMPPSrmi VR128:$src1, addr:$src2, imm:$cc)>;

  def : Pat<(f32 (X86cmps (loadf32 addr:$src2), FR32:$src1,
                          CommutableCMPCC:$cc)),
            (CMPSSrm FR32:$src1, addr:$src2, imm:$cc)>;
}

//===----------------------------------------------------------------------===//
// SSE 1 & 2 - Shuffle Instructions
//===----------------------------------------------------------------------===//

/// sse12_shuffle - sse 1 & 2 fp shuffle instructions
multiclass sse12_shuffle<RegisterClass RC, X86MemOperand x86memop,
                         ValueType vt, string asm, PatFrag mem_frag,
                         X86FoldableSchedWrite sched, Domain d> {
  def rmi : PIi8<0xC6, MRMSrcMem, (outs RC:$dst),
                   (ins RC:$src1, x86memop:$src2, u8imm:$src3), asm,
                   [(set RC:$dst, (vt (X86Shufp RC:$src1, (mem_frag addr:$src2),
                                       (i8 imm:$src3))))], d>,
            Sched<[sched.Folded, sched.ReadAfterFold]>;
  def rri : PIi8<0xC6, MRMSrcReg, (outs RC:$dst),
                 (ins RC:$src1, RC:$src2, u8imm:$src3), asm,
                 [(set RC:$dst, (vt (X86Shufp RC:$src1, RC:$src2,
                                     (i8 imm:$src3))))], d>,
            Sched<[sched]>;
}

let Predicates = [HasAVX, NoVLX] in {
  defm VSHUFPS  : sse12_shuffle<VR128, f128mem, v4f32,
           "shufps\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}",
           loadv4f32, SchedWriteFShuffle.XMM, SSEPackedSingle>,
           PS, VEX_4V, VEX_WIG;
  defm VSHUFPSY : sse12_shuffle<VR256, f256mem, v8f32,
           "shufps\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}",
           loadv8f32, SchedWriteFShuffle.YMM, SSEPackedSingle>,
           PS, VEX_4V, VEX_L, VEX_WIG;
  defm VSHUFPD  : sse12_shuffle<VR128, f128mem, v2f64,
           "shufpd\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}",
           loadv2f64, SchedWriteFShuffle.XMM, SSEPackedDouble>,
           PD, VEX_4V, VEX_WIG;
  defm VSHUFPDY : sse12_shuffle<VR256, f256mem, v4f64,
           "shufpd\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}",
           loadv4f64, SchedWriteFShuffle.YMM, SSEPackedDouble>,
           PD, VEX_4V, VEX_L, VEX_WIG;
}
let Constraints = "$src1 = $dst" in {
  defm SHUFPS : sse12_shuffle<VR128, f128mem, v4f32,
                    "shufps\t{$src3, $src2, $dst|$dst, $src2, $src3}",
                    memopv4f32, SchedWriteFShuffle.XMM, SSEPackedSingle>, PS;
  defm SHUFPD : sse12_shuffle<VR128, f128mem, v2f64,
                    "shufpd\t{$src3, $src2, $dst|$dst, $src2, $src3}",
                    memopv2f64, SchedWriteFShuffle.XMM, SSEPackedDouble>, PD;
}

//===----------------------------------------------------------------------===//
// SSE 1 & 2 - Unpack FP Instructions
//===----------------------------------------------------------------------===//

/// sse12_unpack_interleave - sse 1 & 2 fp unpack and interleave
multiclass sse12_unpack_interleave<bits<8> opc, SDNode OpNode, ValueType vt,
                                   PatFrag mem_frag, RegisterClass RC,
                                   X86MemOperand x86memop, string asm,
                                   X86FoldableSchedWrite sched, Domain d,
                                   bit IsCommutable = 0> {
    let isCommutable = IsCommutable in
    def rr : PI<opc, MRMSrcReg,
                (outs RC:$dst), (ins RC:$src1, RC:$src2),
                asm, [(set RC:$dst,
                           (vt (OpNode RC:$src1, RC:$src2)))], d>,
                Sched<[sched]>;
    def rm : PI<opc, MRMSrcMem,
                (outs RC:$dst), (ins RC:$src1, x86memop:$src2),
                asm, [(set RC:$dst,
                           (vt (OpNode RC:$src1,
                                       (mem_frag addr:$src2))))], d>,
             Sched<[sched.Folded, sched.ReadAfterFold]>;
}

let Predicates = [HasAVX, NoVLX] in {
defm VUNPCKHPS: sse12_unpack_interleave<0x15, X86Unpckh, v4f32, load,
      VR128, f128mem, "unpckhps\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                     SchedWriteFShuffle.XMM, SSEPackedSingle>, PS, VEX_4V, VEX_WIG;
defm VUNPCKHPD: sse12_unpack_interleave<0x15, X86Unpckh, v2f64, load,
      VR128, f128mem, "unpckhpd\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                     SchedWriteFShuffle.XMM, SSEPackedDouble, 1>, PD, VEX_4V, VEX_WIG;
defm VUNPCKLPS: sse12_unpack_interleave<0x14, X86Unpckl, v4f32, load,
      VR128, f128mem, "unpcklps\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                     SchedWriteFShuffle.XMM, SSEPackedSingle>, PS, VEX_4V, VEX_WIG;
defm VUNPCKLPD: sse12_unpack_interleave<0x14, X86Unpckl, v2f64, load,
      VR128, f128mem, "unpcklpd\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                     SchedWriteFShuffle.XMM, SSEPackedDouble>, PD, VEX_4V, VEX_WIG;

defm VUNPCKHPSY: sse12_unpack_interleave<0x15, X86Unpckh, v8f32, load,
      VR256, f256mem, "unpckhps\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                     SchedWriteFShuffle.YMM, SSEPackedSingle>, PS, VEX_4V, VEX_L, VEX_WIG;
defm VUNPCKHPDY: sse12_unpack_interleave<0x15, X86Unpckh, v4f64, load,
      VR256, f256mem, "unpckhpd\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                     SchedWriteFShuffle.YMM, SSEPackedDouble>, PD, VEX_4V, VEX_L, VEX_WIG;
defm VUNPCKLPSY: sse12_unpack_interleave<0x14, X86Unpckl, v8f32, load,
      VR256, f256mem, "unpcklps\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                     SchedWriteFShuffle.YMM, SSEPackedSingle>, PS, VEX_4V, VEX_L, VEX_WIG;
defm VUNPCKLPDY: sse12_unpack_interleave<0x14, X86Unpckl, v4f64, load,
      VR256, f256mem, "unpcklpd\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                     SchedWriteFShuffle.YMM, SSEPackedDouble>, PD, VEX_4V, VEX_L, VEX_WIG;
}// Predicates = [HasAVX, NoVLX]

let Constraints = "$src1 = $dst" in {
  defm UNPCKHPS: sse12_unpack_interleave<0x15, X86Unpckh, v4f32, memop,
        VR128, f128mem, "unpckhps\t{$src2, $dst|$dst, $src2}",
                       SchedWriteFShuffle.XMM, SSEPackedSingle>, PS;
  defm UNPCKHPD: sse12_unpack_interleave<0x15, X86Unpckh, v2f64, memop,
        VR128, f128mem, "unpckhpd\t{$src2, $dst|$dst, $src2}",
                       SchedWriteFShuffle.XMM, SSEPackedDouble, 1>, PD;
  defm UNPCKLPS: sse12_unpack_interleave<0x14, X86Unpckl, v4f32, memop,
        VR128, f128mem, "unpcklps\t{$src2, $dst|$dst, $src2}",
                       SchedWriteFShuffle.XMM, SSEPackedSingle>, PS;
  defm UNPCKLPD: sse12_unpack_interleave<0x14, X86Unpckl, v2f64, memop,
        VR128, f128mem, "unpcklpd\t{$src2, $dst|$dst, $src2}",
                       SchedWriteFShuffle.XMM, SSEPackedDouble>, PD;
} // Constraints = "$src1 = $dst"

let Predicates = [HasAVX1Only] in {
  def : Pat<(v8i32 (X86Unpckl VR256:$src1, (loadv8i32 addr:$src2))),
            (VUNPCKLPSYrm VR256:$src1, addr:$src2)>;
  def : Pat<(v8i32 (X86Unpckl VR256:$src1, VR256:$src2)),
            (VUNPCKLPSYrr VR256:$src1, VR256:$src2)>;
  def : Pat<(v8i32 (X86Unpckh VR256:$src1, (loadv8i32 addr:$src2))),
            (VUNPCKHPSYrm VR256:$src1, addr:$src2)>;
  def : Pat<(v8i32 (X86Unpckh VR256:$src1, VR256:$src2)),
            (VUNPCKHPSYrr VR256:$src1, VR256:$src2)>;

  def : Pat<(v4i64 (X86Unpckl VR256:$src1, (loadv4i64 addr:$src2))),
            (VUNPCKLPDYrm VR256:$src1, addr:$src2)>;
  def : Pat<(v4i64 (X86Unpckl VR256:$src1, VR256:$src2)),
            (VUNPCKLPDYrr VR256:$src1, VR256:$src2)>;
  def : Pat<(v4i64 (X86Unpckh VR256:$src1, (loadv4i64 addr:$src2))),
            (VUNPCKHPDYrm VR256:$src1, addr:$src2)>;
  def : Pat<(v4i64 (X86Unpckh VR256:$src1, VR256:$src2)),
            (VUNPCKHPDYrr VR256:$src1, VR256:$src2)>;
}

//===----------------------------------------------------------------------===//
// SSE 1 & 2 - Extract Floating-Point Sign mask
//===----------------------------------------------------------------------===//

/// sse12_extr_sign_mask - sse 1 & 2 unpack and interleave
multiclass sse12_extr_sign_mask<RegisterClass RC, ValueType vt,
                                string asm, Domain d> {
  def rr : PI<0x50, MRMSrcReg, (outs GR32orGR64:$dst), (ins RC:$src),
              !strconcat(asm, "\t{$src, $dst|$dst, $src}"),
              [(set GR32orGR64:$dst, (X86movmsk (vt RC:$src)))], d>,
              Sched<[WriteFMOVMSK]>;
}

let Predicates = [HasAVX] in {
  defm VMOVMSKPS : sse12_extr_sign_mask<VR128, v4f32, "movmskps",
                                        SSEPackedSingle>, PS, VEX, VEX_WIG;
  defm VMOVMSKPD : sse12_extr_sign_mask<VR128, v2f64, "movmskpd",
                                        SSEPackedDouble>, PD, VEX, VEX_WIG;
  defm VMOVMSKPSY : sse12_extr_sign_mask<VR256, v8f32, "movmskps",
                                         SSEPackedSingle>, PS, VEX, VEX_L, VEX_WIG;
  defm VMOVMSKPDY : sse12_extr_sign_mask<VR256, v4f64, "movmskpd",
                                         SSEPackedDouble>, PD, VEX, VEX_L, VEX_WIG;

  // Also support integer VTs to avoid a int->fp bitcast in the DAG.
  def : Pat<(X86movmsk (v4i32 VR128:$src)),
            (VMOVMSKPSrr VR128:$src)>;
  def : Pat<(X86movmsk (v2i64 VR128:$src)),
            (VMOVMSKPDrr VR128:$src)>;
  def : Pat<(X86movmsk (v8i32 VR256:$src)),
            (VMOVMSKPSYrr VR256:$src)>;
  def : Pat<(X86movmsk (v4i64 VR256:$src)),
            (VMOVMSKPDYrr VR256:$src)>;
}

defm MOVMSKPS : sse12_extr_sign_mask<VR128, v4f32, "movmskps",
                                     SSEPackedSingle>, PS;
defm MOVMSKPD : sse12_extr_sign_mask<VR128, v2f64, "movmskpd",
                                     SSEPackedDouble>, PD;

let Predicates = [UseSSE2] in {
  // Also support integer VTs to avoid a int->fp bitcast in the DAG.
  def : Pat<(X86movmsk (v4i32 VR128:$src)),
            (MOVMSKPSrr VR128:$src)>;
  def : Pat<(X86movmsk (v2i64 VR128:$src)),
            (MOVMSKPDrr VR128:$src)>;
}

//===---------------------------------------------------------------------===//
// SSE2 - Packed Integer Logical Instructions
//===---------------------------------------------------------------------===//

let ExeDomain = SSEPackedInt in { // SSE integer instructions

/// PDI_binop_rm - Simple SSE2 binary operator.
multiclass PDI_binop_rm<bits<8> opc, string OpcodeStr, SDNode OpNode,
                        ValueType OpVT, RegisterClass RC, PatFrag memop_frag,
                        X86MemOperand x86memop, X86FoldableSchedWrite sched,
                        bit IsCommutable, bit Is2Addr> {
  let isCommutable = IsCommutable in
  def rr : PDI<opc, MRMSrcReg, (outs RC:$dst),
       (ins RC:$src1, RC:$src2),
       !if(Is2Addr,
           !strconcat(OpcodeStr, "\t{$src2, $dst|$dst, $src2}"),
           !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}")),
       [(set RC:$dst, (OpVT (OpNode RC:$src1, RC:$src2)))]>,
       Sched<[sched]>;
  def rm : PDI<opc, MRMSrcMem, (outs RC:$dst),
       (ins RC:$src1, x86memop:$src2),
       !if(Is2Addr,
           !strconcat(OpcodeStr, "\t{$src2, $dst|$dst, $src2}"),
           !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}")),
       [(set RC:$dst, (OpVT (OpNode RC:$src1, (memop_frag addr:$src2))))]>,
       Sched<[sched.Folded, sched.ReadAfterFold]>;
}
} // ExeDomain = SSEPackedInt

multiclass PDI_binop_all<bits<8> opc, string OpcodeStr, SDNode Opcode,
                         ValueType OpVT128, ValueType OpVT256,
                         X86SchedWriteWidths sched, bit IsCommutable,
                         Predicate prd> {
let Predicates = [HasAVX, prd] in
  defm V#NAME : PDI_binop_rm<opc, !strconcat("v", OpcodeStr), Opcode, OpVT128,
                             VR128, load, i128mem, sched.XMM,
                             IsCommutable, 0>, VEX_4V, VEX_WIG;

let Constraints = "$src1 = $dst" in
  defm NAME : PDI_binop_rm<opc, OpcodeStr, Opcode, OpVT128, VR128,
                           memop, i128mem, sched.XMM, IsCommutable, 1>;

let Predicates = [HasAVX2, prd] in
  defm V#NAME#Y : PDI_binop_rm<opc, !strconcat("v", OpcodeStr), Opcode,
                               OpVT256, VR256, load, i256mem, sched.YMM,
                               IsCommutable, 0>, VEX_4V, VEX_L, VEX_WIG;
}

// These are ordered here for pattern ordering requirements with the fp versions

defm PAND  : PDI_binop_all<0xDB, "pand", and, v2i64, v4i64,
                           SchedWriteVecLogic, 1, NoVLX>;
defm POR   : PDI_binop_all<0xEB, "por", or, v2i64, v4i64,
                           SchedWriteVecLogic, 1, NoVLX>;
defm PXOR  : PDI_binop_all<0xEF, "pxor", xor, v2i64, v4i64,
                           SchedWriteVecLogic, 1, NoVLX>;
defm PANDN : PDI_binop_all<0xDF, "pandn", X86andnp, v2i64, v4i64,
                           SchedWriteVecLogic, 0, NoVLX>;

//===----------------------------------------------------------------------===//
// SSE 1 & 2 - Logical Instructions
//===----------------------------------------------------------------------===//

/// sse12_fp_packed_logical - SSE 1 & 2 packed FP logical ops
///
/// There are no patterns here because isel prefers integer versions for SSE2
/// and later. There are SSE1 v4f32 patterns later.
multiclass sse12_fp_packed_logical<bits<8> opc, string OpcodeStr,
                                   SDNode OpNode, X86SchedWriteWidths sched> {
  let Predicates = [HasAVX, NoVLX] in {
  defm V#NAME#PSY : sse12_fp_packed_logical_rm<opc, VR256, SSEPackedSingle,
        !strconcat(OpcodeStr, "ps"), f256mem, sched.YMM,
        [], [], 0>, PS, VEX_4V, VEX_L, VEX_WIG;

  defm V#NAME#PDY : sse12_fp_packed_logical_rm<opc, VR256, SSEPackedDouble,
        !strconcat(OpcodeStr, "pd"), f256mem, sched.YMM,
        [], [], 0>, PD, VEX_4V, VEX_L, VEX_WIG;

  defm V#NAME#PS : sse12_fp_packed_logical_rm<opc, VR128, SSEPackedSingle,
       !strconcat(OpcodeStr, "ps"), f128mem, sched.XMM,
       [], [], 0>, PS, VEX_4V, VEX_WIG;

  defm V#NAME#PD : sse12_fp_packed_logical_rm<opc, VR128, SSEPackedDouble,
       !strconcat(OpcodeStr, "pd"), f128mem, sched.XMM,
       [], [], 0>, PD, VEX_4V, VEX_WIG;
  }

  let Constraints = "$src1 = $dst" in {
    defm PS : sse12_fp_packed_logical_rm<opc, VR128, SSEPackedSingle,
         !strconcat(OpcodeStr, "ps"), f128mem, sched.XMM,
         [], []>, PS;

    defm PD : sse12_fp_packed_logical_rm<opc, VR128, SSEPackedDouble,
         !strconcat(OpcodeStr, "pd"), f128mem, sched.XMM,
         [], []>, PD;
  }
}

defm AND  : sse12_fp_packed_logical<0x54, "and", and, SchedWriteFLogic>;
defm OR   : sse12_fp_packed_logical<0x56, "or", or, SchedWriteFLogic>;
defm XOR  : sse12_fp_packed_logical<0x57, "xor", xor, SchedWriteFLogic>;
let isCommutable = 0 in
  defm ANDN : sse12_fp_packed_logical<0x55, "andn", X86andnp, SchedWriteFLogic>;

let Predicates = [HasAVX2, NoVLX] in {
  def : Pat<(v32i8 (and VR256:$src1, VR256:$src2)),
            (VPANDYrr VR256:$src1, VR256:$src2)>;
  def : Pat<(v16i16 (and VR256:$src1, VR256:$src2)),
            (VPANDYrr VR256:$src1, VR256:$src2)>;
  def : Pat<(v8i32 (and VR256:$src1, VR256:$src2)),
            (VPANDYrr VR256:$src1, VR256:$src2)>;

  def : Pat<(v32i8 (or VR256:$src1, VR256:$src2)),
            (VPORYrr VR256:$src1, VR256:$src2)>;
  def : Pat<(v16i16 (or VR256:$src1, VR256:$src2)),
            (VPORYrr VR256:$src1, VR256:$src2)>;
  def : Pat<(v8i32 (or VR256:$src1, VR256:$src2)),
            (VPORYrr VR256:$src1, VR256:$src2)>;

  def : Pat<(v32i8 (xor VR256:$src1, VR256:$src2)),
            (VPXORYrr VR256:$src1, VR256:$src2)>;
  def : Pat<(v16i16 (xor VR256:$src1, VR256:$src2)),
            (VPXORYrr VR256:$src1, VR256:$src2)>;
  def : Pat<(v8i32 (xor VR256:$src1, VR256:$src2)),
            (VPXORYrr VR256:$src1, VR256:$src2)>;

  def : Pat<(v32i8 (X86andnp VR256:$src1, VR256:$src2)),
            (VPANDNYrr VR256:$src1, VR256:$src2)>;
  def : Pat<(v16i16 (X86andnp VR256:$src1, VR256:$src2)),
            (VPANDNYrr VR256:$src1, VR256:$src2)>;
  def : Pat<(v8i32 (X86andnp VR256:$src1, VR256:$src2)),
            (VPANDNYrr VR256:$src1, VR256:$src2)>;

  def : Pat<(and VR256:$src1, (loadv32i8 addr:$src2)),
            (VPANDYrm VR256:$src1, addr:$src2)>;
  def : Pat<(and VR256:$src1, (loadv16i16 addr:$src2)),
            (VPANDYrm VR256:$src1, addr:$src2)>;
  def : Pat<(and VR256:$src1, (loadv8i32 addr:$src2)),
            (VPANDYrm VR256:$src1, addr:$src2)>;

  def : Pat<(or VR256:$src1, (loadv32i8 addr:$src2)),
            (VPORYrm VR256:$src1, addr:$src2)>;
  def : Pat<(or VR256:$src1, (loadv16i16 addr:$src2)),
            (VPORYrm VR256:$src1, addr:$src2)>;
  def : Pat<(or VR256:$src1, (loadv8i32 addr:$src2)),
            (VPORYrm VR256:$src1, addr:$src2)>;

  def : Pat<(xor VR256:$src1, (loadv32i8 addr:$src2)),
            (VPXORYrm VR256:$src1, addr:$src2)>;
  def : Pat<(xor VR256:$src1, (loadv16i16 addr:$src2)),
            (VPXORYrm VR256:$src1, addr:$src2)>;
  def : Pat<(xor VR256:$src1, (loadv8i32 addr:$src2)),
            (VPXORYrm VR256:$src1, addr:$src2)>;

  def : Pat<(X86andnp VR256:$src1, (loadv32i8 addr:$src2)),
            (VPANDNYrm VR256:$src1, addr:$src2)>;
  def : Pat<(X86andnp VR256:$src1, (loadv16i16 addr:$src2)),
            (VPANDNYrm VR256:$src1, addr:$src2)>;
  def : Pat<(X86andnp VR256:$src1, (loadv8i32 addr:$src2)),
            (VPANDNYrm VR256:$src1, addr:$src2)>;
}

// If only AVX1 is supported, we need to handle integer operations with
// floating point instructions since the integer versions aren't available.
let Predicates = [HasAVX1Only] in {
  def : Pat<(v32i8 (and VR256:$src1, VR256:$src2)),
            (VANDPSYrr VR256:$src1, VR256:$src2)>;
  def : Pat<(v16i16 (and VR256:$src1, VR256:$src2)),
            (VANDPSYrr VR256:$src1, VR256:$src2)>;
  def : Pat<(v8i32 (and VR256:$src1, VR256:$src2)),
            (VANDPSYrr VR256:$src1, VR256:$src2)>;
  def : Pat<(v4i64 (and VR256:$src1, VR256:$src2)),
            (VANDPSYrr VR256:$src1, VR256:$src2)>;

  def : Pat<(v32i8 (or VR256:$src1, VR256:$src2)),
            (VORPSYrr VR256:$src1, VR256:$src2)>;
  def : Pat<(v16i16 (or VR256:$src1, VR256:$src2)),
            (VORPSYrr VR256:$src1, VR256:$src2)>;
  def : Pat<(v8i32 (or VR256:$src1, VR256:$src2)),
            (VORPSYrr VR256:$src1, VR256:$src2)>;
  def : Pat<(v4i64 (or VR256:$src1, VR256:$src2)),
            (VORPSYrr VR256:$src1, VR256:$src2)>;

  def : Pat<(v32i8 (xor VR256:$src1, VR256:$src2)),
            (VXORPSYrr VR256:$src1, VR256:$src2)>;
  def : Pat<(v16i16 (xor VR256:$src1, VR256:$src2)),
            (VXORPSYrr VR256:$src1, VR256:$src2)>;
  def : Pat<(v8i32 (xor VR256:$src1, VR256:$src2)),
            (VXORPSYrr VR256:$src1, VR256:$src2)>;
  def : Pat<(v4i64 (xor VR256:$src1, VR256:$src2)),
            (VXORPSYrr VR256:$src1, VR256:$src2)>;

  def : Pat<(v32i8 (X86andnp VR256:$src1, VR256:$src2)),
            (VANDNPSYrr VR256:$src1, VR256:$src2)>;
  def : Pat<(v16i16 (X86andnp VR256:$src1, VR256:$src2)),
            (VANDNPSYrr VR256:$src1, VR256:$src2)>;
  def : Pat<(v8i32 (X86andnp VR256:$src1, VR256:$src2)),
            (VANDNPSYrr VR256:$src1, VR256:$src2)>;
  def : Pat<(v4i64 (X86andnp VR256:$src1, VR256:$src2)),
            (VANDNPSYrr VR256:$src1, VR256:$src2)>;

  def : Pat<(and VR256:$src1, (loadv32i8 addr:$src2)),
            (VANDPSYrm VR256:$src1, addr:$src2)>;
  def : Pat<(and VR256:$src1, (loadv16i16 addr:$src2)),
            (VANDPSYrm VR256:$src1, addr:$src2)>;
  def : Pat<(and VR256:$src1, (loadv8i32 addr:$src2)),
            (VANDPSYrm VR256:$src1, addr:$src2)>;
  def : Pat<(and VR256:$src1, (loadv4i64 addr:$src2)),
            (VANDPSYrm VR256:$src1, addr:$src2)>;

  def : Pat<(or VR256:$src1, (loadv32i8 addr:$src2)),
            (VORPSYrm VR256:$src1, addr:$src2)>;
  def : Pat<(or VR256:$src1, (loadv16i16 addr:$src2)),
            (VORPSYrm VR256:$src1, addr:$src2)>;
  def : Pat<(or VR256:$src1, (loadv8i32 addr:$src2)),
            (VORPSYrm VR256:$src1, addr:$src2)>;
  def : Pat<(or VR256:$src1, (loadv4i64 addr:$src2)),
            (VORPSYrm VR256:$src1, addr:$src2)>;

  def : Pat<(xor VR256:$src1, (loadv32i8 addr:$src2)),
            (VXORPSYrm VR256:$src1, addr:$src2)>;
  def : Pat<(xor VR256:$src1, (loadv16i16 addr:$src2)),
            (VXORPSYrm VR256:$src1, addr:$src2)>;
  def : Pat<(xor VR256:$src1, (loadv8i32 addr:$src2)),
            (VXORPSYrm VR256:$src1, addr:$src2)>;
  def : Pat<(xor VR256:$src1, (loadv4i64 addr:$src2)),
            (VXORPSYrm VR256:$src1, addr:$src2)>;

  def : Pat<(X86andnp VR256:$src1, (loadv32i8 addr:$src2)),
            (VANDNPSYrm VR256:$src1, addr:$src2)>;
  def : Pat<(X86andnp VR256:$src1, (loadv16i16 addr:$src2)),
            (VANDNPSYrm VR256:$src1, addr:$src2)>;
  def : Pat<(X86andnp VR256:$src1, (loadv8i32 addr:$src2)),
            (VANDNPSYrm VR256:$src1, addr:$src2)>;
  def : Pat<(X86andnp VR256:$src1, (loadv4i64 addr:$src2)),
            (VANDNPSYrm VR256:$src1, addr:$src2)>;
}

let Predicates = [HasAVX, NoVLX_Or_NoDQI] in {
  // Use packed logical operations for scalar ops.
  def : Pat<(f64 (X86fand FR64:$src1, FR64:$src2)),
            (COPY_TO_REGCLASS
             (v2f64 (VANDPDrr (v2f64 (COPY_TO_REGCLASS FR64:$src1, VR128)),
                              (v2f64 (COPY_TO_REGCLASS FR64:$src2, VR128)))),
             FR64)>;
  def : Pat<(f64 (X86for FR64:$src1, FR64:$src2)),
            (COPY_TO_REGCLASS
             (v2f64 (VORPDrr (v2f64 (COPY_TO_REGCLASS FR64:$src1, VR128)),
                             (v2f64 (COPY_TO_REGCLASS FR64:$src2, VR128)))),
             FR64)>;
  def : Pat<(f64 (X86fxor FR64:$src1, FR64:$src2)),
            (COPY_TO_REGCLASS
             (v2f64 (VXORPDrr (v2f64 (COPY_TO_REGCLASS FR64:$src1, VR128)),
                              (v2f64 (COPY_TO_REGCLASS FR64:$src2, VR128)))),
             FR64)>;
  def : Pat<(f64 (X86fandn FR64:$src1, FR64:$src2)),
            (COPY_TO_REGCLASS
             (v2f64 (VANDNPDrr (v2f64 (COPY_TO_REGCLASS FR64:$src1, VR128)),
                               (v2f64 (COPY_TO_REGCLASS FR64:$src2, VR128)))),
             FR64)>;

  def : Pat<(f32 (X86fand FR32:$src1, FR32:$src2)),
            (COPY_TO_REGCLASS
             (v4f32 (VANDPSrr (v4f32 (COPY_TO_REGCLASS FR32:$src1, VR128)),
                              (v4f32 (COPY_TO_REGCLASS FR32:$src2, VR128)))),
             FR32)>;
  def : Pat<(f32 (X86for FR32:$src1, FR32:$src2)),
            (COPY_TO_REGCLASS
             (v4f32 (VORPSrr (v4f32 (COPY_TO_REGCLASS FR32:$src1, VR128)),
                             (v4f32 (COPY_TO_REGCLASS FR32:$src2, VR128)))),
             FR32)>;
  def : Pat<(f32 (X86fxor FR32:$src1, FR32:$src2)),
            (COPY_TO_REGCLASS
             (v4f32 (VXORPSrr (v4f32 (COPY_TO_REGCLASS FR32:$src1, VR128)),
                              (v4f32 (COPY_TO_REGCLASS FR32:$src2, VR128)))),
             FR32)>;
  def : Pat<(f32 (X86fandn FR32:$src1, FR32:$src2)),
            (COPY_TO_REGCLASS
             (v4f32 (VANDNPSrr (v4f32 (COPY_TO_REGCLASS FR32:$src1, VR128)),
                               (v4f32 (COPY_TO_REGCLASS FR32:$src2, VR128)))),
             FR32)>;
}

let Predicates = [UseSSE1] in {
  // Use packed logical operations for scalar ops.
  def : Pat<(f32 (X86fand FR32:$src1, FR32:$src2)),
            (COPY_TO_REGCLASS
             (v4f32 (ANDPSrr (v4f32 (COPY_TO_REGCLASS FR32:$src1, VR128)),
                             (v4f32 (COPY_TO_REGCLASS FR32:$src2, VR128)))),
             FR32)>;
  def : Pat<(f32 (X86for FR32:$src1, FR32:$src2)),
            (COPY_TO_REGCLASS
             (v4f32 (ORPSrr (v4f32 (COPY_TO_REGCLASS FR32:$src1, VR128)),
                            (v4f32 (COPY_TO_REGCLASS FR32:$src2, VR128)))),
             FR32)>;
  def : Pat<(f32 (X86fxor FR32:$src1, FR32:$src2)),
            (COPY_TO_REGCLASS
             (v4f32 (XORPSrr (v4f32 (COPY_TO_REGCLASS FR32:$src1, VR128)),
                             (v4f32 (COPY_TO_REGCLASS FR32:$src2, VR128)))),
             FR32)>;
  def : Pat<(f32 (X86fandn FR32:$src1, FR32:$src2)),
            (COPY_TO_REGCLASS
             (v4f32 (ANDNPSrr (v4f32 (COPY_TO_REGCLASS FR32:$src1, VR128)),
                              (v4f32 (COPY_TO_REGCLASS FR32:$src2, VR128)))),
             FR32)>;
}

let Predicates = [UseSSE2] in {
  // Use packed logical operations for scalar ops.
  def : Pat<(f64 (X86fand FR64:$src1, FR64:$src2)),
            (COPY_TO_REGCLASS
             (v2f64 (ANDPDrr (v2f64 (COPY_TO_REGCLASS FR64:$src1, VR128)),
                             (v2f64 (COPY_TO_REGCLASS FR64:$src2, VR128)))),
             FR64)>;
  def : Pat<(f64 (X86for FR64:$src1, FR64:$src2)),
            (COPY_TO_REGCLASS
             (v2f64 (ORPDrr (v2f64 (COPY_TO_REGCLASS FR64:$src1, VR128)),
                            (v2f64 (COPY_TO_REGCLASS FR64:$src2, VR128)))),
             FR64)>;
  def : Pat<(f64 (X86fxor FR64:$src1, FR64:$src2)),
            (COPY_TO_REGCLASS
             (v2f64 (XORPDrr (v2f64 (COPY_TO_REGCLASS FR64:$src1, VR128)),
                             (v2f64 (COPY_TO_REGCLASS FR64:$src2, VR128)))),
             FR64)>;
  def : Pat<(f64 (X86fandn FR64:$src1, FR64:$src2)),
            (COPY_TO_REGCLASS
             (v2f64 (ANDNPDrr (v2f64 (COPY_TO_REGCLASS FR64:$src1, VR128)),
                              (v2f64 (COPY_TO_REGCLASS FR64:$src2, VR128)))),
             FR64)>;
}

let Predicates = [HasAVX, NoVLX] in {
  def : Pat<(v16i8 (and VR128:$src1, VR128:$src2)),
            (VPANDrr VR128:$src1, VR128:$src2)>;
  def : Pat<(v8i16 (and VR128:$src1, VR128:$src2)),
            (VPANDrr VR128:$src1, VR128:$src2)>;
  def : Pat<(v4i32 (and VR128:$src1, VR128:$src2)),
            (VPANDrr VR128:$src1, VR128:$src2)>;

  def : Pat<(v16i8 (or VR128:$src1, VR128:$src2)),
            (VPORrr VR128:$src1, VR128:$src2)>;
  def : Pat<(v8i16 (or VR128:$src1, VR128:$src2)),
            (VPORrr VR128:$src1, VR128:$src2)>;
  def : Pat<(v4i32 (or VR128:$src1, VR128:$src2)),
            (VPORrr VR128:$src1, VR128:$src2)>;

  def : Pat<(v16i8 (xor VR128:$src1, VR128:$src2)),
            (VPXORrr VR128:$src1, VR128:$src2)>;
  def : Pat<(v8i16 (xor VR128:$src1, VR128:$src2)),
            (VPXORrr VR128:$src1, VR128:$src2)>;
  def : Pat<(v4i32 (xor VR128:$src1, VR128:$src2)),
            (VPXORrr VR128:$src1, VR128:$src2)>;

  def : Pat<(v16i8 (X86andnp VR128:$src1, VR128:$src2)),
            (VPANDNrr VR128:$src1, VR128:$src2)>;
  def : Pat<(v8i16 (X86andnp VR128:$src1, VR128:$src2)),
            (VPANDNrr VR128:$src1, VR128:$src2)>;
  def : Pat<(v4i32 (X86andnp VR128:$src1, VR128:$src2)),
            (VPANDNrr VR128:$src1, VR128:$src2)>;

  def : Pat<(and VR128:$src1, (loadv16i8 addr:$src2)),
            (VPANDrm VR128:$src1, addr:$src2)>;
  def : Pat<(and VR128:$src1, (loadv8i16 addr:$src2)),
            (VPANDrm VR128:$src1, addr:$src2)>;
  def : Pat<(and VR128:$src1, (loadv4i32 addr:$src2)),
            (VPANDrm VR128:$src1, addr:$src2)>;

  def : Pat<(or VR128:$src1, (loadv16i8 addr:$src2)),
            (VPORrm VR128:$src1, addr:$src2)>;
  def : Pat<(or VR128:$src1, (loadv8i16 addr:$src2)),
            (VPORrm VR128:$src1, addr:$src2)>;
  def : Pat<(or VR128:$src1, (loadv4i32 addr:$src2)),
            (VPORrm VR128:$src1, addr:$src2)>;

  def : Pat<(xor VR128:$src1, (loadv16i8 addr:$src2)),
            (VPXORrm VR128:$src1, addr:$src2)>;
  def : Pat<(xor VR128:$src1, (loadv8i16 addr:$src2)),
            (VPXORrm VR128:$src1, addr:$src2)>;
  def : Pat<(xor VR128:$src1, (loadv4i32 addr:$src2)),
            (VPXORrm VR128:$src1, addr:$src2)>;

  def : Pat<(X86andnp VR128:$src1, (loadv16i8 addr:$src2)),
            (VPANDNrm VR128:$src1, addr:$src2)>;
  def : Pat<(X86andnp VR128:$src1, (loadv8i16 addr:$src2)),
            (VPANDNrm VR128:$src1, addr:$src2)>;
  def : Pat<(X86andnp VR128:$src1, (loadv4i32 addr:$src2)),
            (VPANDNrm VR128:$src1, addr:$src2)>;
}

let Predicates = [UseSSE2] in {
  def : Pat<(v16i8 (and VR128:$src1, VR128:$src2)),
            (PANDrr VR128:$src1, VR128:$src2)>;
  def : Pat<(v8i16 (and VR128:$src1, VR128:$src2)),
            (PANDrr VR128:$src1, VR128:$src2)>;
  def : Pat<(v4i32 (and VR128:$src1, VR128:$src2)),
            (PANDrr VR128:$src1, VR128:$src2)>;

  def : Pat<(v16i8 (or VR128:$src1, VR128:$src2)),
            (PORrr VR128:$src1, VR128:$src2)>;
  def : Pat<(v8i16 (or VR128:$src1, VR128:$src2)),
            (PORrr VR128:$src1, VR128:$src2)>;
  def : Pat<(v4i32 (or VR128:$src1, VR128:$src2)),
            (PORrr VR128:$src1, VR128:$src2)>;

  def : Pat<(v16i8 (xor VR128:$src1, VR128:$src2)),
            (PXORrr VR128:$src1, VR128:$src2)>;
  def : Pat<(v8i16 (xor VR128:$src1, VR128:$src2)),
            (PXORrr VR128:$src1, VR128:$src2)>;
  def : Pat<(v4i32 (xor VR128:$src1, VR128:$src2)),
            (PXORrr VR128:$src1, VR128:$src2)>;

  def : Pat<(v16i8 (X86andnp VR128:$src1, VR128:$src2)),
            (PANDNrr VR128:$src1, VR128:$src2)>;
  def : Pat<(v8i16 (X86andnp VR128:$src1, VR128:$src2)),
            (PANDNrr VR128:$src1, VR128:$src2)>;
  def : Pat<(v4i32 (X86andnp VR128:$src1, VR128:$src2)),
            (PANDNrr VR128:$src1, VR128:$src2)>;

  def : Pat<(and VR128:$src1, (memopv16i8 addr:$src2)),
            (PANDrm VR128:$src1, addr:$src2)>;
  def : Pat<(and VR128:$src1, (memopv8i16 addr:$src2)),
            (PANDrm VR128:$src1, addr:$src2)>;
  def : Pat<(and VR128:$src1, (memopv4i32 addr:$src2)),
            (PANDrm VR128:$src1, addr:$src2)>;

  def : Pat<(or VR128:$src1, (memopv16i8 addr:$src2)),
            (PORrm VR128:$src1, addr:$src2)>;
  def : Pat<(or VR128:$src1, (memopv8i16 addr:$src2)),
            (PORrm VR128:$src1, addr:$src2)>;
  def : Pat<(or VR128:$src1, (memopv4i32 addr:$src2)),
            (PORrm VR128:$src1, addr:$src2)>;

  def : Pat<(xor VR128:$src1, (memopv16i8 addr:$src2)),
            (PXORrm VR128:$src1, addr:$src2)>;
  def : Pat<(xor VR128:$src1, (memopv8i16 addr:$src2)),
            (PXORrm VR128:$src1, addr:$src2)>;
  def : Pat<(xor VR128:$src1, (memopv4i32 addr:$src2)),
            (PXORrm VR128:$src1, addr:$src2)>;

  def : Pat<(X86andnp VR128:$src1, (memopv16i8 addr:$src2)),
            (PANDNrm VR128:$src1, addr:$src2)>;
  def : Pat<(X86andnp VR128:$src1, (memopv8i16 addr:$src2)),
            (PANDNrm VR128:$src1, addr:$src2)>;
  def : Pat<(X86andnp VR128:$src1, (memopv4i32 addr:$src2)),
            (PANDNrm VR128:$src1, addr:$src2)>;
}

// Patterns for packed operations when we don't have integer type available.
def : Pat<(v4f32 (X86fand VR128:$src1, VR128:$src2)),
          (ANDPSrr VR128:$src1, VR128:$src2)>;
def : Pat<(v4f32 (X86for VR128:$src1, VR128:$src2)),
          (ORPSrr VR128:$src1, VR128:$src2)>;
def : Pat<(v4f32 (X86fxor VR128:$src1, VR128:$src2)),
          (XORPSrr VR128:$src1, VR128:$src2)>;
def : Pat<(v4f32 (X86fandn VR128:$src1, VR128:$src2)),
          (ANDNPSrr VR128:$src1, VR128:$src2)>;

def : Pat<(X86fand VR128:$src1, (memopv4f32 addr:$src2)),
          (ANDPSrm VR128:$src1, addr:$src2)>;
def : Pat<(X86for VR128:$src1, (memopv4f32 addr:$src2)),
          (ORPSrm VR128:$src1, addr:$src2)>;
def : Pat<(X86fxor VR128:$src1, (memopv4f32 addr:$src2)),
          (XORPSrm VR128:$src1, addr:$src2)>;
def : Pat<(X86fandn VR128:$src1, (memopv4f32 addr:$src2)),
          (ANDNPSrm VR128:$src1, addr:$src2)>;

//===----------------------------------------------------------------------===//
// SSE 1 & 2 - Arithmetic Instructions
//===----------------------------------------------------------------------===//

/// basic_sse12_fp_binop_xxx - SSE 1 & 2 binops come in both scalar and
/// vector forms.
///
/// In addition, we also have a special variant of the scalar form here to
/// represent the associated intrinsic operation.  This form is unlike the
/// plain scalar form, in that it takes an entire vector (instead of a scalar)
/// and leaves the top elements unmodified (therefore these cannot be commuted).
///
/// These three forms can each be reg+reg or reg+mem.
///

/// FIXME: once all 256-bit intrinsics are matched, cleanup and refactor those
/// classes below
multiclass basic_sse12_fp_binop_p<bits<8> opc, string OpcodeStr,
                                  SDNode OpNode, X86SchedWriteSizes sched> {
  let Predicates = [HasAVX, NoVLX] in {
  defm V#NAME#PS : sse12_fp_packed<opc, !strconcat(OpcodeStr, "ps"), OpNode,
                               VR128, v4f32, f128mem, loadv4f32,
                               SSEPackedSingle, sched.PS.XMM, 0>, PS, VEX_4V, VEX_WIG;
  defm V#NAME#PD : sse12_fp_packed<opc, !strconcat(OpcodeStr, "pd"), OpNode,
                               VR128, v2f64, f128mem, loadv2f64,
                               SSEPackedDouble, sched.PD.XMM, 0>, PD, VEX_4V, VEX_WIG;

  defm V#NAME#PSY : sse12_fp_packed<opc, !strconcat(OpcodeStr, "ps"),
                        OpNode, VR256, v8f32, f256mem, loadv8f32,
                        SSEPackedSingle, sched.PS.YMM, 0>, PS, VEX_4V, VEX_L, VEX_WIG;
  defm V#NAME#PDY : sse12_fp_packed<opc, !strconcat(OpcodeStr, "pd"),
                        OpNode, VR256, v4f64, f256mem, loadv4f64,
                        SSEPackedDouble, sched.PD.YMM, 0>, PD, VEX_4V, VEX_L, VEX_WIG;
  }

  let Constraints = "$src1 = $dst" in {
    defm PS : sse12_fp_packed<opc, !strconcat(OpcodeStr, "ps"), OpNode, VR128,
                              v4f32, f128mem, memopv4f32, SSEPackedSingle,
                              sched.PS.XMM>, PS;
    defm PD : sse12_fp_packed<opc, !strconcat(OpcodeStr, "pd"), OpNode, VR128,
                              v2f64, f128mem, memopv2f64, SSEPackedDouble,
                              sched.PD.XMM>, PD;
  }
}

multiclass basic_sse12_fp_binop_s<bits<8> opc, string OpcodeStr, SDNode OpNode,
                                  X86SchedWriteSizes sched> {
  defm V#NAME#SS : sse12_fp_scalar<opc, !strconcat(OpcodeStr, "ss"),
                         OpNode, FR32, f32mem, SSEPackedSingle, sched.PS.Scl, 0>,
                         XS, VEX_4V, VEX_LIG, VEX_WIG;
  defm V#NAME#SD : sse12_fp_scalar<opc, !strconcat(OpcodeStr, "sd"),
                         OpNode, FR64, f64mem, SSEPackedDouble, sched.PD.Scl, 0>,
                         XD, VEX_4V, VEX_LIG, VEX_WIG;

  let Constraints = "$src1 = $dst" in {
    defm SS : sse12_fp_scalar<opc, !strconcat(OpcodeStr, "ss"),
                              OpNode, FR32, f32mem, SSEPackedSingle,
                              sched.PS.Scl>, XS;
    defm SD : sse12_fp_scalar<opc, !strconcat(OpcodeStr, "sd"),
                              OpNode, FR64, f64mem, SSEPackedDouble,
                              sched.PD.Scl>, XD;
  }
}

multiclass basic_sse12_fp_binop_s_int<bits<8> opc, string OpcodeStr,
                                      SDPatternOperator OpNode,
                                      X86SchedWriteSizes sched> {
  defm V#NAME#SS : sse12_fp_scalar_int<opc, OpcodeStr, OpNode, VR128, v4f32,
                   !strconcat(OpcodeStr, "ss"), ssmem, sse_load_f32,
                   SSEPackedSingle, sched.PS.Scl, 0>, XS, VEX_4V, VEX_LIG, VEX_WIG;
  defm V#NAME#SD : sse12_fp_scalar_int<opc, OpcodeStr, OpNode, VR128, v2f64,
                   !strconcat(OpcodeStr, "sd"), sdmem, sse_load_f64,
                   SSEPackedDouble, sched.PD.Scl, 0>, XD, VEX_4V, VEX_LIG, VEX_WIG;

  let Constraints = "$src1 = $dst" in {
    defm SS : sse12_fp_scalar_int<opc, OpcodeStr, OpNode, VR128, v4f32,
                   !strconcat(OpcodeStr, "ss"), ssmem, sse_load_f32,
                   SSEPackedSingle, sched.PS.Scl>, XS;
    defm SD : sse12_fp_scalar_int<opc, OpcodeStr, OpNode, VR128, v2f64,
                   !strconcat(OpcodeStr, "sd"), sdmem, sse_load_f64,
                   SSEPackedDouble, sched.PD.Scl>, XD;
  }
}

// Binary Arithmetic instructions
defm ADD : basic_sse12_fp_binop_p<0x58, "add", fadd, SchedWriteFAddSizes>,
           basic_sse12_fp_binop_s<0x58, "add", fadd, SchedWriteFAddSizes>,
           basic_sse12_fp_binop_s_int<0x58, "add", null_frag, SchedWriteFAddSizes>;
defm MUL : basic_sse12_fp_binop_p<0x59, "mul", fmul, SchedWriteFMulSizes>,
           basic_sse12_fp_binop_s<0x59, "mul", fmul, SchedWriteFMulSizes>,
           basic_sse12_fp_binop_s_int<0x59, "mul", null_frag, SchedWriteFMulSizes>;
let isCommutable = 0 in {
  defm SUB : basic_sse12_fp_binop_p<0x5C, "sub", fsub, SchedWriteFAddSizes>,
             basic_sse12_fp_binop_s<0x5C, "sub", fsub, SchedWriteFAddSizes>,
             basic_sse12_fp_binop_s_int<0x5C, "sub", null_frag, SchedWriteFAddSizes>;
  defm DIV : basic_sse12_fp_binop_p<0x5E, "div", fdiv, SchedWriteFDivSizes>,
             basic_sse12_fp_binop_s<0x5E, "div", fdiv, SchedWriteFDivSizes>,
             basic_sse12_fp_binop_s_int<0x5E, "div", null_frag, SchedWriteFDivSizes>;
  defm MAX : basic_sse12_fp_binop_p<0x5F, "max", X86fmax, SchedWriteFCmpSizes>,
             basic_sse12_fp_binop_s<0x5F, "max", X86fmax, SchedWriteFCmpSizes>,
             basic_sse12_fp_binop_s_int<0x5F, "max", X86fmaxs, SchedWriteFCmpSizes>;
  defm MIN : basic_sse12_fp_binop_p<0x5D, "min", X86fmin, SchedWriteFCmpSizes>,
             basic_sse12_fp_binop_s<0x5D, "min", X86fmin, SchedWriteFCmpSizes>,
             basic_sse12_fp_binop_s_int<0x5D, "min", X86fmins, SchedWriteFCmpSizes>;
}

let isCodeGenOnly = 1 in {
  defm MAXC: basic_sse12_fp_binop_p<0x5F, "max", X86fmaxc, SchedWriteFCmpSizes>,
             basic_sse12_fp_binop_s<0x5F, "max", X86fmaxc, SchedWriteFCmpSizes>;
  defm MINC: basic_sse12_fp_binop_p<0x5D, "min", X86fminc, SchedWriteFCmpSizes>,
             basic_sse12_fp_binop_s<0x5D, "min", X86fminc, SchedWriteFCmpSizes>;
}

// Patterns used to select SSE scalar fp arithmetic instructions from
// either:
//
// (1) a scalar fp operation followed by a blend
//
// The effect is that the backend no longer emits unnecessary vector
// insert instructions immediately after SSE scalar fp instructions
// like addss or mulss.
//
// For example, given the following code:
//   __m128 foo(__m128 A, __m128 B) {
//     A[0] += B[0];
//     return A;
//   }
//
// Previously we generated:
//   addss %xmm0, %xmm1
//   movss %xmm1, %xmm0
//
// We now generate:
//   addss %xmm1, %xmm0
//
// (2) a vector packed single/double fp operation followed by a vector insert
//
// The effect is that the backend converts the packed fp instruction
// followed by a vector insert into a single SSE scalar fp instruction.
//
// For example, given the following code:
//   __m128 foo(__m128 A, __m128 B) {
//     __m128 C = A + B;
//     return (__m128) {c[0], a[1], a[2], a[3]};
//   }
//
// Previously we generated:
//   addps %xmm0, %xmm1
//   movss %xmm1, %xmm0
//
// We now generate:
//   addss %xmm1, %xmm0

// TODO: Some canonicalization in lowering would simplify the number of
// patterns we have to try to match.
multiclass scalar_math_patterns<SDNode Op, string OpcPrefix, SDNode Move,
                                    ValueType VT, ValueType EltTy,
                                    RegisterClass RC, Predicate BasePredicate> {
  let Predicates = [BasePredicate] in {
    // extracted scalar math op with insert via movss/movsd
    def : Pat<(VT (Move (VT VR128:$dst),
                        (VT (scalar_to_vector
                             (Op (EltTy (extractelt (VT VR128:$dst), (iPTR 0))),
                                 RC:$src))))),
              (!cast<Instruction>(OpcPrefix#rr_Int) VT:$dst,
               (VT (COPY_TO_REGCLASS RC:$src, VR128)))>;
  }

  // Repeat for AVX versions of the instructions.
  let Predicates = [UseAVX] in {
    // extracted scalar math op with insert via movss/movsd
    def : Pat<(VT (Move (VT VR128:$dst),
                        (VT (scalar_to_vector
                             (Op (EltTy (extractelt (VT VR128:$dst), (iPTR 0))),
                                 RC:$src))))),
              (!cast<Instruction>("V"#OpcPrefix#rr_Int) VT:$dst,
               (VT (COPY_TO_REGCLASS RC:$src, VR128)))>;
  }
}

defm : scalar_math_patterns<fadd, "ADDSS", X86Movss, v4f32, f32, FR32, UseSSE1>;
defm : scalar_math_patterns<fsub, "SUBSS", X86Movss, v4f32, f32, FR32, UseSSE1>;
defm : scalar_math_patterns<fmul, "MULSS", X86Movss, v4f32, f32, FR32, UseSSE1>;
defm : scalar_math_patterns<fdiv, "DIVSS", X86Movss, v4f32, f32, FR32, UseSSE1>;

defm : scalar_math_patterns<fadd, "ADDSD", X86Movsd, v2f64, f64, FR64, UseSSE2>;
defm : scalar_math_patterns<fsub, "SUBSD", X86Movsd, v2f64, f64, FR64, UseSSE2>;
defm : scalar_math_patterns<fmul, "MULSD", X86Movsd, v2f64, f64, FR64, UseSSE2>;
defm : scalar_math_patterns<fdiv, "DIVSD", X86Movsd, v2f64, f64, FR64, UseSSE2>;
 
/// Unop Arithmetic
/// In addition, we also have a special variant of the scalar form here to
/// represent the associated intrinsic operation.  This form is unlike the
/// plain scalar form, in that it takes an entire vector (instead of a
/// scalar) and leaves the top elements undefined.
///
/// And, we have a special variant form for a full-vector intrinsic form.

/// sse_fp_unop_s - SSE1 unops in scalar form
/// For the non-AVX defs, we need $src1 to be tied to $dst because
/// the HW instructions are 2 operand / destructive.
multiclass sse_fp_unop_s<bits<8> opc, string OpcodeStr, RegisterClass RC,
                          ValueType ScalarVT, X86MemOperand x86memop,
                          Operand intmemop, SDNode OpNode, Domain d,
                          X86FoldableSchedWrite sched, Predicate target> {
  let hasSideEffects = 0 in {
  def r : I<opc, MRMSrcReg, (outs RC:$dst), (ins RC:$src1),
              !strconcat(OpcodeStr, "\t{$src1, $dst|$dst, $src1}"),
            [(set RC:$dst, (OpNode RC:$src1))], d>, Sched<[sched]>,
            Requires<[target]>;
  let mayLoad = 1 in
  def m : I<opc, MRMSrcMem, (outs RC:$dst), (ins x86memop:$src1),
            !strconcat(OpcodeStr, "\t{$src1, $dst|$dst, $src1}"),
            [(set RC:$dst, (OpNode (load addr:$src1)))], d>,
            Sched<[sched.Folded]>,
            Requires<[target, OptForSize]>;

  let isCodeGenOnly = 1, Constraints = "$src1 = $dst", ExeDomain = d in {
  def r_Int : I<opc, MRMSrcReg, (outs VR128:$dst), (ins VR128:$src1, VR128:$src2),
                !strconcat(OpcodeStr, "\t{$src2, $dst|$dst, $src2}"), []>,
                Sched<[sched]>;
  let mayLoad = 1 in
  def m_Int : I<opc, MRMSrcMem, (outs VR128:$dst), (ins VR128:$src1, intmemop:$src2),
                !strconcat(OpcodeStr, "\t{$src2, $dst|$dst, $src2}"), []>,
                Sched<[sched.Folded, sched.ReadAfterFold]>;
  }
  }

}

multiclass sse_fp_unop_s_intr<RegisterClass RC, ValueType vt,
                              ComplexPattern int_cpat, Intrinsic Intr,
                              Predicate target, string Suffix> {
  let Predicates = [target] in {
  // These are unary operations, but they are modeled as having 2 source operands
  // because the high elements of the destination are unchanged in SSE.
  def : Pat<(Intr VR128:$src),
            (!cast<Instruction>(NAME#r_Int) VR128:$src, VR128:$src)>;
  }
  // We don't want to fold scalar loads into these instructions unless
  // optimizing for size. This is because the folded instruction will have a
  // partial register update, while the unfolded sequence will not, e.g.
  // movss mem, %xmm0
  // rcpss %xmm0, %xmm0
  // which has a clobber before the rcp, vs.
  // rcpss mem, %xmm0
  let Predicates = [target, OptForSize] in {
    def : Pat<(Intr int_cpat:$src2),
               (!cast<Instruction>(NAME#m_Int)
                      (vt (IMPLICIT_DEF)), addr:$src2)>;
  }
}

multiclass avx_fp_unop_s_intr<RegisterClass RC, ValueType vt, ComplexPattern int_cpat,
                              Intrinsic Intr, Predicate target> {
  let Predicates = [target] in {
   def : Pat<(Intr VR128:$src),
             (!cast<Instruction>(NAME#r_Int) VR128:$src,
                                 VR128:$src)>;
  }
  let Predicates = [target, OptForSize] in {
    def : Pat<(Intr int_cpat:$src2),
              (!cast<Instruction>(NAME#m_Int)
                    (vt (IMPLICIT_DEF)), addr:$src2)>;
  }
}

multiclass avx_fp_unop_s<bits<8> opc, string OpcodeStr, RegisterClass RC,
                          ValueType ScalarVT, X86MemOperand x86memop,
                          Operand intmemop, SDNode OpNode, Domain d,
                          X86FoldableSchedWrite sched, Predicate target> {
  let hasSideEffects = 0 in {
  def r : I<opc, MRMSrcReg, (outs RC:$dst), (ins RC:$src1, RC:$src2),
            !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
            [], d>, Sched<[sched]>;
  let mayLoad = 1 in
  def m : I<opc, MRMSrcMem, (outs RC:$dst), (ins RC:$src1, x86memop:$src2),
             !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
            [], d>, Sched<[sched.Folded, sched.ReadAfterFold]>;
  let isCodeGenOnly = 1, ExeDomain = d in {
  def r_Int : I<opc, MRMSrcReg, (outs VR128:$dst),
                (ins VR128:$src1, VR128:$src2),
             !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
             []>, Sched<[sched]>;
  let mayLoad = 1 in
  def m_Int : I<opc, MRMSrcMem, (outs VR128:$dst),
                (ins VR128:$src1, intmemop:$src2),
             !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
             []>, Sched<[sched.Folded, sched.ReadAfterFold]>;
  }
  }

  // We don't want to fold scalar loads into these instructions unless
  // optimizing for size. This is because the folded instruction will have a
  // partial register update, while the unfolded sequence will not, e.g.
  // vmovss mem, %xmm0
  // vrcpss %xmm0, %xmm0, %xmm0
  // which has a clobber before the rcp, vs.
  // vrcpss mem, %xmm0, %xmm0
  // TODO: In theory, we could fold the load, and avoid the stall caused by
  // the partial register store, either in BreakFalseDeps or with smarter RA.
  let Predicates = [target] in {
   def : Pat<(OpNode RC:$src),  (!cast<Instruction>(NAME#r)
                                (ScalarVT (IMPLICIT_DEF)), RC:$src)>;
  }
  let Predicates = [target, OptForSize] in {
    def : Pat<(ScalarVT (OpNode (load addr:$src))),
              (!cast<Instruction>(NAME#m) (ScalarVT (IMPLICIT_DEF)),
            addr:$src)>;
  }
}

/// sse1_fp_unop_p - SSE1 unops in packed form.
multiclass sse1_fp_unop_p<bits<8> opc, string OpcodeStr, SDNode OpNode,
                          X86SchedWriteWidths sched, list<Predicate> prds> {
let Predicates = prds in {
  def V#NAME#PSr : PSI<opc, MRMSrcReg, (outs VR128:$dst), (ins VR128:$src),
                       !strconcat("v", OpcodeStr,
                                  "ps\t{$src, $dst|$dst, $src}"),
                       [(set VR128:$dst, (v4f32 (OpNode VR128:$src)))]>,
                       VEX, Sched<[sched.XMM]>, VEX_WIG;
  def V#NAME#PSm : PSI<opc, MRMSrcMem, (outs VR128:$dst), (ins f128mem:$src),
                       !strconcat("v", OpcodeStr,
                                  "ps\t{$src, $dst|$dst, $src}"),
                       [(set VR128:$dst, (OpNode (loadv4f32 addr:$src)))]>,
                       VEX, Sched<[sched.XMM.Folded]>, VEX_WIG;
  def V#NAME#PSYr : PSI<opc, MRMSrcReg, (outs VR256:$dst), (ins VR256:$src),
                        !strconcat("v", OpcodeStr,
                                   "ps\t{$src, $dst|$dst, $src}"),
                        [(set VR256:$dst, (v8f32 (OpNode VR256:$src)))]>,
                        VEX, VEX_L, Sched<[sched.YMM]>, VEX_WIG;
  def V#NAME#PSYm : PSI<opc, MRMSrcMem, (outs VR256:$dst), (ins f256mem:$src),
                        !strconcat("v", OpcodeStr,
                                   "ps\t{$src, $dst|$dst, $src}"),
                        [(set VR256:$dst, (OpNode (loadv8f32 addr:$src)))]>,
                        VEX, VEX_L, Sched<[sched.YMM.Folded]>, VEX_WIG;
}

  def PSr : PSI<opc, MRMSrcReg, (outs VR128:$dst), (ins VR128:$src),
                !strconcat(OpcodeStr, "ps\t{$src, $dst|$dst, $src}"),
                [(set VR128:$dst, (v4f32 (OpNode VR128:$src)))]>,
                Sched<[sched.XMM]>;
  def PSm : PSI<opc, MRMSrcMem, (outs VR128:$dst), (ins f128mem:$src),
                !strconcat(OpcodeStr, "ps\t{$src, $dst|$dst, $src}"),
                [(set VR128:$dst, (OpNode (memopv4f32 addr:$src)))]>,
                Sched<[sched.XMM.Folded]>;
}

/// sse2_fp_unop_p - SSE2 unops in vector forms.
multiclass sse2_fp_unop_p<bits<8> opc, string OpcodeStr,
                          SDNode OpNode, X86SchedWriteWidths sched> {
let Predicates = [HasAVX, NoVLX] in {
  def V#NAME#PDr : PDI<opc, MRMSrcReg, (outs VR128:$dst), (ins VR128:$src),
                       !strconcat("v", OpcodeStr,
                                  "pd\t{$src, $dst|$dst, $src}"),
                       [(set VR128:$dst, (v2f64 (OpNode VR128:$src)))]>,
                       VEX, Sched<[sched.XMM]>, VEX_WIG;
  def V#NAME#PDm : PDI<opc, MRMSrcMem, (outs VR128:$dst), (ins f128mem:$src),
                       !strconcat("v", OpcodeStr,
                                  "pd\t{$src, $dst|$dst, $src}"),
                       [(set VR128:$dst, (OpNode (loadv2f64 addr:$src)))]>,
                       VEX, Sched<[sched.XMM.Folded]>, VEX_WIG;
  def V#NAME#PDYr : PDI<opc, MRMSrcReg, (outs VR256:$dst), (ins VR256:$src),
                        !strconcat("v", OpcodeStr,
                                   "pd\t{$src, $dst|$dst, $src}"),
                        [(set VR256:$dst, (v4f64 (OpNode VR256:$src)))]>,
                        VEX, VEX_L, Sched<[sched.YMM]>, VEX_WIG;
  def V#NAME#PDYm : PDI<opc, MRMSrcMem, (outs VR256:$dst), (ins f256mem:$src),
                        !strconcat("v", OpcodeStr,
                                   "pd\t{$src, $dst|$dst, $src}"),
                        [(set VR256:$dst, (OpNode (loadv4f64 addr:$src)))]>,
                        VEX, VEX_L, Sched<[sched.YMM.Folded]>, VEX_WIG;
}

  def PDr : PDI<opc, MRMSrcReg, (outs VR128:$dst), (ins VR128:$src),
                !strconcat(OpcodeStr, "pd\t{$src, $dst|$dst, $src}"),
                [(set VR128:$dst, (v2f64 (OpNode VR128:$src)))]>,
                Sched<[sched.XMM]>;
  def PDm : PDI<opc, MRMSrcMem, (outs VR128:$dst), (ins f128mem:$src),
                !strconcat(OpcodeStr, "pd\t{$src, $dst|$dst, $src}"),
                [(set VR128:$dst, (OpNode (memopv2f64 addr:$src)))]>,
                Sched<[sched.XMM.Folded]>;
}

multiclass sse1_fp_unop_s_intr<bits<8> opc, string OpcodeStr, SDNode OpNode,
                          X86SchedWriteWidths sched, Predicate AVXTarget> {
  defm SS        :  sse_fp_unop_s_intr<FR32, v4f32, sse_load_f32,
                      !cast<Intrinsic>("int_x86_sse_"##OpcodeStr##_ss),
                      UseSSE1, "SS">, XS;
  defm V#NAME#SS  : avx_fp_unop_s_intr<FR32, v4f32, sse_load_f32,
                      !cast<Intrinsic>("int_x86_sse_"##OpcodeStr##_ss),
                      AVXTarget>,
                      XS, VEX_4V, VEX_LIG, VEX_WIG, NotMemoryFoldable;
}

multiclass sse1_fp_unop_s<bits<8> opc, string OpcodeStr, SDNode OpNode,
                          X86SchedWriteWidths sched, Predicate AVXTarget> {
  defm SS        :  sse_fp_unop_s<opc, OpcodeStr##ss, FR32, f32, f32mem,
                      ssmem, OpNode, SSEPackedSingle, sched.Scl, UseSSE1>, XS;
  defm V#NAME#SS  : avx_fp_unop_s<opc, "v"#OpcodeStr##ss, FR32, f32,
                      f32mem, ssmem, OpNode, SSEPackedSingle, sched.Scl, AVXTarget>,
                       XS, VEX_4V, VEX_LIG, VEX_WIG;
}

multiclass sse2_fp_unop_s<bits<8> opc, string OpcodeStr, SDNode OpNode,
                          X86SchedWriteWidths sched, Predicate AVXTarget> {
  defm SD         : sse_fp_unop_s<opc, OpcodeStr##sd, FR64, f64, f64mem,
                         sdmem, OpNode, SSEPackedDouble, sched.Scl, UseSSE2>, XD;
  defm V#NAME#SD  : avx_fp_unop_s<opc, "v"#OpcodeStr##sd, FR64, f64,
                         f64mem, sdmem, OpNode, SSEPackedDouble, sched.Scl, AVXTarget>,
                         XD, VEX_4V, VEX_LIG, VEX_WIG;
}

// Square root.
defm SQRT  : sse1_fp_unop_s<0x51, "sqrt", fsqrt, SchedWriteFSqrt, UseAVX>,
             sse1_fp_unop_p<0x51, "sqrt", fsqrt, SchedWriteFSqrt, [HasAVX, NoVLX]>,
             sse2_fp_unop_s<0x51, "sqrt", fsqrt, SchedWriteFSqrt64, UseAVX>,
             sse2_fp_unop_p<0x51, "sqrt", fsqrt, SchedWriteFSqrt64>;

// Reciprocal approximations. Note that these typically require refinement
// in order to obtain suitable precision.
defm RSQRT : sse1_fp_unop_s<0x52, "rsqrt", X86frsqrt, SchedWriteFRsqrt, HasAVX>,
             sse1_fp_unop_s_intr<0x52, "rsqrt", X86frsqrt, SchedWriteFRsqrt, HasAVX>,
             sse1_fp_unop_p<0x52, "rsqrt", X86frsqrt, SchedWriteFRsqrt, [HasAVX]>;
defm RCP   : sse1_fp_unop_s<0x53, "rcp", X86frcp, SchedWriteFRcp, HasAVX>,
             sse1_fp_unop_s_intr<0x53, "rcp", X86frcp, SchedWriteFRcp, HasAVX>,
             sse1_fp_unop_p<0x53, "rcp", X86frcp, SchedWriteFRcp, [HasAVX]>;

// There is no f64 version of the reciprocal approximation instructions.

multiclass scalar_unary_math_patterns<SDNode OpNode, string OpcPrefix, SDNode Move,
                                      ValueType VT, Predicate BasePredicate> {
  let Predicates = [BasePredicate] in {
    def : Pat<(VT (Move VT:$dst, (scalar_to_vector
                                  (OpNode (extractelt VT:$src, 0))))),
              (!cast<Instruction>(OpcPrefix#r_Int) VT:$dst, VT:$src)>;
  }

  // Repeat for AVX versions of the instructions.
  let Predicates = [UseAVX] in {
    def : Pat<(VT (Move VT:$dst, (scalar_to_vector
                                  (OpNode (extractelt VT:$src, 0))))),
              (!cast<Instruction>("V"#OpcPrefix#r_Int) VT:$dst, VT:$src)>;
  }
}

multiclass scalar_unary_math_imm_patterns<SDNode OpNode, string OpcPrefix, SDNode Move,
                                          ValueType VT, bits<8> ImmV,
                                          Predicate BasePredicate> {
  let Predicates = [BasePredicate] in {
    def : Pat<(VT (Move VT:$dst, (scalar_to_vector
                                  (OpNode (extractelt VT:$src, 0))))),
              (!cast<Instruction>(OpcPrefix#r_Int) VT:$dst, VT:$src, (i32 ImmV))>;
  }

  // Repeat for AVX versions of the instructions.
  let Predicates = [UseAVX] in {
    def : Pat<(VT (Move VT:$dst, (scalar_to_vector
                                  (OpNode (extractelt VT:$src, 0))))),
              (!cast<Instruction>("V"#OpcPrefix#r_Int) VT:$dst, VT:$src, (i32 ImmV))>;
  }
}

defm : scalar_unary_math_patterns<fsqrt, "SQRTSS", X86Movss, v4f32, UseSSE1>;
defm : scalar_unary_math_patterns<fsqrt, "SQRTSD", X86Movsd, v2f64, UseSSE2>;

multiclass scalar_unary_math_intr_patterns<Intrinsic Intr, string OpcPrefix,
                                           SDNode Move, ValueType VT,
                                           Predicate BasePredicate> {
  let Predicates = [BasePredicate] in {
    def : Pat<(VT (Move VT:$dst, (Intr VT:$src))),
              (!cast<Instruction>(OpcPrefix#r_Int) VT:$dst, VT:$src)>;
  }

  // Repeat for AVX versions of the instructions.
  let Predicates = [HasAVX] in {
    def : Pat<(VT (Move VT:$dst, (Intr VT:$src))),
              (!cast<Instruction>("V"#OpcPrefix#r_Int) VT:$dst, VT:$src)>;
  }
}

defm : scalar_unary_math_intr_patterns<int_x86_sse_rcp_ss, "RCPSS", X86Movss,
                                       v4f32, UseSSE1>;
defm : scalar_unary_math_intr_patterns<int_x86_sse_rsqrt_ss, "RSQRTSS", X86Movss,
                                       v4f32, UseSSE1>;


//===----------------------------------------------------------------------===//
// SSE 1 & 2 - Non-temporal stores
//===----------------------------------------------------------------------===//

let AddedComplexity = 400 in { // Prefer non-temporal versions
let Predicates = [HasAVX, NoVLX] in {
let SchedRW = [SchedWriteFMoveLSNT.XMM.MR] in {
def VMOVNTPSmr : VPSI<0x2B, MRMDestMem, (outs),
                     (ins f128mem:$dst, VR128:$src),
                     "movntps\t{$src, $dst|$dst, $src}",
                     [(alignednontemporalstore (v4f32 VR128:$src),
                                               addr:$dst)]>, VEX, VEX_WIG;
def VMOVNTPDmr : VPDI<0x2B, MRMDestMem, (outs),
                     (ins f128mem:$dst, VR128:$src),
                     "movntpd\t{$src, $dst|$dst, $src}",
                     [(alignednontemporalstore (v2f64 VR128:$src),
                                               addr:$dst)]>, VEX, VEX_WIG;
} // SchedRW

let SchedRW = [SchedWriteFMoveLSNT.YMM.MR] in {
def VMOVNTPSYmr : VPSI<0x2B, MRMDestMem, (outs),
                     (ins f256mem:$dst, VR256:$src),
                     "movntps\t{$src, $dst|$dst, $src}",
                     [(alignednontemporalstore (v8f32 VR256:$src),
                                               addr:$dst)]>, VEX, VEX_L, VEX_WIG;
def VMOVNTPDYmr : VPDI<0x2B, MRMDestMem, (outs),
                     (ins f256mem:$dst, VR256:$src),
                     "movntpd\t{$src, $dst|$dst, $src}",
                     [(alignednontemporalstore (v4f64 VR256:$src),
                                               addr:$dst)]>, VEX, VEX_L, VEX_WIG;
} // SchedRW

let ExeDomain = SSEPackedInt in {
def VMOVNTDQmr    : VPDI<0xE7, MRMDestMem, (outs),
                         (ins i128mem:$dst, VR128:$src),
                         "movntdq\t{$src, $dst|$dst, $src}",
                         [(alignednontemporalstore (v2i64 VR128:$src),
                                                   addr:$dst)]>, VEX, VEX_WIG,
                         Sched<[SchedWriteVecMoveLSNT.XMM.MR]>;
def VMOVNTDQYmr : VPDI<0xE7, MRMDestMem, (outs),
                    (ins i256mem:$dst, VR256:$src),
                    "movntdq\t{$src, $dst|$dst, $src}",
                    [(alignednontemporalstore (v4i64 VR256:$src),
                                              addr:$dst)]>, VEX, VEX_L, VEX_WIG,
                    Sched<[SchedWriteVecMoveLSNT.YMM.MR]>;
} // ExeDomain
} // Predicates

let SchedRW = [SchedWriteFMoveLSNT.XMM.MR] in {
def MOVNTPSmr : PSI<0x2B, MRMDestMem, (outs), (ins f128mem:$dst, VR128:$src),
                    "movntps\t{$src, $dst|$dst, $src}",
                    [(alignednontemporalstore (v4f32 VR128:$src), addr:$dst)]>;
def MOVNTPDmr : PDI<0x2B, MRMDestMem, (outs), (ins f128mem:$dst, VR128:$src),
                    "movntpd\t{$src, $dst|$dst, $src}",
                    [(alignednontemporalstore(v2f64 VR128:$src), addr:$dst)]>;
} // SchedRW

let ExeDomain = SSEPackedInt, SchedRW = [SchedWriteVecMoveLSNT.XMM.MR] in
def MOVNTDQmr : PDI<0xE7, MRMDestMem, (outs), (ins f128mem:$dst, VR128:$src),
                    "movntdq\t{$src, $dst|$dst, $src}",
                    [(alignednontemporalstore (v2i64 VR128:$src), addr:$dst)]>;

let SchedRW = [WriteStoreNT] in {
// There is no AVX form for instructions below this point
def MOVNTImr : I<0xC3, MRMDestMem, (outs), (ins i32mem:$dst, GR32:$src),
                 "movnti{l}\t{$src, $dst|$dst, $src}",
                 [(nontemporalstore (i32 GR32:$src), addr:$dst)]>,
               PS, Requires<[HasSSE2]>;
def MOVNTI_64mr : RI<0xC3, MRMDestMem, (outs), (ins i64mem:$dst, GR64:$src),
                     "movnti{q}\t{$src, $dst|$dst, $src}",
                     [(nontemporalstore (i64 GR64:$src), addr:$dst)]>,
                  PS, Requires<[HasSSE2]>;
} // SchedRW = [WriteStoreNT]

let Predicates = [HasAVX, NoVLX] in {
  def : Pat<(alignednontemporalstore (v8i32 VR256:$src), addr:$dst),
            (VMOVNTDQYmr addr:$dst, VR256:$src)>;
  def : Pat<(alignednontemporalstore (v16i16 VR256:$src), addr:$dst),
            (VMOVNTDQYmr addr:$dst, VR256:$src)>;
  def : Pat<(alignednontemporalstore (v32i8 VR256:$src), addr:$dst),
            (VMOVNTDQYmr addr:$dst, VR256:$src)>;

  def : Pat<(alignednontemporalstore (v4i32 VR128:$src), addr:$dst),
            (VMOVNTDQmr addr:$dst, VR128:$src)>;
  def : Pat<(alignednontemporalstore (v8i16 VR128:$src), addr:$dst),
            (VMOVNTDQmr addr:$dst, VR128:$src)>;
  def : Pat<(alignednontemporalstore (v16i8 VR128:$src), addr:$dst),
            (VMOVNTDQmr addr:$dst, VR128:$src)>;
}

let Predicates = [UseSSE2] in {
  def : Pat<(alignednontemporalstore (v4i32 VR128:$src), addr:$dst),
            (MOVNTDQmr addr:$dst, VR128:$src)>;
  def : Pat<(alignednontemporalstore (v8i16 VR128:$src), addr:$dst),
            (MOVNTDQmr addr:$dst, VR128:$src)>;
  def : Pat<(alignednontemporalstore (v16i8 VR128:$src), addr:$dst),
            (MOVNTDQmr addr:$dst, VR128:$src)>;
}

} // AddedComplexity

//===----------------------------------------------------------------------===//
// SSE 1 & 2 - Prefetch and memory fence
//===----------------------------------------------------------------------===//

// Prefetch intrinsic.
let Predicates = [HasSSEPrefetch], SchedRW = [WriteLoad] in {
def PREFETCHT0   : I<0x18, MRM1m, (outs), (ins i8mem:$src),
    "prefetcht0\t$src", [(prefetch addr:$src, imm, (i32 3), (i32 1))]>, TB;
def PREFETCHT1   : I<0x18, MRM2m, (outs), (ins i8mem:$src),
    "prefetcht1\t$src", [(prefetch addr:$src, imm, (i32 2), (i32 1))]>, TB;
def PREFETCHT2   : I<0x18, MRM3m, (outs), (ins i8mem:$src),
    "prefetcht2\t$src", [(prefetch addr:$src, imm, (i32 1), (i32 1))]>, TB;
def PREFETCHNTA  : I<0x18, MRM0m, (outs), (ins i8mem:$src),
    "prefetchnta\t$src", [(prefetch addr:$src, imm, (i32 0), (i32 1))]>, TB;
}

// FIXME: How should flush instruction be modeled?
let SchedRW = [WriteLoad] in {
// Flush cache
def CLFLUSH : I<0xAE, MRM7m, (outs), (ins i8mem:$src),
               "clflush\t$src", [(int_x86_sse2_clflush addr:$src)]>,
               PS, Requires<[HasSSE2]>;
}

let SchedRW = [WriteNop] in {
// Pause. This "instruction" is encoded as "rep; nop", so even though it
// was introduced with SSE2, it's backward compatible.
def PAUSE : I<0x90, RawFrm, (outs), (ins),
              "pause", [(int_x86_sse2_pause)]>, OBXS;
}

let SchedRW = [WriteFence] in {
// Load, store, and memory fence
// TODO: As with mfence, we may want to ease the availablity of sfence/lfence
// to include any 64-bit target.
def SFENCE : I<0xAE, MRM_F8, (outs), (ins), "sfence", [(int_x86_sse_sfence)]>,
               PS, Requires<[HasSSE1]>;
def LFENCE : I<0xAE, MRM_E8, (outs), (ins), "lfence", [(int_x86_sse2_lfence)]>,
               PS, Requires<[HasSSE2]>;
def MFENCE : I<0xAE, MRM_F0, (outs), (ins), "mfence", [(int_x86_sse2_mfence)]>,
               PS, Requires<[HasMFence]>;
} // SchedRW

def : Pat<(X86MFence), (MFENCE)>;

//===----------------------------------------------------------------------===//
// SSE 1 & 2 - Load/Store XCSR register
//===----------------------------------------------------------------------===//

def VLDMXCSR : VPSI<0xAE, MRM2m, (outs), (ins i32mem:$src),
               "ldmxcsr\t$src", [(int_x86_sse_ldmxcsr addr:$src)]>,
               VEX, Sched<[WriteLDMXCSR]>, VEX_WIG;
def VSTMXCSR : VPSI<0xAE, MRM3m, (outs), (ins i32mem:$dst),
               "stmxcsr\t$dst", [(int_x86_sse_stmxcsr addr:$dst)]>,
               VEX, Sched<[WriteSTMXCSR]>, VEX_WIG;

def LDMXCSR : I<0xAE, MRM2m, (outs), (ins i32mem:$src),
              "ldmxcsr\t$src", [(int_x86_sse_ldmxcsr addr:$src)]>,
              TB, Sched<[WriteLDMXCSR]>;
def STMXCSR : I<0xAE, MRM3m, (outs), (ins i32mem:$dst),
              "stmxcsr\t$dst", [(int_x86_sse_stmxcsr addr:$dst)]>,
              TB, Sched<[WriteSTMXCSR]>;

//===---------------------------------------------------------------------===//
// SSE2 - Move Aligned/Unaligned Packed Integer Instructions
//===---------------------------------------------------------------------===//

let ExeDomain = SSEPackedInt in { // SSE integer instructions

let hasSideEffects = 0 in {
def VMOVDQArr  : VPDI<0x6F, MRMSrcReg, (outs VR128:$dst), (ins VR128:$src),
                      "movdqa\t{$src, $dst|$dst, $src}", []>,
                      Sched<[SchedWriteVecMoveLS.XMM.RR]>, VEX, VEX_WIG;
def VMOVDQUrr  : VSSI<0x6F, MRMSrcReg, (outs VR128:$dst), (ins VR128:$src),
                      "movdqu\t{$src, $dst|$dst, $src}", []>,
                      Sched<[SchedWriteVecMoveLS.XMM.RR]>, VEX, VEX_WIG;
def VMOVDQAYrr : VPDI<0x6F, MRMSrcReg, (outs VR256:$dst), (ins VR256:$src),
                      "movdqa\t{$src, $dst|$dst, $src}", []>,
                      Sched<[SchedWriteVecMoveLS.YMM.RR]>, VEX, VEX_L, VEX_WIG;
def VMOVDQUYrr : VSSI<0x6F, MRMSrcReg, (outs VR256:$dst), (ins VR256:$src),
                      "movdqu\t{$src, $dst|$dst, $src}", []>,
                      Sched<[SchedWriteVecMoveLS.YMM.RR]>, VEX, VEX_L, VEX_WIG;
}

// For Disassembler
let isCodeGenOnly = 1, ForceDisassemble = 1, hasSideEffects = 0 in {
def VMOVDQArr_REV  : VPDI<0x7F, MRMDestReg, (outs VR128:$dst), (ins VR128:$src),
                          "movdqa\t{$src, $dst|$dst, $src}", []>,
                          Sched<[SchedWriteVecMoveLS.XMM.RR]>,
                          VEX, VEX_WIG, FoldGenData<"VMOVDQArr">;
def VMOVDQAYrr_REV : VPDI<0x7F, MRMDestReg, (outs VR256:$dst), (ins VR256:$src),
                          "movdqa\t{$src, $dst|$dst, $src}", []>,
                          Sched<[SchedWriteVecMoveLS.YMM.RR]>,
                          VEX, VEX_L, VEX_WIG, FoldGenData<"VMOVDQAYrr">;
def VMOVDQUrr_REV  : VSSI<0x7F, MRMDestReg, (outs VR128:$dst), (ins VR128:$src),
                          "movdqu\t{$src, $dst|$dst, $src}", []>,
                          Sched<[SchedWriteVecMoveLS.XMM.RR]>,
                          VEX, VEX_WIG, FoldGenData<"VMOVDQUrr">;
def VMOVDQUYrr_REV : VSSI<0x7F, MRMDestReg, (outs VR256:$dst), (ins VR256:$src),
                          "movdqu\t{$src, $dst|$dst, $src}", []>,
                          Sched<[SchedWriteVecMoveLS.YMM.RR]>,
                          VEX, VEX_L, VEX_WIG, FoldGenData<"VMOVDQUYrr">;
}

let canFoldAsLoad = 1, mayLoad = 1, isReMaterializable = 1,
    hasSideEffects = 0, Predicates = [HasAVX,NoVLX] in {
def VMOVDQArm  : VPDI<0x6F, MRMSrcMem, (outs VR128:$dst), (ins i128mem:$src),
                      "movdqa\t{$src, $dst|$dst, $src}",
                      [(set VR128:$dst, (alignedloadv2i64 addr:$src))]>,
                      Sched<[SchedWriteVecMoveLS.XMM.RM]>, VEX, VEX_WIG;
def VMOVDQAYrm : VPDI<0x6F, MRMSrcMem, (outs VR256:$dst), (ins i256mem:$src),
                      "movdqa\t{$src, $dst|$dst, $src}", []>,
                      Sched<[SchedWriteVecMoveLS.YMM.RM]>,
                      VEX, VEX_L, VEX_WIG;
def VMOVDQUrm  : I<0x6F, MRMSrcMem, (outs VR128:$dst), (ins i128mem:$src),
                   "vmovdqu\t{$src, $dst|$dst, $src}",
                   [(set VR128:$dst, (loadv2i64 addr:$src))]>,
                   Sched<[SchedWriteVecMoveLS.XMM.RM]>,
                   XS, VEX, VEX_WIG;
def VMOVDQUYrm : I<0x6F, MRMSrcMem, (outs VR256:$dst), (ins i256mem:$src),
                   "vmovdqu\t{$src, $dst|$dst, $src}", []>,
                   Sched<[SchedWriteVecMoveLS.YMM.RM]>,
                   XS, VEX, VEX_L, VEX_WIG;
}

let mayStore = 1, hasSideEffects = 0, Predicates = [HasAVX,NoVLX] in {
def VMOVDQAmr  : VPDI<0x7F, MRMDestMem, (outs),
                      (ins i128mem:$dst, VR128:$src),
                      "movdqa\t{$src, $dst|$dst, $src}",
                      [(alignedstore (v2i64 VR128:$src), addr:$dst)]>,
                      Sched<[SchedWriteVecMoveLS.XMM.MR]>, VEX, VEX_WIG;
def VMOVDQAYmr : VPDI<0x7F, MRMDestMem, (outs),
                      (ins i256mem:$dst, VR256:$src),
                      "movdqa\t{$src, $dst|$dst, $src}", []>,
                     Sched<[SchedWriteVecMoveLS.YMM.MR]>, VEX, VEX_L, VEX_WIG;
def VMOVDQUmr  : I<0x7F, MRMDestMem, (outs), (ins i128mem:$dst, VR128:$src),
                   "vmovdqu\t{$src, $dst|$dst, $src}",
                   [(store (v2i64 VR128:$src), addr:$dst)]>,
                   Sched<[SchedWriteVecMoveLS.XMM.MR]>, XS, VEX, VEX_WIG;
def VMOVDQUYmr : I<0x7F, MRMDestMem, (outs), (ins i256mem:$dst, VR256:$src),
                   "vmovdqu\t{$src, $dst|$dst, $src}",[]>,
                   Sched<[SchedWriteVecMoveLS.YMM.MR]>, XS, VEX, VEX_L, VEX_WIG;
}

let SchedRW = [SchedWriteVecMoveLS.XMM.RR] in {
let hasSideEffects = 0 in {
def MOVDQArr : PDI<0x6F, MRMSrcReg, (outs VR128:$dst), (ins VR128:$src),
                   "movdqa\t{$src, $dst|$dst, $src}", []>;

def MOVDQUrr :   I<0x6F, MRMSrcReg, (outs VR128:$dst), (ins VR128:$src),
                   "movdqu\t{$src, $dst|$dst, $src}", []>,
                   XS, Requires<[UseSSE2]>;
}

// For Disassembler
let isCodeGenOnly = 1, ForceDisassemble = 1, hasSideEffects = 0 in {
def MOVDQArr_REV : PDI<0x7F, MRMDestReg, (outs VR128:$dst), (ins VR128:$src),
                       "movdqa\t{$src, $dst|$dst, $src}", []>,
                       FoldGenData<"MOVDQArr">;

def MOVDQUrr_REV :   I<0x7F, MRMDestReg, (outs VR128:$dst), (ins VR128:$src),
                       "movdqu\t{$src, $dst|$dst, $src}", []>,
                       XS, Requires<[UseSSE2]>, FoldGenData<"MOVDQUrr">;
}
} // SchedRW

let canFoldAsLoad = 1, mayLoad = 1, isReMaterializable = 1,
    hasSideEffects = 0, SchedRW = [SchedWriteVecMoveLS.XMM.RM] in {
def MOVDQArm : PDI<0x6F, MRMSrcMem, (outs VR128:$dst), (ins i128mem:$src),
                   "movdqa\t{$src, $dst|$dst, $src}",
                   [/*(set VR128:$dst, (alignedloadv2i64 addr:$src))*/]>;
def MOVDQUrm :   I<0x6F, MRMSrcMem, (outs VR128:$dst), (ins i128mem:$src),
                   "movdqu\t{$src, $dst|$dst, $src}",
                   [/*(set VR128:$dst, (loadv2i64 addr:$src))*/]>,
                 XS, Requires<[UseSSE2]>;
}

let mayStore = 1, hasSideEffects = 0,
    SchedRW = [SchedWriteVecMoveLS.XMM.MR] in {
def MOVDQAmr : PDI<0x7F, MRMDestMem, (outs), (ins i128mem:$dst, VR128:$src),
                   "movdqa\t{$src, $dst|$dst, $src}",
                   [/*(alignedstore (v2i64 VR128:$src), addr:$dst)*/]>;
def MOVDQUmr :   I<0x7F, MRMDestMem, (outs), (ins i128mem:$dst, VR128:$src),
                   "movdqu\t{$src, $dst|$dst, $src}",
                   [/*(store (v2i64 VR128:$src), addr:$dst)*/]>,
                 XS, Requires<[UseSSE2]>;
}

} // ExeDomain = SSEPackedInt

// Aliases to help the assembler pick two byte VEX encodings by swapping the
// operands relative to the normal instructions to use VEX.R instead of VEX.B.
def : InstAlias<"vmovdqa\t{$src, $dst|$dst, $src}",
                (VMOVDQArr_REV VR128L:$dst, VR128H:$src), 0>;
def : InstAlias<"vmovdqa\t{$src, $dst|$dst, $src}",
                (VMOVDQAYrr_REV VR256L:$dst, VR256H:$src), 0>;
def : InstAlias<"vmovdqu\t{$src, $dst|$dst, $src}",
                (VMOVDQUrr_REV VR128L:$dst, VR128H:$src), 0>;
def : InstAlias<"vmovdqu\t{$src, $dst|$dst, $src}",
                (VMOVDQUYrr_REV VR256L:$dst, VR256H:$src), 0>;

// Reversed version with ".s" suffix for GAS compatibility.
def : InstAlias<"vmovdqa.s\t{$src, $dst|$dst, $src}",
                (VMOVDQArr_REV VR128:$dst, VR128:$src), 0>;
def : InstAlias<"vmovdqa.s\t{$src, $dst|$dst, $src}",
                (VMOVDQAYrr_REV VR256:$dst, VR256:$src), 0>;
def : InstAlias<"vmovdqu.s\t{$src, $dst|$dst, $src}",
                (VMOVDQUrr_REV VR128:$dst, VR128:$src), 0>;
def : InstAlias<"vmovdqu.s\t{$src, $dst|$dst, $src}",
                (VMOVDQUYrr_REV VR256:$dst, VR256:$src), 0>;

// Reversed version with ".s" suffix for GAS compatibility.
def : InstAlias<"movdqa.s\t{$src, $dst|$dst, $src}",
                (MOVDQArr_REV VR128:$dst, VR128:$src), 0>;
def : InstAlias<"movdqu.s\t{$src, $dst|$dst, $src}",
                (MOVDQUrr_REV VR128:$dst, VR128:$src), 0>;

let Predicates = [HasAVX, NoVLX] in {
  // Additional patterns for other integer sizes.
  def : Pat<(alignedloadv4i32 addr:$src),
            (VMOVDQArm addr:$src)>;
  def : Pat<(alignedloadv8i16 addr:$src),
            (VMOVDQArm addr:$src)>;
  def : Pat<(alignedloadv16i8 addr:$src),
            (VMOVDQArm addr:$src)>;
  def : Pat<(loadv4i32 addr:$src),
            (VMOVDQUrm addr:$src)>;
  def : Pat<(loadv8i16 addr:$src),
            (VMOVDQUrm addr:$src)>;
  def : Pat<(loadv16i8 addr:$src),
            (VMOVDQUrm addr:$src)>;

  def : Pat<(alignedstore (v4i32 VR128:$src), addr:$dst),
            (VMOVDQAmr addr:$dst, VR128:$src)>;
  def : Pat<(alignedstore (v8i16 VR128:$src), addr:$dst),
            (VMOVDQAmr addr:$dst, VR128:$src)>;
  def : Pat<(alignedstore (v16i8 VR128:$src), addr:$dst),
            (VMOVDQAmr addr:$dst, VR128:$src)>;
  def : Pat<(store (v4i32 VR128:$src), addr:$dst),
            (VMOVDQUmr addr:$dst, VR128:$src)>;
  def : Pat<(store (v8i16 VR128:$src), addr:$dst),
            (VMOVDQUmr addr:$dst, VR128:$src)>;
  def : Pat<(store (v16i8 VR128:$src), addr:$dst),
            (VMOVDQUmr addr:$dst, VR128:$src)>;
}

//===---------------------------------------------------------------------===//
// SSE2 - Packed Integer Arithmetic Instructions
//===---------------------------------------------------------------------===//

let ExeDomain = SSEPackedInt in { // SSE integer instructions

/// PDI_binop_rm2 - Simple SSE2 binary operator with different src and dst types
multiclass PDI_binop_rm2<bits<8> opc, string OpcodeStr, SDNode OpNode,
                         ValueType DstVT, ValueType SrcVT, RegisterClass RC,
                         PatFrag memop_frag, X86MemOperand x86memop,
                         X86FoldableSchedWrite sched, bit Is2Addr = 1> {
  let isCommutable = 1 in
  def rr : PDI<opc, MRMSrcReg, (outs RC:$dst),
       (ins RC:$src1, RC:$src2),
       !if(Is2Addr,
           !strconcat(OpcodeStr, "\t{$src2, $dst|$dst, $src2}"),
           !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}")),
       [(set RC:$dst, (DstVT (OpNode (SrcVT RC:$src1), RC:$src2)))]>,
       Sched<[sched]>;
  def rm : PDI<opc, MRMSrcMem, (outs RC:$dst),
       (ins RC:$src1, x86memop:$src2),
       !if(Is2Addr,
           !strconcat(OpcodeStr, "\t{$src2, $dst|$dst, $src2}"),
           !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}")),
       [(set RC:$dst, (DstVT (OpNode (SrcVT RC:$src1),
                                     (memop_frag addr:$src2))))]>,
       Sched<[sched.Folded, sched.ReadAfterFold]>;
}
} // ExeDomain = SSEPackedInt

defm PADDB   : PDI_binop_all<0xFC, "paddb", add, v16i8, v32i8,
                             SchedWriteVecALU, 1, NoVLX_Or_NoBWI>;
defm PADDW   : PDI_binop_all<0xFD, "paddw", add, v8i16, v16i16,
                             SchedWriteVecALU, 1, NoVLX_Or_NoBWI>;
defm PADDD   : PDI_binop_all<0xFE, "paddd", add, v4i32, v8i32,
                             SchedWriteVecALU, 1, NoVLX>;
defm PADDQ   : PDI_binop_all<0xD4, "paddq", add, v2i64, v4i64,
                             SchedWriteVecALU, 1, NoVLX>;
defm PADDSB  : PDI_binop_all<0xEC, "paddsb", saddsat, v16i8, v32i8,
                             SchedWriteVecALU, 1, NoVLX_Or_NoBWI>;
defm PADDSW  : PDI_binop_all<0xED, "paddsw", saddsat, v8i16, v16i16,
                             SchedWriteVecALU, 1, NoVLX_Or_NoBWI>;
defm PADDUSB : PDI_binop_all<0xDC, "paddusb", uaddsat, v16i8, v32i8,
                             SchedWriteVecALU, 1, NoVLX_Or_NoBWI>;
defm PADDUSW : PDI_binop_all<0xDD, "paddusw", uaddsat, v8i16, v16i16,
                             SchedWriteVecALU, 1, NoVLX_Or_NoBWI>;
defm PMULLW  : PDI_binop_all<0xD5, "pmullw", mul, v8i16, v16i16,
                             SchedWriteVecIMul, 1, NoVLX_Or_NoBWI>;
defm PMULHUW : PDI_binop_all<0xE4, "pmulhuw", mulhu, v8i16, v16i16,
                             SchedWriteVecIMul, 1, NoVLX_Or_NoBWI>;
defm PMULHW  : PDI_binop_all<0xE5, "pmulhw", mulhs, v8i16, v16i16,
                             SchedWriteVecIMul, 1, NoVLX_Or_NoBWI>;
defm PSUBB   : PDI_binop_all<0xF8, "psubb", sub, v16i8, v32i8,
                             SchedWriteVecALU, 0, NoVLX_Or_NoBWI>;
defm PSUBW   : PDI_binop_all<0xF9, "psubw", sub, v8i16, v16i16,
                             SchedWriteVecALU, 0, NoVLX_Or_NoBWI>;
defm PSUBD   : PDI_binop_all<0xFA, "psubd", sub, v4i32, v8i32,
                             SchedWriteVecALU, 0, NoVLX>;
defm PSUBQ   : PDI_binop_all<0xFB, "psubq", sub, v2i64, v4i64,
                             SchedWriteVecALU, 0, NoVLX>;
defm PSUBSB  : PDI_binop_all<0xE8, "psubsb", ssubsat, v16i8, v32i8,
                             SchedWriteVecALU, 0, NoVLX_Or_NoBWI>;
defm PSUBSW  : PDI_binop_all<0xE9, "psubsw", ssubsat, v8i16, v16i16,
                             SchedWriteVecALU, 0, NoVLX_Or_NoBWI>;
defm PSUBUSB : PDI_binop_all<0xD8, "psubusb", usubsat, v16i8, v32i8,
                             SchedWriteVecALU, 0, NoVLX_Or_NoBWI>;
defm PSUBUSW : PDI_binop_all<0xD9, "psubusw", usubsat, v8i16, v16i16,
                             SchedWriteVecALU, 0, NoVLX_Or_NoBWI>;
defm PMINUB  : PDI_binop_all<0xDA, "pminub", umin, v16i8, v32i8,
                             SchedWriteVecALU, 1, NoVLX_Or_NoBWI>;
defm PMINSW  : PDI_binop_all<0xEA, "pminsw", smin, v8i16, v16i16,
                             SchedWriteVecALU, 1, NoVLX_Or_NoBWI>;
defm PMAXUB  : PDI_binop_all<0xDE, "pmaxub", umax, v16i8, v32i8,
                             SchedWriteVecALU, 1, NoVLX_Or_NoBWI>;
defm PMAXSW  : PDI_binop_all<0xEE, "pmaxsw", smax, v8i16, v16i16,
                             SchedWriteVecALU, 1, NoVLX_Or_NoBWI>;
defm PAVGB   : PDI_binop_all<0xE0, "pavgb", X86avg, v16i8, v32i8,
                             SchedWriteVecALU, 1, NoVLX_Or_NoBWI>;
defm PAVGW   : PDI_binop_all<0xE3, "pavgw", X86avg, v8i16, v16i16,
                             SchedWriteVecALU, 1, NoVLX_Or_NoBWI>;
defm PMULUDQ : PDI_binop_all<0xF4, "pmuludq", X86pmuludq, v2i64, v4i64,
                             SchedWriteVecIMul, 1, NoVLX>;

let Predicates = [HasAVX, NoVLX_Or_NoBWI] in
defm VPMADDWD : PDI_binop_rm2<0xF5, "vpmaddwd", X86vpmaddwd, v4i32, v8i16, VR128,
                              load, i128mem, SchedWriteVecIMul.XMM, 0>,
                              VEX_4V, VEX_WIG;

let Predicates = [HasAVX2, NoVLX_Or_NoBWI] in
defm VPMADDWDY : PDI_binop_rm2<0xF5, "vpmaddwd", X86vpmaddwd, v8i32, v16i16,
                               VR256, load, i256mem, SchedWriteVecIMul.YMM,
                               0>, VEX_4V, VEX_L, VEX_WIG;
let Constraints = "$src1 = $dst" in
defm PMADDWD : PDI_binop_rm2<0xF5, "pmaddwd", X86vpmaddwd, v4i32, v8i16, VR128,
                             memop, i128mem, SchedWriteVecIMul.XMM>;

let Predicates = [HasAVX, NoVLX_Or_NoBWI] in
defm VPSADBW : PDI_binop_rm2<0xF6, "vpsadbw", X86psadbw, v2i64, v16i8, VR128,
                             load, i128mem, SchedWritePSADBW.XMM, 0>,
                             VEX_4V, VEX_WIG;
let Predicates = [HasAVX2, NoVLX_Or_NoBWI] in
defm VPSADBWY : PDI_binop_rm2<0xF6, "vpsadbw", X86psadbw, v4i64, v32i8, VR256,
                             load, i256mem, SchedWritePSADBW.YMM, 0>,
                             VEX_4V, VEX_L, VEX_WIG;
let Constraints = "$src1 = $dst" in
defm PSADBW : PDI_binop_rm2<0xF6, "psadbw", X86psadbw, v2i64, v16i8, VR128,
                            memop, i128mem, SchedWritePSADBW.XMM>;

//===---------------------------------------------------------------------===//
// SSE2 - Packed Integer Logical Instructions
//===---------------------------------------------------------------------===//

multiclass PDI_binop_rmi<bits<8> opc, bits<8> opc2, Format ImmForm,
                         string OpcodeStr, SDNode OpNode,
                         SDNode OpNode2, RegisterClass RC,
                         X86FoldableSchedWrite sched,
                         X86FoldableSchedWrite schedImm,
                         ValueType DstVT, ValueType SrcVT,
                         PatFrag ld_frag, bit Is2Addr = 1> {
  // src2 is always 128-bit
  def rr : PDI<opc, MRMSrcReg, (outs RC:$dst),
       (ins RC:$src1, VR128:$src2),
       !if(Is2Addr,
           !strconcat(OpcodeStr, "\t{$src2, $dst|$dst, $src2}"),
           !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}")),
       [(set RC:$dst, (DstVT (OpNode RC:$src1, (SrcVT VR128:$src2))))]>,
       Sched<[sched]>;
  def rm : PDI<opc, MRMSrcMem, (outs RC:$dst),
       (ins RC:$src1, i128mem:$src2),
       !if(Is2Addr,
           !strconcat(OpcodeStr, "\t{$src2, $dst|$dst, $src2}"),
           !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}")),
       [(set RC:$dst, (DstVT (OpNode RC:$src1,
                       (SrcVT (ld_frag addr:$src2)))))]>,
       Sched<[sched.Folded, sched.ReadAfterFold]>;
  def ri : PDIi8<opc2, ImmForm, (outs RC:$dst),
       (ins RC:$src1, u8imm:$src2),
       !if(Is2Addr,
           !strconcat(OpcodeStr, "\t{$src2, $dst|$dst, $src2}"),
           !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}")),
       [(set RC:$dst, (DstVT (OpNode2 RC:$src1, (i8 imm:$src2))))]>,
       Sched<[schedImm]>;
}

multiclass PDI_binop_rmi_all<bits<8> opc, bits<8> opc2, Format ImmForm,
                             string OpcodeStr, SDNode OpNode,
                             SDNode OpNode2, ValueType DstVT128,
                             ValueType DstVT256, ValueType SrcVT,
                             X86SchedWriteWidths sched,
                             X86SchedWriteWidths schedImm, Predicate prd> {
let Predicates = [HasAVX, prd] in
  defm V#NAME : PDI_binop_rmi<opc, opc2, ImmForm, !strconcat("v", OpcodeStr),
                              OpNode, OpNode2, VR128, sched.XMM, schedImm.XMM,
                              DstVT128, SrcVT, load, 0>, VEX_4V, VEX_WIG;
let Predicates = [HasAVX2, prd] in
  defm V#NAME#Y : PDI_binop_rmi<opc, opc2, ImmForm, !strconcat("v", OpcodeStr),
                                OpNode, OpNode2, VR256, sched.YMM, schedImm.YMM,
                                DstVT256, SrcVT, load, 0>, VEX_4V, VEX_L,
                                VEX_WIG;
let Constraints = "$src1 = $dst" in
  defm NAME : PDI_binop_rmi<opc, opc2, ImmForm, OpcodeStr, OpNode, OpNode2,
                            VR128, sched.XMM, schedImm.XMM, DstVT128, SrcVT,
                            memop>;
}

multiclass PDI_binop_ri<bits<8> opc, Format ImmForm, string OpcodeStr,
                        SDNode OpNode, RegisterClass RC, ValueType VT,
                        X86FoldableSchedWrite sched, bit Is2Addr = 1> {
  def ri : PDIi8<opc, ImmForm, (outs RC:$dst), (ins RC:$src1, u8imm:$src2),
       !if(Is2Addr,
           !strconcat(OpcodeStr, "\t{$src2, $dst|$dst, $src2}"),
           !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}")),
       [(set RC:$dst, (VT (OpNode RC:$src1, (i8 imm:$src2))))]>,
       Sched<[sched]>;
}

multiclass PDI_binop_ri_all<bits<8> opc, Format ImmForm, string OpcodeStr,
                            SDNode OpNode, X86SchedWriteWidths sched> {
let Predicates = [HasAVX, NoVLX_Or_NoBWI] in
  defm V#NAME : PDI_binop_ri<opc, ImmForm, !strconcat("v", OpcodeStr), OpNode,
                             VR128, v16i8, sched.XMM, 0>, VEX_4V, VEX_WIG;
let Predicates = [HasAVX2, NoVLX_Or_NoBWI] in
  defm V#NAME#Y : PDI_binop_ri<opc, ImmForm, !strconcat("v", OpcodeStr), OpNode,
                               VR256, v32i8, sched.YMM, 0>,
                               VEX_4V, VEX_L, VEX_WIG;
let Constraints = "$src1 = $dst" in
  defm NAME : PDI_binop_ri<opc, ImmForm, OpcodeStr, OpNode, VR128, v16i8,
                           sched.XMM>;
}

let ExeDomain = SSEPackedInt in {
  defm PSLLW : PDI_binop_rmi_all<0xF1, 0x71, MRM6r, "psllw", X86vshl, X86vshli,
                                 v8i16, v16i16, v8i16, SchedWriteVecShift,
                                 SchedWriteVecShiftImm, NoVLX_Or_NoBWI>;
  defm PSLLD : PDI_binop_rmi_all<0xF2, 0x72, MRM6r, "pslld", X86vshl, X86vshli,
                                 v4i32, v8i32, v4i32, SchedWriteVecShift,
                                 SchedWriteVecShiftImm, NoVLX>;
  defm PSLLQ : PDI_binop_rmi_all<0xF3, 0x73, MRM6r, "psllq", X86vshl, X86vshli,
                                 v2i64, v4i64, v2i64, SchedWriteVecShift,
                                 SchedWriteVecShiftImm, NoVLX>;

  defm PSRLW : PDI_binop_rmi_all<0xD1, 0x71, MRM2r, "psrlw", X86vsrl, X86vsrli,
                                 v8i16, v16i16, v8i16, SchedWriteVecShift,
                                 SchedWriteVecShiftImm, NoVLX_Or_NoBWI>;
  defm PSRLD : PDI_binop_rmi_all<0xD2, 0x72, MRM2r, "psrld", X86vsrl, X86vsrli,
                                 v4i32, v8i32, v4i32, SchedWriteVecShift,
                                 SchedWriteVecShiftImm, NoVLX>;
  defm PSRLQ : PDI_binop_rmi_all<0xD3, 0x73, MRM2r, "psrlq", X86vsrl, X86vsrli,
                                 v2i64, v4i64, v2i64, SchedWriteVecShift,
                                 SchedWriteVecShiftImm, NoVLX>;

  defm PSRAW : PDI_binop_rmi_all<0xE1, 0x71, MRM4r, "psraw", X86vsra, X86vsrai,
                                 v8i16, v16i16, v8i16, SchedWriteVecShift,
                                 SchedWriteVecShiftImm, NoVLX_Or_NoBWI>;
  defm PSRAD : PDI_binop_rmi_all<0xE2, 0x72, MRM4r, "psrad", X86vsra, X86vsrai,
                                 v4i32, v8i32, v4i32, SchedWriteVecShift,
                                 SchedWriteVecShiftImm, NoVLX>;

  defm PSLLDQ : PDI_binop_ri_all<0x73, MRM7r, "pslldq", X86vshldq,
                                 SchedWriteShuffle>;
  defm PSRLDQ : PDI_binop_ri_all<0x73, MRM3r, "psrldq", X86vshrdq,
                                 SchedWriteShuffle>;
} // ExeDomain = SSEPackedInt

//===---------------------------------------------------------------------===//
// SSE2 - Packed Integer Comparison Instructions
//===---------------------------------------------------------------------===//

defm PCMPEQB : PDI_binop_all<0x74, "pcmpeqb", X86pcmpeq, v16i8, v32i8,
                             SchedWriteVecALU, 1, TruePredicate>;
defm PCMPEQW : PDI_binop_all<0x75, "pcmpeqw", X86pcmpeq, v8i16, v16i16,
                             SchedWriteVecALU, 1, TruePredicate>;
defm PCMPEQD : PDI_binop_all<0x76, "pcmpeqd", X86pcmpeq, v4i32, v8i32,
                             SchedWriteVecALU, 1, TruePredicate>;
defm PCMPGTB : PDI_binop_all<0x64, "pcmpgtb", X86pcmpgt, v16i8, v32i8,
                             SchedWriteVecALU, 0, TruePredicate>;
defm PCMPGTW : PDI_binop_all<0x65, "pcmpgtw", X86pcmpgt, v8i16, v16i16,
                             SchedWriteVecALU, 0, TruePredicate>;
defm PCMPGTD : PDI_binop_all<0x66, "pcmpgtd", X86pcmpgt, v4i32, v8i32,
                             SchedWriteVecALU, 0, TruePredicate>;

//===---------------------------------------------------------------------===//
// SSE2 - Packed Integer Shuffle Instructions
//===---------------------------------------------------------------------===//

let ExeDomain = SSEPackedInt in {
multiclass sse2_pshuffle<string OpcodeStr, ValueType vt128, ValueType vt256,
                         SDNode OpNode, X86SchedWriteWidths sched,
                         Predicate prd> {
let Predicates = [HasAVX, prd] in {
  def V#NAME#ri : Ii8<0x70, MRMSrcReg, (outs VR128:$dst),
                      (ins VR128:$src1, u8imm:$src2),
                      !strconcat("v", OpcodeStr,
                                 "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
                      [(set VR128:$dst,
                        (vt128 (OpNode VR128:$src1, (i8 imm:$src2))))]>,
                      VEX, Sched<[sched.XMM]>, VEX_WIG;
  def V#NAME#mi : Ii8<0x70, MRMSrcMem, (outs VR128:$dst),
                      (ins i128mem:$src1, u8imm:$src2),
                      !strconcat("v", OpcodeStr,
                                 "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
                     [(set VR128:$dst,
                       (vt128 (OpNode (load addr:$src1),
                        (i8 imm:$src2))))]>, VEX,
                  Sched<[sched.XMM.Folded]>, VEX_WIG;
}

let Predicates = [HasAVX2, prd] in {
  def V#NAME#Yri : Ii8<0x70, MRMSrcReg, (outs VR256:$dst),
                       (ins VR256:$src1, u8imm:$src2),
                       !strconcat("v", OpcodeStr,
                                  "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
                       [(set VR256:$dst,
                         (vt256 (OpNode VR256:$src1, (i8 imm:$src2))))]>,
                       VEX, VEX_L, Sched<[sched.YMM]>, VEX_WIG;
  def V#NAME#Ymi : Ii8<0x70, MRMSrcMem, (outs VR256:$dst),
                       (ins i256mem:$src1, u8imm:$src2),
                       !strconcat("v", OpcodeStr,
                                  "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
                      [(set VR256:$dst,
                        (vt256 (OpNode (load addr:$src1),
                         (i8 imm:$src2))))]>, VEX, VEX_L,
                   Sched<[sched.YMM.Folded]>, VEX_WIG;
}

let Predicates = [UseSSE2] in {
  def ri : Ii8<0x70, MRMSrcReg,
               (outs VR128:$dst), (ins VR128:$src1, u8imm:$src2),
               !strconcat(OpcodeStr,
                          "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
               [(set VR128:$dst,
                 (vt128 (OpNode VR128:$src1, (i8 imm:$src2))))]>,
               Sched<[sched.XMM]>;
  def mi : Ii8<0x70, MRMSrcMem,
               (outs VR128:$dst), (ins i128mem:$src1, u8imm:$src2),
               !strconcat(OpcodeStr,
                          "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
               [(set VR128:$dst,
                 (vt128 (OpNode (memop addr:$src1),
                        (i8 imm:$src2))))]>,
               Sched<[sched.XMM.Folded]>;
}
}
} // ExeDomain = SSEPackedInt

defm PSHUFD  : sse2_pshuffle<"pshufd", v4i32, v8i32, X86PShufd,
                             SchedWriteShuffle, NoVLX>, PD;
defm PSHUFHW : sse2_pshuffle<"pshufhw", v8i16, v16i16, X86PShufhw,
                             SchedWriteShuffle, NoVLX_Or_NoBWI>, XS;
defm PSHUFLW : sse2_pshuffle<"pshuflw", v8i16, v16i16, X86PShuflw,
                             SchedWriteShuffle, NoVLX_Or_NoBWI>, XD;

//===---------------------------------------------------------------------===//
// Packed Integer Pack Instructions (SSE & AVX)
//===---------------------------------------------------------------------===//

let ExeDomain = SSEPackedInt in {
multiclass sse2_pack<bits<8> opc, string OpcodeStr, ValueType OutVT,
                     ValueType ArgVT, SDNode OpNode, RegisterClass RC,
                     X86MemOperand x86memop, X86FoldableSchedWrite sched,
                     PatFrag ld_frag, bit Is2Addr = 1> {
  def rr : PDI<opc, MRMSrcReg,
               (outs RC:$dst), (ins RC:$src1, RC:$src2),
               !if(Is2Addr,
                   !strconcat(OpcodeStr, "\t{$src2, $dst|$dst, $src2}"),
                   !strconcat(OpcodeStr,
                              "\t{$src2, $src1, $dst|$dst, $src1, $src2}")),
               [(set RC:$dst,
                     (OutVT (OpNode (ArgVT RC:$src1), RC:$src2)))]>,
               Sched<[sched]>;
  def rm : PDI<opc, MRMSrcMem,
               (outs RC:$dst), (ins RC:$src1, x86memop:$src2),
               !if(Is2Addr,
                   !strconcat(OpcodeStr, "\t{$src2, $dst|$dst, $src2}"),
                   !strconcat(OpcodeStr,
                              "\t{$src2, $src1, $dst|$dst, $src1, $src2}")),
               [(set RC:$dst,
                     (OutVT (OpNode (ArgVT RC:$src1),
                                    (ld_frag addr:$src2))))]>,
               Sched<[sched.Folded, sched.ReadAfterFold]>;
}

multiclass sse4_pack<bits<8> opc, string OpcodeStr, ValueType OutVT,
                     ValueType ArgVT, SDNode OpNode, RegisterClass RC,
                     X86MemOperand x86memop, X86FoldableSchedWrite sched,
                     PatFrag ld_frag, bit Is2Addr = 1> {
  def rr : SS48I<opc, MRMSrcReg,
                 (outs RC:$dst), (ins RC:$src1, RC:$src2),
                 !if(Is2Addr,
                     !strconcat(OpcodeStr, "\t{$src2, $dst|$dst, $src2}"),
                     !strconcat(OpcodeStr,
                                "\t{$src2, $src1, $dst|$dst, $src1, $src2}")),
                 [(set RC:$dst,
                       (OutVT (OpNode (ArgVT RC:$src1), RC:$src2)))]>,
                 Sched<[sched]>;
  def rm : SS48I<opc, MRMSrcMem,
                 (outs RC:$dst), (ins RC:$src1, x86memop:$src2),
                 !if(Is2Addr,
                     !strconcat(OpcodeStr, "\t{$src2, $dst|$dst, $src2}"),
                     !strconcat(OpcodeStr,
                                "\t{$src2, $src1, $dst|$dst, $src1, $src2}")),
                 [(set RC:$dst,
                       (OutVT (OpNode (ArgVT RC:$src1),
                                      (ld_frag addr:$src2))))]>,
                 Sched<[sched.Folded, sched.ReadAfterFold]>;
}

let Predicates = [HasAVX, NoVLX_Or_NoBWI] in {
  defm VPACKSSWB : sse2_pack<0x63, "vpacksswb", v16i8, v8i16, X86Packss, VR128,
                             i128mem, SchedWriteShuffle.XMM, load, 0>,
                             VEX_4V, VEX_WIG;
  defm VPACKSSDW : sse2_pack<0x6B, "vpackssdw", v8i16, v4i32, X86Packss, VR128,
                             i128mem, SchedWriteShuffle.XMM, load, 0>,
                             VEX_4V, VEX_WIG;

  defm VPACKUSWB : sse2_pack<0x67, "vpackuswb", v16i8, v8i16, X86Packus, VR128,
                             i128mem, SchedWriteShuffle.XMM, load, 0>,
                             VEX_4V, VEX_WIG;
  defm VPACKUSDW : sse4_pack<0x2B, "vpackusdw", v8i16, v4i32, X86Packus, VR128,
                             i128mem, SchedWriteShuffle.XMM, load, 0>,
                             VEX_4V;
}

let Predicates = [HasAVX2, NoVLX_Or_NoBWI] in {
  defm VPACKSSWBY : sse2_pack<0x63, "vpacksswb", v32i8, v16i16, X86Packss, VR256,
                              i256mem, SchedWriteShuffle.YMM, load, 0>,
                              VEX_4V, VEX_L, VEX_WIG;
  defm VPACKSSDWY : sse2_pack<0x6B, "vpackssdw", v16i16, v8i32, X86Packss, VR256,
                              i256mem, SchedWriteShuffle.YMM, load, 0>,
                              VEX_4V, VEX_L, VEX_WIG;

  defm VPACKUSWBY : sse2_pack<0x67, "vpackuswb", v32i8, v16i16, X86Packus, VR256,
                              i256mem, SchedWriteShuffle.YMM, load, 0>,
                              VEX_4V, VEX_L, VEX_WIG;
  defm VPACKUSDWY : sse4_pack<0x2B, "vpackusdw", v16i16, v8i32, X86Packus, VR256,
                              i256mem, SchedWriteShuffle.YMM, load, 0>,
                              VEX_4V, VEX_L;
}

let Constraints = "$src1 = $dst" in {
  defm PACKSSWB : sse2_pack<0x63, "packsswb", v16i8, v8i16, X86Packss, VR128,
                            i128mem, SchedWriteShuffle.XMM, memop>;
  defm PACKSSDW : sse2_pack<0x6B, "packssdw", v8i16, v4i32, X86Packss, VR128,
                            i128mem, SchedWriteShuffle.XMM, memop>;

  defm PACKUSWB : sse2_pack<0x67, "packuswb", v16i8, v8i16, X86Packus, VR128,
                            i128mem, SchedWriteShuffle.XMM, memop>;

  defm PACKUSDW : sse4_pack<0x2B, "packusdw", v8i16, v4i32, X86Packus, VR128,
                            i128mem, SchedWriteShuffle.XMM, memop>;
}
} // ExeDomain = SSEPackedInt

//===---------------------------------------------------------------------===//
// SSE2 - Packed Integer Unpack Instructions
//===---------------------------------------------------------------------===//

let ExeDomain = SSEPackedInt in {
multiclass sse2_unpack<bits<8> opc, string OpcodeStr, ValueType vt,
                       SDNode OpNode, RegisterClass RC, X86MemOperand x86memop,
                       X86FoldableSchedWrite sched, PatFrag ld_frag,
                       bit Is2Addr = 1> {
  def rr : PDI<opc, MRMSrcReg,
      (outs RC:$dst), (ins RC:$src1, RC:$src2),
      !if(Is2Addr,
          !strconcat(OpcodeStr,"\t{$src2, $dst|$dst, $src2}"),
          !strconcat(OpcodeStr,"\t{$src2, $src1, $dst|$dst, $src1, $src2}")),
      [(set RC:$dst, (vt (OpNode RC:$src1, RC:$src2)))]>,
      Sched<[sched]>;
  def rm : PDI<opc, MRMSrcMem,
      (outs RC:$dst), (ins RC:$src1, x86memop:$src2),
      !if(Is2Addr,
          !strconcat(OpcodeStr,"\t{$src2, $dst|$dst, $src2}"),
          !strconcat(OpcodeStr,"\t{$src2, $src1, $dst|$dst, $src1, $src2}")),
      [(set RC:$dst, (vt (OpNode RC:$src1, (ld_frag addr:$src2))))]>,
      Sched<[sched.Folded, sched.ReadAfterFold]>;
}

let Predicates = [HasAVX, NoVLX_Or_NoBWI] in {
  defm VPUNPCKLBW  : sse2_unpack<0x60, "vpunpcklbw", v16i8, X86Unpckl, VR128,
                                 i128mem, SchedWriteShuffle.XMM, load, 0>,
                                 VEX_4V, VEX_WIG;
  defm VPUNPCKLWD  : sse2_unpack<0x61, "vpunpcklwd", v8i16, X86Unpckl, VR128,
                                 i128mem, SchedWriteShuffle.XMM, load, 0>,
                                 VEX_4V, VEX_WIG;
  defm VPUNPCKHBW  : sse2_unpack<0x68, "vpunpckhbw", v16i8, X86Unpckh, VR128,
                                 i128mem, SchedWriteShuffle.XMM, load, 0>,
                                 VEX_4V, VEX_WIG;
  defm VPUNPCKHWD  : sse2_unpack<0x69, "vpunpckhwd", v8i16, X86Unpckh, VR128,
                                 i128mem, SchedWriteShuffle.XMM, load, 0>,
                                 VEX_4V, VEX_WIG;
}

let Predicates = [HasAVX, NoVLX] in {
  defm VPUNPCKLDQ  : sse2_unpack<0x62, "vpunpckldq", v4i32, X86Unpckl, VR128,
                                 i128mem, SchedWriteShuffle.XMM, load, 0>,
                                 VEX_4V, VEX_WIG;
  defm VPUNPCKLQDQ : sse2_unpack<0x6C, "vpunpcklqdq", v2i64, X86Unpckl, VR128,
                                 i128mem, SchedWriteShuffle.XMM, load, 0>,
                                 VEX_4V, VEX_WIG;
  defm VPUNPCKHDQ  : sse2_unpack<0x6A, "vpunpckhdq", v4i32, X86Unpckh, VR128,
                                 i128mem, SchedWriteShuffle.XMM, load, 0>,
                                 VEX_4V, VEX_WIG;
  defm VPUNPCKHQDQ : sse2_unpack<0x6D, "vpunpckhqdq", v2i64, X86Unpckh, VR128,
                                 i128mem, SchedWriteShuffle.XMM, load, 0>,
                                 VEX_4V, VEX_WIG;
}

let Predicates = [HasAVX2, NoVLX_Or_NoBWI] in {
  defm VPUNPCKLBWY  : sse2_unpack<0x60, "vpunpcklbw", v32i8, X86Unpckl, VR256,
                                  i256mem, SchedWriteShuffle.YMM, load, 0>,
                                  VEX_4V, VEX_L, VEX_WIG;
  defm VPUNPCKLWDY  : sse2_unpack<0x61, "vpunpcklwd", v16i16, X86Unpckl, VR256,
                                  i256mem, SchedWriteShuffle.YMM, load, 0>,
                                  VEX_4V, VEX_L, VEX_WIG;
  defm VPUNPCKHBWY  : sse2_unpack<0x68, "vpunpckhbw", v32i8, X86Unpckh, VR256,
                                  i256mem, SchedWriteShuffle.YMM, load, 0>,
                                  VEX_4V, VEX_L, VEX_WIG;
  defm VPUNPCKHWDY  : sse2_unpack<0x69, "vpunpckhwd", v16i16, X86Unpckh, VR256,
                                  i256mem, SchedWriteShuffle.YMM, load, 0>,
                                  VEX_4V, VEX_L, VEX_WIG;
}

let Predicates = [HasAVX2, NoVLX] in {
  defm VPUNPCKLDQY  : sse2_unpack<0x62, "vpunpckldq", v8i32, X86Unpckl, VR256,
                                  i256mem, SchedWriteShuffle.YMM, load, 0>,
                                  VEX_4V, VEX_L, VEX_WIG;
  defm VPUNPCKLQDQY : sse2_unpack<0x6C, "vpunpcklqdq", v4i64, X86Unpckl, VR256,
                                  i256mem, SchedWriteShuffle.YMM, load, 0>,
                                  VEX_4V, VEX_L, VEX_WIG;
  defm VPUNPCKHDQY  : sse2_unpack<0x6A, "vpunpckhdq", v8i32, X86Unpckh, VR256,
                                  i256mem, SchedWriteShuffle.YMM, load, 0>,
                                  VEX_4V, VEX_L, VEX_WIG;
  defm VPUNPCKHQDQY : sse2_unpack<0x6D, "vpunpckhqdq", v4i64, X86Unpckh, VR256,
                                  i256mem, SchedWriteShuffle.YMM, load, 0>,
                                  VEX_4V, VEX_L, VEX_WIG;
}

let Constraints = "$src1 = $dst" in {
  defm PUNPCKLBW  : sse2_unpack<0x60, "punpcklbw", v16i8, X86Unpckl, VR128,
                                i128mem, SchedWriteShuffle.XMM, memop>;
  defm PUNPCKLWD  : sse2_unpack<0x61, "punpcklwd", v8i16, X86Unpckl, VR128,
                                i128mem, SchedWriteShuffle.XMM, memop>;
  defm PUNPCKLDQ  : sse2_unpack<0x62, "punpckldq", v4i32, X86Unpckl, VR128,
                                i128mem, SchedWriteShuffle.XMM, memop>;
  defm PUNPCKLQDQ : sse2_unpack<0x6C, "punpcklqdq", v2i64, X86Unpckl, VR128,
                                i128mem, SchedWriteShuffle.XMM, memop>;

  defm PUNPCKHBW  : sse2_unpack<0x68, "punpckhbw", v16i8, X86Unpckh, VR128,
                                i128mem, SchedWriteShuffle.XMM, memop>;
  defm PUNPCKHWD  : sse2_unpack<0x69, "punpckhwd", v8i16, X86Unpckh, VR128,
                                i128mem, SchedWriteShuffle.XMM, memop>;
  defm PUNPCKHDQ  : sse2_unpack<0x6A, "punpckhdq", v4i32, X86Unpckh, VR128,
                                i128mem, SchedWriteShuffle.XMM, memop>;
  defm PUNPCKHQDQ : sse2_unpack<0x6D, "punpckhqdq", v2i64, X86Unpckh, VR128,
                                i128mem, SchedWriteShuffle.XMM, memop>;
}
} // ExeDomain = SSEPackedInt

//===---------------------------------------------------------------------===//
// SSE2 - Packed Integer Extract and Insert
//===---------------------------------------------------------------------===//

let ExeDomain = SSEPackedInt in {
multiclass sse2_pinsrw<bit Is2Addr = 1> {
  def rr : Ii8<0xC4, MRMSrcReg,
       (outs VR128:$dst), (ins VR128:$src1,
        GR32orGR64:$src2, u8imm:$src3),
       !if(Is2Addr,
           "pinsrw\t{$src3, $src2, $dst|$dst, $src2, $src3}",
           "vpinsrw\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}"),
       [(set VR128:$dst,
         (X86pinsrw VR128:$src1, GR32orGR64:$src2, imm:$src3))]>,
       Sched<[WriteVecInsert]>;
  def rm : Ii8<0xC4, MRMSrcMem,
                      (outs VR128:$dst), (ins VR128:$src1,
                       i16mem:$src2, u8imm:$src3),
       !if(Is2Addr,
           "pinsrw\t{$src3, $src2, $dst|$dst, $src2, $src3}",
           "vpinsrw\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}"),
       [(set VR128:$dst,
         (X86pinsrw VR128:$src1, (extloadi16 addr:$src2),
                    imm:$src3))]>,
       Sched<[WriteVecInsert.Folded, WriteVecInsert.ReadAfterFold]>;
}

// Extract
let Predicates = [HasAVX, NoBWI] in
def VPEXTRWrr : Ii8<0xC5, MRMSrcReg,
                    (outs GR32orGR64:$dst), (ins VR128:$src1, u8imm:$src2),
                    "vpextrw\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                    [(set GR32orGR64:$dst, (X86pextrw (v8i16 VR128:$src1),
                                            imm:$src2))]>,
                PD, VEX, Sched<[WriteVecExtract]>;
def PEXTRWrr : PDIi8<0xC5, MRMSrcReg,
                    (outs GR32orGR64:$dst), (ins VR128:$src1, u8imm:$src2),
                    "pextrw\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                    [(set GR32orGR64:$dst, (X86pextrw (v8i16 VR128:$src1),
                                            imm:$src2))]>,
               Sched<[WriteVecExtract]>;

// Insert
let Predicates = [HasAVX, NoBWI] in
defm VPINSRW : sse2_pinsrw<0>, PD, VEX_4V;

let Predicates = [UseSSE2], Constraints = "$src1 = $dst" in
defm PINSRW : sse2_pinsrw, PD;

} // ExeDomain = SSEPackedInt

//===---------------------------------------------------------------------===//
// SSE2 - Packed Mask Creation
//===---------------------------------------------------------------------===//

let ExeDomain = SSEPackedInt in {

def VPMOVMSKBrr  : VPDI<0xD7, MRMSrcReg, (outs GR32orGR64:$dst),
           (ins VR128:$src),
           "pmovmskb\t{$src, $dst|$dst, $src}",
           [(set GR32orGR64:$dst, (X86movmsk (v16i8 VR128:$src)))]>,
           Sched<[WriteVecMOVMSK]>, VEX, VEX_WIG;

let Predicates = [HasAVX2] in {
def VPMOVMSKBYrr  : VPDI<0xD7, MRMSrcReg, (outs GR32orGR64:$dst),
           (ins VR256:$src),
           "pmovmskb\t{$src, $dst|$dst, $src}",
           [(set GR32orGR64:$dst, (X86movmsk (v32i8 VR256:$src)))]>,
           Sched<[WriteVecMOVMSKY]>, VEX, VEX_L, VEX_WIG;
}

def PMOVMSKBrr : PDI<0xD7, MRMSrcReg, (outs GR32orGR64:$dst), (ins VR128:$src),
           "pmovmskb\t{$src, $dst|$dst, $src}",
           [(set GR32orGR64:$dst, (X86movmsk (v16i8 VR128:$src)))]>,
           Sched<[WriteVecMOVMSK]>;

} // ExeDomain = SSEPackedInt

//===---------------------------------------------------------------------===//
// SSE2 - Conditional Store
//===---------------------------------------------------------------------===//

let ExeDomain = SSEPackedInt, SchedRW = [SchedWriteVecMoveLS.XMM.MR] in {
let Uses = [EDI], Predicates = [HasAVX,Not64BitMode] in
def VMASKMOVDQU : VPDI<0xF7, MRMSrcReg, (outs),
           (ins VR128:$src, VR128:$mask),
           "maskmovdqu\t{$mask, $src|$src, $mask}",
           [(int_x86_sse2_maskmov_dqu VR128:$src, VR128:$mask, EDI)]>,
           VEX, VEX_WIG;
let Uses = [RDI], Predicates = [HasAVX,In64BitMode] in
def VMASKMOVDQU64 : VPDI<0xF7, MRMSrcReg, (outs),
           (ins VR128:$src, VR128:$mask),
           "maskmovdqu\t{$mask, $src|$src, $mask}",
           [(int_x86_sse2_maskmov_dqu VR128:$src, VR128:$mask, RDI)]>,
           VEX, VEX_WIG;

let Uses = [EDI], Predicates = [UseSSE2,Not64BitMode] in
def MASKMOVDQU : PDI<0xF7, MRMSrcReg, (outs), (ins VR128:$src, VR128:$mask),
           "maskmovdqu\t{$mask, $src|$src, $mask}",
           [(int_x86_sse2_maskmov_dqu VR128:$src, VR128:$mask, EDI)]>;
let Uses = [RDI], Predicates = [UseSSE2,In64BitMode] in
def MASKMOVDQU64 : PDI<0xF7, MRMSrcReg, (outs), (ins VR128:$src, VR128:$mask),
           "maskmovdqu\t{$mask, $src|$src, $mask}",
           [(int_x86_sse2_maskmov_dqu VR128:$src, VR128:$mask, RDI)]>;

} // ExeDomain = SSEPackedInt

//===---------------------------------------------------------------------===//
// SSE2 - Move Doubleword/Quadword
//===---------------------------------------------------------------------===//

//===---------------------------------------------------------------------===//
// Move Int Doubleword to Packed Double Int
//
let ExeDomain = SSEPackedInt in {
def VMOVDI2PDIrr : VS2I<0x6E, MRMSrcReg, (outs VR128:$dst), (ins GR32:$src),
                        "movd\t{$src, $dst|$dst, $src}",
                        [(set VR128:$dst,
                          (v4i32 (scalar_to_vector GR32:$src)))]>,
                          VEX, Sched<[WriteVecMoveFromGpr]>;
def VMOVDI2PDIrm : VS2I<0x6E, MRMSrcMem, (outs VR128:$dst), (ins i32mem:$src),
                        "movd\t{$src, $dst|$dst, $src}",
                        [(set VR128:$dst,
                          (v4i32 (scalar_to_vector (loadi32 addr:$src))))]>,
                        VEX, Sched<[WriteVecLoad]>;
def VMOV64toPQIrr : VRS2I<0x6E, MRMSrcReg, (outs VR128:$dst), (ins GR64:$src),
                          "movq\t{$src, $dst|$dst, $src}",
                          [(set VR128:$dst,
                            (v2i64 (scalar_to_vector GR64:$src)))]>,
                          VEX, Sched<[WriteVecMoveFromGpr]>;
let isCodeGenOnly = 1, ForceDisassemble = 1, hasSideEffects = 0, mayLoad = 1 in
def VMOV64toPQIrm : VRS2I<0x6E, MRMSrcMem, (outs VR128:$dst), (ins i64mem:$src),
                          "movq\t{$src, $dst|$dst, $src}", []>,
                          VEX, Sched<[WriteVecLoad]>;
let isCodeGenOnly = 1 in
def VMOV64toSDrr : VRS2I<0x6E, MRMSrcReg, (outs FR64:$dst), (ins GR64:$src),
                         "movq\t{$src, $dst|$dst, $src}",
                         [(set FR64:$dst, (bitconvert GR64:$src))]>,
                         VEX, Sched<[WriteVecMoveFromGpr]>;

def MOVDI2PDIrr : S2I<0x6E, MRMSrcReg, (outs VR128:$dst), (ins GR32:$src),
                      "movd\t{$src, $dst|$dst, $src}",
                      [(set VR128:$dst,
                        (v4i32 (scalar_to_vector GR32:$src)))]>,
                      Sched<[WriteVecMoveFromGpr]>;
def MOVDI2PDIrm : S2I<0x6E, MRMSrcMem, (outs VR128:$dst), (ins i32mem:$src),
                      "movd\t{$src, $dst|$dst, $src}",
                      [(set VR128:$dst,
                        (v4i32 (scalar_to_vector (loadi32 addr:$src))))]>,
                      Sched<[WriteVecLoad]>;
def MOV64toPQIrr : RS2I<0x6E, MRMSrcReg, (outs VR128:$dst), (ins GR64:$src),
                        "movq\t{$src, $dst|$dst, $src}",
                        [(set VR128:$dst,
                          (v2i64 (scalar_to_vector GR64:$src)))]>,
                        Sched<[WriteVecMoveFromGpr]>;
let isCodeGenOnly = 1, ForceDisassemble = 1, hasSideEffects = 0, mayLoad = 1 in
def MOV64toPQIrm : RS2I<0x6E, MRMSrcMem, (outs VR128:$dst), (ins i64mem:$src),
                        "movq\t{$src, $dst|$dst, $src}", []>,
                        Sched<[WriteVecLoad]>;
let isCodeGenOnly = 1 in
def MOV64toSDrr : RS2I<0x6E, MRMSrcReg, (outs FR64:$dst), (ins GR64:$src),
                       "movq\t{$src, $dst|$dst, $src}",
                       [(set FR64:$dst, (bitconvert GR64:$src))]>,
                       Sched<[WriteVecMoveFromGpr]>;
} // ExeDomain = SSEPackedInt

//===---------------------------------------------------------------------===//
// Move Int Doubleword to Single Scalar
//
let ExeDomain = SSEPackedInt, isCodeGenOnly = 1 in {
  def VMOVDI2SSrr  : VS2I<0x6E, MRMSrcReg, (outs FR32:$dst), (ins GR32:$src),
                        "movd\t{$src, $dst|$dst, $src}",
                        [(set FR32:$dst, (bitconvert GR32:$src))]>,
                        VEX, Sched<[WriteVecMoveFromGpr]>;

  def VMOVDI2SSrm  : VS2I<0x6E, MRMSrcMem, (outs FR32:$dst), (ins i32mem:$src),
                        "movd\t{$src, $dst|$dst, $src}",
                        [(set FR32:$dst, (bitconvert (loadi32 addr:$src)))]>,
                        VEX, Sched<[WriteVecLoad]>;
  def MOVDI2SSrr  : S2I<0x6E, MRMSrcReg, (outs FR32:$dst), (ins GR32:$src),
                        "movd\t{$src, $dst|$dst, $src}",
                        [(set FR32:$dst, (bitconvert GR32:$src))]>,
                        Sched<[WriteVecMoveFromGpr]>;

  def MOVDI2SSrm  : S2I<0x6E, MRMSrcMem, (outs FR32:$dst), (ins i32mem:$src),
                        "movd\t{$src, $dst|$dst, $src}",
                        [(set FR32:$dst, (bitconvert (loadi32 addr:$src)))]>,
                        Sched<[WriteVecLoad]>;
} // ExeDomain = SSEPackedInt, isCodeGenOnly = 1

//===---------------------------------------------------------------------===//
// Move Packed Doubleword Int to Packed Double Int
//
let ExeDomain = SSEPackedInt in {
def VMOVPDI2DIrr  : VS2I<0x7E, MRMDestReg, (outs GR32:$dst), (ins VR128:$src),
                         "movd\t{$src, $dst|$dst, $src}",
                         [(set GR32:$dst, (extractelt (v4i32 VR128:$src),
                                          (iPTR 0)))]>, VEX,
                         Sched<[WriteVecMoveToGpr]>;
def VMOVPDI2DImr  : VS2I<0x7E, MRMDestMem, (outs),
                         (ins i32mem:$dst, VR128:$src),
                         "movd\t{$src, $dst|$dst, $src}",
                         [(store (i32 (extractelt (v4i32 VR128:$src),
                                       (iPTR 0))), addr:$dst)]>,
                         VEX, Sched<[WriteVecStore]>;
def MOVPDI2DIrr  : S2I<0x7E, MRMDestReg, (outs GR32:$dst), (ins VR128:$src),
                       "movd\t{$src, $dst|$dst, $src}",
                       [(set GR32:$dst, (extractelt (v4i32 VR128:$src),
                                        (iPTR 0)))]>,
                   Sched<[WriteVecMoveToGpr]>;
def MOVPDI2DImr  : S2I<0x7E, MRMDestMem, (outs), (ins i32mem:$dst, VR128:$src),
                       "movd\t{$src, $dst|$dst, $src}",
                       [(store (i32 (extractelt (v4i32 VR128:$src),
                                     (iPTR 0))), addr:$dst)]>,
                       Sched<[WriteVecStore]>;
} // ExeDomain = SSEPackedInt

//===---------------------------------------------------------------------===//
// Move Packed Doubleword Int first element to Doubleword Int
//
let ExeDomain = SSEPackedInt in {
let SchedRW = [WriteVecMoveToGpr] in {
def VMOVPQIto64rr : VRS2I<0x7E, MRMDestReg, (outs GR64:$dst), (ins VR128:$src),
                          "movq\t{$src, $dst|$dst, $src}",
                          [(set GR64:$dst, (extractelt (v2i64 VR128:$src),
                                                        (iPTR 0)))]>,
                      VEX;

def MOVPQIto64rr : RS2I<0x7E, MRMDestReg, (outs GR64:$dst), (ins VR128:$src),
                        "movq\t{$src, $dst|$dst, $src}",
                        [(set GR64:$dst, (extractelt (v2i64 VR128:$src),
                                                         (iPTR 0)))]>;
} //SchedRW

let isCodeGenOnly = 1, ForceDisassemble = 1, hasSideEffects = 0, mayStore = 1 in
def VMOVPQIto64mr : VRS2I<0x7E, MRMDestMem, (outs),
                          (ins i64mem:$dst, VR128:$src),
                          "movq\t{$src, $dst|$dst, $src}", []>,
                          VEX, Sched<[WriteVecStore]>;
let isCodeGenOnly = 1, ForceDisassemble = 1, hasSideEffects = 0, mayStore = 1 in
def MOVPQIto64mr : RS2I<0x7E, MRMDestMem, (outs), (ins i64mem:$dst, VR128:$src),
                        "movq\t{$src, $dst|$dst, $src}", []>,
                        Sched<[WriteVecStore]>;
} // ExeDomain = SSEPackedInt

//===---------------------------------------------------------------------===//
// Bitcast FR64 <-> GR64
//
let ExeDomain = SSEPackedInt, isCodeGenOnly = 1 in {
  let Predicates = [UseAVX] in
  def VMOV64toSDrm : VS2SI<0x7E, MRMSrcMem, (outs FR64:$dst), (ins i64mem:$src),
                          "movq\t{$src, $dst|$dst, $src}",
                          [(set FR64:$dst, (bitconvert (loadi64 addr:$src)))]>,
                          VEX, Sched<[WriteVecLoad]>;
  def VMOVSDto64rr : VRS2I<0x7E, MRMDestReg, (outs GR64:$dst), (ins FR64:$src),
                           "movq\t{$src, $dst|$dst, $src}",
                           [(set GR64:$dst, (bitconvert FR64:$src))]>,
                           VEX, Sched<[WriteVecMoveToGpr]>;
  def VMOVSDto64mr : VRS2I<0x7E, MRMDestMem, (outs), (ins i64mem:$dst, FR64:$src),
                           "movq\t{$src, $dst|$dst, $src}",
                           [(store (i64 (bitconvert FR64:$src)), addr:$dst)]>,
                           VEX, Sched<[WriteVecStore]>;

  def MOV64toSDrm : S2SI<0x7E, MRMSrcMem, (outs FR64:$dst), (ins i64mem:$src),
                         "movq\t{$src, $dst|$dst, $src}",
                         [(set FR64:$dst, (bitconvert (loadi64 addr:$src)))]>,
                         Sched<[WriteVecLoad]>;
  def MOVSDto64rr : RS2I<0x7E, MRMDestReg, (outs GR64:$dst), (ins FR64:$src),
                         "movq\t{$src, $dst|$dst, $src}",
                         [(set GR64:$dst, (bitconvert FR64:$src))]>,
                         Sched<[WriteVecMoveToGpr]>;
  def MOVSDto64mr : RS2I<0x7E, MRMDestMem, (outs), (ins i64mem:$dst, FR64:$src),
                         "movq\t{$src, $dst|$dst, $src}",
                         [(store (i64 (bitconvert FR64:$src)), addr:$dst)]>,
                         Sched<[WriteVecStore]>;
} // ExeDomain = SSEPackedInt, isCodeGenOnly = 1

//===---------------------------------------------------------------------===//
// Move Scalar Single to Double Int
//
let ExeDomain = SSEPackedInt, isCodeGenOnly = 1 in {
  def VMOVSS2DIrr  : VS2I<0x7E, MRMDestReg, (outs GR32:$dst), (ins FR32:$src),
                        "movd\t{$src, $dst|$dst, $src}",
                        [(set GR32:$dst, (bitconvert FR32:$src))]>,
                        VEX, Sched<[WriteVecMoveToGpr]>;
  def VMOVSS2DImr  : VS2I<0x7E, MRMDestMem, (outs), (ins i32mem:$dst, FR32:$src),
                        "movd\t{$src, $dst|$dst, $src}",
                        [(store (i32 (bitconvert FR32:$src)), addr:$dst)]>,
                        VEX, Sched<[WriteVecStore]>;
  def MOVSS2DIrr  : S2I<0x7E, MRMDestReg, (outs GR32:$dst), (ins FR32:$src),
                        "movd\t{$src, $dst|$dst, $src}",
                        [(set GR32:$dst, (bitconvert FR32:$src))]>,
                        Sched<[WriteVecMoveToGpr]>;
  def MOVSS2DImr  : S2I<0x7E, MRMDestMem, (outs), (ins i32mem:$dst, FR32:$src),
                        "movd\t{$src, $dst|$dst, $src}",
                        [(store (i32 (bitconvert FR32:$src)), addr:$dst)]>,
                        Sched<[WriteVecStore]>;
} // ExeDomain = SSEPackedInt, isCodeGenOnly = 1

let Predicates = [UseAVX] in {
  def : Pat<(v4i32 (X86vzmovl (v4i32 (scalar_to_vector GR32:$src)))),
            (VMOVDI2PDIrr GR32:$src)>;

  def : Pat<(v2i64 (X86vzmovl (v2i64 (scalar_to_vector GR64:$src)))),
            (VMOV64toPQIrr GR64:$src)>;

  def : Pat<(v4i64 (X86vzmovl (insert_subvector undef,
              (v2i64 (scalar_to_vector GR64:$src)),(iPTR 0)))),
            (SUBREG_TO_REG (i64 0), (v2i64 (VMOV64toPQIrr GR64:$src)), sub_xmm)>;
  // AVX 128-bit movd/movq instructions write zeros in the high 128-bit part.
  // These instructions also write zeros in the high part of a 256-bit register.
  def : Pat<(v2i64 (X86vzmovl (v2i64 (scalar_to_vector (zextloadi64i32 addr:$src))))),
            (VMOVDI2PDIrm addr:$src)>;
  def : Pat<(v4i32 (X86vzmovl (v4i32 (scalar_to_vector (loadi32 addr:$src))))),
            (VMOVDI2PDIrm addr:$src)>;
  def : Pat<(v4i32 (X86vzmovl (loadv4i32 addr:$src))),
            (VMOVDI2PDIrm addr:$src)>;
  def : Pat<(v4i32 (X86vzload addr:$src)),
            (VMOVDI2PDIrm addr:$src)>;
  def : Pat<(v8i32 (X86vzmovl (insert_subvector undef,
              (v4i32 (scalar_to_vector (loadi32 addr:$src))), (iPTR 0)))),
            (SUBREG_TO_REG (i32 0), (v4i32 (VMOVDI2PDIrm addr:$src)), sub_xmm)>;
  def : Pat<(v8i32 (X86vzload addr:$src)),
            (SUBREG_TO_REG (i64 0), (v4i32 (VMOVDI2PDIrm addr:$src)), sub_xmm)>;
  // Use regular 128-bit instructions to match 256-bit scalar_to_vec+zext.
  def : Pat<(v8i32 (X86vzmovl (insert_subvector undef,
                               (v4i32 (scalar_to_vector GR32:$src)),(iPTR 0)))),
            (SUBREG_TO_REG (i32 0), (v4i32 (VMOVDI2PDIrr GR32:$src)), sub_xmm)>;
}

let Predicates = [UseSSE2] in {
  def : Pat<(v4i32 (X86vzmovl (v4i32 (scalar_to_vector GR32:$src)))),
            (MOVDI2PDIrr GR32:$src)>;

  def : Pat<(v2i64 (X86vzmovl (v2i64 (scalar_to_vector GR64:$src)))),
            (MOV64toPQIrr GR64:$src)>;
  def : Pat<(v2i64 (X86vzmovl (v2i64 (scalar_to_vector (zextloadi64i32 addr:$src))))),
            (MOVDI2PDIrm addr:$src)>;
  def : Pat<(v4i32 (X86vzmovl (v4i32 (scalar_to_vector (loadi32 addr:$src))))),
            (MOVDI2PDIrm addr:$src)>;
  def : Pat<(v4i32 (X86vzmovl (loadv4i32 addr:$src))),
            (MOVDI2PDIrm addr:$src)>;
  def : Pat<(v4i32 (X86vzload addr:$src)),
            (MOVDI2PDIrm addr:$src)>;
}

// Before the MC layer of LLVM existed, clang emitted "movd" assembly instead of
// "movq" due to MacOS parsing limitation. In order to parse old assembly, we add
// these aliases.
def : InstAlias<"movd\t{$src, $dst|$dst, $src}",
                (MOV64toPQIrr VR128:$dst, GR64:$src), 0>;
def : InstAlias<"movd\t{$src, $dst|$dst, $src}",
                (MOVPQIto64rr GR64:$dst, VR128:$src), 0>;
// Allow "vmovd" but print "vmovq" since we don't need compatibility for AVX.
def : InstAlias<"vmovd\t{$src, $dst|$dst, $src}",
                (VMOV64toPQIrr VR128:$dst, GR64:$src), 0>;
def : InstAlias<"vmovd\t{$src, $dst|$dst, $src}",
                (VMOVPQIto64rr GR64:$dst, VR128:$src), 0>;

//===---------------------------------------------------------------------===//
// SSE2 - Move Quadword
//===---------------------------------------------------------------------===//

//===---------------------------------------------------------------------===//
// Move Quadword Int to Packed Quadword Int
//

let ExeDomain = SSEPackedInt, SchedRW = [WriteVecLoad] in {
def VMOVQI2PQIrm : I<0x7E, MRMSrcMem, (outs VR128:$dst), (ins i64mem:$src),
                    "vmovq\t{$src, $dst|$dst, $src}",
                    [(set VR128:$dst,
                      (v2i64 (scalar_to_vector (loadi64 addr:$src))))]>, XS,
                    VEX, Requires<[UseAVX]>, VEX_WIG;
def MOVQI2PQIrm : I<0x7E, MRMSrcMem, (outs VR128:$dst), (ins i64mem:$src),
                    "movq\t{$src, $dst|$dst, $src}",
                    [(set VR128:$dst,
                      (v2i64 (scalar_to_vector (loadi64 addr:$src))))]>,
                    XS, Requires<[UseSSE2]>; // SSE2 instruction with XS Prefix
} // ExeDomain, SchedRW

//===---------------------------------------------------------------------===//
// Move Packed Quadword Int to Quadword Int
//
let ExeDomain = SSEPackedInt, SchedRW = [WriteVecStore] in {
def VMOVPQI2QImr : VS2I<0xD6, MRMDestMem, (outs), (ins i64mem:$dst, VR128:$src),
                        "movq\t{$src, $dst|$dst, $src}",
                        [(store (i64 (extractelt (v2i64 VR128:$src),
                                      (iPTR 0))), addr:$dst)]>,
                        VEX, VEX_WIG;
def MOVPQI2QImr : S2I<0xD6, MRMDestMem, (outs), (ins i64mem:$dst, VR128:$src),
                      "movq\t{$src, $dst|$dst, $src}",
                      [(store (i64 (extractelt (v2i64 VR128:$src),
                                    (iPTR 0))), addr:$dst)]>;
} // ExeDomain, SchedRW

// For disassembler only
let isCodeGenOnly = 1, ForceDisassemble = 1, hasSideEffects = 0,
    SchedRW = [SchedWriteVecLogic.XMM] in {
def VMOVPQI2QIrr : VS2I<0xD6, MRMDestReg, (outs VR128:$dst), (ins VR128:$src),
                     "movq\t{$src, $dst|$dst, $src}", []>, VEX, VEX_WIG;
def MOVPQI2QIrr : S2I<0xD6, MRMDestReg, (outs VR128:$dst), (ins VR128:$src),
                      "movq\t{$src, $dst|$dst, $src}", []>;
}

// Aliases to help the assembler pick two byte VEX encodings by swapping the
// operands relative to the normal instructions to use VEX.R instead of VEX.B.
def : InstAlias<"vmovq\t{$src, $dst|$dst, $src}",
                (VMOVPQI2QIrr VR128L:$dst, VR128H:$src), 0>;

def : InstAlias<"vmovq.s\t{$src, $dst|$dst, $src}",
                (VMOVPQI2QIrr VR128:$dst, VR128:$src), 0>;
def : InstAlias<"movq.s\t{$src, $dst|$dst, $src}",
                (MOVPQI2QIrr VR128:$dst, VR128:$src), 0>;

let Predicates = [UseAVX] in {
  def : Pat<(v2i64 (X86vzmovl (loadv2i64 addr:$src))),
            (VMOVQI2PQIrm addr:$src)>;
  def : Pat<(v2i64 (X86vzload addr:$src)),
            (VMOVQI2PQIrm addr:$src)>;
  def : Pat<(v4i64 (X86vzmovl (insert_subvector undef,
              (v2i64 (scalar_to_vector (loadi64 addr:$src))), (iPTR 0)))),
            (SUBREG_TO_REG (i64 0), (v2i64 (VMOVQI2PQIrm addr:$src)), sub_xmm)>;
  def : Pat<(v4i64 (X86vzload addr:$src)),
            (SUBREG_TO_REG (i64 0), (v2i64 (VMOVQI2PQIrm addr:$src)), sub_xmm)>;
}

let Predicates = [UseSSE2] in {
  def : Pat<(v2i64 (X86vzmovl (loadv2i64 addr:$src))),
            (MOVQI2PQIrm addr:$src)>;
  def : Pat<(v2i64 (X86vzload addr:$src)), (MOVQI2PQIrm addr:$src)>;
}

//===---------------------------------------------------------------------===//
// Moving from XMM to XMM and clear upper 64 bits. Note, there is a bug in
// IA32 document. movq xmm1, xmm2 does clear the high bits.
//
let ExeDomain = SSEPackedInt, SchedRW = [SchedWriteVecLogic.XMM] in {
def VMOVZPQILo2PQIrr : I<0x7E, MRMSrcReg, (outs VR128:$dst), (ins VR128:$src),
                        "vmovq\t{$src, $dst|$dst, $src}",
                    [(set VR128:$dst, (v2i64 (X86vzmovl (v2i64 VR128:$src))))]>,
                         XS, VEX, Requires<[UseAVX]>, VEX_WIG;
def MOVZPQILo2PQIrr : I<0x7E, MRMSrcReg, (outs VR128:$dst), (ins VR128:$src),
                        "movq\t{$src, $dst|$dst, $src}",
                    [(set VR128:$dst, (v2i64 (X86vzmovl (v2i64 VR128:$src))))]>,
                        XS, Requires<[UseSSE2]>;
} // ExeDomain, SchedRW

let Predicates = [UseAVX] in {
  def : Pat<(v2f64 (X86vzmovl (v2f64 VR128:$src))),
            (VMOVZPQILo2PQIrr VR128:$src)>;
}
let Predicates = [UseSSE2] in {
  def : Pat<(v2f64 (X86vzmovl (v2f64 VR128:$src))),
            (MOVZPQILo2PQIrr VR128:$src)>;
}

//===---------------------------------------------------------------------===//
// SSE3 - Replicate Single FP - MOVSHDUP and MOVSLDUP
//===---------------------------------------------------------------------===//

multiclass sse3_replicate_sfp<bits<8> op, SDNode OpNode, string OpcodeStr,
                              ValueType vt, RegisterClass RC, PatFrag mem_frag,
                              X86MemOperand x86memop, X86FoldableSchedWrite sched> {
def rr : S3SI<op, MRMSrcReg, (outs RC:$dst), (ins RC:$src),
                    !strconcat(OpcodeStr, "\t{$src, $dst|$dst, $src}"),
                      [(set RC:$dst, (vt (OpNode RC:$src)))]>,
                      Sched<[sched]>;
def rm : S3SI<op, MRMSrcMem, (outs RC:$dst), (ins x86memop:$src),
                    !strconcat(OpcodeStr, "\t{$src, $dst|$dst, $src}"),
                      [(set RC:$dst, (OpNode (mem_frag addr:$src)))]>,
                      Sched<[sched.Folded]>;
}

let Predicates = [HasAVX, NoVLX] in {
  defm VMOVSHDUP  : sse3_replicate_sfp<0x16, X86Movshdup, "vmovshdup",
                                       v4f32, VR128, loadv4f32, f128mem,
                                       SchedWriteFShuffle.XMM>, VEX, VEX_WIG;
  defm VMOVSLDUP  : sse3_replicate_sfp<0x12, X86Movsldup, "vmovsldup",
                                       v4f32, VR128, loadv4f32, f128mem,
                                       SchedWriteFShuffle.XMM>, VEX, VEX_WIG;
  defm VMOVSHDUPY : sse3_replicate_sfp<0x16, X86Movshdup, "vmovshdup",
                                       v8f32, VR256, loadv8f32, f256mem,
                                       SchedWriteFShuffle.YMM>, VEX, VEX_L, VEX_WIG;
  defm VMOVSLDUPY : sse3_replicate_sfp<0x12, X86Movsldup, "vmovsldup",
                                       v8f32, VR256, loadv8f32, f256mem,
                                       SchedWriteFShuffle.YMM>, VEX, VEX_L, VEX_WIG;
}
defm MOVSHDUP : sse3_replicate_sfp<0x16, X86Movshdup, "movshdup", v4f32, VR128,
                                   memopv4f32, f128mem, SchedWriteFShuffle.XMM>;
defm MOVSLDUP : sse3_replicate_sfp<0x12, X86Movsldup, "movsldup", v4f32, VR128,
                                   memopv4f32, f128mem, SchedWriteFShuffle.XMM>;

let Predicates = [HasAVX, NoVLX] in {
  def : Pat<(v4i32 (X86Movshdup VR128:$src)),
            (VMOVSHDUPrr VR128:$src)>;
  def : Pat<(v4i32 (X86Movshdup (load addr:$src))),
            (VMOVSHDUPrm addr:$src)>;
  def : Pat<(v4i32 (X86Movsldup VR128:$src)),
            (VMOVSLDUPrr VR128:$src)>;
  def : Pat<(v4i32 (X86Movsldup (load addr:$src))),
            (VMOVSLDUPrm addr:$src)>;
  def : Pat<(v8i32 (X86Movshdup VR256:$src)),
            (VMOVSHDUPYrr VR256:$src)>;
  def : Pat<(v8i32 (X86Movshdup (load addr:$src))),
            (VMOVSHDUPYrm addr:$src)>;
  def : Pat<(v8i32 (X86Movsldup VR256:$src)),
            (VMOVSLDUPYrr VR256:$src)>;
  def : Pat<(v8i32 (X86Movsldup (load addr:$src))),
            (VMOVSLDUPYrm addr:$src)>;
}

let Predicates = [UseSSE3] in {
  def : Pat<(v4i32 (X86Movshdup VR128:$src)),
            (MOVSHDUPrr VR128:$src)>;
  def : Pat<(v4i32 (X86Movshdup (memop addr:$src))),
            (MOVSHDUPrm addr:$src)>;
  def : Pat<(v4i32 (X86Movsldup VR128:$src)),
            (MOVSLDUPrr VR128:$src)>;
  def : Pat<(v4i32 (X86Movsldup (memop addr:$src))),
            (MOVSLDUPrm addr:$src)>;
}

//===---------------------------------------------------------------------===//
// SSE3 - Replicate Double FP - MOVDDUP
//===---------------------------------------------------------------------===//

multiclass sse3_replicate_dfp<string OpcodeStr, X86SchedWriteWidths sched> {
def rr  : S3DI<0x12, MRMSrcReg, (outs VR128:$dst), (ins VR128:$src),
                    !strconcat(OpcodeStr, "\t{$src, $dst|$dst, $src}"),
                    [(set VR128:$dst, (v2f64 (X86Movddup VR128:$src)))]>,
                    Sched<[sched.XMM]>;
def rm  : S3DI<0x12, MRMSrcMem, (outs VR128:$dst), (ins f64mem:$src),
                    !strconcat(OpcodeStr, "\t{$src, $dst|$dst, $src}"),
                    [(set VR128:$dst,
                      (v2f64 (X86Movddup
                              (scalar_to_vector (loadf64 addr:$src)))))]>,
                    Sched<[sched.XMM.Folded]>;
}

// FIXME: Merge with above classes when there are patterns for the ymm version
multiclass sse3_replicate_dfp_y<string OpcodeStr, X86SchedWriteWidths sched> {
def rr  : S3DI<0x12, MRMSrcReg, (outs VR256:$dst), (ins VR256:$src),
                    !strconcat(OpcodeStr, "\t{$src, $dst|$dst, $src}"),
                    [(set VR256:$dst, (v4f64 (X86Movddup VR256:$src)))]>,
                    Sched<[sched.YMM]>;
def rm  : S3DI<0x12, MRMSrcMem, (outs VR256:$dst), (ins f256mem:$src),
                    !strconcat(OpcodeStr, "\t{$src, $dst|$dst, $src}"),
                    [(set VR256:$dst,
                      (v4f64 (X86Movddup (loadv4f64 addr:$src))))]>,
                    Sched<[sched.YMM.Folded]>;
}

let Predicates = [HasAVX, NoVLX] in {
  defm VMOVDDUP  : sse3_replicate_dfp<"vmovddup", SchedWriteFShuffle>,
                                      VEX, VEX_WIG;
  defm VMOVDDUPY : sse3_replicate_dfp_y<"vmovddup", SchedWriteFShuffle>,
                                        VEX, VEX_L, VEX_WIG;
}

defm MOVDDUP : sse3_replicate_dfp<"movddup", SchedWriteFShuffle>;


let Predicates = [HasAVX, NoVLX] in {
  def : Pat<(X86Movddup (loadv2f64 addr:$src)),
            (VMOVDDUPrm addr:$src)>, Requires<[HasAVX]>;
  def : Pat<(X86Movddup (v2f64 (X86vzload addr:$src))),
            (VMOVDDUPrm addr:$src)>, Requires<[HasAVX]>;
}

let Predicates = [UseSSE3] in {
  // No need for aligned memory as this only loads 64-bits.
  def : Pat<(X86Movddup (loadv2f64 addr:$src)),
            (MOVDDUPrm addr:$src)>;
  def : Pat<(X86Movddup (v2f64 (X86vzload addr:$src))),
            (MOVDDUPrm addr:$src)>;
}

//===---------------------------------------------------------------------===//
// SSE3 - Move Unaligned Integer
//===---------------------------------------------------------------------===//

let Predicates = [HasAVX] in {
  def VLDDQUrm : S3DI<0xF0, MRMSrcMem, (outs VR128:$dst), (ins i128mem:$src),
                      "vlddqu\t{$src, $dst|$dst, $src}",
                      [(set VR128:$dst, (int_x86_sse3_ldu_dq addr:$src))]>,
                      Sched<[SchedWriteVecMoveLS.XMM.RM]>, VEX, VEX_WIG;
  def VLDDQUYrm : S3DI<0xF0, MRMSrcMem, (outs VR256:$dst), (ins i256mem:$src),
                       "vlddqu\t{$src, $dst|$dst, $src}",
                       [(set VR256:$dst, (int_x86_avx_ldu_dq_256 addr:$src))]>,
                       Sched<[SchedWriteVecMoveLS.YMM.RM]>, VEX, VEX_L, VEX_WIG;
} // Predicates

def LDDQUrm : S3DI<0xF0, MRMSrcMem, (outs VR128:$dst), (ins i128mem:$src),
                   "lddqu\t{$src, $dst|$dst, $src}",
                   [(set VR128:$dst, (int_x86_sse3_ldu_dq addr:$src))]>,
                   Sched<[SchedWriteVecMoveLS.XMM.RM]>;

//===---------------------------------------------------------------------===//
// SSE3 - Arithmetic
//===---------------------------------------------------------------------===//

multiclass sse3_addsub<string OpcodeStr, ValueType vt, RegisterClass RC,
                       X86MemOperand x86memop, X86FoldableSchedWrite sched,
                       PatFrag ld_frag, bit Is2Addr = 1> {
  def rr : I<0xD0, MRMSrcReg,
       (outs RC:$dst), (ins RC:$src1, RC:$src2),
       !if(Is2Addr,
           !strconcat(OpcodeStr, "\t{$src2, $dst|$dst, $src2}"),
           !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}")),
       [(set RC:$dst, (vt (X86Addsub RC:$src1, RC:$src2)))]>,
       Sched<[sched]>;
  def rm : I<0xD0, MRMSrcMem,
       (outs RC:$dst), (ins RC:$src1, x86memop:$src2),
       !if(Is2Addr,
           !strconcat(OpcodeStr, "\t{$src2, $dst|$dst, $src2}"),
           !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}")),
       [(set RC:$dst, (vt (X86Addsub RC:$src1, (ld_frag addr:$src2))))]>,
       Sched<[sched.Folded, sched.ReadAfterFold]>;
}

let Predicates = [HasAVX] in {
  let ExeDomain = SSEPackedSingle in {
    defm VADDSUBPS : sse3_addsub<"vaddsubps", v4f32, VR128, f128mem,
                                 SchedWriteFAddSizes.PS.XMM, loadv4f32, 0>,
                                 XD, VEX_4V, VEX_WIG;
    defm VADDSUBPSY : sse3_addsub<"vaddsubps", v8f32, VR256, f256mem,
                                  SchedWriteFAddSizes.PS.YMM, loadv8f32, 0>,
                                  XD, VEX_4V, VEX_L, VEX_WIG;
  }
  let ExeDomain = SSEPackedDouble in {
    defm VADDSUBPD : sse3_addsub<"vaddsubpd", v2f64, VR128, f128mem,
                                 SchedWriteFAddSizes.PD.XMM, loadv2f64, 0>,
                                 PD, VEX_4V, VEX_WIG;
    defm VADDSUBPDY : sse3_addsub<"vaddsubpd", v4f64, VR256, f256mem,
                                  SchedWriteFAddSizes.PD.YMM, loadv4f64, 0>,
                                  PD, VEX_4V, VEX_L, VEX_WIG;
  }
}
let Constraints = "$src1 = $dst", Predicates = [UseSSE3] in {
  let ExeDomain = SSEPackedSingle in
  defm ADDSUBPS : sse3_addsub<"addsubps", v4f32, VR128, f128mem,
                              SchedWriteFAddSizes.PS.XMM, memopv4f32>, XD;
  let ExeDomain = SSEPackedDouble in
  defm ADDSUBPD : sse3_addsub<"addsubpd", v2f64, VR128, f128mem,
                              SchedWriteFAddSizes.PD.XMM, memopv2f64>, PD;
}

//===---------------------------------------------------------------------===//
// SSE3 Instructions
//===---------------------------------------------------------------------===//

// Horizontal ops
multiclass S3D_Int<bits<8> o, string OpcodeStr, ValueType vt, RegisterClass RC,
                   X86MemOperand x86memop, SDNode OpNode,
                   X86FoldableSchedWrite sched, PatFrag ld_frag,
                   bit Is2Addr = 1> {
  def rr : S3DI<o, MRMSrcReg, (outs RC:$dst), (ins RC:$src1, RC:$src2),
       !if(Is2Addr,
         !strconcat(OpcodeStr, "\t{$src2, $dst|$dst, $src2}"),
         !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}")),
      [(set RC:$dst, (vt (OpNode RC:$src1, RC:$src2)))]>,
      Sched<[sched]>;

  def rm : S3DI<o, MRMSrcMem, (outs RC:$dst), (ins RC:$src1, x86memop:$src2),
       !if(Is2Addr,
         !strconcat(OpcodeStr, "\t{$src2, $dst|$dst, $src2}"),
         !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}")),
      [(set RC:$dst, (vt (OpNode RC:$src1, (ld_frag addr:$src2))))]>,
      Sched<[sched.Folded, sched.ReadAfterFold]>;
}
multiclass S3_Int<bits<8> o, string OpcodeStr, ValueType vt, RegisterClass RC,
                  X86MemOperand x86memop, SDNode OpNode,
                  X86FoldableSchedWrite sched, PatFrag ld_frag,
                  bit Is2Addr = 1> {
  def rr : S3I<o, MRMSrcReg, (outs RC:$dst), (ins RC:$src1, RC:$src2),
       !if(Is2Addr,
         !strconcat(OpcodeStr, "\t{$src2, $dst|$dst, $src2}"),
         !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}")),
      [(set RC:$dst, (vt (OpNode RC:$src1, RC:$src2)))]>,
        Sched<[sched]>;

  def rm : S3I<o, MRMSrcMem, (outs RC:$dst), (ins RC:$src1, x86memop:$src2),
       !if(Is2Addr,
         !strconcat(OpcodeStr, "\t{$src2, $dst|$dst, $src2}"),
         !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}")),
      [(set RC:$dst, (vt (OpNode RC:$src1, (ld_frag addr:$src2))))]>,
        Sched<[sched.Folded, sched.ReadAfterFold]>;
}

let Predicates = [HasAVX] in {
  let ExeDomain = SSEPackedSingle in {
    defm VHADDPS  : S3D_Int<0x7C, "vhaddps", v4f32, VR128, f128mem,
                            X86fhadd, WriteFHAdd, loadv4f32, 0>, VEX_4V, VEX_WIG;
    defm VHSUBPS  : S3D_Int<0x7D, "vhsubps", v4f32, VR128, f128mem,
                            X86fhsub, WriteFHAdd, loadv4f32, 0>, VEX_4V, VEX_WIG;
    defm VHADDPSY : S3D_Int<0x7C, "vhaddps", v8f32, VR256, f256mem,
                            X86fhadd, WriteFHAddY, loadv8f32, 0>, VEX_4V, VEX_L, VEX_WIG;
    defm VHSUBPSY : S3D_Int<0x7D, "vhsubps", v8f32, VR256, f256mem,
                            X86fhsub, WriteFHAddY, loadv8f32, 0>, VEX_4V, VEX_L, VEX_WIG;
  }
  let ExeDomain = SSEPackedDouble in {
    defm VHADDPD  : S3_Int<0x7C, "vhaddpd", v2f64, VR128, f128mem,
                           X86fhadd, WriteFHAdd, loadv2f64, 0>, VEX_4V, VEX_WIG;
    defm VHSUBPD  : S3_Int<0x7D, "vhsubpd", v2f64, VR128, f128mem,
                           X86fhsub, WriteFHAdd, loadv2f64, 0>, VEX_4V, VEX_WIG;
    defm VHADDPDY : S3_Int<0x7C, "vhaddpd", v4f64, VR256, f256mem,
                           X86fhadd, WriteFHAddY, loadv4f64, 0>, VEX_4V, VEX_L, VEX_WIG;
    defm VHSUBPDY : S3_Int<0x7D, "vhsubpd", v4f64, VR256, f256mem,
                           X86fhsub, WriteFHAddY, loadv4f64, 0>, VEX_4V, VEX_L, VEX_WIG;
  }
}

let Constraints = "$src1 = $dst" in {
  let ExeDomain = SSEPackedSingle in {
    defm HADDPS : S3D_Int<0x7C, "haddps", v4f32, VR128, f128mem, X86fhadd,
                          WriteFHAdd, memopv4f32>;
    defm HSUBPS : S3D_Int<0x7D, "hsubps", v4f32, VR128, f128mem, X86fhsub,
                          WriteFHAdd, memopv4f32>;
  }
  let ExeDomain = SSEPackedDouble in {
    defm HADDPD : S3_Int<0x7C, "haddpd", v2f64, VR128, f128mem, X86fhadd,
                         WriteFHAdd, memopv2f64>;
    defm HSUBPD : S3_Int<0x7D, "hsubpd", v2f64, VR128, f128mem, X86fhsub,
                         WriteFHAdd, memopv2f64>;
  }
}

//===---------------------------------------------------------------------===//
// SSSE3 - Packed Absolute Instructions
//===---------------------------------------------------------------------===//

/// SS3I_unop_rm_int - Simple SSSE3 unary op whose type can be v*{i8,i16,i32}.
multiclass SS3I_unop_rm<bits<8> opc, string OpcodeStr, ValueType vt,
                        SDNode OpNode, X86SchedWriteWidths sched, PatFrag ld_frag> {
  def rr : SS38I<opc, MRMSrcReg, (outs VR128:$dst),
                 (ins VR128:$src),
                 !strconcat(OpcodeStr, "\t{$src, $dst|$dst, $src}"),
                 [(set VR128:$dst, (vt (OpNode VR128:$src)))]>,
                 Sched<[sched.XMM]>;

  def rm : SS38I<opc, MRMSrcMem, (outs VR128:$dst),
                 (ins i128mem:$src),
                 !strconcat(OpcodeStr, "\t{$src, $dst|$dst, $src}"),
                 [(set VR128:$dst,
                   (vt (OpNode (ld_frag addr:$src))))]>,
                 Sched<[sched.XMM.Folded]>;
}

/// SS3I_unop_rm_int_y - Simple SSSE3 unary op whose type can be v*{i8,i16,i32}.
multiclass SS3I_unop_rm_y<bits<8> opc, string OpcodeStr, ValueType vt,
                          SDNode OpNode, X86SchedWriteWidths sched> {
  def Yrr : SS38I<opc, MRMSrcReg, (outs VR256:$dst),
                  (ins VR256:$src),
                  !strconcat(OpcodeStr, "\t{$src, $dst|$dst, $src}"),
                  [(set VR256:$dst, (vt (OpNode VR256:$src)))]>,
                  Sched<[sched.YMM]>;

  def Yrm : SS38I<opc, MRMSrcMem, (outs VR256:$dst),
                  (ins i256mem:$src),
                  !strconcat(OpcodeStr, "\t{$src, $dst|$dst, $src}"),
                  [(set VR256:$dst,
                    (vt (OpNode (load addr:$src))))]>,
                  Sched<[sched.YMM.Folded]>;
}

let Predicates = [HasAVX, NoVLX_Or_NoBWI] in {
  defm VPABSB  : SS3I_unop_rm<0x1C, "vpabsb", v16i8, abs, SchedWriteVecALU,
                              load>, VEX, VEX_WIG;
  defm VPABSW  : SS3I_unop_rm<0x1D, "vpabsw", v8i16, abs, SchedWriteVecALU,
                              load>, VEX, VEX_WIG;
}
let Predicates = [HasAVX, NoVLX] in {
  defm VPABSD  : SS3I_unop_rm<0x1E, "vpabsd", v4i32, abs, SchedWriteVecALU,
                              load>, VEX, VEX_WIG;
}
let Predicates = [HasAVX2, NoVLX_Or_NoBWI] in {
  defm VPABSB  : SS3I_unop_rm_y<0x1C, "vpabsb", v32i8, abs, SchedWriteVecALU>,
                                VEX, VEX_L, VEX_WIG;
  defm VPABSW  : SS3I_unop_rm_y<0x1D, "vpabsw", v16i16, abs, SchedWriteVecALU>,
                                VEX, VEX_L, VEX_WIG;
}
let Predicates = [HasAVX2, NoVLX] in {
  defm VPABSD  : SS3I_unop_rm_y<0x1E, "vpabsd", v8i32, abs, SchedWriteVecALU>,
                                VEX, VEX_L, VEX_WIG;
}

defm PABSB : SS3I_unop_rm<0x1C, "pabsb", v16i8, abs, SchedWriteVecALU,
                          memop>;
defm PABSW : SS3I_unop_rm<0x1D, "pabsw", v8i16, abs, SchedWriteVecALU,
                          memop>;
defm PABSD : SS3I_unop_rm<0x1E, "pabsd", v4i32, abs, SchedWriteVecALU,
                          memop>;

//===---------------------------------------------------------------------===//
// SSSE3 - Packed Binary Operator Instructions
//===---------------------------------------------------------------------===//

/// SS3I_binop_rm - Simple SSSE3 bin op
multiclass SS3I_binop_rm<bits<8> opc, string OpcodeStr, SDNode OpNode,
                         ValueType DstVT, ValueType OpVT, RegisterClass RC,
                         PatFrag memop_frag, X86MemOperand x86memop,
                         X86FoldableSchedWrite sched, bit Is2Addr = 1> {
  let isCommutable = 1 in
  def rr : SS38I<opc, MRMSrcReg, (outs RC:$dst),
       (ins RC:$src1, RC:$src2),
       !if(Is2Addr,
         !strconcat(OpcodeStr, "\t{$src2, $dst|$dst, $src2}"),
         !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}")),
       [(set RC:$dst, (DstVT (OpNode (OpVT RC:$src1), RC:$src2)))]>,
       Sched<[sched]>;
  def rm : SS38I<opc, MRMSrcMem, (outs RC:$dst),
       (ins RC:$src1, x86memop:$src2),
       !if(Is2Addr,
         !strconcat(OpcodeStr, "\t{$src2, $dst|$dst, $src2}"),
         !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}")),
       [(set RC:$dst,
         (DstVT (OpNode (OpVT RC:$src1), (memop_frag addr:$src2))))]>,
       Sched<[sched.Folded, sched.ReadAfterFold]>;
}

/// SS3I_binop_rm_int - Simple SSSE3 bin op whose type can be v*{i8,i16,i32}.
multiclass SS3I_binop_rm_int<bits<8> opc, string OpcodeStr,
                             Intrinsic IntId128, X86FoldableSchedWrite sched,
                             PatFrag ld_frag, bit Is2Addr = 1> {
  let isCommutable = 1 in
  def rr : SS38I<opc, MRMSrcReg, (outs VR128:$dst),
       (ins VR128:$src1, VR128:$src2),
       !if(Is2Addr,
         !strconcat(OpcodeStr, "\t{$src2, $dst|$dst, $src2}"),
         !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}")),
       [(set VR128:$dst, (IntId128 VR128:$src1, VR128:$src2))]>,
       Sched<[sched]>;
  def rm : SS38I<opc, MRMSrcMem, (outs VR128:$dst),
       (ins VR128:$src1, i128mem:$src2),
       !if(Is2Addr,
         !strconcat(OpcodeStr, "\t{$src2, $dst|$dst, $src2}"),
         !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}")),
       [(set VR128:$dst,
         (IntId128 VR128:$src1, (ld_frag addr:$src2)))]>,
       Sched<[sched.Folded, sched.ReadAfterFold]>;
}

multiclass SS3I_binop_rm_int_y<bits<8> opc, string OpcodeStr,
                               Intrinsic IntId256,
                               X86FoldableSchedWrite sched> {
  let isCommutable = 1 in
  def Yrr : SS38I<opc, MRMSrcReg, (outs VR256:$dst),
       (ins VR256:$src1, VR256:$src2),
       !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
       [(set VR256:$dst, (IntId256 VR256:$src1, VR256:$src2))]>,
       Sched<[sched]>;
  def Yrm : SS38I<opc, MRMSrcMem, (outs VR256:$dst),
       (ins VR256:$src1, i256mem:$src2),
       !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
       [(set VR256:$dst,
         (IntId256 VR256:$src1, (load addr:$src2)))]>,
       Sched<[sched.Folded, sched.ReadAfterFold]>;
}

let ImmT = NoImm, Predicates = [HasAVX, NoVLX_Or_NoBWI] in {
let isCommutable = 0 in {
  defm VPSHUFB    : SS3I_binop_rm<0x00, "vpshufb", X86pshufb, v16i8, v16i8,
                                  VR128, load, i128mem,
                                  SchedWriteVarShuffle.XMM, 0>, VEX_4V, VEX_WIG;
  defm VPMADDUBSW : SS3I_binop_rm<0x04, "vpmaddubsw", X86vpmaddubsw, v8i16,
                                  v16i8, VR128, load, i128mem,
                                  SchedWriteVecIMul.XMM, 0>, VEX_4V, VEX_WIG;
}
defm VPMULHRSW    : SS3I_binop_rm<0x0B, "vpmulhrsw", X86mulhrs, v8i16, v8i16,
                                  VR128, load, i128mem,
                                  SchedWriteVecIMul.XMM, 0>, VEX_4V, VEX_WIG;
}

let ImmT = NoImm, Predicates = [HasAVX] in {
let isCommutable = 0 in {
  defm VPHADDW    : SS3I_binop_rm<0x01, "vphaddw", X86hadd, v8i16, v8i16, VR128,
                                  load, i128mem,
                                  SchedWritePHAdd.XMM, 0>, VEX_4V, VEX_WIG;
  defm VPHADDD    : SS3I_binop_rm<0x02, "vphaddd", X86hadd, v4i32, v4i32, VR128,
                                  load, i128mem,
                                  SchedWritePHAdd.XMM, 0>, VEX_4V, VEX_WIG;
  defm VPHSUBW    : SS3I_binop_rm<0x05, "vphsubw", X86hsub, v8i16, v8i16, VR128,
                                  load, i128mem,
                                  SchedWritePHAdd.XMM, 0>, VEX_4V, VEX_WIG;
  defm VPHSUBD    : SS3I_binop_rm<0x06, "vphsubd", X86hsub, v4i32, v4i32, VR128,
                                  load, i128mem,
                                  SchedWritePHAdd.XMM, 0>, VEX_4V;
  defm VPSIGNB    : SS3I_binop_rm_int<0x08, "vpsignb",
                                      int_x86_ssse3_psign_b_128,
                                      SchedWriteVecALU.XMM, load, 0>, VEX_4V, VEX_WIG;
  defm VPSIGNW    : SS3I_binop_rm_int<0x09, "vpsignw",
                                      int_x86_ssse3_psign_w_128,
                                      SchedWriteVecALU.XMM, load, 0>, VEX_4V, VEX_WIG;
  defm VPSIGND    : SS3I_binop_rm_int<0x0A, "vpsignd",
                                      int_x86_ssse3_psign_d_128,
                                      SchedWriteVecALU.XMM, load, 0>, VEX_4V, VEX_WIG;
  defm VPHADDSW   : SS3I_binop_rm_int<0x03, "vphaddsw",
                                      int_x86_ssse3_phadd_sw_128,
                                      SchedWritePHAdd.XMM, load, 0>, VEX_4V, VEX_WIG;
  defm VPHSUBSW   : SS3I_binop_rm_int<0x07, "vphsubsw",
                                      int_x86_ssse3_phsub_sw_128,
                                      SchedWritePHAdd.XMM, load, 0>, VEX_4V, VEX_WIG;
}
}

let ImmT = NoImm, Predicates = [HasAVX2, NoVLX_Or_NoBWI] in {
let isCommutable = 0 in {
  defm VPSHUFBY   : SS3I_binop_rm<0x00, "vpshufb", X86pshufb, v32i8, v32i8,
                                  VR256, load, i256mem,
                                  SchedWriteVarShuffle.YMM, 0>, VEX_4V, VEX_L, VEX_WIG;
  defm VPMADDUBSWY : SS3I_binop_rm<0x04, "vpmaddubsw", X86vpmaddubsw, v16i16,
                                   v32i8, VR256, load, i256mem,
                                   SchedWriteVecIMul.YMM, 0>, VEX_4V, VEX_L, VEX_WIG;
}
defm VPMULHRSWY   : SS3I_binop_rm<0x0B, "vpmulhrsw", X86mulhrs, v16i16, v16i16,
                                  VR256, load, i256mem,
                                  SchedWriteVecIMul.YMM, 0>, VEX_4V, VEX_L, VEX_WIG;
}

let ImmT = NoImm, Predicates = [HasAVX2] in {
let isCommutable = 0 in {
  defm VPHADDWY   : SS3I_binop_rm<0x01, "vphaddw", X86hadd, v16i16, v16i16,
                                  VR256, load, i256mem,
                                  SchedWritePHAdd.YMM, 0>, VEX_4V, VEX_L, VEX_WIG;
  defm VPHADDDY   : SS3I_binop_rm<0x02, "vphaddd", X86hadd, v8i32, v8i32, VR256,
                                  load, i256mem,
                                  SchedWritePHAdd.YMM, 0>, VEX_4V, VEX_L, VEX_WIG;
  defm VPHSUBWY   : SS3I_binop_rm<0x05, "vphsubw", X86hsub, v16i16, v16i16,
                                  VR256, load, i256mem,
                                  SchedWritePHAdd.YMM, 0>, VEX_4V, VEX_L, VEX_WIG;
  defm VPHSUBDY   : SS3I_binop_rm<0x06, "vphsubd", X86hsub, v8i32, v8i32, VR256,
                                  load, i256mem,
                                  SchedWritePHAdd.YMM, 0>, VEX_4V, VEX_L;
  defm VPSIGNB   : SS3I_binop_rm_int_y<0x08, "vpsignb", int_x86_avx2_psign_b,
                                       SchedWriteVecALU.YMM>, VEX_4V, VEX_L, VEX_WIG;
  defm VPSIGNW   : SS3I_binop_rm_int_y<0x09, "vpsignw", int_x86_avx2_psign_w,
                                       SchedWriteVecALU.YMM>, VEX_4V, VEX_L, VEX_WIG;
  defm VPSIGND   : SS3I_binop_rm_int_y<0x0A, "vpsignd", int_x86_avx2_psign_d,
                                       SchedWriteVecALU.YMM>, VEX_4V, VEX_L, VEX_WIG;
  defm VPHADDSW  : SS3I_binop_rm_int_y<0x03, "vphaddsw",
                                       int_x86_avx2_phadd_sw,
                                       SchedWritePHAdd.YMM>, VEX_4V, VEX_L, VEX_WIG;
  defm VPHSUBSW  : SS3I_binop_rm_int_y<0x07, "vphsubsw",
                                       int_x86_avx2_phsub_sw,
                                       SchedWritePHAdd.YMM>, VEX_4V, VEX_L, VEX_WIG;
}
}

// None of these have i8 immediate fields.
let ImmT = NoImm, Constraints = "$src1 = $dst" in {
let isCommutable = 0 in {
  defm PHADDW    : SS3I_binop_rm<0x01, "phaddw", X86hadd, v8i16, v8i16, VR128,
                                 memop, i128mem, SchedWritePHAdd.XMM>;
  defm PHADDD    : SS3I_binop_rm<0x02, "phaddd", X86hadd, v4i32, v4i32, VR128,
                                 memop, i128mem, SchedWritePHAdd.XMM>;
  defm PHSUBW    : SS3I_binop_rm<0x05, "phsubw", X86hsub, v8i16, v8i16, VR128,
                                 memop, i128mem, SchedWritePHAdd.XMM>;
  defm PHSUBD    : SS3I_binop_rm<0x06, "phsubd", X86hsub, v4i32, v4i32, VR128,
                                 memop, i128mem, SchedWritePHAdd.XMM>;
  defm PSIGNB    : SS3I_binop_rm_int<0x08, "psignb", int_x86_ssse3_psign_b_128,
                                     SchedWriteVecALU.XMM, memop>;
  defm PSIGNW    : SS3I_binop_rm_int<0x09, "psignw", int_x86_ssse3_psign_w_128,
                                     SchedWriteVecALU.XMM, memop>;
  defm PSIGND    : SS3I_binop_rm_int<0x0A, "psignd", int_x86_ssse3_psign_d_128,
                                     SchedWriteVecALU.XMM, memop>;
  defm PSHUFB    : SS3I_binop_rm<0x00, "pshufb", X86pshufb, v16i8, v16i8, VR128,
                                 memop, i128mem, SchedWriteVarShuffle.XMM>;
  defm PHADDSW   : SS3I_binop_rm_int<0x03, "phaddsw",
                                     int_x86_ssse3_phadd_sw_128,
                                     SchedWritePHAdd.XMM, memop>;
  defm PHSUBSW   : SS3I_binop_rm_int<0x07, "phsubsw",
                                     int_x86_ssse3_phsub_sw_128,
                                     SchedWritePHAdd.XMM, memop>;
  defm PMADDUBSW : SS3I_binop_rm<0x04, "pmaddubsw", X86vpmaddubsw, v8i16,
                                 v16i8, VR128, memop, i128mem,
                                 SchedWriteVecIMul.XMM>;
}
defm PMULHRSW    : SS3I_binop_rm<0x0B, "pmulhrsw", X86mulhrs, v8i16, v8i16,
                                 VR128, memop, i128mem, SchedWriteVecIMul.XMM>;
}

//===---------------------------------------------------------------------===//
// SSSE3 - Packed Align Instruction Patterns
//===---------------------------------------------------------------------===//

multiclass ssse3_palignr<string asm, ValueType VT, RegisterClass RC,
                         PatFrag memop_frag, X86MemOperand x86memop,
                         X86FoldableSchedWrite sched, bit Is2Addr = 1> {
  let hasSideEffects = 0 in {
  def rri : SS3AI<0x0F, MRMSrcReg, (outs RC:$dst),
      (ins RC:$src1, RC:$src2, u8imm:$src3),
      !if(Is2Addr,
        !strconcat(asm, "\t{$src3, $src2, $dst|$dst, $src2, $src3}"),
        !strconcat(asm,
                  "\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}")),
      [(set RC:$dst, (VT (X86PAlignr RC:$src1, RC:$src2, (i8 imm:$src3))))]>,
      Sched<[sched]>;
  let mayLoad = 1 in
  def rmi : SS3AI<0x0F, MRMSrcMem, (outs RC:$dst),
      (ins RC:$src1, x86memop:$src2, u8imm:$src3),
      !if(Is2Addr,
        !strconcat(asm, "\t{$src3, $src2, $dst|$dst, $src2, $src3}"),
        !strconcat(asm,
                  "\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}")),
      [(set RC:$dst, (VT (X86PAlignr RC:$src1,
                                     (memop_frag addr:$src2),
                                     (i8 imm:$src3))))]>,
      Sched<[sched.Folded, sched.ReadAfterFold]>;
  }
}

let Predicates = [HasAVX, NoVLX_Or_NoBWI] in
  defm VPALIGNR : ssse3_palignr<"vpalignr", v16i8, VR128, load, i128mem,
                                SchedWriteShuffle.XMM, 0>, VEX_4V, VEX_WIG;
let Predicates = [HasAVX2, NoVLX_Or_NoBWI] in
  defm VPALIGNRY : ssse3_palignr<"vpalignr", v32i8, VR256, load, i256mem,
                                 SchedWriteShuffle.YMM, 0>, VEX_4V, VEX_L, VEX_WIG;
let Constraints = "$src1 = $dst", Predicates = [UseSSSE3] in
  defm PALIGNR : ssse3_palignr<"palignr", v16i8, VR128, memop, i128mem,
                               SchedWriteShuffle.XMM>;

//===---------------------------------------------------------------------===//
// SSSE3 - Thread synchronization
//===---------------------------------------------------------------------===//

let SchedRW = [WriteSystem] in {
let usesCustomInserter = 1 in {
def MONITOR : PseudoI<(outs), (ins i32mem:$src1, GR32:$src2, GR32:$src3),
                [(int_x86_sse3_monitor addr:$src1, GR32:$src2, GR32:$src3)]>,
                Requires<[HasSSE3]>;
}

let Uses = [EAX, ECX, EDX] in
def MONITORrrr : I<0x01, MRM_C8, (outs), (ins), "monitor", []>,
                   TB, Requires<[HasSSE3]>;

let Uses = [ECX, EAX] in
def MWAITrr   : I<0x01, MRM_C9, (outs), (ins), "mwait",
                  [(int_x86_sse3_mwait ECX, EAX)]>, TB, Requires<[HasSSE3]>;
} // SchedRW

def : InstAlias<"mwait\t{%eax, %ecx|ecx, eax}", (MWAITrr)>, Requires<[Not64BitMode]>;
def : InstAlias<"mwait\t{%rax, %rcx|rcx, rax}", (MWAITrr)>, Requires<[In64BitMode]>;

def : InstAlias<"monitor\t{%eax, %ecx, %edx|edx, ecx, eax}", (MONITORrrr)>,
      Requires<[Not64BitMode]>;
def : InstAlias<"monitor\t{%rax, %rcx, %rdx|rdx, rcx, rax}", (MONITORrrr)>,
      Requires<[In64BitMode]>;

//===----------------------------------------------------------------------===//
// SSE4.1 - Packed Move with Sign/Zero Extend
//===----------------------------------------------------------------------===//

multiclass SS41I_pmovx_rrrm<bits<8> opc, string OpcodeStr, X86MemOperand MemOp,
                            RegisterClass OutRC, RegisterClass InRC,
                            X86FoldableSchedWrite sched> {
  def rr : SS48I<opc, MRMSrcReg, (outs OutRC:$dst), (ins InRC:$src),
                 !strconcat(OpcodeStr, "\t{$src, $dst|$dst, $src}"), []>,
                 Sched<[sched]>;

  def rm : SS48I<opc, MRMSrcMem, (outs OutRC:$dst), (ins MemOp:$src),
                 !strconcat(OpcodeStr, "\t{$src, $dst|$dst, $src}"), []>,
                 Sched<[sched.Folded]>;
}

multiclass SS41I_pmovx_rm_all<bits<8> opc, string OpcodeStr,
                              X86MemOperand MemOp, X86MemOperand MemYOp,
                              Predicate prd> {
  defm NAME : SS41I_pmovx_rrrm<opc, OpcodeStr, MemOp, VR128, VR128,
                               SchedWriteShuffle.XMM>;
  let Predicates = [HasAVX, prd] in
    defm V#NAME   : SS41I_pmovx_rrrm<opc, !strconcat("v", OpcodeStr), MemOp,
                                     VR128, VR128, SchedWriteShuffle.XMM>,
                                     VEX, VEX_WIG;
  let Predicates = [HasAVX2, prd] in
    defm V#NAME#Y : SS41I_pmovx_rrrm<opc, !strconcat("v", OpcodeStr), MemYOp,
                                     VR256, VR128, WriteShuffle256>,
                                     VEX, VEX_L, VEX_WIG;
}

multiclass SS41I_pmovx_rm<bits<8> opc, string OpcodeStr, X86MemOperand MemOp,
                          X86MemOperand MemYOp, Predicate prd> {
  defm PMOVSX#NAME : SS41I_pmovx_rm_all<opc, !strconcat("pmovsx", OpcodeStr),
                                        MemOp, MemYOp, prd>;
  defm PMOVZX#NAME : SS41I_pmovx_rm_all<!add(opc, 0x10),
                                        !strconcat("pmovzx", OpcodeStr),
                                        MemOp, MemYOp, prd>;
}

defm BW : SS41I_pmovx_rm<0x20, "bw", i64mem, i128mem, NoVLX_Or_NoBWI>;
defm WD : SS41I_pmovx_rm<0x23, "wd", i64mem, i128mem, NoVLX>;
defm DQ : SS41I_pmovx_rm<0x25, "dq", i64mem, i128mem, NoVLX>;

defm BD : SS41I_pmovx_rm<0x21, "bd", i32mem, i64mem, NoVLX>;
defm WQ : SS41I_pmovx_rm<0x24, "wq", i32mem, i64mem, NoVLX>;

defm BQ : SS41I_pmovx_rm<0x22, "bq", i16mem, i32mem, NoVLX>;

// Patterns that we also need for any_extend.
// Any_extend_vector_inreg is currently legalized to zero_extend_vector_inreg.
multiclass SS41I_pmovx_avx2_patterns_base<string OpcPrefix, SDNode ExtOp> {
  // Register-Register patterns
  let Predicates = [HasAVX2, NoVLX_Or_NoBWI] in {
    def : Pat<(v16i16 (ExtOp (v16i8 VR128:$src))),
              (!cast<I>(OpcPrefix#BWYrr) VR128:$src)>;
  }

  let Predicates = [HasAVX2, NoVLX] in {
    def : Pat<(v8i32 (ExtOp (v8i16 VR128:$src))),
              (!cast<I>(OpcPrefix#WDYrr) VR128:$src)>;

    def : Pat<(v4i64 (ExtOp (v4i32 VR128:$src))),
              (!cast<I>(OpcPrefix#DQYrr) VR128:$src)>;
  }

  // AVX2 Register-Memory patterns
  let Predicates = [HasAVX2, NoVLX_Or_NoBWI] in {
    def : Pat<(v16i16 (ExtOp (loadv16i8 addr:$src))),
              (!cast<I>(OpcPrefix#BWYrm) addr:$src)>;
    def : Pat<(v16i16 (ExtOp (v16i8 (vzmovl_v2i64 addr:$src)))),
              (!cast<I>(OpcPrefix#BWYrm) addr:$src)>;
    def : Pat<(v16i16 (ExtOp (v16i8 (vzload_v2i64 addr:$src)))),
              (!cast<I>(OpcPrefix#BWYrm) addr:$src)>;
  }

  let Predicates = [HasAVX2, NoVLX] in {
    def : Pat<(v8i32 (ExtOp (loadv8i16 addr:$src))),
              (!cast<I>(OpcPrefix#WDYrm) addr:$src)>;
    def : Pat<(v8i32 (ExtOp (v8i16 (vzmovl_v2i64 addr:$src)))),
              (!cast<I>(OpcPrefix#WDYrm) addr:$src)>;
    def : Pat<(v8i32 (ExtOp (v8i16 (vzload_v2i64 addr:$src)))),
              (!cast<I>(OpcPrefix#WDYrm) addr:$src)>;

    def : Pat<(v4i64 (ExtOp (loadv4i32 addr:$src))),
              (!cast<I>(OpcPrefix#DQYrm) addr:$src)>;
    def : Pat<(v4i64 (ExtOp (v4i32 (vzmovl_v2i64 addr:$src)))),
              (!cast<I>(OpcPrefix#DQYrm) addr:$src)>;
    def : Pat<(v4i64 (ExtOp (v4i32 (vzload_v2i64 addr:$src)))),
              (!cast<I>(OpcPrefix#DQYrm) addr:$src)>;
  }
}

// AVX2 Patterns
multiclass SS41I_pmovx_avx2_patterns<string OpcPrefix, string ExtTy,
                                     SDNode ExtOp, SDNode InVecOp> :
    SS41I_pmovx_avx2_patterns_base<OpcPrefix, ExtOp> {

  // Register-Register patterns
  let Predicates = [HasAVX2, NoVLX] in {
  def : Pat<(v8i32 (InVecOp (v16i8 VR128:$src))),
            (!cast<I>(OpcPrefix#BDYrr) VR128:$src)>;
  def : Pat<(v4i64 (InVecOp (v16i8 VR128:$src))),
            (!cast<I>(OpcPrefix#BQYrr) VR128:$src)>;

  def : Pat<(v4i64 (InVecOp (v8i16 VR128:$src))),
            (!cast<I>(OpcPrefix#WQYrr) VR128:$src)>;
  }

  // Simple Register-Memory patterns
  let Predicates = [HasAVX2, NoVLX_Or_NoBWI] in {
  def : Pat<(v16i16 (!cast<PatFrag>(ExtTy#"extloadvi8") addr:$src)),
            (!cast<I>(OpcPrefix#BWYrm) addr:$src)>;
  }
  let Predicates = [HasAVX2, NoVLX] in {
  def : Pat<(v8i32 (!cast<PatFrag>(ExtTy#"extloadvi8") addr:$src)),
            (!cast<I>(OpcPrefix#BDYrm) addr:$src)>;
  def : Pat<(v4i64 (!cast<PatFrag>(ExtTy#"extloadvi8") addr:$src)),
            (!cast<I>(OpcPrefix#BQYrm) addr:$src)>;

  def : Pat<(v8i32 (!cast<PatFrag>(ExtTy#"extloadvi16") addr:$src)),
            (!cast<I>(OpcPrefix#WDYrm) addr:$src)>;
  def : Pat<(v4i64 (!cast<PatFrag>(ExtTy#"extloadvi16") addr:$src)),
            (!cast<I>(OpcPrefix#WQYrm) addr:$src)>;

  def : Pat<(v4i64 (!cast<PatFrag>(ExtTy#"extloadvi32") addr:$src)),
            (!cast<I>(OpcPrefix#DQYrm) addr:$src)>;
  }

  // AVX2 Register-Memory patterns
  let Predicates = [HasAVX2, NoVLX] in {
  def : Pat<(v8i32 (InVecOp (bc_v16i8 (v2i64 (scalar_to_vector (loadi64 addr:$src)))))),
            (!cast<I>(OpcPrefix#BDYrm) addr:$src)>;
  def : Pat<(v8i32 (InVecOp (v16i8 (vzmovl_v2i64 addr:$src)))),
            (!cast<I>(OpcPrefix#BDYrm) addr:$src)>;
  def : Pat<(v8i32 (InVecOp (v16i8 (vzload_v2i64 addr:$src)))),
            (!cast<I>(OpcPrefix#BDYrm) addr:$src)>;
  def : Pat<(v8i32 (InVecOp (loadv16i8 addr:$src))),
            (!cast<I>(OpcPrefix#BDYrm) addr:$src)>;

  def : Pat<(v4i64 (InVecOp (bc_v16i8 (v4i32 (scalar_to_vector (loadi32 addr:$src)))))),
            (!cast<I>(OpcPrefix#BQYrm) addr:$src)>;
  def : Pat<(v4i64 (InVecOp (v16i8 (vzmovl_v4i32 addr:$src)))),
            (!cast<I>(OpcPrefix#BQYrm) addr:$src)>;
  def : Pat<(v4i64 (InVecOp (v16i8 (vzload_v2i64 addr:$src)))),
            (!cast<I>(OpcPrefix#BQYrm) addr:$src)>;
  def : Pat<(v4i64 (InVecOp (loadv16i8 addr:$src))),
            (!cast<I>(OpcPrefix#BQYrm) addr:$src)>;

  def : Pat<(v4i64 (InVecOp (bc_v8i16 (v2i64 (scalar_to_vector (loadi64 addr:$src)))))),
            (!cast<I>(OpcPrefix#WQYrm) addr:$src)>;
  def : Pat<(v4i64 (InVecOp (v8i16 (vzmovl_v2i64 addr:$src)))),
            (!cast<I>(OpcPrefix#WQYrm) addr:$src)>;
  def : Pat<(v4i64 (InVecOp (v8i16 (vzload_v2i64 addr:$src)))),
            (!cast<I>(OpcPrefix#WQYrm) addr:$src)>;
  def : Pat<(v4i64 (InVecOp (loadv8i16 addr:$src))),
            (!cast<I>(OpcPrefix#WQYrm) addr:$src)>;
  }
}

defm : SS41I_pmovx_avx2_patterns<"VPMOVSX", "s", sext, sext_invec>;
defm : SS41I_pmovx_avx2_patterns<"VPMOVZX", "z", zext, zext_invec>;
defm : SS41I_pmovx_avx2_patterns_base<"VPMOVZX", anyext>;

// SSE4.1/AVX patterns.
multiclass SS41I_pmovx_patterns<string OpcPrefix, string ExtTy,
                                SDNode ExtOp> {
  let Predicates = [HasAVX, NoVLX_Or_NoBWI] in {
  def : Pat<(v8i16 (ExtOp (v16i8 VR128:$src))),
            (!cast<I>(OpcPrefix#BWrr) VR128:$src)>;
  }
  let Predicates = [HasAVX, NoVLX] in {
  def : Pat<(v4i32 (ExtOp (v16i8 VR128:$src))),
            (!cast<I>(OpcPrefix#BDrr) VR128:$src)>;
  def : Pat<(v2i64 (ExtOp (v16i8 VR128:$src))),
            (!cast<I>(OpcPrefix#BQrr) VR128:$src)>;

  def : Pat<(v4i32 (ExtOp (v8i16 VR128:$src))),
            (!cast<I>(OpcPrefix#WDrr) VR128:$src)>;
  def : Pat<(v2i64 (ExtOp (v8i16 VR128:$src))),
            (!cast<I>(OpcPrefix#WQrr) VR128:$src)>;

  def : Pat<(v2i64 (ExtOp (v4i32 VR128:$src))),
            (!cast<I>(OpcPrefix#DQrr) VR128:$src)>;
  }
  let Predicates = [HasAVX, NoVLX_Or_NoBWI] in {
  def : Pat<(v8i16 (!cast<PatFrag>(ExtTy#"extloadvi8") addr:$src)),
            (!cast<I>(OpcPrefix#BWrm) addr:$src)>;
  }
  let Predicates = [HasAVX, NoVLX] in {
  def : Pat<(v4i32 (!cast<PatFrag>(ExtTy#"extloadvi8") addr:$src)),
            (!cast<I>(OpcPrefix#BDrm) addr:$src)>;
  def : Pat<(v2i64 (!cast<PatFrag>(ExtTy#"extloadvi8") addr:$src)),
            (!cast<I>(OpcPrefix#BQrm) addr:$src)>;

  def : Pat<(v4i32 (!cast<PatFrag>(ExtTy#"extloadvi16") addr:$src)),
            (!cast<I>(OpcPrefix#WDrm) addr:$src)>;
  def : Pat<(v2i64 (!cast<PatFrag>(ExtTy#"extloadvi16") addr:$src)),
            (!cast<I>(OpcPrefix#WQrm) addr:$src)>;

  def : Pat<(v2i64 (!cast<PatFrag>(ExtTy#"extloadvi32") addr:$src)),
            (!cast<I>(OpcPrefix#DQrm) addr:$src)>;
  }
  let Predicates = [HasAVX, NoVLX_Or_NoBWI] in {
  def : Pat<(v8i16 (ExtOp (bc_v16i8 (v2i64 (scalar_to_vector (loadi64 addr:$src)))))),
            (!cast<I>(OpcPrefix#BWrm) addr:$src)>;
  def : Pat<(v8i16 (ExtOp (bc_v16i8 (v2f64 (scalar_to_vector (loadf64 addr:$src)))))),
            (!cast<I>(OpcPrefix#BWrm) addr:$src)>;
  def : Pat<(v8i16 (ExtOp (v16i8 (vzmovl_v2i64 addr:$src)))),
            (!cast<I>(OpcPrefix#BWrm) addr:$src)>;
  def : Pat<(v8i16 (ExtOp (v16i8 (vzload_v2i64 addr:$src)))),
            (!cast<I>(OpcPrefix#BWrm) addr:$src)>;
  def : Pat<(v8i16 (ExtOp (loadv16i8 addr:$src))),
            (!cast<I>(OpcPrefix#BWrm) addr:$src)>;
  }
  let Predicates = [HasAVX, NoVLX] in {
  def : Pat<(v4i32 (ExtOp (bc_v16i8 (v4i32 (scalar_to_vector (loadi32 addr:$src)))))),
            (!cast<I>(OpcPrefix#BDrm) addr:$src)>;
  def : Pat<(v4i32 (ExtOp (v16i8 (vzmovl_v4i32 addr:$src)))),
            (!cast<I>(OpcPrefix#BDrm) addr:$src)>;
  def : Pat<(v4i32 (ExtOp (v16i8 (vzload_v2i64 addr:$src)))),
            (!cast<I>(OpcPrefix#BDrm) addr:$src)>;
  def : Pat<(v4i32 (ExtOp (loadv16i8 addr:$src))),
            (!cast<I>(OpcPrefix#BDrm) addr:$src)>;

  def : Pat<(v2i64 (ExtOp (bc_v16i8 (v4i32 (scalar_to_vector (extloadi32i16 addr:$src)))))),
            (!cast<I>(OpcPrefix#BQrm) addr:$src)>;
  def : Pat<(v2i64 (ExtOp (v16i8 (vzmovl_v4i32 addr:$src)))),
            (!cast<I>(OpcPrefix#BQrm) addr:$src)>;
  def : Pat<(v2i64 (ExtOp (v16i8 (vzload_v2i64 addr:$src)))),
            (!cast<I>(OpcPrefix#BQrm) addr:$src)>;
  def : Pat<(v2i64 (ExtOp (loadv16i8 addr:$src))),
            (!cast<I>(OpcPrefix#BQrm) addr:$src)>;

  def : Pat<(v4i32 (ExtOp (bc_v8i16 (v2i64 (scalar_to_vector (loadi64 addr:$src)))))),
            (!cast<I>(OpcPrefix#WDrm) addr:$src)>;
  def : Pat<(v4i32 (ExtOp (bc_v8i16 (v2f64 (scalar_to_vector (loadf64 addr:$src)))))),
            (!cast<I>(OpcPrefix#WDrm) addr:$src)>;
  def : Pat<(v4i32 (ExtOp (v8i16 (vzmovl_v2i64 addr:$src)))),
            (!cast<I>(OpcPrefix#WDrm) addr:$src)>;
  def : Pat<(v4i32 (ExtOp (v8i16 (vzload_v2i64 addr:$src)))),
            (!cast<I>(OpcPrefix#WDrm) addr:$src)>;
  def : Pat<(v4i32 (ExtOp (loadv8i16 addr:$src))),
            (!cast<I>(OpcPrefix#WDrm) addr:$src)>;

  def : Pat<(v2i64 (ExtOp (bc_v8i16 (v4i32 (scalar_to_vector (loadi32 addr:$src)))))),
            (!cast<I>(OpcPrefix#WQrm) addr:$src)>;
  def : Pat<(v2i64 (ExtOp (v8i16 (vzmovl_v4i32 addr:$src)))),
            (!cast<I>(OpcPrefix#WQrm) addr:$src)>;
  def : Pat<(v2i64 (ExtOp (v8i16 (vzload_v2i64 addr:$src)))),
            (!cast<I>(OpcPrefix#WQrm) addr:$src)>;
  def : Pat<(v2i64 (ExtOp (loadv8i16 addr:$src))),
            (!cast<I>(OpcPrefix#WQrm) addr:$src)>;

  def : Pat<(v2i64 (ExtOp (bc_v4i32 (v2i64 (scalar_to_vector (loadi64 addr:$src)))))),
            (!cast<I>(OpcPrefix#DQrm) addr:$src)>;
  def : Pat<(v2i64 (ExtOp (bc_v4i32 (v2f64 (scalar_to_vector (loadf64 addr:$src)))))),
            (!cast<I>(OpcPrefix#DQrm) addr:$src)>;
  def : Pat<(v2i64 (ExtOp (v4i32 (vzmovl_v2i64 addr:$src)))),
            (!cast<I>(OpcPrefix#DQrm) addr:$src)>;
  def : Pat<(v2i64 (ExtOp (v4i32 (vzload_v2i64 addr:$src)))),
            (!cast<I>(OpcPrefix#DQrm) addr:$src)>;
  def : Pat<(v2i64 (ExtOp (loadv4i32 addr:$src))),
            (!cast<I>(OpcPrefix#DQrm) addr:$src)>;
  }
}

defm : SS41I_pmovx_patterns<"VPMOVSX", "s", sext_invec>;
defm : SS41I_pmovx_patterns<"VPMOVZX", "z", zext_invec>;

let Predicates = [UseSSE41] in {
  defm : SS41I_pmovx_patterns<"PMOVSX", "s", sext_invec>;
  defm : SS41I_pmovx_patterns<"PMOVZX", "z", zext_invec>;
}

//===----------------------------------------------------------------------===//
// SSE4.1 - Extract Instructions
//===----------------------------------------------------------------------===//

/// SS41I_binop_ext8 - SSE 4.1 extract 8 bits to 32 bit reg or 8 bit mem
multiclass SS41I_extract8<bits<8> opc, string OpcodeStr> {
  def rr : SS4AIi8<opc, MRMDestReg, (outs GR32orGR64:$dst),
                 (ins VR128:$src1, u8imm:$src2),
                 !strconcat(OpcodeStr,
                            "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
                 [(set GR32orGR64:$dst, (X86pextrb (v16i8 VR128:$src1),
                                         imm:$src2))]>,
                  Sched<[WriteVecExtract]>;
  let hasSideEffects = 0, mayStore = 1 in
  def mr : SS4AIi8<opc, MRMDestMem, (outs),
                 (ins i8mem:$dst, VR128:$src1, u8imm:$src2),
                 !strconcat(OpcodeStr,
                            "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
                 [(store (i8 (trunc (X86pextrb (v16i8 VR128:$src1), imm:$src2))),
                          addr:$dst)]>, Sched<[WriteVecExtractSt]>;
}

let Predicates = [HasAVX, NoBWI] in
  defm VPEXTRB : SS41I_extract8<0x14, "vpextrb">, VEX;

defm PEXTRB      : SS41I_extract8<0x14, "pextrb">;


/// SS41I_extract16 - SSE 4.1 extract 16 bits to memory destination
multiclass SS41I_extract16<bits<8> opc, string OpcodeStr> {
  let isCodeGenOnly = 1, ForceDisassemble = 1, hasSideEffects = 0 in
  def rr_REV : SS4AIi8<opc, MRMDestReg, (outs GR32orGR64:$dst),
                   (ins VR128:$src1, u8imm:$src2),
                   !strconcat(OpcodeStr,
                   "\t{$src2, $src1, $dst|$dst, $src1, $src2}"), []>,
                   Sched<[WriteVecExtract]>, FoldGenData<NAME#rr>;

  let hasSideEffects = 0, mayStore = 1 in
  def mr : SS4AIi8<opc, MRMDestMem, (outs),
                 (ins i16mem:$dst, VR128:$src1, u8imm:$src2),
                 !strconcat(OpcodeStr,
                  "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
                 [(store (i16 (trunc (X86pextrw (v8i16 VR128:$src1), imm:$src2))),
                          addr:$dst)]>, Sched<[WriteVecExtractSt]>;
}

let Predicates = [HasAVX, NoBWI] in
  defm VPEXTRW : SS41I_extract16<0x15, "vpextrw">, VEX;

defm PEXTRW      : SS41I_extract16<0x15, "pextrw">;


/// SS41I_extract32 - SSE 4.1 extract 32 bits to int reg or memory destination
multiclass SS41I_extract32<bits<8> opc, string OpcodeStr> {
  def rr : SS4AIi8<opc, MRMDestReg, (outs GR32:$dst),
                 (ins VR128:$src1, u8imm:$src2),
                 !strconcat(OpcodeStr,
                  "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
                 [(set GR32:$dst,
                  (extractelt (v4i32 VR128:$src1), imm:$src2))]>,
                  Sched<[WriteVecExtract]>;
  def mr : SS4AIi8<opc, MRMDestMem, (outs),
                 (ins i32mem:$dst, VR128:$src1, u8imm:$src2),
                 !strconcat(OpcodeStr,
                  "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
                 [(store (extractelt (v4i32 VR128:$src1), imm:$src2),
                          addr:$dst)]>, Sched<[WriteVecExtractSt]>;
}

let Predicates = [HasAVX, NoDQI] in
  defm VPEXTRD : SS41I_extract32<0x16, "vpextrd">, VEX;

defm PEXTRD      : SS41I_extract32<0x16, "pextrd">;

/// SS41I_extract32 - SSE 4.1 extract 32 bits to int reg or memory destination
multiclass SS41I_extract64<bits<8> opc, string OpcodeStr> {
  def rr : SS4AIi8<opc, MRMDestReg, (outs GR64:$dst),
                 (ins VR128:$src1, u8imm:$src2),
                 !strconcat(OpcodeStr,
                  "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
                 [(set GR64:$dst,
                  (extractelt (v2i64 VR128:$src1), imm:$src2))]>,
                  Sched<[WriteVecExtract]>;
  def mr : SS4AIi8<opc, MRMDestMem, (outs),
                 (ins i64mem:$dst, VR128:$src1, u8imm:$src2),
                 !strconcat(OpcodeStr,
                  "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
                 [(store (extractelt (v2i64 VR128:$src1), imm:$src2),
                          addr:$dst)]>, Sched<[WriteVecExtractSt]>;
}

let Predicates = [HasAVX, NoDQI] in
  defm VPEXTRQ : SS41I_extract64<0x16, "vpextrq">, VEX, VEX_W;

defm PEXTRQ      : SS41I_extract64<0x16, "pextrq">, REX_W;

/// SS41I_extractf32 - SSE 4.1 extract 32 bits fp value to int reg or memory
/// destination
multiclass SS41I_extractf32<bits<8> opc, string OpcodeStr> {
  def rr : SS4AIi8<opc, MRMDestReg, (outs GR32orGR64:$dst),
                   (ins VR128:$src1, u8imm:$src2),
                   !strconcat(OpcodeStr,
                    "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
                   [(set GR32orGR64:$dst,
                      (extractelt (bc_v4i32 (v4f32 VR128:$src1)), imm:$src2))]>,
                   Sched<[WriteVecExtract]>;
  def mr : SS4AIi8<opc, MRMDestMem, (outs),
                   (ins f32mem:$dst, VR128:$src1, u8imm:$src2),
                   !strconcat(OpcodeStr,
                    "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
                   [(store (extractelt (bc_v4i32 (v4f32 VR128:$src1)), imm:$src2),
                            addr:$dst)]>, Sched<[WriteVecExtractSt]>;
}

let ExeDomain = SSEPackedSingle in {
  let Predicates = [UseAVX] in
    defm VEXTRACTPS : SS41I_extractf32<0x17, "vextractps">, VEX, VEX_WIG;
  defm EXTRACTPS   : SS41I_extractf32<0x17, "extractps">;
}

// Also match an EXTRACTPS store when the store is done as f32 instead of i32.
def : Pat<(store (f32 (bitconvert (extractelt (bc_v4i32 (v4f32 VR128:$src1)),
                                              imm:$src2))),
                 addr:$dst),
          (VEXTRACTPSmr addr:$dst, VR128:$src1, imm:$src2)>,
          Requires<[HasAVX]>;
def : Pat<(store (f32 (bitconvert (extractelt (bc_v4i32 (v4f32 VR128:$src1)),
                                              imm:$src2))),
                 addr:$dst),
          (EXTRACTPSmr addr:$dst, VR128:$src1, imm:$src2)>,
          Requires<[UseSSE41]>;

//===----------------------------------------------------------------------===//
// SSE4.1 - Insert Instructions
//===----------------------------------------------------------------------===//

multiclass SS41I_insert8<bits<8> opc, string asm, bit Is2Addr = 1> {
  def rr : SS4AIi8<opc, MRMSrcReg, (outs VR128:$dst),
      (ins VR128:$src1, GR32orGR64:$src2, u8imm:$src3),
      !if(Is2Addr,
        !strconcat(asm, "\t{$src3, $src2, $dst|$dst, $src2, $src3}"),
        !strconcat(asm,
                   "\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}")),
      [(set VR128:$dst,
        (X86pinsrb VR128:$src1, GR32orGR64:$src2, imm:$src3))]>,
      Sched<[WriteVecInsert]>;
  def rm : SS4AIi8<opc, MRMSrcMem, (outs VR128:$dst),
      (ins VR128:$src1, i8mem:$src2, u8imm:$src3),
      !if(Is2Addr,
        !strconcat(asm, "\t{$src3, $src2, $dst|$dst, $src2, $src3}"),
        !strconcat(asm,
                   "\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}")),
      [(set VR128:$dst,
        (X86pinsrb VR128:$src1, (extloadi8 addr:$src2), imm:$src3))]>,
                   Sched<[WriteVecInsert.Folded, WriteVecInsert.ReadAfterFold]>;
}

let Predicates = [HasAVX, NoBWI] in
  defm VPINSRB : SS41I_insert8<0x20, "vpinsrb", 0>, VEX_4V;
let Constraints = "$src1 = $dst" in
  defm PINSRB  : SS41I_insert8<0x20, "pinsrb">;

multiclass SS41I_insert32<bits<8> opc, string asm, bit Is2Addr = 1> {
  def rr : SS4AIi8<opc, MRMSrcReg, (outs VR128:$dst),
      (ins VR128:$src1, GR32:$src2, u8imm:$src3),
      !if(Is2Addr,
        !strconcat(asm, "\t{$src3, $src2, $dst|$dst, $src2, $src3}"),
        !strconcat(asm,
                   "\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}")),
      [(set VR128:$dst,
        (v4i32 (insertelt VR128:$src1, GR32:$src2, imm:$src3)))]>,
      Sched<[WriteVecInsert]>;
  def rm : SS4AIi8<opc, MRMSrcMem, (outs VR128:$dst),
      (ins VR128:$src1, i32mem:$src2, u8imm:$src3),
      !if(Is2Addr,
        !strconcat(asm, "\t{$src3, $src2, $dst|$dst, $src2, $src3}"),
        !strconcat(asm,
                   "\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}")),
      [(set VR128:$dst,
        (v4i32 (insertelt VR128:$src1, (loadi32 addr:$src2), imm:$src3)))]>,
                   Sched<[WriteVecInsert.Folded, WriteVecInsert.ReadAfterFold]>;
}

let Predicates = [HasAVX, NoDQI] in
  defm VPINSRD : SS41I_insert32<0x22, "vpinsrd", 0>, VEX_4V;
let Constraints = "$src1 = $dst" in
  defm PINSRD : SS41I_insert32<0x22, "pinsrd">;

multiclass SS41I_insert64<bits<8> opc, string asm, bit Is2Addr = 1> {
  def rr : SS4AIi8<opc, MRMSrcReg, (outs VR128:$dst),
      (ins VR128:$src1, GR64:$src2, u8imm:$src3),
      !if(Is2Addr,
        !strconcat(asm, "\t{$src3, $src2, $dst|$dst, $src2, $src3}"),
        !strconcat(asm,
                   "\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}")),
      [(set VR128:$dst,
        (v2i64 (insertelt VR128:$src1, GR64:$src2, imm:$src3)))]>,
      Sched<[WriteVecInsert]>;
  def rm : SS4AIi8<opc, MRMSrcMem, (outs VR128:$dst),
      (ins VR128:$src1, i64mem:$src2, u8imm:$src3),
      !if(Is2Addr,
        !strconcat(asm, "\t{$src3, $src2, $dst|$dst, $src2, $src3}"),
        !strconcat(asm,
                   "\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}")),
      [(set VR128:$dst,
        (v2i64 (insertelt VR128:$src1, (loadi64 addr:$src2), imm:$src3)))]>,
                   Sched<[WriteVecInsert.Folded, WriteVecInsert.ReadAfterFold]>;
}

let Predicates = [HasAVX, NoDQI] in
  defm VPINSRQ : SS41I_insert64<0x22, "vpinsrq", 0>, VEX_4V, VEX_W;
let Constraints = "$src1 = $dst" in
  defm PINSRQ : SS41I_insert64<0x22, "pinsrq">, REX_W;

// insertps has a few different modes, there's the first two here below which
// are optimized inserts that won't zero arbitrary elements in the destination
// vector. The next one matches the intrinsic and could zero arbitrary elements
// in the target vector.
multiclass SS41I_insertf32<bits<8> opc, string asm, bit Is2Addr = 1> {
  def rr : SS4AIi8<opc, MRMSrcReg, (outs VR128:$dst),
      (ins VR128:$src1, VR128:$src2, u8imm:$src3),
      !if(Is2Addr,
        !strconcat(asm, "\t{$src3, $src2, $dst|$dst, $src2, $src3}"),
        !strconcat(asm,
                   "\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}")),
      [(set VR128:$dst,
        (X86insertps VR128:$src1, VR128:$src2, imm:$src3))]>,
      Sched<[SchedWriteFShuffle.XMM]>;
  def rm : SS4AIi8<opc, MRMSrcMem, (outs VR128:$dst),
      (ins VR128:$src1, f32mem:$src2, u8imm:$src3),
      !if(Is2Addr,
        !strconcat(asm, "\t{$src3, $src2, $dst|$dst, $src2, $src3}"),
        !strconcat(asm,
                   "\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}")),
      [(set VR128:$dst,
        (X86insertps VR128:$src1,
                   (v4f32 (scalar_to_vector (loadf32 addr:$src2))),
                    imm:$src3))]>,
      Sched<[SchedWriteFShuffle.XMM.Folded, SchedWriteFShuffle.XMM.ReadAfterFold]>;
}

let ExeDomain = SSEPackedSingle in {
  let Predicates = [UseAVX] in
    defm VINSERTPS : SS41I_insertf32<0x21, "vinsertps", 0>,
                     VEX_4V, VEX_WIG;
  let Constraints = "$src1 = $dst" in
    defm INSERTPS : SS41I_insertf32<0x21, "insertps", 1>;
}

let Predicates = [UseAVX] in {
  // If we're inserting an element from a vbroadcast of a load, fold the
  // load into the X86insertps instruction.
  def : Pat<(v4f32 (X86insertps (v4f32 VR128:$src1),
                (X86VBroadcast (loadf32 addr:$src2)), imm:$src3)),
            (VINSERTPSrm VR128:$src1, addr:$src2, imm:$src3)>;
  def : Pat<(v4f32 (X86insertps (v4f32 VR128:$src1),
                (X86VBroadcast (loadv4f32 addr:$src2)), imm:$src3)),
            (VINSERTPSrm VR128:$src1, addr:$src2, imm:$src3)>;
}

//===----------------------------------------------------------------------===//
// SSE4.1 - Round Instructions
//===----------------------------------------------------------------------===//

multiclass sse41_fp_unop_p<bits<8> opc, string OpcodeStr,
                           X86MemOperand x86memop, RegisterClass RC,
                           ValueType VT, PatFrag mem_frag, SDNode OpNode,
                           X86FoldableSchedWrite sched> {
  // Intrinsic operation, reg.
  // Vector intrinsic operation, reg
  def r : SS4AIi8<opc, MRMSrcReg,
                  (outs RC:$dst), (ins RC:$src1, i32u8imm:$src2),
                  !strconcat(OpcodeStr,
                  "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
                  [(set RC:$dst, (VT (OpNode RC:$src1, imm:$src2)))]>,
                  Sched<[sched]>;

  // Vector intrinsic operation, mem
  def m : SS4AIi8<opc, MRMSrcMem,
                  (outs RC:$dst), (ins x86memop:$src1, i32u8imm:$src2),
                  !strconcat(OpcodeStr,
                  "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
                  [(set RC:$dst,
                        (VT (OpNode (mem_frag addr:$src1),imm:$src2)))]>,
                  Sched<[sched.Folded]>;
}

multiclass avx_fp_unop_rm<bits<8> opcss, bits<8> opcsd,
                          string OpcodeStr, X86FoldableSchedWrite sched> {
let ExeDomain = SSEPackedSingle, hasSideEffects = 0 in {
  def SSr : SS4AIi8<opcss, MRMSrcReg,
        (outs FR32:$dst), (ins FR32:$src1, FR32:$src2, i32u8imm:$src3),
        !strconcat(OpcodeStr,
            "ss\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}"),
      []>, Sched<[sched]>;

  let mayLoad = 1 in
  def SSm : SS4AIi8<opcss, MRMSrcMem,
        (outs FR32:$dst), (ins FR32:$src1, f32mem:$src2, i32u8imm:$src3),
        !strconcat(OpcodeStr,
             "ss\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}"),
        []>, Sched<[sched.Folded, sched.ReadAfterFold]>;
} // ExeDomain = SSEPackedSingle, hasSideEffects = 0

let ExeDomain = SSEPackedDouble, hasSideEffects = 0 in {
  def SDr : SS4AIi8<opcsd, MRMSrcReg,
        (outs FR64:$dst), (ins FR64:$src1, FR64:$src2, i32u8imm:$src3),
        !strconcat(OpcodeStr,
              "sd\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}"),
        []>, Sched<[sched]>;

  let mayLoad = 1 in
  def SDm : SS4AIi8<opcsd, MRMSrcMem,
        (outs FR64:$dst), (ins FR64:$src1, f64mem:$src2, i32u8imm:$src3),
        !strconcat(OpcodeStr,
             "sd\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}"),
        []>, Sched<[sched.Folded, sched.ReadAfterFold]>;
} // ExeDomain = SSEPackedDouble, hasSideEffects = 0
}

multiclass sse41_fp_unop_s<bits<8> opcss, bits<8> opcsd,
                           string OpcodeStr, X86FoldableSchedWrite sched> {
let ExeDomain = SSEPackedSingle, hasSideEffects = 0 in {
  def SSr : SS4AIi8<opcss, MRMSrcReg,
                    (outs FR32:$dst), (ins FR32:$src1, i32u8imm:$src2),
                    !strconcat(OpcodeStr,
                               "ss\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
                    []>, Sched<[sched]>;

  let mayLoad = 1 in
  def SSm : SS4AIi8<opcss, MRMSrcMem,
                    (outs FR32:$dst), (ins f32mem:$src1, i32u8imm:$src2),
                    !strconcat(OpcodeStr,
                               "ss\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
                    []>, Sched<[sched.Folded, sched.ReadAfterFold]>;
} // ExeDomain = SSEPackedSingle, hasSideEffects = 0

let ExeDomain = SSEPackedDouble, hasSideEffects = 0 in {
  def SDr : SS4AIi8<opcsd, MRMSrcReg,
                    (outs FR64:$dst), (ins FR64:$src1, i32u8imm:$src2),
                    !strconcat(OpcodeStr,
                               "sd\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
                    []>, Sched<[sched]>;

  let mayLoad = 1 in
  def SDm : SS4AIi8<opcsd, MRMSrcMem,
                    (outs FR64:$dst), (ins f64mem:$src1, i32u8imm:$src2),
                    !strconcat(OpcodeStr,
                               "sd\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
                    []>, Sched<[sched.Folded, sched.ReadAfterFold]>;
} // ExeDomain = SSEPackedDouble, hasSideEffects = 0
}

multiclass sse41_fp_binop_s<bits<8> opcss, bits<8> opcsd,
                            string OpcodeStr, X86FoldableSchedWrite sched,
                            ValueType VT32, ValueType VT64,
                            SDNode OpNode, bit Is2Addr = 1> {
let ExeDomain = SSEPackedSingle, isCodeGenOnly = 1 in {
  def SSr_Int : SS4AIi8<opcss, MRMSrcReg,
        (outs VR128:$dst), (ins VR128:$src1, VR128:$src2, i32u8imm:$src3),
        !if(Is2Addr,
            !strconcat(OpcodeStr,
                "ss\t{$src3, $src2, $dst|$dst, $src2, $src3}"),
            !strconcat(OpcodeStr,
                "ss\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}")),
        [(set VR128:$dst, (VT32 (OpNode VR128:$src1, VR128:$src2, imm:$src3)))]>,
        Sched<[sched]>;

  def SSm_Int : SS4AIi8<opcss, MRMSrcMem,
        (outs VR128:$dst), (ins VR128:$src1, ssmem:$src2, i32u8imm:$src3),
        !if(Is2Addr,
            !strconcat(OpcodeStr,
                "ss\t{$src3, $src2, $dst|$dst, $src2, $src3}"),
            !strconcat(OpcodeStr,
                "ss\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}")),
        [(set VR128:$dst,
             (OpNode VR128:$src1, sse_load_f32:$src2, imm:$src3))]>,
        Sched<[sched.Folded, sched.ReadAfterFold]>;
} // ExeDomain = SSEPackedSingle, isCodeGenOnly = 1

let ExeDomain = SSEPackedDouble, isCodeGenOnly = 1 in {
  def SDr_Int : SS4AIi8<opcsd, MRMSrcReg,
        (outs VR128:$dst), (ins VR128:$src1, VR128:$src2, i32u8imm:$src3),
        !if(Is2Addr,
            !strconcat(OpcodeStr,
                "sd\t{$src3, $src2, $dst|$dst, $src2, $src3}"),
            !strconcat(OpcodeStr,
                "sd\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}")),
        [(set VR128:$dst, (VT64 (OpNode VR128:$src1, VR128:$src2, imm:$src3)))]>,
        Sched<[sched]>;

  def SDm_Int : SS4AIi8<opcsd, MRMSrcMem,
        (outs VR128:$dst), (ins VR128:$src1, sdmem:$src2, i32u8imm:$src3),
        !if(Is2Addr,
            !strconcat(OpcodeStr,
                "sd\t{$src3, $src2, $dst|$dst, $src2, $src3}"),
            !strconcat(OpcodeStr,
                "sd\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}")),
        [(set VR128:$dst,
              (OpNode VR128:$src1, sse_load_f64:$src2, imm:$src3))]>,
        Sched<[sched.Folded, sched.ReadAfterFold]>;
} // ExeDomain = SSEPackedDouble, isCodeGenOnly = 1
}

// FP round - roundss, roundps, roundsd, roundpd
let Predicates = [HasAVX, NoVLX] in {
  let ExeDomain = SSEPackedSingle in {
    // Intrinsic form
    defm VROUNDPS  : sse41_fp_unop_p<0x08, "vroundps", f128mem, VR128, v4f32,
                                     loadv4f32, X86VRndScale, SchedWriteFRnd.XMM>,
                                   VEX, VEX_WIG;
    defm VROUNDPSY : sse41_fp_unop_p<0x08, "vroundps", f256mem, VR256, v8f32,
                                     loadv8f32, X86VRndScale, SchedWriteFRnd.YMM>,
                                   VEX, VEX_L, VEX_WIG;
  }

  let ExeDomain = SSEPackedDouble in {
    defm VROUNDPD  : sse41_fp_unop_p<0x09, "vroundpd", f128mem, VR128, v2f64,
                                     loadv2f64, X86VRndScale, SchedWriteFRnd.XMM>,
                                   VEX, VEX_WIG;
    defm VROUNDPDY : sse41_fp_unop_p<0x09, "vroundpd", f256mem, VR256, v4f64,
                                     loadv4f64, X86VRndScale, SchedWriteFRnd.YMM>,
                                   VEX, VEX_L, VEX_WIG;
  }
}
let Predicates = [HasAVX, NoAVX512] in {
  defm VROUND  : sse41_fp_binop_s<0x0A, 0x0B, "vround", SchedWriteFRnd.Scl,
                                  v4f32, v2f64, X86RndScales, 0>,
                                  VEX_4V, VEX_LIG, VEX_WIG;
  defm VROUND  : avx_fp_unop_rm<0x0A, 0x0B, "vround", SchedWriteFRnd.Scl>,
                                VEX_4V, VEX_LIG, VEX_WIG;
}

let Predicates = [UseAVX] in {
  def : Pat<(ffloor FR32:$src),
            (VROUNDSSr (f32 (IMPLICIT_DEF)), FR32:$src, (i32 0x9))>;
  def : Pat<(f32 (fnearbyint FR32:$src)),
            (VROUNDSSr (f32 (IMPLICIT_DEF)), FR32:$src, (i32 0xC))>;
  def : Pat<(f32 (fceil FR32:$src)),
            (VROUNDSSr (f32 (IMPLICIT_DEF)), FR32:$src, (i32 0xA))>;
  def : Pat<(f32 (frint FR32:$src)),
            (VROUNDSSr (f32 (IMPLICIT_DEF)), FR32:$src, (i32 0x4))>;
  def : Pat<(f32 (ftrunc FR32:$src)),
            (VROUNDSSr (f32 (IMPLICIT_DEF)), FR32:$src, (i32 0xB))>;

  def : Pat<(f64 (ffloor FR64:$src)),
            (VROUNDSDr (f64 (IMPLICIT_DEF)), FR64:$src, (i32 0x9))>;
  def : Pat<(f64 (fnearbyint FR64:$src)),
            (VROUNDSDr (f64 (IMPLICIT_DEF)), FR64:$src, (i32 0xC))>;
  def : Pat<(f64 (fceil FR64:$src)),
            (VROUNDSDr (f64 (IMPLICIT_DEF)), FR64:$src, (i32 0xA))>;
  def : Pat<(f64 (frint FR64:$src)),
            (VROUNDSDr (f64 (IMPLICIT_DEF)), FR64:$src, (i32 0x4))>;
  def : Pat<(f64 (ftrunc FR64:$src)),
            (VROUNDSDr (f64 (IMPLICIT_DEF)), FR64:$src, (i32 0xB))>;
}

let Predicates = [UseAVX, OptForSize] in {
  def : Pat<(ffloor (loadf32 addr:$src)),
            (VROUNDSSm (f32 (IMPLICIT_DEF)), addr:$src, (i32 0x9))>;
  def : Pat<(f32 (fnearbyint (loadf32 addr:$src))),
            (VROUNDSSm (f32 (IMPLICIT_DEF)), addr:$src, (i32 0xC))>;
  def : Pat<(f32 (fceil (loadf32 addr:$src))),
            (VROUNDSSm (f32 (IMPLICIT_DEF)), addr:$src, (i32 0xA))>;
  def : Pat<(f32 (frint (loadf32 addr:$src))),
            (VROUNDSSm (f32 (IMPLICIT_DEF)), addr:$src, (i32 0x4))>;
  def : Pat<(f32 (ftrunc (loadf32 addr:$src))),
            (VROUNDSSm (f32 (IMPLICIT_DEF)), addr:$src, (i32 0xB))>;

  def : Pat<(f64 (ffloor (loadf64 addr:$src))),
            (VROUNDSDm (f64 (IMPLICIT_DEF)), addr:$src, (i32 0x9))>;
  def : Pat<(f64 (fnearbyint (loadf64 addr:$src))),
            (VROUNDSDm (f64 (IMPLICIT_DEF)), addr:$src, (i32 0xC))>;
  def : Pat<(f64 (fceil (loadf64 addr:$src))),
            (VROUNDSDm (f64 (IMPLICIT_DEF)), addr:$src, (i32 0xA))>;
  def : Pat<(f64 (frint (loadf64 addr:$src))),
            (VROUNDSDm (f64 (IMPLICIT_DEF)), addr:$src, (i32 0x4))>;
  def : Pat<(f64 (ftrunc (loadf64 addr:$src))),
            (VROUNDSDm (f64 (IMPLICIT_DEF)), addr:$src, (i32 0xB))>;
}

let Predicates = [HasAVX, NoVLX] in {
  def : Pat<(v4f32 (ffloor VR128:$src)),
            (VROUNDPSr VR128:$src, (i32 0x9))>;
  def : Pat<(v4f32 (fnearbyint VR128:$src)),
            (VROUNDPSr VR128:$src, (i32 0xC))>;
  def : Pat<(v4f32 (fceil VR128:$src)),
            (VROUNDPSr VR128:$src, (i32 0xA))>;
  def : Pat<(v4f32 (frint VR128:$src)),
            (VROUNDPSr VR128:$src, (i32 0x4))>;
  def : Pat<(v4f32 (ftrunc VR128:$src)),
            (VROUNDPSr VR128:$src, (i32 0xB))>;

  def : Pat<(v4f32 (ffloor (loadv4f32 addr:$src))),
            (VROUNDPSm addr:$src, (i32 0x9))>;
  def : Pat<(v4f32 (fnearbyint (loadv4f32 addr:$src))),
            (VROUNDPSm addr:$src, (i32 0xC))>;
  def : Pat<(v4f32 (fceil (loadv4f32 addr:$src))),
            (VROUNDPSm addr:$src, (i32 0xA))>;
  def : Pat<(v4f32 (frint (loadv4f32 addr:$src))),
            (VROUNDPSm addr:$src, (i32 0x4))>;
  def : Pat<(v4f32 (ftrunc (loadv4f32 addr:$src))),
            (VROUNDPSm addr:$src, (i32 0xB))>;

  def : Pat<(v2f64 (ffloor VR128:$src)),
            (VROUNDPDr VR128:$src, (i32 0x9))>;
  def : Pat<(v2f64 (fnearbyint VR128:$src)),
            (VROUNDPDr VR128:$src, (i32 0xC))>;
  def : Pat<(v2f64 (fceil VR128:$src)),
            (VROUNDPDr VR128:$src, (i32 0xA))>;
  def : Pat<(v2f64 (frint VR128:$src)),
            (VROUNDPDr VR128:$src, (i32 0x4))>;
  def : Pat<(v2f64 (ftrunc VR128:$src)),
            (VROUNDPDr VR128:$src, (i32 0xB))>;

  def : Pat<(v2f64 (ffloor (loadv2f64 addr:$src))),
            (VROUNDPDm addr:$src, (i32 0x9))>;
  def : Pat<(v2f64 (fnearbyint (loadv2f64 addr:$src))),
            (VROUNDPDm addr:$src, (i32 0xC))>;
  def : Pat<(v2f64 (fceil (loadv2f64 addr:$src))),
            (VROUNDPDm addr:$src, (i32 0xA))>;
  def : Pat<(v2f64 (frint (loadv2f64 addr:$src))),
            (VROUNDPDm addr:$src, (i32 0x4))>;
  def : Pat<(v2f64 (ftrunc (loadv2f64 addr:$src))),
            (VROUNDPDm addr:$src, (i32 0xB))>;

  def : Pat<(v8f32 (ffloor VR256:$src)),
            (VROUNDPSYr VR256:$src, (i32 0x9))>;
  def : Pat<(v8f32 (fnearbyint VR256:$src)),
            (VROUNDPSYr VR256:$src, (i32 0xC))>;
  def : Pat<(v8f32 (fceil VR256:$src)),
            (VROUNDPSYr VR256:$src, (i32 0xA))>;
  def : Pat<(v8f32 (frint VR256:$src)),
            (VROUNDPSYr VR256:$src, (i32 0x4))>;
  def : Pat<(v8f32 (ftrunc VR256:$src)),
            (VROUNDPSYr VR256:$src, (i32 0xB))>;

  def : Pat<(v8f32 (ffloor (loadv8f32 addr:$src))),
            (VROUNDPSYm addr:$src, (i32 0x9))>;
  def : Pat<(v8f32 (fnearbyint (loadv8f32 addr:$src))),
            (VROUNDPSYm addr:$src, (i32 0xC))>;
  def : Pat<(v8f32 (fceil (loadv8f32 addr:$src))),
            (VROUNDPSYm addr:$src, (i32 0xA))>;
  def : Pat<(v8f32 (frint (loadv8f32 addr:$src))),
            (VROUNDPSYm addr:$src, (i32 0x4))>;
  def : Pat<(v8f32 (ftrunc (loadv8f32 addr:$src))),
            (VROUNDPSYm addr:$src, (i32 0xB))>;

  def : Pat<(v4f64 (ffloor VR256:$src)),
            (VROUNDPDYr VR256:$src, (i32 0x9))>;
  def : Pat<(v4f64 (fnearbyint VR256:$src)),
            (VROUNDPDYr VR256:$src, (i32 0xC))>;
  def : Pat<(v4f64 (fceil VR256:$src)),
            (VROUNDPDYr VR256:$src, (i32 0xA))>;
  def : Pat<(v4f64 (frint VR256:$src)),
            (VROUNDPDYr VR256:$src, (i32 0x4))>;
  def : Pat<(v4f64 (ftrunc VR256:$src)),
            (VROUNDPDYr VR256:$src, (i32 0xB))>;

  def : Pat<(v4f64 (ffloor (loadv4f64 addr:$src))),
            (VROUNDPDYm addr:$src, (i32 0x9))>;
  def : Pat<(v4f64 (fnearbyint (loadv4f64 addr:$src))),
            (VROUNDPDYm addr:$src, (i32 0xC))>;
  def : Pat<(v4f64 (fceil (loadv4f64 addr:$src))),
            (VROUNDPDYm addr:$src, (i32 0xA))>;
  def : Pat<(v4f64 (frint (loadv4f64 addr:$src))),
            (VROUNDPDYm addr:$src, (i32 0x4))>;
  def : Pat<(v4f64 (ftrunc (loadv4f64 addr:$src))),
            (VROUNDPDYm addr:$src, (i32 0xB))>;
}

let ExeDomain = SSEPackedSingle in
defm ROUNDPS  : sse41_fp_unop_p<0x08, "roundps", f128mem, VR128, v4f32,
                                memopv4f32, X86VRndScale, SchedWriteFRnd.XMM>;
let ExeDomain = SSEPackedDouble in
defm ROUNDPD  : sse41_fp_unop_p<0x09, "roundpd", f128mem, VR128, v2f64,
                                memopv2f64, X86VRndScale, SchedWriteFRnd.XMM>;

defm ROUND  : sse41_fp_unop_s<0x0A, 0x0B, "round", SchedWriteFRnd.Scl>;

let Constraints = "$src1 = $dst" in
defm ROUND  : sse41_fp_binop_s<0x0A, 0x0B, "round", SchedWriteFRnd.Scl,
                               v4f32, v2f64, X86RndScales>;

let Predicates = [UseSSE41] in {
  def : Pat<(ffloor FR32:$src),
            (ROUNDSSr FR32:$src, (i32 0x9))>;
  def : Pat<(f32 (fnearbyint FR32:$src)),
            (ROUNDSSr FR32:$src, (i32 0xC))>;
  def : Pat<(f32 (fceil FR32:$src)),
            (ROUNDSSr FR32:$src, (i32 0xA))>;
  def : Pat<(f32 (frint FR32:$src)),
            (ROUNDSSr FR32:$src, (i32 0x4))>;
  def : Pat<(f32 (ftrunc FR32:$src)),
            (ROUNDSSr FR32:$src, (i32 0xB))>;

  def : Pat<(f64 (ffloor FR64:$src)),
            (ROUNDSDr FR64:$src, (i32 0x9))>;
  def : Pat<(f64 (fnearbyint FR64:$src)),
            (ROUNDSDr FR64:$src, (i32 0xC))>;
  def : Pat<(f64 (fceil FR64:$src)),
            (ROUNDSDr FR64:$src, (i32 0xA))>;
  def : Pat<(f64 (frint FR64:$src)),
            (ROUNDSDr FR64:$src, (i32 0x4))>;
  def : Pat<(f64 (ftrunc FR64:$src)),
            (ROUNDSDr FR64:$src, (i32 0xB))>;
}

let Predicates = [UseSSE41, OptForSize] in {
  def : Pat<(ffloor (loadf32 addr:$src)),
            (ROUNDSSm addr:$src, (i32 0x9))>;
  def : Pat<(f32 (fnearbyint (loadf32 addr:$src))),
            (ROUNDSSm addr:$src, (i32 0xC))>;
  def : Pat<(f32 (fceil (loadf32 addr:$src))),
            (ROUNDSSm addr:$src, (i32 0xA))>;
  def : Pat<(f32 (frint (loadf32 addr:$src))),
            (ROUNDSSm addr:$src, (i32 0x4))>;
  def : Pat<(f32 (ftrunc (loadf32 addr:$src))),
            (ROUNDSSm addr:$src, (i32 0xB))>;

  def : Pat<(f64 (ffloor (loadf64 addr:$src))),
            (ROUNDSDm addr:$src, (i32 0x9))>;
  def : Pat<(f64 (fnearbyint (loadf64 addr:$src))),
            (ROUNDSDm addr:$src, (i32 0xC))>;
  def : Pat<(f64 (fceil (loadf64 addr:$src))),
            (ROUNDSDm addr:$src, (i32 0xA))>;
  def : Pat<(f64 (frint (loadf64 addr:$src))),
            (ROUNDSDm addr:$src, (i32 0x4))>;
  def : Pat<(f64 (ftrunc (loadf64 addr:$src))),
            (ROUNDSDm addr:$src, (i32 0xB))>;
}

let Predicates = [UseSSE41] in {
  def : Pat<(v4f32 (ffloor VR128:$src)),
            (ROUNDPSr VR128:$src, (i32 0x9))>;
  def : Pat<(v4f32 (fnearbyint VR128:$src)),
            (ROUNDPSr VR128:$src, (i32 0xC))>;
  def : Pat<(v4f32 (fceil VR128:$src)),
            (ROUNDPSr VR128:$src, (i32 0xA))>;
  def : Pat<(v4f32 (frint VR128:$src)),
            (ROUNDPSr VR128:$src, (i32 0x4))>;
  def : Pat<(v4f32 (ftrunc VR128:$src)),
            (ROUNDPSr VR128:$src, (i32 0xB))>;

  def : Pat<(v4f32 (ffloor (memopv4f32 addr:$src))),
            (ROUNDPSm addr:$src, (i32 0x9))>;
  def : Pat<(v4f32 (fnearbyint (memopv4f32 addr:$src))),
            (ROUNDPSm addr:$src, (i32 0xC))>;
  def : Pat<(v4f32 (fceil (memopv4f32 addr:$src))),
            (ROUNDPSm addr:$src, (i32 0xA))>;
  def : Pat<(v4f32 (frint (memopv4f32 addr:$src))),
            (ROUNDPSm addr:$src, (i32 0x4))>;
  def : Pat<(v4f32 (ftrunc (memopv4f32 addr:$src))),
            (ROUNDPSm addr:$src, (i32 0xB))>;

  def : Pat<(v2f64 (ffloor VR128:$src)),
            (ROUNDPDr VR128:$src, (i32 0x9))>;
  def : Pat<(v2f64 (fnearbyint VR128:$src)),
            (ROUNDPDr VR128:$src, (i32 0xC))>;
  def : Pat<(v2f64 (fceil VR128:$src)),
            (ROUNDPDr VR128:$src, (i32 0xA))>;
  def : Pat<(v2f64 (frint VR128:$src)),
            (ROUNDPDr VR128:$src, (i32 0x4))>;
  def : Pat<(v2f64 (ftrunc VR128:$src)),
            (ROUNDPDr VR128:$src, (i32 0xB))>;

  def : Pat<(v2f64 (ffloor (memopv2f64 addr:$src))),
            (ROUNDPDm addr:$src, (i32 0x9))>;
  def : Pat<(v2f64 (fnearbyint (memopv2f64 addr:$src))),
            (ROUNDPDm addr:$src, (i32 0xC))>;
  def : Pat<(v2f64 (fceil (memopv2f64 addr:$src))),
            (ROUNDPDm addr:$src, (i32 0xA))>;
  def : Pat<(v2f64 (frint (memopv2f64 addr:$src))),
            (ROUNDPDm addr:$src, (i32 0x4))>;
  def : Pat<(v2f64 (ftrunc (memopv2f64 addr:$src))),
            (ROUNDPDm addr:$src, (i32 0xB))>;
}

defm : scalar_unary_math_imm_patterns<ffloor, "ROUNDSS", X86Movss,
                                      v4f32, 0x01, UseSSE41>;
defm : scalar_unary_math_imm_patterns<fceil, "ROUNDSS", X86Movss,
                                      v4f32, 0x02, UseSSE41>;
defm : scalar_unary_math_imm_patterns<ffloor, "ROUNDSD", X86Movsd,
                                      v2f64, 0x01, UseSSE41>;
defm : scalar_unary_math_imm_patterns<fceil, "ROUNDSD", X86Movsd,
                                      v2f64, 0x02, UseSSE41>;

//===----------------------------------------------------------------------===//
// SSE4.1 - Packed Bit Test
//===----------------------------------------------------------------------===//

// ptest instruction we'll lower to this in X86ISelLowering primarily from
// the intel intrinsic that corresponds to this.
let Defs = [EFLAGS], Predicates = [HasAVX] in {
def VPTESTrr  : SS48I<0x17, MRMSrcReg, (outs), (ins VR128:$src1, VR128:$src2),
                "vptest\t{$src2, $src1|$src1, $src2}",
                [(set EFLAGS, (X86ptest VR128:$src1, (v2i64 VR128:$src2)))]>,
                Sched<[SchedWriteVecTest.XMM]>, VEX, VEX_WIG;
def VPTESTrm  : SS48I<0x17, MRMSrcMem, (outs), (ins VR128:$src1, f128mem:$src2),
                "vptest\t{$src2, $src1|$src1, $src2}",
                [(set EFLAGS,(X86ptest VR128:$src1, (loadv2i64 addr:$src2)))]>,
                Sched<[SchedWriteVecTest.XMM.Folded, SchedWriteVecTest.XMM.ReadAfterFold]>,
                VEX, VEX_WIG;

def VPTESTYrr : SS48I<0x17, MRMSrcReg, (outs), (ins VR256:$src1, VR256:$src2),
                "vptest\t{$src2, $src1|$src1, $src2}",
                [(set EFLAGS, (X86ptest VR256:$src1, (v4i64 VR256:$src2)))]>,
                Sched<[SchedWriteVecTest.YMM]>, VEX, VEX_L, VEX_WIG;
def VPTESTYrm : SS48I<0x17, MRMSrcMem, (outs), (ins VR256:$src1, i256mem:$src2),
                "vptest\t{$src2, $src1|$src1, $src2}",
                [(set EFLAGS,(X86ptest VR256:$src1, (loadv4i64 addr:$src2)))]>,
                Sched<[SchedWriteVecTest.YMM.Folded, SchedWriteVecTest.YMM.ReadAfterFold]>,
                VEX, VEX_L, VEX_WIG;
}

let Defs = [EFLAGS] in {
def PTESTrr : SS48I<0x17, MRMSrcReg, (outs), (ins VR128:$src1, VR128:$src2),
              "ptest\t{$src2, $src1|$src1, $src2}",
              [(set EFLAGS, (X86ptest VR128:$src1, (v2i64 VR128:$src2)))]>,
              Sched<[SchedWriteVecTest.XMM]>;
def PTESTrm : SS48I<0x17, MRMSrcMem, (outs), (ins VR128:$src1, f128mem:$src2),
              "ptest\t{$src2, $src1|$src1, $src2}",
              [(set EFLAGS, (X86ptest VR128:$src1, (memopv2i64 addr:$src2)))]>,
              Sched<[SchedWriteVecTest.XMM.Folded, SchedWriteVecTest.XMM.ReadAfterFold]>;
}

// The bit test instructions below are AVX only
multiclass avx_bittest<bits<8> opc, string OpcodeStr, RegisterClass RC,
                       X86MemOperand x86memop, PatFrag mem_frag, ValueType vt,
                       X86FoldableSchedWrite sched> {
  def rr : SS48I<opc, MRMSrcReg, (outs), (ins RC:$src1, RC:$src2),
            !strconcat(OpcodeStr, "\t{$src2, $src1|$src1, $src2}"),
            [(set EFLAGS, (X86testp RC:$src1, (vt RC:$src2)))]>,
            Sched<[sched]>, VEX;
  def rm : SS48I<opc, MRMSrcMem, (outs), (ins RC:$src1, x86memop:$src2),
            !strconcat(OpcodeStr, "\t{$src2, $src1|$src1, $src2}"),
            [(set EFLAGS, (X86testp RC:$src1, (mem_frag addr:$src2)))]>,
            Sched<[sched.Folded, sched.ReadAfterFold]>, VEX;
}

let Defs = [EFLAGS], Predicates = [HasAVX] in {
let ExeDomain = SSEPackedSingle in {
defm VTESTPS  : avx_bittest<0x0E, "vtestps", VR128, f128mem, loadv4f32, v4f32,
                            SchedWriteFTest.XMM>;
defm VTESTPSY : avx_bittest<0x0E, "vtestps", VR256, f256mem, loadv8f32, v8f32,
                            SchedWriteFTest.YMM>, VEX_L;
}
let ExeDomain = SSEPackedDouble in {
defm VTESTPD  : avx_bittest<0x0F, "vtestpd", VR128, f128mem, loadv2f64, v2f64,
                            SchedWriteFTest.XMM>;
defm VTESTPDY : avx_bittest<0x0F, "vtestpd", VR256, f256mem, loadv4f64, v4f64,
                            SchedWriteFTest.YMM>, VEX_L;
}
}

//===----------------------------------------------------------------------===//
// SSE4.1 - Misc Instructions
//===----------------------------------------------------------------------===//

let Defs = [EFLAGS], Predicates = [HasPOPCNT] in {
  def POPCNT16rr : I<0xB8, MRMSrcReg, (outs GR16:$dst), (ins GR16:$src),
                     "popcnt{w}\t{$src, $dst|$dst, $src}",
                     [(set GR16:$dst, (ctpop GR16:$src)), (implicit EFLAGS)]>,
                     Sched<[WritePOPCNT]>, OpSize16, XS;
  def POPCNT16rm : I<0xB8, MRMSrcMem, (outs GR16:$dst), (ins i16mem:$src),
                     "popcnt{w}\t{$src, $dst|$dst, $src}",
                     [(set GR16:$dst, (ctpop (loadi16 addr:$src))),
                      (implicit EFLAGS)]>,
                      Sched<[WritePOPCNT.Folded]>, OpSize16, XS;

  def POPCNT32rr : I<0xB8, MRMSrcReg, (outs GR32:$dst), (ins GR32:$src),
                     "popcnt{l}\t{$src, $dst|$dst, $src}",
                     [(set GR32:$dst, (ctpop GR32:$src)), (implicit EFLAGS)]>,
                     Sched<[WritePOPCNT]>, OpSize32, XS;

  def POPCNT32rm : I<0xB8, MRMSrcMem, (outs GR32:$dst), (ins i32mem:$src),
                     "popcnt{l}\t{$src, $dst|$dst, $src}",
                     [(set GR32:$dst, (ctpop (loadi32 addr:$src))),
                      (implicit EFLAGS)]>,
                      Sched<[WritePOPCNT.Folded]>, OpSize32, XS;

  def POPCNT64rr : RI<0xB8, MRMSrcReg, (outs GR64:$dst), (ins GR64:$src),
                      "popcnt{q}\t{$src, $dst|$dst, $src}",
                      [(set GR64:$dst, (ctpop GR64:$src)), (implicit EFLAGS)]>,
                      Sched<[WritePOPCNT]>, XS;
  def POPCNT64rm : RI<0xB8, MRMSrcMem, (outs GR64:$dst), (ins i64mem:$src),
                      "popcnt{q}\t{$src, $dst|$dst, $src}",
                      [(set GR64:$dst, (ctpop (loadi64 addr:$src))),
                       (implicit EFLAGS)]>,
                       Sched<[WritePOPCNT.Folded]>, XS;
}

// SS41I_unop_rm_int_v16 - SSE 4.1 unary operator whose type is v8i16.
multiclass SS41I_unop_rm_int_v16<bits<8> opc, string OpcodeStr,
                                 SDNode OpNode, PatFrag ld_frag,
                                 X86FoldableSchedWrite Sched> {
  def rr : SS48I<opc, MRMSrcReg, (outs VR128:$dst),
                 (ins VR128:$src),
                 !strconcat(OpcodeStr, "\t{$src, $dst|$dst, $src}"),
                 [(set VR128:$dst, (v8i16 (OpNode (v8i16 VR128:$src))))]>,
                 Sched<[Sched]>;
  def rm : SS48I<opc, MRMSrcMem, (outs VR128:$dst),
                  (ins i128mem:$src),
                  !strconcat(OpcodeStr, "\t{$src, $dst|$dst, $src}"),
                  [(set VR128:$dst,
                    (v8i16 (OpNode (ld_frag addr:$src))))]>,
                 Sched<[Sched.Folded]>;
}

// PHMIN has the same profile as PSAD, thus we use the same scheduling
// model, although the naming is misleading.
let Predicates = [HasAVX] in
defm VPHMINPOSUW : SS41I_unop_rm_int_v16<0x41, "vphminposuw",
                                         X86phminpos, load,
                                         WritePHMINPOS>, VEX, VEX_WIG;
defm PHMINPOSUW : SS41I_unop_rm_int_v16<0x41, "phminposuw",
                                         X86phminpos, memop,
                                         WritePHMINPOS>;

/// SS48I_binop_rm - Simple SSE41 binary operator.
multiclass SS48I_binop_rm<bits<8> opc, string OpcodeStr, SDNode OpNode,
                          ValueType OpVT, RegisterClass RC, PatFrag memop_frag,
                          X86MemOperand x86memop, X86FoldableSchedWrite sched,
                          bit Is2Addr = 1> {
  let isCommutable = 1 in
  def rr : SS48I<opc, MRMSrcReg, (outs RC:$dst),
       (ins RC:$src1, RC:$src2),
       !if(Is2Addr,
           !strconcat(OpcodeStr, "\t{$src2, $dst|$dst, $src2}"),
           !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}")),
       [(set RC:$dst, (OpVT (OpNode RC:$src1, RC:$src2)))]>,
       Sched<[sched]>;
  def rm : SS48I<opc, MRMSrcMem, (outs RC:$dst),
       (ins RC:$src1, x86memop:$src2),
       !if(Is2Addr,
           !strconcat(OpcodeStr, "\t{$src2, $dst|$dst, $src2}"),
           !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}")),
       [(set RC:$dst,
         (OpVT (OpNode RC:$src1, (memop_frag addr:$src2))))]>,
       Sched<[sched.Folded, sched.ReadAfterFold]>;
}

let Predicates = [HasAVX, NoVLX] in {
  defm VPMINSD   : SS48I_binop_rm<0x39, "vpminsd", smin, v4i32, VR128,
                                  load, i128mem, SchedWriteVecALU.XMM, 0>,
                                  VEX_4V, VEX_WIG;
  defm VPMINUD   : SS48I_binop_rm<0x3B, "vpminud", umin, v4i32, VR128,
                                  load, i128mem, SchedWriteVecALU.XMM, 0>,
                                  VEX_4V, VEX_WIG;
  defm VPMAXSD   : SS48I_binop_rm<0x3D, "vpmaxsd", smax, v4i32, VR128,
                                  load, i128mem, SchedWriteVecALU.XMM, 0>,
                                  VEX_4V, VEX_WIG;
  defm VPMAXUD   : SS48I_binop_rm<0x3F, "vpmaxud", umax, v4i32, VR128,
                                  load, i128mem, SchedWriteVecALU.XMM, 0>,
                                  VEX_4V, VEX_WIG;
  defm VPMULDQ   : SS48I_binop_rm<0x28, "vpmuldq", X86pmuldq, v2i64, VR128,
                                  load, i128mem, SchedWriteVecIMul.XMM, 0>,
                                  VEX_4V, VEX_WIG;
}
let Predicates = [HasAVX, NoVLX_Or_NoBWI] in {
  defm VPMINSB   : SS48I_binop_rm<0x38, "vpminsb", smin, v16i8, VR128,
                                  load, i128mem, SchedWriteVecALU.XMM, 0>,
                                  VEX_4V, VEX_WIG;
  defm VPMINUW   : SS48I_binop_rm<0x3A, "vpminuw", umin, v8i16, VR128,
                                  load, i128mem, SchedWriteVecALU.XMM, 0>,
                                  VEX_4V, VEX_WIG;
  defm VPMAXSB   : SS48I_binop_rm<0x3C, "vpmaxsb", smax, v16i8, VR128,
                                  load, i128mem, SchedWriteVecALU.XMM, 0>,
                                  VEX_4V, VEX_WIG;
  defm VPMAXUW   : SS48I_binop_rm<0x3E, "vpmaxuw", umax, v8i16, VR128,
                                  load, i128mem, SchedWriteVecALU.XMM, 0>,
                                  VEX_4V, VEX_WIG;
}

let Predicates = [HasAVX2, NoVLX] in {
  defm VPMINSDY  : SS48I_binop_rm<0x39, "vpminsd", smin, v8i32, VR256,
                                  load, i256mem, SchedWriteVecALU.YMM, 0>,
                                  VEX_4V, VEX_L, VEX_WIG;
  defm VPMINUDY  : SS48I_binop_rm<0x3B, "vpminud", umin, v8i32, VR256,
                                  load, i256mem, SchedWriteVecALU.YMM, 0>,
                                  VEX_4V, VEX_L, VEX_WIG;
  defm VPMAXSDY  : SS48I_binop_rm<0x3D, "vpmaxsd", smax, v8i32, VR256,
                                  load, i256mem, SchedWriteVecALU.YMM, 0>,
                                  VEX_4V, VEX_L, VEX_WIG;
  defm VPMAXUDY  : SS48I_binop_rm<0x3F, "vpmaxud", umax, v8i32, VR256,
                                  load, i256mem, SchedWriteVecALU.YMM, 0>,
                                  VEX_4V, VEX_L, VEX_WIG;
  defm VPMULDQY  : SS48I_binop_rm<0x28, "vpmuldq", X86pmuldq, v4i64, VR256,
                                  load, i256mem, SchedWriteVecIMul.YMM, 0>,
                                  VEX_4V, VEX_L, VEX_WIG;
}
let Predicates = [HasAVX2, NoVLX_Or_NoBWI] in {
  defm VPMINSBY  : SS48I_binop_rm<0x38, "vpminsb", smin, v32i8, VR256,
                                  load, i256mem, SchedWriteVecALU.YMM, 0>,
                                  VEX_4V, VEX_L, VEX_WIG;
  defm VPMINUWY  : SS48I_binop_rm<0x3A, "vpminuw", umin, v16i16, VR256,
                                  load, i256mem, SchedWriteVecALU.YMM, 0>,
                                  VEX_4V, VEX_L, VEX_WIG;
  defm VPMAXSBY  : SS48I_binop_rm<0x3C, "vpmaxsb", smax, v32i8, VR256,
                                  load, i256mem, SchedWriteVecALU.YMM, 0>,
                                  VEX_4V, VEX_L, VEX_WIG;
  defm VPMAXUWY  : SS48I_binop_rm<0x3E, "vpmaxuw", umax, v16i16, VR256,
                                  load, i256mem, SchedWriteVecALU.YMM, 0>,
                                  VEX_4V, VEX_L, VEX_WIG;
}

let Constraints = "$src1 = $dst" in {
  defm PMINSB   : SS48I_binop_rm<0x38, "pminsb", smin, v16i8, VR128,
                                 memop, i128mem, SchedWriteVecALU.XMM, 1>;
  defm PMINSD   : SS48I_binop_rm<0x39, "pminsd", smin, v4i32, VR128,
                                 memop, i128mem, SchedWriteVecALU.XMM, 1>;
  defm PMINUD   : SS48I_binop_rm<0x3B, "pminud", umin, v4i32, VR128,
                                 memop, i128mem, SchedWriteVecALU.XMM, 1>;
  defm PMINUW   : SS48I_binop_rm<0x3A, "pminuw", umin, v8i16, VR128,
                                 memop, i128mem, SchedWriteVecALU.XMM, 1>;
  defm PMAXSB   : SS48I_binop_rm<0x3C, "pmaxsb", smax, v16i8, VR128,
                                 memop, i128mem, SchedWriteVecALU.XMM, 1>;
  defm PMAXSD   : SS48I_binop_rm<0x3D, "pmaxsd", smax, v4i32, VR128,
                                 memop, i128mem, SchedWriteVecALU.XMM, 1>;
  defm PMAXUD   : SS48I_binop_rm<0x3F, "pmaxud", umax, v4i32, VR128,
                                 memop, i128mem, SchedWriteVecALU.XMM, 1>;
  defm PMAXUW   : SS48I_binop_rm<0x3E, "pmaxuw", umax, v8i16, VR128,
                                 memop, i128mem, SchedWriteVecALU.XMM, 1>;
  defm PMULDQ   : SS48I_binop_rm<0x28, "pmuldq", X86pmuldq, v2i64, VR128,
                                 memop, i128mem, SchedWriteVecIMul.XMM, 1>;
}

let Predicates = [HasAVX, NoVLX] in
  defm VPMULLD  : SS48I_binop_rm<0x40, "vpmulld", mul, v4i32, VR128,
                                 load, i128mem, SchedWritePMULLD.XMM, 0>,
                                 VEX_4V, VEX_WIG;
let Predicates = [HasAVX] in
  defm VPCMPEQQ : SS48I_binop_rm<0x29, "vpcmpeqq", X86pcmpeq, v2i64, VR128,
                                 load, i128mem, SchedWriteVecALU.XMM, 0>,
                                 VEX_4V, VEX_WIG;

let Predicates = [HasAVX2, NoVLX] in
  defm VPMULLDY  : SS48I_binop_rm<0x40, "vpmulld", mul, v8i32, VR256,
                                  load, i256mem, SchedWritePMULLD.YMM, 0>,
                                  VEX_4V, VEX_L, VEX_WIG;
let Predicates = [HasAVX2] in
  defm VPCMPEQQY : SS48I_binop_rm<0x29, "vpcmpeqq", X86pcmpeq, v4i64, VR256,
                                  load, i256mem, SchedWriteVecALU.YMM, 0>,
                                  VEX_4V, VEX_L, VEX_WIG;

let Constraints = "$src1 = $dst" in {
  defm PMULLD  : SS48I_binop_rm<0x40, "pmulld", mul, v4i32, VR128,
                                memop, i128mem, SchedWritePMULLD.XMM, 1>;
  defm PCMPEQQ : SS48I_binop_rm<0x29, "pcmpeqq", X86pcmpeq, v2i64, VR128,
                                memop, i128mem, SchedWriteVecALU.XMM, 1>;
}

/// SS41I_binop_rmi_int - SSE 4.1 binary operator with 8-bit immediate
multiclass SS41I_binop_rmi_int<bits<8> opc, string OpcodeStr,
                 Intrinsic IntId, RegisterClass RC, PatFrag memop_frag,
                 X86MemOperand x86memop, bit Is2Addr,
                 X86FoldableSchedWrite sched> {
  let isCommutable = 1 in
  def rri : SS4AIi8<opc, MRMSrcReg, (outs RC:$dst),
        (ins RC:$src1, RC:$src2, u8imm:$src3),
        !if(Is2Addr,
            !strconcat(OpcodeStr,
                "\t{$src3, $src2, $dst|$dst, $src2, $src3}"),
            !strconcat(OpcodeStr,
                "\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}")),
        [(set RC:$dst, (IntId RC:$src1, RC:$src2, imm:$src3))]>,
        Sched<[sched]>;
  def rmi : SS4AIi8<opc, MRMSrcMem, (outs RC:$dst),
        (ins RC:$src1, x86memop:$src2, u8imm:$src3),
        !if(Is2Addr,
            !strconcat(OpcodeStr,
                "\t{$src3, $src2, $dst|$dst, $src2, $src3}"),
            !strconcat(OpcodeStr,
                "\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}")),
        [(set RC:$dst,
          (IntId RC:$src1, (memop_frag addr:$src2), imm:$src3))]>,
        Sched<[sched.Folded, sched.ReadAfterFold]>;
}

/// SS41I_binop_rmi - SSE 4.1 binary operator with 8-bit immediate
multiclass SS41I_binop_rmi<bits<8> opc, string OpcodeStr, SDNode OpNode,
                           ValueType OpVT, RegisterClass RC, PatFrag memop_frag,
                           X86MemOperand x86memop, bit Is2Addr,
                           X86FoldableSchedWrite sched> {
  let isCommutable = 1 in
  def rri : SS4AIi8<opc, MRMSrcReg, (outs RC:$dst),
        (ins RC:$src1, RC:$src2, u8imm:$src3),
        !if(Is2Addr,
            !strconcat(OpcodeStr,
                "\t{$src3, $src2, $dst|$dst, $src2, $src3}"),
            !strconcat(OpcodeStr,
                "\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}")),
        [(set RC:$dst, (OpVT (OpNode RC:$src1, RC:$src2, imm:$src3)))]>,
        Sched<[sched]>;
  def rmi : SS4AIi8<opc, MRMSrcMem, (outs RC:$dst),
        (ins RC:$src1, x86memop:$src2, u8imm:$src3),
        !if(Is2Addr,
            !strconcat(OpcodeStr,
                "\t{$src3, $src2, $dst|$dst, $src2, $src3}"),
            !strconcat(OpcodeStr,
                "\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}")),
        [(set RC:$dst,
          (OpVT (OpNode RC:$src1, (memop_frag addr:$src2), imm:$src3)))]>,
        Sched<[sched.Folded, sched.ReadAfterFold]>;
}

def BlendCommuteImm2 : SDNodeXForm<imm, [{
  uint8_t Imm = N->getZExtValue() & 0x03;
  return getI8Imm(Imm ^ 0x03, SDLoc(N));
}]>;

def BlendCommuteImm4 : SDNodeXForm<imm, [{
  uint8_t Imm = N->getZExtValue() & 0x0f;
  return getI8Imm(Imm ^ 0x0f, SDLoc(N));
}]>;

def BlendCommuteImm8 : SDNodeXForm<imm, [{
  uint8_t Imm = N->getZExtValue() & 0xff;
  return getI8Imm(Imm ^ 0xff, SDLoc(N));
}]>;

let Predicates = [HasAVX] in {
  let isCommutable = 0 in {
    defm VMPSADBW : SS41I_binop_rmi_int<0x42, "vmpsadbw", int_x86_sse41_mpsadbw,
                                        VR128, load, i128mem, 0,
                                        SchedWriteMPSAD.XMM>, VEX_4V, VEX_WIG;
  }

  let ExeDomain = SSEPackedSingle in
  defm VDPPS : SS41I_binop_rmi_int<0x40, "vdpps", int_x86_sse41_dpps,
                                   VR128, load, f128mem, 0,
                                   SchedWriteDPPS.XMM>, VEX_4V, VEX_WIG;
  let ExeDomain = SSEPackedDouble in
  defm VDPPD : SS41I_binop_rmi_int<0x41, "vdppd", int_x86_sse41_dppd,
                                   VR128, load, f128mem, 0,
                                   SchedWriteDPPD.XMM>, VEX_4V, VEX_WIG;
  let ExeDomain = SSEPackedSingle in
  defm VDPPSY : SS41I_binop_rmi_int<0x40, "vdpps", int_x86_avx_dp_ps_256,
                                    VR256, load, i256mem, 0,
                                    SchedWriteDPPS.YMM>, VEX_4V, VEX_L, VEX_WIG;
}

let Predicates = [HasAVX2] in {
  let isCommutable = 0 in {
  defm VMPSADBWY : SS41I_binop_rmi_int<0x42, "vmpsadbw", int_x86_avx2_mpsadbw,
                                  VR256, load, i256mem, 0,
                                  SchedWriteMPSAD.YMM>, VEX_4V, VEX_L, VEX_WIG;
  }
}

let Constraints = "$src1 = $dst" in {
  let isCommutable = 0 in {
  defm MPSADBW : SS41I_binop_rmi_int<0x42, "mpsadbw", int_x86_sse41_mpsadbw,
                                     VR128, memop, i128mem, 1,
                                     SchedWriteMPSAD.XMM>;
  }

  let ExeDomain = SSEPackedSingle in
  defm DPPS : SS41I_binop_rmi_int<0x40, "dpps", int_x86_sse41_dpps,
                                  VR128, memop, f128mem, 1,
                                  SchedWriteDPPS.XMM>;
  let ExeDomain = SSEPackedDouble in
  defm DPPD : SS41I_binop_rmi_int<0x41, "dppd", int_x86_sse41_dppd,
                                  VR128, memop, f128mem, 1,
                                  SchedWriteDPPD.XMM>;
}

/// SS41I_blend_rmi - SSE 4.1 blend with 8-bit immediate
multiclass SS41I_blend_rmi<bits<8> opc, string OpcodeStr, SDNode OpNode,
                           ValueType OpVT, RegisterClass RC, PatFrag memop_frag,
                           X86MemOperand x86memop, bit Is2Addr, Domain d,
                           X86FoldableSchedWrite sched, SDNodeXForm commuteXForm> {
let ExeDomain = d, Constraints = !if(Is2Addr, "$src1 = $dst", "") in {
  let isCommutable = 1 in
  def rri : SS4AIi8<opc, MRMSrcReg, (outs RC:$dst),
        (ins RC:$src1, RC:$src2, u8imm:$src3),
        !if(Is2Addr,
            !strconcat(OpcodeStr,
                "\t{$src3, $src2, $dst|$dst, $src2, $src3}"),
            !strconcat(OpcodeStr,
                "\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}")),
        [(set RC:$dst, (OpVT (OpNode RC:$src1, RC:$src2, imm:$src3)))]>,
        Sched<[sched]>;
  def rmi : SS4AIi8<opc, MRMSrcMem, (outs RC:$dst),
        (ins RC:$src1, x86memop:$src2, u8imm:$src3),
        !if(Is2Addr,
            !strconcat(OpcodeStr,
                "\t{$src3, $src2, $dst|$dst, $src2, $src3}"),
            !strconcat(OpcodeStr,
                "\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}")),
        [(set RC:$dst,
          (OpVT (OpNode RC:$src1, (memop_frag addr:$src2), imm:$src3)))]>,
        Sched<[sched.Folded, sched.ReadAfterFold]>;
}

  // Pattern to commute if load is in first source.
  def : Pat<(OpVT (OpNode (memop_frag addr:$src2), RC:$src1, imm:$src3)),
            (!cast<Instruction>(NAME#"rmi") RC:$src1, addr:$src2,
                                            (commuteXForm imm:$src3))>;
}

let Predicates = [HasAVX] in {
  defm VBLENDPS : SS41I_blend_rmi<0x0C, "vblendps", X86Blendi, v4f32,
                                  VR128, load, f128mem, 0, SSEPackedSingle,
                                  SchedWriteFBlend.XMM, BlendCommuteImm4>,
                                  VEX_4V, VEX_WIG;
  defm VBLENDPSY : SS41I_blend_rmi<0x0C, "vblendps", X86Blendi, v8f32,
                                   VR256, load, f256mem, 0, SSEPackedSingle,
                                   SchedWriteFBlend.YMM, BlendCommuteImm8>,
                                   VEX_4V, VEX_L, VEX_WIG;
  defm VBLENDPD : SS41I_blend_rmi<0x0D, "vblendpd", X86Blendi, v2f64,
                                  VR128, load, f128mem, 0, SSEPackedDouble,
                                  SchedWriteFBlend.XMM, BlendCommuteImm2>,
                                  VEX_4V, VEX_WIG;
  defm VBLENDPDY : SS41I_blend_rmi<0x0D, "vblendpd", X86Blendi, v4f64,
                                   VR256, load, f256mem, 0, SSEPackedDouble,
                                   SchedWriteFBlend.YMM, BlendCommuteImm4>,
                                   VEX_4V, VEX_L, VEX_WIG;
  defm VPBLENDW : SS41I_blend_rmi<0x0E, "vpblendw", X86Blendi, v8i16,
                                  VR128, load, i128mem, 0, SSEPackedInt,
                                  SchedWriteBlend.XMM, BlendCommuteImm8>,
                                  VEX_4V, VEX_WIG;
}

let Predicates = [HasAVX2] in {
  defm VPBLENDWY : SS41I_blend_rmi<0x0E, "vpblendw", X86Blendi, v16i16,
                                   VR256, load, i256mem, 0, SSEPackedInt,
                                   SchedWriteBlend.YMM, BlendCommuteImm8>,
                                   VEX_4V, VEX_L, VEX_WIG;
}

defm BLENDPS : SS41I_blend_rmi<0x0C, "blendps", X86Blendi, v4f32,
                               VR128, memop, f128mem, 1, SSEPackedSingle,
                               SchedWriteFBlend.XMM, BlendCommuteImm4>;
defm BLENDPD : SS41I_blend_rmi<0x0D, "blendpd", X86Blendi, v2f64,
                               VR128, memop, f128mem, 1, SSEPackedDouble,
                               SchedWriteFBlend.XMM, BlendCommuteImm2>;
defm PBLENDW : SS41I_blend_rmi<0x0E, "pblendw", X86Blendi, v8i16,
                               VR128, memop, i128mem, 1, SSEPackedInt,
                               SchedWriteBlend.XMM, BlendCommuteImm8>;

// For insertion into the zero index (low half) of a 256-bit vector, it is
// more efficient to generate a blend with immediate instead of an insert*128.
let Predicates = [HasAVX] in {
def : Pat<(insert_subvector (v4f64 VR256:$src1), (v2f64 VR128:$src2), (iPTR 0)),
          (VBLENDPDYrri VR256:$src1,
                        (INSERT_SUBREG (v4f64 (IMPLICIT_DEF)),
                                       VR128:$src2, sub_xmm), 0x3)>;
def : Pat<(insert_subvector (v8f32 VR256:$src1), (v4f32 VR128:$src2), (iPTR 0)),
          (VBLENDPSYrri VR256:$src1,
                        (INSERT_SUBREG (v8f32 (IMPLICIT_DEF)),
                                       VR128:$src2, sub_xmm), 0xf)>;
}

/// SS41I_quaternary_int_avx - AVX SSE 4.1 with 4 operators
multiclass SS41I_quaternary_int_avx<bits<8> opc, string OpcodeStr,
                                    RegisterClass RC, X86MemOperand x86memop,
                                    PatFrag mem_frag, Intrinsic IntId,
                                    X86FoldableSchedWrite sched> {
  def rr : Ii8Reg<opc, MRMSrcReg, (outs RC:$dst),
                  (ins RC:$src1, RC:$src2, RC:$src3),
                  !strconcat(OpcodeStr,
                    "\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}"),
                  [(set RC:$dst, (IntId RC:$src1, RC:$src2, RC:$src3))],
                  SSEPackedInt>, TAPD, VEX_4V,
                Sched<[sched]>;

  def rm : Ii8Reg<opc, MRMSrcMem, (outs RC:$dst),
                  (ins RC:$src1, x86memop:$src2, RC:$src3),
                  !strconcat(OpcodeStr,
                    "\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}"),
                  [(set RC:$dst,
                        (IntId RC:$src1, (mem_frag addr:$src2),
                               RC:$src3))], SSEPackedInt>, TAPD, VEX_4V,
                Sched<[sched.Folded, sched.ReadAfterFold,
                       // x86memop:$src2
                       ReadDefault, ReadDefault, ReadDefault, ReadDefault,
                       ReadDefault,
                       // RC::$src3
                       sched.ReadAfterFold]>;
}

let Predicates = [HasAVX] in {
let ExeDomain = SSEPackedDouble in {
defm VBLENDVPD  : SS41I_quaternary_int_avx<0x4B, "vblendvpd", VR128, f128mem,
                                           load, int_x86_sse41_blendvpd,
                                           SchedWriteFVarBlend.XMM>;
defm VBLENDVPDY : SS41I_quaternary_int_avx<0x4B, "vblendvpd", VR256, f256mem,
                                  loadv4f64, int_x86_avx_blendv_pd_256,
                                  SchedWriteFVarBlend.YMM>, VEX_L;
} // ExeDomain = SSEPackedDouble
let ExeDomain = SSEPackedSingle in {
defm VBLENDVPS  : SS41I_quaternary_int_avx<0x4A, "vblendvps", VR128, f128mem,
                                           load, int_x86_sse41_blendvps,
                                           SchedWriteFVarBlend.XMM>;
defm VBLENDVPSY : SS41I_quaternary_int_avx<0x4A, "vblendvps", VR256, f256mem,
                                  loadv8f32, int_x86_avx_blendv_ps_256,
                                  SchedWriteFVarBlend.YMM>, VEX_L;
} // ExeDomain = SSEPackedSingle
defm VPBLENDVB  : SS41I_quaternary_int_avx<0x4C, "vpblendvb", VR128, i128mem,
                                           load, int_x86_sse41_pblendvb,
                                           SchedWriteVarBlend.XMM>;
}

let Predicates = [HasAVX2] in {
defm VPBLENDVBY : SS41I_quaternary_int_avx<0x4C, "vpblendvb", VR256, i256mem,
                                      load, int_x86_avx2_pblendvb,
                                      SchedWriteVarBlend.YMM>, VEX_L;
}

let Predicates = [HasAVX] in {
  def : Pat<(v16i8 (vselect (v16i8 VR128:$mask), (v16i8 VR128:$src1),
                            (v16i8 VR128:$src2))),
            (VPBLENDVBrr VR128:$src2, VR128:$src1, VR128:$mask)>;
  def : Pat<(v4i32 (vselect (v4i32 VR128:$mask), (v4i32 VR128:$src1),
                            (v4i32 VR128:$src2))),
            (VBLENDVPSrr VR128:$src2, VR128:$src1, VR128:$mask)>;
  def : Pat<(v4f32 (vselect (v4i32 VR128:$mask), (v4f32 VR128:$src1),
                            (v4f32 VR128:$src2))),
            (VBLENDVPSrr VR128:$src2, VR128:$src1, VR128:$mask)>;
  def : Pat<(v2i64 (vselect (v2i64 VR128:$mask), (v2i64 VR128:$src1),
                            (v2i64 VR128:$src2))),
            (VBLENDVPDrr VR128:$src2, VR128:$src1, VR128:$mask)>;
  def : Pat<(v2f64 (vselect (v2i64 VR128:$mask), (v2f64 VR128:$src1),
                            (v2f64 VR128:$src2))),
            (VBLENDVPDrr VR128:$src2, VR128:$src1, VR128:$mask)>;
  def : Pat<(v8i32 (vselect (v8i32 VR256:$mask), (v8i32 VR256:$src1),
                            (v8i32 VR256:$src2))),
            (VBLENDVPSYrr VR256:$src2, VR256:$src1, VR256:$mask)>;
  def : Pat<(v8f32 (vselect (v8i32 VR256:$mask), (v8f32 VR256:$src1),
                            (v8f32 VR256:$src2))),
            (VBLENDVPSYrr VR256:$src2, VR256:$src1, VR256:$mask)>;
  def : Pat<(v4i64 (vselect (v4i64 VR256:$mask), (v4i64 VR256:$src1),
                            (v4i64 VR256:$src2))),
            (VBLENDVPDYrr VR256:$src2, VR256:$src1, VR256:$mask)>;
  def : Pat<(v4f64 (vselect (v4i64 VR256:$mask), (v4f64 VR256:$src1),
                            (v4f64 VR256:$src2))),
            (VBLENDVPDYrr VR256:$src2, VR256:$src1, VR256:$mask)>;
}

let Predicates = [HasAVX2] in {
  def : Pat<(v32i8 (vselect (v32i8 VR256:$mask), (v32i8 VR256:$src1),
                            (v32i8 VR256:$src2))),
            (VPBLENDVBYrr VR256:$src2, VR256:$src1, VR256:$mask)>;
}

// Prefer a movss or movsd over a blendps when optimizing for size. these were
// changed to use blends because blends have better throughput on sandybridge
// and haswell, but movs[s/d] are 1-2 byte shorter instructions.
let Predicates = [HasAVX, OptForSpeed] in {
  def : Pat<(v4f32 (X86vzmovl (v4f32 VR128:$src))),
            (VBLENDPSrri (v4f32 (V_SET0)), VR128:$src, (i8 1))>;
  def : Pat<(v4i32 (X86vzmovl (v4i32 VR128:$src))),
            (VPBLENDWrri (v4i32 (V_SET0)), VR128:$src, (i8 3))>;

  def : Pat<(v4f32 (X86Movss VR128:$src1, VR128:$src2)),
            (VBLENDPSrri VR128:$src1, VR128:$src2, (i8 1))>;
  def : Pat<(v4f32 (X86Movss VR128:$src1, (loadv4f32 addr:$src2))),
            (VBLENDPSrmi VR128:$src1, addr:$src2, (i8 1))>;
  def : Pat<(v4f32 (X86Movss (loadv4f32 addr:$src2), VR128:$src1)),
            (VBLENDPSrmi VR128:$src1, addr:$src2, (i8 0xe))>;

  def : Pat<(v2f64 (X86Movsd VR128:$src1, VR128:$src2)),
            (VBLENDPDrri VR128:$src1, VR128:$src2, (i8 1))>;
  def : Pat<(v2f64 (X86Movsd VR128:$src1, (loadv2f64 addr:$src2))),
            (VBLENDPDrmi VR128:$src1, addr:$src2, (i8 1))>;
  def : Pat<(v2f64 (X86Movsd (loadv2f64 addr:$src2), VR128:$src1)),
            (VBLENDPDrmi VR128:$src1, addr:$src2, (i8 2))>;

  // Move low f32 and clear high bits.
  def : Pat<(v8f32 (X86vzmovl (v8f32 VR256:$src))),
            (SUBREG_TO_REG (i32 0),
             (v4f32 (VBLENDPSrri (v4f32 (V_SET0)),
                          (v4f32 (EXTRACT_SUBREG (v8f32 VR256:$src), sub_xmm)),
                          (i8 1))), sub_xmm)>;
  def : Pat<(v8i32 (X86vzmovl (v8i32 VR256:$src))),
            (SUBREG_TO_REG (i32 0),
             (v4i32 (VPBLENDWrri (v4i32 (V_SET0)),
                          (v4i32 (EXTRACT_SUBREG (v8i32 VR256:$src), sub_xmm)),
                          (i8 3))), sub_xmm)>;

  def : Pat<(v4f64 (X86vzmovl (v4f64 VR256:$src))),
            (SUBREG_TO_REG (i32 0),
             (v2f64 (VBLENDPDrri (v2f64 (V_SET0)),
                          (v2f64 (EXTRACT_SUBREG (v4f64 VR256:$src), sub_xmm)),
                          (i8 1))), sub_xmm)>;
  def : Pat<(v4i64 (X86vzmovl (v4i64 VR256:$src))),
            (SUBREG_TO_REG (i32 0),
             (v2i64 (VPBLENDWrri (v2i64 (V_SET0)),
                          (v2i64 (EXTRACT_SUBREG (v4i64 VR256:$src), sub_xmm)),
                          (i8 0xf))), sub_xmm)>;
}

// Prefer a movss or movsd over a blendps when optimizing for size. these were
// changed to use blends because blends have better throughput on sandybridge
// and haswell, but movs[s/d] are 1-2 byte shorter instructions.
let Predicates = [UseSSE41, OptForSpeed] in {
  // With SSE41 we can use blends for these patterns.
  def : Pat<(v4f32 (X86vzmovl (v4f32 VR128:$src))),
            (BLENDPSrri (v4f32 (V_SET0)), VR128:$src, (i8 1))>;
  def : Pat<(v4i32 (X86vzmovl (v4i32 VR128:$src))),
            (PBLENDWrri (v4i32 (V_SET0)), VR128:$src, (i8 3))>;

  def : Pat<(v4f32 (X86Movss VR128:$src1, VR128:$src2)),
            (BLENDPSrri VR128:$src1, VR128:$src2, (i8 1))>;
  def : Pat<(v4f32 (X86Movss VR128:$src1, (memopv4f32 addr:$src2))),
            (BLENDPSrmi VR128:$src1, addr:$src2, (i8 1))>;
  def : Pat<(v4f32 (X86Movss (memopv4f32 addr:$src2), VR128:$src1)),
            (BLENDPSrmi VR128:$src1, addr:$src2, (i8 0xe))>;

  def : Pat<(v2f64 (X86Movsd VR128:$src1, VR128:$src2)),
            (BLENDPDrri VR128:$src1, VR128:$src2, (i8 1))>;
  def : Pat<(v2f64 (X86Movsd VR128:$src1, (memopv2f64 addr:$src2))),
            (BLENDPDrmi VR128:$src1, addr:$src2, (i8 1))>;
  def : Pat<(v2f64 (X86Movsd (memopv2f64 addr:$src2), VR128:$src1)),
            (BLENDPDrmi VR128:$src1, addr:$src2, (i8 2))>;
}


/// SS41I_ternary_int - SSE 4.1 ternary operator
let Uses = [XMM0], Constraints = "$src1 = $dst" in {
  multiclass SS41I_ternary_int<bits<8> opc, string OpcodeStr, PatFrag mem_frag,
                               X86MemOperand x86memop, Intrinsic IntId,
                               X86FoldableSchedWrite sched> {
    def rr0 : SS48I<opc, MRMSrcReg, (outs VR128:$dst),
                    (ins VR128:$src1, VR128:$src2),
                    !strconcat(OpcodeStr,
                     "\t{%xmm0, $src2, $dst|$dst, $src2, xmm0}"),
                    [(set VR128:$dst, (IntId VR128:$src1, VR128:$src2, XMM0))]>,
                    Sched<[sched]>;

    def rm0 : SS48I<opc, MRMSrcMem, (outs VR128:$dst),
                    (ins VR128:$src1, x86memop:$src2),
                    !strconcat(OpcodeStr,
                     "\t{%xmm0, $src2, $dst|$dst, $src2, xmm0}"),
                    [(set VR128:$dst,
                      (IntId VR128:$src1,
                       (mem_frag addr:$src2), XMM0))]>,
                    Sched<[sched.Folded, sched.ReadAfterFold]>;
  }
}

let ExeDomain = SSEPackedDouble in
defm BLENDVPD : SS41I_ternary_int<0x15, "blendvpd", memop, f128mem,
                                  int_x86_sse41_blendvpd, SchedWriteFVarBlend.XMM>;
let ExeDomain = SSEPackedSingle in
defm BLENDVPS : SS41I_ternary_int<0x14, "blendvps", memop, f128mem,
                                  int_x86_sse41_blendvps, SchedWriteFVarBlend.XMM>;
defm PBLENDVB : SS41I_ternary_int<0x10, "pblendvb", memop, i128mem,
                                  int_x86_sse41_pblendvb, SchedWriteVarBlend.XMM>;

// Aliases with the implicit xmm0 argument
def : InstAlias<"blendvpd\t{$src2, $dst|$dst, $src2}",
                (BLENDVPDrr0 VR128:$dst, VR128:$src2), 0>;
def : InstAlias<"blendvpd\t{$src2, $dst|$dst, $src2}",
                (BLENDVPDrm0 VR128:$dst, f128mem:$src2), 0>;
def : InstAlias<"blendvps\t{$src2, $dst|$dst, $src2}",
                (BLENDVPSrr0 VR128:$dst, VR128:$src2), 0>;
def : InstAlias<"blendvps\t{$src2, $dst|$dst, $src2}",
                (BLENDVPSrm0 VR128:$dst, f128mem:$src2), 0>;
def : InstAlias<"pblendvb\t{$src2, $dst|$dst, $src2}",
                (PBLENDVBrr0 VR128:$dst, VR128:$src2), 0>;
def : InstAlias<"pblendvb\t{$src2, $dst|$dst, $src2}",
                (PBLENDVBrm0 VR128:$dst, i128mem:$src2), 0>;

let Predicates = [UseSSE41] in {
  def : Pat<(v16i8 (vselect (v16i8 XMM0), (v16i8 VR128:$src1),
                            (v16i8 VR128:$src2))),
            (PBLENDVBrr0 VR128:$src2, VR128:$src1)>;
  def : Pat<(v4i32 (vselect (v4i32 XMM0), (v4i32 VR128:$src1),
                            (v4i32 VR128:$src2))),
            (BLENDVPSrr0 VR128:$src2, VR128:$src1)>;
  def : Pat<(v4f32 (vselect (v4i32 XMM0), (v4f32 VR128:$src1),
                            (v4f32 VR128:$src2))),
            (BLENDVPSrr0 VR128:$src2, VR128:$src1)>;
  def : Pat<(v2i64 (vselect (v2i64 XMM0), (v2i64 VR128:$src1),
                            (v2i64 VR128:$src2))),
            (BLENDVPDrr0 VR128:$src2, VR128:$src1)>;
  def : Pat<(v2f64 (vselect (v2i64 XMM0), (v2f64 VR128:$src1),
                            (v2f64 VR128:$src2))),
            (BLENDVPDrr0 VR128:$src2, VR128:$src1)>;
}

let AddedComplexity = 400 in { // Prefer non-temporal versions

let Predicates = [HasAVX, NoVLX] in
def VMOVNTDQArm : SS48I<0x2A, MRMSrcMem, (outs VR128:$dst), (ins i128mem:$src),
                        "vmovntdqa\t{$src, $dst|$dst, $src}", []>,
                        Sched<[SchedWriteVecMoveLSNT.XMM.RM]>, VEX, VEX_WIG;
let Predicates = [HasAVX2, NoVLX] in
def VMOVNTDQAYrm : SS48I<0x2A, MRMSrcMem, (outs VR256:$dst), (ins i256mem:$src),
                         "vmovntdqa\t{$src, $dst|$dst, $src}", []>,
                         Sched<[SchedWriteVecMoveLSNT.YMM.RM]>, VEX, VEX_L, VEX_WIG;
def MOVNTDQArm : SS48I<0x2A, MRMSrcMem, (outs VR128:$dst), (ins i128mem:$src),
                       "movntdqa\t{$src, $dst|$dst, $src}", []>,
                       Sched<[SchedWriteVecMoveLSNT.XMM.RM]>;

let Predicates = [HasAVX2, NoVLX] in {
  def : Pat<(v8f32 (alignednontemporalload addr:$src)),
            (VMOVNTDQAYrm addr:$src)>;
  def : Pat<(v4f64 (alignednontemporalload addr:$src)),
            (VMOVNTDQAYrm addr:$src)>;
  def : Pat<(v4i64 (alignednontemporalload addr:$src)),
            (VMOVNTDQAYrm addr:$src)>;
  def : Pat<(v8i32 (alignednontemporalload addr:$src)),
            (VMOVNTDQAYrm addr:$src)>;
  def : Pat<(v16i16 (alignednontemporalload addr:$src)),
            (VMOVNTDQAYrm addr:$src)>;
  def : Pat<(v32i8 (alignednontemporalload addr:$src)),
            (VMOVNTDQAYrm addr:$src)>;
}

let Predicates = [HasAVX, NoVLX] in {
  def : Pat<(v4f32 (alignednontemporalload addr:$src)),
            (VMOVNTDQArm addr:$src)>;
  def : Pat<(v2f64 (alignednontemporalload addr:$src)),
            (VMOVNTDQArm addr:$src)>;
  def : Pat<(v2i64 (alignednontemporalload addr:$src)),
            (VMOVNTDQArm addr:$src)>;
  def : Pat<(v4i32 (alignednontemporalload addr:$src)),
            (VMOVNTDQArm addr:$src)>;
  def : Pat<(v8i16 (alignednontemporalload addr:$src)),
            (VMOVNTDQArm addr:$src)>;
  def : Pat<(v16i8 (alignednontemporalload addr:$src)),
            (VMOVNTDQArm addr:$src)>;
}

let Predicates = [UseSSE41] in {
  def : Pat<(v4f32 (alignednontemporalload addr:$src)),
            (MOVNTDQArm addr:$src)>;
  def : Pat<(v2f64 (alignednontemporalload addr:$src)),
            (MOVNTDQArm addr:$src)>;
  def : Pat<(v2i64 (alignednontemporalload addr:$src)),
            (MOVNTDQArm addr:$src)>;
  def : Pat<(v4i32 (alignednontemporalload addr:$src)),
            (MOVNTDQArm addr:$src)>;
  def : Pat<(v8i16 (alignednontemporalload addr:$src)),
            (MOVNTDQArm addr:$src)>;
  def : Pat<(v16i8 (alignednontemporalload addr:$src)),
            (MOVNTDQArm addr:$src)>;
}

} // AddedComplexity

//===----------------------------------------------------------------------===//
// SSE4.2 - Compare Instructions
//===----------------------------------------------------------------------===//

/// SS42I_binop_rm - Simple SSE 4.2 binary operator
multiclass SS42I_binop_rm<bits<8> opc, string OpcodeStr, SDNode OpNode,
                          ValueType OpVT, RegisterClass RC, PatFrag memop_frag,
                          X86MemOperand x86memop, X86FoldableSchedWrite sched,
                          bit Is2Addr = 1> {
  def rr : SS428I<opc, MRMSrcReg, (outs RC:$dst),
       (ins RC:$src1, RC:$src2),
       !if(Is2Addr,
           !strconcat(OpcodeStr, "\t{$src2, $dst|$dst, $src2}"),
           !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}")),
       [(set RC:$dst, (OpVT (OpNode RC:$src1, RC:$src2)))]>,
       Sched<[sched]>;
  def rm : SS428I<opc, MRMSrcMem, (outs RC:$dst),
       (ins RC:$src1, x86memop:$src2),
       !if(Is2Addr,
           !strconcat(OpcodeStr, "\t{$src2, $dst|$dst, $src2}"),
           !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}")),
       [(set RC:$dst,
         (OpVT (OpNode RC:$src1, (memop_frag addr:$src2))))]>,
       Sched<[sched.Folded, sched.ReadAfterFold]>;
}

let Predicates = [HasAVX] in
  defm VPCMPGTQ : SS42I_binop_rm<0x37, "vpcmpgtq", X86pcmpgt, v2i64, VR128,
                                 load, i128mem, SchedWriteVecALU.XMM, 0>,
                                 VEX_4V, VEX_WIG;

let Predicates = [HasAVX2] in
  defm VPCMPGTQY : SS42I_binop_rm<0x37, "vpcmpgtq", X86pcmpgt, v4i64, VR256,
                                  load, i256mem, SchedWriteVecALU.YMM, 0>,
                                  VEX_4V, VEX_L, VEX_WIG;

let Constraints = "$src1 = $dst" in
  defm PCMPGTQ : SS42I_binop_rm<0x37, "pcmpgtq", X86pcmpgt, v2i64, VR128,
                                memop, i128mem, SchedWriteVecALU.XMM>;

//===----------------------------------------------------------------------===//
// SSE4.2 - String/text Processing Instructions
//===----------------------------------------------------------------------===//

multiclass pcmpistrm_SS42AI<string asm> {
  def rr : SS42AI<0x62, MRMSrcReg, (outs),
    (ins VR128:$src1, VR128:$src2, u8imm:$src3),
    !strconcat(asm, "\t{$src3, $src2, $src1|$src1, $src2, $src3}"),
    []>, Sched<[WritePCmpIStrM]>;
  let mayLoad = 1 in
  def rm :SS42AI<0x62, MRMSrcMem, (outs),
    (ins VR128:$src1, i128mem:$src2, u8imm:$src3),
    !strconcat(asm, "\t{$src3, $src2, $src1|$src1, $src2, $src3}"),
    []>, Sched<[WritePCmpIStrM.Folded, WritePCmpIStrM.ReadAfterFold]>;
}

let Defs = [XMM0, EFLAGS], hasSideEffects = 0 in {
  let Predicates = [HasAVX] in
  defm VPCMPISTRM : pcmpistrm_SS42AI<"vpcmpistrm">, VEX;
  defm PCMPISTRM  : pcmpistrm_SS42AI<"pcmpistrm"> ;
}

multiclass SS42AI_pcmpestrm<string asm> {
  def rr : SS42AI<0x60, MRMSrcReg, (outs),
    (ins VR128:$src1, VR128:$src3, u8imm:$src5),
    !strconcat(asm, "\t{$src5, $src3, $src1|$src1, $src3, $src5}"),
    []>, Sched<[WritePCmpEStrM]>;
  let mayLoad = 1 in
  def rm : SS42AI<0x60, MRMSrcMem, (outs),
    (ins VR128:$src1, i128mem:$src3, u8imm:$src5),
    !strconcat(asm, "\t{$src5, $src3, $src1|$src1, $src3, $src5}"),
    []>, Sched<[WritePCmpEStrM.Folded, WritePCmpEStrM.ReadAfterFold]>;
}

let Defs = [XMM0, EFLAGS], Uses = [EAX, EDX], hasSideEffects = 0 in {
  let Predicates = [HasAVX] in
  defm VPCMPESTRM : SS42AI_pcmpestrm<"vpcmpestrm">, VEX;
  defm PCMPESTRM :  SS42AI_pcmpestrm<"pcmpestrm">;
}

multiclass SS42AI_pcmpistri<string asm> {
  def rr : SS42AI<0x63, MRMSrcReg, (outs),
    (ins VR128:$src1, VR128:$src2, u8imm:$src3),
    !strconcat(asm, "\t{$src3, $src2, $src1|$src1, $src2, $src3}"),
    []>, Sched<[WritePCmpIStrI]>;
  let mayLoad = 1 in
  def rm : SS42AI<0x63, MRMSrcMem, (outs),
    (ins VR128:$src1, i128mem:$src2, u8imm:$src3),
    !strconcat(asm, "\t{$src3, $src2, $src1|$src1, $src2, $src3}"),
    []>, Sched<[WritePCmpIStrI.Folded, WritePCmpIStrI.ReadAfterFold]>;
}

let Defs = [ECX, EFLAGS], hasSideEffects = 0 in {
  let Predicates = [HasAVX] in
  defm VPCMPISTRI : SS42AI_pcmpistri<"vpcmpistri">, VEX;
  defm PCMPISTRI  : SS42AI_pcmpistri<"pcmpistri">;
}

multiclass SS42AI_pcmpestri<string asm> {
  def rr : SS42AI<0x61, MRMSrcReg, (outs),
    (ins VR128:$src1, VR128:$src3, u8imm:$src5),
    !strconcat(asm, "\t{$src5, $src3, $src1|$src1, $src3, $src5}"),
    []>, Sched<[WritePCmpEStrI]>;
  let mayLoad = 1 in
  def rm : SS42AI<0x61, MRMSrcMem, (outs),
    (ins VR128:$src1, i128mem:$src3, u8imm:$src5),
    !strconcat(asm, "\t{$src5, $src3, $src1|$src1, $src3, $src5}"),
    []>, Sched<[WritePCmpEStrI.Folded, WritePCmpEStrI.ReadAfterFold]>;
}

let Defs = [ECX, EFLAGS], Uses = [EAX, EDX], hasSideEffects = 0 in {
  let Predicates = [HasAVX] in
  defm VPCMPESTRI : SS42AI_pcmpestri<"vpcmpestri">, VEX;
  defm PCMPESTRI  : SS42AI_pcmpestri<"pcmpestri">;
}

//===----------------------------------------------------------------------===//
// SSE4.2 - CRC Instructions
//===----------------------------------------------------------------------===//

// No CRC instructions have AVX equivalents

// crc intrinsic instruction
// This set of instructions are only rm, the only difference is the size
// of r and m.
class SS42I_crc32r<bits<8> opc, string asm, RegisterClass RCOut,
                   RegisterClass RCIn, SDPatternOperator Int> :
  SS42FI<opc, MRMSrcReg, (outs RCOut:$dst), (ins RCOut:$src1, RCIn:$src2),
         !strconcat(asm, "\t{$src2, $src1|$src1, $src2}"),
         [(set RCOut:$dst, (Int RCOut:$src1, RCIn:$src2))]>,
         Sched<[WriteCRC32]>;

class SS42I_crc32m<bits<8> opc, string asm, RegisterClass RCOut,
                   X86MemOperand x86memop, SDPatternOperator Int> :
  SS42FI<opc, MRMSrcMem, (outs RCOut:$dst), (ins RCOut:$src1, x86memop:$src2),
         !strconcat(asm, "\t{$src2, $src1|$src1, $src2}"),
         [(set RCOut:$dst, (Int RCOut:$src1, (load addr:$src2)))]>,
         Sched<[WriteCRC32.Folded, WriteCRC32.ReadAfterFold]>;

let Constraints = "$src1 = $dst" in {
  def CRC32r32m8  : SS42I_crc32m<0xF0, "crc32{b}", GR32, i8mem,
                                 int_x86_sse42_crc32_32_8>;
  def CRC32r32r8  : SS42I_crc32r<0xF0, "crc32{b}", GR32, GR8,
                                 int_x86_sse42_crc32_32_8>;
  def CRC32r32m16 : SS42I_crc32m<0xF1, "crc32{w}", GR32, i16mem,
                                 int_x86_sse42_crc32_32_16>, OpSize16;
  def CRC32r32r16 : SS42I_crc32r<0xF1, "crc32{w}", GR32, GR16,
                                 int_x86_sse42_crc32_32_16>, OpSize16;
  def CRC32r32m32 : SS42I_crc32m<0xF1, "crc32{l}", GR32, i32mem,
                                 int_x86_sse42_crc32_32_32>, OpSize32;
  def CRC32r32r32 : SS42I_crc32r<0xF1, "crc32{l}", GR32, GR32,
                                 int_x86_sse42_crc32_32_32>, OpSize32;
  def CRC32r64m64 : SS42I_crc32m<0xF1, "crc32{q}", GR64, i64mem,
                                 int_x86_sse42_crc32_64_64>, REX_W;
  def CRC32r64r64 : SS42I_crc32r<0xF1, "crc32{q}", GR64, GR64,
                                 int_x86_sse42_crc32_64_64>, REX_W;
  let hasSideEffects = 0 in {
    let mayLoad = 1 in
    def CRC32r64m8 : SS42I_crc32m<0xF0, "crc32{b}", GR64, i8mem,
                                   null_frag>, REX_W;
    def CRC32r64r8 : SS42I_crc32r<0xF0, "crc32{b}", GR64, GR8,
                                   null_frag>, REX_W;
  }
}

//===----------------------------------------------------------------------===//
// SHA-NI Instructions
//===----------------------------------------------------------------------===//

// FIXME: Is there a better scheduler class for SHA than WriteVecIMul?
multiclass SHAI_binop<bits<8> Opc, string OpcodeStr, Intrinsic IntId,
                      X86FoldableSchedWrite sched, bit UsesXMM0 = 0> {
  def rr : I<Opc, MRMSrcReg, (outs VR128:$dst),
             (ins VR128:$src1, VR128:$src2),
             !if(UsesXMM0,
                 !strconcat(OpcodeStr, "\t{%xmm0, $src2, $dst|$dst, $src2, xmm0}"),
                 !strconcat(OpcodeStr, "\t{$src2, $dst|$dst, $src2}")),
             [!if(UsesXMM0,
                  (set VR128:$dst, (IntId VR128:$src1, VR128:$src2, XMM0)),
                  (set VR128:$dst, (IntId VR128:$src1, VR128:$src2)))]>,
             T8, Sched<[sched]>;

  def rm : I<Opc, MRMSrcMem, (outs VR128:$dst),
             (ins VR128:$src1, i128mem:$src2),
             !if(UsesXMM0,
                 !strconcat(OpcodeStr, "\t{%xmm0, $src2, $dst|$dst, $src2, xmm0}"),
                 !strconcat(OpcodeStr, "\t{$src2, $dst|$dst, $src2}")),
             [!if(UsesXMM0,
                  (set VR128:$dst, (IntId VR128:$src1,
                    (memop addr:$src2), XMM0)),
                  (set VR128:$dst, (IntId VR128:$src1,
                    (memop addr:$src2))))]>, T8,
             Sched<[sched.Folded, sched.ReadAfterFold]>;
}

let Constraints = "$src1 = $dst", Predicates = [HasSHA] in {
  def SHA1RNDS4rri : Ii8<0xCC, MRMSrcReg, (outs VR128:$dst),
                         (ins VR128:$src1, VR128:$src2, u8imm:$src3),
                         "sha1rnds4\t{$src3, $src2, $dst|$dst, $src2, $src3}",
                         [(set VR128:$dst,
                           (int_x86_sha1rnds4 VR128:$src1, VR128:$src2,
                            (i8 imm:$src3)))]>, TA,
                         Sched<[SchedWriteVecIMul.XMM]>;
  def SHA1RNDS4rmi : Ii8<0xCC, MRMSrcMem, (outs VR128:$dst),
                         (ins VR128:$src1, i128mem:$src2, u8imm:$src3),
                         "sha1rnds4\t{$src3, $src2, $dst|$dst, $src2, $src3}",
                         [(set VR128:$dst,
                           (int_x86_sha1rnds4 VR128:$src1,
                            (memop addr:$src2),
                            (i8 imm:$src3)))]>, TA,
                         Sched<[SchedWriteVecIMul.XMM.Folded,
                                SchedWriteVecIMul.XMM.ReadAfterFold]>;

  defm SHA1NEXTE : SHAI_binop<0xC8, "sha1nexte", int_x86_sha1nexte,
                              SchedWriteVecIMul.XMM>;
  defm SHA1MSG1  : SHAI_binop<0xC9, "sha1msg1", int_x86_sha1msg1,
                              SchedWriteVecIMul.XMM>;
  defm SHA1MSG2  : SHAI_binop<0xCA, "sha1msg2", int_x86_sha1msg2,
                              SchedWriteVecIMul.XMM>;

  let Uses=[XMM0] in
  defm SHA256RNDS2 : SHAI_binop<0xCB, "sha256rnds2", int_x86_sha256rnds2,
                                SchedWriteVecIMul.XMM, 1>;

  defm SHA256MSG1 : SHAI_binop<0xCC, "sha256msg1", int_x86_sha256msg1,
                               SchedWriteVecIMul.XMM>;
  defm SHA256MSG2 : SHAI_binop<0xCD, "sha256msg2", int_x86_sha256msg2,
                               SchedWriteVecIMul.XMM>;
}

// Aliases with explicit %xmm0
def : InstAlias<"sha256rnds2\t{$src2, $dst|$dst, $src2}",
                (SHA256RNDS2rr VR128:$dst, VR128:$src2), 0>;
def : InstAlias<"sha256rnds2\t{$src2, $dst|$dst, $src2}",
                (SHA256RNDS2rm VR128:$dst, i128mem:$src2), 0>;

//===----------------------------------------------------------------------===//
// AES-NI Instructions
//===----------------------------------------------------------------------===//

multiclass AESI_binop_rm_int<bits<8> opc, string OpcodeStr,
                             Intrinsic IntId, PatFrag ld_frag,
                             bit Is2Addr = 0, RegisterClass RC = VR128,
                             X86MemOperand MemOp = i128mem> {
  let AsmString = OpcodeStr##
                  !if(Is2Addr, "\t{$src2, $dst|$dst, $src2}",
                               "\t{$src2, $src1, $dst|$dst, $src1, $src2}") in {
    def rr : AES8I<opc, MRMSrcReg, (outs RC:$dst),
                   (ins RC:$src1, RC:$src2), "",
                   [(set RC:$dst, (IntId RC:$src1, RC:$src2))]>,
                   Sched<[WriteAESDecEnc]>;
    def rm : AES8I<opc, MRMSrcMem, (outs RC:$dst),
                   (ins RC:$src1, MemOp:$src2), "",
                   [(set RC:$dst, (IntId RC:$src1, (ld_frag addr:$src2)))]>,
                   Sched<[WriteAESDecEnc.Folded, WriteAESDecEnc.ReadAfterFold]>;
  }
}

// Perform One Round of an AES Encryption/Decryption Flow
let Predicates = [HasAVX, NoVLX_Or_NoVAES, HasAES] in {
  defm VAESENC          : AESI_binop_rm_int<0xDC, "vaesenc",
                         int_x86_aesni_aesenc, load>, VEX_4V, VEX_WIG;
  defm VAESENCLAST      : AESI_binop_rm_int<0xDD, "vaesenclast",
                         int_x86_aesni_aesenclast, load>, VEX_4V, VEX_WIG;
  defm VAESDEC          : AESI_binop_rm_int<0xDE, "vaesdec",
                         int_x86_aesni_aesdec, load>, VEX_4V, VEX_WIG;
  defm VAESDECLAST      : AESI_binop_rm_int<0xDF, "vaesdeclast",
                         int_x86_aesni_aesdeclast, load>, VEX_4V, VEX_WIG;
}

let Predicates = [NoVLX, HasVAES] in {
  defm VAESENCY         : AESI_binop_rm_int<0xDC, "vaesenc",
                         int_x86_aesni_aesenc_256, load, 0, VR256,
                         i256mem>, VEX_4V, VEX_L, VEX_WIG;
  defm VAESENCLASTY     : AESI_binop_rm_int<0xDD, "vaesenclast",
                         int_x86_aesni_aesenclast_256, load, 0, VR256,
                         i256mem>, VEX_4V, VEX_L, VEX_WIG;
  defm VAESDECY         : AESI_binop_rm_int<0xDE, "vaesdec",
                         int_x86_aesni_aesdec_256, load, 0, VR256,
                         i256mem>, VEX_4V, VEX_L, VEX_WIG;
  defm VAESDECLASTY     : AESI_binop_rm_int<0xDF, "vaesdeclast",
                         int_x86_aesni_aesdeclast_256, load, 0, VR256,
                         i256mem>, VEX_4V, VEX_L, VEX_WIG;
}

let Constraints = "$src1 = $dst" in {
  defm AESENC          : AESI_binop_rm_int<0xDC, "aesenc",
                         int_x86_aesni_aesenc, memop, 1>;
  defm AESENCLAST      : AESI_binop_rm_int<0xDD, "aesenclast",
                         int_x86_aesni_aesenclast, memop, 1>;
  defm AESDEC          : AESI_binop_rm_int<0xDE, "aesdec",
                         int_x86_aesni_aesdec, memop, 1>;
  defm AESDECLAST      : AESI_binop_rm_int<0xDF, "aesdeclast",
                         int_x86_aesni_aesdeclast, memop, 1>;
}

// Perform the AES InvMixColumn Transformation
let Predicates = [HasAVX, HasAES] in {
  def VAESIMCrr : AES8I<0xDB, MRMSrcReg, (outs VR128:$dst),
      (ins VR128:$src1),
      "vaesimc\t{$src1, $dst|$dst, $src1}",
      [(set VR128:$dst,
        (int_x86_aesni_aesimc VR128:$src1))]>, Sched<[WriteAESIMC]>,
      VEX, VEX_WIG;
  def VAESIMCrm : AES8I<0xDB, MRMSrcMem, (outs VR128:$dst),
      (ins i128mem:$src1),
      "vaesimc\t{$src1, $dst|$dst, $src1}",
      [(set VR128:$dst, (int_x86_aesni_aesimc (load addr:$src1)))]>,
      Sched<[WriteAESIMC.Folded]>, VEX, VEX_WIG;
}
def AESIMCrr : AES8I<0xDB, MRMSrcReg, (outs VR128:$dst),
  (ins VR128:$src1),
  "aesimc\t{$src1, $dst|$dst, $src1}",
  [(set VR128:$dst,
    (int_x86_aesni_aesimc VR128:$src1))]>, Sched<[WriteAESIMC]>;
def AESIMCrm : AES8I<0xDB, MRMSrcMem, (outs VR128:$dst),
  (ins i128mem:$src1),
  "aesimc\t{$src1, $dst|$dst, $src1}",
  [(set VR128:$dst, (int_x86_aesni_aesimc (memop addr:$src1)))]>,
  Sched<[WriteAESIMC.Folded]>;

// AES Round Key Generation Assist
let Predicates = [HasAVX, HasAES] in {
  def VAESKEYGENASSIST128rr : AESAI<0xDF, MRMSrcReg, (outs VR128:$dst),
      (ins VR128:$src1, u8imm:$src2),
      "vaeskeygenassist\t{$src2, $src1, $dst|$dst, $src1, $src2}",
      [(set VR128:$dst,
        (int_x86_aesni_aeskeygenassist VR128:$src1, imm:$src2))]>,
      Sched<[WriteAESKeyGen]>, VEX, VEX_WIG;
  def VAESKEYGENASSIST128rm : AESAI<0xDF, MRMSrcMem, (outs VR128:$dst),
      (ins i128mem:$src1, u8imm:$src2),
      "vaeskeygenassist\t{$src2, $src1, $dst|$dst, $src1, $src2}",
      [(set VR128:$dst,
        (int_x86_aesni_aeskeygenassist (load addr:$src1), imm:$src2))]>,
      Sched<[WriteAESKeyGen.Folded]>, VEX, VEX_WIG;
}
def AESKEYGENASSIST128rr : AESAI<0xDF, MRMSrcReg, (outs VR128:$dst),
  (ins VR128:$src1, u8imm:$src2),
  "aeskeygenassist\t{$src2, $src1, $dst|$dst, $src1, $src2}",
  [(set VR128:$dst,
    (int_x86_aesni_aeskeygenassist VR128:$src1, imm:$src2))]>,
  Sched<[WriteAESKeyGen]>;
def AESKEYGENASSIST128rm : AESAI<0xDF, MRMSrcMem, (outs VR128:$dst),
  (ins i128mem:$src1, u8imm:$src2),
  "aeskeygenassist\t{$src2, $src1, $dst|$dst, $src1, $src2}",
  [(set VR128:$dst,
    (int_x86_aesni_aeskeygenassist (memop addr:$src1), imm:$src2))]>,
  Sched<[WriteAESKeyGen.Folded]>;

//===----------------------------------------------------------------------===//
// PCLMUL Instructions
//===----------------------------------------------------------------------===//

// Immediate transform to help with commuting.
def PCLMULCommuteImm : SDNodeXForm<imm, [{
  uint8_t Imm = N->getZExtValue();
  return getI8Imm((uint8_t)((Imm >> 4) | (Imm << 4)), SDLoc(N));
}]>;

// SSE carry-less Multiplication instructions
let Predicates = [NoAVX, HasPCLMUL] in {
  let Constraints = "$src1 = $dst" in {
    let isCommutable = 1 in
    def PCLMULQDQrr : PCLMULIi8<0x44, MRMSrcReg, (outs VR128:$dst),
              (ins VR128:$src1, VR128:$src2, u8imm:$src3),
              "pclmulqdq\t{$src3, $src2, $dst|$dst, $src2, $src3}",
              [(set VR128:$dst,
                (int_x86_pclmulqdq VR128:$src1, VR128:$src2, imm:$src3))]>,
                Sched<[WriteCLMul]>;

    def PCLMULQDQrm : PCLMULIi8<0x44, MRMSrcMem, (outs VR128:$dst),
              (ins VR128:$src1, i128mem:$src2, u8imm:$src3),
              "pclmulqdq\t{$src3, $src2, $dst|$dst, $src2, $src3}",
              [(set VR128:$dst,
                 (int_x86_pclmulqdq VR128:$src1, (memop addr:$src2),
                  imm:$src3))]>,
              Sched<[WriteCLMul.Folded, WriteCLMul.ReadAfterFold]>;
  } // Constraints = "$src1 = $dst"

  def : Pat<(int_x86_pclmulqdq (memop addr:$src2), VR128:$src1,
                                (i8 imm:$src3)),
            (PCLMULQDQrm VR128:$src1, addr:$src2,
                          (PCLMULCommuteImm imm:$src3))>;
} // Predicates = [NoAVX, HasPCLMUL]

// SSE aliases
foreach HI = ["hq","lq"] in
foreach LO = ["hq","lq"] in {
  def : InstAlias<"pclmul" # HI # LO # "dq\t{$src, $dst|$dst, $src}",
                  (PCLMULQDQrr VR128:$dst, VR128:$src,
                   !add(!shl(!eq(LO,"hq"),4),!eq(HI,"hq"))), 0>;
  def : InstAlias<"pclmul" # HI # LO # "dq\t{$src, $dst|$dst, $src}",
                  (PCLMULQDQrm VR128:$dst, i128mem:$src,
                   !add(!shl(!eq(LO,"hq"),4),!eq(HI,"hq"))), 0>;
}

// AVX carry-less Multiplication instructions
multiclass vpclmulqdq<RegisterClass RC, X86MemOperand MemOp,
                      PatFrag LdFrag, Intrinsic IntId> {
  let isCommutable = 1 in
  def rr : PCLMULIi8<0x44, MRMSrcReg, (outs RC:$dst),
            (ins RC:$src1, RC:$src2, u8imm:$src3),
            "vpclmulqdq\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}",
            [(set RC:$dst,
              (IntId RC:$src1, RC:$src2, imm:$src3))]>,
            Sched<[WriteCLMul]>;

  def rm : PCLMULIi8<0x44, MRMSrcMem, (outs RC:$dst),
            (ins RC:$src1, MemOp:$src2, u8imm:$src3),
            "vpclmulqdq\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}",
            [(set RC:$dst,
               (IntId RC:$src1, (LdFrag addr:$src2), imm:$src3))]>,
            Sched<[WriteCLMul.Folded, WriteCLMul.ReadAfterFold]>;

  // We can commute a load in the first operand by swapping the sources and
  // rotating the immediate.
  def : Pat<(IntId (LdFrag addr:$src2), RC:$src1, (i8 imm:$src3)),
            (!cast<Instruction>(NAME#"rm") RC:$src1, addr:$src2,
                                           (PCLMULCommuteImm imm:$src3))>;
}

let Predicates = [HasAVX, NoVLX_Or_NoVPCLMULQDQ, HasPCLMUL] in
defm VPCLMULQDQ : vpclmulqdq<VR128, i128mem, load,
                             int_x86_pclmulqdq>, VEX_4V, VEX_WIG;

let Predicates = [NoVLX, HasVPCLMULQDQ] in
defm VPCLMULQDQY : vpclmulqdq<VR256, i256mem, load,
                              int_x86_pclmulqdq_256>, VEX_4V, VEX_L, VEX_WIG;

multiclass vpclmulqdq_aliases_impl<string InstStr, RegisterClass RC,
                                   X86MemOperand MemOp, string Hi, string Lo> {
  def : InstAlias<"vpclmul"##Hi##Lo##"dq\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                  (!cast<Instruction>(InstStr # "rr") RC:$dst, RC:$src1, RC:$src2,
                        !add(!shl(!eq(Lo,"hq"),4),!eq(Hi,"hq"))), 0>;
  def : InstAlias<"vpclmul"##Hi##Lo##"dq\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                  (!cast<Instruction>(InstStr # "rm") RC:$dst, RC:$src1, MemOp:$src2,
                        !add(!shl(!eq(Lo,"hq"),4),!eq(Hi,"hq"))), 0>;
}

multiclass vpclmulqdq_aliases<string InstStr, RegisterClass RC,
                              X86MemOperand MemOp> {
  defm : vpclmulqdq_aliases_impl<InstStr, RC, MemOp, "hq", "hq">;
  defm : vpclmulqdq_aliases_impl<InstStr, RC, MemOp, "hq", "lq">;
  defm : vpclmulqdq_aliases_impl<InstStr, RC, MemOp, "lq", "hq">;
  defm : vpclmulqdq_aliases_impl<InstStr, RC, MemOp, "lq", "lq">;
}

// AVX aliases
defm : vpclmulqdq_aliases<"VPCLMULQDQ", VR128, i128mem>;
defm : vpclmulqdq_aliases<"VPCLMULQDQY", VR256, i256mem>;

//===----------------------------------------------------------------------===//
// SSE4A Instructions
//===----------------------------------------------------------------------===//

let Predicates = [HasSSE4A] in {

let ExeDomain = SSEPackedInt in {
let Constraints = "$src = $dst" in {
def EXTRQI : Ii8<0x78, MRMXr, (outs VR128:$dst),
                 (ins VR128:$src, u8imm:$len, u8imm:$idx),
                 "extrq\t{$idx, $len, $src|$src, $len, $idx}",
                 [(set VR128:$dst, (X86extrqi VR128:$src, imm:$len,
                                    imm:$idx))]>,
                 PD, Sched<[SchedWriteVecALU.XMM]>;
def EXTRQ  : I<0x79, MRMSrcReg, (outs VR128:$dst),
              (ins VR128:$src, VR128:$mask),
              "extrq\t{$mask, $src|$src, $mask}",
              [(set VR128:$dst, (int_x86_sse4a_extrq VR128:$src,
                                 VR128:$mask))]>,
              PD, Sched<[SchedWriteVecALU.XMM]>;

def INSERTQI : Ii8<0x78, MRMSrcReg, (outs VR128:$dst),
                   (ins VR128:$src, VR128:$src2, u8imm:$len, u8imm:$idx),
                   "insertq\t{$idx, $len, $src2, $src|$src, $src2, $len, $idx}",
                   [(set VR128:$dst, (X86insertqi VR128:$src, VR128:$src2,
                                      imm:$len, imm:$idx))]>,
                   XD, Sched<[SchedWriteVecALU.XMM]>;
def INSERTQ  : I<0x79, MRMSrcReg, (outs VR128:$dst),
                 (ins VR128:$src, VR128:$mask),
                 "insertq\t{$mask, $src|$src, $mask}",
                 [(set VR128:$dst, (int_x86_sse4a_insertq VR128:$src,
                                    VR128:$mask))]>,
                 XD, Sched<[SchedWriteVecALU.XMM]>;
}
} // ExeDomain = SSEPackedInt

// Non-temporal (unaligned) scalar stores.
let AddedComplexity = 400 in { // Prefer non-temporal versions
let hasSideEffects = 0, mayStore = 1, SchedRW = [SchedWriteFMoveLSNT.Scl.MR] in {
def MOVNTSS : I<0x2B, MRMDestMem, (outs), (ins f32mem:$dst, VR128:$src),
                "movntss\t{$src, $dst|$dst, $src}", []>, XS;

def MOVNTSD : I<0x2B, MRMDestMem, (outs), (ins f64mem:$dst, VR128:$src),
                "movntsd\t{$src, $dst|$dst, $src}", []>, XD;
} // SchedRW

def : Pat<(nontemporalstore FR32:$src, addr:$dst),
          (MOVNTSS addr:$dst, (v4f32 (COPY_TO_REGCLASS FR32:$src, VR128)))>;

def : Pat<(nontemporalstore FR64:$src, addr:$dst),
          (MOVNTSD addr:$dst, (v2f64 (COPY_TO_REGCLASS FR64:$src, VR128)))>;

} // AddedComplexity
} // HasSSE4A

//===----------------------------------------------------------------------===//
// AVX Instructions
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// VBROADCAST - Load from memory and broadcast to all elements of the
//              destination operand
//
class avx_broadcast_rm<bits<8> opc, string OpcodeStr, RegisterClass RC,
                           X86MemOperand x86memop, ValueType VT,
                           PatFrag ld_frag, SchedWrite Sched> :
  AVX8I<opc, MRMSrcMem, (outs RC:$dst), (ins x86memop:$src),
        !strconcat(OpcodeStr, "\t{$src, $dst|$dst, $src}"),
        [(set RC:$dst, (VT (X86VBroadcast (ld_frag addr:$src))))]>,
        Sched<[Sched]>, VEX;

// AVX2 adds register forms
class avx2_broadcast_rr<bits<8> opc, string OpcodeStr, RegisterClass RC,
                        ValueType ResVT, ValueType OpVT, SchedWrite Sched> :
  AVX28I<opc, MRMSrcReg, (outs RC:$dst), (ins VR128:$src),
         !strconcat(OpcodeStr, "\t{$src, $dst|$dst, $src}"),
         [(set RC:$dst, (ResVT (X86VBroadcast (OpVT VR128:$src))))]>,
         Sched<[Sched]>, VEX;

let ExeDomain = SSEPackedSingle, Predicates = [HasAVX, NoVLX] in {
  def VBROADCASTSSrm  : avx_broadcast_rm<0x18, "vbroadcastss", VR128,
                                         f32mem, v4f32, loadf32,
                                         SchedWriteFShuffle.XMM.Folded>;
  def VBROADCASTSSYrm : avx_broadcast_rm<0x18, "vbroadcastss", VR256,
                                         f32mem, v8f32, loadf32,
                                         SchedWriteFShuffle.XMM.Folded>, VEX_L;
}
let ExeDomain = SSEPackedDouble, Predicates = [HasAVX, NoVLX] in
def VBROADCASTSDYrm  : avx_broadcast_rm<0x19, "vbroadcastsd", VR256, f64mem,
                                        v4f64, loadf64,
                                        SchedWriteFShuffle.XMM.Folded>, VEX_L;

let ExeDomain = SSEPackedSingle, Predicates = [HasAVX2, NoVLX] in {
  def VBROADCASTSSrr  : avx2_broadcast_rr<0x18, "vbroadcastss", VR128,
                                          v4f32, v4f32, SchedWriteFShuffle.XMM>;
  def VBROADCASTSSYrr : avx2_broadcast_rr<0x18, "vbroadcastss", VR256,
                                          v8f32, v4f32, WriteFShuffle256>, VEX_L;
}
let ExeDomain = SSEPackedDouble, Predicates = [HasAVX2, NoVLX] in
def VBROADCASTSDYrr  : avx2_broadcast_rr<0x19, "vbroadcastsd", VR256,
                                         v4f64, v2f64, WriteFShuffle256>, VEX_L;

let Predicates = [HasAVX, NoVLX] in {
  def : Pat<(v4f32 (X86VBroadcast (v4f32 (scalar_to_vector (loadf32 addr:$src))))),
            (VBROADCASTSSrm addr:$src)>;
  def : Pat<(v8f32 (X86VBroadcast (v4f32 (scalar_to_vector (loadf32 addr:$src))))),
            (VBROADCASTSSYrm addr:$src)>;
  def : Pat<(v4f64 (X86VBroadcast (v2f64 (scalar_to_vector (loadf64 addr:$src))))),
            (VBROADCASTSDYrm addr:$src)>;
}

//===----------------------------------------------------------------------===//
// VBROADCAST*128 - Load from memory and broadcast 128-bit vector to both
//                  halves of a 256-bit vector.
//
let mayLoad = 1, hasSideEffects = 0, Predicates = [HasAVX2] in
def VBROADCASTI128 : AVX8I<0x5A, MRMSrcMem, (outs VR256:$dst),
                           (ins i128mem:$src),
                           "vbroadcasti128\t{$src, $dst|$dst, $src}", []>,
                           Sched<[WriteShuffleLd]>, VEX, VEX_L;

let mayLoad = 1, hasSideEffects = 0, Predicates = [HasAVX],
    ExeDomain = SSEPackedSingle in
def VBROADCASTF128 : AVX8I<0x1A, MRMSrcMem, (outs VR256:$dst),
                           (ins f128mem:$src),
                           "vbroadcastf128\t{$src, $dst|$dst, $src}", []>,
                           Sched<[SchedWriteFShuffle.XMM.Folded]>, VEX, VEX_L;

let Predicates = [HasAVX2, NoVLX] in {
def : Pat<(v4i64 (X86SubVBroadcast (loadv2i64 addr:$src))),
          (VBROADCASTI128 addr:$src)>;
def : Pat<(v8i32 (X86SubVBroadcast (loadv4i32 addr:$src))),
          (VBROADCASTI128 addr:$src)>;
def : Pat<(v16i16 (X86SubVBroadcast (loadv8i16 addr:$src))),
          (VBROADCASTI128 addr:$src)>;
def : Pat<(v32i8 (X86SubVBroadcast (loadv16i8 addr:$src))),
          (VBROADCASTI128 addr:$src)>;
}

let Predicates = [HasAVX, NoVLX] in {
def : Pat<(v4f64 (X86SubVBroadcast (loadv2f64 addr:$src))),
          (VBROADCASTF128 addr:$src)>;
def : Pat<(v8f32 (X86SubVBroadcast (loadv4f32 addr:$src))),
          (VBROADCASTF128 addr:$src)>;
}

let Predicates = [HasAVX1Only] in {
def : Pat<(v4i64 (X86SubVBroadcast (loadv2i64 addr:$src))),
          (VBROADCASTF128 addr:$src)>;
def : Pat<(v8i32 (X86SubVBroadcast (loadv4i32 addr:$src))),
          (VBROADCASTF128 addr:$src)>;
def : Pat<(v16i16 (X86SubVBroadcast (loadv8i16 addr:$src))),
          (VBROADCASTF128 addr:$src)>;
def : Pat<(v32i8 (X86SubVBroadcast (loadv16i8 addr:$src))),
          (VBROADCASTF128 addr:$src)>;
}

//===----------------------------------------------------------------------===//
// VINSERTF128 - Insert packed floating-point values
//
let hasSideEffects = 0, ExeDomain = SSEPackedSingle in {
def VINSERTF128rr : AVXAIi8<0x18, MRMSrcReg, (outs VR256:$dst),
          (ins VR256:$src1, VR128:$src2, u8imm:$src3),
          "vinsertf128\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}",
          []>, Sched<[WriteFShuffle256]>, VEX_4V, VEX_L;
let mayLoad = 1 in
def VINSERTF128rm : AVXAIi8<0x18, MRMSrcMem, (outs VR256:$dst),
          (ins VR256:$src1, f128mem:$src2, u8imm:$src3),
          "vinsertf128\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}",
          []>, Sched<[WriteFShuffle256.Folded, WriteFShuffle256.ReadAfterFold]>, VEX_4V, VEX_L;
}

// To create a 256-bit all ones value, we should produce VCMPTRUEPS
// with YMM register containing zero.
// FIXME: Avoid producing vxorps to clear the fake inputs.
let Predicates = [HasAVX1Only] in {
def : Pat<(v8i32 immAllOnesV), (VCMPPSYrri (AVX_SET0), (AVX_SET0), 0xf)>;
}

multiclass vinsert_lowering<string InstrStr, ValueType From, ValueType To,
                            PatFrag memop_frag> {
  def : Pat<(vinsert128_insert:$ins (To VR256:$src1), (From VR128:$src2),
                                   (iPTR imm)),
            (!cast<Instruction>(InstrStr#rr) VR256:$src1, VR128:$src2,
                                       (INSERT_get_vinsert128_imm VR256:$ins))>;
  def : Pat<(vinsert128_insert:$ins (To VR256:$src1),
                                    (From (memop_frag addr:$src2)),
                                    (iPTR imm)),
            (!cast<Instruction>(InstrStr#rm) VR256:$src1, addr:$src2,
                                       (INSERT_get_vinsert128_imm VR256:$ins))>;
}

let Predicates = [HasAVX, NoVLX] in {
  defm : vinsert_lowering<"VINSERTF128", v4f32, v8f32, loadv4f32>;
  defm : vinsert_lowering<"VINSERTF128", v2f64, v4f64, loadv2f64>;
}

let Predicates = [HasAVX1Only] in {
  defm : vinsert_lowering<"VINSERTF128", v2i64, v4i64,  loadv2i64>;
  defm : vinsert_lowering<"VINSERTF128", v4i32, v8i32,  loadv4i32>;
  defm : vinsert_lowering<"VINSERTF128", v8i16, v16i16, loadv8i16>;
  defm : vinsert_lowering<"VINSERTF128", v16i8, v32i8,  loadv16i8>;
}

//===----------------------------------------------------------------------===//
// VEXTRACTF128 - Extract packed floating-point values
//
let hasSideEffects = 0, ExeDomain = SSEPackedSingle in {
def VEXTRACTF128rr : AVXAIi8<0x19, MRMDestReg, (outs VR128:$dst),
          (ins VR256:$src1, u8imm:$src2),
          "vextractf128\t{$src2, $src1, $dst|$dst, $src1, $src2}",
          []>, Sched<[WriteFShuffle256]>, VEX, VEX_L;
let mayStore = 1 in
def VEXTRACTF128mr : AVXAIi8<0x19, MRMDestMem, (outs),
          (ins f128mem:$dst, VR256:$src1, u8imm:$src2),
          "vextractf128\t{$src2, $src1, $dst|$dst, $src1, $src2}",
          []>, Sched<[WriteFStoreX]>, VEX, VEX_L;
}

multiclass vextract_lowering<string InstrStr, ValueType From, ValueType To> {
  def : Pat<(vextract128_extract:$ext VR256:$src1, (iPTR imm)),
            (To (!cast<Instruction>(InstrStr#rr)
                                    (From VR256:$src1),
                                    (EXTRACT_get_vextract128_imm VR128:$ext)))>;
  def : Pat<(store (To (vextract128_extract:$ext (From VR256:$src1),
                                                 (iPTR imm))), addr:$dst),
            (!cast<Instruction>(InstrStr#mr) addr:$dst, VR256:$src1,
             (EXTRACT_get_vextract128_imm VR128:$ext))>;
}

// AVX1 patterns
let Predicates = [HasAVX, NoVLX] in {
  defm : vextract_lowering<"VEXTRACTF128", v8f32, v4f32>;
  defm : vextract_lowering<"VEXTRACTF128", v4f64, v2f64>;
}

let Predicates = [HasAVX1Only] in {
  defm : vextract_lowering<"VEXTRACTF128", v4i64,  v2i64>;
  defm : vextract_lowering<"VEXTRACTF128", v8i32,  v4i32>;
  defm : vextract_lowering<"VEXTRACTF128", v16i16, v8i16>;
  defm : vextract_lowering<"VEXTRACTF128", v32i8,  v16i8>;
}

//===----------------------------------------------------------------------===//
// VMASKMOV - Conditional SIMD Packed Loads and Stores
//
multiclass avx_movmask_rm<bits<8> opc_rm, bits<8> opc_mr, string OpcodeStr,
                          Intrinsic IntLd, Intrinsic IntLd256,
                          Intrinsic IntSt, Intrinsic IntSt256> {
  def rm  : AVX8I<opc_rm, MRMSrcMem, (outs VR128:$dst),
             (ins VR128:$src1, f128mem:$src2),
             !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
             [(set VR128:$dst, (IntLd addr:$src2, VR128:$src1))]>,
             VEX_4V, Sched<[WriteFMaskedLoad]>;
  def Yrm : AVX8I<opc_rm, MRMSrcMem, (outs VR256:$dst),
             (ins VR256:$src1, f256mem:$src2),
             !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
             [(set VR256:$dst, (IntLd256 addr:$src2, VR256:$src1))]>,
             VEX_4V, VEX_L, Sched<[WriteFMaskedLoadY]>;
  def mr  : AVX8I<opc_mr, MRMDestMem, (outs),
             (ins f128mem:$dst, VR128:$src1, VR128:$src2),
             !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
             [(IntSt addr:$dst, VR128:$src1, VR128:$src2)]>,
             VEX_4V, Sched<[WriteFMaskedStore]>;
  def Ymr : AVX8I<opc_mr, MRMDestMem, (outs),
             (ins f256mem:$dst, VR256:$src1, VR256:$src2),
             !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
             [(IntSt256 addr:$dst, VR256:$src1, VR256:$src2)]>,
             VEX_4V, VEX_L, Sched<[WriteFMaskedStoreY]>;
}

let ExeDomain = SSEPackedSingle in
defm VMASKMOVPS : avx_movmask_rm<0x2C, 0x2E, "vmaskmovps",
                                 int_x86_avx_maskload_ps,
                                 int_x86_avx_maskload_ps_256,
                                 int_x86_avx_maskstore_ps,
                                 int_x86_avx_maskstore_ps_256>;
let ExeDomain = SSEPackedDouble in
defm VMASKMOVPD : avx_movmask_rm<0x2D, 0x2F, "vmaskmovpd",
                                 int_x86_avx_maskload_pd,
                                 int_x86_avx_maskload_pd_256,
                                 int_x86_avx_maskstore_pd,
                                 int_x86_avx_maskstore_pd_256>;

//===----------------------------------------------------------------------===//
// VPERMIL - Permute Single and Double Floating-Point Values
//

multiclass avx_permil<bits<8> opc_rm, bits<8> opc_rmi, string OpcodeStr,
                      RegisterClass RC, X86MemOperand x86memop_f,
                      X86MemOperand x86memop_i,
                      ValueType f_vt, ValueType i_vt,
                      X86FoldableSchedWrite sched,
                      X86FoldableSchedWrite varsched> {
  let Predicates = [HasAVX, NoVLX] in {
    def rr  : AVX8I<opc_rm, MRMSrcReg, (outs RC:$dst),
               (ins RC:$src1, RC:$src2),
               !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
               [(set RC:$dst, (f_vt (X86VPermilpv RC:$src1, (i_vt RC:$src2))))]>, VEX_4V,
               Sched<[varsched]>;
    def rm  : AVX8I<opc_rm, MRMSrcMem, (outs RC:$dst),
               (ins RC:$src1, x86memop_i:$src2),
               !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
               [(set RC:$dst, (f_vt (X86VPermilpv RC:$src1,
                              (i_vt (load addr:$src2)))))]>, VEX_4V,
               Sched<[varsched.Folded, sched.ReadAfterFold]>;

    def ri  : AVXAIi8<opc_rmi, MRMSrcReg, (outs RC:$dst),
             (ins RC:$src1, u8imm:$src2),
             !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
             [(set RC:$dst, (f_vt (X86VPermilpi RC:$src1, (i8 imm:$src2))))]>, VEX,
             Sched<[sched]>;
    def mi  : AVXAIi8<opc_rmi, MRMSrcMem, (outs RC:$dst),
             (ins x86memop_f:$src1, u8imm:$src2),
             !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
             [(set RC:$dst,
               (f_vt (X86VPermilpi (load addr:$src1), (i8 imm:$src2))))]>, VEX,
             Sched<[sched.Folded]>;
  }// Predicates = [HasAVX, NoVLX]
}

let ExeDomain = SSEPackedSingle in {
  defm VPERMILPS  : avx_permil<0x0C, 0x04, "vpermilps", VR128, f128mem, i128mem,
                               v4f32, v4i32, SchedWriteFShuffle.XMM,
                               SchedWriteFVarShuffle.XMM>;
  defm VPERMILPSY : avx_permil<0x0C, 0x04, "vpermilps", VR256, f256mem, i256mem,
                               v8f32, v8i32, SchedWriteFShuffle.YMM,
                               SchedWriteFVarShuffle.YMM>, VEX_L;
}
let ExeDomain = SSEPackedDouble in {
  defm VPERMILPD  : avx_permil<0x0D, 0x05, "vpermilpd", VR128, f128mem, i128mem,
                               v2f64, v2i64, SchedWriteFShuffle.XMM,
                               SchedWriteFVarShuffle.XMM>;
  defm VPERMILPDY : avx_permil<0x0D, 0x05, "vpermilpd", VR256, f256mem, i256mem,
                               v4f64, v4i64, SchedWriteFShuffle.YMM,
                               SchedWriteFVarShuffle.YMM>, VEX_L;
}

//===----------------------------------------------------------------------===//
// VPERM2F128 - Permute Floating-Point Values in 128-bit chunks
//

let ExeDomain = SSEPackedSingle in {
let isCommutable = 1 in
def VPERM2F128rr : AVXAIi8<0x06, MRMSrcReg, (outs VR256:$dst),
          (ins VR256:$src1, VR256:$src2, u8imm:$src3),
          "vperm2f128\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}",
          [(set VR256:$dst, (v4f64 (X86VPerm2x128 VR256:$src1, VR256:$src2,
                              (i8 imm:$src3))))]>, VEX_4V, VEX_L,
          Sched<[WriteFShuffle256]>;
def VPERM2F128rm : AVXAIi8<0x06, MRMSrcMem, (outs VR256:$dst),
          (ins VR256:$src1, f256mem:$src2, u8imm:$src3),
          "vperm2f128\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}",
          [(set VR256:$dst, (X86VPerm2x128 VR256:$src1, (loadv4f64 addr:$src2),
                             (i8 imm:$src3)))]>, VEX_4V, VEX_L,
          Sched<[WriteFShuffle256.Folded, WriteFShuffle256.ReadAfterFold]>;
}

// Immediate transform to help with commuting.
def Perm2XCommuteImm : SDNodeXForm<imm, [{
  return getI8Imm(N->getZExtValue() ^ 0x22, SDLoc(N));
}]>;

let Predicates = [HasAVX] in {
// Pattern with load in other operand.
def : Pat<(v4f64 (X86VPerm2x128 (loadv4f64 addr:$src2),
                                VR256:$src1, (i8 imm:$imm))),
          (VPERM2F128rm VR256:$src1, addr:$src2, (Perm2XCommuteImm imm:$imm))>;
}

let Predicates = [HasAVX1Only] in {
def : Pat<(v4i64 (X86VPerm2x128 VR256:$src1, VR256:$src2, (i8 imm:$imm))),
          (VPERM2F128rr VR256:$src1, VR256:$src2, imm:$imm)>;
def : Pat<(v4i64 (X86VPerm2x128 VR256:$src1,
                  (loadv4i64 addr:$src2), (i8 imm:$imm))),
          (VPERM2F128rm VR256:$src1, addr:$src2, imm:$imm)>;
// Pattern with load in other operand.
def : Pat<(v4i64 (X86VPerm2x128 (loadv4i64 addr:$src2),
                                VR256:$src1, (i8 imm:$imm))),
          (VPERM2F128rm VR256:$src1, addr:$src2, (Perm2XCommuteImm imm:$imm))>;
}

//===----------------------------------------------------------------------===//
// VZERO - Zero YMM registers
// Note: These instruction do not affect the YMM16-YMM31.
//

let SchedRW = [WriteSystem] in {
let Defs = [YMM0, YMM1, YMM2, YMM3, YMM4, YMM5, YMM6, YMM7,
            YMM8, YMM9, YMM10, YMM11, YMM12, YMM13, YMM14, YMM15] in {
  // Zero All YMM registers
  def VZEROALL : I<0x77, RawFrm, (outs), (ins), "vzeroall",
                  [(int_x86_avx_vzeroall)]>, PS, VEX, VEX_L,
                  Requires<[HasAVX]>, VEX_WIG;

  // Zero Upper bits of YMM registers
  def VZEROUPPER : I<0x77, RawFrm, (outs), (ins), "vzeroupper",
                     [(int_x86_avx_vzeroupper)]>, PS, VEX,
                     Requires<[HasAVX]>, VEX_WIG;
} // Defs
} // SchedRW

//===----------------------------------------------------------------------===//
// Half precision conversion instructions
//

multiclass f16c_ph2ps<RegisterClass RC, X86MemOperand x86memop,
                      X86FoldableSchedWrite sched> {
  def rr : I<0x13, MRMSrcReg, (outs RC:$dst), (ins VR128:$src),
             "vcvtph2ps\t{$src, $dst|$dst, $src}",
             [(set RC:$dst, (X86cvtph2ps VR128:$src))]>,
             T8PD, VEX, Sched<[sched]>;
  let hasSideEffects = 0, mayLoad = 1 in
  def rm : I<0x13, MRMSrcMem, (outs RC:$dst), (ins x86memop:$src),
             "vcvtph2ps\t{$src, $dst|$dst, $src}",
             [(set RC:$dst, (X86cvtph2ps (loadv8i16 addr:$src)))]>,
             T8PD, VEX, Sched<[sched.Folded]>;
}

multiclass f16c_ps2ph<RegisterClass RC, X86MemOperand x86memop,
                      SchedWrite RR, SchedWrite MR> {
  def rr : Ii8<0x1D, MRMDestReg, (outs VR128:$dst),
               (ins RC:$src1, i32u8imm:$src2),
               "vcvtps2ph\t{$src2, $src1, $dst|$dst, $src1, $src2}",
               [(set VR128:$dst, (X86cvtps2ph RC:$src1, imm:$src2))]>,
               TAPD, VEX, Sched<[RR]>;
  let hasSideEffects = 0, mayStore = 1 in
  def mr : Ii8<0x1D, MRMDestMem, (outs),
               (ins x86memop:$dst, RC:$src1, i32u8imm:$src2),
               "vcvtps2ph\t{$src2, $src1, $dst|$dst, $src1, $src2}", []>,
               TAPD, VEX, Sched<[MR]>;
}

let Predicates = [HasF16C, NoVLX] in {
  defm VCVTPH2PS  : f16c_ph2ps<VR128, f64mem, WriteCvtPH2PS>;
  defm VCVTPH2PSY : f16c_ph2ps<VR256, f128mem, WriteCvtPH2PSY>, VEX_L;
  defm VCVTPS2PH  : f16c_ps2ph<VR128, f64mem, WriteCvtPS2PH,
                               WriteCvtPS2PHSt>;
  defm VCVTPS2PHY : f16c_ps2ph<VR256, f128mem, WriteCvtPS2PHY,
                               WriteCvtPS2PHYSt>, VEX_L;

  // Pattern match vcvtph2ps of a scalar i64 load.
  def : Pat<(v4f32 (X86cvtph2ps (v8i16 (vzmovl_v2i64 addr:$src)))),
            (VCVTPH2PSrm addr:$src)>;
  def : Pat<(v4f32 (X86cvtph2ps (v8i16 (vzload_v2i64 addr:$src)))),
            (VCVTPH2PSrm addr:$src)>;
  def : Pat<(v4f32 (X86cvtph2ps (v8i16 (bitconvert
              (v2i64 (scalar_to_vector (loadi64 addr:$src))))))),
            (VCVTPH2PSrm addr:$src)>;

  def : Pat<(store (f64 (extractelt
                         (bc_v2f64 (v8i16 (X86cvtps2ph VR128:$src1, i32:$src2))),
                         (iPTR 0))), addr:$dst),
            (VCVTPS2PHmr addr:$dst, VR128:$src1, imm:$src2)>;
  def : Pat<(store (i64 (extractelt
                         (bc_v2i64 (v8i16 (X86cvtps2ph VR128:$src1, i32:$src2))),
                         (iPTR 0))), addr:$dst),
            (VCVTPS2PHmr addr:$dst, VR128:$src1, imm:$src2)>;
  def : Pat<(store (v8i16 (X86cvtps2ph VR256:$src1, i32:$src2)), addr:$dst),
            (VCVTPS2PHYmr addr:$dst, VR256:$src1, imm:$src2)>;
}

// Patterns for  matching conversions from float to half-float and vice versa.
let Predicates = [HasF16C, NoVLX] in {
  // Use MXCSR.RC for rounding instead of explicitly specifying the default
  // rounding mode (Nearest-Even, encoded as 0). Both are equivalent in the
  // configurations we support (the default). However, falling back to MXCSR is
  // more consistent with other instructions, which are always controlled by it.
  // It's encoded as 0b100.
  def : Pat<(fp_to_f16 FR32:$src),
            (i16 (EXTRACT_SUBREG (VMOVPDI2DIrr (v8i16 (VCVTPS2PHrr
              (v4f32 (COPY_TO_REGCLASS FR32:$src, VR128)), 4))), sub_16bit))>;

  def : Pat<(f16_to_fp GR16:$src),
            (f32 (COPY_TO_REGCLASS (v4f32 (VCVTPH2PSrr
              (v4i32 (COPY_TO_REGCLASS (MOVSX32rr16 GR16:$src), VR128)))), FR32)) >;

  def : Pat<(f16_to_fp (i16 (fp_to_f16 FR32:$src))),
            (f32 (COPY_TO_REGCLASS (v4f32 (VCVTPH2PSrr
             (v8i16 (VCVTPS2PHrr (v4f32 (COPY_TO_REGCLASS FR32:$src, VR128)), 4)))), FR32)) >;
}

//===----------------------------------------------------------------------===//
// AVX2 Instructions
//===----------------------------------------------------------------------===//

/// AVX2_blend_rmi - AVX2 blend with 8-bit immediate
multiclass AVX2_blend_rmi<bits<8> opc, string OpcodeStr, SDNode OpNode,
                          ValueType OpVT, X86FoldableSchedWrite sched,
                          RegisterClass RC,
                          X86MemOperand x86memop, SDNodeXForm commuteXForm> {
  let isCommutable = 1 in
  def rri : AVX2AIi8<opc, MRMSrcReg, (outs RC:$dst),
        (ins RC:$src1, RC:$src2, u8imm:$src3),
        !strconcat(OpcodeStr,
            "\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}"),
        [(set RC:$dst, (OpVT (OpNode RC:$src1, RC:$src2, imm:$src3)))]>,
        Sched<[sched]>, VEX_4V;
  def rmi : AVX2AIi8<opc, MRMSrcMem, (outs RC:$dst),
        (ins RC:$src1, x86memop:$src2, u8imm:$src3),
        !strconcat(OpcodeStr,
            "\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}"),
        [(set RC:$dst,
          (OpVT (OpNode RC:$src1, (load addr:$src2), imm:$src3)))]>,
        Sched<[sched.Folded, sched.ReadAfterFold]>, VEX_4V;

  // Pattern to commute if load is in first source.
  def : Pat<(OpVT (OpNode (load addr:$src2), RC:$src1, imm:$src3)),
            (!cast<Instruction>(NAME#"rmi") RC:$src1, addr:$src2,
                                            (commuteXForm imm:$src3))>;
}

defm VPBLENDD : AVX2_blend_rmi<0x02, "vpblendd", X86Blendi, v4i32,
                               SchedWriteBlend.XMM, VR128, i128mem,
                               BlendCommuteImm4>;
defm VPBLENDDY : AVX2_blend_rmi<0x02, "vpblendd", X86Blendi, v8i32,
                                SchedWriteBlend.YMM, VR256, i256mem,
                                BlendCommuteImm8>, VEX_L;

// For insertion into the zero index (low half) of a 256-bit vector, it is
// more efficient to generate a blend with immediate instead of an insert*128.
let Predicates = [HasAVX2] in {
def : Pat<(insert_subvector (v8i32 VR256:$src1), (v4i32 VR128:$src2), (iPTR 0)),
          (VPBLENDDYrri VR256:$src1,
                        (INSERT_SUBREG (v8i32 (IMPLICIT_DEF)),
                                       VR128:$src2, sub_xmm), 0xf)>;
def : Pat<(insert_subvector (v4i64 VR256:$src1), (v2i64 VR128:$src2), (iPTR 0)),
          (VPBLENDDYrri VR256:$src1,
                        (INSERT_SUBREG (v8i32 (IMPLICIT_DEF)),
                                       VR128:$src2, sub_xmm), 0xf)>;
def : Pat<(insert_subvector (v16i16 VR256:$src1), (v8i16 VR128:$src2), (iPTR 0)),
          (VPBLENDDYrri VR256:$src1,
                        (INSERT_SUBREG (v8i32 (IMPLICIT_DEF)),
                                       VR128:$src2, sub_xmm), 0xf)>;
def : Pat<(insert_subvector (v32i8 VR256:$src1), (v16i8 VR128:$src2), (iPTR 0)),
          (VPBLENDDYrri VR256:$src1,
                        (INSERT_SUBREG (v8i32 (IMPLICIT_DEF)),
                                       VR128:$src2, sub_xmm), 0xf)>;
}

let Predicates = [HasAVX1Only] in {
def : Pat<(insert_subvector (v8i32 VR256:$src1), (v4i32 VR128:$src2), (iPTR 0)),
          (VBLENDPSYrri VR256:$src1,
                        (INSERT_SUBREG (v8i32 (IMPLICIT_DEF)),
                                       VR128:$src2, sub_xmm), 0xf)>;
def : Pat<(insert_subvector (v4i64 VR256:$src1), (v2i64 VR128:$src2), (iPTR 0)),
          (VBLENDPSYrri VR256:$src1,
                        (INSERT_SUBREG (v8i32 (IMPLICIT_DEF)),
                                       VR128:$src2, sub_xmm), 0xf)>;
def : Pat<(insert_subvector (v16i16 VR256:$src1), (v8i16 VR128:$src2), (iPTR 0)),
          (VBLENDPSYrri VR256:$src1,
                        (INSERT_SUBREG (v8i32 (IMPLICIT_DEF)),
                                       VR128:$src2, sub_xmm), 0xf)>;
def : Pat<(insert_subvector (v32i8 VR256:$src1), (v16i8 VR128:$src2), (iPTR 0)),
          (VBLENDPSYrri VR256:$src1,
                        (INSERT_SUBREG (v8i32 (IMPLICIT_DEF)),
                                       VR128:$src2, sub_xmm), 0xf)>;
}

//===----------------------------------------------------------------------===//
// VPBROADCAST - Load from memory and broadcast to all elements of the
//               destination operand
//
multiclass avx2_broadcast<bits<8> opc, string OpcodeStr,
                          X86MemOperand x86memop, PatFrag ld_frag,
                          ValueType OpVT128, ValueType OpVT256, Predicate prd> {
  let Predicates = [HasAVX2, prd] in {
    def rr : AVX28I<opc, MRMSrcReg, (outs VR128:$dst), (ins VR128:$src),
                  !strconcat(OpcodeStr, "\t{$src, $dst|$dst, $src}"),
                  [(set VR128:$dst,
                   (OpVT128 (X86VBroadcast (OpVT128 VR128:$src))))]>,
                  Sched<[SchedWriteShuffle.XMM]>, VEX;
    def rm : AVX28I<opc, MRMSrcMem, (outs VR128:$dst), (ins x86memop:$src),
                  !strconcat(OpcodeStr, "\t{$src, $dst|$dst, $src}"),
                  [(set VR128:$dst,
                   (OpVT128 (X86VBroadcast (ld_frag addr:$src))))]>,
                  Sched<[SchedWriteShuffle.XMM.Folded]>, VEX;
    def Yrr : AVX28I<opc, MRMSrcReg, (outs VR256:$dst), (ins VR128:$src),
                   !strconcat(OpcodeStr, "\t{$src, $dst|$dst, $src}"),
                   [(set VR256:$dst,
                    (OpVT256 (X86VBroadcast (OpVT128 VR128:$src))))]>,
                   Sched<[WriteShuffle256]>, VEX, VEX_L;
    def Yrm : AVX28I<opc, MRMSrcMem, (outs VR256:$dst), (ins x86memop:$src),
                   !strconcat(OpcodeStr, "\t{$src, $dst|$dst, $src}"),
                   [(set VR256:$dst,
                    (OpVT256 (X86VBroadcast (ld_frag addr:$src))))]>,
                   Sched<[SchedWriteShuffle.XMM.Folded]>, VEX, VEX_L;

    // Provide aliases for broadcast from the same register class that
    // automatically does the extract.
    def : Pat<(OpVT256 (X86VBroadcast (OpVT256 VR256:$src))),
              (!cast<Instruction>(NAME#"Yrr")
                  (OpVT128 (EXTRACT_SUBREG (OpVT256 VR256:$src),sub_xmm)))>;
  }
}

defm VPBROADCASTB  : avx2_broadcast<0x78, "vpbroadcastb", i8mem, loadi8,
                                    v16i8, v32i8, NoVLX_Or_NoBWI>;
defm VPBROADCASTW  : avx2_broadcast<0x79, "vpbroadcastw", i16mem, loadi16,
                                    v8i16, v16i16, NoVLX_Or_NoBWI>;
defm VPBROADCASTD  : avx2_broadcast<0x58, "vpbroadcastd", i32mem, loadi32,
                                    v4i32, v8i32, NoVLX>;
defm VPBROADCASTQ  : avx2_broadcast<0x59, "vpbroadcastq", i64mem, loadi64,
                                    v2i64, v4i64, NoVLX>;

let Predicates = [HasAVX2, NoVLX] in {
  // 32-bit targets will fail to load a i64 directly but can use ZEXT_LOAD.
  def : Pat<(v2i64 (X86VBroadcast (v2i64 (X86vzload addr:$src)))),
            (VPBROADCASTQrm addr:$src)>;
  def : Pat<(v4i64 (X86VBroadcast (v4i64 (X86vzload addr:$src)))),
            (VPBROADCASTQYrm addr:$src)>;

  def : Pat<(v4i32 (X86VBroadcast (v4i32 (scalar_to_vector (loadi32 addr:$src))))),
            (VPBROADCASTDrm addr:$src)>;
  def : Pat<(v8i32 (X86VBroadcast (v4i32 (scalar_to_vector (loadi32 addr:$src))))),
            (VPBROADCASTDYrm addr:$src)>;
  def : Pat<(v2i64 (X86VBroadcast (v2i64 (scalar_to_vector (loadi64 addr:$src))))),
            (VPBROADCASTQrm addr:$src)>;
  def : Pat<(v4i64 (X86VBroadcast (v2i64 (scalar_to_vector (loadi64 addr:$src))))),
            (VPBROADCASTQYrm addr:$src)>;
}
let Predicates = [HasAVX2, NoVLX_Or_NoBWI] in {
  // loadi16 is tricky to fold, because !isTypeDesirableForOp, justifiably.
  // This means we'll encounter truncated i32 loads; match that here.
  def : Pat<(v8i16 (X86VBroadcast (i16 (trunc (i32 (load addr:$src)))))),
            (VPBROADCASTWrm addr:$src)>;
  def : Pat<(v16i16 (X86VBroadcast (i16 (trunc (i32 (load addr:$src)))))),
            (VPBROADCASTWYrm addr:$src)>;
  def : Pat<(v8i16 (X86VBroadcast
              (i16 (trunc (i32 (zextloadi16 addr:$src)))))),
            (VPBROADCASTWrm addr:$src)>;
  def : Pat<(v16i16 (X86VBroadcast
              (i16 (trunc (i32 (zextloadi16 addr:$src)))))),
            (VPBROADCASTWYrm addr:$src)>;
}

let Predicates = [HasAVX2, NoVLX] in {
  // Provide aliases for broadcast from the same register class that
  // automatically does the extract.
  def : Pat<(v8f32 (X86VBroadcast (v8f32 VR256:$src))),
            (VBROADCASTSSYrr (v4f32 (EXTRACT_SUBREG (v8f32 VR256:$src),
                                                    sub_xmm)))>;
  def : Pat<(v4f64 (X86VBroadcast (v4f64 VR256:$src))),
            (VBROADCASTSDYrr (v2f64 (EXTRACT_SUBREG (v4f64 VR256:$src),
                                                    sub_xmm)))>;
}

let Predicates = [HasAVX2, NoVLX] in {
  // Provide fallback in case the load node that is used in the patterns above
  // is used by additional users, which prevents the pattern selection.
    def : Pat<(v4f32 (X86VBroadcast FR32:$src)),
              (VBROADCASTSSrr (v4f32 (COPY_TO_REGCLASS FR32:$src, VR128)))>;
    def : Pat<(v8f32 (X86VBroadcast FR32:$src)),
              (VBROADCASTSSYrr (v4f32 (COPY_TO_REGCLASS FR32:$src, VR128)))>;
    def : Pat<(v4f64 (X86VBroadcast FR64:$src)),
              (VBROADCASTSDYrr (v2f64 (COPY_TO_REGCLASS FR64:$src, VR128)))>;
}

let Predicates = [HasAVX2, NoVLX_Or_NoBWI] in {
  def : Pat<(v16i8 (X86VBroadcast GR8:$src)),
        (VPBROADCASTBrr (v16i8 (COPY_TO_REGCLASS
                         (i32 (INSERT_SUBREG (i32 (IMPLICIT_DEF)),
                                             GR8:$src, sub_8bit)),
                         VR128)))>;
  def : Pat<(v32i8 (X86VBroadcast GR8:$src)),
        (VPBROADCASTBYrr (v16i8 (COPY_TO_REGCLASS
                          (i32 (INSERT_SUBREG (i32 (IMPLICIT_DEF)),
                                              GR8:$src, sub_8bit)),
                          VR128)))>;

  def : Pat<(v8i16 (X86VBroadcast GR16:$src)),
        (VPBROADCASTWrr (v8i16 (COPY_TO_REGCLASS
                         (i32 (INSERT_SUBREG (i32 (IMPLICIT_DEF)),
                                             GR16:$src, sub_16bit)),
                         VR128)))>;
  def : Pat<(v16i16 (X86VBroadcast GR16:$src)),
        (VPBROADCASTWYrr (v8i16 (COPY_TO_REGCLASS
                          (i32 (INSERT_SUBREG (i32 (IMPLICIT_DEF)),
                                              GR16:$src, sub_16bit)),
                          VR128)))>;
}
let Predicates = [HasAVX2, NoVLX] in {
  def : Pat<(v4i32 (X86VBroadcast GR32:$src)),
            (VPBROADCASTDrr (v4i32 (COPY_TO_REGCLASS GR32:$src, VR128)))>;
  def : Pat<(v8i32 (X86VBroadcast GR32:$src)),
            (VPBROADCASTDYrr (v4i32 (COPY_TO_REGCLASS GR32:$src, VR128)))>;
  def : Pat<(v2i64 (X86VBroadcast GR64:$src)),
            (VPBROADCASTQrr (v2i64 (COPY_TO_REGCLASS GR64:$src, VR128)))>;
  def : Pat<(v4i64 (X86VBroadcast GR64:$src)),
            (VPBROADCASTQYrr (v2i64 (COPY_TO_REGCLASS GR64:$src, VR128)))>;
}

// AVX1 broadcast patterns
let Predicates = [HasAVX1Only] in {
def : Pat<(v8i32 (X86VBroadcast (loadi32 addr:$src))),
          (VBROADCASTSSYrm addr:$src)>;
def : Pat<(v4i64 (X86VBroadcast (loadi64 addr:$src))),
          (VBROADCASTSDYrm addr:$src)>;
def : Pat<(v4i32 (X86VBroadcast (loadi32 addr:$src))),
          (VBROADCASTSSrm addr:$src)>;
}

  // Provide fallback in case the load node that is used in the patterns above
  // is used by additional users, which prevents the pattern selection.
let Predicates = [HasAVX, NoVLX] in {
  // 128bit broadcasts:
  def : Pat<(v2f64 (X86VBroadcast f64:$src)),
            (VMOVDDUPrr (v2f64 (COPY_TO_REGCLASS FR64:$src, VR128)))>;
  def : Pat<(v2f64 (X86VBroadcast (loadf64 addr:$src))),
            (VMOVDDUPrm addr:$src)>;

  def : Pat<(v2f64 (X86VBroadcast v2f64:$src)),
            (VMOVDDUPrr VR128:$src)>;
  def : Pat<(v2f64 (X86VBroadcast (loadv2f64 addr:$src))),
            (VMOVDDUPrm addr:$src)>;
  def : Pat<(v2f64 (X86VBroadcast (v2f64 (X86vzload addr:$src)))),
            (VMOVDDUPrm addr:$src)>;
}

let Predicates = [HasAVX1Only] in {
  def : Pat<(v4f32 (X86VBroadcast FR32:$src)),
            (VPERMILPSri (v4f32 (COPY_TO_REGCLASS FR32:$src, VR128)), 0)>;
  def : Pat<(v8f32 (X86VBroadcast FR32:$src)),
            (VINSERTF128rr (INSERT_SUBREG (v8f32 (IMPLICIT_DEF)),
              (v4f32 (VPERMILPSri (v4f32 (COPY_TO_REGCLASS FR32:$src, VR128)), 0)), sub_xmm),
              (v4f32 (VPERMILPSri (v4f32 (COPY_TO_REGCLASS FR32:$src, VR128)), 0)), 1)>;
  def : Pat<(v4f64 (X86VBroadcast FR64:$src)),
            (VINSERTF128rr (INSERT_SUBREG (v4f64 (IMPLICIT_DEF)),
              (v2f64 (VMOVDDUPrr (v2f64 (COPY_TO_REGCLASS FR64:$src, VR128)))), sub_xmm),
              (v2f64 (VMOVDDUPrr (v2f64 (COPY_TO_REGCLASS FR64:$src, VR128)))), 1)>;

  def : Pat<(v4i32 (X86VBroadcast GR32:$src)),
            (VPSHUFDri (v4i32 (COPY_TO_REGCLASS GR32:$src, VR128)), 0)>;
  def : Pat<(v8i32 (X86VBroadcast GR32:$src)),
            (VINSERTF128rr (INSERT_SUBREG (v8i32 (IMPLICIT_DEF)),
              (v4i32 (VPSHUFDri (v4i32 (COPY_TO_REGCLASS GR32:$src, VR128)), 0)), sub_xmm),
              (v4i32 (VPSHUFDri (v4i32 (COPY_TO_REGCLASS GR32:$src, VR128)), 0)), 1)>;
  def : Pat<(v4i64 (X86VBroadcast GR64:$src)),
            (VINSERTF128rr (INSERT_SUBREG (v4i64 (IMPLICIT_DEF)),
              (v4i32 (VPSHUFDri (v4i32 (COPY_TO_REGCLASS GR64:$src, VR128)), 0x44)), sub_xmm),
              (v4i32 (VPSHUFDri (v4i32 (COPY_TO_REGCLASS GR64:$src, VR128)), 0x44)), 1)>;

  def : Pat<(v2i64 (X86VBroadcast i64:$src)),
            (VPSHUFDri (v4i32 (COPY_TO_REGCLASS GR64:$src, VR128)), 0x44)>;
  def : Pat<(v2i64 (X86VBroadcast (loadi64 addr:$src))),
            (VMOVDDUPrm addr:$src)>;
}

//===----------------------------------------------------------------------===//
// VPERM - Permute instructions
//

multiclass avx2_perm<bits<8> opc, string OpcodeStr,
                     ValueType OpVT, X86FoldableSchedWrite Sched,
                     X86MemOperand memOp> {
  let Predicates = [HasAVX2, NoVLX] in {
    def Yrr : AVX28I<opc, MRMSrcReg, (outs VR256:$dst),
                     (ins VR256:$src1, VR256:$src2),
                     !strconcat(OpcodeStr,
                         "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
                     [(set VR256:$dst,
                       (OpVT (X86VPermv VR256:$src1, VR256:$src2)))]>,
                     Sched<[Sched]>, VEX_4V, VEX_L;
    def Yrm : AVX28I<opc, MRMSrcMem, (outs VR256:$dst),
                     (ins VR256:$src1, memOp:$src2),
                     !strconcat(OpcodeStr,
                         "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
                     [(set VR256:$dst,
                       (OpVT (X86VPermv VR256:$src1,
                              (load addr:$src2))))]>,
                     Sched<[Sched.Folded, Sched.ReadAfterFold]>, VEX_4V, VEX_L;
  }
}

defm VPERMD : avx2_perm<0x36, "vpermd", v8i32, WriteVarShuffle256, i256mem>;
let ExeDomain = SSEPackedSingle in
defm VPERMPS : avx2_perm<0x16, "vpermps", v8f32, WriteFVarShuffle256, f256mem>;

multiclass avx2_perm_imm<bits<8> opc, string OpcodeStr, PatFrag mem_frag,
                         ValueType OpVT, X86FoldableSchedWrite Sched,
                         X86MemOperand memOp> {
  let Predicates = [HasAVX2, NoVLX] in {
    def Yri : AVX2AIi8<opc, MRMSrcReg, (outs VR256:$dst),
                       (ins VR256:$src1, u8imm:$src2),
                       !strconcat(OpcodeStr,
                           "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
                       [(set VR256:$dst,
                         (OpVT (X86VPermi VR256:$src1, (i8 imm:$src2))))]>,
                       Sched<[Sched]>, VEX, VEX_L;
    def Ymi : AVX2AIi8<opc, MRMSrcMem, (outs VR256:$dst),
                       (ins memOp:$src1, u8imm:$src2),
                       !strconcat(OpcodeStr,
                           "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
                       [(set VR256:$dst,
                         (OpVT (X86VPermi (mem_frag addr:$src1),
                                (i8 imm:$src2))))]>,
                       Sched<[Sched.Folded, Sched.ReadAfterFold]>, VEX, VEX_L;
  }
}

defm VPERMQ : avx2_perm_imm<0x00, "vpermq", loadv4i64, v4i64,
                            WriteShuffle256, i256mem>, VEX_W;
let ExeDomain = SSEPackedDouble in
defm VPERMPD : avx2_perm_imm<0x01, "vpermpd", loadv4f64, v4f64,
                             WriteFShuffle256, f256mem>, VEX_W;

//===----------------------------------------------------------------------===//
// VPERM2I128 - Permute Floating-Point Values in 128-bit chunks
//
let isCommutable = 1 in
def VPERM2I128rr : AVX2AIi8<0x46, MRMSrcReg, (outs VR256:$dst),
          (ins VR256:$src1, VR256:$src2, u8imm:$src3),
          "vperm2i128\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}",
          [(set VR256:$dst, (v4i64 (X86VPerm2x128 VR256:$src1, VR256:$src2,
                            (i8 imm:$src3))))]>, Sched<[WriteShuffle256]>,
          VEX_4V, VEX_L;
def VPERM2I128rm : AVX2AIi8<0x46, MRMSrcMem, (outs VR256:$dst),
          (ins VR256:$src1, f256mem:$src2, u8imm:$src3),
          "vperm2i128\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}",
          [(set VR256:$dst, (X86VPerm2x128 VR256:$src1, (loadv4i64 addr:$src2),
                             (i8 imm:$src3)))]>,
          Sched<[WriteShuffle256.Folded, WriteShuffle256.ReadAfterFold]>, VEX_4V, VEX_L;

let Predicates = [HasAVX2] in
def : Pat<(v4i64 (X86VPerm2x128 (loadv4i64 addr:$src2),
                                VR256:$src1, (i8 imm:$imm))),
          (VPERM2I128rm VR256:$src1, addr:$src2, (Perm2XCommuteImm imm:$imm))>;


//===----------------------------------------------------------------------===//
// VINSERTI128 - Insert packed integer values
//
let hasSideEffects = 0 in {
def VINSERTI128rr : AVX2AIi8<0x38, MRMSrcReg, (outs VR256:$dst),
          (ins VR256:$src1, VR128:$src2, u8imm:$src3),
          "vinserti128\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}",
          []>, Sched<[WriteShuffle256]>, VEX_4V, VEX_L;
let mayLoad = 1 in
def VINSERTI128rm : AVX2AIi8<0x38, MRMSrcMem, (outs VR256:$dst),
          (ins VR256:$src1, i128mem:$src2, u8imm:$src3),
          "vinserti128\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}",
          []>, Sched<[WriteShuffle256.Folded, WriteShuffle256.ReadAfterFold]>, VEX_4V, VEX_L;
}

let Predicates = [HasAVX2, NoVLX] in {
  defm : vinsert_lowering<"VINSERTI128", v2i64, v4i64,  loadv2i64>;
  defm : vinsert_lowering<"VINSERTI128", v4i32, v8i32,  loadv4i32>;
  defm : vinsert_lowering<"VINSERTI128", v8i16, v16i16, loadv8i16>;
  defm : vinsert_lowering<"VINSERTI128", v16i8, v32i8,  loadv16i8>;
}

//===----------------------------------------------------------------------===//
// VEXTRACTI128 - Extract packed integer values
//
def VEXTRACTI128rr : AVX2AIi8<0x39, MRMDestReg, (outs VR128:$dst),
          (ins VR256:$src1, u8imm:$src2),
          "vextracti128\t{$src2, $src1, $dst|$dst, $src1, $src2}", []>,
          Sched<[WriteShuffle256]>, VEX, VEX_L;
let hasSideEffects = 0, mayStore = 1 in
def VEXTRACTI128mr : AVX2AIi8<0x39, MRMDestMem, (outs),
          (ins i128mem:$dst, VR256:$src1, u8imm:$src2),
          "vextracti128\t{$src2, $src1, $dst|$dst, $src1, $src2}", []>,
          Sched<[SchedWriteVecMoveLS.XMM.MR]>, VEX, VEX_L;

let Predicates = [HasAVX2, NoVLX] in {
  defm : vextract_lowering<"VEXTRACTI128", v4i64,  v2i64>;
  defm : vextract_lowering<"VEXTRACTI128", v8i32,  v4i32>;
  defm : vextract_lowering<"VEXTRACTI128", v16i16, v8i16>;
  defm : vextract_lowering<"VEXTRACTI128", v32i8,  v16i8>;
}

//===----------------------------------------------------------------------===//
// VPMASKMOV - Conditional SIMD Integer Packed Loads and Stores
//
multiclass avx2_pmovmask<string OpcodeStr,
                         Intrinsic IntLd128, Intrinsic IntLd256,
                         Intrinsic IntSt128, Intrinsic IntSt256> {
  def rm  : AVX28I<0x8c, MRMSrcMem, (outs VR128:$dst),
             (ins VR128:$src1, i128mem:$src2),
             !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
             [(set VR128:$dst, (IntLd128 addr:$src2, VR128:$src1))]>,
             VEX_4V, Sched<[WriteVecMaskedLoad]>;
  def Yrm : AVX28I<0x8c, MRMSrcMem, (outs VR256:$dst),
             (ins VR256:$src1, i256mem:$src2),
             !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
             [(set VR256:$dst, (IntLd256 addr:$src2, VR256:$src1))]>,
             VEX_4V, VEX_L, Sched<[WriteVecMaskedLoadY]>;
  def mr  : AVX28I<0x8e, MRMDestMem, (outs),
             (ins i128mem:$dst, VR128:$src1, VR128:$src2),
             !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
             [(IntSt128 addr:$dst, VR128:$src1, VR128:$src2)]>,
             VEX_4V, Sched<[WriteVecMaskedStore]>;
  def Ymr : AVX28I<0x8e, MRMDestMem, (outs),
             (ins i256mem:$dst, VR256:$src1, VR256:$src2),
             !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
             [(IntSt256 addr:$dst, VR256:$src1, VR256:$src2)]>,
             VEX_4V, VEX_L, Sched<[WriteVecMaskedStoreY]>;
}

defm VPMASKMOVD : avx2_pmovmask<"vpmaskmovd",
                                int_x86_avx2_maskload_d,
                                int_x86_avx2_maskload_d_256,
                                int_x86_avx2_maskstore_d,
                                int_x86_avx2_maskstore_d_256>;
defm VPMASKMOVQ : avx2_pmovmask<"vpmaskmovq",
                                int_x86_avx2_maskload_q,
                                int_x86_avx2_maskload_q_256,
                                int_x86_avx2_maskstore_q,
                                int_x86_avx2_maskstore_q_256>, VEX_W;

multiclass maskmov_lowering<string InstrStr, RegisterClass RC, ValueType VT,
                          ValueType MaskVT, string BlendStr, ValueType ZeroVT> {
    // masked store
    def: Pat<(X86mstore (VT RC:$src), addr:$ptr, (MaskVT RC:$mask)),
             (!cast<Instruction>(InstrStr#"mr") addr:$ptr, RC:$mask, RC:$src)>;
    // masked load
    def: Pat<(VT (X86mload addr:$ptr, (MaskVT RC:$mask), undef)),
             (!cast<Instruction>(InstrStr#"rm") RC:$mask, addr:$ptr)>;
    def: Pat<(VT (X86mload addr:$ptr, (MaskVT RC:$mask),
                              (VT (bitconvert (ZeroVT immAllZerosV))))),
             (!cast<Instruction>(InstrStr#"rm") RC:$mask, addr:$ptr)>;
    def: Pat<(VT (X86mload addr:$ptr, (MaskVT RC:$mask), (VT RC:$src0))),
             (!cast<Instruction>(BlendStr#"rr")
                 RC:$src0,
                 (VT (!cast<Instruction>(InstrStr#"rm") RC:$mask, addr:$ptr)),
                 RC:$mask)>;
}
let Predicates = [HasAVX] in {
  defm : maskmov_lowering<"VMASKMOVPS", VR128, v4f32, v4i32, "VBLENDVPS", v4i32>;
  defm : maskmov_lowering<"VMASKMOVPD", VR128, v2f64, v2i64, "VBLENDVPD", v4i32>;
  defm : maskmov_lowering<"VMASKMOVPSY", VR256, v8f32, v8i32, "VBLENDVPSY", v8i32>;
  defm : maskmov_lowering<"VMASKMOVPDY", VR256, v4f64, v4i64, "VBLENDVPDY", v8i32>;
}
let Predicates = [HasAVX1Only] in {
  // load/store i32/i64 not supported use ps/pd version
  defm : maskmov_lowering<"VMASKMOVPSY", VR256, v8i32, v8i32, "VBLENDVPSY", v8i32>;
  defm : maskmov_lowering<"VMASKMOVPDY", VR256, v4i64, v4i64, "VBLENDVPDY", v8i32>;
  defm : maskmov_lowering<"VMASKMOVPS", VR128, v4i32, v4i32, "VBLENDVPS", v4i32>;
  defm : maskmov_lowering<"VMASKMOVPD", VR128, v2i64, v2i64, "VBLENDVPD", v4i32>;
}
let Predicates = [HasAVX2] in {
  defm : maskmov_lowering<"VPMASKMOVDY", VR256, v8i32, v8i32, "VBLENDVPSY", v8i32>;
  defm : maskmov_lowering<"VPMASKMOVQY", VR256, v4i64, v4i64, "VBLENDVPDY", v8i32>;
  defm : maskmov_lowering<"VPMASKMOVD", VR128, v4i32, v4i32, "VBLENDVPS", v4i32>;
  defm : maskmov_lowering<"VPMASKMOVQ", VR128, v2i64, v2i64, "VBLENDVPD", v4i32>;
}

//===----------------------------------------------------------------------===//
// SubVector Broadcasts
// Provide fallback in case the load node that is used in the patterns above
// is used by additional users, which prevents the pattern selection.

let Predicates = [HasAVX2, NoVLX] in {
def : Pat<(v4i64 (X86SubVBroadcast (v2i64 VR128:$src))),
          (VINSERTI128rr (INSERT_SUBREG (v4i64 (IMPLICIT_DEF)), VR128:$src, sub_xmm),
                         (v2i64 VR128:$src), 1)>;
def : Pat<(v8i32 (X86SubVBroadcast (v4i32 VR128:$src))),
          (VINSERTI128rr (INSERT_SUBREG (v8i32 (IMPLICIT_DEF)), VR128:$src, sub_xmm),
                         (v4i32 VR128:$src), 1)>;
def : Pat<(v16i16 (X86SubVBroadcast (v8i16 VR128:$src))),
          (VINSERTI128rr (INSERT_SUBREG (v16i16 (IMPLICIT_DEF)), VR128:$src, sub_xmm),
                         (v8i16 VR128:$src), 1)>;
def : Pat<(v32i8 (X86SubVBroadcast (v16i8 VR128:$src))),
          (VINSERTI128rr (INSERT_SUBREG (v32i8 (IMPLICIT_DEF)), VR128:$src, sub_xmm),
                         (v16i8 VR128:$src), 1)>;
}

let Predicates = [HasAVX, NoVLX] in {
def : Pat<(v4f64 (X86SubVBroadcast (v2f64 VR128:$src))),
          (VINSERTF128rr (INSERT_SUBREG (v4f64 (IMPLICIT_DEF)), VR128:$src, sub_xmm),
                         (v2f64 VR128:$src), 1)>;
def : Pat<(v8f32 (X86SubVBroadcast (v4f32 VR128:$src))),
          (VINSERTF128rr (INSERT_SUBREG (v8f32 (IMPLICIT_DEF)), VR128:$src, sub_xmm),
                         (v4f32 VR128:$src), 1)>;
}

let Predicates = [HasAVX1Only] in {
def : Pat<(v4i64 (X86SubVBroadcast (v2i64 VR128:$src))),
          (VINSERTF128rr (INSERT_SUBREG (v4i64 (IMPLICIT_DEF)), VR128:$src, sub_xmm),
                         (v2i64 VR128:$src), 1)>;
def : Pat<(v8i32 (X86SubVBroadcast (v4i32 VR128:$src))),
          (VINSERTF128rr (INSERT_SUBREG (v8i32 (IMPLICIT_DEF)), VR128:$src, sub_xmm),
                         (v4i32 VR128:$src), 1)>;
def : Pat<(v16i16 (X86SubVBroadcast (v8i16 VR128:$src))),
          (VINSERTF128rr (INSERT_SUBREG (v16i16 (IMPLICIT_DEF)), VR128:$src, sub_xmm),
                         (v8i16 VR128:$src), 1)>;
def : Pat<(v32i8 (X86SubVBroadcast (v16i8 VR128:$src))),
          (VINSERTF128rr (INSERT_SUBREG (v32i8 (IMPLICIT_DEF)), VR128:$src, sub_xmm),
                         (v16i8 VR128:$src), 1)>;
}

//===----------------------------------------------------------------------===//
// Variable Bit Shifts
//
multiclass avx2_var_shift<bits<8> opc, string OpcodeStr, SDNode OpNode,
                          SDNode IntrinNode, ValueType vt128, ValueType vt256> {
  def rr  : AVX28I<opc, MRMSrcReg, (outs VR128:$dst),
             (ins VR128:$src1, VR128:$src2),
             !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
             [(set VR128:$dst,
               (vt128 (OpNode VR128:$src1, (vt128 VR128:$src2))))]>,
             VEX_4V, Sched<[SchedWriteVarVecShift.XMM]>;
  def rm  : AVX28I<opc, MRMSrcMem, (outs VR128:$dst),
             (ins VR128:$src1, i128mem:$src2),
             !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
             [(set VR128:$dst,
               (vt128 (OpNode VR128:$src1,
                       (vt128 (load addr:$src2)))))]>,
             VEX_4V, Sched<[SchedWriteVarVecShift.XMM.Folded,
                            SchedWriteVarVecShift.XMM.ReadAfterFold]>;
  def Yrr : AVX28I<opc, MRMSrcReg, (outs VR256:$dst),
             (ins VR256:$src1, VR256:$src2),
             !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
             [(set VR256:$dst,
               (vt256 (OpNode VR256:$src1, (vt256 VR256:$src2))))]>,
             VEX_4V, VEX_L, Sched<[SchedWriteVarVecShift.YMM]>;
  def Yrm : AVX28I<opc, MRMSrcMem, (outs VR256:$dst),
             (ins VR256:$src1, i256mem:$src2),
             !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
             [(set VR256:$dst,
               (vt256 (OpNode VR256:$src1,
                       (vt256 (load addr:$src2)))))]>,
             VEX_4V, VEX_L, Sched<[SchedWriteVarVecShift.YMM.Folded,
                                   SchedWriteVarVecShift.YMM.ReadAfterFold]>;

  def : Pat<(vt128 (IntrinNode VR128:$src1, VR128:$src2)),
            (!cast<Instruction>(NAME#"rr") VR128:$src1, VR128:$src2)>;
  def : Pat<(vt128 (IntrinNode VR128:$src1, (load addr:$src2))),
            (!cast<Instruction>(NAME#"rm") VR128:$src1, addr:$src2)>;
  def : Pat<(vt256 (IntrinNode VR256:$src1, VR256:$src2)),
            (!cast<Instruction>(NAME#"Yrr") VR256:$src1, VR256:$src2)>;
  def : Pat<(vt256 (IntrinNode VR256:$src1, (load addr:$src2))),
            (!cast<Instruction>(NAME#"Yrm") VR256:$src1, addr:$src2)>;
}

let Predicates = [HasAVX2, NoVLX] in {
  defm VPSLLVD : avx2_var_shift<0x47, "vpsllvd", shl, X86vshlv, v4i32, v8i32>;
  defm VPSLLVQ : avx2_var_shift<0x47, "vpsllvq", shl, X86vshlv, v2i64, v4i64>, VEX_W;
  defm VPSRLVD : avx2_var_shift<0x45, "vpsrlvd", srl, X86vsrlv, v4i32, v8i32>;
  defm VPSRLVQ : avx2_var_shift<0x45, "vpsrlvq", srl, X86vsrlv, v2i64, v4i64>, VEX_W;
  defm VPSRAVD : avx2_var_shift<0x46, "vpsravd", sra, X86vsrav, v4i32, v8i32>;
}

//===----------------------------------------------------------------------===//
// VGATHER - GATHER Operations

// FIXME: Improve scheduling of gather instructions.
multiclass avx2_gather<bits<8> opc, string OpcodeStr, ValueType VTx,
                       ValueType VTy, PatFrag GatherNode128,
                       PatFrag GatherNode256, RegisterClass RC256,
                       X86MemOperand memop128, X86MemOperand memop256,
                       ValueType MTx = VTx, ValueType MTy = VTy> {
  def rm  : AVX28I<opc, MRMSrcMem4VOp3, (outs VR128:$dst, VR128:$mask_wb),
            (ins VR128:$src1, memop128:$src2, VR128:$mask),
            !strconcat(OpcodeStr,
              "\t{$mask, $src2, $dst|$dst, $src2, $mask}"),
            [(set (VTx VR128:$dst), (MTx VR128:$mask_wb),
                  (GatherNode128 VR128:$src1, VR128:$mask,
                                vectoraddr:$src2))]>,
            VEX, Sched<[WriteLoad]>;
  def Yrm : AVX28I<opc, MRMSrcMem4VOp3, (outs RC256:$dst, RC256:$mask_wb),
            (ins RC256:$src1, memop256:$src2, RC256:$mask),
            !strconcat(OpcodeStr,
              "\t{$mask, $src2, $dst|$dst, $src2, $mask}"),
            [(set (VTy RC256:$dst), (MTy RC256:$mask_wb),
                  (GatherNode256 RC256:$src1, RC256:$mask,
                                vectoraddr:$src2))]>,
            VEX, VEX_L, Sched<[WriteLoad]>;
}

let Predicates = [UseAVX2] in {
  let mayLoad = 1, hasSideEffects = 0, Constraints
    = "@earlyclobber $dst,@earlyclobber $mask_wb, $src1 = $dst, $mask = $mask_wb"
    in {
    defm VPGATHERDQ : avx2_gather<0x90, "vpgatherdq", v2i64, v4i64, mgatherv4i32,
                        mgatherv4i32, VR256, vx128mem, vx256mem>, VEX_W;
    defm VPGATHERQQ : avx2_gather<0x91, "vpgatherqq", v2i64, v4i64, mgatherv2i64,
                        mgatherv4i64, VR256, vx128mem, vy256mem>, VEX_W;
    defm VPGATHERDD : avx2_gather<0x90, "vpgatherdd", v4i32, v8i32, mgatherv4i32,
                        mgatherv8i32, VR256, vx128mem, vy256mem>;
    defm VPGATHERQD : avx2_gather<0x91, "vpgatherqd", v4i32, v4i32, mgatherv2i64,
                        mgatherv4i64, VR128, vx64mem, vy128mem>;

    let ExeDomain = SSEPackedDouble in {
      defm VGATHERDPD : avx2_gather<0x92, "vgatherdpd", v2f64, v4f64, mgatherv4i32,
                          mgatherv4i32, VR256, vx128mem, vx256mem,
                          v2i64, v4i64>, VEX_W;
      defm VGATHERQPD : avx2_gather<0x93, "vgatherqpd", v2f64, v4f64, mgatherv2i64,
                          mgatherv4i64, VR256, vx128mem, vy256mem,
                          v2i64, v4i64>, VEX_W;
    }

    let ExeDomain = SSEPackedSingle in {
      defm VGATHERDPS : avx2_gather<0x92, "vgatherdps", v4f32, v8f32, mgatherv4i32,
                          mgatherv8i32, VR256, vx128mem, vy256mem,
                          v4i32, v8i32>;
      defm VGATHERQPS : avx2_gather<0x93, "vgatherqps", v4f32, v4f32, mgatherv2i64,
                          mgatherv4i64, VR128, vx64mem, vy128mem,
                          v4i32, v4i32>;
    }
  }
}

//===----------------------------------------------------------------------===//
// GFNI instructions
//===----------------------------------------------------------------------===//

multiclass GF2P8MULB_rm<string OpcodeStr, ValueType OpVT,
                        RegisterClass RC, PatFrag MemOpFrag,
                        X86MemOperand X86MemOp, bit Is2Addr = 0> {
  let ExeDomain = SSEPackedInt,
      AsmString = !if(Is2Addr,
        OpcodeStr##"\t{$src2, $dst|$dst, $src2}",
        OpcodeStr##"\t{$src2, $src1, $dst|$dst, $src1, $src2}") in {
    let isCommutable = 1 in
    def rr : PDI<0xCF, MRMSrcReg, (outs RC:$dst), (ins RC:$src1, RC:$src2), "",
                 [(set RC:$dst, (OpVT (X86GF2P8mulb RC:$src1, RC:$src2)))]>,
             Sched<[SchedWriteVecALU.XMM]>, T8PD;

    def rm : PDI<0xCF, MRMSrcMem, (outs RC:$dst), (ins RC:$src1, X86MemOp:$src2), "",
                 [(set RC:$dst, (OpVT (X86GF2P8mulb RC:$src1,
                                 (MemOpFrag addr:$src2))))]>,
             Sched<[SchedWriteVecALU.XMM.Folded, SchedWriteVecALU.XMM.ReadAfterFold]>, T8PD;
  }
}

multiclass GF2P8AFFINE_rmi<bits<8> Op, string OpStr, ValueType OpVT,
                           SDNode OpNode, RegisterClass RC, PatFrag MemOpFrag,
                           X86MemOperand X86MemOp, bit Is2Addr = 0> {
  let AsmString = !if(Is2Addr,
      OpStr##"\t{$src3, $src2, $dst|$dst, $src2, $src3}",
      OpStr##"\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}") in {
  def rri : Ii8<Op, MRMSrcReg, (outs RC:$dst),
              (ins RC:$src1, RC:$src2, u8imm:$src3), "",
              [(set RC:$dst, (OpVT (OpNode RC:$src1, RC:$src2, imm:$src3)))],
              SSEPackedInt>, Sched<[SchedWriteVecALU.XMM]>;
  def rmi : Ii8<Op, MRMSrcMem, (outs RC:$dst),
              (ins RC:$src1, X86MemOp:$src2, u8imm:$src3), "",
              [(set RC:$dst, (OpVT (OpNode RC:$src1,
                                    (MemOpFrag addr:$src2),
                              imm:$src3)))], SSEPackedInt>,
              Sched<[SchedWriteVecALU.XMM.Folded, SchedWriteVecALU.XMM.ReadAfterFold]>;
  }
}

multiclass GF2P8AFFINE_common<bits<8> Op, string OpStr, SDNode OpNode> {
  let Constraints = "$src1 = $dst",
      Predicates  = [HasGFNI, UseSSE2] in
  defm NAME         : GF2P8AFFINE_rmi<Op, OpStr, v16i8, OpNode,
                                      VR128, load, i128mem, 1>;
  let Predicates  = [HasGFNI, HasAVX, NoVLX_Or_NoBWI] in {
    defm V##NAME    : GF2P8AFFINE_rmi<Op, "v"##OpStr, v16i8, OpNode, VR128,
                                      load, i128mem>, VEX_4V, VEX_W;
    defm V##NAME##Y : GF2P8AFFINE_rmi<Op, "v"##OpStr, v32i8, OpNode, VR256,
                                      load, i256mem>, VEX_4V, VEX_L, VEX_W;
  }
}

// GF2P8MULB
let Constraints = "$src1 = $dst",
    Predicates  = [HasGFNI, UseSSE2] in
defm GF2P8MULB      : GF2P8MULB_rm<"gf2p8mulb", v16i8, VR128, memop,
                                    i128mem, 1>;
let Predicates  = [HasGFNI, HasAVX, NoVLX_Or_NoBWI] in {
  defm VGF2P8MULB   : GF2P8MULB_rm<"vgf2p8mulb", v16i8, VR128, load,
                                   i128mem>, VEX_4V;
  defm VGF2P8MULBY  : GF2P8MULB_rm<"vgf2p8mulb", v32i8, VR256, load,
                                   i256mem>, VEX_4V, VEX_L;
}
// GF2P8AFFINEINVQB, GF2P8AFFINEQB
let isCommutable = 0 in {
  defm GF2P8AFFINEINVQB : GF2P8AFFINE_common<0xCF, "gf2p8affineinvqb",
                                             X86GF2P8affineinvqb>, TAPD;
  defm GF2P8AFFINEQB    : GF2P8AFFINE_common<0xCE, "gf2p8affineqb",
                                             X86GF2P8affineqb>, TAPD;
}

