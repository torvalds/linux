/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include "libuutil_common.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

#define	ELEM_TO_NODE(lp, e) \
	((uu_list_node_impl_t *)((uintptr_t)(e) + (lp)->ul_offset))

#define	NODE_TO_ELEM(lp, n) \
	((void *)((uintptr_t)(n) - (lp)->ul_offset))

/*
 * uu_list_index_ts define a location for insertion.  They are simply a
 * pointer to the object after the insertion point.  We store a mark
 * in the low-bits of the index, to help prevent mistakes.
 *
 * When debugging, the index mark changes on every insert and delete, to
 * catch stale references.
 */
#define	INDEX_MAX		(sizeof (uintptr_t) - 1)
#define	INDEX_NEXT(m)		(((m) == INDEX_MAX)? 1 : ((m) + 1) & INDEX_MAX)

#define	INDEX_TO_NODE(i)	((uu_list_node_impl_t *)((i) & ~INDEX_MAX))
#define	NODE_TO_INDEX(p, n)	(((uintptr_t)(n) & ~INDEX_MAX) | (p)->ul_index)
#define	INDEX_VALID(p, i)	(((i) & INDEX_MAX) == (p)->ul_index)
#define	INDEX_CHECK(i)		(((i) & INDEX_MAX) != 0)

#define	POOL_TO_MARKER(pp) ((void *)((uintptr_t)(pp) | 1))

static uu_list_pool_t	uu_null_lpool = { &uu_null_lpool, &uu_null_lpool };
static pthread_mutex_t	uu_lpool_list_lock = PTHREAD_MUTEX_INITIALIZER;

uu_list_pool_t *
uu_list_pool_create(const char *name, size_t objsize,
    size_t nodeoffset, uu_compare_fn_t *compare_func, uint32_t flags)
{
	uu_list_pool_t *pp, *next, *prev;

	if (name == NULL ||
	    uu_check_name(name, UU_NAME_DOMAIN) == -1 ||
	    nodeoffset + sizeof (uu_list_node_t) > objsize) {
		uu_set_error(UU_ERROR_INVALID_ARGUMENT);
		return (NULL);
	}

	if (flags & ~UU_LIST_POOL_DEBUG) {
		uu_set_error(UU_ERROR_UNKNOWN_FLAG);
		return (NULL);
	}

	pp = uu_zalloc(sizeof (uu_list_pool_t));
	if (pp == NULL) {
		uu_set_error(UU_ERROR_NO_MEMORY);
		return (NULL);
	}

	(void) strlcpy(pp->ulp_name, name, sizeof (pp->ulp_name));
	pp->ulp_nodeoffset = nodeoffset;
	pp->ulp_objsize = objsize;
	pp->ulp_cmp = compare_func;
	if (flags & UU_LIST_POOL_DEBUG)
		pp->ulp_debug = 1;
	pp->ulp_last_index = 0;

	(void) pthread_mutex_init(&pp->ulp_lock, NULL);

	pp->ulp_null_list.ul_next_enc = UU_PTR_ENCODE(&pp->ulp_null_list);
	pp->ulp_null_list.ul_prev_enc = UU_PTR_ENCODE(&pp->ulp_null_list);

	(void) pthread_mutex_lock(&uu_lpool_list_lock);
	pp->ulp_next = next = &uu_null_lpool;
	pp->ulp_prev = prev = next->ulp_prev;
	next->ulp_prev = pp;
	prev->ulp_next = pp;
	(void) pthread_mutex_unlock(&uu_lpool_list_lock);

	return (pp);
}

void
uu_list_pool_destroy(uu_list_pool_t *pp)
{
	if (pp->ulp_debug) {
		if (pp->ulp_null_list.ul_next_enc !=
		    UU_PTR_ENCODE(&pp->ulp_null_list) ||
		    pp->ulp_null_list.ul_prev_enc !=
		    UU_PTR_ENCODE(&pp->ulp_null_list)) {
			uu_panic("uu_list_pool_destroy: Pool \"%.*s\" (%p) has "
			    "outstanding lists, or is corrupt.\n",
			    (int)sizeof (pp->ulp_name), pp->ulp_name,
			    (void *)pp);
		}
	}
	(void) pthread_mutex_lock(&uu_lpool_list_lock);
	pp->ulp_next->ulp_prev = pp->ulp_prev;
	pp->ulp_prev->ulp_next = pp->ulp_next;
	(void) pthread_mutex_unlock(&uu_lpool_list_lock);
	pp->ulp_prev = NULL;
	pp->ulp_next = NULL;
	uu_free(pp);
}

