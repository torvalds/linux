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
 * Copyright (c) 2002, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#define DEBUG_SUBSYSTEM S_OSC

#include "../../include/linux/libcfs/libcfs.h"


#include "../include/lustre_dlm.h"
#include "../include/lustre_net.h"
#include "../include/lustre/lustre_user.h"
#include "../include/obd_cksum.h"
#include "../include/obd_ost.h"

#include "../include/lustre_ha.h"
#include "../include/lprocfs_status.h"
#include "../include/lustre_log.h"
#include "../include/lustre_debug.h"
#include "../include/lustre_param.h"
#include "../include/lustre_fid.h"
#include "osc_internal.h"
#include "osc_cl_internal.h"

static void osc_release_ppga(struct brw_page **ppga, obd_count count);
static int brw_interpret(const struct lu_env *env,
			 struct ptlrpc_request *req, void *data, int rc);
int osc_cleanup(struct obd_device *obd);

/* Pack OSC object metadata for disk storage (LE byte order). */
static int osc_packmd(struct obd_export *exp, struct lov_mds_md **lmmp,
		      struct lov_stripe_md *lsm)
{
	int lmm_size;

	lmm_size = sizeof(**lmmp);
	if (lmmp == NULL)
		return lmm_size;

	if (*lmmp != NULL && lsm == NULL) {
		OBD_FREE(*lmmp, lmm_size);
		*lmmp = NULL;
		return 0;
	} else if (unlikely(lsm != NULL && ostid_id(&lsm->lsm_oi) == 0)) {
		return -EBADF;
	}

	if (*lmmp == NULL) {
		OBD_ALLOC(*lmmp, lmm_size);
		if (*lmmp == NULL)
			return -ENOMEM;
	}

	if (lsm)
		ostid_cpu_to_le(&lsm->lsm_oi, &(*lmmp)->lmm_oi);

	return lmm_size;
}

/* Unpack OSC object metadata from disk storage (LE byte order). */
static int osc_unpackmd(struct obd_export *exp, struct lov_stripe_md **lsmp,
			struct lov_mds_md *lmm, int lmm_bytes)
{
	int lsm_size;
	struct obd_import *imp = class_exp2cliimp(exp);

	if (lmm != NULL) {
		if (lmm_bytes < sizeof(*lmm)) {
			CERROR("%s: lov_mds_md too small: %d, need %d\n",
			       exp->exp_obd->obd_name, lmm_bytes,
			       (int)sizeof(*lmm));
			return -EINVAL;
		}
		/* XXX LOV_MAGIC etc check? */

		if (unlikely(ostid_id(&lmm->lmm_oi) == 0)) {
			CERROR("%s: zero lmm_object_id: rc = %d\n",
			       exp->exp_obd->obd_name, -EINVAL);
			return -EINVAL;
		}
	}

	lsm_size = lov_stripe_md_size(1);
	if (lsmp == NULL)
		return lsm_size;

	if (*lsmp != NULL && lmm == NULL) {
		OBD_FREE((*lsmp)->lsm_oinfo[0], sizeof(struct lov_oinfo));
		OBD_FREE(*lsmp, lsm_size);
		*lsmp = NULL;
		return 0;
	}

	if (*lsmp == NULL) {
		OBD_ALLOC(*lsmp, lsm_size);
		if (unlikely(*lsmp == NULL))
			return -ENOMEM;
		OBD_ALLOC((*lsmp)->lsm_oinfo[0], sizeof(struct lov_oinfo));
		if (unlikely((*lsmp)->lsm_oinfo[0] == NULL)) {
			OBD_FREE(*lsmp, lsm_size);
			return -ENOMEM;
		}
		loi_init((*lsmp)->lsm_oinfo[0]);
	} else if (unlikely(ostid_id(&(*lsmp)->lsm_oi) == 0)) {
		return -EBADF;
	}

	if (lmm != NULL)
		/* XXX zero *lsmp? */
		ostid_le_to_cpu(&lmm->lmm_oi, &(*lsmp)->lsm_oi);

	if (imp != NULL &&
	    (imp->imp_connect_data.ocd_connect_flags & OBD_CONNECT_MAXBYTES))
		(*lsmp)->lsm_maxbytes = imp->imp_connect_data.ocd_maxbytes;
	else
		(*lsmp)->lsm_maxbytes = LUSTRE_STRIPE_MAXBYTES;

	return lsm_size;
}

static inline void osc_pack_capa(struct ptlrpc_request *req,
				 struct ost_body *body, void *capa)
{
	struct obd_capa *oc = (struct obd_capa *)capa;
	struct lustre_capa *c;

	if (!capa)
		return;

	c = req_capsule_client_get(&req->rq_pill, &RMF_CAPA1);
	LASSERT(c);
	capa_cpy(c, oc);
	body->oa.o_valid |= OBD_MD_FLOSSCAPA;
	DEBUG_CAPA(D_SEC, c, "pack");
}

static inline void osc_pack_req_body(struct ptlrpc_request *req,
				     struct obd_info *oinfo)
{
	struct ost_body *body;

	body = req_capsule_client_get(&req->rq_pill, &RMF_OST_BODY);
	LASSERT(body);

	lustre_set_wire_obdo(&req->rq_import->imp_connect_data, &body->oa,
			     oinfo->oi_oa);
	osc_pack_capa(req, body, oinfo->oi_capa);
}

static inline void osc_set_capa_size(struct ptlrpc_request *req,
				     const struct req_msg_field *field,
				     struct obd_capa *oc)
{
	if (oc == NULL)
		req_capsule_set_size(&req->rq_pill, field, RCL_CLIENT, 0);
	else
		/* it is already calculated as sizeof struct obd_capa */
		;
}

static int osc_getattr_interpret(const struct lu_env *env,
				 struct ptlrpc_request *req,
				 struct osc_async_args *aa, int rc)
{
	struct ost_body *body;

	if (rc != 0)
		GOTO(out, rc);

	body = req_capsule_server_get(&req->rq_pill, &RMF_OST_BODY);
	if (body) {
		CDEBUG(D_INODE, "mode: %o\n", body->oa.o_mode);
		lustre_get_wire_obdo(&req->rq_import->imp_connect_data,
				     aa->aa_oi->oi_oa, &body->oa);

		/* This should really be sent by the OST */
		aa->aa_oi->oi_oa->o_blksize = DT_MAX_BRW_SIZE;
		aa->aa_oi->oi_oa->o_valid |= OBD_MD_FLBLKSZ;
	} else {
		CDEBUG(D_INFO, "can't unpack ost_body\n");
		rc = -EPROTO;
		aa->aa_oi->oi_oa->o_valid = 0;
	}
out:
	rc = aa->aa_oi->oi_cb_up(aa->aa_oi, rc);
	return rc;
}

static int osc_getattr_async(struct obd_export *exp, struct obd_info *oinfo,
			     struct ptlrpc_request_set *set)
{
	struct ptlrpc_request *req;
	struct osc_async_args *aa;
	int		    rc;

	req = ptlrpc_request_alloc(class_exp2cliimp(exp), &RQF_OST_GETATTR);
	if (req == NULL)
		return -ENOMEM;

	osc_set_capa_size(req, &RMF_CAPA1, oinfo->oi_capa);
	rc = ptlrpc_request_pack(req, LUSTRE_OST_VERSION, OST_GETATTR);
	if (rc) {
		ptlrpc_request_free(req);
		return rc;
	}

	osc_pack_req_body(req, oinfo);

	ptlrpc_request_set_replen(req);
	req->rq_interpret_reply = (ptlrpc_interpterer_t)osc_getattr_interpret;

	CLASSERT(sizeof(*aa) <= sizeof(req->rq_async_args));
	aa = ptlrpc_req_async_args(req);
	aa->aa_oi = oinfo;

	ptlrpc_set_add_req(set, req);
	return 0;
}

static int osc_getattr(const struct lu_env *env, struct obd_export *exp,
		       struct obd_info *oinfo)
{
	struct ptlrpc_request *req;
	struct ost_body       *body;
	int		    rc;

	req = ptlrpc_request_alloc(class_exp2cliimp(exp), &RQF_OST_GETATTR);
	if (req == NULL)
		return -ENOMEM;

	osc_set_capa_size(req, &RMF_CAPA1, oinfo->oi_capa);
	rc = ptlrpc_request_pack(req, LUSTRE_OST_VERSION, OST_GETATTR);
	if (rc) {
		ptlrpc_request_free(req);
		return rc;
	}

	osc_pack_req_body(req, oinfo);

	ptlrpc_request_set_replen(req);

	rc = ptlrpc_queue_wait(req);
	if (rc)
		GOTO(out, rc);

	body = req_capsule_server_get(&req->rq_pill, &RMF_OST_BODY);
	if (body == NULL)
		GOTO(out, rc = -EPROTO);

	CDEBUG(D_INODE, "mode: %o\n", body->oa.o_mode);
	lustre_get_wire_obdo(&req->rq_import->imp_connect_data, oinfo->oi_oa,
			     &body->oa);

	oinfo->oi_oa->o_blksize = cli_brw_size(exp->exp_obd);
	oinfo->oi_oa->o_valid |= OBD_MD_FLBLKSZ;

 out:
	ptlrpc_req_finished(req);
	return rc;
}

static int osc_setattr(const struct lu_env *env, struct obd_export *exp,
		       struct obd_info *oinfo, struct obd_trans_info *oti)
{
	struct ptlrpc_request *req;
	struct ost_body       *body;
	int		    rc;

	LASSERT(oinfo->oi_oa->o_valid & OBD_MD_FLGROUP);

	req = ptlrpc_request_alloc(class_exp2cliimp(exp), &RQF_OST_SETATTR);
	if (req == NULL)
		return -ENOMEM;

	osc_set_capa_size(req, &RMF_CAPA1, oinfo->oi_capa);
	rc = ptlrpc_request_pack(req, LUSTRE_OST_VERSION, OST_SETATTR);
	if (rc) {
		ptlrpc_request_free(req);
		return rc;
	}

	osc_pack_req_body(req, oinfo);

	ptlrpc_request_set_replen(req);

	rc = ptlrpc_queue_wait(req);
	if (rc)
		GOTO(out, rc);

	body = req_capsule_server_get(&req->rq_pill, &RMF_OST_BODY);
	if (body == NULL)
		GOTO(out, rc = -EPROTO);

	lustre_get_wire_obdo(&req->rq_import->imp_connect_data, oinfo->oi_oa,
			     &body->oa);

out:
	ptlrpc_req_finished(req);
	return rc;
}

static int osc_setattr_interpret(const struct lu_env *env,
				 struct ptlrpc_request *req,
				 struct osc_setattr_args *sa, int rc)
{
	struct ost_body *body;

	if (rc != 0)
		GOTO(out, rc);

	body = req_capsule_server_get(&req->rq_pill, &RMF_OST_BODY);
	if (body == NULL)
		GOTO(out, rc = -EPROTO);

	lustre_get_wire_obdo(&req->rq_import->imp_connect_data, sa->sa_oa,
			     &body->oa);
out:
	rc = sa->sa_upcall(sa->sa_cookie, rc);
	return rc;
}

int osc_setattr_async_base(struct obd_export *exp, struct obd_info *oinfo,
			   struct obd_trans_info *oti,
			   obd_enqueue_update_f upcall, void *cookie,
			   struct ptlrpc_request_set *rqset)
{
	struct ptlrpc_request   *req;
	struct osc_setattr_args *sa;
	int		      rc;

	req = ptlrpc_request_alloc(class_exp2cliimp(exp), &RQF_OST_SETATTR);
	if (req == NULL)
		return -ENOMEM;

	osc_set_capa_size(req, &RMF_CAPA1, oinfo->oi_capa);
	rc = ptlrpc_request_pack(req, LUSTRE_OST_VERSION, OST_SETATTR);
	if (rc) {
		ptlrpc_request_free(req);
		return rc;
	}

	if (oti && oinfo->oi_oa->o_valid & OBD_MD_FLCOOKIE)
		oinfo->oi_oa->o_lcookie = *oti->oti_logcookies;

	osc_pack_req_body(req, oinfo);

	ptlrpc_request_set_replen(req);

	/* do mds to ost setattr asynchronously */
	if (!rqset) {
		/* Do not wait for response. */
		ptlrpcd_add_req(req, PDL_POLICY_ROUND, -1);
	} else {
		req->rq_interpret_reply =
			(ptlrpc_interpterer_t)osc_setattr_interpret;

		CLASSERT (sizeof(*sa) <= sizeof(req->rq_async_args));
		sa = ptlrpc_req_async_args(req);
		sa->sa_oa = oinfo->oi_oa;
		sa->sa_upcall = upcall;
		sa->sa_cookie = cookie;

		if (rqset == PTLRPCD_SET)
			ptlrpcd_add_req(req, PDL_POLICY_ROUND, -1);
		else
			ptlrpc_set_add_req(rqset, req);
	}

	return 0;
}

static int osc_setattr_async(struct obd_export *exp, struct obd_info *oinfo,
			     struct obd_trans_info *oti,
			     struct ptlrpc_request_set *rqset)
{
	return osc_setattr_async_base(exp, oinfo, oti,
				      oinfo->oi_cb_up, oinfo, rqset);
}

int osc_real_create(struct obd_export *exp, struct obdo *oa,
		    struct lov_stripe_md **ea, struct obd_trans_info *oti)
{
	struct ptlrpc_request *req;
	struct ost_body       *body;
	struct lov_stripe_md  *lsm;
	int		    rc;

	LASSERT(oa);
	LASSERT(ea);

	lsm = *ea;
	if (!lsm) {
		rc = obd_alloc_memmd(exp, &lsm);
		if (rc < 0)
			return rc;
	}

	req = ptlrpc_request_alloc(class_exp2cliimp(exp), &RQF_OST_CREATE);
	if (req == NULL)
		GOTO(out, rc = -ENOMEM);

	rc = ptlrpc_request_pack(req, LUSTRE_OST_VERSION, OST_CREATE);
	if (rc) {
		ptlrpc_request_free(req);
		GOTO(out, rc);
	}

	body = req_capsule_client_get(&req->rq_pill, &RMF_OST_BODY);
	LASSERT(body);

	lustre_set_wire_obdo(&req->rq_import->imp_connect_data, &body->oa, oa);

	ptlrpc_request_set_replen(req);

	if ((oa->o_valid & OBD_MD_FLFLAGS) &&
	    oa->o_flags == OBD_FL_DELORPHAN) {
		DEBUG_REQ(D_HA, req,
			  "delorphan from OST integration");
		/* Don't resend the delorphan req */
		req->rq_no_resend = req->rq_no_delay = 1;
	}

	rc = ptlrpc_queue_wait(req);
	if (rc)
		GOTO(out_req, rc);

	body = req_capsule_server_get(&req->rq_pill, &RMF_OST_BODY);
	if (body == NULL)
		GOTO(out_req, rc = -EPROTO);

	CDEBUG(D_INFO, "oa flags %x\n", oa->o_flags);
	lustre_get_wire_obdo(&req->rq_import->imp_connect_data, oa, &body->oa);

	oa->o_blksize = cli_brw_size(exp->exp_obd);
	oa->o_valid |= OBD_MD_FLBLKSZ;

	/* XXX LOV STACKING: the lsm that is passed to us from LOV does not
	 * have valid lsm_oinfo data structs, so don't go touching that.
	 * This needs to be fixed in a big way.
	 */
	lsm->lsm_oi = oa->o_oi;
	*ea = lsm;

	if (oti != NULL) {
		oti->oti_transno = lustre_msg_get_transno(req->rq_repmsg);

		if (oa->o_valid & OBD_MD_FLCOOKIE) {
			if (!oti->oti_logcookies)
				oti_alloc_cookies(oti, 1);
			*oti->oti_logcookies = oa->o_lcookie;
		}
	}

	CDEBUG(D_HA, "transno: "LPD64"\n",
	       lustre_msg_get_transno(req->rq_repmsg));
out_req:
	ptlrpc_req_finished(req);
out:
	if (rc && !*ea)
		obd_free_memmd(exp, &lsm);
	return rc;
}

int osc_punch_base(struct obd_export *exp, struct obd_info *oinfo,
		   obd_enqueue_update_f upcall, void *cookie,
		   struct ptlrpc_request_set *rqset)
{
	struct ptlrpc_request   *req;
	struct osc_setattr_args *sa;
	struct ost_body	 *body;
	int		      rc;

	req = ptlrpc_request_alloc(class_exp2cliimp(exp), &RQF_OST_PUNCH);
	if (req == NULL)
		return -ENOMEM;

	osc_set_capa_size(req, &RMF_CAPA1, oinfo->oi_capa);
	rc = ptlrpc_request_pack(req, LUSTRE_OST_VERSION, OST_PUNCH);
	if (rc) {
		ptlrpc_request_free(req);
		return rc;
	}
	req->rq_request_portal = OST_IO_PORTAL; /* bug 7198 */
	ptlrpc_at_set_req_timeout(req);

	body = req_capsule_client_get(&req->rq_pill, &RMF_OST_BODY);
	LASSERT(body);
	lustre_set_wire_obdo(&req->rq_import->imp_connect_data, &body->oa,
			     oinfo->oi_oa);
	osc_pack_capa(req, body, oinfo->oi_capa);

	ptlrpc_request_set_replen(req);

