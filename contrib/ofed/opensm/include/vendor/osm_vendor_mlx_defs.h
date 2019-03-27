/*
 * Copyright (c) 2004, 2005 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2005,2009 Mellanox Technologies LTD. All rights reserved.
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

#ifndef _OSMV_DEFS_H_
#define _OSMV_DEFS_H_

#include <vendor/osm_vendor_mlx_inout.h>
#include <opensm/osm_log.h>
#include <vendor/osm_vendor_api.h>
#include <vendor/osm_vendor_mlx_txn.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
/* The maximum number of outstanding MADs an RMPP sender can transmit */
#define OSMV_RMPP_RECV_WIN       16
/* Transaction Timeout = OSMV_TXN_TIMEOUT_FACTOR * Response Timeout */
#define OSMV_TXN_TIMEOUT_FACTOR  128
/************/
/****s* OSM Vendor: Types/osmv_bind_obj_t
* NAME
*	osmv_bind_obj_t
*
* DESCRIPTION
*	The object managing a single bind context.
*       The bind handle is a direct pointer to it.
*
* SYNOPSIS
*/
typedef struct _osmv_bind_obj {
	/* Used to signal when the struct is being destroyed */
	struct _osmv_bind_obj *magic_ptr;

	 osm_vendor_t /*const */  * p_vendor;

	uint32_t hca_hndl;
	uint32_t port_num;

	/* Atomic access protector */
	cl_spinlock_t lock;

	/* is_closing == TRUE --> the handle is being unbound */
	boolean_t is_closing;

	/* Event callbacks */
	osm_vend_mad_recv_callback_t recv_cb;
	osm_vend_mad_send_err_callback_t send_err_cb;
	/* ... and their context */
	void *cb_context;

	/* A pool to manage MAD wrappers */
	osm_mad_pool_t *p_osm_pool;

	/* each subvendor implements its own transport mgr */
	void *p_transp_mgr;

	/* The transaction DB */
	osmv_txn_mgr_t txn_mgr;

} osmv_bind_obj_t;

END_C_DECLS
#endif				/* _OSMV_DEFS_H_ */
