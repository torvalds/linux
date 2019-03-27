/*
 * Copyright (C) 2008  Internet Systems Consortium, Inc. ("ISC")
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

/* $Id: base32.h,v 1.3 2008/09/25 04:02:39 tbox Exp $ */

#ifndef ISC_BASE32_H
#define ISC_BASE32_H 1

/*! \file */

/*
 * Routines for manipulating base 32 and base 32 hex encoded data.
 * Based on RFC 4648.
 *
 * Base 32 hex preserves the sort order of data when it is encoded /
 * decoded.
 */

#include <isc/lang.h>
#include <isc/types.h>

ISC_LANG_BEGINDECLS

/***
 *** Functions
 ***/

isc_result_t
isc_base32_totext(isc_region_t *source, int wordlength,
		  const char *wordbreak, isc_buffer_t *target);
isc_result_t
isc_base32hex_totext(isc_region_t *source, int wordlength,
		     const char *wordbreak, isc_buffer_t *target);
/*!<
 * \brief Convert data into base32 encoded text.
 *
 * Notes:
 *\li	The base32 encoded text in 'target' will be divided into
 *	words of at most 'wordlength' characters, separated by
 * 	the 'wordbreak' string.  No parentheses will surround
 *	the text.
 *
 * Requires:
 *\li	'source' is a region containing binary data
 *\li	'target' is a text buffer containing available space
 *\li	'wordbreak' points to a null-terminated string of
 *		zero or more whitespace characters
 *
 * Ensures:
 *\li	target will contain the base32 encoded version of the data
 *	in source.  The 'used' pointer in target will be advanced as
 *	necessary.
 */

isc_result_t
isc_base32_decodestring(const char *cstr, isc_buffer_t *target);
isc_result_t
isc_base32hex_decodestring(const char *cstr, isc_buffer_t *target);
/*!<
 * \brief Decode a null-terminated base32 string.
 *
 * Requires:
 *\li	'cstr' is non-null.
 *\li	'target' is a valid buffer.
 *
 * Returns:
 *\li	#ISC_R_SUCCESS	-- the entire decoded representation of 'cstring'
 *			   fit in 'target'.
 *\li	#ISC_R_BADBASE32 -- 'cstr' is not a valid base32 encoding.
 *
 * 	Other error returns are any possible error code from:
 *\li		isc_lex_create(),
 *\li		isc_lex_openbuffer(),
 *\li		isc_base32_tobuffer().
 */

isc_result_t
isc_base32_tobuffer(isc_lex_t *lexer, isc_buffer_t *target, int length);
isc_result_t
isc_base32hex_tobuffer(isc_lex_t *lexer, isc_buffer_t *target, int length);
/*!<
 * \brief Convert base32 encoded text from a lexer context into data.
 *
 * Requires:
 *\li	'lex' is a valid lexer context
 *\li	'target' is a buffer containing binary data
 *\li	'length' is an integer
 *
 * Ensures:
 *\li	target will contain the data represented by the base32 encoded
 *	string parsed by the lexer.  No more than length bytes will be read,
 *	if length is positive.  The 'used' pointer in target will be
 *	advanced as necessary.
 */

isc_result_t
isc_base32_decoderegion(isc_region_t *source, isc_buffer_t *target);
isc_result_t
isc_base32hex_decoderegion(isc_region_t *source, isc_buffer_t *target);
/*!<
 * \brief Decode a packed (no white space permitted) base32 region.
 *
 * Requires:
 *\li   'source' is a valid region.
 *\li   'target' is a valid buffer.
 *
 * Returns:
 *\li   #ISC_R_SUCCESS  -- the entire decoded representation of 'cstring'
 *                         fit in 'target'.
 *\li   #ISC_R_BADBASE32 -- 'source' is not a valid base32 encoding.
 */

ISC_LANG_ENDDECLS

#endif /* ISC_BASE32_H */
