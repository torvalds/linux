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
 * Copyright (c) 2018, Joyent, Inc. All rights reserved.
 * Copyright (c) 2011, 2016 by Delphix. All rights reserved.
 * Copyright (c) 2012 DEY Storage Systems, Inc.  All rights reserved.
 * Copyright (c) 2011-2012 Pawel Jakub Dawidek. All rights reserved.
 * Copyright (c) 2013 Martin Matuska. All rights reserved.
 * Copyright (c) 2013 Steven Hartland. All rights reserved.
 * Copyright (c) 2014 Integros [integros.com]
 * Copyright 2017 Nexenta Systems, Inc.
 * Copyright 2016 Igor Kozhukhov <ikozhukhov@gmail.com>
 * Copyright 2017 RackTop Systems.
 */

#include <ctype.h>
#include <errno.h>
#include <libintl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <stddef.h>
#include <zone.h>
#include <fcntl.h>
#include <sys/mntent.h>
#include <sys/mount.h>
#include <priv.h>
#include <pwd.h>
#include <grp.h>
#include <stddef.h>
#ifdef illumos
#include <idmap.h>
#endif

#include <sys/dnode.h>
#include <sys/spa.h>
#include <sys/zap.h>
#include <sys/misc.h>
#include <libzfs.h>

#include "zfs_namecheck.h"
#include "zfs_prop.h"
#include "libzfs_impl.h"
#include "zfs_deleg.h"

static int userquota_propname_decode(const char *propname, boolean_t zoned,
    zfs_userquota_prop_t *typep, char *domain, int domainlen, uint64_t *ridp);

/*
 * Given a single type (not a mask of types), return the type in a human
 * readable form.
 */
const char *
zfs_type_to_name(zfs_type_t type)
{
	switch (type) {
	case ZFS_TYPE_FILESYSTEM:
		return (dgettext(TEXT_DOMAIN, "filesystem"));
	case ZFS_TYPE_SNAPSHOT:
		return (dgettext(TEXT_DOMAIN, "snapshot"));
	case ZFS_TYPE_VOLUME:
		return (dgettext(TEXT_DOMAIN, "volume"));
	case ZFS_TYPE_POOL:
		return (dgettext(TEXT_DOMAIN, "pool"));
	case ZFS_TYPE_BOOKMARK:
		return (dgettext(TEXT_DOMAIN, "bookmark"));
	default:
		assert(!"unhandled zfs_type_t");
	}

	return (NULL);
}

/*
 * Validate a ZFS path.  This is used even before trying to open the dataset, to
 * provide a more meaningful error message.  We call zfs_error_aux() to
 * explain exactly why the name was not valid.
 */
int
zfs_validate_name(libzfs_handle_t *hdl, const char *path, int type,
    boolean_t modifying)
{
	namecheck_err_t why;
	char what;

	if (entity_namecheck(path, &why, &what) != 0) {
		if (hdl != NULL) {
			switch (why) {
			case NAME_ERR_TOOLONG:
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "name is too long"));
				break;

			case NAME_ERR_LEADING_SLASH:
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "leading slash in name"));
				break;

			case NAME_ERR_EMPTY_COMPONENT:
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "empty component in name"));
				break;

			case NAME_ERR_TRAILING_SLASH:
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "trailing slash in name"));
				break;

			case NAME_ERR_INVALCHAR:
				zfs_error_aux(hdl,
				    dgettext(TEXT_DOMAIN, "invalid character "
				    "'%c' in name"), what);
				break;

			case NAME_ERR_MULTIPLE_DELIMITERS:
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "multiple '@' and/or '#' delimiters in "
				    "name"));
				break;

			case NAME_ERR_NOLETTER:
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "pool doesn't begin with a letter"));
				break;

			case NAME_ERR_RESERVED:
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "name is reserved"));
				break;

			case NAME_ERR_DISKLIKE:
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "reserved disk name"));
				break;

			default:
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "(%d) not defined"), why);
				break;
			}
		}

		return (0);
	}

	if (!(type & ZFS_TYPE_SNAPSHOT) && strchr(path, '@') != NULL) {
		if (hdl != NULL)
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "snapshot delimiter '@' is not expected here"));
		return (0);
	}

	if (type == ZFS_TYPE_SNAPSHOT && strchr(path, '@') == NULL) {
		if (hdl != NULL)
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "missing '@' delimiter in snapshot name"));
		return (0);
	}

	if (!(type & ZFS_TYPE_BOOKMARK) && strchr(path, '#') != NULL) {
		if (hdl != NULL)
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "bookmark delimiter '#' is not expected here"));
		return (0);
	}

	if (type == ZFS_TYPE_BOOKMARK && strchr(path, '#') == NULL) {
		if (hdl != NULL)
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "missing '#' delimiter in bookmark name"));
		return (0);
	}

	if (modifying && strchr(path, '%') != NULL) {
		if (hdl != NULL)
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "invalid character %c in name"), '%');
		return (0);
	}

	return (-1);
}

int
zfs_name_valid(const char *name, zfs_type_t type)
{
	if (type == ZFS_TYPE_POOL)
		return (zpool_name_valid(NULL, B_FALSE, name));
	return (zfs_validate_name(NULL, name, type, B_FALSE));
}

/*
 * This function takes the raw DSL properties, and filters out the user-defined
 * properties into a separate nvlist.
 */
static nvlist_t *
process_user_props(zfs_handle_t *zhp, nvlist_t *props)
{
	libzfs_handle_t *hdl = zhp->zfs_hdl;
	nvpair_t *elem;
	nvlist_t *propval;
	nvlist_t *nvl;

	if (nvlist_alloc(&nvl, NV_UNIQUE_NAME, 0) != 0) {
		(void) no_memory(hdl);
		return (NULL);
	}

	elem = NULL;
	while ((elem = nvlist_next_nvpair(props, elem)) != NULL) {
		if (!zfs_prop_user(nvpair_name(elem)))
			continue;

		verify(nvpair_value_nvlist(elem, &propval) == 0);
		if (nvlist_add_nvlist(nvl, nvpair_name(elem), propval) != 0) {
			nvlist_free(nvl);
			(void) no_memory(hdl);
			return (NULL);
		}
	}

	return (nvl);
}

static zpool_handle_t *
zpool_add_handle(zfs_handle_t *zhp, const char *pool_name)
{
	libzfs_handle_t *hdl = zhp->zfs_hdl;
	zpool_handle_t *zph;

	if ((zph = zpool_open_canfail(hdl, pool_name)) != NULL) {
		if (hdl->libzfs_pool_handles != NULL)
			zph->zpool_next = hdl->libzfs_pool_handles;
		hdl->libzfs_pool_handles = zph;
	}
	return (zph);
}

static zpool_handle_t *
zpool_find_handle(zfs_handle_t *zhp, const char *pool_name, int len)
{
	libzfs_handle_t *hdl = zhp->zfs_hdl;
	zpool_handle_t *zph = hdl->libzfs_pool_handles;

	while ((zph != NULL) &&
	    (strncmp(pool_name, zpool_get_name(zph), len) != 0))
		zph = zph->zpool_next;
	return (zph);
}

/*
 * Returns a handle to the pool that contains the provided dataset.
 * If a handle to that pool already exists then that handle is returned.
 * Otherwise, a new handle is created and added to the list of handles.
 */
static zpool_handle_t *
zpool_handle(zfs_handle_t *zhp)
{
	char *pool_name;
	int len;
	zpool_handle_t *zph;

	len = strcspn(zhp->zfs_name, "/@#") + 1;
	pool_name = zfs_alloc(zhp->zfs_hdl, len);
	(void) strlcpy(pool_name, zhp->zfs_name, len);

	zph = zpool_find_handle(zhp, pool_name, len);
	if (zph == NULL)
		zph = zpool_add_handle(zhp, pool_name);

	free(pool_name);
	return (zph);
}

void
zpool_free_handles(libzfs_handle_t *hdl)
{
	zpool_handle_t *next, *zph = hdl->libzfs_pool_handles;

	while (zph != NULL) {
		next = zph->zpool_next;
		zpool_close(zph);
		zph = next;
	}
	hdl->libzfs_pool_handles = NULL;
}

/*
 * Utility function to gather stats (objset and zpl) for the given object.
 */
static int
get_stats_ioctl(zfs_handle_t *zhp, zfs_cmd_t *zc)
{
	libzfs_handle_t *hdl = zhp->zfs_hdl;

	(void) strlcpy(zc->zc_name, zhp->zfs_name, sizeof (zc->zc_name));

	while (ioctl(hdl->libzfs_fd, ZFS_IOC_OBJSET_STATS, zc) != 0) {
		if (errno == ENOMEM) {
			if (zcmd_expand_dst_nvlist(hdl, zc) != 0) {
				return (-1);
			}
		} else {
			return (-1);
		}
	}
	return (0);
}

/*
 * Utility function to get the received properties of the given object.
 */
static int
get_recvd_props_ioctl(zfs_handle_t *zhp)
{
	libzfs_handle_t *hdl = zhp->zfs_hdl;
	nvlist_t *recvdprops;
	zfs_cmd_t zc = { 0 };
	int err;

	if (zcmd_alloc_dst_nvlist(hdl, &zc, 0) != 0)
		return (-1);

	(void) strlcpy(zc.zc_name, zhp->zfs_name, sizeof (zc.zc_name));

	while (ioctl(hdl->libzfs_fd, ZFS_IOC_OBJSET_RECVD_PROPS, &zc) != 0) {
		if (errno == ENOMEM) {
			if (zcmd_expand_dst_nvlist(hdl, &zc) != 0) {
				return (-1);
			}
		} else {
			zcmd_free_nvlists(&zc);
			return (-1);
		}
	}

	err = zcmd_read_dst_nvlist(zhp->zfs_hdl, &zc, &recvdprops);
	zcmd_free_nvlists(&zc);
	if (err != 0)
		return (-1);

	nvlist_free(zhp->zfs_recvd_props);
	zhp->zfs_recvd_props = recvdprops;

	return (0);
}

static int
put_stats_zhdl(zfs_handle_t *zhp, zfs_cmd_t *zc)
{
	nvlist_t *allprops, *userprops;

	zhp->zfs_dmustats = zc->zc_objset_stats; /* structure assignment */

	if (zcmd_read_dst_nvlist(zhp->zfs_hdl, zc, &allprops) != 0) {
		return (-1);
	}

	/*
	 * XXX Why do we store the user props separately, in addition to
	 * storing them in zfs_props?
	 */
	if ((userprops = process_user_props(zhp, allprops)) == NULL) {
		nvlist_free(allprops);
		return (-1);
	}

	nvlist_free(zhp->zfs_props);
	nvlist_free(zhp->zfs_user_props);

	zhp->zfs_props = allprops;
	zhp->zfs_user_props = userprops;

	return (0);
}

static int
get_stats(zfs_handle_t *zhp)
{
	int rc = 0;
	zfs_cmd_t zc = { 0 };

	if (zcmd_alloc_dst_nvlist(zhp->zfs_hdl, &zc, 0) != 0)
		return (-1);
	if (get_stats_ioctl(zhp, &zc) != 0)
		rc = -1;
	else if (put_stats_zhdl(zhp, &zc) != 0)
		rc = -1;
	zcmd_free_nvlists(&zc);
	return (rc);
}

/*
 * Refresh the properties currently stored in the handle.
 */
void
zfs_refresh_properties(zfs_handle_t *zhp)
{
	(void) get_stats(zhp);
}

/*
 * Makes a handle from the given dataset name.  Used by zfs_open() and
 * zfs_iter_* to create child handles on the fly.
 */
static int
make_dataset_handle_common(zfs_handle_t *zhp, zfs_cmd_t *zc)
{
	if (put_stats_zhdl(zhp, zc) != 0)
		return (-1);

	/*
	 * We've managed to open the dataset and gather statistics.  Determine
	 * the high-level type.
	 */
	if (zhp->zfs_dmustats.dds_type == DMU_OST_ZVOL)
		zhp->zfs_head_type = ZFS_TYPE_VOLUME;
	else if (zhp->zfs_dmustats.dds_type == DMU_OST_ZFS)
		zhp->zfs_head_type = ZFS_TYPE_FILESYSTEM;
	else
		abort();

	if (zhp->zfs_dmustats.dds_is_snapshot)
		zhp->zfs_type = ZFS_TYPE_SNAPSHOT;
	else if (zhp->zfs_dmustats.dds_type == DMU_OST_ZVOL)
		zhp->zfs_type = ZFS_TYPE_VOLUME;
	else if (zhp->zfs_dmustats.dds_type == DMU_OST_ZFS)
		zhp->zfs_type = ZFS_TYPE_FILESYSTEM;
	else
		abort();	/* we should never see any other types */

	if ((zhp->zpool_hdl = zpool_handle(zhp)) == NULL)
		return (-1);

	return (0);
}

zfs_handle_t *
make_dataset_handle(libzfs_handle_t *hdl, const char *path)
{
	zfs_cmd_t zc = { 0 };

	zfs_handle_t *zhp = calloc(sizeof (zfs_handle_t), 1);

	if (zhp == NULL)
		return (NULL);

	zhp->zfs_hdl = hdl;
	(void) strlcpy(zhp->zfs_name, path, sizeof (zhp->zfs_name));
	if (zcmd_alloc_dst_nvlist(hdl, &zc, 0) != 0) {
		free(zhp);
		return (NULL);
	}
	if (get_stats_ioctl(zhp, &zc) == -1) {
		zcmd_free_nvlists(&zc);
		free(zhp);
		return (NULL);
	}
	if (make_dataset_handle_common(zhp, &zc) == -1) {
		free(zhp);
		zhp = NULL;
	}
	zcmd_free_nvlists(&zc);
	return (zhp);
}

zfs_handle_t *
make_dataset_handle_zc(libzfs_handle_t *hdl, zfs_cmd_t *zc)
{
	zfs_handle_t *zhp = calloc(sizeof (zfs_handle_t), 1);

	if (zhp == NULL)
		return (NULL);

	zhp->zfs_hdl = hdl;
	(void) strlcpy(zhp->zfs_name, zc->zc_name, sizeof (zhp->zfs_name));
	if (make_dataset_handle_common(zhp, zc) == -1) {
		free(zhp);
		return (NULL);
	}
	return (zhp);
}

zfs_handle_t *
make_dataset_simple_handle_zc(zfs_handle_t *pzhp, zfs_cmd_t *zc)
{
	zfs_handle_t *zhp = calloc(sizeof (zfs_handle_t), 1);

	if (zhp == NULL)
		return (NULL);

	zhp->zfs_hdl = pzhp->zfs_hdl;
	(void) strlcpy(zhp->zfs_name, zc->zc_name, sizeof (zhp->zfs_name));
	zhp->zfs_head_type = pzhp->zfs_type;
	zhp->zfs_type = ZFS_TYPE_SNAPSHOT;
	zhp->zpool_hdl = zpool_handle(zhp);
	return (zhp);
}

zfs_handle_t *
zfs_handle_dup(zfs_handle_t *zhp_orig)
{
	zfs_handle_t *zhp = calloc(sizeof (zfs_handle_t), 1);

	if (zhp == NULL)
		return (NULL);

	zhp->zfs_hdl = zhp_orig->zfs_hdl;
	zhp->zpool_hdl = zhp_orig->zpool_hdl;
	(void) strlcpy(zhp->zfs_name, zhp_orig->zfs_name,
	    sizeof (zhp->zfs_name));
	zhp->zfs_type = zhp_orig->zfs_type;
	zhp->zfs_head_type = zhp_orig->zfs_head_type;
	zhp->zfs_dmustats = zhp_orig->zfs_dmustats;
	if (zhp_orig->zfs_props != NULL) {
		if (nvlist_dup(zhp_orig->zfs_props, &zhp->zfs_props, 0) != 0) {
			(void) no_memory(zhp->zfs_hdl);
			zfs_close(zhp);
			return (NULL);
		}
	}
	if (zhp_orig->zfs_user_props != NULL) {
		if (nvlist_dup(zhp_orig->zfs_user_props,
		    &zhp->zfs_user_props, 0) != 0) {
			(void) no_memory(zhp->zfs_hdl);
			zfs_close(zhp);
			return (NULL);
		}
	}
	if (zhp_orig->zfs_recvd_props != NULL) {
		if (nvlist_dup(zhp_orig->zfs_recvd_props,
		    &zhp->zfs_recvd_props, 0)) {
			(void) no_memory(zhp->zfs_hdl);
			zfs_close(zhp);
			return (NULL);
		}
	}
	zhp->zfs_mntcheck = zhp_orig->zfs_mntcheck;
	if (zhp_orig->zfs_mntopts != NULL) {
		zhp->zfs_mntopts = zfs_strdup(zhp_orig->zfs_hdl,
		    zhp_orig->zfs_mntopts);
	}
	zhp->zfs_props_table = zhp_orig->zfs_props_table;
	return (zhp);
}

boolean_t
zfs_bookmark_exists(const char *path)
{
	nvlist_t *bmarks;
	nvlist_t *props;
	char fsname[ZFS_MAX_DATASET_NAME_LEN];
	char *bmark_name;
	char *pound;
	int err;
	boolean_t rv;


	(void) strlcpy(fsname, path, sizeof (fsname));
	pound = strchr(fsname, '#');
	if (pound == NULL)
		return (B_FALSE);

	*pound = '\0';
	bmark_name = pound + 1;
	props = fnvlist_alloc();
	err = lzc_get_bookmarks(fsname, props, &bmarks);
	nvlist_free(props);
	if (err != 0) {
		nvlist_free(bmarks);
		return (B_FALSE);
	}

	rv = nvlist_exists(bmarks, bmark_name);
	nvlist_free(bmarks);
	return (rv);
}

zfs_handle_t *
make_bookmark_handle(zfs_handle_t *parent, const char *path,
    nvlist_t *bmark_props)
{
	zfs_handle_t *zhp = calloc(sizeof (zfs_handle_t), 1);

	if (zhp == NULL)
		return (NULL);

	/* Fill in the name. */
	zhp->zfs_hdl = parent->zfs_hdl;
	(void) strlcpy(zhp->zfs_name, path, sizeof (zhp->zfs_name));

	/* Set the property lists. */
	if (nvlist_dup(bmark_props, &zhp->zfs_props, 0) != 0) {
		free(zhp);
		return (NULL);
	}

	/* Set the types. */
	zhp->zfs_head_type = parent->zfs_head_type;
	zhp->zfs_type = ZFS_TYPE_BOOKMARK;

	if ((zhp->zpool_hdl = zpool_handle(zhp)) == NULL) {
		nvlist_free(zhp->zfs_props);
		free(zhp);
		return (NULL);
	}

	return (zhp);
}

struct zfs_open_bookmarks_cb_data {
	const char *path;
	zfs_handle_t *zhp;
};

static int
zfs_open_bookmarks_cb(zfs_handle_t *zhp, void *data)
{
	struct zfs_open_bookmarks_cb_data *dp = data;

	/*
	 * Is it the one we are looking for?
	 */
	if (strcmp(dp->path, zfs_get_name(zhp)) == 0) {
		/*
		 * We found it.  Save it and let the caller know we are done.
		 */
		dp->zhp = zhp;
		return (EEXIST);
	}

	/*
	 * Not found.  Close the handle and ask for another one.
	 */
	zfs_close(zhp);
	return (0);
}

/*
 * Opens the given snapshot, bookmark, filesystem, or volume.   The 'types'
 * argument is a mask of acceptable types.  The function will print an
 * appropriate error message and return NULL if it can't be opened.
 */
zfs_handle_t *
zfs_open(libzfs_handle_t *hdl, const char *path, int types)
{
	zfs_handle_t *zhp;
	char errbuf[1024];
	char *bookp;

	(void) snprintf(errbuf, sizeof (errbuf),
	    dgettext(TEXT_DOMAIN, "cannot open '%s'"), path);

	/*
	 * Validate the name before we even try to open it.
	 */
	if (!zfs_validate_name(hdl, path, types, B_FALSE)) {
		(void) zfs_error(hdl, EZFS_INVALIDNAME, errbuf);
		return (NULL);
	}

	/*
	 * Bookmarks needs to be handled separately.
	 */
	bookp = strchr(path, '#');
	if (bookp == NULL) {
		/*
		 * Try to get stats for the dataset, which will tell us if it
		 * exists.
		 */
		errno = 0;
		if ((zhp = make_dataset_handle(hdl, path)) == NULL) {
			(void) zfs_standard_error(hdl, errno, errbuf);
			return (NULL);
		}
	} else {
		char dsname[ZFS_MAX_DATASET_NAME_LEN];
		zfs_handle_t *pzhp;
		struct zfs_open_bookmarks_cb_data cb_data = {path, NULL};

		/*
		 * We need to cut out '#' and everything after '#'
		 * to get the parent dataset name only.
		 */
		assert(bookp - path < sizeof (dsname));
		(void) strncpy(dsname, path, bookp - path);
		dsname[bookp - path] = '\0';

		/*
		 * Create handle for the parent dataset.
		 */
		errno = 0;
		if ((pzhp = make_dataset_handle(hdl, dsname)) == NULL) {
			(void) zfs_standard_error(hdl, errno, errbuf);
			return (NULL);
		}

		/*
		 * Iterate bookmarks to find the right one.
		 */
		errno = 0;
		if ((zfs_iter_bookmarks(pzhp, zfs_open_bookmarks_cb,
		    &cb_data) == 0) && (cb_data.zhp == NULL)) {
			(void) zfs_error(hdl, EZFS_NOENT, errbuf);
			zfs_close(pzhp);
			return (NULL);
		}
		if (cb_data.zhp == NULL) {
			(void) zfs_standard_error(hdl, errno, errbuf);
			zfs_close(pzhp);
			return (NULL);
		}
		zhp = cb_data.zhp;

		/*
		 * Cleanup.
		 */
		zfs_close(pzhp);
	}

	if (zhp == NULL) {
		char *at = strchr(path, '@');

		if (at != NULL)
			*at = '\0';
		errno = 0;
		if ((zhp = make_dataset_handle(hdl, path)) == NULL) {
			(void) zfs_standard_error(hdl, errno, errbuf);
			return (NULL);
		}
		if (at != NULL)
			*at = '@';
		(void) strlcpy(zhp->zfs_name, path, sizeof (zhp->zfs_name));
		zhp->zfs_type = ZFS_TYPE_SNAPSHOT;
	}

	if (!(types & zhp->zfs_type)) {
		(void) zfs_error(hdl, EZFS_BADTYPE, errbuf);
		zfs_close(zhp);
		return (NULL);
	}

	return (zhp);
}

