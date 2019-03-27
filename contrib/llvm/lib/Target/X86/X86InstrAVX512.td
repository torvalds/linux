//===-- X86InstrAVX512.td - AVX512 Instruction Set ---------*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file describes the X86 AVX512 instruction set, defining the
// instructions, and properties of the instructions which are needed for code
// generation, machine code emission, and analysis.
//
//===----------------------------------------------------------------------===//

// Group template arguments that can be derived from the vector type (EltNum x
// EltVT).  These are things like the register class for the writemask, etc.
// The idea is to pass one of these as the template argument rather than the
// individual arguments.
// The template is also used for scalar types, in this case numelts is 1.
class X86VectorVTInfo<int numelts, ValueType eltvt, RegisterClass rc,
                      string suffix = ""> {
  RegisterClass RC = rc;
  ValueType EltVT = eltvt;
  int NumElts = numelts;

  // Corresponding mask register class.
  RegisterClass KRC = !cast<RegisterClass>("VK" # NumElts);

  // Corresponding write-mask register class.
  RegisterClass KRCWM = !cast<RegisterClass>("VK" # NumElts # "WM");

  // The mask VT.
  ValueType KVT = !cast<ValueType>("v" # NumElts # "i1");

  // Suffix used in the instruction mnemonic.
  string Suffix = suffix;

  // VTName is a string name for vector VT. For vector types it will be
  // v # NumElts # EltVT, so for vector of 8 elements of i32 it will be v8i32
  // It is a little bit complex for scalar types, where NumElts = 1.
  // In this case we build v4f32 or v2f64
  string VTName = "v" # !if (!eq (NumElts, 1),
                        !if (!eq (EltVT.Size, 32), 4,
                        !if (!eq (EltVT.Size, 64), 2, NumElts)), NumElts) # EltVT;

  // The vector VT.
  ValueType VT = !cast<ValueType>(VTName);

  string EltTypeName = !cast<string>(EltVT);
  // Size of the element type in bits, e.g. 32 for v16i32.
  string EltSizeName = !subst("i", "", !subst("f", "", EltTypeName));
  int EltSize = EltVT.Size;

  // "i" for integer types and "f" for floating-point types
  string TypeVariantName = !subst(EltSizeName, "", EltTypeName);

  // Size of RC in bits, e.g. 512 for VR512.
  int Size = VT.Size;

  // The corresponding memory operand, e.g. i512mem for VR512.
  X86MemOperand MemOp = !cast<X86MemOperand>(TypeVariantName # Size # "mem");
  X86MemOperand ScalarMemOp = !cast<X86MemOperand>(EltVT # "mem");
  // FP scalar memory operand for intrinsics - ssmem/sdmem.
  Operand IntScalarMemOp = !if (!eq (EltTypeName, "f32"), !cast<Operand>("ssmem"),
                           !if (!eq (EltTypeName, "f64"), !cast<Operand>("sdmem"), ?));

  // Load patterns
  PatFrag LdFrag = !cast<PatFrag>("load" # VTName);

  PatFrag AlignedLdFrag = !cast<PatFrag>("alignedload" # VTName);

  PatFrag ScalarLdFrag = !cast<PatFrag>("load" # EltVT);

  ComplexPattern ScalarIntMemCPat = !if (!eq (EltTypeName, "f32"),
                                          !cast<ComplexPattern>("sse_load_f32"),
                                    !if (!eq (EltTypeName, "f64"),
                                          !cast<ComplexPattern>("sse_load_f64"),
                                    ?));

  // The string to specify embedded broadcast in assembly.
  string BroadcastStr = "{1to" # NumElts # "}";

  // 8-bit compressed displacement tuple/subvector format.  This is only
  // defined for NumElts <= 8.
  CD8VForm CD8TupleForm = !if (!eq (!srl(NumElts, 4), 0),
                               !cast<CD8VForm>("CD8VT" # NumElts), ?);

  SubRegIndex SubRegIdx = !if (!eq (Size, 128), sub_xmm,
                          !if (!eq (Size, 256), sub_ymm, ?));

  Domain ExeDomain = !if (!eq (EltTypeName, "f32"), SSEPackedSingle,
                     !if (!eq (EltTypeName, "f64"), SSEPackedDouble,
                     SSEPackedInt));

  RegisterClass FRC = !if (!eq (EltTypeName, "f32"), FR32X, FR64X);

  // A vector type of the same width with element type i32.  This is used to
  // create the canonical constant zero node ImmAllZerosV.
  ValueType i32VT = !cast<ValueType>("v" # !srl(Size, 5) # "i32");
  dag ImmAllZerosV = (VT (bitconvert (i32VT immAllZerosV)));

  string ZSuffix = !if (!eq (Size, 128), "Z128",
                   !if (!eq (Size, 256), "Z256", "Z"));
}

def v64i8_info  : X86VectorVTInfo<64,  i8, VR512, "b">;
def v32i16_info : X86VectorVTInfo<32, i16, VR512, "w">;
def v16i32_info : X86VectorVTInfo<16, i32, VR512, "d">;
def v8i64_info  : X86VectorVTInfo<8,  i64, VR512, "q">;
def v16f32_info : X86VectorVTInfo<16, f32, VR512, "ps">;
def v8f64_info  : X86VectorVTInfo<8,  f64, VR512, "pd">;

// "x" in v32i8x_info means RC = VR256X
def v32i8x_info  : X86VectorVTInfo<32,  i8, VR256X, "b">;
def v16i16x_info : X86VectorVTInfo<16, i16, VR256X, "w">;
def v8i32x_info  : X86VectorVTInfo<8,  i32, VR256X, "d">;
def v4i64x_info  : X86VectorVTInfo<4,  i64, VR256X, "q">;
def v8f32x_info  : X86VectorVTInfo<8,  f32, VR256X, "ps">;
def v4f64x_info  : X86VectorVTInfo<4,  f64, VR256X, "pd">;

def v16i8x_info  : X86VectorVTInfo<16,  i8, VR128X, "b">;
def v8i16x_info  : X86VectorVTInfo<8,  i16, VR128X, "w">;
def v4i32x_info  : X86VectorVTInfo<4,  i32, VR128X, "d">;
def v2i64x_info  : X86VectorVTInfo<2,  i64, VR128X, "q">;
def v4f32x_info  : X86VectorVTInfo<4,  f32, VR128X, "ps">;
def v2f64x_info  : X86VectorVTInfo<2,  f64, VR128X, "pd">;

// We map scalar types to the smallest (128-bit) vector type
// with the appropriate element type. This allows to use the same masking logic.
def i32x_info    : X86VectorVTInfo<1,  i32, GR32, "si">;
def i64x_info    : X86VectorVTInfo<1,  i64, GR64, "sq">;
def f32x_info    : X86VectorVTInfo<1,  f32, VR128X, "ss">;
def f64x_info    : X86VectorVTInfo<1,  f64, VR128X, "sd">;

class AVX512VLVectorVTInfo<X86VectorVTInfo i512, X86VectorVTInfo i256,
                           X86VectorVTInfo i128> {
  X86VectorVTInfo info512 = i512;
  X86VectorVTInfo info256 = i256;
  X86VectorVTInfo info128 = i128;
}

def avx512vl_i8_info  : AVX512VLVectorVTInfo<v64i8_info, v32i8x_info,
                                             v16i8x_info>;
def avx512vl_i16_info : AVX512VLVectorVTInfo<v32i16_info, v16i16x_info,
                                             v8i16x_info>;
def avx512vl_i32_info : AVX512VLVectorVTInfo<v16i32_info, v8i32x_info,
                                             v4i32x_info>;
def avx512vl_i64_info : AVX512VLVectorVTInfo<v8i64_info, v4i64x_info,
                                             v2i64x_info>;
def avx512vl_f32_info : AVX512VLVectorVTInfo<v16f32_info, v8f32x_info,
                                             v4f32x_info>;
def avx512vl_f64_info : AVX512VLVectorVTInfo<v8f64_info, v4f64x_info,
                                             v2f64x_info>;

class X86KVectorVTInfo<RegisterClass _krc, RegisterClass _krcwm,
                       ValueType _vt> {
  RegisterClass KRC = _krc;
  RegisterClass KRCWM = _krcwm;
  ValueType KVT = _vt;
}

def v1i1_info : X86KVectorVTInfo<VK1, VK1WM, v1i1>;
def v2i1_info : X86KVectorVTInfo<VK2, VK2WM, v2i1>;
def v4i1_info : X86KVectorVTInfo<VK4, VK4WM, v4i1>;
def v8i1_info : X86KVectorVTInfo<VK8, VK8WM, v8i1>;
def v16i1_info : X86KVectorVTInfo<VK16, VK16WM, v16i1>;
def v32i1_info : X86KVectorVTInfo<VK32, VK32WM, v32i1>;
def v64i1_info : X86KVectorVTInfo<VK64, VK64WM, v64i1>;

// This multiclass generates the masking variants from the non-masking
// variant.  It only provides the assembly pieces for the masking variants.
// It assumes custom ISel patterns for masking which can be provided as
// template arguments.
multiclass AVX512_maskable_custom<bits<8> O, Format F,
                                  dag Outs,
                                  dag Ins, dag MaskingIns, dag ZeroMaskingIns,
                                  string OpcodeStr,
                                  string AttSrcAsm, string IntelSrcAsm,
                                  list<dag> Pattern,
                                  list<dag> MaskingPattern,
                                  list<dag> ZeroMaskingPattern,
                                  string MaskingConstraint = "",
                                  bit IsCommutable = 0,
                                  bit IsKCommutable = 0,
                                  bit IsKZCommutable = IsCommutable> {
  let isCommutable = IsCommutable in
    def NAME: AVX512<O, F, Outs, Ins,
                       OpcodeStr#"\t{"#AttSrcAsm#", $dst|"#
                                     "$dst, "#IntelSrcAsm#"}",
                       Pattern>;

  // Prefer over VMOV*rrk Pat<>
  let isCommutable = IsKCommutable in
    def NAME#k: AVX512<O, F, Outs, MaskingIns,
                       OpcodeStr#"\t{"#AttSrcAsm#", $dst {${mask}}|"#
                                     "$dst {${mask}}, "#IntelSrcAsm#"}",
                       MaskingPattern>,
              EVEX_K {
      // In case of the 3src subclass this is overridden with a let.
      string Constraints = MaskingConstraint;
    }

  // Zero mask does not add any restrictions to commute operands transformation.
  // So, it is Ok to use IsCommutable instead of IsKCommutable.
  let isCommutable = IsKZCommutable in // Prefer over VMOV*rrkz Pat<>
    def NAME#kz: AVX512<O, F, Outs, ZeroMaskingIns,
                       OpcodeStr#"\t{"#AttSrcAsm#", $dst {${mask}} {z}|"#
                                     "$dst {${mask}} {z}, "#IntelSrcAsm#"}",
                       ZeroMaskingPattern>,
              EVEX_KZ;
}


// Common base class of AVX512_maskable and AVX512_maskable_3src.
multiclass AVX512_maskable_common<bits<8> O, Format F, X86VectorVTInfo _,
                                  dag Outs,
                                  dag Ins, dag MaskingIns, dag ZeroMaskingIns,
                                  string OpcodeStr,
                                  string AttSrcAsm, string IntelSrcAsm,
                                  dag RHS, dag MaskingRHS,
                                  SDNode Select = vselect,
                                  string MaskingConstraint = "",
                                  bit IsCommutable = 0,
                                  bit IsKCommutable = 0,
                                  bit IsKZCommutable = IsCommutable> :
  AVX512_maskable_custom<O, F, Outs, Ins, MaskingIns, ZeroMaskingIns, OpcodeStr,
                         AttSrcAsm, IntelSrcAsm,
                         [(set _.RC:$dst, RHS)],
                         [(set _.RC:$dst, MaskingRHS)],
                         [(set _.RC:$dst,
                               (Select _.KRCWM:$mask, RHS, _.ImmAllZerosV))],
                         MaskingConstraint, IsCommutable,
                         IsKCommutable, IsKZCommutable>;

// This multiclass generates the unconditional/non-masking, the masking and
// the zero-masking variant of the vector instruction.  In the masking case, the
// perserved vector elements come from a new dummy input operand tied to $dst.
// This version uses a separate dag for non-masking and masking.
multiclass AVX512_maskable_split<bits<8> O, Format F, X86VectorVTInfo _,
                           dag Outs, dag Ins, string OpcodeStr,
                           string AttSrcAsm, string IntelSrcAsm,
                           dag RHS, dag MaskRHS,
                           bit IsCommutable = 0, bit IsKCommutable = 0,
                           SDNode Select = vselect> :
   AVX512_maskable_custom<O, F, Outs, Ins,
                          !con((ins _.RC:$src0, _.KRCWM:$mask), Ins),
                          !con((ins _.KRCWM:$mask), Ins),
                          OpcodeStr, AttSrcAsm, IntelSrcAsm,
                          [(set _.RC:$dst, RHS)],
                          [(set _.RC:$dst,
                              (Select _.KRCWM:$mask, MaskRHS, _.RC:$src0))],
                          [(set _.RC:$dst,
                              (Select _.KRCWM:$mask, MaskRHS, _.ImmAllZerosV))],
                          "$src0 = $dst", IsCommutable, IsKCommutable>;

// This multiclass generates the unconditional/non-masking, the masking and
// the zero-masking variant of the vector instruction.  In the masking case, the
// perserved vector elements come from a new dummy input operand tied to $dst.
multiclass AVX512_maskable<bits<8> O, Format F, X86VectorVTInfo _,
                           dag Outs, dag Ins, string OpcodeStr,
                           string AttSrcAsm, string IntelSrcAsm,
                           dag RHS,
                           bit IsCommutable = 0, bit IsKCommutable = 0,
                           bit IsKZCommutable = IsCommutable,
                           SDNode Select = vselect> :
   AVX512_maskable_common<O, F, _, Outs, Ins,
                          !con((ins _.RC:$src0, _.KRCWM:$mask), Ins),
                          !con((ins _.KRCWM:$mask), Ins),
                          OpcodeStr, AttSrcAsm, IntelSrcAsm, RHS,
                          (Select _.KRCWM:$mask, RHS, _.RC:$src0),
                          Select, "$src0 = $dst", IsCommutable, IsKCommutable,
                          IsKZCommutable>;

// This multiclass generates the unconditional/non-masking, the masking and
// the zero-masking variant of the scalar instruction.
multiclass AVX512_maskable_scalar<bits<8> O, Format F, X86VectorVTInfo _,
                           dag Outs, dag Ins, string OpcodeStr,
                           string AttSrcAsm, string IntelSrcAsm,
                           dag RHS,
                           bit IsCommutable = 0> :
   AVX512_maskable<O, F, _, Outs, Ins, OpcodeStr, AttSrcAsm, IntelSrcAsm,
                   RHS, IsCommutable, 0, IsCommutable, X86selects>;

// Similar to AVX512_maskable but in this case one of the source operands
// ($src1) is already tied to $dst so we just use that for the preserved
// vector elements.  NOTE that the NonTiedIns (the ins dag) should exclude
// $src1.
multiclass AVX512_maskable_3src<bits<8> O, Format F, X86VectorVTInfo _,
                                dag Outs, dag NonTiedIns, string OpcodeStr,
                                string AttSrcAsm, string IntelSrcAsm,
                                dag RHS,
                                bit IsCommutable = 0,
                                bit IsKCommutable = 0,
                                SDNode Select = vselect,
                                bit MaskOnly = 0> :
   AVX512_maskable_common<O, F, _, Outs,
                          !con((ins _.RC:$src1), NonTiedIns),
                          !con((ins _.RC:$src1, _.KRCWM:$mask), NonTiedIns),
                          !con((ins _.RC:$src1, _.KRCWM:$mask), NonTiedIns),
                          OpcodeStr, AttSrcAsm, IntelSrcAsm,
                          !if(MaskOnly, (null_frag), RHS),
                          (Select _.KRCWM:$mask, RHS, _.RC:$src1),
                          Select, "", IsCommutable, IsKCommutable>;

// Similar to AVX512_maskable_3src but in this case the input VT for the tied
// operand differs from the output VT. This requires a bitconvert on
// the preserved vector going into the vselect.
// NOTE: The unmasked pattern is disabled.
multiclass AVX512_maskable_3src_cast<bits<8> O, Format F, X86VectorVTInfo OutVT,
                                     X86VectorVTInfo InVT,
                                     dag Outs, dag NonTiedIns, string OpcodeStr,
                                     string AttSrcAsm, string IntelSrcAsm,
                                     dag RHS, bit IsCommutable = 0> :
   AVX512_maskable_common<O, F, OutVT, Outs,
                          !con((ins InVT.RC:$src1), NonTiedIns),
                          !con((ins InVT.RC:$src1, InVT.KRCWM:$mask), NonTiedIns),
                          !con((ins InVT.RC:$src1, InVT.KRCWM:$mask), NonTiedIns),
                          OpcodeStr, AttSrcAsm, IntelSrcAsm, (null_frag),
                          (vselect InVT.KRCWM:$mask, RHS,
                           (bitconvert InVT.RC:$src1)),
                           vselect, "", IsCommutable>;

multiclass AVX512_maskable_3src_scalar<bits<8> O, Format F, X86VectorVTInfo _,
                                     dag Outs, dag NonTiedIns, string OpcodeStr,
                                     string AttSrcAsm, string IntelSrcAsm,
                                     dag RHS,
                                     bit IsCommutable = 0,
                                     bit IsKCommutable = 0,
                                     bit MaskOnly = 0> :
   AVX512_maskable_3src<O, F, _, Outs, NonTiedIns, OpcodeStr, AttSrcAsm,
                        IntelSrcAsm, RHS, IsCommutable, IsKCommutable,
                        X86selects, MaskOnly>;

multiclass AVX512_maskable_in_asm<bits<8> O, Format F, X86VectorVTInfo _,
                                  dag Outs, dag Ins,
                                  string OpcodeStr,
                                  string AttSrcAsm, string IntelSrcAsm,
                                  list<dag> Pattern> :
   AVX512_maskable_custom<O, F, Outs, Ins,
                          !con((ins _.RC:$src0, _.KRCWM:$mask), Ins),
                          !con((ins _.KRCWM:$mask), Ins),
                          OpcodeStr, AttSrcAsm, IntelSrcAsm, Pattern, [], [],
                          "$src0 = $dst">;

multiclass AVX512_maskable_3src_in_asm<bits<8> O, Format F, X86VectorVTInfo _,
                                       dag Outs, dag NonTiedIns,
                                       string OpcodeStr,
                                       string AttSrcAsm, string IntelSrcAsm,
                                       list<dag> Pattern> :
   AVX512_maskable_custom<O, F, Outs,
                          !con((ins _.RC:$src1), NonTiedIns),
                          !con((ins _.RC:$src1, _.KRCWM:$mask), NonTiedIns),
                          !con((ins _.RC:$src1, _.KRCWM:$mask), NonTiedIns),
                          OpcodeStr, AttSrcAsm, IntelSrcAsm, Pattern, [], [],
                          "">;

// Instruction with mask that puts result in mask register,
// like "compare" and "vptest"
multiclass AVX512_maskable_custom_cmp<bits<8> O, Format F,
                                  dag Outs,
                                  dag Ins, dag MaskingIns,
                                  string OpcodeStr,
                                  string AttSrcAsm, string IntelSrcAsm,
                                  list<dag> Pattern,
                                  list<dag> MaskingPattern,
                                  bit IsCommutable = 0> {
    let isCommutable = IsCommutable in
    def NAME: AVX512<O, F, Outs, Ins,
                       OpcodeStr#"\t{"#AttSrcAsm#", $dst|"#
                                     "$dst, "#IntelSrcAsm#"}",
                       Pattern>;

    def NAME#k: AVX512<O, F, Outs, MaskingIns,
                       OpcodeStr#"\t{"#AttSrcAsm#", $dst {${mask}}|"#
                                     "$dst {${mask}}, "#IntelSrcAsm#"}",
                       MaskingPattern>, EVEX_K;
}

multiclass AVX512_maskable_common_cmp<bits<8> O, Format F, X86VectorVTInfo _,
                                  dag Outs,
                                  dag Ins, dag MaskingIns,
                                  string OpcodeStr,
                                  string AttSrcAsm, string IntelSrcAsm,
                                  dag RHS, dag MaskingRHS,
                                  bit IsCommutable = 0> :
  AVX512_maskable_custom_cmp<O, F, Outs, Ins, MaskingIns, OpcodeStr,
                         AttSrcAsm, IntelSrcAsm,
                         [(set _.KRC:$dst, RHS)],
                         [(set _.KRC:$dst, MaskingRHS)], IsCommutable>;

multiclass AVX512_maskable_cmp<bits<8> O, Format F, X86VectorVTInfo _,
                           dag Outs, dag Ins, string OpcodeStr,
                           string AttSrcAsm, string IntelSrcAsm,
                           dag RHS, bit IsCommutable = 0> :
   AVX512_maskable_common_cmp<O, F, _, Outs, Ins,
                          !con((ins _.KRCWM:$mask), Ins),
                          OpcodeStr, AttSrcAsm, IntelSrcAsm, RHS,
                          (and _.KRCWM:$mask, RHS), IsCommutable>;

multiclass AVX512_maskable_cmp_alt<bits<8> O, Format F, X86VectorVTInfo _,
                           dag Outs, dag Ins, string OpcodeStr,
                           string AttSrcAsm, string IntelSrcAsm> :
   AVX512_maskable_custom_cmp<O, F, Outs,
                             Ins, !con((ins _.KRCWM:$mask),Ins), OpcodeStr,
                             AttSrcAsm, IntelSrcAsm, [], []>;

// This multiclass generates the unconditional/non-masking, the masking and
// the zero-masking variant of the vector instruction.  In the masking case, the
// perserved vector elements come from a new dummy input operand tied to $dst.
multiclass AVX512_maskable_logic<bits<8> O, Format F, X86VectorVTInfo _,
                           dag Outs, dag Ins, string OpcodeStr,
                           string AttSrcAsm, string IntelSrcAsm,
                           dag RHS, dag MaskedRHS,
                           bit IsCommutable = 0, SDNode Select = vselect> :
   AVX512_maskable_custom<O, F, Outs, Ins,
                          !con((ins _.RC:$src0, _.KRCWM:$mask), Ins),
                          !con((ins _.KRCWM:$mask), Ins),
                          OpcodeStr, AttSrcAsm, IntelSrcAsm,
                          [(set _.RC:$dst, RHS)],
                          [(set _.RC:$dst,
                                (Select _.KRCWM:$mask, MaskedRHS, _.RC:$src0))],
                          [(set _.RC:$dst,
                                (Select _.KRCWM:$mask, MaskedRHS,
                                        _.ImmAllZerosV))],
                          "$src0 = $dst", IsCommutable>;


// Alias instruction that maps zero vector to pxor / xorp* for AVX-512.
// This is expanded by ExpandPostRAPseudos to an xorps / vxorps, and then
// swizzled by ExecutionDomainFix to pxor.
// We set canFoldAsLoad because this can be converted to a constant-pool
// load of an all-zeros value if folding it would be beneficial.
let isReMaterializable = 1, isAsCheapAsAMove = 1, canFoldAsLoad = 1,
    isPseudo = 1, Predicates = [HasAVX512], SchedRW = [WriteZero] in {
def AVX512_512_SET0 : I<0, Pseudo, (outs VR512:$dst), (ins), "",
               [(set VR512:$dst, (v16i32 immAllZerosV))]>;
def AVX512_512_SETALLONES : I<0, Pseudo, (outs VR512:$dst), (ins), "",
               [(set VR512:$dst, (v16i32 immAllOnesV))]>;
}

// Alias instructions that allow VPTERNLOG to be used with a mask to create
// a mix of all ones and all zeros elements. This is done this way to force
// the same register to be used as input for all three sources.
let isPseudo = 1, Predicates = [HasAVX512], SchedRW = [WriteVecALU] in {
def AVX512_512_SEXT_MASK_32 : I<0, Pseudo, (outs VR512:$dst),
                                (ins VK16WM:$mask), "",
                           [(set VR512:$dst, (vselect (v16i1 VK16WM:$mask),
                                                      (v16i32 immAllOnesV),
                                                      (v16i32 immAllZerosV)))]>;
def AVX512_512_SEXT_MASK_64 : I<0, Pseudo, (outs VR512:$dst),
                                (ins VK8WM:$mask), "",
                [(set VR512:$dst, (vselect (v8i1 VK8WM:$mask),
                                           (bc_v8i64 (v16i32 immAllOnesV)),
                                           (bc_v8i64 (v16i32 immAllZerosV))))]>;
}

let isReMaterializable = 1, isAsCheapAsAMove = 1, canFoldAsLoad = 1,
    isPseudo = 1, Predicates = [HasAVX512], SchedRW = [WriteZero] in {
def AVX512_128_SET0 : I<0, Pseudo, (outs VR128X:$dst), (ins), "",
               [(set VR128X:$dst, (v4i32 immAllZerosV))]>;
def AVX512_256_SET0 : I<0, Pseudo, (outs VR256X:$dst), (ins), "",
               [(set VR256X:$dst, (v8i32 immAllZerosV))]>;
}

// Alias instructions that map fld0 to xorps for sse or vxorps for avx.
// This is expanded by ExpandPostRAPseudos.
let isReMaterializable = 1, isAsCheapAsAMove = 1, canFoldAsLoad = 1,
    isPseudo = 1, SchedRW = [WriteZero], Predicates = [HasAVX512] in {
  def AVX512_FsFLD0SS : I<0, Pseudo, (outs FR32X:$dst), (ins), "",
                          [(set FR32X:$dst, fp32imm0)]>;
  def AVX512_FsFLD0SD : I<0, Pseudo, (outs FR64X:$dst), (ins), "",
                          [(set FR64X:$dst, fpimm0)]>;
}

//===----------------------------------------------------------------------===//
// AVX-512 - VECTOR INSERT
//

// Supports two different pattern operators for mask and unmasked ops. Allows
// null_frag to be passed for one.
multiclass vinsert_for_size_split<int Opcode, X86VectorVTInfo From,
                                  X86VectorVTInfo To,
                                  SDPatternOperator vinsert_insert,
                                  SDPatternOperator vinsert_for_mask,
                                  X86FoldableSchedWrite sched> {
  let hasSideEffects = 0, ExeDomain = To.ExeDomain in {
    defm rr : AVX512_maskable_split<Opcode, MRMSrcReg, To, (outs To.RC:$dst),
                   (ins To.RC:$src1, From.RC:$src2, u8imm:$src3),
                   "vinsert" # From.EltTypeName # "x" # From.NumElts,
                   "$src3, $src2, $src1", "$src1, $src2, $src3",
                   (vinsert_insert:$src3 (To.VT To.RC:$src1),
                                         (From.VT From.RC:$src2),
                                         (iPTR imm)),
                   (vinsert_for_mask:$src3 (To.VT To.RC:$src1),
                                           (From.VT From.RC:$src2),
                                           (iPTR imm))>,
                   AVX512AIi8Base, EVEX_4V, Sched<[sched]>;
    let mayLoad = 1 in
    defm rm : AVX512_maskable_split<Opcode, MRMSrcMem, To, (outs To.RC:$dst),
                   (ins To.RC:$src1, From.MemOp:$src2, u8imm:$src3),
                   "vinsert" # From.EltTypeName # "x" # From.NumElts,
                   "$src3, $src2, $src1", "$src1, $src2, $src3",
                   (vinsert_insert:$src3 (To.VT To.RC:$src1),
                               (From.VT (From.LdFrag addr:$src2)),
                               (iPTR imm)),
                   (vinsert_for_mask:$src3 (To.VT To.RC:$src1),
                               (From.VT (From.LdFrag addr:$src2)),
                               (iPTR imm))>, AVX512AIi8Base, EVEX_4V,
                   EVEX_CD8<From.EltSize, From.CD8TupleForm>,
                   Sched<[sched.Folded, sched.ReadAfterFold]>;
  }
}

// Passes the same pattern operator for masked and unmasked ops.
multiclass vinsert_for_size<int Opcode, X86VectorVTInfo From,
                            X86VectorVTInfo To,
                            SDPatternOperator vinsert_insert,
                            X86FoldableSchedWrite sched> :
  vinsert_for_size_split<Opcode, From, To, vinsert_insert, vinsert_insert, sched>;

multiclass vinsert_for_size_lowering<string InstrStr, X86VectorVTInfo From,
                       X86VectorVTInfo To, PatFrag vinsert_insert,
                       SDNodeXForm INSERT_get_vinsert_imm , list<Predicate> p> {
  let Predicates = p in {
    def : Pat<(vinsert_insert:$ins
                     (To.VT To.RC:$src1), (From.VT From.RC:$src2), (iPTR imm)),
              (To.VT (!cast<Instruction>(InstrStr#"rr")
                     To.RC:$src1, From.RC:$src2,
                     (INSERT_get_vinsert_imm To.RC:$ins)))>;

    def : Pat<(vinsert_insert:$ins
                  (To.VT To.RC:$src1),
                  (From.VT (From.LdFrag addr:$src2)),
                  (iPTR imm)),
              (To.VT (!cast<Instruction>(InstrStr#"rm")
                  To.RC:$src1, addr:$src2,
                  (INSERT_get_vinsert_imm To.RC:$ins)))>;
  }
}

multiclass vinsert_for_type<ValueType EltVT32, int Opcode128,
                            ValueType EltVT64, int Opcode256,
                            X86FoldableSchedWrite sched> {

  let Predicates = [HasVLX] in
    defm NAME # "32x4Z256" : vinsert_for_size<Opcode128,
                                 X86VectorVTInfo< 4, EltVT32, VR128X>,
                                 X86VectorVTInfo< 8, EltVT32, VR256X>,
                                 vinsert128_insert, sched>, EVEX_V256;

  defm NAME # "32x4Z" : vinsert_for_size<Opcode128,
                                 X86VectorVTInfo< 4, EltVT32, VR128X>,
                                 X86VectorVTInfo<16, EltVT32, VR512>,
                                 vinsert128_insert, sched>, EVEX_V512;

  defm NAME # "64x4Z" : vinsert_for_size<Opcode256,
                                 X86VectorVTInfo< 4, EltVT64, VR256X>,
                                 X86VectorVTInfo< 8, EltVT64, VR512>,
                                 vinsert256_insert, sched>, VEX_W, EVEX_V512;

  // Even with DQI we'd like to only use these instructions for masking.
  let Predicates = [HasVLX, HasDQI] in
    defm NAME # "64x2Z256" : vinsert_for_size_split<Opcode128,
                                   X86VectorVTInfo< 2, EltVT64, VR128X>,
                                   X86VectorVTInfo< 4, EltVT64, VR256X>,
                                   null_frag, vinsert128_insert, sched>,
                                   VEX_W1X, EVEX_V256;

  // Even with DQI we'd like to only use these instructions for masking.
  let Predicates = [HasDQI] in {
    defm NAME # "64x2Z" : vinsert_for_size_split<Opcode128,
                                 X86VectorVTInfo< 2, EltVT64, VR128X>,
                                 X86VectorVTInfo< 8, EltVT64, VR512>,
                                 null_frag, vinsert128_insert, sched>,
                                 VEX_W, EVEX_V512;

    defm NAME # "32x8Z" : vinsert_for_size_split<Opcode256,
                                   X86VectorVTInfo< 8, EltVT32, VR256X>,
                                   X86VectorVTInfo<16, EltVT32, VR512>,
                                   null_frag, vinsert256_insert, sched>,
                                   EVEX_V512;
  }
}

// FIXME: Is there a better scheduler class for VINSERTF/VINSERTI?
defm VINSERTF : vinsert_for_type<f32, 0x18, f64, 0x1a, WriteFShuffle256>;
defm VINSERTI : vinsert_for_type<i32, 0x38, i64, 0x3a, WriteShuffle256>;

// Codegen pattern with the alternative types,
// Even with AVX512DQ we'll still use these for unmasked operations.
defm : vinsert_for_size_lowering<"VINSERTF32x4Z256", v2f64x_info, v4f64x_info,
              vinsert128_insert, INSERT_get_vinsert128_imm, [HasVLX]>;
defm : vinsert_for_size_lowering<"VINSERTI32x4Z256", v2i64x_info, v4i64x_info,
              vinsert128_insert, INSERT_get_vinsert128_imm, [HasVLX]>;

defm : vinsert_for_size_lowering<"VINSERTF32x4Z", v2f64x_info, v8f64_info,
              vinsert128_insert, INSERT_get_vinsert128_imm, [HasAVX512]>;
defm : vinsert_for_size_lowering<"VINSERTI32x4Z", v2i64x_info, v8i64_info,
              vinsert128_insert, INSERT_get_vinsert128_imm, [HasAVX512]>;

defm : vinsert_for_size_lowering<"VINSERTF64x4Z", v8f32x_info, v16f32_info,
              vinsert256_insert, INSERT_get_vinsert256_imm, [HasAVX512]>;
defm : vinsert_for_size_lowering<"VINSERTI64x4Z", v8i32x_info, v16i32_info,
              vinsert256_insert, INSERT_get_vinsert256_imm, [HasAVX512]>;

// Codegen pattern with the alternative types insert VEC128 into VEC256
defm : vinsert_for_size_lowering<"VINSERTI32x4Z256", v8i16x_info, v16i16x_info,
              vinsert128_insert, INSERT_get_vinsert128_imm, [HasVLX]>;
defm : vinsert_for_size_lowering<"VINSERTI32x4Z256", v16i8x_info, v32i8x_info,
              vinsert128_insert, INSERT_get_vinsert128_imm, [HasVLX]>;
// Codegen pattern with the alternative types insert VEC128 into VEC512
defm : vinsert_for_size_lowering<"VINSERTI32x4Z", v8i16x_info, v32i16_info,
              vinsert128_insert, INSERT_get_vinsert128_imm, [HasAVX512]>;
defm : vinsert_for_size_lowering<"VINSERTI32x4Z", v16i8x_info, v64i8_info,
               vinsert128_insert, INSERT_get_vinsert128_imm, [HasAVX512]>;
// Codegen pattern with the alternative types insert VEC256 into VEC512
defm : vinsert_for_size_lowering<"VINSERTI64x4Z", v16i16x_info, v32i16_info,
              vinsert256_insert, INSERT_get_vinsert256_imm, [HasAVX512]>;
defm : vinsert_for_size_lowering<"VINSERTI64x4Z", v32i8x_info, v64i8_info,
              vinsert256_insert, INSERT_get_vinsert256_imm, [HasAVX512]>;


multiclass vinsert_for_mask_cast<string InstrStr, X86VectorVTInfo From,
                                 X86VectorVTInfo To, X86VectorVTInfo Cast,
                                 PatFrag vinsert_insert,
                                 SDNodeXForm INSERT_get_vinsert_imm,
                                 list<Predicate> p> {
let Predicates = p in {
  def : Pat<(Cast.VT
             (vselect Cast.KRCWM:$mask,
                      (bitconvert
                       (vinsert_insert:$ins (To.VT To.RC:$src1),
                                            (From.VT From.RC:$src2),
                                            (iPTR imm))),
                      Cast.RC:$src0)),
            (!cast<Instruction>(InstrStr#"rrk")
             Cast.RC:$src0, Cast.KRCWM:$mask, To.RC:$src1, From.RC:$src2,
             (INSERT_get_vinsert_imm To.RC:$ins))>;
  def : Pat<(Cast.VT
             (vselect Cast.KRCWM:$mask,
                      (bitconvert
                       (vinsert_insert:$ins (To.VT To.RC:$src1),
                                            (From.VT
                                             (bitconvert
                                              (From.LdFrag addr:$src2))),
                                            (iPTR imm))),
                      Cast.RC:$src0)),
            (!cast<Instruction>(InstrStr#"rmk")
             Cast.RC:$src0, Cast.KRCWM:$mask, To.RC:$src1, addr:$src2,
             (INSERT_get_vinsert_imm To.RC:$ins))>;

  def : Pat<(Cast.VT
             (vselect Cast.KRCWM:$mask,
                      (bitconvert
                       (vinsert_insert:$ins (To.VT To.RC:$src1),
                                            (From.VT From.RC:$src2),
                                            (iPTR imm))),
                      Cast.ImmAllZerosV)),
            (!cast<Instruction>(InstrStr#"rrkz")
             Cast.KRCWM:$mask, To.RC:$src1, From.RC:$src2,
             (INSERT_get_vinsert_imm To.RC:$ins))>;
  def : Pat<(Cast.VT
             (vselect Cast.KRCWM:$mask,
                      (bitconvert
                       (vinsert_insert:$ins (To.VT To.RC:$src1),
                                            (From.VT (From.LdFrag addr:$src2)),
                                            (iPTR imm))),
                      Cast.ImmAllZerosV)),
            (!cast<Instruction>(InstrStr#"rmkz")
             Cast.KRCWM:$mask, To.RC:$src1, addr:$src2,
             (INSERT_get_vinsert_imm To.RC:$ins))>;
}
}

defm : vinsert_for_mask_cast<"VINSERTF32x4Z256", v2f64x_info, v4f64x_info,
                             v8f32x_info, vinsert128_insert,
                             INSERT_get_vinsert128_imm, [HasVLX]>;
defm : vinsert_for_mask_cast<"VINSERTF64x2Z256", v4f32x_info, v8f32x_info,
                             v4f64x_info, vinsert128_insert,
                             INSERT_get_vinsert128_imm, [HasDQI, HasVLX]>;

defm : vinsert_for_mask_cast<"VINSERTI32x4Z256", v2i64x_info, v4i64x_info,
                             v8i32x_info, vinsert128_insert,
                             INSERT_get_vinsert128_imm, [HasVLX]>;
defm : vinsert_for_mask_cast<"VINSERTI32x4Z256", v8i16x_info, v16i16x_info,
                             v8i32x_info, vinsert128_insert,
                             INSERT_get_vinsert128_imm, [HasVLX]>;
defm : vinsert_for_mask_cast<"VINSERTI32x4Z256", v16i8x_info, v32i8x_info,
                             v8i32x_info, vinsert128_insert,
                             INSERT_get_vinsert128_imm, [HasVLX]>;
defm : vinsert_for_mask_cast<"VINSERTF64x2Z256", v4i32x_info, v8i32x_info,
                             v4i64x_info, vinsert128_insert,
                             INSERT_get_vinsert128_imm, [HasDQI, HasVLX]>;
defm : vinsert_for_mask_cast<"VINSERTF64x2Z256", v8i16x_info, v16i16x_info,
                             v4i64x_info, vinsert128_insert,
                             INSERT_get_vinsert128_imm, [HasDQI, HasVLX]>;
defm : vinsert_for_mask_cast<"VINSERTF64x2Z256", v16i8x_info, v32i8x_info,
                             v4i64x_info, vinsert128_insert,
                             INSERT_get_vinsert128_imm, [HasDQI, HasVLX]>;

defm : vinsert_for_mask_cast<"VINSERTF32x4Z", v2f64x_info, v8f64_info,
                             v16f32_info, vinsert128_insert,
                             INSERT_get_vinsert128_imm, [HasAVX512]>;
defm : vinsert_for_mask_cast<"VINSERTF64x2Z", v4f32x_info, v16f32_info,
                             v8f64_info, vinsert128_insert,
                             INSERT_get_vinsert128_imm, [HasDQI]>;

defm : vinsert_for_mask_cast<"VINSERTI32x4Z", v2i64x_info, v8i64_info,
                             v16i32_info, vinsert128_insert,
                             INSERT_get_vinsert128_imm, [HasAVX512]>;
defm : vinsert_for_mask_cast<"VINSERTI32x4Z", v8i16x_info, v32i16_info,
                             v16i32_info, vinsert128_insert,
                             INSERT_get_vinsert128_imm, [HasAVX512]>;
defm : vinsert_for_mask_cast<"VINSERTI32x4Z", v16i8x_info, v64i8_info,
                             v16i32_info, vinsert128_insert,
                             INSERT_get_vinsert128_imm, [HasAVX512]>;
defm : vinsert_for_mask_cast<"VINSERTI64x2Z", v4i32x_info, v16i32_info,
                             v8i64_info, vinsert128_insert,
                             INSERT_get_vinsert128_imm, [HasDQI]>;
defm : vinsert_for_mask_cast<"VINSERTI64x2Z", v8i16x_info, v32i16_info,
                             v8i64_info, vinsert128_insert,
                             INSERT_get_vinsert128_imm, [HasDQI]>;
defm : vinsert_for_mask_cast<"VINSERTI64x2Z", v16i8x_info, v64i8_info,
                             v8i64_info, vinsert128_insert,
                             INSERT_get_vinsert128_imm, [HasDQI]>;

defm : vinsert_for_mask_cast<"VINSERTF32x8Z", v4f64x_info, v8f64_info,
                             v16f32_info, vinsert256_insert,
                             INSERT_get_vinsert256_imm, [HasDQI]>;
defm : vinsert_for_mask_cast<"VINSERTF64x4Z", v8f32x_info, v16f32_info,
                             v8f64_info, vinsert256_insert,
                             INSERT_get_vinsert256_imm, [HasAVX512]>;

defm : vinsert_for_mask_cast<"VINSERTI32x8Z", v4i64x_info, v8i64_info,
                             v16i32_info, vinsert256_insert,
                             INSERT_get_vinsert256_imm, [HasDQI]>;
defm : vinsert_for_mask_cast<"VINSERTI32x8Z", v16i16x_info, v32i16_info,
                             v16i32_info, vinsert256_insert,
                             INSERT_get_vinsert256_imm, [HasDQI]>;
defm : vinsert_for_mask_cast<"VINSERTI32x8Z", v32i8x_info, v64i8_info,
                             v16i32_info, vinsert256_insert,
                             INSERT_get_vinsert256_imm, [HasDQI]>;
defm : vinsert_for_mask_cast<"VINSERTI64x4Z", v8i32x_info, v16i32_info,
                             v8i64_info, vinsert256_insert,
                             INSERT_get_vinsert256_imm, [HasAVX512]>;
defm : vinsert_for_mask_cast<"VINSERTI64x4Z", v16i16x_info, v32i16_info,
                             v8i64_info, vinsert256_insert,
                             INSERT_get_vinsert256_imm, [HasAVX512]>;
defm : vinsert_for_mask_cast<"VINSERTI64x4Z", v32i8x_info, v64i8_info,
                             v8i64_info, vinsert256_insert,
                             INSERT_get_vinsert256_imm, [HasAVX512]>;

// vinsertps - insert f32 to XMM
let ExeDomain = SSEPackedSingle in {
def VINSERTPSZrr : AVX512AIi8<0x21, MRMSrcReg, (outs VR128X:$dst),
      (ins VR128X:$src1, VR128X:$src2, u8imm:$src3),
      "vinsertps\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}",
      [(set VR128X:$dst, (X86insertps VR128X:$src1, VR128X:$src2, imm:$src3))]>,
      EVEX_4V, Sched<[SchedWriteFShuffle.XMM]>;
def VINSERTPSZrm: AVX512AIi8<0x21, MRMSrcMem, (outs VR128X:$dst),
      (ins VR128X:$src1, f32mem:$src2, u8imm:$src3),
      "vinsertps\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}",
      [(set VR128X:$dst, (X86insertps VR128X:$src1,
                          (v4f32 (scalar_to_vector (loadf32 addr:$src2))),
                          imm:$src3))]>,
      EVEX_4V, EVEX_CD8<32, CD8VT1>,
      Sched<[SchedWriteFShuffle.XMM.Folded, SchedWriteFShuffle.XMM.ReadAfterFold]>;
}

//===----------------------------------------------------------------------===//
// AVX-512 VECTOR EXTRACT
//---

// Supports two different pattern operators for mask and unmasked ops. Allows
// null_frag to be passed for one.
multiclass vextract_for_size_split<int Opcode,
                                   X86VectorVTInfo From, X86VectorVTInfo To,
                                   SDPatternOperator vextract_extract,
                                   SDPatternOperator vextract_for_mask,
                                   SchedWrite SchedRR, SchedWrite SchedMR> {

  let hasSideEffects = 0, ExeDomain = To.ExeDomain in {
    defm rr : AVX512_maskable_split<Opcode, MRMDestReg, To, (outs To.RC:$dst),
                (ins From.RC:$src1, u8imm:$idx),
                "vextract" # To.EltTypeName # "x" # To.NumElts,
                "$idx, $src1", "$src1, $idx",
                (vextract_extract:$idx (From.VT From.RC:$src1), (iPTR imm)),
                (vextract_for_mask:$idx (From.VT From.RC:$src1), (iPTR imm))>,
                AVX512AIi8Base, EVEX, Sched<[SchedRR]>;

    def mr  : AVX512AIi8<Opcode, MRMDestMem, (outs),
                    (ins To.MemOp:$dst, From.RC:$src1, u8imm:$idx),
                    "vextract" # To.EltTypeName # "x" # To.NumElts #
                        "\t{$idx, $src1, $dst|$dst, $src1, $idx}",
                    [(store (To.VT (vextract_extract:$idx
                                    (From.VT From.RC:$src1), (iPTR imm))),
                             addr:$dst)]>, EVEX,
                    Sched<[SchedMR]>;

    let mayStore = 1, hasSideEffects = 0 in
    def mrk : AVX512AIi8<Opcode, MRMDestMem, (outs),
                    (ins To.MemOp:$dst, To.KRCWM:$mask,
                                        From.RC:$src1, u8imm:$idx),
                     "vextract" # To.EltTypeName # "x" # To.NumElts #
                          "\t{$idx, $src1, $dst {${mask}}|"
                          "$dst {${mask}}, $src1, $idx}", []>,
                    EVEX_K, EVEX, Sched<[SchedMR]>, NotMemoryFoldable;
  }
}

// Passes the same pattern operator for masked and unmasked ops.
multiclass vextract_for_size<int Opcode, X86VectorVTInfo From,
                             X86VectorVTInfo To,
                             SDPatternOperator vextract_extract,
                             SchedWrite SchedRR, SchedWrite SchedMR> :
  vextract_for_size_split<Opcode, From, To, vextract_extract, vextract_extract, SchedRR, SchedMR>;

// Codegen pattern for the alternative types
multiclass vextract_for_size_lowering<string InstrStr, X86VectorVTInfo From,
                X86VectorVTInfo To, PatFrag vextract_extract,
                SDNodeXForm EXTRACT_get_vextract_imm, list<Predicate> p> {
  let Predicates = p in {
     def : Pat<(vextract_extract:$ext (From.VT From.RC:$src1), (iPTR imm)),
               (To.VT (!cast<Instruction>(InstrStr#"rr")
                          From.RC:$src1,
                          (EXTRACT_get_vextract_imm To.RC:$ext)))>;
     def : Pat<(store (To.VT (vextract_extract:$ext (From.VT From.RC:$src1),
                              (iPTR imm))), addr:$dst),
               (!cast<Instruction>(InstrStr#"mr") addr:$dst, From.RC:$src1,
                (EXTRACT_get_vextract_imm To.RC:$ext))>;
  }
}

multiclass vextract_for_type<ValueType EltVT32, int Opcode128,
                             ValueType EltVT64, int Opcode256,
                             SchedWrite SchedRR, SchedWrite SchedMR> {
  let Predicates = [HasAVX512] in {
    defm NAME # "32x4Z" : vextract_for_size<Opcode128,
                                   X86VectorVTInfo<16, EltVT32, VR512>,
                                   X86VectorVTInfo< 4, EltVT32, VR128X>,
                                   vextract128_extract, SchedRR, SchedMR>,
                                       EVEX_V512, EVEX_CD8<32, CD8VT4>;
    defm NAME # "64x4Z" : vextract_for_size<Opcode256,
                                   X86VectorVTInfo< 8, EltVT64, VR512>,
                                   X86VectorVTInfo< 4, EltVT64, VR256X>,
                                   vextract256_extract, SchedRR, SchedMR>,
                                       VEX_W, EVEX_V512, EVEX_CD8<64, CD8VT4>;
  }
  let Predicates = [HasVLX] in
    defm NAME # "32x4Z256" : vextract_for_size<Opcode128,
                                 X86VectorVTInfo< 8, EltVT32, VR256X>,
                                 X86VectorVTInfo< 4, EltVT32, VR128X>,
                                 vextract128_extract, SchedRR, SchedMR>,
                                     EVEX_V256, EVEX_CD8<32, CD8VT4>;

  // Even with DQI we'd like to only use these instructions for masking.
  let Predicates = [HasVLX, HasDQI] in
    defm NAME # "64x2Z256" : vextract_for_size_split<Opcode128,
                                 X86VectorVTInfo< 4, EltVT64, VR256X>,
                                 X86VectorVTInfo< 2, EltVT64, VR128X>,
                                 null_frag, vextract128_extract, SchedRR, SchedMR>,
                                     VEX_W1X, EVEX_V256, EVEX_CD8<64, CD8VT2>;

  // Even with DQI we'd like to only use these instructions for masking.
  let Predicates = [HasDQI] in {
    defm NAME # "64x2Z" : vextract_for_size_split<Opcode128,
                                 X86VectorVTInfo< 8, EltVT64, VR512>,
                                 X86VectorVTInfo< 2, EltVT64, VR128X>,
                                 null_frag, vextract128_extract, SchedRR, SchedMR>,
                                     VEX_W, EVEX_V512, EVEX_CD8<64, CD8VT2>;
    defm NAME # "32x8Z" : vextract_for_size_split<Opcode256,
                                 X86VectorVTInfo<16, EltVT32, VR512>,
                                 X86VectorVTInfo< 8, EltVT32, VR256X>,
                                 null_frag, vextract256_extract, SchedRR, SchedMR>,
                                     EVEX_V512, EVEX_CD8<32, CD8VT8>;
  }
}

// TODO - replace WriteFStore/WriteVecStore with X86SchedWriteMoveLSWidths types.
defm VEXTRACTF : vextract_for_type<f32, 0x19, f64, 0x1b, WriteFShuffle256, WriteFStore>;
defm VEXTRACTI : vextract_for_type<i32, 0x39, i64, 0x3b, WriteShuffle256, WriteVecStore>;

// extract_subvector codegen patterns with the alternative types.
// Even with AVX512DQ we'll still use these for unmasked operations.
defm : vextract_for_size_lowering<"VEXTRACTF32x4Z", v8f64_info, v2f64x_info,
          vextract128_extract, EXTRACT_get_vextract128_imm, [HasAVX512]>;
defm : vextract_for_size_lowering<"VEXTRACTI32x4Z", v8i64_info, v2i64x_info,
          vextract128_extract, EXTRACT_get_vextract128_imm, [HasAVX512]>;

defm : vextract_for_size_lowering<"VEXTRACTF64x4Z", v16f32_info, v8f32x_info,
          vextract256_extract, EXTRACT_get_vextract256_imm, [HasAVX512]>;
defm : vextract_for_size_lowering<"VEXTRACTI64x4Z", v16i32_info, v8i32x_info,
          vextract256_extract, EXTRACT_get_vextract256_imm, [HasAVX512]>;

defm : vextract_for_size_lowering<"VEXTRACTF32x4Z256", v4f64x_info, v2f64x_info,
          vextract128_extract, EXTRACT_get_vextract128_imm, [HasVLX]>;
defm : vextract_for_size_lowering<"VEXTRACTI32x4Z256", v4i64x_info, v2i64x_info,
          vextract128_extract, EXTRACT_get_vextract128_imm, [HasVLX]>;

// Codegen pattern with the alternative types extract VEC128 from VEC256
defm : vextract_for_size_lowering<"VEXTRACTI32x4Z256", v16i16x_info, v8i16x_info,
          vextract128_extract, EXTRACT_get_vextract128_imm, [HasVLX]>;
defm : vextract_for_size_lowering<"VEXTRACTI32x4Z256", v32i8x_info, v16i8x_info,
          vextract128_extract, EXTRACT_get_vextract128_imm, [HasVLX]>;

// Codegen pattern with the alternative types extract VEC128 from VEC512
defm : vextract_for_size_lowering<"VEXTRACTI32x4Z", v32i16_info, v8i16x_info,
                 vextract128_extract, EXTRACT_get_vextract128_imm, [HasAVX512]>;
defm : vextract_for_size_lowering<"VEXTRACTI32x4Z", v64i8_info, v16i8x_info,
                 vextract128_extract, EXTRACT_get_vextract128_imm, [HasAVX512]>;
// Codegen pattern with the alternative types extract VEC256 from VEC512
defm : vextract_for_size_lowering<"VEXTRACTI64x4Z", v32i16_info, v16i16x_info,
                 vextract256_extract, EXTRACT_get_vextract256_imm, [HasAVX512]>;
defm : vextract_for_size_lowering<"VEXTRACTI64x4Z", v64i8_info, v32i8x_info,
                 vextract256_extract, EXTRACT_get_vextract256_imm, [HasAVX512]>;


// A 128-bit extract from bits [255:128] of a 512-bit vector should use a
// smaller extract to enable EVEX->VEX.
let Predicates = [NoVLX] in {
def : Pat<(v2i64 (extract_subvector (v8i64 VR512:$src), (iPTR 2))),
          (v2i64 (VEXTRACTI128rr
                  (v4i64 (EXTRACT_SUBREG (v8i64 VR512:$src), sub_ymm)),
                  (iPTR 1)))>;
def : Pat<(v2f64 (extract_subvector (v8f64 VR512:$src), (iPTR 2))),
          (v2f64 (VEXTRACTF128rr
                  (v4f64 (EXTRACT_SUBREG (v8f64 VR512:$src), sub_ymm)),
                  (iPTR 1)))>;
def : Pat<(v4i32 (extract_subvector (v16i32 VR512:$src), (iPTR 4))),
          (v4i32 (VEXTRACTI128rr
                  (v8i32 (EXTRACT_SUBREG (v16i32 VR512:$src), sub_ymm)),
                  (iPTR 1)))>;
def : Pat<(v4f32 (extract_subvector (v16f32 VR512:$src), (iPTR 4))),
          (v4f32 (VEXTRACTF128rr
                  (v8f32 (EXTRACT_SUBREG (v16f32 VR512:$src), sub_ymm)),
                  (iPTR 1)))>;
def : Pat<(v8i16 (extract_subvector (v32i16 VR512:$src), (iPTR 8))),
          (v8i16 (VEXTRACTI128rr
                  (v16i16 (EXTRACT_SUBREG (v32i16 VR512:$src), sub_ymm)),
                  (iPTR 1)))>;
def : Pat<(v16i8 (extract_subvector (v64i8 VR512:$src), (iPTR 16))),
          (v16i8 (VEXTRACTI128rr
                  (v32i8 (EXTRACT_SUBREG (v64i8 VR512:$src), sub_ymm)),
                  (iPTR 1)))>;
}

// A 128-bit extract from bits [255:128] of a 512-bit vector should use a
// smaller extract to enable EVEX->VEX.
let Predicates = [HasVLX] in {
def : Pat<(v2i64 (extract_subvector (v8i64 VR512:$src), (iPTR 2))),
          (v2i64 (VEXTRACTI32x4Z256rr
                  (v4i64 (EXTRACT_SUBREG (v8i64 VR512:$src), sub_ymm)),
                  (iPTR 1)))>;
def : Pat<(v2f64 (extract_subvector (v8f64 VR512:$src), (iPTR 2))),
          (v2f64 (VEXTRACTF32x4Z256rr
                  (v4f64 (EXTRACT_SUBREG (v8f64 VR512:$src), sub_ymm)),
                  (iPTR 1)))>;
def : Pat<(v4i32 (extract_subvector (v16i32 VR512:$src), (iPTR 4))),
          (v4i32 (VEXTRACTI32x4Z256rr
                  (v8i32 (EXTRACT_SUBREG (v16i32 VR512:$src), sub_ymm)),
                  (iPTR 1)))>;
def : Pat<(v4f32 (extract_subvector (v16f32 VR512:$src), (iPTR 4))),
          (v4f32 (VEXTRACTF32x4Z256rr
                  (v8f32 (EXTRACT_SUBREG (v16f32 VR512:$src), sub_ymm)),
                  (iPTR 1)))>;
def : Pat<(v8i16 (extract_subvector (v32i16 VR512:$src), (iPTR 8))),
          (v8i16 (VEXTRACTI32x4Z256rr
                  (v16i16 (EXTRACT_SUBREG (v32i16 VR512:$src), sub_ymm)),
                  (iPTR 1)))>;
def : Pat<(v16i8 (extract_subvector (v64i8 VR512:$src), (iPTR 16))),
          (v16i8 (VEXTRACTI32x4Z256rr
                  (v32i8 (EXTRACT_SUBREG (v64i8 VR512:$src), sub_ymm)),
                  (iPTR 1)))>;
}


// Additional patterns for handling a bitcast between the vselect and the
// extract_subvector.
multiclass vextract_for_mask_cast<string InstrStr, X86VectorVTInfo From,
                                  X86VectorVTInfo To, X86VectorVTInfo Cast,
                                  PatFrag vextract_extract,
                                  SDNodeXForm EXTRACT_get_vextract_imm,
                                  list<Predicate> p> {
let Predicates = p in {
  def : Pat<(Cast.VT (vselect Cast.KRCWM:$mask,
                              (bitconvert
                               (To.VT (vextract_extract:$ext
                                       (From.VT From.RC:$src), (iPTR imm)))),
                              To.RC:$src0)),
            (Cast.VT (!cast<Instruction>(InstrStr#"rrk")
                      Cast.RC:$src0, Cast.KRCWM:$mask, From.RC:$src,
                      (EXTRACT_get_vextract_imm To.RC:$ext)))>;

  def : Pat<(Cast.VT (vselect Cast.KRCWM:$mask,
                              (bitconvert
                               (To.VT (vextract_extract:$ext
                                       (From.VT From.RC:$src), (iPTR imm)))),
                              Cast.ImmAllZerosV)),
            (Cast.VT (!cast<Instruction>(InstrStr#"rrkz")
                      Cast.KRCWM:$mask, From.RC:$src,
                      (EXTRACT_get_vextract_imm To.RC:$ext)))>;
}
}

defm : vextract_for_mask_cast<"VEXTRACTF32x4Z256", v4f64x_info, v2f64x_info,
                              v4f32x_info, vextract128_extract,
                              EXTRACT_get_vextract128_imm, [HasVLX]>;
defm : vextract_for_mask_cast<"VEXTRACTF64x2Z256", v8f32x_info, v4f32x_info,
                              v2f64x_info, vextract128_extract,
                              EXTRACT_get_vextract128_imm, [HasDQI, HasVLX]>;

defm : vextract_for_mask_cast<"VEXTRACTI32x4Z256", v4i64x_info, v2i64x_info,
                              v4i32x_info, vextract128_extract,
                              EXTRACT_get_vextract128_imm, [HasVLX]>;
defm : vextract_for_mask_cast<"VEXTRACTI32x4Z256", v16i16x_info, v8i16x_info,
                              v4i32x_info, vextract128_extract,
                              EXTRACT_get_vextract128_imm, [HasVLX]>;
defm : vextract_for_mask_cast<"VEXTRACTI32x4Z256", v32i8x_info, v16i8x_info,
                              v4i32x_info, vextract128_extract,
                              EXTRACT_get_vextract128_imm, [HasVLX]>;
defm : vextract_for_mask_cast<"VEXTRACTI64x2Z256", v8i32x_info, v4i32x_info,
                              v2i64x_info, vextract128_extract,
                              EXTRACT_get_vextract128_imm, [HasDQI, HasVLX]>;
defm : vextract_for_mask_cast<"VEXTRACTI64x2Z256", v16i16x_info, v8i16x_info,
                              v2i64x_info, vextract128_extract,
                              EXTRACT_get_vextract128_imm, [HasDQI, HasVLX]>;
defm : vextract_for_mask_cast<"VEXTRACTI64x2Z256", v32i8x_info, v16i8x_info,
                              v2i64x_info, vextract128_extract,
                              EXTRACT_get_vextract128_imm, [HasDQI, HasVLX]>;

defm : vextract_for_mask_cast<"VEXTRACTF32x4Z", v8f64_info, v2f64x_info,
                              v4f32x_info, vextract128_extract,
                              EXTRACT_get_vextract128_imm, [HasAVX512]>;
defm : vextract_for_mask_cast<"VEXTRACTF64x2Z", v16f32_info, v4f32x_info,
                              v2f64x_info, vextract128_extract,
                              EXTRACT_get_vextract128_imm, [HasDQI]>;

defm : vextract_for_mask_cast<"VEXTRACTI32x4Z", v8i64_info, v2i64x_info,
                              v4i32x_info, vextract128_extract,
                              EXTRACT_get_vextract128_imm, [HasAVX512]>;
defm : vextract_for_mask_cast<"VEXTRACTI32x4Z", v32i16_info, v8i16x_info,
                              v4i32x_info, vextract128_extract,
                              EXTRACT_get_vextract128_imm, [HasAVX512]>;
defm : vextract_for_mask_cast<"VEXTRACTI32x4Z", v64i8_info, v16i8x_info,
                              v4i32x_info, vextract128_extract,
                              EXTRACT_get_vextract128_imm, [HasAVX512]>;
defm : vextract_for_mask_cast<"VEXTRACTI64x2Z", v16i32_info, v4i32x_info,
                              v2i64x_info, vextract128_extract,
                              EXTRACT_get_vextract128_imm, [HasDQI]>;
defm : vextract_for_mask_cast<"VEXTRACTI64x2Z", v32i16_info, v8i16x_info,
                              v2i64x_info, vextract128_extract,
                              EXTRACT_get_vextract128_imm, [HasDQI]>;
defm : vextract_for_mask_cast<"VEXTRACTI64x2Z", v64i8_info, v16i8x_info,
                              v2i64x_info, vextract128_extract,
                              EXTRACT_get_vextract128_imm, [HasDQI]>;

defm : vextract_for_mask_cast<"VEXTRACTF32x8Z", v8f64_info, v4f64x_info,
                              v8f32x_info, vextract256_extract,
                              EXTRACT_get_vextract256_imm, [HasDQI]>;
defm : vextract_for_mask_cast<"VEXTRACTF64x4Z", v16f32_info, v8f32x_info,
                              v4f64x_info, vextract256_extract,
                              EXTRACT_get_vextract256_imm, [HasAVX512]>;

defm : vextract_for_mask_cast<"VEXTRACTI32x8Z", v8i64_info, v4i64x_info,
                              v8i32x_info, vextract256_extract,
                              EXTRACT_get_vextract256_imm, [HasDQI]>;
defm : vextract_for_mask_cast<"VEXTRACTI32x8Z", v32i16_info, v16i16x_info,
                              v8i32x_info, vextract256_extract,
                              EXTRACT_get_vextract256_imm, [HasDQI]>;
defm : vextract_for_mask_cast<"VEXTRACTI32x8Z", v64i8_info, v32i8x_info,
                              v8i32x_info, vextract256_extract,
                              EXTRACT_get_vextract256_imm, [HasDQI]>;
defm : vextract_for_mask_cast<"VEXTRACTI64x4Z", v16i32_info, v8i32x_info,
                              v4i64x_info, vextract256_extract,
                              EXTRACT_get_vextract256_imm, [HasAVX512]>;
defm : vextract_for_mask_cast<"VEXTRACTI64x4Z", v32i16_info, v16i16x_info,
                              v4i64x_info, vextract256_extract,
                              EXTRACT_get_vextract256_imm, [HasAVX512]>;
defm : vextract_for_mask_cast<"VEXTRACTI64x4Z", v64i8_info, v32i8x_info,
                              v4i64x_info, vextract256_extract,
                              EXTRACT_get_vextract256_imm, [HasAVX512]>;

// vextractps - extract 32 bits from XMM
def VEXTRACTPSZrr : AVX512AIi8<0x17, MRMDestReg, (outs GR32:$dst),
      (ins VR128X:$src1, u8imm:$src2),
      "vextractps\t{$src2, $src1, $dst|$dst, $src1, $src2}",
      [(set GR32:$dst, (extractelt (bc_v4i32 (v4f32 VR128X:$src1)), imm:$src2))]>,
      EVEX, VEX_WIG, Sched<[WriteVecExtract]>;

def VEXTRACTPSZmr : AVX512AIi8<0x17, MRMDestMem, (outs),
      (ins f32mem:$dst, VR128X:$src1, u8imm:$src2),
      "vextractps\t{$src2, $src1, $dst|$dst, $src1, $src2}",
      [(store (extractelt (bc_v4i32 (v4f32 VR128X:$src1)), imm:$src2),
                          addr:$dst)]>,
      EVEX, VEX_WIG, EVEX_CD8<32, CD8VT1>, Sched<[WriteVecExtractSt]>;

//===---------------------------------------------------------------------===//
// AVX-512 BROADCAST
//---
// broadcast with a scalar argument.
multiclass avx512_broadcast_scalar<bits<8> opc, string OpcodeStr,
                            string Name,
                            X86VectorVTInfo DestInfo, X86VectorVTInfo SrcInfo> {
  def : Pat<(DestInfo.VT (X86VBroadcast SrcInfo.FRC:$src)),
            (!cast<Instruction>(Name#DestInfo.ZSuffix#r)
             (SrcInfo.VT (COPY_TO_REGCLASS SrcInfo.FRC:$src, SrcInfo.RC)))>;
  def : Pat<(DestInfo.VT (vselect DestInfo.KRCWM:$mask,
                                  (X86VBroadcast SrcInfo.FRC:$src),
                                  DestInfo.RC:$src0)),
            (!cast<Instruction>(Name#DestInfo.ZSuffix#rk)
             DestInfo.RC:$src0, DestInfo.KRCWM:$mask,
             (SrcInfo.VT (COPY_TO_REGCLASS SrcInfo.FRC:$src, SrcInfo.RC)))>;
  def : Pat<(DestInfo.VT (vselect DestInfo.KRCWM:$mask,
                                  (X86VBroadcast SrcInfo.FRC:$src),
                                  DestInfo.ImmAllZerosV)),
            (!cast<Instruction>(Name#DestInfo.ZSuffix#rkz)
             DestInfo.KRCWM:$mask, (SrcInfo.VT (COPY_TO_REGCLASS SrcInfo.FRC:$src, SrcInfo.RC)))>;
}

// Split version to allow mask and broadcast node to be different types. This
// helps support the 32x2 broadcasts.
multiclass avx512_broadcast_rm_split<bits<8> opc, string OpcodeStr,
                                     string Name,
                                     SchedWrite SchedRR, SchedWrite SchedRM,
                                     X86VectorVTInfo MaskInfo,
                                     X86VectorVTInfo DestInfo,
                                     X86VectorVTInfo SrcInfo,
                                     SDPatternOperator UnmaskedOp = X86VBroadcast> {
  let ExeDomain = DestInfo.ExeDomain, hasSideEffects = 0 in {
  defm r : AVX512_maskable_split<opc, MRMSrcReg, MaskInfo,
                   (outs MaskInfo.RC:$dst),
                   (ins SrcInfo.RC:$src), OpcodeStr, "$src", "$src",
                   (MaskInfo.VT
                    (bitconvert
                     (DestInfo.VT
                      (UnmaskedOp (SrcInfo.VT SrcInfo.RC:$src))))),
                   (MaskInfo.VT
                    (bitconvert
                     (DestInfo.VT
                      (X86VBroadcast (SrcInfo.VT SrcInfo.RC:$src)))))>,
                   T8PD, EVEX, Sched<[SchedRR]>;
  let mayLoad = 1 in
  defm m : AVX512_maskable_split<opc, MRMSrcMem, MaskInfo,
                   (outs MaskInfo.RC:$dst),
                   (ins SrcInfo.ScalarMemOp:$src), OpcodeStr, "$src", "$src",
                   (MaskInfo.VT
                    (bitconvert
                     (DestInfo.VT (UnmaskedOp
                                   (SrcInfo.ScalarLdFrag addr:$src))))),
                   (MaskInfo.VT
                    (bitconvert
                     (DestInfo.VT (X86VBroadcast
                                   (SrcInfo.ScalarLdFrag addr:$src)))))>,
                   T8PD, EVEX, EVEX_CD8<SrcInfo.EltSize, CD8VT1>,
                   Sched<[SchedRM]>;
  }

  def : Pat<(MaskInfo.VT
             (bitconvert
              (DestInfo.VT (UnmaskedOp
                            (SrcInfo.VT (scalar_to_vector
                                         (SrcInfo.ScalarLdFrag addr:$src))))))),
            (!cast<Instruction>(Name#MaskInfo.ZSuffix#m) addr:$src)>;
  def : Pat<(MaskInfo.VT (vselect MaskInfo.KRCWM:$mask,
                          (bitconvert
                           (DestInfo.VT
                            (X86VBroadcast
                             (SrcInfo.VT (scalar_to_vector
                                          (SrcInfo.ScalarLdFrag addr:$src)))))),
                          MaskInfo.RC:$src0)),
            (!cast<Instruction>(Name#DestInfo.ZSuffix#mk)
             MaskInfo.RC:$src0, MaskInfo.KRCWM:$mask, addr:$src)>;
  def : Pat<(MaskInfo.VT (vselect MaskInfo.KRCWM:$mask,
                          (bitconvert
                           (DestInfo.VT
                            (X86VBroadcast
                             (SrcInfo.VT (scalar_to_vector
                                          (SrcInfo.ScalarLdFrag addr:$src)))))),
                          MaskInfo.ImmAllZerosV)),
            (!cast<Instruction>(Name#MaskInfo.ZSuffix#mkz)
             MaskInfo.KRCWM:$mask, addr:$src)>;
}

// Helper class to force mask and broadcast result to same type.
multiclass avx512_broadcast_rm<bits<8> opc, string OpcodeStr, string Name,
                               SchedWrite SchedRR, SchedWrite SchedRM,
                               X86VectorVTInfo DestInfo,
                               X86VectorVTInfo SrcInfo> :
  avx512_broadcast_rm_split<opc, OpcodeStr, Name, SchedRR, SchedRM,
                            DestInfo, DestInfo, SrcInfo>;

multiclass avx512_fp_broadcast_sd<bits<8> opc, string OpcodeStr,
                                                       AVX512VLVectorVTInfo _> {
  let Predicates = [HasAVX512] in {
    defm Z  : avx512_broadcast_rm<opc, OpcodeStr, NAME, WriteFShuffle256,
                                  WriteFShuffle256Ld, _.info512, _.info128>,
              avx512_broadcast_scalar<opc, OpcodeStr, NAME, _.info512,
                                      _.info128>,
              EVEX_V512;
  }

  let Predicates = [HasVLX] in {
    defm Z256  : avx512_broadcast_rm<opc, OpcodeStr, NAME, WriteFShuffle256,
                                     WriteFShuffle256Ld, _.info256, _.info128>,
                 avx512_broadcast_scalar<opc, OpcodeStr, NAME, _.info256,
                                         _.info128>,
                 EVEX_V256;
  }
}

multiclass avx512_fp_broadcast_ss<bits<8> opc, string OpcodeStr,
                                                       AVX512VLVectorVTInfo _> {
  let Predicates = [HasAVX512] in {
    defm Z  : avx512_broadcast_rm<opc, OpcodeStr, NAME, WriteFShuffle256,
                                  WriteFShuffle256Ld, _.info512, _.info128>,
              avx512_broadcast_scalar<opc, OpcodeStr, NAME, _.info512,
                                      _.info128>,
              EVEX_V512;
  }

  let Predicates = [HasVLX] in {
    defm Z256  : avx512_broadcast_rm<opc, OpcodeStr, NAME, WriteFShuffle256,
                                     WriteFShuffle256Ld, _.info256, _.info128>,
                 avx512_broadcast_scalar<opc, OpcodeStr, NAME, _.info256,
                                         _.info128>,
                 EVEX_V256;
    defm Z128  : avx512_broadcast_rm<opc, OpcodeStr, NAME, WriteFShuffle256,
                                     WriteFShuffle256Ld, _.info128, _.info128>,
                 avx512_broadcast_scalar<opc, OpcodeStr, NAME, _.info128,
                                         _.info128>,
                 EVEX_V128;
  }
}
defm VBROADCASTSS  : avx512_fp_broadcast_ss<0x18, "vbroadcastss",
                                       avx512vl_f32_info>;
defm VBROADCASTSD  : avx512_fp_broadcast_sd<0x19, "vbroadcastsd",
                                       avx512vl_f64_info>, VEX_W1X;

multiclass avx512_int_broadcast_reg<bits<8> opc, SchedWrite SchedRR,
                                    X86VectorVTInfo _, SDPatternOperator OpNode,
                                    RegisterClass SrcRC> {
  let ExeDomain = _.ExeDomain in
  defm r : AVX512_maskable<opc, MRMSrcReg, _, (outs _.RC:$dst),
                         (ins SrcRC:$src),
                         "vpbroadcast"##_.Suffix, "$src", "$src",
                         (_.VT (OpNode SrcRC:$src))>, T8PD, EVEX,
                         Sched<[SchedRR]>;
}

multiclass avx512_int_broadcastbw_reg<bits<8> opc, string Name, SchedWrite SchedRR,
                                    X86VectorVTInfo _, SDPatternOperator OpNode,
                                    RegisterClass SrcRC, SubRegIndex Subreg> {
  let hasSideEffects = 0, ExeDomain = _.ExeDomain in
  defm r : AVX512_maskable_custom<opc, MRMSrcReg,
                        (outs _.RC:$dst), (ins GR32:$src),
                        !con((ins _.RC:$src0, _.KRCWM:$mask), (ins GR32:$src)),
                        !con((ins _.KRCWM:$mask), (ins GR32:$src)),
                        "vpbroadcast"##_.Suffix, "$src", "$src", [], [], [],
                        "$src0 = $dst">, T8PD, EVEX, Sched<[SchedRR]>;

  def : Pat <(_.VT (OpNode SrcRC:$src)),
             (!cast<Instruction>(Name#r)
              (i32 (INSERT_SUBREG (i32 (IMPLICIT_DEF)), SrcRC:$src, Subreg)))>;

  def : Pat <(vselect _.KRCWM:$mask, (_.VT (OpNode SrcRC:$src)), _.RC:$src0),
             (!cast<Instruction>(Name#rk) _.RC:$src0, _.KRCWM:$mask,
              (i32 (INSERT_SUBREG (i32 (IMPLICIT_DEF)), SrcRC:$src, Subreg)))>;

  def : Pat <(vselect _.KRCWM:$mask, (_.VT (OpNode SrcRC:$src)), _.ImmAllZerosV),
             (!cast<Instruction>(Name#rkz) _.KRCWM:$mask,
              (i32 (INSERT_SUBREG (i32 (IMPLICIT_DEF)), SrcRC:$src, Subreg)))>;
}

multiclass avx512_int_broadcastbw_reg_vl<bits<8> opc, string Name,
                      AVX512VLVectorVTInfo _, SDPatternOperator OpNode,
                      RegisterClass SrcRC, SubRegIndex Subreg, Predicate prd> {
  let Predicates = [prd] in
    defm Z : avx512_int_broadcastbw_reg<opc, Name#Z, WriteShuffle256, _.info512,
              OpNode, SrcRC, Subreg>, EVEX_V512;
  let Predicates = [prd, HasVLX] in {
    defm Z256 : avx512_int_broadcastbw_reg<opc, Name#Z256, WriteShuffle256,
              _.info256, OpNode, SrcRC, Subreg>, EVEX_V256;
    defm Z128 : avx512_int_broadcastbw_reg<opc, Name#Z128, WriteShuffle,
              _.info128, OpNode, SrcRC, Subreg>, EVEX_V128;
  }
}

multiclass avx512_int_broadcast_reg_vl<bits<8> opc, AVX512VLVectorVTInfo _,
                                       SDPatternOperator OpNode,
                                       RegisterClass SrcRC, Predicate prd> {
  let Predicates = [prd] in
    defm Z : avx512_int_broadcast_reg<opc, WriteShuffle256, _.info512, OpNode,
                                      SrcRC>, EVEX_V512;
  let Predicates = [prd, HasVLX] in {
    defm Z256 : avx512_int_broadcast_reg<opc, WriteShuffle256, _.info256, OpNode,
                                         SrcRC>, EVEX_V256;
    defm Z128 : avx512_int_broadcast_reg<opc, WriteShuffle, _.info128, OpNode,
                                         SrcRC>, EVEX_V128;
  }
}

defm VPBROADCASTBr : avx512_int_broadcastbw_reg_vl<0x7A, "VPBROADCASTBr",
                       avx512vl_i8_info, X86VBroadcast, GR8, sub_8bit, HasBWI>;
defm VPBROADCASTWr : avx512_int_broadcastbw_reg_vl<0x7B, "VPBROADCASTWr",
                       avx512vl_i16_info, X86VBroadcast, GR16, sub_16bit,
                       HasBWI>;
defm VPBROADCASTDr : avx512_int_broadcast_reg_vl<0x7C, avx512vl_i32_info,
                                                 X86VBroadcast, GR32, HasAVX512>;
defm VPBROADCASTQr : avx512_int_broadcast_reg_vl<0x7C, avx512vl_i64_info,
                                                 X86VBroadcast, GR64, HasAVX512>, VEX_W;

// Provide aliases for broadcast from the same register class that
// automatically does the extract.
multiclass avx512_int_broadcast_rm_lowering<string Name,
                                            X86VectorVTInfo DestInfo,
                                            X86VectorVTInfo SrcInfo,
                                            X86VectorVTInfo ExtInfo> {
  def : Pat<(DestInfo.VT (X86VBroadcast (SrcInfo.VT SrcInfo.RC:$src))),
            (!cast<Instruction>(Name#DestInfo.ZSuffix#"r")
                (ExtInfo.VT (EXTRACT_SUBREG (SrcInfo.VT SrcInfo.RC:$src), sub_xmm)))>;
}

multiclass avx512_int_broadcast_rm_vl<bits<8> opc, string OpcodeStr,
                                        AVX512VLVectorVTInfo _, Predicate prd> {
  let Predicates = [prd] in {
    defm Z :   avx512_broadcast_rm<opc, OpcodeStr, NAME, WriteShuffle256,
                                   WriteShuffle256Ld, _.info512, _.info128>,
               avx512_int_broadcast_rm_lowering<NAME, _.info512, _.info256, _.info128>,
                                  EVEX_V512;
    // Defined separately to avoid redefinition.
    defm Z_Alt : avx512_int_broadcast_rm_lowering<NAME, _.info512, _.info512, _.info128>;
  }
  let Predicates = [prd, HasVLX] in {
    defm Z256 : avx512_broadcast_rm<opc, OpcodeStr, NAME, WriteShuffle256,
                                    WriteShuffle256Ld, _.info256, _.info128>,
                avx512_int_broadcast_rm_lowering<NAME, _.info256, _.info256, _.info128>,
                                 EVEX_V256;
    defm Z128 : avx512_broadcast_rm<opc, OpcodeStr, NAME, WriteShuffle,
                                    WriteShuffleXLd, _.info128, _.info128>,
                                 EVEX_V128;
  }
}

defm VPBROADCASTB  : avx512_int_broadcast_rm_vl<0x78, "vpbroadcastb",
                                           avx512vl_i8_info, HasBWI>;
defm VPBROADCASTW  : avx512_int_broadcast_rm_vl<0x79, "vpbroadcastw",
                                           avx512vl_i16_info, HasBWI>;
defm VPBROADCASTD  : avx512_int_broadcast_rm_vl<0x58, "vpbroadcastd",
                                           avx512vl_i32_info, HasAVX512>;
defm VPBROADCASTQ  : avx512_int_broadcast_rm_vl<0x59, "vpbroadcastq",
                                           avx512vl_i64_info, HasAVX512>, VEX_W1X;

multiclass avx512_subvec_broadcast_rm<bits<8> opc, string OpcodeStr,
                          X86VectorVTInfo _Dst, X86VectorVTInfo _Src> {
  defm rm : AVX512_maskable<opc, MRMSrcMem, _Dst, (outs _Dst.RC:$dst),
                           (ins _Src.MemOp:$src), OpcodeStr, "$src", "$src",
                           (_Dst.VT (X86SubVBroadcast
                             (_Src.VT (_Src.LdFrag addr:$src))))>,
                           Sched<[SchedWriteShuffle.YMM.Folded]>,
                           AVX5128IBase, EVEX;
}

// This should be used for the AVX512DQ broadcast instructions. It disables
// the unmasked patterns so that we only use the DQ instructions when masking
//  is requested.
multiclass avx512_subvec_broadcast_rm_dq<bits<8> opc, string OpcodeStr,
                          X86VectorVTInfo _Dst, X86VectorVTInfo _Src> {
  let hasSideEffects = 0, mayLoad = 1 in
  defm rm : AVX512_maskable_split<opc, MRMSrcMem, _Dst, (outs _Dst.RC:$dst),
                           (ins _Src.MemOp:$src), OpcodeStr, "$src", "$src",
                           (null_frag),
                           (_Dst.VT (X86SubVBroadcast
                             (_Src.VT (_Src.LdFrag addr:$src))))>,
                           Sched<[SchedWriteShuffle.YMM.Folded]>,
                           AVX5128IBase, EVEX;
}

let Predicates = [HasAVX512] in {
  // 32-bit targets will fail to load a i64 directly but can use ZEXT_LOAD.
  def : Pat<(v8i64 (X86VBroadcast (v8i64 (X86vzload addr:$src)))),
            (VPBROADCASTQZm addr:$src)>;
}

let Predicates = [HasVLX] in {
  // 32-bit targets will fail to load a i64 directly but can use ZEXT_LOAD.
  def : Pat<(v2i64 (X86VBroadcast (v2i64 (X86vzload addr:$src)))),
            (VPBROADCASTQZ128m addr:$src)>;
  def : Pat<(v4i64 (X86VBroadcast (v4i64 (X86vzload addr:$src)))),
            (VPBROADCASTQZ256m addr:$src)>;
}
let Predicates = [HasVLX, HasBWI] in {
  // loadi16 is tricky to fold, because !isTypeDesirableForOp, justifiably.
  // This means we'll encounter truncated i32 loads; match that here.
  def : Pat<(v8i16 (X86VBroadcast (i16 (trunc (i32 (load addr:$src)))))),
            (VPBROADCASTWZ128m addr:$src)>;
  def : Pat<(v16i16 (X86VBroadcast (i16 (trunc (i32 (load addr:$src)))))),
            (VPBROADCASTWZ256m addr:$src)>;
  def : Pat<(v8i16 (X86VBroadcast
              (i16 (trunc (i32 (zextloadi16 addr:$src)))))),
            (VPBROADCASTWZ128m addr:$src)>;
  def : Pat<(v16i16 (X86VBroadcast
              (i16 (trunc (i32 (zextloadi16 addr:$src)))))),
            (VPBROADCASTWZ256m addr:$src)>;
}

//===----------------------------------------------------------------------===//
// AVX-512 BROADCAST SUBVECTORS
//

defm VBROADCASTI32X4 : avx512_subvec_broadcast_rm<0x5a, "vbroadcasti32x4",
                       v16i32_info, v4i32x_info>,
                       EVEX_V512, EVEX_CD8<32, CD8VT4>;
defm VBROADCASTF32X4 : avx512_subvec_broadcast_rm<0x1a, "vbroadcastf32x4",
                       v16f32_info, v4f32x_info>,
                       EVEX_V512, EVEX_CD8<32, CD8VT4>;
defm VBROADCASTI64X4 : avx512_subvec_broadcast_rm<0x5b, "vbroadcasti64x4",
                       v8i64_info, v4i64x_info>, VEX_W,
                       EVEX_V512, EVEX_CD8<64, CD8VT4>;
defm VBROADCASTF64X4 : avx512_subvec_broadcast_rm<0x1b, "vbroadcastf64x4",
                       v8f64_info, v4f64x_info>, VEX_W,
                       EVEX_V512, EVEX_CD8<64, CD8VT4>;

let Predicates = [HasAVX512] in {
def : Pat<(v16f32 (X86SubVBroadcast (loadv8f32 addr:$src))),
          (VBROADCASTF64X4rm addr:$src)>;
def : Pat<(v16i32 (X86SubVBroadcast (loadv8i32 addr:$src))),
          (VBROADCASTI64X4rm addr:$src)>;
def : Pat<(v32i16 (X86SubVBroadcast (loadv16i16 addr:$src))),
          (VBROADCASTI64X4rm addr:$src)>;
def : Pat<(v64i8 (X86SubVBroadcast (loadv32i8 addr:$src))),
          (VBROADCASTI64X4rm addr:$src)>;

// Provide fallback in case the load node that is used in the patterns above
// is used by additional users, which prevents the pattern selection.
def : Pat<(v8f64 (X86SubVBroadcast (v4f64 VR256X:$src))),
          (VINSERTF64x4Zrr (INSERT_SUBREG (v8f64 (IMPLICIT_DEF)), VR256X:$src, sub_ymm),
                           (v4f64 VR256X:$src), 1)>;
def : Pat<(v16f32 (X86SubVBroadcast (v8f32 VR256X:$src))),
          (VINSERTF64x4Zrr (INSERT_SUBREG (v16f32 (IMPLICIT_DEF)), VR256X:$src, sub_ymm),
                           (v8f32 VR256X:$src), 1)>;
def : Pat<(v8i64 (X86SubVBroadcast (v4i64 VR256X:$src))),
          (VINSERTI64x4Zrr (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR256X:$src, sub_ymm),
                           (v4i64 VR256X:$src), 1)>;
def : Pat<(v16i32 (X86SubVBroadcast (v8i32 VR256X:$src))),
          (VINSERTI64x4Zrr (INSERT_SUBREG (v16i32 (IMPLICIT_DEF)), VR256X:$src, sub_ymm),
                           (v8i32 VR256X:$src), 1)>;
def : Pat<(v32i16 (X86SubVBroadcast (v16i16 VR256X:$src))),
          (VINSERTI64x4Zrr (INSERT_SUBREG (v32i16 (IMPLICIT_DEF)), VR256X:$src, sub_ymm),
                           (v16i16 VR256X:$src), 1)>;
def : Pat<(v64i8 (X86SubVBroadcast (v32i8 VR256X:$src))),
          (VINSERTI64x4Zrr (INSERT_SUBREG (v64i8 (IMPLICIT_DEF)), VR256X:$src, sub_ymm),
                           (v32i8 VR256X:$src), 1)>;

def : Pat<(v8f64 (X86SubVBroadcast (loadv2f64 addr:$src))),
          (VBROADCASTF32X4rm addr:$src)>;
def : Pat<(v8i64 (X86SubVBroadcast (loadv2i64 addr:$src))),
          (VBROADCASTI32X4rm addr:$src)>;
def : Pat<(v32i16 (X86SubVBroadcast (loadv8i16 addr:$src))),
          (VBROADCASTI32X4rm addr:$src)>;
def : Pat<(v64i8 (X86SubVBroadcast (loadv16i8 addr:$src))),
          (VBROADCASTI32X4rm addr:$src)>;

// Patterns for selects of bitcasted operations.
def : Pat<(vselect VK16WM:$mask,
                   (bc_v16f32 (v8f64 (X86SubVBroadcast (loadv2f64 addr:$src)))),
                   (bc_v16f32 (v16i32 immAllZerosV))),
          (VBROADCASTF32X4rmkz VK16WM:$mask, addr:$src)>;
def : Pat<(vselect VK16WM:$mask,
                   (bc_v16f32 (v8f64 (X86SubVBroadcast (loadv2f64 addr:$src)))),
                   VR512:$src0),
          (VBROADCASTF32X4rmk VR512:$src0, VK16WM:$mask, addr:$src)>;
def : Pat<(vselect VK16WM:$mask,
                   (bc_v16i32 (v8i64 (X86SubVBroadcast (loadv2i64 addr:$src)))),
                   (v16i32 immAllZerosV)),
          (VBROADCASTI32X4rmkz VK16WM:$mask, addr:$src)>;
def : Pat<(vselect VK16WM:$mask,
                   (bc_v16i32 (v8i64 (X86SubVBroadcast (loadv2i64 addr:$src)))),
                   VR512:$src0),
          (VBROADCASTI32X4rmk VR512:$src0, VK16WM:$mask, addr:$src)>;

def : Pat<(vselect VK8WM:$mask,
                   (bc_v8f64 (v16f32 (X86SubVBroadcast (loadv8f32 addr:$src)))),
                   (bc_v8f64 (v16i32 immAllZerosV))),
          (VBROADCASTF64X4rmkz VK8WM:$mask, addr:$src)>;
def : Pat<(vselect VK8WM:$mask,
                   (bc_v8f64 (v16f32 (X86SubVBroadcast (loadv8f32 addr:$src)))),
                   VR512:$src0),
          (VBROADCASTF64X4rmk VR512:$src0, VK8WM:$mask, addr:$src)>;
def : Pat<(vselect VK8WM:$mask,
                   (bc_v8i64 (v16i32 (X86SubVBroadcast (loadv8i32 addr:$src)))),
                   (bc_v8i64 (v16i32 immAllZerosV))),
          (VBROADCASTI64X4rmkz VK8WM:$mask, addr:$src)>;
def : Pat<(vselect VK8WM:$mask,
                   (bc_v8i64 (v16i32 (X86SubVBroadcast (loadv8i32 addr:$src)))),
                   VR512:$src0),
          (VBROADCASTI64X4rmk VR512:$src0, VK8WM:$mask, addr:$src)>;
}

let Predicates = [HasVLX] in {
defm VBROADCASTI32X4Z256 : avx512_subvec_broadcast_rm<0x5a, "vbroadcasti32x4",
                           v8i32x_info, v4i32x_info>,
                           EVEX_V256, EVEX_CD8<32, CD8VT4>;
defm VBROADCASTF32X4Z256 : avx512_subvec_broadcast_rm<0x1a, "vbroadcastf32x4",
                           v8f32x_info, v4f32x_info>,
                           EVEX_V256, EVEX_CD8<32, CD8VT4>;

def : Pat<(v4f64 (X86SubVBroadcast (loadv2f64 addr:$src))),
          (VBROADCASTF32X4Z256rm addr:$src)>;
def : Pat<(v4i64 (X86SubVBroadcast (loadv2i64 addr:$src))),
          (VBROADCASTI32X4Z256rm addr:$src)>;
def : Pat<(v16i16 (X86SubVBroadcast (loadv8i16 addr:$src))),
          (VBROADCASTI32X4Z256rm addr:$src)>;
def : Pat<(v32i8 (X86SubVBroadcast (loadv16i8 addr:$src))),
          (VBROADCASTI32X4Z256rm addr:$src)>;

// Patterns for selects of bitcasted operations.
def : Pat<(vselect VK8WM:$mask,
                   (bc_v8f32 (v4f64 (X86SubVBroadcast (loadv2f64 addr:$src)))),
                   (bc_v8f32 (v8i32 immAllZerosV))),
          (VBROADCASTF32X4Z256rmkz VK8WM:$mask, addr:$src)>;
def : Pat<(vselect VK8WM:$mask,
                   (bc_v8f32 (v4f64 (X86SubVBroadcast (loadv2f64 addr:$src)))),
                   VR256X:$src0),
          (VBROADCASTF32X4Z256rmk VR256X:$src0, VK8WM:$mask, addr:$src)>;
def : Pat<(vselect VK8WM:$mask,
                   (bc_v8i32 (v4i64 (X86SubVBroadcast (loadv2i64 addr:$src)))),
                   (v8i32 immAllZerosV)),
          (VBROADCASTI32X4Z256rmkz VK8WM:$mask, addr:$src)>;
def : Pat<(vselect VK8WM:$mask,
                   (bc_v8i32 (v4i64 (X86SubVBroadcast (loadv2i64 addr:$src)))),
                   VR256X:$src0),
          (VBROADCASTI32X4Z256rmk VR256X:$src0, VK8WM:$mask, addr:$src)>;


// Provide fallback in case the load node that is used in the patterns above
// is used by additional users, which prevents the pattern selection.
def : Pat<(v4f64 (X86SubVBroadcast (v2f64 VR128X:$src))),
          (VINSERTF32x4Z256rr (INSERT_SUBREG (v4f64 (IMPLICIT_DEF)), VR128X:$src, sub_xmm),
                              (v2f64 VR128X:$src), 1)>;
def : Pat<(v8f32 (X86SubVBroadcast (v4f32 VR128X:$src))),
          (VINSERTF32x4Z256rr (INSERT_SUBREG (v8f32 (IMPLICIT_DEF)), VR128X:$src, sub_xmm),
                              (v4f32 VR128X:$src), 1)>;
def : Pat<(v4i64 (X86SubVBroadcast (v2i64 VR128X:$src))),
          (VINSERTI32x4Z256rr (INSERT_SUBREG (v4i64 (IMPLICIT_DEF)), VR128X:$src, sub_xmm),
                              (v2i64 VR128X:$src), 1)>;
def : Pat<(v8i32 (X86SubVBroadcast (v4i32 VR128X:$src))),
          (VINSERTI32x4Z256rr (INSERT_SUBREG (v8i32 (IMPLICIT_DEF)), VR128X:$src, sub_xmm),
                              (v4i32 VR128X:$src), 1)>;
def : Pat<(v16i16 (X86SubVBroadcast (v8i16 VR128X:$src))),
          (VINSERTI32x4Z256rr (INSERT_SUBREG (v16i16 (IMPLICIT_DEF)), VR128X:$src, sub_xmm),
                              (v8i16 VR128X:$src), 1)>;
def : Pat<(v32i8 (X86SubVBroadcast (v16i8 VR128X:$src))),
          (VINSERTI32x4Z256rr (INSERT_SUBREG (v32i8 (IMPLICIT_DEF)), VR128X:$src, sub_xmm),
                              (v16i8 VR128X:$src), 1)>;
}

let Predicates = [HasVLX, HasDQI] in {
defm VBROADCASTI64X2Z128 : avx512_subvec_broadcast_rm_dq<0x5a, "vbroadcasti64x2",
                           v4i64x_info, v2i64x_info>, VEX_W1X,
                           EVEX_V256, EVEX_CD8<64, CD8VT2>;
defm VBROADCASTF64X2Z128 : avx512_subvec_broadcast_rm_dq<0x1a, "vbroadcastf64x2",
                           v4f64x_info, v2f64x_info>, VEX_W1X,
                           EVEX_V256, EVEX_CD8<64, CD8VT2>;

// Patterns for selects of bitcasted operations.
def : Pat<(vselect VK4WM:$mask,
                   (bc_v4f64 (v8f32 (X86SubVBroadcast (loadv4f32 addr:$src)))),
                   (bc_v4f64 (v8i32 immAllZerosV))),
          (VBROADCASTF64X2Z128rmkz VK4WM:$mask, addr:$src)>;
def : Pat<(vselect VK4WM:$mask,
                   (bc_v4f64 (v8f32 (X86SubVBroadcast (loadv4f32 addr:$src)))),
                   VR256X:$src0),
          (VBROADCASTF64X2Z128rmk VR256X:$src0, VK4WM:$mask, addr:$src)>;
def : Pat<(vselect VK4WM:$mask,
                   (bc_v4i64 (v8i32 (X86SubVBroadcast (loadv4i32 addr:$src)))),
                   (bc_v4i64 (v8i32 immAllZerosV))),
          (VBROADCASTI64X2Z128rmkz VK4WM:$mask, addr:$src)>;
def : Pat<(vselect VK4WM:$mask,
                   (bc_v4i64 (v8i32 (X86SubVBroadcast (loadv4i32 addr:$src)))),
                   VR256X:$src0),
          (VBROADCASTI64X2Z128rmk VR256X:$src0, VK4WM:$mask, addr:$src)>;
}

let Predicates = [HasDQI] in {
defm VBROADCASTI64X2 : avx512_subvec_broadcast_rm_dq<0x5a, "vbroadcasti64x2",
                       v8i64_info, v2i64x_info>, VEX_W,
                       EVEX_V512, EVEX_CD8<64, CD8VT2>;
defm VBROADCASTI32X8 : avx512_subvec_broadcast_rm_dq<0x5b, "vbroadcasti32x8",
                       v16i32_info, v8i32x_info>,
                       EVEX_V512, EVEX_CD8<32, CD8VT8>;
defm VBROADCASTF64X2 : avx512_subvec_broadcast_rm_dq<0x1a, "vbroadcastf64x2",
                       v8f64_info, v2f64x_info>, VEX_W,
                       EVEX_V512, EVEX_CD8<64, CD8VT2>;
defm VBROADCASTF32X8 : avx512_subvec_broadcast_rm_dq<0x1b, "vbroadcastf32x8",
                       v16f32_info, v8f32x_info>,
                       EVEX_V512, EVEX_CD8<32, CD8VT8>;

// Patterns for selects of bitcasted operations.
def : Pat<(vselect VK16WM:$mask,
                   (bc_v16f32 (v8f64 (X86SubVBroadcast (loadv4f64 addr:$src)))),
                   (bc_v16f32 (v16i32 immAllZerosV))),
          (VBROADCASTF32X8rmkz VK16WM:$mask, addr:$src)>;
def : Pat<(vselect VK16WM:$mask,
                   (bc_v16f32 (v8f64 (X86SubVBroadcast (loadv4f64 addr:$src)))),
                   VR512:$src0),
          (VBROADCASTF32X8rmk VR512:$src0, VK16WM:$mask, addr:$src)>;
def : Pat<(vselect VK16WM:$mask,
                   (bc_v16i32 (v8i64 (X86SubVBroadcast (loadv4i64 addr:$src)))),
                   (v16i32 immAllZerosV)),
          (VBROADCASTI32X8rmkz VK16WM:$mask, addr:$src)>;
def : Pat<(vselect VK16WM:$mask,
                   (bc_v16i32 (v8i64 (X86SubVBroadcast (loadv4i64 addr:$src)))),
                   VR512:$src0),
          (VBROADCASTI32X8rmk VR512:$src0, VK16WM:$mask, addr:$src)>;

def : Pat<(vselect VK8WM:$mask,
                   (bc_v8f64 (v16f32 (X86SubVBroadcast (loadv4f32 addr:$src)))),
                   (bc_v8f64 (v16i32 immAllZerosV))),
          (VBROADCASTF64X2rmkz VK8WM:$mask, addr:$src)>;
def : Pat<(vselect VK8WM:$mask,
                   (bc_v8f64 (v16f32 (X86SubVBroadcast (loadv4f32 addr:$src)))),
                   VR512:$src0),
          (VBROADCASTF64X2rmk VR512:$src0, VK8WM:$mask, addr:$src)>;
def : Pat<(vselect VK8WM:$mask,
                   (bc_v8i64 (v16i32 (X86SubVBroadcast (loadv4i32 addr:$src)))),
                   (bc_v8i64 (v16i32 immAllZerosV))),
          (VBROADCASTI64X2rmkz VK8WM:$mask, addr:$src)>;
def : Pat<(vselect VK8WM:$mask,
                   (bc_v8i64 (v16i32 (X86SubVBroadcast (loadv4i32 addr:$src)))),
                   VR512:$src0),
          (VBROADCASTI64X2rmk VR512:$src0, VK8WM:$mask, addr:$src)>;
}

multiclass avx512_common_broadcast_32x2<bits<8> opc, string OpcodeStr,
                         AVX512VLVectorVTInfo _Dst, AVX512VLVectorVTInfo _Src> {
  let Predicates = [HasDQI] in
    defm Z :    avx512_broadcast_rm_split<opc, OpcodeStr, NAME, WriteShuffle256,
                                          WriteShuffle256Ld, _Dst.info512,
                                          _Src.info512, _Src.info128, null_frag>,
                                          EVEX_V512;
  let Predicates = [HasDQI, HasVLX] in
    defm Z256 : avx512_broadcast_rm_split<opc, OpcodeStr, NAME, WriteShuffle256,
                                          WriteShuffle256Ld, _Dst.info256,
                                          _Src.info256, _Src.info128, null_frag>,
                                          EVEX_V256;
}

multiclass avx512_common_broadcast_i32x2<bits<8> opc, string OpcodeStr,
                         AVX512VLVectorVTInfo _Dst, AVX512VLVectorVTInfo _Src> :
  avx512_common_broadcast_32x2<opc, OpcodeStr, _Dst, _Src> {

  let Predicates = [HasDQI, HasVLX] in
    defm Z128 : avx512_broadcast_rm_split<opc, OpcodeStr, NAME, WriteShuffle,
                                          WriteShuffleXLd, _Dst.info128,
                                          _Src.info128, _Src.info128, null_frag>,
                                          EVEX_V128;
}

defm VBROADCASTI32X2  : avx512_common_broadcast_i32x2<0x59, "vbroadcasti32x2",
                                          avx512vl_i32_info, avx512vl_i64_info>;
defm VBROADCASTF32X2  : avx512_common_broadcast_32x2<0x19, "vbroadcastf32x2",
                                          avx512vl_f32_info, avx512vl_f64_info>;

let Predicates = [HasVLX] in {
def : Pat<(v8f32 (X86VBroadcast (v8f32 VR256X:$src))),
          (VBROADCASTSSZ256r (v4f32 (EXTRACT_SUBREG (v8f32 VR256X:$src), sub_xmm)))>;
def : Pat<(v4f64 (X86VBroadcast (v4f64 VR256X:$src))),
          (VBROADCASTSDZ256r (v2f64 (EXTRACT_SUBREG (v4f64 VR256X:$src), sub_xmm)))>;
}

def : Pat<(v16f32 (X86VBroadcast (v16f32 VR512:$src))),
          (VBROADCASTSSZr (v4f32 (EXTRACT_SUBREG (v16f32 VR512:$src), sub_xmm)))>;
def : Pat<(v16f32 (X86VBroadcast (v8f32 VR256X:$src))),
          (VBROADCASTSSZr (v4f32 (EXTRACT_SUBREG (v8f32 VR256X:$src), sub_xmm)))>;

def : Pat<(v8f64 (X86VBroadcast (v8f64 VR512:$src))),
          (VBROADCASTSDZr (v2f64 (EXTRACT_SUBREG (v8f64 VR512:$src), sub_xmm)))>;
def : Pat<(v8f64 (X86VBroadcast (v4f64 VR256X:$src))),
          (VBROADCASTSDZr (v2f64 (EXTRACT_SUBREG (v4f64 VR256X:$src), sub_xmm)))>;

//===----------------------------------------------------------------------===//
// AVX-512 BROADCAST MASK TO VECTOR REGISTER
//---
multiclass avx512_mask_broadcastm<bits<8> opc, string OpcodeStr,
                                  X86VectorVTInfo _, RegisterClass KRC> {
  def rr : AVX512XS8I<opc, MRMSrcReg, (outs _.RC:$dst), (ins KRC:$src),
                  !strconcat(OpcodeStr, "\t{$src, $dst|$dst, $src}"),
                  [(set _.RC:$dst, (_.VT (X86VBroadcastm KRC:$src)))]>,
                  EVEX, Sched<[WriteShuffle]>;
}

multiclass avx512_mask_broadcast<bits<8> opc, string OpcodeStr,
                                 AVX512VLVectorVTInfo VTInfo, RegisterClass KRC> {
  let Predicates = [HasCDI] in
    defm Z : avx512_mask_broadcastm<opc, OpcodeStr, VTInfo.info512, KRC>, EVEX_V512;
  let Predicates = [HasCDI, HasVLX] in {
    defm Z256 : avx512_mask_broadcastm<opc, OpcodeStr, VTInfo.info256, KRC>, EVEX_V256;
    defm Z128 : avx512_mask_broadcastm<opc, OpcodeStr, VTInfo.info128, KRC>, EVEX_V128;
  }
}

defm VPBROADCASTMW2D : avx512_mask_broadcast<0x3A, "vpbroadcastmw2d",
                                               avx512vl_i32_info, VK16>;
defm VPBROADCASTMB2Q : avx512_mask_broadcast<0x2A, "vpbroadcastmb2q",
                                               avx512vl_i64_info, VK8>, VEX_W;

//===----------------------------------------------------------------------===//
// -- VPERMI2 - 3 source operands form --
multiclass avx512_perm_i<bits<8> opc, string OpcodeStr,
                         X86FoldableSchedWrite sched,
                         X86VectorVTInfo _, X86VectorVTInfo IdxVT> {
let Constraints = "$src1 = $dst", ExeDomain = _.ExeDomain,
    hasSideEffects = 0 in {
  defm rr: AVX512_maskable_3src_cast<opc, MRMSrcReg, _, IdxVT, (outs _.RC:$dst),
          (ins _.RC:$src2, _.RC:$src3),
          OpcodeStr, "$src3, $src2", "$src2, $src3",
          (_.VT (X86VPermt2 _.RC:$src2, IdxVT.RC:$src1, _.RC:$src3)), 1>,
          EVEX_4V, AVX5128IBase, Sched<[sched]>;

  let mayLoad = 1 in
  defm rm: AVX512_maskable_3src_cast<opc, MRMSrcMem, _, IdxVT, (outs _.RC:$dst),
            (ins _.RC:$src2, _.MemOp:$src3),
            OpcodeStr, "$src3, $src2", "$src2, $src3",
            (_.VT (X86VPermt2 _.RC:$src2, IdxVT.RC:$src1,
                   (_.VT (_.LdFrag addr:$src3)))), 1>,
            EVEX_4V, AVX5128IBase, Sched<[sched.Folded, sched.ReadAfterFold]>;
  }
}

multiclass avx512_perm_i_mb<bits<8> opc, string OpcodeStr,
                            X86FoldableSchedWrite sched,
                            X86VectorVTInfo _, X86VectorVTInfo IdxVT> {
  let Constraints = "$src1 = $dst", ExeDomain = _.ExeDomain,
      hasSideEffects = 0, mayLoad = 1 in
  defm rmb: AVX512_maskable_3src_cast<opc, MRMSrcMem, _, IdxVT, (outs _.RC:$dst),
              (ins _.RC:$src2, _.ScalarMemOp:$src3),
              OpcodeStr,   !strconcat("${src3}", _.BroadcastStr,", $src2"),
              !strconcat("$src2, ${src3}", _.BroadcastStr ),
              (_.VT (X86VPermt2 _.RC:$src2,
               IdxVT.RC:$src1,(_.VT (X86VBroadcast (_.ScalarLdFrag addr:$src3))))), 1>,
              AVX5128IBase, EVEX_4V, EVEX_B,
              Sched<[sched.Folded, sched.ReadAfterFold]>;
}

multiclass avx512_perm_i_sizes<bits<8> opc, string OpcodeStr,
                               X86FoldableSchedWrite sched,
                               AVX512VLVectorVTInfo VTInfo,
                               AVX512VLVectorVTInfo ShuffleMask> {
  defm NAME: avx512_perm_i<opc, OpcodeStr, sched, VTInfo.info512,
                           ShuffleMask.info512>,
            avx512_perm_i_mb<opc, OpcodeStr, sched, VTInfo.info512,
                             ShuffleMask.info512>, EVEX_V512;
  let Predicates = [HasVLX] in {
  defm NAME#128: avx512_perm_i<opc, OpcodeStr, sched, VTInfo.info128,
                               ShuffleMask.info128>,
                 avx512_perm_i_mb<opc, OpcodeStr, sched, VTInfo.info128,
                                  ShuffleMask.info128>, EVEX_V128;
  defm NAME#256: avx512_perm_i<opc, OpcodeStr, sched, VTInfo.info256,
                               ShuffleMask.info256>,
                 avx512_perm_i_mb<opc, OpcodeStr, sched, VTInfo.info256,
                                  ShuffleMask.info256>, EVEX_V256;
  }
}

multiclass avx512_perm_i_sizes_bw<bits<8> opc, string OpcodeStr,
                                  X86FoldableSchedWrite sched,
                                  AVX512VLVectorVTInfo VTInfo,
                                  AVX512VLVectorVTInfo Idx,
                                  Predicate Prd> {
  let Predicates = [Prd] in
  defm NAME: avx512_perm_i<opc, OpcodeStr, sched, VTInfo.info512,
                           Idx.info512>, EVEX_V512;
  let Predicates = [Prd, HasVLX] in {
  defm NAME#128: avx512_perm_i<opc, OpcodeStr, sched, VTInfo.info128,
                               Idx.info128>, EVEX_V128;
  defm NAME#256: avx512_perm_i<opc, OpcodeStr, sched, VTInfo.info256,
                               Idx.info256>,  EVEX_V256;
  }
}

defm VPERMI2D  : avx512_perm_i_sizes<0x76, "vpermi2d", WriteVarShuffle256,
                  avx512vl_i32_info, avx512vl_i32_info>, EVEX_CD8<32, CD8VF>;
defm VPERMI2Q  : avx512_perm_i_sizes<0x76, "vpermi2q", WriteVarShuffle256,
                  avx512vl_i64_info, avx512vl_i64_info>, VEX_W, EVEX_CD8<64, CD8VF>;
defm VPERMI2W  : avx512_perm_i_sizes_bw<0x75, "vpermi2w", WriteVarShuffle256,
                  avx512vl_i16_info, avx512vl_i16_info, HasBWI>,
                  VEX_W, EVEX_CD8<16, CD8VF>;
defm VPERMI2B  : avx512_perm_i_sizes_bw<0x75, "vpermi2b", WriteVarShuffle256,
                  avx512vl_i8_info, avx512vl_i8_info, HasVBMI>,
                  EVEX_CD8<8, CD8VF>;
defm VPERMI2PS : avx512_perm_i_sizes<0x77, "vpermi2ps", WriteFVarShuffle256,
                  avx512vl_f32_info, avx512vl_i32_info>, EVEX_CD8<32, CD8VF>;
defm VPERMI2PD : avx512_perm_i_sizes<0x77, "vpermi2pd", WriteFVarShuffle256,
                  avx512vl_f64_info, avx512vl_i64_info>, VEX_W, EVEX_CD8<64, CD8VF>;

// Extra patterns to deal with extra bitcasts due to passthru and index being
// different types on the fp versions.
multiclass avx512_perm_i_lowering<string InstrStr, X86VectorVTInfo _,
                                  X86VectorVTInfo IdxVT,
                                  X86VectorVTInfo CastVT> {
  def : Pat<(_.VT (vselect _.KRCWM:$mask,
                             (X86VPermt2 (_.VT _.RC:$src2),
                                         (IdxVT.VT (bitconvert (CastVT.VT _.RC:$src1))), _.RC:$src3),
                             (_.VT (bitconvert (CastVT.VT _.RC:$src1))))),
            (!cast<Instruction>(InstrStr#"rrk") _.RC:$src1, _.KRCWM:$mask,
                                                _.RC:$src2, _.RC:$src3)>;
  def : Pat<(_.VT (vselect _.KRCWM:$mask,
                             (X86VPermt2 _.RC:$src2,
                                         (IdxVT.VT (bitconvert  (CastVT.VT _.RC:$src1))),
                                         (_.LdFrag addr:$src3)),
                             (_.VT (bitconvert  (CastVT.VT _.RC:$src1))))),
            (!cast<Instruction>(InstrStr#"rmk") _.RC:$src1, _.KRCWM:$mask,
                                                _.RC:$src2, addr:$src3)>;
  def : Pat<(_.VT (vselect _.KRCWM:$mask,
                             (X86VPermt2 _.RC:$src2,
                                         (IdxVT.VT (bitconvert  (CastVT.VT _.RC:$src1))),
                                         (X86VBroadcast (_.ScalarLdFrag addr:$src3))),
                             (_.VT (bitconvert  (CastVT.VT _.RC:$src1))))),
            (!cast<Instruction>(InstrStr#"rmbk") _.RC:$src1, _.KRCWM:$mask,
                                                 _.RC:$src2, addr:$src3)>;
}

// TODO: Should we add more casts? The vXi64 case is common due to ABI.
defm : avx512_perm_i_lowering<"VPERMI2PS", v16f32_info, v16i32_info, v8i64_info>;
defm : avx512_perm_i_lowering<"VPERMI2PS256", v8f32x_info, v8i32x_info, v4i64x_info>;
defm : avx512_perm_i_lowering<"VPERMI2PS128", v4f32x_info, v4i32x_info, v2i64x_info>;

// VPERMT2
multiclass avx512_perm_t<bits<8> opc, string OpcodeStr,
                         X86FoldableSchedWrite sched,
                         X86VectorVTInfo _, X86VectorVTInfo IdxVT> {
let Constraints = "$src1 = $dst", ExeDomain = _.ExeDomain in {
  defm rr: AVX512_maskable_3src<opc, MRMSrcReg, _, (outs _.RC:$dst),
          (ins IdxVT.RC:$src2, _.RC:$src3),
          OpcodeStr, "$src3, $src2", "$src2, $src3",
          (_.VT (X86VPermt2 _.RC:$src1, IdxVT.RC:$src2, _.RC:$src3)), 1>,
          EVEX_4V, AVX5128IBase, Sched<[sched]>;

  defm rm: AVX512_maskable_3src<opc, MRMSrcMem, _, (outs _.RC:$dst),
            (ins IdxVT.RC:$src2, _.MemOp:$src3),
            OpcodeStr, "$src3, $src2", "$src2, $src3",
            (_.VT (X86VPermt2 _.RC:$src1, IdxVT.RC:$src2,
                   (_.LdFrag addr:$src3))), 1>,
            EVEX_4V, AVX5128IBase, Sched<[sched.Folded, sched.ReadAfterFold]>;
  }
}
multiclass avx512_perm_t_mb<bits<8> opc, string OpcodeStr,
                            X86FoldableSchedWrite sched,
                            X86VectorVTInfo _, X86VectorVTInfo IdxVT> {
  let Constraints = "$src1 = $dst", ExeDomain = _.ExeDomain in
  defm rmb: AVX512_maskable_3src<opc, MRMSrcMem, _, (outs _.RC:$dst),
              (ins IdxVT.RC:$src2, _.ScalarMemOp:$src3),
              OpcodeStr,   !strconcat("${src3}", _.BroadcastStr,", $src2"),
              !strconcat("$src2, ${src3}", _.BroadcastStr ),
              (_.VT (X86VPermt2 _.RC:$src1,
               IdxVT.RC:$src2,(_.VT (X86VBroadcast (_.ScalarLdFrag addr:$src3))))), 1>,
              AVX5128IBase, EVEX_4V, EVEX_B,
              Sched<[sched.Folded, sched.ReadAfterFold]>;
}

multiclass avx512_perm_t_sizes<bits<8> opc, string OpcodeStr,
                               X86FoldableSchedWrite sched,
                               AVX512VLVectorVTInfo VTInfo,
                               AVX512VLVectorVTInfo ShuffleMask> {
  defm NAME: avx512_perm_t<opc, OpcodeStr, sched, VTInfo.info512,
                              ShuffleMask.info512>,
            avx512_perm_t_mb<opc, OpcodeStr, sched, VTInfo.info512,
                              ShuffleMask.info512>, EVEX_V512;
  let Predicates = [HasVLX] in {
  defm NAME#128: avx512_perm_t<opc, OpcodeStr, sched, VTInfo.info128,
                              ShuffleMask.info128>,
                 avx512_perm_t_mb<opc, OpcodeStr, sched, VTInfo.info128,
                              ShuffleMask.info128>, EVEX_V128;
  defm NAME#256: avx512_perm_t<opc, OpcodeStr, sched, VTInfo.info256,
                              ShuffleMask.info256>,
                 avx512_perm_t_mb<opc, OpcodeStr, sched, VTInfo.info256,
                              ShuffleMask.info256>, EVEX_V256;
  }
}

multiclass avx512_perm_t_sizes_bw<bits<8> opc, string OpcodeStr,
                                  X86FoldableSchedWrite sched,
                                  AVX512VLVectorVTInfo VTInfo,
                                  AVX512VLVectorVTInfo Idx, Predicate Prd> {
  let Predicates = [Prd] in
  defm NAME: avx512_perm_t<opc, OpcodeStr, sched, VTInfo.info512,
                           Idx.info512>, EVEX_V512;
  let Predicates = [Prd, HasVLX] in {
  defm NAME#128: avx512_perm_t<opc, OpcodeStr, sched, VTInfo.info128,
                               Idx.info128>, EVEX_V128;
  defm NAME#256: avx512_perm_t<opc, OpcodeStr, sched, VTInfo.info256,
                               Idx.info256>, EVEX_V256;
  }
}

defm VPERMT2D  : avx512_perm_t_sizes<0x7E, "vpermt2d", WriteVarShuffle256,
                  avx512vl_i32_info, avx512vl_i32_info>, EVEX_CD8<32, CD8VF>;
defm VPERMT2Q  : avx512_perm_t_sizes<0x7E, "vpermt2q", WriteVarShuffle256,
                  avx512vl_i64_info, avx512vl_i64_info>, VEX_W, EVEX_CD8<64, CD8VF>;
defm VPERMT2W  : avx512_perm_t_sizes_bw<0x7D, "vpermt2w", WriteVarShuffle256,
                  avx512vl_i16_info, avx512vl_i16_info, HasBWI>,
                  VEX_W, EVEX_CD8<16, CD8VF>;
defm VPERMT2B  : avx512_perm_t_sizes_bw<0x7D, "vpermt2b", WriteVarShuffle256,
                  avx512vl_i8_info, avx512vl_i8_info, HasVBMI>,
                  EVEX_CD8<8, CD8VF>;
defm VPERMT2PS : avx512_perm_t_sizes<0x7F, "vpermt2ps", WriteFVarShuffle256,
                  avx512vl_f32_info, avx512vl_i32_info>, EVEX_CD8<32, CD8VF>;
defm VPERMT2PD : avx512_perm_t_sizes<0x7F, "vpermt2pd", WriteFVarShuffle256,
                  avx512vl_f64_info, avx512vl_i64_info>, VEX_W, EVEX_CD8<64, CD8VF>;

//===----------------------------------------------------------------------===//
// AVX-512 - BLEND using mask
//

multiclass WriteFVarBlendask<bits<8> opc, string OpcodeStr,
                             X86FoldableSchedWrite sched, X86VectorVTInfo _> {
  let ExeDomain = _.ExeDomain, hasSideEffects = 0 in {
  def rr : AVX5128I<opc, MRMSrcReg, (outs _.RC:$dst),
             (ins _.RC:$src1, _.RC:$src2),
             !strconcat(OpcodeStr,
             "\t{$src2, $src1, ${dst}|${dst}, $src1, $src2}"), []>,
             EVEX_4V, Sched<[sched]>;
  def rrk : AVX5128I<opc, MRMSrcReg, (outs _.RC:$dst),
             (ins _.KRCWM:$mask, _.RC:$src1, _.RC:$src2),
             !strconcat(OpcodeStr,
             "\t{$src2, $src1, ${dst} {${mask}}|${dst} {${mask}}, $src1, $src2}"),
             []>, EVEX_4V, EVEX_K, Sched<[sched]>;
  def rrkz : AVX5128I<opc, MRMSrcReg, (outs _.RC:$dst),
             (ins _.KRCWM:$mask, _.RC:$src1, _.RC:$src2),
             !strconcat(OpcodeStr,
             "\t{$src2, $src1, ${dst} {${mask}} {z}|${dst} {${mask}} {z}, $src1, $src2}"),
             []>, EVEX_4V, EVEX_KZ, Sched<[sched]>, NotMemoryFoldable;
  let mayLoad = 1 in {
  def rm  : AVX5128I<opc, MRMSrcMem, (outs _.RC:$dst),
             (ins _.RC:$src1, _.MemOp:$src2),
             !strconcat(OpcodeStr,
             "\t{$src2, $src1, ${dst}|${dst}, $src1, $src2}"),
             []>, EVEX_4V, EVEX_CD8<_.EltSize, CD8VF>,
             Sched<[sched.Folded, sched.ReadAfterFold]>;
  def rmk : AVX5128I<opc, MRMSrcMem, (outs _.RC:$dst),
             (ins _.KRCWM:$mask, _.RC:$src1, _.MemOp:$src2),
             !strconcat(OpcodeStr,
             "\t{$src2, $src1, ${dst} {${mask}}|${dst} {${mask}}, $src1, $src2}"),
             []>, EVEX_4V, EVEX_K, EVEX_CD8<_.EltSize, CD8VF>,
             Sched<[sched.Folded, sched.ReadAfterFold]>;
  def rmkz : AVX5128I<opc, MRMSrcMem, (outs _.RC:$dst),
             (ins _.KRCWM:$mask, _.RC:$src1, _.MemOp:$src2),
             !strconcat(OpcodeStr,
             "\t{$src2, $src1, ${dst} {${mask}} {z}|${dst} {${mask}} {z}, $src1, $src2}"),
             []>, EVEX_4V, EVEX_KZ, EVEX_CD8<_.EltSize, CD8VF>,
             Sched<[sched.Folded, sched.ReadAfterFold]>, NotMemoryFoldable;
  }
  }
}
multiclass WriteFVarBlendask_rmb<bits<8> opc, string OpcodeStr,
                                 X86FoldableSchedWrite sched, X86VectorVTInfo _> {
  let mayLoad = 1, hasSideEffects = 0 in {
  def rmbk : AVX5128I<opc, MRMSrcMem, (outs _.RC:$dst),
      (ins _.KRCWM:$mask, _.RC:$src1, _.ScalarMemOp:$src2),
       !strconcat(OpcodeStr,
            "\t{${src2}", _.BroadcastStr, ", $src1, $dst {${mask}}|",
            "$dst {${mask}}, $src1, ${src2}", _.BroadcastStr, "}"), []>,
      EVEX_4V, EVEX_K, EVEX_B, EVEX_CD8<_.EltSize, CD8VF>,
      Sched<[sched.Folded, sched.ReadAfterFold]>;

  def rmbkz : AVX5128I<opc, MRMSrcMem, (outs _.RC:$dst),
      (ins _.KRCWM:$mask, _.RC:$src1, _.ScalarMemOp:$src2),
       !strconcat(OpcodeStr,
            "\t{${src2}", _.BroadcastStr, ", $src1, $dst {${mask}} {z}|",
            "$dst {${mask}} {z}, $src1, ${src2}", _.BroadcastStr, "}"), []>,
      EVEX_4V, EVEX_KZ, EVEX_B, EVEX_CD8<_.EltSize, CD8VF>,
      Sched<[sched.Folded, sched.ReadAfterFold]>, NotMemoryFoldable;

  def rmb : AVX5128I<opc, MRMSrcMem, (outs _.RC:$dst),
      (ins _.RC:$src1, _.ScalarMemOp:$src2),
       !strconcat(OpcodeStr,
            "\t{${src2}", _.BroadcastStr, ", $src1, $dst|",
            "$dst, $src1, ${src2}", _.BroadcastStr, "}"), []>,
      EVEX_4V, EVEX_B, EVEX_CD8<_.EltSize, CD8VF>,
      Sched<[sched.Folded, sched.ReadAfterFold]>;
  }
}

multiclass blendmask_dq<bits<8> opc, string OpcodeStr, X86SchedWriteWidths sched,
                        AVX512VLVectorVTInfo VTInfo> {
  defm Z : WriteFVarBlendask<opc, OpcodeStr, sched.ZMM, VTInfo.info512>,
           WriteFVarBlendask_rmb<opc, OpcodeStr, sched.ZMM, VTInfo.info512>,
                                 EVEX_V512;

  let Predicates = [HasVLX] in {
    defm Z256 : WriteFVarBlendask<opc, OpcodeStr, sched.YMM, VTInfo.info256>,
                WriteFVarBlendask_rmb<opc, OpcodeStr, sched.YMM, VTInfo.info256>,
                                      EVEX_V256;
    defm Z128 : WriteFVarBlendask<opc, OpcodeStr, sched.XMM, VTInfo.info128>,
                WriteFVarBlendask_rmb<opc, OpcodeStr, sched.XMM, VTInfo.info128>,
                                      EVEX_V128;
  }
}

multiclass blendmask_bw<bits<8> opc, string OpcodeStr, X86SchedWriteWidths sched,
                        AVX512VLVectorVTInfo VTInfo> {
  let Predicates = [HasBWI] in
    defm Z : WriteFVarBlendask<opc, OpcodeStr, sched.ZMM, VTInfo.info512>,
                               EVEX_V512;

  let Predicates = [HasBWI, HasVLX] in {
    defm Z256 : WriteFVarBlendask<opc, OpcodeStr, sched.YMM, VTInfo.info256>,
                                  EVEX_V256;
    defm Z128 : WriteFVarBlendask<opc, OpcodeStr, sched.XMM, VTInfo.info128>,
                                  EVEX_V128;
  }
}

defm VBLENDMPS : blendmask_dq<0x65, "vblendmps", SchedWriteFVarBlend,
                              avx512vl_f32_info>;
defm VBLENDMPD : blendmask_dq<0x65, "vblendmpd", SchedWriteFVarBlend,
                              avx512vl_f64_info>, VEX_W;
defm VPBLENDMD : blendmask_dq<0x64, "vpblendmd", SchedWriteVarBlend,
                              avx512vl_i32_info>;
defm VPBLENDMQ : blendmask_dq<0x64, "vpblendmq", SchedWriteVarBlend,
                              avx512vl_i64_info>, VEX_W;
defm VPBLENDMB : blendmask_bw<0x66, "vpblendmb", SchedWriteVarBlend,
                              avx512vl_i8_info>;
defm VPBLENDMW : blendmask_bw<0x66, "vpblendmw", SchedWriteVarBlend,
                              avx512vl_i16_info>, VEX_W;

//===----------------------------------------------------------------------===//
// Compare Instructions
//===----------------------------------------------------------------------===//

// avx512_cmp_scalar - AVX512 CMPSS and CMPSD

multiclass avx512_cmp_scalar<X86VectorVTInfo _, SDNode OpNode, SDNode OpNodeRnd,
                             X86FoldableSchedWrite sched> {
  defm  rr_Int  : AVX512_maskable_cmp<0xC2, MRMSrcReg, _,
                      (outs _.KRC:$dst),
                      (ins _.RC:$src1, _.RC:$src2, AVXCC:$cc),
                      "vcmp${cc}"#_.Suffix,
                      "$src2, $src1", "$src1, $src2",
                      (OpNode (_.VT _.RC:$src1),
                              (_.VT _.RC:$src2),
                              imm:$cc)>, EVEX_4V, Sched<[sched]>;
  let mayLoad = 1 in
  defm  rm_Int  : AVX512_maskable_cmp<0xC2, MRMSrcMem, _,
                    (outs _.KRC:$dst),
                    (ins _.RC:$src1, _.IntScalarMemOp:$src2, AVXCC:$cc),
                    "vcmp${cc}"#_.Suffix,
                    "$src2, $src1", "$src1, $src2",
                    (OpNode (_.VT _.RC:$src1), _.ScalarIntMemCPat:$src2,
                        imm:$cc)>, EVEX_4V, EVEX_CD8<_.EltSize, CD8VT1>,
                    Sched<[sched.Folded, sched.ReadAfterFold]>;

  defm  rrb_Int  : AVX512_maskable_cmp<0xC2, MRMSrcReg, _,
                     (outs _.KRC:$dst),
                     (ins _.RC:$src1, _.RC:$src2, AVXCC:$cc),
                     "vcmp${cc}"#_.Suffix,
                     "{sae}, $src2, $src1", "$src1, $src2, {sae}",
                     (OpNodeRnd (_.VT _.RC:$src1),
                                (_.VT _.RC:$src2),
                                imm:$cc,
                                (i32 FROUND_NO_EXC))>,
                     EVEX_4V, EVEX_B, Sched<[sched]>;
  // Accept explicit immediate argument form instead of comparison code.
  let isAsmParserOnly = 1, hasSideEffects = 0 in {
    defm  rri_alt  : AVX512_maskable_cmp_alt<0xC2, MRMSrcReg, _,
                        (outs VK1:$dst),
                        (ins _.RC:$src1, _.RC:$src2, u8imm:$cc),
                        "vcmp"#_.Suffix,
                        "$cc, $src2, $src1", "$src1, $src2, $cc">, EVEX_4V,
                        Sched<[sched]>, NotMemoryFoldable;
  let mayLoad = 1 in
    defm  rmi_alt  : AVX512_maskable_cmp_alt<0xC2, MRMSrcMem, _,
                        (outs _.KRC:$dst),
                        (ins _.RC:$src1, _.ScalarMemOp:$src2, u8imm:$cc),
                        "vcmp"#_.Suffix,
                        "$cc, $src2, $src1", "$src1, $src2, $cc">,
                        EVEX_4V, EVEX_CD8<_.EltSize, CD8VT1>,
                        Sched<[sched.Folded, sched.ReadAfterFold]>, NotMemoryFoldable;

    defm  rrb_alt  : AVX512_maskable_cmp_alt<0xC2, MRMSrcReg, _,
                       (outs _.KRC:$dst),
                       (ins _.RC:$src1, _.RC:$src2, u8imm:$cc),
                       "vcmp"#_.Suffix,
                       "$cc, {sae}, $src2, $src1","$src1, $src2, {sae}, $cc">,
                       EVEX_4V, EVEX_B, Sched<[sched]>, NotMemoryFoldable;
  }// let isAsmParserOnly = 1, hasSideEffects = 0

  let isCodeGenOnly = 1 in {
    let isCommutable = 1 in
    def rr : AVX512Ii8<0xC2, MRMSrcReg,
                (outs _.KRC:$dst), (ins _.FRC:$src1, _.FRC:$src2, AVXCC:$cc),
                !strconcat("vcmp${cc}", _.Suffix,
                           "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
                [(set _.KRC:$dst, (OpNode _.FRC:$src1,
                                          _.FRC:$src2,
                                          imm:$cc))]>,
                EVEX_4V, Sched<[sched]>;
    def rm : AVX512Ii8<0xC2, MRMSrcMem,
              (outs _.KRC:$dst),
              (ins _.FRC:$src1, _.ScalarMemOp:$src2, AVXCC:$cc),
              !strconcat("vcmp${cc}", _.Suffix,
                         "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
              [(set _.KRC:$dst, (OpNode _.FRC:$src1,
                                        (_.ScalarLdFrag addr:$src2),
                                        imm:$cc))]>,
              EVEX_4V, EVEX_CD8<_.EltSize, CD8VT1>,
              Sched<[sched.Folded, sched.ReadAfterFold]>;
  }
}

let Predicates = [HasAVX512] in {
  let ExeDomain = SSEPackedSingle in
  defm VCMPSSZ : avx512_cmp_scalar<f32x_info, X86cmpms, X86cmpmsRnd,
                                   SchedWriteFCmp.Scl>, AVX512XSIi8Base;
  let ExeDomain = SSEPackedDouble in
  defm VCMPSDZ : avx512_cmp_scalar<f64x_info, X86cmpms, X86cmpmsRnd,
                                   SchedWriteFCmp.Scl>, AVX512XDIi8Base, VEX_W;
}

multiclass avx512_icmp_packed<bits<8> opc, string OpcodeStr, PatFrag OpNode,
                              X86FoldableSchedWrite sched, X86VectorVTInfo _,
                              bit IsCommutable> {
  let isCommutable = IsCommutable in
  def rr : AVX512BI<opc, MRMSrcReg,
             (outs _.KRC:$dst), (ins _.RC:$src1, _.RC:$src2),
             !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
             [(set _.KRC:$dst, (OpNode (_.VT _.RC:$src1), (_.VT _.RC:$src2)))]>,
             EVEX_4V, Sched<[sched]>;
  def rm : AVX512BI<opc, MRMSrcMem,
             (outs _.KRC:$dst), (ins _.RC:$src1, _.MemOp:$src2),
             !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
             [(set _.KRC:$dst, (OpNode (_.VT _.RC:$src1),
                                       (_.VT (_.LdFrag addr:$src2))))]>,
             EVEX_4V, Sched<[sched.Folded, sched.ReadAfterFold]>;
  let isCommutable = IsCommutable in
  def rrk : AVX512BI<opc, MRMSrcReg,
              (outs _.KRC:$dst), (ins _.KRCWM:$mask, _.RC:$src1, _.RC:$src2),
              !strconcat(OpcodeStr, "\t{$src2, $src1, $dst {${mask}}|",
                          "$dst {${mask}}, $src1, $src2}"),
              [(set _.KRC:$dst, (and _.KRCWM:$mask,
                                   (OpNode (_.VT _.RC:$src1), (_.VT _.RC:$src2))))]>,
              EVEX_4V, EVEX_K, Sched<[sched]>;
  def rmk : AVX512BI<opc, MRMSrcMem,
              (outs _.KRC:$dst), (ins _.KRCWM:$mask, _.RC:$src1, _.MemOp:$src2),
              !strconcat(OpcodeStr, "\t{$src2, $src1, $dst {${mask}}|",
                          "$dst {${mask}}, $src1, $src2}"),
              [(set _.KRC:$dst, (and _.KRCWM:$mask,
                                   (OpNode (_.VT _.RC:$src1),
                                       (_.VT (_.LdFrag addr:$src2)))))]>,
              EVEX_4V, EVEX_K, Sched<[sched.Folded, sched.ReadAfterFold]>;
}

multiclass avx512_icmp_packed_rmb<bits<8> opc, string OpcodeStr, PatFrag OpNode,
                                  X86FoldableSchedWrite sched, X86VectorVTInfo _,
                                  bit IsCommutable> :
           avx512_icmp_packed<opc, OpcodeStr, OpNode, sched, _, IsCommutable> {
  def rmb : AVX512BI<opc, MRMSrcMem,
              (outs _.KRC:$dst), (ins _.RC:$src1, _.ScalarMemOp:$src2),
              !strconcat(OpcodeStr, "\t{${src2}", _.BroadcastStr, ", $src1, $dst",
                                    "|$dst, $src1, ${src2}", _.BroadcastStr, "}"),
              [(set _.KRC:$dst, (OpNode (_.VT _.RC:$src1),
                              (X86VBroadcast (_.ScalarLdFrag addr:$src2))))]>,
              EVEX_4V, EVEX_B, Sched<[sched.Folded, sched.ReadAfterFold]>;
  def rmbk : AVX512BI<opc, MRMSrcMem,
               (outs _.KRC:$dst), (ins _.KRCWM:$mask, _.RC:$src1,
                                       _.ScalarMemOp:$src2),
               !strconcat(OpcodeStr,
                          "\t{${src2}", _.BroadcastStr, ", $src1, $dst {${mask}}|",
                          "$dst {${mask}}, $src1, ${src2}", _.BroadcastStr, "}"),
               [(set _.KRC:$dst, (and _.KRCWM:$mask,
                                      (OpNode (_.VT _.RC:$src1),
                                        (X86VBroadcast
                                          (_.ScalarLdFrag addr:$src2)))))]>,
               EVEX_4V, EVEX_K, EVEX_B,
               Sched<[sched.Folded, sched.ReadAfterFold]>;
}

multiclass avx512_icmp_packed_vl<bits<8> opc, string OpcodeStr, PatFrag OpNode,
                                 X86SchedWriteWidths sched,
                                 AVX512VLVectorVTInfo VTInfo, Predicate prd,
                                 bit IsCommutable = 0> {
  let Predicates = [prd] in
  defm Z : avx512_icmp_packed<opc, OpcodeStr, OpNode, sched.ZMM,
                              VTInfo.info512, IsCommutable>, EVEX_V512;

  let Predicates = [prd, HasVLX] in {
    defm Z256 : avx512_icmp_packed<opc, OpcodeStr, OpNode, sched.YMM,
                                   VTInfo.info256, IsCommutable>, EVEX_V256;
    defm Z128 : avx512_icmp_packed<opc, OpcodeStr, OpNode, sched.XMM,
                                   VTInfo.info128, IsCommutable>, EVEX_V128;
  }
}

multiclass avx512_icmp_packed_rmb_vl<bits<8> opc, string OpcodeStr,
                                     PatFrag OpNode, X86SchedWriteWidths sched,
                                     AVX512VLVectorVTInfo VTInfo,
                                     Predicate prd, bit IsCommutable = 0> {
  let Predicates = [prd] in
  defm Z : avx512_icmp_packed_rmb<opc, OpcodeStr, OpNode, sched.ZMM,
                                  VTInfo.info512, IsCommutable>, EVEX_V512;

  let Predicates = [prd, HasVLX] in {
    defm Z256 : avx512_icmp_packed_rmb<opc, OpcodeStr, OpNode, sched.YMM,
                                       VTInfo.info256, IsCommutable>, EVEX_V256;
    defm Z128 : avx512_icmp_packed_rmb<opc, OpcodeStr, OpNode, sched.XMM,
                                       VTInfo.info128, IsCommutable>, EVEX_V128;
  }
}

// This fragment treats X86cmpm as commutable to help match loads in both
// operands for PCMPEQ.
def X86setcc_commute : SDNode<"ISD::SETCC", SDTSetCC, [SDNPCommutative]>;
def X86pcmpeqm_c : PatFrag<(ops node:$src1, node:$src2),
                           (X86setcc_commute node:$src1, node:$src2, SETEQ)>;
def X86pcmpgtm : PatFrag<(ops node:$src1, node:$src2),
                         (setcc node:$src1, node:$src2, SETGT)>;

// AddedComplexity is needed because the explicit SETEQ/SETGT CondCode doesn't
// increase the pattern complexity the way an immediate would.
let AddedComplexity = 2 in {
// FIXME: Is there a better scheduler class for VPCMP?
defm VPCMPEQB : avx512_icmp_packed_vl<0x74, "vpcmpeqb", X86pcmpeqm_c,
                      SchedWriteVecALU, avx512vl_i8_info, HasBWI, 1>,
                EVEX_CD8<8, CD8VF>, VEX_WIG;

defm VPCMPEQW : avx512_icmp_packed_vl<0x75, "vpcmpeqw", X86pcmpeqm_c,
                      SchedWriteVecALU, avx512vl_i16_info, HasBWI, 1>,
                EVEX_CD8<16, CD8VF>, VEX_WIG;

defm VPCMPEQD : avx512_icmp_packed_rmb_vl<0x76, "vpcmpeqd", X86pcmpeqm_c,
                      SchedWriteVecALU, avx512vl_i32_info, HasAVX512, 1>,
                EVEX_CD8<32, CD8VF>;

defm VPCMPEQQ : avx512_icmp_packed_rmb_vl<0x29, "vpcmpeqq", X86pcmpeqm_c,
                      SchedWriteVecALU, avx512vl_i64_info, HasAVX512, 1>,
                T8PD, VEX_W, EVEX_CD8<64, CD8VF>;

defm VPCMPGTB : avx512_icmp_packed_vl<0x64, "vpcmpgtb", X86pcmpgtm,
                      SchedWriteVecALU, avx512vl_i8_info, HasBWI>,
                EVEX_CD8<8, CD8VF>, VEX_WIG;

defm VPCMPGTW : avx512_icmp_packed_vl<0x65, "vpcmpgtw", X86pcmpgtm,
                      SchedWriteVecALU, avx512vl_i16_info, HasBWI>,
                EVEX_CD8<16, CD8VF>, VEX_WIG;

defm VPCMPGTD : avx512_icmp_packed_rmb_vl<0x66, "vpcmpgtd", X86pcmpgtm,
                      SchedWriteVecALU, avx512vl_i32_info, HasAVX512>,
                EVEX_CD8<32, CD8VF>;

defm VPCMPGTQ : avx512_icmp_packed_rmb_vl<0x37, "vpcmpgtq", X86pcmpgtm,
                      SchedWriteVecALU, avx512vl_i64_info, HasAVX512>,
                T8PD, VEX_W, EVEX_CD8<64, CD8VF>;
}

multiclass avx512_icmp_cc<bits<8> opc, string Suffix, PatFrag Frag,
                          PatFrag CommFrag, X86FoldableSchedWrite sched,
                          X86VectorVTInfo _, string Name> {
  let isCommutable = 1 in
  def rri : AVX512AIi8<opc, MRMSrcReg,
             (outs _.KRC:$dst), (ins _.RC:$src1, _.RC:$src2, AVX512ICC:$cc),
             !strconcat("vpcmp${cc}", Suffix,
                        "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
             [(set _.KRC:$dst, (_.KVT (Frag:$cc (_.VT _.RC:$src1),
                                                (_.VT _.RC:$src2),
                                                cond)))]>,
             EVEX_4V, Sched<[sched]>;
  def rmi : AVX512AIi8<opc, MRMSrcMem,
             (outs _.KRC:$dst), (ins _.RC:$src1, _.MemOp:$src2, AVX512ICC:$cc),
             !strconcat("vpcmp${cc}", Suffix,
                        "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
             [(set _.KRC:$dst, (_.KVT
                                (Frag:$cc
                                 (_.VT _.RC:$src1),
                                 (_.VT (_.LdFrag addr:$src2)),
                                 cond)))]>,
             EVEX_4V, Sched<[sched.Folded, sched.ReadAfterFold]>;
  let isCommutable = 1 in
  def rrik : AVX512AIi8<opc, MRMSrcReg,
              (outs _.KRC:$dst), (ins _.KRCWM:$mask, _.RC:$src1, _.RC:$src2,
                                      AVX512ICC:$cc),
              !strconcat("vpcmp${cc}", Suffix,
                         "\t{$src2, $src1, $dst {${mask}}|",
                         "$dst {${mask}}, $src1, $src2}"),
              [(set _.KRC:$dst, (and _.KRCWM:$mask,
                                     (_.KVT (Frag:$cc (_.VT _.RC:$src1),
                                                      (_.VT _.RC:$src2),
                                                      cond))))]>,
              EVEX_4V, EVEX_K, Sched<[sched]>;
  def rmik : AVX512AIi8<opc, MRMSrcMem,
              (outs _.KRC:$dst), (ins _.KRCWM:$mask, _.RC:$src1, _.MemOp:$src2,
                                    AVX512ICC:$cc),
              !strconcat("vpcmp${cc}", Suffix,
                         "\t{$src2, $src1, $dst {${mask}}|",
                         "$dst {${mask}}, $src1, $src2}"),
              [(set _.KRC:$dst, (and _.KRCWM:$mask,
                                     (_.KVT
                                      (Frag:$cc
                                       (_.VT _.RC:$src1),
                                       (_.VT (_.LdFrag addr:$src2)),
                                       cond))))]>,
              EVEX_4V, EVEX_K, Sched<[sched.Folded, sched.ReadAfterFold]>;

  // Accept explicit immediate argument form instead of comparison code.
  let isAsmParserOnly = 1, hasSideEffects = 0 in {
    def rri_alt : AVX512AIi8<opc, MRMSrcReg,
               (outs _.KRC:$dst), (ins _.RC:$src1, _.RC:$src2, u8imm:$cc),
               !strconcat("vpcmp", Suffix, "\t{$cc, $src2, $src1, $dst|",
                          "$dst, $src1, $src2, $cc}"), []>,
               EVEX_4V, Sched<[sched]>, NotMemoryFoldable;
    let mayLoad = 1 in
    def rmi_alt : AVX512AIi8<opc, MRMSrcMem,
               (outs _.KRC:$dst), (ins _.RC:$src1, _.MemOp:$src2, u8imm:$cc),
               !strconcat("vpcmp", Suffix, "\t{$cc, $src2, $src1, $dst|",
                          "$dst, $src1, $src2, $cc}"), []>,
               EVEX_4V, Sched<[sched.Folded, sched.ReadAfterFold]>, NotMemoryFoldable;
    def rrik_alt : AVX512AIi8<opc, MRMSrcReg,
               (outs _.KRC:$dst), (ins _.KRCWM:$mask, _.RC:$src1, _.RC:$src2,
                                       u8imm:$cc),
               !strconcat("vpcmp", Suffix,
                          "\t{$cc, $src2, $src1, $dst {${mask}}|",
                          "$dst {${mask}}, $src1, $src2, $cc}"), []>,
               EVEX_4V, EVEX_K, Sched<[sched]>, NotMemoryFoldable;
    let mayLoad = 1 in
    def rmik_alt : AVX512AIi8<opc, MRMSrcMem,
               (outs _.KRC:$dst), (ins _.KRCWM:$mask, _.RC:$src1, _.MemOp:$src2,
                                       u8imm:$cc),
               !strconcat("vpcmp", Suffix,
                          "\t{$cc, $src2, $src1, $dst {${mask}}|",
                          "$dst {${mask}}, $src1, $src2, $cc}"), []>,
               EVEX_4V, EVEX_K, Sched<[sched.Folded, sched.ReadAfterFold]>,
               NotMemoryFoldable;
  }

  def : Pat<(_.KVT (CommFrag:$cc (_.LdFrag addr:$src2),
                                 (_.VT _.RC:$src1), cond)),
            (!cast<Instruction>(Name#_.ZSuffix#"rmi")
             _.RC:$src1, addr:$src2, (CommFrag.OperandTransform $cc))>;

  def : Pat<(and _.KRCWM:$mask,
                 (_.KVT (CommFrag:$cc (_.LdFrag addr:$src2),
                                      (_.VT _.RC:$src1), cond))),
            (!cast<Instruction>(Name#_.ZSuffix#"rmik")
             _.KRCWM:$mask, _.RC:$src1, addr:$src2,
             (CommFrag.OperandTransform $cc))>;
}

multiclass avx512_icmp_cc_rmb<bits<8> opc, string Suffix, PatFrag Frag,
                              PatFrag CommFrag, X86FoldableSchedWrite sched,
                              X86VectorVTInfo _, string Name> :
           avx512_icmp_cc<opc, Suffix, Frag, CommFrag, sched, _, Name> {
  def rmib : AVX512AIi8<opc, MRMSrcMem,
             (outs _.KRC:$dst), (ins _.RC:$src1, _.ScalarMemOp:$src2,
                                     AVX512ICC:$cc),
             !strconcat("vpcmp${cc}", Suffix,
                        "\t{${src2}", _.BroadcastStr, ", $src1, $dst|",
                        "$dst, $src1, ${src2}", _.BroadcastStr, "}"),
             [(set _.KRC:$dst, (_.KVT (Frag:$cc
                                       (_.VT _.RC:$src1),
                                       (X86VBroadcast
                                        (_.ScalarLdFrag addr:$src2)),
                                       cond)))]>,
             EVEX_4V, EVEX_B, Sched<[sched.Folded, sched.ReadAfterFold]>;
  def rmibk : AVX512AIi8<opc, MRMSrcMem,
              (outs _.KRC:$dst), (ins _.KRCWM:$mask, _.RC:$src1,
                                       _.ScalarMemOp:$src2, AVX512ICC:$cc),
              !strconcat("vpcmp${cc}", Suffix,
                       "\t{${src2}", _.BroadcastStr, ", $src1, $dst {${mask}}|",
                       "$dst {${mask}}, $src1, ${src2}", _.BroadcastStr, "}"),
              [(set _.KRC:$dst, (and _.KRCWM:$mask,
                                     (_.KVT (Frag:$cc
                                             (_.VT _.RC:$src1),
                                             (X86VBroadcast
                                              (_.ScalarLdFrag addr:$src2)),
                                             cond))))]>,
              EVEX_4V, EVEX_K, EVEX_B, Sched<[sched.Folded, sched.ReadAfterFold]>;

  // Accept explicit immediate argument form instead of comparison code.
  let isAsmParserOnly = 1, hasSideEffects = 0, mayLoad = 1 in {
    def rmib_alt : AVX512AIi8<opc, MRMSrcMem,
               (outs _.KRC:$dst), (ins _.RC:$src1, _.ScalarMemOp:$src2,
                                       u8imm:$cc),
               !strconcat("vpcmp", Suffix,
                   "\t{$cc, ${src2}", _.BroadcastStr, ", $src1, $dst|",
                   "$dst, $src1, ${src2}", _.BroadcastStr, ", $cc}"), []>,
               EVEX_4V, EVEX_B, Sched<[sched.Folded, sched.ReadAfterFold]>,
               NotMemoryFoldable;
    def rmibk_alt : AVX512AIi8<opc, MRMSrcMem,
               (outs _.KRC:$dst), (ins _.KRCWM:$mask, _.RC:$src1,
                                       _.ScalarMemOp:$src2, u8imm:$cc),
               !strconcat("vpcmp", Suffix,
                  "\t{$cc, ${src2}", _.BroadcastStr, ", $src1, $dst {${mask}}|",
                  "$dst {${mask}}, $src1, ${src2}", _.BroadcastStr, ", $cc}"), []>,
               EVEX_4V, EVEX_K, EVEX_B, Sched<[sched.Folded, sched.ReadAfterFold]>,
               NotMemoryFoldable;
  }

  def : Pat<(_.KVT (CommFrag:$cc (X86VBroadcast (_.ScalarLdFrag addr:$src2)),
                    (_.VT _.RC:$src1), cond)),
            (!cast<Instruction>(Name#_.ZSuffix#"rmib")
             _.RC:$src1, addr:$src2, (CommFrag.OperandTransform $cc))>;

  def : Pat<(and _.KRCWM:$mask,
                 (_.KVT (CommFrag:$cc (X86VBroadcast
                                       (_.ScalarLdFrag addr:$src2)),
                                      (_.VT _.RC:$src1), cond))),
            (!cast<Instruction>(Name#_.ZSuffix#"rmibk")
             _.KRCWM:$mask, _.RC:$src1, addr:$src2,
             (CommFrag.OperandTransform $cc))>;
}

multiclass avx512_icmp_cc_vl<bits<8> opc, string Suffix, PatFrag Frag,
                             PatFrag CommFrag, X86SchedWriteWidths sched,
                             AVX512VLVectorVTInfo VTInfo, Predicate prd> {
  let Predicates = [prd] in
  defm Z : avx512_icmp_cc<opc, Suffix, Frag, CommFrag, sched.ZMM,
                          VTInfo.info512, NAME>, EVEX_V512;

  let Predicates = [prd, HasVLX] in {
    defm Z256 : avx512_icmp_cc<opc, Suffix, Frag, CommFrag, sched.YMM,
                               VTInfo.info256, NAME>, EVEX_V256;
    defm Z128 : avx512_icmp_cc<opc, Suffix, Frag, CommFrag, sched.XMM,
                               VTInfo.info128, NAME>, EVEX_V128;
  }
}

multiclass avx512_icmp_cc_rmb_vl<bits<8> opc, string Suffix, PatFrag Frag,
                                 PatFrag CommFrag, X86SchedWriteWidths sched,
                                 AVX512VLVectorVTInfo VTInfo, Predicate prd> {
  let Predicates = [prd] in
  defm Z : avx512_icmp_cc_rmb<opc, Suffix, Frag, CommFrag, sched.ZMM,
                              VTInfo.info512, NAME>, EVEX_V512;

  let Predicates = [prd, HasVLX] in {
    defm Z256 : avx512_icmp_cc_rmb<opc, Suffix, Frag, CommFrag, sched.YMM,
                                    VTInfo.info256, NAME>, EVEX_V256;
    defm Z128 : avx512_icmp_cc_rmb<opc, Suffix, Frag, CommFrag, sched.XMM,
                                   VTInfo.info128, NAME>, EVEX_V128;
  }
}

def X86pcmpm_imm : SDNodeXForm<setcc, [{
  ISD::CondCode CC = cast<CondCodeSDNode>(N->getOperand(2))->get();
  uint8_t SSECC = X86::getVPCMPImmForCond(CC);
  return getI8Imm(SSECC, SDLoc(N));
}]>;

// Swapped operand version of the above.
def X86pcmpm_imm_commute : SDNodeXForm<setcc, [{
  ISD::CondCode CC = cast<CondCodeSDNode>(N->getOperand(2))->get();
  uint8_t SSECC = X86::getVPCMPImmForCond(CC);
  SSECC = X86::getSwappedVPCMPImm(SSECC);
  return getI8Imm(SSECC, SDLoc(N));
}]>;

def X86pcmpm : PatFrag<(ops node:$src1, node:$src2, node:$cc),
                       (setcc node:$src1, node:$src2, node:$cc), [{
  ISD::CondCode CC = cast<CondCodeSDNode>(N->getOperand(2))->get();
  return !ISD::isUnsignedIntSetCC(CC);
}], X86pcmpm_imm>;

// Same as above, but commutes immediate. Use for load folding.
def X86pcmpm_commute : PatFrag<(ops node:$src1, node:$src2, node:$cc),
                               (setcc node:$src1, node:$src2, node:$cc), [{
  ISD::CondCode CC = cast<CondCodeSDNode>(N->getOperand(2))->get();
  return !ISD::isUnsignedIntSetCC(CC);
}], X86pcmpm_imm_commute>;

def X86pcmpum : PatFrag<(ops node:$src1, node:$src2, node:$cc),
                        (setcc node:$src1, node:$src2, node:$cc), [{
  ISD::CondCode CC = cast<CondCodeSDNode>(N->getOperand(2))->get();
  return ISD::isUnsignedIntSetCC(CC);
}], X86pcmpm_imm>;

// Same as above, but commutes immediate. Use for load folding.
def X86pcmpum_commute : PatFrag<(ops node:$src1, node:$src2, node:$cc),
                                (setcc node:$src1, node:$src2, node:$cc), [{
  ISD::CondCode CC = cast<CondCodeSDNode>(N->getOperand(2))->get();
  return ISD::isUnsignedIntSetCC(CC);
}], X86pcmpm_imm_commute>;

// FIXME: Is there a better scheduler class for VPCMP/VPCMPU?
defm VPCMPB : avx512_icmp_cc_vl<0x3F, "b", X86pcmpm, X86pcmpm_commute,
                                SchedWriteVecALU, avx512vl_i8_info, HasBWI>,
                                EVEX_CD8<8, CD8VF>;
defm VPCMPUB : avx512_icmp_cc_vl<0x3E, "ub", X86pcmpum, X86pcmpum_commute,
                                 SchedWriteVecALU, avx512vl_i8_info, HasBWI>,
                                 EVEX_CD8<8, CD8VF>;

defm VPCMPW : avx512_icmp_cc_vl<0x3F, "w", X86pcmpm, X86pcmpm_commute,
                                SchedWriteVecALU, avx512vl_i16_info, HasBWI>,
                                VEX_W, EVEX_CD8<16, CD8VF>;
defm VPCMPUW : avx512_icmp_cc_vl<0x3E, "uw", X86pcmpum, X86pcmpum_commute,
                                 SchedWriteVecALU, avx512vl_i16_info, HasBWI>,
                                 VEX_W, EVEX_CD8<16, CD8VF>;

defm VPCMPD : avx512_icmp_cc_rmb_vl<0x1F, "d", X86pcmpm, X86pcmpm_commute,
                                    SchedWriteVecALU, avx512vl_i32_info,
                                    HasAVX512>, EVEX_CD8<32, CD8VF>;
defm VPCMPUD : avx512_icmp_cc_rmb_vl<0x1E, "ud", X86pcmpum, X86pcmpum_commute,
                                     SchedWriteVecALU, avx512vl_i32_info,
                                     HasAVX512>, EVEX_CD8<32, CD8VF>;

defm VPCMPQ : avx512_icmp_cc_rmb_vl<0x1F, "q", X86pcmpm, X86pcmpm_commute,
                                    SchedWriteVecALU, avx512vl_i64_info,
                                    HasAVX512>, VEX_W, EVEX_CD8<64, CD8VF>;
defm VPCMPUQ : avx512_icmp_cc_rmb_vl<0x1E, "uq", X86pcmpum, X86pcmpum_commute,
                                     SchedWriteVecALU, avx512vl_i64_info,
                                     HasAVX512>, VEX_W, EVEX_CD8<64, CD8VF>;

multiclass avx512_vcmp_common<X86FoldableSchedWrite sched, X86VectorVTInfo _,
                              string Name> {
  defm  rri  : AVX512_maskable_cmp<0xC2, MRMSrcReg, _,
                   (outs _.KRC:$dst), (ins _.RC:$src1, _.RC:$src2,AVXCC:$cc),
                   "vcmp${cc}"#_.Suffix,
                   "$src2, $src1", "$src1, $src2",
                   (X86cmpm (_.VT _.RC:$src1),
                         (_.VT _.RC:$src2),
                           imm:$cc), 1>,
                   Sched<[sched]>;

  defm  rmi  : AVX512_maskable_cmp<0xC2, MRMSrcMem, _,
                (outs _.KRC:$dst),(ins _.RC:$src1, _.MemOp:$src2, AVXCC:$cc),
                "vcmp${cc}"#_.Suffix,
                "$src2, $src1", "$src1, $src2",
                (X86cmpm (_.VT _.RC:$src1),
                        (_.VT (_.LdFrag addr:$src2)),
                        imm:$cc)>,
                Sched<[sched.Folded, sched.ReadAfterFold]>;

  defm  rmbi : AVX512_maskable_cmp<0xC2, MRMSrcMem, _,
                (outs _.KRC:$dst),
                (ins _.RC:$src1, _.ScalarMemOp:$src2, AVXCC:$cc),
                "vcmp${cc}"#_.Suffix,
                "${src2}"##_.BroadcastStr##", $src1",
                "$src1, ${src2}"##_.BroadcastStr,
                (X86cmpm (_.VT _.RC:$src1),
                        (_.VT (X86VBroadcast(_.ScalarLdFrag addr:$src2))),
                        imm:$cc)>,
                EVEX_B, Sched<[sched.Folded, sched.ReadAfterFold]>;
  // Accept explicit immediate argument form instead of comparison code.
  let isAsmParserOnly = 1, hasSideEffects = 0 in {
    defm  rri_alt : AVX512_maskable_cmp_alt<0xC2, MRMSrcReg, _,
                         (outs _.KRC:$dst),
                         (ins _.RC:$src1, _.RC:$src2, u8imm:$cc),
                         "vcmp"#_.Suffix,
                         "$cc, $src2, $src1", "$src1, $src2, $cc">,
                         Sched<[sched]>, NotMemoryFoldable;

    let mayLoad = 1 in {
      defm rmi_alt : AVX512_maskable_cmp_alt<0xC2, MRMSrcMem, _,
                             (outs _.KRC:$dst),
                             (ins _.RC:$src1, _.MemOp:$src2, u8imm:$cc),
                             "vcmp"#_.Suffix,
                             "$cc, $src2, $src1", "$src1, $src2, $cc">,
                             Sched<[sched.Folded, sched.ReadAfterFold]>,
                             NotMemoryFoldable;

      defm  rmbi_alt : AVX512_maskable_cmp_alt<0xC2, MRMSrcMem, _,
                         (outs _.KRC:$dst),
                         (ins _.RC:$src1, _.ScalarMemOp:$src2, u8imm:$cc),
                         "vcmp"#_.Suffix,
                         "$cc, ${src2}"##_.BroadcastStr##", $src1",
                         "$src1, ${src2}"##_.BroadcastStr##", $cc">,
                         EVEX_B, Sched<[sched.Folded, sched.ReadAfterFold]>,
                         NotMemoryFoldable;
    }
  }

  // Patterns for selecting with loads in other operand.
  def : Pat<(X86cmpm (_.LdFrag addr:$src2), (_.VT _.RC:$src1),
                     CommutableCMPCC:$cc),
            (!cast<Instruction>(Name#_.ZSuffix#"rmi") _.RC:$src1, addr:$src2,
                                                      imm:$cc)>;

  def : Pat<(and _.KRCWM:$mask, (X86cmpm (_.LdFrag addr:$src2),
                                         (_.VT _.RC:$src1),
                                         CommutableCMPCC:$cc)),
            (!cast<Instruction>(Name#_.ZSuffix#"rmik") _.KRCWM:$mask,
                                                       _.RC:$src1, addr:$src2,
                                                       imm:$cc)>;

  def : Pat<(X86cmpm (X86VBroadcast (_.ScalarLdFrag addr:$src2)),
                     (_.VT _.RC:$src1), CommutableCMPCC:$cc),
            (!cast<Instruction>(Name#_.ZSuffix#"rmbi") _.RC:$src1, addr:$src2,
                                                       imm:$cc)>;

  def : Pat<(and _.KRCWM:$mask, (X86cmpm (X86VBroadcast
                                          (_.ScalarLdFrag addr:$src2)),
                                         (_.VT _.RC:$src1),
                                         CommutableCMPCC:$cc)),
            (!cast<Instruction>(Name#_.ZSuffix#"rmbik") _.KRCWM:$mask,
                                                        _.RC:$src1, addr:$src2,
                                                        imm:$cc)>;
}

multiclass avx512_vcmp_sae<X86FoldableSchedWrite sched, X86VectorVTInfo _> {
  // comparison code form (VCMP[EQ/LT/LE/...]
  defm  rrib  : AVX512_maskable_cmp<0xC2, MRMSrcReg, _,
                     (outs _.KRC:$dst),(ins _.RC:$src1, _.RC:$src2, AVXCC:$cc),
                     "vcmp${cc}"#_.Suffix,
                     "{sae}, $src2, $src1", "$src1, $src2, {sae}",
                     (X86cmpmRnd (_.VT _.RC:$src1),
                                    (_.VT _.RC:$src2),
                                    imm:$cc,
                                (i32 FROUND_NO_EXC))>,
                     EVEX_B, Sched<[sched]>;

  let isAsmParserOnly = 1, hasSideEffects = 0 in {
    defm  rrib_alt  : AVX512_maskable_cmp_alt<0xC2, MRMSrcReg, _,
                         (outs _.KRC:$dst),
                         (ins _.RC:$src1, _.RC:$src2, u8imm:$cc),
                         "vcmp"#_.Suffix,
                         "$cc, {sae}, $src2, $src1",
                         "$src1, $src2, {sae}, $cc">,
                         EVEX_B, Sched<[sched]>, NotMemoryFoldable;
   }
}

multiclass avx512_vcmp<X86SchedWriteWidths sched, AVX512VLVectorVTInfo _> {
  let Predicates = [HasAVX512] in {
    defm Z    : avx512_vcmp_common<sched.ZMM, _.info512, NAME>,
                avx512_vcmp_sae<sched.ZMM, _.info512>, EVEX_V512;

  }
  let Predicates = [HasAVX512,HasVLX] in {
   defm Z128 : avx512_vcmp_common<sched.XMM, _.info128, NAME>, EVEX_V128;
   defm Z256 : avx512_vcmp_common<sched.YMM, _.info256, NAME>, EVEX_V256;
  }
}

defm VCMPPD : avx512_vcmp<SchedWriteFCmp, avx512vl_f64_info>,
                          AVX512PDIi8Base, EVEX_4V, EVEX_CD8<64, CD8VF>, VEX_W;
defm VCMPPS : avx512_vcmp<SchedWriteFCmp, avx512vl_f32_info>,
                          AVX512PSIi8Base, EVEX_4V, EVEX_CD8<32, CD8VF>;

// Patterns to select fp compares with load as first operand.
let Predicates = [HasAVX512] in {
  def : Pat<(v1i1 (X86cmpms (loadf64 addr:$src2), FR64X:$src1,
                            CommutableCMPCC:$cc)),
            (VCMPSDZrm FR64X:$src1, addr:$src2, imm:$cc)>;

  def : Pat<(v1i1 (X86cmpms (loadf32 addr:$src2), FR32X:$src1,
                            CommutableCMPCC:$cc)),
            (VCMPSSZrm FR32X:$src1, addr:$src2, imm:$cc)>;
}

// ----------------------------------------------------------------
// FPClass
//handle fpclass instruction  mask =  op(reg_scalar,imm)
//                                    op(mem_scalar,imm)
multiclass avx512_scalar_fpclass<bits<8> opc, string OpcodeStr, SDNode OpNode,
                                 X86FoldableSchedWrite sched, X86VectorVTInfo _,
                                 Predicate prd> {
  let Predicates = [prd], ExeDomain = _.ExeDomain in {
      def rr : AVX512<opc, MRMSrcReg, (outs _.KRC:$dst),
                      (ins _.RC:$src1, i32u8imm:$src2),
                      OpcodeStr##_.Suffix#"\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                      [(set _.KRC:$dst,(OpNode (_.VT _.RC:$src1),
                              (i32 imm:$src2)))]>,
                      Sched<[sched]>;
      def rrk : AVX512<opc, MRMSrcReg, (outs _.KRC:$dst),
                      (ins _.KRCWM:$mask, _.RC:$src1, i32u8imm:$src2),
                      OpcodeStr##_.Suffix#
                      "\t{$src2, $src1, $dst {${mask}}|$dst {${mask}}, $src1, $src2}",
                      [(set _.KRC:$dst,(and _.KRCWM:$mask,
                                      (OpNode (_.VT _.RC:$src1),
                                      (i32 imm:$src2))))]>,
                      EVEX_K, Sched<[sched]>;
    def rm : AVX512<opc, MRMSrcMem, (outs _.KRC:$dst),
                    (ins _.IntScalarMemOp:$src1, i32u8imm:$src2),
                    OpcodeStr##_.Suffix##
                              "\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                    [(set _.KRC:$dst,
                          (OpNode _.ScalarIntMemCPat:$src1,
                                  (i32 imm:$src2)))]>,
                    Sched<[sched.Folded, sched.ReadAfterFold]>;
    def rmk : AVX512<opc, MRMSrcMem, (outs _.KRC:$dst),
                    (ins _.KRCWM:$mask, _.IntScalarMemOp:$src1, i32u8imm:$src2),
                    OpcodeStr##_.Suffix##
                    "\t{$src2, $src1, $dst {${mask}}|$dst {${mask}}, $src1, $src2}",
                    [(set _.KRC:$dst,(and _.KRCWM:$mask,
                        (OpNode _.ScalarIntMemCPat:$src1,
                            (i32 imm:$src2))))]>,
                    EVEX_K, Sched<[sched.Folded, sched.ReadAfterFold]>;
  }
}

//handle fpclass instruction mask = fpclass(reg_vec, reg_vec, imm)
//                                  fpclass(reg_vec, mem_vec, imm)
//                                  fpclass(reg_vec, broadcast(eltVt), imm)
multiclass avx512_vector_fpclass<bits<8> opc, string OpcodeStr, SDNode OpNode,
                                 X86FoldableSchedWrite sched, X86VectorVTInfo _,
                                 string mem, string broadcast>{
  let ExeDomain = _.ExeDomain in {
  def rr : AVX512<opc, MRMSrcReg, (outs _.KRC:$dst),
                      (ins _.RC:$src1, i32u8imm:$src2),
                      OpcodeStr##_.Suffix#"\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                      [(set _.KRC:$dst,(OpNode (_.VT _.RC:$src1),
                                       (i32 imm:$src2)))]>,
                      Sched<[sched]>;
  def rrk : AVX512<opc, MRMSrcReg, (outs _.KRC:$dst),
                      (ins _.KRCWM:$mask, _.RC:$src1, i32u8imm:$src2),
                      OpcodeStr##_.Suffix#
                      "\t{$src2, $src1, $dst {${mask}}|$dst {${mask}}, $src1, $src2}",
                      [(set _.KRC:$dst,(and _.KRCWM:$mask,
                                       (OpNode (_.VT _.RC:$src1),
                                       (i32 imm:$src2))))]>,
                      EVEX_K, Sched<[sched]>;
  def rm : AVX512<opc, MRMSrcMem, (outs _.KRC:$dst),
                    (ins _.MemOp:$src1, i32u8imm:$src2),
                    OpcodeStr##_.Suffix##mem#
                    "\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                    [(set _.KRC:$dst,(OpNode
                                     (_.VT (_.LdFrag addr:$src1)),
                                     (i32 imm:$src2)))]>,
                    Sched<[sched.Folded, sched.ReadAfterFold]>;
  def rmk : AVX512<opc, MRMSrcMem, (outs _.KRC:$dst),
                    (ins _.KRCWM:$mask, _.MemOp:$src1, i32u8imm:$src2),
                    OpcodeStr##_.Suffix##mem#
                    "\t{$src2, $src1, $dst {${mask}}|$dst {${mask}}, $src1, $src2}",
                    [(set _.KRC:$dst, (and _.KRCWM:$mask, (OpNode
                                  (_.VT (_.LdFrag addr:$src1)),
                                  (i32 imm:$src2))))]>,
                    EVEX_K, Sched<[sched.Folded, sched.ReadAfterFold]>;
  def rmb : AVX512<opc, MRMSrcMem, (outs _.KRC:$dst),
                    (ins _.ScalarMemOp:$src1, i32u8imm:$src2),
                    OpcodeStr##_.Suffix##broadcast##"\t{$src2, ${src1}"##
                                      _.BroadcastStr##", $dst|$dst, ${src1}"
                                                  ##_.BroadcastStr##", $src2}",
                    [(set _.KRC:$dst,(OpNode
                                     (_.VT (X86VBroadcast
                                           (_.ScalarLdFrag addr:$src1))),
                                     (i32 imm:$src2)))]>,
                    EVEX_B, Sched<[sched.Folded, sched.ReadAfterFold]>;
  def rmbk : AVX512<opc, MRMSrcMem, (outs _.KRC:$dst),
                    (ins _.KRCWM:$mask, _.ScalarMemOp:$src1, i32u8imm:$src2),
                    OpcodeStr##_.Suffix##broadcast##"\t{$src2, ${src1}"##
                          _.BroadcastStr##", $dst {${mask}}|$dst {${mask}}, ${src1}"##
                                                   _.BroadcastStr##", $src2}",
                    [(set _.KRC:$dst,(and _.KRCWM:$mask, (OpNode
                                     (_.VT (X86VBroadcast
                                           (_.ScalarLdFrag addr:$src1))),
                                     (i32 imm:$src2))))]>,
                    EVEX_B, EVEX_K,  Sched<[sched.Folded, sched.ReadAfterFold]>;
  }
}

multiclass avx512_vector_fpclass_all<string OpcodeStr, AVX512VLVectorVTInfo _,
                                     bits<8> opc, SDNode OpNode,
                                     X86SchedWriteWidths sched, Predicate prd,
                                     string broadcast>{
  let Predicates = [prd] in {
    defm Z    : avx512_vector_fpclass<opc, OpcodeStr, OpNode, sched.ZMM,
                                      _.info512, "{z}", broadcast>, EVEX_V512;
  }
  let Predicates = [prd, HasVLX] in {
    defm Z128 : avx512_vector_fpclass<opc, OpcodeStr, OpNode, sched.XMM,
                                      _.info128, "{x}", broadcast>, EVEX_V128;
    defm Z256 : avx512_vector_fpclass<opc, OpcodeStr, OpNode, sched.YMM,
                                      _.info256, "{y}", broadcast>, EVEX_V256;
  }
}

multiclass avx512_fp_fpclass_all<string OpcodeStr, bits<8> opcVec,
                                 bits<8> opcScalar, SDNode VecOpNode,
                                 SDNode ScalarOpNode, X86SchedWriteWidths sched,
                                 Predicate prd> {
  defm PS : avx512_vector_fpclass_all<OpcodeStr,  avx512vl_f32_info, opcVec,
                                      VecOpNode, sched, prd, "{l}">,
                                      EVEX_CD8<32, CD8VF>;
  defm PD : avx512_vector_fpclass_all<OpcodeStr,  avx512vl_f64_info, opcVec,
                                      VecOpNode, sched, prd, "{q}">,
                                      EVEX_CD8<64, CD8VF> , VEX_W;
  defm SSZ : avx512_scalar_fpclass<opcScalar, OpcodeStr, ScalarOpNode,
                                   sched.Scl, f32x_info, prd>,
                                   EVEX_CD8<32, CD8VT1>;
  defm SDZ : avx512_scalar_fpclass<opcScalar, OpcodeStr, ScalarOpNode,
                                   sched.Scl, f64x_info, prd>,
                                   EVEX_CD8<64, CD8VT1>, VEX_W;
}

defm VFPCLASS : avx512_fp_fpclass_all<"vfpclass", 0x66, 0x67, X86Vfpclass,
                                      X86Vfpclasss, SchedWriteFCmp, HasDQI>,
                                      AVX512AIi8Base, EVEX;

//-----------------------------------------------------------------
// Mask register copy, including
// - copy between mask registers
// - load/store mask registers
// - copy from GPR to mask register and vice versa
//
multiclass avx512_mask_mov<bits<8> opc_kk, bits<8> opc_km, bits<8> opc_mk,
                         string OpcodeStr, RegisterClass KRC,
                         ValueType vvt, X86MemOperand x86memop> {
  let isMoveReg = 1, hasSideEffects = 0, SchedRW = [WriteMove] in
  def kk : I<opc_kk, MRMSrcReg, (outs KRC:$dst), (ins KRC:$src),
             !strconcat(OpcodeStr, "\t{$src, $dst|$dst, $src}"), []>,
             Sched<[WriteMove]>;
  def km : I<opc_km, MRMSrcMem, (outs KRC:$dst), (ins x86memop:$src),
             !strconcat(OpcodeStr, "\t{$src, $dst|$dst, $src}"),
             [(set KRC:$dst, (vvt (load addr:$src)))]>,
             Sched<[WriteLoad]>;
  def mk : I<opc_mk, MRMDestMem, (outs), (ins x86memop:$dst, KRC:$src),
             !strconcat(OpcodeStr, "\t{$src, $dst|$dst, $src}"),
             [(store KRC:$src, addr:$dst)]>,
             Sched<[WriteStore]>;
}

multiclass avx512_mask_mov_gpr<bits<8> opc_kr, bits<8> opc_rk,
                             string OpcodeStr,
                             RegisterClass KRC, RegisterClass GRC> {
  let hasSideEffects = 0 in {
    def kr : I<opc_kr, MRMSrcReg, (outs KRC:$dst), (ins GRC:$src),
               !strconcat(OpcodeStr, "\t{$src, $dst|$dst, $src}"), []>,
               Sched<[WriteMove]>;
    def rk : I<opc_rk, MRMSrcReg, (outs GRC:$dst), (ins KRC:$src),
               !strconcat(OpcodeStr, "\t{$src, $dst|$dst, $src}"), []>,
               Sched<[WriteMove]>;
  }
}

let Predicates = [HasDQI] in
  defm KMOVB : avx512_mask_mov<0x90, 0x90, 0x91, "kmovb", VK8, v8i1, i8mem>,
               avx512_mask_mov_gpr<0x92, 0x93, "kmovb", VK8, GR32>,
               VEX, PD;

let Predicates = [HasAVX512] in
  defm KMOVW : avx512_mask_mov<0x90, 0x90, 0x91, "kmovw", VK16, v16i1, i16mem>,
               avx512_mask_mov_gpr<0x92, 0x93, "kmovw", VK16, GR32>,
               VEX, PS;

let Predicates = [HasBWI] in {
  defm KMOVD : avx512_mask_mov<0x90, 0x90, 0x91, "kmovd", VK32, v32i1,i32mem>,
               VEX, PD, VEX_W;
  defm KMOVD : avx512_mask_mov_gpr<0x92, 0x93, "kmovd", VK32, GR32>,
               VEX, XD;
  defm KMOVQ : avx512_mask_mov<0x90, 0x90, 0x91, "kmovq", VK64, v64i1, i64mem>,
               VEX, PS, VEX_W;
  defm KMOVQ : avx512_mask_mov_gpr<0x92, 0x93, "kmovq", VK64, GR64>,
               VEX, XD, VEX_W;
}

// GR from/to mask register
def : Pat<(v16i1 (bitconvert (i16 GR16:$src))),
          (COPY_TO_REGCLASS (i32 (INSERT_SUBREG (IMPLICIT_DEF), GR16:$src, sub_16bit)), VK16)>;
def : Pat<(i16 (bitconvert (v16i1 VK16:$src))),
          (EXTRACT_SUBREG (i32 (COPY_TO_REGCLASS VK16:$src, GR32)), sub_16bit)>;

def : Pat<(v8i1 (bitconvert (i8 GR8:$src))),
          (COPY_TO_REGCLASS (i32 (INSERT_SUBREG (IMPLICIT_DEF), GR8:$src, sub_8bit)), VK8)>;
def : Pat<(i8 (bitconvert (v8i1 VK8:$src))),
          (EXTRACT_SUBREG (i32 (COPY_TO_REGCLASS VK8:$src, GR32)), sub_8bit)>;

def : Pat<(i32 (zext (i16 (bitconvert (v16i1 VK16:$src))))),
          (KMOVWrk VK16:$src)>;
def : Pat<(i32 (anyext (i16 (bitconvert (v16i1 VK16:$src))))),
          (COPY_TO_REGCLASS VK16:$src, GR32)>;

def : Pat<(i32 (zext (i8 (bitconvert (v8i1 VK8:$src))))),
          (KMOVBrk VK8:$src)>, Requires<[HasDQI]>;
def : Pat<(i32 (anyext (i8 (bitconvert (v8i1 VK8:$src))))),
          (COPY_TO_REGCLASS VK8:$src, GR32)>;

def : Pat<(v32i1 (bitconvert (i32 GR32:$src))),
          (COPY_TO_REGCLASS GR32:$src, VK32)>;
def : Pat<(i32 (bitconvert (v32i1 VK32:$src))),
          (COPY_TO_REGCLASS VK32:$src, GR32)>;
def : Pat<(v64i1 (bitconvert (i64 GR64:$src))),
          (COPY_TO_REGCLASS GR64:$src, VK64)>;
def : Pat<(i64 (bitconvert (v64i1 VK64:$src))),
          (COPY_TO_REGCLASS VK64:$src, GR64)>;

// Load/store kreg
let Predicates = [HasDQI] in {
  def : Pat<(store VK1:$src, addr:$dst),
            (KMOVBmk addr:$dst, (COPY_TO_REGCLASS VK1:$src, VK8))>;

  def : Pat<(v1i1 (load addr:$src)),
            (COPY_TO_REGCLASS (KMOVBkm addr:$src), VK1)>;
  def : Pat<(v2i1 (load addr:$src)),
            (COPY_TO_REGCLASS (KMOVBkm addr:$src), VK2)>;
  def : Pat<(v4i1 (load addr:$src)),
            (COPY_TO_REGCLASS (KMOVBkm addr:$src), VK4)>;
}

let Predicates = [HasAVX512] in {
  def : Pat<(v8i1 (bitconvert (i8 (load addr:$src)))),
            (COPY_TO_REGCLASS (MOVZX32rm8 addr:$src), VK8)>;
  def : Pat<(v16i1 (bitconvert (loadi16 addr:$src))),
            (KMOVWkm addr:$src)>;
}

def X86kextract : SDNode<"ISD::EXTRACT_VECTOR_ELT",
                         SDTypeProfile<1, 2, [SDTCisVT<0, i8>,
                                              SDTCVecEltisVT<1, i1>,
                                              SDTCisPtrTy<2>]>>;

let Predicates = [HasAVX512] in {
  multiclass operation_gpr_mask_copy_lowering<RegisterClass maskRC, ValueType maskVT> {
    def : Pat<(maskVT (scalar_to_vector GR32:$src)),
              (COPY_TO_REGCLASS GR32:$src, maskRC)>;

    def : Pat<(maskVT (scalar_to_vector GR8:$src)),
              (COPY_TO_REGCLASS (INSERT_SUBREG (i32 (IMPLICIT_DEF)), GR8:$src, sub_8bit), maskRC)>;

    def : Pat<(i8 (X86kextract maskRC:$src, (iPTR 0))),
              (EXTRACT_SUBREG (i32 (COPY_TO_REGCLASS maskRC:$src, GR32)), sub_8bit)>;

    def : Pat<(i32 (anyext (i8 (X86kextract maskRC:$src, (iPTR 0))))),
              (i32 (COPY_TO_REGCLASS maskRC:$src, GR32))>;
  }

  defm : operation_gpr_mask_copy_lowering<VK1,  v1i1>;
  defm : operation_gpr_mask_copy_lowering<VK2,  v2i1>;
  defm : operation_gpr_mask_copy_lowering<VK4,  v4i1>;
  defm : operation_gpr_mask_copy_lowering<VK8,  v8i1>;
  defm : operation_gpr_mask_copy_lowering<VK16,  v16i1>;
  defm : operation_gpr_mask_copy_lowering<VK32,  v32i1>;
  defm : operation_gpr_mask_copy_lowering<VK64,  v64i1>;

  def : Pat<(insert_subvector (v16i1 immAllZerosV),
                              (v1i1 (scalar_to_vector GR8:$src)), (iPTR 0)),
            (COPY_TO_REGCLASS
             (KMOVWkr (AND32ri8
                       (INSERT_SUBREG (i32 (IMPLICIT_DEF)), GR8:$src, sub_8bit),
                       (i32 1))), VK16)>;
}

// Mask unary operation
// - KNOT
multiclass avx512_mask_unop<bits<8> opc, string OpcodeStr,
                            RegisterClass KRC, SDPatternOperator OpNode,
                            X86FoldableSchedWrite sched, Predicate prd> {
  let Predicates = [prd] in
    def rr : I<opc, MRMSrcReg, (outs KRC:$dst), (ins KRC:$src),
               !strconcat(OpcodeStr, "\t{$src, $dst|$dst, $src}"),
               [(set KRC:$dst, (OpNode KRC:$src))]>,
               Sched<[sched]>;
}

multiclass avx512_mask_unop_all<bits<8> opc, string OpcodeStr,
                                SDPatternOperator OpNode,
                                X86FoldableSchedWrite sched> {
  defm B : avx512_mask_unop<opc, !strconcat(OpcodeStr, "b"), VK8, OpNode,
                            sched, HasDQI>, VEX, PD;
  defm W : avx512_mask_unop<opc, !strconcat(OpcodeStr, "w"), VK16, OpNode,
                            sched, HasAVX512>, VEX, PS;
  defm D : avx512_mask_unop<opc, !strconcat(OpcodeStr, "d"), VK32, OpNode,
                            sched, HasBWI>, VEX, PD, VEX_W;
  defm Q : avx512_mask_unop<opc, !strconcat(OpcodeStr, "q"), VK64, OpNode,
                            sched, HasBWI>, VEX, PS, VEX_W;
}

// TODO - do we need a X86SchedWriteWidths::KMASK type?
defm KNOT : avx512_mask_unop_all<0x44, "knot", vnot, SchedWriteVecLogic.XMM>;

// KNL does not support KMOVB, 8-bit mask is promoted to 16-bit
let Predicates = [HasAVX512, NoDQI] in
def : Pat<(vnot VK8:$src),
          (COPY_TO_REGCLASS (KNOTWrr (COPY_TO_REGCLASS VK8:$src, VK16)), VK8)>;

def : Pat<(vnot VK4:$src),
          (COPY_TO_REGCLASS (KNOTWrr (COPY_TO_REGCLASS VK4:$src, VK16)), VK4)>;
def : Pat<(vnot VK2:$src),
          (COPY_TO_REGCLASS (KNOTWrr (COPY_TO_REGCLASS VK2:$src, VK16)), VK2)>;

// Mask binary operation
// - KAND, KANDN, KOR, KXNOR, KXOR
multiclass avx512_mask_binop<bits<8> opc, string OpcodeStr,
                           RegisterClass KRC, SDPatternOperator OpNode,
                           X86FoldableSchedWrite sched, Predicate prd,
                           bit IsCommutable> {
  let Predicates = [prd], isCommutable = IsCommutable in
    def rr : I<opc, MRMSrcReg, (outs KRC:$dst), (ins KRC:$src1, KRC:$src2),
               !strconcat(OpcodeStr,
                          "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
               [(set KRC:$dst, (OpNode KRC:$src1, KRC:$src2))]>,
               Sched<[sched]>;
}

multiclass avx512_mask_binop_all<bits<8> opc, string OpcodeStr,
                                 SDPatternOperator OpNode,
                                 X86FoldableSchedWrite sched, bit IsCommutable,
                                 Predicate prdW = HasAVX512> {
  defm B : avx512_mask_binop<opc, !strconcat(OpcodeStr, "b"), VK8, OpNode,
                             sched, HasDQI, IsCommutable>, VEX_4V, VEX_L, PD;
  defm W : avx512_mask_binop<opc, !strconcat(OpcodeStr, "w"), VK16, OpNode,
                             sched, prdW, IsCommutable>, VEX_4V, VEX_L, PS;
  defm D : avx512_mask_binop<opc, !strconcat(OpcodeStr, "d"), VK32, OpNode,
                             sched, HasBWI, IsCommutable>, VEX_4V, VEX_L, VEX_W, PD;
  defm Q : avx512_mask_binop<opc, !strconcat(OpcodeStr, "q"), VK64, OpNode,
                             sched, HasBWI, IsCommutable>, VEX_4V, VEX_L, VEX_W, PS;
}

def andn : PatFrag<(ops node:$i0, node:$i1), (and (not node:$i0), node:$i1)>;
def xnor : PatFrag<(ops node:$i0, node:$i1), (not (xor node:$i0, node:$i1))>;
// These nodes use 'vnot' instead of 'not' to support vectors.
def vandn : PatFrag<(ops node:$i0, node:$i1), (and (vnot node:$i0), node:$i1)>;
def vxnor : PatFrag<(ops node:$i0, node:$i1), (vnot (xor node:$i0, node:$i1))>;

// TODO - do we need a X86SchedWriteWidths::KMASK type?
defm KAND  : avx512_mask_binop_all<0x41, "kand",  and,     SchedWriteVecLogic.XMM, 1>;
defm KOR   : avx512_mask_binop_all<0x45, "kor",   or,      SchedWriteVecLogic.XMM, 1>;
defm KXNOR : avx512_mask_binop_all<0x46, "kxnor", vxnor,   SchedWriteVecLogic.XMM, 1>;
defm KXOR  : avx512_mask_binop_all<0x47, "kxor",  xor,     SchedWriteVecLogic.XMM, 1>;
defm KANDN : avx512_mask_binop_all<0x42, "kandn", vandn,   SchedWriteVecLogic.XMM, 0>;
defm KADD  : avx512_mask_binop_all<0x4A, "kadd",  X86kadd, SchedWriteVecLogic.XMM, 1, HasDQI>;

multiclass avx512_binop_pat<SDPatternOperator VOpNode, SDPatternOperator OpNode,
                            Instruction Inst> {
  // With AVX512F, 8-bit mask is promoted to 16-bit mask,
  // for the DQI set, this type is legal and KxxxB instruction is used
  let Predicates = [NoDQI] in
  def : Pat<(VOpNode VK8:$src1, VK8:$src2),
            (COPY_TO_REGCLASS
              (Inst (COPY_TO_REGCLASS VK8:$src1, VK16),
                    (COPY_TO_REGCLASS VK8:$src2, VK16)), VK8)>;

  // All types smaller than 8 bits require conversion anyway
  def : Pat<(OpNode VK1:$src1, VK1:$src2),
        (COPY_TO_REGCLASS (Inst
                           (COPY_TO_REGCLASS VK1:$src1, VK16),
                           (COPY_TO_REGCLASS VK1:$src2, VK16)), VK1)>;
  def : Pat<(VOpNode VK2:$src1, VK2:$src2),
        (COPY_TO_REGCLASS (Inst
                           (COPY_TO_REGCLASS VK2:$src1, VK16),
                           (COPY_TO_REGCLASS VK2:$src2, VK16)), VK1)>;
  def : Pat<(VOpNode VK4:$src1, VK4:$src2),
        (COPY_TO_REGCLASS (Inst
                           (COPY_TO_REGCLASS VK4:$src1, VK16),
                           (COPY_TO_REGCLASS VK4:$src2, VK16)), VK1)>;
}

defm : avx512_binop_pat<and,   and,  KANDWrr>;
defm : avx512_binop_pat<vandn, andn, KANDNWrr>;
defm : avx512_binop_pat<or,    or,   KORWrr>;
defm : avx512_binop_pat<vxnor, xnor, KXNORWrr>;
defm : avx512_binop_pat<xor,   xor,  KXORWrr>;

// Mask unpacking
multiclass avx512_mask_unpck<string Suffix,RegisterClass KRC, ValueType VT,
                             RegisterClass KRCSrc, X86FoldableSchedWrite sched,
                             Predicate prd> {
  let Predicates = [prd] in {
    let hasSideEffects = 0 in
    def rr : I<0x4b, MRMSrcReg, (outs KRC:$dst),
               (ins KRC:$src1, KRC:$src2),
               "kunpck"#Suffix#"\t{$src2, $src1, $dst|$dst, $src1, $src2}", []>,
               VEX_4V, VEX_L, Sched<[sched]>;

    def : Pat<(VT (concat_vectors KRCSrc:$src1, KRCSrc:$src2)),
              (!cast<Instruction>(NAME##rr)
                        (COPY_TO_REGCLASS KRCSrc:$src2, KRC),
                        (COPY_TO_REGCLASS KRCSrc:$src1, KRC))>;
  }
}

defm KUNPCKBW : avx512_mask_unpck<"bw", VK16, v16i1, VK8, WriteShuffle, HasAVX512>, PD;
defm KUNPCKWD : avx512_mask_unpck<"wd", VK32, v32i1, VK16, WriteShuffle, HasBWI>, PS;
defm KUNPCKDQ : avx512_mask_unpck<"dq", VK64, v64i1, VK32, WriteShuffle, HasBWI>, PS, VEX_W;

// Mask bit testing
multiclass avx512_mask_testop<bits<8> opc, string OpcodeStr, RegisterClass KRC,
                              SDNode OpNode, X86FoldableSchedWrite sched,
                              Predicate prd> {
  let Predicates = [prd], Defs = [EFLAGS] in
    def rr : I<opc, MRMSrcReg, (outs), (ins KRC:$src1, KRC:$src2),
               !strconcat(OpcodeStr, "\t{$src2, $src1|$src1, $src2}"),
               [(set EFLAGS, (OpNode KRC:$src1, KRC:$src2))]>,
               Sched<[sched]>;
}

multiclass avx512_mask_testop_w<bits<8> opc, string OpcodeStr, SDNode OpNode,
                                X86FoldableSchedWrite sched,
                                Predicate prdW = HasAVX512> {
  defm B : avx512_mask_testop<opc, OpcodeStr#"b", VK8, OpNode, sched, HasDQI>,
                                                                VEX, PD;
  defm W : avx512_mask_testop<opc, OpcodeStr#"w", VK16, OpNode, sched, prdW>,
                                                                VEX, PS;
  defm Q : avx512_mask_testop<opc, OpcodeStr#"q", VK64, OpNode, sched, HasBWI>,
                                                                VEX, PS, VEX_W;
  defm D : avx512_mask_testop<opc, OpcodeStr#"d", VK32, OpNode, sched, HasBWI>,
                                                                VEX, PD, VEX_W;
}

// TODO - do we need a X86SchedWriteWidths::KMASK type?
defm KORTEST : avx512_mask_testop_w<0x98, "kortest", X86kortest, SchedWriteVecLogic.XMM>;
defm KTEST   : avx512_mask_testop_w<0x99, "ktest", X86ktest, SchedWriteVecLogic.XMM, HasDQI>;

// Mask shift
multiclass avx512_mask_shiftop<bits<8> opc, string OpcodeStr, RegisterClass KRC,
                               SDNode OpNode, X86FoldableSchedWrite sched> {
  let Predicates = [HasAVX512] in
    def ri : Ii8<opc, MRMSrcReg, (outs KRC:$dst), (ins KRC:$src, u8imm:$imm),
                 !strconcat(OpcodeStr,
                            "\t{$imm, $src, $dst|$dst, $src, $imm}"),
                            [(set KRC:$dst, (OpNode KRC:$src, (i8 imm:$imm)))]>,
                 Sched<[sched]>;
}

multiclass avx512_mask_shiftop_w<bits<8> opc1, bits<8> opc2, string OpcodeStr,
                                 SDNode OpNode, X86FoldableSchedWrite sched> {
  defm W : avx512_mask_shiftop<opc1, !strconcat(OpcodeStr, "w"), VK16, OpNode,
                               sched>, VEX, TAPD, VEX_W;
  let Predicates = [HasDQI] in
  defm B : avx512_mask_shiftop<opc1, !strconcat(OpcodeStr, "b"), VK8, OpNode,
                               sched>, VEX, TAPD;
  let Predicates = [HasBWI] in {
  defm Q : avx512_mask_shiftop<opc2, !strconcat(OpcodeStr, "q"), VK64, OpNode,
                               sched>, VEX, TAPD, VEX_W;
  defm D : avx512_mask_shiftop<opc2, !strconcat(OpcodeStr, "d"), VK32, OpNode,
                               sched>, VEX, TAPD;
  }
}

defm KSHIFTL : avx512_mask_shiftop_w<0x32, 0x33, "kshiftl", X86kshiftl, WriteShuffle>;
defm KSHIFTR : avx512_mask_shiftop_w<0x30, 0x31, "kshiftr", X86kshiftr, WriteShuffle>;

// Patterns for comparing 128/256-bit integer vectors using 512-bit instruction.
multiclass axv512_icmp_packed_no_vlx_lowering<PatFrag Frag, string InstStr,
                                              X86VectorVTInfo Narrow,
                                              X86VectorVTInfo Wide> {
  def : Pat<(Narrow.KVT (Frag (Narrow.VT Narrow.RC:$src1),
                              (Narrow.VT Narrow.RC:$src2))),
          (COPY_TO_REGCLASS
           (!cast<Instruction>(InstStr#"Zrr")
            (Wide.VT (INSERT_SUBREG (IMPLICIT_DEF), Narrow.RC:$src1, Narrow.SubRegIdx)),
            (Wide.VT (INSERT_SUBREG (IMPLICIT_DEF), Narrow.RC:$src2, Narrow.SubRegIdx))),
           Narrow.KRC)>;

  def : Pat<(Narrow.KVT (and Narrow.KRC:$mask,
                             (Frag (Narrow.VT Narrow.RC:$src1),
                                   (Narrow.VT Narrow.RC:$src2)))),
          (COPY_TO_REGCLASS
           (!cast<Instruction>(InstStr#"Zrrk")
            (COPY_TO_REGCLASS Narrow.KRC:$mask, Wide.KRC),
            (Wide.VT (INSERT_SUBREG (IMPLICIT_DEF), Narrow.RC:$src1, Narrow.SubRegIdx)),
            (Wide.VT (INSERT_SUBREG (IMPLICIT_DEF), Narrow.RC:$src2, Narrow.SubRegIdx))),
           Narrow.KRC)>;
}

// Patterns for comparing 128/256-bit integer vectors using 512-bit instruction.
multiclass axv512_icmp_packed_cc_no_vlx_lowering<PatFrag Frag,
                                                 string InstStr,
                                                 X86VectorVTInfo Narrow,
                                                 X86VectorVTInfo Wide> {
def : Pat<(Narrow.KVT (Frag:$cc (Narrow.VT Narrow.RC:$src1),
                                (Narrow.VT Narrow.RC:$src2), cond)),
          (COPY_TO_REGCLASS
           (!cast<Instruction>(InstStr##Zrri)
            (Wide.VT (INSERT_SUBREG (IMPLICIT_DEF), Narrow.RC:$src1, Narrow.SubRegIdx)),
            (Wide.VT (INSERT_SUBREG (IMPLICIT_DEF), Narrow.RC:$src2, Narrow.SubRegIdx)),
            (Frag.OperandTransform $cc)), Narrow.KRC)>;

def : Pat<(Narrow.KVT (and Narrow.KRC:$mask,
                           (Narrow.KVT (Frag:$cc (Narrow.VT Narrow.RC:$src1),
                                                 (Narrow.VT Narrow.RC:$src2),
                                                 cond)))),
          (COPY_TO_REGCLASS (!cast<Instruction>(InstStr##Zrrik)
           (COPY_TO_REGCLASS Narrow.KRC:$mask, Wide.KRC),
           (Wide.VT (INSERT_SUBREG (IMPLICIT_DEF), Narrow.RC:$src1, Narrow.SubRegIdx)),
           (Wide.VT (INSERT_SUBREG (IMPLICIT_DEF), Narrow.RC:$src2, Narrow.SubRegIdx)),
           (Frag.OperandTransform $cc)), Narrow.KRC)>;
}

// Same as above, but for fp types which don't use PatFrags.
multiclass axv512_cmp_packed_cc_no_vlx_lowering<SDNode OpNode, string InstStr,
                                                X86VectorVTInfo Narrow,
                                                X86VectorVTInfo Wide> {
def : Pat<(Narrow.KVT (OpNode (Narrow.VT Narrow.RC:$src1),
                              (Narrow.VT Narrow.RC:$src2), imm:$cc)),
          (COPY_TO_REGCLASS
           (!cast<Instruction>(InstStr##Zrri)
            (Wide.VT (INSERT_SUBREG (IMPLICIT_DEF), Narrow.RC:$src1, Narrow.SubRegIdx)),
            (Wide.VT (INSERT_SUBREG (IMPLICIT_DEF), Narrow.RC:$src2, Narrow.SubRegIdx)),
            imm:$cc), Narrow.KRC)>;

def : Pat<(Narrow.KVT (and Narrow.KRC:$mask,
                           (OpNode (Narrow.VT Narrow.RC:$src1),
                                   (Narrow.VT Narrow.RC:$src2), imm:$cc))),
          (COPY_TO_REGCLASS (!cast<Instruction>(InstStr##Zrrik)
           (COPY_TO_REGCLASS Narrow.KRC:$mask, Wide.KRC),
           (Wide.VT (INSERT_SUBREG (IMPLICIT_DEF), Narrow.RC:$src1, Narrow.SubRegIdx)),
           (Wide.VT (INSERT_SUBREG (IMPLICIT_DEF), Narrow.RC:$src2, Narrow.SubRegIdx)),
           imm:$cc), Narrow.KRC)>;
}

let Predicates = [HasAVX512, NoVLX] in {
  // AddedComplexity is needed because the explicit SETEQ/SETGT CondCode doesn't
  // increase the pattern complexity the way an immediate would.
  let AddedComplexity = 2 in {
  defm : axv512_icmp_packed_no_vlx_lowering<X86pcmpgtm, "VPCMPGTD", v8i32x_info, v16i32_info>;
  defm : axv512_icmp_packed_no_vlx_lowering<X86pcmpeqm_c, "VPCMPEQD", v8i32x_info, v16i32_info>;

  defm : axv512_icmp_packed_no_vlx_lowering<X86pcmpgtm, "VPCMPGTD", v4i32x_info, v16i32_info>;
  defm : axv512_icmp_packed_no_vlx_lowering<X86pcmpeqm_c, "VPCMPEQD", v4i32x_info, v16i32_info>;

  defm : axv512_icmp_packed_no_vlx_lowering<X86pcmpgtm, "VPCMPGTQ", v4i64x_info, v8i64_info>;
  defm : axv512_icmp_packed_no_vlx_lowering<X86pcmpeqm_c, "VPCMPEQQ", v4i64x_info, v8i64_info>;

  defm : axv512_icmp_packed_no_vlx_lowering<X86pcmpgtm, "VPCMPGTQ", v2i64x_info, v8i64_info>;
  defm : axv512_icmp_packed_no_vlx_lowering<X86pcmpeqm_c, "VPCMPEQQ", v2i64x_info, v8i64_info>;
  }

  defm : axv512_icmp_packed_cc_no_vlx_lowering<X86pcmpm, "VPCMPD", v8i32x_info, v16i32_info>;
  defm : axv512_icmp_packed_cc_no_vlx_lowering<X86pcmpum, "VPCMPUD", v8i32x_info, v16i32_info>;

  defm : axv512_icmp_packed_cc_no_vlx_lowering<X86pcmpm, "VPCMPD", v4i32x_info, v16i32_info>;
  defm : axv512_icmp_packed_cc_no_vlx_lowering<X86pcmpum, "VPCMPUD", v4i32x_info, v16i32_info>;

  defm : axv512_icmp_packed_cc_no_vlx_lowering<X86pcmpm, "VPCMPQ", v4i64x_info, v8i64_info>;
  defm : axv512_icmp_packed_cc_no_vlx_lowering<X86pcmpum, "VPCMPUQ", v4i64x_info, v8i64_info>;

  defm : axv512_icmp_packed_cc_no_vlx_lowering<X86pcmpm, "VPCMPQ", v2i64x_info, v8i64_info>;
  defm : axv512_icmp_packed_cc_no_vlx_lowering<X86pcmpum, "VPCMPUQ", v2i64x_info, v8i64_info>;

  defm : axv512_cmp_packed_cc_no_vlx_lowering<X86cmpm, "VCMPPS", v8f32x_info, v16f32_info>;
  defm : axv512_cmp_packed_cc_no_vlx_lowering<X86cmpm, "VCMPPS", v4f32x_info, v16f32_info>;
  defm : axv512_cmp_packed_cc_no_vlx_lowering<X86cmpm, "VCMPPD", v4f64x_info, v8f64_info>;
  defm : axv512_cmp_packed_cc_no_vlx_lowering<X86cmpm, "VCMPPD", v2f64x_info, v8f64_info>;
}

let Predicates = [HasBWI, NoVLX] in {
  // AddedComplexity is needed because the explicit SETEQ/SETGT CondCode doesn't
  // increase the pattern complexity the way an immediate would.
  let AddedComplexity = 2 in {
  defm : axv512_icmp_packed_no_vlx_lowering<X86pcmpgtm, "VPCMPGTB", v32i8x_info, v64i8_info>;
  defm : axv512_icmp_packed_no_vlx_lowering<X86pcmpeqm_c, "VPCMPEQB", v32i8x_info, v64i8_info>;

  defm : axv512_icmp_packed_no_vlx_lowering<X86pcmpgtm, "VPCMPGTB", v16i8x_info, v64i8_info>;
  defm : axv512_icmp_packed_no_vlx_lowering<X86pcmpeqm_c, "VPCMPEQB", v16i8x_info, v64i8_info>;

  defm : axv512_icmp_packed_no_vlx_lowering<X86pcmpgtm, "VPCMPGTW", v16i16x_info, v32i16_info>;
  defm : axv512_icmp_packed_no_vlx_lowering<X86pcmpeqm_c, "VPCMPEQW", v16i16x_info, v32i16_info>;

  defm : axv512_icmp_packed_no_vlx_lowering<X86pcmpgtm, "VPCMPGTW", v8i16x_info, v32i16_info>;
  defm : axv512_icmp_packed_no_vlx_lowering<X86pcmpeqm_c, "VPCMPEQW", v8i16x_info, v32i16_info>;
  }

  defm : axv512_icmp_packed_cc_no_vlx_lowering<X86pcmpm, "VPCMPB", v32i8x_info, v64i8_info>;
  defm : axv512_icmp_packed_cc_no_vlx_lowering<X86pcmpum, "VPCMPUB", v32i8x_info, v64i8_info>;

  defm : axv512_icmp_packed_cc_no_vlx_lowering<X86pcmpm, "VPCMPB", v16i8x_info, v64i8_info>;
  defm : axv512_icmp_packed_cc_no_vlx_lowering<X86pcmpum, "VPCMPUB", v16i8x_info, v64i8_info>;

  defm : axv512_icmp_packed_cc_no_vlx_lowering<X86pcmpm, "VPCMPW", v16i16x_info, v32i16_info>;
  defm : axv512_icmp_packed_cc_no_vlx_lowering<X86pcmpum, "VPCMPUW", v16i16x_info, v32i16_info>;

  defm : axv512_icmp_packed_cc_no_vlx_lowering<X86pcmpm, "VPCMPW", v8i16x_info, v32i16_info>;
  defm : axv512_icmp_packed_cc_no_vlx_lowering<X86pcmpum, "VPCMPUW", v8i16x_info, v32i16_info>;
}

// Mask setting all 0s or 1s
multiclass avx512_mask_setop<RegisterClass KRC, ValueType VT, PatFrag Val> {
  let Predicates = [HasAVX512] in
    let isReMaterializable = 1, isAsCheapAsAMove = 1, isPseudo = 1,
        SchedRW = [WriteZero] in
      def #NAME# : I<0, Pseudo, (outs KRC:$dst), (ins), "",
                     [(set KRC:$dst, (VT Val))]>;
}

multiclass avx512_mask_setop_w<PatFrag Val> {
  defm W : avx512_mask_setop<VK16, v16i1, Val>;
  defm D : avx512_mask_setop<VK32,  v32i1, Val>;
  defm Q : avx512_mask_setop<VK64, v64i1, Val>;
}

defm KSET0 : avx512_mask_setop_w<immAllZerosV>;
defm KSET1 : avx512_mask_setop_w<immAllOnesV>;

// With AVX-512 only, 8-bit mask is promoted to 16-bit mask.
let Predicates = [HasAVX512] in {
  def : Pat<(v8i1 immAllZerosV), (COPY_TO_REGCLASS (KSET0W), VK8)>;
  def : Pat<(v4i1 immAllZerosV), (COPY_TO_REGCLASS (KSET0W), VK4)>;
  def : Pat<(v2i1 immAllZerosV), (COPY_TO_REGCLASS (KSET0W), VK2)>;
  def : Pat<(v1i1 immAllZerosV), (COPY_TO_REGCLASS (KSET0W), VK1)>;
  def : Pat<(v8i1 immAllOnesV),  (COPY_TO_REGCLASS (KSET1W), VK8)>;
  def : Pat<(v4i1 immAllOnesV),  (COPY_TO_REGCLASS (KSET1W), VK4)>;
  def : Pat<(v2i1 immAllOnesV),  (COPY_TO_REGCLASS (KSET1W), VK2)>;
  def : Pat<(v1i1 immAllOnesV),  (COPY_TO_REGCLASS (KSET1W), VK1)>;
}

// Patterns for kmask insert_subvector/extract_subvector to/from index=0
multiclass operation_subvector_mask_lowering<RegisterClass subRC, ValueType subVT,
                                             RegisterClass RC, ValueType VT> {
  def : Pat<(subVT (extract_subvector (VT RC:$src), (iPTR 0))),
            (subVT (COPY_TO_REGCLASS RC:$src, subRC))>;

  def : Pat<(VT (insert_subvector undef, subRC:$src, (iPTR 0))),
            (VT (COPY_TO_REGCLASS subRC:$src, RC))>;
}
defm : operation_subvector_mask_lowering<VK1,  v1i1,  VK2,  v2i1>;
defm : operation_subvector_mask_lowering<VK1,  v1i1,  VK4,  v4i1>;
defm : operation_subvector_mask_lowering<VK1,  v1i1,  VK8,  v8i1>;
defm : operation_subvector_mask_lowering<VK1,  v1i1,  VK16, v16i1>;
defm : operation_subvector_mask_lowering<VK1,  v1i1,  VK32, v32i1>;
defm : operation_subvector_mask_lowering<VK1,  v1i1,  VK64, v64i1>;

defm : operation_subvector_mask_lowering<VK2,  v2i1,  VK4,  v4i1>;
defm : operation_subvector_mask_lowering<VK2,  v2i1,  VK8,  v8i1>;
defm : operation_subvector_mask_lowering<VK2,  v2i1,  VK16, v16i1>;
defm : operation_subvector_mask_lowering<VK2,  v2i1,  VK32, v32i1>;
defm : operation_subvector_mask_lowering<VK2,  v2i1,  VK64, v64i1>;

defm : operation_subvector_mask_lowering<VK4,  v4i1,  VK8,  v8i1>;
defm : operation_subvector_mask_lowering<VK4,  v4i1,  VK16, v16i1>;
defm : operation_subvector_mask_lowering<VK4,  v4i1,  VK32, v32i1>;
defm : operation_subvector_mask_lowering<VK4,  v4i1,  VK64, v64i1>;

defm : operation_subvector_mask_lowering<VK8,  v8i1,  VK16, v16i1>;
defm : operation_subvector_mask_lowering<VK8,  v8i1,  VK32, v32i1>;
defm : operation_subvector_mask_lowering<VK8,  v8i1,  VK64, v64i1>;

defm : operation_subvector_mask_lowering<VK16, v16i1, VK32, v32i1>;
defm : operation_subvector_mask_lowering<VK16, v16i1, VK64, v64i1>;

defm : operation_subvector_mask_lowering<VK32, v32i1, VK64, v64i1>;

//===----------------------------------------------------------------------===//
// AVX-512 - Aligned and unaligned load and store
//

multiclass avx512_load<bits<8> opc, string OpcodeStr, string Name,
                       X86VectorVTInfo _, PatFrag ld_frag, PatFrag mload,
                       X86SchedWriteMoveLS Sched, string EVEX2VEXOvrd,
                       bit NoRMPattern = 0,
                       SDPatternOperator SelectOprr = vselect> {
  let hasSideEffects = 0 in {
  let isMoveReg = 1 in
  def rr : AVX512PI<opc, MRMSrcReg, (outs _.RC:$dst), (ins _.RC:$src),
                    !strconcat(OpcodeStr, "\t{$src, $dst|$dst, $src}"), [],
                    _.ExeDomain>, EVEX, Sched<[Sched.RR]>,
                    EVEX2VEXOverride<EVEX2VEXOvrd#"rr">;
  def rrkz : AVX512PI<opc, MRMSrcReg, (outs _.RC:$dst),
                      (ins _.KRCWM:$mask,  _.RC:$src),
                      !strconcat(OpcodeStr, "\t{$src, ${dst} {${mask}} {z}|",
                       "${dst} {${mask}} {z}, $src}"),
                       [(set _.RC:$dst, (_.VT (SelectOprr _.KRCWM:$mask,
                                           (_.VT _.RC:$src),
                                           _.ImmAllZerosV)))], _.ExeDomain>,
                       EVEX, EVEX_KZ, Sched<[Sched.RR]>;

  let mayLoad = 1, canFoldAsLoad = 1, isReMaterializable = 1 in
  def rm : AVX512PI<opc, MRMSrcMem, (outs _.RC:$dst), (ins _.MemOp:$src),
                    !strconcat(OpcodeStr, "\t{$src, $dst|$dst, $src}"),
                    !if(NoRMPattern, [],
                        [(set _.RC:$dst,
                          (_.VT (ld_frag addr:$src)))]),
                    _.ExeDomain>, EVEX, Sched<[Sched.RM]>,
                    EVEX2VEXOverride<EVEX2VEXOvrd#"rm">;

  let Constraints = "$src0 = $dst", isConvertibleToThreeAddress = 1 in {
    def rrk : AVX512PI<opc, MRMSrcReg, (outs _.RC:$dst),
                      (ins _.RC:$src0, _.KRCWM:$mask, _.RC:$src1),
                      !strconcat(OpcodeStr, "\t{$src1, ${dst} {${mask}}|",
                      "${dst} {${mask}}, $src1}"),
                      [(set _.RC:$dst, (_.VT (SelectOprr _.KRCWM:$mask,
                                          (_.VT _.RC:$src1),
                                          (_.VT _.RC:$src0))))], _.ExeDomain>,
                       EVEX, EVEX_K, Sched<[Sched.RR]>;
    def rmk : AVX512PI<opc, MRMSrcMem, (outs _.RC:$dst),
                     (ins _.RC:$src0, _.KRCWM:$mask, _.MemOp:$src1),
                     !strconcat(OpcodeStr, "\t{$src1, ${dst} {${mask}}|",
                      "${dst} {${mask}}, $src1}"),
                     [(set _.RC:$dst, (_.VT
                         (vselect _.KRCWM:$mask,
                          (_.VT (ld_frag addr:$src1)),
                           (_.VT _.RC:$src0))))], _.ExeDomain>,
                     EVEX, EVEX_K, Sched<[Sched.RM]>;
  }
  def rmkz : AVX512PI<opc, MRMSrcMem, (outs _.RC:$dst),
                  (ins _.KRCWM:$mask, _.MemOp:$src),
                  OpcodeStr #"\t{$src, ${dst} {${mask}} {z}|"#
                                "${dst} {${mask}} {z}, $src}",
                  [(set _.RC:$dst, (_.VT (vselect _.KRCWM:$mask,
                    (_.VT (ld_frag addr:$src)), _.ImmAllZerosV)))],
                  _.ExeDomain>, EVEX, EVEX_KZ, Sched<[Sched.RM]>;
  }
  def : Pat<(_.VT (mload addr:$ptr, _.KRCWM:$mask, undef)),
            (!cast<Instruction>(Name#_.ZSuffix##rmkz) _.KRCWM:$mask, addr:$ptr)>;

  def : Pat<(_.VT (mload addr:$ptr, _.KRCWM:$mask, _.ImmAllZerosV)),
            (!cast<Instruction>(Name#_.ZSuffix##rmkz) _.KRCWM:$mask, addr:$ptr)>;

  def : Pat<(_.VT (mload addr:$ptr, _.KRCWM:$mask, (_.VT _.RC:$src0))),
            (!cast<Instruction>(Name#_.ZSuffix##rmk) _.RC:$src0,
             _.KRCWM:$mask, addr:$ptr)>;
}

multiclass avx512_alignedload_vl<bits<8> opc, string OpcodeStr,
                                 AVX512VLVectorVTInfo _, Predicate prd,
                                 X86SchedWriteMoveLSWidths Sched,
                                 string EVEX2VEXOvrd, bit NoRMPattern = 0> {
  let Predicates = [prd] in
  defm Z : avx512_load<opc, OpcodeStr, NAME, _.info512,
                       _.info512.AlignedLdFrag, masked_load_aligned512,
                       Sched.ZMM, "", NoRMPattern>, EVEX_V512;

  let Predicates = [prd, HasVLX] in {
  defm Z256 : avx512_load<opc, OpcodeStr, NAME, _.info256,
                          _.info256.AlignedLdFrag, masked_load_aligned256,
                          Sched.YMM, EVEX2VEXOvrd#"Y", NoRMPattern>, EVEX_V256;
  defm Z128 : avx512_load<opc, OpcodeStr, NAME, _.info128,
                          _.info128.AlignedLdFrag, masked_load_aligned128,
                          Sched.XMM, EVEX2VEXOvrd, NoRMPattern>, EVEX_V128;
  }
}

multiclass avx512_load_vl<bits<8> opc, string OpcodeStr,
                          AVX512VLVectorVTInfo _, Predicate prd,
                          X86SchedWriteMoveLSWidths Sched,
                          string EVEX2VEXOvrd, bit NoRMPattern = 0,
                          SDPatternOperator SelectOprr = vselect> {
  let Predicates = [prd] in
  defm Z : avx512_load<opc, OpcodeStr, NAME, _.info512, _.info512.LdFrag,
                       masked_load_unaligned, Sched.ZMM, "",
                       NoRMPattern, SelectOprr>, EVEX_V512;

  let Predicates = [prd, HasVLX] in {
  defm Z256 : avx512_load<opc, OpcodeStr, NAME, _.info256, _.info256.LdFrag,
                         masked_load_unaligned, Sched.YMM, EVEX2VEXOvrd#"Y",
                         NoRMPattern, SelectOprr>, EVEX_V256;
  defm Z128 : avx512_load<opc, OpcodeStr, NAME, _.info128, _.info128.LdFrag,
                         masked_load_unaligned, Sched.XMM, EVEX2VEXOvrd,
                         NoRMPattern, SelectOprr>, EVEX_V128;
  }
}

multiclass avx512_store<bits<8> opc, string OpcodeStr, string BaseName,
                        X86VectorVTInfo _, PatFrag st_frag, PatFrag mstore,
                        X86SchedWriteMoveLS Sched, string EVEX2VEXOvrd,
                        bit NoMRPattern = 0> {
  let hasSideEffects = 0, isCodeGenOnly = 1, ForceDisassemble = 1 in {
  let isMoveReg = 1 in
  def rr_REV  : AVX512PI<opc, MRMDestReg, (outs _.RC:$dst), (ins _.RC:$src),
                         OpcodeStr # "\t{$src, $dst|$dst, $src}",
                         [], _.ExeDomain>, EVEX,
                         FoldGenData<BaseName#_.ZSuffix#rr>, Sched<[Sched.RR]>,
                         EVEX2VEXOverride<EVEX2VEXOvrd#"rr_REV">;
  def rrk_REV : AVX512PI<opc, MRMDestReg, (outs  _.RC:$dst),
                         (ins _.KRCWM:$mask, _.RC:$src),
                         OpcodeStr # "\t{$src, ${dst} {${mask}}|"#
                         "${dst} {${mask}}, $src}",
                         [], _.ExeDomain>,  EVEX, EVEX_K,
                         FoldGenData<BaseName#_.ZSuffix#rrk>,
                         Sched<[Sched.RR]>;
  def rrkz_REV : AVX512PI<opc, MRMDestReg, (outs  _.RC:$dst),
                          (ins _.KRCWM:$mask, _.RC:$src),
                          OpcodeStr # "\t{$src, ${dst} {${mask}} {z}|" #
                          "${dst} {${mask}} {z}, $src}",
                          [], _.ExeDomain>, EVEX, EVEX_KZ,
                          FoldGenData<BaseName#_.ZSuffix#rrkz>,
                          Sched<[Sched.RR]>;
  }

  let hasSideEffects = 0, mayStore = 1 in
  def mr : AVX512PI<opc, MRMDestMem, (outs), (ins _.MemOp:$dst, _.RC:$src),
                    !strconcat(OpcodeStr, "\t{$src, $dst|$dst, $src}"),
                    !if(NoMRPattern, [],
                        [(st_frag (_.VT _.RC:$src), addr:$dst)]),
                    _.ExeDomain>, EVEX, Sched<[Sched.MR]>,
                    EVEX2VEXOverride<EVEX2VEXOvrd#"mr">;
  def mrk : AVX512PI<opc, MRMDestMem, (outs),
                     (ins _.MemOp:$dst, _.KRCWM:$mask, _.RC:$src),
              OpcodeStr # "\t{$src, ${dst} {${mask}}|${dst} {${mask}}, $src}",
               [], _.ExeDomain>, EVEX, EVEX_K, Sched<[Sched.MR]>,
               NotMemoryFoldable;

  def: Pat<(mstore (_.VT _.RC:$src), addr:$ptr, _.KRCWM:$mask),
           (!cast<Instruction>(BaseName#_.ZSuffix#mrk) addr:$ptr,
                                                        _.KRCWM:$mask, _.RC:$src)>;

  def : InstAlias<OpcodeStr#".s\t{$src, $dst|$dst, $src}",
                  (!cast<Instruction>(BaseName#_.ZSuffix#"rr_REV")
                   _.RC:$dst, _.RC:$src), 0>;
  def : InstAlias<OpcodeStr#".s\t{$src, ${dst} {${mask}}|${dst} {${mask}}, $src}",
                  (!cast<Instruction>(BaseName#_.ZSuffix#"rrk_REV")
                   _.RC:$dst, _.KRCWM:$mask, _.RC:$src), 0>;
  def : InstAlias<OpcodeStr#".s\t{$src, ${dst} {${mask}} {z}|${dst} {${mask}} {z}, $src}",
                  (!cast<Instruction>(BaseName#_.ZSuffix#"rrkz_REV")
                   _.RC:$dst, _.KRCWM:$mask, _.RC:$src), 0>;
}

multiclass avx512_store_vl< bits<8> opc, string OpcodeStr,
                            AVX512VLVectorVTInfo _, Predicate prd,
                            X86SchedWriteMoveLSWidths Sched,
                            string EVEX2VEXOvrd, bit NoMRPattern = 0> {
  let Predicates = [prd] in
  defm Z : avx512_store<opc, OpcodeStr, NAME, _.info512, store,
                        masked_store_unaligned, Sched.ZMM, "",
                        NoMRPattern>, EVEX_V512;
  let Predicates = [prd, HasVLX] in {
    defm Z256 : avx512_store<opc, OpcodeStr, NAME, _.info256, store,
                             masked_store_unaligned, Sched.YMM,
                             EVEX2VEXOvrd#"Y", NoMRPattern>, EVEX_V256;
    defm Z128 : avx512_store<opc, OpcodeStr, NAME, _.info128, store,
                             masked_store_unaligned, Sched.XMM, EVEX2VEXOvrd,
                             NoMRPattern>, EVEX_V128;
  }
}

multiclass avx512_alignedstore_vl<bits<8> opc, string OpcodeStr,
                                  AVX512VLVectorVTInfo _, Predicate prd,
                                  X86SchedWriteMoveLSWidths Sched,
                                  string EVEX2VEXOvrd, bit NoMRPattern = 0> {
  let Predicates = [prd] in
  defm Z : avx512_store<opc, OpcodeStr, NAME, _.info512, alignedstore,
                        masked_store_aligned512, Sched.ZMM, "",
                        NoMRPattern>, EVEX_V512;

  let Predicates = [prd, HasVLX] in {
    defm Z256 : avx512_store<opc, OpcodeStr, NAME, _.info256, alignedstore,
                             masked_store_aligned256, Sched.YMM,
                             EVEX2VEXOvrd#"Y", NoMRPattern>, EVEX_V256;
    defm Z128 : avx512_store<opc, OpcodeStr, NAME, _.info128, alignedstore,
                             masked_store_aligned128, Sched.XMM, EVEX2VEXOvrd,
                             NoMRPattern>, EVEX_V128;
  }
}

defm VMOVAPS : avx512_alignedload_vl<0x28, "vmovaps", avx512vl_f32_info,
                                     HasAVX512, SchedWriteFMoveLS, "VMOVAPS">,
               avx512_alignedstore_vl<0x29, "vmovaps", avx512vl_f32_info,
                                      HasAVX512, SchedWriteFMoveLS, "VMOVAPS">,
               PS, EVEX_CD8<32, CD8VF>;

defm VMOVAPD : avx512_alignedload_vl<0x28, "vmovapd", avx512vl_f64_info,
                                     HasAVX512, SchedWriteFMoveLS, "VMOVAPD">,
               avx512_alignedstore_vl<0x29, "vmovapd", avx512vl_f64_info,
                                      HasAVX512, SchedWriteFMoveLS, "VMOVAPD">,
               PD, VEX_W, EVEX_CD8<64, CD8VF>;

defm VMOVUPS : avx512_load_vl<0x10, "vmovups", avx512vl_f32_info, HasAVX512,
                              SchedWriteFMoveLS, "VMOVUPS", 0, null_frag>,
               avx512_store_vl<0x11, "vmovups", avx512vl_f32_info, HasAVX512,
                               SchedWriteFMoveLS, "VMOVUPS">,
                               PS, EVEX_CD8<32, CD8VF>;

defm VMOVUPD : avx512_load_vl<0x10, "vmovupd", avx512vl_f64_info, HasAVX512,
                              SchedWriteFMoveLS, "VMOVUPD", 0, null_frag>,
               avx512_store_vl<0x11, "vmovupd", avx512vl_f64_info, HasAVX512,
                               SchedWriteFMoveLS, "VMOVUPD">,
               PD, VEX_W, EVEX_CD8<64, CD8VF>;

defm VMOVDQA32 : avx512_alignedload_vl<0x6F, "vmovdqa32", avx512vl_i32_info,
                                       HasAVX512, SchedWriteVecMoveLS,
                                       "VMOVDQA", 1>,
                 avx512_alignedstore_vl<0x7F, "vmovdqa32", avx512vl_i32_info,
                                        HasAVX512, SchedWriteVecMoveLS,
                                        "VMOVDQA", 1>,
                 PD, EVEX_CD8<32, CD8VF>;

defm VMOVDQA64 : avx512_alignedload_vl<0x6F, "vmovdqa64", avx512vl_i64_info,
                                       HasAVX512, SchedWriteVecMoveLS,
                                       "VMOVDQA">,
                 avx512_alignedstore_vl<0x7F, "vmovdqa64", avx512vl_i64_info,
                                        HasAVX512, SchedWriteVecMoveLS,
                                        "VMOVDQA">,
                 PD, VEX_W, EVEX_CD8<64, CD8VF>;

defm VMOVDQU8 : avx512_load_vl<0x6F, "vmovdqu8", avx512vl_i8_info, HasBWI,
                               SchedWriteVecMoveLS, "VMOVDQU", 1>,
                avx512_store_vl<0x7F, "vmovdqu8", avx512vl_i8_info, HasBWI,
                                SchedWriteVecMoveLS, "VMOVDQU", 1>,
                XD, EVEX_CD8<8, CD8VF>;

defm VMOVDQU16 : avx512_load_vl<0x6F, "vmovdqu16", avx512vl_i16_info, HasBWI,
                                SchedWriteVecMoveLS, "VMOVDQU", 1>,
                 avx512_store_vl<0x7F, "vmovdqu16", avx512vl_i16_info, HasBWI,
                                 SchedWriteVecMoveLS, "VMOVDQU", 1>,
                 XD, VEX_W, EVEX_CD8<16, CD8VF>;

defm VMOVDQU32 : avx512_load_vl<0x6F, "vmovdqu32", avx512vl_i32_info, HasAVX512,
                                SchedWriteVecMoveLS, "VMOVDQU", 1, null_frag>,
                 avx512_store_vl<0x7F, "vmovdqu32", avx512vl_i32_info, HasAVX512,
                                 SchedWriteVecMoveLS, "VMOVDQU", 1>,
                 XS, EVEX_CD8<32, CD8VF>;

defm VMOVDQU64 : avx512_load_vl<0x6F, "vmovdqu64", avx512vl_i64_info, HasAVX512,
                                SchedWriteVecMoveLS, "VMOVDQU", 0, null_frag>,
                 avx512_store_vl<0x7F, "vmovdqu64", avx512vl_i64_info, HasAVX512,
                                 SchedWriteVecMoveLS, "VMOVDQU">,
                 XS, VEX_W, EVEX_CD8<64, CD8VF>;

// Special instructions to help with spilling when we don't have VLX. We need
// to load or store from a ZMM register instead. These are converted in
// expandPostRAPseudos.
let isReMaterializable = 1, canFoldAsLoad = 1,
    isPseudo = 1, mayLoad = 1, hasSideEffects = 0 in {
def VMOVAPSZ128rm_NOVLX : I<0, Pseudo, (outs VR128X:$dst), (ins f128mem:$src),
                            "", []>, Sched<[WriteFLoadX]>;
def VMOVAPSZ256rm_NOVLX : I<0, Pseudo, (outs VR256X:$dst), (ins f256mem:$src),
                            "", []>, Sched<[WriteFLoadY]>;
def VMOVUPSZ128rm_NOVLX : I<0, Pseudo, (outs VR128X:$dst), (ins f128mem:$src),
                            "", []>, Sched<[WriteFLoadX]>;
def VMOVUPSZ256rm_NOVLX : I<0, Pseudo, (outs VR256X:$dst), (ins f256mem:$src),
                            "", []>, Sched<[WriteFLoadY]>;
}

let isPseudo = 1, mayStore = 1, hasSideEffects = 0 in {
def VMOVAPSZ128mr_NOVLX : I<0, Pseudo, (outs), (ins f128mem:$dst, VR128X:$src),
                            "", []>, Sched<[WriteFStoreX]>;
def VMOVAPSZ256mr_NOVLX : I<0, Pseudo, (outs), (ins f256mem:$dst, VR256X:$src),
                            "", []>, Sched<[WriteFStoreY]>;
def VMOVUPSZ128mr_NOVLX : I<0, Pseudo, (outs), (ins f128mem:$dst, VR128X:$src),
                            "", []>, Sched<[WriteFStoreX]>;
def VMOVUPSZ256mr_NOVLX : I<0, Pseudo, (outs), (ins f256mem:$dst, VR256X:$src),
                            "", []>, Sched<[WriteFStoreY]>;
}

def : Pat<(v8i64 (vselect VK8WM:$mask, (bc_v8i64 (v16i32 immAllZerosV)),
                          (v8i64 VR512:$src))),
   (VMOVDQA64Zrrkz (COPY_TO_REGCLASS (KNOTWrr (COPY_TO_REGCLASS VK8:$mask, VK16)),
                                              VK8), VR512:$src)>;

def : Pat<(v16i32 (vselect VK16WM:$mask, (v16i32 immAllZerosV),
                           (v16i32 VR512:$src))),
                  (VMOVDQA32Zrrkz (KNOTWrr VK16WM:$mask), VR512:$src)>;

// These patterns exist to prevent the above patterns from introducing a second
// mask inversion when one already exists.
def : Pat<(v8i64 (vselect (xor VK8:$mask, (v8i1 immAllOnesV)),
                          (bc_v8i64 (v16i32 immAllZerosV)),
                          (v8i64 VR512:$src))),
                 (VMOVDQA64Zrrkz VK8:$mask, VR512:$src)>;
def : Pat<(v16i32 (vselect (xor VK16:$mask, (v16i1 immAllOnesV)),
                           (v16i32 immAllZerosV),
                           (v16i32 VR512:$src))),
                  (VMOVDQA32Zrrkz VK16WM:$mask, VR512:$src)>;

multiclass mask_move_lowering<string InstrStr, X86VectorVTInfo Narrow,
                              X86VectorVTInfo Wide> {
 def : Pat<(Narrow.VT (vselect (Narrow.KVT Narrow.KRCWM:$mask),
                               Narrow.RC:$src1, Narrow.RC:$src0)),
           (EXTRACT_SUBREG
            (Wide.VT
             (!cast<Instruction>(InstrStr#"rrk")
              (Wide.VT (INSERT_SUBREG (IMPLICIT_DEF), Narrow.RC:$src0, Narrow.SubRegIdx)),
              (COPY_TO_REGCLASS Narrow.KRCWM:$mask, Wide.KRCWM),
              (Wide.VT (INSERT_SUBREG (IMPLICIT_DEF), Narrow.RC:$src1, Narrow.SubRegIdx)))),
            Narrow.SubRegIdx)>;

 def : Pat<(Narrow.VT (vselect (Narrow.KVT Narrow.KRCWM:$mask),
                               Narrow.RC:$src1, Narrow.ImmAllZerosV)),
           (EXTRACT_SUBREG
            (Wide.VT
             (!cast<Instruction>(InstrStr#"rrkz")
              (COPY_TO_REGCLASS Narrow.KRCWM:$mask, Wide.KRCWM),
              (Wide.VT (INSERT_SUBREG (IMPLICIT_DEF), Narrow.RC:$src1, Narrow.SubRegIdx)))),
            Narrow.SubRegIdx)>;
}

// Patterns for handling v8i1 selects of 256-bit vectors when VLX isn't
// available. Use a 512-bit operation and extract.
let Predicates = [HasAVX512, NoVLX] in {
  defm : mask_move_lowering<"VMOVAPSZ", v4f32x_info, v16f32_info>;
  defm : mask_move_lowering<"VMOVDQA32Z", v4i32x_info, v16i32_info>;
  defm : mask_move_lowering<"VMOVAPSZ", v8f32x_info, v16f32_info>;
  defm : mask_move_lowering<"VMOVDQA32Z", v8i32x_info, v16i32_info>;

  defm : mask_move_lowering<"VMOVAPDZ", v2f64x_info, v8f64_info>;
  defm : mask_move_lowering<"VMOVDQA64Z", v2i64x_info, v8i64_info>;
  defm : mask_move_lowering<"VMOVAPDZ", v4f64x_info, v8f64_info>;
  defm : mask_move_lowering<"VMOVDQA64Z", v4i64x_info, v8i64_info>;
}

let Predicates = [HasBWI, NoVLX] in {
  defm : mask_move_lowering<"VMOVDQU8Z", v16i8x_info, v64i8_info>;
  defm : mask_move_lowering<"VMOVDQU8Z", v32i8x_info, v64i8_info>;

  defm : mask_move_lowering<"VMOVDQU16Z", v8i16x_info, v32i16_info>;
  defm : mask_move_lowering<"VMOVDQU16Z", v16i16x_info, v32i16_info>;
}

let Predicates = [HasAVX512] in {
  // 512-bit load.
  def : Pat<(alignedloadv16i32 addr:$src),
            (VMOVDQA64Zrm addr:$src)>;
  def : Pat<(alignedloadv32i16 addr:$src),
            (VMOVDQA64Zrm addr:$src)>;
  def : Pat<(alignedloadv64i8 addr:$src),
            (VMOVDQA64Zrm addr:$src)>;
  def : Pat<(loadv16i32 addr:$src),
            (VMOVDQU64Zrm addr:$src)>;
  def : Pat<(loadv32i16 addr:$src),
            (VMOVDQU64Zrm addr:$src)>;
  def : Pat<(loadv64i8 addr:$src),
            (VMOVDQU64Zrm addr:$src)>;

  // 512-bit store.
  def : Pat<(alignedstore (v16i32 VR512:$src), addr:$dst),
            (VMOVDQA64Zmr addr:$dst, VR512:$src)>;
  def : Pat<(alignedstore (v32i16 VR512:$src), addr:$dst),
            (VMOVDQA64Zmr addr:$dst, VR512:$src)>;
  def : Pat<(alignedstore (v64i8 VR512:$src), addr:$dst),
            (VMOVDQA64Zmr addr:$dst, VR512:$src)>;
  def : Pat<(store (v16i32 VR512:$src), addr:$dst),
            (VMOVDQU64Zmr addr:$dst, VR512:$src)>;
  def : Pat<(store (v32i16 VR512:$src), addr:$dst),
            (VMOVDQU64Zmr addr:$dst, VR512:$src)>;
  def : Pat<(store (v64i8 VR512:$src), addr:$dst),
            (VMOVDQU64Zmr addr:$dst, VR512:$src)>;
}

let Predicates = [HasVLX] in {
  // 128-bit load.
  def : Pat<(alignedloadv4i32 addr:$src),
            (VMOVDQA64Z128rm addr:$src)>;
  def : Pat<(alignedloadv8i16 addr:$src),
            (VMOVDQA64Z128rm addr:$src)>;
  def : Pat<(alignedloadv16i8 addr:$src),
            (VMOVDQA64Z128rm addr:$src)>;
  def : Pat<(loadv4i32 addr:$src),
            (VMOVDQU64Z128rm addr:$src)>;
  def : Pat<(loadv8i16 addr:$src),
            (VMOVDQU64Z128rm addr:$src)>;
  def : Pat<(loadv16i8 addr:$src),
            (VMOVDQU64Z128rm addr:$src)>;

  // 128-bit store.
  def : Pat<(alignedstore (v4i32 VR128X:$src), addr:$dst),
            (VMOVDQA64Z128mr addr:$dst, VR128X:$src)>;
  def : Pat<(alignedstore (v8i16 VR128X:$src), addr:$dst),
            (VMOVDQA64Z128mr addr:$dst, VR128X:$src)>;
  def : Pat<(alignedstore (v16i8 VR128X:$src), addr:$dst),
            (VMOVDQA64Z128mr addr:$dst, VR128X:$src)>;
  def : Pat<(store (v4i32 VR128X:$src), addr:$dst),
            (VMOVDQU64Z128mr addr:$dst, VR128X:$src)>;
  def : Pat<(store (v8i16 VR128X:$src), addr:$dst),
            (VMOVDQU64Z128mr addr:$dst, VR128X:$src)>;
  def : Pat<(store (v16i8 VR128X:$src), addr:$dst),
            (VMOVDQU64Z128mr addr:$dst, VR128X:$src)>;

  // 256-bit load.
  def : Pat<(alignedloadv8i32 addr:$src),
            (VMOVDQA64Z256rm addr:$src)>;
  def : Pat<(alignedloadv16i16 addr:$src),
            (VMOVDQA64Z256rm addr:$src)>;
  def : Pat<(alignedloadv32i8 addr:$src),
            (VMOVDQA64Z256rm addr:$src)>;
  def : Pat<(loadv8i32 addr:$src),
            (VMOVDQU64Z256rm addr:$src)>;
  def : Pat<(loadv16i16 addr:$src),
            (VMOVDQU64Z256rm addr:$src)>;
  def : Pat<(loadv32i8 addr:$src),
            (VMOVDQU64Z256rm addr:$src)>;

  // 256-bit store.
  def : Pat<(alignedstore (v8i32 VR256X:$src), addr:$dst),
            (VMOVDQA64Z256mr addr:$dst, VR256X:$src)>;
  def : Pat<(alignedstore (v16i16 VR256X:$src), addr:$dst),
            (VMOVDQA64Z256mr addr:$dst, VR256X:$src)>;
  def : Pat<(alignedstore (v32i8 VR256X:$src), addr:$dst),
            (VMOVDQA64Z256mr addr:$dst, VR256X:$src)>;
  def : Pat<(store (v8i32 VR256X:$src), addr:$dst),
            (VMOVDQU64Z256mr addr:$dst, VR256X:$src)>;
  def : Pat<(store (v16i16 VR256X:$src), addr:$dst),
            (VMOVDQU64Z256mr addr:$dst, VR256X:$src)>;
  def : Pat<(store (v32i8 VR256X:$src), addr:$dst),
            (VMOVDQU64Z256mr addr:$dst, VR256X:$src)>;
}

multiclass masked_move_for_extract<string InstrStr, X86VectorVTInfo From,
                                   X86VectorVTInfo To, X86VectorVTInfo Cast> {
  def : Pat<(Cast.VT (vselect Cast.KRCWM:$mask,
                              (bitconvert
                               (To.VT (extract_subvector
                                       (From.VT From.RC:$src), (iPTR 0)))),
                              To.RC:$src0)),
            (Cast.VT (!cast<Instruction>(InstrStr#"rrk")
                      Cast.RC:$src0, Cast.KRCWM:$mask,
                      (To.VT (EXTRACT_SUBREG From.RC:$src, To.SubRegIdx))))>;

  def : Pat<(Cast.VT (vselect Cast.KRCWM:$mask,
                              (bitconvert
                               (To.VT (extract_subvector
                                       (From.VT From.RC:$src), (iPTR 0)))),
                              Cast.ImmAllZerosV)),
            (Cast.VT (!cast<Instruction>(InstrStr#"rrkz")
                      Cast.KRCWM:$mask,
                      (To.VT (EXTRACT_SUBREG From.RC:$src, To.SubRegIdx))))>;
}


let Predicates = [HasVLX] in {
// A masked extract from the first 128-bits of a 256-bit vector can be
// implemented with masked move.
defm : masked_move_for_extract<"VMOVDQA64Z128", v4i64x_info,  v2i64x_info, v2i64x_info>;
defm : masked_move_for_extract<"VMOVDQA64Z128", v8i32x_info,  v4i32x_info, v2i64x_info>;
defm : masked_move_for_extract<"VMOVDQA64Z128", v16i16x_info, v8i16x_info, v2i64x_info>;
defm : masked_move_for_extract<"VMOVDQA64Z128", v32i8x_info,  v16i8x_info, v2i64x_info>;
defm : masked_move_for_extract<"VMOVDQA32Z128", v4i64x_info,  v2i64x_info, v4i32x_info>;
defm : masked_move_for_extract<"VMOVDQA32Z128", v8i32x_info,  v4i32x_info, v4i32x_info>;
defm : masked_move_for_extract<"VMOVDQA32Z128", v16i16x_info, v8i16x_info, v4i32x_info>;
defm : masked_move_for_extract<"VMOVDQA32Z128", v32i8x_info,  v16i8x_info, v4i32x_info>;
defm : masked_move_for_extract<"VMOVAPDZ128",   v4f64x_info,  v2f64x_info, v2f64x_info>;
defm : masked_move_for_extract<"VMOVAPDZ128",   v8f32x_info,  v4f32x_info, v2f64x_info>;
defm : masked_move_for_extract<"VMOVAPSZ128",   v4f64x_info,  v2f64x_info, v4f32x_info>;
defm : masked_move_for_extract<"VMOVAPSZ128",   v8f32x_info,  v4f32x_info, v4f32x_info>;

// A masked extract from the first 128-bits of a 512-bit vector can be
// implemented with masked move.
defm : masked_move_for_extract<"VMOVDQA64Z128", v8i64_info,  v2i64x_info, v2i64x_info>;
defm : masked_move_for_extract<"VMOVDQA64Z128", v16i32_info, v4i32x_info, v2i64x_info>;
defm : masked_move_for_extract<"VMOVDQA64Z128", v32i16_info, v8i16x_info, v2i64x_info>;
defm : masked_move_for_extract<"VMOVDQA64Z128", v64i8_info,  v16i8x_info, v2i64x_info>;
defm : masked_move_for_extract<"VMOVDQA32Z128", v8i64_info,  v2i64x_info, v4i32x_info>;
defm : masked_move_for_extract<"VMOVDQA32Z128", v16i32_info, v4i32x_info, v4i32x_info>;
defm : masked_move_for_extract<"VMOVDQA32Z128", v32i16_info, v8i16x_info, v4i32x_info>;
defm : masked_move_for_extract<"VMOVDQA32Z128", v64i8_info,  v16i8x_info, v4i32x_info>;
defm : masked_move_for_extract<"VMOVAPDZ128",   v8f64_info,  v2f64x_info, v2f64x_info>;
defm : masked_move_for_extract<"VMOVAPDZ128",   v16f32_info, v4f32x_info, v2f64x_info>;
defm : masked_move_for_extract<"VMOVAPSZ128",   v8f64_info,  v2f64x_info, v4f32x_info>;
defm : masked_move_for_extract<"VMOVAPSZ128",   v16f32_info, v4f32x_info, v4f32x_info>;

// A masked extract from the first 256-bits of a 512-bit vector can be
// implemented with masked move.
defm : masked_move_for_extract<"VMOVDQA64Z256", v8i64_info,  v4i64x_info,  v4i64x_info>;
defm : masked_move_for_extract<"VMOVDQA64Z256", v16i32_info, v8i32x_info,  v4i64x_info>;
defm : masked_move_for_extract<"VMOVDQA64Z256", v32i16_info, v16i16x_info, v4i64x_info>;
defm : masked_move_for_extract<"VMOVDQA64Z256", v64i8_info,  v32i8x_info,  v4i64x_info>;
defm : masked_move_for_extract<"VMOVDQA32Z256", v8i64_info,  v4i64x_info,  v8i32x_info>;
defm : masked_move_for_extract<"VMOVDQA32Z256", v16i32_info, v8i32x_info,  v8i32x_info>;
defm : masked_move_for_extract<"VMOVDQA32Z256", v32i16_info, v16i16x_info, v8i32x_info>;
defm : masked_move_for_extract<"VMOVDQA32Z256", v64i8_info,  v32i8x_info,  v8i32x_info>;
defm : masked_move_for_extract<"VMOVAPDZ256",   v8f64_info,  v4f64x_info,  v4f64x_info>;
defm : masked_move_for_extract<"VMOVAPDZ256",   v16f32_info, v8f32x_info,  v4f64x_info>;
defm : masked_move_for_extract<"VMOVAPSZ256",   v8f64_info,  v4f64x_info,  v8f32x_info>;
defm : masked_move_for_extract<"VMOVAPSZ256",   v16f32_info, v8f32x_info,  v8f32x_info>;
}

// Move Int Doubleword to Packed Double Int
//
let ExeDomain = SSEPackedInt in {
def VMOVDI2PDIZrr : AVX512BI<0x6E, MRMSrcReg, (outs VR128X:$dst), (ins GR32:$src),
                      "vmovd\t{$src, $dst|$dst, $src}",
                      [(set VR128X:$dst,
                        (v4i32 (scalar_to_vector GR32:$src)))]>,
                        EVEX, Sched<[WriteVecMoveFromGpr]>;
def VMOVDI2PDIZrm : AVX512BI<0x6E, MRMSrcMem, (outs VR128X:$dst), (ins i32mem:$src),
                      "vmovd\t{$src, $dst|$dst, $src}",
                      [(set VR128X:$dst,
                        (v4i32 (scalar_to_vector (loadi32 addr:$src))))]>,
                      EVEX, EVEX_CD8<32, CD8VT1>, Sched<[WriteVecLoad]>;
def VMOV64toPQIZrr : AVX512BI<0x6E, MRMSrcReg, (outs VR128X:$dst), (ins GR64:$src),
                      "vmovq\t{$src, $dst|$dst, $src}",
                        [(set VR128X:$dst,
                          (v2i64 (scalar_to_vector GR64:$src)))]>,
                      EVEX, VEX_W, Sched<[WriteVecMoveFromGpr]>;
let isCodeGenOnly = 1, ForceDisassemble = 1, hasSideEffects = 0, mayLoad = 1 in
def VMOV64toPQIZrm : AVX512BI<0x6E, MRMSrcMem, (outs VR128X:$dst),
                      (ins i64mem:$src),
                      "vmovq\t{$src, $dst|$dst, $src}", []>,
                      EVEX, VEX_W, EVEX_CD8<64, CD8VT1>, Sched<[WriteVecLoad]>;
let isCodeGenOnly = 1 in {
def VMOV64toSDZrr : AVX512BI<0x6E, MRMSrcReg, (outs FR64X:$dst), (ins GR64:$src),
                       "vmovq\t{$src, $dst|$dst, $src}",
                       [(set FR64X:$dst, (bitconvert GR64:$src))]>,
                       EVEX, VEX_W, Sched<[WriteVecMoveFromGpr]>;
def VMOV64toSDZrm : AVX512XSI<0x7E, MRMSrcMem, (outs FR64X:$dst), (ins i64mem:$src),
                      "vmovq\t{$src, $dst|$dst, $src}",
                      [(set FR64X:$dst, (bitconvert (loadi64 addr:$src)))]>,
                      EVEX, VEX_W, EVEX_CD8<8, CD8VT8>, Sched<[WriteVecLoad]>;
def VMOVSDto64Zrr : AVX512BI<0x7E, MRMDestReg, (outs GR64:$dst), (ins FR64X:$src),
                         "vmovq\t{$src, $dst|$dst, $src}",
                         [(set GR64:$dst, (bitconvert FR64X:$src))]>,
                         EVEX, VEX_W, Sched<[WriteVecMoveFromGpr]>;
def VMOVSDto64Zmr : AVX512BI<0x7E, MRMDestMem, (outs), (ins i64mem:$dst, FR64X:$src),
                         "vmovq\t{$src, $dst|$dst, $src}",
                         [(store (i64 (bitconvert FR64X:$src)), addr:$dst)]>,
                         EVEX, VEX_W, Sched<[WriteVecStore]>,
                         EVEX_CD8<64, CD8VT1>;
}
} // ExeDomain = SSEPackedInt

// Move Int Doubleword to Single Scalar
//
let ExeDomain = SSEPackedInt, isCodeGenOnly = 1 in {
def VMOVDI2SSZrr  : AVX512BI<0x6E, MRMSrcReg, (outs FR32X:$dst), (ins GR32:$src),
                      "vmovd\t{$src, $dst|$dst, $src}",
                      [(set FR32X:$dst, (bitconvert GR32:$src))]>,
                      EVEX, Sched<[WriteVecMoveFromGpr]>;

def VMOVDI2SSZrm  : AVX512BI<0x6E, MRMSrcMem, (outs FR32X:$dst), (ins i32mem:$src),
                      "vmovd\t{$src, $dst|$dst, $src}",
                      [(set FR32X:$dst, (bitconvert (loadi32 addr:$src)))]>,
                      EVEX, EVEX_CD8<32, CD8VT1>, Sched<[WriteVecLoad]>;
} // ExeDomain = SSEPackedInt, isCodeGenOnly = 1

// Move doubleword from xmm register to r/m32
//
let ExeDomain = SSEPackedInt in {
def VMOVPDI2DIZrr  : AVX512BI<0x7E, MRMDestReg, (outs GR32:$dst), (ins VR128X:$src),
                       "vmovd\t{$src, $dst|$dst, $src}",
                       [(set GR32:$dst, (extractelt (v4i32 VR128X:$src),
                                        (iPTR 0)))]>,
                       EVEX, Sched<[WriteVecMoveToGpr]>;
def VMOVPDI2DIZmr  : AVX512BI<0x7E, MRMDestMem, (outs),
                       (ins i32mem:$dst, VR128X:$src),
                       "vmovd\t{$src, $dst|$dst, $src}",
                       [(store (i32 (extractelt (v4i32 VR128X:$src),
                                     (iPTR 0))), addr:$dst)]>,
                       EVEX, EVEX_CD8<32, CD8VT1>, Sched<[WriteVecStore]>;
} // ExeDomain = SSEPackedInt

// Move quadword from xmm1 register to r/m64
//
let ExeDomain = SSEPackedInt in {
def VMOVPQIto64Zrr : I<0x7E, MRMDestReg, (outs GR64:$dst), (ins VR128X:$src),
                      "vmovq\t{$src, $dst|$dst, $src}",
                      [(set GR64:$dst, (extractelt (v2i64 VR128X:$src),
                                                   (iPTR 0)))]>,
                      PD, EVEX, VEX_W, Sched<[WriteVecMoveToGpr]>,
                      Requires<[HasAVX512]>;

let isCodeGenOnly = 1, ForceDisassemble = 1, hasSideEffects = 0, mayStore = 1 in
def VMOVPQIto64Zmr : I<0x7E, MRMDestMem, (outs), (ins i64mem:$dst, VR128X:$src),
                      "vmovq\t{$src, $dst|$dst, $src}", []>, PD,
                      EVEX, VEX_W, Sched<[WriteVecStore]>,
                      Requires<[HasAVX512, In64BitMode]>;

def VMOVPQI2QIZmr : I<0xD6, MRMDestMem, (outs),
                      (ins i64mem:$dst, VR128X:$src),
                      "vmovq\t{$src, $dst|$dst, $src}",
                      [(store (extractelt (v2i64 VR128X:$src), (iPTR 0)),
                              addr:$dst)]>,
                      EVEX, PD, VEX_W, EVEX_CD8<64, CD8VT1>,
                      Sched<[WriteVecStore]>, Requires<[HasAVX512]>;

let hasSideEffects = 0, isCodeGenOnly = 1, ForceDisassemble = 1 in
def VMOVPQI2QIZrr : AVX512BI<0xD6, MRMDestReg, (outs VR128X:$dst),
                             (ins VR128X:$src),
                             "vmovq\t{$src, $dst|$dst, $src}", []>,
                             EVEX, VEX_W, Sched<[SchedWriteVecLogic.XMM]>;
} // ExeDomain = SSEPackedInt

def : InstAlias<"vmovq.s\t{$src, $dst|$dst, $src}",
                (VMOVPQI2QIZrr VR128X:$dst, VR128X:$src), 0>;

// Move Scalar Single to Double Int
//
let ExeDomain = SSEPackedInt, isCodeGenOnly = 1 in {
def VMOVSS2DIZrr  : AVX512BI<0x7E, MRMDestReg, (outs GR32:$dst),
                      (ins FR32X:$src),
                      "vmovd\t{$src, $dst|$dst, $src}",
                      [(set GR32:$dst, (bitconvert FR32X:$src))]>,
                      EVEX, Sched<[WriteVecMoveToGpr]>;
def VMOVSS2DIZmr  : AVX512BI<0x7E, MRMDestMem, (outs),
                      (ins i32mem:$dst, FR32X:$src),
                      "vmovd\t{$src, $dst|$dst, $src}",
                      [(store (i32 (bitconvert FR32X:$src)), addr:$dst)]>,
                      EVEX, EVEX_CD8<32, CD8VT1>, Sched<[WriteVecStore]>;
} // ExeDomain = SSEPackedInt, isCodeGenOnly = 1

// Move Quadword Int to Packed Quadword Int
//
let ExeDomain = SSEPackedInt in {
def VMOVQI2PQIZrm : AVX512XSI<0x7E, MRMSrcMem, (outs VR128X:$dst),
                      (ins i64mem:$src),
                      "vmovq\t{$src, $dst|$dst, $src}",
                      [(set VR128X:$dst,
                        (v2i64 (scalar_to_vector (loadi64 addr:$src))))]>,
                      EVEX, VEX_W, EVEX_CD8<8, CD8VT8>, Sched<[WriteVecLoad]>;
} // ExeDomain = SSEPackedInt

// Allow "vmovd" but print "vmovq".
def : InstAlias<"vmovd\t{$src, $dst|$dst, $src}",
                (VMOV64toPQIZrr VR128X:$dst, GR64:$src), 0>;
def : InstAlias<"vmovd\t{$src, $dst|$dst, $src}",
                (VMOVPQIto64Zrr GR64:$dst, VR128X:$src), 0>;

//===----------------------------------------------------------------------===//
// AVX-512  MOVSS, MOVSD
//===----------------------------------------------------------------------===//

multiclass avx512_move_scalar<string asm, SDNode OpNode,
                              X86VectorVTInfo _> {
  let Predicates = [HasAVX512, OptForSize] in
  def rr : AVX512PI<0x10, MRMSrcReg, (outs _.RC:$dst),
             (ins _.RC:$src1, _.RC:$src2),
             !strconcat(asm, "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
             [(set _.RC:$dst, (_.VT (OpNode _.RC:$src1, _.RC:$src2)))],
             _.ExeDomain>, EVEX_4V, Sched<[SchedWriteFShuffle.XMM]>;
  def rrkz : AVX512PI<0x10, MRMSrcReg, (outs _.RC:$dst),
              (ins _.KRCWM:$mask, _.RC:$src1, _.RC:$src2),
              !strconcat(asm, "\t{$src2, $src1, $dst {${mask}} {z}|",
              "$dst {${mask}} {z}, $src1, $src2}"),
              [(set _.RC:$dst, (_.VT (X86selects _.KRCWM:$mask,
                                      (_.VT (OpNode _.RC:$src1, _.RC:$src2)),
                                      _.ImmAllZerosV)))],
              _.ExeDomain>, EVEX_4V, EVEX_KZ, Sched<[SchedWriteFShuffle.XMM]>;
  let Constraints = "$src0 = $dst"  in
  def rrk : AVX512PI<0x10, MRMSrcReg, (outs _.RC:$dst),
             (ins _.RC:$src0, _.KRCWM:$mask, _.RC:$src1, _.RC:$src2),
             !strconcat(asm, "\t{$src2, $src1, $dst {${mask}}|",
             "$dst {${mask}}, $src1, $src2}"),
             [(set _.RC:$dst, (_.VT (X86selects _.KRCWM:$mask,
                                     (_.VT (OpNode _.RC:$src1, _.RC:$src2)),
                                     (_.VT _.RC:$src0))))],
             _.ExeDomain>, EVEX_4V, EVEX_K, Sched<[SchedWriteFShuffle.XMM]>;
  let canFoldAsLoad = 1, isReMaterializable = 1 in
  def rm : AVX512PI<0x10, MRMSrcMem, (outs _.FRC:$dst), (ins _.ScalarMemOp:$src),
             !strconcat(asm, "\t{$src, $dst|$dst, $src}"),
             [(set _.FRC:$dst, (_.ScalarLdFrag addr:$src))],
             _.ExeDomain>, EVEX, Sched<[WriteFLoad]>;
  let mayLoad = 1, hasSideEffects = 0 in {
    let Constraints = "$src0 = $dst" in
    def rmk : AVX512PI<0x10, MRMSrcMem, (outs _.RC:$dst),
               (ins _.RC:$src0, _.KRCWM:$mask, _.ScalarMemOp:$src),
               !strconcat(asm, "\t{$src, $dst {${mask}}|",
               "$dst {${mask}}, $src}"),
               [], _.ExeDomain>, EVEX, EVEX_K, Sched<[WriteFLoad]>;
    def rmkz : AVX512PI<0x10, MRMSrcMem, (outs _.RC:$dst),
               (ins _.KRCWM:$mask, _.ScalarMemOp:$src),
               !strconcat(asm, "\t{$src, $dst {${mask}} {z}|",
               "$dst {${mask}} {z}, $src}"),
               [], _.ExeDomain>, EVEX, EVEX_KZ, Sched<[WriteFLoad]>;
  }
  def mr: AVX512PI<0x11, MRMDestMem, (outs), (ins _.ScalarMemOp:$dst, _.FRC:$src),
             !strconcat(asm, "\t{$src, $dst|$dst, $src}"),
             [(store _.FRC:$src, addr:$dst)],  _.ExeDomain>,
             EVEX, Sched<[WriteFStore]>;
  let mayStore = 1, hasSideEffects = 0 in
  def mrk: AVX512PI<0x11, MRMDestMem, (outs),
              (ins _.ScalarMemOp:$dst, VK1WM:$mask, _.FRC:$src),
              !strconcat(asm, "\t{$src, $dst {${mask}}|$dst {${mask}}, $src}"),
              [], _.ExeDomain>, EVEX, EVEX_K, Sched<[WriteFStore]>,
              NotMemoryFoldable;
}

defm VMOVSSZ : avx512_move_scalar<"vmovss", X86Movss, f32x_info>,
                                  VEX_LIG, XS, EVEX_CD8<32, CD8VT1>;

defm VMOVSDZ : avx512_move_scalar<"vmovsd", X86Movsd, f64x_info>,
                                  VEX_LIG, XD, VEX_W, EVEX_CD8<64, CD8VT1>;


multiclass avx512_move_scalar_lowering<string InstrStr, SDNode OpNode,
                                       PatLeaf ZeroFP, X86VectorVTInfo _> {

def : Pat<(_.VT (OpNode _.RC:$src0,
                        (_.VT (scalar_to_vector
                                  (_.EltVT (X86selects VK1WM:$mask,
                                                       (_.EltVT _.FRC:$src1),
                                                       (_.EltVT _.FRC:$src2))))))),
          (!cast<Instruction>(InstrStr#rrk)
                        (_.VT (COPY_TO_REGCLASS _.FRC:$src2, _.RC)),
                        VK1WM:$mask,
                        (_.VT _.RC:$src0),
                        (_.VT (COPY_TO_REGCLASS _.FRC:$src1, _.RC)))>;

def : Pat<(_.VT (OpNode _.RC:$src0,
                        (_.VT (scalar_to_vector
                                  (_.EltVT (X86selects VK1WM:$mask,
                                                       (_.EltVT _.FRC:$src1),
                                                       (_.EltVT ZeroFP))))))),
          (!cast<Instruction>(InstrStr#rrkz)
                        VK1WM:$mask,
                        (_.VT _.RC:$src0),
                        (_.VT (COPY_TO_REGCLASS _.FRC:$src1, _.RC)))>;
}

multiclass avx512_store_scalar_lowering<string InstrStr, AVX512VLVectorVTInfo _,
                                        dag Mask, RegisterClass MaskRC> {

def : Pat<(masked_store
             (_.info512.VT (insert_subvector undef,
                               (_.info128.VT _.info128.RC:$src),
                               (iPTR 0))), addr:$dst, Mask),
          (!cast<Instruction>(InstrStr#mrk) addr:$dst,
                      (COPY_TO_REGCLASS MaskRC:$mask, VK1WM),
                      (COPY_TO_REGCLASS _.info128.RC:$src, _.info128.FRC))>;

}

multiclass avx512_store_scalar_lowering_subreg<string InstrStr,
                                               AVX512VLVectorVTInfo _,
                                               dag Mask, RegisterClass MaskRC,
                                               SubRegIndex subreg> {

def : Pat<(masked_store
             (_.info512.VT (insert_subvector undef,
                               (_.info128.VT _.info128.RC:$src),
                               (iPTR 0))), addr:$dst, Mask),
          (!cast<Instruction>(InstrStr#mrk) addr:$dst,
                      (COPY_TO_REGCLASS (i32 (INSERT_SUBREG (IMPLICIT_DEF), MaskRC:$mask, subreg)), VK1WM),
                      (COPY_TO_REGCLASS _.info128.RC:$src, _.info128.FRC))>;

}

// This matches the more recent codegen from clang that avoids emitting a 512
// bit masked store directly. Codegen will widen 128-bit masked store to 512
// bits on AVX512F only targets.
multiclass avx512_store_scalar_lowering_subreg2<string InstrStr,
                                               AVX512VLVectorVTInfo _,
                                               dag Mask512, dag Mask128,
                                               RegisterClass MaskRC,
                                               SubRegIndex subreg> {

// AVX512F pattern.
def : Pat<(masked_store
             (_.info512.VT (insert_subvector undef,
                               (_.info128.VT _.info128.RC:$src),
                               (iPTR 0))), addr:$dst, Mask512),
          (!cast<Instruction>(InstrStr#mrk) addr:$dst,
                      (COPY_TO_REGCLASS (i32 (INSERT_SUBREG (IMPLICIT_DEF), MaskRC:$mask, subreg)), VK1WM),
                      (COPY_TO_REGCLASS _.info128.RC:$src, _.info128.FRC))>;

// AVX512VL pattern.
def : Pat<(masked_store (_.info128.VT _.info128.RC:$src), addr:$dst, Mask128),
          (!cast<Instruction>(InstrStr#mrk) addr:$dst,
                      (COPY_TO_REGCLASS (i32 (INSERT_SUBREG (IMPLICIT_DEF), MaskRC:$mask, subreg)), VK1WM),
                      (COPY_TO_REGCLASS _.info128.RC:$src, _.info128.FRC))>;
}

multiclass avx512_load_scalar_lowering<string InstrStr, AVX512VLVectorVTInfo _,
                                       dag Mask, RegisterClass MaskRC> {

def : Pat<(_.info128.VT (extract_subvector
                         (_.info512.VT (masked_load addr:$srcAddr, Mask,
                                        (_.info512.VT (bitconvert
                                                       (v16i32 immAllZerosV))))),
                           (iPTR 0))),
          (!cast<Instruction>(InstrStr#rmkz)
                      (COPY_TO_REGCLASS MaskRC:$mask, VK1WM),
                      addr:$srcAddr)>;

def : Pat<(_.info128.VT (extract_subvector
                (_.info512.VT (masked_load addr:$srcAddr, Mask,
                      (_.info512.VT (insert_subvector undef,
                            (_.info128.VT (X86vzmovl _.info128.RC:$src)),
                            (iPTR 0))))),
                (iPTR 0))),
          (!cast<Instruction>(InstrStr#rmk) _.info128.RC:$src,
                      (COPY_TO_REGCLASS MaskRC:$mask, VK1WM),
                      addr:$srcAddr)>;

}

multiclass avx512_load_scalar_lowering_subreg<string InstrStr,
                                              AVX512VLVectorVTInfo _,
                                              dag Mask, RegisterClass MaskRC,
                                              SubRegIndex subreg> {

def : Pat<(_.info128.VT (extract_subvector
                         (_.info512.VT (masked_load addr:$srcAddr, Mask,
                                        (_.info512.VT (bitconvert
                                                       (v16i32 immAllZerosV))))),
                           (iPTR 0))),
          (!cast<Instruction>(InstrStr#rmkz)
                      (COPY_TO_REGCLASS (i32 (INSERT_SUBREG (IMPLICIT_DEF), MaskRC:$mask, subreg)), VK1WM),
                      addr:$srcAddr)>;

def : Pat<(_.info128.VT (extract_subvector
                (_.info512.VT (masked_load addr:$srcAddr, Mask,
                      (_.info512.VT (insert_subvector undef,
                            (_.info128.VT (X86vzmovl _.info128.RC:$src)),
                            (iPTR 0))))),
                (iPTR 0))),
          (!cast<Instruction>(InstrStr#rmk) _.info128.RC:$src,
                      (COPY_TO_REGCLASS (i32 (INSERT_SUBREG (IMPLICIT_DEF), MaskRC:$mask, subreg)), VK1WM),
                      addr:$srcAddr)>;

}

// This matches the more recent codegen from clang that avoids emitting a 512
// bit masked load directly. Codegen will widen 128-bit masked load to 512
// bits on AVX512F only targets.
multiclass avx512_load_scalar_lowering_subreg2<string InstrStr,
                                              AVX512VLVectorVTInfo _,
                                              dag Mask512, dag Mask128,
                                              RegisterClass MaskRC,
                                              SubRegIndex subreg> {
// AVX512F patterns.
def : Pat<(_.info128.VT (extract_subvector
                         (_.info512.VT (masked_load addr:$srcAddr, Mask512,
                                        (_.info512.VT (bitconvert
                                                       (v16i32 immAllZerosV))))),
                           (iPTR 0))),
          (!cast<Instruction>(InstrStr#rmkz)
                      (COPY_TO_REGCLASS (i32 (INSERT_SUBREG (IMPLICIT_DEF), MaskRC:$mask, subreg)), VK1WM),
                      addr:$srcAddr)>;

def : Pat<(_.info128.VT (extract_subvector
                (_.info512.VT (masked_load addr:$srcAddr, Mask512,
                      (_.info512.VT (insert_subvector undef,
                            (_.info128.VT (X86vzmovl _.info128.RC:$src)),
                            (iPTR 0))))),
                (iPTR 0))),
          (!cast<Instruction>(InstrStr#rmk) _.info128.RC:$src,
                      (COPY_TO_REGCLASS (i32 (INSERT_SUBREG (IMPLICIT_DEF), MaskRC:$mask, subreg)), VK1WM),
                      addr:$srcAddr)>;

// AVX512Vl patterns.
def : Pat<(_.info128.VT (masked_load addr:$srcAddr, Mask128,
                         (_.info128.VT (bitconvert (v4i32 immAllZerosV))))),
          (!cast<Instruction>(InstrStr#rmkz)
                      (COPY_TO_REGCLASS (i32 (INSERT_SUBREG (IMPLICIT_DEF), MaskRC:$mask, subreg)), VK1WM),
                      addr:$srcAddr)>;

def : Pat<(_.info128.VT (masked_load addr:$srcAddr, Mask128,
                         (_.info128.VT (X86vzmovl _.info128.RC:$src)))),
          (!cast<Instruction>(InstrStr#rmk) _.info128.RC:$src,
                      (COPY_TO_REGCLASS (i32 (INSERT_SUBREG (IMPLICIT_DEF), MaskRC:$mask, subreg)), VK1WM),
                      addr:$srcAddr)>;
}

defm : avx512_move_scalar_lowering<"VMOVSSZ", X86Movss, fp32imm0, v4f32x_info>;
defm : avx512_move_scalar_lowering<"VMOVSDZ", X86Movsd, fp64imm0, v2f64x_info>;

defm : avx512_store_scalar_lowering<"VMOVSSZ", avx512vl_f32_info,
                   (v16i1 (bitconvert (i16 (trunc (and GR32:$mask, (i32 1)))))), GR32>;
defm : avx512_store_scalar_lowering_subreg<"VMOVSSZ", avx512vl_f32_info,
                   (v16i1 (bitconvert (i16 (and GR16:$mask, (i16 1))))), GR16, sub_16bit>;
defm : avx512_store_scalar_lowering_subreg<"VMOVSDZ", avx512vl_f64_info,
                   (v8i1 (bitconvert (i8 (and GR8:$mask, (i8 1))))), GR8, sub_8bit>;

defm : avx512_store_scalar_lowering_subreg2<"VMOVSSZ", avx512vl_f32_info,
                   (v16i1 (insert_subvector
                           (v16i1 immAllZerosV),
                           (v4i1 (extract_subvector
                                  (v8i1 (bitconvert (and GR8:$mask, (i8 1)))),
                                  (iPTR 0))),
                           (iPTR 0))),
                   (v4i1 (extract_subvector
                          (v8i1 (bitconvert (and GR8:$mask, (i8 1)))),
                          (iPTR 0))), GR8, sub_8bit>;
defm : avx512_store_scalar_lowering_subreg2<"VMOVSDZ", avx512vl_f64_info,
                   (v8i1
                    (extract_subvector
                     (v16i1
                      (insert_subvector
                       (v16i1 immAllZerosV),
                       (v2i1 (extract_subvector
                              (v8i1 (bitconvert (i8 (and GR8:$mask, (i8 1))))),
                              (iPTR 0))),
                       (iPTR 0))),
                     (iPTR 0))),
                   (v2i1 (extract_subvector
                          (v8i1 (bitconvert (i8 (and GR8:$mask, (i8 1))))),
                          (iPTR 0))), GR8, sub_8bit>;

defm : avx512_load_scalar_lowering<"VMOVSSZ", avx512vl_f32_info,
                   (v16i1 (bitconvert (i16 (trunc (and GR32:$mask, (i32 1)))))), GR32>;
defm : avx512_load_scalar_lowering_subreg<"VMOVSSZ", avx512vl_f32_info,
                   (v16i1 (bitconvert (i16 (and GR16:$mask, (i16 1))))), GR16, sub_16bit>;
defm : avx512_load_scalar_lowering_subreg<"VMOVSDZ", avx512vl_f64_info,
                   (v8i1 (bitconvert (i8 (and GR8:$mask, (i8 1))))), GR8, sub_8bit>;

defm : avx512_load_scalar_lowering_subreg2<"VMOVSSZ", avx512vl_f32_info,
                   (v16i1 (insert_subvector
                           (v16i1 immAllZerosV),
                           (v4i1 (extract_subvector
                                  (v8i1 (bitconvert (and GR8:$mask, (i8 1)))),
                                  (iPTR 0))),
                           (iPTR 0))),
                   (v4i1 (extract_subvector
                          (v8i1 (bitconvert (and GR8:$mask, (i8 1)))),
                          (iPTR 0))), GR8, sub_8bit>;
defm : avx512_load_scalar_lowering_subreg2<"VMOVSDZ", avx512vl_f64_info,
                   (v8i1
                    (extract_subvector
                     (v16i1
                      (insert_subvector
                       (v16i1 immAllZerosV),
                       (v2i1 (extract_subvector
                              (v8i1 (bitconvert (i8 (and GR8:$mask, (i8 1))))),
                              (iPTR 0))),
                       (iPTR 0))),
                     (iPTR 0))),
                   (v2i1 (extract_subvector
                          (v8i1 (bitconvert (i8 (and GR8:$mask, (i8 1))))),
                          (iPTR 0))), GR8, sub_8bit>;

def : Pat<(f32 (X86selects VK1WM:$mask, (f32 FR32X:$src1), (f32 FR32X:$src2))),
          (COPY_TO_REGCLASS (v4f32 (VMOVSSZrrk
           (v4f32 (COPY_TO_REGCLASS FR32X:$src2, VR128X)),
           VK1WM:$mask, (v4f32 (IMPLICIT_DEF)),
           (v4f32 (COPY_TO_REGCLASS FR32X:$src1, VR128X)))), FR32X)>;

def : Pat<(f32 (X86selects VK1WM:$mask, (f32 FR32X:$src1), fp32imm0)),
          (COPY_TO_REGCLASS (v4f32 (VMOVSSZrrkz VK1WM:$mask, (v4f32 (IMPLICIT_DEF)),
           (v4f32 (COPY_TO_REGCLASS FR32X:$src1, VR128X)))), FR32X)>;

def : Pat<(f64 (X86selects VK1WM:$mask, (f64 FR64X:$src1), (f64 FR64X:$src2))),
          (COPY_TO_REGCLASS (v2f64 (VMOVSDZrrk
           (v2f64 (COPY_TO_REGCLASS FR64X:$src2, VR128X)),
           VK1WM:$mask, (v2f64 (IMPLICIT_DEF)),
           (v2f64 (COPY_TO_REGCLASS FR64X:$src1, VR128X)))), FR64X)>;

def : Pat<(f64 (X86selects VK1WM:$mask, (f64 FR64X:$src1), fpimm0)),
          (COPY_TO_REGCLASS (v2f64 (VMOVSDZrrkz VK1WM:$mask, (v2f64 (IMPLICIT_DEF)),
           (v2f64 (COPY_TO_REGCLASS FR64X:$src1, VR128X)))), FR64X)>;

let hasSideEffects = 0, isCodeGenOnly = 1, ForceDisassemble = 1 in {
  def VMOVSSZrr_REV: AVX512<0x11, MRMDestReg, (outs VR128X:$dst),
                           (ins VR128X:$src1, VR128X:$src2),
                           "vmovss\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                           []>, XS, EVEX_4V, VEX_LIG,
                           FoldGenData<"VMOVSSZrr">,
                           Sched<[SchedWriteFShuffle.XMM]>;

  let Constraints = "$src0 = $dst" in
  def VMOVSSZrrk_REV: AVX512<0x11, MRMDestReg, (outs VR128X:$dst),
                             (ins f32x_info.RC:$src0, f32x_info.KRCWM:$mask,
                                                   VR128X:$src1, VR128X:$src2),
                             "vmovss\t{$src2, $src1, $dst {${mask}}|"#
                                        "$dst {${mask}}, $src1, $src2}",
                             []>, EVEX_K, XS, EVEX_4V, VEX_LIG,
                             FoldGenData<"VMOVSSZrrk">,
                             Sched<[SchedWriteFShuffle.XMM]>;

  def VMOVSSZrrkz_REV: AVX512<0x11, MRMDestReg, (outs VR128X:$dst),
                         (ins f32x_info.KRCWM:$mask, VR128X:$src1, VR128X:$src2),
                         "vmovss\t{$src2, $src1, $dst {${mask}} {z}|"#
                                    "$dst {${mask}} {z}, $src1, $src2}",
                         []>, EVEX_KZ, XS, EVEX_4V, VEX_LIG,
                         FoldGenData<"VMOVSSZrrkz">,
                         Sched<[SchedWriteFShuffle.XMM]>;

  def VMOVSDZrr_REV: AVX512<0x11, MRMDestReg, (outs VR128X:$dst),
                           (ins VR128X:$src1, VR128X:$src2),
                           "vmovsd\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                           []>, XD, EVEX_4V, VEX_LIG, VEX_W,
                           FoldGenData<"VMOVSDZrr">,
                           Sched<[SchedWriteFShuffle.XMM]>;

  let Constraints = "$src0 = $dst" in
  def VMOVSDZrrk_REV: AVX512<0x11, MRMDestReg, (outs VR128X:$dst),
                             (ins f64x_info.RC:$src0, f64x_info.KRCWM:$mask,
                                                   VR128X:$src1, VR128X:$src2),
                             "vmovsd\t{$src2, $src1, $dst {${mask}}|"#
                                        "$dst {${mask}}, $src1, $src2}",
                             []>, EVEX_K, XD, EVEX_4V, VEX_LIG,
                             VEX_W, FoldGenData<"VMOVSDZrrk">,
                             Sched<[SchedWriteFShuffle.XMM]>;

  def VMOVSDZrrkz_REV: AVX512<0x11, MRMDestReg, (outs VR128X:$dst),
                              (ins f64x_info.KRCWM:$mask, VR128X:$src1,
                                                          VR128X:$src2),
                              "vmovsd\t{$src2, $src1, $dst {${mask}} {z}|"#
                                         "$dst {${mask}} {z}, $src1, $src2}",
                              []>, EVEX_KZ, XD, EVEX_4V, VEX_LIG,
                              VEX_W, FoldGenData<"VMOVSDZrrkz">,
                              Sched<[SchedWriteFShuffle.XMM]>;
}

def : InstAlias<"vmovss.s\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                (VMOVSSZrr_REV VR128X:$dst, VR128X:$src1, VR128X:$src2), 0>;
def : InstAlias<"vmovss.s\t{$src2, $src1, $dst {${mask}}|"#
                             "$dst {${mask}}, $src1, $src2}",
                (VMOVSSZrrk_REV VR128X:$dst, VK1WM:$mask,
                                VR128X:$src1, VR128X:$src2), 0>;
def : InstAlias<"vmovss.s\t{$src2, $src1, $dst {${mask}} {z}|"#
                             "$dst {${mask}} {z}, $src1, $src2}",
                (VMOVSSZrrkz_REV VR128X:$dst, VK1WM:$mask,
                                 VR128X:$src1, VR128X:$src2), 0>;
def : InstAlias<"vmovsd.s\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                (VMOVSDZrr_REV VR128X:$dst, VR128X:$src1, VR128X:$src2), 0>;
def : InstAlias<"vmovsd.s\t{$src2, $src1, $dst {${mask}}|"#
                             "$dst {${mask}}, $src1, $src2}",
                (VMOVSDZrrk_REV VR128X:$dst, VK1WM:$mask,
                                VR128X:$src1, VR128X:$src2), 0>;
def : InstAlias<"vmovsd.s\t{$src2, $src1, $dst {${mask}} {z}|"#
                             "$dst {${mask}} {z}, $src1, $src2}",
                (VMOVSDZrrkz_REV VR128X:$dst, VK1WM:$mask,
                                 VR128X:$src1, VR128X:$src2), 0>;

let Predicates = [HasAVX512, OptForSize] in {
  def : Pat<(v4f32 (X86vzmovl (v4f32 VR128X:$src))),
            (VMOVSSZrr (v4f32 (AVX512_128_SET0)), VR128X:$src)>;
  def : Pat<(v4i32 (X86vzmovl (v4i32 VR128X:$src))),
            (VMOVSSZrr (v4i32 (AVX512_128_SET0)), VR128X:$src)>;

  // Move low f32 and clear high bits.
  def : Pat<(v8f32 (X86vzmovl (v8f32 VR256X:$src))),
            (SUBREG_TO_REG (i32 0),
             (v4f32 (VMOVSSZrr (v4f32 (AVX512_128_SET0)),
              (v4f32 (EXTRACT_SUBREG (v8f32 VR256X:$src), sub_xmm)))), sub_xmm)>;
  def : Pat<(v8i32 (X86vzmovl (v8i32 VR256X:$src))),
            (SUBREG_TO_REG (i32 0),
             (v4i32 (VMOVSSZrr (v4i32 (AVX512_128_SET0)),
              (v4i32 (EXTRACT_SUBREG (v8i32 VR256X:$src), sub_xmm)))), sub_xmm)>;

  def : Pat<(v4f64 (X86vzmovl (v4f64 VR256X:$src))),
            (SUBREG_TO_REG (i32 0),
             (v2f64 (VMOVSDZrr (v2f64 (AVX512_128_SET0)),
              (v2f64 (EXTRACT_SUBREG (v4f64 VR256X:$src), sub_xmm)))), sub_xmm)>;
  def : Pat<(v4i64 (X86vzmovl (v4i64 VR256X:$src))),
            (SUBREG_TO_REG (i32 0),
             (v2i64 (VMOVSDZrr (v2i64 (AVX512_128_SET0)),
              (v2i64 (EXTRACT_SUBREG (v4i64 VR256X:$src), sub_xmm)))), sub_xmm)>;

  def : Pat<(v16f32 (X86vzmovl (v16f32 VR512:$src))),
            (SUBREG_TO_REG (i32 0),
             (v4f32 (VMOVSSZrr (v4f32 (AVX512_128_SET0)),
              (v4f32 (EXTRACT_SUBREG (v16f32 VR512:$src), sub_xmm)))), sub_xmm)>;
  def : Pat<(v16i32 (X86vzmovl (v16i32 VR512:$src))),
            (SUBREG_TO_REG (i32 0),
             (v4i32 (VMOVSSZrr (v4i32 (AVX512_128_SET0)),
              (v4i32 (EXTRACT_SUBREG (v16i32 VR512:$src), sub_xmm)))), sub_xmm)>;

  def : Pat<(v8f64 (X86vzmovl (v8f64 VR512:$src))),
            (SUBREG_TO_REG (i32 0),
             (v2f64 (VMOVSDZrr (v2f64 (AVX512_128_SET0)),
              (v2f64 (EXTRACT_SUBREG (v8f64 VR512:$src), sub_xmm)))), sub_xmm)>;

  def : Pat<(v8i64 (X86vzmovl (v8i64 VR512:$src))),
            (SUBREG_TO_REG (i32 0),
             (v2i64 (VMOVSDZrr (v2i64 (AVX512_128_SET0)),
              (v2i64 (EXTRACT_SUBREG (v8i64 VR512:$src), sub_xmm)))), sub_xmm)>;

}

// Use 128-bit blends for OptForSpeed since BLENDs have better throughput than
// VMOVSS/SD. Unfortunately, loses the ability to use XMM16-31.
let Predicates = [HasAVX512, OptForSpeed] in {
  def : Pat<(v16f32 (X86vzmovl (v16f32 VR512:$src))),
            (SUBREG_TO_REG (i32 0),
             (v4f32 (VBLENDPSrri (v4f32 (V_SET0)),
                          (v4f32 (EXTRACT_SUBREG (v16f32 VR512:$src), sub_xmm)),
                          (i8 1))), sub_xmm)>;
  def : Pat<(v16i32 (X86vzmovl (v16i32 VR512:$src))),
            (SUBREG_TO_REG (i32 0),
             (v4i32 (VPBLENDWrri (v4i32 (V_SET0)),
                          (v4i32 (EXTRACT_SUBREG (v16i32 VR512:$src), sub_xmm)),
                          (i8 3))), sub_xmm)>;

  def : Pat<(v8f64 (X86vzmovl (v8f64 VR512:$src))),
            (SUBREG_TO_REG (i32 0),
             (v2f64 (VBLENDPDrri (v2f64 (V_SET0)),
                          (v2f64 (EXTRACT_SUBREG (v8f64 VR512:$src), sub_xmm)),
                          (i8 1))), sub_xmm)>;
  def : Pat<(v8i64 (X86vzmovl (v8i64 VR512:$src))),
            (SUBREG_TO_REG (i32 0),
             (v2i64 (VPBLENDWrri (v2i64 (V_SET0)),
                          (v2i64 (EXTRACT_SUBREG (v8i64 VR512:$src), sub_xmm)),
                          (i8 0xf))), sub_xmm)>;
}

let Predicates = [HasAVX512] in {

  // MOVSSrm zeros the high parts of the register; represent this
  // with SUBREG_TO_REG. The AVX versions also write: DST[255:128] <- 0
  def : Pat<(v4f32 (X86vzmovl (v4f32 (scalar_to_vector (loadf32 addr:$src))))),
            (COPY_TO_REGCLASS (VMOVSSZrm addr:$src), VR128X)>;
  def : Pat<(v4f32 (X86vzmovl (loadv4f32 addr:$src))),
            (COPY_TO_REGCLASS (VMOVSSZrm addr:$src), VR128X)>;
  def : Pat<(v4f32 (X86vzload addr:$src)),
            (COPY_TO_REGCLASS (VMOVSSZrm addr:$src), VR128X)>;

  // MOVSDrm zeros the high parts of the register; represent this
  // with SUBREG_TO_REG. The AVX versions also write: DST[255:128] <- 0
  def : Pat<(v2f64 (X86vzmovl (v2f64 (scalar_to_vector (loadf64 addr:$src))))),
            (COPY_TO_REGCLASS (VMOVSDZrm addr:$src), VR128X)>;
  def : Pat<(v2f64 (X86vzmovl (loadv2f64 addr:$src))),
            (COPY_TO_REGCLASS (VMOVSDZrm addr:$src), VR128X)>;
  def : Pat<(v2f64 (X86vzload addr:$src)),
            (COPY_TO_REGCLASS (VMOVSDZrm addr:$src), VR128X)>;

  // Represent the same patterns above but in the form they appear for
  // 256-bit types
  def : Pat<(v8i32 (X86vzmovl (insert_subvector undef,
                   (v4i32 (scalar_to_vector (loadi32 addr:$src))), (iPTR 0)))),
            (SUBREG_TO_REG (i32 0), (v4i32 (VMOVDI2PDIZrm addr:$src)), sub_xmm)>;
  def : Pat<(v8f32 (X86vzmovl (insert_subvector undef,
                   (v4f32 (scalar_to_vector (loadf32 addr:$src))), (iPTR 0)))),
            (SUBREG_TO_REG (i32 0), (VMOVSSZrm addr:$src), sub_xmm)>;
  def : Pat<(v8f32 (X86vzload addr:$src)),
            (SUBREG_TO_REG (i32 0), (VMOVSSZrm addr:$src), sub_xmm)>;
  def : Pat<(v4f64 (X86vzmovl (insert_subvector undef,
                   (v2f64 (scalar_to_vector (loadf64 addr:$src))), (iPTR 0)))),
            (SUBREG_TO_REG (i32 0), (VMOVSDZrm addr:$src), sub_xmm)>;
  def : Pat<(v4f64 (X86vzload addr:$src)),
            (SUBREG_TO_REG (i32 0), (VMOVSDZrm addr:$src), sub_xmm)>;

  // Represent the same patterns above but in the form they appear for
  // 512-bit types
  def : Pat<(v16i32 (X86vzmovl (insert_subvector undef,
                   (v4i32 (scalar_to_vector (loadi32 addr:$src))), (iPTR 0)))),
            (SUBREG_TO_REG (i32 0), (v4i32 (VMOVDI2PDIZrm addr:$src)), sub_xmm)>;
  def : Pat<(v16f32 (X86vzmovl (insert_subvector undef,
                   (v4f32 (scalar_to_vector (loadf32 addr:$src))), (iPTR 0)))),
            (SUBREG_TO_REG (i32 0), (VMOVSSZrm addr:$src), sub_xmm)>;
  def : Pat<(v16f32 (X86vzload addr:$src)),
            (SUBREG_TO_REG (i32 0), (VMOVSSZrm addr:$src), sub_xmm)>;
  def : Pat<(v8f64 (X86vzmovl (insert_subvector undef,
                   (v2f64 (scalar_to_vector (loadf64 addr:$src))), (iPTR 0)))),
            (SUBREG_TO_REG (i32 0), (VMOVSDZrm addr:$src), sub_xmm)>;
  def : Pat<(v8f64 (X86vzload addr:$src)),
            (SUBREG_TO_REG (i32 0), (VMOVSDZrm addr:$src), sub_xmm)>;

  def : Pat<(v4i64 (X86vzmovl (insert_subvector undef,
                   (v2i64 (scalar_to_vector (loadi64 addr:$src))), (iPTR 0)))),
            (SUBREG_TO_REG (i64 0), (v2i64 (VMOVQI2PQIZrm addr:$src)), sub_xmm)>;

  // Extract and store.
  def : Pat<(store (f32 (extractelt (v4f32 VR128X:$src), (iPTR 0))),
                   addr:$dst),
            (VMOVSSZmr addr:$dst, (COPY_TO_REGCLASS (v4f32 VR128X:$src), FR32X))>;
}

let ExeDomain = SSEPackedInt, SchedRW = [SchedWriteVecLogic.XMM] in {
def VMOVZPQILo2PQIZrr : AVX512XSI<0x7E, MRMSrcReg, (outs VR128X:$dst),
                                (ins VR128X:$src),
                                "vmovq\t{$src, $dst|$dst, $src}",
                                [(set VR128X:$dst, (v2i64 (X86vzmovl
                                                   (v2i64 VR128X:$src))))]>,
                                EVEX, VEX_W;
}

let Predicates = [HasAVX512] in {
  def : Pat<(v4i32 (X86vzmovl (v4i32 (scalar_to_vector GR32:$src)))),
            (VMOVDI2PDIZrr GR32:$src)>;

  def : Pat<(v2i64 (X86vzmovl (v2i64 (scalar_to_vector GR64:$src)))),
            (VMOV64toPQIZrr GR64:$src)>;

  def : Pat<(v4i64 (X86vzmovl (insert_subvector undef,
                               (v2i64 (scalar_to_vector GR64:$src)),(iPTR 0)))),
            (SUBREG_TO_REG (i64 0), (v2i64 (VMOV64toPQIZrr GR64:$src)), sub_xmm)>;

  def : Pat<(v8i64 (X86vzmovl (insert_subvector undef,
                               (v2i64 (scalar_to_vector GR64:$src)),(iPTR 0)))),
            (SUBREG_TO_REG (i64 0), (v2i64 (VMOV64toPQIZrr GR64:$src)), sub_xmm)>;

  // AVX 128-bit movd/movq instruction write zeros in the high 128-bit part.
  def : Pat<(v2i64 (X86vzmovl (v2i64 (scalar_to_vector (zextloadi64i32 addr:$src))))),
            (VMOVDI2PDIZrm addr:$src)>;
  def : Pat<(v4i32 (X86vzmovl (v4i32 (scalar_to_vector (loadi32 addr:$src))))),
            (VMOVDI2PDIZrm addr:$src)>;
  def : Pat<(v4i32 (X86vzmovl (loadv4i32 addr:$src))),
            (VMOVDI2PDIZrm addr:$src)>;
  def : Pat<(v4i32 (X86vzload addr:$src)),
            (VMOVDI2PDIZrm addr:$src)>;
  def : Pat<(v8i32 (X86vzload addr:$src)),
            (SUBREG_TO_REG (i32 0), (v4i32 (VMOVDI2PDIZrm addr:$src)), sub_xmm)>;
  def : Pat<(v2i64 (X86vzmovl (loadv2i64 addr:$src))),
            (VMOVQI2PQIZrm addr:$src)>;
  def : Pat<(v2f64 (X86vzmovl (v2f64 VR128X:$src))),
            (VMOVZPQILo2PQIZrr VR128X:$src)>;
  def : Pat<(v2i64 (X86vzload addr:$src)),
            (VMOVQI2PQIZrm addr:$src)>;
  def : Pat<(v4i64 (X86vzload addr:$src)),
            (SUBREG_TO_REG (i64 0), (v2i64 (VMOVQI2PQIZrm addr:$src)), sub_xmm)>;

  // Use regular 128-bit instructions to match 256-bit scalar_to_vec+zext.
  def : Pat<(v8i32 (X86vzmovl (insert_subvector undef,
                               (v4i32 (scalar_to_vector GR32:$src)),(iPTR 0)))),
            (SUBREG_TO_REG (i32 0), (v4i32 (VMOVDI2PDIZrr GR32:$src)), sub_xmm)>;
  def : Pat<(v16i32 (X86vzmovl (insert_subvector undef,
                                (v4i32 (scalar_to_vector GR32:$src)),(iPTR 0)))),
            (SUBREG_TO_REG (i32 0), (v4i32 (VMOVDI2PDIZrr GR32:$src)), sub_xmm)>;

  // Use regular 128-bit instructions to match 512-bit scalar_to_vec+zext.
  def : Pat<(v16i32 (X86vzload addr:$src)),
            (SUBREG_TO_REG (i32 0), (v4i32 (VMOVDI2PDIZrm addr:$src)), sub_xmm)>;
  def : Pat<(v8i64 (X86vzload addr:$src)),
            (SUBREG_TO_REG (i64 0), (v2i64 (VMOVQI2PQIZrm addr:$src)), sub_xmm)>;
}

//===----------------------------------------------------------------------===//
// AVX-512 - Non-temporals
//===----------------------------------------------------------------------===//

def VMOVNTDQAZrm : AVX512PI<0x2A, MRMSrcMem, (outs VR512:$dst),
                      (ins i512mem:$src), "vmovntdqa\t{$src, $dst|$dst, $src}",
                      [], SSEPackedInt>, Sched<[SchedWriteVecMoveLS.ZMM.RM]>,
                      EVEX, T8PD, EVEX_V512, EVEX_CD8<64, CD8VF>;

let Predicates = [HasVLX] in {
  def VMOVNTDQAZ256rm : AVX512PI<0x2A, MRMSrcMem, (outs VR256X:$dst),
                       (ins i256mem:$src),
                       "vmovntdqa\t{$src, $dst|$dst, $src}",
                       [], SSEPackedInt>, Sched<[SchedWriteVecMoveLS.YMM.RM]>,
                       EVEX, T8PD, EVEX_V256, EVEX_CD8<64, CD8VF>;

  def VMOVNTDQAZ128rm : AVX512PI<0x2A, MRMSrcMem, (outs VR128X:$dst),
                      (ins i128mem:$src),
                      "vmovntdqa\t{$src, $dst|$dst, $src}",
                      [], SSEPackedInt>, Sched<[SchedWriteVecMoveLS.XMM.RM]>,
                      EVEX, T8PD, EVEX_V128, EVEX_CD8<64, CD8VF>;
}

multiclass avx512_movnt<bits<8> opc, string OpcodeStr, X86VectorVTInfo _,
                        X86SchedWriteMoveLS Sched,
                        PatFrag st_frag = alignednontemporalstore> {
  let SchedRW = [Sched.MR], AddedComplexity = 400 in
  def mr : AVX512PI<opc, MRMDestMem, (outs), (ins _.MemOp:$dst, _.RC:$src),
                    !strconcat(OpcodeStr, "\t{$src, $dst|$dst, $src}"),
                    [(st_frag (_.VT _.RC:$src), addr:$dst)],
                    _.ExeDomain>, EVEX, EVEX_CD8<_.EltSize, CD8VF>;
}

multiclass avx512_movnt_vl<bits<8> opc, string OpcodeStr,
                           AVX512VLVectorVTInfo VTInfo,
                           X86SchedWriteMoveLSWidths Sched> {
  let Predicates = [HasAVX512] in
    defm Z : avx512_movnt<opc, OpcodeStr, VTInfo.info512, Sched.ZMM>, EVEX_V512;

  let Predicates = [HasAVX512, HasVLX] in {
    defm Z256 : avx512_movnt<opc, OpcodeStr, VTInfo.info256, Sched.YMM>, EVEX_V256;
    defm Z128 : avx512_movnt<opc, OpcodeStr, VTInfo.info128, Sched.XMM>, EVEX_V128;
  }
}

defm VMOVNTDQ : avx512_movnt_vl<0xE7, "vmovntdq", avx512vl_i64_info,
                                SchedWriteVecMoveLSNT>, PD;
defm VMOVNTPD : avx512_movnt_vl<0x2B, "vmovntpd", avx512vl_f64_info,
                                SchedWriteFMoveLSNT>, PD, VEX_W;
defm VMOVNTPS : avx512_movnt_vl<0x2B, "vmovntps", avx512vl_f32_info,
                                SchedWriteFMoveLSNT>, PS;

let Predicates = [HasAVX512], AddedComplexity = 400 in {
  def : Pat<(alignednontemporalstore (v16i32 VR512:$src), addr:$dst),
            (VMOVNTDQZmr addr:$dst, VR512:$src)>;
  def : Pat<(alignednontemporalstore (v32i16 VR512:$src), addr:$dst),
            (VMOVNTDQZmr addr:$dst, VR512:$src)>;
  def : Pat<(alignednontemporalstore (v64i8 VR512:$src), addr:$dst),
            (VMOVNTDQZmr addr:$dst, VR512:$src)>;

  def : Pat<(v8f64 (alignednontemporalload addr:$src)),
            (VMOVNTDQAZrm addr:$src)>;
  def : Pat<(v16f32 (alignednontemporalload addr:$src)),
            (VMOVNTDQAZrm addr:$src)>;
  def : Pat<(v8i64 (alignednontemporalload addr:$src)),
            (VMOVNTDQAZrm addr:$src)>;
  def : Pat<(v16i32 (alignednontemporalload addr:$src)),
            (VMOVNTDQAZrm addr:$src)>;
  def : Pat<(v32i16 (alignednontemporalload addr:$src)),
            (VMOVNTDQAZrm addr:$src)>;
  def : Pat<(v64i8 (alignednontemporalload addr:$src)),
            (VMOVNTDQAZrm addr:$src)>;
}

let Predicates = [HasVLX], AddedComplexity = 400 in {
  def : Pat<(alignednontemporalstore (v8i32 VR256X:$src), addr:$dst),
            (VMOVNTDQZ256mr addr:$dst, VR256X:$src)>;
  def : Pat<(alignednontemporalstore (v16i16 VR256X:$src), addr:$dst),
            (VMOVNTDQZ256mr addr:$dst, VR256X:$src)>;
  def : Pat<(alignednontemporalstore (v32i8 VR256X:$src), addr:$dst),
            (VMOVNTDQZ256mr addr:$dst, VR256X:$src)>;

  def : Pat<(v4f64 (alignednontemporalload addr:$src)),
            (VMOVNTDQAZ256rm addr:$src)>;
  def : Pat<(v8f32 (alignednontemporalload addr:$src)),
            (VMOVNTDQAZ256rm addr:$src)>;
  def : Pat<(v4i64 (alignednontemporalload addr:$src)),
            (VMOVNTDQAZ256rm addr:$src)>;
  def : Pat<(v8i32 (alignednontemporalload addr:$src)),
            (VMOVNTDQAZ256rm addr:$src)>;
  def : Pat<(v16i16 (alignednontemporalload addr:$src)),
            (VMOVNTDQAZ256rm addr:$src)>;
  def : Pat<(v32i8 (alignednontemporalload addr:$src)),
            (VMOVNTDQAZ256rm addr:$src)>;

  def : Pat<(alignednontemporalstore (v4i32 VR128X:$src), addr:$dst),
            (VMOVNTDQZ128mr addr:$dst, VR128X:$src)>;
  def : Pat<(alignednontemporalstore (v8i16 VR128X:$src), addr:$dst),
            (VMOVNTDQZ128mr addr:$dst, VR128X:$src)>;
  def : Pat<(alignednontemporalstore (v16i8 VR128X:$src), addr:$dst),
            (VMOVNTDQZ128mr addr:$dst, VR128X:$src)>;

  def : Pat<(v2f64 (alignednontemporalload addr:$src)),
            (VMOVNTDQAZ128rm addr:$src)>;
  def : Pat<(v4f32 (alignednontemporalload addr:$src)),
            (VMOVNTDQAZ128rm addr:$src)>;
  def : Pat<(v2i64 (alignednontemporalload addr:$src)),
            (VMOVNTDQAZ128rm addr:$src)>;
  def : Pat<(v4i32 (alignednontemporalload addr:$src)),
            (VMOVNTDQAZ128rm addr:$src)>;
  def : Pat<(v8i16 (alignednontemporalload addr:$src)),
            (VMOVNTDQAZ128rm addr:$src)>;
  def : Pat<(v16i8 (alignednontemporalload addr:$src)),
            (VMOVNTDQAZ128rm addr:$src)>;
}

//===----------------------------------------------------------------------===//
// AVX-512 - Integer arithmetic
//
multiclass avx512_binop_rm<bits<8> opc, string OpcodeStr, SDNode OpNode,
                           X86VectorVTInfo _, X86FoldableSchedWrite sched,
                           bit IsCommutable = 0> {
  defm rr : AVX512_maskable<opc, MRMSrcReg, _, (outs _.RC:$dst),
                    (ins _.RC:$src1, _.RC:$src2), OpcodeStr,
                    "$src2, $src1", "$src1, $src2",
                    (_.VT (OpNode _.RC:$src1, _.RC:$src2)),
                    IsCommutable>, AVX512BIBase, EVEX_4V,
                    Sched<[sched]>;

  defm rm : AVX512_maskable<opc, MRMSrcMem, _, (outs _.RC:$dst),
                  (ins _.RC:$src1, _.MemOp:$src2), OpcodeStr,
                  "$src2, $src1", "$src1, $src2",
                  (_.VT (OpNode _.RC:$src1, (_.LdFrag addr:$src2)))>,
                  AVX512BIBase, EVEX_4V,
                  Sched<[sched.Folded, sched.ReadAfterFold]>;
}

multiclass avx512_binop_rmb<bits<8> opc, string OpcodeStr, SDNode OpNode,
                            X86VectorVTInfo _, X86FoldableSchedWrite sched,
                            bit IsCommutable = 0> :
           avx512_binop_rm<opc, OpcodeStr, OpNode, _, sched, IsCommutable> {
  defm rmb : AVX512_maskable<opc, MRMSrcMem, _, (outs _.RC:$dst),
                  (ins _.RC:$src1, _.ScalarMemOp:$src2), OpcodeStr,
                  "${src2}"##_.BroadcastStr##", $src1",
                  "$src1, ${src2}"##_.BroadcastStr,
                  (_.VT (OpNode _.RC:$src1,
                                (X86VBroadcast
                                    (_.ScalarLdFrag addr:$src2))))>,
                  AVX512BIBase, EVEX_4V, EVEX_B,
                  Sched<[sched.Folded, sched.ReadAfterFold]>;
}

multiclass avx512_binop_rm_vl<bits<8> opc, string OpcodeStr, SDNode OpNode,
                              AVX512VLVectorVTInfo VTInfo,
                              X86SchedWriteWidths sched, Predicate prd,
                              bit IsCommutable = 0> {
  let Predicates = [prd] in
    defm Z : avx512_binop_rm<opc, OpcodeStr, OpNode, VTInfo.info512, sched.ZMM,
                             IsCommutable>, EVEX_V512;

  let Predicates = [prd, HasVLX] in {
    defm Z256 : avx512_binop_rm<opc, OpcodeStr, OpNode, VTInfo.info256,
                                sched.YMM, IsCommutable>, EVEX_V256;
    defm Z128 : avx512_binop_rm<opc, OpcodeStr, OpNode, VTInfo.info128,
                                sched.XMM, IsCommutable>, EVEX_V128;
  }
}

multiclass avx512_binop_rmb_vl<bits<8> opc, string OpcodeStr, SDNode OpNode,
                               AVX512VLVectorVTInfo VTInfo,
                               X86SchedWriteWidths sched, Predicate prd,
                               bit IsCommutable = 0> {
  let Predicates = [prd] in
    defm Z : avx512_binop_rmb<opc, OpcodeStr, OpNode, VTInfo.info512, sched.ZMM,
                             IsCommutable>, EVEX_V512;

  let Predicates = [prd, HasVLX] in {
    defm Z256 : avx512_binop_rmb<opc, OpcodeStr, OpNode, VTInfo.info256,
                                 sched.YMM, IsCommutable>, EVEX_V256;
    defm Z128 : avx512_binop_rmb<opc, OpcodeStr, OpNode, VTInfo.info128,
                                 sched.XMM, IsCommutable>, EVEX_V128;
  }
}

multiclass avx512_binop_rm_vl_q<bits<8> opc, string OpcodeStr, SDNode OpNode,
                                X86SchedWriteWidths sched, Predicate prd,
                                bit IsCommutable = 0> {
  defm NAME : avx512_binop_rmb_vl<opc, OpcodeStr, OpNode, avx512vl_i64_info,
                                  sched, prd, IsCommutable>,
                                  VEX_W, EVEX_CD8<64, CD8VF>;
}

multiclass avx512_binop_rm_vl_d<bits<8> opc, string OpcodeStr, SDNode OpNode,
                                X86SchedWriteWidths sched, Predicate prd,
                                bit IsCommutable = 0> {
  defm NAME : avx512_binop_rmb_vl<opc, OpcodeStr, OpNode, avx512vl_i32_info,
                                  sched, prd, IsCommutable>, EVEX_CD8<32, CD8VF>;
}

multiclass avx512_binop_rm_vl_w<bits<8> opc, string OpcodeStr, SDNode OpNode,
                                X86SchedWriteWidths sched, Predicate prd,
                                bit IsCommutable = 0> {
  defm NAME : avx512_binop_rm_vl<opc, OpcodeStr, OpNode, avx512vl_i16_info,
                                 sched, prd, IsCommutable>, EVEX_CD8<16, CD8VF>,
                                 VEX_WIG;
}

multiclass avx512_binop_rm_vl_b<bits<8> opc, string OpcodeStr, SDNode OpNode,
                                X86SchedWriteWidths sched, Predicate prd,
                                bit IsCommutable = 0> {
  defm NAME : avx512_binop_rm_vl<opc, OpcodeStr, OpNode, avx512vl_i8_info,
                                 sched, prd, IsCommutable>, EVEX_CD8<8, CD8VF>,
                                 VEX_WIG;
}

multiclass avx512_binop_rm_vl_dq<bits<8> opc_d, bits<8> opc_q, string OpcodeStr,
                                 SDNode OpNode, X86SchedWriteWidths sched,
                                 Predicate prd, bit IsCommutable = 0> {
  defm Q : avx512_binop_rm_vl_q<opc_q, OpcodeStr#"q", OpNode, sched, prd,
                                   IsCommutable>;

  defm D : avx512_binop_rm_vl_d<opc_d, OpcodeStr#"d", OpNode, sched, prd,
                                   IsCommutable>;
}

multiclass avx512_binop_rm_vl_bw<bits<8> opc_b, bits<8> opc_w, string OpcodeStr,
                                 SDNode OpNode, X86SchedWriteWidths sched,
                                 Predicate prd, bit IsCommutable = 0> {
  defm W : avx512_binop_rm_vl_w<opc_w, OpcodeStr#"w", OpNode, sched, prd,
                                   IsCommutable>;

  defm B : avx512_binop_rm_vl_b<opc_b, OpcodeStr#"b", OpNode, sched, prd,
                                   IsCommutable>;
}

multiclass avx512_binop_rm_vl_all<bits<8> opc_b, bits<8> opc_w,
                                  bits<8> opc_d, bits<8> opc_q,
                                  string OpcodeStr, SDNode OpNode,
                                  X86SchedWriteWidths sched,
                                  bit IsCommutable = 0> {
  defm NAME : avx512_binop_rm_vl_dq<opc_d, opc_q, OpcodeStr, OpNode,
                                    sched, HasAVX512, IsCommutable>,
              avx512_binop_rm_vl_bw<opc_b, opc_w, OpcodeStr, OpNode,
                                    sched, HasBWI, IsCommutable>;
}

multiclass avx512_binop_rm2<bits<8> opc, string OpcodeStr,
                            X86FoldableSchedWrite sched,
                            SDNode OpNode,X86VectorVTInfo _Src,
                            X86VectorVTInfo _Dst, X86VectorVTInfo _Brdct,
                            bit IsCommutable = 0> {
  defm rr : AVX512_maskable<opc, MRMSrcReg, _Dst, (outs _Dst.RC:$dst),
                            (ins _Src.RC:$src1, _Src.RC:$src2), OpcodeStr,
                            "$src2, $src1","$src1, $src2",
                            (_Dst.VT (OpNode
                                         (_Src.VT _Src.RC:$src1),
                                         (_Src.VT _Src.RC:$src2))),
                            IsCommutable>,
                            AVX512BIBase, EVEX_4V, Sched<[sched]>;
  defm rm : AVX512_maskable<opc, MRMSrcMem, _Dst, (outs _Dst.RC:$dst),
                        (ins _Src.RC:$src1, _Src.MemOp:$src2), OpcodeStr,
                        "$src2, $src1", "$src1, $src2",
                        (_Dst.VT (OpNode (_Src.VT _Src.RC:$src1),
                                      (_Src.LdFrag addr:$src2)))>,
                        AVX512BIBase, EVEX_4V,
                        Sched<[sched.Folded, sched.ReadAfterFold]>;

  defm rmb : AVX512_maskable<opc, MRMSrcMem, _Dst, (outs _Dst.RC:$dst),
                    (ins _Src.RC:$src1, _Brdct.ScalarMemOp:$src2),
                    OpcodeStr,
                    "${src2}"##_Brdct.BroadcastStr##", $src1",
                     "$src1, ${src2}"##_Brdct.BroadcastStr,
                    (_Dst.VT (OpNode (_Src.VT _Src.RC:$src1), (bitconvert
                                 (_Brdct.VT (X86VBroadcast
                                          (_Brdct.ScalarLdFrag addr:$src2))))))>,
                    AVX512BIBase, EVEX_4V, EVEX_B,
                    Sched<[sched.Folded, sched.ReadAfterFold]>;
}

defm VPADD : avx512_binop_rm_vl_all<0xFC, 0xFD, 0xFE, 0xD4, "vpadd", add,
                                    SchedWriteVecALU, 1>;
defm VPSUB : avx512_binop_rm_vl_all<0xF8, 0xF9, 0xFA, 0xFB, "vpsub", sub,
                                    SchedWriteVecALU, 0>;
defm VPADDS : avx512_binop_rm_vl_bw<0xEC, 0xED, "vpadds", saddsat,
                                    SchedWriteVecALU, HasBWI, 1>;
defm VPSUBS : avx512_binop_rm_vl_bw<0xE8, 0xE9, "vpsubs", ssubsat,
                                    SchedWriteVecALU, HasBWI, 0>;
defm VPADDUS : avx512_binop_rm_vl_bw<0xDC, 0xDD, "vpaddus", uaddsat,
                                     SchedWriteVecALU, HasBWI, 1>;
defm VPSUBUS : avx512_binop_rm_vl_bw<0xD8, 0xD9, "vpsubus", usubsat,
                                     SchedWriteVecALU, HasBWI, 0>;
defm VPMULLD : avx512_binop_rm_vl_d<0x40, "vpmulld", mul,
                                    SchedWritePMULLD, HasAVX512, 1>, T8PD;
defm VPMULLW : avx512_binop_rm_vl_w<0xD5, "vpmullw", mul,
                                    SchedWriteVecIMul, HasBWI, 1>;
defm VPMULLQ : avx512_binop_rm_vl_q<0x40, "vpmullq", mul,
                                    SchedWriteVecIMul, HasDQI, 1>, T8PD,
                                    NotEVEX2VEXConvertible;
defm VPMULHW : avx512_binop_rm_vl_w<0xE5, "vpmulhw", mulhs, SchedWriteVecIMul,
                                    HasBWI, 1>;
defm VPMULHUW : avx512_binop_rm_vl_w<0xE4, "vpmulhuw", mulhu, SchedWriteVecIMul,
                                     HasBWI, 1>;
defm VPMULHRSW : avx512_binop_rm_vl_w<0x0B, "vpmulhrsw", X86mulhrs,
                                      SchedWriteVecIMul, HasBWI, 1>, T8PD;
defm VPAVG : avx512_binop_rm_vl_bw<0xE0, 0xE3, "vpavg", X86avg,
                                   SchedWriteVecALU, HasBWI, 1>;
defm VPMULDQ : avx512_binop_rm_vl_q<0x28, "vpmuldq", X86pmuldq,
                                    SchedWriteVecIMul, HasAVX512, 1>, T8PD;
defm VPMULUDQ : avx512_binop_rm_vl_q<0xF4, "vpmuludq", X86pmuludq,
                                     SchedWriteVecIMul, HasAVX512, 1>;

multiclass avx512_binop_all<bits<8> opc, string OpcodeStr,
                            X86SchedWriteWidths sched,
                            AVX512VLVectorVTInfo _SrcVTInfo,
                            AVX512VLVectorVTInfo _DstVTInfo,
                            SDNode OpNode, Predicate prd,  bit IsCommutable = 0> {
  let Predicates = [prd] in
    defm NAME#Z : avx512_binop_rm2<opc, OpcodeStr, sched.ZMM, OpNode,
                                 _SrcVTInfo.info512, _DstVTInfo.info512,
                                 v8i64_info, IsCommutable>,
                                  EVEX_V512, EVEX_CD8<64, CD8VF>, VEX_W;
  let Predicates = [HasVLX, prd] in {
    defm NAME#Z256 : avx512_binop_rm2<opc, OpcodeStr, sched.YMM, OpNode,
                                      _SrcVTInfo.info256, _DstVTInfo.info256,
                                      v4i64x_info, IsCommutable>,
                                      EVEX_V256, EVEX_CD8<64, CD8VF>, VEX_W;
    defm NAME#Z128 : avx512_binop_rm2<opc, OpcodeStr, sched.XMM, OpNode,
                                      _SrcVTInfo.info128, _DstVTInfo.info128,
                                      v2i64x_info, IsCommutable>,
                                     EVEX_V128, EVEX_CD8<64, CD8VF>, VEX_W;
  }
}

defm VPMULTISHIFTQB : avx512_binop_all<0x83, "vpmultishiftqb", SchedWriteVecALU,
                                avx512vl_i8_info, avx512vl_i8_info,
                                X86multishift, HasVBMI, 0>, T8PD;

multiclass avx512_packs_rmb<bits<8> opc, string OpcodeStr, SDNode OpNode,
                            X86VectorVTInfo _Src, X86VectorVTInfo _Dst,
                            X86FoldableSchedWrite sched> {
  defm rmb : AVX512_maskable<opc, MRMSrcMem, _Dst, (outs _Dst.RC:$dst),
                    (ins _Src.RC:$src1, _Src.ScalarMemOp:$src2),
                    OpcodeStr,
                    "${src2}"##_Src.BroadcastStr##", $src1",
                     "$src1, ${src2}"##_Src.BroadcastStr,
                    (_Dst.VT (OpNode (_Src.VT _Src.RC:$src1), (bitconvert
                                 (_Src.VT (X86VBroadcast
                                          (_Src.ScalarLdFrag addr:$src2))))))>,
                    EVEX_4V, EVEX_B, EVEX_CD8<_Src.EltSize, CD8VF>,
                    Sched<[sched.Folded, sched.ReadAfterFold]>;
}

multiclass avx512_packs_rm<bits<8> opc, string OpcodeStr,
                            SDNode OpNode,X86VectorVTInfo _Src,
                            X86VectorVTInfo _Dst, X86FoldableSchedWrite sched,
                            bit IsCommutable = 0> {
  defm rr : AVX512_maskable<opc, MRMSrcReg, _Dst, (outs _Dst.RC:$dst),
                            (ins _Src.RC:$src1, _Src.RC:$src2), OpcodeStr,
                            "$src2, $src1","$src1, $src2",
                            (_Dst.VT (OpNode
                                         (_Src.VT _Src.RC:$src1),
                                         (_Src.VT _Src.RC:$src2))),
                            IsCommutable>,
                            EVEX_CD8<_Src.EltSize, CD8VF>, EVEX_4V, Sched<[sched]>;
  defm rm : AVX512_maskable<opc, MRMSrcMem, _Dst, (outs _Dst.RC:$dst),
                        (ins _Src.RC:$src1, _Src.MemOp:$src2), OpcodeStr,
                        "$src2, $src1", "$src1, $src2",
                        (_Dst.VT (OpNode (_Src.VT _Src.RC:$src1),
                                      (_Src.LdFrag addr:$src2)))>,
                         EVEX_4V, EVEX_CD8<_Src.EltSize, CD8VF>,
                         Sched<[sched.Folded, sched.ReadAfterFold]>;
}

multiclass avx512_packs_all_i32_i16<bits<8> opc, string OpcodeStr,
                                    SDNode OpNode> {
  let Predicates = [HasBWI] in
  defm NAME#Z : avx512_packs_rm<opc, OpcodeStr, OpNode, v16i32_info,
                                 v32i16_info, SchedWriteShuffle.ZMM>,
                avx512_packs_rmb<opc, OpcodeStr, OpNode, v16i32_info,
                                 v32i16_info, SchedWriteShuffle.ZMM>, EVEX_V512;
  let Predicates = [HasBWI, HasVLX] in {
    defm NAME#Z256 : avx512_packs_rm<opc, OpcodeStr, OpNode, v8i32x_info,
                                     v16i16x_info, SchedWriteShuffle.YMM>,
                     avx512_packs_rmb<opc, OpcodeStr, OpNode, v8i32x_info,
                                      v16i16x_info, SchedWriteShuffle.YMM>,
                                      EVEX_V256;
    defm NAME#Z128 : avx512_packs_rm<opc, OpcodeStr, OpNode, v4i32x_info,
                                     v8i16x_info, SchedWriteShuffle.XMM>,
                     avx512_packs_rmb<opc, OpcodeStr, OpNode, v4i32x_info,
                                      v8i16x_info, SchedWriteShuffle.XMM>,
                                      EVEX_V128;
  }
}
multiclass avx512_packs_all_i16_i8<bits<8> opc, string OpcodeStr,
                            SDNode OpNode> {
  let Predicates = [HasBWI] in
  defm NAME#Z : avx512_packs_rm<opc, OpcodeStr, OpNode, v32i16_info, v64i8_info,
                                SchedWriteShuffle.ZMM>, EVEX_V512, VEX_WIG;
  let Predicates = [HasBWI, HasVLX] in {
    defm NAME#Z256 : avx512_packs_rm<opc, OpcodeStr, OpNode, v16i16x_info,
                                     v32i8x_info, SchedWriteShuffle.YMM>,
                                     EVEX_V256, VEX_WIG;
    defm NAME#Z128 : avx512_packs_rm<opc, OpcodeStr, OpNode, v8i16x_info,
                                     v16i8x_info, SchedWriteShuffle.XMM>,
                                     EVEX_V128, VEX_WIG;
  }
}

multiclass avx512_vpmadd<bits<8> opc, string OpcodeStr,
                            SDNode OpNode, AVX512VLVectorVTInfo _Src,
                            AVX512VLVectorVTInfo _Dst, bit IsCommutable = 0> {
  let Predicates = [HasBWI] in
  defm NAME#Z : avx512_packs_rm<opc, OpcodeStr, OpNode, _Src.info512,
                                _Dst.info512, SchedWriteVecIMul.ZMM,
                                IsCommutable>, EVEX_V512;
  let Predicates = [HasBWI, HasVLX] in {
    defm NAME#Z256 : avx512_packs_rm<opc, OpcodeStr, OpNode, _Src.info256,
                                     _Dst.info256, SchedWriteVecIMul.YMM,
                                     IsCommutable>, EVEX_V256;
    defm NAME#Z128 : avx512_packs_rm<opc, OpcodeStr, OpNode, _Src.info128,
                                     _Dst.info128, SchedWriteVecIMul.XMM,
                                     IsCommutable>, EVEX_V128;
  }
}

defm VPACKSSDW : avx512_packs_all_i32_i16<0x6B, "vpackssdw", X86Packss>, AVX512BIBase;
defm VPACKUSDW : avx512_packs_all_i32_i16<0x2b, "vpackusdw", X86Packus>, AVX5128IBase;
defm VPACKSSWB : avx512_packs_all_i16_i8 <0x63, "vpacksswb", X86Packss>, AVX512BIBase;
defm VPACKUSWB : avx512_packs_all_i16_i8 <0x67, "vpackuswb", X86Packus>, AVX512BIBase;

defm VPMADDUBSW : avx512_vpmadd<0x04, "vpmaddubsw", X86vpmaddubsw,
                     avx512vl_i8_info, avx512vl_i16_info>, AVX512BIBase, T8PD, VEX_WIG;
defm VPMADDWD   : avx512_vpmadd<0xF5, "vpmaddwd", X86vpmaddwd,
                     avx512vl_i16_info, avx512vl_i32_info, 1>, AVX512BIBase, VEX_WIG;

defm VPMAXSB : avx512_binop_rm_vl_b<0x3C, "vpmaxsb", smax,
                                    SchedWriteVecALU, HasBWI, 1>, T8PD;
defm VPMAXSW : avx512_binop_rm_vl_w<0xEE, "vpmaxsw", smax,
                                    SchedWriteVecALU, HasBWI, 1>;
defm VPMAXSD : avx512_binop_rm_vl_d<0x3D, "vpmaxsd", smax,
                                    SchedWriteVecALU, HasAVX512, 1>, T8PD;
defm VPMAXSQ : avx512_binop_rm_vl_q<0x3D, "vpmaxsq", smax,
                                    SchedWriteVecALU, HasAVX512, 1>, T8PD,
                                    NotEVEX2VEXConvertible;

defm VPMAXUB : avx512_binop_rm_vl_b<0xDE, "vpmaxub", umax,
                                    SchedWriteVecALU, HasBWI, 1>;
defm VPMAXUW : avx512_binop_rm_vl_w<0x3E, "vpmaxuw", umax,
                                    SchedWriteVecALU, HasBWI, 1>, T8PD;
defm VPMAXUD : avx512_binop_rm_vl_d<0x3F, "vpmaxud", umax,
                                    SchedWriteVecALU, HasAVX512, 1>, T8PD;
defm VPMAXUQ : avx512_binop_rm_vl_q<0x3F, "vpmaxuq", umax,
                                    SchedWriteVecALU, HasAVX512, 1>, T8PD,
                                    NotEVEX2VEXConvertible;

defm VPMINSB : avx512_binop_rm_vl_b<0x38, "vpminsb", smin,
                                    SchedWriteVecALU, HasBWI, 1>, T8PD;
defm VPMINSW : avx512_binop_rm_vl_w<0xEA, "vpminsw", smin,
                                    SchedWriteVecALU, HasBWI, 1>;
defm VPMINSD : avx512_binop_rm_vl_d<0x39, "vpminsd", smin,
                                    SchedWriteVecALU, HasAVX512, 1>, T8PD;
defm VPMINSQ : avx512_binop_rm_vl_q<0x39, "vpminsq", smin,
                                    SchedWriteVecALU, HasAVX512, 1>, T8PD,
                                    NotEVEX2VEXConvertible;

defm VPMINUB : avx512_binop_rm_vl_b<0xDA, "vpminub", umin,
                                    SchedWriteVecALU, HasBWI, 1>;
defm VPMINUW : avx512_binop_rm_vl_w<0x3A, "vpminuw", umin,
                                    SchedWriteVecALU, HasBWI, 1>, T8PD;
defm VPMINUD : avx512_binop_rm_vl_d<0x3B, "vpminud", umin,
                                    SchedWriteVecALU, HasAVX512, 1>, T8PD;
defm VPMINUQ : avx512_binop_rm_vl_q<0x3B, "vpminuq", umin,
                                    SchedWriteVecALU, HasAVX512, 1>, T8PD,
                                    NotEVEX2VEXConvertible;

// PMULLQ: Use 512bit version to implement 128/256 bit in case NoVLX.
let Predicates = [HasDQI, NoVLX] in {
  def : Pat<(v4i64 (mul (v4i64 VR256X:$src1), (v4i64 VR256X:$src2))),
            (EXTRACT_SUBREG
                (VPMULLQZrr
                    (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR256X:$src1, sub_ymm),
                    (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR256X:$src2, sub_ymm)),
             sub_ymm)>;

  def : Pat<(v2i64 (mul (v2i64 VR128X:$src1), (v2i64 VR128X:$src2))),
            (EXTRACT_SUBREG
                (VPMULLQZrr
                    (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR128X:$src1, sub_xmm),
                    (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR128X:$src2, sub_xmm)),
             sub_xmm)>;
}

// PMULLQ: Use 512bit version to implement 128/256 bit in case NoVLX.
let Predicates = [HasDQI, NoVLX] in {
  def : Pat<(v4i64 (mul (v4i64 VR256X:$src1), (v4i64 VR256X:$src2))),
            (EXTRACT_SUBREG
                (VPMULLQZrr
                    (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR256X:$src1, sub_ymm),
                    (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR256X:$src2, sub_ymm)),
             sub_ymm)>;

  def : Pat<(v2i64 (mul (v2i64 VR128X:$src1), (v2i64 VR128X:$src2))),
            (EXTRACT_SUBREG
                (VPMULLQZrr
                    (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR128X:$src1, sub_xmm),
                    (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR128X:$src2, sub_xmm)),
             sub_xmm)>;
}

multiclass avx512_min_max_lowering<Instruction Instr, SDNode OpNode> {
  def : Pat<(v4i64 (OpNode VR256X:$src1, VR256X:$src2)),
            (EXTRACT_SUBREG
                (Instr
                    (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR256X:$src1, sub_ymm),
                    (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR256X:$src2, sub_ymm)),
             sub_ymm)>;

  def : Pat<(v2i64 (OpNode VR128X:$src1, VR128X:$src2)),
            (EXTRACT_SUBREG
                (Instr
                    (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR128X:$src1, sub_xmm),
                    (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR128X:$src2, sub_xmm)),
             sub_xmm)>;
}

let Predicates = [HasAVX512, NoVLX] in {
  defm : avx512_min_max_lowering<VPMAXUQZrr, umax>;
  defm : avx512_min_max_lowering<VPMINUQZrr, umin>;
  defm : avx512_min_max_lowering<VPMAXSQZrr, smax>;
  defm : avx512_min_max_lowering<VPMINSQZrr, smin>;
}

//===----------------------------------------------------------------------===//
// AVX-512  Logical Instructions
//===----------------------------------------------------------------------===//

defm VPAND : avx512_binop_rm_vl_dq<0xDB, 0xDB, "vpand", and,
                                   SchedWriteVecLogic, HasAVX512, 1>;
defm VPOR : avx512_binop_rm_vl_dq<0xEB, 0xEB, "vpor", or,
                                  SchedWriteVecLogic, HasAVX512, 1>;
defm VPXOR : avx512_binop_rm_vl_dq<0xEF, 0xEF, "vpxor", xor,
                                   SchedWriteVecLogic, HasAVX512, 1>;
defm VPANDN : avx512_binop_rm_vl_dq<0xDF, 0xDF, "vpandn", X86andnp,
                                    SchedWriteVecLogic, HasAVX512>;

let Predicates = [HasVLX] in {
  def : Pat<(v16i8 (and VR128X:$src1, VR128X:$src2)),
            (VPANDQZ128rr VR128X:$src1, VR128X:$src2)>;
  def : Pat<(v8i16 (and VR128X:$src1, VR128X:$src2)),
            (VPANDQZ128rr VR128X:$src1, VR128X:$src2)>;

  def : Pat<(v16i8 (or VR128X:$src1, VR128X:$src2)),
            (VPORQZ128rr VR128X:$src1, VR128X:$src2)>;
  def : Pat<(v8i16 (or VR128X:$src1, VR128X:$src2)),
            (VPORQZ128rr VR128X:$src1, VR128X:$src2)>;

  def : Pat<(v16i8 (xor VR128X:$src1, VR128X:$src2)),
            (VPXORQZ128rr VR128X:$src1, VR128X:$src2)>;
  def : Pat<(v8i16 (xor VR128X:$src1, VR128X:$src2)),
            (VPXORQZ128rr VR128X:$src1, VR128X:$src2)>;

  def : Pat<(v16i8 (X86andnp VR128X:$src1, VR128X:$src2)),
            (VPANDNQZ128rr VR128X:$src1, VR128X:$src2)>;
  def : Pat<(v8i16 (X86andnp VR128X:$src1, VR128X:$src2)),
            (VPANDNQZ128rr VR128X:$src1, VR128X:$src2)>;

  def : Pat<(and VR128X:$src1, (loadv16i8 addr:$src2)),
            (VPANDQZ128rm VR128X:$src1, addr:$src2)>;
  def : Pat<(and VR128X:$src1, (loadv8i16 addr:$src2)),
            (VPANDQZ128rm VR128X:$src1, addr:$src2)>;

  def : Pat<(or VR128X:$src1, (loadv16i8 addr:$src2)),
            (VPORQZ128rm VR128X:$src1, addr:$src2)>;
  def : Pat<(or VR128X:$src1, (loadv8i16 addr:$src2)),
            (VPORQZ128rm VR128X:$src1, addr:$src2)>;

  def : Pat<(xor VR128X:$src1, (loadv16i8 addr:$src2)),
            (VPXORQZ128rm VR128X:$src1, addr:$src2)>;
  def : Pat<(xor VR128X:$src1, (loadv8i16 addr:$src2)),
            (VPXORQZ128rm VR128X:$src1, addr:$src2)>;

  def : Pat<(X86andnp VR128X:$src1, (loadv16i8 addr:$src2)),
            (VPANDNQZ128rm VR128X:$src1, addr:$src2)>;
  def : Pat<(X86andnp VR128X:$src1, (loadv8i16 addr:$src2)),
            (VPANDNQZ128rm VR128X:$src1, addr:$src2)>;

  def : Pat<(and VR128X:$src1,
                 (bc_v4i32 (v4f32 (X86VBroadcast (loadf32 addr:$src2))))),
            (VPANDDZ128rmb VR128X:$src1, addr:$src2)>;
  def : Pat<(or VR128X:$src1,
                (bc_v4i32 (v4f32 (X86VBroadcast (loadf32 addr:$src2))))),
            (VPORDZ128rmb VR128X:$src1, addr:$src2)>;
  def : Pat<(xor VR128X:$src1,
                 (bc_v4i32 (v4f32 (X86VBroadcast (loadf32 addr:$src2))))),
            (VPXORDZ128rmb VR128X:$src1, addr:$src2)>;
  def : Pat<(X86andnp VR128X:$src1,
                      (bc_v4i32 (v4f32 (X86VBroadcast (loadf32 addr:$src2))))),
            (VPANDNDZ128rmb VR128X:$src1, addr:$src2)>;

  def : Pat<(and VR128X:$src1,
                 (bc_v2i64 (v2f64 (X86VBroadcast (loadf64 addr:$src2))))),
            (VPANDQZ128rmb VR128X:$src1, addr:$src2)>;
  def : Pat<(or VR128X:$src1,
                (bc_v2i64 (v2f64 (X86VBroadcast (loadf64 addr:$src2))))),
            (VPORQZ128rmb VR128X:$src1, addr:$src2)>;
  def : Pat<(xor VR128X:$src1,
                 (bc_v2i64 (v2f64 (X86VBroadcast (loadf64 addr:$src2))))),
            (VPXORQZ128rmb VR128X:$src1, addr:$src2)>;
  def : Pat<(X86andnp VR128X:$src1,
                      (bc_v2i64 (v2f64 (X86VBroadcast (loadf64 addr:$src2))))),
            (VPANDNQZ128rmb VR128X:$src1, addr:$src2)>;

  def : Pat<(v32i8 (and VR256X:$src1, VR256X:$src2)),
            (VPANDQZ256rr VR256X:$src1, VR256X:$src2)>;
  def : Pat<(v16i16 (and VR256X:$src1, VR256X:$src2)),
            (VPANDQZ256rr VR256X:$src1, VR256X:$src2)>;

  def : Pat<(v32i8 (or VR256X:$src1, VR256X:$src2)),
            (VPORQZ256rr VR256X:$src1, VR256X:$src2)>;
  def : Pat<(v16i16 (or VR256X:$src1, VR256X:$src2)),
            (VPORQZ256rr VR256X:$src1, VR256X:$src2)>;

  def : Pat<(v32i8 (xor VR256X:$src1, VR256X:$src2)),
            (VPXORQZ256rr VR256X:$src1, VR256X:$src2)>;
  def : Pat<(v16i16 (xor VR256X:$src1, VR256X:$src2)),
            (VPXORQZ256rr VR256X:$src1, VR256X:$src2)>;

  def : Pat<(v32i8 (X86andnp VR256X:$src1, VR256X:$src2)),
            (VPANDNQZ256rr VR256X:$src1, VR256X:$src2)>;
  def : Pat<(v16i16 (X86andnp VR256X:$src1, VR256X:$src2)),
            (VPANDNQZ256rr VR256X:$src1, VR256X:$src2)>;

  def : Pat<(and VR256X:$src1, (loadv32i8 addr:$src2)),
            (VPANDQZ256rm VR256X:$src1, addr:$src2)>;
  def : Pat<(and VR256X:$src1, (loadv16i16 addr:$src2)),
            (VPANDQZ256rm VR256X:$src1, addr:$src2)>;

  def : Pat<(or VR256X:$src1, (loadv32i8 addr:$src2)),
            (VPORQZ256rm VR256X:$src1, addr:$src2)>;
  def : Pat<(or VR256X:$src1, (loadv16i16 addr:$src2)),
            (VPORQZ256rm VR256X:$src1, addr:$src2)>;

  def : Pat<(xor VR256X:$src1, (loadv32i8 addr:$src2)),
            (VPXORQZ256rm VR256X:$src1, addr:$src2)>;
  def : Pat<(xor VR256X:$src1, (loadv16i16 addr:$src2)),
            (VPXORQZ256rm VR256X:$src1, addr:$src2)>;

  def : Pat<(X86andnp VR256X:$src1, (loadv32i8 addr:$src2)),
            (VPANDNQZ256rm VR256X:$src1, addr:$src2)>;
  def : Pat<(X86andnp VR256X:$src1, (loadv16i16 addr:$src2)),
            (VPANDNQZ256rm VR256X:$src1, addr:$src2)>;

  def : Pat<(and VR256X:$src1,
                 (bc_v8i32 (v8f32 (X86VBroadcast (loadf32 addr:$src2))))),
            (VPANDDZ256rmb VR256X:$src1, addr:$src2)>;
  def : Pat<(or VR256X:$src1,
                (bc_v8i32 (v8f32 (X86VBroadcast (loadf32 addr:$src2))))),
            (VPORDZ256rmb VR256X:$src1, addr:$src2)>;
  def : Pat<(xor VR256X:$src1,
                 (bc_v8i32 (v8f32 (X86VBroadcast (loadf32 addr:$src2))))),
            (VPXORDZ256rmb VR256X:$src1, addr:$src2)>;
  def : Pat<(X86andnp VR256X:$src1,
                      (bc_v8i32 (v8f32 (X86VBroadcast (loadf32 addr:$src2))))),
            (VPANDNDZ256rmb VR256X:$src1, addr:$src2)>;

  def : Pat<(and VR256X:$src1,
                 (bc_v4i64 (v4f64 (X86VBroadcast (loadf64 addr:$src2))))),
            (VPANDQZ256rmb VR256X:$src1, addr:$src2)>;
  def : Pat<(or VR256X:$src1,
                (bc_v4i64 (v4f64 (X86VBroadcast (loadf64 addr:$src2))))),
            (VPORQZ256rmb VR256X:$src1, addr:$src2)>;
  def : Pat<(xor VR256X:$src1,
                 (bc_v4i64 (v4f64 (X86VBroadcast (loadf64 addr:$src2))))),
            (VPXORQZ256rmb VR256X:$src1, addr:$src2)>;
  def : Pat<(X86andnp VR256X:$src1,
                      (bc_v4i64 (v4f64 (X86VBroadcast (loadf64 addr:$src2))))),
            (VPANDNQZ256rmb VR256X:$src1, addr:$src2)>;
}

let Predicates = [HasAVX512] in {
  def : Pat<(v64i8 (and VR512:$src1, VR512:$src2)),
            (VPANDQZrr VR512:$src1, VR512:$src2)>;
  def : Pat<(v32i16 (and VR512:$src1, VR512:$src2)),
            (VPANDQZrr VR512:$src1, VR512:$src2)>;

  def : Pat<(v64i8 (or VR512:$src1, VR512:$src2)),
            (VPORQZrr VR512:$src1, VR512:$src2)>;
  def : Pat<(v32i16 (or VR512:$src1, VR512:$src2)),
            (VPORQZrr VR512:$src1, VR512:$src2)>;

  def : Pat<(v64i8 (xor VR512:$src1, VR512:$src2)),
            (VPXORQZrr VR512:$src1, VR512:$src2)>;
  def : Pat<(v32i16 (xor VR512:$src1, VR512:$src2)),
            (VPXORQZrr VR512:$src1, VR512:$src2)>;

  def : Pat<(v64i8 (X86andnp VR512:$src1, VR512:$src2)),
            (VPANDNQZrr VR512:$src1, VR512:$src2)>;
  def : Pat<(v32i16 (X86andnp VR512:$src1, VR512:$src2)),
            (VPANDNQZrr VR512:$src1, VR512:$src2)>;

  def : Pat<(and VR512:$src1, (loadv64i8 addr:$src2)),
            (VPANDQZrm VR512:$src1, addr:$src2)>;
  def : Pat<(and VR512:$src1, (loadv32i16 addr:$src2)),
            (VPANDQZrm VR512:$src1, addr:$src2)>;

  def : Pat<(or VR512:$src1, (loadv64i8 addr:$src2)),
            (VPORQZrm VR512:$src1, addr:$src2)>;
  def : Pat<(or VR512:$src1, (loadv32i16 addr:$src2)),
            (VPORQZrm VR512:$src1, addr:$src2)>;

  def : Pat<(xor VR512:$src1, (loadv64i8 addr:$src2)),
            (VPXORQZrm VR512:$src1, addr:$src2)>;
  def : Pat<(xor VR512:$src1, (loadv32i16 addr:$src2)),
            (VPXORQZrm VR512:$src1, addr:$src2)>;

  def : Pat<(X86andnp VR512:$src1, (loadv64i8 addr:$src2)),
            (VPANDNQZrm VR512:$src1, addr:$src2)>;
  def : Pat<(X86andnp VR512:$src1, (loadv32i16 addr:$src2)),
            (VPANDNQZrm VR512:$src1, addr:$src2)>;

  def : Pat<(and VR512:$src1,
                 (bc_v16i32 (v16f32 (X86VBroadcast (loadf32 addr:$src2))))),
            (VPANDDZrmb VR512:$src1, addr:$src2)>;
  def : Pat<(or VR512:$src1,
                (bc_v16i32 (v16f32 (X86VBroadcast (loadf32 addr:$src2))))),
            (VPORDZrmb VR512:$src1, addr:$src2)>;
  def : Pat<(xor VR512:$src1,
                 (bc_v16i32 (v16f32 (X86VBroadcast (loadf32 addr:$src2))))),
            (VPXORDZrmb VR512:$src1, addr:$src2)>;
  def : Pat<(X86andnp VR512:$src1,
                      (bc_v16i32 (v16f32 (X86VBroadcast (loadf32 addr:$src2))))),
            (VPANDNDZrmb VR512:$src1, addr:$src2)>;

  def : Pat<(and VR512:$src1,
                 (bc_v8i64 (v8f64 (X86VBroadcast (loadf64 addr:$src2))))),
            (VPANDQZrmb VR512:$src1, addr:$src2)>;
  def : Pat<(or VR512:$src1,
                (bc_v8i64 (v8f64 (X86VBroadcast (loadf64 addr:$src2))))),
            (VPORQZrmb VR512:$src1, addr:$src2)>;
  def : Pat<(xor VR512:$src1,
                 (bc_v8i64 (v8f64 (X86VBroadcast (loadf64 addr:$src2))))),
            (VPXORQZrmb VR512:$src1, addr:$src2)>;
  def : Pat<(X86andnp VR512:$src1,
                      (bc_v8i64 (v8f64 (X86VBroadcast (loadf64 addr:$src2))))),
            (VPANDNQZrmb VR512:$src1, addr:$src2)>;
}

// Patterns to catch vselect with different type than logic op.
multiclass avx512_logical_lowering<string InstrStr, SDNode OpNode,
                                    X86VectorVTInfo _,
                                    X86VectorVTInfo IntInfo> {
  // Masked register-register logical operations.
  def : Pat<(_.VT (vselect _.KRCWM:$mask,
                   (bitconvert (IntInfo.VT (OpNode _.RC:$src1, _.RC:$src2))),
                   _.RC:$src0)),
            (!cast<Instruction>(InstrStr#rrk) _.RC:$src0, _.KRCWM:$mask,
             _.RC:$src1, _.RC:$src2)>;

  def : Pat<(_.VT (vselect _.KRCWM:$mask,
                   (bitconvert (IntInfo.VT (OpNode _.RC:$src1, _.RC:$src2))),
                   _.ImmAllZerosV)),
            (!cast<Instruction>(InstrStr#rrkz) _.KRCWM:$mask, _.RC:$src1,
             _.RC:$src2)>;

  // Masked register-memory logical operations.
  def : Pat<(_.VT (vselect _.KRCWM:$mask,
                   (bitconvert (IntInfo.VT (OpNode _.RC:$src1,
                                            (load addr:$src2)))),
                   _.RC:$src0)),
            (!cast<Instruction>(InstrStr#rmk) _.RC:$src0, _.KRCWM:$mask,
             _.RC:$src1, addr:$src2)>;
  def : Pat<(_.VT (vselect _.KRCWM:$mask,
                   (bitconvert (IntInfo.VT (OpNode _.RC:$src1,
                                            (load addr:$src2)))),
                   _.ImmAllZerosV)),
            (!cast<Instruction>(InstrStr#rmkz) _.KRCWM:$mask, _.RC:$src1,
             addr:$src2)>;
}

multiclass avx512_logical_lowering_bcast<string InstrStr, SDNode OpNode,
                                         X86VectorVTInfo _,
                                         X86VectorVTInfo IntInfo> {
  // Register-broadcast logical operations.
  def : Pat<(IntInfo.VT (OpNode _.RC:$src1,
                         (bitconvert (_.VT (X86VBroadcast
                                            (_.ScalarLdFrag addr:$src2)))))),
            (!cast<Instruction>(InstrStr#rmb) _.RC:$src1, addr:$src2)>;
  def : Pat<(_.VT (vselect _.KRCWM:$mask,
                   (bitconvert
                    (IntInfo.VT (OpNode _.RC:$src1,
                                 (bitconvert (_.VT
                                              (X86VBroadcast
                                               (_.ScalarLdFrag addr:$src2))))))),
                   _.RC:$src0)),
            (!cast<Instruction>(InstrStr#rmbk) _.RC:$src0, _.KRCWM:$mask,
             _.RC:$src1, addr:$src2)>;
  def : Pat<(_.VT (vselect _.KRCWM:$mask,
                   (bitconvert
                    (IntInfo.VT (OpNode _.RC:$src1,
                                 (bitconvert (_.VT
                                              (X86VBroadcast
                                               (_.ScalarLdFrag addr:$src2))))))),
                   _.ImmAllZerosV)),
            (!cast<Instruction>(InstrStr#rmbkz)  _.KRCWM:$mask,
             _.RC:$src1, addr:$src2)>;
}

multiclass avx512_logical_lowering_sizes<string InstrStr, SDNode OpNode,
                                         AVX512VLVectorVTInfo SelectInfo,
                                         AVX512VLVectorVTInfo IntInfo> {
let Predicates = [HasVLX] in {
  defm : avx512_logical_lowering<InstrStr#"Z128", OpNode, SelectInfo.info128,
                                 IntInfo.info128>;
  defm : avx512_logical_lowering<InstrStr#"Z256", OpNode, SelectInfo.info256,
                                 IntInfo.info256>;
}
let Predicates = [HasAVX512] in {
  defm : avx512_logical_lowering<InstrStr#"Z", OpNode, SelectInfo.info512,
                                 IntInfo.info512>;
}
}

multiclass avx512_logical_lowering_sizes_bcast<string InstrStr, SDNode OpNode,
                                               AVX512VLVectorVTInfo SelectInfo,
                                               AVX512VLVectorVTInfo IntInfo> {
let Predicates = [HasVLX] in {
  defm : avx512_logical_lowering_bcast<InstrStr#"Z128", OpNode,
                                       SelectInfo.info128, IntInfo.info128>;
  defm : avx512_logical_lowering_bcast<InstrStr#"Z256", OpNode,
                                       SelectInfo.info256, IntInfo.info256>;
}
let Predicates = [HasAVX512] in {
  defm : avx512_logical_lowering_bcast<InstrStr#"Z", OpNode,
                                       SelectInfo.info512, IntInfo.info512>;
}
}

multiclass avx512_logical_lowering_types<string InstrStr, SDNode OpNode> {
  // i64 vselect with i32/i16/i8 logic op
  defm : avx512_logical_lowering_sizes<InstrStr#"Q", OpNode, avx512vl_i64_info,
                                       avx512vl_i32_info>;
  defm : avx512_logical_lowering_sizes<InstrStr#"Q", OpNode, avx512vl_i64_info,
                                       avx512vl_i16_info>;
  defm : avx512_logical_lowering_sizes<InstrStr#"Q", OpNode, avx512vl_i64_info,
                                       avx512vl_i8_info>;

  // i32 vselect with i64/i16/i8 logic op
  defm : avx512_logical_lowering_sizes<InstrStr#"D", OpNode, avx512vl_i32_info,
                                       avx512vl_i64_info>;
  defm : avx512_logical_lowering_sizes<InstrStr#"D", OpNode, avx512vl_i32_info,
                                       avx512vl_i16_info>;
  defm : avx512_logical_lowering_sizes<InstrStr#"D", OpNode, avx512vl_i32_info,
                                       avx512vl_i8_info>;

  // f32 vselect with i64/i32/i16/i8 logic op
  defm : avx512_logical_lowering_sizes<InstrStr#"D", OpNode, avx512vl_f32_info,
                                       avx512vl_i64_info>;
  defm : avx512_logical_lowering_sizes<InstrStr#"D", OpNode, avx512vl_f32_info,
                                       avx512vl_i32_info>;
  defm : avx512_logical_lowering_sizes<InstrStr#"D", OpNode, avx512vl_f32_info,
                                       avx512vl_i16_info>;
  defm : avx512_logical_lowering_sizes<InstrStr#"D", OpNode, avx512vl_f32_info,
                                       avx512vl_i8_info>;

  // f64 vselect with i64/i32/i16/i8 logic op
  defm : avx512_logical_lowering_sizes<InstrStr#"Q", OpNode, avx512vl_f64_info,
                                       avx512vl_i64_info>;
  defm : avx512_logical_lowering_sizes<InstrStr#"Q", OpNode, avx512vl_f64_info,
                                       avx512vl_i32_info>;
  defm : avx512_logical_lowering_sizes<InstrStr#"Q", OpNode, avx512vl_f64_info,
                                       avx512vl_i16_info>;
  defm : avx512_logical_lowering_sizes<InstrStr#"Q", OpNode, avx512vl_f64_info,
                                       avx512vl_i8_info>;

  defm : avx512_logical_lowering_sizes_bcast<InstrStr#"D", OpNode,
                                             avx512vl_f32_info,
                                             avx512vl_i32_info>;
  defm : avx512_logical_lowering_sizes_bcast<InstrStr#"Q", OpNode,
                                             avx512vl_f64_info,
                                             avx512vl_i64_info>;
}

defm : avx512_logical_lowering_types<"VPAND", and>;
defm : avx512_logical_lowering_types<"VPOR",  or>;
defm : avx512_logical_lowering_types<"VPXOR", xor>;
defm : avx512_logical_lowering_types<"VPANDN", X86andnp>;

//===----------------------------------------------------------------------===//
// AVX-512  FP arithmetic
//===----------------------------------------------------------------------===//

multiclass avx512_fp_scalar<bits<8> opc, string OpcodeStr,X86VectorVTInfo _,
                            SDNode OpNode, SDNode VecNode,
                            X86FoldableSchedWrite sched, bit IsCommutable> {
  let ExeDomain = _.ExeDomain in {
  defm rr_Int : AVX512_maskable_scalar<opc, MRMSrcReg, _, (outs _.RC:$dst),
                           (ins _.RC:$src1, _.RC:$src2), OpcodeStr,
                           "$src2, $src1", "$src1, $src2",
                           (_.VT (VecNode _.RC:$src1, _.RC:$src2,
                                          (i32 FROUND_CURRENT)))>,
                           Sched<[sched]>;

  defm rm_Int : AVX512_maskable_scalar<opc, MRMSrcMem, _, (outs _.RC:$dst),
                         (ins _.RC:$src1, _.IntScalarMemOp:$src2), OpcodeStr,
                         "$src2, $src1", "$src1, $src2",
                         (_.VT (VecNode _.RC:$src1,
                                        _.ScalarIntMemCPat:$src2,
                                        (i32 FROUND_CURRENT)))>,
                         Sched<[sched.Folded, sched.ReadAfterFold]>;
  let isCodeGenOnly = 1, Predicates = [HasAVX512] in {
  def rr : I< opc, MRMSrcReg, (outs _.FRC:$dst),
                         (ins _.FRC:$src1, _.FRC:$src2),
                          OpcodeStr#"\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                          [(set _.FRC:$dst, (OpNode _.FRC:$src1, _.FRC:$src2))]>,
                          Sched<[sched]> {
    let isCommutable = IsCommutable;
  }
  def rm : I< opc, MRMSrcMem, (outs _.FRC:$dst),
                         (ins _.FRC:$src1, _.ScalarMemOp:$src2),
                         OpcodeStr#"\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                         [(set _.FRC:$dst, (OpNode _.FRC:$src1,
                         (_.ScalarLdFrag addr:$src2)))]>,
                         Sched<[sched.Folded, sched.ReadAfterFold]>;
  }
  }
}

multiclass avx512_fp_scalar_round<bits<8> opc, string OpcodeStr,X86VectorVTInfo _,
                                  SDNode VecNode, X86FoldableSchedWrite sched,
                                  bit IsCommutable = 0> {
  let ExeDomain = _.ExeDomain in
  defm rrb_Int : AVX512_maskable_scalar<opc, MRMSrcReg, _, (outs _.RC:$dst),
                          (ins _.RC:$src1, _.RC:$src2, AVX512RC:$rc), OpcodeStr,
                          "$rc, $src2, $src1", "$src1, $src2, $rc",
                          (VecNode (_.VT _.RC:$src1), (_.VT _.RC:$src2),
                          (i32 imm:$rc)), IsCommutable>,
                          EVEX_B, EVEX_RC, Sched<[sched]>;
}
multiclass avx512_fp_scalar_sae<bits<8> opc, string OpcodeStr,X86VectorVTInfo _,
                                SDNode OpNode, SDNode VecNode, SDNode SaeNode,
                                X86FoldableSchedWrite sched, bit IsCommutable> {
  let ExeDomain = _.ExeDomain in {
  defm rr_Int : AVX512_maskable_scalar<opc, MRMSrcReg, _, (outs _.RC:$dst),
                           (ins _.RC:$src1, _.RC:$src2), OpcodeStr,
                           "$src2, $src1", "$src1, $src2",
                           (_.VT (VecNode _.RC:$src1, _.RC:$src2))>,
                           Sched<[sched]>;

  defm rm_Int : AVX512_maskable_scalar<opc, MRMSrcMem, _, (outs _.RC:$dst),
                         (ins _.RC:$src1, _.IntScalarMemOp:$src2), OpcodeStr,
                         "$src2, $src1", "$src1, $src2",
                         (_.VT (VecNode _.RC:$src1,
                                        _.ScalarIntMemCPat:$src2))>,
                         Sched<[sched.Folded, sched.ReadAfterFold]>;

  let isCodeGenOnly = 1, Predicates = [HasAVX512] in {
  def rr : I< opc, MRMSrcReg, (outs _.FRC:$dst),
                         (ins _.FRC:$src1, _.FRC:$src2),
                          OpcodeStr#"\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                          [(set _.FRC:$dst, (OpNode _.FRC:$src1, _.FRC:$src2))]>,
                          Sched<[sched]> {
    let isCommutable = IsCommutable;
  }
  def rm : I< opc, MRMSrcMem, (outs _.FRC:$dst),
                         (ins _.FRC:$src1, _.ScalarMemOp:$src2),
                         OpcodeStr#"\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                         [(set _.FRC:$dst, (OpNode _.FRC:$src1,
                         (_.ScalarLdFrag addr:$src2)))]>,
                         Sched<[sched.Folded, sched.ReadAfterFold]>;
  }

  defm rrb_Int : AVX512_maskable_scalar<opc, MRMSrcReg, _, (outs _.RC:$dst),
                            (ins _.RC:$src1, _.RC:$src2), OpcodeStr,
                            "{sae}, $src2, $src1", "$src1, $src2, {sae}",
                            (SaeNode (_.VT _.RC:$src1), (_.VT _.RC:$src2),
                            (i32 FROUND_NO_EXC))>, EVEX_B,
                            Sched<[sched]>;
  }
}

multiclass avx512_binop_s_round<bits<8> opc, string OpcodeStr, SDNode OpNode,
                                SDNode VecNode, X86SchedWriteSizes sched,
                                bit IsCommutable> {
  defm SSZ : avx512_fp_scalar<opc, OpcodeStr#"ss", f32x_info, OpNode, VecNode,
                              sched.PS.Scl, IsCommutable>,
             avx512_fp_scalar_round<opc, OpcodeStr#"ss", f32x_info, VecNode,
                              sched.PS.Scl, IsCommutable>,
                              XS, EVEX_4V, VEX_LIG,  EVEX_CD8<32, CD8VT1>;
  defm SDZ : avx512_fp_scalar<opc, OpcodeStr#"sd", f64x_info, OpNode, VecNode,
                              sched.PD.Scl, IsCommutable>,
             avx512_fp_scalar_round<opc, OpcodeStr#"sd", f64x_info, VecNode,
                              sched.PD.Scl, IsCommutable>,
                              XD, VEX_W, EVEX_4V, VEX_LIG, EVEX_CD8<64, CD8VT1>;
}

multiclass avx512_binop_s_sae<bits<8> opc, string OpcodeStr, SDNode OpNode,
                              SDNode VecNode, SDNode SaeNode,
                              X86SchedWriteSizes sched, bit IsCommutable> {
  defm SSZ : avx512_fp_scalar_sae<opc, OpcodeStr#"ss", f32x_info, OpNode,
                              VecNode, SaeNode, sched.PS.Scl, IsCommutable>,
                              XS, EVEX_4V, VEX_LIG,  EVEX_CD8<32, CD8VT1>;
  defm SDZ : avx512_fp_scalar_sae<opc, OpcodeStr#"sd", f64x_info, OpNode,
                              VecNode, SaeNode, sched.PD.Scl, IsCommutable>,
                              XD, VEX_W, EVEX_4V, VEX_LIG, EVEX_CD8<64, CD8VT1>;
}
defm VADD : avx512_binop_s_round<0x58, "vadd", fadd, X86faddRnds,
                                 SchedWriteFAddSizes, 1>;
defm VMUL : avx512_binop_s_round<0x59, "vmul", fmul, X86fmulRnds,
                                 SchedWriteFMulSizes, 1>;
defm VSUB : avx512_binop_s_round<0x5C, "vsub", fsub, X86fsubRnds,
                                 SchedWriteFAddSizes, 0>;
defm VDIV : avx512_binop_s_round<0x5E, "vdiv", fdiv, X86fdivRnds,
                                 SchedWriteFDivSizes, 0>;
defm VMIN : avx512_binop_s_sae<0x5D, "vmin", X86fmin, X86fmins, X86fminRnds,
                               SchedWriteFCmpSizes, 0>;
defm VMAX : avx512_binop_s_sae<0x5F, "vmax", X86fmax, X86fmaxs, X86fmaxRnds,
                               SchedWriteFCmpSizes, 0>;

// MIN/MAX nodes are commutable under "unsafe-fp-math". In this case we use
// X86fminc and X86fmaxc instead of X86fmin and X86fmax
multiclass avx512_comutable_binop_s<bits<8> opc, string OpcodeStr,
                                    X86VectorVTInfo _, SDNode OpNode,
                                    X86FoldableSchedWrite sched> {
  let isCodeGenOnly = 1, Predicates = [HasAVX512], ExeDomain = _.ExeDomain in {
  def rr : I< opc, MRMSrcReg, (outs _.FRC:$dst),
                         (ins _.FRC:$src1, _.FRC:$src2),
                          OpcodeStr#"\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                          [(set _.FRC:$dst, (OpNode _.FRC:$src1, _.FRC:$src2))]>,
                          Sched<[sched]> {
    let isCommutable = 1;
  }
  def rm : I< opc, MRMSrcMem, (outs _.FRC:$dst),
                         (ins _.FRC:$src1, _.ScalarMemOp:$src2),
                         OpcodeStr#"\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                         [(set _.FRC:$dst, (OpNode _.FRC:$src1,
                         (_.ScalarLdFrag addr:$src2)))]>,
                         Sched<[sched.Folded, sched.ReadAfterFold]>;
  }
}
defm VMINCSSZ : avx512_comutable_binop_s<0x5D, "vminss", f32x_info, X86fminc,
                                         SchedWriteFCmp.Scl>, XS, EVEX_4V,
                                         VEX_LIG, EVEX_CD8<32, CD8VT1>;

defm VMINCSDZ : avx512_comutable_binop_s<0x5D, "vminsd", f64x_info, X86fminc,
                                         SchedWriteFCmp.Scl>, XD, VEX_W, EVEX_4V,
                                         VEX_LIG, EVEX_CD8<64, CD8VT1>;

defm VMAXCSSZ : avx512_comutable_binop_s<0x5F, "vmaxss", f32x_info, X86fmaxc,
                                         SchedWriteFCmp.Scl>, XS, EVEX_4V,
                                         VEX_LIG, EVEX_CD8<32, CD8VT1>;

defm VMAXCSDZ : avx512_comutable_binop_s<0x5F, "vmaxsd", f64x_info, X86fmaxc,
                                         SchedWriteFCmp.Scl>, XD, VEX_W, EVEX_4V,
                                         VEX_LIG, EVEX_CD8<64, CD8VT1>;

multiclass avx512_fp_packed<bits<8> opc, string OpcodeStr, SDPatternOperator OpNode,
                            X86VectorVTInfo _, X86FoldableSchedWrite sched,
                            bit IsCommutable,
                            bit IsKZCommutable = IsCommutable> {
  let ExeDomain = _.ExeDomain, hasSideEffects = 0 in {
  defm rr: AVX512_maskable<opc, MRMSrcReg, _, (outs _.RC:$dst),
                  (ins _.RC:$src1, _.RC:$src2), OpcodeStr##_.Suffix,
                  "$src2, $src1", "$src1, $src2",
                  (_.VT (OpNode _.RC:$src1, _.RC:$src2)), IsCommutable, 0,
                  IsKZCommutable>,
                  EVEX_4V, Sched<[sched]>;
  let mayLoad = 1 in {
    defm rm: AVX512_maskable<opc, MRMSrcMem, _, (outs _.RC:$dst),
                    (ins _.RC:$src1, _.MemOp:$src2), OpcodeStr##_.Suffix,
                    "$src2, $src1", "$src1, $src2",
                    (OpNode _.RC:$src1, (_.LdFrag addr:$src2))>,
                    EVEX_4V, Sched<[sched.Folded, sched.ReadAfterFold]>;
    defm rmb: AVX512_maskable<opc, MRMSrcMem, _, (outs _.RC:$dst),
                     (ins _.RC:$src1, _.ScalarMemOp:$src2), OpcodeStr##_.Suffix,
                     "${src2}"##_.BroadcastStr##", $src1",
                     "$src1, ${src2}"##_.BroadcastStr,
                     (OpNode  _.RC:$src1, (_.VT (X86VBroadcast
                                                (_.ScalarLdFrag addr:$src2))))>,
                     EVEX_4V, EVEX_B,
                     Sched<[sched.Folded, sched.ReadAfterFold]>;
    }
  }
}

multiclass avx512_fp_round_packed<bits<8> opc, string OpcodeStr,
                                  SDPatternOperator OpNodeRnd,
                                  X86FoldableSchedWrite sched, X86VectorVTInfo _> {
  let ExeDomain = _.ExeDomain in
  defm rrb: AVX512_maskable<opc, MRMSrcReg, _, (outs _.RC:$dst),
                  (ins _.RC:$src1, _.RC:$src2, AVX512RC:$rc), OpcodeStr##_.Suffix,
                  "$rc, $src2, $src1", "$src1, $src2, $rc",
                  (_.VT (OpNodeRnd _.RC:$src1, _.RC:$src2, (i32 imm:$rc)))>,
                  EVEX_4V, EVEX_B, EVEX_RC, Sched<[sched]>;
}

multiclass avx512_fp_sae_packed<bits<8> opc, string OpcodeStr,
                                SDPatternOperator OpNodeRnd,
                                X86FoldableSchedWrite sched, X86VectorVTInfo _> {
  let ExeDomain = _.ExeDomain in
  defm rrb: AVX512_maskable<opc, MRMSrcReg, _, (outs _.RC:$dst),
                  (ins _.RC:$src1, _.RC:$src2), OpcodeStr##_.Suffix,
                  "{sae}, $src2, $src1", "$src1, $src2, {sae}",
                  (_.VT (OpNodeRnd _.RC:$src1, _.RC:$src2, (i32 FROUND_NO_EXC)))>,
                  EVEX_4V, EVEX_B, Sched<[sched]>;
}

multiclass avx512_fp_binop_p<bits<8> opc, string OpcodeStr, SDPatternOperator OpNode,
                             Predicate prd, X86SchedWriteSizes sched,
                             bit IsCommutable = 0,
                             bit IsPD128Commutable = IsCommutable> {
  let Predicates = [prd] in {
  defm PSZ : avx512_fp_packed<opc, OpcodeStr, OpNode, v16f32_info,
                              sched.PS.ZMM, IsCommutable>, EVEX_V512, PS,
                              EVEX_CD8<32, CD8VF>;
  defm PDZ : avx512_fp_packed<opc, OpcodeStr, OpNode, v8f64_info,
                              sched.PD.ZMM, IsCommutable>, EVEX_V512, PD, VEX_W,
                              EVEX_CD8<64, CD8VF>;
  }

    // Define only if AVX512VL feature is present.
  let Predicates = [prd, HasVLX] in {
    defm PSZ128 : avx512_fp_packed<opc, OpcodeStr, OpNode, v4f32x_info,
                                   sched.PS.XMM, IsCommutable>, EVEX_V128, PS,
                                   EVEX_CD8<32, CD8VF>;
    defm PSZ256 : avx512_fp_packed<opc, OpcodeStr, OpNode, v8f32x_info,
                                   sched.PS.YMM, IsCommutable>, EVEX_V256, PS,
                                   EVEX_CD8<32, CD8VF>;
    defm PDZ128 : avx512_fp_packed<opc, OpcodeStr, OpNode, v2f64x_info,
                                   sched.PD.XMM, IsPD128Commutable,
                                   IsCommutable>, EVEX_V128, PD, VEX_W,
                                   EVEX_CD8<64, CD8VF>;
    defm PDZ256 : avx512_fp_packed<opc, OpcodeStr, OpNode, v4f64x_info,
                                   sched.PD.YMM, IsCommutable>, EVEX_V256, PD, VEX_W,
                                   EVEX_CD8<64, CD8VF>;
  }
}

multiclass avx512_fp_binop_p_round<bits<8> opc, string OpcodeStr, SDNode OpNodeRnd,
                                   X86SchedWriteSizes sched> {
  defm PSZ : avx512_fp_round_packed<opc, OpcodeStr, OpNodeRnd, sched.PS.ZMM,
                                    v16f32_info>,
                                    EVEX_V512, PS, EVEX_CD8<32, CD8VF>;
  defm PDZ : avx512_fp_round_packed<opc, OpcodeStr, OpNodeRnd, sched.PD.ZMM,
                                    v8f64_info>,
                                    EVEX_V512, PD, VEX_W,EVEX_CD8<64, CD8VF>;
}

multiclass avx512_fp_binop_p_sae<bits<8> opc, string OpcodeStr, SDNode OpNodeRnd,
                                 X86SchedWriteSizes sched> {
  defm PSZ : avx512_fp_sae_packed<opc, OpcodeStr, OpNodeRnd, sched.PS.ZMM,
                                  v16f32_info>,
                                  EVEX_V512, PS, EVEX_CD8<32, CD8VF>;
  defm PDZ : avx512_fp_sae_packed<opc, OpcodeStr, OpNodeRnd, sched.PD.ZMM,
                                  v8f64_info>,
                                  EVEX_V512, PD, VEX_W,EVEX_CD8<64, CD8VF>;
}

defm VADD : avx512_fp_binop_p<0x58, "vadd", fadd, HasAVX512,
                              SchedWriteFAddSizes, 1>,
            avx512_fp_binop_p_round<0x58, "vadd", X86faddRnd, SchedWriteFAddSizes>;
defm VMUL : avx512_fp_binop_p<0x59, "vmul", fmul, HasAVX512,
                              SchedWriteFMulSizes, 1>,
            avx512_fp_binop_p_round<0x59, "vmul", X86fmulRnd, SchedWriteFMulSizes>;
defm VSUB : avx512_fp_binop_p<0x5C, "vsub", fsub, HasAVX512,
                              SchedWriteFAddSizes>,
            avx512_fp_binop_p_round<0x5C, "vsub", X86fsubRnd, SchedWriteFAddSizes>;
defm VDIV : avx512_fp_binop_p<0x5E, "vdiv", fdiv, HasAVX512,
                              SchedWriteFDivSizes>,
            avx512_fp_binop_p_round<0x5E, "vdiv", X86fdivRnd, SchedWriteFDivSizes>;
defm VMIN : avx512_fp_binop_p<0x5D, "vmin", X86fmin, HasAVX512,
                              SchedWriteFCmpSizes, 0>,
            avx512_fp_binop_p_sae<0x5D, "vmin", X86fminRnd, SchedWriteFCmpSizes>;
defm VMAX : avx512_fp_binop_p<0x5F, "vmax", X86fmax, HasAVX512,
                              SchedWriteFCmpSizes, 0>,
            avx512_fp_binop_p_sae<0x5F, "vmax", X86fmaxRnd, SchedWriteFCmpSizes>;
let isCodeGenOnly = 1 in {
  defm VMINC : avx512_fp_binop_p<0x5D, "vmin", X86fminc, HasAVX512,
                                 SchedWriteFCmpSizes, 1>;
  defm VMAXC : avx512_fp_binop_p<0x5F, "vmax", X86fmaxc, HasAVX512,
                                 SchedWriteFCmpSizes, 1>;
}
defm VAND  : avx512_fp_binop_p<0x54, "vand", null_frag, HasDQI,
                               SchedWriteFLogicSizes, 1>;
defm VANDN : avx512_fp_binop_p<0x55, "vandn", null_frag, HasDQI,
                               SchedWriteFLogicSizes, 0>;
defm VOR   : avx512_fp_binop_p<0x56, "vor", null_frag, HasDQI,
                               SchedWriteFLogicSizes, 1>;
defm VXOR  : avx512_fp_binop_p<0x57, "vxor", null_frag, HasDQI,
                               SchedWriteFLogicSizes, 1>;

let Predicates = [HasVLX,HasDQI] in {
  // Use packed logical operations for scalar ops.
  def : Pat<(f64 (X86fand FR64X:$src1, FR64X:$src2)),
            (COPY_TO_REGCLASS
             (v2f64 (VANDPDZ128rr (v2f64 (COPY_TO_REGCLASS FR64X:$src1, VR128X)),
                                  (v2f64 (COPY_TO_REGCLASS FR64X:$src2, VR128X)))),
             FR64X)>;
  def : Pat<(f64 (X86for FR64X:$src1, FR64X:$src2)),
            (COPY_TO_REGCLASS
             (v2f64 (VORPDZ128rr (v2f64 (COPY_TO_REGCLASS FR64X:$src1, VR128X)),
                                 (v2f64 (COPY_TO_REGCLASS FR64X:$src2, VR128X)))),
             FR64X)>;
  def : Pat<(f64 (X86fxor FR64X:$src1, FR64X:$src2)),
            (COPY_TO_REGCLASS
             (v2f64 (VXORPDZ128rr (v2f64 (COPY_TO_REGCLASS FR64X:$src1, VR128X)),
                                  (v2f64 (COPY_TO_REGCLASS FR64X:$src2, VR128X)))),
             FR64X)>;
  def : Pat<(f64 (X86fandn FR64X:$src1, FR64X:$src2)),
            (COPY_TO_REGCLASS
             (v2f64 (VANDNPDZ128rr (v2f64 (COPY_TO_REGCLASS FR64X:$src1, VR128X)),
                                   (v2f64 (COPY_TO_REGCLASS FR64X:$src2, VR128X)))),
             FR64X)>;

  def : Pat<(f32 (X86fand FR32X:$src1, FR32X:$src2)),
            (COPY_TO_REGCLASS
             (v4f32 (VANDPSZ128rr (v4f32 (COPY_TO_REGCLASS FR32X:$src1, VR128X)),
                                  (v4f32 (COPY_TO_REGCLASS FR32X:$src2, VR128X)))),
             FR32X)>;
  def : Pat<(f32 (X86for FR32X:$src1, FR32X:$src2)),
            (COPY_TO_REGCLASS
             (v4f32 (VORPSZ128rr (v4f32 (COPY_TO_REGCLASS FR32X:$src1, VR128X)),
                                 (v4f32 (COPY_TO_REGCLASS FR32X:$src2, VR128X)))),
             FR32X)>;
  def : Pat<(f32 (X86fxor FR32X:$src1, FR32X:$src2)),
            (COPY_TO_REGCLASS
             (v4f32 (VXORPSZ128rr (v4f32 (COPY_TO_REGCLASS FR32X:$src1, VR128X)),
                                  (v4f32 (COPY_TO_REGCLASS FR32X:$src2, VR128X)))),
             FR32X)>;
  def : Pat<(f32 (X86fandn FR32X:$src1, FR32X:$src2)),
            (COPY_TO_REGCLASS
             (v4f32 (VANDNPSZ128rr (v4f32 (COPY_TO_REGCLASS FR32X:$src1, VR128X)),
                                   (v4f32 (COPY_TO_REGCLASS FR32X:$src2, VR128X)))),
             FR32X)>;
}

multiclass avx512_fp_scalef_p<bits<8> opc, string OpcodeStr, SDNode OpNode,
                              X86FoldableSchedWrite sched, X86VectorVTInfo _> {
  let ExeDomain = _.ExeDomain in {
  defm rr: AVX512_maskable<opc, MRMSrcReg, _, (outs _.RC:$dst),
                  (ins _.RC:$src1, _.RC:$src2), OpcodeStr##_.Suffix,
                  "$src2, $src1", "$src1, $src2",
                  (_.VT (OpNode _.RC:$src1, _.RC:$src2, (i32 FROUND_CURRENT)))>,
                  EVEX_4V, Sched<[sched]>;
  defm rm: AVX512_maskable<opc, MRMSrcMem, _, (outs _.RC:$dst),
                  (ins _.RC:$src1, _.MemOp:$src2), OpcodeStr##_.Suffix,
                  "$src2, $src1", "$src1, $src2",
                  (OpNode _.RC:$src1, (_.LdFrag addr:$src2), (i32 FROUND_CURRENT))>,
                  EVEX_4V, Sched<[sched.Folded, sched.ReadAfterFold]>;
  defm rmb: AVX512_maskable<opc, MRMSrcMem, _, (outs _.RC:$dst),
                   (ins _.RC:$src1, _.ScalarMemOp:$src2), OpcodeStr##_.Suffix,
                   "${src2}"##_.BroadcastStr##", $src1",
                   "$src1, ${src2}"##_.BroadcastStr,
                   (OpNode  _.RC:$src1, (_.VT (X86VBroadcast
                                              (_.ScalarLdFrag addr:$src2))),
                                              (i32 FROUND_CURRENT))>,
                   EVEX_4V, EVEX_B, Sched<[sched.Folded, sched.ReadAfterFold]>;
  }
}

multiclass avx512_fp_scalef_scalar<bits<8> opc, string OpcodeStr, SDNode OpNode,
                                   X86FoldableSchedWrite sched, X86VectorVTInfo _> {
  let ExeDomain = _.ExeDomain in {
  defm rr: AVX512_maskable_scalar<opc, MRMSrcReg, _, (outs _.RC:$dst),
                  (ins _.RC:$src1, _.RC:$src2), OpcodeStr##_.Suffix,
                  "$src2, $src1", "$src1, $src2",
                  (_.VT (OpNode _.RC:$src1, _.RC:$src2, (i32 FROUND_CURRENT)))>,
                  Sched<[sched]>;
  defm rm: AVX512_maskable_scalar<opc, MRMSrcMem, _, (outs _.RC:$dst),
                  (ins _.RC:$src1, _.IntScalarMemOp:$src2), OpcodeStr##_.Suffix,
                  "$src2, $src1", "$src1, $src2",
                  (OpNode _.RC:$src1, _.ScalarIntMemCPat:$src2,
                          (i32 FROUND_CURRENT))>,
                  Sched<[sched.Folded, sched.ReadAfterFold]>;
  }
}

multiclass avx512_fp_scalef_all<bits<8> opc, bits<8> opcScaler, string OpcodeStr,
                                SDNode OpNode, SDNode OpNodeScal,
                                X86SchedWriteWidths sched> {
  defm PSZ : avx512_fp_scalef_p<opc, OpcodeStr, OpNode, sched.ZMM, v16f32_info>,
             avx512_fp_round_packed<opc, OpcodeStr, OpNode, sched.ZMM, v16f32_info>,
                              EVEX_V512, EVEX_CD8<32, CD8VF>;
  defm PDZ : avx512_fp_scalef_p<opc, OpcodeStr, OpNode, sched.ZMM, v8f64_info>,
             avx512_fp_round_packed<opc, OpcodeStr, OpNode, sched.ZMM, v8f64_info>,
                              EVEX_V512, VEX_W, EVEX_CD8<64, CD8VF>;
  defm SSZ : avx512_fp_scalef_scalar<opcScaler, OpcodeStr, OpNodeScal, sched.Scl, f32x_info>,
             avx512_fp_scalar_round<opcScaler, OpcodeStr##"ss", f32x_info, OpNodeScal, sched.Scl>,
                           EVEX_4V,EVEX_CD8<32, CD8VT1>;
  defm SDZ : avx512_fp_scalef_scalar<opcScaler, OpcodeStr, OpNodeScal, sched.Scl, f64x_info>,
             avx512_fp_scalar_round<opcScaler, OpcodeStr##"sd", f64x_info, OpNodeScal, sched.Scl>,
                           EVEX_4V, EVEX_CD8<64, CD8VT1>, VEX_W;

  // Define only if AVX512VL feature is present.
  let Predicates = [HasVLX] in {
    defm PSZ128 : avx512_fp_scalef_p<opc, OpcodeStr, OpNode, sched.XMM, v4f32x_info>,
                                   EVEX_V128, EVEX_CD8<32, CD8VF>;
    defm PSZ256 : avx512_fp_scalef_p<opc, OpcodeStr, OpNode, sched.YMM, v8f32x_info>,
                                   EVEX_V256, EVEX_CD8<32, CD8VF>;
    defm PDZ128 : avx512_fp_scalef_p<opc, OpcodeStr, OpNode, sched.XMM, v2f64x_info>,
                                   EVEX_V128, VEX_W, EVEX_CD8<64, CD8VF>;
    defm PDZ256 : avx512_fp_scalef_p<opc, OpcodeStr, OpNode, sched.YMM, v4f64x_info>,
                                   EVEX_V256, VEX_W, EVEX_CD8<64, CD8VF>;
  }
}
defm VSCALEF : avx512_fp_scalef_all<0x2C, 0x2D, "vscalef", X86scalef, X86scalefs,
                                    SchedWriteFAdd>, T8PD, NotEVEX2VEXConvertible;

//===----------------------------------------------------------------------===//
// AVX-512  VPTESTM instructions
//===----------------------------------------------------------------------===//

multiclass avx512_vptest<bits<8> opc, string OpcodeStr, PatFrag OpNode,
                         X86FoldableSchedWrite sched, X86VectorVTInfo _,
                         string Name> {
  let ExeDomain = _.ExeDomain in {
  let isCommutable = 1 in
  defm rr : AVX512_maskable_cmp<opc, MRMSrcReg, _, (outs _.KRC:$dst),
                   (ins _.RC:$src1, _.RC:$src2), OpcodeStr,
                      "$src2, $src1", "$src1, $src2",
                   (OpNode (and _.RC:$src1, _.RC:$src2), _.ImmAllZerosV)>,
                   EVEX_4V, Sched<[sched]>;
  defm rm : AVX512_maskable_cmp<opc, MRMSrcMem, _, (outs _.KRC:$dst),
                   (ins _.RC:$src1, _.MemOp:$src2), OpcodeStr,
                       "$src2, $src1", "$src1, $src2",
                   (OpNode (and _.RC:$src1, (_.LdFrag addr:$src2)),
                           _.ImmAllZerosV)>,
                   EVEX_4V, EVEX_CD8<_.EltSize, CD8VF>,
                   Sched<[sched.Folded, sched.ReadAfterFold]>;
  }

  // Patterns for compare with 0 that just use the same source twice.
  def : Pat<(_.KVT (OpNode _.RC:$src, _.ImmAllZerosV)),
            (_.KVT (!cast<Instruction>(Name # _.ZSuffix # "rr")
                                      _.RC:$src, _.RC:$src))>;

  def : Pat<(_.KVT (and _.KRC:$mask, (OpNode _.RC:$src, _.ImmAllZerosV))),
            (_.KVT (!cast<Instruction>(Name # _.ZSuffix # "rrk")
                                      _.KRC:$mask, _.RC:$src, _.RC:$src))>;
}

multiclass avx512_vptest_mb<bits<8> opc, string OpcodeStr, PatFrag OpNode,
                            X86FoldableSchedWrite sched, X86VectorVTInfo _> {
  let ExeDomain = _.ExeDomain in
  defm rmb : AVX512_maskable_cmp<opc, MRMSrcMem, _, (outs _.KRC:$dst),
                    (ins _.RC:$src1, _.ScalarMemOp:$src2), OpcodeStr,
                    "${src2}"##_.BroadcastStr##", $src1",
                    "$src1, ${src2}"##_.BroadcastStr,
                    (OpNode (and _.RC:$src1,
                                       (X86VBroadcast
                                        (_.ScalarLdFrag addr:$src2))),
                            _.ImmAllZerosV)>,
                    EVEX_B, EVEX_4V, EVEX_CD8<_.EltSize, CD8VF>,
                    Sched<[sched.Folded, sched.ReadAfterFold]>;
}

// Use 512bit version to implement 128/256 bit in case NoVLX.
multiclass avx512_vptest_lowering<PatFrag OpNode, X86VectorVTInfo ExtendInfo,
                                  X86VectorVTInfo _, string Name> {
  def : Pat<(_.KVT (OpNode (and _.RC:$src1, _.RC:$src2),
                           _.ImmAllZerosV)),
            (_.KVT (COPY_TO_REGCLASS
                     (!cast<Instruction>(Name # "Zrr")
                       (INSERT_SUBREG (ExtendInfo.VT (IMPLICIT_DEF)),
                                      _.RC:$src1, _.SubRegIdx),
                       (INSERT_SUBREG (ExtendInfo.VT (IMPLICIT_DEF)),
                                      _.RC:$src2, _.SubRegIdx)),
                   _.KRC))>;

  def : Pat<(_.KVT (and _.KRC:$mask,
                        (OpNode (and _.RC:$src1, _.RC:$src2),
                                _.ImmAllZerosV))),
            (COPY_TO_REGCLASS
             (!cast<Instruction>(Name # "Zrrk")
              (COPY_TO_REGCLASS _.KRC:$mask, ExtendInfo.KRC),
              (INSERT_SUBREG (ExtendInfo.VT (IMPLICIT_DEF)),
                             _.RC:$src1, _.SubRegIdx),
              (INSERT_SUBREG (ExtendInfo.VT (IMPLICIT_DEF)),
                             _.RC:$src2, _.SubRegIdx)),
             _.KRC)>;

  def : Pat<(_.KVT (OpNode _.RC:$src, _.ImmAllZerosV)),
            (_.KVT (COPY_TO_REGCLASS
                     (!cast<Instruction>(Name # "Zrr")
                       (INSERT_SUBREG (ExtendInfo.VT (IMPLICIT_DEF)),
                                      _.RC:$src, _.SubRegIdx),
                       (INSERT_SUBREG (ExtendInfo.VT (IMPLICIT_DEF)),
                                      _.RC:$src, _.SubRegIdx)),
                   _.KRC))>;

  def : Pat<(_.KVT (and _.KRC:$mask, (OpNode _.RC:$src, _.ImmAllZerosV))),
            (COPY_TO_REGCLASS
             (!cast<Instruction>(Name # "Zrrk")
              (COPY_TO_REGCLASS _.KRC:$mask, ExtendInfo.KRC),
              (INSERT_SUBREG (ExtendInfo.VT (IMPLICIT_DEF)),
                             _.RC:$src, _.SubRegIdx),
              (INSERT_SUBREG (ExtendInfo.VT (IMPLICIT_DEF)),
                             _.RC:$src, _.SubRegIdx)),
             _.KRC)>;
}

multiclass avx512_vptest_dq_sizes<bits<8> opc, string OpcodeStr, PatFrag OpNode,
                                  X86SchedWriteWidths sched, AVX512VLVectorVTInfo _> {
  let Predicates  = [HasAVX512] in
  defm Z : avx512_vptest<opc, OpcodeStr, OpNode, sched.ZMM, _.info512, NAME>,
           avx512_vptest_mb<opc, OpcodeStr, OpNode, sched.ZMM, _.info512>, EVEX_V512;

  let Predicates = [HasAVX512, HasVLX] in {
  defm Z256 : avx512_vptest<opc, OpcodeStr, OpNode, sched.YMM, _.info256, NAME>,
              avx512_vptest_mb<opc, OpcodeStr, OpNode, sched.YMM, _.info256>, EVEX_V256;
  defm Z128 : avx512_vptest<opc, OpcodeStr, OpNode, sched.XMM, _.info128, NAME>,
              avx512_vptest_mb<opc, OpcodeStr, OpNode, sched.XMM, _.info128>, EVEX_V128;
  }
  let Predicates = [HasAVX512, NoVLX] in {
  defm Z256_Alt : avx512_vptest_lowering< OpNode, _.info512, _.info256, NAME>;
  defm Z128_Alt : avx512_vptest_lowering< OpNode, _.info512, _.info128, NAME>;
  }
}

multiclass avx512_vptest_dq<bits<8> opc, string OpcodeStr, PatFrag OpNode,
                            X86SchedWriteWidths sched> {
  defm D : avx512_vptest_dq_sizes<opc, OpcodeStr#"d", OpNode, sched,
                                 avx512vl_i32_info>;
  defm Q : avx512_vptest_dq_sizes<opc, OpcodeStr#"q", OpNode, sched,
                                 avx512vl_i64_info>, VEX_W;
}

multiclass avx512_vptest_wb<bits<8> opc, string OpcodeStr,
                            PatFrag OpNode, X86SchedWriteWidths sched> {
  let Predicates = [HasBWI] in {
  defm WZ:    avx512_vptest<opc, OpcodeStr#"w", OpNode, sched.ZMM,
                            v32i16_info, NAME#"W">, EVEX_V512, VEX_W;
  defm BZ:    avx512_vptest<opc, OpcodeStr#"b", OpNode, sched.ZMM,
                            v64i8_info, NAME#"B">, EVEX_V512;
  }
  let Predicates = [HasVLX, HasBWI] in {

  defm WZ256: avx512_vptest<opc, OpcodeStr#"w", OpNode, sched.YMM,
                            v16i16x_info, NAME#"W">, EVEX_V256, VEX_W;
  defm WZ128: avx512_vptest<opc, OpcodeStr#"w", OpNode, sched.XMM,
                            v8i16x_info, NAME#"W">, EVEX_V128, VEX_W;
  defm BZ256: avx512_vptest<opc, OpcodeStr#"b", OpNode, sched.YMM,
                            v32i8x_info, NAME#"B">, EVEX_V256;
  defm BZ128: avx512_vptest<opc, OpcodeStr#"b", OpNode, sched.XMM,
                            v16i8x_info, NAME#"B">, EVEX_V128;
  }

  let Predicates = [HasBWI, NoVLX] in {
  defm BZ256_Alt : avx512_vptest_lowering<OpNode, v64i8_info, v32i8x_info, NAME#"B">;
  defm BZ128_Alt : avx512_vptest_lowering<OpNode, v64i8_info, v16i8x_info, NAME#"B">;
  defm WZ256_Alt : avx512_vptest_lowering<OpNode, v32i16_info, v16i16x_info, NAME#"W">;
  defm WZ128_Alt : avx512_vptest_lowering<OpNode, v32i16_info, v8i16x_info, NAME#"W">;
  }
}

// These patterns are used to match vptestm/vptestnm. We don't treat pcmpeqm
// as commutable here because we already canonicalized all zeros vectors to the
// RHS during lowering.
def X86pcmpeqm : PatFrag<(ops node:$src1, node:$src2),
                         (setcc node:$src1, node:$src2, SETEQ)>;
def X86pcmpnem : PatFrag<(ops node:$src1, node:$src2),
                         (setcc node:$src1, node:$src2, SETNE)>;

multiclass avx512_vptest_all_forms<bits<8> opc_wb, bits<8> opc_dq, string OpcodeStr,
                                   PatFrag OpNode, X86SchedWriteWidths sched> :
  avx512_vptest_wb<opc_wb, OpcodeStr, OpNode, sched>,
  avx512_vptest_dq<opc_dq, OpcodeStr, OpNode, sched>;

defm VPTESTM   : avx512_vptest_all_forms<0x26, 0x27, "vptestm", X86pcmpnem,
                                         SchedWriteVecLogic>, T8PD;
defm VPTESTNM  : avx512_vptest_all_forms<0x26, 0x27, "vptestnm", X86pcmpeqm,
                                         SchedWriteVecLogic>, T8XS;


multiclass avx512_vptest_lowering_pats<string InstrStr, PatFrag OpNode,
                                       X86VectorVTInfo _,
                                       X86VectorVTInfo AndInfo> {
  def : Pat<(_.KVT (OpNode (bitconvert
                            (AndInfo.VT (and _.RC:$src1, _.RC:$src2))),
                           _.ImmAllZerosV)),
            (!cast<Instruction>(InstrStr # "rr") _.RC:$src1, _.RC:$src2)>;

  def : Pat<(_.KVT (and _.KRC:$mask,
                    (OpNode (bitconvert
                             (AndInfo.VT (and _.RC:$src1, _.RC:$src2))),
                            _.ImmAllZerosV))),
            (!cast<Instruction>(InstrStr # "rrk") _.KRC:$mask, _.RC:$src1,
                                                  _.RC:$src2)>;

  def : Pat<(_.KVT (OpNode (bitconvert
                            (AndInfo.VT (and _.RC:$src1,
                                             (AndInfo.LdFrag addr:$src2)))),
                           _.ImmAllZerosV)),
            (!cast<Instruction>(InstrStr # "rm") _.RC:$src1, addr:$src2)>;

  def : Pat<(_.KVT (and _.KRC:$mask,
                    (OpNode (bitconvert
                             (AndInfo.VT (and _.RC:$src1,
                                              (AndInfo.LdFrag addr:$src2)))),
                            _.ImmAllZerosV))),
            (!cast<Instruction>(InstrStr # "rmk") _.KRC:$mask, _.RC:$src1,
                                                  addr:$src2)>;
}

// Patterns to use 512-bit instructions when 128/256 are not available.
multiclass avx512_vptest_lowering_wide_pats<string InstrStr, PatFrag OpNode,
                                            X86VectorVTInfo _,
                                            X86VectorVTInfo AndInfo,
                                            X86VectorVTInfo ExtendInfo> {
  def : Pat<(_.KVT (OpNode (bitconvert
                            (AndInfo.VT (and _.RC:$src1, _.RC:$src2))),
                           _.ImmAllZerosV)),
            (_.KVT (COPY_TO_REGCLASS
                     (!cast<Instruction>(InstrStr#"rr")
                       (INSERT_SUBREG (ExtendInfo.VT (IMPLICIT_DEF)),
                                      _.RC:$src1, _.SubRegIdx),
                       (INSERT_SUBREG (ExtendInfo.VT (IMPLICIT_DEF)),
                                      _.RC:$src2, _.SubRegIdx)),
                   _.KRC))>;

  def : Pat<(_.KVT (and _.KRC:$mask,
                    (OpNode (bitconvert
                             (AndInfo.VT (and _.RC:$src1, _.RC:$src2))),
                            _.ImmAllZerosV))),
            (COPY_TO_REGCLASS
             (!cast<Instruction>(InstrStr#"rrk")
              (COPY_TO_REGCLASS _.KRC:$mask, ExtendInfo.KRC),
              (INSERT_SUBREG (ExtendInfo.VT (IMPLICIT_DEF)),
                             _.RC:$src1, _.SubRegIdx),
              (INSERT_SUBREG (ExtendInfo.VT (IMPLICIT_DEF)),
                             _.RC:$src2, _.SubRegIdx)),
             _.KRC)>;
}

multiclass avx512_vptest_lowering_sizes<string InstrStr, PatFrag OpNode,
                                        Predicate prd,
                                        AVX512VLVectorVTInfo CmpInfo,
                                        AVX512VLVectorVTInfo AndInfo> {
let Predicates = [prd, HasVLX] in {
  defm : avx512_vptest_lowering_pats<InstrStr#"Z128", OpNode,
                                     CmpInfo.info128, AndInfo.info128>;
  defm : avx512_vptest_lowering_pats<InstrStr#"Z256", OpNode,
                                     CmpInfo.info256, AndInfo.info256>;
}
let Predicates = [prd] in {
  defm : avx512_vptest_lowering_pats<InstrStr#"Z", OpNode,
                                     CmpInfo.info512, AndInfo.info512>;
}

let Predicates = [prd, NoVLX] in {
  defm : avx512_vptest_lowering_wide_pats<InstrStr#"Z", OpNode,
                                          CmpInfo.info128, AndInfo.info128,
                                          CmpInfo.info512>;
  defm : avx512_vptest_lowering_wide_pats<InstrStr#"Z", OpNode,
                                          CmpInfo.info256, AndInfo.info256,
                                          CmpInfo.info512>;
}
}

multiclass avx512_vptest_lowering_types<string InstrStr, PatFrag OpNode> {
  defm : avx512_vptest_lowering_sizes<InstrStr # "B", OpNode, HasBWI,
                                      avx512vl_i8_info, avx512vl_i16_info>;
  defm : avx512_vptest_lowering_sizes<InstrStr # "B", OpNode, HasBWI,
                                      avx512vl_i8_info, avx512vl_i32_info>;
  defm : avx512_vptest_lowering_sizes<InstrStr # "B", OpNode, HasBWI,
                                      avx512vl_i8_info, avx512vl_i64_info>;

  defm : avx512_vptest_lowering_sizes<InstrStr # "W", OpNode, HasBWI,
                                      avx512vl_i16_info, avx512vl_i8_info>;
  defm : avx512_vptest_lowering_sizes<InstrStr # "W", OpNode, HasBWI,
                                      avx512vl_i16_info, avx512vl_i32_info>;
  defm : avx512_vptest_lowering_sizes<InstrStr # "W", OpNode, HasBWI,
                                      avx512vl_i16_info, avx512vl_i64_info>;

  defm : avx512_vptest_lowering_sizes<InstrStr # "D", OpNode, HasAVX512,
                                      avx512vl_i32_info, avx512vl_i8_info>;
  defm : avx512_vptest_lowering_sizes<InstrStr # "D", OpNode, HasAVX512,
                                      avx512vl_i32_info, avx512vl_i16_info>;
  defm : avx512_vptest_lowering_sizes<InstrStr # "D", OpNode, HasAVX512,
                                      avx512vl_i32_info, avx512vl_i64_info>;

  defm : avx512_vptest_lowering_sizes<InstrStr # "Q", OpNode, HasAVX512,
                                      avx512vl_i64_info, avx512vl_i8_info>;
  defm : avx512_vptest_lowering_sizes<InstrStr # "Q", OpNode, HasAVX512,
                                      avx512vl_i64_info, avx512vl_i16_info>;
  defm : avx512_vptest_lowering_sizes<InstrStr # "Q", OpNode, HasAVX512,
                                      avx512vl_i64_info, avx512vl_i32_info>;
}

defm : avx512_vptest_lowering_types<"VPTESTM", X86pcmpnem>;
defm : avx512_vptest_lowering_types<"VPTESTNM", X86pcmpeqm>;

//===----------------------------------------------------------------------===//
// AVX-512  Shift instructions
//===----------------------------------------------------------------------===//

multiclass avx512_shift_rmi<bits<8> opc, Format ImmFormR, Format ImmFormM,
                            string OpcodeStr, SDNode OpNode,
                            X86FoldableSchedWrite sched, X86VectorVTInfo _> {
  let ExeDomain = _.ExeDomain in {
  defm ri : AVX512_maskable<opc, ImmFormR, _, (outs _.RC:$dst),
                   (ins _.RC:$src1, u8imm:$src2), OpcodeStr,
                      "$src2, $src1", "$src1, $src2",
                   (_.VT (OpNode _.RC:$src1, (i8 imm:$src2)))>,
                   Sched<[sched]>;
  defm mi : AVX512_maskable<opc, ImmFormM, _, (outs _.RC:$dst),
                   (ins _.MemOp:$src1, u8imm:$src2), OpcodeStr,
                       "$src2, $src1", "$src1, $src2",
                   (_.VT (OpNode (_.VT (_.LdFrag addr:$src1)),
                          (i8 imm:$src2)))>,
                   Sched<[sched.Folded]>;
  }
}

multiclass avx512_shift_rmbi<bits<8> opc, Format ImmFormM,
                             string OpcodeStr, SDNode OpNode,
                             X86FoldableSchedWrite sched, X86VectorVTInfo _> {
  let ExeDomain = _.ExeDomain in
  defm mbi : AVX512_maskable<opc, ImmFormM, _, (outs _.RC:$dst),
                   (ins _.ScalarMemOp:$src1, u8imm:$src2), OpcodeStr,
      "$src2, ${src1}"##_.BroadcastStr, "${src1}"##_.BroadcastStr##", $src2",
     (_.VT (OpNode (X86VBroadcast (_.ScalarLdFrag addr:$src1)), (i8 imm:$src2)))>,
     EVEX_B, Sched<[sched.Folded]>;
}

multiclass avx512_shift_rrm<bits<8> opc, string OpcodeStr, SDNode OpNode,
                            X86FoldableSchedWrite sched, ValueType SrcVT,
                            X86VectorVTInfo _> {
   // src2 is always 128-bit
  let ExeDomain = _.ExeDomain in {
  defm rr : AVX512_maskable<opc, MRMSrcReg, _, (outs _.RC:$dst),
                   (ins _.RC:$src1, VR128X:$src2), OpcodeStr,
                      "$src2, $src1", "$src1, $src2",
                   (_.VT (OpNode _.RC:$src1, (SrcVT VR128X:$src2)))>,
                   AVX512BIBase, EVEX_4V, Sched<[sched]>;
  defm rm : AVX512_maskable<opc, MRMSrcMem, _, (outs _.RC:$dst),
                   (ins _.RC:$src1, i128mem:$src2), OpcodeStr,
                       "$src2, $src1", "$src1, $src2",
                   (_.VT (OpNode _.RC:$src1, (SrcVT (load addr:$src2))))>,
                   AVX512BIBase,
                   EVEX_4V, Sched<[sched.Folded, sched.ReadAfterFold]>;
  }
}

multiclass avx512_shift_sizes<bits<8> opc, string OpcodeStr, SDNode OpNode,
                              X86SchedWriteWidths sched, ValueType SrcVT,
                              AVX512VLVectorVTInfo VTInfo,
                              Predicate prd> {
  let Predicates = [prd] in
  defm Z    : avx512_shift_rrm<opc, OpcodeStr, OpNode, sched.ZMM, SrcVT,
                               VTInfo.info512>, EVEX_V512,
                               EVEX_CD8<VTInfo.info512.EltSize, CD8VQ> ;
  let Predicates = [prd, HasVLX] in {
  defm Z256 : avx512_shift_rrm<opc, OpcodeStr, OpNode, sched.YMM, SrcVT,
                               VTInfo.info256>, EVEX_V256,
                               EVEX_CD8<VTInfo.info256.EltSize, CD8VH>;
  defm Z128 : avx512_shift_rrm<opc, OpcodeStr, OpNode, sched.XMM, SrcVT,
                               VTInfo.info128>, EVEX_V128,
                               EVEX_CD8<VTInfo.info128.EltSize, CD8VF>;
  }
}

multiclass avx512_shift_types<bits<8> opcd, bits<8> opcq, bits<8> opcw,
                              string OpcodeStr, SDNode OpNode,
                              X86SchedWriteWidths sched,
                              bit NotEVEX2VEXConvertibleQ = 0> {
  defm D : avx512_shift_sizes<opcd, OpcodeStr#"d", OpNode, sched, v4i32,
                              avx512vl_i32_info, HasAVX512>;
  let notEVEX2VEXConvertible = NotEVEX2VEXConvertibleQ in
  defm Q : avx512_shift_sizes<opcq, OpcodeStr#"q", OpNode, sched, v2i64,
                              avx512vl_i64_info, HasAVX512>, VEX_W;
  defm W : avx512_shift_sizes<opcw, OpcodeStr#"w", OpNode, sched, v8i16,
                              avx512vl_i16_info, HasBWI>;
}

multiclass avx512_shift_rmi_sizes<bits<8> opc, Format ImmFormR, Format ImmFormM,
                                  string OpcodeStr, SDNode OpNode,
                                  X86SchedWriteWidths sched,
                                  AVX512VLVectorVTInfo VTInfo> {
  let Predicates = [HasAVX512] in
  defm Z:    avx512_shift_rmi<opc, ImmFormR, ImmFormM, OpcodeStr, OpNode,
                              sched.ZMM, VTInfo.info512>,
             avx512_shift_rmbi<opc, ImmFormM, OpcodeStr, OpNode, sched.ZMM,
                               VTInfo.info512>, EVEX_V512;
  let Predicates = [HasAVX512, HasVLX] in {
  defm Z256: avx512_shift_rmi<opc, ImmFormR, ImmFormM, OpcodeStr, OpNode,
                              sched.YMM, VTInfo.info256>,
             avx512_shift_rmbi<opc, ImmFormM, OpcodeStr, OpNode, sched.YMM,
                               VTInfo.info256>, EVEX_V256;
  defm Z128: avx512_shift_rmi<opc, ImmFormR, ImmFormM, OpcodeStr, OpNode,
                              sched.XMM, VTInfo.info128>,
             avx512_shift_rmbi<opc, ImmFormM, OpcodeStr, OpNode, sched.XMM,
                               VTInfo.info128>, EVEX_V128;
  }
}

multiclass avx512_shift_rmi_w<bits<8> opcw, Format ImmFormR, Format ImmFormM,
                              string OpcodeStr, SDNode OpNode,
                              X86SchedWriteWidths sched> {
  let Predicates = [HasBWI] in
  defm WZ:    avx512_shift_rmi<opcw, ImmFormR, ImmFormM, OpcodeStr, OpNode,
                               sched.ZMM, v32i16_info>, EVEX_V512, VEX_WIG;
  let Predicates = [HasVLX, HasBWI] in {
  defm WZ256: avx512_shift_rmi<opcw, ImmFormR, ImmFormM, OpcodeStr, OpNode,
                               sched.YMM, v16i16x_info>, EVEX_V256, VEX_WIG;
  defm WZ128: avx512_shift_rmi<opcw, ImmFormR, ImmFormM, OpcodeStr, OpNode,
                               sched.XMM, v8i16x_info>, EVEX_V128, VEX_WIG;
  }
}

multiclass avx512_shift_rmi_dq<bits<8> opcd, bits<8> opcq,
                               Format ImmFormR, Format ImmFormM,
                               string OpcodeStr, SDNode OpNode,
                               X86SchedWriteWidths sched,
                               bit NotEVEX2VEXConvertibleQ = 0> {
  defm D: avx512_shift_rmi_sizes<opcd, ImmFormR, ImmFormM, OpcodeStr#"d", OpNode,
                                 sched, avx512vl_i32_info>, EVEX_CD8<32, CD8VF>;
  let notEVEX2VEXConvertible = NotEVEX2VEXConvertibleQ in
  defm Q: avx512_shift_rmi_sizes<opcq, ImmFormR, ImmFormM, OpcodeStr#"q", OpNode,
                                 sched, avx512vl_i64_info>, EVEX_CD8<64, CD8VF>, VEX_W;
}

defm VPSRL : avx512_shift_rmi_dq<0x72, 0x73, MRM2r, MRM2m, "vpsrl", X86vsrli,
                                 SchedWriteVecShiftImm>,
             avx512_shift_rmi_w<0x71, MRM2r, MRM2m, "vpsrlw", X86vsrli,
                                SchedWriteVecShiftImm>, AVX512BIi8Base, EVEX_4V;

defm VPSLL : avx512_shift_rmi_dq<0x72, 0x73, MRM6r, MRM6m, "vpsll", X86vshli,
                                 SchedWriteVecShiftImm>,
             avx512_shift_rmi_w<0x71, MRM6r, MRM6m, "vpsllw", X86vshli,
                                SchedWriteVecShiftImm>, AVX512BIi8Base, EVEX_4V;

defm VPSRA : avx512_shift_rmi_dq<0x72, 0x72, MRM4r, MRM4m, "vpsra", X86vsrai,
                                 SchedWriteVecShiftImm, 1>,
             avx512_shift_rmi_w<0x71, MRM4r, MRM4m, "vpsraw", X86vsrai,
                                SchedWriteVecShiftImm>, AVX512BIi8Base, EVEX_4V;

defm VPROR : avx512_shift_rmi_dq<0x72, 0x72, MRM0r, MRM0m, "vpror", X86vrotri,
                                 SchedWriteVecShiftImm>, AVX512BIi8Base, EVEX_4V;
defm VPROL : avx512_shift_rmi_dq<0x72, 0x72, MRM1r, MRM1m, "vprol", X86vrotli,
                                 SchedWriteVecShiftImm>, AVX512BIi8Base, EVEX_4V;

defm VPSLL : avx512_shift_types<0xF2, 0xF3, 0xF1, "vpsll", X86vshl,
                                SchedWriteVecShift>;
defm VPSRA : avx512_shift_types<0xE2, 0xE2, 0xE1, "vpsra", X86vsra,
                                SchedWriteVecShift, 1>;
defm VPSRL : avx512_shift_types<0xD2, 0xD3, 0xD1, "vpsrl", X86vsrl,
                                SchedWriteVecShift>;

// Use 512bit VPSRA/VPSRAI version to implement v2i64/v4i64 in case NoVLX.
let Predicates = [HasAVX512, NoVLX] in {
  def : Pat<(v4i64 (X86vsra (v4i64 VR256X:$src1), (v2i64 VR128X:$src2))),
            (EXTRACT_SUBREG (v8i64
              (VPSRAQZrr
                (v8i64 (INSERT_SUBREG (IMPLICIT_DEF), VR256X:$src1, sub_ymm)),
                 VR128X:$src2)), sub_ymm)>;

  def : Pat<(v2i64 (X86vsra (v2i64 VR128X:$src1), (v2i64 VR128X:$src2))),
            (EXTRACT_SUBREG (v8i64
              (VPSRAQZrr
                (v8i64 (INSERT_SUBREG (IMPLICIT_DEF), VR128X:$src1, sub_xmm)),
                 VR128X:$src2)), sub_xmm)>;

  def : Pat<(v4i64 (X86vsrai (v4i64 VR256X:$src1), (i8 imm:$src2))),
            (EXTRACT_SUBREG (v8i64
              (VPSRAQZri
                (v8i64 (INSERT_SUBREG (IMPLICIT_DEF), VR256X:$src1, sub_ymm)),
                 imm:$src2)), sub_ymm)>;

  def : Pat<(v2i64 (X86vsrai (v2i64 VR128X:$src1), (i8 imm:$src2))),
            (EXTRACT_SUBREG (v8i64
              (VPSRAQZri
                (v8i64 (INSERT_SUBREG (IMPLICIT_DEF), VR128X:$src1, sub_xmm)),
                 imm:$src2)), sub_xmm)>;
}

//===-------------------------------------------------------------------===//
// Variable Bit Shifts
//===-------------------------------------------------------------------===//

multiclass avx512_var_shift<bits<8> opc, string OpcodeStr, SDNode OpNode,
                            X86FoldableSchedWrite sched, X86VectorVTInfo _> {
  let ExeDomain = _.ExeDomain in {
  defm rr : AVX512_maskable<opc, MRMSrcReg, _, (outs _.RC:$dst),
                   (ins _.RC:$src1, _.RC:$src2), OpcodeStr,
                      "$src2, $src1", "$src1, $src2",
                   (_.VT (OpNode _.RC:$src1, (_.VT _.RC:$src2)))>,
                   AVX5128IBase, EVEX_4V, Sched<[sched]>;
  defm rm : AVX512_maskable<opc, MRMSrcMem, _, (outs _.RC:$dst),
                   (ins _.RC:$src1, _.MemOp:$src2), OpcodeStr,
                       "$src2, $src1", "$src1, $src2",
                   (_.VT (OpNode _.RC:$src1,
                   (_.VT (_.LdFrag addr:$src2))))>,
                   AVX5128IBase, EVEX_4V, EVEX_CD8<_.EltSize, CD8VF>,
                   Sched<[sched.Folded, sched.ReadAfterFold]>;
  }
}

multiclass avx512_var_shift_mb<bits<8> opc, string OpcodeStr, SDNode OpNode,
                               X86FoldableSchedWrite sched, X86VectorVTInfo _> {
  let ExeDomain = _.ExeDomain in
  defm rmb : AVX512_maskable<opc, MRMSrcMem, _, (outs _.RC:$dst),
                    (ins _.RC:$src1, _.ScalarMemOp:$src2), OpcodeStr,
                    "${src2}"##_.BroadcastStr##", $src1",
                    "$src1, ${src2}"##_.BroadcastStr,
                    (_.VT (OpNode _.RC:$src1, (_.VT (X86VBroadcast
                                                (_.ScalarLdFrag addr:$src2)))))>,
                    AVX5128IBase, EVEX_B, EVEX_4V, EVEX_CD8<_.EltSize, CD8VF>,
                    Sched<[sched.Folded, sched.ReadAfterFold]>;
}

multiclass avx512_var_shift_sizes<bits<8> opc, string OpcodeStr, SDNode OpNode,
                                  X86SchedWriteWidths sched, AVX512VLVectorVTInfo _> {
  let Predicates  = [HasAVX512] in
  defm Z : avx512_var_shift<opc, OpcodeStr, OpNode, sched.ZMM, _.info512>,
           avx512_var_shift_mb<opc, OpcodeStr, OpNode, sched.ZMM, _.info512>, EVEX_V512;

  let Predicates = [HasAVX512, HasVLX] in {
  defm Z256 : avx512_var_shift<opc, OpcodeStr, OpNode, sched.YMM, _.info256>,
              avx512_var_shift_mb<opc, OpcodeStr, OpNode, sched.YMM, _.info256>, EVEX_V256;
  defm Z128 : avx512_var_shift<opc, OpcodeStr, OpNode, sched.XMM, _.info128>,
              avx512_var_shift_mb<opc, OpcodeStr, OpNode, sched.XMM, _.info128>, EVEX_V128;
  }
}

multiclass avx512_var_shift_types<bits<8> opc, string OpcodeStr,
                                  SDNode OpNode, X86SchedWriteWidths sched> {
  defm D : avx512_var_shift_sizes<opc, OpcodeStr#"d", OpNode, sched,
                                 avx512vl_i32_info>;
  defm Q : avx512_var_shift_sizes<opc, OpcodeStr#"q", OpNode, sched,
                                 avx512vl_i64_info>, VEX_W;
}

// Use 512bit version to implement 128/256 bit in case NoVLX.
multiclass avx512_var_shift_lowering<AVX512VLVectorVTInfo _, string OpcodeStr,
                                     SDNode OpNode, list<Predicate> p> {
  let Predicates = p in {
  def : Pat<(_.info256.VT (OpNode (_.info256.VT _.info256.RC:$src1),
                                  (_.info256.VT _.info256.RC:$src2))),
            (EXTRACT_SUBREG
                (!cast<Instruction>(OpcodeStr#"Zrr")
                    (INSERT_SUBREG (_.info512.VT (IMPLICIT_DEF)), VR256X:$src1, sub_ymm),
                    (INSERT_SUBREG (_.info512.VT (IMPLICIT_DEF)), VR256X:$src2, sub_ymm)),
             sub_ymm)>;

  def : Pat<(_.info128.VT (OpNode (_.info128.VT _.info128.RC:$src1),
                                  (_.info128.VT _.info128.RC:$src2))),
            (EXTRACT_SUBREG
                (!cast<Instruction>(OpcodeStr#"Zrr")
                    (INSERT_SUBREG (_.info512.VT (IMPLICIT_DEF)), VR128X:$src1, sub_xmm),
                    (INSERT_SUBREG (_.info512.VT (IMPLICIT_DEF)), VR128X:$src2, sub_xmm)),
             sub_xmm)>;
  }
}
multiclass avx512_var_shift_w<bits<8> opc, string OpcodeStr,
                              SDNode OpNode, X86SchedWriteWidths sched> {
  let Predicates = [HasBWI] in
  defm WZ:    avx512_var_shift<opc, OpcodeStr, OpNode, sched.ZMM, v32i16_info>,
              EVEX_V512, VEX_W;
  let Predicates = [HasVLX, HasBWI] in {

  defm WZ256: avx512_var_shift<opc, OpcodeStr, OpNode, sched.YMM, v16i16x_info>,
              EVEX_V256, VEX_W;
  defm WZ128: avx512_var_shift<opc, OpcodeStr, OpNode, sched.XMM, v8i16x_info>,
              EVEX_V128, VEX_W;
  }
}

defm VPSLLV : avx512_var_shift_types<0x47, "vpsllv", shl, SchedWriteVarVecShift>,
              avx512_var_shift_w<0x12, "vpsllvw", shl, SchedWriteVarVecShift>;

defm VPSRAV : avx512_var_shift_types<0x46, "vpsrav", sra, SchedWriteVarVecShift>,
              avx512_var_shift_w<0x11, "vpsravw", sra, SchedWriteVarVecShift>;

defm VPSRLV : avx512_var_shift_types<0x45, "vpsrlv", srl, SchedWriteVarVecShift>,
              avx512_var_shift_w<0x10, "vpsrlvw", srl, SchedWriteVarVecShift>;

defm VPRORV : avx512_var_shift_types<0x14, "vprorv", rotr, SchedWriteVarVecShift>;
defm VPROLV : avx512_var_shift_types<0x15, "vprolv", rotl, SchedWriteVarVecShift>;

defm : avx512_var_shift_lowering<avx512vl_i64_info, "VPSRAVQ", sra, [HasAVX512, NoVLX]>;
defm : avx512_var_shift_lowering<avx512vl_i16_info, "VPSLLVW", shl, [HasBWI, NoVLX]>;
defm : avx512_var_shift_lowering<avx512vl_i16_info, "VPSRAVW", sra, [HasBWI, NoVLX]>;
defm : avx512_var_shift_lowering<avx512vl_i16_info, "VPSRLVW", srl, [HasBWI, NoVLX]>;

// Special handing for handling VPSRAV intrinsics.
multiclass avx512_var_shift_int_lowering<string InstrStr, SDNode OpNode,
                                         X86VectorVTInfo _, list<Predicate> p> {
  let Predicates = p in {
    def : Pat<(_.VT (OpNode _.RC:$src1, _.RC:$src2)),
              (!cast<Instruction>(InstrStr#_.ZSuffix#rr) _.RC:$src1,
               _.RC:$src2)>;
    def : Pat<(_.VT (OpNode _.RC:$src1, (_.LdFrag addr:$src2))),
              (!cast<Instruction>(InstrStr#_.ZSuffix##rm)
               _.RC:$src1, addr:$src2)>;
    def : Pat<(_.VT (vselect _.KRCWM:$mask,
                     (OpNode _.RC:$src1, _.RC:$src2), _.RC:$src0)),
              (!cast<Instruction>(InstrStr#_.ZSuffix#rrk) _.RC:$src0,
               _.KRC:$mask, _.RC:$src1, _.RC:$src2)>;
    def : Pat<(_.VT (vselect _.KRCWM:$mask,
                     (OpNode _.RC:$src1, (_.LdFrag addr:$src2)),
                     _.RC:$src0)),
              (!cast<Instruction>(InstrStr#_.ZSuffix##rmk) _.RC:$src0,
               _.KRC:$mask, _.RC:$src1, addr:$src2)>;
    def : Pat<(_.VT (vselect _.KRCWM:$mask,
                     (OpNode _.RC:$src1, _.RC:$src2), _.ImmAllZerosV)),
              (!cast<Instruction>(InstrStr#_.ZSuffix#rrkz) _.KRC:$mask,
               _.RC:$src1, _.RC:$src2)>;
    def : Pat<(_.VT (vselect _.KRCWM:$mask,
                     (OpNode _.RC:$src1, (_.LdFrag addr:$src2)),
                     _.ImmAllZerosV)),
              (!cast<Instruction>(InstrStr#_.ZSuffix##rmkz) _.KRC:$mask,
               _.RC:$src1, addr:$src2)>;
  }
}

multiclass avx512_var_shift_int_lowering_mb<string InstrStr, SDNode OpNode,
                                            X86VectorVTInfo _,
                                            list<Predicate> p> :
           avx512_var_shift_int_lowering<InstrStr, OpNode, _, p> {
  let Predicates = p in {
    def : Pat<(_.VT (OpNode _.RC:$src1,
                     (X86VBroadcast (_.ScalarLdFrag addr:$src2)))),
              (!cast<Instruction>(InstrStr#_.ZSuffix##rmb)
               _.RC:$src1, addr:$src2)>;
    def : Pat<(_.VT (vselect _.KRCWM:$mask,
                     (OpNode _.RC:$src1,
                      (X86VBroadcast (_.ScalarLdFrag addr:$src2))),
                     _.RC:$src0)),
              (!cast<Instruction>(InstrStr#_.ZSuffix##rmbk) _.RC:$src0,
               _.KRC:$mask, _.RC:$src1, addr:$src2)>;
    def : Pat<(_.VT (vselect _.KRCWM:$mask,
                     (OpNode _.RC:$src1,
                      (X86VBroadcast (_.ScalarLdFrag addr:$src2))),
                     _.ImmAllZerosV)),
              (!cast<Instruction>(InstrStr#_.ZSuffix##rmbkz) _.KRC:$mask,
               _.RC:$src1, addr:$src2)>;
  }
}

multiclass avx512_var_shift_int_lowering_vl<string InstrStr, SDNode OpNode,
                                            AVX512VLVectorVTInfo VTInfo,
                                            Predicate p> {
  defm : avx512_var_shift_int_lowering<InstrStr, OpNode, VTInfo.info512, [p]>;
  defm : avx512_var_shift_int_lowering<InstrStr, OpNode, VTInfo.info256,
                                       [HasVLX, p]>;
  defm : avx512_var_shift_int_lowering<InstrStr, OpNode, VTInfo.info128,
                                       [HasVLX, p]>;
}

multiclass avx512_var_shift_int_lowering_mb_vl<string InstrStr, SDNode OpNode,
                                               AVX512VLVectorVTInfo VTInfo,
                                               Predicate p> {
  defm : avx512_var_shift_int_lowering_mb<InstrStr, OpNode, VTInfo.info512, [p]>;
  defm : avx512_var_shift_int_lowering_mb<InstrStr, OpNode, VTInfo.info256,
                                          [HasVLX, p]>;
  defm : avx512_var_shift_int_lowering_mb<InstrStr, OpNode, VTInfo.info128,
                                          [HasVLX, p]>;
}

defm : avx512_var_shift_int_lowering_vl<"VPSRAVW", X86vsrav, avx512vl_i16_info,
                                        HasBWI>;
defm : avx512_var_shift_int_lowering_mb_vl<"VPSRAVD", X86vsrav,
                                           avx512vl_i32_info, HasAVX512>;
defm : avx512_var_shift_int_lowering_mb_vl<"VPSRAVQ", X86vsrav,
                                           avx512vl_i64_info, HasAVX512>;

defm : avx512_var_shift_int_lowering_vl<"VPSRLVW", X86vsrlv, avx512vl_i16_info,
                                        HasBWI>;
defm : avx512_var_shift_int_lowering_mb_vl<"VPSRLVD", X86vsrlv,
                                           avx512vl_i32_info, HasAVX512>;
defm : avx512_var_shift_int_lowering_mb_vl<"VPSRLVQ", X86vsrlv,
                                           avx512vl_i64_info, HasAVX512>;

defm : avx512_var_shift_int_lowering_vl<"VPSLLVW", X86vshlv, avx512vl_i16_info,
                                        HasBWI>;
defm : avx512_var_shift_int_lowering_mb_vl<"VPSLLVD", X86vshlv,
                                           avx512vl_i32_info, HasAVX512>;
defm : avx512_var_shift_int_lowering_mb_vl<"VPSLLVQ", X86vshlv,
                                           avx512vl_i64_info, HasAVX512>;


// Use 512bit VPROL/VPROLI version to implement v2i64/v4i64 + v4i32/v8i32 in case NoVLX.
let Predicates = [HasAVX512, NoVLX] in {
  def : Pat<(v2i64 (rotl (v2i64 VR128X:$src1), (v2i64 VR128X:$src2))),
            (EXTRACT_SUBREG (v8i64
              (VPROLVQZrr
                (v8i64 (INSERT_SUBREG (IMPLICIT_DEF), VR128X:$src1, sub_xmm)),
                (v8i64 (INSERT_SUBREG (IMPLICIT_DEF), VR128X:$src2, sub_xmm)))),
                       sub_xmm)>;
  def : Pat<(v4i64 (rotl (v4i64 VR256X:$src1), (v4i64 VR256X:$src2))),
            (EXTRACT_SUBREG (v8i64
              (VPROLVQZrr
                (v8i64 (INSERT_SUBREG (IMPLICIT_DEF), VR256X:$src1, sub_ymm)),
                (v8i64 (INSERT_SUBREG (IMPLICIT_DEF), VR256X:$src2, sub_ymm)))),
                       sub_ymm)>;

  def : Pat<(v4i32 (rotl (v4i32 VR128X:$src1), (v4i32 VR128X:$src2))),
            (EXTRACT_SUBREG (v16i32
              (VPROLVDZrr
                (v16i32 (INSERT_SUBREG (IMPLICIT_DEF), VR128X:$src1, sub_xmm)),
                (v16i32 (INSERT_SUBREG (IMPLICIT_DEF), VR128X:$src2, sub_xmm)))),
                        sub_xmm)>;
  def : Pat<(v8i32 (rotl (v8i32 VR256X:$src1), (v8i32 VR256X:$src2))),
            (EXTRACT_SUBREG (v16i32
              (VPROLVDZrr
                (v16i32 (INSERT_SUBREG (IMPLICIT_DEF), VR256X:$src1, sub_ymm)),
                (v16i32 (INSERT_SUBREG (IMPLICIT_DEF), VR256X:$src2, sub_ymm)))),
                        sub_ymm)>;

  def : Pat<(v2i64 (X86vrotli (v2i64 VR128X:$src1), (i8 imm:$src2))),
            (EXTRACT_SUBREG (v8i64
              (VPROLQZri
                (v8i64 (INSERT_SUBREG (IMPLICIT_DEF), VR128X:$src1, sub_xmm)),
                        imm:$src2)), sub_xmm)>;
  def : Pat<(v4i64 (X86vrotli (v4i64 VR256X:$src1), (i8 imm:$src2))),
            (EXTRACT_SUBREG (v8i64
              (VPROLQZri
                (v8i64 (INSERT_SUBREG (IMPLICIT_DEF), VR256X:$src1, sub_ymm)),
                       imm:$src2)), sub_ymm)>;

  def : Pat<(v4i32 (X86vrotli (v4i32 VR128X:$src1), (i8 imm:$src2))),
            (EXTRACT_SUBREG (v16i32
              (VPROLDZri
                (v16i32 (INSERT_SUBREG (IMPLICIT_DEF), VR128X:$src1, sub_xmm)),
                        imm:$src2)), sub_xmm)>;
  def : Pat<(v8i32 (X86vrotli (v8i32 VR256X:$src1), (i8 imm:$src2))),
            (EXTRACT_SUBREG (v16i32
              (VPROLDZri
                (v16i32 (INSERT_SUBREG (IMPLICIT_DEF), VR256X:$src1, sub_ymm)),
                        imm:$src2)), sub_ymm)>;
}

// Use 512bit VPROR/VPRORI version to implement v2i64/v4i64 + v4i32/v8i32 in case NoVLX.
let Predicates = [HasAVX512, NoVLX] in {
  def : Pat<(v2i64 (rotr (v2i64 VR128X:$src1), (v2i64 VR128X:$src2))),
            (EXTRACT_SUBREG (v8i64
              (VPRORVQZrr
                (v8i64 (INSERT_SUBREG (IMPLICIT_DEF), VR128X:$src1, sub_xmm)),
                (v8i64 (INSERT_SUBREG (IMPLICIT_DEF), VR128X:$src2, sub_xmm)))),
                       sub_xmm)>;
  def : Pat<(v4i64 (rotr (v4i64 VR256X:$src1), (v4i64 VR256X:$src2))),
            (EXTRACT_SUBREG (v8i64
              (VPRORVQZrr
                (v8i64 (INSERT_SUBREG (IMPLICIT_DEF), VR256X:$src1, sub_ymm)),
                (v8i64 (INSERT_SUBREG (IMPLICIT_DEF), VR256X:$src2, sub_ymm)))),
                       sub_ymm)>;

  def : Pat<(v4i32 (rotr (v4i32 VR128X:$src1), (v4i32 VR128X:$src2))),
            (EXTRACT_SUBREG (v16i32
              (VPRORVDZrr
                (v16i32 (INSERT_SUBREG (IMPLICIT_DEF), VR128X:$src1, sub_xmm)),
                (v16i32 (INSERT_SUBREG (IMPLICIT_DEF), VR128X:$src2, sub_xmm)))),
                        sub_xmm)>;
  def : Pat<(v8i32 (rotr (v8i32 VR256X:$src1), (v8i32 VR256X:$src2))),
            (EXTRACT_SUBREG (v16i32
              (VPRORVDZrr
                (v16i32 (INSERT_SUBREG (IMPLICIT_DEF), VR256X:$src1, sub_ymm)),
                (v16i32 (INSERT_SUBREG (IMPLICIT_DEF), VR256X:$src2, sub_ymm)))),
                        sub_ymm)>;

  def : Pat<(v2i64 (X86vrotri (v2i64 VR128X:$src1), (i8 imm:$src2))),
            (EXTRACT_SUBREG (v8i64
              (VPRORQZri
                (v8i64 (INSERT_SUBREG (IMPLICIT_DEF), VR128X:$src1, sub_xmm)),
                        imm:$src2)), sub_xmm)>;
  def : Pat<(v4i64 (X86vrotri (v4i64 VR256X:$src1), (i8 imm:$src2))),
            (EXTRACT_SUBREG (v8i64
              (VPRORQZri
                (v8i64 (INSERT_SUBREG (IMPLICIT_DEF), VR256X:$src1, sub_ymm)),
                       imm:$src2)), sub_ymm)>;

  def : Pat<(v4i32 (X86vrotri (v4i32 VR128X:$src1), (i8 imm:$src2))),
            (EXTRACT_SUBREG (v16i32
              (VPRORDZri
                (v16i32 (INSERT_SUBREG (IMPLICIT_DEF), VR128X:$src1, sub_xmm)),
                        imm:$src2)), sub_xmm)>;
  def : Pat<(v8i32 (X86vrotri (v8i32 VR256X:$src1), (i8 imm:$src2))),
            (EXTRACT_SUBREG (v16i32
              (VPRORDZri
                (v16i32 (INSERT_SUBREG (IMPLICIT_DEF), VR256X:$src1, sub_ymm)),
                        imm:$src2)), sub_ymm)>;
}

//===-------------------------------------------------------------------===//
// 1-src variable permutation VPERMW/D/Q
//===-------------------------------------------------------------------===//

multiclass avx512_vperm_dq_sizes<bits<8> opc, string OpcodeStr, SDNode OpNode,
                                 X86FoldableSchedWrite sched, AVX512VLVectorVTInfo _> {
  let Predicates  = [HasAVX512] in
  defm Z : avx512_var_shift<opc, OpcodeStr, OpNode, sched, _.info512>,
           avx512_var_shift_mb<opc, OpcodeStr, OpNode, sched, _.info512>, EVEX_V512;

  let Predicates = [HasAVX512, HasVLX] in
  defm Z256 : avx512_var_shift<opc, OpcodeStr, OpNode, sched, _.info256>,
              avx512_var_shift_mb<opc, OpcodeStr, OpNode, sched, _.info256>, EVEX_V256;
}

multiclass avx512_vpermi_dq_sizes<bits<8> opc, Format ImmFormR, Format ImmFormM,
                                 string OpcodeStr, SDNode OpNode,
                                 X86FoldableSchedWrite sched, AVX512VLVectorVTInfo VTInfo> {
  let Predicates = [HasAVX512] in
  defm Z:    avx512_shift_rmi<opc, ImmFormR, ImmFormM, OpcodeStr, OpNode,
                              sched, VTInfo.info512>,
             avx512_shift_rmbi<opc, ImmFormM, OpcodeStr, OpNode,
                               sched, VTInfo.info512>, EVEX_V512;
  let Predicates = [HasAVX512, HasVLX] in
  defm Z256: avx512_shift_rmi<opc, ImmFormR, ImmFormM, OpcodeStr, OpNode,
                              sched, VTInfo.info256>,
             avx512_shift_rmbi<opc, ImmFormM, OpcodeStr, OpNode,
                               sched, VTInfo.info256>, EVEX_V256;
}

multiclass avx512_vperm_bw<bits<8> opc, string OpcodeStr,
                              Predicate prd, SDNode OpNode,
                              X86FoldableSchedWrite sched, AVX512VLVectorVTInfo _> {
  let Predicates = [prd] in
  defm Z:    avx512_var_shift<opc, OpcodeStr, OpNode, sched, _.info512>,
              EVEX_V512 ;
  let Predicates = [HasVLX, prd] in {
  defm Z256: avx512_var_shift<opc, OpcodeStr, OpNode, sched, _.info256>,
              EVEX_V256 ;
  defm Z128: avx512_var_shift<opc, OpcodeStr, OpNode, sched, _.info128>,
              EVEX_V128 ;
  }
}

defm VPERMW  : avx512_vperm_bw<0x8D, "vpermw", HasBWI, X86VPermv,
                               WriteVarShuffle256, avx512vl_i16_info>, VEX_W;
defm VPERMB  : avx512_vperm_bw<0x8D, "vpermb", HasVBMI, X86VPermv,
                               WriteVarShuffle256, avx512vl_i8_info>;

defm VPERMD : avx512_vperm_dq_sizes<0x36, "vpermd", X86VPermv,
                                    WriteVarShuffle256, avx512vl_i32_info>;
defm VPERMQ : avx512_vperm_dq_sizes<0x36, "vpermq", X86VPermv,
                                    WriteVarShuffle256, avx512vl_i64_info>, VEX_W;
defm VPERMPS : avx512_vperm_dq_sizes<0x16, "vpermps", X86VPermv,
                                     WriteFVarShuffle256, avx512vl_f32_info>;
defm VPERMPD : avx512_vperm_dq_sizes<0x16, "vpermpd", X86VPermv,
                                     WriteFVarShuffle256, avx512vl_f64_info>, VEX_W;

defm VPERMQ : avx512_vpermi_dq_sizes<0x00, MRMSrcReg, MRMSrcMem, "vpermq",
                             X86VPermi, WriteShuffle256, avx512vl_i64_info>,
                             EVEX, AVX512AIi8Base, EVEX_CD8<64, CD8VF>, VEX_W;
defm VPERMPD : avx512_vpermi_dq_sizes<0x01, MRMSrcReg, MRMSrcMem, "vpermpd",
                             X86VPermi, WriteFShuffle256, avx512vl_f64_info>,
                             EVEX, AVX512AIi8Base, EVEX_CD8<64, CD8VF>, VEX_W;

//===----------------------------------------------------------------------===//
// AVX-512 - VPERMIL
//===----------------------------------------------------------------------===//

multiclass avx512_permil_vec<bits<8> OpcVar, string OpcodeStr, SDNode OpNode,
                             X86FoldableSchedWrite sched, X86VectorVTInfo _,
                             X86VectorVTInfo Ctrl> {
  defm rr: AVX512_maskable<OpcVar, MRMSrcReg, _, (outs _.RC:$dst),
                  (ins _.RC:$src1, Ctrl.RC:$src2), OpcodeStr,
                  "$src2, $src1", "$src1, $src2",
                  (_.VT (OpNode _.RC:$src1,
                               (Ctrl.VT Ctrl.RC:$src2)))>,
                  T8PD, EVEX_4V, Sched<[sched]>;
  defm rm: AVX512_maskable<OpcVar, MRMSrcMem, _, (outs _.RC:$dst),
                  (ins _.RC:$src1, Ctrl.MemOp:$src2), OpcodeStr,
                  "$src2, $src1", "$src1, $src2",
                  (_.VT (OpNode
                           _.RC:$src1,
                           (Ctrl.VT (Ctrl.LdFrag addr:$src2))))>,
                  T8PD, EVEX_4V, EVEX_CD8<_.EltSize, CD8VF>,
                  Sched<[sched.Folded, sched.ReadAfterFold]>;
  defm rmb: AVX512_maskable<OpcVar, MRMSrcMem, _, (outs _.RC:$dst),
                   (ins _.RC:$src1, _.ScalarMemOp:$src2), OpcodeStr,
                   "${src2}"##_.BroadcastStr##", $src1",
                   "$src1, ${src2}"##_.BroadcastStr,
                   (_.VT (OpNode
                            _.RC:$src1,
                            (Ctrl.VT (X86VBroadcast
                                       (Ctrl.ScalarLdFrag addr:$src2)))))>,
                   T8PD, EVEX_4V, EVEX_B, EVEX_CD8<_.EltSize, CD8VF>,
                   Sched<[sched.Folded, sched.ReadAfterFold]>;
}

multiclass avx512_permil_vec_common<string OpcodeStr, bits<8> OpcVar,
                                    X86SchedWriteWidths sched,
                                    AVX512VLVectorVTInfo _,
                                    AVX512VLVectorVTInfo Ctrl> {
  let Predicates = [HasAVX512] in {
    defm Z    : avx512_permil_vec<OpcVar, OpcodeStr, X86VPermilpv, sched.ZMM,
                                  _.info512, Ctrl.info512>, EVEX_V512;
  }
  let Predicates = [HasAVX512, HasVLX] in {
    defm Z128 : avx512_permil_vec<OpcVar, OpcodeStr, X86VPermilpv, sched.XMM,
                                  _.info128, Ctrl.info128>, EVEX_V128;
    defm Z256 : avx512_permil_vec<OpcVar, OpcodeStr, X86VPermilpv, sched.YMM,
                                  _.info256, Ctrl.info256>, EVEX_V256;
  }
}

multiclass avx512_permil<string OpcodeStr, bits<8> OpcImm, bits<8> OpcVar,
                         AVX512VLVectorVTInfo _, AVX512VLVectorVTInfo Ctrl>{
  defm NAME: avx512_permil_vec_common<OpcodeStr, OpcVar, SchedWriteFVarShuffle,
                                      _, Ctrl>;
  defm NAME: avx512_shift_rmi_sizes<OpcImm, MRMSrcReg, MRMSrcMem, OpcodeStr,
                                    X86VPermilpi, SchedWriteFShuffle, _>,
                    EVEX, AVX512AIi8Base, EVEX_CD8<_.info128.EltSize, CD8VF>;
}

let ExeDomain = SSEPackedSingle in
defm VPERMILPS : avx512_permil<"vpermilps", 0x04, 0x0C, avx512vl_f32_info,
                               avx512vl_i32_info>;
let ExeDomain = SSEPackedDouble in
defm VPERMILPD : avx512_permil<"vpermilpd", 0x05, 0x0D, avx512vl_f64_info,
                               avx512vl_i64_info>, VEX_W1X;

//===----------------------------------------------------------------------===//
// AVX-512 - VPSHUFD, VPSHUFLW, VPSHUFHW
//===----------------------------------------------------------------------===//

defm VPSHUFD : avx512_shift_rmi_sizes<0x70, MRMSrcReg, MRMSrcMem, "vpshufd",
                             X86PShufd, SchedWriteShuffle, avx512vl_i32_info>,
                             EVEX, AVX512BIi8Base, EVEX_CD8<32, CD8VF>;
defm VPSHUFH : avx512_shift_rmi_w<0x70, MRMSrcReg, MRMSrcMem, "vpshufhw",
                                  X86PShufhw, SchedWriteShuffle>,
                                  EVEX, AVX512XSIi8Base;
defm VPSHUFL : avx512_shift_rmi_w<0x70, MRMSrcReg, MRMSrcMem, "vpshuflw",
                                  X86PShuflw, SchedWriteShuffle>,
                                  EVEX, AVX512XDIi8Base;

//===----------------------------------------------------------------------===//
// AVX-512 - VPSHUFB
//===----------------------------------------------------------------------===//

multiclass avx512_pshufb_sizes<bits<8> opc, string OpcodeStr, SDNode OpNode,
                               X86SchedWriteWidths sched> {
  let Predicates = [HasBWI] in
  defm Z:    avx512_var_shift<opc, OpcodeStr, OpNode, sched.ZMM, v64i8_info>,
                              EVEX_V512;

  let Predicates = [HasVLX, HasBWI] in {
  defm Z256: avx512_var_shift<opc, OpcodeStr, OpNode, sched.YMM, v32i8x_info>,
                              EVEX_V256;
  defm Z128: avx512_var_shift<opc, OpcodeStr, OpNode, sched.XMM, v16i8x_info>,
                              EVEX_V128;
  }
}

defm VPSHUFB: avx512_pshufb_sizes<0x00, "vpshufb", X86pshufb,
                                  SchedWriteVarShuffle>, VEX_WIG;

//===----------------------------------------------------------------------===//
// Move Low to High and High to Low packed FP Instructions
//===----------------------------------------------------------------------===//

def VMOVLHPSZrr : AVX512PSI<0x16, MRMSrcReg, (outs VR128X:$dst),
          (ins VR128X:$src1, VR128X:$src2),
          "vmovlhps\t{$src2, $src1, $dst|$dst, $src1, $src2}",
          [(set VR128X:$dst, (v4f32 (X86Movlhps VR128X:$src1, VR128X:$src2)))]>,
          Sched<[SchedWriteFShuffle.XMM]>, EVEX_4V;
let isCommutable = 1 in
def VMOVHLPSZrr : AVX512PSI<0x12, MRMSrcReg, (outs VR128X:$dst),
          (ins VR128X:$src1, VR128X:$src2),
          "vmovhlps\t{$src2, $src1, $dst|$dst, $src1, $src2}",
          [(set VR128X:$dst, (v4f32 (X86Movhlps VR128X:$src1, VR128X:$src2)))]>,
          Sched<[SchedWriteFShuffle.XMM]>, EVEX_4V, NotMemoryFoldable;

//===----------------------------------------------------------------------===//
// VMOVHPS/PD VMOVLPS Instructions
// All patterns was taken from SSS implementation.
//===----------------------------------------------------------------------===//

multiclass avx512_mov_hilo_packed<bits<8> opc, string OpcodeStr,
                                  SDPatternOperator OpNode,
                                  X86VectorVTInfo _> {
  let hasSideEffects = 0, mayLoad = 1, ExeDomain = _.ExeDomain in
  def rm : AVX512<opc, MRMSrcMem, (outs _.RC:$dst),
                  (ins _.RC:$src1, f64mem:$src2),
                  !strconcat(OpcodeStr,
                             "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
                  [(set _.RC:$dst,
                     (OpNode _.RC:$src1,
                       (_.VT (bitconvert
                         (v2f64 (scalar_to_vector (loadf64 addr:$src2)))))))]>,
                  Sched<[SchedWriteFShuffle.XMM.Folded, SchedWriteFShuffle.XMM.ReadAfterFold]>, EVEX_4V;
}

// No patterns for MOVLPS/MOVHPS as the Movlhps node should only be created in
// SSE1. And MOVLPS pattern is even more complex.
defm VMOVHPSZ128 : avx512_mov_hilo_packed<0x16, "vmovhps", null_frag,
                                  v4f32x_info>, EVEX_CD8<32, CD8VT2>, PS;
defm VMOVHPDZ128 : avx512_mov_hilo_packed<0x16, "vmovhpd", X86Unpckl,
                                  v2f64x_info>, EVEX_CD8<64, CD8VT1>, PD, VEX_W;
defm VMOVLPSZ128 : avx512_mov_hilo_packed<0x12, "vmovlps", null_frag,
                                  v4f32x_info>, EVEX_CD8<32, CD8VT2>, PS;
defm VMOVLPDZ128 : avx512_mov_hilo_packed<0x12, "vmovlpd", X86Movsd,
                                  v2f64x_info>, EVEX_CD8<64, CD8VT1>, PD, VEX_W;

let Predicates = [HasAVX512] in {
  // VMOVHPD patterns
  def : Pat<(v2f64 (X86Unpckl VR128X:$src1,
                    (bc_v2f64 (v2i64 (scalar_to_vector (loadi64 addr:$src2)))))),
           (VMOVHPDZ128rm VR128X:$src1, addr:$src2)>;
}

let SchedRW = [WriteFStore] in {
def VMOVHPSZ128mr : AVX512PSI<0x17, MRMDestMem, (outs),
                       (ins f64mem:$dst, VR128X:$src),
                       "vmovhps\t{$src, $dst|$dst, $src}",
                       [(store (f64 (extractelt
                                     (X86Unpckh (bc_v2f64 (v4f32 VR128X:$src)),
                                                (bc_v2f64 (v4f32 VR128X:$src))),
                                     (iPTR 0))), addr:$dst)]>,
                       EVEX, EVEX_CD8<32, CD8VT2>;
def VMOVHPDZ128mr : AVX512PDI<0x17, MRMDestMem, (outs),
                       (ins f64mem:$dst, VR128X:$src),
                       "vmovhpd\t{$src, $dst|$dst, $src}",
                       [(store (f64 (extractelt
                                     (v2f64 (X86Unpckh VR128X:$src, VR128X:$src)),
                                     (iPTR 0))), addr:$dst)]>,
                       EVEX, EVEX_CD8<64, CD8VT1>, VEX_W;
def VMOVLPSZ128mr : AVX512PSI<0x13, MRMDestMem, (outs),
                       (ins f64mem:$dst, VR128X:$src),
                       "vmovlps\t{$src, $dst|$dst, $src}",
                       [(store (f64 (extractelt (bc_v2f64 (v4f32 VR128X:$src)),
                                     (iPTR 0))), addr:$dst)]>,
                       EVEX, EVEX_CD8<32, CD8VT2>;
def VMOVLPDZ128mr : AVX512PDI<0x13, MRMDestMem, (outs),
                       (ins f64mem:$dst, VR128X:$src),
                       "vmovlpd\t{$src, $dst|$dst, $src}",
                       [(store (f64 (extractelt (v2f64 VR128X:$src),
                                     (iPTR 0))), addr:$dst)]>,
                       EVEX, EVEX_CD8<64, CD8VT1>, VEX_W;
} // SchedRW

let Predicates = [HasAVX512] in {
  // VMOVHPD patterns
  def : Pat<(store (f64 (extractelt
                           (v2f64 (X86VPermilpi VR128X:$src, (i8 1))),
                           (iPTR 0))), addr:$dst),
           (VMOVHPDZ128mr addr:$dst, VR128X:$src)>;
}
//===----------------------------------------------------------------------===//
// FMA - Fused Multiply Operations
//

multiclass avx512_fma3p_213_rm<bits<8> opc, string OpcodeStr, SDNode OpNode,
                               X86FoldableSchedWrite sched,
                               X86VectorVTInfo _, string Suff> {
  let Constraints = "$src1 = $dst", ExeDomain = _.ExeDomain, hasSideEffects = 0 in {
  defm r: AVX512_maskable_3src<opc, MRMSrcReg, _, (outs _.RC:$dst),
          (ins _.RC:$src2, _.RC:$src3),
          OpcodeStr, "$src3, $src2", "$src2, $src3",
          (_.VT (OpNode _.RC:$src2, _.RC:$src1, _.RC:$src3)), 1, 1>,
          AVX512FMA3Base, Sched<[sched]>;

  defm m: AVX512_maskable_3src<opc, MRMSrcMem, _, (outs _.RC:$dst),
          (ins _.RC:$src2, _.MemOp:$src3),
          OpcodeStr, "$src3, $src2", "$src2, $src3",
          (_.VT (OpNode _.RC:$src2, _.RC:$src1, (_.LdFrag addr:$src3))), 1, 0>,
          AVX512FMA3Base, Sched<[sched.Folded, sched.ReadAfterFold]>;

  defm mb: AVX512_maskable_3src<opc, MRMSrcMem, _, (outs _.RC:$dst),
            (ins _.RC:$src2, _.ScalarMemOp:$src3),
            OpcodeStr,   !strconcat("${src3}", _.BroadcastStr,", $src2"),
            !strconcat("$src2, ${src3}", _.BroadcastStr ),
            (OpNode _.RC:$src2,
             _.RC:$src1,(_.VT (X86VBroadcast (_.ScalarLdFrag addr:$src3)))), 1, 0>,
             AVX512FMA3Base, EVEX_B, Sched<[sched.Folded, sched.ReadAfterFold]>;
  }
}

multiclass avx512_fma3_213_round<bits<8> opc, string OpcodeStr, SDNode OpNode,
                                 X86FoldableSchedWrite sched,
                                 X86VectorVTInfo _, string Suff> {
  let Constraints = "$src1 = $dst", ExeDomain = _.ExeDomain, hasSideEffects = 0 in
  defm rb: AVX512_maskable_3src<opc, MRMSrcReg, _, (outs _.RC:$dst),
          (ins _.RC:$src2, _.RC:$src3, AVX512RC:$rc),
          OpcodeStr, "$rc, $src3, $src2", "$src2, $src3, $rc",
          (_.VT ( OpNode _.RC:$src2, _.RC:$src1, _.RC:$src3, (i32 imm:$rc))), 1, 1>,
          AVX512FMA3Base, EVEX_B, EVEX_RC, Sched<[sched]>;
}

multiclass avx512_fma3p_213_common<bits<8> opc, string OpcodeStr, SDNode OpNode,
                                   SDNode OpNodeRnd, X86SchedWriteWidths sched,
                                   AVX512VLVectorVTInfo _, string Suff> {
  let Predicates = [HasAVX512] in {
    defm Z      : avx512_fma3p_213_rm<opc, OpcodeStr, OpNode, sched.ZMM,
                                      _.info512, Suff>,
                  avx512_fma3_213_round<opc, OpcodeStr, OpNodeRnd, sched.ZMM,
                                        _.info512, Suff>,
                              EVEX_V512, EVEX_CD8<_.info512.EltSize, CD8VF>;
  }
  let Predicates = [HasVLX, HasAVX512] in {
    defm Z256 : avx512_fma3p_213_rm<opc, OpcodeStr, OpNode, sched.YMM,
                                    _.info256, Suff>,
                      EVEX_V256, EVEX_CD8<_.info256.EltSize, CD8VF>;
    defm Z128 : avx512_fma3p_213_rm<opc, OpcodeStr, OpNode, sched.XMM,
                                    _.info128, Suff>,
                      EVEX_V128, EVEX_CD8<_.info128.EltSize, CD8VF>;
  }
}

multiclass avx512_fma3p_213_f<bits<8> opc, string OpcodeStr, SDNode OpNode,
                              SDNode OpNodeRnd> {
    defm PS : avx512_fma3p_213_common<opc, OpcodeStr#"ps", OpNode, OpNodeRnd,
                                      SchedWriteFMA, avx512vl_f32_info, "PS">;
    defm PD : avx512_fma3p_213_common<opc, OpcodeStr#"pd", OpNode, OpNodeRnd,
                                      SchedWriteFMA, avx512vl_f64_info, "PD">,
                                      VEX_W;
}

defm VFMADD213    : avx512_fma3p_213_f<0xA8, "vfmadd213", X86Fmadd, X86FmaddRnd>;
defm VFMSUB213    : avx512_fma3p_213_f<0xAA, "vfmsub213", X86Fmsub, X86FmsubRnd>;
defm VFMADDSUB213 : avx512_fma3p_213_f<0xA6, "vfmaddsub213", X86Fmaddsub, X86FmaddsubRnd>;
defm VFMSUBADD213 : avx512_fma3p_213_f<0xA7, "vfmsubadd213", X86Fmsubadd, X86FmsubaddRnd>;
defm VFNMADD213   : avx512_fma3p_213_f<0xAC, "vfnmadd213", X86Fnmadd, X86FnmaddRnd>;
defm VFNMSUB213   : avx512_fma3p_213_f<0xAE, "vfnmsub213", X86Fnmsub, X86FnmsubRnd>;


multiclass avx512_fma3p_231_rm<bits<8> opc, string OpcodeStr, SDNode OpNode,
                               X86FoldableSchedWrite sched,
                               X86VectorVTInfo _, string Suff> {
  let Constraints = "$src1 = $dst", ExeDomain = _.ExeDomain, hasSideEffects = 0 in {
  defm r: AVX512_maskable_3src<opc, MRMSrcReg, _, (outs _.RC:$dst),
          (ins _.RC:$src2, _.RC:$src3),
          OpcodeStr, "$src3, $src2", "$src2, $src3",
          (_.VT (OpNode _.RC:$src2, _.RC:$src3, _.RC:$src1)), 1, 1,
          vselect, 1>, AVX512FMA3Base, Sched<[sched]>;

  defm m: AVX512_maskable_3src<opc, MRMSrcMem, _, (outs _.RC:$dst),
          (ins _.RC:$src2, _.MemOp:$src3),
          OpcodeStr, "$src3, $src2", "$src2, $src3",
          (_.VT (OpNode _.RC:$src2, (_.LdFrag addr:$src3), _.RC:$src1)), 1, 0>,
          AVX512FMA3Base, Sched<[sched.Folded, sched.ReadAfterFold]>;

  defm mb: AVX512_maskable_3src<opc, MRMSrcMem, _, (outs _.RC:$dst),
         (ins _.RC:$src2, _.ScalarMemOp:$src3),
         OpcodeStr, "${src3}"##_.BroadcastStr##", $src2",
         "$src2, ${src3}"##_.BroadcastStr,
         (_.VT (OpNode _.RC:$src2,
                      (_.VT (X86VBroadcast(_.ScalarLdFrag addr:$src3))),
                      _.RC:$src1)), 1, 0>, AVX512FMA3Base, EVEX_B,
         Sched<[sched.Folded, sched.ReadAfterFold]>;
  }
}

multiclass avx512_fma3_231_round<bits<8> opc, string OpcodeStr, SDNode OpNode,
                                 X86FoldableSchedWrite sched,
                                 X86VectorVTInfo _, string Suff> {
  let Constraints = "$src1 = $dst", ExeDomain = _.ExeDomain, hasSideEffects = 0 in
  defm rb: AVX512_maskable_3src<opc, MRMSrcReg, _, (outs _.RC:$dst),
          (ins _.RC:$src2, _.RC:$src3, AVX512RC:$rc),
          OpcodeStr, "$rc, $src3, $src2", "$src2, $src3, $rc",
          (_.VT ( OpNode _.RC:$src2, _.RC:$src3, _.RC:$src1, (i32 imm:$rc))),
          1, 1, vselect, 1>,
          AVX512FMA3Base, EVEX_B, EVEX_RC, Sched<[sched]>;
}

multiclass avx512_fma3p_231_common<bits<8> opc, string OpcodeStr, SDNode OpNode,
                                   SDNode OpNodeRnd, X86SchedWriteWidths sched,
                                   AVX512VLVectorVTInfo _, string Suff> {
  let Predicates = [HasAVX512] in {
    defm Z      : avx512_fma3p_231_rm<opc, OpcodeStr, OpNode, sched.ZMM,
                                      _.info512, Suff>,
                  avx512_fma3_231_round<opc, OpcodeStr, OpNodeRnd, sched.ZMM,
                                        _.info512, Suff>,
                              EVEX_V512, EVEX_CD8<_.info512.EltSize, CD8VF>;
  }
  let Predicates = [HasVLX, HasAVX512] in {
    defm Z256 : avx512_fma3p_231_rm<opc, OpcodeStr, OpNode, sched.YMM,
                                    _.info256, Suff>,
                      EVEX_V256, EVEX_CD8<_.info256.EltSize, CD8VF>;
    defm Z128 : avx512_fma3p_231_rm<opc, OpcodeStr, OpNode, sched.XMM,
                                    _.info128, Suff>,
                      EVEX_V128, EVEX_CD8<_.info128.EltSize, CD8VF>;
  }
}

multiclass avx512_fma3p_231_f<bits<8> opc, string OpcodeStr, SDNode OpNode,
                              SDNode OpNodeRnd > {
    defm PS : avx512_fma3p_231_common<opc, OpcodeStr#"ps", OpNode, OpNodeRnd,
                                      SchedWriteFMA, avx512vl_f32_info, "PS">;
    defm PD : avx512_fma3p_231_common<opc, OpcodeStr#"pd", OpNode, OpNodeRnd,
                                      SchedWriteFMA, avx512vl_f64_info, "PD">,
                                      VEX_W;
}

defm VFMADD231    : avx512_fma3p_231_f<0xB8, "vfmadd231", X86Fmadd, X86FmaddRnd>;
defm VFMSUB231    : avx512_fma3p_231_f<0xBA, "vfmsub231", X86Fmsub, X86FmsubRnd>;
defm VFMADDSUB231 : avx512_fma3p_231_f<0xB6, "vfmaddsub231", X86Fmaddsub, X86FmaddsubRnd>;
defm VFMSUBADD231 : avx512_fma3p_231_f<0xB7, "vfmsubadd231", X86Fmsubadd, X86FmsubaddRnd>;
defm VFNMADD231   : avx512_fma3p_231_f<0xBC, "vfnmadd231", X86Fnmadd, X86FnmaddRnd>;
defm VFNMSUB231   : avx512_fma3p_231_f<0xBE, "vfnmsub231", X86Fnmsub, X86FnmsubRnd>;

multiclass avx512_fma3p_132_rm<bits<8> opc, string OpcodeStr, SDNode OpNode,
                               X86FoldableSchedWrite sched,
                               X86VectorVTInfo _, string Suff> {
  let Constraints = "$src1 = $dst", ExeDomain = _.ExeDomain, hasSideEffects = 0 in {
  defm r: AVX512_maskable_3src<opc, MRMSrcReg, _, (outs _.RC:$dst),
          (ins _.RC:$src2, _.RC:$src3),
          OpcodeStr, "$src3, $src2", "$src2, $src3",
          (_.VT (OpNode _.RC:$src1, _.RC:$src3, _.RC:$src2)), 1, 1, vselect, 1>,
          AVX512FMA3Base, Sched<[sched]>;

  // Pattern is 312 order so that the load is in a different place from the
  // 213 and 231 patterns this helps tablegen's duplicate pattern detection.
  defm m: AVX512_maskable_3src<opc, MRMSrcMem, _, (outs _.RC:$dst),
          (ins _.RC:$src2, _.MemOp:$src3),
          OpcodeStr, "$src3, $src2", "$src2, $src3",
          (_.VT (OpNode (_.LdFrag addr:$src3), _.RC:$src1, _.RC:$src2)), 1, 0>,
          AVX512FMA3Base, Sched<[sched.Folded, sched.ReadAfterFold]>;

  // Pattern is 312 order so that the load is in a different place from the
  // 213 and 231 patterns this helps tablegen's duplicate pattern detection.
  defm mb: AVX512_maskable_3src<opc, MRMSrcMem, _, (outs _.RC:$dst),
         (ins _.RC:$src2, _.ScalarMemOp:$src3),
         OpcodeStr, "${src3}"##_.BroadcastStr##", $src2",
         "$src2, ${src3}"##_.BroadcastStr,
         (_.VT (OpNode (_.VT (X86VBroadcast(_.ScalarLdFrag addr:$src3))),
                       _.RC:$src1, _.RC:$src2)), 1, 0>,
         AVX512FMA3Base, EVEX_B, Sched<[sched.Folded, sched.ReadAfterFold]>;
  }
}

multiclass avx512_fma3_132_round<bits<8> opc, string OpcodeStr, SDNode OpNode,
                                 X86FoldableSchedWrite sched,
                                 X86VectorVTInfo _, string Suff> {
  let Constraints = "$src1 = $dst", ExeDomain = _.ExeDomain, hasSideEffects = 0 in
  defm rb: AVX512_maskable_3src<opc, MRMSrcReg, _, (outs _.RC:$dst),
          (ins _.RC:$src2, _.RC:$src3, AVX512RC:$rc),
          OpcodeStr, "$rc, $src3, $src2", "$src2, $src3, $rc",
          (_.VT ( OpNode _.RC:$src1, _.RC:$src3, _.RC:$src2, (i32 imm:$rc))),
          1, 1, vselect, 1>,
          AVX512FMA3Base, EVEX_B, EVEX_RC, Sched<[sched]>;
}

multiclass avx512_fma3p_132_common<bits<8> opc, string OpcodeStr, SDNode OpNode,
                                   SDNode OpNodeRnd, X86SchedWriteWidths sched,
                                   AVX512VLVectorVTInfo _, string Suff> {
  let Predicates = [HasAVX512] in {
    defm Z      : avx512_fma3p_132_rm<opc, OpcodeStr, OpNode, sched.ZMM,
                                      _.info512, Suff>,
                  avx512_fma3_132_round<opc, OpcodeStr, OpNodeRnd, sched.ZMM,
                                        _.info512, Suff>,
                              EVEX_V512, EVEX_CD8<_.info512.EltSize, CD8VF>;
  }
  let Predicates = [HasVLX, HasAVX512] in {
    defm Z256 : avx512_fma3p_132_rm<opc, OpcodeStr, OpNode, sched.YMM,
                                    _.info256, Suff>,
                      EVEX_V256, EVEX_CD8<_.info256.EltSize, CD8VF>;
    defm Z128 : avx512_fma3p_132_rm<opc, OpcodeStr, OpNode, sched.XMM,
                                    _.info128, Suff>,
                      EVEX_V128, EVEX_CD8<_.info128.EltSize, CD8VF>;
  }
}

multiclass avx512_fma3p_132_f<bits<8> opc, string OpcodeStr, SDNode OpNode,
                              SDNode OpNodeRnd > {
    defm PS : avx512_fma3p_132_common<opc, OpcodeStr#"ps", OpNode, OpNodeRnd,
                                      SchedWriteFMA, avx512vl_f32_info, "PS">;
    defm PD : avx512_fma3p_132_common<opc, OpcodeStr#"pd", OpNode, OpNodeRnd,
                                      SchedWriteFMA, avx512vl_f64_info, "PD">,
                                      VEX_W;
}

defm VFMADD132    : avx512_fma3p_132_f<0x98, "vfmadd132", X86Fmadd, X86FmaddRnd>;
defm VFMSUB132    : avx512_fma3p_132_f<0x9A, "vfmsub132", X86Fmsub, X86FmsubRnd>;
defm VFMADDSUB132 : avx512_fma3p_132_f<0x96, "vfmaddsub132", X86Fmaddsub, X86FmaddsubRnd>;
defm VFMSUBADD132 : avx512_fma3p_132_f<0x97, "vfmsubadd132", X86Fmsubadd, X86FmsubaddRnd>;
defm VFNMADD132   : avx512_fma3p_132_f<0x9C, "vfnmadd132", X86Fnmadd, X86FnmaddRnd>;
defm VFNMSUB132   : avx512_fma3p_132_f<0x9E, "vfnmsub132", X86Fnmsub, X86FnmsubRnd>;

// Scalar FMA
multiclass avx512_fma3s_common<bits<8> opc, string OpcodeStr, X86VectorVTInfo _,
                               dag RHS_r, dag RHS_m, dag RHS_b, bit MaskOnlyReg> {
let Constraints = "$src1 = $dst", hasSideEffects = 0 in {
  defm r_Int: AVX512_maskable_3src_scalar<opc, MRMSrcReg, _, (outs _.RC:$dst),
          (ins _.RC:$src2, _.RC:$src3), OpcodeStr,
          "$src3, $src2", "$src2, $src3", (null_frag), 1, 1>,
          AVX512FMA3Base, Sched<[SchedWriteFMA.Scl]>;

  let mayLoad = 1 in
  defm m_Int: AVX512_maskable_3src_scalar<opc, MRMSrcMem, _, (outs _.RC:$dst),
          (ins _.RC:$src2, _.IntScalarMemOp:$src3), OpcodeStr,
          "$src3, $src2", "$src2, $src3", (null_frag), 1, 1>,
          AVX512FMA3Base, Sched<[SchedWriteFMA.Scl.Folded, SchedWriteFMA.Scl.ReadAfterFold]>;

  defm rb_Int: AVX512_maskable_3src_scalar<opc, MRMSrcReg, _, (outs _.RC:$dst),
         (ins _.RC:$src2, _.RC:$src3, AVX512RC:$rc),
         OpcodeStr, "$rc, $src3, $src2", "$src2, $src3, $rc", (null_frag), 1, 1>,
         AVX512FMA3Base, EVEX_B, EVEX_RC, Sched<[SchedWriteFMA.Scl]>;

  let isCodeGenOnly = 1, isCommutable = 1 in {
    def r     : AVX512FMA3S<opc, MRMSrcReg, (outs _.FRC:$dst),
                     (ins _.FRC:$src1, _.FRC:$src2, _.FRC:$src3),
                     !strconcat(OpcodeStr,
                              "\t{$src3, $src2, $dst|$dst, $src2, $src3}"),
                     !if(MaskOnlyReg, [], [RHS_r])>, Sched<[SchedWriteFMA.Scl]>;
    def m     : AVX512FMA3S<opc, MRMSrcMem, (outs _.FRC:$dst),
                    (ins _.FRC:$src1, _.FRC:$src2, _.ScalarMemOp:$src3),
                    !strconcat(OpcodeStr,
                               "\t{$src3, $src2, $dst|$dst, $src2, $src3}"),
                    [RHS_m]>, Sched<[SchedWriteFMA.Scl.Folded, SchedWriteFMA.Scl.ReadAfterFold]>;

    def rb    : AVX512FMA3S<opc, MRMSrcReg, (outs _.FRC:$dst),
                     (ins _.FRC:$src1, _.FRC:$src2, _.FRC:$src3, AVX512RC:$rc),
                     !strconcat(OpcodeStr,
                              "\t{$src3, $src2, $dst|$dst, $src2, $src3}"),
                     !if(MaskOnlyReg, [], [RHS_b])>, EVEX_B, EVEX_RC,
                     Sched<[SchedWriteFMA.Scl]>;
  }// isCodeGenOnly = 1
}// Constraints = "$src1 = $dst"
}

multiclass avx512_fma3s_all<bits<8> opc213, bits<8> opc231, bits<8> opc132,
                            string OpcodeStr, SDNode OpNode, SDNode OpNodeRnd,
                            X86VectorVTInfo _, string SUFF> {
  let ExeDomain = _.ExeDomain in {
  defm NAME#213#SUFF#Z: avx512_fma3s_common<opc213, OpcodeStr#"213"#_.Suffix, _,
                // Operands for intrinsic are in 123 order to preserve passthu
                // semantics.
                (set _.FRC:$dst, (_.EltVT (OpNode _.FRC:$src2, _.FRC:$src1,
                         _.FRC:$src3))),
                (set _.FRC:$dst, (_.EltVT (OpNode _.FRC:$src2, _.FRC:$src1,
                         (_.ScalarLdFrag addr:$src3)))),
                (set _.FRC:$dst, (_.EltVT (OpNodeRnd _.FRC:$src2, _.FRC:$src1,
                         _.FRC:$src3, (i32 imm:$rc)))), 0>;

  defm NAME#231#SUFF#Z: avx512_fma3s_common<opc231, OpcodeStr#"231"#_.Suffix, _,
                (set _.FRC:$dst, (_.EltVT (OpNode _.FRC:$src2, _.FRC:$src3,
                                          _.FRC:$src1))),
                (set _.FRC:$dst, (_.EltVT (OpNode _.FRC:$src2,
                            (_.ScalarLdFrag addr:$src3), _.FRC:$src1))),
                (set _.FRC:$dst, (_.EltVT (OpNodeRnd _.FRC:$src2, _.FRC:$src3,
                         _.FRC:$src1, (i32 imm:$rc)))), 1>;

  // One pattern is 312 order so that the load is in a different place from the
  // 213 and 231 patterns this helps tablegen's duplicate pattern detection.
  defm NAME#132#SUFF#Z: avx512_fma3s_common<opc132, OpcodeStr#"132"#_.Suffix, _,
                (set _.FRC:$dst, (_.EltVT (OpNode _.FRC:$src1, _.FRC:$src3,
                         _.FRC:$src2))),
                (set _.FRC:$dst, (_.EltVT (OpNode (_.ScalarLdFrag addr:$src3),
                                 _.FRC:$src1, _.FRC:$src2))),
                (set _.FRC:$dst, (_.EltVT (OpNodeRnd _.FRC:$src1, _.FRC:$src3,
                         _.FRC:$src2, (i32 imm:$rc)))), 1>;
  }
}

multiclass avx512_fma3s<bits<8> opc213, bits<8> opc231, bits<8> opc132,
                        string OpcodeStr, SDNode OpNode, SDNode OpNodeRnd> {
  let Predicates = [HasAVX512] in {
    defm NAME : avx512_fma3s_all<opc213, opc231, opc132, OpcodeStr, OpNode,
                                 OpNodeRnd, f32x_info, "SS">,
                                 EVEX_CD8<32, CD8VT1>, VEX_LIG;
    defm NAME : avx512_fma3s_all<opc213, opc231, opc132, OpcodeStr, OpNode,
                                 OpNodeRnd, f64x_info, "SD">,
                                 EVEX_CD8<64, CD8VT1>, VEX_LIG, VEX_W;
  }
}

defm VFMADD  : avx512_fma3s<0xA9, 0xB9, 0x99, "vfmadd", X86Fmadd, X86FmaddRnd>;
defm VFMSUB  : avx512_fma3s<0xAB, 0xBB, 0x9B, "vfmsub", X86Fmsub, X86FmsubRnd>;
defm VFNMADD : avx512_fma3s<0xAD, 0xBD, 0x9D, "vfnmadd", X86Fnmadd, X86FnmaddRnd>;
defm VFNMSUB : avx512_fma3s<0xAF, 0xBF, 0x9F, "vfnmsub", X86Fnmsub, X86FnmsubRnd>;

multiclass avx512_scalar_fma_patterns<SDNode Op, SDNode RndOp, string Prefix,
                                      string Suffix, SDNode Move,
                                      X86VectorVTInfo _, PatLeaf ZeroFP> {
  let Predicates = [HasAVX512] in {
    def : Pat<(_.VT (Move (_.VT VR128X:$src1), (_.VT (scalar_to_vector
                (Op _.FRC:$src2,
                    (_.EltVT (extractelt (_.VT VR128X:$src1), (iPTR 0))),
                    _.FRC:$src3))))),
              (!cast<I>(Prefix#"213"#Suffix#"Zr_Int")
               VR128X:$src1, (_.VT (COPY_TO_REGCLASS _.FRC:$src2, VR128X)),
               (_.VT (COPY_TO_REGCLASS _.FRC:$src3, VR128X)))>;

    def : Pat<(_.VT (Move (_.VT VR128X:$src1), (_.VT (scalar_to_vector
                (Op _.FRC:$src2, _.FRC:$src3,
                    (_.EltVT (extractelt (_.VT VR128X:$src1), (iPTR 0)))))))),
              (!cast<I>(Prefix#"231"#Suffix#"Zr_Int")
               VR128X:$src1, (_.VT (COPY_TO_REGCLASS _.FRC:$src2, VR128X)),
               (_.VT (COPY_TO_REGCLASS _.FRC:$src3, VR128X)))>;

    def : Pat<(_.VT (Move (_.VT VR128X:$src1), (_.VT (scalar_to_vector
                (Op _.FRC:$src2,
                    (_.EltVT (extractelt (_.VT VR128X:$src1), (iPTR 0))),
                    (_.ScalarLdFrag addr:$src3)))))),
              (!cast<I>(Prefix#"213"#Suffix#"Zm_Int")
               VR128X:$src1, (_.VT (COPY_TO_REGCLASS _.FRC:$src2, VR128X)),
               addr:$src3)>;

    def : Pat<(_.VT (Move (_.VT VR128X:$src1), (_.VT (scalar_to_vector
                (Op (_.EltVT (extractelt (_.VT VR128X:$src1), (iPTR 0))),
                    (_.ScalarLdFrag addr:$src3), _.FRC:$src2))))),
              (!cast<I>(Prefix#"132"#Suffix#"Zm_Int")
               VR128X:$src1, (_.VT (COPY_TO_REGCLASS _.FRC:$src2, VR128X)),
               addr:$src3)>;

    def : Pat<(_.VT (Move (_.VT VR128X:$src1), (_.VT (scalar_to_vector
                (Op _.FRC:$src2, (_.ScalarLdFrag addr:$src3),
                    (_.EltVT (extractelt (_.VT VR128X:$src1), (iPTR 0)))))))),
              (!cast<I>(Prefix#"231"#Suffix#"Zm_Int")
               VR128X:$src1, (_.VT (COPY_TO_REGCLASS _.FRC:$src2, VR128X)),
               addr:$src3)>;

    def : Pat<(_.VT (Move (_.VT VR128X:$src1), (_.VT (scalar_to_vector
               (X86selects VK1WM:$mask,
                (Op _.FRC:$src2,
                    (_.EltVT (extractelt (_.VT VR128X:$src1), (iPTR 0))),
                    _.FRC:$src3),
                (_.EltVT (extractelt (_.VT VR128X:$src1), (iPTR 0)))))))),
              (!cast<I>(Prefix#"213"#Suffix#"Zr_Intk")
               VR128X:$src1, VK1WM:$mask,
               (_.VT (COPY_TO_REGCLASS _.FRC:$src2, VR128X)),
               (_.VT (COPY_TO_REGCLASS _.FRC:$src3, VR128X)))>;

    def : Pat<(_.VT (Move (_.VT VR128X:$src1), (_.VT (scalar_to_vector
               (X86selects VK1WM:$mask,
                (Op _.FRC:$src2,
                    (_.EltVT (extractelt (_.VT VR128X:$src1), (iPTR 0))),
                    (_.ScalarLdFrag addr:$src3)),
                (_.EltVT (extractelt (_.VT VR128X:$src1), (iPTR 0)))))))),
              (!cast<I>(Prefix#"213"#Suffix#"Zm_Intk")
               VR128X:$src1, VK1WM:$mask,
               (_.VT (COPY_TO_REGCLASS _.FRC:$src2, VR128X)), addr:$src3)>;

    def : Pat<(_.VT (Move (_.VT VR128X:$src1), (_.VT (scalar_to_vector
               (X86selects VK1WM:$mask,
                (Op (_.EltVT (extractelt (_.VT VR128X:$src1), (iPTR 0))),
                    (_.ScalarLdFrag addr:$src3), _.FRC:$src2),
                (_.EltVT (extractelt (_.VT VR128X:$src1), (iPTR 0)))))))),
              (!cast<I>(Prefix#"132"#Suffix#"Zm_Intk")
               VR128X:$src1, VK1WM:$mask,
               (_.VT (COPY_TO_REGCLASS _.FRC:$src2, VR128X)), addr:$src3)>;

    def : Pat<(_.VT (Move (_.VT VR128X:$src1), (_.VT (scalar_to_vector
               (X86selects VK1WM:$mask,
                (Op _.FRC:$src2, _.FRC:$src3,
                    (_.EltVT (extractelt (_.VT VR128X:$src1), (iPTR 0)))),
                (_.EltVT (extractelt (_.VT VR128X:$src1), (iPTR 0)))))))),
              (!cast<I>(Prefix#"231"#Suffix#"Zr_Intk")
               VR128X:$src1, VK1WM:$mask,
               (_.VT (COPY_TO_REGCLASS _.FRC:$src2, VR128X)),
               (_.VT (COPY_TO_REGCLASS _.FRC:$src3, VR128X)))>;

    def : Pat<(_.VT (Move (_.VT VR128X:$src1), (_.VT (scalar_to_vector
               (X86selects VK1WM:$mask,
                (Op _.FRC:$src2, (_.ScalarLdFrag addr:$src3),
                    (_.EltVT (extractelt (_.VT VR128X:$src1), (iPTR 0)))),
                (_.EltVT (extractelt (_.VT VR128X:$src1), (iPTR 0)))))))),
              (!cast<I>(Prefix#"231"#Suffix#"Zm_Intk")
               VR128X:$src1, VK1WM:$mask,
               (_.VT (COPY_TO_REGCLASS _.FRC:$src2, VR128X)), addr:$src3)>;

    def : Pat<(_.VT (Move (_.VT VR128X:$src1), (_.VT (scalar_to_vector
               (X86selects VK1WM:$mask,
                (Op _.FRC:$src2,
                    (_.EltVT (extractelt (_.VT VR128X:$src1), (iPTR 0))),
                    _.FRC:$src3),
                (_.EltVT ZeroFP)))))),
              (!cast<I>(Prefix#"213"#Suffix#"Zr_Intkz")
               VR128X:$src1, VK1WM:$mask,
               (_.VT (COPY_TO_REGCLASS _.FRC:$src2, VR128X)),
               (_.VT (COPY_TO_REGCLASS _.FRC:$src3, VR128X)))>;

    def : Pat<(_.VT (Move (_.VT VR128X:$src1), (_.VT (scalar_to_vector
               (X86selects VK1WM:$mask,
                (Op _.FRC:$src2, _.FRC:$src3,
                    (_.EltVT (extractelt (_.VT VR128X:$src1), (iPTR 0)))),
                (_.EltVT ZeroFP)))))),
              (!cast<I>(Prefix#"231"#Suffix#"Zr_Intkz")
               VR128X:$src1, VK1WM:$mask,
               (_.VT (COPY_TO_REGCLASS _.FRC:$src2, VR128X)),
               (_.VT (COPY_TO_REGCLASS _.FRC:$src3, VR128X)))>;

    def : Pat<(_.VT (Move (_.VT VR128X:$src1), (_.VT (scalar_to_vector
               (X86selects VK1WM:$mask,
                (Op _.FRC:$src2,
                    (_.EltVT (extractelt (_.VT VR128X:$src1), (iPTR 0))),
                    (_.ScalarLdFrag addr:$src3)),
                (_.EltVT ZeroFP)))))),
              (!cast<I>(Prefix#"213"#Suffix#"Zm_Intkz")
               VR128X:$src1, VK1WM:$mask,
               (_.VT (COPY_TO_REGCLASS _.FRC:$src2, VR128X)), addr:$src3)>;

    def : Pat<(_.VT (Move (_.VT VR128X:$src1), (_.VT (scalar_to_vector
               (X86selects VK1WM:$mask,
                (Op (_.EltVT (extractelt (_.VT VR128X:$src1), (iPTR 0))),
                    _.FRC:$src2, (_.ScalarLdFrag addr:$src3)),
                (_.EltVT ZeroFP)))))),
              (!cast<I>(Prefix#"132"#Suffix#"Zm_Intkz")
               VR128X:$src1, VK1WM:$mask,
               (_.VT (COPY_TO_REGCLASS _.FRC:$src2, VR128X)), addr:$src3)>;

    def : Pat<(_.VT (Move (_.VT VR128X:$src1), (_.VT (scalar_to_vector
               (X86selects VK1WM:$mask,
                (Op _.FRC:$src2, (_.ScalarLdFrag addr:$src3),
                    (_.EltVT (extractelt (_.VT VR128X:$src1), (iPTR 0)))),
                (_.EltVT ZeroFP)))))),
              (!cast<I>(Prefix#"231"#Suffix#"Zm_Intkz")
               VR128X:$src1, VK1WM:$mask,
               (_.VT (COPY_TO_REGCLASS _.FRC:$src2, VR128X)), addr:$src3)>;

    // Patterns with rounding mode.
    def : Pat<(_.VT (Move (_.VT VR128X:$src1), (_.VT (scalar_to_vector
                (RndOp _.FRC:$src2,
                       (_.EltVT (extractelt (_.VT VR128X:$src1), (iPTR 0))),
                       _.FRC:$src3, (i32 imm:$rc)))))),
              (!cast<I>(Prefix#"213"#Suffix#"Zrb_Int")
               VR128X:$src1, (_.VT (COPY_TO_REGCLASS _.FRC:$src2, VR128X)),
               (_.VT (COPY_TO_REGCLASS _.FRC:$src3, VR128X)), imm:$rc)>;

    def : Pat<(_.VT (Move (_.VT VR128X:$src1), (_.VT (scalar_to_vector
                (RndOp _.FRC:$src2, _.FRC:$src3,
                       (_.EltVT (extractelt (_.VT VR128X:$src1), (iPTR 0))),
                       (i32 imm:$rc)))))),
              (!cast<I>(Prefix#"231"#Suffix#"Zrb_Int")
               VR128X:$src1, (_.VT (COPY_TO_REGCLASS _.FRC:$src2, VR128X)),
               (_.VT (COPY_TO_REGCLASS _.FRC:$src3, VR128X)), imm:$rc)>;

    def : Pat<(_.VT (Move (_.VT VR128X:$src1), (_.VT (scalar_to_vector
               (X86selects VK1WM:$mask,
                (RndOp _.FRC:$src2,
                       (_.EltVT (extractelt (_.VT VR128X:$src1), (iPTR 0))),
                       _.FRC:$src3, (i32 imm:$rc)),
                (_.EltVT (extractelt (_.VT VR128X:$src1), (iPTR 0)))))))),
              (!cast<I>(Prefix#"213"#Suffix#"Zrb_Intk")
               VR128X:$src1, VK1WM:$mask,
               (_.VT (COPY_TO_REGCLASS _.FRC:$src2, VR128X)),
               (_.VT (COPY_TO_REGCLASS _.FRC:$src3, VR128X)), imm:$rc)>;

    def : Pat<(_.VT (Move (_.VT VR128X:$src1), (_.VT (scalar_to_vector
               (X86selects VK1WM:$mask,
                (RndOp _.FRC:$src2, _.FRC:$src3,
                       (_.EltVT (extractelt (_.VT VR128X:$src1), (iPTR 0))),
                       (i32 imm:$rc)),
                (_.EltVT (extractelt (_.VT VR128X:$src1), (iPTR 0)))))))),
              (!cast<I>(Prefix#"231"#Suffix#"Zrb_Intk")
               VR128X:$src1, VK1WM:$mask,
               (_.VT (COPY_TO_REGCLASS _.FRC:$src2, VR128X)),
               (_.VT (COPY_TO_REGCLASS _.FRC:$src3, VR128X)), imm:$rc)>;

    def : Pat<(_.VT (Move (_.VT VR128X:$src1), (_.VT (scalar_to_vector
               (X86selects VK1WM:$mask,
                (RndOp _.FRC:$src2,
                       (_.EltVT (extractelt (_.VT VR128X:$src1), (iPTR 0))),
                       _.FRC:$src3, (i32 imm:$rc)),
                (_.EltVT ZeroFP)))))),
              (!cast<I>(Prefix#"213"#Suffix#"Zrb_Intkz")
               VR128X:$src1, VK1WM:$mask,
               (_.VT (COPY_TO_REGCLASS _.FRC:$src2, VR128X)),
               (_.VT (COPY_TO_REGCLASS _.FRC:$src3, VR128X)), imm:$rc)>;

    def : Pat<(_.VT (Move (_.VT VR128X:$src1), (_.VT (scalar_to_vector
               (X86selects VK1WM:$mask,
                (RndOp _.FRC:$src2, _.FRC:$src3,
                       (_.EltVT (extractelt (_.VT VR128X:$src1), (iPTR 0))),
                       (i32 imm:$rc)),
                (_.EltVT ZeroFP)))))),
              (!cast<I>(Prefix#"231"#Suffix#"Zrb_Intkz")
               VR128X:$src1, VK1WM:$mask,
               (_.VT (COPY_TO_REGCLASS _.FRC:$src2, VR128X)),
               (_.VT (COPY_TO_REGCLASS _.FRC:$src3, VR128X)), imm:$rc)>;
  }
}

defm : avx512_scalar_fma_patterns<X86Fmadd, X86FmaddRnd, "VFMADD", "SS",
                                  X86Movss, v4f32x_info, fp32imm0>;
defm : avx512_scalar_fma_patterns<X86Fmsub, X86FmsubRnd, "VFMSUB", "SS",
                                  X86Movss, v4f32x_info, fp32imm0>;
defm : avx512_scalar_fma_patterns<X86Fnmadd, X86FnmaddRnd, "VFNMADD", "SS",
                                  X86Movss, v4f32x_info, fp32imm0>;
defm : avx512_scalar_fma_patterns<X86Fnmsub, X86FnmsubRnd, "VFNMSUB", "SS",
                                  X86Movss, v4f32x_info, fp32imm0>;

defm : avx512_scalar_fma_patterns<X86Fmadd, X86FmaddRnd, "VFMADD", "SD",
                                  X86Movsd, v2f64x_info, fp64imm0>;
defm : avx512_scalar_fma_patterns<X86Fmsub, X86FmsubRnd, "VFMSUB", "SD",
                                  X86Movsd, v2f64x_info, fp64imm0>;
defm : avx512_scalar_fma_patterns<X86Fnmadd, X86FnmaddRnd, "VFNMADD", "SD",
                                  X86Movsd, v2f64x_info, fp64imm0>;
defm : avx512_scalar_fma_patterns<X86Fnmsub, X86FnmsubRnd, "VFNMSUB", "SD",
                                  X86Movsd, v2f64x_info, fp64imm0>;

//===----------------------------------------------------------------------===//
// AVX-512  Packed Multiply of Unsigned 52-bit Integers and Add the Low 52-bit IFMA
//===----------------------------------------------------------------------===//
let Constraints = "$src1 = $dst" in {
multiclass avx512_pmadd52_rm<bits<8> opc, string OpcodeStr, SDNode OpNode,
                             X86FoldableSchedWrite sched, X86VectorVTInfo _> {
  // NOTE: The SDNode have the multiply operands first with the add last.
  // This enables commuted load patterns to be autogenerated by tablegen.
  let ExeDomain = _.ExeDomain in {
  defm r: AVX512_maskable_3src<opc, MRMSrcReg, _, (outs _.RC:$dst),
          (ins _.RC:$src2, _.RC:$src3),
          OpcodeStr, "$src3, $src2", "$src2, $src3",
          (_.VT (OpNode _.RC:$src2, _.RC:$src3, _.RC:$src1)), 1, 1>,
         AVX512FMA3Base, Sched<[sched]>;

  defm m: AVX512_maskable_3src<opc, MRMSrcMem, _, (outs _.RC:$dst),
          (ins _.RC:$src2, _.MemOp:$src3),
          OpcodeStr, "$src3, $src2", "$src2, $src3",
          (_.VT (OpNode _.RC:$src2, (_.LdFrag addr:$src3), _.RC:$src1))>,
          AVX512FMA3Base, Sched<[sched.Folded, sched.ReadAfterFold]>;

  defm mb: AVX512_maskable_3src<opc, MRMSrcMem, _, (outs _.RC:$dst),
            (ins _.RC:$src2, _.ScalarMemOp:$src3),
            OpcodeStr,   !strconcat("${src3}", _.BroadcastStr,", $src2"),
            !strconcat("$src2, ${src3}", _.BroadcastStr ),
            (OpNode _.RC:$src2,
                    (_.VT (X86VBroadcast (_.ScalarLdFrag addr:$src3))),
                    _.RC:$src1)>,
            AVX512FMA3Base, EVEX_B, Sched<[sched.Folded, sched.ReadAfterFold]>;
  }
}
} // Constraints = "$src1 = $dst"

multiclass avx512_pmadd52_common<bits<8> opc, string OpcodeStr, SDNode OpNode,
                                 X86SchedWriteWidths sched, AVX512VLVectorVTInfo _> {
  let Predicates = [HasIFMA] in {
    defm Z      : avx512_pmadd52_rm<opc, OpcodeStr, OpNode, sched.ZMM, _.info512>,
                      EVEX_V512, EVEX_CD8<_.info512.EltSize, CD8VF>;
  }
  let Predicates = [HasVLX, HasIFMA] in {
    defm Z256 : avx512_pmadd52_rm<opc, OpcodeStr, OpNode, sched.YMM, _.info256>,
                      EVEX_V256, EVEX_CD8<_.info256.EltSize, CD8VF>;
    defm Z128 : avx512_pmadd52_rm<opc, OpcodeStr, OpNode, sched.XMM, _.info128>,
                      EVEX_V128, EVEX_CD8<_.info128.EltSize, CD8VF>;
  }
}

defm VPMADD52LUQ : avx512_pmadd52_common<0xb4, "vpmadd52luq", x86vpmadd52l,
                                         SchedWriteVecIMul, avx512vl_i64_info>,
                                         VEX_W;
defm VPMADD52HUQ : avx512_pmadd52_common<0xb5, "vpmadd52huq", x86vpmadd52h,
                                         SchedWriteVecIMul, avx512vl_i64_info>,
                                         VEX_W;

//===----------------------------------------------------------------------===//
// AVX-512  Scalar convert from sign integer to float/double
//===----------------------------------------------------------------------===//

multiclass avx512_vcvtsi<bits<8> opc, SDNode OpNode, X86FoldableSchedWrite sched,
                    RegisterClass SrcRC, X86VectorVTInfo DstVT,
                    X86MemOperand x86memop, PatFrag ld_frag, string asm> {
  let hasSideEffects = 0 in {
    def rr : SI<opc, MRMSrcReg, (outs DstVT.FRC:$dst),
              (ins DstVT.FRC:$src1, SrcRC:$src),
              !strconcat(asm,"\t{$src, $src1, $dst|$dst, $src1, $src}"), []>,
              EVEX_4V, Sched<[sched]>;
    let mayLoad = 1 in
      def rm : SI<opc, MRMSrcMem, (outs DstVT.FRC:$dst),
              (ins DstVT.FRC:$src1, x86memop:$src),
              !strconcat(asm,"\t{$src, $src1, $dst|$dst, $src1, $src}"), []>,
              EVEX_4V, Sched<[sched.Folded, sched.ReadAfterFold]>;
  } // hasSideEffects = 0
  let isCodeGenOnly = 1 in {
    def rr_Int : SI<opc, MRMSrcReg, (outs DstVT.RC:$dst),
                  (ins DstVT.RC:$src1, SrcRC:$src2),
                  !strconcat(asm,"\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
                  [(set DstVT.RC:$dst,
                        (OpNode (DstVT.VT DstVT.RC:$src1),
                                 SrcRC:$src2,
                                 (i32 FROUND_CURRENT)))]>,
                 EVEX_4V, Sched<[sched]>;

    def rm_Int : SI<opc, MRMSrcMem, (outs DstVT.RC:$dst),
                  (ins DstVT.RC:$src1, x86memop:$src2),
                  !strconcat(asm,"\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
                  [(set DstVT.RC:$dst,
                        (OpNode (DstVT.VT DstVT.RC:$src1),
                                 (ld_frag addr:$src2),
                                 (i32 FROUND_CURRENT)))]>,
                  EVEX_4V, Sched<[sched.Folded, sched.ReadAfterFold]>;
  }//isCodeGenOnly = 1
}

multiclass avx512_vcvtsi_round<bits<8> opc, SDNode OpNode,
                               X86FoldableSchedWrite sched, RegisterClass SrcRC,
                               X86VectorVTInfo DstVT, string asm> {
  def rrb_Int : SI<opc, MRMSrcReg, (outs DstVT.RC:$dst),
              (ins DstVT.RC:$src1, SrcRC:$src2, AVX512RC:$rc),
              !strconcat(asm,
                  "\t{$src2, $rc, $src1, $dst|$dst, $src1, $rc, $src2}"),
              [(set DstVT.RC:$dst,
                    (OpNode (DstVT.VT DstVT.RC:$src1),
                             SrcRC:$src2,
                             (i32 imm:$rc)))]>,
              EVEX_4V, EVEX_B, EVEX_RC, Sched<[sched]>;
}

multiclass avx512_vcvtsi_common<bits<8> opc, SDNode OpNode,
                                X86FoldableSchedWrite sched,
                                RegisterClass SrcRC, X86VectorVTInfo DstVT,
                                X86MemOperand x86memop, PatFrag ld_frag, string asm> {
  defm NAME : avx512_vcvtsi_round<opc, OpNode, sched, SrcRC, DstVT, asm>,
              avx512_vcvtsi<opc, OpNode, sched, SrcRC, DstVT, x86memop,
                            ld_frag, asm>, VEX_LIG;
}

let Predicates = [HasAVX512] in {
defm VCVTSI2SSZ  : avx512_vcvtsi_common<0x2A, X86SintToFpRnd, WriteCvtI2SS, GR32,
                                 v4f32x_info, i32mem, loadi32, "cvtsi2ss{l}">,
                                 XS, EVEX_CD8<32, CD8VT1>;
defm VCVTSI642SSZ: avx512_vcvtsi_common<0x2A, X86SintToFpRnd, WriteCvtI2SS, GR64,
                                 v4f32x_info, i64mem, loadi64, "cvtsi2ss{q}">,
                                 XS, VEX_W, EVEX_CD8<64, CD8VT1>;
defm VCVTSI2SDZ  : avx512_vcvtsi_common<0x2A, X86SintToFpRnd, WriteCvtI2SD, GR32,
                                 v2f64x_info, i32mem, loadi32, "cvtsi2sd{l}">,
                                 XD, EVEX_CD8<32, CD8VT1>;
defm VCVTSI642SDZ: avx512_vcvtsi_common<0x2A, X86SintToFpRnd, WriteCvtI2SD, GR64,
                                 v2f64x_info, i64mem, loadi64, "cvtsi2sd{q}">,
                                 XD, VEX_W, EVEX_CD8<64, CD8VT1>;

def : InstAlias<"vcvtsi2ss\t{$src, $src1, $dst|$dst, $src1, $src}",
              (VCVTSI2SSZrm FR64X:$dst, FR64X:$src1, i32mem:$src), 0, "att">;
def : InstAlias<"vcvtsi2sd\t{$src, $src1, $dst|$dst, $src1, $src}",
              (VCVTSI2SDZrm FR64X:$dst, FR64X:$src1, i32mem:$src), 0, "att">;

def : Pat<(f32 (sint_to_fp (loadi32 addr:$src))),
          (VCVTSI2SSZrm (f32 (IMPLICIT_DEF)), addr:$src)>;
def : Pat<(f32 (sint_to_fp (loadi64 addr:$src))),
          (VCVTSI642SSZrm (f32 (IMPLICIT_DEF)), addr:$src)>;
def : Pat<(f64 (sint_to_fp (loadi32 addr:$src))),
          (VCVTSI2SDZrm (f64 (IMPLICIT_DEF)), addr:$src)>;
def : Pat<(f64 (sint_to_fp (loadi64 addr:$src))),
          (VCVTSI642SDZrm (f64 (IMPLICIT_DEF)), addr:$src)>;

def : Pat<(f32 (sint_to_fp GR32:$src)),
          (VCVTSI2SSZrr (f32 (IMPLICIT_DEF)), GR32:$src)>;
def : Pat<(f32 (sint_to_fp GR64:$src)),
          (VCVTSI642SSZrr (f32 (IMPLICIT_DEF)), GR64:$src)>;
def : Pat<(f64 (sint_to_fp GR32:$src)),
          (VCVTSI2SDZrr (f64 (IMPLICIT_DEF)), GR32:$src)>;
def : Pat<(f64 (sint_to_fp GR64:$src)),
          (VCVTSI642SDZrr (f64 (IMPLICIT_DEF)), GR64:$src)>;

defm VCVTUSI2SSZ   : avx512_vcvtsi_common<0x7B, X86UintToFpRnd, WriteCvtI2SS, GR32,
                                  v4f32x_info, i32mem, loadi32,
                                  "cvtusi2ss{l}">, XS, EVEX_CD8<32, CD8VT1>;
defm VCVTUSI642SSZ : avx512_vcvtsi_common<0x7B, X86UintToFpRnd, WriteCvtI2SS, GR64,
                                  v4f32x_info, i64mem, loadi64, "cvtusi2ss{q}">,
                                  XS, VEX_W, EVEX_CD8<64, CD8VT1>;
defm VCVTUSI2SDZ   : avx512_vcvtsi<0x7B, X86UintToFpRnd, WriteCvtI2SD, GR32, v2f64x_info,
                                  i32mem, loadi32, "cvtusi2sd{l}">,
                                  XD, VEX_LIG, EVEX_CD8<32, CD8VT1>;
defm VCVTUSI642SDZ : avx512_vcvtsi_common<0x7B, X86UintToFpRnd, WriteCvtI2SD, GR64,
                                  v2f64x_info, i64mem, loadi64, "cvtusi2sd{q}">,
                                  XD, VEX_W, EVEX_CD8<64, CD8VT1>;

def : InstAlias<"vcvtusi2ss\t{$src, $src1, $dst|$dst, $src1, $src}",
              (VCVTUSI2SSZrm FR64X:$dst, FR64X:$src1, i32mem:$src), 0, "att">;
def : InstAlias<"vcvtusi2sd\t{$src, $src1, $dst|$dst, $src1, $src}",
              (VCVTUSI2SDZrm FR64X:$dst, FR64X:$src1, i32mem:$src), 0, "att">;

def : Pat<(f32 (uint_to_fp (loadi32 addr:$src))),
          (VCVTUSI2SSZrm (f32 (IMPLICIT_DEF)), addr:$src)>;
def : Pat<(f32 (uint_to_fp (loadi64 addr:$src))),
          (VCVTUSI642SSZrm (f32 (IMPLICIT_DEF)), addr:$src)>;
def : Pat<(f64 (uint_to_fp (loadi32 addr:$src))),
          (VCVTUSI2SDZrm (f64 (IMPLICIT_DEF)), addr:$src)>;
def : Pat<(f64 (uint_to_fp (loadi64 addr:$src))),
          (VCVTUSI642SDZrm (f64 (IMPLICIT_DEF)), addr:$src)>;

def : Pat<(f32 (uint_to_fp GR32:$src)),
          (VCVTUSI2SSZrr (f32 (IMPLICIT_DEF)), GR32:$src)>;
def : Pat<(f32 (uint_to_fp GR64:$src)),
          (VCVTUSI642SSZrr (f32 (IMPLICIT_DEF)), GR64:$src)>;
def : Pat<(f64 (uint_to_fp GR32:$src)),
          (VCVTUSI2SDZrr (f64 (IMPLICIT_DEF)), GR32:$src)>;
def : Pat<(f64 (uint_to_fp GR64:$src)),
          (VCVTUSI642SDZrr (f64 (IMPLICIT_DEF)), GR64:$src)>;
}

//===----------------------------------------------------------------------===//
// AVX-512  Scalar convert from float/double to integer
//===----------------------------------------------------------------------===//

multiclass avx512_cvt_s_int_round<bits<8> opc, X86VectorVTInfo SrcVT,
                                  X86VectorVTInfo DstVT, SDNode OpNode,
                                  SDNode OpNodeRnd,
                                  X86FoldableSchedWrite sched, string asm,
                                  string aliasStr,
                                  bit CodeGenOnly = 1> {
  let Predicates = [HasAVX512] in {
    def rr_Int : SI<opc, MRMSrcReg, (outs DstVT.RC:$dst), (ins SrcVT.RC:$src),
                !strconcat(asm,"\t{$src, $dst|$dst, $src}"),
                [(set DstVT.RC:$dst, (OpNode (SrcVT.VT SrcVT.RC:$src)))]>,
                EVEX, VEX_LIG, Sched<[sched]>;
    def rrb_Int : SI<opc, MRMSrcReg, (outs DstVT.RC:$dst), (ins SrcVT.RC:$src, AVX512RC:$rc),
                 !strconcat(asm,"\t{$rc, $src, $dst|$dst, $src, $rc}"),
                 [(set DstVT.RC:$dst, (OpNodeRnd (SrcVT.VT SrcVT.RC:$src),(i32 imm:$rc)))]>,
                 EVEX, VEX_LIG, EVEX_B, EVEX_RC,
                 Sched<[sched]>;
    let isCodeGenOnly = CodeGenOnly, ForceDisassemble = CodeGenOnly in
    def rm_Int : SI<opc, MRMSrcMem, (outs DstVT.RC:$dst), (ins SrcVT.IntScalarMemOp:$src),
                !strconcat(asm,"\t{$src, $dst|$dst, $src}"),
                [(set DstVT.RC:$dst, (OpNode
                      (SrcVT.VT SrcVT.ScalarIntMemCPat:$src)))]>,
                EVEX, VEX_LIG, Sched<[sched.Folded, sched.ReadAfterFold]>;

    def : InstAlias<"v" # asm # aliasStr # "\t{$src, $dst|$dst, $src}",
            (!cast<Instruction>(NAME # "rr_Int") DstVT.RC:$dst, SrcVT.RC:$src), 0, "att">;
    def : InstAlias<"v" # asm # aliasStr # "\t{$rc, $src, $dst|$dst, $src, $rc}",
            (!cast<Instruction>(NAME # "rrb_Int") DstVT.RC:$dst, SrcVT.RC:$src, AVX512RC:$rc), 0, "att">;
  } // Predicates = [HasAVX512]
}

multiclass avx512_cvt_s_int_round_aliases<bits<8> opc, X86VectorVTInfo SrcVT,
                                          X86VectorVTInfo DstVT, SDNode OpNode,
                                          SDNode OpNodeRnd,
                                          X86FoldableSchedWrite sched, string asm,
                                          string aliasStr> :
  avx512_cvt_s_int_round<opc, SrcVT, DstVT, OpNode, OpNodeRnd, sched, asm, aliasStr, 0> {
  let Predicates = [HasAVX512] in {
    def : InstAlias<"v" # asm # aliasStr # "\t{$src, $dst|$dst, $src}",
            (!cast<Instruction>(NAME # "rm_Int") DstVT.RC:$dst,
                                            SrcVT.IntScalarMemOp:$src), 0, "att">;
  } // Predicates = [HasAVX512]
}

// Convert float/double to signed/unsigned int 32/64
defm VCVTSS2SIZ: avx512_cvt_s_int_round<0x2D, f32x_info, i32x_info,X86cvts2si,
                                   X86cvts2siRnd, WriteCvtSS2I, "cvtss2si", "{l}">,
                                   XS, EVEX_CD8<32, CD8VT1>;
defm VCVTSS2SI64Z: avx512_cvt_s_int_round<0x2D, f32x_info, i64x_info, X86cvts2si,
                                   X86cvts2siRnd, WriteCvtSS2I, "cvtss2si", "{q}">,
                                   XS, VEX_W, EVEX_CD8<32, CD8VT1>;
defm VCVTSS2USIZ: avx512_cvt_s_int_round_aliases<0x79, f32x_info, i32x_info, X86cvts2usi,
                                   X86cvts2usiRnd, WriteCvtSS2I, "cvtss2usi", "{l}">,
                                   XS, EVEX_CD8<32, CD8VT1>;
defm VCVTSS2USI64Z: avx512_cvt_s_int_round_aliases<0x79, f32x_info, i64x_info, X86cvts2usi,
                                   X86cvts2usiRnd, WriteCvtSS2I, "cvtss2usi", "{q}">,
                                   XS, VEX_W, EVEX_CD8<32, CD8VT1>;
defm VCVTSD2SIZ: avx512_cvt_s_int_round<0x2D, f64x_info, i32x_info, X86cvts2si,
                                   X86cvts2siRnd, WriteCvtSD2I, "cvtsd2si", "{l}">,
                                   XD, EVEX_CD8<64, CD8VT1>;
defm VCVTSD2SI64Z: avx512_cvt_s_int_round<0x2D, f64x_info, i64x_info, X86cvts2si,
                                   X86cvts2siRnd, WriteCvtSD2I, "cvtsd2si", "{q}">,
                                   XD, VEX_W, EVEX_CD8<64, CD8VT1>;
defm VCVTSD2USIZ:   avx512_cvt_s_int_round_aliases<0x79, f64x_info, i32x_info, X86cvts2usi,
                                   X86cvts2usiRnd, WriteCvtSD2I, "cvtsd2usi", "{l}">,
                                   XD, EVEX_CD8<64, CD8VT1>;
defm VCVTSD2USI64Z: avx512_cvt_s_int_round_aliases<0x79, f64x_info, i64x_info, X86cvts2usi,
                                   X86cvts2usiRnd, WriteCvtSD2I, "cvtsd2usi", "{q}">,
                                   XD, VEX_W, EVEX_CD8<64, CD8VT1>;

// Patterns used for matching vcvtsi2s{s,d} intrinsic sequences from clang
// which produce unnecessary vmovs{s,d} instructions
let Predicates = [HasAVX512] in {
def : Pat<(v4f32 (X86Movss
                   (v4f32 VR128X:$dst),
                   (v4f32 (scalar_to_vector (f32 (sint_to_fp GR64:$src)))))),
          (VCVTSI642SSZrr_Int VR128X:$dst, GR64:$src)>;

def : Pat<(v4f32 (X86Movss
                   (v4f32 VR128X:$dst),
                   (v4f32 (scalar_to_vector (f32 (sint_to_fp (loadi64 addr:$src))))))),
          (VCVTSI642SSZrm_Int VR128X:$dst, addr:$src)>;

def : Pat<(v4f32 (X86Movss
                   (v4f32 VR128X:$dst),
                   (v4f32 (scalar_to_vector (f32 (sint_to_fp GR32:$src)))))),
          (VCVTSI2SSZrr_Int VR128X:$dst, GR32:$src)>;

def : Pat<(v4f32 (X86Movss
                   (v4f32 VR128X:$dst),
                   (v4f32 (scalar_to_vector (f32 (sint_to_fp (loadi32 addr:$src))))))),
          (VCVTSI2SSZrm_Int VR128X:$dst, addr:$src)>;

def : Pat<(v2f64 (X86Movsd
                   (v2f64 VR128X:$dst),
                   (v2f64 (scalar_to_vector (f64 (sint_to_fp GR64:$src)))))),
          (VCVTSI642SDZrr_Int VR128X:$dst, GR64:$src)>;

def : Pat<(v2f64 (X86Movsd
                   (v2f64 VR128X:$dst),
                   (v2f64 (scalar_to_vector (f64 (sint_to_fp (loadi64 addr:$src))))))),
          (VCVTSI642SDZrm_Int VR128X:$dst, addr:$src)>;

def : Pat<(v2f64 (X86Movsd
                   (v2f64 VR128X:$dst),
                   (v2f64 (scalar_to_vector (f64 (sint_to_fp GR32:$src)))))),
          (VCVTSI2SDZrr_Int VR128X:$dst, GR32:$src)>;

def : Pat<(v2f64 (X86Movsd
                   (v2f64 VR128X:$dst),
                   (v2f64 (scalar_to_vector (f64 (sint_to_fp (loadi32 addr:$src))))))),
          (VCVTSI2SDZrm_Int VR128X:$dst, addr:$src)>;

def : Pat<(v4f32 (X86Movss
                   (v4f32 VR128X:$dst),
                   (v4f32 (scalar_to_vector (f32 (uint_to_fp GR64:$src)))))),
          (VCVTUSI642SSZrr_Int VR128X:$dst, GR64:$src)>;

def : Pat<(v4f32 (X86Movss
                   (v4f32 VR128X:$dst),
                   (v4f32 (scalar_to_vector (f32 (uint_to_fp (loadi64 addr:$src))))))),
          (VCVTUSI642SSZrm_Int VR128X:$dst, addr:$src)>;

def : Pat<(v4f32 (X86Movss
                   (v4f32 VR128X:$dst),
                   (v4f32 (scalar_to_vector (f32 (uint_to_fp GR32:$src)))))),
          (VCVTUSI2SSZrr_Int VR128X:$dst, GR32:$src)>;

def : Pat<(v4f32 (X86Movss
                   (v4f32 VR128X:$dst),
                   (v4f32 (scalar_to_vector (f32 (uint_to_fp (loadi32 addr:$src))))))),
          (VCVTUSI2SSZrm_Int VR128X:$dst, addr:$src)>;

def : Pat<(v2f64 (X86Movsd
                   (v2f64 VR128X:$dst),
                   (v2f64 (scalar_to_vector (f64 (uint_to_fp GR64:$src)))))),
          (VCVTUSI642SDZrr_Int VR128X:$dst, GR64:$src)>;

def : Pat<(v2f64 (X86Movsd
                   (v2f64 VR128X:$dst),
                   (v2f64 (scalar_to_vector (f64 (uint_to_fp (loadi64 addr:$src))))))),
          (VCVTUSI642SDZrm_Int VR128X:$dst, addr:$src)>;

def : Pat<(v2f64 (X86Movsd
                   (v2f64 VR128X:$dst),
                   (v2f64 (scalar_to_vector (f64 (uint_to_fp GR32:$src)))))),
          (VCVTUSI2SDZrr_Int VR128X:$dst, GR32:$src)>;

def : Pat<(v2f64 (X86Movsd
                   (v2f64 VR128X:$dst),
                   (v2f64 (scalar_to_vector (f64 (uint_to_fp (loadi32 addr:$src))))))),
          (VCVTUSI2SDZrm_Int VR128X:$dst, addr:$src)>;
} // Predicates = [HasAVX512]

// Convert float/double to signed/unsigned int 32/64 with truncation
multiclass avx512_cvt_s_all<bits<8> opc, string asm, X86VectorVTInfo _SrcRC,
                            X86VectorVTInfo _DstRC, SDNode OpNode,
                            SDNode OpNodeInt, SDNode OpNodeRnd,
                            X86FoldableSchedWrite sched, string aliasStr,
                            bit CodeGenOnly = 1>{
let Predicates = [HasAVX512] in {
  let isCodeGenOnly = 1 in {
  def rr : AVX512<opc, MRMSrcReg, (outs _DstRC.RC:$dst), (ins _SrcRC.FRC:$src),
              !strconcat(asm,"\t{$src, $dst|$dst, $src}"),
              [(set _DstRC.RC:$dst, (OpNode _SrcRC.FRC:$src))]>,
              EVEX, Sched<[sched]>;
  def rm : AVX512<opc, MRMSrcMem, (outs _DstRC.RC:$dst), (ins _SrcRC.ScalarMemOp:$src),
              !strconcat(asm,"\t{$src, $dst|$dst, $src}"),
              [(set _DstRC.RC:$dst, (OpNode (_SrcRC.ScalarLdFrag addr:$src)))]>,
              EVEX, Sched<[sched.Folded, sched.ReadAfterFold]>;
  }

  def rr_Int : AVX512<opc, MRMSrcReg, (outs _DstRC.RC:$dst), (ins _SrcRC.RC:$src),
            !strconcat(asm,"\t{$src, $dst|$dst, $src}"),
           [(set _DstRC.RC:$dst, (OpNodeInt (_SrcRC.VT _SrcRC.RC:$src)))]>,
           EVEX, VEX_LIG, Sched<[sched]>;
  def rrb_Int : AVX512<opc, MRMSrcReg, (outs _DstRC.RC:$dst), (ins _SrcRC.RC:$src),
            !strconcat(asm,"\t{{sae}, $src, $dst|$dst, $src, {sae}}"),
            [(set _DstRC.RC:$dst, (OpNodeRnd (_SrcRC.VT _SrcRC.RC:$src),
                                  (i32 FROUND_NO_EXC)))]>,
                                  EVEX,VEX_LIG , EVEX_B, Sched<[sched]>;
  let isCodeGenOnly = CodeGenOnly, ForceDisassemble = CodeGenOnly in
  def rm_Int : AVX512<opc, MRMSrcMem, (outs _DstRC.RC:$dst),
              (ins _SrcRC.IntScalarMemOp:$src),
              !strconcat(asm,"\t{$src, $dst|$dst, $src}"),
              [(set _DstRC.RC:$dst,
                (OpNodeInt (_SrcRC.VT _SrcRC.ScalarIntMemCPat:$src)))]>,
              EVEX, VEX_LIG, Sched<[sched.Folded, sched.ReadAfterFold]>;

  def : InstAlias<asm # aliasStr # "\t{$src, $dst|$dst, $src}",
          (!cast<Instruction>(NAME # "rr_Int") _DstRC.RC:$dst, _SrcRC.RC:$src), 0, "att">;
  def : InstAlias<asm # aliasStr # "\t{{sae}, $src, $dst|$dst, $src, {sae}}",
          (!cast<Instruction>(NAME # "rrb_Int") _DstRC.RC:$dst, _SrcRC.RC:$src), 0, "att">;
} //HasAVX512
}

multiclass avx512_cvt_s_all_unsigned<bits<8> opc, string asm,
                                     X86VectorVTInfo _SrcRC,
                                     X86VectorVTInfo _DstRC, SDNode OpNode,
                                     SDNode OpNodeInt, SDNode OpNodeRnd,
                                     X86FoldableSchedWrite sched,
                                     string aliasStr> :
  avx512_cvt_s_all<opc, asm, _SrcRC, _DstRC, OpNode, OpNodeInt, OpNodeRnd, sched,
                   aliasStr, 0> {
let Predicates = [HasAVX512] in {
  def : InstAlias<asm # aliasStr # "\t{$src, $dst|$dst, $src}",
          (!cast<Instruction>(NAME # "rm_Int") _DstRC.RC:$dst,
                                          _SrcRC.IntScalarMemOp:$src), 0, "att">;
}
}

defm VCVTTSS2SIZ: avx512_cvt_s_all<0x2C, "vcvttss2si", f32x_info, i32x_info,
                        fp_to_sint, X86cvtts2Int, X86cvtts2IntRnd, WriteCvtSS2I,
                        "{l}">, XS, EVEX_CD8<32, CD8VT1>;
defm VCVTTSS2SI64Z: avx512_cvt_s_all<0x2C, "vcvttss2si", f32x_info, i64x_info,
                        fp_to_sint, X86cvtts2Int, X86cvtts2IntRnd, WriteCvtSS2I,
                        "{q}">, VEX_W, XS, EVEX_CD8<32, CD8VT1>;
defm VCVTTSD2SIZ: avx512_cvt_s_all<0x2C, "vcvttsd2si", f64x_info, i32x_info,
                        fp_to_sint, X86cvtts2Int, X86cvtts2IntRnd, WriteCvtSD2I,
                        "{l}">, XD, EVEX_CD8<64, CD8VT1>;
defm VCVTTSD2SI64Z: avx512_cvt_s_all<0x2C, "vcvttsd2si", f64x_info, i64x_info,
                        fp_to_sint, X86cvtts2Int, X86cvtts2IntRnd, WriteCvtSD2I,
                        "{q}">, VEX_W, XD, EVEX_CD8<64, CD8VT1>;

defm VCVTTSS2USIZ: avx512_cvt_s_all_unsigned<0x78, "vcvttss2usi", f32x_info, i32x_info,
                        fp_to_uint, X86cvtts2UInt, X86cvtts2UIntRnd, WriteCvtSS2I,
                        "{l}">, XS, EVEX_CD8<32, CD8VT1>;
defm VCVTTSS2USI64Z: avx512_cvt_s_all_unsigned<0x78, "vcvttss2usi", f32x_info, i64x_info,
                        fp_to_uint, X86cvtts2UInt, X86cvtts2UIntRnd, WriteCvtSS2I,
                        "{q}">, XS,VEX_W, EVEX_CD8<32, CD8VT1>;
defm VCVTTSD2USIZ: avx512_cvt_s_all_unsigned<0x78, "vcvttsd2usi", f64x_info, i32x_info,
                        fp_to_uint, X86cvtts2UInt, X86cvtts2UIntRnd, WriteCvtSD2I,
                        "{l}">, XD, EVEX_CD8<64, CD8VT1>;
defm VCVTTSD2USI64Z: avx512_cvt_s_all_unsigned<0x78, "vcvttsd2usi", f64x_info, i64x_info,
                        fp_to_uint, X86cvtts2UInt, X86cvtts2UIntRnd, WriteCvtSD2I,
                        "{q}">, XD, VEX_W, EVEX_CD8<64, CD8VT1>;

//===----------------------------------------------------------------------===//
// AVX-512  Convert form float to double and back
//===----------------------------------------------------------------------===//

multiclass avx512_cvt_fp_scalar<bits<8> opc, string OpcodeStr, X86VectorVTInfo _,
                                X86VectorVTInfo _Src, SDNode OpNode,
                                X86FoldableSchedWrite sched> {
  defm rr_Int : AVX512_maskable_scalar<opc, MRMSrcReg, _, (outs _.RC:$dst),
                         (ins _.RC:$src1, _Src.RC:$src2), OpcodeStr,
                         "$src2, $src1", "$src1, $src2",
                         (_.VT (OpNode (_.VT _.RC:$src1),
                                       (_Src.VT _Src.RC:$src2),
                                       (i32 FROUND_CURRENT)))>,
                         EVEX_4V, VEX_LIG, Sched<[sched]>;
  defm rm_Int : AVX512_maskable_scalar<opc, MRMSrcMem, _, (outs _.RC:$dst),
                         (ins _.RC:$src1, _Src.IntScalarMemOp:$src2), OpcodeStr,
                         "$src2, $src1", "$src1, $src2",
                         (_.VT (OpNode (_.VT _.RC:$src1),
                                  (_Src.VT _Src.ScalarIntMemCPat:$src2),
                                  (i32 FROUND_CURRENT)))>,
                         EVEX_4V, VEX_LIG,
                         Sched<[sched.Folded, sched.ReadAfterFold]>;

  let isCodeGenOnly = 1, hasSideEffects = 0 in {
    def rr : I<opc, MRMSrcReg, (outs _.FRC:$dst),
               (ins _.FRC:$src1, _Src.FRC:$src2),
               OpcodeStr#"\t{$src2, $src1, $dst|$dst, $src1, $src2}", []>,
               EVEX_4V, VEX_LIG, Sched<[sched]>;
    let mayLoad = 1 in
    def rm : I<opc, MRMSrcMem, (outs _.FRC:$dst),
               (ins _.FRC:$src1, _Src.ScalarMemOp:$src2),
               OpcodeStr#"\t{$src2, $src1, $dst|$dst, $src1, $src2}", []>,
               EVEX_4V, VEX_LIG, Sched<[sched.Folded, sched.ReadAfterFold]>;
  }
}

// Scalar Coversion with SAE - suppress all exceptions
multiclass avx512_cvt_fp_sae_scalar<bits<8> opc, string OpcodeStr, X86VectorVTInfo _,
                                    X86VectorVTInfo _Src, SDNode OpNodeRnd,
                                    X86FoldableSchedWrite sched> {
  defm rrb_Int : AVX512_maskable_scalar<opc, MRMSrcReg, _, (outs _.RC:$dst),
                        (ins _.RC:$src1, _Src.RC:$src2), OpcodeStr,
                        "{sae}, $src2, $src1", "$src1, $src2, {sae}",
                        (_.VT (OpNodeRnd (_.VT _.RC:$src1),
                                         (_Src.VT _Src.RC:$src2),
                                         (i32 FROUND_NO_EXC)))>,
                        EVEX_4V, VEX_LIG, EVEX_B, Sched<[sched]>;
}

// Scalar Conversion with rounding control (RC)
multiclass avx512_cvt_fp_rc_scalar<bits<8> opc, string OpcodeStr, X86VectorVTInfo _,
                                   X86VectorVTInfo _Src, SDNode OpNodeRnd,
                                   X86FoldableSchedWrite sched> {
  defm rrb_Int : AVX512_maskable_scalar<opc, MRMSrcReg, _, (outs _.RC:$dst),
                        (ins _.RC:$src1, _Src.RC:$src2, AVX512RC:$rc), OpcodeStr,
                        "$rc, $src2, $src1", "$src1, $src2, $rc",
                        (_.VT (OpNodeRnd (_.VT _.RC:$src1),
                                         (_Src.VT _Src.RC:$src2), (i32 imm:$rc)))>,
                        EVEX_4V, VEX_LIG, Sched<[sched]>,
                        EVEX_B, EVEX_RC;
}
multiclass avx512_cvt_fp_scalar_sd2ss<bits<8> opc, string OpcodeStr,
                                  SDNode OpNodeRnd, X86FoldableSchedWrite sched,
                                  X86VectorVTInfo _src, X86VectorVTInfo _dst> {
  let Predicates = [HasAVX512] in {
    defm Z : avx512_cvt_fp_scalar<opc, OpcodeStr, _dst, _src, OpNodeRnd, sched>,
             avx512_cvt_fp_rc_scalar<opc, OpcodeStr, _dst, _src,
                               OpNodeRnd, sched>, VEX_W, EVEX_CD8<64, CD8VT1>, XD;
  }
}

multiclass avx512_cvt_fp_scalar_ss2sd<bits<8> opc, string OpcodeStr, SDNode OpNodeRnd,
                                      X86FoldableSchedWrite sched,
                                      X86VectorVTInfo _src, X86VectorVTInfo _dst> {
  let Predicates = [HasAVX512] in {
    defm Z : avx512_cvt_fp_scalar<opc, OpcodeStr, _dst, _src, OpNodeRnd, sched>,
             avx512_cvt_fp_sae_scalar<opc, OpcodeStr, _dst, _src, OpNodeRnd, sched>,
             EVEX_CD8<32, CD8VT1>, XS;
  }
}
defm VCVTSD2SS : avx512_cvt_fp_scalar_sd2ss<0x5A, "vcvtsd2ss",
                                         X86froundRnd, WriteCvtSD2SS, f64x_info,
                                         f32x_info>;
defm VCVTSS2SD : avx512_cvt_fp_scalar_ss2sd<0x5A, "vcvtss2sd",
                                          X86fpextRnd, WriteCvtSS2SD, f32x_info,
                                          f64x_info>;

def : Pat<(f64 (fpextend FR32X:$src)),
          (VCVTSS2SDZrr (f64 (IMPLICIT_DEF)), FR32X:$src)>,
          Requires<[HasAVX512]>;
def : Pat<(f64 (fpextend (loadf32 addr:$src))),
          (VCVTSS2SDZrm (f64 (IMPLICIT_DEF)), addr:$src)>,
          Requires<[HasAVX512, OptForSize]>;

def : Pat<(f64 (extloadf32 addr:$src)),
          (VCVTSS2SDZrm (f64 (IMPLICIT_DEF)), addr:$src)>,
      Requires<[HasAVX512, OptForSize]>;

def : Pat<(f64 (extloadf32 addr:$src)),
          (VCVTSS2SDZrr (f64 (IMPLICIT_DEF)), (VMOVSSZrm addr:$src))>,
          Requires<[HasAVX512, OptForSpeed]>;

def : Pat<(f32 (fpround FR64X:$src)),
          (VCVTSD2SSZrr (f32 (IMPLICIT_DEF)), FR64X:$src)>,
           Requires<[HasAVX512]>;

def : Pat<(v4f32 (X86Movss
                   (v4f32 VR128X:$dst),
                   (v4f32 (scalar_to_vector
                     (f32 (fpround (f64 (extractelt VR128X:$src, (iPTR 0))))))))),
          (VCVTSD2SSZrr_Int VR128X:$dst, VR128X:$src)>,
          Requires<[HasAVX512]>;

def : Pat<(v2f64 (X86Movsd
                   (v2f64 VR128X:$dst),
                   (v2f64 (scalar_to_vector
                     (f64 (fpextend (f32 (extractelt VR128X:$src, (iPTR 0))))))))),
          (VCVTSS2SDZrr_Int VR128X:$dst, VR128X:$src)>,
          Requires<[HasAVX512]>;

//===----------------------------------------------------------------------===//
// AVX-512  Vector convert from signed/unsigned integer to float/double
//          and from float/double to signed/unsigned integer
//===----------------------------------------------------------------------===//

multiclass avx512_vcvt_fp<bits<8> opc, string OpcodeStr, X86VectorVTInfo _,
                          X86VectorVTInfo _Src, SDNode OpNode,
                          X86FoldableSchedWrite sched,
                          string Broadcast = _.BroadcastStr,
                          string Alias = "", X86MemOperand MemOp = _Src.MemOp,
                          RegisterClass MaskRC = _.KRCWM> {

  defm rr : AVX512_maskable_common<opc, MRMSrcReg, _, (outs _.RC:$dst),
                         (ins _Src.RC:$src),
                         (ins _.RC:$src0, MaskRC:$mask, _Src.RC:$src),
                         (ins MaskRC:$mask, _Src.RC:$src),
                          OpcodeStr, "$src", "$src",
                         (_.VT (OpNode (_Src.VT _Src.RC:$src))),
                         (vselect MaskRC:$mask,
                                  (_.VT (OpNode (_Src.VT _Src.RC:$src))),
                                  _.RC:$src0),
                         vselect, "$src0 = $dst">,
                         EVEX, Sched<[sched]>;

  defm rm : AVX512_maskable_common<opc, MRMSrcMem, _, (outs _.RC:$dst),
                         (ins MemOp:$src),
                         (ins _.RC:$src0, MaskRC:$mask, MemOp:$src),
                         (ins MaskRC:$mask, MemOp:$src),
                         OpcodeStr#Alias, "$src", "$src",
                         (_.VT (OpNode (_Src.VT
                             (_Src.LdFrag addr:$src)))),
                         (vselect MaskRC:$mask,
                                  (_.VT (OpNode (_Src.VT
                                                 (_Src.LdFrag addr:$src)))),
                                  _.RC:$src0),
                         vselect, "$src0 = $dst">,
                         EVEX, Sched<[sched.Folded]>;

  defm rmb : AVX512_maskable_common<opc, MRMSrcMem, _, (outs _.RC:$dst),
                         (ins _Src.ScalarMemOp:$src),
                         (ins _.RC:$src0, MaskRC:$mask, _Src.ScalarMemOp:$src),
                         (ins MaskRC:$mask, _Src.ScalarMemOp:$src),
                         OpcodeStr,
                         "${src}"##Broadcast, "${src}"##Broadcast,
                         (_.VT (OpNode (_Src.VT
                                  (X86VBroadcast (_Src.ScalarLdFrag addr:$src)))
                            )),
                         (vselect MaskRC:$mask,
                                  (_.VT
                                   (OpNode
                                    (_Src.VT
                                     (X86VBroadcast
                                      (_Src.ScalarLdFrag addr:$src))))),
                                  _.RC:$src0),
                         vselect, "$src0 = $dst">,
                         EVEX, EVEX_B, Sched<[sched.Folded]>;
}
// Coversion with SAE - suppress all exceptions
multiclass avx512_vcvt_fp_sae<bits<8> opc, string OpcodeStr, X86VectorVTInfo _,
                              X86VectorVTInfo _Src, SDNode OpNodeRnd,
                              X86FoldableSchedWrite sched> {
  defm rrb : AVX512_maskable<opc, MRMSrcReg, _, (outs _.RC:$dst),
                        (ins _Src.RC:$src), OpcodeStr,
                        "{sae}, $src", "$src, {sae}",
                        (_.VT (OpNodeRnd (_Src.VT _Src.RC:$src),
                               (i32 FROUND_NO_EXC)))>,
                        EVEX, EVEX_B, Sched<[sched]>;
}

// Conversion with rounding control (RC)
multiclass avx512_vcvt_fp_rc<bits<8> opc, string OpcodeStr, X86VectorVTInfo _,
                         X86VectorVTInfo _Src, SDNode OpNodeRnd,
                         X86FoldableSchedWrite sched> {
  defm rrb : AVX512_maskable<opc, MRMSrcReg, _, (outs _.RC:$dst),
                        (ins _Src.RC:$src, AVX512RC:$rc), OpcodeStr,
                        "$rc, $src", "$src, $rc",
                        (_.VT (OpNodeRnd (_Src.VT _Src.RC:$src), (i32 imm:$rc)))>,
                        EVEX, EVEX_B, EVEX_RC, Sched<[sched]>;
}

// Extend Float to Double
multiclass avx512_cvtps2pd<bits<8> opc, string OpcodeStr,
                           X86SchedWriteWidths sched> {
  let Predicates = [HasAVX512] in {
    defm Z : avx512_vcvt_fp<opc, OpcodeStr, v8f64_info, v8f32x_info,
                            fpextend, sched.ZMM>,
             avx512_vcvt_fp_sae<opc, OpcodeStr, v8f64_info, v8f32x_info,
                                X86vfpextRnd, sched.ZMM>, EVEX_V512;
  }
  let Predicates = [HasVLX] in {
    defm Z128 : avx512_vcvt_fp<opc, OpcodeStr, v2f64x_info, v4f32x_info,
                               X86vfpext, sched.XMM, "{1to2}", "", f64mem>, EVEX_V128;
    defm Z256 : avx512_vcvt_fp<opc, OpcodeStr, v4f64x_info, v4f32x_info, fpextend,
                               sched.YMM>, EVEX_V256;
  }
}

// Truncate Double to Float
multiclass avx512_cvtpd2ps<bits<8> opc, string OpcodeStr, X86SchedWriteWidths sched> {
  let Predicates = [HasAVX512] in {
    defm Z : avx512_vcvt_fp<opc, OpcodeStr, v8f32x_info, v8f64_info, fpround, sched.ZMM>,
             avx512_vcvt_fp_rc<opc, OpcodeStr, v8f32x_info, v8f64_info,
                               X86vfproundRnd, sched.ZMM>, EVEX_V512;
  }
  let Predicates = [HasVLX] in {
    defm Z128 : avx512_vcvt_fp<opc, OpcodeStr, v4f32x_info, v2f64x_info,
                               null_frag, sched.XMM, "{1to2}", "{x}", f128mem, VK2WM>,
                               EVEX_V128;
    defm Z256 : avx512_vcvt_fp<opc, OpcodeStr, v4f32x_info, v4f64x_info, fpround,
                               sched.YMM, "{1to4}", "{y}">, EVEX_V256;

    def : InstAlias<OpcodeStr##"x\t{$src, $dst|$dst, $src}",
                    (!cast<Instruction>(NAME # "Z128rr") VR128X:$dst, VR128X:$src), 0>;
    def : InstAlias<OpcodeStr##"x\t{$src, $dst|$dst, $src}",
                    (!cast<Instruction>(NAME # "Z128rm") VR128X:$dst, f128mem:$src), 0, "intel">;
    def : InstAlias<OpcodeStr##"y\t{$src, $dst|$dst, $src}",
                    (!cast<Instruction>(NAME # "Z256rr") VR128X:$dst, VR256X:$src), 0>;
    def : InstAlias<OpcodeStr##"y\t{$src, $dst|$dst, $src}",
                    (!cast<Instruction>(NAME # "Z256rm") VR128X:$dst, f256mem:$src), 0, "intel">;
  }
}

defm VCVTPD2PS : avx512_cvtpd2ps<0x5A, "vcvtpd2ps", SchedWriteCvtPD2PS>,
                                  VEX_W, PD, EVEX_CD8<64, CD8VF>;
defm VCVTPS2PD : avx512_cvtps2pd<0x5A, "vcvtps2pd", SchedWriteCvtPS2PD>,
                                  PS, EVEX_CD8<32, CD8VH>;

def : Pat<(v8f64 (extloadv8f32 addr:$src)),
            (VCVTPS2PDZrm addr:$src)>;

let Predicates = [HasVLX] in {
  def : Pat<(X86vzmovl (v2f64 (bitconvert
                               (v4f32 (X86vfpround (v2f64 VR128X:$src)))))),
            (VCVTPD2PSZ128rr VR128X:$src)>;
  def : Pat<(X86vzmovl (v2f64 (bitconvert
                               (v4f32 (X86vfpround (loadv2f64 addr:$src)))))),
            (VCVTPD2PSZ128rm addr:$src)>;
  def : Pat<(v2f64 (extloadv2f32 addr:$src)),
              (VCVTPS2PDZ128rm addr:$src)>;
  def : Pat<(v4f64 (extloadv4f32 addr:$src)),
              (VCVTPS2PDZ256rm addr:$src)>;

  // Special patterns to allow use of X86vmfpround for masking. Instruction
  // patterns have been disabled with null_frag.
  def : Pat<(X86vfpround (v2f64 VR128X:$src)),
            (VCVTPD2PSZ128rr VR128X:$src)>;
  def : Pat<(X86vmfpround (v2f64 VR128X:$src), (v4f32 VR128X:$src0),
                          VK2WM:$mask),
            (VCVTPD2PSZ128rrk VR128X:$src0, VK2WM:$mask, VR128X:$src)>;
  def : Pat<(X86vmfpround (v2f64 VR128X:$src), v4f32x_info.ImmAllZerosV,
                          VK2WM:$mask),
            (VCVTPD2PSZ128rrkz VK2WM:$mask, VR128X:$src)>;

  def : Pat<(X86vfpround (loadv2f64 addr:$src)),
            (VCVTPD2PSZ128rm addr:$src)>;
  def : Pat<(X86vmfpround (loadv2f64 addr:$src), (v4f32 VR128X:$src0),
                          VK2WM:$mask),
            (VCVTPD2PSZ128rmk VR128X:$src0, VK2WM:$mask, addr:$src)>;
  def : Pat<(X86vmfpround (loadv2f64 addr:$src), v4f32x_info.ImmAllZerosV,
                          VK2WM:$mask),
            (VCVTPD2PSZ128rmkz VK2WM:$mask, addr:$src)>;

  def : Pat<(X86vfpround (v2f64 (X86VBroadcast (loadf64 addr:$src)))),
            (VCVTPD2PSZ128rmb addr:$src)>;
  def : Pat<(X86vmfpround (v2f64 (X86VBroadcast (loadf64 addr:$src))),
                          (v4f32 VR128X:$src0), VK2WM:$mask),
            (VCVTPD2PSZ128rmbk VR128X:$src0, VK2WM:$mask, addr:$src)>;
  def : Pat<(X86vmfpround (v2f64 (X86VBroadcast (loadf64 addr:$src))),
                          v4f32x_info.ImmAllZerosV, VK2WM:$mask),
            (VCVTPD2PSZ128rmbkz VK2WM:$mask, addr:$src)>;
}

// Convert Signed/Unsigned Doubleword to Double
multiclass avx512_cvtdq2pd<bits<8> opc, string OpcodeStr, SDNode OpNode,
                           SDNode OpNode128, X86SchedWriteWidths sched> {
  // No rounding in this op
  let Predicates = [HasAVX512] in
    defm Z : avx512_vcvt_fp<opc, OpcodeStr, v8f64_info, v8i32x_info, OpNode,
                            sched.ZMM>, EVEX_V512;

  let Predicates = [HasVLX] in {
    defm Z128 : avx512_vcvt_fp<opc, OpcodeStr, v2f64x_info, v4i32x_info,
                               OpNode128, sched.XMM, "{1to2}", "", i64mem>, EVEX_V128;
    defm Z256 : avx512_vcvt_fp<opc, OpcodeStr, v4f64x_info, v4i32x_info, OpNode,
                               sched.YMM>, EVEX_V256;
  }
}

// Convert Signed/Unsigned Doubleword to Float
multiclass avx512_cvtdq2ps<bits<8> opc, string OpcodeStr, SDNode OpNode,
                           SDNode OpNodeRnd, X86SchedWriteWidths sched> {
  let Predicates = [HasAVX512] in
    defm Z : avx512_vcvt_fp<opc, OpcodeStr, v16f32_info, v16i32_info, OpNode,
                            sched.ZMM>,
             avx512_vcvt_fp_rc<opc, OpcodeStr, v16f32_info, v16i32_info,
                               OpNodeRnd, sched.ZMM>, EVEX_V512;

  let Predicates = [HasVLX] in {
    defm Z128 : avx512_vcvt_fp<opc, OpcodeStr, v4f32x_info, v4i32x_info, OpNode,
                               sched.XMM>, EVEX_V128;
    defm Z256 : avx512_vcvt_fp<opc, OpcodeStr, v8f32x_info, v8i32x_info, OpNode,
                               sched.YMM>, EVEX_V256;
  }
}

// Convert Float to Signed/Unsigned Doubleword with truncation
multiclass avx512_cvttps2dq<bits<8> opc, string OpcodeStr, SDNode OpNode,
                            SDNode OpNodeRnd, X86SchedWriteWidths sched> {
  let Predicates = [HasAVX512] in {
    defm Z : avx512_vcvt_fp<opc, OpcodeStr, v16i32_info, v16f32_info, OpNode,
                            sched.ZMM>,
             avx512_vcvt_fp_sae<opc, OpcodeStr, v16i32_info, v16f32_info,
                                OpNodeRnd, sched.ZMM>, EVEX_V512;
  }
  let Predicates = [HasVLX] in {
    defm Z128 : avx512_vcvt_fp<opc, OpcodeStr, v4i32x_info, v4f32x_info, OpNode,
                               sched.XMM>, EVEX_V128;
    defm Z256 : avx512_vcvt_fp<opc, OpcodeStr, v8i32x_info, v8f32x_info, OpNode,
                               sched.YMM>, EVEX_V256;
  }
}

// Convert Float to Signed/Unsigned Doubleword
multiclass avx512_cvtps2dq<bits<8> opc, string OpcodeStr, SDNode OpNode,
                           SDNode OpNodeRnd, X86SchedWriteWidths sched> {
  let Predicates = [HasAVX512] in {
    defm Z : avx512_vcvt_fp<opc, OpcodeStr, v16i32_info, v16f32_info, OpNode,
                            sched.ZMM>,
             avx512_vcvt_fp_rc<opc, OpcodeStr, v16i32_info, v16f32_info,
                                OpNodeRnd, sched.ZMM>, EVEX_V512;
  }
  let Predicates = [HasVLX] in {
    defm Z128 : avx512_vcvt_fp<opc, OpcodeStr, v4i32x_info, v4f32x_info, OpNode,
                               sched.XMM>, EVEX_V128;
    defm Z256 : avx512_vcvt_fp<opc, OpcodeStr, v8i32x_info, v8f32x_info, OpNode,
                               sched.YMM>, EVEX_V256;
  }
}

// Convert Double to Signed/Unsigned Doubleword with truncation
multiclass avx512_cvttpd2dq<bits<8> opc, string OpcodeStr, SDNode OpNode,
                            SDNode OpNodeRnd, X86SchedWriteWidths sched> {
  let Predicates = [HasAVX512] in {
    defm Z : avx512_vcvt_fp<opc, OpcodeStr, v8i32x_info, v8f64_info, OpNode,
                            sched.ZMM>,
             avx512_vcvt_fp_sae<opc, OpcodeStr, v8i32x_info, v8f64_info,
                                OpNodeRnd, sched.ZMM>, EVEX_V512;
  }
  let Predicates = [HasVLX] in {
    // we need "x"/"y" suffixes in order to distinguish between 128 and 256
    // memory forms of these instructions in Asm Parser. They have the same
    // dest type - 'v4i32x_info'. We also specify the broadcast string explicitly
    // due to the same reason.
    defm Z128 : avx512_vcvt_fp<opc, OpcodeStr, v4i32x_info, v2f64x_info,
                               null_frag, sched.XMM, "{1to2}", "{x}", f128mem,
                               VK2WM>, EVEX_V128;
    defm Z256 : avx512_vcvt_fp<opc, OpcodeStr, v4i32x_info, v4f64x_info, OpNode,
                               sched.YMM, "{1to4}", "{y}">, EVEX_V256;

    def : InstAlias<OpcodeStr##"x\t{$src, $dst|$dst, $src}",
                    (!cast<Instruction>(NAME # "Z128rr") VR128X:$dst, VR128X:$src), 0>;
    def : InstAlias<OpcodeStr##"x\t{$src, $dst|$dst, $src}",
                    (!cast<Instruction>(NAME # "Z128rm") VR128X:$dst, i128mem:$src), 0, "intel">;
    def : InstAlias<OpcodeStr##"y\t{$src, $dst|$dst, $src}",
                    (!cast<Instruction>(NAME # "Z256rr") VR128X:$dst, VR256X:$src), 0>;
    def : InstAlias<OpcodeStr##"y\t{$src, $dst|$dst, $src}",
                    (!cast<Instruction>(NAME # "Z256rm") VR128X:$dst, i256mem:$src), 0, "intel">;
  }
}

// Convert Double to Signed/Unsigned Doubleword
multiclass avx512_cvtpd2dq<bits<8> opc, string OpcodeStr, SDNode OpNode,
                           SDNode OpNodeRnd, X86SchedWriteWidths sched> {
  let Predicates = [HasAVX512] in {
    defm Z : avx512_vcvt_fp<opc, OpcodeStr, v8i32x_info, v8f64_info, OpNode,
                            sched.ZMM>,
             avx512_vcvt_fp_rc<opc, OpcodeStr, v8i32x_info, v8f64_info,
                               OpNodeRnd, sched.ZMM>, EVEX_V512;
  }
  let Predicates = [HasVLX] in {
    // we need "x"/"y" suffixes in order to distinguish between 128 and 256
    // memory forms of these instructions in Asm Parcer. They have the same
    // dest type - 'v4i32x_info'. We also specify the broadcast string explicitly
    // due to the same reason.
    defm Z128 : avx512_vcvt_fp<opc, OpcodeStr, v4i32x_info, v2f64x_info,
                               null_frag, sched.XMM, "{1to2}", "{x}", f128mem,
                               VK2WM>, EVEX_V128;
    defm Z256 : avx512_vcvt_fp<opc, OpcodeStr, v4i32x_info, v4f64x_info, OpNode,
                               sched.YMM, "{1to4}", "{y}">, EVEX_V256;

    def : InstAlias<OpcodeStr##"x\t{$src, $dst|$dst, $src}",
                    (!cast<Instruction>(NAME # "Z128rr") VR128X:$dst, VR128X:$src), 0>;
    def : InstAlias<OpcodeStr##"x\t{$src, $dst|$dst, $src}",
                    (!cast<Instruction>(NAME # "Z128rm") VR128X:$dst, f128mem:$src), 0, "intel">;
    def : InstAlias<OpcodeStr##"y\t{$src, $dst|$dst, $src}",
                    (!cast<Instruction>(NAME # "Z256rr") VR128X:$dst, VR256X:$src), 0>;
    def : InstAlias<OpcodeStr##"y\t{$src, $dst|$dst, $src}",
                    (!cast<Instruction>(NAME # "Z256rm") VR128X:$dst, f256mem:$src), 0, "intel">;
  }
}

// Convert Double to Signed/Unsigned Quardword
multiclass avx512_cvtpd2qq<bits<8> opc, string OpcodeStr, SDNode OpNode,
                           SDNode OpNodeRnd, X86SchedWriteWidths sched> {
  let Predicates = [HasDQI] in {
    defm Z : avx512_vcvt_fp<opc, OpcodeStr, v8i64_info, v8f64_info, OpNode,
                            sched.ZMM>,
             avx512_vcvt_fp_rc<opc, OpcodeStr, v8i64_info, v8f64_info,
                               OpNodeRnd, sched.ZMM>, EVEX_V512;
  }
  let Predicates = [HasDQI, HasVLX] in {
    defm Z128 : avx512_vcvt_fp<opc, OpcodeStr, v2i64x_info, v2f64x_info, OpNode,
                               sched.XMM>, EVEX_V128;
    defm Z256 : avx512_vcvt_fp<opc, OpcodeStr, v4i64x_info, v4f64x_info, OpNode,
                               sched.YMM>, EVEX_V256;
  }
}

// Convert Double to Signed/Unsigned Quardword with truncation
multiclass avx512_cvttpd2qq<bits<8> opc, string OpcodeStr, SDNode OpNode,
                            SDNode OpNodeRnd, X86SchedWriteWidths sched> {
  let Predicates = [HasDQI] in {
    defm Z : avx512_vcvt_fp<opc, OpcodeStr, v8i64_info, v8f64_info, OpNode,
                            sched.ZMM>,
             avx512_vcvt_fp_sae<opc, OpcodeStr, v8i64_info, v8f64_info,
                                OpNodeRnd, sched.ZMM>, EVEX_V512;
  }
  let Predicates = [HasDQI, HasVLX] in {
    defm Z128 : avx512_vcvt_fp<opc, OpcodeStr, v2i64x_info, v2f64x_info, OpNode,
                               sched.XMM>, EVEX_V128;
    defm Z256 : avx512_vcvt_fp<opc, OpcodeStr, v4i64x_info, v4f64x_info, OpNode,
                               sched.YMM>, EVEX_V256;
  }
}

// Convert Signed/Unsigned Quardword to Double
multiclass avx512_cvtqq2pd<bits<8> opc, string OpcodeStr, SDNode OpNode,
                           SDNode OpNodeRnd, X86SchedWriteWidths sched> {
  let Predicates = [HasDQI] in {
    defm Z : avx512_vcvt_fp<opc, OpcodeStr, v8f64_info, v8i64_info, OpNode,
                            sched.ZMM>,
             avx512_vcvt_fp_rc<opc, OpcodeStr, v8f64_info, v8i64_info,
                               OpNodeRnd, sched.ZMM>, EVEX_V512;
  }
  let Predicates = [HasDQI, HasVLX] in {
    defm Z128 : avx512_vcvt_fp<opc, OpcodeStr, v2f64x_info, v2i64x_info, OpNode,
                               sched.XMM>, EVEX_V128, NotEVEX2VEXConvertible;
    defm Z256 : avx512_vcvt_fp<opc, OpcodeStr, v4f64x_info, v4i64x_info, OpNode,
                               sched.YMM>, EVEX_V256, NotEVEX2VEXConvertible;
  }
}

// Convert Float to Signed/Unsigned Quardword
multiclass avx512_cvtps2qq<bits<8> opc, string OpcodeStr, SDNode OpNode,
                           SDNode OpNodeRnd, X86SchedWriteWidths sched> {
  let Predicates = [HasDQI] in {
    defm Z : avx512_vcvt_fp<opc, OpcodeStr, v8i64_info, v8f32x_info, OpNode,
                            sched.ZMM>,
             avx512_vcvt_fp_rc<opc, OpcodeStr, v8i64_info, v8f32x_info,
                               OpNodeRnd, sched.ZMM>, EVEX_V512;
  }
  let Predicates = [HasDQI, HasVLX] in {
    // Explicitly specified broadcast string, since we take only 2 elements
    // from v4f32x_info source
    defm Z128 : avx512_vcvt_fp<opc, OpcodeStr, v2i64x_info, v4f32x_info, OpNode,
                               sched.XMM, "{1to2}", "", f64mem>, EVEX_V128;
    defm Z256 : avx512_vcvt_fp<opc, OpcodeStr, v4i64x_info, v4f32x_info, OpNode,
                               sched.YMM>, EVEX_V256;
  }
}

// Convert Float to Signed/Unsigned Quardword with truncation
multiclass avx512_cvttps2qq<bits<8> opc, string OpcodeStr, SDNode OpNode,
                            SDNode OpNodeRnd, X86SchedWriteWidths sched> {
  let Predicates = [HasDQI] in {
    defm Z : avx512_vcvt_fp<opc, OpcodeStr, v8i64_info, v8f32x_info, OpNode, sched.ZMM>,
             avx512_vcvt_fp_sae<opc, OpcodeStr, v8i64_info, v8f32x_info,
                                OpNodeRnd, sched.ZMM>, EVEX_V512;
  }
  let Predicates = [HasDQI, HasVLX] in {
    // Explicitly specified broadcast string, since we take only 2 elements
    // from v4f32x_info source
    defm Z128 : avx512_vcvt_fp<opc, OpcodeStr, v2i64x_info, v4f32x_info, OpNode,
                               sched.XMM, "{1to2}", "", f64mem>, EVEX_V128;
    defm Z256 : avx512_vcvt_fp<opc, OpcodeStr, v4i64x_info, v4f32x_info, OpNode,
                               sched.YMM>, EVEX_V256;
  }
}

// Convert Signed/Unsigned Quardword to Float
multiclass avx512_cvtqq2ps<bits<8> opc, string OpcodeStr, SDNode OpNode,
                           SDNode OpNode128, SDNode OpNodeRnd,
                           X86SchedWriteWidths sched> {
  let Predicates = [HasDQI] in {
    defm Z : avx512_vcvt_fp<opc, OpcodeStr, v8f32x_info, v8i64_info, OpNode,
                            sched.ZMM>,
             avx512_vcvt_fp_rc<opc, OpcodeStr, v8f32x_info, v8i64_info,
                               OpNodeRnd, sched.ZMM>, EVEX_V512;
  }
  let Predicates = [HasDQI, HasVLX] in {
    // we need "x"/"y" suffixes in order to distinguish between 128 and 256
    // memory forms of these instructions in Asm Parcer. They have the same
    // dest type - 'v4i32x_info'. We also specify the broadcast string explicitly
    // due to the same reason.
    defm Z128 : avx512_vcvt_fp<opc, OpcodeStr, v4f32x_info, v2i64x_info, OpNode128,
                               sched.XMM, "{1to2}", "{x}">, EVEX_V128,
                               NotEVEX2VEXConvertible;
    defm Z256 : avx512_vcvt_fp<opc, OpcodeStr, v4f32x_info, v4i64x_info, OpNode,
                               sched.YMM, "{1to4}", "{y}">, EVEX_V256,
                               NotEVEX2VEXConvertible;

    def : InstAlias<OpcodeStr##"x\t{$src, $dst|$dst, $src}",
                    (!cast<Instruction>(NAME # "Z128rr") VR128X:$dst, VR128X:$src), 0>;
    def : InstAlias<OpcodeStr##"x\t{$src, $dst|$dst, $src}",
                    (!cast<Instruction>(NAME # "Z128rm") VR128X:$dst, i128mem:$src), 0, "intel">;
    def : InstAlias<OpcodeStr##"y\t{$src, $dst|$dst, $src}",
                    (!cast<Instruction>(NAME # "Z256rr") VR128X:$dst, VR256X:$src), 0>;
    def : InstAlias<OpcodeStr##"y\t{$src, $dst|$dst, $src}",
                    (!cast<Instruction>(NAME # "Z256rm") VR128X:$dst, i256mem:$src), 0, "intel">;
  }
}

defm VCVTDQ2PD : avx512_cvtdq2pd<0xE6, "vcvtdq2pd", sint_to_fp, X86VSintToFP,
                                 SchedWriteCvtDQ2PD>, XS, EVEX_CD8<32, CD8VH>;

defm VCVTDQ2PS : avx512_cvtdq2ps<0x5B, "vcvtdq2ps", sint_to_fp,
                                X86VSintToFpRnd, SchedWriteCvtDQ2PS>,
                                PS, EVEX_CD8<32, CD8VF>;

defm VCVTTPS2DQ : avx512_cvttps2dq<0x5B, "vcvttps2dq", X86cvttp2si,
                                X86cvttp2siRnd, SchedWriteCvtPS2DQ>,
                                XS, EVEX_CD8<32, CD8VF>;

defm VCVTTPD2DQ : avx512_cvttpd2dq<0xE6, "vcvttpd2dq", X86cvttp2si,
                                 X86cvttp2siRnd, SchedWriteCvtPD2DQ>,
                                 PD, VEX_W, EVEX_CD8<64, CD8VF>;

defm VCVTTPS2UDQ : avx512_cvttps2dq<0x78, "vcvttps2udq", X86cvttp2ui,
                                 X86cvttp2uiRnd, SchedWriteCvtPS2DQ>, PS,
                                 EVEX_CD8<32, CD8VF>;

defm VCVTTPD2UDQ : avx512_cvttpd2dq<0x78, "vcvttpd2udq", X86cvttp2ui,
                                 X86cvttp2uiRnd, SchedWriteCvtPD2DQ>,
                                 PS, VEX_W, EVEX_CD8<64, CD8VF>;

defm VCVTUDQ2PD : avx512_cvtdq2pd<0x7A, "vcvtudq2pd", uint_to_fp,
                                  X86VUintToFP, SchedWriteCvtDQ2PD>, XS,
                                  EVEX_CD8<32, CD8VH>;

defm VCVTUDQ2PS : avx512_cvtdq2ps<0x7A, "vcvtudq2ps", uint_to_fp,
                                 X86VUintToFpRnd, SchedWriteCvtDQ2PS>, XD,
                                 EVEX_CD8<32, CD8VF>;

defm VCVTPS2DQ : avx512_cvtps2dq<0x5B, "vcvtps2dq", X86cvtp2Int,
                                 X86cvtp2IntRnd, SchedWriteCvtPS2DQ>, PD,
                                 EVEX_CD8<32, CD8VF>;

defm VCVTPD2DQ : avx512_cvtpd2dq<0xE6, "vcvtpd2dq", X86cvtp2Int,
                                 X86cvtp2IntRnd, SchedWriteCvtPD2DQ>, XD,
                                 VEX_W, EVEX_CD8<64, CD8VF>;

defm VCVTPS2UDQ : avx512_cvtps2dq<0x79, "vcvtps2udq", X86cvtp2UInt,
                                 X86cvtp2UIntRnd, SchedWriteCvtPS2DQ>,
                                 PS, EVEX_CD8<32, CD8VF>;

defm VCVTPD2UDQ : avx512_cvtpd2dq<0x79, "vcvtpd2udq", X86cvtp2UInt,
                                 X86cvtp2UIntRnd, SchedWriteCvtPD2DQ>, VEX_W,
                                 PS, EVEX_CD8<64, CD8VF>;

defm VCVTPD2QQ : avx512_cvtpd2qq<0x7B, "vcvtpd2qq", X86cvtp2Int,
                                 X86cvtp2IntRnd, SchedWriteCvtPD2DQ>, VEX_W,
                                 PD, EVEX_CD8<64, CD8VF>;

defm VCVTPS2QQ : avx512_cvtps2qq<0x7B, "vcvtps2qq", X86cvtp2Int,
                                 X86cvtp2IntRnd, SchedWriteCvtPS2DQ>, PD,
                                 EVEX_CD8<32, CD8VH>;

defm VCVTPD2UQQ : avx512_cvtpd2qq<0x79, "vcvtpd2uqq", X86cvtp2UInt,
                                 X86cvtp2UIntRnd, SchedWriteCvtPD2DQ>, VEX_W,
                                 PD, EVEX_CD8<64, CD8VF>;

defm VCVTPS2UQQ : avx512_cvtps2qq<0x79, "vcvtps2uqq", X86cvtp2UInt,
                                 X86cvtp2UIntRnd, SchedWriteCvtPS2DQ>, PD,
                                 EVEX_CD8<32, CD8VH>;

defm VCVTTPD2QQ : avx512_cvttpd2qq<0x7A, "vcvttpd2qq", X86cvttp2si,
                                 X86cvttp2siRnd, SchedWriteCvtPD2DQ>, VEX_W,
                                 PD, EVEX_CD8<64, CD8VF>;

defm VCVTTPS2QQ : avx512_cvttps2qq<0x7A, "vcvttps2qq", X86cvttp2si,
                                 X86cvttp2siRnd, SchedWriteCvtPS2DQ>, PD,
                                 EVEX_CD8<32, CD8VH>;

defm VCVTTPD2UQQ : avx512_cvttpd2qq<0x78, "vcvttpd2uqq", X86cvttp2ui,
                                 X86cvttp2uiRnd, SchedWriteCvtPD2DQ>, VEX_W,
                                 PD, EVEX_CD8<64, CD8VF>;

defm VCVTTPS2UQQ : avx512_cvttps2qq<0x78, "vcvttps2uqq", X86cvttp2ui,
                                 X86cvttp2uiRnd, SchedWriteCvtPS2DQ>, PD,
                                 EVEX_CD8<32, CD8VH>;

defm VCVTQQ2PD : avx512_cvtqq2pd<0xE6, "vcvtqq2pd", sint_to_fp,
                            X86VSintToFpRnd, SchedWriteCvtDQ2PD>, VEX_W, XS,
                            EVEX_CD8<64, CD8VF>;

defm VCVTUQQ2PD : avx512_cvtqq2pd<0x7A, "vcvtuqq2pd", uint_to_fp,
                            X86VUintToFpRnd, SchedWriteCvtDQ2PD>, VEX_W, XS,
                            EVEX_CD8<64, CD8VF>;

defm VCVTQQ2PS : avx512_cvtqq2ps<0x5B, "vcvtqq2ps", sint_to_fp, X86VSintToFP,
                            X86VSintToFpRnd, SchedWriteCvtDQ2PS>, VEX_W, PS,
                            EVEX_CD8<64, CD8VF>;

defm VCVTUQQ2PS : avx512_cvtqq2ps<0x7A, "vcvtuqq2ps", uint_to_fp, X86VUintToFP,
                            X86VUintToFpRnd, SchedWriteCvtDQ2PS>, VEX_W, XD,
                            EVEX_CD8<64, CD8VF>;

let Predicates = [HasAVX512] in  {
  def : Pat<(v16i32 (fp_to_sint (v16f32 VR512:$src))),
            (VCVTTPS2DQZrr VR512:$src)>;
  def : Pat<(v16i32 (fp_to_sint (loadv16f32 addr:$src))),
            (VCVTTPS2DQZrm addr:$src)>;

  def : Pat<(v16i32 (fp_to_uint (v16f32 VR512:$src))),
            (VCVTTPS2UDQZrr VR512:$src)>;
  def : Pat<(v16i32 (fp_to_uint (loadv16f32 addr:$src))),
            (VCVTTPS2UDQZrm addr:$src)>;

  def : Pat<(v8i32 (fp_to_sint (v8f64 VR512:$src))),
            (VCVTTPD2DQZrr VR512:$src)>;
  def : Pat<(v8i32 (fp_to_sint (loadv8f64 addr:$src))),
            (VCVTTPD2DQZrm addr:$src)>;

  def : Pat<(v8i32 (fp_to_uint (v8f64 VR512:$src))),
            (VCVTTPD2UDQZrr VR512:$src)>;
  def : Pat<(v8i32 (fp_to_uint (loadv8f64 addr:$src))),
            (VCVTTPD2UDQZrm addr:$src)>;
}

let Predicates = [HasVLX] in {
  def : Pat<(v4i32 (fp_to_sint (v4f32 VR128X:$src))),
            (VCVTTPS2DQZ128rr VR128X:$src)>;
  def : Pat<(v4i32 (fp_to_sint (loadv4f32 addr:$src))),
            (VCVTTPS2DQZ128rm addr:$src)>;

  def : Pat<(v4i32 (fp_to_uint (v4f32 VR128X:$src))),
            (VCVTTPS2UDQZ128rr VR128X:$src)>;
  def : Pat<(v4i32 (fp_to_uint (loadv4f32 addr:$src))),
            (VCVTTPS2UDQZ128rm addr:$src)>;

  def : Pat<(v8i32 (fp_to_sint (v8f32 VR256X:$src))),
            (VCVTTPS2DQZ256rr VR256X:$src)>;
  def : Pat<(v8i32 (fp_to_sint (loadv8f32 addr:$src))),
            (VCVTTPS2DQZ256rm addr:$src)>;

  def : Pat<(v8i32 (fp_to_uint (v8f32 VR256X:$src))),
            (VCVTTPS2UDQZ256rr VR256X:$src)>;
  def : Pat<(v8i32 (fp_to_uint (loadv8f32 addr:$src))),
            (VCVTTPS2UDQZ256rm addr:$src)>;

  def : Pat<(v4i32 (fp_to_sint (v4f64 VR256X:$src))),
            (VCVTTPD2DQZ256rr VR256X:$src)>;
  def : Pat<(v4i32 (fp_to_sint (loadv4f64 addr:$src))),
            (VCVTTPD2DQZ256rm addr:$src)>;

  def : Pat<(v4i32 (fp_to_uint (v4f64 VR256X:$src))),
            (VCVTTPD2UDQZ256rr VR256X:$src)>;
  def : Pat<(v4i32 (fp_to_uint (loadv4f64 addr:$src))),
            (VCVTTPD2UDQZ256rm addr:$src)>;

  // Special patterns to allow use of X86mcvtp2Int for masking. Instruction
  // patterns have been disabled with null_frag.
  def : Pat<(v4i32 (X86cvtp2Int (v2f64 VR128X:$src))),
            (VCVTPD2DQZ128rr VR128X:$src)>;
  def : Pat<(X86mcvtp2Int (v2f64 VR128X:$src), (v4i32 VR128X:$src0),
                          VK2WM:$mask),
            (VCVTPD2DQZ128rrk VR128X:$src0, VK2WM:$mask, VR128X:$src)>;
  def : Pat<(X86mcvtp2Int (v2f64 VR128X:$src), v4i32x_info.ImmAllZerosV,
                          VK2WM:$mask),
            (VCVTPD2DQZ128rrkz VK2WM:$mask, VR128X:$src)>;

  def : Pat<(v4i32 (X86cvtp2Int (loadv2f64 addr:$src))),
            (VCVTPD2DQZ128rm addr:$src)>;
  def : Pat<(X86mcvtp2Int (loadv2f64 addr:$src), (v4i32 VR128X:$src0),
                          VK2WM:$mask),
            (VCVTPD2DQZ128rmk VR128X:$src0, VK2WM:$mask, addr:$src)>;
  def : Pat<(X86mcvtp2Int (loadv2f64 addr:$src), v4i32x_info.ImmAllZerosV,
                          VK2WM:$mask),
            (VCVTPD2DQZ128rmkz VK2WM:$mask, addr:$src)>;

  def : Pat<(v4i32 (X86cvtp2Int (v2f64 (X86VBroadcast (loadf64 addr:$src))))),
            (VCVTPD2DQZ128rmb addr:$src)>;
  def : Pat<(X86mcvtp2Int (v2f64 (X86VBroadcast (loadf64 addr:$src))),
                          (v4i32 VR128X:$src0), VK2WM:$mask),
            (VCVTPD2DQZ128rmbk VR128X:$src0, VK2WM:$mask, addr:$src)>;
  def : Pat<(X86mcvtp2Int (v2f64 (X86VBroadcast (loadf64 addr:$src))),
                          v4i32x_info.ImmAllZerosV, VK2WM:$mask),
            (VCVTPD2DQZ128rmbkz VK2WM:$mask, addr:$src)>;

  // Special patterns to allow use of X86mcvttp2si for masking. Instruction
  // patterns have been disabled with null_frag.
  def : Pat<(v4i32 (X86cvttp2si (v2f64 VR128X:$src))),
            (VCVTTPD2DQZ128rr VR128X:$src)>;
  def : Pat<(X86mcvttp2si (v2f64 VR128X:$src), (v4i32 VR128X:$src0),
                          VK2WM:$mask),
            (VCVTTPD2DQZ128rrk VR128X:$src0, VK2WM:$mask, VR128X:$src)>;
  def : Pat<(X86mcvttp2si (v2f64 VR128X:$src), v4i32x_info.ImmAllZerosV,
                          VK2WM:$mask),
            (VCVTTPD2DQZ128rrkz VK2WM:$mask, VR128X:$src)>;

  def : Pat<(v4i32 (X86cvttp2si (loadv2f64 addr:$src))),
            (VCVTTPD2DQZ128rm addr:$src)>;
  def : Pat<(X86mcvttp2si (loadv2f64 addr:$src), (v4i32 VR128X:$src0),
                          VK2WM:$mask),
            (VCVTTPD2DQZ128rmk VR128X:$src0, VK2WM:$mask, addr:$src)>;
  def : Pat<(X86mcvttp2si (loadv2f64 addr:$src), v4i32x_info.ImmAllZerosV,
                          VK2WM:$mask),
            (VCVTTPD2DQZ128rmkz VK2WM:$mask, addr:$src)>;

  def : Pat<(v4i32 (X86cvttp2si (v2f64 (X86VBroadcast (loadf64 addr:$src))))),
            (VCVTTPD2DQZ128rmb addr:$src)>;
  def : Pat<(X86mcvttp2si (v2f64 (X86VBroadcast (loadf64 addr:$src))),
                          (v4i32 VR128X:$src0), VK2WM:$mask),
            (VCVTTPD2DQZ128rmbk VR128X:$src0, VK2WM:$mask, addr:$src)>;
  def : Pat<(X86mcvttp2si (v2f64 (X86VBroadcast (loadf64 addr:$src))),
                          v4i32x_info.ImmAllZerosV, VK2WM:$mask),
            (VCVTTPD2DQZ128rmbkz VK2WM:$mask, addr:$src)>;

  // Special patterns to allow use of X86mcvtp2UInt for masking. Instruction
  // patterns have been disabled with null_frag.
  def : Pat<(v4i32 (X86cvtp2UInt (v2f64 VR128X:$src))),
            (VCVTPD2UDQZ128rr VR128X:$src)>;
  def : Pat<(X86mcvtp2UInt (v2f64 VR128X:$src), (v4i32 VR128X:$src0),
                           VK2WM:$mask),
            (VCVTPD2UDQZ128rrk VR128X:$src0, VK2WM:$mask, VR128X:$src)>;
  def : Pat<(X86mcvtp2UInt (v2f64 VR128X:$src), v4i32x_info.ImmAllZerosV,
                           VK2WM:$mask),
            (VCVTPD2UDQZ128rrkz VK2WM:$mask, VR128X:$src)>;

  def : Pat<(v4i32 (X86cvtp2UInt (loadv2f64 addr:$src))),
            (VCVTPD2UDQZ128rm addr:$src)>;
  def : Pat<(X86mcvtp2UInt (loadv2f64 addr:$src), (v4i32 VR128X:$src0),
                           VK2WM:$mask),
            (VCVTPD2UDQZ128rmk VR128X:$src0, VK2WM:$mask, addr:$src)>;
  def : Pat<(X86mcvtp2UInt (loadv2f64 addr:$src), v4i32x_info.ImmAllZerosV,
                           VK2WM:$mask),
            (VCVTPD2UDQZ128rmkz VK2WM:$mask, addr:$src)>;

  def : Pat<(v4i32 (X86cvtp2UInt (v2f64 (X86VBroadcast (loadf64 addr:$src))))),
            (VCVTPD2UDQZ128rmb addr:$src)>;
  def : Pat<(X86mcvtp2UInt (v2f64 (X86VBroadcast (loadf64 addr:$src))),
                           (v4i32 VR128X:$src0), VK2WM:$mask),
            (VCVTPD2UDQZ128rmbk VR128X:$src0, VK2WM:$mask, addr:$src)>;
  def : Pat<(X86mcvtp2UInt (v2f64 (X86VBroadcast (loadf64 addr:$src))),
                           v4i32x_info.ImmAllZerosV, VK2WM:$mask),
            (VCVTPD2UDQZ128rmbkz VK2WM:$mask, addr:$src)>;

  // Special patterns to allow use of X86mcvtp2UInt for masking. Instruction
  // patterns have been disabled with null_frag.
  def : Pat<(v4i32 (X86cvttp2ui (v2f64 VR128X:$src))),
            (VCVTTPD2UDQZ128rr VR128X:$src)>;
  def : Pat<(X86mcvttp2ui (v2f64 VR128X:$src), (v4i32 VR128X:$src0),
                          VK2WM:$mask),
            (VCVTTPD2UDQZ128rrk VR128X:$src0, VK2WM:$mask, VR128X:$src)>;
  def : Pat<(X86mcvttp2ui (v2f64 VR128X:$src), v4i32x_info.ImmAllZerosV,
                          VK2WM:$mask),
            (VCVTTPD2UDQZ128rrkz VK2WM:$mask, VR128X:$src)>;

  def : Pat<(v4i32 (X86cvttp2ui (loadv2f64 addr:$src))),
            (VCVTTPD2UDQZ128rm addr:$src)>;
  def : Pat<(X86mcvttp2ui (loadv2f64 addr:$src), (v4i32 VR128X:$src0),
                          VK2WM:$mask),
            (VCVTTPD2UDQZ128rmk VR128X:$src0, VK2WM:$mask, addr:$src)>;
  def : Pat<(X86mcvttp2ui (loadv2f64 addr:$src), v4i32x_info.ImmAllZerosV,
                          VK2WM:$mask),
            (VCVTTPD2UDQZ128rmkz VK2WM:$mask, addr:$src)>;

  def : Pat<(v4i32 (X86cvttp2ui (v2f64 (X86VBroadcast (loadf64 addr:$src))))),
            (VCVTTPD2UDQZ128rmb addr:$src)>;
  def : Pat<(X86mcvttp2ui (v2f64 (X86VBroadcast (loadf64 addr:$src))),
                          (v4i32 VR128X:$src0), VK2WM:$mask),
            (VCVTTPD2UDQZ128rmbk VR128X:$src0, VK2WM:$mask, addr:$src)>;
  def : Pat<(X86mcvttp2ui (v2f64 (X86VBroadcast (loadf64 addr:$src))),
                          v4i32x_info.ImmAllZerosV, VK2WM:$mask),
            (VCVTTPD2UDQZ128rmbkz VK2WM:$mask, addr:$src)>;
}

let Predicates = [HasDQI] in {
  def : Pat<(v8i64 (fp_to_sint (v8f32 VR256X:$src))),
            (VCVTTPS2QQZrr VR256X:$src)>;
  def : Pat<(v8i64 (fp_to_sint (loadv8f32 addr:$src))),
            (VCVTTPS2QQZrm addr:$src)>;

  def : Pat<(v8i64 (fp_to_uint (v8f32 VR256X:$src))),
            (VCVTTPS2UQQZrr VR256X:$src)>;
  def : Pat<(v8i64 (fp_to_uint (loadv8f32 addr:$src))),
            (VCVTTPS2UQQZrm addr:$src)>;

  def : Pat<(v8i64 (fp_to_sint (v8f64 VR512:$src))),
            (VCVTTPD2QQZrr VR512:$src)>;
  def : Pat<(v8i64 (fp_to_sint (loadv8f64 addr:$src))),
            (VCVTTPD2QQZrm addr:$src)>;

  def : Pat<(v8i64 (fp_to_uint (v8f64 VR512:$src))),
            (VCVTTPD2UQQZrr VR512:$src)>;
  def : Pat<(v8i64 (fp_to_uint (loadv8f64 addr:$src))),
            (VCVTTPD2UQQZrm addr:$src)>;
}

let Predicates = [HasDQI, HasVLX] in {
  def : Pat<(v4i64 (fp_to_sint (v4f32 VR128X:$src))),
            (VCVTTPS2QQZ256rr VR128X:$src)>;
  def : Pat<(v4i64 (fp_to_sint (loadv4f32 addr:$src))),
            (VCVTTPS2QQZ256rm addr:$src)>;

  def : Pat<(v4i64 (fp_to_uint (v4f32 VR128X:$src))),
            (VCVTTPS2UQQZ256rr VR128X:$src)>;
  def : Pat<(v4i64 (fp_to_uint (loadv4f32 addr:$src))),
            (VCVTTPS2UQQZ256rm addr:$src)>;

  def : Pat<(v2i64 (fp_to_sint (v2f64 VR128X:$src))),
            (VCVTTPD2QQZ128rr VR128X:$src)>;
  def : Pat<(v2i64 (fp_to_sint (loadv2f64 addr:$src))),
            (VCVTTPD2QQZ128rm addr:$src)>;

  def : Pat<(v2i64 (fp_to_uint (v2f64 VR128X:$src))),
            (VCVTTPD2UQQZ128rr VR128X:$src)>;
  def : Pat<(v2i64 (fp_to_uint (loadv2f64 addr:$src))),
            (VCVTTPD2UQQZ128rm addr:$src)>;

  def : Pat<(v4i64 (fp_to_sint (v4f64 VR256X:$src))),
            (VCVTTPD2QQZ256rr VR256X:$src)>;
  def : Pat<(v4i64 (fp_to_sint (loadv4f64 addr:$src))),
            (VCVTTPD2QQZ256rm addr:$src)>;

  def : Pat<(v4i64 (fp_to_uint (v4f64 VR256X:$src))),
            (VCVTTPD2UQQZ256rr VR256X:$src)>;
  def : Pat<(v4i64 (fp_to_uint (loadv4f64 addr:$src))),
            (VCVTTPD2UQQZ256rm addr:$src)>;
}

let Predicates = [HasAVX512, NoVLX] in {
def : Pat<(v8i32 (fp_to_uint (v8f32 VR256X:$src1))),
          (EXTRACT_SUBREG (v16i32 (VCVTTPS2UDQZrr
           (v16f32 (INSERT_SUBREG (IMPLICIT_DEF),
                                  VR256X:$src1, sub_ymm)))), sub_ymm)>;

def : Pat<(v4i32 (fp_to_uint (v4f32 VR128X:$src1))),
          (EXTRACT_SUBREG (v16i32 (VCVTTPS2UDQZrr
           (v16f32 (INSERT_SUBREG (IMPLICIT_DEF),
                                  VR128X:$src1, sub_xmm)))), sub_xmm)>;

def : Pat<(v4i32 (fp_to_uint (v4f64 VR256X:$src1))),
          (EXTRACT_SUBREG (v8i32 (VCVTTPD2UDQZrr
           (v8f64 (INSERT_SUBREG (IMPLICIT_DEF),
                                 VR256X:$src1, sub_ymm)))), sub_xmm)>;

def : Pat<(v8f32 (uint_to_fp (v8i32 VR256X:$src1))),
          (EXTRACT_SUBREG (v16f32 (VCVTUDQ2PSZrr
           (v16i32 (INSERT_SUBREG (IMPLICIT_DEF),
                                  VR256X:$src1, sub_ymm)))), sub_ymm)>;

def : Pat<(v4f32 (uint_to_fp (v4i32 VR128X:$src1))),
          (EXTRACT_SUBREG (v16f32 (VCVTUDQ2PSZrr
           (v16i32 (INSERT_SUBREG (IMPLICIT_DEF),
                                  VR128X:$src1, sub_xmm)))), sub_xmm)>;

def : Pat<(v4f64 (uint_to_fp (v4i32 VR128X:$src1))),
          (EXTRACT_SUBREG (v8f64 (VCVTUDQ2PDZrr
           (v8i32 (INSERT_SUBREG (IMPLICIT_DEF),
                                 VR128X:$src1, sub_xmm)))), sub_ymm)>;

def : Pat<(v2f64 (X86VUintToFP (v4i32 VR128X:$src1))),
          (EXTRACT_SUBREG (v8f64 (VCVTUDQ2PDZrr
           (v8i32 (INSERT_SUBREG (IMPLICIT_DEF),
                                 VR128X:$src1, sub_xmm)))), sub_xmm)>;
}

let Predicates = [HasAVX512, HasVLX] in {
  def : Pat<(X86vzmovl (v2i64 (bitconvert
                              (v4i32 (X86cvtp2Int (v2f64 VR128X:$src)))))),
            (VCVTPD2DQZ128rr VR128X:$src)>;
  def : Pat<(X86vzmovl (v2i64 (bitconvert
                              (v4i32 (X86cvtp2Int (loadv2f64 addr:$src)))))),
            (VCVTPD2DQZ128rm addr:$src)>;
  def : Pat<(X86vzmovl (v2i64 (bitconvert
                               (v4i32 (X86cvtp2UInt (v2f64 VR128X:$src)))))),
            (VCVTPD2UDQZ128rr VR128X:$src)>;
  def : Pat<(X86vzmovl (v2i64 (bitconvert
                              (v4i32 (X86cvttp2si (v2f64 VR128X:$src)))))),
            (VCVTTPD2DQZ128rr VR128X:$src)>;
  def : Pat<(X86vzmovl (v2i64 (bitconvert
                              (v4i32 (X86cvttp2si (loadv2f64 addr:$src)))))),
            (VCVTTPD2DQZ128rm addr:$src)>;
  def : Pat<(X86vzmovl (v2i64 (bitconvert
                               (v4i32 (X86cvttp2ui (v2f64 VR128X:$src)))))),
            (VCVTTPD2UDQZ128rr VR128X:$src)>;

  def : Pat<(v2f64 (X86VSintToFP (bc_v4i32 (v2i64 (scalar_to_vector (loadi64 addr:$src)))))),
            (VCVTDQ2PDZ128rm addr:$src)>;
  def : Pat<(v2f64 (X86VSintToFP (bc_v4i32 (v2i64 (X86vzload addr:$src))))),
            (VCVTDQ2PDZ128rm addr:$src)>;

  def : Pat<(v2f64 (X86VUintToFP (bc_v4i32 (v2i64 (scalar_to_vector (loadi64 addr:$src)))))),
            (VCVTUDQ2PDZ128rm addr:$src)>;
  def : Pat<(v2f64 (X86VUintToFP (bc_v4i32 (v2i64 (X86vzload addr:$src))))),
            (VCVTUDQ2PDZ128rm addr:$src)>;
}

let Predicates = [HasAVX512] in {
  def : Pat<(v8f32 (fpround (loadv8f64 addr:$src))),
            (VCVTPD2PSZrm addr:$src)>;
  def : Pat<(v8f64 (extloadv8f32 addr:$src)),
            (VCVTPS2PDZrm addr:$src)>;
}

let Predicates = [HasDQI, HasVLX] in {
  def : Pat<(X86vzmovl (v2f64 (bitconvert
                              (v4f32 (X86VSintToFP (v2i64 VR128X:$src)))))),
            (VCVTQQ2PSZ128rr VR128X:$src)>;
  def : Pat<(X86vzmovl (v2f64 (bitconvert
                              (v4f32 (X86VUintToFP (v2i64 VR128X:$src)))))),
            (VCVTUQQ2PSZ128rr VR128X:$src)>;
}

let Predicates = [HasDQI, NoVLX] in {
def : Pat<(v2i64 (fp_to_sint (v2f64 VR128X:$src1))),
          (EXTRACT_SUBREG (v8i64 (VCVTTPD2QQZrr
           (v8f64 (INSERT_SUBREG (IMPLICIT_DEF),
                                  VR128X:$src1, sub_xmm)))), sub_xmm)>;

def : Pat<(v4i64 (fp_to_sint (v4f32 VR128X:$src1))),
          (EXTRACT_SUBREG (v8i64 (VCVTTPS2QQZrr
           (v8f32 (INSERT_SUBREG (IMPLICIT_DEF),
                                  VR128X:$src1, sub_xmm)))), sub_ymm)>;

def : Pat<(v4i64 (fp_to_sint (v4f64 VR256X:$src1))),
          (EXTRACT_SUBREG (v8i64 (VCVTTPD2QQZrr
           (v8f64 (INSERT_SUBREG (IMPLICIT_DEF),
                                  VR256X:$src1, sub_ymm)))), sub_ymm)>;

def : Pat<(v2i64 (fp_to_uint (v2f64 VR128X:$src1))),
          (EXTRACT_SUBREG (v8i64 (VCVTTPD2UQQZrr
           (v8f64 (INSERT_SUBREG (IMPLICIT_DEF),
                                  VR128X:$src1, sub_xmm)))), sub_xmm)>;

def : Pat<(v4i64 (fp_to_uint (v4f32 VR128X:$src1))),
          (EXTRACT_SUBREG (v8i64 (VCVTTPS2UQQZrr
           (v8f32 (INSERT_SUBREG (IMPLICIT_DEF),
                                  VR128X:$src1, sub_xmm)))), sub_ymm)>;

def : Pat<(v4i64 (fp_to_uint (v4f64 VR256X:$src1))),
          (EXTRACT_SUBREG (v8i64 (VCVTTPD2UQQZrr
           (v8f64 (INSERT_SUBREG (IMPLICIT_DEF),
                                  VR256X:$src1, sub_ymm)))), sub_ymm)>;

def : Pat<(v4f32 (sint_to_fp (v4i64 VR256X:$src1))),
          (EXTRACT_SUBREG (v8f32 (VCVTQQ2PSZrr
           (v8i64 (INSERT_SUBREG (IMPLICIT_DEF),
                                  VR256X:$src1, sub_ymm)))), sub_xmm)>;

def : Pat<(v2f64 (sint_to_fp (v2i64 VR128X:$src1))),
          (EXTRACT_SUBREG (v8f64 (VCVTQQ2PDZrr
           (v8i64 (INSERT_SUBREG (IMPLICIT_DEF),
                                  VR128X:$src1, sub_xmm)))), sub_xmm)>;

def : Pat<(v4f64 (sint_to_fp (v4i64 VR256X:$src1))),
          (EXTRACT_SUBREG (v8f64 (VCVTQQ2PDZrr
           (v8i64 (INSERT_SUBREG (IMPLICIT_DEF),
                                  VR256X:$src1, sub_ymm)))), sub_ymm)>;

def : Pat<(v4f32 (uint_to_fp (v4i64 VR256X:$src1))),
          (EXTRACT_SUBREG (v8f32 (VCVTUQQ2PSZrr
           (v8i64 (INSERT_SUBREG (IMPLICIT_DEF),
                                  VR256X:$src1, sub_ymm)))), sub_xmm)>;

def : Pat<(v2f64 (uint_to_fp (v2i64 VR128X:$src1))),
          (EXTRACT_SUBREG (v8f64 (VCVTUQQ2PDZrr
           (v8i64 (INSERT_SUBREG (IMPLICIT_DEF),
                                  VR128X:$src1, sub_xmm)))), sub_xmm)>;

def : Pat<(v4f64 (uint_to_fp (v4i64 VR256X:$src1))),
          (EXTRACT_SUBREG (v8f64 (VCVTUQQ2PDZrr
           (v8i64 (INSERT_SUBREG (IMPLICIT_DEF),
                                  VR256X:$src1, sub_ymm)))), sub_ymm)>;
}

//===----------------------------------------------------------------------===//
// Half precision conversion instructions
//===----------------------------------------------------------------------===//

multiclass avx512_cvtph2ps<X86VectorVTInfo _dest, X86VectorVTInfo _src,
                           X86MemOperand x86memop, PatFrag ld_frag,
                           X86FoldableSchedWrite sched> {
  defm rr : AVX512_maskable<0x13, MRMSrcReg, _dest ,(outs _dest.RC:$dst),
                            (ins _src.RC:$src), "vcvtph2ps", "$src", "$src",
                            (X86cvtph2ps (_src.VT _src.RC:$src))>,
                            T8PD, Sched<[sched]>;
  defm rm : AVX512_maskable<0x13, MRMSrcMem, _dest, (outs _dest.RC:$dst),
                            (ins x86memop:$src), "vcvtph2ps", "$src", "$src",
                            (X86cvtph2ps (_src.VT
                                          (ld_frag addr:$src)))>,
                            T8PD, Sched<[sched.Folded]>;
}

multiclass avx512_cvtph2ps_sae<X86VectorVTInfo _dest, X86VectorVTInfo _src,
                               X86FoldableSchedWrite sched> {
  defm rrb : AVX512_maskable<0x13, MRMSrcReg, _dest, (outs _dest.RC:$dst),
                             (ins _src.RC:$src), "vcvtph2ps",
                             "{sae}, $src", "$src, {sae}",
                             (X86cvtph2psRnd (_src.VT _src.RC:$src),
                                             (i32 FROUND_NO_EXC))>,
                             T8PD, EVEX_B, Sched<[sched]>;
}

let Predicates = [HasAVX512] in
  defm VCVTPH2PSZ : avx512_cvtph2ps<v16f32_info, v16i16x_info, f256mem, load,
                                    WriteCvtPH2PSZ>,
                    avx512_cvtph2ps_sae<v16f32_info, v16i16x_info, WriteCvtPH2PSZ>,
                    EVEX, EVEX_V512, EVEX_CD8<32, CD8VH>;

let Predicates = [HasVLX] in {
  defm VCVTPH2PSZ256 : avx512_cvtph2ps<v8f32x_info, v8i16x_info, f128mem,
                       load, WriteCvtPH2PSY>, EVEX, EVEX_V256,
                       EVEX_CD8<32, CD8VH>;
  defm VCVTPH2PSZ128 : avx512_cvtph2ps<v4f32x_info, v8i16x_info, f64mem,
                       load, WriteCvtPH2PS>, EVEX, EVEX_V128,
                       EVEX_CD8<32, CD8VH>;

  // Pattern match vcvtph2ps of a scalar i64 load.
  def : Pat<(v4f32 (X86cvtph2ps (v8i16 (vzmovl_v2i64 addr:$src)))),
            (VCVTPH2PSZ128rm addr:$src)>;
  def : Pat<(v4f32 (X86cvtph2ps (v8i16 (vzload_v2i64 addr:$src)))),
            (VCVTPH2PSZ128rm addr:$src)>;
  def : Pat<(v4f32 (X86cvtph2ps (v8i16 (bitconvert
              (v2i64 (scalar_to_vector (loadi64 addr:$src))))))),
            (VCVTPH2PSZ128rm addr:$src)>;
}

multiclass avx512_cvtps2ph<X86VectorVTInfo _dest, X86VectorVTInfo _src,
                           X86MemOperand x86memop, SchedWrite RR, SchedWrite MR> {
let ExeDomain = GenericDomain in {
  def rr : AVX512AIi8<0x1D, MRMDestReg, (outs _dest.RC:$dst),
             (ins _src.RC:$src1, i32u8imm:$src2),
             "vcvtps2ph\t{$src2, $src1, $dst|$dst, $src1, $src2}",
             [(set _dest.RC:$dst,
                   (X86cvtps2ph (_src.VT _src.RC:$src1), (i32 imm:$src2)))]>,
             Sched<[RR]>;
  let Constraints = "$src0 = $dst" in
  def rrk : AVX512AIi8<0x1D, MRMDestReg, (outs _dest.RC:$dst),
             (ins _dest.RC:$src0, _src.KRCWM:$mask, _src.RC:$src1, i32u8imm:$src2),
             "vcvtps2ph\t{$src2, $src1, $dst {${mask}}|$dst {${mask}}, $src1, $src2}",
             [(set _dest.RC:$dst,
                   (X86mcvtps2ph (_src.VT _src.RC:$src1), (i32 imm:$src2),
                                 _dest.RC:$src0, _src.KRCWM:$mask))]>,
             Sched<[RR]>, EVEX_K;
  def rrkz : AVX512AIi8<0x1D, MRMDestReg, (outs _dest.RC:$dst),
             (ins _src.KRCWM:$mask, _src.RC:$src1, i32u8imm:$src2),
             "vcvtps2ph\t{$src2, $src1, $dst {${mask}} {z}|$dst {${mask}} {z}, $src1, $src2}",
             [(set _dest.RC:$dst,
                   (X86mcvtps2ph (_src.VT _src.RC:$src1), (i32 imm:$src2),
                                 _dest.ImmAllZerosV, _src.KRCWM:$mask))]>,
             Sched<[RR]>, EVEX_KZ;
  let hasSideEffects = 0, mayStore = 1 in {
    def mr : AVX512AIi8<0x1D, MRMDestMem, (outs),
               (ins x86memop:$dst, _src.RC:$src1, i32u8imm:$src2),
               "vcvtps2ph\t{$src2, $src1, $dst|$dst, $src1, $src2}", []>,
               Sched<[MR]>;
    def mrk : AVX512AIi8<0x1D, MRMDestMem, (outs),
               (ins x86memop:$dst, _dest.KRCWM:$mask, _src.RC:$src1, i32u8imm:$src2),
               "vcvtps2ph\t{$src2, $src1, $dst {${mask}}|$dst {${mask}}, $src1, $src2}", []>,
                EVEX_K, Sched<[MR]>, NotMemoryFoldable;
  }
}
}

multiclass avx512_cvtps2ph_sae<X86VectorVTInfo _dest, X86VectorVTInfo _src,
                               SchedWrite Sched> {
  let hasSideEffects = 0 in
  defm rrb : AVX512_maskable_in_asm<0x1D, MRMDestReg, _dest,
                   (outs _dest.RC:$dst),
                   (ins _src.RC:$src1, i32u8imm:$src2),
                   "vcvtps2ph", "$src2, {sae}, $src1", "$src1, {sae}, $src2", []>,
                   EVEX_B, AVX512AIi8Base, Sched<[Sched]>;
}

let Predicates = [HasAVX512] in {
  defm VCVTPS2PHZ : avx512_cvtps2ph<v16i16x_info, v16f32_info, f256mem,
                                    WriteCvtPS2PHZ, WriteCvtPS2PHZSt>,
                    avx512_cvtps2ph_sae<v16i16x_info, v16f32_info, WriteCvtPS2PHZ>,
                                        EVEX, EVEX_V512, EVEX_CD8<32, CD8VH>;
  let Predicates = [HasVLX] in {
    defm VCVTPS2PHZ256 : avx512_cvtps2ph<v8i16x_info, v8f32x_info, f128mem,
                                         WriteCvtPS2PHY, WriteCvtPS2PHYSt>,
                                         EVEX, EVEX_V256, EVEX_CD8<32, CD8VH>;
    defm VCVTPS2PHZ128 : avx512_cvtps2ph<v8i16x_info, v4f32x_info, f64mem,
                                         WriteCvtPS2PH, WriteCvtPS2PHSt>,
                                         EVEX, EVEX_V128, EVEX_CD8<32, CD8VH>;
  }

  def : Pat<(store (f64 (extractelt
                         (bc_v2f64 (v8i16 (X86cvtps2ph VR128X:$src1, i32:$src2))),
                         (iPTR 0))), addr:$dst),
            (VCVTPS2PHZ128mr addr:$dst, VR128X:$src1, imm:$src2)>;
  def : Pat<(store (i64 (extractelt
                         (bc_v2i64 (v8i16 (X86cvtps2ph VR128X:$src1, i32:$src2))),
                         (iPTR 0))), addr:$dst),
            (VCVTPS2PHZ128mr addr:$dst, VR128X:$src1, imm:$src2)>;
  def : Pat<(store (v8i16 (X86cvtps2ph VR256X:$src1, i32:$src2)), addr:$dst),
            (VCVTPS2PHZ256mr addr:$dst, VR256X:$src1, imm:$src2)>;
  def : Pat<(store (v16i16 (X86cvtps2ph VR512:$src1, i32:$src2)), addr:$dst),
            (VCVTPS2PHZmr addr:$dst, VR512:$src1, imm:$src2)>;
}

// Patterns for matching conversions from float to half-float and vice versa.
let Predicates = [HasVLX] in {
  // Use MXCSR.RC for rounding instead of explicitly specifying the default
  // rounding mode (Nearest-Even, encoded as 0). Both are equivalent in the
  // configurations we support (the default). However, falling back to MXCSR is
  // more consistent with other instructions, which are always controlled by it.
  // It's encoded as 0b100.
  def : Pat<(fp_to_f16 FR32X:$src),
            (i16 (EXTRACT_SUBREG (VMOVPDI2DIZrr (v8i16 (VCVTPS2PHZ128rr
              (v4f32 (COPY_TO_REGCLASS FR32X:$src, VR128X)), 4))), sub_16bit))>;

  def : Pat<(f16_to_fp GR16:$src),
            (f32 (COPY_TO_REGCLASS (v4f32 (VCVTPH2PSZ128rr
              (v8i16 (COPY_TO_REGCLASS (MOVSX32rr16 GR16:$src), VR128X)))), FR32X)) >;

  def : Pat<(f16_to_fp (i16 (fp_to_f16 FR32X:$src))),
            (f32 (COPY_TO_REGCLASS (v4f32 (VCVTPH2PSZ128rr
              (v8i16 (VCVTPS2PHZ128rr
               (v4f32 (COPY_TO_REGCLASS FR32X:$src, VR128X)), 4)))), FR32X)) >;
}

//  Unordered/Ordered scalar fp compare with Sae and set EFLAGS
multiclass avx512_ord_cmp_sae<bits<8> opc, X86VectorVTInfo _,
                            string OpcodeStr, X86FoldableSchedWrite sched> {
  let hasSideEffects = 0 in
  def rrb: AVX512<opc, MRMSrcReg, (outs), (ins _.RC:$src1, _.RC:$src2),
                  !strconcat(OpcodeStr, "\t{{sae}, $src2, $src1|$src1, $src2, {sae}}"), []>,
                  EVEX, EVEX_B, VEX_LIG, EVEX_V128, Sched<[sched]>;
}

let Defs = [EFLAGS], Predicates = [HasAVX512] in {
  defm VUCOMISSZ : avx512_ord_cmp_sae<0x2E, v4f32x_info, "vucomiss", WriteFCom>,
                                   AVX512PSIi8Base, EVEX_CD8<32, CD8VT1>;
  defm VUCOMISDZ : avx512_ord_cmp_sae<0x2E, v2f64x_info, "vucomisd", WriteFCom>,
                                   AVX512PDIi8Base, VEX_W, EVEX_CD8<64, CD8VT1>;
  defm VCOMISSZ : avx512_ord_cmp_sae<0x2F, v4f32x_info, "vcomiss", WriteFCom>,
                                   AVX512PSIi8Base, EVEX_CD8<32, CD8VT1>;
  defm VCOMISDZ : avx512_ord_cmp_sae<0x2F, v2f64x_info, "vcomisd", WriteFCom>,
                                   AVX512PDIi8Base, VEX_W, EVEX_CD8<64, CD8VT1>;
}

let Defs = [EFLAGS], Predicates = [HasAVX512] in {
  defm VUCOMISSZ : sse12_ord_cmp<0x2E, FR32X, X86cmp, f32, f32mem, loadf32,
                                 "ucomiss", WriteFCom>, PS, EVEX, VEX_LIG,
                                 EVEX_CD8<32, CD8VT1>;
  defm VUCOMISDZ : sse12_ord_cmp<0x2E, FR64X, X86cmp, f64, f64mem, loadf64,
                                  "ucomisd", WriteFCom>, PD, EVEX,
                                  VEX_LIG, VEX_W, EVEX_CD8<64, CD8VT1>;
  let Pattern = []<dag> in {
    defm VCOMISSZ  : sse12_ord_cmp<0x2F, FR32X, undef, f32, f32mem, loadf32,
                                   "comiss", WriteFCom>, PS, EVEX, VEX_LIG,
                                   EVEX_CD8<32, CD8VT1>;
    defm VCOMISDZ  : sse12_ord_cmp<0x2F, FR64X, undef, f64, f64mem, loadf64,
                                   "comisd", WriteFCom>, PD, EVEX,
                                    VEX_LIG, VEX_W, EVEX_CD8<64, CD8VT1>;
  }
  let isCodeGenOnly = 1 in {
    defm VUCOMISSZ  : sse12_ord_cmp_int<0x2E, VR128X, X86ucomi, v4f32, ssmem,
                          sse_load_f32, "ucomiss", WriteFCom>, PS, EVEX, VEX_LIG,
                          EVEX_CD8<32, CD8VT1>;
    defm VUCOMISDZ  : sse12_ord_cmp_int<0x2E, VR128X, X86ucomi, v2f64, sdmem,
                          sse_load_f64, "ucomisd", WriteFCom>, PD, EVEX,
                          VEX_LIG, VEX_W, EVEX_CD8<64, CD8VT1>;

    defm VCOMISSZ  : sse12_ord_cmp_int<0x2F, VR128X, X86comi, v4f32, ssmem,
                          sse_load_f32, "comiss", WriteFCom>, PS, EVEX, VEX_LIG,
                          EVEX_CD8<32, CD8VT1>;
    defm VCOMISDZ  : sse12_ord_cmp_int<0x2F, VR128X, X86comi, v2f64, sdmem,
                          sse_load_f64, "comisd", WriteFCom>, PD, EVEX,
                          VEX_LIG, VEX_W, EVEX_CD8<64, CD8VT1>;
  }
}

/// avx512_fp14_s rcp14ss, rcp14sd, rsqrt14ss, rsqrt14sd
multiclass avx512_fp14_s<bits<8> opc, string OpcodeStr, SDNode OpNode,
                         X86FoldableSchedWrite sched, X86VectorVTInfo _> {
  let Predicates = [HasAVX512], ExeDomain = _.ExeDomain in {
  defm rr : AVX512_maskable_scalar<opc, MRMSrcReg, _, (outs _.RC:$dst),
                           (ins _.RC:$src1, _.RC:$src2), OpcodeStr,
                           "$src2, $src1", "$src1, $src2",
                           (OpNode (_.VT _.RC:$src1), (_.VT _.RC:$src2))>,
                           EVEX_4V, Sched<[sched]>;
  defm rm : AVX512_maskable_scalar<opc, MRMSrcMem, _, (outs _.RC:$dst),
                         (ins _.RC:$src1, _.IntScalarMemOp:$src2), OpcodeStr,
                         "$src2, $src1", "$src1, $src2",
                         (OpNode (_.VT _.RC:$src1),
                          _.ScalarIntMemCPat:$src2)>, EVEX_4V,
                          Sched<[sched.Folded, sched.ReadAfterFold]>;
}
}

defm VRCP14SSZ : avx512_fp14_s<0x4D, "vrcp14ss", X86rcp14s, SchedWriteFRcp.Scl,
                               f32x_info>, EVEX_CD8<32, CD8VT1>,
                               T8PD;
defm VRCP14SDZ : avx512_fp14_s<0x4D, "vrcp14sd", X86rcp14s, SchedWriteFRcp.Scl,
                               f64x_info>, VEX_W, EVEX_CD8<64, CD8VT1>,
                               T8PD;
defm VRSQRT14SSZ : avx512_fp14_s<0x4F, "vrsqrt14ss", X86rsqrt14s,
                                 SchedWriteFRsqrt.Scl, f32x_info>,
                                 EVEX_CD8<32, CD8VT1>, T8PD;
defm VRSQRT14SDZ : avx512_fp14_s<0x4F, "vrsqrt14sd", X86rsqrt14s,
                                 SchedWriteFRsqrt.Scl, f64x_info>, VEX_W,
                                 EVEX_CD8<64, CD8VT1>, T8PD;

/// avx512_fp14_p rcp14ps, rcp14pd, rsqrt14ps, rsqrt14pd
multiclass avx512_fp14_p<bits<8> opc, string OpcodeStr, SDNode OpNode,
                         X86FoldableSchedWrite sched, X86VectorVTInfo _> {
  let ExeDomain = _.ExeDomain in {
  defm r: AVX512_maskable<opc, MRMSrcReg, _, (outs _.RC:$dst),
                         (ins _.RC:$src), OpcodeStr, "$src", "$src",
                         (_.VT (OpNode _.RC:$src))>, EVEX, T8PD,
                         Sched<[sched]>;
  defm m: AVX512_maskable<opc, MRMSrcMem, _, (outs _.RC:$dst),
                         (ins _.MemOp:$src), OpcodeStr, "$src", "$src",
                         (OpNode (_.VT
                           (bitconvert (_.LdFrag addr:$src))))>, EVEX, T8PD,
                         Sched<[sched.Folded, sched.ReadAfterFold]>;
  defm mb: AVX512_maskable<opc, MRMSrcMem, _, (outs _.RC:$dst),
                          (ins _.ScalarMemOp:$src), OpcodeStr,
                          "${src}"##_.BroadcastStr, "${src}"##_.BroadcastStr,
                          (OpNode (_.VT
                            (X86VBroadcast (_.ScalarLdFrag addr:$src))))>,
                          EVEX, T8PD, EVEX_B, Sched<[sched.Folded, sched.ReadAfterFold]>;
  }
}

multiclass avx512_fp14_p_vl_all<bits<8> opc, string OpcodeStr, SDNode OpNode,
                                X86SchedWriteWidths sched> {
  defm PSZ : avx512_fp14_p<opc, !strconcat(OpcodeStr, "ps"), OpNode, sched.ZMM,
                           v16f32_info>, EVEX_V512, EVEX_CD8<32, CD8VF>;
  defm PDZ : avx512_fp14_p<opc, !strconcat(OpcodeStr, "pd"), OpNode, sched.ZMM,
                           v8f64_info>, EVEX_V512, VEX_W, EVEX_CD8<64, CD8VF>;

  // Define only if AVX512VL feature is present.
  let Predicates = [HasVLX] in {
    defm PSZ128 : avx512_fp14_p<opc, !strconcat(OpcodeStr, "ps"),
                                OpNode, sched.XMM, v4f32x_info>,
                               EVEX_V128, EVEX_CD8<32, CD8VF>;
    defm PSZ256 : avx512_fp14_p<opc, !strconcat(OpcodeStr, "ps"),
                                OpNode, sched.YMM, v8f32x_info>,
                               EVEX_V256, EVEX_CD8<32, CD8VF>;
    defm PDZ128 : avx512_fp14_p<opc, !strconcat(OpcodeStr, "pd"),
                                OpNode, sched.XMM, v2f64x_info>,
                               EVEX_V128, VEX_W, EVEX_CD8<64, CD8VF>;
    defm PDZ256 : avx512_fp14_p<opc, !strconcat(OpcodeStr, "pd"),
                                OpNode, sched.YMM, v4f64x_info>,
                               EVEX_V256, VEX_W, EVEX_CD8<64, CD8VF>;
  }
}

defm VRSQRT14 : avx512_fp14_p_vl_all<0x4E, "vrsqrt14", X86rsqrt14, SchedWriteFRsqrt>;
defm VRCP14 : avx512_fp14_p_vl_all<0x4C, "vrcp14", X86rcp14, SchedWriteFRcp>;

/// avx512_fp28_s rcp28ss, rcp28sd, rsqrt28ss, rsqrt28sd
multiclass avx512_fp28_s<bits<8> opc, string OpcodeStr,X86VectorVTInfo _,
                         SDNode OpNode, X86FoldableSchedWrite sched> {
  let ExeDomain = _.ExeDomain in {
  defm r : AVX512_maskable_scalar<opc, MRMSrcReg, _, (outs _.RC:$dst),
                           (ins _.RC:$src1, _.RC:$src2), OpcodeStr,
                           "$src2, $src1", "$src1, $src2",
                           (OpNode (_.VT _.RC:$src1), (_.VT _.RC:$src2),
                           (i32 FROUND_CURRENT))>,
                           Sched<[sched]>;

  defm rb : AVX512_maskable_scalar<opc, MRMSrcReg, _, (outs _.RC:$dst),
                            (ins _.RC:$src1, _.RC:$src2), OpcodeStr,
                            "{sae}, $src2, $src1", "$src1, $src2, {sae}",
                            (OpNode (_.VT _.RC:$src1), (_.VT _.RC:$src2),
                            (i32 FROUND_NO_EXC))>, EVEX_B,
                            Sched<[sched]>;

  defm m : AVX512_maskable_scalar<opc, MRMSrcMem, _, (outs _.RC:$dst),
                         (ins _.RC:$src1, _.IntScalarMemOp:$src2), OpcodeStr,
                         "$src2, $src1", "$src1, $src2",
                         (OpNode (_.VT _.RC:$src1), _.ScalarIntMemCPat:$src2,
                         (i32 FROUND_CURRENT))>,
                         Sched<[sched.Folded, sched.ReadAfterFold]>;
  }
}

multiclass avx512_eri_s<bits<8> opc, string OpcodeStr, SDNode OpNode,
                        X86FoldableSchedWrite sched> {
  defm SSZ : avx512_fp28_s<opc, OpcodeStr#"ss", f32x_info, OpNode, sched>,
               EVEX_CD8<32, CD8VT1>;
  defm SDZ : avx512_fp28_s<opc, OpcodeStr#"sd", f64x_info, OpNode, sched>,
               EVEX_CD8<64, CD8VT1>, VEX_W;
}

let Predicates = [HasERI] in {
  defm VRCP28   : avx512_eri_s<0xCB, "vrcp28", X86rcp28s, SchedWriteFRcp.Scl>,
                              T8PD, EVEX_4V;
  defm VRSQRT28 : avx512_eri_s<0xCD, "vrsqrt28", X86rsqrt28s,
                               SchedWriteFRsqrt.Scl>, T8PD, EVEX_4V;
}

defm VGETEXP   : avx512_eri_s<0x43, "vgetexp", X86fgetexpRnds,
                              SchedWriteFRnd.Scl>, T8PD, EVEX_4V;
/// avx512_fp28_p rcp28ps, rcp28pd, rsqrt28ps, rsqrt28pd

multiclass avx512_fp28_p<bits<8> opc, string OpcodeStr, X86VectorVTInfo _,
                         SDNode OpNode, X86FoldableSchedWrite sched> {
  let ExeDomain = _.ExeDomain in {
  defm r : AVX512_maskable<opc, MRMSrcReg, _, (outs _.RC:$dst),
                         (ins _.RC:$src), OpcodeStr, "$src", "$src",
                         (OpNode (_.VT _.RC:$src), (i32 FROUND_CURRENT))>,
                         Sched<[sched]>;

  defm m : AVX512_maskable<opc, MRMSrcMem, _, (outs _.RC:$dst),
                         (ins _.MemOp:$src), OpcodeStr, "$src", "$src",
                         (OpNode (_.VT
                             (bitconvert (_.LdFrag addr:$src))),
                          (i32 FROUND_CURRENT))>,
                          Sched<[sched.Folded, sched.ReadAfterFold]>;

  defm mb : AVX512_maskable<opc, MRMSrcMem, _, (outs _.RC:$dst),
                         (ins _.ScalarMemOp:$src), OpcodeStr,
                         "${src}"##_.BroadcastStr, "${src}"##_.BroadcastStr,
                         (OpNode (_.VT
                                  (X86VBroadcast (_.ScalarLdFrag addr:$src))),
                                 (i32 FROUND_CURRENT))>, EVEX_B,
                         Sched<[sched.Folded, sched.ReadAfterFold]>;
  }
}
multiclass avx512_fp28_p_round<bits<8> opc, string OpcodeStr, X86VectorVTInfo _,
                         SDNode OpNode, X86FoldableSchedWrite sched> {
  let ExeDomain = _.ExeDomain in
  defm rb : AVX512_maskable<opc, MRMSrcReg, _, (outs _.RC:$dst),
                        (ins _.RC:$src), OpcodeStr,
                        "{sae}, $src", "$src, {sae}",
                        (OpNode (_.VT _.RC:$src), (i32 FROUND_NO_EXC))>,
                        EVEX_B, Sched<[sched]>;
}

multiclass  avx512_eri<bits<8> opc, string OpcodeStr, SDNode OpNode,
                       X86SchedWriteWidths sched> {
   defm PSZ : avx512_fp28_p<opc, OpcodeStr#"ps", v16f32_info, OpNode, sched.ZMM>,
              avx512_fp28_p_round<opc, OpcodeStr#"ps", v16f32_info, OpNode, sched.ZMM>,
              T8PD, EVEX_V512, EVEX_CD8<32, CD8VF>;
   defm PDZ : avx512_fp28_p<opc, OpcodeStr#"pd", v8f64_info, OpNode, sched.ZMM>,
              avx512_fp28_p_round<opc, OpcodeStr#"pd", v8f64_info, OpNode, sched.ZMM>,
              T8PD, EVEX_V512, VEX_W, EVEX_CD8<64, CD8VF>;
}

multiclass avx512_fp_unaryop_packed<bits<8> opc, string OpcodeStr,
                                  SDNode OpNode, X86SchedWriteWidths sched> {
  // Define only if AVX512VL feature is present.
  let Predicates = [HasVLX] in {
    defm PSZ128 : avx512_fp28_p<opc, OpcodeStr#"ps", v4f32x_info, OpNode, sched.XMM>,
                                     EVEX_V128, T8PD, EVEX_CD8<32, CD8VF>;
    defm PSZ256 : avx512_fp28_p<opc, OpcodeStr#"ps", v8f32x_info, OpNode, sched.YMM>,
                                     EVEX_V256, T8PD, EVEX_CD8<32, CD8VF>;
    defm PDZ128 : avx512_fp28_p<opc, OpcodeStr#"pd", v2f64x_info, OpNode, sched.XMM>,
                                     EVEX_V128, VEX_W, T8PD, EVEX_CD8<64, CD8VF>;
    defm PDZ256 : avx512_fp28_p<opc, OpcodeStr#"pd", v4f64x_info, OpNode, sched.YMM>,
                                     EVEX_V256, VEX_W, T8PD, EVEX_CD8<64, CD8VF>;
  }
}

let Predicates = [HasERI] in {
 defm VRSQRT28 : avx512_eri<0xCC, "vrsqrt28", X86rsqrt28, SchedWriteFRsqrt>, EVEX;
 defm VRCP28   : avx512_eri<0xCA, "vrcp28", X86rcp28, SchedWriteFRcp>, EVEX;
 defm VEXP2    : avx512_eri<0xC8, "vexp2", X86exp2, SchedWriteFAdd>, EVEX;
}
defm VGETEXP   : avx512_eri<0x42, "vgetexp", X86fgetexpRnd, SchedWriteFRnd>,
                 avx512_fp_unaryop_packed<0x42, "vgetexp", X86fgetexpRnd,
                                          SchedWriteFRnd>, EVEX;

multiclass avx512_sqrt_packed_round<bits<8> opc, string OpcodeStr,
                                    X86FoldableSchedWrite sched, X86VectorVTInfo _>{
  let ExeDomain = _.ExeDomain in
  defm rb: AVX512_maskable<opc, MRMSrcReg, _, (outs _.RC:$dst),
                         (ins _.RC:$src, AVX512RC:$rc), OpcodeStr, "$rc, $src", "$src, $rc",
                         (_.VT (X86fsqrtRnd _.RC:$src, (i32 imm:$rc)))>,
                         EVEX, EVEX_B, EVEX_RC, Sched<[sched]>;
}

multiclass avx512_sqrt_packed<bits<8> opc, string OpcodeStr,
                              X86FoldableSchedWrite sched, X86VectorVTInfo _>{
  let ExeDomain = _.ExeDomain in {
  defm r: AVX512_maskable<opc, MRMSrcReg, _, (outs _.RC:$dst),
                         (ins _.RC:$src), OpcodeStr, "$src", "$src",
                         (_.VT (fsqrt _.RC:$src))>, EVEX,
                         Sched<[sched]>;
  defm m: AVX512_maskable<opc, MRMSrcMem, _, (outs _.RC:$dst),
                         (ins _.MemOp:$src), OpcodeStr, "$src", "$src",
                         (fsqrt (_.VT
                           (bitconvert (_.LdFrag addr:$src))))>, EVEX,
                           Sched<[sched.Folded, sched.ReadAfterFold]>;
  defm mb: AVX512_maskable<opc, MRMSrcMem, _, (outs _.RC:$dst),
                          (ins _.ScalarMemOp:$src), OpcodeStr,
                          "${src}"##_.BroadcastStr, "${src}"##_.BroadcastStr,
                          (fsqrt (_.VT
                            (X86VBroadcast (_.ScalarLdFrag addr:$src))))>,
                          EVEX, EVEX_B, Sched<[sched.Folded, sched.ReadAfterFold]>;
  }
}

multiclass avx512_sqrt_packed_all<bits<8> opc, string OpcodeStr,
                                  X86SchedWriteSizes sched> {
  defm PSZ : avx512_sqrt_packed<opc, !strconcat(OpcodeStr, "ps"),
                                sched.PS.ZMM, v16f32_info>,
                                EVEX_V512, PS, EVEX_CD8<32, CD8VF>;
  defm PDZ : avx512_sqrt_packed<opc, !strconcat(OpcodeStr, "pd"),
                                sched.PD.ZMM, v8f64_info>,
                                EVEX_V512, VEX_W, PD, EVEX_CD8<64, CD8VF>;
  // Define only if AVX512VL feature is present.
  let Predicates = [HasVLX] in {
    defm PSZ128 : avx512_sqrt_packed<opc, !strconcat(OpcodeStr, "ps"),
                                     sched.PS.XMM, v4f32x_info>,
                                     EVEX_V128, PS, EVEX_CD8<32, CD8VF>;
    defm PSZ256 : avx512_sqrt_packed<opc, !strconcat(OpcodeStr, "ps"),
                                     sched.PS.YMM, v8f32x_info>,
                                     EVEX_V256, PS, EVEX_CD8<32, CD8VF>;
    defm PDZ128 : avx512_sqrt_packed<opc, !strconcat(OpcodeStr, "pd"),
                                     sched.PD.XMM, v2f64x_info>,
                                     EVEX_V128, VEX_W, PD, EVEX_CD8<64, CD8VF>;
    defm PDZ256 : avx512_sqrt_packed<opc, !strconcat(OpcodeStr, "pd"),
                                     sched.PD.YMM, v4f64x_info>,
                                     EVEX_V256, VEX_W, PD, EVEX_CD8<64, CD8VF>;
  }
}

multiclass avx512_sqrt_packed_all_round<bits<8> opc, string OpcodeStr,
                                        X86SchedWriteSizes sched> {
  defm PSZ : avx512_sqrt_packed_round<opc, !strconcat(OpcodeStr, "ps"),
                                      sched.PS.ZMM, v16f32_info>,
                                      EVEX_V512, PS, EVEX_CD8<32, CD8VF>;
  defm PDZ : avx512_sqrt_packed_round<opc, !strconcat(OpcodeStr, "pd"),
                                      sched.PD.ZMM, v8f64_info>,
                                      EVEX_V512, VEX_W, PD, EVEX_CD8<64, CD8VF>;
}

multiclass avx512_sqrt_scalar<bits<8> opc, string OpcodeStr, X86FoldableSchedWrite sched,
                              X86VectorVTInfo _, string Name> {
  let ExeDomain = _.ExeDomain in {
    defm r_Int : AVX512_maskable_scalar<opc, MRMSrcReg, _, (outs _.RC:$dst),
                         (ins _.RC:$src1, _.RC:$src2), OpcodeStr,
                         "$src2, $src1", "$src1, $src2",
                         (X86fsqrtRnds (_.VT _.RC:$src1),
                                    (_.VT _.RC:$src2),
                                    (i32 FROUND_CURRENT))>,
                         Sched<[sched]>;
    defm m_Int : AVX512_maskable_scalar<opc, MRMSrcMem, _, (outs _.RC:$dst),
                         (ins _.RC:$src1, _.IntScalarMemOp:$src2), OpcodeStr,
                         "$src2, $src1", "$src1, $src2",
                         (X86fsqrtRnds (_.VT _.RC:$src1),
                                    _.ScalarIntMemCPat:$src2,
                                    (i32 FROUND_CURRENT))>,
                         Sched<[sched.Folded, sched.ReadAfterFold]>;
    defm rb_Int : AVX512_maskable_scalar<opc, MRMSrcReg, _, (outs _.RC:$dst),
                         (ins _.RC:$src1, _.RC:$src2, AVX512RC:$rc), OpcodeStr,
                         "$rc, $src2, $src1", "$src1, $src2, $rc",
                         (X86fsqrtRnds (_.VT _.RC:$src1),
                                     (_.VT _.RC:$src2),
                                     (i32 imm:$rc))>,
                         EVEX_B, EVEX_RC, Sched<[sched]>;

    let isCodeGenOnly = 1, hasSideEffects = 0, Predicates=[HasAVX512] in {
      def r : I<opc, MRMSrcReg, (outs _.FRC:$dst),
                (ins _.FRC:$src1, _.FRC:$src2),
                OpcodeStr#"\t{$src2, $src1, $dst|$dst, $src1, $src2}", []>,
                Sched<[sched]>;
      let mayLoad = 1 in
        def m : I<opc, MRMSrcMem, (outs _.FRC:$dst),
                  (ins _.FRC:$src1, _.ScalarMemOp:$src2),
                  OpcodeStr#"\t{$src2, $src1, $dst|$dst, $src1, $src2}", []>,
                  Sched<[sched.Folded, sched.ReadAfterFold]>;
    }
  }

  let Predicates = [HasAVX512] in {
    def : Pat<(_.EltVT (fsqrt _.FRC:$src)),
              (!cast<Instruction>(Name#Zr)
                  (_.EltVT (IMPLICIT_DEF)), _.FRC:$src)>;
  }

  let Predicates = [HasAVX512, OptForSize] in {
    def : Pat<(_.EltVT (fsqrt (load addr:$src))),
              (!cast<Instruction>(Name#Zm)
                  (_.EltVT (IMPLICIT_DEF)), addr:$src)>;
  }
}

multiclass avx512_sqrt_scalar_all<bits<8> opc, string OpcodeStr,
                                  X86SchedWriteSizes sched> {
  defm SSZ : avx512_sqrt_scalar<opc, OpcodeStr#"ss", sched.PS.Scl, f32x_info, NAME#"SS">,
                        EVEX_CD8<32, CD8VT1>, EVEX_4V, XS;
  defm SDZ : avx512_sqrt_scalar<opc, OpcodeStr#"sd", sched.PD.Scl, f64x_info, NAME#"SD">,
                        EVEX_CD8<64, CD8VT1>, EVEX_4V, XD, VEX_W;
}

defm VSQRT : avx512_sqrt_packed_all<0x51, "vsqrt", SchedWriteFSqrtSizes>,
             avx512_sqrt_packed_all_round<0x51, "vsqrt", SchedWriteFSqrtSizes>;

defm VSQRT : avx512_sqrt_scalar_all<0x51, "vsqrt", SchedWriteFSqrtSizes>, VEX_LIG;

multiclass avx512_rndscale_scalar<bits<8> opc, string OpcodeStr,
                                  X86FoldableSchedWrite sched, X86VectorVTInfo _> {
  let ExeDomain = _.ExeDomain in {
  defm r_Int : AVX512_maskable_scalar<opc, MRMSrcReg, _, (outs _.RC:$dst),
                           (ins _.RC:$src1, _.RC:$src2, i32u8imm:$src3), OpcodeStr,
                           "$src3, $src2, $src1", "$src1, $src2, $src3",
                           (_.VT (X86RndScales (_.VT _.RC:$src1), (_.VT _.RC:$src2),
                           (i32 imm:$src3)))>,
                           Sched<[sched]>;

  defm rb_Int : AVX512_maskable_scalar<opc, MRMSrcReg, _, (outs _.RC:$dst),
                         (ins _.RC:$src1, _.RC:$src2, i32u8imm:$src3), OpcodeStr,
                         "$src3, {sae}, $src2, $src1", "$src1, $src2, {sae}, $src3",
                         (_.VT (X86RndScalesRnd (_.VT _.RC:$src1), (_.VT _.RC:$src2),
                         (i32 imm:$src3), (i32 FROUND_NO_EXC)))>, EVEX_B,
                         Sched<[sched]>;

  defm m_Int : AVX512_maskable_scalar<opc, MRMSrcMem, _, (outs _.RC:$dst),
                         (ins _.RC:$src1, _.IntScalarMemOp:$src2, i32u8imm:$src3),
                         OpcodeStr,
                         "$src3, $src2, $src1", "$src1, $src2, $src3",
                         (_.VT (X86RndScales _.RC:$src1,
                                _.ScalarIntMemCPat:$src2, (i32 imm:$src3)))>,
                         Sched<[sched.Folded, sched.ReadAfterFold]>;

  let isCodeGenOnly = 1, hasSideEffects = 0, Predicates = [HasAVX512] in {
    def r : I<opc, MRMSrcReg, (outs _.FRC:$dst),
               (ins _.FRC:$src1, _.FRC:$src2, i32u8imm:$src3),
               OpcodeStr#"\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}",
               []>, Sched<[sched]>;

    let mayLoad = 1 in
      def m : I<opc, MRMSrcMem, (outs _.FRC:$dst),
                 (ins _.FRC:$src1, _.ScalarMemOp:$src2, i32u8imm:$src3),
                 OpcodeStr#"\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}",
                 []>, Sched<[sched.Folded, sched.ReadAfterFold]>;
  }
  }

  let Predicates = [HasAVX512] in {
    def : Pat<(ffloor _.FRC:$src),
              (_.EltVT (!cast<Instruction>(NAME##r) (_.EltVT (IMPLICIT_DEF)),
               _.FRC:$src, (i32 0x9)))>;
    def : Pat<(fceil _.FRC:$src),
              (_.EltVT (!cast<Instruction>(NAME##r) (_.EltVT (IMPLICIT_DEF)),
               _.FRC:$src, (i32 0xa)))>;
    def : Pat<(ftrunc _.FRC:$src),
              (_.EltVT (!cast<Instruction>(NAME##r) (_.EltVT (IMPLICIT_DEF)),
               _.FRC:$src, (i32 0xb)))>;
    def : Pat<(frint _.FRC:$src),
              (_.EltVT (!cast<Instruction>(NAME##r) (_.EltVT (IMPLICIT_DEF)),
               _.FRC:$src, (i32 0x4)))>;
    def : Pat<(fnearbyint _.FRC:$src),
              (_.EltVT (!cast<Instruction>(NAME##r) (_.EltVT (IMPLICIT_DEF)),
               _.FRC:$src, (i32 0xc)))>;
  }

  let Predicates = [HasAVX512, OptForSize] in {
    def : Pat<(ffloor (_.ScalarLdFrag addr:$src)),
              (_.EltVT (!cast<Instruction>(NAME##m) (_.EltVT (IMPLICIT_DEF)),
               addr:$src, (i32 0x9)))>;
    def : Pat<(fceil (_.ScalarLdFrag addr:$src)),
              (_.EltVT (!cast<Instruction>(NAME##m) (_.EltVT (IMPLICIT_DEF)),
               addr:$src, (i32 0xa)))>;
    def : Pat<(ftrunc (_.ScalarLdFrag addr:$src)),
              (_.EltVT (!cast<Instruction>(NAME##m) (_.EltVT (IMPLICIT_DEF)),
               addr:$src, (i32 0xb)))>;
    def : Pat<(frint (_.ScalarLdFrag addr:$src)),
              (_.EltVT (!cast<Instruction>(NAME##m) (_.EltVT (IMPLICIT_DEF)),
               addr:$src, (i32 0x4)))>;
    def : Pat<(fnearbyint (_.ScalarLdFrag addr:$src)),
              (_.EltVT (!cast<Instruction>(NAME##m) (_.EltVT (IMPLICIT_DEF)),
               addr:$src, (i32 0xc)))>;
  }
}

defm VRNDSCALESSZ : avx512_rndscale_scalar<0x0A, "vrndscaless",
                                           SchedWriteFRnd.Scl, f32x_info>,
                                           AVX512AIi8Base, EVEX_4V,
                                           EVEX_CD8<32, CD8VT1>;

defm VRNDSCALESDZ : avx512_rndscale_scalar<0x0B, "vrndscalesd",
                                           SchedWriteFRnd.Scl, f64x_info>,
                                           VEX_W, AVX512AIi8Base, EVEX_4V,
                                           EVEX_CD8<64, CD8VT1>;

multiclass avx512_masked_scalar<SDNode OpNode, string OpcPrefix, SDNode Move,
                                dag Mask, X86VectorVTInfo _, PatLeaf ZeroFP,
                                dag OutMask, Predicate BasePredicate> {
  let Predicates = [BasePredicate] in {
    def : Pat<(Move _.VT:$src1, (scalar_to_vector (X86selects Mask,
               (OpNode (extractelt _.VT:$src2, (iPTR 0))),
               (extractelt _.VT:$dst, (iPTR 0))))),
              (!cast<Instruction>("V"#OpcPrefix#r_Intk)
               _.VT:$dst, OutMask, _.VT:$src2, _.VT:$src1)>;

    def : Pat<(Move _.VT:$src1, (scalar_to_vector (X86selects Mask,
               (OpNode (extractelt _.VT:$src2, (iPTR 0))),
               ZeroFP))),
              (!cast<Instruction>("V"#OpcPrefix#r_Intkz)
               OutMask, _.VT:$src2, _.VT:$src1)>;
  }
}

defm : avx512_masked_scalar<fsqrt, "SQRTSSZ", X86Movss,
                            (v1i1 (scalar_to_vector (i8 (trunc (i32 GR32:$mask))))), v4f32x_info,
                            fp32imm0, (COPY_TO_REGCLASS  $mask, VK1WM), HasAVX512>;
defm : avx512_masked_scalar<fsqrt, "SQRTSDZ", X86Movsd,
                            (v1i1 (scalar_to_vector (i8 (trunc (i32 GR32:$mask))))), v2f64x_info,
                            fp64imm0, (COPY_TO_REGCLASS  $mask, VK1WM), HasAVX512>;

multiclass avx512_masked_scalar_imm<SDNode OpNode, string OpcPrefix, SDNode Move,
                                    X86VectorVTInfo _, PatLeaf ZeroFP,
                                    bits<8> ImmV, Predicate BasePredicate> {
  let Predicates = [BasePredicate] in {
    def : Pat<(Move _.VT:$src1, (scalar_to_vector (X86selects VK1WM:$mask,
               (OpNode (extractelt _.VT:$src2, (iPTR 0))),
               (extractelt _.VT:$dst, (iPTR 0))))),
              (!cast<Instruction>("V"#OpcPrefix#Zr_Intk)
               _.VT:$dst, VK1WM:$mask, _.VT:$src1, _.VT:$src2, (i32 ImmV))>;

    def : Pat<(Move _.VT:$src1, (scalar_to_vector (X86selects VK1WM:$mask,
               (OpNode (extractelt _.VT:$src2, (iPTR 0))), ZeroFP))),
              (!cast<Instruction>("V"#OpcPrefix#Zr_Intkz)
               VK1WM:$mask, _.VT:$src1, _.VT:$src2, (i32 ImmV))>;
  }
}

defm : avx512_masked_scalar_imm<ffloor, "RNDSCALESS", X86Movss,
                                v4f32x_info, fp32imm0, 0x01, HasAVX512>;
defm : avx512_masked_scalar_imm<fceil, "RNDSCALESS", X86Movss,
                                v4f32x_info, fp32imm0, 0x02, HasAVX512>;
defm : avx512_masked_scalar_imm<ffloor, "RNDSCALESD", X86Movsd,
                                v2f64x_info, fp64imm0, 0x01, HasAVX512>;
defm : avx512_masked_scalar_imm<fceil, "RNDSCALESD", X86Movsd,
                                v2f64x_info, fp64imm0, 0x02,  HasAVX512>;


//-------------------------------------------------
// Integer truncate and extend operations
//-------------------------------------------------

// PatFrags that contain a select and a truncate op. The take operands in the
// same order as X86vmtrunc, X86vmtruncs, X86vmtruncus. This allows us to pass
// either to the multiclasses.
def select_trunc : PatFrag<(ops node:$src, node:$src0, node:$mask),
                           (vselect node:$mask,
                                    (trunc node:$src), node:$src0)>;
def select_truncs : PatFrag<(ops node:$src, node:$src0, node:$mask),
                            (vselect node:$mask,
                                     (X86vtruncs node:$src), node:$src0)>;
def select_truncus : PatFrag<(ops node:$src, node:$src0, node:$mask),
                             (vselect node:$mask,
                                      (X86vtruncus node:$src), node:$src0)>;

multiclass avx512_trunc_common<bits<8> opc, string OpcodeStr, SDNode OpNode,
                              SDPatternOperator MaskNode,
                              X86FoldableSchedWrite sched, X86VectorVTInfo SrcInfo,
                              X86VectorVTInfo DestInfo, X86MemOperand x86memop> {
  let ExeDomain = DestInfo.ExeDomain in {
  def rr : AVX512XS8I<opc, MRMDestReg, (outs DestInfo.RC:$dst),
             (ins SrcInfo.RC:$src),
             OpcodeStr # "\t{$src, $dst|$dst, $src}",
             [(set DestInfo.RC:$dst,
                   (DestInfo.VT (OpNode (SrcInfo.VT SrcInfo.RC:$src))))]>,
             EVEX, Sched<[sched]>;
  let Constraints = "$src0 = $dst" in
  def rrk : AVX512XS8I<opc, MRMDestReg, (outs DestInfo.RC:$dst),
             (ins DestInfo.RC:$src0, SrcInfo.KRCWM:$mask, SrcInfo.RC:$src),
             OpcodeStr # "\t{$src, $dst {${mask}}|$dst {${mask}}, $src}",
             [(set DestInfo.RC:$dst,
                   (MaskNode (SrcInfo.VT SrcInfo.RC:$src),
                             (DestInfo.VT DestInfo.RC:$src0),
                             SrcInfo.KRCWM:$mask))]>,
             EVEX, EVEX_K, Sched<[sched]>;
  def rrkz : AVX512XS8I<opc, MRMDestReg, (outs DestInfo.RC:$dst),
             (ins SrcInfo.KRCWM:$mask, SrcInfo.RC:$src),
             OpcodeStr # "\t{$src, $dst {${mask}} {z}|$dst {${mask}} {z}, $src}",
             [(set DestInfo.RC:$dst,
                   (DestInfo.VT (MaskNode (SrcInfo.VT SrcInfo.RC:$src),
                             DestInfo.ImmAllZerosV, SrcInfo.KRCWM:$mask)))]>,
             EVEX, EVEX_KZ, Sched<[sched]>;
  }

  let mayStore = 1, hasSideEffects = 0, ExeDomain = DestInfo.ExeDomain in {
    def mr : AVX512XS8I<opc, MRMDestMem, (outs),
               (ins x86memop:$dst, SrcInfo.RC:$src),
               OpcodeStr # "\t{$src, $dst|$dst, $src}", []>,
               EVEX, Sched<[sched.Folded]>;

    def mrk : AVX512XS8I<opc, MRMDestMem, (outs),
               (ins x86memop:$dst, SrcInfo.KRCWM:$mask, SrcInfo.RC:$src),
               OpcodeStr # "\t{$src, $dst {${mask}}|$dst {${mask}}, $src}", []>,
               EVEX, EVEX_K, Sched<[sched.Folded]>, NotMemoryFoldable;
  }//mayStore = 1, hasSideEffects = 0
}

multiclass avx512_trunc_mr_lowering<X86VectorVTInfo SrcInfo,
                                    X86VectorVTInfo DestInfo,
                                    PatFrag truncFrag, PatFrag mtruncFrag,
                                    string Name> {

  def : Pat<(truncFrag (SrcInfo.VT SrcInfo.RC:$src), addr:$dst),
            (!cast<Instruction>(Name#SrcInfo.ZSuffix##mr)
                                    addr:$dst, SrcInfo.RC:$src)>;

  def : Pat<(mtruncFrag (SrcInfo.VT SrcInfo.RC:$src), addr:$dst,
                        SrcInfo.KRCWM:$mask),
            (!cast<Instruction>(Name#SrcInfo.ZSuffix##mrk)
                            addr:$dst, SrcInfo.KRCWM:$mask, SrcInfo.RC:$src)>;
}

multiclass avx512_trunc<bits<8> opc, string OpcodeStr, SDNode OpNode128,
                        SDNode OpNode256, SDNode OpNode512,
                        SDPatternOperator MaskNode128,
                        SDPatternOperator MaskNode256,
                        SDPatternOperator MaskNode512,
                        X86FoldableSchedWrite sched,
                        AVX512VLVectorVTInfo VTSrcInfo,
                        X86VectorVTInfo DestInfoZ128,
                        X86VectorVTInfo DestInfoZ256, X86VectorVTInfo DestInfoZ,
                        X86MemOperand x86memopZ128, X86MemOperand x86memopZ256,
                        X86MemOperand x86memopZ, PatFrag truncFrag,
                        PatFrag mtruncFrag, Predicate prd = HasAVX512>{

  let Predicates = [HasVLX, prd] in {
    defm Z128:  avx512_trunc_common<opc, OpcodeStr, OpNode128, MaskNode128, sched,
                             VTSrcInfo.info128, DestInfoZ128, x86memopZ128>,
                avx512_trunc_mr_lowering<VTSrcInfo.info128, DestInfoZ128,
                             truncFrag, mtruncFrag, NAME>, EVEX_V128;

    defm Z256:  avx512_trunc_common<opc, OpcodeStr, OpNode256, MaskNode256, sched,
                             VTSrcInfo.info256, DestInfoZ256, x86memopZ256>,
                avx512_trunc_mr_lowering<VTSrcInfo.info256, DestInfoZ256,
                             truncFrag, mtruncFrag, NAME>, EVEX_V256;
  }
  let Predicates = [prd] in
    defm Z:     avx512_trunc_common<opc, OpcodeStr, OpNode512, MaskNode512, sched,
                             VTSrcInfo.info512, DestInfoZ, x86memopZ>,
                avx512_trunc_mr_lowering<VTSrcInfo.info512, DestInfoZ,
                             truncFrag, mtruncFrag, NAME>, EVEX_V512;
}

multiclass avx512_trunc_qb<bits<8> opc, string OpcodeStr, SDNode OpNode,
                           SDPatternOperator MaskNode,
                           X86FoldableSchedWrite sched, PatFrag StoreNode,
                           PatFrag MaskedStoreNode, SDNode InVecNode,
                           SDPatternOperator InVecMaskNode> {
  defm NAME: avx512_trunc<opc, OpcodeStr, InVecNode, InVecNode, InVecNode,
                          InVecMaskNode, InVecMaskNode, InVecMaskNode, sched,
                          avx512vl_i64_info, v16i8x_info, v16i8x_info,
                          v16i8x_info, i16mem, i32mem, i64mem, StoreNode,
                          MaskedStoreNode>, EVEX_CD8<8, CD8VO>;
}

multiclass avx512_trunc_qw<bits<8> opc, string OpcodeStr, SDNode OpNode,
                           SDPatternOperator MaskNode,
                           X86FoldableSchedWrite sched, PatFrag StoreNode,
                           PatFrag MaskedStoreNode, SDNode InVecNode,
                           SDPatternOperator InVecMaskNode> {
  defm NAME: avx512_trunc<opc, OpcodeStr, InVecNode, InVecNode, OpNode,
                          InVecMaskNode, InVecMaskNode, MaskNode, sched,
                          avx512vl_i64_info, v8i16x_info, v8i16x_info,
                          v8i16x_info, i32mem, i64mem, i128mem, StoreNode,
                          MaskedStoreNode>, EVEX_CD8<16, CD8VQ>;
}

multiclass avx512_trunc_qd<bits<8> opc, string OpcodeStr, SDNode OpNode,
                           SDPatternOperator MaskNode,
                           X86FoldableSchedWrite sched, PatFrag StoreNode,
                           PatFrag MaskedStoreNode, SDNode InVecNode,
                           SDPatternOperator InVecMaskNode> {
  defm NAME: avx512_trunc<opc, OpcodeStr, InVecNode, OpNode, OpNode,
                          InVecMaskNode, MaskNode, MaskNode, sched,
                          avx512vl_i64_info, v4i32x_info, v4i32x_info,
                          v8i32x_info, i64mem, i128mem, i256mem, StoreNode,
                          MaskedStoreNode>, EVEX_CD8<32, CD8VH>;
}

multiclass avx512_trunc_db<bits<8> opc, string OpcodeStr, SDNode OpNode,
                           SDPatternOperator MaskNode,
                           X86FoldableSchedWrite sched, PatFrag StoreNode,
                           PatFrag MaskedStoreNode, SDNode InVecNode,
                           SDPatternOperator InVecMaskNode> {
  defm NAME: avx512_trunc<opc, OpcodeStr, InVecNode, InVecNode, OpNode,
                          InVecMaskNode, InVecMaskNode, MaskNode, sched,
                          avx512vl_i32_info, v16i8x_info, v16i8x_info,
                          v16i8x_info, i32mem, i64mem, i128mem, StoreNode,
                          MaskedStoreNode>, EVEX_CD8<8, CD8VQ>;
}

multiclass avx512_trunc_dw<bits<8> opc, string OpcodeStr, SDNode OpNode,
                           SDPatternOperator MaskNode,
                           X86FoldableSchedWrite sched, PatFrag StoreNode,
                           PatFrag MaskedStoreNode, SDNode InVecNode,
                           SDPatternOperator InVecMaskNode> {
  defm NAME: avx512_trunc<opc, OpcodeStr, InVecNode, OpNode, OpNode,
                          InVecMaskNode, MaskNode, MaskNode, sched,
                          avx512vl_i32_info, v8i16x_info, v8i16x_info,
                          v16i16x_info, i64mem, i128mem, i256mem, StoreNode,
                          MaskedStoreNode>, EVEX_CD8<16, CD8VH>;
}

multiclass avx512_trunc_wb<bits<8> opc, string OpcodeStr, SDNode OpNode,
                           SDPatternOperator MaskNode,
                           X86FoldableSchedWrite sched, PatFrag StoreNode,
                           PatFrag MaskedStoreNode, SDNode InVecNode,
                           SDPatternOperator InVecMaskNode> {
  defm NAME: avx512_trunc<opc, OpcodeStr, InVecNode, OpNode, OpNode,
                          InVecMaskNode, MaskNode, MaskNode, sched,
                          avx512vl_i16_info, v16i8x_info, v16i8x_info,
                          v32i8x_info, i64mem, i128mem, i256mem, StoreNode,
                          MaskedStoreNode, HasBWI>, EVEX_CD8<16, CD8VH>;
}

defm VPMOVQB    : avx512_trunc_qb<0x32, "vpmovqb",   trunc, select_trunc,
                                  WriteShuffle256, truncstorevi8,
                                  masked_truncstorevi8, X86vtrunc, X86vmtrunc>;
defm VPMOVSQB   : avx512_trunc_qb<0x22, "vpmovsqb",  X86vtruncs, select_truncs,
                                  WriteShuffle256, truncstore_s_vi8,
                                  masked_truncstore_s_vi8, X86vtruncs,
                                  X86vmtruncs>;
defm VPMOVUSQB  : avx512_trunc_qb<0x12, "vpmovusqb", X86vtruncus,
                                  select_truncus, WriteShuffle256,
                                  truncstore_us_vi8, masked_truncstore_us_vi8,
                                  X86vtruncus, X86vmtruncus>;

defm VPMOVQW    : avx512_trunc_qw<0x34, "vpmovqw", trunc, select_trunc,
                                  WriteShuffle256, truncstorevi16,
                                  masked_truncstorevi16, X86vtrunc, X86vmtrunc>;
defm VPMOVSQW   : avx512_trunc_qw<0x24, "vpmovsqw",  X86vtruncs, select_truncs,
                                  WriteShuffle256, truncstore_s_vi16,
                                  masked_truncstore_s_vi16, X86vtruncs,
                                  X86vmtruncs>;
defm VPMOVUSQW  : avx512_trunc_qw<0x14, "vpmovusqw", X86vtruncus,
                                  select_truncus, WriteShuffle256,
                                  truncstore_us_vi16, masked_truncstore_us_vi16,
                                  X86vtruncus, X86vmtruncus>;

defm VPMOVQD    : avx512_trunc_qd<0x35, "vpmovqd", trunc, select_trunc,
                                  WriteShuffle256, truncstorevi32,
                                  masked_truncstorevi32, X86vtrunc, X86vmtrunc>;
defm VPMOVSQD   : avx512_trunc_qd<0x25, "vpmovsqd",  X86vtruncs, select_truncs,
                                  WriteShuffle256, truncstore_s_vi32,
                                  masked_truncstore_s_vi32, X86vtruncs,
                                  X86vmtruncs>;
defm VPMOVUSQD  : avx512_trunc_qd<0x15, "vpmovusqd", X86vtruncus,
                                  select_truncus, WriteShuffle256,
                                  truncstore_us_vi32, masked_truncstore_us_vi32,
                                  X86vtruncus, X86vmtruncus>;

defm VPMOVDB    : avx512_trunc_db<0x31, "vpmovdb", trunc, select_trunc,
                                  WriteShuffle256, truncstorevi8,
                                  masked_truncstorevi8, X86vtrunc, X86vmtrunc>;
defm VPMOVSDB   : avx512_trunc_db<0x21, "vpmovsdb", X86vtruncs, select_truncs,
                                  WriteShuffle256, truncstore_s_vi8,
                                  masked_truncstore_s_vi8, X86vtruncs,
                                  X86vmtruncs>;
defm VPMOVUSDB  : avx512_trunc_db<0x11, "vpmovusdb",  X86vtruncus,
                                  select_truncus, WriteShuffle256,
                                  truncstore_us_vi8, masked_truncstore_us_vi8,
                                  X86vtruncus, X86vmtruncus>;

defm VPMOVDW    : avx512_trunc_dw<0x33, "vpmovdw", trunc, select_trunc,
                                  WriteShuffle256, truncstorevi16,
                                  masked_truncstorevi16, X86vtrunc, X86vmtrunc>;
defm VPMOVSDW   : avx512_trunc_dw<0x23, "vpmovsdw", X86vtruncs, select_truncs,
                                  WriteShuffle256, truncstore_s_vi16,
                                  masked_truncstore_s_vi16, X86vtruncs,
                                  X86vmtruncs>;
defm VPMOVUSDW  : avx512_trunc_dw<0x13, "vpmovusdw", X86vtruncus,
                                  select_truncus, WriteShuffle256,
                                  truncstore_us_vi16, masked_truncstore_us_vi16,
                                  X86vtruncus, X86vmtruncus>;

defm VPMOVWB    : avx512_trunc_wb<0x30, "vpmovwb", trunc, select_trunc,
                                  WriteShuffle256, truncstorevi8,
                                  masked_truncstorevi8, X86vtrunc,
                                  X86vmtrunc>;
defm VPMOVSWB   : avx512_trunc_wb<0x20, "vpmovswb", X86vtruncs, select_truncs,
                                  WriteShuffle256, truncstore_s_vi8,
                                  masked_truncstore_s_vi8, X86vtruncs,
                                  X86vmtruncs>;
defm VPMOVUSWB  : avx512_trunc_wb<0x10, "vpmovuswb", X86vtruncus,
                                  select_truncus, WriteShuffle256,
                                  truncstore_us_vi8, masked_truncstore_us_vi8,
                                  X86vtruncus, X86vmtruncus>;

let Predicates = [HasAVX512, NoVLX] in {
def: Pat<(v8i16 (trunc (v8i32 VR256X:$src))),
         (v8i16 (EXTRACT_SUBREG
                 (v16i16 (VPMOVDWZrr (v16i32 (INSERT_SUBREG (IMPLICIT_DEF),
                                          VR256X:$src, sub_ymm)))), sub_xmm))>;
def: Pat<(v4i32 (trunc (v4i64 VR256X:$src))),
         (v4i32 (EXTRACT_SUBREG
                 (v8i32 (VPMOVQDZrr (v8i64 (INSERT_SUBREG (IMPLICIT_DEF),
                                           VR256X:$src, sub_ymm)))), sub_xmm))>;
}

let Predicates = [HasBWI, NoVLX] in {
def: Pat<(v16i8 (trunc (v16i16 VR256X:$src))),
         (v16i8 (EXTRACT_SUBREG (VPMOVWBZrr (v32i16 (INSERT_SUBREG (IMPLICIT_DEF),
                                            VR256X:$src, sub_ymm))), sub_xmm))>;
}

// Without BWI we can't use vXi16/vXi8 vselect so we have to use vmtrunc nodes.
multiclass mtrunc_lowering<string InstrName, SDNode OpNode,
                           X86VectorVTInfo DestInfo,
                           X86VectorVTInfo SrcInfo> {
  def : Pat<(DestInfo.VT (OpNode (SrcInfo.VT SrcInfo.RC:$src),
                                 DestInfo.RC:$src0,
                                 SrcInfo.KRCWM:$mask)),
            (!cast<Instruction>(InstrName#"rrk") DestInfo.RC:$src0,
                                                 SrcInfo.KRCWM:$mask,
                                                 SrcInfo.RC:$src)>;

  def : Pat<(DestInfo.VT (OpNode (SrcInfo.VT SrcInfo.RC:$src),
                                 DestInfo.ImmAllZerosV,
                                 SrcInfo.KRCWM:$mask)),
            (!cast<Instruction>(InstrName#"rrkz") SrcInfo.KRCWM:$mask,
                                                  SrcInfo.RC:$src)>;
}

let Predicates = [HasVLX] in {
defm : mtrunc_lowering<"VPMOVDWZ256", X86vmtrunc, v8i16x_info, v8i32x_info>;
defm : mtrunc_lowering<"VPMOVSDWZ256", X86vmtruncs, v8i16x_info, v8i32x_info>;
defm : mtrunc_lowering<"VPMOVUSDWZ256", X86vmtruncus, v8i16x_info, v8i32x_info>;
}

let Predicates = [HasAVX512] in {
defm : mtrunc_lowering<"VPMOVDWZ", X86vmtrunc, v16i16x_info, v16i32_info>;
defm : mtrunc_lowering<"VPMOVSDWZ", X86vmtruncs, v16i16x_info, v16i32_info>;
defm : mtrunc_lowering<"VPMOVUSDWZ", X86vmtruncus, v16i16x_info, v16i32_info>;

defm : mtrunc_lowering<"VPMOVDBZ", X86vmtrunc, v16i8x_info, v16i32_info>;
defm : mtrunc_lowering<"VPMOVSDBZ", X86vmtruncs, v16i8x_info, v16i32_info>;
defm : mtrunc_lowering<"VPMOVUSDBZ", X86vmtruncus, v16i8x_info, v16i32_info>;

defm : mtrunc_lowering<"VPMOVQWZ", X86vmtrunc, v8i16x_info, v8i64_info>;
defm : mtrunc_lowering<"VPMOVSQWZ", X86vmtruncs, v8i16x_info, v8i64_info>;
defm : mtrunc_lowering<"VPMOVUSQWZ", X86vmtruncus, v8i16x_info, v8i64_info>;
}

multiclass WriteShuffle256_common<bits<8> opc, string OpcodeStr, X86FoldableSchedWrite sched,
              X86VectorVTInfo DestInfo, X86VectorVTInfo SrcInfo,
              X86MemOperand x86memop, PatFrag LdFrag, SDNode OpNode>{
  let ExeDomain = DestInfo.ExeDomain in {
  defm rr   : AVX512_maskable<opc, MRMSrcReg, DestInfo, (outs DestInfo.RC:$dst),
                    (ins SrcInfo.RC:$src), OpcodeStr ,"$src", "$src",
                    (DestInfo.VT (OpNode (SrcInfo.VT SrcInfo.RC:$src)))>,
                  EVEX, Sched<[sched]>;

  defm rm : AVX512_maskable<opc, MRMSrcMem, DestInfo, (outs DestInfo.RC:$dst),
                  (ins x86memop:$src), OpcodeStr ,"$src", "$src",
                  (DestInfo.VT (LdFrag addr:$src))>,
                EVEX, Sched<[sched.Folded]>;
  }
}

multiclass WriteShuffle256_BW<bits<8> opc, string OpcodeStr,
          SDNode OpNode, SDNode InVecNode, string ExtTy,
          X86FoldableSchedWrite sched, PatFrag LdFrag = !cast<PatFrag>(ExtTy#"extloadvi8")> {
  let Predicates = [HasVLX, HasBWI] in {
    defm Z128:  WriteShuffle256_common<opc, OpcodeStr, sched, v8i16x_info,
                    v16i8x_info, i64mem, LdFrag, InVecNode>,
                     EVEX_CD8<8, CD8VH>, T8PD, EVEX_V128, VEX_WIG;

    defm Z256:  WriteShuffle256_common<opc, OpcodeStr, sched, v16i16x_info,
                    v16i8x_info, i128mem, LdFrag, OpNode>,
                     EVEX_CD8<8, CD8VH>, T8PD, EVEX_V256, VEX_WIG;
  }
  let Predicates = [HasBWI] in {
    defm Z   :  WriteShuffle256_common<opc, OpcodeStr, sched, v32i16_info,
                    v32i8x_info, i256mem, LdFrag, OpNode>,
                     EVEX_CD8<8, CD8VH>, T8PD, EVEX_V512, VEX_WIG;
  }
}

multiclass WriteShuffle256_BD<bits<8> opc, string OpcodeStr,
          SDNode OpNode, SDNode InVecNode, string ExtTy,
          X86FoldableSchedWrite sched, PatFrag LdFrag = !cast<PatFrag>(ExtTy#"extloadvi8")> {
  let Predicates = [HasVLX, HasAVX512] in {
    defm Z128:  WriteShuffle256_common<opc, OpcodeStr, sched, v4i32x_info,
                   v16i8x_info, i32mem, LdFrag, InVecNode>,
                         EVEX_CD8<8, CD8VQ>, T8PD, EVEX_V128, VEX_WIG;

    defm Z256:  WriteShuffle256_common<opc, OpcodeStr, sched, v8i32x_info,
                   v16i8x_info, i64mem, LdFrag, InVecNode>,
                         EVEX_CD8<8, CD8VQ>, T8PD, EVEX_V256, VEX_WIG;
  }
  let Predicates = [HasAVX512] in {
    defm Z   :  WriteShuffle256_common<opc, OpcodeStr, sched, v16i32_info,
                   v16i8x_info, i128mem, LdFrag, OpNode>,
                         EVEX_CD8<8, CD8VQ>, T8PD, EVEX_V512, VEX_WIG;
  }
}

multiclass WriteShuffle256_BQ<bits<8> opc, string OpcodeStr,
          SDNode OpNode, SDNode InVecNode, string ExtTy,
          X86FoldableSchedWrite sched, PatFrag LdFrag = !cast<PatFrag>(ExtTy#"extloadvi8")> {
  let Predicates = [HasVLX, HasAVX512] in {
    defm Z128:  WriteShuffle256_common<opc, OpcodeStr, sched, v2i64x_info,
                   v16i8x_info, i16mem, LdFrag, InVecNode>,
                     EVEX_CD8<8, CD8VO>, T8PD, EVEX_V128, VEX_WIG;

    defm Z256:  WriteShuffle256_common<opc, OpcodeStr, sched, v4i64x_info,
                   v16i8x_info, i32mem, LdFrag, InVecNode>,
                     EVEX_CD8<8, CD8VO>, T8PD, EVEX_V256, VEX_WIG;
  }
  let Predicates = [HasAVX512] in {
    defm Z   :  WriteShuffle256_common<opc, OpcodeStr, sched, v8i64_info,
                   v16i8x_info, i64mem, LdFrag, InVecNode>,
                     EVEX_CD8<8, CD8VO>, T8PD, EVEX_V512, VEX_WIG;
  }
}

multiclass WriteShuffle256_WD<bits<8> opc, string OpcodeStr,
         SDNode OpNode, SDNode InVecNode, string ExtTy,
         X86FoldableSchedWrite sched, PatFrag LdFrag = !cast<PatFrag>(ExtTy#"extloadvi16")> {
  let Predicates = [HasVLX, HasAVX512] in {
    defm Z128:  WriteShuffle256_common<opc, OpcodeStr, sched, v4i32x_info,
                   v8i16x_info, i64mem, LdFrag, InVecNode>,
                     EVEX_CD8<16, CD8VH>, T8PD, EVEX_V128, VEX_WIG;

    defm Z256:  WriteShuffle256_common<opc, OpcodeStr, sched, v8i32x_info,
                   v8i16x_info, i128mem, LdFrag, OpNode>,
                     EVEX_CD8<16, CD8VH>, T8PD, EVEX_V256, VEX_WIG;
  }
  let Predicates = [HasAVX512] in {
    defm Z   :  WriteShuffle256_common<opc, OpcodeStr, sched, v16i32_info,
                   v16i16x_info, i256mem, LdFrag, OpNode>,
                     EVEX_CD8<16, CD8VH>, T8PD, EVEX_V512, VEX_WIG;
  }
}

multiclass WriteShuffle256_WQ<bits<8> opc, string OpcodeStr,
         SDNode OpNode, SDNode InVecNode, string ExtTy,
         X86FoldableSchedWrite sched, PatFrag LdFrag = !cast<PatFrag>(ExtTy#"extloadvi16")> {
  let Predicates = [HasVLX, HasAVX512] in {
    defm Z128:  WriteShuffle256_common<opc, OpcodeStr, sched, v2i64x_info,
                   v8i16x_info, i32mem, LdFrag, InVecNode>,
                     EVEX_CD8<16, CD8VQ>, T8PD, EVEX_V128, VEX_WIG;

    defm Z256:  WriteShuffle256_common<opc, OpcodeStr, sched, v4i64x_info,
                   v8i16x_info, i64mem, LdFrag, InVecNode>,
                     EVEX_CD8<16, CD8VQ>, T8PD, EVEX_V256, VEX_WIG;
  }
  let Predicates = [HasAVX512] in {
    defm Z   :  WriteShuffle256_common<opc, OpcodeStr, sched, v8i64_info,
                   v8i16x_info, i128mem, LdFrag, OpNode>,
                     EVEX_CD8<16, CD8VQ>, T8PD, EVEX_V512, VEX_WIG;
  }
}

multiclass WriteShuffle256_DQ<bits<8> opc, string OpcodeStr,
         SDNode OpNode, SDNode InVecNode, string ExtTy,
         X86FoldableSchedWrite sched, PatFrag LdFrag = !cast<PatFrag>(ExtTy#"extloadvi32")> {

  let Predicates = [HasVLX, HasAVX512] in {
    defm Z128:  WriteShuffle256_common<opc, OpcodeStr, sched, v2i64x_info,
                   v4i32x_info, i64mem, LdFrag, InVecNode>,
                     EVEX_CD8<32, CD8VH>, T8PD, EVEX_V128;

    defm Z256:  WriteShuffle256_common<opc, OpcodeStr, sched, v4i64x_info,
                   v4i32x_info, i128mem, LdFrag, OpNode>,
                     EVEX_CD8<32, CD8VH>, T8PD, EVEX_V256;
  }
  let Predicates = [HasAVX512] in {
    defm Z   :  WriteShuffle256_common<opc, OpcodeStr, sched, v8i64_info,
                   v8i32x_info, i256mem, LdFrag, OpNode>,
                     EVEX_CD8<32, CD8VH>, T8PD, EVEX_V512;
  }
}

defm VPMOVZXBW : WriteShuffle256_BW<0x30, "vpmovzxbw", zext, zext_invec, "z", WriteShuffle256>;
defm VPMOVZXBD : WriteShuffle256_BD<0x31, "vpmovzxbd", zext, zext_invec, "z", WriteShuffle256>;
defm VPMOVZXBQ : WriteShuffle256_BQ<0x32, "vpmovzxbq", zext, zext_invec, "z", WriteShuffle256>;
defm VPMOVZXWD : WriteShuffle256_WD<0x33, "vpmovzxwd", zext, zext_invec, "z", WriteShuffle256>;
defm VPMOVZXWQ : WriteShuffle256_WQ<0x34, "vpmovzxwq", zext, zext_invec, "z", WriteShuffle256>;
defm VPMOVZXDQ : WriteShuffle256_DQ<0x35, "vpmovzxdq", zext, zext_invec, "z", WriteShuffle256>;

defm VPMOVSXBW: WriteShuffle256_BW<0x20, "vpmovsxbw", sext, sext_invec, "s", WriteShuffle256>;
defm VPMOVSXBD: WriteShuffle256_BD<0x21, "vpmovsxbd", sext, sext_invec, "s", WriteShuffle256>;
defm VPMOVSXBQ: WriteShuffle256_BQ<0x22, "vpmovsxbq", sext, sext_invec, "s", WriteShuffle256>;
defm VPMOVSXWD: WriteShuffle256_WD<0x23, "vpmovsxwd", sext, sext_invec, "s", WriteShuffle256>;
defm VPMOVSXWQ: WriteShuffle256_WQ<0x24, "vpmovsxwq", sext, sext_invec, "s", WriteShuffle256>;
defm VPMOVSXDQ: WriteShuffle256_DQ<0x25, "vpmovsxdq", sext, sext_invec, "s", WriteShuffle256>;


// Patterns that we also need any extend versions of. aext_vector_inreg
// is currently legalized to zext_vector_inreg.
multiclass AVX512_pmovx_patterns_base<string OpcPrefix, SDNode ExtOp> {
  // 256-bit patterns
  let Predicates = [HasVLX, HasBWI] in {
    def : Pat<(v16i16 (ExtOp (loadv16i8 addr:$src))),
              (!cast<I>(OpcPrefix#BWZ256rm) addr:$src)>;
    def : Pat<(v16i16 (ExtOp (v16i8 (vzmovl_v2i64 addr:$src)))),
              (!cast<I>(OpcPrefix#BWZ256rm) addr:$src)>;
    def : Pat<(v16i16 (ExtOp (v16i8 (vzload_v2i64 addr:$src)))),
              (!cast<I>(OpcPrefix#BWZ256rm) addr:$src)>;
  }

  let Predicates = [HasVLX] in {
    def : Pat<(v8i32 (ExtOp (loadv8i16 addr:$src))),
              (!cast<I>(OpcPrefix#WDZ256rm) addr:$src)>;
    def : Pat<(v8i32 (ExtOp (v8i16 (vzmovl_v2i64 addr:$src)))),
              (!cast<I>(OpcPrefix#WDZ256rm) addr:$src)>;
    def : Pat<(v8i32 (ExtOp (v8i16 (vzload_v2i64 addr:$src)))),
              (!cast<I>(OpcPrefix#WDZ256rm) addr:$src)>;

    def : Pat<(v4i64 (ExtOp (loadv4i32 addr:$src))),
              (!cast<I>(OpcPrefix#DQZ256rm) addr:$src)>;
    def : Pat<(v4i64 (ExtOp (v4i32 (vzmovl_v2i64 addr:$src)))),
              (!cast<I>(OpcPrefix#DQZ256rm) addr:$src)>;
    def : Pat<(v4i64 (ExtOp (v4i32 (vzload_v2i64 addr:$src)))),
              (!cast<I>(OpcPrefix#DQZ256rm) addr:$src)>;
  }

  // 512-bit patterns
  let Predicates = [HasBWI] in {
    def : Pat<(v32i16 (ExtOp (loadv32i8 addr:$src))),
              (!cast<I>(OpcPrefix#BWZrm) addr:$src)>;
  }
  let Predicates = [HasAVX512] in {
    def : Pat<(v16i32 (ExtOp (loadv16i8 addr:$src))),
              (!cast<I>(OpcPrefix#BDZrm) addr:$src)>;
    def : Pat<(v16i32 (ExtOp (loadv16i16 addr:$src))),
              (!cast<I>(OpcPrefix#WDZrm) addr:$src)>;

    def : Pat<(v8i64 (ExtOp (loadv8i16 addr:$src))),
              (!cast<I>(OpcPrefix#WQZrm) addr:$src)>;

    def : Pat<(v8i64 (ExtOp (loadv8i32 addr:$src))),
              (!cast<I>(OpcPrefix#DQZrm) addr:$src)>;
  }
}

multiclass AVX512_pmovx_patterns_aext<string OpcPrefix, SDNode ExtOp> :
    AVX512_pmovx_patterns_base<OpcPrefix, ExtOp> {
  let Predicates = [HasVLX, HasBWI] in {
    def : Pat<(v16i16 (ExtOp (v16i8 VR128X:$src))),
              (!cast<I>(OpcPrefix#BWZ256rr) VR128X:$src)>;
  }

  let Predicates = [HasVLX] in {
    def : Pat<(v8i32 (ExtOp (v8i16 VR128X:$src))),
              (!cast<I>(OpcPrefix#WDZ256rr) VR128X:$src)>;

    def : Pat<(v4i64 (ExtOp (v4i32 VR128X:$src))),
              (!cast<I>(OpcPrefix#DQZ256rr) VR128X:$src)>;
  }

  // 512-bit patterns
  let Predicates = [HasBWI] in {
    def : Pat<(v32i16 (ExtOp (v32i8 VR256X:$src))),
              (!cast<I>(OpcPrefix#BWZrr) VR256X:$src)>;
  }
  let Predicates = [HasAVX512] in {
    def : Pat<(v16i32 (ExtOp (v16i8 VR128X:$src))),
              (!cast<I>(OpcPrefix#BDZrr) VR128X:$src)>;
    def : Pat<(v16i32 (ExtOp (v16i16 VR256X:$src))),
              (!cast<I>(OpcPrefix#WDZrr) VR256X:$src)>;

    def : Pat<(v8i64 (ExtOp (v8i16 VR128X:$src))),
              (!cast<I>(OpcPrefix#WQZrr) VR128X:$src)>;

    def : Pat<(v8i64 (ExtOp (v8i32 VR256X:$src))),
              (!cast<I>(OpcPrefix#DQZrr) VR256X:$src)>;
  }
}


multiclass AVX512_pmovx_patterns<string OpcPrefix, SDNode ExtOp,
                                 SDNode InVecOp> :
    AVX512_pmovx_patterns_base<OpcPrefix, ExtOp> {
  // 128-bit patterns
  let Predicates = [HasVLX, HasBWI] in {
  def : Pat<(v8i16 (InVecOp (bc_v16i8 (v2i64 (scalar_to_vector (loadi64 addr:$src)))))),
            (!cast<I>(OpcPrefix#BWZ128rm) addr:$src)>;
  def : Pat<(v8i16 (InVecOp (bc_v16i8 (v2f64 (scalar_to_vector (loadf64 addr:$src)))))),
            (!cast<I>(OpcPrefix#BWZ128rm) addr:$src)>;
  def : Pat<(v8i16 (InVecOp (v16i8 (vzmovl_v2i64 addr:$src)))),
            (!cast<I>(OpcPrefix#BWZ128rm) addr:$src)>;
  def : Pat<(v8i16 (InVecOp (v16i8 (vzload_v2i64 addr:$src)))),
            (!cast<I>(OpcPrefix#BWZ128rm) addr:$src)>;
  def : Pat<(v8i16 (InVecOp (loadv16i8 addr:$src))),
            (!cast<I>(OpcPrefix#BWZ128rm) addr:$src)>;
  }
  let Predicates = [HasVLX] in {
  def : Pat<(v4i32 (InVecOp (bc_v16i8 (v4i32 (scalar_to_vector (loadi32 addr:$src)))))),
            (!cast<I>(OpcPrefix#BDZ128rm) addr:$src)>;
  def : Pat<(v4i32 (InVecOp (v16i8 (vzmovl_v4i32 addr:$src)))),
            (!cast<I>(OpcPrefix#BDZ128rm) addr:$src)>;
  def : Pat<(v4i32 (InVecOp (v16i8 (vzload_v2i64 addr:$src)))),
            (!cast<I>(OpcPrefix#BDZ128rm) addr:$src)>;
  def : Pat<(v4i32 (InVecOp (loadv16i8 addr:$src))),
            (!cast<I>(OpcPrefix#BDZ128rm) addr:$src)>;

  def : Pat<(v2i64 (InVecOp (bc_v16i8 (v4i32 (scalar_to_vector (extloadi32i16 addr:$src)))))),
            (!cast<I>(OpcPrefix#BQZ128rm) addr:$src)>;
  def : Pat<(v2i64 (InVecOp (v16i8 (vzmovl_v4i32 addr:$src)))),
            (!cast<I>(OpcPrefix#BQZ128rm) addr:$src)>;
  def : Pat<(v2i64 (InVecOp (v16i8 (vzload_v2i64 addr:$src)))),
            (!cast<I>(OpcPrefix#BQZ128rm) addr:$src)>;
  def : Pat<(v2i64 (InVecOp (loadv16i8 addr:$src))),
            (!cast<I>(OpcPrefix#BQZ128rm) addr:$src)>;

  def : Pat<(v4i32 (InVecOp (bc_v8i16 (v2i64 (scalar_to_vector (loadi64 addr:$src)))))),
            (!cast<I>(OpcPrefix#WDZ128rm) addr:$src)>;
  def : Pat<(v4i32 (InVecOp (bc_v8i16 (v2f64 (scalar_to_vector (loadf64 addr:$src)))))),
            (!cast<I>(OpcPrefix#WDZ128rm) addr:$src)>;
  def : Pat<(v4i32 (InVecOp (v8i16 (vzmovl_v2i64 addr:$src)))),
            (!cast<I>(OpcPrefix#WDZ128rm) addr:$src)>;
  def : Pat<(v4i32 (InVecOp (v8i16 (vzload_v2i64 addr:$src)))),
            (!cast<I>(OpcPrefix#WDZ128rm) addr:$src)>;
  def : Pat<(v4i32 (InVecOp (loadv8i16 addr:$src))),
            (!cast<I>(OpcPrefix#WDZ128rm) addr:$src)>;

  def : Pat<(v2i64 (InVecOp (bc_v8i16 (v4i32 (scalar_to_vector (loadi32 addr:$src)))))),
            (!cast<I>(OpcPrefix#WQZ128rm) addr:$src)>;
  def : Pat<(v2i64 (InVecOp (v8i16 (vzmovl_v4i32 addr:$src)))),
            (!cast<I>(OpcPrefix#WQZ128rm) addr:$src)>;
  def : Pat<(v2i64 (InVecOp (v8i16 (vzload_v2i64 addr:$src)))),
            (!cast<I>(OpcPrefix#WQZ128rm) addr:$src)>;
  def : Pat<(v2i64 (InVecOp (loadv8i16 addr:$src))),
            (!cast<I>(OpcPrefix#WQZ128rm) addr:$src)>;

  def : Pat<(v2i64 (InVecOp (bc_v4i32 (v2i64 (scalar_to_vector (loadi64 addr:$src)))))),
            (!cast<I>(OpcPrefix#DQZ128rm) addr:$src)>;
  def : Pat<(v2i64 (InVecOp (bc_v4i32 (v2f64 (scalar_to_vector (loadf64 addr:$src)))))),
            (!cast<I>(OpcPrefix#DQZ128rm) addr:$src)>;
  def : Pat<(v2i64 (InVecOp (v4i32 (vzmovl_v2i64 addr:$src)))),
            (!cast<I>(OpcPrefix#DQZ128rm) addr:$src)>;
  def : Pat<(v2i64 (InVecOp (v4i32 (vzload_v2i64 addr:$src)))),
            (!cast<I>(OpcPrefix#DQZ128rm) addr:$src)>;
  def : Pat<(v2i64 (InVecOp (loadv4i32 addr:$src))),
            (!cast<I>(OpcPrefix#DQZ128rm) addr:$src)>;
  }
  let Predicates = [HasVLX] in {
  def : Pat<(v8i32 (InVecOp (bc_v16i8 (v2i64 (scalar_to_vector (loadi64 addr:$src)))))),
            (!cast<I>(OpcPrefix#BDZ256rm) addr:$src)>;
  def : Pat<(v8i32 (InVecOp (v16i8 (vzmovl_v2i64 addr:$src)))),
            (!cast<I>(OpcPrefix#BDZ256rm) addr:$src)>;
  def : Pat<(v8i32 (InVecOp (v16i8 (vzload_v2i64 addr:$src)))),
            (!cast<I>(OpcPrefix#BDZ256rm) addr:$src)>;
  def : Pat<(v8i32 (InVecOp (loadv16i8 addr:$src))),
            (!cast<I>(OpcPrefix#BDZ256rm) addr:$src)>;

  def : Pat<(v4i64 (InVecOp (bc_v16i8 (v4i32 (scalar_to_vector (loadi32 addr:$src)))))),
            (!cast<I>(OpcPrefix#BQZ256rm) addr:$src)>;
  def : Pat<(v4i64 (InVecOp (v16i8 (vzmovl_v4i32 addr:$src)))),
            (!cast<I>(OpcPrefix#BQZ256rm) addr:$src)>;
  def : Pat<(v4i64 (InVecOp (v16i8 (vzload_v2i64 addr:$src)))),
            (!cast<I>(OpcPrefix#BQZ256rm) addr:$src)>;
  def : Pat<(v4i64 (InVecOp (loadv16i8 addr:$src))),
            (!cast<I>(OpcPrefix#BQZ256rm) addr:$src)>;

  def : Pat<(v4i64 (InVecOp (bc_v8i16 (v2i64 (scalar_to_vector (loadi64 addr:$src)))))),
            (!cast<I>(OpcPrefix#WQZ256rm) addr:$src)>;
  def : Pat<(v4i64 (InVecOp (v8i16 (vzmovl_v2i64 addr:$src)))),
            (!cast<I>(OpcPrefix#WQZ256rm) addr:$src)>;
  def : Pat<(v4i64 (InVecOp (v8i16 (vzload_v2i64 addr:$src)))),
            (!cast<I>(OpcPrefix#WQZ256rm) addr:$src)>;
  def : Pat<(v4i64 (InVecOp (loadv8i16 addr:$src))),
            (!cast<I>(OpcPrefix#WQZ256rm) addr:$src)>;
  }
  // 512-bit patterns
  let Predicates = [HasAVX512] in {
  def : Pat<(v8i64 (InVecOp (bc_v16i8 (v2i64 (scalar_to_vector (loadi64 addr:$src)))))),
            (!cast<I>(OpcPrefix#BQZrm) addr:$src)>;
  def : Pat<(v8i64 (InVecOp (loadv16i8 addr:$src))),
            (!cast<I>(OpcPrefix#BQZrm) addr:$src)>;
  }
}

defm : AVX512_pmovx_patterns<"VPMOVSX", sext, sext_invec>;
defm : AVX512_pmovx_patterns<"VPMOVZX", zext, zext_invec>;
defm : AVX512_pmovx_patterns_aext<"VPMOVZX", anyext>;

// Without BWI we can't do a trunc from v16i16 to v16i8. DAG combine can merge
// ext+trunc aggresively making it impossible to legalize the DAG to this
// pattern directly.
let Predicates = [HasAVX512, NoBWI] in {
def: Pat<(v16i8 (trunc (v16i16 VR256X:$src))),
         (VPMOVDBZrr (v16i32 (VPMOVZXWDZrr VR256X:$src)))>;
def: Pat<(v16i8 (trunc (bc_v16i16 (loadv4i64 addr:$src)))),
         (VPMOVDBZrr (v16i32 (VPMOVZXWDZrm addr:$src)))>;
def: Pat<(store (v16i8 (trunc (v16i16 VR256X:$src))), addr:$dst),
         (VPMOVDBZmr addr:$dst, (v16i32 (VPMOVZXWDZrr VR256X:$src)))>;
}

// Without BWI we can't do a trunc from v16i16 to v16i8. DAG combine can merge
// ext+trunc aggresively making it impossible to legalize the DAG to this
// pattern directly.
let Predicates = [HasAVX512, NoBWI] in {
def: Pat<(v16i8 (trunc (v16i16 VR256X:$src))),
         (VPMOVDBZrr (v16i32 (VPMOVZXWDZrr VR256X:$src)))>;
def: Pat<(v16i8 (trunc (bc_v16i16 (loadv4i64 addr:$src)))),
         (VPMOVDBZrr (v16i32 (VPMOVZXWDZrm addr:$src)))>;
def: Pat<(store (v16i8 (trunc (v16i16 VR256X:$src))), addr:$dst),
         (VPMOVDBZmr addr:$dst, (v16i32 (VPMOVZXWDZrr VR256X:$src)))>;
}

//===----------------------------------------------------------------------===//
// GATHER - SCATTER Operations

// FIXME: Improve scheduling of gather/scatter instructions.
multiclass avx512_gather<bits<8> opc, string OpcodeStr, X86VectorVTInfo _,
                         X86MemOperand memop, PatFrag GatherNode,
                         RegisterClass MaskRC = _.KRCWM> {
  let Constraints = "@earlyclobber $dst, $src1 = $dst, $mask = $mask_wb",
      ExeDomain = _.ExeDomain in
  def rm  : AVX5128I<opc, MRMSrcMem, (outs _.RC:$dst, MaskRC:$mask_wb),
            (ins _.RC:$src1, MaskRC:$mask, memop:$src2),
            !strconcat(OpcodeStr#_.Suffix,
            "\t{$src2, ${dst} {${mask}}|${dst} {${mask}}, $src2}"),
            [(set _.RC:$dst, MaskRC:$mask_wb,
              (GatherNode  (_.VT _.RC:$src1), MaskRC:$mask,
                     vectoraddr:$src2))]>, EVEX, EVEX_K,
             EVEX_CD8<_.EltSize, CD8VT1>, Sched<[WriteLoad]>;
}

multiclass avx512_gather_q_pd<bits<8> dopc, bits<8> qopc,
                        AVX512VLVectorVTInfo _, string OpcodeStr, string SUFF> {
  defm NAME##D##SUFF##Z: avx512_gather<dopc, OpcodeStr##"d", _.info512,
                                      vy512xmem, mgatherv8i32>, EVEX_V512, VEX_W;
  defm NAME##Q##SUFF##Z: avx512_gather<qopc, OpcodeStr##"q", _.info512,
                                      vz512mem,  mgatherv8i64>, EVEX_V512, VEX_W;
let Predicates = [HasVLX] in {
  defm NAME##D##SUFF##Z256: avx512_gather<dopc, OpcodeStr##"d", _.info256,
                              vx256xmem, mgatherv4i32>, EVEX_V256, VEX_W;
  defm NAME##Q##SUFF##Z256: avx512_gather<qopc, OpcodeStr##"q", _.info256,
                              vy256xmem, mgatherv4i64>, EVEX_V256, VEX_W;
  defm NAME##D##SUFF##Z128: avx512_gather<dopc, OpcodeStr##"d", _.info128,
                              vx128xmem, mgatherv4i32>, EVEX_V128, VEX_W;
  defm NAME##Q##SUFF##Z128: avx512_gather<qopc, OpcodeStr##"q", _.info128,
                              vx128xmem, mgatherv2i64>, EVEX_V128, VEX_W;
}
}

multiclass avx512_gather_d_ps<bits<8> dopc, bits<8> qopc,
                       AVX512VLVectorVTInfo _, string OpcodeStr, string SUFF> {
  defm NAME##D##SUFF##Z: avx512_gather<dopc, OpcodeStr##"d", _.info512, vz512mem,
                                       mgatherv16i32>, EVEX_V512;
  defm NAME##Q##SUFF##Z: avx512_gather<qopc, OpcodeStr##"q", _.info256, vz256mem,
                                       mgatherv8i64>, EVEX_V512;
let Predicates = [HasVLX] in {
  defm NAME##D##SUFF##Z256: avx512_gather<dopc, OpcodeStr##"d", _.info256,
                                          vy256xmem, mgatherv8i32>, EVEX_V256;
  defm NAME##Q##SUFF##Z256: avx512_gather<qopc, OpcodeStr##"q", _.info128,
                                          vy128xmem, mgatherv4i64>, EVEX_V256;
  defm NAME##D##SUFF##Z128: avx512_gather<dopc, OpcodeStr##"d", _.info128,
                                          vx128xmem, mgatherv4i32>, EVEX_V128;
  defm NAME##Q##SUFF##Z128: avx512_gather<qopc, OpcodeStr##"q", _.info128,
                                          vx64xmem, mgatherv2i64, VK2WM>,
                                          EVEX_V128;
}
}


defm VGATHER : avx512_gather_q_pd<0x92, 0x93, avx512vl_f64_info, "vgather", "PD">,
               avx512_gather_d_ps<0x92, 0x93, avx512vl_f32_info, "vgather", "PS">;

defm VPGATHER : avx512_gather_q_pd<0x90, 0x91, avx512vl_i64_info, "vpgather", "Q">,
                avx512_gather_d_ps<0x90, 0x91, avx512vl_i32_info, "vpgather", "D">;

multiclass avx512_scatter<bits<8> opc, string OpcodeStr, X86VectorVTInfo _,
                          X86MemOperand memop, PatFrag ScatterNode,
                          RegisterClass MaskRC = _.KRCWM> {

let mayStore = 1, Constraints = "$mask = $mask_wb", ExeDomain = _.ExeDomain in

  def mr  : AVX5128I<opc, MRMDestMem, (outs MaskRC:$mask_wb),
            (ins memop:$dst, MaskRC:$mask, _.RC:$src),
            !strconcat(OpcodeStr#_.Suffix,
            "\t{$src, ${dst} {${mask}}|${dst} {${mask}}, $src}"),
            [(set MaskRC:$mask_wb, (ScatterNode (_.VT _.RC:$src),
                                    MaskRC:$mask,  vectoraddr:$dst))]>,
            EVEX, EVEX_K, EVEX_CD8<_.EltSize, CD8VT1>,
            Sched<[WriteStore]>;
}

multiclass avx512_scatter_q_pd<bits<8> dopc, bits<8> qopc,
                        AVX512VLVectorVTInfo _, string OpcodeStr, string SUFF> {
  defm NAME##D##SUFF##Z: avx512_scatter<dopc, OpcodeStr##"d", _.info512,
                                      vy512xmem, mscatterv8i32>, EVEX_V512, VEX_W;
  defm NAME##Q##SUFF##Z: avx512_scatter<qopc, OpcodeStr##"q", _.info512,
                                      vz512mem,  mscatterv8i64>, EVEX_V512, VEX_W;
let Predicates = [HasVLX] in {
  defm NAME##D##SUFF##Z256: avx512_scatter<dopc, OpcodeStr##"d", _.info256,
                              vx256xmem, mscatterv4i32>, EVEX_V256, VEX_W;
  defm NAME##Q##SUFF##Z256: avx512_scatter<qopc, OpcodeStr##"q", _.info256,
                              vy256xmem, mscatterv4i64>, EVEX_V256, VEX_W;
  defm NAME##D##SUFF##Z128: avx512_scatter<dopc, OpcodeStr##"d", _.info128,
                              vx128xmem, mscatterv4i32>, EVEX_V128, VEX_W;
  defm NAME##Q##SUFF##Z128: avx512_scatter<qopc, OpcodeStr##"q", _.info128,
                              vx128xmem, mscatterv2i64>, EVEX_V128, VEX_W;
}
}

multiclass avx512_scatter_d_ps<bits<8> dopc, bits<8> qopc,
                       AVX512VLVectorVTInfo _, string OpcodeStr, string SUFF> {
  defm NAME##D##SUFF##Z: avx512_scatter<dopc, OpcodeStr##"d", _.info512, vz512mem,
                                       mscatterv16i32>, EVEX_V512;
  defm NAME##Q##SUFF##Z: avx512_scatter<qopc, OpcodeStr##"q", _.info256, vz256mem,
                                       mscatterv8i64>, EVEX_V512;
let Predicates = [HasVLX] in {
  defm NAME##D##SUFF##Z256: avx512_scatter<dopc, OpcodeStr##"d", _.info256,
                                          vy256xmem, mscatterv8i32>, EVEX_V256;
  defm NAME##Q##SUFF##Z256: avx512_scatter<qopc, OpcodeStr##"q", _.info128,
                                          vy128xmem, mscatterv4i64>, EVEX_V256;
  defm NAME##D##SUFF##Z128: avx512_scatter<dopc, OpcodeStr##"d", _.info128,
                                          vx128xmem, mscatterv4i32>, EVEX_V128;
  defm NAME##Q##SUFF##Z128: avx512_scatter<qopc, OpcodeStr##"q", _.info128,
                                          vx64xmem, mscatterv2i64, VK2WM>,
                                          EVEX_V128;
}
}

defm VSCATTER : avx512_scatter_q_pd<0xA2, 0xA3, avx512vl_f64_info, "vscatter", "PD">,
               avx512_scatter_d_ps<0xA2, 0xA3, avx512vl_f32_info, "vscatter", "PS">;

defm VPSCATTER : avx512_scatter_q_pd<0xA0, 0xA1, avx512vl_i64_info, "vpscatter", "Q">,
                avx512_scatter_d_ps<0xA0, 0xA1, avx512vl_i32_info, "vpscatter", "D">;

// prefetch
multiclass avx512_gather_scatter_prefetch<bits<8> opc, Format F, string OpcodeStr,
                       RegisterClass KRC, X86MemOperand memop> {
  let Predicates = [HasPFI], mayLoad = 1, mayStore = 1 in
  def m  : AVX5128I<opc, F, (outs), (ins KRC:$mask, memop:$src),
            !strconcat(OpcodeStr, "\t{$src {${mask}}|{${mask}}, $src}"), []>,
            EVEX, EVEX_K, Sched<[WriteLoad]>;
}

defm VGATHERPF0DPS: avx512_gather_scatter_prefetch<0xC6, MRM1m, "vgatherpf0dps",
                     VK16WM, vz512mem>, EVEX_V512, EVEX_CD8<32, CD8VT1>;

defm VGATHERPF0QPS: avx512_gather_scatter_prefetch<0xC7, MRM1m, "vgatherpf0qps",
                     VK8WM, vz256mem>, EVEX_V512, EVEX_CD8<64, CD8VT1>;

defm VGATHERPF0DPD: avx512_gather_scatter_prefetch<0xC6, MRM1m, "vgatherpf0dpd",
                     VK8WM, vy512xmem>, EVEX_V512, VEX_W, EVEX_CD8<32, CD8VT1>;

defm VGATHERPF0QPD: avx512_gather_scatter_prefetch<0xC7, MRM1m, "vgatherpf0qpd",
                     VK8WM, vz512mem>, EVEX_V512, VEX_W, EVEX_CD8<64, CD8VT1>;

defm VGATHERPF1DPS: avx512_gather_scatter_prefetch<0xC6, MRM2m, "vgatherpf1dps",
                     VK16WM, vz512mem>, EVEX_V512, EVEX_CD8<32, CD8VT1>;

defm VGATHERPF1QPS: avx512_gather_scatter_prefetch<0xC7, MRM2m, "vgatherpf1qps",
                     VK8WM, vz256mem>, EVEX_V512, EVEX_CD8<64, CD8VT1>;

defm VGATHERPF1DPD: avx512_gather_scatter_prefetch<0xC6, MRM2m, "vgatherpf1dpd",
                     VK8WM, vy512xmem>, EVEX_V512, VEX_W, EVEX_CD8<32, CD8VT1>;

defm VGATHERPF1QPD: avx512_gather_scatter_prefetch<0xC7, MRM2m, "vgatherpf1qpd",
                     VK8WM, vz512mem>, EVEX_V512, VEX_W, EVEX_CD8<64, CD8VT1>;

defm VSCATTERPF0DPS: avx512_gather_scatter_prefetch<0xC6, MRM5m, "vscatterpf0dps",
                     VK16WM, vz512mem>, EVEX_V512, EVEX_CD8<32, CD8VT1>;

defm VSCATTERPF0QPS: avx512_gather_scatter_prefetch<0xC7, MRM5m, "vscatterpf0qps",
                     VK8WM, vz256mem>, EVEX_V512, EVEX_CD8<64, CD8VT1>;

defm VSCATTERPF0DPD: avx512_gather_scatter_prefetch<0xC6, MRM5m, "vscatterpf0dpd",
                     VK8WM, vy512xmem>, EVEX_V512, VEX_W, EVEX_CD8<32, CD8VT1>;

defm VSCATTERPF0QPD: avx512_gather_scatter_prefetch<0xC7, MRM5m, "vscatterpf0qpd",
                     VK8WM, vz512mem>, EVEX_V512, VEX_W, EVEX_CD8<64, CD8VT1>;

defm VSCATTERPF1DPS: avx512_gather_scatter_prefetch<0xC6, MRM6m, "vscatterpf1dps",
                     VK16WM, vz512mem>, EVEX_V512, EVEX_CD8<32, CD8VT1>;

defm VSCATTERPF1QPS: avx512_gather_scatter_prefetch<0xC7, MRM6m, "vscatterpf1qps",
                     VK8WM, vz256mem>, EVEX_V512, EVEX_CD8<64, CD8VT1>;

defm VSCATTERPF1DPD: avx512_gather_scatter_prefetch<0xC6, MRM6m, "vscatterpf1dpd",
                     VK8WM, vy512xmem>, EVEX_V512, VEX_W, EVEX_CD8<32, CD8VT1>;

defm VSCATTERPF1QPD: avx512_gather_scatter_prefetch<0xC7, MRM6m, "vscatterpf1qpd",
                     VK8WM, vz512mem>, EVEX_V512, VEX_W, EVEX_CD8<64, CD8VT1>;

multiclass cvt_by_vec_width<bits<8> opc, X86VectorVTInfo Vec, string OpcodeStr > {
def rr : AVX512XS8I<opc, MRMSrcReg, (outs Vec.RC:$dst), (ins Vec.KRC:$src),
                  !strconcat(OpcodeStr##Vec.Suffix, "\t{$src, $dst|$dst, $src}"),
                  [(set Vec.RC:$dst, (Vec.VT (sext Vec.KRC:$src)))]>,
                  EVEX, Sched<[WriteMove]>; // TODO - WriteVecTrunc?

// Also need a pattern for anyextend.
def : Pat<(Vec.VT (anyext Vec.KRC:$src)),
          (!cast<Instruction>(NAME#"rr") Vec.KRC:$src)>;
}

multiclass cvt_mask_by_elt_width<bits<8> opc, AVX512VLVectorVTInfo VTInfo,
                                 string OpcodeStr, Predicate prd> {
let Predicates = [prd] in
  defm Z : cvt_by_vec_width<opc, VTInfo.info512, OpcodeStr>, EVEX_V512;

  let Predicates = [prd, HasVLX] in {
    defm Z256 : cvt_by_vec_width<opc, VTInfo.info256, OpcodeStr>, EVEX_V256;
    defm Z128 : cvt_by_vec_width<opc, VTInfo.info128, OpcodeStr>, EVEX_V128;
  }
}

defm VPMOVM2B : cvt_mask_by_elt_width<0x28, avx512vl_i8_info, "vpmovm2" , HasBWI>;
defm VPMOVM2W : cvt_mask_by_elt_width<0x28, avx512vl_i16_info, "vpmovm2", HasBWI> , VEX_W;
defm VPMOVM2D : cvt_mask_by_elt_width<0x38, avx512vl_i32_info, "vpmovm2", HasDQI>;
defm VPMOVM2Q : cvt_mask_by_elt_width<0x38, avx512vl_i64_info, "vpmovm2", HasDQI> , VEX_W;

multiclass convert_vector_to_mask_common<bits<8> opc, X86VectorVTInfo _, string OpcodeStr > {
    def rr : AVX512XS8I<opc, MRMSrcReg, (outs _.KRC:$dst), (ins _.RC:$src),
                        !strconcat(OpcodeStr, "\t{$src, $dst|$dst, $src}"),
                        [(set _.KRC:$dst, (X86pcmpgtm _.ImmAllZerosV, (_.VT _.RC:$src)))]>,
                        EVEX, Sched<[WriteMove]>;
}

// Use 512bit version to implement 128/256 bit in case NoVLX.
multiclass convert_vector_to_mask_lowering<X86VectorVTInfo ExtendInfo,
                                           X86VectorVTInfo _,
                                           string Name> {

  def : Pat<(_.KVT (X86pcmpgtm _.ImmAllZerosV, (_.VT _.RC:$src))),
            (_.KVT (COPY_TO_REGCLASS
                     (!cast<Instruction>(Name#"Zrr")
                       (INSERT_SUBREG (ExtendInfo.VT (IMPLICIT_DEF)),
                                      _.RC:$src, _.SubRegIdx)),
                   _.KRC))>;
}

multiclass avx512_convert_vector_to_mask<bits<8> opc, string OpcodeStr,
                                   AVX512VLVectorVTInfo VTInfo, Predicate prd> {
  let Predicates = [prd] in
    defm Z : convert_vector_to_mask_common <opc, VTInfo.info512, OpcodeStr>,
                                            EVEX_V512;

  let Predicates = [prd, HasVLX] in {
    defm Z256 : convert_vector_to_mask_common<opc, VTInfo.info256, OpcodeStr>,
                                              EVEX_V256;
    defm Z128 : convert_vector_to_mask_common<opc, VTInfo.info128, OpcodeStr>,
                                               EVEX_V128;
  }
  let Predicates = [prd, NoVLX] in {
    defm Z256_Alt : convert_vector_to_mask_lowering<VTInfo.info512, VTInfo.info256, NAME>;
    defm Z128_Alt : convert_vector_to_mask_lowering<VTInfo.info512, VTInfo.info128, NAME>;
  }
}

defm VPMOVB2M : avx512_convert_vector_to_mask<0x29, "vpmovb2m",
                                              avx512vl_i8_info, HasBWI>;
defm VPMOVW2M : avx512_convert_vector_to_mask<0x29, "vpmovw2m",
                                              avx512vl_i16_info, HasBWI>, VEX_W;
defm VPMOVD2M : avx512_convert_vector_to_mask<0x39, "vpmovd2m",
                                              avx512vl_i32_info, HasDQI>;
defm VPMOVQ2M : avx512_convert_vector_to_mask<0x39, "vpmovq2m",
                                              avx512vl_i64_info, HasDQI>, VEX_W;

// Patterns for handling sext from a mask register to v16i8/v16i16 when DQI
// is available, but BWI is not. We can't handle this in lowering because
// a target independent DAG combine likes to combine sext and trunc.
let Predicates = [HasDQI, NoBWI] in {
  def : Pat<(v16i8 (sext (v16i1 VK16:$src))),
            (VPMOVDBZrr (v16i32 (VPMOVM2DZrr VK16:$src)))>;
  def : Pat<(v16i16 (sext (v16i1 VK16:$src))),
            (VPMOVDWZrr (v16i32 (VPMOVM2DZrr VK16:$src)))>;

  def : Pat<(v16i8 (anyext (v16i1 VK16:$src))),
            (VPMOVDBZrr (v16i32 (VPMOVM2DZrr VK16:$src)))>;
  def : Pat<(v16i16 (anyext (v16i1 VK16:$src))),
            (VPMOVDWZrr (v16i32 (VPMOVM2DZrr VK16:$src)))>;
}

let Predicates = [HasDQI, NoBWI, HasVLX] in {
  def : Pat<(v8i16 (sext (v8i1 VK8:$src))),
            (VPMOVDWZ256rr (v8i32 (VPMOVM2DZ256rr VK8:$src)))>;

  def : Pat<(v8i16 (anyext (v8i1 VK8:$src))),
            (VPMOVDWZ256rr (v8i32 (VPMOVM2DZ256rr VK8:$src)))>;
}

//===----------------------------------------------------------------------===//
// AVX-512 - COMPRESS and EXPAND
//

multiclass compress_by_vec_width_common<bits<8> opc, X86VectorVTInfo _,
                                 string OpcodeStr, X86FoldableSchedWrite sched> {
  defm rr : AVX512_maskable<opc, MRMDestReg, _, (outs _.RC:$dst),
              (ins _.RC:$src1), OpcodeStr, "$src1", "$src1",
              (_.VT (X86compress _.RC:$src1))>, AVX5128IBase,
              Sched<[sched]>;

  let mayStore = 1, hasSideEffects = 0 in
  def mr : AVX5128I<opc, MRMDestMem, (outs),
              (ins _.MemOp:$dst, _.RC:$src),
              OpcodeStr # "\t{$src, $dst|$dst, $src}",
              []>, EVEX_CD8<_.EltSize, CD8VT1>,
              Sched<[sched.Folded]>;

  def mrk : AVX5128I<opc, MRMDestMem, (outs),
              (ins _.MemOp:$dst, _.KRCWM:$mask, _.RC:$src),
              OpcodeStr # "\t{$src, $dst {${mask}}|$dst {${mask}}, $src}",
              []>,
              EVEX_K, EVEX_CD8<_.EltSize, CD8VT1>,
              Sched<[sched.Folded]>;
}

multiclass compress_by_vec_width_lowering<X86VectorVTInfo _, string Name> {
  def : Pat<(X86mCompressingStore (_.VT _.RC:$src), addr:$dst, _.KRCWM:$mask),
            (!cast<Instruction>(Name#_.ZSuffix##mrk)
                            addr:$dst, _.KRCWM:$mask, _.RC:$src)>;
}

multiclass compress_by_elt_width<bits<8> opc, string OpcodeStr,
                                 X86FoldableSchedWrite sched,
                                 AVX512VLVectorVTInfo VTInfo,
                                 Predicate Pred = HasAVX512> {
  let Predicates = [Pred] in
  defm Z : compress_by_vec_width_common<opc, VTInfo.info512, OpcodeStr, sched>,
           compress_by_vec_width_lowering<VTInfo.info512, NAME>, EVEX_V512;

  let Predicates = [Pred, HasVLX] in {
    defm Z256 : compress_by_vec_width_common<opc, VTInfo.info256, OpcodeStr, sched>,
                compress_by_vec_width_lowering<VTInfo.info256, NAME>, EVEX_V256;
    defm Z128 : compress_by_vec_width_common<opc, VTInfo.info128, OpcodeStr, sched>,
                compress_by_vec_width_lowering<VTInfo.info128, NAME>, EVEX_V128;
  }
}

// FIXME: Is there a better scheduler class for VPCOMPRESS?
defm VPCOMPRESSD : compress_by_elt_width <0x8B, "vpcompressd", WriteVarShuffle256,
                                          avx512vl_i32_info>, EVEX, NotMemoryFoldable;
defm VPCOMPRESSQ : compress_by_elt_width <0x8B, "vpcompressq", WriteVarShuffle256,
                                          avx512vl_i64_info>, EVEX, VEX_W, NotMemoryFoldable;
defm VCOMPRESSPS : compress_by_elt_width <0x8A, "vcompressps", WriteVarShuffle256,
                                          avx512vl_f32_info>, EVEX, NotMemoryFoldable;
defm VCOMPRESSPD : compress_by_elt_width <0x8A, "vcompresspd", WriteVarShuffle256,
                                          avx512vl_f64_info>, EVEX, VEX_W, NotMemoryFoldable;

// expand
multiclass expand_by_vec_width<bits<8> opc, X86VectorVTInfo _,
                                 string OpcodeStr, X86FoldableSchedWrite sched> {
  defm rr : AVX512_maskable<opc, MRMSrcReg, _, (outs _.RC:$dst),
              (ins _.RC:$src1), OpcodeStr, "$src1", "$src1",
              (_.VT (X86expand _.RC:$src1))>, AVX5128IBase,
              Sched<[sched]>;

  defm rm : AVX512_maskable<opc, MRMSrcMem, _, (outs _.RC:$dst),
              (ins _.MemOp:$src1), OpcodeStr, "$src1", "$src1",
              (_.VT (X86expand (_.VT (bitconvert
                                      (_.LdFrag addr:$src1)))))>,
            AVX5128IBase, EVEX_CD8<_.EltSize, CD8VT1>,
            Sched<[sched.Folded, sched.ReadAfterFold]>;
}

multiclass expand_by_vec_width_lowering<X86VectorVTInfo _, string Name> {

  def : Pat<(_.VT (X86mExpandingLoad addr:$src, _.KRCWM:$mask, undef)),
            (!cast<Instruction>(Name#_.ZSuffix##rmkz)
                                        _.KRCWM:$mask, addr:$src)>;

  def : Pat<(_.VT (X86mExpandingLoad addr:$src, _.KRCWM:$mask, _.ImmAllZerosV)),
            (!cast<Instruction>(Name#_.ZSuffix##rmkz)
                                        _.KRCWM:$mask, addr:$src)>;

  def : Pat<(_.VT (X86mExpandingLoad addr:$src, _.KRCWM:$mask,
                                               (_.VT _.RC:$src0))),
            (!cast<Instruction>(Name#_.ZSuffix##rmk)
                            _.RC:$src0, _.KRCWM:$mask, addr:$src)>;
}

multiclass expand_by_elt_width<bits<8> opc, string OpcodeStr,
                               X86FoldableSchedWrite sched,
                               AVX512VLVectorVTInfo VTInfo,
                               Predicate Pred = HasAVX512> {
  let Predicates = [Pred] in
  defm Z : expand_by_vec_width<opc, VTInfo.info512, OpcodeStr, sched>,
           expand_by_vec_width_lowering<VTInfo.info512, NAME>, EVEX_V512;

  let Predicates = [Pred, HasVLX] in {
    defm Z256 : expand_by_vec_width<opc, VTInfo.info256, OpcodeStr, sched>,
                expand_by_vec_width_lowering<VTInfo.info256, NAME>, EVEX_V256;
    defm Z128 : expand_by_vec_width<opc, VTInfo.info128, OpcodeStr, sched>,
                expand_by_vec_width_lowering<VTInfo.info128, NAME>, EVEX_V128;
  }
}

// FIXME: Is there a better scheduler class for VPEXPAND?
defm VPEXPANDD : expand_by_elt_width <0x89, "vpexpandd", WriteVarShuffle256,
                                      avx512vl_i32_info>, EVEX;
defm VPEXPANDQ : expand_by_elt_width <0x89, "vpexpandq", WriteVarShuffle256,
                                      avx512vl_i64_info>, EVEX, VEX_W;
defm VEXPANDPS : expand_by_elt_width <0x88, "vexpandps", WriteVarShuffle256,
                                      avx512vl_f32_info>, EVEX;
defm VEXPANDPD : expand_by_elt_width <0x88, "vexpandpd", WriteVarShuffle256,
                                      avx512vl_f64_info>, EVEX, VEX_W;

//handle instruction  reg_vec1 = op(reg_vec,imm)
//                               op(mem_vec,imm)
//                               op(broadcast(eltVt),imm)
//all instruction created with FROUND_CURRENT
multiclass avx512_unary_fp_packed_imm<bits<8> opc, string OpcodeStr, SDNode OpNode,
                                      X86FoldableSchedWrite sched, X86VectorVTInfo _> {
  let ExeDomain = _.ExeDomain in {
  defm rri : AVX512_maskable<opc, MRMSrcReg, _, (outs _.RC:$dst),
                      (ins _.RC:$src1, i32u8imm:$src2),
                      OpcodeStr##_.Suffix, "$src2, $src1", "$src1, $src2",
                      (OpNode (_.VT _.RC:$src1),
                              (i32 imm:$src2))>, Sched<[sched]>;
  defm rmi : AVX512_maskable<opc, MRMSrcMem, _, (outs _.RC:$dst),
                    (ins _.MemOp:$src1, i32u8imm:$src2),
                    OpcodeStr##_.Suffix, "$src2, $src1", "$src1, $src2",
                    (OpNode (_.VT (bitconvert (_.LdFrag addr:$src1))),
                            (i32 imm:$src2))>,
                    Sched<[sched.Folded, sched.ReadAfterFold]>;
  defm rmbi : AVX512_maskable<opc, MRMSrcMem, _, (outs _.RC:$dst),
                    (ins _.ScalarMemOp:$src1, i32u8imm:$src2),
                    OpcodeStr##_.Suffix, "$src2, ${src1}"##_.BroadcastStr,
                    "${src1}"##_.BroadcastStr##", $src2",
                    (OpNode (_.VT (X86VBroadcast(_.ScalarLdFrag addr:$src1))),
                            (i32 imm:$src2))>, EVEX_B,
                    Sched<[sched.Folded, sched.ReadAfterFold]>;
  }
}

//handle instruction  reg_vec1 = op(reg_vec2,reg_vec3,imm),{sae}
multiclass avx512_unary_fp_sae_packed_imm<bits<8> opc, string OpcodeStr,
                                          SDNode OpNode, X86FoldableSchedWrite sched,
                                          X86VectorVTInfo _> {
  let ExeDomain = _.ExeDomain in
  defm rrib : AVX512_maskable<opc, MRMSrcReg, _, (outs _.RC:$dst),
                      (ins _.RC:$src1, i32u8imm:$src2),
                      OpcodeStr##_.Suffix, "$src2, {sae}, $src1",
                      "$src1, {sae}, $src2",
                      (OpNode (_.VT _.RC:$src1),
                              (i32 imm:$src2),
                              (i32 FROUND_NO_EXC))>,
                      EVEX_B, Sched<[sched]>;
}

multiclass avx512_common_unary_fp_sae_packed_imm<string OpcodeStr,
            AVX512VLVectorVTInfo _, bits<8> opc, SDNode OpNode,
            SDNode OpNodeRnd, X86SchedWriteWidths sched, Predicate prd>{
  let Predicates = [prd] in {
    defm Z    : avx512_unary_fp_packed_imm<opc, OpcodeStr, OpNode, sched.ZMM,
                                           _.info512>,
                avx512_unary_fp_sae_packed_imm<opc, OpcodeStr, OpNodeRnd,
                                               sched.ZMM, _.info512>, EVEX_V512;
  }
  let Predicates = [prd, HasVLX] in {
    defm Z128 : avx512_unary_fp_packed_imm<opc, OpcodeStr, OpNode, sched.XMM,
                                           _.info128>, EVEX_V128;
    defm Z256 : avx512_unary_fp_packed_imm<opc, OpcodeStr, OpNode, sched.YMM,
                                           _.info256>, EVEX_V256;
  }
}

//handle instruction  reg_vec1 = op(reg_vec2,reg_vec3,imm)
//                               op(reg_vec2,mem_vec,imm)
//                               op(reg_vec2,broadcast(eltVt),imm)
//all instruction created with FROUND_CURRENT
multiclass avx512_fp_packed_imm<bits<8> opc, string OpcodeStr, SDNode OpNode,
                                X86FoldableSchedWrite sched, X86VectorVTInfo _>{
  let ExeDomain = _.ExeDomain in {
  defm rri : AVX512_maskable<opc, MRMSrcReg, _, (outs _.RC:$dst),
                      (ins _.RC:$src1, _.RC:$src2, i32u8imm:$src3),
                      OpcodeStr, "$src3, $src2, $src1", "$src1, $src2, $src3",
                      (OpNode (_.VT _.RC:$src1),
                              (_.VT _.RC:$src2),
                              (i32 imm:$src3))>,
                      Sched<[sched]>;
  defm rmi : AVX512_maskable<opc, MRMSrcMem, _, (outs _.RC:$dst),
                    (ins _.RC:$src1, _.MemOp:$src2, i32u8imm:$src3),
                    OpcodeStr, "$src3, $src2, $src1", "$src1, $src2, $src3",
                    (OpNode (_.VT _.RC:$src1),
                            (_.VT (bitconvert (_.LdFrag addr:$src2))),
                            (i32 imm:$src3))>,
                    Sched<[sched.Folded, sched.ReadAfterFold]>;
  defm rmbi : AVX512_maskable<opc, MRMSrcMem, _, (outs _.RC:$dst),
                    (ins _.RC:$src1, _.ScalarMemOp:$src2, i32u8imm:$src3),
                    OpcodeStr, "$src3, ${src2}"##_.BroadcastStr##", $src1",
                    "$src1, ${src2}"##_.BroadcastStr##", $src3",
                    (OpNode (_.VT _.RC:$src1),
                            (_.VT (X86VBroadcast(_.ScalarLdFrag addr:$src2))),
                            (i32 imm:$src3))>, EVEX_B,
                    Sched<[sched.Folded, sched.ReadAfterFold]>;
  }
}

//handle instruction  reg_vec1 = op(reg_vec2,reg_vec3,imm)
//                               op(reg_vec2,mem_vec,imm)
multiclass avx512_3Op_rm_imm8<bits<8> opc, string OpcodeStr, SDNode OpNode,
                              X86FoldableSchedWrite sched, X86VectorVTInfo DestInfo,
                              X86VectorVTInfo SrcInfo>{
  let ExeDomain = DestInfo.ExeDomain in {
  defm rri : AVX512_maskable<opc, MRMSrcReg, DestInfo, (outs DestInfo.RC:$dst),
                  (ins SrcInfo.RC:$src1, SrcInfo.RC:$src2, u8imm:$src3),
                  OpcodeStr, "$src3, $src2, $src1", "$src1, $src2, $src3",
                  (DestInfo.VT (OpNode (SrcInfo.VT SrcInfo.RC:$src1),
                               (SrcInfo.VT SrcInfo.RC:$src2),
                               (i8 imm:$src3)))>,
                  Sched<[sched]>;
  defm rmi : AVX512_maskable<opc, MRMSrcMem, DestInfo, (outs DestInfo.RC:$dst),
                (ins SrcInfo.RC:$src1, SrcInfo.MemOp:$src2, u8imm:$src3),
                OpcodeStr, "$src3, $src2, $src1", "$src1, $src2, $src3",
                (DestInfo.VT (OpNode (SrcInfo.VT SrcInfo.RC:$src1),
                             (SrcInfo.VT (bitconvert
                                                (SrcInfo.LdFrag addr:$src2))),
                             (i8 imm:$src3)))>,
                Sched<[sched.Folded, sched.ReadAfterFold]>;
  }
}

//handle instruction  reg_vec1 = op(reg_vec2,reg_vec3,imm)
//                               op(reg_vec2,mem_vec,imm)
//                               op(reg_vec2,broadcast(eltVt),imm)
multiclass avx512_3Op_imm8<bits<8> opc, string OpcodeStr, SDNode OpNode,
                           X86FoldableSchedWrite sched, X86VectorVTInfo _>:
  avx512_3Op_rm_imm8<opc, OpcodeStr, OpNode, sched, _, _>{

  let ExeDomain = _.ExeDomain in
  defm rmbi : AVX512_maskable<opc, MRMSrcMem, _, (outs _.RC:$dst),
                    (ins _.RC:$src1, _.ScalarMemOp:$src2, u8imm:$src3),
                    OpcodeStr, "$src3, ${src2}"##_.BroadcastStr##", $src1",
                    "$src1, ${src2}"##_.BroadcastStr##", $src3",
                    (OpNode (_.VT _.RC:$src1),
                            (_.VT (X86VBroadcast(_.ScalarLdFrag addr:$src2))),
                            (i8 imm:$src3))>, EVEX_B,
                    Sched<[sched.Folded, sched.ReadAfterFold]>;
}

//handle scalar instruction  reg_vec1 = op(reg_vec2,reg_vec3,imm)
//                                      op(reg_vec2,mem_scalar,imm)
multiclass avx512_fp_scalar_imm<bits<8> opc, string OpcodeStr, SDNode OpNode,
                                X86FoldableSchedWrite sched, X86VectorVTInfo _> {
  let ExeDomain = _.ExeDomain in {
  defm rri : AVX512_maskable_scalar<opc, MRMSrcReg, _, (outs _.RC:$dst),
                      (ins _.RC:$src1, _.RC:$src2, i32u8imm:$src3),
                      OpcodeStr, "$src3, $src2, $src1", "$src1, $src2, $src3",
                      (OpNode (_.VT _.RC:$src1),
                              (_.VT _.RC:$src2),
                              (i32 imm:$src3))>,
                      Sched<[sched]>;
  defm rmi : AVX512_maskable_scalar<opc, MRMSrcMem, _, (outs _.RC:$dst),
                    (ins _.RC:$src1, _.ScalarMemOp:$src2, i32u8imm:$src3),
                    OpcodeStr, "$src3, $src2, $src1", "$src1, $src2, $src3",
                    (OpNode (_.VT _.RC:$src1),
                            (_.VT (scalar_to_vector
                                      (_.ScalarLdFrag addr:$src2))),
                            (i32 imm:$src3))>,
                    Sched<[sched.Folded, sched.ReadAfterFold]>;
  }
}

//handle instruction  reg_vec1 = op(reg_vec2,reg_vec3,imm),{sae}
multiclass avx512_fp_sae_packed_imm<bits<8> opc, string OpcodeStr,
                                    SDNode OpNode, X86FoldableSchedWrite sched,
                                    X86VectorVTInfo _> {
  let ExeDomain = _.ExeDomain in
  defm rrib : AVX512_maskable<opc, MRMSrcReg, _, (outs _.RC:$dst),
                      (ins _.RC:$src1, _.RC:$src2, i32u8imm:$src3),
                      OpcodeStr, "$src3, {sae}, $src2, $src1",
                      "$src1, $src2, {sae}, $src3",
                      (OpNode (_.VT _.RC:$src1),
                              (_.VT _.RC:$src2),
                              (i32 imm:$src3),
                              (i32 FROUND_NO_EXC))>,
                      EVEX_B, Sched<[sched]>;
}

//handle scalar instruction  reg_vec1 = op(reg_vec2,reg_vec3,imm),{sae}
multiclass avx512_fp_sae_scalar_imm<bits<8> opc, string OpcodeStr, SDNode OpNode,
                                    X86FoldableSchedWrite sched, X86VectorVTInfo _> {
  let ExeDomain = _.ExeDomain in
  defm NAME#rrib : AVX512_maskable_scalar<opc, MRMSrcReg, _, (outs _.RC:$dst),
                      (ins _.RC:$src1, _.RC:$src2, i32u8imm:$src3),
                      OpcodeStr, "$src3, {sae}, $src2, $src1",
                      "$src1, $src2, {sae}, $src3",
                      (OpNode (_.VT _.RC:$src1),
                              (_.VT _.RC:$src2),
                              (i32 imm:$src3),
                              (i32 FROUND_NO_EXC))>,
                      EVEX_B, Sched<[sched]>;
}

multiclass avx512_common_fp_sae_packed_imm<string OpcodeStr,
            AVX512VLVectorVTInfo _, bits<8> opc, SDNode OpNode,
            SDNode OpNodeRnd, X86SchedWriteWidths sched, Predicate prd>{
  let Predicates = [prd] in {
    defm Z    : avx512_fp_packed_imm<opc, OpcodeStr, OpNode, sched.ZMM, _.info512>,
                avx512_fp_sae_packed_imm<opc, OpcodeStr, OpNodeRnd, sched.ZMM, _.info512>,
                                  EVEX_V512;

  }
  let Predicates = [prd, HasVLX] in {
    defm Z128 : avx512_fp_packed_imm<opc, OpcodeStr, OpNode, sched.XMM, _.info128>,
                                  EVEX_V128;
    defm Z256 : avx512_fp_packed_imm<opc, OpcodeStr, OpNode, sched.YMM, _.info256>,
                                  EVEX_V256;
  }
}

multiclass avx512_common_3Op_rm_imm8<bits<8> opc, SDNode OpNode, string OpStr,
                   X86SchedWriteWidths sched, AVX512VLVectorVTInfo DestInfo,
                   AVX512VLVectorVTInfo SrcInfo, Predicate Pred = HasBWI> {
  let Predicates = [Pred] in {
    defm Z    : avx512_3Op_rm_imm8<opc, OpStr, OpNode, sched.ZMM, DestInfo.info512,
                           SrcInfo.info512>, EVEX_V512, AVX512AIi8Base, EVEX_4V;
  }
  let Predicates = [Pred, HasVLX] in {
    defm Z128 : avx512_3Op_rm_imm8<opc, OpStr, OpNode, sched.XMM, DestInfo.info128,
                           SrcInfo.info128>, EVEX_V128, AVX512AIi8Base, EVEX_4V;
    defm Z256 : avx512_3Op_rm_imm8<opc, OpStr, OpNode, sched.YMM, DestInfo.info256,
                           SrcInfo.info256>, EVEX_V256, AVX512AIi8Base, EVEX_4V;
  }
}

multiclass avx512_common_3Op_imm8<string OpcodeStr, AVX512VLVectorVTInfo _,
                                  bits<8> opc, SDNode OpNode, X86SchedWriteWidths sched,
                                  Predicate Pred = HasAVX512> {
  let Predicates = [Pred] in {
    defm Z    : avx512_3Op_imm8<opc, OpcodeStr, OpNode, sched.ZMM, _.info512>,
                                EVEX_V512;
  }
  let Predicates = [Pred, HasVLX] in {
    defm Z128 : avx512_3Op_imm8<opc, OpcodeStr, OpNode, sched.XMM, _.info128>,
                                EVEX_V128;
    defm Z256 : avx512_3Op_imm8<opc, OpcodeStr, OpNode, sched.YMM, _.info256>,
                                EVEX_V256;
  }
}

multiclass avx512_common_fp_sae_scalar_imm<string OpcodeStr,
                  X86VectorVTInfo _, bits<8> opc, SDNode OpNode,
                  SDNode OpNodeRnd, X86SchedWriteWidths sched, Predicate prd> {
  let Predicates = [prd] in {
     defm Z : avx512_fp_scalar_imm<opc, OpcodeStr, OpNode, sched.XMM, _>,
              avx512_fp_sae_scalar_imm<opc, OpcodeStr, OpNodeRnd, sched.XMM, _>;
  }
}

multiclass avx512_common_unary_fp_sae_packed_imm_all<string OpcodeStr,
                    bits<8> opcPs, bits<8> opcPd, SDNode OpNode,
                    SDNode OpNodeRnd, X86SchedWriteWidths sched, Predicate prd>{
  defm PS : avx512_common_unary_fp_sae_packed_imm<OpcodeStr, avx512vl_f32_info,
                            opcPs, OpNode, OpNodeRnd, sched, prd>,
                            EVEX_CD8<32, CD8VF>;
  defm PD : avx512_common_unary_fp_sae_packed_imm<OpcodeStr, avx512vl_f64_info,
                            opcPd, OpNode, OpNodeRnd, sched, prd>,
                            EVEX_CD8<64, CD8VF>, VEX_W;
}

defm VREDUCE   : avx512_common_unary_fp_sae_packed_imm_all<"vreduce", 0x56, 0x56,
                              X86VReduce, X86VReduceRnd, SchedWriteFRnd, HasDQI>,
                              AVX512AIi8Base, EVEX;
defm VRNDSCALE : avx512_common_unary_fp_sae_packed_imm_all<"vrndscale", 0x08, 0x09,
                              X86VRndScale, X86VRndScaleRnd, SchedWriteFRnd, HasAVX512>,
                              AVX512AIi8Base, EVEX;
defm VGETMANT : avx512_common_unary_fp_sae_packed_imm_all<"vgetmant", 0x26, 0x26,
                              X86VGetMant, X86VGetMantRnd, SchedWriteFRnd, HasAVX512>,
                              AVX512AIi8Base, EVEX;

defm VRANGEPD : avx512_common_fp_sae_packed_imm<"vrangepd", avx512vl_f64_info,
                                                0x50, X86VRange, X86VRangeRnd,
                                                SchedWriteFAdd, HasDQI>,
      AVX512AIi8Base, EVEX_4V, EVEX_CD8<64, CD8VF>, VEX_W;
defm VRANGEPS : avx512_common_fp_sae_packed_imm<"vrangeps", avx512vl_f32_info,
                                                0x50, X86VRange, X86VRangeRnd,
                                                SchedWriteFAdd, HasDQI>,
      AVX512AIi8Base, EVEX_4V, EVEX_CD8<32, CD8VF>;

defm VRANGESD: avx512_common_fp_sae_scalar_imm<"vrangesd",
      f64x_info, 0x51, X86Ranges, X86RangesRnd, SchedWriteFAdd, HasDQI>,
      AVX512AIi8Base, VEX_LIG, EVEX_4V, EVEX_CD8<64, CD8VT1>, VEX_W;
defm VRANGESS: avx512_common_fp_sae_scalar_imm<"vrangess", f32x_info,
      0x51, X86Ranges, X86RangesRnd, SchedWriteFAdd, HasDQI>,
      AVX512AIi8Base, VEX_LIG, EVEX_4V, EVEX_CD8<32, CD8VT1>;

defm VREDUCESD: avx512_common_fp_sae_scalar_imm<"vreducesd", f64x_info,
      0x57, X86Reduces, X86ReducesRnd, SchedWriteFRnd, HasDQI>,
      AVX512AIi8Base, VEX_LIG, EVEX_4V, EVEX_CD8<64, CD8VT1>, VEX_W;
defm VREDUCESS: avx512_common_fp_sae_scalar_imm<"vreducess", f32x_info,
      0x57, X86Reduces, X86ReducesRnd, SchedWriteFRnd, HasDQI>,
      AVX512AIi8Base, VEX_LIG, EVEX_4V, EVEX_CD8<32, CD8VT1>;

defm VGETMANTSD: avx512_common_fp_sae_scalar_imm<"vgetmantsd", f64x_info,
      0x27, X86GetMants, X86GetMantsRnd, SchedWriteFRnd, HasAVX512>,
      AVX512AIi8Base, VEX_LIG, EVEX_4V, EVEX_CD8<64, CD8VT1>, VEX_W;
defm VGETMANTSS: avx512_common_fp_sae_scalar_imm<"vgetmantss", f32x_info,
      0x27, X86GetMants, X86GetMantsRnd, SchedWriteFRnd, HasAVX512>,
      AVX512AIi8Base, VEX_LIG, EVEX_4V, EVEX_CD8<32, CD8VT1>;


multiclass AVX512_rndscale_lowering<X86VectorVTInfo _, string Suffix> {
  // Register
  def : Pat<(_.VT (ffloor _.RC:$src)),
            (!cast<Instruction>("VRNDSCALE"#Suffix#_.ZSuffix#"rri")
             _.RC:$src, (i32 0x9))>;
  def : Pat<(_.VT (fnearbyint _.RC:$src)),
            (!cast<Instruction>("VRNDSCALE"#Suffix#_.ZSuffix#"rri")
             _.RC:$src, (i32 0xC))>;
  def : Pat<(_.VT (fceil _.RC:$src)),
            (!cast<Instruction>("VRNDSCALE"#Suffix#_.ZSuffix#"rri")
             _.RC:$src, (i32 0xA))>;
  def : Pat<(_.VT (frint _.RC:$src)),
            (!cast<Instruction>("VRNDSCALE"#Suffix#_.ZSuffix#"rri")
             _.RC:$src, (i32 0x4))>;
  def : Pat<(_.VT (ftrunc _.RC:$src)),
            (!cast<Instruction>("VRNDSCALE"#Suffix#_.ZSuffix#"rri")
             _.RC:$src, (i32 0xB))>;

  // Merge-masking
  def : Pat<(_.VT (vselect _.KRCWM:$mask, (ffloor _.RC:$src), _.RC:$dst)),
            (!cast<Instruction>("VRNDSCALE"#Suffix#_.ZSuffix#"rrik")
             _.RC:$dst, _.KRCWM:$mask, _.RC:$src, (i32 0x9))>;
  def : Pat<(_.VT (vselect _.KRCWM:$mask, (fnearbyint _.RC:$src), _.RC:$dst)),
            (!cast<Instruction>("VRNDSCALE"#Suffix#_.ZSuffix#"rrik")
             _.RC:$dst, _.KRCWM:$mask, _.RC:$src, (i32 0xC))>;
  def : Pat<(_.VT (vselect _.KRCWM:$mask, (fceil _.RC:$src), _.RC:$dst)),
            (!cast<Instruction>("VRNDSCALE"#Suffix#_.ZSuffix#"rrik")
             _.RC:$dst, _.KRCWM:$mask, _.RC:$src, (i32 0xA))>;
  def : Pat<(_.VT (vselect _.KRCWM:$mask, (frint _.RC:$src), _.RC:$dst)),
            (!cast<Instruction>("VRNDSCALE"#Suffix#_.ZSuffix#"rrik")
             _.RC:$dst, _.KRCWM:$mask, _.RC:$src, (i32 0x4))>;
  def : Pat<(_.VT (vselect _.KRCWM:$mask, (ftrunc _.RC:$src), _.RC:$dst)),
            (!cast<Instruction>("VRNDSCALE"#Suffix#_.ZSuffix#"rrik")
             _.RC:$dst, _.KRCWM:$mask, _.RC:$src, (i32 0xB))>;

  // Zero-masking
  def : Pat<(_.VT (vselect _.KRCWM:$mask, (ffloor _.RC:$src),
                           _.ImmAllZerosV)),
            (!cast<Instruction>("VRNDSCALE"#Suffix#_.ZSuffix#"rrikz")
             _.KRCWM:$mask, _.RC:$src, (i32 0x9))>;
  def : Pat<(_.VT (vselect _.KRCWM:$mask, (fnearbyint _.RC:$src),
                           _.ImmAllZerosV)),
            (!cast<Instruction>("VRNDSCALE"#Suffix#_.ZSuffix#"rrikz")
             _.KRCWM:$mask, _.RC:$src, (i32 0xC))>;
  def : Pat<(_.VT (vselect _.KRCWM:$mask, (fceil _.RC:$src),
                           _.ImmAllZerosV)),
            (!cast<Instruction>("VRNDSCALE"#Suffix#_.ZSuffix#"rrikz")
             _.KRCWM:$mask, _.RC:$src, (i32 0xA))>;
  def : Pat<(_.VT (vselect _.KRCWM:$mask, (frint _.RC:$src),
                           _.ImmAllZerosV)),
            (!cast<Instruction>("VRNDSCALE"#Suffix#_.ZSuffix#"rrikz")
             _.KRCWM:$mask, _.RC:$src, (i32 0x4))>;
  def : Pat<(_.VT (vselect _.KRCWM:$mask, (ftrunc _.RC:$src),
                           _.ImmAllZerosV)),
            (!cast<Instruction>("VRNDSCALE"#Suffix#_.ZSuffix#"rrikz")
             _.KRCWM:$mask, _.RC:$src, (i32 0xB))>;

  // Load
  def : Pat<(_.VT (ffloor (_.LdFrag addr:$src))),
            (!cast<Instruction>("VRNDSCALE"#Suffix#_.ZSuffix#"rmi")
             addr:$src, (i32 0x9))>;
  def : Pat<(_.VT (fnearbyint (_.LdFrag addr:$src))),
            (!cast<Instruction>("VRNDSCALE"#Suffix#_.ZSuffix#"rmi")
             addr:$src, (i32 0xC))>;
  def : Pat<(_.VT (fceil (_.LdFrag addr:$src))),
            (!cast<Instruction>("VRNDSCALE"#Suffix#_.ZSuffix#"rmi")
             addr:$src, (i32 0xA))>;
  def : Pat<(_.VT (frint (_.LdFrag addr:$src))),
            (!cast<Instruction>("VRNDSCALE"#Suffix#_.ZSuffix#"rmi")
             addr:$src, (i32 0x4))>;
  def : Pat<(_.VT (ftrunc (_.LdFrag addr:$src))),
            (!cast<Instruction>("VRNDSCALE"#Suffix#_.ZSuffix#"rmi")
             addr:$src, (i32 0xB))>;

  // Merge-masking + load
  def : Pat<(_.VT (vselect _.KRCWM:$mask, (ffloor (_.LdFrag addr:$src)),
                           _.RC:$dst)),
            (!cast<Instruction>("VRNDSCALE"#Suffix#_.ZSuffix#"rmik")
             _.RC:$dst, _.KRCWM:$mask, addr:$src, (i32 0x9))>;
  def : Pat<(_.VT (vselect _.KRCWM:$mask, (fnearbyint (_.LdFrag addr:$src)),
                           _.RC:$dst)),
            (!cast<Instruction>("VRNDSCALE"#Suffix#_.ZSuffix#"rmik")
             _.RC:$dst, _.KRCWM:$mask, addr:$src, (i32 0xC))>;
  def : Pat<(_.VT (vselect _.KRCWM:$mask, (fceil (_.LdFrag addr:$src)),
                           _.RC:$dst)),
            (!cast<Instruction>("VRNDSCALE"#Suffix#_.ZSuffix#"rmik")
             _.RC:$dst, _.KRCWM:$mask, addr:$src, (i32 0xA))>;
  def : Pat<(_.VT (vselect _.KRCWM:$mask, (frint (_.LdFrag addr:$src)),
                           _.RC:$dst)),
            (!cast<Instruction>("VRNDSCALE"#Suffix#_.ZSuffix#"rmik")
             _.RC:$dst, _.KRCWM:$mask, addr:$src, (i32 0x4))>;
  def : Pat<(_.VT (vselect _.KRCWM:$mask, (ftrunc (_.LdFrag addr:$src)),
                           _.RC:$dst)),
            (!cast<Instruction>("VRNDSCALE"#Suffix#_.ZSuffix#"rmik")
             _.RC:$dst, _.KRCWM:$mask, addr:$src, (i32 0xB))>;

  // Zero-masking + load
  def : Pat<(_.VT (vselect _.KRCWM:$mask, (ffloor (_.LdFrag addr:$src)),
                           _.ImmAllZerosV)),
            (!cast<Instruction>("VRNDSCALE"#Suffix#_.ZSuffix#"rmikz")
             _.KRCWM:$mask, addr:$src, (i32 0x9))>;
  def : Pat<(_.VT (vselect _.KRCWM:$mask, (fnearbyint (_.LdFrag addr:$src)),
                           _.ImmAllZerosV)),
            (!cast<Instruction>("VRNDSCALE"#Suffix#_.ZSuffix#"rmikz")
             _.KRCWM:$mask, addr:$src, (i32 0xC))>;
  def : Pat<(_.VT (vselect _.KRCWM:$mask, (fceil (_.LdFrag addr:$src)),
                           _.ImmAllZerosV)),
            (!cast<Instruction>("VRNDSCALE"#Suffix#_.ZSuffix#"rmikz")
             _.KRCWM:$mask, addr:$src, (i32 0xA))>;
  def : Pat<(_.VT (vselect _.KRCWM:$mask, (frint (_.LdFrag addr:$src)),
                           _.ImmAllZerosV)),
            (!cast<Instruction>("VRNDSCALE"#Suffix#_.ZSuffix#"rmikz")
             _.KRCWM:$mask, addr:$src, (i32 0x4))>;
  def : Pat<(_.VT (vselect _.KRCWM:$mask, (ftrunc (_.LdFrag addr:$src)),
                           _.ImmAllZerosV)),
            (!cast<Instruction>("VRNDSCALE"#Suffix#_.ZSuffix#"rmikz")
             _.KRCWM:$mask, addr:$src, (i32 0xB))>;

  // Broadcast load
  def : Pat<(_.VT (ffloor (X86VBroadcast (_.ScalarLdFrag addr:$src)))),
            (!cast<Instruction>("VRNDSCALE"#Suffix#_.ZSuffix#"rmbi")
             addr:$src, (i32 0x9))>;
  def : Pat<(_.VT (fnearbyint (X86VBroadcast (_.ScalarLdFrag addr:$src)))),
            (!cast<Instruction>("VRNDSCALE"#Suffix#_.ZSuffix#"rmbi")
             addr:$src, (i32 0xC))>;
  def : Pat<(_.VT (fceil (X86VBroadcast (_.ScalarLdFrag addr:$src)))),
            (!cast<Instruction>("VRNDSCALE"#Suffix#_.ZSuffix#"rmbi")
             addr:$src, (i32 0xA))>;
  def : Pat<(_.VT (frint (X86VBroadcast (_.ScalarLdFrag addr:$src)))),
            (!cast<Instruction>("VRNDSCALE"#Suffix#_.ZSuffix#"rmbi")
             addr:$src, (i32 0x4))>;
  def : Pat<(_.VT (ftrunc (X86VBroadcast (_.ScalarLdFrag addr:$src)))),
            (!cast<Instruction>("VRNDSCALE"#Suffix#_.ZSuffix#"rmbi")
             addr:$src, (i32 0xB))>;

  // Merge-masking + broadcast load
  def : Pat<(_.VT (vselect _.KRCWM:$mask,
                           (ffloor (X86VBroadcast (_.ScalarLdFrag addr:$src))),
                           _.RC:$dst)),
            (!cast<Instruction>("VRNDSCALE"#Suffix#_.ZSuffix#"rmbik")
             _.RC:$dst, _.KRCWM:$mask, addr:$src, (i32 0x9))>;
  def : Pat<(_.VT (vselect _.KRCWM:$mask,
                           (fnearbyint (X86VBroadcast (_.ScalarLdFrag addr:$src))),
                           _.RC:$dst)),
            (!cast<Instruction>("VRNDSCALE"#Suffix#_.ZSuffix#"rmbik")
             _.RC:$dst, _.KRCWM:$mask, addr:$src, (i32 0xC))>;
  def : Pat<(_.VT (vselect _.KRCWM:$mask,
                           (fceil (X86VBroadcast (_.ScalarLdFrag addr:$src))),
                           _.RC:$dst)),
            (!cast<Instruction>("VRNDSCALE"#Suffix#_.ZSuffix#"rmbik")
             _.RC:$dst, _.KRCWM:$mask, addr:$src, (i32 0xA))>;
  def : Pat<(_.VT (vselect _.KRCWM:$mask,
                           (frint (X86VBroadcast (_.ScalarLdFrag addr:$src))),
                           _.RC:$dst)),
            (!cast<Instruction>("VRNDSCALE"#Suffix#_.ZSuffix#"rmbik")
             _.RC:$dst, _.KRCWM:$mask, addr:$src, (i32 0x4))>;
  def : Pat<(_.VT (vselect _.KRCWM:$mask,
                           (ftrunc (X86VBroadcast (_.ScalarLdFrag addr:$src))),
                           _.RC:$dst)),
            (!cast<Instruction>("VRNDSCALE"#Suffix#_.ZSuffix#"rmbik")
             _.RC:$dst, _.KRCWM:$mask, addr:$src, (i32 0xB))>;

  // Zero-masking + broadcast load
  def : Pat<(_.VT (vselect _.KRCWM:$mask,
                           (ffloor (X86VBroadcast (_.ScalarLdFrag addr:$src))),
                           _.ImmAllZerosV)),
            (!cast<Instruction>("VRNDSCALE"#Suffix#_.ZSuffix#"rmbikz")
             _.KRCWM:$mask, addr:$src, (i32 0x9))>;
  def : Pat<(_.VT (vselect _.KRCWM:$mask,
                           (fnearbyint (X86VBroadcast (_.ScalarLdFrag addr:$src))),
                           _.ImmAllZerosV)),
            (!cast<Instruction>("VRNDSCALE"#Suffix#_.ZSuffix#"rmbikz")
             _.KRCWM:$mask, addr:$src, (i32 0xC))>;
  def : Pat<(_.VT (vselect _.KRCWM:$mask,
                           (fceil (X86VBroadcast (_.ScalarLdFrag addr:$src))),
                           _.ImmAllZerosV)),
            (!cast<Instruction>("VRNDSCALE"#Suffix#_.ZSuffix#"rmbikz")
             _.KRCWM:$mask, addr:$src, (i32 0xA))>;
  def : Pat<(_.VT (vselect _.KRCWM:$mask,
                           (frint (X86VBroadcast (_.ScalarLdFrag addr:$src))),
                           _.ImmAllZerosV)),
            (!cast<Instruction>("VRNDSCALE"#Suffix#_.ZSuffix#"rmbikz")
             _.KRCWM:$mask, addr:$src, (i32 0x4))>;
  def : Pat<(_.VT (vselect _.KRCWM:$mask,
                           (ftrunc (X86VBroadcast (_.ScalarLdFrag addr:$src))),
                           _.ImmAllZerosV)),
            (!cast<Instruction>("VRNDSCALE"#Suffix#_.ZSuffix#"rmbikz")
             _.KRCWM:$mask, addr:$src, (i32 0xB))>;
}

let Predicates = [HasAVX512] in {
  defm : AVX512_rndscale_lowering<v16f32_info, "PS">;
  defm : AVX512_rndscale_lowering<v8f64_info,  "PD">;
}

let Predicates = [HasVLX] in {
  defm : AVX512_rndscale_lowering<v8f32x_info, "PS">;
  defm : AVX512_rndscale_lowering<v4f64x_info, "PD">;
  defm : AVX512_rndscale_lowering<v4f32x_info, "PS">;
  defm : AVX512_rndscale_lowering<v2f64x_info, "PD">;
}

multiclass avx512_shuff_packed_128_common<bits<8> opc, string OpcodeStr,
                                          X86FoldableSchedWrite sched,
                                          X86VectorVTInfo _,
                                          X86VectorVTInfo CastInfo,
                                          string EVEX2VEXOvrd> {
  let ExeDomain = _.ExeDomain in {
  defm rri : AVX512_maskable<opc, MRMSrcReg, _, (outs _.RC:$dst),
                  (ins _.RC:$src1, _.RC:$src2, u8imm:$src3),
                  OpcodeStr, "$src3, $src2, $src1", "$src1, $src2, $src3",
                  (_.VT (bitconvert
                         (CastInfo.VT (X86Shuf128 _.RC:$src1, _.RC:$src2,
                                                  (i8 imm:$src3)))))>,
                  Sched<[sched]>, EVEX2VEXOverride<EVEX2VEXOvrd#"rr">;
  defm rmi : AVX512_maskable<opc, MRMSrcMem, _, (outs _.RC:$dst),
                (ins _.RC:$src1, _.MemOp:$src2, u8imm:$src3),
                OpcodeStr, "$src3, $src2, $src1", "$src1, $src2, $src3",
                (_.VT
                 (bitconvert
                  (CastInfo.VT (X86Shuf128 _.RC:$src1,
                                           (CastInfo.LdFrag addr:$src2),
                                           (i8 imm:$src3)))))>,
                Sched<[sched.Folded, sched.ReadAfterFold]>,
                EVEX2VEXOverride<EVEX2VEXOvrd#"rm">;
  defm rmbi : AVX512_maskable<opc, MRMSrcMem, _, (outs _.RC:$dst),
                    (ins _.RC:$src1, _.ScalarMemOp:$src2, u8imm:$src3),
                    OpcodeStr, "$src3, ${src2}"##_.BroadcastStr##", $src1",
                    "$src1, ${src2}"##_.BroadcastStr##", $src3",
                    (_.VT
                     (bitconvert
                      (CastInfo.VT
                       (X86Shuf128 _.RC:$src1,
                                   (X86VBroadcast (_.ScalarLdFrag addr:$src2)),
                                   (i8 imm:$src3)))))>, EVEX_B,
                    Sched<[sched.Folded, sched.ReadAfterFold]>;
  }
}

multiclass avx512_shuff_packed_128<string OpcodeStr, X86FoldableSchedWrite sched,
                                   AVX512VLVectorVTInfo _,
                                   AVX512VLVectorVTInfo CastInfo, bits<8> opc,
                                   string EVEX2VEXOvrd>{
  let Predicates = [HasAVX512] in
  defm Z : avx512_shuff_packed_128_common<opc, OpcodeStr, sched,
                                          _.info512, CastInfo.info512, "">, EVEX_V512;

  let Predicates = [HasAVX512, HasVLX] in
  defm Z256 : avx512_shuff_packed_128_common<opc, OpcodeStr, sched,
                                             _.info256, CastInfo.info256,
                                             EVEX2VEXOvrd>, EVEX_V256;
}

defm VSHUFF32X4 : avx512_shuff_packed_128<"vshuff32x4", WriteFShuffle256,
      avx512vl_f32_info, avx512vl_f64_info, 0x23, "VPERM2F128">, AVX512AIi8Base, EVEX_4V, EVEX_CD8<32, CD8VF>;
defm VSHUFF64X2 : avx512_shuff_packed_128<"vshuff64x2", WriteFShuffle256,
      avx512vl_f64_info, avx512vl_f64_info, 0x23, "VPERM2F128">, AVX512AIi8Base, EVEX_4V, EVEX_CD8<64, CD8VF>, VEX_W;
defm VSHUFI32X4 : avx512_shuff_packed_128<"vshufi32x4", WriteFShuffle256,
      avx512vl_i32_info, avx512vl_i64_info, 0x43, "VPERM2I128">, AVX512AIi8Base, EVEX_4V, EVEX_CD8<32, CD8VF>;
defm VSHUFI64X2 : avx512_shuff_packed_128<"vshufi64x2", WriteFShuffle256,
      avx512vl_i64_info, avx512vl_i64_info, 0x43, "VPERM2I128">, AVX512AIi8Base, EVEX_4V, EVEX_CD8<64, CD8VF>, VEX_W;

let Predicates = [HasAVX512] in {
// Provide fallback in case the load node that is used in the broadcast
// patterns above is used by additional users, which prevents the pattern
// selection.
def : Pat<(v8f64 (X86SubVBroadcast (v2f64 VR128X:$src))),
          (VSHUFF64X2Zrri (INSERT_SUBREG (v8f64 (IMPLICIT_DEF)), VR128X:$src, sub_xmm),
                          (INSERT_SUBREG (v8f64 (IMPLICIT_DEF)), VR128X:$src, sub_xmm),
                          0)>;
def : Pat<(v8i64 (X86SubVBroadcast (v2i64 VR128X:$src))),
          (VSHUFI64X2Zrri (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR128X:$src, sub_xmm),
                          (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR128X:$src, sub_xmm),
                          0)>;

def : Pat<(v16f32 (X86SubVBroadcast (v4f32 VR128X:$src))),
          (VSHUFF32X4Zrri (INSERT_SUBREG (v16f32 (IMPLICIT_DEF)), VR128X:$src, sub_xmm),
                          (INSERT_SUBREG (v16f32 (IMPLICIT_DEF)), VR128X:$src, sub_xmm),
                          0)>;
def : Pat<(v16i32 (X86SubVBroadcast (v4i32 VR128X:$src))),
          (VSHUFI32X4Zrri (INSERT_SUBREG (v16i32 (IMPLICIT_DEF)), VR128X:$src, sub_xmm),
                          (INSERT_SUBREG (v16i32 (IMPLICIT_DEF)), VR128X:$src, sub_xmm),
                          0)>;

def : Pat<(v32i16 (X86SubVBroadcast (v8i16 VR128X:$src))),
          (VSHUFI32X4Zrri (INSERT_SUBREG (v32i16 (IMPLICIT_DEF)), VR128X:$src, sub_xmm),
                          (INSERT_SUBREG (v32i16 (IMPLICIT_DEF)), VR128X:$src, sub_xmm),
                          0)>;

def : Pat<(v64i8 (X86SubVBroadcast (v16i8 VR128X:$src))),
          (VSHUFI32X4Zrri (INSERT_SUBREG (v64i8 (IMPLICIT_DEF)), VR128X:$src, sub_xmm),
                          (INSERT_SUBREG (v64i8 (IMPLICIT_DEF)), VR128X:$src, sub_xmm),
                          0)>;
}

multiclass avx512_valign<bits<8> opc, string OpcodeStr,
                         X86FoldableSchedWrite sched, X86VectorVTInfo _>{
  // NOTE: EVEX2VEXOverride changed back to Unset for 256-bit at the
  // instantiation of this class.
  let ExeDomain = _.ExeDomain in {
  defm rri : AVX512_maskable<opc, MRMSrcReg, _, (outs _.RC:$dst),
                  (ins _.RC:$src1, _.RC:$src2, u8imm:$src3),
                  OpcodeStr, "$src3, $src2, $src1", "$src1, $src2, $src3",
                  (_.VT (X86VAlign _.RC:$src1, _.RC:$src2, (i8 imm:$src3)))>,
                  Sched<[sched]>, EVEX2VEXOverride<"VPALIGNRrri">;
  defm rmi : AVX512_maskable<opc, MRMSrcMem, _, (outs _.RC:$dst),
                (ins _.RC:$src1, _.MemOp:$src2, u8imm:$src3),
                OpcodeStr, "$src3, $src2, $src1", "$src1, $src2, $src3",
                (_.VT (X86VAlign _.RC:$src1,
                                 (bitconvert (_.LdFrag addr:$src2)),
                                 (i8 imm:$src3)))>,
                Sched<[sched.Folded, sched.ReadAfterFold]>,
                EVEX2VEXOverride<"VPALIGNRrmi">;

  defm rmbi : AVX512_maskable<opc, MRMSrcMem, _, (outs _.RC:$dst),
                   (ins _.RC:$src1, _.ScalarMemOp:$src2, u8imm:$src3),
                   OpcodeStr, "$src3, ${src2}"##_.BroadcastStr##", $src1",
                   "$src1, ${src2}"##_.BroadcastStr##", $src3",
                   (X86VAlign _.RC:$src1,
                              (_.VT (X86VBroadcast(_.ScalarLdFrag addr:$src2))),
                              (i8 imm:$src3))>, EVEX_B,
                   Sched<[sched.Folded, sched.ReadAfterFold]>;
  }
}

multiclass avx512_valign_common<string OpcodeStr, X86SchedWriteWidths sched,
                                AVX512VLVectorVTInfo _> {
  let Predicates = [HasAVX512] in {
    defm Z    : avx512_valign<0x03, OpcodeStr, sched.ZMM, _.info512>,
                                AVX512AIi8Base, EVEX_4V, EVEX_V512;
  }
  let Predicates = [HasAVX512, HasVLX] in {
    defm Z128 : avx512_valign<0x03, OpcodeStr, sched.XMM, _.info128>,
                                AVX512AIi8Base, EVEX_4V, EVEX_V128;
    // We can't really override the 256-bit version so change it back to unset.
    let EVEX2VEXOverride = ? in
    defm Z256 : avx512_valign<0x03, OpcodeStr, sched.YMM, _.info256>,
                                AVX512AIi8Base, EVEX_4V, EVEX_V256;
  }
}

defm VALIGND: avx512_valign_common<"valignd", SchedWriteShuffle,
                                   avx512vl_i32_info>, EVEX_CD8<32, CD8VF>;
defm VALIGNQ: avx512_valign_common<"valignq", SchedWriteShuffle,
                                   avx512vl_i64_info>, EVEX_CD8<64, CD8VF>,
                                   VEX_W;

defm VPALIGNR: avx512_common_3Op_rm_imm8<0x0F, X86PAlignr, "vpalignr",
                                         SchedWriteShuffle, avx512vl_i8_info,
                                         avx512vl_i8_info>, EVEX_CD8<8, CD8VF>;

// Fragments to help convert valignq into masked valignd. Or valignq/valignd
// into vpalignr.
def ValignqImm32XForm : SDNodeXForm<imm, [{
  return getI8Imm(N->getZExtValue() * 2, SDLoc(N));
}]>;
def ValignqImm8XForm : SDNodeXForm<imm, [{
  return getI8Imm(N->getZExtValue() * 8, SDLoc(N));
}]>;
def ValigndImm8XForm : SDNodeXForm<imm, [{
  return getI8Imm(N->getZExtValue() * 4, SDLoc(N));
}]>;

multiclass avx512_vpalign_mask_lowering<string OpcodeStr, SDNode OpNode,
                                        X86VectorVTInfo From, X86VectorVTInfo To,
                                        SDNodeXForm ImmXForm> {
  def : Pat<(To.VT (vselect To.KRCWM:$mask,
                            (bitconvert
                             (From.VT (OpNode From.RC:$src1, From.RC:$src2,
                                              imm:$src3))),
                            To.RC:$src0)),
            (!cast<Instruction>(OpcodeStr#"rrik") To.RC:$src0, To.KRCWM:$mask,
                                                  To.RC:$src1, To.RC:$src2,
                                                  (ImmXForm imm:$src3))>;

  def : Pat<(To.VT (vselect To.KRCWM:$mask,
                            (bitconvert
                             (From.VT (OpNode From.RC:$src1, From.RC:$src2,
                                              imm:$src3))),
                            To.ImmAllZerosV)),
            (!cast<Instruction>(OpcodeStr#"rrikz") To.KRCWM:$mask,
                                                   To.RC:$src1, To.RC:$src2,
                                                   (ImmXForm imm:$src3))>;

  def : Pat<(To.VT (vselect To.KRCWM:$mask,
                            (bitconvert
                             (From.VT (OpNode From.RC:$src1,
                                              (From.LdFrag addr:$src2),
                                      imm:$src3))),
                            To.RC:$src0)),
            (!cast<Instruction>(OpcodeStr#"rmik") To.RC:$src0, To.KRCWM:$mask,
                                                  To.RC:$src1, addr:$src2,
                                                  (ImmXForm imm:$src3))>;

  def : Pat<(To.VT (vselect To.KRCWM:$mask,
                            (bitconvert
                             (From.VT (OpNode From.RC:$src1,
                                              (From.LdFrag addr:$src2),
                                      imm:$src3))),
                            To.ImmAllZerosV)),
            (!cast<Instruction>(OpcodeStr#"rmikz") To.KRCWM:$mask,
                                                   To.RC:$src1, addr:$src2,
                                                   (ImmXForm imm:$src3))>;
}

multiclass avx512_vpalign_mask_lowering_mb<string OpcodeStr, SDNode OpNode,
                                           X86VectorVTInfo From,
                                           X86VectorVTInfo To,
                                           SDNodeXForm ImmXForm> :
      avx512_vpalign_mask_lowering<OpcodeStr, OpNode, From, To, ImmXForm> {
  def : Pat<(From.VT (OpNode From.RC:$src1,
                             (bitconvert (To.VT (X86VBroadcast
                                                (To.ScalarLdFrag addr:$src2)))),
                             imm:$src3)),
            (!cast<Instruction>(OpcodeStr#"rmbi") To.RC:$src1, addr:$src2,
                                                  (ImmXForm imm:$src3))>;

  def : Pat<(To.VT (vselect To.KRCWM:$mask,
                            (bitconvert
                             (From.VT (OpNode From.RC:$src1,
                                      (bitconvert
                                       (To.VT (X86VBroadcast
                                               (To.ScalarLdFrag addr:$src2)))),
                                      imm:$src3))),
                            To.RC:$src0)),
            (!cast<Instruction>(OpcodeStr#"rmbik") To.RC:$src0, To.KRCWM:$mask,
                                                   To.RC:$src1, addr:$src2,
                                                   (ImmXForm imm:$src3))>;

  def : Pat<(To.VT (vselect To.KRCWM:$mask,
                            (bitconvert
                             (From.VT (OpNode From.RC:$src1,
                                      (bitconvert
                                       (To.VT (X86VBroadcast
                                               (To.ScalarLdFrag addr:$src2)))),
                                      imm:$src3))),
                            To.ImmAllZerosV)),
            (!cast<Instruction>(OpcodeStr#"rmbikz") To.KRCWM:$mask,
                                                    To.RC:$src1, addr:$src2,
                                                    (ImmXForm imm:$src3))>;
}

let Predicates = [HasAVX512] in {
  // For 512-bit we lower to the widest element type we can. So we only need
  // to handle converting valignq to valignd.
  defm : avx512_vpalign_mask_lowering_mb<"VALIGNDZ", X86VAlign, v8i64_info,
                                         v16i32_info, ValignqImm32XForm>;
}

let Predicates = [HasVLX] in {
  // For 128-bit we lower to the widest element type we can. So we only need
  // to handle converting valignq to valignd.
  defm : avx512_vpalign_mask_lowering_mb<"VALIGNDZ128", X86VAlign, v2i64x_info,
                                         v4i32x_info, ValignqImm32XForm>;
  // For 256-bit we lower to the widest element type we can. So we only need
  // to handle converting valignq to valignd.
  defm : avx512_vpalign_mask_lowering_mb<"VALIGNDZ256", X86VAlign, v4i64x_info,
                                         v8i32x_info, ValignqImm32XForm>;
}

let Predicates = [HasVLX, HasBWI] in {
  // We can turn 128 and 256 bit VALIGND/VALIGNQ into VPALIGNR.
  defm : avx512_vpalign_mask_lowering<"VPALIGNRZ128", X86VAlign, v2i64x_info,
                                      v16i8x_info, ValignqImm8XForm>;
  defm : avx512_vpalign_mask_lowering<"VPALIGNRZ128", X86VAlign, v4i32x_info,
                                      v16i8x_info, ValigndImm8XForm>;
}

defm VDBPSADBW: avx512_common_3Op_rm_imm8<0x42, X86dbpsadbw, "vdbpsadbw",
                SchedWritePSADBW, avx512vl_i16_info, avx512vl_i8_info>,
                EVEX_CD8<8, CD8VF>, NotEVEX2VEXConvertible;

multiclass avx512_unary_rm<bits<8> opc, string OpcodeStr, SDNode OpNode,
                           X86FoldableSchedWrite sched, X86VectorVTInfo _> {
  let ExeDomain = _.ExeDomain in {
  defm rr : AVX512_maskable<opc, MRMSrcReg, _, (outs _.RC:$dst),
                    (ins _.RC:$src1), OpcodeStr,
                    "$src1", "$src1",
                    (_.VT (OpNode _.RC:$src1))>, EVEX, AVX5128IBase,
                    Sched<[sched]>;

  defm rm : AVX512_maskable<opc, MRMSrcMem, _, (outs _.RC:$dst),
                  (ins _.MemOp:$src1), OpcodeStr,
                  "$src1", "$src1",
                  (_.VT (OpNode (bitconvert (_.LdFrag addr:$src1))))>,
            EVEX, AVX5128IBase, EVEX_CD8<_.EltSize, CD8VF>,
            Sched<[sched.Folded]>;
  }
}

multiclass avx512_unary_rmb<bits<8> opc, string OpcodeStr, SDNode OpNode,
                            X86FoldableSchedWrite sched, X86VectorVTInfo _> :
           avx512_unary_rm<opc, OpcodeStr, OpNode, sched, _> {
  defm rmb : AVX512_maskable<opc, MRMSrcMem, _, (outs _.RC:$dst),
                  (ins _.ScalarMemOp:$src1), OpcodeStr,
                  "${src1}"##_.BroadcastStr,
                  "${src1}"##_.BroadcastStr,
                  (_.VT (OpNode (X86VBroadcast
                                    (_.ScalarLdFrag addr:$src1))))>,
             EVEX, AVX5128IBase, EVEX_B, EVEX_CD8<_.EltSize, CD8VF>,
             Sched<[sched.Folded]>;
}

multiclass avx512_unary_rm_vl<bits<8> opc, string OpcodeStr, SDNode OpNode,
                              X86SchedWriteWidths sched,
                              AVX512VLVectorVTInfo VTInfo, Predicate prd> {
  let Predicates = [prd] in
    defm Z : avx512_unary_rm<opc, OpcodeStr, OpNode, sched.ZMM, VTInfo.info512>,
                             EVEX_V512;

  let Predicates = [prd, HasVLX] in {
    defm Z256 : avx512_unary_rm<opc, OpcodeStr, OpNode, sched.YMM, VTInfo.info256>,
                              EVEX_V256;
    defm Z128 : avx512_unary_rm<opc, OpcodeStr, OpNode, sched.XMM, VTInfo.info128>,
                              EVEX_V128;
  }
}

multiclass avx512_unary_rmb_vl<bits<8> opc, string OpcodeStr, SDNode OpNode,
                               X86SchedWriteWidths sched, AVX512VLVectorVTInfo VTInfo,
                               Predicate prd> {
  let Predicates = [prd] in
    defm Z : avx512_unary_rmb<opc, OpcodeStr, OpNode, sched.ZMM, VTInfo.info512>,
                              EVEX_V512;

  let Predicates = [prd, HasVLX] in {
    defm Z256 : avx512_unary_rmb<opc, OpcodeStr, OpNode, sched.YMM, VTInfo.info256>,
                                 EVEX_V256;
    defm Z128 : avx512_unary_rmb<opc, OpcodeStr, OpNode, sched.XMM, VTInfo.info128>,
                                 EVEX_V128;
  }
}

multiclass avx512_unary_rm_vl_dq<bits<8> opc_d, bits<8> opc_q, string OpcodeStr,
                                 SDNode OpNode, X86SchedWriteWidths sched,
                                 Predicate prd> {
  defm Q : avx512_unary_rmb_vl<opc_q, OpcodeStr#"q", OpNode, sched,
                               avx512vl_i64_info, prd>, VEX_W;
  defm D : avx512_unary_rmb_vl<opc_d, OpcodeStr#"d", OpNode, sched,
                               avx512vl_i32_info, prd>;
}

multiclass avx512_unary_rm_vl_bw<bits<8> opc_b, bits<8> opc_w, string OpcodeStr,
                                 SDNode OpNode, X86SchedWriteWidths sched,
                                 Predicate prd> {
  defm W : avx512_unary_rm_vl<opc_w, OpcodeStr#"w", OpNode, sched,
                              avx512vl_i16_info, prd>, VEX_WIG;
  defm B : avx512_unary_rm_vl<opc_b, OpcodeStr#"b", OpNode, sched,
                              avx512vl_i8_info, prd>, VEX_WIG;
}

multiclass avx512_unary_rm_vl_all<bits<8> opc_b, bits<8> opc_w,
                                  bits<8> opc_d, bits<8> opc_q,
                                  string OpcodeStr, SDNode OpNode,
                                  X86SchedWriteWidths sched> {
  defm NAME : avx512_unary_rm_vl_dq<opc_d, opc_q, OpcodeStr, OpNode, sched,
                                    HasAVX512>,
              avx512_unary_rm_vl_bw<opc_b, opc_w, OpcodeStr, OpNode, sched,
                                    HasBWI>;
}

defm VPABS : avx512_unary_rm_vl_all<0x1C, 0x1D, 0x1E, 0x1F, "vpabs", abs,
                                    SchedWriteVecALU>;

// VPABS: Use 512bit version to implement 128/256 bit in case NoVLX.
let Predicates = [HasAVX512, NoVLX] in {
  def : Pat<(v4i64 (abs VR256X:$src)),
            (EXTRACT_SUBREG
                (VPABSQZrr
                    (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR256X:$src, sub_ymm)),
             sub_ymm)>;
  def : Pat<(v2i64 (abs VR128X:$src)),
            (EXTRACT_SUBREG
                (VPABSQZrr
                    (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR128X:$src, sub_xmm)),
             sub_xmm)>;
}

// Use 512bit version to implement 128/256 bit.
multiclass avx512_unary_lowering<string InstrStr, SDNode OpNode,
                                 AVX512VLVectorVTInfo _, Predicate prd> {
  let Predicates = [prd, NoVLX] in {
    def : Pat<(_.info256.VT(OpNode _.info256.RC:$src1)),
              (EXTRACT_SUBREG
                (!cast<Instruction>(InstrStr # "Zrr")
                  (INSERT_SUBREG(_.info512.VT(IMPLICIT_DEF)),
                                 _.info256.RC:$src1,
                                 _.info256.SubRegIdx)),
              _.info256.SubRegIdx)>;

    def : Pat<(_.info128.VT(OpNode _.info128.RC:$src1)),
              (EXTRACT_SUBREG
                (!cast<Instruction>(InstrStr # "Zrr")
                  (INSERT_SUBREG(_.info512.VT(IMPLICIT_DEF)),
                                 _.info128.RC:$src1,
                                 _.info128.SubRegIdx)),
              _.info128.SubRegIdx)>;
  }
}

defm VPLZCNT    : avx512_unary_rm_vl_dq<0x44, 0x44, "vplzcnt", ctlz,
                                        SchedWriteVecIMul, HasCDI>;

// FIXME: Is there a better scheduler class for VPCONFLICT?
defm VPCONFLICT : avx512_unary_rm_vl_dq<0xC4, 0xC4, "vpconflict", X86Conflict,
                                        SchedWriteVecALU, HasCDI>;

// VPLZCNT: Use 512bit version to implement 128/256 bit in case NoVLX.
defm : avx512_unary_lowering<"VPLZCNTQ", ctlz, avx512vl_i64_info, HasCDI>;
defm : avx512_unary_lowering<"VPLZCNTD", ctlz, avx512vl_i32_info, HasCDI>;

//===---------------------------------------------------------------------===//
// Counts number of ones - VPOPCNTD and VPOPCNTQ
//===---------------------------------------------------------------------===//

// FIXME: Is there a better scheduler class for VPOPCNTD/VPOPCNTQ?
defm VPOPCNT : avx512_unary_rm_vl_dq<0x55, 0x55, "vpopcnt", ctpop,
                                     SchedWriteVecALU, HasVPOPCNTDQ>;

defm : avx512_unary_lowering<"VPOPCNTQ", ctpop, avx512vl_i64_info, HasVPOPCNTDQ>;
defm : avx512_unary_lowering<"VPOPCNTD", ctpop, avx512vl_i32_info, HasVPOPCNTDQ>;

//===---------------------------------------------------------------------===//
// Replicate Single FP - MOVSHDUP and MOVSLDUP
//===---------------------------------------------------------------------===//

multiclass avx512_replicate<bits<8> opc, string OpcodeStr, SDNode OpNode,
                            X86SchedWriteWidths sched> {
  defm NAME:       avx512_unary_rm_vl<opc, OpcodeStr, OpNode, sched,
                                      avx512vl_f32_info, HasAVX512>, XS;
}

defm VMOVSHDUP : avx512_replicate<0x16, "vmovshdup", X86Movshdup,
                                  SchedWriteFShuffle>;
defm VMOVSLDUP : avx512_replicate<0x12, "vmovsldup", X86Movsldup,
                                  SchedWriteFShuffle>;

//===----------------------------------------------------------------------===//
// AVX-512 - MOVDDUP
//===----------------------------------------------------------------------===//

multiclass avx512_movddup_128<bits<8> opc, string OpcodeStr, SDNode OpNode,
                              X86FoldableSchedWrite sched, X86VectorVTInfo _> {
  let ExeDomain = _.ExeDomain in {
  defm rr : AVX512_maskable<opc, MRMSrcReg, _, (outs _.RC:$dst),
                   (ins _.RC:$src), OpcodeStr, "$src", "$src",
                   (_.VT (OpNode (_.VT _.RC:$src)))>, EVEX,
                   Sched<[sched]>;
  defm rm : AVX512_maskable<opc, MRMSrcMem, _, (outs _.RC:$dst),
                 (ins _.ScalarMemOp:$src), OpcodeStr, "$src", "$src",
                 (_.VT (OpNode (_.VT (scalar_to_vector
                                       (_.ScalarLdFrag addr:$src)))))>,
                 EVEX, EVEX_CD8<_.EltSize, CD8VH>,
                 Sched<[sched.Folded]>;
  }
}

multiclass avx512_movddup_common<bits<8> opc, string OpcodeStr, SDNode OpNode,
                                 X86SchedWriteWidths sched, AVX512VLVectorVTInfo VTInfo> {
  defm Z : avx512_unary_rm<opc, OpcodeStr, X86Movddup, sched.ZMM,
                           VTInfo.info512>, EVEX_V512;

  let Predicates = [HasAVX512, HasVLX] in {
    defm Z256 : avx512_unary_rm<opc, OpcodeStr, X86Movddup, sched.YMM,
                                VTInfo.info256>, EVEX_V256;
    defm Z128 : avx512_movddup_128<opc, OpcodeStr, X86VBroadcast, sched.XMM,
                                   VTInfo.info128>, EVEX_V128;
  }
}

multiclass avx512_movddup<bits<8> opc, string OpcodeStr, SDNode OpNode,
                          X86SchedWriteWidths sched> {
  defm NAME:      avx512_movddup_common<opc, OpcodeStr, OpNode, sched,
                                        avx512vl_f64_info>, XD, VEX_W;
}

defm VMOVDDUP : avx512_movddup<0x12, "vmovddup", X86Movddup, SchedWriteFShuffle>;

let Predicates = [HasVLX] in {
def : Pat<(v2f64 (X86VBroadcast (loadf64 addr:$src))),
          (VMOVDDUPZ128rm addr:$src)>;
def : Pat<(v2f64 (X86VBroadcast f64:$src)),
          (VMOVDDUPZ128rr (v2f64 (COPY_TO_REGCLASS FR64X:$src, VR128X)))>;
def : Pat<(v2f64 (X86VBroadcast (loadv2f64 addr:$src))),
          (VMOVDDUPZ128rm addr:$src)>;
def : Pat<(v2f64 (X86VBroadcast (v2f64 (X86vzload addr:$src)))),
          (VMOVDDUPZ128rm addr:$src)>;

def : Pat<(vselect (v2i1 VK2WM:$mask), (v2f64 (X86VBroadcast f64:$src)),
                   (v2f64 VR128X:$src0)),
          (VMOVDDUPZ128rrk VR128X:$src0, VK2WM:$mask,
                           (v2f64 (COPY_TO_REGCLASS FR64X:$src, VR128X)))>;
def : Pat<(vselect (v2i1 VK2WM:$mask), (v2f64 (X86VBroadcast f64:$src)),
                   (bitconvert (v4i32 immAllZerosV))),
          (VMOVDDUPZ128rrkz VK2WM:$mask, (v2f64 (COPY_TO_REGCLASS FR64X:$src, VR128X)))>;

def : Pat<(vselect (v2i1 VK2WM:$mask), (v2f64 (X86VBroadcast (loadf64 addr:$src))),
                   (v2f64 VR128X:$src0)),
          (VMOVDDUPZ128rmk VR128X:$src0, VK2WM:$mask, addr:$src)>;
def : Pat<(vselect (v2i1 VK2WM:$mask), (v2f64 (X86VBroadcast (loadf64 addr:$src))),
                   (bitconvert (v4i32 immAllZerosV))),
          (VMOVDDUPZ128rmkz VK2WM:$mask, addr:$src)>;

def : Pat<(vselect (v2i1 VK2WM:$mask), (v2f64 (X86VBroadcast (loadv2f64 addr:$src))),
                   (v2f64 VR128X:$src0)),
          (VMOVDDUPZ128rmk VR128X:$src0, VK2WM:$mask, addr:$src)>;
def : Pat<(vselect (v2i1 VK2WM:$mask), (v2f64 (X86VBroadcast (loadv2f64 addr:$src))),
                   (bitconvert (v4i32 immAllZerosV))),
          (VMOVDDUPZ128rmkz VK2WM:$mask, addr:$src)>;
}

//===----------------------------------------------------------------------===//
// AVX-512 - Unpack Instructions
//===----------------------------------------------------------------------===//

defm VUNPCKH : avx512_fp_binop_p<0x15, "vunpckh", X86Unpckh, HasAVX512,
                                 SchedWriteFShuffleSizes, 0, 1>;
defm VUNPCKL : avx512_fp_binop_p<0x14, "vunpckl", X86Unpckl, HasAVX512,
                                 SchedWriteFShuffleSizes>;

defm VPUNPCKLBW : avx512_binop_rm_vl_b<0x60, "vpunpcklbw", X86Unpckl,
                                       SchedWriteShuffle, HasBWI>;
defm VPUNPCKHBW : avx512_binop_rm_vl_b<0x68, "vpunpckhbw", X86Unpckh,
                                       SchedWriteShuffle, HasBWI>;
defm VPUNPCKLWD : avx512_binop_rm_vl_w<0x61, "vpunpcklwd", X86Unpckl,
                                       SchedWriteShuffle, HasBWI>;
defm VPUNPCKHWD : avx512_binop_rm_vl_w<0x69, "vpunpckhwd", X86Unpckh,
                                       SchedWriteShuffle, HasBWI>;

defm VPUNPCKLDQ : avx512_binop_rm_vl_d<0x62, "vpunpckldq", X86Unpckl,
                                       SchedWriteShuffle, HasAVX512>;
defm VPUNPCKHDQ : avx512_binop_rm_vl_d<0x6A, "vpunpckhdq", X86Unpckh,
                                       SchedWriteShuffle, HasAVX512>;
defm VPUNPCKLQDQ : avx512_binop_rm_vl_q<0x6C, "vpunpcklqdq", X86Unpckl,
                                        SchedWriteShuffle, HasAVX512>;
defm VPUNPCKHQDQ : avx512_binop_rm_vl_q<0x6D, "vpunpckhqdq", X86Unpckh,
                                        SchedWriteShuffle, HasAVX512>;

//===----------------------------------------------------------------------===//
// AVX-512 - Extract & Insert Integer Instructions
//===----------------------------------------------------------------------===//

multiclass avx512_extract_elt_bw_m<bits<8> opc, string OpcodeStr, SDNode OpNode,
                                                            X86VectorVTInfo _> {
  def mr : AVX512Ii8<opc, MRMDestMem, (outs),
              (ins _.ScalarMemOp:$dst, _.RC:$src1, u8imm:$src2),
              OpcodeStr#"\t{$src2, $src1, $dst|$dst, $src1, $src2}",
              [(store (_.EltVT (trunc (OpNode (_.VT _.RC:$src1), imm:$src2))),
                       addr:$dst)]>,
              EVEX, EVEX_CD8<_.EltSize, CD8VT1>, Sched<[WriteVecExtractSt]>;
}

multiclass avx512_extract_elt_b<string OpcodeStr, X86VectorVTInfo _> {
  let Predicates = [HasBWI] in {
    def rr : AVX512Ii8<0x14, MRMDestReg, (outs GR32orGR64:$dst),
                  (ins _.RC:$src1, u8imm:$src2),
                  OpcodeStr#"\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                  [(set GR32orGR64:$dst,
                        (X86pextrb (_.VT _.RC:$src1), imm:$src2))]>,
                  EVEX, TAPD, Sched<[WriteVecExtract]>;

    defm NAME : avx512_extract_elt_bw_m<0x14, OpcodeStr, X86pextrb, _>, TAPD;
  }
}

multiclass avx512_extract_elt_w<string OpcodeStr, X86VectorVTInfo _> {
  let Predicates = [HasBWI] in {
    def rr : AVX512Ii8<0xC5, MRMSrcReg, (outs GR32orGR64:$dst),
                  (ins _.RC:$src1, u8imm:$src2),
                  OpcodeStr#"\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                  [(set GR32orGR64:$dst,
                        (X86pextrw (_.VT _.RC:$src1), imm:$src2))]>,
                  EVEX, PD, Sched<[WriteVecExtract]>;

    let hasSideEffects = 0, isCodeGenOnly = 1, ForceDisassemble = 1 in
    def rr_REV : AVX512Ii8<0x15, MRMDestReg, (outs GR32orGR64:$dst),
                   (ins _.RC:$src1, u8imm:$src2),
                   OpcodeStr#"\t{$src2, $src1, $dst|$dst, $src1, $src2}", []>,
                   EVEX, TAPD, FoldGenData<NAME#rr>,
                   Sched<[WriteVecExtract]>;

    defm NAME : avx512_extract_elt_bw_m<0x15, OpcodeStr, X86pextrw, _>, TAPD;
  }
}

multiclass avx512_extract_elt_dq<string OpcodeStr, X86VectorVTInfo _,
                                                            RegisterClass GRC> {
  let Predicates = [HasDQI] in {
    def rr : AVX512Ii8<0x16, MRMDestReg, (outs GRC:$dst),
                  (ins _.RC:$src1, u8imm:$src2),
                  OpcodeStr#"\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                  [(set GRC:$dst,
                      (extractelt (_.VT _.RC:$src1), imm:$src2))]>,
                  EVEX, TAPD, Sched<[WriteVecExtract]>;

    def mr : AVX512Ii8<0x16, MRMDestMem, (outs),
                (ins _.ScalarMemOp:$dst, _.RC:$src1, u8imm:$src2),
                OpcodeStr#"\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                [(store (extractelt (_.VT _.RC:$src1),
                                    imm:$src2),addr:$dst)]>,
                EVEX, EVEX_CD8<_.EltSize, CD8VT1>, TAPD,
                Sched<[WriteVecExtractSt]>;
  }
}

defm VPEXTRBZ : avx512_extract_elt_b<"vpextrb", v16i8x_info>, VEX_WIG;
defm VPEXTRWZ : avx512_extract_elt_w<"vpextrw", v8i16x_info>, VEX_WIG;
defm VPEXTRDZ : avx512_extract_elt_dq<"vpextrd", v4i32x_info, GR32>;
defm VPEXTRQZ : avx512_extract_elt_dq<"vpextrq", v2i64x_info, GR64>, VEX_W;

multiclass avx512_insert_elt_m<bits<8> opc, string OpcodeStr, SDNode OpNode,
                                            X86VectorVTInfo _, PatFrag LdFrag> {
  def rm : AVX512Ii8<opc, MRMSrcMem, (outs _.RC:$dst),
      (ins _.RC:$src1,  _.ScalarMemOp:$src2, u8imm:$src3),
      OpcodeStr#"\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}",
      [(set _.RC:$dst,
          (_.VT (OpNode _.RC:$src1, (LdFrag addr:$src2), imm:$src3)))]>,
      EVEX_4V, EVEX_CD8<_.EltSize, CD8VT1>, Sched<[WriteVecInsert.Folded, WriteVecInsert.ReadAfterFold]>;
}

multiclass avx512_insert_elt_bw<bits<8> opc, string OpcodeStr, SDNode OpNode,
                                            X86VectorVTInfo _, PatFrag LdFrag> {
  let Predicates = [HasBWI] in {
    def rr : AVX512Ii8<opc, MRMSrcReg, (outs _.RC:$dst),
        (ins _.RC:$src1, GR32orGR64:$src2, u8imm:$src3),
        OpcodeStr#"\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}",
        [(set _.RC:$dst,
            (OpNode _.RC:$src1, GR32orGR64:$src2, imm:$src3))]>, EVEX_4V,
        Sched<[WriteVecInsert]>;

    defm NAME : avx512_insert_elt_m<opc, OpcodeStr, OpNode, _, LdFrag>;
  }
}

multiclass avx512_insert_elt_dq<bits<8> opc, string OpcodeStr,
                                         X86VectorVTInfo _, RegisterClass GRC> {
  let Predicates = [HasDQI] in {
    def rr : AVX512Ii8<opc, MRMSrcReg, (outs _.RC:$dst),
        (ins _.RC:$src1, GRC:$src2, u8imm:$src3),
        OpcodeStr#"\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}",
        [(set _.RC:$dst,
            (_.VT (insertelt _.RC:$src1, GRC:$src2, imm:$src3)))]>,
        EVEX_4V, TAPD, Sched<[WriteVecInsert]>;

    defm NAME : avx512_insert_elt_m<opc, OpcodeStr, insertelt, _,
                                    _.ScalarLdFrag>, TAPD;
  }
}

defm VPINSRBZ : avx512_insert_elt_bw<0x20, "vpinsrb", X86pinsrb, v16i8x_info,
                                     extloadi8>, TAPD, VEX_WIG;
defm VPINSRWZ : avx512_insert_elt_bw<0xC4, "vpinsrw", X86pinsrw, v8i16x_info,
                                     extloadi16>, PD, VEX_WIG;
defm VPINSRDZ : avx512_insert_elt_dq<0x22, "vpinsrd", v4i32x_info, GR32>;
defm VPINSRQZ : avx512_insert_elt_dq<0x22, "vpinsrq", v2i64x_info, GR64>, VEX_W;

//===----------------------------------------------------------------------===//
// VSHUFPS - VSHUFPD Operations
//===----------------------------------------------------------------------===//

multiclass avx512_shufp<string OpcodeStr, AVX512VLVectorVTInfo VTInfo_I,
                        AVX512VLVectorVTInfo VTInfo_FP>{
  defm NAME: avx512_common_3Op_imm8<OpcodeStr, VTInfo_FP, 0xC6, X86Shufp,
                                    SchedWriteFShuffle>,
                                    EVEX_CD8<VTInfo_FP.info512.EltSize, CD8VF>,
                                    AVX512AIi8Base, EVEX_4V;
}

defm VSHUFPS: avx512_shufp<"vshufps", avx512vl_i32_info, avx512vl_f32_info>, PS;
defm VSHUFPD: avx512_shufp<"vshufpd", avx512vl_i64_info, avx512vl_f64_info>, PD, VEX_W;

//===----------------------------------------------------------------------===//
// AVX-512 - Byte shift Left/Right
//===----------------------------------------------------------------------===//

// FIXME: The SSE/AVX names are PSLLDQri etc. - should we add the i here as well?
multiclass avx512_shift_packed<bits<8> opc, SDNode OpNode, Format MRMr,
                               Format MRMm, string OpcodeStr,
                               X86FoldableSchedWrite sched, X86VectorVTInfo _>{
  def rr : AVX512<opc, MRMr,
             (outs _.RC:$dst), (ins _.RC:$src1, u8imm:$src2),
             !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
             [(set _.RC:$dst,(_.VT (OpNode _.RC:$src1, (i8 imm:$src2))))]>,
             Sched<[sched]>;
  def rm : AVX512<opc, MRMm,
           (outs _.RC:$dst), (ins _.MemOp:$src1, u8imm:$src2),
           !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
           [(set _.RC:$dst,(_.VT (OpNode
                                 (_.VT (bitconvert (_.LdFrag addr:$src1))),
                                 (i8 imm:$src2))))]>,
           Sched<[sched.Folded, sched.ReadAfterFold]>;
}

multiclass avx512_shift_packed_all<bits<8> opc, SDNode OpNode, Format MRMr,
                                   Format MRMm, string OpcodeStr,
                                   X86SchedWriteWidths sched, Predicate prd>{
  let Predicates = [prd] in
    defm Z : avx512_shift_packed<opc, OpNode, MRMr, MRMm, OpcodeStr,
                                 sched.ZMM, v64i8_info>, EVEX_V512;
  let Predicates = [prd, HasVLX] in {
    defm Z256 : avx512_shift_packed<opc, OpNode, MRMr, MRMm, OpcodeStr,
                                    sched.YMM, v32i8x_info>, EVEX_V256;
    defm Z128 : avx512_shift_packed<opc, OpNode, MRMr, MRMm, OpcodeStr,
                                    sched.XMM, v16i8x_info>, EVEX_V128;
  }
}
defm VPSLLDQ : avx512_shift_packed_all<0x73, X86vshldq, MRM7r, MRM7m, "vpslldq",
                                       SchedWriteShuffle, HasBWI>,
                                       AVX512PDIi8Base, EVEX_4V, VEX_WIG;
defm VPSRLDQ : avx512_shift_packed_all<0x73, X86vshrdq, MRM3r, MRM3m, "vpsrldq",
                                       SchedWriteShuffle, HasBWI>,
                                       AVX512PDIi8Base, EVEX_4V, VEX_WIG;

multiclass avx512_psadbw_packed<bits<8> opc, SDNode OpNode,
                                string OpcodeStr, X86FoldableSchedWrite sched,
                                X86VectorVTInfo _dst, X86VectorVTInfo _src> {
  def rr : AVX512BI<opc, MRMSrcReg,
             (outs _dst.RC:$dst), (ins _src.RC:$src1, _src.RC:$src2),
             !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
             [(set _dst.RC:$dst,(_dst.VT
                                (OpNode (_src.VT _src.RC:$src1),
                                        (_src.VT _src.RC:$src2))))]>,
             Sched<[sched]>;
  def rm : AVX512BI<opc, MRMSrcMem,
           (outs _dst.RC:$dst), (ins _src.RC:$src1, _src.MemOp:$src2),
           !strconcat(OpcodeStr, "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
           [(set _dst.RC:$dst,(_dst.VT
                              (OpNode (_src.VT _src.RC:$src1),
                              (_src.VT (bitconvert
                                        (_src.LdFrag addr:$src2))))))]>,
           Sched<[sched.Folded, sched.ReadAfterFold]>;
}

multiclass avx512_psadbw_packed_all<bits<8> opc, SDNode OpNode,
                                    string OpcodeStr, X86SchedWriteWidths sched,
                                    Predicate prd> {
  let Predicates = [prd] in
    defm Z : avx512_psadbw_packed<opc, OpNode, OpcodeStr, sched.ZMM,
                                  v8i64_info, v64i8_info>, EVEX_V512;
  let Predicates = [prd, HasVLX] in {
    defm Z256 : avx512_psadbw_packed<opc, OpNode, OpcodeStr, sched.YMM,
                                     v4i64x_info, v32i8x_info>, EVEX_V256;
    defm Z128 : avx512_psadbw_packed<opc, OpNode, OpcodeStr, sched.XMM,
                                     v2i64x_info, v16i8x_info>, EVEX_V128;
  }
}

defm VPSADBW : avx512_psadbw_packed_all<0xf6, X86psadbw, "vpsadbw",
                                        SchedWritePSADBW, HasBWI>, EVEX_4V, VEX_WIG;

// Transforms to swizzle an immediate to enable better matching when
// memory operand isn't in the right place.
def VPTERNLOG321_imm8 : SDNodeXForm<imm, [{
  // Convert a VPTERNLOG immediate by swapping operand 0 and operand 2.
  uint8_t Imm = N->getZExtValue();
  // Swap bits 1/4 and 3/6.
  uint8_t NewImm = Imm & 0xa5;
  if (Imm & 0x02) NewImm |= 0x10;
  if (Imm & 0x10) NewImm |= 0x02;
  if (Imm & 0x08) NewImm |= 0x40;
  if (Imm & 0x40) NewImm |= 0x08;
  return getI8Imm(NewImm, SDLoc(N));
}]>;
def VPTERNLOG213_imm8 : SDNodeXForm<imm, [{
  // Convert a VPTERNLOG immediate by swapping operand 1 and operand 2.
  uint8_t Imm = N->getZExtValue();
  // Swap bits 2/4 and 3/5.
  uint8_t NewImm = Imm & 0xc3;
  if (Imm & 0x04) NewImm |= 0x10;
  if (Imm & 0x10) NewImm |= 0x04;
  if (Imm & 0x08) NewImm |= 0x20;
  if (Imm & 0x20) NewImm |= 0x08;
  return getI8Imm(NewImm, SDLoc(N));
}]>;
def VPTERNLOG132_imm8 : SDNodeXForm<imm, [{
  // Convert a VPTERNLOG immediate by swapping operand 1 and operand 2.
  uint8_t Imm = N->getZExtValue();
  // Swap bits 1/2 and 5/6.
  uint8_t NewImm = Imm & 0x99;
  if (Imm & 0x02) NewImm |= 0x04;
  if (Imm & 0x04) NewImm |= 0x02;
  if (Imm & 0x20) NewImm |= 0x40;
  if (Imm & 0x40) NewImm |= 0x20;
  return getI8Imm(NewImm, SDLoc(N));
}]>;
def VPTERNLOG231_imm8 : SDNodeXForm<imm, [{
  // Convert a VPTERNLOG immediate by moving operand 1 to the end.
  uint8_t Imm = N->getZExtValue();
  // Move bits 1->2, 2->4, 3->6, 4->1, 5->3, 6->5
  uint8_t NewImm = Imm & 0x81;
  if (Imm & 0x02) NewImm |= 0x04;
  if (Imm & 0x04) NewImm |= 0x10;
  if (Imm & 0x08) NewImm |= 0x40;
  if (Imm & 0x10) NewImm |= 0x02;
  if (Imm & 0x20) NewImm |= 0x08;
  if (Imm & 0x40) NewImm |= 0x20;
  return getI8Imm(NewImm, SDLoc(N));
}]>;
def VPTERNLOG312_imm8 : SDNodeXForm<imm, [{
  // Convert a VPTERNLOG immediate by moving operand 2 to the beginning.
  uint8_t Imm = N->getZExtValue();
  // Move bits 1->4, 2->1, 3->5, 4->2, 5->6, 6->3
  uint8_t NewImm = Imm & 0x81;
  if (Imm & 0x02) NewImm |= 0x10;
  if (Imm & 0x04) NewImm |= 0x02;
  if (Imm & 0x08) NewImm |= 0x20;
  if (Imm & 0x10) NewImm |= 0x04;
  if (Imm & 0x20) NewImm |= 0x40;
  if (Imm & 0x40) NewImm |= 0x08;
  return getI8Imm(NewImm, SDLoc(N));
}]>;

multiclass avx512_ternlog<bits<8> opc, string OpcodeStr, SDNode OpNode,
                          X86FoldableSchedWrite sched, X86VectorVTInfo _,
                          string Name>{
  let Constraints = "$src1 = $dst", ExeDomain = _.ExeDomain in {
  defm rri : AVX512_maskable_3src<opc, MRMSrcReg, _, (outs _.RC:$dst),
                      (ins _.RC:$src2, _.RC:$src3, u8imm:$src4),
                      OpcodeStr, "$src4, $src3, $src2", "$src2, $src3, $src4",
                      (OpNode (_.VT _.RC:$src1),
                              (_.VT _.RC:$src2),
                              (_.VT _.RC:$src3),
                              (i8 imm:$src4)), 1, 1>,
                      AVX512AIi8Base, EVEX_4V, Sched<[sched]>;
  defm rmi : AVX512_maskable_3src<opc, MRMSrcMem, _, (outs _.RC:$dst),
                    (ins _.RC:$src2, _.MemOp:$src3, u8imm:$src4),
                    OpcodeStr, "$src4, $src3, $src2", "$src2, $src3, $src4",
                    (OpNode (_.VT _.RC:$src1),
                            (_.VT _.RC:$src2),
                            (_.VT (bitconvert (_.LdFrag addr:$src3))),
                            (i8 imm:$src4)), 1, 0>,
                    AVX512AIi8Base, EVEX_4V, EVEX_CD8<_.EltSize, CD8VF>,
                    Sched<[sched.Folded, sched.ReadAfterFold]>;
  defm rmbi : AVX512_maskable_3src<opc, MRMSrcMem, _, (outs _.RC:$dst),
                    (ins _.RC:$src2, _.ScalarMemOp:$src3, u8imm:$src4),
                    OpcodeStr, "$src4, ${src3}"##_.BroadcastStr##", $src2",
                    "$src2, ${src3}"##_.BroadcastStr##", $src4",
                    (OpNode (_.VT _.RC:$src1),
                            (_.VT _.RC:$src2),
                            (_.VT (X86VBroadcast(_.ScalarLdFrag addr:$src3))),
                            (i8 imm:$src4)), 1, 0>, EVEX_B,
                    AVX512AIi8Base, EVEX_4V, EVEX_CD8<_.EltSize, CD8VF>,
                    Sched<[sched.Folded, sched.ReadAfterFold]>;
  }// Constraints = "$src1 = $dst"

  // Additional patterns for matching passthru operand in other positions.
  def : Pat<(_.VT (vselect _.KRCWM:$mask,
                   (OpNode _.RC:$src3, _.RC:$src2, _.RC:$src1, (i8 imm:$src4)),
                   _.RC:$src1)),
            (!cast<Instruction>(Name#_.ZSuffix#rrik) _.RC:$src1, _.KRCWM:$mask,
             _.RC:$src2, _.RC:$src3, (VPTERNLOG321_imm8 imm:$src4))>;
  def : Pat<(_.VT (vselect _.KRCWM:$mask,
                   (OpNode _.RC:$src2, _.RC:$src1, _.RC:$src3, (i8 imm:$src4)),
                   _.RC:$src1)),
            (!cast<Instruction>(Name#_.ZSuffix#rrik) _.RC:$src1, _.KRCWM:$mask,
             _.RC:$src2, _.RC:$src3, (VPTERNLOG213_imm8 imm:$src4))>;

  // Additional patterns for matching loads in other positions.
  def : Pat<(_.VT (OpNode (bitconvert (_.LdFrag addr:$src3)),
                          _.RC:$src2, _.RC:$src1, (i8 imm:$src4))),
            (!cast<Instruction>(Name#_.ZSuffix#rmi) _.RC:$src1, _.RC:$src2,
                                   addr:$src3, (VPTERNLOG321_imm8 imm:$src4))>;
  def : Pat<(_.VT (OpNode _.RC:$src1,
                          (bitconvert (_.LdFrag addr:$src3)),
                          _.RC:$src2, (i8 imm:$src4))),
            (!cast<Instruction>(Name#_.ZSuffix#rmi) _.RC:$src1, _.RC:$src2,
                                   addr:$src3, (VPTERNLOG132_imm8 imm:$src4))>;

  // Additional patterns for matching zero masking with loads in other
  // positions.
  def : Pat<(_.VT (vselect _.KRCWM:$mask,
                   (OpNode (bitconvert (_.LdFrag addr:$src3)),
                    _.RC:$src2, _.RC:$src1, (i8 imm:$src4)),
                   _.ImmAllZerosV)),
            (!cast<Instruction>(Name#_.ZSuffix#rmikz) _.RC:$src1, _.KRCWM:$mask,
             _.RC:$src2, addr:$src3, (VPTERNLOG321_imm8 imm:$src4))>;
  def : Pat<(_.VT (vselect _.KRCWM:$mask,
                   (OpNode _.RC:$src1, (bitconvert (_.LdFrag addr:$src3)),
                    _.RC:$src2, (i8 imm:$src4)),
                   _.ImmAllZerosV)),
            (!cast<Instruction>(Name#_.ZSuffix#rmikz) _.RC:$src1, _.KRCWM:$mask,
             _.RC:$src2, addr:$src3, (VPTERNLOG132_imm8 imm:$src4))>;

  // Additional patterns for matching masked loads with different
  // operand orders.
  def : Pat<(_.VT (vselect _.KRCWM:$mask,
                   (OpNode _.RC:$src1, (bitconvert (_.LdFrag addr:$src3)),
                    _.RC:$src2, (i8 imm:$src4)),
                   _.RC:$src1)),
            (!cast<Instruction>(Name#_.ZSuffix#rmik) _.RC:$src1, _.KRCWM:$mask,
             _.RC:$src2, addr:$src3, (VPTERNLOG132_imm8 imm:$src4))>;
  def : Pat<(_.VT (vselect _.KRCWM:$mask,
                   (OpNode (bitconvert (_.LdFrag addr:$src3)),
                    _.RC:$src2, _.RC:$src1, (i8 imm:$src4)),
                   _.RC:$src1)),
            (!cast<Instruction>(Name#_.ZSuffix#rmik) _.RC:$src1, _.KRCWM:$mask,
             _.RC:$src2, addr:$src3, (VPTERNLOG321_imm8 imm:$src4))>;
  def : Pat<(_.VT (vselect _.KRCWM:$mask,
                   (OpNode _.RC:$src2, _.RC:$src1,
                    (bitconvert (_.LdFrag addr:$src3)), (i8 imm:$src4)),
                   _.RC:$src1)),
            (!cast<Instruction>(Name#_.ZSuffix#rmik) _.RC:$src1, _.KRCWM:$mask,
             _.RC:$src2, addr:$src3, (VPTERNLOG213_imm8 imm:$src4))>;
  def : Pat<(_.VT (vselect _.KRCWM:$mask,
                   (OpNode _.RC:$src2, (bitconvert (_.LdFrag addr:$src3)),
                    _.RC:$src1, (i8 imm:$src4)),
                   _.RC:$src1)),
            (!cast<Instruction>(Name#_.ZSuffix#rmik) _.RC:$src1, _.KRCWM:$mask,
             _.RC:$src2, addr:$src3, (VPTERNLOG231_imm8 imm:$src4))>;
  def : Pat<(_.VT (vselect _.KRCWM:$mask,
                   (OpNode (bitconvert (_.LdFrag addr:$src3)),
                    _.RC:$src1, _.RC:$src2, (i8 imm:$src4)),
                   _.RC:$src1)),
            (!cast<Instruction>(Name#_.ZSuffix#rmik) _.RC:$src1, _.KRCWM:$mask,
             _.RC:$src2, addr:$src3, (VPTERNLOG312_imm8 imm:$src4))>;

  // Additional patterns for matching broadcasts in other positions.
  def : Pat<(_.VT (OpNode (X86VBroadcast (_.ScalarLdFrag addr:$src3)),
                          _.RC:$src2, _.RC:$src1, (i8 imm:$src4))),
            (!cast<Instruction>(Name#_.ZSuffix#rmbi) _.RC:$src1, _.RC:$src2,
                                   addr:$src3, (VPTERNLOG321_imm8 imm:$src4))>;
  def : Pat<(_.VT (OpNode _.RC:$src1,
                          (X86VBroadcast (_.ScalarLdFrag addr:$src3)),
                          _.RC:$src2, (i8 imm:$src4))),
            (!cast<Instruction>(Name#_.ZSuffix#rmbi) _.RC:$src1, _.RC:$src2,
                                   addr:$src3, (VPTERNLOG132_imm8 imm:$src4))>;

  // Additional patterns for matching zero masking with broadcasts in other
  // positions.
  def : Pat<(_.VT (vselect _.KRCWM:$mask,
                   (OpNode (X86VBroadcast (_.ScalarLdFrag addr:$src3)),
                    _.RC:$src2, _.RC:$src1, (i8 imm:$src4)),
                   _.ImmAllZerosV)),
            (!cast<Instruction>(Name#_.ZSuffix#rmbikz) _.RC:$src1,
             _.KRCWM:$mask, _.RC:$src2, addr:$src3,
             (VPTERNLOG321_imm8 imm:$src4))>;
  def : Pat<(_.VT (vselect _.KRCWM:$mask,
                   (OpNode _.RC:$src1,
                    (X86VBroadcast (_.ScalarLdFrag addr:$src3)),
                    _.RC:$src2, (i8 imm:$src4)),
                   _.ImmAllZerosV)),
            (!cast<Instruction>(Name#_.ZSuffix#rmbikz) _.RC:$src1,
             _.KRCWM:$mask, _.RC:$src2, addr:$src3,
             (VPTERNLOG132_imm8 imm:$src4))>;

  // Additional patterns for matching masked broadcasts with different
  // operand orders.
  def : Pat<(_.VT (vselect _.KRCWM:$mask,
                   (OpNode _.RC:$src1,
                    (X86VBroadcast (_.ScalarLdFrag addr:$src3)),
                    _.RC:$src2, (i8 imm:$src4)),
                   _.RC:$src1)),
            (!cast<Instruction>(Name#_.ZSuffix#rmbik) _.RC:$src1, _.KRCWM:$mask,
             _.RC:$src2, addr:$src3, (VPTERNLOG132_imm8 imm:$src4))>;
  def : Pat<(_.VT (vselect _.KRCWM:$mask,
                   (OpNode (X86VBroadcast (_.ScalarLdFrag addr:$src3)),
                    _.RC:$src2, _.RC:$src1, (i8 imm:$src4)),
                   _.RC:$src1)),
            (!cast<Instruction>(Name#_.ZSuffix#rmbik) _.RC:$src1, _.KRCWM:$mask,
             _.RC:$src2, addr:$src3, (VPTERNLOG321_imm8 imm:$src4))>;
  def : Pat<(_.VT (vselect _.KRCWM:$mask,
                   (OpNode _.RC:$src2, _.RC:$src1,
                    (X86VBroadcast (_.ScalarLdFrag addr:$src3)),
                    (i8 imm:$src4)), _.RC:$src1)),
            (!cast<Instruction>(Name#_.ZSuffix#rmbik) _.RC:$src1, _.KRCWM:$mask,
             _.RC:$src2, addr:$src3, (VPTERNLOG213_imm8 imm:$src4))>;
  def : Pat<(_.VT (vselect _.KRCWM:$mask,
                   (OpNode _.RC:$src2,
                    (X86VBroadcast (_.ScalarLdFrag addr:$src3)),
                    _.RC:$src1, (i8 imm:$src4)),
                   _.RC:$src1)),
            (!cast<Instruction>(Name#_.ZSuffix#rmbik) _.RC:$src1, _.KRCWM:$mask,
             _.RC:$src2, addr:$src3, (VPTERNLOG231_imm8 imm:$src4))>;
  def : Pat<(_.VT (vselect _.KRCWM:$mask,
                   (OpNode (X86VBroadcast (_.ScalarLdFrag addr:$src3)),
                    _.RC:$src1, _.RC:$src2, (i8 imm:$src4)),
                   _.RC:$src1)),
            (!cast<Instruction>(Name#_.ZSuffix#rmbik) _.RC:$src1, _.KRCWM:$mask,
             _.RC:$src2, addr:$src3, (VPTERNLOG312_imm8 imm:$src4))>;
}

multiclass avx512_common_ternlog<string OpcodeStr, X86SchedWriteWidths sched,
                                 AVX512VLVectorVTInfo _> {
  let Predicates = [HasAVX512] in
    defm Z    : avx512_ternlog<0x25, OpcodeStr, X86vpternlog, sched.ZMM,
                               _.info512, NAME>, EVEX_V512;
  let Predicates = [HasAVX512, HasVLX] in {
    defm Z128 : avx512_ternlog<0x25, OpcodeStr, X86vpternlog, sched.XMM,
                               _.info128, NAME>, EVEX_V128;
    defm Z256 : avx512_ternlog<0x25, OpcodeStr, X86vpternlog, sched.YMM,
                               _.info256, NAME>, EVEX_V256;
  }
}

defm VPTERNLOGD : avx512_common_ternlog<"vpternlogd", SchedWriteVecALU,
                                        avx512vl_i32_info>;
defm VPTERNLOGQ : avx512_common_ternlog<"vpternlogq", SchedWriteVecALU,
                                        avx512vl_i64_info>, VEX_W;

// Patterns to implement vnot using vpternlog instead of creating all ones
// using pcmpeq or vpternlog and then xoring with that. The value 15 is chosen
// so that the result is only dependent on src0. But we use the same source
// for all operands to prevent a false dependency.
// TODO: We should maybe have a more generalized algorithm for folding to
// vpternlog.
let Predicates = [HasAVX512] in {
  def : Pat<(xor VR512:$src, (bc_v64i8 (v16i32 immAllOnesV))),
            (VPTERNLOGQZrri VR512:$src, VR512:$src, VR512:$src, (i8 15))>;
  def : Pat<(xor VR512:$src, (bc_v32i16 (v16i32 immAllOnesV))),
            (VPTERNLOGQZrri VR512:$src, VR512:$src, VR512:$src, (i8 15))>;
  def : Pat<(xor VR512:$src, (bc_v16i32 (v16i32 immAllOnesV))),
            (VPTERNLOGQZrri VR512:$src, VR512:$src, VR512:$src, (i8 15))>;
  def : Pat<(xor VR512:$src, (bc_v8i64 (v16i32 immAllOnesV))),
            (VPTERNLOGQZrri VR512:$src, VR512:$src, VR512:$src, (i8 15))>;
}

let Predicates = [HasAVX512, NoVLX] in {
  def : Pat<(xor VR128X:$src, (bc_v16i8 (v4i32 immAllOnesV))),
            (EXTRACT_SUBREG
             (VPTERNLOGQZrri
              (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR128X:$src, sub_xmm),
              (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR128X:$src, sub_xmm),
              (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR128X:$src, sub_xmm),
              (i8 15)), sub_xmm)>;
  def : Pat<(xor VR128X:$src, (bc_v8i16 (v4i32 immAllOnesV))),
            (EXTRACT_SUBREG
             (VPTERNLOGQZrri
              (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR128X:$src, sub_xmm),
              (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR128X:$src, sub_xmm),
              (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR128X:$src, sub_xmm),
              (i8 15)), sub_xmm)>;
  def : Pat<(xor VR128X:$src, (bc_v4i32 (v4i32 immAllOnesV))),
            (EXTRACT_SUBREG
             (VPTERNLOGQZrri
              (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR128X:$src, sub_xmm),
              (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR128X:$src, sub_xmm),
              (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR128X:$src, sub_xmm),
              (i8 15)), sub_xmm)>;
  def : Pat<(xor VR128X:$src, (bc_v2i64 (v4i32 immAllOnesV))),
            (EXTRACT_SUBREG
             (VPTERNLOGQZrri
              (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR128X:$src, sub_xmm),
              (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR128X:$src, sub_xmm),
              (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR128X:$src, sub_xmm),
              (i8 15)), sub_xmm)>;

  def : Pat<(xor VR256X:$src, (bc_v32i8 (v8i32 immAllOnesV))),
            (EXTRACT_SUBREG
             (VPTERNLOGQZrri
              (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR256X:$src, sub_ymm),
              (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR256X:$src, sub_ymm),
              (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR256X:$src, sub_ymm),
              (i8 15)), sub_ymm)>;
  def : Pat<(xor VR256X:$src, (bc_v16i16 (v8i32 immAllOnesV))),
            (EXTRACT_SUBREG
             (VPTERNLOGQZrri
              (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR256X:$src, sub_ymm),
              (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR256X:$src, sub_ymm),
              (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR256X:$src, sub_ymm),
              (i8 15)), sub_ymm)>;
  def : Pat<(xor VR256X:$src, (bc_v8i32 (v8i32 immAllOnesV))),
            (EXTRACT_SUBREG
             (VPTERNLOGQZrri
              (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR256X:$src, sub_ymm),
              (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR256X:$src, sub_ymm),
              (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR256X:$src, sub_ymm),
              (i8 15)), sub_ymm)>;
  def : Pat<(xor VR256X:$src, (bc_v4i64 (v8i32 immAllOnesV))),
            (EXTRACT_SUBREG
             (VPTERNLOGQZrri
              (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR256X:$src, sub_ymm),
              (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR256X:$src, sub_ymm),
              (INSERT_SUBREG (v8i64 (IMPLICIT_DEF)), VR256X:$src, sub_ymm),
              (i8 15)), sub_ymm)>;
}

let Predicates = [HasVLX] in {
  def : Pat<(xor VR128X:$src, (bc_v16i8 (v4i32 immAllOnesV))),
            (VPTERNLOGQZ128rri VR128X:$src, VR128X:$src, VR128X:$src, (i8 15))>;
  def : Pat<(xor VR128X:$src, (bc_v8i16 (v4i32 immAllOnesV))),
            (VPTERNLOGQZ128rri VR128X:$src, VR128X:$src, VR128X:$src, (i8 15))>;
  def : Pat<(xor VR128X:$src, (bc_v4i32 (v4i32 immAllOnesV))),
            (VPTERNLOGQZ128rri VR128X:$src, VR128X:$src, VR128X:$src, (i8 15))>;
  def : Pat<(xor VR128X:$src, (bc_v2i64 (v4i32 immAllOnesV))),
            (VPTERNLOGQZ128rri VR128X:$src, VR128X:$src, VR128X:$src, (i8 15))>;

  def : Pat<(xor VR256X:$src, (bc_v32i8 (v8i32 immAllOnesV))),
            (VPTERNLOGQZ256rri VR256X:$src, VR256X:$src, VR256X:$src, (i8 15))>;
  def : Pat<(xor VR256X:$src, (bc_v16i16 (v8i32 immAllOnesV))),
            (VPTERNLOGQZ256rri VR256X:$src, VR256X:$src, VR256X:$src, (i8 15))>;
  def : Pat<(xor VR256X:$src, (bc_v8i32 (v8i32 immAllOnesV))),
            (VPTERNLOGQZ256rri VR256X:$src, VR256X:$src, VR256X:$src, (i8 15))>;
  def : Pat<(xor VR256X:$src, (bc_v4i64 (v8i32 immAllOnesV))),
            (VPTERNLOGQZ256rri VR256X:$src, VR256X:$src, VR256X:$src, (i8 15))>;
}

//===----------------------------------------------------------------------===//
// AVX-512 - FixupImm
//===----------------------------------------------------------------------===//

multiclass avx512_fixupimm_packed<bits<8> opc, string OpcodeStr, SDNode OpNode,
                                  X86FoldableSchedWrite sched, X86VectorVTInfo _,
                                  X86VectorVTInfo TblVT>{
  let Constraints = "$src1 = $dst", ExeDomain = _.ExeDomain in {
    defm rri : AVX512_maskable_3src<opc, MRMSrcReg, _, (outs _.RC:$dst),
                        (ins _.RC:$src2, _.RC:$src3, i32u8imm:$src4),
                         OpcodeStr##_.Suffix, "$src4, $src3, $src2", "$src2, $src3, $src4",
                        (OpNode (_.VT _.RC:$src1),
                                (_.VT _.RC:$src2),
                                (TblVT.VT _.RC:$src3),
                                (i32 imm:$src4),
                                (i32 FROUND_CURRENT))>, Sched<[sched]>;
    defm rmi : AVX512_maskable_3src<opc, MRMSrcMem, _, (outs _.RC:$dst),
                      (ins _.RC:$src2, _.MemOp:$src3, i32u8imm:$src4),
                      OpcodeStr##_.Suffix, "$src4, $src3, $src2", "$src2, $src3, $src4",
                      (OpNode (_.VT _.RC:$src1),
                              (_.VT _.RC:$src2),
                              (TblVT.VT (bitconvert (TblVT.LdFrag addr:$src3))),
                              (i32 imm:$src4),
                              (i32 FROUND_CURRENT))>,
                      Sched<[sched.Folded, sched.ReadAfterFold]>;
    defm rmbi : AVX512_maskable_3src<opc, MRMSrcMem, _, (outs _.RC:$dst),
                      (ins _.RC:$src2, _.ScalarMemOp:$src3, i32u8imm:$src4),
                    OpcodeStr##_.Suffix, "$src4, ${src3}"##_.BroadcastStr##", $src2",
                    "$src2, ${src3}"##_.BroadcastStr##", $src4",
                      (OpNode (_.VT _.RC:$src1),
                              (_.VT _.RC:$src2),
                              (TblVT.VT (X86VBroadcast(TblVT.ScalarLdFrag addr:$src3))),
                              (i32 imm:$src4),
                              (i32 FROUND_CURRENT))>,
                    EVEX_B, Sched<[sched.Folded, sched.ReadAfterFold]>;
  } // Constraints = "$src1 = $dst"
}

multiclass avx512_fixupimm_packed_sae<bits<8> opc, string OpcodeStr,
                                      SDNode OpNode, X86FoldableSchedWrite sched,
                                      X86VectorVTInfo _, X86VectorVTInfo TblVT>{
let Constraints = "$src1 = $dst", ExeDomain = _.ExeDomain in {
  defm rrib : AVX512_maskable_3src<opc, MRMSrcReg, _, (outs _.RC:$dst),
                      (ins _.RC:$src2, _.RC:$src3, i32u8imm:$src4),
                      OpcodeStr##_.Suffix, "$src4, {sae}, $src3, $src2",
                      "$src2, $src3, {sae}, $src4",
                      (OpNode (_.VT _.RC:$src1),
                                (_.VT _.RC:$src2),
                                (TblVT.VT _.RC:$src3),
                                (i32 imm:$src4),
                                (i32 FROUND_NO_EXC))>,
                      EVEX_B, Sched<[sched]>;
  }
}

multiclass avx512_fixupimm_scalar<bits<8> opc, string OpcodeStr, SDNode OpNode,
                                  X86FoldableSchedWrite sched, X86VectorVTInfo _,
                                  X86VectorVTInfo _src3VT> {
  let Constraints = "$src1 = $dst" , Predicates = [HasAVX512],
      ExeDomain = _.ExeDomain in {
    defm rri : AVX512_maskable_3src_scalar<opc, MRMSrcReg, _, (outs _.RC:$dst),
                      (ins _.RC:$src2, _.RC:$src3, i32u8imm:$src4),
                      OpcodeStr##_.Suffix, "$src4, $src3, $src2", "$src2, $src3, $src4",
                      (OpNode (_.VT _.RC:$src1),
                              (_.VT _.RC:$src2),
                              (_src3VT.VT _src3VT.RC:$src3),
                              (i32 imm:$src4),
                              (i32 FROUND_CURRENT))>, Sched<[sched]>;
    defm rrib : AVX512_maskable_3src_scalar<opc, MRMSrcReg, _, (outs _.RC:$dst),
                      (ins _.RC:$src2, _.RC:$src3, i32u8imm:$src4),
                      OpcodeStr##_.Suffix, "$src4, {sae}, $src3, $src2",
                      "$src2, $src3, {sae}, $src4",
                      (OpNode (_.VT _.RC:$src1),
                              (_.VT _.RC:$src2),
                              (_src3VT.VT _src3VT.RC:$src3),
                              (i32 imm:$src4),
                              (i32 FROUND_NO_EXC))>,
                      EVEX_B, Sched<[sched.Folded, sched.ReadAfterFold]>;
    defm rmi : AVX512_maskable_3src_scalar<opc, MRMSrcMem, _, (outs _.RC:$dst),
                     (ins _.RC:$src2, _.ScalarMemOp:$src3, i32u8imm:$src4),
                     OpcodeStr##_.Suffix, "$src4, $src3, $src2", "$src2, $src3, $src4",
                     (OpNode (_.VT _.RC:$src1),
                             (_.VT _.RC:$src2),
                             (_src3VT.VT (scalar_to_vector
                                       (_src3VT.ScalarLdFrag addr:$src3))),
                             (i32 imm:$src4),
                             (i32 FROUND_CURRENT))>,
                     Sched<[sched.Folded, sched.ReadAfterFold]>;
  }
}

multiclass avx512_fixupimm_packed_all<X86SchedWriteWidths sched,
                                      AVX512VLVectorVTInfo _Vec, 
                                      AVX512VLVectorVTInfo _Tbl> {
  let Predicates = [HasAVX512] in
    defm Z    : avx512_fixupimm_packed<0x54, "vfixupimm", X86VFixupimm, sched.ZMM,
                                       _Vec.info512, _Tbl.info512>,
                avx512_fixupimm_packed_sae<0x54, "vfixupimm", X86VFixupimm, sched.ZMM,
                                _Vec.info512, _Tbl.info512>, AVX512AIi8Base,
                                EVEX_4V, EVEX_V512;
  let Predicates = [HasAVX512, HasVLX] in {
    defm Z128 : avx512_fixupimm_packed<0x54, "vfixupimm", X86VFixupimm, sched.XMM,
                            _Vec.info128, _Tbl.info128>, AVX512AIi8Base,
                            EVEX_4V, EVEX_V128;
    defm Z256 : avx512_fixupimm_packed<0x54, "vfixupimm", X86VFixupimm, sched.YMM,
                            _Vec.info256, _Tbl.info256>, AVX512AIi8Base,
                            EVEX_4V, EVEX_V256;
  }
}

defm VFIXUPIMMSSZ : avx512_fixupimm_scalar<0x55, "vfixupimm", X86VFixupimmScalar,
                                           SchedWriteFAdd.Scl, f32x_info, v4i32x_info>,
                          AVX512AIi8Base, VEX_LIG, EVEX_4V, EVEX_CD8<32, CD8VT1>;
defm VFIXUPIMMSDZ : avx512_fixupimm_scalar<0x55, "vfixupimm", X86VFixupimmScalar,
                                           SchedWriteFAdd.Scl, f64x_info, v2i64x_info>,
                          AVX512AIi8Base, VEX_LIG, EVEX_4V, EVEX_CD8<64, CD8VT1>, VEX_W;
defm VFIXUPIMMPS : avx512_fixupimm_packed_all<SchedWriteFAdd, avx512vl_f32_info,
                         avx512vl_i32_info>, EVEX_CD8<32, CD8VF>;
defm VFIXUPIMMPD : avx512_fixupimm_packed_all<SchedWriteFAdd, avx512vl_f64_info,
                         avx512vl_i64_info>, EVEX_CD8<64, CD8VF>, VEX_W;

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
multiclass AVX512_scalar_math_fp_patterns<SDNode Op, string OpcPrefix, SDNode MoveNode,
                                           X86VectorVTInfo _, PatLeaf ZeroFP> {
  let Predicates = [HasAVX512] in {
    // extracted scalar math op with insert via movss
    def : Pat<(MoveNode
               (_.VT VR128X:$dst),
               (_.VT (scalar_to_vector
                      (Op (_.EltVT (extractelt (_.VT VR128X:$dst), (iPTR 0))),
                          _.FRC:$src)))),
              (!cast<Instruction>("V"#OpcPrefix#Zrr_Int) _.VT:$dst,
               (_.VT (COPY_TO_REGCLASS _.FRC:$src, VR128X)))>;

    // extracted masked scalar math op with insert via movss
    def : Pat<(MoveNode (_.VT VR128X:$src1),
               (scalar_to_vector
                (X86selects VK1WM:$mask,
                            (Op (_.EltVT
                                 (extractelt (_.VT VR128X:$src1), (iPTR 0))),
                                _.FRC:$src2),
                            _.FRC:$src0))),
              (!cast<Instruction>("V"#OpcPrefix#Zrr_Intk)
               (_.VT (COPY_TO_REGCLASS _.FRC:$src0, VR128X)),
               VK1WM:$mask, _.VT:$src1,
               (_.VT (COPY_TO_REGCLASS _.FRC:$src2, VR128X)))>;

    // extracted masked scalar math op with insert via movss
    def : Pat<(MoveNode (_.VT VR128X:$src1),
               (scalar_to_vector
                (X86selects VK1WM:$mask,
                            (Op (_.EltVT
                                 (extractelt (_.VT VR128X:$src1), (iPTR 0))),
                                _.FRC:$src2), (_.EltVT ZeroFP)))),
      (!cast<I>("V"#OpcPrefix#Zrr_Intkz) 
          VK1WM:$mask, _.VT:$src1,
          (_.VT (COPY_TO_REGCLASS _.FRC:$src2, VR128X)))>;
  }
}

defm : AVX512_scalar_math_fp_patterns<fadd, "ADDSS", X86Movss, v4f32x_info, fp32imm0>;
defm : AVX512_scalar_math_fp_patterns<fsub, "SUBSS", X86Movss, v4f32x_info, fp32imm0>;
defm : AVX512_scalar_math_fp_patterns<fmul, "MULSS", X86Movss, v4f32x_info, fp32imm0>;
defm : AVX512_scalar_math_fp_patterns<fdiv, "DIVSS", X86Movss, v4f32x_info, fp32imm0>;

defm : AVX512_scalar_math_fp_patterns<fadd, "ADDSD", X86Movsd, v2f64x_info, fp64imm0>;
defm : AVX512_scalar_math_fp_patterns<fsub, "SUBSD", X86Movsd, v2f64x_info, fp64imm0>;
defm : AVX512_scalar_math_fp_patterns<fmul, "MULSD", X86Movsd, v2f64x_info, fp64imm0>;
defm : AVX512_scalar_math_fp_patterns<fdiv, "DIVSD", X86Movsd, v2f64x_info, fp64imm0>;

multiclass AVX512_scalar_unary_math_patterns<SDNode OpNode, string OpcPrefix,
                                             SDNode Move, X86VectorVTInfo _> {
  let Predicates = [HasAVX512] in {
    def : Pat<(_.VT (Move _.VT:$dst,
                     (scalar_to_vector (OpNode (extractelt _.VT:$src, 0))))),
              (!cast<Instruction>("V"#OpcPrefix#Zr_Int) _.VT:$dst, _.VT:$src)>;
  }
}

defm : AVX512_scalar_unary_math_patterns<fsqrt, "SQRTSS", X86Movss, v4f32x_info>;
defm : AVX512_scalar_unary_math_patterns<fsqrt, "SQRTSD", X86Movsd, v2f64x_info>;

multiclass AVX512_scalar_unary_math_imm_patterns<SDNode OpNode, string OpcPrefix,
                                                 SDNode Move, X86VectorVTInfo _,
                                                 bits<8> ImmV> {
  let Predicates = [HasAVX512] in {
    def : Pat<(_.VT (Move _.VT:$dst,
                     (scalar_to_vector (OpNode (extractelt _.VT:$src, 0))))),
              (!cast<Instruction>("V"#OpcPrefix#Zr_Int) _.VT:$dst, _.VT:$src,
                                                        (i32 ImmV))>;
  }
}

defm : AVX512_scalar_unary_math_imm_patterns<ffloor, "RNDSCALESS", X86Movss,
                                             v4f32x_info, 0x01>;
defm : AVX512_scalar_unary_math_imm_patterns<fceil, "RNDSCALESS", X86Movss,
                                             v4f32x_info, 0x02>;
defm : AVX512_scalar_unary_math_imm_patterns<ffloor, "RNDSCALESD", X86Movsd,
                                             v2f64x_info, 0x01>;
defm : AVX512_scalar_unary_math_imm_patterns<fceil, "RNDSCALESD", X86Movsd,
                                             v2f64x_info, 0x02>;

//===----------------------------------------------------------------------===//
// AES instructions
//===----------------------------------------------------------------------===//

multiclass avx512_vaes<bits<8> Op, string OpStr, string IntPrefix> {
  let Predicates = [HasVLX, HasVAES] in {
    defm Z128 : AESI_binop_rm_int<Op, OpStr,
                                  !cast<Intrinsic>(IntPrefix),
                                  loadv2i64, 0, VR128X, i128mem>,
                  EVEX_4V, EVEX_CD8<64, CD8VF>, EVEX_V128, VEX_WIG;
    defm Z256 : AESI_binop_rm_int<Op, OpStr,
                                  !cast<Intrinsic>(IntPrefix##"_256"),
                                  loadv4i64, 0, VR256X, i256mem>,
                  EVEX_4V, EVEX_CD8<64, CD8VF>, EVEX_V256, VEX_WIG;
    }
    let Predicates = [HasAVX512, HasVAES] in
    defm Z    : AESI_binop_rm_int<Op, OpStr,
                                  !cast<Intrinsic>(IntPrefix##"_512"),
                                  loadv8i64, 0, VR512, i512mem>,
                  EVEX_4V, EVEX_CD8<64, CD8VF>, EVEX_V512, VEX_WIG;
}

defm VAESENC      : avx512_vaes<0xDC, "vaesenc", "int_x86_aesni_aesenc">;
defm VAESENCLAST  : avx512_vaes<0xDD, "vaesenclast", "int_x86_aesni_aesenclast">;
defm VAESDEC      : avx512_vaes<0xDE, "vaesdec", "int_x86_aesni_aesdec">;
defm VAESDECLAST  : avx512_vaes<0xDF, "vaesdeclast", "int_x86_aesni_aesdeclast">;

//===----------------------------------------------------------------------===//
// PCLMUL instructions - Carry less multiplication
//===----------------------------------------------------------------------===//

let Predicates = [HasAVX512, HasVPCLMULQDQ] in
defm VPCLMULQDQZ : vpclmulqdq<VR512, i512mem, loadv8i64, int_x86_pclmulqdq_512>,
                              EVEX_4V, EVEX_V512, EVEX_CD8<64, CD8VF>, VEX_WIG;

let Predicates = [HasVLX, HasVPCLMULQDQ] in {
defm VPCLMULQDQZ128 : vpclmulqdq<VR128X, i128mem, loadv2i64, int_x86_pclmulqdq>,
                              EVEX_4V, EVEX_V128, EVEX_CD8<64, CD8VF>, VEX_WIG;

defm VPCLMULQDQZ256: vpclmulqdq<VR256X, i256mem, loadv4i64,
                                int_x86_pclmulqdq_256>, EVEX_4V, EVEX_V256,
                                EVEX_CD8<64, CD8VF>, VEX_WIG;
}

// Aliases
defm : vpclmulqdq_aliases<"VPCLMULQDQZ", VR512, i512mem>;
defm : vpclmulqdq_aliases<"VPCLMULQDQZ128", VR128X, i128mem>;
defm : vpclmulqdq_aliases<"VPCLMULQDQZ256", VR256X, i256mem>;

//===----------------------------------------------------------------------===//
// VBMI2
//===----------------------------------------------------------------------===//

multiclass VBMI2_shift_var_rm<bits<8> Op, string OpStr, SDNode OpNode,
                              X86FoldableSchedWrite sched, X86VectorVTInfo VTI> {
  let Constraints = "$src1 = $dst",
      ExeDomain   = VTI.ExeDomain in {
    defm r:   AVX512_maskable_3src<Op, MRMSrcReg, VTI, (outs VTI.RC:$dst),
                (ins VTI.RC:$src2, VTI.RC:$src3), OpStr,
                "$src3, $src2", "$src2, $src3",
                (VTI.VT (OpNode VTI.RC:$src1, VTI.RC:$src2, VTI.RC:$src3))>,
                AVX512FMA3Base, Sched<[sched]>;
    defm m:   AVX512_maskable_3src<Op, MRMSrcMem, VTI, (outs VTI.RC:$dst),
                (ins VTI.RC:$src2, VTI.MemOp:$src3), OpStr,
                "$src3, $src2", "$src2, $src3",
                (VTI.VT (OpNode VTI.RC:$src1, VTI.RC:$src2,
                        (VTI.VT (VTI.LdFrag addr:$src3))))>,
                AVX512FMA3Base,
                Sched<[sched.Folded, sched.ReadAfterFold]>;
  }
}

multiclass VBMI2_shift_var_rmb<bits<8> Op, string OpStr, SDNode OpNode,
                               X86FoldableSchedWrite sched, X86VectorVTInfo VTI>
         : VBMI2_shift_var_rm<Op, OpStr, OpNode, sched, VTI> {
  let Constraints = "$src1 = $dst",
      ExeDomain   = VTI.ExeDomain in
  defm mb:  AVX512_maskable_3src<Op, MRMSrcMem, VTI, (outs VTI.RC:$dst),
              (ins VTI.RC:$src2, VTI.ScalarMemOp:$src3), OpStr,
              "${src3}"##VTI.BroadcastStr##", $src2",
              "$src2, ${src3}"##VTI.BroadcastStr,
              (OpNode VTI.RC:$src1, VTI.RC:$src2,
               (VTI.VT (X86VBroadcast (VTI.ScalarLdFrag addr:$src3))))>,
              AVX512FMA3Base, EVEX_B,
              Sched<[sched.Folded, sched.ReadAfterFold]>;
}

multiclass VBMI2_shift_var_rm_common<bits<8> Op, string OpStr, SDNode OpNode,
                                     X86SchedWriteWidths sched, AVX512VLVectorVTInfo VTI> {
  let Predicates = [HasVBMI2] in
  defm Z      : VBMI2_shift_var_rm<Op, OpStr, OpNode, sched.ZMM, VTI.info512>,
                                   EVEX_V512;
  let Predicates = [HasVBMI2, HasVLX] in {
    defm Z256 : VBMI2_shift_var_rm<Op, OpStr, OpNode, sched.YMM, VTI.info256>,
                                   EVEX_V256;
    defm Z128 : VBMI2_shift_var_rm<Op, OpStr, OpNode, sched.XMM, VTI.info128>,
                                   EVEX_V128;
  }
}

multiclass VBMI2_shift_var_rmb_common<bits<8> Op, string OpStr, SDNode OpNode,
                                      X86SchedWriteWidths sched, AVX512VLVectorVTInfo VTI> {
  let Predicates = [HasVBMI2] in
  defm Z      : VBMI2_shift_var_rmb<Op, OpStr, OpNode, sched.ZMM, VTI.info512>,
                                    EVEX_V512;
  let Predicates = [HasVBMI2, HasVLX] in {
    defm Z256 : VBMI2_shift_var_rmb<Op, OpStr, OpNode, sched.YMM, VTI.info256>,
                                    EVEX_V256;
    defm Z128 : VBMI2_shift_var_rmb<Op, OpStr, OpNode, sched.XMM, VTI.info128>,
                                    EVEX_V128;
  }
}
multiclass VBMI2_shift_var<bits<8> wOp, bits<8> dqOp, string Prefix,
                           SDNode OpNode, X86SchedWriteWidths sched> {
  defm W : VBMI2_shift_var_rm_common<wOp, Prefix##"w", OpNode, sched,
             avx512vl_i16_info>, VEX_W, EVEX_CD8<16, CD8VF>;
  defm D : VBMI2_shift_var_rmb_common<dqOp, Prefix##"d", OpNode, sched,
             avx512vl_i32_info>, EVEX_CD8<32, CD8VF>;
  defm Q : VBMI2_shift_var_rmb_common<dqOp, Prefix##"q", OpNode, sched,
             avx512vl_i64_info>, VEX_W, EVEX_CD8<64, CD8VF>;
}

multiclass VBMI2_shift_imm<bits<8> wOp, bits<8> dqOp, string Prefix,
                           SDNode OpNode, X86SchedWriteWidths sched> {
  defm W : avx512_common_3Op_rm_imm8<wOp, OpNode, Prefix##"w", sched,
             avx512vl_i16_info, avx512vl_i16_info, HasVBMI2>,
             VEX_W, EVEX_CD8<16, CD8VF>;
  defm D : avx512_common_3Op_imm8<Prefix##"d", avx512vl_i32_info, dqOp,
             OpNode, sched, HasVBMI2>, AVX512AIi8Base, EVEX_4V, EVEX_CD8<32, CD8VF>;
  defm Q : avx512_common_3Op_imm8<Prefix##"q", avx512vl_i64_info, dqOp, OpNode,
             sched, HasVBMI2>, AVX512AIi8Base, EVEX_4V, EVEX_CD8<64, CD8VF>, VEX_W;
}

// Concat & Shift
defm VPSHLDV : VBMI2_shift_var<0x70, 0x71, "vpshldv", X86VShldv, SchedWriteVecIMul>;
defm VPSHRDV : VBMI2_shift_var<0x72, 0x73, "vpshrdv", X86VShrdv, SchedWriteVecIMul>;
defm VPSHLD  : VBMI2_shift_imm<0x70, 0x71, "vpshld", X86VShld, SchedWriteVecIMul>;
defm VPSHRD  : VBMI2_shift_imm<0x72, 0x73, "vpshrd", X86VShrd, SchedWriteVecIMul>;

// Compress
defm VPCOMPRESSB : compress_by_elt_width<0x63, "vpcompressb", WriteVarShuffle256,
                                         avx512vl_i8_info, HasVBMI2>, EVEX,
                                         NotMemoryFoldable;
defm VPCOMPRESSW : compress_by_elt_width <0x63, "vpcompressw", WriteVarShuffle256,
                                          avx512vl_i16_info, HasVBMI2>, EVEX, VEX_W,
                                          NotMemoryFoldable;
// Expand
defm VPEXPANDB : expand_by_elt_width <0x62, "vpexpandb", WriteVarShuffle256,
                                      avx512vl_i8_info, HasVBMI2>, EVEX;
defm VPEXPANDW : expand_by_elt_width <0x62, "vpexpandw", WriteVarShuffle256,
                                      avx512vl_i16_info, HasVBMI2>, EVEX, VEX_W;

//===----------------------------------------------------------------------===//
// VNNI
//===----------------------------------------------------------------------===//

let Constraints = "$src1 = $dst" in
multiclass VNNI_rmb<bits<8> Op, string OpStr, SDNode OpNode,
                    X86FoldableSchedWrite sched, X86VectorVTInfo VTI> {
  defm r  :   AVX512_maskable_3src<Op, MRMSrcReg, VTI, (outs VTI.RC:$dst),
                                   (ins VTI.RC:$src2, VTI.RC:$src3), OpStr,
                                   "$src3, $src2", "$src2, $src3",
                                   (VTI.VT (OpNode VTI.RC:$src1,
                                            VTI.RC:$src2, VTI.RC:$src3))>,
                                   EVEX_4V, T8PD, Sched<[sched]>;
  defm m  :   AVX512_maskable_3src<Op, MRMSrcMem, VTI, (outs VTI.RC:$dst),
                                   (ins VTI.RC:$src2, VTI.MemOp:$src3), OpStr,
                                   "$src3, $src2", "$src2, $src3",
                                   (VTI.VT (OpNode VTI.RC:$src1, VTI.RC:$src2,
                                            (VTI.VT (VTI.LdFrag addr:$src3))))>,
                                   EVEX_4V, EVEX_CD8<32, CD8VF>, T8PD,
                                   Sched<[sched.Folded, sched.ReadAfterFold]>;
  defm mb :   AVX512_maskable_3src<Op, MRMSrcMem, VTI, (outs VTI.RC:$dst),
                                   (ins VTI.RC:$src2, VTI.ScalarMemOp:$src3),
                                   OpStr, "${src3}"##VTI.BroadcastStr##", $src2",
                                   "$src2, ${src3}"##VTI.BroadcastStr,
                                   (OpNode VTI.RC:$src1, VTI.RC:$src2,
                                    (VTI.VT (X86VBroadcast
                                             (VTI.ScalarLdFrag addr:$src3))))>,
                                   EVEX_4V, EVEX_CD8<32, CD8VF>, EVEX_B,
                                   T8PD, Sched<[sched.Folded, sched.ReadAfterFold]>;
}

multiclass VNNI_common<bits<8> Op, string OpStr, SDNode OpNode,
                       X86SchedWriteWidths sched> {
  let Predicates = [HasVNNI] in
  defm Z      :   VNNI_rmb<Op, OpStr, OpNode, sched.ZMM, v16i32_info>, EVEX_V512;
  let Predicates = [HasVNNI, HasVLX] in {
    defm Z256 :   VNNI_rmb<Op, OpStr, OpNode, sched.YMM, v8i32x_info>, EVEX_V256;
    defm Z128 :   VNNI_rmb<Op, OpStr, OpNode, sched.XMM, v4i32x_info>, EVEX_V128;
  }
}

// FIXME: Is there a better scheduler class for VPDP?
defm VPDPBUSD   : VNNI_common<0x50, "vpdpbusd", X86Vpdpbusd, SchedWriteVecIMul>;
defm VPDPBUSDS  : VNNI_common<0x51, "vpdpbusds", X86Vpdpbusds, SchedWriteVecIMul>;
defm VPDPWSSD   : VNNI_common<0x52, "vpdpwssd", X86Vpdpwssd, SchedWriteVecIMul>;
defm VPDPWSSDS  : VNNI_common<0x53, "vpdpwssds", X86Vpdpwssds, SchedWriteVecIMul>;

//===----------------------------------------------------------------------===//
// Bit Algorithms
//===----------------------------------------------------------------------===//

// FIXME: Is there a better scheduler class for VPOPCNTB/VPOPCNTW?
defm VPOPCNTB : avx512_unary_rm_vl<0x54, "vpopcntb", ctpop, SchedWriteVecALU,
                                   avx512vl_i8_info, HasBITALG>;
defm VPOPCNTW : avx512_unary_rm_vl<0x54, "vpopcntw", ctpop, SchedWriteVecALU,
                                   avx512vl_i16_info, HasBITALG>, VEX_W;

defm : avx512_unary_lowering<"VPOPCNTB", ctpop, avx512vl_i8_info, HasBITALG>;
defm : avx512_unary_lowering<"VPOPCNTW", ctpop, avx512vl_i16_info, HasBITALG>;

multiclass VPSHUFBITQMB_rm<X86FoldableSchedWrite sched, X86VectorVTInfo VTI> {
  defm rr : AVX512_maskable_cmp<0x8F, MRMSrcReg, VTI, (outs VTI.KRC:$dst),
                                (ins VTI.RC:$src1, VTI.RC:$src2),
                                "vpshufbitqmb",
                                "$src2, $src1", "$src1, $src2",
                                (X86Vpshufbitqmb (VTI.VT VTI.RC:$src1),
                                (VTI.VT VTI.RC:$src2))>, EVEX_4V, T8PD,
                                Sched<[sched]>;
  defm rm : AVX512_maskable_cmp<0x8F, MRMSrcMem, VTI, (outs VTI.KRC:$dst),
                                (ins VTI.RC:$src1, VTI.MemOp:$src2),
                                "vpshufbitqmb",
                                "$src2, $src1", "$src1, $src2",
                                (X86Vpshufbitqmb (VTI.VT VTI.RC:$src1),
                                (VTI.VT (VTI.LdFrag addr:$src2)))>,
                                EVEX_4V, EVEX_CD8<8, CD8VF>, T8PD,
                                Sched<[sched.Folded, sched.ReadAfterFold]>;
}

multiclass VPSHUFBITQMB_common<X86SchedWriteWidths sched, AVX512VLVectorVTInfo VTI> {
  let Predicates = [HasBITALG] in
  defm Z      : VPSHUFBITQMB_rm<sched.ZMM, VTI.info512>, EVEX_V512;
  let Predicates = [HasBITALG, HasVLX] in {
    defm Z256 : VPSHUFBITQMB_rm<sched.YMM, VTI.info256>, EVEX_V256;
    defm Z128 : VPSHUFBITQMB_rm<sched.XMM, VTI.info128>, EVEX_V128;
  }
}

// FIXME: Is there a better scheduler class for VPSHUFBITQMB?
defm VPSHUFBITQMB : VPSHUFBITQMB_common<SchedWriteVecIMul, avx512vl_i8_info>;

//===----------------------------------------------------------------------===//
// GFNI
//===----------------------------------------------------------------------===//

multiclass GF2P8MULB_avx512_common<bits<8> Op, string OpStr, SDNode OpNode,
                                   X86SchedWriteWidths sched> {
  let Predicates = [HasGFNI, HasAVX512, HasBWI] in
  defm Z      : avx512_binop_rm<Op, OpStr, OpNode, v64i8_info, sched.ZMM, 1>,
                                EVEX_V512;
  let Predicates = [HasGFNI, HasVLX, HasBWI] in {
    defm Z256 : avx512_binop_rm<Op, OpStr, OpNode, v32i8x_info, sched.YMM, 1>,
                                EVEX_V256;
    defm Z128 : avx512_binop_rm<Op, OpStr, OpNode, v16i8x_info, sched.XMM, 1>,
                                EVEX_V128;
  }
}

defm VGF2P8MULB : GF2P8MULB_avx512_common<0xCF, "vgf2p8mulb", X86GF2P8mulb,
                                          SchedWriteVecALU>,
                                          EVEX_CD8<8, CD8VF>, T8PD;

multiclass GF2P8AFFINE_avx512_rmb_imm<bits<8> Op, string OpStr, SDNode OpNode,
                                      X86FoldableSchedWrite sched, X86VectorVTInfo VTI,
                                      X86VectorVTInfo BcstVTI>
           : avx512_3Op_rm_imm8<Op, OpStr, OpNode, sched, VTI, VTI> {
  let ExeDomain = VTI.ExeDomain in
  defm rmbi : AVX512_maskable<Op, MRMSrcMem, VTI, (outs VTI.RC:$dst),
                (ins VTI.RC:$src1, VTI.ScalarMemOp:$src2, u8imm:$src3),
                OpStr, "$src3, ${src2}"##BcstVTI.BroadcastStr##", $src1",
                "$src1, ${src2}"##BcstVTI.BroadcastStr##", $src3",
                (OpNode (VTI.VT VTI.RC:$src1),
                 (bitconvert (BcstVTI.VT (X86VBroadcast (loadi64 addr:$src2)))),
                 (i8 imm:$src3))>, EVEX_B,
                 Sched<[sched.Folded, sched.ReadAfterFold]>;
}

multiclass GF2P8AFFINE_avx512_common<bits<8> Op, string OpStr, SDNode OpNode,
                                     X86SchedWriteWidths sched> {
  let Predicates = [HasGFNI, HasAVX512, HasBWI] in
  defm Z      : GF2P8AFFINE_avx512_rmb_imm<Op, OpStr, OpNode, sched.ZMM,
                                           v64i8_info, v8i64_info>, EVEX_V512;
  let Predicates = [HasGFNI, HasVLX, HasBWI] in {
    defm Z256 : GF2P8AFFINE_avx512_rmb_imm<Op, OpStr, OpNode, sched.YMM,
                                           v32i8x_info, v4i64x_info>, EVEX_V256;
    defm Z128 : GF2P8AFFINE_avx512_rmb_imm<Op, OpStr, OpNode, sched.XMM,
                                           v16i8x_info, v2i64x_info>, EVEX_V128;
  }
}

defm VGF2P8AFFINEINVQB : GF2P8AFFINE_avx512_common<0xCF, "vgf2p8affineinvqb",
                         X86GF2P8affineinvqb, SchedWriteVecIMul>,
                         EVEX_4V, EVEX_CD8<8, CD8VF>, VEX_W, AVX512AIi8Base;
defm VGF2P8AFFINEQB    : GF2P8AFFINE_avx512_common<0xCE, "vgf2p8affineqb",
                         X86GF2P8affineqb, SchedWriteVecIMul>,
                         EVEX_4V, EVEX_CD8<8, CD8VF>, VEX_W, AVX512AIi8Base;


//===----------------------------------------------------------------------===//
// AVX5124FMAPS
//===----------------------------------------------------------------------===//

let hasSideEffects = 0, mayLoad = 1, ExeDomain = SSEPackedSingle,
    Constraints = "$src1 = $dst" in {
defm V4FMADDPSrm : AVX512_maskable_3src_in_asm<0x9A, MRMSrcMem, v16f32_info,
                    (outs VR512:$dst), (ins VR512:$src2, f128mem:$src3),
                    "v4fmaddps", "$src3, $src2", "$src2, $src3",
                    []>, EVEX_V512, EVEX_4V, T8XD, EVEX_CD8<32, CD8VQ>,
                    Sched<[SchedWriteFMA.ZMM.Folded]>;

defm V4FNMADDPSrm : AVX512_maskable_3src_in_asm<0xAA, MRMSrcMem, v16f32_info,
                     (outs VR512:$dst), (ins VR512:$src2, f128mem:$src3),
                     "v4fnmaddps", "$src3, $src2", "$src2, $src3",
                     []>, EVEX_V512, EVEX_4V, T8XD, EVEX_CD8<32, CD8VQ>,
                     Sched<[SchedWriteFMA.ZMM.Folded]>;

defm V4FMADDSSrm : AVX512_maskable_3src_in_asm<0x9B, MRMSrcMem, f32x_info,
                    (outs VR128X:$dst), (ins  VR128X:$src2, f128mem:$src3),
                    "v4fmaddss", "$src3, $src2", "$src2, $src3",
                    []>, EVEX_V128, EVEX_4V, T8XD, EVEX_CD8<32, CD8VF>,
                    Sched<[SchedWriteFMA.Scl.Folded]>;

defm V4FNMADDSSrm : AVX512_maskable_3src_in_asm<0xAB, MRMSrcMem, f32x_info,
                     (outs VR128X:$dst), (ins VR128X:$src2, f128mem:$src3),
                     "v4fnmaddss", "$src3, $src2", "$src2, $src3",
                     []>, EVEX_V128, EVEX_4V, T8XD, EVEX_CD8<32, CD8VF>,
                     Sched<[SchedWriteFMA.Scl.Folded]>;
}

//===----------------------------------------------------------------------===//
// AVX5124VNNIW
//===----------------------------------------------------------------------===//

let hasSideEffects = 0, mayLoad = 1, ExeDomain = SSEPackedInt,
    Constraints = "$src1 = $dst" in {
defm VP4DPWSSDrm : AVX512_maskable_3src_in_asm<0x52, MRMSrcMem, v16i32_info,
                    (outs VR512:$dst), (ins VR512:$src2, f128mem:$src3),
                     "vp4dpwssd", "$src3, $src2", "$src2, $src3",
                    []>, EVEX_V512, EVEX_4V, T8XD, EVEX_CD8<32, CD8VQ>,
                    Sched<[SchedWriteFMA.ZMM.Folded]>;

defm VP4DPWSSDSrm : AVX512_maskable_3src_in_asm<0x53, MRMSrcMem, v16i32_info,
                     (outs VR512:$dst), (ins VR512:$src2, f128mem:$src3),
                     "vp4dpwssds", "$src3, $src2", "$src2, $src3",
                     []>, EVEX_V512, EVEX_4V, T8XD, EVEX_CD8<32, CD8VQ>,
                     Sched<[SchedWriteFMA.ZMM.Folded]>;
}

