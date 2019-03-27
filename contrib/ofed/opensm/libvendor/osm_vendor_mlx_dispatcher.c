/*
 * Copyright (c) 2004-2008 Voltaire, Inc. All rights reserved.
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

#include <string.h>
#include <vendor/osm_vendor_mlx.h>
#include <vendor/osm_vendor_mlx_defs.h>
#include <vendor/osm_vendor_mlx_svc.h>
#include <vendor/osm_vendor_mlx_transport.h>
#include <vendor/osm_vendor_mlx_sender.h>
#include <vendor/osm_pkt_randomizer.h>

typedef enum _osmv_disp_route {

	OSMV_ROUTE_DROP,
	OSMV_ROUTE_SIMPLE,
	OSMV_ROUTE_RMPP,

} osmv_disp_route_t;

/**
 *   FORWARD REFERENCES TO PRIVATE FUNCTIONS
 */

static osmv_disp_route_t
__osmv_dispatch_route(IN osm_bind_handle_t h_bind,
		      IN const ib_mad_t * p_mad, OUT osmv_txn_ctx_t ** pp_txn);

static void
__osmv_dispatch_simple_mad(IN osm_bind_handle_t h_bind,
			   IN const ib_mad_t * p_mad,
			   IN osmv_txn_ctx_t * p_txn,
			   IN const osm_mad_addr_t * p_mad_addr);

static void
__osmv_dispatch_rmpp_mad(IN osm_bind_handle_t h_bind,
			 IN const ib_mad_t * p_mad,
			 IN osmv_txn_ctx_t * p_txn,
			 IN const osm_mad_addr_t * p_mad_addr);

static void
__osmv_dispatch_rmpp_snd(IN osm_bind_handle_t h_bind,
			 IN const ib_mad_t * p_mad,
			 IN osmv_txn_ctx_t * p_txn,
			 IN const osm_mad_addr_t * p_mad_addr);

static ib_api_status_t
__osmv_dispatch_rmpp_rcv(IN osm_bind_handle_t h_bind,
			 IN const ib_mad_t * p_mad,
			 IN osmv_txn_ctx_t * p_txn,
			 IN const osm_mad_addr_t * p_mad_addr);

static ib_api_status_t
__osmv_dispatch_accept_seg(IN osm_bind_handle_t h_bind,
			   IN osmv_txn_ctx_t * p_txn,
			   IN const ib_mad_t * p_mad);
static void
__osmv_dispatch_send_ack(IN osm_bind_handle_t h_bind,
			 IN const ib_mad_t * p_req_mad,
			 IN osmv_txn_ctx_t * p_txn,
			 IN const osm_mad_addr_t * p_mad_addr);

/*
 * NAME
 *   osmv_dispatch_mad
 *
 * DESCRIPTION
 *   Lower-level MAD dispatcher.
 *   Implements a switch between the following MAD consumers:
 *   (1) Non-RMPP consumer (DATA)
 *   (2) RMPP receiver     (DATA/ABORT/STOP)
 *   (3) RMPP sender       (ACK/ABORT/STOP)
 *
 * PARAMETERS
 *   h_bind                The bind handle
 *   p_mad_buf             The 256 byte buffer of individual MAD
 *   p_mad_addr            The MAD originator's address
 */

