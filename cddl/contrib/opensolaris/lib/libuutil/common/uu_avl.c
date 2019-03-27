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
#include <sys/avl.h>

static uu_avl_pool_t	uu_null_apool = { &uu_null_apool, &uu_null_apool };
static pthread_mutex_t	uu_apool_list_lock = PTHREAD_MUTEX_INITIALIZER;

/*
 * The index mark change on every insert and delete, to catch stale
 * references.
 *
 * We leave the low bit alone, since the avl code uses it.
 */
#define	INDEX_MAX		(sizeof (uintptr_t) - 2)
#define	INDEX_NEXT(m)		(((m) == INDEX_MAX)? 2 : ((m) + 2) & INDEX_MAX)

#define	INDEX_DECODE(i)		((i) & ~INDEX_MAX)
#define	INDEX_ENCODE(p, n)	(((n) & ~INDEX_MAX) | (p)->ua_index)
#define	INDEX_VALID(p, i)	(((i) & INDEX_MAX) == (p)->ua_index)
#define	INDEX_CHECK(i)		(((i) & INDEX_MAX) != 0)

/*
 * When an element is inactive (not in a tree), we keep a marked pointer to
 * its containing pool in its first word, and a NULL pointer in its second.
 *
 * On insert, we use these to verify that it comes from the correct pool.
 */
#define	NODE_ARRAY(p, n)	((uintptr_t *)((uintptr_t)(n) + \
				    (pp)->uap_nodeoffset))

#define	POOL_TO_MARKER(pp) (((uintptr_t)(pp) | 1))

#define	DEAD_MARKER		0xc4

uu_avl_pool_t *
uu_avl_pool_create(const char *name, size_t objsize, size_t nodeoffset,
    uu_compare_fn_t *compare_func, uint32_t flags)
{
	uu_avl_pool_t *pp, *next, *prev;

	if (name == NULL ||
	    uu_check_name(name, UU_NAME_DOMAIN) == -1 ||
	    nodeoffset + sizeof (uu_avl_node_t) > objsize ||
	    compare_func == NULL) {
		uu_set_error(UU_ERROR_INVALID_ARGUMENT);
		return (NULL);
	}

	if (flags & ~UU_AVL_POOL_DEBUG) {
		uu_set_error(UU_ERROR_UNKNOWN_FLAG);
		return (NULL);
	}

	pp = uu_zalloc(sizeof (uu_avl_pool_t));
	if (pp == NULL) {
		uu_set_error(UU_ERROR_NO_MEMORY);
		return (NULL);
	}

	(void) strlcpy(pp->uap_name, name, sizeof (pp->uap_name));
	pp->uap_nodeoffset = nodeoffset;
	pp->uap_objsize = objsize;
	pp->uap_cmp = compare_func;
	if (flags & UU_AVL_POOL_DEBUG)
		pp->uap_debug = 1;
	pp->uap_last_index = 0;

	(void) pthread_mutex_init(&pp->uap_lock, NULL);

	pp->uap_null_avl.ua_next_enc = UU_PTR_ENCODE(&pp->uap_null_avl);
	pp->uap_null_avl.ua_prev_enc = UU_PTR_ENCODE(&pp->uap_null_avl);

	(void) pthread_mutex_lock(&uu_apool_list_lock);
	pp->uap_next = next = &uu_null_apool;
	pp->uap_prev = prev = next->uap_prev;
	next->uap_prev = pp;
	prev->uap_next = pp;
	(void) pthread_mutex_unlock(&uu_apool_list_lock);

	return (pp);
}

