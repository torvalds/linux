/*
 * CDDL HEADER SART
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
 * Copyright (c) 2013 Martin Matuska <mm@FreeBSD.org>. All rights reserved.
 */

#include "libzfs_compat.h"

int zfs_ioctl_version = ZFS_IOCVER_UNDEF;
static int zfs_spa_version = -1;

/*
 * Get zfs_ioctl_version
 */
int
get_zfs_ioctl_version(void)
{
	size_t ver_size;
	int ver = ZFS_IOCVER_NONE;

	ver_size = sizeof(ver);
	sysctlbyname("vfs.zfs.version.ioctl", &ver, &ver_size, NULL, 0);

	return (ver);
}

/*
 * Get the SPA version
 */
static int
get_zfs_spa_version(void)
{
	size_t ver_size;
	int ver = 0;

	ver_size = sizeof(ver);
	sysctlbyname("vfs.zfs.version.spa", &ver, &ver_size, NULL, 0);

	return (ver);
}

/*
 * This is FreeBSD version of ioctl, because Solaris' ioctl() updates
 * zc_nvlist_dst_size even if an error is returned, on FreeBSD if an
 * error is returned zc_nvlist_dst_size won't be updated.
 */
int
zcmd_ioctl(int fd, int request, zfs_cmd_t *zc)
{
	size_t oldsize;
	int ret, cflag = ZFS_CMD_COMPAT_NONE;

	if (zfs_ioctl_version == ZFS_IOCVER_UNDEF)
		zfs_ioctl_version = get_zfs_ioctl_version();

	if (zfs_ioctl_version >= ZFS_IOCVER_DEADMAN) {
		switch (zfs_ioctl_version) {
		case ZFS_IOCVER_INLANES:
			cflag = ZFS_CMD_COMPAT_INLANES;
			break;
		case ZFS_IOCVER_RESUME:
			cflag = ZFS_CMD_COMPAT_RESUME;
			break;
		case ZFS_IOCVER_EDBP:
			cflag = ZFS_CMD_COMPAT_EDBP;
			break;
		case ZFS_IOCVER_ZCMD:
			cflag = ZFS_CMD_COMPAT_ZCMD;
			break;
		case ZFS_IOCVER_LZC:
			cflag = ZFS_CMD_COMPAT_LZC;
			break;
		case ZFS_IOCVER_DEADMAN:
			cflag = ZFS_CMD_COMPAT_DEADMAN;
			break;
		}
	} else {
		/*
		 * If vfs.zfs.version.ioctl is not defined, assume we have v28
		 * compatible binaries and use vfs.zfs.version.spa to test for v15
		 */
		cflag = ZFS_CMD_COMPAT_V28;

		if (zfs_spa_version < 0)
			zfs_spa_version = get_zfs_spa_version();

		if (zfs_spa_version == SPA_VERSION_15 ||
		    zfs_spa_version == SPA_VERSION_14 ||
		    zfs_spa_version == SPA_VERSION_13)
			cflag = ZFS_CMD_COMPAT_V15;
	}

	oldsize = zc->zc_nvlist_dst_size;
	ret = zcmd_ioctl_compat(fd, request, zc, cflag);

	if (ret == 0 && oldsize < zc->zc_nvlist_dst_size) {
		ret = -1;
		errno = ENOMEM;
	}

	return (ret);
}
