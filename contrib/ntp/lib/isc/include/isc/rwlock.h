/*
 * Copyright (C) 2004-2007  Internet Systems Consortium, Inc. ("ISC")
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

/* $Id: rwlock.h,v 1.28 2007/06/19 23:47:18 tbox Exp $ */

#ifndef ISC_RWLOCK_H
#define ISC_RWLOCK_H 1

/*! \file isc/rwlock.h */

#include <isc/condition.h>
#include <isc/lang.h>
#include <isc/platform.h>
#include <isc/types.h>

ISC_LANG_BEGINDECLS

typedef enum {
	isc_rwlocktype_none = 0,
	isc_rwlocktype_read,
	isc_rwlocktype_write
} isc_rwlocktype_t;

#ifdef ISC_PLATFORM_USETHREADS
#if defined(ISC_PLATFORM_HAVEXADD) && defined(ISC_PLATFORM_HAVECMPXCHG)
#define ISC_RWLOCK_USEATOMIC 1
#endif

struct isc_rwlock {
	/* Unlocked. */
	unsigned int		magic;
	isc_mutex_t		lock;

#if defined(ISC_PLATFORM_HAVEXADD) && defined(ISC_PLATFORM_HAVECMPXCHG)
	/*
	 * When some atomic instructions with hardware assistance are
	 * available, rwlock will use those so that concurrent readers do not
	 * interfere with each other through mutex as long as no writers
	 * appear, massively reducing the lock overhead in the typical case.
	 *
	 * The basic algorithm of this approach is the "simple
	 * writer-preference lock" shown in the following URL:
	 * http://www.cs.rochester.edu/u/scott/synchronization/pseudocode/rw.html
	 * but our implementation does not rely on the spin lock unlike the
	 * original algorithm to be more portable as a user space application.
	 */

	/* Read or modified atomically. */
	isc_int32_t		write_requests;
	isc_int32_t		write_completions;
	isc_int32_t		cnt_and_flag;

	/* Locked by lock. */
	isc_condition_t		readable;
	isc_condition_t		writeable;
	unsigned int		readers_waiting;

	/* Locked by rwlock itself. */
	unsigned int		write_granted;

	/* Unlocked. */
	unsigned int		write_quota;

#else  /* ISC_PLATFORM_HAVEXADD && ISC_PLATFORM_HAVECMPXCHG */

	/*%< Locked by lock. */
	isc_condition_t		readable;
	isc_condition_t		writeable;
	isc_rwlocktype_t	type;

	/*% The number of threads that have the lock. */
	unsigned int		active;

	/*%
	 * The number of lock grants made since the lock was last switched
	 * from reading to writing or vice versa; used in determining
	 * when the quota is reached and it is time to switch.
	 */
	unsigned int		granted;
	
	unsigned int		readers_waiting;
	unsigned int		writers_waiting;
	unsigned int		read_quota;
	unsigned int		write_quota;
	isc_rwlocktype_t	original;
#endif  /* ISC_PLATFORM_HAVEXADD && ISC_PLATFORM_HAVECMPXCHG */
};
#else /* ISC_PLATFORM_USETHREADS */
struct isc_rwlock {
	unsigned int		magic;
	isc_rwlocktype_t	type;
	unsigned int		active;
};
#endif /* ISC_PLATFORM_USETHREADS */


isc_result_t
isc_rwlock_init(isc_rwlock_t *rwl, unsigned int read_quota,
		unsigned int write_quota);

isc_result_t
isc_rwlock_lock(isc_rwlock_t *rwl, isc_rwlocktype_t type);

isc_result_t
isc_rwlock_trylock(isc_rwlock_t *rwl, isc_rwlocktype_t type);

isc_result_t
isc_rwlock_unlock(isc_rwlock_t *rwl, isc_rwlocktype_t type);

isc_result_t
isc_rwlock_tryupgrade(isc_rwlock_t *rwl);

void
isc_rwlock_downgrade(isc_rwlock_t *rwl);

void
isc_rwlock_destroy(isc_rwlock_t *rwl);

ISC_LANG_ENDDECLS

#endif /* ISC_RWLOCK_H */
