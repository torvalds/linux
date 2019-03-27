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

#ifndef _OSM_BIND_H_
#define _OSM_BIND_H_

#include <opensm/osm_helper.h>
#include <vendor/osm_vendor_mtl.h>
#include <vendor/osm_vendor_api.h>
#include <opensm/osm_subnet.h>
#include <opensm/osm_opensm.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
/****s* OpenSM: Vendor/osm_vendor_mgt_bind
* NAME
*   osm_vendor_mgt_bind_t
*
* DESCRIPTION
* 	Tracks the handles returned by IB_MGT to the SMI and GSI
*  Nulled on init of the vendor obj. Populated on first bind.
*
* SYNOPSIS
*/
typedef struct _osm_vendor_mgt_bind {
	boolean_t smi_init, gsi_init;
	IB_MGT_mad_hndl_t smi_mads_hdl;
	IB_MGT_mad_hndl_t gsi_mads_hdl;
	struct _osm_mtl_bind_info *smi_p_bind;
} osm_vendor_mgt_bind_t;

/*
* FIELDS
*	smi_mads_hdl
*		Handle returned by IB_MGT_get_handle to the IB_MGT_SMI
*
*	gsi_mads_hdl
*		Handle returned by IB_MGT_get_handle to the IB_MGT_GSI
*
* SEE ALSO
*********/

/****s* OpenSM: Vendor osm_mtl_bind_info_t
* NAME
*   osm_mtl_bind_info_t
*
* DESCRIPTION
* 	Handle to the result of binding a class callbacks to IB_MGT.
*
* SYNOPSIS
*/
typedef struct _osm_mtl_bind_info {
	IB_MGT_mad_hndl_t mad_hndl;
	osm_vendor_t *p_vend;
	void *client_context;
	VAPI_hca_hndl_t hca_hndl;
	VAPI_hca_id_t hca_id;
	uint8_t port_num;
	osm_vend_mad_recv_callback_t rcv_callback;
	osm_vend_mad_send_err_callback_t send_err_callback;
	osm_mad_pool_t *p_osm_pool;
} osm_mtl_bind_info_t;

/*
* FIELDS
*	mad_hndl
*		the handle returned from the registration in IB_MGT
*
*	p_vend
*		Pointer to the vendor object.
*
*	client_context
*		User's context passed during osm_bind
*
*  hca_id
*     HCA Id we bind to.
*
*	port_num
*		Port number (within the HCA) of the bound port.
*
*	rcv_callback
*		OSM Callback function to be called on receive of MAD.
*
*  send_err_callback
*     OSM Callback to be called on send error.
*
*  p_osm_pool
*     Points to the MAD pool used by OSM
*
*
* SEE ALSO
*********/
ib_api_status_t
osm_mtl_send_mad(IN osm_mtl_bind_info_t * p_bind, IN osm_madw_t * const p_madw);

END_C_DECLS
#endif				// _OSM_BIND_H_
