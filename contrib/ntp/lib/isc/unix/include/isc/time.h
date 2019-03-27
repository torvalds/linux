/*
 * Copyright (C) 2004-2009  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 1998-2001  Internet Software Consortium.
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

/* $Id: time.h,v 1.40 2009/01/05 23:47:54 tbox Exp $ */

#ifndef ISC_TIME_H
#define ISC_TIME_H 1

/*! \file */

#include <isc/lang.h>
#include <isc/types.h>

/***
 *** Intervals
 ***/

/*!
 *  \brief
 * The contents of this structure are private, and MUST NOT be accessed
 * directly by callers.
 *
 * The contents are exposed only to allow callers to avoid dynamic allocation.
 */
struct isc_interval {
	unsigned int seconds;
	unsigned int nanoseconds;
};

extern isc_interval_t *isc_interval_zero;

ISC_LANG_BEGINDECLS

void
isc_interval_set(isc_interval_t *i,
		 unsigned int seconds, unsigned int nanoseconds);
/*%<
 * Set 'i' to a value representing an interval of 'seconds' seconds and
 * 'nanoseconds' nanoseconds, suitable for use in isc_time_add() and
 * isc_time_subtract().
 *
 * Requires:
 *
 *\li	't' is a valid pointer.
 *\li	nanoseconds < 1000000000.
 */

isc_boolean_t
isc_interval_iszero(const isc_interval_t *i);
/*%<
 * Returns ISC_TRUE iff. 'i' is the zero interval.
 *
 * Requires:
 *
 *\li	'i' is a valid pointer.
 */

/***
 *** Absolute Times
 ***/

/*%
 * The contents of this structure are private, and MUST NOT be accessed
 * directly by callers.
 *
 * The contents are exposed only to allow callers to avoid dynamic allocation.
 */

struct isc_time {
	unsigned int	seconds;
	unsigned int	nanoseconds;
};

extern isc_time_t *isc_time_epoch;

void
isc_time_set(isc_time_t *t, unsigned int seconds, unsigned int nanoseconds);
/*%<
 * Set 't' to a value which represents the given number of seconds and
 * nanoseconds since 00:00:00 January 1, 1970, UTC.
 *
 * Notes:
 *\li	The Unix version of this call is equivalent to:
 *\code
 *	isc_time_settoepoch(t);
 *	isc_interval_set(i, seconds, nanoseconds);
 *	isc_time_add(t, i, t);
 *\endcode
 *
 * Requires:
 *\li	't' is a valid pointer.
 *\li	nanoseconds < 1000000000.
 */

void
isc_time_settoepoch(isc_time_t *t);
/*%<
 * Set 't' to the time of the epoch.
 *
 * Notes:
 *\li	The date of the epoch is platform-dependent.
 *
 * Requires:
 *
 *\li	't' is a valid pointer.
 */

isc_boolean_t
isc_time_isepoch(const isc_time_t *t);
/*%<
 * Returns ISC_TRUE iff. 't' is the epoch ("time zero").
 *
 * Requires:
 *
 *\li	't' is a valid pointer.
 */

isc_result_t
isc_time_now(isc_time_t *t);
/*%<
 * Set 't' to the current absolute time.
 *
 * Requires:
 *
 *\li	't' is a valid pointer.
 *
 * Returns:
 *
 *\li	Success
 *\li	Unexpected error
 *		Getting the time from the system failed.
 *\li	Out of range
 *		The time from the system is too large to be represented
 *		in the current definition of isc_time_t.
 */

isc_result_t
isc_time_nowplusinterval(isc_time_t *t, const isc_interval_t *i);
/*%<
 * Set *t to the current absolute time + i.
 *
 * Note:
 *\li	This call is equivalent to:
 *
 *\code
 *		isc_time_now(t);
 *		isc_time_add(t, i, t);
 *\endcode
 *
 * Requires:
 *
 *\li	't' and 'i' are valid pointers.
 *
 * Returns:
 *
 *\li	Success
 *\li	Unexpected error
 *		Getting the time from the system failed.
 *\li	Out of range
 *		The interval added to the time from the system is too large to
 *		be represented in the current definition of isc_time_t.
 */

int
isc_time_compare(const isc_time_t *t1, const isc_time_t *t2);
/*%<
 * Compare the times referenced by 't1' and 't2'
 *
 * Requires:
 *
 *\li	't1' and 't2' are valid pointers.
 *
 * Returns:
 *
 *\li	-1		t1 < t2		(comparing times, not pointers)
 *\li	0		t1 = t2
 *\li	1		t1 > t2
 */

