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
 * Copyright (c) 2012, 2015, Intel Corporation.
 *
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * osc cache management.
 *
 * Author: Jinshan Xiong <jinshan.xiong@whamcloud.com>
 */

#define DEBUG_SUBSYSTEM S_OSC

#include "osc_cl_internal.h"
#include "osc_internal.h"

static int extent_debug; /* set it to be true for more debug */

static void osc_update_pending(struct osc_object *obj, int cmd, int delta);
static int osc_extent_wait(const struct lu_env *env, struct osc_extent *ext,
			   int state);
static void osc_ap_completion(const struct lu_env *env, struct client_obd *cli,
			      struct osc_async_page *oap, int sent, int rc);
static int osc_make_ready(const struct lu_env *env, struct osc_async_page *oap,
			  int cmd);
static int osc_refresh_count(const struct lu_env *env,
			     struct osc_async_page *oap, int cmd);
static int osc_io_unplug_async(const struct lu_env *env,
			       struct client_obd *cli, struct osc_object *osc);
static void osc_free_grant(struct client_obd *cli, unsigned int nr_pages,
			   unsigned int lost_grant);

static void osc_extent_tree_dump0(int level, struct osc_object *obj,
				  const char *func, int line);
#define osc_extent_tree_dump(lvl, obj) \
	osc_extent_tree_dump0(lvl, obj, __func__, __LINE__)

/** \addtogroup osc
 *  @{
 */

/* ------------------ osc extent ------------------ */
static inline char *ext_flags(struct osc_extent *ext, char *flags)
{
	char *buf = flags;
	*buf++ = ext->oe_rw ? 'r' : 'w';
	if (ext->oe_intree)
		*buf++ = 'i';
	if (ext->oe_sync)
		*buf++ = 'S';
	if (ext->oe_srvlock)
		*buf++ = 's';
	if (ext->oe_hp)
		*buf++ = 'h';
	if (ext->oe_urgent)
		*buf++ = 'u';
	if (ext->oe_memalloc)
		*buf++ = 'm';
	if (ext->oe_trunc_pending)
		*buf++ = 't';
	if (ext->oe_fsync_wait)
		*buf++ = 'Y';
	*buf = 0;
	return flags;
}

static inline char list_empty_marker(struct list_head *list)
{
	return list_empty(list) ? '-' : '+';
}

#define EXTSTR       "[%lu -> %lu/%lu]"
#define EXTPARA(ext) (ext)->oe_start, (ext)->oe_end, (ext)->oe_max_end
static const char *oes_strings[] = {
	"inv", "active", "cache", "locking", "lockdone", "rpc", "trunc", NULL };

#define OSC_EXTENT_DUMP(lvl, extent, fmt, ...) do {			      \
	struct osc_extent *__ext = (extent);				      \
	char __buf[16];							      \
									      \
	CDEBUG(lvl,							      \
		"extent %p@{" EXTSTR ", "				      \
		"[%d|%d|%c|%s|%s|%p], [%d|%d|%c|%c|%p|%u|%p]} " fmt,	      \
		/* ----- extent part 0 ----- */				      \
		__ext, EXTPARA(__ext),					      \
		/* ----- part 1 ----- */				      \
		atomic_read(&__ext->oe_refc),				      \
		atomic_read(&__ext->oe_users),				      \
		list_empty_marker(&__ext->oe_link),			      \
		oes_strings[__ext->oe_state], ext_flags(__ext, __buf),	      \
		__ext->oe_obj,						      \
		/* ----- part 2 ----- */				      \
		__ext->oe_grants, __ext->oe_nr_pages,			      \
		list_empty_marker(&__ext->oe_pages),			      \
		waitqueue_active(&__ext->oe_waitq) ? '+' : '-',		      \
		__ext->oe_dlmlock, __ext->oe_mppr, __ext->oe_owner,	      \
		/* ----- part 4 ----- */				      \
		## __VA_ARGS__);					      \
	if (lvl == D_ERROR && __ext->oe_dlmlock)			      \
		LDLM_ERROR(__ext->oe_dlmlock, "extent: %p", __ext);	      \
	else								      \
		LDLM_DEBUG(__ext->oe_dlmlock, "extent: %p", __ext);	      \
} while (0)

#undef EASSERTF
#define EASSERTF(expr, ext, fmt, args...) do {				\
	if (!(expr)) {							\
		OSC_EXTENT_DUMP(D_ERROR, (ext), fmt, ##args);		\
		osc_extent_tree_dump(D_ERROR, (ext)->oe_obj);		\
		LASSERT(expr);						\
	}								\
} while (0)

#undef EASSERT
#define EASSERT(expr, ext) EASSERTF(expr, ext, "\n")

static inline struct osc_extent *rb_extent(struct rb_node *n)
{
	if (!n)
		return NULL;

	return container_of(n, struct osc_extent, oe_node);
}

static inline struct osc_extent *next_extent(struct osc_extent *ext)
{
	if (!ext)
		return NULL;

	LASSERT(ext->oe_intree);
	return rb_extent(rb_next(&ext->oe_node));
}

static inline struct osc_extent *prev_extent(struct osc_extent *ext)
{
	if (!ext)
		return NULL;

	LASSERT(ext->oe_intree);
	return rb_extent(rb_prev(&ext->oe_node));
}

static inline struct osc_extent *first_extent(struct osc_object *obj)
{
	return rb_extent(rb_first(&obj->oo_root));
}

/* object must be locked by caller. */
static int osc_extent_sanity_check0(struct osc_extent *ext,
				    const char *func, const int line)
{
	struct osc_object *obj = ext->oe_obj;
	struct osc_async_page *oap;
	int page_count;
	int rc = 0;

	if (!osc_object_is_locked(obj)) {
		rc = 9;
		goto out;
	}

	if (ext->oe_state >= OES_STATE_MAX) {
		rc = 10;
		goto out;
	}

	if (atomic_read(&ext->oe_refc) <= 0) {
		rc = 20;
		goto out;
	}

	if (atomic_read(&ext->oe_refc) < atomic_read(&ext->oe_users)) {
		rc = 30;
		goto out;
	}

	switch (ext->oe_state) {
	case OES_INV:
		if (ext->oe_nr_pages > 0 || !list_empty(&ext->oe_pages))
			rc = 35;
		else
			rc = 0;
		goto out;
	case OES_ACTIVE:
		if (atomic_read(&ext->oe_users) == 0) {
			rc = 40;
			goto out;
		}
		if (ext->oe_hp) {
			rc = 50;
			goto out;
		}
		if (ext->oe_fsync_wait && !ext->oe_urgent) {
			rc = 55;
			goto out;
		}
		break;
	case OES_CACHE:
		if (ext->oe_grants == 0) {
			rc = 60;
			goto out;
		}
		if (ext->oe_fsync_wait && !ext->oe_urgent && !ext->oe_hp) {
			rc = 65;
			goto out;
		}
	default:
		if (atomic_read(&ext->oe_users) > 0) {
			rc = 70;
			goto out;
		}
	}

	if (ext->oe_max_end < ext->oe_end || ext->oe_end < ext->oe_start) {
		rc = 80;
		goto out;
	}

	if (ext->oe_sync && ext->oe_grants > 0) {
		rc = 90;
		goto out;
	}

	if (ext->oe_dlmlock) {
		struct ldlm_extent *extent;

		extent = &ext->oe_dlmlock->l_policy_data.l_extent;
		if (!(extent->start <= cl_offset(osc2cl(obj), ext->oe_start) &&
		      extent->end >= cl_offset(osc2cl(obj), ext->oe_max_end))) {
			rc = 100;
			goto out;
		}

		if (!(ext->oe_dlmlock->l_granted_mode & (LCK_PW | LCK_GROUP))) {
			rc = 102;
			goto out;
		}
	}

	if (ext->oe_nr_pages > ext->oe_mppr) {
		rc = 105;
		goto out;
	}

	/* Do not verify page list if extent is in RPC. This is because an
	 * in-RPC extent is supposed to be exclusively accessible w/o lock.
	 */
	if (ext->oe_state > OES_CACHE) {
		rc = 0;
		goto out;
	}

	if (!extent_debug) {
		rc = 0;
		goto out;
	}

	page_count = 0;
	list_for_each_entry(oap, &ext->oe_pages, oap_pending_item) {
		pgoff_t index = osc_index(oap2osc(oap));
		++page_count;
		if (index > ext->oe_end || index < ext->oe_start) {
			rc = 110;
			goto out;
		}
	}
	if (page_count != ext->oe_nr_pages) {
		rc = 120;
		goto out;
	}

out:
	if (rc != 0)
		OSC_EXTENT_DUMP(D_ERROR, ext,
				"%s:%d sanity check %p failed with rc = %d\n",
				func, line, ext, rc);
	return rc;
}

#define sanity_check_nolock(ext) \
	osc_extent_sanity_check0(ext, __func__, __LINE__)

#define sanity_check(ext) ({						\
	int __res;							\
	osc_object_lock((ext)->oe_obj);					\
	__res = sanity_check_nolock(ext);				\
	osc_object_unlock((ext)->oe_obj);				\
	__res;								\
})

/**
 * sanity check - to make sure there is no overlapped extent in the tree.
 */
static int osc_extent_is_overlapped(struct osc_object *obj,
				    struct osc_extent *ext)
{
	struct osc_extent *tmp;

	LASSERT(osc_object_is_locked(obj));

	if (!extent_debug)
		return 0;

	for (tmp = first_extent(obj); tmp; tmp = next_extent(tmp)) {
		if (tmp == ext)
			continue;
		if (tmp->oe_end >= ext->oe_start &&
		    tmp->oe_start <= ext->oe_end)
			return 1;
	}
	return 0;
}

static void osc_extent_state_set(struct osc_extent *ext, int state)
{
	LASSERT(osc_object_is_locked(ext->oe_obj));
	LASSERT(state >= OES_INV && state < OES_STATE_MAX);

	/* Never try to sanity check a state changing extent :-) */
	/* LASSERT(sanity_check_nolock(ext) == 0); */

	/* TODO: validate the state machine */
	ext->oe_state = state;
	wake_up_all(&ext->oe_waitq);
}

static struct osc_extent *osc_extent_alloc(struct osc_object *obj)
{
	struct osc_extent *ext;

	ext = kmem_cache_zalloc(osc_extent_kmem, GFP_NOFS);
	if (!ext)
		return NULL;

	RB_CLEAR_NODE(&ext->oe_node);
	ext->oe_obj = obj;
	atomic_set(&ext->oe_refc, 1);
	atomic_set(&ext->oe_users, 0);
	INIT_LIST_HEAD(&ext->oe_link);
	ext->oe_state = OES_INV;
	INIT_LIST_HEAD(&ext->oe_pages);
	init_waitqueue_head(&ext->oe_waitq);
	ext->oe_dlmlock = NULL;

	return ext;
}

static void osc_extent_free(struct osc_extent *ext)
{
	kmem_cache_free(osc_extent_kmem, ext);
}

static struct osc_extent *osc_extent_get(struct osc_extent *ext)
{
	LASSERT(atomic_read(&ext->oe_refc) >= 0);
	atomic_inc(&ext->oe_refc);
	return ext;
}

static void osc_extent_put(const struct lu_env *env, struct osc_extent *ext)
{
	LASSERT(atomic_read(&ext->oe_refc) > 0);
	if (atomic_dec_and_test(&ext->oe_refc)) {
		LASSERT(list_empty(&ext->oe_link));
		LASSERT(atomic_read(&ext->oe_users) == 0);
		LASSERT(ext->oe_state == OES_INV);
		LASSERT(!ext->oe_intree);

		if (ext->oe_dlmlock) {
			lu_ref_add(&ext->oe_dlmlock->l_reference,
				   "osc_extent", ext);
			LDLM_LOCK_PUT(ext->oe_dlmlock);
			ext->oe_dlmlock = NULL;
		}
		osc_extent_free(ext);
	}
}

/**
 * osc_extent_put_trust() is a special version of osc_extent_put() when
 * it's known that the caller is not the last user. This is to address the
 * problem of lacking of lu_env ;-).
 */
static void osc_extent_put_trust(struct osc_extent *ext)
{
	LASSERT(atomic_read(&ext->oe_refc) > 1);
	LASSERT(osc_object_is_locked(ext->oe_obj));
	atomic_dec(&ext->oe_refc);
}

/**
 * Return the extent which includes pgoff @index, or return the greatest
 * previous extent in the tree.
 */
static struct osc_extent *osc_extent_search(struct osc_object *obj,
					    pgoff_t index)
{
	struct rb_node *n = obj->oo_root.rb_node;
	struct osc_extent *tmp, *p = NULL;

	LASSERT(osc_object_is_locked(obj));
	while (n) {
		tmp = rb_extent(n);
		if (index < tmp->oe_start) {
			n = n->rb_left;
		} else if (index > tmp->oe_end) {
			p = rb_extent(n);
			n = n->rb_right;
		} else {
			return tmp;
		}
	}
	return p;
}

/*
 * Return the extent covering @index, otherwise return NULL.
 * caller must have held object lock.
 */
static struct osc_extent *osc_extent_lookup(struct osc_object *obj,
					    pgoff_t index)
{
	struct osc_extent *ext;

	ext = osc_extent_search(obj, index);
	if (ext && ext->oe_start <= index && index <= ext->oe_end)
		return osc_extent_get(ext);
	return NULL;
}

/* caller must have held object lock. */
static void osc_extent_insert(struct osc_object *obj, struct osc_extent *ext)
{
	struct rb_node **n = &obj->oo_root.rb_node;
	struct rb_node *parent = NULL;
	struct osc_extent *tmp;

	LASSERT(ext->oe_intree == 0);
	LASSERT(ext->oe_obj == obj);
	LASSERT(osc_object_is_locked(obj));
	while (*n) {
		tmp = rb_extent(*n);
		parent = *n;

		if (ext->oe_end < tmp->oe_start)
			n = &(*n)->rb_left;
		else if (ext->oe_start > tmp->oe_end)
			n = &(*n)->rb_right;
		else
			EASSERTF(0, tmp, EXTSTR"\n", EXTPARA(ext));
	}
	rb_link_node(&ext->oe_node, parent, n);
	rb_insert_color(&ext->oe_node, &obj->oo_root);
	osc_extent_get(ext);
	ext->oe_intree = 1;
}