	req->rq_interpret_reply = (ptlrpc_interpterer_t)osc_setattr_interpret;
	CLASSERT (sizeof(*sa) <= sizeof(req->rq_async_args));
	sa = ptlrpc_req_async_args(req);
	sa->sa_oa     = oinfo->oi_oa;
	sa->sa_upcall = upcall;
	sa->sa_cookie = cookie;
	if (rqset == PTLRPCD_SET)
		ptlrpcd_add_req(req, PDL_POLICY_ROUND, -1);
	else
		ptlrpc_set_add_req(rqset, req);

	return 0;
}

static int osc_punch(const struct lu_env *env, struct obd_export *exp,
		     struct obd_info *oinfo, struct obd_trans_info *oti,
		     struct ptlrpc_request_set *rqset)
{
	oinfo->oi_oa->o_size   = oinfo->oi_policy.l_extent.start;
	oinfo->oi_oa->o_blocks = oinfo->oi_policy.l_extent.end;
	oinfo->oi_oa->o_valid |= OBD_MD_FLSIZE | OBD_MD_FLBLOCKS;
	return osc_punch_base(exp, oinfo,
			      oinfo->oi_cb_up, oinfo, rqset);
}

static int osc_sync_interpret(const struct lu_env *env,
			      struct ptlrpc_request *req,
			      void *arg, int rc)
{
	struct osc_fsync_args *fa = arg;
	struct ost_body *body;

	if (rc)
		GOTO(out, rc);

	body = req_capsule_server_get(&req->rq_pill, &RMF_OST_BODY);
	if (body == NULL) {
		CERROR ("can't unpack ost_body\n");
		GOTO(out, rc = -EPROTO);
	}

	*fa->fa_oi->oi_oa = body->oa;
out:
	rc = fa->fa_upcall(fa->fa_cookie, rc);
	return rc;
}

int osc_sync_base(struct obd_export *exp, struct obd_info *oinfo,
		  obd_enqueue_update_f upcall, void *cookie,
		  struct ptlrpc_request_set *rqset)
{
	struct ptlrpc_request *req;
	struct ost_body       *body;
	struct osc_fsync_args *fa;
	int		    rc;

	req = ptlrpc_request_alloc(class_exp2cliimp(exp), &RQF_OST_SYNC);
	if (req == NULL)
		return -ENOMEM;

	osc_set_capa_size(req, &RMF_CAPA1, oinfo->oi_capa);
	rc = ptlrpc_request_pack(req, LUSTRE_OST_VERSION, OST_SYNC);
	if (rc) {
		ptlrpc_request_free(req);
		return rc;
	}

	/* overload the size and blocks fields in the oa with start/end */
	body = req_capsule_client_get(&req->rq_pill, &RMF_OST_BODY);
	LASSERT(body);
	lustre_set_wire_obdo(&req->rq_import->imp_connect_data, &body->oa,
			     oinfo->oi_oa);
	osc_pack_capa(req, body, oinfo->oi_capa);

	ptlrpc_request_set_replen(req);
	req->rq_interpret_reply = osc_sync_interpret;

	CLASSERT(sizeof(*fa) <= sizeof(req->rq_async_args));
	fa = ptlrpc_req_async_args(req);
	fa->fa_oi = oinfo;
	fa->fa_upcall = upcall;
	fa->fa_cookie = cookie;

	if (rqset == PTLRPCD_SET)
		ptlrpcd_add_req(req, PDL_POLICY_ROUND, -1);
	else
		ptlrpc_set_add_req(rqset, req);

	return 0;
}

static int osc_sync(const struct lu_env *env, struct obd_export *exp,
		    struct obd_info *oinfo, obd_size start, obd_size end,
		    struct ptlrpc_request_set *set)
{
	if (!oinfo->oi_oa) {
		CDEBUG(D_INFO, "oa NULL\n");
		return -EINVAL;
	}

	oinfo->oi_oa->o_size = start;
	oinfo->oi_oa->o_blocks = end;
	oinfo->oi_oa->o_valid |= (OBD_MD_FLSIZE | OBD_MD_FLBLOCKS);

	return osc_sync_base(exp, oinfo, oinfo->oi_cb_up, oinfo, set);
}

/* Find and cancel locally locks matched by @mode in the resource found by
 * @objid. Found locks are added into @cancel list. Returns the amount of
 * locks added to @cancels list. */
static int osc_resource_get_unused(struct obd_export *exp, struct obdo *oa,
				   struct list_head *cancels,
				   ldlm_mode_t mode, __u64 lock_flags)
{
	struct ldlm_namespace *ns = exp->exp_obd->obd_namespace;
	struct ldlm_res_id res_id;
	struct ldlm_resource *res;
	int count;

	/* Return, i.e. cancel nothing, only if ELC is supported (flag in
	 * export) but disabled through procfs (flag in NS).
	 *
	 * This distinguishes from a case when ELC is not supported originally,
	 * when we still want to cancel locks in advance and just cancel them
	 * locally, without sending any RPC. */
	if (exp_connect_cancelset(exp) && !ns_connect_cancelset(ns))
		return 0;

	ostid_build_res_name(&oa->o_oi, &res_id);
	res = ldlm_resource_get(ns, NULL, &res_id, 0, 0);
	if (res == NULL)
		return 0;

	LDLM_RESOURCE_ADDREF(res);
	count = ldlm_cancel_resource_local(res, cancels, NULL, mode,
					   lock_flags, 0, NULL);
	LDLM_RESOURCE_DELREF(res);
	ldlm_resource_putref(res);
	return count;
}

static int osc_destroy_interpret(const struct lu_env *env,
				 struct ptlrpc_request *req, void *data,
				 int rc)
{
	struct client_obd *cli = &req->rq_import->imp_obd->u.cli;

	atomic_dec(&cli->cl_destroy_in_flight);
	wake_up(&cli->cl_destroy_waitq);
	return 0;
}

static int osc_can_send_destroy(struct client_obd *cli)
{
	if (atomic_inc_return(&cli->cl_destroy_in_flight) <=
	    cli->cl_max_rpcs_in_flight) {
		/* The destroy request can be sent */
		return 1;
	}
	if (atomic_dec_return(&cli->cl_destroy_in_flight) <
	    cli->cl_max_rpcs_in_flight) {
		/*
		 * The counter has been modified between the two atomic
		 * operations.
		 */
		wake_up(&cli->cl_destroy_waitq);
	}
	return 0;
}

int osc_create(const struct lu_env *env, struct obd_export *exp,
	       struct obdo *oa, struct lov_stripe_md **ea,
	       struct obd_trans_info *oti)
{
	int rc = 0;

	LASSERT(oa);
	LASSERT(ea);
	LASSERT(oa->o_valid & OBD_MD_FLGROUP);

	if ((oa->o_valid & OBD_MD_FLFLAGS) &&
	    oa->o_flags == OBD_FL_RECREATE_OBJS) {
		return osc_real_create(exp, oa, ea, oti);
	}

	if (!fid_seq_is_mdt(ostid_seq(&oa->o_oi)))
		return osc_real_create(exp, oa, ea, oti);

	/* we should not get here anymore */
	LBUG();

	return rc;
}

/* Destroy requests can be async always on the client, and we don't even really
 * care about the return code since the client cannot do anything at all about
 * a destroy failure.
 * When the MDS is unlinking a filename, it saves the file objects into a
 * recovery llog, and these object records are cancelled when the OST reports
 * they were destroyed and sync'd to disk (i.e. transaction committed).
 * If the client dies, or the OST is down when the object should be destroyed,
 * the records are not cancelled, and when the OST reconnects to the MDS next,
 * it will retrieve the llog unlink logs and then sends the log cancellation
 * cookies to the MDS after committing destroy transactions. */
static int osc_destroy(const struct lu_env *env, struct obd_export *exp,
		       struct obdo *oa, struct lov_stripe_md *ea,
		       struct obd_trans_info *oti, struct obd_export *md_export,
		       void *capa)
{
	struct client_obd     *cli = &exp->exp_obd->u.cli;
	struct ptlrpc_request *req;
	struct ost_body       *body;
	LIST_HEAD(cancels);
	int rc, count;

	if (!oa) {
		CDEBUG(D_INFO, "oa NULL\n");
		return -EINVAL;
	}

	count = osc_resource_get_unused(exp, oa, &cancels, LCK_PW,
					LDLM_FL_DISCARD_DATA);

	req = ptlrpc_request_alloc(class_exp2cliimp(exp), &RQF_OST_DESTROY);
	if (req == NULL) {
		ldlm_lock_list_put(&cancels, l_bl_ast, count);
		return -ENOMEM;
	}

	osc_set_capa_size(req, &RMF_CAPA1, (struct obd_capa *)capa);
	rc = ldlm_prep_elc_req(exp, req, LUSTRE_OST_VERSION, OST_DESTROY,
			       0, &cancels, count);
	if (rc) {
		ptlrpc_request_free(req);
		return rc;
	}

	req->rq_request_portal = OST_IO_PORTAL; /* bug 7198 */
	ptlrpc_at_set_req_timeout(req);

	if (oti != NULL && oa->o_valid & OBD_MD_FLCOOKIE)
		oa->o_lcookie = *oti->oti_logcookies;
	body = req_capsule_client_get(&req->rq_pill, &RMF_OST_BODY);
	LASSERT(body);
	lustre_set_wire_obdo(&req->rq_import->imp_connect_data, &body->oa, oa);

	osc_pack_capa(req, body, (struct obd_capa *)capa);
	ptlrpc_request_set_replen(req);

	/* If osc_destroy is for destroying the unlink orphan,
	 * sent from MDT to OST, which should not be blocked here,
	 * because the process might be triggered by ptlrpcd, and
	 * it is not good to block ptlrpcd thread (b=16006)*/
	if (!(oa->o_flags & OBD_FL_DELORPHAN)) {
		req->rq_interpret_reply = osc_destroy_interpret;
		if (!osc_can_send_destroy(cli)) {
			struct l_wait_info lwi = LWI_INTR(LWI_ON_SIGNAL_NOOP,
							  NULL);

			/*
			 * Wait until the number of on-going destroy RPCs drops
			 * under max_rpc_in_flight
			 */
			l_wait_event_exclusive(cli->cl_destroy_waitq,
					       osc_can_send_destroy(cli), &lwi);
		}
	}

	/* Do not wait for response */
	ptlrpcd_add_req(req, PDL_POLICY_ROUND, -1);
	return 0;
}

static void osc_announce_cached(struct client_obd *cli, struct obdo *oa,
				long writing_bytes)
{
	obd_flag bits = OBD_MD_FLBLOCKS|OBD_MD_FLGRANT;

	LASSERT(!(oa->o_valid & bits));

	oa->o_valid |= bits;
	client_obd_list_lock(&cli->cl_loi_list_lock);
	oa->o_dirty = cli->cl_dirty;
	if (unlikely(cli->cl_dirty - cli->cl_dirty_transit >
		     cli->cl_dirty_max)) {
		CERROR("dirty %lu - %lu > dirty_max %lu\n",
		       cli->cl_dirty, cli->cl_dirty_transit, cli->cl_dirty_max);
		oa->o_undirty = 0;
	} else if (unlikely(atomic_read(&obd_dirty_pages) -
			    atomic_read(&obd_dirty_transit_pages) >
			    (long)(obd_max_dirty_pages + 1))) {
		/* The atomic_read() allowing the atomic_inc() are
		 * not covered by a lock thus they may safely race and trip
		 * this CERROR() unless we add in a small fudge factor (+1). */
		CERROR("dirty %d - %d > system dirty_max %d\n",
		       atomic_read(&obd_dirty_pages),
		       atomic_read(&obd_dirty_transit_pages),
		       obd_max_dirty_pages);
		oa->o_undirty = 0;
	} else if (unlikely(cli->cl_dirty_max - cli->cl_dirty > 0x7fffffff)) {
		CERROR("dirty %lu - dirty_max %lu too big???\n",
		       cli->cl_dirty, cli->cl_dirty_max);
		oa->o_undirty = 0;
	} else {
		long max_in_flight = (cli->cl_max_pages_per_rpc <<
				      PAGE_CACHE_SHIFT)*
				     (cli->cl_max_rpcs_in_flight + 1);
		oa->o_undirty = max(cli->cl_dirty_max, max_in_flight);
	}
	oa->o_grant = cli->cl_avail_grant + cli->cl_reserved_grant;
	oa->o_dropped = cli->cl_lost_grant;
	cli->cl_lost_grant = 0;
	client_obd_list_unlock(&cli->cl_loi_list_lock);
	CDEBUG(D_CACHE,"dirty: "LPU64" undirty: %u dropped %u grant: "LPU64"\n",
	       oa->o_dirty, oa->o_undirty, oa->o_dropped, oa->o_grant);

}

void osc_update_next_shrink(struct client_obd *cli)
{
	cli->cl_next_shrink_grant =
		cfs_time_shift(cli->cl_grant_shrink_interval);
	CDEBUG(D_CACHE, "next time %ld to shrink grant \n",
	       cli->cl_next_shrink_grant);
}

static void __osc_update_grant(struct client_obd *cli, obd_size grant)
{
	client_obd_list_lock(&cli->cl_loi_list_lock);
	cli->cl_avail_grant += grant;
	client_obd_list_unlock(&cli->cl_loi_list_lock);
}

static void osc_update_grant(struct client_obd *cli, struct ost_body *body)
{
	if (body->oa.o_valid & OBD_MD_FLGRANT) {
		CDEBUG(D_CACHE, "got "LPU64" extra grant\n", body->oa.o_grant);
		__osc_update_grant(cli, body->oa.o_grant);
	}
}

static int osc_set_info_async(const struct lu_env *env, struct obd_export *exp,
			      obd_count keylen, void *key, obd_count vallen,
			      void *val, struct ptlrpc_request_set *set);

static int osc_shrink_grant_interpret(const struct lu_env *env,
				      struct ptlrpc_request *req,
				      void *aa, int rc)
{
	struct client_obd *cli = &req->rq_import->imp_obd->u.cli;
	struct obdo *oa = ((struct osc_grant_args *)aa)->aa_oa;
	struct ost_body *body;

	if (rc != 0) {
		__osc_update_grant(cli, oa->o_grant);
		GOTO(out, rc);
	}

	body = req_capsule_server_get(&req->rq_pill, &RMF_OST_BODY);
	LASSERT(body);
	osc_update_grant(cli, body);
out:
	OBDO_FREE(oa);
	return rc;
}

static void osc_shrink_grant_local(struct client_obd *cli, struct obdo *oa)
{
	client_obd_list_lock(&cli->cl_loi_list_lock);
	oa->o_grant = cli->cl_avail_grant / 4;
	cli->cl_avail_grant -= oa->o_grant;
	client_obd_list_unlock(&cli->cl_loi_list_lock);
	if (!(oa->o_valid & OBD_MD_FLFLAGS)) {
		oa->o_valid |= OBD_MD_FLFLAGS;
		oa->o_flags = 0;
	}
	oa->o_flags |= OBD_FL_SHRINK_GRANT;
	osc_update_next_shrink(cli);
}

/* Shrink the current grant, either from some large amount to enough for a
 * full set of in-flight RPCs, or if we have already shrunk to that limit
 * then to enough for a single RPC.  This avoids keeping more grant than
 * needed, and avoids shrinking the grant piecemeal. */
static int osc_shrink_grant(struct client_obd *cli)
{
	__u64 target_bytes = (cli->cl_max_rpcs_in_flight + 1) *
			     (cli->cl_max_pages_per_rpc << PAGE_CACHE_SHIFT);

	client_obd_list_lock(&cli->cl_loi_list_lock);
	if (cli->cl_avail_grant <= target_bytes)
		target_bytes = cli->cl_max_pages_per_rpc << PAGE_CACHE_SHIFT;
	client_obd_list_unlock(&cli->cl_loi_list_lock);

	return osc_shrink_grant_to_target(cli, target_bytes);
}

int osc_shrink_grant_to_target(struct client_obd *cli, __u64 target_bytes)
{
	int			rc = 0;
	struct ost_body	*body;

	client_obd_list_lock(&cli->cl_loi_list_lock);
	/* Don't shrink if we are already above or below the desired limit
	 * We don't want to shrink below a single RPC, as that will negatively
	 * impact block allocation and long-term performance. */
	if (target_bytes < cli->cl_max_pages_per_rpc << PAGE_CACHE_SHIFT)
		target_bytes = cli->cl_max_pages_per_rpc << PAGE_CACHE_SHIFT;

	if (target_bytes >= cli->cl_avail_grant) {
		client_obd_list_unlock(&cli->cl_loi_list_lock);
		return 0;
	}
	client_obd_list_unlock(&cli->cl_loi_list_lock);

	OBD_ALLOC_PTR(body);
	if (!body)
		return -ENOMEM;

	osc_announce_cached(cli, &body->oa, 0);

	client_obd_list_lock(&cli->cl_loi_list_lock);
	body->oa.o_grant = cli->cl_avail_grant - target_bytes;
	cli->cl_avail_grant = target_bytes;
	client_obd_list_unlock(&cli->cl_loi_list_lock);
	if (!(body->oa.o_valid & OBD_MD_FLFLAGS)) {
		body->oa.o_valid |= OBD_MD_FLFLAGS;
		body->oa.o_flags = 0;
	}
	body->oa.o_flags |= OBD_FL_SHRINK_GRANT;
	osc_update_next_shrink(cli);

	rc = osc_set_info_async(NULL, cli->cl_import->imp_obd->obd_self_export,
				sizeof(KEY_GRANT_SHRINK), KEY_GRANT_SHRINK,
				sizeof(*body), body, NULL);
	if (rc != 0)
		__osc_update_grant(cli, body->oa.o_grant);
	OBD_FREE_PTR(body);
	return rc;
}

