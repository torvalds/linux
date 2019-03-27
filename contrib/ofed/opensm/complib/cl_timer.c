/*
 * Copyright (c) 2004-2006 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2005 Mellanox Technologies LTD. All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

/*
 * Abstract:
 * Abstraction of Timer create, destroy functions.
 *
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <stdlib.h>
#include <string.h>
#include <complib/cl_timer.h>
#include <sys/time.h>
#include <sys/errno.h>
#include <stdio.h>

/* Timer provider (emulates timers in user mode). */
typedef struct _cl_timer_prov {
	pthread_t thread;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	cl_qlist_t queue;

	boolean_t exit;

} cl_timer_prov_t;

/* Global timer provider. */
static cl_timer_prov_t *gp_timer_prov = NULL;

static void *__cl_timer_prov_cb(IN void *const context);

/*
 * Creates the process global timer provider.  Must be called by the shared
 * object framework to solve all serialization issues.
 */
cl_status_t __cl_timer_prov_create(void)
{
	CL_ASSERT(gp_timer_prov == NULL);

	gp_timer_prov = malloc(sizeof(cl_timer_prov_t));
	if (!gp_timer_prov)
		return (CL_INSUFFICIENT_MEMORY);
	else
		memset(gp_timer_prov, 0, sizeof(cl_timer_prov_t));

	cl_qlist_init(&gp_timer_prov->queue);

	pthread_mutex_init(&gp_timer_prov->mutex, NULL);
	pthread_cond_init(&gp_timer_prov->cond, NULL);

	if (pthread_create(&gp_timer_prov->thread, NULL,
			   __cl_timer_prov_cb, NULL)) {
		__cl_timer_prov_destroy();
		return (CL_ERROR);
	}

	return (CL_SUCCESS);
}

void __cl_timer_prov_destroy(void)
{
	pthread_t tid;

	if (!gp_timer_prov)
		return;

	tid = gp_timer_prov->thread;
	pthread_mutex_lock(&gp_timer_prov->mutex);
	gp_timer_prov->exit = TRUE;
	pthread_cond_broadcast(&gp_timer_prov->cond);
	pthread_mutex_unlock(&gp_timer_prov->mutex);
	pthread_join(tid, NULL);

	/* Destroy the mutex and condition variable. */
	pthread_mutex_destroy(&gp_timer_prov->mutex);
	pthread_cond_destroy(&gp_timer_prov->cond);

	/* Free the memory and reset the global pointer. */
	free(gp_timer_prov);
	gp_timer_prov = NULL;
}

/*
 * This is the internal work function executed by the timer's thread.
 */
static void *__cl_timer_prov_cb(IN void *const context)
{
	int ret;
	cl_timer_t *p_timer;

	pthread_mutex_lock(&gp_timer_prov->mutex);
	while (!gp_timer_prov->exit) {
		if (cl_is_qlist_empty(&gp_timer_prov->queue)) {
			/* Wait until we exit or a timer is queued. */
			/* cond wait does:
			 * pthread_cond_wait atomically unlocks the mutex (as per
			 * pthread_unlock_mutex) and waits for the condition variable
			 * cond to be signaled. The thread execution is suspended and
			 * does not consume any CPU time until the condition variable is
			 * signaled. The mutex must be locked by the calling thread on
			 * entrance to pthread_cond_wait. Before RETURNING TO THE
			 * CALLING THREAD, PTHREAD_COND_WAIT RE-ACQUIRES MUTEX (as per
			 * pthread_lock_mutex).
			 */
			ret = pthread_cond_wait(&gp_timer_prov->cond,
						&gp_timer_prov->mutex);
		} else {
			/*
			 * The timer elements are on the queue in expiration order.
			 * Get the first in the list to determine how long to wait.
			 */

			p_timer =
			    (cl_timer_t *) cl_qlist_head(&gp_timer_prov->queue);
			ret =
			    pthread_cond_timedwait(&gp_timer_prov->cond,
						   &gp_timer_prov->mutex,
						   &p_timer->timeout);

			/*
			   Sleep again on every event other than timeout and invalid
			   Note: EINVAL means that we got behind. This can occur when
			   we are very busy...
			 */
			if (ret != ETIMEDOUT && ret != EINVAL)
				continue;

			/*
			 * The timer expired.  Check the state in case it was cancelled
			 * after it expired but before we got a chance to invoke the
			 * callback.
			 */
			if (p_timer->timer_state != CL_TIMER_QUEUED)
				continue;

			/*
			 * Mark the timer as running to synchronize with its
			 * cancelation since we can't hold the mutex during the
			 * callback.
			 */
			p_timer->timer_state = CL_TIMER_RUNNING;

			/* Remove the item from the timer queue. */
			cl_qlist_remove_item(&gp_timer_prov->queue,
					     &p_timer->list_item);
			pthread_mutex_unlock(&gp_timer_prov->mutex);
			/* Invoke the callback. */
			p_timer->pfn_callback((void *)p_timer->context);

			/* Acquire the mutex again. */
			pthread_mutex_lock(&gp_timer_prov->mutex);
			/*
			 * Only set the state to idle if the timer has not been accessed
			 * from the callback
			 */
			if (p_timer->timer_state == CL_TIMER_RUNNING)
				p_timer->timer_state = CL_TIMER_IDLE;

			/*
			 * Signal any thread trying to manipulate the timer
			 * that expired.
			 */
			pthread_cond_signal(&p_timer->cond);
		}
	}
	gp_timer_prov->thread = 0;
	pthread_mutex_unlock(&gp_timer_prov->mutex);
	pthread_exit(NULL);
}

