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
 * Implementation of cl_object for LOV layer.
 *
 *   Author: Nikita Danilov <nikita.danilov@sun.com>
 *   Author: Jinshan Xiong <jinshan.xiong@whamcloud.com>
 */

#define DEBUG_SUBSYSTEM S_LOV

#include "lov_cl_internal.h"

static inline struct lov_device *lov_object_dev(struct lov_object *obj)
{
	return lu2lov_dev(obj->lo_cl.co_lu.lo_dev);
}

/** \addtogroup lov
 *  @{
 */

/*****************************************************************************
 *
 * Layout operations.
 *
 */

struct lov_layout_operations {
	int (*llo_init)(const struct lu_env *env, struct lov_device *dev,
			struct lov_object *lov, struct lov_stripe_md *lsm,
			const struct cl_object_conf *conf,
			union lov_layout_state *state);
	int (*llo_delete)(const struct lu_env *env, struct lov_object *lov,
			  union lov_layout_state *state);
	void (*llo_fini)(const struct lu_env *env, struct lov_object *lov,
			 union lov_layout_state *state);
	void (*llo_install)(const struct lu_env *env, struct lov_object *lov,
			    union lov_layout_state *state);
	int  (*llo_print)(const struct lu_env *env, void *cookie,
			  lu_printer_t p, const struct lu_object *o);
	int  (*llo_page_init)(const struct lu_env *env, struct cl_object *obj,
			      struct cl_page *page, pgoff_t index);
	int  (*llo_lock_init)(const struct lu_env *env,
			      struct cl_object *obj, struct cl_lock *lock,
			      const struct cl_io *io);
	int  (*llo_io_init)(const struct lu_env *env,
			    struct cl_object *obj, struct cl_io *io);
	int  (*llo_getattr)(const struct lu_env *env, struct cl_object *obj,
			    struct cl_attr *attr);
};

static int lov_layout_wait(const struct lu_env *env, struct lov_object *lov);

static void lov_lsm_put(struct lov_stripe_md *lsm)
{
	if (lsm)
		lov_free_memmd(&lsm);
}

/*****************************************************************************
 *
 * Lov object layout operations.
 *
 */

static void lov_install_empty(const struct lu_env *env,
			      struct lov_object *lov,
			      union  lov_layout_state *state)
{
	/*
	 * File without objects.
	 */
}

static int lov_init_empty(const struct lu_env *env, struct lov_device *dev,
			  struct lov_object *lov, struct lov_stripe_md *lsm,
			  const struct cl_object_conf *conf,
			  union lov_layout_state *state)
{
	return 0;
}

static void lov_install_raid0(const struct lu_env *env,
			      struct lov_object *lov,
			      union lov_layout_state *state)
{
}

static struct cl_object *lov_sub_find(const struct lu_env *env,
				      struct cl_device *dev,
				      const struct lu_fid *fid,
				      const struct cl_object_conf *conf)
{
	struct lu_object *o;

	o = lu_object_find_at(env, cl2lu_dev(dev), fid, &conf->coc_lu);
	LASSERT(ergo(!IS_ERR(o), o->lo_dev->ld_type == &lovsub_device_type));
	return lu2cl(o);
}

static int lov_init_sub(const struct lu_env *env, struct lov_object *lov,
			struct cl_object *stripe, struct lov_layout_raid0 *r0,
			int idx)
{
	struct cl_object_header *hdr;
	struct cl_object_header *subhdr;
	struct cl_object_header *parent;
	struct lov_oinfo	*oinfo;
	int result;

	if (OBD_FAIL_CHECK(OBD_FAIL_LOV_INIT)) {
		/* For sanity:test_206.
		 * Do not leave the object in cache to avoid accessing
		 * freed memory. This is because osc_object is referring to
		 * lov_oinfo of lsm_stripe_data which will be freed due to
		 * this failure.
		 */
		cl_object_kill(env, stripe);
		cl_object_put(env, stripe);
		return -EIO;
	}

	hdr    = cl_object_header(lov2cl(lov));
	subhdr = cl_object_header(stripe);

	oinfo = lov->lo_lsm->lsm_oinfo[idx];
	CDEBUG(D_INODE, DFID "@%p[%d] -> " DFID "@%p: ostid: " DOSTID " idx: %d gen: %d\n",
	       PFID(&subhdr->coh_lu.loh_fid), subhdr, idx,
	       PFID(&hdr->coh_lu.loh_fid), hdr, POSTID(&oinfo->loi_oi),
	       oinfo->loi_ost_idx, oinfo->loi_ost_gen);

	/* reuse ->coh_attr_guard to protect coh_parent change */
	spin_lock(&subhdr->coh_attr_guard);
	parent = subhdr->coh_parent;
	if (!parent) {
		subhdr->coh_parent = hdr;
		spin_unlock(&subhdr->coh_attr_guard);
		subhdr->coh_nesting = hdr->coh_nesting + 1;
		lu_object_ref_add(&stripe->co_lu, "lov-parent", lov);
		r0->lo_sub[idx] = cl2lovsub(stripe);
		r0->lo_sub[idx]->lso_super = lov;
		r0->lo_sub[idx]->lso_index = idx;
		result = 0;
	} else {
		struct lu_object  *old_obj;
		struct lov_object *old_lov;
		unsigned int mask = D_INODE;

		spin_unlock(&subhdr->coh_attr_guard);
		old_obj = lu_object_locate(&parent->coh_lu, &lov_device_type);
		LASSERT(old_obj);
		old_lov = cl2lov(lu2cl(old_obj));
		if (old_lov->lo_layout_invalid) {
			/* the object's layout has already changed but isn't
			 * refreshed
			 */
			lu_object_unhash(env, &stripe->co_lu);
			result = -EAGAIN;
		} else {
			mask = D_ERROR;
			result = -EIO;
		}

		LU_OBJECT_DEBUG(mask, env, &stripe->co_lu,
				"stripe %d is already owned.", idx);
		LU_OBJECT_DEBUG(mask, env, old_obj, "owned.");
		LU_OBJECT_HEADER(mask, env, lov2lu(lov), "try to own.\n");
		cl_object_put(env, stripe);
	}
	return result;
}

static int lov_page_slice_fixup(struct lov_object *lov,
				struct cl_object *stripe)
{
	struct cl_object_header *hdr = cl_object_header(&lov->lo_cl);
	struct cl_object *o;

	if (!stripe)
		return hdr->coh_page_bufsize - lov->lo_cl.co_slice_off -
		       cfs_size_round(sizeof(struct lov_page));

	cl_object_for_each(o, stripe)
		o->co_slice_off += hdr->coh_page_bufsize;

	return cl_object_header(stripe)->coh_page_bufsize;
}

static int lov_init_raid0(const struct lu_env *env, struct lov_device *dev,
			  struct lov_object *lov, struct lov_stripe_md *lsm,
			  const struct cl_object_conf *conf,
			  union  lov_layout_state *state)
{
	int result;
	int i;

	struct cl_object	*stripe;
	struct lov_thread_info  *lti     = lov_env_info(env);
	struct cl_object_conf   *subconf = &lti->lti_stripe_conf;
	struct lu_fid	   *ofid    = &lti->lti_fid;
	struct lov_layout_raid0 *r0      = &state->raid0;