void
uu_list_node_init(void *base, uu_list_node_t *np_arg, uu_list_pool_t *pp)
{
	uu_list_node_impl_t *np = (uu_list_node_impl_t *)np_arg;

	if (pp->ulp_debug) {
		uintptr_t offset = (uintptr_t)np - (uintptr_t)base;
		if (offset + sizeof (*np) > pp->ulp_objsize) {
			uu_panic("uu_list_node_init(%p, %p, %p (\"%s\")): "
			    "offset %ld doesn't fit in object (size %ld)\n",
			    base, (void *)np, (void *)pp, pp->ulp_name,
			    (long)offset, (long)pp->ulp_objsize);
		}
		if (offset != pp->ulp_nodeoffset) {
			uu_panic("uu_list_node_init(%p, %p, %p (\"%s\")): "
			    "offset %ld doesn't match pool's offset (%ld)\n",
			    base, (void *)np, (void *)pp, pp->ulp_name,
			    (long)offset, (long)pp->ulp_objsize);
		}
	}
	np->uln_next = POOL_TO_MARKER(pp);
	np->uln_prev = NULL;
}

void
uu_list_node_fini(void *base, uu_list_node_t *np_arg, uu_list_pool_t *pp)
{
	uu_list_node_impl_t *np = (uu_list_node_impl_t *)np_arg;

	if (pp->ulp_debug) {
		if (np->uln_next == NULL &&
		    np->uln_prev == NULL) {
			uu_panic("uu_list_node_fini(%p, %p, %p (\"%s\")): "
			    "node already finied\n",
			    base, (void *)np_arg, (void *)pp, pp->ulp_name);
		}
		if (np->uln_next != POOL_TO_MARKER(pp) ||
		    np->uln_prev != NULL) {
			uu_panic("uu_list_node_fini(%p, %p, %p (\"%s\")): "
			    "node corrupt or on list\n",
			    base, (void *)np_arg, (void *)pp, pp->ulp_name);
		}
	}
	np->uln_next = NULL;
	np->uln_prev = NULL;
}

uu_list_t *
uu_list_create(uu_list_pool_t *pp, void *parent, uint32_t flags)
{
	uu_list_t *lp, *next, *prev;

	if (flags & ~(UU_LIST_DEBUG | UU_LIST_SORTED)) {
		uu_set_error(UU_ERROR_UNKNOWN_FLAG);
		return (NULL);
	}

	if ((flags & UU_LIST_SORTED) && pp->ulp_cmp == NULL) {
		if (pp->ulp_debug)
			uu_panic("uu_list_create(%p, ...): requested "
			    "UU_LIST_SORTED, but pool has no comparison func\n",
			    (void *)pp);
		uu_set_error(UU_ERROR_NOT_SUPPORTED);
		return (NULL);
	}

	lp = uu_zalloc(sizeof (*lp));
	if (lp == NULL) {
		uu_set_error(UU_ERROR_NO_MEMORY);
		return (NULL);
	}

	lp->ul_pool = pp;
	lp->ul_parent_enc = UU_PTR_ENCODE(parent);
	lp->ul_offset = pp->ulp_nodeoffset;
	lp->ul_debug = pp->ulp_debug || (flags & UU_LIST_DEBUG);
	lp->ul_sorted = (flags & UU_LIST_SORTED);
	lp->ul_numnodes = 0;
	lp->ul_index = (pp->ulp_last_index = INDEX_NEXT(pp->ulp_last_index));

	lp->ul_null_node.uln_next = &lp->ul_null_node;
	lp->ul_null_node.uln_prev = &lp->ul_null_node;

	lp->ul_null_walk.ulw_next = &lp->ul_null_walk;
	lp->ul_null_walk.ulw_prev = &lp->ul_null_walk;

	(void) pthread_mutex_lock(&pp->ulp_lock);
	next = &pp->ulp_null_list;
	prev = UU_PTR_DECODE(next->ul_prev_enc);
	lp->ul_next_enc = UU_PTR_ENCODE(next);
	lp->ul_prev_enc = UU_PTR_ENCODE(prev);
	next->ul_prev_enc = UU_PTR_ENCODE(lp);
	prev->ul_next_enc = UU_PTR_ENCODE(lp);
	(void) pthread_mutex_unlock(&pp->ulp_lock);

	return (lp);
}

