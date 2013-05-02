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
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/obdclass/lu_object.c
 *
 * Lustre Object.
 * These are the only exported functions, they provide some generic
 * infrastructure for managing object devices
 *
 *   Author: Nikita Danilov <nikita.danilov@sun.com>
 */

#define DEBUG_SUBSYSTEM S_CLASS

#include <linux/libcfs/libcfs.h>

# include <linux/module.h>

/* hash_long() */
#include <linux/libcfs/libcfs_hash.h>
#include <obd_class.h>
#include <obd_support.h>
#include <lustre_disk.h>
#include <lustre_fid.h>
#include <lu_object.h>
#include <lu_ref.h>
#include <linux/list.h>

static void lu_object_free(const struct lu_env *env, struct lu_object *o);

/**
 * Decrease reference counter on object. If last reference is freed, return
 * object to the cache, unless lu_object_is_dying(o) holds. In the latter
 * case, free object immediately.
 */
void lu_object_put(const struct lu_env *env, struct lu_object *o)
{
	struct lu_site_bkt_data *bkt;
	struct lu_object_header *top;
	struct lu_site	  *site;
	struct lu_object	*orig;
	cfs_hash_bd_t	    bd;
	const struct lu_fid     *fid;

	top  = o->lo_header;
	site = o->lo_dev->ld_site;
	orig = o;

	/*
	 * till we have full fids-on-OST implemented anonymous objects
	 * are possible in OSP. such an object isn't listed in the site
	 * so we should not remove it from the site.
	 */
	fid = lu_object_fid(o);
	if (fid_is_zero(fid)) {
		LASSERT(top->loh_hash.next == NULL
			&& top->loh_hash.pprev == NULL);
		LASSERT(list_empty(&top->loh_lru));
		if (!atomic_dec_and_test(&top->loh_ref))
			return;
		list_for_each_entry_reverse(o, &top->loh_layers, lo_linkage) {
			if (o->lo_ops->loo_object_release != NULL)
				o->lo_ops->loo_object_release(env, o);
		}
		lu_object_free(env, orig);
		return;
	}

	cfs_hash_bd_get(site->ls_obj_hash, &top->loh_fid, &bd);
	bkt = cfs_hash_bd_extra_get(site->ls_obj_hash, &bd);

	if (!cfs_hash_bd_dec_and_lock(site->ls_obj_hash, &bd, &top->loh_ref)) {
		if (lu_object_is_dying(top)) {

			/*
			 * somebody may be waiting for this, currently only
			 * used for cl_object, see cl_object_put_last().
			 */
			wake_up_all(&bkt->lsb_marche_funebre);
		}
		return;
	}

	LASSERT(bkt->lsb_busy > 0);
	bkt->lsb_busy--;
	/*
	 * When last reference is released, iterate over object
	 * layers, and notify them that object is no longer busy.
	 */
	list_for_each_entry_reverse(o, &top->loh_layers, lo_linkage) {
		if (o->lo_ops->loo_object_release != NULL)
			o->lo_ops->loo_object_release(env, o);
	}

	if (!lu_object_is_dying(top)) {
		LASSERT(list_empty(&top->loh_lru));
		list_add_tail(&top->loh_lru, &bkt->lsb_lru);
		cfs_hash_bd_unlock(site->ls_obj_hash, &bd, 1);
		return;
	}

	/*
	 * If object is dying (will not be cached), removed it
	 * from hash table and LRU.
	 *
	 * This is done with hash table and LRU lists locked. As the only
	 * way to acquire first reference to previously unreferenced
	 * object is through hash-table lookup (lu_object_find()),
	 * or LRU scanning (lu_site_purge()), that are done under hash-table
	 * and LRU lock, no race with concurrent object lookup is possible
	 * and we can safely destroy object below.
	 */
	if (!test_and_set_bit(LU_OBJECT_UNHASHED, &top->loh_flags))
		cfs_hash_bd_del_locked(site->ls_obj_hash, &bd, &top->loh_hash);
	cfs_hash_bd_unlock(site->ls_obj_hash, &bd, 1);
	/*
	 * Object was already removed from hash and lru above, can
	 * kill it.
	 */
	lu_object_free(env, orig);
}
EXPORT_SYMBOL(lu_object_put);

/**
 * Put object and don't keep in cache. This is temporary solution for
 * multi-site objects when its layering is not constant.
 */
void lu_object_put_nocache(const struct lu_env *env, struct lu_object *o)
{
	set_bit(LU_OBJECT_HEARD_BANSHEE, &o->lo_header->loh_flags);
	return lu_object_put(env, o);
}
EXPORT_SYMBOL(lu_object_put_nocache);

/**
 * Kill the object and take it out of LRU cache.
 * Currently used by client code for layout change.
 */
void lu_object_unhash(const struct lu_env *env, struct lu_object *o)
{
	struct lu_object_header *top;

	top = o->lo_header;
	set_bit(LU_OBJECT_HEARD_BANSHEE, &top->loh_flags);
	if (!test_and_set_bit(LU_OBJECT_UNHASHED, &top->loh_flags)) {
		cfs_hash_t *obj_hash = o->lo_dev->ld_site->ls_obj_hash;
		cfs_hash_bd_t bd;

		cfs_hash_bd_get_and_lock(obj_hash, &top->loh_fid, &bd, 1);
		list_del_init(&top->loh_lru);
		cfs_hash_bd_del_locked(obj_hash, &bd, &top->loh_hash);
		cfs_hash_bd_unlock(obj_hash, &bd, 1);
	}
}
EXPORT_SYMBOL(lu_object_unhash);

/**
 * Allocate new object.
 *
 * This follows object creation protocol, described in the comment within
 * struct lu_device_operations definition.
 */
static struct lu_object *lu_object_alloc(const struct lu_env *env,
					 struct lu_device *dev,
					 const struct lu_fid *f,
					 const struct lu_object_conf *conf)
{
	struct lu_object *scan;
	struct lu_object *top;
	struct list_head *layers;
	int clean;
	int result;
	ENTRY;

	/*
	 * Create top-level object slice. This will also create
	 * lu_object_header.
	 */
	top = dev->ld_ops->ldo_object_alloc(env, NULL, dev);
	if (top == NULL)
		RETURN(ERR_PTR(-ENOMEM));
	if (IS_ERR(top))
		RETURN(top);
	/*
	 * This is the only place where object fid is assigned. It's constant
	 * after this point.
	 */
	top->lo_header->loh_fid = *f;
	layers = &top->lo_header->loh_layers;
	do {
		/*
		 * Call ->loo_object_init() repeatedly, until no more new
		 * object slices are created.
		 */
		clean = 1;
		list_for_each_entry(scan, layers, lo_linkage) {
			if (scan->lo_flags & LU_OBJECT_ALLOCATED)
				continue;
			clean = 0;
			scan->lo_header = top->lo_header;
			result = scan->lo_ops->loo_object_init(env, scan, conf);
			if (result != 0) {
				lu_object_free(env, top);
				RETURN(ERR_PTR(result));
			}
			scan->lo_flags |= LU_OBJECT_ALLOCATED;
		}
	} while (!clean);

	list_for_each_entry_reverse(scan, layers, lo_linkage) {
		if (scan->lo_ops->loo_object_start != NULL) {
			result = scan->lo_ops->loo_object_start(env, scan);
			if (result != 0) {
				lu_object_free(env, top);
				RETURN(ERR_PTR(result));
			}
		}
	}

	lprocfs_counter_incr(dev->ld_site->ls_stats, LU_SS_CREATED);
	RETURN(top);
}

/**
 * Free an object.
 */
static void lu_object_free(const struct lu_env *env, struct lu_object *o)
{
	struct lu_site_bkt_data *bkt;
	struct lu_site	  *site;
	struct lu_object	*scan;
	struct list_head	      *layers;
	struct list_head	       splice;

	site   = o->lo_dev->ld_site;
	layers = &o->lo_header->loh_layers;
	bkt    = lu_site_bkt_from_fid(site, &o->lo_header->loh_fid);
	/*
	 * First call ->loo_object_delete() method to release all resources.
	 */
	list_for_each_entry_reverse(scan, layers, lo_linkage) {
		if (scan->lo_ops->loo_object_delete != NULL)
			scan->lo_ops->loo_object_delete(env, scan);
	}

	/*
	 * Then, splice object layers into stand-alone list, and call
	 * ->loo_object_free() on all layers to free memory. Splice is
	 * necessary, because lu_object_header is freed together with the
	 * top-level slice.
	 */
	INIT_LIST_HEAD(&splice);
	list_splice_init(layers, &splice);
	while (!list_empty(&splice)) {
		/*
		 * Free layers in bottom-to-top order, so that object header
		 * lives as long as possible and ->loo_object_free() methods
		 * can look at its contents.
		 */
		o = container_of0(splice.prev, struct lu_object, lo_linkage);
		list_del_init(&o->lo_linkage);
		LASSERT(o->lo_ops->loo_object_free != NULL);
		o->lo_ops->loo_object_free(env, o);
	}

	if (waitqueue_active(&bkt->lsb_marche_funebre))
		wake_up_all(&bkt->lsb_marche_funebre);
}

