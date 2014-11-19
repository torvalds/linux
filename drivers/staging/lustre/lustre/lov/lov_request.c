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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#define DEBUG_SUBSYSTEM S_LOV

#include "../../include/linux/libcfs/libcfs.h"

#include "../include/obd_class.h"
#include "../include/obd_ost.h"
#include "../include/lustre/lustre_idl.h"
#include "lov_internal.h"

static void lov_init_set(struct lov_request_set *set)
{
	set->set_count = 0;
	atomic_set(&set->set_completes, 0);
	atomic_set(&set->set_success, 0);
	atomic_set(&set->set_finish_checked, 0);
	set->set_cookies = NULL;
	INIT_LIST_HEAD(&set->set_list);
	atomic_set(&set->set_refcount, 1);
	init_waitqueue_head(&set->set_waitq);
	spin_lock_init(&set->set_lock);
}

void lov_finish_set(struct lov_request_set *set)
{
	struct list_head *pos, *n;

	LASSERT(set);
	list_for_each_safe(pos, n, &set->set_list) {
		struct lov_request *req = list_entry(pos,
							 struct lov_request,
							 rq_link);
		list_del_init(&req->rq_link);

		if (req->rq_oi.oi_oa)
			OBDO_FREE(req->rq_oi.oi_oa);
		if (req->rq_oi.oi_md)
			OBD_FREE_LARGE(req->rq_oi.oi_md, req->rq_buflen);
		if (req->rq_oi.oi_osfs)
			OBD_FREE(req->rq_oi.oi_osfs,
				 sizeof(*req->rq_oi.oi_osfs));
		OBD_FREE(req, sizeof(*req));
	}

	if (set->set_pga) {
		int len = set->set_oabufs * sizeof(*set->set_pga);
		OBD_FREE_LARGE(set->set_pga, len);
	}
	if (set->set_lockh)
		lov_llh_put(set->set_lockh);

	OBD_FREE(set, sizeof(*set));
}

int lov_set_finished(struct lov_request_set *set, int idempotent)
{
	int completes = atomic_read(&set->set_completes);

	CDEBUG(D_INFO, "check set %d/%d\n", completes, set->set_count);

	if (completes == set->set_count) {
		if (idempotent)
			return 1;
		if (atomic_inc_return(&set->set_finish_checked) == 1)
			return 1;
	}
	return 0;
}

void lov_update_set(struct lov_request_set *set,
		    struct lov_request *req, int rc)
{
	req->rq_complete = 1;
	req->rq_rc = rc;

	atomic_inc(&set->set_completes);
	if (rc == 0)
		atomic_inc(&set->set_success);

	wake_up(&set->set_waitq);
}

int lov_update_common_set(struct lov_request_set *set,
			  struct lov_request *req, int rc)
{
	struct lov_obd *lov = &set->set_exp->exp_obd->u.lov;

	lov_update_set(set, req, rc);

	/* grace error on inactive ost */
	if (rc && !(lov->lov_tgts[req->rq_idx] &&
		    lov->lov_tgts[req->rq_idx]->ltd_active))
		rc = 0;

	/* FIXME in raid1 regime, should return 0 */
	return rc;
}

void lov_set_add_req(struct lov_request *req, struct lov_request_set *set)
{
	list_add_tail(&req->rq_link, &set->set_list);
	set->set_count++;
	req->rq_rqset = set;
}

static int lov_check_set(struct lov_obd *lov, int idx)
{
	int rc;
	struct lov_tgt_desc *tgt;

	mutex_lock(&lov->lov_lock);
	tgt = lov->lov_tgts[idx];
	rc = !tgt || tgt->ltd_active ||
		(tgt->ltd_exp &&
		 class_exp2cliimp(tgt->ltd_exp)->imp_connect_tried);
	mutex_unlock(&lov->lov_lock);

	return rc;
}

/* Check if the OSC connection exists and is active.
 * If the OSC has not yet had a chance to connect to the OST the first time,
 * wait once for it to connect instead of returning an error.
 */
int lov_check_and_wait_active(struct lov_obd *lov, int ost_idx)
{
	wait_queue_head_t waitq;
	struct l_wait_info lwi;
	struct lov_tgt_desc *tgt;
	int rc = 0;

	mutex_lock(&lov->lov_lock);

	tgt = lov->lov_tgts[ost_idx];

	if (unlikely(tgt == NULL))
		GOTO(out, rc = 0);

	if (likely(tgt->ltd_active))
		GOTO(out, rc = 1);

	if (tgt->ltd_exp && class_exp2cliimp(tgt->ltd_exp)->imp_connect_tried)
		GOTO(out, rc = 0);

	mutex_unlock(&lov->lov_lock);

	init_waitqueue_head(&waitq);
	lwi = LWI_TIMEOUT_INTERVAL(cfs_time_seconds(obd_timeout),
				   cfs_time_seconds(1), NULL, NULL);

	rc = l_wait_event(waitq, lov_check_set(lov, ost_idx), &lwi);
	if (tgt != NULL && tgt->ltd_active)
		return 1;

	return 0;

out:
	mutex_unlock(&lov->lov_lock);
	return rc;
}

static int lov_update_enqueue_lov(struct obd_export *exp,
				  struct lustre_handle *lov_lockhp,
				  struct lov_oinfo *loi, __u64 flags, int idx,
				  struct ost_id *oi, int rc)
{
	struct lov_obd *lov = &exp->exp_obd->u.lov;

	if (rc != ELDLM_OK &&
	    !(rc == ELDLM_LOCK_ABORTED && (flags & LDLM_FL_HAS_INTENT))) {
		memset(lov_lockhp, 0, sizeof(*lov_lockhp));
		if (lov->lov_tgts[idx] && lov->lov_tgts[idx]->ltd_active) {
			/* -EUSERS used by OST to report file contention */
			if (rc != -EINTR && rc != -EUSERS)
				CERROR("%s: enqueue objid "DOSTID" subobj"
				       DOSTID" on OST idx %d: rc %d\n",
				       exp->exp_obd->obd_name,
				       POSTID(oi), POSTID(&loi->loi_oi),
				       loi->loi_ost_idx, rc);
		} else
			rc = ELDLM_OK;
	}
	return rc;
}

