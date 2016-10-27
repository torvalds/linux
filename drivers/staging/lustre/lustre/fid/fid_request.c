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
 * lustre/fid/fid_request.c
 *
 * Lustre Sequence Manager
 *
 * Author: Yury Umanets <umka@clusterfs.com>
 */

#define DEBUG_SUBSYSTEM S_FID

#include "../../include/linux/libcfs/libcfs.h"
#include <linux/module.h>

#include "../include/obd.h"
#include "../include/obd_class.h"
#include "../include/obd_support.h"
#include "../include/lustre_fid.h"
/* mdc RPC locks */
#include "../include/lustre_mdc.h"
#include "fid_internal.h"

static struct dentry *seq_debugfs_dir;

static int seq_client_rpc(struct lu_client_seq *seq,
			  struct lu_seq_range *output, __u32 opc,
			  const char *opcname)
{
	struct obd_export     *exp = seq->lcs_exp;
	struct ptlrpc_request *req;
	struct lu_seq_range   *out, *in;
	__u32                 *op;
	unsigned int           debug_mask;
	int                    rc;

	LASSERT(exp && !IS_ERR(exp));
	req = ptlrpc_request_alloc_pack(class_exp2cliimp(exp), &RQF_SEQ_QUERY,
					LUSTRE_MDS_VERSION, SEQ_QUERY);
	if (!req)
		return -ENOMEM;

	/* Init operation code */
	op = req_capsule_client_get(&req->rq_pill, &RMF_SEQ_OPC);
	*op = opc;

	/* Zero out input range, this is not recovery yet. */
	in = req_capsule_client_get(&req->rq_pill, &RMF_SEQ_RANGE);
	lu_seq_range_init(in);

	ptlrpc_request_set_replen(req);

	in->lsr_index = seq->lcs_space.lsr_index;
	if (seq->lcs_type == LUSTRE_SEQ_METADATA)
		fld_range_set_mdt(in);
	else
		fld_range_set_ost(in);

	if (opc == SEQ_ALLOC_SUPER) {
		req->rq_request_portal = SEQ_CONTROLLER_PORTAL;
		req->rq_reply_portal = MDC_REPLY_PORTAL;
		/* During allocating super sequence for data object,
		 * the current thread might hold the export of MDT0(MDT0
		 * precreating objects on this OST), and it will send the
		 * request to MDT0 here, so we can not keep resending the
		 * request here, otherwise if MDT0 is failed(umounted),
		 * it can not release the export of MDT0
		 */
		if (seq->lcs_type == LUSTRE_SEQ_DATA) {
			req->rq_no_delay = 1;
			req->rq_no_resend = 1;
		}
		debug_mask = D_CONSOLE;
	} else {
		if (seq->lcs_type == LUSTRE_SEQ_METADATA) {
			req->rq_reply_portal = MDC_REPLY_PORTAL;
			req->rq_request_portal = SEQ_METADATA_PORTAL;
		} else {
			req->rq_reply_portal = OSC_REPLY_PORTAL;
			req->rq_request_portal = SEQ_DATA_PORTAL;
		}
		debug_mask = D_INFO;
	}

	ptlrpc_at_set_req_timeout(req);

	if (opc != SEQ_ALLOC_SUPER && seq->lcs_type == LUSTRE_SEQ_METADATA)
		mdc_get_rpc_lock(exp->exp_obd->u.cli.cl_rpc_lock, NULL);
	rc = ptlrpc_queue_wait(req);
	if (opc != SEQ_ALLOC_SUPER && seq->lcs_type == LUSTRE_SEQ_METADATA)
		mdc_put_rpc_lock(exp->exp_obd->u.cli.cl_rpc_lock, NULL);
	if (rc)
		goto out_req;

	out = req_capsule_server_get(&req->rq_pill, &RMF_SEQ_RANGE);
	*output = *out;

	if (!lu_seq_range_is_sane(output)) {
		CERROR("%s: Invalid range received from server: "
		       DRANGE "\n", seq->lcs_name, PRANGE(output));
		rc = -EINVAL;
		goto out_req;
	}

	if (lu_seq_range_is_exhausted(output)) {
		CERROR("%s: Range received from server is exhausted: "
		       DRANGE "]\n", seq->lcs_name, PRANGE(output));
		rc = -EINVAL;
		goto out_req;
	}

	CDEBUG_LIMIT(debug_mask, "%s: Allocated %s-sequence " DRANGE "]\n",
		     seq->lcs_name, opcname, PRANGE(output));

out_req:
	ptlrpc_req_finished(req);
	return rc;
}

/* Request sequence-controller node to allocate new meta-sequence. */
static int seq_client_alloc_meta(const struct lu_env *env,
				 struct lu_client_seq *seq)
{
	int rc;

	do {
		/* If meta server return -EINPROGRESS or EAGAIN,
		 * it means meta server might not be ready to
		 * allocate super sequence from sequence controller
		 * (MDT0)yet
		 */
		rc = seq_client_rpc(seq, &seq->lcs_space,
				    SEQ_ALLOC_META, "meta");
	} while (rc == -EINPROGRESS || rc == -EAGAIN);

