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
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2010, 2015, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

/**
 * This file deals with various client/target related logic including recovery.
 *
 * TODO: This code more logically belongs in the ptlrpc module than in ldlm and
 * should be moved.
 */

#define DEBUG_SUBSYSTEM S_LDLM

#include "../../include/linux/libcfs/libcfs.h"
#include "../include/obd.h"
#include "../include/obd_class.h"
#include "../include/lustre_dlm.h"
#include "../include/lustre_net.h"
#include "../include/lustre_sec.h"
#include "ldlm_internal.h"

/* @priority: If non-zero, move the selected connection to the list head.
 * @create: If zero, only search in existing connections.
 */
static int import_set_conn(struct obd_import *imp, struct obd_uuid *uuid,
			   int priority, int create)
{
	struct ptlrpc_connection *ptlrpc_conn;
	struct obd_import_conn *imp_conn = NULL, *item;
	int rc = 0;

	if (!create && !priority) {
		CDEBUG(D_HA, "Nothing to do\n");
		return -EINVAL;
	}

	ptlrpc_conn = ptlrpc_uuid_to_connection(uuid);
	if (!ptlrpc_conn) {
		CDEBUG(D_HA, "can't find connection %s\n", uuid->uuid);
		return -ENOENT;
	}

	if (create) {
		imp_conn = kzalloc(sizeof(*imp_conn), GFP_NOFS);
		if (!imp_conn) {
			rc = -ENOMEM;
			goto out_put;
		}
	}

	spin_lock(&imp->imp_lock);
	list_for_each_entry(item, &imp->imp_conn_list, oic_item) {
		if (obd_uuid_equals(uuid, &item->oic_uuid)) {
			if (priority) {
				list_del(&item->oic_item);
				list_add(&item->oic_item,
					 &imp->imp_conn_list);
				item->oic_last_attempt = 0;
			}
			CDEBUG(D_HA, "imp %p@%s: found existing conn %s%s\n",
			       imp, imp->imp_obd->obd_name, uuid->uuid,
			       (priority ? ", moved to head" : ""));
			spin_unlock(&imp->imp_lock);
			rc = 0;
			goto out_free;
		}
	}
	/* No existing import connection found for \a uuid. */
	if (create) {
		imp_conn->oic_conn = ptlrpc_conn;
		imp_conn->oic_uuid = *uuid;
		imp_conn->oic_last_attempt = 0;
		if (priority)
			list_add(&imp_conn->oic_item, &imp->imp_conn_list);
		else
			list_add_tail(&imp_conn->oic_item,
				      &imp->imp_conn_list);
		CDEBUG(D_HA, "imp %p@%s: add connection %s at %s\n",
		       imp, imp->imp_obd->obd_name, uuid->uuid,
		       (priority ? "head" : "tail"));
	} else {
		spin_unlock(&imp->imp_lock);
		rc = -ENOENT;
		goto out_free;
	}

	spin_unlock(&imp->imp_lock);
	return 0;
out_free:
	kfree(imp_conn);
out_put:
	ptlrpc_connection_put(ptlrpc_conn);
	return rc;
}

int import_set_conn_priority(struct obd_import *imp, struct obd_uuid *uuid)
{
	return import_set_conn(imp, uuid, 1, 0);
}

int client_import_add_conn(struct obd_import *imp, struct obd_uuid *uuid,
			   int priority)
{
	return import_set_conn(imp, uuid, priority, 1);
}
EXPORT_SYMBOL(client_import_add_conn);

