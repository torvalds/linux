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
 *	Declaration of spin lock object.
 */

#ifndef _CL_SPINLOCK_H_
#define _CL_SPINLOCK_H_

#include <complib/cl_spinlock_osd.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
/****h* Public/Spinlock
* NAME
*	Spinlock
*
* DESCRIPTION
*	Spinlock provides synchronization between threads for exclusive access to
*	a resource.
*
*	The spinlock functions manipulate a cl_spinlock_t structure which should
*	be treated as opaque and should be manipulated only through the provided
*	functions.
*
* SEE ALSO
*	Structures:
*		cl_spinlock_t
*
*	Initialization:
*		cl_spinlock_construct, cl_spinlock_init, cl_spinlock_destroy
*
*	Manipulation
*		cl_spinlock_acquire, cl_spinlock_release
*********/
/****f* Component Library: Spinlock/cl_spinlock_construct
* NAME
*	cl_spinlock_construct
*
* DESCRIPTION
*	The cl_spinlock_construct function initializes the state of a
*	spin lock.
*
* SYNOPSIS
*/
void cl_spinlock_construct(IN cl_spinlock_t * const p_spinlock);
/*
* PARAMETERS
*	p_spin_lock
*		[in] Pointer to a spin lock structure whose state to initialize.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Allows calling cl_spinlock_destroy without first calling
*	cl_spinlock_init.
*
*	Calling cl_spinlock_construct is a prerequisite to calling any other
*	spin lock function except cl_spinlock_init.
*
* SEE ALSO
*	Spinlock, cl_spinlock_init, cl_spinlock_destroy
*********/

/****f* Component Library: Spinlock/cl_spinlock_init
* NAME
*	cl_spinlock_init
*
* DESCRIPTION
*	The cl_spinlock_init function initializes a spin lock for use.
*
* SYNOPSIS
*/
cl_status_t cl_spinlock_init(IN cl_spinlock_t * const p_spinlock);
/*
* PARAMETERS
*	p_spin_lock
*		[in] Pointer to a spin lock structure to initialize.
*
* RETURN VALUES
*	CL_SUCCESS if initialization succeeded.
*
*	CL_ERROR if initialization failed. Callers should call
*	cl_spinlock_destroy to clean up any resources allocated during
*	initialization.
*
* NOTES
*	Initialize the spin lock structure. Allows calling cl_spinlock_aquire
*	and cl_spinlock_release.
*
* SEE ALSO
*	Spinlock, cl_spinlock_construct, cl_spinlock_destroy,
*	cl_spinlock_acquire, cl_spinlock_release
*********/

/****f* Component Library: Spinlock/cl_spinlock_destroy
* NAME
*	cl_spinlock_destroy
*
* DESCRIPTION
*	The cl_spinlock_destroy function performs all necessary cleanup of a
*	spin lock.
*
* SYNOPSIS
*/
void cl_spinlock_destroy(IN cl_spinlock_t * const p_spinlock);
/*
* PARAMETERS
*	p_spin_lock
*		[in] Pointer to a spin lock structure to destroy.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Performs any necessary cleanup of a spin lock. This function must only
*	be called if either cl_spinlock_construct or cl_spinlock_init has been
*	called.
*
* SEE ALSO
*	Spinlock, cl_spinlock_construct, cl_spinlock_init
*********/

/****f* Component Library: Spinlock/cl_spinlock_acquire
* NAME
*	cl_spinlock_acquire
*
* DESCRIPTION
*	The cl_spinlock_acquire function acquires a spin lock.
*	This version of lock does not prevent an interrupt from
*	occuring on the processor on which the code is being
*	executed.
*
* SYNOPSIS
*/
void cl_spinlock_acquire(IN cl_spinlock_t * const p_spinlock);
/*
* PARAMETERS
*	p_spin_lock
*		[in] Pointer to a spin lock structure to acquire.
*
* RETURN VALUE
*	This function does not return a value.
*
* SEE ALSO
*	Spinlock, cl_spinlock_release
*********/

/****f* Component Library: Spinlock/cl_spinlock_release
* NAME
*	cl_spinlock_release
*
* DESCRIPTION
*	The cl_spinlock_release function releases a spin lock object.
*
* SYNOPSIS
*/
void cl_spinlock_release(IN cl_spinlock_t * const p_spinlock);
/*
* PARAMETERS
*	p_spin_lock
*		[in] Pointer to a spin lock structure to release.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Releases a spin lock after a call to cl_spinlock_acquire.
*
* SEE ALSO
*	Spinlock, cl_spinlock_acquire
*********/

END_C_DECLS
#endif				/* _CL_SPINLOCK_H_ */