void
uu_list_destroy(uu_list_t *lp)
{
	uu_list_pool_t *pp = lp->ul_pool;

	if (lp->ul_debug) {
		if (lp->ul_null_node.uln_next != &lp->ul_null_node ||
		    lp->ul_null_node.uln_prev != &lp->ul_null_node) {
			uu_panic("uu_list_destroy(%p):  list not empty\n",
			    (void *)lp);
		}
		if (lp->ul_numnodes != 0) {
			uu_panic("uu_list_destroy(%p):  numnodes is nonzero, "
			    "but list is empty\n", (void *)lp);
		}
		if (lp->ul_null_walk.ulw_next != &lp->ul_null_walk ||
		    lp->ul_null_walk.ulw_prev != &lp->ul_null_walk) {
			uu_panic("uu_list_destroy(%p):  outstanding walkers\n",
			    (void *)lp);
		}
	}

	(void) pthread_mutex_lock(&pp->ulp_lock);
	UU_LIST_PTR(lp->ul_next_enc)->ul_prev_enc = lp->ul_prev_enc;
	UU_LIST_PTR(lp->ul_prev_enc)->ul_next_enc = lp->ul_next_enc;
	(void) pthread_mutex_unlock(&pp->ulp_lock);
	lp->ul_prev_enc = UU_PTR_ENCODE(NULL);
	lp->ul_next_enc = UU_PTR_ENCODE(NULL);
	lp->ul_pool = NULL;
	uu_free(lp);
}

static void
list_insert(uu_list_t *lp, uu_list_node_impl_t *np, uu_list_node_impl_t *prev,
    uu_list_node_impl_t *next)
{
	if (lp->ul_debug) {
		if (next->uln_prev != prev || prev->uln_next != next)
			uu_panic("insert(%p): internal error: %p and %p not "
			    "neighbors\n", (void *)lp, (void *)next,
			    (void *)prev);

		if (np->uln_next != POOL_TO_MARKER(lp->ul_pool) ||
		    np->uln_prev != NULL) {
			uu_panic("insert(%p): elem %p node %p corrupt, "
			    "not initialized, or already in a list.\n",
			    (void *)lp, NODE_TO_ELEM(lp, np), (void *)np);
		}
		/*
		 * invalidate outstanding uu_list_index_ts.
		 */
		lp->ul_index = INDEX_NEXT(lp->ul_index);
	}
	np->uln_next = next;
	np->uln_prev = prev;
	next->uln_prev = np;
	prev->uln_next = np;

	lp->ul_numnodes++;
}

void
uu_list_insert(uu_list_t *lp, void *elem, uu_list_index_t idx)
{
	uu_list_node_impl_t *np;

	np = INDEX_TO_NODE(idx);
	if (np == NULL)
		np = &lp->ul_null_node;

	if (lp->ul_debug) {
		if (!INDEX_VALID(lp, idx))
			uu_panic("uu_list_insert(%p, %p, %p): %s\n",
			    (void *)lp, elem, (void *)idx,
			    INDEX_CHECK(idx)? "outdated index" :
			    "invalid index");
		if (np->uln_prev == NULL)
			uu_panic("uu_list_insert(%p, %p, %p): out-of-date "
			    "index\n", (void *)lp, elem, (void *)idx);
	}

	list_insert(lp, ELEM_TO_NODE(lp, elem), np->uln_prev, np);
}

