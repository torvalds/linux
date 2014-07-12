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
 * Client Lustre Object.
 *
 *   Author: Nikita Danilov <nikita.danilov@sun.com>
 */

/*
 * Locking.
 *
 *  i_mutex
 *      PG_locked
 *	  ->coh_page_guard
 *	  ->coh_lock_guard
 *	  ->coh_attr_guard
 *	  ->ls_guard
 */

#define DEBUG_SUBSYSTEM S_CLASS

#include "../../include/linux/libcfs/libcfs.h"
/* class_put_type() */
#include <obd_class.h>
#include <obd_support.h>
#include <lustre_fid.h>
#include <linux/list.h>
#include "../../include/linux/libcfs/libcfs_hash.h"	/* for cfs_hash stuff */
#include <cl_object.h>
#include "cl_internal.h"

static struct kmem_cache *cl_env_kmem;

/** Lock class of cl_object_header::coh_page_guard */
static struct lock_class_key cl_page_guard_class;
/** Lock class of cl_object_header::coh_lock_guard */
static struct lock_class_key cl_lock_guard_class;
/** Lock class of cl_object_header::coh_attr_guard */
static struct lock_class_key cl_attr_guard_class;

extern __u32 lu_context_tags_default;
extern __u32 lu_session_tags_default;
/**
 * Initialize cl_object_header.
 */
int cl_object_header_init(struct cl_object_header *h)
{
	int result;

	result = lu_object_header_init(&h->coh_lu);
	if (result == 0) {
		spin_lock_init(&h->coh_page_guard);
		spin_lock_init(&h->coh_lock_guard);
		spin_lock_init(&h->coh_attr_guard);
		lockdep_set_class(&h->coh_page_guard, &cl_page_guard_class);
		lockdep_set_class(&h->coh_lock_guard, &cl_lock_guard_class);
		lockdep_set_class(&h->coh_attr_guard, &cl_attr_guard_class);
		h->coh_pages = 0;
		/* XXX hard coded GFP_* mask. */
		INIT_RADIX_TREE(&h->coh_tree, GFP_ATOMIC);
		INIT_LIST_HEAD(&h->coh_locks);
		h->coh_page_bufsize = ALIGN(sizeof(struct cl_page), 8);
	}
	return result;
}
EXPORT_SYMBOL(cl_object_header_init);

/**
 * Finalize cl_object_header.
 */
void cl_object_header_fini(struct cl_object_header *h)
{
	LASSERT(list_empty(&h->coh_locks));
	lu_object_header_fini(&h->coh_lu);
}
EXPORT_SYMBOL(cl_object_header_fini);

/**
 * Returns a cl_object with a given \a fid.
 *
 * Returns either cached or newly created object. Additional reference on the
 * returned object is acquired.
 *
 * \see lu_object_find(), cl_page_find(), cl_lock_find()
 */
struct cl_object *cl_object_find(const struct lu_env *env,
				 struct cl_device *cd, const struct lu_fid *fid,
				 const struct cl_object_conf *c)
{
	might_sleep();
	return lu2cl(lu_object_find_slice(env, cl2lu_dev(cd), fid, &c->coc_lu));
}
EXPORT_SYMBOL(cl_object_find);

/**
 * Releases a reference on \a o.
 *
 * When last reference is released object is returned to the cache, unless
 * lu_object_header_flags::LU_OBJECT_HEARD_BANSHEE bit is set in its header.
 *
 * \see cl_page_put(), cl_lock_put().
 */
void cl_object_put(const struct lu_env *env, struct cl_object *o)
{
	lu_object_put(env, &o->co_lu);
}
EXPORT_SYMBOL(cl_object_put);

/**
 * Acquire an additional reference to the object \a o.
 *
 * This can only be used to acquire _additional_ reference, i.e., caller
 * already has to possess at least one reference to \a o before calling this.
 *
 * \see cl_page_get(), cl_lock_get().
 */
void cl_object_get(struct cl_object *o)
{
	lu_object_get(&o->co_lu);
}
EXPORT_SYMBOL(cl_object_get);

/**
 * Returns the top-object for a given \a o.
 *
 * \see cl_page_top(), cl_io_top()
 */
struct cl_object *cl_object_top(struct cl_object *o)
{
	struct cl_object_header *hdr = cl_object_header(o);
	struct cl_object *top;

	while (hdr->coh_parent != NULL)
		hdr = hdr->coh_parent;