void
uu_avl_pool_destroy(uu_avl_pool_t *pp)
{
	if (pp->uap_debug) {
		if (pp->uap_null_avl.ua_next_enc !=
		    UU_PTR_ENCODE(&pp->uap_null_avl) ||
		    pp->uap_null_avl.ua_prev_enc !=
		    UU_PTR_ENCODE(&pp->uap_null_avl)) {
			uu_panic("uu_avl_pool_destroy: Pool \"%.*s\" (%p) has "
			    "outstanding avls, or is corrupt.\n",
			    (int)sizeof (pp->uap_name), pp->uap_name,
			    (void *)pp);
		}
	}
	(void) pthread_mutex_lock(&uu_apool_list_lock);
	pp->uap_next->uap_prev = pp->uap_prev;
	pp->uap_prev->uap_next = pp->uap_next;
	(void) pthread_mutex_unlock(&uu_apool_list_lock);
	(void) pthread_mutex_destroy(&pp->uap_lock);
	pp->uap_prev = NULL;
	pp->uap_next = NULL;
	uu_free(pp);
}

void
uu_avl_node_init(void *base, uu_avl_node_t *np, uu_avl_pool_t *pp)
{
	uintptr_t *na = (uintptr_t *)np;

	if (pp->uap_debug) {
		uintptr_t offset = (uintptr_t)np - (uintptr_t)base;
		if (offset + sizeof (*np) > pp->uap_objsize) {
			uu_panic("uu_avl_node_init(%p, %p, %p (\"%s\")): "
			    "offset %ld doesn't fit in object (size %ld)\n",
			    base, (void *)np, (void *)pp, pp->uap_name,
			    (long)offset, (long)pp->uap_objsize);
		}
		if (offset != pp->uap_nodeoffset) {
			uu_panic("uu_avl_node_init(%p, %p, %p (\"%s\")): "
			    "offset %ld doesn't match pool's offset (%ld)\n",
			    base, (void *)np, (void *)pp, pp->uap_name,
			    (long)offset, (long)pp->uap_objsize);
		}
	}

	na[0] = POOL_TO_MARKER(pp);
	na[1] = 0;
}

void
uu_avl_node_fini(void *base, uu_avl_node_t *np, uu_avl_pool_t *pp)
{
	uintptr_t *na = (uintptr_t *)np;

	if (pp->uap_debug) {
		if (na[0] == DEAD_MARKER && na[1] == DEAD_MARKER) {
			uu_panic("uu_avl_node_fini(%p, %p, %p (\"%s\")): "
			    "node already finied\n",
			    base, (void *)np, (void *)pp, pp->uap_name);
		}
		if (na[0] != POOL_TO_MARKER(pp) || na[1] != 0) {
			uu_panic("uu_avl_node_fini(%p, %p, %p (\"%s\")): "
			    "node corrupt, in tree, or in different pool\n",
			    base, (void *)np, (void *)pp, pp->uap_name);
		}
	}

	na[0] = DEAD_MARKER;
	na[1] = DEAD_MARKER;
	na[2] = DEAD_MARKER;
}

struct uu_avl_node_compare_info {
	uu_compare_fn_t	*ac_compare;
	void		*ac_private;
	void		*ac_right;
	void		*ac_found;
};

static int
uu_avl_node_compare(const void *l, const void *r)
{
	struct uu_avl_node_compare_info *info =
	    (struct uu_avl_node_compare_info *)l;

	int res = info->ac_compare(r, info->ac_right, info->ac_private);

	if (res == 0) {
		if (info->ac_found == NULL)
			info->ac_found = (void *)r;
		return (-1);
	}
	if (res < 0)
		return (1);
	return (-1);
}

