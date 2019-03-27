/*
 * Copyright (C) 2004, 2007  Internet Systems Consortium, Inc. ("ISC")
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

/* $Id: condition.h,v 1.6 2007/06/19 23:47:18 tbox Exp $ */

/*
 * This provides a limited subset of the isc_condition_t
 * functionality for use by single-threaded programs that
 * need to block waiting for events.   Only a single
 * call to isc_condition_wait() may be blocked at any given
 * time, and the _waituntil and _broadcast functions are not
 * supported.  This is intended primarily for use by the omapi
 * library, and may go away once omapi goes away.  Use for
 * other purposes is strongly discouraged.
 */

#ifndef ISC_CONDITION_H
#define ISC_CONDITION_H 1

#include <isc/mutex.h>

typedef int isc_condition_t;

isc_result_t isc__nothread_wait_hack(isc_condition_t *cp, isc_mutex_t *mp);
isc_result_t isc__nothread_signal_hack(isc_condition_t *cp);

#define isc_condition_init(cp) \
	(*(cp) = 0, ISC_R_SUCCESS)

#define isc_condition_wait(cp, mp) \
	isc__nothread_wait_hack(cp, mp)

#define isc_condition_waituntil(cp, mp, tp) \
	((void)(cp), (void)(mp), (void)(tp), ISC_R_NOTIMPLEMENTED)

#define isc_condition_signal(cp) \
	isc__nothread_signal_hack(cp)

#define isc_condition_broadcast(cp) \
	((void)(cp), ISC_R_NOTIMPLEMENTED)

#define isc_condition_destroy(cp) \
	(*(cp) == 0 ? (*(cp) = -1, ISC_R_SUCCESS) : ISC_R_UNEXPECTED)

#endif /* ISC_CONDITION_H */
