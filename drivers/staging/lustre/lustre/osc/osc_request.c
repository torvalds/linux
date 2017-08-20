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
 * Copyright (c) 2002, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2015, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#define DEBUG_SUBSYSTEM S_OSC

#include <linux/libcfs/libcfs.h>

#include <lustre_dlm.h>
#include <lustre_net.h>
#include <uapi/linux/lustre/lustre_idl.h>
#include <obd_cksum.h>

#include <lustre_ha.h>
#include <lprocfs_status.h>
#include <uapi/linux/lustre/lustre_ioctl.h>
#include <lustre_debug.h>
#include <lustre_obdo.h>
#include <uapi/linux/lustre/lustre_param.h>
#include <lustre_fid.h>
#include <obd_class.h>
#include <obd.h>
#include "osc_internal.h"
#include "osc_cl_internal.h"

atomic_t osc_pool_req_count;
unsigned int osc_reqpool_maxreqcount;
struct ptlrpc_request_pool *osc_rq_pool;

/* max memory used for request pool, unit is MB */
static unsigned int osc_reqpool_mem_max = 5;
module_param(osc_reqpool_mem_max, uint, 0444);

struct osc_brw_async_args {
	struct obdo       *aa_oa;
	int		aa_requested_nob;
	int		aa_nio_count;
	u32		aa_page_count;
	int		aa_resends;
	struct brw_page  **aa_ppga;
	struct client_obd *aa_cli;
	struct list_head	 aa_oaps;
	struct list_head	 aa_exts;
};

struct osc_async_args {
	struct obd_info   *aa_oi;
};

struct osc_setattr_args {
	struct obdo	 *sa_oa;
	obd_enqueue_update_f sa_upcall;
	void		*sa_cookie;
};

struct osc_fsync_args {
	struct osc_object	*fa_obj;
	struct obdo		*fa_oa;
	obd_enqueue_update_f fa_upcall;
	void		*fa_cookie;
};

struct osc_enqueue_args {
	struct obd_export	*oa_exp;
	enum ldlm_type		oa_type;
	enum ldlm_mode		oa_mode;
	__u64		    *oa_flags;
	osc_enqueue_upcall_f	oa_upcall;
	void		     *oa_cookie;
	struct ost_lvb	   *oa_lvb;
	struct lustre_handle	oa_lockh;
	unsigned int	      oa_agl:1;
};

static void osc_release_ppga(struct brw_page **ppga, u32 count);
static int brw_interpret(const struct lu_env *env,
			 struct ptlrpc_request *req, void *data, int rc);

static inline void osc_pack_req_body(struct ptlrpc_request *req,
				     struct obdo *oa)
{
	struct ost_body *body;

	body = req_capsule_client_get(&req->rq_pill, &RMF_OST_BODY);
	LASSERT(body);

	lustre_set_wire_obdo(&req->rq_import->imp_connect_data, &body->oa, oa);
}

static int osc_getattr(const struct lu_env *env, struct obd_export *exp,
		       struct obdo *oa)
{
	struct ptlrpc_request *req;
	struct ost_body *body;
	int rc;

	req = ptlrpc_request_alloc(class_exp2cliimp(exp), &RQF_OST_GETATTR);
	if (!req)
		return -ENOMEM;

	rc = ptlrpc_request_pack(req, LUSTRE_OST_VERSION, OST_GETATTR);
	if (rc) {
		ptlrpc_request_free(req);
		return rc;
	}

	osc_pack_req_body(req, oa);

	ptlrpc_request_set_replen(req);

	rc = ptlrpc_queue_wait(req);
	if (rc)
		goto out;

	body = req_capsule_server_get(&req->rq_pill, &RMF_OST_BODY);
	if (!body) {
		rc = -EPROTO;
		goto out;
	}

	CDEBUG(D_INODE, "mode: %o\n", body->oa.o_mode);
	lustre_get_wire_obdo(&req->rq_import->imp_connect_data, oa,
			     &body->oa);

	oa->o_blksize = cli_brw_size(exp->exp_obd);
	oa->o_valid |= OBD_MD_FLBLKSZ;

 out:
	ptlrpc_req_finished(req);
	return rc;
}

static int osc_setattr(const struct lu_env *env, struct obd_export *exp,
		       struct obdo *oa)
{
	struct ptlrpc_request *req;
	struct ost_body *body;
	int rc;

	LASSERT(oa->o_valid & OBD_MD_FLGROUP);

	req = ptlrpc_request_alloc(class_exp2cliimp(exp), &RQF_OST_SETATTR);
	if (!req)
		return -ENOMEM;

	rc = ptlrpc_request_pack(req, LUSTRE_OST_VERSION, OST_SETATTR);
	if (rc) {
		ptlrpc_request_free(req);
		return rc;
	}

	osc_pack_req_body(req, oa);

	ptlrpc_request_set_replen(req);

	rc = ptlrpc_queue_wait(req);
	if (rc)
		goto out;

	body = req_capsule_server_get(&req->rq_pill, &RMF_OST_BODY);
	if (!body) {
		rc = -EPROTO;
		goto out;
	}

	lustre_get_wire_obdo(&req->rq_import->imp_connect_data, oa,
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
		goto out;

	body = req_capsule_server_get(&req->rq_pill, &RMF_OST_BODY);
	if (!body) {
		rc = -EPROTO;
		goto out;
	}

	lustre_get_wire_obdo(&req->rq_import->imp_connect_data, sa->sa_oa,
			     &body->oa);
out:
	rc = sa->sa_upcall(sa->sa_cookie, rc);
	return rc;
}

int osc_setattr_async(struct obd_export *exp, struct obdo *oa,
		      obd_enqueue_update_f upcall, void *cookie,
		      struct ptlrpc_request_set *rqset)
{
	struct ptlrpc_request *req;
	struct osc_setattr_args *sa;
	int rc;

	req = ptlrpc_request_alloc(class_exp2cliimp(exp), &RQF_OST_SETATTR);
	if (!req)
		return -ENOMEM;

	rc = ptlrpc_request_pack(req, LUSTRE_OST_VERSION, OST_SETATTR);
	if (rc) {
		ptlrpc_request_free(req);
		return rc;
	}

	osc_pack_req_body(req, oa);

	ptlrpc_request_set_replen(req);

	/* do mds to ost setattr asynchronously */
	if (!rqset) {
		/* Do not wait for response. */
		ptlrpcd_add_req(req);
	} else {
		req->rq_interpret_reply =
			(ptlrpc_interpterer_t)osc_setattr_interpret;

		BUILD_BUG_ON(sizeof(*sa) > sizeof(req->rq_async_args));
		sa = ptlrpc_req_async_args(req);
		sa->sa_oa = oa;
		sa->sa_upcall = upcall;
		sa->sa_cookie = cookie;

		if (rqset == PTLRPCD_SET)
			ptlrpcd_add_req(req);
		else
			ptlrpc_set_add_req(rqset, req);
	}

	return 0;
}

static int osc_create(const struct lu_env *env, struct obd_export *exp,
		      struct obdo *oa)
{
	struct ptlrpc_request *req;
	struct ost_body *body;
	int rc;

	LASSERT(oa);
	LASSERT(oa->o_valid & OBD_MD_FLGROUP);
	LASSERT(fid_seq_is_echo(ostid_seq(&oa->o_oi)));

	req = ptlrpc_request_alloc(class_exp2cliimp(exp), &RQF_OST_CREATE);
	if (!req) {
		rc = -ENOMEM;
		goto out;
	}

	rc = ptlrpc_request_pack(req, LUSTRE_OST_VERSION, OST_CREATE);
	if (rc) {
		ptlrpc_request_free(req);
		goto out;
	}

	body = req_capsule_client_get(&req->rq_pill, &RMF_OST_BODY);
	LASSERT(body);

	lustre_set_wire_obdo(&req->rq_import->imp_connect_data, &body->oa, oa);

	ptlrpc_request_set_replen(req);

	rc = ptlrpc_queue_wait(req);
	if (rc)
		goto out_req;

	body = req_capsule_server_get(&req->rq_pill, &RMF_OST_BODY);
	if (!body) {
		rc = -EPROTO;
		goto out_req;
	}

	CDEBUG(D_INFO, "oa flags %x\n", oa->o_flags);
	lustre_get_wire_obdo(&req->rq_import->imp_connect_data, oa, &body->oa);

	oa->o_blksize = cli_brw_size(exp->exp_obd);
	oa->o_valid |= OBD_MD_FLBLKSZ;

	CDEBUG(D_HA, "transno: %lld\n",
	       lustre_msg_get_transno(req->rq_repmsg));
out_req:
	ptlrpc_req_finished(req);
out:
	return rc;
}

int osc_punch_base(struct obd_export *exp, struct obdo *oa,
		   obd_enqueue_update_f upcall, void *cookie,
		   struct ptlrpc_request_set *rqset)
{
	struct ptlrpc_request *req;
	struct osc_setattr_args *sa;
	struct ost_body *body;
	int rc;

	req = ptlrpc_request_alloc(class_exp2cliimp(exp), &RQF_OST_PUNCH);
	if (!req)
		return -ENOMEM;

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
			     oa);

	ptlrpc_request_set_replen(req);

	req->rq_interpret_reply = (ptlrpc_interpterer_t)osc_setattr_interpret;
	BUILD_BUG_ON(sizeof(*sa) > sizeof(req->rq_async_args));
	sa = ptlrpc_req_async_args(req);
	sa->sa_oa = oa;
	sa->sa_upcall = upcall;
	sa->sa_cookie = cookie;
	if (rqset == PTLRPCD_SET)
		ptlrpcd_add_req(req);
	else
		ptlrpc_set_add_req(rqset, req);

	return 0;
}

