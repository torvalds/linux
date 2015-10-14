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
 * Copyright (c) 2011, 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#define DEBUG_SUBSYSTEM S_LMV
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/pagemap.h>
#include <asm/div64.h>
#include <linux/seq_file.h>
#include <linux/namei.h>
#include "../include/lustre_intent.h"
#include "../include/obd_support.h"
#include "../include/lustre/lustre_idl.h"
#include "../include/lustre_lib.h"
#include "../include/lustre_net.h"
#include "../include/lustre_dlm.h"
#include "../include/obd_class.h"
#include "../include/lprocfs_status.h"
#include "lmv_internal.h"

static int lmv_intent_remote(struct obd_export *exp, void *lmm,
			     int lmmsize, struct lookup_intent *it,
			     const struct lu_fid *parent_fid, int flags,
			     struct ptlrpc_request **reqp,
			     ldlm_blocking_callback cb_blocking,
			     __u64 extra_lock_flags)
{
	struct obd_device	*obd = exp->exp_obd;
	struct lmv_obd		*lmv = &obd->u.lmv;
	struct ptlrpc_request	*req = NULL;
	struct lustre_handle	plock;
	struct md_op_data	*op_data;
	struct lmv_tgt_desc	*tgt;
	struct mdt_body		*body;
	int			pmode;
	int			rc = 0;

	body = req_capsule_server_get(&(*reqp)->rq_pill, &RMF_MDT_BODY);
	if (body == NULL)
		return -EPROTO;

	LASSERT((body->valid & OBD_MD_MDS));

	/*
	 * Unfortunately, we have to lie to MDC/MDS to retrieve
	 * attributes llite needs and provideproper locking.
	 */
	if (it->it_op & IT_LOOKUP)
		it->it_op = IT_GETATTR;

	/*
	 * We got LOOKUP lock, but we really need attrs.
	 */
	pmode = it->d.lustre.it_lock_mode;
	if (pmode) {
		plock.cookie = it->d.lustre.it_lock_handle;
		it->d.lustre.it_lock_mode = 0;
		it->d.lustre.it_data = NULL;
	}

	LASSERT(fid_is_sane(&body->fid1));

	tgt = lmv_find_target(lmv, &body->fid1);
	if (IS_ERR(tgt)) {
		rc = PTR_ERR(tgt);
		goto out;
	}

	op_data = kzalloc(sizeof(*op_data), GFP_NOFS);
	if (!op_data) {
		rc = -ENOMEM;
		goto out;
	}

	op_data->op_fid1 = body->fid1;
	/* Sent the parent FID to the remote MDT */
	if (parent_fid != NULL) {
		/* The parent fid is only for remote open to
		 * check whether the open is from OBF,
		 * see mdt_cross_open */
		LASSERT(it->it_op & IT_OPEN);
		op_data->op_fid2 = *parent_fid;
		/* Add object FID to op_fid3, in case it needs to check stale
		 * (M_CHECK_STALE), see mdc_finish_intent_lock */
		op_data->op_fid3 = body->fid1;
	}

	op_data->op_bias = MDS_CROSS_REF;
	CDEBUG(D_INODE, "REMOTE_INTENT with fid="DFID" -> mds #%d\n",
	       PFID(&body->fid1), tgt->ltd_idx);

	rc = md_intent_lock(tgt->ltd_exp, op_data, lmm, lmmsize, it,
			    flags, &req, cb_blocking, extra_lock_flags);
	if (rc)
		goto out_free_op_data;

	/*
	 * LLite needs LOOKUP lock to track dentry revocation in order to
	 * maintain dcache consistency. Thus drop UPDATE|PERM lock here
	 * and put LOOKUP in request.
	 */
	if (it->d.lustre.it_lock_mode != 0) {
		it->d.lustre.it_remote_lock_handle =
					it->d.lustre.it_lock_handle;
		it->d.lustre.it_remote_lock_mode = it->d.lustre.it_lock_mode;
	}

	it->d.lustre.it_lock_handle = plock.cookie;
	it->d.lustre.it_lock_mode = pmode;

out_free_op_data:
	kfree(op_data);
out:
	if (rc && pmode)
		ldlm_lock_decref(&plock, pmode);

	ptlrpc_req_finished(*reqp);
	*reqp = req;
	return rc;
}

/*
 * IT_OPEN is intended to open (and create, possible) an object. Parent (pid)
 * may be split dir.
 */
