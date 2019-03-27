//===-- AMDGPUAsmUtils.h - AsmParser/InstPrinter common ---------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPU_UTILS_AMDGPUASMUTILS_H
#define LLVM_LIB_TARGET_AMDGPU_UTILS_AMDGPUASMUTILS_H

namespace llvm {
namespace AMDGPU {
namespace SendMsg { // Symbolic names for the sendmsg(...) syntax.

extern const char* const IdSymbolic[];
extern const char* const OpSysSymbolic[];
extern const char* const OpGsSymbolic[];

} // namespace SendMsg

namespace Hwreg { // Symbolic names for the hwreg(...) syntax.

extern const char* const IdSymbolic[];

} // namespace Hwreg

namespace Swizzle { // Symbolic names for the swizzle(...) syntax.

extern const char* const IdSymbolic[];

} // namespace Swizzle
} // namespace AMDGPU
} // namespace llvm

#endif