uu_avl_t *
uu_avl_create(uu_avl_pool_t *pp, void *parent, uint32_t flags)
{
	uu_avl_t *ap, *next, *prev;

	if (flags & ~UU_AVL_DEBUG) {
		uu_set_error(UU_ERROR_UNKNOWN_FLAG);
		return (NULL);
	}

	ap = uu_zalloc(sizeof (*ap));
	if (ap == NULL) {
		uu_set_error(UU_ERROR_NO_MEMORY);
		return (NULL);
	}

	ap->ua_pool = pp;
	ap->ua_parent_enc = UU_PTR_ENCODE(parent);
	ap->ua_debug = pp->uap_debug || (flags & UU_AVL_DEBUG);
	ap->ua_index = (pp->uap_last_index = INDEX_NEXT(pp->uap_last_index));

	avl_create(&ap->ua_tree, &uu_avl_node_compare, pp->uap_objsize,
	    pp->uap_nodeoffset);

	ap->ua_null_walk.uaw_next = &ap->ua_null_walk;
	ap->ua_null_walk.uaw_prev = &ap->ua_null_walk;

	(void) pthread_mutex_lock(&pp->uap_lock);
	next = &pp->uap_null_avl;
	prev = UU_PTR_DECODE(next->ua_prev_enc);
	ap->ua_next_enc = UU_PTR_ENCODE(next);
	ap->ua_prev_enc = UU_PTR_ENCODE(prev);
	next->ua_prev_enc = UU_PTR_ENCODE(ap);
	prev->ua_next_enc = UU_PTR_ENCODE(ap);
	(void) pthread_mutex_unlock(&pp->uap_lock);

	return (ap);
}

void
uu_avl_destroy(uu_avl_t *ap)
{
	uu_avl_pool_t *pp = ap->ua_pool;

	if (ap->ua_debug) {
		if (avl_numnodes(&ap->ua_tree) != 0) {
			uu_panic("uu_avl_destroy(%p): tree not empty\n",
			    (void *)ap);
		}
		if (ap->ua_null_walk.uaw_next != &ap->ua_null_walk ||
		    ap->ua_null_walk.uaw_prev != &ap->ua_null_walk) {
			uu_panic("uu_avl_destroy(%p):  outstanding walkers\n",
			    (void *)ap);
		}
	}
	(void) pthread_mutex_lock(&pp->uap_lock);
	UU_AVL_PTR(ap->ua_next_enc)->ua_prev_enc = ap->ua_prev_enc;
	UU_AVL_PTR(ap->ua_prev_enc)->ua_next_enc = ap->ua_next_enc;
	(void) pthread_mutex_unlock(&pp->uap_lock);
	ap->ua_prev_enc = UU_PTR_ENCODE(NULL);
	ap->ua_next_enc = UU_PTR_ENCODE(NULL);

	ap->ua_pool = NULL;
	avl_destroy(&ap->ua_tree);

	uu_free(ap);
}

size_t
uu_avl_numnodes(uu_avl_t *ap)
{
	return (avl_numnodes(&ap->ua_tree));
}

void *
uu_avl_first(uu_avl_t *ap)
{
	return (avl_first(&ap->ua_tree));
}

void *
uu_avl_last(uu_avl_t *ap)
{
	return (avl_last(&ap->ua_tree));
}

void *
uu_avl_next(uu_avl_t *ap, void *node)
{
	return (AVL_NEXT(&ap->ua_tree, node));
}

void *
uu_avl_prev(uu_avl_t *ap, void *node)
{
	return (AVL_PREV(&ap->ua_tree, node));
}

static void
_avl_walk_init(uu_avl_walk_t *wp, uu_avl_t *ap, uint32_t flags)
{
	uu_avl_walk_t *next, *prev;

	int robust = (flags & UU_WALK_ROBUST);
	int direction = (flags & UU_WALK_REVERSE)? -1 : 1;

	(void) memset(wp, 0, sizeof (*wp));
	wp->uaw_avl = ap;
	wp->uaw_robust = robust;
	wp->uaw_dir = direction;

	if (direction > 0)
		wp->uaw_next_result = avl_first(&ap->ua_tree);
	else
		wp->uaw_next_result = avl_last(&ap->ua_tree);

	if (ap->ua_debug || robust) {
		wp->uaw_next = next = &ap->ua_null_walk;
		wp->uaw_prev = prev = next->uaw_prev;
		next->uaw_prev = wp;
		prev->uaw_next = wp;
	}
}

