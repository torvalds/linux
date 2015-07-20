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
 * Client Lustre Page.
 *
 *   Author: Nikita Danilov <nikita.danilov@sun.com>
 */

#define DEBUG_SUBSYSTEM S_CLASS

#include "../../include/linux/libcfs/libcfs.h"
#include "../include/obd_class.h"
#include "../include/obd_support.h"
#include <linux/list.h>

#include "../include/cl_object.h"
#include "cl_internal.h"

static void cl_page_delete0(const struct lu_env *env, struct cl_page *pg,
			    int radix);

# define PASSERT(env, page, expr)				       \
  do {								    \
	  if (unlikely(!(expr))) {				      \
		  CL_PAGE_DEBUG(D_ERROR, (env), (page), #expr "\n");    \
		  LASSERT(0);					   \
	  }							     \
  } while (0)

# define PINVRNT(env, page, exp) \
	((void)sizeof(env), (void)sizeof(page), (void)sizeof !!(exp))

/**
 * Internal version of cl_page_top, it should be called if the page is
 * known to be not freed, says with page referenced, or radix tree lock held,
 * or page owned.
 */
static struct cl_page *cl_page_top_trusted(struct cl_page *page)
{
	while (page->cp_parent != NULL)
		page = page->cp_parent;
	return page;
}

/**
 * Internal version of cl_page_get().
 *
 * This function can be used to obtain initial reference to previously
 * unreferenced cached object. It can be called only if concurrent page
 * reclamation is somehow prevented, e.g., by locking page radix-tree
 * (cl_object_header::hdr->coh_page_guard), or by keeping a lock on a VM page,
 * associated with \a page.
 *
 * Use with care! Not exported.
 */
static void cl_page_get_trust(struct cl_page *page)
{
	LASSERT(atomic_read(&page->cp_ref) > 0);
	atomic_inc(&page->cp_ref);
}

/**
 * Returns a slice within a page, corresponding to the given layer in the
 * device stack.
 *
 * \see cl_lock_at()
 */
static const struct cl_page_slice *
cl_page_at_trusted(const struct cl_page *page,
		   const struct lu_device_type *dtype)
{
	const struct cl_page_slice *slice;

	page = cl_page_top_trusted((struct cl_page *)page);
	do {
		list_for_each_entry(slice, &page->cp_layers, cpl_linkage) {
			if (slice->cpl_obj->co_lu.lo_dev->ld_type == dtype)
				return slice;
		}
		page = page->cp_child;
	} while (page != NULL);
	return NULL;
}

/**
 * Returns a page with given index in the given object, or NULL if no page is
 * found. Acquires a reference on \a page.
 *
 * Locking: called under cl_object_header::coh_page_guard spin-lock.
 */
struct cl_page *cl_page_lookup(struct cl_object_header *hdr, pgoff_t index)
{
	struct cl_page *page;

	assert_spin_locked(&hdr->coh_page_guard);

	page = radix_tree_lookup(&hdr->coh_tree, index);
	if (page != NULL)
		cl_page_get_trust(page);
	return page;
}
EXPORT_SYMBOL(cl_page_lookup);

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
int cl_page_gang_lookup(const struct lu_env *env, struct cl_object *obj,
			struct cl_io *io, pgoff_t start, pgoff_t end,
			cl_page_gang_cb_t cb, void *cbdata)
{
	struct cl_object_header *hdr;
	struct cl_page	  *page;
	struct cl_page	 **pvec;
	const struct cl_page_slice  *slice;
	const struct lu_device_type *dtype;
	pgoff_t		  idx;
	unsigned int	     nr;
	unsigned int	     i;
	unsigned int	     j;
	int		      res = CLP_GANG_OKAY;
	int		      tree_lock = 1;

	idx = start;
	hdr = cl_object_header(obj);
	pvec = cl_env_info(env)->clt_pvec;
	dtype = cl_object_top(obj)->co_lu.lo_dev->ld_type;
	spin_lock(&hdr->coh_page_guard);
	while ((nr = radix_tree_gang_lookup(&hdr->coh_tree, (void **)pvec,
					    idx, CLT_PVEC_SIZE)) > 0) {
		int end_of_region = 0;
		idx = pvec[nr - 1]->cp_index + 1;
		for (i = 0, j = 0; i < nr; ++i) {
			page = pvec[i];
			pvec[i] = NULL;

			LASSERT(page->cp_type == CPT_CACHEABLE);
			if (page->cp_index > end) {
				end_of_region = 1;
				break;
			}
			if (page->cp_state == CPS_FREEING)
				continue;

			slice = cl_page_at_trusted(page, dtype);
			/*
			 * Pages for lsm-less file has no underneath sub-page
			 * for osc, in case of ...
			 */
			PASSERT(env, page, slice != NULL);

			page = slice->cpl_page;
			/*
			 * Can safely call cl_page_get_trust() under
			 * radix-tree spin-lock.
			 *
			 * XXX not true, because @page is from object another
			 * than @hdr and protected by different tree lock.
			 */
			cl_page_get_trust(page);
			lu_ref_add_atomic(&page->cp_reference,
					  "gang_lookup", current);
			pvec[j++] = page;
		}

		/*
		 * Here a delicate locking dance is performed. Current thread
		 * holds a reference to a page, but has to own it before it
		 * can be placed into queue. Owning implies waiting, so
		 * radix-tree lock is to be released. After a wait one has to
		 * check that pages weren't truncated (cl_page_own() returns
		 * error in the latter case).
		 */
		spin_unlock(&hdr->coh_page_guard);
		tree_lock = 0;

		for (i = 0; i < j; ++i) {
			page = pvec[i];
			if (res == CLP_GANG_OKAY)
				res = (*cb)(env, io, page, cbdata);
			lu_ref_del(&page->cp_reference,
				   "gang_lookup", current);
			cl_page_put(env, page);
		}
		if (nr < CLT_PVEC_SIZE || end_of_region)
			break;

		if (res == CLP_GANG_OKAY && need_resched())
			res = CLP_GANG_RESCHED;
		if (res != CLP_GANG_OKAY)
			break;

		spin_lock(&hdr->coh_page_guard);
		tree_lock = 1;
	}
	if (tree_lock)
		spin_unlock(&hdr->coh_page_guard);
	return res;
}
EXPORT_SYMBOL(cl_page_gang_lookup);

static void cl_page_free(const struct lu_env *env, struct cl_page *page)
{
	struct cl_object *obj  = page->cp_obj;

	PASSERT(env, page, list_empty(&page->cp_batch));
	PASSERT(env, page, page->cp_owner == NULL);
	PASSERT(env, page, page->cp_req == NULL);
	PASSERT(env, page, page->cp_parent == NULL);
	PASSERT(env, page, page->cp_state == CPS_FREEING);

	might_sleep();
	while (!list_empty(&page->cp_layers)) {
		struct cl_page_slice *slice;

		slice = list_entry(page->cp_layers.next,
				       struct cl_page_slice, cpl_linkage);
		list_del_init(page->cp_layers.next);
		slice->cpl_ops->cpo_fini(env, slice);
	}
	lu_object_ref_del_at(&obj->co_lu, &page->cp_obj_ref, "cl_page", page);
	cl_object_put(env, obj);
	lu_ref_fini(&page->cp_reference);
	kfree(page);
}

/**
 * Helper function updating page state. This is the only place in the code
 * where cl_page::cp_state field is mutated.
 */
static inline void cl_page_state_set_trust(struct cl_page *page,
					   enum cl_page_state state)
{
	/* bypass const. */
	*(enum cl_page_state *)&page->cp_state = state;
}

static struct cl_page *cl_page_alloc(const struct lu_env *env,
		struct cl_object *o, pgoff_t ind, struct page *vmpage,
		enum cl_page_type type)
{
	struct cl_page	  *page;
	struct lu_object_header *head;

	OBD_ALLOC_GFP(page, cl_object_header(o)->coh_page_bufsize,
			GFP_NOFS);
	if (page != NULL) {
		int result = 0;
		atomic_set(&page->cp_ref, 1);
		if (type == CPT_CACHEABLE) /* for radix tree */
			atomic_inc(&page->cp_ref);
		page->cp_obj = o;
		cl_object_get(o);
		lu_object_ref_add_at(&o->co_lu, &page->cp_obj_ref, "cl_page",
				     page);
		page->cp_index = ind;
		cl_page_state_set_trust(page, CPS_CACHED);
		page->cp_type = type;
		INIT_LIST_HEAD(&page->cp_layers);
		INIT_LIST_HEAD(&page->cp_batch);
		INIT_LIST_HEAD(&page->cp_flight);
		mutex_init(&page->cp_mutex);
		lu_ref_init(&page->cp_reference);
		head = o->co_lu.lo_header;
		list_for_each_entry(o, &head->loh_layers,
					co_lu.lo_linkage) {
			if (o->co_ops->coo_page_init != NULL) {
				result = o->co_ops->coo_page_init(env, o,
								  page, vmpage);
				if (result != 0) {
					cl_page_delete0(env, page, 0);
					cl_page_free(env, page);
					page = ERR_PTR(result);
					break;
				}
			}
		}
	} else {
		page = ERR_PTR(-ENOMEM);
	}
	return page;
}

/**
 * Returns a cl_page with index \a idx at the object \a o, and associated with
 * the VM page \a vmpage.
 *
 * This is the main entry point into the cl_page caching interface. First, a
 * cache (implemented as a per-object radix tree) is consulted. If page is
 * found there, it is returned immediately. Otherwise new page is allocated
 * and returned. In any case, additional reference to page is acquired.
 *
 * \see cl_object_find(), cl_lock_find()
 */
static struct cl_page *cl_page_find0(const struct lu_env *env,
				     struct cl_object *o,
				     pgoff_t idx, struct page *vmpage,
				     enum cl_page_type type,
				     struct cl_page *parent)
{
	struct cl_page	  *page = NULL;
	struct cl_page	  *ghost = NULL;
	struct cl_object_header *hdr;
	int err;

	LASSERT(type == CPT_CACHEABLE || type == CPT_TRANSIENT);
	might_sleep();

	hdr = cl_object_header(o);

	CDEBUG(D_PAGE, "%lu@"DFID" %p %lx %d\n",
	       idx, PFID(&hdr->coh_lu.loh_fid), vmpage, vmpage->private, type);
	/* fast path. */
	if (type == CPT_CACHEABLE) {
		/* vmpage lock is used to protect the child/parent
		 * relationship */
		KLASSERT(PageLocked(vmpage));
		/*
		 * cl_vmpage_page() can be called here without any locks as
		 *
		 *     - "vmpage" is locked (which prevents ->private from
		 *       concurrent updates), and
		 *
		 *     - "o" cannot be destroyed while current thread holds a
		 *       reference on it.
		 */
		page = cl_vmpage_page(vmpage, o);
		PINVRNT(env, page,
			ergo(page != NULL,
			     cl_page_vmpage(env, page) == vmpage &&
			     (void *)radix_tree_lookup(&hdr->coh_tree,
						       idx) == page));
	}

	if (page != NULL) {
		return page;
	}

	/* allocate and initialize cl_page */
	page = cl_page_alloc(env, o, idx, vmpage, type);
	if (IS_ERR(page))
		return page;

	if (type == CPT_TRANSIENT) {
		if (parent) {
			LASSERT(page->cp_parent == NULL);
			page->cp_parent = parent;
			parent->cp_child = page;
		}
		return page;
	}

	/*
	 * XXX optimization: use radix_tree_preload() here, and change tree
	 * gfp mask to GFP_KERNEL in cl_object_header_init().
	 */
	spin_lock(&hdr->coh_page_guard);
	err = radix_tree_insert(&hdr->coh_tree, idx, page);
	if (err != 0) {
		ghost = page;
		/*
		 * Noted by Jay: a lock on \a vmpage protects cl_page_find()
		 * from this race, but
		 *
		 *     0. it's better to have cl_page interface "locally
		 *     consistent" so that its correctness can be reasoned
		 *     about without appealing to the (obscure world of) VM
		 *     locking.
		 *
		 *     1. handling this race allows ->coh_tree to remain
		 *     consistent even when VM locking is somehow busted,
		 *     which is very useful during diagnosing and debugging.
		 */
		page = ERR_PTR(err);
		CL_PAGE_DEBUG(D_ERROR, env, ghost,
			      "fail to insert into radix tree: %d\n", err);
	} else {
		if (parent) {
			LASSERT(page->cp_parent == NULL);
			page->cp_parent = parent;
			parent->cp_child = page;
		}
		hdr->coh_pages++;
	}
	spin_unlock(&hdr->coh_page_guard);

	if (unlikely(ghost != NULL)) {
		cl_page_delete0(env, ghost, 0);
		cl_page_free(env, ghost);
	}
	return page;
}

struct cl_page *cl_page_find(const struct lu_env *env, struct cl_object *o,
			     pgoff_t idx, struct page *vmpage,
			     enum cl_page_type type)
{
	return cl_page_find0(env, o, idx, vmpage, type, NULL);
}
EXPORT_SYMBOL(cl_page_find);


struct cl_page *cl_page_find_sub(const struct lu_env *env, struct cl_object *o,
				 pgoff_t idx, struct page *vmpage,
				 struct cl_page *parent)
{
	return cl_page_find0(env, o, idx, vmpage, parent->cp_type, parent);
}
EXPORT_SYMBOL(cl_page_find_sub);

static inline int cl_page_invariant(const struct cl_page *pg)
{
	struct cl_object_header *header;
	struct cl_page	  *parent;
	struct cl_page	  *child;
	struct cl_io	    *owner;

	/*
	 * Page invariant is protected by a VM lock.
	 */
	LINVRNT(cl_page_is_vmlocked(NULL, pg));

	header = cl_object_header(pg->cp_obj);
	parent = pg->cp_parent;
	child  = pg->cp_child;
	owner  = pg->cp_owner;

	return cl_page_in_use(pg) &&
		ergo(parent != NULL, parent->cp_child == pg) &&
		ergo(child != NULL, child->cp_parent == pg) &&
		ergo(child != NULL, pg->cp_obj != child->cp_obj) &&
		ergo(parent != NULL, pg->cp_obj != parent->cp_obj) &&
		ergo(owner != NULL && parent != NULL,
		     parent->cp_owner == pg->cp_owner->ci_parent) &&
		ergo(owner != NULL && child != NULL,
		     child->cp_owner->ci_parent == owner) &&
		/*
		 * Either page is early in initialization (has neither child
		 * nor parent yet), or it is in the object radix tree.
		 */
		ergo(pg->cp_state < CPS_FREEING && pg->cp_type == CPT_CACHEABLE,
		     (void *)radix_tree_lookup(&header->coh_tree,
					       pg->cp_index) == pg ||
		     (child == NULL && parent == NULL));
}

static void cl_page_state_set0(const struct lu_env *env,
			       struct cl_page *page, enum cl_page_state state)
{
	enum cl_page_state old;

	/*
	 * Matrix of allowed state transitions [old][new], for sanity
	 * checking.
	 */
	static const int allowed_transitions[CPS_NR][CPS_NR] = {
		[CPS_CACHED] = {
			[CPS_CACHED]  = 0,
			[CPS_OWNED]   = 1, /* io finds existing cached page */
			[CPS_PAGEIN]  = 0,
			[CPS_PAGEOUT] = 1, /* write-out from the cache */
			[CPS_FREEING] = 1, /* eviction on the memory pressure */
		},
		[CPS_OWNED] = {
			[CPS_CACHED]  = 1, /* release to the cache */
			[CPS_OWNED]   = 0,
			[CPS_PAGEIN]  = 1, /* start read immediately */
			[CPS_PAGEOUT] = 1, /* start write immediately */
			[CPS_FREEING] = 1, /* lock invalidation or truncate */
		},
		[CPS_PAGEIN] = {
			[CPS_CACHED]  = 1, /* io completion */
			[CPS_OWNED]   = 0,
			[CPS_PAGEIN]  = 0,
			[CPS_PAGEOUT] = 0,
			[CPS_FREEING] = 0,
		},
		[CPS_PAGEOUT] = {
			[CPS_CACHED]  = 1, /* io completion */
			[CPS_OWNED]   = 0,
			[CPS_PAGEIN]  = 0,
			[CPS_PAGEOUT] = 0,
			[CPS_FREEING] = 0,
		},
		[CPS_FREEING] = {
			[CPS_CACHED]  = 0,
			[CPS_OWNED]   = 0,
			[CPS_PAGEIN]  = 0,
			[CPS_PAGEOUT] = 0,
			[CPS_FREEING] = 0,
		}
	};

	old = page->cp_state;
	PASSERT(env, page, allowed_transitions[old][state]);
	CL_PAGE_HEADER(D_TRACE, env, page, "%d -> %d\n", old, state);
	for (; page != NULL; page = page->cp_child) {
		PASSERT(env, page, page->cp_state == old);
		PASSERT(env, page,
			equi(state == CPS_OWNED, page->cp_owner != NULL));

		cl_page_state_set_trust(page, state);
	}
}

static void cl_page_state_set(const struct lu_env *env,
			      struct cl_page *page, enum cl_page_state state)
{
	cl_page_state_set0(env, page, state);
}

/**
 * Acquires an additional reference to a page.
 *
 * This can be called only by caller already possessing a reference to \a
 * page.
 *
 * \see cl_object_get(), cl_lock_get().
 */
void cl_page_get(struct cl_page *page)
{
	cl_page_get_trust(page);
}
EXPORT_SYMBOL(cl_page_get);

/**
 * Releases a reference to a page.
 *
 * When last reference is released, page is returned to the cache, unless it
 * is in cl_page_state::CPS_FREEING state, in which case it is immediately
 * destroyed.
 *
 * \see cl_object_put(), cl_lock_put().
 */
void cl_page_put(const struct lu_env *env, struct cl_page *page)
{
	PASSERT(env, page, atomic_read(&page->cp_ref) > !!page->cp_parent);

	CL_PAGE_HEADER(D_TRACE, env, page, "%d\n",
		       atomic_read(&page->cp_ref));

	if (atomic_dec_and_test(&page->cp_ref)) {
		LASSERT(page->cp_state == CPS_FREEING);

		LASSERT(atomic_read(&page->cp_ref) == 0);
		PASSERT(env, page, page->cp_owner == NULL);
		PASSERT(env, page, list_empty(&page->cp_batch));
		/*
		 * Page is no longer reachable by other threads. Tear
		 * it down.
		 */
		cl_page_free(env, page);
	}
}
EXPORT_SYMBOL(cl_page_put);

/**
 * Returns a VM page associated with a given cl_page.
 */
struct page *cl_page_vmpage(const struct lu_env *env, struct cl_page *page)
{
	const struct cl_page_slice *slice;

	/*
	 * Find uppermost layer with ->cpo_vmpage() method, and return its
	 * result.
	 */
	page = cl_page_top(page);
	do {
		list_for_each_entry(slice, &page->cp_layers, cpl_linkage) {
			if (slice->cpl_ops->cpo_vmpage != NULL)
				return slice->cpl_ops->cpo_vmpage(env, slice);
		}
		page = page->cp_child;
	} while (page != NULL);
	LBUG(); /* ->cpo_vmpage() has to be defined somewhere in the stack */
}
EXPORT_SYMBOL(cl_page_vmpage);

/**
 * Returns a cl_page associated with a VM page, and given cl_object.
 */
struct cl_page *cl_vmpage_page(struct page *vmpage, struct cl_object *obj)
{
	struct cl_page *top;
	struct cl_page *page;

	KLASSERT(PageLocked(vmpage));

	/*
	 * NOTE: absence of races and liveness of data are guaranteed by page
	 *       lock on a "vmpage". That works because object destruction has
	 *       bottom-to-top pass.
	 */

	/*
	 * This loop assumes that ->private points to the top-most page. This
	 * can be rectified easily.
	 */
	top = (struct cl_page *)vmpage->private;
	if (top == NULL)
		return NULL;

	for (page = top; page != NULL; page = page->cp_child) {
		if (cl_object_same(page->cp_obj, obj)) {
			cl_page_get_trust(page);
			break;
		}
	}
	LASSERT(ergo(page, page->cp_type == CPT_CACHEABLE));
	return page;
}
EXPORT_SYMBOL(cl_vmpage_page);

/**
 * Returns the top-page for a given page.
 *
 * \see cl_object_top(), cl_io_top()
 */
struct cl_page *cl_page_top(struct cl_page *page)
{
	return cl_page_top_trusted(page);
}
EXPORT_SYMBOL(cl_page_top);

const struct cl_page_slice *cl_page_at(const struct cl_page *page,
				       const struct lu_device_type *dtype)
{
	return cl_page_at_trusted(page, dtype);
}
EXPORT_SYMBOL(cl_page_at);

#define CL_PAGE_OP(opname) offsetof(struct cl_page_operations, opname)

#define CL_PAGE_INVOKE(_env, _page, _op, _proto, ...)		   \
({								      \
	const struct lu_env	*__env  = (_env);		    \
	struct cl_page	     *__page = (_page);		   \
	const struct cl_page_slice *__scan;			     \
	int			 __result;			   \
	ptrdiff_t		   __op   = (_op);		     \
	int		       (*__method)_proto;		    \
									\
	__result = 0;						   \
	__page = cl_page_top(__page);				   \
	do {							    \
		list_for_each_entry(__scan, &__page->cp_layers,     \
					cpl_linkage) {		  \
			__method = *(void **)((char *)__scan->cpl_ops + \
					      __op);		    \
			if (__method != NULL) {			 \
				__result = (*__method)(__env, __scan,   \
						       ## __VA_ARGS__); \
				if (__result != 0)		      \
					break;			  \
			}					       \
		}						       \
		__page = __page->cp_child;			      \
	} while (__page != NULL && __result == 0);		      \
	if (__result > 0)					       \
		__result = 0;					   \
	__result;						       \
})

#define CL_PAGE_INVOID(_env, _page, _op, _proto, ...)		   \
do {								    \
	const struct lu_env	*__env  = (_env);		    \
	struct cl_page	     *__page = (_page);		   \
	const struct cl_page_slice *__scan;			     \
	ptrdiff_t		   __op   = (_op);		     \
	void		      (*__method)_proto;		    \
									\
	__page = cl_page_top(__page);				   \
	do {							    \
		list_for_each_entry(__scan, &__page->cp_layers,     \
					cpl_linkage) {		  \
			__method = *(void **)((char *)__scan->cpl_ops + \
					      __op);		    \
			if (__method != NULL)			   \
				(*__method)(__env, __scan,	      \
					    ## __VA_ARGS__);	    \
		}						       \
		__page = __page->cp_child;			      \
	} while (__page != NULL);				       \
} while (0)

#define CL_PAGE_INVOID_REVERSE(_env, _page, _op, _proto, ...)	       \
do {									\
	const struct lu_env	*__env  = (_env);			\
	struct cl_page	     *__page = (_page);		       \
	const struct cl_page_slice *__scan;				 \
	ptrdiff_t		   __op   = (_op);			 \
	void		      (*__method)_proto;			\
									    \
	/* get to the bottom page. */				       \
	while (__page->cp_child != NULL)				    \
		__page = __page->cp_child;				  \
	do {								\
		list_for_each_entry_reverse(__scan, &__page->cp_layers, \
						cpl_linkage) {	      \
			__method = *(void **)((char *)__scan->cpl_ops +     \
					      __op);			\
			if (__method != NULL)			       \
				(*__method)(__env, __scan,		  \
					    ## __VA_ARGS__);		\
		}							   \
		__page = __page->cp_parent;				 \
	} while (__page != NULL);					   \
} while (0)

static int cl_page_invoke(const struct lu_env *env,
			  struct cl_io *io, struct cl_page *page, ptrdiff_t op)

{
	PINVRNT(env, page, cl_object_same(page->cp_obj, io->ci_obj));
	return CL_PAGE_INVOKE(env, page, op,
			      (const struct lu_env *,
			       const struct cl_page_slice *, struct cl_io *),
			      io);
}

static void cl_page_invoid(const struct lu_env *env,
			   struct cl_io *io, struct cl_page *page, ptrdiff_t op)

{
	PINVRNT(env, page, cl_object_same(page->cp_obj, io->ci_obj));
	CL_PAGE_INVOID(env, page, op,
		       (const struct lu_env *,
			const struct cl_page_slice *, struct cl_io *), io);
}

static void cl_page_owner_clear(struct cl_page *page)
{
	for (page = cl_page_top(page); page != NULL; page = page->cp_child) {
		if (page->cp_owner != NULL) {
			LASSERT(page->cp_owner->ci_owned_nr > 0);
			page->cp_owner->ci_owned_nr--;
			page->cp_owner = NULL;
			page->cp_task = NULL;
		}
	}
}

static void cl_page_owner_set(struct cl_page *page)
{
	for (page = cl_page_top(page); page != NULL; page = page->cp_child) {
		LASSERT(page->cp_owner != NULL);
		page->cp_owner->ci_owned_nr++;
	}
}

void cl_page_disown0(const struct lu_env *env,
		     struct cl_io *io, struct cl_page *pg)
{
	enum cl_page_state state;

	state = pg->cp_state;
	PINVRNT(env, pg, state == CPS_OWNED || state == CPS_FREEING);
	PINVRNT(env, pg, cl_page_invariant(pg));
	cl_page_owner_clear(pg);

	if (state == CPS_OWNED)
		cl_page_state_set(env, pg, CPS_CACHED);
	/*
	 * Completion call-backs are executed in the bottom-up order, so that
	 * uppermost layer (llite), responsible for VFS/VM interaction runs
	 * last and can release locks safely.
	 */
	CL_PAGE_INVOID_REVERSE(env, pg, CL_PAGE_OP(cpo_disown),
			       (const struct lu_env *,
				const struct cl_page_slice *, struct cl_io *),
			       io);
}

/**
 * returns true, iff page is owned by the given io.
 */
int cl_page_is_owned(const struct cl_page *pg, const struct cl_io *io)
{
	LINVRNT(cl_object_same(pg->cp_obj, io->ci_obj));
	return pg->cp_state == CPS_OWNED && pg->cp_owner == io;
}
EXPORT_SYMBOL(cl_page_is_owned);

/**
 * Try to own a page by IO.
 *
 * Waits until page is in cl_page_state::CPS_CACHED state, and then switch it
 * into cl_page_state::CPS_OWNED state.
 *
 * \pre  !cl_page_is_owned(pg, io)
 * \post result == 0 iff cl_page_is_owned(pg, io)
 *
 * \retval 0   success
 *
 * \retval -ve failure, e.g., page was destroyed (and landed in
 *	     cl_page_state::CPS_FREEING instead of cl_page_state::CPS_CACHED).
 *	     or, page was owned by another thread, or in IO.
 *
 * \see cl_page_disown()
 * \see cl_page_operations::cpo_own()
 * \see cl_page_own_try()
 * \see cl_page_own
 */
static int cl_page_own0(const struct lu_env *env, struct cl_io *io,
			struct cl_page *pg, int nonblock)
{
	int result;

	PINVRNT(env, pg, !cl_page_is_owned(pg, io));

	pg = cl_page_top(pg);
	io = cl_io_top(io);

	if (pg->cp_state == CPS_FREEING) {
		result = -ENOENT;
	} else {
		result = CL_PAGE_INVOKE(env, pg, CL_PAGE_OP(cpo_own),
					(const struct lu_env *,
					 const struct cl_page_slice *,
					 struct cl_io *, int),
					io, nonblock);
		if (result == 0) {
			PASSERT(env, pg, pg->cp_owner == NULL);
			PASSERT(env, pg, pg->cp_req == NULL);
			pg->cp_owner = io;
			pg->cp_task  = current;
			cl_page_owner_set(pg);
			if (pg->cp_state != CPS_FREEING) {
				cl_page_state_set(env, pg, CPS_OWNED);
			} else {
				cl_page_disown0(env, io, pg);
				result = -ENOENT;
			}
		}
	}
	PINVRNT(env, pg, ergo(result == 0, cl_page_invariant(pg)));
	return result;
}

/**
 * Own a page, might be blocked.
 *
 * \see cl_page_own0()
 */
int cl_page_own(const struct lu_env *env, struct cl_io *io, struct cl_page *pg)
{
	return cl_page_own0(env, io, pg, 0);
}
EXPORT_SYMBOL(cl_page_own);

/**
 * Nonblock version of cl_page_own().
 *
 * \see cl_page_own0()
 */
int cl_page_own_try(const struct lu_env *env, struct cl_io *io,
		    struct cl_page *pg)
{
	return cl_page_own0(env, io, pg, 1);
}
EXPORT_SYMBOL(cl_page_own_try);


/**
 * Assume page ownership.
 *
 * Called when page is already locked by the hosting VM.
 *
 * \pre !cl_page_is_owned(pg, io)
 * \post cl_page_is_owned(pg, io)
 *
 * \see cl_page_operations::cpo_assume()
 */
void cl_page_assume(const struct lu_env *env,
		    struct cl_io *io, struct cl_page *pg)
{
	PINVRNT(env, pg, cl_object_same(pg->cp_obj, io->ci_obj));

	pg = cl_page_top(pg);
	io = cl_io_top(io);

	cl_page_invoid(env, io, pg, CL_PAGE_OP(cpo_assume));
	PASSERT(env, pg, pg->cp_owner == NULL);
	pg->cp_owner = io;
	pg->cp_task = current;
	cl_page_owner_set(pg);
	cl_page_state_set(env, pg, CPS_OWNED);
}
EXPORT_SYMBOL(cl_page_assume);

/**
 * Releases page ownership without unlocking the page.
 *
 * Moves page into cl_page_state::CPS_CACHED without releasing a lock on the
 * underlying VM page (as VM is supposed to do this itself).
 *
 * \pre   cl_page_is_owned(pg, io)
 * \post !cl_page_is_owned(pg, io)
 *
 * \see cl_page_assume()
 */
void cl_page_unassume(const struct lu_env *env,
		      struct cl_io *io, struct cl_page *pg)
{
	PINVRNT(env, pg, cl_page_is_owned(pg, io));
	PINVRNT(env, pg, cl_page_invariant(pg));

	pg = cl_page_top(pg);
	io = cl_io_top(io);
	cl_page_owner_clear(pg);
	cl_page_state_set(env, pg, CPS_CACHED);
	CL_PAGE_INVOID_REVERSE(env, pg, CL_PAGE_OP(cpo_unassume),
			       (const struct lu_env *,
				const struct cl_page_slice *, struct cl_io *),
			       io);
}
EXPORT_SYMBOL(cl_page_unassume);

/**
 * Releases page ownership.
 *
 * Moves page into cl_page_state::CPS_CACHED.
 *
 * \pre   cl_page_is_owned(pg, io)
 * \post !cl_page_is_owned(pg, io)
 *
 * \see cl_page_own()
 * \see cl_page_operations::cpo_disown()
 */
void cl_page_disown(const struct lu_env *env,
		    struct cl_io *io, struct cl_page *pg)
{
	PINVRNT(env, pg, cl_page_is_owned(pg, io));

	pg = cl_page_top(pg);
	io = cl_io_top(io);
	cl_page_disown0(env, io, pg);
}
EXPORT_SYMBOL(cl_page_disown);

/**
 * Called when page is to be removed from the object, e.g., as a result of
 * truncate.
 *
 * Calls cl_page_operations::cpo_discard() top-to-bottom.
 *
 * \pre cl_page_is_owned(pg, io)
 *
 * \see cl_page_operations::cpo_discard()
 */
void cl_page_discard(const struct lu_env *env,
		     struct cl_io *io, struct cl_page *pg)
{
	PINVRNT(env, pg, cl_page_is_owned(pg, io));
	PINVRNT(env, pg, cl_page_invariant(pg));

	cl_page_invoid(env, io, pg, CL_PAGE_OP(cpo_discard));
}
EXPORT_SYMBOL(cl_page_discard);

/**
 * Version of cl_page_delete() that can be called for not fully constructed
 * pages, e.g,. in a error handling cl_page_find()->cl_page_delete0()
 * path. Doesn't check page invariant.
 */
static void cl_page_delete0(const struct lu_env *env, struct cl_page *pg,
			    int radix)
{
	struct cl_page *tmp = pg;

	PASSERT(env, pg, pg == cl_page_top(pg));
	PASSERT(env, pg, pg->cp_state != CPS_FREEING);

	/*
	 * Severe all ways to obtain new pointers to @pg.
	 */
	cl_page_owner_clear(pg);

	/*
	 * unexport the page firstly before freeing it so that
	 * the page content is considered to be invalid.
	 * We have to do this because a CPS_FREEING cl_page may
	 * be NOT under the protection of a cl_lock.
	 * Afterwards, if this page is found by other threads, then this
	 * page will be forced to reread.
	 */
	cl_page_export(env, pg, 0);
	cl_page_state_set0(env, pg, CPS_FREEING);

	CL_PAGE_INVOID(env, pg, CL_PAGE_OP(cpo_delete),
		       (const struct lu_env *, const struct cl_page_slice *));

	if (tmp->cp_type == CPT_CACHEABLE) {
		if (!radix)
			/* !radix means that @pg is not yet in the radix tree,
			 * skip removing it.
			 */
			tmp = pg->cp_child;
		for (; tmp != NULL; tmp = tmp->cp_child) {
			void		    *value;
			struct cl_object_header *hdr;

			hdr = cl_object_header(tmp->cp_obj);
			spin_lock(&hdr->coh_page_guard);
			value = radix_tree_delete(&hdr->coh_tree,
						  tmp->cp_index);
			PASSERT(env, tmp, value == tmp);
			PASSERT(env, tmp, hdr->coh_pages > 0);
			hdr->coh_pages--;
			spin_unlock(&hdr->coh_page_guard);
			cl_page_put(env, tmp);
		}
	}
}

/**
 * Called when a decision is made to throw page out of memory.
 *
 * Notifies all layers about page destruction by calling
 * cl_page_operations::cpo_delete() method top-to-bottom.
 *
 * Moves page into cl_page_state::CPS_FREEING state (this is the only place
 * where transition to this state happens).
 *
 * Eliminates all venues through which new references to the page can be
 * obtained:
 *
 *     - removes page from the radix trees,
 *
 *     - breaks linkage from VM page to cl_page.
 *
 * Once page reaches cl_page_state::CPS_FREEING, all remaining references will
 * drain after some time, at which point page will be recycled.
 *
 * \pre  pg == cl_page_top(pg)
 * \pre  VM page is locked
 * \post pg->cp_state == CPS_FREEING
 *
 * \see cl_page_operations::cpo_delete()
 */
void cl_page_delete(const struct lu_env *env, struct cl_page *pg)
{
	PINVRNT(env, pg, cl_page_invariant(pg));
	cl_page_delete0(env, pg, 1);
}
EXPORT_SYMBOL(cl_page_delete);

/**
 * Unmaps page from user virtual memory.
 *
 * Calls cl_page_operations::cpo_unmap() through all layers top-to-bottom. The
 * layer responsible for VM interaction has to unmap page from user space
 * virtual memory.
 *
 * \see cl_page_operations::cpo_unmap()
 */
int cl_page_unmap(const struct lu_env *env,
		  struct cl_io *io, struct cl_page *pg)
{
	PINVRNT(env, pg, cl_page_is_owned(pg, io));
	PINVRNT(env, pg, cl_page_invariant(pg));

	return cl_page_invoke(env, io, pg, CL_PAGE_OP(cpo_unmap));
}
EXPORT_SYMBOL(cl_page_unmap);

/**
 * Marks page up-to-date.
 *
 * Call cl_page_operations::cpo_export() through all layers top-to-bottom. The
 * layer responsible for VM interaction has to mark/clear page as up-to-date
 * by the \a uptodate argument.
 *
 * \see cl_page_operations::cpo_export()
 */
void cl_page_export(const struct lu_env *env, struct cl_page *pg, int uptodate)
{
	PINVRNT(env, pg, cl_page_invariant(pg));
	CL_PAGE_INVOID(env, pg, CL_PAGE_OP(cpo_export),
		       (const struct lu_env *,
			const struct cl_page_slice *, int), uptodate);
}
EXPORT_SYMBOL(cl_page_export);

/**
 * Returns true, iff \a pg is VM locked in a suitable sense by the calling
 * thread.
 */
int cl_page_is_vmlocked(const struct lu_env *env, const struct cl_page *pg)
{
	int result;
	const struct cl_page_slice *slice;

	pg = cl_page_top_trusted((struct cl_page *)pg);
	slice = container_of(pg->cp_layers.next,
			     const struct cl_page_slice, cpl_linkage);
	PASSERT(env, pg, slice->cpl_ops->cpo_is_vmlocked != NULL);
	/*
	 * Call ->cpo_is_vmlocked() directly instead of going through
	 * CL_PAGE_INVOKE(), because cl_page_is_vmlocked() is used by
	 * cl_page_invariant().
	 */
	result = slice->cpl_ops->cpo_is_vmlocked(env, slice);
	PASSERT(env, pg, result == -EBUSY || result == -ENODATA);
	return result == -EBUSY;
}
EXPORT_SYMBOL(cl_page_is_vmlocked);

static enum cl_page_state cl_req_type_state(enum cl_req_type crt)
{
	return crt == CRT_WRITE ? CPS_PAGEOUT : CPS_PAGEIN;
}

static void cl_page_io_start(const struct lu_env *env,
			     struct cl_page *pg, enum cl_req_type crt)
{
	/*
	 * Page is queued for IO, change its state.
	 */
	cl_page_owner_clear(pg);
	cl_page_state_set(env, pg, cl_req_type_state(crt));
}

/**
 * Prepares page for immediate transfer. cl_page_operations::cpo_prep() is
 * called top-to-bottom. Every layer either agrees to submit this page (by
 * returning 0), or requests to omit this page (by returning -EALREADY). Layer
 * handling interactions with the VM also has to inform VM that page is under
 * transfer now.
 */
int cl_page_prep(const struct lu_env *env, struct cl_io *io,
		 struct cl_page *pg, enum cl_req_type crt)
{
	int result;

	PINVRNT(env, pg, cl_page_is_owned(pg, io));
	PINVRNT(env, pg, cl_page_invariant(pg));
	PINVRNT(env, pg, crt < CRT_NR);

	/*
	 * XXX this has to be called bottom-to-top, so that llite can set up
	 * PG_writeback without risking other layers deciding to skip this
	 * page.
	 */
	if (crt >= CRT_NR)
		return -EINVAL;
	result = cl_page_invoke(env, io, pg, CL_PAGE_OP(io[crt].cpo_prep));
	if (result == 0)
		cl_page_io_start(env, pg, crt);

	KLASSERT(ergo(crt == CRT_WRITE && pg->cp_type == CPT_CACHEABLE,
		      equi(result == 0,
			   PageWriteback(cl_page_vmpage(env, pg)))));
	CL_PAGE_HEADER(D_TRACE, env, pg, "%d %d\n", crt, result);
	return result;
}
EXPORT_SYMBOL(cl_page_prep);

/**
 * Notify layers about transfer completion.
 *
 * Invoked by transfer sub-system (which is a part of osc) to notify layers
 * that a transfer, of which this page is a part of has completed.
 *
 * Completion call-backs are executed in the bottom-up order, so that
 * uppermost layer (llite), responsible for the VFS/VM interaction runs last
 * and can release locks safely.
 *
 * \pre  pg->cp_state == CPS_PAGEIN || pg->cp_state == CPS_PAGEOUT
 * \post pg->cp_state == CPS_CACHED
 *
 * \see cl_page_operations::cpo_completion()
 */
void cl_page_completion(const struct lu_env *env,
			struct cl_page *pg, enum cl_req_type crt, int ioret)
{
	struct cl_sync_io *anchor = pg->cp_sync_io;

	PASSERT(env, pg, crt < CRT_NR);
	/* cl_page::cp_req already cleared by the caller (osc_completion()) */
	PASSERT(env, pg, pg->cp_req == NULL);
	PASSERT(env, pg, pg->cp_state == cl_req_type_state(crt));

	CL_PAGE_HEADER(D_TRACE, env, pg, "%d %d\n", crt, ioret);
	if (crt == CRT_READ && ioret == 0) {
		PASSERT(env, pg, !(pg->cp_flags & CPF_READ_COMPLETED));
		pg->cp_flags |= CPF_READ_COMPLETED;
	}

	cl_page_state_set(env, pg, CPS_CACHED);
	if (crt >= CRT_NR)
		return;
	CL_PAGE_INVOID_REVERSE(env, pg, CL_PAGE_OP(io[crt].cpo_completion),
			       (const struct lu_env *,
				const struct cl_page_slice *, int), ioret);
	if (anchor) {
		LASSERT(cl_page_is_vmlocked(env, pg));
		LASSERT(pg->cp_sync_io == anchor);
		pg->cp_sync_io = NULL;
	}
	/*
	 * As page->cp_obj is pinned by a reference from page->cp_req, it is
	 * safe to call cl_page_put() without risking object destruction in a
	 * non-blocking context.
	 */
	cl_page_put(env, pg);

	if (anchor)
		cl_sync_io_note(anchor, ioret);
}
EXPORT_SYMBOL(cl_page_completion);

/**
 * Notify layers that transfer formation engine decided to yank this page from
 * the cache and to make it a part of a transfer.
 *
 * \pre  pg->cp_state == CPS_CACHED
 * \post pg->cp_state == CPS_PAGEIN || pg->cp_state == CPS_PAGEOUT
 *
 * \see cl_page_operations::cpo_make_ready()
 */
int cl_page_make_ready(const struct lu_env *env, struct cl_page *pg,
		       enum cl_req_type crt)
{
	int result;

	PINVRNT(env, pg, crt < CRT_NR);

	if (crt >= CRT_NR)
		return -EINVAL;
	result = CL_PAGE_INVOKE(env, pg, CL_PAGE_OP(io[crt].cpo_make_ready),
				(const struct lu_env *,
				 const struct cl_page_slice *));
	if (result == 0) {
		PASSERT(env, pg, pg->cp_state == CPS_CACHED);
		cl_page_io_start(env, pg, crt);
	}
	CL_PAGE_HEADER(D_TRACE, env, pg, "%d %d\n", crt, result);
	return result;
}
EXPORT_SYMBOL(cl_page_make_ready);

/**
 * Notify layers that high level io decided to place this page into a cache
 * for future transfer.
 *
 * The layer implementing transfer engine (osc) has to register this page in
 * its queues.
 *
 * \pre  cl_page_is_owned(pg, io)
 * \post cl_page_is_owned(pg, io)
 *
 * \see cl_page_operations::cpo_cache_add()
 */
int cl_page_cache_add(const struct lu_env *env, struct cl_io *io,
		      struct cl_page *pg, enum cl_req_type crt)
{
	const struct cl_page_slice *scan;
	int result = 0;

	PINVRNT(env, pg, crt < CRT_NR);
	PINVRNT(env, pg, cl_page_is_owned(pg, io));
	PINVRNT(env, pg, cl_page_invariant(pg));

	if (crt >= CRT_NR)
		return -EINVAL;

	list_for_each_entry(scan, &pg->cp_layers, cpl_linkage) {
		if (scan->cpl_ops->io[crt].cpo_cache_add == NULL)
			continue;

		result = scan->cpl_ops->io[crt].cpo_cache_add(env, scan, io);
		if (result != 0)
			break;
	}
	CL_PAGE_HEADER(D_TRACE, env, pg, "%d %d\n", crt, result);
	return result;
}
EXPORT_SYMBOL(cl_page_cache_add);

/**
 * Called if a pge is being written back by kernel's intention.
 *
 * \pre  cl_page_is_owned(pg, io)
 * \post ergo(result == 0, pg->cp_state == CPS_PAGEOUT)
 *
 * \see cl_page_operations::cpo_flush()
 */
int cl_page_flush(const struct lu_env *env, struct cl_io *io,
		  struct cl_page *pg)
{
	int result;

	PINVRNT(env, pg, cl_page_is_owned(pg, io));
	PINVRNT(env, pg, cl_page_invariant(pg));

	result = cl_page_invoke(env, io, pg, CL_PAGE_OP(cpo_flush));

	CL_PAGE_HEADER(D_TRACE, env, pg, "%d\n", result);
	return result;
}
EXPORT_SYMBOL(cl_page_flush);

/**
 * Checks whether page is protected by any extent lock is at least required
 * mode.
 *
 * \return the same as in cl_page_operations::cpo_is_under_lock() method.
 * \see cl_page_operations::cpo_is_under_lock()
 */
int cl_page_is_under_lock(const struct lu_env *env, struct cl_io *io,
			  struct cl_page *page)
{
	int rc;

	PINVRNT(env, page, cl_page_invariant(page));

	rc = CL_PAGE_INVOKE(env, page, CL_PAGE_OP(cpo_is_under_lock),
			    (const struct lu_env *,
			     const struct cl_page_slice *, struct cl_io *),
			    io);
	PASSERT(env, page, rc != 0);
	return rc;
}
EXPORT_SYMBOL(cl_page_is_under_lock);

static int page_prune_cb(const struct lu_env *env, struct cl_io *io,
			 struct cl_page *page, void *cbdata)
{
	cl_page_own(env, io, page);
	cl_page_unmap(env, io, page);
	cl_page_discard(env, io, page);
	cl_page_disown(env, io, page);
	return CLP_GANG_OKAY;
}

/**
 * Purges all cached pages belonging to the object \a obj.
 */
int cl_pages_prune(const struct lu_env *env, struct cl_object *clobj)
{
	struct cl_thread_info   *info;
	struct cl_object	*obj = cl_object_top(clobj);
	struct cl_io	    *io;
	int		      result;

	info  = cl_env_info(env);
	io    = &info->clt_io;

	/*
	 * initialize the io. This is ugly since we never do IO in this
	 * function, we just make cl_page_list functions happy. -jay
	 */
	io->ci_obj = obj;
	io->ci_ignore_layout = 1;
	result = cl_io_init(env, io, CIT_MISC, obj);
	if (result != 0) {
		cl_io_fini(env, io);
		return io->ci_result;
	}

	do {
		result = cl_page_gang_lookup(env, obj, io, 0, CL_PAGE_EOF,
					     page_prune_cb, NULL);
		if (result == CLP_GANG_RESCHED)
			cond_resched();
	} while (result != CLP_GANG_OKAY);

	cl_io_fini(env, io);
	return result;
}
EXPORT_SYMBOL(cl_pages_prune);

/**
 * Tells transfer engine that only part of a page is to be transmitted.
 *
 * \see cl_page_operations::cpo_clip()
 */
void cl_page_clip(const struct lu_env *env, struct cl_page *pg,
		  int from, int to)
{
	PINVRNT(env, pg, cl_page_invariant(pg));

	CL_PAGE_HEADER(D_TRACE, env, pg, "%d %d\n", from, to);
	CL_PAGE_INVOID(env, pg, CL_PAGE_OP(cpo_clip),
		       (const struct lu_env *,
			const struct cl_page_slice *,int, int),
		       from, to);
}
EXPORT_SYMBOL(cl_page_clip);

/**
 * Prints human readable representation of \a pg to the \a f.
 */
void cl_page_header_print(const struct lu_env *env, void *cookie,
			  lu_printer_t printer, const struct cl_page *pg)
{
	(*printer)(env, cookie,
		   "page@%p[%d %p:%lu ^%p_%p %d %d %d %p %p %#x]\n",
		   pg, atomic_read(&pg->cp_ref), pg->cp_obj,
		   pg->cp_index, pg->cp_parent, pg->cp_child,
		   pg->cp_state, pg->cp_error, pg->cp_type,
		   pg->cp_owner, pg->cp_req, pg->cp_flags);
}
EXPORT_SYMBOL(cl_page_header_print);

/**
 * Prints human readable representation of \a pg to the \a f.
 */
void cl_page_print(const struct lu_env *env, void *cookie,
		   lu_printer_t printer, const struct cl_page *pg)
{
	struct cl_page *scan;

	for (scan = cl_page_top((struct cl_page *)pg);
	     scan != NULL; scan = scan->cp_child)
		cl_page_header_print(env, cookie, printer, scan);
	CL_PAGE_INVOKE(env, (struct cl_page *)pg, CL_PAGE_OP(cpo_print),
		       (const struct lu_env *env,
			const struct cl_page_slice *slice,
			void *cookie, lu_printer_t p), cookie, printer);
	(*printer)(env, cookie, "end page@%p\n", pg);
}
EXPORT_SYMBOL(cl_page_print);

/**
 * Cancel a page which is still in a transfer.
 */
int cl_page_cancel(const struct lu_env *env, struct cl_page *page)
{
	return CL_PAGE_INVOKE(env, page, CL_PAGE_OP(cpo_cancel),
			      (const struct lu_env *,
			       const struct cl_page_slice *));
}
EXPORT_SYMBOL(cl_page_cancel);

/**
 * Converts a byte offset within object \a obj into a page index.
 */
loff_t cl_offset(const struct cl_object *obj, pgoff_t idx)
{
	/*
	 * XXX for now.
	 */
	return (loff_t)idx << PAGE_CACHE_SHIFT;
}
EXPORT_SYMBOL(cl_offset);

/**
 * Converts a page index into a byte offset within object \a obj.
 */
pgoff_t cl_index(const struct cl_object *obj, loff_t offset)
{
	/*
	 * XXX for now.
	 */
	return offset >> PAGE_CACHE_SHIFT;
}
EXPORT_SYMBOL(cl_index);

int cl_page_size(const struct cl_object *obj)
{
	return 1 << PAGE_CACHE_SHIFT;
}
EXPORT_SYMBOL(cl_page_size);

/**
 * Adds page slice to the compound page.
 *
 * This is called by cl_object_operations::coo_page_init() methods to add a
 * per-layer state to the page. New state is added at the end of
 * cl_page::cp_layers list, that is, it is at the bottom of the stack.
 *
 * \see cl_lock_slice_add(), cl_req_slice_add(), cl_io_slice_add()
 */
void cl_page_slice_add(struct cl_page *page, struct cl_page_slice *slice,
		       struct cl_object *obj,
		       const struct cl_page_operations *ops)
{
	list_add_tail(&slice->cpl_linkage, &page->cp_layers);
	slice->cpl_obj  = obj;
	slice->cpl_ops  = ops;
	slice->cpl_page = page;
}
EXPORT_SYMBOL(cl_page_slice_add);

int  cl_page_init(void)
{
	return 0;
}

void cl_page_fini(void)
{
}