	top = lu2cl(lu_object_top(&hdr->coh_lu));
	CDEBUG(D_TRACE, "%p -> %p\n", o, top);
	return top;
}
EXPORT_SYMBOL(cl_object_top);

/**
 * Returns pointer to the lock protecting data-attributes for the given object
 * \a o.
 *
 * Data-attributes are protected by the cl_object_header::coh_attr_guard
 * spin-lock in the top-object.
 *
 * \see cl_attr, cl_object_attr_lock(), cl_object_operations::coo_attr_get().
 */
static spinlock_t *cl_object_attr_guard(struct cl_object *o)
{
	return &cl_object_header(cl_object_top(o))->coh_attr_guard;
}

/**
 * Locks data-attributes.
 *
 * Prevents data-attributes from changing, until lock is released by
 * cl_object_attr_unlock(). This has to be called before calls to
 * cl_object_attr_get(), cl_object_attr_set().
 */
void cl_object_attr_lock(struct cl_object *o)
{
	spin_lock(cl_object_attr_guard(o));
}
EXPORT_SYMBOL(cl_object_attr_lock);

/**
 * Releases data-attributes lock, acquired by cl_object_attr_lock().
 */
void cl_object_attr_unlock(struct cl_object *o)
{
	spin_unlock(cl_object_attr_guard(o));
}
EXPORT_SYMBOL(cl_object_attr_unlock);

/**
 * Returns data-attributes of an object \a obj.
 *
 * Every layer is asked (by calling cl_object_operations::coo_attr_get())
 * top-to-bottom to fill in parts of \a attr that this layer is responsible
 * for.
 */
int cl_object_attr_get(const struct lu_env *env, struct cl_object *obj,
		       struct cl_attr *attr)
{
	struct lu_object_header *top;
	int result;

	assert_spin_locked(cl_object_attr_guard(obj));

	top = obj->co_lu.lo_header;
	result = 0;
	list_for_each_entry(obj, &top->loh_layers, co_lu.lo_linkage) {
		if (obj->co_ops->coo_attr_get != NULL) {
			result = obj->co_ops->coo_attr_get(env, obj, attr);
			if (result != 0) {
				if (result > 0)
					result = 0;
				break;
			}
		}
	}
	return result;
}
EXPORT_SYMBOL(cl_object_attr_get);

/**
 * Updates data-attributes of an object \a obj.
 *
 * Only attributes, mentioned in a validness bit-mask \a v are
 * updated. Calls cl_object_operations::coo_attr_set() on every layer, bottom
 * to top.
 */
int cl_object_attr_set(const struct lu_env *env, struct cl_object *obj,
		       const struct cl_attr *attr, unsigned v)
{
	struct lu_object_header *top;
	int result;

	assert_spin_locked(cl_object_attr_guard(obj));

	top = obj->co_lu.lo_header;
	result = 0;
	list_for_each_entry_reverse(obj, &top->loh_layers,
					co_lu.lo_linkage) {
		if (obj->co_ops->coo_attr_set != NULL) {
			result = obj->co_ops->coo_attr_set(env, obj, attr, v);
			if (result != 0) {
				if (result > 0)
					result = 0;
				break;
			}
		}
	}
	return result;
}
EXPORT_SYMBOL(cl_object_attr_set);

/**
 * Notifies layers (bottom-to-top) that glimpse AST was received.
 *
 * Layers have to fill \a lvb fields with information that will be shipped
 * back to glimpse issuer.
 *
 * \see cl_lock_operations::clo_glimpse()
 */
int cl_object_glimpse(const struct lu_env *env, struct cl_object *obj,
		      struct ost_lvb *lvb)
{
	struct lu_object_header *top;
	int result;

	top = obj->co_lu.lo_header;
	result = 0;
	list_for_each_entry_reverse(obj, &top->loh_layers,
					co_lu.lo_linkage) {
		if (obj->co_ops->coo_glimpse != NULL) {
			result = obj->co_ops->coo_glimpse(env, obj, lvb);
			if (result != 0)
				break;
		}
	}
	LU_OBJECT_HEADER(D_DLMTRACE, env, lu_object_top(top),
			 "size: "LPU64" mtime: "LPU64" atime: "LPU64" "
			 "ctime: "LPU64" blocks: "LPU64"\n",
			 lvb->lvb_size, lvb->lvb_mtime, lvb->lvb_atime,
			 lvb->lvb_ctime, lvb->lvb_blocks);
	return result;
}
EXPORT_SYMBOL(cl_object_glimpse);

