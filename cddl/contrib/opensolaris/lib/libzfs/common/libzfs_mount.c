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
 * Copyright 2015 Nexenta Systems, Inc.  All rights reserved.
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2014, 2016 by Delphix. All rights reserved.
 * Copyright 2016 Igor Kozhukhov <ikozhukhov@gmail.com>
 * Copyright 2017 Joyent, Inc.
 * Copyright 2017 RackTop Systems.
 * Copyright 2018 OmniOS Community Edition (OmniOSce) Association.
 */

/*
 * Routines to manage ZFS mounts.  We separate all the nasty routines that have
 * to deal with the OS.  The following functions are the main entry points --
 * they are used by mount and unmount and when changing a filesystem's
 * mountpoint.
 *
 *	zfs_is_mounted()
 *	zfs_mount()
 *	zfs_unmount()
 *	zfs_unmountall()
 *
 * This file also contains the functions used to manage sharing filesystems via
 * NFS and iSCSI:
 *
 *	zfs_is_shared()
 *	zfs_share()
 *	zfs_unshare()
 *
 *	zfs_is_shared_nfs()
 *	zfs_is_shared_smb()
 *	zfs_share_proto()
 *	zfs_shareall();
 *	zfs_unshare_nfs()
 *	zfs_unshare_smb()
 *	zfs_unshareall_nfs()
 *	zfs_unshareall_smb()
 *	zfs_unshareall()
 *	zfs_unshareall_bypath()
 *
 * The following functions are available for pool consumers, and will
 * mount/unmount and share/unshare all datasets within pool:
 *
 *	zpool_enable_datasets()
 *	zpool_disable_datasets()
 */

#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <libintl.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <zone.h>
#include <sys/mntent.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

#include <libzfs.h>

#include "libzfs_impl.h"
#include <thread_pool.h>

#include <libshare.h>
#define	MAXISALEN	257	/* based on sysinfo(2) man page */

static int mount_tp_nthr = 512; /* tpool threads for multi-threaded mounting */

static void zfs_mount_task(void *);
static int zfs_share_proto(zfs_handle_t *, zfs_share_proto_t *);
zfs_share_type_t zfs_is_shared_proto(zfs_handle_t *, char **,
    zfs_share_proto_t);

/*
 * The share protocols table must be in the same order as the zfs_share_proto_t
 * enum in libzfs_impl.h
 */
typedef struct {
	zfs_prop_t p_prop;
	char *p_name;
	int p_share_err;
	int p_unshare_err;
} proto_table_t;

proto_table_t proto_table[PROTO_END] = {
	{ZFS_PROP_SHARENFS, "nfs", EZFS_SHARENFSFAILED, EZFS_UNSHARENFSFAILED},
	{ZFS_PROP_SHARESMB, "smb", EZFS_SHARESMBFAILED, EZFS_UNSHARESMBFAILED},
};

zfs_share_proto_t nfs_only[] = {
	PROTO_NFS,
	PROTO_END
};

zfs_share_proto_t smb_only[] = {
	PROTO_SMB,
	PROTO_END
};
zfs_share_proto_t share_all_proto[] = {
	PROTO_NFS,
	PROTO_SMB,
	PROTO_END
};

/*
 * Search the sharetab for the given mountpoint and protocol, returning
 * a zfs_share_type_t value.
 */
static zfs_share_type_t
is_shared(libzfs_handle_t *hdl, const char *mountpoint, zfs_share_proto_t proto)
{
	char buf[MAXPATHLEN], *tab;
	char *ptr;

	if (hdl->libzfs_sharetab == NULL)
		return (SHARED_NOT_SHARED);

	(void) fseek(hdl->libzfs_sharetab, 0, SEEK_SET);

	while (fgets(buf, sizeof (buf), hdl->libzfs_sharetab) != NULL) {

		/* the mountpoint is the first entry on each line */
		if ((tab = strchr(buf, '\t')) == NULL)
			continue;

		*tab = '\0';
		if (strcmp(buf, mountpoint) == 0) {
#ifdef illumos
			/*
			 * the protocol field is the third field
			 * skip over second field
			 */
			ptr = ++tab;
			if ((tab = strchr(ptr, '\t')) == NULL)
				continue;
			ptr = ++tab;
			if ((tab = strchr(ptr, '\t')) == NULL)
				continue;
			*tab = '\0';
			if (strcmp(ptr,
			    proto_table[proto].p_name) == 0) {
				switch (proto) {
				case PROTO_NFS:
					return (SHARED_NFS);
				case PROTO_SMB:
					return (SHARED_SMB);
				default:
					return (0);
				}
			}
#else
			if (proto == PROTO_NFS)
				return (SHARED_NFS);
#endif
		}
	}

	return (SHARED_NOT_SHARED);
}

#ifdef illumos
static boolean_t
dir_is_empty_stat(const char *dirname)
{
	struct stat st;

	/*
	 * We only want to return false if the given path is a non empty
	 * directory, all other errors are handled elsewhere.
	 */
	if (stat(dirname, &st) < 0 || !S_ISDIR(st.st_mode)) {
		return (B_TRUE);
	}

	/*
	 * An empty directory will still have two entries in it, one
	 * entry for each of "." and "..".
	 */
	if (st.st_size > 2) {
		return (B_FALSE);
	}

	return (B_TRUE);
}

static boolean_t
dir_is_empty_readdir(const char *dirname)
{
	DIR *dirp;
	struct dirent64 *dp;
	int dirfd;

	if ((dirfd = openat(AT_FDCWD, dirname,
	    O_RDONLY | O_NDELAY | O_LARGEFILE | O_CLOEXEC, 0)) < 0) {
		return (B_TRUE);
	}

	if ((dirp = fdopendir(dirfd)) == NULL) {
		(void) close(dirfd);
		return (B_TRUE);
	}

	while ((dp = readdir64(dirp)) != NULL) {

		if (strcmp(dp->d_name, ".") == 0 ||
		    strcmp(dp->d_name, "..") == 0)
			continue;

		(void) closedir(dirp);
		return (B_FALSE);
	}

	(void) closedir(dirp);
	return (B_TRUE);
}

/*
 * Returns true if the specified directory is empty.  If we can't open the
 * directory at all, return true so that the mount can fail with a more
 * informative error message.
 */
static boolean_t
dir_is_empty(const char *dirname)
{
	struct statvfs64 st;

	/*
	 * If the statvfs call fails or the filesystem is not a ZFS
	 * filesystem, fall back to the slow path which uses readdir.
	 */
	if ((statvfs64(dirname, &st) != 0) ||
	    (strcmp(st.f_basetype, "zfs") != 0)) {
		return (dir_is_empty_readdir(dirname));
	}

	/*
	 * At this point, we know the provided path is on a ZFS
	 * filesystem, so we can use stat instead of readdir to
	 * determine if the directory is empty or not. We try to avoid
	 * using readdir because that requires opening "dirname"; this
	 * open file descriptor can potentially end up in a child
	 * process if there's a concurrent fork, thus preventing the
	 * zfs_mount() from otherwise succeeding (the open file
	 * descriptor inherited by the child process will cause the
	 * parent's mount to fail with EBUSY). The performance
	 * implications of replacing the open, read, and close with a
	 * single stat is nice; but is not the main motivation for the
	 * added complexity.
	 */
	return (dir_is_empty_stat(dirname));
}
#endif

