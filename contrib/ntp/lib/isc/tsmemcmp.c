/*
 * Copyright (C) 2004-2007, 2011, 2012  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 1999-2001, 2003  Internet Software Consortium.
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

/*! \file */

#include <config.h>
#include <limits.h>
#include <isc/string.h>

/* Making a portable memcmp that has no internal branches and loops always
 * once for every byte without early-out shortcut has a few challenges.
 *
 * Inspired by 'timingsafe_memcmp()' from the BSD system and
 * https://github.com/libressl-portable/openbsd/blob/master/src/lib/libc/string/timingsafe_memcmp.c
 *
 * Sadly, that one is not portable C: It makes assumptions on the representation
 * of negative integers and assumes sign-preserving right-shift of negative
 * signed values. This is a rewrite from scratch that should not suffer from
 * such issues.
 *
 * 2015-12-12, J. Perlinger (perlinger-at-ntp-dot-org)
 */
int
isc_tsmemcmp(const void *p1, const void *p2, size_t nb) {
	const unsigned char *ucp1 = p1;
	const unsigned char *ucp2 = p2;
	unsigned int isLT = 0u;
	unsigned int isGT = 0u;
	volatile unsigned int mask = (1u << CHAR_BIT);

	for (/*NOP*/; 0 != nb; --nb, ++ucp1, ++ucp2) {
		isLT |= mask &
		    ((unsigned int)*ucp1 - (unsigned int)*ucp2);
		isGT |= mask &
		    ((unsigned int)*ucp2 - (unsigned int)*ucp1);
		mask &= ~(isLT | isGT);
	}
	return (int)(isGT >> CHAR_BIT) - (int)(isLT >> CHAR_BIT);
}
