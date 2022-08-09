/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2017 Intel Corporation
 */

#include <linux/fs.h>
#include <linux/mount.h>

#include "i915_drv.h"
#include "i915_gemfs.h"

int i915_gemfs_init(struct drm_i915_private *i915)
{
	struct file_system_type *type;
	struct vfsmount *gemfs;
	char *opts;

	type = get_fs_type("tmpfs");
	if (!type)
		return -ENODEV;

	/*
	 * By creating our own shmemfs mountpoint, we can pass in
	 * mount flags that better match our usecase.
	 *
	 * One example, although it is probably better with a per-file
	 * control, is selecting huge page allocations ("huge=within_size").
	 * However, we only do so to offset the overhead of iommu lookups
	 * due to bandwidth issues (slow reads) on Broadwell+.
	 */

	opts = NULL;
	if (intel_vtd_active()) {
		if (IS_ENABLED(CONFIG_TRANSPARENT_HUGEPAGE)) {
			static char huge_opt[] = "huge=within_size"; /* r/w */

			opts = huge_opt;
			drm_info(&i915->drm,
				 "Transparent Hugepage mode '%s'\n",
				 opts);
		} else {
			drm_notice(&i915->drm,
				   "Transparent Hugepage support is recommended for optimal performance when IOMMU is enabled!\n");
		}
	}

	gemfs = vfs_kern_mount(type, SB_KERNMOUNT, type->name, opts);
	if (IS_ERR(gemfs))
		return PTR_ERR(gemfs);

	i915->mm.gemfs = gemfs;

	return 0;
}

void i915_gemfs_fini(struct drm_i915_private *i915)
{
	kern_unmount(i915->mm.gemfs);
}