/*
 * Checks to see if the mount is active.  If the filesystem is mounted, we fill
 * in 'where' with the current mountpoint, and return 1.  Otherwise, we return
 * 0.
 */
boolean_t
is_mounted(libzfs_handle_t *zfs_hdl, const char *special, char **where)
{
	struct mnttab entry;

	if (libzfs_mnttab_find(zfs_hdl, special, &entry) != 0)
		return (B_FALSE);

	if (where != NULL)
		*where = zfs_strdup(zfs_hdl, entry.mnt_mountp);

	return (B_TRUE);
}

boolean_t
zfs_is_mounted(zfs_handle_t *zhp, char **where)
{
	return (is_mounted(zhp->zfs_hdl, zfs_get_name(zhp), where));
}

/*
 * Returns true if the given dataset is mountable, false otherwise.  Returns the
 * mountpoint in 'buf'.
 */
static boolean_t
zfs_is_mountable(zfs_handle_t *zhp, char *buf, size_t buflen,
    zprop_source_t *source)
{
	char sourceloc[MAXNAMELEN];
	zprop_source_t sourcetype;

	if (!zfs_prop_valid_for_type(ZFS_PROP_MOUNTPOINT, zhp->zfs_type))
		return (B_FALSE);

	verify(zfs_prop_get(zhp, ZFS_PROP_MOUNTPOINT, buf, buflen,
	    &sourcetype, sourceloc, sizeof (sourceloc), B_FALSE) == 0);

	if (strcmp(buf, ZFS_MOUNTPOINT_NONE) == 0 ||
	    strcmp(buf, ZFS_MOUNTPOINT_LEGACY) == 0)
		return (B_FALSE);

	if (zfs_prop_get_int(zhp, ZFS_PROP_CANMOUNT) == ZFS_CANMOUNT_OFF)
		return (B_FALSE);

	if (zfs_prop_get_int(zhp, ZFS_PROP_ZONED) &&
	    getzoneid() == GLOBAL_ZONEID)
		return (B_FALSE);

	if (source)
		*source = sourcetype;

	return (B_TRUE);
}

/*
 * Mount the given filesystem.
 */
int
zfs_mount(zfs_handle_t *zhp, const char *options, int flags)
{
	struct stat buf;
	char mountpoint[ZFS_MAXPROPLEN];
	char mntopts[MNT_LINE_MAX];
	libzfs_handle_t *hdl = zhp->zfs_hdl;

	if (options == NULL)
		mntopts[0] = '\0';
	else
		(void) strlcpy(mntopts, options, sizeof (mntopts));

	/*
	 * If the pool is imported read-only then all mounts must be read-only
	 */
	if (zpool_get_prop_int(zhp->zpool_hdl, ZPOOL_PROP_READONLY, NULL))
		flags |= MS_RDONLY;

	if (!zfs_is_mountable(zhp, mountpoint, sizeof (mountpoint), NULL))
		return (0);

	/* Create the directory if it doesn't already exist */
	if (lstat(mountpoint, &buf) != 0) {
		if (mkdirp(mountpoint, 0755) != 0) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "failed to create mountpoint"));
			return (zfs_error_fmt(hdl, EZFS_MOUNTFAILED,
			    dgettext(TEXT_DOMAIN, "cannot mount '%s'"),
			    mountpoint));
		}
	}

#ifdef illumos	/* FreeBSD: overlay mounts are not checked. */
	/*
	 * Determine if the mountpoint is empty.  If so, refuse to perform the
	 * mount.  We don't perform this check if MS_OVERLAY is specified, which
	 * would defeat the point.  We also avoid this check if 'remount' is
	 * specified.
	 */
	if ((flags & MS_OVERLAY) == 0 &&
	    strstr(mntopts, MNTOPT_REMOUNT) == NULL &&
	    !dir_is_empty(mountpoint)) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "directory is not empty"));
		return (zfs_error_fmt(hdl, EZFS_MOUNTFAILED,
		    dgettext(TEXT_DOMAIN, "cannot mount '%s'"), mountpoint));
	}
#endif

	/* perform the mount */
	if (zmount(zfs_get_name(zhp), mountpoint, flags,
	    MNTTYPE_ZFS, NULL, 0, mntopts, sizeof (mntopts)) != 0) {
		/*
		 * Generic errors are nasty, but there are just way too many
		 * from mount(), and they're well-understood.  We pick a few
		 * common ones to improve upon.
		 */
		if (errno == EBUSY) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "mountpoint or dataset is busy"));
		} else if (errno == EPERM) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "Insufficient privileges"));
		} else if (errno == ENOTSUP) {
			char buf[256];
			int spa_version;

			VERIFY(zfs_spa_version(zhp, &spa_version) == 0);
			(void) snprintf(buf, sizeof (buf),
			    dgettext(TEXT_DOMAIN, "Can't mount a version %lld "
			    "file system on a version %d pool. Pool must be"
			    " upgraded to mount this file system."),
			    (u_longlong_t)zfs_prop_get_int(zhp,
			    ZFS_PROP_VERSION), spa_version);
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN, buf));
		} else {
			zfs_error_aux(hdl, strerror(errno));
		}
		return (zfs_error_fmt(hdl, EZFS_MOUNTFAILED,
		    dgettext(TEXT_DOMAIN, "cannot mount '%s'"),
		    zhp->zfs_name));
	}

	/* add the mounted entry into our cache */
	libzfs_mnttab_add(hdl, zfs_get_name(zhp), mountpoint,
	    mntopts);
	return (0);
}

/*
 * Unmount a single filesystem.
 */
static int
unmount_one(libzfs_handle_t *hdl, const char *mountpoint, int flags)
{
	if (umount2(mountpoint, flags) != 0) {
		zfs_error_aux(hdl, strerror(errno));
		return (zfs_error_fmt(hdl, EZFS_UMOUNTFAILED,
		    dgettext(TEXT_DOMAIN, "cannot unmount '%s'"),
		    mountpoint));
	}

	return (0);
}

/*
 * Unmount the given filesystem.
 */
int
zfs_unmount(zfs_handle_t *zhp, const char *mountpoint, int flags)
{
	libzfs_handle_t *hdl = zhp->zfs_hdl;
	struct mnttab entry;
	char *mntpt = NULL;

	/* check to see if we need to unmount the filesystem */
	if (mountpoint != NULL || ((zfs_get_type(zhp) == ZFS_TYPE_FILESYSTEM) &&
	    libzfs_mnttab_find(hdl, zhp->zfs_name, &entry) == 0)) {
		/*
		 * mountpoint may have come from a call to
		 * getmnt/getmntany if it isn't NULL. If it is NULL,
		 * we know it comes from libzfs_mnttab_find which can
		 * then get freed later. We strdup it to play it safe.
		 */
		if (mountpoint == NULL)
			mntpt = zfs_strdup(hdl, entry.mnt_mountp);
		else
			mntpt = zfs_strdup(hdl, mountpoint);

		/*
		 * Unshare and unmount the filesystem
		 */
		if (zfs_unshare_proto(zhp, mntpt, share_all_proto) != 0)
			return (-1);

		if (unmount_one(hdl, mntpt, flags) != 0) {
			free(mntpt);
			(void) zfs_shareall(zhp);
			return (-1);
		}
		libzfs_mnttab_remove(hdl, zhp->zfs_name);
		free(mntpt);
	}

	return (0);
}