	if (lsm->lsm_magic != LOV_MAGIC_V1 && lsm->lsm_magic != LOV_MAGIC_V3) {
		dump_lsm(D_ERROR, lsm);
		LASSERTF(0, "magic mismatch, expected %d/%d, actual %d.\n",
			 LOV_MAGIC_V1, LOV_MAGIC_V3, lsm->lsm_magic);
	}

	LASSERT(!lov->lo_lsm);
	lov->lo_lsm = lsm_addref(lsm);
	lov->lo_layout_invalid = true;
	r0->lo_nr  = lsm->lsm_stripe_count;
	LASSERT(r0->lo_nr <= lov_targets_nr(dev));

	r0->lo_sub = libcfs_kvzalloc(r0->lo_nr * sizeof(r0->lo_sub[0]),
				     GFP_NOFS);
	if (r0->lo_sub) {
		int psz = 0;

		result = 0;
		subconf->coc_inode = conf->coc_inode;
		spin_lock_init(&r0->lo_sub_lock);
		/*
		 * Create stripe cl_objects.
		 */
		for (i = 0; i < r0->lo_nr && result == 0; ++i) {
			struct cl_device *subdev;
			struct lov_oinfo *oinfo = lsm->lsm_oinfo[i];
			int ost_idx = oinfo->loi_ost_idx;

			if (lov_oinfo_is_dummy(oinfo))
				continue;

			result = ostid_to_fid(ofid, &oinfo->loi_oi,
					      oinfo->loi_ost_idx);
			if (result != 0)
				goto out;

			if (!dev->ld_target[ost_idx]) {
				CERROR("%s: OST %04x is not initialized\n",
				lov2obd(dev->ld_lov)->obd_name, ost_idx);
				result = -EIO;
				goto out;
			}

			subdev = lovsub2cl_dev(dev->ld_target[ost_idx]);
			subconf->u.coc_oinfo = oinfo;
			LASSERTF(subdev, "not init ost %d\n", ost_idx);
			/* In the function below, .hs_keycmp resolves to
			 * lu_obj_hop_keycmp()
			 */
			/* coverity[overrun-buffer-val] */
			stripe = lov_sub_find(env, subdev, ofid, subconf);
			if (!IS_ERR(stripe)) {
				result = lov_init_sub(env, lov, stripe, r0, i);
				if (result == -EAGAIN) { /* try again */
					--i;
					result = 0;
					continue;
				}
			} else {
				result = PTR_ERR(stripe);
			}

			if (result == 0) {
				int sz = lov_page_slice_fixup(lov, stripe);

				LASSERT(ergo(psz > 0, psz == sz));
				psz = sz;
			}
		}
		if (result == 0)
			cl_object_header(&lov->lo_cl)->coh_page_bufsize += psz;
	} else {
		result = -ENOMEM;
	}
out:
	return result;
}

static int lov_init_released(const struct lu_env *env, struct lov_device *dev,
			     struct lov_object *lov, struct lov_stripe_md *lsm,
			     const struct cl_object_conf *conf,
			     union  lov_layout_state *state)
{
	LASSERT(lsm);
	LASSERT(lsm_is_released(lsm));
	LASSERT(!lov->lo_lsm);

	lov->lo_lsm = lsm_addref(lsm);
	return 0;
}

static struct cl_object *lov_find_subobj(const struct lu_env *env,
					 struct lov_object *lov,
					 struct lov_stripe_md *lsm,
					 int stripe_idx)
{
	struct lov_device *dev = lu2lov_dev(lov2lu(lov)->lo_dev);
	struct lov_oinfo *oinfo = lsm->lsm_oinfo[stripe_idx];
	struct lov_thread_info *lti = lov_env_info(env);
	struct lu_fid *ofid = &lti->lti_fid;
	struct cl_device *subdev;
	struct cl_object *result;
	int ost_idx;
	int rc;

	if (lov->lo_type != LLT_RAID0) {
		result = NULL;
		goto out;
	}

	ost_idx = oinfo->loi_ost_idx;
	rc = ostid_to_fid(ofid, &oinfo->loi_oi, ost_idx);
	if (rc) {
		result = NULL;
		goto out;
	}

	subdev = lovsub2cl_dev(dev->ld_target[ost_idx]);
	result = lov_sub_find(env, subdev, ofid, NULL);
out:
	if (!result)
		result = ERR_PTR(-EINVAL);
	return result;
}

static int lov_delete_empty(const struct lu_env *env, struct lov_object *lov,
			    union lov_layout_state *state)
{
	LASSERT(lov->lo_type == LLT_EMPTY || lov->lo_type == LLT_RELEASED);

	lov_layout_wait(env, lov);
	return 0;
}

static void lov_subobject_kill(const struct lu_env *env, struct lov_object *lov,
			       struct lovsub_object *los, int idx)
{
	struct cl_object	*sub;
	struct lov_layout_raid0 *r0;
	struct lu_site	  *site;
	struct lu_site_bkt_data *bkt;
	wait_queue_entry_t	  *waiter;

	r0  = &lov->u.raid0;
	LASSERT(r0->lo_sub[idx] == los);

	sub  = lovsub2cl(los);
	site = sub->co_lu.lo_dev->ld_site;
	bkt  = lu_site_bkt_from_fid(site, &sub->co_lu.lo_header->loh_fid);

	cl_object_kill(env, sub);
	/* release a reference to the sub-object and ... */
	lu_object_ref_del(&sub->co_lu, "lov-parent", lov);
	cl_object_put(env, sub);

	/* ... wait until it is actually destroyed---sub-object clears its
	 * ->lo_sub[] slot in lovsub_object_fini()
	 */
	if (r0->lo_sub[idx] == los) {
		waiter = &lov_env_info(env)->lti_waiter;
		init_waitqueue_entry(waiter, current);
		add_wait_queue(&bkt->lsb_marche_funebre, waiter);
		set_current_state(TASK_UNINTERRUPTIBLE);
		while (1) {
			/* this wait-queue is signaled at the end of
			 * lu_object_free().
			 */
			set_current_state(TASK_UNINTERRUPTIBLE);
			spin_lock(&r0->lo_sub_lock);
			if (r0->lo_sub[idx] == los) {
				spin_unlock(&r0->lo_sub_lock);
				schedule();
			} else {
				spin_unlock(&r0->lo_sub_lock);
				set_current_state(TASK_RUNNING);
				break;
			}
		}
		remove_wait_queue(&bkt->lsb_marche_funebre, waiter);
	}
	LASSERT(!r0->lo_sub[idx]);
}

static int lov_delete_raid0(const struct lu_env *env, struct lov_object *lov,
			    union lov_layout_state *state)
{
	struct lov_layout_raid0 *r0 = &state->raid0;
	struct lov_stripe_md    *lsm = lov->lo_lsm;
	int i;

	dump_lsm(D_INODE, lsm);

	lov_layout_wait(env, lov);
	if (r0->lo_sub) {
		for (i = 0; i < r0->lo_nr; ++i) {
			struct lovsub_object *los = r0->lo_sub[i];

			if (los) {
				cl_object_prune(env, &los->lso_cl);
				/*
				 * If top-level object is to be evicted from
				 * the cache, so are its sub-objects.
				 */
				lov_subobject_kill(env, lov, los, i);
			}
		}
	}
	return 0;
}

