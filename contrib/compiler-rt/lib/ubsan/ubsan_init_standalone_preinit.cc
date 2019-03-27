//===-- ubsan_init_standalone_preinit.cc ---------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Initialization of standalone UBSan runtime.
//
//===----------------------------------------------------------------------===//

#include "ubsan_platform.h"
#if !CAN_SANITIZE_UB
#error "UBSan is not supported on this platform!"
#endif

#include "sanitizer_common/sanitizer_internal_defs.h"
#include "ubsan_init.h"
#include "ubsan_signals_standalone.h"

#if SANITIZER_CAN_USE_PREINIT_ARRAY

namespace __ubsan {

static void PreInitAsStandalone() {
  InitAsStandalone();
  InitializeDeadlySignals();
}

} // namespace __ubsan

__attribute__((section(".preinit_array"), used)) void (*__local_ubsan_preinit)(
    void) = __ubsan::PreInitAsStandalone;
#endif // SANITIZER_CAN_USE_PREINIT_ARRAY
