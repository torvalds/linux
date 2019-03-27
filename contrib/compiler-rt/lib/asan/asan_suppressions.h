//===-- asan_suppressions.h -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
// ASan-private header for asan_suppressions.cc.
//===----------------------------------------------------------------------===//
#ifndef ASAN_SUPPRESSIONS_H
#define ASAN_SUPPRESSIONS_H

#include "asan_internal.h"
#include "sanitizer_common/sanitizer_stacktrace.h"

namespace __asan {

void InitializeSuppressions();
bool IsInterceptorSuppressed(const char *interceptor_name);
bool HaveStackTraceBasedSuppressions();
bool IsStackTraceSuppressed(const StackTrace *stack);
bool IsODRViolationSuppressed(const char *global_var_name);

} // namespace __asan

#endif // ASAN_SUPPRESSIONS_H