/* caller must have held object lock. */
static void osc_extent_erase(struct osc_extent *ext)
{
	struct osc_object *obj = ext->oe_obj;

	LASSERT(osc_object_is_locked(obj));
	if (ext->oe_intree) {
		rb_erase(&ext->oe_node, &obj->oo_root);
		ext->oe_intree = 0;
		/* rbtree held a refcount */
		osc_extent_put_trust(ext);
	}
}

static struct osc_extent *osc_extent_hold(struct osc_extent *ext)
{
	struct osc_object *obj = ext->oe_obj;

	LASSERT(osc_object_is_locked(obj));
	LASSERT(ext->oe_state == OES_ACTIVE || ext->oe_state == OES_CACHE);
	if (ext->oe_state == OES_CACHE) {
		osc_extent_state_set(ext, OES_ACTIVE);
		osc_update_pending(obj, OBD_BRW_WRITE, -ext->oe_nr_pages);
	}
	atomic_inc(&ext->oe_users);
	list_del_init(&ext->oe_link);
	return osc_extent_get(ext);
}

static void __osc_extent_remove(struct osc_extent *ext)
{
	LASSERT(osc_object_is_locked(ext->oe_obj));
	LASSERT(list_empty(&ext->oe_pages));
	osc_extent_erase(ext);
	list_del_init(&ext->oe_link);
	osc_extent_state_set(ext, OES_INV);
	OSC_EXTENT_DUMP(D_CACHE, ext, "destroyed.\n");
}

static void osc_extent_remove(struct osc_extent *ext)
{
	struct osc_object *obj = ext->oe_obj;

	osc_object_lock(obj);
	__osc_extent_remove(ext);
	osc_object_unlock(obj);
}

/**
 * This function is used to merge extents to get better performance. It checks
 * if @cur and @victim are contiguous at chunk level.
 */
static int osc_extent_merge(const struct lu_env *env, struct osc_extent *cur,
			    struct osc_extent *victim)
{
	struct osc_object *obj = cur->oe_obj;
	pgoff_t chunk_start;
	pgoff_t chunk_end;
	int ppc_bits;

	LASSERT(cur->oe_state == OES_CACHE);
	LASSERT(osc_object_is_locked(obj));
	if (!victim)
		return -EINVAL;

	if (victim->oe_state != OES_CACHE || victim->oe_fsync_wait)
		return -EBUSY;

	if (cur->oe_max_end != victim->oe_max_end)
		return -ERANGE;

	LASSERT(cur->oe_dlmlock == victim->oe_dlmlock);
	ppc_bits = osc_cli(obj)->cl_chunkbits - PAGE_SHIFT;
	chunk_start = cur->oe_start >> ppc_bits;
	chunk_end = cur->oe_end >> ppc_bits;
	if (chunk_start != (victim->oe_end >> ppc_bits) + 1 &&
	    chunk_end + 1 != victim->oe_start >> ppc_bits)
		return -ERANGE;

	OSC_EXTENT_DUMP(D_CACHE, victim, "will be merged by %p.\n", cur);

	cur->oe_start = min(cur->oe_start, victim->oe_start);
	cur->oe_end = max(cur->oe_end, victim->oe_end);
	cur->oe_grants += victim->oe_grants;
	cur->oe_nr_pages += victim->oe_nr_pages;
	/* only the following bits are needed to merge */
	cur->oe_urgent |= victim->oe_urgent;
	cur->oe_memalloc |= victim->oe_memalloc;
	list_splice_init(&victim->oe_pages, &cur->oe_pages);
	list_del_init(&victim->oe_link);
	victim->oe_nr_pages = 0;

	osc_extent_get(victim);
	__osc_extent_remove(victim);
	osc_extent_put(env, victim);

	OSC_EXTENT_DUMP(D_CACHE, cur, "after merging %p.\n", victim);
	return 0;
}

/**
 * Drop user count of osc_extent, and unplug IO asynchronously.
 */
void osc_extent_release(const struct lu_env *env, struct osc_extent *ext)
{
	struct osc_object *obj = ext->oe_obj;

	LASSERT(atomic_read(&ext->oe_users) > 0);
	LASSERT(sanity_check(ext) == 0);
	LASSERT(ext->oe_grants > 0);

	if (atomic_dec_and_lock(&ext->oe_users, &obj->oo_lock)) {
		LASSERT(ext->oe_state == OES_ACTIVE);
		if (ext->oe_trunc_pending) {
			/* a truncate process is waiting for this extent.
			 * This may happen due to a race, check
			 * osc_cache_truncate_start().
			 */
			osc_extent_state_set(ext, OES_TRUNC);
			ext->oe_trunc_pending = 0;
		} else {
			osc_extent_state_set(ext, OES_CACHE);
			osc_update_pending(obj, OBD_BRW_WRITE,
					   ext->oe_nr_pages);

			/* try to merge the previous and next extent. */
			osc_extent_merge(env, ext, prev_extent(ext));
			osc_extent_merge(env, ext, next_extent(ext));

			if (ext->oe_urgent)
				list_move_tail(&ext->oe_link,
					       &obj->oo_urgent_exts);
		}
		osc_object_unlock(obj);

		osc_io_unplug_async(env, osc_cli(obj), obj);
	}
	osc_extent_put(env, ext);
}

static inline int overlapped(struct osc_extent *ex1, struct osc_extent *ex2)
{
	return !(ex1->oe_end < ex2->oe_start || ex2->oe_end < ex1->oe_start);
}

/**
 * Find or create an extent which includes @index, core function to manage
 * extent tree.
 */
static struct osc_extent *osc_extent_find(const struct lu_env *env,
					  struct osc_object *obj, pgoff_t index,
					  int *grants)
{
	struct client_obd *cli = osc_cli(obj);
	struct osc_lock   *olck;
	struct cl_lock_descr *descr;
	struct osc_extent *cur;
	struct osc_extent *ext;
	struct osc_extent *conflict = NULL;
	struct osc_extent *found = NULL;
	pgoff_t chunk;
	pgoff_t max_end;
	int max_pages; /* max_pages_per_rpc */
	int chunksize;
	int ppc_bits; /* pages per chunk bits */
	int chunk_mask;
	int rc;

	cur = osc_extent_alloc(obj);
	if (!cur)
		return ERR_PTR(-ENOMEM);

	olck = osc_env_io(env)->oi_write_osclock;
	LASSERTF(olck, "page %lu is not covered by lock\n", index);
	LASSERT(olck->ols_state == OLS_GRANTED);

	descr = &olck->ols_cl.cls_lock->cll_descr;
	LASSERT(descr->cld_mode >= CLM_WRITE);

	LASSERT(cli->cl_chunkbits >= PAGE_SHIFT);
	ppc_bits = cli->cl_chunkbits - PAGE_SHIFT;
	chunk_mask = ~((1 << ppc_bits) - 1);
	chunksize = 1 << cli->cl_chunkbits;
	chunk = index >> ppc_bits;

	/* align end to rpc edge, rpc size may not be a power 2 integer. */
	max_pages = cli->cl_max_pages_per_rpc;
	LASSERT((max_pages & ~chunk_mask) == 0);
	max_end = index - (index % max_pages) + max_pages - 1;
	max_end = min_t(pgoff_t, max_end, descr->cld_end);

	/* initialize new extent by parameters so far */
	cur->oe_max_end = max_end;
	cur->oe_start = index & chunk_mask;
	cur->oe_end = ((index + ~chunk_mask + 1) & chunk_mask) - 1;
	if (cur->oe_start < descr->cld_start)
		cur->oe_start = descr->cld_start;
	if (cur->oe_end > max_end)
		cur->oe_end = max_end;
	cur->oe_grants = 0;
	cur->oe_mppr = max_pages;
	if (olck->ols_dlmlock) {
		LASSERT(olck->ols_hold);
		cur->oe_dlmlock = LDLM_LOCK_GET(olck->ols_dlmlock);
		lu_ref_add(&olck->ols_dlmlock->l_reference, "osc_extent", cur);
	}

	/* grants has been allocated by caller */
	LASSERTF(*grants >= chunksize + cli->cl_extent_tax,
		 "%u/%u/%u.\n", *grants, chunksize, cli->cl_extent_tax);
	LASSERTF((max_end - cur->oe_start) < max_pages, EXTSTR"\n",
		 EXTPARA(cur));

restart:
	osc_object_lock(obj);
	ext = osc_extent_search(obj, cur->oe_start);
	if (!ext)
		ext = first_extent(obj);
	while (ext) {
		loff_t ext_chk_start = ext->oe_start >> ppc_bits;
		loff_t ext_chk_end = ext->oe_end >> ppc_bits;

		LASSERT(sanity_check_nolock(ext) == 0);
		if (chunk > ext_chk_end + 1)
			break;

		/* if covering by different locks, no chance to match */
		if (olck->ols_dlmlock != ext->oe_dlmlock) {
			EASSERTF(!overlapped(ext, cur), ext,
				 EXTSTR"\n", EXTPARA(cur));

			ext = next_extent(ext);
			continue;
		}

		/* discontiguous chunks? */
		if (chunk + 1 < ext_chk_start) {
			ext = next_extent(ext);
			continue;
		}

		/* ok, from now on, ext and cur have these attrs:
		 * 1. covered by the same lock
		 * 2. contiguous at chunk level or overlapping.
		 */

		if (overlapped(ext, cur)) {
			/* cur is the minimum unit, so overlapping means
			 * full contain.
			 */
			EASSERTF((ext->oe_start <= cur->oe_start &&
				  ext->oe_end >= cur->oe_end),
				 ext, EXTSTR"\n", EXTPARA(cur));

			if (ext->oe_state > OES_CACHE || ext->oe_fsync_wait) {
				/* for simplicity, we wait for this extent to
				 * finish before going forward.
				 */
				conflict = osc_extent_get(ext);
				break;
			}

			found = osc_extent_hold(ext);
			break;
		}

		/* non-overlapped extent */
		if (ext->oe_state != OES_CACHE || ext->oe_fsync_wait) {
			/* we can't do anything for a non OES_CACHE extent, or
			 * if there is someone waiting for this extent to be
			 * flushed, try next one.
			 */
			ext = next_extent(ext);
			continue;
		}

		/* check if they belong to the same rpc slot before trying to
		 * merge. the extents are not overlapped and contiguous at
		 * chunk level to get here.
		 */
		if (ext->oe_max_end != max_end) {
			/* if they don't belong to the same RPC slot or
			 * max_pages_per_rpc has ever changed, do not merge.
			 */
			ext = next_extent(ext);
			continue;
		}

		/* it's required that an extent must be contiguous at chunk
		 * level so that we know the whole extent is covered by grant
		 * (the pages in the extent are NOT required to be contiguous).
		 * Otherwise, it will be too much difficult to know which
		 * chunks have grants allocated.
		 */

		/* try to do front merge - extend ext's start */
		if (chunk + 1 == ext_chk_start) {
			/* ext must be chunk size aligned */
			EASSERT((ext->oe_start & ~chunk_mask) == 0, ext);

			/* pull ext's start back to cover cur */
			ext->oe_start = cur->oe_start;
			ext->oe_grants += chunksize;
			*grants -= chunksize;

			found = osc_extent_hold(ext);
		} else if (chunk == ext_chk_end + 1) {
			/* rear merge */
			ext->oe_end = cur->oe_end;
			ext->oe_grants += chunksize;
			*grants -= chunksize;

			/* try to merge with the next one because we just fill
			 * in a gap
			 */
			if (osc_extent_merge(env, ext, next_extent(ext)) == 0)
				/* we can save extent tax from next extent */
				*grants += cli->cl_extent_tax;

			found = osc_extent_hold(ext);
		}
		if (found)
			break;

		ext = next_extent(ext);
	}

	osc_extent_tree_dump(D_CACHE, obj);
	if (found) {
		LASSERT(!conflict);
		if (!IS_ERR(found)) {
			LASSERT(found->oe_dlmlock == cur->oe_dlmlock);
			OSC_EXTENT_DUMP(D_CACHE, found,
					"found caching ext for %lu.\n", index);
		}
	} else if (!conflict) {
		/* create a new extent */
		EASSERT(osc_extent_is_overlapped(obj, cur) == 0, cur);
		cur->oe_grants = chunksize + cli->cl_extent_tax;
		*grants -= cur->oe_grants;
		LASSERT(*grants >= 0);

		cur->oe_state = OES_CACHE;
		found = osc_extent_hold(cur);
		osc_extent_insert(obj, cur);
		OSC_EXTENT_DUMP(D_CACHE, cur, "add into tree %lu/%lu.\n",
				index, descr->cld_end);
	}
	osc_object_unlock(obj);

	if (conflict) {
		LASSERT(!found);

		/* waiting for IO to finish. Please notice that it's impossible
		 * to be an OES_TRUNC extent.
		 */
		rc = osc_extent_wait(env, conflict, OES_INV);
		osc_extent_put(env, conflict);
		conflict = NULL;
		if (rc < 0) {
			found = ERR_PTR(rc);
			goto out;
		}

		goto restart;
	}

out:
	osc_extent_put(env, cur);
	LASSERT(*grants >= 0);
	return found;
}

/**
 * Called when IO is finished to an extent.
 */