int lmv_intent_open(struct obd_export *exp, struct md_op_data *op_data,
		    void *lmm, int lmmsize, struct lookup_intent *it,
		    int flags, struct ptlrpc_request **reqp,
		    ldlm_blocking_callback cb_blocking,
		    __u64 extra_lock_flags)
{
	struct obd_device	*obd = exp->exp_obd;
	struct lmv_obd		*lmv = &obd->u.lmv;
	struct lmv_tgt_desc	*tgt;
	struct mdt_body		*body;
	int			rc;

	tgt = lmv_locate_mds(lmv, op_data, &op_data->op_fid1);
	if (IS_ERR(tgt))
		return PTR_ERR(tgt);

	/* If it is ready to open the file by FID, do not need
	 * allocate FID at all, otherwise it will confuse MDT */
	if ((it->it_op & IT_CREAT) &&
	    !(it->it_flags & MDS_OPEN_BY_FID)) {
		/*
		 * For open with IT_CREATE and for IT_CREATE cases allocate new
		 * fid and setup FLD for it.
		 */
		op_data->op_fid3 = op_data->op_fid2;
		rc = lmv_fid_alloc(exp, &op_data->op_fid2, op_data);
		if (rc != 0)
			return rc;
	}

	CDEBUG(D_INODE, "OPEN_INTENT with fid1=" DFID ", fid2=" DFID ", name='%s' -> mds #%d\n",
	       PFID(&op_data->op_fid1),
	       PFID(&op_data->op_fid2), op_data->op_name, tgt->ltd_idx);

	rc = md_intent_lock(tgt->ltd_exp, op_data, lmm, lmmsize, it, flags,
			    reqp, cb_blocking, extra_lock_flags);
	if (rc != 0)
		return rc;
	/*
	 * Nothing is found, do not access body->fid1 as it is zero and thus
	 * pointless.
	 */
	if ((it->d.lustre.it_disposition & DISP_LOOKUP_NEG) &&
	    !(it->d.lustre.it_disposition & DISP_OPEN_CREATE) &&
	    !(it->d.lustre.it_disposition & DISP_OPEN_OPEN))
		return rc;

	body = req_capsule_server_get(&(*reqp)->rq_pill, &RMF_MDT_BODY);
	if (body == NULL)
		return -EPROTO;
	/*
	 * Not cross-ref case, just get out of here.
	 */
	if (likely(!(body->valid & OBD_MD_MDS)))
		return 0;

	/*
	 * Okay, MDS has returned success. Probably name has been resolved in
	 * remote inode.
	 */
	rc = lmv_intent_remote(exp, lmm, lmmsize, it, &op_data->op_fid1, flags,
			       reqp, cb_blocking, extra_lock_flags);
	if (rc != 0) {
		LASSERT(rc < 0);
		/*
		 * This is possible, that some userspace application will try to
		 * open file as directory and we will have -ENOTDIR here. As
		 * this is normal situation, we should not print error here,
		 * only debug info.
		 */
		CDEBUG(D_INODE, "Can't handle remote %s: dir " DFID "(" DFID "):%*s: %d\n",
		       LL_IT2STR(it), PFID(&op_data->op_fid2),
		       PFID(&op_data->op_fid1), op_data->op_namelen,
		       op_data->op_name, rc);
		return rc;
	}

	return rc;
}

/*
 * Handler for: getattr, lookup and revalidate cases.
 */
int lmv_intent_lookup(struct obd_export *exp, struct md_op_data *op_data,
		      void *lmm, int lmmsize, struct lookup_intent *it,
		      int flags, struct ptlrpc_request **reqp,
		      ldlm_blocking_callback cb_blocking,
		      __u64 extra_lock_flags)
{
	struct obd_device      *obd = exp->exp_obd;
	struct lmv_obd	 *lmv = &obd->u.lmv;
	struct lmv_tgt_desc    *tgt = NULL;
	struct mdt_body	*body;
	int		     rc = 0;

	tgt = lmv_locate_mds(lmv, op_data, &op_data->op_fid1);
	if (IS_ERR(tgt))
		return PTR_ERR(tgt);

	if (!fid_is_sane(&op_data->op_fid2))
		fid_zero(&op_data->op_fid2);

	CDEBUG(D_INODE, "LOOKUP_INTENT with fid1="DFID", fid2="DFID
	       ", name='%s' -> mds #%d\n", PFID(&op_data->op_fid1),
	       PFID(&op_data->op_fid2),
	       op_data->op_name ? op_data->op_name : "<NULL>",
	       tgt->ltd_idx);

	op_data->op_bias &= ~MDS_CROSS_REF;

	rc = md_intent_lock(tgt->ltd_exp, op_data, lmm, lmmsize, it,
			     flags, reqp, cb_blocking, extra_lock_flags);

	if (rc < 0 || *reqp == NULL)
		return rc;

	/*
	 * MDS has returned success. Probably name has been resolved in
	 * remote inode. Let's check this.
	 */
	body = req_capsule_server_get(&(*reqp)->rq_pill, &RMF_MDT_BODY);
	if (body == NULL)
		return -EPROTO;
	/* Not cross-ref case, just get out of here. */
	if (likely(!(body->valid & OBD_MD_MDS)))
		return 0;

	rc = lmv_intent_remote(exp, lmm, lmmsize, it, NULL, flags, reqp,
			       cb_blocking, extra_lock_flags);

	return rc;
}

int lmv_intent_lock(struct obd_export *exp, struct md_op_data *op_data,
		    void *lmm, int lmmsize, struct lookup_intent *it,
		    int flags, struct ptlrpc_request **reqp,
		    ldlm_blocking_callback cb_blocking,
		    __u64 extra_lock_flags)
{
	struct obd_device *obd = exp->exp_obd;
	int		rc;

	LASSERT(it != NULL);
	LASSERT(fid_is_sane(&op_data->op_fid1));

	CDEBUG(D_INODE, "INTENT LOCK '%s' for '%*s' on "DFID"\n",
	       LL_IT2STR(it), op_data->op_namelen, op_data->op_name,
	       PFID(&op_data->op_fid1));

	rc = lmv_check_connect(obd);
	if (rc)
		return rc;

	if (it->it_op & (IT_LOOKUP | IT_GETATTR | IT_LAYOUT))
		rc = lmv_intent_lookup(exp, op_data, lmm, lmmsize, it,
				       flags, reqp, cb_blocking,
				       extra_lock_flags);
	else if (it->it_op & IT_OPEN)
		rc = lmv_intent_open(exp, op_data, lmm, lmmsize, it,
				     flags, reqp, cb_blocking,
				     extra_lock_flags);
	else
		LBUG();
	return rc;
}