static void lov_fini_empty(const struct lu_env *env, struct lov_object *lov,
			   union lov_layout_state *state)
{
	LASSERT(lov->lo_type == LLT_EMPTY || lov->lo_type == LLT_RELEASED);
}

static void lov_fini_raid0(const struct lu_env *env, struct lov_object *lov,
			   union lov_layout_state *state)
{
	struct lov_layout_raid0 *r0 = &state->raid0;

	if (r0->lo_sub) {
		kvfree(r0->lo_sub);
		r0->lo_sub = NULL;
	}

	dump_lsm(D_INODE, lov->lo_lsm);
	lov_free_memmd(&lov->lo_lsm);
}

static void lov_fini_released(const struct lu_env *env, struct lov_object *lov,
			      union lov_layout_state *state)
{
	dump_lsm(D_INODE, lov->lo_lsm);
	lov_free_memmd(&lov->lo_lsm);
}

static int lov_print_empty(const struct lu_env *env, void *cookie,
			   lu_printer_t p, const struct lu_object *o)
{
	(*p)(env, cookie, "empty %d\n", lu2lov(o)->lo_layout_invalid);
	return 0;
}

static int lov_print_raid0(const struct lu_env *env, void *cookie,
			   lu_printer_t p, const struct lu_object *o)
{
	struct lov_object	*lov = lu2lov(o);
	struct lov_layout_raid0	*r0  = lov_r0(lov);
	struct lov_stripe_md	*lsm = lov->lo_lsm;
	int			 i;

	(*p)(env, cookie, "stripes: %d, %s, lsm{%p 0x%08X %d %u %u}:\n",
	     r0->lo_nr, lov->lo_layout_invalid ? "invalid" : "valid", lsm,
	     lsm->lsm_magic, atomic_read(&lsm->lsm_refc),
	     lsm->lsm_stripe_count, lsm->lsm_layout_gen);
	for (i = 0; i < r0->lo_nr; ++i) {
		struct lu_object *sub;

		if (r0->lo_sub[i]) {
			sub = lovsub2lu(r0->lo_sub[i]);
			lu_object_print(env, cookie, p, sub);
		} else {
			(*p)(env, cookie, "sub %d absent\n", i);
		}
	}
	return 0;
}

static int lov_print_released(const struct lu_env *env, void *cookie,
			      lu_printer_t p, const struct lu_object *o)
{
	struct lov_object	*lov = lu2lov(o);
	struct lov_stripe_md	*lsm = lov->lo_lsm;

	(*p)(env, cookie,
	     "released: %s, lsm{%p 0x%08X %d %u %u}:\n",
	     lov->lo_layout_invalid ? "invalid" : "valid", lsm,
	     lsm->lsm_magic, atomic_read(&lsm->lsm_refc),
	     lsm->lsm_stripe_count, lsm->lsm_layout_gen);
	return 0;
}

/**
 * Implements cl_object_operations::coo_attr_get() method for an object
 * without stripes (LLT_EMPTY layout type).
 *
 * The only attributes this layer is authoritative in this case is
 * cl_attr::cat_blocks---it's 0.
 */
static int lov_attr_get_empty(const struct lu_env *env, struct cl_object *obj,
			      struct cl_attr *attr)
{
	attr->cat_blocks = 0;
	return 0;
}

static int lov_attr_get_raid0(const struct lu_env *env, struct cl_object *obj,
			      struct cl_attr *attr)
{
	struct lov_object	*lov = cl2lov(obj);
	struct lov_layout_raid0 *r0 = lov_r0(lov);
	struct cl_attr		*lov_attr = &r0->lo_attr;
	int			 result = 0;

	/* this is called w/o holding type guard mutex, so it must be inside
	 * an on going IO otherwise lsm may be replaced.
	 * LU-2117: it turns out there exists one exception. For mmaped files,
	 * the lock of those files may be requested in the other file's IO
	 * context, and this function is called in ccc_lock_state(), it will
	 * hit this assertion.
	 * Anyway, it's still okay to call attr_get w/o type guard as layout
	 * can't go if locks exist.
	 */
	/* LASSERT(atomic_read(&lsm->lsm_refc) > 1); */

	if (!r0->lo_attr_valid) {
		struct lov_stripe_md    *lsm = lov->lo_lsm;
		struct ost_lvb	  *lvb = &lov_env_info(env)->lti_lvb;
		__u64		    kms = 0;

		memset(lvb, 0, sizeof(*lvb));
		/* XXX: timestamps can be negative by sanity:test_39m,
		 * how can it be?
		 */
		lvb->lvb_atime = LLONG_MIN;
		lvb->lvb_ctime = LLONG_MIN;
		lvb->lvb_mtime = LLONG_MIN;

		/*
		 * XXX that should be replaced with a loop over sub-objects,
		 * doing cl_object_attr_get() on them. But for now, let's
		 * reuse old lov code.
		 */

		/*
		 * XXX take lsm spin-lock to keep lov_merge_lvb_kms()
		 * happy. It's not needed, because new code uses
		 * ->coh_attr_guard spin-lock to protect consistency of
		 * sub-object attributes.
		 */
		lov_stripe_lock(lsm);
		result = lov_merge_lvb_kms(lsm, lvb, &kms);
		lov_stripe_unlock(lsm);
		if (result == 0) {
			cl_lvb2attr(lov_attr, lvb);
			lov_attr->cat_kms = kms;
			r0->lo_attr_valid = 1;
		}
	}
	if (result == 0) { /* merge results */
		attr->cat_blocks = lov_attr->cat_blocks;
		attr->cat_size = lov_attr->cat_size;
		attr->cat_kms = lov_attr->cat_kms;
		if (attr->cat_atime < lov_attr->cat_atime)
			attr->cat_atime = lov_attr->cat_atime;
		if (attr->cat_ctime < lov_attr->cat_ctime)
			attr->cat_ctime = lov_attr->cat_ctime;
		if (attr->cat_mtime < lov_attr->cat_mtime)
			attr->cat_mtime = lov_attr->cat_mtime;
	}
	return result;
}

static const struct lov_layout_operations lov_dispatch[] = {
	[LLT_EMPTY] = {
		.llo_init      = lov_init_empty,
		.llo_delete    = lov_delete_empty,
		.llo_fini      = lov_fini_empty,
		.llo_install   = lov_install_empty,
		.llo_print     = lov_print_empty,
		.llo_page_init = lov_page_init_empty,
		.llo_lock_init = lov_lock_init_empty,
		.llo_io_init   = lov_io_init_empty,
		.llo_getattr   = lov_attr_get_empty
	},
	[LLT_RAID0] = {
		.llo_init      = lov_init_raid0,
		.llo_delete    = lov_delete_raid0,
		.llo_fini      = lov_fini_raid0,
		.llo_install   = lov_install_raid0,
		.llo_print     = lov_print_raid0,
		.llo_page_init = lov_page_init_raid0,
		.llo_lock_init = lov_lock_init_raid0,
		.llo_io_init   = lov_io_init_raid0,
		.llo_getattr   = lov_attr_get_raid0
	},
	[LLT_RELEASED] = {
		.llo_init      = lov_init_released,
		.llo_delete    = lov_delete_empty,
		.llo_fini      = lov_fini_released,
		.llo_install   = lov_install_empty,
		.llo_print     = lov_print_released,
		.llo_page_init = lov_page_init_empty,
		.llo_lock_init = lov_lock_init_empty,
		.llo_io_init   = lov_io_init_released,
		.llo_getattr   = lov_attr_get_empty
	}
};

