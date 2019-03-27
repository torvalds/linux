//===- IntrinsicsAARCH64.td - Defines AARCH64 intrinsics ---*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines all of the AARCH64-specific intrinsics.
//
//===----------------------------------------------------------------------===//

let TargetPrefix = "aarch64" in {

def int_aarch64_ldxr : Intrinsic<[llvm_i64_ty], [llvm_anyptr_ty]>;
def int_aarch64_ldaxr : Intrinsic<[llvm_i64_ty], [llvm_anyptr_ty]>;
def int_aarch64_stxr : Intrinsic<[llvm_i32_ty], [llvm_i64_ty, llvm_anyptr_ty]>;
def int_aarch64_stlxr : Intrinsic<[llvm_i32_ty], [llvm_i64_ty, llvm_anyptr_ty]>;

def int_aarch64_ldxp : Intrinsic<[llvm_i64_ty, llvm_i64_ty], [llvm_ptr_ty]>;
def int_aarch64_ldaxp : Intrinsic<[llvm_i64_ty, llvm_i64_ty], [llvm_ptr_ty]>;
def int_aarch64_stxp : Intrinsic<[llvm_i32_ty],
                               [llvm_i64_ty, llvm_i64_ty, llvm_ptr_ty]>;
def int_aarch64_stlxp : Intrinsic<[llvm_i32_ty],
                                [llvm_i64_ty, llvm_i64_ty, llvm_ptr_ty]>;

def int_aarch64_clrex : Intrinsic<[]>;

def int_aarch64_sdiv : Intrinsic<[llvm_anyint_ty], [LLVMMatchType<0>,
                                LLVMMatchType<0>], [IntrNoMem]>;
def int_aarch64_udiv : Intrinsic<[llvm_anyint_ty], [LLVMMatchType<0>,
                                LLVMMatchType<0>], [IntrNoMem]>;

//===----------------------------------------------------------------------===//
// HINT

def int_aarch64_hint : Intrinsic<[], [llvm_i32_ty]>;

//===----------------------------------------------------------------------===//
// Data Barrier Instructions

def int_aarch64_dmb : GCCBuiltin<"__builtin_arm_dmb">, MSBuiltin<"__dmb">, Intrinsic<[], [llvm_i32_ty]>;
def int_aarch64_dsb : GCCBuiltin<"__builtin_arm_dsb">, MSBuiltin<"__dsb">, Intrinsic<[], [llvm_i32_ty]>;
def int_aarch64_isb : GCCBuiltin<"__builtin_arm_isb">, MSBuiltin<"__isb">, Intrinsic<[], [llvm_i32_ty]>;

// A space-consuming intrinsic primarily for testing block and jump table
// placements. The first argument is the number of bytes this "instruction"
// takes up, the second and return value are essentially chains, used to force
// ordering during ISel.
def int_aarch64_space : Intrinsic<[llvm_i64_ty], [llvm_i32_ty, llvm_i64_ty], []>;

}

//===----------------------------------------------------------------------===//
// Advanced SIMD (NEON)