/* Timer implementation. */
void cl_timer_construct(IN cl_timer_t * const p_timer)
{
	memset(p_timer, 0, sizeof(cl_timer_t));
	p_timer->state = CL_UNINITIALIZED;
}

cl_status_t cl_timer_init(IN cl_timer_t * const p_timer,
			  IN cl_pfn_timer_callback_t pfn_callback,
			  IN const void *const context)
{
	CL_ASSERT(p_timer);
	CL_ASSERT(pfn_callback);

	cl_timer_construct(p_timer);

	if (!gp_timer_prov)
		return (CL_ERROR);

	/* Store timer parameters. */
	p_timer->pfn_callback = pfn_callback;
	p_timer->context = context;

	/* Mark the timer as idle. */
	p_timer->timer_state = CL_TIMER_IDLE;

	/* Create the condition variable that is used when cancelling a timer. */
	pthread_cond_init(&p_timer->cond, NULL);

	p_timer->state = CL_INITIALIZED;

	return (CL_SUCCESS);
}

void cl_timer_destroy(IN cl_timer_t * const p_timer)
{
	CL_ASSERT(p_timer);
	CL_ASSERT(cl_is_state_valid(p_timer->state));

	if (p_timer->state == CL_INITIALIZED)
		cl_timer_stop(p_timer);

	p_timer->state = CL_UNINITIALIZED;

	/* is it possible we have some threads waiting on the cond now? */
	pthread_cond_broadcast(&p_timer->cond);
	pthread_cond_destroy(&p_timer->cond);

}

/*
 * Return TRUE if timeout value 1 is earlier than timeout value 2.
 */
static __inline boolean_t __cl_timer_is_earlier(IN struct timespec *p_timeout1,
						IN struct timespec *p_timeout2)
{
	return ((p_timeout1->tv_sec < p_timeout2->tv_sec) ||
		((p_timeout1->tv_sec == p_timeout2->tv_sec) &&
		 (p_timeout1->tv_nsec < p_timeout2->tv_nsec)));
}

/*
 * Search for a timer with an earlier timeout than the one provided by
 * the context.  Both the list item and the context are pointers to
 * a cl_timer_t structure with valid timeouts.
 */
static cl_status_t __cl_timer_find(IN const cl_list_item_t * const p_list_item,
				   IN void *const context)
{
	cl_timer_t *p_in_list;
	cl_timer_t *p_new;

	CL_ASSERT(p_list_item);
	CL_ASSERT(context);

	p_in_list = (cl_timer_t *) p_list_item;
	p_new = (cl_timer_t *) context;

	CL_ASSERT(p_in_list->state == CL_INITIALIZED);
	CL_ASSERT(p_new->state == CL_INITIALIZED);

	CL_ASSERT(p_in_list->timer_state == CL_TIMER_QUEUED);

	if (__cl_timer_is_earlier(&p_in_list->timeout, &p_new->timeout))
		return (CL_SUCCESS);

	return (CL_NOT_FOUND);
}

/*
 * Calculate 'struct timespec' value that is the
 * current time plus the 'time_ms' milliseconds.
 */
