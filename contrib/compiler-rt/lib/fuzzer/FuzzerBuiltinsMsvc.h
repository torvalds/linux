//===- FuzzerBuiltinsMSVC.h - Internal header for builtins ------*- C++ -* ===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// Wrapper functions and marcos that use intrinsics instead of builtin functions
// which cannot be compiled by MSVC.
//===----------------------------------------------------------------------===//

#ifndef LLVM_FUZZER_BUILTINS_MSVC_H
#define LLVM_FUZZER_BUILTINS_MSVC_H

#include "FuzzerDefs.h"

#if LIBFUZZER_MSVC
#if !defined(_M_ARM) && !defined(_M_X64)
#error "_BitScanReverse64 unavailable on this platform so MSVC is unsupported."
#endif
#include <intrin.h>
#include <cstdint>
#include <cstdlib>

// __builtin_return_address() cannot be compiled with MSVC. Use the equivalent
// from <intrin.h>
#define GET_CALLER_PC() reinterpret_cast<uintptr_t>(_ReturnAddress())

namespace fuzzer {

inline uint8_t  Bswap(uint8_t x)  { return x; }
// Use alternatives to __builtin functions from <stdlib.h> and <intrin.h> on
// Windows since the builtins are not supported by MSVC.
inline uint16_t Bswap(uint16_t x) { return _byteswap_ushort(x); }
inline uint32_t Bswap(uint32_t x) { return _byteswap_ulong(x); }
inline uint64_t Bswap(uint64_t x) { return _byteswap_uint64(x); }

// The functions below were mostly copied from
// compiler-rt/lib/builtins/int_lib.h which defines the __builtin functions used
// outside of Windows.
inline uint32_t Clzll(uint64_t X) {
  unsigned long LeadZeroIdx = 0;
  if (_BitScanReverse64(&LeadZeroIdx, X)) return 63 - LeadZeroIdx;
  return 64;
}

inline uint32_t Clz(uint32_t X) {
  unsigned long LeadZeroIdx = 0;
  if (_BitScanReverse(&LeadZeroIdx, X)) return 31 - LeadZeroIdx;
  return 32;
}

inline int Popcountll(unsigned long long X) { return __popcnt64(X); }

}  // namespace fuzzer

#endif  // LIBFUZER_MSVC
#endif  // LLVM_FUZZER_BUILTINS_MSVC_H
