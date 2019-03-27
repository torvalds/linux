/*
 * Copyright (C) 2005, 2007  Internet Systems Consortium, Inc. ("ISC")
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

/* $Id: atomic.h,v 1.5 2007/06/19 23:47:18 tbox Exp $ */

/*
 * This code was written based on FreeBSD's kernel source whose copyright
 * follows:
 */

/*-
 * Copyright (c) 1998 Doug Rabson.
 * Copyright (c) 2001 Jake Burkholder.
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
 *	from: FreeBSD: src/sys/i386/include/atomic.h,v 1.20 2001/02/11
 * $FreeBSD:  258945 2013-12-04 21:33:17Z roberto $
 */

#ifndef ISC_ATOMIC_H
#define ISC_ATOMIC_H 1

#include <isc/platform.h>
#include <isc/types.h>

#define	ASI_P	0x80		/* Primary Address Space Identifier */

#ifdef ISC_PLATFORM_USEGCCASM

/*
 * This routine atomically increments the value stored in 'p' by 'val', and
 * returns the previous value.
 */
static inline isc_int32_t
isc_atomic_xadd(isc_int32_t *p, isc_int32_t val) {
	isc_int32_t prev, swapped;

	for (prev = *(volatile isc_int32_t *)p; ; prev = swapped) {
		swapped = prev + val;
		__asm__ volatile(
			"casa [%1] %2, %3, %0"
			: "+r"(swapped)
			: "r"(p), "n"(ASI_P), "r"(prev));
		if (swapped == prev)
			break;
	}

	return (prev);
}

/*
 * This routine atomically stores the value 'val' in 'p'.
 */
static inline void
isc_atomic_store(isc_int32_t *p, isc_int32_t val) {
	isc_int32_t prev, swapped;

	for (prev = *(volatile isc_int32_t *)p; ; prev = swapped) {
		swapped = val;
		__asm__ volatile(
			"casa [%1] %2, %3, %0"
			: "+r"(swapped)
			: "r"(p), "n"(ASI_P), "r"(prev)
			: "memory");
		if (swapped == prev)
			break;
	}
}

/*
 * This routine atomically replaces the value in 'p' with 'val', if the
 * original value is equal to 'cmpval'.  The original value is returned in any
 * case.
 */
static inline isc_int32_t
isc_atomic_cmpxchg(isc_int32_t *p, isc_int32_t cmpval, isc_int32_t val) {
	isc_int32_t temp = val;

	__asm__ volatile(
		"casa [%1] %2, %3, %0"
		: "+r"(temp)
		: "r"(p), "n"(ASI_P), "r"(cmpval));

	return (temp);
}

#else  /* ISC_PLATFORM_USEGCCASM */

#error "unsupported compiler.  disable atomic ops by --disable-atomic"

#endif /* ISC_PLATFORM_USEGCCASM */

#endif /* ISC_ATOMIC_H */
