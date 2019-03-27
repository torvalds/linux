//===- IntrinsicsARM.td - Defines ARM intrinsics -----------*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines all of the ARM-specific intrinsics.
//
//===----------------------------------------------------------------------===//


//===----------------------------------------------------------------------===//
// TLS

let TargetPrefix = "arm" in {  // All intrinsics start with "llvm.arm.".

// A space-consuming intrinsic primarily for testing ARMConstantIslands. The
// first argument is the number of bytes this "instruction" takes up, the second
// and return value are essentially chains, used to force ordering during ISel.
def int_arm_space : Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], []>;

// 16-bit multiplications
def int_arm_smulbb : GCCBuiltin<"__builtin_arm_smulbb">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], [IntrNoMem]>;
def int_arm_smulbt : GCCBuiltin<"__builtin_arm_smulbt">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], [IntrNoMem]>;
def int_arm_smultb : GCCBuiltin<"__builtin_arm_smultb">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], [IntrNoMem]>;
def int_arm_smultt : GCCBuiltin<"__builtin_arm_smultt">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], [IntrNoMem]>;
def int_arm_smulwb : GCCBuiltin<"__builtin_arm_smulwb">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], [IntrNoMem]>;
def int_arm_smulwt : GCCBuiltin<"__builtin_arm_smulwt">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], [IntrNoMem]>;

//===----------------------------------------------------------------------===//
// Saturating Arithmetic

def int_arm_qadd : GCCBuiltin<"__builtin_arm_qadd">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty],
    [Commutative, IntrNoMem]>;
def int_arm_qsub : GCCBuiltin<"__builtin_arm_qsub">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], [IntrNoMem]>;
def int_arm_ssat : GCCBuiltin<"__builtin_arm_ssat">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], [IntrNoMem]>;
def int_arm_usat : GCCBuiltin<"__builtin_arm_usat">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], [IntrNoMem]>;

// Accumulating multiplications
def int_arm_smlabb : GCCBuiltin<"__builtin_arm_smlabb">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
    [IntrNoMem]>;
def int_arm_smlabt : GCCBuiltin<"__builtin_arm_smlabt">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
    [IntrNoMem]>;
def int_arm_smlatb : GCCBuiltin<"__builtin_arm_smlatb">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
    [IntrNoMem]>;
def int_arm_smlatt : GCCBuiltin<"__builtin_arm_smlatt">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
    [IntrNoMem]>;
def int_arm_smlawb : GCCBuiltin<"__builtin_arm_smlawb">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
    [IntrNoMem]>;
def int_arm_smlawt : GCCBuiltin<"__builtin_arm_smlawt">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
    [IntrNoMem]>;

// Parallel 16-bit saturation
def int_arm_ssat16 : GCCBuiltin<"__builtin_arm_ssat16">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], [IntrNoMem]>;
def int_arm_usat16 : GCCBuiltin<"__builtin_arm_usat16">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], [IntrNoMem]>;

// Packing and unpacking
def int_arm_sxtab16 : GCCBuiltin<"__builtin_arm_sxtab16">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], [IntrNoMem]>;
def int_arm_sxtb16 : GCCBuiltin<"__builtin_arm_sxtb16">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty], [IntrNoMem]>;
def int_arm_uxtab16 : GCCBuiltin<"__builtin_arm_uxtab16">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], [IntrNoMem]>;
def int_arm_uxtb16 : GCCBuiltin<"__builtin_arm_uxtb16">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty], [IntrNoMem]>;

// Parallel selection, reads the GE flags.
def int_arm_sel : GCCBuiltin<"__builtin_arm_sel">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], [IntrReadMem]>;

// Parallel 8-bit addition and subtraction
def int_arm_qadd8  : GCCBuiltin<"__builtin_arm_qadd8">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], [IntrNoMem]>;
def int_arm_qsub8  : GCCBuiltin<"__builtin_arm_qsub8">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], [IntrNoMem]>;
// Writes to the GE bits.
def int_arm_sadd8  : GCCBuiltin<"__builtin_arm_sadd8">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], []>;
def int_arm_shadd8  : GCCBuiltin<"__builtin_arm_shadd8">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], [IntrNoMem]>;
def int_arm_shsub8  : GCCBuiltin<"__builtin_arm_shsub8">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], [IntrNoMem]>;
// Writes to the GE bits.
def int_arm_ssub8  : GCCBuiltin<"__builtin_arm_ssub8">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], []>;
// Writes to the GE bits.
def int_arm_uadd8  : GCCBuiltin<"__builtin_arm_uadd8">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], []>;
def int_arm_uhadd8  : GCCBuiltin<"__builtin_arm_uhadd8">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], [IntrNoMem]>;
def int_arm_uhsub8  : GCCBuiltin<"__builtin_arm_uhsub8">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], [IntrNoMem]>;
def int_arm_uqadd8  : GCCBuiltin<"__builtin_arm_uqadd8">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], [IntrNoMem]>;
def int_arm_uqsub8  : GCCBuiltin<"__builtin_arm_uqsub8">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], [IntrNoMem]>;
// Writes to the GE bits.
def int_arm_usub8  : GCCBuiltin<"__builtin_arm_usub8">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], []>;

// Sum of 8-bit absolute differences
def int_arm_usad8  : GCCBuiltin<"__builtin_arm_usad8">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], [IntrNoMem]>;
def int_arm_usada8  : GCCBuiltin<"__builtin_arm_usada8">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [IntrNoMem]>;

