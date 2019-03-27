/*
 * Copyright (c) 2004-2008 Voltaire, Inc. All rights reserved.
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

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <stdlib.h>

#include <vendor/osm_vendor_mlx.h>
#include <vendor/osm_vendor_mlx_defs.h>
#include <vendor/osm_vendor_mlx_txn.h>
#include <vendor/osm_vendor_mlx_svc.h>
#include <vendor/osm_vendor_mlx_sender.h>

static ib_api_status_t
__osmv_txnmgr_lookup(IN osmv_txn_mgr_t * p_tx_mgr,
		     IN uint64_t key, OUT osmv_txn_ctx_t ** pp_txn);

static ib_api_status_t
__osmv_txnmgr_insert_txn(IN osmv_txn_mgr_t * p_tx_mgr,
			 IN osmv_txn_ctx_t * p_txn, IN uint64_t key);

static ib_api_status_t
__osmv_txnmgr_remove_txn(IN osmv_txn_mgr_t * p_tx_mgr,
			 IN uint64_t key, OUT osmv_txn_ctx_t ** pp_txn);

static void __osmv_txn_all_done(osm_bind_handle_t h_bind);

static uint64_t
__osmv_txn_timeout_cb(IN uint64_t key,
		      IN uint32_t num_regs, IN void *cb_context);

ib_api_status_t
osmv_txn_init(IN osm_bind_handle_t h_bind,
	      IN uint64_t tid, IN uint64_t key, OUT osmv_txn_ctx_t ** pp_txn)
{
	ib_api_status_t st;
	osmv_txn_ctx_t *p_txn;
	osmv_bind_obj_t *p_bo = (osmv_bind_obj_t *) h_bind;

	OSM_LOG_ENTER(p_bo->p_vendor->p_log);

	CL_ASSERT(NULL != h_bind && NULL != pp_txn);

	osm_log(p_bo->p_vendor->p_log, OSM_LOG_DEBUG,
		"Starting transaction 0x%016" PRIx64
		" (key=0x%016" PRIx64 ")\n", tid, key);

	p_txn = malloc(sizeof(osmv_txn_ctx_t));
	if (!p_txn) {
		return IB_INSUFFICIENT_MEMORY;
	}

	memset(p_txn, 0, sizeof(osmv_txn_ctx_t));
	p_txn->p_log = p_bo->txn_mgr.p_log;
	p_txn->tid = tid;
	p_txn->key = key;
	p_txn->p_madw = NULL;
	p_txn->rmpp_txfr.rmpp_state = OSMV_TXN_RMPP_NONE;

	/* insert into transaction manager DB */
	st = __osmv_txnmgr_insert_txn(&p_bo->txn_mgr, p_txn, key);
	if (IB_SUCCESS != st) {
		osm_log(p_bo->p_vendor->p_log, OSM_LOG_ERROR,
			"osmv_txn_init: ERR 6703: "
			"Failed to insert to transaction 0x%016" PRIx64
			" (key=0x%016" PRIx64 ") to manager DB\n",
			tid, key);
		goto insert_txn_failed;
	}

	*pp_txn = p_txn;
	OSM_LOG_EXIT(p_bo->p_vendor->p_log);
	return IB_SUCCESS;

insert_txn_failed:
	free(p_txn);

	OSM_LOG_EXIT(p_bo->p_vendor->p_log);
	return st;
}

ib_api_status_t
osmv_txn_init_rmpp_sender(IN osm_bind_handle_t h_bind,
			  IN osmv_txn_ctx_t * p_txn, IN osm_madw_t * p_madw)
{
	ib_api_status_t st;

	CL_ASSERT(p_txn);

	/* Double-Sided RMPP Direction Switch */
	osmv_txn_remove_timeout_ev(h_bind, osmv_txn_get_key(p_txn));

	p_txn->rmpp_txfr.rmpp_state = OSMV_TXN_RMPP_SENDER;
	p_txn->rmpp_txfr.p_rmpp_send_ctx = malloc(sizeof(osmv_rmpp_send_ctx_t));

	if (!p_txn->rmpp_txfr.p_rmpp_send_ctx) {
		return IB_INSUFFICIENT_MEMORY;
	}

	memset(p_txn->rmpp_txfr.p_rmpp_send_ctx, 0,
	       sizeof(osmv_rmpp_send_ctx_t));

	st = osmv_rmpp_send_ctx_init(p_txn->rmpp_txfr.p_rmpp_send_ctx,
				     (void *)p_madw->p_mad,
				     p_madw->mad_size, p_txn->p_log);
	return st;
}