int lov_update_enqueue_set(struct lov_request *req, __u32 mode, int rc)
{
	struct lov_request_set *set = req->rq_rqset;
	struct lustre_handle *lov_lockhp;
	struct obd_info *oi = set->set_oi;
	struct lov_oinfo *loi;

	LASSERT(oi != NULL);

	lov_lockhp = set->set_lockh->llh_handles + req->rq_stripe;
	loi = oi->oi_md->lsm_oinfo[req->rq_stripe];

	/* XXX LOV STACKING: OSC gets a copy, created in lov_prep_enqueue_set
	 * and that copy can be arbitrarily out of date.
	 *
	 * The LOV API is due for a serious rewriting anyways, and this
	 * can be addressed then. */

	lov_stripe_lock(oi->oi_md);
	osc_update_enqueue(lov_lockhp, loi, oi->oi_flags,
			   &req->rq_oi.oi_md->lsm_oinfo[0]->loi_lvb, mode, rc);
	if (rc == ELDLM_LOCK_ABORTED && (oi->oi_flags & LDLM_FL_HAS_INTENT))
		memset(lov_lockhp, 0, sizeof(*lov_lockhp));
	rc = lov_update_enqueue_lov(set->set_exp, lov_lockhp, loi, oi->oi_flags,
				    req->rq_idx, &oi->oi_md->lsm_oi, rc);
	lov_stripe_unlock(oi->oi_md);
	lov_update_set(set, req, rc);
	return rc;
}

/* The callback for osc_enqueue that updates lov info for every OSC request. */
static int cb_update_enqueue(void *cookie, int rc)
{
	struct obd_info *oinfo = cookie;
	struct ldlm_enqueue_info *einfo;
	struct lov_request *lovreq;

	lovreq = container_of(oinfo, struct lov_request, rq_oi);
	einfo = lovreq->rq_rqset->set_ei;
	return lov_update_enqueue_set(lovreq, einfo->ei_mode, rc);
}

static int enqueue_done(struct lov_request_set *set, __u32 mode)
{
	struct lov_request *req;
	struct lov_obd *lov = &set->set_exp->exp_obd->u.lov;
	int completes = atomic_read(&set->set_completes);
	int rc = 0;

	/* enqueue/match success, just return */
	if (completes && completes == atomic_read(&set->set_success))
		return 0;

	/* cancel enqueued/matched locks */
	list_for_each_entry(req, &set->set_list, rq_link) {
		struct lustre_handle *lov_lockhp;

		if (!req->rq_complete || req->rq_rc)
			continue;

		lov_lockhp = set->set_lockh->llh_handles + req->rq_stripe;
		LASSERT(lov_lockhp);
		if (!lustre_handle_is_used(lov_lockhp))
			continue;

		rc = obd_cancel(lov->lov_tgts[req->rq_idx]->ltd_exp,
				req->rq_oi.oi_md, mode, lov_lockhp);
		if (rc && lov->lov_tgts[req->rq_idx] &&
		    lov->lov_tgts[req->rq_idx]->ltd_active)
			CERROR("%s: cancelling obdjid "DOSTID" on OST"
			       "idx %d error: rc = %d\n",
			       set->set_exp->exp_obd->obd_name,
			       POSTID(&req->rq_oi.oi_md->lsm_oi),
			       req->rq_idx, rc);
	}
	if (set->set_lockh)
		lov_llh_put(set->set_lockh);
	return rc;
}

int lov_fini_enqueue_set(struct lov_request_set *set, __u32 mode, int rc,
			 struct ptlrpc_request_set *rqset)
{
	int ret = 0;

	if (set == NULL)
		return 0;
	LASSERT(set->set_exp);
	/* Do enqueue_done only for sync requests and if any request
	 * succeeded. */
	if (!rqset) {
		if (rc)
			atomic_set(&set->set_completes, 0);
		ret = enqueue_done(set, mode);
	} else if (set->set_lockh)
		lov_llh_put(set->set_lockh);

	lov_put_reqset(set);

	return rc ? rc : ret;
}

static void lov_llh_addref(void *llhp)
{
	struct lov_lock_handles *llh = llhp;

	atomic_inc(&llh->llh_refcount);
	CDEBUG(D_INFO, "GETting llh %p : new refcount %d\n", llh,
	       atomic_read(&llh->llh_refcount));
}

static struct portals_handle_ops lov_handle_ops = {
	.hop_addref = lov_llh_addref,
	.hop_free   = NULL,
};

static struct lov_lock_handles *lov_llh_new(struct lov_stripe_md *lsm)
{
	struct lov_lock_handles *llh;

	OBD_ALLOC(llh, sizeof(*llh) +
		  sizeof(*llh->llh_handles) * lsm->lsm_stripe_count);
	if (llh == NULL)
		return NULL;

	atomic_set(&llh->llh_refcount, 2);
	llh->llh_stripe_count = lsm->lsm_stripe_count;
	INIT_LIST_HEAD(&llh->llh_handle.h_link);
	class_handle_hash(&llh->llh_handle, &lov_handle_ops);

	return llh;
}

int lov_prep_enqueue_set(struct obd_export *exp, struct obd_info *oinfo,
			 struct ldlm_enqueue_info *einfo,
			 struct lov_request_set **reqset)
{
	struct lov_obd *lov = &exp->exp_obd->u.lov;
	struct lov_request_set *set;
	int i, rc = 0;

	OBD_ALLOC(set, sizeof(*set));
	if (set == NULL)
		return -ENOMEM;
	lov_init_set(set);

	set->set_exp = exp;
	set->set_oi = oinfo;
	set->set_ei = einfo;
	set->set_lockh = lov_llh_new(oinfo->oi_md);
	if (set->set_lockh == NULL)
		GOTO(out_set, rc = -ENOMEM);
	oinfo->oi_lockh->cookie = set->set_lockh->llh_handle.h_cookie;

