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
 * 	Declaration of osm_mad_wrapper_t.
 *	This object represents the context wrapper for OpenSM MAD processing.
 *	This object is part of the OpenSM family of objects.
 */

#ifndef _OSM_UMADT_h_
#define _OSM_UMADT_h_

#include "iba/ib_types.h"
#include <vendor/osm_vendor_api.h>
#include <opensm/osm_log.h>
#include "umadt.h"
#include "ibt.h"

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS

typedef struct _umadt_obj_t {
	void *umadt_handle;
	UMADT_INTERFACE uMadtInterface;
	IBT_INTERFACE IbtInterface;
	boolean init_done;
	cl_spinlock_t register_lock;
	cl_qlist_t register_list;
	osm_log_t *p_log;
	uint32_t timeout;

} umadt_obj_t;
/*********/

/****s* OpenSM: Umadt MAD Wrapper/osm_bind_info
* NAME
*	osm_bind_info
*
* DESCRIPTION
*	Context needed for processing individual MADs
*
* SYNOPSIS
*/

typedef struct _mad_bind_info_t {
	cl_list_item_t list_item;
	umadt_obj_t *p_umadt_obj;
	osm_mad_pool_t *p_mad_pool;
	osm_vend_mad_recv_callback_t mad_recv_callback;
	void *client_context;
	cl_thread_t recv_processor_thread;
	cl_spinlock_t trans_ctxt_lock;
	cl_qlist_t trans_ctxt_list;
	cl_timer_t timeout_timer;
	cl_spinlock_t timeout_list_lock;
	cl_qlist_t timeout_list;
	RegisterClassStruct umadt_reg_class;
	MADT_HANDLE umadt_handle;	/* Umadt type */

} mad_bind_info_t;

typedef struct _trans_context_t {
	cl_list_item_t list_item;
	uint64_t trans_id;
	uint64_t sent_time;	/* micro secs */
	void *context;
} trans_context_t;

/*
* FIELDS
*	list_item
*		List linkage for pools and lists.  MUST BE FIRST MEMBER!
*
*	p_mad_pool
*		Pointer to the MAD pool to be used by mads with this bind handle.
*
*	mad_recv_callback
*		Callback function called by the mad receive processor.
*
*	client_context
*		context to be passed to the receive callback.
*
*	recv_processor_thread
*		Thread structure for the receive processor thread.
*
*	umadt_reg_class
*		Umadt register class struct used to register with Umadt.
*
*	umadt_handle
*		Umadt returns this handle from a registration call. The transport layer
*		uses this handle to talk to Umadt.
*
* SEE ALSO
*********/

END_C_DECLS
#endif /*_OSM_UMADT_h_ */
