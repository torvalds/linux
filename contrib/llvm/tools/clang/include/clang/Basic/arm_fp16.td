//===--- arm_fp16.td - ARM FP16 compiler interface ------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the TableGen definitions from which the ARM FP16 header
//  file will be generated.
//
//===----------------------------------------------------------------------===//

include "arm_neon_incl.td"

// ARMv8.2-A FP16 intrinsics.
let ArchGuard = "defined(__ARM_FEATURE_FP16_SCALAR_ARITHMETIC) && defined(__aarch64__)" in {

  // Negate
  def VNEGSH          : SInst<"vneg", "ss", "Sh">;

  // Reciprocal/Sqrt
  def SCALAR_FRECPSH  : IInst<"vrecps", "sss", "Sh">;
  def FSQRTSH         : SInst<"vsqrt", "ss", "Sh">;
  def SCALAR_FRSQRTSH : IInst<"vrsqrts", "sss", "Sh">;

  // Reciprocal Estimate
  def SCALAR_FRECPEH  : IInst<"vrecpe", "ss", "Sh">;

  // Reciprocal Exponent
  def SCALAR_FRECPXH  : IInst<"vrecpx", "ss", "Sh">;

  // Reciprocal Square Root Estimate
  def SCALAR_FRSQRTEH : IInst<"vrsqrte", "ss", "Sh">;

  // Rounding
  def FRINTZ_S64H     : SInst<"vrnd", "ss", "Sh">;
  def FRINTA_S64H     : SInst<"vrnda", "ss", "Sh">;
  def FRINTI_S64H     : SInst<"vrndi", "ss", "Sh">;
  def FRINTM_S64H     : SInst<"vrndm", "ss", "Sh">;
  def FRINTN_S64H     : SInst<"vrndn", "ss", "Sh">;
  def FRINTP_S64H     : SInst<"vrndp", "ss", "Sh">;
  def FRINTX_S64H     : SInst<"vrndx", "ss", "Sh">;

  // Conversion
  def SCALAR_SCVTFSH  : SInst<"vcvth_f16", "Ys", "silUsUiUl">;
  def SCALAR_FCVTZSH  : SInst<"vcvt_s16", "$s", "Sh">;
  def SCALAR_FCVTZSH1 : SInst<"vcvt_s32", "Is", "Sh">;
  def SCALAR_FCVTZSH2 : SInst<"vcvt_s64", "Ls", "Sh">;
  def SCALAR_FCVTZUH  : SInst<"vcvt_u16", "bs", "Sh">;
  def SCALAR_FCVTZUH1 : SInst<"vcvt_u32", "Us", "Sh">;
  def SCALAR_FCVTZUH2 : SInst<"vcvt_u64", "Os", "Sh">;
  def SCALAR_FCVTASH  : SInst<"vcvta_s16", "$s", "Sh">;
  def SCALAR_FCVTASH1 : SInst<"vcvta_s32", "Is", "Sh">;
  def SCALAR_FCVTASH2 : SInst<"vcvta_s64", "Ls", "Sh">;
  def SCALAR_FCVTAUH  : SInst<"vcvta_u16", "bs", "Sh">;
  def SCALAR_FCVTAUH1 : SInst<"vcvta_u32", "Us", "Sh">;
  def SCALAR_FCVTAUH2 : SInst<"vcvta_u64", "Os", "Sh">;
  def SCALAR_FCVTMSH  : SInst<"vcvtm_s16", "$s", "Sh">;
  def SCALAR_FCVTMSH1 : SInst<"vcvtm_s32", "Is", "Sh">;
  def SCALAR_FCVTMSH2 : SInst<"vcvtm_s64", "Ls", "Sh">;
  def SCALAR_FCVTMUH  : SInst<"vcvtm_u16", "bs", "Sh">;
  def SCALAR_FCVTMUH1 : SInst<"vcvtm_u32", "Us", "Sh">;
  def SCALAR_FCVTMUH2 : SInst<"vcvtm_u64", "Os", "Sh">;
  def SCALAR_FCVTNSH  : SInst<"vcvtn_s16", "$s", "Sh">;
  def SCALAR_FCVTNSH1 : SInst<"vcvtn_s32", "Is", "Sh">;
  def SCALAR_FCVTNSH2 : SInst<"vcvtn_s64", "Ls", "Sh">;
  def SCALAR_FCVTNUH  : SInst<"vcvtn_u16", "bs", "Sh">;
  def SCALAR_FCVTNUH1 : SInst<"vcvtn_u32", "Us", "Sh">;
  def SCALAR_FCVTNUH2 : SInst<"vcvtn_u64", "Os", "Sh">;
  def SCALAR_FCVTPSH  : SInst<"vcvtp_s16", "$s", "Sh">;
  def SCALAR_FCVTPSH1 : SInst<"vcvtp_s32", "Is", "Sh">;
  def SCALAR_FCVTPSH2 : SInst<"vcvtp_s64", "Ls", "Sh">;
  def SCALAR_FCVTPUH  : SInst<"vcvtp_u16", "bs", "Sh">;
  def SCALAR_FCVTPUH1 : SInst<"vcvtp_u32", "Us", "Sh">;
  def SCALAR_FCVTPUH2 : SInst<"vcvtp_u64", "Os", "Sh">;
  let isVCVT_N = 1 in {
    def SCALAR_SCVTFSHO : SInst<"vcvth_n_f16", "Ysi", "silUsUiUl">;
    def SCALAR_FCVTZSHO : SInst<"vcvt_n_s16", "$si", "Sh">;
    def SCALAR_FCVTZSH1O: SInst<"vcvt_n_s32", "Isi", "Sh">;
    def SCALAR_FCVTZSH2O: SInst<"vcvt_n_s64", "Lsi", "Sh">;
    def SCALAR_FCVTZUHO : SInst<"vcvt_n_u16", "bsi", "Sh">;
    def SCALAR_FCVTZUH1O: SInst<"vcvt_n_u32", "Usi", "Sh">;
    def SCALAR_FCVTZUH2O: SInst<"vcvt_n_u64", "Osi", "Sh">;
  }
  // Comparison
  def SCALAR_CMEQRH   : SInst<"vceq", "bss", "Sh">;
  def SCALAR_CMEQZH   : SInst<"vceqz", "bs", "Sh">;
  def SCALAR_CMGERH   : SInst<"vcge", "bss", "Sh">;
  def SCALAR_CMGEZH   : SInst<"vcgez", "bs", "Sh">;
  def SCALAR_CMGTRH   : SInst<"vcgt", "bss", "Sh">;
  def SCALAR_CMGTZH   : SInst<"vcgtz", "bs", "Sh">;
  def SCALAR_CMLERH   : SInst<"vcle", "bss", "Sh">;
  def SCALAR_CMLEZH   : SInst<"vclez", "bs", "Sh">;
  def SCALAR_CMLTH    : SInst<"vclt", "bss", "Sh">;
  def SCALAR_CMLTZH   : SInst<"vcltz", "bs", "Sh">;

  // Absolute Compare Mask Greater Than Or Equal
  def SCALAR_FACGEH   : IInst<"vcage", "bss", "Sh">;
  def SCALAR_FACLEH   : IInst<"vcale", "bss", "Sh">;

  // Absolute Compare Mask Greater Than
  def SCALAR_FACGT    : IInst<"vcagt", "bss", "Sh">;
  def SCALAR_FACLT    : IInst<"vcalt", "bss", "Sh">;

  // Scalar Absolute Value
  def SCALAR_ABSH     : SInst<"vabs", "ss", "Sh">;

  // Scalar Absolute Difference
  def SCALAR_ABDH: IInst<"vabd", "sss", "Sh">;

  // Add/Sub
  def VADDSH          : SInst<"vadd", "sss", "Sh">;
  def VSUBHS          : SInst<"vsub", "sss", "Sh">;

  // Max/Min
  def VMAXHS          : SInst<"vmax", "sss", "Sh">;
  def VMINHS          : SInst<"vmin", "sss", "Sh">;
  def FMAXNMHS        : SInst<"vmaxnm", "sss", "Sh">;
  def FMINNMHS        : SInst<"vminnm", "sss", "Sh">;

  // Multiplication/Division
  def VMULHS          : SInst<"vmul", "sss", "Sh">;
  def MULXHS          : SInst<"vmulx", "sss", "Sh">;
  def FDIVHS          : SInst<"vdiv", "sss",  "Sh">;

  // Vector fused multiply-add operations
  def VFMAHS          : SInst<"vfma", "ssss", "Sh">;
  def VFMSHS          : SInst<"vfms", "ssss", "Sh">;
}