/**
 * Updates a configuration of an object \a obj.
 */
int cl_conf_set(const struct lu_env *env, struct cl_object *obj,
		const struct cl_object_conf *conf)
{
	struct lu_object_header *top;
	int result;

	top = obj->co_lu.lo_header;
	result = 0;
	list_for_each_entry(obj, &top->loh_layers, co_lu.lo_linkage) {
		if (obj->co_ops->coo_conf_set != NULL) {
			result = obj->co_ops->coo_conf_set(env, obj, conf);
			if (result != 0)
				break;
		}
	}
	return result;
}
EXPORT_SYMBOL(cl_conf_set);

/**
 * Helper function removing all object locks, and marking object for
 * deletion. All object pages must have been deleted at this point.
 *
 * This is called by cl_inode_fini() and lov_object_delete() to destroy top-
 * and sub- objects respectively.
 */
void cl_object_kill(const struct lu_env *env, struct cl_object *obj)
{
	struct cl_object_header *hdr;

	hdr = cl_object_header(obj);
	LASSERT(hdr->coh_tree.rnode == NULL);
	LASSERT(hdr->coh_pages == 0);

	set_bit(LU_OBJECT_HEARD_BANSHEE, &hdr->coh_lu.loh_flags);
	/*
	 * Destroy all locks. Object destruction (including cl_inode_fini())
	 * cannot cancel the locks, because in the case of a local client,
	 * where client and server share the same thread running
	 * prune_icache(), this can dead-lock with ldlm_cancel_handler()
	 * waiting on __wait_on_freeing_inode().
	 */
	cl_locks_prune(env, obj, 0);
}
EXPORT_SYMBOL(cl_object_kill);

/**
 * Prunes caches of pages and locks for this object.
 */
void cl_object_prune(const struct lu_env *env, struct cl_object *obj)
{
	cl_pages_prune(env, obj);
	cl_locks_prune(env, obj, 1);
}
EXPORT_SYMBOL(cl_object_prune);

/**
 * Check if the object has locks.
 */
int cl_object_has_locks(struct cl_object *obj)
{
	struct cl_object_header *head = cl_object_header(obj);
	int has;

	spin_lock(&head->coh_lock_guard);
	has = list_empty(&head->coh_locks);
	spin_unlock(&head->coh_lock_guard);

	return (has == 0);
}
EXPORT_SYMBOL(cl_object_has_locks);

void cache_stats_init(struct cache_stats *cs, const char *name)
{
	int i;

	cs->cs_name = name;
	for (i = 0; i < CS_NR; i++)
		atomic_set(&cs->cs_stats[i], 0);
}

int cache_stats_print(const struct cache_stats *cs, struct seq_file *m, int h)
{
	int i;
	/*
	 *   lookup    hit    total  cached create
	 * env: ...... ...... ...... ...... ......
	 */
	if (h) {
		const char *names[CS_NR] = CS_NAMES;

		seq_printf(m, "%6s", " ");
		for (i = 0; i < CS_NR; i++)
			seq_printf(m, "%8s", names[i]);
		seq_printf(m, "\n");
	}

	seq_printf(m, "%5.5s:", cs->cs_name);
	for (i = 0; i < CS_NR; i++)
		seq_printf(m, "%8u", atomic_read(&cs->cs_stats[i]));
	return 0;
}

/**
 * Initialize client site.
 *
 * Perform common initialization (lu_site_init()), and initialize statistical
 * counters. Also perform global initializations on the first call.
 */
int cl_site_init(struct cl_site *s, struct cl_device *d)
{
	int i;
	int result;

	result = lu_site_init(&s->cs_lu, &d->cd_lu_dev);
	if (result == 0) {
		cache_stats_init(&s->cs_pages, "pages");
		cache_stats_init(&s->cs_locks, "locks");
		for (i = 0; i < ARRAY_SIZE(s->cs_pages_state); ++i)
			atomic_set(&s->cs_pages_state[0], 0);
		for (i = 0; i < ARRAY_SIZE(s->cs_locks_state); ++i)
			atomic_set(&s->cs_locks_state[i], 0);
	}
	return result;
}
EXPORT_SYMBOL(cl_site_init);

/**
 * Finalize client site. Dual to cl_site_init().
 */