// Parallel 16-bit addition and subtraction
def int_arm_qadd16  : GCCBuiltin<"__builtin_arm_qadd16">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], [IntrNoMem]>;
def int_arm_qasx  : GCCBuiltin<"__builtin_arm_qasx">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], [IntrNoMem]>;
def int_arm_qsax  : GCCBuiltin<"__builtin_arm_qsax">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], [IntrNoMem]>;
def int_arm_qsub16  : GCCBuiltin<"__builtin_arm_qsub16">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], [IntrNoMem]>;
// Writes to the GE bits.
def int_arm_sadd16  : GCCBuiltin<"__builtin_arm_sadd16">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], []>;
// Writes to the GE bits.
def int_arm_sasx  : GCCBuiltin<"__builtin_arm_sasx">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], []>;
def int_arm_shadd16  : GCCBuiltin<"__builtin_arm_shadd16">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], [IntrNoMem]>;
def int_arm_shasx  : GCCBuiltin<"__builtin_arm_shasx">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], [IntrNoMem]>;
def int_arm_shsax  : GCCBuiltin<"__builtin_arm_shsax">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], [IntrNoMem]>;
def int_arm_shsub16  : GCCBuiltin<"__builtin_arm_shsub16">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], [IntrNoMem]>;
// Writes to the GE bits.
def int_arm_ssax  : GCCBuiltin<"__builtin_arm_ssax">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], []>;
// Writes to the GE bits.
def int_arm_ssub16  : GCCBuiltin<"__builtin_arm_ssub16">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], []>;
// Writes to the GE bits.
def int_arm_uadd16  : GCCBuiltin<"__builtin_arm_uadd16">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], []>;
// Writes to the GE bits.
def int_arm_uasx  : GCCBuiltin<"__builtin_arm_uasx">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], []>;
def int_arm_uhadd16  : GCCBuiltin<"__builtin_arm_uhadd16">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], [IntrNoMem]>;
def int_arm_uhasx  : GCCBuiltin<"__builtin_arm_uhasx">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], [IntrNoMem]>;
def int_arm_uhsax  : GCCBuiltin<"__builtin_arm_uhsax">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], [IntrNoMem]>;
def int_arm_uhsub16  : GCCBuiltin<"__builtin_arm_uhsub16">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], [IntrNoMem]>;
def int_arm_uqadd16  : GCCBuiltin<"__builtin_arm_uqadd16">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], [IntrNoMem]>;
def int_arm_uqasx  : GCCBuiltin<"__builtin_arm_uqasx">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], [IntrNoMem]>;
def int_arm_uqsax  : GCCBuiltin<"__builtin_arm_uqsax">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], [IntrNoMem]>;
def int_arm_uqsub16  : GCCBuiltin<"__builtin_arm_uqsub16">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], [IntrNoMem]>;
// Writes to the GE bits.
def int_arm_usax  : GCCBuiltin<"__builtin_arm_usax">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], []>;
// Writes to the GE bits.
def int_arm_usub16  : GCCBuiltin<"__builtin_arm_usub16">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], []>;

// Parallel 16-bit multiplication
def int_arm_smlad : GCCBuiltin<"__builtin_arm_smlad">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [IntrNoMem]>;
def int_arm_smladx : GCCBuiltin<"__builtin_arm_smladx">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [IntrNoMem]>;
def int_arm_smlald : GCCBuiltin<"__builtin_arm_smlald">,
    Intrinsic<[llvm_i64_ty], [llvm_i32_ty, llvm_i32_ty, llvm_i64_ty],
              [IntrNoMem]>;
def int_arm_smlaldx : GCCBuiltin<"__builtin_arm_smlaldx">,
    Intrinsic<[llvm_i64_ty], [llvm_i32_ty, llvm_i32_ty, llvm_i64_ty],
              [IntrNoMem]>;
def int_arm_smlsd : GCCBuiltin<"__builtin_arm_smlsd">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [IntrNoMem]>;
def int_arm_smlsdx : GCCBuiltin<"__builtin_arm_smlsdx">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [IntrNoMem]>;
def int_arm_smlsld : GCCBuiltin<"__builtin_arm_smlsld">,
    Intrinsic<[llvm_i64_ty], [llvm_i32_ty, llvm_i32_ty, llvm_i64_ty],
              [IntrNoMem]>;
def int_arm_smlsldx : GCCBuiltin<"__builtin_arm_smlsldx">,
    Intrinsic<[llvm_i64_ty], [llvm_i32_ty, llvm_i32_ty, llvm_i64_ty],
              [IntrNoMem]>;
def int_arm_smuad : GCCBuiltin<"__builtin_arm_smuad">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], [IntrNoMem]>;
def int_arm_smuadx : GCCBuiltin<"__builtin_arm_smuadx">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], [IntrNoMem]>;
def int_arm_smusd : GCCBuiltin<"__builtin_arm_smusd">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], [IntrNoMem]>;
def int_arm_smusdx : GCCBuiltin<"__builtin_arm_smusdx">,
    Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], [IntrNoMem]>;


//===----------------------------------------------------------------------===//
// Load, Store and Clear exclusive

def int_arm_ldrex : Intrinsic<[llvm_i32_ty], [llvm_anyptr_ty]>;
def int_arm_strex : Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_anyptr_ty]>;

