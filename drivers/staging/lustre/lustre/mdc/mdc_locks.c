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
 * Copyright (c) 2011, 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#define DEBUG_SUBSYSTEM S_MDC

# include <linux/module.h>

#include <linux/lustre_intent.h>
#include <obd.h>
#include <obd_class.h>
#include <lustre_dlm.h>
#include <lustre_fid.h> /* fid_res_name_eq() */
#include <lustre_mdc.h>
#include <lustre_net.h>
#include <lustre_req_layout.h>
#include "mdc_internal.h"

struct mdc_getattr_args {
	struct obd_export	   *ga_exp;
	struct md_enqueue_info      *ga_minfo;
	struct ldlm_enqueue_info    *ga_einfo;
};

int it_disposition(struct lookup_intent *it, int flag)
{
	return it->d.lustre.it_disposition & flag;
}
EXPORT_SYMBOL(it_disposition);

void it_set_disposition(struct lookup_intent *it, int flag)
{
	it->d.lustre.it_disposition |= flag;
}
EXPORT_SYMBOL(it_set_disposition);

void it_clear_disposition(struct lookup_intent *it, int flag)
{
	it->d.lustre.it_disposition &= ~flag;
}
EXPORT_SYMBOL(it_clear_disposition);

int it_open_error(int phase, struct lookup_intent *it)
{
	if (it_disposition(it, DISP_OPEN_LEASE)) {
		if (phase >= DISP_OPEN_LEASE)
			return it->d.lustre.it_status;
		else
			return 0;
	}
	if (it_disposition(it, DISP_OPEN_OPEN)) {
		if (phase >= DISP_OPEN_OPEN)
			return it->d.lustre.it_status;
		else
			return 0;
	}

	if (it_disposition(it, DISP_OPEN_CREATE)) {
		if (phase >= DISP_OPEN_CREATE)
			return it->d.lustre.it_status;
		else
			return 0;
	}

	if (it_disposition(it, DISP_LOOKUP_EXECD)) {
		if (phase >= DISP_LOOKUP_EXECD)
			return it->d.lustre.it_status;
		else
			return 0;
	}

	if (it_disposition(it, DISP_IT_EXECD)) {
		if (phase >= DISP_IT_EXECD)
			return it->d.lustre.it_status;
		else
			return 0;
	}
	CERROR("it disp: %X, status: %d\n", it->d.lustre.it_disposition,
	       it->d.lustre.it_status);
	LBUG();
	return 0;
}
EXPORT_SYMBOL(it_open_error);

/* this must be called on a lockh that is known to have a referenced lock */
int mdc_set_lock_data(struct obd_export *exp, __u64 *lockh, void *data,
		      __u64 *bits)
{
	struct ldlm_lock *lock;
	struct inode *new_inode = data;

	if (bits)
		*bits = 0;

	if (!*lockh)
		return 0;

	lock = ldlm_handle2lock((struct lustre_handle *)lockh);

	LASSERT(lock != NULL);
	lock_res_and_lock(lock);
	if (lock->l_resource->lr_lvb_inode &&
	    lock->l_resource->lr_lvb_inode != data) {
		struct inode *old_inode = lock->l_resource->lr_lvb_inode;
		LASSERTF(old_inode->i_state & I_FREEING,
			 "Found existing inode %p/%lu/%u state %lu in lock: "
			 "setting data to %p/%lu/%u\n", old_inode,
			 old_inode->i_ino, old_inode->i_generation,
			 old_inode->i_state,
			 new_inode, new_inode->i_ino, new_inode->i_generation);
	}
	lock->l_resource->lr_lvb_inode = new_inode;
	if (bits)
		*bits = lock->l_policy_data.l_inodebits.bits;

	unlock_res_and_lock(lock);
	LDLM_LOCK_PUT(lock);

	return 0;
}

ldlm_mode_t mdc_lock_match(struct obd_export *exp, __u64 flags,
			   const struct lu_fid *fid, ldlm_type_t type,
			   ldlm_policy_data_t *policy, ldlm_mode_t mode,
			   struct lustre_handle *lockh)
{
	struct ldlm_res_id res_id;
	ldlm_mode_t rc;

	fid_build_reg_res_name(fid, &res_id);
	/* LU-4405: Clear bits not supported by server */
	policy->l_inodebits.bits &= exp_connect_ibits(exp);
	rc = ldlm_lock_match(class_exp2obd(exp)->obd_namespace, flags,
			     &res_id, type, policy, mode, lockh, 0);
	return rc;
}

int mdc_cancel_unused(struct obd_export *exp,
		      const struct lu_fid *fid,
		      ldlm_policy_data_t *policy,
		      ldlm_mode_t mode,
		      ldlm_cancel_flags_t flags,
		      void *opaque)
{
	struct ldlm_res_id res_id;
	struct obd_device *obd = class_exp2obd(exp);
	int rc;

	fid_build_reg_res_name(fid, &res_id);
	rc = ldlm_cli_cancel_unused_resource(obd->obd_namespace, &res_id,
					     policy, mode, flags, opaque);
	return rc;
}

int mdc_null_inode(struct obd_export *exp,
		   const struct lu_fid *fid)
{
	struct ldlm_res_id res_id;
	struct ldlm_resource *res;
	struct ldlm_namespace *ns = class_exp2obd(exp)->obd_namespace;

	LASSERTF(ns != NULL, "no namespace passed\n");

	fid_build_reg_res_name(fid, &res_id);

	res = ldlm_resource_get(ns, NULL, &res_id, 0, 0);
	if (res == NULL)
		return 0;

	lock_res(res);
	res->lr_lvb_inode = NULL;
	unlock_res(res);

	ldlm_resource_putref(res);
	return 0;
}

/* find any ldlm lock of the inode in mdc
 * return 0    not find
 *	1    find one
 *      < 0    error */
int mdc_find_cbdata(struct obd_export *exp,
		    const struct lu_fid *fid,
		    ldlm_iterator_t it, void *data)
{
	struct ldlm_res_id res_id;
	int rc = 0;

	fid_build_reg_res_name((struct lu_fid*)fid, &res_id);
	rc = ldlm_resource_iterate(class_exp2obd(exp)->obd_namespace, &res_id,
				   it, data);
	if (rc == LDLM_ITER_STOP)
		return 1;
	else if (rc == LDLM_ITER_CONTINUE)
		return 0;
	return rc;
}