void cl_site_fini(struct cl_site *s)
{
	lu_site_fini(&s->cs_lu);
}
EXPORT_SYMBOL(cl_site_fini);

static struct cache_stats cl_env_stats = {
	.cs_name    = "envs",
	.cs_stats = { ATOMIC_INIT(0), }
};

/**
 * Outputs client site statistical counters into a buffer. Suitable for
 * ll_rd_*()-style functions.
 */
int cl_site_stats_print(const struct cl_site *site, struct seq_file *m)
{
	int i;
	static const char *pstate[] = {
		[CPS_CACHED]  = "c",
		[CPS_OWNED]   = "o",
		[CPS_PAGEOUT] = "w",
		[CPS_PAGEIN]  = "r",
		[CPS_FREEING] = "f"
	};
	static const char *lstate[] = {
		[CLS_NEW]       = "n",
		[CLS_QUEUING]   = "q",
		[CLS_ENQUEUED]  = "e",
		[CLS_HELD]      = "h",
		[CLS_INTRANSIT] = "t",
		[CLS_CACHED]    = "c",
		[CLS_FREEING]   = "f"
	};
/*
       lookup    hit  total   busy create
pages: ...... ...... ...... ...... ...... [...... ...... ...... ......]
locks: ...... ...... ...... ...... ...... [...... ...... ...... ...... ......]
  env: ...... ...... ...... ...... ......
 */
	lu_site_stats_print(&site->cs_lu, m);
	cache_stats_print(&site->cs_pages, m, 1);
	seq_printf(m, " [");
	for (i = 0; i < ARRAY_SIZE(site->cs_pages_state); ++i)
		seq_printf(m, "%s: %u ", pstate[i],
				atomic_read(&site->cs_pages_state[i]));
	seq_printf(m, "]\n");
	cache_stats_print(&site->cs_locks, m, 0);
	seq_printf(m, " [");
	for (i = 0; i < ARRAY_SIZE(site->cs_locks_state); ++i)
		seq_printf(m, "%s: %u ", lstate[i],
				atomic_read(&site->cs_locks_state[i]));
	seq_printf(m, "]\n");
	cache_stats_print(&cl_env_stats, m, 0);
	seq_printf(m, "\n");
	return 0;
}
EXPORT_SYMBOL(cl_site_stats_print);

/*****************************************************************************
 *
 * lu_env handling on client.
 *
 */

/**
 * The most efficient way is to store cl_env pointer in task specific
 * structures. On Linux, it wont' be easy to use task_struct->journal_info
 * because Lustre code may call into other fs which has certain assumptions
 * about journal_info. Currently following fields in task_struct are identified
 * can be used for this purpose:
 *  - cl_env: for liblustre.
 *  - tux_info: only on RedHat kernel.
 *  - ...
 * \note As long as we use task_struct to store cl_env, we assume that once
 * called into Lustre, we'll never call into the other part of the kernel
 * which will use those fields in task_struct without explicitly exiting
 * Lustre.
 *
 * If there's no space in task_struct is available, hash will be used.
 * bz20044, bz22683.
 */

struct cl_env {
	void	     *ce_magic;
	struct lu_env     ce_lu;
	struct lu_context ce_ses;

	/**
	 * This allows cl_env to be entered into cl_env_hash which implements
	 * the current thread -> client environment lookup.
	 */
	struct hlist_node  ce_node;
	/**
	 * Owner for the current cl_env.
	 *
	 * If LL_TASK_CL_ENV is defined, this point to the owning current,
	 * only for debugging purpose ;
	 * Otherwise hash is used, and this is the key for cfs_hash.
	 * Now current thread pid is stored. Note using thread pointer would
	 * lead to unbalanced hash because of its specific allocation locality
	 * and could be varied for different platforms and OSes, even different
	 * OS versions.
	 */
	void	     *ce_owner;

	/*
	 * Linkage into global list of all client environments. Used for
	 * garbage collection.
	 */
	struct list_head	ce_linkage;
	/*
	 *
	 */
	int	       ce_ref;
	/*
	 * Debugging field: address of the caller who made original
	 * allocation.
	 */
	void	     *ce_debug;
};

#define CL_ENV_INC(counter)
#define CL_ENV_DEC(counter)

static void cl_env_init0(struct cl_env *cle, void *debug)
{
	LASSERT(cle->ce_ref == 0);
	LASSERT(cle->ce_magic == &cl_env_init0);
	LASSERT(cle->ce_debug == NULL && cle->ce_owner == NULL);

	cle->ce_ref = 1;
	cle->ce_debug = debug;
	CL_ENV_INC(busy);
}


