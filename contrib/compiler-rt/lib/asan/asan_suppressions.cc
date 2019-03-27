//===-- asan_suppressions.cc ----------------------------------------------===//
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
// Issue suppression and suppression-related functions.
//===----------------------------------------------------------------------===//

#include "asan_suppressions.h"

#include "asan_stack.h"
#include "sanitizer_common/sanitizer_placement_new.h"
#include "sanitizer_common/sanitizer_suppressions.h"
#include "sanitizer_common/sanitizer_symbolizer.h"

namespace __asan {

ALIGNED(64) static char suppression_placeholder[sizeof(SuppressionContext)];
static SuppressionContext *suppression_ctx = nullptr;
static const char kInterceptorName[] = "interceptor_name";
static const char kInterceptorViaFunction[] = "interceptor_via_fun";
static const char kInterceptorViaLibrary[] = "interceptor_via_lib";
static const char kODRViolation[] = "odr_violation";
static const char *kSuppressionTypes[] = {
    kInterceptorName, kInterceptorViaFunction, kInterceptorViaLibrary,
    kODRViolation};

SANITIZER_INTERFACE_WEAK_DEF(const char *, __asan_default_suppressions, void) {
  return "";
}

void InitializeSuppressions() {
  CHECK_EQ(nullptr, suppression_ctx);
  suppression_ctx = new (suppression_placeholder)  // NOLINT
      SuppressionContext(kSuppressionTypes, ARRAY_SIZE(kSuppressionTypes));
  suppression_ctx->ParseFromFile(flags()->suppressions);
  if (&__asan_default_suppressions)
    suppression_ctx->Parse(__asan_default_suppressions());
}

bool IsInterceptorSuppressed(const char *interceptor_name) {
  CHECK(suppression_ctx);
  Suppression *s;
  // Match "interceptor_name" suppressions.
  return suppression_ctx->Match(interceptor_name, kInterceptorName, &s);
}

bool HaveStackTraceBasedSuppressions() {
  CHECK(suppression_ctx);
  return suppression_ctx->HasSuppressionType(kInterceptorViaFunction) ||
         suppression_ctx->HasSuppressionType(kInterceptorViaLibrary);
}

bool IsODRViolationSuppressed(const char *global_var_name) {
  CHECK(suppression_ctx);
  Suppression *s;
  // Match "odr_violation" suppressions.
  return suppression_ctx->Match(global_var_name, kODRViolation, &s);
}

bool IsStackTraceSuppressed(const StackTrace *stack) {
  if (!HaveStackTraceBasedSuppressions())
    return false;

  CHECK(suppression_ctx);
  Symbolizer *symbolizer = Symbolizer::GetOrInit();
  Suppression *s;
  for (uptr i = 0; i < stack->size && stack->trace[i]; i++) {
    uptr addr = stack->trace[i];

    if (suppression_ctx->HasSuppressionType(kInterceptorViaLibrary)) {
      // Match "interceptor_via_lib" suppressions.
      if (const char *module_name = symbolizer->GetModuleNameForPc(addr))
        if (suppression_ctx->Match(module_name, kInterceptorViaLibrary, &s))
          return true;
    }

    if (suppression_ctx->HasSuppressionType(kInterceptorViaFunction)) {
      SymbolizedStack *frames = symbolizer->SymbolizePC(addr);
      CHECK(frames);
      for (SymbolizedStack *cur = frames; cur; cur = cur->next) {
        const char *function_name = cur->info.function;
        if (!function_name) {
          continue;
        }
        // Match "interceptor_via_fun" suppressions.
        if (suppression_ctx->Match(function_name, kInterceptorViaFunction,
                                   &s)) {
          frames->ClearAll();
          return true;
        }
      }
      frames->ClearAll();
    }
  }
  return false;
}

} // namespace __asan
