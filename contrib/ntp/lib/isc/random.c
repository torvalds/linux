/*
 * Copyright (C) 2004, 2005, 2007, 2009  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 1999-2003  Internet Software Consortium.
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

/* $Id: random.c,v 1.28 2009/07/16 05:52:46 marka Exp $ */

/*! \file */

#include <config.h>

#include <stdlib.h>
#include <time.h>		/* Required for time(). */
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <isc/mutex.h>
#include <isc/once.h>
#include <isc/random.h>
#include <isc/string.h>
#include <isc/util.h>

static isc_once_t once = ISC_ONCE_INIT;

static void
initialize_rand(void)
{
#ifndef HAVE_ARC4RANDOM
	unsigned int pid = getpid();

	/*
	 * The low bits of pid generally change faster.
	 * Xor them with the high bits of time which change slowly.
	 */
	pid = ((pid << 16) & 0xffff0000) | ((pid >> 16) & 0xffff);

	srand(time(NULL) ^ pid);
#endif
}

static void
initialize(void)
{
	RUNTIME_CHECK(isc_once_do(&once, initialize_rand) == ISC_R_SUCCESS);
}

void
isc_random_seed(isc_uint32_t seed)
{
	initialize();

#ifndef HAVE_ARC4RANDOM
	srand(seed);
#endif
}

void
isc_random_get(isc_uint32_t *val)
{
	REQUIRE(val != NULL);

	initialize();

#ifndef HAVE_ARC4RANDOM
	/*
	 * rand()'s lower bits are not random.
	 * rand()'s upper bit is zero.
	 */
#if RAND_MAX >= 0xfffff
	/* We have at least 20 bits.  Use lower 16 excluding lower most 4 */
	*val = ((rand() >> 4) & 0xffff) | ((rand() << 12) & 0xffff0000);
#elif RAND_MAX >= 0x7fff
	/* We have at least 15 bits.  Use lower 10/11 excluding lower most 4 */
	*val = ((rand() >> 4) & 0x000007ff) | ((rand() << 7) & 0x003ff800) |
		((rand() << 18) & 0xffc00000);
#else
#error RAND_MAX is too small
#endif
#else
	*val = arc4random();
#endif
}

isc_uint32_t
isc_random_jitter(isc_uint32_t max, isc_uint32_t jitter) {
	isc_uint32_t rnd;

	REQUIRE(jitter < max || (jitter == 0 && max == 0));

	if (jitter == 0)
		return (max);

	isc_random_get(&rnd);
	return (max - rnd % jitter);
}
