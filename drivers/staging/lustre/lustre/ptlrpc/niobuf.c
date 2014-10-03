/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.sun.com/software/products/lustre/docs/GPLv2.pdf
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2002, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#define DEBUG_SUBSYSTEM S_RPC
#include "../include/obd_support.h"
#include "../include/lustre_net.h"
#include "../include/lustre_lib.h"
#include "../include/obd.h"
#include "../include/obd_class.h"
#include "ptlrpc_internal.h"

/**
 * Helper function. Sends \a len bytes from \a base at offset \a offset
 * over \a conn connection to portal \a portal.
 * Returns 0 on success or error code.
 */
static int ptl_send_buf(lnet_handle_md_t *mdh, void *base, int len,
			lnet_ack_req_t ack, struct ptlrpc_cb_id *cbid,
			struct ptlrpc_connection *conn, int portal, __u64 xid,
			unsigned int offset)
{
	int	      rc;
	lnet_md_t	 md;

	LASSERT(portal != 0);
	LASSERT(conn != NULL);
	CDEBUG(D_INFO, "conn=%p id %s\n", conn, libcfs_id2str(conn->c_peer));
	md.start     = base;
	md.length    = len;
	md.threshold = (ack == LNET_ACK_REQ) ? 2 : 1;
	md.options   = PTLRPC_MD_OPTIONS;
	md.user_ptr  = cbid;
	md.eq_handle = ptlrpc_eq_h;

	if (unlikely(ack == LNET_ACK_REQ &&
		     OBD_FAIL_CHECK_ORSET(OBD_FAIL_PTLRPC_ACK,
					  OBD_FAIL_ONCE))) {
		/* don't ask for the ack to simulate failing client */
		ack = LNET_NOACK_REQ;
	}

	rc = LNetMDBind(md, LNET_UNLINK, mdh);
	if (unlikely(rc != 0)) {
		CERROR("LNetMDBind failed: %d\n", rc);
		LASSERT(rc == -ENOMEM);
		return -ENOMEM;
	}

	CDEBUG(D_NET, "Sending %d bytes to portal %d, xid %lld, offset %u\n",
	       len, portal, xid, offset);

	rc = LNetPut(conn->c_self, *mdh, ack,
		     conn->c_peer, portal, xid, offset, 0);
	if (unlikely(rc != 0)) {
		int rc2;
		/* We're going to get an UNLINK event when I unlink below,
		 * which will complete just like any other failed send, so
		 * I fall through and return success here! */
		CERROR("LNetPut(%s, %d, %lld) failed: %d\n",
		       libcfs_id2str(conn->c_peer), portal, xid, rc);
		rc2 = LNetMDUnlink(*mdh);
		LASSERTF(rc2 == 0, "rc2 = %d\n", rc2);
	}

	return 0;
}

static void mdunlink_iterate_helper(lnet_handle_md_t *bd_mds, int count)
{
	int i;

	for (i = 0; i < count; i++)
		LNetMDUnlink(bd_mds[i]);
}


/**
 * Register bulk at the sender for later transfer.
 * Returns 0 on success or error code.
 */