/**
 * Free \a nr objects from the cold end of the site LRU list.
 */
int lu_site_purge(const struct lu_env *env, struct lu_site *s, int nr)
{
	struct lu_object_header *h;
	struct lu_object_header *temp;
	struct lu_site_bkt_data *bkt;
	cfs_hash_bd_t	    bd;
	cfs_hash_bd_t	    bd2;
	struct list_head	       dispose;
	int		      did_sth;
	int		      start;
	int		      count;
	int		      bnr;
	int		      i;

	if (OBD_FAIL_CHECK(OBD_FAIL_OBD_NO_LRU))
		RETURN(0);

	INIT_LIST_HEAD(&dispose);
	/*
	 * Under LRU list lock, scan LRU list and move unreferenced objects to
	 * the dispose list, removing them from LRU and hash table.
	 */
	start = s->ls_purge_start;
	bnr = (nr == ~0) ? -1 : nr / CFS_HASH_NBKT(s->ls_obj_hash) + 1;
 again:
	did_sth = 0;
	cfs_hash_for_each_bucket(s->ls_obj_hash, &bd, i) {
		if (i < start)
			continue;
		count = bnr;
		cfs_hash_bd_lock(s->ls_obj_hash, &bd, 1);
		bkt = cfs_hash_bd_extra_get(s->ls_obj_hash, &bd);

		list_for_each_entry_safe(h, temp, &bkt->lsb_lru, loh_lru) {
			LASSERT(atomic_read(&h->loh_ref) == 0);

			cfs_hash_bd_get(s->ls_obj_hash, &h->loh_fid, &bd2);
			LASSERT(bd.bd_bucket == bd2.bd_bucket);

			cfs_hash_bd_del_locked(s->ls_obj_hash,
					       &bd2, &h->loh_hash);
			list_move(&h->loh_lru, &dispose);
			if (did_sth == 0)
				did_sth = 1;

			if (nr != ~0 && --nr == 0)
				break;

			if (count > 0 && --count == 0)
				break;

		}
		cfs_hash_bd_unlock(s->ls_obj_hash, &bd, 1);
		cond_resched();
		/*
		 * Free everything on the dispose list. This is safe against
		 * races due to the reasons described in lu_object_put().
		 */
		while (!list_empty(&dispose)) {
			h = container_of0(dispose.next,
					  struct lu_object_header, loh_lru);
			list_del_init(&h->loh_lru);
			lu_object_free(env, lu_object_top(h));
			lprocfs_counter_incr(s->ls_stats, LU_SS_LRU_PURGED);
		}

		if (nr == 0)
			break;
	}

	if (nr != 0 && did_sth && start != 0) {
		start = 0; /* restart from the first bucket */
		goto again;
	}
	/* race on s->ls_purge_start, but nobody cares */
	s->ls_purge_start = i % CFS_HASH_NBKT(s->ls_obj_hash);

	return nr;
}
EXPORT_SYMBOL(lu_site_purge);

/*
 * Object printing.
 *
 * Code below has to jump through certain loops to output object description
 * into libcfs_debug_msg-based log. The problem is that lu_object_print()
 * composes object description from strings that are parts of _lines_ of
 * output (i.e., strings that are not terminated by newline). This doesn't fit
 * very well into libcfs_debug_msg() interface that assumes that each message
 * supplied to it is a self-contained output line.
 *
 * To work around this, strings are collected in a temporary buffer
 * (implemented as a value of lu_cdebug_key key), until terminating newline
 * character is detected.
 *
 */

enum {
	/**
	 * Maximal line size.
	 *
	 * XXX overflow is not handled correctly.
	 */
	LU_CDEBUG_LINE = 512
};

struct lu_cdebug_data {
	/**
	 * Temporary buffer.
	 */
	char lck_area[LU_CDEBUG_LINE];
};

/* context key constructor/destructor: lu_global_key_init, lu_global_key_fini */
LU_KEY_INIT_FINI(lu_global, struct lu_cdebug_data);

/**
 * Key, holding temporary buffer. This key is registered very early by
 * lu_global_init().
 */
struct lu_context_key lu_global_key = {
	.lct_tags = LCT_MD_THREAD | LCT_DT_THREAD |
		    LCT_MG_THREAD | LCT_CL_THREAD,
	.lct_init = lu_global_key_init,
	.lct_fini = lu_global_key_fini
};

/**
 * Printer function emitting messages through libcfs_debug_msg().
 */
int lu_cdebug_printer(const struct lu_env *env,
		      void *cookie, const char *format, ...)
{
	struct libcfs_debug_msg_data *msgdata = cookie;
	struct lu_cdebug_data	*key;
	int used;
	int complete;
	va_list args;

	va_start(args, format);

	key = lu_context_key_get(&env->le_ctx, &lu_global_key);
	LASSERT(key != NULL);

	used = strlen(key->lck_area);
	complete = format[strlen(format) - 1] == '\n';
	/*
	 * Append new chunk to the buffer.
	 */
	vsnprintf(key->lck_area + used,
		  ARRAY_SIZE(key->lck_area) - used, format, args);
	if (complete) {
		if (cfs_cdebug_show(msgdata->msg_mask, msgdata->msg_subsys))
			libcfs_debug_msg(msgdata, "%s", key->lck_area);
		key->lck_area[0] = 0;
	}
	va_end(args);
	return 0;
}
EXPORT_SYMBOL(lu_cdebug_printer);

/**
 * Print object header.
 */
void lu_object_header_print(const struct lu_env *env, void *cookie,
			    lu_printer_t printer,
			    const struct lu_object_header *hdr)
{
	(*printer)(env, cookie, "header@%p[%#lx, %d, "DFID"%s%s%s]",
		   hdr, hdr->loh_flags, atomic_read(&hdr->loh_ref),
		   PFID(&hdr->loh_fid),
		   hlist_unhashed(&hdr->loh_hash) ? "" : " hash",
		   list_empty((struct list_head *)&hdr->loh_lru) ? \
		   "" : " lru",
		   hdr->loh_attr & LOHA_EXISTS ? " exist":"");
}
EXPORT_SYMBOL(lu_object_header_print);

/**
 * Print human readable representation of the \a o to the \a printer.
 */
void lu_object_print(const struct lu_env *env, void *cookie,
		     lu_printer_t printer, const struct lu_object *o)
{
	static const char ruler[] = "........................................";
	struct lu_object_header *top;
	int depth;

	top = o->lo_header;
	lu_object_header_print(env, cookie, printer, top);
	(*printer)(env, cookie, "{ \n");
	list_for_each_entry(o, &top->loh_layers, lo_linkage) {
		depth = o->lo_depth + 4;

		/*
		 * print `.' \a depth times followed by type name and address
		 */
		(*printer)(env, cookie, "%*.*s%s@%p", depth, depth, ruler,
			   o->lo_dev->ld_type->ldt_name, o);
		if (o->lo_ops->loo_object_print != NULL)
			o->lo_ops->loo_object_print(env, cookie, printer, o);
		(*printer)(env, cookie, "\n");
	}
	(*printer)(env, cookie, "} header@%p\n", top);
}
EXPORT_SYMBOL(lu_object_print);

/**
 * Check object consistency.
 */
int lu_object_invariant(const struct lu_object *o)
{
	struct lu_object_header *top;

	top = o->lo_header;
	list_for_each_entry(o, &top->loh_layers, lo_linkage) {
		if (o->lo_ops->loo_object_invariant != NULL &&
		    !o->lo_ops->loo_object_invariant(o))
			return 0;
	}
	return 1;
}
EXPORT_SYMBOL(lu_object_invariant);

static struct lu_object *htable_lookup(struct lu_site *s,
				       cfs_hash_bd_t *bd,
				       const struct lu_fid *f,
				       wait_queue_t *waiter,
				       __u64 *version)
{
	struct lu_site_bkt_data *bkt;
	struct lu_object_header *h;
	struct hlist_node	*hnode;
	__u64  ver = cfs_hash_bd_version_get(bd);

	if (*version == ver)
		return NULL;

	*version = ver;
	bkt = cfs_hash_bd_extra_get(s->ls_obj_hash, bd);
	/* cfs_hash_bd_peek_locked is a somehow "internal" function
	 * of cfs_hash, it doesn't add refcount on object. */
	hnode = cfs_hash_bd_peek_locked(s->ls_obj_hash, bd, (void *)f);
	if (hnode == NULL) {
		lprocfs_counter_incr(s->ls_stats, LU_SS_CACHE_MISS);
		return NULL;
	}

	h = container_of0(hnode, struct lu_object_header, loh_hash);
	if (likely(!lu_object_is_dying(h))) {
		cfs_hash_get(s->ls_obj_hash, hnode);
		lprocfs_counter_incr(s->ls_stats, LU_SS_CACHE_HIT);
		list_del_init(&h->loh_lru);
		return lu_object_top(h);
	}

	/*
	 * Lookup found an object being destroyed this object cannot be
	 * returned (to assure that references to dying objects are eventually
	 * drained), and moreover, lookup has to wait until object is freed.
	 */

	init_waitqueue_entry_current(waiter);
	add_wait_queue(&bkt->lsb_marche_funebre, waiter);
	set_current_state(TASK_UNINTERRUPTIBLE);
	lprocfs_counter_incr(s->ls_stats, LU_SS_CACHE_DEATH_RACE);
	return ERR_PTR(-EAGAIN);
}

