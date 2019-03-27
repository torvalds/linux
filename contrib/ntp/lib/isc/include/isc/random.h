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

/* $Id: random.h,v 1.20 2009/01/17 23:47:43 tbox Exp $ */

#ifndef ISC_RANDOM_H
#define ISC_RANDOM_H 1

#include <isc/lang.h>
#include <isc/types.h>

/*! \file isc/random.h
 * \brief Implements a random state pool which will let the caller return a
 * series of possibly non-reproducible random values.
 *
 * Note that the
 * strength of these numbers is not all that high, and should not be
 * used in cryptography functions.  It is useful for jittering values
 * a bit here and there, such as timeouts, etc.
 */

ISC_LANG_BEGINDECLS

void
isc_random_seed(isc_uint32_t seed);
/*%<
 * Set the initial seed of the random state.
 */

void
isc_random_get(isc_uint32_t *val);
/*%<
 * Get a random value.
 *
 * Requires:
 *	val != NULL.
 */

isc_uint32_t
isc_random_jitter(isc_uint32_t max, isc_uint32_t jitter);
/*%<
 * Get a random value between (max - jitter) and (max).
 * This is useful for jittering timer values.
 */

ISC_LANG_ENDDECLS

#endif /* ISC_RANDOM_H */