int osc_extent_finish(const struct lu_env *env, struct osc_extent *ext,
		      int sent, int rc)
{
	struct client_obd *cli = osc_cli(ext->oe_obj);
	struct osc_async_page *oap;
	struct osc_async_page *tmp;
	int nr_pages = ext->oe_nr_pages;
	int lost_grant = 0;
	int blocksize = cli->cl_import->imp_obd->obd_osfs.os_bsize ? : 4096;
	__u64 last_off = 0;
	int last_count = -1;

	OSC_EXTENT_DUMP(D_CACHE, ext, "extent finished.\n");

	ext->oe_rc = rc ?: ext->oe_nr_pages;
	EASSERT(ergo(rc == 0, ext->oe_state == OES_RPC), ext);

	osc_lru_add_batch(cli, &ext->oe_pages);
	list_for_each_entry_safe(oap, tmp, &ext->oe_pages, oap_pending_item) {
		list_del_init(&oap->oap_rpc_item);
		list_del_init(&oap->oap_pending_item);
		if (last_off <= oap->oap_obj_off) {
			last_off = oap->oap_obj_off;
			last_count = oap->oap_count;
		}

		--ext->oe_nr_pages;
		osc_ap_completion(env, cli, oap, sent, rc);
	}
	EASSERT(ext->oe_nr_pages == 0, ext);

	if (!sent) {
		lost_grant = ext->oe_grants;
	} else if (blocksize < PAGE_SIZE &&
		   last_count != PAGE_SIZE) {
		/* For short writes we shouldn't count parts of pages that
		 * span a whole chunk on the OST side, or our accounting goes
		 * wrong.  Should match the code in filter_grant_check.
		 */
		int offset = last_off & ~PAGE_MASK;
		int count = last_count + (offset & (blocksize - 1));
		int end = (offset + last_count) & (blocksize - 1);
		if (end)
			count += blocksize - end;

		lost_grant = PAGE_SIZE - count;
	}
	if (ext->oe_grants > 0)
		osc_free_grant(cli, nr_pages, lost_grant);

	osc_extent_remove(ext);
	/* put the refcount for RPC */
	osc_extent_put(env, ext);
	return 0;
}

static int extent_wait_cb(struct osc_extent *ext, int state)
{
	int ret;

	osc_object_lock(ext->oe_obj);
	ret = ext->oe_state == state;
	osc_object_unlock(ext->oe_obj);

	return ret;
}

/**
 * Wait for the extent's state to become @state.
 */
static int osc_extent_wait(const struct lu_env *env, struct osc_extent *ext,
			   int state)
{
	struct osc_object *obj = ext->oe_obj;
	struct l_wait_info lwi = LWI_TIMEOUT_INTR(cfs_time_seconds(600), NULL,
						  LWI_ON_SIGNAL_NOOP, NULL);
	int rc = 0;

	osc_object_lock(obj);
	LASSERT(sanity_check_nolock(ext) == 0);
	/* `Kick' this extent only if the caller is waiting for it to be
	 * written out.
	 */
	if (state == OES_INV && !ext->oe_urgent && !ext->oe_hp &&
	    !ext->oe_trunc_pending) {
		if (ext->oe_state == OES_ACTIVE) {
			ext->oe_urgent = 1;
		} else if (ext->oe_state == OES_CACHE) {
			ext->oe_urgent = 1;
			osc_extent_hold(ext);
			rc = 1;
		}
	}
	osc_object_unlock(obj);
	if (rc == 1)
		osc_extent_release(env, ext);

	/* wait for the extent until its state becomes @state */
	rc = l_wait_event(ext->oe_waitq, extent_wait_cb(ext, state), &lwi);
	if (rc == -ETIMEDOUT) {
		OSC_EXTENT_DUMP(D_ERROR, ext,
			"%s: wait ext to %d timedout, recovery in progress?\n",
			osc_export(obj)->exp_obd->obd_name, state);

		lwi = LWI_INTR(NULL, NULL);
		rc = l_wait_event(ext->oe_waitq, extent_wait_cb(ext, state),
				  &lwi);
	}
	if (rc == 0 && ext->oe_rc < 0)
		rc = ext->oe_rc;
	return rc;
}

/**
 * Discard pages with index greater than @size. If @ext is overlapped with
 * @size, then partial truncate happens.
 */
static int osc_extent_truncate(struct osc_extent *ext, pgoff_t trunc_index,
			       bool partial)
{
	struct cl_env_nest nest;
	struct lu_env *env;
	struct cl_io *io;
	struct osc_object *obj = ext->oe_obj;
	struct client_obd *cli = osc_cli(obj);
	struct osc_async_page *oap;
	struct osc_async_page *tmp;
	int pages_in_chunk = 0;
	int ppc_bits = cli->cl_chunkbits - PAGE_SHIFT;
	__u64 trunc_chunk = trunc_index >> ppc_bits;
	int grants = 0;
	int nr_pages = 0;
	int rc = 0;

	LASSERT(sanity_check(ext) == 0);
	EASSERT(ext->oe_state == OES_TRUNC, ext);
	EASSERT(!ext->oe_urgent, ext);

	/* Request new lu_env.
	 * We can't use that env from osc_cache_truncate_start() because
	 * it's from lov_io_sub and not fully initialized.
	 */
	env = cl_env_nested_get(&nest);
	io  = &osc_env_info(env)->oti_io;
	io->ci_obj = cl_object_top(osc2cl(obj));
	rc = cl_io_init(env, io, CIT_MISC, io->ci_obj);
	if (rc < 0)
		goto out;

	/* discard all pages with index greater then trunc_index */
	list_for_each_entry_safe(oap, tmp, &ext->oe_pages, oap_pending_item) {
		pgoff_t index = osc_index(oap2osc(oap));
		struct cl_page *page = oap2cl_page(oap);

		LASSERT(list_empty(&oap->oap_rpc_item));

		/* only discard the pages with their index greater than
		 * trunc_index, and ...
		 */
		if (index < trunc_index ||
		    (index == trunc_index && partial)) {
			/* accounting how many pages remaining in the chunk
			 * so that we can calculate grants correctly. */
			if (index >> ppc_bits == trunc_chunk)
				++pages_in_chunk;
			continue;
		}

		list_del_init(&oap->oap_pending_item);

		cl_page_get(page);
		lu_ref_add(&page->cp_reference, "truncate", current);

		if (cl_page_own(env, io, page) == 0) {
			cl_page_discard(env, io, page);
			cl_page_disown(env, io, page);
		} else {
			LASSERT(page->cp_state == CPS_FREEING);
			LASSERT(0);
		}

		lu_ref_del(&page->cp_reference, "truncate", current);
		cl_page_put(env, page);

		--ext->oe_nr_pages;
		++nr_pages;
	}
	EASSERTF(ergo(ext->oe_start >= trunc_index + !!partial,
		      ext->oe_nr_pages == 0),
		ext, "trunc_index %lu, partial %d\n", trunc_index, partial);

	osc_object_lock(obj);
	if (ext->oe_nr_pages == 0) {
		LASSERT(pages_in_chunk == 0);
		grants = ext->oe_grants;
		ext->oe_grants = 0;
	} else { /* calculate how many grants we can free */
		int chunks = (ext->oe_end >> ppc_bits) - trunc_chunk;
		pgoff_t last_index;

		/* if there is no pages in this chunk, we can also free grants
		 * for the last chunk
		 */
		if (pages_in_chunk == 0) {
			/* if this is the 1st chunk and no pages in this chunk,
			 * ext->oe_nr_pages must be zero, so we should be in
			 * the other if-clause.
			 */
			LASSERT(trunc_chunk > 0);
			--trunc_chunk;
			++chunks;
		}

		/* this is what we can free from this extent */
		grants = chunks << cli->cl_chunkbits;
		ext->oe_grants -= grants;
		last_index = ((trunc_chunk + 1) << ppc_bits) - 1;
		ext->oe_end = min(last_index, ext->oe_max_end);
		LASSERT(ext->oe_end >= ext->oe_start);
		LASSERT(ext->oe_grants > 0);
	}
	osc_object_unlock(obj);

	if (grants > 0 || nr_pages > 0)
		osc_free_grant(cli, nr_pages, grants);

out:
	cl_io_fini(env, io);
	cl_env_nested_put(&nest, env);
	return rc;
}

/**
 * This function is used to make the extent prepared for transfer.
 * A race with flushing page - ll_writepage() has to be handled cautiously.
 */
static int osc_extent_make_ready(const struct lu_env *env,
				 struct osc_extent *ext)
{
	struct osc_async_page *oap;
	struct osc_async_page *last = NULL;
	struct osc_object *obj = ext->oe_obj;
	int page_count = 0;
	int rc;

	/* we're going to grab page lock, so object lock must not be taken. */
	LASSERT(sanity_check(ext) == 0);
	/* in locking state, any process should not touch this extent. */
	EASSERT(ext->oe_state == OES_LOCKING, ext);
	EASSERT(ext->oe_owner, ext);

	OSC_EXTENT_DUMP(D_CACHE, ext, "make ready\n");

	list_for_each_entry(oap, &ext->oe_pages, oap_pending_item) {
		++page_count;
		if (!last || last->oap_obj_off < oap->oap_obj_off)
			last = oap;

		/* checking ASYNC_READY is race safe */
		if ((oap->oap_async_flags & ASYNC_READY) != 0)
			continue;

		rc = osc_make_ready(env, oap, OBD_BRW_WRITE);
		switch (rc) {
		case 0:
			spin_lock(&oap->oap_lock);
			oap->oap_async_flags |= ASYNC_READY;
			spin_unlock(&oap->oap_lock);
			break;
		case -EALREADY:
			LASSERT((oap->oap_async_flags & ASYNC_READY) != 0);
			break;
		default:
			LASSERTF(0, "unknown return code: %d\n", rc);
		}
	}

	LASSERT(page_count == ext->oe_nr_pages);
	LASSERT(last);
	/* the last page is the only one we need to refresh its count by
	 * the size of file.
	 */
	if (!(last->oap_async_flags & ASYNC_COUNT_STABLE)) {
		last->oap_count = osc_refresh_count(env, last, OBD_BRW_WRITE);
		LASSERT(last->oap_count > 0);
		LASSERT(last->oap_page_off + last->oap_count <= PAGE_SIZE);
		spin_lock(&last->oap_lock);
		last->oap_async_flags |= ASYNC_COUNT_STABLE;
		spin_unlock(&last->oap_lock);
	}

	/* for the rest of pages, we don't need to call osf_refresh_count()
	 * because it's known they are not the last page
	 */
	list_for_each_entry(oap, &ext->oe_pages, oap_pending_item) {
		if (!(oap->oap_async_flags & ASYNC_COUNT_STABLE)) {
			oap->oap_count = PAGE_SIZE - oap->oap_page_off;
			spin_lock(&last->oap_lock);
			oap->oap_async_flags |= ASYNC_COUNT_STABLE;
			spin_unlock(&last->oap_lock);
		}
	}

	osc_object_lock(obj);
	osc_extent_state_set(ext, OES_RPC);
	osc_object_unlock(obj);
	/* get a refcount for RPC. */
	osc_extent_get(ext);

	return 0;
}

/**
 * Quick and simple version of osc_extent_find(). This function is frequently
 * called to expand the extent for the same IO. To expand the extent, the
 * page index must be in the same or next chunk of ext->oe_end.
 */
static int osc_extent_expand(struct osc_extent *ext, pgoff_t index, int *grants)
{
	struct osc_object *obj = ext->oe_obj;
	struct client_obd *cli = osc_cli(obj);
	struct osc_extent *next;
	int ppc_bits = cli->cl_chunkbits - PAGE_SHIFT;
	pgoff_t chunk = index >> ppc_bits;
	pgoff_t end_chunk;
	pgoff_t end_index;
	int chunksize = 1 << cli->cl_chunkbits;
	int rc = 0;

	LASSERT(ext->oe_max_end >= index && ext->oe_start <= index);
	osc_object_lock(obj);
	LASSERT(sanity_check_nolock(ext) == 0);
	end_chunk = ext->oe_end >> ppc_bits;
	if (chunk > end_chunk + 1) {
		rc = -ERANGE;
		goto out;
	}

	if (end_chunk >= chunk) {
		rc = 0;
		goto out;
	}

	LASSERT(end_chunk + 1 == chunk);
	/* try to expand this extent to cover @index */
	end_index = min(ext->oe_max_end, ((chunk + 1) << ppc_bits) - 1);

	next = next_extent(ext);
	if (next && next->oe_start <= end_index) {
		/* complex mode - overlapped with the next extent,
		 * this case will be handled by osc_extent_find()
		 */
		rc = -EAGAIN;
		goto out;
	}

	ext->oe_end = end_index;
	ext->oe_grants += chunksize;
	*grants -= chunksize;
	LASSERT(*grants >= 0);
	EASSERTF(osc_extent_is_overlapped(obj, ext) == 0, ext,
		 "overlapped after expanding for %lu.\n", index);

out:
	osc_object_unlock(obj);
	return rc;
}

static void osc_extent_tree_dump0(int level, struct osc_object *obj,
				  const char *func, int line)
{
	struct osc_extent *ext;
	int cnt;

	CDEBUG(level, "Dump object %p extents at %s:%d, mppr: %u.\n",
	       obj, func, line, osc_cli(obj)->cl_max_pages_per_rpc);

	/* osc_object_lock(obj); */
	cnt = 1;
	for (ext = first_extent(obj); ext; ext = next_extent(ext))
		OSC_EXTENT_DUMP(level, ext, "in tree %d.\n", cnt++);

	cnt = 1;
	list_for_each_entry(ext, &obj->oo_hp_exts, oe_link)
		OSC_EXTENT_DUMP(level, ext, "hp %d.\n", cnt++);

	cnt = 1;
	list_for_each_entry(ext, &obj->oo_urgent_exts, oe_link)
		OSC_EXTENT_DUMP(level, ext, "urgent %d.\n", cnt++);

	cnt = 1;
	list_for_each_entry(ext, &obj->oo_reading_exts, oe_link)
		OSC_EXTENT_DUMP(level, ext, "reading %d.\n", cnt++);
	/* osc_object_unlock(obj); */
}