static int osc_should_shrink_grant(struct client_obd *client)
{
	unsigned long time = cfs_time_current();
	unsigned long next_shrink = client->cl_next_shrink_grant;

	if ((client->cl_import->imp_connect_data.ocd_connect_flags &
	     OBD_CONNECT_GRANT_SHRINK) == 0)
		return 0;

	if (cfs_time_aftereq(time, next_shrink - 5 * CFS_TICK)) {
		/* Get the current RPC size directly, instead of going via:
		 * cli_brw_size(obd->u.cli.cl_import->imp_obd->obd_self_export)
		 * Keep comment here so that it can be found by searching. */
		int brw_size = client->cl_max_pages_per_rpc << PAGE_CACHE_SHIFT;

		if (client->cl_import->imp_state == LUSTRE_IMP_FULL &&
		    client->cl_avail_grant > brw_size)
			return 1;
		else
			osc_update_next_shrink(client);
	}
	return 0;
}

static int osc_grant_shrink_grant_cb(struct timeout_item *item, void *data)
{
	struct client_obd *client;

	list_for_each_entry(client, &item->ti_obd_list,
				cl_grant_shrink_list) {
		if (osc_should_shrink_grant(client))
			osc_shrink_grant(client);
	}
	return 0;
}

static int osc_add_shrink_grant(struct client_obd *client)
{
	int rc;

	rc = ptlrpc_add_timeout_client(client->cl_grant_shrink_interval,
				       TIMEOUT_GRANT,
				       osc_grant_shrink_grant_cb, NULL,
				       &client->cl_grant_shrink_list);
	if (rc) {
		CERROR("add grant client %s error %d\n",
			client->cl_import->imp_obd->obd_name, rc);
		return rc;
	}
	CDEBUG(D_CACHE, "add grant client %s \n",
	       client->cl_import->imp_obd->obd_name);
	osc_update_next_shrink(client);
	return 0;
}

static int osc_del_shrink_grant(struct client_obd *client)
{
	return ptlrpc_del_timeout_client(&client->cl_grant_shrink_list,
					 TIMEOUT_GRANT);
}

static void osc_init_grant(struct client_obd *cli, struct obd_connect_data *ocd)
{
	/*
	 * ocd_grant is the total grant amount we're expect to hold: if we've
	 * been evicted, it's the new avail_grant amount, cl_dirty will drop
	 * to 0 as inflight RPCs fail out; otherwise, it's avail_grant + dirty.
	 *
	 * race is tolerable here: if we're evicted, but imp_state already
	 * left EVICTED state, then cl_dirty must be 0 already.
	 */
	client_obd_list_lock(&cli->cl_loi_list_lock);
	if (cli->cl_import->imp_state == LUSTRE_IMP_EVICTED)
		cli->cl_avail_grant = ocd->ocd_grant;
	else
		cli->cl_avail_grant = ocd->ocd_grant - cli->cl_dirty;

	if (cli->cl_avail_grant < 0) {
		CWARN("%s: available grant < 0: avail/ocd/dirty %ld/%u/%ld\n",
		      cli->cl_import->imp_obd->obd_name, cli->cl_avail_grant,
		      ocd->ocd_grant, cli->cl_dirty);
		/* workaround for servers which do not have the patch from
		 * LU-2679 */
		cli->cl_avail_grant = ocd->ocd_grant;
	}

	/* determine the appropriate chunk size used by osc_extent. */
	cli->cl_chunkbits = max_t(int, PAGE_CACHE_SHIFT, ocd->ocd_blocksize);
	client_obd_list_unlock(&cli->cl_loi_list_lock);

	CDEBUG(D_CACHE, "%s, setting cl_avail_grant: %ld cl_lost_grant: %ld."
		"chunk bits: %d.\n", cli->cl_import->imp_obd->obd_name,
		cli->cl_avail_grant, cli->cl_lost_grant, cli->cl_chunkbits);

	if (ocd->ocd_connect_flags & OBD_CONNECT_GRANT_SHRINK &&
	    list_empty(&cli->cl_grant_shrink_list))
		osc_add_shrink_grant(cli);
}

/* We assume that the reason this OSC got a short read is because it read
 * beyond the end of a stripe file; i.e. lustre is reading a sparse file
 * via the LOV, and it _knows_ it's reading inside the file, it's just that
 * this stripe never got written at or beyond this stripe offset yet. */
static void handle_short_read(int nob_read, obd_count page_count,
			      struct brw_page **pga)
{
	char *ptr;
	int i = 0;

	/* skip bytes read OK */
	while (nob_read > 0) {
		LASSERT (page_count > 0);

		if (pga[i]->count > nob_read) {
			/* EOF inside this page */
			ptr = kmap(pga[i]->pg) +
				(pga[i]->off & ~CFS_PAGE_MASK);
			memset(ptr + nob_read, 0, pga[i]->count - nob_read);
			kunmap(pga[i]->pg);
			page_count--;
			i++;
			break;
		}

		nob_read -= pga[i]->count;
		page_count--;
		i++;
	}

	/* zero remaining pages */
	while (page_count-- > 0) {
		ptr = kmap(pga[i]->pg) + (pga[i]->off & ~CFS_PAGE_MASK);
		memset(ptr, 0, pga[i]->count);
		kunmap(pga[i]->pg);
		i++;
	}
}

static int check_write_rcs(struct ptlrpc_request *req,
			   int requested_nob, int niocount,
			   obd_count page_count, struct brw_page **pga)
{
	int     i;
	__u32   *remote_rcs;

	remote_rcs = req_capsule_server_sized_get(&req->rq_pill, &RMF_RCS,
						  sizeof(*remote_rcs) *
						  niocount);
	if (remote_rcs == NULL) {
		CDEBUG(D_INFO, "Missing/short RC vector on BRW_WRITE reply\n");
		return(-EPROTO);
	}

	/* return error if any niobuf was in error */
	for (i = 0; i < niocount; i++) {
		if ((int)remote_rcs[i] < 0)
			return(remote_rcs[i]);

		if (remote_rcs[i] != 0) {
			CDEBUG(D_INFO, "rc[%d] invalid (%d) req %p\n",
				i, remote_rcs[i], req);
			return(-EPROTO);
		}
	}

	if (req->rq_bulk->bd_nob_transferred != requested_nob) {
		CERROR("Unexpected # bytes transferred: %d (requested %d)\n",
		       req->rq_bulk->bd_nob_transferred, requested_nob);
		return(-EPROTO);
	}

	return (0);
}

static inline int can_merge_pages(struct brw_page *p1, struct brw_page *p2)
{
	if (p1->flag != p2->flag) {
		unsigned mask = ~(OBD_BRW_FROM_GRANT| OBD_BRW_NOCACHE|
				  OBD_BRW_SYNC|OBD_BRW_ASYNC|OBD_BRW_NOQUOTA);

		/* warn if we try to combine flags that we don't know to be
		 * safe to combine */
		if (unlikely((p1->flag & mask) != (p2->flag & mask))) {
			CWARN("Saw flags 0x%x and 0x%x in the same brw, please "
			      "report this at http://bugs.whamcloud.com/\n",
			      p1->flag, p2->flag);
		}
		return 0;
	}

	return (p1->off + p1->count == p2->off);
}

static obd_count osc_checksum_bulk(int nob, obd_count pg_count,
				   struct brw_page **pga, int opc,
				   cksum_type_t cksum_type)
{
	__u32				cksum;
	int				i = 0;
	struct cfs_crypto_hash_desc	*hdesc;
	unsigned int			bufsize;
	int				err;
	unsigned char			cfs_alg = cksum_obd2cfs(cksum_type);

	LASSERT(pg_count > 0);

	hdesc = cfs_crypto_hash_init(cfs_alg, NULL, 0);
	if (IS_ERR(hdesc)) {
		CERROR("Unable to initialize checksum hash %s\n",
		       cfs_crypto_hash_name(cfs_alg));
		return PTR_ERR(hdesc);
	}

	while (nob > 0 && pg_count > 0) {
		int count = pga[i]->count > nob ? nob : pga[i]->count;

		/* corrupt the data before we compute the checksum, to
		 * simulate an OST->client data error */
		if (i == 0 && opc == OST_READ &&
		    OBD_FAIL_CHECK(OBD_FAIL_OSC_CHECKSUM_RECEIVE)) {
			unsigned char *ptr = kmap(pga[i]->pg);
			int off = pga[i]->off & ~CFS_PAGE_MASK;
			memcpy(ptr + off, "bad1", min(4, nob));
			kunmap(pga[i]->pg);
		}
		cfs_crypto_hash_update_page(hdesc, pga[i]->pg,
				  pga[i]->off & ~CFS_PAGE_MASK,
				  count);
		CDEBUG(D_PAGE,
		       "page %p map %p index %lu flags %lx count %u priv %0lx: off %d\n",
		       pga[i]->pg, pga[i]->pg->mapping, pga[i]->pg->index,
		       (long)pga[i]->pg->flags, page_count(pga[i]->pg),
		       page_private(pga[i]->pg),
		       (int)(pga[i]->off & ~CFS_PAGE_MASK));

		nob -= pga[i]->count;
		pg_count--;
		i++;
	}

	bufsize = 4;
	err = cfs_crypto_hash_final(hdesc, (unsigned char *)&cksum, &bufsize);

	if (err)
		cfs_crypto_hash_final(hdesc, NULL, NULL);

	/* For sending we only compute the wrong checksum instead
	 * of corrupting the data so it is still correct on a redo */
	if (opc == OST_WRITE && OBD_FAIL_CHECK(OBD_FAIL_OSC_CHECKSUM_SEND))
		cksum++;

	return cksum;
}

static int osc_brw_prep_request(int cmd, struct client_obd *cli,struct obdo *oa,
				struct lov_stripe_md *lsm, obd_count page_count,
				struct brw_page **pga,
				struct ptlrpc_request **reqp,
				struct obd_capa *ocapa, int reserve,
				int resend)
{
	struct ptlrpc_request   *req;
	struct ptlrpc_bulk_desc *desc;
	struct ost_body	 *body;
	struct obd_ioobj	*ioobj;
	struct niobuf_remote    *niobuf;
	int niocount, i, requested_nob, opc, rc;
	struct osc_brw_async_args *aa;
	struct req_capsule      *pill;
	struct brw_page *pg_prev;

	if (OBD_FAIL_CHECK(OBD_FAIL_OSC_BRW_PREP_REQ))
		return -ENOMEM; /* Recoverable */
	if (OBD_FAIL_CHECK(OBD_FAIL_OSC_BRW_PREP_REQ2))
		return -EINVAL; /* Fatal */

	if ((cmd & OBD_BRW_WRITE) != 0) {
		opc = OST_WRITE;
		req = ptlrpc_request_alloc_pool(cli->cl_import,
						cli->cl_import->imp_rq_pool,
						&RQF_OST_BRW_WRITE);
	} else {
		opc = OST_READ;
		req = ptlrpc_request_alloc(cli->cl_import, &RQF_OST_BRW_READ);
	}
	if (req == NULL)
		return -ENOMEM;

	for (niocount = i = 1; i < page_count; i++) {
		if (!can_merge_pages(pga[i - 1], pga[i]))
			niocount++;
	}

	pill = &req->rq_pill;
	req_capsule_set_size(pill, &RMF_OBD_IOOBJ, RCL_CLIENT,
			     sizeof(*ioobj));
	req_capsule_set_size(pill, &RMF_NIOBUF_REMOTE, RCL_CLIENT,
			     niocount * sizeof(*niobuf));
	osc_set_capa_size(req, &RMF_CAPA1, ocapa);

	rc = ptlrpc_request_pack(req, LUSTRE_OST_VERSION, opc);
	if (rc) {
		ptlrpc_request_free(req);
		return rc;
	}
	req->rq_request_portal = OST_IO_PORTAL; /* bug 7198 */
	ptlrpc_at_set_req_timeout(req);
	/* ask ptlrpc not to resend on EINPROGRESS since BRWs have their own
	 * retry logic */
	req->rq_no_retry_einprogress = 1;

	desc = ptlrpc_prep_bulk_imp(req, page_count,
		cli->cl_import->imp_connect_data.ocd_brw_size >> LNET_MTU_BITS,
		opc == OST_WRITE ? BULK_GET_SOURCE : BULK_PUT_SINK,
		OST_BULK_PORTAL);

	if (desc == NULL)
		GOTO(out, rc = -ENOMEM);
	/* NB request now owns desc and will free it when it gets freed */

	body = req_capsule_client_get(pill, &RMF_OST_BODY);
	ioobj = req_capsule_client_get(pill, &RMF_OBD_IOOBJ);
	niobuf = req_capsule_client_get(pill, &RMF_NIOBUF_REMOTE);
	LASSERT(body != NULL && ioobj != NULL && niobuf != NULL);

	lustre_set_wire_obdo(&req->rq_import->imp_connect_data, &body->oa, oa);

	obdo_to_ioobj(oa, ioobj);
	ioobj->ioo_bufcnt = niocount;
	/* The high bits of ioo_max_brw tells server _maximum_ number of bulks
	 * that might be send for this request.  The actual number is decided
	 * when the RPC is finally sent in ptlrpc_register_bulk(). It sends
	 * "max - 1" for old client compatibility sending "0", and also so the
	 * the actual maximum is a power-of-two number, not one less. LU-1431 */
	ioobj_max_brw_set(ioobj, desc->bd_md_max_brw);
	osc_pack_capa(req, body, ocapa);
	LASSERT(page_count > 0);
	pg_prev = pga[0];
	for (requested_nob = i = 0; i < page_count; i++, niobuf++) {
		struct brw_page *pg = pga[i];
		int poff = pg->off & ~CFS_PAGE_MASK;

		LASSERT(pg->count > 0);
		/* make sure there is no gap in the middle of page array */
		LASSERTF(page_count == 1 ||
			 (ergo(i == 0, poff + pg->count == PAGE_CACHE_SIZE) &&
			  ergo(i > 0 && i < page_count - 1,
			       poff == 0 && pg->count == PAGE_CACHE_SIZE)   &&
			  ergo(i == page_count - 1, poff == 0)),
			 "i: %d/%d pg: %p off: "LPU64", count: %u\n",
			 i, page_count, pg, pg->off, pg->count);
		LASSERTF(i == 0 || pg->off > pg_prev->off,
			 "i %d p_c %u pg %p [pri %lu ind %lu] off "LPU64
			 " prev_pg %p [pri %lu ind %lu] off "LPU64"\n",
			 i, page_count,
			 pg->pg, page_private(pg->pg), pg->pg->index, pg->off,
			 pg_prev->pg, page_private(pg_prev->pg),
			 pg_prev->pg->index, pg_prev->off);
		LASSERT((pga[0]->flag & OBD_BRW_SRVLOCK) ==
			(pg->flag & OBD_BRW_SRVLOCK));

		ptlrpc_prep_bulk_page_pin(desc, pg->pg, poff, pg->count);
		requested_nob += pg->count;

		if (i > 0 && can_merge_pages(pg_prev, pg)) {
			niobuf--;
			niobuf->len += pg->count;
		} else {
			niobuf->offset = pg->off;
			niobuf->len    = pg->count;
			niobuf->flags  = pg->flag;
		}
		pg_prev = pg;
	}

	LASSERTF((void *)(niobuf - niocount) ==
		req_capsule_client_get(&req->rq_pill, &RMF_NIOBUF_REMOTE),
		"want %p - real %p\n", req_capsule_client_get(&req->rq_pill,
		&RMF_NIOBUF_REMOTE), (void *)(niobuf - niocount));

	osc_announce_cached(cli, &body->oa, opc == OST_WRITE ? requested_nob:0);
	if (resend) {
		if ((body->oa.o_valid & OBD_MD_FLFLAGS) == 0) {
			body->oa.o_valid |= OBD_MD_FLFLAGS;
			body->oa.o_flags = 0;
		}
		body->oa.o_flags |= OBD_FL_RECOV_RESEND;
	}

	if (osc_should_shrink_grant(cli))
		osc_shrink_grant_local(cli, &body->oa);

	/* size[REQ_REC_OFF] still sizeof (*body) */
	if (opc == OST_WRITE) {
		if (cli->cl_checksum &&
		    !sptlrpc_flavor_has_bulk(&req->rq_flvr)) {
			/* store cl_cksum_type in a local variable since
			 * it can be changed via lprocfs */
			cksum_type_t cksum_type = cli->cl_cksum_type;

			if ((body->oa.o_valid & OBD_MD_FLFLAGS) == 0) {
				oa->o_flags &= OBD_FL_LOCAL_MASK;
				body->oa.o_flags = 0;
			}
			body->oa.o_flags |= cksum_type_pack(cksum_type);
			body->oa.o_valid |= OBD_MD_FLCKSUM | OBD_MD_FLFLAGS;
			body->oa.o_cksum = osc_checksum_bulk(requested_nob,
							     page_count, pga,
							     OST_WRITE,
							     cksum_type);
			CDEBUG(D_PAGE, "checksum at write origin: %x\n",
			       body->oa.o_cksum);
			/* save this in 'oa', too, for later checking */
			oa->o_valid |= OBD_MD_FLCKSUM | OBD_MD_FLFLAGS;
			oa->o_flags |= cksum_type_pack(cksum_type);
		} else {
			/* clear out the checksum flag, in case this is a
			 * resend but cl_checksum is no longer set. b=11238 */
			oa->o_valid &= ~OBD_MD_FLCKSUM;
		}
		oa->o_cksum = body->oa.o_cksum;
		/* 1 RC per niobuf */
		req_capsule_set_size(pill, &RMF_RCS, RCL_SERVER,
				     sizeof(__u32) * niocount);
	} else {
		if (cli->cl_checksum &&
		    !sptlrpc_flavor_has_bulk(&req->rq_flvr)) {
			if ((body->oa.o_valid & OBD_MD_FLFLAGS) == 0)
				body->oa.o_flags = 0;
			body->oa.o_flags |= cksum_type_pack(cli->cl_cksum_type);
			body->oa.o_valid |= OBD_MD_FLCKSUM | OBD_MD_FLFLAGS;
		}
	}
	ptlrpc_request_set_replen(req);

	CLASSERT(sizeof(*aa) <= sizeof(req->rq_async_args));
	aa = ptlrpc_req_async_args(req);
	aa->aa_oa = oa;
	aa->aa_requested_nob = requested_nob;
	aa->aa_nio_count = niocount;
	aa->aa_page_count = page_count;
	aa->aa_resends = 0;
	aa->aa_ppga = pga;
	aa->aa_cli = cli;
	INIT_LIST_HEAD(&aa->aa_oaps);
	if (ocapa && reserve)
		aa->aa_ocapa = capa_get(ocapa);

	*reqp = req;
	return 0;

 out:
	ptlrpc_req_finished(req);
	return rc;
}

