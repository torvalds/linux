/*
 * Copyright (C) 2004-2007  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 2001, 2002  Internet Software Consortium.
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

/* $Id: parseint.h,v 1.9 2007/06/19 23:47:18 tbox Exp $ */

#ifndef ISC_PARSEINT_H
#define ISC_PARSEINT_H 1

#include <isc/lang.h>
#include <isc/types.h>

/*! \file isc/parseint.h
 * \brief Parse integers, in a saner way than atoi() or strtoul() do.
 */

/***
 ***	Functions
 ***/

ISC_LANG_BEGINDECLS

isc_result_t
isc_parse_uint32(isc_uint32_t *uip, const char *string, int base);

isc_result_t
isc_parse_uint16(isc_uint16_t *uip, const char *string, int base);

isc_result_t
isc_parse_uint8(isc_uint8_t *uip, const char *string, int base);
/*%<
 * Parse the null-terminated string 'string' containing a base 'base'
 * integer, storing the result in '*uip'.  
 * The base is interpreted
 * as in strtoul().  Unlike strtoul(), leading whitespace, minus or
 * plus signs are not accepted, and all errors (including overflow)
 * are reported uniformly through the return value.
 *
 * Requires:
 *\li	'string' points to a null-terminated string
 *\li	0 <= 'base' <= 36
 *
 * Returns:
 *\li	#ISC_R_SUCCESS
 *\li	#ISC_R_BADNUMBER   The string is not numeric (in the given base)
 *\li	#ISC_R_RANGE	  The number is not representable as the requested type.
 */

ISC_LANG_ENDDECLS

#endif /* ISC_PARSEINT_H */
