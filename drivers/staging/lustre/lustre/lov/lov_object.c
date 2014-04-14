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
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2012, Intel Corporation.
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
#include <lustre_debug.h>

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
			struct lov_object *lov,
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
				struct cl_page *page, struct page *vmpage);
	int  (*llo_lock_init)(const struct lu_env *env,
			      struct cl_object *obj, struct cl_lock *lock,
			      const struct cl_io *io);
	int  (*llo_io_init)(const struct lu_env *env,
			    struct cl_object *obj, struct cl_io *io);
	int  (*llo_getattr)(const struct lu_env *env, struct cl_object *obj,
			    struct cl_attr *attr);
};

static int lov_layout_wait(const struct lu_env *env, struct lov_object *lov);

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

static int lov_init_empty(const struct lu_env *env,
			  struct lov_device *dev, struct lov_object *lov,
			  const struct cl_object_conf *conf,
			  union  lov_layout_state *state)
{
	return 0;
}

static void lov_install_raid0(const struct lu_env *env,
			      struct lov_object *lov,
			      union  lov_layout_state *state)
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
			struct cl_object *stripe,
			struct lov_layout_raid0 *r0, int idx)
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
		 * this failure. */
		cl_object_kill(env, stripe);
		cl_object_put(env, stripe);
		return -EIO;
	}

	hdr    = cl_object_header(lov2cl(lov));
	subhdr = cl_object_header(stripe);
	parent = subhdr->coh_parent;

	oinfo = lov->lo_lsm->lsm_oinfo[idx];
	CDEBUG(D_INODE, DFID"@%p[%d] -> "DFID"@%p: ostid: "DOSTID
	       " idx: %d gen: %d\n",
	       PFID(&subhdr->coh_lu.loh_fid), subhdr, idx,
	       PFID(&hdr->coh_lu.loh_fid), hdr, POSTID(&oinfo->loi_oi),
	       oinfo->loi_ost_idx, oinfo->loi_ost_gen);

	if (parent == NULL) {
		subhdr->coh_parent = hdr;
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

		old_obj = lu_object_locate(&parent->coh_lu, &lov_device_type);
		LASSERT(old_obj != NULL);
		old_lov = cl2lov(lu2cl(old_obj));
		if (old_lov->lo_layout_invalid) {
			/* the object's layout has already changed but isn't
			 * refreshed */
			lu_object_unhash(env, &stripe->co_lu);
			result = -EAGAIN;
		} else {
			mask = D_ERROR;
			result = -EIO;
		}

		LU_OBJECT_DEBUG(mask, env, &stripe->co_lu,
				"stripe %d is already owned.\n", idx);
		LU_OBJECT_DEBUG(mask, env, old_obj, "owned.\n");
		LU_OBJECT_HEADER(mask, env, lov2lu(lov), "try to own.\n");
		cl_object_put(env, stripe);
	}
	return result;
}

static int lov_init_raid0(const struct lu_env *env,
			  struct lov_device *dev, struct lov_object *lov,
			  const struct cl_object_conf *conf,
			  union  lov_layout_state *state)
{
	int result;
	int i;

	struct cl_object	*stripe;
	struct lov_thread_info  *lti     = lov_env_info(env);
	struct cl_object_conf   *subconf = &lti->lti_stripe_conf;
	struct lov_stripe_md    *lsm     = conf->u.coc_md->lsm;
	struct lu_fid	   *ofid    = &lti->lti_fid;
	struct lov_layout_raid0 *r0      = &state->raid0;

	if (lsm->lsm_magic != LOV_MAGIC_V1 && lsm->lsm_magic != LOV_MAGIC_V3) {
		dump_lsm(D_ERROR, lsm);
		LASSERTF(0, "magic mismatch, expected %d/%d, actual %d.\n",
			 LOV_MAGIC_V1, LOV_MAGIC_V3, lsm->lsm_magic);
	}

	LASSERT(lov->lo_lsm == NULL);
	lov->lo_lsm = lsm_addref(lsm);
	r0->lo_nr  = lsm->lsm_stripe_count;
	LASSERT(r0->lo_nr <= lov_targets_nr(dev));