static int osc_sync_interpret(const struct lu_env *env,
			      struct ptlrpc_request *req,
			      void *arg, int rc)
{
	struct cl_attr *attr = &osc_env_info(env)->oti_attr;
	struct osc_fsync_args *fa = arg;
	unsigned long valid = 0;
	struct ost_body *body;
	struct cl_object *obj;

	if (rc)
		goto out;

	body = req_capsule_server_get(&req->rq_pill, &RMF_OST_BODY);
	if (!body) {
		CERROR("can't unpack ost_body\n");
		rc = -EPROTO;
		goto out;
	}

	*fa->fa_oa = body->oa;
	obj = osc2cl(fa->fa_obj);

	/* Update osc object's blocks attribute */
	cl_object_attr_lock(obj);
	if (body->oa.o_valid & OBD_MD_FLBLOCKS) {
		attr->cat_blocks = body->oa.o_blocks;
		valid |= CAT_BLOCKS;
	}

	if (valid)
		cl_object_attr_update(env, obj, attr, valid);
	cl_object_attr_unlock(obj);

out:
	rc = fa->fa_upcall(fa->fa_cookie, rc);
	return rc;
}

int osc_sync_base(struct osc_object *obj, struct obdo *oa,
		  obd_enqueue_update_f upcall, void *cookie,
		  struct ptlrpc_request_set *rqset)
{
	struct obd_export *exp = osc_export(obj);
	struct ptlrpc_request *req;
	struct ost_body *body;
	struct osc_fsync_args *fa;
	int rc;

	req = ptlrpc_request_alloc(class_exp2cliimp(exp), &RQF_OST_SYNC);
	if (!req)
		return -ENOMEM;

	rc = ptlrpc_request_pack(req, LUSTRE_OST_VERSION, OST_SYNC);
	if (rc) {
		ptlrpc_request_free(req);
		return rc;
	}

	/* overload the size and blocks fields in the oa with start/end */
	body = req_capsule_client_get(&req->rq_pill, &RMF_OST_BODY);
	LASSERT(body);
	lustre_set_wire_obdo(&req->rq_import->imp_connect_data, &body->oa,
			     oa);

	ptlrpc_request_set_replen(req);
	req->rq_interpret_reply = osc_sync_interpret;

	BUILD_BUG_ON(sizeof(*fa) > sizeof(req->rq_async_args));
	fa = ptlrpc_req_async_args(req);
	fa->fa_obj = obj;
	fa->fa_oa = oa;
	fa->fa_upcall = upcall;
	fa->fa_cookie = cookie;

	if (rqset == PTLRPCD_SET)
		ptlrpcd_add_req(req);
	else
		ptlrpc_set_add_req(rqset, req);

	return 0;
}

/* Find and cancel locally locks matched by @mode in the resource found by
 * @objid. Found locks are added into @cancel list. Returns the amount of
 * locks added to @cancels list.
 */
static int osc_resource_get_unused(struct obd_export *exp, struct obdo *oa,
				   struct list_head *cancels,
				   enum ldlm_mode mode, __u64 lock_flags)
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
	 * locally, without sending any RPC.
	 */
	if (exp_connect_cancelset(exp) && !ns_connect_cancelset(ns))
		return 0;

	ostid_build_res_name(&oa->o_oi, &res_id);
	res = ldlm_resource_get(ns, NULL, &res_id, 0, 0);
	if (IS_ERR(res))
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

static int osc_destroy(const struct lu_env *env, struct obd_export *exp,
		       struct obdo *oa)
{
	struct client_obd *cli = &exp->exp_obd->u.cli;
	struct ptlrpc_request *req;
	struct ost_body *body;
	LIST_HEAD(cancels);
	int rc, count;

	if (!oa) {
		CDEBUG(D_INFO, "oa NULL\n");
		return -EINVAL;
	}

	count = osc_resource_get_unused(exp, oa, &cancels, LCK_PW,
					LDLM_FL_DISCARD_DATA);

	req = ptlrpc_request_alloc(class_exp2cliimp(exp), &RQF_OST_DESTROY);
	if (!req) {
		ldlm_lock_list_put(&cancels, l_bl_ast, count);
		return -ENOMEM;
	}

	rc = ldlm_prep_elc_req(exp, req, LUSTRE_OST_VERSION, OST_DESTROY,
			       0, &cancels, count);
	if (rc) {
		ptlrpc_request_free(req);
		return rc;
	}

	req->rq_request_portal = OST_IO_PORTAL; /* bug 7198 */
	ptlrpc_at_set_req_timeout(req);

	body = req_capsule_client_get(&req->rq_pill, &RMF_OST_BODY);
	LASSERT(body);
	lustre_set_wire_obdo(&req->rq_import->imp_connect_data, &body->oa, oa);

	ptlrpc_request_set_replen(req);

	req->rq_interpret_reply = osc_destroy_interpret;
	if (!osc_can_send_destroy(cli)) {
		struct l_wait_info lwi = LWI_INTR(LWI_ON_SIGNAL_NOOP, NULL);

		/*
		 * Wait until the number of on-going destroy RPCs drops
		 * under max_rpc_in_flight
		 */
		l_wait_event_exclusive(cli->cl_destroy_waitq,
				       osc_can_send_destroy(cli), &lwi);
	}

	/* Do not wait for response */
	ptlrpcd_add_req(req);
	return 0;
}

static void osc_announce_cached(struct client_obd *cli, struct obdo *oa,
				long writing_bytes)
{
	u32 bits = OBD_MD_FLBLOCKS | OBD_MD_FLGRANT;

	LASSERT(!(oa->o_valid & bits));

	oa->o_valid |= bits;
	spin_lock(&cli->cl_loi_list_lock);
	oa->o_dirty = cli->cl_dirty_pages << PAGE_SHIFT;
	if (unlikely(cli->cl_dirty_pages - cli->cl_dirty_transit >
		     cli->cl_dirty_max_pages)) {
		CERROR("dirty %lu - %lu > dirty_max %lu\n",
		       cli->cl_dirty_pages, cli->cl_dirty_transit,
		       cli->cl_dirty_max_pages);
		oa->o_undirty = 0;
	} else if (unlikely(atomic_long_read(&obd_dirty_pages) -
			    atomic_long_read(&obd_dirty_transit_pages) >
			    (long)(obd_max_dirty_pages + 1))) {
		/* The atomic_read() allowing the atomic_inc() are
		 * not covered by a lock thus they may safely race and trip
		 * this CERROR() unless we add in a small fudge factor (+1).
		 */
		CERROR("%s: dirty %ld + %ld > system dirty_max %ld\n",
		       cli_name(cli), atomic_long_read(&obd_dirty_pages),
		       atomic_long_read(&obd_dirty_transit_pages),
		       obd_max_dirty_pages);
		oa->o_undirty = 0;
	} else if (unlikely(cli->cl_dirty_max_pages - cli->cl_dirty_pages >
		   0x7fffffff)) {
		CERROR("dirty %lu - dirty_max %lu too big???\n",
		       cli->cl_dirty_pages, cli->cl_dirty_max_pages);
		oa->o_undirty = 0;
	} else {
		unsigned long max_in_flight;

		max_in_flight = (cli->cl_max_pages_per_rpc << PAGE_SHIFT) *
				(cli->cl_max_rpcs_in_flight + 1);
		oa->o_undirty = max(cli->cl_dirty_max_pages << PAGE_SHIFT,
				    max_in_flight);
	}
	oa->o_grant = cli->cl_avail_grant + cli->cl_reserved_grant;
	oa->o_dropped = cli->cl_lost_grant;
	cli->cl_lost_grant = 0;
	spin_unlock(&cli->cl_loi_list_lock);
	CDEBUG(D_CACHE, "dirty: %llu undirty: %u dropped %u grant: %llu\n",
	       oa->o_dirty, oa->o_undirty, oa->o_dropped, oa->o_grant);
}

void osc_update_next_shrink(struct client_obd *cli)
{
	cli->cl_next_shrink_grant =
		cfs_time_shift(cli->cl_grant_shrink_interval);
	CDEBUG(D_CACHE, "next time %ld to shrink grant\n",
	       cli->cl_next_shrink_grant);
}

static void __osc_update_grant(struct client_obd *cli, u64 grant)
{
	spin_lock(&cli->cl_loi_list_lock);
	cli->cl_avail_grant += grant;
	spin_unlock(&cli->cl_loi_list_lock);
}

static void osc_update_grant(struct client_obd *cli, struct ost_body *body)
{
	if (body->oa.o_valid & OBD_MD_FLGRANT) {
		CDEBUG(D_CACHE, "got %llu extra grant\n", body->oa.o_grant);
		__osc_update_grant(cli, body->oa.o_grant);
	}
}

static int osc_set_info_async(const struct lu_env *env, struct obd_export *exp,
			      u32 keylen, void *key, u32 vallen,
			      void *val, struct ptlrpc_request_set *set);

static int osc_shrink_grant_interpret(const struct lu_env *env,
				      struct ptlrpc_request *req,
				      void *aa, int rc)
{
	struct client_obd *cli = &req->rq_import->imp_obd->u.cli;
	struct obdo *oa = ((struct osc_brw_async_args *)aa)->aa_oa;
	struct ost_body *body;

	if (rc != 0) {
		__osc_update_grant(cli, oa->o_grant);
		goto out;
	}

	body = req_capsule_server_get(&req->rq_pill, &RMF_OST_BODY);
	LASSERT(body);
	osc_update_grant(cli, body);
out:
	kmem_cache_free(obdo_cachep, oa);
	return rc;
}