/**
 * Search cache for an object with the fid \a f. If such object is found,
 * return it. Otherwise, create new object, insert it into cache and return
 * it. In any case, additional reference is acquired on the returned object.
 */
struct lu_object *lu_object_find(const struct lu_env *env,
				 struct lu_device *dev, const struct lu_fid *f,
				 const struct lu_object_conf *conf)
{
	return lu_object_find_at(env, dev->ld_site->ls_top_dev, f, conf);
}
EXPORT_SYMBOL(lu_object_find);

static struct lu_object *lu_object_new(const struct lu_env *env,
				       struct lu_device *dev,
				       const struct lu_fid *f,
				       const struct lu_object_conf *conf)
{
	struct lu_object	*o;
	cfs_hash_t	      *hs;
	cfs_hash_bd_t	    bd;
	struct lu_site_bkt_data *bkt;

	o = lu_object_alloc(env, dev, f, conf);
	if (unlikely(IS_ERR(o)))
		return o;

	hs = dev->ld_site->ls_obj_hash;
	cfs_hash_bd_get_and_lock(hs, (void *)f, &bd, 1);
	bkt = cfs_hash_bd_extra_get(hs, &bd);
	cfs_hash_bd_add_locked(hs, &bd, &o->lo_header->loh_hash);
	bkt->lsb_busy++;
	cfs_hash_bd_unlock(hs, &bd, 1);
	return o;
}

/**
 * Core logic of lu_object_find*() functions.
 */
static struct lu_object *lu_object_find_try(const struct lu_env *env,
					    struct lu_device *dev,
					    const struct lu_fid *f,
					    const struct lu_object_conf *conf,
					    wait_queue_t *waiter)
{
	struct lu_object      *o;
	struct lu_object      *shadow;
	struct lu_site	*s;
	cfs_hash_t	    *hs;
	cfs_hash_bd_t	  bd;
	__u64		  version = 0;

	/*
	 * This uses standard index maintenance protocol:
	 *
	 *     - search index under lock, and return object if found;
	 *     - otherwise, unlock index, allocate new object;
	 *     - lock index and search again;
	 *     - if nothing is found (usual case), insert newly created
	 *       object into index;
	 *     - otherwise (race: other thread inserted object), free
	 *       object just allocated.
	 *     - unlock index;
	 *     - return object.
	 *
	 * For "LOC_F_NEW" case, we are sure the object is new established.
	 * It is unnecessary to perform lookup-alloc-lookup-insert, instead,
	 * just alloc and insert directly.
	 *
	 * If dying object is found during index search, add @waiter to the
	 * site wait-queue and return ERR_PTR(-EAGAIN).
	 */
	if (conf != NULL && conf->loc_flags & LOC_F_NEW)
		return lu_object_new(env, dev, f, conf);

	s  = dev->ld_site;
	hs = s->ls_obj_hash;
	cfs_hash_bd_get_and_lock(hs, (void *)f, &bd, 1);
	o = htable_lookup(s, &bd, f, waiter, &version);
	cfs_hash_bd_unlock(hs, &bd, 1);
	if (o != NULL)
		return o;

	/*
	 * Allocate new object. This may result in rather complicated
	 * operations, including fld queries, inode loading, etc.
	 */
	o = lu_object_alloc(env, dev, f, conf);
	if (unlikely(IS_ERR(o)))
		return o;

	LASSERT(lu_fid_eq(lu_object_fid(o), f));

	cfs_hash_bd_lock(hs, &bd, 1);

	shadow = htable_lookup(s, &bd, f, waiter, &version);
	if (likely(shadow == NULL)) {
		struct lu_site_bkt_data *bkt;

		bkt = cfs_hash_bd_extra_get(hs, &bd);
		cfs_hash_bd_add_locked(hs, &bd, &o->lo_header->loh_hash);
		bkt->lsb_busy++;
		cfs_hash_bd_unlock(hs, &bd, 1);
		return o;
	}

	lprocfs_counter_incr(s->ls_stats, LU_SS_CACHE_RACE);
	cfs_hash_bd_unlock(hs, &bd, 1);
	lu_object_free(env, o);
	return shadow;
}

/**
 * Much like lu_object_find(), but top level device of object is specifically
 * \a dev rather than top level device of the site. This interface allows
 * objects of different "stacking" to be created within the same site.
 */
struct lu_object *lu_object_find_at(const struct lu_env *env,
				    struct lu_device *dev,
				    const struct lu_fid *f,
				    const struct lu_object_conf *conf)
{
	struct lu_site_bkt_data *bkt;
	struct lu_object	*obj;
	wait_queue_t	   wait;

	while (1) {
		obj = lu_object_find_try(env, dev, f, conf, &wait);
		if (obj != ERR_PTR(-EAGAIN))
			return obj;
		/*
		 * lu_object_find_try() already added waiter into the
		 * wait queue.
		 */
		waitq_wait(&wait, TASK_UNINTERRUPTIBLE);
		bkt = lu_site_bkt_from_fid(dev->ld_site, (void *)f);
		remove_wait_queue(&bkt->lsb_marche_funebre, &wait);
	}
}
EXPORT_SYMBOL(lu_object_find_at);

/**
 * Find object with given fid, and return its slice belonging to given device.
 */
struct lu_object *lu_object_find_slice(const struct lu_env *env,
				       struct lu_device *dev,
				       const struct lu_fid *f,
				       const struct lu_object_conf *conf)
{
	struct lu_object *top;
	struct lu_object *obj;

	top = lu_object_find(env, dev, f, conf);
	if (!IS_ERR(top)) {
		obj = lu_object_locate(top->lo_header, dev->ld_type);
		if (obj == NULL)
			lu_object_put(env, top);
	} else
		obj = top;
	return obj;
}
EXPORT_SYMBOL(lu_object_find_slice);

/**
 * Global list of all device types.
 */
static LIST_HEAD(lu_device_types);

int lu_device_type_init(struct lu_device_type *ldt)
{
	int result = 0;

	INIT_LIST_HEAD(&ldt->ldt_linkage);
	if (ldt->ldt_ops->ldto_init)
		result = ldt->ldt_ops->ldto_init(ldt);
	if (result == 0)
		list_add(&ldt->ldt_linkage, &lu_device_types);
	return result;
}
EXPORT_SYMBOL(lu_device_type_init);

void lu_device_type_fini(struct lu_device_type *ldt)
{
	list_del_init(&ldt->ldt_linkage);
	if (ldt->ldt_ops->ldto_fini)
		ldt->ldt_ops->ldto_fini(ldt);
}
EXPORT_SYMBOL(lu_device_type_fini);

void lu_types_stop(void)
{
	struct lu_device_type *ldt;

	list_for_each_entry(ldt, &lu_device_types, ldt_linkage) {
		if (ldt->ldt_device_nr == 0 && ldt->ldt_ops->ldto_stop)
			ldt->ldt_ops->ldto_stop(ldt);
	}
}
EXPORT_SYMBOL(lu_types_stop);

/**
 * Global list of all sites on this node
 */
static LIST_HEAD(lu_sites);
static DEFINE_MUTEX(lu_sites_guard);

/**
 * Global environment used by site shrinker.
 */
static struct lu_env lu_shrink_env;

struct lu_site_print_arg {
	struct lu_env   *lsp_env;
	void	    *lsp_cookie;
	lu_printer_t     lsp_printer;
};

static int
lu_site_obj_print(cfs_hash_t *hs, cfs_hash_bd_t *bd,
		  struct hlist_node *hnode, void *data)
{
	struct lu_site_print_arg *arg = (struct lu_site_print_arg *)data;
	struct lu_object_header  *h;

	h = hlist_entry(hnode, struct lu_object_header, loh_hash);
	if (!list_empty(&h->loh_layers)) {
		const struct lu_object *o;

		o = lu_object_top(h);
		lu_object_print(arg->lsp_env, arg->lsp_cookie,
				arg->lsp_printer, o);
	} else {
		lu_object_header_print(arg->lsp_env, arg->lsp_cookie,
				       arg->lsp_printer, h);
	}
	return 0;
}

/**
 * Print all objects in \a s.
 */
void lu_site_print(const struct lu_env *env, struct lu_site *s, void *cookie,
		   lu_printer_t printer)
{
	struct lu_site_print_arg arg = {
		.lsp_env     = (struct lu_env *)env,
		.lsp_cookie  = cookie,
		.lsp_printer = printer,
	};

	cfs_hash_for_each(s->ls_obj_hash, lu_site_obj_print, &arg);
}
EXPORT_SYMBOL(lu_site_print);