/* ------------------ osc extent end ------------------ */

static inline int osc_is_ready(struct osc_object *osc)
{
	return !list_empty(&osc->oo_ready_item) ||
	       !list_empty(&osc->oo_hp_ready_item);
}

#define OSC_IO_DEBUG(OSC, STR, args...)					       \
	CDEBUG(D_CACHE, "obj %p ready %d|%c|%c wr %d|%c|%c rd %d|%c " STR,     \
	       (OSC), osc_is_ready(OSC),				       \
	       list_empty_marker(&(OSC)->oo_hp_ready_item),		       \
	       list_empty_marker(&(OSC)->oo_ready_item),		       \
	       atomic_read(&(OSC)->oo_nr_writes),			       \
	       list_empty_marker(&(OSC)->oo_hp_exts),			       \
	       list_empty_marker(&(OSC)->oo_urgent_exts),		       \
	       atomic_read(&(OSC)->oo_nr_reads),			       \
	       list_empty_marker(&(OSC)->oo_reading_exts),		       \
	       ##args)

static int osc_make_ready(const struct lu_env *env, struct osc_async_page *oap,
			  int cmd)
{
	struct osc_page *opg = oap2osc_page(oap);
	struct cl_page  *page = oap2cl_page(oap);
	int result;

	LASSERT(cmd == OBD_BRW_WRITE); /* no cached reads */

	result = cl_page_make_ready(env, page, CRT_WRITE);
	if (result == 0)
		opg->ops_submit_time = cfs_time_current();
	return result;
}

static int osc_refresh_count(const struct lu_env *env,
			     struct osc_async_page *oap, int cmd)
{
	struct osc_page *opg = oap2osc_page(oap);
	pgoff_t index = osc_index(oap2osc(oap));
	struct cl_object *obj;
	struct cl_attr *attr = &osc_env_info(env)->oti_attr;

	int result;
	loff_t kms;

	/* readpage queues with _COUNT_STABLE, shouldn't get here. */
	LASSERT(!(cmd & OBD_BRW_READ));
	obj = opg->ops_cl.cpl_obj;

	cl_object_attr_lock(obj);
	result = cl_object_attr_get(env, obj, attr);
	cl_object_attr_unlock(obj);
	if (result < 0)
		return result;
	kms = attr->cat_kms;
	if (cl_offset(obj, index) >= kms)
		/* catch race with truncate */
		return 0;
	else if (cl_offset(obj, index + 1) > kms)
		/* catch sub-page write at end of file */
		return kms % PAGE_SIZE;
	else
		return PAGE_SIZE;
}

static int osc_completion(const struct lu_env *env, struct osc_async_page *oap,
			  int cmd, int rc)
{
	struct osc_page *opg = oap2osc_page(oap);
	struct cl_page    *page = oap2cl_page(oap);
	struct osc_object *obj = cl2osc(opg->ops_cl.cpl_obj);
	enum cl_req_type crt;
	int srvlock;

	cmd &= ~OBD_BRW_NOQUOTA;
	LASSERTF(equi(page->cp_state == CPS_PAGEIN, cmd == OBD_BRW_READ),
		 "cp_state:%u, cmd:%d\n", page->cp_state, cmd);
	LASSERTF(equi(page->cp_state == CPS_PAGEOUT, cmd == OBD_BRW_WRITE),
		 "cp_state:%u, cmd:%d\n", page->cp_state, cmd);
	LASSERT(opg->ops_transfer_pinned);

	/*
	 * page->cp_req can be NULL if io submission failed before
	 * cl_req was allocated.
	 */
	if (page->cp_req)
		cl_req_page_done(env, page);
	LASSERT(!page->cp_req);

	crt = cmd == OBD_BRW_READ ? CRT_READ : CRT_WRITE;
	/* Clear opg->ops_transfer_pinned before VM lock is released. */
	opg->ops_transfer_pinned = 0;

	spin_lock(&obj->oo_seatbelt);
	LASSERT(opg->ops_submitter);
	LASSERT(!list_empty(&opg->ops_inflight));
	list_del_init(&opg->ops_inflight);
	opg->ops_submitter = NULL;
	spin_unlock(&obj->oo_seatbelt);

	opg->ops_submit_time = 0;
	srvlock = oap->oap_brw_flags & OBD_BRW_SRVLOCK;

	/* statistic */
	if (rc == 0 && srvlock) {
		struct lu_device *ld = opg->ops_cl.cpl_obj->co_lu.lo_dev;
		struct osc_stats *stats = &lu2osc_dev(ld)->od_stats;
		int bytes = oap->oap_count;

		if (crt == CRT_READ)
			stats->os_lockless_reads += bytes;
		else
			stats->os_lockless_writes += bytes;
	}

	/*
	 * This has to be the last operation with the page, as locks are
	 * released in cl_page_completion() and nothing except for the
	 * reference counter protects page from concurrent reclaim.
	 */
	lu_ref_del(&page->cp_reference, "transfer", page);

	cl_page_completion(env, page, crt, rc);

	return 0;
}

#define OSC_DUMP_GRANT(lvl, cli, fmt, args...) do {			      \
	struct client_obd *__tmp = (cli);				      \
	CDEBUG(lvl, "%s: grant { dirty: %ld/%ld dirty_pages: %d/%d "	      \
	       "dropped: %ld avail: %ld, reserved: %ld, flight: %d }"	      \
	       "lru {in list: %d, left: %d, waiters: %d }" fmt,		      \
	       __tmp->cl_import->imp_obd->obd_name,			      \
	       __tmp->cl_dirty_pages, __tmp->cl_dirty_max_pages,	      \
	       atomic_read(&obd_dirty_pages), obd_max_dirty_pages,	      \
	       __tmp->cl_lost_grant, __tmp->cl_avail_grant,		      \
	       __tmp->cl_reserved_grant, __tmp->cl_w_in_flight,		      \
	       atomic_read(&__tmp->cl_lru_in_list),			      \
	       atomic_read(&__tmp->cl_lru_busy),			      \
	       atomic_read(&__tmp->cl_lru_shrinkers), ##args);		      \
} while (0)

/* caller must hold loi_list_lock */
static void osc_consume_write_grant(struct client_obd *cli,
				    struct brw_page *pga)
{
	assert_spin_locked(&cli->cl_loi_list_lock);
	LASSERT(!(pga->flag & OBD_BRW_FROM_GRANT));
	atomic_inc(&obd_dirty_pages);
	cli->cl_dirty_pages++;
	pga->flag |= OBD_BRW_FROM_GRANT;
	CDEBUG(D_CACHE, "using %lu grant credits for brw %p page %p\n",
	       PAGE_SIZE, pga, pga->pg);
	osc_update_next_shrink(cli);
}

/* the companion to osc_consume_write_grant, called when a brw has completed.
 * must be called with the loi lock held.
 */
static void osc_release_write_grant(struct client_obd *cli,
				    struct brw_page *pga)
{
	assert_spin_locked(&cli->cl_loi_list_lock);
	if (!(pga->flag & OBD_BRW_FROM_GRANT)) {
		return;
	}

	pga->flag &= ~OBD_BRW_FROM_GRANT;
	atomic_dec(&obd_dirty_pages);
	cli->cl_dirty_pages--;
	if (pga->flag & OBD_BRW_NOCACHE) {
		pga->flag &= ~OBD_BRW_NOCACHE;
		atomic_dec(&obd_dirty_transit_pages);
		cli->cl_dirty_transit--;
	}
}

/**
 * To avoid sleeping with object lock held, it's good for us allocate enough
 * grants before entering into critical section.
 *
 * spin_lock held by caller
 */
static int osc_reserve_grant(struct client_obd *cli, unsigned int bytes)
{
	int rc = -EDQUOT;

	if (cli->cl_avail_grant >= bytes) {
		cli->cl_avail_grant -= bytes;
		cli->cl_reserved_grant += bytes;
		rc = 0;
	}
	return rc;
}

static void __osc_unreserve_grant(struct client_obd *cli,
				  unsigned int reserved, unsigned int unused)
{
	/* it's quite normal for us to get more grant than reserved.
	 * Thinking about a case that two extents merged by adding a new
	 * chunk, we can save one extent tax. If extent tax is greater than
	 * one chunk, we can save more grant by adding a new chunk
	 */
	cli->cl_reserved_grant -= reserved;
	if (unused > reserved) {
		cli->cl_avail_grant += reserved;
		cli->cl_lost_grant  += unused - reserved;
	} else {
		cli->cl_avail_grant += unused;
	}
}

static void osc_unreserve_grant(struct client_obd *cli,
				unsigned int reserved, unsigned int unused)
{
	spin_lock(&cli->cl_loi_list_lock);
	__osc_unreserve_grant(cli, reserved, unused);
	if (unused > 0)
		osc_wake_cache_waiters(cli);
	spin_unlock(&cli->cl_loi_list_lock);
}

/**
 * Free grant after IO is finished or canceled.
 *
 * @lost_grant is used to remember how many grants we have allocated but not
 * used, we should return these grants to OST. There're two cases where grants
 * can be lost:
 * 1. truncate;
 * 2. blocksize at OST is less than PAGE_SIZE and a partial page was
 *    written. In this case OST may use less chunks to serve this partial
 *    write. OSTs don't actually know the page size on the client side. so
 *    clients have to calculate lost grant by the blocksize on the OST.
 *    See filter_grant_check() for details.
 */
static void osc_free_grant(struct client_obd *cli, unsigned int nr_pages,
			   unsigned int lost_grant)
{
	int grant = (1 << cli->cl_chunkbits) + cli->cl_extent_tax;

	spin_lock(&cli->cl_loi_list_lock);
	atomic_sub(nr_pages, &obd_dirty_pages);
	cli->cl_dirty_pages -= nr_pages;
	cli->cl_lost_grant += lost_grant;
	if (cli->cl_avail_grant < grant && cli->cl_lost_grant >= grant) {
		/* borrow some grant from truncate to avoid the case that
		 * truncate uses up all avail grant
		 */
		cli->cl_lost_grant -= grant;
		cli->cl_avail_grant += grant;
	}
	osc_wake_cache_waiters(cli);
	spin_unlock(&cli->cl_loi_list_lock);
	CDEBUG(D_CACHE, "lost %u grant: %lu avail: %lu dirty: %lu\n",
	       lost_grant, cli->cl_lost_grant,
	       cli->cl_avail_grant, cli->cl_dirty_pages << PAGE_SHIFT);
}

/**
 * The companion to osc_enter_cache(), called when @oap is no longer part of
 * the dirty accounting due to error.
 */
static void osc_exit_cache(struct client_obd *cli, struct osc_async_page *oap)
{
	spin_lock(&cli->cl_loi_list_lock);
	osc_release_write_grant(cli, &oap->oap_brw_page);
	spin_unlock(&cli->cl_loi_list_lock);
}

/**
 * Non-blocking version of osc_enter_cache() that consumes grant only when it
 * is available.
 */
static int osc_enter_cache_try(struct client_obd *cli,
			       struct osc_async_page *oap,
			       int bytes, int transient)
{
	int rc;

	OSC_DUMP_GRANT(D_CACHE, cli, "need:%d.\n", bytes);

	rc = osc_reserve_grant(cli, bytes);
	if (rc < 0)
		return 0;

	if (cli->cl_dirty_pages <= cli->cl_dirty_max_pages &&
	    atomic_read(&obd_dirty_pages) + 1 <= obd_max_dirty_pages) {
		osc_consume_write_grant(cli, &oap->oap_brw_page);
		if (transient) {
			cli->cl_dirty_transit++;
			atomic_inc(&obd_dirty_transit_pages);
			oap->oap_brw_flags |= OBD_BRW_NOCACHE;
		}
		rc = 1;
	} else {
		__osc_unreserve_grant(cli, bytes, bytes);
		rc = 0;
	}
	return rc;
}

static int ocw_granted(struct client_obd *cli, struct osc_cache_waiter *ocw)
{
	int rc;

	spin_lock(&cli->cl_loi_list_lock);
	rc = list_empty(&ocw->ocw_entry);
	spin_unlock(&cli->cl_loi_list_lock);
	return rc;
}

/**
 * The main entry to reserve dirty page accounting. Usually the grant reserved
 * in this function will be freed in bulk in osc_free_grant() unless it fails
 * to add osc cache, in that case, it will be freed in osc_exit_cache().
 *
 * The process will be put into sleep if it's already run out of grant.
 */
static int osc_enter_cache(const struct lu_env *env, struct client_obd *cli,
			   struct osc_async_page *oap, int bytes)
{
	struct osc_object *osc = oap->oap_obj;
	struct lov_oinfo *loi = osc->oo_oinfo;
	struct osc_cache_waiter ocw;
	struct l_wait_info lwi = LWI_TIMEOUT_INTR(cfs_time_seconds(600), NULL,
						  LWI_ON_SIGNAL_NOOP, NULL);
	int rc = -EDQUOT;

	OSC_DUMP_GRANT(D_CACHE, cli, "need:%d.\n", bytes);

	spin_lock(&cli->cl_loi_list_lock);

	/* force the caller to try sync io.  this can jump the list
	 * of queued writes and create a discontiguous rpc stream
	 */
	if (OBD_FAIL_CHECK(OBD_FAIL_OSC_NO_GRANT) ||
	    !cli->cl_dirty_max_pages || cli->cl_ar.ar_force_sync ||
	    loi->loi_ar.ar_force_sync) {
		rc = -EDQUOT;
		goto out;
	}

	/* Hopefully normal case - cache space and write credits available */
	if (osc_enter_cache_try(cli, oap, bytes, 0)) {
		rc = 0;
		goto out;
	}

	/* We can get here for two reasons: too many dirty pages in cache, or
	 * run out of grants. In both cases we should write dirty pages out.
	 * Adding a cache waiter will trigger urgent write-out no matter what
	 * RPC size will be.
	 * The exiting condition is no avail grants and no dirty pages caching,
	 * that really means there is no space on the OST.
	 */
	init_waitqueue_head(&ocw.ocw_waitq);
	ocw.ocw_oap   = oap;
	ocw.ocw_grant = bytes;
	while (cli->cl_dirty_pages > 0 || cli->cl_w_in_flight > 0) {
		list_add_tail(&ocw.ocw_entry, &cli->cl_cache_waiters);
		ocw.ocw_rc = 0;
		spin_unlock(&cli->cl_loi_list_lock);

		osc_io_unplug_async(env, cli, NULL);

		CDEBUG(D_CACHE, "%s: sleeping for cache space @ %p for %p\n",
		       cli->cl_import->imp_obd->obd_name, &ocw, oap);

		rc = l_wait_event(ocw.ocw_waitq, ocw_granted(cli, &ocw), &lwi);

		spin_lock(&cli->cl_loi_list_lock);

		/* l_wait_event is interrupted by signal, or timed out */
		if (rc < 0) {
			if (rc == -ETIMEDOUT) {
				OSC_DUMP_GRANT(D_ERROR, cli,
					       "try to reserve %d.\n", bytes);
				osc_extent_tree_dump(D_ERROR, osc);
				rc = -EDQUOT;
			}

			list_del_init(&ocw.ocw_entry);
			goto out;
		}

		LASSERT(list_empty(&ocw.ocw_entry));
		rc = ocw.ocw_rc;

		if (rc != -EDQUOT)
			goto out;
		if (osc_enter_cache_try(cli, oap, bytes, 0)) {
			rc = 0;
			goto out;
		}
	}
out:
	spin_unlock(&cli->cl_loi_list_lock);
	OSC_DUMP_GRANT(D_CACHE, cli, "returned %d.\n", rc);
	return rc;
}

/* caller must hold loi_list_lock */
void osc_wake_cache_waiters(struct client_obd *cli)
{
	struct list_head *l, *tmp;
	struct osc_cache_waiter *ocw;

	list_for_each_safe(l, tmp, &cli->cl_cache_waiters) {
		ocw = list_entry(l, struct osc_cache_waiter, ocw_entry);
		list_del_init(&ocw->ocw_entry);

		ocw->ocw_rc = -EDQUOT;
		/* we can't dirty more */
		if ((cli->cl_dirty_pages > cli->cl_dirty_max_pages) ||
		    (atomic_read(&obd_dirty_pages) + 1 > obd_max_dirty_pages)) {
			CDEBUG(D_CACHE, "no dirty room: dirty: %ld osc max %ld, sys max %d\n",
			       cli->cl_dirty_pages, cli->cl_dirty_max_pages,
			       obd_max_dirty_pages);
			goto wakeup;
		}

		ocw->ocw_rc = 0;
		if (!osc_enter_cache_try(cli, ocw->ocw_oap, ocw->ocw_grant, 0))
			ocw->ocw_rc = -EDQUOT;

wakeup:
		CDEBUG(D_CACHE, "wake up %p for oap %p, avail grant %ld, %d\n",
		       ocw, ocw->ocw_oap, cli->cl_avail_grant, ocw->ocw_rc);

		wake_up(&ocw->ocw_waitq);
	}
}

static int osc_max_rpc_in_flight(struct client_obd *cli, struct osc_object *osc)
{
	int hprpc = !!list_empty(&osc->oo_hp_exts);

	return rpcs_in_flight(cli) >= cli->cl_max_rpcs_in_flight + hprpc;
}

/* This maintains the lists of pending pages to read/write for a given object
 * (lop).  This is used by osc_check_rpcs->osc_next_obj() and osc_list_maint()
 * to quickly find objects that are ready to send an RPC.
 */
static int osc_makes_rpc(struct client_obd *cli, struct osc_object *osc,
			 int cmd)
{
	int invalid_import = 0;

	/* if we have an invalid import we want to drain the queued pages
	 * by forcing them through rpcs that immediately fail and complete
	 * the pages.  recovery relies on this to empty the queued pages
	 * before canceling the locks and evicting down the llite pages
	 */
	if (!cli->cl_import || cli->cl_import->imp_invalid)
		invalid_import = 1;

	if (cmd & OBD_BRW_WRITE) {
		if (atomic_read(&osc->oo_nr_writes) == 0)
			return 0;
		if (invalid_import) {
			CDEBUG(D_CACHE, "invalid import forcing RPC\n");
			return 1;
		}
		if (!list_empty(&osc->oo_hp_exts)) {
			CDEBUG(D_CACHE, "high prio request forcing RPC\n");
			return 1;
		}
		if (!list_empty(&osc->oo_urgent_exts)) {
			CDEBUG(D_CACHE, "urgent request forcing RPC\n");
			return 1;
		}
		/* trigger a write rpc stream as long as there are dirtiers
		 * waiting for space.  as they're waiting, they're not going to
		 * create more pages to coalesce with what's waiting..
		 */
		if (!list_empty(&cli->cl_cache_waiters)) {
			CDEBUG(D_CACHE, "cache waiters forcing RPC\n");
			return 1;
		}
		if (atomic_read(&osc->oo_nr_writes) >=
		    cli->cl_max_pages_per_rpc)
			return 1;
	} else {
		if (atomic_read(&osc->oo_nr_reads) == 0)
			return 0;
		if (invalid_import) {
			CDEBUG(D_CACHE, "invalid import forcing RPC\n");
			return 1;
		}
		/* all read are urgent. */
		if (!list_empty(&osc->oo_reading_exts))
			return 1;
	}

	return 0;
}

static void osc_update_pending(struct osc_object *obj, int cmd, int delta)
{
	struct client_obd *cli = osc_cli(obj);

	if (cmd & OBD_BRW_WRITE) {
		atomic_add(delta, &obj->oo_nr_writes);
		atomic_add(delta, &cli->cl_pending_w_pages);
		LASSERT(atomic_read(&obj->oo_nr_writes) >= 0);
	} else {
		atomic_add(delta, &obj->oo_nr_reads);
		atomic_add(delta, &cli->cl_pending_r_pages);
		LASSERT(atomic_read(&obj->oo_nr_reads) >= 0);
	}
	OSC_IO_DEBUG(obj, "update pending cmd %d delta %d.\n", cmd, delta);
}

static int osc_makes_hprpc(struct osc_object *obj)
{
	return !list_empty(&obj->oo_hp_exts);
}

static void on_list(struct list_head *item, struct list_head *list, int should_be_on)
{
	if (list_empty(item) && should_be_on)
		list_add_tail(item, list);
	else if (!list_empty(item) && !should_be_on)
		list_del_init(item);
}

/* maintain the osc's cli list membership invariants so that osc_send_oap_rpc
 * can find pages to build into rpcs quickly
 */
static int __osc_list_maint(struct client_obd *cli, struct osc_object *osc)
{
	if (osc_makes_hprpc(osc)) {
		/* HP rpc */
		on_list(&osc->oo_ready_item, &cli->cl_loi_ready_list, 0);
		on_list(&osc->oo_hp_ready_item, &cli->cl_loi_hp_ready_list, 1);
	} else {
		on_list(&osc->oo_hp_ready_item, &cli->cl_loi_hp_ready_list, 0);
		on_list(&osc->oo_ready_item, &cli->cl_loi_ready_list,
			osc_makes_rpc(cli, osc, OBD_BRW_WRITE) ||
			osc_makes_rpc(cli, osc, OBD_BRW_READ));
	}

	on_list(&osc->oo_write_item, &cli->cl_loi_write_list,
		atomic_read(&osc->oo_nr_writes) > 0);

	on_list(&osc->oo_read_item, &cli->cl_loi_read_list,
		atomic_read(&osc->oo_nr_reads) > 0);

	return osc_is_ready(osc);
}

static int osc_list_maint(struct client_obd *cli, struct osc_object *osc)
{
	int is_ready;

	spin_lock(&cli->cl_loi_list_lock);
	is_ready = __osc_list_maint(cli, osc);
	spin_unlock(&cli->cl_loi_list_lock);

	return is_ready;
}

/* this is trying to propagate async writeback errors back up to the
 * application.  As an async write fails we record the error code for later if
 * the app does an fsync.  As long as errors persist we force future rpcs to be
 * sync so that the app can get a sync error and break the cycle of queueing
 * pages for which writeback will fail.
 */
static void osc_process_ar(struct osc_async_rc *ar, __u64 xid,
			   int rc)
{
	if (rc) {
		if (!ar->ar_rc)
			ar->ar_rc = rc;

		ar->ar_force_sync = 1;
		ar->ar_min_xid = ptlrpc_sample_next_xid();
		return;
	}

	if (ar->ar_force_sync && (xid >= ar->ar_min_xid))
		ar->ar_force_sync = 0;
}

/* this must be called holding the loi list lock to give coverage to exit_cache,
 * async_flag maintenance, and oap_request
 */
static void osc_ap_completion(const struct lu_env *env, struct client_obd *cli,
			      struct osc_async_page *oap, int sent, int rc)
{
	struct osc_object *osc = oap->oap_obj;
	struct lov_oinfo *loi = osc->oo_oinfo;
	__u64 xid = 0;

	if (oap->oap_request) {
		xid = ptlrpc_req_xid(oap->oap_request);
		ptlrpc_req_finished(oap->oap_request);
		oap->oap_request = NULL;
	}

	/* As the transfer for this page is being done, clear the flags */
	spin_lock(&oap->oap_lock);
	oap->oap_async_flags = 0;
	spin_unlock(&oap->oap_lock);
	oap->oap_interrupted = 0;

	if (oap->oap_cmd & OBD_BRW_WRITE && xid > 0) {
		spin_lock(&cli->cl_loi_list_lock);
		osc_process_ar(&cli->cl_ar, xid, rc);
		osc_process_ar(&loi->loi_ar, xid, rc);
		spin_unlock(&cli->cl_loi_list_lock);
	}

	rc = osc_completion(env, oap, oap->oap_cmd, rc);
	if (rc)
		CERROR("completion on oap %p obj %p returns %d.\n",
		       oap, osc, rc);
}

/**
 * Try to add extent to one RPC. We need to think about the following things:
 * - # of pages must not be over max_pages_per_rpc
 * - extent must be compatible with previous ones
 */
static int try_to_add_extent_for_io(struct client_obd *cli,
				    struct osc_extent *ext, struct list_head *rpclist,
				    int *pc, unsigned int *max_pages)
{
	struct osc_extent *tmp;
	struct osc_async_page *oap = list_first_entry(&ext->oe_pages,
						      struct osc_async_page,
						      oap_pending_item);

	EASSERT((ext->oe_state == OES_CACHE || ext->oe_state == OES_LOCK_DONE),
		ext);

	*max_pages = max(ext->oe_mppr, *max_pages);
	if (*pc + ext->oe_nr_pages > *max_pages)
		return 0;

	list_for_each_entry(tmp, rpclist, oe_link) {
		struct osc_async_page *oap2;

		oap2 = list_first_entry(&tmp->oe_pages, struct osc_async_page,
					oap_pending_item);
		EASSERT(tmp->oe_owner == current, tmp);
		if (oap2cl_page(oap)->cp_type != oap2cl_page(oap2)->cp_type) {
			CDEBUG(D_CACHE, "Do not permit different type of IO"
					" for a same RPC\n");
			return 0;
		}

		if (tmp->oe_srvlock != ext->oe_srvlock ||
		    !tmp->oe_grants != !ext->oe_grants)
			return 0;

		/* remove break for strict check */
		break;
	}

	*pc += ext->oe_nr_pages;
	list_move_tail(&ext->oe_link, rpclist);
	ext->oe_owner = current;
	return 1;
}

/**
 * In order to prevent multiple ptlrpcd from breaking contiguous extents,
 * get_write_extent() takes all appropriate extents in atomic.
 *
 * The following policy is used to collect extents for IO:
 * 1. Add as many HP extents as possible;
 * 2. Add the first urgent extent in urgent extent list and take it out of
 *    urgent list;
 * 3. Add subsequent extents of this urgent extent;
 * 4. If urgent list is not empty, goto 2;
 * 5. Traverse the extent tree from the 1st extent;
 * 6. Above steps exit if there is no space in this RPC.
 */
static int get_write_extents(struct osc_object *obj, struct list_head *rpclist)
{
	struct client_obd *cli = osc_cli(obj);
	struct osc_extent *ext;
	struct osc_extent *temp;
	int page_count = 0;
	unsigned int max_pages = cli->cl_max_pages_per_rpc;

	LASSERT(osc_object_is_locked(obj));
	list_for_each_entry_safe(ext, temp, &obj->oo_hp_exts, oe_link) {
		LASSERT(ext->oe_state == OES_CACHE);
		if (!try_to_add_extent_for_io(cli, ext, rpclist, &page_count,
					      &max_pages))
			return page_count;
		EASSERT(ext->oe_nr_pages <= max_pages, ext);
	}
	if (page_count == max_pages)
		return page_count;

	while (!list_empty(&obj->oo_urgent_exts)) {
		ext = list_entry(obj->oo_urgent_exts.next,
				 struct osc_extent, oe_link);
		if (!try_to_add_extent_for_io(cli, ext, rpclist, &page_count,
					      &max_pages))
			return page_count;

		if (!ext->oe_intree)
			continue;

		while ((ext = next_extent(ext)) != NULL) {
			if ((ext->oe_state != OES_CACHE) ||
			    (!list_empty(&ext->oe_link) &&
			     ext->oe_owner))
				continue;

			if (!try_to_add_extent_for_io(cli, ext, rpclist,
						      &page_count, &max_pages))
				return page_count;
		}
	}
	if (page_count == max_pages)
		return page_count;

	ext = first_extent(obj);
	while (ext) {
		if ((ext->oe_state != OES_CACHE) ||
		    /* this extent may be already in current rpclist */
		    (!list_empty(&ext->oe_link) && ext->oe_owner)) {
			ext = next_extent(ext);
			continue;
		}

		if (!try_to_add_extent_for_io(cli, ext, rpclist, &page_count,
					      &max_pages))
			return page_count;

		ext = next_extent(ext);
	}
	return page_count;
}

static int
osc_send_write_rpc(const struct lu_env *env, struct client_obd *cli,
		   struct osc_object *osc)
	__must_hold(osc)
{
	LIST_HEAD(rpclist);
	struct osc_extent *ext;
	struct osc_extent *tmp;
	struct osc_extent *first = NULL;
	u32 page_count = 0;
	int srvlock = 0;
	int rc = 0;

	LASSERT(osc_object_is_locked(osc));

	page_count = get_write_extents(osc, &rpclist);
	LASSERT(equi(page_count == 0, list_empty(&rpclist)));

	if (list_empty(&rpclist))
		return 0;

	osc_update_pending(osc, OBD_BRW_WRITE, -page_count);

	list_for_each_entry(ext, &rpclist, oe_link) {
		LASSERT(ext->oe_state == OES_CACHE ||
			ext->oe_state == OES_LOCK_DONE);
		if (ext->oe_state == OES_CACHE)
			osc_extent_state_set(ext, OES_LOCKING);
		else
			osc_extent_state_set(ext, OES_RPC);
	}

	/* we're going to grab page lock, so release object lock because
	 * lock order is page lock -> object lock.
	 */
	osc_object_unlock(osc);

	list_for_each_entry_safe(ext, tmp, &rpclist, oe_link) {
		if (ext->oe_state == OES_LOCKING) {
			rc = osc_extent_make_ready(env, ext);
			if (unlikely(rc < 0)) {
				list_del_init(&ext->oe_link);
				osc_extent_finish(env, ext, 0, rc);
				continue;
			}
		}
		if (!first) {
			first = ext;
			srvlock = ext->oe_srvlock;
		} else {
			LASSERT(srvlock == ext->oe_srvlock);
		}
	}

	if (!list_empty(&rpclist)) {
		LASSERT(page_count > 0);
		rc = osc_build_rpc(env, cli, &rpclist, OBD_BRW_WRITE);
		LASSERT(list_empty(&rpclist));
	}

	osc_object_lock(osc);
	return rc;
}

/**
 * prepare pages for ASYNC io and put pages in send queue.
 *
 * \param cmd OBD_BRW_* macroses
 * \param lop pending pages
 *
 * \return zero if no page added to send queue.
 * \return 1 if pages successfully added to send queue.
 * \return negative on errors.
 */
static int
osc_send_read_rpc(const struct lu_env *env, struct client_obd *cli,
		  struct osc_object *osc)
	__must_hold(osc)
{
	struct osc_extent *ext;
	struct osc_extent *next;
	LIST_HEAD(rpclist);
	int page_count = 0;
	unsigned int max_pages = cli->cl_max_pages_per_rpc;
	int rc = 0;

	LASSERT(osc_object_is_locked(osc));
	list_for_each_entry_safe(ext, next, &osc->oo_reading_exts, oe_link) {
		EASSERT(ext->oe_state == OES_LOCK_DONE, ext);
		if (!try_to_add_extent_for_io(cli, ext, &rpclist, &page_count,
					      &max_pages))
			break;
		osc_extent_state_set(ext, OES_RPC);
		EASSERT(ext->oe_nr_pages <= max_pages, ext);
	}
	LASSERT(page_count <= max_pages);

	osc_update_pending(osc, OBD_BRW_READ, -page_count);

	if (!list_empty(&rpclist)) {
		osc_object_unlock(osc);

		LASSERT(page_count > 0);
		rc = osc_build_rpc(env, cli, &rpclist, OBD_BRW_READ);
		LASSERT(list_empty(&rpclist));

		osc_object_lock(osc);
	}
	return rc;
}

#define list_to_obj(list, item) ({					      \
	struct list_head *__tmp = (list)->next;				      \
	list_del_init(__tmp);						      \
	list_entry(__tmp, struct osc_object, oo_##item);		      \
})