ib_api_status_t
osmv_txn_init_rmpp_receiver(IN osm_bind_handle_t h_bind,
			    IN osmv_txn_ctx_t * p_txn,
			    IN boolean_t is_init_by_peer)
{
	ib_api_status_t st;
	osmv_bind_obj_t *p_bo = (osmv_bind_obj_t *) h_bind;
	uint64_t key = osmv_txn_get_key(p_txn);

	CL_ASSERT(p_txn);

	/* Double-Sided RMPP Direction Switch */
	osmv_txn_remove_timeout_ev(h_bind, key);

	/* Set the Transaction Timeout value */
	st = osmv_txn_set_timeout_ev(h_bind, key,
				     p_bo->p_vendor->ttime_timeout);
	if (IB_SUCCESS != st) {

		return st;
	}

	p_txn->rmpp_txfr.rmpp_state = OSMV_TXN_RMPP_RECEIVER;
	p_txn->rmpp_txfr.is_rmpp_init_by_peer = is_init_by_peer;

	p_txn->rmpp_txfr.p_rmpp_recv_ctx = malloc(sizeof(osmv_rmpp_recv_ctx_t));

	if (!p_txn->rmpp_txfr.p_rmpp_recv_ctx) {

		osmv_txn_remove_timeout_ev(h_bind, key);
		return IB_INSUFFICIENT_MEMORY;
	}

	memset(p_txn->rmpp_txfr.p_rmpp_recv_ctx, 0,
	       sizeof(osmv_rmpp_recv_ctx_t));

	st = osmv_rmpp_recv_ctx_init(p_txn->rmpp_txfr.p_rmpp_recv_ctx,
				     p_txn->p_log);

	return st;
}

/*
 * NAME
 *  osmv_txn_set_timeout_ev
 *
 * DESCRIPTION
 *
 * SEE ALSO
 *
 */
ib_api_status_t
osmv_txn_set_timeout_ev(IN osm_bind_handle_t h_bind,
			IN uint64_t key, IN uint64_t msec)
{
	osmv_bind_obj_t *p_bo = (osmv_bind_obj_t *) h_bind;
	cl_event_wheel_t *p_event_wheel = p_bo->txn_mgr.p_event_wheel;
	cl_status_t status;

	status = cl_event_wheel_reg(p_event_wheel, key, cl_get_time_stamp() + 1000 * msec,	/* TTL */
				    __osmv_txn_timeout_cb,
				    p_bo /* The context */ );

	return (ib_api_status_t) status;
}

/*
 * NAME
 *  osmv_txn_remove_timeout_ev
 *
 * DESCRIPTION

 * SEE ALSO
 *
 */
void osmv_txn_remove_timeout_ev(IN osm_bind_handle_t h_bind, IN uint64_t key)
{
	cl_event_wheel_t *p_event_wheel =
	    ((osmv_bind_obj_t *) h_bind)->txn_mgr.p_event_wheel;
	cl_event_wheel_unreg(p_event_wheel, key);
}

void
osmv_txn_done(IN osm_bind_handle_t h_bind,
	      IN uint64_t key, IN boolean_t is_in_cb)
{
	osmv_txn_ctx_t *p_ctx;
	osmv_bind_obj_t *const p_bo = (osmv_bind_obj_t *) h_bind;

	OSM_LOG_ENTER(p_bo->p_vendor->p_log);

	CL_ASSERT(h_bind);

	/* Cancel the (single) timeout possibly outstanding for this txn
	 * Don't do this if you are in the callback context, for 2 reasons:
	 * (1) The event wheel will remove the context itself.
	 * (2) If we try to, there is a deadlock in the event wheel
	 */
	if (FALSE == is_in_cb) {
		osmv_txn_remove_timeout_ev(h_bind, key);
	}

	/* Remove from DB */
	if (IB_NOT_FOUND ==
	    __osmv_txnmgr_remove_txn(&p_bo->txn_mgr, key, &p_ctx)) {
		return;
	}

	/* Destroy the transaction's RMPP contexts
	 * (can be more than one in the case of double sided transfer)
	 */

	if (p_ctx->rmpp_txfr.p_rmpp_send_ctx) {
		osmv_rmpp_send_ctx_done(p_ctx->rmpp_txfr.p_rmpp_send_ctx);
	}

	if (p_ctx->rmpp_txfr.p_rmpp_recv_ctx) {
		osmv_rmpp_recv_ctx_done(p_ctx->rmpp_txfr.p_rmpp_recv_ctx);
	}

	free(p_ctx);

	OSM_LOG_EXIT(p_bo->p_vendor->p_log);
}

