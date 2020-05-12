// SPDX-License-Identifier: MIT

/*
 * Copyright © 2019 Intel Corporation
 */

#include <linux/debugfs.h>

#include "debugfs_engines.h"
#include "debugfs_gt.h"
#include "debugfs_gt_pm.h"
#include "i915_drv.h"

void debugfs_gt_register(struct intel_gt *gt)
{
	struct dentry *root;

	if (!gt->i915->drm.primary->debugfs_root)
		return;

	root = debugfs_create_dir("gt", gt->i915->drm.primary->debugfs_root);
	if (IS_ERR(root))
		return;

	debugfs_engines_register(gt, root);
	debugfs_gt_pm_register(gt, root);
}

void debugfs_gt_register_files(struct intel_gt *gt,
			       struct dentry *root,
			       const struct debugfs_gt_file *files,
			       unsigned long count)
{
	while (count--) {
		if (!files->eval || files->eval(gt))
			debugfs_create_file(files->name,
					    0444, root, gt,
					    files->fops);

		files++;
	}
}
