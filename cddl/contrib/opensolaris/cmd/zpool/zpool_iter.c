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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/*
 * Copyright 2016 Igor Kozhukhov <ikozhukhov@gmail.com>.
 */

#include <solaris.h>
#include <libintl.h>
#include <libuutil.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

#include <libzfs.h>

#include "zpool_util.h"

/*
 * Private interface for iterating over pools specified on the command line.
 * Most consumers will call for_each_pool, but in order to support iostat, we
 * allow fined grained control through the zpool_list_t interface.
 */

typedef struct zpool_node {
	zpool_handle_t	*zn_handle;
	uu_avl_node_t	zn_avlnode;
	int		zn_mark;
} zpool_node_t;

struct zpool_list {
	boolean_t	zl_findall;
	uu_avl_t	*zl_avl;
	uu_avl_pool_t	*zl_pool;
	zprop_list_t	**zl_proplist;
};

/* ARGSUSED */
static int
zpool_compare(const void *larg, const void *rarg, void *unused)
{
	zpool_handle_t *l = ((zpool_node_t *)larg)->zn_handle;
	zpool_handle_t *r = ((zpool_node_t *)rarg)->zn_handle;
	const char *lname = zpool_get_name(l);
	const char *rname = zpool_get_name(r);

	return (strcmp(lname, rname));
}

/*
 * Callback function for pool_list_get().  Adds the given pool to the AVL tree
 * of known pools.
 */
static int
add_pool(zpool_handle_t *zhp, void *data)
{
	zpool_list_t *zlp = data;
	zpool_node_t *node = safe_malloc(sizeof (zpool_node_t));
	uu_avl_index_t idx;

	node->zn_handle = zhp;
	uu_avl_node_init(node, &node->zn_avlnode, zlp->zl_pool);
	if (uu_avl_find(zlp->zl_avl, node, NULL, &idx) == NULL) {
		if (zlp->zl_proplist &&
		    zpool_expand_proplist(zhp, zlp->zl_proplist) != 0) {
			zpool_close(zhp);
			free(node);
			return (-1);
		}
		uu_avl_insert(zlp->zl_avl, node, idx);
	} else {
		zpool_close(zhp);
		free(node);
		return (-1);
	}

	return (0);
}

/*
 * Create a list of pools based on the given arguments.  If we're given no
 * arguments, then iterate over all pools in the system and add them to the AVL
 * tree.  Otherwise, add only those pool explicitly specified on the command
 * line.
 */
zpool_list_t *
pool_list_get(int argc, char **argv, zprop_list_t **proplist, int *err)
{
	zpool_list_t *zlp;

	zlp = safe_malloc(sizeof (zpool_list_t));

	zlp->zl_pool = uu_avl_pool_create("zfs_pool", sizeof (zpool_node_t),
	    offsetof(zpool_node_t, zn_avlnode), zpool_compare, UU_DEFAULT);

	if (zlp->zl_pool == NULL)
		zpool_no_memory();

	if ((zlp->zl_avl = uu_avl_create(zlp->zl_pool, NULL,
	    UU_DEFAULT)) == NULL)
		zpool_no_memory();

	zlp->zl_proplist = proplist;

	if (argc == 0) {
		(void) zpool_iter(g_zfs, add_pool, zlp);
		zlp->zl_findall = B_TRUE;
	} else {
		int i;

		for (i = 0; i < argc; i++) {
			zpool_handle_t *zhp;

			if ((zhp = zpool_open_canfail(g_zfs, argv[i])) !=
			    NULL) {
				if (add_pool(zhp, zlp) != 0)
					*err = B_TRUE;
			} else {
				*err = B_TRUE;
			}
		}
	}

	return (zlp);
}

/*
 * Search for any new pools, adding them to the list.  We only add pools when no
 * options were given on the command line.  Otherwise, we keep the list fixed as
 * those that were explicitly specified.
 */
void
pool_list_update(zpool_list_t *zlp)
{
	if (zlp->zl_findall)
		(void) zpool_iter(g_zfs, add_pool, zlp);
}

/*
 * Iterate over all pools in the list, executing the callback for each
 */
int
pool_list_iter(zpool_list_t *zlp, int unavail, zpool_iter_f func,
    void *data)
{
	zpool_node_t *node, *next_node;
	int ret = 0;

	for (node = uu_avl_first(zlp->zl_avl); node != NULL; node = next_node) {
		next_node = uu_avl_next(zlp->zl_avl, node);
		if (zpool_get_state(node->zn_handle) != POOL_STATE_UNAVAIL ||
		    unavail)
			ret |= func(node->zn_handle, data);
	}

	return (ret);
}

/*
 * Remove the given pool from the list.  When running iostat, we want to remove
 * those pools that no longer exist.
 */
void
pool_list_remove(zpool_list_t *zlp, zpool_handle_t *zhp)
{
	zpool_node_t search, *node;

	search.zn_handle = zhp;
	if ((node = uu_avl_find(zlp->zl_avl, &search, NULL, NULL)) != NULL) {
		uu_avl_remove(zlp->zl_avl, node);
		zpool_close(node->zn_handle);
		free(node);
	}
}

/*
 * Free all the handles associated with this list.
 */
void
pool_list_free(zpool_list_t *zlp)
{
	uu_avl_walk_t *walk;
	zpool_node_t *node;

	if ((walk = uu_avl_walk_start(zlp->zl_avl, UU_WALK_ROBUST)) == NULL) {
		(void) fprintf(stderr,
		    gettext("internal error: out of memory"));
		exit(1);
	}

	while ((node = uu_avl_walk_next(walk)) != NULL) {
		uu_avl_remove(zlp->zl_avl, node);
		zpool_close(node->zn_handle);
		free(node);
	}

	uu_avl_walk_end(walk);
	uu_avl_destroy(zlp->zl_avl);
	uu_avl_pool_destroy(zlp->zl_pool);

	free(zlp);
}

/*
 * Returns the number of elements in the pool list.
 */
int
pool_list_count(zpool_list_t *zlp)
{
	return (uu_avl_numnodes(zlp->zl_avl));
}

/*
 * High level function which iterates over all pools given on the command line,
 * using the pool_list_* interfaces.
 */
int
for_each_pool(int argc, char **argv, boolean_t unavail,
    zprop_list_t **proplist, zpool_iter_f func, void *data)
{
	zpool_list_t *list;
	int ret = 0;

	if ((list = pool_list_get(argc, argv, proplist, &ret)) == NULL)
		return (1);

	if (pool_list_iter(list, unavail, func, data) != 0)
		ret = 1;

	pool_list_free(list);

	return (ret);
}