static void *
_avl_walk_advance(uu_avl_walk_t *wp, uu_avl_t *ap)
{
	void *np = wp->uaw_next_result;

	avl_tree_t *t = &ap->ua_tree;

	if (np == NULL)
		return (NULL);

	wp->uaw_next_result = (wp->uaw_dir > 0)? AVL_NEXT(t, np) :
	    AVL_PREV(t, np);

	return (np);
}

static void
_avl_walk_fini(uu_avl_walk_t *wp)
{
	if (wp->uaw_next != NULL) {
		wp->uaw_next->uaw_prev = wp->uaw_prev;
		wp->uaw_prev->uaw_next = wp->uaw_next;
		wp->uaw_next = NULL;
		wp->uaw_prev = NULL;
	}
	wp->uaw_avl = NULL;
	wp->uaw_next_result = NULL;
}

uu_avl_walk_t *
uu_avl_walk_start(uu_avl_t *ap, uint32_t flags)
{
	uu_avl_walk_t *wp;

	if (flags & ~(UU_WALK_ROBUST | UU_WALK_REVERSE)) {
		uu_set_error(UU_ERROR_UNKNOWN_FLAG);
		return (NULL);
	}

	wp = uu_zalloc(sizeof (*wp));
	if (wp == NULL) {
		uu_set_error(UU_ERROR_NO_MEMORY);
		return (NULL);
	}

	_avl_walk_init(wp, ap, flags);
	return (wp);
}

void *
uu_avl_walk_next(uu_avl_walk_t *wp)
{
	return (_avl_walk_advance(wp, wp->uaw_avl));
}

void
uu_avl_walk_end(uu_avl_walk_t *wp)
{
	_avl_walk_fini(wp);
	uu_free(wp);
}

int
uu_avl_walk(uu_avl_t *ap, uu_walk_fn_t *func, void *private, uint32_t flags)
{
	void *e;
	uu_avl_walk_t my_walk;

	int status = UU_WALK_NEXT;

	if (flags & ~(UU_WALK_ROBUST | UU_WALK_REVERSE)) {
		uu_set_error(UU_ERROR_UNKNOWN_FLAG);
		return (-1);
	}

	_avl_walk_init(&my_walk, ap, flags);
	while (status == UU_WALK_NEXT &&
	    (e = _avl_walk_advance(&my_walk, ap)) != NULL)
		status = (*func)(e, private);
	_avl_walk_fini(&my_walk);

	if (status >= 0)
		return (0);
	uu_set_error(UU_ERROR_CALLBACK_FAILED);
	return (-1);
}

void
uu_avl_remove(uu_avl_t *ap, void *elem)
{
	uu_avl_walk_t *wp;
	uu_avl_pool_t *pp = ap->ua_pool;
	uintptr_t *na = NODE_ARRAY(pp, elem);

	if (ap->ua_debug) {
		/*
		 * invalidate outstanding uu_avl_index_ts.
		 */
		ap->ua_index = INDEX_NEXT(ap->ua_index);
	}

	/*
	 * Robust walkers most be advanced, if we are removing the node
	 * they are currently using.  In debug mode, non-robust walkers
	 * are also on the walker list.
	 */
	for (wp = ap->ua_null_walk.uaw_next; wp != &ap->ua_null_walk;
	    wp = wp->uaw_next) {
		if (wp->uaw_robust) {
			if (elem == wp->uaw_next_result)
				(void) _avl_walk_advance(wp, ap);
		} else if (wp->uaw_next_result != NULL) {
			uu_panic("uu_avl_remove(%p, %p): active non-robust "
			    "walker\n", (void *)ap, elem);
		}
	}

	avl_remove(&ap->ua_tree, elem);

	na[0] = POOL_TO_MARKER(pp);
	na[1] = 0;
}

void *
uu_avl_teardown(uu_avl_t *ap, void **cookie)
{
	void *elem = avl_destroy_nodes(&ap->ua_tree, cookie);

	if (elem != NULL) {
		uu_avl_pool_t *pp = ap->ua_pool;
		uintptr_t *na = NODE_ARRAY(pp, elem);

		na[0] = POOL_TO_MARKER(pp);
		na[1] = 0;
	}
	return (elem);
}

