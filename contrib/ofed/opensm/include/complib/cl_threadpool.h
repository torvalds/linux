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
 *	Declaration of thread pool.
 */

#ifndef _CL_THREAD_POOL_H_
#define _CL_THREAD_POOL_H_

#include <pthread.h>
#include <complib/cl_types.h>
#include <complib/cl_thread.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
/****h* Component Library/Thread Pool
* NAME
*	Thread Pool
*
* DESCRIPTION
*	The Thread Pool manages a user specified number of threads.
*
*	Each thread in the thread pool waits for a user initiated signal before
*	invoking a user specified callback function. All threads in the thread
*	pool invoke the same callback function.
*
*	The thread pool functions operate on a cl_thread_pool_t structure which
*	should be treated as opaque, and should be manipulated only through the
*	provided functions.
*
* SEE ALSO
*	Structures:
*		cl_thread_pool_t
*
*	Initialization:
*		cl_thread_pool_init, cl_thread_pool_destroy
*
*	Manipulation
*		cl_thread_pool_signal
*********/
/****s* Component Library: Thread Pool/cl_thread_pool_t
* NAME
*	cl_thread_pool_t
*
* DESCRIPTION
*	Thread pool structure.
*
*	The cl_thread_pool_t structure should be treated as opaque, and should be
*	manipulated only through the provided functions.
*
* SYNOPSIS
*/
typedef struct _cl_thread_pool {
	void (*pfn_callback) (void *);
	void *context;
	unsigned running_count;
	unsigned events;
	pthread_cond_t cond;
	pthread_mutex_t mutex;
	pthread_t *tid;
} cl_thread_pool_t;
/*
* FIELDS
*	pfn_callback
*		Callback function for the thread to invoke.
*
*	context
*		Context to pass to the thread callback function.
*
*	running_count
*		Number of threads running.
*
*	events
*		events counter
*
*	mutex
*		mutex for cond variable protection
*
*	cond
*		conditional variable to signal an event to thread
*
*	tid
*		array of allocated thread ids.
*
* SEE ALSO
*	Thread Pool
*********/

/****f* Component Library: Thread Pool/cl_thread_pool_init
* NAME
*	cl_thread_pool_init
*
* DESCRIPTION
*	The cl_thread_pool_init function creates the threads to be
*	managed by a thread pool.
*
* SYNOPSIS
*/
cl_status_t
cl_thread_pool_init(IN cl_thread_pool_t * const p_thread_pool,
		    IN unsigned count,
		    IN void (*pfn_callback) (void *),
		    IN void *context, IN const char *const name);
/*
* PARAMETERS
*	p_thread_pool
*		[in] Pointer to a thread pool structure to initialize.
*
*	thread_count
*		[in] Number of threads to be managed by the thread pool.
*
*	pfn_callback
*		[in] Address of a function to be invoked by a thread.
*		See the cl_pfn_thread_callback_t function type definition for
*		details about the callback function.
*
*	context
*		[in] Value to pass to the callback function.
*
*	name
*		[in] Name to associate with the threads.  The name may be up to 16
*		characters, including a terminating null character.  All threads
*		created in the pool have the same name.
*
* RETURN VALUES
*	CL_SUCCESS if the thread pool creation succeeded.
*
*	CL_INSUFFICIENT_MEMORY if there was not enough memory to inititalize
*	the thread pool.
*
*	CL_ERROR if the threads could not be created.
*
* NOTES
*	cl_thread_pool_init creates and starts the specified number of threads.
*	If thread_count is zero, the thread pool creates as many threads as there
*	are processors in the system.
*
* SEE ALSO
*	Thread Pool, cl_thread_pool_destroy,
*	cl_thread_pool_signal, cl_pfn_thread_callback_t
*********/

/****f* Component Library: Thread Pool/cl_thread_pool_destroy
* NAME
*	cl_thread_pool_destroy
*
* DESCRIPTION
*	The cl_thread_pool_destroy function performs any necessary cleanup
*	for a thread pool.
*
* SYNOPSIS
*/
void cl_thread_pool_destroy(IN cl_thread_pool_t * const p_thread_pool);
/*
* PARAMETERS
*	p_thread_pool
*		[in] Pointer to a thread pool structure to destroy.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	This function blocks until all threads exit, and must therefore not
*	be called from any of the thread pool's threads. Because of its blocking
*	nature, callers of cl_thread_pool_destroy must ensure that entering a wait
*	state is valid from the calling thread context.
*
*	This function should only be called after a call to
*	cl_thread_pool_init.
*
* SEE ALSO
*	Thread Pool, cl_thread_pool_init
*********/

/****f* Component Library: Thread Pool/cl_thread_pool_signal
* NAME
*	cl_thread_pool_signal
*
* DESCRIPTION
*	The cl_thread_pool_signal function signals a single thread of
*	the thread pool to invoke the thread pool's callback function.
*
* SYNOPSIS
*/
cl_status_t cl_thread_pool_signal(IN cl_thread_pool_t * const p_thread_pool);
/*
* PARAMETERS
*	p_thread_pool
*		[in] Pointer to a thread pool structure to signal.
*
* RETURN VALUES
*	CL_SUCCESS if the thread pool was successfully signalled.
*
*	CL_ERROR otherwise.
*
* NOTES
*	Each call to this function wakes up at most one waiting thread in
*	the thread pool.
*
*	If all threads are running, cl_thread_pool_signal has no effect.
*
* SEE ALSO
*	Thread Pool
*********/

END_C_DECLS
#endif				/* _CL_THREAD_POOL_H_ */