let TargetPrefix = "aarch64" in {  // All intrinsics start with "llvm.aarch64.".
  class AdvSIMD_2Scalar_Float_Intrinsic
    : Intrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>, LLVMMatchType<0>],
                [IntrNoMem]>;

  class AdvSIMD_FPToIntRounding_Intrinsic
    : Intrinsic<[llvm_anyint_ty], [llvm_anyfloat_ty], [IntrNoMem]>;

  class AdvSIMD_1IntArg_Intrinsic
    : Intrinsic<[llvm_anyint_ty], [LLVMMatchType<0>], [IntrNoMem]>;
  class AdvSIMD_1FloatArg_Intrinsic
    : Intrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>], [IntrNoMem]>;
  class AdvSIMD_1VectorArg_Intrinsic
    : Intrinsic<[llvm_anyvector_ty], [LLVMMatchType<0>], [IntrNoMem]>;
  class AdvSIMD_1VectorArg_Expand_Intrinsic
    : Intrinsic<[llvm_anyvector_ty], [llvm_anyvector_ty], [IntrNoMem]>;
  class AdvSIMD_1VectorArg_Long_Intrinsic
    : Intrinsic<[llvm_anyvector_ty], [LLVMTruncatedType<0>], [IntrNoMem]>;
  class AdvSIMD_1IntArg_Narrow_Intrinsic
    : Intrinsic<[llvm_anyint_ty], [llvm_anyint_ty], [IntrNoMem]>;
  class AdvSIMD_1VectorArg_Narrow_Intrinsic
    : Intrinsic<[llvm_anyint_ty], [LLVMExtendedType<0>], [IntrNoMem]>;
  class AdvSIMD_1VectorArg_Int_Across_Intrinsic
    : Intrinsic<[llvm_anyint_ty], [llvm_anyvector_ty], [IntrNoMem]>;
  class AdvSIMD_1VectorArg_Float_Across_Intrinsic
    : Intrinsic<[llvm_anyfloat_ty], [llvm_anyvector_ty], [IntrNoMem]>;

  class AdvSIMD_2IntArg_Intrinsic
    : Intrinsic<[llvm_anyint_ty], [LLVMMatchType<0>, LLVMMatchType<0>],
                [IntrNoMem]>;
  class AdvSIMD_2FloatArg_Intrinsic
    : Intrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>, LLVMMatchType<0>],
                [IntrNoMem]>;
  class AdvSIMD_2VectorArg_Intrinsic
    : Intrinsic<[llvm_anyvector_ty], [LLVMMatchType<0>, LLVMMatchType<0>],
                [IntrNoMem]>;
  class AdvSIMD_2VectorArg_Compare_Intrinsic
    : Intrinsic<[llvm_anyvector_ty], [llvm_anyvector_ty, LLVMMatchType<1>],
                [IntrNoMem]>;
  class AdvSIMD_2Arg_FloatCompare_Intrinsic
    : Intrinsic<[llvm_anyint_ty], [llvm_anyfloat_ty, LLVMMatchType<1>],
                [IntrNoMem]>;
  class AdvSIMD_2VectorArg_Long_Intrinsic
    : Intrinsic<[llvm_anyvector_ty],
                [LLVMTruncatedType<0>, LLVMTruncatedType<0>],
                [IntrNoMem]>;
  class AdvSIMD_2VectorArg_Wide_Intrinsic
    : Intrinsic<[llvm_anyvector_ty],
                [LLVMMatchType<0>, LLVMTruncatedType<0>],
                [IntrNoMem]>;
  class AdvSIMD_2VectorArg_Narrow_Intrinsic
    : Intrinsic<[llvm_anyvector_ty],
                [LLVMExtendedType<0>, LLVMExtendedType<0>],
                [IntrNoMem]>;
  class AdvSIMD_2Arg_Scalar_Narrow_Intrinsic
    : Intrinsic<[llvm_anyint_ty],
                [LLVMExtendedType<0>, llvm_i32_ty],
                [IntrNoMem]>;
  class AdvSIMD_2VectorArg_Scalar_Expand_BySize_Intrinsic
    : Intrinsic<[llvm_anyvector_ty],
                [llvm_anyvector_ty],
                [IntrNoMem]>;
  class AdvSIMD_2VectorArg_Scalar_Wide_BySize_Intrinsic
    : Intrinsic<[llvm_anyvector_ty],
                [LLVMTruncatedType<0>],
                [IntrNoMem]>;
  class AdvSIMD_2VectorArg_Scalar_Wide_Intrinsic
    : Intrinsic<[llvm_anyvector_ty],
                [LLVMTruncatedType<0>, llvm_i32_ty],
                [IntrNoMem]>;
  class AdvSIMD_2VectorArg_Tied_Narrow_Intrinsic
    : Intrinsic<[llvm_anyvector_ty],
                [LLVMHalfElementsVectorType<0>, llvm_anyvector_ty],
                [IntrNoMem]>;

  class AdvSIMD_3VectorArg_Intrinsic
      : Intrinsic<[llvm_anyvector_ty],
               [LLVMMatchType<0>, LLVMMatchType<0>, LLVMMatchType<0>],
               [IntrNoMem]>;
  class AdvSIMD_3VectorArg_Scalar_Intrinsic
      : Intrinsic<[llvm_anyvector_ty],
               [LLVMMatchType<0>, LLVMMatchType<0>, llvm_i32_ty],
               [IntrNoMem]>;
  class AdvSIMD_3VectorArg_Tied_Narrow_Intrinsic
      : Intrinsic<[llvm_anyvector_ty],
               [LLVMHalfElementsVectorType<0>, llvm_anyvector_ty,
                LLVMMatchType<1>], [IntrNoMem]>;
  class AdvSIMD_3VectorArg_Scalar_Tied_Narrow_Intrinsic
    : Intrinsic<[llvm_anyvector_ty],
                [LLVMHalfElementsVectorType<0>, llvm_anyvector_ty, llvm_i32_ty],
                [IntrNoMem]>;
  class AdvSIMD_CvtFxToFP_Intrinsic
    : Intrinsic<[llvm_anyfloat_ty], [llvm_anyint_ty, llvm_i32_ty],
                [IntrNoMem]>;
  class AdvSIMD_CvtFPToFx_Intrinsic
    : Intrinsic<[llvm_anyint_ty], [llvm_anyfloat_ty, llvm_i32_ty],
                [IntrNoMem]>;

  class AdvSIMD_1Arg_Intrinsic
    : Intrinsic<[llvm_any_ty], [LLVMMatchType<0>], [IntrNoMem]>;

  class AdvSIMD_Dot_Intrinsic
    : Intrinsic<[llvm_anyvector_ty],
                [LLVMMatchType<0>, llvm_anyvector_ty, LLVMMatchType<1>],
                [IntrNoMem]>;

  class AdvSIMD_FP16FML_Intrinsic
    : Intrinsic<[llvm_anyvector_ty],
                [LLVMMatchType<0>, llvm_anyvector_ty, LLVMMatchType<1>],
                [IntrNoMem]>;
}

// Arithmetic ops