/*
 * Unmount this filesystem and any children inheriting the mountpoint property.
 * To do this, just act like we're changing the mountpoint property, but don't
 * remount the filesystems afterwards.
 */
int
zfs_unmountall(zfs_handle_t *zhp, int flags)
{
	prop_changelist_t *clp;
	int ret;

	clp = changelist_gather(zhp, ZFS_PROP_MOUNTPOINT, 0, flags);
	if (clp == NULL)
		return (-1);

	ret = changelist_prefix(clp);
	changelist_free(clp);

	return (ret);
}

boolean_t
zfs_is_shared(zfs_handle_t *zhp)
{
	zfs_share_type_t rc = 0;
	zfs_share_proto_t *curr_proto;

	if (ZFS_IS_VOLUME(zhp))
		return (B_FALSE);

	for (curr_proto = share_all_proto; *curr_proto != PROTO_END;
	    curr_proto++)
		rc |= zfs_is_shared_proto(zhp, NULL, *curr_proto);

	return (rc ? B_TRUE : B_FALSE);
}

int
zfs_share(zfs_handle_t *zhp)
{
	assert(!ZFS_IS_VOLUME(zhp));
	return (zfs_share_proto(zhp, share_all_proto));
}

int
zfs_unshare(zfs_handle_t *zhp)
{
	assert(!ZFS_IS_VOLUME(zhp));
	return (zfs_unshareall(zhp));
}

/*
 * Check to see if the filesystem is currently shared.
 */
zfs_share_type_t
zfs_is_shared_proto(zfs_handle_t *zhp, char **where, zfs_share_proto_t proto)
{
	char *mountpoint;
	zfs_share_type_t rc;

	if (!zfs_is_mounted(zhp, &mountpoint))
		return (SHARED_NOT_SHARED);

	if ((rc = is_shared(zhp->zfs_hdl, mountpoint, proto))
	    != SHARED_NOT_SHARED) {
		if (where != NULL)
			*where = mountpoint;
		else
			free(mountpoint);
		return (rc);
	} else {
		free(mountpoint);
		return (SHARED_NOT_SHARED);
	}
}

boolean_t
zfs_is_shared_nfs(zfs_handle_t *zhp, char **where)
{
	return (zfs_is_shared_proto(zhp, where,
	    PROTO_NFS) != SHARED_NOT_SHARED);
}

boolean_t
zfs_is_shared_smb(zfs_handle_t *zhp, char **where)
{
	return (zfs_is_shared_proto(zhp, where,
	    PROTO_SMB) != SHARED_NOT_SHARED);
}

/*
 * Make sure things will work if libshare isn't installed by using
 * wrapper functions that check to see that the pointers to functions
 * initialized in _zfs_init_libshare() are actually present.
 */

#ifdef illumos
static sa_handle_t (*_sa_init)(int);
static sa_handle_t (*_sa_init_arg)(int, void *);
static void (*_sa_fini)(sa_handle_t);
static sa_share_t (*_sa_find_share)(sa_handle_t, char *);
static int (*_sa_enable_share)(sa_share_t, char *);
static int (*_sa_disable_share)(sa_share_t, char *);
static char *(*_sa_errorstr)(int);
static int (*_sa_parse_legacy_options)(sa_group_t, char *, char *);
static boolean_t (*_sa_needs_refresh)(sa_handle_t *);
static libzfs_handle_t *(*_sa_get_zfs_handle)(sa_handle_t);
static int (*_sa_zfs_process_share)(sa_handle_t, sa_group_t, sa_share_t,
    char *, char *, zprop_source_t, char *, char *, char *);
static void (*_sa_update_sharetab_ts)(sa_handle_t);
#endif

/*
 * _zfs_init_libshare()
 *
 * Find the libshare.so.1 entry points that we use here and save the
 * values to be used later. This is triggered by the runtime loader.
 * Make sure the correct ISA version is loaded.
 */

#pragma init(_zfs_init_libshare)
static void
_zfs_init_libshare(void)
{
#ifdef illumos
	void *libshare;
	char path[MAXPATHLEN];
	char isa[MAXISALEN];

#if defined(_LP64)
	if (sysinfo(SI_ARCHITECTURE_64, isa, MAXISALEN) == -1)
		isa[0] = '\0';
#else
	isa[0] = '\0';
#endif
	(void) snprintf(path, MAXPATHLEN,
	    "/usr/lib/%s/libshare.so.1", isa);

	if ((libshare = dlopen(path, RTLD_LAZY | RTLD_GLOBAL)) != NULL) {
		_sa_init = (sa_handle_t (*)(int))dlsym(libshare, "sa_init");
		_sa_init_arg = (sa_handle_t (*)(int, void *))dlsym(libshare,
		    "sa_init_arg");
		_sa_fini = (void (*)(sa_handle_t))dlsym(libshare, "sa_fini");
		_sa_find_share = (sa_share_t (*)(sa_handle_t, char *))
		    dlsym(libshare, "sa_find_share");
		_sa_enable_share = (int (*)(sa_share_t, char *))dlsym(libshare,
		    "sa_enable_share");
		_sa_disable_share = (int (*)(sa_share_t, char *))dlsym(libshare,
		    "sa_disable_share");
		_sa_errorstr = (char *(*)(int))dlsym(libshare, "sa_errorstr");
		_sa_parse_legacy_options = (int (*)(sa_group_t, char *, char *))
		    dlsym(libshare, "sa_parse_legacy_options");
		_sa_needs_refresh = (boolean_t (*)(sa_handle_t *))
		    dlsym(libshare, "sa_needs_refresh");
		_sa_get_zfs_handle = (libzfs_handle_t *(*)(sa_handle_t))
		    dlsym(libshare, "sa_get_zfs_handle");
		_sa_zfs_process_share = (int (*)(sa_handle_t, sa_group_t,
		    sa_share_t, char *, char *, zprop_source_t, char *,
		    char *, char *))dlsym(libshare, "sa_zfs_process_share");
		_sa_update_sharetab_ts = (void (*)(sa_handle_t))
		    dlsym(libshare, "sa_update_sharetab_ts");
		if (_sa_init == NULL || _sa_init_arg == NULL ||
		    _sa_fini == NULL || _sa_find_share == NULL ||
		    _sa_enable_share == NULL || _sa_disable_share == NULL ||
		    _sa_errorstr == NULL || _sa_parse_legacy_options == NULL ||
		    _sa_needs_refresh == NULL || _sa_get_zfs_handle == NULL ||
		    _sa_zfs_process_share == NULL ||
		    _sa_update_sharetab_ts == NULL) {
			_sa_init = NULL;
			_sa_init_arg = NULL;
			_sa_fini = NULL;
			_sa_disable_share = NULL;
			_sa_enable_share = NULL;
			_sa_errorstr = NULL;
			_sa_parse_legacy_options = NULL;
			(void) dlclose(libshare);
			_sa_needs_refresh = NULL;
			_sa_get_zfs_handle = NULL;
			_sa_zfs_process_share = NULL;
			_sa_update_sharetab_ts = NULL;
		}
	}
#endif
}

