/*
 * Copyright (C) 2009, 2012  Internet Systems Consortium, Inc. ("ISC")
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

/* $Id$ */

#ifndef ISC_STATS_H
#define ISC_STATS_H 1

/*! \file isc/stats.h */

#include <isc/types.h>

ISC_LANG_BEGINDECLS

/*%<
 * Flag(s) for isc_stats_dump().
 */
#define ISC_STATSDUMP_VERBOSE	0x00000001 /*%< dump 0-value counters */

/*%<
 * Dump callback type.
 */
typedef void (*isc_stats_dumper_t)(isc_statscounter_t, isc_uint64_t, void *);

isc_result_t
isc_stats_create(isc_mem_t *mctx, isc_stats_t **statsp, int ncounters);
/*%<
 * Create a statistics counter structure of general type.  It counts a general
 * set of counters indexed by an ID between 0 and ncounters -1.
 *
 * Requires:
 *\li	'mctx' must be a valid memory context.
 *
 *\li	'statsp' != NULL && '*statsp' == NULL.
 *
 * Returns:
 *\li	ISC_R_SUCCESS	-- all ok
 *
 *\li	anything else	-- failure
 */

void
isc_stats_attach(isc_stats_t *stats, isc_stats_t **statsp);
/*%<
 * Attach to a statistics set.
 *
 * Requires:
 *\li	'stats' is a valid isc_stats_t.
 *
 *\li	'statsp' != NULL && '*statsp' == NULL
 */

void
isc_stats_detach(isc_stats_t **statsp);
/*%<
 * Detaches from the statistics set.
 *
 * Requires:
 *\li	'statsp' != NULL and '*statsp' is a valid isc_stats_t.
 */

int
isc_stats_ncounters(isc_stats_t *stats);
/*%<
 * Returns the number of counters contained in stats.
 *
 * Requires:
 *\li	'stats' is a valid isc_stats_t.
 *
 */

void
isc_stats_increment(isc_stats_t *stats, isc_statscounter_t counter);
/*%<
 * Increment the counter-th counter of stats.
 *
 * Requires:
 *\li	'stats' is a valid isc_stats_t.
 *
 *\li	counter is less than the maximum available ID for the stats specified
 *	on creation.
 */

void
isc_stats_decrement(isc_stats_t *stats, isc_statscounter_t counter);
/*%<
 * Decrement the counter-th counter of stats.
 *
 * Requires:
 *\li	'stats' is a valid isc_stats_t.
 */

void
isc_stats_dump(isc_stats_t *stats, isc_stats_dumper_t dump_fn, void *arg,
	       unsigned int options);
/*%<
 * Dump the current statistics counters in a specified way.  For each counter
 * in stats, dump_fn is called with its current value and the given argument
 * arg.  By default counters that have a value of 0 is skipped; if options has
 * the ISC_STATSDUMP_VERBOSE flag, even such counters are dumped.
 *
 * Requires:
 *\li	'stats' is a valid isc_stats_t.
 */

ISC_LANG_ENDDECLS

#endif /* ISC_STATS_H */