static void osc_shrink_grant_local(struct client_obd *cli, struct obdo *oa)
{
	spin_lock(&cli->cl_loi_list_lock);
	oa->o_grant = cli->cl_avail_grant / 4;
	cli->cl_avail_grant -= oa->o_grant;
	spin_unlock(&cli->cl_loi_list_lock);
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
 * needed, and avoids shrinking the grant piecemeal.
 */
static int osc_shrink_grant(struct client_obd *cli)
{
	__u64 target_bytes = (cli->cl_max_rpcs_in_flight + 1) *
			     (cli->cl_max_pages_per_rpc << PAGE_SHIFT);

	spin_lock(&cli->cl_loi_list_lock);
	if (cli->cl_avail_grant <= target_bytes)
		target_bytes = cli->cl_max_pages_per_rpc << PAGE_SHIFT;
	spin_unlock(&cli->cl_loi_list_lock);

	return osc_shrink_grant_to_target(cli, target_bytes);
}

int osc_shrink_grant_to_target(struct client_obd *cli, __u64 target_bytes)
{
	int rc = 0;
	struct ost_body	*body;

	spin_lock(&cli->cl_loi_list_lock);
	/* Don't shrink if we are already above or below the desired limit
	 * We don't want to shrink below a single RPC, as that will negatively
	 * impact block allocation and long-term performance.
	 */
	if (target_bytes < cli->cl_max_pages_per_rpc << PAGE_SHIFT)
		target_bytes = cli->cl_max_pages_per_rpc << PAGE_SHIFT;

	if (target_bytes >= cli->cl_avail_grant) {
		spin_unlock(&cli->cl_loi_list_lock);
		return 0;
	}
	spin_unlock(&cli->cl_loi_list_lock);

	body = kzalloc(sizeof(*body), GFP_NOFS);
	if (!body)
		return -ENOMEM;

	osc_announce_cached(cli, &body->oa, 0);

	spin_lock(&cli->cl_loi_list_lock);
	body->oa.o_grant = cli->cl_avail_grant - target_bytes;
	cli->cl_avail_grant = target_bytes;
	spin_unlock(&cli->cl_loi_list_lock);
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
	kfree(body);
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
		 * Keep comment here so that it can be found by searching.
		 */
		int brw_size = client->cl_max_pages_per_rpc << PAGE_SHIFT;

		if (client->cl_import->imp_state == LUSTRE_IMP_FULL &&
		    client->cl_avail_grant > brw_size)
			return 1;

		osc_update_next_shrink(client);
	}
	return 0;
}

static int osc_grant_shrink_grant_cb(struct timeout_item *item, void *data)
{
	struct client_obd *client;

	list_for_each_entry(client, &item->ti_obd_list, cl_grant_shrink_list) {
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
		CERROR("add grant client %s error %d\n", cli_name(client), rc);
		return rc;
	}
	CDEBUG(D_CACHE, "add grant client %s\n", cli_name(client));
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
	 * been evicted, it's the new avail_grant amount, cl_dirty_pages will
	 * drop to 0 as inflight RPCs fail out; otherwise, it's avail_grant +
	 * dirty.
	 *
	 * race is tolerable here: if we're evicted, but imp_state already
	 * left EVICTED state, then cl_dirty_pages must be 0 already.
	 */
	spin_lock(&cli->cl_loi_list_lock);
	if (cli->cl_import->imp_state == LUSTRE_IMP_EVICTED)
		cli->cl_avail_grant = ocd->ocd_grant;
	else
		cli->cl_avail_grant = ocd->ocd_grant -
				      (cli->cl_dirty_pages << PAGE_SHIFT);

	/* determine the appropriate chunk size used by osc_extent. */
	cli->cl_chunkbits = max_t(int, PAGE_SHIFT, ocd->ocd_blocksize);
	spin_unlock(&cli->cl_loi_list_lock);

	CDEBUG(D_CACHE, "%s, setting cl_avail_grant: %ld cl_lost_grant: %ld chunk bits: %d\n",
	       cli_name(cli), cli->cl_avail_grant, cli->cl_lost_grant,
	       cli->cl_chunkbits);

	if (ocd->ocd_connect_flags & OBD_CONNECT_GRANT_SHRINK &&
	    list_empty(&cli->cl_grant_shrink_list))
		osc_add_shrink_grant(cli);
}

/* We assume that the reason this OSC got a short read is because it read
 * beyond the end of a stripe file; i.e. lustre is reading a sparse file
 * via the LOV, and it _knows_ it's reading inside the file, it's just that
 * this stripe never got written at or beyond this stripe offset yet.
 */
static void handle_short_read(int nob_read, u32 page_count,
			      struct brw_page **pga)
{
	char *ptr;
	int i = 0;

	/* skip bytes read OK */
	while (nob_read > 0) {
		LASSERT(page_count > 0);

		if (pga[i]->count > nob_read) {
			/* EOF inside this page */
			ptr = kmap(pga[i]->pg) +
				(pga[i]->off & ~PAGE_MASK);
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
		ptr = kmap(pga[i]->pg) + (pga[i]->off & ~PAGE_MASK);
		memset(ptr, 0, pga[i]->count);
		kunmap(pga[i]->pg);
		i++;
	}
}

static int check_write_rcs(struct ptlrpc_request *req,
			   int requested_nob, int niocount,
			   u32 page_count, struct brw_page **pga)
{
	int i;
	__u32 *remote_rcs;

	remote_rcs = req_capsule_server_sized_get(&req->rq_pill, &RMF_RCS,
						  sizeof(*remote_rcs) *
						  niocount);
	if (!remote_rcs) {
		CDEBUG(D_INFO, "Missing/short RC vector on BRW_WRITE reply\n");
		return -EPROTO;
	}

	/* return error if any niobuf was in error */
	for (i = 0; i < niocount; i++) {
		if ((int)remote_rcs[i] < 0)
			return remote_rcs[i];

		if (remote_rcs[i] != 0) {
			CDEBUG(D_INFO, "rc[%d] invalid (%d) req %p\n",
			       i, remote_rcs[i], req);
			return -EPROTO;
		}
	}

	if (req->rq_bulk->bd_nob_transferred != requested_nob) {
		CERROR("Unexpected # bytes transferred: %d (requested %d)\n",
		       req->rq_bulk->bd_nob_transferred, requested_nob);
		return -EPROTO;
	}

	return 0;
}

static inline int can_merge_pages(struct brw_page *p1, struct brw_page *p2)
{
	if (p1->flag != p2->flag) {
		unsigned int mask = ~(OBD_BRW_FROM_GRANT | OBD_BRW_NOCACHE |
				      OBD_BRW_SYNC | OBD_BRW_ASYNC |
				      OBD_BRW_NOQUOTA | OBD_BRW_SOFT_SYNC);

		/* warn if we try to combine flags that we don't know to be
		 * safe to combine
		 */
		if (unlikely((p1->flag & mask) != (p2->flag & mask))) {
			CWARN("Saw flags 0x%x and 0x%x in the same brw, please report this at http://bugs.whamcloud.com/\n",
			      p1->flag, p2->flag);
		}
		return 0;
	}

	return (p1->off + p1->count == p2->off);
}

static u32 osc_checksum_bulk(int nob, u32 pg_count,
			     struct brw_page **pga, int opc,
			     enum cksum_type cksum_type)
{
	__u32 cksum;
	int i = 0;
	struct cfs_crypto_hash_desc *hdesc;
	unsigned int bufsize;
	unsigned char cfs_alg = cksum_obd2cfs(cksum_type);

	LASSERT(pg_count > 0);

	hdesc = cfs_crypto_hash_init(cfs_alg, NULL, 0);
	if (IS_ERR(hdesc)) {
		CERROR("Unable to initialize checksum hash %s\n",
		       cfs_crypto_hash_name(cfs_alg));
		return PTR_ERR(hdesc);
	}

	while (nob > 0 && pg_count > 0) {
		unsigned int count = pga[i]->count > nob ? nob : pga[i]->count;

		/* corrupt the data before we compute the checksum, to
		 * simulate an OST->client data error
		 */
		if (i == 0 && opc == OST_READ &&
		    OBD_FAIL_CHECK(OBD_FAIL_OSC_CHECKSUM_RECEIVE)) {
			unsigned char *ptr = kmap(pga[i]->pg);
			int off = pga[i]->off & ~PAGE_MASK;

			memcpy(ptr + off, "bad1", min_t(typeof(nob), 4, nob));
			kunmap(pga[i]->pg);
		}
		cfs_crypto_hash_update_page(hdesc, pga[i]->pg,
					    pga[i]->off & ~PAGE_MASK,
				  count);
		CDEBUG(D_PAGE,
		       "page %p map %p index %lu flags %lx count %u priv %0lx: off %d\n",
		       pga[i]->pg, pga[i]->pg->mapping, pga[i]->pg->index,
		       (long)pga[i]->pg->flags, page_count(pga[i]->pg),
		       page_private(pga[i]->pg),
		       (int)(pga[i]->off & ~PAGE_MASK));

		nob -= pga[i]->count;
		pg_count--;
		i++;
	}

	bufsize = sizeof(cksum);
	cfs_crypto_hash_final(hdesc, (unsigned char *)&cksum, &bufsize);

	/* For sending we only compute the wrong checksum instead
	 * of corrupting the data so it is still correct on a redo
	 */
	if (opc == OST_WRITE && OBD_FAIL_CHECK(OBD_FAIL_OSC_CHECKSUM_SEND))
		cksum++;