/*
 * zfs_init_libshare(zhandle, service)
 *
 * Initialize the libshare API if it hasn't already been initialized.
 * In all cases it returns 0 if it succeeded and an error if not. The
 * service value is which part(s) of the API to initialize and is a
 * direct map to the libshare sa_init(service) interface.
 */
static int
zfs_init_libshare_impl(libzfs_handle_t *zhandle, int service, void *arg)
{
#ifdef illumos
	/*
	 * libshare is either not installed or we're in a branded zone. The
	 * rest of the wrapper functions around the libshare calls already
	 * handle NULL function pointers, but we don't want the callers of
	 * zfs_init_libshare() to fail prematurely if libshare is not available.
	 */
	if (_sa_init == NULL)
		return (SA_OK);

	/*
	 * Attempt to refresh libshare. This is necessary if there was a cache
	 * miss for a new ZFS dataset that was just created, or if state of the
	 * sharetab file has changed since libshare was last initialized. We
	 * want to make sure so check timestamps to see if a different process
	 * has updated any of the configuration. If there was some non-ZFS
	 * change, we need to re-initialize the internal cache.
	 */
	if (_sa_needs_refresh != NULL &&
	    _sa_needs_refresh(zhandle->libzfs_sharehdl)) {
		zfs_uninit_libshare(zhandle);
		zhandle->libzfs_sharehdl = _sa_init_arg(service, arg);
	}

	if (zhandle && zhandle->libzfs_sharehdl == NULL)
		zhandle->libzfs_sharehdl = _sa_init_arg(service, arg);

	if (zhandle->libzfs_sharehdl == NULL)
		return (SA_NO_MEMORY);
#endif

	return (SA_OK);
}
int
zfs_init_libshare(libzfs_handle_t *zhandle, int service)
{
	return (zfs_init_libshare_impl(zhandle, service, NULL));
}

int
zfs_init_libshare_arg(libzfs_handle_t *zhandle, int service, void *arg)
{
	return (zfs_init_libshare_impl(zhandle, service, arg));
}


/*
 * zfs_uninit_libshare(zhandle)
 *
 * Uninitialize the libshare API if it hasn't already been
 * uninitialized. It is OK to call multiple times.
 */
void
zfs_uninit_libshare(libzfs_handle_t *zhandle)
{
	if (zhandle != NULL && zhandle->libzfs_sharehdl != NULL) {
#ifdef illumos
		if (_sa_fini != NULL)
			_sa_fini(zhandle->libzfs_sharehdl);
#endif
		zhandle->libzfs_sharehdl = NULL;
	}
}

/*
 * zfs_parse_options(options, proto)
 *
 * Call the legacy parse interface to get the protocol specific
 * options using the NULL arg to indicate that this is a "parse" only.
 */
int
zfs_parse_options(char *options, zfs_share_proto_t proto)
{
#ifdef illumos
	if (_sa_parse_legacy_options != NULL) {
		return (_sa_parse_legacy_options(NULL, options,
		    proto_table[proto].p_name));
	}
	return (SA_CONFIG_ERR);
#else
	return (SA_OK);
#endif
}

#ifdef illumos
/*
 * zfs_sa_find_share(handle, path)
 *
 * wrapper around sa_find_share to find a share path in the
 * configuration.
 */
static sa_share_t
zfs_sa_find_share(sa_handle_t handle, char *path)
{
	if (_sa_find_share != NULL)
		return (_sa_find_share(handle, path));
	return (NULL);
}

/*
 * zfs_sa_enable_share(share, proto)
 *
 * Wrapper for sa_enable_share which enables a share for a specified
 * protocol.
 */
static int
zfs_sa_enable_share(sa_share_t share, char *proto)
{
	if (_sa_enable_share != NULL)
		return (_sa_enable_share(share, proto));
	return (SA_CONFIG_ERR);
}

/*
 * zfs_sa_disable_share(share, proto)
 *
 * Wrapper for sa_enable_share which disables a share for a specified
 * protocol.
 */
static int
zfs_sa_disable_share(sa_share_t share, char *proto)
{
	if (_sa_disable_share != NULL)
		return (_sa_disable_share(share, proto));
	return (SA_CONFIG_ERR);
}
#endif	/* illumos */

/*
 * Share the given filesystem according to the options in the specified
 * protocol specific properties (sharenfs, sharesmb).  We rely
 * on "libshare" to the dirty work for us.
 */
static int
zfs_share_proto(zfs_handle_t *zhp, zfs_share_proto_t *proto)
{
	char mountpoint[ZFS_MAXPROPLEN];
	char shareopts[ZFS_MAXPROPLEN];
	char sourcestr[ZFS_MAXPROPLEN];
	libzfs_handle_t *hdl = zhp->zfs_hdl;
	zfs_share_proto_t *curr_proto;
	zprop_source_t sourcetype;
	int error, ret;

	if (!zfs_is_mountable(zhp, mountpoint, sizeof (mountpoint), NULL))
		return (0);

	for (curr_proto = proto; *curr_proto != PROTO_END; curr_proto++) {
		/*
		 * Return success if there are no share options.
		 */
		if (zfs_prop_get(zhp, proto_table[*curr_proto].p_prop,
		    shareopts, sizeof (shareopts), &sourcetype, sourcestr,
		    ZFS_MAXPROPLEN, B_FALSE) != 0 ||
		    strcmp(shareopts, "off") == 0)
			continue;
#ifdef illumos
		ret = zfs_init_libshare_arg(hdl, SA_INIT_ONE_SHARE_FROM_HANDLE,
		    zhp);
		if (ret != SA_OK) {
			(void) zfs_error_fmt(hdl, EZFS_SHARENFSFAILED,
			    dgettext(TEXT_DOMAIN, "cannot share '%s': %s"),
			    zfs_get_name(zhp), _sa_errorstr != NULL ?
			    _sa_errorstr(ret) : "");
			return (-1);
		}
#endif

		/*
		 * If the 'zoned' property is set, then zfs_is_mountable()
		 * will have already bailed out if we are in the global zone.
		 * But local zones cannot be NFS servers, so we ignore it for
		 * local zones as well.
		 */
		if (zfs_prop_get_int(zhp, ZFS_PROP_ZONED))
			continue;

#ifdef illumos
		share = zfs_sa_find_share(hdl->libzfs_sharehdl, mountpoint);
		if (share == NULL) {
			/*
			 * This may be a new file system that was just
			 * created so isn't in the internal cache
			 * (second time through). Rather than
			 * reloading the entire configuration, we can
			 * assume ZFS has done the checking and it is
			 * safe to add this to the internal
			 * configuration.
			 */
			if (_sa_zfs_process_share(hdl->libzfs_sharehdl,
			    NULL, NULL, mountpoint,
			    proto_table[*curr_proto].p_name, sourcetype,
			    shareopts, sourcestr, zhp->zfs_name) != SA_OK) {
				(void) zfs_error_fmt(hdl,
				    proto_table[*curr_proto].p_share_err,
				    dgettext(TEXT_DOMAIN, "cannot share '%s'"),
				    zfs_get_name(zhp));
				return (-1);
			}
			share = zfs_sa_find_share(hdl->libzfs_sharehdl,
			    mountpoint);
		}
		if (share != NULL) {
			int err;
			err = zfs_sa_enable_share(share,
			    proto_table[*curr_proto].p_name);
			if (err != SA_OK) {
				(void) zfs_error_fmt(hdl,
				    proto_table[*curr_proto].p_share_err,
				    dgettext(TEXT_DOMAIN, "cannot share '%s'"),
				    zfs_get_name(zhp));
				return (-1);
			}
		} else
#else
		if (*curr_proto != PROTO_NFS) {
			fprintf(stderr, "Unsupported share protocol: %d.\n",
			    *curr_proto);
			continue;
		}

		if (strcmp(shareopts, "on") == 0)
			error = fsshare(ZFS_EXPORTS_PATH, mountpoint, "");
		else
			error = fsshare(ZFS_EXPORTS_PATH, mountpoint, shareopts);
		if (error != 0)
#endif
		{
			(void) zfs_error_fmt(hdl,
			    proto_table[*curr_proto].p_share_err,
			    dgettext(TEXT_DOMAIN, "cannot share '%s'"),
			    zfs_get_name(zhp));
			return (-1);
		}

	}
	return (0);
}