void *
uu_list_find(uu_list_t *lp, void *elem, void *private, uu_list_index_t *out)
{
	int sorted = lp->ul_sorted;
	uu_compare_fn_t *func = lp->ul_pool->ulp_cmp;
	uu_list_node_impl_t *np;

	if (func == NULL) {
		if (out != NULL)
			*out = 0;
		uu_set_error(UU_ERROR_NOT_SUPPORTED);
		return (NULL);
	}
	for (np = lp->ul_null_node.uln_next; np != &lp->ul_null_node;
	    np = np->uln_next) {
		void *ep = NODE_TO_ELEM(lp, np);
		int cmp = func(ep, elem, private);
		if (cmp == 0) {
			if (out != NULL)
				*out = NODE_TO_INDEX(lp, np);
			return (ep);
		}
		if (sorted && cmp > 0) {
			if (out != NULL)
				*out = NODE_TO_INDEX(lp, np);
			return (NULL);
		}
	}
	if (out != NULL)
		*out = NODE_TO_INDEX(lp, 0);
	return (NULL);
}

void *
uu_list_nearest_next(uu_list_t *lp, uu_list_index_t idx)
{
	uu_list_node_impl_t *np = INDEX_TO_NODE(idx);

	if (np == NULL)
		np = &lp->ul_null_node;

	if (lp->ul_debug) {
		if (!INDEX_VALID(lp, idx))
			uu_panic("uu_list_nearest_next(%p, %p): %s\n",
			    (void *)lp, (void *)idx,
			    INDEX_CHECK(idx)? "outdated index" :
			    "invalid index");
		if (np->uln_prev == NULL)
			uu_panic("uu_list_nearest_next(%p, %p): out-of-date "
			    "index\n", (void *)lp, (void *)idx);
	}

	if (np == &lp->ul_null_node)
		return (NULL);
	else
		return (NODE_TO_ELEM(lp, np));
}

void *
uu_list_nearest_prev(uu_list_t *lp, uu_list_index_t idx)
{
	uu_list_node_impl_t *np = INDEX_TO_NODE(idx);

	if (np == NULL)
		np = &lp->ul_null_node;

	if (lp->ul_debug) {
		if (!INDEX_VALID(lp, idx))
			uu_panic("uu_list_nearest_prev(%p, %p): %s\n",
			    (void *)lp, (void *)idx, INDEX_CHECK(idx)?
			    "outdated index" : "invalid index");
		if (np->uln_prev == NULL)
			uu_panic("uu_list_nearest_prev(%p, %p): out-of-date "
			    "index\n", (void *)lp, (void *)idx);
	}

	if ((np = np->uln_prev) == &lp->ul_null_node)
		return (NULL);
	else
		return (NODE_TO_ELEM(lp, np));
}

static void
list_walk_init(uu_list_walk_t *wp, uu_list_t *lp, uint32_t flags)
{
	uu_list_walk_t *next, *prev;

	int robust = (flags & UU_WALK_ROBUST);
	int direction = (flags & UU_WALK_REVERSE)? -1 : 1;

	(void) memset(wp, 0, sizeof (*wp));
	wp->ulw_list = lp;
	wp->ulw_robust = robust;
	wp->ulw_dir = direction;
	if (direction > 0)
		wp->ulw_next_result = lp->ul_null_node.uln_next;
	else
		wp->ulw_next_result = lp->ul_null_node.uln_prev;

	if (lp->ul_debug || robust) {
		/*
		 * Add this walker to the list's list of walkers so
		 * uu_list_remove() can advance us if somebody tries to
		 * remove ulw_next_result.
		 */
		wp->ulw_next = next = &lp->ul_null_walk;
		wp->ulw_prev = prev = next->ulw_prev;
		next->ulw_prev = wp;
		prev->ulw_next = wp;
	}
}

static uu_list_node_impl_t *
list_walk_advance(uu_list_walk_t *wp, uu_list_t *lp)
{
	uu_list_node_impl_t *np = wp->ulw_next_result;
	uu_list_node_impl_t *next;

	if (np == &lp->ul_null_node)
		return (NULL);

	next = (wp->ulw_dir > 0)? np->uln_next : np->uln_prev;

	wp->ulw_next_result = next;
	return (np);
}

static void
list_walk_fini(uu_list_walk_t *wp)
{
	/* GLXXX debugging? */
	if (wp->ulw_next != NULL) {
		wp->ulw_next->ulw_prev = wp->ulw_prev;
		wp->ulw_prev->ulw_next = wp->ulw_next;
		wp->ulw_next = NULL;
		wp->ulw_prev = NULL;
	}
	wp->ulw_list = NULL;
	wp->ulw_next_result = NULL;
}