def int_arm_ldaex : Intrinsic<[llvm_i32_ty], [llvm_anyptr_ty]>;
def int_arm_stlex : Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_anyptr_ty]>;

def int_arm_clrex : Intrinsic<[]>;

def int_arm_strexd : Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty,
    llvm_ptr_ty]>;
def int_arm_ldrexd : Intrinsic<[llvm_i32_ty, llvm_i32_ty], [llvm_ptr_ty]>;

def int_arm_stlexd : Intrinsic<[llvm_i32_ty],
                               [llvm_i32_ty, llvm_i32_ty, llvm_ptr_ty]>;
def int_arm_ldaexd : Intrinsic<[llvm_i32_ty, llvm_i32_ty], [llvm_ptr_ty]>;

//===----------------------------------------------------------------------===//
// Data barrier instructions
def int_arm_dmb : GCCBuiltin<"__builtin_arm_dmb">, MSBuiltin<"__dmb">,
                  Intrinsic<[], [llvm_i32_ty]>;
def int_arm_dsb : GCCBuiltin<"__builtin_arm_dsb">, MSBuiltin<"__dsb">,
                  Intrinsic<[], [llvm_i32_ty]>;
def int_arm_isb : GCCBuiltin<"__builtin_arm_isb">, MSBuiltin<"__isb">,
                  Intrinsic<[], [llvm_i32_ty]>;

//===----------------------------------------------------------------------===//
// VFP

def int_arm_get_fpscr : GCCBuiltin<"__builtin_arm_get_fpscr">,
                       Intrinsic<[llvm_i32_ty], [], []>;
def int_arm_set_fpscr : GCCBuiltin<"__builtin_arm_set_fpscr">,
                       Intrinsic<[], [llvm_i32_ty], []>;
def int_arm_vcvtr     : Intrinsic<[llvm_float_ty], [llvm_anyfloat_ty],
                                  [IntrNoMem]>;
def int_arm_vcvtru    : Intrinsic<[llvm_float_ty], [llvm_anyfloat_ty],
                                  [IntrNoMem]>;

//===----------------------------------------------------------------------===//
// Coprocessor

def int_arm_ldc : GCCBuiltin<"__builtin_arm_ldc">,
   Intrinsic<[], [llvm_i32_ty, llvm_i32_ty, llvm_ptr_ty], []>;
def int_arm_ldcl : GCCBuiltin<"__builtin_arm_ldcl">,
   Intrinsic<[], [llvm_i32_ty, llvm_i32_ty, llvm_ptr_ty], []>;
def int_arm_ldc2 : GCCBuiltin<"__builtin_arm_ldc2">,
   Intrinsic<[], [llvm_i32_ty, llvm_i32_ty, llvm_ptr_ty], []>;
def int_arm_ldc2l : GCCBuiltin<"__builtin_arm_ldc2l">,
   Intrinsic<[], [llvm_i32_ty, llvm_i32_ty, llvm_ptr_ty], []>;

def int_arm_stc : GCCBuiltin<"__builtin_arm_stc">,
   Intrinsic<[], [llvm_i32_ty, llvm_i32_ty, llvm_ptr_ty], []>;
def int_arm_stcl : GCCBuiltin<"__builtin_arm_stcl">,
   Intrinsic<[], [llvm_i32_ty, llvm_i32_ty, llvm_ptr_ty], []>;
def int_arm_stc2 : GCCBuiltin<"__builtin_arm_stc2">,
   Intrinsic<[], [llvm_i32_ty, llvm_i32_ty, llvm_ptr_ty], []>;
def int_arm_stc2l : GCCBuiltin<"__builtin_arm_stc2l">,
   Intrinsic<[], [llvm_i32_ty, llvm_i32_ty, llvm_ptr_ty], []>;

// Move to coprocessor
def int_arm_mcr : GCCBuiltin<"__builtin_arm_mcr">,
   Intrinsic<[], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                  llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], []>;
def int_arm_mcr2 : GCCBuiltin<"__builtin_arm_mcr2">,
   Intrinsic<[], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                  llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], []>;

// Move from coprocessor
def int_arm_mrc : GCCBuiltin<"__builtin_arm_mrc">,
                  MSBuiltin<"_MoveFromCoprocessor">,
   Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                             llvm_i32_ty, llvm_i32_ty], []>;
def int_arm_mrc2 : GCCBuiltin<"__builtin_arm_mrc2">,
                   MSBuiltin<"_MoveFromCoprocessor2">,
   Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                             llvm_i32_ty, llvm_i32_ty], []>;

// Coprocessor data processing
def int_arm_cdp : GCCBuiltin<"__builtin_arm_cdp">,
   Intrinsic<[], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                  llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], []>;
def int_arm_cdp2 : GCCBuiltin<"__builtin_arm_cdp2">,
   Intrinsic<[], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                  llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], []>;

// Move from two registers to coprocessor
def int_arm_mcrr : Intrinsic<[], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                                  llvm_i32_ty, llvm_i32_ty], []>;
def int_arm_mcrr2 : Intrinsic<[], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                                   llvm_i32_ty, llvm_i32_ty], []>;

def int_arm_mrrc : Intrinsic<[llvm_i32_ty, llvm_i32_ty], [llvm_i32_ty,
                              llvm_i32_ty, llvm_i32_ty], []>;
def int_arm_mrrc2 : Intrinsic<[llvm_i32_ty, llvm_i32_ty], [llvm_i32_ty,
                               llvm_i32_ty, llvm_i32_ty], []>;