int
zfs_share_nfs(zfs_handle_t *zhp)
{
	return (zfs_share_proto(zhp, nfs_only));
}

int
zfs_share_smb(zfs_handle_t *zhp)
{
	return (zfs_share_proto(zhp, smb_only));
}

int
zfs_shareall(zfs_handle_t *zhp)
{
	return (zfs_share_proto(zhp, share_all_proto));
}

/*
 * Unshare a filesystem by mountpoint.
 */
static int
unshare_one(libzfs_handle_t *hdl, const char *name, const char *mountpoint,
    zfs_share_proto_t proto)
{
#ifdef illumos
	sa_share_t share;
	int err;
	char *mntpt;

	/*
	 * Mountpoint could get trashed if libshare calls getmntany
	 * which it does during API initialization, so strdup the
	 * value.
	 */
	mntpt = zfs_strdup(hdl, mountpoint);

	/*
	 * make sure libshare initialized, initialize everything because we
	 * don't know what other unsharing may happen later. Functions up the
	 * stack are allowed to initialize instead a subset of shares at the
	 * time the set is known.
	 */
	if ((err = zfs_init_libshare_arg(hdl, SA_INIT_ONE_SHARE_FROM_NAME,
	    (void *)name)) != SA_OK) {
		free(mntpt);	/* don't need the copy anymore */
		return (zfs_error_fmt(hdl, proto_table[proto].p_unshare_err,
		    dgettext(TEXT_DOMAIN, "cannot unshare '%s': %s"),
		    name, _sa_errorstr(err)));
	}

	share = zfs_sa_find_share(hdl->libzfs_sharehdl, mntpt);
	free(mntpt);	/* don't need the copy anymore */

	if (share != NULL) {
		err = zfs_sa_disable_share(share, proto_table[proto].p_name);
		if (err != SA_OK) {
			return (zfs_error_fmt(hdl,
			    proto_table[proto].p_unshare_err,
			    dgettext(TEXT_DOMAIN, "cannot unshare '%s': %s"),
			    name, _sa_errorstr(err)));
		}
	} else {
		return (zfs_error_fmt(hdl, proto_table[proto].p_unshare_err,
		    dgettext(TEXT_DOMAIN, "cannot unshare '%s': not found"),
		    name));
	}
#else
	char buf[MAXPATHLEN];
	FILE *fp;
	int err;

	if (proto != PROTO_NFS) {
		fprintf(stderr, "No SMB support in FreeBSD yet.\n");
		return (EOPNOTSUPP);
	}

	err = fsunshare(ZFS_EXPORTS_PATH, mountpoint);
	if (err != 0) {
		zfs_error_aux(hdl, "%s", strerror(err));
		return (zfs_error_fmt(hdl, EZFS_UNSHARENFSFAILED,
		    dgettext(TEXT_DOMAIN,
		    "cannot unshare '%s'"), name));
	}
#endif
	return (0);
}

/*
 * Unshare the given filesystem.
 */
int
zfs_unshare_proto(zfs_handle_t *zhp, const char *mountpoint,
    zfs_share_proto_t *proto)
{
	libzfs_handle_t *hdl = zhp->zfs_hdl;
	struct mnttab entry;
	char *mntpt = NULL;

	/* check to see if need to unmount the filesystem */
	rewind(zhp->zfs_hdl->libzfs_mnttab);
	if (mountpoint != NULL)
		mountpoint = mntpt = zfs_strdup(hdl, mountpoint);

	if (mountpoint != NULL || ((zfs_get_type(zhp) == ZFS_TYPE_FILESYSTEM) &&
	    libzfs_mnttab_find(hdl, zfs_get_name(zhp), &entry) == 0)) {
		zfs_share_proto_t *curr_proto;

		if (mountpoint == NULL)
			mntpt = zfs_strdup(zhp->zfs_hdl, entry.mnt_mountp);

		for (curr_proto = proto; *curr_proto != PROTO_END;
		    curr_proto++) {

			if (is_shared(hdl, mntpt, *curr_proto) &&
			    unshare_one(hdl, zhp->zfs_name,
			    mntpt, *curr_proto) != 0) {
				if (mntpt != NULL)
					free(mntpt);
				return (-1);
			}
		}
	}
	if (mntpt != NULL)
		free(mntpt);

	return (0);
}

int
zfs_unshare_nfs(zfs_handle_t *zhp, const char *mountpoint)
{
	return (zfs_unshare_proto(zhp, mountpoint, nfs_only));
}

int
zfs_unshare_smb(zfs_handle_t *zhp, const char *mountpoint)
{
	return (zfs_unshare_proto(zhp, mountpoint, smb_only));
}

/*
 * Same as zfs_unmountall(), but for NFS and SMB unshares.
 */
int
zfs_unshareall_proto(zfs_handle_t *zhp, zfs_share_proto_t *proto)
{
	prop_changelist_t *clp;
	int ret;

	clp = changelist_gather(zhp, ZFS_PROP_SHARENFS, 0, 0);
	if (clp == NULL)
		return (-1);

	ret = changelist_unshare(clp, proto);
	changelist_free(clp);

	return (ret);
}

