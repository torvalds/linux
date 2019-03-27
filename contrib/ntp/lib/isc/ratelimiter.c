/*
 * Copyright (C) 2004, 2005, 2007  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 1999-2002  Internet Software Consortium.
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

/* $Id: ratelimiter.c,v 1.25 2007/06/19 23:47:17 tbox Exp $ */

/*! \file */

#include <config.h>

#include <isc/mem.h>
#include <isc/ratelimiter.h>
#include <isc/task.h>
#include <isc/time.h>
#include <isc/timer.h>
#include <isc/util.h>

typedef enum {
	isc_ratelimiter_stalled = 0,
	isc_ratelimiter_ratelimited = 1,
	isc_ratelimiter_idle = 2,
	isc_ratelimiter_shuttingdown = 3
} isc_ratelimiter_state_t;

struct isc_ratelimiter {
	isc_mem_t *		mctx;
	isc_mutex_t		lock;
	int			refs;
	isc_task_t *		task;
	isc_timer_t *		timer;
	isc_interval_t		interval;
	isc_uint32_t		pertic;
	isc_ratelimiter_state_t	state;
	isc_event_t		shutdownevent;
	ISC_LIST(isc_event_t)	pending;
};

#define ISC_RATELIMITEREVENT_SHUTDOWN (ISC_EVENTCLASS_RATELIMITER + 1)

static void
ratelimiter_tick(isc_task_t *task, isc_event_t *event);

static void
ratelimiter_shutdowncomplete(isc_task_t *task, isc_event_t *event);

isc_result_t
isc_ratelimiter_create(isc_mem_t *mctx, isc_timermgr_t *timermgr,
		       isc_task_t *task, isc_ratelimiter_t **ratelimiterp)
{
	isc_result_t result;
	isc_ratelimiter_t *rl;
	INSIST(ratelimiterp != NULL && *ratelimiterp == NULL);

	rl = isc_mem_get(mctx, sizeof(*rl));
	if (rl == NULL)
		return ISC_R_NOMEMORY;
	rl->mctx = mctx;
	rl->refs = 1;
	rl->task = task;
	isc_interval_set(&rl->interval, 0, 0);
	rl->timer = NULL;
	rl->pertic = 1;
	rl->state = isc_ratelimiter_idle;
	ISC_LIST_INIT(rl->pending);

	result = isc_mutex_init(&rl->lock);
	if (result != ISC_R_SUCCESS)
		goto free_mem;
	result = isc_timer_create(timermgr, isc_timertype_inactive,
				  NULL, NULL, rl->task, ratelimiter_tick,
				  rl, &rl->timer);
	if (result != ISC_R_SUCCESS)
		goto free_mutex;

	/*
	 * Increment the reference count to indicate that we may
	 * (soon) have events outstanding.
	 */
	rl->refs++;

	ISC_EVENT_INIT(&rl->shutdownevent,
		       sizeof(isc_event_t),
		       0, NULL, ISC_RATELIMITEREVENT_SHUTDOWN,
		       ratelimiter_shutdowncomplete, rl, rl, NULL, NULL);

	*ratelimiterp = rl;
	return (ISC_R_SUCCESS);

free_mutex:
	DESTROYLOCK(&rl->lock);
free_mem:
	isc_mem_put(mctx, rl, sizeof(*rl));
	return (result);
}

isc_result_t
isc_ratelimiter_setinterval(isc_ratelimiter_t *rl, isc_interval_t *interval) {
	isc_result_t result = ISC_R_SUCCESS;
	LOCK(&rl->lock);
	rl->interval = *interval;
	/*
	 * If the timer is currently running, change its rate.
	 */
        if (rl->state == isc_ratelimiter_ratelimited) {
		result = isc_timer_reset(rl->timer, isc_timertype_ticker, NULL,
					 &rl->interval, ISC_FALSE);
	}
	UNLOCK(&rl->lock);
	return (result);
}

void
isc_ratelimiter_setpertic(isc_ratelimiter_t *rl, isc_uint32_t pertic) {
	if (pertic == 0)
		pertic = 1;
	rl->pertic = pertic;
}

isc_result_t
isc_ratelimiter_enqueue(isc_ratelimiter_t *rl, isc_task_t *task,
			isc_event_t **eventp)
{
	isc_result_t result = ISC_R_SUCCESS;
	isc_event_t *ev;

	REQUIRE(eventp != NULL && *eventp != NULL);
	REQUIRE(task != NULL);
	ev = *eventp;
	REQUIRE(ev->ev_sender == NULL);

	LOCK(&rl->lock);
        if (rl->state == isc_ratelimiter_ratelimited ||
	    rl->state == isc_ratelimiter_stalled) {
		isc_event_t *ev = *eventp;
		ev->ev_sender = task;
                ISC_LIST_APPEND(rl->pending, ev, ev_link);
		*eventp = NULL;
        } else if (rl->state == isc_ratelimiter_idle) {
		result = isc_timer_reset(rl->timer, isc_timertype_ticker, NULL,
					 &rl->interval, ISC_FALSE);
		if (result == ISC_R_SUCCESS) {
			ev->ev_sender = task;
			rl->state = isc_ratelimiter_ratelimited;
		}
	} else {
		INSIST(rl->state == isc_ratelimiter_shuttingdown);
		result = ISC_R_SHUTTINGDOWN;
	}
	UNLOCK(&rl->lock);
	if (*eventp != NULL && result == ISC_R_SUCCESS)
		isc_task_send(task, eventp);
	return (result);
}