/* This is called by osc_check_rpcs() to find which objects have pages that
 * we could be sending.  These lists are maintained by osc_makes_rpc().
 */
static struct osc_object *osc_next_obj(struct client_obd *cli)
{
	/* First return objects that have blocked locks so that they
	 * will be flushed quickly and other clients can get the lock,
	 * then objects which have pages ready to be stuffed into RPCs
	 */
	if (!list_empty(&cli->cl_loi_hp_ready_list))
		return list_to_obj(&cli->cl_loi_hp_ready_list, hp_ready_item);
	if (!list_empty(&cli->cl_loi_ready_list))
		return list_to_obj(&cli->cl_loi_ready_list, ready_item);

	/* then if we have cache waiters, return all objects with queued
	 * writes.  This is especially important when many small files
	 * have filled up the cache and not been fired into rpcs because
	 * they don't pass the nr_pending/object threshold
	 */
	if (!list_empty(&cli->cl_cache_waiters) &&
	    !list_empty(&cli->cl_loi_write_list))
		return list_to_obj(&cli->cl_loi_write_list, write_item);

	/* then return all queued objects when we have an invalid import
	 * so that they get flushed
	 */
	if (!cli->cl_import || cli->cl_import->imp_invalid) {
		if (!list_empty(&cli->cl_loi_write_list))
			return list_to_obj(&cli->cl_loi_write_list, write_item);
		if (!list_empty(&cli->cl_loi_read_list))
			return list_to_obj(&cli->cl_loi_read_list, read_item);
	}
	return NULL;
}