let TargetPrefix = "aarch64", IntrProperties = [IntrNoMem] in {
  // Vector Add Across Lanes
  def int_aarch64_neon_saddv : AdvSIMD_1VectorArg_Int_Across_Intrinsic;
  def int_aarch64_neon_uaddv : AdvSIMD_1VectorArg_Int_Across_Intrinsic;
  def int_aarch64_neon_faddv : AdvSIMD_1VectorArg_Float_Across_Intrinsic;

  // Vector Long Add Across Lanes
  def int_aarch64_neon_saddlv : AdvSIMD_1VectorArg_Int_Across_Intrinsic;
  def int_aarch64_neon_uaddlv : AdvSIMD_1VectorArg_Int_Across_Intrinsic;

  // Vector Halving Add
  def int_aarch64_neon_shadd : AdvSIMD_2VectorArg_Intrinsic;
  def int_aarch64_neon_uhadd : AdvSIMD_2VectorArg_Intrinsic;

  // Vector Rounding Halving Add
  def int_aarch64_neon_srhadd : AdvSIMD_2VectorArg_Intrinsic;
  def int_aarch64_neon_urhadd : AdvSIMD_2VectorArg_Intrinsic;

  // Vector Saturating Add
  def int_aarch64_neon_sqadd : AdvSIMD_2IntArg_Intrinsic;
  def int_aarch64_neon_suqadd : AdvSIMD_2IntArg_Intrinsic;
  def int_aarch64_neon_usqadd : AdvSIMD_2IntArg_Intrinsic;
  def int_aarch64_neon_uqadd : AdvSIMD_2IntArg_Intrinsic;

  // Vector Add High-Half
  // FIXME: this is a legacy intrinsic for aarch64_simd.h. Remove it when that
  // header is no longer supported.
  def int_aarch64_neon_addhn : AdvSIMD_2VectorArg_Narrow_Intrinsic;

  // Vector Rounding Add High-Half
  def int_aarch64_neon_raddhn : AdvSIMD_2VectorArg_Narrow_Intrinsic;

  // Vector Saturating Doubling Multiply High
  def int_aarch64_neon_sqdmulh : AdvSIMD_2IntArg_Intrinsic;

  // Vector Saturating Rounding Doubling Multiply High
  def int_aarch64_neon_sqrdmulh : AdvSIMD_2IntArg_Intrinsic;

  // Vector Polynominal Multiply
  def int_aarch64_neon_pmul : AdvSIMD_2VectorArg_Intrinsic;

  // Vector Long Multiply
  def int_aarch64_neon_smull : AdvSIMD_2VectorArg_Long_Intrinsic;
  def int_aarch64_neon_umull : AdvSIMD_2VectorArg_Long_Intrinsic;
  def int_aarch64_neon_pmull : AdvSIMD_2VectorArg_Long_Intrinsic;

  // 64-bit polynomial multiply really returns an i128, which is not legal. Fake
  // it with a v16i8.
  def int_aarch64_neon_pmull64 :
        Intrinsic<[llvm_v16i8_ty], [llvm_i64_ty, llvm_i64_ty], [IntrNoMem]>;

  // Vector Extending Multiply
  def int_aarch64_neon_fmulx : AdvSIMD_2FloatArg_Intrinsic {
    let IntrProperties = [IntrNoMem, Commutative];
  }

  // Vector Saturating Doubling Long Multiply
  def int_aarch64_neon_sqdmull : AdvSIMD_2VectorArg_Long_Intrinsic;
  def int_aarch64_neon_sqdmulls_scalar
    : Intrinsic<[llvm_i64_ty], [llvm_i32_ty, llvm_i32_ty], [IntrNoMem]>;

  // Vector Halving Subtract
  def int_aarch64_neon_shsub : AdvSIMD_2VectorArg_Intrinsic;
  def int_aarch64_neon_uhsub : AdvSIMD_2VectorArg_Intrinsic;

  // Vector Saturating Subtract
  def int_aarch64_neon_sqsub : AdvSIMD_2IntArg_Intrinsic;
  def int_aarch64_neon_uqsub : AdvSIMD_2IntArg_Intrinsic;

  // Vector Subtract High-Half
  // FIXME: this is a legacy intrinsic for aarch64_simd.h. Remove it when that
  // header is no longer supported.
  def int_aarch64_neon_subhn : AdvSIMD_2VectorArg_Narrow_Intrinsic;

  // Vector Rounding Subtract High-Half
  def int_aarch64_neon_rsubhn : AdvSIMD_2VectorArg_Narrow_Intrinsic;

  // Vector Compare Absolute Greater-than-or-equal
  def int_aarch64_neon_facge : AdvSIMD_2Arg_FloatCompare_Intrinsic;

  // Vector Compare Absolute Greater-than
  def int_aarch64_neon_facgt : AdvSIMD_2Arg_FloatCompare_Intrinsic;

  // Vector Absolute Difference
  def int_aarch64_neon_sabd : AdvSIMD_2VectorArg_Intrinsic;
  def int_aarch64_neon_uabd : AdvSIMD_2VectorArg_Intrinsic;
  def int_aarch64_neon_fabd : AdvSIMD_2VectorArg_Intrinsic;

  // Scalar Absolute Difference
  def int_aarch64_sisd_fabd : AdvSIMD_2Scalar_Float_Intrinsic;

  // Vector Max
  def int_aarch64_neon_smax : AdvSIMD_2VectorArg_Intrinsic;
  def int_aarch64_neon_umax : AdvSIMD_2VectorArg_Intrinsic;
  def int_aarch64_neon_fmax : AdvSIMD_2FloatArg_Intrinsic;
  def int_aarch64_neon_fmaxnmp : AdvSIMD_2VectorArg_Intrinsic;

  // Vector Max Across Lanes
  def int_aarch64_neon_smaxv : AdvSIMD_1VectorArg_Int_Across_Intrinsic;
  def int_aarch64_neon_umaxv : AdvSIMD_1VectorArg_Int_Across_Intrinsic;
  def int_aarch64_neon_fmaxv : AdvSIMD_1VectorArg_Float_Across_Intrinsic;
  def int_aarch64_neon_fmaxnmv : AdvSIMD_1VectorArg_Float_Across_Intrinsic;

  // Vector Min
  def int_aarch64_neon_smin : AdvSIMD_2VectorArg_Intrinsic;
  def int_aarch64_neon_umin : AdvSIMD_2VectorArg_Intrinsic;
  def int_aarch64_neon_fmin : AdvSIMD_2FloatArg_Intrinsic;
  def int_aarch64_neon_fminnmp : AdvSIMD_2VectorArg_Intrinsic;

  // Vector Min/Max Number
  def int_aarch64_neon_fminnm : AdvSIMD_2FloatArg_Intrinsic;
  def int_aarch64_neon_fmaxnm : AdvSIMD_2FloatArg_Intrinsic;

  // Vector Min Across Lanes
  def int_aarch64_neon_sminv : AdvSIMD_1VectorArg_Int_Across_Intrinsic;
  def int_aarch64_neon_uminv : AdvSIMD_1VectorArg_Int_Across_Intrinsic;
  def int_aarch64_neon_fminv : AdvSIMD_1VectorArg_Float_Across_Intrinsic;
  def int_aarch64_neon_fminnmv : AdvSIMD_1VectorArg_Float_Across_Intrinsic;

  // Pairwise Add
  def int_aarch64_neon_addp : AdvSIMD_2VectorArg_Intrinsic;

  // Long Pairwise Add
  // FIXME: In theory, we shouldn't need intrinsics for saddlp or
  // uaddlp, but tblgen's type inference currently can't handle the
  // pattern fragments this ends up generating.
  def int_aarch64_neon_saddlp : AdvSIMD_1VectorArg_Expand_Intrinsic;
  def int_aarch64_neon_uaddlp : AdvSIMD_1VectorArg_Expand_Intrinsic;

  // Folding Maximum
  def int_aarch64_neon_smaxp : AdvSIMD_2VectorArg_Intrinsic;
  def int_aarch64_neon_umaxp : AdvSIMD_2VectorArg_Intrinsic;
  def int_aarch64_neon_fmaxp : AdvSIMD_2VectorArg_Intrinsic;

  // Folding Minimum
  def int_aarch64_neon_sminp : AdvSIMD_2VectorArg_Intrinsic;
  def int_aarch64_neon_uminp : AdvSIMD_2VectorArg_Intrinsic;
  def int_aarch64_neon_fminp : AdvSIMD_2VectorArg_Intrinsic;

  // Reciprocal Estimate/Step
  def int_aarch64_neon_frecps : AdvSIMD_2FloatArg_Intrinsic;
  def int_aarch64_neon_frsqrts : AdvSIMD_2FloatArg_Intrinsic;

  // Reciprocal Exponent
  def int_aarch64_neon_frecpx : AdvSIMD_1FloatArg_Intrinsic;

  // Vector Saturating Shift Left
  def int_aarch64_neon_sqshl : AdvSIMD_2IntArg_Intrinsic;
  def int_aarch64_neon_uqshl : AdvSIMD_2IntArg_Intrinsic;

  // Vector Rounding Shift Left
  def int_aarch64_neon_srshl : AdvSIMD_2IntArg_Intrinsic;
  def int_aarch64_neon_urshl : AdvSIMD_2IntArg_Intrinsic;

  // Vector Saturating Rounding Shift Left
  def int_aarch64_neon_sqrshl : AdvSIMD_2IntArg_Intrinsic;
  def int_aarch64_neon_uqrshl : AdvSIMD_2IntArg_Intrinsic;

  // Vector Signed->Unsigned Shift Left by Constant
  def int_aarch64_neon_sqshlu : AdvSIMD_2IntArg_Intrinsic;

  // Vector Signed->Unsigned Narrowing Saturating Shift Right by Constant
  def int_aarch64_neon_sqshrun : AdvSIMD_2Arg_Scalar_Narrow_Intrinsic;

  // Vector Signed->Unsigned Rounding Narrowing Saturating Shift Right by Const
  def int_aarch64_neon_sqrshrun : AdvSIMD_2Arg_Scalar_Narrow_Intrinsic;

  // Vector Narrowing Shift Right by Constant
  def int_aarch64_neon_sqshrn : AdvSIMD_2Arg_Scalar_Narrow_Intrinsic;
  def int_aarch64_neon_uqshrn : AdvSIMD_2Arg_Scalar_Narrow_Intrinsic;

  // Vector Rounding Narrowing Shift Right by Constant
  def int_aarch64_neon_rshrn : AdvSIMD_2Arg_Scalar_Narrow_Intrinsic;

  // Vector Rounding Narrowing Saturating Shift Right by Constant
  def int_aarch64_neon_sqrshrn : AdvSIMD_2Arg_Scalar_Narrow_Intrinsic;
  def int_aarch64_neon_uqrshrn : AdvSIMD_2Arg_Scalar_Narrow_Intrinsic;

  // Vector Shift Left
  def int_aarch64_neon_sshl : AdvSIMD_2IntArg_Intrinsic;
  def int_aarch64_neon_ushl : AdvSIMD_2IntArg_Intrinsic;

  // Vector Widening Shift Left by Constant
  def int_aarch64_neon_shll : AdvSIMD_2VectorArg_Scalar_Wide_BySize_Intrinsic;
  def int_aarch64_neon_sshll : AdvSIMD_2VectorArg_Scalar_Wide_Intrinsic;
  def int_aarch64_neon_ushll : AdvSIMD_2VectorArg_Scalar_Wide_Intrinsic;

  // Vector Shift Right by Constant and Insert
  def int_aarch64_neon_vsri : AdvSIMD_3VectorArg_Scalar_Intrinsic;

  // Vector Shift Left by Constant and Insert
  def int_aarch64_neon_vsli : AdvSIMD_3VectorArg_Scalar_Intrinsic;

  // Vector Saturating Narrow
  def int_aarch64_neon_scalar_sqxtn: AdvSIMD_1IntArg_Narrow_Intrinsic;
  def int_aarch64_neon_scalar_uqxtn : AdvSIMD_1IntArg_Narrow_Intrinsic;
  def int_aarch64_neon_sqxtn : AdvSIMD_1VectorArg_Narrow_Intrinsic;
  def int_aarch64_neon_uqxtn : AdvSIMD_1VectorArg_Narrow_Intrinsic;

  // Vector Saturating Extract and Unsigned Narrow
  def int_aarch64_neon_scalar_sqxtun : AdvSIMD_1IntArg_Narrow_Intrinsic;
  def int_aarch64_neon_sqxtun : AdvSIMD_1VectorArg_Narrow_Intrinsic;

  // Vector Absolute Value
  def int_aarch64_neon_abs : AdvSIMD_1Arg_Intrinsic;

  // Vector Saturating Absolute Value
  def int_aarch64_neon_sqabs : AdvSIMD_1IntArg_Intrinsic;

  // Vector Saturating Negation
  def int_aarch64_neon_sqneg : AdvSIMD_1IntArg_Intrinsic;

  // Vector Count Leading Sign Bits
  def int_aarch64_neon_cls : AdvSIMD_1VectorArg_Intrinsic;

  // Vector Reciprocal Estimate
  def int_aarch64_neon_urecpe : AdvSIMD_1VectorArg_Intrinsic;
  def int_aarch64_neon_frecpe : AdvSIMD_1FloatArg_Intrinsic;

  // Vector Square Root Estimate
  def int_aarch64_neon_ursqrte : AdvSIMD_1VectorArg_Intrinsic;
  def int_aarch64_neon_frsqrte : AdvSIMD_1FloatArg_Intrinsic;

  // Vector Bitwise Reverse
  def int_aarch64_neon_rbit : AdvSIMD_1VectorArg_Intrinsic;

  // Vector Conversions Between Half-Precision and Single-Precision.
  def int_aarch64_neon_vcvtfp2hf
    : Intrinsic<[llvm_v4i16_ty], [llvm_v4f32_ty], [IntrNoMem]>;
  def int_aarch64_neon_vcvthf2fp
    : Intrinsic<[llvm_v4f32_ty], [llvm_v4i16_ty], [IntrNoMem]>;

  // Vector Conversions Between Floating-point and Fixed-point.
  def int_aarch64_neon_vcvtfp2fxs : AdvSIMD_CvtFPToFx_Intrinsic;
  def int_aarch64_neon_vcvtfp2fxu : AdvSIMD_CvtFPToFx_Intrinsic;
  def int_aarch64_neon_vcvtfxs2fp : AdvSIMD_CvtFxToFP_Intrinsic;
  def int_aarch64_neon_vcvtfxu2fp : AdvSIMD_CvtFxToFP_Intrinsic;

  // Vector FP->Int Conversions
  def int_aarch64_neon_fcvtas : AdvSIMD_FPToIntRounding_Intrinsic;
  def int_aarch64_neon_fcvtau : AdvSIMD_FPToIntRounding_Intrinsic;
  def int_aarch64_neon_fcvtms : AdvSIMD_FPToIntRounding_Intrinsic;
  def int_aarch64_neon_fcvtmu : AdvSIMD_FPToIntRounding_Intrinsic;
  def int_aarch64_neon_fcvtns : AdvSIMD_FPToIntRounding_Intrinsic;
  def int_aarch64_neon_fcvtnu : AdvSIMD_FPToIntRounding_Intrinsic;
  def int_aarch64_neon_fcvtps : AdvSIMD_FPToIntRounding_Intrinsic;
  def int_aarch64_neon_fcvtpu : AdvSIMD_FPToIntRounding_Intrinsic;
  def int_aarch64_neon_fcvtzs : AdvSIMD_FPToIntRounding_Intrinsic;
  def int_aarch64_neon_fcvtzu : AdvSIMD_FPToIntRounding_Intrinsic;

  // Vector FP Rounding: only ties to even is unrepresented by a normal
  // intrinsic.
  def int_aarch64_neon_frintn : AdvSIMD_1FloatArg_Intrinsic;

  // Scalar FP->Int conversions

  // Vector FP Inexact Narrowing
  def int_aarch64_neon_fcvtxn : AdvSIMD_1VectorArg_Expand_Intrinsic;

  // Scalar FP Inexact Narrowing
  def int_aarch64_sisd_fcvtxn : Intrinsic<[llvm_float_ty], [llvm_double_ty],
                                        [IntrNoMem]>;

  // v8.2-A Dot Product
  def int_aarch64_neon_udot : AdvSIMD_Dot_Intrinsic;
  def int_aarch64_neon_sdot : AdvSIMD_Dot_Intrinsic;

  // v8.2-A FP16 Fused Multiply-Add Long
  def int_aarch64_neon_fmlal : AdvSIMD_FP16FML_Intrinsic;
  def int_aarch64_neon_fmlsl : AdvSIMD_FP16FML_Intrinsic;
  def int_aarch64_neon_fmlal2 : AdvSIMD_FP16FML_Intrinsic;
  def int_aarch64_neon_fmlsl2 : AdvSIMD_FP16FML_Intrinsic;
}

