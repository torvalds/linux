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
 * Copyright (c) 2012, 2017 by Delphix. All rights reserved.
 * Copyright (c) 2013 Steven Hartland. All rights reserved.
 * Copyright (c) 2014 Integros [integros.com]
 * Copyright 2017 RackTop Systems.
 */

/*
 * LibZFS_Core (lzc) is intended to replace most functionality in libzfs.
 * It has the following characteristics:
 *
 *  - Thread Safe.  libzfs_core is accessible concurrently from multiple
 *  threads.  This is accomplished primarily by avoiding global data
 *  (e.g. caching).  Since it's thread-safe, there is no reason for a
 *  process to have multiple libzfs "instances".  Therefore, we store
 *  our few pieces of data (e.g. the file descriptor) in global
 *  variables.  The fd is reference-counted so that the libzfs_core
 *  library can be "initialized" multiple times (e.g. by different
 *  consumers within the same process).
 *
 *  - Committed Interface.  The libzfs_core interface will be committed,
 *  therefore consumers can compile against it and be confident that
 *  their code will continue to work on future releases of this code.
 *  Currently, the interface is Evolving (not Committed), but we intend
 *  to commit to it once it is more complete and we determine that it
 *  meets the needs of all consumers.
 *
 *  - Programatic Error Handling.  libzfs_core communicates errors with
 *  defined error numbers, and doesn't print anything to stdout/stderr.
 *
 *  - Thin Layer.  libzfs_core is a thin layer, marshaling arguments
 *  to/from the kernel ioctls.  There is generally a 1:1 correspondence
 *  between libzfs_core functions and ioctls to /dev/zfs.
 *
 *  - Clear Atomicity.  Because libzfs_core functions are generally 1:1
 *  with kernel ioctls, and kernel ioctls are general atomic, each
 *  libzfs_core function is atomic.  For example, creating multiple
 *  snapshots with a single call to lzc_snapshot() is atomic -- it
 *  can't fail with only some of the requested snapshots created, even
 *  in the event of power loss or system crash.
 *
 *  - Continued libzfs Support.  Some higher-level operations (e.g.
 *  support for "zfs send -R") are too complicated to fit the scope of
 *  libzfs_core.  This functionality will continue to live in libzfs.
 *  Where appropriate, libzfs will use the underlying atomic operations
 *  of libzfs_core.  For example, libzfs may implement "zfs send -R |
 *  zfs receive" by using individual "send one snapshot", rename,
 *  destroy, and "receive one snapshot" operations in libzfs_core.
 *  /sbin/zfs and /zbin/zpool will link with both libzfs and
 *  libzfs_core.  Other consumers should aim to use only libzfs_core,
 *  since that will be the supported, stable interface going forwards.
 */

#define _IN_LIBZFS_CORE_

#include <libzfs_core.h>
#include <ctype.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/nvpair.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/zfs_ioctl.h>
#include "libzfs_core_compat.h"
#include "libzfs_compat.h"

#ifdef __FreeBSD__
extern int zfs_ioctl_version;
#endif

static int g_fd = -1;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_refcount;

int
libzfs_core_init(void)
{
	(void) pthread_mutex_lock(&g_lock);
	if (g_refcount == 0) {
		g_fd = open("/dev/zfs", O_RDWR);
		if (g_fd < 0) {
			(void) pthread_mutex_unlock(&g_lock);
			return (errno);
		}
	}
	g_refcount++;
	(void) pthread_mutex_unlock(&g_lock);

	return (0);
}

void
libzfs_core_fini(void)
{
	(void) pthread_mutex_lock(&g_lock);
	ASSERT3S(g_refcount, >, 0);

	if (g_refcount > 0)
		g_refcount--;

	if (g_refcount == 0 && g_fd != -1) {
		(void) close(g_fd);
		g_fd = -1;
	}
	(void) pthread_mutex_unlock(&g_lock);
}