	return rc;
}

/* Allocate new sequence for client. */
static int seq_client_alloc_seq(const struct lu_env *env,
				struct lu_client_seq *seq, u64 *seqnr)
{
	int rc;

	LASSERT(lu_seq_range_is_sane(&seq->lcs_space));

	if (lu_seq_range_is_exhausted(&seq->lcs_space)) {
		rc = seq_client_alloc_meta(env, seq);
		if (rc) {
			CERROR("%s: Can't allocate new meta-sequence, rc %d\n",
			       seq->lcs_name, rc);
			return rc;
		}
		CDEBUG(D_INFO, "%s: New range - " DRANGE "\n",
		       seq->lcs_name, PRANGE(&seq->lcs_space));
	} else {
		rc = 0;
	}

	LASSERT(!lu_seq_range_is_exhausted(&seq->lcs_space));
	*seqnr = seq->lcs_space.lsr_start;
	seq->lcs_space.lsr_start += 1;

	CDEBUG(D_INFO, "%s: Allocated sequence [%#llx]\n", seq->lcs_name,
	       *seqnr);

	return rc;
}

static int seq_fid_alloc_prep(struct lu_client_seq *seq,
			      wait_queue_t *link)
{
	if (seq->lcs_update) {
		add_wait_queue(&seq->lcs_waitq, link);
		set_current_state(TASK_UNINTERRUPTIBLE);
		mutex_unlock(&seq->lcs_mutex);

		schedule();

		mutex_lock(&seq->lcs_mutex);
		remove_wait_queue(&seq->lcs_waitq, link);
		set_current_state(TASK_RUNNING);
		return -EAGAIN;
	}
	++seq->lcs_update;
	mutex_unlock(&seq->lcs_mutex);
	return 0;
}

static void seq_fid_alloc_fini(struct lu_client_seq *seq)
{
	LASSERT(seq->lcs_update == 1);
	mutex_lock(&seq->lcs_mutex);
	--seq->lcs_update;
	wake_up(&seq->lcs_waitq);
}

/* Allocate new fid on passed client @seq and save it to @fid. */
int seq_client_alloc_fid(const struct lu_env *env,
			 struct lu_client_seq *seq, struct lu_fid *fid)
{
	wait_queue_t link;
	int rc;

	LASSERT(seq);
	LASSERT(fid);

	init_waitqueue_entry(&link, current);
	mutex_lock(&seq->lcs_mutex);

	if (OBD_FAIL_CHECK(OBD_FAIL_SEQ_EXHAUST))
		seq->lcs_fid.f_oid = seq->lcs_width;

	while (1) {
		u64 seqnr;

		if (!fid_is_zero(&seq->lcs_fid) &&
		    fid_oid(&seq->lcs_fid) < seq->lcs_width) {
			/* Just bump last allocated fid and return to caller. */
			seq->lcs_fid.f_oid += 1;
			rc = 0;
			break;
		}

		rc = seq_fid_alloc_prep(seq, &link);
		if (rc)
			continue;

		rc = seq_client_alloc_seq(env, seq, &seqnr);
		if (rc) {
			CERROR("%s: Can't allocate new sequence, rc %d\n",
			       seq->lcs_name, rc);
			seq_fid_alloc_fini(seq);
			mutex_unlock(&seq->lcs_mutex);
			return rc;
		}

		CDEBUG(D_INFO, "%s: Switch to sequence [0x%16.16Lx]\n",
		       seq->lcs_name, seqnr);

		seq->lcs_fid.f_oid = LUSTRE_FID_INIT_OID;
		seq->lcs_fid.f_seq = seqnr;
		seq->lcs_fid.f_ver = 0;

		/*
		 * Inform caller that sequence switch is performed to allow it
		 * to setup FLD for it.
		 */
		rc = 1;

		seq_fid_alloc_fini(seq);
		break;
	}

	*fid = seq->lcs_fid;
	mutex_unlock(&seq->lcs_mutex);

	CDEBUG(D_INFO, "%s: Allocated FID "DFID"\n", seq->lcs_name,  PFID(fid));
	return rc;
}
EXPORT_SYMBOL(seq_client_alloc_fid);

/*
 * Finish the current sequence due to disconnect.
 * See mdc_import_event()
 */
void seq_client_flush(struct lu_client_seq *seq)
{
	wait_queue_t link;

	LASSERT(seq);
	init_waitqueue_entry(&link, current);
	mutex_lock(&seq->lcs_mutex);

	while (seq->lcs_update) {
		add_wait_queue(&seq->lcs_waitq, &link);
		set_current_state(TASK_UNINTERRUPTIBLE);
		mutex_unlock(&seq->lcs_mutex);

		schedule();

		mutex_lock(&seq->lcs_mutex);
		remove_wait_queue(&seq->lcs_waitq, &link);
		set_current_state(TASK_RUNNING);
	}

	fid_zero(&seq->lcs_fid);
	/**
	 * this id shld not be used for seq range allocation.
	 * set to -1 for dgb check.
	 */

	seq->lcs_space.lsr_index = -1;

	lu_seq_range_init(&seq->lcs_space);
	mutex_unlock(&seq->lcs_mutex);
}
EXPORT_SYMBOL(seq_client_flush);