int client_import_del_conn(struct obd_import *imp, struct obd_uuid *uuid)
{
	struct obd_import_conn *imp_conn;
	struct obd_export *dlmexp;
	int rc = -ENOENT;

	spin_lock(&imp->imp_lock);
	if (list_empty(&imp->imp_conn_list)) {
		LASSERT(!imp->imp_connection);
		goto out;
	}

	list_for_each_entry(imp_conn, &imp->imp_conn_list, oic_item) {
		if (!obd_uuid_equals(uuid, &imp_conn->oic_uuid))
			continue;
		LASSERT(imp_conn->oic_conn);

		if (imp_conn == imp->imp_conn_current) {
			LASSERT(imp_conn->oic_conn == imp->imp_connection);

			if (imp->imp_state != LUSTRE_IMP_CLOSED &&
			    imp->imp_state != LUSTRE_IMP_DISCON) {
				CERROR("can't remove current connection\n");
				rc = -EBUSY;
				goto out;
			}

			ptlrpc_connection_put(imp->imp_connection);
			imp->imp_connection = NULL;

			dlmexp = class_conn2export(&imp->imp_dlm_handle);
			if (dlmexp && dlmexp->exp_connection) {
				LASSERT(dlmexp->exp_connection ==
					imp_conn->oic_conn);
				ptlrpc_connection_put(dlmexp->exp_connection);
				dlmexp->exp_connection = NULL;
			}
		}

		list_del(&imp_conn->oic_item);
		ptlrpc_connection_put(imp_conn->oic_conn);
		kfree(imp_conn);
		CDEBUG(D_HA, "imp %p@%s: remove connection %s\n",
		       imp, imp->imp_obd->obd_name, uuid->uuid);
		rc = 0;
		break;
	}
out:
	spin_unlock(&imp->imp_lock);
	if (rc == -ENOENT)
		CERROR("connection %s not found\n", uuid->uuid);
	return rc;
}
EXPORT_SYMBOL(client_import_del_conn);

/**
 * Find conn UUID by peer NID. \a peer is a server NID. This function is used
 * to find a conn uuid of \a imp which can reach \a peer.
 */
int client_import_find_conn(struct obd_import *imp, lnet_nid_t peer,
			    struct obd_uuid *uuid)
{
	struct obd_import_conn *conn;
	int rc = -ENOENT;

	spin_lock(&imp->imp_lock);
	list_for_each_entry(conn, &imp->imp_conn_list, oic_item) {
		/* Check if conn UUID does have this peer NID. */
		if (class_check_uuid(&conn->oic_uuid, peer)) {
			*uuid = conn->oic_uuid;
			rc = 0;
			break;
		}
	}
	spin_unlock(&imp->imp_lock);
	return rc;
}
EXPORT_SYMBOL(client_import_find_conn);

void client_destroy_import(struct obd_import *imp)
{
	/* Drop security policy instance after all RPCs have finished/aborted
	 * to let all busy contexts be released.
	 */
	class_import_get(imp);
	class_destroy_import(imp);
	sptlrpc_import_sec_put(imp);
	class_import_put(imp);
}
EXPORT_SYMBOL(client_destroy_import);

/* Configure an RPC client OBD device.
 *
 * lcfg parameters:
 * 1 - client UUID
 * 2 - server UUID
 * 3 - inactive-on-startup
 */
