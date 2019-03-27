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
 *	Declaration of atomic manipulation functions.
 */

#ifndef _CL_ATOMIC_H_
#define _CL_ATOMIC_H_

#include <complib/cl_atomic_osd.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
/****h* Component Library/Atomic Operations
* NAME
*	Atomic Operations
*
* DESCRIPTION
*	The Atomic Operations functions allow callers to operate on
*	32-bit signed integers in an atomic fashion.
*********/
/****f* Component Library: Atomic Operations/cl_atomic_inc
* NAME
*	cl_atomic_inc
*
* DESCRIPTION
*	The cl_atomic_inc function atomically increments a 32-bit signed
*	integer and returns the incremented value.
*
* SYNOPSIS
*/
int32_t cl_atomic_inc(IN atomic32_t * const p_value);
/*
* PARAMETERS
*	p_value
*		[in] Pointer to a 32-bit integer to increment.
*
* RETURN VALUE
*	Returns the incremented value pointed to by p_value.
*
* NOTES
*	The provided value is incremented and its value returned in one atomic
*	operation.
*
*	cl_atomic_inc maintains data consistency without requiring additional
*	synchronization mechanisms in multi-threaded environments.
*
* SEE ALSO
*	Atomic Operations, cl_atomic_dec, cl_atomic_add, cl_atomic_sub
*********/

/****f* Component Library: Atomic Operations/cl_atomic_dec
* NAME
*	cl_atomic_dec
*
* DESCRIPTION
*	The cl_atomic_dec function atomically decrements a 32-bit signed
*	integer and returns the decremented value.
*
* SYNOPSIS
*/
int32_t cl_atomic_dec(IN atomic32_t * const p_value);
/*
* PARAMETERS
*	p_value
*		[in] Pointer to a 32-bit integer to decrement.
*
* RETURN VALUE
*	Returns the decremented value pointed to by p_value.
*
* NOTES
*	The provided value is decremented and its value returned in one atomic
*	operation.
*
*	cl_atomic_dec maintains data consistency without requiring additional
*	synchronization mechanisms in multi-threaded environments.
*
* SEE ALSO
*	Atomic Operations, cl_atomic_inc, cl_atomic_add, cl_atomic_sub
*********/

/****f* Component Library: Atomic Operations/cl_atomic_add
* NAME
*	cl_atomic_add
*
* DESCRIPTION
*	The cl_atomic_add function atomically adds a value to a
*	32-bit signed integer and returns the resulting value.
*
* SYNOPSIS
*/
int32_t
cl_atomic_add(IN atomic32_t * const p_value, IN const int32_t increment);
/*
* PARAMETERS
*	p_value
*		[in] Pointer to a 32-bit integer that will be added to.
*
*	increment
*		[in] Value by which to increment the integer pointed to by p_value.
*
* RETURN VALUE
*	Returns the value pointed to by p_value after the addition.
*
* NOTES
*	The provided increment is added to the value and the result returned in
*	one atomic operation.
*
*	cl_atomic_add maintains data consistency without requiring additional
*	synchronization mechanisms in multi-threaded environments.
*
* SEE ALSO
*	Atomic Operations, cl_atomic_inc, cl_atomic_dec, cl_atomic_sub
*********/

/****f* Component Library: Atomic Operations/cl_atomic_sub
* NAME
*	cl_atomic_sub
*
* DESCRIPTION
*	The cl_atomic_sub function atomically subtracts a value from a
*	32-bit signed integer and returns the resulting value.
*
* SYNOPSIS
*/
int32_t
cl_atomic_sub(IN atomic32_t * const p_value, IN const int32_t decrement);
/*
* PARAMETERS
*	p_value
*		[in] Pointer to a 32-bit integer that will be subtracted from.
*
*	decrement
*		[in] Value by which to decrement the integer pointed to by p_value.
*
* RETURN VALUE
*	Returns the value pointed to by p_value after the subtraction.
*
* NOTES
*	The provided decrement is subtracted from the value and the result
*	returned in one atomic operation.
*
*	cl_atomic_sub maintains data consistency without requiring additional
*	synchronization mechanisms in multi-threaded environments.
*
* SEE ALSO
*	Atomic Operations, cl_atomic_inc, cl_atomic_dec, cl_atomic_add
*********/

END_C_DECLS
#endif				/* _CL_ATOMIC_H_ */
