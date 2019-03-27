//===-- llvm/CodeGen/MachineCombinerPattern.h - Instruction pattern supported by
// combiner  ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines instruction pattern supported by combiner
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINECOMBINERPATTERN_H
#define LLVM_CODEGEN_MACHINECOMBINERPATTERN_H

namespace llvm {

/// These are instruction patterns matched by the machine combiner pass.
enum class MachineCombinerPattern {
  // These are commutative variants for reassociating a computation chain. See
  // the comments before getMachineCombinerPatterns() in TargetInstrInfo.cpp.
  REASSOC_AX_BY,
  REASSOC_AX_YB,
  REASSOC_XA_BY,
  REASSOC_XA_YB,

  // These are multiply-add patterns matched by the AArch64 machine combiner.
  MULADDW_OP1,
  MULADDW_OP2,
  MULSUBW_OP1,
  MULSUBW_OP2,
  MULADDWI_OP1,
  MULSUBWI_OP1,
  MULADDX_OP1,
  MULADDX_OP2,
  MULSUBX_OP1,
  MULSUBX_OP2,
  MULADDXI_OP1,
  MULSUBXI_OP1,
  // Floating Point
  FMULADDS_OP1,
  FMULADDS_OP2,
  FMULSUBS_OP1,
  FMULSUBS_OP2,
  FMULADDD_OP1,
  FMULADDD_OP2,
  FMULSUBD_OP1,
  FMULSUBD_OP2,
  FNMULSUBS_OP1,
  FNMULSUBD_OP1,
  FMLAv1i32_indexed_OP1,
  FMLAv1i32_indexed_OP2,
  FMLAv1i64_indexed_OP1,
  FMLAv1i64_indexed_OP2,
  FMLAv2f32_OP2,
  FMLAv2f32_OP1,
  FMLAv2f64_OP1,
  FMLAv2f64_OP2,
  FMLAv2i32_indexed_OP1,
  FMLAv2i32_indexed_OP2,
  FMLAv2i64_indexed_OP1,
  FMLAv2i64_indexed_OP2,
  FMLAv4f32_OP1,
  FMLAv4f32_OP2,
  FMLAv4i32_indexed_OP1,
  FMLAv4i32_indexed_OP2,
  FMLSv1i32_indexed_OP2,
  FMLSv1i64_indexed_OP2,
  FMLSv2f32_OP1,
  FMLSv2f32_OP2,
  FMLSv2f64_OP1,
  FMLSv2f64_OP2,
  FMLSv2i32_indexed_OP1,
  FMLSv2i32_indexed_OP2,
  FMLSv2i64_indexed_OP1,
  FMLSv2i64_indexed_OP2,
  FMLSv4f32_OP1,
  FMLSv4f32_OP2,
  FMLSv4i32_indexed_OP1,
  FMLSv4i32_indexed_OP2
};

} // end namespace llvm

#endif
