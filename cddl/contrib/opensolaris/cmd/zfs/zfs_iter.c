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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012 Pawel Jakub Dawidek. All rights reserved.
 * Copyright 2013 Nexenta Systems, Inc.  All rights reserved.
 * Copyright (c) 2013 by Delphix. All rights reserved.
 */

#include <libintl.h>
#include <libuutil.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

#include <libzfs.h>

#include "zfs_util.h"
#include "zfs_iter.h"

/*
 * This is a private interface used to gather up all the datasets specified on
 * the command line so that we can iterate over them in order.
 *
 * First, we iterate over all filesystems, gathering them together into an
 * AVL tree.  We report errors for any explicitly specified datasets
 * that we couldn't open.
 *
 * When finished, we have an AVL tree of ZFS handles.  We go through and execute
 * the provided callback for each one, passing whatever data the user supplied.
 */

typedef struct zfs_node {
	zfs_handle_t	*zn_handle;
	uu_avl_node_t	zn_avlnode;
} zfs_node_t;

typedef struct callback_data {
	uu_avl_t		*cb_avl;
	int			cb_flags;
	zfs_type_t		cb_types;
	zfs_sort_column_t	*cb_sortcol;
	zprop_list_t		**cb_proplist;
	int			cb_depth_limit;
	int			cb_depth;
	uint8_t			cb_props_table[ZFS_NUM_PROPS];
} callback_data_t;

uu_avl_pool_t *avl_pool;

/*
 * Include snaps if they were requested or if this a zfs list where types
 * were not specified and the "listsnapshots" property is set on this pool.
 */
static boolean_t
zfs_include_snapshots(zfs_handle_t *zhp, callback_data_t *cb)
{
	zpool_handle_t *zph;

	if ((cb->cb_flags & ZFS_ITER_PROP_LISTSNAPS) == 0)
		return (cb->cb_types & ZFS_TYPE_SNAPSHOT);

	zph = zfs_get_pool_handle(zhp);
	return (zpool_get_prop_int(zph, ZPOOL_PROP_LISTSNAPS, NULL));
}

/*
 * Called for each dataset.  If the object is of an appropriate type,
 * add it to the avl tree and recurse over any children as necessary.
 */
static int
zfs_callback(zfs_handle_t *zhp, void *data)
{
	callback_data_t *cb = data;
	boolean_t should_close = B_TRUE;
	boolean_t include_snaps = zfs_include_snapshots(zhp, cb);
	boolean_t include_bmarks = (cb->cb_types & ZFS_TYPE_BOOKMARK);

	if ((zfs_get_type(zhp) & cb->cb_types) ||
	    ((zfs_get_type(zhp) == ZFS_TYPE_SNAPSHOT) && include_snaps)) {
		uu_avl_index_t idx;
		zfs_node_t *node = safe_malloc(sizeof (zfs_node_t));

		node->zn_handle = zhp;
		uu_avl_node_init(node, &node->zn_avlnode, avl_pool);
		if (uu_avl_find(cb->cb_avl, node, cb->cb_sortcol,
		    &idx) == NULL) {
			if (cb->cb_proplist) {
				if ((*cb->cb_proplist) &&
				    !(*cb->cb_proplist)->pl_all)
					zfs_prune_proplist(zhp,
					    cb->cb_props_table);

				if (zfs_expand_proplist(zhp, cb->cb_proplist,
				    (cb->cb_flags & ZFS_ITER_RECVD_PROPS),
				    (cb->cb_flags & ZFS_ITER_LITERAL_PROPS))
				    != 0) {
					free(node);
					return (-1);
				}
			}
			uu_avl_insert(cb->cb_avl, node, idx);
			should_close = B_FALSE;
		} else {
			free(node);
		}
	}

	/*
	 * Recurse if necessary.
	 */
	if (cb->cb_flags & ZFS_ITER_RECURSE &&
	    ((cb->cb_flags & ZFS_ITER_DEPTH_LIMIT) == 0 ||
	    cb->cb_depth < cb->cb_depth_limit)) {
		cb->cb_depth++;
		if (zfs_get_type(zhp) == ZFS_TYPE_FILESYSTEM)
			(void) zfs_iter_filesystems(zhp, zfs_callback, data);
		if (((zfs_get_type(zhp) & (ZFS_TYPE_SNAPSHOT |
		    ZFS_TYPE_BOOKMARK)) == 0) && include_snaps)
			(void) zfs_iter_snapshots(zhp,
			    (cb->cb_flags & ZFS_ITER_SIMPLE) != 0, zfs_callback,
			    data);
		if (((zfs_get_type(zhp) & (ZFS_TYPE_SNAPSHOT |
		    ZFS_TYPE_BOOKMARK)) == 0) && include_bmarks)
			(void) zfs_iter_bookmarks(zhp, zfs_callback, data);
		cb->cb_depth--;
	}

	if (should_close)
		zfs_close(zhp);

	return (0);
}

