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
 * http://www.gnu.org/licenses/gpl-2.0.html
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lnet/selftest/conctl.c
 *
 * Console framework rpcs
 *
 * Author: Liang Zhen <liang@whamcloud.com>
 */

#include "../../include/linux/libcfs/libcfs.h"
#include "../../include/linux/lnet/lib-lnet.h"
#include "timer.h"
#include "conrpc.h"
#include "console.h"

void lstcon_rpc_stat_reply(struct lstcon_rpc_trans *, struct srpc_msg *,
			   struct lstcon_node *, struct lstcon_trans_stat *);

static void
lstcon_rpc_done(struct srpc_client_rpc *rpc)
{
	struct lstcon_rpc *crpc = (struct lstcon_rpc *)rpc->crpc_priv;

	LASSERT(crpc && rpc == crpc->crp_rpc);
	LASSERT(crpc->crp_posted && !crpc->crp_finished);

	spin_lock(&rpc->crpc_lock);

	if (!crpc->crp_trans) {
		/*
		 * Orphan RPC is not in any transaction,
		 * I'm just a poor body and nobody loves me
		 */
		spin_unlock(&rpc->crpc_lock);

		/* release it */
		lstcon_rpc_put(crpc);
		return;
	}

	/* not an orphan RPC */
	crpc->crp_finished = 1;

	if (!crpc->crp_stamp) {
		/* not aborted */
		LASSERT(!crpc->crp_status);

		crpc->crp_stamp = cfs_time_current();
		crpc->crp_status = rpc->crpc_status;
	}

	/* wakeup (transaction)thread if I'm the last RPC in the transaction */
	if (atomic_dec_and_test(&crpc->crp_trans->tas_remaining))
		wake_up(&crpc->crp_trans->tas_waitq);

	spin_unlock(&rpc->crpc_lock);
}

static int
lstcon_rpc_init(struct lstcon_node *nd, int service, unsigned int feats,
		int bulk_npg, int bulk_len, int embedded,
		struct lstcon_rpc *crpc)
{
	crpc->crp_rpc = sfw_create_rpc(nd->nd_id, service,
				       feats, bulk_npg, bulk_len,
				       lstcon_rpc_done, (void *)crpc);
	if (!crpc->crp_rpc)
		return -ENOMEM;

	crpc->crp_trans = NULL;
	crpc->crp_node = nd;
	crpc->crp_posted = 0;
	crpc->crp_finished = 0;
	crpc->crp_unpacked = 0;
	crpc->crp_status = 0;
	crpc->crp_stamp = 0;
	crpc->crp_embedded = embedded;
	INIT_LIST_HEAD(&crpc->crp_link);

	atomic_inc(&console_session.ses_rpc_counter);

	return 0;
}

static int
lstcon_rpc_prep(struct lstcon_node *nd, int service, unsigned int feats,
		int bulk_npg, int bulk_len, struct lstcon_rpc **crpcpp)
{
	struct lstcon_rpc *crpc = NULL;
	int rc;

	spin_lock(&console_session.ses_rpc_lock);

	crpc = list_first_entry_or_null(&console_session.ses_rpc_freelist,
					struct lstcon_rpc, crp_link);
	if (crpc)
		list_del_init(&crpc->crp_link);

	spin_unlock(&console_session.ses_rpc_lock);

	if (!crpc) {
		LIBCFS_ALLOC(crpc, sizeof(*crpc));
		if (!crpc)
			return -ENOMEM;
	}

	rc = lstcon_rpc_init(nd, service, feats, bulk_npg, bulk_len, 0, crpc);
	if (!rc) {
		*crpcpp = crpc;
		return 0;
	}

	LIBCFS_FREE(crpc, sizeof(*crpc));

	return rc;
}

void
lstcon_rpc_put(struct lstcon_rpc *crpc)
{
	struct srpc_bulk *bulk = &crpc->crp_rpc->crpc_bulk;
	int i;

	LASSERT(list_empty(&crpc->crp_link));

	for (i = 0; i < bulk->bk_niov; i++) {
		if (!bulk->bk_iovs[i].bv_page)
			continue;

		__free_page(bulk->bk_iovs[i].bv_page);
	}

	srpc_client_rpc_decref(crpc->crp_rpc);

	if (crpc->crp_embedded) {
		/* embedded RPC, don't recycle it */
		memset(crpc, 0, sizeof(*crpc));
		crpc->crp_embedded = 1;

	} else {
		spin_lock(&console_session.ses_rpc_lock);

		list_add(&crpc->crp_link,
			 &console_session.ses_rpc_freelist);

		spin_unlock(&console_session.ses_rpc_lock);
	}

	/* RPC is not alive now */
	atomic_dec(&console_session.ses_rpc_counter);
}

static void
lstcon_rpc_post(struct lstcon_rpc *crpc)
{
	struct lstcon_rpc_trans *trans = crpc->crp_trans;

	LASSERT(trans);

	atomic_inc(&trans->tas_remaining);
	crpc->crp_posted = 1;

	sfw_post_rpc(crpc->crp_rpc);
}

