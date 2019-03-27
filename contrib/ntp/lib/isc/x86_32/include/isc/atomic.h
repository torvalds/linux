/*
 * Copyright (C) 2005, 2007, 2008  Internet Systems Consortium, Inc. ("ISC")
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/* $Id: atomic.h,v 1.10 2008/01/24 23:47:00 tbox Exp $ */

#ifndef ISC_ATOMIC_H
#define ISC_ATOMIC_H 1

#include <isc/platform.h>
#include <isc/types.h>

#ifdef ISC_PLATFORM_USEGCCASM
/*
 * This routine atomically increments the value stored in 'p' by 'val', and
 * returns the previous value.
 */
static __inline__ isc_int32_t
isc_atomic_xadd(isc_int32_t *p, isc_int32_t val) {
	isc_int32_t prev = val;

	__asm__ volatile(
#ifdef ISC_PLATFORM_USETHREADS
		"lock;"
#endif
		"xadd %0, %1"
		:"=q"(prev)
		:"m"(*p), "0"(prev)
		:"memory", "cc");

	return (prev);
}

#ifdef ISC_PLATFORM_HAVEXADDQ
static __inline__ isc_int64_t
isc_atomic_xaddq(isc_int64_t *p, isc_int64_t val) {
	isc_int64_t prev = val;

	__asm__ volatile(
#ifdef ISC_PLATFORM_USETHREADS
	    "lock;"
#endif
	    "xaddq %0, %1"
	    :"=q"(prev)
	    :"m"(*p), "0"(prev)
	    :"memory", "cc");

	return (prev);
}
#endif /* ISC_PLATFORM_HAVEXADDQ */

/*
 * This routine atomically stores the value 'val' in 'p'.
 */
static __inline__ void
isc_atomic_store(isc_int32_t *p, isc_int32_t val) {
	__asm__ volatile(
#ifdef ISC_PLATFORM_USETHREADS
		/*
		 * xchg should automatically lock memory, but we add it
		 * explicitly just in case (it at least doesn't harm)
		 */
		"lock;"
#endif

		"xchgl %1, %0"
		:
		: "r"(val), "m"(*p)
		: "memory");
}

/*
 * This routine atomically replaces the value in 'p' with 'val', if the
 * original value is equal to 'cmpval'.  The original value is returned in any
 * case.
 */
static __inline__ isc_int32_t
isc_atomic_cmpxchg(isc_int32_t *p, isc_int32_t cmpval, isc_int32_t val) {
	__asm__ volatile(
#ifdef ISC_PLATFORM_USETHREADS
		"lock;"
#endif
		"cmpxchgl %1, %2"
		: "=a"(cmpval)
		: "r"(val), "m"(*p), "a"(cmpval)
		: "memory");

	return (cmpval);
}

#elif defined(ISC_PLATFORM_USESTDASM)
/*
 * The followings are "generic" assembly code which implements the same
 * functionality in case the gcc extension cannot be used.  It should be
 * better to avoid inlining below, since we directly refer to specific
 * positions of the stack frame, which would not actually point to the
 * intended address in the embedded mnemonic.
 */
#include <isc/util.h>		/* for 'UNUSED' macro */

static isc_int32_t
isc_atomic_xadd(isc_int32_t *p, isc_int32_t val) {
	UNUSED(p);
	UNUSED(val);

	__asm (
		"movl 8(%ebp), %ecx\n"
		"movl 12(%ebp), %edx\n"
#ifdef ISC_PLATFORM_USETHREADS
		"lock;"
#endif
		"xadd %edx, (%ecx)\n"

		/*
		 * set the return value directly in the register so that we
		 * can avoid guessing the correct position in the stack for a
		 * local variable.
		 */
		"movl %edx, %eax"
		);
}

static void
isc_atomic_store(isc_int32_t *p, isc_int32_t val) {
	UNUSED(p);
	UNUSED(val);

	__asm (
		"movl 8(%ebp), %ecx\n"
		"movl 12(%ebp), %edx\n"
#ifdef ISC_PLATFORM_USETHREADS
		"lock;"
#endif
		"xchgl (%ecx), %edx\n"
		);
}

static isc_int32_t
isc_atomic_cmpxchg(isc_int32_t *p, isc_int32_t cmpval, isc_int32_t val) {
	UNUSED(p);
	UNUSED(cmpval);
	UNUSED(val);

	__asm (
		"movl 8(%ebp), %ecx\n"
		"movl 12(%ebp), %eax\n"	/* must be %eax for cmpxchgl */
		"movl 16(%ebp), %edx\n"
#ifdef ISC_PLATFORM_USETHREADS
		"lock;"
#endif

		/*
		 * If (%ecx) == %eax then (%ecx) := %edx.
		 % %eax is set to old (%ecx), which will be the return value.
		 */
		"cmpxchgl %edx, (%ecx)"
		);
}
#else /* !ISC_PLATFORM_USEGCCASM && !ISC_PLATFORM_USESTDASM */

#error "unsupported compiler.  disable atomic ops by --disable-atomic"

#endif
#endif /* ISC_ATOMIC_H */