/**
 * Performs a double-dispatch based on the layout type of an object.
 */
#define LOV_2DISPATCH_NOLOCK(obj, op, ...)			      \
({								      \
	struct lov_object		      *__obj = (obj);	  \
	enum lov_layout_type		    __llt;		  \
									\
	__llt = __obj->lo_type;					 \
	LASSERT(__llt < ARRAY_SIZE(lov_dispatch));		\
	lov_dispatch[__llt].op(__VA_ARGS__);			    \
})

/**
 * Return lov_layout_type associated with a given lsm
 */
static enum lov_layout_type lov_type(struct lov_stripe_md *lsm)
{
	if (!lsm)
		return LLT_EMPTY;
	if (lsm_is_released(lsm))
		return LLT_RELEASED;
	return LLT_RAID0;
}

static inline void lov_conf_freeze(struct lov_object *lov)
{
	CDEBUG(D_INODE, "To take share lov(%p) owner %p/%p\n",
	       lov, lov->lo_owner, current);
	if (lov->lo_owner != current)
		down_read(&lov->lo_type_guard);
}

static inline void lov_conf_thaw(struct lov_object *lov)
{
	CDEBUG(D_INODE, "To release share lov(%p) owner %p/%p\n",
	       lov, lov->lo_owner, current);
	if (lov->lo_owner != current)
		up_read(&lov->lo_type_guard);
}

#define LOV_2DISPATCH_MAYLOCK(obj, op, lock, ...)		       \
({								      \
	struct lov_object		      *__obj = (obj);	  \
	int				     __lock = !!(lock);      \
	typeof(lov_dispatch[0].op(__VA_ARGS__)) __result;	       \
									\
	if (__lock)						     \
		lov_conf_freeze(__obj);					\
	__result = LOV_2DISPATCH_NOLOCK(obj, op, __VA_ARGS__);	  \
	if (__lock)						     \
		lov_conf_thaw(__obj);					\
	__result;						       \
})

/**
 * Performs a locked double-dispatch based on the layout type of an object.
 */
#define LOV_2DISPATCH(obj, op, ...)		     \
	LOV_2DISPATCH_MAYLOCK(obj, op, 1, __VA_ARGS__)

#define LOV_2DISPATCH_VOID(obj, op, ...)				\
do {								    \
	struct lov_object		      *__obj = (obj);	  \
	enum lov_layout_type		    __llt;		  \
									\
	lov_conf_freeze(__obj);						\
	__llt = __obj->lo_type;					 \
	LASSERT(__llt < ARRAY_SIZE(lov_dispatch));	\
	lov_dispatch[__llt].op(__VA_ARGS__);			    \
	lov_conf_thaw(__obj);						\
} while (0)

static void lov_conf_lock(struct lov_object *lov)
{
	LASSERT(lov->lo_owner != current);
	down_write(&lov->lo_type_guard);
	LASSERT(!lov->lo_owner);
	lov->lo_owner = current;
	CDEBUG(D_INODE, "Took exclusive lov(%p) owner %p\n",
	       lov, lov->lo_owner);
}

static void lov_conf_unlock(struct lov_object *lov)
{
	CDEBUG(D_INODE, "To release exclusive lov(%p) owner %p\n",
	       lov, lov->lo_owner);
	lov->lo_owner = NULL;
	up_write(&lov->lo_type_guard);
}

static int lov_layout_wait(const struct lu_env *env, struct lov_object *lov)
{
	struct l_wait_info lwi = { 0 };

	while (atomic_read(&lov->lo_active_ios) > 0) {
		CDEBUG(D_INODE, "file:" DFID " wait for active IO, now: %d.\n",
		       PFID(lu_object_fid(lov2lu(lov))),
		       atomic_read(&lov->lo_active_ios));

		l_wait_event(lov->lo_waitq,
			     atomic_read(&lov->lo_active_ios) == 0, &lwi);
	}
	return 0;
}

static int lov_layout_change(const struct lu_env *unused,
			     struct lov_object *lov, struct lov_stripe_md *lsm,
			     const struct cl_object_conf *conf)
{
	struct lov_device *lov_dev = lov_object_dev(lov);
	enum lov_layout_type llt = lov_type(lsm);
	union lov_layout_state *state = &lov->u;
	const struct lov_layout_operations *old_ops;
	const struct lov_layout_operations *new_ops;
	struct lu_env *env;
	u16 refcheck;
	int rc;

	LASSERT(lov->lo_type < ARRAY_SIZE(lov_dispatch));

	env = cl_env_get(&refcheck);
	if (IS_ERR(env))
		return PTR_ERR(env);

	LASSERT(llt < ARRAY_SIZE(lov_dispatch));

	CDEBUG(D_INODE, DFID " from %s to %s\n",
	       PFID(lu_object_fid(lov2lu(lov))),
	       llt2str(lov->lo_type), llt2str(llt));

	old_ops = &lov_dispatch[lov->lo_type];
	new_ops = &lov_dispatch[llt];

	rc = cl_object_prune(env, &lov->lo_cl);
	if (rc)
		goto out;

	rc = old_ops->llo_delete(env, lov, &lov->u);
	if (rc)
		goto out;

	old_ops->llo_fini(env, lov, &lov->u);

	LASSERT(!atomic_read(&lov->lo_active_ios));

	CDEBUG(D_INODE, DFID "Apply new layout lov %p, type %d\n",
	       PFID(lu_object_fid(lov2lu(lov))), lov, llt);

	lov->lo_type = LLT_EMPTY;

	/* page bufsize fixup */
	cl_object_header(&lov->lo_cl)->coh_page_bufsize -=
			lov_page_slice_fixup(lov, NULL);

	rc = new_ops->llo_init(env, lov_dev, lov, lsm, conf, state);
	if (rc) {
		struct obd_device *obd = lov2obd(lov_dev->ld_lov);

		CERROR("%s: cannot apply new layout on " DFID " : rc = %d\n",
		       obd->obd_name, PFID(lu_object_fid(lov2lu(lov))), rc);
		new_ops->llo_delete(env, lov, state);
		new_ops->llo_fini(env, lov, state);
		/* this file becomes an EMPTY file. */
		goto out;
	}

	new_ops->llo_install(env, lov, state);
	lov->lo_type = llt;
out:
	cl_env_put(env, &refcheck);
	return rc;
}

/*****************************************************************************
 *
 * Lov object operations.
 *
 */
