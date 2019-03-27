/*
 * Copyright (C) 2004, 2005, 2007  Internet Systems Consortium, Inc. ("ISC")
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

/* $Id: bitstring.c,v 1.17 2007/06/19 23:47:17 tbox Exp $ */

/*! \file */

#include <config.h>

#include <stddef.h>

#include <isc/magic.h>
#include <isc/bitstring.h>
#include <isc/util.h>

#define DIV8(x)			((x) >> 3)
#define MOD8(x)			((x) & 0x00000007U)
#define OCTETS(n)		(((n) + 7) >> 3)
#define PADDED(n)		((((n) + 7) >> 3) << 3)
#define BITSET(bs, n) 		(((bs)->data[DIV8(n)] & \
				 (1 << (7 - MOD8(n)))) != 0)
#define SETBIT(bs, n)		(bs)->data[DIV8(n)] |= (1 << (7 - MOD8(n)))
#define CLEARBIT(bs, n)		(bs)->data[DIV8(n)] &= ~(1 << (7 - MOD8(n)))

#define BITSTRING_MAGIC		ISC_MAGIC('B', 'S', 't', 'r')
#define VALID_BITSTRING(b)	ISC_MAGIC_VALID(b, BITSTRING_MAGIC)

void
isc_bitstring_init(isc_bitstring_t *bitstring, unsigned char *data,
		   unsigned int length, unsigned int size, isc_boolean_t lsb0)
{
	/*
	 * Make 'bitstring' refer to the bitstring of 'size' bits starting
	 * at 'data'.  'length' bits of the bitstring are valid.  If 'lsb0'
	 * is set then, bit 0 refers to the least significant bit of the
	 * bitstring.  Otherwise bit 0 is the most significant bit.
	 */

	REQUIRE(bitstring != NULL);
	REQUIRE(data != NULL);
	REQUIRE(length <= size);

	bitstring->magic = BITSTRING_MAGIC;
	bitstring->data = data;
	bitstring->length = length;
	bitstring->size = size;
	bitstring->lsb0 = lsb0;
}

void
isc_bitstring_invalidate(isc_bitstring_t *bitstring) {

	/*
	 * Invalidate 'bitstring'.
	 */

	REQUIRE(VALID_BITSTRING(bitstring));

	bitstring->magic = 0;
	bitstring->data = NULL;
	bitstring->length = 0;
	bitstring->size = 0;
	bitstring->lsb0 = ISC_FALSE;
}

void
isc_bitstring_copy(isc_bitstring_t *source, unsigned int sbitpos,
		   isc_bitstring_t *target, unsigned int tbitpos,
		   unsigned int n)
{
	unsigned int tlast;

	/*
	 * Starting at bit 'sbitpos', copy 'n' bits from 'source' to
	 * the 'n' bits of 'target' starting at 'tbitpos'.
	 */

	REQUIRE(VALID_BITSTRING(source));
	REQUIRE(VALID_BITSTRING(target));
	REQUIRE(source->lsb0 == target->lsb0);
	if (source->lsb0) {
		REQUIRE(sbitpos <= source->length);
		sbitpos = PADDED(source->size) - sbitpos;
		REQUIRE(sbitpos >= n);
		sbitpos -= n;
	} else
		REQUIRE(sbitpos + n <= source->length);
	tlast = tbitpos + n;
	if (target->lsb0) {
		REQUIRE(tbitpos <= target->length);
		tbitpos = PADDED(target->size) - tbitpos;
		REQUIRE(tbitpos >= n);
		tbitpos -= n;
	} else
		REQUIRE(tlast <= target->size);

	if (tlast > target->length)
		target->length = tlast;

	/*
	 * This is far from optimal...
	 */

	while (n > 0) {
		if (BITSET(source, sbitpos))
			SETBIT(target, tbitpos);
		else
			CLEARBIT(target, tbitpos);
		sbitpos++;
		tbitpos++;
		n--;
	}
}