	OBD_ALLOC_LARGE(r0->lo_sub, r0->lo_nr * sizeof(r0->lo_sub[0]));
	if (r0->lo_sub != NULL) {
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

			result = ostid_to_fid(ofid, &oinfo->loi_oi,
					      oinfo->loi_ost_idx);
			if (result != 0)
				GOTO(out, result);

			subdev = lovsub2cl_dev(dev->ld_target[ost_idx]);
			subconf->u.coc_oinfo = oinfo;
			LASSERTF(subdev != NULL, "not init ost %d\n", ost_idx);
			/* In the function below, .hs_keycmp resolves to
			 * lu_obj_hop_keycmp() */
			/* coverity[overrun-buffer-val] */
			stripe = lov_sub_find(env, subdev, ofid, subconf);
			if (!IS_ERR(stripe)) {
				result = lov_init_sub(env, lov, stripe, r0, i);
				if (result == -EAGAIN) { /* try again */
					--i;
					result = 0;
				}
			} else {
				result = PTR_ERR(stripe);
			}
		}
	} else
		result = -ENOMEM;
out:
	return result;
}

static int lov_init_released(const struct lu_env *env,
			struct lov_device *dev, struct lov_object *lov,
			const struct cl_object_conf *conf,
			union  lov_layout_state *state)
{
	struct lov_stripe_md *lsm = conf->u.coc_md->lsm;

	LASSERT(lsm != NULL);
	LASSERT(lsm_is_released(lsm));
	LASSERT(lov->lo_lsm == NULL);

	lov->lo_lsm = lsm_addref(lsm);
	return 0;
}

static int lov_delete_empty(const struct lu_env *env, struct lov_object *lov,
			    union lov_layout_state *state)
{
	LASSERT(lov->lo_type == LLT_EMPTY || lov->lo_type == LLT_RELEASED);

	lov_layout_wait(env, lov);

	cl_object_prune(env, &lov->lo_cl);
	return 0;
}

static void lov_subobject_kill(const struct lu_env *env, struct lov_object *lov,
			       struct lovsub_object *los, int idx)
{
	struct cl_object	*sub;
	struct lov_layout_raid0 *r0;
	struct lu_site	  *site;
	struct lu_site_bkt_data *bkt;
	wait_queue_t	  *waiter;

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
	 * ->lo_sub[] slot in lovsub_object_fini() */
	if (r0->lo_sub[idx] == los) {
		waiter = &lov_env_info(env)->lti_waiter;
		init_waitqueue_entry_current(waiter);
		add_wait_queue(&bkt->lsb_marche_funebre, waiter);
		set_current_state(TASK_UNINTERRUPTIBLE);
		while (1) {
			/* this wait-queue is signaled at the end of
			 * lu_object_free(). */
			set_current_state(TASK_UNINTERRUPTIBLE);
			spin_lock(&r0->lo_sub_lock);
			if (r0->lo_sub[idx] == los) {
				spin_unlock(&r0->lo_sub_lock);
				waitq_wait(waiter, TASK_UNINTERRUPTIBLE);
			} else {
				spin_unlock(&r0->lo_sub_lock);
				set_current_state(TASK_RUNNING);
				break;
			}
		}
		remove_wait_queue(&bkt->lsb_marche_funebre, waiter);
	}
	LASSERT(r0->lo_sub[idx] == NULL);
}

static int lov_delete_raid0(const struct lu_env *env, struct lov_object *lov,
			    union lov_layout_state *state)
{
	struct lov_layout_raid0 *r0 = &state->raid0;
	struct lov_stripe_md    *lsm = lov->lo_lsm;
	int i;

	dump_lsm(D_INODE, lsm);

	lov_layout_wait(env, lov);
	if (r0->lo_sub != NULL) {
		for (i = 0; i < r0->lo_nr; ++i) {
			struct lovsub_object *los = r0->lo_sub[i];

			if (los != NULL) {
				cl_locks_prune(env, &los->lso_cl, 1);
				/*
				 * If top-level object is to be evicted from
				 * the cache, so are its sub-objects.
				 */
				lov_subobject_kill(env, lov, los, i);
			}
		}
	}
	cl_object_prune(env, &lov->lo_cl);
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

