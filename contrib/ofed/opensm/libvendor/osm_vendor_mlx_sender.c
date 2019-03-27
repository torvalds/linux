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
#include <vendor/osm_vendor_mlx_sender.h>
#include <vendor/osm_vendor_mlx_transport.h>
#include <vendor/osm_vendor_mlx_svc.h>
#include <vendor/osm_pkt_randomizer.h>

static ib_api_status_t
__osmv_rmpp_send_segment(IN osm_bind_handle_t h_bind,
			 IN osmv_txn_ctx_t * p_txn, IN uint32_t seg_num);

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
		      IN osmv_txn_ctx_t * p_txn, IN boolean_t is_retry)
{
	ib_api_status_t ret;
	osmv_bind_obj_t *p_bo = (osmv_bind_obj_t *) h_bind;
	osm_mad_addr_t *p_mad_addr = osm_madw_get_mad_addr_ptr(p_madw);
	uint8_t mad_buf[MAD_BLOCK_SIZE];
	ib_mad_t *p_mad = (ib_mad_t *) mad_buf;
	uint64_t key = 0;

	OSM_LOG_ENTER(p_bo->p_vendor->p_log);

	CL_ASSERT(p_madw->mad_size <= MAD_BLOCK_SIZE);

	memset(p_mad, 0, MAD_BLOCK_SIZE);
	memcpy(p_mad, osm_madw_get_mad_ptr(p_madw), p_madw->mad_size);

	if (NULL != p_txn) {
		/* Push a fake txn id to the MAD */
		key = osmv_txn_get_key(p_txn);
		p_mad->trans_id = cl_hton64(key);
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
				"The MAD will not be sent. \n");
			ret = IB_SUCCESS;
		} else {
			ret =
			    osmv_transport_mad_send(h_bind, p_mad, p_mad_addr);
		}
	} else {
		ret = osmv_transport_mad_send(h_bind, p_mad, p_mad_addr);
	}

	if ((IB_SUCCESS == ret) && (NULL != p_txn) && (!is_retry)) {
		/* Set the timeout for receiving the response MAD */
		ret = osmv_txn_set_timeout_ev(h_bind, key,
					      p_bo->p_vendor->resp_timeout);
	}

	OSM_LOG_EXIT(p_bo->p_vendor->p_log);
	return ret;
}

/***** OSM Vendor/osmv_rmpp_send_madw
 * NAME
 *   osmv_rmpp_send_madw
 *
 * DESCRIPTION
 * Send a single message (MAD wrapper of arbitrary length).
 *      Follow the RMPP semantics
 *      (segmentation, send window, timeouts etc).
 *
 *      The function call returns either when the whole message
 *      has been acknowledged, or upon error.
 *
 *  ASSUMPTIONS
 *      The RMPP sender context is set up
 */