	for (i = 0; i < oinfo->oi_md->lsm_stripe_count; i++) {
		struct lov_oinfo *loi;
		struct lov_request *req;
		obd_off start, end;

		loi = oinfo->oi_md->lsm_oinfo[i];
		if (!lov_stripe_intersects(oinfo->oi_md, i,
					   oinfo->oi_policy.l_extent.start,
					   oinfo->oi_policy.l_extent.end,
					   &start, &end))
			continue;

		if (!lov_check_and_wait_active(lov, loi->loi_ost_idx)) {
			CDEBUG(D_HA, "lov idx %d inactive\n", loi->loi_ost_idx);
			continue;
		}

		OBD_ALLOC(req, sizeof(*req));
		if (req == NULL)
			GOTO(out_set, rc = -ENOMEM);

		req->rq_buflen = sizeof(*req->rq_oi.oi_md) +
			sizeof(struct lov_oinfo *) +
			sizeof(struct lov_oinfo);
		OBD_ALLOC_LARGE(req->rq_oi.oi_md, req->rq_buflen);
		if (req->rq_oi.oi_md == NULL) {
			OBD_FREE(req, sizeof(*req));
			GOTO(out_set, rc = -ENOMEM);
		}
		req->rq_oi.oi_md->lsm_oinfo[0] =
			((void *)req->rq_oi.oi_md) + sizeof(*req->rq_oi.oi_md) +
			sizeof(struct lov_oinfo *);

		/* Set lov request specific parameters. */
		req->rq_oi.oi_lockh = set->set_lockh->llh_handles + i;
		req->rq_oi.oi_cb_up = cb_update_enqueue;
		req->rq_oi.oi_flags = oinfo->oi_flags;

		LASSERT(req->rq_oi.oi_lockh);

		req->rq_oi.oi_policy.l_extent.gid =
			oinfo->oi_policy.l_extent.gid;
		req->rq_oi.oi_policy.l_extent.start = start;
		req->rq_oi.oi_policy.l_extent.end = end;

		req->rq_idx = loi->loi_ost_idx;
		req->rq_stripe = i;

		/* XXX LOV STACKING: submd should be from the subobj */
		req->rq_oi.oi_md->lsm_oi = loi->loi_oi;
		req->rq_oi.oi_md->lsm_stripe_count = 0;
		req->rq_oi.oi_md->lsm_oinfo[0]->loi_kms_valid =
			loi->loi_kms_valid;
		req->rq_oi.oi_md->lsm_oinfo[0]->loi_kms = loi->loi_kms;
		req->rq_oi.oi_md->lsm_oinfo[0]->loi_lvb = loi->loi_lvb;

		lov_set_add_req(req, set);
	}
	if (!set->set_count)
		GOTO(out_set, rc = -EIO);
	*reqset = set;
	return 0;
out_set:
	lov_fini_enqueue_set(set, einfo->ei_mode, rc, NULL);
	return rc;
}

int lov_fini_match_set(struct lov_request_set *set, __u32 mode, __u64 flags)
{
	int rc = 0;

	if (set == NULL)
		return 0;
	LASSERT(set->set_exp);
	rc = enqueue_done(set, mode);
	if ((set->set_count == atomic_read(&set->set_success)) &&
	    (flags & LDLM_FL_TEST_LOCK))
		lov_llh_put(set->set_lockh);

	lov_put_reqset(set);

	return rc;
}

int lov_prep_match_set(struct obd_export *exp, struct obd_info *oinfo,
		       struct lov_stripe_md *lsm, ldlm_policy_data_t *policy,
		       __u32 mode, struct lustre_handle *lockh,
		       struct lov_request_set **reqset)
{
	struct lov_obd *lov = &exp->exp_obd->u.lov;
	struct lov_request_set *set;
	int i, rc = 0;

	OBD_ALLOC(set, sizeof(*set));
	if (set == NULL)
		return -ENOMEM;
	lov_init_set(set);

	set->set_exp = exp;
	set->set_oi = oinfo;
	set->set_oi->oi_md = lsm;
	set->set_lockh = lov_llh_new(lsm);
	if (set->set_lockh == NULL)
		GOTO(out_set, rc = -ENOMEM);
	lockh->cookie = set->set_lockh->llh_handle.h_cookie;

	for (i = 0; i < lsm->lsm_stripe_count; i++) {
		struct lov_oinfo *loi;
		struct lov_request *req;
		obd_off start, end;

		loi = lsm->lsm_oinfo[i];
		if (!lov_stripe_intersects(lsm, i, policy->l_extent.start,
					   policy->l_extent.end, &start, &end))
			continue;

		/* FIXME raid1 should grace this error */
		if (!lov_check_and_wait_active(lov, loi->loi_ost_idx)) {
			CDEBUG(D_HA, "lov idx %d inactive\n", loi->loi_ost_idx);
			GOTO(out_set, rc = -EIO);
		}

		OBD_ALLOC(req, sizeof(*req));
		if (req == NULL)
			GOTO(out_set, rc = -ENOMEM);

		req->rq_buflen = sizeof(*req->rq_oi.oi_md);
		OBD_ALLOC_LARGE(req->rq_oi.oi_md, req->rq_buflen);
		if (req->rq_oi.oi_md == NULL) {
			OBD_FREE(req, sizeof(*req));
			GOTO(out_set, rc = -ENOMEM);
		}

		req->rq_oi.oi_policy.l_extent.start = start;
		req->rq_oi.oi_policy.l_extent.end = end;
		req->rq_oi.oi_policy.l_extent.gid = policy->l_extent.gid;

		req->rq_idx = loi->loi_ost_idx;
		req->rq_stripe = i;

		/* XXX LOV STACKING: submd should be from the subobj */
		req->rq_oi.oi_md->lsm_oi = loi->loi_oi;
		req->rq_oi.oi_md->lsm_stripe_count = 0;

		lov_set_add_req(req, set);
	}
	if (!set->set_count)
		GOTO(out_set, rc = -EIO);
	*reqset = set;
	return rc;
out_set:
	lov_fini_match_set(set, mode, 0);
	return rc;
}

int lov_fini_cancel_set(struct lov_request_set *set)
{
	int rc = 0;

	if (set == NULL)
		return 0;

	LASSERT(set->set_exp);
	if (set->set_lockh)
		lov_llh_put(set->set_lockh);

	lov_put_reqset(set);

	return rc;
}

int lov_prep_cancel_set(struct obd_export *exp, struct obd_info *oinfo,
			struct lov_stripe_md *lsm, __u32 mode,
			struct lustre_handle *lockh,
			struct lov_request_set **reqset)
{
	struct lov_request_set *set;
	int i, rc = 0;

	OBD_ALLOC(set, sizeof(*set));
	if (set == NULL)
		return -ENOMEM;
	lov_init_set(set);

	set->set_exp = exp;
	set->set_oi = oinfo;
	set->set_oi->oi_md = lsm;
	set->set_lockh = lov_handle2llh(lockh);
	if (set->set_lockh == NULL) {
		CERROR("LOV: invalid lov lock handle %p\n", lockh);
		GOTO(out_set, rc = -EINVAL);
	}
	lockh->cookie = set->set_lockh->llh_handle.h_cookie;

