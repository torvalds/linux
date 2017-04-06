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
 * lustre/fld/fld_request.c
 *
 * FLD (Fids Location Database)
 *
 * Author: Yury Umanets <umka@clusterfs.com>
 */

#define DEBUG_SUBSYSTEM S_FLD

#include "../../include/linux/libcfs/libcfs.h"
#include <linux/module.h>
#include <asm/div64.h>

#include "../include/obd.h"
#include "../include/obd_class.h"
#include "../include/lustre_ver.h"
#include "../include/obd_support.h"
#include "../include/lprocfs_status.h"

#include "../include/lustre_req_layout.h"
#include "../include/lustre_fld.h"
#include "../include/lustre_mdc.h"
#include "fld_internal.h"

static int fld_rrb_hash(struct lu_client_fld *fld, u64 seq)
{
	LASSERT(fld->lcf_count > 0);
	return do_div(seq, fld->lcf_count);
}

static struct lu_fld_target *
fld_rrb_scan(struct lu_client_fld *fld, u64 seq)
{
	struct lu_fld_target *target;
	int hash;

	/* Because almost all of special sequence located in MDT0,
	 * it should go to index 0 directly, instead of calculating
	 * hash again, and also if other MDTs is not being connected,
	 * the fld lookup requests(for seq on MDT0) should not be
	 * blocked because of other MDTs
	 */
	if (fid_seq_is_norm(seq))
		hash = fld_rrb_hash(fld, seq);
	else
		hash = 0;

again:
	list_for_each_entry(target, &fld->lcf_targets, ft_chain) {
		if (target->ft_idx == hash)
			return target;
	}

	if (hash != 0) {
		/* It is possible the remote target(MDT) are not connected to
		 * with client yet, so we will refer this to MDT0, which should
		 * be connected during mount
		 */
		hash = 0;
		goto again;
	}

	CERROR("%s: Can't find target by hash %d (seq %#llx). Targets (%d):\n",
	       fld->lcf_name, hash, seq, fld->lcf_count);

	list_for_each_entry(target, &fld->lcf_targets, ft_chain) {
		const char *srv_name = target->ft_srv ?
			target->ft_srv->lsf_name : "<null>";
		const char *exp_name = target->ft_exp ?
			(char *)target->ft_exp->exp_obd->obd_uuid.uuid :
			"<null>";

		CERROR("  exp: 0x%p (%s), srv: 0x%p (%s), idx: %llu\n",
		       target->ft_exp, exp_name, target->ft_srv,
		       srv_name, target->ft_idx);
	}

	/*
	 * If target is not found, there is logical error anyway, so here is
	 * LBUG() to catch this situation.
	 */
	LBUG();
	return NULL;
}

struct lu_fld_hash fld_hash[] = {
	{
		.fh_name = "RRB",
		.fh_hash_func = fld_rrb_hash,
		.fh_scan_func = fld_rrb_scan
	},
	{
		NULL,
	}
};

static struct lu_fld_target *
fld_client_get_target(struct lu_client_fld *fld, u64 seq)
{
	struct lu_fld_target *target;

	LASSERT(fld->lcf_hash);

	spin_lock(&fld->lcf_lock);
	target = fld->lcf_hash->fh_scan_func(fld, seq);
	spin_unlock(&fld->lcf_lock);

	if (target) {
		CDEBUG(D_INFO, "%s: Found target (idx %llu) by seq %#llx\n",
		       fld->lcf_name, target->ft_idx, seq);
	}

	return target;
}

/*
 * Add export to FLD. This is usually done by CMM and LMV as they are main users
 * of FLD module.
 */
int fld_client_add_target(struct lu_client_fld *fld,
			  struct lu_fld_target *tar)
{
	const char *name;
	struct lu_fld_target *target, *tmp;

	LASSERT(tar);
	name = fld_target_name(tar);
	LASSERT(name);
	LASSERT(tar->ft_srv || tar->ft_exp);

	CDEBUG(D_INFO, "%s: Adding target %s (idx %llu)\n",
	       fld->lcf_name, name, tar->ft_idx);

	target = kzalloc(sizeof(*target), GFP_NOFS);
	if (!target)
		return -ENOMEM;

	spin_lock(&fld->lcf_lock);
	list_for_each_entry(tmp, &fld->lcf_targets, ft_chain) {
		if (tmp->ft_idx == tar->ft_idx) {
			spin_unlock(&fld->lcf_lock);
			kfree(target);
			CERROR("Target %s exists in FLD and known as %s:#%llu\n",
			       name, fld_target_name(tmp), tmp->ft_idx);
			return -EEXIST;
		}
	}

