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
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/lov/lov_log.c
 *
 * Author: Phil Schwan <phil@clusterfs.com>
 * Author: Peter Braam <braam@clusterfs.com>
 * Author: Mike Shaver <shaver@clusterfs.com>
 */

#define DEBUG_SUBSYSTEM S_LOV
#include <linux/libcfs/libcfs.h>

#include <obd_support.h>
#include <lustre_lib.h>
#include <lustre_net.h>
#include <lustre/lustre_idl.h>
#include <lustre_dlm.h>
#include <lustre_mds.h>
#include <obd_class.h>
#include <obd_lov.h>
#include <obd_ost.h>
#include <lprocfs_status.h>
#include <lustre_log.h>

#include "lov_internal.h"

/* Add log records for each OSC that this object is striped over, and return
 * cookies for each one.  We _would_ have nice abstraction here, except that
 * we need to keep cookies in stripe order, even if some are NULL, so that
 * the right cookies are passed back to the right OSTs at the client side.
 * Unset cookies should be all-zero (which will never occur naturally). */
static int lov_llog_origin_add(const struct lu_env *env,
			       struct llog_ctxt *ctxt,
			       struct llog_rec_hdr *rec,
			       struct lov_stripe_md *lsm,
			       struct llog_cookie *logcookies, int numcookies)
{
	struct obd_device *obd = ctxt->loc_obd;
	struct lov_obd *lov = &obd->u.lov;
	int i, rc = 0, cookies = 0;
	ENTRY;

	LASSERTF(logcookies && numcookies >= lsm->lsm_stripe_count,
		 "logcookies %p, numcookies %d lsm->lsm_stripe_count %d \n",
		 logcookies, numcookies, lsm->lsm_stripe_count);

	for (i = 0; i < lsm->lsm_stripe_count; i++) {
		struct lov_oinfo *loi = lsm->lsm_oinfo[i];
		struct obd_device *child =
			lov->lov_tgts[loi->loi_ost_idx]->ltd_exp->exp_obd;
		struct llog_ctxt *cctxt = llog_get_context(child, ctxt->loc_idx);

		/* fill mds unlink/setattr log record */
		switch (rec->lrh_type) {
		case MDS_UNLINK_REC: {
			struct llog_unlink_rec *lur = (struct llog_unlink_rec *)rec;
			lur->lur_oid = ostid_id(&loi->loi_oi);
			lur->lur_oseq = (__u32)ostid_seq(&loi->loi_oi);
			break;
		}
		case MDS_SETATTR64_REC: {
			struct llog_setattr64_rec *lsr = (struct llog_setattr64_rec *)rec;
			lsr->lsr_oi = loi->loi_oi;
			break;
		}
		default:
			break;
		}

		/* inject error in llog_obd_add() below */
		if (OBD_FAIL_CHECK(OBD_FAIL_MDS_FAIL_LOV_LOG_ADD)) {
			llog_ctxt_put(cctxt);
			cctxt = NULL;
		}
		rc = llog_obd_add(env, cctxt, rec, NULL, logcookies + cookies,
				  numcookies - cookies);
		llog_ctxt_put(cctxt);
		if (rc < 0) {
			CERROR("Can't add llog (rc = %d) for stripe %d\n",
			       rc, cookies);
			memset(logcookies + cookies, 0,
			       sizeof(struct llog_cookie));
			rc = 1; /* skip this cookie */
		}
		/* Note that rc is always 1 if llog_obd_add was successful */
		cookies += rc;
	}
	RETURN(cookies);
}

static int lov_llog_origin_connect(struct llog_ctxt *ctxt,
				   struct llog_logid *logid,
				   struct llog_gen *gen,
				   struct obd_uuid *uuid)
{
	struct obd_device *obd = ctxt->loc_obd;
	struct lov_obd *lov = &obd->u.lov;
	int i, rc = 0, err = 0;
	ENTRY;

	obd_getref(obd);
	for (i = 0; i < lov->desc.ld_tgt_count; i++) {
		struct obd_device *child;
		struct llog_ctxt *cctxt;

		if (!lov->lov_tgts[i] || !lov->lov_tgts[i]->ltd_active)
			continue;
		if (uuid && !obd_uuid_equals(uuid, &lov->lov_tgts[i]->ltd_uuid))
			continue;
		CDEBUG(D_CONFIG, "connect %d/%d\n", i, lov->desc.ld_tgt_count);
		child = lov->lov_tgts[i]->ltd_exp->exp_obd;
		cctxt = llog_get_context(child, ctxt->loc_idx);
		rc = llog_connect(cctxt, logid, gen, uuid);
		llog_ctxt_put(cctxt);

		if (rc) {
			CERROR("error osc_llog_connect tgt %d (%d)\n", i, rc);
			if (!err)
				err = rc;
		}
	}
	obd_putref(obd);