static int check_write_checksum(struct obdo *oa, const lnet_process_id_t *peer,
				__u32 client_cksum, __u32 server_cksum, int nob,
				obd_count page_count, struct brw_page **pga,
				cksum_type_t client_cksum_type)
{
	__u32 new_cksum;
	char *msg;
	cksum_type_t cksum_type;

	if (server_cksum == client_cksum) {
		CDEBUG(D_PAGE, "checksum %x confirmed\n", client_cksum);
		return 0;
	}

	cksum_type = cksum_type_unpack(oa->o_valid & OBD_MD_FLFLAGS ?
				       oa->o_flags : 0);
	new_cksum = osc_checksum_bulk(nob, page_count, pga, OST_WRITE,
				      cksum_type);

	if (cksum_type != client_cksum_type)
		msg = "the server did not use the checksum type specified in "
		      "the original request - likely a protocol problem";
	else if (new_cksum == server_cksum)
		msg = "changed on the client after we checksummed it - "
		      "likely false positive due to mmap IO (bug 11742)";
	else if (new_cksum == client_cksum)
		msg = "changed in transit before arrival at OST";
	else
		msg = "changed in transit AND doesn't match the original - "
		      "likely false positive due to mmap IO (bug 11742)";

	LCONSOLE_ERROR_MSG(0x132, "BAD WRITE CHECKSUM: %s: from %s inode "DFID
			   " object "DOSTID" extent ["LPU64"-"LPU64"]\n",
			   msg, libcfs_nid2str(peer->nid),
			   oa->o_valid & OBD_MD_FLFID ? oa->o_parent_seq : (__u64)0,
			   oa->o_valid & OBD_MD_FLFID ? oa->o_parent_oid : 0,
			   oa->o_valid & OBD_MD_FLFID ? oa->o_parent_ver : 0,
			   POSTID(&oa->o_oi), pga[0]->off,
			   pga[page_count-1]->off + pga[page_count-1]->count - 1);
	CERROR("original client csum %x (type %x), server csum %x (type %x), "
	       "client csum now %x\n", client_cksum, client_cksum_type,
	       server_cksum, cksum_type, new_cksum);
	return 1;
}

/* Note rc enters this function as number of bytes transferred */
static int osc_brw_fini_request(struct ptlrpc_request *req, int rc)
{
	struct osc_brw_async_args *aa = (void *)&req->rq_async_args;
	const lnet_process_id_t *peer =
			&req->rq_import->imp_connection->c_peer;
	struct client_obd *cli = aa->aa_cli;
	struct ost_body *body;
	__u32 client_cksum = 0;

	if (rc < 0 && rc != -EDQUOT) {
		DEBUG_REQ(D_INFO, req, "Failed request with rc = %d\n", rc);
		return rc;
	}

	LASSERTF(req->rq_repmsg != NULL, "rc = %d\n", rc);
	body = req_capsule_server_get(&req->rq_pill, &RMF_OST_BODY);
	if (body == NULL) {
		DEBUG_REQ(D_INFO, req, "Can't unpack body\n");
		return -EPROTO;
	}

	/* set/clear over quota flag for a uid/gid */
	if (lustre_msg_get_opc(req->rq_reqmsg) == OST_WRITE &&
	    body->oa.o_valid & (OBD_MD_FLUSRQUOTA | OBD_MD_FLGRPQUOTA)) {
		unsigned int qid[MAXQUOTAS] = { body->oa.o_uid, body->oa.o_gid };

		CDEBUG(D_QUOTA, "setdq for [%u %u] with valid "LPX64", flags %x\n",
		       body->oa.o_uid, body->oa.o_gid, body->oa.o_valid,
		       body->oa.o_flags);
		osc_quota_setdq(cli, qid, body->oa.o_valid, body->oa.o_flags);
	}

	osc_update_grant(cli, body);

	if (rc < 0)
		return rc;

	if (aa->aa_oa->o_valid & OBD_MD_FLCKSUM)
		client_cksum = aa->aa_oa->o_cksum; /* save for later */

	if (lustre_msg_get_opc(req->rq_reqmsg) == OST_WRITE) {
		if (rc > 0) {
			CERROR("Unexpected +ve rc %d\n", rc);
			return -EPROTO;
		}
		LASSERT(req->rq_bulk->bd_nob == aa->aa_requested_nob);

		if (sptlrpc_cli_unwrap_bulk_write(req, req->rq_bulk))
			return -EAGAIN;

		if ((aa->aa_oa->o_valid & OBD_MD_FLCKSUM) && client_cksum &&
		    check_write_checksum(&body->oa, peer, client_cksum,
					 body->oa.o_cksum, aa->aa_requested_nob,
					 aa->aa_page_count, aa->aa_ppga,
					 cksum_type_unpack(aa->aa_oa->o_flags)))
			return -EAGAIN;

		rc = check_write_rcs(req, aa->aa_requested_nob,aa->aa_nio_count,
				     aa->aa_page_count, aa->aa_ppga);
		GOTO(out, rc);
	}

	/* The rest of this function executes only for OST_READs */

	/* if unwrap_bulk failed, return -EAGAIN to retry */
	rc = sptlrpc_cli_unwrap_bulk_read(req, req->rq_bulk, rc);
	if (rc < 0)
		GOTO(out, rc = -EAGAIN);

	if (rc > aa->aa_requested_nob) {
		CERROR("Unexpected rc %d (%d requested)\n", rc,
		       aa->aa_requested_nob);
		return -EPROTO;
	}

	if (rc != req->rq_bulk->bd_nob_transferred) {
		CERROR ("Unexpected rc %d (%d transferred)\n",
			rc, req->rq_bulk->bd_nob_transferred);
		return (-EPROTO);
	}

	if (rc < aa->aa_requested_nob)
		handle_short_read(rc, aa->aa_page_count, aa->aa_ppga);

	if (body->oa.o_valid & OBD_MD_FLCKSUM) {
		static int cksum_counter;
		__u32      server_cksum = body->oa.o_cksum;
		char      *via;
		char      *router;
		cksum_type_t cksum_type;

		cksum_type = cksum_type_unpack(body->oa.o_valid &OBD_MD_FLFLAGS?
					       body->oa.o_flags : 0);
		client_cksum = osc_checksum_bulk(rc, aa->aa_page_count,
						 aa->aa_ppga, OST_READ,
						 cksum_type);

		if (peer->nid == req->rq_bulk->bd_sender) {
			via = router = "";
		} else {
			via = " via ";
			router = libcfs_nid2str(req->rq_bulk->bd_sender);
		}

		if (server_cksum != client_cksum) {
			LCONSOLE_ERROR_MSG(0x133, "%s: BAD READ CHECKSUM: from "
					   "%s%s%s inode "DFID" object "DOSTID
					   " extent ["LPU64"-"LPU64"]\n",
					   req->rq_import->imp_obd->obd_name,
					   libcfs_nid2str(peer->nid),
					   via, router,
					   body->oa.o_valid & OBD_MD_FLFID ?
						body->oa.o_parent_seq : (__u64)0,
					   body->oa.o_valid & OBD_MD_FLFID ?
						body->oa.o_parent_oid : 0,
					   body->oa.o_valid & OBD_MD_FLFID ?
						body->oa.o_parent_ver : 0,
					   POSTID(&body->oa.o_oi),
					   aa->aa_ppga[0]->off,
					   aa->aa_ppga[aa->aa_page_count-1]->off +
					   aa->aa_ppga[aa->aa_page_count-1]->count -
									1);
			CERROR("client %x, server %x, cksum_type %x\n",
			       client_cksum, server_cksum, cksum_type);
			cksum_counter = 0;
			aa->aa_oa->o_cksum = client_cksum;
			rc = -EAGAIN;
		} else {
			cksum_counter++;
			CDEBUG(D_PAGE, "checksum %x confirmed\n", client_cksum);
			rc = 0;
		}
	} else if (unlikely(client_cksum)) {
		static int cksum_missed;

		cksum_missed++;
		if ((cksum_missed & (-cksum_missed)) == cksum_missed)
			CERROR("Checksum %u requested from %s but not sent\n",
			       cksum_missed, libcfs_nid2str(peer->nid));
	} else {
		rc = 0;
	}
out:
	if (rc >= 0)
		lustre_get_wire_obdo(&req->rq_import->imp_connect_data,
				     aa->aa_oa, &body->oa);

	return rc;
}

static int osc_brw_internal(int cmd, struct obd_export *exp, struct obdo *oa,
			    struct lov_stripe_md *lsm,
			    obd_count page_count, struct brw_page **pga,
			    struct obd_capa *ocapa)
{
	struct ptlrpc_request *req;
	int		    rc;
	wait_queue_head_t	    waitq;
	int		    generation, resends = 0;
	struct l_wait_info     lwi;

	init_waitqueue_head(&waitq);
	generation = exp->exp_obd->u.cli.cl_import->imp_generation;

restart_bulk:
	rc = osc_brw_prep_request(cmd, &exp->exp_obd->u.cli, oa, lsm,
				  page_count, pga, &req, ocapa, 0, resends);
	if (rc != 0)
		return (rc);

	if (resends) {
		req->rq_generation_set = 1;
		req->rq_import_generation = generation;
		req->rq_sent = cfs_time_current_sec() + resends;
	}

	rc = ptlrpc_queue_wait(req);

	if (rc == -ETIMEDOUT && req->rq_resend) {
		DEBUG_REQ(D_HA, req,  "BULK TIMEOUT");
		ptlrpc_req_finished(req);
		goto restart_bulk;
	}

	rc = osc_brw_fini_request(req, rc);

	ptlrpc_req_finished(req);
	/* When server return -EINPROGRESS, client should always retry
	 * regardless of the number of times the bulk was resent already.*/
	if (osc_recoverable_error(rc)) {
		resends++;
		if (rc != -EINPROGRESS &&
		    !client_should_resend(resends, &exp->exp_obd->u.cli)) {
			CERROR("%s: too many resend retries for object: "
			       ""DOSTID", rc = %d.\n", exp->exp_obd->obd_name,
			       POSTID(&oa->o_oi), rc);
			goto out;
		}
		if (generation !=
		    exp->exp_obd->u.cli.cl_import->imp_generation) {
			CDEBUG(D_HA, "%s: resend cross eviction for object: "
			       ""DOSTID", rc = %d.\n", exp->exp_obd->obd_name,
			       POSTID(&oa->o_oi), rc);
			goto out;
		}

		lwi = LWI_TIMEOUT_INTR(cfs_time_seconds(resends), NULL, NULL,
				       NULL);
		l_wait_event(waitq, 0, &lwi);

		goto restart_bulk;
	}
out:
	if (rc == -EAGAIN || rc == -EINPROGRESS)
		rc = -EIO;
	return rc;
}

static int osc_brw_redo_request(struct ptlrpc_request *request,
				struct osc_brw_async_args *aa, int rc)
{
	struct ptlrpc_request *new_req;
	struct osc_brw_async_args *new_aa;
	struct osc_async_page *oap;

	DEBUG_REQ(rc == -EINPROGRESS ? D_RPCTRACE : D_ERROR, request,
		  "redo for recoverable error %d", rc);

	rc = osc_brw_prep_request(lustre_msg_get_opc(request->rq_reqmsg) ==
					OST_WRITE ? OBD_BRW_WRITE :OBD_BRW_READ,
				  aa->aa_cli, aa->aa_oa,
				  NULL /* lsm unused by osc currently */,
				  aa->aa_page_count, aa->aa_ppga,
				  &new_req, aa->aa_ocapa, 0, 1);
	if (rc)
		return rc;

	list_for_each_entry(oap, &aa->aa_oaps, oap_rpc_item) {
		if (oap->oap_request != NULL) {
			LASSERTF(request == oap->oap_request,
				 "request %p != oap_request %p\n",
				 request, oap->oap_request);
			if (oap->oap_interrupted) {
				ptlrpc_req_finished(new_req);
				return -EINTR;
			}
		}
	}
	/* New request takes over pga and oaps from old request.
	 * Note that copying a list_head doesn't work, need to move it... */
	aa->aa_resends++;
	new_req->rq_interpret_reply = request->rq_interpret_reply;
	new_req->rq_async_args = request->rq_async_args;
	/* cap resend delay to the current request timeout, this is similar to
	 * what ptlrpc does (see after_reply()) */
	if (aa->aa_resends > new_req->rq_timeout)
		new_req->rq_sent = cfs_time_current_sec() + new_req->rq_timeout;
	else
		new_req->rq_sent = cfs_time_current_sec() + aa->aa_resends;
	new_req->rq_generation_set = 1;
	new_req->rq_import_generation = request->rq_import_generation;

	new_aa = ptlrpc_req_async_args(new_req);

	INIT_LIST_HEAD(&new_aa->aa_oaps);
	list_splice_init(&aa->aa_oaps, &new_aa->aa_oaps);
	INIT_LIST_HEAD(&new_aa->aa_exts);
	list_splice_init(&aa->aa_exts, &new_aa->aa_exts);
	new_aa->aa_resends = aa->aa_resends;

	list_for_each_entry(oap, &new_aa->aa_oaps, oap_rpc_item) {
		if (oap->oap_request) {
			ptlrpc_req_finished(oap->oap_request);
			oap->oap_request = ptlrpc_request_addref(new_req);
		}
	}

	new_aa->aa_ocapa = aa->aa_ocapa;
	aa->aa_ocapa = NULL;

	/* XXX: This code will run into problem if we're going to support
	 * to add a series of BRW RPCs into a self-defined ptlrpc_request_set
	 * and wait for all of them to be finished. We should inherit request
	 * set from old request. */
	ptlrpcd_add_req(new_req, PDL_POLICY_SAME, -1);

	DEBUG_REQ(D_INFO, new_req, "new request");
	return 0;
}

/*
 * ugh, we want disk allocation on the target to happen in offset order.  we'll
 * follow sedgewicks advice and stick to the dead simple shellsort -- it'll do
 * fine for our small page arrays and doesn't require allocation.  its an
 * insertion sort that swaps elements that are strides apart, shrinking the
 * stride down until its '1' and the array is sorted.
 */
static void sort_brw_pages(struct brw_page **array, int num)
{
	int stride, i, j;
	struct brw_page *tmp;

	if (num == 1)
		return;
	for (stride = 1; stride < num ; stride = (stride * 3) + 1)
		;

	do {
		stride /= 3;
		for (i = stride ; i < num ; i++) {
			tmp = array[i];
			j = i;
			while (j >= stride && array[j - stride]->off > tmp->off) {
				array[j] = array[j - stride];
				j -= stride;
			}
			array[j] = tmp;
		}
	} while (stride > 1);
}

static obd_count max_unfragmented_pages(struct brw_page **pg, obd_count pages)
{
	int count = 1;
	int offset;
	int i = 0;

	LASSERT (pages > 0);
	offset = pg[i]->off & ~CFS_PAGE_MASK;

	for (;;) {
		pages--;
		if (pages == 0)	 /* that's all */
			return count;

		if (offset + pg[i]->count < PAGE_CACHE_SIZE)
			return count;   /* doesn't end on page boundary */

		i++;
		offset = pg[i]->off & ~CFS_PAGE_MASK;
		if (offset != 0)	/* doesn't start on page boundary */
			return count;

		count++;
	}
}