/*
 * Release a ZFS handle.  Nothing to do but free the associated memory.
 */
void
zfs_close(zfs_handle_t *zhp)
{
	if (zhp->zfs_mntopts)
		free(zhp->zfs_mntopts);
	nvlist_free(zhp->zfs_props);
	nvlist_free(zhp->zfs_user_props);
	nvlist_free(zhp->zfs_recvd_props);
	free(zhp);
}

typedef struct mnttab_node {
	struct mnttab mtn_mt;
	avl_node_t mtn_node;
} mnttab_node_t;

static int
libzfs_mnttab_cache_compare(const void *arg1, const void *arg2)
{
	const mnttab_node_t *mtn1 = (const mnttab_node_t *)arg1;
	const mnttab_node_t *mtn2 = (const mnttab_node_t *)arg2;
	int rv;

	rv = strcmp(mtn1->mtn_mt.mnt_special, mtn2->mtn_mt.mnt_special);

	return (AVL_ISIGN(rv));
}

void
libzfs_mnttab_init(libzfs_handle_t *hdl)
{
	pthread_mutex_init(&hdl->libzfs_mnttab_cache_lock, NULL);
	assert(avl_numnodes(&hdl->libzfs_mnttab_cache) == 0);
	avl_create(&hdl->libzfs_mnttab_cache, libzfs_mnttab_cache_compare,
	    sizeof (mnttab_node_t), offsetof(mnttab_node_t, mtn_node));
}

void
libzfs_mnttab_update(libzfs_handle_t *hdl)
{
	struct mnttab entry;

	rewind(hdl->libzfs_mnttab);
	while (getmntent(hdl->libzfs_mnttab, &entry) == 0) {
		mnttab_node_t *mtn;

		if (strcmp(entry.mnt_fstype, MNTTYPE_ZFS) != 0)
			continue;
		mtn = zfs_alloc(hdl, sizeof (mnttab_node_t));
		mtn->mtn_mt.mnt_special = zfs_strdup(hdl, entry.mnt_special);
		mtn->mtn_mt.mnt_mountp = zfs_strdup(hdl, entry.mnt_mountp);
		mtn->mtn_mt.mnt_fstype = zfs_strdup(hdl, entry.mnt_fstype);
		mtn->mtn_mt.mnt_mntopts = zfs_strdup(hdl, entry.mnt_mntopts);
		avl_add(&hdl->libzfs_mnttab_cache, mtn);
	}
}

void
libzfs_mnttab_fini(libzfs_handle_t *hdl)
{
	void *cookie = NULL;
	mnttab_node_t *mtn;

	while ((mtn = avl_destroy_nodes(&hdl->libzfs_mnttab_cache, &cookie))
	    != NULL) {
		free(mtn->mtn_mt.mnt_special);
		free(mtn->mtn_mt.mnt_mountp);
		free(mtn->mtn_mt.mnt_fstype);
		free(mtn->mtn_mt.mnt_mntopts);
		free(mtn);
	}
	avl_destroy(&hdl->libzfs_mnttab_cache);
	(void) pthread_mutex_destroy(&hdl->libzfs_mnttab_cache_lock);
}

void
libzfs_mnttab_cache(libzfs_handle_t *hdl, boolean_t enable)
{
	hdl->libzfs_mnttab_enable = enable;
}

int
libzfs_mnttab_find(libzfs_handle_t *hdl, const char *fsname,
    struct mnttab *entry)
{
	mnttab_node_t find;
	mnttab_node_t *mtn;
	int ret = ENOENT;

	if (!hdl->libzfs_mnttab_enable) {
		struct mnttab srch = { 0 };

		if (avl_numnodes(&hdl->libzfs_mnttab_cache))
			libzfs_mnttab_fini(hdl);
		rewind(hdl->libzfs_mnttab);
		srch.mnt_special = (char *)fsname;
		srch.mnt_fstype = MNTTYPE_ZFS;
		if (getmntany(hdl->libzfs_mnttab, entry, &srch) == 0)
			return (0);
		else
			return (ENOENT);
	}

	pthread_mutex_lock(&hdl->libzfs_mnttab_cache_lock);
	if (avl_numnodes(&hdl->libzfs_mnttab_cache) == 0)
		libzfs_mnttab_update(hdl);

	find.mtn_mt.mnt_special = (char *)fsname;
	mtn = avl_find(&hdl->libzfs_mnttab_cache, &find, NULL);
	if (mtn) {
		*entry = mtn->mtn_mt;
		ret = 0;
	}
	pthread_mutex_unlock(&hdl->libzfs_mnttab_cache_lock);
	return (ret);
}

void
libzfs_mnttab_add(libzfs_handle_t *hdl, const char *special,
    const char *mountp, const char *mntopts)
{
	mnttab_node_t *mtn;

	pthread_mutex_lock(&hdl->libzfs_mnttab_cache_lock);
	if (avl_numnodes(&hdl->libzfs_mnttab_cache) == 0) {
		mtn = zfs_alloc(hdl, sizeof (mnttab_node_t));
		mtn->mtn_mt.mnt_special = zfs_strdup(hdl, special);
		mtn->mtn_mt.mnt_mountp = zfs_strdup(hdl, mountp);
		mtn->mtn_mt.mnt_fstype = zfs_strdup(hdl, MNTTYPE_ZFS);
		mtn->mtn_mt.mnt_mntopts = zfs_strdup(hdl, mntopts);
		avl_add(&hdl->libzfs_mnttab_cache, mtn);
	}
	pthread_mutex_unlock(&hdl->libzfs_mnttab_cache_lock);
}		

void
libzfs_mnttab_remove(libzfs_handle_t *hdl, const char *fsname)
{
	mnttab_node_t find;
	mnttab_node_t *ret;

	pthread_mutex_lock(&hdl->libzfs_mnttab_cache_lock);
	find.mtn_mt.mnt_special = (char *)fsname;
	if ((ret = avl_find(&hdl->libzfs_mnttab_cache, (void *)&find, NULL))
	    != NULL) {
		avl_remove(&hdl->libzfs_mnttab_cache, ret);
		free(ret->mtn_mt.mnt_special);
		free(ret->mtn_mt.mnt_mountp);
		free(ret->mtn_mt.mnt_fstype);
		free(ret->mtn_mt.mnt_mntopts);
		free(ret);
	}
	pthread_mutex_unlock(&hdl->libzfs_mnttab_cache_lock);
}

int
zfs_spa_version(zfs_handle_t *zhp, int *spa_version)
{
	zpool_handle_t *zpool_handle = zhp->zpool_hdl;

	if (zpool_handle == NULL)
		return (-1);

	*spa_version = zpool_get_prop_int(zpool_handle,
	    ZPOOL_PROP_VERSION, NULL);
	return (0);
}

/*
 * The choice of reservation property depends on the SPA version.
 */
static int
zfs_which_resv_prop(zfs_handle_t *zhp, zfs_prop_t *resv_prop)
{
	int spa_version;

	if (zfs_spa_version(zhp, &spa_version) < 0)
		return (-1);

	if (spa_version >= SPA_VERSION_REFRESERVATION)
		*resv_prop = ZFS_PROP_REFRESERVATION;
	else
		*resv_prop = ZFS_PROP_RESERVATION;

	return (0);
}

/*
 * Given an nvlist of properties to set, validates that they are correct, and
 * parses any numeric properties (index, boolean, etc) if they are specified as
 * strings.
 */
nvlist_t *
zfs_valid_proplist(libzfs_handle_t *hdl, zfs_type_t type, nvlist_t *nvl,
    uint64_t zoned, zfs_handle_t *zhp, zpool_handle_t *zpool_hdl,
    const char *errbuf)
{
	nvpair_t *elem;
	uint64_t intval;
	char *strval;
	zfs_prop_t prop;
	nvlist_t *ret;
	int chosen_normal = -1;
	int chosen_utf = -1;

	if (nvlist_alloc(&ret, NV_UNIQUE_NAME, 0) != 0) {
		(void) no_memory(hdl);
		return (NULL);
	}

	/*
	 * Make sure this property is valid and applies to this type.
	 */

	elem = NULL;
	while ((elem = nvlist_next_nvpair(nvl, elem)) != NULL) {
		const char *propname = nvpair_name(elem);

		prop = zfs_name_to_prop(propname);
		if (prop == ZPROP_INVAL && zfs_prop_user(propname)) {
			/*
			 * This is a user property: make sure it's a
			 * string, and that it's less than ZAP_MAXNAMELEN.
			 */
			if (nvpair_type(elem) != DATA_TYPE_STRING) {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "'%s' must be a string"), propname);
				(void) zfs_error(hdl, EZFS_BADPROP, errbuf);
				goto error;
			}

			if (strlen(nvpair_name(elem)) >= ZAP_MAXNAMELEN) {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "property name '%s' is too long"),
				    propname);
				(void) zfs_error(hdl, EZFS_BADPROP, errbuf);
				goto error;
			}

			(void) nvpair_value_string(elem, &strval);
			if (nvlist_add_string(ret, propname, strval) != 0) {
				(void) no_memory(hdl);
				goto error;
			}
			continue;
		}

		/*
		 * Currently, only user properties can be modified on
		 * snapshots.
		 */
		if (type == ZFS_TYPE_SNAPSHOT) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "this property can not be modified for snapshots"));
			(void) zfs_error(hdl, EZFS_PROPTYPE, errbuf);
			goto error;
		}

		if (prop == ZPROP_INVAL && zfs_prop_userquota(propname)) {
			zfs_userquota_prop_t uqtype;
			char newpropname[128];
			char domain[128];
			uint64_t rid;
			uint64_t valary[3];

			if (userquota_propname_decode(propname, zoned,
			    &uqtype, domain, sizeof (domain), &rid) != 0) {
				zfs_error_aux(hdl,
				    dgettext(TEXT_DOMAIN,
				    "'%s' has an invalid user/group name"),
				    propname);
				(void) zfs_error(hdl, EZFS_BADPROP, errbuf);
				goto error;
			}

			if (uqtype != ZFS_PROP_USERQUOTA &&
			    uqtype != ZFS_PROP_GROUPQUOTA) {
				zfs_error_aux(hdl,
				    dgettext(TEXT_DOMAIN, "'%s' is readonly"),
				    propname);
				(void) zfs_error(hdl, EZFS_PROPREADONLY,
				    errbuf);
				goto error;
			}

			if (nvpair_type(elem) == DATA_TYPE_STRING) {
				(void) nvpair_value_string(elem, &strval);
				if (strcmp(strval, "none") == 0) {
					intval = 0;
				} else if (zfs_nicestrtonum(hdl,
				    strval, &intval) != 0) {
					(void) zfs_error(hdl,
					    EZFS_BADPROP, errbuf);
					goto error;
				}
			} else if (nvpair_type(elem) ==
			    DATA_TYPE_UINT64) {
				(void) nvpair_value_uint64(elem, &intval);
				if (intval == 0) {
					zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
					    "use 'none' to disable "
					    "userquota/groupquota"));
					goto error;
				}
			} else {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "'%s' must be a number"), propname);
				(void) zfs_error(hdl, EZFS_BADPROP, errbuf);
				goto error;
			}

			/*
			 * Encode the prop name as
			 * userquota@<hex-rid>-domain, to make it easy
			 * for the kernel to decode.
			 */
			(void) snprintf(newpropname, sizeof (newpropname),
			    "%s%llx-%s", zfs_userquota_prop_prefixes[uqtype],
			    (longlong_t)rid, domain);
			valary[0] = uqtype;
			valary[1] = rid;
			valary[2] = intval;
			if (nvlist_add_uint64_array(ret, newpropname,
			    valary, 3) != 0) {
				(void) no_memory(hdl);
				goto error;
			}
			continue;
		} else if (prop == ZPROP_INVAL && zfs_prop_written(propname)) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "'%s' is readonly"),
			    propname);
			(void) zfs_error(hdl, EZFS_PROPREADONLY, errbuf);
			goto error;
		}

		if (prop == ZPROP_INVAL) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "invalid property '%s'"), propname);
			(void) zfs_error(hdl, EZFS_BADPROP, errbuf);
			goto error;
		}

		if (!zfs_prop_valid_for_type(prop, type)) {
			zfs_error_aux(hdl,
			    dgettext(TEXT_DOMAIN, "'%s' does not "
			    "apply to datasets of this type"), propname);
			(void) zfs_error(hdl, EZFS_PROPTYPE, errbuf);
			goto error;
		}

		if (zfs_prop_readonly(prop) &&
		    (!zfs_prop_setonce(prop) || zhp != NULL)) {
			zfs_error_aux(hdl,
			    dgettext(TEXT_DOMAIN, "'%s' is readonly"),
			    propname);
			(void) zfs_error(hdl, EZFS_PROPREADONLY, errbuf);
			goto error;
		}

		if (zprop_parse_value(hdl, elem, prop, type, ret,
		    &strval, &intval, errbuf) != 0)
			goto error;

		/*
		 * Perform some additional checks for specific properties.
		 */
		switch (prop) {
		case ZFS_PROP_VERSION:
		{
			int version;

			if (zhp == NULL)
				break;
			version = zfs_prop_get_int(zhp, ZFS_PROP_VERSION);
			if (intval < version) {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "Can not downgrade; already at version %u"),
				    version);
				(void) zfs_error(hdl, EZFS_BADPROP, errbuf);
				goto error;
			}
			break;
		}

		case ZFS_PROP_VOLBLOCKSIZE:
		case ZFS_PROP_RECORDSIZE:
		{
			int maxbs = SPA_MAXBLOCKSIZE;
			if (zpool_hdl != NULL) {
				maxbs = zpool_get_prop_int(zpool_hdl,
				    ZPOOL_PROP_MAXBLOCKSIZE, NULL);
			}
			/*
			 * Volumes are limited to a volblocksize of 128KB,
			 * because they typically service workloads with
			 * small random writes, which incur a large performance
			 * penalty with large blocks.
			 */
			if (prop == ZFS_PROP_VOLBLOCKSIZE)
				maxbs = SPA_OLD_MAXBLOCKSIZE;
			/*
			 * The value must be a power of two between
			 * SPA_MINBLOCKSIZE and maxbs.
			 */
			if (intval < SPA_MINBLOCKSIZE ||
			    intval > maxbs || !ISP2(intval)) {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "'%s' must be power of 2 from 512B "
				    "to %uKB"), propname, maxbs >> 10);
				(void) zfs_error(hdl, EZFS_BADPROP, errbuf);
				goto error;
			}
			break;
		}
		case ZFS_PROP_MLSLABEL:
		{
#ifdef illumos
			/*
			 * Verify the mlslabel string and convert to
			 * internal hex label string.
			 */

			m_label_t *new_sl;
			char *hex = NULL;	/* internal label string */

			/* Default value is already OK. */
			if (strcasecmp(strval, ZFS_MLSLABEL_DEFAULT) == 0)
				break;

			/* Verify the label can be converted to binary form */
			if (((new_sl = m_label_alloc(MAC_LABEL)) == NULL) ||
			    (str_to_label(strval, &new_sl, MAC_LABEL,
			    L_NO_CORRECTION, NULL) == -1)) {
				goto badlabel;
			}

			/* Now translate to hex internal label string */
			if (label_to_str(new_sl, &hex, M_INTERNAL,
			    DEF_NAMES) != 0) {
				if (hex)
					free(hex);
				goto badlabel;
			}
			m_label_free(new_sl);

			/* If string is already in internal form, we're done. */
			if (strcmp(strval, hex) == 0) {
				free(hex);
				break;
			}

			/* Replace the label string with the internal form. */
			(void) nvlist_remove(ret, zfs_prop_to_name(prop),
			    DATA_TYPE_STRING);
			verify(nvlist_add_string(ret, zfs_prop_to_name(prop),
			    hex) == 0);
			free(hex);

			break;

badlabel:
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "invalid mlslabel '%s'"), strval);
			(void) zfs_error(hdl, EZFS_BADPROP, errbuf);
			m_label_free(new_sl);	/* OK if null */
#else	/* !illumos */
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "mlslabel is not supported on FreeBSD"));
			(void) zfs_error(hdl, EZFS_BADPROP, errbuf);