//===----------------------------------------------------------------------===//
// CRC32

def int_arm_crc32b  : Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty],
    [IntrNoMem]>;
def int_arm_crc32cb : Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty],
    [IntrNoMem]>;
def int_arm_crc32h  : Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty],
    [IntrNoMem]>;
def int_arm_crc32ch : Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty],
    [IntrNoMem]>;
def int_arm_crc32w  : Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty],
    [IntrNoMem]>;
def int_arm_crc32cw : Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty],
    [IntrNoMem]>;

//===----------------------------------------------------------------------===//
// HINT

def int_arm_hint : Intrinsic<[], [llvm_i32_ty]>;
def int_arm_dbg : Intrinsic<[], [llvm_i32_ty]>;

//===----------------------------------------------------------------------===//
// UND (reserved undefined sequence)

def int_arm_undefined : Intrinsic<[], [llvm_i32_ty]>;

//===----------------------------------------------------------------------===//
// Advanced SIMD (NEON)

// The following classes do not correspond directly to GCC builtins.
class Neon_1Arg_Intrinsic
  : Intrinsic<[llvm_anyvector_ty], [LLVMMatchType<0>], [IntrNoMem]>;
class Neon_1Arg_Narrow_Intrinsic
  : Intrinsic<[llvm_anyvector_ty], [LLVMExtendedType<0>], [IntrNoMem]>;
class Neon_2Arg_Intrinsic
  : Intrinsic<[llvm_anyvector_ty], [LLVMMatchType<0>, LLVMMatchType<0>],
              [IntrNoMem]>;
class Neon_2Arg_Narrow_Intrinsic
  : Intrinsic<[llvm_anyvector_ty], [LLVMExtendedType<0>, LLVMExtendedType<0>],
              [IntrNoMem]>;
class Neon_2Arg_Long_Intrinsic
  : Intrinsic<[llvm_anyvector_ty], [LLVMTruncatedType<0>, LLVMTruncatedType<0>],
              [IntrNoMem]>;
class Neon_3Arg_Intrinsic
  : Intrinsic<[llvm_anyvector_ty],
              [LLVMMatchType<0>, LLVMMatchType<0>, LLVMMatchType<0>],
              [IntrNoMem]>;
class Neon_3Arg_Long_Intrinsic
  : Intrinsic<[llvm_anyvector_ty],
              [LLVMMatchType<0>, LLVMTruncatedType<0>, LLVMTruncatedType<0>],
              [IntrNoMem]>;

class Neon_1FloatArg_Intrinsic
  : Intrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>], [IntrNoMem]>;

class Neon_CvtFxToFP_Intrinsic
  : Intrinsic<[llvm_anyfloat_ty], [llvm_anyint_ty, llvm_i32_ty], [IntrNoMem]>;
class Neon_CvtFPToFx_Intrinsic
  : Intrinsic<[llvm_anyint_ty], [llvm_anyfloat_ty, llvm_i32_ty], [IntrNoMem]>;
class Neon_CvtFPtoInt_1Arg_Intrinsic
  : Intrinsic<[llvm_anyvector_ty], [llvm_anyvector_ty], [IntrNoMem]>;

class Neon_Compare_Intrinsic
  : Intrinsic<[llvm_anyvector_ty], [llvm_anyvector_ty, LLVMMatchType<1>],
              [IntrNoMem]>;

// The table operands for VTBL and VTBX consist of 1 to 4 v8i8 vectors.
// Besides the table, VTBL has one other v8i8 argument and VTBX has two.
// Overall, the classes range from 2 to 6 v8i8 arguments.
class Neon_Tbl2Arg_Intrinsic
  : Intrinsic<[llvm_v8i8_ty],
              [llvm_v8i8_ty, llvm_v8i8_ty], [IntrNoMem]>;
class Neon_Tbl3Arg_Intrinsic
  : Intrinsic<[llvm_v8i8_ty],
              [llvm_v8i8_ty, llvm_v8i8_ty, llvm_v8i8_ty], [IntrNoMem]>;
class Neon_Tbl4Arg_Intrinsic
  : Intrinsic<[llvm_v8i8_ty],
              [llvm_v8i8_ty, llvm_v8i8_ty, llvm_v8i8_ty, llvm_v8i8_ty],
              [IntrNoMem]>;
class Neon_Tbl5Arg_Intrinsic
  : Intrinsic<[llvm_v8i8_ty],
              [llvm_v8i8_ty, llvm_v8i8_ty, llvm_v8i8_ty, llvm_v8i8_ty,
               llvm_v8i8_ty], [IntrNoMem]>;
class Neon_Tbl6Arg_Intrinsic
  : Intrinsic<[llvm_v8i8_ty],
              [llvm_v8i8_ty, llvm_v8i8_ty, llvm_v8i8_ty, llvm_v8i8_ty,
               llvm_v8i8_ty, llvm_v8i8_ty], [IntrNoMem]>;

// Arithmetic ops

