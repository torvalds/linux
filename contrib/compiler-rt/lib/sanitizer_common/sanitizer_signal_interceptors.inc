//===-- sanitizer_signal_interceptors.inc -----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Signal interceptors for sanitizers.
//
//===----------------------------------------------------------------------===//

#include "interception/interception.h"
#include "sanitizer_common.h"
#include "sanitizer_internal_defs.h"
#include "sanitizer_platform_interceptors.h"

using namespace __sanitizer;

#if SANITIZER_NETBSD
#define sigaction_symname __sigaction14
#else
#define sigaction_symname sigaction
#endif

#ifndef SIGNAL_INTERCEPTOR_SIGNAL_IMPL
#define SIGNAL_INTERCEPTOR_SIGNAL_IMPL(func, signum, handler) \
  { return REAL(func)(signum, handler); }
#endif

#ifndef SIGNAL_INTERCEPTOR_SIGACTION_IMPL
#define SIGNAL_INTERCEPTOR_SIGACTION_IMPL(signum, act, oldact) \
  { return REAL(sigaction_symname)(signum, act, oldact); }
#endif

#if SANITIZER_INTERCEPT_BSD_SIGNAL
INTERCEPTOR(uptr, bsd_signal, int signum, uptr handler) {
  if (GetHandleSignalMode(signum) == kHandleSignalExclusive) return 0;
  SIGNAL_INTERCEPTOR_SIGNAL_IMPL(bsd_signal, signum, handler);
}
#define INIT_BSD_SIGNAL COMMON_INTERCEPT_FUNCTION(bsd_signal)
#else  // SANITIZER_INTERCEPT_BSD_SIGNAL
#define INIT_BSD_SIGNAL
#endif  // SANITIZER_INTERCEPT_BSD_SIGNAL

#if SANITIZER_INTERCEPT_SIGNAL_AND_SIGACTION
INTERCEPTOR(uptr, signal, int signum, uptr handler) {
  if (GetHandleSignalMode(signum) == kHandleSignalExclusive)
    return (uptr) nullptr;
  SIGNAL_INTERCEPTOR_SIGNAL_IMPL(signal, signum, handler);
}
#define INIT_SIGNAL COMMON_INTERCEPT_FUNCTION(signal)

INTERCEPTOR(int, sigaction_symname, int signum,
            const __sanitizer_sigaction *act, __sanitizer_sigaction *oldact) {
  if (GetHandleSignalMode(signum) == kHandleSignalExclusive) return 0;
  SIGNAL_INTERCEPTOR_SIGACTION_IMPL(signum, act, oldact);
}
#define INIT_SIGACTION COMMON_INTERCEPT_FUNCTION(sigaction_symname)

namespace __sanitizer {
int real_sigaction(int signum, const void *act, void *oldact) {
  return REAL(sigaction_symname)(signum, (const __sanitizer_sigaction *)act,
                         (__sanitizer_sigaction *)oldact);
}
}  // namespace __sanitizer
#else  // SANITIZER_INTERCEPT_SIGNAL_AND_SIGACTION
#define INIT_SIGNAL
#define INIT_SIGACTION
// We need to have defined REAL(sigaction) on other systems.
namespace __sanitizer {
struct __sanitizer_sigaction;
}
DEFINE_REAL(int, sigaction, int signum, const __sanitizer_sigaction *act,
            __sanitizer_sigaction *oldact)
#endif  // SANITIZER_INTERCEPT_SIGNAL_AND_SIGACTION

static void InitializeSignalInterceptors() {
  static bool was_called_once;
  CHECK(!was_called_once);
  was_called_once = true;

  INIT_BSD_SIGNAL;
  INIT_SIGNAL;
  INIT_SIGACTION;
}
