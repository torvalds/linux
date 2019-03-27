//=-- ubsan_signals_standalone.cc
//------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Installs signal handlers and related interceptors for UBSan standalone.
//
//===----------------------------------------------------------------------===//

#include "ubsan_platform.h"
#include "sanitizer_common/sanitizer_platform.h"
#if CAN_SANITIZE_UB
#include "interception/interception.h"
#include "sanitizer_common/sanitizer_stacktrace.h"
#include "ubsan_diag.h"
#include "ubsan_init.h"

// Interception of signals breaks too many things on Android.
// * It requires that ubsan is the first dependency of the main executable for
// the interceptors to work correctly. This complicates deployment, as it
// prevents us from enabling ubsan on random platform modules independently.
// * For this to work with ART VM, ubsan signal handler has to be set after the
// debuggerd handler, but before the ART handler.
// * Interceptors don't work at all when ubsan runtime is loaded late, ex. when
// it is part of an APK that does not use wrap.sh method.
#if SANITIZER_FUCHSIA || SANITIZER_ANDROID

namespace __ubsan {
void InitializeDeadlySignals() {}
}

#else

#define COMMON_INTERCEPT_FUNCTION(name) INTERCEPT_FUNCTION(name)
#include "sanitizer_common/sanitizer_signal_interceptors.inc"

namespace __ubsan {

static void OnStackUnwind(const SignalContext &sig, const void *,
                          BufferedStackTrace *stack) {
  GetStackTrace(stack, kStackTraceMax, sig.pc, sig.bp, sig.context,
                common_flags()->fast_unwind_on_fatal);
}

static void UBsanOnDeadlySignal(int signo, void *siginfo, void *context) {
  HandleDeadlySignal(siginfo, context, GetTid(), &OnStackUnwind, nullptr);
}

static bool is_initialized = false;

void InitializeDeadlySignals() {
  if (is_initialized)
    return;
  is_initialized = true;
  InitializeSignalInterceptors();
  InstallDeadlySignalHandlers(&UBsanOnDeadlySignal);
}

} // namespace __ubsan

#endif

#endif // CAN_SANITIZE_UB