static char *
lstcon_rpc_trans_name(int transop)
{
	if (transop == LST_TRANS_SESNEW)
		return "SESNEW";

	if (transop == LST_TRANS_SESEND)
		return "SESEND";

	if (transop == LST_TRANS_SESQRY)
		return "SESQRY";

	if (transop == LST_TRANS_SESPING)
		return "SESPING";

	if (transop == LST_TRANS_TSBCLIADD)
		return "TSBCLIADD";

	if (transop == LST_TRANS_TSBSRVADD)
		return "TSBSRVADD";

	if (transop == LST_TRANS_TSBRUN)
		return "TSBRUN";

	if (transop == LST_TRANS_TSBSTOP)
		return "TSBSTOP";

	if (transop == LST_TRANS_TSBCLIQRY)
		return "TSBCLIQRY";

	if (transop == LST_TRANS_TSBSRVQRY)
		return "TSBSRVQRY";

	if (transop == LST_TRANS_STATQRY)
		return "STATQRY";

	return "Unknown";
}

int
lstcon_rpc_trans_prep(struct list_head *translist, int transop,
		      struct lstcon_rpc_trans **transpp)
{
	struct lstcon_rpc_trans *trans;

	if (translist) {
		list_for_each_entry(trans, translist, tas_link) {
			/*
			 * Can't enqueue two private transaction on
			 * the same object
			 */
			if ((trans->tas_opc & transop) == LST_TRANS_PRIVATE)
				return -EPERM;
		}
	}

	/* create a trans group */
	LIBCFS_ALLOC(trans, sizeof(*trans));
	if (!trans)
		return -ENOMEM;

	trans->tas_opc = transop;

	if (!translist)
		INIT_LIST_HEAD(&trans->tas_olink);
	else
		list_add_tail(&trans->tas_olink, translist);

	list_add_tail(&trans->tas_link, &console_session.ses_trans_list);

	INIT_LIST_HEAD(&trans->tas_rpcs_list);
	atomic_set(&trans->tas_remaining, 0);
	init_waitqueue_head(&trans->tas_waitq);

	spin_lock(&console_session.ses_rpc_lock);
	trans->tas_features = console_session.ses_features;
	spin_unlock(&console_session.ses_rpc_lock);

	*transpp = trans;
	return 0;
}

void
lstcon_rpc_trans_addreq(struct lstcon_rpc_trans *trans, struct lstcon_rpc *crpc)
{
	list_add_tail(&crpc->crp_link, &trans->tas_rpcs_list);
	crpc->crp_trans = trans;
}

void
lstcon_rpc_trans_abort(struct lstcon_rpc_trans *trans, int error)
{
	struct srpc_client_rpc *rpc;
	struct lstcon_rpc *crpc;
	struct lstcon_node *nd;

	list_for_each_entry(crpc, &trans->tas_rpcs_list, crp_link) {
		rpc = crpc->crp_rpc;

		spin_lock(&rpc->crpc_lock);

		if (!crpc->crp_posted || /* not posted */
		    crpc->crp_stamp) {	 /* rpc done or aborted already */
			if (!crpc->crp_stamp) {
				crpc->crp_stamp = cfs_time_current();
				crpc->crp_status = -EINTR;
			}
			spin_unlock(&rpc->crpc_lock);
			continue;
		}

		crpc->crp_stamp = cfs_time_current();
		crpc->crp_status = error;

		spin_unlock(&rpc->crpc_lock);

		sfw_abort_rpc(rpc);

		if (error != -ETIMEDOUT)
			continue;

		nd = crpc->crp_node;
		if (cfs_time_after(nd->nd_stamp, crpc->crp_stamp))
			continue;

		nd->nd_stamp = crpc->crp_stamp;
		nd->nd_state = LST_NODE_DOWN;
	}
}

static int
lstcon_rpc_trans_check(struct lstcon_rpc_trans *trans)
{
	if (console_session.ses_shutdown &&
	    !list_empty(&trans->tas_olink)) /* Not an end session RPC */
		return 1;

	return !atomic_read(&trans->tas_remaining) ? 1 : 0;
}

int
lstcon_rpc_trans_postwait(struct lstcon_rpc_trans *trans, int timeout)
{
	struct lstcon_rpc *crpc;
	int rc;

	if (list_empty(&trans->tas_rpcs_list))
		return 0;

	if (timeout < LST_TRANS_MIN_TIMEOUT)
		timeout = LST_TRANS_MIN_TIMEOUT;

	CDEBUG(D_NET, "Transaction %s started\n",
	       lstcon_rpc_trans_name(trans->tas_opc));

	/* post all requests */
	list_for_each_entry(crpc, &trans->tas_rpcs_list, crp_link) {
		LASSERT(!crpc->crp_posted);

		lstcon_rpc_post(crpc);
	}

	mutex_unlock(&console_session.ses_mutex);

	rc = wait_event_interruptible_timeout(trans->tas_waitq,
					      lstcon_rpc_trans_check(trans),
					      cfs_time_seconds(timeout));
	rc = (rc > 0) ? 0 : ((rc < 0) ? -EINTR : -ETIMEDOUT);

	mutex_lock(&console_session.ses_mutex);

	if (console_session.ses_shutdown)
		rc = -ESHUTDOWN;

	if (rc || atomic_read(&trans->tas_remaining)) {
		/* treat short timeout as canceled */
		if (rc == -ETIMEDOUT && timeout < LST_TRANS_MIN_TIMEOUT * 2)
			rc = -EINTR;

		lstcon_rpc_trans_abort(trans, rc);
	}

	CDEBUG(D_NET, "Transaction %s stopped: %d\n",
	       lstcon_rpc_trans_name(trans->tas_opc), rc);

	lstcon_rpc_trans_stat(trans, lstcon_trans_stat());

	return rc;
}

