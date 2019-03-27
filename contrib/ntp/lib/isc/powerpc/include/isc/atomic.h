/*
 * Copyright (C) 2005, 2007, 2009, 2011, 2012  Internet Systems Consortium, Inc. ("ISC")
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

/* $Id$ */

#ifndef ISC_ATOMIC_H
#define ISC_ATOMIC_H 1

#include <isc/platform.h>
#include <isc/types.h>

/*!\file
 * static inline isc_int32_t
 * isc_atomic_xadd(isc_int32_t *p, isc_int32_t val);
 *
 * This routine atomically increments the value stored in 'p' by 'val', and
 * returns the previous value.
 *
 * static inline void
 * isc_atomic_store(void *p, isc_int32_t val);
 *
 * This routine atomically stores the value 'val' in 'p'.
 *
 * static inline isc_int32_t
 * isc_atomic_cmpxchg(isc_int32_t *p, isc_int32_t cmpval, isc_int32_t val);
 *
 * This routine atomically replaces the value in 'p' with 'val', if the
 * original value is equal to 'cmpval'.  The original value is returned in any
 * case.
 */

#if defined(_AIX)

#include <sys/atomic_op.h>

#define isc_atomic_store(p, v) _clear_lock(p, v)

#ifdef __GNUC__
static inline isc_int32_t
#else
static isc_int32_t
#endif
isc_atomic_xadd(isc_int32_t *p, isc_int32_t val) {
	int ret;

#ifdef __GNUC__
	asm("ics");
#else
	 __isync();
#endif

	ret = fetch_and_add((atomic_p)p, (int)val);

#ifdef __GNUC__
	asm("ics");
#else
	 __isync();
#endif

	 return (ret);
}

#ifdef __GNUC__
static inline int
#else
static int
#endif
isc_atomic_cmpxchg(atomic_p p, int old, int new) {
	int orig = old;

#ifdef __GNUC__
	asm("ics");
#else
	 __isync();
#endif
	if (compare_and_swap(p, &orig, new))
		orig = old;

#ifdef __GNUC__
	asm("ics");
#else
	 __isync();
#endif

	return (orig);
}

#elif defined(ISC_PLATFORM_USEGCCASM) || defined(ISC_PLATFORM_USEMACASM)
static inline isc_int32_t
isc_atomic_xadd(isc_int32_t *p, isc_int32_t val) {
	isc_int32_t orig;

	__asm__ volatile (
#ifdef ISC_PLATFORM_USEMACASM
		"1:"
		"lwarx r6, 0, %1\n"
		"mr %0, r6\n"
		"add r6, r6, %2\n"
		"stwcx. r6, 0, %1\n"
		"bne- 1b\n"
		"sync"
#else
		"1:"
		"lwarx 6, 0, %1\n"
		"mr %0, 6\n"
		"add 6, 6, %2\n"
		"stwcx. 6, 0, %1\n"
		"bne- 1b\n"
		"sync"
#endif
		: "=&r"(orig)
		: "r"(p), "r"(val)
		: "r6", "memory"
		);

	return (orig);
}

static inline void
isc_atomic_store(void *p, isc_int32_t val) {
	__asm__ volatile (
#ifdef ISC_PLATFORM_USEMACASM
		"1:"
		"lwarx r6, 0, %0\n"
		"lwz r6, %1\n"
		"stwcx. r6, 0, %0\n"
		"bne- 1b\n"
		"sync"
#else
		"1:"
		"lwarx 6, 0, %0\n"
		"lwz 6, %1\n"
		"stwcx. 6, 0, %0\n"
		"bne- 1b\n"
		"sync"
#endif
		:
		: "r"(p), "m"(val)
		: "r6", "memory"
		);
}

static inline isc_int32_t
isc_atomic_cmpxchg(isc_int32_t *p, isc_int32_t cmpval, isc_int32_t val) {
	isc_int32_t orig;

	__asm__ volatile (
#ifdef ISC_PLATFORM_USEMACASM
		"1:"
		"lwarx r6, 0, %1\n"
		"mr %0,r6\n"
		"cmpw r6, %2\n"
		"bne 2f\n"
		"mr r6, %3\n"
		"stwcx. r6, 0, %1\n"
		"bne- 1b\n"
		"2:\n"
		"sync"
#else
		"1:"
		"lwarx 6, 0, %1\n"
		"mr %0,6\n"
		"cmpw 6, %2\n"
		"bne 2f\n"
		"mr 6, %3\n"
		"stwcx. 6, 0, %1\n"
		"bne- 1b\n"
		"2:\n"
		"sync"
#endif
		: "=&r" (orig)
		: "r"(p), "r"(cmpval), "r"(val)
		: "r6", "memory"
		);

	return (orig);
}

#else

#error "unsupported compiler.  disable atomic ops by --disable-atomic"

#endif
#endif /* ISC_ATOMIC_H */