	target->ft_exp = tar->ft_exp;
	if (target->ft_exp)
		class_export_get(target->ft_exp);
	target->ft_srv = tar->ft_srv;
	target->ft_idx = tar->ft_idx;

	list_add_tail(&target->ft_chain, &fld->lcf_targets);

	fld->lcf_count++;
	spin_unlock(&fld->lcf_lock);

	return 0;
}
EXPORT_SYMBOL(fld_client_add_target);

/* Remove export from FLD */
int fld_client_del_target(struct lu_client_fld *fld, __u64 idx)
{
	struct lu_fld_target *target, *tmp;

	spin_lock(&fld->lcf_lock);
	list_for_each_entry_safe(target, tmp, &fld->lcf_targets, ft_chain) {
		if (target->ft_idx == idx) {
			fld->lcf_count--;
			list_del(&target->ft_chain);
			spin_unlock(&fld->lcf_lock);

			if (target->ft_exp)
				class_export_put(target->ft_exp);

			kfree(target);
			return 0;
		}
	}
	spin_unlock(&fld->lcf_lock);
	return -ENOENT;
}

static struct dentry *fld_debugfs_dir;

static int fld_client_debugfs_init(struct lu_client_fld *fld)
{
	int rc;

	fld->lcf_debugfs_entry = ldebugfs_register(fld->lcf_name,
						   fld_debugfs_dir,
						   NULL, NULL);

	if (IS_ERR_OR_NULL(fld->lcf_debugfs_entry)) {
		CERROR("%s: LdebugFS failed in fld-init\n", fld->lcf_name);
		rc = fld->lcf_debugfs_entry ? PTR_ERR(fld->lcf_debugfs_entry)
					    : -ENOMEM;
		fld->lcf_debugfs_entry = NULL;
		return rc;
	}

	rc = ldebugfs_add_vars(fld->lcf_debugfs_entry,
			       fld_client_debugfs_list, fld);
	if (rc) {
		CERROR("%s: Can't init FLD debufs, rc %d\n", fld->lcf_name, rc);
		goto out_cleanup;
	}

	return 0;

out_cleanup:
	fld_client_debugfs_fini(fld);
	return rc;
}

void fld_client_debugfs_fini(struct lu_client_fld *fld)
{
	if (!IS_ERR_OR_NULL(fld->lcf_debugfs_entry))
		ldebugfs_remove(&fld->lcf_debugfs_entry);
}
EXPORT_SYMBOL(fld_client_debugfs_fini);

static inline int hash_is_sane(int hash)
{
	return (hash >= 0 && hash < ARRAY_SIZE(fld_hash));
}

int fld_client_init(struct lu_client_fld *fld,
		    const char *prefix, int hash)
{
	int cache_size, cache_threshold;
	int rc;

	snprintf(fld->lcf_name, sizeof(fld->lcf_name),
		 "cli-%s", prefix);

	if (!hash_is_sane(hash)) {
		CERROR("%s: Wrong hash function %#x\n",
		       fld->lcf_name, hash);
		return -EINVAL;
	}

	fld->lcf_count = 0;
	spin_lock_init(&fld->lcf_lock);
	fld->lcf_hash = &fld_hash[hash];
	INIT_LIST_HEAD(&fld->lcf_targets);

	cache_size = FLD_CLIENT_CACHE_SIZE /
		sizeof(struct fld_cache_entry);

	cache_threshold = cache_size *
		FLD_CLIENT_CACHE_THRESHOLD / 100;

	fld->lcf_cache = fld_cache_init(fld->lcf_name,
					cache_size, cache_threshold);
	if (IS_ERR(fld->lcf_cache)) {
		rc = PTR_ERR(fld->lcf_cache);
		fld->lcf_cache = NULL;
		goto out;
	}

	rc = fld_client_debugfs_init(fld);
	if (rc)
		goto out;
out:
	if (rc)
		fld_client_fini(fld);
	else
		CDEBUG(D_INFO, "%s: Using \"%s\" hash\n",
		       fld->lcf_name, fld->lcf_hash->fh_name);
	return rc;
}
EXPORT_SYMBOL(fld_client_init);

void fld_client_fini(struct lu_client_fld *fld)
{
	struct lu_fld_target *target, *tmp;

	spin_lock(&fld->lcf_lock);
	list_for_each_entry_safe(target, tmp, &fld->lcf_targets, ft_chain) {
		fld->lcf_count--;
		list_del(&target->ft_chain);
		if (target->ft_exp)
			class_export_put(target->ft_exp);
		kfree(target);
	}
	spin_unlock(&fld->lcf_lock);

	if (fld->lcf_cache) {
		if (!IS_ERR(fld->lcf_cache))
			fld_cache_fini(fld->lcf_cache);
		fld->lcf_cache = NULL;
	}
}
EXPORT_SYMBOL(fld_client_fini);

