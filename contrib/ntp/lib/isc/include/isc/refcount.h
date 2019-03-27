/*
 * Copyright (C) 2004-2007, 2009  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 2001, 2003  Internet Software Consortium.
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

/* $Id: refcount.h,v 1.17 2009/09/29 23:48:04 tbox Exp $ */

#ifndef ISC_REFCOUNT_H
#define ISC_REFCOUNT_H 1

#include <isc/atomic.h>
#include <isc/lang.h>
#include <isc/mutex.h>
#include <isc/platform.h>
#include <isc/types.h>
#include <isc/util.h>

/*! \file isc/refcount.h
 * \brief Implements a locked reference counter.
 *
 * These functions may actually be
 * implemented using macros, and implementations of these macros are below.
 * The isc_refcount_t type should not be accessed directly, as its contents
 * depend on the implementation.
 */

ISC_LANG_BEGINDECLS

/*
 * Function prototypes
 */

/*
 * isc_result_t
 * isc_refcount_init(isc_refcount_t *ref, unsigned int n);
 *
 * Initialize the reference counter.  There will be 'n' initial references.
 *
 * Requires:
 *	ref != NULL
 */

/*
 * void
 * isc_refcount_destroy(isc_refcount_t *ref);
 *
 * Destroys a reference counter.
 *
 * Requires:
 *	ref != NULL
 *	The number of references is 0.
 */

/*
 * void
 * isc_refcount_increment(isc_refcount_t *ref, unsigned int *targetp);
 * isc_refcount_increment0(isc_refcount_t *ref, unsigned int *targetp);
 *
 * Increments the reference count, returning the new value in targetp if it's
 * not NULL.  The reference counter typically begins with the initial counter
 * of 1, and will be destroyed once the counter reaches 0.  Thus,
 * isc_refcount_increment() additionally requires the previous counter be
 * larger than 0 so that an error which violates the usage can be easily
 * caught.  isc_refcount_increment0() does not have this restriction.
 *
 * Requires:
 *	ref != NULL.
 */

/*
 * void
 * isc_refcount_decrement(isc_refcount_t *ref, unsigned int *targetp);
 *
 * Decrements the reference count,  returning the new value in targetp if it's
 * not NULL.
 *
 * Requires:
 *	ref != NULL.
 */


/*
 * Sample implementations
 */
#ifdef ISC_PLATFORM_USETHREADS
#ifdef ISC_PLATFORM_HAVEXADD

#define ISC_REFCOUNT_HAVEATOMIC 1

typedef struct isc_refcount {
	isc_int32_t refs;
} isc_refcount_t;

#define isc_refcount_destroy(rp) REQUIRE((rp)->refs == 0)
#define isc_refcount_current(rp) ((unsigned int)((rp)->refs))

#define isc_refcount_increment0(rp, tp)				\
	do {							\
		unsigned int *_tmp = (unsigned int *)(tp);	\
		isc_int32_t prev;				\
		prev = isc_atomic_xadd(&(rp)->refs, 1);		\
		if (_tmp != NULL)				\
			*_tmp = prev + 1;			\
	} while (0)

#define isc_refcount_increment(rp, tp)				\
	do {							\
		unsigned int *_tmp = (unsigned int *)(tp);	\
		isc_int32_t prev;				\
		prev = isc_atomic_xadd(&(rp)->refs, 1);		\
		REQUIRE(prev > 0);				\
		if (_tmp != NULL)				\
			*_tmp = prev + 1;			\
	} while (0)

#define isc_refcount_decrement(rp, tp)				\
	do {							\
		unsigned int *_tmp = (unsigned int *)(tp);	\
		isc_int32_t prev;				\
		prev = isc_atomic_xadd(&(rp)->refs, -1);	\
		REQUIRE(prev > 0);				\
		if (_tmp != NULL)				\
			*_tmp = prev - 1;			\
	} while (0)

#else  /* ISC_PLATFORM_HAVEXADD */

typedef struct isc_refcount {
	int refs;
	isc_mutex_t lock;
} isc_refcount_t;

/*% Destroys a reference counter. */
#define isc_refcount_destroy(rp)			\
	do {						\
		REQUIRE((rp)->refs == 0);		\
		DESTROYLOCK(&(rp)->lock);		\
	} while (0)

#define isc_refcount_current(rp) ((unsigned int)((rp)->refs))

/*% Increments the reference count, returning the new value in targetp if it's not NULL. */
#define isc_refcount_increment0(rp, tp)				\
	do {							\
		unsigned int *_tmp = (unsigned int *)(tp);	\
		LOCK(&(rp)->lock);				\
		++((rp)->refs);					\
		if (_tmp != NULL)				\
			*_tmp = ((rp)->refs);			\
		UNLOCK(&(rp)->lock);				\
	} while (0)

#define isc_refcount_increment(rp, tp)				\
	do {							\
		unsigned int *_tmp = (unsigned int *)(tp);	\
		LOCK(&(rp)->lock);				\
		REQUIRE((rp)->refs > 0);			\
		++((rp)->refs);					\
		if (_tmp != NULL)				\
			*_tmp = ((rp)->refs);			\
		UNLOCK(&(rp)->lock);				\
	} while (0)

/*% Decrements the reference count,  returning the new value in targetp if it's not NULL. */
#define isc_refcount_decrement(rp, tp)				\
	do {							\
		unsigned int *_tmp = (unsigned int *)(tp);	\
		LOCK(&(rp)->lock);				\
		REQUIRE((rp)->refs > 0);			\
		--((rp)->refs);					\
		if (_tmp != NULL)				\
			*_tmp = ((rp)->refs);			\
		UNLOCK(&(rp)->lock);				\
	} while (0)

#endif /* ISC_PLATFORM_HAVEXADD */
#else  /* ISC_PLATFORM_USETHREADS */

typedef struct isc_refcount {
	int refs;
} isc_refcount_t;

#define isc_refcount_destroy(rp) REQUIRE((rp)->refs == 0)
#define isc_refcount_current(rp) ((unsigned int)((rp)->refs))

#define isc_refcount_increment0(rp, tp)					\
	do {								\
		unsigned int *_tmp = (unsigned int *)(tp);		\
		int _n = ++(rp)->refs;					\
		if (_tmp != NULL)					\
			*_tmp = _n;					\
	} while (0)

#define isc_refcount_increment(rp, tp)					\
	do {								\
		unsigned int *_tmp = (unsigned int *)(tp);		\
		int _n;							\
		REQUIRE((rp)->refs > 0);				\
		_n = ++(rp)->refs;					\
		if (_tmp != NULL)					\
			*_tmp = _n;					\
	} while (0)

#define isc_refcount_decrement(rp, tp)					\
	do {								\
		unsigned int *_tmp = (unsigned int *)(tp);		\
		int _n;							\
		REQUIRE((rp)->refs > 0);				\
		_n = --(rp)->refs;					\
		if (_tmp != NULL)					\
			*_tmp = _n;					\
	} while (0)

#endif /* ISC_PLATFORM_USETHREADS */

isc_result_t
isc_refcount_init(isc_refcount_t *ref, unsigned int n);

ISC_LANG_ENDDECLS

#endif /* ISC_REFCOUNT_H */