static int
lstcon_rpc_get_reply(struct lstcon_rpc *crpc, struct srpc_msg **msgpp)
{
	struct lstcon_node *nd = crpc->crp_node;
	struct srpc_client_rpc *rpc = crpc->crp_rpc;
	struct srpc_generic_reply *rep;

	LASSERT(nd && rpc);
	LASSERT(crpc->crp_stamp);

	if (crpc->crp_status) {
		*msgpp = NULL;
		return crpc->crp_status;
	}

	*msgpp = &rpc->crpc_replymsg;
	if (!crpc->crp_unpacked) {
		sfw_unpack_message(*msgpp);
		crpc->crp_unpacked = 1;
	}

	if (cfs_time_after(nd->nd_stamp, crpc->crp_stamp))
		return 0;

	nd->nd_stamp = crpc->crp_stamp;
	rep = &(*msgpp)->msg_body.reply;

	if (rep->sid.ses_nid == LNET_NID_ANY)
		nd->nd_state = LST_NODE_UNKNOWN;
	else if (lstcon_session_match(rep->sid))
		nd->nd_state = LST_NODE_ACTIVE;
	else
		nd->nd_state = LST_NODE_BUSY;

	return 0;
}

void
lstcon_rpc_trans_stat(struct lstcon_rpc_trans *trans, struct lstcon_trans_stat *stat)
{
	struct lstcon_rpc *crpc;
	struct srpc_msg *rep;
	int error;

	LASSERT(stat);

	memset(stat, 0, sizeof(*stat));

	list_for_each_entry(crpc, &trans->tas_rpcs_list, crp_link) {
		lstcon_rpc_stat_total(stat, 1);

		LASSERT(crpc->crp_stamp);

		error = lstcon_rpc_get_reply(crpc, &rep);
		if (error) {
			lstcon_rpc_stat_failure(stat, 1);
			if (!stat->trs_rpc_errno)
				stat->trs_rpc_errno = -error;

			continue;
		}

		lstcon_rpc_stat_success(stat, 1);

		lstcon_rpc_stat_reply(trans, rep, crpc->crp_node, stat);
	}

	if (trans->tas_opc == LST_TRANS_SESNEW && !stat->trs_fwk_errno) {
		stat->trs_fwk_errno =
		      lstcon_session_feats_check(trans->tas_features);
	}

	CDEBUG(D_NET, "transaction %s : success %d, failure %d, total %d, RPC error(%d), Framework error(%d)\n",
	       lstcon_rpc_trans_name(trans->tas_opc),
	       lstcon_rpc_stat_success(stat, 0),
	       lstcon_rpc_stat_failure(stat, 0),
	       lstcon_rpc_stat_total(stat, 0),
	       stat->trs_rpc_errno, stat->trs_fwk_errno);
}

int
lstcon_rpc_trans_interpreter(struct lstcon_rpc_trans *trans,
			     struct list_head __user *head_up,
			     lstcon_rpc_readent_func_t readent)
{
	struct list_head tmp;
	struct list_head __user *next;
	struct lstcon_rpc_ent *ent;
	struct srpc_generic_reply *rep;
	struct lstcon_rpc *crpc;
	struct srpc_msg *msg;
	struct lstcon_node *nd;
	long dur;
	struct timeval tv;
	int error;

	LASSERT(head_up);

	next = head_up;

	list_for_each_entry(crpc, &trans->tas_rpcs_list, crp_link) {
		if (copy_from_user(&tmp, next,
				   sizeof(struct list_head)))
			return -EFAULT;

		if (tmp.next == head_up)
			return 0;

		next = tmp.next;

		ent = list_entry(next, struct lstcon_rpc_ent, rpe_link);

		LASSERT(crpc->crp_stamp);

		error = lstcon_rpc_get_reply(crpc, &msg);

		nd = crpc->crp_node;

		dur = (long)cfs_time_sub(crpc->crp_stamp,
		      (unsigned long)console_session.ses_id.ses_stamp);
		jiffies_to_timeval(dur, &tv);

		if (copy_to_user(&ent->rpe_peer, &nd->nd_id,
				 sizeof(lnet_process_id_t)) ||
		    copy_to_user(&ent->rpe_stamp, &tv, sizeof(tv)) ||
		    copy_to_user(&ent->rpe_state, &nd->nd_state,
				 sizeof(nd->nd_state)) ||
		    copy_to_user(&ent->rpe_rpc_errno, &error,
				 sizeof(error)))
			return -EFAULT;

		if (error)
			continue;

		/* RPC is done */
		rep = (struct srpc_generic_reply *)&msg->msg_body.reply;

		if (copy_to_user(&ent->rpe_sid, &rep->sid, sizeof(rep->sid)) ||
		    copy_to_user(&ent->rpe_fwk_errno, &rep->status,
				 sizeof(rep->status)))
			return -EFAULT;

		if (!readent)
			continue;

		error = readent(trans->tas_opc, msg, ent);
		if (error)
			return error;
	}

	return 0;
}

