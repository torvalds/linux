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
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lnet/selftest/conctl.c
 *
 * Test client & Server
 *
 * Author: Liang Zhen <liangzhen@clusterfs.com>
 */

#include "selftest.h"

#define LST_PING_TEST_MAGIC	0xbabeface

static int ping_srv_workitems = SFW_TEST_WI_MAX;
module_param(ping_srv_workitems, int, 0644);
MODULE_PARM_DESC(ping_srv_workitems, "# PING server workitems");

struct lst_ping_data {
	spinlock_t	pnd_lock;	/* serialize */
	int		pnd_counter;	/* sequence counter */
};

static struct lst_ping_data  lst_ping_data;

static int
ping_client_init(sfw_test_instance_t *tsi)
{
	sfw_session_t *sn = tsi->tsi_batch->bat_session;

	LASSERT(tsi->tsi_is_client);
	LASSERT(sn && !(sn->sn_features & ~LST_FEATS_MASK));

	spin_lock_init(&lst_ping_data.pnd_lock);
	lst_ping_data.pnd_counter = 0;

	return 0;
}

static void
ping_client_fini(sfw_test_instance_t *tsi)
{
	sfw_session_t *sn = tsi->tsi_batch->bat_session;
	int errors;

	LASSERT(sn);
	LASSERT(tsi->tsi_is_client);

	errors = atomic_read(&sn->sn_ping_errors);
	if (errors)
		CWARN("%d pings have failed.\n", errors);
	else
		CDEBUG(D_NET, "Ping test finished OK.\n");
}

static int
ping_client_prep_rpc(sfw_test_unit_t *tsu, lnet_process_id_t dest,
		     srpc_client_rpc_t **rpc)
{
	srpc_ping_reqst_t *req;
	sfw_test_instance_t *tsi = tsu->tsu_instance;
	sfw_session_t *sn = tsi->tsi_batch->bat_session;
	struct timespec64 ts;
	int rc;

	LASSERT(sn);
	LASSERT(!(sn->sn_features & ~LST_FEATS_MASK));

	rc = sfw_create_test_rpc(tsu, dest, sn->sn_features, 0, 0, rpc);
	if (rc)
		return rc;

	req = &(*rpc)->crpc_reqstmsg.msg_body.ping_reqst;

	req->pnr_magic = LST_PING_TEST_MAGIC;

	spin_lock(&lst_ping_data.pnd_lock);
	req->pnr_seq = lst_ping_data.pnd_counter++;
	spin_unlock(&lst_ping_data.pnd_lock);

	ktime_get_real_ts64(&ts);
	req->pnr_time_sec = ts.tv_sec;
	req->pnr_time_usec = ts.tv_nsec / NSEC_PER_USEC;

	return rc;
}

static void
ping_client_done_rpc(sfw_test_unit_t *tsu, srpc_client_rpc_t *rpc)
{
	sfw_test_instance_t *tsi = tsu->tsu_instance;
	sfw_session_t *sn = tsi->tsi_batch->bat_session;
	srpc_ping_reqst_t *reqst = &rpc->crpc_reqstmsg.msg_body.ping_reqst;
	srpc_ping_reply_t *reply = &rpc->crpc_replymsg.msg_body.ping_reply;
	struct timespec64 ts;

	LASSERT(sn);

	if (rpc->crpc_status) {
		if (!tsi->tsi_stopping)	/* rpc could have been aborted */
			atomic_inc(&sn->sn_ping_errors);
		CERROR("Unable to ping %s (%d): %d\n",
		       libcfs_id2str(rpc->crpc_dest),
		       reqst->pnr_seq, rpc->crpc_status);
		return;
	}

	if (rpc->crpc_replymsg.msg_magic != SRPC_MSG_MAGIC) {
		__swab32s(&reply->pnr_seq);
		__swab32s(&reply->pnr_magic);
		__swab32s(&reply->pnr_status);
	}

	if (reply->pnr_magic != LST_PING_TEST_MAGIC) {
		rpc->crpc_status = -EBADMSG;
		atomic_inc(&sn->sn_ping_errors);
		CERROR("Bad magic %u from %s, %u expected.\n",
		       reply->pnr_magic, libcfs_id2str(rpc->crpc_dest),
		       LST_PING_TEST_MAGIC);
		return;
	}

	if (reply->pnr_seq != reqst->pnr_seq) {
		rpc->crpc_status = -EBADMSG;
		atomic_inc(&sn->sn_ping_errors);
		CERROR("Bad seq %u from %s, %u expected.\n",
		       reply->pnr_seq, libcfs_id2str(rpc->crpc_dest),
		       reqst->pnr_seq);
		return;
	}

	ktime_get_real_ts64(&ts);
	CDEBUG(D_NET, "%d reply in %u usec\n", reply->pnr_seq,
	       (unsigned)((ts.tv_sec - reqst->pnr_time_sec) * 1000000 +
			  (ts.tv_nsec / NSEC_PER_USEC - reqst->pnr_time_usec)));
}

static int
ping_server_handle(struct srpc_server_rpc *rpc)
{
	struct srpc_service *sv = rpc->srpc_scd->scd_svc;
	srpc_msg_t *reqstmsg = &rpc->srpc_reqstbuf->buf_msg;
	srpc_msg_t *replymsg = &rpc->srpc_replymsg;
	srpc_ping_reqst_t *req = &reqstmsg->msg_body.ping_reqst;
	srpc_ping_reply_t *rep = &rpc->srpc_replymsg.msg_body.ping_reply;

	LASSERT(sv->sv_id == SRPC_SERVICE_PING);

	if (reqstmsg->msg_magic != SRPC_MSG_MAGIC) {
		LASSERT(reqstmsg->msg_magic == __swab32(SRPC_MSG_MAGIC));

		__swab32s(&req->pnr_seq);
		__swab32s(&req->pnr_magic);
		__swab64s(&req->pnr_time_sec);
		__swab64s(&req->pnr_time_usec);
	}
	LASSERT(reqstmsg->msg_type == srpc_service2request(sv->sv_id));

	if (req->pnr_magic != LST_PING_TEST_MAGIC) {
		CERROR("Unexpected magic %08x from %s\n",
		       req->pnr_magic, libcfs_id2str(rpc->srpc_peer));
		return -EINVAL;
	}

	rep->pnr_seq = req->pnr_seq;
	rep->pnr_magic = LST_PING_TEST_MAGIC;

	if (reqstmsg->msg_ses_feats & ~LST_FEATS_MASK) {
		replymsg->msg_ses_feats = LST_FEATS_MASK;
		rep->pnr_status = EPROTO;
		return 0;
	}

	replymsg->msg_ses_feats = reqstmsg->msg_ses_feats;

	CDEBUG(D_NET, "Get ping %d from %s\n",
	       req->pnr_seq, libcfs_id2str(rpc->srpc_peer));
	return 0;
}

sfw_test_client_ops_t ping_test_client;
void ping_init_test_client(void)
{
	ping_test_client.tso_init = ping_client_init;
	ping_test_client.tso_fini = ping_client_fini;
	ping_test_client.tso_prep_rpc = ping_client_prep_rpc;
	ping_test_client.tso_done_rpc = ping_client_done_rpc;
}

srpc_service_t ping_test_service;
void ping_init_test_service(void)
{
	ping_test_service.sv_id = SRPC_SERVICE_PING;
	ping_test_service.sv_name = "ping_test";
	ping_test_service.sv_handler = ping_server_handle;
	ping_test_service.sv_wi_total = ping_srv_workitems;
}
