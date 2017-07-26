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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2015, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#define DEBUG_SUBSYSTEM S_LOV

#include "../../include/linux/libcfs/libcfs.h"

#include "../include/obd_class.h"
#include "../include/lustre/lustre_idl.h"
#include "lov_internal.h"

static void lov_init_set(struct lov_request_set *set)
{
	set->set_count = 0;
	atomic_set(&set->set_completes, 0);
	atomic_set(&set->set_success, 0);
	INIT_LIST_HEAD(&set->set_list);
}

static void lov_finish_set(struct lov_request_set *set)
{
	struct list_head *pos, *n;

	LASSERT(set);
	list_for_each_safe(pos, n, &set->set_list) {
		struct lov_request *req = list_entry(pos,
							 struct lov_request,
							 rq_link);
		list_del_init(&req->rq_link);

		kfree(req->rq_oi.oi_osfs);
		kfree(req);
	}
	kfree(set);
}

static void lov_update_set(struct lov_request_set *set,
			   struct lov_request *req, int rc)
{
	atomic_inc(&set->set_completes);
	if (rc == 0)
		atomic_inc(&set->set_success);
}

static void lov_set_add_req(struct lov_request *req,
			    struct lov_request_set *set)
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
static int lov_check_and_wait_active(struct lov_obd *lov, int ost_idx)
{
	wait_queue_head_t waitq;
	struct l_wait_info lwi;
	struct lov_tgt_desc *tgt;
	int rc = 0;

	mutex_lock(&lov->lov_lock);

	tgt = lov->lov_tgts[ost_idx];

	if (unlikely(!tgt)) {
		rc = 0;
		goto out;
	}

	if (likely(tgt->ltd_active)) {
		rc = 1;
		goto out;
	}

	if (tgt->ltd_exp && class_exp2cliimp(tgt->ltd_exp)->imp_connect_tried) {
		rc = 0;
		goto out;
	}

	mutex_unlock(&lov->lov_lock);

	init_waitqueue_head(&waitq);
	lwi = LWI_TIMEOUT_INTERVAL(cfs_time_seconds(obd_timeout),
				   cfs_time_seconds(1), NULL, NULL);

	rc = l_wait_event(waitq, lov_check_set(lov, ost_idx), &lwi);
	if (tgt->ltd_active)
		return 1;

	return 0;

out:
	mutex_unlock(&lov->lov_lock);
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

static int lov_fini_statfs(struct obd_device *obd, struct obd_statfs *osfs,
			   int success)
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

	if (!set)
		return 0;

	if (atomic_read(&set->set_completes)) {
		rc = lov_fini_statfs(set->set_obd, set->set_oi->oi_osfs,
				     atomic_read(&set->set_success));
	}

	lov_finish_set(set);

	return rc;
}

static void lov_update_statfs(struct obd_statfs *osfs,
			      struct obd_statfs *lov_sfs,
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
 * response is received.
 */
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
	 * lovset->set_exp is not initialized.
	 */
	lov_update_set(set, lovreq, rc);
	if (rc)
		goto out;

	obd_getref(lovobd);
	tgt = lov->lov_tgts[lovreq->rq_idx];
	if (!tgt || !tgt->ltd_active)
		goto out_update;

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
	return 0;
}

int lov_prep_statfs_set(struct obd_device *obd, struct obd_info *oinfo,
			struct lov_request_set **reqset)
{
	struct lov_request_set *set;
	struct lov_obd *lov = &obd->u.lov;
	int rc = 0, i;

	set = kzalloc(sizeof(*set), GFP_NOFS);
	if (!set)
		return -ENOMEM;
	lov_init_set(set);

	set->set_obd = obd;
	set->set_oi = oinfo;

	/* We only get block data from the OBD */
	for (i = 0; i < lov->desc.ld_tgt_count; i++) {
		struct lov_request *req;

		if (!lov->lov_tgts[i] ||
		    (oinfo->oi_flags & OBD_STATFS_NODELAY &&
		     !lov->lov_tgts[i]->ltd_active)) {
			CDEBUG(D_HA, "lov idx %d inactive\n", i);
			continue;
		}

		/* skip targets that have been explicitly disabled by the
		 * administrator
		 */
		if (!lov->lov_tgts[i]->ltd_exp) {
			CDEBUG(D_HA, "lov idx %d administratively disabled\n", i);
			continue;
		}

		if (!lov->lov_tgts[i]->ltd_active)
			lov_check_and_wait_active(lov, i);

		req = kzalloc(sizeof(*req), GFP_NOFS);
		if (!req) {
			rc = -ENOMEM;
			goto out_set;
		}

		req->rq_oi.oi_osfs = kzalloc(sizeof(*req->rq_oi.oi_osfs),
					     GFP_NOFS);
		if (!req->rq_oi.oi_osfs) {
			kfree(req);
			rc = -ENOMEM;
			goto out_set;
		}

		req->rq_idx = i;
		req->rq_oi.oi_cb_up = cb_statfs_update;
		req->rq_oi.oi_flags = oinfo->oi_flags;

		lov_set_add_req(req, set);
	}
	if (!set->set_count) {
		rc = -EIO;
		goto out_set;
	}
	*reqset = set;
	return rc;
out_set:
	lov_fini_statfs_set(set);
	return rc;
}
