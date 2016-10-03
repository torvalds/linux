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
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2015, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * Implementation of cl_object for OSC layer.
 *
 *   Author: Nikita Danilov <nikita.danilov@sun.com>
 *   Author: Jinshan Xiong <jinshan.xiong@intel.com>
 */

#define DEBUG_SUBSYSTEM S_OSC

#include "osc_cl_internal.h"

/** \addtogroup osc
 *  @{
 */

/*****************************************************************************
 *
 * Type conversions.
 *
 */

static struct lu_object *osc2lu(struct osc_object *osc)
{
	return &osc->oo_cl.co_lu;
}

static struct osc_object *lu2osc(const struct lu_object *obj)
{
	LINVRNT(osc_is_object(obj));
	return container_of0(obj, struct osc_object, oo_cl.co_lu);
}

/*****************************************************************************
 *
 * Object operations.
 *
 */

static int osc_object_init(const struct lu_env *env, struct lu_object *obj,
			   const struct lu_object_conf *conf)
{
	struct osc_object *osc = lu2osc(obj);
	const struct cl_object_conf *cconf = lu2cl_conf(conf);
	int i;

	osc->oo_oinfo = cconf->u.coc_oinfo;
	spin_lock_init(&osc->oo_seatbelt);
	for (i = 0; i < CRT_NR; ++i)
		INIT_LIST_HEAD(&osc->oo_inflight[i]);

	INIT_LIST_HEAD(&osc->oo_ready_item);
	INIT_LIST_HEAD(&osc->oo_hp_ready_item);
	INIT_LIST_HEAD(&osc->oo_write_item);
	INIT_LIST_HEAD(&osc->oo_read_item);

	osc->oo_root.rb_node = NULL;
	INIT_LIST_HEAD(&osc->oo_hp_exts);
	INIT_LIST_HEAD(&osc->oo_urgent_exts);
	INIT_LIST_HEAD(&osc->oo_rpc_exts);
	INIT_LIST_HEAD(&osc->oo_reading_exts);
	atomic_set(&osc->oo_nr_reads, 0);
	atomic_set(&osc->oo_nr_writes, 0);
	spin_lock_init(&osc->oo_lock);
	spin_lock_init(&osc->oo_tree_lock);
	spin_lock_init(&osc->oo_ol_spin);
	INIT_LIST_HEAD(&osc->oo_ol_list);

	cl_object_page_init(lu2cl(obj), sizeof(struct osc_page));

	return 0;
}

static void osc_object_free(const struct lu_env *env, struct lu_object *obj)
{
	struct osc_object *osc = lu2osc(obj);
	int i;

	for (i = 0; i < CRT_NR; ++i)
		LASSERT(list_empty(&osc->oo_inflight[i]));

	LASSERT(list_empty(&osc->oo_ready_item));
	LASSERT(list_empty(&osc->oo_hp_ready_item));
	LASSERT(list_empty(&osc->oo_write_item));
	LASSERT(list_empty(&osc->oo_read_item));

	LASSERT(!osc->oo_root.rb_node);
	LASSERT(list_empty(&osc->oo_hp_exts));
	LASSERT(list_empty(&osc->oo_urgent_exts));
	LASSERT(list_empty(&osc->oo_rpc_exts));
	LASSERT(list_empty(&osc->oo_reading_exts));
	LASSERT(atomic_read(&osc->oo_nr_reads) == 0);
	LASSERT(atomic_read(&osc->oo_nr_writes) == 0);
	LASSERT(list_empty(&osc->oo_ol_list));

	lu_object_fini(obj);
	kmem_cache_free(osc_object_kmem, osc);
}

int osc_lvb_print(const struct lu_env *env, void *cookie,
		  lu_printer_t p, const struct ost_lvb *lvb)
{
	return (*p)(env, cookie, "size: %llu mtime: %llu atime: %llu ctime: %llu blocks: %llu",
		    lvb->lvb_size, lvb->lvb_mtime, lvb->lvb_atime,
		    lvb->lvb_ctime, lvb->lvb_blocks);
}

static int osc_object_print(const struct lu_env *env, void *cookie,
			    lu_printer_t p, const struct lu_object *obj)
{
	struct osc_object *osc = lu2osc(obj);
	struct lov_oinfo *oinfo = osc->oo_oinfo;
	struct osc_async_rc *ar = &oinfo->loi_ar;

	(*p)(env, cookie, "id: " DOSTID " idx: %d gen: %d kms_valid: %u kms %llu rc: %d force_sync: %d min_xid: %llu ",
	     POSTID(&oinfo->loi_oi), oinfo->loi_ost_idx,
	     oinfo->loi_ost_gen, oinfo->loi_kms_valid, oinfo->loi_kms,
	     ar->ar_rc, ar->ar_force_sync, ar->ar_min_xid);
	osc_lvb_print(env, cookie, p, &oinfo->loi_lvb);
	return 0;
}

