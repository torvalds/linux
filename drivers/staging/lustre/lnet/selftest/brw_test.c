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
 * Copyright (c) 2011, 2015, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lnet/selftest/brw_test.c
 *
 * Author: Isaac Huang <isaac@clusterfs.com>
 */

#include "selftest.h"

static int brw_srv_workitems = SFW_TEST_WI_MAX;
module_param(brw_srv_workitems, int, 0644);
MODULE_PARM_DESC(brw_srv_workitems, "# BRW server workitems");

static int brw_inject_errors;
module_param(brw_inject_errors, int, 0644);
MODULE_PARM_DESC(brw_inject_errors, "# data errors to inject randomly, zero by default");

static void
brw_client_fini(struct sfw_test_instance *tsi)
{
	struct srpc_bulk *bulk;
	struct sfw_test_unit	*tsu;

	LASSERT(tsi->tsi_is_client);

	list_for_each_entry(tsu, &tsi->tsi_units, tsu_list) {
		bulk = tsu->tsu_private;
		if (!bulk)
			continue;

		srpc_free_bulk(bulk);
		tsu->tsu_private = NULL;
	}
}

static int
brw_client_init(struct sfw_test_instance *tsi)
{
	struct sfw_session *sn = tsi->tsi_batch->bat_session;
	int flags;
	int npg;
	int len;
	int opc;
	struct srpc_bulk *bulk;
	struct sfw_test_unit *tsu;

	LASSERT(sn);
	LASSERT(tsi->tsi_is_client);

	if (!(sn->sn_features & LST_FEAT_BULK_LEN)) {
		struct test_bulk_req *breq = &tsi->tsi_u.bulk_v0;

		opc = breq->blk_opc;
		flags = breq->blk_flags;
		npg = breq->blk_npg;
		/*
		 * NB: this is not going to work for variable page size,
		 * but we have to keep it for compatibility
		 */
		len = npg * PAGE_SIZE;
	} else {
		struct test_bulk_req_v1 *breq = &tsi->tsi_u.bulk_v1;

		/*
		 * I should never get this step if it's unknown feature
		 * because make_session will reject unknown feature
		 */
		LASSERT(!(sn->sn_features & ~LST_FEATS_MASK));

		opc = breq->blk_opc;
		flags = breq->blk_flags;
		len = breq->blk_len;
		npg = (len + PAGE_SIZE - 1) >> PAGE_SHIFT;
	}

	if (npg > LNET_MAX_IOV || npg <= 0)
		return -EINVAL;

	if (opc != LST_BRW_READ && opc != LST_BRW_WRITE)
		return -EINVAL;

	if (flags != LST_BRW_CHECK_NONE &&
	    flags != LST_BRW_CHECK_FULL && flags != LST_BRW_CHECK_SIMPLE)
		return -EINVAL;

	list_for_each_entry(tsu, &tsi->tsi_units, tsu_list) {
		bulk = srpc_alloc_bulk(lnet_cpt_of_nid(tsu->tsu_dest.nid),
				       npg, len, opc == LST_BRW_READ);
		if (!bulk) {
			brw_client_fini(tsi);
			return -ENOMEM;
		}

		tsu->tsu_private = bulk;
	}

	return 0;
}

#define BRW_POISON	0xbeefbeefbeefbeefULL
#define BRW_MAGIC	0xeeb0eeb1eeb2eeb3ULL
#define BRW_MSIZE	sizeof(__u64)

static int
brw_inject_one_error(void)
{
	struct timespec64 ts;

	if (brw_inject_errors <= 0)
		return 0;

	ktime_get_ts64(&ts);

	if (!((ts.tv_nsec / NSEC_PER_USEC) & 1))
		return 0;

	return brw_inject_errors--;
}