int fld_client_rpc(struct obd_export *exp,
		   struct lu_seq_range *range, __u32 fld_op,
		   struct ptlrpc_request **reqp)
{
	struct ptlrpc_request *req = NULL;
	struct lu_seq_range   *prange;
	__u32		 *op;
	int		    rc = 0;
	struct obd_import     *imp;

	LASSERT(exp);

	imp = class_exp2cliimp(exp);
	switch (fld_op) {
	case FLD_QUERY:
		req = ptlrpc_request_alloc_pack(imp, &RQF_FLD_QUERY,
						LUSTRE_MDS_VERSION, FLD_QUERY);
		if (!req)
			return -ENOMEM;

		/*
		 * XXX: only needed when talking to old server(< 2.6), it should
		 * be removed when < 2.6 server is not supported
		 */
		op = req_capsule_client_get(&req->rq_pill, &RMF_FLD_OPC);
		*op = FLD_LOOKUP;

		if (imp->imp_connect_flags_orig & OBD_CONNECT_MDS_MDS)
			req->rq_allow_replay = 1;
		break;
	case FLD_READ:
		req = ptlrpc_request_alloc_pack(imp, &RQF_FLD_READ,
						LUSTRE_MDS_VERSION, FLD_READ);
		if (!req)
			return -ENOMEM;

		req_capsule_set_size(&req->rq_pill, &RMF_GENERIC_DATA,
				     RCL_SERVER, PAGE_SIZE);
		break;
	default:
		rc = -EINVAL;
		break;
	}
	if (rc)
		return rc;

	prange = req_capsule_client_get(&req->rq_pill, &RMF_FLD_MDFLD);
	*prange = *range;
	ptlrpc_request_set_replen(req);
	req->rq_request_portal = FLD_REQUEST_PORTAL;
	req->rq_reply_portal = MDC_REPLY_PORTAL;
	ptlrpc_at_set_req_timeout(req);

	obd_get_request_slot(&exp->exp_obd->u.cli);
	rc = ptlrpc_queue_wait(req);
	obd_put_request_slot(&exp->exp_obd->u.cli);
	if (rc)
		goto out_req;

	if (fld_op == FLD_QUERY) {
		prange = req_capsule_server_get(&req->rq_pill, &RMF_FLD_MDFLD);
		if (!prange) {
			rc = -EFAULT;
			goto out_req;
		}
		*range = *prange;
	}

out_req:
	if (rc || !reqp) {
		ptlrpc_req_finished(req);
		req = NULL;
	}

	if (reqp)
		*reqp = req;

	return rc;
}

int fld_client_lookup(struct lu_client_fld *fld, u64 seq, u32 *mds,
		      __u32 flags, const struct lu_env *env)
{
	struct lu_seq_range res = { 0 };
	struct lu_fld_target *target;
	int rc;

	rc = fld_cache_lookup(fld->lcf_cache, seq, &res);
	if (rc == 0) {
		*mds = res.lsr_index;
		return 0;
	}

	/* Can not find it in the cache */
	target = fld_client_get_target(fld, seq);
	LASSERT(target);

	CDEBUG(D_INFO, "%s: Lookup fld entry (seq: %#llx) on target %s (idx %llu)\n",
	       fld->lcf_name, seq, fld_target_name(target), target->ft_idx);

	res.lsr_start = seq;
	fld_range_set_type(&res, flags);
	rc = fld_client_rpc(target->ft_exp, &res, FLD_QUERY, NULL);

	if (rc == 0) {
		*mds = res.lsr_index;

		fld_cache_insert(fld->lcf_cache, &res);
	}
	return rc;
}
EXPORT_SYMBOL(fld_client_lookup);

void fld_client_flush(struct lu_client_fld *fld)
{
	fld_cache_flush(fld->lcf_cache);
}

static int __init fld_init(void)
{
	fld_debugfs_dir = ldebugfs_register(LUSTRE_FLD_NAME,
					    debugfs_lustre_root,
					    NULL, NULL);
	return PTR_ERR_OR_ZERO(fld_debugfs_dir);
}

static void __exit fld_exit(void)
{
	if (!IS_ERR_OR_NULL(fld_debugfs_dir))
		ldebugfs_remove(&fld_debugfs_dir);
}

MODULE_AUTHOR("OpenSFS, Inc. <http://www.lustre.org/>");
MODULE_DESCRIPTION("Lustre FID Location Database");
MODULE_VERSION(LUSTRE_VERSION_STRING);
MODULE_LICENSE("GPL");

module_init(fld_init)
module_exit(fld_exit)
