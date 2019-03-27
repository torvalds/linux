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

#ifndef _OSMV_RMPP_SENDER_H_
#define _OSMV_RMPP_SENDER_H_

#include <vendor/osm_vendor_mlx.h>
#include <vendor/osm_vendor_mlx_txn.h>
#include <opensm/osm_madw.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
/****d* OSM Vendor/osmv_simple_send_madw
 * NAME
 *   osmv_simple_send_madw
 *
 * DESCRIPTION
 *   Send a single MAD (256 bytes).
 *
 *   If this MAD requires a response, set the timeout event.
 *   The function call returns when the MAD's send completion is received.
 *
 */
ib_api_status_t
osmv_simple_send_madw(IN osm_bind_handle_t h_bind,
		      IN osm_madw_t * const p_madw,
		      IN osmv_txn_ctx_t * p_txn, IN boolean_t is_retry);

/****d* OSM Vendor/osmv_rmpp_send_madw
 * NAME
 *   osmv_rmpp_send_madw
 *
 * DESCRIPTION
 *	Send a single MAD wrapper (of arbitrary length).
 *      Follow the RMPP semantics
 *      (segmentation, send window, timeouts etc).
 *
 *      The function call returns either when the whole MAD
 *      has been acknowledged, or upon error.
 */
ib_api_status_t
osmv_rmpp_send_madw(IN osm_bind_handle_t h_bind,
		    IN osm_madw_t * const p_madw,
		    IN osmv_txn_ctx_t * p_txn, IN boolean_t is_rmpp_ds);

/*
 *  NAME            osmv_rmpp_send_ack
 *
 *  DESCRIPTION
 */

ib_api_status_t
osmv_rmpp_send_ack(IN osm_bind_handle_t h_bind,
		   IN const ib_mad_t * p_req_mad,
		   IN uint32_t seg_num,
		   IN uint32_t nwl, IN const osm_mad_addr_t * p_mad_addr);

/*
 *  NAME           osmv_rmpp_send_nak
 *
 *  DESCRIPTION    Send the RMPP ABORT or STOP packet
 */

ib_api_status_t
osmv_rmpp_send_nak(IN osm_bind_handle_t h_bind,
		   IN const ib_mad_t * p_req_mad,
		   IN const osm_mad_addr_t * p_mad_addr,
		   IN uint8_t nak_type, IN uint8_t status);

/*
 *  NAME           osmv_rmpp_snd_error
 *
 *  DESCRIPTION    Mark an error status and signal the sender thread to handle it
 */

static inline void
osmv_rmpp_snd_error(IN osmv_rmpp_send_ctx_t * p_send_ctx,
		    IN ib_api_status_t status)
{
	p_send_ctx->status = status;

	/* Release the thread waiting on send()
	 * It will release the transaction's context
	 */
	cl_event_signal(&p_send_ctx->event);
}

END_C_DECLS
#endif				/* _OSMV_RMPP_SENDER_H_ */