/* called with the loi list lock held */
static void osc_check_rpcs(const struct lu_env *env, struct client_obd *cli)
	__must_hold(&cli->cl_loi_list_lock)
{
	struct osc_object *osc;
	int rc = 0;

	while ((osc = osc_next_obj(cli)) != NULL) {
		struct cl_object *obj = osc2cl(osc);
		struct lu_ref_link link;

		OSC_IO_DEBUG(osc, "%lu in flight\n", rpcs_in_flight(cli));

		if (osc_max_rpc_in_flight(cli, osc)) {
			__osc_list_maint(cli, osc);
			break;
		}

		cl_object_get(obj);
		spin_unlock(&cli->cl_loi_list_lock);
		lu_object_ref_add_at(&obj->co_lu, &link, "check", current);

		/* attempt some read/write balancing by alternating between
		 * reads and writes in an object.  The makes_rpc checks here
		 * would be redundant if we were getting read/write work items
		 * instead of objects.  we don't want send_oap_rpc to drain a
		 * partial read pending queue when we're given this object to
		 * do io on writes while there are cache waiters
		 */
		osc_object_lock(osc);
		if (osc_makes_rpc(cli, osc, OBD_BRW_WRITE)) {
			rc = osc_send_write_rpc(env, cli, osc);
			if (rc < 0) {
				CERROR("Write request failed with %d\n", rc);

				/* osc_send_write_rpc failed, mostly because of
				 * memory pressure.
				 *
				 * It can't break here, because if:
				 *  - a page was submitted by osc_io_submit, so
				 *    page locked;
				 *  - no request in flight
				 *  - no subsequent request
				 * The system will be in live-lock state,
				 * because there is no chance to call
				 * osc_io_unplug() and osc_check_rpcs() any
				 * more. pdflush can't help in this case,
				 * because it might be blocked at grabbing
				 * the page lock as we mentioned.
				 *
				 * Anyway, continue to drain pages.
				 */
				/* break; */
			}
		}
		if (osc_makes_rpc(cli, osc, OBD_BRW_READ)) {
			rc = osc_send_read_rpc(env, cli, osc);
			if (rc < 0)
				CERROR("Read request failed with %d\n", rc);
		}
		osc_object_unlock(osc);

		osc_list_maint(cli, osc);
		lu_object_ref_del_at(&obj->co_lu, &link, "check", current);
		cl_object_put(env, obj);

		spin_lock(&cli->cl_loi_list_lock);
	}
}

static int osc_io_unplug0(const struct lu_env *env, struct client_obd *cli,
			  struct osc_object *osc, int async)
{
	int rc = 0;

	if (osc && osc_list_maint(cli, osc) == 0)
		return 0;

	if (!async) {
		/* disable osc_lru_shrink() temporarily to avoid
		 * potential stack overrun problem. LU-2859
		 */
		atomic_inc(&cli->cl_lru_shrinkers);
		spin_lock(&cli->cl_loi_list_lock);
		osc_check_rpcs(env, cli);
		spin_unlock(&cli->cl_loi_list_lock);
		atomic_dec(&cli->cl_lru_shrinkers);
	} else {
		CDEBUG(D_CACHE, "Queue writeback work for client %p.\n", cli);
		LASSERT(cli->cl_writeback_work);
		rc = ptlrpcd_queue_work(cli->cl_writeback_work);
	}
	return rc;
}

static int osc_io_unplug_async(const struct lu_env *env,
			       struct client_obd *cli, struct osc_object *osc)
{
	return osc_io_unplug0(env, cli, osc, 1);
}

void osc_io_unplug(const struct lu_env *env, struct client_obd *cli,
		   struct osc_object *osc)
{
	(void)osc_io_unplug0(env, cli, osc, 0);
}

int osc_prep_async_page(struct osc_object *osc, struct osc_page *ops,
			struct page *page, loff_t offset)
{
	struct obd_export *exp = osc_export(osc);
	struct osc_async_page *oap = &ops->ops_oap;

	if (!page)
		return cfs_size_round(sizeof(*oap));

	oap->oap_magic = OAP_MAGIC;
	oap->oap_cli = &exp->exp_obd->u.cli;
	oap->oap_obj = osc;

	oap->oap_page = page;
	oap->oap_obj_off = offset;
	LASSERT(!(offset & ~PAGE_MASK));

	if (capable(CFS_CAP_SYS_RESOURCE))
		oap->oap_brw_flags = OBD_BRW_NOQUOTA;

	INIT_LIST_HEAD(&oap->oap_pending_item);
	INIT_LIST_HEAD(&oap->oap_rpc_item);

	spin_lock_init(&oap->oap_lock);
	CDEBUG(D_INFO, "oap %p page %p obj off %llu\n",
	       oap, page, oap->oap_obj_off);
	return 0;
}

int osc_queue_async_io(const struct lu_env *env, struct cl_io *io,
		       struct osc_page *ops)
{
	struct osc_io *oio = osc_env_io(env);
	struct osc_extent *ext = NULL;
	struct osc_async_page *oap = &ops->ops_oap;
	struct client_obd *cli = oap->oap_cli;
	struct osc_object *osc = oap->oap_obj;
	pgoff_t index;
	int grants = 0;
	int brw_flags = OBD_BRW_ASYNC;
	int cmd = OBD_BRW_WRITE;
	int need_release = 0;
	int rc = 0;

	if (oap->oap_magic != OAP_MAGIC)
		return -EINVAL;

	if (!cli->cl_import || cli->cl_import->imp_invalid)
		return -EIO;

	if (!list_empty(&oap->oap_pending_item) ||
	    !list_empty(&oap->oap_rpc_item))
		return -EBUSY;

	/* Set the OBD_BRW_SRVLOCK before the page is queued. */
	brw_flags |= ops->ops_srvlock ? OBD_BRW_SRVLOCK : 0;
	if (capable(CFS_CAP_SYS_RESOURCE)) {
		brw_flags |= OBD_BRW_NOQUOTA;
		cmd |= OBD_BRW_NOQUOTA;
	}

	/* check if the file's owner/group is over quota */
	if (!(cmd & OBD_BRW_NOQUOTA)) {
		struct cl_object *obj;
		struct cl_attr *attr;
		unsigned int qid[MAXQUOTAS];

		obj = cl_object_top(&osc->oo_cl);
		attr = &osc_env_info(env)->oti_attr;

		cl_object_attr_lock(obj);
		rc = cl_object_attr_get(env, obj, attr);
		cl_object_attr_unlock(obj);

		qid[USRQUOTA] = attr->cat_uid;
		qid[GRPQUOTA] = attr->cat_gid;
		if (rc == 0 && osc_quota_chkdq(cli, qid) == NO_QUOTA)
			rc = -EDQUOT;
		if (rc)
			return rc;
	}

	oap->oap_cmd = cmd;
	oap->oap_page_off = ops->ops_from;
	oap->oap_count = ops->ops_to - ops->ops_from;
	/*
	 * No need to hold a lock here,
	 * since this page is not in any list yet.
	 */
	oap->oap_async_flags = 0;
	oap->oap_brw_flags = brw_flags;

	OSC_IO_DEBUG(osc, "oap %p page %p added for cmd %d\n",
		     oap, oap->oap_page, oap->oap_cmd & OBD_BRW_RWMASK);

	index = osc_index(oap2osc(oap));

	/* Add this page into extent by the following steps:
	 * 1. if there exists an active extent for this IO, mostly this page
	 *    can be added to the active extent and sometimes we need to
	 *    expand extent to accommodate this page;
	 * 2. otherwise, a new extent will be allocated.
	 */

	ext = oio->oi_active;
	if (ext && ext->oe_start <= index && ext->oe_max_end >= index) {
		/* one chunk plus extent overhead must be enough to write this
		 * page
		 */
		grants = (1 << cli->cl_chunkbits) + cli->cl_extent_tax;
		if (ext->oe_end >= index)
			grants = 0;

		/* it doesn't need any grant to dirty this page */
		spin_lock(&cli->cl_loi_list_lock);
		rc = osc_enter_cache_try(cli, oap, grants, 0);
		spin_unlock(&cli->cl_loi_list_lock);
		if (rc == 0) { /* try failed */
			grants = 0;
			need_release = 1;
		} else if (ext->oe_end < index) {
			int tmp = grants;
			/* try to expand this extent */
			rc = osc_extent_expand(ext, index, &tmp);
			if (rc < 0) {
				need_release = 1;
				/* don't free reserved grant */
			} else {
				OSC_EXTENT_DUMP(D_CACHE, ext,
						"expanded for %lu.\n", index);
				osc_unreserve_grant(cli, grants, tmp);
				grants = 0;
			}
		}
		rc = 0;
	} else if (ext) {
		/* index is located outside of active extent */
		need_release = 1;
	}
	if (need_release) {
		osc_extent_release(env, ext);
		oio->oi_active = NULL;
		ext = NULL;
	}