int ptlrpc_register_bulk(struct ptlrpc_request *req)
{
	struct ptlrpc_bulk_desc *desc = req->rq_bulk;
	lnet_process_id_t peer;
	int rc = 0;
	int rc2;
	int posted_md;
	int total_md;
	__u64 xid;
	lnet_handle_me_t  me_h;
	lnet_md_t	 md;

	if (OBD_FAIL_CHECK(OBD_FAIL_PTLRPC_BULK_GET_NET))
		return 0;

	/* NB no locking required until desc is on the network */
	LASSERT(desc->bd_nob > 0);
	LASSERT(desc->bd_md_count == 0);
	LASSERT(desc->bd_md_max_brw <= PTLRPC_BULK_OPS_COUNT);
	LASSERT(desc->bd_iov_count <= PTLRPC_MAX_BRW_PAGES);
	LASSERT(desc->bd_req != NULL);
	LASSERT(desc->bd_type == BULK_PUT_SINK ||
		desc->bd_type == BULK_GET_SOURCE);

	/* cleanup the state of the bulk for it will be reused */
	if (req->rq_resend || req->rq_send_state == LUSTRE_IMP_REPLAY)
		desc->bd_nob_transferred = 0;
	else
		LASSERT(desc->bd_nob_transferred == 0);

	desc->bd_failure = 0;

	peer = desc->bd_import->imp_connection->c_peer;

	LASSERT(desc->bd_cbid.cbid_fn == client_bulk_callback);
	LASSERT(desc->bd_cbid.cbid_arg == desc);

	/* An XID is only used for a single request from the client.
	 * For retried bulk transfers, a new XID will be allocated in
	 * in ptlrpc_check_set() if it needs to be resent, so it is not
	 * using the same RDMA match bits after an error.
	 *
	 * For multi-bulk RPCs, rq_xid is the last XID needed for bulks. The
	 * first bulk XID is power-of-two aligned before rq_xid. LU-1431 */
	xid = req->rq_xid & ~((__u64)desc->bd_md_max_brw - 1);
	LASSERTF(!(desc->bd_registered &&
		   req->rq_send_state != LUSTRE_IMP_REPLAY) ||
		 xid != desc->bd_last_xid,
		 "registered: %d  rq_xid: %llu bd_last_xid: %llu\n",
		 desc->bd_registered, xid, desc->bd_last_xid);

	total_md = (desc->bd_iov_count + LNET_MAX_IOV - 1) / LNET_MAX_IOV;
	desc->bd_registered = 1;
	desc->bd_last_xid = xid;
	desc->bd_md_count = total_md;
	md.user_ptr = &desc->bd_cbid;
	md.eq_handle = ptlrpc_eq_h;
	md.threshold = 1;		       /* PUT or GET */

	for (posted_md = 0; posted_md < total_md; posted_md++, xid++) {
		md.options = PTLRPC_MD_OPTIONS |
			     ((desc->bd_type == BULK_GET_SOURCE) ?
			      LNET_MD_OP_GET : LNET_MD_OP_PUT);
		ptlrpc_fill_bulk_md(&md, desc, posted_md);

		rc = LNetMEAttach(desc->bd_portal, peer, xid, 0,
				  LNET_UNLINK, LNET_INS_AFTER, &me_h);
		if (rc != 0) {
			CERROR("%s: LNetMEAttach failed x%llu/%d: rc = %d\n",
			       desc->bd_import->imp_obd->obd_name, xid,
			       posted_md, rc);
			break;
		}

		/* About to let the network at it... */
		rc = LNetMDAttach(me_h, md, LNET_UNLINK,
				  &desc->bd_mds[posted_md]);
		if (rc != 0) {
			CERROR("%s: LNetMDAttach failed x%llu/%d: rc = %d\n",
			       desc->bd_import->imp_obd->obd_name, xid,
			       posted_md, rc);
			rc2 = LNetMEUnlink(me_h);
			LASSERT(rc2 == 0);
			break;
		}
	}

	if (rc != 0) {
		LASSERT(rc == -ENOMEM);
		spin_lock(&desc->bd_lock);
		desc->bd_md_count -= total_md - posted_md;
		spin_unlock(&desc->bd_lock);
		LASSERT(desc->bd_md_count >= 0);
		mdunlink_iterate_helper(desc->bd_mds, desc->bd_md_max_brw);
		req->rq_status = -ENOMEM;
		return -ENOMEM;
	}

	/* Set rq_xid to matchbits of the final bulk so that server can
	 * infer the number of bulks that were prepared */
	req->rq_xid = --xid;
	LASSERTF(desc->bd_last_xid == (req->rq_xid & PTLRPC_BULK_OPS_MASK),
		 "bd_last_xid = x%llu, rq_xid = x%llu\n",
		 desc->bd_last_xid, req->rq_xid);

	spin_lock(&desc->bd_lock);
	/* Holler if peer manages to touch buffers before he knows the xid */
	if (desc->bd_md_count != total_md)
		CWARN("%s: Peer %s touched %d buffers while I registered\n",
		      desc->bd_import->imp_obd->obd_name, libcfs_id2str(peer),
		      total_md - desc->bd_md_count);
	spin_unlock(&desc->bd_lock);

	CDEBUG(D_NET, "Setup %u bulk %s buffers: %u pages %u bytes, "
	       "xid x%#llx-%#llx, portal %u\n", desc->bd_md_count,
	       desc->bd_type == BULK_GET_SOURCE ? "get-source" : "put-sink",
	       desc->bd_iov_count, desc->bd_nob,
	       desc->bd_last_xid, req->rq_xid, desc->bd_portal);

	return 0;
}
EXPORT_SYMBOL(ptlrpc_register_bulk);