#endif	/* illumos */
			goto error;

		}

		case ZFS_PROP_MOUNTPOINT:
		{
			namecheck_err_t why;

			if (strcmp(strval, ZFS_MOUNTPOINT_NONE) == 0 ||
			    strcmp(strval, ZFS_MOUNTPOINT_LEGACY) == 0)
				break;

			if (mountpoint_namecheck(strval, &why)) {
				switch (why) {
				case NAME_ERR_LEADING_SLASH:
					zfs_error_aux(hdl,
					    dgettext(TEXT_DOMAIN,
					    "'%s' must be an absolute path, "
					    "'none', or 'legacy'"), propname);
					break;
				case NAME_ERR_TOOLONG:
					zfs_error_aux(hdl,
					    dgettext(TEXT_DOMAIN,
					    "component of '%s' is too long"),
					    propname);
					break;

				default:
					zfs_error_aux(hdl,
					    dgettext(TEXT_DOMAIN,
					    "(%d) not defined"),
					    why);
					break;
				}
				(void) zfs_error(hdl, EZFS_BADPROP, errbuf);
				goto error;
			}
		}

			/*FALLTHRU*/

		case ZFS_PROP_SHARESMB:
		case ZFS_PROP_SHARENFS:
			/*
			 * For the mountpoint and sharenfs or sharesmb
			 * properties, check if it can be set in a
			 * global/non-global zone based on
			 * the zoned property value:
			 *
			 *		global zone	    non-global zone
			 * --------------------------------------------------
			 * zoned=on	mountpoint (no)	    mountpoint (yes)
			 *		sharenfs (no)	    sharenfs (no)
			 *		sharesmb (no)	    sharesmb (no)
			 *
			 * zoned=off	mountpoint (yes)	N/A
			 *		sharenfs (yes)
			 *		sharesmb (yes)
			 */
			if (zoned) {
				if (getzoneid() == GLOBAL_ZONEID) {
					zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
					    "'%s' cannot be set on "
					    "dataset in a non-global zone"),
					    propname);
					(void) zfs_error(hdl, EZFS_ZONED,
					    errbuf);
					goto error;
				} else if (prop == ZFS_PROP_SHARENFS ||
				    prop == ZFS_PROP_SHARESMB) {
					zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
					    "'%s' cannot be set in "
					    "a non-global zone"), propname);
					(void) zfs_error(hdl, EZFS_ZONED,
					    errbuf);
					goto error;
				}
			} else if (getzoneid() != GLOBAL_ZONEID) {
				/*
				 * If zoned property is 'off', this must be in
				 * a global zone. If not, something is wrong.
				 */
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "'%s' cannot be set while dataset "
				    "'zoned' property is set"), propname);
				(void) zfs_error(hdl, EZFS_ZONED, errbuf);
				goto error;
			}

			/*
			 * At this point, it is legitimate to set the
			 * property. Now we want to make sure that the
			 * property value is valid if it is sharenfs.
			 */
			if ((prop == ZFS_PROP_SHARENFS ||
			    prop == ZFS_PROP_SHARESMB) &&
			    strcmp(strval, "on") != 0 &&
			    strcmp(strval, "off") != 0) {
				zfs_share_proto_t proto;

				if (prop == ZFS_PROP_SHARESMB)
					proto = PROTO_SMB;
				else
					proto = PROTO_NFS;

				/*
				 * Must be an valid sharing protocol
				 * option string so init the libshare
				 * in order to enable the parser and
				 * then parse the options. We use the
				 * control API since we don't care about
				 * the current configuration and don't
				 * want the overhead of loading it
				 * until we actually do something.
				 */

				if (zfs_init_libshare(hdl,
				    SA_INIT_CONTROL_API) != SA_OK) {
					/*
					 * An error occurred so we can't do
					 * anything
					 */
					zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
					    "'%s' cannot be set: problem "
					    "in share initialization"),
					    propname);
					(void) zfs_error(hdl, EZFS_BADPROP,
					    errbuf);
					goto error;
				}

				if (zfs_parse_options(strval, proto) != SA_OK) {
					/*
					 * There was an error in parsing so
					 * deal with it by issuing an error
					 * message and leaving after
					 * uninitializing the the libshare
					 * interface.
					 */
					zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
					    "'%s' cannot be set to invalid "
					    "options"), propname);
					(void) zfs_error(hdl, EZFS_BADPROP,
					    errbuf);
					zfs_uninit_libshare(hdl);
					goto error;
				}
				zfs_uninit_libshare(hdl);
			}

			break;

		case ZFS_PROP_UTF8ONLY:
			chosen_utf = (int)intval;
			break;

		case ZFS_PROP_NORMALIZE:
			chosen_normal = (int)intval;
			break;

		default:
			break;
		}

		/*
		 * For changes to existing volumes, we have some additional
		 * checks to enforce.
		 */
		if (type == ZFS_TYPE_VOLUME && zhp != NULL) {
			uint64_t volsize = zfs_prop_get_int(zhp,
			    ZFS_PROP_VOLSIZE);
			uint64_t blocksize = zfs_prop_get_int(zhp,
			    ZFS_PROP_VOLBLOCKSIZE);
			char buf[64];

			switch (prop) {
			case ZFS_PROP_RESERVATION:
				if (intval > volsize) {
					zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
					    "'%s' is greater than current "
					    "volume size"), propname);
					(void) zfs_error(hdl, EZFS_BADPROP,
					    errbuf);
					goto error;
				}
				break;

			case ZFS_PROP_REFRESERVATION:
				if (intval > volsize && intval != UINT64_MAX) {
					zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
					    "'%s' is greater than current "
					    "volume size"), propname);
					(void) zfs_error(hdl, EZFS_BADPROP,
					    errbuf);
					goto error;
				}
				break;

			case ZFS_PROP_VOLSIZE:
				if (intval % blocksize != 0) {
					zfs_nicenum(blocksize, buf,
					    sizeof (buf));
					zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
					    "'%s' must be a multiple of "
					    "volume block size (%s)"),
					    propname, buf);
					(void) zfs_error(hdl, EZFS_BADPROP,
					    errbuf);
					goto error;
				}

				if (intval == 0) {
					zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
					    "'%s' cannot be zero"),
					    propname);
					(void) zfs_error(hdl, EZFS_BADPROP,
					    errbuf);
					goto error;
				}
				break;

			default:
				break;
			}
		}
	}

	/*
	 * If normalization was chosen, but no UTF8 choice was made,
	 * enforce rejection of non-UTF8 names.
	 *
	 * If normalization was chosen, but rejecting non-UTF8 names
	 * was explicitly not chosen, it is an error.
	 */
	if (chosen_normal > 0 && chosen_utf < 0) {
		if (nvlist_add_uint64(ret,
		    zfs_prop_to_name(ZFS_PROP_UTF8ONLY), 1) != 0) {
			(void) no_memory(hdl);
			goto error;
		}
	} else if (chosen_normal > 0 && chosen_utf == 0) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "'%s' must be set 'on' if normalization chosen"),
		    zfs_prop_to_name(ZFS_PROP_UTF8ONLY));
		(void) zfs_error(hdl, EZFS_BADPROP, errbuf);
		goto error;
	}
	return (ret);

error:
	nvlist_free(ret);
	return (NULL);
}

int
zfs_add_synthetic_resv(zfs_handle_t *zhp, nvlist_t *nvl)
{
	uint64_t old_volsize;
	uint64_t new_volsize;
	uint64_t old_reservation;
	uint64_t new_reservation;
	zfs_prop_t resv_prop;
	nvlist_t *props;

	/*
	 * If this is an existing volume, and someone is setting the volsize,
	 * make sure that it matches the reservation, or add it if necessary.
	 */
	old_volsize = zfs_prop_get_int(zhp, ZFS_PROP_VOLSIZE);
	if (zfs_which_resv_prop(zhp, &resv_prop) < 0)
		return (-1);
	old_reservation = zfs_prop_get_int(zhp, resv_prop);

	props = fnvlist_alloc();
	fnvlist_add_uint64(props, zfs_prop_to_name(ZFS_PROP_VOLBLOCKSIZE),
	    zfs_prop_get_int(zhp, ZFS_PROP_VOLBLOCKSIZE));

	if ((zvol_volsize_to_reservation(old_volsize, props) !=
	    old_reservation) || nvlist_exists(nvl,
	    zfs_prop_to_name(resv_prop))) {
		fnvlist_free(props);
		return (0);
	}
	if (nvlist_lookup_uint64(nvl, zfs_prop_to_name(ZFS_PROP_VOLSIZE),
	    &new_volsize) != 0) {
		fnvlist_free(props);
		return (-1);
	}
	new_reservation = zvol_volsize_to_reservation(new_volsize, props);
	fnvlist_free(props);

	if (nvlist_add_uint64(nvl, zfs_prop_to_name(resv_prop),
	    new_reservation) != 0) {
		(void) no_memory(zhp->zfs_hdl);
		return (-1);
	}
	return (1);
}

/*
 * Helper for 'zfs {set|clone} refreservation=auto'.  Must be called after
 * zfs_valid_proplist(), as it is what sets the UINT64_MAX sentinal value.
 * Return codes must match zfs_add_synthetic_resv().
 */
static int
zfs_fix_auto_resv(zfs_handle_t *zhp, nvlist_t *nvl)
{
	uint64_t volsize;
	uint64_t resvsize;
	zfs_prop_t prop;
	nvlist_t *props;

	if (!ZFS_IS_VOLUME(zhp)) {
		return (0);
	}

	if (zfs_which_resv_prop(zhp, &prop) != 0) {
		return (-1);
	}

	if (prop != ZFS_PROP_REFRESERVATION) {
		return (0);
	}

	if (nvlist_lookup_uint64(nvl, zfs_prop_to_name(prop), &resvsize) != 0) {
		/* No value being set, so it can't be "auto" */
		return (0);
	}
	if (resvsize != UINT64_MAX) {
		/* Being set to a value other than "auto" */
		return (0);
	}

	props = fnvlist_alloc();

	fnvlist_add_uint64(props, zfs_prop_to_name(ZFS_PROP_VOLBLOCKSIZE),
	    zfs_prop_get_int(zhp, ZFS_PROP_VOLBLOCKSIZE));

	if (nvlist_lookup_uint64(nvl, zfs_prop_to_name(ZFS_PROP_VOLSIZE),
	    &volsize) != 0) {
		volsize = zfs_prop_get_int(zhp, ZFS_PROP_VOLSIZE);
	}

	resvsize = zvol_volsize_to_reservation(volsize, props);
	fnvlist_free(props);

	(void) nvlist_remove_all(nvl, zfs_prop_to_name(prop));
	if (nvlist_add_uint64(nvl, zfs_prop_to_name(prop), resvsize) != 0) {
		(void) no_memory(zhp->zfs_hdl);
		return (-1);
	}
	return (1);
}

void
zfs_setprop_error(libzfs_handle_t *hdl, zfs_prop_t prop, int err,
    char *errbuf)
{
	switch (err) {

	case ENOSPC:
		/*
		 * For quotas and reservations, ENOSPC indicates
		 * something different; setting a quota or reservation
		 * doesn't use any disk space.
		 */
		switch (prop) {
		case ZFS_PROP_QUOTA:
		case ZFS_PROP_REFQUOTA:
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "size is less than current used or "
			    "reserved space"));
			(void) zfs_error(hdl, EZFS_PROPSPACE, errbuf);
			break;

		case ZFS_PROP_RESERVATION:
		case ZFS_PROP_REFRESERVATION:
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "size is greater than available space"));
			(void) zfs_error(hdl, EZFS_PROPSPACE, errbuf);
			break;

		default:
			(void) zfs_standard_error(hdl, err, errbuf);
			break;
		}
		break;

	case EBUSY:
		(void) zfs_standard_error(hdl, EBUSY, errbuf);
		break;

	case EROFS:
		(void) zfs_error(hdl, EZFS_DSREADONLY, errbuf);
		break;

	case E2BIG:
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "property value too long"));
		(void) zfs_error(hdl, EZFS_BADPROP, errbuf);
		break;

	case ENOTSUP:
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "pool and or dataset must be upgraded to set this "
		    "property or value"));
		(void) zfs_error(hdl, EZFS_BADVERSION, errbuf);
		break;

	case ERANGE:
	case EDOM:
		if (prop == ZFS_PROP_COMPRESSION ||
		    prop == ZFS_PROP_RECORDSIZE) {
			(void) zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "property setting is not allowed on "
			    "bootable datasets"));
			(void) zfs_error(hdl, EZFS_NOTSUP, errbuf);
		} else if (prop == ZFS_PROP_CHECKSUM ||
		    prop == ZFS_PROP_DEDUP) {
			(void) zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "property setting is not allowed on "
			    "root pools"));
			(void) zfs_error(hdl, EZFS_NOTSUP, errbuf);
		} else {
			(void) zfs_standard_error(hdl, err, errbuf);
		}
		break;

	case EINVAL:
		if (prop == ZPROP_INVAL) {
			(void) zfs_error(hdl, EZFS_BADPROP, errbuf);
		} else {
			(void) zfs_standard_error(hdl, err, errbuf);
		}
		break;

	case EOVERFLOW:
		/*
		 * This platform can't address a volume this big.
		 */
#ifdef _ILP32
		if (prop == ZFS_PROP_VOLSIZE) {
			(void) zfs_error(hdl, EZFS_VOLTOOBIG, errbuf);
			break;
		}
#endif
		/* FALLTHROUGH */
	default:
		(void) zfs_standard_error(hdl, err, errbuf);
	}
}

/*
 * Given a property name and value, set the property for the given dataset.
 */
int
zfs_prop_set(zfs_handle_t *zhp, const char *propname, const char *propval)
{
	int ret = -1;
	char errbuf[1024];
	libzfs_handle_t *hdl = zhp->zfs_hdl;
	nvlist_t *nvl = NULL;

	(void) snprintf(errbuf, sizeof (errbuf),
	    dgettext(TEXT_DOMAIN, "cannot set property for '%s'"),
	    zhp->zfs_name);

	if (nvlist_alloc(&nvl, NV_UNIQUE_NAME, 0) != 0 ||
	    nvlist_add_string(nvl, propname, propval) != 0) {
		(void) no_memory(hdl);
		goto error;
	}

	ret = zfs_prop_set_list(zhp, nvl);

error:
	nvlist_free(nvl);
	return (ret);
}



/*
 * Given an nvlist of property names and values, set the properties for the
 * given dataset.
 */
int
zfs_prop_set_list(zfs_handle_t *zhp, nvlist_t *props)
{
	zfs_cmd_t zc = { 0 };
	int ret = -1;
	prop_changelist_t **cls = NULL;
	int cl_idx;
	char errbuf[1024];
	libzfs_handle_t *hdl = zhp->zfs_hdl;
	nvlist_t *nvl;
	int nvl_len;
	int added_resv = 0;

	(void) snprintf(errbuf, sizeof (errbuf),
	    dgettext(TEXT_DOMAIN, "cannot set property for '%s'"),
	    zhp->zfs_name);

	if ((nvl = zfs_valid_proplist(hdl, zhp->zfs_type, props,
	    zfs_prop_get_int(zhp, ZFS_PROP_ZONED), zhp, zhp->zpool_hdl,
	    errbuf)) == NULL)
		goto error;

	/*
	 * We have to check for any extra properties which need to be added
	 * before computing the length of the nvlist.
	 */
	for (nvpair_t *elem = nvlist_next_nvpair(nvl, NULL);
	    elem != NULL;
	    elem = nvlist_next_nvpair(nvl, elem)) {
		if (zfs_name_to_prop(nvpair_name(elem)) == ZFS_PROP_VOLSIZE &&
		    (added_resv = zfs_add_synthetic_resv(zhp, nvl)) == -1) {
			goto error;
		}
	}

	if (added_resv != 1 &&
	    (added_resv = zfs_fix_auto_resv(zhp, nvl)) == -1) {
		goto error;
	}

	/*
	 * Check how many properties we're setting and allocate an array to
	 * store changelist pointers for postfix().
	 */
	nvl_len = 0;
	for (nvpair_t *elem = nvlist_next_nvpair(nvl, NULL);
	    elem != NULL;
	    elem = nvlist_next_nvpair(nvl, elem))
		nvl_len++;
	if ((cls = calloc(nvl_len, sizeof (prop_changelist_t *))) == NULL)
		goto error;

	cl_idx = 0;
	for (nvpair_t *elem = nvlist_next_nvpair(nvl, NULL);
	    elem != NULL;
	    elem = nvlist_next_nvpair(nvl, elem)) {

		zfs_prop_t prop = zfs_name_to_prop(nvpair_name(elem));

		assert(cl_idx < nvl_len);
		/*
		 * We don't want to unmount & remount the dataset when changing
		 * its canmount property to 'on' or 'noauto'.  We only use
		 * the changelist logic to unmount when setting canmount=off.
		 */
		if (prop != ZFS_PROP_CANMOUNT ||
		    (fnvpair_value_uint64(elem) == ZFS_CANMOUNT_OFF &&
		    zfs_is_mounted(zhp, NULL))) {
			cls[cl_idx] = changelist_gather(zhp, prop, 0, 0);
			if (cls[cl_idx] == NULL)
				goto error;
		}

		if (prop == ZFS_PROP_MOUNTPOINT &&
		    changelist_haszonedchild(cls[cl_idx])) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "child dataset with inherited mountpoint is used "
			    "in a non-global zone"));
			ret = zfs_error(hdl, EZFS_ZONED, errbuf);
			goto error;
		}

		/* We don't support those properties on FreeBSD. */
		switch (prop) {
		case ZFS_PROP_DEVICES:
		case ZFS_PROP_ISCSIOPTIONS:
		case ZFS_PROP_XATTR:
		case ZFS_PROP_VSCAN:
		case ZFS_PROP_NBMAND:
		case ZFS_PROP_MLSLABEL:
			(void) snprintf(errbuf, sizeof (errbuf),
			    "property '%s' not supported on FreeBSD",
			    nvpair_name(elem));
			ret = zfs_error(hdl, EZFS_PERM, errbuf);
			goto error;
		}

		if (cls[cl_idx] != NULL &&
		    (ret = changelist_prefix(cls[cl_idx])) != 0)
			goto error;

		cl_idx++;
	}
	assert(cl_idx == nvl_len);

	/*
	 * Execute the corresponding ioctl() to set this list of properties.
	 */
	(void) strlcpy(zc.zc_name, zhp->zfs_name, sizeof (zc.zc_name));

	if ((ret = zcmd_write_src_nvlist(hdl, &zc, nvl)) != 0 ||
	    (ret = zcmd_alloc_dst_nvlist(hdl, &zc, 0)) != 0)
		goto error;

	ret = zfs_ioctl(hdl, ZFS_IOC_SET_PROP, &zc);

	if (ret != 0) {
		/* Get the list of unset properties back and report them. */
		nvlist_t *errorprops = NULL;
		if (zcmd_read_dst_nvlist(hdl, &zc, &errorprops) != 0)
			goto error;
		for (nvpair_t *elem = nvlist_next_nvpair(nvl, NULL);
		    elem != NULL;
		    elem = nvlist_next_nvpair(nvl, elem)) {
			zfs_prop_t prop = zfs_name_to_prop(nvpair_name(elem));
			zfs_setprop_error(hdl, prop, errno, errbuf);
		}
		nvlist_free(errorprops);

		if (added_resv && errno == ENOSPC) {
			/* clean up the volsize property we tried to set */
			uint64_t old_volsize = zfs_prop_get_int(zhp,
			    ZFS_PROP_VOLSIZE);
			nvlist_free(nvl);
			nvl = NULL;
			zcmd_free_nvlists(&zc);

			if (nvlist_alloc(&nvl, NV_UNIQUE_NAME, 0) != 0)
				goto error;
			if (nvlist_add_uint64(nvl,
			    zfs_prop_to_name(ZFS_PROP_VOLSIZE),
			    old_volsize) != 0)
				goto error;
			if (zcmd_write_src_nvlist(hdl, &zc, nvl) != 0)
				goto error;
			(void) zfs_ioctl(hdl, ZFS_IOC_SET_PROP, &zc);
		}
	} else {
		for (cl_idx = 0; cl_idx < nvl_len; cl_idx++) {
			if (cls[cl_idx] != NULL) {
				int clp_err = changelist_postfix(cls[cl_idx]);
				if (clp_err != 0)
					ret = clp_err;
			}
		}

		/*
		 * Refresh the statistics so the new property value
		 * is reflected.
		 */
		if (ret == 0)
			(void) get_stats(zhp);
	}

error:
	nvlist_free(nvl);
	zcmd_free_nvlists(&zc);
	if (cls != NULL) {
		for (cl_idx = 0; cl_idx < nvl_len; cl_idx++) {
			if (cls[cl_idx] != NULL)
				changelist_free(cls[cl_idx]);
		}
		free(cls);
	}
	return (ret);
}

/*
 * Given a property, inherit the value from the parent dataset, or if received
 * is TRUE, revert to the received value, if any.
 */
int
zfs_prop_inherit(zfs_handle_t *zhp, const char *propname, boolean_t received)
{
	zfs_cmd_t zc = { 0 };
	int ret;
	prop_changelist_t *cl;
	libzfs_handle_t *hdl = zhp->zfs_hdl;
	char errbuf[1024];
	zfs_prop_t prop;

	(void) snprintf(errbuf, sizeof (errbuf), dgettext(TEXT_DOMAIN,
	    "cannot inherit %s for '%s'"), propname, zhp->zfs_name);

	zc.zc_cookie = received;
	if ((prop = zfs_name_to_prop(propname)) == ZPROP_INVAL) {
		/*
		 * For user properties, the amount of work we have to do is very
		 * small, so just do it here.
		 */
		if (!zfs_prop_user(propname)) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "invalid property"));
			return (zfs_error(hdl, EZFS_BADPROP, errbuf));
		}

		(void) strlcpy(zc.zc_name, zhp->zfs_name, sizeof (zc.zc_name));
		(void) strlcpy(zc.zc_value, propname, sizeof (zc.zc_value));

		if (zfs_ioctl(zhp->zfs_hdl, ZFS_IOC_INHERIT_PROP, &zc) != 0)
			return (zfs_standard_error(hdl, errno, errbuf));

		return (0);
	}

	/*
	 * Verify that this property is inheritable.
	 */
	if (zfs_prop_readonly(prop))
		return (zfs_error(hdl, EZFS_PROPREADONLY, errbuf));

	if (!zfs_prop_inheritable(prop) && !received)
		return (zfs_error(hdl, EZFS_PROPNONINHERIT, errbuf));

	/*
	 * Check to see if the value applies to this type
	 */
	if (!zfs_prop_valid_for_type(prop, zhp->zfs_type))
		return (zfs_error(hdl, EZFS_PROPTYPE, errbuf));

	/*
	 * Normalize the name, to get rid of shorthand abbreviations.
	 */
	propname = zfs_prop_to_name(prop);
	(void) strlcpy(zc.zc_name, zhp->zfs_name, sizeof (zc.zc_name));
	(void) strlcpy(zc.zc_value, propname, sizeof (zc.zc_value));

	if (prop == ZFS_PROP_MOUNTPOINT && getzoneid() == GLOBAL_ZONEID &&
	    zfs_prop_get_int(zhp, ZFS_PROP_ZONED)) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "dataset is used in a non-global zone"));
		return (zfs_error(hdl, EZFS_ZONED, errbuf));
	}

	/*
	 * Determine datasets which will be affected by this change, if any.
	 */
	if ((cl = changelist_gather(zhp, prop, 0, 0)) == NULL)
		return (-1);

	if (prop == ZFS_PROP_MOUNTPOINT && changelist_haszonedchild(cl)) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "child dataset with inherited mountpoint is used "
		    "in a non-global zone"));
		ret = zfs_error(hdl, EZFS_ZONED, errbuf);
		goto error;
	}

	if ((ret = changelist_prefix(cl)) != 0)
		goto error;

	if ((ret = zfs_ioctl(zhp->zfs_hdl, ZFS_IOC_INHERIT_PROP, &zc)) != 0) {
		return (zfs_standard_error(hdl, errno, errbuf));
	} else {

		if ((ret = changelist_postfix(cl)) != 0)
			goto error;

		/*
		 * Refresh the statistics so the new property is reflected.
		 */
		(void) get_stats(zhp);
	}

error:
	changelist_free(cl);
	return (ret);
}

/*
 * True DSL properties are stored in an nvlist.  The following two functions
 * extract them appropriately.
 */