	if (!ext) {
		int tmp = (1 << cli->cl_chunkbits) + cli->cl_extent_tax;

		/* try to find new extent to cover this page */
		LASSERT(!oio->oi_active);
		/* we may have allocated grant for this page if we failed
		 * to expand the previous active extent.
		 */
		LASSERT(ergo(grants > 0, grants >= tmp));

		rc = 0;
		if (grants == 0) {
			/* we haven't allocated grant for this page. */
			rc = osc_enter_cache(env, cli, oap, tmp);
			if (rc == 0)
				grants = tmp;
		}

		tmp = grants;
		if (rc == 0) {
			ext = osc_extent_find(env, osc, index, &tmp);
			if (IS_ERR(ext)) {
				LASSERT(tmp == grants);
				osc_exit_cache(cli, oap);
				rc = PTR_ERR(ext);
				ext = NULL;
			} else {
				oio->oi_active = ext;
			}
		}
		if (grants > 0)
			osc_unreserve_grant(cli, grants, tmp);
	}

	LASSERT(ergo(rc == 0, ext));
	if (ext) {
		EASSERTF(ext->oe_end >= index && ext->oe_start <= index,
			 ext, "index = %lu.\n", index);
		LASSERT((oap->oap_brw_flags & OBD_BRW_FROM_GRANT) != 0);

		osc_object_lock(osc);
		if (ext->oe_nr_pages == 0)
			ext->oe_srvlock = ops->ops_srvlock;
		else
			LASSERT(ext->oe_srvlock == ops->ops_srvlock);
		++ext->oe_nr_pages;
		list_add_tail(&oap->oap_pending_item, &ext->oe_pages);
		osc_object_unlock(osc);
	}
	return rc;
}

int osc_teardown_async_page(const struct lu_env *env,
			    struct osc_object *obj, struct osc_page *ops)
{
	struct osc_async_page *oap = &ops->ops_oap;
	struct osc_extent *ext = NULL;
	int rc = 0;

	LASSERT(oap->oap_magic == OAP_MAGIC);

	CDEBUG(D_INFO, "teardown oap %p page %p at index %lu.\n",
	       oap, ops, osc_index(oap2osc(oap)));

	osc_object_lock(obj);
	if (!list_empty(&oap->oap_rpc_item)) {
		CDEBUG(D_CACHE, "oap %p is not in cache.\n", oap);
		rc = -EBUSY;
	} else if (!list_empty(&oap->oap_pending_item)) {
		ext = osc_extent_lookup(obj, osc_index(oap2osc(oap)));
		/* only truncated pages are allowed to be taken out.
		 * See osc_extent_truncate() and osc_cache_truncate_start()
		 * for details.
		 */
		if (ext && ext->oe_state != OES_TRUNC) {
			OSC_EXTENT_DUMP(D_ERROR, ext, "trunc at %lu.\n",
					osc_index(oap2osc(oap)));
			rc = -EBUSY;
		}
	}
	osc_object_unlock(obj);
	if (ext)
		osc_extent_put(env, ext);
	return rc;
}

/**
 * This is called when a page is picked up by kernel to write out.
 *
 * We should find out the corresponding extent and add the whole extent
 * into urgent list. The extent may be being truncated or used, handle it
 * carefully.
 */
int osc_flush_async_page(const struct lu_env *env, struct cl_io *io,
			 struct osc_page *ops)
{
	struct osc_extent *ext = NULL;
	struct osc_object *obj = cl2osc(ops->ops_cl.cpl_obj);
	struct cl_page *cp = ops->ops_cl.cpl_page;
	pgoff_t            index = osc_index(ops);
	struct osc_async_page *oap = &ops->ops_oap;
	bool unplug = false;
	int rc = 0;

	osc_object_lock(obj);
	ext = osc_extent_lookup(obj, index);
	if (!ext) {
		osc_extent_tree_dump(D_ERROR, obj);
		LASSERTF(0, "page index %lu is NOT covered.\n", index);
	}

	switch (ext->oe_state) {
	case OES_RPC:
	case OES_LOCK_DONE:
		CL_PAGE_DEBUG(D_ERROR, env, cp, "flush an in-rpc page?\n");
		LASSERT(0);
		break;
	case OES_LOCKING:
		/* If we know this extent is being written out, we should abort
		 * so that the writer can make this page ready. Otherwise, there
		 * exists a deadlock problem because other process can wait for
		 * page writeback bit holding page lock; and meanwhile in
		 * vvp_page_make_ready(), we need to grab page lock before
		 * really sending the RPC.
		 */
	case OES_TRUNC:
		/* race with truncate, page will be redirtied */
	case OES_ACTIVE:
		/* The extent is active so we need to abort and let the caller
		 * re-dirty the page. If we continued on here, and we were the
		 * one making the extent active, we could deadlock waiting for
		 * the page writeback to clear but it won't because the extent
		 * is active and won't be written out.
		 */
		rc = -EAGAIN;
		goto out;
	default:
		break;
	}

	rc = cl_page_prep(env, io, cp, CRT_WRITE);
	if (rc)
		goto out;

	spin_lock(&oap->oap_lock);
	oap->oap_async_flags |= ASYNC_READY | ASYNC_URGENT;
	spin_unlock(&oap->oap_lock);

	if (memory_pressure_get())
		ext->oe_memalloc = 1;

	ext->oe_urgent = 1;
	if (ext->oe_state == OES_CACHE) {
		OSC_EXTENT_DUMP(D_CACHE, ext,
				"flush page %p make it urgent.\n", oap);
		if (list_empty(&ext->oe_link))
			list_add_tail(&ext->oe_link, &obj->oo_urgent_exts);
		unplug = true;
	}
	rc = 0;

out:
	osc_object_unlock(obj);
	osc_extent_put(env, ext);
	if (unplug)
		osc_io_unplug_async(env, osc_cli(obj), obj);
	return rc;
}

/**
 * this is called when a sync waiter receives an interruption.  Its job is to
 * get the caller woken as soon as possible.  If its page hasn't been put in an
 * rpc yet it can dequeue immediately.  Otherwise it has to mark the rpc as
 * desiring interruption which will forcefully complete the rpc once the rpc
 * has timed out.
 */
int osc_cancel_async_page(const struct lu_env *env, struct osc_page *ops)
{
	struct osc_async_page *oap = &ops->ops_oap;
	struct osc_object *obj = oap->oap_obj;
	struct client_obd *cli = osc_cli(obj);
	struct osc_extent *ext;
	struct osc_extent *found = NULL;
	struct list_head *plist;
	pgoff_t index = osc_index(ops);
	int rc = -EBUSY;
	int cmd;

	LASSERT(!oap->oap_interrupted);
	oap->oap_interrupted = 1;

	/* Find out the caching extent */
	osc_object_lock(obj);
	if (oap->oap_cmd & OBD_BRW_WRITE) {
		plist = &obj->oo_urgent_exts;
		cmd = OBD_BRW_WRITE;
	} else {
		plist = &obj->oo_reading_exts;
		cmd = OBD_BRW_READ;
	}
	list_for_each_entry(ext, plist, oe_link) {
		if (ext->oe_start <= index && ext->oe_end >= index) {
			LASSERT(ext->oe_state == OES_LOCK_DONE);
			/* For OES_LOCK_DONE state extent, it has already held
			 * a refcount for RPC.
			 */
			found = osc_extent_get(ext);
			break;
		}
	}
	if (found) {
		list_del_init(&found->oe_link);
		osc_update_pending(obj, cmd, -found->oe_nr_pages);
		osc_object_unlock(obj);

		osc_extent_finish(env, found, 0, -EINTR);
		osc_extent_put(env, found);
		rc = 0;
	} else {
		osc_object_unlock(obj);
		/* ok, it's been put in an rpc. only one oap gets a request
		 * reference
		 */
		if (oap->oap_request) {
			ptlrpc_mark_interrupted(oap->oap_request);
			ptlrpcd_wake(oap->oap_request);
			ptlrpc_req_finished(oap->oap_request);
			oap->oap_request = NULL;
		}
	}

	osc_list_maint(cli, obj);
	return rc;
}

int osc_queue_sync_pages(const struct lu_env *env, struct osc_object *obj,
			 struct list_head *list, int cmd, int brw_flags)
{
	struct client_obd *cli = osc_cli(obj);
	struct osc_extent *ext;
	struct osc_async_page *oap, *tmp;
	int page_count = 0;
	int mppr = cli->cl_max_pages_per_rpc;
	pgoff_t start = CL_PAGE_EOF;
	pgoff_t end = 0;

	list_for_each_entry(oap, list, oap_pending_item) {
		pgoff_t index = osc_index(oap2osc(oap));

		if (index > end)
			end = index;
		if (index < start)
			start = index;
		++page_count;
		mppr <<= (page_count > mppr);
	}

	ext = osc_extent_alloc(obj);
	if (!ext) {
		list_for_each_entry_safe(oap, tmp, list, oap_pending_item) {
			list_del_init(&oap->oap_pending_item);
			osc_ap_completion(env, cli, oap, 0, -ENOMEM);
		}
		return -ENOMEM;
	}

	ext->oe_rw = !!(cmd & OBD_BRW_READ);
	ext->oe_sync = 1;
	ext->oe_urgent = 1;
	ext->oe_start = start;
	ext->oe_end = end;
	ext->oe_max_end = end;
	ext->oe_obj = obj;
	ext->oe_srvlock = !!(brw_flags & OBD_BRW_SRVLOCK);
	ext->oe_nr_pages = page_count;
	ext->oe_mppr = mppr;
	list_splice_init(list, &ext->oe_pages);

	osc_object_lock(obj);
	/* Reuse the initial refcount for RPC, don't drop it */
	osc_extent_state_set(ext, OES_LOCK_DONE);
	if (cmd & OBD_BRW_WRITE) {
		list_add_tail(&ext->oe_link, &obj->oo_urgent_exts);
		osc_update_pending(obj, OBD_BRW_WRITE, page_count);
	} else {
		list_add_tail(&ext->oe_link, &obj->oo_reading_exts);
		osc_update_pending(obj, OBD_BRW_READ, page_count);
	}
	osc_object_unlock(obj);

	osc_io_unplug_async(env, cli, obj);
	return 0;
}

/**
 * Called by osc_io_setattr_start() to freeze and destroy covering extents.
 */
int osc_cache_truncate_start(const struct lu_env *env, struct osc_io *oio,
			     struct osc_object *obj, __u64 size)
{
	struct client_obd *cli = osc_cli(obj);
	struct osc_extent *ext;
	struct osc_extent *temp;
	struct osc_extent *waiting = NULL;
	pgoff_t index;
	LIST_HEAD(list);
	int result = 0;
	bool partial;

	/* pages with index greater or equal to index will be truncated. */
	index = cl_index(osc2cl(obj), size);
	partial = size > cl_offset(osc2cl(obj), index);

again:
	osc_object_lock(obj);
	ext = osc_extent_search(obj, index);
	if (!ext)
		ext = first_extent(obj);
	else if (ext->oe_end < index)
		ext = next_extent(ext);
	while (ext) {
		EASSERT(ext->oe_state != OES_TRUNC, ext);

		if (ext->oe_state > OES_CACHE || ext->oe_urgent) {
			/* if ext is in urgent state, it means there must exist
			 * a page already having been flushed by write_page().
			 * We have to wait for this extent because we can't
			 * truncate that page.
			 */
			LASSERT(!ext->oe_hp);
			OSC_EXTENT_DUMP(D_CACHE, ext,
					"waiting for busy extent\n");
			waiting = osc_extent_get(ext);
			break;
		}

		OSC_EXTENT_DUMP(D_CACHE, ext, "try to trunc:%llu.\n", size);

		osc_extent_get(ext);
		if (ext->oe_state == OES_ACTIVE) {
			/* though we grab inode mutex for write path, but we
			 * release it before releasing extent(in osc_io_end()),
			 * so there is a race window that an extent is still
			 * in OES_ACTIVE when truncate starts.
			 */
			LASSERT(!ext->oe_trunc_pending);
			ext->oe_trunc_pending = 1;
		} else {
			EASSERT(ext->oe_state == OES_CACHE, ext);
			osc_extent_state_set(ext, OES_TRUNC);
			osc_update_pending(obj, OBD_BRW_WRITE,
					   -ext->oe_nr_pages);
		}
		EASSERT(list_empty(&ext->oe_link), ext);
		list_add_tail(&ext->oe_link, &list);

		ext = next_extent(ext);
	}
	osc_object_unlock(obj);

	osc_list_maint(cli, obj);

	list_for_each_entry_safe(ext, temp, &list, oe_link) {
		int rc;

		list_del_init(&ext->oe_link);

		/* extent may be in OES_ACTIVE state because inode mutex
		 * is released before osc_io_end() in file write case
		 */
		if (ext->oe_state != OES_TRUNC)
			osc_extent_wait(env, ext, OES_TRUNC);

		rc = osc_extent_truncate(ext, index, partial);
		if (rc < 0) {
			if (result == 0)
				result = rc;

			OSC_EXTENT_DUMP(D_ERROR, ext,
					"truncate error %d\n", rc);
		} else if (ext->oe_nr_pages == 0) {
			osc_extent_remove(ext);
		} else {
			/* this must be an overlapped extent which means only
			 * part of pages in this extent have been truncated.
			 */
			EASSERTF(ext->oe_start <= index, ext,
				 "trunc index = %lu/%d.\n", index, partial);
			/* fix index to skip this partially truncated extent */
			index = ext->oe_end + 1;
			partial = false;

			/* we need to hold this extent in OES_TRUNC state so
			 * that no writeback will happen. This is to avoid
			 * BUG 17397.
			 */
			LASSERT(!oio->oi_trunc);
			oio->oi_trunc = osc_extent_get(ext);
			OSC_EXTENT_DUMP(D_CACHE, ext,
					"trunc at %llu\n", size);
		}
		osc_extent_put(env, ext);
	}
	if (waiting) {
		int rc;

		/* ignore the result of osc_extent_wait the write initiator
		 * should take care of it.
		 */
		rc = osc_extent_wait(env, waiting, OES_INV);
		if (rc < 0)
			OSC_EXTENT_DUMP(D_CACHE, waiting, "error: %d.\n", rc);

		osc_extent_put(env, waiting);
		waiting = NULL;
		goto again;
	}
	return result;
}

