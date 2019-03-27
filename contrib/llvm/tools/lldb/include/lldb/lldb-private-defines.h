//===-- lldb-private-defines.h ----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_lldb_private_defines_h_
#define liblldb_lldb_private_defines_h_

#if defined(__cplusplus)

// Include Compiler.h here so we don't define LLVM_FALLTHROUGH and then
// Compiler.h later tries to redefine it.
#include "llvm/Support/Compiler.h"

#ifndef LLVM_FALLTHROUGH

#ifndef __has_cpp_attribute
#define __has_cpp_attribute(x) 0
#endif

/// \macro LLVM_FALLTHROUGH
/// Marks an empty statement preceding a deliberate switch fallthrough.
#if __has_cpp_attribute(clang::fallthrough)
#define LLVM_FALLTHROUGH [[clang::fallthrough]]
#else
#define LLVM_FALLTHROUGH
#endif

#endif // ifndef LLVM_FALLTHROUGH

#endif // #if defined(__cplusplus)

#endif // liblldb_lldb_private_defines_h_
