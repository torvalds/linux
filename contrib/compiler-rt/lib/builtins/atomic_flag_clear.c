/*===-- atomic_flag_clear.c -------------------------------------------------===
 *
 *                     The LLVM Compiler Infrastructure
 *
 * This file is dual licensed under the MIT and the University of Illinois Open
 * Source Licenses. See LICENSE.TXT for details.
 *
 *===------------------------------------------------------------------------===
 *
 * This file implements atomic_flag_clear from C11's stdatomic.h.
 *
 *===------------------------------------------------------------------------===
 */

#ifndef __has_include
#define __has_include(inc) 0
#endif

#if __has_include(<stdatomic.h>)

#include <stdatomic.h>
#undef atomic_flag_clear
void atomic_flag_clear(volatile atomic_flag *object) {
  __c11_atomic_store(&(object)->_Value, 0, __ATOMIC_SEQ_CST);
}

#endif