void
lstcon_rpc_trans_destroy(struct lstcon_rpc_trans *trans)
{
	struct srpc_client_rpc *rpc;
	struct lstcon_rpc *crpc;
	struct lstcon_rpc *tmp;
	int count = 0;

	list_for_each_entry_safe(crpc, tmp, &trans->tas_rpcs_list, crp_link) {
		rpc = crpc->crp_rpc;

		spin_lock(&rpc->crpc_lock);

		/* free it if not posted or finished already */
		if (!crpc->crp_posted || crpc->crp_finished) {
			spin_unlock(&rpc->crpc_lock);

			list_del_init(&crpc->crp_link);
			lstcon_rpc_put(crpc);

			continue;
		}

		/*
		 * rpcs can be still not callbacked (even LNetMDUnlink is
		 * called) because huge timeout for inaccessible network,
		 * don't make user wait for them, just abandon them, they
		 * will be recycled in callback
		 */
		LASSERT(crpc->crp_status);

		crpc->crp_node = NULL;
		crpc->crp_trans = NULL;
		list_del_init(&crpc->crp_link);
		count++;

		spin_unlock(&rpc->crpc_lock);

		atomic_dec(&trans->tas_remaining);
	}

	LASSERT(!atomic_read(&trans->tas_remaining));

	list_del(&trans->tas_link);
	if (!list_empty(&trans->tas_olink))
		list_del(&trans->tas_olink);

	CDEBUG(D_NET, "Transaction %s destroyed with %d pending RPCs\n",
	       lstcon_rpc_trans_name(trans->tas_opc), count);

	LIBCFS_FREE(trans, sizeof(*trans));
}

int
lstcon_sesrpc_prep(struct lstcon_node *nd, int transop,
		   unsigned int feats, struct lstcon_rpc **crpc)
{
	struct srpc_mksn_reqst *msrq;
	struct srpc_rmsn_reqst *rsrq;
	int rc;

	switch (transop) {
	case LST_TRANS_SESNEW:
		rc = lstcon_rpc_prep(nd, SRPC_SERVICE_MAKE_SESSION,
				     feats, 0, 0, crpc);
		if (rc)
			return rc;

		msrq = &(*crpc)->crp_rpc->crpc_reqstmsg.msg_body.mksn_reqst;
		msrq->mksn_sid = console_session.ses_id;
		msrq->mksn_force = console_session.ses_force;
		strlcpy(msrq->mksn_name, console_session.ses_name,
			sizeof(msrq->mksn_name));
		break;

	case LST_TRANS_SESEND:
		rc = lstcon_rpc_prep(nd, SRPC_SERVICE_REMOVE_SESSION,
				     feats, 0, 0, crpc);
		if (rc)
			return rc;

		rsrq = &(*crpc)->crp_rpc->crpc_reqstmsg.msg_body.rmsn_reqst;
		rsrq->rmsn_sid = console_session.ses_id;
		break;

	default:
		LBUG();
	}

	return 0;
}

int
lstcon_dbgrpc_prep(struct lstcon_node *nd, unsigned int feats,
		   struct lstcon_rpc **crpc)
{
	struct srpc_debug_reqst *drq;
	int rc;

	rc = lstcon_rpc_prep(nd, SRPC_SERVICE_DEBUG, feats, 0, 0, crpc);
	if (rc)
		return rc;

	drq = &(*crpc)->crp_rpc->crpc_reqstmsg.msg_body.dbg_reqst;

	drq->dbg_sid = console_session.ses_id;
	drq->dbg_flags = 0;

	return rc;
}

int
lstcon_batrpc_prep(struct lstcon_node *nd, int transop, unsigned int feats,
		   struct lstcon_tsb_hdr *tsb, struct lstcon_rpc **crpc)
{
	struct lstcon_batch *batch;
	struct srpc_batch_reqst *brq;
	int rc;

	rc = lstcon_rpc_prep(nd, SRPC_SERVICE_BATCH, feats, 0, 0, crpc);
	if (rc)
		return rc;

	brq = &(*crpc)->crp_rpc->crpc_reqstmsg.msg_body.bat_reqst;

	brq->bar_sid = console_session.ses_id;
	brq->bar_bid = tsb->tsb_id;
	brq->bar_testidx = tsb->tsb_index;
	brq->bar_opc = transop == LST_TRANS_TSBRUN ? SRPC_BATCH_OPC_RUN :
		       (transop == LST_TRANS_TSBSTOP ? SRPC_BATCH_OPC_STOP :
		       SRPC_BATCH_OPC_QUERY);

	if (transop != LST_TRANS_TSBRUN &&
	    transop != LST_TRANS_TSBSTOP)
		return 0;

	LASSERT(!tsb->tsb_index);

	batch = (struct lstcon_batch *)tsb;
	brq->bar_arg = batch->bat_arg;

	return 0;
}

int
lstcon_statrpc_prep(struct lstcon_node *nd, unsigned int feats,
		    struct lstcon_rpc **crpc)
{
	struct srpc_stat_reqst *srq;
	int rc;

	rc = lstcon_rpc_prep(nd, SRPC_SERVICE_QUERY_STAT, feats, 0, 0, crpc);
	if (rc)
		return rc;

	srq = &(*crpc)->crp_rpc->crpc_reqstmsg.msg_body.stat_reqst;

	srq->str_sid = console_session.ses_id;
	srq->str_type = 0; /* XXX remove it */

	return 0;
}

static struct lnet_process_id_packed *
lstcon_next_id(int idx, int nkiov, lnet_kiov_t *kiov)
{
	struct lnet_process_id_packed *pid;
	int i;

	i = idx / SFW_ID_PER_PAGE;

	LASSERT(i < nkiov);

	pid = (struct lnet_process_id_packed *)page_address(kiov[i].bv_page);

	return &pid[idx % SFW_ID_PER_PAGE];
}

