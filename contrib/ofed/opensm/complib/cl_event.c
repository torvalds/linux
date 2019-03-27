/*
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
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

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <complib/cl_event.h>
#include <complib/cl_debug.h>
#include <sys/time.h>
#include <sys/errno.h>

void cl_event_construct(IN cl_event_t * p_event)
{
	CL_ASSERT(p_event);

	p_event->state = CL_UNINITIALIZED;
}

cl_status_t cl_event_init(IN cl_event_t * const p_event,
			  IN const boolean_t manual_reset)
{
	CL_ASSERT(p_event);

	cl_event_construct(p_event);

	pthread_cond_init(&p_event->condvar, NULL);
	pthread_mutex_init(&p_event->mutex, NULL);
	p_event->signaled = FALSE;
	p_event->manual_reset = manual_reset;
	p_event->state = CL_INITIALIZED;

	return CL_SUCCESS;
}

void cl_event_destroy(IN cl_event_t * const p_event)
{
	CL_ASSERT(cl_is_state_valid(p_event->state));

	/* Destroy only if the event was constructed */
	if (p_event->state == CL_INITIALIZED) {
		pthread_cond_broadcast(&p_event->condvar);
		pthread_cond_destroy(&p_event->condvar);
		pthread_mutex_destroy(&p_event->mutex);
	}

	p_event->state = CL_UNINITIALIZED;
}

cl_status_t cl_event_signal(IN cl_event_t * const p_event)
{
	/* Make sure that the event was started */
	CL_ASSERT(p_event->state == CL_INITIALIZED);

	pthread_mutex_lock(&p_event->mutex);
	p_event->signaled = TRUE;
	/* Wake up one or all depending on whether the event is auto-resetting. */
	if (p_event->manual_reset)
		pthread_cond_broadcast(&p_event->condvar);
	else
		pthread_cond_signal(&p_event->condvar);

	pthread_mutex_unlock(&p_event->mutex);

	return CL_SUCCESS;
}

cl_status_t cl_event_reset(IN cl_event_t * const p_event)
{
	/* Make sure that the event was started */
	CL_ASSERT(p_event->state == CL_INITIALIZED);

	pthread_mutex_lock(&p_event->mutex);
	p_event->signaled = FALSE;
	pthread_mutex_unlock(&p_event->mutex);

	return CL_SUCCESS;
}

cl_status_t cl_event_wait_on(IN cl_event_t * const p_event,
			     IN const uint32_t wait_us,
			     IN const boolean_t interruptible)
{
	cl_status_t status;
	int wait_ret;
	struct timespec timeout;
	struct timeval curtime;

	/* Make sure that the event was Started */
	CL_ASSERT(p_event->state == CL_INITIALIZED);

	pthread_mutex_lock(&p_event->mutex);

	/* Return immediately if the event is signalled. */
	if (p_event->signaled) {
		if (!p_event->manual_reset)
			p_event->signaled = FALSE;

		pthread_mutex_unlock(&p_event->mutex);
		return CL_SUCCESS;
	}

	/* If just testing the state, return CL_TIMEOUT. */
	if (wait_us == 0) {
		pthread_mutex_unlock(&p_event->mutex);
		return CL_TIMEOUT;
	}

	if (wait_us == EVENT_NO_TIMEOUT) {
		/* Wait for condition variable to be signaled or broadcast. */
		if (pthread_cond_wait(&p_event->condvar, &p_event->mutex))
			status = CL_NOT_DONE;
		else
			status = CL_SUCCESS;
	} else {
		/* Get the current time */
		if (gettimeofday(&curtime, NULL) == 0) {
			unsigned long n_sec =
			    (curtime.tv_usec + (wait_us % 1000000)) * 1000;
			timeout.tv_sec = curtime.tv_sec + (wait_us / 1000000)
			    + (n_sec / 1000000000);
			timeout.tv_nsec = n_sec % 1000000000;

			wait_ret = pthread_cond_timedwait(&p_event->condvar,
							  &p_event->mutex,
							  &timeout);
			if (wait_ret == 0)
				status =
				    (p_event->
				     signaled ? CL_SUCCESS : CL_NOT_DONE);
			else if (wait_ret == ETIMEDOUT)
				status = CL_TIMEOUT;
			else
				status = CL_NOT_DONE;
		} else
			status = CL_ERROR;
	}
	if (!p_event->manual_reset)
		p_event->signaled = FALSE;

	pthread_mutex_unlock(&p_event->mutex);
	return status;
}