let IntrProperties = [IntrNoMem, Commutative] in {

  // Vector Add.
  def int_arm_neon_vhadds : Neon_2Arg_Intrinsic;
  def int_arm_neon_vhaddu : Neon_2Arg_Intrinsic;
  def int_arm_neon_vrhadds : Neon_2Arg_Intrinsic;
  def int_arm_neon_vrhaddu : Neon_2Arg_Intrinsic;
  def int_arm_neon_vqadds : Neon_2Arg_Intrinsic;
  def int_arm_neon_vqaddu : Neon_2Arg_Intrinsic;
  def int_arm_neon_vraddhn : Neon_2Arg_Narrow_Intrinsic;

  // Vector Multiply.
  def int_arm_neon_vmulp : Neon_2Arg_Intrinsic;
  def int_arm_neon_vqdmulh : Neon_2Arg_Intrinsic;
  def int_arm_neon_vqrdmulh : Neon_2Arg_Intrinsic;
  def int_arm_neon_vmulls : Neon_2Arg_Long_Intrinsic;
  def int_arm_neon_vmullu : Neon_2Arg_Long_Intrinsic;
  def int_arm_neon_vmullp : Neon_2Arg_Long_Intrinsic;
  def int_arm_neon_vqdmull : Neon_2Arg_Long_Intrinsic;

  // Vector Maximum.
  def int_arm_neon_vmaxs : Neon_2Arg_Intrinsic;
  def int_arm_neon_vmaxu : Neon_2Arg_Intrinsic;
  def int_arm_neon_vmaxnm : Neon_2Arg_Intrinsic;

  // Vector Minimum.
  def int_arm_neon_vmins : Neon_2Arg_Intrinsic;
  def int_arm_neon_vminu : Neon_2Arg_Intrinsic;
  def int_arm_neon_vminnm : Neon_2Arg_Intrinsic;

  // Vector Reciprocal Step.
  def int_arm_neon_vrecps : Neon_2Arg_Intrinsic;

  // Vector Reciprocal Square Root Step.
  def int_arm_neon_vrsqrts : Neon_2Arg_Intrinsic;
}

// Vector Subtract.
def int_arm_neon_vhsubs : Neon_2Arg_Intrinsic;
def int_arm_neon_vhsubu : Neon_2Arg_Intrinsic;
def int_arm_neon_vqsubs : Neon_2Arg_Intrinsic;
def int_arm_neon_vqsubu : Neon_2Arg_Intrinsic;
def int_arm_neon_vrsubhn : Neon_2Arg_Narrow_Intrinsic;

// Vector Absolute Compare.
def int_arm_neon_vacge : Neon_Compare_Intrinsic;
def int_arm_neon_vacgt : Neon_Compare_Intrinsic;

// Vector Absolute Differences.
def int_arm_neon_vabds : Neon_2Arg_Intrinsic;
def int_arm_neon_vabdu : Neon_2Arg_Intrinsic;

// Vector Pairwise Add.
def int_arm_neon_vpadd : Neon_2Arg_Intrinsic;

// Vector Pairwise Add Long.
// Note: This is different than the other "long" NEON intrinsics because
// the result vector has half as many elements as the source vector.
// The source and destination vector types must be specified separately.
def int_arm_neon_vpaddls : Intrinsic<[llvm_anyvector_ty], [llvm_anyvector_ty],
                                     [IntrNoMem]>;
def int_arm_neon_vpaddlu : Intrinsic<[llvm_anyvector_ty], [llvm_anyvector_ty],
                                     [IntrNoMem]>;

// Vector Pairwise Add and Accumulate Long.
// Note: This is similar to vpaddl but the destination vector also appears
// as the first argument.
def int_arm_neon_vpadals : Intrinsic<[llvm_anyvector_ty],
                                     [LLVMMatchType<0>, llvm_anyvector_ty],
                                     [IntrNoMem]>;
def int_arm_neon_vpadalu : Intrinsic<[llvm_anyvector_ty],
                                     [LLVMMatchType<0>, llvm_anyvector_ty],
                                     [IntrNoMem]>;

// Vector Pairwise Maximum and Minimum.
def int_arm_neon_vpmaxs : Neon_2Arg_Intrinsic;
def int_arm_neon_vpmaxu : Neon_2Arg_Intrinsic;
def int_arm_neon_vpmins : Neon_2Arg_Intrinsic;
def int_arm_neon_vpminu : Neon_2Arg_Intrinsic;

// Vector Shifts:
//
// The various saturating and rounding vector shift operations need to be
// represented by intrinsics in LLVM, and even the basic VSHL variable shift
// operation cannot be safely translated to LLVM's shift operators.  VSHL can
// be used for both left and right shifts, or even combinations of the two,
// depending on the signs of the shift amounts.  It also has well-defined
// behavior for shift amounts that LLVM leaves undefined.  Only basic shifts
// by constants can be represented with LLVM's shift operators.
//
// The shift counts for these intrinsics are always vectors, even for constant
// shifts, where the constant is replicated.  For consistency with VSHL (and
// other variable shift instructions), left shifts have positive shift counts
// and right shifts have negative shift counts.  This convention is also used
// for constant right shift intrinsics, and to help preserve sanity, the
// intrinsic names use "shift" instead of either "shl" or "shr".  Where
// applicable, signed and unsigned versions of the intrinsics are
// distinguished with "s" and "u" suffixes.  A few NEON shift instructions,
// such as VQSHLU, take signed operands but produce unsigned results; these
// use a "su" suffix.

// Vector Shift.
def int_arm_neon_vshifts : Neon_2Arg_Intrinsic;
def int_arm_neon_vshiftu : Neon_2Arg_Intrinsic;