static inline void mdc_clear_replay_flag(struct ptlrpc_request *req, int rc)
{
	/* Don't hold error requests for replay. */
	if (req->rq_replay) {
		spin_lock(&req->rq_lock);
		req->rq_replay = 0;
		spin_unlock(&req->rq_lock);
	}
	if (rc && req->rq_transno != 0) {
		DEBUG_REQ(D_ERROR, req, "transno returned on error rc %d", rc);
		LBUG();
	}
}

/* Save a large LOV EA into the request buffer so that it is available
 * for replay.  We don't do this in the initial request because the
 * original request doesn't need this buffer (at most it sends just the
 * lov_mds_md) and it is a waste of RAM/bandwidth to send the empty
 * buffer and may also be difficult to allocate and save a very large
 * request buffer for each open. (bug 5707)
 *
 * OOM here may cause recovery failure if lmm is needed (only for the
 * original open if the MDS crashed just when this client also OOM'd)
 * but this is incredibly unlikely, and questionable whether the client
 * could do MDS recovery under OOM anyways... */
static void mdc_realloc_openmsg(struct ptlrpc_request *req,
				struct mdt_body *body)
{
	int     rc;

	/* FIXME: remove this explicit offset. */
	rc = sptlrpc_cli_enlarge_reqbuf(req, DLM_INTENT_REC_OFF + 4,
					body->eadatasize);
	if (rc) {
		CERROR("Can't enlarge segment %d size to %d\n",
		       DLM_INTENT_REC_OFF + 4, body->eadatasize);
		body->valid &= ~OBD_MD_FLEASIZE;
		body->eadatasize = 0;
	}
}

static struct ptlrpc_request *mdc_intent_open_pack(struct obd_export *exp,
						   struct lookup_intent *it,
						   struct md_op_data *op_data,
						   void *lmm, int lmmsize,
						   void *cb_data)
{
	struct ptlrpc_request *req;
	struct obd_device     *obddev = class_exp2obd(exp);
	struct ldlm_intent    *lit;
	LIST_HEAD(cancels);
	int		    count = 0;
	int		    mode;
	int		    rc;

	it->it_create_mode = (it->it_create_mode & ~S_IFMT) | S_IFREG;

	/* XXX: openlock is not cancelled for cross-refs. */
	/* If inode is known, cancel conflicting OPEN locks. */
	if (fid_is_sane(&op_data->op_fid2)) {
		if (it->it_flags & MDS_OPEN_LEASE) { /* try to get lease */
			if (it->it_flags & FMODE_WRITE)
				mode = LCK_EX;
			else
				mode = LCK_PR;
		} else {
			if (it->it_flags & (FMODE_WRITE|MDS_OPEN_TRUNC))
				mode = LCK_CW;
#ifdef FMODE_EXEC
			else if (it->it_flags & FMODE_EXEC)
				mode = LCK_PR;
#endif
			else
				mode = LCK_CR;
		}
		count = mdc_resource_get_unused(exp, &op_data->op_fid2,
						&cancels, mode,
						MDS_INODELOCK_OPEN);
	}

	/* If CREATE, cancel parent's UPDATE lock. */
	if (it->it_op & IT_CREAT)
		mode = LCK_EX;
	else
		mode = LCK_CR;
	count += mdc_resource_get_unused(exp, &op_data->op_fid1,
					 &cancels, mode,
					 MDS_INODELOCK_UPDATE);

	req = ptlrpc_request_alloc(class_exp2cliimp(exp),
				   &RQF_LDLM_INTENT_OPEN);
	if (req == NULL) {
		ldlm_lock_list_put(&cancels, l_bl_ast, count);
		return ERR_PTR(-ENOMEM);
	}

	/* parent capability */
	mdc_set_capa_size(req, &RMF_CAPA1, op_data->op_capa1);
	/* child capability, reserve the size according to parent capa, it will
	 * be filled after we get the reply */
	mdc_set_capa_size(req, &RMF_CAPA2, op_data->op_capa1);

	req_capsule_set_size(&req->rq_pill, &RMF_NAME, RCL_CLIENT,
			     op_data->op_namelen + 1);
	req_capsule_set_size(&req->rq_pill, &RMF_EADATA, RCL_CLIENT,
			     max(lmmsize, obddev->u.cli.cl_default_mds_easize));

	rc = ldlm_prep_enqueue_req(exp, req, &cancels, count);
	if (rc < 0) {
		ptlrpc_request_free(req);
		return ERR_PTR(rc);
	}

	spin_lock(&req->rq_lock);
	req->rq_replay = req->rq_import->imp_replayable;
	spin_unlock(&req->rq_lock);

	/* pack the intent */
	lit = req_capsule_client_get(&req->rq_pill, &RMF_LDLM_INTENT);
	lit->opc = (__u64)it->it_op;

	/* pack the intended request */
	mdc_open_pack(req, op_data, it->it_create_mode, 0, it->it_flags, lmm,
		      lmmsize);

	/* for remote client, fetch remote perm for current user */
	if (client_is_remote(exp))
		req_capsule_set_size(&req->rq_pill, &RMF_ACL, RCL_SERVER,
				     sizeof(struct mdt_remote_perm));
	ptlrpc_request_set_replen(req);
	return req;
}

static struct ptlrpc_request *
mdc_intent_getxattr_pack(struct obd_export *exp,
			 struct lookup_intent *it,
			 struct md_op_data *op_data)
{
	struct ptlrpc_request	*req;
	struct ldlm_intent	*lit;
	int			rc, count = 0, maxdata;
	LIST_HEAD(cancels);



	req = ptlrpc_request_alloc(class_exp2cliimp(exp),
					&RQF_LDLM_INTENT_GETXATTR);
	if (req == NULL)
		return ERR_PTR(-ENOMEM);

	mdc_set_capa_size(req, &RMF_CAPA1, op_data->op_capa1);

	rc = ldlm_prep_enqueue_req(exp, req, &cancels, count);
	if (rc) {
		ptlrpc_request_free(req);
		return ERR_PTR(rc);
	}

