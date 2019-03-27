//===-- sanitizer_coverage_win_weak_interception.cc -----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// This module should be included in Sanitizer Coverage when it implemented as a
// shared library on Windows (dll), in order to delegate the calls of weak
// functions to the implementation in the main executable when a strong
// definition is provided.
//===----------------------------------------------------------------------===//
#ifdef SANITIZER_DYNAMIC
#include "sanitizer_win_weak_interception.h"
#include "sanitizer_interface_internal.h"
#include "sancov_flags.h"
// Check if strong definitions for weak functions are present in the main
// executable. If that is the case, override dll functions to point to strong
// implementations.
#define INTERFACE_FUNCTION(Name)
#define INTERFACE_WEAK_FUNCTION(Name) INTERCEPT_SANITIZER_WEAK_FUNCTION(Name)
#include "sanitizer_coverage_interface.inc"
#endif // SANITIZER_DYNAMIC
