// SPDX-License-Identifier: GPL-2.0+
/* Copyright (C) 2024 Raspberry Pi */

#include <linux/fs.h>
#include <linux/mount.h>

#include "v3d_drv.h"

void v3d_gemfs_init(struct v3d_dev *v3d)
{
	char huge_opt[] = "huge=within_size";
	struct file_system_type *type;
	struct vfsmount *gemfs;

	/*
	 * By creating our own shmemfs mountpoint, we can pass in
	 * mount flags that better match our usecase. However, we
	 * only do so on platforms which benefit from it.
	 */
	if (!IS_ENABLED(CONFIG_TRANSPARENT_HUGEPAGE))
		goto err;

	/* The user doesn't want to enable Super Pages */
	if (!super_pages)
		goto err;

	type = get_fs_type("tmpfs");
	if (!type)
		goto err;

	gemfs = vfs_kern_mount(type, SB_KERNMOUNT, type->name, huge_opt);
	if (IS_ERR(gemfs))
		goto err;

	v3d->gemfs = gemfs;
	drm_info(&v3d->drm, "Using Transparent Hugepages\n");

	return;

err:
	v3d->gemfs = NULL;
	drm_notice(&v3d->drm,
		   "Transparent Hugepage support is recommended for optimal performance on this platform!\n");
}

void v3d_gemfs_fini(struct v3d_dev *v3d)
{
	if (v3d->gemfs)
		kern_unmount(v3d->gemfs);
}
