/*
 * Copyright (C) 2004, 2006, 2007  Internet Systems Consortium, Inc. ("ISC")
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

/* $Id: condition.c,v 1.23 2007/06/18 23:47:49 tbox Exp $ */

#include <config.h>

#include <isc/condition.h>
#include <isc/assertions.h>
#include <isc/util.h>
#include <isc/thread.h>
#include <isc/time.h>

#define LSIGNAL		0
#define LBROADCAST	1

isc_result_t
isc_condition_init(isc_condition_t *cond) {
	HANDLE h;

	REQUIRE(cond != NULL);

	cond->waiters = 0;
	/*
	 * This handle is shared across all threads
	 */
	h = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (h == NULL) {
		/* XXX */
		return (ISC_R_UNEXPECTED);
	}
	cond->events[LSIGNAL] = h;

	/*
	 * The threadlist will hold the actual events needed
	 * for the wait condition
	 */
	ISC_LIST_INIT(cond->threadlist);

	return (ISC_R_SUCCESS);
}

/*
 * Add the thread to the threadlist along with the required events
 */
static isc_result_t
register_thread(unsigned long thrd, isc_condition_t *gblcond,
		isc_condition_thread_t **localcond)
{
	HANDLE hc;
	isc_condition_thread_t *newthread;

	REQUIRE(localcond != NULL && *localcond == NULL);

	newthread = malloc(sizeof(isc_condition_thread_t));
	if (newthread == NULL)
		return (ISC_R_NOMEMORY);

	/*
	 * Create the thread-specific handle
	 */
	hc = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (hc == NULL) {
		free(newthread);
		return (ISC_R_UNEXPECTED);
	}

	/*
	 * Add the thread ID and handles to list of threads for broadcast
	 */
	newthread->handle[LSIGNAL] = gblcond->events[LSIGNAL];
	newthread->handle[LBROADCAST] = hc;
	newthread->th = thrd;

	/*
	 * The thread is holding the manager lock so this is safe
	 */
	ISC_LIST_APPEND(gblcond->threadlist, newthread, link);
	*localcond = newthread;
	return (ISC_R_SUCCESS);
}

static isc_result_t
find_thread_condition(unsigned long thrd, isc_condition_t *cond,
		      isc_condition_thread_t **threadcondp)
{
	isc_condition_thread_t *threadcond;

	REQUIRE(threadcondp != NULL && *threadcondp == NULL);

	/*
	 * Look for the thread ID.
	 */
	for (threadcond = ISC_LIST_HEAD(cond->threadlist);
	     threadcond != NULL;
	     threadcond = ISC_LIST_NEXT(threadcond, link)) {

		if (threadcond->th == thrd) {
			*threadcondp = threadcond;
			return (ISC_R_SUCCESS);
		}
	}

	/*
	 * Not found, so add it.
	 */
	return (register_thread(thrd, cond, threadcondp));
}

isc_result_t
isc_condition_signal(isc_condition_t *cond) {

	/*
	 * Unlike pthreads, the caller MUST hold the lock associated with
	 * the condition variable when calling us.
	 */
	REQUIRE(cond != NULL);

	if (!SetEvent(cond->events[LSIGNAL])) {
		/* XXX */
		return (ISC_R_UNEXPECTED);
	}

	return (ISC_R_SUCCESS);
}

isc_result_t
isc_condition_broadcast(isc_condition_t *cond) {

	isc_condition_thread_t *threadcond;
	isc_boolean_t failed = ISC_FALSE;

	/*
	 * Unlike pthreads, the caller MUST hold the lock associated with
	 * the condition variable when calling us.
	 */
	REQUIRE(cond != NULL);

	/*
	 * Notify every thread registered for this
	 */
	for (threadcond = ISC_LIST_HEAD(cond->threadlist);
	     threadcond != NULL;
	     threadcond = ISC_LIST_NEXT(threadcond, link)) {

		if (!SetEvent(threadcond->handle[LBROADCAST]))
			failed = ISC_TRUE;
	}

	if (failed)
		return (ISC_R_UNEXPECTED);

	return (ISC_R_SUCCESS);
}

isc_result_t
isc_condition_destroy(isc_condition_t *cond) {

	isc_condition_thread_t *next, *threadcond;

	REQUIRE(cond != NULL);
	REQUIRE(cond->waiters == 0);

	(void)CloseHandle(cond->events[LSIGNAL]);

	/*
	 * Delete the threadlist
	 */
	threadcond = ISC_LIST_HEAD(cond->threadlist);

	while (threadcond != NULL) {
		next = ISC_LIST_NEXT(threadcond, link);
		DEQUEUE(cond->threadlist, threadcond, link);
		(void) CloseHandle(threadcond->handle[LBROADCAST]);
		free(threadcond);
		threadcond = next;
	}

	return (ISC_R_SUCCESS);
}

/*
 * This is always called when the mutex (lock) is held, but because
 * we are waiting we need to release it and reacquire it as soon as the wait
 * is over. This allows other threads to make use of the object guarded
 * by the mutex but it should never try to delete it as long as the
 * number of waiters > 0. Always reacquire the mutex regardless of the
 * result of the wait. Note that EnterCriticalSection will wait to acquire
 * the mutex.
 */
static isc_result_t
wait(isc_condition_t *cond, isc_mutex_t *mutex, DWORD milliseconds) {
	DWORD result;
	isc_result_t tresult;
	isc_condition_thread_t *threadcond = NULL;

	/*
	 * Get the thread events needed for the wait
	 */
	tresult = find_thread_condition(isc_thread_self(), cond, &threadcond);
	if (tresult !=  ISC_R_SUCCESS)
		return (tresult);

	cond->waiters++;
	LeaveCriticalSection(mutex);
	result = WaitForMultipleObjects(2, threadcond->handle, FALSE,
					milliseconds);
	EnterCriticalSection(mutex);
	cond->waiters--;
	if (result == WAIT_FAILED) {
		/* XXX */
		return (ISC_R_UNEXPECTED);
	}
	if (result == WAIT_TIMEOUT)
		return (ISC_R_TIMEDOUT);

	return (ISC_R_SUCCESS);
}

isc_result_t
isc_condition_wait(isc_condition_t *cond, isc_mutex_t *mutex) {
	return (wait(cond, mutex, INFINITE));
}

isc_result_t
isc_condition_waituntil(isc_condition_t *cond, isc_mutex_t *mutex,
			isc_time_t *t) {
	DWORD milliseconds;
	isc_uint64_t microseconds;
	isc_time_t now;

	if (isc_time_now(&now) != ISC_R_SUCCESS) {
		/* XXX */
		return (ISC_R_UNEXPECTED);
	}

	microseconds = isc_time_microdiff(t, &now);
	if (microseconds > 0xFFFFFFFFi64 * 1000)
		milliseconds = 0xFFFFFFFF;
	else
		milliseconds = (DWORD)(microseconds / 1000);

	return (wait(cond, mutex, milliseconds));
}