static struct brw_page **osc_build_ppga(struct brw_page *pga, obd_count count)
{
	struct brw_page **ppga;
	int i;

	OBD_ALLOC(ppga, sizeof(*ppga) * count);
	if (ppga == NULL)
		return NULL;

	for (i = 0; i < count; i++)
		ppga[i] = pga + i;
	return ppga;
}

static void osc_release_ppga(struct brw_page **ppga, obd_count count)
{
	LASSERT(ppga != NULL);
	OBD_FREE(ppga, sizeof(*ppga) * count);
}

static int osc_brw(int cmd, struct obd_export *exp, struct obd_info *oinfo,
		   obd_count page_count, struct brw_page *pga,
		   struct obd_trans_info *oti)
{
	struct obdo *saved_oa = NULL;
	struct brw_page **ppga, **orig;
	struct obd_import *imp = class_exp2cliimp(exp);
	struct client_obd *cli;
	int rc, page_count_orig;

	LASSERT((imp != NULL) && (imp->imp_obd != NULL));
	cli = &imp->imp_obd->u.cli;

	if (cmd & OBD_BRW_CHECK) {
		/* The caller just wants to know if there's a chance that this
		 * I/O can succeed */

		if (imp->imp_invalid)
			return -EIO;
		return 0;
	}

	/* test_brw with a failed create can trip this, maybe others. */
	LASSERT(cli->cl_max_pages_per_rpc);

	rc = 0;

	orig = ppga = osc_build_ppga(pga, page_count);
	if (ppga == NULL)
		return -ENOMEM;
	page_count_orig = page_count;

	sort_brw_pages(ppga, page_count);
	while (page_count) {
		obd_count pages_per_brw;

		if (page_count > cli->cl_max_pages_per_rpc)
			pages_per_brw = cli->cl_max_pages_per_rpc;
		else
			pages_per_brw = page_count;

		pages_per_brw = max_unfragmented_pages(ppga, pages_per_brw);

		if (saved_oa != NULL) {
			/* restore previously saved oa */
			*oinfo->oi_oa = *saved_oa;
		} else if (page_count > pages_per_brw) {
			/* save a copy of oa (brw will clobber it) */
			OBDO_ALLOC(saved_oa);
			if (saved_oa == NULL)
				GOTO(out, rc = -ENOMEM);
			*saved_oa = *oinfo->oi_oa;
		}

		rc = osc_brw_internal(cmd, exp, oinfo->oi_oa, oinfo->oi_md,
				      pages_per_brw, ppga, oinfo->oi_capa);

		if (rc != 0)
			break;

		page_count -= pages_per_brw;
		ppga += pages_per_brw;
	}

out:
	osc_release_ppga(orig, page_count_orig);

	if (saved_oa != NULL)
		OBDO_FREE(saved_oa);

	return rc;
}

static int brw_interpret(const struct lu_env *env,
			 struct ptlrpc_request *req, void *data, int rc)
{
	struct osc_brw_async_args *aa = data;
	struct osc_extent *ext;
	struct osc_extent *tmp;
	struct cl_object  *obj = NULL;
	struct client_obd *cli = aa->aa_cli;

	rc = osc_brw_fini_request(req, rc);
	CDEBUG(D_INODE, "request %p aa %p rc %d\n", req, aa, rc);
	/* When server return -EINPROGRESS, client should always retry
	 * regardless of the number of times the bulk was resent already. */
	if (osc_recoverable_error(rc)) {
		if (req->rq_import_generation !=
		    req->rq_import->imp_generation) {
			CDEBUG(D_HA, "%s: resend cross eviction for object: "
			       ""DOSTID", rc = %d.\n",
			       req->rq_import->imp_obd->obd_name,
			       POSTID(&aa->aa_oa->o_oi), rc);
		} else if (rc == -EINPROGRESS ||
		    client_should_resend(aa->aa_resends, aa->aa_cli)) {
			rc = osc_brw_redo_request(req, aa, rc);
		} else {
			CERROR("%s: too many resent retries for object: "
			       ""LPU64":"LPU64", rc = %d.\n",
			       req->rq_import->imp_obd->obd_name,
			       POSTID(&aa->aa_oa->o_oi), rc);
		}

		if (rc == 0)
			return 0;
		else if (rc == -EAGAIN || rc == -EINPROGRESS)
			rc = -EIO;
	}

	if (aa->aa_ocapa) {
		capa_put(aa->aa_ocapa);
		aa->aa_ocapa = NULL;
	}

	list_for_each_entry_safe(ext, tmp, &aa->aa_exts, oe_link) {
		if (obj == NULL && rc == 0) {
			obj = osc2cl(ext->oe_obj);
			cl_object_get(obj);
		}

		list_del_init(&ext->oe_link);
		osc_extent_finish(env, ext, 1, rc);
	}
	LASSERT(list_empty(&aa->aa_exts));
	LASSERT(list_empty(&aa->aa_oaps));

	if (obj != NULL) {
		struct obdo *oa = aa->aa_oa;
		struct cl_attr *attr  = &osc_env_info(env)->oti_attr;
		unsigned long valid = 0;

		LASSERT(rc == 0);
		if (oa->o_valid & OBD_MD_FLBLOCKS) {
			attr->cat_blocks = oa->o_blocks;
			valid |= CAT_BLOCKS;
		}
		if (oa->o_valid & OBD_MD_FLMTIME) {
			attr->cat_mtime = oa->o_mtime;
			valid |= CAT_MTIME;
		}
		if (oa->o_valid & OBD_MD_FLATIME) {
			attr->cat_atime = oa->o_atime;
			valid |= CAT_ATIME;
		}
		if (oa->o_valid & OBD_MD_FLCTIME) {
			attr->cat_ctime = oa->o_ctime;
			valid |= CAT_CTIME;
		}
		if (valid != 0) {
			cl_object_attr_lock(obj);
			cl_object_attr_set(env, obj, attr, valid);
			cl_object_attr_unlock(obj);
		}
		cl_object_put(env, obj);
	}
	OBDO_FREE(aa->aa_oa);

	cl_req_completion(env, aa->aa_clerq, rc < 0 ? rc :
			  req->rq_bulk->bd_nob_transferred);
	osc_release_ppga(aa->aa_ppga, aa->aa_page_count);
	ptlrpc_lprocfs_brw(req, req->rq_bulk->bd_nob_transferred);

	client_obd_list_lock(&cli->cl_loi_list_lock);
	/* We need to decrement before osc_ap_completion->osc_wake_cache_waiters
	 * is called so we know whether to go to sync BRWs or wait for more
	 * RPCs to complete */
	if (lustre_msg_get_opc(req->rq_reqmsg) == OST_WRITE)
		cli->cl_w_in_flight--;
	else
		cli->cl_r_in_flight--;
	osc_wake_cache_waiters(cli);
	client_obd_list_unlock(&cli->cl_loi_list_lock);

	osc_io_unplug(env, cli, NULL, PDL_POLICY_SAME);
	return rc;
}

/**
 * Build an RPC by the list of extent @ext_list. The caller must ensure
 * that the total pages in this list are NOT over max pages per RPC.
 * Extents in the list must be in OES_RPC state.
 */
int osc_build_rpc(const struct lu_env *env, struct client_obd *cli,
		  struct list_head *ext_list, int cmd, pdl_policy_t pol)
{
	struct ptlrpc_request		*req = NULL;
	struct osc_extent		*ext;
	struct brw_page			**pga = NULL;
	struct osc_brw_async_args	*aa = NULL;
	struct obdo			*oa = NULL;
	struct osc_async_page		*oap;
	struct osc_async_page		*tmp;
	struct cl_req			*clerq = NULL;
	enum cl_req_type		crt = (cmd & OBD_BRW_WRITE) ? CRT_WRITE :
								      CRT_READ;
	struct ldlm_lock		*lock = NULL;
	struct cl_req_attr		*crattr = NULL;
	obd_off				starting_offset = OBD_OBJECT_EOF;
	obd_off				ending_offset = 0;
	int				mpflag = 0;
	int				mem_tight = 0;
	int				page_count = 0;
	int				i;
	int				rc;
	LIST_HEAD(rpc_list);

	LASSERT(!list_empty(ext_list));

	/* add pages into rpc_list to build BRW rpc */
	list_for_each_entry(ext, ext_list, oe_link) {
		LASSERT(ext->oe_state == OES_RPC);
		mem_tight |= ext->oe_memalloc;
		list_for_each_entry(oap, &ext->oe_pages, oap_pending_item) {
			++page_count;
			list_add_tail(&oap->oap_rpc_item, &rpc_list);
			if (starting_offset > oap->oap_obj_off)
				starting_offset = oap->oap_obj_off;
			else
				LASSERT(oap->oap_page_off == 0);
			if (ending_offset < oap->oap_obj_off + oap->oap_count)
				ending_offset = oap->oap_obj_off +
						oap->oap_count;
			else
				LASSERT(oap->oap_page_off + oap->oap_count ==
					PAGE_CACHE_SIZE);
		}
	}

	if (mem_tight)
		mpflag = cfs_memory_pressure_get_and_set();

	OBD_ALLOC(crattr, sizeof(*crattr));
	if (crattr == NULL)
		GOTO(out, rc = -ENOMEM);

	OBD_ALLOC(pga, sizeof(*pga) * page_count);
	if (pga == NULL)
		GOTO(out, rc = -ENOMEM);

	OBDO_ALLOC(oa);
	if (oa == NULL)
		GOTO(out, rc = -ENOMEM);

	i = 0;
	list_for_each_entry(oap, &rpc_list, oap_rpc_item) {
		struct cl_page *page = oap2cl_page(oap);
		if (clerq == NULL) {
			clerq = cl_req_alloc(env, page, crt,
					     1 /* only 1-object rpcs for now */);
			if (IS_ERR(clerq))
				GOTO(out, rc = PTR_ERR(clerq));
			lock = oap->oap_ldlm_lock;
		}
		if (mem_tight)
			oap->oap_brw_flags |= OBD_BRW_MEMALLOC;
		pga[i] = &oap->oap_brw_page;
		pga[i]->off = oap->oap_obj_off + oap->oap_page_off;
		CDEBUG(0, "put page %p index %lu oap %p flg %x to pga\n",
		       pga[i]->pg, page_index(oap->oap_page), oap,
		       pga[i]->flag);
		i++;
		cl_req_page_add(env, clerq, page);
	}

	/* always get the data for the obdo for the rpc */
	LASSERT(clerq != NULL);
	crattr->cra_oa = oa;
	cl_req_attr_set(env, clerq, crattr, ~0ULL);
	if (lock) {
		oa->o_handle = lock->l_remote_handle;
		oa->o_valid |= OBD_MD_FLHANDLE;
	}

	rc = cl_req_prep(env, clerq);
	if (rc != 0) {
		CERROR("cl_req_prep failed: %d\n", rc);
		GOTO(out, rc);
	}

	sort_brw_pages(pga, page_count);
	rc = osc_brw_prep_request(cmd, cli, oa, NULL, page_count,
			pga, &req, crattr->cra_capa, 1, 0);
	if (rc != 0) {
		CERROR("prep_req failed: %d\n", rc);
		GOTO(out, rc);
	}

	req->rq_interpret_reply = brw_interpret;

	if (mem_tight != 0)
		req->rq_memalloc = 1;

	/* Need to update the timestamps after the request is built in case
	 * we race with setattr (locally or in queue at OST).  If OST gets
	 * later setattr before earlier BRW (as determined by the request xid),
	 * the OST will not use BRW timestamps.  Sadly, there is no obvious
	 * way to do this in a single call.  bug 10150 */
	cl_req_attr_set(env, clerq, crattr,
			OBD_MD_FLMTIME|OBD_MD_FLCTIME|OBD_MD_FLATIME);

	lustre_msg_set_jobid(req->rq_reqmsg, crattr->cra_jobid);

	CLASSERT(sizeof(*aa) <= sizeof(req->rq_async_args));
	aa = ptlrpc_req_async_args(req);
	INIT_LIST_HEAD(&aa->aa_oaps);
	list_splice_init(&rpc_list, &aa->aa_oaps);
	INIT_LIST_HEAD(&aa->aa_exts);
	list_splice_init(ext_list, &aa->aa_exts);
	aa->aa_clerq = clerq;

	/* queued sync pages can be torn down while the pages
	 * were between the pending list and the rpc */
	tmp = NULL;
	list_for_each_entry(oap, &aa->aa_oaps, oap_rpc_item) {
		/* only one oap gets a request reference */
		if (tmp == NULL)
			tmp = oap;
		if (oap->oap_interrupted && !req->rq_intr) {
			CDEBUG(D_INODE, "oap %p in req %p interrupted\n",
					oap, req);
			ptlrpc_mark_interrupted(req);
		}
	}
	if (tmp != NULL)
		tmp->oap_request = ptlrpc_request_addref(req);

	client_obd_list_lock(&cli->cl_loi_list_lock);
	starting_offset >>= PAGE_CACHE_SHIFT;
	if (cmd == OBD_BRW_READ) {
		cli->cl_r_in_flight++;
		lprocfs_oh_tally_log2(&cli->cl_read_page_hist, page_count);
		lprocfs_oh_tally(&cli->cl_read_rpc_hist, cli->cl_r_in_flight);
		lprocfs_oh_tally_log2(&cli->cl_read_offset_hist,
				      starting_offset + 1);
	} else {
		cli->cl_w_in_flight++;
		lprocfs_oh_tally_log2(&cli->cl_write_page_hist, page_count);
		lprocfs_oh_tally(&cli->cl_write_rpc_hist, cli->cl_w_in_flight);
		lprocfs_oh_tally_log2(&cli->cl_write_offset_hist,
				      starting_offset + 1);
	}
	client_obd_list_unlock(&cli->cl_loi_list_lock);

	DEBUG_REQ(D_INODE, req, "%d pages, aa %p. now %dr/%dw in flight",
		  page_count, aa, cli->cl_r_in_flight,
		  cli->cl_w_in_flight);

	/* XXX: Maybe the caller can check the RPC bulk descriptor to
	 * see which CPU/NUMA node the majority of pages were allocated
	 * on, and try to assign the async RPC to the CPU core
	 * (PDL_POLICY_PREFERRED) to reduce cross-CPU memory traffic.
	 *
	 * But on the other hand, we expect that multiple ptlrpcd
	 * threads and the initial write sponsor can run in parallel,
	 * especially when data checksum is enabled, which is CPU-bound
	 * operation and single ptlrpcd thread cannot process in time.
	 * So more ptlrpcd threads sharing BRW load
	 * (with PDL_POLICY_ROUND) seems better.
	 */
	ptlrpcd_add_req(req, pol, -1);
	rc = 0;

out:
	if (mem_tight != 0)
		cfs_memory_pressure_restore(mpflag);

	if (crattr != NULL) {
		capa_put(crattr->cra_capa);
		OBD_FREE(crattr, sizeof(*crattr));
	}

	if (rc != 0) {
		LASSERT(req == NULL);

		if (oa)
			OBDO_FREE(oa);
		if (pga)
			OBD_FREE(pga, sizeof(*pga) * page_count);
		/* this should happen rarely and is pretty bad, it makes the
		 * pending list not follow the dirty order */
		while (!list_empty(ext_list)) {
			ext = list_entry(ext_list->next, struct osc_extent,
					     oe_link);
			list_del_init(&ext->oe_link);
			osc_extent_finish(env, ext, 0, rc);
		}
		if (clerq && !IS_ERR(clerq))
			cl_req_completion(env, clerq, rc);
	}
	return rc;
}

static int osc_set_lock_data_with_check(struct ldlm_lock *lock,
					struct ldlm_enqueue_info *einfo)
{
	void *data = einfo->ei_cbdata;
	int set = 0;

	LASSERT(lock != NULL);
	LASSERT(lock->l_blocking_ast == einfo->ei_cb_bl);
	LASSERT(lock->l_resource->lr_type == einfo->ei_type);
	LASSERT(lock->l_completion_ast == einfo->ei_cb_cp);
	LASSERT(lock->l_glimpse_ast == einfo->ei_cb_gl);

	lock_res_and_lock(lock);
	spin_lock(&osc_ast_guard);

	if (lock->l_ast_data == NULL)
		lock->l_ast_data = data;
	if (lock->l_ast_data == data)
		set = 1;

	spin_unlock(&osc_ast_guard);
	unlock_res_and_lock(lock);

	return set;
}

static int osc_set_data_with_check(struct lustre_handle *lockh,
				   struct ldlm_enqueue_info *einfo)
{
	struct ldlm_lock *lock = ldlm_handle2lock(lockh);
	int set = 0;

	if (lock != NULL) {
		set = osc_set_lock_data_with_check(lock, einfo);
		LDLM_LOCK_PUT(lock);
	} else
		CERROR("lockh %p, data %p - client evicted?\n",
		       lockh, einfo->ei_cbdata);
	return set;
}

static int osc_change_cbdata(struct obd_export *exp, struct lov_stripe_md *lsm,
			     ldlm_iterator_t replace, void *data)
{
	struct ldlm_res_id res_id;
	struct obd_device *obd = class_exp2obd(exp);

	ostid_build_res_name(&lsm->lsm_oi, &res_id);
	ldlm_resource_iterate(obd->obd_namespace, &res_id, replace, data);
	return 0;
}

