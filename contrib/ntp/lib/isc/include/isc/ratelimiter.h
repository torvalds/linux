/*
 * Copyright (C) 2004-2007, 2009  Internet Systems Consortium, Inc. ("ISC")
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

/* $Id: ratelimiter.h,v 1.23 2009/01/18 23:48:14 tbox Exp $ */

#ifndef ISC_RATELIMITER_H
#define ISC_RATELIMITER_H 1

/*****
 ***** Module Info
 *****/

/*! \file isc/ratelimiter.h
 * \brief A rate limiter is a mechanism for dispatching events at a limited
 * rate.  This is intended to be used when sending zone maintenance
 * SOA queries, NOTIFY messages, etc.
 */

/***
 *** Imports.
 ***/

#include <isc/lang.h>
#include <isc/types.h>

ISC_LANG_BEGINDECLS

/*****
 ***** Functions.
 *****/

isc_result_t
isc_ratelimiter_create(isc_mem_t *mctx, isc_timermgr_t *timermgr,
		       isc_task_t *task, isc_ratelimiter_t **ratelimiterp);
/*%<
 * Create a rate limiter.  The execution interval is initially undefined.
 */

isc_result_t
isc_ratelimiter_setinterval(isc_ratelimiter_t *rl, isc_interval_t *interval);
/*!<
 * Set the minimum interval between event executions.
 * The interval value is copied, so the caller need not preserve it.
 *
 * Requires:
 *	'*interval' is a nonzero interval.
 */

void
isc_ratelimiter_setpertic(isc_ratelimiter_t *rl, isc_uint32_t perint);
/*%<
 * Set the number of events processed per interval timer tick.
 * If 'perint' is zero it is treated as 1.
 */

isc_result_t
isc_ratelimiter_enqueue(isc_ratelimiter_t *rl, isc_task_t *task,
			isc_event_t **eventp);
/*%<
 * Queue an event for rate-limited execution.
 *
 * This is similar
 * to doing an isc_task_send() to the 'task', except that the
 * execution may be delayed to achieve the desired rate of
 * execution.
 *
 * '(*eventp)->ev_sender' is used to hold the task.  The caller
 * must ensure that the task exists until the event is delivered.
 *
 * Requires:
 *\li	An interval has been set by calling
 *	isc_ratelimiter_setinterval().
 *
 *\li	'task' to be non NULL.
 *\li	'(*eventp)->ev_sender' to be NULL.
 */

void
isc_ratelimiter_shutdown(isc_ratelimiter_t *ratelimiter);
/*%<
 * Shut down a rate limiter.
 *
 * Ensures:
 *\li	All events that have not yet been
 * 	dispatched to the task are dispatched immediately with
 *	the #ISC_EVENTATTR_CANCELED bit set in ev_attributes.
 *
 *\li	Further attempts to enqueue events will fail with
 * 	#ISC_R_SHUTTINGDOWN.
 *
 *\li	The rate limiter is no longer attached to its task.
 */

void
isc_ratelimiter_attach(isc_ratelimiter_t *source, isc_ratelimiter_t **target);
/*%<
 * Attach to a rate limiter.
 */

void
isc_ratelimiter_detach(isc_ratelimiter_t **ratelimiterp);
/*%<
 * Detach from a rate limiter.
 */

isc_result_t
isc_ratelimiter_stall(isc_ratelimiter_t *rl);
/*%<
 * Stall event processing.
 */

isc_result_t
isc_ratelimiter_release(isc_ratelimiter_t *rl);
/*%<
 * Release a stalled rate limiter.
 */

ISC_LANG_ENDDECLS

#endif /* ISC_RATELIMITER_H */
