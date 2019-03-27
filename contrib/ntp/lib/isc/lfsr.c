/*
 * Copyright (C) 2004, 2005, 2007  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 1999-2002  Internet Software Consortium.
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

/* $Id: lfsr.c,v 1.20 2007/06/19 23:47:17 tbox Exp $ */

/*! \file */

#include <config.h>

#include <stddef.h>
#include <stdlib.h>

#include <isc/assertions.h>
#include <isc/lfsr.h>
#include <isc/util.h>

#define VALID_LFSR(x)	(x != NULL)

void
isc_lfsr_init(isc_lfsr_t *lfsr, isc_uint32_t state, unsigned int bits,
	      isc_uint32_t tap, unsigned int count,
	      isc_lfsrreseed_t reseed, void *arg)
{
	REQUIRE(VALID_LFSR(lfsr));
	REQUIRE(8 <= bits && bits <= 32);
	REQUIRE(tap != 0);

	lfsr->state = state;
	lfsr->bits = bits;
	lfsr->tap = tap;
	lfsr->count = count;
	lfsr->reseed = reseed;
	lfsr->arg = arg;

	if (count == 0 && reseed != NULL)
		reseed(lfsr, arg);
	if (lfsr->state == 0)
		lfsr->state = 0xffffffffU >> (32 - lfsr->bits);
}

/*!
 * Return the next state of the lfsr.
 */
static inline isc_uint32_t
lfsr_generate(isc_lfsr_t *lfsr)
{

	/*
	 * If the previous state is zero, we must fill it with something
	 * here, or we will begin to generate an extremely predictable output.
	 *
	 * First, give the reseed function a crack at it.  If the state is
	 * still 0, set it to all ones.
	 */
	if (lfsr->state == 0) {
		if (lfsr->reseed != NULL)
			lfsr->reseed(lfsr, lfsr->arg);
		if (lfsr->state == 0)
			lfsr->state = 0xffffffffU >> (32 - lfsr->bits);
	}

	if (lfsr->state & 0x01) {
		lfsr->state = (lfsr->state >> 1) ^ lfsr->tap;
		return (1);
	} else {
		lfsr->state >>= 1;
		return (0);
	}
}

void
isc_lfsr_generate(isc_lfsr_t *lfsr, void *data, unsigned int count)
{
	unsigned char *p;
	unsigned int bit;
	unsigned int byte;

	REQUIRE(VALID_LFSR(lfsr));
	REQUIRE(data != NULL);
	REQUIRE(count > 0);

	p = data;
	byte = count;

	while (byte--) {
		*p = 0;
		for (bit = 0; bit < 7; bit++) {
			*p |= lfsr_generate(lfsr);
			*p <<= 1;
		}
		*p |= lfsr_generate(lfsr);
		p++;
	}

	if (lfsr->count != 0 && lfsr->reseed != NULL) {
		if (lfsr->count <= count * 8)
			lfsr->reseed(lfsr, lfsr->arg);
		else
			lfsr->count -= (count * 8);
	}
}

static inline isc_uint32_t
lfsr_skipgenerate(isc_lfsr_t *lfsr, unsigned int skip)
{
	while (skip--)
		(void)lfsr_generate(lfsr);

	(void)lfsr_generate(lfsr);

	return (lfsr->state);
}

/*
 * Skip "skip" states in "lfsr".
 */
void
isc_lfsr_skip(isc_lfsr_t *lfsr, unsigned int skip)
{
	REQUIRE(VALID_LFSR(lfsr));

	while (skip--)
		(void)lfsr_generate(lfsr);
}

/*
 * Skip states in lfsr1 and lfsr2 using the other's current state.
 * Return the final state of lfsr1 ^ lfsr2.
 */
isc_uint32_t
isc_lfsr_generate32(isc_lfsr_t *lfsr1, isc_lfsr_t *lfsr2)
{
	isc_uint32_t state1, state2;
	isc_uint32_t skip1, skip2;

	REQUIRE(VALID_LFSR(lfsr1));
	REQUIRE(VALID_LFSR(lfsr2));

	skip1 = lfsr1->state & 0x01;
	skip2 = lfsr2->state & 0x01;

	/* cross-skip. */
	state1 = lfsr_skipgenerate(lfsr1, skip2);
	state2 = lfsr_skipgenerate(lfsr2, skip1);

	return (state1 ^ state2);
}