// Vector Rounding Shift.
def int_arm_neon_vrshifts : Neon_2Arg_Intrinsic;
def int_arm_neon_vrshiftu : Neon_2Arg_Intrinsic;
def int_arm_neon_vrshiftn : Neon_2Arg_Narrow_Intrinsic;

// Vector Saturating Shift.
def int_arm_neon_vqshifts : Neon_2Arg_Intrinsic;
def int_arm_neon_vqshiftu : Neon_2Arg_Intrinsic;
def int_arm_neon_vqshiftsu : Neon_2Arg_Intrinsic;
def int_arm_neon_vqshiftns : Neon_2Arg_Narrow_Intrinsic;
def int_arm_neon_vqshiftnu : Neon_2Arg_Narrow_Intrinsic;
def int_arm_neon_vqshiftnsu : Neon_2Arg_Narrow_Intrinsic;

// Vector Saturating Rounding Shift.
def int_arm_neon_vqrshifts : Neon_2Arg_Intrinsic;
def int_arm_neon_vqrshiftu : Neon_2Arg_Intrinsic;
def int_arm_neon_vqrshiftns : Neon_2Arg_Narrow_Intrinsic;
def int_arm_neon_vqrshiftnu : Neon_2Arg_Narrow_Intrinsic;
def int_arm_neon_vqrshiftnsu : Neon_2Arg_Narrow_Intrinsic;

// Vector Shift and Insert.
def int_arm_neon_vshiftins : Neon_3Arg_Intrinsic;

// Vector Absolute Value and Saturating Absolute Value.
def int_arm_neon_vabs : Neon_1Arg_Intrinsic;
def int_arm_neon_vqabs : Neon_1Arg_Intrinsic;

// Vector Saturating Negate.
def int_arm_neon_vqneg : Neon_1Arg_Intrinsic;

// Vector Count Leading Sign/Zero Bits.
def int_arm_neon_vcls : Neon_1Arg_Intrinsic;

// Vector Reciprocal Estimate.
def int_arm_neon_vrecpe : Neon_1Arg_Intrinsic;

// Vector Reciprocal Square Root Estimate.
def int_arm_neon_vrsqrte : Neon_1Arg_Intrinsic;

// Vector Conversions Between Floating-point and Integer
def int_arm_neon_vcvtau : Neon_CvtFPtoInt_1Arg_Intrinsic;
def int_arm_neon_vcvtas : Neon_CvtFPtoInt_1Arg_Intrinsic;
def int_arm_neon_vcvtnu : Neon_CvtFPtoInt_1Arg_Intrinsic;
def int_arm_neon_vcvtns : Neon_CvtFPtoInt_1Arg_Intrinsic;
def int_arm_neon_vcvtpu : Neon_CvtFPtoInt_1Arg_Intrinsic;
def int_arm_neon_vcvtps : Neon_CvtFPtoInt_1Arg_Intrinsic;
def int_arm_neon_vcvtmu : Neon_CvtFPtoInt_1Arg_Intrinsic;
def int_arm_neon_vcvtms : Neon_CvtFPtoInt_1Arg_Intrinsic;

// Vector Conversions Between Floating-point and Fixed-point.
def int_arm_neon_vcvtfp2fxs : Neon_CvtFPToFx_Intrinsic;
def int_arm_neon_vcvtfp2fxu : Neon_CvtFPToFx_Intrinsic;
def int_arm_neon_vcvtfxs2fp : Neon_CvtFxToFP_Intrinsic;
def int_arm_neon_vcvtfxu2fp : Neon_CvtFxToFP_Intrinsic;

// Vector Conversions Between Half-Precision and Single-Precision.
def int_arm_neon_vcvtfp2hf
    : Intrinsic<[llvm_v4i16_ty], [llvm_v4f32_ty], [IntrNoMem]>;
def int_arm_neon_vcvthf2fp
    : Intrinsic<[llvm_v4f32_ty], [llvm_v4i16_ty], [IntrNoMem]>;

// Narrowing Saturating Vector Moves.
def int_arm_neon_vqmovns : Neon_1Arg_Narrow_Intrinsic;
def int_arm_neon_vqmovnu : Neon_1Arg_Narrow_Intrinsic;
def int_arm_neon_vqmovnsu : Neon_1Arg_Narrow_Intrinsic;

// Vector Table Lookup.
// The first 1-4 arguments are the table.
def int_arm_neon_vtbl1 : Neon_Tbl2Arg_Intrinsic;
def int_arm_neon_vtbl2 : Neon_Tbl3Arg_Intrinsic;
def int_arm_neon_vtbl3 : Neon_Tbl4Arg_Intrinsic;
def int_arm_neon_vtbl4 : Neon_Tbl5Arg_Intrinsic;

// Vector Table Extension.
// Some elements of the destination vector may not be updated, so the original
// value of that vector is passed as the first argument.  The next 1-4
// arguments after that are the table.
def int_arm_neon_vtbx1 : Neon_Tbl3Arg_Intrinsic;
def int_arm_neon_vtbx2 : Neon_Tbl4Arg_Intrinsic;
def int_arm_neon_vtbx3 : Neon_Tbl5Arg_Intrinsic;
def int_arm_neon_vtbx4 : Neon_Tbl6Arg_Intrinsic;