int
zfs_unshareall_nfs(zfs_handle_t *zhp)
{
	return (zfs_unshareall_proto(zhp, nfs_only));
}

int
zfs_unshareall_smb(zfs_handle_t *zhp)
{
	return (zfs_unshareall_proto(zhp, smb_only));
}

int
zfs_unshareall(zfs_handle_t *zhp)
{
	return (zfs_unshareall_proto(zhp, share_all_proto));
}

int
zfs_unshareall_bypath(zfs_handle_t *zhp, const char *mountpoint)
{
	return (zfs_unshare_proto(zhp, mountpoint, share_all_proto));
}

/*
 * Remove the mountpoint associated with the current dataset, if necessary.
 * We only remove the underlying directory if:
 *
 *	- The mountpoint is not 'none' or 'legacy'
 *	- The mountpoint is non-empty
 *	- The mountpoint is the default or inherited
 *	- The 'zoned' property is set, or we're in a local zone
 *
 * Any other directories we leave alone.
 */
void
remove_mountpoint(zfs_handle_t *zhp)
{
	char mountpoint[ZFS_MAXPROPLEN];
	zprop_source_t source;

	if (!zfs_is_mountable(zhp, mountpoint, sizeof (mountpoint),
	    &source))
		return;

	if (source == ZPROP_SRC_DEFAULT ||
	    source == ZPROP_SRC_INHERITED) {
		/*
		 * Try to remove the directory, silently ignoring any errors.
		 * The filesystem may have since been removed or moved around,
		 * and this error isn't really useful to the administrator in
		 * any way.
		 */
		(void) rmdir(mountpoint);
	}
}

/*
 * Add the given zfs handle to the cb_handles array, dynamically reallocating
 * the array if it is out of space
 */
void
libzfs_add_handle(get_all_cb_t *cbp, zfs_handle_t *zhp)
{
	if (cbp->cb_alloc == cbp->cb_used) {
		size_t newsz;
		zfs_handle_t **newhandles;

		newsz = cbp->cb_alloc != 0 ? cbp->cb_alloc * 2 : 64;
		newhandles = zfs_realloc(zhp->zfs_hdl,
		    cbp->cb_handles, cbp->cb_alloc * sizeof (zfs_handle_t *),
		    newsz * sizeof (zfs_handle_t *));
		cbp->cb_handles = newhandles;
		cbp->cb_alloc = newsz;
	}
	cbp->cb_handles[cbp->cb_used++] = zhp;
}

/*
 * Recursive helper function used during file system enumeration
 */
static int
zfs_iter_cb(zfs_handle_t *zhp, void *data)
{
	get_all_cb_t *cbp = data;

	if (!(zfs_get_type(zhp) & ZFS_TYPE_FILESYSTEM)) {
		zfs_close(zhp);
		return (0);
	}

	if (zfs_prop_get_int(zhp, ZFS_PROP_CANMOUNT) == ZFS_CANMOUNT_NOAUTO) {
		zfs_close(zhp);
		return (0);
	}

	/*
	 * If this filesystem is inconsistent and has a receive resume
	 * token, we can not mount it.
	 */
	if (zfs_prop_get_int(zhp, ZFS_PROP_INCONSISTENT) &&
	    zfs_prop_get(zhp, ZFS_PROP_RECEIVE_RESUME_TOKEN,
	    NULL, 0, NULL, NULL, 0, B_TRUE) == 0) {
		zfs_close(zhp);
		return (0);
	}

	libzfs_add_handle(cbp, zhp);
	if (zfs_iter_filesystems(zhp, zfs_iter_cb, cbp) != 0) {
		zfs_close(zhp);
		return (-1);
	}
	return (0);
}

/*
 * Sort comparator that compares two mountpoint paths. We sort these paths so
 * that subdirectories immediately follow their parents. This means that we
 * effectively treat the '/' character as the lowest value non-nul char.
 * Since filesystems from non-global zones can have the same mountpoint
 * as other filesystems, the comparator sorts global zone filesystems to
 * the top of the list. This means that the global zone will traverse the
 * filesystem list in the correct order and can stop when it sees the
 * first zoned filesystem. In a non-global zone, only the delegated
 * filesystems are seen.
 *
 * An example sorted list using this comparator would look like:
 *
 * /foo
 * /foo/bar
 * /foo/bar/baz
 * /foo/baz
 * /foo.bar
 * /foo (NGZ1)
 * /foo (NGZ2)
 *
 * The mount code depend on this ordering to deterministically iterate
 * over filesystems in order to spawn parallel mount tasks.
 */
static int
mountpoint_cmp(const void *arga, const void *argb)
{
	zfs_handle_t *const *zap = arga;
	zfs_handle_t *za = *zap;
	zfs_handle_t *const *zbp = argb;
	zfs_handle_t *zb = *zbp;
	char mounta[MAXPATHLEN];
	char mountb[MAXPATHLEN];
	const char *a = mounta;
	const char *b = mountb;
	boolean_t gota, gotb;
	uint64_t zoneda, zonedb;

	zoneda = zfs_prop_get_int(za, ZFS_PROP_ZONED);
	zonedb = zfs_prop_get_int(zb, ZFS_PROP_ZONED);
	if (zoneda && !zonedb)
		return (1);
	if (!zoneda && zonedb)
		return (-1);
	gota = (zfs_get_type(za) == ZFS_TYPE_FILESYSTEM);
	if (gota)
		verify(zfs_prop_get(za, ZFS_PROP_MOUNTPOINT, mounta,
		    sizeof (mounta), NULL, NULL, 0, B_FALSE) == 0);
	gotb = (zfs_get_type(zb) == ZFS_TYPE_FILESYSTEM);
	if (gotb)
		verify(zfs_prop_get(zb, ZFS_PROP_MOUNTPOINT, mountb,
		    sizeof (mountb), NULL, NULL, 0, B_FALSE) == 0);

	if (gota && gotb) {
		while (*a != '\0' && (*a == *b)) {
			a++;
			b++;
		}
		if (*a == *b)
			return (0);
		if (*a == '\0')
			return (-1);
		if (*b == '\0')
			return (1);
		if (*a == '/')
			return (-1);
		if (*b == '/')
			return (1);
		return (*a < *b ? -1 : *a > *b);
	}

	if (gota)
		return (-1);
	if (gotb)
		return (1);

	/*
	 * If neither filesystem has a mountpoint, revert to sorting by
	 * datset name.
	 */
	return (strcmp(zfs_get_name(za), zfs_get_name(zb)));
}

/*
 * Reutrn true if path2 is a child of path1
 */
static boolean_t
libzfs_path_contains(const char *path1, const char *path2)
{
	return (strstr(path2, path1) == path2 && path2[strlen(path1)] == '/');
}


static int
non_descendant_idx(zfs_handle_t **handles, size_t num_handles, int idx)
{
	char parent[ZFS_MAXPROPLEN];
	char child[ZFS_MAXPROPLEN];
	int i;

	verify(zfs_prop_get(handles[idx], ZFS_PROP_MOUNTPOINT, parent,
	    sizeof (parent), NULL, NULL, 0, B_FALSE) == 0);

	for (i = idx + 1; i < num_handles; i++) {
		verify(zfs_prop_get(handles[i], ZFS_PROP_MOUNTPOINT, child,
		    sizeof (child), NULL, NULL, 0, B_FALSE) == 0);
		if (!libzfs_path_contains(parent, child))
			break;
	}
	return (i);
}