	/* pack the intent */
	lit = req_capsule_client_get(&req->rq_pill, &RMF_LDLM_INTENT);
	lit->opc = IT_GETXATTR;

	maxdata = class_exp2cliimp(exp)->imp_connect_data.ocd_max_easize;

	/* pack the intended request */
	mdc_pack_body(req, &op_data->op_fid1, op_data->op_capa1,
			op_data->op_valid, maxdata, -1, 0);

	req_capsule_set_size(&req->rq_pill, &RMF_EADATA,
				RCL_SERVER, maxdata);

	req_capsule_set_size(&req->rq_pill, &RMF_EAVALS,
				RCL_SERVER, maxdata);

	req_capsule_set_size(&req->rq_pill, &RMF_EAVALS_LENS,
				RCL_SERVER, maxdata);

	ptlrpc_request_set_replen(req);

	return req;
}

static struct ptlrpc_request *mdc_intent_unlink_pack(struct obd_export *exp,
						     struct lookup_intent *it,
						     struct md_op_data *op_data)
{
	struct ptlrpc_request *req;
	struct obd_device     *obddev = class_exp2obd(exp);
	struct ldlm_intent    *lit;
	int		    rc;

	req = ptlrpc_request_alloc(class_exp2cliimp(exp),
				   &RQF_LDLM_INTENT_UNLINK);
	if (req == NULL)
		return ERR_PTR(-ENOMEM);

	mdc_set_capa_size(req, &RMF_CAPA1, op_data->op_capa1);
	req_capsule_set_size(&req->rq_pill, &RMF_NAME, RCL_CLIENT,
			     op_data->op_namelen + 1);

	rc = ldlm_prep_enqueue_req(exp, req, NULL, 0);
	if (rc) {
		ptlrpc_request_free(req);
		return ERR_PTR(rc);
	}

	/* pack the intent */
	lit = req_capsule_client_get(&req->rq_pill, &RMF_LDLM_INTENT);
	lit->opc = (__u64)it->it_op;

	/* pack the intended request */
	mdc_unlink_pack(req, op_data);

	req_capsule_set_size(&req->rq_pill, &RMF_MDT_MD, RCL_SERVER,
			     obddev->u.cli.cl_max_mds_easize);
	req_capsule_set_size(&req->rq_pill, &RMF_ACL, RCL_SERVER,
			     obddev->u.cli.cl_max_mds_cookiesize);
	ptlrpc_request_set_replen(req);
	return req;
}

static struct ptlrpc_request *mdc_intent_getattr_pack(struct obd_export *exp,
						      struct lookup_intent *it,
						      struct md_op_data *op_data)
{
	struct ptlrpc_request *req;
	struct obd_device     *obddev = class_exp2obd(exp);
	obd_valid	      valid = OBD_MD_FLGETATTR | OBD_MD_FLEASIZE |
				       OBD_MD_FLMODEASIZE | OBD_MD_FLDIREA |
				       OBD_MD_FLMDSCAPA | OBD_MD_MEA |
				       (client_is_remote(exp) ?
					       OBD_MD_FLRMTPERM : OBD_MD_FLACL);
	struct ldlm_intent    *lit;
	int		    rc;

	req = ptlrpc_request_alloc(class_exp2cliimp(exp),
				   &RQF_LDLM_INTENT_GETATTR);
	if (req == NULL)
		return ERR_PTR(-ENOMEM);

	mdc_set_capa_size(req, &RMF_CAPA1, op_data->op_capa1);
	req_capsule_set_size(&req->rq_pill, &RMF_NAME, RCL_CLIENT,
			     op_data->op_namelen + 1);

	rc = ldlm_prep_enqueue_req(exp, req, NULL, 0);
	if (rc) {
		ptlrpc_request_free(req);
		return ERR_PTR(rc);
	}

	/* pack the intent */
	lit = req_capsule_client_get(&req->rq_pill, &RMF_LDLM_INTENT);
	lit->opc = (__u64)it->it_op;

	/* pack the intended request */
	mdc_getattr_pack(req, valid, it->it_flags, op_data,
			 obddev->u.cli.cl_max_mds_easize);

	req_capsule_set_size(&req->rq_pill, &RMF_MDT_MD, RCL_SERVER,
			     obddev->u.cli.cl_max_mds_easize);
	if (client_is_remote(exp))
		req_capsule_set_size(&req->rq_pill, &RMF_ACL, RCL_SERVER,
				     sizeof(struct mdt_remote_perm));
	ptlrpc_request_set_replen(req);
	return req;
}

static struct ptlrpc_request *mdc_intent_layout_pack(struct obd_export *exp,
						     struct lookup_intent *it,
						     struct md_op_data *unused)
{
	struct obd_device     *obd = class_exp2obd(exp);
	struct ptlrpc_request *req;
	struct ldlm_intent    *lit;
	struct layout_intent  *layout;
	int rc;

	req = ptlrpc_request_alloc(class_exp2cliimp(exp),
				&RQF_LDLM_INTENT_LAYOUT);
	if (req == NULL)
		return ERR_PTR(-ENOMEM);

	req_capsule_set_size(&req->rq_pill, &RMF_EADATA, RCL_CLIENT, 0);
	rc = ldlm_prep_enqueue_req(exp, req, NULL, 0);
	if (rc) {
		ptlrpc_request_free(req);
		return ERR_PTR(rc);
	}

	/* pack the intent */
	lit = req_capsule_client_get(&req->rq_pill, &RMF_LDLM_INTENT);
	lit->opc = (__u64)it->it_op;

	/* pack the layout intent request */
	layout = req_capsule_client_get(&req->rq_pill, &RMF_LAYOUT_INTENT);
	/* LAYOUT_INTENT_ACCESS is generic, specific operation will be
	 * set for replication */
	layout->li_opc = LAYOUT_INTENT_ACCESS;

	req_capsule_set_size(&req->rq_pill, &RMF_DLM_LVB, RCL_SERVER,
			obd->u.cli.cl_max_mds_easize);
	ptlrpc_request_set_replen(req);
	return req;
}

