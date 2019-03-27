/*===-- atomic_thread_fence.c -----------------------------------------------===
 *
 *                     The LLVM Compiler Infrastructure
 *
 * This file is dual licensed under the MIT and the University of Illinois Open
 * Source Licenses. See LICENSE.TXT for details.
 *
 *===------------------------------------------------------------------------===
 *
 * This file implements atomic_thread_fence from C11's stdatomic.h.
 *
 *===------------------------------------------------------------------------===
 */

#ifndef __has_include
#define __has_include(inc) 0
#endif

#if __has_include(<stdatomic.h>)

#include <stdatomic.h>
#undef atomic_thread_fence
void atomic_thread_fence(memory_order order) {
  __c11_atomic_thread_fence(order);
}

#endif