/**
 * Disconnect a bulk desc from the network. Idempotent. Not
 * thread-safe (i.e. only interlocks with completion callback).
 * Returns 1 on success or 0 if network unregistration failed for whatever
 * reason.
 */
int ptlrpc_unregister_bulk(struct ptlrpc_request *req, int async)
{
	struct ptlrpc_bulk_desc *desc = req->rq_bulk;
	wait_queue_head_t	     *wq;
	struct l_wait_info       lwi;
	int		      rc;

	LASSERT(!in_interrupt());     /* might sleep */

	/* Let's setup deadline for reply unlink. */
	if (OBD_FAIL_CHECK(OBD_FAIL_PTLRPC_LONG_BULK_UNLINK) &&
	    async && req->rq_bulk_deadline == 0)
		req->rq_bulk_deadline = get_seconds() + LONG_UNLINK;

	if (ptlrpc_client_bulk_active(req) == 0)	/* completed or */
		return 1;				/* never registered */

	LASSERT(desc->bd_req == req);  /* bd_req NULL until registered */

	/* the unlink ensures the callback happens ASAP and is the last
	 * one.  If it fails, it must be because completion just happened,
	 * but we must still l_wait_event() in this case to give liblustre
	 * a chance to run client_bulk_callback() */
	mdunlink_iterate_helper(desc->bd_mds, desc->bd_md_max_brw);

	if (ptlrpc_client_bulk_active(req) == 0)	/* completed or */
		return 1;				/* never registered */

	/* Move to "Unregistering" phase as bulk was not unlinked yet. */
	ptlrpc_rqphase_move(req, RQ_PHASE_UNREGISTERING);

	/* Do not wait for unlink to finish. */
	if (async)
		return 0;

	if (req->rq_set != NULL)
		wq = &req->rq_set->set_waitq;
	else
		wq = &req->rq_reply_waitq;

	for (;;) {
		/* Network access will complete in finite time but the HUGE
		 * timeout lets us CWARN for visibility of sluggish NALs */
		lwi = LWI_TIMEOUT_INTERVAL(cfs_time_seconds(LONG_UNLINK),
					   cfs_time_seconds(1), NULL, NULL);
		rc = l_wait_event(*wq, !ptlrpc_client_bulk_active(req), &lwi);
		if (rc == 0) {
			ptlrpc_rqphase_move(req, req->rq_next_phase);
			return 1;
		}

		LASSERT(rc == -ETIMEDOUT);
		DEBUG_REQ(D_WARNING, req, "Unexpectedly long timeout: desc %p",
			  desc);
	}
	return 0;
}
EXPORT_SYMBOL(ptlrpc_unregister_bulk);

static void ptlrpc_at_set_reply(struct ptlrpc_request *req, int flags)
{
	struct ptlrpc_service_part	*svcpt = req->rq_rqbd->rqbd_svcpt;
	struct ptlrpc_service		*svc = svcpt->scp_service;
	int service_time = max_t(int, get_seconds() -
				 req->rq_arrival_time.tv_sec, 1);

	if (!(flags & PTLRPC_REPLY_EARLY) &&
	    (req->rq_type != PTL_RPC_MSG_ERR) &&
	    (req->rq_reqmsg != NULL) &&
	    !(lustre_msg_get_flags(req->rq_reqmsg) &
	      (MSG_RESENT | MSG_REPLAY |
	       MSG_REQ_REPLAY_DONE | MSG_LOCK_REPLAY_DONE))) {
		/* early replies, errors and recovery requests don't count
		 * toward our service time estimate */
		int oldse = at_measured(&svcpt->scp_at_estimate, service_time);

		if (oldse != 0) {
			DEBUG_REQ(D_ADAPTTO, req,
				  "svc %s changed estimate from %d to %d",
				  svc->srv_name, oldse,
				  at_get(&svcpt->scp_at_estimate));
		}
	}
	/* Report actual service time for client latency calc */
	lustre_msg_set_service_time(req->rq_repmsg, service_time);
	/* Report service time estimate for future client reqs, but report 0
	 * (to be ignored by client) if it's a error reply during recovery.
	 * (bz15815) */
	if (req->rq_type == PTL_RPC_MSG_ERR &&
	    (req->rq_export == NULL || req->rq_export->exp_obd->obd_recovering))
		lustre_msg_set_timeout(req->rq_repmsg, 0);
	else
		lustre_msg_set_timeout(req->rq_repmsg,
				       at_get(&svcpt->scp_at_estimate));

	if (req->rq_reqmsg &&
	    !(lustre_msghdr_get_flags(req->rq_reqmsg) & MSGHDR_AT_SUPPORT)) {
		CDEBUG(D_ADAPTTO, "No early reply support: flags=%#x "
		       "req_flags=%#x magic=%d:%x/%x len=%d\n",
		       flags, lustre_msg_get_flags(req->rq_reqmsg),
		       lustre_msg_is_v1(req->rq_reqmsg),
		       lustre_msg_get_magic(req->rq_reqmsg),
		       lustre_msg_get_magic(req->rq_repmsg), req->rq_replen);
	}
}

