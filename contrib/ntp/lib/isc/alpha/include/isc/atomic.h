/*
 * Copyright (C) 2005, 2007, 2009  Internet Systems Consortium, Inc. ("ISC")
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

/* $Id: atomic.h,v 1.7 2009/04/08 06:48:23 tbox Exp $ */

/*
 * This code was written based on FreeBSD's kernel source whose copyright
 * follows:
 */

/*-
 * Copyright (c) 1998 Doug Rabson
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD:  258945 2013-12-04 21:33:17Z roberto $
 */

#ifndef ISC_ATOMIC_H
#define ISC_ATOMIC_H 1

#include <isc/platform.h>
#include <isc/types.h>

#ifdef ISC_PLATFORM_USEOSFASM
#include <c_asm.h>

#pragma intrinsic(asm)

/*
 * This routine atomically increments the value stored in 'p' by 'val', and
 * returns the previous value.  Memory access ordering around this function
 * can be critical, so we add explicit memory block instructions at the
 * beginning and the end of it (same for other functions).
 */
static inline isc_int32_t
isc_atomic_xadd(isc_int32_t *p, isc_int32_t val) {
	return (asm("mb;"
		    "1:"
		    "ldl_l %t0, 0(%a0);"	/* load old value */
		    "mov %t0, %v0;"		/* copy the old value */
		    "addl %t0, %a1, %t0;"	/* calculate new value */
		    "stl_c %t0, 0(%a0);"	/* attempt to store */
		    "beq %t0, 1b;"		/* spin if failed */
		    "mb;",
		    p, val));
}

/*
 * This routine atomically stores the value 'val' in 'p'.
 */
static inline void
isc_atomic_store(isc_int32_t *p, isc_int32_t val) {
	(void)asm("mb;"
		  "1:"
		  "ldl_l %t0, 0(%a0);"		/* load old value */
		  "mov %a1, %t0;"		/* value to store */
		  "stl_c %t0, 0(%a0);"		/* attempt to store */
		  "beq %t0, 1b;"		/* spin if failed */
		  "mb;",
		  p, val);
}

/*
 * This routine atomically replaces the value in 'p' with 'val', if the
 * original value is equal to 'cmpval'.  The original value is returned in any
 * case.
 */
static inline isc_int32_t
isc_atomic_cmpxchg(isc_int32_t *p, isc_int32_t cmpval, isc_int32_t val) {

	return(asm("mb;"
		   "1:"
		   "ldl_l %t0, 0(%a0);"		/* load old value */
		   "mov %t0, %v0;"		/* copy the old value */
		   "cmpeq %t0, %a1, %t0;"	/* compare */
		   "beq %t0, 2f;"		/* exit if not equal */
		   "mov %a2, %t0;"		/* value to store */
		   "stl_c %t0, 0(%a0);"		/* attempt to store */
		   "beq %t0, 1b;"		/* if it failed, spin */
		   "2:"
		   "mb;",
		   p, cmpval, val));
}
#elif defined (ISC_PLATFORM_USEGCCASM)
static inline isc_int32_t
isc_atomic_xadd(isc_int32_t *p, isc_int32_t val) {
	isc_int32_t temp, prev;

	__asm__ volatile(
		"mb;"
		"1:"
		"ldl_l %0, %1;"			/* load old value */
		"mov %0, %2;"			/* copy the old value */
		"addl %0, %3, %0;"		/* calculate new value */
		"stl_c %0, %1;"			/* attempt to store */
		"beq %0, 1b;"			/* spin if failed */
		"mb;"
		: "=&r"(temp), "+m"(*p), "=&r"(prev)
		: "r"(val)
		: "memory");

	return (prev);
}

static inline void
isc_atomic_store(isc_int32_t *p, isc_int32_t val) {
	isc_int32_t temp;

	__asm__ volatile(
		"mb;"
		"1:"
		"ldl_l %0, %1;"			/* load old value */
		"mov %2, %0;"			/* value to store */
		"stl_c %0, %1;"			/* attempt to store */
		"beq %0, 1b;"			/* if it failed, spin */
		"mb;"
		: "=&r"(temp), "+m"(*p)
		: "r"(val)
		: "memory");
}

static inline isc_int32_t
isc_atomic_cmpxchg(isc_int32_t *p, isc_int32_t cmpval, isc_int32_t val) {
	isc_int32_t temp, prev;

	__asm__ volatile(
		"mb;"
		"1:"
		"ldl_l %0, %1;"			/* load old value */
		"mov %0, %2;"			/* copy the old value */
		"cmpeq %0, %3, %0;"		/* compare */
		"beq %0, 2f;"			/* exit if not equal */
		"mov %4, %0;"			/* value to store */
		"stl_c %0, %1;"			/* attempt to store */
		"beq %0, 1b;"			/* if it failed, spin */
		"2:"
		"mb;"
		: "=&r"(temp), "+m"(*p), "=&r"(prev)
		: "r"(cmpval), "r"(val)
		: "memory");

	return (prev);
}
#else

#error "unsupported compiler.  disable atomic ops by --disable-atomic"

#endif

#endif /* ISC_ATOMIC_H */
