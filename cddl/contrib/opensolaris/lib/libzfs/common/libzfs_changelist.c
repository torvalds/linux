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
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 *
 * Portions Copyright 2007 Ramprakash Jelari
 * Copyright (c) 2011 Pawel Jakub Dawidek <pawel@dawidek.net>.
 * All rights reserved.
 * Copyright (c) 2014, 2016 by Delphix. All rights reserved.
 * Copyright 2016 Igor Kozhukhov <ikozhukhov@gmail.com>
 */

#include <libintl.h>
#include <libuutil.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zone.h>

#include <libzfs.h>

#include "libzfs_impl.h"

/*
 * Structure to keep track of dataset state.  Before changing the 'sharenfs' or
 * 'mountpoint' property, we record whether the filesystem was previously
 * mounted/shared.  This prior state dictates whether we remount/reshare the
 * dataset after the property has been changed.
 *
 * The interface consists of the following sequence of functions:
 *
 * 	changelist_gather()
 * 	changelist_prefix()
 * 	< change property >
 * 	changelist_postfix()
 * 	changelist_free()
 *
 * Other interfaces:
 *
 * changelist_remove() - remove a node from a gathered list
 * changelist_rename() - renames all datasets appropriately when doing a rename
 * changelist_unshare() - unshares all the nodes in a given changelist
 * changelist_haszonedchild() - check if there is any child exported to
 *				a local zone
 */
typedef struct prop_changenode {
	zfs_handle_t		*cn_handle;
	int			cn_shared;
	int			cn_mounted;
	int			cn_zoned;
	boolean_t		cn_needpost;	/* is postfix() needed? */
	uu_list_node_t		cn_listnode;
} prop_changenode_t;

struct prop_changelist {
	zfs_prop_t		cl_prop;
	zfs_prop_t		cl_realprop;
	zfs_prop_t		cl_shareprop;  /* used with sharenfs/sharesmb */
	uu_list_pool_t		*cl_pool;
	uu_list_t		*cl_list;
	boolean_t		cl_waslegacy;
	boolean_t		cl_allchildren;
	boolean_t		cl_alldependents;
	int			cl_mflags;	/* Mount flags */
	int			cl_gflags;	/* Gather request flags */
	boolean_t		cl_haszonedchild;
	boolean_t		cl_sorted;
};

/*
 * If the property is 'mountpoint', go through and unmount filesystems as
 * necessary.  We don't do the same for 'sharenfs', because we can just re-share
 * with different options without interrupting service. We do handle 'sharesmb'
 * since there may be old resource names that need to be removed.
 */
int
changelist_prefix(prop_changelist_t *clp)
{
	prop_changenode_t *cn;
	int ret = 0;

	if (clp->cl_prop != ZFS_PROP_MOUNTPOINT &&
	    clp->cl_prop != ZFS_PROP_SHARESMB)
		return (0);

	for (cn = uu_list_first(clp->cl_list); cn != NULL;
	    cn = uu_list_next(clp->cl_list, cn)) {

		/* if a previous loop failed, set the remaining to false */
		if (ret == -1) {
			cn->cn_needpost = B_FALSE;
			continue;
		}

		/*
		 * If we are in the global zone, but this dataset is exported
		 * to a local zone, do nothing.
		 */
		if (getzoneid() == GLOBAL_ZONEID && cn->cn_zoned)
			continue;

		if (!ZFS_IS_VOLUME(cn->cn_handle)) {
			/*
			 * Do the property specific processing.
			 */
			switch (clp->cl_prop) {
			case ZFS_PROP_MOUNTPOINT:
				if (clp->cl_gflags & CL_GATHER_DONT_UNMOUNT)
					break;
				if (zfs_unmount(cn->cn_handle, NULL,
				    clp->cl_mflags) != 0) {
					ret = -1;
					cn->cn_needpost = B_FALSE;
				}
				break;
			case ZFS_PROP_SHARESMB:
				(void) zfs_unshare_smb(cn->cn_handle, NULL);
				break;

			default:
				break;
			}
		}
	}

	if (ret == -1)
		(void) changelist_postfix(clp);

	return (ret);
}