ib_api_status_t
osmv_dispatch_mad(IN osm_bind_handle_t h_bind,
		  IN const void *p_mad_buf,
		  IN const osm_mad_addr_t * p_mad_addr)
{
	ib_api_status_t ret = IB_SUCCESS;
	const ib_mad_t *p_mad = (ib_mad_t *) p_mad_buf;
	osmv_bind_obj_t *p_bo = (osmv_bind_obj_t *) h_bind;
	osmv_txn_ctx_t *p_txn = NULL;
	osm_log_t *p_log = p_bo->p_vendor->p_log;

	OSM_LOG_ENTER(p_bo->p_vendor->p_log);

	CL_ASSERT(NULL != h_bind && NULL != p_mad && NULL != p_mad_addr);

	osmv_txn_lock(p_bo);

	if (TRUE == p_bo->is_closing) {

		osm_log(p_bo->p_vendor->p_log, OSM_LOG_DEBUG,
			"The bind handle %p is being closed. "
			"The MAD will not be dispatched.\n", p_bo);

		ret = IB_INTERRUPTED;
		goto dispatch_mad_done;
	}

	/*
	   Add call for packet drop randomizer.
	   This is a testing feature. If run_randomizer flag is set to TRUE,
	   the randomizer will be called, and randomally will drop
	   a packet. This is used for simulating unstable fabric.
	 */
	if (p_bo->p_vendor->run_randomizer == TRUE) {
		/* Try the randomizer */
		if (osm_pkt_randomizer_mad_drop(p_bo->p_vendor->p_log,
						p_bo->p_vendor->
						p_pkt_randomizer,
						p_mad) == TRUE) {
			osm_log(p_bo->p_vendor->p_log, OSM_LOG_DEBUG,
				"The MAD will not be dispatched.\n");
			goto dispatch_mad_done;
		}
	}

	switch (__osmv_dispatch_route(h_bind, p_mad, &p_txn)) {

	case OSMV_ROUTE_DROP:
		break;		/* Do nothing */

	case OSMV_ROUTE_SIMPLE:
		__osmv_dispatch_simple_mad(h_bind, p_mad, p_txn, p_mad_addr);
		break;

	case OSMV_ROUTE_RMPP:
		__osmv_dispatch_rmpp_mad(h_bind, p_mad, p_txn, p_mad_addr);
		break;

	default:
		CL_ASSERT(FALSE);
	}

dispatch_mad_done:
	osmv_txn_unlock(p_bo);

	OSM_LOG_EXIT(p_log);
	return ret;
}

/*
 *  NAME            __osmv_dispatch_route()
 *
 *  DESCRIPTION     Decide which way to handle the received MAD: simple txn/RMPP/drop
 */

static osmv_disp_route_t
__osmv_dispatch_route(IN osm_bind_handle_t h_bind,
		      IN const ib_mad_t * p_mad, OUT osmv_txn_ctx_t ** pp_txn)
{
	ib_api_status_t ret;
	osmv_bind_obj_t *p_bo = (osmv_bind_obj_t *) h_bind;
	boolean_t is_resp = ib_mad_is_response(p_mad);
	boolean_t is_txn;
	uint64_t key = cl_ntoh64(p_mad->trans_id);

	CL_ASSERT(NULL != pp_txn);

	ret = osmv_txn_lookup(h_bind, key, pp_txn);
	is_txn = (IB_SUCCESS == ret);

	if (FALSE == is_txn && TRUE == is_resp) {
		osm_log(p_bo->p_vendor->p_log, OSM_LOG_DEBUG,
			"Received a response to a non-started/aged-out transaction (tid=0x%" PRIx64 "). "
			"Dropping the MAD.\n", key);
		return OSMV_ROUTE_DROP;
	}

	if (TRUE == osmv_mad_is_rmpp(p_mad)) {
		/* An RMPP transaction. The filtering is more delicate there */
		return OSMV_ROUTE_RMPP;
	}

	if (TRUE == is_txn && FALSE == is_resp) {
		/* Does this MAD try to start a transaction with duplicate tid? */
		osm_log(p_bo->p_vendor->p_log, OSM_LOG_DEBUG,
			"Duplicate TID 0x%" PRIx64 " received (not a response). "
			"Dropping the MAD.\n", key);

		return OSMV_ROUTE_DROP;
	}

	return OSMV_ROUTE_SIMPLE;
}

/*
 *  NAME            __osmv_dispatch_simple_mad()
 *
 *  DESCRIPTION     Handle a MAD that is part of non-RMPP transfer
 */

