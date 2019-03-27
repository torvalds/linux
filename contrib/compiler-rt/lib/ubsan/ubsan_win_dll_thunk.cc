//===-- ubsan_win_dll_thunk.cc --------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines a family of thunks that should be statically linked into
// the DLLs that have instrumentation in order to delegate the calls to the
// shared runtime that lives in the main binary.
// See https://github.com/google/sanitizers/issues/209 for the details.
//===----------------------------------------------------------------------===//
#ifdef SANITIZER_DLL_THUNK
#include "sanitizer_common/sanitizer_win_dll_thunk.h"
// Ubsan interface functions.
#define INTERFACE_FUNCTION(Name) INTERCEPT_SANITIZER_FUNCTION(Name)
#define INTERFACE_WEAK_FUNCTION(Name) INTERCEPT_SANITIZER_WEAK_FUNCTION(Name)
#include "ubsan_interface.inc"
#endif // SANITIZER_DLL_THUNK