typedef struct mnt_param {
	libzfs_handle_t	*mnt_hdl;
	tpool_t		*mnt_tp;
	zfs_handle_t	**mnt_zhps; /* filesystems to mount */
	size_t		mnt_num_handles;
	int		mnt_idx;	/* Index of selected entry to mount */
	zfs_iter_f	mnt_func;
	void		*mnt_data;
} mnt_param_t;

/*
 * Allocate and populate the parameter struct for mount function, and
 * schedule mounting of the entry selected by idx.
 */
static void
zfs_dispatch_mount(libzfs_handle_t *hdl, zfs_handle_t **handles,
    size_t num_handles, int idx, zfs_iter_f func, void *data, tpool_t *tp)
{
	mnt_param_t *mnt_param = zfs_alloc(hdl, sizeof (mnt_param_t));

	mnt_param->mnt_hdl = hdl;
	mnt_param->mnt_tp = tp;
	mnt_param->mnt_zhps = handles;
	mnt_param->mnt_num_handles = num_handles;
	mnt_param->mnt_idx = idx;
	mnt_param->mnt_func = func;
	mnt_param->mnt_data = data;

	(void) tpool_dispatch(tp, zfs_mount_task, (void*)mnt_param);
}

/*
 * This is the structure used to keep state of mounting or sharing operations
 * during a call to zpool_enable_datasets().
 */
typedef struct mount_state {
	/*
	 * ms_mntstatus is set to -1 if any mount fails. While multiple threads
	 * could update this variable concurrently, no synchronization is
	 * needed as it's only ever set to -1.
	 */
	int		ms_mntstatus;
	int		ms_mntflags;
	const char	*ms_mntopts;
} mount_state_t;

static int
zfs_mount_one(zfs_handle_t *zhp, void *arg)
{
	mount_state_t *ms = arg;
	int ret = 0;

	if (zfs_mount(zhp, ms->ms_mntopts, ms->ms_mntflags) != 0)
		ret = ms->ms_mntstatus = -1;
	return (ret);
}

static int
zfs_share_one(zfs_handle_t *zhp, void *arg)
{
	mount_state_t *ms = arg;
	int ret = 0;

	if (zfs_share(zhp) != 0)
		ret = ms->ms_mntstatus = -1;
	return (ret);
}

/*
 * Thread pool function to mount one file system. On completion, it finds and
 * schedules its children to be mounted. This depends on the sorting done in
 * zfs_foreach_mountpoint(). Note that the degenerate case (chain of entries
 * each descending from the previous) will have no parallelism since we always
 * have to wait for the parent to finish mounting before we can schedule
 * its children.
 */
static void
zfs_mount_task(void *arg)
{
	mnt_param_t *mp = arg;
	int idx = mp->mnt_idx;
	zfs_handle_t **handles = mp->mnt_zhps;
	size_t num_handles = mp->mnt_num_handles;
	char mountpoint[ZFS_MAXPROPLEN];

	verify(zfs_prop_get(handles[idx], ZFS_PROP_MOUNTPOINT, mountpoint,
	    sizeof (mountpoint), NULL, NULL, 0, B_FALSE) == 0);

	if (mp->mnt_func(handles[idx], mp->mnt_data) != 0)
		return;

	/*
	 * We dispatch tasks to mount filesystems with mountpoints underneath
	 * this one. We do this by dispatching the next filesystem with a
	 * descendant mountpoint of the one we just mounted, then skip all of
	 * its descendants, dispatch the next descendant mountpoint, and so on.
	 * The non_descendant_idx() function skips over filesystems that are
	 * descendants of the filesystem we just dispatched.
	 */
	for (int i = idx + 1; i < num_handles;
	    i = non_descendant_idx(handles, num_handles, i)) {
		char child[ZFS_MAXPROPLEN];
		verify(zfs_prop_get(handles[i], ZFS_PROP_MOUNTPOINT,
		    child, sizeof (child), NULL, NULL, 0, B_FALSE) == 0);

		if (!libzfs_path_contains(mountpoint, child))
			break; /* not a descendant, return */
		zfs_dispatch_mount(mp->mnt_hdl, handles, num_handles, i,
		    mp->mnt_func, mp->mnt_data, mp->mnt_tp);
	}
	free(mp);
}

/*
 * Issue the func callback for each ZFS handle contained in the handles
 * array. This function is used to mount all datasets, and so this function
 * guarantees that filesystems for parent mountpoints are called before their
 * children. As such, before issuing any callbacks, we first sort the array
 * of handles by mountpoint.
 *
 * Callbacks are issued in one of two ways:
 *
 * 1. Sequentially: If the parallel argument is B_FALSE or the ZFS_SERIAL_MOUNT
 *    environment variable is set, then we issue callbacks sequentially.
 *
 * 2. In parallel: If the parallel argument is B_TRUE and the ZFS_SERIAL_MOUNT
 *    environment variable is not set, then we use a tpool to dispatch threads
 *    to mount filesystems in parallel. This function dispatches tasks to mount
 *    the filesystems at the top-level mountpoints, and these tasks in turn
 *    are responsible for recursively mounting filesystems in their children
 *    mountpoints.
 */
void
zfs_foreach_mountpoint(libzfs_handle_t *hdl, zfs_handle_t **handles,
    size_t num_handles, zfs_iter_f func, void *data, boolean_t parallel)
{
	zoneid_t zoneid = getzoneid();

	/*
	 * The ZFS_SERIAL_MOUNT environment variable is an undocumented
	 * variable that can be used as a convenience to do a/b comparison
	 * of serial vs. parallel mounting.
	 */
	boolean_t serial_mount = !parallel ||
	    (getenv("ZFS_SERIAL_MOUNT") != NULL);

	/*
	 * Sort the datasets by mountpoint. See mountpoint_cmp for details
	 * of how these are sorted.
	 */
	qsort(handles, num_handles, sizeof (zfs_handle_t *), mountpoint_cmp);

	if (serial_mount) {
		for (int i = 0; i < num_handles; i++) {
			func(handles[i], data);
		}
		return;
	}

	/*
	 * Issue the callback function for each dataset using a parallel
	 * algorithm that uses a thread pool to manage threads.
	 */
	tpool_t *tp = tpool_create(1, mount_tp_nthr, 0, NULL);

	/*
	 * There may be multiple "top level" mountpoints outside of the pool's
	 * root mountpoint, e.g.: /foo /bar. Dispatch a mount task for each of
	 * these.
	 */
	for (int i = 0; i < num_handles;
	    i = non_descendant_idx(handles, num_handles, i)) {
		/*
		 * Since the mountpoints have been sorted so that the zoned
		 * filesystems are at the end, a zoned filesystem seen from
		 * the global zone means that we're done.
		 */
		if (zoneid == GLOBAL_ZONEID &&
		    zfs_prop_get_int(handles[i], ZFS_PROP_ZONED))
			break;
		zfs_dispatch_mount(hdl, handles, num_handles, i, func, data,
		    tp);
	}

	tpool_wait(tp);	/* wait for all scheduled mounts to complete */
	tpool_destroy(tp);
}