static int osc_attr_get(const struct lu_env *env, struct cl_object *obj,
			struct cl_attr *attr)
{
	struct lov_oinfo *oinfo = cl2osc(obj)->oo_oinfo;

	cl_lvb2attr(attr, &oinfo->loi_lvb);
	attr->cat_kms = oinfo->loi_kms_valid ? oinfo->loi_kms : 0;
	return 0;
}

static int osc_attr_update(const struct lu_env *env, struct cl_object *obj,
			   const struct cl_attr *attr, unsigned int valid)
{
	struct lov_oinfo *oinfo = cl2osc(obj)->oo_oinfo;
	struct ost_lvb *lvb = &oinfo->loi_lvb;

	if (valid & CAT_SIZE)
		lvb->lvb_size = attr->cat_size;
	if (valid & CAT_MTIME)
		lvb->lvb_mtime = attr->cat_mtime;
	if (valid & CAT_ATIME)
		lvb->lvb_atime = attr->cat_atime;
	if (valid & CAT_CTIME)
		lvb->lvb_ctime = attr->cat_ctime;
	if (valid & CAT_BLOCKS)
		lvb->lvb_blocks = attr->cat_blocks;
	if (valid & CAT_KMS) {
		CDEBUG(D_CACHE, "set kms from %llu to %llu\n",
		       oinfo->loi_kms, (__u64)attr->cat_kms);
		loi_kms_set(oinfo, attr->cat_kms);
	}
	return 0;
}

static int osc_object_glimpse(const struct lu_env *env,
			      const struct cl_object *obj, struct ost_lvb *lvb)
{
	struct lov_oinfo *oinfo = cl2osc(obj)->oo_oinfo;

	lvb->lvb_size = oinfo->loi_kms;
	lvb->lvb_blocks = oinfo->loi_lvb.lvb_blocks;
	return 0;
}

static int osc_object_ast_clear(struct ldlm_lock *lock, void *data)
{
	if (lock->l_ast_data == data)
		lock->l_ast_data = NULL;
	return LDLM_ITER_CONTINUE;
}

static int osc_object_prune(const struct lu_env *env, struct cl_object *obj)
{
	struct osc_object       *osc = cl2osc(obj);
	struct ldlm_res_id      *resname = &osc_env_info(env)->oti_resname;

	LASSERTF(osc->oo_npages == 0,
		 DFID "still have %lu pages, obj: %p, osc: %p\n",
		 PFID(lu_object_fid(&obj->co_lu)), osc->oo_npages, obj, osc);

	/* DLM locks don't hold a reference of osc_object so we have to
	 * clear it before the object is being destroyed.
	 */
	ostid_build_res_name(&osc->oo_oinfo->loi_oi, resname);
	ldlm_resource_iterate(osc_export(osc)->exp_obd->obd_namespace, resname,
			      osc_object_ast_clear, osc);
	return 0;
}

static int osc_object_fiemap(const struct lu_env *env, struct cl_object *obj,
			     struct ll_fiemap_info_key *fmkey,
			     struct fiemap *fiemap, size_t *buflen)
{
	struct obd_export *exp = osc_export(cl2osc(obj));
	ldlm_policy_data_t policy;
	struct ptlrpc_request *req;
	struct lustre_handle lockh;
	struct ldlm_res_id resid;
	enum ldlm_mode mode = 0;
	struct fiemap *reply;
	char *tmp;
	int rc;

	fmkey->lfik_oa.o_oi = cl2osc(obj)->oo_oinfo->loi_oi;
	if (!(fmkey->lfik_fiemap.fm_flags & FIEMAP_FLAG_SYNC))
		goto skip_locking;

	policy.l_extent.start = fmkey->lfik_fiemap.fm_start & PAGE_MASK;

	if (OBD_OBJECT_EOF - fmkey->lfik_fiemap.fm_length <=
	    fmkey->lfik_fiemap.fm_start + PAGE_SIZE - 1)
		policy.l_extent.end = OBD_OBJECT_EOF;
	else
		policy.l_extent.end = (fmkey->lfik_fiemap.fm_start +
				       fmkey->lfik_fiemap.fm_length +
				       PAGE_SIZE - 1) & PAGE_MASK;

