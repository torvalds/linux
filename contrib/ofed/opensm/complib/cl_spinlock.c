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

#include <complib/cl_spinlock.h>

void cl_spinlock_construct(IN cl_spinlock_t * const p_spinlock)
{
	CL_ASSERT(p_spinlock);

	p_spinlock->state = CL_UNINITIALIZED;
}

cl_status_t cl_spinlock_init(IN cl_spinlock_t * const p_spinlock)
{
	CL_ASSERT(p_spinlock);

	cl_spinlock_construct(p_spinlock);

	/* Initialize with pthread_mutexattr_t = NULL */
	if (pthread_mutex_init(&p_spinlock->mutex, NULL))
		return (CL_ERROR);

	p_spinlock->state = CL_INITIALIZED;
	return (CL_SUCCESS);
}

void cl_spinlock_destroy(IN cl_spinlock_t * const p_spinlock)
{
	CL_ASSERT(p_spinlock);
	CL_ASSERT(cl_is_state_valid(p_spinlock->state));

	if (p_spinlock->state == CL_INITIALIZED) {
		p_spinlock->state = CL_UNINITIALIZED;
		pthread_mutex_lock(&p_spinlock->mutex);
		pthread_mutex_unlock(&p_spinlock->mutex);
		pthread_mutex_destroy(&p_spinlock->mutex);
	}
	p_spinlock->state = CL_UNINITIALIZED;
}

void cl_spinlock_acquire(IN cl_spinlock_t * const p_spinlock)
{
	CL_ASSERT(p_spinlock);
	CL_ASSERT(p_spinlock->state == CL_INITIALIZED);

	pthread_mutex_lock(&p_spinlock->mutex);
}

void cl_spinlock_release(IN cl_spinlock_t * const p_spinlock)
{
	CL_ASSERT(p_spinlock);
	CL_ASSERT(p_spinlock->state == CL_INITIALIZED);

	pthread_mutex_unlock(&p_spinlock->mutex);
}