static struct ptlrpc_request *
mdc_enqueue_pack(struct obd_export *exp, int lvb_len)
{
	struct ptlrpc_request *req;
	int rc;

	req = ptlrpc_request_alloc(class_exp2cliimp(exp), &RQF_LDLM_ENQUEUE);
	if (req == NULL)
		return ERR_PTR(-ENOMEM);

	rc = ldlm_prep_enqueue_req(exp, req, NULL, 0);
	if (rc) {
		ptlrpc_request_free(req);
		return ERR_PTR(rc);
	}

	req_capsule_set_size(&req->rq_pill, &RMF_DLM_LVB, RCL_SERVER, lvb_len);
	ptlrpc_request_set_replen(req);
	return req;
}

static int mdc_finish_enqueue(struct obd_export *exp,
			      struct ptlrpc_request *req,
			      struct ldlm_enqueue_info *einfo,
			      struct lookup_intent *it,
			      struct lustre_handle *lockh,
			      int rc)
{
	struct req_capsule  *pill = &req->rq_pill;
	struct ldlm_request *lockreq;
	struct ldlm_reply   *lockrep;
	struct lustre_intent_data *intent = &it->d.lustre;
	struct ldlm_lock    *lock;
	void		*lvb_data = NULL;
	int		  lvb_len = 0;

	LASSERT(rc >= 0);
	/* Similarly, if we're going to replay this request, we don't want to
	 * actually get a lock, just perform the intent. */
	if (req->rq_transno || req->rq_replay) {
		lockreq = req_capsule_client_get(pill, &RMF_DLM_REQ);
		lockreq->lock_flags |= ldlm_flags_to_wire(LDLM_FL_INTENT_ONLY);
	}

	if (rc == ELDLM_LOCK_ABORTED) {
		einfo->ei_mode = 0;
		memset(lockh, 0, sizeof(*lockh));
		rc = 0;
	} else { /* rc = 0 */
		lock = ldlm_handle2lock(lockh);
		LASSERT(lock != NULL);

		/* If the server gave us back a different lock mode, we should
		 * fix up our variables. */
		if (lock->l_req_mode != einfo->ei_mode) {
			ldlm_lock_addref(lockh, lock->l_req_mode);
			ldlm_lock_decref(lockh, einfo->ei_mode);
			einfo->ei_mode = lock->l_req_mode;
		}
		LDLM_LOCK_PUT(lock);
	}

	lockrep = req_capsule_server_get(pill, &RMF_DLM_REP);
	LASSERT(lockrep != NULL); /* checked by ldlm_cli_enqueue() */

	intent->it_disposition = (int)lockrep->lock_policy_res1;
	intent->it_status = (int)lockrep->lock_policy_res2;
	intent->it_lock_mode = einfo->ei_mode;
	intent->it_lock_handle = lockh->cookie;
	intent->it_data = req;

	/* Technically speaking rq_transno must already be zero if
	 * it_status is in error, so the check is a bit redundant */
	if ((!req->rq_transno || intent->it_status < 0) && req->rq_replay)
		mdc_clear_replay_flag(req, intent->it_status);

	/* If we're doing an IT_OPEN which did not result in an actual
	 * successful open, then we need to remove the bit which saves
	 * this request for unconditional replay.
	 *
	 * It's important that we do this first!  Otherwise we might exit the
	 * function without doing so, and try to replay a failed create
	 * (bug 3440) */
	if (it->it_op & IT_OPEN && req->rq_replay &&
	    (!it_disposition(it, DISP_OPEN_OPEN) ||intent->it_status != 0))
		mdc_clear_replay_flag(req, intent->it_status);

	DEBUG_REQ(D_RPCTRACE, req, "op: %d disposition: %x, status: %d",
		  it->it_op, intent->it_disposition, intent->it_status);

	/* We know what to expect, so we do any byte flipping required here */
	if (it->it_op & (IT_OPEN | IT_UNLINK | IT_LOOKUP | IT_GETATTR)) {
		struct mdt_body *body;

		body = req_capsule_server_get(pill, &RMF_MDT_BODY);
		if (body == NULL) {
			CERROR ("Can't swab mdt_body\n");
			return -EPROTO;
		}

		if (it_disposition(it, DISP_OPEN_OPEN) &&
		    !it_open_error(DISP_OPEN_OPEN, it)) {
			/*
			 * If this is a successful OPEN request, we need to set
			 * replay handler and data early, so that if replay
			 * happens immediately after swabbing below, new reply
			 * is swabbed by that handler correctly.
			 */
			mdc_set_open_replay_data(NULL, NULL, it);
		}

		if ((body->valid & (OBD_MD_FLDIREA | OBD_MD_FLEASIZE)) != 0) {
			void *eadata;

			mdc_update_max_ea_from_body(exp, body);

			/*
			 * The eadata is opaque; just check that it is there.
			 * Eventually, obd_unpackmd() will check the contents.
			 */
			eadata = req_capsule_server_sized_get(pill, &RMF_MDT_MD,
							      body->eadatasize);
			if (eadata == NULL)
				return -EPROTO;

			/* save lvb data and length in case this is for layout
			 * lock */
			lvb_data = eadata;
			lvb_len = body->eadatasize;

			/*
			 * We save the reply LOV EA in case we have to replay a
			 * create for recovery.  If we didn't allocate a large
			 * enough request buffer above we need to reallocate it
			 * here to hold the actual LOV EA.
			 *
			 * To not save LOV EA if request is not going to replay
			 * (for example error one).
			 */
			if ((it->it_op & IT_OPEN) && req->rq_replay) {
				void *lmm;
				if (req_capsule_get_size(pill, &RMF_EADATA,
							 RCL_CLIENT) <
				    body->eadatasize)
					mdc_realloc_openmsg(req, body);
				else
					req_capsule_shrink(pill, &RMF_EADATA,
							   body->eadatasize,
							   RCL_CLIENT);

				req_capsule_set_size(pill, &RMF_EADATA,
						     RCL_CLIENT,
						     body->eadatasize);

				lmm = req_capsule_client_get(pill, &RMF_EADATA);
				if (lmm)
					memcpy(lmm, eadata, body->eadatasize);
			}
		}

		if (body->valid & OBD_MD_FLRMTPERM) {
			struct mdt_remote_perm *perm;

			LASSERT(client_is_remote(exp));
			perm = req_capsule_server_swab_get(pill, &RMF_ACL,
						lustre_swab_mdt_remote_perm);
			if (perm == NULL)
				return -EPROTO;
		}
		if (body->valid & OBD_MD_FLMDSCAPA) {
			struct lustre_capa *capa, *p;

			capa = req_capsule_server_get(pill, &RMF_CAPA1);
			if (capa == NULL)
				return -EPROTO;

			if (it->it_op & IT_OPEN) {
				/* client fid capa will be checked in replay */
				p = req_capsule_client_get(pill, &RMF_CAPA2);
				LASSERT(p);
				*p = *capa;
			}
		}
		if (body->valid & OBD_MD_FLOSSCAPA) {
			struct lustre_capa *capa;

			capa = req_capsule_server_get(pill, &RMF_CAPA2);
			if (capa == NULL)
				return -EPROTO;
		}
	} else if (it->it_op & IT_LAYOUT) {
		/* maybe the lock was granted right away and layout
		 * is packed into RMF_DLM_LVB of req */
		lvb_len = req_capsule_get_size(pill, &RMF_DLM_LVB, RCL_SERVER);
		if (lvb_len > 0) {
			lvb_data = req_capsule_server_sized_get(pill,
							&RMF_DLM_LVB, lvb_len);
			if (lvb_data == NULL)
				return -EPROTO;
		}
	}

	/* fill in stripe data for layout lock */
	lock = ldlm_handle2lock(lockh);
	if (lock != NULL && ldlm_has_layout(lock) && lvb_data != NULL) {
		void *lmm;

		LDLM_DEBUG(lock, "layout lock returned by: %s, lvb_len: %d\n",
			ldlm_it2str(it->it_op), lvb_len);

		OBD_ALLOC_LARGE(lmm, lvb_len);
		if (lmm == NULL) {
			LDLM_LOCK_PUT(lock);
			return -ENOMEM;
		}
		memcpy(lmm, lvb_data, lvb_len);

		/* install lvb_data */
		lock_res_and_lock(lock);
		if (lock->l_lvb_data == NULL) {
			lock->l_lvb_type = LVB_T_LAYOUT;
			lock->l_lvb_data = lmm;
			lock->l_lvb_len = lvb_len;
			lmm = NULL;
		}
		unlock_res_and_lock(lock);
		if (lmm != NULL)
			OBD_FREE_LARGE(lmm, lvb_len);
	}
	if (lock != NULL)
		LDLM_LOCK_PUT(lock);

	return rc;
}

