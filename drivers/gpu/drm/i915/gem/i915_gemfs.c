// SPDX-License-Identifier: MIT
/*
 * Copyright © 2017 Intel Corporation
 */

#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/fs_context.h>

#include "i915_drv.h"
#include "i915_gemfs.h"
#include "i915_utils.h"

void i915_gemfs_init(struct drm_i915_private *i915)
{
	struct file_system_type *type;
	struct fs_context *fc;
	struct vfsmount *gemfs;
	int ret;

	/*
	 * By creating our own shmemfs mountpoint, we can pass in
	 * mount flags that better match our usecase.
	 *
	 * One example, although it is probably better with a per-file
	 * control, is selecting huge page allocations ("huge=within_size").
	 * However, we only do so on platforms which benefit from it, or to
	 * offset the overhead of iommu lookups, where with latter it is a net
	 * win even on platforms which would otherwise see some performance
	 * regressions such a slow reads issue on Broadwell and Skylake.
	 */

	if (GRAPHICS_VER(i915) < 11 && !i915_vtd_active(i915))
		return;

	if (!IS_ENABLED(CONFIG_TRANSPARENT_HUGEPAGE))
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

	i915->mm.gemfs = gemfs;
	drm_info(&i915->drm, "Using Transparent Hugepages\n");
	return;

err:
	drm_notice(&i915->drm,
		   "Transparent Hugepage support is recommended for optimal performance%s\n",
		   GRAPHICS_VER(i915) >= 11 ? " on this platform!" :
					      " when IOMMU is enabled!");
}

void i915_gemfs_fini(struct drm_i915_private *i915)
{
	kern_unmount(i915->mm.gemfs);
}