/**
 * Send request reply from request \a req reply buffer.
 * \a flags defines reply types
 * Returns 0 on success or error code
 */
int ptlrpc_send_reply(struct ptlrpc_request *req, int flags)
{
	struct ptlrpc_reply_state *rs = req->rq_reply_state;
	struct ptlrpc_connection  *conn;
	int			rc;

	/* We must already have a reply buffer (only ptlrpc_error() may be
	 * called without one). The reply generated by sptlrpc layer (e.g.
	 * error notify, etc.) might have NULL rq->reqmsg; Otherwise we must
	 * have a request buffer which is either the actual (swabbed) incoming
	 * request, or a saved copy if this is a req saved in
	 * target_queue_final_reply().
	 */
	LASSERT(req->rq_no_reply == 0);
	LASSERT(req->rq_reqbuf != NULL);
	LASSERT(rs != NULL);
	LASSERT((flags & PTLRPC_REPLY_MAYBE_DIFFICULT) || !rs->rs_difficult);
	LASSERT(req->rq_repmsg != NULL);
	LASSERT(req->rq_repmsg == rs->rs_msg);
	LASSERT(rs->rs_cb_id.cbid_fn == reply_out_callback);
	LASSERT(rs->rs_cb_id.cbid_arg == rs);

	/* There may be no rq_export during failover */

	if (unlikely(req->rq_export && req->rq_export->exp_obd &&
		     req->rq_export->exp_obd->obd_fail)) {
		/* Failed obd's only send ENODEV */
		req->rq_type = PTL_RPC_MSG_ERR;
		req->rq_status = -ENODEV;
		CDEBUG(D_HA, "sending ENODEV from failed obd %d\n",
		       req->rq_export->exp_obd->obd_minor);
	}

	/* In order to keep interoprability with the client (< 2.3) which
	 * doesn't have pb_jobid in ptlrpc_body, We have to shrink the
	 * ptlrpc_body in reply buffer to ptlrpc_body_v2, otherwise, the
	 * reply buffer on client will be overflow.
	 *
	 * XXX Remove this whenever we drop the interoprability with such client.
	 */
	req->rq_replen = lustre_shrink_msg(req->rq_repmsg, 0,
					   sizeof(struct ptlrpc_body_v2), 1);

	if (req->rq_type != PTL_RPC_MSG_ERR)
		req->rq_type = PTL_RPC_MSG_REPLY;

	lustre_msg_set_type(req->rq_repmsg, req->rq_type);
	lustre_msg_set_status(req->rq_repmsg,
			      ptlrpc_status_hton(req->rq_status));
	lustre_msg_set_opc(req->rq_repmsg,
		req->rq_reqmsg ? lustre_msg_get_opc(req->rq_reqmsg) : 0);

	target_pack_pool_reply(req);

	ptlrpc_at_set_reply(req, flags);

	if (req->rq_export == NULL || req->rq_export->exp_connection == NULL)
		conn = ptlrpc_connection_get(req->rq_peer, req->rq_self, NULL);
	else
		conn = ptlrpc_connection_addref(req->rq_export->exp_connection);

	if (unlikely(conn == NULL)) {
		CERROR("not replying on NULL connection\n"); /* bug 9635 */
		return -ENOTCONN;
	}
	ptlrpc_rs_addref(rs);		   /* +1 ref for the network */

	rc = sptlrpc_svc_wrap_reply(req);
	if (unlikely(rc))
		goto out;

	req->rq_sent = get_seconds();

	rc = ptl_send_buf(&rs->rs_md_h, rs->rs_repbuf, rs->rs_repdata_len,
			  (rs->rs_difficult && !rs->rs_no_ack) ?
			  LNET_ACK_REQ : LNET_NOACK_REQ,
			  &rs->rs_cb_id, conn,
			  ptlrpc_req2svc(req)->srv_rep_portal,
			  req->rq_xid, req->rq_reply_off);
out:
	if (unlikely(rc != 0))
		ptlrpc_req_drop_rs(req);
	ptlrpc_connection_put(conn);
	return rc;
}
EXPORT_SYMBOL(ptlrpc_send_reply);

