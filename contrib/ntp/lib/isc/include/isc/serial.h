/*
 * Copyright (C) 2004-2007, 2009  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 1999-2001  Internet Software Consortium.
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

/* $Id: serial.h,v 1.18 2009/01/18 23:48:14 tbox Exp $ */

#ifndef ISC_SERIAL_H
#define ISC_SERIAL_H 1

#include <isc/lang.h>
#include <isc/types.h>

/*! \file isc/serial.h
 *	\brief Implement 32 bit serial space arithmetic comparison functions.
 *	Note: Undefined results are returned as ISC_FALSE.
 */

/***
 ***	Functions
 ***/

ISC_LANG_BEGINDECLS

isc_boolean_t
isc_serial_lt(isc_uint32_t a, isc_uint32_t b);
/*%<
 *	Return true if 'a' < 'b' otherwise false.
 */

isc_boolean_t
isc_serial_gt(isc_uint32_t a, isc_uint32_t b);
/*%<
 *	Return true if 'a' > 'b' otherwise false.
 */

isc_boolean_t
isc_serial_le(isc_uint32_t a, isc_uint32_t b);
/*%<
 *	Return true if 'a' <= 'b' otherwise false.
 */

isc_boolean_t
isc_serial_ge(isc_uint32_t a, isc_uint32_t b);
/*%<
 *	Return true if 'a' >= 'b' otherwise false.
 */

isc_boolean_t
isc_serial_eq(isc_uint32_t a, isc_uint32_t b);
/*%<
 *	Return true if 'a' == 'b' otherwise false.
 */

isc_boolean_t
isc_serial_ne(isc_uint32_t a, isc_uint32_t b);
/*%<
 *	Return true if 'a' != 'b' otherwise false.
 */

ISC_LANG_ENDDECLS

#endif /* ISC_SERIAL_H */
