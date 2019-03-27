/*
 * Copyright (C) 2004, 2005, 2007, 2008, 2011, 2012  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 2000-2002  Internet Software Consortium.
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

/* $Id: mutex.c,v 1.18 2011/01/04 23:47:14 tbox Exp $ */

/*! \file */

#include <config.h>

#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include <errno.h>

#include <isc/mutex.h>
#include <isc/util.h>
#include <isc/strerror.h>

#if HAVE_PTHREADS < 5		/* HP-UX 10.20 has 4, needs this */
# define pthread_mutex_init(m, a)					\
	 pthread_mutex_init(m, (a)					\
				? *(const pthread_mutexattr_t *)(a)	\
				: pthread_mutexattr_default)
# define PTHREAD_MUTEX_RECURSIVE	MUTEX_RECURSIVE_NP
# define pthread_mutexattr_settype	pthread_mutexattr_setkind_np
#endif

#if ISC_MUTEX_PROFILE

/*@{*/
/*% Operations on timevals; adapted from FreeBSD's sys/time.h */
#define timevalclear(tvp)      ((tvp)->tv_sec = (tvp)->tv_usec = 0)
#define timevaladd(vvp, uvp)                                            \
	do {                                                            \
		(vvp)->tv_sec += (uvp)->tv_sec;                         \
		(vvp)->tv_usec += (uvp)->tv_usec;                       \
		if ((vvp)->tv_usec >= 1000000) {                        \
			(vvp)->tv_sec++;                                \
			(vvp)->tv_usec -= 1000000;                      \
		}                                                       \
	} while (0)
#define timevalsub(vvp, uvp)                                            \
	do {                                                            \
		(vvp)->tv_sec -= (uvp)->tv_sec;                         \
		(vvp)->tv_usec -= (uvp)->tv_usec;                       \
		if ((vvp)->tv_usec < 0) {                               \
			(vvp)->tv_sec--;                                \
			(vvp)->tv_usec += 1000000;                      \
		}                                                       \
	} while (0)

/*@}*/

#define ISC_MUTEX_MAX_LOCKERS 32

typedef struct {
	const char *		file;
	int			line;
	unsigned		count;
	struct timeval		locked_total;
	struct timeval		wait_total;
} isc_mutexlocker_t;

struct isc_mutexstats {
	const char *		file;	/*%< File mutex was created in. */
	int 			line;	/*%< Line mutex was created on. */
	unsigned		count;
	struct timeval		lock_t;
	struct timeval		locked_total;
	struct timeval		wait_total;
	isc_mutexlocker_t *	cur_locker;
	isc_mutexlocker_t	lockers[ISC_MUTEX_MAX_LOCKERS];
};

#ifndef ISC_MUTEX_PROFTABLESIZE
#define ISC_MUTEX_PROFTABLESIZE (1024 * 1024)
#endif
static isc_mutexstats_t stats[ISC_MUTEX_PROFTABLESIZE];
static int stats_next = 0;
static isc_boolean_t stats_init = ISC_FALSE;
static pthread_mutex_t statslock = PTHREAD_MUTEX_INITIALIZER;


isc_result_t
isc_mutex_init_profile(isc_mutex_t *mp, const char *file, int line) {
	int i, err;

	err = pthread_mutex_init(&mp->mutex, NULL);
	if (err == ENOMEM)
		return (ISC_R_NOMEMORY);
	if (err != 0)
		return (ISC_R_UNEXPECTED);

	RUNTIME_CHECK(pthread_mutex_lock(&statslock) == 0);

	if (stats_init == ISC_FALSE)
		stats_init = ISC_TRUE;

	/*
	 * If all statistics entries have been used, give up and trigger an
	 * assertion failure.  There would be no other way to deal with this
	 * because we'd like to keep record of all locks for the purpose of
	 * debugging and the number of necessary locks is unpredictable.
	 * If this failure is triggered while debugging, named should be
	 * rebuilt with an increased ISC_MUTEX_PROFTABLESIZE.
	 */
	RUNTIME_CHECK(stats_next < ISC_MUTEX_PROFTABLESIZE);
	mp->stats = &stats[stats_next++];

	RUNTIME_CHECK(pthread_mutex_unlock(&statslock) == 0);

	mp->stats->file = file;
	mp->stats->line = line;
	mp->stats->count = 0;
	timevalclear(&mp->stats->locked_total);
	timevalclear(&mp->stats->wait_total);
	for (i = 0; i < ISC_MUTEX_MAX_LOCKERS; i++) {
		mp->stats->lockers[i].file = NULL;
		mp->stats->lockers[i].line = 0;
		mp->stats->lockers[i].count = 0;
		timevalclear(&mp->stats->lockers[i].locked_total);
		timevalclear(&mp->stats->lockers[i].wait_total);
	}

	return (ISC_R_SUCCESS);
}