/*
 * The implementation of using hash table to connect cl_env and thread
 */

static struct cfs_hash *cl_env_hash;

static unsigned cl_env_hops_hash(struct cfs_hash *lh,
				 const void *key, unsigned mask)
{
#if BITS_PER_LONG == 64
	return cfs_hash_u64_hash((__u64)key, mask);
#else
	return cfs_hash_u32_hash((__u32)key, mask);
#endif
}

static void *cl_env_hops_obj(struct hlist_node *hn)
{
	struct cl_env *cle = hlist_entry(hn, struct cl_env, ce_node);
	LASSERT(cle->ce_magic == &cl_env_init0);
	return (void *)cle;
}

static int cl_env_hops_keycmp(const void *key, struct hlist_node *hn)
{
	struct cl_env *cle = cl_env_hops_obj(hn);

	LASSERT(cle->ce_owner != NULL);
	return (key == cle->ce_owner);
}

static void cl_env_hops_noop(struct cfs_hash *hs, struct hlist_node *hn)
{
	struct cl_env *cle = hlist_entry(hn, struct cl_env, ce_node);
	LASSERT(cle->ce_magic == &cl_env_init0);
}

static cfs_hash_ops_t cl_env_hops = {
	.hs_hash	= cl_env_hops_hash,
	.hs_key	 = cl_env_hops_obj,
	.hs_keycmp      = cl_env_hops_keycmp,
	.hs_object      = cl_env_hops_obj,
	.hs_get	 = cl_env_hops_noop,
	.hs_put_locked  = cl_env_hops_noop,
};

static inline struct cl_env *cl_env_fetch(void)
{
	struct cl_env *cle;

	cle = cfs_hash_lookup(cl_env_hash, (void *) (long) current->pid);
	LASSERT(ergo(cle, cle->ce_magic == &cl_env_init0));
	return cle;
}

static inline void cl_env_attach(struct cl_env *cle)
{
	if (cle) {
		int rc;

		LASSERT(cle->ce_owner == NULL);
		cle->ce_owner = (void *) (long) current->pid;
		rc = cfs_hash_add_unique(cl_env_hash, cle->ce_owner,
					 &cle->ce_node);
		LASSERT(rc == 0);
	}
}

static inline void cl_env_do_detach(struct cl_env *cle)
{
	void *cookie;

	LASSERT(cle->ce_owner == (void *) (long) current->pid);
	cookie = cfs_hash_del(cl_env_hash, cle->ce_owner,
			      &cle->ce_node);
	LASSERT(cookie == cle);
	cle->ce_owner = NULL;
}

static int cl_env_store_init(void) {
	cl_env_hash = cfs_hash_create("cl_env",
				      HASH_CL_ENV_BITS, HASH_CL_ENV_BITS,
				      HASH_CL_ENV_BKT_BITS, 0,
				      CFS_HASH_MIN_THETA,
				      CFS_HASH_MAX_THETA,
				      &cl_env_hops,
				      CFS_HASH_RW_BKTLOCK);
	return cl_env_hash != NULL ? 0 :-ENOMEM;
}

static void cl_env_store_fini(void) {
	cfs_hash_putref(cl_env_hash);
}


static inline struct cl_env *cl_env_detach(struct cl_env *cle)
{
	if (cle == NULL)
		cle = cl_env_fetch();

	if (cle && cle->ce_owner)
		cl_env_do_detach(cle);

	return cle;
}

static struct lu_env *cl_env_new(__u32 ctx_tags, __u32 ses_tags, void *debug)
{
	struct lu_env *env;
	struct cl_env *cle;

	OBD_SLAB_ALLOC_PTR_GFP(cle, cl_env_kmem, GFP_NOFS);
	if (cle != NULL) {
		int rc;

		INIT_LIST_HEAD(&cle->ce_linkage);
		cle->ce_magic = &cl_env_init0;
		env = &cle->ce_lu;
		rc = lu_env_init(env, LCT_CL_THREAD|ctx_tags);
		if (rc == 0) {
			rc = lu_context_init(&cle->ce_ses,
					     LCT_SESSION | ses_tags);
			if (rc == 0) {
				lu_context_enter(&cle->ce_ses);
				env->le_ses = &cle->ce_ses;
				cl_env_init0(cle, debug);
			} else
				lu_env_fini(env);
		}
		if (rc != 0) {
			OBD_SLAB_FREE_PTR(cle, cl_env_kmem);
			env = ERR_PTR(rc);
		} else {
			CL_ENV_INC(create);
			CL_ENV_INC(total);
		}
	} else
		env = ERR_PTR(-ENOMEM);
	return env;
}