int lov_object_init(const struct lu_env *env, struct lu_object *obj,
		    const struct lu_object_conf *conf)
{
	struct lov_object	    *lov   = lu2lov(obj);
	struct lov_device *dev = lov_object_dev(lov);
	const struct cl_object_conf  *cconf = lu2cl_conf(conf);
	union  lov_layout_state      *set   = &lov->u;
	const struct lov_layout_operations *ops;
	struct lov_stripe_md *lsm = NULL;
	int rc;

	init_rwsem(&lov->lo_type_guard);
	atomic_set(&lov->lo_active_ios, 0);
	init_waitqueue_head(&lov->lo_waitq);
	cl_object_page_init(lu2cl(obj), sizeof(struct lov_page));

	lov->lo_type = LLT_EMPTY;
	if (cconf->u.coc_layout.lb_buf) {
		lsm = lov_unpackmd(dev->ld_lov,
				   cconf->u.coc_layout.lb_buf,
				   cconf->u.coc_layout.lb_len);
		if (IS_ERR(lsm))
			return PTR_ERR(lsm);
	}

	/* no locking is necessary, as object is being created */
	lov->lo_type = lov_type(lsm);
	ops = &lov_dispatch[lov->lo_type];
	rc = ops->llo_init(env, dev, lov, lsm, cconf, set);
	if (!rc)
		ops->llo_install(env, lov, set);

	lov_lsm_put(lsm);

	return rc;
}

static int lov_conf_set(const struct lu_env *env, struct cl_object *obj,
			const struct cl_object_conf *conf)
{
	struct lov_stripe_md	*lsm = NULL;
	struct lov_object	*lov = cl2lov(obj);
	int			 result = 0;

	if (conf->coc_opc == OBJECT_CONF_SET &&
	    conf->u.coc_layout.lb_buf) {
		lsm = lov_unpackmd(lov_object_dev(lov)->ld_lov,
				   conf->u.coc_layout.lb_buf,
				   conf->u.coc_layout.lb_len);
		if (IS_ERR(lsm))
			return PTR_ERR(lsm);
	}

	lov_conf_lock(lov);
	if (conf->coc_opc == OBJECT_CONF_INVALIDATE) {
		lov->lo_layout_invalid = true;
		result = 0;
		goto out;
	}

	if (conf->coc_opc == OBJECT_CONF_WAIT) {
		if (lov->lo_layout_invalid &&
		    atomic_read(&lov->lo_active_ios) > 0) {
			lov_conf_unlock(lov);
			result = lov_layout_wait(env, lov);
			lov_conf_lock(lov);
		}
		goto out;
	}

	LASSERT(conf->coc_opc == OBJECT_CONF_SET);

	if ((!lsm && !lov->lo_lsm) ||
	    ((lsm && lov->lo_lsm) &&
	     (lov->lo_lsm->lsm_layout_gen == lsm->lsm_layout_gen) &&
	     (lov->lo_lsm->lsm_pattern == lsm->lsm_pattern))) {
		/* same version of layout */
		lov->lo_layout_invalid = false;
		result = 0;
		goto out;
	}

	/* will change layout - check if there still exists active IO. */
	if (atomic_read(&lov->lo_active_ios) > 0) {
		lov->lo_layout_invalid = true;
		result = -EBUSY;
		goto out;
	}

	result = lov_layout_change(env, lov, lsm, conf);
	lov->lo_layout_invalid = result != 0;

out:
	lov_conf_unlock(lov);
	lov_lsm_put(lsm);
	CDEBUG(D_INODE, DFID " lo_layout_invalid=%d\n",
	       PFID(lu_object_fid(lov2lu(lov))), lov->lo_layout_invalid);
	return result;
}

static void lov_object_delete(const struct lu_env *env, struct lu_object *obj)
{
	struct lov_object *lov = lu2lov(obj);

	LOV_2DISPATCH_VOID(lov, llo_delete, env, lov, &lov->u);
}

static void lov_object_free(const struct lu_env *env, struct lu_object *obj)
{
	struct lov_object *lov = lu2lov(obj);

	LOV_2DISPATCH_VOID(lov, llo_fini, env, lov, &lov->u);
	lu_object_fini(obj);
	kmem_cache_free(lov_object_kmem, lov);
}

static int lov_object_print(const struct lu_env *env, void *cookie,
			    lu_printer_t p, const struct lu_object *o)
{
	return LOV_2DISPATCH_NOLOCK(lu2lov(o), llo_print, env, cookie, p, o);
}

int lov_page_init(const struct lu_env *env, struct cl_object *obj,
		  struct cl_page *page, pgoff_t index)
{
	return LOV_2DISPATCH_NOLOCK(cl2lov(obj), llo_page_init, env, obj, page,
				    index);
}

/**
 * Implements cl_object_operations::clo_io_init() method for lov
 * layer. Dispatches to the appropriate layout io initialization method.
 */
int lov_io_init(const struct lu_env *env, struct cl_object *obj,
		struct cl_io *io)
{
	CL_IO_SLICE_CLEAN(lov_env_io(env), lis_cl);

	CDEBUG(D_INODE, DFID "io %p type %d ignore/verify layout %d/%d\n",
	       PFID(lu_object_fid(&obj->co_lu)), io, io->ci_type,
	       io->ci_ignore_layout, io->ci_verify_layout);

	return LOV_2DISPATCH_MAYLOCK(cl2lov(obj), llo_io_init,
				     !io->ci_ignore_layout, env, obj, io);
}

/**
 * An implementation of cl_object_operations::clo_attr_get() method for lov
 * layer. For raid0 layout this collects and merges attributes of all
 * sub-objects.
 */
static int lov_attr_get(const struct lu_env *env, struct cl_object *obj,
			struct cl_attr *attr)
{
	/* do not take lock, as this function is called under a
	 * spin-lock. Layout is protected from changing by ongoing IO.
	 */
	return LOV_2DISPATCH_NOLOCK(cl2lov(obj), llo_getattr, env, obj, attr);
}

static int lov_attr_update(const struct lu_env *env, struct cl_object *obj,
			   const struct cl_attr *attr, unsigned int valid)
{
	/*
	 * No dispatch is required here, as no layout implements this.
	 */
	return 0;
}

int lov_lock_init(const struct lu_env *env, struct cl_object *obj,
		  struct cl_lock *lock, const struct cl_io *io)
{
	/* No need to lock because we've taken one refcount of layout.  */
	return LOV_2DISPATCH_NOLOCK(cl2lov(obj), llo_lock_init, env, obj, lock,
				    io);
}

/**
 * We calculate on which OST the mapping will end. If the length of mapping
 * is greater than (stripe_size * stripe_count) then the last_stripe will
 * will be one just before start_stripe. Else we check if the mapping
 * intersects each OST and find last_stripe.
 * This function returns the last_stripe and also sets the stripe_count
 * over which the mapping is spread
 *
 * \param lsm [in]		striping information for the file
 * \param fm_start [in]		logical start of mapping
 * \param fm_end [in]		logical end of mapping
 * \param start_stripe [in]	starting stripe of the mapping
 * \param stripe_count [out]	the number of stripes across which to map is
 *				returned
 *
 * \retval last_stripe		return the last stripe of the mapping
 */
static int fiemap_calc_last_stripe(struct lov_stripe_md *lsm,
				   u64 fm_start, u64 fm_end,
				   int start_stripe, int *stripe_count)
{
	int last_stripe;
	u64 obd_start;
	u64 obd_end;
	int i, j;

	if (fm_end - fm_start > lsm->lsm_stripe_size * lsm->lsm_stripe_count) {
		last_stripe = (start_stripe < 1 ? lsm->lsm_stripe_count - 1 :
			       start_stripe - 1);
		*stripe_count = lsm->lsm_stripe_count;
	} else {
		for (j = 0, i = start_stripe; j < lsm->lsm_stripe_count;
		     i = (i + 1) % lsm->lsm_stripe_count, j++) {
			if (!(lov_stripe_intersects(lsm, i, fm_start, fm_end,
						    &obd_start, &obd_end)))
				break;
		}
		*stripe_count = j;
		last_stripe = (start_stripe + j - 1) % lsm->lsm_stripe_count;
	}

	return last_stripe;
}

