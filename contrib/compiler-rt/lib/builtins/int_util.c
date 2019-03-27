/* ===-- int_util.c - Implement internal utilities --------------------------===
 *
 *                     The LLVM Compiler Infrastructure
 *
 * This file is dual licensed under the MIT and the University of Illinois Open
 * Source Licenses. See LICENSE.TXT for details.
 *
 * ===----------------------------------------------------------------------===
 */

#include "int_lib.h"
#include "int_util.h"

/* NOTE: The definitions in this file are declared weak because we clients to be
 * able to arbitrarily package individual functions into separate .a files. If
 * we did not declare these weak, some link situations might end up seeing
 * duplicate strong definitions of the same symbol.
 *
 * We can't use this solution for kernel use (which may not support weak), but
 * currently expect that when built for kernel use all the functionality is
 * packaged into a single library.
 */

#ifdef KERNEL_USE

NORETURN extern void panic(const char *, ...);
#ifndef _WIN32
__attribute__((visibility("hidden")))
#endif
void __compilerrt_abort_impl(const char *file, int line, const char *function) {
  panic("%s:%d: abort in %s", file, line, function);
}

#elif __APPLE__

/* from libSystem.dylib */
NORETURN extern void __assert_rtn(const char *func, const char *file, int line,
                                  const char *message);

#ifndef _WIN32
__attribute__((weak))
__attribute__((visibility("hidden")))
#endif
void __compilerrt_abort_impl(const char *file, int line, const char *function) {
  __assert_rtn(function, file, line, "libcompiler_rt abort");
}

#elif __Fuchsia__

#ifndef _WIN32
__attribute__((weak))
__attribute__((visibility("hidden")))
#endif
void __compilerrt_abort_impl(const char *file, int line, const char *function) {
  __builtin_trap();
}

#else

/* Get the system definition of abort() */
#include <stdlib.h>

#ifndef _WIN32
__attribute__((weak))
__attribute__((visibility("hidden")))
#endif
void __compilerrt_abort_impl(const char *file, int line, const char *function) {
  abort();
}

#endif