static void
brw_fill_page(struct page *pg, int pattern, __u64 magic)
{
	char *addr = page_address(pg);
	int i;

	LASSERT(addr);

	if (pattern == LST_BRW_CHECK_NONE)
		return;

	if (magic == BRW_MAGIC)
		magic += brw_inject_one_error();

	if (pattern == LST_BRW_CHECK_SIMPLE) {
		memcpy(addr, &magic, BRW_MSIZE);
		addr += PAGE_SIZE - BRW_MSIZE;
		memcpy(addr, &magic, BRW_MSIZE);
		return;
	}

	if (pattern == LST_BRW_CHECK_FULL) {
		for (i = 0; i < PAGE_SIZE / BRW_MSIZE; i++)
			memcpy(addr + i * BRW_MSIZE, &magic, BRW_MSIZE);
		return;
	}

	LBUG();
}

static int
brw_check_page(struct page *pg, int pattern, __u64 magic)
{
	char *addr = page_address(pg);
	__u64 data = 0; /* make compiler happy */
	int i;

	LASSERT(addr);

	if (pattern == LST_BRW_CHECK_NONE)
		return 0;

	if (pattern == LST_BRW_CHECK_SIMPLE) {
		data = *((__u64 *)addr);
		if (data != magic)
			goto bad_data;

		addr += PAGE_SIZE - BRW_MSIZE;
		data = *((__u64 *)addr);
		if (data != magic)
			goto bad_data;

		return 0;
	}

	if (pattern == LST_BRW_CHECK_FULL) {
		for (i = 0; i < PAGE_SIZE / BRW_MSIZE; i++) {
			data = *(((__u64 *)addr) + i);
			if (data != magic)
				goto bad_data;
		}

		return 0;
	}

	LBUG();

bad_data:
	CERROR("Bad data in page %p: %#llx, %#llx expected\n",
	       pg, data, magic);
	return 1;
}

static void
brw_fill_bulk(struct srpc_bulk *bk, int pattern, __u64 magic)
{
	int i;
	struct page *pg;

	for (i = 0; i < bk->bk_niov; i++) {
		pg = bk->bk_iovs[i].bv_page;
		brw_fill_page(pg, pattern, magic);
	}
}

static int
brw_check_bulk(struct srpc_bulk *bk, int pattern, __u64 magic)
{
	int i;
	struct page *pg;

	for (i = 0; i < bk->bk_niov; i++) {
		pg = bk->bk_iovs[i].bv_page;
		if (brw_check_page(pg, pattern, magic)) {
			CERROR("Bulk page %p (%d/%d) is corrupted!\n",
			       pg, i, bk->bk_niov);
			return 1;
		}
	}

	return 0;
}

static int
brw_client_prep_rpc(struct sfw_test_unit *tsu,
		    lnet_process_id_t dest, struct srpc_client_rpc **rpcpp)
{
	struct srpc_bulk *bulk = tsu->tsu_private;
	struct sfw_test_instance *tsi = tsu->tsu_instance;
	struct sfw_session *sn = tsi->tsi_batch->bat_session;
	struct srpc_client_rpc *rpc;
	struct srpc_brw_reqst *req;
	int flags;
	int npg;
	int len;
	int opc;
	int rc;

	LASSERT(sn);
	LASSERT(bulk);

	if (!(sn->sn_features & LST_FEAT_BULK_LEN)) {
		struct test_bulk_req *breq = &tsi->tsi_u.bulk_v0;

		opc = breq->blk_opc;
		flags = breq->blk_flags;
		npg = breq->blk_npg;
		len = npg * PAGE_SIZE;
	} else {
		struct test_bulk_req_v1 *breq = &tsi->tsi_u.bulk_v1;

		/*
		 * I should never get this step if it's unknown feature
		 * because make_session will reject unknown feature
		 */
		LASSERT(!(sn->sn_features & ~LST_FEATS_MASK));

		opc = breq->blk_opc;
		flags = breq->blk_flags;
		len = breq->blk_len;
		npg = (len + PAGE_SIZE - 1) >> PAGE_SHIFT;
	}

	rc = sfw_create_test_rpc(tsu, dest, sn->sn_features, npg, len, &rpc);
	if (rc)
		return rc;

	memcpy(&rpc->crpc_bulk, bulk, offsetof(struct srpc_bulk, bk_iovs[npg]));
	if (opc == LST_BRW_WRITE)
		brw_fill_bulk(&rpc->crpc_bulk, flags, BRW_MAGIC);
	else
		brw_fill_bulk(&rpc->crpc_bulk, flags, BRW_POISON);

	req = &rpc->crpc_reqstmsg.msg_body.brw_reqst;
	req->brw_flags = flags;
	req->brw_rw = opc;
	req->brw_len = len;

	*rpcpp = rpc;
	return 0;
}