static void
__osmv_dispatch_simple_mad(IN osm_bind_handle_t h_bind,
			   IN const ib_mad_t * p_mad,
			   IN osmv_txn_ctx_t * p_txn,
			   IN const osm_mad_addr_t * p_mad_addr)
{
	osm_madw_t *p_madw;
	ib_mad_t *p_mad_buf;
	osm_madw_t *p_req_madw = NULL;
	osmv_bind_obj_t *p_bo = (osmv_bind_obj_t *) h_bind;

	OSM_LOG_ENTER(p_bo->p_vendor->p_log);

	/* Build the MAD wrapper to be returned to the user.
	 * The actual storage for the MAD is allocated there.
	 */
	p_madw =
	    osm_mad_pool_get(p_bo->p_osm_pool, h_bind, MAD_BLOCK_SIZE,
			     p_mad_addr);

	if (NULL == p_madw) {
		osm_log(p_bo->p_vendor->p_log, OSM_LOG_ERROR,
			"__osmv_dispatch_simple_mad: ERR 6501: "
			"Out Of Memory - could not allocate a buffer of size %d\n",
			MAD_BLOCK_SIZE);

		goto dispatch_simple_mad_done;
	}

	p_mad_buf = osm_madw_get_mad_ptr(p_madw);
	/* Copy the payload to the MAD buffer */
	memcpy((void *)p_mad_buf, (void *)p_mad, MAD_BLOCK_SIZE);

	if (NULL != p_txn) {
		/* This is a RESPONSE MAD. Pair it with the REQUEST MAD, pass upstream */
		p_req_madw = p_txn->p_madw;
		CL_ASSERT(NULL != p_req_madw);

		p_mad_buf->trans_id = cl_hton64(osmv_txn_get_tid(p_txn));
		osm_log(p_bo->p_vendor->p_log, OSM_LOG_DEBUG,
			"Restoring the original TID to 0x%" PRIx64 "\n",
			cl_ntoh64(p_mad_buf->trans_id));

		/* Reply matched, transaction complete */
		osmv_txn_done(h_bind, osmv_txn_get_key(p_txn), FALSE);
	} else {
		/* This is a REQUEST  MAD. Don't create a context, pass upstream */
	}

	/* Do the job ! */
	p_bo->recv_cb(p_madw, p_bo->cb_context, p_req_madw);

dispatch_simple_mad_done:
	OSM_LOG_EXIT(p_bo->p_vendor->p_log);
}

/*
 *  NAME            __osmv_dispatch_rmpp_mad()
 *
 *  DESCRIPTION     Handle a MAD that is part of RMPP transfer
 */

static void
__osmv_dispatch_rmpp_mad(IN osm_bind_handle_t h_bind,
			 IN const ib_mad_t * p_mad,
			 IN osmv_txn_ctx_t * p_txn,
			 IN const osm_mad_addr_t * p_mad_addr)
{
	ib_api_status_t status = IB_SUCCESS;
	uint64_t key = cl_ntoh64(p_mad->trans_id);
	boolean_t is_init_by_peer = FALSE;
	osmv_bind_obj_t *p_bo = (osmv_bind_obj_t *) h_bind;
	osm_madw_t *p_madw;

	OSM_LOG_ENTER(p_bo->p_vendor->p_log);

	if (NULL == p_txn) {
		if (FALSE == osmv_rmpp_is_data(p_mad)
		    || FALSE == osmv_rmpp_is_first(p_mad)) {
			osm_log(p_bo->p_vendor->p_log, OSM_LOG_DEBUG,
				"The MAD does not match any transaction "
				"and does not start a sender-initiated RMPP transfer.\n");
			goto dispatch_rmpp_mad_done;
		}

		/* IB Spec 13.6.2.2. This is a Sender Initiated Transfer.
		   My peer is the requester and RMPP Sender. I am the RMPP Receiver.
		 */
		status = osmv_txn_init(h_bind, /*tid==key */ key, key, &p_txn);
		if (IB_SUCCESS != status) {
			goto dispatch_rmpp_mad_done;
		}

		is_init_by_peer = TRUE;
		osm_log(p_bo->p_vendor->p_log, OSM_LOG_DEBUG,
			"A new sender-initiated transfer (TID=0x%" PRIx64 ") started\n",
			key);
	}

	if (OSMV_TXN_RMPP_NONE == osmv_txn_get_rmpp_state(p_txn)) {
		/* Case 1: Fall through from above.
		 * Case 2: When the transaction was initiated by me
		 *         (a single request MAD), there was an uncertainty
		 *         whether the reply will be RMPP. Now it's resolved,
		 *         since the reply is RMPP!
		 */
		status =
		    osmv_txn_init_rmpp_receiver(h_bind, p_txn, is_init_by_peer);
		if (IB_SUCCESS != status) {
			goto dispatch_rmpp_mad_done;
		}
	}

	switch (osmv_txn_get_rmpp_state(p_txn)) {

	case OSMV_TXN_RMPP_RECEIVER:
		status =
		    __osmv_dispatch_rmpp_rcv(h_bind, p_mad, p_txn, p_mad_addr);
		if (IB_SUCCESS != status) {
			if (FALSE == osmv_txn_is_rmpp_init_by_peer(p_txn)) {
				/* This is a requester, still waiting for the reply. Apply the callback */
				/* update the status of the p_madw */
				p_madw = osmv_txn_get_madw(p_txn);
				p_madw->status = status;
				p_bo->send_err_cb(p_bo->cb_context, p_madw);
			}

			/* ABORT/STOP/LOCAL ERROR */
			osmv_txn_done(h_bind, osmv_txn_get_key(p_txn), FALSE);
		}
		break;

	case OSMV_TXN_RMPP_SENDER:
		__osmv_dispatch_rmpp_snd(h_bind, p_mad, p_txn, p_mad_addr);
		/* If an error happens here, it's the sender thread to cleanup the txn */
		break;

	default:
		CL_ASSERT(FALSE);
	}

dispatch_rmpp_mad_done:
	OSM_LOG_EXIT(p_bo->p_vendor->p_log);
}