/* We always reserve enough space in the reply packet for a stripe MD, because
 * we don't know in advance the file type. */
int mdc_enqueue(struct obd_export *exp, struct ldlm_enqueue_info *einfo,
		struct lookup_intent *it, struct md_op_data *op_data,
		struct lustre_handle *lockh, void *lmm, int lmmsize,
		struct ptlrpc_request **reqp, __u64 extra_lock_flags)
{
	struct obd_device     *obddev = class_exp2obd(exp);
	struct ptlrpc_request *req = NULL;
	__u64		  flags, saved_flags = extra_lock_flags;
	int		    rc;
	struct ldlm_res_id res_id;
	static const ldlm_policy_data_t lookup_policy =
			    { .l_inodebits = { MDS_INODELOCK_LOOKUP } };
	static const ldlm_policy_data_t update_policy =
			    { .l_inodebits = { MDS_INODELOCK_UPDATE } };
	static const ldlm_policy_data_t layout_policy =
			    { .l_inodebits = { MDS_INODELOCK_LAYOUT } };
	static const ldlm_policy_data_t getxattr_policy = {
			      .l_inodebits = { MDS_INODELOCK_XATTR } };
	ldlm_policy_data_t const *policy = &lookup_policy;
	int		    generation, resends = 0;
	struct ldlm_reply     *lockrep;
	enum lvb_type	       lvb_type = 0;

	LASSERTF(!it || einfo->ei_type == LDLM_IBITS, "lock type %d\n",
		 einfo->ei_type);

	fid_build_reg_res_name(&op_data->op_fid1, &res_id);

	if (it) {
		saved_flags |= LDLM_FL_HAS_INTENT;
		if (it->it_op & (IT_UNLINK | IT_GETATTR | IT_READDIR))
			policy = &update_policy;
		else if (it->it_op & IT_LAYOUT)
			policy = &layout_policy;
		else if (it->it_op & (IT_GETXATTR | IT_SETXATTR))
			policy = &getxattr_policy;
	}

	LASSERT(reqp == NULL);

	generation = obddev->u.cli.cl_import->imp_generation;
resend:
	flags = saved_flags;
	if (!it) {
		/* The only way right now is FLOCK, in this case we hide flock
		   policy as lmm, but lmmsize is 0 */
		LASSERT(lmm && lmmsize == 0);
		LASSERTF(einfo->ei_type == LDLM_FLOCK, "lock type %d\n",
			 einfo->ei_type);
		policy = (ldlm_policy_data_t *)lmm;
		res_id.name[3] = LDLM_FLOCK;
	} else if (it->it_op & IT_OPEN) {
		req = mdc_intent_open_pack(exp, it, op_data, lmm, lmmsize,
					   einfo->ei_cbdata);
		policy = &update_policy;
		einfo->ei_cbdata = NULL;
		lmm = NULL;
	} else if (it->it_op & IT_UNLINK) {
		req = mdc_intent_unlink_pack(exp, it, op_data);
	} else if (it->it_op & (IT_GETATTR | IT_LOOKUP)) {
		req = mdc_intent_getattr_pack(exp, it, op_data);
	} else if (it->it_op & IT_READDIR) {
		req = mdc_enqueue_pack(exp, 0);
	} else if (it->it_op & IT_LAYOUT) {
		if (!imp_connect_lvb_type(class_exp2cliimp(exp)))
			return -EOPNOTSUPP;
		req = mdc_intent_layout_pack(exp, it, op_data);
		lvb_type = LVB_T_LAYOUT;
	} else if (it->it_op & IT_GETXATTR) {
		req = mdc_intent_getxattr_pack(exp, it, op_data);
	} else {
		LBUG();
		return -EINVAL;
	}

	if (IS_ERR(req))
		return PTR_ERR(req);