isc_result_t
isc_time_add(const isc_time_t *t, const isc_interval_t *i, isc_time_t *result);
/*%<
 * Add 'i' to 't', storing the result in 'result'.
 *
 * Requires:
 *
 *\li	't', 'i', and 'result' are valid pointers.
 *
 * Returns:
 *\li	Success
 *\li	Out of range
 * 		The interval added to the time is too large to
 *		be represented in the current definition of isc_time_t.
 */

isc_result_t
isc_time_subtract(const isc_time_t *t, const isc_interval_t *i,
		  isc_time_t *result);
/*%<
 * Subtract 'i' from 't', storing the result in 'result'.
 *
 * Requires:
 *
 *\li	't', 'i', and 'result' are valid pointers.
 *
 * Returns:
 *\li	Success
 *\li	Out of range
 *		The interval is larger than the time since the epoch.
 */

isc_uint64_t
isc_time_microdiff(const isc_time_t *t1, const isc_time_t *t2);
/*%<
 * Find the difference in microseconds between time t1 and time t2.
 * t2 is the subtrahend of t1; ie, difference = t1 - t2.
 *
 * Requires:
 *
 *\li	't1' and 't2' are valid pointers.
 *
 * Returns:
 *\li	The difference of t1 - t2, or 0 if t1 <= t2.
 */

isc_uint32_t
isc_time_seconds(const isc_time_t *t);
/*%<
 * Return the number of seconds since the epoch stored in a time structure.
 *
 * Requires:
 *
 *\li	't' is a valid pointer.
 */

isc_result_t
isc_time_secondsastimet(const isc_time_t *t, time_t *secondsp);
/*%<
 * Ensure the number of seconds in an isc_time_t is representable by a time_t.
 *
 * Notes:
 *\li	The number of seconds stored in an isc_time_t might be larger
 *	than the number of seconds a time_t is able to handle.  Since
 *	time_t is mostly opaque according to the ANSI/ISO standard
 *	(essentially, all you can be sure of is that it is an arithmetic type,
 *	not even necessarily integral), it can be tricky to ensure that
 *	the isc_time_t is in the range a time_t can handle.  Use this
 *	function in place of isc_time_seconds() any time you need to set a
 *	time_t from an isc_time_t.
 *
 * Requires:
 *\li	't' is a valid pointer.
 *
 * Returns:
 *\li	Success
 *\li	Out of range
 */

isc_uint32_t
isc_time_nanoseconds(const isc_time_t *t);
/*%<
 * Return the number of nanoseconds stored in a time structure.
 *
 * Notes:
 *\li	This is the number of nanoseconds in excess of the number
 *	of seconds since the epoch; it will always be less than one
 *	full second.
 *
 * Requires:
 *\li	't' is a valid pointer.
 *
 * Ensures:
 *\li	The returned value is less than 1*10^9.
 */

void
isc_time_formattimestamp(const isc_time_t *t, char *buf, unsigned int len);
/*%<
 * Format the time 't' into the buffer 'buf' of length 'len',
 * using a format like "30-Aug-2000 04:06:47.997" and the local time zone.
 * If the text does not fit in the buffer, the result is indeterminate,
 * but is always guaranteed to be null terminated.
 *
 *  Requires:
 *\li      'len' > 0
 *\li      'buf' points to an array of at least len chars
 *
 */

void
isc_time_formathttptimestamp(const isc_time_t *t, char *buf, unsigned int len);
/*%<
 * Format the time 't' into the buffer 'buf' of length 'len',
 * using a format like "Mon, 30 Aug 2000 04:06:47 GMT"
 * If the text does not fit in the buffer, the result is indeterminate,
 * but is always guaranteed to be null terminated.
 *
 *  Requires:
 *\li      'len' > 0
 *\li      'buf' points to an array of at least len chars
 *
 */

void
isc_time_formatISO8601(const isc_time_t *t, char *buf, unsigned int len);
/*%<
 * Format the time 't' into the buffer 'buf' of length 'len',
 * using the ISO8601 format: "yyyy-mm-ddThh:mm:ssZ"
 * If the text does not fit in the buffer, the result is indeterminate,
 * but is always guaranteed to be null terminated.
 *
 *  Requires:
 *\li      'len' > 0
 *\li      'buf' points to an array of at least len chars
 *
 */

ISC_LANG_ENDDECLS

#endif /* ISC_TIME_H */
