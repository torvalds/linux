/*
 * Copyright (C) 2004, 2005, 2007  Internet Systems Consortium, Inc. ("ISC")
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

/* $Id: condition.h,v 1.26 2007/06/19 23:47:18 tbox Exp $ */

#ifndef ISC_CONDITION_H
#define ISC_CONDITION_H 1

/*! \file */

#include <isc/lang.h>
#include <isc/mutex.h>
#include <isc/result.h>
#include <isc/types.h>

typedef pthread_cond_t isc_condition_t;

#define isc_condition_init(cp) \
	((pthread_cond_init((cp), NULL) == 0) ? \
	 ISC_R_SUCCESS : ISC_R_UNEXPECTED)

#if ISC_MUTEX_PROFILE
#define isc_condition_wait(cp, mp) \
	((pthread_cond_wait((cp), &((mp)->mutex)) == 0) ? \
	 ISC_R_SUCCESS : ISC_R_UNEXPECTED)
#else
#define isc_condition_wait(cp, mp) \
	((pthread_cond_wait((cp), (mp)) == 0) ? \
	 ISC_R_SUCCESS : ISC_R_UNEXPECTED)
#endif

#define isc_condition_signal(cp) \
	((pthread_cond_signal((cp)) == 0) ? \
	 ISC_R_SUCCESS : ISC_R_UNEXPECTED)

#define isc_condition_broadcast(cp) \
	((pthread_cond_broadcast((cp)) == 0) ? \
	 ISC_R_SUCCESS : ISC_R_UNEXPECTED)

#define isc_condition_destroy(cp) \
	((pthread_cond_destroy((cp)) == 0) ? \
	 ISC_R_SUCCESS : ISC_R_UNEXPECTED)

ISC_LANG_BEGINDECLS

isc_result_t
isc_condition_waituntil(isc_condition_t *, isc_mutex_t *, isc_time_t *);

ISC_LANG_ENDDECLS

#endif /* ISC_CONDITION_H */