ib_api_status_t
osmv_txn_lookup(IN osm_bind_handle_t h_bind,
		IN uint64_t key, OUT osmv_txn_ctx_t ** pp_txn)
{
	return __osmv_txnmgr_lookup(&(((osmv_bind_obj_t *) h_bind)->txn_mgr),
				    key, pp_txn);
}

void osmv_txn_abort_rmpp_txns(osm_bind_handle_t h_bind)
{
	osmv_bind_obj_t *p_bo = (osmv_bind_obj_t *) h_bind;
	cl_map_item_t *p_item;
	cl_map_obj_t *p_obj;
	osmv_txn_ctx_t *p_txn;
	osmv_rmpp_send_ctx_t *p_send_ctx;
	cl_qmap_t *p_map = p_bo->txn_mgr.p_txn_map;

	OSM_LOG_ENTER(p_bo->p_vendor->p_log);

	while (FALSE == cl_is_qmap_empty(p_map)) {

		p_item = cl_qmap_head(p_map);
		p_obj = PARENT_STRUCT(p_item, cl_map_obj_t, item);
		p_txn = (osmv_txn_ctx_t *) cl_qmap_obj(p_obj);
		p_send_ctx = osmv_txn_get_rmpp_send_ctx(p_txn);

		if (NULL != p_send_ctx) {

			p_send_ctx->status = IB_INTERRUPTED;

			/* Wake up the sender thread to let it break out */
			cl_event_signal(&p_send_ctx->event);
		}

		cl_qmap_remove_item(p_map, p_item);
	}

	OSM_LOG_EXIT(p_bo->p_vendor->p_log);
}

ib_api_status_t
osmv_txnmgr_init(IN osmv_txn_mgr_t * p_tx_mgr,
		 IN osm_log_t * p_log, IN cl_spinlock_t * p_lock)
{
	cl_status_t cl_st = CL_SUCCESS;

	p_tx_mgr->p_event_wheel = malloc(sizeof(cl_event_wheel_t));
	if (!p_tx_mgr->p_event_wheel) {
		return IB_INSUFFICIENT_MEMORY;
	}

	memset(p_tx_mgr->p_event_wheel, 0, sizeof(cl_event_wheel_t));

	cl_event_wheel_construct(p_tx_mgr->p_event_wheel);

	/* NOTE! We are using an extended constructor.
	 * We tell the Event Wheel run in a non-protected manner in the reg/unreg calls,
	 * and acquire an external lock in the asynchronous callback.
	 */
	cl_st = cl_event_wheel_init_ex(p_tx_mgr->p_event_wheel, p_lock);
	if (cl_st != CL_SUCCESS) {
		free(p_tx_mgr->p_event_wheel);
		return (ib_api_status_t) cl_st;
	}

	p_tx_mgr->p_txn_map = malloc(sizeof(cl_qmap_t));
	if (!p_tx_mgr->p_txn_map) {
		cl_event_wheel_destroy(p_tx_mgr->p_event_wheel);
		free(p_tx_mgr->p_event_wheel);
		return IB_INSUFFICIENT_MEMORY;
	}

	memset(p_tx_mgr->p_txn_map, 0, sizeof(cl_qmap_t));

	cl_qmap_init(p_tx_mgr->p_txn_map);
	p_tx_mgr->p_log = p_log;

	return cl_st;
}

void osmv_txnmgr_done(IN osm_bind_handle_t h_bind)
{
	osmv_bind_obj_t *p_bo = (osmv_bind_obj_t *) h_bind;

	__osmv_txn_all_done(h_bind);
	free(p_bo->txn_mgr.p_txn_map);

	cl_event_wheel_destroy(p_bo->txn_mgr.p_event_wheel);
	free(p_bo->txn_mgr.p_event_wheel);
}

