/*
 * Copyright (C) 2004, 2005, 2007  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 2001-2003  Internet Software Consortium.
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

/* $Id: parseint.c,v 1.8 2007/06/19 23:47:17 tbox Exp $ */

/*! \file */

#include <config.h>

#include <ctype.h>
#include <errno.h>
#include <limits.h>

#include <isc/parseint.h>
#include <isc/result.h>
#include <isc/stdlib.h>

isc_result_t
isc_parse_uint32(isc_uint32_t *uip, const char *string, int base) {
	unsigned long n;
	char *e;
	if (! isalnum((unsigned char)(string[0])))
		return (ISC_R_BADNUMBER);
	errno = 0;
	n = strtoul(string, &e, base);
	if (*e != '\0')
		return (ISC_R_BADNUMBER);
	if (n == ULONG_MAX && errno == ERANGE)
		return (ISC_R_RANGE);
	*uip = n;
	return (ISC_R_SUCCESS);
}

isc_result_t
isc_parse_uint16(isc_uint16_t *uip, const char *string, int base) {
	isc_uint32_t val;
	isc_result_t result;
	result = isc_parse_uint32(&val, string, base);
	if (result != ISC_R_SUCCESS)
		return (result);
	if (val > 0xFFFF)
		return (ISC_R_RANGE);
	*uip = (isc_uint16_t) val;
	return (ISC_R_SUCCESS);
}

isc_result_t
isc_parse_uint8(isc_uint8_t *uip, const char *string, int base) {
	isc_uint32_t val;
	isc_result_t result;
	result = isc_parse_uint32(&val, string, base);
	if (result != ISC_R_SUCCESS)
		return (result);
	if (val > 0xFF)
		return (ISC_R_RANGE);
	*uip = (isc_uint8_t) val;
	return (ISC_R_SUCCESS);
}