static uint64_t
getprop_uint64(zfs_handle_t *zhp, zfs_prop_t prop, char **source)
{
	nvlist_t *nv;
	uint64_t value;

	*source = NULL;
	if (nvlist_lookup_nvlist(zhp->zfs_props,
	    zfs_prop_to_name(prop), &nv) == 0) {
		verify(nvlist_lookup_uint64(nv, ZPROP_VALUE, &value) == 0);
		(void) nvlist_lookup_string(nv, ZPROP_SOURCE, source);
	} else {
		verify(!zhp->zfs_props_table ||
		    zhp->zfs_props_table[prop] == B_TRUE);
		value = zfs_prop_default_numeric(prop);
		*source = "";
	}

	return (value);
}

static const char *
getprop_string(zfs_handle_t *zhp, zfs_prop_t prop, char **source)
{
	nvlist_t *nv;
	const char *value;

	*source = NULL;
	if (nvlist_lookup_nvlist(zhp->zfs_props,
	    zfs_prop_to_name(prop), &nv) == 0) {
		value = fnvlist_lookup_string(nv, ZPROP_VALUE);
		(void) nvlist_lookup_string(nv, ZPROP_SOURCE, source);
	} else {
		verify(!zhp->zfs_props_table ||
		    zhp->zfs_props_table[prop] == B_TRUE);
		value = zfs_prop_default_string(prop);
		*source = "";
	}

	return (value);
}

static boolean_t
zfs_is_recvd_props_mode(zfs_handle_t *zhp)
{
	return (zhp->zfs_props == zhp->zfs_recvd_props);
}

static void
zfs_set_recvd_props_mode(zfs_handle_t *zhp, uint64_t *cookie)
{
	*cookie = (uint64_t)(uintptr_t)zhp->zfs_props;
	zhp->zfs_props = zhp->zfs_recvd_props;
}

static void
zfs_unset_recvd_props_mode(zfs_handle_t *zhp, uint64_t *cookie)
{
	zhp->zfs_props = (nvlist_t *)(uintptr_t)*cookie;
	*cookie = 0;
}

/*
 * Internal function for getting a numeric property.  Both zfs_prop_get() and
 * zfs_prop_get_int() are built using this interface.
 *
 * Certain properties can be overridden using 'mount -o'.  In this case, scan
 * the contents of the /etc/mnttab entry, searching for the appropriate options.
 * If they differ from the on-disk values, report the current values and mark
 * the source "temporary".
 */
static int
get_numeric_property(zfs_handle_t *zhp, zfs_prop_t prop, zprop_source_t *src,
    char **source, uint64_t *val)
{
	zfs_cmd_t zc = { 0 };
	nvlist_t *zplprops = NULL;
	struct mnttab mnt;
	char *mntopt_on = NULL;
	char *mntopt_off = NULL;
	boolean_t received = zfs_is_recvd_props_mode(zhp);

	*source = NULL;

	switch (prop) {
	case ZFS_PROP_ATIME:
		mntopt_on = MNTOPT_ATIME;
		mntopt_off = MNTOPT_NOATIME;
		break;

	case ZFS_PROP_DEVICES:
		mntopt_on = MNTOPT_DEVICES;
		mntopt_off = MNTOPT_NODEVICES;
		break;

	case ZFS_PROP_EXEC:
		mntopt_on = MNTOPT_EXEC;
		mntopt_off = MNTOPT_NOEXEC;
		break;

	case ZFS_PROP_READONLY:
		mntopt_on = MNTOPT_RO;
		mntopt_off = MNTOPT_RW;
		break;

	case ZFS_PROP_SETUID:
		mntopt_on = MNTOPT_SETUID;
		mntopt_off = MNTOPT_NOSETUID;
		break;

	case ZFS_PROP_XATTR:
		mntopt_on = MNTOPT_XATTR;
		mntopt_off = MNTOPT_NOXATTR;
		break;

	case ZFS_PROP_NBMAND:
		mntopt_on = MNTOPT_NBMAND;
		mntopt_off = MNTOPT_NONBMAND;
		break;

	default:
		break;
	}

	/*
	 * Because looking up the mount options is potentially expensive
	 * (iterating over all of /etc/mnttab), we defer its calculation until
	 * we're looking up a property which requires its presence.
	 */
	if (!zhp->zfs_mntcheck &&
	    (mntopt_on != NULL || prop == ZFS_PROP_MOUNTED)) {
		libzfs_handle_t *hdl = zhp->zfs_hdl;
		struct mnttab entry;

		if (libzfs_mnttab_find(hdl, zhp->zfs_name, &entry) == 0) {
			zhp->zfs_mntopts = zfs_strdup(hdl,
			    entry.mnt_mntopts);
			if (zhp->zfs_mntopts == NULL)
				return (-1);
		}

		zhp->zfs_mntcheck = B_TRUE;
	}

	if (zhp->zfs_mntopts == NULL)
		mnt.mnt_mntopts = "";
	else
		mnt.mnt_mntopts = zhp->zfs_mntopts;

	switch (prop) {
	case ZFS_PROP_ATIME:
	case ZFS_PROP_DEVICES:
	case ZFS_PROP_EXEC:
	case ZFS_PROP_READONLY:
	case ZFS_PROP_SETUID:
	case ZFS_PROP_XATTR:
	case ZFS_PROP_NBMAND:
		*val = getprop_uint64(zhp, prop, source);

		if (received)
			break;

		if (hasmntopt(&mnt, mntopt_on) && !*val) {
			*val = B_TRUE;
			if (src)
				*src = ZPROP_SRC_TEMPORARY;
		} else if (hasmntopt(&mnt, mntopt_off) && *val) {
			*val = B_FALSE;
			if (src)
				*src = ZPROP_SRC_TEMPORARY;
		}
		break;

	case ZFS_PROP_CANMOUNT:
	case ZFS_PROP_VOLSIZE:
	case ZFS_PROP_QUOTA:
	case ZFS_PROP_REFQUOTA:
	case ZFS_PROP_RESERVATION:
	case ZFS_PROP_REFRESERVATION:
	case ZFS_PROP_FILESYSTEM_LIMIT:
	case ZFS_PROP_SNAPSHOT_LIMIT:
	case ZFS_PROP_FILESYSTEM_COUNT:
	case ZFS_PROP_SNAPSHOT_COUNT:
		*val = getprop_uint64(zhp, prop, source);

		if (*source == NULL) {
			/* not default, must be local */
			*source = zhp->zfs_name;
		}
		break;

	case ZFS_PROP_MOUNTED:
		*val = (zhp->zfs_mntopts != NULL);
		break;

	case ZFS_PROP_NUMCLONES:
		*val = zhp->zfs_dmustats.dds_num_clones;
		break;

	case ZFS_PROP_VERSION:
	case ZFS_PROP_NORMALIZE:
	case ZFS_PROP_UTF8ONLY:
	case ZFS_PROP_CASE:
		if (!zfs_prop_valid_for_type(prop, zhp->zfs_head_type) ||
		    zcmd_alloc_dst_nvlist(zhp->zfs_hdl, &zc, 0) != 0)
			return (-1);
		(void) strlcpy(zc.zc_name, zhp->zfs_name, sizeof (zc.zc_name));
		if (zfs_ioctl(zhp->zfs_hdl, ZFS_IOC_OBJSET_ZPLPROPS, &zc)) {
			zcmd_free_nvlists(&zc);
			return (-1);
		}
		if (zcmd_read_dst_nvlist(zhp->zfs_hdl, &zc, &zplprops) != 0 ||
		    nvlist_lookup_uint64(zplprops, zfs_prop_to_name(prop),
		    val) != 0) {
			zcmd_free_nvlists(&zc);
			return (-1);
		}
		nvlist_free(zplprops);
		zcmd_free_nvlists(&zc);
		break;

	case ZFS_PROP_INCONSISTENT:
		*val = zhp->zfs_dmustats.dds_inconsistent;
		break;

	default:
		switch (zfs_prop_get_type(prop)) {
		case PROP_TYPE_NUMBER:
		case PROP_TYPE_INDEX:
			*val = getprop_uint64(zhp, prop, source);
			/*
			 * If we tried to use a default value for a
			 * readonly property, it means that it was not
			 * present.  Note this only applies to "truly"
			 * readonly properties, not set-once properties
			 * like volblocksize.
			 */
			if (zfs_prop_readonly(prop) &&
			    !zfs_prop_setonce(prop) &&
			    *source != NULL && (*source)[0] == '\0') {
				*source = NULL;
				return (-1);
			}
			break;

		case PROP_TYPE_STRING:
		default:
			zfs_error_aux(zhp->zfs_hdl, dgettext(TEXT_DOMAIN,
			    "cannot get non-numeric property"));
			return (zfs_error(zhp->zfs_hdl, EZFS_BADPROP,
			    dgettext(TEXT_DOMAIN, "internal error")));
		}
	}

	return (0);
}

/*
 * Calculate the source type, given the raw source string.
 */
static void
get_source(zfs_handle_t *zhp, zprop_source_t *srctype, char *source,
    char *statbuf, size_t statlen)
{
	if (statbuf == NULL || *srctype == ZPROP_SRC_TEMPORARY)
		return;

	if (source == NULL) {
		*srctype = ZPROP_SRC_NONE;
	} else if (source[0] == '\0') {
		*srctype = ZPROP_SRC_DEFAULT;
	} else if (strstr(source, ZPROP_SOURCE_VAL_RECVD) != NULL) {
		*srctype = ZPROP_SRC_RECEIVED;
	} else {
		if (strcmp(source, zhp->zfs_name) == 0) {
			*srctype = ZPROP_SRC_LOCAL;
		} else {
			(void) strlcpy(statbuf, source, statlen);
			*srctype = ZPROP_SRC_INHERITED;
		}
	}

}

int
zfs_prop_get_recvd(zfs_handle_t *zhp, const char *propname, char *propbuf,
    size_t proplen, boolean_t literal)
{
	zfs_prop_t prop;
	int err = 0;

	if (zhp->zfs_recvd_props == NULL)
		if (get_recvd_props_ioctl(zhp) != 0)
			return (-1);

	prop = zfs_name_to_prop(propname);

	if (prop != ZPROP_INVAL) {
		uint64_t cookie;
		if (!nvlist_exists(zhp->zfs_recvd_props, propname))
			return (-1);
		zfs_set_recvd_props_mode(zhp, &cookie);
		err = zfs_prop_get(zhp, prop, propbuf, proplen,
		    NULL, NULL, 0, literal);
		zfs_unset_recvd_props_mode(zhp, &cookie);
	} else {
		nvlist_t *propval;
		char *recvdval;
		if (nvlist_lookup_nvlist(zhp->zfs_recvd_props,
		    propname, &propval) != 0)
			return (-1);
		verify(nvlist_lookup_string(propval, ZPROP_VALUE,
		    &recvdval) == 0);
		(void) strlcpy(propbuf, recvdval, proplen);
	}

	return (err == 0 ? 0 : -1);
}

static int
get_clones_string(zfs_handle_t *zhp, char *propbuf, size_t proplen)
{
	nvlist_t *value;
	nvpair_t *pair;

	value = zfs_get_clones_nvl(zhp);
	if (value == NULL)
		return (-1);

	propbuf[0] = '\0';
	for (pair = nvlist_next_nvpair(value, NULL); pair != NULL;
	    pair = nvlist_next_nvpair(value, pair)) {
		if (propbuf[0] != '\0')
			(void) strlcat(propbuf, ",", proplen);
		(void) strlcat(propbuf, nvpair_name(pair), proplen);
	}

	return (0);
}

struct get_clones_arg {
	uint64_t numclones;
	nvlist_t *value;
	const char *origin;
	char buf[ZFS_MAX_DATASET_NAME_LEN];
};

int
get_clones_cb(zfs_handle_t *zhp, void *arg)
{
	struct get_clones_arg *gca = arg;

	if (gca->numclones == 0) {
		zfs_close(zhp);
		return (0);
	}

	if (zfs_prop_get(zhp, ZFS_PROP_ORIGIN, gca->buf, sizeof (gca->buf),
	    NULL, NULL, 0, B_TRUE) != 0)
		goto out;
	if (strcmp(gca->buf, gca->origin) == 0) {
		fnvlist_add_boolean(gca->value, zfs_get_name(zhp));
		gca->numclones--;
	}

out:
	(void) zfs_iter_children(zhp, get_clones_cb, gca);
	zfs_close(zhp);
	return (0);
}

nvlist_t *
zfs_get_clones_nvl(zfs_handle_t *zhp)
{
	nvlist_t *nv, *value;

	if (nvlist_lookup_nvlist(zhp->zfs_props,
	    zfs_prop_to_name(ZFS_PROP_CLONES), &nv) != 0) {
		struct get_clones_arg gca;

		/*
		 * if this is a snapshot, then the kernel wasn't able
		 * to get the clones.  Do it by slowly iterating.
		 */
		if (zhp->zfs_type != ZFS_TYPE_SNAPSHOT)
			return (NULL);
		if (nvlist_alloc(&nv, NV_UNIQUE_NAME, 0) != 0)
			return (NULL);
		if (nvlist_alloc(&value, NV_UNIQUE_NAME, 0) != 0) {
			nvlist_free(nv);
			return (NULL);
		}

		gca.numclones = zfs_prop_get_int(zhp, ZFS_PROP_NUMCLONES);
		gca.value = value;
		gca.origin = zhp->zfs_name;

		if (gca.numclones != 0) {
			zfs_handle_t *root;
			char pool[ZFS_MAX_DATASET_NAME_LEN];
			char *cp = pool;

			/* get the pool name */
			(void) strlcpy(pool, zhp->zfs_name, sizeof (pool));
			(void) strsep(&cp, "/@");
			root = zfs_open(zhp->zfs_hdl, pool,
			    ZFS_TYPE_FILESYSTEM);

			(void) get_clones_cb(root, &gca);
		}

		if (gca.numclones != 0 ||
		    nvlist_add_nvlist(nv, ZPROP_VALUE, value) != 0 ||
		    nvlist_add_nvlist(zhp->zfs_props,
		    zfs_prop_to_name(ZFS_PROP_CLONES), nv) != 0) {
			nvlist_free(nv);
			nvlist_free(value);
			return (NULL);
		}
		nvlist_free(nv);
		nvlist_free(value);
		verify(0 == nvlist_lookup_nvlist(zhp->zfs_props,
		    zfs_prop_to_name(ZFS_PROP_CLONES), &nv));
	}

	verify(nvlist_lookup_nvlist(nv, ZPROP_VALUE, &value) == 0);

	return (value);
}

/*
 * Accepts a property and value and checks that the value
 * matches the one found by the channel program. If they are
 * not equal, print both of them.
 */
void
zcp_check(zfs_handle_t *zhp, zfs_prop_t prop, uint64_t intval,
    const char *strval)
{
	if (!zhp->zfs_hdl->libzfs_prop_debug)
		return;
	int error;
	char *poolname = zhp->zpool_hdl->zpool_name;
	const char *program =
	    "args = ...\n"
	    "ds = args['dataset']\n"
	    "prop = args['property']\n"
	    "value, setpoint = zfs.get_prop(ds, prop)\n"
	    "return {value=value, setpoint=setpoint}\n";
	nvlist_t *outnvl;
	nvlist_t *retnvl;
	nvlist_t *argnvl = fnvlist_alloc();

	fnvlist_add_string(argnvl, "dataset", zhp->zfs_name);
	fnvlist_add_string(argnvl, "property", zfs_prop_to_name(prop));

	error = lzc_channel_program_nosync(poolname, program,
	    10 * 1000 * 1000, 10 * 1024 * 1024, argnvl, &outnvl);

	if (error == 0) {
		retnvl = fnvlist_lookup_nvlist(outnvl, "return");
		if (zfs_prop_get_type(prop) == PROP_TYPE_NUMBER) {
			int64_t ans;
			error = nvlist_lookup_int64(retnvl, "value", &ans);
			if (error != 0) {
				(void) fprintf(stderr, "zcp check error: %u\n",
				    error);
				return;
			}
			if (ans != intval) {
				(void) fprintf(stderr,
				    "%s: zfs found %lld, but zcp found %lld\n",
				    zfs_prop_to_name(prop),
				    (longlong_t)intval, (longlong_t)ans);
			}
		} else {
			char *str_ans;
			error = nvlist_lookup_string(retnvl, "value", &str_ans);
			if (error != 0) {
				(void) fprintf(stderr, "zcp check error: %u\n",
				    error);
				return;
			}
			if (strcmp(strval, str_ans) != 0) {
				(void) fprintf(stderr,
				    "%s: zfs found %s, but zcp found %s\n",
				    zfs_prop_to_name(prop),
				    strval, str_ans);
			}
		}
	} else {
		(void) fprintf(stderr,
		    "zcp check failed, channel program error: %u\n", error);
	}
	nvlist_free(argnvl);
	nvlist_free(outnvl);
}

/*
 * Retrieve a property from the given object.  If 'literal' is specified, then
 * numbers are left as exact values.  Otherwise, numbers are converted to a
 * human-readable form.
 *
 * Returns 0 on success, or -1 on error.
 */
