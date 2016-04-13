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
 * Copyright (c) 2004, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2015, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#ifndef _LMV_INTERNAL_H_
#define _LMV_INTERNAL_H_

#include "../include/lustre/lustre_idl.h"
#include "../include/obd.h"

#define LMV_MAX_TGT_COUNT 128

#define lmv_init_lock(lmv)   mutex_lock(&lmv->init_mutex)
#define lmv_init_unlock(lmv) mutex_unlock(&lmv->init_mutex)

#define LL_IT2STR(it)					\
	((it) ? ldlm_it2str((it)->it_op) : "0")

int lmv_check_connect(struct obd_device *obd);

int lmv_intent_lock(struct obd_export *exp, struct md_op_data *op_data,
		    void *lmm, int lmmsize, struct lookup_intent *it,
		    int flags, struct ptlrpc_request **reqp,
		    ldlm_blocking_callback cb_blocking,
		    __u64 extra_lock_flags);

int lmv_fld_lookup(struct lmv_obd *lmv, const struct lu_fid *fid, u32 *mds);
int __lmv_fid_alloc(struct lmv_obd *lmv, struct lu_fid *fid, u32 mds);
int lmv_fid_alloc(struct obd_export *exp, struct lu_fid *fid,
		  struct md_op_data *op_data);

static inline struct lmv_stripe_md *lmv_get_mea(struct ptlrpc_request *req)
{
	struct mdt_body	 *body;
	struct lmv_stripe_md    *mea;

	LASSERT(req);

	body = req_capsule_server_get(&req->rq_pill, &RMF_MDT_BODY);

	if (!body || !S_ISDIR(body->mode) || !body->eadatasize)
		return NULL;

	mea = req_capsule_server_sized_get(&req->rq_pill, &RMF_MDT_MD,
					   body->eadatasize);
	if (mea->mea_count == 0)
		return NULL;
	if (mea->mea_magic != MEA_MAGIC_LAST_CHAR &&
	    mea->mea_magic != MEA_MAGIC_ALL_CHARS &&
	    mea->mea_magic != MEA_MAGIC_HASH_SEGMENT)
		return NULL;

	return mea;
}

static inline int lmv_get_easize(struct lmv_obd *lmv)
{
	return sizeof(struct lmv_stripe_md) +
		lmv->desc.ld_tgt_count *
		sizeof(struct lu_fid);
}

static inline struct lmv_tgt_desc *
lmv_get_target(struct lmv_obd *lmv, u32 mds)
{
	int count = lmv->desc.ld_tgt_count;
	int i;

	for (i = 0; i < count; i++) {
		if (!lmv->tgts[i])
			continue;

		if (lmv->tgts[i]->ltd_idx == mds)
			return lmv->tgts[i];
	}

	return ERR_PTR(-ENODEV);
}

static inline struct lmv_tgt_desc *
lmv_find_target(struct lmv_obd *lmv, const struct lu_fid *fid)
{
	u32 mds = 0;
	int rc;

	if (lmv->desc.ld_tgt_count > 1) {
		rc = lmv_fld_lookup(lmv, fid, &mds);
		if (rc)
			return ERR_PTR(rc);
	}

	return lmv_get_target(lmv, mds);
}

struct lmv_tgt_desc
*lmv_locate_mds(struct lmv_obd *lmv, struct md_op_data *op_data,
		struct lu_fid *fid);
/* lproc_lmv.c */
void lprocfs_lmv_init_vars(struct lprocfs_static_vars *lvars);

extern struct file_operations lmv_proc_target_fops;

#endif
