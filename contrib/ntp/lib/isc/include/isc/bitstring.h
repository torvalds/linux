/*
 * Copyright (C) 2004-2007  Internet Systems Consortium, Inc. ("ISC")
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

/* $Id: bitstring.h,v 1.14 2007/06/19 23:47:18 tbox Exp $ */

#ifndef ISC_BITSTRING_H
#define ISC_BITSTRING_H 1

/*****
 ***** Module Info
 *****/

/*! \file isc/bitstring.h
 *
 * \brief Bitstring manipulation functions.
 *
 * A bitstring is a packed array of bits, stored in a contiguous
 * sequence of octets.  The "most significant bit" (msb) of a bitstring
 * is the high bit of the first octet.  The "least significant bit" of a
 * bitstring is the low bit of the last octet.
 *
 * Two bit numbering schemes are supported, "msb0" and "lsb0".
 *
 * In the "msb0" scheme, bit number 0 designates the most significant bit,
 * and any padding bits required to make the bitstring a multiple of 8 bits
 * long are added to the least significant end of the last octet.
 *
 * In the "lsb0" scheme, bit number 0 designates the least significant bit,
 * and any padding bits required to make the bitstring a multiple of 8 bits
 * long are added to the most significant end of the first octet.
 *
 * E.g., consider the bitstring "11010001111".  This bitstring is 11 bits
 * long and will take two octets.  Let "p" denote a pad bit.  In the msb0
 * encoding, it would be
 *
 * \verbatim
 *             Octet 0           Octet 1
 *                         |
 *         1 1 0 1 0 0 0 1 | 1 1 1 p p p p p
 *         ^               |               ^
 *         |                               |
 *         bit 0                           bit 15
 * \endverbatim
 *
 * In the lsb0 encoding, it would be
 *
 * \verbatim
 *             Octet 0           Octet 1
 *                         |
 *         p p p p p 1 1 0 | 1 0 0 0 1 1 1 1 
 *         ^               |               ^
 *         |                               |
 *         bit 15                          bit 0
 * \endverbatim
 */

/***
 *** Imports
 ***/

#include <isc/lang.h>
#include <isc/types.h>

ISC_LANG_BEGINDECLS

/***
 *** Types
 ***/

struct isc_bitstring {
	unsigned int		magic;
	unsigned char *		data;
	unsigned int		length;
	unsigned int		size;
	isc_boolean_t		lsb0;
};

/***
 *** Functions
 ***/

void
isc_bitstring_init(isc_bitstring_t *bitstring, unsigned char *data,
		   unsigned int length, unsigned int size, isc_boolean_t lsb0);
/*!<
 * \brief Make 'bitstring' refer to the bitstring of 'size' bits starting
 * at 'data'.  'length' bits of the bitstring are valid.  If 'lsb0'
 * is set then, bit 0 refers to the least significant bit of the
 * bitstring.  Otherwise bit 0 is the most significant bit.
 *
 * Requires:
 *
 *\li	'bitstring' points to a isc_bitstring_t.
 *
 *\li	'data' points to an array of unsigned char large enough to hold
 *	'size' bits.
 *
 *\li	'length' <= 'size'.
 *
 * Ensures:
 *
 *\li	'bitstring' is a valid bitstring.
 */

void
isc_bitstring_invalidate(isc_bitstring_t *bitstring);
/*!<
 * \brief Invalidate 'bitstring'.
 *
 * Requires:
 *
 *\li	'bitstring' is a valid bitstring.
 *
 * Ensures:
 *
 *\li	'bitstring' is not a valid bitstring.
 */

void
isc_bitstring_copy(isc_bitstring_t *source, unsigned int sbitpos,
		   isc_bitstring_t *target, unsigned int tbitpos,
		   unsigned int n);
/*!<
 * \brief Starting at bit 'sbitpos', copy 'n' bits from 'source' to
 * the 'n' bits of 'target' starting at 'tbitpos'.
 *
 * Requires:
 *
 *\li	'source' and target are valid bitstrings with the same lsb0 setting.
 *
 *\li	'sbitpos' + 'n' is less than or equal to the length of 'source'.
 *
 *\li	'tbitpos' + 'n' is less than or equal to the size of 'target'.
 *
 * Ensures:
 *
 *\li	The specified bits have been copied, and the length of 'target'
 *	adjusted (if required).
 */

ISC_LANG_ENDDECLS

#endif /* ISC_BITSTRING_H */