/*
 *  NAME            __osmv_dispatch_rmpp_snd()
 *
 *  DESCRIPTION     MAD handling by an RMPP sender (ACK/ABORT/STOP)
 */

static void
__osmv_dispatch_rmpp_snd(IN osm_bind_handle_t h_bind,
			 IN const ib_mad_t * p_mad,
			 IN osmv_txn_ctx_t * p_txn,
			 IN const osm_mad_addr_t * p_mad_addr)
{
	osmv_rmpp_send_ctx_t *p_send_ctx = osmv_txn_get_rmpp_send_ctx(p_txn);

	uint32_t old_wl = p_send_ctx->window_last;
	uint32_t total_segs = osmv_rmpp_send_ctx_get_num_segs(p_send_ctx);
	uint32_t seg_num = cl_ntoh32(((ib_rmpp_mad_t *) p_mad)->seg_num);
	uint32_t new_wl = cl_ntoh32(((ib_rmpp_mad_t *) p_mad)->paylen_newwin);
	osmv_bind_obj_t *p_bo = (osmv_bind_obj_t *) h_bind;

	OSM_LOG_ENTER(p_bo->p_vendor->p_log);

	if (TRUE == osmv_rmpp_is_abort_stop(p_mad)) {

		osm_log(p_bo->p_vendor->p_log, OSM_LOG_ERROR,
			"__osmv_dispatch_rmpp_snd: ERR 6502: "
			"The remote side sent an ABORT/STOP indication.\n");
		osmv_rmpp_snd_error(p_send_ctx, IB_REMOTE_ERROR);
		goto dispatch_rmpp_snd_done;
	}

	if (FALSE == osmv_rmpp_is_ack(p_mad)) {

		osm_log(p_bo->p_vendor->p_log, OSM_LOG_DEBUG,
			"Not supposed to receive DATA packets --> dropping the MAD\n");
		goto dispatch_rmpp_snd_done;
	}

	/* Continue processing the ACK */
	if (seg_num > old_wl) {

		osm_log(p_bo->p_vendor->p_log, OSM_LOG_ERROR,
			"__osmv_dispatch_rmpp_snd: ERR 6503: "
			"ACK received for a non-sent segment %d\n", seg_num);

		osmv_rmpp_send_nak(h_bind, p_mad, p_mad_addr,
				   IB_RMPP_TYPE_ABORT, IB_RMPP_STATUS_S2B);

		osmv_rmpp_snd_error(p_send_ctx, IB_REMOTE_ERROR);
		goto dispatch_rmpp_snd_done;
	}

	osm_log(p_bo->p_vendor->p_log, OSM_LOG_DEBUG,
		"__osmv_dispatch_rmpp_snd: "
		"New WL = %u Old WL = %u Total Segs = %u\n",
		new_wl, old_wl, total_segs);

	if (new_wl < old_wl) {
		osm_log(p_bo->p_vendor->p_log, OSM_LOG_ERROR,
			"__osmv_dispatch_rmpp_snd: ERR 6508: "
			"The receiver requests a smaller WL (%d) than before (%d)\n",
			new_wl, old_wl);

		osmv_rmpp_send_nak(h_bind, p_mad, p_mad_addr,
				   IB_RMPP_TYPE_ABORT, IB_RMPP_STATUS_W2S);

		osmv_rmpp_snd_error(p_send_ctx, IB_REMOTE_ERROR);
		goto dispatch_rmpp_snd_done;
	}

	/* Update the sender's window, and optionally wake up the sender thread
	 * Note! A single ACK can acknowledge a whole range of segments: [WF..SEG_NUM]
	 */
	osm_log(p_bo->p_vendor->p_log, OSM_LOG_DEBUG,
		"ACK for seg_num #%d accepted.\n", seg_num);

	if (seg_num == old_wl) {

		osm_log(p_bo->p_vendor->p_log, OSM_LOG_DEBUG,
			"The send window [%d:%d] is totally acknowledged.\n",
			p_send_ctx->window_first, old_wl);

		p_send_ctx->window_first = seg_num + 1;
		p_send_ctx->window_last =
		    (new_wl < total_segs) ? new_wl : total_segs;

		/* Remove the response timeout event for the window */
		osmv_txn_remove_timeout_ev(h_bind, osmv_txn_get_key(p_txn));

		/* Wake up the sending thread */
		cl_event_signal(&p_send_ctx->event);
	}

dispatch_rmpp_snd_done:
	OSM_LOG_EXIT(p_bo->p_vendor->p_log);
}