static int
lstcon_dstnodes_prep(struct lstcon_group *grp, int idx,
		     int dist, int span, int nkiov, lnet_kiov_t *kiov)
{
	struct lnet_process_id_packed *pid;
	struct lstcon_ndlink *ndl;
	struct lstcon_node *nd;
	int start;
	int end;
	int i = 0;

	LASSERT(dist >= 1);
	LASSERT(span >= 1);
	LASSERT(grp->grp_nnode >= 1);

	if (span > grp->grp_nnode)
		return -EINVAL;

	start = ((idx / dist) * span) % grp->grp_nnode;
	end = ((idx / dist) * span + span - 1) % grp->grp_nnode;

	list_for_each_entry(ndl, &grp->grp_ndl_list, ndl_link) {
		nd = ndl->ndl_node;
		if (i < start) {
			i++;
			continue;
		}

		if (i > (end >= start ? end : grp->grp_nnode))
			break;

		pid = lstcon_next_id((i - start), nkiov, kiov);
		pid->nid = nd->nd_id.nid;
		pid->pid = nd->nd_id.pid;
		i++;
	}

	if (start <= end) /* done */
		return 0;

	list_for_each_entry(ndl, &grp->grp_ndl_list, ndl_link) {
		if (i > grp->grp_nnode + end)
			break;

		nd = ndl->ndl_node;
		pid = lstcon_next_id((i - start), nkiov, kiov);
		pid->nid = nd->nd_id.nid;
		pid->pid = nd->nd_id.pid;
		i++;
	}

	return 0;
}

static int
lstcon_pingrpc_prep(struct lst_test_ping_param *param, struct srpc_test_reqst *req)
{
	struct test_ping_req *prq = &req->tsr_u.ping;

	prq->png_size = param->png_size;
	prq->png_flags = param->png_flags;
	/* TODO dest */
	return 0;
}

static int
lstcon_bulkrpc_v0_prep(struct lst_test_bulk_param *param,
		       struct srpc_test_reqst *req)
{
	struct test_bulk_req *brq = &req->tsr_u.bulk_v0;

	brq->blk_opc = param->blk_opc;
	brq->blk_npg = (param->blk_size + PAGE_SIZE - 1) /
			PAGE_SIZE;
	brq->blk_flags = param->blk_flags;

	return 0;
}

static int
lstcon_bulkrpc_v1_prep(struct lst_test_bulk_param *param, bool is_client,
		       struct srpc_test_reqst *req)
{
	struct test_bulk_req_v1 *brq = &req->tsr_u.bulk_v1;

	brq->blk_opc = param->blk_opc;
	brq->blk_flags = param->blk_flags;
	brq->blk_len = param->blk_size;
	brq->blk_offset	= is_client ? param->blk_cli_off : param->blk_srv_off;

	return 0;
}

int
lstcon_testrpc_prep(struct lstcon_node *nd, int transop, unsigned int feats,
		    struct lstcon_test *test, struct lstcon_rpc **crpc)
{
	struct lstcon_group *sgrp = test->tes_src_grp;
	struct lstcon_group *dgrp = test->tes_dst_grp;
	struct srpc_test_reqst *trq;
	struct srpc_bulk *bulk;
	int i;
	int npg = 0;
	int nob = 0;
	int rc = 0;

	if (transop == LST_TRANS_TSBCLIADD) {
		npg = sfw_id_pages(test->tes_span);
		nob = !(feats & LST_FEAT_BULK_LEN) ?
		      npg * PAGE_SIZE :
		      sizeof(struct lnet_process_id_packed) * test->tes_span;
	}

	rc = lstcon_rpc_prep(nd, SRPC_SERVICE_TEST, feats, npg, nob, crpc);
	if (rc)
		return rc;

	trq = &(*crpc)->crp_rpc->crpc_reqstmsg.msg_body.tes_reqst;

	if (transop == LST_TRANS_TSBSRVADD) {
		int ndist = (sgrp->grp_nnode + test->tes_dist - 1) /
			    test->tes_dist;
		int nspan = (dgrp->grp_nnode + test->tes_span - 1) /
			    test->tes_span;
		int nmax = (ndist + nspan - 1) / nspan;

		trq->tsr_ndest = 0;
		trq->tsr_loop = nmax * test->tes_dist * test->tes_concur;
	} else {
		bulk = &(*crpc)->crp_rpc->crpc_bulk;

		for (i = 0; i < npg; i++) {
			int len;

			LASSERT(nob > 0);

			len = !(feats & LST_FEAT_BULK_LEN) ?
			      PAGE_SIZE :
			      min_t(int, nob, PAGE_SIZE);
			nob -= len;

			bulk->bk_iovs[i].bv_offset = 0;
			bulk->bk_iovs[i].bv_len = len;
			bulk->bk_iovs[i].bv_page = alloc_page(GFP_KERNEL);

			if (!bulk->bk_iovs[i].bv_page) {
				lstcon_rpc_put(*crpc);
				return -ENOMEM;
			}
		}

		bulk->bk_sink = 0;

		LASSERT(transop == LST_TRANS_TSBCLIADD);

		rc = lstcon_dstnodes_prep(test->tes_dst_grp,
					  test->tes_cliidx++,
					  test->tes_dist,
					  test->tes_span,
					  npg, &bulk->bk_iovs[0]);
		if (rc) {
			lstcon_rpc_put(*crpc);
			return rc;
		}

		trq->tsr_ndest = test->tes_span;
		trq->tsr_loop = test->tes_loop;
	}