enum {
	LU_CACHE_PERCENT_MAX     = 50,
	LU_CACHE_PERCENT_DEFAULT = 20
};

static unsigned int lu_cache_percent = LU_CACHE_PERCENT_DEFAULT;
CFS_MODULE_PARM(lu_cache_percent, "i", int, 0644,
		"Percentage of memory to be used as lu_object cache");

/**
 * Return desired hash table order.
 */
static int lu_htable_order(void)
{
	unsigned long cache_size;
	int bits;

	/*
	 * Calculate hash table size, assuming that we want reasonable
	 * performance when 20% of total memory is occupied by cache of
	 * lu_objects.
	 *
	 * Size of lu_object is (arbitrary) taken as 1K (together with inode).
	 */
	cache_size = num_physpages;

#if BITS_PER_LONG == 32
	/* limit hashtable size for lowmem systems to low RAM */
	if (cache_size > 1 << (30 - PAGE_CACHE_SHIFT))
		cache_size = 1 << (30 - PAGE_CACHE_SHIFT) * 3 / 4;
#endif

	/* clear off unreasonable cache setting. */
	if (lu_cache_percent == 0 || lu_cache_percent > LU_CACHE_PERCENT_MAX) {
		CWARN("obdclass: invalid lu_cache_percent: %u, it must be in"
		      " the range of (0, %u]. Will use default value: %u.\n",
		      lu_cache_percent, LU_CACHE_PERCENT_MAX,
		      LU_CACHE_PERCENT_DEFAULT);

		lu_cache_percent = LU_CACHE_PERCENT_DEFAULT;
	}
	cache_size = cache_size / 100 * lu_cache_percent *
		(PAGE_CACHE_SIZE / 1024);

	for (bits = 1; (1 << bits) < cache_size; ++bits) {
		;
	}
	return bits;
}

static unsigned lu_obj_hop_hash(cfs_hash_t *hs,
				const void *key, unsigned mask)
{
	struct lu_fid  *fid = (struct lu_fid *)key;
	__u32	   hash;

	hash = fid_flatten32(fid);
	hash += (hash >> 4) + (hash << 12); /* mixing oid and seq */
	hash = cfs_hash_long(hash, hs->hs_bkt_bits);

	/* give me another random factor */
	hash -= cfs_hash_long((unsigned long)hs, fid_oid(fid) % 11 + 3);

	hash <<= hs->hs_cur_bits - hs->hs_bkt_bits;
	hash |= (fid_seq(fid) + fid_oid(fid)) & (CFS_HASH_NBKT(hs) - 1);

	return hash & mask;
}

static void *lu_obj_hop_object(struct hlist_node *hnode)
{
	return hlist_entry(hnode, struct lu_object_header, loh_hash);
}

static void *lu_obj_hop_key(struct hlist_node *hnode)
{
	struct lu_object_header *h;

	h = hlist_entry(hnode, struct lu_object_header, loh_hash);
	return &h->loh_fid;
}

static int lu_obj_hop_keycmp(const void *key, struct hlist_node *hnode)
{
	struct lu_object_header *h;

	h = hlist_entry(hnode, struct lu_object_header, loh_hash);
	return lu_fid_eq(&h->loh_fid, (struct lu_fid *)key);
}

static void lu_obj_hop_get(cfs_hash_t *hs, struct hlist_node *hnode)
{
	struct lu_object_header *h;

	h = hlist_entry(hnode, struct lu_object_header, loh_hash);
	if (atomic_add_return(1, &h->loh_ref) == 1) {
		struct lu_site_bkt_data *bkt;
		cfs_hash_bd_t	    bd;

		cfs_hash_bd_get(hs, &h->loh_fid, &bd);
		bkt = cfs_hash_bd_extra_get(hs, &bd);
		bkt->lsb_busy++;
	}
}

static void lu_obj_hop_put_locked(cfs_hash_t *hs, struct hlist_node *hnode)
{
	LBUG(); /* we should never called it */
}

cfs_hash_ops_t lu_site_hash_ops = {
	.hs_hash	= lu_obj_hop_hash,
	.hs_key	 = lu_obj_hop_key,
	.hs_keycmp      = lu_obj_hop_keycmp,
	.hs_object      = lu_obj_hop_object,
	.hs_get	 = lu_obj_hop_get,
	.hs_put_locked  = lu_obj_hop_put_locked,
};

void lu_dev_add_linkage(struct lu_site *s, struct lu_device *d)
{
	spin_lock(&s->ls_ld_lock);
	if (list_empty(&d->ld_linkage))
		list_add(&d->ld_linkage, &s->ls_ld_linkage);
	spin_unlock(&s->ls_ld_lock);
}
EXPORT_SYMBOL(lu_dev_add_linkage);

void lu_dev_del_linkage(struct lu_site *s, struct lu_device *d)
{
	spin_lock(&s->ls_ld_lock);
	list_del_init(&d->ld_linkage);
	spin_unlock(&s->ls_ld_lock);
}
EXPORT_SYMBOL(lu_dev_del_linkage);

/**
 * Initialize site \a s, with \a d as the top level device.
 */
#define LU_SITE_BITS_MIN    12
#define LU_SITE_BITS_MAX    24
/**
 * total 256 buckets, we don't want too many buckets because:
 * - consume too much memory
 * - avoid unbalanced LRU list
 */
#define LU_SITE_BKT_BITS    8

int lu_site_init(struct lu_site *s, struct lu_device *top)
{
	struct lu_site_bkt_data *bkt;
	cfs_hash_bd_t bd;
	char name[16];
	int bits;
	int i;
	ENTRY;

	memset(s, 0, sizeof *s);
	bits = lu_htable_order();
	snprintf(name, 16, "lu_site_%s", top->ld_type->ldt_name);
	for (bits = min(max(LU_SITE_BITS_MIN, bits), LU_SITE_BITS_MAX);
	     bits >= LU_SITE_BITS_MIN; bits--) {
		s->ls_obj_hash = cfs_hash_create(name, bits, bits,
						 bits - LU_SITE_BKT_BITS,
						 sizeof(*bkt), 0, 0,
						 &lu_site_hash_ops,
						 CFS_HASH_SPIN_BKTLOCK |
						 CFS_HASH_NO_ITEMREF |
						 CFS_HASH_DEPTH |
						 CFS_HASH_ASSERT_EMPTY);
		if (s->ls_obj_hash != NULL)
			break;
	}

	if (s->ls_obj_hash == NULL) {
		CERROR("failed to create lu_site hash with bits: %d\n", bits);
		return -ENOMEM;
	}

	cfs_hash_for_each_bucket(s->ls_obj_hash, &bd, i) {
		bkt = cfs_hash_bd_extra_get(s->ls_obj_hash, &bd);
		INIT_LIST_HEAD(&bkt->lsb_lru);
		init_waitqueue_head(&bkt->lsb_marche_funebre);
	}

	s->ls_stats = lprocfs_alloc_stats(LU_SS_LAST_STAT, 0);
	if (s->ls_stats == NULL) {
		cfs_hash_putref(s->ls_obj_hash);
		s->ls_obj_hash = NULL;
		return -ENOMEM;
	}

	lprocfs_counter_init(s->ls_stats, LU_SS_CREATED,
			     0, "created", "created");
	lprocfs_counter_init(s->ls_stats, LU_SS_CACHE_HIT,
			     0, "cache_hit", "cache_hit");
	lprocfs_counter_init(s->ls_stats, LU_SS_CACHE_MISS,
			     0, "cache_miss", "cache_miss");
	lprocfs_counter_init(s->ls_stats, LU_SS_CACHE_RACE,
			     0, "cache_race", "cache_race");
	lprocfs_counter_init(s->ls_stats, LU_SS_CACHE_DEATH_RACE,
			     0, "cache_death_race", "cache_death_race");
	lprocfs_counter_init(s->ls_stats, LU_SS_LRU_PURGED,
			     0, "lru_purged", "lru_purged");

	INIT_LIST_HEAD(&s->ls_linkage);
	s->ls_top_dev = top;
	top->ld_site = s;
	lu_device_get(top);
	lu_ref_add(&top->ld_reference, "site-top", s);

	INIT_LIST_HEAD(&s->ls_ld_linkage);
	spin_lock_init(&s->ls_ld_lock);

	lu_dev_add_linkage(s, top);

	RETURN(0);
}
EXPORT_SYMBOL(lu_site_init);

/**
 * Finalize \a s and release its resources.
 */