static __inline void __cl_timer_calculate(IN const uint32_t time_ms,
					  OUT struct timespec * const p_timer)
{
	struct timeval curtime, deltatime, endtime;

	gettimeofday(&curtime, NULL);

	deltatime.tv_sec = time_ms / 1000;
	deltatime.tv_usec = (time_ms % 1000) * 1000;
	timeradd(&curtime, &deltatime, &endtime);
	p_timer->tv_sec = endtime.tv_sec;
	p_timer->tv_nsec = endtime.tv_usec * 1000;
}

cl_status_t cl_timer_start(IN cl_timer_t * const p_timer,
			   IN const uint32_t time_ms)
{
	cl_list_item_t *p_list_item;

	CL_ASSERT(p_timer);
	CL_ASSERT(p_timer->state == CL_INITIALIZED);

	pthread_mutex_lock(&gp_timer_prov->mutex);
	/* Signal the timer provider thread to wake up. */
	pthread_cond_signal(&gp_timer_prov->cond);

	/* Remove the timer from the queue if currently queued. */
	if (p_timer->timer_state == CL_TIMER_QUEUED)
		cl_qlist_remove_item(&gp_timer_prov->queue,
				     &p_timer->list_item);

	__cl_timer_calculate(time_ms, &p_timer->timeout);

	/* Add the timer to the queue. */
	if (cl_is_qlist_empty(&gp_timer_prov->queue)) {
		/* The timer list is empty.  Add to the head. */
		cl_qlist_insert_head(&gp_timer_prov->queue,
				     &p_timer->list_item);
	} else {
		/* Find the correct insertion place in the list for the timer. */
		p_list_item = cl_qlist_find_from_tail(&gp_timer_prov->queue,
						      __cl_timer_find, p_timer);

		/* Insert the timer. */
		cl_qlist_insert_next(&gp_timer_prov->queue, p_list_item,
				     &p_timer->list_item);
	}
	/* Set the state. */
	p_timer->timer_state = CL_TIMER_QUEUED;
	pthread_mutex_unlock(&gp_timer_prov->mutex);

	return (CL_SUCCESS);
}

void cl_timer_stop(IN cl_timer_t * const p_timer)
{
	CL_ASSERT(p_timer);
	CL_ASSERT(p_timer->state == CL_INITIALIZED);

	pthread_mutex_lock(&gp_timer_prov->mutex);
	switch (p_timer->timer_state) {
	case CL_TIMER_RUNNING:
		/* Wait for the callback to complete. */
		pthread_cond_wait(&p_timer->cond, &gp_timer_prov->mutex);
		/* Timer could have been queued while we were waiting. */
		if (p_timer->timer_state != CL_TIMER_QUEUED)
			break;

	case CL_TIMER_QUEUED:
		/* Change the state of the timer. */
		p_timer->timer_state = CL_TIMER_IDLE;
		/* Remove the timer from the queue. */
		cl_qlist_remove_item(&gp_timer_prov->queue,
				     &p_timer->list_item);
		/*
		 * Signal the timer provider thread to move onto the
		 * next timer in the queue.
		 */
		pthread_cond_signal(&gp_timer_prov->cond);
		break;

	case CL_TIMER_IDLE:
		break;
	}
	pthread_mutex_unlock(&gp_timer_prov->mutex);
}

cl_status_t cl_timer_trim(IN cl_timer_t * const p_timer,
			  IN const uint32_t time_ms)
{
	struct timespec newtime;
	cl_status_t status;

	CL_ASSERT(p_timer);
	CL_ASSERT(p_timer->state == CL_INITIALIZED);

	pthread_mutex_lock(&gp_timer_prov->mutex);

	__cl_timer_calculate(time_ms, &newtime);

	if (p_timer->timer_state == CL_TIMER_QUEUED) {
		/* If the old time is earlier, do not trim it.  Just return. */
		if (__cl_timer_is_earlier(&p_timer->timeout, &newtime)) {
			pthread_mutex_unlock(&gp_timer_prov->mutex);
			return (CL_SUCCESS);
		}
	}

	/* Reset the timer to the new timeout value. */

	pthread_mutex_unlock(&gp_timer_prov->mutex);
	status = cl_timer_start(p_timer, time_ms);

	return (status);
}

uint64_t cl_get_time_stamp(void)
{
	uint64_t tstamp;
	struct timeval tv;

	gettimeofday(&tv, NULL);

	/* Convert the time of day into a microsecond timestamp. */
	tstamp = ((uint64_t) tv.tv_sec * 1000000) + (uint64_t) tv.tv_usec;

	return (tstamp);
}

uint32_t cl_get_time_stamp_sec(void)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);

	return (tv.tv_sec);
}
