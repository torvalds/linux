// CodeGen/RuntimeLibcallSignatures.cpp - R.T. Lib. Call Signatures -*- C++ -*--
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains signature information for runtime libcalls.
///
/// CodeGen uses external symbols, which it refers to by name. The WebAssembly
/// target needs type information for all functions. This file contains a big
/// table providing type signatures for all runtime library functions that LLVM
/// uses.
///
/// This is currently a fairly heavy-handed solution.
///
//===----------------------------------------------------------------------===//

#include "WebAssemblyRuntimeLibcallSignatures.h"
#include "WebAssemblySubtarget.h"
#include "llvm/CodeGen/RuntimeLibcalls.h"
#include "llvm/Support/ManagedStatic.h"

using namespace llvm;

namespace {

enum RuntimeLibcallSignature {
  func,
  f32_func_f32,
  f32_func_f64,
  f32_func_i32,
  f32_func_i64,
  f32_func_i16,
  f64_func_f32,
  f64_func_f64,
  f64_func_i32,
  f64_func_i64,
  i32_func_f32,
  i32_func_f64,
  i32_func_i32,
  i64_func_f32,
  i64_func_f64,
  i64_func_i64,
  f32_func_f32_f32,
  f32_func_f32_i32,
  f32_func_i64_i64,
  f64_func_f64_f64,
  f64_func_f64_i32,
  f64_func_i64_i64,
  i16_func_f32,
  i8_func_i8_i8,
  func_f32_iPTR_iPTR,
  func_f64_iPTR_iPTR,
  i16_func_i16_i16,
  i32_func_f32_f32,
  i32_func_f64_f64,
  i32_func_i32_i32,
  i32_func_i32_i32_iPTR,
  i64_func_i64_i64,
  i64_func_i64_i64_iPTR,
  i64_i64_func_f32,
  i64_i64_func_f64,
  i16_i16_func_i16_i16,
  i32_i32_func_i32_i32,
  i64_i64_func_i64_i64,
  i64_i64_func_i64_i64_i64_i64,
  i64_i64_func_i64_i64_i64_i64_iPTR,
  i64_i64_i64_i64_func_i64_i64_i64_i64,
  i64_i64_func_i64_i64_i32,
  iPTR_func_iPTR_i32_iPTR,
  iPTR_func_iPTR_iPTR_iPTR,
  f32_func_f32_f32_f32,
  f64_func_f64_f64_f64,
  func_i64_i64_iPTR_iPTR,
  func_iPTR_f32,
  func_iPTR_f64,
  func_iPTR_i32,
  func_iPTR_i64,
  func_iPTR_i64_i64,
  func_iPTR_i64_i64_i64_i64,
  func_iPTR_i64_i64_i64_i64_i64_i64,
  i32_func_i64_i64,
  i32_func_i64_i64_i64_i64,
  unsupported
};

struct RuntimeLibcallSignatureTable {
  std::vector<RuntimeLibcallSignature> Table;