int ptlrpc_reply(struct ptlrpc_request *req)
{
	if (req->rq_no_reply)
		return 0;
	else
		return (ptlrpc_send_reply(req, 0));
}
EXPORT_SYMBOL(ptlrpc_reply);

/**
 * For request \a req send an error reply back. Create empty
 * reply buffers if necessary.
 */
int ptlrpc_send_error(struct ptlrpc_request *req, int may_be_difficult)
{
	int rc;

	if (req->rq_no_reply)
		return 0;

	if (!req->rq_repmsg) {
		rc = lustre_pack_reply(req, 1, NULL, NULL);
		if (rc)
			return rc;
	}

	if (req->rq_status != -ENOSPC && req->rq_status != -EACCES &&
	    req->rq_status != -EPERM && req->rq_status != -ENOENT &&
	    req->rq_status != -EINPROGRESS && req->rq_status != -EDQUOT)
		req->rq_type = PTL_RPC_MSG_ERR;

	rc = ptlrpc_send_reply(req, may_be_difficult);
	return rc;
}
EXPORT_SYMBOL(ptlrpc_send_error);

int ptlrpc_error(struct ptlrpc_request *req)
{
	return ptlrpc_send_error(req, 0);
}
EXPORT_SYMBOL(ptlrpc_error);

/**
 * Send request \a request.
 * if \a noreply is set, don't expect any reply back and don't set up
 * reply buffers.
 * Returns 0 on success or error code.
 */