ib_api_status_t
osmv_rmpp_send_madw(IN osm_bind_handle_t h_bind,
		    IN osm_madw_t * const p_madw,
		    IN osmv_txn_ctx_t * p_txn, IN boolean_t is_rmpp_ds)
{
	ib_api_status_t ret = IB_SUCCESS;
	uint32_t i, total_segs;

	osmv_rmpp_send_ctx_t *p_send_ctx = osmv_txn_get_rmpp_send_ctx(p_txn);
	osmv_bind_obj_t *p_bo = (osmv_bind_obj_t *) h_bind;

	OSM_LOG_ENTER(p_bo->p_vendor->p_log);

	total_segs = osmv_rmpp_send_ctx_get_num_segs(p_send_ctx);
	CL_ASSERT(total_segs >= 1);

	/* In the double-sided transfer, wait for ACK 0 */

	for (;;) {

		if (p_send_ctx->window_first > total_segs) {

			/* Every segment is acknowledged */
			break;
		}

		/* Send the next burst. */
		for (i = p_send_ctx->window_first; i <= p_send_ctx->window_last;
		     i++) {

			/* Send a segment and setup a timeout timer */
			ret = __osmv_rmpp_send_segment(h_bind, p_txn, i);
			if (IB_SUCCESS != ret) {
				goto send_done;
			}
		}

		/* Set the Response Timeout for the ACK on the last DATA segment */
		ret = osmv_txn_set_timeout_ev(h_bind, osmv_txn_get_key(p_txn),
					      p_bo->p_vendor->resp_timeout);
		if (IB_SUCCESS != ret) {
			goto send_done;
		}

		/* Going to sleep. Let the others access the transaction DB */
		osmv_txn_unlock(p_bo);

		osm_log(p_bo->p_vendor->p_log, OSM_LOG_DEBUG,
			"RMPP Sender thread (madw=%p) going to sleep ...\n",
			p_madw);

		/* Await the next event to happen */
		cl_event_wait_on(&p_send_ctx->event,
				 EVENT_NO_TIMEOUT, TRUE /* interruptible */ );

		/* Got a signal from the MAD dispatcher/timeout handler */
		osm_log(p_bo->p_vendor->p_log, OSM_LOG_DEBUG,
			"RMPP Sender thread (madw=%p) waking up on a signal ...\n",
			p_madw);

		/* Let's see what changed... Make this atomic - re-acquire the lock. */
		osmv_txn_lock(p_bo);

		if (TRUE == p_bo->is_closing) {
			osm_log(p_bo->p_vendor->p_log, OSM_LOG_ERROR,
				"osmv_rmpp_send_madw: ERR 6601: "
				"The bind handle %p is being closed. "
				"Stopping the RMPP Send of MADW %p\n",
				h_bind, p_madw);

			ret = IB_TIMEOUT;
			return IB_INTERRUPTED;
		}

		/* STOP? ABORT? TIMEOUT? */
		if (IB_SUCCESS != p_send_ctx->status) {
			osm_log(p_bo->p_vendor->p_log, OSM_LOG_ERROR,
				"osmv_rmpp_send_madw: ERR 6602: "
				"An error (%s) happened during the RMPP send of %p. Bailing out.\n",
				ib_get_err_str(p_send_ctx->status), p_madw);
			ret = p_send_ctx->status;
			goto send_done;
		}
	}

	if (TRUE == is_rmpp_ds) {
		osm_log(p_bo->p_vendor->p_log, OSM_LOG_DEBUG,
			"Double-sided RMPP - switching to be the receiver.\n");

		ret = osmv_txn_init_rmpp_receiver(h_bind, p_txn, FALSE
						  /*Send was initiated by me */
						  );

		if (IB_SUCCESS == ret) {
			/* Send ACK on the 0 segment */
			ret = __osmv_rmpp_send_segment(h_bind, p_txn, 0);
		}
	}

send_done:
	OSM_LOG_EXIT(p_bo->p_vendor->p_log);
	return ret;
}

/*
 *  NAME                osmv_rmpp_send_ack
 *
 *  DESCRIPTION
 *
 */

ib_api_status_t
osmv_rmpp_send_ack(IN osm_bind_handle_t h_bind,
		   IN const ib_mad_t * p_req_mad,
		   IN uint32_t seg_num,
		   IN uint32_t nwl, IN const osm_mad_addr_t * p_mad_addr)
{
	uint8_t resp_mad[MAD_BLOCK_SIZE];
	ib_rmpp_mad_t *p_resp_mad = (ib_rmpp_mad_t *) resp_mad;

#ifdef OSMV_RANDOM_DROP
	if (TRUE == osmv_random_drop()) {
		osm_log(((osmv_bind_obj_t *) h_bind)->p_vendor->p_log,
			OSM_LOG_DEBUG,
			"Error injection - dropping the RMPP ACK\n");
		return IB_SUCCESS;
	}
#endif

	memcpy(p_resp_mad, p_req_mad, MAD_BLOCK_SIZE);

	p_resp_mad->common_hdr.method = osmv_invert_method(p_req_mad->method);
	p_resp_mad->rmpp_type = IB_RMPP_TYPE_ACK;
	p_resp_mad->seg_num = cl_hton32(seg_num);
	p_resp_mad->paylen_newwin = cl_hton32(nwl);
	p_resp_mad->rmpp_flags = IB_RMPP_FLAG_ACTIVE;

	return osmv_transport_mad_send(h_bind, p_resp_mad, p_mad_addr);
}