int
zfs_prop_get(zfs_handle_t *zhp, zfs_prop_t prop, char *propbuf, size_t proplen,
    zprop_source_t *src, char *statbuf, size_t statlen, boolean_t literal)
{
	char *source = NULL;
	uint64_t val;
	const char *str;
	const char *strval;
	boolean_t received = zfs_is_recvd_props_mode(zhp);

	/*
	 * Check to see if this property applies to our object
	 */
	if (!zfs_prop_valid_for_type(prop, zhp->zfs_type))
		return (-1);

	if (received && zfs_prop_readonly(prop))
		return (-1);

	if (src)
		*src = ZPROP_SRC_NONE;

	switch (prop) {
	case ZFS_PROP_CREATION:
		/*
		 * 'creation' is a time_t stored in the statistics.  We convert
		 * this into a string unless 'literal' is specified.
		 */
		{
			val = getprop_uint64(zhp, prop, &source);
			time_t time = (time_t)val;
			struct tm t;

			if (literal ||
			    localtime_r(&time, &t) == NULL ||
			    strftime(propbuf, proplen, "%a %b %e %k:%M %Y",
			    &t) == 0)
				(void) snprintf(propbuf, proplen, "%llu", val);
		}
		zcp_check(zhp, prop, val, NULL);
		break;

	case ZFS_PROP_MOUNTPOINT:
		/*
		 * Getting the precise mountpoint can be tricky.
		 *
		 *  - for 'none' or 'legacy', return those values.
		 *  - for inherited mountpoints, we want to take everything
		 *    after our ancestor and append it to the inherited value.
		 *
		 * If the pool has an alternate root, we want to prepend that
		 * root to any values we return.
		 */

		str = getprop_string(zhp, prop, &source);

		if (str[0] == '/') {
			char buf[MAXPATHLEN];
			char *root = buf;
			const char *relpath;

			/*
			 * If we inherit the mountpoint, even from a dataset
			 * with a received value, the source will be the path of
			 * the dataset we inherit from. If source is
			 * ZPROP_SOURCE_VAL_RECVD, the received value is not
			 * inherited.
			 */
			if (strcmp(source, ZPROP_SOURCE_VAL_RECVD) == 0) {
				relpath = "";
			} else {
				relpath = zhp->zfs_name + strlen(source);
				if (relpath[0] == '/')
					relpath++;
			}

			if ((zpool_get_prop(zhp->zpool_hdl,
			    ZPOOL_PROP_ALTROOT, buf, MAXPATHLEN, NULL,
			    B_FALSE)) || (strcmp(root, "-") == 0))
				root[0] = '\0';
			/*
			 * Special case an alternate root of '/'. This will
			 * avoid having multiple leading slashes in the
			 * mountpoint path.
			 */
			if (strcmp(root, "/") == 0)
				root++;

			/*
			 * If the mountpoint is '/' then skip over this
			 * if we are obtaining either an alternate root or
			 * an inherited mountpoint.
			 */
			if (str[1] == '\0' && (root[0] != '\0' ||
			    relpath[0] != '\0'))
				str++;

			if (relpath[0] == '\0')
				(void) snprintf(propbuf, proplen, "%s%s",
				    root, str);
			else
				(void) snprintf(propbuf, proplen, "%s%s%s%s",
				    root, str, relpath[0] == '@' ? "" : "/",
				    relpath);
		} else {
			/* 'legacy' or 'none' */
			(void) strlcpy(propbuf, str, proplen);
		}
		zcp_check(zhp, prop, NULL, propbuf);
		break;

	case ZFS_PROP_ORIGIN:
		str = getprop_string(zhp, prop, &source);
		if (str == NULL)
			return (-1);
		(void) strlcpy(propbuf, str, proplen);
		zcp_check(zhp, prop, NULL, str);
		break;

	case ZFS_PROP_CLONES:
		if (get_clones_string(zhp, propbuf, proplen) != 0)
			return (-1);
		break;

	case ZFS_PROP_QUOTA:
	case ZFS_PROP_REFQUOTA:
	case ZFS_PROP_RESERVATION:
	case ZFS_PROP_REFRESERVATION:

		if (get_numeric_property(zhp, prop, src, &source, &val) != 0)
			return (-1);
		/*
		 * If quota or reservation is 0, we translate this into 'none'
		 * (unless literal is set), and indicate that it's the default
		 * value.  Otherwise, we print the number nicely and indicate
		 * that its set locally.
		 */
		if (val == 0) {
			if (literal)
				(void) strlcpy(propbuf, "0", proplen);
			else
				(void) strlcpy(propbuf, "none", proplen);
		} else {
			if (literal)
				(void) snprintf(propbuf, proplen, "%llu",
				    (u_longlong_t)val);
			else
				zfs_nicenum(val, propbuf, proplen);
		}
		zcp_check(zhp, prop, val, NULL);
		break;

	case ZFS_PROP_FILESYSTEM_LIMIT:
	case ZFS_PROP_SNAPSHOT_LIMIT:
	case ZFS_PROP_FILESYSTEM_COUNT:
	case ZFS_PROP_SNAPSHOT_COUNT:

		if (get_numeric_property(zhp, prop, src, &source, &val) != 0)
			return (-1);

		/*
		 * If limit is UINT64_MAX, we translate this into 'none' (unless
		 * literal is set), and indicate that it's the default value.
		 * Otherwise, we print the number nicely and indicate that it's
		 * set locally.
		 */
		if (literal) {
			(void) snprintf(propbuf, proplen, "%llu",
			    (u_longlong_t)val);
		} else if (val == UINT64_MAX) {
			(void) strlcpy(propbuf, "none", proplen);
		} else {
			zfs_nicenum(val, propbuf, proplen);
		}

		zcp_check(zhp, prop, val, NULL);
		break;

	case ZFS_PROP_REFRATIO:
	case ZFS_PROP_COMPRESSRATIO:
		if (get_numeric_property(zhp, prop, src, &source, &val) != 0)
			return (-1);
		(void) snprintf(propbuf, proplen, "%llu.%02llux",
		    (u_longlong_t)(val / 100),
		    (u_longlong_t)(val % 100));
		zcp_check(zhp, prop, val, NULL);
		break;

	case ZFS_PROP_TYPE:
		switch (zhp->zfs_type) {
		case ZFS_TYPE_FILESYSTEM:
			str = "filesystem";
			break;
		case ZFS_TYPE_VOLUME:
			str = "volume";
			break;
		case ZFS_TYPE_SNAPSHOT:
			str = "snapshot";
			break;
		case ZFS_TYPE_BOOKMARK:
			str = "bookmark";
			break;
		default:
			abort();
		}
		(void) snprintf(propbuf, proplen, "%s", str);
		zcp_check(zhp, prop, NULL, propbuf);
		break;

	case ZFS_PROP_MOUNTED:
		/*
		 * The 'mounted' property is a pseudo-property that described
		 * whether the filesystem is currently mounted.  Even though
		 * it's a boolean value, the typical values of "on" and "off"
		 * don't make sense, so we translate to "yes" and "no".
		 */
		if (get_numeric_property(zhp, ZFS_PROP_MOUNTED,
		    src, &source, &val) != 0)
			return (-1);
		if (val)
			(void) strlcpy(propbuf, "yes", proplen);
		else
			(void) strlcpy(propbuf, "no", proplen);
		break;

	case ZFS_PROP_NAME:
		/*
		 * The 'name' property is a pseudo-property derived from the
		 * dataset name.  It is presented as a real property to simplify
		 * consumers.
		 */
		(void) strlcpy(propbuf, zhp->zfs_name, proplen);
		zcp_check(zhp, prop, NULL, propbuf);
		break;

	case ZFS_PROP_MLSLABEL:
		{
#ifdef illumos
			m_label_t *new_sl = NULL;
			char *ascii = NULL;	/* human readable label */

			(void) strlcpy(propbuf,
			    getprop_string(zhp, prop, &source), proplen);

			if (literal || (strcasecmp(propbuf,
			    ZFS_MLSLABEL_DEFAULT) == 0))
				break;

			/*
			 * Try to translate the internal hex string to
			 * human-readable output.  If there are any
			 * problems just use the hex string.
			 */

			if (str_to_label(propbuf, &new_sl, MAC_LABEL,
			    L_NO_CORRECTION, NULL) == -1) {
				m_label_free(new_sl);
				break;
			}

			if (label_to_str(new_sl, &ascii, M_LABEL,
			    DEF_NAMES) != 0) {
				if (ascii)
					free(ascii);
				m_label_free(new_sl);
				break;
			}
			m_label_free(new_sl);

			(void) strlcpy(propbuf, ascii, proplen);
			free(ascii);
#else	/* !illumos */
			propbuf[0] = '\0';
#endif	/* illumos */
		}
		break;

	case ZFS_PROP_GUID:
	case ZFS_PROP_CREATETXG:
		/*
		 * GUIDs are stored as numbers, but they are identifiers.
		 * We don't want them to be pretty printed, because pretty
		 * printing mangles the ID into a truncated and useless value.
		 */
		if (get_numeric_property(zhp, prop, src, &source, &val) != 0)
			return (-1);
		(void) snprintf(propbuf, proplen, "%llu", (u_longlong_t)val);
		zcp_check(zhp, prop, val, NULL);
		break;

	default:
		switch (zfs_prop_get_type(prop)) {
		case PROP_TYPE_NUMBER:
			if (get_numeric_property(zhp, prop, src,
			    &source, &val) != 0) {
				return (-1);
			}

			if (literal) {
				(void) snprintf(propbuf, proplen, "%llu",
				    (u_longlong_t)val);
			} else {
				zfs_nicenum(val, propbuf, proplen);
			}
			zcp_check(zhp, prop, val, NULL);
			break;

		case PROP_TYPE_STRING:
			str = getprop_string(zhp, prop, &source);
			if (str == NULL)
				return (-1);

			(void) strlcpy(propbuf, str, proplen);
			zcp_check(zhp, prop, NULL, str);
			break;

		case PROP_TYPE_INDEX:
			if (get_numeric_property(zhp, prop, src,
			    &source, &val) != 0)
				return (-1);
			if (zfs_prop_index_to_string(prop, val, &strval) != 0)
				return (-1);

			(void) strlcpy(propbuf, strval, proplen);
			zcp_check(zhp, prop, NULL, strval);
			break;

		default:
			abort();
		}
	}

	get_source(zhp, src, source, statbuf, statlen);

	return (0);
}

/*
 * Utility function to get the given numeric property.  Does no validation that
 * the given property is the appropriate type; should only be used with
 * hard-coded property types.
 */
uint64_t
zfs_prop_get_int(zfs_handle_t *zhp, zfs_prop_t prop)
{
	char *source;
	uint64_t val;

	(void) get_numeric_property(zhp, prop, NULL, &source, &val);

	return (val);
}

int
zfs_prop_set_int(zfs_handle_t *zhp, zfs_prop_t prop, uint64_t val)
{
	char buf[64];

	(void) snprintf(buf, sizeof (buf), "%llu", (longlong_t)val);
	return (zfs_prop_set(zhp, zfs_prop_to_name(prop), buf));
}

/*
 * Similar to zfs_prop_get(), but returns the value as an integer.
 */
int
zfs_prop_get_numeric(zfs_handle_t *zhp, zfs_prop_t prop, uint64_t *value,
    zprop_source_t *src, char *statbuf, size_t statlen)
{
	char *source;

	/*
	 * Check to see if this property applies to our object
	 */
	if (!zfs_prop_valid_for_type(prop, zhp->zfs_type)) {
		return (zfs_error_fmt(zhp->zfs_hdl, EZFS_PROPTYPE,
		    dgettext(TEXT_DOMAIN, "cannot get property '%s'"),
		    zfs_prop_to_name(prop)));
	}

	if (src)
		*src = ZPROP_SRC_NONE;

	if (get_numeric_property(zhp, prop, src, &source, value) != 0)
		return (-1);

	get_source(zhp, src, source, statbuf, statlen);

	return (0);
}

static int
idmap_id_to_numeric_domain_rid(uid_t id, boolean_t isuser,
    char **domainp, idmap_rid_t *ridp)
{
#ifdef illumos
	idmap_get_handle_t *get_hdl = NULL;
	idmap_stat status;
	int err = EINVAL;

	if (idmap_get_create(&get_hdl) != IDMAP_SUCCESS)
		goto out;

	if (isuser) {
		err = idmap_get_sidbyuid(get_hdl, id,
		    IDMAP_REQ_FLG_USE_CACHE, domainp, ridp, &status);
	} else {
		err = idmap_get_sidbygid(get_hdl, id,
		    IDMAP_REQ_FLG_USE_CACHE, domainp, ridp, &status);
	}
	if (err == IDMAP_SUCCESS &&
	    idmap_get_mappings(get_hdl) == IDMAP_SUCCESS &&
	    status == IDMAP_SUCCESS)
		err = 0;
	else
		err = EINVAL;
out:
	if (get_hdl)
		idmap_get_destroy(get_hdl);
	return (err);
#else	/* !illumos */
	assert(!"invalid code path");
	return (EINVAL); // silence compiler warning
#endif	/* illumos */
}

/*
 * convert the propname into parameters needed by kernel
 * Eg: userquota@ahrens -> ZFS_PROP_USERQUOTA, "", 126829
 * Eg: userused@matt@domain -> ZFS_PROP_USERUSED, "S-1-123-456", 789
 */
static int
userquota_propname_decode(const char *propname, boolean_t zoned,
    zfs_userquota_prop_t *typep, char *domain, int domainlen, uint64_t *ridp)
{
	zfs_userquota_prop_t type;
	char *cp, *end;
	char *numericsid = NULL;
	boolean_t isuser;

	domain[0] = '\0';
	*ridp = 0;
	/* Figure out the property type ({user|group}{quota|space}) */
	for (type = 0; type < ZFS_NUM_USERQUOTA_PROPS; type++) {
		if (strncmp(propname, zfs_userquota_prop_prefixes[type],
		    strlen(zfs_userquota_prop_prefixes[type])) == 0)
			break;
	}
	if (type == ZFS_NUM_USERQUOTA_PROPS)
		return (EINVAL);
	*typep = type;

	isuser = (type == ZFS_PROP_USERQUOTA ||
	    type == ZFS_PROP_USERUSED);

	cp = strchr(propname, '@') + 1;

	if (strchr(cp, '@')) {
#ifdef illumos
		/*
		 * It's a SID name (eg "user@domain") that needs to be
		 * turned into S-1-domainID-RID.
		 */
		int flag = 0;
		idmap_stat stat, map_stat;
		uid_t pid;
		idmap_rid_t rid;
		idmap_get_handle_t *gh = NULL;

		stat = idmap_get_create(&gh);
		if (stat != IDMAP_SUCCESS) {
			idmap_get_destroy(gh);
			return (ENOMEM);
		}
		if (zoned && getzoneid() == GLOBAL_ZONEID)
			return (ENOENT);
		if (isuser) {
			stat = idmap_getuidbywinname(cp, NULL, flag, &pid);
			if (stat < 0)
				return (ENOENT);
			stat = idmap_get_sidbyuid(gh, pid, flag, &numericsid,
			    &rid, &map_stat);
		} else {
			stat = idmap_getgidbywinname(cp, NULL, flag, &pid);
			if (stat < 0)
				return (ENOENT);
			stat = idmap_get_sidbygid(gh, pid, flag, &numericsid,
			    &rid, &map_stat);
		}
		if (stat < 0) {
			idmap_get_destroy(gh);
			return (ENOENT);
		}
		stat = idmap_get_mappings(gh);
		idmap_get_destroy(gh);

		if (stat < 0) {
			return (ENOENT);
		}
		if (numericsid == NULL)
			return (ENOENT);
		cp = numericsid;
		*ridp = rid;
		/* will be further decoded below */
#else	/* !illumos */
		return (ENOENT);
#endif	/* illumos */
	}

	if (strncmp(cp, "S-1-", 4) == 0) {
		/* It's a numeric SID (eg "S-1-234-567-89") */
		(void) strlcpy(domain, cp, domainlen);
		errno = 0;
		if (*ridp == 0) {
			cp = strrchr(domain, '-');
			*cp = '\0';
			cp++;
			*ridp = strtoull(cp, &end, 10);
		} else {
			end = "";
		}
		if (numericsid) {
			free(numericsid);
			numericsid = NULL;
		}
		if (errno != 0 || *end != '\0')
			return (EINVAL);
	} else if (!isdigit(*cp)) {
		/*
		 * It's a user/group name (eg "user") that needs to be
		 * turned into a uid/gid
		 */
		if (zoned && getzoneid() == GLOBAL_ZONEID)
			return (ENOENT);
		if (isuser) {
			struct passwd *pw;
			pw = getpwnam(cp);
			if (pw == NULL)
				return (ENOENT);
			*ridp = pw->pw_uid;
		} else {
			struct group *gr;
			gr = getgrnam(cp);
			if (gr == NULL)
				return (ENOENT);
			*ridp = gr->gr_gid;
		}
	} else {
		/* It's a user/group ID (eg "12345"). */
		uid_t id = strtoul(cp, &end, 10);
		idmap_rid_t rid;
		char *mapdomain;

		if (*end != '\0')
			return (EINVAL);
		if (id > MAXUID) {
			/* It's an ephemeral ID. */
			if (idmap_id_to_numeric_domain_rid(id, isuser,
			    &mapdomain, &rid) != 0)
				return (ENOENT);
			(void) strlcpy(domain, mapdomain, domainlen);
			*ridp = rid;
		} else {
			*ridp = id;
		}
	}

	ASSERT3P(numericsid, ==, NULL);
	return (0);
}

static int
zfs_prop_get_userquota_common(zfs_handle_t *zhp, const char *propname,
    uint64_t *propvalue, zfs_userquota_prop_t *typep)
{
	int err;
	zfs_cmd_t zc = { 0 };

	(void) strlcpy(zc.zc_name, zhp->zfs_name, sizeof (zc.zc_name));

	err = userquota_propname_decode(propname,
	    zfs_prop_get_int(zhp, ZFS_PROP_ZONED),
	    typep, zc.zc_value, sizeof (zc.zc_value), &zc.zc_guid);
	zc.zc_objset_type = *typep;
	if (err)
		return (err);

	err = ioctl(zhp->zfs_hdl->libzfs_fd, ZFS_IOC_USERSPACE_ONE, &zc);
	if (err)
		return (err);

	*propvalue = zc.zc_cookie;
	return (0);
}

int
zfs_prop_get_userquota_int(zfs_handle_t *zhp, const char *propname,
    uint64_t *propvalue)
{
	zfs_userquota_prop_t type;

	return (zfs_prop_get_userquota_common(zhp, propname, propvalue,
	    &type));
}

int
zfs_prop_get_userquota(zfs_handle_t *zhp, const char *propname,
    char *propbuf, int proplen, boolean_t literal)
{
	int err;
	uint64_t propvalue;
	zfs_userquota_prop_t type;

	err = zfs_prop_get_userquota_common(zhp, propname, &propvalue,
	    &type);

	if (err)
		return (err);

	if (literal) {
		(void) snprintf(propbuf, proplen, "%llu", propvalue);
	} else if (propvalue == 0 &&
	    (type == ZFS_PROP_USERQUOTA || type == ZFS_PROP_GROUPQUOTA)) {
		(void) strlcpy(propbuf, "none", proplen);
	} else {
		zfs_nicenum(propvalue, propbuf, proplen);
	}
	return (0);
}

int
zfs_prop_get_written_int(zfs_handle_t *zhp, const char *propname,
    uint64_t *propvalue)
{
	int err;
	zfs_cmd_t zc = { 0 };
	const char *snapname;

	(void) strlcpy(zc.zc_name, zhp->zfs_name, sizeof (zc.zc_name));

	snapname = strchr(propname, '@') + 1;
	if (strchr(snapname, '@')) {
		(void) strlcpy(zc.zc_value, snapname, sizeof (zc.zc_value));
	} else {
		/* snapname is the short name, append it to zhp's fsname */
		char *cp;

		(void) strlcpy(zc.zc_value, zhp->zfs_name,
		    sizeof (zc.zc_value));
		cp = strchr(zc.zc_value, '@');
		if (cp != NULL)
			*cp = '\0';
		(void) strlcat(zc.zc_value, "@", sizeof (zc.zc_value));
		(void) strlcat(zc.zc_value, snapname, sizeof (zc.zc_value));
	}

	err = ioctl(zhp->zfs_hdl->libzfs_fd, ZFS_IOC_SPACE_WRITTEN, &zc);
	if (err)
		return (err);

	*propvalue = zc.zc_cookie;
	return (0);
}

int
zfs_prop_get_written(zfs_handle_t *zhp, const char *propname,
    char *propbuf, int proplen, boolean_t literal)
{
	int err;
	uint64_t propvalue;

	err = zfs_prop_get_written_int(zhp, propname, &propvalue);

	if (err)
		return (err);

	if (literal) {
		(void) snprintf(propbuf, proplen, "%llu", propvalue);
	} else {
		zfs_nicenum(propvalue, propbuf, proplen);
	}
	return (0);
}

/*
 * Returns the name of the given zfs handle.
 */
const char *
zfs_get_name(const zfs_handle_t *zhp)
{
	return (zhp->zfs_name);
}

/*
 * Returns the name of the parent pool for the given zfs handle.
 */
const char *
zfs_get_pool_name(const zfs_handle_t *zhp)
{
	return (zhp->zpool_hdl->zpool_name);
}

/*
 * Returns the type of the given zfs handle.
 */
zfs_type_t
zfs_get_type(const zfs_handle_t *zhp)
{
	return (zhp->zfs_type);
}

/*
 * Is one dataset name a child dataset of another?
 *
 * Needs to handle these cases:
 * Dataset 1	"a/foo"		"a/foo"		"a/foo"		"a/foo"
 * Dataset 2	"a/fo"		"a/foobar"	"a/bar/baz"	"a/foo/bar"
 * Descendant?	No.		No.		No.		Yes.
 */
static boolean_t
is_descendant(const char *ds1, const char *ds2)
{
	size_t d1len = strlen(ds1);

	/* ds2 can't be a descendant if it's smaller */
	if (strlen(ds2) < d1len)
		return (B_FALSE);

	/* otherwise, compare strings and verify that there's a '/' char */
	return (ds2[d1len] == '/' && (strncmp(ds1, ds2, d1len) == 0));
}

/*
 * Given a complete name, return just the portion that refers to the parent.
 * Will return -1 if there is no parent (path is just the name of the
 * pool).
 */
static int
parent_name(const char *path, char *buf, size_t buflen)
{
	char *slashp;

	(void) strlcpy(buf, path, buflen);

	if ((slashp = strrchr(buf, '/')) == NULL)
		return (-1);
	*slashp = '\0';

	return (0);
}

/*
 * If accept_ancestor is false, then check to make sure that the given path has
 * a parent, and that it exists.  If accept_ancestor is true, then find the
 * closest existing ancestor for the given path.  In prefixlen return the
 * length of already existing prefix of the given path.  We also fetch the
 * 'zoned' property, which is used to validate property settings when creating
 * new datasets.
 */
static int
check_parents(libzfs_handle_t *hdl, const char *path, uint64_t *zoned,
    boolean_t accept_ancestor, int *prefixlen)
{
	zfs_cmd_t zc = { 0 };
	char parent[ZFS_MAX_DATASET_NAME_LEN];
	char *slash;
	zfs_handle_t *zhp;
	char errbuf[1024];
	uint64_t is_zoned;

	(void) snprintf(errbuf, sizeof (errbuf),
	    dgettext(TEXT_DOMAIN, "cannot create '%s'"), path);

	/* get parent, and check to see if this is just a pool */
	if (parent_name(path, parent, sizeof (parent)) != 0) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "missing dataset name"));
		return (zfs_error(hdl, EZFS_INVALIDNAME, errbuf));
	}

	/* check to see if the pool exists */
	if ((slash = strchr(parent, '/')) == NULL)
		slash = parent + strlen(parent);
	(void) strncpy(zc.zc_name, parent, slash - parent);
	zc.zc_name[slash - parent] = '\0';
	if (ioctl(hdl->libzfs_fd, ZFS_IOC_OBJSET_STATS, &zc) != 0 &&
	    errno == ENOENT) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "no such pool '%s'"), zc.zc_name);
		return (zfs_error(hdl, EZFS_NOENT, errbuf));
	}

	/* check to see if the parent dataset exists */
	while ((zhp = make_dataset_handle(hdl, parent)) == NULL) {
		if (errno == ENOENT && accept_ancestor) {
			/*
			 * Go deeper to find an ancestor, give up on top level.
			 */
			if (parent_name(parent, parent, sizeof (parent)) != 0) {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "no such pool '%s'"), zc.zc_name);
				return (zfs_error(hdl, EZFS_NOENT, errbuf));
			}
		} else if (errno == ENOENT) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "parent does not exist"));
			return (zfs_error(hdl, EZFS_NOENT, errbuf));
		} else
			return (zfs_standard_error(hdl, errno, errbuf));
	}

	is_zoned = zfs_prop_get_int(zhp, ZFS_PROP_ZONED);
	if (zoned != NULL)
		*zoned = is_zoned;

	/* we are in a non-global zone, but parent is in the global zone */
	if (getzoneid() != GLOBAL_ZONEID && !is_zoned) {
		(void) zfs_standard_error(hdl, EPERM, errbuf);
		zfs_close(zhp);
		return (-1);
	}

	/* make sure parent is a filesystem */
	if (zfs_get_type(zhp) != ZFS_TYPE_FILESYSTEM) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "parent is not a filesystem"));
		(void) zfs_error(hdl, EZFS_BADTYPE, errbuf);
		zfs_close(zhp);
		return (-1);
	}

	zfs_close(zhp);
	if (prefixlen != NULL)
		*prefixlen = strlen(parent);
	return (0);
}

