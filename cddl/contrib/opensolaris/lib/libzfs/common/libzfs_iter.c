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
 * Copyright (c) 2013, 2015 by Delphix. All rights reserved.
 * Copyright (c) 2012 Pawel Jakub Dawidek. All rights reserved.
 * Copyright 2014 Nexenta Systems, Inc.  All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <stddef.h>
#include <libintl.h>
#include <libzfs.h>

#include "libzfs_impl.h"

int
zfs_iter_clones(zfs_handle_t *zhp, zfs_iter_f func, void *data)
{
	nvlist_t *nvl = zfs_get_clones_nvl(zhp);
	nvpair_t *pair;

	if (nvl == NULL)
		return (0);

	for (pair = nvlist_next_nvpair(nvl, NULL); pair != NULL;
	    pair = nvlist_next_nvpair(nvl, pair)) {
		zfs_handle_t *clone = zfs_open(zhp->zfs_hdl, nvpair_name(pair),
		    ZFS_TYPE_FILESYSTEM | ZFS_TYPE_VOLUME);
		if (clone != NULL) {
			int err = func(clone, data);
			if (err != 0)
				return (err);
		}
	}
	return (0);
}

static int
zfs_do_list_ioctl(zfs_handle_t *zhp, unsigned long arg, zfs_cmd_t *zc)
{
	int rc;
	uint64_t	orig_cookie;

	orig_cookie = zc->zc_cookie;
top:
	(void) strlcpy(zc->zc_name, zhp->zfs_name, sizeof (zc->zc_name));
	rc = ioctl(zhp->zfs_hdl->libzfs_fd, arg, zc);

	if (rc == -1) {
		switch (errno) {
		case ENOMEM:
			/* expand nvlist memory and try again */
			if (zcmd_expand_dst_nvlist(zhp->zfs_hdl, zc) != 0) {
				zcmd_free_nvlists(zc);
				return (-1);
			}
			zc->zc_cookie = orig_cookie;
			goto top;
		/*
		 * An errno value of ESRCH indicates normal completion.
		 * If ENOENT is returned, then the underlying dataset
		 * has been removed since we obtained the handle.
		 */
		case ESRCH:
		case ENOENT:
			rc = 1;
			break;
		default:
			rc = zfs_standard_error(zhp->zfs_hdl, errno,
			    dgettext(TEXT_DOMAIN,
			    "cannot iterate filesystems"));
			break;
		}
	}
	return (rc);
}

/*
 * Iterate over all child filesystems
 */
int
zfs_iter_filesystems(zfs_handle_t *zhp, zfs_iter_f func, void *data)
{
	zfs_cmd_t zc = { 0 };
	zfs_handle_t *nzhp;
	int ret;

	if (zhp->zfs_type != ZFS_TYPE_FILESYSTEM)
		return (0);

	if (zcmd_alloc_dst_nvlist(zhp->zfs_hdl, &zc, 0) != 0)
		return (-1);

	while ((ret = zfs_do_list_ioctl(zhp, ZFS_IOC_DATASET_LIST_NEXT,
	    &zc)) == 0) {
		/*
		 * Silently ignore errors, as the only plausible explanation is
		 * that the pool has since been removed.
		 */
		if ((nzhp = make_dataset_handle_zc(zhp->zfs_hdl,
		    &zc)) == NULL) {
			continue;
		}

		if ((ret = func(nzhp, data)) != 0) {
			zcmd_free_nvlists(&zc);
			return (ret);
		}
	}
	zcmd_free_nvlists(&zc);
	return ((ret < 0) ? ret : 0);
}

/*
 * Iterate over all snapshots
 */