	if (req != NULL && it && it->it_op & IT_CREAT)
		/* ask ptlrpc not to resend on EINPROGRESS since we have our own
		 * retry logic */
		req->rq_no_retry_einprogress = 1;

	if (resends) {
		req->rq_generation_set = 1;
		req->rq_import_generation = generation;
		req->rq_sent = cfs_time_current_sec() + resends;
	}

	/* It is important to obtain rpc_lock first (if applicable), so that
	 * threads that are serialised with rpc_lock are not polluting our
	 * rpcs in flight counter. We do not do flock request limiting, though*/
	if (it) {
		mdc_get_rpc_lock(obddev->u.cli.cl_rpc_lock, it);
		rc = mdc_enter_request(&obddev->u.cli);
		if (rc != 0) {
			mdc_put_rpc_lock(obddev->u.cli.cl_rpc_lock, it);
			mdc_clear_replay_flag(req, 0);
			ptlrpc_req_finished(req);
			return rc;
		}
	}

	rc = ldlm_cli_enqueue(exp, &req, einfo, &res_id, policy, &flags, NULL,
			      0, lvb_type, lockh, 0);
	if (!it) {
		/* For flock requests we immediately return without further
		   delay and let caller deal with the rest, since rest of
		   this function metadata processing makes no sense for flock
		   requests anyway. But in case of problem during comms with
		   Server (ETIMEDOUT) or any signal/kill attempt (EINTR), we
		   can not rely on caller and this mainly for F_UNLCKs
		   (explicits or automatically generated by Kernel to clean
		   current FLocks upon exit) that can't be trashed */
		if ((rc == -EINTR) || (rc == -ETIMEDOUT))
			goto resend;
		return rc;
	}

	mdc_exit_request(&obddev->u.cli);
	mdc_put_rpc_lock(obddev->u.cli.cl_rpc_lock, it);

	if (rc < 0) {
		CERROR("ldlm_cli_enqueue: %d\n", rc);
		mdc_clear_replay_flag(req, rc);
		ptlrpc_req_finished(req);
		return rc;
	}

	lockrep = req_capsule_server_get(&req->rq_pill, &RMF_DLM_REP);
	LASSERT(lockrep != NULL);

	lockrep->lock_policy_res2 =
		ptlrpc_status_ntoh(lockrep->lock_policy_res2);

	/* Retry the create infinitely when we get -EINPROGRESS from
	 * server. This is required by the new quota design. */
	if (it && it->it_op & IT_CREAT &&
	    (int)lockrep->lock_policy_res2 == -EINPROGRESS) {
		mdc_clear_replay_flag(req, rc);
		ptlrpc_req_finished(req);
		resends++;

		CDEBUG(D_HA, "%s: resend:%d op:%d "DFID"/"DFID"\n",
		       obddev->obd_name, resends, it->it_op,
		       PFID(&op_data->op_fid1), PFID(&op_data->op_fid2));

		if (generation == obddev->u.cli.cl_import->imp_generation) {
			goto resend;
		} else {
			CDEBUG(D_HA, "resend cross eviction\n");
			return -EIO;
		}
	}

	rc = mdc_finish_enqueue(exp, req, einfo, it, lockh, rc);
	if (rc < 0) {
		if (lustre_handle_is_used(lockh)) {
			ldlm_lock_decref(lockh, einfo->ei_mode);
			memset(lockh, 0, sizeof(*lockh));
		}
		ptlrpc_req_finished(req);
	}
	return rc;
}

static int mdc_finish_intent_lock(struct obd_export *exp,
				  struct ptlrpc_request *request,
				  struct md_op_data *op_data,
				  struct lookup_intent *it,
				  struct lustre_handle *lockh)
{
	struct lustre_handle old_lock;
	struct mdt_body *mdt_body;
	struct ldlm_lock *lock;
	int rc;

	LASSERT(request != NULL);
	LASSERT(request != LP_POISON);
	LASSERT(request->rq_repmsg != LP_POISON);

	if (!it_disposition(it, DISP_IT_EXECD)) {
		/* The server failed before it even started executing the
		 * intent, i.e. because it couldn't unpack the request. */
		LASSERT(it->d.lustre.it_status != 0);
		return it->d.lustre.it_status;
	}
	rc = it_open_error(DISP_IT_EXECD, it);
	if (rc)
		return rc;

	mdt_body = req_capsule_server_get(&request->rq_pill, &RMF_MDT_BODY);
	LASSERT(mdt_body != NULL);      /* mdc_enqueue checked */

	/* If we were revalidating a fid/name pair, mark the intent in
	 * case we fail and get called again from lookup */
	if (fid_is_sane(&op_data->op_fid2) &&
	    it->it_create_mode & M_CHECK_STALE &&
	    it->it_op != IT_GETATTR) {

		/* Also: did we find the same inode? */
		/* sever can return one of two fids:
		 * op_fid2 - new allocated fid - if file is created.
		 * op_fid3 - existent fid - if file only open.
		 * op_fid3 is saved in lmv_intent_open */
		if ((!lu_fid_eq(&op_data->op_fid2, &mdt_body->fid1)) &&
		    (!lu_fid_eq(&op_data->op_fid3, &mdt_body->fid1))) {
			CDEBUG(D_DENTRY, "Found stale data "DFID"("DFID")/"DFID
			       "\n", PFID(&op_data->op_fid2),
			       PFID(&op_data->op_fid2), PFID(&mdt_body->fid1));
			return -ESTALE;
		}
	}

	rc = it_open_error(DISP_LOOKUP_EXECD, it);
	if (rc)
		return rc;

	/* keep requests around for the multiple phases of the call
	 * this shows the DISP_XX must guarantee we make it into the call
	 */
	if (!it_disposition(it, DISP_ENQ_CREATE_REF) &&
	    it_disposition(it, DISP_OPEN_CREATE) &&
	    !it_open_error(DISP_OPEN_CREATE, it)) {
		it_set_disposition(it, DISP_ENQ_CREATE_REF);
		ptlrpc_request_addref(request); /* balanced in ll_create_node */
	}
	if (!it_disposition(it, DISP_ENQ_OPEN_REF) &&
	    it_disposition(it, DISP_OPEN_OPEN) &&
	    !it_open_error(DISP_OPEN_OPEN, it)) {
		it_set_disposition(it, DISP_ENQ_OPEN_REF);
		ptlrpc_request_addref(request); /* balanced in ll_file_open */
		/* BUG 11546 - eviction in the middle of open rpc processing */
		OBD_FAIL_TIMEOUT(OBD_FAIL_MDC_ENQUEUE_PAUSE, obd_timeout);
	}