static void cl_env_fini(struct cl_env *cle)
{
	CL_ENV_DEC(total);
	lu_context_fini(&cle->ce_lu.le_ctx);
	lu_context_fini(&cle->ce_ses);
	OBD_SLAB_FREE_PTR(cle, cl_env_kmem);
}

static inline struct cl_env *cl_env_container(struct lu_env *env)
{
	return container_of(env, struct cl_env, ce_lu);
}

struct lu_env *cl_env_peek(int *refcheck)
{
	struct lu_env *env;
	struct cl_env *cle;

	CL_ENV_INC(lookup);

	/* check that we don't go far from untrusted pointer */
	CLASSERT(offsetof(struct cl_env, ce_magic) == 0);

	env = NULL;
	cle = cl_env_fetch();
	if (cle != NULL) {
		CL_ENV_INC(hit);
		env = &cle->ce_lu;
		*refcheck = ++cle->ce_ref;
	}
	CDEBUG(D_OTHER, "%d@%p\n", cle ? cle->ce_ref : 0, cle);
	return env;
}
EXPORT_SYMBOL(cl_env_peek);

/**
 * Returns lu_env: if there already is an environment associated with the
 * current thread, it is returned, otherwise, new environment is allocated.
 *
 * \param refcheck pointer to a counter used to detect environment leaks. In
 * the usual case cl_env_get() and cl_env_put() are called in the same lexical
 * scope and pointer to the same integer is passed as \a refcheck. This is
 * used to detect missed cl_env_put().
 *
 * \see cl_env_put()
 */
struct lu_env *cl_env_get(int *refcheck)
{
	struct lu_env *env;

	env = cl_env_peek(refcheck);
	if (env == NULL) {
		env = cl_env_new(lu_context_tags_default,
				 lu_session_tags_default,
				 __builtin_return_address(0));

		if (!IS_ERR(env)) {
			struct cl_env *cle;

			cle = cl_env_container(env);
			cl_env_attach(cle);
			*refcheck = cle->ce_ref;
			CDEBUG(D_OTHER, "%d@%p\n", cle->ce_ref, cle);
		}
	}
	return env;
}
EXPORT_SYMBOL(cl_env_get);

/**
 * Forces an allocation of a fresh environment with given tags.
 *
 * \see cl_env_get()
 */
struct lu_env *cl_env_alloc(int *refcheck, __u32 tags)
{
	struct lu_env *env;

	LASSERT(cl_env_peek(refcheck) == NULL);
	env = cl_env_new(tags, tags, __builtin_return_address(0));
	if (!IS_ERR(env)) {
		struct cl_env *cle;

		cle = cl_env_container(env);
		*refcheck = cle->ce_ref;
		CDEBUG(D_OTHER, "%d@%p\n", cle->ce_ref, cle);
	}
	return env;
}
EXPORT_SYMBOL(cl_env_alloc);

static void cl_env_exit(struct cl_env *cle)
{
	LASSERT(cle->ce_owner == NULL);
	lu_context_exit(&cle->ce_lu.le_ctx);
	lu_context_exit(&cle->ce_ses);
}

/**
 * Release an environment.
 *
 * Decrement \a env reference counter. When counter drops to 0, nothing in
 * this thread is using environment and it is returned to the allocation
 * cache, or freed straight away, if cache is large enough.
 */
void cl_env_put(struct lu_env *env, int *refcheck)
{
	struct cl_env *cle;

	cle = cl_env_container(env);

	LASSERT(cle->ce_ref > 0);
	LASSERT(ergo(refcheck != NULL, cle->ce_ref == *refcheck));

	CDEBUG(D_OTHER, "%d@%p\n", cle->ce_ref, cle);
	if (--cle->ce_ref == 0) {
		CL_ENV_DEC(busy);
		cl_env_detach(cle);
		cle->ce_debug = NULL;
		cl_env_exit(cle);
		cl_env_fini(cle);
	}
}
EXPORT_SYMBOL(cl_env_put);

