// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_debugfs.h"

#include <linux/debugfs.h>
#include <linux/fault-inject.h>
#include <linux/string_helpers.h>

#include <drm/drm_debugfs.h>

#include "xe_bo.h"
#include "xe_device.h"
#include "xe_force_wake.h"
#include "xe_gt_debugfs.h"
#include "xe_gt_printk.h"
#include "xe_guc_ads.h"
#include "xe_pm.h"
#include "xe_sriov.h"
#include "xe_step.h"

#ifdef CONFIG_DRM_XE_DEBUG
#include "xe_bo_evict.h"
#include "xe_migrate.h"
#include "xe_vm.h"
#endif

DECLARE_FAULT_ATTR(gt_reset_failure);

static struct xe_device *node_to_xe(struct drm_info_node *node)
{
	return to_xe_device(node->minor->dev);
}

static int info(struct seq_file *m, void *data)
{
	struct xe_device *xe = node_to_xe(m->private);
	struct drm_printer p = drm_seq_file_printer(m);
	struct xe_gt *gt;
	u8 id;

	xe_pm_runtime_get(xe);

	drm_printf(&p, "graphics_verx100 %d\n", xe->info.graphics_verx100);
	drm_printf(&p, "media_verx100 %d\n", xe->info.media_verx100);
	drm_printf(&p, "stepping G:%s M:%s D:%s B:%s\n",
		   xe_step_name(xe->info.step.graphics),
		   xe_step_name(xe->info.step.media),
		   xe_step_name(xe->info.step.display),
		   xe_step_name(xe->info.step.basedie));
	drm_printf(&p, "is_dgfx %s\n", str_yes_no(xe->info.is_dgfx));
	drm_printf(&p, "platform %d\n", xe->info.platform);
	drm_printf(&p, "subplatform %d\n",
		   xe->info.subplatform > XE_SUBPLATFORM_NONE ? xe->info.subplatform : 0);
	drm_printf(&p, "devid 0x%x\n", xe->info.devid);
	drm_printf(&p, "revid %d\n", xe->info.revid);
	drm_printf(&p, "tile_count %d\n", xe->info.tile_count);
	drm_printf(&p, "vm_max_level %d\n", xe->info.vm_max_level);
	drm_printf(&p, "force_execlist %s\n", str_yes_no(xe->info.force_execlist));
	drm_printf(&p, "has_flat_ccs %s\n", str_yes_no(xe->info.has_flat_ccs));
	drm_printf(&p, "has_usm %s\n", str_yes_no(xe->info.has_usm));
	drm_printf(&p, "skip_guc_pc %s\n", str_yes_no(xe->info.skip_guc_pc));
	for_each_gt(gt, xe, id) {
		drm_printf(&p, "gt%d force wake %d\n", id,
			   xe_force_wake_ref(gt_to_fw(gt), XE_FW_GT));
		drm_printf(&p, "gt%d engine_mask 0x%llx\n", id,
			   gt->info.engine_mask);
	}

	xe_pm_runtime_put(xe);
	return 0;
}

static int sriov_info(struct seq_file *m, void *data)
{
	struct xe_device *xe = node_to_xe(m->private);
	struct drm_printer p = drm_seq_file_printer(m);

	xe_sriov_print_info(xe, &p);
	return 0;
}

static const struct drm_info_list debugfs_list[] = {
	{"info", info, 0},
	{ .name = "sriov_info", .show = sriov_info, },
};

static int forcewake_open(struct inode *inode, struct file *file)
{
	struct xe_device *xe = inode->i_private;
	struct xe_gt *gt;
	u8 id;

	xe_pm_runtime_get(xe);
	for_each_gt(gt, xe, id)
		XE_WARN_ON(xe_force_wake_get(gt_to_fw(gt), XE_FORCEWAKE_ALL));

	return 0;
}

static int forcewake_release(struct inode *inode, struct file *file)
{
	struct xe_device *xe = inode->i_private;
	struct xe_gt *gt;
	u8 id;

	for_each_gt(gt, xe, id)
		XE_WARN_ON(xe_force_wake_put(gt_to_fw(gt), XE_FORCEWAKE_ALL));
	xe_pm_runtime_put(xe);

	return 0;
}

static const struct file_operations forcewake_all_fops = {
	.owner = THIS_MODULE,
	.open = forcewake_open,
	.release = forcewake_release,
};

static ssize_t wedged_mode_show(struct file *f, char __user *ubuf,
				size_t size, loff_t *pos)
{
	struct xe_device *xe = file_inode(f)->i_private;
	char buf[32];
	int len = 0;

	len = scnprintf(buf, sizeof(buf), "%d\n", xe->wedged.mode);

	return simple_read_from_buffer(ubuf, size, pos, buf, len);
}

static ssize_t wedged_mode_set(struct file *f, const char __user *ubuf,
			       size_t size, loff_t *pos)
{
	struct xe_device *xe = file_inode(f)->i_private;
	struct xe_gt *gt;
	u32 wedged_mode;
	ssize_t ret;
	u8 id;

	ret = kstrtouint_from_user(ubuf, size, 0, &wedged_mode);
	if (ret)
		return ret;

	if (wedged_mode > 2)
		return -EINVAL;

	if (xe->wedged.mode == wedged_mode)
		return 0;

	xe->wedged.mode = wedged_mode;

	xe_pm_runtime_get(xe);
	for_each_gt(gt, xe, id) {
		ret = xe_guc_ads_scheduler_policy_toggle_reset(&gt->uc.guc.ads);
		if (ret) {
			xe_gt_err(gt, "Failed to update GuC ADS scheduler policy. GuC may still cause engine reset even with wedged_mode=2\n");
			return -EIO;
		}
	}
	xe_pm_runtime_put(xe);

	return size;
}

static const struct file_operations wedged_mode_fops = {
	.owner = THIS_MODULE,
	.read = wedged_mode_show,
	.write = wedged_mode_set,
};

void xe_debugfs_register(struct xe_device *xe)
{
	struct ttm_device *bdev = &xe->ttm;
	struct drm_minor *minor = xe->drm.primary;
	struct dentry *root = minor->debugfs_root;
	struct ttm_resource_manager *man;
	struct xe_gt *gt;
	u32 mem_type;
	u8 id;

	drm_debugfs_create_files(debugfs_list,
				 ARRAY_SIZE(debugfs_list),
				 root, minor);

	debugfs_create_file("forcewake_all", 0400, root, xe,
			    &forcewake_all_fops);

	debugfs_create_file("wedged_mode", 0400, root, xe,
			    &wedged_mode_fops);

	for (mem_type = XE_PL_VRAM0; mem_type <= XE_PL_VRAM1; ++mem_type) {
		man = ttm_manager_type(bdev, mem_type);

		if (man) {
			char name[16];

			snprintf(name, sizeof(name), "vram%d_mm", mem_type - XE_PL_VRAM0);
			ttm_resource_manager_create_debugfs(man, root, name);
		}
	}

	man = ttm_manager_type(bdev, XE_PL_TT);
	ttm_resource_manager_create_debugfs(man, root, "gtt_mm");

	man = ttm_manager_type(bdev, XE_PL_STOLEN);
	if (man)
		ttm_resource_manager_create_debugfs(man, root, "stolen_mm");

	for_each_gt(gt, xe, id)
		xe_gt_debugfs_register(gt);

	fault_create_debugfs_attr("fail_gt_reset", root, &gt_reset_failure);
}
