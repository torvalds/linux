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

/*
 * Abstract:
 *	Declaration of thread abstraction and thread related operations.
 */

#ifndef _CL_THREAD_H_
#define _CL_THREAD_H_

#include <complib/cl_thread_osd.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
/****i* Component Library/Thread
* NAME
*	Thread
*
* DESCRIPTION
*	The Thread provides a separate thread of execution.
*
*	The cl_thread_t structure should be treated as opaque and should be
*	manipulated only through the provided functions.
*********/
/****d* Component Library: Thread/cl_pfn_thread_callback_t
* NAME
*	cl_pfn_thread_callback_t
*
* DESCRIPTION
*	The cl_pfn_thread_callback_t function type defines the prototype
*	for functions invoked by thread objects
*
* SYNOPSIS
*/
typedef void (*cl_pfn_thread_callback_t) (IN void *context);
/*
* PARAMETERS
*	context
*		[in] Value specified in a call to cl_thread_init.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	This function type is provided as function prototype reference for
*	the function provided by users as a parameter to cl_thread_init.
*
* SEE ALSO
*	Thread Pool
*********/

/****i* Component Library: Thread/cl_thread_t
* NAME
*	cl_thread_t
*
* DESCRIPTION
*	Thread structure.
*
*	The cl_thread_t structure should be treated as opaque and should be
*	manipulated only through the provided functions.
*
* SYNOPSIS
*/
typedef struct _cl_thread {
	cl_thread_osd_t osd;
	cl_pfn_thread_callback_t pfn_callback;
	const void *context;
	char name[16];
} cl_thread_t;
/*
* FIELDS
*	osd
*		Implementation specific structure for managing thread information.
*
*	pfn_callback
*		Callback function for the thread to invoke.
*
*	context
*		Context to pass to the thread callback function.
*
*	name
*		Name to assign to the thread.
*
* SEE ALSO
*	Thread
*********/

/****i* Component Library: Thread/cl_thread_construct
* NAME
*	cl_thread_construct
*
* DESCRIPTION
*	The cl_thread_construct function initializes the state of a thread.
*
* SYNOPSIS
*/
void cl_thread_construct(IN cl_thread_t * const p_thread);
/*
* PARAMETERS
*	p_thread
*		[in] Pointer to a cl_thread_t structure whose state to initialize.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Allows calling cl_thread_destroy without first calling cl_thread_init.
*
*	Calling cl_thread_construct is a prerequisite to calling any other
*	thread function except cl_thread_init.
*
* SEE ALSO
*	Thread, cl_thread_init, cl_thread_destroy
*********/

/****i* Component Library: Thread/cl_thread_init
* NAME
*	cl_thread_init
*
* DESCRIPTION
*	The cl_thread_init function creates a new thread of execution.
*
* SYNOPSIS
*/
cl_status_t
cl_thread_init(IN cl_thread_t * const p_thread,
	       IN cl_pfn_thread_callback_t pfn_callback,
	       IN const void *const context, IN const char *const name);
/*
* PARAMETERS
*	p_thread
*		[in] Pointer to a cl_thread_t structure to initialize.
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
*		[in] Name to associate with the thread.  The name may be up to 16
*		characters, including a terminating null character.
*
* RETURN VALUES
*	CL_SUCCESS if thread creation succeeded.
*
*	CL_ERROR if thread creation failed.
*
* NOTES
*	The thread created with cl_thread_init will invoke the callback
*	specified by the callback parameter with context as single parameter.
*
*	The callback function is invoked once, and the thread exits when the
*	callback returns.
*
*	It is invalid to call cl_thread_destroy from the callback function,
*	as doing so will result in a deadlock.
*
* SEE ALSO
*	Thread, cl_thread_construct, cl_thread_destroy, cl_thread_suspend,
*	cl_thread_stall, cl_pfn_thread_callback_t
*********/

/****i* Component Library: Thread/cl_thread_destroy
* NAME
*	cl_thread_destroy
*
* DESCRIPTION
*	The cl_thread_destroy function performs any necessary cleanup to free
*	resources associated with the specified thread.
*
* SYNOPSIS
*/
void cl_thread_destroy(IN cl_thread_t * const p_thread);
/*
* PARAMETERS
*	p_thread
*		[in] Pointer to a cl_thread_t structure to destroy.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	This function blocks until the thread exits and must not be called by the
*	thread itself.  Callers must therefore ensure that such a blocking call is
*	possible from the context of the call.
*
*	This function must only be called after a call to cl_thread_construct or
*	cl_thread_init.
*
* SEE ALSO
*	Thread, cl_thread_construct, cl_thread_init
*********/

/****f* Component Library: Thread/cl_thread_suspend
* NAME
*	cl_thread_suspend
*
* DESCRIPTION
*	The cl_thread_suspend function suspends the calling thread for a minimum
*	of the specified number of milliseconds.
*
* SYNOPSIS
*/
void cl_thread_suspend(IN const uint32_t pause_ms);
/*
* PARAMETERS
*	pause_ms
*		[in] Number of milliseconds to suspend the calling thread.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	This function should only be called if it is valid for the caller's thread
*	to enter a wait state. For stalling a thread that cannot enter a wait
*	state, callers should use cl_thread_stall.
*
* SEE ALSO
*	Thread, cl_thread_stall
*********/

/****f* Component Library: Thread/cl_thread_stall
* NAME
*	cl_thread_stall
*
* DESCRIPTION
*	The cl_thread_stall function stalls the calling thread for a minimum of
*	the specified number of microseconds.
*
* SYNOPSIS
*/
void cl_thread_stall(IN const uint32_t pause_us);
/*
* PARAMETERS
*	pause_us
*		[in] Number of microseconds to stall the calling thread.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	The cl_thread_stall function performs a busy wait for the specified
*	number of microseconds. Care should be taken when using this function as
*	it does not relinquish its quantum of operation. For longer wait
*	operations, users should call cl_thread_suspend if possible.
*
* SEE ALSO
*	Thread, cl_thread_suspend
*********/

/****f* Component Library: Thread/cl_proc_count
* NAME
*	cl_proc_count
*
* DESCRIPTION
*	The cl_proc_count function returns the number of processors in the system.
*
* SYNOPSIS
*/
int cl_proc_count(void);
/*
* RETURN VALUE
*	Returns the number of processors in the system.
*********/

/****i* Component Library: Thread/cl_is_current_thread
* NAME
*	cl_is_current_thread
*
* DESCRIPTION
*	The cl_is_current_thread function compares the calling thread to the
*	specified thread and returns whether they are the same.
*
* SYNOPSIS
*/
boolean_t cl_is_current_thread(IN const cl_thread_t * const p_thread);
/*
* PARAMETERS
*	p_thread
*		[in] Pointer to a cl_thread_t structure to compare to the
*		caller's thead.
*
* RETURN VALUES
*	TRUE if the thread specified by the p_thread parameter is the
*	calling thread.
*
*	FALSE otherwise.
*
* SEE ALSO
*	Thread, cl_threadinit_t
*********/

/****f* Component Library: Thread/cl_is_blockable
* NAME
*	cl_is_blockable
*
* DESCRIPTION
*	The cl_is_blockable indicates if the current caller context is
*	blockable.
*
* SYNOPSIS
*/
boolean_t cl_is_blockable(void);
/*
* RETURN VALUE
*	TRUE
*		Current caller context can be blocked, i.e it is safe to perform
*		a sleep, or call a down operation on a semaphore.
*
*********/

END_C_DECLS
#endif				/* _CL_THREAD_H_ */