/**
 * Declares a point of re-entrancy.
 *
 * \see cl_env_reexit()
 */
void *cl_env_reenter(void)
{
	return cl_env_detach(NULL);
}
EXPORT_SYMBOL(cl_env_reenter);

/**
 * Exits re-entrancy.
 */
void cl_env_reexit(void *cookie)
{
	cl_env_detach(NULL);
	cl_env_attach(cookie);
}
EXPORT_SYMBOL(cl_env_reexit);

/**
 * Setup user-supplied \a env as a current environment. This is to be used to
 * guaranteed that environment exists even when cl_env_get() fails. It is up
 * to user to ensure proper concurrency control.
 *
 * \see cl_env_unplant()
 */
void cl_env_implant(struct lu_env *env, int *refcheck)
{
	struct cl_env *cle = cl_env_container(env);

	LASSERT(cle->ce_ref > 0);

	cl_env_attach(cle);
	cl_env_get(refcheck);
	CDEBUG(D_OTHER, "%d@%p\n", cle->ce_ref, cle);
}
EXPORT_SYMBOL(cl_env_implant);

/**
 * Detach environment installed earlier by cl_env_implant().
 */
void cl_env_unplant(struct lu_env *env, int *refcheck)
{
	struct cl_env *cle = cl_env_container(env);

	LASSERT(cle->ce_ref > 1);

	CDEBUG(D_OTHER, "%d@%p\n", cle->ce_ref, cle);

	cl_env_detach(cle);
	cl_env_put(env, refcheck);
}
EXPORT_SYMBOL(cl_env_unplant);

struct lu_env *cl_env_nested_get(struct cl_env_nest *nest)
{
	struct lu_env *env;

	nest->cen_cookie = NULL;
	env = cl_env_peek(&nest->cen_refcheck);
	if (env != NULL) {
		if (!cl_io_is_going(env))
			return env;
		else {
			cl_env_put(env, &nest->cen_refcheck);
			nest->cen_cookie = cl_env_reenter();
		}
	}
	env = cl_env_get(&nest->cen_refcheck);
	if (IS_ERR(env)) {
		cl_env_reexit(nest->cen_cookie);
		return env;
	}

	LASSERT(!cl_io_is_going(env));
	return env;
}
EXPORT_SYMBOL(cl_env_nested_get);

void cl_env_nested_put(struct cl_env_nest *nest, struct lu_env *env)
{
	cl_env_put(env, &nest->cen_refcheck);
	cl_env_reexit(nest->cen_cookie);
}
EXPORT_SYMBOL(cl_env_nested_put);

/**
 * Converts struct cl_attr to struct ost_lvb.
 *
 * \see cl_lvb2attr
 */
void cl_attr2lvb(struct ost_lvb *lvb, const struct cl_attr *attr)
{
	lvb->lvb_size   = attr->cat_size;
	lvb->lvb_mtime  = attr->cat_mtime;
	lvb->lvb_atime  = attr->cat_atime;
	lvb->lvb_ctime  = attr->cat_ctime;
	lvb->lvb_blocks = attr->cat_blocks;
}
EXPORT_SYMBOL(cl_attr2lvb);

/**
 * Converts struct ost_lvb to struct cl_attr.
 *
 * \see cl_attr2lvb
 */
void cl_lvb2attr(struct cl_attr *attr, const struct ost_lvb *lvb)
{
	attr->cat_size   = lvb->lvb_size;
	attr->cat_mtime  = lvb->lvb_mtime;
	attr->cat_atime  = lvb->lvb_atime;
	attr->cat_ctime  = lvb->lvb_ctime;
	attr->cat_blocks = lvb->lvb_blocks;
}
EXPORT_SYMBOL(cl_lvb2attr);

/*****************************************************************************
 *
 * Temporary prototype thing: mirror obd-devices into cl devices.
 *
 */

struct cl_device *cl_type_setup(const struct lu_env *env, struct lu_site *site,
				struct lu_device_type *ldt,
				struct lu_device *next)
{
	const char       *typename;
	struct lu_device *d;

	LASSERT(ldt != NULL);