	for (i = 0; i < lsm->lsm_stripe_count; i++) {
		struct lov_request *req;
		struct lustre_handle *lov_lockhp;
		struct lov_oinfo *loi = lsm->lsm_oinfo[i];

		lov_lockhp = set->set_lockh->llh_handles + i;
		if (!lustre_handle_is_used(lov_lockhp)) {
			CDEBUG(D_INFO, "lov idx %d subobj "DOSTID" no lock\n",
			       loi->loi_ost_idx, POSTID(&loi->loi_oi));
			continue;
		}

		OBD_ALLOC(req, sizeof(*req));
		if (req == NULL)
			GOTO(out_set, rc = -ENOMEM);

		req->rq_buflen = sizeof(*req->rq_oi.oi_md);
		OBD_ALLOC_LARGE(req->rq_oi.oi_md, req->rq_buflen);
		if (req->rq_oi.oi_md == NULL) {
			OBD_FREE(req, sizeof(*req));
			GOTO(out_set, rc = -ENOMEM);
		}

		req->rq_idx = loi->loi_ost_idx;
		req->rq_stripe = i;

		/* XXX LOV STACKING: submd should be from the subobj */
		req->rq_oi.oi_md->lsm_oi = loi->loi_oi;
		req->rq_oi.oi_md->lsm_stripe_count = 0;

		lov_set_add_req(req, set);
	}
	if (!set->set_count)
		GOTO(out_set, rc = -EIO);
	*reqset = set;
	return rc;
out_set:
	lov_fini_cancel_set(set);
	return rc;
}
static int common_attr_done(struct lov_request_set *set)
{
	struct list_head *pos;
	struct lov_request *req;
	struct obdo *tmp_oa;
	int rc = 0, attrset = 0;

	LASSERT(set->set_oi != NULL);

	if (set->set_oi->oi_oa == NULL)
		return 0;

	if (!atomic_read(&set->set_success))
		return -EIO;

	OBDO_ALLOC(tmp_oa);
	if (tmp_oa == NULL)
		GOTO(out, rc = -ENOMEM);

	list_for_each(pos, &set->set_list) {
		req = list_entry(pos, struct lov_request, rq_link);

		if (!req->rq_complete || req->rq_rc)
			continue;
		if (req->rq_oi.oi_oa->o_valid == 0)   /* inactive stripe */
			continue;
		lov_merge_attrs(tmp_oa, req->rq_oi.oi_oa,
				req->rq_oi.oi_oa->o_valid,
				set->set_oi->oi_md, req->rq_stripe, &attrset);
	}
	if (!attrset) {
		CERROR("No stripes had valid attrs\n");
		rc = -EIO;
	}
	if ((set->set_oi->oi_oa->o_valid & OBD_MD_FLEPOCH) &&
	    (set->set_oi->oi_md->lsm_stripe_count != attrset)) {
		/* When we take attributes of some epoch, we require all the
		 * ost to be active. */
		CERROR("Not all the stripes had valid attrs\n");
		GOTO(out, rc = -EIO);
	}

	tmp_oa->o_oi = set->set_oi->oi_oa->o_oi;
	memcpy(set->set_oi->oi_oa, tmp_oa, sizeof(*set->set_oi->oi_oa));
out:
	if (tmp_oa)
		OBDO_FREE(tmp_oa);
	return rc;

}

static int brw_done(struct lov_request_set *set)
{
	struct lov_stripe_md *lsm = set->set_oi->oi_md;
	struct lov_oinfo     *loi = NULL;
	struct list_head *pos;
	struct lov_request *req;

	list_for_each(pos, &set->set_list) {
		req = list_entry(pos, struct lov_request, rq_link);

		if (!req->rq_complete || req->rq_rc)
			continue;

		loi = lsm->lsm_oinfo[req->rq_stripe];

		if (req->rq_oi.oi_oa->o_valid & OBD_MD_FLBLOCKS)
			loi->loi_lvb.lvb_blocks = req->rq_oi.oi_oa->o_blocks;
	}

	return 0;
}

int lov_fini_brw_set(struct lov_request_set *set)
{
	int rc = 0;

	if (set == NULL)
		return 0;
	LASSERT(set->set_exp);
	if (atomic_read(&set->set_completes)) {
		rc = brw_done(set);
		/* FIXME update qos data here */
	}
	lov_put_reqset(set);

	return rc;
}

int lov_prep_brw_set(struct obd_export *exp, struct obd_info *oinfo,
		     obd_count oa_bufs, struct brw_page *pga,
		     struct obd_trans_info *oti,
		     struct lov_request_set **reqset)
{
	struct {
		obd_count       index;
		obd_count       count;
		obd_count       off;
	} *info = NULL;
	struct lov_request_set *set;
	struct lov_obd *lov = &exp->exp_obd->u.lov;
	int rc = 0, i, shift;

	OBD_ALLOC(set, sizeof(*set));
	if (set == NULL)
		return -ENOMEM;
	lov_init_set(set);

	set->set_exp = exp;
	set->set_oti = oti;
	set->set_oi = oinfo;
	set->set_oabufs = oa_bufs;
	OBD_ALLOC_LARGE(set->set_pga, oa_bufs * sizeof(*set->set_pga));
	if (!set->set_pga)
		GOTO(out, rc = -ENOMEM);

	OBD_ALLOC_LARGE(info, sizeof(*info) * oinfo->oi_md->lsm_stripe_count);
	if (!info)
		GOTO(out, rc = -ENOMEM);

	/* calculate the page count for each stripe */
	for (i = 0; i < oa_bufs; i++) {
		int stripe = lov_stripe_number(oinfo->oi_md, pga[i].off);
		info[stripe].count++;
	}

	/* alloc and initialize lov request */
	shift = 0;
	for (i = 0; i < oinfo->oi_md->lsm_stripe_count; i++) {
		struct lov_oinfo *loi = NULL;
		struct lov_request *req;

		if (info[i].count == 0)
			continue;

		loi = oinfo->oi_md->lsm_oinfo[i];
		if (!lov_check_and_wait_active(lov, loi->loi_ost_idx)) {
			CDEBUG(D_HA, "lov idx %d inactive\n", loi->loi_ost_idx);
			GOTO(out, rc = -EIO);
		}

		OBD_ALLOC(req, sizeof(*req));
		if (req == NULL)
			GOTO(out, rc = -ENOMEM);

		OBDO_ALLOC(req->rq_oi.oi_oa);
		if (req->rq_oi.oi_oa == NULL) {
			OBD_FREE(req, sizeof(*req));
			GOTO(out, rc = -ENOMEM);
		}

		if (oinfo->oi_oa) {
			memcpy(req->rq_oi.oi_oa, oinfo->oi_oa,
			       sizeof(*req->rq_oi.oi_oa));
		}
		req->rq_oi.oi_oa->o_oi = loi->loi_oi;
		req->rq_oi.oi_oa->o_stripe_idx = i;

		req->rq_buflen = sizeof(*req->rq_oi.oi_md);
		OBD_ALLOC_LARGE(req->rq_oi.oi_md, req->rq_buflen);
		if (req->rq_oi.oi_md == NULL) {
			OBDO_FREE(req->rq_oi.oi_oa);
			OBD_FREE(req, sizeof(*req));
			GOTO(out, rc = -ENOMEM);
		}

		req->rq_idx = loi->loi_ost_idx;
		req->rq_stripe = i;

		/* XXX LOV STACKING */
		req->rq_oi.oi_md->lsm_oi = loi->loi_oi;
		req->rq_oabufs = info[i].count;
		req->rq_pgaidx = shift;
		shift += req->rq_oabufs;

		/* remember the index for sort brw_page array */
		info[i].index = req->rq_pgaidx;

		req->rq_oi.oi_capa = oinfo->oi_capa;

		lov_set_add_req(req, set);
	}
	if (!set->set_count)
		GOTO(out, rc = -EIO);