int ptl_send_rpc(struct ptlrpc_request *request, int noreply)
{
	int rc;
	int rc2;
	int mpflag = 0;
	struct ptlrpc_connection *connection;
	lnet_handle_me_t  reply_me_h;
	lnet_md_t	 reply_md;
	struct obd_device *obd = request->rq_import->imp_obd;

	if (OBD_FAIL_CHECK(OBD_FAIL_PTLRPC_DROP_RPC))
		return 0;

	LASSERT(request->rq_type == PTL_RPC_MSG_REQUEST);
	LASSERT(request->rq_wait_ctx == 0);

	/* If this is a re-transmit, we're required to have disengaged
	 * cleanly from the previous attempt */
	LASSERT(!request->rq_receiving_reply);
	LASSERT(!((lustre_msg_get_flags(request->rq_reqmsg) & MSG_REPLAY) &&
		(request->rq_import->imp_state == LUSTRE_IMP_FULL)));

	if (unlikely(obd != NULL && obd->obd_fail)) {
		CDEBUG(D_HA, "muting rpc for failed imp obd %s\n",
			obd->obd_name);
		/* this prevents us from waiting in ptlrpc_queue_wait */
		spin_lock(&request->rq_lock);
		request->rq_err = 1;
		spin_unlock(&request->rq_lock);
		request->rq_status = -ENODEV;
		return -ENODEV;
	}

	connection = request->rq_import->imp_connection;

	lustre_msg_set_handle(request->rq_reqmsg,
			      &request->rq_import->imp_remote_handle);
	lustre_msg_set_type(request->rq_reqmsg, PTL_RPC_MSG_REQUEST);
	lustre_msg_set_conn_cnt(request->rq_reqmsg,
				request->rq_import->imp_conn_cnt);
	lustre_msghdr_set_flags(request->rq_reqmsg,
				request->rq_import->imp_msghdr_flags);

	if (request->rq_resend)
		lustre_msg_add_flags(request->rq_reqmsg, MSG_RESENT);

	if (request->rq_memalloc)
		mpflag = cfs_memory_pressure_get_and_set();

	rc = sptlrpc_cli_wrap_request(request);
	if (rc)
		GOTO(out, rc);

	/* bulk register should be done after wrap_request() */
	if (request->rq_bulk != NULL) {
		rc = ptlrpc_register_bulk(request);
		if (rc != 0)
			GOTO(out, rc);
	}

	if (!noreply) {
		LASSERT(request->rq_replen != 0);
		if (request->rq_repbuf == NULL) {
			LASSERT(request->rq_repdata == NULL);
			LASSERT(request->rq_repmsg == NULL);
			rc = sptlrpc_cli_alloc_repbuf(request,
						      request->rq_replen);
			if (rc) {
				/* this prevents us from looping in
				 * ptlrpc_queue_wait */
				spin_lock(&request->rq_lock);
				request->rq_err = 1;
				spin_unlock(&request->rq_lock);
				request->rq_status = rc;
				GOTO(cleanup_bulk, rc);
			}
		} else {
			request->rq_repdata = NULL;
			request->rq_repmsg = NULL;
		}

		rc = LNetMEAttach(request->rq_reply_portal,/*XXX FIXME bug 249*/
				  connection->c_peer, request->rq_xid, 0,
				  LNET_UNLINK, LNET_INS_AFTER, &reply_me_h);
		if (rc != 0) {
			CERROR("LNetMEAttach failed: %d\n", rc);
			LASSERT(rc == -ENOMEM);
			GOTO(cleanup_bulk, rc = -ENOMEM);
		}
	}

	spin_lock(&request->rq_lock);
	/* If the MD attach succeeds, there _will_ be a reply_in callback */
	request->rq_receiving_reply = !noreply;
	request->rq_req_unlink = 1;
	/* We are responsible for unlinking the reply buffer */
	request->rq_reply_unlink = !noreply;
	/* Clear any flags that may be present from previous sends. */
	request->rq_replied = 0;
	request->rq_err = 0;
	request->rq_timedout = 0;
	request->rq_net_err = 0;
	request->rq_resend = 0;
	request->rq_restart = 0;
	request->rq_reply_truncate = 0;
	spin_unlock(&request->rq_lock);

	if (!noreply) {
		reply_md.start     = request->rq_repbuf;
		reply_md.length    = request->rq_repbuf_len;
		/* Allow multiple early replies */
		reply_md.threshold = LNET_MD_THRESH_INF;
		/* Manage remote for early replies */
		reply_md.options   = PTLRPC_MD_OPTIONS | LNET_MD_OP_PUT |
			LNET_MD_MANAGE_REMOTE |
			LNET_MD_TRUNCATE; /* allow to make EOVERFLOW error */;
		reply_md.user_ptr  = &request->rq_reply_cbid;
		reply_md.eq_handle = ptlrpc_eq_h;

		/* We must see the unlink callback to unset rq_reply_unlink,
		   so we can't auto-unlink */
		rc = LNetMDAttach(reply_me_h, reply_md, LNET_RETAIN,
				  &request->rq_reply_md_h);
		if (rc != 0) {
			CERROR("LNetMDAttach failed: %d\n", rc);
			LASSERT(rc == -ENOMEM);
			spin_lock(&request->rq_lock);
			/* ...but the MD attach didn't succeed... */
			request->rq_receiving_reply = 0;
			spin_unlock(&request->rq_lock);
			GOTO(cleanup_me, rc = -ENOMEM);
		}

		CDEBUG(D_NET, "Setup reply buffer: %u bytes, xid %llu, portal %u\n",
		       request->rq_repbuf_len, request->rq_xid,
		       request->rq_reply_portal);
	}

	/* add references on request for request_out_callback */
	ptlrpc_request_addref(request);
	if (obd != NULL && obd->obd_svc_stats != NULL)
		lprocfs_counter_add(obd->obd_svc_stats, PTLRPC_REQACTIVE_CNTR,
			atomic_read(&request->rq_import->imp_inflight));

	OBD_FAIL_TIMEOUT(OBD_FAIL_PTLRPC_DELAY_SEND, request->rq_timeout + 5);

	do_gettimeofday(&request->rq_arrival_time);
	request->rq_sent = get_seconds();
	/* We give the server rq_timeout secs to process the req, and
	   add the network latency for our local timeout. */
	request->rq_deadline = request->rq_sent + request->rq_timeout +
		ptlrpc_at_get_net_latency(request);

	ptlrpc_pinger_sending_on_import(request->rq_import);

	DEBUG_REQ(D_INFO, request, "send flg=%x",
		  lustre_msg_get_flags(request->rq_reqmsg));
	rc = ptl_send_buf(&request->rq_req_md_h,
			  request->rq_reqbuf, request->rq_reqdata_len,
			  LNET_NOACK_REQ, &request->rq_req_cbid,
			  connection,
			  request->rq_request_portal,
			  request->rq_xid, 0);
	if (rc == 0)
		GOTO(out, rc);

	ptlrpc_req_finished(request);
	if (noreply)
		GOTO(out, rc);

 cleanup_me:
	/* MEUnlink is safe; the PUT didn't even get off the ground, and
	 * nobody apart from the PUT's target has the right nid+XID to
	 * access the reply buffer. */
	rc2 = LNetMEUnlink(reply_me_h);
	LASSERT(rc2 == 0);
	/* UNLINKED callback called synchronously */
	LASSERT(!request->rq_receiving_reply);

 cleanup_bulk:
	/* We do sync unlink here as there was no real transfer here so
	 * the chance to have long unlink to sluggish net is smaller here. */
	ptlrpc_unregister_bulk(request, 0);
 out:
	if (request->rq_memalloc)
		cfs_memory_pressure_restore(mpflag);
	return rc;
}
EXPORT_SYMBOL(ptl_send_rpc);