int
zfs_iter_snapshots(zfs_handle_t *zhp, boolean_t simple, zfs_iter_f func,
    void *data)
{
	zfs_cmd_t zc = { 0 };
	zfs_handle_t *nzhp;
	int ret;

	if (zhp->zfs_type == ZFS_TYPE_SNAPSHOT ||
	    zhp->zfs_type == ZFS_TYPE_BOOKMARK)
		return (0);

	zc.zc_simple = simple;

	if (zcmd_alloc_dst_nvlist(zhp->zfs_hdl, &zc, 0) != 0)
		return (-1);
	while ((ret = zfs_do_list_ioctl(zhp, ZFS_IOC_SNAPSHOT_LIST_NEXT,
	    &zc)) == 0) {

		if (simple)
			nzhp = make_dataset_simple_handle_zc(zhp, &zc);
		else
			nzhp = make_dataset_handle_zc(zhp->zfs_hdl, &zc);
		if (nzhp == NULL)
			continue;

		if ((ret = func(nzhp, data)) != 0) {
			zcmd_free_nvlists(&zc);
			return (ret);
		}
	}
	zcmd_free_nvlists(&zc);
	return ((ret < 0) ? ret : 0);
}

/*
 * Iterate over all bookmarks
 */
int
zfs_iter_bookmarks(zfs_handle_t *zhp, zfs_iter_f func, void *data)
{
	zfs_handle_t *nzhp;
	nvlist_t *props = NULL;
	nvlist_t *bmarks = NULL;
	int err;

	if ((zfs_get_type(zhp) & (ZFS_TYPE_SNAPSHOT | ZFS_TYPE_BOOKMARK)) != 0)
		return (0);

	/* Setup the requested properties nvlist. */
	props = fnvlist_alloc();
	fnvlist_add_boolean(props, zfs_prop_to_name(ZFS_PROP_GUID));
	fnvlist_add_boolean(props, zfs_prop_to_name(ZFS_PROP_CREATETXG));
	fnvlist_add_boolean(props, zfs_prop_to_name(ZFS_PROP_CREATION));

	if ((err = lzc_get_bookmarks(zhp->zfs_name, props, &bmarks)) != 0)
		goto out;

	for (nvpair_t *pair = nvlist_next_nvpair(bmarks, NULL);
	    pair != NULL; pair = nvlist_next_nvpair(bmarks, pair)) {
		char name[ZFS_MAX_DATASET_NAME_LEN];
		char *bmark_name;
		nvlist_t *bmark_props;

		bmark_name = nvpair_name(pair);
		bmark_props = fnvpair_value_nvlist(pair);

		(void) snprintf(name, sizeof (name), "%s#%s", zhp->zfs_name,
		    bmark_name);

		nzhp = make_bookmark_handle(zhp, name, bmark_props);
		if (nzhp == NULL)
			continue;

		if ((err = func(nzhp, data)) != 0)
			goto out;
	}

out:
	fnvlist_free(props);
	fnvlist_free(bmarks);

	return (err);
}

/*
 * Routines for dealing with the sorted snapshot functionality
 */
typedef struct zfs_node {
	zfs_handle_t	*zn_handle;
	avl_node_t	zn_avlnode;
} zfs_node_t;

static int
zfs_sort_snaps(zfs_handle_t *zhp, void *data)
{
	avl_tree_t *avl = data;
	zfs_node_t *node;
	zfs_node_t search;

	search.zn_handle = zhp;
	node = avl_find(avl, &search, NULL);
	if (node) {
		/*
		 * If this snapshot was renamed while we were creating the
		 * AVL tree, it's possible that we already inserted it under
		 * its old name. Remove the old handle before adding the new
		 * one.
		 */
		zfs_close(node->zn_handle);
		avl_remove(avl, node);
		free(node);
	}

	node = zfs_alloc(zhp->zfs_hdl, sizeof (zfs_node_t));
	node->zn_handle = zhp;
	avl_add(avl, node);

	return (0);
}

static int
zfs_snapshot_compare(const void *larg, const void *rarg)
{
	zfs_handle_t *l = ((zfs_node_t *)larg)->zn_handle;
	zfs_handle_t *r = ((zfs_node_t *)rarg)->zn_handle;
	uint64_t lcreate, rcreate;

	/*
	 * Sort them according to creation time.  We use the hidden
	 * CREATETXG property to get an absolute ordering of snapshots.
	 */
	lcreate = zfs_prop_get_int(l, ZFS_PROP_CREATETXG);
	rcreate = zfs_prop_get_int(r, ZFS_PROP_CREATETXG);

	return (AVL_CMP(lcreate, rcreate));
}