static void
brw_client_done_rpc(struct sfw_test_unit *tsu, struct srpc_client_rpc *rpc)
{
	__u64 magic = BRW_MAGIC;
	struct sfw_test_instance *tsi = tsu->tsu_instance;
	struct sfw_session *sn = tsi->tsi_batch->bat_session;
	struct srpc_msg *msg = &rpc->crpc_replymsg;
	struct srpc_brw_reply *reply = &msg->msg_body.brw_reply;
	struct srpc_brw_reqst *reqst = &rpc->crpc_reqstmsg.msg_body.brw_reqst;

	LASSERT(sn);

	if (rpc->crpc_status) {
		CERROR("BRW RPC to %s failed with %d\n",
		       libcfs_id2str(rpc->crpc_dest), rpc->crpc_status);
		if (!tsi->tsi_stopping)	/* rpc could have been aborted */
			atomic_inc(&sn->sn_brw_errors);
		return;
	}

	if (msg->msg_magic != SRPC_MSG_MAGIC) {
		__swab64s(&magic);
		__swab32s(&reply->brw_status);
	}

	CDEBUG(reply->brw_status ? D_WARNING : D_NET,
	       "BRW RPC to %s finished with brw_status: %d\n",
	       libcfs_id2str(rpc->crpc_dest), reply->brw_status);

	if (reply->brw_status) {
		atomic_inc(&sn->sn_brw_errors);
		rpc->crpc_status = -(int)reply->brw_status;
		return;
	}

	if (reqst->brw_rw == LST_BRW_WRITE)
		return;

	if (brw_check_bulk(&rpc->crpc_bulk, reqst->brw_flags, magic)) {
		CERROR("Bulk data from %s is corrupted!\n",
		       libcfs_id2str(rpc->crpc_dest));
		atomic_inc(&sn->sn_brw_errors);
		rpc->crpc_status = -EBADMSG;
	}
}

static void
brw_server_rpc_done(struct srpc_server_rpc *rpc)
{
	struct srpc_bulk *blk = rpc->srpc_bulk;

	if (!blk)
		return;

	if (rpc->srpc_status)
		CERROR("Bulk transfer %s %s has failed: %d\n",
		       blk->bk_sink ? "from" : "to",
		       libcfs_id2str(rpc->srpc_peer), rpc->srpc_status);
	else
		CDEBUG(D_NET, "Transferred %d pages bulk data %s %s\n",
		       blk->bk_niov, blk->bk_sink ? "from" : "to",
		       libcfs_id2str(rpc->srpc_peer));

	sfw_free_pages(rpc);
}

static int
brw_bulk_ready(struct srpc_server_rpc *rpc, int status)
{
	__u64 magic = BRW_MAGIC;
	struct srpc_brw_reply *reply = &rpc->srpc_replymsg.msg_body.brw_reply;
	struct srpc_brw_reqst *reqst;
	struct srpc_msg *reqstmsg;

	LASSERT(rpc->srpc_bulk);
	LASSERT(rpc->srpc_reqstbuf);

	reqstmsg = &rpc->srpc_reqstbuf->buf_msg;
	reqst = &reqstmsg->msg_body.brw_reqst;

	if (status) {
		CERROR("BRW bulk %s failed for RPC from %s: %d\n",
		       reqst->brw_rw == LST_BRW_READ ? "READ" : "WRITE",
		       libcfs_id2str(rpc->srpc_peer), status);
		return -EIO;
	}

	if (reqst->brw_rw == LST_BRW_READ)
		return 0;

	if (reqstmsg->msg_magic != SRPC_MSG_MAGIC)
		__swab64s(&magic);

	if (brw_check_bulk(rpc->srpc_bulk, reqst->brw_flags, magic)) {
		CERROR("Bulk data from %s is corrupted!\n",
		       libcfs_id2str(rpc->srpc_peer));
		reply->brw_status = EBADMSG;
	}

	return 0;
}

