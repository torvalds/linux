/*
 * Copyright (C) 2004-2008, 2011, 2012  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 1998-2001, 2003  Internet Software Consortium.
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

/*! \file */

#include <config.h>

#include <errno.h>
#include <limits.h>
#include <syslog.h>
#include <time.h>

#include <sys/time.h>	/* Required for struct timeval on some platforms. */

#include <isc/log.h>
#include <isc/print.h>
#include <isc/strerror.h>
#include <isc/string.h>
#include <isc/time.h>
#include <isc/util.h>

#define NS_PER_S	1000000000	/*%< Nanoseconds per second. */
#define NS_PER_US	1000		/*%< Nanoseconds per microsecond. */
#define US_PER_S	1000000		/*%< Microseconds per second. */

/*
 * All of the INSIST()s checks of nanoseconds < NS_PER_S are for
 * consistency checking of the type. In lieu of magic numbers, it
 * is the best we've got.  The check is only performed on functions which
 * need an initialized type.
 */

#ifndef ISC_FIX_TV_USEC
#define ISC_FIX_TV_USEC 1
#endif

/*%
 *** Intervals
 ***/

static isc_interval_t zero_interval = { 0, 0 };
isc_interval_t *isc_interval_zero = &zero_interval;

#if ISC_FIX_TV_USEC
static inline void
fix_tv_usec(struct timeval *tv) {
	isc_boolean_t fixed = ISC_FALSE;

	if (tv->tv_usec < 0) {
		fixed = ISC_TRUE;
		do {
			tv->tv_sec -= 1;
			tv->tv_usec += US_PER_S;
		} while (tv->tv_usec < 0);
	} else if (tv->tv_usec >= US_PER_S) {
		fixed = ISC_TRUE;
		do {
			tv->tv_sec += 1;
			tv->tv_usec -= US_PER_S;
		} while (tv->tv_usec >=US_PER_S);
	}
	/*
	 * Call syslog directly as was are called from the logging functions.
	 */
	if (fixed)
		(void)syslog(LOG_ERR, "gettimeofday returned bad tv_usec: corrected");
}
#endif

void
isc_interval_set(isc_interval_t *i,
		 unsigned int seconds, unsigned int nanoseconds)
{
	REQUIRE(i != NULL);
	REQUIRE(nanoseconds < NS_PER_S);

	i->seconds = seconds;
	i->nanoseconds = nanoseconds;
}

isc_boolean_t
isc_interval_iszero(const isc_interval_t *i) {
	REQUIRE(i != NULL);
	INSIST(i->nanoseconds < NS_PER_S);

	if (i->seconds == 0 && i->nanoseconds == 0)
		return (ISC_TRUE);

	return (ISC_FALSE);
}


/***
 *** Absolute Times
 ***/

static isc_time_t epoch = { 0, 0 };
isc_time_t *isc_time_epoch = &epoch;

void
isc_time_set(isc_time_t *t, unsigned int seconds, unsigned int nanoseconds) {
	REQUIRE(t != NULL);
	REQUIRE(nanoseconds < NS_PER_S);

	t->seconds = seconds;
	t->nanoseconds = nanoseconds;
}

void
isc_time_settoepoch(isc_time_t *t) {
	REQUIRE(t != NULL);

	t->seconds = 0;
	t->nanoseconds = 0;
}

isc_boolean_t
isc_time_isepoch(const isc_time_t *t) {
	REQUIRE(t != NULL);
	INSIST(t->nanoseconds < NS_PER_S);

	if (t->seconds == 0 && t->nanoseconds == 0)
		return (ISC_TRUE);

	return (ISC_FALSE);
}


isc_result_t
isc_time_now(isc_time_t *t) {
	struct timeval tv;
	char strbuf[ISC_STRERRORSIZE];

	REQUIRE(t != NULL);

	if (gettimeofday(&tv, NULL) == -1) {
		isc__strerror(errno, strbuf, sizeof(strbuf));
		UNEXPECTED_ERROR(__FILE__, __LINE__, "%s", strbuf);
		return (ISC_R_UNEXPECTED);
	}

	/*
	 * Does POSIX guarantee the signedness of tv_sec and tv_usec?  If not,
	 * then this test will generate warnings for platforms on which it is
	 * unsigned.  In any event, the chances of any of these problems
	 * happening are pretty much zero, but since the libisc library ensures
	 * certain things to be true ...
	 */
#if ISC_FIX_TV_USEC
	fix_tv_usec(&tv);
	if (tv.tv_sec < 0)
		return (ISC_R_UNEXPECTED);
#else
	if (tv.tv_sec < 0 || tv.tv_usec < 0 || tv.tv_usec >= US_PER_S)
		return (ISC_R_UNEXPECTED);
#endif

	/*
	 * Ensure the tv_sec value fits in t->seconds.
	 */
	if (sizeof(tv.tv_sec) > sizeof(t->seconds) &&
	    ((tv.tv_sec | (unsigned int)-1) ^ (unsigned int)-1) != 0U)
		return (ISC_R_RANGE);

	t->seconds = tv.tv_sec;
	t->nanoseconds = tv.tv_usec * NS_PER_US;

	return (ISC_R_SUCCESS);
}