/* find any ldlm lock of the inode in osc
 * return 0    not find
 *	1    find one
 *      < 0    error */
static int osc_find_cbdata(struct obd_export *exp, struct lov_stripe_md *lsm,
			   ldlm_iterator_t replace, void *data)
{
	struct ldlm_res_id res_id;
	struct obd_device *obd = class_exp2obd(exp);
	int rc = 0;

	ostid_build_res_name(&lsm->lsm_oi, &res_id);
	rc = ldlm_resource_iterate(obd->obd_namespace, &res_id, replace, data);
	if (rc == LDLM_ITER_STOP)
		return(1);
	if (rc == LDLM_ITER_CONTINUE)
		return(0);
	return(rc);
}

static int osc_enqueue_fini(struct ptlrpc_request *req, struct ost_lvb *lvb,
			    obd_enqueue_update_f upcall, void *cookie,
			    __u64 *flags, int agl, int rc)
{
	int intent = *flags & LDLM_FL_HAS_INTENT;

	if (intent) {
		/* The request was created before ldlm_cli_enqueue call. */
		if (rc == ELDLM_LOCK_ABORTED) {
			struct ldlm_reply *rep;
			rep = req_capsule_server_get(&req->rq_pill,
						     &RMF_DLM_REP);

			LASSERT(rep != NULL);
			rep->lock_policy_res1 =
				ptlrpc_status_ntoh(rep->lock_policy_res1);
			if (rep->lock_policy_res1)
				rc = rep->lock_policy_res1;
		}
	}

	if ((intent != 0 && rc == ELDLM_LOCK_ABORTED && agl == 0) ||
	    (rc == 0)) {
		*flags |= LDLM_FL_LVB_READY;
		CDEBUG(D_INODE,"got kms "LPU64" blocks "LPU64" mtime "LPU64"\n",
		       lvb->lvb_size, lvb->lvb_blocks, lvb->lvb_mtime);
	}

	/* Call the update callback. */
	rc = (*upcall)(cookie, rc);
	return rc;
}

static int osc_enqueue_interpret(const struct lu_env *env,
				 struct ptlrpc_request *req,
				 struct osc_enqueue_args *aa, int rc)
{
	struct ldlm_lock *lock;
	struct lustre_handle handle;
	__u32 mode;
	struct ost_lvb *lvb;
	__u32 lvb_len;
	__u64 *flags = aa->oa_flags;

	/* Make a local copy of a lock handle and a mode, because aa->oa_*
	 * might be freed anytime after lock upcall has been called. */
	lustre_handle_copy(&handle, aa->oa_lockh);
	mode = aa->oa_ei->ei_mode;

	/* ldlm_cli_enqueue is holding a reference on the lock, so it must
	 * be valid. */
	lock = ldlm_handle2lock(&handle);

	/* Take an additional reference so that a blocking AST that
	 * ldlm_cli_enqueue_fini() might post for a failed lock, is guaranteed
	 * to arrive after an upcall has been executed by
	 * osc_enqueue_fini(). */
	ldlm_lock_addref(&handle, mode);

	/* Let CP AST to grant the lock first. */
	OBD_FAIL_TIMEOUT(OBD_FAIL_OSC_CP_ENQ_RACE, 1);

	if (aa->oa_agl && rc == ELDLM_LOCK_ABORTED) {
		lvb = NULL;
		lvb_len = 0;
	} else {
		lvb = aa->oa_lvb;
		lvb_len = sizeof(*aa->oa_lvb);
	}

	/* Complete obtaining the lock procedure. */
	rc = ldlm_cli_enqueue_fini(aa->oa_exp, req, aa->oa_ei->ei_type, 1,
				   mode, flags, lvb, lvb_len, &handle, rc);
	/* Complete osc stuff. */
	rc = osc_enqueue_fini(req, aa->oa_lvb, aa->oa_upcall, aa->oa_cookie,
			      flags, aa->oa_agl, rc);

	OBD_FAIL_TIMEOUT(OBD_FAIL_OSC_CP_CANCEL_RACE, 10);

	/* Release the lock for async request. */
	if (lustre_handle_is_used(&handle) && rc == ELDLM_OK)
		/*
		 * Releases a reference taken by ldlm_cli_enqueue(), if it is
		 * not already released by
		 * ldlm_cli_enqueue_fini()->failed_lock_cleanup()
		 */
		ldlm_lock_decref(&handle, mode);

	LASSERTF(lock != NULL, "lockh %p, req %p, aa %p - client evicted?\n",
		 aa->oa_lockh, req, aa);
	ldlm_lock_decref(&handle, mode);
	LDLM_LOCK_PUT(lock);
	return rc;
}

void osc_update_enqueue(struct lustre_handle *lov_lockhp,
			struct lov_oinfo *loi, __u64 flags,
			struct ost_lvb *lvb, __u32 mode, int rc)
{
	struct ldlm_lock *lock = ldlm_handle2lock(lov_lockhp);

	if (rc == ELDLM_OK) {
		__u64 tmp;

		LASSERT(lock != NULL);
		loi->loi_lvb = *lvb;
		tmp = loi->loi_lvb.lvb_size;
		/* Extend KMS up to the end of this lock and no further
		 * A lock on [x,y] means a KMS of up to y + 1 bytes! */
		if (tmp > lock->l_policy_data.l_extent.end)
			tmp = lock->l_policy_data.l_extent.end + 1;
		if (tmp >= loi->loi_kms) {
			LDLM_DEBUG(lock, "lock acquired, setting rss="LPU64
				   ", kms="LPU64, loi->loi_lvb.lvb_size, tmp);
			loi_kms_set(loi, tmp);
		} else {
			LDLM_DEBUG(lock, "lock acquired, setting rss="
				   LPU64"; leaving kms="LPU64", end="LPU64,
				   loi->loi_lvb.lvb_size, loi->loi_kms,
				   lock->l_policy_data.l_extent.end);
		}
		ldlm_lock_allow_match(lock);
	} else if (rc == ELDLM_LOCK_ABORTED && (flags & LDLM_FL_HAS_INTENT)) {
		LASSERT(lock != NULL);
		loi->loi_lvb = *lvb;
		ldlm_lock_allow_match(lock);
		CDEBUG(D_INODE, "glimpsed, setting rss="LPU64"; leaving"
		       " kms="LPU64"\n", loi->loi_lvb.lvb_size, loi->loi_kms);
		rc = ELDLM_OK;
	}

	if (lock != NULL) {
		if (rc != ELDLM_OK)
			ldlm_lock_fail_match(lock);

		LDLM_LOCK_PUT(lock);
	}
}
EXPORT_SYMBOL(osc_update_enqueue);

struct ptlrpc_request_set *PTLRPCD_SET = (void *)1;

/* When enqueuing asynchronously, locks are not ordered, we can obtain a lock
 * from the 2nd OSC before a lock from the 1st one. This does not deadlock with
 * other synchronous requests, however keeping some locks and trying to obtain
 * others may take a considerable amount of time in a case of ost failure; and
 * when other sync requests do not get released lock from a client, the client
 * is excluded from the cluster -- such scenarious make the life difficult, so
 * release locks just after they are obtained. */
int osc_enqueue_base(struct obd_export *exp, struct ldlm_res_id *res_id,
		     __u64 *flags, ldlm_policy_data_t *policy,
		     struct ost_lvb *lvb, int kms_valid,
		     obd_enqueue_update_f upcall, void *cookie,
		     struct ldlm_enqueue_info *einfo,
		     struct lustre_handle *lockh,
		     struct ptlrpc_request_set *rqset, int async, int agl)
{
	struct obd_device *obd = exp->exp_obd;
	struct ptlrpc_request *req = NULL;
	int intent = *flags & LDLM_FL_HAS_INTENT;
	__u64 match_lvb = (agl != 0 ? 0 : LDLM_FL_LVB_READY);
	ldlm_mode_t mode;
	int rc;

	/* Filesystem lock extents are extended to page boundaries so that
	 * dealing with the page cache is a little smoother.  */
	policy->l_extent.start -= policy->l_extent.start & ~CFS_PAGE_MASK;
	policy->l_extent.end |= ~CFS_PAGE_MASK;

	/*
	 * kms is not valid when either object is completely fresh (so that no
	 * locks are cached), or object was evicted. In the latter case cached
	 * lock cannot be used, because it would prime inode state with
	 * potentially stale LVB.
	 */
	if (!kms_valid)
		goto no_match;

	/* Next, search for already existing extent locks that will cover us */
	/* If we're trying to read, we also search for an existing PW lock.  The
	 * VFS and page cache already protect us locally, so lots of readers/
	 * writers can share a single PW lock.
	 *
	 * There are problems with conversion deadlocks, so instead of
	 * converting a read lock to a write lock, we'll just enqueue a new
	 * one.
	 *
	 * At some point we should cancel the read lock instead of making them
	 * send us a blocking callback, but there are problems with canceling
	 * locks out from other users right now, too. */
	mode = einfo->ei_mode;
	if (einfo->ei_mode == LCK_PR)
		mode |= LCK_PW;
	mode = ldlm_lock_match(obd->obd_namespace, *flags | match_lvb, res_id,
			       einfo->ei_type, policy, mode, lockh, 0);
	if (mode) {
		struct ldlm_lock *matched = ldlm_handle2lock(lockh);

		if ((agl != 0) && !(matched->l_flags & LDLM_FL_LVB_READY)) {
			/* For AGL, if enqueue RPC is sent but the lock is not
			 * granted, then skip to process this strpe.
			 * Return -ECANCELED to tell the caller. */
			ldlm_lock_decref(lockh, mode);
			LDLM_LOCK_PUT(matched);
			return -ECANCELED;
		} else if (osc_set_lock_data_with_check(matched, einfo)) {
			*flags |= LDLM_FL_LVB_READY;
			/* addref the lock only if not async requests and PW
			 * lock is matched whereas we asked for PR. */
			if (!rqset && einfo->ei_mode != mode)
				ldlm_lock_addref(lockh, LCK_PR);
			if (intent) {
				/* I would like to be able to ASSERT here that
				 * rss <= kms, but I can't, for reasons which
				 * are explained in lov_enqueue() */
			}

			/* We already have a lock, and it's referenced.
			 *
			 * At this point, the cl_lock::cll_state is CLS_QUEUING,
			 * AGL upcall may change it to CLS_HELD directly. */
			(*upcall)(cookie, ELDLM_OK);

			if (einfo->ei_mode != mode)
				ldlm_lock_decref(lockh, LCK_PW);
			else if (rqset)
				/* For async requests, decref the lock. */
				ldlm_lock_decref(lockh, einfo->ei_mode);
			LDLM_LOCK_PUT(matched);
			return ELDLM_OK;
		} else {
			ldlm_lock_decref(lockh, mode);
			LDLM_LOCK_PUT(matched);
		}
	}

 no_match:
	if (intent) {
		LIST_HEAD(cancels);
		req = ptlrpc_request_alloc(class_exp2cliimp(exp),
					   &RQF_LDLM_ENQUEUE_LVB);
		if (req == NULL)
			return -ENOMEM;

		rc = ldlm_prep_enqueue_req(exp, req, &cancels, 0);
		if (rc) {
			ptlrpc_request_free(req);
			return rc;
		}

		req_capsule_set_size(&req->rq_pill, &RMF_DLM_LVB, RCL_SERVER,
				     sizeof(*lvb));
		ptlrpc_request_set_replen(req);
	}

	/* users of osc_enqueue() can pass this flag for ldlm_lock_match() */
	*flags &= ~LDLM_FL_BLOCK_GRANTED;

	rc = ldlm_cli_enqueue(exp, &req, einfo, res_id, policy, flags, lvb,
			      sizeof(*lvb), LVB_T_OST, lockh, async);
	if (rqset) {
		if (!rc) {
			struct osc_enqueue_args *aa;
			CLASSERT (sizeof(*aa) <= sizeof(req->rq_async_args));
			aa = ptlrpc_req_async_args(req);
			aa->oa_ei = einfo;
			aa->oa_exp = exp;
			aa->oa_flags  = flags;
			aa->oa_upcall = upcall;
			aa->oa_cookie = cookie;
			aa->oa_lvb    = lvb;
			aa->oa_lockh  = lockh;
			aa->oa_agl    = !!agl;

			req->rq_interpret_reply =
				(ptlrpc_interpterer_t)osc_enqueue_interpret;
			if (rqset == PTLRPCD_SET)
				ptlrpcd_add_req(req, PDL_POLICY_ROUND, -1);
			else
				ptlrpc_set_add_req(rqset, req);
		} else if (intent) {
			ptlrpc_req_finished(req);
		}
		return rc;
	}

	rc = osc_enqueue_fini(req, lvb, upcall, cookie, flags, agl, rc);
	if (intent)
		ptlrpc_req_finished(req);

	return rc;
}

static int osc_enqueue(struct obd_export *exp, struct obd_info *oinfo,
		       struct ldlm_enqueue_info *einfo,
		       struct ptlrpc_request_set *rqset)
{
	struct ldlm_res_id res_id;
	int rc;

	ostid_build_res_name(&oinfo->oi_md->lsm_oi, &res_id);
	rc = osc_enqueue_base(exp, &res_id, &oinfo->oi_flags, &oinfo->oi_policy,
			      &oinfo->oi_md->lsm_oinfo[0]->loi_lvb,
			      oinfo->oi_md->lsm_oinfo[0]->loi_kms_valid,
			      oinfo->oi_cb_up, oinfo, einfo, oinfo->oi_lockh,
			      rqset, rqset != NULL, 0);
	return rc;
}

int osc_match_base(struct obd_export *exp, struct ldlm_res_id *res_id,
		   __u32 type, ldlm_policy_data_t *policy, __u32 mode,
		   __u64 *flags, void *data, struct lustre_handle *lockh,
		   int unref)
{
	struct obd_device *obd = exp->exp_obd;
	__u64 lflags = *flags;
	ldlm_mode_t rc;

	if (OBD_FAIL_CHECK(OBD_FAIL_OSC_MATCH))
		return -EIO;

	/* Filesystem lock extents are extended to page boundaries so that
	 * dealing with the page cache is a little smoother */
	policy->l_extent.start -= policy->l_extent.start & ~CFS_PAGE_MASK;
	policy->l_extent.end |= ~CFS_PAGE_MASK;

	/* Next, search for already existing extent locks that will cover us */
	/* If we're trying to read, we also search for an existing PW lock.  The
	 * VFS and page cache already protect us locally, so lots of readers/
	 * writers can share a single PW lock. */
	rc = mode;
	if (mode == LCK_PR)
		rc |= LCK_PW;
	rc = ldlm_lock_match(obd->obd_namespace, lflags,
			     res_id, type, policy, rc, lockh, unref);
	if (rc) {
		if (data != NULL) {
			if (!osc_set_data_with_check(lockh, data)) {
				if (!(lflags & LDLM_FL_TEST_LOCK))
					ldlm_lock_decref(lockh, rc);
				return 0;
			}
		}
		if (!(lflags & LDLM_FL_TEST_LOCK) && mode != rc) {
			ldlm_lock_addref(lockh, LCK_PR);
			ldlm_lock_decref(lockh, LCK_PW);
		}
		return rc;
	}
	return rc;
}

int osc_cancel_base(struct lustre_handle *lockh, __u32 mode)
{
	if (unlikely(mode == LCK_GROUP))
		ldlm_lock_decref_and_cancel(lockh, mode);
	else
		ldlm_lock_decref(lockh, mode);

	return 0;
}

static int osc_cancel(struct obd_export *exp, struct lov_stripe_md *md,
		      __u32 mode, struct lustre_handle *lockh)
{
	return osc_cancel_base(lockh, mode);
}

static int osc_cancel_unused(struct obd_export *exp,
			     struct lov_stripe_md *lsm,
			     ldlm_cancel_flags_t flags,
			     void *opaque)
{
	struct obd_device *obd = class_exp2obd(exp);
	struct ldlm_res_id res_id, *resp = NULL;

	if (lsm != NULL) {
		ostid_build_res_name(&lsm->lsm_oi, &res_id);
		resp = &res_id;
	}

	return ldlm_cli_cancel_unused(obd->obd_namespace, resp, flags, opaque);
}

static int osc_statfs_interpret(const struct lu_env *env,
				struct ptlrpc_request *req,
				struct osc_async_args *aa, int rc)
{
	struct obd_statfs *msfs;

	if (rc == -EBADR)
		/* The request has in fact never been sent
		 * due to issues at a higher level (LOV).
		 * Exit immediately since the caller is
		 * aware of the problem and takes care
		 * of the clean up */
		 return rc;

	if ((rc == -ENOTCONN || rc == -EAGAIN) &&
	    (aa->aa_oi->oi_flags & OBD_STATFS_NODELAY))
		GOTO(out, rc = 0);

	if (rc != 0)
		GOTO(out, rc);

	msfs = req_capsule_server_get(&req->rq_pill, &RMF_OBD_STATFS);
	if (msfs == NULL) {
		GOTO(out, rc = -EPROTO);
	}

	*aa->aa_oi->oi_osfs = *msfs;
out:
	rc = aa->aa_oi->oi_cb_up(aa->aa_oi, rc);
	return rc;
}