/*
 * Mount and share all datasets within the given pool.  This assumes that no
 * datasets within the pool are currently mounted.
 */
#pragma weak zpool_mount_datasets = zpool_enable_datasets
int
zpool_enable_datasets(zpool_handle_t *zhp, const char *mntopts, int flags)
{
	get_all_cb_t cb = { 0 };
	mount_state_t ms = { 0 };
	zfs_handle_t *zfsp;
	int ret = 0;

	if ((zfsp = zfs_open(zhp->zpool_hdl, zhp->zpool_name,
	    ZFS_TYPE_DATASET)) == NULL)
		goto out;

	/*
	 * Gather all non-snapshot datasets within the pool. Start by adding
	 * the root filesystem for this pool to the list, and then iterate
	 * over all child filesystems.
	 */
	libzfs_add_handle(&cb, zfsp);
	if (zfs_iter_filesystems(zfsp, zfs_iter_cb, &cb) != 0)
		goto out;

	/*
	 * Mount all filesystems
	 */
	ms.ms_mntopts = mntopts;
	ms.ms_mntflags = flags;
	zfs_foreach_mountpoint(zhp->zpool_hdl, cb.cb_handles, cb.cb_used,
	    zfs_mount_one, &ms, B_TRUE);
	if (ms.ms_mntstatus != 0)
		ret = ms.ms_mntstatus;

	/*
	 * Share all filesystems that need to be shared. This needs to be
	 * a separate pass because libshare is not mt-safe, and so we need
	 * to share serially.
	 */
	ms.ms_mntstatus = 0;
	zfs_foreach_mountpoint(zhp->zpool_hdl, cb.cb_handles, cb.cb_used,
	    zfs_share_one, &ms, B_FALSE);
	if (ms.ms_mntstatus != 0)
		ret = ms.ms_mntstatus;

out:
	for (int i = 0; i < cb.cb_used; i++)
		zfs_close(cb.cb_handles[i]);
	free(cb.cb_handles);

	return (ret);
}

static int
mountpoint_compare(const void *a, const void *b)
{
	const char *mounta = *((char **)a);
	const char *mountb = *((char **)b);

	return (strcmp(mountb, mounta));
}

/* alias for 2002/240 */
#pragma weak zpool_unmount_datasets = zpool_disable_datasets
/*
 * Unshare and unmount all datasets within the given pool.  We don't want to
 * rely on traversing the DSL to discover the filesystems within the pool,
 * because this may be expensive (if not all of them are mounted), and can fail
 * arbitrarily (on I/O error, for example).  Instead, we walk /etc/mnttab and
 * gather all the filesystems that are currently mounted.
 */
int
zpool_disable_datasets(zpool_handle_t *zhp, boolean_t force)
{
	int used, alloc;
	struct mnttab entry;
	size_t namelen;
	char **mountpoints = NULL;
	zfs_handle_t **datasets = NULL;
	libzfs_handle_t *hdl = zhp->zpool_hdl;
	int i;
	int ret = -1;
	int flags = (force ? MS_FORCE : 0);
#ifdef illumos
	sa_init_selective_arg_t sharearg;
#endif

	namelen = strlen(zhp->zpool_name);

	rewind(hdl->libzfs_mnttab);
	used = alloc = 0;
	while (getmntent(hdl->libzfs_mnttab, &entry) == 0) {
		/*
		 * Ignore non-ZFS entries.
		 */
		if (entry.mnt_fstype == NULL ||
		    strcmp(entry.mnt_fstype, MNTTYPE_ZFS) != 0)
			continue;

		/*
		 * Ignore filesystems not within this pool.
		 */
		if (entry.mnt_mountp == NULL ||
		    strncmp(entry.mnt_special, zhp->zpool_name, namelen) != 0 ||
		    (entry.mnt_special[namelen] != '/' &&
		    entry.mnt_special[namelen] != '\0'))
			continue;

		/*
		 * At this point we've found a filesystem within our pool.  Add
		 * it to our growing list.
		 */
		if (used == alloc) {
			if (alloc == 0) {
				if ((mountpoints = zfs_alloc(hdl,
				    8 * sizeof (void *))) == NULL)
					goto out;

				if ((datasets = zfs_alloc(hdl,
				    8 * sizeof (void *))) == NULL)
					goto out;

				alloc = 8;
			} else {
				void *ptr;

				if ((ptr = zfs_realloc(hdl, mountpoints,
				    alloc * sizeof (void *),
				    alloc * 2 * sizeof (void *))) == NULL)
					goto out;
				mountpoints = ptr;

				if ((ptr = zfs_realloc(hdl, datasets,
				    alloc * sizeof (void *),
				    alloc * 2 * sizeof (void *))) == NULL)
					goto out;
				datasets = ptr;

				alloc *= 2;
			}
		}

		if ((mountpoints[used] = zfs_strdup(hdl,
		    entry.mnt_mountp)) == NULL)
			goto out;

		/*
		 * This is allowed to fail, in case there is some I/O error.  It
		 * is only used to determine if we need to remove the underlying
		 * mountpoint, so failure is not fatal.
		 */
		datasets[used] = make_dataset_handle(hdl, entry.mnt_special);

		used++;
	}

	/*
	 * At this point, we have the entire list of filesystems, so sort it by
	 * mountpoint.
	 */
#ifdef illumos
	sharearg.zhandle_arr = datasets;
	sharearg.zhandle_len = used;
	ret = zfs_init_libshare_arg(hdl, SA_INIT_SHARE_API_SELECTIVE,
	    &sharearg);
	if (ret != 0)
		goto out;
#endif
	qsort(mountpoints, used, sizeof (char *), mountpoint_compare);

	/*
	 * Walk through and first unshare everything.
	 */
	for (i = 0; i < used; i++) {
		zfs_share_proto_t *curr_proto;
		for (curr_proto = share_all_proto; *curr_proto != PROTO_END;
		    curr_proto++) {
			if (is_shared(hdl, mountpoints[i], *curr_proto) &&
			    unshare_one(hdl, mountpoints[i],
			    mountpoints[i], *curr_proto) != 0)
				goto out;
		}
	}

	/*
	 * Now unmount everything, removing the underlying directories as
	 * appropriate.
	 */
	for (i = 0; i < used; i++) {
		if (unmount_one(hdl, mountpoints[i], flags) != 0)
			goto out;
	}

	for (i = 0; i < used; i++) {
		if (datasets[i])
			remove_mountpoint(datasets[i]);
	}

	ret = 0;
out:
	for (i = 0; i < used; i++) {
		if (datasets[i])
			zfs_close(datasets[i]);
		free(mountpoints[i]);
	}
	free(datasets);
	free(mountpoints);

	return (ret);
}