	ostid_build_res_name(&fmkey->lfik_oa.o_oi, &resid);
	mode = ldlm_lock_match(exp->exp_obd->obd_namespace,
			       LDLM_FL_BLOCK_GRANTED | LDLM_FL_LVB_READY,
			       &resid, LDLM_EXTENT, &policy,
			       LCK_PR | LCK_PW, &lockh, 0);
	if (mode) { /* lock is cached on client */
		if (mode != LCK_PR) {
			ldlm_lock_addref(&lockh, LCK_PR);
			ldlm_lock_decref(&lockh, LCK_PW);
		}
	} else { /* no cached lock, needs acquire lock on server side */
		fmkey->lfik_oa.o_valid |= OBD_MD_FLFLAGS;
		fmkey->lfik_oa.o_flags |= OBD_FL_SRVLOCK;
	}

skip_locking:
	req = ptlrpc_request_alloc(class_exp2cliimp(exp),
				   &RQF_OST_GET_INFO_FIEMAP);
	if (!req) {
		rc = -ENOMEM;
		goto drop_lock;
	}

	req_capsule_set_size(&req->rq_pill, &RMF_FIEMAP_KEY, RCL_CLIENT,
			     sizeof(*fmkey));
	req_capsule_set_size(&req->rq_pill, &RMF_FIEMAP_VAL, RCL_CLIENT,
			     *buflen);
	req_capsule_set_size(&req->rq_pill, &RMF_FIEMAP_VAL, RCL_SERVER,
			     *buflen);

	rc = ptlrpc_request_pack(req, LUSTRE_OST_VERSION, OST_GET_INFO);
	if (rc) {
		ptlrpc_request_free(req);
		goto drop_lock;
	}
	tmp = req_capsule_client_get(&req->rq_pill, &RMF_FIEMAP_KEY);
	memcpy(tmp, fmkey, sizeof(*fmkey));
	tmp = req_capsule_client_get(&req->rq_pill, &RMF_FIEMAP_VAL);
	memcpy(tmp, fiemap, *buflen);
	ptlrpc_request_set_replen(req);

	rc = ptlrpc_queue_wait(req);
	if (rc)
		goto fini_req;

	reply = req_capsule_server_get(&req->rq_pill, &RMF_FIEMAP_VAL);
	if (!reply) {
		rc = -EPROTO;
		goto fini_req;
	}

	memcpy(fiemap, reply, *buflen);
fini_req:
	ptlrpc_req_finished(req);
drop_lock:
	if (mode)
		ldlm_lock_decref(&lockh, LCK_PR);
	return rc;
}

void osc_object_set_contended(struct osc_object *obj)
{
	obj->oo_contention_time = cfs_time_current();
	/* mb(); */
	obj->oo_contended = 1;
}

void osc_object_clear_contended(struct osc_object *obj)
{
	obj->oo_contended = 0;
}

int osc_object_is_contended(struct osc_object *obj)
{
	struct osc_device *dev = lu2osc_dev(obj->oo_cl.co_lu.lo_dev);
	int osc_contention_time = dev->od_contention_time;
	unsigned long cur_time = cfs_time_current();
	unsigned long retry_time;

	if (OBD_FAIL_CHECK(OBD_FAIL_OSC_OBJECT_CONTENTION))
		return 1;

	if (!obj->oo_contended)
		return 0;

	/*
	 * I like copy-paste. the code is copied from
	 * ll_file_is_contended.
	 */
	retry_time = cfs_time_add(obj->oo_contention_time,
				  cfs_time_seconds(osc_contention_time));
	if (cfs_time_after(cur_time, retry_time)) {
		osc_object_clear_contended(obj);
		return 0;
	}
	return 1;
}

static const struct cl_object_operations osc_ops = {
	.coo_page_init = osc_page_init,
	.coo_lock_init = osc_lock_init,
	.coo_io_init   = osc_io_init,
	.coo_attr_get  = osc_attr_get,
	.coo_attr_update = osc_attr_update,
	.coo_glimpse   = osc_object_glimpse,
	.coo_prune	 = osc_object_prune,
	.coo_fiemap	 = osc_object_fiemap,
};

static const struct lu_object_operations osc_lu_obj_ops = {
	.loo_object_init      = osc_object_init,
	.loo_object_release   = NULL,
	.loo_object_free      = osc_object_free,
	.loo_object_print     = osc_object_print,
	.loo_object_invariant = NULL
};

struct lu_object *osc_object_alloc(const struct lu_env *env,
				   const struct lu_object_header *unused,
				   struct lu_device *dev)
{
	struct osc_object *osc;
	struct lu_object *obj;

	osc = kmem_cache_zalloc(osc_object_kmem, GFP_NOFS);
	if (osc) {
		obj = osc2lu(osc);
		lu_object_init(obj, NULL, dev);
		osc->oo_cl.co_ops = &osc_ops;
		obj->lo_ops = &osc_lu_obj_ops;
	} else {
		obj = NULL;
	}
	return obj;
}

/** @} osc */