static int osc_statfs_async(struct obd_export *exp,
			    struct obd_info *oinfo, __u64 max_age,
			    struct ptlrpc_request_set *rqset)
{
	struct obd_device     *obd = class_exp2obd(exp);
	struct ptlrpc_request *req;
	struct osc_async_args *aa;
	int		    rc;

	/* We could possibly pass max_age in the request (as an absolute
	 * timestamp or a "seconds.usec ago") so the target can avoid doing
	 * extra calls into the filesystem if that isn't necessary (e.g.
	 * during mount that would help a bit).  Having relative timestamps
	 * is not so great if request processing is slow, while absolute
	 * timestamps are not ideal because they need time synchronization. */
	req = ptlrpc_request_alloc(obd->u.cli.cl_import, &RQF_OST_STATFS);
	if (req == NULL)
		return -ENOMEM;

	rc = ptlrpc_request_pack(req, LUSTRE_OST_VERSION, OST_STATFS);
	if (rc) {
		ptlrpc_request_free(req);
		return rc;
	}
	ptlrpc_request_set_replen(req);
	req->rq_request_portal = OST_CREATE_PORTAL;
	ptlrpc_at_set_req_timeout(req);

	if (oinfo->oi_flags & OBD_STATFS_NODELAY) {
		/* procfs requests not want stat in wait for avoid deadlock */
		req->rq_no_resend = 1;
		req->rq_no_delay = 1;
	}

	req->rq_interpret_reply = (ptlrpc_interpterer_t)osc_statfs_interpret;
	CLASSERT (sizeof(*aa) <= sizeof(req->rq_async_args));
	aa = ptlrpc_req_async_args(req);
	aa->aa_oi = oinfo;

	ptlrpc_set_add_req(rqset, req);
	return 0;
}

static int osc_statfs(const struct lu_env *env, struct obd_export *exp,
		      struct obd_statfs *osfs, __u64 max_age, __u32 flags)
{
	struct obd_device     *obd = class_exp2obd(exp);
	struct obd_statfs     *msfs;
	struct ptlrpc_request *req;
	struct obd_import     *imp = NULL;
	int rc;

	/*Since the request might also come from lprocfs, so we need
	 *sync this with client_disconnect_export Bug15684*/
	down_read(&obd->u.cli.cl_sem);
	if (obd->u.cli.cl_import)
		imp = class_import_get(obd->u.cli.cl_import);
	up_read(&obd->u.cli.cl_sem);
	if (!imp)
		return -ENODEV;

	/* We could possibly pass max_age in the request (as an absolute
	 * timestamp or a "seconds.usec ago") so the target can avoid doing
	 * extra calls into the filesystem if that isn't necessary (e.g.
	 * during mount that would help a bit).  Having relative timestamps
	 * is not so great if request processing is slow, while absolute
	 * timestamps are not ideal because they need time synchronization. */
	req = ptlrpc_request_alloc(imp, &RQF_OST_STATFS);

	class_import_put(imp);

	if (req == NULL)
		return -ENOMEM;

	rc = ptlrpc_request_pack(req, LUSTRE_OST_VERSION, OST_STATFS);
	if (rc) {
		ptlrpc_request_free(req);
		return rc;
	}
	ptlrpc_request_set_replen(req);
	req->rq_request_portal = OST_CREATE_PORTAL;
	ptlrpc_at_set_req_timeout(req);

	if (flags & OBD_STATFS_NODELAY) {
		/* procfs requests not want stat in wait for avoid deadlock */
		req->rq_no_resend = 1;
		req->rq_no_delay = 1;
	}

	rc = ptlrpc_queue_wait(req);
	if (rc)
		GOTO(out, rc);

	msfs = req_capsule_server_get(&req->rq_pill, &RMF_OBD_STATFS);
	if (msfs == NULL) {
		GOTO(out, rc = -EPROTO);
	}

	*osfs = *msfs;

 out:
	ptlrpc_req_finished(req);
	return rc;
}

/* Retrieve object striping information.
 *
 * @lmmu is a pointer to an in-core struct with lmm_ost_count indicating
 * the maximum number of OST indices which will fit in the user buffer.
 * lmm_magic must be LOV_MAGIC (we only use 1 slot here).
 */
static int osc_getstripe(struct lov_stripe_md *lsm, struct lov_user_md *lump)
{
	/* we use lov_user_md_v3 because it is larger than lov_user_md_v1 */
	struct lov_user_md_v3 lum, *lumk;
	struct lov_user_ost_data_v1 *lmm_objects;
	int rc = 0, lum_size;

	if (!lsm)
		return -ENODATA;

	/* we only need the header part from user space to get lmm_magic and
	 * lmm_stripe_count, (the header part is common to v1 and v3) */
	lum_size = sizeof(struct lov_user_md_v1);
	if (copy_from_user(&lum, lump, lum_size))
		return -EFAULT;

	if ((lum.lmm_magic != LOV_USER_MAGIC_V1) &&
	    (lum.lmm_magic != LOV_USER_MAGIC_V3))
		return -EINVAL;

	/* lov_user_md_vX and lov_mds_md_vX must have the same size */
	LASSERT(sizeof(struct lov_user_md_v1) == sizeof(struct lov_mds_md_v1));
	LASSERT(sizeof(struct lov_user_md_v3) == sizeof(struct lov_mds_md_v3));
	LASSERT(sizeof(lum.lmm_objects[0]) == sizeof(lumk->lmm_objects[0]));

	/* we can use lov_mds_md_size() to compute lum_size
	 * because lov_user_md_vX and lov_mds_md_vX have the same size */
	if (lum.lmm_stripe_count > 0) {
		lum_size = lov_mds_md_size(lum.lmm_stripe_count, lum.lmm_magic);
		OBD_ALLOC(lumk, lum_size);
		if (!lumk)
			return -ENOMEM;

		if (lum.lmm_magic == LOV_USER_MAGIC_V1)
			lmm_objects =
			    &(((struct lov_user_md_v1 *)lumk)->lmm_objects[0]);
		else
			lmm_objects = &(lumk->lmm_objects[0]);
		lmm_objects->l_ost_oi = lsm->lsm_oi;
	} else {
		lum_size = lov_mds_md_size(0, lum.lmm_magic);
		lumk = &lum;
	}

	lumk->lmm_oi = lsm->lsm_oi;
	lumk->lmm_stripe_count = 1;

	if (copy_to_user(lump, lumk, lum_size))
		rc = -EFAULT;

	if (lumk != &lum)
		OBD_FREE(lumk, lum_size);

	return rc;
}


static int osc_iocontrol(unsigned int cmd, struct obd_export *exp, int len,
			 void *karg, void *uarg)
{
	struct obd_device *obd = exp->exp_obd;
	struct obd_ioctl_data *data = karg;
	int err = 0;

	if (!try_module_get(THIS_MODULE)) {
		CERROR("Can't get module. Is it alive?");
		return -EINVAL;
	}
	switch (cmd) {
	case OBD_IOC_LOV_GET_CONFIG: {
		char *buf;
		struct lov_desc *desc;
		struct obd_uuid uuid;

		buf = NULL;
		len = 0;
		if (obd_ioctl_getdata(&buf, &len, (void *)uarg))
			GOTO(out, err = -EINVAL);

		data = (struct obd_ioctl_data *)buf;

		if (sizeof(*desc) > data->ioc_inllen1) {
			obd_ioctl_freedata(buf, len);
			GOTO(out, err = -EINVAL);
		}

		if (data->ioc_inllen2 < sizeof(uuid)) {
			obd_ioctl_freedata(buf, len);
			GOTO(out, err = -EINVAL);
		}

		desc = (struct lov_desc *)data->ioc_inlbuf1;
		desc->ld_tgt_count = 1;
		desc->ld_active_tgt_count = 1;
		desc->ld_default_stripe_count = 1;
		desc->ld_default_stripe_size = 0;
		desc->ld_default_stripe_offset = 0;
		desc->ld_pattern = 0;
		memcpy(&desc->ld_uuid, &obd->obd_uuid, sizeof(uuid));

		memcpy(data->ioc_inlbuf2, &obd->obd_uuid, sizeof(uuid));

		err = copy_to_user((void *)uarg, buf, len);
		if (err)
			err = -EFAULT;
		obd_ioctl_freedata(buf, len);
		GOTO(out, err);
	}
	case LL_IOC_LOV_SETSTRIPE:
		err = obd_alloc_memmd(exp, karg);
		if (err > 0)
			err = 0;
		GOTO(out, err);
	case LL_IOC_LOV_GETSTRIPE:
		err = osc_getstripe(karg, uarg);
		GOTO(out, err);
	case OBD_IOC_CLIENT_RECOVER:
		err = ptlrpc_recover_import(obd->u.cli.cl_import,
					    data->ioc_inlbuf1, 0);
		if (err > 0)
			err = 0;
		GOTO(out, err);
	case IOC_OSC_SET_ACTIVE:
		err = ptlrpc_set_import_active(obd->u.cli.cl_import,
					       data->ioc_offset);
		GOTO(out, err);
	case OBD_IOC_POLL_QUOTACHECK:
		err = osc_quota_poll_check(exp, (struct if_quotacheck *)karg);
		GOTO(out, err);
	case OBD_IOC_PING_TARGET:
		err = ptlrpc_obd_ping(obd);
		GOTO(out, err);
	default:
		CDEBUG(D_INODE, "unrecognised ioctl %#x by %s\n",
		       cmd, current_comm());
		GOTO(out, err = -ENOTTY);
	}
out:
	module_put(THIS_MODULE);
	return err;
}

static int osc_get_info(const struct lu_env *env, struct obd_export *exp,
			obd_count keylen, void *key, __u32 *vallen, void *val,
			struct lov_stripe_md *lsm)
{
	if (!vallen || !val)
		return -EFAULT;

	if (KEY_IS(KEY_LOCK_TO_STRIPE)) {
		__u32 *stripe = val;
		*vallen = sizeof(*stripe);
		*stripe = 0;
		return 0;
	} else if (KEY_IS(KEY_LAST_ID)) {
		struct ptlrpc_request *req;
		obd_id		*reply;
		char		  *tmp;
		int		    rc;

		req = ptlrpc_request_alloc(class_exp2cliimp(exp),
					   &RQF_OST_GET_INFO_LAST_ID);
		if (req == NULL)
			return -ENOMEM;

		req_capsule_set_size(&req->rq_pill, &RMF_SETINFO_KEY,
				     RCL_CLIENT, keylen);
		rc = ptlrpc_request_pack(req, LUSTRE_OST_VERSION, OST_GET_INFO);
		if (rc) {
			ptlrpc_request_free(req);
			return rc;
		}

		tmp = req_capsule_client_get(&req->rq_pill, &RMF_SETINFO_KEY);
		memcpy(tmp, key, keylen);

		req->rq_no_delay = req->rq_no_resend = 1;
		ptlrpc_request_set_replen(req);
		rc = ptlrpc_queue_wait(req);
		if (rc)
			GOTO(out, rc);

		reply = req_capsule_server_get(&req->rq_pill, &RMF_OBD_ID);
		if (reply == NULL)
			GOTO(out, rc = -EPROTO);

		*((obd_id *)val) = *reply;
	out:
		ptlrpc_req_finished(req);
		return rc;
	} else if (KEY_IS(KEY_FIEMAP)) {
		struct ll_fiemap_info_key *fm_key =
				(struct ll_fiemap_info_key *)key;
		struct ldlm_res_id	 res_id;
		ldlm_policy_data_t	 policy;
		struct lustre_handle	 lockh;
		ldlm_mode_t		 mode = 0;
		struct ptlrpc_request	*req;
		struct ll_user_fiemap	*reply;
		char			*tmp;
		int			 rc;

		if (!(fm_key->fiemap.fm_flags & FIEMAP_FLAG_SYNC))
			goto skip_locking;

		policy.l_extent.start = fm_key->fiemap.fm_start &
						CFS_PAGE_MASK;

		if (OBD_OBJECT_EOF - fm_key->fiemap.fm_length <=
		    fm_key->fiemap.fm_start + PAGE_CACHE_SIZE - 1)
			policy.l_extent.end = OBD_OBJECT_EOF;
		else
			policy.l_extent.end = (fm_key->fiemap.fm_start +
				fm_key->fiemap.fm_length +
				PAGE_CACHE_SIZE - 1) & CFS_PAGE_MASK;

		ostid_build_res_name(&fm_key->oa.o_oi, &res_id);
		mode = ldlm_lock_match(exp->exp_obd->obd_namespace,
				       LDLM_FL_BLOCK_GRANTED |
				       LDLM_FL_LVB_READY,
				       &res_id, LDLM_EXTENT, &policy,
				       LCK_PR | LCK_PW, &lockh, 0);
		if (mode) { /* lock is cached on client */
			if (mode != LCK_PR) {
				ldlm_lock_addref(&lockh, LCK_PR);
				ldlm_lock_decref(&lockh, LCK_PW);
			}
		} else { /* no cached lock, needs acquire lock on server side */
			fm_key->oa.o_valid |= OBD_MD_FLFLAGS;
			fm_key->oa.o_flags |= OBD_FL_SRVLOCK;
		}

skip_locking:
		req = ptlrpc_request_alloc(class_exp2cliimp(exp),
					   &RQF_OST_GET_INFO_FIEMAP);
		if (req == NULL)
			GOTO(drop_lock, rc = -ENOMEM);

		req_capsule_set_size(&req->rq_pill, &RMF_FIEMAP_KEY,
				     RCL_CLIENT, keylen);
		req_capsule_set_size(&req->rq_pill, &RMF_FIEMAP_VAL,
				     RCL_CLIENT, *vallen);
		req_capsule_set_size(&req->rq_pill, &RMF_FIEMAP_VAL,
				     RCL_SERVER, *vallen);

		rc = ptlrpc_request_pack(req, LUSTRE_OST_VERSION, OST_GET_INFO);
		if (rc) {
			ptlrpc_request_free(req);
			GOTO(drop_lock, rc);
		}

		tmp = req_capsule_client_get(&req->rq_pill, &RMF_FIEMAP_KEY);
		memcpy(tmp, key, keylen);
		tmp = req_capsule_client_get(&req->rq_pill, &RMF_FIEMAP_VAL);
		memcpy(tmp, val, *vallen);

		ptlrpc_request_set_replen(req);
		rc = ptlrpc_queue_wait(req);
		if (rc)
			GOTO(fini_req, rc);

		reply = req_capsule_server_get(&req->rq_pill, &RMF_FIEMAP_VAL);
		if (reply == NULL)
			GOTO(fini_req, rc = -EPROTO);

		memcpy(val, reply, *vallen);
fini_req:
		ptlrpc_req_finished(req);
drop_lock:
		if (mode)
			ldlm_lock_decref(&lockh, LCK_PR);
		return rc;
	}

	return -EINVAL;
}

static int osc_set_info_async(const struct lu_env *env, struct obd_export *exp,
			      obd_count keylen, void *key, obd_count vallen,
			      void *val, struct ptlrpc_request_set *set)
{
	struct ptlrpc_request *req;
	struct obd_device     *obd = exp->exp_obd;
	struct obd_import     *imp = class_exp2cliimp(exp);
	char		  *tmp;
	int		    rc;

	OBD_FAIL_TIMEOUT(OBD_FAIL_OSC_SHUTDOWN, 10);

	if (KEY_IS(KEY_CHECKSUM)) {
		if (vallen != sizeof(int))
			return -EINVAL;
		exp->exp_obd->u.cli.cl_checksum = (*(int *)val) ? 1 : 0;
		return 0;
	}

	if (KEY_IS(KEY_SPTLRPC_CONF)) {
		sptlrpc_conf_client_adapt(obd);
		return 0;
	}

	if (KEY_IS(KEY_FLUSH_CTX)) {
		sptlrpc_import_flush_my_ctx(imp);
		return 0;
	}

	if (KEY_IS(KEY_CACHE_SET)) {
		struct client_obd *cli = &obd->u.cli;

		LASSERT(cli->cl_cache == NULL); /* only once */
		cli->cl_cache = (struct cl_client_cache *)val;
		atomic_inc(&cli->cl_cache->ccc_users);
		cli->cl_lru_left = &cli->cl_cache->ccc_lru_left;

		/* add this osc into entity list */
		LASSERT(list_empty(&cli->cl_lru_osc));
		spin_lock(&cli->cl_cache->ccc_lru_lock);
		list_add(&cli->cl_lru_osc, &cli->cl_cache->ccc_lru);
		spin_unlock(&cli->cl_cache->ccc_lru_lock);

		return 0;
	}

	if (KEY_IS(KEY_CACHE_LRU_SHRINK)) {
		struct client_obd *cli = &obd->u.cli;
		int nr = atomic_read(&cli->cl_lru_in_list) >> 1;
		int target = *(int *)val;

		nr = osc_lru_shrink(cli, min(nr, target));
		*(int *)val -= nr;
		return 0;
	}

	if (!set && !KEY_IS(KEY_GRANT_SHRINK))
		return -EINVAL;

	/* We pass all other commands directly to OST. Since nobody calls osc
	   methods directly and everybody is supposed to go through LOV, we
	   assume lov checked invalid values for us.
	   The only recognised values so far are evict_by_nid and mds_conn.
	   Even if something bad goes through, we'd get a -EINVAL from OST
	   anyway. */

	req = ptlrpc_request_alloc(imp, KEY_IS(KEY_GRANT_SHRINK) ?
						&RQF_OST_SET_GRANT_INFO :
						&RQF_OBD_SET_INFO);
	if (req == NULL)
		return -ENOMEM;

	req_capsule_set_size(&req->rq_pill, &RMF_SETINFO_KEY,
			     RCL_CLIENT, keylen);
	if (!KEY_IS(KEY_GRANT_SHRINK))
		req_capsule_set_size(&req->rq_pill, &RMF_SETINFO_VAL,
				     RCL_CLIENT, vallen);
	rc = ptlrpc_request_pack(req, LUSTRE_OST_VERSION, OST_SET_INFO);
	if (rc) {
		ptlrpc_request_free(req);
		return rc;
	}

	tmp = req_capsule_client_get(&req->rq_pill, &RMF_SETINFO_KEY);
	memcpy(tmp, key, keylen);
	tmp = req_capsule_client_get(&req->rq_pill, KEY_IS(KEY_GRANT_SHRINK) ?
							&RMF_OST_BODY :
							&RMF_SETINFO_VAL);
	memcpy(tmp, val, vallen);

	if (KEY_IS(KEY_GRANT_SHRINK)) {
		struct osc_grant_args *aa;
		struct obdo *oa;

		CLASSERT(sizeof(*aa) <= sizeof(req->rq_async_args));
		aa = ptlrpc_req_async_args(req);
		OBDO_ALLOC(oa);
		if (!oa) {
			ptlrpc_req_finished(req);
			return -ENOMEM;
		}
		*oa = ((struct ost_body *)val)->oa;
		aa->aa_oa = oa;
		req->rq_interpret_reply = osc_shrink_grant_interpret;
	}

	ptlrpc_request_set_replen(req);
	if (!KEY_IS(KEY_GRANT_SHRINK)) {
		LASSERT(set != NULL);
		ptlrpc_set_add_req(set, req);
		ptlrpc_check_set(NULL, set);
	} else
		ptlrpcd_add_req(req, PDL_POLICY_ROUND, -1);

	return 0;
}