int client_obd_setup(struct obd_device *obddev, struct lustre_cfg *lcfg)
{
	struct client_obd *cli = &obddev->u.cli;
	struct obd_import *imp;
	struct obd_uuid server_uuid;
	int rq_portal, rp_portal, connect_op;
	char *name = obddev->obd_type->typ_name;
	enum ldlm_ns_type ns_type = LDLM_NS_TYPE_UNKNOWN;
	int rc;

	/* In a more perfect world, we would hang a ptlrpc_client off of
	 * obd_type and just use the values from there.
	 */
	if (!strcmp(name, LUSTRE_OSC_NAME)) {
		rq_portal = OST_REQUEST_PORTAL;
		rp_portal = OSC_REPLY_PORTAL;
		connect_op = OST_CONNECT;
		cli->cl_sp_me = LUSTRE_SP_CLI;
		cli->cl_sp_to = LUSTRE_SP_OST;
		ns_type = LDLM_NS_TYPE_OSC;
	} else if (!strcmp(name, LUSTRE_MDC_NAME) ||
		   !strcmp(name, LUSTRE_LWP_NAME)) {
		rq_portal = MDS_REQUEST_PORTAL;
		rp_portal = MDC_REPLY_PORTAL;
		connect_op = MDS_CONNECT;
		cli->cl_sp_me = LUSTRE_SP_CLI;
		cli->cl_sp_to = LUSTRE_SP_MDT;
		ns_type = LDLM_NS_TYPE_MDC;
	} else if (!strcmp(name, LUSTRE_MGC_NAME)) {
		rq_portal = MGS_REQUEST_PORTAL;
		rp_portal = MGC_REPLY_PORTAL;
		connect_op = MGS_CONNECT;
		cli->cl_sp_me = LUSTRE_SP_MGC;
		cli->cl_sp_to = LUSTRE_SP_MGS;
		cli->cl_flvr_mgc.sf_rpc = SPTLRPC_FLVR_INVALID;
		ns_type = LDLM_NS_TYPE_MGC;
	} else {
		CERROR("unknown client OBD type \"%s\", can't setup\n",
		       name);
		return -EINVAL;
	}

	if (LUSTRE_CFG_BUFLEN(lcfg, 1) < 1) {
		CERROR("requires a TARGET UUID\n");
		return -EINVAL;
	}

	if (LUSTRE_CFG_BUFLEN(lcfg, 1) > 37) {
		CERROR("client UUID must be less than 38 characters\n");
		return -EINVAL;
	}

	if (LUSTRE_CFG_BUFLEN(lcfg, 2) < 1) {
		CERROR("setup requires a SERVER UUID\n");
		return -EINVAL;
	}

	if (LUSTRE_CFG_BUFLEN(lcfg, 2) > 37) {
		CERROR("target UUID must be less than 38 characters\n");
		return -EINVAL;
	}

	init_rwsem(&cli->cl_sem);
	cli->cl_conn_count = 0;
	memcpy(server_uuid.uuid, lustre_cfg_buf(lcfg, 2),
	       min_t(unsigned int, LUSTRE_CFG_BUFLEN(lcfg, 2),
		     sizeof(server_uuid)));

	cli->cl_dirty_pages = 0;
	cli->cl_avail_grant = 0;
	/* FIXME: Should limit this for the sum of all cl_dirty_max_pages. */
	/*
	 * cl_dirty_max_pages may be changed at connect time in
	 * ptlrpc_connect_interpret().
	 */
	client_adjust_max_dirty(cli);
	INIT_LIST_HEAD(&cli->cl_cache_waiters);
	INIT_LIST_HEAD(&cli->cl_loi_ready_list);
	INIT_LIST_HEAD(&cli->cl_loi_hp_ready_list);
	INIT_LIST_HEAD(&cli->cl_loi_write_list);
	INIT_LIST_HEAD(&cli->cl_loi_read_list);
	spin_lock_init(&cli->cl_loi_list_lock);
	atomic_set(&cli->cl_pending_w_pages, 0);
	atomic_set(&cli->cl_pending_r_pages, 0);
	cli->cl_r_in_flight = 0;
	cli->cl_w_in_flight = 0;

	spin_lock_init(&cli->cl_read_rpc_hist.oh_lock);
	spin_lock_init(&cli->cl_write_rpc_hist.oh_lock);
	spin_lock_init(&cli->cl_read_page_hist.oh_lock);
	spin_lock_init(&cli->cl_write_page_hist.oh_lock);
	spin_lock_init(&cli->cl_read_offset_hist.oh_lock);
	spin_lock_init(&cli->cl_write_offset_hist.oh_lock);

	/* lru for osc. */
	INIT_LIST_HEAD(&cli->cl_lru_osc);
	atomic_set(&cli->cl_lru_shrinkers, 0);
	atomic_set(&cli->cl_lru_busy, 0);
	atomic_set(&cli->cl_lru_in_list, 0);
	INIT_LIST_HEAD(&cli->cl_lru_list);
	spin_lock_init(&cli->cl_lru_list_lock);
	atomic_set(&cli->cl_unstable_count, 0);

	init_waitqueue_head(&cli->cl_destroy_waitq);
	atomic_set(&cli->cl_destroy_in_flight, 0);
	/* Turn on checksumming by default. */
	cli->cl_checksum = 1;
	/*
	 * The supported checksum types will be worked out at connect time
	 * Set cl_chksum* to CRC32 for now to avoid returning screwed info
	 * through procfs.
	 */
	cli->cl_cksum_type = OBD_CKSUM_CRC32;
	cli->cl_supp_cksum_types = OBD_CKSUM_CRC32;
	atomic_set(&cli->cl_resends, OSC_DEFAULT_RESENDS);

	/* This value may be reduced at connect time in
	 * ptlrpc_connect_interpret() . We initialize it to only
	 * 1MB until we know what the performance looks like.
	 * In the future this should likely be increased. LU-1431
	 */
	cli->cl_max_pages_per_rpc = min_t(int, PTLRPC_MAX_BRW_PAGES,
					  LNET_MTU >> PAGE_SHIFT);

	/*
	 * set cl_chunkbits default value to PAGE_CACHE_SHIFT,
	 * it will be updated at OSC connection time.
	 */
	cli->cl_chunkbits = PAGE_SHIFT;

	if (!strcmp(name, LUSTRE_MDC_NAME)) {
		cli->cl_max_rpcs_in_flight = OBD_MAX_RIF_DEFAULT;
	} else if (totalram_pages >> (20 - PAGE_SHIFT) <= 128 /* MB */) {
		cli->cl_max_rpcs_in_flight = 2;
	} else if (totalram_pages >> (20 - PAGE_SHIFT) <= 256 /* MB */) {
		cli->cl_max_rpcs_in_flight = 3;
	} else if (totalram_pages >> (20 - PAGE_SHIFT) <= 512 /* MB */) {
		cli->cl_max_rpcs_in_flight = 4;
	} else {
		cli->cl_max_rpcs_in_flight = OBD_MAX_RIF_DEFAULT;
	}
	rc = ldlm_get_ref();
	if (rc) {
		CERROR("ldlm_get_ref failed: %d\n", rc);
		goto err;
	}

	ptlrpc_init_client(rq_portal, rp_portal, name,
			   &obddev->obd_ldlm_client);

	imp = class_new_import(obddev);
	if (!imp) {
		rc = -ENOENT;
		goto err_ldlm;
	}
	imp->imp_client = &obddev->obd_ldlm_client;
	imp->imp_connect_op = connect_op;
	memcpy(cli->cl_target_uuid.uuid, lustre_cfg_buf(lcfg, 1),
	       LUSTRE_CFG_BUFLEN(lcfg, 1));
	class_import_put(imp);

	rc = client_import_add_conn(imp, &server_uuid, 1);
	if (rc) {
		CERROR("can't add initial connection\n");
		goto err_import;
	}

	cli->cl_import = imp;
	/* cli->cl_max_mds_{easize,cookiesize} updated by mdc_init_ea_size() */
	cli->cl_max_mds_easize = sizeof(struct lov_mds_md_v3);
	cli->cl_max_mds_cookiesize = sizeof(struct llog_cookie);

	if (LUSTRE_CFG_BUFLEN(lcfg, 3) > 0) {
		if (!strcmp(lustre_cfg_string(lcfg, 3), "inactive")) {
			CDEBUG(D_HA, "marking %s %s->%s as inactive\n",
			       name, obddev->obd_name,
			       cli->cl_target_uuid.uuid);
			spin_lock(&imp->imp_lock);
			imp->imp_deactive = 1;
			spin_unlock(&imp->imp_lock);
		}
	}

	obddev->obd_namespace = ldlm_namespace_new(obddev, obddev->obd_name,
						   LDLM_NAMESPACE_CLIENT,
						   LDLM_NAMESPACE_GREEDY,
						   ns_type);
	if (!obddev->obd_namespace) {
		CERROR("Unable to create client namespace - %s\n",
		       obddev->obd_name);
		rc = -ENOMEM;
		goto err_import;
	}

	cli->cl_qchk_stat = CL_NOT_QUOTACHECKED;

	return rc;

err_import:
	class_destroy_import(imp);
err_ldlm:
	ldlm_put_ref();
err:
	return rc;
}
EXPORT_SYMBOL(client_obd_setup);