let TargetPrefix = "aarch64" in {  // All intrinsics start with "llvm.aarch64.".
  class AdvSIMD_2Vector2Index_Intrinsic
    : Intrinsic<[llvm_anyvector_ty],
                [llvm_anyvector_ty, llvm_i64_ty, LLVMMatchType<0>, llvm_i64_ty],
                [IntrNoMem]>;
}

// Vector element to element moves
def int_aarch64_neon_vcopy_lane: AdvSIMD_2Vector2Index_Intrinsic;

let TargetPrefix = "aarch64" in {  // All intrinsics start with "llvm.aarch64.".
  class AdvSIMD_1Vec_Load_Intrinsic
      : Intrinsic<[llvm_anyvector_ty], [LLVMAnyPointerType<LLVMMatchType<0>>],
                  [IntrReadMem, IntrArgMemOnly]>;
  class AdvSIMD_1Vec_Store_Lane_Intrinsic
    : Intrinsic<[], [llvm_anyvector_ty, llvm_i64_ty, llvm_anyptr_ty],
                [IntrArgMemOnly, NoCapture<2>]>;

  class AdvSIMD_2Vec_Load_Intrinsic
    : Intrinsic<[llvm_anyvector_ty, LLVMMatchType<0>],
                [LLVMAnyPointerType<LLVMMatchType<0>>],
                [IntrReadMem, IntrArgMemOnly]>;
  class AdvSIMD_2Vec_Load_Lane_Intrinsic
    : Intrinsic<[llvm_anyvector_ty, LLVMMatchType<0>],
                [LLVMMatchType<0>, LLVMMatchType<0>,
                 llvm_i64_ty, llvm_anyptr_ty],
                [IntrReadMem, IntrArgMemOnly]>;
  class AdvSIMD_2Vec_Store_Intrinsic
    : Intrinsic<[], [llvm_anyvector_ty, LLVMMatchType<0>,
                     LLVMAnyPointerType<LLVMMatchType<0>>],
                [IntrArgMemOnly, NoCapture<2>]>;
  class AdvSIMD_2Vec_Store_Lane_Intrinsic
    : Intrinsic<[], [llvm_anyvector_ty, LLVMMatchType<0>,
                 llvm_i64_ty, llvm_anyptr_ty],
                [IntrArgMemOnly, NoCapture<3>]>;

  class AdvSIMD_3Vec_Load_Intrinsic
    : Intrinsic<[llvm_anyvector_ty, LLVMMatchType<0>, LLVMMatchType<0>],
                [LLVMAnyPointerType<LLVMMatchType<0>>],
                [IntrReadMem, IntrArgMemOnly]>;
  class AdvSIMD_3Vec_Load_Lane_Intrinsic
    : Intrinsic<[llvm_anyvector_ty, LLVMMatchType<0>, LLVMMatchType<0>],
                [LLVMMatchType<0>, LLVMMatchType<0>, LLVMMatchType<0>,
                 llvm_i64_ty, llvm_anyptr_ty],
                [IntrReadMem, IntrArgMemOnly]>;
  class AdvSIMD_3Vec_Store_Intrinsic
    : Intrinsic<[], [llvm_anyvector_ty, LLVMMatchType<0>,
                     LLVMMatchType<0>, LLVMAnyPointerType<LLVMMatchType<0>>],
                [IntrArgMemOnly, NoCapture<3>]>;
  class AdvSIMD_3Vec_Store_Lane_Intrinsic
    : Intrinsic<[], [llvm_anyvector_ty,
                 LLVMMatchType<0>, LLVMMatchType<0>,
                 llvm_i64_ty, llvm_anyptr_ty],
                [IntrArgMemOnly, NoCapture<4>]>;

  class AdvSIMD_4Vec_Load_Intrinsic
    : Intrinsic<[llvm_anyvector_ty, LLVMMatchType<0>,
                 LLVMMatchType<0>, LLVMMatchType<0>],
                [LLVMAnyPointerType<LLVMMatchType<0>>],
                [IntrReadMem, IntrArgMemOnly]>;
  class AdvSIMD_4Vec_Load_Lane_Intrinsic
    : Intrinsic<[llvm_anyvector_ty, LLVMMatchType<0>,
                 LLVMMatchType<0>, LLVMMatchType<0>],
                [LLVMMatchType<0>, LLVMMatchType<0>,
                 LLVMMatchType<0>, LLVMMatchType<0>,
                 llvm_i64_ty, llvm_anyptr_ty],
                [IntrReadMem, IntrArgMemOnly]>;
  class AdvSIMD_4Vec_Store_Intrinsic
    : Intrinsic<[], [llvm_anyvector_ty, LLVMMatchType<0>,
                 LLVMMatchType<0>, LLVMMatchType<0>,
                 LLVMAnyPointerType<LLVMMatchType<0>>],
                [IntrArgMemOnly, NoCapture<4>]>;
  class AdvSIMD_4Vec_Store_Lane_Intrinsic
    : Intrinsic<[], [llvm_anyvector_ty, LLVMMatchType<0>,
                 LLVMMatchType<0>, LLVMMatchType<0>,
                 llvm_i64_ty, llvm_anyptr_ty],
                [IntrArgMemOnly, NoCapture<5>]>;
}

