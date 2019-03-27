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

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <complib/cl_types.h>
#include <complib/cl_debug.h>
#include <complib/cl_spinlock.h>

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

/*
 *  Prototypes
 */

extern cl_status_t __cl_timer_prov_create(void);

extern void __cl_timer_prov_destroy(void);

cl_spinlock_t cl_atomic_spinlock;

void complib_init(void)
{
	cl_status_t status = CL_SUCCESS;

	status = cl_spinlock_init(&cl_atomic_spinlock);
	if (status != CL_SUCCESS)
		goto _error;

	status = __cl_timer_prov_create();
	if (status != CL_SUCCESS)
		goto _error;
	return;

_error:
	cl_msg_out("__init: failed to create complib (%s)\n",
		   CL_STATUS_MSG(status));
	exit(1);
}

void complib_exit(void)
{
	__cl_timer_prov_destroy();
	cl_spinlock_destroy(&cl_atomic_spinlock);
}

boolean_t cl_is_debug(void)
{
#if defined( _DEBUG_ )
	return TRUE;
#else
	return FALSE;
#endif				/* defined( _DEBUG_ ) */
}