static void
ratelimiter_tick(isc_task_t *task, isc_event_t *event) {
	isc_result_t result = ISC_R_SUCCESS;
	isc_ratelimiter_t *rl = (isc_ratelimiter_t *)event->ev_arg;
	isc_event_t *p;
	isc_uint32_t pertic;

	UNUSED(task);

	isc_event_free(&event);

	pertic = rl->pertic;
        while (pertic != 0) {
		pertic--;
		LOCK(&rl->lock);
		p = ISC_LIST_HEAD(rl->pending);
		if (p != NULL) {
			/*
			 * There is work to do.  Let's do it after unlocking.
			 */
			ISC_LIST_UNLINK(rl->pending, p, ev_link);
		} else {
			/*
			 * No work left to do.  Stop the timer so that we don't
			 * waste resources by having it fire periodically.
			 */
			result = isc_timer_reset(rl->timer,
						 isc_timertype_inactive,
						 NULL, NULL, ISC_FALSE);
			RUNTIME_CHECK(result == ISC_R_SUCCESS);
			rl->state = isc_ratelimiter_idle;
			pertic = 0;	/* Force the loop to exit. */
		}
		UNLOCK(&rl->lock);
		if (p != NULL) {
			isc_task_t *evtask = p->ev_sender;
			isc_task_send(evtask, &p);
		}
		INSIST(p == NULL);
	}
}

void
isc_ratelimiter_shutdown(isc_ratelimiter_t *rl) {
	isc_event_t *ev;
	isc_task_t *task;
	LOCK(&rl->lock);
	rl->state = isc_ratelimiter_shuttingdown;
	(void)isc_timer_reset(rl->timer, isc_timertype_inactive,
			      NULL, NULL, ISC_FALSE);
	while ((ev = ISC_LIST_HEAD(rl->pending)) != NULL) {
		ISC_LIST_UNLINK(rl->pending, ev, ev_link);
		ev->ev_attributes |= ISC_EVENTATTR_CANCELED;
		task = ev->ev_sender;
		isc_task_send(task, &ev);
	}
	isc_timer_detach(&rl->timer);
	/*
	 * Send an event to our task.  The delivery of this event
	 * indicates that no more timer events will be delivered.
	 */
	ev = &rl->shutdownevent;
	isc_task_send(rl->task, &ev);

	UNLOCK(&rl->lock);
}

static void
ratelimiter_shutdowncomplete(isc_task_t *task, isc_event_t *event) {
	isc_ratelimiter_t *rl = (isc_ratelimiter_t *)event->ev_arg;

	UNUSED(task);

	isc_ratelimiter_detach(&rl);
}

static void
ratelimiter_free(isc_ratelimiter_t *rl) {
	DESTROYLOCK(&rl->lock);
	isc_mem_put(rl->mctx, rl, sizeof(*rl));
}

void
isc_ratelimiter_attach(isc_ratelimiter_t *source, isc_ratelimiter_t **target) {
	REQUIRE(source != NULL);
	REQUIRE(target != NULL && *target == NULL);

	LOCK(&source->lock);
	REQUIRE(source->refs > 0);
	source->refs++;
	INSIST(source->refs > 0);
	UNLOCK(&source->lock);
	*target = source;
}

void
isc_ratelimiter_detach(isc_ratelimiter_t **rlp) {
	isc_ratelimiter_t *rl = *rlp;
	isc_boolean_t free_now = ISC_FALSE;

	LOCK(&rl->lock);
	REQUIRE(rl->refs > 0);
	rl->refs--;
	if (rl->refs == 0)
		free_now = ISC_TRUE;
	UNLOCK(&rl->lock);

	if (free_now)
		ratelimiter_free(rl);

	*rlp = NULL;
}

isc_result_t
isc_ratelimiter_stall(isc_ratelimiter_t *rl) {
	isc_result_t result = ISC_R_SUCCESS;

	LOCK(&rl->lock);
	switch (rl->state) {
	case isc_ratelimiter_shuttingdown:
		result = ISC_R_SHUTTINGDOWN;
		break;
	case isc_ratelimiter_ratelimited:
		result = isc_timer_reset(rl->timer, isc_timertype_inactive,
				 	 NULL, NULL, ISC_FALSE);
		RUNTIME_CHECK(result == ISC_R_SUCCESS);
	case isc_ratelimiter_idle:
	case isc_ratelimiter_stalled:
		rl->state = isc_ratelimiter_stalled;
		break;
	}
	UNLOCK(&rl->lock);
	return (result);
}

isc_result_t
isc_ratelimiter_release(isc_ratelimiter_t *rl) {
	isc_result_t result = ISC_R_SUCCESS;

	LOCK(&rl->lock);
	switch (rl->state) {
	case isc_ratelimiter_shuttingdown:
		result = ISC_R_SHUTTINGDOWN;
		break;
	case isc_ratelimiter_stalled:
		if (!ISC_LIST_EMPTY(rl->pending)) {
			result = isc_timer_reset(rl->timer,
						 isc_timertype_ticker, NULL,
						 &rl->interval, ISC_FALSE);
			if (result == ISC_R_SUCCESS)
				rl->state = isc_ratelimiter_ratelimited;
		} else 
			rl->state = isc_ratelimiter_idle;
		break;
	case isc_ratelimiter_ratelimited:
	case isc_ratelimiter_idle:
		break;
	}
	UNLOCK(&rl->lock);
	return (result);
}
