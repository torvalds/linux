//===-- X86AsmParserCommon.h - Common functions for X86AsmParser ---------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_X86_ASMPARSER_X86ASMPARSERCOMMON_H
#define LLVM_LIB_TARGET_X86_ASMPARSER_X86ASMPARSERCOMMON_H

#include "llvm/Support/MathExtras.h"

namespace llvm {

inline bool isImmSExti16i8Value(uint64_t Value) {
  return isInt<8>(Value) ||
         (isUInt<16>(Value) && isInt<8>(static_cast<int16_t>(Value)));
}

inline bool isImmSExti32i8Value(uint64_t Value) {
  return isInt<8>(Value) ||
         (isUInt<32>(Value) && isInt<8>(static_cast<int32_t>(Value)));
}

inline bool isImmSExti64i8Value(uint64_t Value) {
  return isInt<8>(Value);
}

inline bool isImmSExti64i32Value(uint64_t Value) {
  return isInt<32>(Value);
}

inline bool isImmUnsignedi8Value(uint64_t Value) {
  return isUInt<8>(Value) || isInt<8>(Value);
}

} // End of namespace llvm

#endif
