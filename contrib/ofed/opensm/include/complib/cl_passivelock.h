/*
 * Copyright (c) 2004, 2005 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2010 Mellanox Technologies LTD. All rights reserved.
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
 *	This file contains the passive lock, which synchronizes passive threads.
 *	The passive lock allows multiple readers to access a resource
 *	simultaneously, exclusive from a single thread allowed writing.
 * Several writer threads are allowed - but only one can write at a given time
 */

#ifndef _CL_PASSIVE_LOCK_H_
#define _CL_PASSIVE_LOCK_H_
#include <complib/cl_types.h>
#include <pthread.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
/****h* Component Library/Passive Lock
* NAME
*	Passive Lock
*
* DESCRIPTION
*	The Passive Lock provides synchronization between multiple threads that
*	are sharing the lock with a single thread holding the lock exclusively.
*
*	Passive lock works exclusively between threads and cannot be used in
*	situations where the caller cannot be put into a waiting state.
*
*	The passive lock functions operate a cl_plock_t structure which should
*	be treated as opaque and should be manipulated only through the provided
*	functions.
*
* SEE ALSO
*	Structures:
*		cl_plock_t
*
*	Initialization:
*		cl_plock_construct, cl_plock_init, cl_plock_destroy
*
*	Manipulation
*		cl_plock_acquire, cl_plock_excl_acquire, cl_plock_release
*********/
/****s* Component Library: Passive Lock/cl_plock_t
* NAME
*	cl_plock_t
*
* DESCRIPTION
*	Passive Lock structure.
*
*	The cl_plock_t structure should be treated as opaque and should
*	be manipulated only through the provided functions.
*
* SYNOPSIS
*/
typedef struct _cl_plock {
	pthread_rwlock_t lock;
	cl_state_t state;
} cl_plock_t;
/*
* FIELDS
*	lock
*		Pthread RWLOCK object
*
*	state
*		Records the current state of the lock, such as initialized,
*		destroying, etc.
*
* SEE ALSO
*	Passive Lock
*********/

/****f* Component Library: Passive Lock/cl_plock_construct
* NAME
*	cl_plock_construct
*
* DESCRIPTION
*	The cl_plock_construct function initializes the state of a
*	passive lock.
*
* SYNOPSIS
*/
static inline void cl_plock_construct(IN cl_plock_t * const p_lock)
{
	CL_ASSERT(p_lock);

	p_lock->state = CL_UNINITIALIZED;
}

/*
* PARAMETERS
*	p_lock
*		[in] Pointer to a cl_plock_t structure whose state to initialize.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Allows calling cl_plock_destroy without first calling cl_plock_init.
*
*	Calling cl_plock_construct is a prerequisite to calling any other
*	passive lock function except cl_plock_init.
*
* SEE ALSO
*	Passive Lock, cl_plock_init, cl_plock_destroy
*********/

/****f* Component Library: Passive Lock/cl_plock_destroy
* NAME
*	cl_plock_destroy
*
* DESCRIPTION
*	The cl_plock_destroy function performs any necessary cleanup
*	of a passive lock.
*
* SYNOPSIS
*/
static inline void cl_plock_destroy(IN cl_plock_t * const p_lock)
{
	CL_ASSERT(p_lock);
	p_lock->state = CL_DESTROYING;
	pthread_rwlock_destroy(&p_lock->lock);
	p_lock->state = CL_DESTROYED;
}

/*
* PARAMETERS
*	p_lock
*		[in] Pointer to a cl_plock_t structure whose state to initialize.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	cl_plock_destroy performs any necessary cleanup of the specified
*	passive lock.
*
*	This function must only be called if cl_plock_construct or
*	cl_plock_init has been called. The passive lock must not be held
*	when calling this function.
*
* SEE ALSO
*	Passive Lock, cl_plock_construct, cl_plock_init
*********/

