/*
 * Copyright (C) 2004, 2005, 2007, 2009  Internet Systems Consortium, Inc. ("ISC")
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

/* $Id: thread.h,v 1.25 2009/09/29 04:37:08 marka Exp $ */

#ifndef ISC_THREAD_H
#define ISC_THREAD_H 1

#include <windows.h>

#include <isc/lang.h>
#include <isc/result.h>

/*
 * Inlines to help with wait retrun checking
 */

/* check handle for NULL and INVALID_HANDLE */
inline BOOL IsValidHandle( HANDLE hHandle) {
    return ((hHandle != NULL) && (hHandle != INVALID_HANDLE_VALUE));
}

/* validate wait return codes... */
inline BOOL WaitSucceeded( DWORD dwWaitResult, DWORD dwHandleCount) {
    return ((dwWaitResult >= WAIT_OBJECT_0) &&
	    (dwWaitResult < WAIT_OBJECT_0 + dwHandleCount));
}

inline BOOL WaitAbandoned( DWORD dwWaitResult, DWORD dwHandleCount) {
    return ((dwWaitResult >= WAIT_ABANDONED_0) &&
	    (dwWaitResult < WAIT_ABANDONED_0 + dwHandleCount));
}

inline BOOL WaitTimeout( DWORD dwWaitResult) {
    return (dwWaitResult == WAIT_TIMEOUT);
}

inline BOOL WaitFailed( DWORD dwWaitResult) {
    return (dwWaitResult == WAIT_FAILED);
}

/* compute object indices for waits... */
inline DWORD WaitSucceededIndex( DWORD dwWaitResult) {
    return (dwWaitResult - WAIT_OBJECT_0);
}

inline DWORD WaitAbandonedIndex( DWORD dwWaitResult) {
    return (dwWaitResult - WAIT_ABANDONED_0);
}



typedef HANDLE isc_thread_t;
typedef DWORD isc_threadresult_t;
typedef void * isc_threadarg_t;
typedef isc_threadresult_t (WINAPI *isc_threadfunc_t)(isc_threadarg_t);
typedef DWORD isc_thread_key_t;

#define isc_thread_self (unsigned long)GetCurrentThreadId

ISC_LANG_BEGINDECLS

isc_result_t
isc_thread_create(isc_threadfunc_t, isc_threadarg_t, isc_thread_t *);

isc_result_t
isc_thread_join(isc_thread_t, isc_threadresult_t *);

void
isc_thread_setconcurrency(unsigned int level);

int
isc_thread_key_create(isc_thread_key_t *key, void (*func)(void *));

int
isc_thread_key_delete(isc_thread_key_t key);

void *
isc_thread_key_getspecific(isc_thread_key_t);

int
isc_thread_key_setspecific(isc_thread_key_t key, void *value);

ISC_LANG_ENDDECLS

#endif /* ISC_THREAD_H */