void *
uu_avl_find(uu_avl_t *ap, void *elem, void *private, uu_avl_index_t *out)
{
	struct uu_avl_node_compare_info info;
	void *result;

	info.ac_compare = ap->ua_pool->uap_cmp;
	info.ac_private = private;
	info.ac_right = elem;
	info.ac_found = NULL;

	result = avl_find(&ap->ua_tree, &info, out);
	if (out != NULL)
		*out = INDEX_ENCODE(ap, *out);

	if (ap->ua_debug && result != NULL)
		uu_panic("uu_avl_find: internal error: avl_find succeeded\n");

	return (info.ac_found);
}

void
uu_avl_insert(uu_avl_t *ap, void *elem, uu_avl_index_t idx)
{
	if (ap->ua_debug) {
		uu_avl_pool_t *pp = ap->ua_pool;
		uintptr_t *na = NODE_ARRAY(pp, elem);

		if (na[1] != 0)
			uu_panic("uu_avl_insert(%p, %p, %p): node already "
			    "in tree, or corrupt\n",
			    (void *)ap, elem, (void *)idx);
		if (na[0] == 0)
			uu_panic("uu_avl_insert(%p, %p, %p): node not "
			    "initialized\n",
			    (void *)ap, elem, (void *)idx);
		if (na[0] != POOL_TO_MARKER(pp))
			uu_panic("uu_avl_insert(%p, %p, %p): node from "
			    "other pool, or corrupt\n",
			    (void *)ap, elem, (void *)idx);

		if (!INDEX_VALID(ap, idx))
			uu_panic("uu_avl_insert(%p, %p, %p): %s\n",
			    (void *)ap, elem, (void *)idx,
			    INDEX_CHECK(idx)? "outdated index" :
			    "invalid index");

		/*
		 * invalidate outstanding uu_avl_index_ts.
		 */
		ap->ua_index = INDEX_NEXT(ap->ua_index);
	}
	avl_insert(&ap->ua_tree, elem, INDEX_DECODE(idx));
}

void *
uu_avl_nearest_next(uu_avl_t *ap, uu_avl_index_t idx)
{
	if (ap->ua_debug && !INDEX_VALID(ap, idx))
		uu_panic("uu_avl_nearest_next(%p, %p): %s\n",
		    (void *)ap, (void *)idx, INDEX_CHECK(idx)?
		    "outdated index" : "invalid index");
	return (avl_nearest(&ap->ua_tree, INDEX_DECODE(idx), AVL_AFTER));
}

void *
uu_avl_nearest_prev(uu_avl_t *ap, uu_avl_index_t idx)
{
	if (ap->ua_debug && !INDEX_VALID(ap, idx))
		uu_panic("uu_avl_nearest_prev(%p, %p): %s\n",
		    (void *)ap, (void *)idx, INDEX_CHECK(idx)?
		    "outdated index" : "invalid index");
	return (avl_nearest(&ap->ua_tree, INDEX_DECODE(idx), AVL_BEFORE));
}

/*
 * called from uu_lockup() and uu_release(), as part of our fork1()-safety.
 */
void
uu_avl_lockup(void)
{
	uu_avl_pool_t *pp;

	(void) pthread_mutex_lock(&uu_apool_list_lock);
	for (pp = uu_null_apool.uap_next; pp != &uu_null_apool;
	    pp = pp->uap_next)
		(void) pthread_mutex_lock(&pp->uap_lock);
}

void
uu_avl_release(void)
{
	uu_avl_pool_t *pp;

	for (pp = uu_null_apool.uap_next; pp != &uu_null_apool;
	    pp = pp->uap_next)
		(void) pthread_mutex_unlock(&pp->uap_lock);
	(void) pthread_mutex_unlock(&uu_apool_list_lock);
}