  // Any newly-added libcalls will be unsupported by default.
  RuntimeLibcallSignatureTable() : Table(RTLIB::UNKNOWN_LIBCALL, unsupported) {
    // Integer
    Table[RTLIB::SHL_I16] = i16_func_i16_i16;
    Table[RTLIB::SHL_I32] = i32_func_i32_i32;
    Table[RTLIB::SHL_I64] = i64_func_i64_i64;
    Table[RTLIB::SHL_I128] = i64_i64_func_i64_i64_i32;
    Table[RTLIB::SRL_I16] = i16_func_i16_i16;
    Table[RTLIB::SRL_I32] = i32_func_i32_i32;
    Table[RTLIB::SRL_I64] = i64_func_i64_i64;
    Table[RTLIB::SRL_I128] = i64_i64_func_i64_i64_i32;
    Table[RTLIB::SRA_I16] = i16_func_i16_i16;
    Table[RTLIB::SRA_I32] = i32_func_i32_i32;
    Table[RTLIB::SRA_I64] = i64_func_i64_i64;
    Table[RTLIB::SRA_I128] = i64_i64_func_i64_i64_i32;
    Table[RTLIB::MUL_I8] = i8_func_i8_i8;
    Table[RTLIB::MUL_I16] = i16_func_i16_i16;
    Table[RTLIB::MUL_I32] = i32_func_i32_i32;
    Table[RTLIB::MUL_I64] = i64_func_i64_i64;
    Table[RTLIB::MUL_I128] = i64_i64_func_i64_i64_i64_i64;
    Table[RTLIB::MULO_I32] = i32_func_i32_i32_iPTR;
    Table[RTLIB::MULO_I64] = i64_func_i64_i64_iPTR;
    Table[RTLIB::MULO_I128] = i64_i64_func_i64_i64_i64_i64_iPTR;
    Table[RTLIB::SDIV_I8] = i8_func_i8_i8;
    Table[RTLIB::SDIV_I16] = i16_func_i16_i16;
    Table[RTLIB::SDIV_I32] = i32_func_i32_i32;
    Table[RTLIB::SDIV_I64] = i64_func_i64_i64;
    Table[RTLIB::SDIV_I128] = i64_i64_func_i64_i64_i64_i64;
    Table[RTLIB::UDIV_I8] = i8_func_i8_i8;
    Table[RTLIB::UDIV_I16] = i16_func_i16_i16;
    Table[RTLIB::UDIV_I32] = i32_func_i32_i32;
    Table[RTLIB::UDIV_I64] = i64_func_i64_i64;
    Table[RTLIB::UDIV_I128] = i64_i64_func_i64_i64_i64_i64;
    Table[RTLIB::SREM_I8] = i8_func_i8_i8;
    Table[RTLIB::SREM_I16] = i16_func_i16_i16;
    Table[RTLIB::SREM_I32] = i32_func_i32_i32;
    Table[RTLIB::SREM_I64] = i64_func_i64_i64;
    Table[RTLIB::SREM_I128] = i64_i64_func_i64_i64_i64_i64;
    Table[RTLIB::UREM_I8] = i8_func_i8_i8;
    Table[RTLIB::UREM_I16] = i16_func_i16_i16;
    Table[RTLIB::UREM_I32] = i32_func_i32_i32;
    Table[RTLIB::UREM_I64] = i64_func_i64_i64;
    Table[RTLIB::UREM_I128] = i64_i64_func_i64_i64_i64_i64;
    Table[RTLIB::SDIVREM_I8] = i8_func_i8_i8;
    Table[RTLIB::SDIVREM_I16] = i16_i16_func_i16_i16;
    Table[RTLIB::SDIVREM_I32] = i32_i32_func_i32_i32;
    Table[RTLIB::SDIVREM_I64] = i64_func_i64_i64;
    Table[RTLIB::SDIVREM_I128] = i64_i64_i64_i64_func_i64_i64_i64_i64;
    Table[RTLIB::UDIVREM_I8] = i8_func_i8_i8;
    Table[RTLIB::UDIVREM_I16] = i16_i16_func_i16_i16;
    Table[RTLIB::UDIVREM_I32] = i32_i32_func_i32_i32;
    Table[RTLIB::UDIVREM_I64] = i64_i64_func_i64_i64;
    Table[RTLIB::UDIVREM_I128] = i64_i64_i64_i64_func_i64_i64_i64_i64;
    Table[RTLIB::NEG_I32] = i32_func_i32;
    Table[RTLIB::NEG_I64] = i64_func_i64;

    // Floating-point.
    // All F80 and PPCF128 routines are unsupported.
    Table[RTLIB::ADD_F32] = f32_func_f32_f32;
    Table[RTLIB::ADD_F64] = f64_func_f64_f64;
    Table[RTLIB::ADD_F128] = func_iPTR_i64_i64_i64_i64;
    Table[RTLIB::SUB_F32] = f32_func_f32_f32;
    Table[RTLIB::SUB_F64] = f64_func_f64_f64;
    Table[RTLIB::SUB_F128] = func_iPTR_i64_i64_i64_i64;
    Table[RTLIB::MUL_F32] = f32_func_f32_f32;
    Table[RTLIB::MUL_F64] = f64_func_f64_f64;
    Table[RTLIB::MUL_F128] = func_iPTR_i64_i64_i64_i64;
    Table[RTLIB::DIV_F32] = f32_func_f32_f32;
    Table[RTLIB::DIV_F64] = f64_func_f64_f64;
    Table[RTLIB::DIV_F128] = func_iPTR_i64_i64_i64_i64;
    Table[RTLIB::REM_F32] = f32_func_f32_f32;
    Table[RTLIB::REM_F64] = f64_func_f64_f64;
    Table[RTLIB::REM_F128] = func_iPTR_i64_i64_i64_i64;
    Table[RTLIB::FMA_F32] = f32_func_f32_f32_f32;
    Table[RTLIB::FMA_F64] = f64_func_f64_f64_f64;
    Table[RTLIB::FMA_F128] = func_iPTR_i64_i64_i64_i64_i64_i64;
    Table[RTLIB::POWI_F32] = f32_func_f32_i32;
    Table[RTLIB::POWI_F64] = f64_func_f64_i32;
    Table[RTLIB::POWI_F128] = func_iPTR_i64_i64_i64_i64;
    Table[RTLIB::SQRT_F32] = f32_func_f32;
    Table[RTLIB::SQRT_F64] = f64_func_f64;
    Table[RTLIB::SQRT_F128] = func_iPTR_i64_i64;
    Table[RTLIB::LOG_F32] = f32_func_f32;
    Table[RTLIB::LOG_F64] = f64_func_f64;
    Table[RTLIB::LOG_F128] = func_iPTR_i64_i64;
    Table[RTLIB::LOG2_F32] = f32_func_f32;
    Table[RTLIB::LOG2_F64] = f64_func_f64;
    Table[RTLIB::LOG2_F128] = func_iPTR_i64_i64;
    Table[RTLIB::LOG10_F32] = f32_func_f32;
    Table[RTLIB::LOG10_F64] = f64_func_f64;
    Table[RTLIB::LOG10_F128] = func_iPTR_i64_i64;
    Table[RTLIB::EXP_F32] = f32_func_f32;
    Table[RTLIB::EXP_F64] = f64_func_f64;
    Table[RTLIB::EXP_F128] = func_iPTR_i64_i64;
    Table[RTLIB::EXP2_F32] = f32_func_f32;
    Table[RTLIB::EXP2_F64] = f64_func_f64;
    Table[RTLIB::EXP2_F128] = func_iPTR_i64_i64;
    Table[RTLIB::SIN_F32] = f32_func_f32;
    Table[RTLIB::SIN_F64] = f64_func_f64;
    Table[RTLIB::SIN_F128] = func_iPTR_i64_i64;
    Table[RTLIB::COS_F32] = f32_func_f32;
    Table[RTLIB::COS_F64] = f64_func_f64;
    Table[RTLIB::COS_F128] = func_iPTR_i64_i64;
    Table[RTLIB::SINCOS_F32] = func_f32_iPTR_iPTR;
    Table[RTLIB::SINCOS_F64] = func_f64_iPTR_iPTR;
    Table[RTLIB::SINCOS_F128] = func_i64_i64_iPTR_iPTR;
    Table[RTLIB::POW_F32] = f32_func_f32_f32;
    Table[RTLIB::POW_F64] = f64_func_f64_f64;
    Table[RTLIB::POW_F128] = func_iPTR_i64_i64_i64_i64;
    Table[RTLIB::CEIL_F32] = f32_func_f32;
    Table[RTLIB::CEIL_F64] = f64_func_f64;
    Table[RTLIB::CEIL_F128] = func_iPTR_i64_i64;
    Table[RTLIB::TRUNC_F32] = f32_func_f32;
    Table[RTLIB::TRUNC_F64] = f64_func_f64;
    Table[RTLIB::TRUNC_F128] = func_iPTR_i64_i64;
    Table[RTLIB::RINT_F32] = f32_func_f32;
    Table[RTLIB::RINT_F64] = f64_func_f64;
    Table[RTLIB::RINT_F128] = func_iPTR_i64_i64;
    Table[RTLIB::NEARBYINT_F32] = f32_func_f32;
    Table[RTLIB::NEARBYINT_F64] = f64_func_f64;
    Table[RTLIB::NEARBYINT_F128] = func_iPTR_i64_i64;
    Table[RTLIB::ROUND_F32] = f32_func_f32;
    Table[RTLIB::ROUND_F64] = f64_func_f64;
    Table[RTLIB::ROUND_F128] = func_iPTR_i64_i64;
    Table[RTLIB::FLOOR_F32] = f32_func_f32;
    Table[RTLIB::FLOOR_F64] = f64_func_f64;
    Table[RTLIB::FLOOR_F128] = func_iPTR_i64_i64;
    Table[RTLIB::COPYSIGN_F32] = f32_func_f32_f32;
    Table[RTLIB::COPYSIGN_F64] = f64_func_f64_f64;
    Table[RTLIB::COPYSIGN_F128] = func_iPTR_i64_i64_i64_i64;
    Table[RTLIB::FMIN_F32] = f32_func_f32_f32;
    Table[RTLIB::FMIN_F64] = f64_func_f64_f64;
    Table[RTLIB::FMIN_F128] = func_iPTR_i64_i64_i64_i64;
    Table[RTLIB::FMAX_F32] = f32_func_f32_f32;
    Table[RTLIB::FMAX_F64] = f64_func_f64_f64;
    Table[RTLIB::FMAX_F128] = func_iPTR_i64_i64_i64_i64;

    // Conversion
    // All F80 and PPCF128 routines are unspported.
    Table[RTLIB::FPEXT_F64_F128] = func_iPTR_f64;
    Table[RTLIB::FPEXT_F32_F128] = func_iPTR_f32;
    Table[RTLIB::FPEXT_F32_F64] = f64_func_f32;
    Table[RTLIB::FPEXT_F16_F32] = f32_func_i16;
    Table[RTLIB::FPROUND_F32_F16] = i16_func_f32;
    Table[RTLIB::FPROUND_F64_F32] = f32_func_f64;
    Table[RTLIB::FPROUND_F128_F32] = f32_func_i64_i64;
    Table[RTLIB::FPROUND_F128_F64] = f64_func_i64_i64;
    Table[RTLIB::FPTOSINT_F32_I32] = i32_func_f32;
    Table[RTLIB::FPTOSINT_F32_I64] = i64_func_f32;
    Table[RTLIB::FPTOSINT_F32_I128] = i64_i64_func_f32;
    Table[RTLIB::FPTOSINT_F64_I32] = i32_func_f64;
    Table[RTLIB::FPTOSINT_F64_I64] = i64_func_f64;
    Table[RTLIB::FPTOSINT_F64_I128] = i64_i64_func_f64;
    Table[RTLIB::FPTOSINT_F128_I32] = i32_func_i64_i64;
    Table[RTLIB::FPTOSINT_F128_I64] = i64_func_i64_i64;
    Table[RTLIB::FPTOSINT_F128_I128] = i64_i64_func_i64_i64;
    Table[RTLIB::FPTOUINT_F32_I32] = i32_func_f32;
    Table[RTLIB::FPTOUINT_F32_I64] = i64_func_f32;
    Table[RTLIB::FPTOUINT_F32_I128] = i64_i64_func_f32;
    Table[RTLIB::FPTOUINT_F64_I32] = i32_func_f64;
    Table[RTLIB::FPTOUINT_F64_I64] = i64_func_f64;
    Table[RTLIB::FPTOUINT_F64_I128] = i64_i64_func_f64;
    Table[RTLIB::FPTOUINT_F128_I32] = i32_func_i64_i64;
    Table[RTLIB::FPTOUINT_F128_I64] = i64_func_i64_i64;
    Table[RTLIB::FPTOUINT_F128_I128] = i64_i64_func_i64_i64;
    Table[RTLIB::SINTTOFP_I32_F32] = f32_func_i32;
    Table[RTLIB::SINTTOFP_I32_F64] = f64_func_i32;
    Table[RTLIB::SINTTOFP_I32_F128] = func_iPTR_i32;
    Table[RTLIB::SINTTOFP_I64_F32] = f32_func_i64;
    Table[RTLIB::SINTTOFP_I64_F64] = f64_func_i64;
    Table[RTLIB::SINTTOFP_I64_F128] = func_iPTR_i64;
    Table[RTLIB::SINTTOFP_I128_F32] = f32_func_i64_i64;
    Table[RTLIB::SINTTOFP_I128_F64] = f64_func_i64_i64;
    Table[RTLIB::SINTTOFP_I128_F128] = func_iPTR_i64_i64;
    Table[RTLIB::UINTTOFP_I32_F32] = f32_func_i32;
    Table[RTLIB::UINTTOFP_I32_F64] = f64_func_i64;
    Table[RTLIB::UINTTOFP_I32_F128] = func_iPTR_i32;
    Table[RTLIB::UINTTOFP_I64_F32] = f32_func_i64;
    Table[RTLIB::UINTTOFP_I64_F64] = f64_func_i64;
    Table[RTLIB::UINTTOFP_I64_F128] = func_iPTR_i64;
    Table[RTLIB::UINTTOFP_I128_F32] = f32_func_i64_i64;
    Table[RTLIB::UINTTOFP_I128_F64] = f64_func_i64_i64;
    Table[RTLIB::UINTTOFP_I128_F128] = func_iPTR_i64_i64;

    // Comparison
    // ALl F80 and PPCF128 routines are unsupported.
    Table[RTLIB::OEQ_F32] = i32_func_f32_f32;
    Table[RTLIB::OEQ_F64] = i32_func_f64_f64;
    Table[RTLIB::OEQ_F128] = i32_func_i64_i64_i64_i64;
    Table[RTLIB::UNE_F32] = i32_func_f32_f32;
    Table[RTLIB::UNE_F64] = i32_func_f64_f64;
    Table[RTLIB::UNE_F128] = i32_func_i64_i64_i64_i64;
    Table[RTLIB::OGE_F32] = i32_func_f32_f32;
    Table[RTLIB::OGE_F64] = i32_func_f64_f64;
    Table[RTLIB::OGE_F128] = i32_func_i64_i64_i64_i64;
    Table[RTLIB::OLT_F32] = i32_func_f32_f32;
    Table[RTLIB::OLT_F64] = i32_func_f64_f64;
    Table[RTLIB::OLT_F128] = i32_func_i64_i64_i64_i64;
    Table[RTLIB::OLE_F32] = i32_func_f32_f32;
    Table[RTLIB::OLE_F64] = i32_func_f64_f64;
    Table[RTLIB::OLE_F128] = i32_func_i64_i64_i64_i64;
    Table[RTLIB::OGT_F32] = i32_func_f32_f32;
    Table[RTLIB::OGT_F64] = i32_func_f64_f64;
    Table[RTLIB::OGT_F128] = i32_func_i64_i64_i64_i64;
    Table[RTLIB::UO_F32] = i32_func_f32_f32;
    Table[RTLIB::UO_F64] = i32_func_f64_f64;
    Table[RTLIB::UO_F128] = i32_func_i64_i64_i64_i64;
    // O_FXX has the weird property that it uses the same libcall name as UO_FXX
    // This breaks our name-based lookup. Fortunately only the UO family of
    // libcalls appears to be actually used.
    Table[RTLIB::O_F32] = unsupported;
    Table[RTLIB::O_F64] = unsupported;
    Table[RTLIB::O_F128] = unsupported;

    // Memory
    Table[RTLIB::MEMCPY] = iPTR_func_iPTR_iPTR_iPTR;
    Table[RTLIB::MEMSET] = iPTR_func_iPTR_i32_iPTR;
    Table[RTLIB::MEMMOVE] = iPTR_func_iPTR_iPTR_iPTR;

    // Element-wise Atomic memory
    // TODO: Fix these when we implement atomic support
    Table[RTLIB::MEMCPY_ELEMENT_UNORDERED_ATOMIC_1] = unsupported;
    Table[RTLIB::MEMCPY_ELEMENT_UNORDERED_ATOMIC_2] = unsupported;
    Table[RTLIB::MEMCPY_ELEMENT_UNORDERED_ATOMIC_4] = unsupported;
    Table[RTLIB::MEMCPY_ELEMENT_UNORDERED_ATOMIC_8] = unsupported;
    Table[RTLIB::MEMCPY_ELEMENT_UNORDERED_ATOMIC_16] = unsupported;
    Table[RTLIB::MEMMOVE_ELEMENT_UNORDERED_ATOMIC_1] = unsupported;
    Table[RTLIB::MEMMOVE_ELEMENT_UNORDERED_ATOMIC_2] = unsupported;
    Table[RTLIB::MEMMOVE_ELEMENT_UNORDERED_ATOMIC_4] = unsupported;
    Table[RTLIB::MEMMOVE_ELEMENT_UNORDERED_ATOMIC_8] = unsupported;
    Table[RTLIB::MEMMOVE_ELEMENT_UNORDERED_ATOMIC_16] = unsupported;

    Table[RTLIB::MEMSET_ELEMENT_UNORDERED_ATOMIC_1] = unsupported;
    Table[RTLIB::MEMSET_ELEMENT_UNORDERED_ATOMIC_2] = unsupported;
    Table[RTLIB::MEMSET_ELEMENT_UNORDERED_ATOMIC_4] = unsupported;
    Table[RTLIB::MEMSET_ELEMENT_UNORDERED_ATOMIC_8] = unsupported;
    Table[RTLIB::MEMSET_ELEMENT_UNORDERED_ATOMIC_16] = unsupported;

    // Atomic '__sync_*' libcalls.
    // TODO: Fix these when we implement atomic support
    Table[RTLIB::SYNC_VAL_COMPARE_AND_SWAP_1] = unsupported;
    Table[RTLIB::SYNC_VAL_COMPARE_AND_SWAP_2] = unsupported;
    Table[RTLIB::SYNC_VAL_COMPARE_AND_SWAP_4] = unsupported;
    Table[RTLIB::SYNC_VAL_COMPARE_AND_SWAP_8] = unsupported;
    Table[RTLIB::SYNC_VAL_COMPARE_AND_SWAP_16] = unsupported;
    Table[RTLIB::SYNC_LOCK_TEST_AND_SET_1] = unsupported;
    Table[RTLIB::SYNC_LOCK_TEST_AND_SET_2] = unsupported;
    Table[RTLIB::SYNC_LOCK_TEST_AND_SET_4] = unsupported;
    Table[RTLIB::SYNC_LOCK_TEST_AND_SET_8] = unsupported;
    Table[RTLIB::SYNC_LOCK_TEST_AND_SET_16] = unsupported;
    Table[RTLIB::SYNC_FETCH_AND_ADD_1] = unsupported;
    Table[RTLIB::SYNC_FETCH_AND_ADD_2] = unsupported;
    Table[RTLIB::SYNC_FETCH_AND_ADD_4] = unsupported;
    Table[RTLIB::SYNC_FETCH_AND_ADD_8] = unsupported;
    Table[RTLIB::SYNC_FETCH_AND_ADD_16] = unsupported;
    Table[RTLIB::SYNC_FETCH_AND_SUB_1] = unsupported;
    Table[RTLIB::SYNC_FETCH_AND_SUB_2] = unsupported;
    Table[RTLIB::SYNC_FETCH_AND_SUB_4] = unsupported;
    Table[RTLIB::SYNC_FETCH_AND_SUB_8] = unsupported;
    Table[RTLIB::SYNC_FETCH_AND_SUB_16] = unsupported;
    Table[RTLIB::SYNC_FETCH_AND_AND_1] = unsupported;
    Table[RTLIB::SYNC_FETCH_AND_AND_2] = unsupported;
    Table[RTLIB::SYNC_FETCH_AND_AND_4] = unsupported;
    Table[RTLIB::SYNC_FETCH_AND_AND_8] = unsupported;
    Table[RTLIB::SYNC_FETCH_AND_AND_16] = unsupported;
    Table[RTLIB::SYNC_FETCH_AND_OR_1] = unsupported;
    Table[RTLIB::SYNC_FETCH_AND_OR_2] = unsupported;
    Table[RTLIB::SYNC_FETCH_AND_OR_4] = unsupported;
    Table[RTLIB::SYNC_FETCH_AND_OR_8] = unsupported;
    Table[RTLIB::SYNC_FETCH_AND_OR_16] = unsupported;
    Table[RTLIB::SYNC_FETCH_AND_XOR_1] = unsupported;
    Table[RTLIB::SYNC_FETCH_AND_XOR_2] = unsupported;
    Table[RTLIB::SYNC_FETCH_AND_XOR_4] = unsupported;
    Table[RTLIB::SYNC_FETCH_AND_XOR_8] = unsupported;
    Table[RTLIB::SYNC_FETCH_AND_XOR_16] = unsupported;
    Table[RTLIB::SYNC_FETCH_AND_NAND_1] = unsupported;
    Table[RTLIB::SYNC_FETCH_AND_NAND_2] = unsupported;
    Table[RTLIB::SYNC_FETCH_AND_NAND_4] = unsupported;
    Table[RTLIB::SYNC_FETCH_AND_NAND_8] = unsupported;
    Table[RTLIB::SYNC_FETCH_AND_NAND_16] = unsupported;
    Table[RTLIB::SYNC_FETCH_AND_MAX_1] = unsupported;
    Table[RTLIB::SYNC_FETCH_AND_MAX_2] = unsupported;
    Table[RTLIB::SYNC_FETCH_AND_MAX_4] = unsupported;
    Table[RTLIB::SYNC_FETCH_AND_MAX_8] = unsupported;
    Table[RTLIB::SYNC_FETCH_AND_MAX_16] = unsupported;
    Table[RTLIB::SYNC_FETCH_AND_UMAX_1] = unsupported;
    Table[RTLIB::SYNC_FETCH_AND_UMAX_2] = unsupported;
    Table[RTLIB::SYNC_FETCH_AND_UMAX_4] = unsupported;
    Table[RTLIB::SYNC_FETCH_AND_UMAX_8] = unsupported;
    Table[RTLIB::SYNC_FETCH_AND_UMAX_16] = unsupported;
    Table[RTLIB::SYNC_FETCH_AND_MIN_1] = unsupported;
    Table[RTLIB::SYNC_FETCH_AND_MIN_2] = unsupported;
    Table[RTLIB::SYNC_FETCH_AND_MIN_4] = unsupported;
    Table[RTLIB::SYNC_FETCH_AND_MIN_8] = unsupported;
    Table[RTLIB::SYNC_FETCH_AND_MIN_16] = unsupported;
    Table[RTLIB::SYNC_FETCH_AND_UMIN_1] = unsupported;
    Table[RTLIB::SYNC_FETCH_AND_UMIN_2] = unsupported;
    Table[RTLIB::SYNC_FETCH_AND_UMIN_4] = unsupported;
    Table[RTLIB::SYNC_FETCH_AND_UMIN_8] = unsupported;
    Table[RTLIB::SYNC_FETCH_AND_UMIN_16] = unsupported;

    // Atomic '__atomic_*' libcalls.
    // TODO: Fix these when we implement atomic support
    Table[RTLIB::ATOMIC_LOAD] = unsupported;
    Table[RTLIB::ATOMIC_LOAD_1] = unsupported;
    Table[RTLIB::ATOMIC_LOAD_2] = unsupported;
    Table[RTLIB::ATOMIC_LOAD_4] = unsupported;
    Table[RTLIB::ATOMIC_LOAD_8] = unsupported;
    Table[RTLIB::ATOMIC_LOAD_16] = unsupported;

    Table[RTLIB::ATOMIC_STORE] = unsupported;
    Table[RTLIB::ATOMIC_STORE_1] = unsupported;
    Table[RTLIB::ATOMIC_STORE_2] = unsupported;
    Table[RTLIB::ATOMIC_STORE_4] = unsupported;
    Table[RTLIB::ATOMIC_STORE_8] = unsupported;
    Table[RTLIB::ATOMIC_STORE_16] = unsupported;

    Table[RTLIB::ATOMIC_EXCHANGE] = unsupported;
    Table[RTLIB::ATOMIC_EXCHANGE_1] = unsupported;
    Table[RTLIB::ATOMIC_EXCHANGE_2] = unsupported;
    Table[RTLIB::ATOMIC_EXCHANGE_4] = unsupported;
    Table[RTLIB::ATOMIC_EXCHANGE_8] = unsupported;
    Table[RTLIB::ATOMIC_EXCHANGE_16] = unsupported;

    Table[RTLIB::ATOMIC_COMPARE_EXCHANGE] = unsupported;
    Table[RTLIB::ATOMIC_COMPARE_EXCHANGE_1] = unsupported;
    Table[RTLIB::ATOMIC_COMPARE_EXCHANGE_2] = unsupported;
    Table[RTLIB::ATOMIC_COMPARE_EXCHANGE_4] = unsupported;
    Table[RTLIB::ATOMIC_COMPARE_EXCHANGE_8] = unsupported;
    Table[RTLIB::ATOMIC_COMPARE_EXCHANGE_16] = unsupported;

    Table[RTLIB::ATOMIC_FETCH_ADD_1] = unsupported;
    Table[RTLIB::ATOMIC_FETCH_ADD_2] = unsupported;
    Table[RTLIB::ATOMIC_FETCH_ADD_4] = unsupported;
    Table[RTLIB::ATOMIC_FETCH_ADD_8] = unsupported;
    Table[RTLIB::ATOMIC_FETCH_ADD_16] = unsupported;

    Table[RTLIB::ATOMIC_FETCH_SUB_1] = unsupported;
    Table[RTLIB::ATOMIC_FETCH_SUB_2] = unsupported;
    Table[RTLIB::ATOMIC_FETCH_SUB_4] = unsupported;
    Table[RTLIB::ATOMIC_FETCH_SUB_8] = unsupported;
    Table[RTLIB::ATOMIC_FETCH_SUB_16] = unsupported;

    Table[RTLIB::ATOMIC_FETCH_AND_1] = unsupported;
    Table[RTLIB::ATOMIC_FETCH_AND_2] = unsupported;
    Table[RTLIB::ATOMIC_FETCH_AND_4] = unsupported;
    Table[RTLIB::ATOMIC_FETCH_AND_8] = unsupported;
    Table[RTLIB::ATOMIC_FETCH_AND_16] = unsupported;

    Table[RTLIB::ATOMIC_FETCH_OR_1] = unsupported;
    Table[RTLIB::ATOMIC_FETCH_OR_2] = unsupported;
    Table[RTLIB::ATOMIC_FETCH_OR_4] = unsupported;
    Table[RTLIB::ATOMIC_FETCH_OR_8] = unsupported;
    Table[RTLIB::ATOMIC_FETCH_OR_16] = unsupported;

    Table[RTLIB::ATOMIC_FETCH_XOR_1] = unsupported;
    Table[RTLIB::ATOMIC_FETCH_XOR_2] = unsupported;
    Table[RTLIB::ATOMIC_FETCH_XOR_4] = unsupported;
    Table[RTLIB::ATOMIC_FETCH_XOR_8] = unsupported;
    Table[RTLIB::ATOMIC_FETCH_XOR_16] = unsupported;

    Table[RTLIB::ATOMIC_FETCH_NAND_1] = unsupported;
    Table[RTLIB::ATOMIC_FETCH_NAND_2] = unsupported;
    Table[RTLIB::ATOMIC_FETCH_NAND_4] = unsupported;
    Table[RTLIB::ATOMIC_FETCH_NAND_8] = unsupported;
    Table[RTLIB::ATOMIC_FETCH_NAND_16] = unsupported;
  }
};

ManagedStatic<RuntimeLibcallSignatureTable> RuntimeLibcallSignatures;

// Maps libcall names to their RTLIB::Libcall number. Builds the map in a
// constructor for use with ManagedStatic
struct StaticLibcallNameMap {
  StringMap<RTLIB::Libcall> Map;
  StaticLibcallNameMap() {
    static const std::pair<const char *, RTLIB::Libcall> NameLibcalls[] = {
#define HANDLE_LIBCALL(code, name) {(const char *)name, RTLIB::code},
#include "llvm/IR/RuntimeLibcalls.def"
#undef HANDLE_LIBCALL
    };
    for (const auto &NameLibcall : NameLibcalls) {
      if (NameLibcall.first != nullptr &&
          RuntimeLibcallSignatures->Table[NameLibcall.second] != unsupported) {
        assert(Map.find(NameLibcall.first) == Map.end() &&
               "duplicate libcall names in name map");
        Map[NameLibcall.first] = NameLibcall.second;
      }
    }
  }
};

} // end anonymous namespace