// Memory ops

def int_aarch64_neon_ld1x2 : AdvSIMD_2Vec_Load_Intrinsic;
def int_aarch64_neon_ld1x3 : AdvSIMD_3Vec_Load_Intrinsic;
def int_aarch64_neon_ld1x4 : AdvSIMD_4Vec_Load_Intrinsic;

def int_aarch64_neon_st1x2 : AdvSIMD_2Vec_Store_Intrinsic;
def int_aarch64_neon_st1x3 : AdvSIMD_3Vec_Store_Intrinsic;
def int_aarch64_neon_st1x4 : AdvSIMD_4Vec_Store_Intrinsic;

def int_aarch64_neon_ld2 : AdvSIMD_2Vec_Load_Intrinsic;
def int_aarch64_neon_ld3 : AdvSIMD_3Vec_Load_Intrinsic;
def int_aarch64_neon_ld4 : AdvSIMD_4Vec_Load_Intrinsic;

def int_aarch64_neon_ld2lane : AdvSIMD_2Vec_Load_Lane_Intrinsic;
def int_aarch64_neon_ld3lane : AdvSIMD_3Vec_Load_Lane_Intrinsic;
def int_aarch64_neon_ld4lane : AdvSIMD_4Vec_Load_Lane_Intrinsic;

def int_aarch64_neon_ld2r : AdvSIMD_2Vec_Load_Intrinsic;
def int_aarch64_neon_ld3r : AdvSIMD_3Vec_Load_Intrinsic;
def int_aarch64_neon_ld4r : AdvSIMD_4Vec_Load_Intrinsic;