/*
 * If the property is 'mountpoint' or 'sharenfs', go through and remount and/or
 * reshare the filesystems as necessary.  In changelist_gather() we recorded
 * whether the filesystem was previously shared or mounted.  The action we take
 * depends on the previous state, and whether the value was previously 'legacy'.
 * For non-legacy properties, we only remount/reshare the filesystem if it was
 * previously mounted/shared.  Otherwise, we always remount/reshare the
 * filesystem.
 */
int
changelist_postfix(prop_changelist_t *clp)
{
	prop_changenode_t *cn;
	char shareopts[ZFS_MAXPROPLEN];
	int errors = 0;
	libzfs_handle_t *hdl;
#ifdef illumos
	size_t num_datasets = 0, i;
	zfs_handle_t **zhandle_arr;
	sa_init_selective_arg_t sharearg;
#endif

	/*
	 * If we're changing the mountpoint, attempt to destroy the underlying
	 * mountpoint.  All other datasets will have inherited from this dataset
	 * (in which case their mountpoints exist in the filesystem in the new
	 * location), or have explicit mountpoints set (in which case they won't
	 * be in the changelist).
	 */
	if ((cn = uu_list_last(clp->cl_list)) == NULL)
		return (0);

	if (clp->cl_prop == ZFS_PROP_MOUNTPOINT &&
	    !(clp->cl_gflags & CL_GATHER_DONT_UNMOUNT)) {
		remove_mountpoint(cn->cn_handle);
	}

	/*
	 * It is possible that the changelist_prefix() used libshare
	 * to unshare some entries. Since libshare caches data, an
	 * attempt to reshare during postfix can fail unless libshare
	 * is uninitialized here so that it will reinitialize later.
	 */
	if (cn->cn_handle != NULL) {
		hdl = cn->cn_handle->zfs_hdl;
		assert(hdl != NULL);
		zfs_uninit_libshare(hdl);

#ifdef illumos
		/*
		 * For efficiencies sake, we initialize libshare for only a few
		 * shares (the ones affected here). Future initializations in
		 * this process should just use the cached initialization.
		 */
		for (cn = uu_list_last(clp->cl_list); cn != NULL;
		    cn = uu_list_prev(clp->cl_list, cn)) {
			num_datasets++;
		}

		zhandle_arr = zfs_alloc(hdl,
		    num_datasets * sizeof (zfs_handle_t *));
		for (i = 0, cn = uu_list_last(clp->cl_list); cn != NULL;
		    cn = uu_list_prev(clp->cl_list, cn)) {
			zhandle_arr[i++] = cn->cn_handle;
			zfs_refresh_properties(cn->cn_handle);
		}
		assert(i == num_datasets);
		sharearg.zhandle_arr = zhandle_arr;
		sharearg.zhandle_len = num_datasets;
		errors = zfs_init_libshare_arg(hdl, SA_INIT_SHARE_API_SELECTIVE,
		    &sharearg);
		free(zhandle_arr);
#endif
	}
	/*
	 * We walk the datasets in reverse, because we want to mount any parent
	 * datasets before mounting the children.  We walk all datasets even if
	 * there are errors.
	 */
	for (cn = uu_list_last(clp->cl_list); cn != NULL;
	    cn = uu_list_prev(clp->cl_list, cn)) {

		boolean_t sharenfs;
		boolean_t sharesmb;
		boolean_t mounted;

		/*
		 * If we are in the global zone, but this dataset is exported
		 * to a local zone, do nothing.
		 */
		if (getzoneid() == GLOBAL_ZONEID && cn->cn_zoned)
			continue;

		/* Only do post-processing if it's required */
		if (!cn->cn_needpost)
			continue;
		cn->cn_needpost = B_FALSE;

#ifndef illumos
		zfs_refresh_properties(cn->cn_handle);
#endif

		if (ZFS_IS_VOLUME(cn->cn_handle))
			continue;

		/*
		 * Remount if previously mounted or mountpoint was legacy,
		 * or sharenfs or sharesmb  property is set.
		 */
		sharenfs = ((zfs_prop_get(cn->cn_handle, ZFS_PROP_SHARENFS,
		    shareopts, sizeof (shareopts), NULL, NULL, 0,
		    B_FALSE) == 0) && (strcmp(shareopts, "off") != 0));

		sharesmb = ((zfs_prop_get(cn->cn_handle, ZFS_PROP_SHARESMB,
		    shareopts, sizeof (shareopts), NULL, NULL, 0,
		    B_FALSE) == 0) && (strcmp(shareopts, "off") != 0));

		mounted = (clp->cl_gflags & CL_GATHER_DONT_UNMOUNT) ||
		    zfs_is_mounted(cn->cn_handle, NULL);

		if (!mounted && (cn->cn_mounted ||
		    ((sharenfs || sharesmb || clp->cl_waslegacy) &&
		    (zfs_prop_get_int(cn->cn_handle,
		    ZFS_PROP_CANMOUNT) == ZFS_CANMOUNT_ON)))) {

			if (zfs_mount(cn->cn_handle, NULL, 0) != 0)
				errors++;
			else
				mounted = TRUE;
		}

		/*
		 * If the file system is mounted we always re-share even
		 * if the filesystem is currently shared, so that we can
		 * adopt any new options.
		 */
		if (sharenfs && mounted)
			errors += zfs_share_nfs(cn->cn_handle);
		else if (cn->cn_shared || clp->cl_waslegacy)
			errors += zfs_unshare_nfs(cn->cn_handle, NULL);
		if (sharesmb && mounted)
			errors += zfs_share_smb(cn->cn_handle);
		else if (cn->cn_shared || clp->cl_waslegacy)
			errors += zfs_unshare_smb(cn->cn_handle, NULL);
	}

	return (errors ? -1 : 0);
}

