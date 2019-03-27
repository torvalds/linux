/*
 * Copyright (C) 2004, 2007-2009  Internet Systems Consortium, Inc. ("ISC")
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

/* $Id: mutex.h,v 1.22 2009/01/18 23:48:14 tbox Exp $ */

#ifndef ISC_MUTEX_H
#define ISC_MUTEX_H 1

#include <isc/net.h>
#include <windows.h>

#include <isc/result.h>

typedef CRITICAL_SECTION isc_mutex_t;

/*
 * This definition is here since some versions of WINBASE.H
 * omits it for some reason.
 */
#if (_WIN32_WINNT < 0x0400)
WINBASEAPI BOOL WINAPI
TryEnterCriticalSection(LPCRITICAL_SECTION lpCriticalSection);
#endif /* _WIN32_WINNT < 0x0400 */

#define isc_mutex_init(mp) \
	(InitializeCriticalSection((mp)), ISC_R_SUCCESS)
#define isc_mutex_lock(mp) \
	(EnterCriticalSection((mp)), ISC_R_SUCCESS)
#define isc_mutex_unlock(mp) \
	(LeaveCriticalSection((mp)), ISC_R_SUCCESS)
#define isc_mutex_trylock(mp) \
	(TryEnterCriticalSection((mp)) ? ISC_R_SUCCESS : ISC_R_LOCKBUSY)
#define isc_mutex_destroy(mp) \
	(DeleteCriticalSection((mp)), ISC_R_SUCCESS)

/*
 * This is a placeholder for now since we are not keeping any mutex stats
 */
#define isc_mutex_stats(fp) do {} while (0)

#endif /* ISC_MUTEX_H */