/**
 * Set fe_device and copy extents from local buffer into main return buffer.
 *
 * \param fiemap [out]		fiemap to hold all extents
 * \param lcl_fm_ext [in]	array of fiemap extents get from OSC layer
 * \param ost_index [in]	OST index to be written into the fm_device
 *				field for each extent
 * \param ext_count [in]	number of extents to be copied
 * \param current_extent [in]	where to start copying in the extent array
 */
static void fiemap_prepare_and_copy_exts(struct fiemap *fiemap,
					 struct fiemap_extent *lcl_fm_ext,
					 int ost_index, unsigned int ext_count,
					 int current_extent)
{
	unsigned int ext;
	char *to;

	for (ext = 0; ext < ext_count; ext++) {
		lcl_fm_ext[ext].fe_device = ost_index;
		lcl_fm_ext[ext].fe_flags |= FIEMAP_EXTENT_NET;
	}

	/* Copy fm_extent's from fm_local to return buffer */
	to = (char *)fiemap + fiemap_count_to_size(current_extent);
	memcpy(to, lcl_fm_ext, ext_count * sizeof(struct fiemap_extent));
}

#define FIEMAP_BUFFER_SIZE 4096

/**
 * Non-zero fe_logical indicates that this is a continuation FIEMAP
 * call. The local end offset and the device are sent in the first
 * fm_extent. This function calculates the stripe number from the index.
 * This function returns a stripe_no on which mapping is to be restarted.
 *
 * This function returns fm_end_offset which is the in-OST offset at which
 * mapping should be restarted. If fm_end_offset=0 is returned then caller
 * will re-calculate proper offset in next stripe.
 * Note that the first extent is passed to lov_get_info via the value field.
 *
 * \param fiemap [in]		fiemap request header
 * \param lsm [in]		striping information for the file
 * \param fm_start [in]		logical start of mapping
 * \param fm_end [in]		logical end of mapping
 * \param start_stripe [out]	starting stripe will be returned in this
 */
static u64 fiemap_calc_fm_end_offset(struct fiemap *fiemap,
				     struct lov_stripe_md *lsm,
				     u64 fm_start, u64 fm_end,
				     int *start_stripe)
{
	u64 local_end = fiemap->fm_extents[0].fe_logical;
	u64 lun_start, lun_end;
	u64 fm_end_offset;
	int stripe_no = -1;
	int i;

	if (!fiemap->fm_extent_count || !fiemap->fm_extents[0].fe_logical)
		return 0;

	/* Find out stripe_no from ost_index saved in the fe_device */
	for (i = 0; i < lsm->lsm_stripe_count; i++) {
		struct lov_oinfo *oinfo = lsm->lsm_oinfo[i];

		if (lov_oinfo_is_dummy(oinfo))
			continue;

		if (oinfo->loi_ost_idx == fiemap->fm_extents[0].fe_device) {
			stripe_no = i;
			break;
		}
	}

	if (stripe_no == -1)
		return -EINVAL;

	/*
	 * If we have finished mapping on previous device, shift logical
	 * offset to start of next device
	 */
	if (lov_stripe_intersects(lsm, stripe_no, fm_start, fm_end,
				  &lun_start, &lun_end) &&
	    local_end < lun_end) {
		fm_end_offset = local_end;
		*start_stripe = stripe_no;
	} else {
		/* This is a special value to indicate that caller should
		 * calculate offset in next stripe.
		 */
		fm_end_offset = 0;
		*start_stripe = (stripe_no + 1) % lsm->lsm_stripe_count;
	}

	return fm_end_offset;
}

struct fiemap_state {
	struct fiemap	*fs_fm;
	u64		fs_start;
	u64		fs_length;
	u64		fs_end;
	u64		fs_end_offset;
	int		fs_cur_extent;
	int		fs_cnt_need;
	int		fs_start_stripe;
	int		fs_last_stripe;
	bool		fs_device_done;
	bool		fs_finish;
	bool		fs_enough;
};

static int fiemap_for_stripe(const struct lu_env *env, struct cl_object *obj,
			     struct lov_stripe_md *lsm,
			     struct fiemap *fiemap, size_t *buflen,
			     struct ll_fiemap_info_key *fmkey, int stripeno,
			     struct fiemap_state *fs)
{
	struct cl_object *subobj;
	struct lov_obd *lov = lu2lov_dev(obj->co_lu.lo_dev)->ld_lov;
	struct fiemap_extent *fm_ext = &fs->fs_fm->fm_extents[0];
	u64 req_fm_len; /* Stores length of required mapping */
	u64 len_mapped_single_call;
	u64 lun_start;
	u64 lun_end;
	u64 obd_object_end;
	unsigned int ext_count;
	/* EOF for object */
	bool ost_eof = false;
	/* done with required mapping for this OST? */
	bool ost_done = false;
	int ost_index;
	int rc = 0;

	fs->fs_device_done = false;
	/* Find out range of mapping on this stripe */
	if ((lov_stripe_intersects(lsm, stripeno, fs->fs_start, fs->fs_end,
				   &lun_start, &obd_object_end)) == 0)
		return 0;

	if (lov_oinfo_is_dummy(lsm->lsm_oinfo[stripeno]))
		return -EIO;

	/* If this is a continuation FIEMAP call and we are on
	 * starting stripe then lun_start needs to be set to
	 * end_offset */
	if (fs->fs_end_offset != 0 && stripeno == fs->fs_start_stripe)
		lun_start = fs->fs_end_offset;

	lun_end = fs->fs_length;
	if (lun_end != ~0ULL) {
		/* Handle fs->fs_start + fs->fs_length overflow */
		if (fs->fs_start + fs->fs_length < fs->fs_start)
			fs->fs_length = ~0ULL - fs->fs_start;
		lun_end = lov_size_to_stripe(lsm, fs->fs_start + fs->fs_length,
					     stripeno);
	}

	if (lun_start == lun_end)
		return 0;

	req_fm_len = obd_object_end - lun_start;
	fs->fs_fm->fm_length = 0;
	len_mapped_single_call = 0;