static int
lzc_ioctl(zfs_ioc_t ioc, const char *name,
    nvlist_t *source, nvlist_t **resultp)
{
	zfs_cmd_t zc = { 0 };
	int error = 0;
	char *packed;
#ifdef __FreeBSD__
	nvlist_t *oldsource;
#endif
	size_t size;

	ASSERT3S(g_refcount, >, 0);
	VERIFY3S(g_fd, !=, -1);

	(void) strlcpy(zc.zc_name, name, sizeof (zc.zc_name));

#ifdef __FreeBSD__
	if (zfs_ioctl_version == ZFS_IOCVER_UNDEF)
		zfs_ioctl_version = get_zfs_ioctl_version();

	if (zfs_ioctl_version < ZFS_IOCVER_LZC) {
		oldsource = source;
		error = lzc_compat_pre(&zc, &ioc, &source);
		if (error)
			return (error);
	}
#endif

	packed = fnvlist_pack(source, &size);
	zc.zc_nvlist_src = (uint64_t)(uintptr_t)packed;
	zc.zc_nvlist_src_size = size;

	if (resultp != NULL) {
		*resultp = NULL;
		if (ioc == ZFS_IOC_CHANNEL_PROGRAM) {
			zc.zc_nvlist_dst_size = fnvlist_lookup_uint64(source,
			    ZCP_ARG_MEMLIMIT);
		} else {
			zc.zc_nvlist_dst_size = MAX(size * 2, 128 * 1024);
		}
		zc.zc_nvlist_dst = (uint64_t)(uintptr_t)
		    malloc(zc.zc_nvlist_dst_size);
#ifdef illumos
		if (zc.zc_nvlist_dst == NULL) {
#else
		if (zc.zc_nvlist_dst == 0) {
#endif
			error = ENOMEM;
			goto out;
		}
	}

	while (ioctl(g_fd, ioc, &zc) != 0) {
		/*
		 * If ioctl exited with ENOMEM, we retry the ioctl after
		 * increasing the size of the destination nvlist.
		 *
		 * Channel programs that exit with ENOMEM ran over the
		 * lua memory sandbox; they should not be retried.
		 */
		if (errno == ENOMEM && resultp != NULL &&
		    ioc != ZFS_IOC_CHANNEL_PROGRAM) {
			free((void *)(uintptr_t)zc.zc_nvlist_dst);
			zc.zc_nvlist_dst_size *= 2;
			zc.zc_nvlist_dst = (uint64_t)(uintptr_t)
			    malloc(zc.zc_nvlist_dst_size);
#ifdef illumos
			if (zc.zc_nvlist_dst == NULL) {
#else
			if (zc.zc_nvlist_dst == 0) {
#endif
				error = ENOMEM;
				goto out;
			}
		} else {
			error = errno;
			break;
		}
	}

#ifdef __FreeBSD__
	if (zfs_ioctl_version < ZFS_IOCVER_LZC)
		lzc_compat_post(&zc, ioc);
#endif
	if (zc.zc_nvlist_dst_filled) {
		*resultp = fnvlist_unpack((void *)(uintptr_t)zc.zc_nvlist_dst,
		    zc.zc_nvlist_dst_size);
	}
#ifdef __FreeBSD__
	if (zfs_ioctl_version < ZFS_IOCVER_LZC)
		lzc_compat_outnvl(&zc, ioc, resultp);
#endif
out:
#ifdef __FreeBSD__
	if (zfs_ioctl_version < ZFS_IOCVER_LZC) {
		if (source != oldsource)
			nvlist_free(source);
		source = oldsource;
	}
#endif
	fnvlist_pack_free(packed, size);
	free((void *)(uintptr_t)zc.zc_nvlist_dst);
	return (error);
}

int
lzc_create(const char *fsname, enum lzc_dataset_type type, nvlist_t *props)
{
	int error;
	nvlist_t *args = fnvlist_alloc();
	fnvlist_add_int32(args, "type", (dmu_objset_type_t)type);
	if (props != NULL)
		fnvlist_add_nvlist(args, "props", props);
	error = lzc_ioctl(ZFS_IOC_CREATE, fsname, args, NULL);
	nvlist_free(args);
	return (error);
}

int
lzc_clone(const char *fsname, const char *origin,
    nvlist_t *props)
{
	int error;
	nvlist_t *args = fnvlist_alloc();
	fnvlist_add_string(args, "origin", origin);
	if (props != NULL)
		fnvlist_add_nvlist(args, "props", props);
	error = lzc_ioctl(ZFS_IOC_CLONE, fsname, args, NULL);
	nvlist_free(args);
	return (error);
}

int
lzc_promote(const char *fsname, char *snapnamebuf, int snapnamelen)
{
	/*
	 * The promote ioctl is still legacy, so we need to construct our
	 * own zfs_cmd_t rather than using lzc_ioctl().
	 */
	zfs_cmd_t zc = { 0 };

	ASSERT3S(g_refcount, >, 0);
	VERIFY3S(g_fd, !=, -1);

	(void) strlcpy(zc.zc_name, fsname, sizeof (zc.zc_name));
	if (ioctl(g_fd, ZFS_IOC_PROMOTE, &zc) != 0) {
		int error = errno;
		if (error == EEXIST && snapnamebuf != NULL)
			(void) strlcpy(snapnamebuf, zc.zc_string, snapnamelen);
		return (error);
	}
	return (0);
}

int
lzc_remap(const char *fsname)
{
	int error;
	nvlist_t *args = fnvlist_alloc();
	error = lzc_ioctl(ZFS_IOC_REMAP, fsname, args, NULL);
	nvlist_free(args);
	return (error);
}

int
lzc_rename(const char *source, const char *target)
{
	zfs_cmd_t zc = { 0 };
	int error;

	ASSERT3S(g_refcount, >, 0);
	VERIFY3S(g_fd, !=, -1);

	(void) strlcpy(zc.zc_name, source, sizeof (zc.zc_name));
	(void) strlcpy(zc.zc_value, target, sizeof (zc.zc_value));
	error = ioctl(g_fd, ZFS_IOC_RENAME, &zc);
	if (error != 0)
		error = errno;
	return (error);
}

int
lzc_destroy(const char *fsname)
{
	int error;

	nvlist_t *args = fnvlist_alloc();
	error = lzc_ioctl(ZFS_IOC_DESTROY, fsname, args, NULL);
	nvlist_free(args);
	return (error);
}

/*
 * Creates snapshots.
 *
 * The keys in the snaps nvlist are the snapshots to be created.
 * They must all be in the same pool.
 *
 * The props nvlist is properties to set.  Currently only user properties
 * are supported.  { user:prop_name -> string value }
 *
 * The returned results nvlist will have an entry for each snapshot that failed.
 * The value will be the (int32) error code.
 *
 * The return value will be 0 if all snapshots were created, otherwise it will
 * be the errno of a (unspecified) snapshot that failed.
 */
int
lzc_snapshot(nvlist_t *snaps, nvlist_t *props, nvlist_t **errlist)
{
	nvpair_t *elem;
	nvlist_t *args;
	int error;
	char pool[ZFS_MAX_DATASET_NAME_LEN];

	*errlist = NULL;

	/* determine the pool name */
	elem = nvlist_next_nvpair(snaps, NULL);
	if (elem == NULL)
		return (0);
	(void) strlcpy(pool, nvpair_name(elem), sizeof (pool));
	pool[strcspn(pool, "/@")] = '\0';

	args = fnvlist_alloc();
	fnvlist_add_nvlist(args, "snaps", snaps);
	if (props != NULL)
		fnvlist_add_nvlist(args, "props", props);

	error = lzc_ioctl(ZFS_IOC_SNAPSHOT, pool, args, errlist);
	nvlist_free(args);

	return (error);
}

/*
 * Destroys snapshots.
 *
 * The keys in the snaps nvlist are the snapshots to be destroyed.
 * They must all be in the same pool.
 *
 * Snapshots that do not exist will be silently ignored.
 *
 * If 'defer' is not set, and a snapshot has user holds or clones, the
 * destroy operation will fail and none of the snapshots will be
 * destroyed.
 *
 * If 'defer' is set, and a snapshot has user holds or clones, it will be
 * marked for deferred destruction, and will be destroyed when the last hold
 * or clone is removed/destroyed.
 *
 * The return value will be 0 if all snapshots were destroyed (or marked for
 * later destruction if 'defer' is set) or didn't exist to begin with.
 *
 * Otherwise the return value will be the errno of a (unspecified) snapshot
 * that failed, no snapshots will be destroyed, and the errlist will have an
 * entry for each snapshot that failed.  The value in the errlist will be
 * the (int32) error code.
 */
int
lzc_destroy_snaps(nvlist_t *snaps, boolean_t defer, nvlist_t **errlist)
{
	nvpair_t *elem;
	nvlist_t *args;
	int error;
	char pool[ZFS_MAX_DATASET_NAME_LEN];

	/* determine the pool name */
	elem = nvlist_next_nvpair(snaps, NULL);
	if (elem == NULL)
		return (0);
	(void) strlcpy(pool, nvpair_name(elem), sizeof (pool));
	pool[strcspn(pool, "/@")] = '\0';

	args = fnvlist_alloc();
	fnvlist_add_nvlist(args, "snaps", snaps);
	if (defer)
		fnvlist_add_boolean(args, "defer");

	error = lzc_ioctl(ZFS_IOC_DESTROY_SNAPS, pool, args, errlist);
	nvlist_free(args);

	return (error);
}

int
lzc_snaprange_space(const char *firstsnap, const char *lastsnap,
    uint64_t *usedp)
{
	nvlist_t *args;
	nvlist_t *result;
	int err;
	char fs[ZFS_MAX_DATASET_NAME_LEN];
	char *atp;

	/* determine the fs name */
	(void) strlcpy(fs, firstsnap, sizeof (fs));
	atp = strchr(fs, '@');
	if (atp == NULL)
		return (EINVAL);
	*atp = '\0';

	args = fnvlist_alloc();
	fnvlist_add_string(args, "firstsnap", firstsnap);

	err = lzc_ioctl(ZFS_IOC_SPACE_SNAPS, lastsnap, args, &result);
	nvlist_free(args);
	if (err == 0)
		*usedp = fnvlist_lookup_uint64(result, "used");
	fnvlist_free(result);

	return (err);
}

boolean_t
lzc_exists(const char *dataset)
{
	/*
	 * The objset_stats ioctl is still legacy, so we need to construct our
	 * own zfs_cmd_t rather than using lzc_ioctl().
	 */
	zfs_cmd_t zc = { 0 };

	ASSERT3S(g_refcount, >, 0);
	VERIFY3S(g_fd, !=, -1);

	(void) strlcpy(zc.zc_name, dataset, sizeof (zc.zc_name));
	return (ioctl(g_fd, ZFS_IOC_OBJSET_STATS, &zc) == 0);
}

/*
 * Create "user holds" on snapshots.  If there is a hold on a snapshot,
 * the snapshot can not be destroyed.  (However, it can be marked for deletion
 * by lzc_destroy_snaps(defer=B_TRUE).)
 *
 * The keys in the nvlist are snapshot names.
 * The snapshots must all be in the same pool.
 * The value is the name of the hold (string type).
 *
 * If cleanup_fd is not -1, it must be the result of open("/dev/zfs", O_EXCL).
 * In this case, when the cleanup_fd is closed (including on process
 * termination), the holds will be released.  If the system is shut down
 * uncleanly, the holds will be released when the pool is next opened
 * or imported.
 *
 * Holds for snapshots which don't exist will be skipped and have an entry
 * added to errlist, but will not cause an overall failure.
 *
 * The return value will be 0 if all holds, for snapshots that existed,
 * were succesfully created.
 *
 * Otherwise the return value will be the errno of a (unspecified) hold that
 * failed and no holds will be created.
 *
 * In all cases the errlist will have an entry for each hold that failed
 * (name = snapshot), with its value being the error code (int32).
 */
int
lzc_hold(nvlist_t *holds, int cleanup_fd, nvlist_t **errlist)
{
	char pool[ZFS_MAX_DATASET_NAME_LEN];
	nvlist_t *args;
	nvpair_t *elem;
	int error;

	/* determine the pool name */
	elem = nvlist_next_nvpair(holds, NULL);
	if (elem == NULL)
		return (0);
	(void) strlcpy(pool, nvpair_name(elem), sizeof (pool));
	pool[strcspn(pool, "/@")] = '\0';

	args = fnvlist_alloc();
	fnvlist_add_nvlist(args, "holds", holds);
	if (cleanup_fd != -1)
		fnvlist_add_int32(args, "cleanup_fd", cleanup_fd);

	error = lzc_ioctl(ZFS_IOC_HOLD, pool, args, errlist);
	nvlist_free(args);
	return (error);
}

/*
 * Release "user holds" on snapshots.  If the snapshot has been marked for
 * deferred destroy (by lzc_destroy_snaps(defer=B_TRUE)), it does not have
 * any clones, and all the user holds are removed, then the snapshot will be
 * destroyed.
 *
 * The keys in the nvlist are snapshot names.
 * The snapshots must all be in the same pool.
 * The value is a nvlist whose keys are the holds to remove.
 *
 * Holds which failed to release because they didn't exist will have an entry
 * added to errlist, but will not cause an overall failure.
 *
 * The return value will be 0 if the nvl holds was empty or all holds that
 * existed, were successfully removed.
 *
 * Otherwise the return value will be the errno of a (unspecified) hold that
 * failed to release and no holds will be released.
 *
 * In all cases the errlist will have an entry for each hold that failed to
 * to release.
 */
int
lzc_release(nvlist_t *holds, nvlist_t **errlist)
{
	char pool[ZFS_MAX_DATASET_NAME_LEN];
	nvpair_t *elem;

	/* determine the pool name */
	elem = nvlist_next_nvpair(holds, NULL);
	if (elem == NULL)
		return (0);
	(void) strlcpy(pool, nvpair_name(elem), sizeof (pool));
	pool[strcspn(pool, "/@")] = '\0';

	return (lzc_ioctl(ZFS_IOC_RELEASE, pool, holds, errlist));
}

/*
 * Retrieve list of user holds on the specified snapshot.
 *
 * On success, *holdsp will be set to a nvlist which the caller must free.
 * The keys are the names of the holds, and the value is the creation time
 * of the hold (uint64) in seconds since the epoch.
 */
int
lzc_get_holds(const char *snapname, nvlist_t **holdsp)
{
	int error;
	nvlist_t *innvl = fnvlist_alloc();
	error = lzc_ioctl(ZFS_IOC_GET_HOLDS, snapname, innvl, holdsp);
	fnvlist_free(innvl);
	return (error);
}

/*
 * Generate a zfs send stream for the specified snapshot and write it to
 * the specified file descriptor.
 *
 * "snapname" is the full name of the snapshot to send (e.g. "pool/fs@snap")
 *
 * If "from" is NULL, a full (non-incremental) stream will be sent.
 * If "from" is non-NULL, it must be the full name of a snapshot or
 * bookmark to send an incremental from (e.g. "pool/fs@earlier_snap" or
 * "pool/fs#earlier_bmark").  If non-NULL, the specified snapshot or
 * bookmark must represent an earlier point in the history of "snapname").
 * It can be an earlier snapshot in the same filesystem or zvol as "snapname",
 * or it can be the origin of "snapname"'s filesystem, or an earlier
 * snapshot in the origin, etc.
 *
 * "fd" is the file descriptor to write the send stream to.
 *
 * If "flags" contains LZC_SEND_FLAG_LARGE_BLOCK, the stream is permitted
 * to contain DRR_WRITE records with drr_length > 128K, and DRR_OBJECT
 * records with drr_blksz > 128K.
 *
 * If "flags" contains LZC_SEND_FLAG_EMBED_DATA, the stream is permitted
 * to contain DRR_WRITE_EMBEDDED records with drr_etype==BP_EMBEDDED_TYPE_DATA,
 * which the receiving system must support (as indicated by support
 * for the "embedded_data" feature).
 */
int
lzc_send(const char *snapname, const char *from, int fd,
    enum lzc_send_flags flags)
{
	return (lzc_send_resume(snapname, from, fd, flags, 0, 0));
}

int
lzc_send_resume(const char *snapname, const char *from, int fd,
    enum lzc_send_flags flags, uint64_t resumeobj, uint64_t resumeoff)
{
	nvlist_t *args;
	int err;

	args = fnvlist_alloc();
	fnvlist_add_int32(args, "fd", fd);
	if (from != NULL)
		fnvlist_add_string(args, "fromsnap", from);
	if (flags & LZC_SEND_FLAG_LARGE_BLOCK)
		fnvlist_add_boolean(args, "largeblockok");
	if (flags & LZC_SEND_FLAG_EMBED_DATA)
		fnvlist_add_boolean(args, "embedok");
	if (flags & LZC_SEND_FLAG_COMPRESS)
		fnvlist_add_boolean(args, "compressok");
	if (resumeobj != 0 || resumeoff != 0) {
		fnvlist_add_uint64(args, "resume_object", resumeobj);
		fnvlist_add_uint64(args, "resume_offset", resumeoff);
	}
	err = lzc_ioctl(ZFS_IOC_SEND_NEW, snapname, args, NULL);
	nvlist_free(args);
	return (err);
}

/*
 * "from" can be NULL, a snapshot, or a bookmark.
 *
 * If from is NULL, a full (non-incremental) stream will be estimated.  This
 * is calculated very efficiently.
 *
 * If from is a snapshot, lzc_send_space uses the deadlists attached to
 * each snapshot to efficiently estimate the stream size.
 *
 * If from is a bookmark, the indirect blocks in the destination snapshot
 * are traversed, looking for blocks with a birth time since the creation TXG of
 * the snapshot this bookmark was created from.  This will result in
 * significantly more I/O and be less efficient than a send space estimation on
 * an equivalent snapshot.
 */
int
lzc_send_space(const char *snapname, const char *from,
    enum lzc_send_flags flags, uint64_t *spacep)
{
	nvlist_t *args;
	nvlist_t *result;
	int err;

	args = fnvlist_alloc();
	if (from != NULL)
		fnvlist_add_string(args, "from", from);
	if (flags & LZC_SEND_FLAG_LARGE_BLOCK)
		fnvlist_add_boolean(args, "largeblockok");
	if (flags & LZC_SEND_FLAG_EMBED_DATA)
		fnvlist_add_boolean(args, "embedok");
	if (flags & LZC_SEND_FLAG_COMPRESS)
		fnvlist_add_boolean(args, "compressok");
	err = lzc_ioctl(ZFS_IOC_SEND_SPACE, snapname, args, &result);
	nvlist_free(args);
	if (err == 0)
		*spacep = fnvlist_lookup_uint64(result, "space");
	nvlist_free(result);
	return (err);
}

static int
recv_read(int fd, void *buf, int ilen)
{
	char *cp = buf;
	int rv;
	int len = ilen;

	do {
		rv = read(fd, cp, len);
		cp += rv;
		len -= rv;
	} while (rv > 0);

	if (rv < 0 || len != 0)
		return (EIO);

	return (0);
}

static int
recv_impl(const char *snapname, nvlist_t *props, const char *origin,
    boolean_t force, boolean_t resumable, int fd,
    const dmu_replay_record_t *begin_record)
{
	/*
	 * The receive ioctl is still legacy, so we need to construct our own
	 * zfs_cmd_t rather than using zfsc_ioctl().
	 */
	zfs_cmd_t zc = { 0 };
	char *atp;
	char *packed = NULL;
	size_t size;
	int error;

	ASSERT3S(g_refcount, >, 0);
	VERIFY3S(g_fd, !=, -1);

	/* zc_name is name of containing filesystem */
	(void) strlcpy(zc.zc_name, snapname, sizeof (zc.zc_name));
	atp = strchr(zc.zc_name, '@');
	if (atp == NULL)
		return (EINVAL);
	*atp = '\0';

	/* if the fs does not exist, try its parent. */
	if (!lzc_exists(zc.zc_name)) {
		char *slashp = strrchr(zc.zc_name, '/');
		if (slashp == NULL)
			return (ENOENT);
		*slashp = '\0';

	}

	/* zc_value is full name of the snapshot to create */
	(void) strlcpy(zc.zc_value, snapname, sizeof (zc.zc_value));

	if (props != NULL) {
		/* zc_nvlist_src is props to set */
		packed = fnvlist_pack(props, &size);
		zc.zc_nvlist_src = (uint64_t)(uintptr_t)packed;
		zc.zc_nvlist_src_size = size;
	}

	/* zc_string is name of clone origin (if DRR_FLAG_CLONE) */
	if (origin != NULL)
		(void) strlcpy(zc.zc_string, origin, sizeof (zc.zc_string));

	/* zc_begin_record is non-byteswapped BEGIN record */
	if (begin_record == NULL) {
		error = recv_read(fd, &zc.zc_begin_record,
		    sizeof (zc.zc_begin_record));
		if (error != 0)
			goto out;
	} else {
		zc.zc_begin_record = *begin_record;
	}

	/* zc_cookie is fd to read from */
	zc.zc_cookie = fd;

	/* zc guid is force flag */
	zc.zc_guid = force;

	zc.zc_resumable = resumable;

	/* zc_cleanup_fd is unused */
	zc.zc_cleanup_fd = -1;

	error = ioctl(g_fd, ZFS_IOC_RECV, &zc);
	if (error != 0)
		error = errno;

out:
	if (packed != NULL)
		fnvlist_pack_free(packed, size);
	free((void*)(uintptr_t)zc.zc_nvlist_dst);
	return (error);
}

/*
 * The simplest receive case: receive from the specified fd, creating the
 * specified snapshot.  Apply the specified properties as "received" properties
 * (which can be overridden by locally-set properties).  If the stream is a
 * clone, its origin snapshot must be specified by 'origin'.  The 'force'
 * flag will cause the target filesystem to be rolled back or destroyed if
 * necessary to receive.
 *
 * Return 0 on success or an errno on failure.
 *
 * Note: this interface does not work on dedup'd streams
 * (those with DMU_BACKUP_FEATURE_DEDUP).
 */
int
lzc_receive(const char *snapname, nvlist_t *props, const char *origin,
    boolean_t force, int fd)
{
	return (recv_impl(snapname, props, origin, force, B_FALSE, fd, NULL));
}

/*
 * Like lzc_receive, but if the receive fails due to premature stream
 * termination, the intermediate state will be preserved on disk.  In this
 * case, ECKSUM will be returned.  The receive may subsequently be resumed
 * with a resuming send stream generated by lzc_send_resume().
 */
int
lzc_receive_resumable(const char *snapname, nvlist_t *props, const char *origin,
    boolean_t force, int fd)
{
	return (recv_impl(snapname, props, origin, force, B_TRUE, fd, NULL));
}

/*
 * Like lzc_receive, but allows the caller to read the begin record and then to
 * pass it in.  That could be useful if the caller wants to derive, for example,
 * the snapname or the origin parameters based on the information contained in
 * the begin record.
 * The begin record must be in its original form as read from the stream,
 * in other words, it should not be byteswapped.
 *
 * The 'resumable' parameter allows to obtain the same behavior as with
 * lzc_receive_resumable.
 */
int
lzc_receive_with_header(const char *snapname, nvlist_t *props,
    const char *origin, boolean_t force, boolean_t resumable, int fd,
    const dmu_replay_record_t *begin_record)
{
	if (begin_record == NULL)
		return (EINVAL);
	return (recv_impl(snapname, props, origin, force, resumable, fd,
	    begin_record));
}

/*
 * Roll back this filesystem or volume to its most recent snapshot.
 * If snapnamebuf is not NULL, it will be filled in with the name
 * of the most recent snapshot.
 * Note that the latest snapshot may change if a new one is concurrently
 * created or the current one is destroyed.  lzc_rollback_to can be used
 * to roll back to a specific latest snapshot.
 *
 * Return 0 on success or an errno on failure.
 */
int
lzc_rollback(const char *fsname, char *snapnamebuf, int snapnamelen)
{
	nvlist_t *args;
	nvlist_t *result;
	int err;

	args = fnvlist_alloc();
	err = lzc_ioctl(ZFS_IOC_ROLLBACK, fsname, args, &result);
	nvlist_free(args);
	if (err == 0 && snapnamebuf != NULL) {
		const char *snapname = fnvlist_lookup_string(result, "target");
		(void) strlcpy(snapnamebuf, snapname, snapnamelen);
	}
	nvlist_free(result);

	return (err);
}

/*
 * Roll back this filesystem or volume to the specified snapshot,
 * if possible.
 *
 * Return 0 on success or an errno on failure.
 */
int
lzc_rollback_to(const char *fsname, const char *snapname)
{
	nvlist_t *args;
	nvlist_t *result;
	int err;

	args = fnvlist_alloc();
	fnvlist_add_string(args, "target", snapname);
	err = lzc_ioctl(ZFS_IOC_ROLLBACK, fsname, args, &result);
	nvlist_free(args);
	nvlist_free(result);
	return (err);
}

/*
 * Creates bookmarks.
 *
 * The bookmarks nvlist maps from name of the bookmark (e.g. "pool/fs#bmark") to
 * the name of the snapshot (e.g. "pool/fs@snap").  All the bookmarks and
 * snapshots must be in the same pool.
 *
 * The returned results nvlist will have an entry for each bookmark that failed.
 * The value will be the (int32) error code.
 *
 * The return value will be 0 if all bookmarks were created, otherwise it will
 * be the errno of a (undetermined) bookmarks that failed.
 */
int
lzc_bookmark(nvlist_t *bookmarks, nvlist_t **errlist)
{
	nvpair_t *elem;
	int error;
	char pool[ZFS_MAX_DATASET_NAME_LEN];

	/* determine the pool name */
	elem = nvlist_next_nvpair(bookmarks, NULL);
	if (elem == NULL)
		return (0);
	(void) strlcpy(pool, nvpair_name(elem), sizeof (pool));
	pool[strcspn(pool, "/#")] = '\0';

	error = lzc_ioctl(ZFS_IOC_BOOKMARK, pool, bookmarks, errlist);

	return (error);
}

/*
 * Retrieve bookmarks.
 *
 * Retrieve the list of bookmarks for the given file system. The props
 * parameter is an nvlist of property names (with no values) that will be
 * returned for each bookmark.
 *
 * The following are valid properties on bookmarks, all of which are numbers
 * (represented as uint64 in the nvlist)
 *
 * "guid" - globally unique identifier of the snapshot it refers to
 * "createtxg" - txg when the snapshot it refers to was created
 * "creation" - timestamp when the snapshot it refers to was created
 *
 * The format of the returned nvlist as follows:
 * <short name of bookmark> -> {
 *     <name of property> -> {
 *         "value" -> uint64
 *     }
 *  }
 */
int
lzc_get_bookmarks(const char *fsname, nvlist_t *props, nvlist_t **bmarks)
{
	return (lzc_ioctl(ZFS_IOC_GET_BOOKMARKS, fsname, props, bmarks));
}

/*
 * Destroys bookmarks.
 *
 * The keys in the bmarks nvlist are the bookmarks to be destroyed.
 * They must all be in the same pool.  Bookmarks are specified as
 * <fs>#<bmark>.
 *
 * Bookmarks that do not exist will be silently ignored.
 *
 * The return value will be 0 if all bookmarks that existed were destroyed.
 *
 * Otherwise the return value will be the errno of a (undetermined) bookmark
 * that failed, no bookmarks will be destroyed, and the errlist will have an
 * entry for each bookmarks that failed.  The value in the errlist will be
 * the (int32) error code.
 */
int
lzc_destroy_bookmarks(nvlist_t *bmarks, nvlist_t **errlist)
{
	nvpair_t *elem;
	int error;
	char pool[ZFS_MAX_DATASET_NAME_LEN];

	/* determine the pool name */
	elem = nvlist_next_nvpair(bmarks, NULL);
	if (elem == NULL)
		return (0);
	(void) strlcpy(pool, nvpair_name(elem), sizeof (pool));
	pool[strcspn(pool, "/#")] = '\0';

	error = lzc_ioctl(ZFS_IOC_DESTROY_BOOKMARKS, pool, bmarks, errlist);

	return (error);
}

static int
lzc_channel_program_impl(const char *pool, const char *program, boolean_t sync,
    uint64_t instrlimit, uint64_t memlimit, nvlist_t *argnvl, nvlist_t **outnvl)
{
	int error;
	nvlist_t *args;

	args = fnvlist_alloc();
	fnvlist_add_string(args, ZCP_ARG_PROGRAM, program);
	fnvlist_add_nvlist(args, ZCP_ARG_ARGLIST, argnvl);
	fnvlist_add_boolean_value(args, ZCP_ARG_SYNC, sync);
	fnvlist_add_uint64(args, ZCP_ARG_INSTRLIMIT, instrlimit);
	fnvlist_add_uint64(args, ZCP_ARG_MEMLIMIT, memlimit);
	error = lzc_ioctl(ZFS_IOC_CHANNEL_PROGRAM, pool, args, outnvl);
	fnvlist_free(args);

	return (error);
}

/*
 * Executes a channel program.
 *
 * If this function returns 0 the channel program was successfully loaded and
 * ran without failing. Note that individual commands the channel program ran
 * may have failed and the channel program is responsible for reporting such
 * errors through outnvl if they are important.
 *
 * This method may also return:
 *
 * EINVAL   The program contains syntax errors, or an invalid memory or time
 *          limit was given. No part of the channel program was executed.
 *          If caused by syntax errors, 'outnvl' contains information about the
 *          errors.
 *
 * EDOM     The program was executed, but encountered a runtime error, such as
 *          calling a function with incorrect arguments, invoking the error()
 *          function directly, failing an assert() command, etc. Some portion
 *          of the channel program may have executed and committed changes.
 *          Information about the failure can be found in 'outnvl'.
 *
 * ENOMEM   The program fully executed, but the output buffer was not large
 *          enough to store the returned value. No output is returned through
 *          'outnvl'.
 *
 * ENOSPC   The program was terminated because it exceeded its memory usage
 *          limit. Some portion of the channel program may have executed and
 *          committed changes to disk. No output is returned through 'outnvl'.
 *
 * ETIMEDOUT The program was terminated because it exceeded its Lua instruction
 *           limit. Some portion of the channel program may have executed and
 *           committed changes to disk. No output is returned through 'outnvl'.
 */
int
lzc_channel_program(const char *pool, const char *program, uint64_t instrlimit,
    uint64_t memlimit, nvlist_t *argnvl, nvlist_t **outnvl)
{
	return (lzc_channel_program_impl(pool, program, B_TRUE, instrlimit,
	    memlimit, argnvl, outnvl));
}

/*
 * Creates a checkpoint for the specified pool.
 *
 * If this function returns 0 the pool was successfully checkpointed.
 *
 * This method may also return:
 *
 * ZFS_ERR_CHECKPOINT_EXISTS
 *	The pool already has a checkpoint. A pools can only have one
 *	checkpoint at most, at any given time.
 *
 * ZFS_ERR_DISCARDING_CHECKPOINT
 * 	ZFS is in the middle of discarding a checkpoint for this pool.
 * 	The pool can be checkpointed again once the discard is done.
 *
 * ZFS_DEVRM_IN_PROGRESS
 * 	A vdev is currently being removed. The pool cannot be
 * 	checkpointed until the device removal is done.
 *
 * ZFS_VDEV_TOO_BIG
 * 	One or more top-level vdevs exceed the maximum vdev size
 * 	supported for this feature.
 */
int
lzc_pool_checkpoint(const char *pool)
{
	int error;

	nvlist_t *result = NULL;
	nvlist_t *args = fnvlist_alloc();

	error = lzc_ioctl(ZFS_IOC_POOL_CHECKPOINT, pool, args, &result);

	fnvlist_free(args);
	fnvlist_free(result);

	return (error);
}

/*
 * Discard the checkpoint from the specified pool.
 *
 * If this function returns 0 the checkpoint was successfully discarded.
 *
 * This method may also return:
 *
 * ZFS_ERR_NO_CHECKPOINT
 * 	The pool does not have a checkpoint.
 *
 * ZFS_ERR_DISCARDING_CHECKPOINT
 * 	ZFS is already in the middle of discarding the checkpoint.
 */
int
lzc_pool_checkpoint_discard(const char *pool)
{
	int error;

	nvlist_t *result = NULL;
	nvlist_t *args = fnvlist_alloc();

	error = lzc_ioctl(ZFS_IOC_POOL_DISCARD_CHECKPOINT, pool, args, &result);

	fnvlist_free(args);
	fnvlist_free(result);

	return (error);
}

/*
 * Executes a read-only channel program.
 *
 * A read-only channel program works programmatically the same way as a
 * normal channel program executed with lzc_channel_program(). The only
 * difference is it runs exclusively in open-context and therefore can
 * return faster. The downside to that, is that the program cannot change
 * on-disk state by calling functions from the zfs.sync submodule.
 *
 * The return values of this function (and their meaning) are exactly the
 * same as the ones described in lzc_channel_program().
 */
int
lzc_channel_program_nosync(const char *pool, const char *program,
    uint64_t timeout, uint64_t memlimit, nvlist_t *argnvl, nvlist_t **outnvl)
{
	return (lzc_channel_program_impl(pool, program, B_FALSE, timeout,
	    memlimit, argnvl, outnvl));
}

/*
 * Changes initializing state.
 *
 * vdevs should be a list of (<key>, guid) where guid is a uint64 vdev GUID.
 * The key is ignored.
 *
 * If there are errors related to vdev arguments, per-vdev errors are returned
 * in an nvlist with the key "vdevs". Each error is a (guid, errno) pair where
 * guid is stringified with PRIu64, and errno is one of the following as
 * an int64_t:
 *	- ENODEV if the device was not found
 *	- EINVAL if the devices is not a leaf or is not concrete (e.g. missing)
 *	- EROFS if the device is not writeable
 *	- EBUSY start requested but the device is already being initialized
 *	- ESRCH cancel/suspend requested but device is not being initialized
 *
 * If the errlist is empty, then return value will be:
 *	- EINVAL if one or more arguments was invalid
 *	- Other spa_open failures
 *	- 0 if the operation succeeded
 */
int
lzc_initialize(const char *poolname, pool_initialize_func_t cmd_type,
    nvlist_t *vdevs, nvlist_t **errlist)
{
	int error;
	nvlist_t *args = fnvlist_alloc();
	fnvlist_add_uint64(args, ZPOOL_INITIALIZE_COMMAND, (uint64_t)cmd_type);
	fnvlist_add_nvlist(args, ZPOOL_INITIALIZE_VDEVS, vdevs);

	error = lzc_ioctl(ZFS_IOC_POOL_INITIALIZE, poolname, args, errlist);

	fnvlist_free(args);

	return (error);
}