	return cksum;
}

static int osc_brw_prep_request(int cmd, struct client_obd *cli,
				struct obdo *oa, u32 page_count,
				struct brw_page **pga,
				struct ptlrpc_request **reqp,
				int reserve,
				int resend)
{
	struct ptlrpc_request *req;
	struct ptlrpc_bulk_desc *desc;
	struct ost_body	*body;
	struct obd_ioobj *ioobj;
	struct niobuf_remote *niobuf;
	int niocount, i, requested_nob, opc, rc;
	struct osc_brw_async_args *aa;
	struct req_capsule *pill;
	struct brw_page *pg_prev;

	if (OBD_FAIL_CHECK(OBD_FAIL_OSC_BRW_PREP_REQ))
		return -ENOMEM; /* Recoverable */
	if (OBD_FAIL_CHECK(OBD_FAIL_OSC_BRW_PREP_REQ2))
		return -EINVAL; /* Fatal */

	if ((cmd & OBD_BRW_WRITE) != 0) {
		opc = OST_WRITE;
		req = ptlrpc_request_alloc_pool(cli->cl_import,
						osc_rq_pool,
						&RQF_OST_BRW_WRITE);
	} else {
		opc = OST_READ;
		req = ptlrpc_request_alloc(cli->cl_import, &RQF_OST_BRW_READ);
	}
	if (!req)
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

	rc = ptlrpc_request_pack(req, LUSTRE_OST_VERSION, opc);
	if (rc) {
		ptlrpc_request_free(req);
		return rc;
	}
	req->rq_request_portal = OST_IO_PORTAL; /* bug 7198 */
	ptlrpc_at_set_req_timeout(req);
	/* ask ptlrpc not to resend on EINPROGRESS since BRWs have their own
	 * retry logic
	 */
	req->rq_no_retry_einprogress = 1;

	desc = ptlrpc_prep_bulk_imp(req, page_count,
		cli->cl_import->imp_connect_data.ocd_brw_size >> LNET_MTU_BITS,
		(opc == OST_WRITE ? PTLRPC_BULK_GET_SOURCE :
		 PTLRPC_BULK_PUT_SINK) | PTLRPC_BULK_BUF_KIOV, OST_BULK_PORTAL,
		 &ptlrpc_bulk_kiov_pin_ops);

	if (!desc) {
		rc = -ENOMEM;
		goto out;
	}
	/* NB request now owns desc and will free it when it gets freed */

	body = req_capsule_client_get(pill, &RMF_OST_BODY);
	ioobj = req_capsule_client_get(pill, &RMF_OBD_IOOBJ);
	niobuf = req_capsule_client_get(pill, &RMF_NIOBUF_REMOTE);
	LASSERT(body && ioobj && niobuf);

	lustre_set_wire_obdo(&req->rq_import->imp_connect_data, &body->oa, oa);

	obdo_to_ioobj(oa, ioobj);
	ioobj->ioo_bufcnt = niocount;
	/* The high bits of ioo_max_brw tells server _maximum_ number of bulks
	 * that might be send for this request.  The actual number is decided
	 * when the RPC is finally sent in ptlrpc_register_bulk(). It sends
	 * "max - 1" for old client compatibility sending "0", and also so the
	 * the actual maximum is a power-of-two number, not one less. LU-1431
	 */
	ioobj_max_brw_set(ioobj, desc->bd_md_max_brw);
	LASSERT(page_count > 0);
	pg_prev = pga[0];
	for (requested_nob = i = 0; i < page_count; i++, niobuf++) {
		struct brw_page *pg = pga[i];
		int poff = pg->off & ~PAGE_MASK;

		LASSERT(pg->count > 0);
		/* make sure there is no gap in the middle of page array */
		LASSERTF(page_count == 1 ||
			 (ergo(i == 0, poff + pg->count == PAGE_SIZE) &&
			  ergo(i > 0 && i < page_count - 1,
			       poff == 0 && pg->count == PAGE_SIZE)   &&
			  ergo(i == page_count - 1, poff == 0)),
			 "i: %d/%d pg: %p off: %llu, count: %u\n",
			 i, page_count, pg, pg->off, pg->count);
		LASSERTF(i == 0 || pg->off > pg_prev->off,
			 "i %d p_c %u pg %p [pri %lu ind %lu] off %llu prev_pg %p [pri %lu ind %lu] off %llu\n",
			 i, page_count,
			 pg->pg, page_private(pg->pg), pg->pg->index, pg->off,
			 pg_prev->pg, page_private(pg_prev->pg),
			 pg_prev->pg->index, pg_prev->off);
		LASSERT((pga[0]->flag & OBD_BRW_SRVLOCK) ==
			(pg->flag & OBD_BRW_SRVLOCK));

		desc->bd_frag_ops->add_kiov_frag(desc, pg->pg, poff, pg->count);
		requested_nob += pg->count;

		if (i > 0 && can_merge_pages(pg_prev, pg)) {
			niobuf--;
			niobuf->rnb_len += pg->count;
		} else {
			niobuf->rnb_offset = pg->off;
			niobuf->rnb_len = pg->count;
			niobuf->rnb_flags = pg->flag;
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
			 * it can be changed via lprocfs
			 */
			enum cksum_type cksum_type = cli->cl_cksum_type;

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
			 * resend but cl_checksum is no longer set. b=11238
			 */
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

	BUILD_BUG_ON(sizeof(*aa) > sizeof(req->rq_async_args));
	aa = ptlrpc_req_async_args(req);
	aa->aa_oa = oa;
	aa->aa_requested_nob = requested_nob;
	aa->aa_nio_count = niocount;
	aa->aa_page_count = page_count;
	aa->aa_resends = 0;
	aa->aa_ppga = pga;
	aa->aa_cli = cli;
	INIT_LIST_HEAD(&aa->aa_oaps);

	*reqp = req;
	niobuf = req_capsule_client_get(pill, &RMF_NIOBUF_REMOTE);
	CDEBUG(D_RPCTRACE, "brw rpc %p - object " DOSTID " offset %lld<>%lld\n",
	       req, POSTID(&oa->o_oi), niobuf[0].rnb_offset,
	       niobuf[niocount - 1].rnb_offset + niobuf[niocount - 1].rnb_len);

	return 0;

 out:
	ptlrpc_req_finished(req);
	return rc;
}

static int check_write_checksum(struct obdo *oa,
				const struct lnet_process_id *peer,
				__u32 client_cksum, __u32 server_cksum, int nob,
				u32 page_count, struct brw_page **pga,
				enum cksum_type client_cksum_type)
{
	__u32 new_cksum;
	char *msg;
	enum cksum_type cksum_type;

	if (server_cksum == client_cksum) {
		CDEBUG(D_PAGE, "checksum %x confirmed\n", client_cksum);
		return 0;
	}

	cksum_type = cksum_type_unpack(oa->o_valid & OBD_MD_FLFLAGS ?
				       oa->o_flags : 0);
	new_cksum = osc_checksum_bulk(nob, page_count, pga, OST_WRITE,
				      cksum_type);

	if (cksum_type != client_cksum_type)
		msg = "the server did not use the checksum type specified in the original request - likely a protocol problem"
			;
	else if (new_cksum == server_cksum)
		msg = "changed on the client after we checksummed it - likely false positive due to mmap IO (bug 11742)"
			;
	else if (new_cksum == client_cksum)
		msg = "changed in transit before arrival at OST";
	else
		msg = "changed in transit AND doesn't match the original - likely false positive due to mmap IO (bug 11742)"
			;

	LCONSOLE_ERROR_MSG(0x132, "BAD WRITE CHECKSUM: %s: from %s inode " DFID " object " DOSTID " extent [%llu-%llu]\n",
			   msg, libcfs_nid2str(peer->nid),
			   oa->o_valid & OBD_MD_FLFID ? oa->o_parent_seq : (__u64)0,
			   oa->o_valid & OBD_MD_FLFID ? oa->o_parent_oid : 0,
			   oa->o_valid & OBD_MD_FLFID ? oa->o_parent_ver : 0,
			   POSTID(&oa->o_oi), pga[0]->off,
			   pga[page_count - 1]->off +
			   pga[page_count - 1]->count - 1);
	CERROR("original client csum %x (type %x), server csum %x (type %x), client csum now %x\n",
	       client_cksum, client_cksum_type,
	       server_cksum, cksum_type, new_cksum);
	return 1;
}

/* Note rc enters this function as number of bytes transferred */
static int osc_brw_fini_request(struct ptlrpc_request *req, int rc)
{
	struct osc_brw_async_args *aa = (void *)&req->rq_async_args;
	const struct lnet_process_id *peer =
			&req->rq_import->imp_connection->c_peer;
	struct client_obd *cli = aa->aa_cli;
	struct ost_body *body;
	__u32 client_cksum = 0;

	if (rc < 0 && rc != -EDQUOT) {
		DEBUG_REQ(D_INFO, req, "Failed request with rc = %d\n", rc);
		return rc;
	}

	LASSERTF(req->rq_repmsg, "rc = %d\n", rc);
	body = req_capsule_server_get(&req->rq_pill, &RMF_OST_BODY);
	if (!body) {
		DEBUG_REQ(D_INFO, req, "Can't unpack body\n");
		return -EPROTO;
	}

	/* set/clear over quota flag for a uid/gid */
	if (lustre_msg_get_opc(req->rq_reqmsg) == OST_WRITE &&
	    body->oa.o_valid & (OBD_MD_FLUSRQUOTA | OBD_MD_FLGRPQUOTA)) {
		unsigned int qid[MAXQUOTAS] = { body->oa.o_uid, body->oa.o_gid };

		CDEBUG(D_QUOTA, "setdq for [%u %u] with valid %#llx, flags %x\n",
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

		rc = check_write_rcs(req, aa->aa_requested_nob,
				     aa->aa_nio_count,
				     aa->aa_page_count, aa->aa_ppga);
		goto out;
	}

	/* The rest of this function executes only for OST_READs */

	/* if unwrap_bulk failed, return -EAGAIN to retry */
	rc = sptlrpc_cli_unwrap_bulk_read(req, req->rq_bulk, rc);
	if (rc < 0) {
		rc = -EAGAIN;
		goto out;
	}

	if (rc > aa->aa_requested_nob) {
		CERROR("Unexpected rc %d (%d requested)\n", rc,
		       aa->aa_requested_nob);
		return -EPROTO;
	}

	if (rc != req->rq_bulk->bd_nob_transferred) {
		CERROR("Unexpected rc %d (%d transferred)\n",
		       rc, req->rq_bulk->bd_nob_transferred);
		return -EPROTO;
	}

	if (rc < aa->aa_requested_nob)
		handle_short_read(rc, aa->aa_page_count, aa->aa_ppga);

	if (body->oa.o_valid & OBD_MD_FLCKSUM) {
		static int cksum_counter;
		__u32 server_cksum = body->oa.o_cksum;
		char *via = "";
		char *router = "";
		enum cksum_type cksum_type;

		cksum_type = cksum_type_unpack(body->oa.o_valid &
					       OBD_MD_FLFLAGS ?
					       body->oa.o_flags : 0);
		client_cksum = osc_checksum_bulk(rc, aa->aa_page_count,
						 aa->aa_ppga, OST_READ,
						 cksum_type);

		if (peer->nid != req->rq_bulk->bd_sender) {
			via = " via ";
			router = libcfs_nid2str(req->rq_bulk->bd_sender);
		}

		if (server_cksum != client_cksum) {
			LCONSOLE_ERROR_MSG(0x133, "%s: BAD READ CHECKSUM: from %s%s%s inode " DFID " object " DOSTID " extent [%llu-%llu]\n",
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

static int osc_brw_redo_request(struct ptlrpc_request *request,
				struct osc_brw_async_args *aa, int rc)
{
	struct ptlrpc_request *new_req;
	struct osc_brw_async_args *new_aa;
	struct osc_async_page *oap;

	DEBUG_REQ(rc == -EINPROGRESS ? D_RPCTRACE : D_ERROR, request,
		  "redo for recoverable error %d", rc);

	rc = osc_brw_prep_request(lustre_msg_get_opc(request->rq_reqmsg) ==
					OST_WRITE ? OBD_BRW_WRITE : OBD_BRW_READ,
				  aa->aa_cli, aa->aa_oa,
				  aa->aa_page_count, aa->aa_ppga,
				  &new_req, 0, 1);
	if (rc)
		return rc;

	list_for_each_entry(oap, &aa->aa_oaps, oap_rpc_item) {
		if (oap->oap_request) {
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
	 * Note that copying a list_head doesn't work, need to move it...
	 */
	aa->aa_resends++;
	new_req->rq_interpret_reply = request->rq_interpret_reply;
	new_req->rq_async_args = request->rq_async_args;
	new_req->rq_commit_cb = request->rq_commit_cb;
	/* cap resend delay to the current request timeout, this is similar to
	 * what ptlrpc does (see after_reply())
	 */
	if (aa->aa_resends > new_req->rq_timeout)
		new_req->rq_sent = ktime_get_real_seconds() + new_req->rq_timeout;
	else
		new_req->rq_sent = ktime_get_real_seconds() + aa->aa_resends;
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

	/* XXX: This code will run into problem if we're going to support
	 * to add a series of BRW RPCs into a self-defined ptlrpc_request_set
	 * and wait for all of them to be finished. We should inherit request
	 * set from old request.
	 */
	ptlrpcd_add_req(new_req);

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

static void osc_release_ppga(struct brw_page **ppga, u32 count)
{
	LASSERT(ppga);
	kfree(ppga);
}

static int brw_interpret(const struct lu_env *env,
			 struct ptlrpc_request *req, void *data, int rc)
{
	struct osc_brw_async_args *aa = data;
	struct osc_extent *ext;
	struct osc_extent *tmp;
	struct client_obd *cli = aa->aa_cli;

	rc = osc_brw_fini_request(req, rc);
	CDEBUG(D_INODE, "request %p aa %p rc %d\n", req, aa, rc);
	/* When server return -EINPROGRESS, client should always retry
	 * regardless of the number of times the bulk was resent already.
	 */
	if (osc_recoverable_error(rc)) {
		if (req->rq_import_generation !=
		    req->rq_import->imp_generation) {
			CDEBUG(D_HA, "%s: resend cross eviction for object: " DOSTID ", rc = %d.\n",
			       req->rq_import->imp_obd->obd_name,
			       POSTID(&aa->aa_oa->o_oi), rc);
		} else if (rc == -EINPROGRESS ||
		    client_should_resend(aa->aa_resends, aa->aa_cli)) {
			rc = osc_brw_redo_request(req, aa, rc);
		} else {
			CERROR("%s: too many resent retries for object: %llu:%llu, rc = %d.\n",
			       req->rq_import->imp_obd->obd_name,
			       POSTID(&aa->aa_oa->o_oi), rc);
		}

		if (rc == 0)
			return 0;
		else if (rc == -EAGAIN || rc == -EINPROGRESS)
			rc = -EIO;
	}

	if (rc == 0) {
		struct obdo *oa = aa->aa_oa;
		struct cl_attr *attr  = &osc_env_info(env)->oti_attr;
		unsigned long valid = 0;
		struct cl_object *obj;
		struct osc_async_page *last;

		last = brw_page2oap(aa->aa_ppga[aa->aa_page_count - 1]);
		obj = osc2cl(last->oap_obj);

		cl_object_attr_lock(obj);
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

		if (lustre_msg_get_opc(req->rq_reqmsg) == OST_WRITE) {
			struct lov_oinfo *loi = cl2osc(obj)->oo_oinfo;
			loff_t last_off = last->oap_count + last->oap_obj_off +
					  last->oap_page_off;

			/* Change file size if this is an out of quota or
			 * direct IO write and it extends the file size
			 */
			if (loi->loi_lvb.lvb_size < last_off) {
				attr->cat_size = last_off;
				valid |= CAT_SIZE;
			}
			/* Extend KMS if it's not a lockless write */
			if (loi->loi_kms < last_off &&
			    oap2osc_page(last)->ops_srvlock == 0) {
				attr->cat_kms = last_off;
				valid |= CAT_KMS;
			}
		}

		if (valid != 0)
			cl_object_attr_update(env, obj, attr, valid);
		cl_object_attr_unlock(obj);
	}
	kmem_cache_free(obdo_cachep, aa->aa_oa);

	if (lustre_msg_get_opc(req->rq_reqmsg) == OST_WRITE && rc == 0)
		osc_inc_unstable_pages(req);

	list_for_each_entry_safe(ext, tmp, &aa->aa_exts, oe_link) {
		list_del_init(&ext->oe_link);
		osc_extent_finish(env, ext, 1, rc);
	}
	LASSERT(list_empty(&aa->aa_exts));
	LASSERT(list_empty(&aa->aa_oaps));

	osc_release_ppga(aa->aa_ppga, aa->aa_page_count);
	ptlrpc_lprocfs_brw(req, req->rq_bulk->bd_nob_transferred);

	spin_lock(&cli->cl_loi_list_lock);
	/* We need to decrement before osc_ap_completion->osc_wake_cache_waiters
	 * is called so we know whether to go to sync BRWs or wait for more
	 * RPCs to complete
	 */
	if (lustre_msg_get_opc(req->rq_reqmsg) == OST_WRITE)
		cli->cl_w_in_flight--;
	else
		cli->cl_r_in_flight--;
	osc_wake_cache_waiters(cli);
	spin_unlock(&cli->cl_loi_list_lock);

	osc_io_unplug(env, cli, NULL);
	return rc;
}

static void brw_commit(struct ptlrpc_request *req)
{
	/*
	 * If osc_inc_unstable_pages (via osc_extent_finish) races with
	 * this called via the rq_commit_cb, I need to ensure
	 * osc_dec_unstable_pages is still called. Otherwise unstable
	 * pages may be leaked.
	 */
	spin_lock(&req->rq_lock);
	if (unlikely(req->rq_unstable)) {
		req->rq_unstable = 0;
		spin_unlock(&req->rq_lock);
		osc_dec_unstable_pages(req);
	} else {
		req->rq_committed = 1;
		spin_unlock(&req->rq_lock);
	}
}

/**
 * Build an RPC by the list of extent @ext_list. The caller must ensure
 * that the total pages in this list are NOT over max pages per RPC.
 * Extents in the list must be in OES_RPC state.
 */
int osc_build_rpc(const struct lu_env *env, struct client_obd *cli,
		  struct list_head *ext_list, int cmd)
{
	struct ptlrpc_request *req = NULL;
	struct osc_extent *ext;
	struct brw_page **pga = NULL;
	struct osc_brw_async_args *aa = NULL;
	struct obdo *oa = NULL;
	struct osc_async_page *oap;
	struct osc_object *obj = NULL;
	struct cl_req_attr *crattr = NULL;
	u64 starting_offset = OBD_OBJECT_EOF;
	u64 ending_offset = 0;
	int mpflag = 0;
	int mem_tight = 0;
	int page_count = 0;
	bool soft_sync = false;
	bool interrupted = false;
	int i;
	int rc;
	struct ost_body *body;
	LIST_HEAD(rpc_list);

	LASSERT(!list_empty(ext_list));

	/* add pages into rpc_list to build BRW rpc */
	list_for_each_entry(ext, ext_list, oe_link) {
		LASSERT(ext->oe_state == OES_RPC);
		mem_tight |= ext->oe_memalloc;
		page_count += ext->oe_nr_pages;
		if (!obj)
			obj = ext->oe_obj;
	}

	soft_sync = osc_over_unstable_soft_limit(cli);
	if (mem_tight)
		mpflag = cfs_memory_pressure_get_and_set();

	pga = kcalloc(page_count, sizeof(*pga), GFP_NOFS);
	if (!pga) {
		rc = -ENOMEM;
		goto out;
	}

	oa = kmem_cache_zalloc(obdo_cachep, GFP_NOFS);
	if (!oa) {
		rc = -ENOMEM;
		goto out;
	}

	i = 0;
	list_for_each_entry(ext, ext_list, oe_link) {
		list_for_each_entry(oap, &ext->oe_pages, oap_pending_item) {
			if (mem_tight)
				oap->oap_brw_flags |= OBD_BRW_MEMALLOC;
			if (soft_sync)
				oap->oap_brw_flags |= OBD_BRW_SOFT_SYNC;
			pga[i] = &oap->oap_brw_page;
			pga[i]->off = oap->oap_obj_off + oap->oap_page_off;
			i++;

			list_add_tail(&oap->oap_rpc_item, &rpc_list);
			if (starting_offset == OBD_OBJECT_EOF ||
			    starting_offset > oap->oap_obj_off)
				starting_offset = oap->oap_obj_off;
			else
				LASSERT(!oap->oap_page_off);
			if (ending_offset < oap->oap_obj_off + oap->oap_count)
				ending_offset = oap->oap_obj_off +
						oap->oap_count;
			else
				LASSERT(oap->oap_page_off + oap->oap_count ==
					PAGE_SIZE);
			if (oap->oap_interrupted)
				interrupted = true;
		}
	}

	/* first page in the list */
	oap = list_entry(rpc_list.next, typeof(*oap), oap_rpc_item);

	crattr = &osc_env_info(env)->oti_req_attr;
	memset(crattr, 0, sizeof(*crattr));
	crattr->cra_type = (cmd & OBD_BRW_WRITE) ? CRT_WRITE : CRT_READ;
	crattr->cra_flags = ~0ULL;
	crattr->cra_page = oap2cl_page(oap);
	crattr->cra_oa = oa;
	cl_req_attr_set(env, osc2cl(obj), crattr);

	sort_brw_pages(pga, page_count);
	rc = osc_brw_prep_request(cmd, cli, oa, page_count, pga, &req, 1, 0);
	if (rc != 0) {
		CERROR("prep_req failed: %d\n", rc);
		goto out;
	}

	req->rq_commit_cb = brw_commit;
	req->rq_interpret_reply = brw_interpret;

	req->rq_memalloc = mem_tight != 0;
	oap->oap_request = ptlrpc_request_addref(req);
	if (interrupted && !req->rq_intr)
		ptlrpc_mark_interrupted(req);

	/* Need to update the timestamps after the request is built in case
	 * we race with setattr (locally or in queue at OST).  If OST gets
	 * later setattr before earlier BRW (as determined by the request xid),
	 * the OST will not use BRW timestamps.  Sadly, there is no obvious
	 * way to do this in a single call.  bug 10150
	 */
	body = req_capsule_client_get(&req->rq_pill, &RMF_OST_BODY);
	crattr->cra_oa = &body->oa;
	crattr->cra_flags = OBD_MD_FLMTIME | OBD_MD_FLCTIME | OBD_MD_FLATIME;
	cl_req_attr_set(env, osc2cl(obj), crattr);
	lustre_msg_set_jobid(req->rq_reqmsg, crattr->cra_jobid);

	BUILD_BUG_ON(sizeof(*aa) > sizeof(req->rq_async_args));
	aa = ptlrpc_req_async_args(req);
	INIT_LIST_HEAD(&aa->aa_oaps);
	list_splice_init(&rpc_list, &aa->aa_oaps);
	INIT_LIST_HEAD(&aa->aa_exts);
	list_splice_init(ext_list, &aa->aa_exts);

	spin_lock(&cli->cl_loi_list_lock);
	starting_offset >>= PAGE_SHIFT;
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
	spin_unlock(&cli->cl_loi_list_lock);

	DEBUG_REQ(D_INODE, req, "%d pages, aa %p. now %ur/%dw in flight",
		  page_count, aa, cli->cl_r_in_flight,
		  cli->cl_w_in_flight);
	OBD_FAIL_TIMEOUT(OBD_FAIL_OSC_DELAY_IO, cfs_fail_val);

	ptlrpcd_add_req(req);
	rc = 0;

out:
	if (mem_tight != 0)
		cfs_memory_pressure_restore(mpflag);

	if (rc != 0) {
		LASSERT(!req);

		if (oa)
			kmem_cache_free(obdo_cachep, oa);
		kfree(pga);
		/* this should happen rarely and is pretty bad, it makes the
		 * pending list not follow the dirty order
		 */
		while (!list_empty(ext_list)) {
			ext = list_entry(ext_list->next, struct osc_extent,
					 oe_link);
			list_del_init(&ext->oe_link);
			osc_extent_finish(env, ext, 0, rc);
		}
	}
	return rc;
}

static int osc_set_lock_data(struct ldlm_lock *lock, void *data)
{
	int set = 0;

	LASSERT(lock);

	lock_res_and_lock(lock);

	if (!lock->l_ast_data)
		lock->l_ast_data = data;
	if (lock->l_ast_data == data)
		set = 1;

	unlock_res_and_lock(lock);

	return set;
}

static int osc_enqueue_fini(struct ptlrpc_request *req,
			    osc_enqueue_upcall_f upcall, void *cookie,
			    struct lustre_handle *lockh, enum ldlm_mode mode,
			    __u64 *flags, int agl, int errcode)
{
	bool intent = *flags & LDLM_FL_HAS_INTENT;
	int rc;

	/* The request was created before ldlm_cli_enqueue call. */
	if (intent && errcode == ELDLM_LOCK_ABORTED) {
		struct ldlm_reply *rep;

		rep = req_capsule_server_get(&req->rq_pill, &RMF_DLM_REP);

		rep->lock_policy_res1 =
			ptlrpc_status_ntoh(rep->lock_policy_res1);
		if (rep->lock_policy_res1)
			errcode = rep->lock_policy_res1;
		if (!agl)
			*flags |= LDLM_FL_LVB_READY;
	} else if (errcode == ELDLM_OK) {
		*flags |= LDLM_FL_LVB_READY;
	}

	/* Call the update callback. */
	rc = (*upcall)(cookie, lockh, errcode);
	/* release the reference taken in ldlm_cli_enqueue() */
	if (errcode == ELDLM_LOCK_MATCHED)
		errcode = ELDLM_OK;
	if (errcode == ELDLM_OK && lustre_handle_is_used(lockh))
		ldlm_lock_decref(lockh, mode);

	return rc;
}

static int osc_enqueue_interpret(const struct lu_env *env,
				 struct ptlrpc_request *req,
				 struct osc_enqueue_args *aa, int rc)
{
	struct ldlm_lock *lock;
	struct lustre_handle *lockh = &aa->oa_lockh;
	enum ldlm_mode mode = aa->oa_mode;
	struct ost_lvb *lvb = aa->oa_lvb;
	__u32 lvb_len = sizeof(*lvb);
	__u64 flags = 0;


	/* ldlm_cli_enqueue is holding a reference on the lock, so it must
	 * be valid.
	 */
	lock = ldlm_handle2lock(lockh);
	LASSERTF(lock, "lockh %llx, req %p, aa %p - client evicted?\n",
		 lockh->cookie, req, aa);

	/* Take an additional reference so that a blocking AST that
	 * ldlm_cli_enqueue_fini() might post for a failed lock, is guaranteed
	 * to arrive after an upcall has been executed by
	 * osc_enqueue_fini().
	 */
	ldlm_lock_addref(lockh, mode);

	/* Let cl_lock_state_wait fail with -ERESTARTSYS to unuse sublocks. */
	OBD_FAIL_TIMEOUT(OBD_FAIL_LDLM_ENQUEUE_HANG, 2);

	/* Let CP AST to grant the lock first. */
	OBD_FAIL_TIMEOUT(OBD_FAIL_OSC_CP_ENQ_RACE, 1);

	if (aa->oa_agl) {
		LASSERT(!aa->oa_lvb);
		LASSERT(!aa->oa_flags);
		aa->oa_flags = &flags;
	}

	/* Complete obtaining the lock procedure. */
	rc = ldlm_cli_enqueue_fini(aa->oa_exp, req, aa->oa_type, 1,
				   aa->oa_mode, aa->oa_flags, lvb, lvb_len,
				   lockh, rc);
	/* Complete osc stuff. */
	rc = osc_enqueue_fini(req, aa->oa_upcall, aa->oa_cookie, lockh, mode,
			      aa->oa_flags, aa->oa_agl, rc);

	OBD_FAIL_TIMEOUT(OBD_FAIL_OSC_CP_CANCEL_RACE, 10);

	ldlm_lock_decref(lockh, mode);
	LDLM_LOCK_PUT(lock);
	return rc;
}

struct ptlrpc_request_set *PTLRPCD_SET = (void *)1;

/* When enqueuing asynchronously, locks are not ordered, we can obtain a lock
 * from the 2nd OSC before a lock from the 1st one. This does not deadlock with
 * other synchronous requests, however keeping some locks and trying to obtain
 * others may take a considerable amount of time in a case of ost failure; and
 * when other sync requests do not get released lock from a client, the client
 * is evicted from the cluster -- such scenaries make the life difficult, so
 * release locks just after they are obtained.
 */
int osc_enqueue_base(struct obd_export *exp, struct ldlm_res_id *res_id,
		     __u64 *flags, union ldlm_policy_data *policy,
		     struct ost_lvb *lvb, int kms_valid,
		     osc_enqueue_upcall_f upcall, void *cookie,
		     struct ldlm_enqueue_info *einfo,
		     struct ptlrpc_request_set *rqset, int async, int agl)
{
	struct obd_device *obd = exp->exp_obd;
	struct lustre_handle lockh = { 0 };
	struct ptlrpc_request *req = NULL;
	int intent = *flags & LDLM_FL_HAS_INTENT;
	__u64 match_flags = *flags;
	enum ldlm_mode mode;
	int rc;

	/* Filesystem lock extents are extended to page boundaries so that
	 * dealing with the page cache is a little smoother.
	 */
	policy->l_extent.start -= policy->l_extent.start & ~PAGE_MASK;
	policy->l_extent.end |= ~PAGE_MASK;

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
	 * locks out from other users right now, too.
	 */
	mode = einfo->ei_mode;
	if (einfo->ei_mode == LCK_PR)
		mode |= LCK_PW;
	if (agl == 0)
		match_flags |= LDLM_FL_LVB_READY;
	if (intent != 0)
		match_flags |= LDLM_FL_BLOCK_GRANTED;
	mode = ldlm_lock_match(obd->obd_namespace, match_flags, res_id,
			       einfo->ei_type, policy, mode, &lockh, 0);
	if (mode) {
		struct ldlm_lock *matched;

		if (*flags & LDLM_FL_TEST_LOCK)
			return ELDLM_OK;

		matched = ldlm_handle2lock(&lockh);
		if (agl) {
			/* AGL enqueues DLM locks speculatively. Therefore if
			 * it already exists a DLM lock, it wll just inform the
			 * caller to cancel the AGL process for this stripe.
			 */
			ldlm_lock_decref(&lockh, mode);
			LDLM_LOCK_PUT(matched);
			return -ECANCELED;
		} else if (osc_set_lock_data(matched, einfo->ei_cbdata)) {
			*flags |= LDLM_FL_LVB_READY;
			/* We already have a lock, and it's referenced. */
			(*upcall)(cookie, &lockh, ELDLM_LOCK_MATCHED);

			ldlm_lock_decref(&lockh, mode);
			LDLM_LOCK_PUT(matched);
			return ELDLM_OK;
		} else {
			ldlm_lock_decref(&lockh, mode);
			LDLM_LOCK_PUT(matched);
		}
	}

no_match:
	if (*flags & (LDLM_FL_TEST_LOCK | LDLM_FL_MATCH_LOCK))
		return -ENOLCK;
	if (intent) {
		req = ptlrpc_request_alloc(class_exp2cliimp(exp),
					   &RQF_LDLM_ENQUEUE_LVB);
		if (!req)
			return -ENOMEM;

		rc = ldlm_prep_enqueue_req(exp, req, NULL, 0);
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
			      sizeof(*lvb), LVB_T_OST, &lockh, async);
	if (async) {
		if (!rc) {
			struct osc_enqueue_args *aa;

			BUILD_BUG_ON(sizeof(*aa) > sizeof(req->rq_async_args));
			aa = ptlrpc_req_async_args(req);
			aa->oa_exp = exp;
			aa->oa_mode = einfo->ei_mode;
			aa->oa_type = einfo->ei_type;
			lustre_handle_copy(&aa->oa_lockh, &lockh);
			aa->oa_upcall = upcall;
			aa->oa_cookie = cookie;
			aa->oa_agl    = !!agl;
			if (!agl) {
				aa->oa_flags = flags;
				aa->oa_lvb = lvb;
			} else {
				/* AGL is essentially to enqueue an DLM lock
				* in advance, so we don't care about the
				* result of AGL enqueue.
				*/
				aa->oa_lvb = NULL;
				aa->oa_flags = NULL;
			}

			req->rq_interpret_reply =
				(ptlrpc_interpterer_t)osc_enqueue_interpret;
			if (rqset == PTLRPCD_SET)
				ptlrpcd_add_req(req);
			else
				ptlrpc_set_add_req(rqset, req);
		} else if (intent) {
			ptlrpc_req_finished(req);
		}
		return rc;
	}

	rc = osc_enqueue_fini(req, upcall, cookie, &lockh, einfo->ei_mode,
			      flags, agl, rc);
	if (intent)
		ptlrpc_req_finished(req);

	return rc;
}

int osc_match_base(struct obd_export *exp, struct ldlm_res_id *res_id,
		   enum ldlm_type type, union ldlm_policy_data *policy,
		   enum ldlm_mode mode, __u64 *flags, void *data,
		   struct lustre_handle *lockh, int unref)
{
	struct obd_device *obd = exp->exp_obd;
	__u64 lflags = *flags;
	enum ldlm_mode rc;

	if (OBD_FAIL_CHECK(OBD_FAIL_OSC_MATCH))
		return -EIO;

	/* Filesystem lock extents are extended to page boundaries so that
	 * dealing with the page cache is a little smoother
	 */
	policy->l_extent.start -= policy->l_extent.start & ~PAGE_MASK;
	policy->l_extent.end |= ~PAGE_MASK;

	/* Next, search for already existing extent locks that will cover us */
	/* If we're trying to read, we also search for an existing PW lock.  The
	 * VFS and page cache already protect us locally, so lots of readers/
	 * writers can share a single PW lock.
	 */
	rc = mode;
	if (mode == LCK_PR)
		rc |= LCK_PW;
	rc = ldlm_lock_match(obd->obd_namespace, lflags,
			     res_id, type, policy, rc, lockh, unref);
	if (!rc || lflags & LDLM_FL_TEST_LOCK)
		return rc;

	if (data) {
		struct ldlm_lock *lock = ldlm_handle2lock(lockh);

		LASSERT(lock);
		if (!osc_set_lock_data(lock, data)) {
			ldlm_lock_decref(lockh, rc);
			rc = 0;
		}
		LDLM_LOCK_PUT(lock);
	}
	return rc;
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
		 * of the clean up
		 */
		return rc;

	if ((rc == -ENOTCONN || rc == -EAGAIN) &&
	    (aa->aa_oi->oi_flags & OBD_STATFS_NODELAY)) {
		rc = 0;
		goto out;
	}

	if (rc != 0)
		goto out;

	msfs = req_capsule_server_get(&req->rq_pill, &RMF_OBD_STATFS);
	if (!msfs) {
		rc = -EPROTO;
		goto out;
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
	struct obd_device *obd = class_exp2obd(exp);
	struct ptlrpc_request *req;
	struct osc_async_args *aa;
	int rc;

	/* We could possibly pass max_age in the request (as an absolute
	 * timestamp or a "seconds.usec ago") so the target can avoid doing
	 * extra calls into the filesystem if that isn't necessary (e.g.
	 * during mount that would help a bit).  Having relative timestamps
	 * is not so great if request processing is slow, while absolute
	 * timestamps are not ideal because they need time synchronization.
	 */
	req = ptlrpc_request_alloc(obd->u.cli.cl_import, &RQF_OST_STATFS);
	if (!req)
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
	BUILD_BUG_ON(sizeof(*aa) > sizeof(req->rq_async_args));
	aa = ptlrpc_req_async_args(req);
	aa->aa_oi = oinfo;

	ptlrpc_set_add_req(rqset, req);
	return 0;
}

static int osc_statfs(const struct lu_env *env, struct obd_export *exp,
		      struct obd_statfs *osfs, __u64 max_age, __u32 flags)
{
	struct obd_device *obd = class_exp2obd(exp);
	struct obd_statfs *msfs;
	struct ptlrpc_request *req;
	struct obd_import *imp = NULL;
	int rc;

	/* Since the request might also come from lprocfs, so we need
	 * sync this with client_disconnect_export Bug15684
	 */
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
	 * timestamps are not ideal because they need time synchronization.
	 */
	req = ptlrpc_request_alloc(imp, &RQF_OST_STATFS);

	class_import_put(imp);

	if (!req)
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
		goto out;

	msfs = req_capsule_server_get(&req->rq_pill, &RMF_OBD_STATFS);
	if (!msfs) {
		rc = -EPROTO;
		goto out;
	}

	*osfs = *msfs;

 out:
	ptlrpc_req_finished(req);
	return rc;
}

static int osc_iocontrol(unsigned int cmd, struct obd_export *exp, int len,
			 void *karg, void __user *uarg)
{
	struct obd_device *obd = exp->exp_obd;
	struct obd_ioctl_data *data = karg;
	int err = 0;

	if (!try_module_get(THIS_MODULE)) {
		CERROR("%s: cannot get module '%s'\n", obd->obd_name,
		       module_name(THIS_MODULE));
		return -EINVAL;
	}
	switch (cmd) {
	case OBD_IOC_CLIENT_RECOVER:
		err = ptlrpc_recover_import(obd->u.cli.cl_import,
					    data->ioc_inlbuf1, 0);
		if (err > 0)
			err = 0;
		goto out;
	case IOC_OSC_SET_ACTIVE:
		err = ptlrpc_set_import_active(obd->u.cli.cl_import,
					       data->ioc_offset);
		goto out;
	case OBD_IOC_PING_TARGET:
		err = ptlrpc_obd_ping(obd);
		goto out;
	default:
		CDEBUG(D_INODE, "unrecognised ioctl %#x by %s\n",
		       cmd, current_comm());
		err = -ENOTTY;
		goto out;
	}
out:
	module_put(THIS_MODULE);
	return err;
}

static int osc_set_info_async(const struct lu_env *env, struct obd_export *exp,
			      u32 keylen, void *key, u32 vallen,
			      void *val, struct ptlrpc_request_set *set)
{
	struct ptlrpc_request *req;
	struct obd_device *obd = exp->exp_obd;
	struct obd_import *imp = class_exp2cliimp(exp);
	char *tmp;
	int rc;

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

		LASSERT(!cli->cl_cache); /* only once */
		cli->cl_cache = val;
		cl_cache_incref(cli->cl_cache);
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
		long nr = atomic_long_read(&cli->cl_lru_in_list) >> 1;
		long target = *(long *)val;

		nr = osc_lru_shrink(env, cli, min(nr, target), true);
		*(long *)val -= nr;
		return 0;
	}

	if (!set && !KEY_IS(KEY_GRANT_SHRINK))
		return -EINVAL;

	/* We pass all other commands directly to OST. Since nobody calls osc
	 * methods directly and everybody is supposed to go through LOV, we
	 * assume lov checked invalid values for us.
	 * The only recognised values so far are evict_by_nid and mds_conn.
	 * Even if something bad goes through, we'd get a -EINVAL from OST
	 * anyway.
	 */

	req = ptlrpc_request_alloc(imp, KEY_IS(KEY_GRANT_SHRINK) ?
						&RQF_OST_SET_GRANT_INFO :
						&RQF_OBD_SET_INFO);
	if (!req)
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
		struct osc_brw_async_args *aa;
		struct obdo *oa;

		BUILD_BUG_ON(sizeof(*aa) > sizeof(req->rq_async_args));
		aa = ptlrpc_req_async_args(req);
		oa = kmem_cache_zalloc(obdo_cachep, GFP_NOFS);
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
		LASSERT(set);
		ptlrpc_set_add_req(set, req);
		ptlrpc_check_set(NULL, set);
	} else {
		ptlrpcd_add_req(req);
	}

	return 0;
}

static int osc_reconnect(const struct lu_env *env,
			 struct obd_export *exp, struct obd_device *obd,
			 struct obd_uuid *cluuid,
			 struct obd_connect_data *data,
			 void *localdata)
{
	struct client_obd *cli = &obd->u.cli;

	if (data && (data->ocd_connect_flags & OBD_CONNECT_GRANT)) {
		long lost_grant;

		spin_lock(&cli->cl_loi_list_lock);
		data->ocd_grant = (cli->cl_avail_grant +
				   (cli->cl_dirty_pages << PAGE_SHIFT)) ?:
				   2 * cli_brw_size(obd);
		lost_grant = cli->cl_lost_grant;
		cli->cl_lost_grant = 0;
		spin_unlock(&cli->cl_loi_list_lock);

		CDEBUG(D_RPCTRACE, "ocd_connect_flags: %#llx ocd_version: %d ocd_grant: %d, lost: %ld.\n",
		       data->ocd_connect_flags,
		       data->ocd_version, data->ocd_grant, lost_grant);
	}

	return 0;
}

static int osc_disconnect(struct obd_export *exp)
{
	struct obd_device *obd = class_exp2obd(exp);
	int rc;

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
	if (!obd->u.cli.cl_import)
		osc_del_shrink_grant(&obd->u.cli);
	return rc;
}

static int osc_ldlm_resource_invalidate(struct cfs_hash *hs,
					struct cfs_hash_bd *bd,
					struct hlist_node *hnode, void *arg)
{
	struct ldlm_resource *res = cfs_hash_object(hs, hnode);
	struct osc_object *osc = NULL;
	struct lu_env *env = arg;
	struct ldlm_lock *lock;

	lock_res(res);
	list_for_each_entry(lock, &res->lr_granted, l_res_link) {
		if (lock->l_ast_data && !osc) {
			osc = lock->l_ast_data;
			cl_object_get(osc2cl(osc));
		}

		/*
		 * clear LDLM_FL_CLEANED flag to make sure it will be canceled
		 * by the 2nd round of ldlm_namespace_clean() call in
		 * osc_import_event().
		 */
		ldlm_clear_cleaned(lock);
	}
	unlock_res(res);

	if (osc) {
		osc_object_invalidate(env, osc);
		cl_object_put(env, osc2cl(osc));
	}

	return 0;
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
		spin_lock(&cli->cl_loi_list_lock);
		cli->cl_avail_grant = 0;
		cli->cl_lost_grant = 0;
		spin_unlock(&cli->cl_loi_list_lock);
		break;
	}
	case IMP_EVENT_INACTIVE: {
		rc = obd_notify_observer(obd, obd, OBD_NOTIFY_INACTIVE, NULL);
		break;
	}
	case IMP_EVENT_INVALIDATE: {
		struct ldlm_namespace *ns = obd->obd_namespace;
		struct lu_env *env;
		u16 refcheck;

		ldlm_namespace_cleanup(ns, LDLM_FL_LOCAL_ONLY);

		env = cl_env_get(&refcheck);
		if (!IS_ERR(env)) {
			osc_io_unplug(env, &obd->u.cli, NULL);

			cfs_hash_for_each_nolock(ns->ns_rs_hash,
						 osc_ldlm_resource_invalidate,
						 env, 0);
			cl_env_put(env, &refcheck);

			ldlm_namespace_cleanup(ns, LDLM_FL_LOCAL_ONLY);
		} else {
			rc = PTR_ERR(env);
		}
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
			imp->imp_client->cli_request_portal = OST_REQUEST_PORTAL;

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
static int osc_cancel_weight(struct ldlm_lock *lock)
{
	/*
	 * Cancel all unused and granted extent lock.
	 */
	if (lock->l_resource->lr_type == LDLM_EXTENT &&
	    lock->l_granted_mode == lock->l_req_mode &&
	    osc_ldlm_weigh_ast(lock) == 0)
		return 1;

	return 0;
}

static int brw_queue_work(const struct lu_env *env, void *data)
{
	struct client_obd *cli = data;

	CDEBUG(D_CACHE, "Run writeback work for client obd %p.\n", cli);

	osc_io_unplug(env, cli, NULL);
	return 0;
}

int osc_setup(struct obd_device *obd, struct lustre_cfg *lcfg)
{
	struct lprocfs_static_vars lvars = { NULL };
	struct client_obd *cli = &obd->u.cli;
	void *handler;
	int rc;
	int adding;
	int added;
	int req_count;

	rc = ptlrpcd_addref();
	if (rc)
		return rc;

	rc = client_obd_setup(obd, lcfg);
	if (rc)
		goto out_ptlrpcd;

	handler = ptlrpcd_alloc_work(cli->cl_import, brw_queue_work, cli);
	if (IS_ERR(handler)) {
		rc = PTR_ERR(handler);
		goto out_client_setup;
	}
	cli->cl_writeback_work = handler;

	handler = ptlrpcd_alloc_work(cli->cl_import, lru_queue_work, cli);
	if (IS_ERR(handler)) {
		rc = PTR_ERR(handler);
		goto out_ptlrpcd_work;
	}

	cli->cl_lru_work = handler;

	rc = osc_quota_setup(obd);
	if (rc)
		goto out_ptlrpcd_work;

	cli->cl_grant_shrink_interval = GRANT_SHRINK_INTERVAL;
	lprocfs_osc_init_vars(&lvars);
	if (lprocfs_obd_setup(obd, lvars.obd_vars, lvars.sysfs_vars) == 0) {
		lproc_osc_attach_seqstat(obd);
		sptlrpc_lprocfs_cliobd_attach(obd);
		ptlrpc_lprocfs_register_obd(obd);
	}

	/*
	 * We try to control the total number of requests with a upper limit
	 * osc_reqpool_maxreqcount. There might be some race which will cause
	 * over-limit allocation, but it is fine.
	 */
	req_count = atomic_read(&osc_pool_req_count);
	if (req_count < osc_reqpool_maxreqcount) {
		adding = cli->cl_max_rpcs_in_flight + 2;
		if (req_count + adding > osc_reqpool_maxreqcount)
			adding = osc_reqpool_maxreqcount - req_count;

		added = ptlrpc_add_rqs_to_pool(osc_rq_pool, adding);
		atomic_add(added, &osc_pool_req_count);
	}

	INIT_LIST_HEAD(&cli->cl_grant_shrink_list);
	ns_register_cancel(obd->obd_namespace, osc_cancel_weight);

	spin_lock(&osc_shrink_lock);
	list_add_tail(&cli->cl_shrink_list, &osc_shrink_list);
	spin_unlock(&osc_shrink_lock);

	return rc;

out_ptlrpcd_work:
	if (cli->cl_writeback_work) {
		ptlrpcd_destroy_work(cli->cl_writeback_work);
		cli->cl_writeback_work = NULL;
	}
	if (cli->cl_lru_work) {
		ptlrpcd_destroy_work(cli->cl_lru_work);
		cli->cl_lru_work = NULL;
	}
out_client_setup:
	client_obd_cleanup(obd);
out_ptlrpcd:
	ptlrpcd_decref();
	return rc;
}

static int osc_precleanup(struct obd_device *obd)
{
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

	if (cli->cl_lru_work) {
		ptlrpcd_destroy_work(cli->cl_lru_work);
		cli->cl_lru_work = NULL;
	}

	obd_cleanup_client_import(obd);
	ptlrpc_lprocfs_unregister_obd(obd);
	lprocfs_obd_cleanup(obd);
	return 0;
}

static int osc_cleanup(struct obd_device *obd)
{
	struct client_obd *cli = &obd->u.cli;
	int rc;

	spin_lock(&osc_shrink_lock);
	list_del(&cli->cl_shrink_list);
	spin_unlock(&osc_shrink_lock);

	/* lru cleanup */
	if (cli->cl_cache) {
		LASSERT(atomic_read(&cli->cl_cache->ccc_users) > 0);
		spin_lock(&cli->cl_cache->ccc_lru_lock);
		list_del_init(&cli->cl_lru_osc);
		spin_unlock(&cli->cl_cache->ccc_lru_lock);
		cli->cl_lru_left = NULL;
		cl_cache_decref(cli->cl_cache);
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
	struct lprocfs_static_vars lvars = { NULL };
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

	return rc;
}

static int osc_process_config(struct obd_device *obd, u32 len, void *buf)
{
	return osc_process_config_base(obd, buf);
}

static struct obd_ops osc_obd_ops = {
	.owner          = THIS_MODULE,
	.setup          = osc_setup,
	.precleanup     = osc_precleanup,
	.cleanup        = osc_cleanup,
	.add_conn       = client_import_add_conn,
	.del_conn       = client_import_del_conn,
	.connect        = client_connect_import,
	.reconnect      = osc_reconnect,
	.disconnect     = osc_disconnect,
	.statfs         = osc_statfs,
	.statfs_async   = osc_statfs_async,
	.create         = osc_create,
	.destroy        = osc_destroy,
	.getattr        = osc_getattr,
	.setattr        = osc_setattr,
	.iocontrol      = osc_iocontrol,
	.set_info_async = osc_set_info_async,
	.import_event   = osc_import_event,
	.process_config = osc_process_config,
	.quotactl       = osc_quotactl,
};

struct list_head osc_shrink_list = LIST_HEAD_INIT(osc_shrink_list);
DEFINE_SPINLOCK(osc_shrink_lock);

static struct shrinker osc_cache_shrinker = {
	.count_objects	= osc_cache_shrink_count,
	.scan_objects	= osc_cache_shrink_scan,
	.seeks		= DEFAULT_SEEKS,
};

static int __init osc_init(void)
{
	struct lprocfs_static_vars lvars = { NULL };
	unsigned int reqpool_size;
	unsigned int reqsize;
	int rc;

	/* print an address of _any_ initialized kernel symbol from this
	 * module, to allow debugging with gdb that doesn't support data
	 * symbols from modules.
	 */
	CDEBUG(D_INFO, "Lustre OSC module (%p).\n", &osc_caches);

	rc = lu_kmem_init(osc_caches);
	if (rc)
		return rc;

	lprocfs_osc_init_vars(&lvars);

	rc = class_register_type(&osc_obd_ops, NULL,
				 LUSTRE_OSC_NAME, &osc_device_type);
	if (rc)
		goto out_kmem;

	register_shrinker(&osc_cache_shrinker);

	/* This is obviously too much memory, only prevent overflow here */
	if (osc_reqpool_mem_max >= 1 << 12 || osc_reqpool_mem_max == 0) {
		rc = -EINVAL;
		goto out_type;
	}

	reqpool_size = osc_reqpool_mem_max << 20;

	reqsize = 1;
	while (reqsize < OST_MAXREQSIZE)
		reqsize = reqsize << 1;

	/*
	 * We don't enlarge the request count in OSC pool according to
	 * cl_max_rpcs_in_flight. The allocation from the pool will only be
	 * tried after normal allocation failed. So a small OSC pool won't
	 * cause much performance degression in most of cases.
	 */
	osc_reqpool_maxreqcount = reqpool_size / reqsize;

	atomic_set(&osc_pool_req_count, 0);
	osc_rq_pool = ptlrpc_init_rq_pool(0, OST_MAXREQSIZE,
					  ptlrpc_add_rqs_to_pool);

	if (osc_rq_pool)
		return 0;

	rc = -ENOMEM;

out_type:
	class_unregister_type(LUSTRE_OSC_NAME);
out_kmem:
	lu_kmem_fini(osc_caches);
	return rc;
}

static void /*__exit*/ osc_exit(void)
{
	unregister_shrinker(&osc_cache_shrinker);
	class_unregister_type(LUSTRE_OSC_NAME);
	lu_kmem_fini(osc_caches);
	ptlrpc_free_rq_pool(osc_rq_pool);
}

MODULE_AUTHOR("OpenSFS, Inc. <http://www.lustre.org/>");
MODULE_DESCRIPTION("Lustre Object Storage Client (OSC)");
MODULE_LICENSE("GPL");
MODULE_VERSION(LUSTRE_VERSION_STRING);

module_init(osc_init);
module_exit(osc_exit);