isc_result_t
isc_time_nowplusinterval(isc_time_t *t, const isc_interval_t *i) {
	struct timeval tv;
	char strbuf[ISC_STRERRORSIZE];

	REQUIRE(t != NULL);
	REQUIRE(i != NULL);
	INSIST(i->nanoseconds < NS_PER_S);

	if (gettimeofday(&tv, NULL) == -1) {
		isc__strerror(errno, strbuf, sizeof(strbuf));
		UNEXPECTED_ERROR(__FILE__, __LINE__, "%s", strbuf);
		return (ISC_R_UNEXPECTED);
	}

	/*
	 * Does POSIX guarantee the signedness of tv_sec and tv_usec?  If not,
	 * then this test will generate warnings for platforms on which it is
	 * unsigned.  In any event, the chances of any of these problems
	 * happening are pretty much zero, but since the libisc library ensures
	 * certain things to be true ...
	 */
#if ISC_FIX_TV_USEC
	fix_tv_usec(&tv);
	if (tv.tv_sec < 0)
		return (ISC_R_UNEXPECTED);
#else
	if (tv.tv_sec < 0 || tv.tv_usec < 0 || tv.tv_usec >= US_PER_S)
		return (ISC_R_UNEXPECTED);
#endif

	/*
	 * Ensure the resulting seconds value fits in the size of an
	 * unsigned int.  (It is written this way as a slight optimization;
	 * note that even if both values == INT_MAX, then when added
	 * and getting another 1 added below the result is UINT_MAX.)
	 */
	if ((tv.tv_sec > INT_MAX || i->seconds > INT_MAX) &&
	    ((long long)tv.tv_sec + i->seconds > UINT_MAX))
		return (ISC_R_RANGE);

	t->seconds = tv.tv_sec + i->seconds;
	t->nanoseconds = tv.tv_usec * NS_PER_US + i->nanoseconds;
	if (t->nanoseconds >= NS_PER_S) {
		t->seconds++;
		t->nanoseconds -= NS_PER_S;
	}

	return (ISC_R_SUCCESS);
}

int
isc_time_compare(const isc_time_t *t1, const isc_time_t *t2) {
	REQUIRE(t1 != NULL && t2 != NULL);
	INSIST(t1->nanoseconds < NS_PER_S && t2->nanoseconds < NS_PER_S);

	if (t1->seconds < t2->seconds)
		return (-1);
	if (t1->seconds > t2->seconds)
		return (1);
	if (t1->nanoseconds < t2->nanoseconds)
		return (-1);
	if (t1->nanoseconds > t2->nanoseconds)
		return (1);
	return (0);
}

isc_result_t
isc_time_add(const isc_time_t *t, const isc_interval_t *i, isc_time_t *result)
{
	REQUIRE(t != NULL && i != NULL && result != NULL);
	INSIST(t->nanoseconds < NS_PER_S && i->nanoseconds < NS_PER_S);

	/*
	 * Ensure the resulting seconds value fits in the size of an
	 * unsigned int.  (It is written this way as a slight optimization;
	 * note that even if both values == INT_MAX, then when added
	 * and getting another 1 added below the result is UINT_MAX.)
	 */
	if ((t->seconds > INT_MAX || i->seconds > INT_MAX) &&
	    ((long long)t->seconds + i->seconds > UINT_MAX))
		return (ISC_R_RANGE);

	result->seconds = t->seconds + i->seconds;
	result->nanoseconds = t->nanoseconds + i->nanoseconds;
	if (result->nanoseconds >= NS_PER_S) {
		result->seconds++;
		result->nanoseconds -= NS_PER_S;
	}

	return (ISC_R_SUCCESS);
}