int client_obd_cleanup(struct obd_device *obddev)
{
	ldlm_namespace_free_post(obddev->obd_namespace);
	obddev->obd_namespace = NULL;

	obd_cleanup_client_import(obddev);
	LASSERT(!obddev->u.cli.cl_import);

	ldlm_put_ref();
	return 0;
}
EXPORT_SYMBOL(client_obd_cleanup);

/* ->o_connect() method for client side (OSC and MDC and MGC) */
int client_connect_import(const struct lu_env *env,
			  struct obd_export **exp,
			  struct obd_device *obd, struct obd_uuid *cluuid,
			  struct obd_connect_data *data, void *localdata)
{
	struct client_obd       *cli    = &obd->u.cli;
	struct obd_import       *imp    = cli->cl_import;
	struct obd_connect_data *ocd;
	struct lustre_handle    conn    = { 0 };
	int		     rc;

	*exp = NULL;
	down_write(&cli->cl_sem);
	if (cli->cl_conn_count > 0) {
		rc = -EALREADY;
		goto out_sem;
	}

	rc = class_connect(&conn, obd, cluuid);
	if (rc)
		goto out_sem;

	cli->cl_conn_count++;
	*exp = class_conn2export(&conn);

	LASSERT(obd->obd_namespace);

	imp->imp_dlm_handle = conn;
	rc = ptlrpc_init_import(imp);
	if (rc != 0)
		goto out_ldlm;