ib_api_status_t
__osmv_txnmgr_lookup(IN osmv_txn_mgr_t * p_tx_mgr,
		     IN uint64_t key, OUT osmv_txn_ctx_t ** pp_txn)
{
	ib_api_status_t status = IB_SUCCESS;
	cl_map_item_t *p_item;
	cl_map_obj_t *p_obj;

	uint64_t tmp_key;

	OSM_LOG_ENTER(p_tx_mgr->p_log);

	CL_ASSERT(p_tx_mgr);
	CL_ASSERT(pp_txn);

	osm_log(p_tx_mgr->p_log, OSM_LOG_DEBUG,
		"__osmv_txnmgr_lookup: "
		"Looking for key: 0x%016" PRIx64 " in map ptr:%p\n", key,
		p_tx_mgr->p_txn_map);

	p_item = cl_qmap_head(p_tx_mgr->p_txn_map);
	while (p_item != cl_qmap_end(p_tx_mgr->p_txn_map)) {
		tmp_key = cl_qmap_key(p_item);
		osm_log(p_tx_mgr->p_log, OSM_LOG_DEBUG,
			"__osmv_txnmgr_lookup: "
			"Found key 0x%016" PRIx64 "\n", tmp_key);
		p_item = cl_qmap_next(p_item);
	}

	p_item = cl_qmap_get(p_tx_mgr->p_txn_map, key);
	if (cl_qmap_end(p_tx_mgr->p_txn_map) == p_item) {
		status = IB_NOT_FOUND;
	} else {
		p_obj = PARENT_STRUCT(p_item, cl_map_obj_t, item);
		*pp_txn = cl_qmap_obj(p_obj);
	}

	OSM_LOG_EXIT(p_tx_mgr->p_log);
	return status;
}

ib_api_status_t
__osmv_txnmgr_insert_txn(IN osmv_txn_mgr_t * p_tx_mgr,
			 IN osmv_txn_ctx_t * p_txn, IN uint64_t key)
{
	cl_map_obj_t *p_obj = NULL;
	cl_map_item_t *p_item;
	uint64_t tmp_key;

	CL_ASSERT(p_tx_mgr);
	CL_ASSERT(p_txn);

	key = osmv_txn_get_key(p_txn);
	p_obj = malloc(sizeof(cl_map_obj_t));
	if (NULL == p_obj)
		return IB_INSUFFICIENT_MEMORY;

	osm_log(p_tx_mgr->p_log, OSM_LOG_DEBUG,
		"__osmv_txnmgr_insert_txn: "
		"Inserting key: 0x%016" PRIx64 " to map ptr:%p\n", key,
		p_tx_mgr->p_txn_map);

	memset(p_obj, 0, sizeof(cl_map_obj_t));

	cl_qmap_set_obj(p_obj, p_txn);
	/* assuming lookup with this key was made and the result was IB_NOT_FOUND */
	cl_qmap_insert(p_tx_mgr->p_txn_map, key, &p_obj->item);

	p_item = cl_qmap_head(p_tx_mgr->p_txn_map);
	while (p_item != cl_qmap_end(p_tx_mgr->p_txn_map)) {
		tmp_key = cl_qmap_key(p_item);
		osm_log(p_tx_mgr->p_log, OSM_LOG_DEBUG,
			"__osmv_txnmgr_insert_txn: "
			"Found key 0x%016" PRIx64 "\n", tmp_key);
		p_item = cl_qmap_next(p_item);
	}

	return IB_SUCCESS;
}

ib_api_status_t
__osmv_txnmgr_remove_txn(IN osmv_txn_mgr_t * p_tx_mgr,
			 IN uint64_t key, OUT osmv_txn_ctx_t ** pp_txn)
{
	cl_map_obj_t *p_obj;
	cl_map_item_t *p_item;

	OSM_LOG_ENTER(p_tx_mgr->p_log);

	CL_ASSERT(p_tx_mgr);
	CL_ASSERT(pp_txn);

	p_item = cl_qmap_remove(p_tx_mgr->p_txn_map, key);

	if (p_item == cl_qmap_end(p_tx_mgr->p_txn_map)) {

		osm_log(p_tx_mgr->p_log, OSM_LOG_ERROR,
			"__osmv_txnmgr_remove_txn: ERR 6701: "
			"Could not remove the transaction 0x%016" PRIx64 " - "
			"something is really wrong!\n", key);
		OSM_LOG_EXIT(p_tx_mgr->p_log);
		return IB_NOT_FOUND;
	}

	p_obj = PARENT_STRUCT(p_item, cl_map_obj_t, item);
	*pp_txn = cl_qmap_obj(p_obj);

	free(p_obj);

	OSM_LOG_EXIT(p_tx_mgr->p_log);
	return IB_SUCCESS;
}