/*
 * Is this "dataset" a child of "parent"?
 */
boolean_t
isa_child_of(const char *dataset, const char *parent)
{
	int len;

	len = strlen(parent);

	if (strncmp(dataset, parent, len) == 0 &&
	    (dataset[len] == '@' || dataset[len] == '/' ||
	    dataset[len] == '\0'))
		return (B_TRUE);
	else
		return (B_FALSE);

}

/*
 * If we rename a filesystem, child filesystem handles are no longer valid
 * since we identify each dataset by its name in the ZFS namespace.  As a
 * result, we have to go through and fix up all the names appropriately.  We
 * could do this automatically if libzfs kept track of all open handles, but
 * this is a lot less work.
 */
void
changelist_rename(prop_changelist_t *clp, const char *src, const char *dst)
{
	prop_changenode_t *cn;
	char newname[ZFS_MAX_DATASET_NAME_LEN];

	for (cn = uu_list_first(clp->cl_list); cn != NULL;
	    cn = uu_list_next(clp->cl_list, cn)) {
		/*
		 * Do not rename a clone that's not in the source hierarchy.
		 */
		if (!isa_child_of(cn->cn_handle->zfs_name, src))
			continue;

		/*
		 * Destroy the previous mountpoint if needed.
		 */
		remove_mountpoint(cn->cn_handle);

		(void) strlcpy(newname, dst, sizeof (newname));
		(void) strcat(newname, cn->cn_handle->zfs_name + strlen(src));

		(void) strlcpy(cn->cn_handle->zfs_name, newname,
		    sizeof (cn->cn_handle->zfs_name));
	}
}

/*
 * Given a gathered changelist for the 'sharenfs' or 'sharesmb' property,
 * unshare all the datasets in the list.
 */
int
changelist_unshare(prop_changelist_t *clp, zfs_share_proto_t *proto)
{
	prop_changenode_t *cn;
	int ret = 0;

	if (clp->cl_prop != ZFS_PROP_SHARENFS &&
	    clp->cl_prop != ZFS_PROP_SHARESMB)
		return (0);

	for (cn = uu_list_first(clp->cl_list); cn != NULL;
	    cn = uu_list_next(clp->cl_list, cn)) {
		if (zfs_unshare_proto(cn->cn_handle, NULL, proto) != 0)
			ret = -1;
	}

	return (ret);
}

/*
 * Check if there is any child exported to a local zone in a given changelist.
 * This information has already been recorded while gathering the changelist
 * via changelist_gather().
 */
int
changelist_haszonedchild(prop_changelist_t *clp)
{
	return (clp->cl_haszonedchild);
}

/*
 * Remove a node from a gathered list.
 */
void
changelist_remove(prop_changelist_t *clp, const char *name)
{
	prop_changenode_t *cn;

	for (cn = uu_list_first(clp->cl_list); cn != NULL;
	    cn = uu_list_next(clp->cl_list, cn)) {

		if (strcmp(cn->cn_handle->zfs_name, name) == 0) {
			uu_list_remove(clp->cl_list, cn);
			zfs_close(cn->cn_handle);
			free(cn);
			return;
		}
	}
}