	ocd = &imp->imp_connect_data;
	if (data) {
		*ocd = *data;
		imp->imp_connect_flags_orig = data->ocd_connect_flags;
	}

	rc = ptlrpc_connect_import(imp);
	if (rc != 0) {
		LASSERT(imp->imp_state == LUSTRE_IMP_DISCON);
		goto out_ldlm;
	}
	LASSERT(*exp && (*exp)->exp_connection);

	if (data) {
		LASSERTF((ocd->ocd_connect_flags & data->ocd_connect_flags) ==
			 ocd->ocd_connect_flags, "old %#llx, new %#llx\n",
			 data->ocd_connect_flags, ocd->ocd_connect_flags);
		data->ocd_connect_flags = ocd->ocd_connect_flags;
	}

	ptlrpc_pinger_add_import(imp);

	if (rc) {
out_ldlm:
		cli->cl_conn_count--;
		class_disconnect(*exp);
		*exp = NULL;
	}
out_sem:
	up_write(&cli->cl_sem);

	return rc;
}
EXPORT_SYMBOL(client_connect_import);

int client_disconnect_export(struct obd_export *exp)
{
	struct obd_device *obd = class_exp2obd(exp);
	struct client_obd *cli;
	struct obd_import *imp;
	int rc = 0, err;

	if (!obd) {
		CERROR("invalid export for disconnect: exp %p cookie %#llx\n",
		       exp, exp ? exp->exp_handle.h_cookie : -1);
		return -EINVAL;
	}

	cli = &obd->u.cli;
	imp = cli->cl_import;

	down_write(&cli->cl_sem);
	CDEBUG(D_INFO, "disconnect %s - %d\n", obd->obd_name,
	       cli->cl_conn_count);

	if (!cli->cl_conn_count) {
		CERROR("disconnecting disconnected device (%s)\n",
		       obd->obd_name);
		rc = -EINVAL;
		goto out_disconnect;
	}

	cli->cl_conn_count--;
	if (cli->cl_conn_count) {
		rc = 0;
		goto out_disconnect;
	}

	/* Mark import deactivated now, so we don't try to reconnect if any
	 * of the cleanup RPCs fails (e.g. LDLM cancel, etc).  We don't
	 * fully deactivate the import, or that would drop all requests.
	 */
	spin_lock(&imp->imp_lock);
	imp->imp_deactive = 1;
	spin_unlock(&imp->imp_lock);

	/* Some non-replayable imports (MDS's OSCs) are pinged, so just
	 * delete it regardless.  (It's safe to delete an import that was
	 * never added.)
	 */
	(void)ptlrpc_pinger_del_import(imp);

	if (obd->obd_namespace) {
		/* obd_force == local only */
		ldlm_cli_cancel_unused(obd->obd_namespace, NULL,
				       obd->obd_force ? LCF_LOCAL : 0, NULL);
		ldlm_namespace_free_prior(obd->obd_namespace, imp,
					  obd->obd_force);
	}

	/* There's no need to hold sem while disconnecting an import,
	 * and it may actually cause deadlock in GSS.
	 */
	up_write(&cli->cl_sem);
	rc = ptlrpc_disconnect_import(imp, 0);
	down_write(&cli->cl_sem);

	ptlrpc_invalidate_import(imp);

out_disconnect:
	/* Use server style - class_disconnect should be always called for
	 * o_disconnect.
	 */
	err = class_disconnect(exp);
	if (!rc && err)
		rc = err;

	up_write(&cli->cl_sem);

	return rc;
}
EXPORT_SYMBOL(client_disconnect_export);