int
zfs_add_sort_column(zfs_sort_column_t **sc, const char *name,
    boolean_t reverse)
{
	zfs_sort_column_t *col;
	zfs_prop_t prop;

	if ((prop = zfs_name_to_prop(name)) == ZPROP_INVAL &&
	    !zfs_prop_user(name))
		return (-1);

	col = safe_malloc(sizeof (zfs_sort_column_t));

	col->sc_prop = prop;
	col->sc_reverse = reverse;
	if (prop == ZPROP_INVAL) {
		col->sc_user_prop = safe_malloc(strlen(name) + 1);
		(void) strcpy(col->sc_user_prop, name);
	}

	if (*sc == NULL) {
		col->sc_last = col;
		*sc = col;
	} else {
		(*sc)->sc_last->sc_next = col;
		(*sc)->sc_last = col;
	}

	return (0);
}

void
zfs_free_sort_columns(zfs_sort_column_t *sc)
{
	zfs_sort_column_t *col;

	while (sc != NULL) {
		col = sc->sc_next;
		free(sc->sc_user_prop);
		free(sc);
		sc = col;
	}
}

boolean_t
zfs_sort_only_by_name(const zfs_sort_column_t *sc)
{

	return (sc != NULL && sc->sc_next == NULL &&
	    sc->sc_prop == ZFS_PROP_NAME);
}

/* ARGSUSED */
static int
zfs_compare(const void *larg, const void *rarg, void *unused)
{
	zfs_handle_t *l = ((zfs_node_t *)larg)->zn_handle;
	zfs_handle_t *r = ((zfs_node_t *)rarg)->zn_handle;
	const char *lname = zfs_get_name(l);
	const char *rname = zfs_get_name(r);
	char *lat, *rat;
	uint64_t lcreate, rcreate;
	int ret;

	lat = (char *)strchr(lname, '@');
	rat = (char *)strchr(rname, '@');

	if (lat != NULL)
		*lat = '\0';
	if (rat != NULL)
		*rat = '\0';

	ret = strcmp(lname, rname);
	if (ret == 0) {
		/*
		 * If we're comparing a dataset to one of its snapshots, we
		 * always make the full dataset first.
		 */
		if (lat == NULL) {
			ret = -1;
		} else if (rat == NULL) {
			ret = 1;
		} else {
			/*
			 * If we have two snapshots from the same dataset, then
			 * we want to sort them according to creation time.  We
			 * use the hidden CREATETXG property to get an absolute
			 * ordering of snapshots.
			 */
			lcreate = zfs_prop_get_int(l, ZFS_PROP_CREATETXG);
			rcreate = zfs_prop_get_int(r, ZFS_PROP_CREATETXG);

			/*
			 * Both lcreate and rcreate being 0 means we don't have
			 * properties and we should compare full name.
			 */
			if (lcreate == 0 && rcreate == 0)
				ret = strcmp(lat + 1, rat + 1);
			else if (lcreate < rcreate)
				ret = -1;
			else if (lcreate > rcreate)
				ret = 1;
		}
	}

	if (lat != NULL)
		*lat = '@';
	if (rat != NULL)
		*rat = '@';

	return (ret);
}