	if (it->it_op & IT_CREAT) {
		/* XXX this belongs in ll_create_it */
	} else if (it->it_op == IT_OPEN) {
		LASSERT(!it_disposition(it, DISP_OPEN_CREATE));
	} else {
		LASSERT(it->it_op & (IT_GETATTR | IT_LOOKUP | IT_LAYOUT));
	}

	/* If we already have a matching lock, then cancel the new
	 * one.  We have to set the data here instead of in
	 * mdc_enqueue, because we need to use the child's inode as
	 * the l_ast_data to match, and that's not available until
	 * intent_finish has performed the iget().) */
	lock = ldlm_handle2lock(lockh);
	if (lock) {
		ldlm_policy_data_t policy = lock->l_policy_data;
		LDLM_DEBUG(lock, "matching against this");

		LASSERTF(fid_res_name_eq(&mdt_body->fid1,
					 &lock->l_resource->lr_name),
			 "Lock res_id: "DLDLMRES", fid: "DFID"\n",
			 PLDLMRES(lock->l_resource), PFID(&mdt_body->fid1));
		LDLM_LOCK_PUT(lock);

		memcpy(&old_lock, lockh, sizeof(*lockh));
		if (ldlm_lock_match(NULL, LDLM_FL_BLOCK_GRANTED, NULL,
				    LDLM_IBITS, &policy, LCK_NL, &old_lock, 0)) {
			ldlm_lock_decref_and_cancel(lockh,
						    it->d.lustre.it_lock_mode);
			memcpy(lockh, &old_lock, sizeof(old_lock));
			it->d.lustre.it_lock_handle = lockh->cookie;
		}
	}
	CDEBUG(D_DENTRY,"D_IT dentry %.*s intent: %s status %d disp %x rc %d\n",
	       op_data->op_namelen, op_data->op_name, ldlm_it2str(it->it_op),
	       it->d.lustre.it_status, it->d.lustre.it_disposition, rc);
	return rc;
}

int mdc_revalidate_lock(struct obd_export *exp, struct lookup_intent *it,
			struct lu_fid *fid, __u64 *bits)
{
	/* We could just return 1 immediately, but since we should only
	 * be called in revalidate_it if we already have a lock, let's
	 * verify that. */
	struct ldlm_res_id res_id;
	struct lustre_handle lockh;
	ldlm_policy_data_t policy;
	ldlm_mode_t mode;

	if (it->d.lustre.it_lock_handle) {
		lockh.cookie = it->d.lustre.it_lock_handle;
		mode = ldlm_revalidate_lock_handle(&lockh, bits);
	} else {
		fid_build_reg_res_name(fid, &res_id);
		switch (it->it_op) {
		case IT_GETATTR:
			/* File attributes are held under multiple bits:
			 * nlink is under lookup lock, size and times are
			 * under UPDATE lock and recently we've also got
			 * a separate permissions lock for owner/group/acl that
			 * were protected by lookup lock before.
			 * Getattr must provide all of that information,
			 * so we need to ensure we have all of those locks.
			 * Unfortunately, if the bits are split across multiple
			 * locks, there's no easy way to match all of them here,
			 * so an extra RPC would be performed to fetch all
			 * of those bits at once for now. */
			/* For new MDTs(> 2.4), UPDATE|PERM should be enough,
			 * but for old MDTs (< 2.4), permission is covered
			 * by LOOKUP lock, so it needs to match all bits here.*/
			policy.l_inodebits.bits = MDS_INODELOCK_UPDATE |
						  MDS_INODELOCK_LOOKUP |
						  MDS_INODELOCK_PERM;
			break;
		case IT_LAYOUT:
			policy.l_inodebits.bits = MDS_INODELOCK_LAYOUT;
			break;
		default:
			policy.l_inodebits.bits = MDS_INODELOCK_LOOKUP;
			break;
		}

		mode = mdc_lock_match(exp, LDLM_FL_BLOCK_GRANTED, fid,
				       LDLM_IBITS, &policy,
				      LCK_CR | LCK_CW | LCK_PR | LCK_PW,
				      &lockh);
	}

	if (mode) {
		it->d.lustre.it_lock_handle = lockh.cookie;
		it->d.lustre.it_lock_mode = mode;
	} else {
		it->d.lustre.it_lock_handle = 0;
		it->d.lustre.it_lock_mode = 0;
	}

	return !!mode;
}

/*
 * This long block is all about fixing up the lock and request state
 * so that it is correct as of the moment _before_ the operation was
 * applied; that way, the VFS will think that everything is normal and
 * call Lustre's regular VFS methods.
 *
 * If we're performing a creation, that means that unless the creation
 * failed with EEXIST, we should fake up a negative dentry.
 *
 * For everything else, we want to lookup to succeed.
 *
 * One additional note: if CREATE or OPEN succeeded, we add an extra
 * reference to the request because we need to keep it around until
 * ll_create/ll_open gets called.
 *
 * The server will return to us, in it_disposition, an indication of
 * exactly what d.lustre.it_status refers to.
 *
 * If DISP_OPEN_OPEN is set, then d.lustre.it_status refers to the open() call,
 * otherwise if DISP_OPEN_CREATE is set, then it status is the
 * creation failure mode.  In either case, one of DISP_LOOKUP_NEG or
 * DISP_LOOKUP_POS will be set, indicating whether the child lookup
 * was successful.
 *
 * Else, if DISP_LOOKUP_EXECD then d.lustre.it_status is the rc of the
 * child lookup.
 */