/**
 * Packs current SLV and Limit into \a req.
 */
int target_pack_pool_reply(struct ptlrpc_request *req)
{
	struct obd_device *obd;

	/* Check that we still have all structures alive as this may
	 * be some late RPC at shutdown time.
	 */
	if (unlikely(!req->rq_export || !req->rq_export->exp_obd ||
		     !exp_connect_lru_resize(req->rq_export))) {
		lustre_msg_set_slv(req->rq_repmsg, 0);
		lustre_msg_set_limit(req->rq_repmsg, 0);
		return 0;
	}

	/* OBD is alive here as export is alive, which we checked above. */
	obd = req->rq_export->exp_obd;

	read_lock(&obd->obd_pool_lock);
	lustre_msg_set_slv(req->rq_repmsg, obd->obd_pool_slv);
	lustre_msg_set_limit(req->rq_repmsg, obd->obd_pool_limit);
	read_unlock(&obd->obd_pool_lock);

	return 0;
}
EXPORT_SYMBOL(target_pack_pool_reply);

static int
target_send_reply_msg(struct ptlrpc_request *req, int rc, int fail_id)
{
	if (OBD_FAIL_CHECK_ORSET(fail_id & ~OBD_FAIL_ONCE, OBD_FAIL_ONCE)) {
		DEBUG_REQ(D_ERROR, req, "dropping reply");
		return -ECOMM;
	}

	if (unlikely(rc)) {
		DEBUG_REQ(D_NET, req, "processing error (%d)", rc);
		req->rq_status = rc;
		return ptlrpc_send_error(req, 1);
	}

	DEBUG_REQ(D_NET, req, "sending reply");
	return ptlrpc_send_reply(req, PTLRPC_REPLY_MAYBE_DIFFICULT);
}

void target_send_reply(struct ptlrpc_request *req, int rc, int fail_id)
{
	struct ptlrpc_service_part *svcpt;
	int			netrc;
	struct ptlrpc_reply_state *rs;
	struct obd_export	 *exp;

	if (req->rq_no_reply)
		return;

	svcpt = req->rq_rqbd->rqbd_svcpt;
	rs = req->rq_reply_state;
	if (!rs || !rs->rs_difficult) {
		/* no notifiers */
		target_send_reply_msg(req, rc, fail_id);
		return;
	}

	/* must be an export if locks saved */
	LASSERT(req->rq_export);
	/* req/reply consistent */
	LASSERT(rs->rs_svcpt == svcpt);

	/* "fresh" reply */
	LASSERT(!rs->rs_scheduled);
	LASSERT(!rs->rs_scheduled_ever);
	LASSERT(!rs->rs_handled);
	LASSERT(!rs->rs_on_net);
	LASSERT(!rs->rs_export);
	LASSERT(list_empty(&rs->rs_obd_list));
	LASSERT(list_empty(&rs->rs_exp_list));

	exp = class_export_get(req->rq_export);

	/* disable reply scheduling while I'm setting up */
	rs->rs_scheduled = 1;
	rs->rs_on_net    = 1;
	rs->rs_xid       = req->rq_xid;
	rs->rs_transno   = req->rq_transno;
	rs->rs_export    = exp;
	rs->rs_opc       = lustre_msg_get_opc(req->rq_reqmsg);

	spin_lock(&exp->exp_uncommitted_replies_lock);
	CDEBUG(D_NET, "rs transno = %llu, last committed = %llu\n",
	       rs->rs_transno, exp->exp_last_committed);
	if (rs->rs_transno > exp->exp_last_committed) {
		/* not committed already */
		list_add_tail(&rs->rs_obd_list,
			      &exp->exp_uncommitted_replies);
	}
	spin_unlock(&exp->exp_uncommitted_replies_lock);

	spin_lock(&exp->exp_lock);
	list_add_tail(&rs->rs_exp_list, &exp->exp_outstanding_replies);
	spin_unlock(&exp->exp_lock);

	netrc = target_send_reply_msg(req, rc, fail_id);

	spin_lock(&svcpt->scp_rep_lock);

	atomic_inc(&svcpt->scp_nreps_difficult);

	if (netrc != 0) {
		/* error sending: reply is off the net.  Also we need +1
		 * reply ref until ptlrpc_handle_rs() is done
		 * with the reply state (if the send was successful, there
		 * would have been +1 ref for the net, which
		 * reply_out_callback leaves alone)
		 */
		rs->rs_on_net = 0;
		ptlrpc_rs_addref(rs);
	}

	spin_lock(&rs->rs_lock);
	if (rs->rs_transno <= exp->exp_last_committed ||
	    (!rs->rs_on_net && !rs->rs_no_ack) ||
	    list_empty(&rs->rs_exp_list) ||     /* completed already */
	    list_empty(&rs->rs_obd_list)) {
		CDEBUG(D_HA, "Schedule reply immediately\n");
		ptlrpc_dispatch_difficult_reply(rs);
	} else {
		list_add(&rs->rs_list, &svcpt->scp_rep_active);
		rs->rs_scheduled = 0;	/* allow notifier to schedule */
	}
	spin_unlock(&rs->rs_lock);
	spin_unlock(&svcpt->scp_rep_lock);
}
EXPORT_SYMBOL(target_send_reply);