int
zfs_iter_snapshots_sorted(zfs_handle_t *zhp, zfs_iter_f callback, void *data)
{
	int ret = 0;
	zfs_node_t *node;
	avl_tree_t avl;
	void *cookie = NULL;

	avl_create(&avl, zfs_snapshot_compare,
	    sizeof (zfs_node_t), offsetof(zfs_node_t, zn_avlnode));

	ret = zfs_iter_snapshots(zhp, B_FALSE, zfs_sort_snaps, &avl);

	for (node = avl_first(&avl); node != NULL; node = AVL_NEXT(&avl, node))
		ret |= callback(node->zn_handle, data);

	while ((node = avl_destroy_nodes(&avl, &cookie)) != NULL)
		free(node);

	avl_destroy(&avl);

	return (ret);
}

typedef struct {
	char *ssa_first;
	char *ssa_last;
	boolean_t ssa_seenfirst;
	boolean_t ssa_seenlast;
	zfs_iter_f ssa_func;
	void *ssa_arg;
} snapspec_arg_t;

static int
snapspec_cb(zfs_handle_t *zhp, void *arg)
{
	snapspec_arg_t *ssa = arg;
	const char *shortsnapname;
	int err = 0;

	if (ssa->ssa_seenlast)
		return (0);

	shortsnapname = strchr(zfs_get_name(zhp), '@') + 1;
	if (!ssa->ssa_seenfirst && strcmp(shortsnapname, ssa->ssa_first) == 0)
		ssa->ssa_seenfirst = B_TRUE;
	if (strcmp(shortsnapname, ssa->ssa_last) == 0)
		ssa->ssa_seenlast = B_TRUE;

	if (ssa->ssa_seenfirst) {
		err = ssa->ssa_func(zhp, ssa->ssa_arg);
	} else {
		zfs_close(zhp);
	}

	return (err);
}

/*
 * spec is a string like "A,B%C,D"
 *
 * <snaps>, where <snaps> can be:
 *      <snap>          (single snapshot)
 *      <snap>%<snap>   (range of snapshots, inclusive)
 *      %<snap>         (range of snapshots, starting with earliest)
 *      <snap>%         (range of snapshots, ending with last)
 *      %               (all snapshots)
 *      <snaps>[,...]   (comma separated list of the above)
 *
 * If a snapshot can not be opened, continue trying to open the others, but
 * return ENOENT at the end.
 */
int
zfs_iter_snapspec(zfs_handle_t *fs_zhp, const char *spec_orig,
    zfs_iter_f func, void *arg)
{
	char *buf, *comma_separated, *cp;
	int err = 0;
	int ret = 0;

	buf = zfs_strdup(fs_zhp->zfs_hdl, spec_orig);
	cp = buf;

	while ((comma_separated = strsep(&cp, ",")) != NULL) {
		char *pct = strchr(comma_separated, '%');
		if (pct != NULL) {
			snapspec_arg_t ssa = { 0 };
			ssa.ssa_func = func;
			ssa.ssa_arg = arg;

			if (pct == comma_separated)
				ssa.ssa_seenfirst = B_TRUE;
			else
				ssa.ssa_first = comma_separated;
			*pct = '\0';
			ssa.ssa_last = pct + 1;

			/*
			 * If there is a lastname specified, make sure it
			 * exists.
			 */
			if (ssa.ssa_last[0] != '\0') {
				char snapname[ZFS_MAX_DATASET_NAME_LEN];
				(void) snprintf(snapname, sizeof (snapname),
				    "%s@%s", zfs_get_name(fs_zhp),
				    ssa.ssa_last);
				if (!zfs_dataset_exists(fs_zhp->zfs_hdl,
				    snapname, ZFS_TYPE_SNAPSHOT)) {
					ret = ENOENT;
					continue;
				}
			}

			err = zfs_iter_snapshots_sorted(fs_zhp,
			    snapspec_cb, &ssa);
			if (ret == 0)
				ret = err;
			if (ret == 0 && (!ssa.ssa_seenfirst ||
			    (ssa.ssa_last[0] != '\0' && !ssa.ssa_seenlast))) {
				ret = ENOENT;
			}
		} else {
			char snapname[ZFS_MAX_DATASET_NAME_LEN];
			zfs_handle_t *snap_zhp;
			(void) snprintf(snapname, sizeof (snapname), "%s@%s",
			    zfs_get_name(fs_zhp), comma_separated);
			snap_zhp = make_dataset_handle(fs_zhp->zfs_hdl,
			    snapname);
			if (snap_zhp == NULL) {
				ret = ENOENT;
				continue;
			}
			err = func(snap_zhp, arg);
			if (ret == 0)
				ret = err;
		}
	}

	free(buf);
	return (ret);
}