	/* rotate & sort the brw_page array */
	for (i = 0; i < oa_bufs; i++) {
		int stripe = lov_stripe_number(oinfo->oi_md, pga[i].off);

		shift = info[stripe].index + info[stripe].off;
		LASSERT(shift < oa_bufs);
		set->set_pga[shift] = pga[i];
		lov_stripe_offset(oinfo->oi_md, pga[i].off, stripe,
				  &set->set_pga[shift].off);
		info[stripe].off++;
	}
out:
	if (info)
		OBD_FREE_LARGE(info,
			       sizeof(*info) * oinfo->oi_md->lsm_stripe_count);

	if (rc == 0)
		*reqset = set;
	else
		lov_fini_brw_set(set);

	return rc;
}

int lov_fini_getattr_set(struct lov_request_set *set)
{
	int rc = 0;

	if (set == NULL)
		return 0;
	LASSERT(set->set_exp);
	if (atomic_read(&set->set_completes))
		rc = common_attr_done(set);

	lov_put_reqset(set);

	return rc;
}

/* The callback for osc_getattr_async that finalizes a request info when a
 * response is received. */
static int cb_getattr_update(void *cookie, int rc)
{
	struct obd_info *oinfo = cookie;
	struct lov_request *lovreq;

	lovreq = container_of(oinfo, struct lov_request, rq_oi);
	return lov_update_common_set(lovreq->rq_rqset, lovreq, rc);
}

int lov_prep_getattr_set(struct obd_export *exp, struct obd_info *oinfo,
			 struct lov_request_set **reqset)
{
	struct lov_request_set *set;
	struct lov_obd *lov = &exp->exp_obd->u.lov;
	int rc = 0, i;

	OBD_ALLOC(set, sizeof(*set));
	if (set == NULL)
		return -ENOMEM;
	lov_init_set(set);

	set->set_exp = exp;
	set->set_oi = oinfo;

	for (i = 0; i < oinfo->oi_md->lsm_stripe_count; i++) {
		struct lov_oinfo *loi;
		struct lov_request *req;

		loi = oinfo->oi_md->lsm_oinfo[i];
		if (!lov_check_and_wait_active(lov, loi->loi_ost_idx)) {
			CDEBUG(D_HA, "lov idx %d inactive\n", loi->loi_ost_idx);
			if (oinfo->oi_oa->o_valid & OBD_MD_FLEPOCH)
				/* SOM requires all the OSTs to be active. */
				GOTO(out_set, rc = -EIO);
			continue;
		}

		OBD_ALLOC(req, sizeof(*req));
		if (req == NULL)
			GOTO(out_set, rc = -ENOMEM);

		req->rq_stripe = i;
		req->rq_idx = loi->loi_ost_idx;

		OBDO_ALLOC(req->rq_oi.oi_oa);
		if (req->rq_oi.oi_oa == NULL) {
			OBD_FREE(req, sizeof(*req));
			GOTO(out_set, rc = -ENOMEM);
		}
		memcpy(req->rq_oi.oi_oa, oinfo->oi_oa,
		       sizeof(*req->rq_oi.oi_oa));
		req->rq_oi.oi_oa->o_oi = loi->loi_oi;
		req->rq_oi.oi_cb_up = cb_getattr_update;
		req->rq_oi.oi_capa = oinfo->oi_capa;

		lov_set_add_req(req, set);
	}
	if (!set->set_count)
		GOTO(out_set, rc = -EIO);
	*reqset = set;
	return rc;
out_set:
	lov_fini_getattr_set(set);
	return rc;
}

int lov_fini_destroy_set(struct lov_request_set *set)
{
	if (set == NULL)
		return 0;
	LASSERT(set->set_exp);
	if (atomic_read(&set->set_completes)) {
		/* FIXME update qos data here */
	}

	lov_put_reqset(set);

	return 0;
}

int lov_prep_destroy_set(struct obd_export *exp, struct obd_info *oinfo,
			 struct obdo *src_oa, struct lov_stripe_md *lsm,
			 struct obd_trans_info *oti,
			 struct lov_request_set **reqset)
{
	struct lov_request_set *set;
	struct lov_obd *lov = &exp->exp_obd->u.lov;
	int rc = 0, i;

	OBD_ALLOC(set, sizeof(*set));
	if (set == NULL)
		return -ENOMEM;
	lov_init_set(set);

	set->set_exp = exp;
	set->set_oi = oinfo;
	set->set_oi->oi_md = lsm;
	set->set_oi->oi_oa = src_oa;
	set->set_oti = oti;
	if (oti != NULL && src_oa->o_valid & OBD_MD_FLCOOKIE)
		set->set_cookies = oti->oti_logcookies;

	for (i = 0; i < lsm->lsm_stripe_count; i++) {
		struct lov_oinfo *loi;
		struct lov_request *req;

		loi = lsm->lsm_oinfo[i];
		if (!lov_check_and_wait_active(lov, loi->loi_ost_idx)) {
			CDEBUG(D_HA, "lov idx %d inactive\n", loi->loi_ost_idx);
			continue;
		}

		OBD_ALLOC(req, sizeof(*req));
		if (req == NULL)
			GOTO(out_set, rc = -ENOMEM);

		req->rq_stripe = i;
		req->rq_idx = loi->loi_ost_idx;

		OBDO_ALLOC(req->rq_oi.oi_oa);
		if (req->rq_oi.oi_oa == NULL) {
			OBD_FREE(req, sizeof(*req));
			GOTO(out_set, rc = -ENOMEM);
		}
		memcpy(req->rq_oi.oi_oa, src_oa, sizeof(*req->rq_oi.oi_oa));
		req->rq_oi.oi_oa->o_oi = loi->loi_oi;
		lov_set_add_req(req, set);
	}
	if (!set->set_count)
		GOTO(out_set, rc = -EIO);
	*reqset = set;
	return rc;
out_set:
	lov_fini_destroy_set(set);
	return rc;
}