enum ldlm_mode lck_compat_array[] = {
	[LCK_EX]	= LCK_COMPAT_EX,
	[LCK_PW]	= LCK_COMPAT_PW,
	[LCK_PR]	= LCK_COMPAT_PR,
	[LCK_CW]	= LCK_COMPAT_CW,
	[LCK_CR]	= LCK_COMPAT_CR,
	[LCK_NL]	= LCK_COMPAT_NL,
	[LCK_GROUP]	= LCK_COMPAT_GROUP,
	[LCK_COS]	= LCK_COMPAT_COS,
};

/**
 * Rather arbitrary mapping from LDLM error codes to errno values. This should
 * not escape to the user level.
 */
int ldlm_error2errno(enum ldlm_error error)
{
	int result;

	switch (error) {
	case ELDLM_OK:
	case ELDLM_LOCK_MATCHED:
		result = 0;
		break;
	case ELDLM_LOCK_CHANGED:
		result = -ESTALE;
		break;
	case ELDLM_LOCK_ABORTED:
		result = -ENAVAIL;
		break;
	case ELDLM_LOCK_REPLACED:
		result = -ESRCH;
		break;
	case ELDLM_NO_LOCK_DATA:
		result = -ENOENT;
		break;
	case ELDLM_NAMESPACE_EXISTS:
		result = -EEXIST;
		break;
	case ELDLM_BAD_NAMESPACE:
		result = -EBADF;
		break;
	default:
		if (((int)error) < 0)  /* cast to signed type */
			result = error; /* as enum ldlm_error can be unsigned */
		else {
			CERROR("Invalid DLM result code: %d\n", error);
			result = -EPROTO;
		}
	}
	return result;
}
EXPORT_SYMBOL(ldlm_error2errno);

#if LUSTRE_TRACKS_LOCK_EXP_REFS
void ldlm_dump_export_locks(struct obd_export *exp)
{
	spin_lock(&exp->exp_locks_list_guard);
	if (!list_empty(&exp->exp_locks_list)) {
		struct ldlm_lock *lock;

		CERROR("dumping locks for export %p,ignore if the unmount doesn't hang\n",
		       exp);
		list_for_each_entry(lock, &exp->exp_locks_list,
				    l_exp_refs_link)
			LDLM_ERROR(lock, "lock:");
	}
	spin_unlock(&exp->exp_locks_list_guard);
}
#endif
