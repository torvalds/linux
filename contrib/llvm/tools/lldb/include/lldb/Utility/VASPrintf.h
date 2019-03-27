//===-- VASPrintf.h ---------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_UTILITY_VASPRINTF_H
#define LLDB_UTILITY_VASPRINTF_H

#include "llvm/ADT/SmallVector.h"

#include <cstdarg>

namespace lldb_private {
bool VASprintf(llvm::SmallVectorImpl<char> &buf, const char *fmt, va_list args);
}

#endif // #ifdef LLDB_UTILITY_VASPRINTF_H