	typename = ldt->ldt_name;
	d = ldt->ldt_ops->ldto_device_alloc(env, ldt, NULL);
	if (!IS_ERR(d)) {
		int rc;

		if (site != NULL)
			d->ld_site = site;
		rc = ldt->ldt_ops->ldto_device_init(env, d, typename, next);
		if (rc == 0) {
			lu_device_get(d);
			lu_ref_add(&d->ld_reference,
				   "lu-stack", &lu_site_init);
		} else {
			ldt->ldt_ops->ldto_device_free(env, d);
			CERROR("can't init device '%s', %d\n", typename, rc);
			d = ERR_PTR(rc);
		}
	} else
		CERROR("Cannot allocate device: '%s'\n", typename);
	return lu2cl_dev(d);
}
EXPORT_SYMBOL(cl_type_setup);

/**
 * Finalize device stack by calling lu_stack_fini().
 */
void cl_stack_fini(const struct lu_env *env, struct cl_device *cl)
{
	lu_stack_fini(env, cl2lu_dev(cl));
}
EXPORT_SYMBOL(cl_stack_fini);

int  cl_lock_init(void);
void cl_lock_fini(void);

int  cl_page_init(void);
void cl_page_fini(void);

static struct lu_context_key cl_key;

struct cl_thread_info *cl_env_info(const struct lu_env *env)
{
	return lu_context_key_get(&env->le_ctx, &cl_key);
}

/* defines cl0_key_{init,fini}() */
LU_KEY_INIT_FINI(cl0, struct cl_thread_info);

static void *cl_key_init(const struct lu_context *ctx,
			 struct lu_context_key *key)
{
	struct cl_thread_info *info;

	info = cl0_key_init(ctx, key);
	if (!IS_ERR(info)) {
		int i;

		for (i = 0; i < ARRAY_SIZE(info->clt_counters); ++i)
			lu_ref_init(&info->clt_counters[i].ctc_locks_locked);
	}
	return info;
}

static void cl_key_fini(const struct lu_context *ctx,
			struct lu_context_key *key, void *data)
{
	struct cl_thread_info *info;
	int i;

	info = data;
	for (i = 0; i < ARRAY_SIZE(info->clt_counters); ++i)
		lu_ref_fini(&info->clt_counters[i].ctc_locks_locked);
	cl0_key_fini(ctx, key, data);
}

static void cl_key_exit(const struct lu_context *ctx,
			struct lu_context_key *key, void *data)
{
	struct cl_thread_info *info = data;
	int i;

	for (i = 0; i < ARRAY_SIZE(info->clt_counters); ++i) {
		LASSERT(info->clt_counters[i].ctc_nr_held == 0);
		LASSERT(info->clt_counters[i].ctc_nr_used == 0);
		LASSERT(info->clt_counters[i].ctc_nr_locks_acquired == 0);
		LASSERT(info->clt_counters[i].ctc_nr_locks_locked == 0);
		lu_ref_fini(&info->clt_counters[i].ctc_locks_locked);
		lu_ref_init(&info->clt_counters[i].ctc_locks_locked);
	}
}

static struct lu_context_key cl_key = {
	.lct_tags = LCT_CL_THREAD,
	.lct_init = cl_key_init,
	.lct_fini = cl_key_fini,
	.lct_exit = cl_key_exit
};

static struct lu_kmem_descr cl_object_caches[] = {
	{
		.ckd_cache = &cl_env_kmem,
		.ckd_name  = "cl_env_kmem",
		.ckd_size  = sizeof (struct cl_env)
	},
	{
		.ckd_cache = NULL
	}
};

/**
 * Global initialization of cl-data. Create kmem caches, register
 * lu_context_key's, etc.
 *
 * \see cl_global_fini()
 */
int cl_global_init(void)
{
	int result;

	result = cl_env_store_init();
	if (result)
		return result;

	result = lu_kmem_init(cl_object_caches);
	if (result)
		goto out_store;

	LU_CONTEXT_KEY_INIT(&cl_key);
	result = lu_context_key_register(&cl_key);
	if (result)
		goto out_kmem;

	result = cl_lock_init();
	if (result)
		goto out_context;

	result = cl_page_init();
	if (result)
		goto out_lock;

	return 0;
out_lock:
	cl_lock_fini();
out_context:
	lu_context_key_degister(&cl_key);
out_kmem:
	lu_kmem_fini(cl_object_caches);
out_store:
	cl_env_store_fini();
	return result;
}

/**
 * Finalization of global cl-data. Dual to cl_global_init().
 */
void cl_global_fini(void)
{
	cl_lock_fini();
	cl_page_fini();
	lu_context_key_degister(&cl_key);
	lu_kmem_fini(cl_object_caches);
	cl_env_store_fini();
}
