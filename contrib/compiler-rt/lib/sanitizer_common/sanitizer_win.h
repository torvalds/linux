//===-- sanitizer_win.h -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Windows-specific declarations.
//
//===----------------------------------------------------------------------===//
#ifndef SANITIZER_WIN_H
#define SANITIZER_WIN_H

#include "sanitizer_platform.h"
#if SANITIZER_WINDOWS
#include "sanitizer_internal_defs.h"

namespace __sanitizer {
// Check based on flags if we should handle the exception.
bool IsHandledDeadlyException(DWORD exceptionCode);
}  // namespace __sanitizer

#endif  // SANITIZER_WINDOWS
#endif  // SANITIZER_WIN_H