/*
 * Iterate over all children, snapshots and filesystems
 * Process snapshots before filesystems because they are nearer the input
 * handle: this is extremely important when used with zfs_iter_f functions
 * looking for data, following the logic that we would like to find it as soon
 * and as close as possible.
 */
int
zfs_iter_children(zfs_handle_t *zhp, zfs_iter_f func, void *data)
{
	int ret;

	if ((ret = zfs_iter_snapshots(zhp, B_FALSE, func, data)) != 0)
		return (ret);

	return (zfs_iter_filesystems(zhp, func, data));
}


typedef struct iter_stack_frame {
	struct iter_stack_frame *next;
	zfs_handle_t *zhp;
} iter_stack_frame_t;

typedef struct iter_dependents_arg {
	boolean_t first;
	boolean_t allowrecursion;
	iter_stack_frame_t *stack;
	zfs_iter_f func;
	void *data;
} iter_dependents_arg_t;

static int
iter_dependents_cb(zfs_handle_t *zhp, void *arg)
{
	iter_dependents_arg_t *ida = arg;
	int err = 0;
	boolean_t first = ida->first;
	ida->first = B_FALSE;

	if (zhp->zfs_type == ZFS_TYPE_SNAPSHOT) {
		err = zfs_iter_clones(zhp, iter_dependents_cb, ida);
	} else if (zhp->zfs_type != ZFS_TYPE_BOOKMARK) {
		iter_stack_frame_t isf;
		iter_stack_frame_t *f;

		/*
		 * check if there is a cycle by seeing if this fs is already
		 * on the stack.
		 */
		for (f = ida->stack; f != NULL; f = f->next) {
			if (f->zhp->zfs_dmustats.dds_guid ==
			    zhp->zfs_dmustats.dds_guid) {
				if (ida->allowrecursion) {
					zfs_close(zhp);
					return (0);
				} else {
					zfs_error_aux(zhp->zfs_hdl,
					    dgettext(TEXT_DOMAIN,
					    "recursive dependency at '%s'"),
					    zfs_get_name(zhp));
					err = zfs_error(zhp->zfs_hdl,
					    EZFS_RECURSIVE,
					    dgettext(TEXT_DOMAIN,
					    "cannot determine dependent "
					    "datasets"));
					zfs_close(zhp);
					return (err);
				}
			}
		}

		isf.zhp = zhp;
		isf.next = ida->stack;
		ida->stack = &isf;
		err = zfs_iter_filesystems(zhp, iter_dependents_cb, ida);
		if (err == 0) {
			err = zfs_iter_snapshots(zhp, B_FALSE,
			    iter_dependents_cb, ida);
		}
		ida->stack = isf.next;
	}

	if (!first && err == 0)
		err = ida->func(zhp, ida->data);
	else
		zfs_close(zhp);

	return (err);
}

int
zfs_iter_dependents(zfs_handle_t *zhp, boolean_t allowrecursion,
    zfs_iter_f func, void *data)
{
	iter_dependents_arg_t ida;
	ida.allowrecursion = allowrecursion;
	ida.stack = NULL;
	ida.func = func;
	ida.data = data;
	ida.first = B_TRUE;
	return (iter_dependents_cb(zfs_handle_dup(zhp), &ida));
}