void __osmv_txn_all_done(osm_bind_handle_t h_bind)
{
	osmv_bind_obj_t *p_bo = (osmv_bind_obj_t *) h_bind;
	cl_map_item_t *p_item;
	cl_map_obj_t *p_obj;
	osmv_txn_ctx_t *p_txn;

	OSM_LOG_ENTER(p_bo->p_vendor->p_log);

	p_item = cl_qmap_head(p_bo->txn_mgr.p_txn_map);
	while (p_item != cl_qmap_end(p_bo->txn_mgr.p_txn_map)) {

		p_obj = PARENT_STRUCT(p_item, cl_map_obj_t, item);
		p_txn = (osmv_txn_ctx_t *) cl_qmap_obj(p_obj);
		osmv_txn_done(h_bind, osmv_txn_get_key(p_txn), FALSE);
		free(p_obj);
		/* assuming osmv_txn_done has removed the txn from the map */
		p_item = cl_qmap_head(p_bo->txn_mgr.p_txn_map);
	}

	OSM_LOG_EXIT(p_bo->p_vendor->p_log);
}

/******************************************************************************/

void osmv_txn_lock(IN osm_bind_handle_t h_bind)
{
	osmv_bind_obj_t *p_bo = (osmv_bind_obj_t *) h_bind;

	osm_log(p_bo->p_vendor->p_log, OSM_LOG_DEBUG,
		"--> Acquiring lock %p on bind handle %p\n", &p_bo->lock, p_bo);

	cl_spinlock_acquire(&p_bo->lock);

	osm_log(p_bo->p_vendor->p_log, OSM_LOG_DEBUG,
		"--> Acquired lock %p on bind handle %p\n", &p_bo->lock, p_bo);
}

void osmv_txn_unlock(IN osm_bind_handle_t h_bind)
{
	osmv_bind_obj_t *p_bo = (osmv_bind_obj_t *) h_bind;
	cl_spinlock_t *p_lock = &p_bo->lock;
	osm_log_t *p_log = p_bo->p_vendor->p_log;

	osm_log(p_log, OSM_LOG_DEBUG,
		"<-- Releasing lock %p on bind handle %p\n", p_lock, p_bo);

	cl_spinlock_release(&p_bo->lock);

	/* We'll use the saved ptrs, since now the p_bo can be destroyed already */
	osm_log(p_log, OSM_LOG_DEBUG,
		"<-- Released lock %p on bind handle %p\n", p_lock, p_bo);

}

