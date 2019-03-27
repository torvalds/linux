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

/* $Id: lfsr.h,v 1.17 2007/06/19 23:47:18 tbox Exp $ */

#ifndef ISC_LFSR_H
#define ISC_LFSR_H 1

/*! \file isc/lfsr.h */

#include <isc/lang.h>
#include <isc/types.h>

typedef struct isc_lfsr isc_lfsr_t;

/*%
 * This function is called when reseeding is needed.  It is allowed to
 * modify any state in the LFSR in any way it sees fit OTHER THAN "bits".
 *
 * It MUST set "count" to a new value or the lfsr will never reseed again.
 *
 * Also, a reseed will never occur in the middle of an extraction.  This
 * is purely an optimization, and is probably what one would want.
 */
typedef void (*isc_lfsrreseed_t)(isc_lfsr_t *, void *);

/*%
 * The members of this structure can be used by the application, but care
 * needs to be taken to not change state once the lfsr is in operation.
 */
struct isc_lfsr {
	isc_uint32_t		state;	/*%< previous state */
	unsigned int		bits;	/*%< length */
	isc_uint32_t		tap;	/*%< bit taps */
	unsigned int		count;	/*%< reseed count (in BITS!) */
	isc_lfsrreseed_t	reseed;	/*%< reseed function */
	void		       *arg;	/*%< reseed function argument */
};

ISC_LANG_BEGINDECLS


void 
isc_lfsr_init(isc_lfsr_t *lfsr, isc_uint32_t state, unsigned int bits,
		   isc_uint32_t tap, unsigned int count,
		   isc_lfsrreseed_t reseed, void *arg);
/*%<
 * Initialize an LFSR.
 *
 * Note:
 *
 *\li	Putting untrusted values into this function will cause the LFSR to
 *	generate (perhaps) non-maximal length sequences.
 *
 * Requires:
 *
 *\li	lfsr != NULL
 *
 *\li	8 <= bits <= 32
 *
 *\li	tap != 0
 */

void 
isc_lfsr_generate(isc_lfsr_t *lfsr, void *data, unsigned int count);
/*%<
 * Returns "count" bytes of data from the LFSR.
 *
 * Requires:
 *
 *\li	lfsr be valid.
 *
 *\li	data != NULL.
 *
 *\li	count > 0.
 */

void 
isc_lfsr_skip(isc_lfsr_t *lfsr, unsigned int skip);
/*%<
 * Skip "skip" states.
 *
 * Requires:
 *
 *\li	lfsr be valid.
 */

isc_uint32_t 
isc_lfsr_generate32(isc_lfsr_t *lfsr1, isc_lfsr_t *lfsr2);
/*%<
 * Given two LFSRs, use the current state from each to skip entries in the
 * other.  The next states are then xor'd together and returned.
 *
 * WARNING:
 *
 *\li	This function is used only for very, very low security data, such
 *	as DNS message IDs where it is desired to have an unpredictable
 *	stream of bytes that are harder to predict than a simple flooding
 *	attack.
 *
 * Notes:
 *
 *\li	Since the current state from each of the LFSRs is used to skip
 *	state in the other, it is important that no state be leaked
 *	from either LFSR.
 *
 * Requires:
 *
 *\li	lfsr1 and lfsr2 be valid.
 *
 *\li	1 <= skipbits <= 31
 */

ISC_LANG_ENDDECLS

#endif /* ISC_LFSR_H */