def int_aarch64_neon_st2  : AdvSIMD_2Vec_Store_Intrinsic;
def int_aarch64_neon_st3  : AdvSIMD_3Vec_Store_Intrinsic;
def int_aarch64_neon_st4  : AdvSIMD_4Vec_Store_Intrinsic;

def int_aarch64_neon_st2lane  : AdvSIMD_2Vec_Store_Lane_Intrinsic;
def int_aarch64_neon_st3lane  : AdvSIMD_3Vec_Store_Lane_Intrinsic;
def int_aarch64_neon_st4lane  : AdvSIMD_4Vec_Store_Lane_Intrinsic;

let TargetPrefix = "aarch64" in {  // All intrinsics start with "llvm.aarch64.".
  class AdvSIMD_Tbl1_Intrinsic
    : Intrinsic<[llvm_anyvector_ty], [llvm_v16i8_ty, LLVMMatchType<0>],
                [IntrNoMem]>;
  class AdvSIMD_Tbl2_Intrinsic
    : Intrinsic<[llvm_anyvector_ty],
                [llvm_v16i8_ty, llvm_v16i8_ty, LLVMMatchType<0>], [IntrNoMem]>;
  class AdvSIMD_Tbl3_Intrinsic
    : Intrinsic<[llvm_anyvector_ty],
                [llvm_v16i8_ty, llvm_v16i8_ty, llvm_v16i8_ty,
                 LLVMMatchType<0>],
                [IntrNoMem]>;
  class AdvSIMD_Tbl4_Intrinsic
    : Intrinsic<[llvm_anyvector_ty],
                [llvm_v16i8_ty, llvm_v16i8_ty, llvm_v16i8_ty, llvm_v16i8_ty,
                 LLVMMatchType<0>],
                [IntrNoMem]>;

  class AdvSIMD_Tbx1_Intrinsic
    : Intrinsic<[llvm_anyvector_ty],
                [LLVMMatchType<0>, llvm_v16i8_ty, LLVMMatchType<0>],
                [IntrNoMem]>;
  class AdvSIMD_Tbx2_Intrinsic
    : Intrinsic<[llvm_anyvector_ty],
                [LLVMMatchType<0>, llvm_v16i8_ty, llvm_v16i8_ty,
                 LLVMMatchType<0>],
                [IntrNoMem]>;
  class AdvSIMD_Tbx3_Intrinsic
    : Intrinsic<[llvm_anyvector_ty],
                [LLVMMatchType<0>, llvm_v16i8_ty, llvm_v16i8_ty,
                 llvm_v16i8_ty, LLVMMatchType<0>],
                [IntrNoMem]>;
  class AdvSIMD_Tbx4_Intrinsic
    : Intrinsic<[llvm_anyvector_ty],
                [LLVMMatchType<0>, llvm_v16i8_ty, llvm_v16i8_ty,
                 llvm_v16i8_ty, llvm_v16i8_ty, LLVMMatchType<0>],
                [IntrNoMem]>;
}
def int_aarch64_neon_tbl1 : AdvSIMD_Tbl1_Intrinsic;
def int_aarch64_neon_tbl2 : AdvSIMD_Tbl2_Intrinsic;
def int_aarch64_neon_tbl3 : AdvSIMD_Tbl3_Intrinsic;
def int_aarch64_neon_tbl4 : AdvSIMD_Tbl4_Intrinsic;