int lov_fini_setattr_set(struct lov_request_set *set)
{
	int rc = 0;

	if (set == NULL)
		return 0;
	LASSERT(set->set_exp);
	if (atomic_read(&set->set_completes)) {
		rc = common_attr_done(set);
		/* FIXME update qos data here */
	}

	lov_put_reqset(set);
	return rc;
}

int lov_update_setattr_set(struct lov_request_set *set,
			   struct lov_request *req, int rc)
{
	struct lov_obd *lov = &req->rq_rqset->set_exp->exp_obd->u.lov;
	struct lov_stripe_md *lsm = req->rq_rqset->set_oi->oi_md;

	lov_update_set(set, req, rc);

	/* grace error on inactive ost */
	if (rc && !(lov->lov_tgts[req->rq_idx] &&
		    lov->lov_tgts[req->rq_idx]->ltd_active))
		rc = 0;

	if (rc == 0) {
		if (req->rq_oi.oi_oa->o_valid & OBD_MD_FLCTIME)
			lsm->lsm_oinfo[req->rq_stripe]->loi_lvb.lvb_ctime =
				req->rq_oi.oi_oa->o_ctime;
		if (req->rq_oi.oi_oa->o_valid & OBD_MD_FLMTIME)
			lsm->lsm_oinfo[req->rq_stripe]->loi_lvb.lvb_mtime =
				req->rq_oi.oi_oa->o_mtime;
		if (req->rq_oi.oi_oa->o_valid & OBD_MD_FLATIME)
			lsm->lsm_oinfo[req->rq_stripe]->loi_lvb.lvb_atime =
				req->rq_oi.oi_oa->o_atime;
	}

	return rc;
}

/* The callback for osc_setattr_async that finalizes a request info when a
 * response is received. */
static int cb_setattr_update(void *cookie, int rc)
{
	struct obd_info *oinfo = cookie;
	struct lov_request *lovreq;

	lovreq = container_of(oinfo, struct lov_request, rq_oi);
	return lov_update_setattr_set(lovreq->rq_rqset, lovreq, rc);
}

int lov_prep_setattr_set(struct obd_export *exp, struct obd_info *oinfo,
			 struct obd_trans_info *oti,
			 struct lov_request_set **reqset)
{
	struct lov_request_set *set;
	struct lov_obd *lov = &exp->exp_obd->u.lov;
	int rc = 0, i;

	OBD_ALLOC(set, sizeof(*set));
	if (set == NULL)
		return -ENOMEM;
	lov_init_set(set);

	set->set_exp = exp;
	set->set_oti = oti;
	set->set_oi = oinfo;
	if (oti != NULL && oinfo->oi_oa->o_valid & OBD_MD_FLCOOKIE)
		set->set_cookies = oti->oti_logcookies;

	for (i = 0; i < oinfo->oi_md->lsm_stripe_count; i++) {
		struct lov_oinfo *loi = oinfo->oi_md->lsm_oinfo[i];
		struct lov_request *req;

		if (!lov_check_and_wait_active(lov, loi->loi_ost_idx)) {
			CDEBUG(D_HA, "lov idx %d inactive\n", loi->loi_ost_idx);
			continue;
		}

		OBD_ALLOC(req, sizeof(*req));
		if (req == NULL)
			GOTO(out_set, rc = -ENOMEM);
		req->rq_stripe = i;
		req->rq_idx = loi->loi_ost_idx;

		OBDO_ALLOC(req->rq_oi.oi_oa);
		if (req->rq_oi.oi_oa == NULL) {
			OBD_FREE(req, sizeof(*req));
			GOTO(out_set, rc = -ENOMEM);
		}
		memcpy(req->rq_oi.oi_oa, oinfo->oi_oa,
		       sizeof(*req->rq_oi.oi_oa));
		req->rq_oi.oi_oa->o_oi = loi->loi_oi;
		req->rq_oi.oi_oa->o_stripe_idx = i;
		req->rq_oi.oi_cb_up = cb_setattr_update;
		req->rq_oi.oi_capa = oinfo->oi_capa;

		if (oinfo->oi_oa->o_valid & OBD_MD_FLSIZE) {
			int off = lov_stripe_offset(oinfo->oi_md,
						    oinfo->oi_oa->o_size, i,
						    &req->rq_oi.oi_oa->o_size);

			if (off < 0 && req->rq_oi.oi_oa->o_size)
				req->rq_oi.oi_oa->o_size--;

			CDEBUG(D_INODE, "stripe %d has size %llu/%llu\n",
			       i, req->rq_oi.oi_oa->o_size,
			       oinfo->oi_oa->o_size);
		}
		lov_set_add_req(req, set);
	}
	if (!set->set_count)
		GOTO(out_set, rc = -EIO);
	*reqset = set;
	return rc;
out_set:
	lov_fini_setattr_set(set);
	return rc;
}

int lov_fini_punch_set(struct lov_request_set *set)
{
	int rc = 0;

	if (set == NULL)
		return 0;
	LASSERT(set->set_exp);
	if (atomic_read(&set->set_completes)) {
		rc = -EIO;
		/* FIXME update qos data here */
		if (atomic_read(&set->set_success))
			rc = common_attr_done(set);
	}

	lov_put_reqset(set);

	return rc;
}

int lov_update_punch_set(struct lov_request_set *set,
			 struct lov_request *req, int rc)
{
	struct lov_obd *lov = &req->rq_rqset->set_exp->exp_obd->u.lov;
	struct lov_stripe_md *lsm = req->rq_rqset->set_oi->oi_md;

	lov_update_set(set, req, rc);

	/* grace error on inactive ost */
	if (rc && !lov->lov_tgts[req->rq_idx]->ltd_active)
		rc = 0;

	if (rc == 0) {
		lov_stripe_lock(lsm);
		if (req->rq_oi.oi_oa->o_valid & OBD_MD_FLBLOCKS) {
			lsm->lsm_oinfo[req->rq_stripe]->loi_lvb.lvb_blocks =
				req->rq_oi.oi_oa->o_blocks;
		}

		lov_stripe_unlock(lsm);
	}

	return rc;
}

/* The callback for osc_punch that finalizes a request info when a response
 * is received. */