uu_list_walk_t *
uu_list_walk_start(uu_list_t *lp, uint32_t flags)
{
	uu_list_walk_t *wp;

	if (flags & ~(UU_WALK_ROBUST | UU_WALK_REVERSE)) {
		uu_set_error(UU_ERROR_UNKNOWN_FLAG);
		return (NULL);
	}

	wp = uu_zalloc(sizeof (*wp));
	if (wp == NULL) {
		uu_set_error(UU_ERROR_NO_MEMORY);
		return (NULL);
	}

	list_walk_init(wp, lp, flags);
	return (wp);
}

void *
uu_list_walk_next(uu_list_walk_t *wp)
{
	uu_list_t *lp = wp->ulw_list;
	uu_list_node_impl_t *np = list_walk_advance(wp, lp);

	if (np == NULL)
		return (NULL);

	return (NODE_TO_ELEM(lp, np));
}

void
uu_list_walk_end(uu_list_walk_t *wp)
{
	list_walk_fini(wp);
	uu_free(wp);
}

int
uu_list_walk(uu_list_t *lp, uu_walk_fn_t *func, void *private, uint32_t flags)
{
	uu_list_node_impl_t *np;

	int status = UU_WALK_NEXT;

	int robust = (flags & UU_WALK_ROBUST);
	int reverse = (flags & UU_WALK_REVERSE);

	if (flags & ~(UU_WALK_ROBUST | UU_WALK_REVERSE)) {
		uu_set_error(UU_ERROR_UNKNOWN_FLAG);
		return (-1);
	}

	if (lp->ul_debug || robust) {
		uu_list_walk_t my_walk;
		void *e;

		list_walk_init(&my_walk, lp, flags);
		while (status == UU_WALK_NEXT &&
		    (e = uu_list_walk_next(&my_walk)) != NULL)
			status = (*func)(e, private);
		list_walk_fini(&my_walk);
	} else {
		if (!reverse) {
			for (np = lp->ul_null_node.uln_next;
			    status == UU_WALK_NEXT && np != &lp->ul_null_node;
			    np = np->uln_next) {
				status = (*func)(NODE_TO_ELEM(lp, np), private);
			}
		} else {
			for (np = lp->ul_null_node.uln_prev;
			    status == UU_WALK_NEXT && np != &lp->ul_null_node;
			    np = np->uln_prev) {
				status = (*func)(NODE_TO_ELEM(lp, np), private);
			}
		}
	}
	if (status >= 0)
		return (0);
	uu_set_error(UU_ERROR_CALLBACK_FAILED);
	return (-1);
}

void
uu_list_remove(uu_list_t *lp, void *elem)
{
	uu_list_node_impl_t *np = ELEM_TO_NODE(lp, elem);
	uu_list_walk_t *wp;

	if (lp->ul_debug) {
		if (np->uln_prev == NULL)
			uu_panic("uu_list_remove(%p, %p): elem not on list\n",
			    (void *)lp, elem);
		/*
		 * invalidate outstanding uu_list_index_ts.
		 */
		lp->ul_index = INDEX_NEXT(lp->ul_index);
	}

	/*
	 * robust walkers must be advanced.  In debug mode, non-robust
	 * walkers are also on the list.  If there are any, it's an error.
	 */
	for (wp = lp->ul_null_walk.ulw_next; wp != &lp->ul_null_walk;
	    wp = wp->ulw_next) {
		if (wp->ulw_robust) {
			if (np == wp->ulw_next_result)
				(void) list_walk_advance(wp, lp);
		} else if (wp->ulw_next_result != NULL) {
			uu_panic("uu_list_remove(%p, %p): active non-robust "
			    "walker\n", (void *)lp, elem);
		}
	}

	np->uln_next->uln_prev = np->uln_prev;
	np->uln_prev->uln_next = np->uln_next;

	lp->ul_numnodes--;

	np->uln_next = POOL_TO_MARKER(lp->ul_pool);
	np->uln_prev = NULL;
}