/*
 * Finds whether the dataset of the given type(s) exists.
 */
boolean_t
zfs_dataset_exists(libzfs_handle_t *hdl, const char *path, zfs_type_t types)
{
	zfs_handle_t *zhp;

	if (!zfs_validate_name(hdl, path, types, B_FALSE))
		return (B_FALSE);

	/*
	 * Try to get stats for the dataset, which will tell us if it exists.
	 */
	if ((zhp = make_dataset_handle(hdl, path)) != NULL) {
		int ds_type = zhp->zfs_type;

		zfs_close(zhp);
		if (types & ds_type)
			return (B_TRUE);
	}
	return (B_FALSE);
}

/*
 * Given a path to 'target', create all the ancestors between
 * the prefixlen portion of the path, and the target itself.
 * Fail if the initial prefixlen-ancestor does not already exist.
 */
int
create_parents(libzfs_handle_t *hdl, char *target, int prefixlen)
{
	zfs_handle_t *h;
	char *cp;
	const char *opname;

	/* make sure prefix exists */
	cp = target + prefixlen;
	if (*cp != '/') {
		assert(strchr(cp, '/') == NULL);
		h = zfs_open(hdl, target, ZFS_TYPE_FILESYSTEM);
	} else {
		*cp = '\0';
		h = zfs_open(hdl, target, ZFS_TYPE_FILESYSTEM);
		*cp = '/';
	}
	if (h == NULL)
		return (-1);
	zfs_close(h);

	/*
	 * Attempt to create, mount, and share any ancestor filesystems,
	 * up to the prefixlen-long one.
	 */
	for (cp = target + prefixlen + 1;
	    (cp = strchr(cp, '/')) != NULL; *cp = '/', cp++) {

		*cp = '\0';

		h = make_dataset_handle(hdl, target);
		if (h) {
			/* it already exists, nothing to do here */
			zfs_close(h);
			continue;
		}

		if (zfs_create(hdl, target, ZFS_TYPE_FILESYSTEM,
		    NULL) != 0) {
			opname = dgettext(TEXT_DOMAIN, "create");
			goto ancestorerr;
		}

		h = zfs_open(hdl, target, ZFS_TYPE_FILESYSTEM);
		if (h == NULL) {
			opname = dgettext(TEXT_DOMAIN, "open");
			goto ancestorerr;
		}

		if (zfs_mount(h, NULL, 0) != 0) {
			opname = dgettext(TEXT_DOMAIN, "mount");
			goto ancestorerr;
		}

		if (zfs_share(h) != 0) {
			opname = dgettext(TEXT_DOMAIN, "share");
			goto ancestorerr;
		}

		zfs_close(h);
	}

	return (0);

ancestorerr:
	zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
	    "failed to %s ancestor '%s'"), opname, target);
	return (-1);
}

/*
 * Creates non-existing ancestors of the given path.
 */
int
zfs_create_ancestors(libzfs_handle_t *hdl, const char *path)
{
	int prefix;
	char *path_copy;
	char errbuf[1024];
	int rc = 0;

	(void) snprintf(errbuf, sizeof (errbuf), dgettext(TEXT_DOMAIN,
	    "cannot create '%s'"), path);

	/*
	 * Check that we are not passing the nesting limit
	 * before we start creating any ancestors.
	 */
	if (dataset_nestcheck(path) != 0) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "maximum name nesting depth exceeded"));
		return (zfs_error(hdl, EZFS_INVALIDNAME, errbuf));
	}

	if (check_parents(hdl, path, NULL, B_TRUE, &prefix) != 0)
		return (-1);

	if ((path_copy = strdup(path)) != NULL) {
		rc = create_parents(hdl, path_copy, prefix);
		free(path_copy);
	}
	if (path_copy == NULL || rc != 0)
		return (-1);

	return (0);
}

/*
 * Create a new filesystem or volume.
 */
int
zfs_create(libzfs_handle_t *hdl, const char *path, zfs_type_t type,
    nvlist_t *props)
{
	int ret;
	uint64_t size = 0;
	uint64_t blocksize = zfs_prop_default_numeric(ZFS_PROP_VOLBLOCKSIZE);
	char errbuf[1024];
	uint64_t zoned;
	enum lzc_dataset_type ost;
	zpool_handle_t *zpool_handle;

	(void) snprintf(errbuf, sizeof (errbuf), dgettext(TEXT_DOMAIN,
	    "cannot create '%s'"), path);

	/* validate the path, taking care to note the extended error message */
	if (!zfs_validate_name(hdl, path, type, B_TRUE))
		return (zfs_error(hdl, EZFS_INVALIDNAME, errbuf));

	if (dataset_nestcheck(path) != 0) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "maximum name nesting depth exceeded"));
		return (zfs_error(hdl, EZFS_INVALIDNAME, errbuf));
	}

	/* validate parents exist */
	if (check_parents(hdl, path, &zoned, B_FALSE, NULL) != 0)
		return (-1);

	/*
	 * The failure modes when creating a dataset of a different type over
	 * one that already exists is a little strange.  In particular, if you
	 * try to create a dataset on top of an existing dataset, the ioctl()
	 * will return ENOENT, not EEXIST.  To prevent this from happening, we
	 * first try to see if the dataset exists.
	 */
	if (zfs_dataset_exists(hdl, path, ZFS_TYPE_DATASET)) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "dataset already exists"));
		return (zfs_error(hdl, EZFS_EXISTS, errbuf));
	}

	if (type == ZFS_TYPE_VOLUME)
		ost = LZC_DATSET_TYPE_ZVOL;
	else
		ost = LZC_DATSET_TYPE_ZFS;

	/* open zpool handle for prop validation */
	char pool_path[ZFS_MAX_DATASET_NAME_LEN];
	(void) strlcpy(pool_path, path, sizeof (pool_path));

	/* truncate pool_path at first slash */
	char *p = strchr(pool_path, '/');
	if (p != NULL)
		*p = '\0';

	if ((zpool_handle = zpool_open(hdl, pool_path)) == NULL)
		return (-1);

	if (props && (props = zfs_valid_proplist(hdl, type, props,
	    zoned, NULL, zpool_handle, errbuf)) == 0) {
		zpool_close(zpool_handle);
		return (-1);
	}
	zpool_close(zpool_handle);

	if (type == ZFS_TYPE_VOLUME) {
		/*
		 * If we are creating a volume, the size and block size must
		 * satisfy a few restraints.  First, the blocksize must be a
		 * valid block size between SPA_{MIN,MAX}BLOCKSIZE.  Second, the
		 * volsize must be a multiple of the block size, and cannot be
		 * zero.
		 */
		if (props == NULL || nvlist_lookup_uint64(props,
		    zfs_prop_to_name(ZFS_PROP_VOLSIZE), &size) != 0) {
			nvlist_free(props);
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "missing volume size"));
			return (zfs_error(hdl, EZFS_BADPROP, errbuf));
		}

		if ((ret = nvlist_lookup_uint64(props,
		    zfs_prop_to_name(ZFS_PROP_VOLBLOCKSIZE),
		    &blocksize)) != 0) {
			if (ret == ENOENT) {
				blocksize = zfs_prop_default_numeric(
				    ZFS_PROP_VOLBLOCKSIZE);
			} else {
				nvlist_free(props);
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "missing volume block size"));
				return (zfs_error(hdl, EZFS_BADPROP, errbuf));
			}
		}

		if (size == 0) {
			nvlist_free(props);
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "volume size cannot be zero"));
			return (zfs_error(hdl, EZFS_BADPROP, errbuf));
		}

		if (size % blocksize != 0) {
			nvlist_free(props);
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "volume size must be a multiple of volume block "
			    "size"));
			return (zfs_error(hdl, EZFS_BADPROP, errbuf));
		}
	}

	/* create the dataset */
	ret = lzc_create(path, ost, props);
	nvlist_free(props);

	/* check for failure */
	if (ret != 0) {
		char parent[ZFS_MAX_DATASET_NAME_LEN];
		(void) parent_name(path, parent, sizeof (parent));

		switch (errno) {
		case ENOENT:
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "no such parent '%s'"), parent);
			return (zfs_error(hdl, EZFS_NOENT, errbuf));

		case EINVAL:
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "parent '%s' is not a filesystem"), parent);
			return (zfs_error(hdl, EZFS_BADTYPE, errbuf));

		case ENOTSUP:
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "pool must be upgraded to set this "
			    "property or value"));
			return (zfs_error(hdl, EZFS_BADVERSION, errbuf));
		case ERANGE:
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "invalid property value(s) specified"));
			return (zfs_error(hdl, EZFS_BADPROP, errbuf));
#ifdef _ILP32
		case EOVERFLOW:
			/*
			 * This platform can't address a volume this big.
			 */
			if (type == ZFS_TYPE_VOLUME)
				return (zfs_error(hdl, EZFS_VOLTOOBIG,
				    errbuf));
#endif
			/* FALLTHROUGH */
		default:
			return (zfs_standard_error(hdl, errno, errbuf));
		}
	}

	return (0);
}

/*
 * Destroys the given dataset.  The caller must make sure that the filesystem
 * isn't mounted, and that there are no active dependents. If the file system
 * does not exist this function does nothing.
 */
int
zfs_destroy(zfs_handle_t *zhp, boolean_t defer)
{
	int error;

	if (zhp->zfs_type != ZFS_TYPE_SNAPSHOT && defer)
		return (EINVAL);

	if (zhp->zfs_type == ZFS_TYPE_BOOKMARK) {
		nvlist_t *nv = fnvlist_alloc();
		fnvlist_add_boolean(nv, zhp->zfs_name);
		error = lzc_destroy_bookmarks(nv, NULL);
		fnvlist_free(nv);
		if (error != 0) {
			return (zfs_standard_error_fmt(zhp->zfs_hdl, error,
			    dgettext(TEXT_DOMAIN, "cannot destroy '%s'"),
			    zhp->zfs_name));
		}
		return (0);
	}

	if (zhp->zfs_type == ZFS_TYPE_SNAPSHOT) {
		nvlist_t *nv = fnvlist_alloc();
		fnvlist_add_boolean(nv, zhp->zfs_name);
		error = lzc_destroy_snaps(nv, defer, NULL);
		fnvlist_free(nv);
	} else {
		error = lzc_destroy(zhp->zfs_name);
	}

	if (error != 0 && error != ENOENT) {
		return (zfs_standard_error_fmt(zhp->zfs_hdl, errno,
		    dgettext(TEXT_DOMAIN, "cannot destroy '%s'"),
		    zhp->zfs_name));
	}

	remove_mountpoint(zhp);

	return (0);
}

struct destroydata {
	nvlist_t *nvl;
	const char *snapname;
};

static int
zfs_check_snap_cb(zfs_handle_t *zhp, void *arg)
{
	struct destroydata *dd = arg;
	char name[ZFS_MAX_DATASET_NAME_LEN];
	int rv = 0;

	(void) snprintf(name, sizeof (name),
	    "%s@%s", zhp->zfs_name, dd->snapname);

	if (lzc_exists(name))
		verify(nvlist_add_boolean(dd->nvl, name) == 0);

	rv = zfs_iter_filesystems(zhp, zfs_check_snap_cb, dd);
	zfs_close(zhp);
	return (rv);
}

/*
 * Destroys all snapshots with the given name in zhp & descendants.
 */
int
zfs_destroy_snaps(zfs_handle_t *zhp, char *snapname, boolean_t defer)
{
	int ret;
	struct destroydata dd = { 0 };

	dd.snapname = snapname;
	verify(nvlist_alloc(&dd.nvl, NV_UNIQUE_NAME, 0) == 0);
	(void) zfs_check_snap_cb(zfs_handle_dup(zhp), &dd);

	if (nvlist_empty(dd.nvl)) {
		ret = zfs_standard_error_fmt(zhp->zfs_hdl, ENOENT,
		    dgettext(TEXT_DOMAIN, "cannot destroy '%s@%s'"),
		    zhp->zfs_name, snapname);
	} else {
		ret = zfs_destroy_snaps_nvl(zhp->zfs_hdl, dd.nvl, defer);
	}
	nvlist_free(dd.nvl);
	return (ret);
}

/*
 * Destroys all the snapshots named in the nvlist.
 */
int
zfs_destroy_snaps_nvl(libzfs_handle_t *hdl, nvlist_t *snaps, boolean_t defer)
{
	int ret;
	nvlist_t *errlist = NULL;

	ret = lzc_destroy_snaps(snaps, defer, &errlist);

	if (ret == 0) {
		nvlist_free(errlist);
		return (0);
	}

	if (nvlist_empty(errlist)) {
		char errbuf[1024];
		(void) snprintf(errbuf, sizeof (errbuf),
		    dgettext(TEXT_DOMAIN, "cannot destroy snapshots"));

		ret = zfs_standard_error(hdl, ret, errbuf);
	}
	for (nvpair_t *pair = nvlist_next_nvpair(errlist, NULL);
	    pair != NULL; pair = nvlist_next_nvpair(errlist, pair)) {
		char errbuf[1024];
		(void) snprintf(errbuf, sizeof (errbuf),
		    dgettext(TEXT_DOMAIN, "cannot destroy snapshot %s"),
		    nvpair_name(pair));

		switch (fnvpair_value_int32(pair)) {
		case EEXIST:
			zfs_error_aux(hdl,
			    dgettext(TEXT_DOMAIN, "snapshot is cloned"));
			ret = zfs_error(hdl, EZFS_EXISTS, errbuf);
			break;
		default:
			ret = zfs_standard_error(hdl, errno, errbuf);
			break;
		}
	}

	nvlist_free(errlist);
	return (ret);
}

/*
 * Clones the given dataset.  The target must be of the same type as the source.
 */
int
zfs_clone(zfs_handle_t *zhp, const char *target, nvlist_t *props)
{
	char parent[ZFS_MAX_DATASET_NAME_LEN];
	int ret;
	char errbuf[1024];
	libzfs_handle_t *hdl = zhp->zfs_hdl;
	uint64_t zoned;

	assert(zhp->zfs_type == ZFS_TYPE_SNAPSHOT);

	(void) snprintf(errbuf, sizeof (errbuf), dgettext(TEXT_DOMAIN,
	    "cannot create '%s'"), target);

	/* validate the target/clone name */
	if (!zfs_validate_name(hdl, target, ZFS_TYPE_FILESYSTEM, B_TRUE))
		return (zfs_error(hdl, EZFS_INVALIDNAME, errbuf));

	/* validate parents exist */
	if (check_parents(hdl, target, &zoned, B_FALSE, NULL) != 0)
		return (-1);

	(void) parent_name(target, parent, sizeof (parent));

	/* do the clone */

	if (props) {
		zfs_type_t type;

		if (ZFS_IS_VOLUME(zhp)) {
			type = ZFS_TYPE_VOLUME;
		} else {
			type = ZFS_TYPE_FILESYSTEM;
		}
		if ((props = zfs_valid_proplist(hdl, type, props, zoned,
		    zhp, zhp->zpool_hdl, errbuf)) == NULL)
			return (-1);
		if (zfs_fix_auto_resv(zhp, props) == -1) {
			nvlist_free(props);
			return (-1);
		}
	}

	ret = lzc_clone(target, zhp->zfs_name, props);
	nvlist_free(props);

	if (ret != 0) {
		switch (errno) {

		case ENOENT:
			/*
			 * The parent doesn't exist.  We should have caught this
			 * above, but there may a race condition that has since
			 * destroyed the parent.
			 *
			 * At this point, we don't know whether it's the source
			 * that doesn't exist anymore, or whether the target
			 * dataset doesn't exist.
			 */
			zfs_error_aux(zhp->zfs_hdl, dgettext(TEXT_DOMAIN,
			    "no such parent '%s'"), parent);
			return (zfs_error(zhp->zfs_hdl, EZFS_NOENT, errbuf));

		case EXDEV:
			zfs_error_aux(zhp->zfs_hdl, dgettext(TEXT_DOMAIN,
			    "source and target pools differ"));
			return (zfs_error(zhp->zfs_hdl, EZFS_CROSSTARGET,
			    errbuf));

		default:
			return (zfs_standard_error(zhp->zfs_hdl, errno,
			    errbuf));
		}
	}

	return (ret);
}

/*
 * Promotes the given clone fs to be the clone parent.
 */
int
zfs_promote(zfs_handle_t *zhp)
{
	libzfs_handle_t *hdl = zhp->zfs_hdl;
	char snapname[ZFS_MAX_DATASET_NAME_LEN];
	int ret;
	char errbuf[1024];

	(void) snprintf(errbuf, sizeof (errbuf), dgettext(TEXT_DOMAIN,
	    "cannot promote '%s'"), zhp->zfs_name);

	if (zhp->zfs_type == ZFS_TYPE_SNAPSHOT) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "snapshots can not be promoted"));
		return (zfs_error(hdl, EZFS_BADTYPE, errbuf));
	}

	if (zhp->zfs_dmustats.dds_origin[0] == '\0') {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "not a cloned filesystem"));
		return (zfs_error(hdl, EZFS_BADTYPE, errbuf));
	}

	if (!zfs_validate_name(hdl, zhp->zfs_name, zhp->zfs_type, B_TRUE))
		return (zfs_error(hdl, EZFS_INVALIDNAME, errbuf));

	ret = lzc_promote(zhp->zfs_name, snapname, sizeof (snapname));

	if (ret != 0) {
		switch (ret) {
		case EEXIST:
			/* There is a conflicting snapshot name. */
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "conflicting snapshot '%s' from parent '%s'"),
			    snapname, zhp->zfs_dmustats.dds_origin);
			return (zfs_error(hdl, EZFS_EXISTS, errbuf));

		default:
			return (zfs_standard_error(hdl, ret, errbuf));
		}
	}
	return (ret);
}

typedef struct snapdata {
	nvlist_t *sd_nvl;
	const char *sd_snapname;
} snapdata_t;

static int
zfs_snapshot_cb(zfs_handle_t *zhp, void *arg)
{
	snapdata_t *sd = arg;
	char name[ZFS_MAX_DATASET_NAME_LEN];
	int rv = 0;

	if (zfs_prop_get_int(zhp, ZFS_PROP_INCONSISTENT) == 0) {
		(void) snprintf(name, sizeof (name),
		    "%s@%s", zfs_get_name(zhp), sd->sd_snapname);

		fnvlist_add_boolean(sd->sd_nvl, name);

		rv = zfs_iter_filesystems(zhp, zfs_snapshot_cb, sd);
	}
	zfs_close(zhp);

	return (rv);
}

int
zfs_remap_indirects(libzfs_handle_t *hdl, const char *fs)
{
	int err;
	char errbuf[1024];

	(void) snprintf(errbuf, sizeof (errbuf), dgettext(TEXT_DOMAIN,
	    "cannot remap dataset '%s'"), fs);

	err = lzc_remap(fs);

	if (err != 0) {
		switch (err) {
		case ENOTSUP:
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "pool must be upgraded"));
			(void) zfs_error(hdl, EZFS_BADVERSION, errbuf);
			break;
		case EINVAL:
			(void) zfs_error(hdl, EZFS_BADTYPE, errbuf);
			break;
		default:
			(void) zfs_standard_error(hdl, err, errbuf);
			break;
		}
	}

	return (err);
}

/*
 * Creates snapshots.  The keys in the snaps nvlist are the snapshots to be
 * created.
 */