	trq->tsr_sid = console_session.ses_id;
	trq->tsr_bid = test->tes_hdr.tsb_id;
	trq->tsr_concur = test->tes_concur;
	trq->tsr_is_client = (transop == LST_TRANS_TSBCLIADD) ? 1 : 0;
	trq->tsr_stop_onerr = !!test->tes_stop_onerr;

	switch (test->tes_type) {
	case LST_TEST_PING:
		trq->tsr_service = SRPC_SERVICE_PING;
		rc = lstcon_pingrpc_prep((struct lst_test_ping_param *)
					 &test->tes_param[0], trq);
		break;

	case LST_TEST_BULK:
		trq->tsr_service = SRPC_SERVICE_BRW;
		if (!(feats & LST_FEAT_BULK_LEN)) {
			rc = lstcon_bulkrpc_v0_prep((struct lst_test_bulk_param *)
						    &test->tes_param[0], trq);
		} else {
			rc = lstcon_bulkrpc_v1_prep((struct lst_test_bulk_param *)
						    &test->tes_param[0],
						    trq->tsr_is_client, trq);
		}

		break;
	default:
		LBUG();
		break;
	}

	return rc;
}

static int
lstcon_sesnew_stat_reply(struct lstcon_rpc_trans *trans,
			 struct lstcon_node *nd, struct srpc_msg *reply)
{
	struct srpc_mksn_reply *mksn_rep = &reply->msg_body.mksn_reply;
	int status = mksn_rep->mksn_status;

	if (!status &&
	    (reply->msg_ses_feats & ~LST_FEATS_MASK)) {
		mksn_rep->mksn_status = EPROTO;
		status = EPROTO;
	}

	if (status == EPROTO) {
		CNETERR("session protocol error from %s: %u\n",
			libcfs_nid2str(nd->nd_id.nid),
			reply->msg_ses_feats);
	}

	if (status)
		return status;

	if (!trans->tas_feats_updated) {
		spin_lock(&console_session.ses_rpc_lock);
		if (!trans->tas_feats_updated) {	/* recheck with lock */
			trans->tas_feats_updated = 1;
			trans->tas_features = reply->msg_ses_feats;
		}
		spin_unlock(&console_session.ses_rpc_lock);
	}

	if (reply->msg_ses_feats != trans->tas_features) {
		CNETERR("Framework features %x from %s is different with features on this transaction: %x\n",
			reply->msg_ses_feats, libcfs_nid2str(nd->nd_id.nid),
			trans->tas_features);
		mksn_rep->mksn_status = EPROTO;
		status = EPROTO;
	}

	if (!status) {
		/* session timeout on remote node */
		nd->nd_timeout = mksn_rep->mksn_timeout;
	}

	return status;
}

void
lstcon_rpc_stat_reply(struct lstcon_rpc_trans *trans, struct srpc_msg *msg,
		      struct lstcon_node *nd, struct lstcon_trans_stat *stat)
{
	struct srpc_rmsn_reply *rmsn_rep;
	struct srpc_debug_reply *dbg_rep;
	struct srpc_batch_reply *bat_rep;
	struct srpc_test_reply *test_rep;
	struct srpc_stat_reply *stat_rep;
	int rc = 0;

	switch (trans->tas_opc) {
	case LST_TRANS_SESNEW:
		rc = lstcon_sesnew_stat_reply(trans, nd, msg);
		if (!rc) {
			lstcon_sesop_stat_success(stat, 1);
			return;
		}

		lstcon_sesop_stat_failure(stat, 1);
		break;

	case LST_TRANS_SESEND:
		rmsn_rep = &msg->msg_body.rmsn_reply;
		/* ESRCH is not an error for end session */
		if (!rmsn_rep->rmsn_status ||
		    rmsn_rep->rmsn_status == ESRCH) {
			lstcon_sesop_stat_success(stat, 1);
			return;
		}

		lstcon_sesop_stat_failure(stat, 1);
		rc = rmsn_rep->rmsn_status;
		break;

	case LST_TRANS_SESQRY:
	case LST_TRANS_SESPING:
		dbg_rep = &msg->msg_body.dbg_reply;

		if (dbg_rep->dbg_status == ESRCH) {
			lstcon_sesqry_stat_unknown(stat, 1);
			return;
		}

		if (lstcon_session_match(dbg_rep->dbg_sid))
			lstcon_sesqry_stat_active(stat, 1);
		else
			lstcon_sesqry_stat_busy(stat, 1);
		return;

	case LST_TRANS_TSBRUN:
	case LST_TRANS_TSBSTOP:
		bat_rep = &msg->msg_body.bat_reply;

		if (!bat_rep->bar_status) {
			lstcon_tsbop_stat_success(stat, 1);
			return;
		}

		if (bat_rep->bar_status == EPERM &&
		    trans->tas_opc == LST_TRANS_TSBSTOP) {
			lstcon_tsbop_stat_success(stat, 1);
			return;
		}

		lstcon_tsbop_stat_failure(stat, 1);
		rc = bat_rep->bar_status;
		break;

	case LST_TRANS_TSBCLIQRY:
	case LST_TRANS_TSBSRVQRY:
		bat_rep = &msg->msg_body.bat_reply;

		if (bat_rep->bar_active)
			lstcon_tsbqry_stat_run(stat, 1);
		else
			lstcon_tsbqry_stat_idle(stat, 1);

		if (!bat_rep->bar_status)
			return;

		lstcon_tsbqry_stat_failure(stat, 1);
		rc = bat_rep->bar_status;
		break;

	case LST_TRANS_TSBCLIADD:
	case LST_TRANS_TSBSRVADD:
		test_rep = &msg->msg_body.tes_reply;

		if (!test_rep->tsr_status) {
			lstcon_tsbop_stat_success(stat, 1);
			return;
		}

		lstcon_tsbop_stat_failure(stat, 1);
		rc = test_rep->tsr_status;
		break;

	case LST_TRANS_STATQRY:
		stat_rep = &msg->msg_body.stat_reply;

		if (!stat_rep->str_status) {
			lstcon_statqry_stat_success(stat, 1);
			return;
		}

		lstcon_statqry_stat_failure(stat, 1);
		rc = stat_rep->str_status;
		break;

	default:
		LBUG();
	}