def int_aarch64_neon_tbx1 : AdvSIMD_Tbx1_Intrinsic;
def int_aarch64_neon_tbx2 : AdvSIMD_Tbx2_Intrinsic;
def int_aarch64_neon_tbx3 : AdvSIMD_Tbx3_Intrinsic;
def int_aarch64_neon_tbx4 : AdvSIMD_Tbx4_Intrinsic;

let TargetPrefix = "aarch64" in {
  class FPCR_Get_Intrinsic
    : Intrinsic<[llvm_i64_ty], [], [IntrNoMem]>;
}

// FPCR
def int_aarch64_get_fpcr : FPCR_Get_Intrinsic;

let TargetPrefix = "aarch64" in {
  class Crypto_AES_DataKey_Intrinsic
    : Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty], [IntrNoMem]>;

  class Crypto_AES_Data_Intrinsic
    : Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty], [IntrNoMem]>;

  // SHA intrinsic taking 5 words of the hash (v4i32, i32) and 4 of the schedule
  // (v4i32).
  class Crypto_SHA_5Hash4Schedule_Intrinsic
    : Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_i32_ty, llvm_v4i32_ty],
                [IntrNoMem]>;

  // SHA intrinsic taking 5 words of the hash (v4i32, i32) and 4 of the schedule
  // (v4i32).
  class Crypto_SHA_1Hash_Intrinsic
    : Intrinsic<[llvm_i32_ty], [llvm_i32_ty], [IntrNoMem]>;

  // SHA intrinsic taking 8 words of the schedule
  class Crypto_SHA_8Schedule_Intrinsic
    : Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty], [IntrNoMem]>;

  // SHA intrinsic taking 12 words of the schedule
  class Crypto_SHA_12Schedule_Intrinsic
    : Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty, llvm_v4i32_ty],
                [IntrNoMem]>;

  // SHA intrinsic taking 8 words of the hash and 4 of the schedule.
  class Crypto_SHA_8Hash4Schedule_Intrinsic
    : Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty, llvm_v4i32_ty],
                [IntrNoMem]>;
}

