//===-- xray_init.cc --------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of XRay, a dynamic runtime instrumentation system.
//
// XRay initialisation logic.
//===----------------------------------------------------------------------===//

#include <fcntl.h>
#include <strings.h>
#include <unistd.h>

#include "sanitizer_common/sanitizer_common.h"
#include "xray_defs.h"
#include "xray_flags.h"
#include "xray_interface_internal.h"

extern "C" {
void __xray_init();
extern const XRaySledEntry __start_xray_instr_map[] __attribute__((weak));
extern const XRaySledEntry __stop_xray_instr_map[] __attribute__((weak));
extern const XRayFunctionSledIndex __start_xray_fn_idx[] __attribute__((weak));
extern const XRayFunctionSledIndex __stop_xray_fn_idx[] __attribute__((weak));

#if SANITIZER_MAC
// HACK: This is a temporary workaround to make XRay build on 
// Darwin, but it will probably not work at runtime.
const XRaySledEntry __start_xray_instr_map[] = {};
extern const XRaySledEntry __stop_xray_instr_map[] = {};
extern const XRayFunctionSledIndex __start_xray_fn_idx[] = {};
extern const XRayFunctionSledIndex __stop_xray_fn_idx[] = {};
#endif
}

using namespace __xray;

// When set to 'true' this means the XRay runtime has been initialised. We use
// the weak symbols defined above (__start_xray_inst_map and
// __stop_xray_instr_map) to initialise the instrumentation map that XRay uses
// for runtime patching/unpatching of instrumentation points.
//
// FIXME: Support DSO instrumentation maps too. The current solution only works
// for statically linked executables.
atomic_uint8_t XRayInitialized{0};

// This should always be updated before XRayInitialized is updated.
SpinMutex XRayInstrMapMutex;
XRaySledMap XRayInstrMap;

// Global flag to determine whether the flags have been initialized.
atomic_uint8_t XRayFlagsInitialized{0};

// A mutex to allow only one thread to initialize the XRay data structures.
SpinMutex XRayInitMutex;

// __xray_init() will do the actual loading of the current process' memory map
// and then proceed to look for the .xray_instr_map section/segment.
void __xray_init() XRAY_NEVER_INSTRUMENT {
  SpinMutexLock Guard(&XRayInitMutex);
  // Short-circuit if we've already initialized XRay before.
  if (atomic_load(&XRayInitialized, memory_order_acquire))
    return;

  // XRAY is not compatible with PaX MPROTECT
  CheckMPROTECT();

  if (!atomic_load(&XRayFlagsInitialized, memory_order_acquire)) {
    initializeFlags();
    atomic_store(&XRayFlagsInitialized, true, memory_order_release);
  }

  if (__start_xray_instr_map == nullptr) {
    if (Verbosity())
      Report("XRay instrumentation map missing. Not initializing XRay.\n");
    return;
  }

  {
    SpinMutexLock Guard(&XRayInstrMapMutex);
    XRayInstrMap.Sleds = __start_xray_instr_map;
    XRayInstrMap.Entries = __stop_xray_instr_map - __start_xray_instr_map;
    XRayInstrMap.SledsIndex = __start_xray_fn_idx;
    XRayInstrMap.Functions = __stop_xray_fn_idx - __start_xray_fn_idx;
  }
  atomic_store(&XRayInitialized, true, memory_order_release);

#ifndef XRAY_NO_PREINIT
  if (flags()->patch_premain)
    __xray_patch();
#endif
}

// FIXME: Make check-xray tests work on FreeBSD without
// SANITIZER_CAN_USE_PREINIT_ARRAY.
// See sanitizer_internal_defs.h where the macro is defined.
// Calling unresolved PLT functions in .preinit_array can lead to deadlock on
// FreeBSD but here it seems benign.
#if !defined(XRAY_NO_PREINIT) &&                                               \
    (SANITIZER_CAN_USE_PREINIT_ARRAY || SANITIZER_FREEBSD)
// Only add the preinit array initialization if the sanitizers can.
__attribute__((section(".preinit_array"),
               used)) void (*__local_xray_preinit)(void) = __xray_init;
#else
// If we cannot use the .preinit_array section, we should instead use dynamic
// initialisation.
__attribute__ ((constructor (0)))
static void __local_xray_dyninit() {
  __xray_init();
}
#endif