/*
 * Release any memory associated with a changelist.
 */
void
changelist_free(prop_changelist_t *clp)
{
	prop_changenode_t *cn;
	void *cookie;

	if (clp->cl_list) {
		cookie = NULL;
		while ((cn = uu_list_teardown(clp->cl_list, &cookie)) != NULL) {
			zfs_close(cn->cn_handle);
			free(cn);
		}

		uu_list_destroy(clp->cl_list);
	}
	if (clp->cl_pool)
		uu_list_pool_destroy(clp->cl_pool);

	free(clp);
}

static int
change_one(zfs_handle_t *zhp, void *data)
{
	prop_changelist_t *clp = data;
	char property[ZFS_MAXPROPLEN];
	char where[64];
	prop_changenode_t *cn;
	zprop_source_t sourcetype;
	zprop_source_t share_sourcetype;

	/*
	 * We only want to unmount/unshare those filesystems that may inherit
	 * from the target filesystem.  If we find any filesystem with a
	 * locally set mountpoint, we ignore any children since changing the
	 * property will not affect them.  If this is a rename, we iterate
	 * over all children regardless, since we need them unmounted in
	 * order to do the rename.  Also, if this is a volume and we're doing
	 * a rename, then always add it to the changelist.
	 */

	if (!(ZFS_IS_VOLUME(zhp) && clp->cl_realprop == ZFS_PROP_NAME) &&
	    zfs_prop_get(zhp, clp->cl_prop, property,
	    sizeof (property), &sourcetype, where, sizeof (where),
	    B_FALSE) != 0) {
		zfs_close(zhp);
		return (0);
	}

	/*
	 * If we are "watching" sharenfs or sharesmb
	 * then check out the companion property which is tracked
	 * in cl_shareprop
	 */
	if (clp->cl_shareprop != ZPROP_INVAL &&
	    zfs_prop_get(zhp, clp->cl_shareprop, property,
	    sizeof (property), &share_sourcetype, where, sizeof (where),
	    B_FALSE) != 0) {
		zfs_close(zhp);
		return (0);
	}

	if (clp->cl_alldependents || clp->cl_allchildren ||
	    sourcetype == ZPROP_SRC_DEFAULT ||
	    sourcetype == ZPROP_SRC_INHERITED ||
	    (clp->cl_shareprop != ZPROP_INVAL &&
	    (share_sourcetype == ZPROP_SRC_DEFAULT ||
	    share_sourcetype == ZPROP_SRC_INHERITED))) {
		if ((cn = zfs_alloc(zfs_get_handle(zhp),
		    sizeof (prop_changenode_t))) == NULL) {
			zfs_close(zhp);
			return (-1);
		}

		cn->cn_handle = zhp;
		cn->cn_mounted = (clp->cl_gflags & CL_GATHER_MOUNT_ALWAYS) ||
		    zfs_is_mounted(zhp, NULL);
		cn->cn_shared = zfs_is_shared(zhp);
		cn->cn_zoned = zfs_prop_get_int(zhp, ZFS_PROP_ZONED);
		cn->cn_needpost = B_TRUE;

		/* Indicate if any child is exported to a local zone. */
		if (getzoneid() == GLOBAL_ZONEID && cn->cn_zoned)
			clp->cl_haszonedchild = B_TRUE;

		uu_list_node_init(cn, &cn->cn_listnode, clp->cl_pool);

		if (clp->cl_sorted) {
			uu_list_index_t idx;

			(void) uu_list_find(clp->cl_list, cn, NULL,
			    &idx);
			uu_list_insert(clp->cl_list, cn, idx);
		} else {
			/*
			 * Add this child to beginning of the list. Children
			 * below this one in the hierarchy will get added above
			 * this one in the list. This produces a list in
			 * reverse dataset name order.
			 * This is necessary when the original mountpoint
			 * is legacy or none.
			 */
			verify(uu_list_insert_before(clp->cl_list,
			    uu_list_first(clp->cl_list), cn) == 0);
		}

		if (!clp->cl_alldependents)
			return (zfs_iter_children(zhp, change_one, data));
	} else {
		zfs_close(zhp);
	}

	return (0);
}

