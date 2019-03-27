/*
 * Copyright (C) 2004, 2007, 2009  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 1999-2001  Internet Software Consortium.
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

/* $Id: app.c,v 1.9 2009/09/02 23:48:03 tbox Exp $ */

#include <config.h>

#include <sys/types.h>

#include <stddef.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <process.h>

#include <isc/app.h>
#include <isc/boolean.h>
#include <isc/condition.h>
#include <isc/msgs.h>
#include <isc/mutex.h>
#include <isc/event.h>
#include <isc/platform.h>
#include <isc/string.h>
#include <isc/task.h>
#include <isc/time.h>
#include <isc/util.h>
#include <isc/thread.h>

static isc_eventlist_t	on_run;
static isc_mutex_t	lock;
static isc_boolean_t	shutdown_requested = ISC_FALSE;
static isc_boolean_t	running = ISC_FALSE;
/*
 * We assume that 'want_shutdown' can be read and written atomically.
 */
static isc_boolean_t	want_shutdown = ISC_FALSE;
/*
 * We assume that 'want_reload' can be read and written atomically.
 */
static isc_boolean_t	want_reload = ISC_FALSE;

static isc_boolean_t	blocked  = ISC_FALSE;

static isc_thread_t	blockedthread;

/* Events to wait for */

#define NUM_EVENTS 2

enum {
	RELOAD_EVENT,
	SHUTDOWN_EVENT
};

static HANDLE hEvents[NUM_EVENTS];
DWORD  dwWaitResult;

/*
 * We need to remember which thread is the main thread...
 */
static isc_thread_t	main_thread;

isc_result_t
isc__app_start(void) {
	isc_result_t result;

	/*
	 * Start an ISC library application.
	 */

	main_thread = GetCurrentThread();

	result = isc_mutex_init(&lock);
	if (result != ISC_R_SUCCESS)
		return (result);

	/* Create the reload event in a non-signaled state */
	hEvents[RELOAD_EVENT] = CreateEvent(NULL, FALSE, FALSE, NULL);

	/* Create the shutdown event in a non-signaled state */
	hEvents[SHUTDOWN_EVENT] = CreateEvent(NULL, FALSE, FALSE, NULL);

	ISC_LIST_INIT(on_run);
	return (ISC_R_SUCCESS);
}

isc_result_t
isc__app_onrun(isc_mem_t *mctx, isc_task_t *task, isc_taskaction_t action,
	      void *arg) {
	isc_event_t *event;
	isc_task_t *cloned_task = NULL;
	isc_result_t result;


	LOCK(&lock);
	if (running) {
		result = ISC_R_ALREADYRUNNING;
		goto unlock;
	}

	/*
	 * Note that we store the task to which we're going to send the event
	 * in the event's "sender" field.
	 */
	isc_task_attach(task, &cloned_task);
	event = isc_event_allocate(mctx, cloned_task, ISC_APPEVENT_SHUTDOWN,
				   action, arg, sizeof(*event));
	if (event == NULL) {
		result = ISC_R_NOMEMORY;
		goto unlock;
	}

	ISC_LIST_APPEND(on_run, event, ev_link);
	result = ISC_R_SUCCESS;

 unlock:
	UNLOCK(&lock);
	return (result);
}

isc_result_t
isc__app_run(void) {
	isc_event_t *event, *next_event;
	isc_task_t *task;
	HANDLE *pHandles = NULL;

	REQUIRE(main_thread == GetCurrentThread());
	LOCK(&lock);
	if (!running) {
		running = ISC_TRUE;

		/*
		 * Post any on-run events (in FIFO order).
		 */
		for (event = ISC_LIST_HEAD(on_run);
		     event != NULL;
		     event = next_event) {
			next_event = ISC_LIST_NEXT(event, ev_link);
			ISC_LIST_UNLINK(on_run, event, ev_link);
			task = event->ev_sender;
			event->ev_sender = NULL;
			isc_task_sendanddetach(&task, &event);
		}

	}

	UNLOCK(&lock);

	/*
	 * There is no danger if isc_app_shutdown() is called before we wait
	 * for events.
	 */

	while (!want_shutdown) {
		dwWaitResult = WaitForMultipleObjects(NUM_EVENTS, hEvents,
						      FALSE, INFINITE);

		/* See why we returned */

		if (WaitSucceeded(dwWaitResult, NUM_EVENTS)) {
			/*
			 * The return was due to one of the events
			 * being signaled
			 */
			switch (WaitSucceededIndex(dwWaitResult)) {
			case RELOAD_EVENT:
				want_reload = ISC_TRUE;
				break;

			case SHUTDOWN_EVENT:
				want_shutdown = ISC_TRUE;
				break;
			}
		}
		if (want_reload) {
			want_reload = ISC_FALSE;
			return (ISC_R_RELOAD);
		}

		if (want_shutdown && blocked)
			exit(-1);
	}

	return (ISC_R_SUCCESS);
}

isc_result_t
isc__app_shutdown(void) {
	isc_boolean_t want_kill = ISC_TRUE;

	LOCK(&lock);
	REQUIRE(running);

	if (shutdown_requested)
		want_kill = ISC_FALSE;		/* We're only signaling once */
	else
		shutdown_requested = ISC_TRUE;

	UNLOCK(&lock);
	if (want_kill)
		SetEvent(hEvents[SHUTDOWN_EVENT]);

	return (ISC_R_SUCCESS);
}

isc_result_t
isc__app_reload(void) {
	isc_boolean_t want_reload = ISC_TRUE;

	LOCK(&lock);
	REQUIRE(running);

	/*
	 * Don't send the reload signal if we're shutting down.
	 */
	if (shutdown_requested)
		want_reload = ISC_FALSE;

	UNLOCK(&lock);
	if (want_reload)
		SetEvent(hEvents[RELOAD_EVENT]);

	return (ISC_R_SUCCESS);
}

void
isc__app_finish(void) {
	DESTROYLOCK(&lock);
}

void
isc__app_block(void) {
	REQUIRE(running);
	REQUIRE(!blocked);

	blocked = ISC_TRUE;
	blockedthread = GetCurrentThread();
}

void
isc__app_unblock(void) {
	REQUIRE(running);
	REQUIRE(blocked);
	blocked = ISC_FALSE;
	REQUIRE(blockedthread == GetCurrentThread());
}
