/* vi: set sw=4 ts=4: */
/*
 * Utility routines.
 *
 * Copyright (C) 1999-2004 by Erik Andersen <andersen@codepoet.org>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
#include "libbb.h"
#include <mntent.h>

/*
 * Given a block device, find the mount table entry if that block device
 * is mounted.
 *
 * Given any other file (or directory), find the mount table entry for its
 * filesystem.
 */
struct mntent* FAST_FUNC find_mount_point(const char *name, int subdir_too)
{
	struct stat s;
	FILE *mtab_fp;
	struct mntent *mountEntry;
	dev_t devno_of_name;
	bool block_dev;

	if (stat(name, &s) != 0)
		return NULL;

	devno_of_name = s.st_dev;
	block_dev = 0;
	/* Why S_ISCHR? - UBI volumes use char devices, not block */
	if (S_ISBLK(s.st_mode) || S_ISCHR(s.st_mode)) {
		devno_of_name = s.st_rdev;
		block_dev = 1;
	}

	mtab_fp = setmntent(bb_path_mtab_file, "r");
	if (!mtab_fp)
		return NULL;

	while ((mountEntry = getmntent(mtab_fp)) != NULL) {
		/* rootfs mount in Linux 2.6 exists always,
		 * and it makes sense to always ignore it.
		 * Otherwise people can't reference their "real" root! */
		if (ENABLE_FEATURE_SKIP_ROOTFS && strcmp(mountEntry->mnt_fsname, "rootfs") == 0)
			continue;

		if (strcmp(name, mountEntry->mnt_dir) == 0
		 || strcmp(name, mountEntry->mnt_fsname) == 0
		) { /* String match. */
			break;
		}

		if (!(subdir_too || block_dev))
			continue;

		/* Is device's dev_t == name's dev_t? */
		if (stat(mountEntry->mnt_fsname, &s) == 0 && s.st_rdev == devno_of_name)
			break;
		/* Match the directory's mount point. */
		if (stat(mountEntry->mnt_dir, &s) == 0 && s.st_dev == devno_of_name)
			break;
	}
	endmntent(mtab_fp);

	return mountEntry;
}