isc_result_t
isc_mutex_lock_profile(isc_mutex_t *mp, const char *file, int line) {
	struct timeval prelock_t;
	struct timeval postlock_t;
	isc_mutexlocker_t *locker = NULL;
	int i;

	gettimeofday(&prelock_t, NULL);

	if (pthread_mutex_lock(&mp->mutex) != 0)
		return (ISC_R_UNEXPECTED);

	gettimeofday(&postlock_t, NULL);
	mp->stats->lock_t = postlock_t;

	timevalsub(&postlock_t, &prelock_t);

	mp->stats->count++;
	timevaladd(&mp->stats->wait_total, &postlock_t);

	for (i = 0; i < ISC_MUTEX_MAX_LOCKERS; i++) {
		if (mp->stats->lockers[i].file == NULL) {
			locker = &mp->stats->lockers[i];
			locker->file = file;
			locker->line = line;
			break;
		} else if (mp->stats->lockers[i].file == file &&
			   mp->stats->lockers[i].line == line) {
			locker = &mp->stats->lockers[i];
			break;
		}
	}

	if (locker != NULL) {
		locker->count++;
		timevaladd(&locker->wait_total, &postlock_t);
	}

	mp->stats->cur_locker = locker;

	return (ISC_R_SUCCESS);
}

isc_result_t
isc_mutex_unlock_profile(isc_mutex_t *mp, const char *file, int line) {
	struct timeval unlock_t;

	UNUSED(file);
	UNUSED(line);

	if (mp->stats->cur_locker != NULL) {
		gettimeofday(&unlock_t, NULL);
		timevalsub(&unlock_t, &mp->stats->lock_t);
		timevaladd(&mp->stats->locked_total, &unlock_t);
		timevaladd(&mp->stats->cur_locker->locked_total, &unlock_t);
		mp->stats->cur_locker = NULL;
	}

	return ((pthread_mutex_unlock((&mp->mutex)) == 0) ? \
		ISC_R_SUCCESS : ISC_R_UNEXPECTED);
}


void
isc_mutex_statsprofile(FILE *fp) {
	isc_mutexlocker_t *locker;
	int i, j;

	fprintf(fp, "Mutex stats (in us)\n");
	for (i = 0; i < stats_next; i++) {
		fprintf(fp, "%-12s %4d: %10u  %lu.%06lu %lu.%06lu %5d\n",
			stats[i].file, stats[i].line, stats[i].count,
			stats[i].locked_total.tv_sec,
			stats[i].locked_total.tv_usec,
			stats[i].wait_total.tv_sec,
			stats[i].wait_total.tv_usec,
			i);
		for (j = 0; j < ISC_MUTEX_MAX_LOCKERS; j++) {
			locker = &stats[i].lockers[j];
			if (locker->file == NULL)
				continue;
			fprintf(fp, " %-11s %4d: %10u  %lu.%06lu %lu.%06lu %5d\n",
				locker->file, locker->line, locker->count,
				locker->locked_total.tv_sec,
				locker->locked_total.tv_usec,
				locker->wait_total.tv_sec,
				locker->wait_total.tv_usec,
				i);
		}
	}
}

#endif /* ISC_MUTEX_PROFILE */

#if ISC_MUTEX_DEBUG && defined(PTHREAD_MUTEX_ERRORCHECK)
isc_result_t
isc_mutex_init_errcheck(isc_mutex_t *mp)
{
	pthread_mutexattr_t attr;
	int err;

	if (pthread_mutexattr_init(&attr) != 0)
		return (ISC_R_UNEXPECTED);

	if (pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK) != 0) {
		pthread_mutexattr_destroy(&attr);
		return (ISC_R_UNEXPECTED);
	}

	err = pthread_mutex_init(mp, &attr) != 0)
	pthread_mutexattr_destroy(&attr);
	if (err == ENOMEM)
		return (ISC_R_NOMEMORY);
	return ((err == 0) ? ISC_R_SUCCESS : ISC_R_UNEXPECTED);
}
#endif

#if ISC_MUTEX_DEBUG && defined(__NetBSD__) && defined(PTHREAD_MUTEX_ERRORCHECK)
pthread_mutexattr_t isc__mutex_attrs = {
	PTHREAD_MUTEX_ERRORCHECK,	/* m_type */
	0				/* m_flags, which appears to be unused. */
};
#endif

#if !(ISC_MUTEX_DEBUG && defined(PTHREAD_MUTEX_ERRORCHECK)) && !ISC_MUTEX_PROFILE
isc_result_t
isc__mutex_init(isc_mutex_t *mp, const char *file, unsigned int line) {
	char strbuf[ISC_STRERRORSIZE];
	isc_result_t result = ISC_R_SUCCESS;
	int err;

	err = pthread_mutex_init(mp, ISC__MUTEX_ATTRS);
	if (err == ENOMEM)
		return (ISC_R_NOMEMORY);
	if (err != 0) {
		isc__strerror(errno, strbuf, sizeof(strbuf));
		UNEXPECTED_ERROR(file, line, "isc_mutex_init() failed: %s",
				 strbuf);
		result = ISC_R_UNEXPECTED;
	}
	return (result);
}
#endif
