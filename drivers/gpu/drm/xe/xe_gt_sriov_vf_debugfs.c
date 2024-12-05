// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023-2024 Intel Corporation
 */

#include <linux/debugfs.h>

#include <drm/drm_debugfs.h>

#include "xe_gt_debugfs.h"
#include "xe_gt_sriov_vf.h"
#include "xe_gt_sriov_vf_debugfs.h"
#include "xe_gt_types.h"
#include "xe_sriov.h"

/*
 *      /sys/kernel/debug/dri/0/
 *      ├── gt0
 *      │   ├── vf
 *      │   │   ├── self_config
 *      │   │   ├── abi_versions
 *      │   │   ├── runtime_regs
 */

static const struct drm_info_list vf_info[] = {
	{
		"self_config",
		.show = xe_gt_debugfs_simple_show,
		.data = xe_gt_sriov_vf_print_config,
	},
	{
		"abi_versions",
		.show = xe_gt_debugfs_simple_show,
		.data = xe_gt_sriov_vf_print_version,
	},
#if IS_ENABLED(CONFIG_DRM_XE_DEBUG) || IS_ENABLED(CONFIG_DRM_XE_DEBUG_SRIOV)
	{
		"runtime_regs",
		.show = xe_gt_debugfs_simple_show,
		.data = xe_gt_sriov_vf_print_runtime,
	},
#endif
};

/**
 * xe_gt_sriov_vf_debugfs_register - Register SR-IOV VF specific entries in GT debugfs.
 * @gt: the &xe_gt to register
 * @root: the &dentry that represents the GT directory
 *
 * Register SR-IOV VF entries that are GT related and must be shown under GT debugfs.
 */
void xe_gt_sriov_vf_debugfs_register(struct xe_gt *gt, struct dentry *root)
{
	struct xe_device *xe = gt_to_xe(gt);
	struct drm_minor *minor = xe->drm.primary;
	struct dentry *vfdentry;

	xe_assert(xe, IS_SRIOV_VF(xe));
	xe_assert(xe, root->d_inode->i_private == gt);

	/*
	 *      /sys/kernel/debug/dri/0/
	 *      ├── gt0
	 *      │   ├── vf
	 */
	vfdentry = debugfs_create_dir("vf", root);
	if (IS_ERR(vfdentry))
		return;
	vfdentry->d_inode->i_private = gt;

	drm_debugfs_create_files(vf_info, ARRAY_SIZE(vf_info), vfdentry, minor);
}
