// SPDX-License-Identifier: GPL-2.0+
/* Copyright (C) 2024 Raspberry Pi */

#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/fs_context.h>

#include "v3d_drv.h"

void v3d_gemfs_init(struct v3d_dev *v3d)
{
	struct file_system_type *type;
	struct fs_context *fc;
	struct vfsmount *gemfs;
	int ret;

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

	fc = fs_context_for_mount(type, SB_KERNMOUNT);
	if (IS_ERR(fc))
		goto err;
	ret = vfs_parse_fs_string(fc, "source", "tmpfs");
	if (!ret)
		ret = vfs_parse_fs_string(fc, "huge", "within_size");
	if (!ret)
		gemfs = fc_mount_longterm(fc);
	put_fs_context(fc);
	if (ret)
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