int mdc_intent_lock(struct obd_export *exp, struct md_op_data *op_data,
		    void *lmm, int lmmsize, struct lookup_intent *it,
		    int lookup_flags, struct ptlrpc_request **reqp,
		    ldlm_blocking_callback cb_blocking,
		    __u64 extra_lock_flags)
{
	struct ldlm_enqueue_info einfo = {
		.ei_type	= LDLM_IBITS,
		.ei_mode	= it_to_lock_mode(it),
		.ei_cb_bl	= cb_blocking,
		.ei_cb_cp	= ldlm_completion_ast,
	};
	struct lustre_handle lockh;
	int rc = 0;

	LASSERT(it);

	CDEBUG(D_DLMTRACE, "(name: %.*s,"DFID") in obj "DFID
		", intent: %s flags %#Lo\n", op_data->op_namelen,
		op_data->op_name, PFID(&op_data->op_fid2),
		PFID(&op_data->op_fid1), ldlm_it2str(it->it_op),
		it->it_flags);

	lockh.cookie = 0;
	if (fid_is_sane(&op_data->op_fid2) &&
	    (it->it_op & (IT_LOOKUP | IT_GETATTR))) {
		/* We could just return 1 immediately, but since we should only
		 * be called in revalidate_it if we already have a lock, let's
		 * verify that. */
		it->d.lustre.it_lock_handle = 0;
		rc = mdc_revalidate_lock(exp, it, &op_data->op_fid2, NULL);
		/* Only return failure if it was not GETATTR by cfid
		   (from inode_revalidate) */
		if (rc || op_data->op_namelen != 0)
			return rc;
	}

	/* For case if upper layer did not alloc fid, do it now. */
	if (!fid_is_sane(&op_data->op_fid2) && it->it_op & IT_CREAT) {
		rc = mdc_fid_alloc(exp, &op_data->op_fid2, op_data);
		if (rc < 0) {
			CERROR("Can't alloc new fid, rc %d\n", rc);
			return rc;
		}
	}
	rc = mdc_enqueue(exp, &einfo, it, op_data, &lockh, lmm, lmmsize, NULL,
			 extra_lock_flags);
	if (rc < 0)
		return rc;

	*reqp = it->d.lustre.it_data;
	rc = mdc_finish_intent_lock(exp, *reqp, op_data, it, &lockh);
	return rc;
}

static int mdc_intent_getattr_async_interpret(const struct lu_env *env,
					      struct ptlrpc_request *req,
					      void *args, int rc)
{
	struct mdc_getattr_args  *ga = args;
	struct obd_export	*exp = ga->ga_exp;
	struct md_enqueue_info   *minfo = ga->ga_minfo;
	struct ldlm_enqueue_info *einfo = ga->ga_einfo;
	struct lookup_intent     *it;
	struct lustre_handle     *lockh;
	struct obd_device	*obddev;
	struct ldlm_reply	 *lockrep;
	__u64		     flags = LDLM_FL_HAS_INTENT;

	it    = &minfo->mi_it;
	lockh = &minfo->mi_lockh;

	obddev = class_exp2obd(exp);

	mdc_exit_request(&obddev->u.cli);
	if (OBD_FAIL_CHECK(OBD_FAIL_MDC_GETATTR_ENQUEUE))
		rc = -ETIMEDOUT;

	rc = ldlm_cli_enqueue_fini(exp, req, einfo->ei_type, 1, einfo->ei_mode,
				   &flags, NULL, 0, lockh, rc);
	if (rc < 0) {
		CERROR("ldlm_cli_enqueue_fini: %d\n", rc);
		mdc_clear_replay_flag(req, rc);
		GOTO(out, rc);
	}

	lockrep = req_capsule_server_get(&req->rq_pill, &RMF_DLM_REP);
	LASSERT(lockrep != NULL);

	lockrep->lock_policy_res2 =
		ptlrpc_status_ntoh(lockrep->lock_policy_res2);

	rc = mdc_finish_enqueue(exp, req, einfo, it, lockh, rc);
	if (rc)
		GOTO(out, rc);

	rc = mdc_finish_intent_lock(exp, req, &minfo->mi_data, it, lockh);

out:
	OBD_FREE_PTR(einfo);
	minfo->mi_cb(req, minfo, rc);
	return 0;
}

int mdc_intent_getattr_async(struct obd_export *exp,
			     struct md_enqueue_info *minfo,
			     struct ldlm_enqueue_info *einfo)
{
	struct md_op_data       *op_data = &minfo->mi_data;
	struct lookup_intent    *it = &minfo->mi_it;
	struct ptlrpc_request   *req;
	struct mdc_getattr_args *ga;
	struct obd_device       *obddev = class_exp2obd(exp);
	struct ldlm_res_id       res_id;
	/*XXX: Both MDS_INODELOCK_LOOKUP and MDS_INODELOCK_UPDATE are needed
	 *     for statahead currently. Consider CMD in future, such two bits
	 *     maybe managed by different MDS, should be adjusted then. */
	ldlm_policy_data_t       policy = {
					.l_inodebits = { MDS_INODELOCK_LOOKUP |
							 MDS_INODELOCK_UPDATE }
				 };
	int		      rc = 0;
	__u64		    flags = LDLM_FL_HAS_INTENT;

	CDEBUG(D_DLMTRACE,
		"name: %.*s in inode "DFID", intent: %s flags %#Lo\n",
		op_data->op_namelen, op_data->op_name, PFID(&op_data->op_fid1),
		ldlm_it2str(it->it_op), it->it_flags);

	fid_build_reg_res_name(&op_data->op_fid1, &res_id);
	req = mdc_intent_getattr_pack(exp, it, op_data);
	if (IS_ERR(req))
		return PTR_ERR(req);

	rc = mdc_enter_request(&obddev->u.cli);
	if (rc != 0) {
		ptlrpc_req_finished(req);
		return rc;
	}

	rc = ldlm_cli_enqueue(exp, &req, einfo, &res_id, &policy, &flags, NULL,
			      0, LVB_T_NONE, &minfo->mi_lockh, 1);
	if (rc < 0) {
		mdc_exit_request(&obddev->u.cli);
		ptlrpc_req_finished(req);
		return rc;
	}

	CLASSERT(sizeof(*ga) <= sizeof(req->rq_async_args));
	ga = ptlrpc_req_async_args(req);
	ga->ga_exp = exp;
	ga->ga_minfo = minfo;
	ga->ga_einfo = einfo;

	req->rq_interpret_reply = mdc_intent_getattr_async_interpret;
	ptlrpcd_add_req(req, PDL_POLICY_LOCAL, -1);

	return 0;
}
