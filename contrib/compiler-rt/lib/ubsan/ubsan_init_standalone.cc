//===-- ubsan_init_standalone.cc ------------------------------------------===//
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
# error "UBSan is not supported on this platform!"
#endif

#include "sanitizer_common/sanitizer_internal_defs.h"
#include "ubsan_init.h"
#include "ubsan_signals_standalone.h"

namespace __ubsan {

class UbsanStandaloneInitializer {
 public:
  UbsanStandaloneInitializer() {
    InitAsStandalone();
    InitializeDeadlySignals();
  }
};
static UbsanStandaloneInitializer ubsan_standalone_initializer;

} // namespace __ubsan