static int osc_llog_init(struct obd_device *obd, struct obd_llog_group *olg,
			 struct obd_device *disk_obd, int *index)
{
	/* this code is not supposed to be used with LOD/OSP
	 * to be removed soon */
	LBUG();
	return 0;
}

static int osc_llog_finish(struct obd_device *obd, int count)
{
	struct llog_ctxt *ctxt;

	ctxt = llog_get_context(obd, LLOG_MDS_OST_ORIG_CTXT);
	if (ctxt) {
		llog_cat_close(NULL, ctxt->loc_handle);
		llog_cleanup(NULL, ctxt);
	}

	ctxt = llog_get_context(obd, LLOG_SIZE_REPL_CTXT);
	if (ctxt)
		llog_cleanup(NULL, ctxt);
	return 0;
}

static int osc_reconnect(const struct lu_env *env,
			 struct obd_export *exp, struct obd_device *obd,
			 struct obd_uuid *cluuid,
			 struct obd_connect_data *data,
			 void *localdata)
{
	struct client_obd *cli = &obd->u.cli;

	if (data != NULL && (data->ocd_connect_flags & OBD_CONNECT_GRANT)) {
		long lost_grant;

		client_obd_list_lock(&cli->cl_loi_list_lock);
		data->ocd_grant = (cli->cl_avail_grant + cli->cl_dirty) ?:
				2 * cli_brw_size(obd);
		lost_grant = cli->cl_lost_grant;
		cli->cl_lost_grant = 0;
		client_obd_list_unlock(&cli->cl_loi_list_lock);

		CDEBUG(D_RPCTRACE, "ocd_connect_flags: "LPX64" ocd_version: %d"
		       " ocd_grant: %d, lost: %ld.\n", data->ocd_connect_flags,
		       data->ocd_version, data->ocd_grant, lost_grant);
	}

	return 0;
}

static int osc_disconnect(struct obd_export *exp)
{
	struct obd_device *obd = class_exp2obd(exp);
	struct llog_ctxt  *ctxt;
	int rc;

	ctxt = llog_get_context(obd, LLOG_SIZE_REPL_CTXT);
	if (ctxt) {
		if (obd->u.cli.cl_conn_count == 1) {
			/* Flush any remaining cancel messages out to the
			 * target */
			llog_sync(ctxt, exp, 0);
		}
		llog_ctxt_put(ctxt);
	} else {
		CDEBUG(D_HA, "No LLOG_SIZE_REPL_CTXT found in obd %p\n",
		       obd);
	}

	rc = client_disconnect_export(exp);
	/**
	 * Initially we put del_shrink_grant before disconnect_export, but it
	 * causes the following problem if setup (connect) and cleanup
	 * (disconnect) are tangled together.
	 *      connect p1		     disconnect p2
	 *   ptlrpc_connect_import
	 *     ...............	       class_manual_cleanup
	 *				     osc_disconnect
	 *				     del_shrink_grant
	 *   ptlrpc_connect_interrupt
	 *     init_grant_shrink
	 *   add this client to shrink list
	 *				      cleanup_osc
	 * Bang! pinger trigger the shrink.
	 * So the osc should be disconnected from the shrink list, after we
	 * are sure the import has been destroyed. BUG18662
	 */
	if (obd->u.cli.cl_import == NULL)
		osc_del_shrink_grant(&obd->u.cli);
	return rc;
}

static int osc_import_event(struct obd_device *obd,
			    struct obd_import *imp,
			    enum obd_import_event event)
{
	struct client_obd *cli;
	int rc = 0;

	LASSERT(imp->imp_obd == obd);

	switch (event) {
	case IMP_EVENT_DISCON: {
		cli = &obd->u.cli;
		client_obd_list_lock(&cli->cl_loi_list_lock);
		cli->cl_avail_grant = 0;
		cli->cl_lost_grant = 0;
		client_obd_list_unlock(&cli->cl_loi_list_lock);
		break;
	}
	case IMP_EVENT_INACTIVE: {
		rc = obd_notify_observer(obd, obd, OBD_NOTIFY_INACTIVE, NULL);
		break;
	}
	case IMP_EVENT_INVALIDATE: {
		struct ldlm_namespace *ns = obd->obd_namespace;
		struct lu_env	 *env;
		int		    refcheck;

		env = cl_env_get(&refcheck);
		if (!IS_ERR(env)) {
			/* Reset grants */
			cli = &obd->u.cli;
			/* all pages go to failing rpcs due to the invalid
			 * import */
			osc_io_unplug(env, cli, NULL, PDL_POLICY_ROUND);

			ldlm_namespace_cleanup(ns, LDLM_FL_LOCAL_ONLY);
			cl_env_put(env, &refcheck);
		} else
			rc = PTR_ERR(env);
		break;
	}
	case IMP_EVENT_ACTIVE: {
		rc = obd_notify_observer(obd, obd, OBD_NOTIFY_ACTIVE, NULL);
		break;
	}
	case IMP_EVENT_OCD: {
		struct obd_connect_data *ocd = &imp->imp_connect_data;

		if (ocd->ocd_connect_flags & OBD_CONNECT_GRANT)
			osc_init_grant(&obd->u.cli, ocd);

		/* See bug 7198 */
		if (ocd->ocd_connect_flags & OBD_CONNECT_REQPORTAL)
			imp->imp_client->cli_request_portal =OST_REQUEST_PORTAL;

		rc = obd_notify_observer(obd, obd, OBD_NOTIFY_OCD, NULL);
		break;
	}
	case IMP_EVENT_DEACTIVATE: {
		rc = obd_notify_observer(obd, obd, OBD_NOTIFY_DEACTIVATE, NULL);
		break;
	}
	case IMP_EVENT_ACTIVATE: {
		rc = obd_notify_observer(obd, obd, OBD_NOTIFY_ACTIVATE, NULL);
		break;
	}
	default:
		CERROR("Unknown import event %d\n", event);
		LBUG();
	}
	return rc;
}

/**
 * Determine whether the lock can be canceled before replaying the lock
 * during recovery, see bug16774 for detailed information.
 *
 * \retval zero the lock can't be canceled
 * \retval other ok to cancel
 */
static int osc_cancel_for_recovery(struct ldlm_lock *lock)
{
	check_res_locked(lock->l_resource);

	/*
	 * Cancel all unused extent lock in granted mode LCK_PR or LCK_CR.
	 *
	 * XXX as a future improvement, we can also cancel unused write lock
	 * if it doesn't have dirty data and active mmaps.
	 */
	if (lock->l_resource->lr_type == LDLM_EXTENT &&
	    (lock->l_granted_mode == LCK_PR ||
	     lock->l_granted_mode == LCK_CR) &&
	    (osc_dlm_lock_pageref(lock) == 0))
		return 1;

	return 0;
}

static int brw_queue_work(const struct lu_env *env, void *data)
{
	struct client_obd *cli = data;

	CDEBUG(D_CACHE, "Run writeback work for client obd %p.\n", cli);

	osc_io_unplug(env, cli, NULL, PDL_POLICY_SAME);
	return 0;
}

int osc_setup(struct obd_device *obd, struct lustre_cfg *lcfg)
{
	struct lprocfs_static_vars lvars = { 0 };
	struct client_obd	  *cli = &obd->u.cli;
	void		       *handler;
	int			rc;

	rc = ptlrpcd_addref();
	if (rc)
		return rc;

	rc = client_obd_setup(obd, lcfg);
	if (rc)
		GOTO(out_ptlrpcd, rc);

	handler = ptlrpcd_alloc_work(cli->cl_import, brw_queue_work, cli);
	if (IS_ERR(handler))
		GOTO(out_client_setup, rc = PTR_ERR(handler));
	cli->cl_writeback_work = handler;

	rc = osc_quota_setup(obd);
	if (rc)
		GOTO(out_ptlrpcd_work, rc);

	cli->cl_grant_shrink_interval = GRANT_SHRINK_INTERVAL;
	lprocfs_osc_init_vars(&lvars);
	if (lprocfs_obd_setup(obd, lvars.obd_vars) == 0) {
		lproc_osc_attach_seqstat(obd);
		sptlrpc_lprocfs_cliobd_attach(obd);
		ptlrpc_lprocfs_register_obd(obd);
	}

	/* We need to allocate a few requests more, because
	 * brw_interpret tries to create new requests before freeing
	 * previous ones, Ideally we want to have 2x max_rpcs_in_flight
	 * reserved, but I'm afraid that might be too much wasted RAM
	 * in fact, so 2 is just my guess and still should work. */
	cli->cl_import->imp_rq_pool =
		ptlrpc_init_rq_pool(cli->cl_max_rpcs_in_flight + 2,
				    OST_MAXREQSIZE,
				    ptlrpc_add_rqs_to_pool);

	INIT_LIST_HEAD(&cli->cl_grant_shrink_list);
	ns_register_cancel(obd->obd_namespace, osc_cancel_for_recovery);
	return rc;

out_ptlrpcd_work:
	ptlrpcd_destroy_work(handler);
out_client_setup:
	client_obd_cleanup(obd);
out_ptlrpcd:
	ptlrpcd_decref();
	return rc;
}

static int osc_precleanup(struct obd_device *obd, enum obd_cleanup_stage stage)
{
	int rc = 0;

	switch (stage) {
	case OBD_CLEANUP_EARLY: {
		struct obd_import *imp;
		imp = obd->u.cli.cl_import;
		CDEBUG(D_HA, "Deactivating import %s\n", obd->obd_name);
		/* ptlrpc_abort_inflight to stop an mds_lov_synchronize */
		ptlrpc_deactivate_import(imp);
		spin_lock(&imp->imp_lock);
		imp->imp_pingable = 0;
		spin_unlock(&imp->imp_lock);
		break;
	}
	case OBD_CLEANUP_EXPORTS: {
		struct client_obd *cli = &obd->u.cli;
		/* LU-464
		 * for echo client, export may be on zombie list, wait for
		 * zombie thread to cull it, because cli.cl_import will be
		 * cleared in client_disconnect_export():
		 *   class_export_destroy() -> obd_cleanup() ->
		 *   echo_device_free() -> echo_client_cleanup() ->
		 *   obd_disconnect() -> osc_disconnect() ->
		 *   client_disconnect_export()
		 */
		obd_zombie_barrier();
		if (cli->cl_writeback_work) {
			ptlrpcd_destroy_work(cli->cl_writeback_work);
			cli->cl_writeback_work = NULL;
		}
		obd_cleanup_client_import(obd);
		ptlrpc_lprocfs_unregister_obd(obd);
		lprocfs_obd_cleanup(obd);
		rc = obd_llog_finish(obd, 0);
		if (rc != 0)
			CERROR("failed to cleanup llogging subsystems\n");
		break;
		}
	}
	return rc;
}

int osc_cleanup(struct obd_device *obd)
{
	struct client_obd *cli = &obd->u.cli;
	int rc;

	/* lru cleanup */
	if (cli->cl_cache != NULL) {
		LASSERT(atomic_read(&cli->cl_cache->ccc_users) > 0);
		spin_lock(&cli->cl_cache->ccc_lru_lock);
		list_del_init(&cli->cl_lru_osc);
		spin_unlock(&cli->cl_cache->ccc_lru_lock);
		cli->cl_lru_left = NULL;
		atomic_dec(&cli->cl_cache->ccc_users);
		cli->cl_cache = NULL;
	}

	/* free memory of osc quota cache */
	osc_quota_cleanup(obd);

	rc = client_obd_cleanup(obd);

	ptlrpcd_decref();
	return rc;
}

int osc_process_config_base(struct obd_device *obd, struct lustre_cfg *lcfg)
{
	struct lprocfs_static_vars lvars = { 0 };
	int rc = 0;

	lprocfs_osc_init_vars(&lvars);

	switch (lcfg->lcfg_command) {
	default:
		rc = class_process_proc_param(PARAM_OSC, lvars.obd_vars,
					      lcfg, obd);
		if (rc > 0)
			rc = 0;
		break;
	}

	return(rc);
}

static int osc_process_config(struct obd_device *obd, obd_count len, void *buf)
{
	return osc_process_config_base(obd, buf);
}

struct obd_ops osc_obd_ops = {
	.o_owner		= THIS_MODULE,
	.o_setup		= osc_setup,
	.o_precleanup	   = osc_precleanup,
	.o_cleanup	      = osc_cleanup,
	.o_add_conn	     = client_import_add_conn,
	.o_del_conn	     = client_import_del_conn,
	.o_connect	      = client_connect_import,
	.o_reconnect	    = osc_reconnect,
	.o_disconnect	   = osc_disconnect,
	.o_statfs	       = osc_statfs,
	.o_statfs_async	 = osc_statfs_async,
	.o_packmd	       = osc_packmd,
	.o_unpackmd	     = osc_unpackmd,
	.o_create	       = osc_create,
	.o_destroy	      = osc_destroy,
	.o_getattr	      = osc_getattr,
	.o_getattr_async	= osc_getattr_async,
	.o_setattr	      = osc_setattr,
	.o_setattr_async	= osc_setattr_async,
	.o_brw		  = osc_brw,
	.o_punch		= osc_punch,
	.o_sync		 = osc_sync,
	.o_enqueue	      = osc_enqueue,
	.o_change_cbdata	= osc_change_cbdata,
	.o_find_cbdata	  = osc_find_cbdata,
	.o_cancel	       = osc_cancel,
	.o_cancel_unused	= osc_cancel_unused,
	.o_iocontrol	    = osc_iocontrol,
	.o_get_info	     = osc_get_info,
	.o_set_info_async       = osc_set_info_async,
	.o_import_event	 = osc_import_event,
	.o_llog_init	    = osc_llog_init,
	.o_llog_finish	  = osc_llog_finish,
	.o_process_config       = osc_process_config,
	.o_quotactl	     = osc_quotactl,
	.o_quotacheck	   = osc_quotacheck,
};

extern struct lu_kmem_descr osc_caches[];
extern spinlock_t osc_ast_guard;
extern struct lock_class_key osc_ast_guard_class;

int __init osc_init(void)
{
	struct lprocfs_static_vars lvars = { 0 };
	int rc;

	/* print an address of _any_ initialized kernel symbol from this
	 * module, to allow debugging with gdb that doesn't support data
	 * symbols from modules.*/
	CDEBUG(D_INFO, "Lustre OSC module (%p).\n", &osc_caches);

	rc = lu_kmem_init(osc_caches);
	if (rc)
		return rc;

	lprocfs_osc_init_vars(&lvars);

	rc = class_register_type(&osc_obd_ops, NULL, lvars.module_vars,
				 LUSTRE_OSC_NAME, &osc_device_type);
	if (rc) {
		lu_kmem_fini(osc_caches);
		return rc;
	}

	spin_lock_init(&osc_ast_guard);
	lockdep_set_class(&osc_ast_guard, &osc_ast_guard_class);

	return rc;
}

static void /*__exit*/ osc_exit(void)
{
	class_unregister_type(LUSTRE_OSC_NAME);
	lu_kmem_fini(osc_caches);
}

MODULE_AUTHOR("Sun Microsystems, Inc. <http://www.lustre.org/>");
MODULE_DESCRIPTION("Lustre Object Storage Client (OSC)");
MODULE_LICENSE("GPL");
MODULE_VERSION(LUSTRE_VERSION_STRING);

module_init(osc_init);
module_exit(osc_exit);
