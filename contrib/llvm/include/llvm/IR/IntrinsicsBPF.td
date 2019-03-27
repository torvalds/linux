//===- IntrinsicsBPF.td - Defines BPF intrinsics -----------*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines all of the BPF-specific intrinsics.
//
//===----------------------------------------------------------------------===//

// Specialized loads from packet
let TargetPrefix = "bpf" in {  // All intrinsics start with "llvm.bpf."
  def int_bpf_load_byte : GCCBuiltin<"__builtin_bpf_load_byte">,
              Intrinsic<[llvm_i64_ty], [llvm_ptr_ty, llvm_i64_ty], [IntrReadMem]>;
  def int_bpf_load_half : GCCBuiltin<"__builtin_bpf_load_half">,
              Intrinsic<[llvm_i64_ty], [llvm_ptr_ty, llvm_i64_ty], [IntrReadMem]>;
  def int_bpf_load_word : GCCBuiltin<"__builtin_bpf_load_word">,
              Intrinsic<[llvm_i64_ty], [llvm_ptr_ty, llvm_i64_ty], [IntrReadMem]>;
  def int_bpf_pseudo : GCCBuiltin<"__builtin_bpf_pseudo">,
              Intrinsic<[llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty]>;
}
