/*
 * Copyright (c) 2004-2007 Voltaire, Inc. All rights reserved.
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
 *	Implementation of thread pool.
 *
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <complib/cl_threadpool.h>

static void cleanup_mutex(void *arg)
{
	pthread_mutex_unlock(&((cl_thread_pool_t *) arg)->mutex);
}

static void *thread_pool_routine(void *context)
{
	cl_thread_pool_t *p_thread_pool = (cl_thread_pool_t *) context;

	do {
		pthread_mutex_lock(&p_thread_pool->mutex);
		pthread_cleanup_push(cleanup_mutex, p_thread_pool);
		while (!p_thread_pool->events)
			pthread_cond_wait(&p_thread_pool->cond,
					  &p_thread_pool->mutex);
		p_thread_pool->events--;
		pthread_cleanup_pop(1);
		/* The event has been signalled.  Invoke the callback. */
		(*p_thread_pool->pfn_callback) (p_thread_pool->context);
	} while (1);

	return NULL;
}

cl_status_t cl_thread_pool_init(IN cl_thread_pool_t * const p_thread_pool,
				IN unsigned count,
				IN void (*pfn_callback) (void *),
				IN void *context, IN const char *const name)
{
	int i;

	CL_ASSERT(p_thread_pool);
	CL_ASSERT(pfn_callback);

	memset(p_thread_pool, 0, sizeof(*p_thread_pool));

	if (!count)
		count = cl_proc_count();

	pthread_mutex_init(&p_thread_pool->mutex, NULL);
	pthread_cond_init(&p_thread_pool->cond, NULL);

	p_thread_pool->events = 0;

	p_thread_pool->pfn_callback = pfn_callback;
	p_thread_pool->context = context;

	p_thread_pool->tid = calloc(count, sizeof(*p_thread_pool->tid));
	if (!p_thread_pool->tid) {
		cl_thread_pool_destroy(p_thread_pool);
		return CL_INSUFFICIENT_MEMORY;
	}

	p_thread_pool->running_count = count;

	for (i = 0; i < count; i++) {
		if (pthread_create(&p_thread_pool->tid[i], NULL,
				   thread_pool_routine, p_thread_pool) != 0) {
			cl_thread_pool_destroy(p_thread_pool);
			return CL_INSUFFICIENT_RESOURCES;
		}
	}

	return (CL_SUCCESS);
}

void cl_thread_pool_destroy(IN cl_thread_pool_t * const p_thread_pool)
{
	int i;

	CL_ASSERT(p_thread_pool);

	for (i = 0; i < p_thread_pool->running_count; i++)
		if (p_thread_pool->tid[i])
			pthread_cancel(p_thread_pool->tid[i]);

	for (i = 0; i < p_thread_pool->running_count; i++)
		if (p_thread_pool->tid[i])
			pthread_join(p_thread_pool->tid[i], NULL);

	p_thread_pool->running_count = 0;

	free(p_thread_pool->tid);

	pthread_cond_destroy(&p_thread_pool->cond);
	pthread_mutex_destroy(&p_thread_pool->mutex);

	p_thread_pool->events = 0;
}

cl_status_t cl_thread_pool_signal(IN cl_thread_pool_t * const p_thread_pool)
{
	int ret;
	CL_ASSERT(p_thread_pool);
	pthread_mutex_lock(&p_thread_pool->mutex);
	p_thread_pool->events++;
	ret = pthread_cond_signal(&p_thread_pool->cond);
	pthread_mutex_unlock(&p_thread_pool->mutex);
	return ret;
}