// Vector and Scalar Rounding.
def int_arm_neon_vrintn : Neon_1FloatArg_Intrinsic;
def int_arm_neon_vrintx : Neon_1Arg_Intrinsic;
def int_arm_neon_vrinta : Neon_1Arg_Intrinsic;
def int_arm_neon_vrintz : Neon_1Arg_Intrinsic;
def int_arm_neon_vrintm : Neon_1Arg_Intrinsic;
def int_arm_neon_vrintp : Neon_1Arg_Intrinsic;

// De-interleaving vector loads from N-element structures.
// Source operands are the address and alignment.
def int_arm_neon_vld1 : Intrinsic<[llvm_anyvector_ty],
                                  [llvm_anyptr_ty, llvm_i32_ty],
                                  [IntrReadMem, IntrArgMemOnly]>;
def int_arm_neon_vld2 : Intrinsic<[llvm_anyvector_ty, LLVMMatchType<0>],
                                  [llvm_anyptr_ty, llvm_i32_ty],
                                  [IntrReadMem, IntrArgMemOnly]>;
def int_arm_neon_vld3 : Intrinsic<[llvm_anyvector_ty, LLVMMatchType<0>,
                                   LLVMMatchType<0>],
                                  [llvm_anyptr_ty, llvm_i32_ty],
                                  [IntrReadMem, IntrArgMemOnly]>;
def int_arm_neon_vld4 : Intrinsic<[llvm_anyvector_ty, LLVMMatchType<0>,
                                   LLVMMatchType<0>, LLVMMatchType<0>],
                                  [llvm_anyptr_ty, llvm_i32_ty],
                                  [IntrReadMem, IntrArgMemOnly]>;

def int_arm_neon_vld1x2 : Intrinsic<[llvm_anyvector_ty, LLVMMatchType<0>],
                                    [LLVMAnyPointerType<LLVMMatchType<0>>],
                                    [IntrReadMem, IntrArgMemOnly]>;
def int_arm_neon_vld1x3 : Intrinsic<[llvm_anyvector_ty, LLVMMatchType<0>,
                                     LLVMMatchType<0>],
                                    [LLVMAnyPointerType<LLVMMatchType<0>>],
                                    [IntrReadMem, IntrArgMemOnly]>;
def int_arm_neon_vld1x4 : Intrinsic<[llvm_anyvector_ty, LLVMMatchType<0>,
                                     LLVMMatchType<0>, LLVMMatchType<0>],
                                    [LLVMAnyPointerType<LLVMMatchType<0>>],
                                    [IntrReadMem, IntrArgMemOnly]>;

// Vector load N-element structure to one lane.
// Source operands are: the address, the N input vectors (since only one
// lane is assigned), the lane number, and the alignment.
def int_arm_neon_vld2lane : Intrinsic<[llvm_anyvector_ty, LLVMMatchType<0>],
                                      [llvm_anyptr_ty, LLVMMatchType<0>,
                                       LLVMMatchType<0>, llvm_i32_ty,
                                       llvm_i32_ty], [IntrReadMem, IntrArgMemOnly]>;
def int_arm_neon_vld3lane : Intrinsic<[llvm_anyvector_ty, LLVMMatchType<0>,
                                       LLVMMatchType<0>],
                                      [llvm_anyptr_ty, LLVMMatchType<0>,
                                       LLVMMatchType<0>, LLVMMatchType<0>,
                                       llvm_i32_ty, llvm_i32_ty],
                                      [IntrReadMem, IntrArgMemOnly]>;
def int_arm_neon_vld4lane : Intrinsic<[llvm_anyvector_ty, LLVMMatchType<0>,
                                       LLVMMatchType<0>, LLVMMatchType<0>],
                                      [llvm_anyptr_ty, LLVMMatchType<0>,
                                       LLVMMatchType<0>, LLVMMatchType<0>,
                                       LLVMMatchType<0>, llvm_i32_ty,
                                       llvm_i32_ty], [IntrReadMem, IntrArgMemOnly]>;

// Vector load N-element structure to all lanes.
// Source operands are the address and alignment.
def int_arm_neon_vld2dup : Intrinsic<[llvm_anyvector_ty, LLVMMatchType<0>],
                                     [llvm_anyptr_ty, llvm_i32_ty],
                                     [IntrReadMem, IntrArgMemOnly]>;
def int_arm_neon_vld3dup : Intrinsic<[llvm_anyvector_ty, LLVMMatchType<0>,
                                      LLVMMatchType<0>],
                                     [llvm_anyptr_ty, llvm_i32_ty],
                                     [IntrReadMem, IntrArgMemOnly]>;
def int_arm_neon_vld4dup : Intrinsic<[llvm_anyvector_ty, LLVMMatchType<0>,
                                      LLVMMatchType<0>, LLVMMatchType<0>],
                                     [llvm_anyptr_ty, llvm_i32_ty],
                                     [IntrReadMem, IntrArgMemOnly]>;

// Interleaving vector stores from N-element structures.
// Source operands are: the address, the N vectors, and the alignment.
def int_arm_neon_vst1 : Intrinsic<[],
                                  [llvm_anyptr_ty, llvm_anyvector_ty,
                                   llvm_i32_ty], [IntrArgMemOnly]>;
def int_arm_neon_vst2 : Intrinsic<[],
                                  [llvm_anyptr_ty, llvm_anyvector_ty,
                                   LLVMMatchType<1>, llvm_i32_ty],
                                  [IntrArgMemOnly]>;
def int_arm_neon_vst3 : Intrinsic<[],
                                  [llvm_anyptr_ty, llvm_anyvector_ty,
                                   LLVMMatchType<1>, LLVMMatchType<1>,
                                   llvm_i32_ty], [IntrArgMemOnly]>;