isc_result_t
isc_time_subtract(const isc_time_t *t, const isc_interval_t *i,
		  isc_time_t *result)
{
	REQUIRE(t != NULL && i != NULL && result != NULL);
	INSIST(t->nanoseconds < NS_PER_S && i->nanoseconds < NS_PER_S);

	if ((unsigned int)t->seconds < i->seconds ||
	    ((unsigned int)t->seconds == i->seconds &&
	     t->nanoseconds < i->nanoseconds))
	    return (ISC_R_RANGE);

	result->seconds = t->seconds - i->seconds;
	if (t->nanoseconds >= i->nanoseconds)
		result->nanoseconds = t->nanoseconds - i->nanoseconds;
	else {
		result->nanoseconds = NS_PER_S - i->nanoseconds +
			t->nanoseconds;
		result->seconds--;
	}

	return (ISC_R_SUCCESS);
}

isc_uint64_t
isc_time_microdiff(const isc_time_t *t1, const isc_time_t *t2) {
	isc_uint64_t i1, i2, i3;

	REQUIRE(t1 != NULL && t2 != NULL);
	INSIST(t1->nanoseconds < NS_PER_S && t2->nanoseconds < NS_PER_S);

	i1 = (isc_uint64_t)t1->seconds * NS_PER_S + t1->nanoseconds;
	i2 = (isc_uint64_t)t2->seconds * NS_PER_S + t2->nanoseconds;

	if (i1 <= i2)
		return (0);

	i3 = i1 - i2;

	/*
	 * Convert to microseconds.
	 */
	i3 /= NS_PER_US;

	return (i3);
}

isc_uint32_t
isc_time_seconds(const isc_time_t *t) {
	REQUIRE(t != NULL);
	INSIST(t->nanoseconds < NS_PER_S);

	return ((isc_uint32_t)t->seconds);
}

isc_result_t
isc_time_secondsastimet(const isc_time_t *t, time_t *secondsp) {
	time_t seconds;

	REQUIRE(t != NULL);
	INSIST(t->nanoseconds < NS_PER_S);

	/*
	 * Ensure that the number of seconds represented by t->seconds
	 * can be represented by a time_t.  Since t->seconds is an unsigned
	 * int and since time_t is mostly opaque, this is trickier than
	 * it seems.  (This standardized opaqueness of time_t is *very*
	 * frustrating; time_t is not even limited to being an integral
	 * type.)
	 *
	 * The mission, then, is to avoid generating any kind of warning
	 * about "signed versus unsigned" while trying to determine if the
	 * the unsigned int t->seconds is out range for tv_sec, which is
	 * pretty much only true if time_t is a signed integer of the same
	 * size as the return value of isc_time_seconds.
	 *
	 * If the paradox in the if clause below is true, t->seconds is out
	 * of range for time_t.
	 */
	seconds = (time_t)t->seconds;

	INSIST(sizeof(unsigned int) == sizeof(isc_uint32_t));
	INSIST(sizeof(time_t) >= sizeof(isc_uint32_t));

	if (t->seconds > (~0U>>1) && seconds <= (time_t)(~0U>>1))
		return (ISC_R_RANGE);

	*secondsp = seconds;

	return (ISC_R_SUCCESS);
}

isc_uint32_t
isc_time_nanoseconds(const isc_time_t *t) {
	REQUIRE(t != NULL);

	ENSURE(t->nanoseconds < NS_PER_S);

	return ((isc_uint32_t)t->nanoseconds);
}

void
isc_time_formattimestamp(const isc_time_t *t, char *buf, unsigned int len) {
	time_t now;
	unsigned int flen;

	REQUIRE(len > 0);

	now = (time_t) t->seconds;
	flen = strftime(buf, len, "%d-%b-%Y %X", localtime(&now));
	INSIST(flen < len);
	if (flen != 0)
		snprintf(buf + flen, len - flen,
			 ".%03u", t->nanoseconds / 1000000);
	else
		snprintf(buf, len, "99-Bad-9999 99:99:99.999");
}

void
isc_time_formathttptimestamp(const isc_time_t *t, char *buf, unsigned int len) {
	time_t now;
	unsigned int flen;

	REQUIRE(len > 0);

	now = (time_t)t->seconds;
	flen = strftime(buf, len, "%a, %d %b %Y %H:%M:%S GMT", gmtime(&now));
	INSIST(flen < len);
}

void
isc_time_formatISO8601(const isc_time_t *t, char *buf, unsigned int len) {
	time_t now;
	unsigned int flen;

	REQUIRE(len > 0);

	now = (time_t)t->seconds;
	flen = strftime(buf, len, "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
	INSIST(flen < len);
}
