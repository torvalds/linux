/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2017 Intel Corporation
 */

#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/pagemap.h>

#include "i915_drv.h"
#include "i915_gemfs.h"

int i915_gemfs_init(struct drm_i915_private *i915)
{
	struct file_system_type *type;
	struct vfsmount *gemfs;

	type = get_fs_type("tmpfs");
	if (!type)
		return -ENODEV;

	gemfs = kern_mount(type);
	if (IS_ERR(gemfs))
		return PTR_ERR(gemfs);

	/*
	 * Enable huge-pages for objects that are at least HPAGE_PMD_SIZE, most
	 * likely 2M. Note that within_size may overallocate huge-pages, if say
	 * we allocate an object of size 2M + 4K, we may get 2M + 2M, but under
	 * memory pressure shmem should split any huge-pages which can be
	 * shrunk.
	 */

	if (has_transparent_hugepage()) {
		struct super_block *sb = gemfs->mnt_sb;
		/* FIXME: Disabled until we get W/A for read BW issue. */
		char options[] = "huge=never";
		int flags = 0;
		int err;

		err = sb->s_op->remount_fs(sb, &flags, options);
		if (err) {
			kern_unmount(gemfs);
			return err;
		}
	}

	i915->mm.gemfs = gemfs;

	return 0;
}

void i915_gemfs_fini(struct drm_i915_private *i915)
{
	kern_unmount(i915->mm.gemfs);
}
