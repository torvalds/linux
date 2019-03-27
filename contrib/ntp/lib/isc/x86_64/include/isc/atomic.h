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

/* $Id: atomic.h,v 1.6 2008/01/24 23:47:00 tbox Exp $ */

#ifndef ISC_ATOMIC_H
#define ISC_ATOMIC_H 1

#include <isc/platform.h>
#include <isc/types.h>

#ifdef ISC_PLATFORM_USEGCCASM

/* We share the gcc-version with x86_32 */
#error "impossible case.  check build configuration"

#elif defined(ISC_PLATFORM_USESTDASM)
/*
 * The followings are "generic" assembly code which implements the same
 * functionality in case the gcc extension cannot be used.  It should be
 * better to avoid inlining below, since we directly refer to specific
 * registers for arguments, which would not actually correspond to the
 * intended address or value in the embedded mnemonic.
 */
#include <isc/util.h>		/* for 'UNUSED' macro */

static isc_int32_t
isc_atomic_xadd(isc_int32_t *p, isc_int32_t val) {
	UNUSED(p);
	UNUSED(val);

	__asm (
		"movq %rdi, %rdx\n"
		"movl %esi, %eax\n"
#ifdef ISC_PLATFORM_USETHREADS
		"lock;"
#endif
		"xadd %eax, (%rdx)\n"
		/*
		 * XXX: assume %eax will be used as the return value.
		 */
		);
}

#ifdef ISC_PLATFORM_HAVEXADDQ
static isc_int64_t
isc_atomic_xaddq(isc_int64_t *p, isc_int64_t val) {
	UNUSED(p);
	UNUSED(val);

	__asm (
		"movq %rdi, %rdx\n"
		"movq %rsi, %rax\n"
#ifdef ISC_PLATFORM_USETHREADS
		"lock;"
#endif
		"xaddq %rax, (%rdx)\n"
		/*
		 * XXX: assume %rax will be used as the return value.
		 */
		);
}
#endif

static void
isc_atomic_store(isc_int32_t *p, isc_int32_t val) {
	UNUSED(p);
	UNUSED(val);

	__asm (
		"movq %rdi, %rax\n"
		"movl %esi, %edx\n"
#ifdef ISC_PLATFORM_USETHREADS
		"lock;"
#endif
		"xchgl (%rax), %edx\n"
		/*
		 * XXX: assume %rax will be used as the return value.
		 */
		);
}

static isc_int32_t
isc_atomic_cmpxchg(isc_int32_t *p, isc_int32_t cmpval, isc_int32_t val) {
	UNUSED(p);
	UNUSED(cmpval);
	UNUSED(val);

	__asm (
		"movl %edx, %ecx\n"
		"movl %esi, %eax\n"
		"movq %rdi, %rdx\n"

#ifdef ISC_PLATFORM_USETHREADS
		"lock;"
#endif
		/*
		 * If (%rdi) == %eax then (%rdi) := %edx.
		 * %eax is set to old (%ecx), which will be the return value.
		 */
		"cmpxchgl %ecx, (%rdx)"
		);
}

#else /* !ISC_PLATFORM_USEGCCASM && !ISC_PLATFORM_USESTDASM */

#error "unsupported compiler.  disable atomic ops by --disable-atomic"

#endif
#endif /* ISC_ATOMIC_H */