/*ARGSUSED*/
static int
compare_mountpoints(const void *a, const void *b, void *unused)
{
	const prop_changenode_t *ca = a;
	const prop_changenode_t *cb = b;

	char mounta[MAXPATHLEN];
	char mountb[MAXPATHLEN];

	boolean_t hasmounta, hasmountb;

	/*
	 * When unsharing or unmounting filesystems, we need to do it in
	 * mountpoint order.  This allows the user to have a mountpoint
	 * hierarchy that is different from the dataset hierarchy, and still
	 * allow it to be changed.  However, if either dataset doesn't have a
	 * mountpoint (because it is a volume or a snapshot), we place it at the
	 * end of the list, because it doesn't affect our change at all.
	 */
	hasmounta = (zfs_prop_get(ca->cn_handle, ZFS_PROP_MOUNTPOINT, mounta,
	    sizeof (mounta), NULL, NULL, 0, B_FALSE) == 0);
	hasmountb = (zfs_prop_get(cb->cn_handle, ZFS_PROP_MOUNTPOINT, mountb,
	    sizeof (mountb), NULL, NULL, 0, B_FALSE) == 0);

	if (!hasmounta && hasmountb)
		return (-1);
	else if (hasmounta && !hasmountb)
		return (1);
	else if (!hasmounta && !hasmountb)
		return (0);
	else
		return (strcmp(mountb, mounta));
}

/*
 * Given a ZFS handle and a property, construct a complete list of datasets
 * that need to be modified as part of this process.  For anything but the
 * 'mountpoint' and 'sharenfs' properties, this just returns an empty list.
 * Otherwise, we iterate over all children and look for any datasets that
 * inherit the property.  For each such dataset, we add it to the list and
 * mark whether it was shared beforehand.
 */
