/*
 * Copyright (C) 2004, 2005, 2007  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 1998-2002  Internet Software Consortium.
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

/* $Id: mutex.h,v 1.30 2007/06/19 23:47:18 tbox Exp $ */

#ifndef ISC_MUTEX_H
#define ISC_MUTEX_H 1

/*! \file */

#include <pthread.h>
#include <stdio.h>

#include <isc/lang.h>
#include <isc/result.h>		/* for ISC_R_ codes */

ISC_LANG_BEGINDECLS

/*!
 * Supply mutex attributes that enable deadlock detection
 * (helpful when debugging).  This is system dependent and
 * currently only supported on NetBSD.
 */
#if ISC_MUTEX_DEBUG && defined(__NetBSD__) && defined(PTHREAD_MUTEX_ERRORCHECK)
extern pthread_mutexattr_t isc__mutex_attrs;
#define ISC__MUTEX_ATTRS &isc__mutex_attrs
#else
#define ISC__MUTEX_ATTRS NULL
#endif

/* XXX We could do fancier error handling... */

/*!
 * Define ISC_MUTEX_PROFILE to turn on profiling of mutexes by line.  When
 * enabled, isc_mutex_stats() can be used to print a table showing the
 * number of times each type of mutex was locked and the amount of time
 * waiting to obtain the lock.
 */
#ifndef ISC_MUTEX_PROFILE
#define ISC_MUTEX_PROFILE 0
#endif

#if ISC_MUTEX_PROFILE
typedef struct isc_mutexstats isc_mutexstats_t;

typedef struct {
	pthread_mutex_t		mutex;	/*%< The actual mutex. */
	isc_mutexstats_t *	stats;	/*%< Mutex statistics. */
} isc_mutex_t;
#else
typedef pthread_mutex_t	isc_mutex_t;
#endif


#if ISC_MUTEX_PROFILE
#define isc_mutex_init(mp) \
	isc_mutex_init_profile((mp), __FILE__, __LINE__)
#else
#if ISC_MUTEX_DEBUG && defined(PTHREAD_MUTEX_ERRORCHECK)
#define isc_mutex_init(mp) \
        isc_mutex_init_errcheck((mp))
#else
#define isc_mutex_init(mp) \
	isc__mutex_init((mp), __FILE__, __LINE__)
isc_result_t isc__mutex_init(isc_mutex_t *mp, const char *file, unsigned int line);
#endif
#endif

#if ISC_MUTEX_PROFILE
#define isc_mutex_lock(mp) \
	isc_mutex_lock_profile((mp), __FILE__, __LINE__)
#else
#define isc_mutex_lock(mp) \
	((pthread_mutex_lock((mp)) == 0) ? \
	 ISC_R_SUCCESS : ISC_R_UNEXPECTED)
#endif

#if ISC_MUTEX_PROFILE
#define isc_mutex_unlock(mp) \
	isc_mutex_unlock_profile((mp), __FILE__, __LINE__)
#else
#define isc_mutex_unlock(mp) \
	((pthread_mutex_unlock((mp)) == 0) ? \
	 ISC_R_SUCCESS : ISC_R_UNEXPECTED)
#endif

#if ISC_MUTEX_PROFILE
#define isc_mutex_trylock(mp) \
	((pthread_mutex_trylock((&(mp)->mutex)) == 0) ? \
	 ISC_R_SUCCESS : ISC_R_LOCKBUSY)
#else
#define isc_mutex_trylock(mp) \
	((pthread_mutex_trylock((mp)) == 0) ? \
	 ISC_R_SUCCESS : ISC_R_LOCKBUSY)
#endif

#if ISC_MUTEX_PROFILE
#define isc_mutex_destroy(mp) \
	((pthread_mutex_destroy((&(mp)->mutex)) == 0) ? \
	 ISC_R_SUCCESS : ISC_R_UNEXPECTED)
#else
#define isc_mutex_destroy(mp) \
	((pthread_mutex_destroy((mp)) == 0) ? \
	 ISC_R_SUCCESS : ISC_R_UNEXPECTED)
#endif

#if ISC_MUTEX_PROFILE
#define isc_mutex_stats(fp) isc_mutex_statsprofile(fp);
#else
#define isc_mutex_stats(fp)
#endif

#if ISC_MUTEX_PROFILE

isc_result_t
isc_mutex_init_profile(isc_mutex_t *mp, const char * _file, int _line);
isc_result_t
isc_mutex_lock_profile(isc_mutex_t *mp, const char * _file, int _line);
isc_result_t
isc_mutex_unlock_profile(isc_mutex_t *mp, const char * _file, int _line);

void
isc_mutex_statsprofile(FILE *fp);

isc_result_t
isc_mutex_init_errcheck(isc_mutex_t *mp);

#endif /* ISC_MUTEX_PROFILE */

ISC_LANG_ENDDECLS
#endif /* ISC_MUTEX_H */