/*
 *  NAME           __osmv_dispatch_rmpp_rcv()
 *
 *  DESCRIPTION    MAD handling by an RMPP receiver (DATA/ABORT/STOP)
 */

static ib_api_status_t
__osmv_dispatch_rmpp_rcv(IN osm_bind_handle_t h_bind,
			 IN const ib_mad_t * p_mad,
			 IN osmv_txn_ctx_t * p_txn,
			 IN const osm_mad_addr_t * p_mad_addr)
{
	ib_api_status_t status = IB_SUCCESS;
	osmv_rmpp_recv_ctx_t *p_recv_ctx = osmv_txn_get_rmpp_recv_ctx(p_txn);
	osmv_bind_obj_t *p_bo = (osmv_bind_obj_t *) h_bind;
	boolean_t is_last1 = FALSE, is_last2 = FALSE;
	osm_madw_t *p_new_madw = NULL, *p_req_madw = NULL;
	ib_mad_t *p_mad_buf;
	uint32_t size = 0;
	uint64_t key = osmv_txn_get_key(p_txn);
	uint64_t tid = osmv_txn_get_tid(p_txn);

	OSM_LOG_ENTER(p_bo->p_vendor->p_log);

	if (TRUE == osmv_rmpp_is_ack(p_mad)) {
		osm_log(p_bo->p_vendor->p_log, OSM_LOG_DEBUG,
			"Not supposed to receive ACK's --> dropping the MAD\n");

		goto dispatch_rmpp_rcv_done;
	}

	if (TRUE == osmv_rmpp_is_abort_stop(p_mad)) {
		osm_log(p_bo->p_vendor->p_log, OSM_LOG_DEBUG,
			"__osmv_dispatch_rmpp_rcv: ERR 6504: "
			"The Remote Side stopped sending\n");

		status = IB_REMOTE_ERROR;
		goto dispatch_rmpp_rcv_done;
	}

	status = __osmv_dispatch_accept_seg(h_bind, p_txn, p_mad);
	switch (status) {

	case IB_SUCCESS:

		/* Check wheter this is the legal last MAD */
		/* Criteria #1: the received MAD is marked last */
		is_last1 = osmv_rmpp_is_last(p_mad);

		/* Criteria #2: the total accumulated length hits the advertised one */
		is_last2 = is_last1;

		size = osmv_rmpp_recv_ctx_get_byte_num_from_first(p_recv_ctx);
		if (size > 0) {
			is_last2 =
			    (osmv_rmpp_recv_ctx_get_cur_byte_num(p_recv_ctx) >=
			     size);
		}

		if (is_last1 != is_last2) {

			osmv_rmpp_send_nak(h_bind, p_mad, p_mad_addr,
					   IB_RMPP_TYPE_ABORT,
					   IB_RMPP_STATUS_BAD_LEN);

			status = IB_ERROR;
			goto dispatch_rmpp_rcv_done;
		}

		/* TBD Consider an optimization - sending an ACK
		 * only for the last segment in the window
		 */
		__osmv_dispatch_send_ack(h_bind, p_mad, p_txn, p_mad_addr);
		break;

	case IB_INSUFFICIENT_RESOURCES:
		/* An out-of-order segment received. Send the ACK anyway */
		__osmv_dispatch_send_ack(h_bind, p_mad, p_txn, p_mad_addr);
		status = IB_SUCCESS;
		goto dispatch_rmpp_rcv_done;

	case IB_INSUFFICIENT_MEMORY:
		osmv_rmpp_send_nak(h_bind, p_mad, p_mad_addr,
				   IB_RMPP_TYPE_STOP, IB_RMPP_STATUS_RESX);
		goto dispatch_rmpp_rcv_done;

	default:
		/* Illegal return code */
		CL_ASSERT(FALSE);
	}

	if (TRUE != is_last1) {
		osm_log(p_bo->p_vendor->p_log, OSM_LOG_DEBUG,
			"RMPP MADW assembly continues, TID=0x%" PRIx64 "\n", tid);
		goto dispatch_rmpp_rcv_done;
	}

	/* This is the last packet. */
	if (0 == size) {
		/* The total size was not advertised in the first packet */
		size = osmv_rmpp_recv_ctx_get_byte_num_from_last(p_recv_ctx);
	}

	/*
	   NOTE: the received mad might not be >= 256 bytes.
	   some MADs might contain several SA records but still be
	   less then a full MAD.
	   We have to use RMPP to send them over since on a regular
	   "simple" MAD there is no way to know how many records were sent
	 */

	/* Build the MAD wrapper to be returned to the user.
	 * The actual storage for the MAD is allocated there.
	 */
	p_new_madw =
	    osm_mad_pool_get(p_bo->p_osm_pool, h_bind, size, p_mad_addr);
	if (NULL == p_new_madw) {
		osm_log(p_bo->p_vendor->p_log, OSM_LOG_ERROR,
			"__osmv_dispatch_rmpp_rcv: ERR 6506: "
			"Out Of Memory - could not allocate %d bytes for the MADW\n",
			size);

		status = IB_INSUFFICIENT_MEMORY;
		goto dispatch_rmpp_rcv_done;
	}

	p_req_madw = osmv_txn_get_madw(p_txn);
	p_mad_buf = osm_madw_get_mad_ptr(p_new_madw);
	status = osmv_rmpp_recv_ctx_reassemble_arbt_mad(p_recv_ctx, size,
							(uint8_t *) p_mad_buf);
	if (IB_SUCCESS != status) {
		osm_log(p_bo->p_vendor->p_log, OSM_LOG_ERROR,
			"__osmv_dispatch_rmpp_rcv: ERR 6507: "
			"Internal error - could not reassemble the result MAD\n");
		goto dispatch_rmpp_rcv_done;	/* What can happen here? */
	}

	/* The MAD is assembled, we are about to apply the callback.
	 * Delete the transaction context, unless the transaction is double sided */
	if (FALSE == osmv_txn_is_rmpp_init_by_peer(p_txn)
	    || FALSE == osmv_mad_is_multi_resp(p_mad)) {

		osmv_txn_done(h_bind, key, FALSE);
	}

	osm_log(p_bo->p_vendor->p_log, OSM_LOG_DEBUG,
		"RMPP MADW %p assembly complete, TID=0x%" PRIx64 "\n", p_new_madw,
		tid);

	p_mad_buf->trans_id = cl_hton64(tid);
	osm_log(p_bo->p_vendor->p_log, OSM_LOG_DEBUG,
		"Restoring the original TID to 0x%" PRIx64 "\n",
		cl_ntoh64(p_mad_buf->trans_id));

	/* Finally, do the job! */
	p_bo->recv_cb(p_new_madw, p_bo->cb_context, p_req_madw);

dispatch_rmpp_rcv_done:
	OSM_LOG_EXIT(p_bo->p_vendor->p_log);
	return status;
}

