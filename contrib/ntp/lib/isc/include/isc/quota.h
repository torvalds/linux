/*
 * Copyright (C) 2004, 2005, 2007  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 2000, 2001  Internet Software Consortium.
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

/* $Id: quota.h,v 1.16 2007/06/19 23:47:18 tbox Exp $ */

#ifndef ISC_QUOTA_H
#define ISC_QUOTA_H 1

/*****
 ***** Module Info
 *****/

/*! \file isc/quota.h
 *
 * \brief The isc_quota_t object is a simple helper object for implementing
 * quotas on things like the number of simultaneous connections to
 * a server.  It keeps track of the amount of quota in use, and
 * encapsulates the locking necessary to allow multiple tasks to
 * share a quota.
 */

/***
 *** Imports.
 ***/

#include <isc/lang.h>
#include <isc/mutex.h>
#include <isc/types.h>

/*****
 ***** Types.
 *****/

ISC_LANG_BEGINDECLS

/*% isc_quota structure */
struct isc_quota {
	isc_mutex_t	lock; /*%< Locked by lock. */
	int 		max;
	int 		used;
	int		soft;
};

isc_result_t
isc_quota_init(isc_quota_t *quota, int max);
/*%<
 * Initialize a quota object.
 *
 * Returns:
 * 	ISC_R_SUCCESS
 *	Other error	Lock creation failed.
 */

void
isc_quota_destroy(isc_quota_t *quota);
/*%<
 * Destroy a quota object.
 */

void
isc_quota_soft(isc_quota_t *quota, int soft);
/*%<
 * Set a soft quota.
 */

void
isc_quota_max(isc_quota_t *quota, int max);
/*%<
 * Re-set a maximum quota.
 */

isc_result_t
isc_quota_reserve(isc_quota_t *quota);
/*%<
 * Attempt to reserve one unit of 'quota'.
 *
 * Returns:
 * \li 	#ISC_R_SUCCESS		Success
 * \li	#ISC_R_SOFTQUOTA	Success soft quota reached
 * \li	#ISC_R_QUOTA		Quota is full
 */

void
isc_quota_release(isc_quota_t *quota);
/*%<
 * Release one unit of quota.
 */

isc_result_t
isc_quota_attach(isc_quota_t *quota, isc_quota_t **p);
/*%<
 * Like isc_quota_reserve, and also attaches '*p' to the
 * quota if successful (ISC_R_SUCCESS or ISC_R_SOFTQUOTA).
 */

void
isc_quota_detach(isc_quota_t **p);
/*%<
 * Like isc_quota_release, and also detaches '*p' from the
 * quota.
 */

ISC_LANG_ENDDECLS

#endif /* ISC_QUOTA_H */
