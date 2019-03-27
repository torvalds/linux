//===-- SIIntrinsics.td - SI Intrinsic defs ----------------*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Backend internal SI Intrinsic Definitions. User code should not
// directly use these.
//
//===----------------------------------------------------------------------===//


let TargetPrefix = "SI", isTarget = 1 in {
  def int_SI_load_const : Intrinsic <[llvm_float_ty], [llvm_anyint_ty, llvm_i32_ty], [IntrNoMem]>;

} // End TargetPrefix = "SI", isTarget = 1