static int cb_update_punch(void *cookie, int rc)
{
	struct obd_info *oinfo = cookie;
	struct lov_request *lovreq;

	lovreq = container_of(oinfo, struct lov_request, rq_oi);
	return lov_update_punch_set(lovreq->rq_rqset, lovreq, rc);
}

int lov_prep_punch_set(struct obd_export *exp, struct obd_info *oinfo,
		       struct obd_trans_info *oti,
		       struct lov_request_set **reqset)
{
	struct lov_request_set *set;
	struct lov_obd *lov = &exp->exp_obd->u.lov;
	int rc = 0, i;

	OBD_ALLOC(set, sizeof(*set));
	if (set == NULL)
		return -ENOMEM;
	lov_init_set(set);

	set->set_oi = oinfo;
	set->set_exp = exp;

	for (i = 0; i < oinfo->oi_md->lsm_stripe_count; i++) {
		struct lov_oinfo *loi = oinfo->oi_md->lsm_oinfo[i];
		struct lov_request *req;
		obd_off rs, re;

		if (!lov_stripe_intersects(oinfo->oi_md, i,
					   oinfo->oi_policy.l_extent.start,
					   oinfo->oi_policy.l_extent.end,
					   &rs, &re))
			continue;

		if (!lov_check_and_wait_active(lov, loi->loi_ost_idx)) {
			CDEBUG(D_HA, "lov idx %d inactive\n", loi->loi_ost_idx);
			GOTO(out_set, rc = -EIO);
		}

		OBD_ALLOC(req, sizeof(*req));
		if (req == NULL)
			GOTO(out_set, rc = -ENOMEM);
		req->rq_stripe = i;
		req->rq_idx = loi->loi_ost_idx;

		OBDO_ALLOC(req->rq_oi.oi_oa);
		if (req->rq_oi.oi_oa == NULL) {
			OBD_FREE(req, sizeof(*req));
			GOTO(out_set, rc = -ENOMEM);
		}
		memcpy(req->rq_oi.oi_oa, oinfo->oi_oa,
		       sizeof(*req->rq_oi.oi_oa));
		req->rq_oi.oi_oa->o_oi = loi->loi_oi;
		req->rq_oi.oi_oa->o_valid |= OBD_MD_FLGROUP;

		req->rq_oi.oi_oa->o_stripe_idx = i;
		req->rq_oi.oi_cb_up = cb_update_punch;

		req->rq_oi.oi_policy.l_extent.start = rs;
		req->rq_oi.oi_policy.l_extent.end = re;
		req->rq_oi.oi_policy.l_extent.gid = -1;

		req->rq_oi.oi_capa = oinfo->oi_capa;

		lov_set_add_req(req, set);
	}
	if (!set->set_count)
		GOTO(out_set, rc = -EIO);
	*reqset = set;
	return rc;
out_set:
	lov_fini_punch_set(set);
	return rc;
}

int lov_fini_sync_set(struct lov_request_set *set)
{
	int rc = 0;

	if (set == NULL)
		return 0;
	LASSERT(set->set_exp);
	if (atomic_read(&set->set_completes)) {
		if (!atomic_read(&set->set_success))
			rc = -EIO;
		/* FIXME update qos data here */
	}

	lov_put_reqset(set);

	return rc;
}

/* The callback for osc_sync that finalizes a request info when a
 * response is received. */
static int cb_sync_update(void *cookie, int rc)
{
	struct obd_info *oinfo = cookie;
	struct lov_request *lovreq;

	lovreq = container_of(oinfo, struct lov_request, rq_oi);
	return lov_update_common_set(lovreq->rq_rqset, lovreq, rc);
}

int lov_prep_sync_set(struct obd_export *exp, struct obd_info *oinfo,
		      obd_off start, obd_off end,
		      struct lov_request_set **reqset)
{
	struct lov_request_set *set;
	struct lov_obd *lov = &exp->exp_obd->u.lov;
	int rc = 0, i;

	OBD_ALLOC_PTR(set);
	if (set == NULL)
		return -ENOMEM;
	lov_init_set(set);

	set->set_exp = exp;
	set->set_oi = oinfo;

	for (i = 0; i < oinfo->oi_md->lsm_stripe_count; i++) {
		struct lov_oinfo *loi = oinfo->oi_md->lsm_oinfo[i];
		struct lov_request *req;
		obd_off rs, re;

		if (!lov_check_and_wait_active(lov, loi->loi_ost_idx)) {
			CDEBUG(D_HA, "lov idx %d inactive\n", loi->loi_ost_idx);
			continue;
		}

		if (!lov_stripe_intersects(oinfo->oi_md, i, start, end, &rs,
					   &re))
			continue;

		OBD_ALLOC_PTR(req);
		if (req == NULL)
			GOTO(out_set, rc = -ENOMEM);
		req->rq_stripe = i;
		req->rq_idx = loi->loi_ost_idx;

		OBDO_ALLOC(req->rq_oi.oi_oa);
		if (req->rq_oi.oi_oa == NULL) {
			OBD_FREE(req, sizeof(*req));
			GOTO(out_set, rc = -ENOMEM);
		}
		*req->rq_oi.oi_oa = *oinfo->oi_oa;
		req->rq_oi.oi_oa->o_oi = loi->loi_oi;
		req->rq_oi.oi_oa->o_stripe_idx = i;

		req->rq_oi.oi_policy.l_extent.start = rs;
		req->rq_oi.oi_policy.l_extent.end = re;
		req->rq_oi.oi_policy.l_extent.gid = -1;
		req->rq_oi.oi_cb_up = cb_sync_update;

		lov_set_add_req(req, set);
	}
	if (!set->set_count)
		GOTO(out_set, rc = -EIO);
	*reqset = set;
	return rc;
out_set:
	lov_fini_sync_set(set);
	return rc;
}

#define LOV_U64_MAX ((__u64)~0ULL)
#define LOV_SUM_MAX(tot, add)					   \
	do {							    \
		if ((tot) + (add) < (tot))			      \
			(tot) = LOV_U64_MAX;			    \
		else						    \
			(tot) += (add);				 \
	} while (0)

int lov_fini_statfs(struct obd_device *obd, struct obd_statfs *osfs,int success)
{
	if (success) {
		__u32 expected_stripes = lov_get_stripecnt(&obd->u.lov,
							   LOV_MAGIC, 0);
		if (osfs->os_files != LOV_U64_MAX)
			lov_do_div64(osfs->os_files, expected_stripes);
		if (osfs->os_ffree != LOV_U64_MAX)
			lov_do_div64(osfs->os_ffree, expected_stripes);

		spin_lock(&obd->obd_osfs_lock);
		memcpy(&obd->obd_osfs, osfs, sizeof(*osfs));
		obd->obd_osfs_age = cfs_time_current_64();
		spin_unlock(&obd->obd_osfs_lock);
		return 0;
	}

	return -EIO;
}

