//===-- sanitizer_win_dll_thunk.cc ----------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// This file defines a family of thunks that should be statically linked into
// the DLLs that have instrumentation in order to delegate the calls to the
// shared runtime that lives in the main binary.
// See https://github.com/google/sanitizers/issues/209 for the details.
//===----------------------------------------------------------------------===//

#ifdef SANITIZER_DLL_THUNK
#include "sanitizer_win_defs.h"
#include "sanitizer_win_dll_thunk.h"
#include "interception/interception.h"

extern "C" {
void *WINAPI GetModuleHandleA(const char *module_name);
void abort();
}

namespace __sanitizer {
uptr dllThunkGetRealAddrOrDie(const char *name) {
  uptr ret =
      __interception::InternalGetProcAddress((void *)GetModuleHandleA(0), name);
  if (!ret)
    abort();
  return ret;
}

int dllThunkIntercept(const char* main_function, uptr dll_function) {
  uptr wrapper = dllThunkGetRealAddrOrDie(main_function);
  if (!__interception::OverrideFunction(dll_function, wrapper, 0))
    abort();
  return 0;
}

int dllThunkInterceptWhenPossible(const char* main_function,
    const char* default_function, uptr dll_function) {
  uptr wrapper = __interception::InternalGetProcAddress(
    (void *)GetModuleHandleA(0), main_function);
  if (!wrapper)
    wrapper = dllThunkGetRealAddrOrDie(default_function);
  if (!__interception::OverrideFunction(dll_function, wrapper, 0))
    abort();
  return 0;
}
} // namespace __sanitizer

// Include Sanitizer Common interface.
#define INTERFACE_FUNCTION(Name) INTERCEPT_SANITIZER_FUNCTION(Name)
#define INTERFACE_WEAK_FUNCTION(Name) INTERCEPT_SANITIZER_WEAK_FUNCTION(Name)
#include "sanitizer_common_interface.inc"

#pragma section(".DLLTH$A", read)  // NOLINT
#pragma section(".DLLTH$Z", read)  // NOLINT

typedef void (*DllThunkCB)();
extern "C" {
__declspec(allocate(".DLLTH$A")) DllThunkCB __start_dll_thunk;
__declspec(allocate(".DLLTH$Z")) DllThunkCB __stop_dll_thunk;
}

// Disable compiler warnings that show up if we declare our own version
// of a compiler intrinsic (e.g. strlen).
#pragma warning(disable: 4391)
#pragma warning(disable: 4392)

extern "C" int __dll_thunk_init() {
  static bool flag = false;
  // __dll_thunk_init is expected to be called by only one thread.
  if (flag) return 0;
  flag = true;

  for (DllThunkCB *it = &__start_dll_thunk; it < &__stop_dll_thunk; ++it)
    if (*it)
      (*it)();

  // In DLLs, the callbacks are expected to return 0,
  // otherwise CRT initialization fails.
  return 0;
}

// We want to call dll_thunk_init before C/C++ initializers / constructors are
// executed, otherwise functions like memset might be invoked.
#pragma section(".CRT$XIB", long, read)  // NOLINT
__declspec(allocate(".CRT$XIB")) int (*__dll_thunk_preinit)() =
    __dll_thunk_init;

static void WINAPI dll_thunk_thread_init(void *mod, unsigned long reason,
                                         void *reserved) {
  if (reason == /*DLL_PROCESS_ATTACH=*/1) __dll_thunk_init();
}

#pragma section(".CRT$XLAB", long, read)  // NOLINT
__declspec(allocate(".CRT$XLAB")) void (WINAPI *__dll_thunk_tls_init)(void *,
    unsigned long, void *) = dll_thunk_thread_init;

#endif // SANITIZER_DLL_THUNK
