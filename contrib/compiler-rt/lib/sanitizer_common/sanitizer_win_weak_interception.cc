//===-- sanitizer_win_weak_interception.cc --------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// This module should be included in the sanitizer when it is implemented as a
// shared library on Windows (dll), in order to delegate the calls of weak
// functions to the implementation in the main executable when a strong
// definition is provided.
//===----------------------------------------------------------------------===//

#include "sanitizer_platform.h"
#if SANITIZER_WINDOWS && SANITIZER_DYNAMIC
#include "sanitizer_win_weak_interception.h"
#include "sanitizer_allocator_interface.h"
#include "sanitizer_interface_internal.h"
#include "sanitizer_win_defs.h"
#include "interception/interception.h"

extern "C" {
void *WINAPI GetModuleHandleA(const char *module_name);
void abort();
}

namespace __sanitizer {
// Try to get a pointer to real_function in the main module and override
// dll_function with that pointer. If the function isn't found, nothing changes.
int interceptWhenPossible(uptr dll_function, const char *real_function) {
  uptr real = __interception::InternalGetProcAddress(
      (void *)GetModuleHandleA(0), real_function);
  if (real && !__interception::OverrideFunction((uptr)dll_function, real, 0))
    abort();
  return 0;
}
} // namespace __sanitizer

// Declare weak hooks.
extern "C" {
void __sanitizer_weak_hook_memcmp(uptr called_pc, const void *s1,
                                  const void *s2, uptr n, int result);
void __sanitizer_weak_hook_strcmp(uptr called_pc, const char *s1,
                                  const char *s2, int result);
void __sanitizer_weak_hook_strncmp(uptr called_pc, const char *s1,
                                   const char *s2, uptr n, int result);
void __sanitizer_weak_hook_strstr(uptr called_pc, const char *s1,
                                  const char *s2, char *result);
}

// Include Sanitizer Common interface.
#define INTERFACE_FUNCTION(Name)
#define INTERFACE_WEAK_FUNCTION(Name) INTERCEPT_SANITIZER_WEAK_FUNCTION(Name)
#include "sanitizer_common_interface.inc"

#pragma section(".WEAK$A", read)  // NOLINT
#pragma section(".WEAK$Z", read)  // NOLINT

typedef void (*InterceptCB)();
extern "C" {
__declspec(allocate(".WEAK$A")) InterceptCB __start_weak_list;
__declspec(allocate(".WEAK$Z")) InterceptCB __stop_weak_list;
}

static int weak_intercept_init() {
  static bool flag = false;
  // weak_interception_init is expected to be called by only one thread.
  if (flag) return 0;
  flag = true;

  for (InterceptCB *it = &__start_weak_list; it < &__stop_weak_list; ++it)
    if (*it)
      (*it)();

  // In DLLs, the callbacks are expected to return 0,
  // otherwise CRT initialization fails.
  return 0;
}

#pragma section(".CRT$XIB", long, read)  // NOLINT
__declspec(allocate(".CRT$XIB")) int (*__weak_intercept_preinit)() =
    weak_intercept_init;

static void WINAPI weak_intercept_thread_init(void *mod, unsigned long reason,
                                              void *reserved) {
  if (reason == /*DLL_PROCESS_ATTACH=*/1) weak_intercept_init();
}

#pragma section(".CRT$XLAB", long, read)  // NOLINT
__declspec(allocate(".CRT$XLAB")) void(WINAPI *__weak_intercept_tls_init)(
    void *, unsigned long, void *) = weak_intercept_thread_init;

#endif // SANITIZER_WINDOWS && SANITIZER_DYNAMIC