void *
uu_list_teardown(uu_list_t *lp, void **cookie)
{
	void *ep;

	/*
	 * XXX: disable list modification until list is empty
	 */
	if (lp->ul_debug && *cookie != NULL)
		uu_panic("uu_list_teardown(%p, %p): unexpected cookie\n",
		    (void *)lp, (void *)cookie);

	ep = uu_list_first(lp);
	if (ep)
		uu_list_remove(lp, ep);
	return (ep);
}

int
uu_list_insert_before(uu_list_t *lp, void *target, void *elem)
{
	uu_list_node_impl_t *np = ELEM_TO_NODE(lp, target);

	if (target == NULL)
		np = &lp->ul_null_node;

	if (lp->ul_debug) {
		if (np->uln_prev == NULL)
			uu_panic("uu_list_insert_before(%p, %p, %p): %p is "
			    "not currently on a list\n",
			    (void *)lp, target, elem, target);
	}
	if (lp->ul_sorted) {
		if (lp->ul_debug)
			uu_panic("uu_list_insert_before(%p, ...): list is "
			    "UU_LIST_SORTED\n", (void *)lp);
		uu_set_error(UU_ERROR_NOT_SUPPORTED);
		return (-1);
	}

	list_insert(lp, ELEM_TO_NODE(lp, elem), np->uln_prev, np);
	return (0);
}

int
uu_list_insert_after(uu_list_t *lp, void *target, void *elem)
{
	uu_list_node_impl_t *np = ELEM_TO_NODE(lp, target);

	if (target == NULL)
		np = &lp->ul_null_node;

	if (lp->ul_debug) {
		if (np->uln_prev == NULL)
			uu_panic("uu_list_insert_after(%p, %p, %p): %p is "
			    "not currently on a list\n",
			    (void *)lp, target, elem, target);
	}
	if (lp->ul_sorted) {
		if (lp->ul_debug)
			uu_panic("uu_list_insert_after(%p, ...): list is "
			    "UU_LIST_SORTED\n", (void *)lp);
		uu_set_error(UU_ERROR_NOT_SUPPORTED);
		return (-1);
	}

	list_insert(lp, ELEM_TO_NODE(lp, elem), np, np->uln_next);
	return (0);
}

size_t
uu_list_numnodes(uu_list_t *lp)
{
	return (lp->ul_numnodes);
}

void *
uu_list_first(uu_list_t *lp)
{
	uu_list_node_impl_t *n = lp->ul_null_node.uln_next;
	if (n == &lp->ul_null_node)
		return (NULL);
	return (NODE_TO_ELEM(lp, n));
}

void *
uu_list_last(uu_list_t *lp)
{
	uu_list_node_impl_t *n = lp->ul_null_node.uln_prev;
	if (n == &lp->ul_null_node)
		return (NULL);
	return (NODE_TO_ELEM(lp, n));
}

void *
uu_list_next(uu_list_t *lp, void *elem)
{
	uu_list_node_impl_t *n = ELEM_TO_NODE(lp, elem);

	n = n->uln_next;
	if (n == &lp->ul_null_node)
		return (NULL);
	return (NODE_TO_ELEM(lp, n));
}

void *
uu_list_prev(uu_list_t *lp, void *elem)
{
	uu_list_node_impl_t *n = ELEM_TO_NODE(lp, elem);

	n = n->uln_prev;
	if (n == &lp->ul_null_node)
		return (NULL);
	return (NODE_TO_ELEM(lp, n));
}

/*
 * called from uu_lockup() and uu_release(), as part of our fork1()-safety.
 */
void
uu_list_lockup(void)
{
	uu_list_pool_t *pp;

	(void) pthread_mutex_lock(&uu_lpool_list_lock);
	for (pp = uu_null_lpool.ulp_next; pp != &uu_null_lpool;
	    pp = pp->ulp_next)
		(void) pthread_mutex_lock(&pp->ulp_lock);
}

void
uu_list_release(void)
{
	uu_list_pool_t *pp;

	for (pp = uu_null_lpool.ulp_next; pp != &uu_null_lpool;
	    pp = pp->ulp_next)
		(void) pthread_mutex_unlock(&pp->ulp_lock);
	(void) pthread_mutex_unlock(&uu_lpool_list_lock);
}