void llvm::GetLibcallSignature(const WebAssemblySubtarget &Subtarget,
                               RTLIB::Libcall LC,
                               SmallVectorImpl<wasm::ValType> &Rets,
                               SmallVectorImpl<wasm::ValType> &Params) {
  assert(Rets.empty());
  assert(Params.empty());

  wasm::ValType iPTR =
      Subtarget.hasAddr64() ? wasm::ValType::I64 : wasm::ValType::I32;

  auto &Table = RuntimeLibcallSignatures->Table;
  switch (Table[LC]) {
  case func:
    break;
  case f32_func_f32:
    Rets.push_back(wasm::ValType::F32);
    Params.push_back(wasm::ValType::F32);
    break;
  case f32_func_f64:
    Rets.push_back(wasm::ValType::F32);
    Params.push_back(wasm::ValType::F64);
    break;
  case f32_func_i32:
    Rets.push_back(wasm::ValType::F32);
    Params.push_back(wasm::ValType::I32);
    break;
  case f32_func_i64:
    Rets.push_back(wasm::ValType::F32);
    Params.push_back(wasm::ValType::I64);
    break;
  case f32_func_i16:
    Rets.push_back(wasm::ValType::F32);
    Params.push_back(wasm::ValType::I32);
    break;
  case f64_func_f32:
    Rets.push_back(wasm::ValType::F64);
    Params.push_back(wasm::ValType::F32);
    break;
  case f64_func_f64:
    Rets.push_back(wasm::ValType::F64);
    Params.push_back(wasm::ValType::F64);
    break;
  case f64_func_i32:
    Rets.push_back(wasm::ValType::F64);
    Params.push_back(wasm::ValType::I32);
    break;
  case f64_func_i64:
    Rets.push_back(wasm::ValType::F64);
    Params.push_back(wasm::ValType::I64);
    break;
  case i32_func_f32:
    Rets.push_back(wasm::ValType::I32);
    Params.push_back(wasm::ValType::F32);
    break;
  case i32_func_f64:
    Rets.push_back(wasm::ValType::I32);
    Params.push_back(wasm::ValType::F64);
    break;
  case i32_func_i32:
    Rets.push_back(wasm::ValType::I32);
    Params.push_back(wasm::ValType::I32);
    break;
  case i64_func_f32:
    Rets.push_back(wasm::ValType::I64);
    Params.push_back(wasm::ValType::F32);
    break;
  case i64_func_f64:
    Rets.push_back(wasm::ValType::I64);
    Params.push_back(wasm::ValType::F64);
    break;
  case i64_func_i64:
    Rets.push_back(wasm::ValType::I64);
    Params.push_back(wasm::ValType::I64);
    break;
  case f32_func_f32_f32:
    Rets.push_back(wasm::ValType::F32);
    Params.push_back(wasm::ValType::F32);
    Params.push_back(wasm::ValType::F32);
    break;
  case f32_func_f32_i32:
    Rets.push_back(wasm::ValType::F32);
    Params.push_back(wasm::ValType::F32);
    Params.push_back(wasm::ValType::I32);
    break;
  case f32_func_i64_i64:
    Rets.push_back(wasm::ValType::F32);
    Params.push_back(wasm::ValType::I64);
    Params.push_back(wasm::ValType::I64);
    break;
  case f64_func_f64_f64:
    Rets.push_back(wasm::ValType::F64);
    Params.push_back(wasm::ValType::F64);
    Params.push_back(wasm::ValType::F64);
    break;
  case f64_func_f64_i32:
    Rets.push_back(wasm::ValType::F64);
    Params.push_back(wasm::ValType::F64);
    Params.push_back(wasm::ValType::I32);
    break;
  case f64_func_i64_i64:
    Rets.push_back(wasm::ValType::F64);
    Params.push_back(wasm::ValType::I64);
    Params.push_back(wasm::ValType::I64);
    break;
  case i16_func_f32:
    Rets.push_back(wasm::ValType::I32);
    Params.push_back(wasm::ValType::F32);
    break;
  case i8_func_i8_i8:
    Rets.push_back(wasm::ValType::I32);
    Params.push_back(wasm::ValType::I32);
    Params.push_back(wasm::ValType::I32);
    break;
  case func_f32_iPTR_iPTR:
    Params.push_back(wasm::ValType::F32);
    Params.push_back(iPTR);
    Params.push_back(iPTR);
    break;
  case func_f64_iPTR_iPTR:
    Params.push_back(wasm::ValType::F64);
    Params.push_back(iPTR);
    Params.push_back(iPTR);
    break;
  case i16_func_i16_i16:
    Rets.push_back(wasm::ValType::I32);
    Params.push_back(wasm::ValType::I32);
    Params.push_back(wasm::ValType::I32);
    break;
  case i32_func_f32_f32:
    Rets.push_back(wasm::ValType::I32);
    Params.push_back(wasm::ValType::F32);
    Params.push_back(wasm::ValType::F32);
    break;
  case i32_func_f64_f64:
    Rets.push_back(wasm::ValType::I32);
    Params.push_back(wasm::ValType::F64);
    Params.push_back(wasm::ValType::F64);
    break;
  case i32_func_i32_i32:
    Rets.push_back(wasm::ValType::I32);
    Params.push_back(wasm::ValType::I32);
    Params.push_back(wasm::ValType::I32);
    break;
  case i32_func_i32_i32_iPTR:
    Rets.push_back(wasm::ValType::I32);
    Params.push_back(wasm::ValType::I32);
    Params.push_back(wasm::ValType::I32);
    Params.push_back(iPTR);
    break;
  case i64_func_i64_i64:
    Rets.push_back(wasm::ValType::I64);
    Params.push_back(wasm::ValType::I64);
    Params.push_back(wasm::ValType::I64);
    break;
  case i64_func_i64_i64_iPTR:
    Rets.push_back(wasm::ValType::I64);
    Params.push_back(wasm::ValType::I64);
    Params.push_back(wasm::ValType::I64);
    Params.push_back(iPTR);
    break;
  case i64_i64_func_f32:
#if 0 // TODO: Enable this when wasm gets multiple-return-value support.
    Rets.push_back(wasm::ValType::I64);
    Rets.push_back(wasm::ValType::I64);
#else
    Params.push_back(iPTR);
#endif
    Params.push_back(wasm::ValType::F32);
    break;
  case i64_i64_func_f64:
#if 0 // TODO: Enable this when wasm gets multiple-return-value support.
    Rets.push_back(wasm::ValType::I64);
    Rets.push_back(wasm::ValType::I64);
#else
    Params.push_back(iPTR);
#endif
    Params.push_back(wasm::ValType::F64);
    break;
  case i16_i16_func_i16_i16:
#if 0 // TODO: Enable this when wasm gets multiple-return-value support.
    Rets.push_back(wasm::ValType::I32);
    Rets.push_back(wasm::ValType::I32);
#else
    Params.push_back(iPTR);
#endif
    Params.push_back(wasm::ValType::I32);
    Params.push_back(wasm::ValType::I32);
    break;
  case i32_i32_func_i32_i32:
#if 0 // TODO: Enable this when wasm gets multiple-return-value support.
    Rets.push_back(wasm::ValType::I32);
    Rets.push_back(wasm::ValType::I32);
#else
    Params.push_back(iPTR);
#endif
    Params.push_back(wasm::ValType::I32);
    Params.push_back(wasm::ValType::I32);
    break;
  case i64_i64_func_i64_i64:
#if 0 // TODO: Enable this when wasm gets multiple-return-value support.
    Rets.push_back(wasm::ValType::I64);
    Rets.push_back(wasm::ValType::I64);
#else
    Params.push_back(iPTR);
#endif
    Params.push_back(wasm::ValType::I64);
    Params.push_back(wasm::ValType::I64);
    break;
  case i64_i64_func_i64_i64_i64_i64:
#if 0 // TODO: Enable this when wasm gets multiple-return-value support.
    Rets.push_back(wasm::ValType::I64);
    Rets.push_back(wasm::ValType::I64);
#else
    Params.push_back(iPTR);
#endif
    Params.push_back(wasm::ValType::I64);
    Params.push_back(wasm::ValType::I64);
    Params.push_back(wasm::ValType::I64);
    Params.push_back(wasm::ValType::I64);
    break;
  case i64_i64_func_i64_i64_i64_i64_iPTR:
#if 0 // TODO: Enable this when wasm gets multiple-return-value support.
    Rets.push_back(wasm::ValType::I64);
    Rets.push_back(wasm::ValType::I64);
#else
    Params.push_back(iPTR);
#endif
    Params.push_back(wasm::ValType::I64);
    Params.push_back(wasm::ValType::I64);
    Params.push_back(wasm::ValType::I64);
    Params.push_back(wasm::ValType::I64);
    Params.push_back(iPTR);
    break;
  case i64_i64_i64_i64_func_i64_i64_i64_i64:
#if 0 // TODO: Enable this when wasm gets multiple-return-value support.
    Rets.push_back(wasm::ValType::I64);
    Rets.push_back(wasm::ValType::I64);
    Rets.push_back(wasm::ValType::I64);
    Rets.push_back(wasm::ValType::I64);
#else
    Params.push_back(iPTR);
#endif
    Params.push_back(wasm::ValType::I64);
    Params.push_back(wasm::ValType::I64);
    Params.push_back(wasm::ValType::I64);
    Params.push_back(wasm::ValType::I64);
    break;
  case i64_i64_func_i64_i64_i32:
#if 0 // TODO: Enable this when wasm gets multiple-return-value support.
    Rets.push_back(wasm::ValType::I64);
    Rets.push_back(wasm::ValType::I64);
    Rets.push_back(wasm::ValType::I64);
    Rets.push_back(wasm::ValType::I64);
#else
    Params.push_back(iPTR);
#endif
    Params.push_back(wasm::ValType::I64);
    Params.push_back(wasm::ValType::I64);
    Params.push_back(wasm::ValType::I32);
    break;
  case iPTR_func_iPTR_i32_iPTR:
    Rets.push_back(iPTR);
    Params.push_back(iPTR);
    Params.push_back(wasm::ValType::I32);
    Params.push_back(iPTR);
    break;
  case iPTR_func_iPTR_iPTR_iPTR:
    Rets.push_back(iPTR);
    Params.push_back(iPTR);
    Params.push_back(iPTR);
    Params.push_back(iPTR);
    break;
  case f32_func_f32_f32_f32:
    Rets.push_back(wasm::ValType::F32);
    Params.push_back(wasm::ValType::F32);
    Params.push_back(wasm::ValType::F32);
    Params.push_back(wasm::ValType::F32);
    break;
  case f64_func_f64_f64_f64:
    Rets.push_back(wasm::ValType::F64);
    Params.push_back(wasm::ValType::F64);
    Params.push_back(wasm::ValType::F64);
    Params.push_back(wasm::ValType::F64);
    break;
  case func_i64_i64_iPTR_iPTR:
    Params.push_back(wasm::ValType::I64);
    Params.push_back(wasm::ValType::I64);
    Params.push_back(iPTR);
    Params.push_back(iPTR);
    break;
  case func_iPTR_f32:
    Params.push_back(iPTR);
    Params.push_back(wasm::ValType::F32);
    break;
  case func_iPTR_f64:
    Params.push_back(iPTR);
    Params.push_back(wasm::ValType::F64);
    break;
  case func_iPTR_i32:
    Params.push_back(iPTR);
    Params.push_back(wasm::ValType::I32);
    break;
  case func_iPTR_i64:
    Params.push_back(iPTR);
    Params.push_back(wasm::ValType::I64);
    break;
  case func_iPTR_i64_i64:
    Params.push_back(iPTR);
    Params.push_back(wasm::ValType::I64);
    Params.push_back(wasm::ValType::I64);
    break;
  case func_iPTR_i64_i64_i64_i64:
    Params.push_back(iPTR);
    Params.push_back(wasm::ValType::I64);
    Params.push_back(wasm::ValType::I64);
    Params.push_back(wasm::ValType::I64);
    Params.push_back(wasm::ValType::I64);
    break;
  case func_iPTR_i64_i64_i64_i64_i64_i64:
    Params.push_back(iPTR);
    Params.push_back(wasm::ValType::I64);
    Params.push_back(wasm::ValType::I64);
    Params.push_back(wasm::ValType::I64);
    Params.push_back(wasm::ValType::I64);
    Params.push_back(wasm::ValType::I64);
    Params.push_back(wasm::ValType::I64);
    break;
  case i32_func_i64_i64:
    Rets.push_back(wasm::ValType::I32);
    Params.push_back(wasm::ValType::I64);
    Params.push_back(wasm::ValType::I64);
    break;
  case i32_func_i64_i64_i64_i64:
    Rets.push_back(wasm::ValType::I32);
    Params.push_back(wasm::ValType::I64);
    Params.push_back(wasm::ValType::I64);
    Params.push_back(wasm::ValType::I64);
    Params.push_back(wasm::ValType::I64);
    break;
  case unsupported:
    llvm_unreachable("unsupported runtime library signature");
  }
}

static ManagedStatic<StaticLibcallNameMap> LibcallNameMap;
// TODO: If the RTLIB::Libcall-taking flavor of GetSignature remains unsed
// other than here, just roll its logic into this version.
void llvm::GetLibcallSignature(const WebAssemblySubtarget &Subtarget,
                               const char *Name,
                               SmallVectorImpl<wasm::ValType> &Rets,
                               SmallVectorImpl<wasm::ValType> &Params) {
  auto &Map = LibcallNameMap->Map;
  auto val = Map.find(Name);
  assert(val != Map.end() && "unexpected runtime library name");
  return GetLibcallSignature(Subtarget, val->second, Rets, Params);
}