// AES
def int_aarch64_crypto_aese   : Crypto_AES_DataKey_Intrinsic;
def int_aarch64_crypto_aesd   : Crypto_AES_DataKey_Intrinsic;
def int_aarch64_crypto_aesmc  : Crypto_AES_Data_Intrinsic;
def int_aarch64_crypto_aesimc : Crypto_AES_Data_Intrinsic;

// SHA1
def int_aarch64_crypto_sha1c  : Crypto_SHA_5Hash4Schedule_Intrinsic;
def int_aarch64_crypto_sha1p  : Crypto_SHA_5Hash4Schedule_Intrinsic;
def int_aarch64_crypto_sha1m  : Crypto_SHA_5Hash4Schedule_Intrinsic;
def int_aarch64_crypto_sha1h  : Crypto_SHA_1Hash_Intrinsic;

def int_aarch64_crypto_sha1su0 : Crypto_SHA_12Schedule_Intrinsic;
def int_aarch64_crypto_sha1su1 : Crypto_SHA_8Schedule_Intrinsic;

// SHA256
def int_aarch64_crypto_sha256h   : Crypto_SHA_8Hash4Schedule_Intrinsic;
def int_aarch64_crypto_sha256h2  : Crypto_SHA_8Hash4Schedule_Intrinsic;
def int_aarch64_crypto_sha256su0 : Crypto_SHA_8Schedule_Intrinsic;
def int_aarch64_crypto_sha256su1 : Crypto_SHA_12Schedule_Intrinsic;

//===----------------------------------------------------------------------===//
// CRC32

let TargetPrefix = "aarch64" in {

def int_aarch64_crc32b  : Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty],
    [IntrNoMem]>;
def int_aarch64_crc32cb : Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty],
    [IntrNoMem]>;
def int_aarch64_crc32h  : Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty],
    [IntrNoMem]>;
def int_aarch64_crc32ch : Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty],
    [IntrNoMem]>;
def int_aarch64_crc32w  : Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty],
    [IntrNoMem]>;
def int_aarch64_crc32cw : Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty],
    [IntrNoMem]>;
def int_aarch64_crc32x  : Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i64_ty],
    [IntrNoMem]>;
def int_aarch64_crc32cx : Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i64_ty],
    [IntrNoMem]>;
}