/*
 * Sort datasets by specified columns.
 *
 * o  Numeric types sort in ascending order.
 * o  String types sort in alphabetical order.
 * o  Types inappropriate for a row sort that row to the literal
 *    bottom, regardless of the specified ordering.
 *
 * If no sort columns are specified, or two datasets compare equally
 * across all specified columns, they are sorted alphabetically by name
 * with snapshots grouped under their parents.
 */
static int
zfs_sort(const void *larg, const void *rarg, void *data)
{
	zfs_handle_t *l = ((zfs_node_t *)larg)->zn_handle;
	zfs_handle_t *r = ((zfs_node_t *)rarg)->zn_handle;
	zfs_sort_column_t *sc = (zfs_sort_column_t *)data;
	zfs_sort_column_t *psc;

	for (psc = sc; psc != NULL; psc = psc->sc_next) {
		char lbuf[ZFS_MAXPROPLEN], rbuf[ZFS_MAXPROPLEN];
		char *lstr, *rstr;
		uint64_t lnum, rnum;
		boolean_t lvalid, rvalid;
		int ret = 0;

		/*
		 * We group the checks below the generic code.  If 'lstr' and
		 * 'rstr' are non-NULL, then we do a string based comparison.
		 * Otherwise, we compare 'lnum' and 'rnum'.
		 */
		lstr = rstr = NULL;
		if (psc->sc_prop == ZPROP_INVAL) {
			nvlist_t *luser, *ruser;
			nvlist_t *lval, *rval;

			luser = zfs_get_user_props(l);
			ruser = zfs_get_user_props(r);

			lvalid = (nvlist_lookup_nvlist(luser,
			    psc->sc_user_prop, &lval) == 0);
			rvalid = (nvlist_lookup_nvlist(ruser,
			    psc->sc_user_prop, &rval) == 0);

			if (lvalid)
				verify(nvlist_lookup_string(lval,
				    ZPROP_VALUE, &lstr) == 0);
			if (rvalid)
				verify(nvlist_lookup_string(rval,
				    ZPROP_VALUE, &rstr) == 0);
		} else if (psc->sc_prop == ZFS_PROP_NAME) {
			lvalid = rvalid = B_TRUE;

			(void) strlcpy(lbuf, zfs_get_name(l), sizeof (lbuf));
			(void) strlcpy(rbuf, zfs_get_name(r), sizeof (rbuf));

			lstr = lbuf;
			rstr = rbuf;
		} else if (zfs_prop_is_string(psc->sc_prop)) {
			lvalid = (zfs_prop_get(l, psc->sc_prop, lbuf,
			    sizeof (lbuf), NULL, NULL, 0, B_TRUE) == 0);
			rvalid = (zfs_prop_get(r, psc->sc_prop, rbuf,
			    sizeof (rbuf), NULL, NULL, 0, B_TRUE) == 0);

			lstr = lbuf;
			rstr = rbuf;
		} else {
			lvalid = zfs_prop_valid_for_type(psc->sc_prop,
			    zfs_get_type(l));
			rvalid = zfs_prop_valid_for_type(psc->sc_prop,
			    zfs_get_type(r));

			if (lvalid)
				(void) zfs_prop_get_numeric(l, psc->sc_prop,
				    &lnum, NULL, NULL, 0);
			if (rvalid)
				(void) zfs_prop_get_numeric(r, psc->sc_prop,
				    &rnum, NULL, NULL, 0);
		}

		if (!lvalid && !rvalid)
			continue;
		else if (!lvalid)
			return (1);
		else if (!rvalid)
			return (-1);

		if (lstr)
			ret = strcmp(lstr, rstr);
		else if (lnum < rnum)
			ret = -1;
		else if (lnum > rnum)
			ret = 1;

		if (ret != 0) {
			if (psc->sc_reverse == B_TRUE)
				ret = (ret < 0) ? 1 : -1;
			return (ret);
		}
	}

	return (zfs_compare(larg, rarg, NULL));
}

