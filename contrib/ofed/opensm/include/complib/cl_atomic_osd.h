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
 *	Implementation specific header files for atomic operations.
 */

#ifndef _CL_ATOMIC_OSD_H_
#define _CL_ATOMIC_OSD_H_

#include <complib/cl_types.h>
#include <complib/cl_spinlock.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS

extern cl_spinlock_t cl_atomic_spinlock;

static inline int32_t cl_atomic_inc(IN atomic32_t * const p_value)
{
	int32_t new_val;

	cl_spinlock_acquire(&cl_atomic_spinlock);
	new_val = *p_value + 1;
	*p_value = new_val;
	cl_spinlock_release(&cl_atomic_spinlock);
	return (new_val);
}

static inline int32_t cl_atomic_dec(IN atomic32_t * const p_value)
{
	int32_t new_val;

	cl_spinlock_acquire(&cl_atomic_spinlock);
	new_val = *p_value - 1;
	*p_value = new_val;
	cl_spinlock_release(&cl_atomic_spinlock);
	return (new_val);
}

static inline int32_t
cl_atomic_add(IN atomic32_t * const p_value, IN const int32_t increment)
{
	int32_t new_val;

	cl_spinlock_acquire(&cl_atomic_spinlock);
	new_val = *p_value + increment;
	*p_value = new_val;
	cl_spinlock_release(&cl_atomic_spinlock);
	return (new_val);
}

static inline int32_t
cl_atomic_sub(IN atomic32_t * const p_value, IN const int32_t decrement)
{
	int32_t new_val;

	cl_spinlock_acquire(&cl_atomic_spinlock);
	new_val = *p_value - decrement;
	*p_value = new_val;
	cl_spinlock_release(&cl_atomic_spinlock);
	return (new_val);
}

END_C_DECLS
#endif				/* _CL_ATOMIC_OSD_H_ */
