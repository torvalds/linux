/*
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2012 Mellanox Technologies LTD. All rights reserved.
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
 *	Defines sized datatypes for Linux User mode
 *  exported sizes are int8_t, uint8_t, int16_t, uint16_t, int32_t, uint32_t
 *  int64_t, uint64_t.
 */

#ifndef _CL_TYPES_OSD_H_
#define _CL_TYPES_OSD_H_

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
#if defined (_DEBUG_)
#ifdef __IA64__
#define cl_break() asm("   break 0")
#else				/* __IA64__ */
#define cl_break() asm("   int $3")
#endif				/* __IA64__ */
#else				/* _DEBUG_ */
#define cl_break
#endif
#include <inttypes.h>
#include <assert.h>
#include <string.h>

/*
 * Branch prediction hints
 */
#if defined(HAVE_BUILTIN_EXPECT)
#define PT(exp)    __builtin_expect( ((uintptr_t)(exp)), 1 )
#define PF(exp)    __builtin_expect( ((uintptr_t)(exp)), 0 )
#else
#define PT(exp)    (exp)
#define PF(exp)    (exp)
#endif

#if defined (_DEBUG_)
#define CL_ASSERT	assert
#else				/* _DEBUG_ */
#define CL_ASSERT( __exp__ )
#endif				/* _DEBUG_ */
/*
 * Types not explicitly defined are native to the platform.
 */
typedef int boolean_t;
typedef volatile int32_t atomic32_t;

#ifndef NULL
#define NULL	(void*)0
#endif

#define UNUSED_PARAM( P )

END_C_DECLS
#endif				/* _CL_TYPES_OSD_H_ */