int
zfs_snapshot_nvl(libzfs_handle_t *hdl, nvlist_t *snaps, nvlist_t *props)
{
	int ret;
	char errbuf[1024];
	nvpair_t *elem;
	nvlist_t *errors;

	(void) snprintf(errbuf, sizeof (errbuf), dgettext(TEXT_DOMAIN,
	    "cannot create snapshots "));

	elem = NULL;
	while ((elem = nvlist_next_nvpair(snaps, elem)) != NULL) {
		const char *snapname = nvpair_name(elem);

		/* validate the target name */
		if (!zfs_validate_name(hdl, snapname, ZFS_TYPE_SNAPSHOT,
		    B_TRUE)) {
			(void) snprintf(errbuf, sizeof (errbuf),
			    dgettext(TEXT_DOMAIN,
			    "cannot create snapshot '%s'"), snapname);
			return (zfs_error(hdl, EZFS_INVALIDNAME, errbuf));
		}
	}

	/*
	 * get pool handle for prop validation. assumes all snaps are in the
	 * same pool, as does lzc_snapshot (below).
	 */
	char pool[ZFS_MAX_DATASET_NAME_LEN];
	elem = nvlist_next_nvpair(snaps, NULL);
	(void) strlcpy(pool, nvpair_name(elem), sizeof (pool));
	pool[strcspn(pool, "/@")] = '\0';
	zpool_handle_t *zpool_hdl = zpool_open(hdl, pool);

	if (props != NULL &&
	    (props = zfs_valid_proplist(hdl, ZFS_TYPE_SNAPSHOT,
	    props, B_FALSE, NULL, zpool_hdl, errbuf)) == NULL) {
		zpool_close(zpool_hdl);
		return (-1);
	}
	zpool_close(zpool_hdl);

	ret = lzc_snapshot(snaps, props, &errors);

	if (ret != 0) {
		boolean_t printed = B_FALSE;
		for (elem = nvlist_next_nvpair(errors, NULL);
		    elem != NULL;
		    elem = nvlist_next_nvpair(errors, elem)) {
			(void) snprintf(errbuf, sizeof (errbuf),
			    dgettext(TEXT_DOMAIN,
			    "cannot create snapshot '%s'"), nvpair_name(elem));
			(void) zfs_standard_error(hdl,
			    fnvpair_value_int32(elem), errbuf);
			printed = B_TRUE;
		}
		if (!printed) {
			switch (ret) {
			case EXDEV:
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "multiple snapshots of same "
				    "fs not allowed"));
				(void) zfs_error(hdl, EZFS_EXISTS, errbuf);

				break;
			default:
				(void) zfs_standard_error(hdl, ret, errbuf);
			}
		}
	}

	nvlist_free(props);
	nvlist_free(errors);
	return (ret);
}

int
zfs_snapshot(libzfs_handle_t *hdl, const char *path, boolean_t recursive,
    nvlist_t *props)
{
	int ret;
	snapdata_t sd = { 0 };
	char fsname[ZFS_MAX_DATASET_NAME_LEN];
	char *cp;
	zfs_handle_t *zhp;
	char errbuf[1024];

	(void) snprintf(errbuf, sizeof (errbuf), dgettext(TEXT_DOMAIN,
	    "cannot snapshot %s"), path);

	if (!zfs_validate_name(hdl, path, ZFS_TYPE_SNAPSHOT, B_TRUE))
		return (zfs_error(hdl, EZFS_INVALIDNAME, errbuf));

	(void) strlcpy(fsname, path, sizeof (fsname));
	cp = strchr(fsname, '@');
	*cp = '\0';
	sd.sd_snapname = cp + 1;

	if ((zhp = zfs_open(hdl, fsname, ZFS_TYPE_FILESYSTEM |
	    ZFS_TYPE_VOLUME)) == NULL) {
		return (-1);
	}

	verify(nvlist_alloc(&sd.sd_nvl, NV_UNIQUE_NAME, 0) == 0);
	if (recursive) {
		(void) zfs_snapshot_cb(zfs_handle_dup(zhp), &sd);
	} else {
		fnvlist_add_boolean(sd.sd_nvl, path);
	}

	ret = zfs_snapshot_nvl(hdl, sd.sd_nvl, props);
	nvlist_free(sd.sd_nvl);
	zfs_close(zhp);
	return (ret);
}

/*
 * Destroy any more recent snapshots.  We invoke this callback on any dependents
 * of the snapshot first.  If the 'cb_dependent' member is non-zero, then this
 * is a dependent and we should just destroy it without checking the transaction
 * group.
 */
typedef struct rollback_data {
	const char	*cb_target;		/* the snapshot */
	uint64_t	cb_create;		/* creation time reference */
	boolean_t	cb_error;
	boolean_t	cb_force;
} rollback_data_t;

static int
rollback_destroy_dependent(zfs_handle_t *zhp, void *data)
{
	rollback_data_t *cbp = data;
	prop_changelist_t *clp;

	/* We must destroy this clone; first unmount it */
	clp = changelist_gather(zhp, ZFS_PROP_NAME, 0,
	    cbp->cb_force ? MS_FORCE: 0);
	if (clp == NULL || changelist_prefix(clp) != 0) {
		cbp->cb_error = B_TRUE;
		zfs_close(zhp);
		return (0);
	}
	if (zfs_destroy(zhp, B_FALSE) != 0)
		cbp->cb_error = B_TRUE;
	else
		changelist_remove(clp, zhp->zfs_name);
	(void) changelist_postfix(clp);
	changelist_free(clp);

	zfs_close(zhp);
	return (0);
}

static int
rollback_destroy(zfs_handle_t *zhp, void *data)
{
	rollback_data_t *cbp = data;

	if (zfs_prop_get_int(zhp, ZFS_PROP_CREATETXG) > cbp->cb_create) {
		cbp->cb_error |= zfs_iter_dependents(zhp, B_FALSE,
		    rollback_destroy_dependent, cbp);

		cbp->cb_error |= zfs_destroy(zhp, B_FALSE);
	}

	zfs_close(zhp);
	return (0);
}

/*
 * Given a dataset, rollback to a specific snapshot, discarding any
 * data changes since then and making it the active dataset.
 *
 * Any snapshots and bookmarks more recent than the target are
 * destroyed, along with their dependents (i.e. clones).
 */
int
zfs_rollback(zfs_handle_t *zhp, zfs_handle_t *snap, boolean_t force)
{
	rollback_data_t cb = { 0 };
	int err;
	boolean_t restore_resv = 0;
	uint64_t old_volsize = 0, new_volsize;
	zfs_prop_t resv_prop;

	assert(zhp->zfs_type == ZFS_TYPE_FILESYSTEM ||
	    zhp->zfs_type == ZFS_TYPE_VOLUME);

	/*
	 * Destroy all recent snapshots and their dependents.
	 */
	cb.cb_force = force;
	cb.cb_target = snap->zfs_name;
	cb.cb_create = zfs_prop_get_int(snap, ZFS_PROP_CREATETXG);
	(void) zfs_iter_snapshots(zhp, B_FALSE, rollback_destroy, &cb);
	(void) zfs_iter_bookmarks(zhp, rollback_destroy, &cb);

	if (cb.cb_error)
		return (-1);

	/*
	 * Now that we have verified that the snapshot is the latest,
	 * rollback to the given snapshot.
	 */

	if (zhp->zfs_type == ZFS_TYPE_VOLUME) {
		if (zfs_which_resv_prop(zhp, &resv_prop) < 0)
			return (-1);
		old_volsize = zfs_prop_get_int(zhp, ZFS_PROP_VOLSIZE);
		restore_resv =
		    (old_volsize == zfs_prop_get_int(zhp, resv_prop));
	}

	/*
	 * Pass both the filesystem and the wanted snapshot names,
	 * we would get an error back if the snapshot is destroyed or
	 * a new snapshot is created before this request is processed.
	 */
	err = lzc_rollback_to(zhp->zfs_name, snap->zfs_name);
	if (err != 0) {
		char errbuf[1024];

		(void) snprintf(errbuf, sizeof (errbuf),
		    dgettext(TEXT_DOMAIN, "cannot rollback '%s'"),
		    zhp->zfs_name);
		switch (err) {
		case EEXIST:
			zfs_error_aux(zhp->zfs_hdl, dgettext(TEXT_DOMAIN,
			    "there is a snapshot or bookmark more recent "
			    "than '%s'"), snap->zfs_name);
			(void) zfs_error(zhp->zfs_hdl, EZFS_EXISTS, errbuf);
			break;
		case ESRCH:
			zfs_error_aux(zhp->zfs_hdl, dgettext(TEXT_DOMAIN,
			    "'%s' is not found among snapshots of '%s'"),
			    snap->zfs_name, zhp->zfs_name);
			(void) zfs_error(zhp->zfs_hdl, EZFS_NOENT, errbuf);
			break;
		case EINVAL:
			(void) zfs_error(zhp->zfs_hdl, EZFS_BADTYPE, errbuf);
			break;
		default:
			(void) zfs_standard_error(zhp->zfs_hdl, err, errbuf);
		}
		return (err);
	}

	/*
	 * For volumes, if the pre-rollback volsize matched the pre-
	 * rollback reservation and the volsize has changed then set
	 * the reservation property to the post-rollback volsize.
	 * Make a new handle since the rollback closed the dataset.
	 */
	if ((zhp->zfs_type == ZFS_TYPE_VOLUME) &&
	    (zhp = make_dataset_handle(zhp->zfs_hdl, zhp->zfs_name))) {
		if (restore_resv) {
			new_volsize = zfs_prop_get_int(zhp, ZFS_PROP_VOLSIZE);
			if (old_volsize != new_volsize)
				err = zfs_prop_set_int(zhp, resv_prop,
				    new_volsize);
		}
		zfs_close(zhp);
	}
	return (err);
}

/*
 * Renames the given dataset.
 */
int
zfs_rename(zfs_handle_t *zhp, const char *source, const char *target,
    renameflags_t flags)
{
	int ret = 0;
	zfs_cmd_t zc = { 0 };
	char *delim;
	prop_changelist_t *cl = NULL;
	zfs_handle_t *zhrp = NULL;
	char *parentname = NULL;
	char parent[ZFS_MAX_DATASET_NAME_LEN];
	char property[ZFS_MAXPROPLEN];
	libzfs_handle_t *hdl = zhp->zfs_hdl;
	char errbuf[1024];

	/* if we have the same exact name, just return success */
	if (strcmp(zhp->zfs_name, target) == 0)
		return (0);

	(void) snprintf(errbuf, sizeof (errbuf), dgettext(TEXT_DOMAIN,
	    "cannot rename to '%s'"), target);

	if (source != NULL) {
		/*
		 * This is recursive snapshots rename, put snapshot name
		 * (that might not exist) into zfs_name.
		 */
		assert(flags.recurse);

		(void) strlcat(zhp->zfs_name, "@", sizeof(zhp->zfs_name));
		(void) strlcat(zhp->zfs_name, source, sizeof(zhp->zfs_name));
		zhp->zfs_type = ZFS_TYPE_SNAPSHOT;
	}

	/* make sure source name is valid */
	if (!zfs_validate_name(hdl, zhp->zfs_name, zhp->zfs_type, B_TRUE))
		return (zfs_error(hdl, EZFS_INVALIDNAME, errbuf));

	/*
	 * Make sure the target name is valid
	 */
	if (zhp->zfs_type == ZFS_TYPE_SNAPSHOT) {
		if ((strchr(target, '@') == NULL) ||
		    *target == '@') {
			/*
			 * Snapshot target name is abbreviated,
			 * reconstruct full dataset name
			 */
			(void) strlcpy(parent, zhp->zfs_name,
			    sizeof (parent));
			delim = strchr(parent, '@');
			if (strchr(target, '@') == NULL)
				*(++delim) = '\0';
			else
				*delim = '\0';
			(void) strlcat(parent, target, sizeof (parent));
			target = parent;
		} else {
			/*
			 * Make sure we're renaming within the same dataset.
			 */
			delim = strchr(target, '@');
			if (strncmp(zhp->zfs_name, target, delim - target)
			    != 0 || zhp->zfs_name[delim - target] != '@') {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "snapshots must be part of same "
				    "dataset"));
				return (zfs_error(hdl, EZFS_CROSSTARGET,
				    errbuf));
			}
		}

		if (!zfs_validate_name(hdl, target, zhp->zfs_type, B_TRUE))
			return (zfs_error(hdl, EZFS_INVALIDNAME, errbuf));
	} else {
		if (flags.recurse) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "recursive rename must be a snapshot"));
			return (zfs_error(hdl, EZFS_BADTYPE, errbuf));
		}

		if (!zfs_validate_name(hdl, target, zhp->zfs_type, B_TRUE))
			return (zfs_error(hdl, EZFS_INVALIDNAME, errbuf));

		/* validate parents */
		if (check_parents(hdl, target, NULL, B_FALSE, NULL) != 0)
			return (-1);

		/* make sure we're in the same pool */
		verify((delim = strchr(target, '/')) != NULL);
		if (strncmp(zhp->zfs_name, target, delim - target) != 0 ||
		    zhp->zfs_name[delim - target] != '/') {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "datasets must be within same pool"));
			return (zfs_error(hdl, EZFS_CROSSTARGET, errbuf));
		}

		/* new name cannot be a child of the current dataset name */
		if (is_descendant(zhp->zfs_name, target)) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "New dataset name cannot be a descendant of "
			    "current dataset name"));
			return (zfs_error(hdl, EZFS_INVALIDNAME, errbuf));
		}
	}

	(void) snprintf(errbuf, sizeof (errbuf),
	    dgettext(TEXT_DOMAIN, "cannot rename '%s'"), zhp->zfs_name);

	if (getzoneid() == GLOBAL_ZONEID &&
	    zfs_prop_get_int(zhp, ZFS_PROP_ZONED)) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "dataset is used in a non-global zone"));
		return (zfs_error(hdl, EZFS_ZONED, errbuf));
	}

	/*
	 * Avoid unmounting file systems with mountpoint property set to
	 * 'legacy' or 'none' even if -u option is not given.
	 */
	if (zhp->zfs_type == ZFS_TYPE_FILESYSTEM &&
	    !flags.recurse && !flags.nounmount &&
	    zfs_prop_get(zhp, ZFS_PROP_MOUNTPOINT, property,
	    sizeof (property), NULL, NULL, 0, B_FALSE) == 0 &&
	    (strcmp(property, "legacy") == 0 ||
	     strcmp(property, "none") == 0)) {
		flags.nounmount = B_TRUE;
	}
	if (flags.recurse) {

		parentname = zfs_strdup(zhp->zfs_hdl, zhp->zfs_name);
		if (parentname == NULL) {
			ret = -1;
			goto error;
		}
		delim = strchr(parentname, '@');
		*delim = '\0';
		zhrp = zfs_open(zhp->zfs_hdl, parentname, ZFS_TYPE_DATASET);
		if (zhrp == NULL) {
			ret = -1;
			goto error;
		}
	} else if (zhp->zfs_type != ZFS_TYPE_SNAPSHOT) {
		if ((cl = changelist_gather(zhp, ZFS_PROP_NAME,
		    flags.nounmount ? CL_GATHER_DONT_UNMOUNT : 0,
		    flags.forceunmount ? MS_FORCE : 0)) == NULL) {
			return (-1);
		}

		if (changelist_haszonedchild(cl)) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "child dataset with inherited mountpoint is used "
			    "in a non-global zone"));
			(void) zfs_error(hdl, EZFS_ZONED, errbuf);
			ret = -1;
			goto error;
		}

		if ((ret = changelist_prefix(cl)) != 0)
			goto error;
	}

	if (ZFS_IS_VOLUME(zhp))
		zc.zc_objset_type = DMU_OST_ZVOL;
	else
		zc.zc_objset_type = DMU_OST_ZFS;

	(void) strlcpy(zc.zc_name, zhp->zfs_name, sizeof (zc.zc_name));
	(void) strlcpy(zc.zc_value, target, sizeof (zc.zc_value));

	zc.zc_cookie = flags.recurse ? 1 : 0;
	if (flags.nounmount)
		zc.zc_cookie |= 2;

	if ((ret = zfs_ioctl(zhp->zfs_hdl, ZFS_IOC_RENAME, &zc)) != 0) {
		/*
		 * if it was recursive, the one that actually failed will
		 * be in zc.zc_name
		 */
		(void) snprintf(errbuf, sizeof (errbuf), dgettext(TEXT_DOMAIN,
		    "cannot rename '%s'"), zc.zc_name);

		if (flags.recurse && errno == EEXIST) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "a child dataset already has a snapshot "
			    "with the new name"));
			(void) zfs_error(hdl, EZFS_EXISTS, errbuf);
		} else {
			(void) zfs_standard_error(zhp->zfs_hdl, errno, errbuf);
		}

		/*
		 * On failure, we still want to remount any filesystems that
		 * were previously mounted, so we don't alter the system state.
		 */
		if (cl != NULL)
			(void) changelist_postfix(cl);
	} else {
		if (cl != NULL) {
			changelist_rename(cl, zfs_get_name(zhp), target);
			ret = changelist_postfix(cl);
		}
	}

error:
	if (parentname != NULL) {
		free(parentname);
	}
	if (zhrp != NULL) {
		zfs_close(zhrp);
	}
	if (cl != NULL) {
		changelist_free(cl);
	}
	return (ret);
}

nvlist_t *
zfs_get_user_props(zfs_handle_t *zhp)
{
	return (zhp->zfs_user_props);
}

nvlist_t *
zfs_get_recvd_props(zfs_handle_t *zhp)
{
	if (zhp->zfs_recvd_props == NULL)
		if (get_recvd_props_ioctl(zhp) != 0)
			return (NULL);
	return (zhp->zfs_recvd_props);
}

/*
 * This function is used by 'zfs list' to determine the exact set of columns to
 * display, and their maximum widths.  This does two main things:
 *
 *      - If this is a list of all properties, then expand the list to include
 *        all native properties, and set a flag so that for each dataset we look
 *        for new unique user properties and add them to the list.
 *
 *      - For non fixed-width properties, keep track of the maximum width seen
 *        so that we can size the column appropriately. If the user has
 *        requested received property values, we also need to compute the width
 *        of the RECEIVED column.
 */
int
zfs_expand_proplist(zfs_handle_t *zhp, zprop_list_t **plp, boolean_t received,
    boolean_t literal)
{
	libzfs_handle_t *hdl = zhp->zfs_hdl;
	zprop_list_t *entry;
	zprop_list_t **last, **start;
	nvlist_t *userprops, *propval;
	nvpair_t *elem;
	char *strval;
	char buf[ZFS_MAXPROPLEN];

	if (zprop_expand_list(hdl, plp, ZFS_TYPE_DATASET) != 0)
		return (-1);

	userprops = zfs_get_user_props(zhp);

	entry = *plp;
	if (entry->pl_all && nvlist_next_nvpair(userprops, NULL) != NULL) {
		/*
		 * Go through and add any user properties as necessary.  We
		 * start by incrementing our list pointer to the first
		 * non-native property.
		 */
		start = plp;
		while (*start != NULL) {
			if ((*start)->pl_prop == ZPROP_INVAL)
				break;
			start = &(*start)->pl_next;
		}

		elem = NULL;
		while ((elem = nvlist_next_nvpair(userprops, elem)) != NULL) {
			/*
			 * See if we've already found this property in our list.
			 */
			for (last = start; *last != NULL;
			    last = &(*last)->pl_next) {
				if (strcmp((*last)->pl_user_prop,
				    nvpair_name(elem)) == 0)
					break;
			}

			if (*last == NULL) {
				if ((entry = zfs_alloc(hdl,
				    sizeof (zprop_list_t))) == NULL ||
				    ((entry->pl_user_prop = zfs_strdup(hdl,
				    nvpair_name(elem)))) == NULL) {
					free(entry);
					return (-1);
				}

				entry->pl_prop = ZPROP_INVAL;
				entry->pl_width = strlen(nvpair_name(elem));
				entry->pl_all = B_TRUE;
				*last = entry;
			}
		}
	}

	/*
	 * Now go through and check the width of any non-fixed columns
	 */
	for (entry = *plp; entry != NULL; entry = entry->pl_next) {
		if (entry->pl_fixed && !literal)
			continue;

		if (entry->pl_prop != ZPROP_INVAL) {
			if (zfs_prop_get(zhp, entry->pl_prop,
			    buf, sizeof (buf), NULL, NULL, 0, literal) == 0) {
				if (strlen(buf) > entry->pl_width)
					entry->pl_width = strlen(buf);
			}
			if (received && zfs_prop_get_recvd(zhp,
			    zfs_prop_to_name(entry->pl_prop),
			    buf, sizeof (buf), literal) == 0)
				if (strlen(buf) > entry->pl_recvd_width)
					entry->pl_recvd_width = strlen(buf);
		} else {
			if (nvlist_lookup_nvlist(userprops, entry->pl_user_prop,
			    &propval) == 0) {
				verify(nvlist_lookup_string(propval,
				    ZPROP_VALUE, &strval) == 0);
				if (strlen(strval) > entry->pl_width)
					entry->pl_width = strlen(strval);
			}
			if (received && zfs_prop_get_recvd(zhp,
			    entry->pl_user_prop,
			    buf, sizeof (buf), literal) == 0)
				if (strlen(buf) > entry->pl_recvd_width)
					entry->pl_recvd_width = strlen(buf);
		}
	}

	return (0);
}

int
zfs_deleg_share_nfs(libzfs_handle_t *hdl, char *dataset, char *path,
    char *resource, void *export, void *sharetab,
    int sharemax, zfs_share_op_t operation)
{
	zfs_cmd_t zc = { 0 };
	int error;

	(void) strlcpy(zc.zc_name, dataset, sizeof (zc.zc_name));
	(void) strlcpy(zc.zc_value, path, sizeof (zc.zc_value));
	if (resource)
		(void) strlcpy(zc.zc_string, resource, sizeof (zc.zc_string));
	zc.zc_share.z_sharedata = (uint64_t)(uintptr_t)sharetab;
	zc.zc_share.z_exportdata = (uint64_t)(uintptr_t)export;
	zc.zc_share.z_sharetype = operation;
	zc.zc_share.z_sharemax = sharemax;
	error = ioctl(hdl->libzfs_fd, ZFS_IOC_SHARE, &zc);
	return (error);
}