	if (!stat->trs_fwk_errno)
		stat->trs_fwk_errno = rc;
}

int
lstcon_rpc_trans_ndlist(struct list_head *ndlist,
			struct list_head *translist, int transop,
			void *arg, lstcon_rpc_cond_func_t condition,
			struct lstcon_rpc_trans **transpp)
{
	struct lstcon_rpc_trans *trans;
	struct lstcon_ndlink *ndl;
	struct lstcon_node *nd;
	struct lstcon_rpc *rpc;
	unsigned int feats;
	int rc;

	/* Creating session RPG for list of nodes */

	rc = lstcon_rpc_trans_prep(translist, transop, &trans);
	if (rc) {
		CERROR("Can't create transaction %d: %d\n", transop, rc);
		return rc;
	}

	feats = trans->tas_features;
	list_for_each_entry(ndl, ndlist, ndl_link) {
		rc = !condition ? 1 :
		     condition(transop, ndl->ndl_node, arg);

		if (!rc)
			continue;

		if (rc < 0) {
			CDEBUG(D_NET, "Condition error while creating RPC for transaction %d: %d\n",
			       transop, rc);
			break;
		}

		nd = ndl->ndl_node;

		switch (transop) {
		case LST_TRANS_SESNEW:
		case LST_TRANS_SESEND:
			rc = lstcon_sesrpc_prep(nd, transop, feats, &rpc);
			break;
		case LST_TRANS_SESQRY:
		case LST_TRANS_SESPING:
			rc = lstcon_dbgrpc_prep(nd, feats, &rpc);
			break;
		case LST_TRANS_TSBCLIADD:
		case LST_TRANS_TSBSRVADD:
			rc = lstcon_testrpc_prep(nd, transop, feats,
						 (struct lstcon_test *)arg,
						 &rpc);
			break;
		case LST_TRANS_TSBRUN:
		case LST_TRANS_TSBSTOP:
		case LST_TRANS_TSBCLIQRY:
		case LST_TRANS_TSBSRVQRY:
			rc = lstcon_batrpc_prep(nd, transop, feats,
						(struct lstcon_tsb_hdr *)arg,
						&rpc);
			break;
		case LST_TRANS_STATQRY:
			rc = lstcon_statrpc_prep(nd, feats, &rpc);
			break;
		default:
			rc = -EINVAL;
			break;
		}

		if (rc) {
			CERROR("Failed to create RPC for transaction %s: %d\n",
			       lstcon_rpc_trans_name(transop), rc);
			break;
		}

		lstcon_rpc_trans_addreq(trans, rpc);
	}

	if (!rc) {
		*transpp = trans;
		return 0;
	}

	lstcon_rpc_trans_destroy(trans);

	return rc;
}

static void
lstcon_rpc_pinger(void *arg)
{
	struct stt_timer *ptimer = (struct stt_timer *)arg;
	struct lstcon_rpc_trans *trans;
	struct lstcon_rpc *crpc;
	struct srpc_msg *rep;
	struct srpc_debug_reqst *drq;
	struct lstcon_ndlink *ndl;
	struct lstcon_node *nd;
	int intv;
	int count = 0;
	int rc;

	/*
	 * RPC pinger is a special case of transaction,
	 * it's called by timer at 8 seconds interval.
	 */
	mutex_lock(&console_session.ses_mutex);

	if (console_session.ses_shutdown || console_session.ses_expired) {
		mutex_unlock(&console_session.ses_mutex);
		return;
	}

	if (!console_session.ses_expired &&
	    ktime_get_real_seconds() - console_session.ses_laststamp >
	    (time64_t)console_session.ses_timeout)
		console_session.ses_expired = 1;

	trans = console_session.ses_ping;

	LASSERT(trans);

	list_for_each_entry(ndl, &console_session.ses_ndl_list, ndl_link) {
		nd = ndl->ndl_node;

		if (console_session.ses_expired) {
			/* idle console, end session on all nodes */
			if (nd->nd_state != LST_NODE_ACTIVE)
				continue;

			rc = lstcon_sesrpc_prep(nd, LST_TRANS_SESEND,
						trans->tas_features, &crpc);
			if (rc) {
				CERROR("Out of memory\n");
				break;
			}

			lstcon_rpc_trans_addreq(trans, crpc);
			lstcon_rpc_post(crpc);

			continue;
		}

		crpc = &nd->nd_ping;

		if (crpc->crp_rpc) {
			LASSERT(crpc->crp_trans == trans);
			LASSERT(!list_empty(&crpc->crp_link));

			spin_lock(&crpc->crp_rpc->crpc_lock);

			LASSERT(crpc->crp_posted);

			if (!crpc->crp_finished) {
				/* in flight */
				spin_unlock(&crpc->crp_rpc->crpc_lock);
				continue;
			}

			spin_unlock(&crpc->crp_rpc->crpc_lock);

			lstcon_rpc_get_reply(crpc, &rep);

			list_del_init(&crpc->crp_link);

			lstcon_rpc_put(crpc);
		}

		if (nd->nd_state != LST_NODE_ACTIVE)
			continue;

		intv = (jiffies - nd->nd_stamp) / msecs_to_jiffies(MSEC_PER_SEC);
		if (intv < nd->nd_timeout / 2)
			continue;

		rc = lstcon_rpc_init(nd, SRPC_SERVICE_DEBUG,
				     trans->tas_features, 0, 0, 1, crpc);
		if (rc) {
			CERROR("Out of memory\n");
			break;
		}

		drq = &crpc->crp_rpc->crpc_reqstmsg.msg_body.dbg_reqst;

		drq->dbg_sid = console_session.ses_id;
		drq->dbg_flags = 0;

		lstcon_rpc_trans_addreq(trans, crpc);
		lstcon_rpc_post(crpc);

		count++;
	}

	if (console_session.ses_expired) {
		mutex_unlock(&console_session.ses_mutex);
		return;
	}

	CDEBUG(D_NET, "Ping %d nodes in session\n", count);

	ptimer->stt_expires = ktime_get_real_seconds() + LST_PING_INTERVAL;
	stt_add_timer(ptimer);

	mutex_unlock(&console_session.ses_mutex);
}