static void seq_client_debugfs_fini(struct lu_client_seq *seq)
{
	if (!IS_ERR_OR_NULL(seq->lcs_debugfs_entry))
		ldebugfs_remove(&seq->lcs_debugfs_entry);
}

static int seq_client_debugfs_init(struct lu_client_seq *seq)
{
	int rc;

	seq->lcs_debugfs_entry = ldebugfs_register(seq->lcs_name,
						   seq_debugfs_dir,
						   NULL, NULL);

	if (IS_ERR_OR_NULL(seq->lcs_debugfs_entry)) {
		CERROR("%s: LdebugFS failed in seq-init\n", seq->lcs_name);
		rc = seq->lcs_debugfs_entry ? PTR_ERR(seq->lcs_debugfs_entry)
					    : -ENOMEM;
		seq->lcs_debugfs_entry = NULL;
		return rc;
	}

	rc = ldebugfs_add_vars(seq->lcs_debugfs_entry,
			       seq_client_debugfs_list, seq);
	if (rc) {
		CERROR("%s: Can't init sequence manager debugfs, rc %d\n",
		       seq->lcs_name, rc);
		goto out_cleanup;
	}

	return 0;

out_cleanup:
	seq_client_debugfs_fini(seq);
	return rc;
}

static void seq_client_fini(struct lu_client_seq *seq)
{
	seq_client_debugfs_fini(seq);

	if (seq->lcs_exp) {
		class_export_put(seq->lcs_exp);
		seq->lcs_exp = NULL;
	}
}

static int seq_client_init(struct lu_client_seq *seq,
			   struct obd_export *exp,
			   enum lu_cli_type type,
			   const char *prefix)
{
	int rc;

	LASSERT(seq);
	LASSERT(prefix);

	seq->lcs_type = type;

	mutex_init(&seq->lcs_mutex);
	if (type == LUSTRE_SEQ_METADATA)
		seq->lcs_width = LUSTRE_METADATA_SEQ_MAX_WIDTH;
	else
		seq->lcs_width = LUSTRE_DATA_SEQ_MAX_WIDTH;

	init_waitqueue_head(&seq->lcs_waitq);
	/* Make sure that things are clear before work is started. */
	seq_client_flush(seq);

	seq->lcs_exp = class_export_get(exp);

	snprintf(seq->lcs_name, sizeof(seq->lcs_name),
		 "cli-%s", prefix);

	rc = seq_client_debugfs_init(seq);
	if (rc)
		seq_client_fini(seq);
	return rc;
}

int client_fid_init(struct obd_device *obd,
		    struct obd_export *exp, enum lu_cli_type type)
{
	struct client_obd *cli = &obd->u.cli;
	char *prefix;
	int rc;

	cli->cl_seq = kzalloc(sizeof(*cli->cl_seq), GFP_NOFS);
	if (!cli->cl_seq)
		return -ENOMEM;

	prefix = kzalloc(MAX_OBD_NAME + 5, GFP_NOFS);
	if (!prefix) {
		rc = -ENOMEM;
		goto out_free_seq;
	}

	snprintf(prefix, MAX_OBD_NAME + 5, "cli-%s", obd->obd_name);

	/* Init client side sequence-manager */
	rc = seq_client_init(cli->cl_seq, exp, type, prefix);
	kfree(prefix);
	if (rc)
		goto out_free_seq;

	return rc;
out_free_seq:
	kfree(cli->cl_seq);
	cli->cl_seq = NULL;
	return rc;
}
EXPORT_SYMBOL(client_fid_init);

int client_fid_fini(struct obd_device *obd)
{
	struct client_obd *cli = &obd->u.cli;

	if (cli->cl_seq) {
		seq_client_fini(cli->cl_seq);
		kfree(cli->cl_seq);
		cli->cl_seq = NULL;
	}

	return 0;
}
EXPORT_SYMBOL(client_fid_fini);

static int __init fid_init(void)
{
	seq_debugfs_dir = ldebugfs_register(LUSTRE_SEQ_NAME,
					    debugfs_lustre_root,
					    NULL, NULL);
	return PTR_ERR_OR_ZERO(seq_debugfs_dir);
}

static void __exit fid_exit(void)
{
	if (!IS_ERR_OR_NULL(seq_debugfs_dir))
		ldebugfs_remove(&seq_debugfs_dir);
}

MODULE_AUTHOR("OpenSFS, Inc. <http://www.lustre.org/>");
MODULE_DESCRIPTION("Lustre File IDentifier");
MODULE_VERSION(LUSTRE_VERSION_STRING);
MODULE_LICENSE("GPL");

module_init(fid_init);
module_exit(fid_exit);