def int_arm_neon_vst4 : Intrinsic<[],
                                  [llvm_anyptr_ty, llvm_anyvector_ty,
                                   LLVMMatchType<1>, LLVMMatchType<1>,
                                   LLVMMatchType<1>, llvm_i32_ty],
                                  [IntrArgMemOnly]>;

def int_arm_neon_vst1x2 : Intrinsic<[],
                                    [llvm_anyptr_ty, llvm_anyvector_ty,
                                     LLVMMatchType<1>],
                                    [IntrArgMemOnly, NoCapture<0>]>;
def int_arm_neon_vst1x3 : Intrinsic<[],
                                    [llvm_anyptr_ty, llvm_anyvector_ty,
                                     LLVMMatchType<1>, LLVMMatchType<1>],
                                    [IntrArgMemOnly, NoCapture<0>]>;
def int_arm_neon_vst1x4 : Intrinsic<[],
                                    [llvm_anyptr_ty, llvm_anyvector_ty,
                                     LLVMMatchType<1>, LLVMMatchType<1>,
                                     LLVMMatchType<1>],
                                    [IntrArgMemOnly, NoCapture<0>]>;

// Vector store N-element structure from one lane.
// Source operands are: the address, the N vectors, the lane number, and
// the alignment.
def int_arm_neon_vst2lane : Intrinsic<[],
                                      [llvm_anyptr_ty, llvm_anyvector_ty,
                                       LLVMMatchType<1>, llvm_i32_ty,
                                       llvm_i32_ty], [IntrArgMemOnly]>;
def int_arm_neon_vst3lane : Intrinsic<[],
                                      [llvm_anyptr_ty, llvm_anyvector_ty,
                                       LLVMMatchType<1>, LLVMMatchType<1>,
                                       llvm_i32_ty, llvm_i32_ty],
                                      [IntrArgMemOnly]>;
def int_arm_neon_vst4lane : Intrinsic<[],
                                      [llvm_anyptr_ty, llvm_anyvector_ty,
                                       LLVMMatchType<1>, LLVMMatchType<1>,
                                       LLVMMatchType<1>, llvm_i32_ty,
                                       llvm_i32_ty], [IntrArgMemOnly]>;

// Vector bitwise select.
def int_arm_neon_vbsl : Intrinsic<[llvm_anyvector_ty],
                        [LLVMMatchType<0>, LLVMMatchType<0>, LLVMMatchType<0>],
                        [IntrNoMem]>;


// Crypto instructions
class AES_1Arg_Intrinsic : Intrinsic<[llvm_v16i8_ty],
                                     [llvm_v16i8_ty], [IntrNoMem]>;
class AES_2Arg_Intrinsic : Intrinsic<[llvm_v16i8_ty],
                                     [llvm_v16i8_ty, llvm_v16i8_ty],
                                     [IntrNoMem]>;

class SHA_1Arg_Intrinsic : Intrinsic<[llvm_i32_ty], [llvm_i32_ty],
                                     [IntrNoMem]>;
class SHA_2Arg_Intrinsic : Intrinsic<[llvm_v4i32_ty],
                                     [llvm_v4i32_ty, llvm_v4i32_ty],
                                     [IntrNoMem]>;
class SHA_3Arg_i32_Intrinsic : Intrinsic<[llvm_v4i32_ty],
                                   [llvm_v4i32_ty, llvm_i32_ty, llvm_v4i32_ty],
                                   [IntrNoMem]>;
class SHA_3Arg_v4i32_Intrinsic : Intrinsic<[llvm_v4i32_ty],
                                   [llvm_v4i32_ty, llvm_v4i32_ty,llvm_v4i32_ty],
                                   [IntrNoMem]>;

def int_arm_neon_aesd : AES_2Arg_Intrinsic;
def int_arm_neon_aese : AES_2Arg_Intrinsic;
def int_arm_neon_aesimc : AES_1Arg_Intrinsic;
def int_arm_neon_aesmc : AES_1Arg_Intrinsic;
def int_arm_neon_sha1h : SHA_1Arg_Intrinsic;
def int_arm_neon_sha1su1 : SHA_2Arg_Intrinsic;
def int_arm_neon_sha256su0 : SHA_2Arg_Intrinsic;
def int_arm_neon_sha1c : SHA_3Arg_i32_Intrinsic;
def int_arm_neon_sha1m : SHA_3Arg_i32_Intrinsic;
def int_arm_neon_sha1p : SHA_3Arg_i32_Intrinsic;
def int_arm_neon_sha1su0: SHA_3Arg_v4i32_Intrinsic;
def int_arm_neon_sha256h: SHA_3Arg_v4i32_Intrinsic;
def int_arm_neon_sha256h2: SHA_3Arg_v4i32_Intrinsic;
def int_arm_neon_sha256su1: SHA_3Arg_v4i32_Intrinsic;

// Armv8.2-A dot product instructions
class Neon_Dot_Intrinsic
  : Intrinsic<[llvm_anyvector_ty],
              [LLVMMatchType<0>, llvm_anyvector_ty,
               LLVMMatchType<1>],
              [IntrNoMem]>;
def int_arm_neon_udot : Neon_Dot_Intrinsic;
def int_arm_neon_sdot : Neon_Dot_Intrinsic;


} // end TargetPrefix