void
zfs_prune_proplist(zfs_handle_t *zhp, uint8_t *props)
{
	nvpair_t *curr;

	/*
	 * Keep a reference to the props-table against which we prune the
	 * properties.
	 */
	zhp->zfs_props_table = props;

	curr = nvlist_next_nvpair(zhp->zfs_props, NULL);

	while (curr) {
		zfs_prop_t zfs_prop = zfs_name_to_prop(nvpair_name(curr));
		nvpair_t *next = nvlist_next_nvpair(zhp->zfs_props, curr);

		/*
		 * User properties will result in ZPROP_INVAL, and since we
		 * only know how to prune standard ZFS properties, we always
		 * leave these in the list.  This can also happen if we
		 * encounter an unknown DSL property (when running older
		 * software, for example).
		 */
		if (zfs_prop != ZPROP_INVAL && props[zfs_prop] == B_FALSE)
			(void) nvlist_remove(zhp->zfs_props,
			    nvpair_name(curr), nvpair_type(curr));
		curr = next;
	}
}

#ifdef illumos
static int
zfs_smb_acl_mgmt(libzfs_handle_t *hdl, char *dataset, char *path,
    zfs_smb_acl_op_t cmd, char *resource1, char *resource2)
{
	zfs_cmd_t zc = { 0 };
	nvlist_t *nvlist = NULL;
	int error;

	(void) strlcpy(zc.zc_name, dataset, sizeof (zc.zc_name));
	(void) strlcpy(zc.zc_value, path, sizeof (zc.zc_value));
	zc.zc_cookie = (uint64_t)cmd;

	if (cmd == ZFS_SMB_ACL_RENAME) {
		if (nvlist_alloc(&nvlist, NV_UNIQUE_NAME, 0) != 0) {
			(void) no_memory(hdl);
			return (0);
		}
	}

	switch (cmd) {
	case ZFS_SMB_ACL_ADD:
	case ZFS_SMB_ACL_REMOVE:
		(void) strlcpy(zc.zc_string, resource1, sizeof (zc.zc_string));
		break;
	case ZFS_SMB_ACL_RENAME:
		if (nvlist_add_string(nvlist, ZFS_SMB_ACL_SRC,
		    resource1) != 0) {
				(void) no_memory(hdl);
				return (-1);
		}
		if (nvlist_add_string(nvlist, ZFS_SMB_ACL_TARGET,
		    resource2) != 0) {
				(void) no_memory(hdl);
				return (-1);
		}
		if (zcmd_write_src_nvlist(hdl, &zc, nvlist) != 0) {
			nvlist_free(nvlist);
			return (-1);
		}
		break;
	case ZFS_SMB_ACL_PURGE:
		break;
	default:
		return (-1);
	}
	error = ioctl(hdl->libzfs_fd, ZFS_IOC_SMB_ACL, &zc);
	nvlist_free(nvlist);
	return (error);
}

int
zfs_smb_acl_add(libzfs_handle_t *hdl, char *dataset,
    char *path, char *resource)
{
	return (zfs_smb_acl_mgmt(hdl, dataset, path, ZFS_SMB_ACL_ADD,
	    resource, NULL));
}

int
zfs_smb_acl_remove(libzfs_handle_t *hdl, char *dataset,
    char *path, char *resource)
{
	return (zfs_smb_acl_mgmt(hdl, dataset, path, ZFS_SMB_ACL_REMOVE,
	    resource, NULL));
}

int
zfs_smb_acl_purge(libzfs_handle_t *hdl, char *dataset, char *path)
{
	return (zfs_smb_acl_mgmt(hdl, dataset, path, ZFS_SMB_ACL_PURGE,
	    NULL, NULL));
}

int
zfs_smb_acl_rename(libzfs_handle_t *hdl, char *dataset, char *path,
    char *oldname, char *newname)
{
	return (zfs_smb_acl_mgmt(hdl, dataset, path, ZFS_SMB_ACL_RENAME,
	    oldname, newname));
}
#endif	/* illumos */

int
zfs_userspace(zfs_handle_t *zhp, zfs_userquota_prop_t type,
    zfs_userspace_cb_t func, void *arg)
{
	zfs_cmd_t zc = { 0 };
	zfs_useracct_t buf[100];
	libzfs_handle_t *hdl = zhp->zfs_hdl;
	int ret;

	(void) strlcpy(zc.zc_name, zhp->zfs_name, sizeof (zc.zc_name));

	zc.zc_objset_type = type;
	zc.zc_nvlist_dst = (uintptr_t)buf;

	for (;;) {
		zfs_useracct_t *zua = buf;

		zc.zc_nvlist_dst_size = sizeof (buf);
		if (zfs_ioctl(hdl, ZFS_IOC_USERSPACE_MANY, &zc) != 0) {
			char errbuf[1024];

			(void) snprintf(errbuf, sizeof (errbuf),
			    dgettext(TEXT_DOMAIN,
			    "cannot get used/quota for %s"), zc.zc_name);
			return (zfs_standard_error_fmt(hdl, errno, errbuf));
		}
		if (zc.zc_nvlist_dst_size == 0)
			break;

		while (zc.zc_nvlist_dst_size > 0) {
			if ((ret = func(arg, zua->zu_domain, zua->zu_rid,
			    zua->zu_space)) != 0)
				return (ret);
			zua++;
			zc.zc_nvlist_dst_size -= sizeof (zfs_useracct_t);
		}
	}

	return (0);
}

struct holdarg {
	nvlist_t *nvl;
	const char *snapname;
	const char *tag;
	boolean_t recursive;
	int error;
};

static int
zfs_hold_one(zfs_handle_t *zhp, void *arg)
{
	struct holdarg *ha = arg;
	char name[ZFS_MAX_DATASET_NAME_LEN];
	int rv = 0;

	(void) snprintf(name, sizeof (name),
	    "%s@%s", zhp->zfs_name, ha->snapname);

	if (lzc_exists(name))
		fnvlist_add_string(ha->nvl, name, ha->tag);

	if (ha->recursive)
		rv = zfs_iter_filesystems(zhp, zfs_hold_one, ha);
	zfs_close(zhp);
	return (rv);
}

int
zfs_hold(zfs_handle_t *zhp, const char *snapname, const char *tag,
    boolean_t recursive, int cleanup_fd)
{
	int ret;
	struct holdarg ha;

	ha.nvl = fnvlist_alloc();
	ha.snapname = snapname;
	ha.tag = tag;
	ha.recursive = recursive;
	(void) zfs_hold_one(zfs_handle_dup(zhp), &ha);

	if (nvlist_empty(ha.nvl)) {
		char errbuf[1024];

		fnvlist_free(ha.nvl);
		ret = ENOENT;
		(void) snprintf(errbuf, sizeof (errbuf),
		    dgettext(TEXT_DOMAIN,
		    "cannot hold snapshot '%s@%s'"),
		    zhp->zfs_name, snapname);
		(void) zfs_standard_error(zhp->zfs_hdl, ret, errbuf);
		return (ret);
	}

	ret = zfs_hold_nvl(zhp, cleanup_fd, ha.nvl);
	fnvlist_free(ha.nvl);

	return (ret);
}

int
zfs_hold_nvl(zfs_handle_t *zhp, int cleanup_fd, nvlist_t *holds)
{
	int ret;
	nvlist_t *errors;
	libzfs_handle_t *hdl = zhp->zfs_hdl;
	char errbuf[1024];
	nvpair_t *elem;

	errors = NULL;
	ret = lzc_hold(holds, cleanup_fd, &errors);

	if (ret == 0) {
		/* There may be errors even in the success case. */
		fnvlist_free(errors);
		return (0);
	}

	if (nvlist_empty(errors)) {
		/* no hold-specific errors */
		(void) snprintf(errbuf, sizeof (errbuf),
		    dgettext(TEXT_DOMAIN, "cannot hold"));
		switch (ret) {
		case ENOTSUP:
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "pool must be upgraded"));
			(void) zfs_error(hdl, EZFS_BADVERSION, errbuf);
			break;
		case EINVAL:
			(void) zfs_error(hdl, EZFS_BADTYPE, errbuf);
			break;
		default:
			(void) zfs_standard_error(hdl, ret, errbuf);
		}
	}

	for (elem = nvlist_next_nvpair(errors, NULL);
	    elem != NULL;
	    elem = nvlist_next_nvpair(errors, elem)) {
		(void) snprintf(errbuf, sizeof (errbuf),
		    dgettext(TEXT_DOMAIN,
		    "cannot hold snapshot '%s'"), nvpair_name(elem));
		switch (fnvpair_value_int32(elem)) {
		case E2BIG:
			/*
			 * Temporary tags wind up having the ds object id
			 * prepended. So even if we passed the length check
			 * above, it's still possible for the tag to wind
			 * up being slightly too long.
			 */
			(void) zfs_error(hdl, EZFS_TAGTOOLONG, errbuf);
			break;
		case EINVAL:
			(void) zfs_error(hdl, EZFS_BADTYPE, errbuf);
			break;
		case EEXIST:
			(void) zfs_error(hdl, EZFS_REFTAG_HOLD, errbuf);
			break;
		default:
			(void) zfs_standard_error(hdl,
			    fnvpair_value_int32(elem), errbuf);
		}
	}

	fnvlist_free(errors);
	return (ret);
}

static int
zfs_release_one(zfs_handle_t *zhp, void *arg)
{
	struct holdarg *ha = arg;
	char name[ZFS_MAX_DATASET_NAME_LEN];
	int rv = 0;
	nvlist_t *existing_holds;

	(void) snprintf(name, sizeof (name),
	    "%s@%s", zhp->zfs_name, ha->snapname);

	if (lzc_get_holds(name, &existing_holds) != 0) {
		ha->error = ENOENT;
	} else if (!nvlist_exists(existing_holds, ha->tag)) {
		ha->error = ESRCH;
	} else {
		nvlist_t *torelease = fnvlist_alloc();
		fnvlist_add_boolean(torelease, ha->tag);
		fnvlist_add_nvlist(ha->nvl, name, torelease);
		fnvlist_free(torelease);
	}

	if (ha->recursive)
		rv = zfs_iter_filesystems(zhp, zfs_release_one, ha);
	zfs_close(zhp);
	return (rv);
}

int
zfs_release(zfs_handle_t *zhp, const char *snapname, const char *tag,
    boolean_t recursive)
{
	int ret;
	struct holdarg ha;
	nvlist_t *errors = NULL;
	nvpair_t *elem;
	libzfs_handle_t *hdl = zhp->zfs_hdl;
	char errbuf[1024];

	ha.nvl = fnvlist_alloc();
	ha.snapname = snapname;
	ha.tag = tag;
	ha.recursive = recursive;
	ha.error = 0;
	(void) zfs_release_one(zfs_handle_dup(zhp), &ha);

	if (nvlist_empty(ha.nvl)) {
		fnvlist_free(ha.nvl);
		ret = ha.error;
		(void) snprintf(errbuf, sizeof (errbuf),
		    dgettext(TEXT_DOMAIN,
		    "cannot release hold from snapshot '%s@%s'"),
		    zhp->zfs_name, snapname);
		if (ret == ESRCH) {
			(void) zfs_error(hdl, EZFS_REFTAG_RELE, errbuf);
		} else {
			(void) zfs_standard_error(hdl, ret, errbuf);
		}
		return (ret);
	}

	ret = lzc_release(ha.nvl, &errors);
	fnvlist_free(ha.nvl);

	if (ret == 0) {
		/* There may be errors even in the success case. */
		fnvlist_free(errors);
		return (0);
	}

	if (nvlist_empty(errors)) {
		/* no hold-specific errors */
		(void) snprintf(errbuf, sizeof (errbuf), dgettext(TEXT_DOMAIN,
		    "cannot release"));
		switch (errno) {
		case ENOTSUP:
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "pool must be upgraded"));
			(void) zfs_error(hdl, EZFS_BADVERSION, errbuf);
			break;
		default:
			(void) zfs_standard_error_fmt(hdl, errno, errbuf);
		}
	}

	for (elem = nvlist_next_nvpair(errors, NULL);
	    elem != NULL;
	    elem = nvlist_next_nvpair(errors, elem)) {
		(void) snprintf(errbuf, sizeof (errbuf),
		    dgettext(TEXT_DOMAIN,
		    "cannot release hold from snapshot '%s'"),
		    nvpair_name(elem));
		switch (fnvpair_value_int32(elem)) {
		case ESRCH:
			(void) zfs_error(hdl, EZFS_REFTAG_RELE, errbuf);
			break;
		case EINVAL:
			(void) zfs_error(hdl, EZFS_BADTYPE, errbuf);
			break;
		default:
			(void) zfs_standard_error_fmt(hdl,
			    fnvpair_value_int32(elem), errbuf);
		}
	}

	fnvlist_free(errors);
	return (ret);
}

int
zfs_get_fsacl(zfs_handle_t *zhp, nvlist_t **nvl)
{
	zfs_cmd_t zc = { 0 };
	libzfs_handle_t *hdl = zhp->zfs_hdl;
	int nvsz = 2048;
	void *nvbuf;
	int err = 0;
	char errbuf[1024];

	assert(zhp->zfs_type == ZFS_TYPE_VOLUME ||
	    zhp->zfs_type == ZFS_TYPE_FILESYSTEM);

tryagain:

	nvbuf = malloc(nvsz);
	if (nvbuf == NULL) {
		err = (zfs_error(hdl, EZFS_NOMEM, strerror(errno)));
		goto out;
	}

	zc.zc_nvlist_dst_size = nvsz;
	zc.zc_nvlist_dst = (uintptr_t)nvbuf;

	(void) strlcpy(zc.zc_name, zhp->zfs_name, sizeof (zc.zc_name));

	if (ioctl(hdl->libzfs_fd, ZFS_IOC_GET_FSACL, &zc) != 0) {
		(void) snprintf(errbuf, sizeof (errbuf),
		    dgettext(TEXT_DOMAIN, "cannot get permissions on '%s'"),
		    zc.zc_name);
		switch (errno) {
		case ENOMEM:
			free(nvbuf);
			nvsz = zc.zc_nvlist_dst_size;
			goto tryagain;

		case ENOTSUP:
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "pool must be upgraded"));
			err = zfs_error(hdl, EZFS_BADVERSION, errbuf);
			break;
		case EINVAL:
			err = zfs_error(hdl, EZFS_BADTYPE, errbuf);
			break;
		case ENOENT:
			err = zfs_error(hdl, EZFS_NOENT, errbuf);
			break;
		default:
			err = zfs_standard_error_fmt(hdl, errno, errbuf);
			break;
		}
	} else {
		/* success */
		int rc = nvlist_unpack(nvbuf, zc.zc_nvlist_dst_size, nvl, 0);
		if (rc) {
			(void) snprintf(errbuf, sizeof (errbuf), dgettext(
			    TEXT_DOMAIN, "cannot get permissions on '%s'"),
			    zc.zc_name);
			err = zfs_standard_error_fmt(hdl, rc, errbuf);
		}
	}

	free(nvbuf);
out:
	return (err);
}

int
zfs_set_fsacl(zfs_handle_t *zhp, boolean_t un, nvlist_t *nvl)
{
	zfs_cmd_t zc = { 0 };
	libzfs_handle_t *hdl = zhp->zfs_hdl;
	char *nvbuf;
	char errbuf[1024];
	size_t nvsz;
	int err;

	assert(zhp->zfs_type == ZFS_TYPE_VOLUME ||
	    zhp->zfs_type == ZFS_TYPE_FILESYSTEM);

	err = nvlist_size(nvl, &nvsz, NV_ENCODE_NATIVE);
	assert(err == 0);

	nvbuf = malloc(nvsz);

	err = nvlist_pack(nvl, &nvbuf, &nvsz, NV_ENCODE_NATIVE, 0);
	assert(err == 0);

	zc.zc_nvlist_src_size = nvsz;
	zc.zc_nvlist_src = (uintptr_t)nvbuf;
	zc.zc_perm_action = un;

	(void) strlcpy(zc.zc_name, zhp->zfs_name, sizeof (zc.zc_name));

	if (zfs_ioctl(hdl, ZFS_IOC_SET_FSACL, &zc) != 0) {
		(void) snprintf(errbuf, sizeof (errbuf),
		    dgettext(TEXT_DOMAIN, "cannot set permissions on '%s'"),
		    zc.zc_name);
		switch (errno) {
		case ENOTSUP:
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "pool must be upgraded"));
			err = zfs_error(hdl, EZFS_BADVERSION, errbuf);
			break;
		case EINVAL:
			err = zfs_error(hdl, EZFS_BADTYPE, errbuf);
			break;
		case ENOENT:
			err = zfs_error(hdl, EZFS_NOENT, errbuf);
			break;
		default:
			err = zfs_standard_error_fmt(hdl, errno, errbuf);
			break;
		}
	}

	free(nvbuf);

	return (err);
}

int
zfs_get_holds(zfs_handle_t *zhp, nvlist_t **nvl)
{
	int err;
	char errbuf[1024];

	err = lzc_get_holds(zhp->zfs_name, nvl);

	if (err != 0) {
		libzfs_handle_t *hdl = zhp->zfs_hdl;

		(void) snprintf(errbuf, sizeof (errbuf),
		    dgettext(TEXT_DOMAIN, "cannot get holds for '%s'"),
		    zhp->zfs_name);
		switch (err) {
		case ENOTSUP:
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "pool must be upgraded"));
			err = zfs_error(hdl, EZFS_BADVERSION, errbuf);
			break;
		case EINVAL:
			err = zfs_error(hdl, EZFS_BADTYPE, errbuf);
			break;
		case ENOENT:
			err = zfs_error(hdl, EZFS_NOENT, errbuf);
			break;
		default:
			err = zfs_standard_error_fmt(hdl, errno, errbuf);
			break;
		}
	}

	return (err);
}

/*
 * Convert the zvol's volume size to an appropriate reservation.
 * Note: If this routine is updated, it is necessary to update the ZFS test
 * suite's shell version in reservation.kshlib.
 */
uint64_t
zvol_volsize_to_reservation(uint64_t volsize, nvlist_t *props)
{
	uint64_t numdb;
	uint64_t nblocks, volblocksize;
	int ncopies;
	char *strval;

	if (nvlist_lookup_string(props,
	    zfs_prop_to_name(ZFS_PROP_COPIES), &strval) == 0)
		ncopies = atoi(strval);
	else
		ncopies = 1;
	if (nvlist_lookup_uint64(props,
	    zfs_prop_to_name(ZFS_PROP_VOLBLOCKSIZE),
	    &volblocksize) != 0)
		volblocksize = ZVOL_DEFAULT_BLOCKSIZE;
	nblocks = volsize/volblocksize;
	/* start with metadnode L0-L6 */
	numdb = 7;
	/* calculate number of indirects */
	while (nblocks > 1) {
		nblocks += DNODES_PER_LEVEL - 1;
		nblocks /= DNODES_PER_LEVEL;
		numdb += nblocks;
	}
	numdb *= MIN(SPA_DVAS_PER_BP, ncopies + 1);
	volsize *= ncopies;
	/*
	 * this is exactly DN_MAX_INDBLKSHIFT when metadata isn't
	 * compressed, but in practice they compress down to about
	 * 1100 bytes
	 */
	numdb *= 1ULL << DN_MAX_INDBLKSHIFT;
	volsize += numdb;
	return (volsize);
}

/*
 * Attach/detach the given filesystem to/from the given jail.
 */
int
zfs_jail(zfs_handle_t *zhp, int jailid, int attach)
{
	libzfs_handle_t *hdl = zhp->zfs_hdl;
	zfs_cmd_t zc = { 0 };
	char errbuf[1024];
	unsigned long cmd;
	int ret;

	if (attach) {
		(void) snprintf(errbuf, sizeof (errbuf),
		    dgettext(TEXT_DOMAIN, "cannot jail '%s'"), zhp->zfs_name);
	} else {
		(void) snprintf(errbuf, sizeof (errbuf),
		    dgettext(TEXT_DOMAIN, "cannot unjail '%s'"), zhp->zfs_name);
	}

	switch (zhp->zfs_type) {
	case ZFS_TYPE_VOLUME:
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "volumes can not be jailed"));
		return (zfs_error(hdl, EZFS_BADTYPE, errbuf));
	case ZFS_TYPE_SNAPSHOT:
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "snapshots can not be jailed"));
		return (zfs_error(hdl, EZFS_BADTYPE, errbuf));
	}
	assert(zhp->zfs_type == ZFS_TYPE_FILESYSTEM);

	(void) strlcpy(zc.zc_name, zhp->zfs_name, sizeof (zc.zc_name));
	zc.zc_objset_type = DMU_OST_ZFS;
	zc.zc_jailid = jailid;

	cmd = attach ? ZFS_IOC_JAIL : ZFS_IOC_UNJAIL;
	if ((ret = ioctl(hdl->libzfs_fd, cmd, &zc)) != 0)
		zfs_standard_error(hdl, errno, errbuf);

	return (ret);
}