/**
 * Called after osc_io_setattr_end to add oio->oi_trunc back to cache.
 */
void osc_cache_truncate_end(const struct lu_env *env, struct osc_io *oio,
			    struct osc_object *obj)
{
	struct osc_extent *ext = oio->oi_trunc;

	oio->oi_trunc = NULL;
	if (ext) {
		bool unplug = false;

		EASSERT(ext->oe_nr_pages > 0, ext);
		EASSERT(ext->oe_state == OES_TRUNC, ext);
		EASSERT(!ext->oe_urgent, ext);

		OSC_EXTENT_DUMP(D_CACHE, ext, "trunc -> cache.\n");
		osc_object_lock(obj);
		osc_extent_state_set(ext, OES_CACHE);
		if (ext->oe_fsync_wait && !ext->oe_urgent) {
			ext->oe_urgent = 1;
			list_move_tail(&ext->oe_link, &obj->oo_urgent_exts);
			unplug = true;
		}
		osc_update_pending(obj, OBD_BRW_WRITE, ext->oe_nr_pages);
		osc_object_unlock(obj);
		osc_extent_put(env, ext);

		if (unplug)
			osc_io_unplug_async(env, osc_cli(obj), obj);
	}
}

/**
 * Wait for extents in a specific range to be written out.
 * The caller must have called osc_cache_writeback_range() to issue IO
 * otherwise it will take a long time for this function to finish.
 *
 * Caller must hold inode_mutex , or cancel exclusive dlm lock so that
 * nobody else can dirty this range of file while we're waiting for
 * extents to be written.
 */
int osc_cache_wait_range(const struct lu_env *env, struct osc_object *obj,
			 pgoff_t start, pgoff_t end)
{
	struct osc_extent *ext;
	pgoff_t index = start;
	int result = 0;

again:
	osc_object_lock(obj);
	ext = osc_extent_search(obj, index);
	if (!ext)
		ext = first_extent(obj);
	else if (ext->oe_end < index)
		ext = next_extent(ext);
	while (ext) {
		int rc;

		if (ext->oe_start > end)
			break;

		if (!ext->oe_fsync_wait) {
			ext = next_extent(ext);
			continue;
		}

		EASSERT(ergo(ext->oe_state == OES_CACHE,
			     ext->oe_hp || ext->oe_urgent), ext);
		EASSERT(ergo(ext->oe_state == OES_ACTIVE,
			     !ext->oe_hp && ext->oe_urgent), ext);

		index = ext->oe_end + 1;
		osc_extent_get(ext);
		osc_object_unlock(obj);

		rc = osc_extent_wait(env, ext, OES_INV);
		if (result == 0)
			result = rc;
		osc_extent_put(env, ext);
		goto again;
	}
	osc_object_unlock(obj);

	OSC_IO_DEBUG(obj, "sync file range.\n");
	return result;
}

/**
 * Called to write out a range of osc object.
 *
 * @hp     : should be set this is caused by lock cancel;
 * @discard: is set if dirty pages should be dropped - file will be deleted or
 *	   truncated, this implies there is no partially discarding extents.
 *
 * Return how many pages will be issued, or error code if error occurred.
 */
int osc_cache_writeback_range(const struct lu_env *env, struct osc_object *obj,
			      pgoff_t start, pgoff_t end, int hp, int discard)
{
	struct osc_extent *ext;
	LIST_HEAD(discard_list);
	bool unplug = false;
	int result = 0;

	osc_object_lock(obj);
	ext = osc_extent_search(obj, start);
	if (!ext)
		ext = first_extent(obj);
	else if (ext->oe_end < start)
		ext = next_extent(ext);
	while (ext) {
		if (ext->oe_start > end)
			break;

		ext->oe_fsync_wait = 1;
		switch (ext->oe_state) {
		case OES_CACHE:
			result += ext->oe_nr_pages;
			if (!discard) {
				struct list_head *list = NULL;

				if (hp) {
					EASSERT(!ext->oe_hp, ext);
					ext->oe_hp = 1;
					list = &obj->oo_hp_exts;
				} else if (!ext->oe_urgent) {
					ext->oe_urgent = 1;
					list = &obj->oo_urgent_exts;
				}
				if (list)
					list_move_tail(&ext->oe_link, list);
				unplug = true;
			} else {
				/* the only discarder is lock cancelling, so
				 * [start, end] must contain this extent
				 */
				EASSERT(ext->oe_start >= start &&
					ext->oe_max_end <= end, ext);
				osc_extent_state_set(ext, OES_LOCKING);
				ext->oe_owner = current;
				list_move_tail(&ext->oe_link, &discard_list);
				osc_update_pending(obj, OBD_BRW_WRITE,
						   -ext->oe_nr_pages);
			}
			break;
		case OES_ACTIVE:
			/* It's pretty bad to wait for ACTIVE extents, because
			 * we don't know how long we will wait for it to be
			 * flushed since it may be blocked at awaiting more
			 * grants. We do this for the correctness of fsync.
			 */
			LASSERT(hp == 0 && discard == 0);
			ext->oe_urgent = 1;
			break;
		case OES_TRUNC:
			/* this extent is being truncated, can't do anything
			 * for it now. it will be set to urgent after truncate
			 * is finished in osc_cache_truncate_end().
			 */
		default:
			break;
		}
		ext = next_extent(ext);
	}
	osc_object_unlock(obj);

	LASSERT(ergo(!discard, list_empty(&discard_list)));
	if (!list_empty(&discard_list)) {
		struct osc_extent *tmp;
		int rc;

		osc_list_maint(osc_cli(obj), obj);
		list_for_each_entry_safe(ext, tmp, &discard_list, oe_link) {
			list_del_init(&ext->oe_link);
			EASSERT(ext->oe_state == OES_LOCKING, ext);

			/* Discard caching pages. We don't actually write this
			 * extent out but we complete it as if we did.
			 */
			rc = osc_extent_make_ready(env, ext);
			if (unlikely(rc < 0)) {
				OSC_EXTENT_DUMP(D_ERROR, ext,
						"make_ready returned %d\n", rc);
				if (result >= 0)
					result = rc;
			}

			/* finish the extent as if the pages were sent */
			osc_extent_finish(env, ext, 0, 0);
		}
	}

	if (unplug)
		osc_io_unplug(env, osc_cli(obj), obj);

	if (hp || discard) {
		int rc;

		rc = osc_cache_wait_range(env, obj, start, end);
		if (result >= 0 && rc < 0)
			result = rc;
	}

	OSC_IO_DEBUG(obj, "pageout [%lu, %lu], %d.\n", start, end, result);
	return result;
}

/**
 * Returns a list of pages by a given [start, end] of \a obj.
 *
 * \param resched If not NULL, then we give up before hogging CPU for too
 * long and set *resched = 1, in that case caller should implement a retry
 * logic.
 *
 * Gang tree lookup (radix_tree_gang_lookup()) optimization is absolutely
 * crucial in the face of [offset, EOF] locks.
 *
 * Return at least one page in @queue unless there is no covered page.
 */
int osc_page_gang_lookup(const struct lu_env *env, struct cl_io *io,
			 struct osc_object *osc, pgoff_t start, pgoff_t end,
			 osc_page_gang_cbt cb, void *cbdata)
{
	struct osc_page *ops;
	void            **pvec;
	pgoff_t         idx;
	unsigned int    nr;
	unsigned int    i;
	unsigned int    j;
	int             res = CLP_GANG_OKAY;
	bool            tree_lock = true;

	idx = start;
	pvec = osc_env_info(env)->oti_pvec;
	spin_lock(&osc->oo_tree_lock);
	while ((nr = radix_tree_gang_lookup(&osc->oo_tree, pvec,
					    idx, OTI_PVEC_SIZE)) > 0) {
		struct cl_page *page;
		bool end_of_region = false;

		for (i = 0, j = 0; i < nr; ++i) {
			ops = pvec[i];
			pvec[i] = NULL;

			idx = osc_index(ops);
			if (idx > end) {
				end_of_region = true;
				break;
			}

			page = ops->ops_cl.cpl_page;
			LASSERT(page->cp_type == CPT_CACHEABLE);
			if (page->cp_state == CPS_FREEING)
				continue;

			cl_page_get(page);
			lu_ref_add_atomic(&page->cp_reference,
					  "gang_lookup", current);
			pvec[j++] = ops;
		}
		++idx;

		/*
		 * Here a delicate locking dance is performed. Current thread
		 * holds a reference to a page, but has to own it before it
		 * can be placed into queue. Owning implies waiting, so
		 * radix-tree lock is to be released. After a wait one has to
		 * check that pages weren't truncated (cl_page_own() returns
		 * error in the latter case).
		 */
		spin_unlock(&osc->oo_tree_lock);
		tree_lock = false;

		for (i = 0; i < j; ++i) {
			ops = pvec[i];
			if (res == CLP_GANG_OKAY)
				res = (*cb)(env, io, ops, cbdata);

			page = ops->ops_cl.cpl_page;
			lu_ref_del(&page->cp_reference, "gang_lookup", current);
			cl_page_put(env, page);
		}
		if (nr < OTI_PVEC_SIZE || end_of_region)
			break;

		if (res == CLP_GANG_OKAY && need_resched())
			res = CLP_GANG_RESCHED;
		if (res != CLP_GANG_OKAY)
			break;

		spin_lock(&osc->oo_tree_lock);
		tree_lock = true;
	}
	if (tree_lock)
		spin_unlock(&osc->oo_tree_lock);
	return res;
}

/**
 * Check if page @page is covered by an extra lock or discard it.
 */
static int check_and_discard_cb(const struct lu_env *env, struct cl_io *io,
				struct osc_page *ops, void *cbdata)
{
	struct osc_thread_info *info = osc_env_info(env);
	struct osc_object *osc = cbdata;
	pgoff_t index;

	index = osc_index(ops);
	if (index >= info->oti_fn_index) {
		struct ldlm_lock *tmp;
		struct cl_page *page = ops->ops_cl.cpl_page;

		/* refresh non-overlapped index */
		tmp = osc_dlmlock_at_pgoff(env, osc, index, 0, 0);
		if (tmp) {
			__u64 end = tmp->l_policy_data.l_extent.end;
			/* Cache the first-non-overlapped index so as to skip
			 * all pages within [index, oti_fn_index). This is safe
			 * because if tmp lock is canceled, it will discard
			 * these pages.
			 */
			info->oti_fn_index = cl_index(osc2cl(osc), end + 1);
			if (end == OBD_OBJECT_EOF)
				info->oti_fn_index = CL_PAGE_EOF;
			LDLM_LOCK_PUT(tmp);
		} else if (cl_page_own(env, io, page) == 0) {
			/* discard the page */
			cl_page_discard(env, io, page);
			cl_page_disown(env, io, page);
		} else {
			LASSERT(page->cp_state == CPS_FREEING);
		}
	}

	info->oti_next_index = index + 1;
	return CLP_GANG_OKAY;
}

static int discard_cb(const struct lu_env *env, struct cl_io *io,
		      struct osc_page *ops, void *cbdata)
{
	struct osc_thread_info *info = osc_env_info(env);
	struct cl_page *page = ops->ops_cl.cpl_page;

	/* page is top page. */
	info->oti_next_index = osc_index(ops) + 1;
	if (cl_page_own(env, io, page) == 0) {
		KLASSERT(ergo(page->cp_type == CPT_CACHEABLE,
			      !PageDirty(cl_page_vmpage(page))));

		/* discard the page */
		cl_page_discard(env, io, page);
		cl_page_disown(env, io, page);
	} else {
		LASSERT(page->cp_state == CPS_FREEING);
	}

	return CLP_GANG_OKAY;
}

/**
 * Discard pages protected by the given lock. This function traverses radix
 * tree to find all covering pages and discard them. If a page is being covered
 * by other locks, it should remain in cache.
 *
 * If error happens on any step, the process continues anyway (the reasoning
 * behind this being that lock cancellation cannot be delayed indefinitely).
 */
int osc_lock_discard_pages(const struct lu_env *env, struct osc_object *osc,
			   pgoff_t start, pgoff_t end, enum cl_lock_mode mode)
{
	struct osc_thread_info *info = osc_env_info(env);
	struct cl_io *io = &info->oti_io;
	osc_page_gang_cbt cb;
	int res;
	int result;

	io->ci_obj = cl_object_top(osc2cl(osc));
	io->ci_ignore_layout = 1;
	result = cl_io_init(env, io, CIT_MISC, io->ci_obj);
	if (result != 0)
		goto out;

	cb = mode == CLM_READ ? check_and_discard_cb : discard_cb;
	info->oti_fn_index = start;
	info->oti_next_index = start;
	do {
		res = osc_page_gang_lookup(env, io, osc,
					   info->oti_next_index, end, cb, osc);
		if (info->oti_next_index > end)
			break;

		if (res == CLP_GANG_RESCHED)
			cond_resched();
	} while (res != CLP_GANG_OKAY);
out:
	cl_io_fini(env, io);
	return result;
}

/** @} osc */