void lu_site_fini(struct lu_site *s)
{
	mutex_lock(&lu_sites_guard);
	list_del_init(&s->ls_linkage);
	mutex_unlock(&lu_sites_guard);

	if (s->ls_obj_hash != NULL) {
		cfs_hash_putref(s->ls_obj_hash);
		s->ls_obj_hash = NULL;
	}

	if (s->ls_top_dev != NULL) {
		s->ls_top_dev->ld_site = NULL;
		lu_ref_del(&s->ls_top_dev->ld_reference, "site-top", s);
		lu_device_put(s->ls_top_dev);
		s->ls_top_dev = NULL;
	}

	if (s->ls_stats != NULL)
		lprocfs_free_stats(&s->ls_stats);
}
EXPORT_SYMBOL(lu_site_fini);

/**
 * Called when initialization of stack for this site is completed.
 */
int lu_site_init_finish(struct lu_site *s)
{
	int result;
	mutex_lock(&lu_sites_guard);
	result = lu_context_refill(&lu_shrink_env.le_ctx);
	if (result == 0)
		list_add(&s->ls_linkage, &lu_sites);
	mutex_unlock(&lu_sites_guard);
	return result;
}
EXPORT_SYMBOL(lu_site_init_finish);

/**
 * Acquire additional reference on device \a d
 */
void lu_device_get(struct lu_device *d)
{
	atomic_inc(&d->ld_ref);
}
EXPORT_SYMBOL(lu_device_get);

/**
 * Release reference on device \a d.
 */
void lu_device_put(struct lu_device *d)
{
	LASSERT(atomic_read(&d->ld_ref) > 0);
	atomic_dec(&d->ld_ref);
}
EXPORT_SYMBOL(lu_device_put);

/**
 * Initialize device \a d of type \a t.
 */
int lu_device_init(struct lu_device *d, struct lu_device_type *t)
{
	if (t->ldt_device_nr++ == 0 && t->ldt_ops->ldto_start != NULL)
		t->ldt_ops->ldto_start(t);
	memset(d, 0, sizeof *d);
	atomic_set(&d->ld_ref, 0);
	d->ld_type = t;
	lu_ref_init(&d->ld_reference);
	INIT_LIST_HEAD(&d->ld_linkage);
	return 0;
}
EXPORT_SYMBOL(lu_device_init);

/**
 * Finalize device \a d.
 */
void lu_device_fini(struct lu_device *d)
{
	struct lu_device_type *t;

	t = d->ld_type;
	if (d->ld_obd != NULL) {
		d->ld_obd->obd_lu_dev = NULL;
		d->ld_obd = NULL;
	}

	lu_ref_fini(&d->ld_reference);
	LASSERTF(atomic_read(&d->ld_ref) == 0,
		 "Refcount is %u\n", atomic_read(&d->ld_ref));
	LASSERT(t->ldt_device_nr > 0);
	if (--t->ldt_device_nr == 0 && t->ldt_ops->ldto_stop != NULL)
		t->ldt_ops->ldto_stop(t);
}
EXPORT_SYMBOL(lu_device_fini);

/**
 * Initialize object \a o that is part of compound object \a h and was created
 * by device \a d.
 */
int lu_object_init(struct lu_object *o,
		   struct lu_object_header *h, struct lu_device *d)
{
	memset(o, 0, sizeof *o);
	o->lo_header = h;
	o->lo_dev    = d;
	lu_device_get(d);
	o->lo_dev_ref = lu_ref_add(&d->ld_reference, "lu_object", o);
	INIT_LIST_HEAD(&o->lo_linkage);
	return 0;
}
EXPORT_SYMBOL(lu_object_init);

/**
 * Finalize object and release its resources.
 */
void lu_object_fini(struct lu_object *o)
{
	struct lu_device *dev = o->lo_dev;

	LASSERT(list_empty(&o->lo_linkage));

	if (dev != NULL) {
		lu_ref_del_at(&dev->ld_reference,
			      o->lo_dev_ref , "lu_object", o);
		lu_device_put(dev);
		o->lo_dev = NULL;
	}
}
EXPORT_SYMBOL(lu_object_fini);

/**
 * Add object \a o as first layer of compound object \a h
 *
 * This is typically called by the ->ldo_object_alloc() method of top-level
 * device.
 */
void lu_object_add_top(struct lu_object_header *h, struct lu_object *o)
{
	list_move(&o->lo_linkage, &h->loh_layers);
}
EXPORT_SYMBOL(lu_object_add_top);

/**
 * Add object \a o as a layer of compound object, going after \a before.
 *
 * This is typically called by the ->ldo_object_alloc() method of \a
 * before->lo_dev.
 */
void lu_object_add(struct lu_object *before, struct lu_object *o)
{
	list_move(&o->lo_linkage, &before->lo_linkage);
}
EXPORT_SYMBOL(lu_object_add);

/**
 * Initialize compound object.
 */
int lu_object_header_init(struct lu_object_header *h)
{
	memset(h, 0, sizeof *h);
	atomic_set(&h->loh_ref, 1);
	INIT_HLIST_NODE(&h->loh_hash);
	INIT_LIST_HEAD(&h->loh_lru);
	INIT_LIST_HEAD(&h->loh_layers);
	lu_ref_init(&h->loh_reference);
	return 0;
}
EXPORT_SYMBOL(lu_object_header_init);

/**
 * Finalize compound object.
 */
void lu_object_header_fini(struct lu_object_header *h)
{
	LASSERT(list_empty(&h->loh_layers));
	LASSERT(list_empty(&h->loh_lru));
	LASSERT(hlist_unhashed(&h->loh_hash));
	lu_ref_fini(&h->loh_reference);
}
EXPORT_SYMBOL(lu_object_header_fini);

/**
 * Given a compound object, find its slice, corresponding to the device type
 * \a dtype.
 */
struct lu_object *lu_object_locate(struct lu_object_header *h,
				   const struct lu_device_type *dtype)
{
	struct lu_object *o;

	list_for_each_entry(o, &h->loh_layers, lo_linkage) {
		if (o->lo_dev->ld_type == dtype)
			return o;
	}
	return NULL;
}
EXPORT_SYMBOL(lu_object_locate);



/**
 * Finalize and free devices in the device stack.
 *
 * Finalize device stack by purging object cache, and calling
 * lu_device_type_operations::ldto_device_fini() and
 * lu_device_type_operations::ldto_device_free() on all devices in the stack.
 */
void lu_stack_fini(const struct lu_env *env, struct lu_device *top)
{
	struct lu_site   *site = top->ld_site;
	struct lu_device *scan;
	struct lu_device *next;

	lu_site_purge(env, site, ~0);
	for (scan = top; scan != NULL; scan = next) {
		next = scan->ld_type->ldt_ops->ldto_device_fini(env, scan);
		lu_ref_del(&scan->ld_reference, "lu-stack", &lu_site_init);
		lu_device_put(scan);
	}

	/* purge again. */
	lu_site_purge(env, site, ~0);

	for (scan = top; scan != NULL; scan = next) {
		const struct lu_device_type *ldt = scan->ld_type;
		struct obd_type	     *type;

		next = ldt->ldt_ops->ldto_device_free(env, scan);
		type = ldt->ldt_obd_type;
		if (type != NULL) {
			type->typ_refcnt--;
			class_put_type(type);
		}
	}
}
EXPORT_SYMBOL(lu_stack_fini);

enum {
	/**
	 * Maximal number of tld slots.
	 */
	LU_CONTEXT_KEY_NR = 40
};

static struct lu_context_key *lu_keys[LU_CONTEXT_KEY_NR] = { NULL, };

static DEFINE_SPINLOCK(lu_keys_guard);

/**
 * Global counter incremented whenever key is registered, unregistered,
 * revived or quiesced. This is used to void unnecessary calls to
 * lu_context_refill(). No locking is provided, as initialization and shutdown
 * are supposed to be externally serialized.
 */
static unsigned key_set_version = 0;

/**
 * Register new key.
 */
int lu_context_key_register(struct lu_context_key *key)
{
	int result;
	int i;

	LASSERT(key->lct_init != NULL);
	LASSERT(key->lct_fini != NULL);
	LASSERT(key->lct_tags != 0);
	LASSERT(key->lct_owner != NULL);

	result = -ENFILE;
	spin_lock(&lu_keys_guard);
	for (i = 0; i < ARRAY_SIZE(lu_keys); ++i) {
		if (lu_keys[i] == NULL) {
			key->lct_index = i;
			atomic_set(&key->lct_used, 1);
			lu_keys[i] = key;
			lu_ref_init(&key->lct_reference);
			result = 0;
			++key_set_version;
			break;
		}
	}
	spin_unlock(&lu_keys_guard);
	return result;
}
EXPORT_SYMBOL(lu_context_key_register);

static void key_fini(struct lu_context *ctx, int index)
{
	if (ctx->lc_value != NULL && ctx->lc_value[index] != NULL) {
		struct lu_context_key *key;

		key = lu_keys[index];
		LASSERT(key != NULL);
		LASSERT(key->lct_fini != NULL);
		LASSERT(atomic_read(&key->lct_used) > 1);

		key->lct_fini(ctx, key, ctx->lc_value[index]);
		lu_ref_del(&key->lct_reference, "ctx", ctx);
		atomic_dec(&key->lct_used);

		LASSERT(key->lct_owner != NULL);
		if ((ctx->lc_tags & LCT_NOREF) == 0) {
			LINVRNT(module_refcount(key->lct_owner) > 0);
			module_put(key->lct_owner);
		}
		ctx->lc_value[index] = NULL;
	}
}