	RETURN(err);
}

/* the replicators commit callback */
static int lov_llog_repl_cancel(const struct lu_env *env,
				struct llog_ctxt *ctxt,
				struct lov_stripe_md *lsm,
				int count, struct llog_cookie *cookies,
				int flags)
{
	struct lov_obd *lov;
	struct obd_device *obd = ctxt->loc_obd;
	int rc = 0, i;
	ENTRY;

	LASSERT(lsm != NULL);
	LASSERT(count == lsm->lsm_stripe_count);

	lov = &obd->u.lov;
	obd_getref(obd);
	for (i = 0; i < count; i++, cookies++) {
		struct lov_oinfo *loi = lsm->lsm_oinfo[i];
		struct obd_device *child =
			lov->lov_tgts[loi->loi_ost_idx]->ltd_exp->exp_obd;
		struct llog_ctxt *cctxt =
			llog_get_context(child, ctxt->loc_idx);
		int err;

		err = llog_cancel(env, cctxt, NULL, 1, cookies, flags);
		llog_ctxt_put(cctxt);
		if (err && lov->lov_tgts[loi->loi_ost_idx]->ltd_active) {
			CERROR("%s: objid "DOSTID" subobj "DOSTID
			       " on OST idx %d: rc = %d\n",
			       obd->obd_name, POSTID(&lsm->lsm_oi),
			       POSTID(&loi->loi_oi), loi->loi_ost_idx, err);
			if (!rc)
				rc = err;
		}
	}
	obd_putref(obd);
	RETURN(rc);
}

static struct llog_operations lov_mds_ost_orig_logops = {
	.lop_obd_add	= lov_llog_origin_add,
	.lop_connect	= lov_llog_origin_connect,
};

static struct llog_operations lov_size_repl_logops = {
	.lop_cancel	= lov_llog_repl_cancel,
};

int lov_llog_init(struct obd_device *obd, struct obd_llog_group *olg,
		  struct obd_device *disk_obd, int *index)
{
	struct lov_obd *lov = &obd->u.lov;
	struct obd_device *child;
	int i, rc = 0;
	ENTRY;

	LASSERT(olg == &obd->obd_olg);
	rc = llog_setup(NULL, obd, olg, LLOG_MDS_OST_ORIG_CTXT, disk_obd,
			&lov_mds_ost_orig_logops);
	if (rc)
		RETURN(rc);

	rc = llog_setup(NULL, obd, olg, LLOG_SIZE_REPL_CTXT, disk_obd,
			&lov_size_repl_logops);
	if (rc)
		GOTO(err_cleanup, rc);

	obd_getref(obd);
	/* count may not match lov->desc.ld_tgt_count during dynamic ost add */
	for (i = 0; i < lov->desc.ld_tgt_count; i++) {
		if (!lov->lov_tgts[i])
			continue;

		if (index && i != *index)
			continue;

		child = lov->lov_tgts[i]->ltd_obd;
		rc = obd_llog_init(child, &child->obd_olg, disk_obd, &i);
		if (rc)
			CERROR("error osc_llog_init idx %d osc '%s' tgt '%s' "
			       "(rc=%d)\n", i, child->obd_name,
			       disk_obd->obd_name, rc);
		rc = 0;
	}
	obd_putref(obd);
	GOTO(err_cleanup, rc);
err_cleanup:
	if (rc) {
		struct llog_ctxt *ctxt =
			llog_get_context(obd, LLOG_SIZE_REPL_CTXT);
		if (ctxt)
			llog_cleanup(NULL, ctxt);
		ctxt = llog_get_context(obd, LLOG_MDS_OST_ORIG_CTXT);
		if (ctxt)
			llog_cleanup(NULL, ctxt);
	}
	return rc;
}

int lov_llog_finish(struct obd_device *obd, int count)
{
	struct llog_ctxt *ctxt;

	ENTRY;

	/* cleanup our llogs only if the ctxts have been setup
	 * (client lov doesn't setup, mds lov does). */
	ctxt = llog_get_context(obd, LLOG_MDS_OST_ORIG_CTXT);
	if (ctxt)
		llog_cleanup(NULL, ctxt);

	ctxt = llog_get_context(obd, LLOG_SIZE_REPL_CTXT);
	if (ctxt)
		llog_cleanup(NULL, ctxt);

	/* lov->tgt llogs are cleaned during osc_cleanup. */
	RETURN(0);
}
