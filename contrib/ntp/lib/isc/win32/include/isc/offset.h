/*
 * Copyright (C) 2004, 2007  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 2000, 2001  Internet Software Consortium.
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

/* $Id: offset.h,v 1.6 2007/06/19 23:47:20 tbox Exp $ */

#ifndef ISC_OFFSET_H
#define ISC_OFFSET_H 1

/*
 * File offsets are operating-system dependent.
 */
#include <limits.h>             /* Required for CHAR_BIT. */
#include <sys/types.h>

typedef _off_t isc_offset_t;

/*
 * POSIX says "Additionally, blkcnt_t and off_t are extended signed integral
 * types", so the maximum value is all 1s except for the high bit.
 * This definition is more complex than it really needs to be because it was
 * crafted to keep both the SunOS 5.6 and the HP/UX 11 compilers quiet about
 * integer overflow.  For example, though this is equivalent to just left
 * shifting 1 to the high bit and then inverting the bits, the SunOS compiler
 * is unhappy about shifting a positive "1" to negative in a signed integer.
 */
#define ISC_OFFSET_MAXIMUM \
	(~(((off_t)-1 >> (sizeof(off_t) * CHAR_BIT - 1)) \
		      << (sizeof(off_t) * CHAR_BIT - 1)))

#endif /* ISC_OFFSET_H */