static int
brw_server_handle(struct srpc_server_rpc *rpc)
{
	struct srpc_service *sv = rpc->srpc_scd->scd_svc;
	struct srpc_msg *replymsg = &rpc->srpc_replymsg;
	struct srpc_msg *reqstmsg = &rpc->srpc_reqstbuf->buf_msg;
	struct srpc_brw_reply *reply = &replymsg->msg_body.brw_reply;
	struct srpc_brw_reqst *reqst = &reqstmsg->msg_body.brw_reqst;
	int npg;
	int rc;

	LASSERT(sv->sv_id == SRPC_SERVICE_BRW);

	if (reqstmsg->msg_magic != SRPC_MSG_MAGIC) {
		LASSERT(reqstmsg->msg_magic == __swab32(SRPC_MSG_MAGIC));

		__swab32s(&reqst->brw_rw);
		__swab32s(&reqst->brw_len);
		__swab32s(&reqst->brw_flags);
		__swab64s(&reqst->brw_rpyid);
		__swab64s(&reqst->brw_bulkid);
	}
	LASSERT(reqstmsg->msg_type == (__u32)srpc_service2request(sv->sv_id));

	reply->brw_status = 0;
	rpc->srpc_done = brw_server_rpc_done;

	if ((reqst->brw_rw != LST_BRW_READ && reqst->brw_rw != LST_BRW_WRITE) ||
	    (reqst->brw_flags != LST_BRW_CHECK_NONE &&
	     reqst->brw_flags != LST_BRW_CHECK_FULL &&
	     reqst->brw_flags != LST_BRW_CHECK_SIMPLE)) {
		reply->brw_status = EINVAL;
		return 0;
	}

	if (reqstmsg->msg_ses_feats & ~LST_FEATS_MASK) {
		replymsg->msg_ses_feats = LST_FEATS_MASK;
		reply->brw_status = EPROTO;
		return 0;
	}

	if (!(reqstmsg->msg_ses_feats & LST_FEAT_BULK_LEN)) {
		/* compat with old version */
		if (reqst->brw_len & ~PAGE_MASK) {
			reply->brw_status = EINVAL;
			return 0;
		}
		npg = reqst->brw_len >> PAGE_SHIFT;

	} else {
		npg = (reqst->brw_len + PAGE_SIZE - 1) >> PAGE_SHIFT;
	}

	replymsg->msg_ses_feats = reqstmsg->msg_ses_feats;

	if (!reqst->brw_len || npg > LNET_MAX_IOV) {
		reply->brw_status = EINVAL;
		return 0;
	}

	rc = sfw_alloc_pages(rpc, rpc->srpc_scd->scd_cpt, npg,
			     reqst->brw_len,
			     reqst->brw_rw == LST_BRW_WRITE);
	if (rc)
		return rc;

	if (reqst->brw_rw == LST_BRW_READ)
		brw_fill_bulk(rpc->srpc_bulk, reqst->brw_flags, BRW_MAGIC);
	else
		brw_fill_bulk(rpc->srpc_bulk, reqst->brw_flags, BRW_POISON);

	return 0;
}

struct sfw_test_client_ops brw_test_client;

void brw_init_test_client(void)
{
	brw_test_client.tso_init = brw_client_init;
	brw_test_client.tso_fini = brw_client_fini;
	brw_test_client.tso_prep_rpc = brw_client_prep_rpc;
	brw_test_client.tso_done_rpc = brw_client_done_rpc;
};

struct srpc_service brw_test_service;

void brw_init_test_service(void)
{
	brw_test_service.sv_id = SRPC_SERVICE_BRW;
	brw_test_service.sv_name = "brw_test";
	brw_test_service.sv_handler = brw_server_handle;
	brw_test_service.sv_bulk_ready = brw_bulk_ready;
	brw_test_service.sv_wi_total = brw_srv_workitems;
}