/**
 * Deregister key.
 */
void lu_context_key_degister(struct lu_context_key *key)
{
	LASSERT(atomic_read(&key->lct_used) >= 1);
	LINVRNT(0 <= key->lct_index && key->lct_index < ARRAY_SIZE(lu_keys));

	lu_context_key_quiesce(key);

	++key_set_version;
	spin_lock(&lu_keys_guard);
	key_fini(&lu_shrink_env.le_ctx, key->lct_index);
	if (lu_keys[key->lct_index]) {
		lu_keys[key->lct_index] = NULL;
		lu_ref_fini(&key->lct_reference);
	}
	spin_unlock(&lu_keys_guard);

	LASSERTF(atomic_read(&key->lct_used) == 1,
		 "key has instances: %d\n",
		 atomic_read(&key->lct_used));
}
EXPORT_SYMBOL(lu_context_key_degister);

/**
 * Register a number of keys. This has to be called after all keys have been
 * initialized by a call to LU_CONTEXT_KEY_INIT().
 */
int lu_context_key_register_many(struct lu_context_key *k, ...)
{
	struct lu_context_key *key = k;
	va_list args;
	int result;

	va_start(args, k);
	do {
		result = lu_context_key_register(key);
		if (result)
			break;
		key = va_arg(args, struct lu_context_key *);
	} while (key != NULL);
	va_end(args);

	if (result != 0) {
		va_start(args, k);
		while (k != key) {
			lu_context_key_degister(k);
			k = va_arg(args, struct lu_context_key *);
		}
		va_end(args);
	}

	return result;
}
EXPORT_SYMBOL(lu_context_key_register_many);

/**
 * De-register a number of keys. This is a dual to
 * lu_context_key_register_many().
 */
void lu_context_key_degister_many(struct lu_context_key *k, ...)
{
	va_list args;

	va_start(args, k);
	do {
		lu_context_key_degister(k);
		k = va_arg(args, struct lu_context_key*);
	} while (k != NULL);
	va_end(args);
}
EXPORT_SYMBOL(lu_context_key_degister_many);

/**
 * Revive a number of keys.
 */
void lu_context_key_revive_many(struct lu_context_key *k, ...)
{
	va_list args;

	va_start(args, k);
	do {
		lu_context_key_revive(k);
		k = va_arg(args, struct lu_context_key*);
	} while (k != NULL);
	va_end(args);
}
EXPORT_SYMBOL(lu_context_key_revive_many);

/**
 * Quiescent a number of keys.
 */
void lu_context_key_quiesce_many(struct lu_context_key *k, ...)
{
	va_list args;

	va_start(args, k);
	do {
		lu_context_key_quiesce(k);
		k = va_arg(args, struct lu_context_key*);
	} while (k != NULL);
	va_end(args);
}
EXPORT_SYMBOL(lu_context_key_quiesce_many);

/**
 * Return value associated with key \a key in context \a ctx.
 */
void *lu_context_key_get(const struct lu_context *ctx,
			 const struct lu_context_key *key)
{
	LINVRNT(ctx->lc_state == LCS_ENTERED);
	LINVRNT(0 <= key->lct_index && key->lct_index < ARRAY_SIZE(lu_keys));
	LASSERT(lu_keys[key->lct_index] == key);
	return ctx->lc_value[key->lct_index];
}
EXPORT_SYMBOL(lu_context_key_get);

/**
 * List of remembered contexts. XXX document me.
 */
static LIST_HEAD(lu_context_remembered);

/**
 * Destroy \a key in all remembered contexts. This is used to destroy key
 * values in "shared" contexts (like service threads), when a module owning
 * the key is about to be unloaded.
 */
void lu_context_key_quiesce(struct lu_context_key *key)
{
	struct lu_context *ctx;

	if (!(key->lct_tags & LCT_QUIESCENT)) {
		/*
		 * XXX layering violation.
		 */
		key->lct_tags |= LCT_QUIESCENT;
		/*
		 * XXX memory barrier has to go here.
		 */
		spin_lock(&lu_keys_guard);
		list_for_each_entry(ctx, &lu_context_remembered,
					lc_remember)
			key_fini(ctx, key->lct_index);
		spin_unlock(&lu_keys_guard);
		++key_set_version;
	}
}
EXPORT_SYMBOL(lu_context_key_quiesce);

void lu_context_key_revive(struct lu_context_key *key)
{
	key->lct_tags &= ~LCT_QUIESCENT;
	++key_set_version;
}
EXPORT_SYMBOL(lu_context_key_revive);

static void keys_fini(struct lu_context *ctx)
{
	int	i;

	if (ctx->lc_value == NULL)
		return;

	for (i = 0; i < ARRAY_SIZE(lu_keys); ++i)
		key_fini(ctx, i);

	OBD_FREE(ctx->lc_value, ARRAY_SIZE(lu_keys) * sizeof ctx->lc_value[0]);
	ctx->lc_value = NULL;
}

static int keys_fill(struct lu_context *ctx)
{
	int i;

	LINVRNT(ctx->lc_value != NULL);
	for (i = 0; i < ARRAY_SIZE(lu_keys); ++i) {
		struct lu_context_key *key;

		key = lu_keys[i];
		if (ctx->lc_value[i] == NULL && key != NULL &&
		    (key->lct_tags & ctx->lc_tags) &&
		    /*
		     * Don't create values for a LCT_QUIESCENT key, as this
		     * will pin module owning a key.
		     */
		    !(key->lct_tags & LCT_QUIESCENT)) {
			void *value;

			LINVRNT(key->lct_init != NULL);
			LINVRNT(key->lct_index == i);

			value = key->lct_init(ctx, key);
			if (unlikely(IS_ERR(value)))
				return PTR_ERR(value);

			LASSERT(key->lct_owner != NULL);
			if (!(ctx->lc_tags & LCT_NOREF))
				try_module_get(key->lct_owner);
			lu_ref_add_atomic(&key->lct_reference, "ctx", ctx);
			atomic_inc(&key->lct_used);
			/*
			 * This is the only place in the code, where an
			 * element of ctx->lc_value[] array is set to non-NULL
			 * value.
			 */
			ctx->lc_value[i] = value;
			if (key->lct_exit != NULL)
				ctx->lc_tags |= LCT_HAS_EXIT;
		}
		ctx->lc_version = key_set_version;
	}
	return 0;
}

static int keys_init(struct lu_context *ctx)
{
	OBD_ALLOC(ctx->lc_value, ARRAY_SIZE(lu_keys) * sizeof ctx->lc_value[0]);
	if (likely(ctx->lc_value != NULL))
		return keys_fill(ctx);

	return -ENOMEM;
}

/**
 * Initialize context data-structure. Create values for all keys.
 */
int lu_context_init(struct lu_context *ctx, __u32 tags)
{
	int	rc;

	memset(ctx, 0, sizeof *ctx);
	ctx->lc_state = LCS_INITIALIZED;
	ctx->lc_tags = tags;
	if (tags & LCT_REMEMBER) {
		spin_lock(&lu_keys_guard);
		list_add(&ctx->lc_remember, &lu_context_remembered);
		spin_unlock(&lu_keys_guard);
	} else {
		INIT_LIST_HEAD(&ctx->lc_remember);
	}

	rc = keys_init(ctx);
	if (rc != 0)
		lu_context_fini(ctx);

	return rc;
}
EXPORT_SYMBOL(lu_context_init);

/**
 * Finalize context data-structure. Destroy key values.
 */
void lu_context_fini(struct lu_context *ctx)
{
	LINVRNT(ctx->lc_state == LCS_INITIALIZED || ctx->lc_state == LCS_LEFT);
	ctx->lc_state = LCS_FINALIZED;

	if ((ctx->lc_tags & LCT_REMEMBER) == 0) {
		LASSERT(list_empty(&ctx->lc_remember));
		keys_fini(ctx);

	} else { /* could race with key degister */
		spin_lock(&lu_keys_guard);
		keys_fini(ctx);
		list_del_init(&ctx->lc_remember);
		spin_unlock(&lu_keys_guard);
	}
}
EXPORT_SYMBOL(lu_context_fini);

/**
 * Called before entering context.
 */
void lu_context_enter(struct lu_context *ctx)
{
	LINVRNT(ctx->lc_state == LCS_INITIALIZED || ctx->lc_state == LCS_LEFT);
	ctx->lc_state = LCS_ENTERED;
}
EXPORT_SYMBOL(lu_context_enter);

/**
 * Called after exiting from \a ctx
 */
