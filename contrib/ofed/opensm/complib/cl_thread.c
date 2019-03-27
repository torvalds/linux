/*
 * Copyright (c) 2004, 2005 Voltaire, Inc. All rights reserved.
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

#include <stdio.h>
#include <unistd.h>
#include <sys/sysctl.h>
#include <complib/cl_thread.h>

/*
 * Internal function to run a new user mode thread.
 * This function is always run as a result of creation a new user mode thread.
 * Its main job is to synchronize the creation and running of the new thread.
 */
static void *__cl_thread_wrapper(void *arg)
{
	cl_thread_t *p_thread = (cl_thread_t *) arg;

	CL_ASSERT(p_thread);
	CL_ASSERT(p_thread->pfn_callback);

	p_thread->pfn_callback((void *)p_thread->context);

	return (NULL);
}

void cl_thread_construct(IN cl_thread_t * const p_thread)
{
	CL_ASSERT(p_thread);

	p_thread->osd.state = CL_UNINITIALIZED;
}

cl_status_t cl_thread_init(IN cl_thread_t * const p_thread,
			   IN cl_pfn_thread_callback_t pfn_callback,
			   IN const void *const context,
			   IN const char *const name)
{
	int ret;

	CL_ASSERT(p_thread);

	cl_thread_construct(p_thread);

	/* Initialize the thread structure */
	p_thread->pfn_callback = pfn_callback;
	p_thread->context = context;

	ret = pthread_create(&p_thread->osd.id, NULL,
			     __cl_thread_wrapper, (void *)p_thread);

	if (ret != 0)		/* pthread_create returns a "0" for success */
		return (CL_ERROR);

	p_thread->osd.state = CL_INITIALIZED;

	return (CL_SUCCESS);
}

void cl_thread_destroy(IN cl_thread_t * const p_thread)
{
	CL_ASSERT(p_thread);
	CL_ASSERT(cl_is_state_valid(p_thread->osd.state));

	if (p_thread->osd.state == CL_INITIALIZED)
		pthread_join(p_thread->osd.id, NULL);

	p_thread->osd.state = CL_UNINITIALIZED;
}

void cl_thread_suspend(IN const uint32_t pause_ms)
{
	/* Convert to micro seconds */
	usleep(pause_ms * 1000);
}

void cl_thread_stall(IN const uint32_t pause_us)
{
	/*
	 * Not quite a busy wait, but Linux is lacking in terms of high
	 * resolution time stamp information in user mode.
	 */
	usleep(pause_us);
}

int cl_proc_count(void)
{
	int ret;
	size_t size = sizeof(ret);

	if (sysctlbyname("hw.ncpu", &ret, &size, NULL, 0) != 0 || ret < 1)
		ret = 1;
	return ret;
}

boolean_t cl_is_current_thread(IN const cl_thread_t * const p_thread)
{
	pthread_t current;

	CL_ASSERT(p_thread);
	CL_ASSERT(p_thread->osd.state == CL_INITIALIZED);

	current = pthread_self();
	return (pthread_equal(current, p_thread->osd.id));
}