static uint64_t
__osmv_txn_timeout_cb(IN uint64_t key,
		      IN uint32_t num_regs, IN void *cb_context)
{
	osmv_bind_obj_t *p_bo = (osmv_bind_obj_t *) cb_context;
	uint64_t ret = 0;
	osmv_txn_ctx_t *p_txn;
	osmv_rmpp_send_ctx_t *p_send_ctx;
	osm_madw_t *p_madw = NULL;
	ib_mad_t *p_mad;
	osm_mad_addr_t *p_mad_addr;
	boolean_t invoke_err_cb = FALSE;

	OSM_LOG_ENTER(p_bo->p_vendor->p_log);

	/* Don't try to acquire a lock on the Bind Object -
	 * it's taken by the mechanism that drives the timeout based events!
	 * (Recall the special constructor that the Event Wheel is applied with)
	 */
	if (p_bo->is_closing) {
		goto txn_done;
	}

	ret = osmv_txn_lookup(p_bo, key, &p_txn);
	if (IB_NOT_FOUND == ret) {
		/* Prevent a race - the transaction is already destroyed */
		goto txn_done;
	}

	p_madw = p_txn->p_madw;

	switch (osmv_txn_get_rmpp_state(p_txn)) {

	case OSMV_TXN_RMPP_NONE:
		if (num_regs <= OSM_DEFAULT_RETRY_COUNT) {
			/* We still did not exceed the limit of retransmissions.
			 * Set the next timeout's value.
			 */
			osm_log(p_bo->p_vendor->p_log, OSM_LOG_DEBUG,
				"__osmv_txn_timeout_cb: "
				"The transaction request (tid=0x%016" PRIx64 ")"
				" timed out %d times. Retrying the send.\n",
				osmv_txn_get_tid(p_txn), num_regs);

			/* resend this mad */
			ret = osmv_simple_send_madw((osm_bind_handle_t *) p_bo,
						    p_madw, p_txn, TRUE);
			if (ret != IB_SUCCESS) {
				osm_log(p_bo->p_vendor->p_log, OSM_LOG_ERROR,
					"__osmv_txn_timeout_cb: "
					"Fail to send retry for transaction"
					"request (tid=0x%016" PRIx64 ").\n",
					osmv_txn_get_tid(p_txn));

				osmv_txn_done((osm_bind_handle_t) p_bo, key,
					      TRUE /*in timeout callback */ );

				/* This is a requester. Always apply the callback */
				invoke_err_cb = TRUE;
			} else {
				uint64_t next_timeout_ms;
				next_timeout_ms =
				    p_bo->p_vendor->resp_timeout * (num_regs +
								    1) *
				    (num_regs + 1);
				/* when do we need to timeout again */
				ret =
				    cl_get_time_stamp() +
				    (uint64_t) (1000 * next_timeout_ms);

				osm_log(p_bo->p_vendor->p_log, OSM_LOG_DEBUG,
					"__osmv_txn_timeout_cb: "
					"Retry request timout in : %lu [msec].\n",
					next_timeout_ms);
			}
		} else {
			osm_log(p_bo->p_vendor->p_log, OSM_LOG_ERROR,
				"__osmv_txn_timeout_cb: ERR 6702: "
				"The transaction request (0x%016" PRIx64 ") "
				"timed out (after %d retries). "
				"Invoking the error callback.\n",
				osmv_txn_get_tid(p_txn), num_regs);

			osmv_txn_done((osm_bind_handle_t) p_bo, key,
				      TRUE /*in timeout callback */ );

			/* This is a requester. Always apply the callback */
			invoke_err_cb = TRUE;
		}
		break;

	case OSMV_TXN_RMPP_SENDER:
		osm_log(p_bo->p_vendor->p_log, OSM_LOG_DEBUG,
			"RMPP sender (tid=0x%016" PRIx64 ") did not receive ACK "
			"on every segment in the current send window.\n",
			osmv_txn_get_tid(p_txn));

		p_send_ctx = osmv_txn_get_rmpp_send_ctx(p_txn);
		if (num_regs <= OSM_DEFAULT_RETRY_COUNT) {
			/* We still did not exceed the limit of retransmissions.
			 * Set the next timeout's value.
			 */
			ret =
			    cl_get_time_stamp() +
			    1000 * p_bo->p_vendor->resp_timeout;
		} else {
			p_send_ctx->status = IB_TIMEOUT;

			p_mad = osm_madw_get_mad_ptr(p_madw);
			p_mad_addr = osm_madw_get_mad_addr_ptr(p_madw);

			/* Send an ABORT to the other side */
			osmv_rmpp_send_nak((osm_bind_handle_t) p_bo, p_mad,
					   p_mad_addr, IB_RMPP_TYPE_ABORT,
					   IB_RMPP_STATUS_T2L);
		}

		/* Wake the RMPP sender thread up */
		cl_event_signal(&p_send_ctx->event);
		break;

	case OSMV_TXN_RMPP_RECEIVER:
		osm_log(p_bo->p_vendor->p_log, OSM_LOG_DEBUG,
			"Transaction timeout on an RMPP receiver "
			"(tid=0x%016" PRIx64 "). Dropping the transaction.\n",
			osmv_txn_get_tid(p_txn));

		osmv_txn_done((osm_bind_handle_t) p_bo, key,
			      TRUE /*in timeout callback */ );

		if (FALSE == osmv_txn_is_rmpp_init_by_peer(p_txn)) {
			/* This is a requester, still waiting for the reply. Apply the callback */
			invoke_err_cb = TRUE;
		}

		break;

	default:
		CL_ASSERT(FALSE);
	}

	if (TRUE == invoke_err_cb) {
		CL_ASSERT(NULL != p_madw);
		/* update the status in the p_madw */
		p_madw->status = IB_TIMEOUT;
		p_bo->send_err_cb(p_bo->cb_context, p_madw);
		/* no re-registration */
		ret = 0;
	}

txn_done:
	OSM_LOG_EXIT(p_bo->p_vendor->p_log);
	return ret;
}