void lu_context_exit(struct lu_context *ctx)
{
	int i;

	LINVRNT(ctx->lc_state == LCS_ENTERED);
	ctx->lc_state = LCS_LEFT;
	if (ctx->lc_tags & LCT_HAS_EXIT && ctx->lc_value != NULL) {
		for (i = 0; i < ARRAY_SIZE(lu_keys); ++i) {
			if (ctx->lc_value[i] != NULL) {
				struct lu_context_key *key;

				key = lu_keys[i];
				LASSERT(key != NULL);
				if (key->lct_exit != NULL)
					key->lct_exit(ctx,
						      key, ctx->lc_value[i]);
			}
		}
	}
}
EXPORT_SYMBOL(lu_context_exit);

/**
 * Allocate for context all missing keys that were registered after context
 * creation. key_set_version is only changed in rare cases when modules
 * are loaded and removed.
 */
int lu_context_refill(struct lu_context *ctx)
{
	return likely(ctx->lc_version == key_set_version) ? 0 : keys_fill(ctx);
}
EXPORT_SYMBOL(lu_context_refill);

/**
 * lu_ctx_tags/lu_ses_tags will be updated if there are new types of
 * obd being added. Currently, this is only used on client side, specifically
 * for echo device client, for other stack (like ptlrpc threads), context are
 * predefined when the lu_device type are registered, during the module probe
 * phase.
 */
__u32 lu_context_tags_default = 0;
__u32 lu_session_tags_default = 0;

void lu_context_tags_update(__u32 tags)
{
	spin_lock(&lu_keys_guard);
	lu_context_tags_default |= tags;
	key_set_version++;
	spin_unlock(&lu_keys_guard);
}
EXPORT_SYMBOL(lu_context_tags_update);

void lu_context_tags_clear(__u32 tags)
{
	spin_lock(&lu_keys_guard);
	lu_context_tags_default &= ~tags;
	key_set_version++;
	spin_unlock(&lu_keys_guard);
}
EXPORT_SYMBOL(lu_context_tags_clear);

void lu_session_tags_update(__u32 tags)
{
	spin_lock(&lu_keys_guard);
	lu_session_tags_default |= tags;
	key_set_version++;
	spin_unlock(&lu_keys_guard);
}
EXPORT_SYMBOL(lu_session_tags_update);

void lu_session_tags_clear(__u32 tags)
{
	spin_lock(&lu_keys_guard);
	lu_session_tags_default &= ~tags;
	key_set_version++;
	spin_unlock(&lu_keys_guard);
}
EXPORT_SYMBOL(lu_session_tags_clear);

int lu_env_init(struct lu_env *env, __u32 tags)
{
	int result;

	env->le_ses = NULL;
	result = lu_context_init(&env->le_ctx, tags);
	if (likely(result == 0))
		lu_context_enter(&env->le_ctx);
	return result;
}
EXPORT_SYMBOL(lu_env_init);

void lu_env_fini(struct lu_env *env)
{
	lu_context_exit(&env->le_ctx);
	lu_context_fini(&env->le_ctx);
	env->le_ses = NULL;
}
EXPORT_SYMBOL(lu_env_fini);

int lu_env_refill(struct lu_env *env)
{
	int result;

	result = lu_context_refill(&env->le_ctx);
	if (result == 0 && env->le_ses != NULL)
		result = lu_context_refill(env->le_ses);
	return result;
}
EXPORT_SYMBOL(lu_env_refill);

/**
 * Currently, this API will only be used by echo client.
 * Because echo client and normal lustre client will share
 * same cl_env cache. So echo client needs to refresh
 * the env context after it get one from the cache, especially
 * when normal client and echo client co-exist in the same client.
 */
int lu_env_refill_by_tags(struct lu_env *env, __u32 ctags,
			  __u32 stags)
{
	int    result;

	if ((env->le_ctx.lc_tags & ctags) != ctags) {
		env->le_ctx.lc_version = 0;
		env->le_ctx.lc_tags |= ctags;
	}

	if (env->le_ses && (env->le_ses->lc_tags & stags) != stags) {
		env->le_ses->lc_version = 0;
		env->le_ses->lc_tags |= stags;
	}

	result = lu_env_refill(env);

	return result;
}
EXPORT_SYMBOL(lu_env_refill_by_tags);

static struct shrinker *lu_site_shrinker = NULL;

typedef struct lu_site_stats{
	unsigned	lss_populated;
	unsigned	lss_max_search;
	unsigned	lss_total;
	unsigned	lss_busy;
} lu_site_stats_t;

static void lu_site_stats_get(cfs_hash_t *hs,
			      lu_site_stats_t *stats, int populated)
{
	cfs_hash_bd_t bd;
	int	   i;

	cfs_hash_for_each_bucket(hs, &bd, i) {
		struct lu_site_bkt_data *bkt = cfs_hash_bd_extra_get(hs, &bd);
		struct hlist_head	*hhead;

		cfs_hash_bd_lock(hs, &bd, 1);
		stats->lss_busy  += bkt->lsb_busy;
		stats->lss_total += cfs_hash_bd_count_get(&bd);
		stats->lss_max_search = max((int)stats->lss_max_search,
					    cfs_hash_bd_depmax_get(&bd));
		if (!populated) {
			cfs_hash_bd_unlock(hs, &bd, 1);
			continue;
		}

		cfs_hash_bd_for_each_hlist(hs, &bd, hhead) {
			if (!hlist_empty(hhead))
				stats->lss_populated++;
		}
		cfs_hash_bd_unlock(hs, &bd, 1);
	}
}


/*
 * There exists a potential lock inversion deadlock scenario when using
 * Lustre on top of ZFS. This occurs between one of ZFS's
 * buf_hash_table.ht_lock's, and Lustre's lu_sites_guard lock. Essentially,
 * thread A will take the lu_sites_guard lock and sleep on the ht_lock,
 * while thread B will take the ht_lock and sleep on the lu_sites_guard
 * lock. Obviously neither thread will wake and drop their respective hold
 * on their lock.
 *
 * To prevent this from happening we must ensure the lu_sites_guard lock is
 * not taken while down this code path. ZFS reliably does not set the
 * __GFP_FS bit in its code paths, so this can be used to determine if it
 * is safe to take the lu_sites_guard lock.
 *
 * Ideally we should accurately return the remaining number of cached
 * objects without taking the  lu_sites_guard lock, but this is not
 * possible in the current implementation.
 */
static int lu_cache_shrink(SHRINKER_ARGS(sc, nr_to_scan, gfp_mask))
{
	lu_site_stats_t stats;
	struct lu_site *s;
	struct lu_site *tmp;
	int cached = 0;
	int remain = shrink_param(sc, nr_to_scan);
	LIST_HEAD(splice);

	if (!(shrink_param(sc, gfp_mask) & __GFP_FS)) {
		if (remain != 0)
			return -1;
		else
			/* We must not take the lu_sites_guard lock when
			 * __GFP_FS is *not* set because of the deadlock
			 * possibility detailed above. Additionally,
			 * since we cannot determine the number of
			 * objects in the cache without taking this
			 * lock, we're in a particularly tough spot. As
			 * a result, we'll just lie and say our cache is
			 * empty. This _should_ be ok, as we can't
			 * reclaim objects when __GFP_FS is *not* set
			 * anyways.
			 */
			return 0;
	}

	CDEBUG(D_INODE, "Shrink %d objects\n", remain);

	mutex_lock(&lu_sites_guard);
	list_for_each_entry_safe(s, tmp, &lu_sites, ls_linkage) {
		if (shrink_param(sc, nr_to_scan) != 0) {
			remain = lu_site_purge(&lu_shrink_env, s, remain);
			/*
			 * Move just shrunk site to the tail of site list to
			 * assure shrinking fairness.
			 */
			list_move_tail(&s->ls_linkage, &splice);
		}

		memset(&stats, 0, sizeof(stats));
		lu_site_stats_get(s->ls_obj_hash, &stats, 0);
		cached += stats.lss_total - stats.lss_busy;
		if (shrink_param(sc, nr_to_scan) && remain <= 0)
			break;
	}
	list_splice(&splice, lu_sites.prev);
	mutex_unlock(&lu_sites_guard);

	cached = (cached / 100) * sysctl_vfs_cache_pressure;
	if (shrink_param(sc, nr_to_scan) == 0)
		CDEBUG(D_INODE, "%d objects cached\n", cached);
	return cached;
}

/*
 * Debugging stuff.
 */

/**
 * Environment to be used in debugger, contains all tags.
 */
struct lu_env lu_debugging_env;

/**
 * Debugging printer function using printk().
 */
int lu_printk_printer(const struct lu_env *env,
		      void *unused, const char *format, ...)
{
	va_list args;

	va_start(args, format);
	vprintk(format, args);
	va_end(args);
	return 0;
}

int lu_debugging_setup(void)
{
	return lu_env_init(&lu_debugging_env, ~0);
}