int
zfs_for_each(int argc, char **argv, int flags, zfs_type_t types,
    zfs_sort_column_t *sortcol, zprop_list_t **proplist, int limit,
    zfs_iter_f callback, void *data)
{
	callback_data_t cb = {0};
	int ret = 0;
	zfs_node_t *node;
	uu_avl_walk_t *walk;

	avl_pool = uu_avl_pool_create("zfs_pool", sizeof (zfs_node_t),
	    offsetof(zfs_node_t, zn_avlnode), zfs_sort, UU_DEFAULT);

	if (avl_pool == NULL)
		nomem();

	cb.cb_sortcol = sortcol;
	cb.cb_flags = flags;
	cb.cb_proplist = proplist;
	cb.cb_types = types;
	cb.cb_depth_limit = limit;
	/*
	 * If cb_proplist is provided then in the zfs_handles created we
	 * retain only those properties listed in cb_proplist and sortcol.
	 * The rest are pruned. So, the caller should make sure that no other
	 * properties other than those listed in cb_proplist/sortcol are
	 * accessed.
	 *
	 * If cb_proplist is NULL then we retain all the properties.  We
	 * always retain the zoned property, which some other properties
	 * need (userquota & friends), and the createtxg property, which
	 * we need to sort snapshots.
	 */
	if (cb.cb_proplist && *cb.cb_proplist) {
		zprop_list_t *p = *cb.cb_proplist;

		while (p) {
			if (p->pl_prop >= ZFS_PROP_TYPE &&
			    p->pl_prop < ZFS_NUM_PROPS) {
				cb.cb_props_table[p->pl_prop] = B_TRUE;
			}
			p = p->pl_next;
		}

		while (sortcol) {
			if (sortcol->sc_prop >= ZFS_PROP_TYPE &&
			    sortcol->sc_prop < ZFS_NUM_PROPS) {
				cb.cb_props_table[sortcol->sc_prop] = B_TRUE;
			}
			sortcol = sortcol->sc_next;
		}

		cb.cb_props_table[ZFS_PROP_ZONED] = B_TRUE;
		cb.cb_props_table[ZFS_PROP_CREATETXG] = B_TRUE;
	} else {
		(void) memset(cb.cb_props_table, B_TRUE,
		    sizeof (cb.cb_props_table));
	}

	if ((cb.cb_avl = uu_avl_create(avl_pool, NULL, UU_DEFAULT)) == NULL)
		nomem();

	if (argc == 0) {
		/*
		 * If given no arguments, iterate over all datasets.
		 */
		cb.cb_flags |= ZFS_ITER_RECURSE;
		ret = zfs_iter_root(g_zfs, zfs_callback, &cb);
	} else {
		int i;
		zfs_handle_t *zhp;
		zfs_type_t argtype;

		/*
		 * If we're recursive, then we always allow filesystems as
		 * arguments.  If we also are interested in snapshots, then we
		 * can take volumes as well.
		 */
		argtype = types;
		if (flags & ZFS_ITER_RECURSE) {
			argtype |= ZFS_TYPE_FILESYSTEM;
			if (types & ZFS_TYPE_SNAPSHOT)
				argtype |= ZFS_TYPE_VOLUME;
		}

		for (i = 0; i < argc; i++) {
			if (flags & ZFS_ITER_ARGS_CAN_BE_PATHS) {
				zhp = zfs_path_to_zhandle(g_zfs, argv[i],
				    argtype);
			} else {
				zhp = zfs_open(g_zfs, argv[i], argtype);
			}
			if (zhp != NULL)
				ret |= zfs_callback(zhp, &cb);
			else
				ret = 1;
		}
	}

	/*
	 * At this point we've got our AVL tree full of zfs handles, so iterate
	 * over each one and execute the real user callback.
	 */
	for (node = uu_avl_first(cb.cb_avl); node != NULL;
	    node = uu_avl_next(cb.cb_avl, node))
		ret |= callback(node->zn_handle, data);

	/*
	 * Finally, clean up the AVL tree.
	 */
	if ((walk = uu_avl_walk_start(cb.cb_avl, UU_WALK_ROBUST)) == NULL)
		nomem();

	while ((node = uu_avl_walk_next(walk)) != NULL) {
		uu_avl_remove(cb.cb_avl, node);
		zfs_close(node->zn_handle);
		free(node);
	}

	uu_avl_walk_end(walk);
	uu_avl_destroy(cb.cb_avl);
	uu_avl_pool_destroy(avl_pool);

	return (ret);
}