int lov_fini_statfs_set(struct lov_request_set *set)
{
	int rc = 0;

	if (set == NULL)
		return 0;

	if (atomic_read(&set->set_completes)) {
		rc = lov_fini_statfs(set->set_obd, set->set_oi->oi_osfs,
				     atomic_read(&set->set_success));
	}
	lov_put_reqset(set);
	return rc;
}

void lov_update_statfs(struct obd_statfs *osfs, struct obd_statfs *lov_sfs,
		       int success)
{
	int shift = 0, quit = 0;
	__u64 tmp;

	if (success == 0) {
		memcpy(osfs, lov_sfs, sizeof(*lov_sfs));
	} else {
		if (osfs->os_bsize != lov_sfs->os_bsize) {
			/* assume all block sizes are always powers of 2 */
			/* get the bits difference */
			tmp = osfs->os_bsize | lov_sfs->os_bsize;
			for (shift = 0; shift <= 64; ++shift) {
				if (tmp & 1) {
					if (quit)
						break;
					else
						quit = 1;
					shift = 0;
				}
				tmp >>= 1;
			}
		}

		if (osfs->os_bsize < lov_sfs->os_bsize) {
			osfs->os_bsize = lov_sfs->os_bsize;

			osfs->os_bfree  >>= shift;
			osfs->os_bavail >>= shift;
			osfs->os_blocks >>= shift;
		} else if (shift != 0) {
			lov_sfs->os_bfree  >>= shift;
			lov_sfs->os_bavail >>= shift;
			lov_sfs->os_blocks >>= shift;
		}
		osfs->os_bfree += lov_sfs->os_bfree;
		osfs->os_bavail += lov_sfs->os_bavail;
		osfs->os_blocks += lov_sfs->os_blocks;
		/* XXX not sure about this one - depends on policy.
		 *   - could be minimum if we always stripe on all OBDs
		 *     (but that would be wrong for any other policy,
		 *     if one of the OBDs has no more objects left)
		 *   - could be sum if we stripe whole objects
		 *   - could be average, just to give a nice number
		 *
		 * To give a "reasonable" (if not wholly accurate)
		 * number, we divide the total number of free objects
		 * by expected stripe count (watch out for overflow).
		 */
		LOV_SUM_MAX(osfs->os_files, lov_sfs->os_files);
		LOV_SUM_MAX(osfs->os_ffree, lov_sfs->os_ffree);
	}
}

/* The callback for osc_statfs_async that finalizes a request info when a
 * response is received. */
static int cb_statfs_update(void *cookie, int rc)
{
	struct obd_info *oinfo = cookie;
	struct lov_request *lovreq;
	struct lov_request_set *set;
	struct obd_statfs *osfs, *lov_sfs;
	struct lov_obd *lov;
	struct lov_tgt_desc *tgt;
	struct obd_device *lovobd, *tgtobd;
	int success;

	lovreq = container_of(oinfo, struct lov_request, rq_oi);
	set = lovreq->rq_rqset;
	lovobd = set->set_obd;
	lov = &lovobd->u.lov;
	osfs = set->set_oi->oi_osfs;
	lov_sfs = oinfo->oi_osfs;
	success = atomic_read(&set->set_success);
	/* XXX: the same is done in lov_update_common_set, however
	   lovset->set_exp is not initialized. */
	lov_update_set(set, lovreq, rc);
	if (rc)
		GOTO(out, rc);

	obd_getref(lovobd);
	tgt = lov->lov_tgts[lovreq->rq_idx];
	if (!tgt || !tgt->ltd_active)
		GOTO(out_update, rc);

	tgtobd = class_exp2obd(tgt->ltd_exp);
	spin_lock(&tgtobd->obd_osfs_lock);
	memcpy(&tgtobd->obd_osfs, lov_sfs, sizeof(*lov_sfs));
	if ((oinfo->oi_flags & OBD_STATFS_FROM_CACHE) == 0)
		tgtobd->obd_osfs_age = cfs_time_current_64();
	spin_unlock(&tgtobd->obd_osfs_lock);

out_update:
	lov_update_statfs(osfs, lov_sfs, success);
	obd_putref(lovobd);

out:
	if (set->set_oi->oi_flags & OBD_STATFS_PTLRPCD &&
	    lov_set_finished(set, 0)) {
		lov_statfs_interpret(NULL, set, set->set_count !=
				     atomic_read(&set->set_success));
	}

	return 0;
}

int lov_prep_statfs_set(struct obd_device *obd, struct obd_info *oinfo,
			struct lov_request_set **reqset)
{
	struct lov_request_set *set;
	struct lov_obd *lov = &obd->u.lov;
	int rc = 0, i;

	OBD_ALLOC(set, sizeof(*set));
	if (set == NULL)
		return -ENOMEM;
	lov_init_set(set);

	set->set_obd = obd;
	set->set_oi = oinfo;

	/* We only get block data from the OBD */
	for (i = 0; i < lov->desc.ld_tgt_count; i++) {
		struct lov_request *req;

		if (lov->lov_tgts[i] == NULL ||
		    (!lov_check_and_wait_active(lov, i) &&
		     (oinfo->oi_flags & OBD_STATFS_NODELAY))) {
			CDEBUG(D_HA, "lov idx %d inactive\n", i);
			continue;
		}

		/* skip targets that have been explicitly disabled by the
		 * administrator */
		if (!lov->lov_tgts[i]->ltd_exp) {
			CDEBUG(D_HA, "lov idx %d administratively disabled\n", i);
			continue;
		}

		OBD_ALLOC(req, sizeof(*req));
		if (req == NULL)
			GOTO(out_set, rc = -ENOMEM);

		OBD_ALLOC(req->rq_oi.oi_osfs, sizeof(*req->rq_oi.oi_osfs));
		if (req->rq_oi.oi_osfs == NULL) {
			OBD_FREE(req, sizeof(*req));
			GOTO(out_set, rc = -ENOMEM);
		}

		req->rq_idx = i;
		req->rq_oi.oi_cb_up = cb_statfs_update;
		req->rq_oi.oi_flags = oinfo->oi_flags;

		lov_set_add_req(req, set);
	}
	if (!set->set_count)
		GOTO(out_set, rc = -EIO);
	*reqset = set;
	return rc;
out_set:
	lov_fini_statfs_set(set);
	return rc;
}