void lu_context_keys_dump(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(lu_keys); ++i) {
		struct lu_context_key *key;

		key = lu_keys[i];
		if (key != NULL) {
			CERROR("[%d]: %p %x (%p,%p,%p) %d %d \"%s\"@%p\n",
			       i, key, key->lct_tags,
			       key->lct_init, key->lct_fini, key->lct_exit,
			       key->lct_index, atomic_read(&key->lct_used),
			       key->lct_owner ? key->lct_owner->name : "",
			       key->lct_owner);
			lu_ref_print(&key->lct_reference);
		}
	}
}
EXPORT_SYMBOL(lu_context_keys_dump);

/**
 * Initialization of global lu_* data.
 */
int lu_global_init(void)
{
	int result;

	CDEBUG(D_INFO, "Lustre LU module (%p).\n", &lu_keys);

	result = lu_ref_global_init();
	if (result != 0)
		return result;

	LU_CONTEXT_KEY_INIT(&lu_global_key);
	result = lu_context_key_register(&lu_global_key);
	if (result != 0)
		return result;

	/*
	 * At this level, we don't know what tags are needed, so allocate them
	 * conservatively. This should not be too bad, because this
	 * environment is global.
	 */
	mutex_lock(&lu_sites_guard);
	result = lu_env_init(&lu_shrink_env, LCT_SHRINKER);
	mutex_unlock(&lu_sites_guard);
	if (result != 0)
		return result;

	/*
	 * seeks estimation: 3 seeks to read a record from oi, one to read
	 * inode, one for ea. Unfortunately setting this high value results in
	 * lu_object/inode cache consuming all the memory.
	 */
	lu_site_shrinker = set_shrinker(DEFAULT_SEEKS, lu_cache_shrink);
	if (lu_site_shrinker == NULL)
		return -ENOMEM;

	return result;
}

/**
 * Dual to lu_global_init().
 */
void lu_global_fini(void)
{
	if (lu_site_shrinker != NULL) {
		remove_shrinker(lu_site_shrinker);
		lu_site_shrinker = NULL;
	}

	lu_context_key_degister(&lu_global_key);

	/*
	 * Tear shrinker environment down _after_ de-registering
	 * lu_global_key, because the latter has a value in the former.
	 */
	mutex_lock(&lu_sites_guard);
	lu_env_fini(&lu_shrink_env);
	mutex_unlock(&lu_sites_guard);

	lu_ref_global_fini();
}

static __u32 ls_stats_read(struct lprocfs_stats *stats, int idx)
{
#ifdef LPROCFS
	struct lprocfs_counter ret;

	lprocfs_stats_collect(stats, idx, &ret);
	return (__u32)ret.lc_count;
#else
	return 0;
#endif
}

/**
 * Output site statistical counters into a buffer. Suitable for
 * lprocfs_rd_*()-style functions.
 */
int lu_site_stats_print(const struct lu_site *s, char *page, int count)
{
	lu_site_stats_t stats;

	memset(&stats, 0, sizeof(stats));
	lu_site_stats_get(s->ls_obj_hash, &stats, 1);

	return snprintf(page, count, "%d/%d %d/%d %d %d %d %d %d %d %d\n",
			stats.lss_busy,
			stats.lss_total,
			stats.lss_populated,
			CFS_HASH_NHLIST(s->ls_obj_hash),
			stats.lss_max_search,
			ls_stats_read(s->ls_stats, LU_SS_CREATED),
			ls_stats_read(s->ls_stats, LU_SS_CACHE_HIT),
			ls_stats_read(s->ls_stats, LU_SS_CACHE_MISS),
			ls_stats_read(s->ls_stats, LU_SS_CACHE_RACE),
			ls_stats_read(s->ls_stats, LU_SS_CACHE_DEATH_RACE),
			ls_stats_read(s->ls_stats, LU_SS_LRU_PURGED));
}
EXPORT_SYMBOL(lu_site_stats_print);

/**
 * Helper function to initialize a number of kmem slab caches at once.
 */
int lu_kmem_init(struct lu_kmem_descr *caches)
{
	int result;
	struct lu_kmem_descr *iter = caches;

	for (result = 0; iter->ckd_cache != NULL; ++iter) {
		*iter->ckd_cache = kmem_cache_create(iter->ckd_name,
							iter->ckd_size,
							0, 0, NULL);
		if (*iter->ckd_cache == NULL) {
			result = -ENOMEM;
			/* free all previously allocated caches */
			lu_kmem_fini(caches);
			break;
		}
	}
	return result;
}
EXPORT_SYMBOL(lu_kmem_init);

/**
 * Helper function to finalize a number of kmem slab cached at once. Dual to
 * lu_kmem_init().
 */
void lu_kmem_fini(struct lu_kmem_descr *caches)
{
	for (; caches->ckd_cache != NULL; ++caches) {
		if (*caches->ckd_cache != NULL) {
			kmem_cache_destroy(*caches->ckd_cache);
			*caches->ckd_cache = NULL;
		}
	}
}
EXPORT_SYMBOL(lu_kmem_fini);

/**
 * Temporary solution to be able to assign fid in ->do_create()
 * till we have fully-functional OST fids
 */
void lu_object_assign_fid(const struct lu_env *env, struct lu_object *o,
			  const struct lu_fid *fid)
{
	struct lu_site		*s = o->lo_dev->ld_site;
	struct lu_fid		*old = &o->lo_header->loh_fid;
	struct lu_site_bkt_data	*bkt;
	struct lu_object	*shadow;
	wait_queue_t		 waiter;
	cfs_hash_t		*hs;
	cfs_hash_bd_t		 bd;
	__u64			 version = 0;

	LASSERT(fid_is_zero(old));

	hs = s->ls_obj_hash;
	cfs_hash_bd_get_and_lock(hs, (void *)fid, &bd, 1);
	shadow = htable_lookup(s, &bd, fid, &waiter, &version);
	/* supposed to be unique */
	LASSERT(shadow == NULL);
	*old = *fid;
	bkt = cfs_hash_bd_extra_get(hs, &bd);
	cfs_hash_bd_add_locked(hs, &bd, &o->lo_header->loh_hash);
	bkt->lsb_busy++;
	cfs_hash_bd_unlock(hs, &bd, 1);
}
EXPORT_SYMBOL(lu_object_assign_fid);

/**
 * allocates object with 0 (non-assiged) fid
 * XXX: temporary solution to be able to assign fid in ->do_create()
 *      till we have fully-functional OST fids
 */
struct lu_object *lu_object_anon(const struct lu_env *env,
				 struct lu_device *dev,
				 const struct lu_object_conf *conf)
{
	struct lu_fid     fid;
	struct lu_object *o;

	fid_zero(&fid);
	o = lu_object_alloc(env, dev, &fid, conf);

	return o;
}
EXPORT_SYMBOL(lu_object_anon);

struct lu_buf LU_BUF_NULL = {
	.lb_buf = NULL,
	.lb_len = 0
};
EXPORT_SYMBOL(LU_BUF_NULL);

void lu_buf_free(struct lu_buf *buf)
{
	LASSERT(buf);
	if (buf->lb_buf) {
		LASSERT(buf->lb_len > 0);
		OBD_FREE_LARGE(buf->lb_buf, buf->lb_len);
		buf->lb_buf = NULL;
		buf->lb_len = 0;
	}
}
EXPORT_SYMBOL(lu_buf_free);

void lu_buf_alloc(struct lu_buf *buf, int size)
{
	LASSERT(buf);
	LASSERT(buf->lb_buf == NULL);
	LASSERT(buf->lb_len == 0);
	OBD_ALLOC_LARGE(buf->lb_buf, size);
	if (likely(buf->lb_buf))
		buf->lb_len = size;
}
EXPORT_SYMBOL(lu_buf_alloc);

void lu_buf_realloc(struct lu_buf *buf, int size)
{
	lu_buf_free(buf);
	lu_buf_alloc(buf, size);
}
EXPORT_SYMBOL(lu_buf_realloc);

struct lu_buf *lu_buf_check_and_alloc(struct lu_buf *buf, int len)
{
	if (buf->lb_buf == NULL && buf->lb_len == 0)
		lu_buf_alloc(buf, len);

	if ((len > buf->lb_len) && (buf->lb_buf != NULL))
		lu_buf_realloc(buf, len);

	return buf;
}
EXPORT_SYMBOL(lu_buf_check_and_alloc);

/**
 * Increase the size of the \a buf.
 * preserves old data in buffer
 * old buffer remains unchanged on error
 * \retval 0 or -ENOMEM
 */
int lu_buf_check_and_grow(struct lu_buf *buf, int len)
{
	char *ptr;

	if (len <= buf->lb_len)
		return 0;

	OBD_ALLOC_LARGE(ptr, len);
	if (ptr == NULL)
		return -ENOMEM;

	/* Free the old buf */
	if (buf->lb_buf != NULL) {
		memcpy(ptr, buf->lb_buf, buf->lb_len);
		OBD_FREE_LARGE(buf->lb_buf, buf->lb_len);
	}

	buf->lb_buf = ptr;
	buf->lb_len = len;
	return 0;
}
EXPORT_SYMBOL(lu_buf_check_and_grow);
