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
 *	Debug Macros.
 */

#ifndef _CL_DEBUG_OSD_H_
#define _CL_DEBUG_OSD_H_

#include <complib/cl_types.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
#if !defined(__MODULE__)
#define __MODULE__			""
#define __MOD_DELIMITER__	""
#else				/* !defined(__MODULE__) */
#define __MOD_DELIMITER__	":"
#endif				/* !defined(__MODULE__) */
/*
 * Define specifiers for print functions based on the platform
 */
#ifdef __IA64__
#define PRIdSIZE_T	"ld"
#else
#define PRIdSIZE_T	"d"
#endif
#include <inttypes.h>
#include <stdio.h>
#define cl_msg_out	printf
#if defined( _DEBUG_ )
#define cl_dbg_out	printf
#else
#define cl_dbg_out	foo
#endif				/* _DEBUG_ */
/*
 * The following macros are used internally by the CL_ENTER, CL_TRACE,
 * CL_TRACE_EXIT, and CL_EXIT macros.
 */
#define _CL_DBG_ENTER	\
	("%s%s%s() [\n", __MODULE__, __MOD_DELIMITER__, __func__)
#define _CL_DBG_EXIT	\
	("%s%s%s() ]\n", __MODULE__, __MOD_DELIMITER__, __func__)
#define _CL_DBG_INFO	\
	("%s%s%s(): ", __MODULE__, __MOD_DELIMITER__, __func__)
#define _CL_DBG_ERROR	\
	("%s%s%s() !ERROR!: ", __MODULE__, __MOD_DELIMITER__, __func__)
#define CL_CHK_STK
END_C_DECLS
#endif				/* _CL_DEBUG_OSD_H_ */