prop_changelist_t *
changelist_gather(zfs_handle_t *zhp, zfs_prop_t prop, int gather_flags,
    int mnt_flags)
{
	prop_changelist_t *clp;
	prop_changenode_t *cn;
	zfs_handle_t *temp;
	char property[ZFS_MAXPROPLEN];
	uu_compare_fn_t *compare = NULL;
	boolean_t legacy = B_FALSE;

	if ((clp = zfs_alloc(zhp->zfs_hdl, sizeof (prop_changelist_t))) == NULL)
		return (NULL);

	/*
	 * For mountpoint-related tasks, we want to sort everything by
	 * mountpoint, so that we mount and unmount them in the appropriate
	 * order, regardless of their position in the hierarchy.
	 */
	if (prop == ZFS_PROP_NAME || prop == ZFS_PROP_ZONED ||
	    prop == ZFS_PROP_MOUNTPOINT || prop == ZFS_PROP_SHARENFS ||
	    prop == ZFS_PROP_SHARESMB) {

		if (zfs_prop_get(zhp, ZFS_PROP_MOUNTPOINT,
		    property, sizeof (property),
		    NULL, NULL, 0, B_FALSE) == 0 &&
		    (strcmp(property, "legacy") == 0 ||
		    strcmp(property, "none") == 0)) {

			legacy = B_TRUE;
		}
		if (!legacy) {
			compare = compare_mountpoints;
			clp->cl_sorted = B_TRUE;
		}
	}

	clp->cl_pool = uu_list_pool_create("changelist_pool",
	    sizeof (prop_changenode_t),
	    offsetof(prop_changenode_t, cn_listnode),
	    compare, 0);
	if (clp->cl_pool == NULL) {
		assert(uu_error() == UU_ERROR_NO_MEMORY);
		(void) zfs_error(zhp->zfs_hdl, EZFS_NOMEM, "internal error");
		changelist_free(clp);
		return (NULL);
	}

	clp->cl_list = uu_list_create(clp->cl_pool, NULL,
	    clp->cl_sorted ? UU_LIST_SORTED : 0);
	clp->cl_gflags = gather_flags;
	clp->cl_mflags = mnt_flags;

	if (clp->cl_list == NULL) {
		assert(uu_error() == UU_ERROR_NO_MEMORY);
		(void) zfs_error(zhp->zfs_hdl, EZFS_NOMEM, "internal error");
		changelist_free(clp);
		return (NULL);
	}

	/*
	 * If this is a rename or the 'zoned' property, we pretend we're
	 * changing the mountpoint and flag it so we can catch all children in
	 * change_one().
	 *
	 * Flag cl_alldependents to catch all children plus the dependents
	 * (clones) that are not in the hierarchy.
	 */
	if (prop == ZFS_PROP_NAME) {
		clp->cl_prop = ZFS_PROP_MOUNTPOINT;
		clp->cl_alldependents = B_TRUE;
	} else if (prop == ZFS_PROP_ZONED) {
		clp->cl_prop = ZFS_PROP_MOUNTPOINT;
		clp->cl_allchildren = B_TRUE;
	} else if (prop == ZFS_PROP_CANMOUNT) {
		clp->cl_prop = ZFS_PROP_MOUNTPOINT;
	} else if (prop == ZFS_PROP_VOLSIZE) {
		clp->cl_prop = ZFS_PROP_MOUNTPOINT;
	} else {
		clp->cl_prop = prop;
	}
	clp->cl_realprop = prop;

	if (clp->cl_prop != ZFS_PROP_MOUNTPOINT &&
	    clp->cl_prop != ZFS_PROP_SHARENFS &&
	    clp->cl_prop != ZFS_PROP_SHARESMB)
		return (clp);

	/*
	 * If watching SHARENFS or SHARESMB then
	 * also watch its companion property.
	 */
	if (clp->cl_prop == ZFS_PROP_SHARENFS)
		clp->cl_shareprop = ZFS_PROP_SHARESMB;
	else if (clp->cl_prop == ZFS_PROP_SHARESMB)
		clp->cl_shareprop = ZFS_PROP_SHARENFS;

	if (clp->cl_alldependents) {
		if (zfs_iter_dependents(zhp, B_TRUE, change_one, clp) != 0) {
			changelist_free(clp);
			return (NULL);
		}
	} else if (zfs_iter_children(zhp, change_one, clp) != 0) {
		changelist_free(clp);
		return (NULL);
	}

	/*
	 * We have to re-open ourselves because we auto-close all the handles
	 * and can't tell the difference.
	 */
	if ((temp = zfs_open(zhp->zfs_hdl, zfs_get_name(zhp),
	    ZFS_TYPE_DATASET)) == NULL) {
		changelist_free(clp);
		return (NULL);
	}

	/*
	 * Always add ourself to the list.  We add ourselves to the end so that
	 * we're the last to be unmounted.
	 */
	if ((cn = zfs_alloc(zhp->zfs_hdl,
	    sizeof (prop_changenode_t))) == NULL) {
		zfs_close(temp);
		changelist_free(clp);
		return (NULL);
	}

	cn->cn_handle = temp;
	cn->cn_mounted = (clp->cl_gflags & CL_GATHER_MOUNT_ALWAYS) ||
	    zfs_is_mounted(temp, NULL);
	cn->cn_shared = zfs_is_shared(temp);
	cn->cn_zoned = zfs_prop_get_int(zhp, ZFS_PROP_ZONED);
	cn->cn_needpost = B_TRUE;

	uu_list_node_init(cn, &cn->cn_listnode, clp->cl_pool);
	if (clp->cl_sorted) {
		uu_list_index_t idx;
		(void) uu_list_find(clp->cl_list, cn, NULL, &idx);
		uu_list_insert(clp->cl_list, cn, idx);
	} else {
		/*
		 * Add the target dataset to the end of the list.
		 * The list is not really unsorted. The list will be
		 * in reverse dataset name order. This is necessary
		 * when the original mountpoint is legacy or none.
		 */
		verify(uu_list_insert_after(clp->cl_list,
		    uu_list_last(clp->cl_list), cn) == 0);
	}

	/*
	 * If the mountpoint property was previously 'legacy', or 'none',
	 * record it as the behavior of changelist_postfix() will be different.
	 */
	if ((clp->cl_prop == ZFS_PROP_MOUNTPOINT) && legacy) {
		/*
		 * do not automatically mount ex-legacy datasets if
		 * we specifically set canmount to noauto
		 */
		if (zfs_prop_get_int(zhp, ZFS_PROP_CANMOUNT) !=
		    ZFS_CANMOUNT_NOAUTO)
			clp->cl_waslegacy = B_TRUE;
	}

	return (clp);
}
