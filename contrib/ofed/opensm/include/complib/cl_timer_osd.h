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
 *	Declaration of timer object.
 */

#ifndef _CL_TIMER_OSD_H_
#define _CL_TIMER_OSD_H_

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
#include <complib/cl_qlist.h>
#include <pthread.h>
typedef enum _cl_timer_state {
	CL_TIMER_IDLE,
	CL_TIMER_QUEUED,
	CL_TIMER_RUNNING
} cl_timer_state_t;

typedef struct _cl_timer_t {
	cl_list_item_t list_item;
	cl_timer_state_t timer_state;
	cl_state_t state;
	cl_pfn_timer_callback_t pfn_callback;
	const void *context;
	pthread_cond_t cond;
	struct timespec timeout;
} cl_timer_t;

/* Internal functions to create the timer provider. */
cl_status_t __cl_timer_prov_create(void);

/* Internal function to destroy the timer provider. */
void __cl_timer_prov_destroy(void);

END_C_DECLS
#endif				/* _CL_TIMER_OSD_H_ */