/*
 *  NAME            __osmv_dispatch_accept_seg()
 *
 *  DESCRIPTION     Store a DATA segment at the RMPP receiver side,
 *                  if one is received in order.
 */

static ib_api_status_t
__osmv_dispatch_accept_seg(IN osm_bind_handle_t h_bind,
			   IN osmv_txn_ctx_t * p_txn, IN const ib_mad_t * p_mad)
{
	ib_api_status_t ret = IB_SUCCESS;
	uint32_t seg_num = cl_ntoh32(((ib_rmpp_mad_t *) p_mad)->seg_num);
	osmv_rmpp_recv_ctx_t *p_recv_ctx = osmv_txn_get_rmpp_recv_ctx(p_txn);
	osmv_bind_obj_t *p_bo = (osmv_bind_obj_t *) h_bind;
	uint64_t tid = osmv_txn_get_tid(p_txn);

	OSM_LOG_ENTER(p_bo->p_vendor->p_log);

	if (seg_num != p_recv_ctx->expected_seg) {
		osm_log(p_bo->p_vendor->p_log, OSM_LOG_DEBUG,
			"TID 0x%" PRIx64 ": can't accept this segment (%d) - "
			"this is a Go-Back-N implementation\n", tid, seg_num);
		return IB_INSUFFICIENT_RESOURCES;
	}

	/* Store the packet's copy in the reassembly list.
	 * Promote the expected segment counter.
	 */
	ret = osmv_rmpp_recv_ctx_store_mad_seg(p_recv_ctx, (uint8_t *) p_mad);
	if (IB_SUCCESS != ret) {
		return ret;
	}

	osm_log(p_bo->p_vendor->p_log, OSM_LOG_DEBUG,
		"TID 0x%" PRIx64 ": segment %d accepted\n", tid, seg_num);
	p_recv_ctx->expected_seg = seg_num + 1;

	OSM_LOG_EXIT(p_bo->p_vendor->p_log);
	return IB_SUCCESS;
}

/*
 *  NAME              __osmv_dispatch_send_ack()
 *
 *  DESCRIPTION
 *
 *  ISSUES
 *    Consider sending the ACK from an async thread
 *    if problems with the receiving side processing arise.
 */

static void
__osmv_dispatch_send_ack(IN osm_bind_handle_t h_bind,
			 IN const ib_mad_t * p_req_mad,
			 IN osmv_txn_ctx_t * p_txn,
			 IN const osm_mad_addr_t * p_mad_addr)
{
	osmv_rmpp_recv_ctx_t *p_recv_ctx = osmv_txn_get_rmpp_recv_ctx(p_txn);

	/* ACK the segment # that was accepted */
	uint32_t seg_num = cl_ntoh32(((ib_rmpp_mad_t *) p_req_mad)->seg_num);

	/* NOTE! The receiver can publish the New Window Last (NWL) value
	 * that is greater than the total number of segments to be sent.
	 * It's the sender's responsibility to compute the correct number
	 * of segments to send in the next burst.
	 */
	uint32_t nwl = p_recv_ctx->expected_seg + OSMV_RMPP_RECV_WIN - 1;

	osmv_rmpp_send_ack(h_bind, p_req_mad, seg_num, nwl, p_mad_addr);
}