int
lstcon_rpc_pinger_start(void)
{
	struct stt_timer *ptimer;
	int rc;

	LASSERT(list_empty(&console_session.ses_rpc_freelist));
	LASSERT(!atomic_read(&console_session.ses_rpc_counter));

	rc = lstcon_rpc_trans_prep(NULL, LST_TRANS_SESPING,
				   &console_session.ses_ping);
	if (rc) {
		CERROR("Failed to create console pinger\n");
		return rc;
	}

	ptimer = &console_session.ses_ping_timer;
	ptimer->stt_expires = ktime_get_real_seconds() + LST_PING_INTERVAL;

	stt_add_timer(ptimer);

	return 0;
}

void
lstcon_rpc_pinger_stop(void)
{
	LASSERT(console_session.ses_shutdown);

	stt_del_timer(&console_session.ses_ping_timer);

	lstcon_rpc_trans_abort(console_session.ses_ping, -ESHUTDOWN);
	lstcon_rpc_trans_stat(console_session.ses_ping, lstcon_trans_stat());
	lstcon_rpc_trans_destroy(console_session.ses_ping);

	memset(lstcon_trans_stat(), 0, sizeof(struct lstcon_trans_stat));

	console_session.ses_ping = NULL;
}

void
lstcon_rpc_cleanup_wait(void)
{
	struct lstcon_rpc_trans *trans;
	struct lstcon_rpc *crpc;
	struct lstcon_rpc *temp;
	struct list_head *pacer;
	struct list_head zlist;

	/* Called with hold of global mutex */

	LASSERT(console_session.ses_shutdown);

	while (!list_empty(&console_session.ses_trans_list)) {
		list_for_each(pacer, &console_session.ses_trans_list) {
			trans = list_entry(pacer, struct lstcon_rpc_trans,
					   tas_link);

			CDEBUG(D_NET, "Session closed, wakeup transaction %s\n",
			       lstcon_rpc_trans_name(trans->tas_opc));

			wake_up(&trans->tas_waitq);
		}

		mutex_unlock(&console_session.ses_mutex);

		CWARN("Session is shutting down, waiting for termination of transactions\n");
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(cfs_time_seconds(1));

		mutex_lock(&console_session.ses_mutex);
	}

	spin_lock(&console_session.ses_rpc_lock);

	lst_wait_until(!atomic_read(&console_session.ses_rpc_counter),
		       console_session.ses_rpc_lock,
		       "Network is not accessible or target is down, waiting for %d console RPCs to being recycled\n",
		       atomic_read(&console_session.ses_rpc_counter));

	list_add(&zlist, &console_session.ses_rpc_freelist);
	list_del_init(&console_session.ses_rpc_freelist);

	spin_unlock(&console_session.ses_rpc_lock);

	list_for_each_entry_safe(crpc, temp, &zlist, crp_link) {
		list_del(&crpc->crp_link);
		LIBCFS_FREE(crpc, sizeof(struct lstcon_rpc));
	}
}

int
lstcon_rpc_module_init(void)
{
	INIT_LIST_HEAD(&console_session.ses_ping_timer.stt_list);
	console_session.ses_ping_timer.stt_func = lstcon_rpc_pinger;
	console_session.ses_ping_timer.stt_data = &console_session.ses_ping_timer;

	console_session.ses_ping = NULL;

	spin_lock_init(&console_session.ses_rpc_lock);
	atomic_set(&console_session.ses_rpc_counter, 0);
	INIT_LIST_HEAD(&console_session.ses_rpc_freelist);

	return 0;
}

void
lstcon_rpc_module_fini(void)
{
	LASSERT(list_empty(&console_session.ses_rpc_freelist));
	LASSERT(!atomic_read(&console_session.ses_rpc_counter));
}