/*
 *  NAME            osmv_rmpp_send_nak
 *
 *  DESCRIPTION     Send the RMPP ABORT or STOP packet
 */

ib_api_status_t
osmv_rmpp_send_nak(IN osm_bind_handle_t h_bind,
		   IN const ib_mad_t * p_req_mad,
		   IN const osm_mad_addr_t * p_mad_addr,
		   IN uint8_t nak_type, IN uint8_t status)
{
	uint8_t resp_mad[MAD_BLOCK_SIZE];
	ib_rmpp_mad_t *p_resp_mad = (ib_rmpp_mad_t *) resp_mad;

	memcpy(p_resp_mad, p_req_mad, MAD_BLOCK_SIZE);

	p_resp_mad->common_hdr.method = osmv_invert_method(p_req_mad->method);
	p_resp_mad->rmpp_type = nak_type;
	p_resp_mad->rmpp_status = status;

	return osmv_transport_mad_send(h_bind, p_resp_mad, p_mad_addr);
}

/*
 *  NAME              __osmv_rmpp_send_segment
 *
 *  DESCRIPTION       Build a MAD for a specific segment and send it
 */

static ib_api_status_t
__osmv_rmpp_send_segment(IN osm_bind_handle_t h_bind,
			 IN osmv_txn_ctx_t * p_txn, IN uint32_t seg_num)
{
	ib_api_status_t ret;
	osmv_rmpp_send_ctx_t *p_send_ctx;
	uint8_t mad_buf[MAD_BLOCK_SIZE];
	ib_mad_t *p_mad = (ib_mad_t *) mad_buf;
	osmv_bind_obj_t *p_bo = (osmv_bind_obj_t *) h_bind;
	osm_mad_addr_t *p_mad_addr =
	    osm_madw_get_mad_addr_ptr(osmv_txn_get_madw(p_txn));
	uint32_t timeout = p_bo->p_vendor->resp_timeout;
	uint64_t key;

	OSM_LOG_ENTER(p_bo->p_vendor->p_log);

#ifdef OSMV_RANDOM_DROP
	if (TRUE == osmv_random_drop()) {

		osm_log(p_bo->p_vendor->p_log, OSM_LOG_DEBUG,
			"Error injection - simulating the RMPP segment drop\n");
		return IB_SUCCESS;
	}
#endif

	p_send_ctx = osmv_txn_get_rmpp_send_ctx(p_txn);
	key = osmv_txn_get_key(p_txn);

	if (0 != seg_num) {
		ret =
		    osmv_rmpp_send_ctx_get_seg(p_send_ctx, seg_num, timeout,
					       p_mad);
		CL_ASSERT(IB_SUCCESS == ret);

		/* Put the segment to the wire ! */
		p_mad->trans_id = cl_hton64(key);

		osm_log(p_bo->p_vendor->p_log, OSM_LOG_DEBUG,
			"Sending RMPP segment #%d, on-wire TID=0x%" PRIx64 "\n",
			seg_num, p_mad->trans_id);

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
					"The MAD will not be sent. \n");
				ret = IB_SUCCESS;
			} else {
				ret =
				    osmv_transport_mad_send((osm_bind_handle_t)
							    p_bo, p_mad,
							    p_mad_addr);
			}
		} else {
			ret =
			    osmv_transport_mad_send((osm_bind_handle_t) p_bo,
						    p_mad, p_mad_addr);
		}
	} else {
		/* This is an ACK for double-sided handshake. Give it a special treatment. */

		/* It doesn't really matter which data to put. Only the header matters. */
		ret = osmv_rmpp_send_ctx_get_seg(p_send_ctx, 1, timeout, p_mad);
		CL_ASSERT(IB_SUCCESS == ret);

		p_mad->trans_id = cl_hton64(key);
		ret =
		    osmv_rmpp_send_ack((osm_bind_handle_t) p_bo, p_mad,
				       0 /* segnum */ ,
				       OSMV_RMPP_RECV_WIN /* NWL */ ,
				       p_mad_addr);
	}

	OSM_LOG_EXIT(p_bo->p_vendor->p_log);
	return ret;
}