	if (r0->lo_sub != NULL) {
		OBD_FREE_LARGE(r0->lo_sub, r0->lo_nr * sizeof(r0->lo_sub[0]));
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

		if (r0->lo_sub[i] != NULL) {
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
	 * can't go if locks exist. */
	/* LASSERT(atomic_read(&lsm->lsm_refc) > 1); */

	if (!r0->lo_attr_valid) {
		struct lov_stripe_md    *lsm = lov->lo_lsm;
		struct ost_lvb	  *lvb = &lov_env_info(env)->lti_lvb;
		__u64		    kms = 0;

		memset(lvb, 0, sizeof(*lvb));
		/* XXX: timestamps can be negative by sanity:test_39m,
		 * how can it be? */
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

const static struct lov_layout_operations lov_dispatch[] = {
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
	LASSERT(0 <= __llt && __llt < ARRAY_SIZE(lov_dispatch));	\
	lov_dispatch[__llt].op(__VA_ARGS__);			    \
})

/**
 * Return lov_layout_type associated with a given lsm
 */
enum lov_layout_type lov_type(struct lov_stripe_md *lsm)
{
	if (lsm == NULL)
		return LLT_EMPTY;
	if (lsm_is_released(lsm))
		return LLT_RELEASED;
	return LLT_RAID0;
}

static inline void lov_conf_freeze(struct lov_object *lov)
{
	if (lov->lo_owner != current)
		down_read(&lov->lo_type_guard);
}

static inline void lov_conf_thaw(struct lov_object *lov)
{
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
	LASSERT(0 <= __llt && __llt < ARRAY_SIZE(lov_dispatch));	\
	lov_dispatch[__llt].op(__VA_ARGS__);			    \
	lov_conf_thaw(__obj);						\
} while (0)

static void lov_conf_lock(struct lov_object *lov)
{
	LASSERT(lov->lo_owner != current);
	down_write(&lov->lo_type_guard);
	LASSERT(lov->lo_owner == NULL);
	lov->lo_owner = current;
}

static void lov_conf_unlock(struct lov_object *lov)
{
	lov->lo_owner = NULL;
	up_write(&lov->lo_type_guard);
}

static int lov_layout_wait(const struct lu_env *env, struct lov_object *lov)
{
	struct l_wait_info lwi = { 0 };

	while (atomic_read(&lov->lo_active_ios) > 0) {
		CDEBUG(D_INODE, "file:"DFID" wait for active IO, now: %d.\n",
			PFID(lu_object_fid(lov2lu(lov))),
			atomic_read(&lov->lo_active_ios));

		l_wait_event(lov->lo_waitq,
			     atomic_read(&lov->lo_active_ios) == 0, &lwi);
	}
	return 0;
}

static int lov_layout_change(const struct lu_env *unused,
			     struct lov_object *lov,
			     const struct cl_object_conf *conf)
{
	int result;
	enum lov_layout_type llt = LLT_EMPTY;
	union lov_layout_state *state = &lov->u;
	const struct lov_layout_operations *old_ops;
	const struct lov_layout_operations *new_ops;

	struct cl_object_header *hdr = cl_object_header(&lov->lo_cl);
	void *cookie;
	struct lu_env *env;
	int refcheck;

	LASSERT(0 <= lov->lo_type && lov->lo_type < ARRAY_SIZE(lov_dispatch));

	if (conf->u.coc_md != NULL)
		llt = lov_type(conf->u.coc_md->lsm);
	LASSERT(0 <= llt && llt < ARRAY_SIZE(lov_dispatch));

	cookie = cl_env_reenter();
	env = cl_env_get(&refcheck);
	if (IS_ERR(env)) {
		cl_env_reexit(cookie);
		return PTR_ERR(env);
	}

	CDEBUG(D_INODE, DFID" from %s to %s\n",
	       PFID(lu_object_fid(lov2lu(lov))),
	       llt2str(lov->lo_type), llt2str(llt));

	old_ops = &lov_dispatch[lov->lo_type];
	new_ops = &lov_dispatch[llt];

	result = old_ops->llo_delete(env, lov, &lov->u);
	if (result == 0) {
		old_ops->llo_fini(env, lov, &lov->u);

		LASSERT(atomic_read(&lov->lo_active_ios) == 0);
		LASSERT(hdr->coh_tree.rnode == NULL);
		LASSERT(hdr->coh_pages == 0);

		lov->lo_type = LLT_EMPTY;
		result = new_ops->llo_init(env,
					lu2lov_dev(lov->lo_cl.co_lu.lo_dev),
					lov, conf, state);
		if (result == 0) {
			new_ops->llo_install(env, lov, state);
			lov->lo_type = llt;
		} else {
			new_ops->llo_delete(env, lov, state);
			new_ops->llo_fini(env, lov, state);
			/* this file becomes an EMPTY file. */
		}
	}

	cl_env_put(env, &refcheck);
	cl_env_reexit(cookie);
	return result;
}

/*****************************************************************************
 *
 * Lov object operations.
 *
 */
int lov_object_init(const struct lu_env *env, struct lu_object *obj,
		    const struct lu_object_conf *conf)
{
	struct lov_device	    *dev   = lu2lov_dev(obj->lo_dev);
	struct lov_object	    *lov   = lu2lov(obj);
	const struct cl_object_conf  *cconf = lu2cl_conf(conf);
	union  lov_layout_state      *set   = &lov->u;
	const struct lov_layout_operations *ops;
	int result;

	init_rwsem(&lov->lo_type_guard);
	atomic_set(&lov->lo_active_ios, 0);
	init_waitqueue_head(&lov->lo_waitq);

	cl_object_page_init(lu2cl(obj), sizeof(struct lov_page));

	/* no locking is necessary, as object is being created */
	lov->lo_type = lov_type(cconf->u.coc_md->lsm);
	ops = &lov_dispatch[lov->lo_type];
	result = ops->llo_init(env, dev, lov, cconf, set);
	if (result == 0)
		ops->llo_install(env, lov, set);
	return result;
}

static int lov_conf_set(const struct lu_env *env, struct cl_object *obj,
			const struct cl_object_conf *conf)
{
	struct lov_stripe_md	*lsm = NULL;
	struct lov_object	*lov = cl2lov(obj);
	int			 result = 0;

	lov_conf_lock(lov);
	if (conf->coc_opc == OBJECT_CONF_INVALIDATE) {
		lov->lo_layout_invalid = true;
		GOTO(out, result = 0);
	}

	if (conf->coc_opc == OBJECT_CONF_WAIT) {
		if (lov->lo_layout_invalid &&
		    atomic_read(&lov->lo_active_ios) > 0) {
			lov_conf_unlock(lov);
			result = lov_layout_wait(env, lov);
			lov_conf_lock(lov);
		}
		GOTO(out, result);
	}

	LASSERT(conf->coc_opc == OBJECT_CONF_SET);

	if (conf->u.coc_md != NULL)
		lsm = conf->u.coc_md->lsm;
	if ((lsm == NULL && lov->lo_lsm == NULL) ||
	    ((lsm != NULL && lov->lo_lsm != NULL) &&
	     (lov->lo_lsm->lsm_layout_gen == lsm->lsm_layout_gen) &&
	     (lov->lo_lsm->lsm_pattern == lsm->lsm_pattern))) {
		/* same version of layout */
		lov->lo_layout_invalid = false;
		GOTO(out, result = 0);
	}

	/* will change layout - check if there still exists active IO. */
	if (atomic_read(&lov->lo_active_ios) > 0) {
		lov->lo_layout_invalid = true;
		GOTO(out, result = -EBUSY);
	}

	lov->lo_layout_invalid = lov_layout_change(env, lov, conf);

out:
	lov_conf_unlock(lov);
	CDEBUG(D_INODE, DFID" lo_layout_invalid=%d\n",
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
	OBD_SLAB_FREE_PTR(lov, lov_object_kmem);
}

static int lov_object_print(const struct lu_env *env, void *cookie,
			    lu_printer_t p, const struct lu_object *o)
{
	return LOV_2DISPATCH_NOLOCK(lu2lov(o), llo_print, env, cookie, p, o);
}

int lov_page_init(const struct lu_env *env, struct cl_object *obj,
		struct cl_page *page, struct page *vmpage)
{
	return LOV_2DISPATCH_NOLOCK(cl2lov(obj),
				    llo_page_init, env, obj, page, vmpage);
}

/**
 * Implements cl_object_operations::clo_io_init() method for lov
 * layer. Dispatches to the appropriate layout io initialization method.
 */
int lov_io_init(const struct lu_env *env, struct cl_object *obj,
		struct cl_io *io)
{
	CL_IO_SLICE_CLEAN(lov_env_io(env), lis_cl);
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
	 * spin-lock. Layout is protected from changing by ongoing IO. */
	return LOV_2DISPATCH_NOLOCK(cl2lov(obj), llo_getattr, env, obj, attr);
}

static int lov_attr_set(const struct lu_env *env, struct cl_object *obj,
			const struct cl_attr *attr, unsigned valid)
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

static const struct cl_object_operations lov_ops = {
	.coo_page_init = lov_page_init,
	.coo_lock_init = lov_lock_init,
	.coo_io_init   = lov_io_init,
	.coo_attr_get  = lov_attr_get,
	.coo_attr_set  = lov_attr_set,
	.coo_conf_set  = lov_conf_set
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

	OBD_SLAB_ALLOC_PTR_GFP(lov, lov_object_kmem, __GFP_IO);
	if (lov != NULL) {
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
	} else
		obj = NULL;
	return obj;
}

struct lov_stripe_md *lov_lsm_addref(struct lov_object *lov)
{
	struct lov_stripe_md *lsm = NULL;

	lov_conf_freeze(lov);
	if (lov->lo_lsm != NULL) {
		lsm = lsm_addref(lov->lo_lsm);
		CDEBUG(D_INODE, "lsm %p addref %d/%d by %p.\n",
			lsm, atomic_read(&lsm->lsm_refc),
			lov->lo_layout_invalid, current);
	}
	lov_conf_thaw(lov);
	return lsm;
}

void lov_lsm_decref(struct lov_object *lov, struct lov_stripe_md *lsm)
{
	if (lsm == NULL)
		return;

	CDEBUG(D_INODE, "lsm %p decref %d by %p.\n",
		lsm, atomic_read(&lsm->lsm_refc), current);

	lov_free_memmd(&lsm);
}

struct lov_stripe_md *lov_lsm_get(struct cl_object *clobj)
{
	struct lu_object *luobj;
	struct lov_stripe_md *lsm = NULL;

	if (clobj == NULL)
		return NULL;

	luobj = lu_object_locate(&cl_object_header(clobj)->coh_lu,
				 &lov_device_type);
	if (luobj != NULL)
		lsm = lov_lsm_addref(lu2lov(luobj));
	return lsm;
}
EXPORT_SYMBOL(lov_lsm_get);

void lov_lsm_put(struct cl_object *unused, struct lov_stripe_md *lsm)
{
	if (lsm != NULL)
		lov_free_memmd(&lsm);
}
EXPORT_SYMBOL(lov_lsm_put);

int lov_read_and_clear_async_rc(struct cl_object *clob)
{
	struct lu_object *luobj;
	int rc = 0;

	luobj = lu_object_locate(&cl_object_header(clob)->coh_lu,
				 &lov_device_type);
	if (luobj != NULL) {
		struct lov_object *lov = lu2lov(luobj);

		lov_conf_freeze(lov);
		switch (lov->lo_type) {
		case LLT_RAID0: {
			struct lov_stripe_md *lsm;
			int i;

			lsm = lov->lo_lsm;
			LASSERT(lsm != NULL);
			for (i = 0; i < lsm->lsm_stripe_count; i++) {
				struct lov_oinfo *loi = lsm->lsm_oinfo[i];
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