/****f* Component Library: Passive Lock/cl_plock_init
* NAME
*	cl_plock_init
*
* DESCRIPTION
*	The cl_plock_init function initializes a passive lock.
*
* SYNOPSIS
*/
static inline cl_status_t cl_plock_init(IN cl_plock_t * const p_lock)
{
	cl_status_t status;

	CL_ASSERT(p_lock);
	status = pthread_rwlock_init(&p_lock->lock, NULL);
	if (status)
		return CL_ERROR;
	p_lock->state = CL_INITIALIZED;
	return (CL_SUCCESS);
}

/*
* PARAMETERS
*	p_lock
*		[in] Pointer to a cl_plock_t structure to initialize.
*
* RETURN VALUES
*	CL_SUCCESS if the passive lock was initialized successfully.
*
*	CL_ERROR otherwise.
*
* NOTES
*	Allows calling cl_plock_acquire, cl_plock_release,
*	cl_plock_excl_acquire
*
* SEE ALSO
*	Passive Lock, cl_plock_construct, cl_plock_destroy,
*	cl_plock_excl_acquire, cl_plock_acquire, cl_plock_release
*********/

/****f* Component Library: Passive Lock/cl_plock_acquire
* NAME
*	cl_plock_acquire
*
* DESCRIPTION
*	The cl_plock_acquire function acquires a passive lock for
*	shared access.
*
* SYNOPSIS
*/
static inline void cl_plock_acquire(IN cl_plock_t * const p_lock)
{
	cl_status_t __attribute__((unused)) status;
	CL_ASSERT(p_lock);
	CL_ASSERT(p_lock->state == CL_INITIALIZED);

	status = pthread_rwlock_rdlock(&p_lock->lock);
	CL_ASSERT(status == 0);
}

/*
* PARAMETERS
*	p_lock
*		[in] Pointer to a cl_plock_t structure to acquire.
*
* RETURN VALUE
*	This function does not return a value.
*
* SEE ALSO
*	Passive Lock, cl_plock_release, cl_plock_excl_acquire
*********/

/****f* Component Library: Passive Lock/cl_plock_excl_acquire
* NAME
*	cl_plock_excl_acquire
*
* DESCRIPTION
*	The cl_plock_excl_acquire function acquires exclusive access
*	to a passive lock.
*
* SYNOPSIS
*/
static inline void cl_plock_excl_acquire(IN cl_plock_t * const p_lock)
{
	cl_status_t __attribute__((unused)) status;

	CL_ASSERT(p_lock);
	CL_ASSERT(p_lock->state == CL_INITIALIZED);

	status = pthread_rwlock_wrlock(&p_lock->lock);
	CL_ASSERT(status == 0);
}

/*
* PARAMETERS
*	p_lock
*		[in] Pointer to a cl_plock_t structure to acquire exclusively.
*
* RETURN VALUE
*	This function does not return a value.
*
* SEE ALSO
*	Passive Lock, cl_plock_release, cl_plock_acquire
*********/

/****f* Component Library: Passive Lock/cl_plock_release
* NAME
*	cl_plock_release
*
* DESCRIPTION
*	The cl_plock_release function releases a passive lock from
*	shared or exclusive access.
*
* SYNOPSIS
*/
static inline void cl_plock_release(IN cl_plock_t * const p_lock)
{
	cl_status_t __attribute__((unused)) status;
	CL_ASSERT(p_lock);
	CL_ASSERT(p_lock->state == CL_INITIALIZED);

	status = pthread_rwlock_unlock(&p_lock->lock);
	CL_ASSERT(status == 0);
}

/*
* PARAMETERS
*	p_lock
*		[in] Pointer to a cl_plock_t structure to release.
*
* RETURN VALUE
*	This function does not return a value.
*
* SEE ALSO
*	Passive Lock, cl_plock_acquire, cl_plock_excl_acquire
*********/

END_C_DECLS
#endif				/* _CL_PASSIVE_LOCK_H_ */