	/* find lobsub object */
	subobj = lov_find_subobj(env, cl2lov(obj), lsm, stripeno);
	if (IS_ERR(subobj))
		return PTR_ERR(subobj);
	/* If the output buffer is very large and the objects have many
	 * extents we may need to loop on a single OST repeatedly */
	do {
		if (fiemap->fm_extent_count > 0) {
			/* Don't get too many extents. */
			if (fs->fs_cur_extent + fs->fs_cnt_need >
			    fiemap->fm_extent_count)
				fs->fs_cnt_need = fiemap->fm_extent_count -
						  fs->fs_cur_extent;
		}

		lun_start += len_mapped_single_call;
		fs->fs_fm->fm_length = req_fm_len - len_mapped_single_call;
		req_fm_len = fs->fs_fm->fm_length;
		fs->fs_fm->fm_extent_count = fs->fs_enough ?
					     1 : fs->fs_cnt_need;
		fs->fs_fm->fm_mapped_extents = 0;
		fs->fs_fm->fm_flags = fiemap->fm_flags;

		ost_index = lsm->lsm_oinfo[stripeno]->loi_ost_idx;

		if (ost_index < 0 || ost_index >= lov->desc.ld_tgt_count) {
			rc = -EINVAL;
			goto obj_put;
		}
		/* If OST is inactive, return extent with UNKNOWN flag. */
		if (!lov->lov_tgts[ost_index]->ltd_active) {
			fs->fs_fm->fm_flags |= FIEMAP_EXTENT_LAST;
			fs->fs_fm->fm_mapped_extents = 1;

			fm_ext[0].fe_logical = lun_start;
			fm_ext[0].fe_length = obd_object_end - lun_start;
			fm_ext[0].fe_flags |= FIEMAP_EXTENT_UNKNOWN;

			goto inactive_tgt;
		}

		fs->fs_fm->fm_start = lun_start;
		fs->fs_fm->fm_flags &= ~FIEMAP_FLAG_DEVICE_ORDER;
		memcpy(&fmkey->lfik_fiemap, fs->fs_fm, sizeof(*fs->fs_fm));
		*buflen = fiemap_count_to_size(fs->fs_fm->fm_extent_count);

		rc = cl_object_fiemap(env, subobj, fmkey, fs->fs_fm, buflen);
		if (rc)
			goto obj_put;
inactive_tgt:
		ext_count = fs->fs_fm->fm_mapped_extents;
		if (ext_count == 0) {
			ost_done = true;
			fs->fs_device_done = true;
			/* If last stripe has hold at the end,
			 * we need to return */
			if (stripeno == fs->fs_last_stripe) {
				fiemap->fm_mapped_extents = 0;
				fs->fs_finish = true;
				goto obj_put;
			}
			break;
		} else if (fs->fs_enough) {
			/*
			 * We've collected enough extents and there are
			 * more extents after it.
			 */
			fs->fs_finish = true;
			goto obj_put;
		}

		/* If we just need num of extents, got to next device */
		if (fiemap->fm_extent_count == 0) {
			fs->fs_cur_extent += ext_count;
			break;
		}

		/* prepare to copy retrived map extents */
		len_mapped_single_call = fm_ext[ext_count - 1].fe_logical +
					 fm_ext[ext_count - 1].fe_length -
					 lun_start;

		/* Have we finished mapping on this device? */
		if (req_fm_len <= len_mapped_single_call) {
			ost_done = true;
			fs->fs_device_done = true;
		}

		/* Clear the EXTENT_LAST flag which can be present on
		 * the last extent */
		if (fm_ext[ext_count - 1].fe_flags & FIEMAP_EXTENT_LAST)
			fm_ext[ext_count - 1].fe_flags &= ~FIEMAP_EXTENT_LAST;
		if (lov_stripe_size(lsm, fm_ext[ext_count - 1].fe_logical +
					 fm_ext[ext_count - 1].fe_length,
				    stripeno) >= fmkey->lfik_oa.o_size) {
			ost_eof = true;
			fs->fs_device_done = true;
		}

		fiemap_prepare_and_copy_exts(fiemap, fm_ext, ost_index,
					     ext_count, fs->fs_cur_extent);
		fs->fs_cur_extent += ext_count;

		/* Ran out of available extents? */
		if (fs->fs_cur_extent >= fiemap->fm_extent_count)
			fs->fs_enough = true;
	} while (!ost_done && !ost_eof);

	if (stripeno == fs->fs_last_stripe)
		fs->fs_finish = true;
obj_put:
	cl_object_put(env, subobj);

	return rc;
}

/**
 * Break down the FIEMAP request and send appropriate calls to individual OSTs.
 * This also handles the restarting of FIEMAP calls in case mapping overflows
 * the available number of extents in single call.
 *
 * \param env [in]		lustre environment
 * \param obj [in]		file object
 * \param fmkey [in]		fiemap request header and other info
 * \param fiemap [out]		fiemap buffer holding retrived map extents
 * \param buflen [in/out]	max buffer length of @fiemap, when iterate
 *				each OST, it is used to limit max map needed
 * \retval 0	success
 * \retval < 0	error
 */
static int lov_object_fiemap(const struct lu_env *env, struct cl_object *obj,
			     struct ll_fiemap_info_key *fmkey,
			     struct fiemap *fiemap, size_t *buflen)
{
	unsigned int buffer_size = FIEMAP_BUFFER_SIZE;
	struct fiemap *fm_local = NULL;
	struct lov_stripe_md *lsm;
	int rc = 0;
	int cur_stripe;
	int stripe_count;
	struct fiemap_state fs = { 0 };

	lsm = lov_lsm_addref(cl2lov(obj));
	if (!lsm)
		return -ENODATA;

	/**
	 * If the stripe_count > 1 and the application does not understand
	 * DEVICE_ORDER flag, it cannot interpret the extents correctly.
	 */
	if (lsm->lsm_stripe_count > 1 &&
	    !(fiemap->fm_flags & FIEMAP_FLAG_DEVICE_ORDER)) {
		rc = -ENOTSUPP;
		goto out;
	}

	if (lsm_is_released(lsm)) {
		if (fiemap->fm_start < fmkey->lfik_oa.o_size) {
			/**
			 * released file, return a minimal FIEMAP if
			 * request fits in file-size.
			 */
			fiemap->fm_mapped_extents = 1;
			fiemap->fm_extents[0].fe_logical = fiemap->fm_start;
			if (fiemap->fm_start + fiemap->fm_length <
			    fmkey->lfik_oa.o_size)
				fiemap->fm_extents[0].fe_length =
					 fiemap->fm_length;
			else
				fiemap->fm_extents[0].fe_length =
					fmkey->lfik_oa.o_size -
					fiemap->fm_start;
			fiemap->fm_extents[0].fe_flags |=
				FIEMAP_EXTENT_UNKNOWN | FIEMAP_EXTENT_LAST;
		}
		rc = 0;
		goto out;
	}

	if (fiemap_count_to_size(fiemap->fm_extent_count) < buffer_size)
		buffer_size = fiemap_count_to_size(fiemap->fm_extent_count);

	fm_local = libcfs_kvzalloc(buffer_size, GFP_NOFS);
	if (!fm_local) {
		rc = -ENOMEM;
		goto out;
	}
	fs.fs_fm = fm_local;
	fs.fs_cnt_need = fiemap_size_to_count(buffer_size);

	fs.fs_start = fiemap->fm_start;
	/* fs_start is beyond the end of the file */
	if (fs.fs_start > fmkey->lfik_oa.o_size) {
		rc = -EINVAL;
		goto out;
	}
	/* Calculate start stripe, last stripe and length of mapping */
	fs.fs_start_stripe = lov_stripe_number(lsm, fs.fs_start);
	fs.fs_end = (fs.fs_length == ~0ULL) ? fmkey->lfik_oa.o_size :
					      fs.fs_start + fs.fs_length - 1;
	/* If fs_length != ~0ULL but fs_start+fs_length-1 exceeds file size */
	if (fs.fs_end > fmkey->lfik_oa.o_size) {
		fs.fs_end = fmkey->lfik_oa.o_size;
		fs.fs_length = fs.fs_end - fs.fs_start;
	}