/**
 * Register request buffer descriptor for request receiving.
 */
int ptlrpc_register_rqbd(struct ptlrpc_request_buffer_desc *rqbd)
{
	struct ptlrpc_service	  *service = rqbd->rqbd_svcpt->scp_service;
	static lnet_process_id_t  match_id = {LNET_NID_ANY, LNET_PID_ANY};
	int			  rc;
	lnet_md_t		 md;
	lnet_handle_me_t	  me_h;

	CDEBUG(D_NET, "LNetMEAttach: portal %d\n",
	       service->srv_req_portal);

	if (OBD_FAIL_CHECK(OBD_FAIL_PTLRPC_RQBD))
		return (-ENOMEM);

	/* NB: CPT affinity service should use new LNet flag LNET_INS_LOCAL,
	 * which means buffer can only be attached on local CPT, and LND
	 * threads can find it by grabbing a local lock */
	rc = LNetMEAttach(service->srv_req_portal,
			  match_id, 0, ~0, LNET_UNLINK,
			  rqbd->rqbd_svcpt->scp_cpt >= 0 ?
			  LNET_INS_LOCAL : LNET_INS_AFTER, &me_h);
	if (rc != 0) {
		CERROR("LNetMEAttach failed: %d\n", rc);
		return (-ENOMEM);
	}

	LASSERT(rqbd->rqbd_refcount == 0);
	rqbd->rqbd_refcount = 1;

	md.start     = rqbd->rqbd_buffer;
	md.length    = service->srv_buf_size;
	md.max_size  = service->srv_max_req_size;
	md.threshold = LNET_MD_THRESH_INF;
	md.options   = PTLRPC_MD_OPTIONS | LNET_MD_OP_PUT | LNET_MD_MAX_SIZE;
	md.user_ptr  = &rqbd->rqbd_cbid;
	md.eq_handle = ptlrpc_eq_h;

	rc = LNetMDAttach(me_h, md, LNET_UNLINK, &rqbd->rqbd_md_h);
	if (rc == 0)
		return (0);

	CERROR("LNetMDAttach failed: %d;\n", rc);
	LASSERT(rc == -ENOMEM);
	rc = LNetMEUnlink(me_h);
	LASSERT(rc == 0);
	rqbd->rqbd_refcount = 0;

	return (-ENOMEM);
}