	fs.fs_last_stripe = fiemap_calc_last_stripe(lsm, fs.fs_start, fs.fs_end,
						    fs.fs_start_stripe,
						    &stripe_count);
	fs.fs_end_offset = fiemap_calc_fm_end_offset(fiemap, lsm, fs.fs_start,
						     fs.fs_end,
						     &fs.fs_start_stripe);
	if (fs.fs_end_offset == -EINVAL) {
		rc = -EINVAL;
		goto out;
	}


	/**
	 * Requested extent count exceeds the fiemap buffer size, shrink our
	 * ambition.
	 */
	if (fiemap_count_to_size(fiemap->fm_extent_count) > *buflen)
		fiemap->fm_extent_count = fiemap_size_to_count(*buflen);
	if (!fiemap->fm_extent_count)
		fs.fs_cnt_need = 0;

	fs.fs_finish = false;
	fs.fs_enough = false;
	fs.fs_cur_extent = 0;

	/* Check each stripe */
	for (cur_stripe = fs.fs_start_stripe; stripe_count > 0;
	     --stripe_count,
	     cur_stripe = (cur_stripe + 1) % lsm->lsm_stripe_count) {
		rc = fiemap_for_stripe(env, obj, lsm, fiemap, buflen, fmkey,
				       cur_stripe, &fs);
		if (rc < 0)
			goto out;
		if (fs.fs_finish)
			break;
	} /* for each stripe */
	/*
	 * Indicate that we are returning device offsets unless file just has
	 * single stripe
	 */
	if (lsm->lsm_stripe_count > 1)
		fiemap->fm_flags |= FIEMAP_FLAG_DEVICE_ORDER;

	if (!fiemap->fm_extent_count)
		goto skip_last_device_calc;

	/*
	 * Check if we have reached the last stripe and whether mapping for that
	 * stripe is done.
	 */
	if ((cur_stripe == fs.fs_last_stripe) && fs.fs_device_done)
		fiemap->fm_extents[fs.fs_cur_extent - 1].fe_flags |=
							FIEMAP_EXTENT_LAST;
skip_last_device_calc:
	fiemap->fm_mapped_extents = fs.fs_cur_extent;
out:
	kvfree(fm_local);
	lov_lsm_put(lsm);
	return rc;
}

static int lov_object_getstripe(const struct lu_env *env, struct cl_object *obj,
				struct lov_user_md __user *lum)
{
	struct lov_object *lov = cl2lov(obj);
	struct lov_stripe_md *lsm;
	int rc = 0;

	lsm = lov_lsm_addref(lov);
	if (!lsm)
		return -ENODATA;

	rc = lov_getstripe(cl2lov(obj), lsm, lum);
	lov_lsm_put(lsm);
	return rc;
}

static int lov_object_layout_get(const struct lu_env *env,
				 struct cl_object *obj,
				 struct cl_layout *cl)
{
	struct lov_object *lov = cl2lov(obj);
	struct lov_stripe_md *lsm = lov_lsm_addref(lov);
	struct lu_buf *buf = &cl->cl_buf;
	ssize_t rc;

	if (!lsm) {
		cl->cl_size = 0;
		cl->cl_layout_gen = CL_LAYOUT_GEN_EMPTY;
		return 0;
	}

	cl->cl_size = lov_mds_md_size(lsm->lsm_stripe_count, lsm->lsm_magic);
	cl->cl_layout_gen = lsm->lsm_layout_gen;

	rc = lov_lsm_pack(lsm, buf->lb_buf, buf->lb_len);
	lov_lsm_put(lsm);

	return rc < 0 ? rc : 0;
}

static loff_t lov_object_maxbytes(struct cl_object *obj)
{
	struct lov_object *lov = cl2lov(obj);
	struct lov_stripe_md *lsm = lov_lsm_addref(lov);
	loff_t maxbytes;

	if (!lsm)
		return LLONG_MAX;

	maxbytes = lsm->lsm_maxbytes;

	lov_lsm_put(lsm);

	return maxbytes;
}

static const struct cl_object_operations lov_ops = {
	.coo_page_init = lov_page_init,
	.coo_lock_init = lov_lock_init,
	.coo_io_init   = lov_io_init,
	.coo_attr_get  = lov_attr_get,
	.coo_attr_update = lov_attr_update,
	.coo_conf_set  = lov_conf_set,
	.coo_getstripe = lov_object_getstripe,
	.coo_layout_get	 = lov_object_layout_get,
	.coo_maxbytes	 = lov_object_maxbytes,
	.coo_fiemap	 = lov_object_fiemap,
};

static const struct lu_object_operations lov_lu_obj_ops = {
	.loo_object_init      = lov_object_init,
	.loo_object_delete    = lov_object_delete,
	.loo_object_release   = NULL,
	.loo_object_free      = lov_object_free,
	.loo_object_print     = lov_object_print,
	.loo_object_invariant = NULL
};

struct lu_object *lov_object_alloc(const struct lu_env *env,
				   const struct lu_object_header *unused,
				   struct lu_device *dev)
{
	struct lov_object *lov;
	struct lu_object  *obj;

	lov = kmem_cache_zalloc(lov_object_kmem, GFP_NOFS);
	if (lov) {
		obj = lov2lu(lov);
		lu_object_init(obj, NULL, dev);
		lov->lo_cl.co_ops = &lov_ops;
		lov->lo_type = -1; /* invalid, to catch uninitialized type */
		/*
		 * object io operation vector (cl_object::co_iop) is installed
		 * later in lov_object_init(), as different vectors are used
		 * for object with different layouts.
		 */
		obj->lo_ops = &lov_lu_obj_ops;
	} else {
		obj = NULL;
	}
	return obj;
}

struct lov_stripe_md *lov_lsm_addref(struct lov_object *lov)
{
	struct lov_stripe_md *lsm = NULL;

	lov_conf_freeze(lov);
	if (lov->lo_lsm) {
		lsm = lsm_addref(lov->lo_lsm);
		CDEBUG(D_INODE, "lsm %p addref %d/%d by %p.\n",
		       lsm, atomic_read(&lsm->lsm_refc),
		       lov->lo_layout_invalid, current);
	}
	lov_conf_thaw(lov);
	return lsm;
}

int lov_read_and_clear_async_rc(struct cl_object *clob)
{
	struct lu_object *luobj;
	int rc = 0;

	luobj = lu_object_locate(&cl_object_header(clob)->coh_lu,
				 &lov_device_type);
	if (luobj) {
		struct lov_object *lov = lu2lov(luobj);

		lov_conf_freeze(lov);
		switch (lov->lo_type) {
		case LLT_RAID0: {
			struct lov_stripe_md *lsm;
			int i;

			lsm = lov->lo_lsm;
			for (i = 0; i < lsm->lsm_stripe_count; i++) {
				struct lov_oinfo *loi = lsm->lsm_oinfo[i];

				if (lov_oinfo_is_dummy(loi))
					continue;

				if (loi->loi_ar.ar_rc && !rc)
					rc = loi->loi_ar.ar_rc;
				loi->loi_ar.ar_rc = 0;
			}
		}
		case LLT_RELEASED:
		case LLT_EMPTY:
			break;
		default:
			LBUG();
		}
		lov_conf_thaw(lov);
	}
	return rc;
}
EXPORT_SYMBOL(lov_read_and_clear_async_rc);

/** @} lov */
