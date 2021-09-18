// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include <linux/debugfs.h>

#include "debugfs_gt_pm.h"
#include "i915_drv.h"
#include "intel_gt_debugfs.h"
#include "intel_gt_engines_debugfs.h"
#include "intel_sseu_debugfs.h"
#include "uc/intel_uc_debugfs.h"

void intel_gt_debugfs_register(struct intel_gt *gt)
{
	struct dentry *root;

	if (!gt->i915->drm.primary->debugfs_root)
		return;

	root = debugfs_create_dir("gt", gt->i915->drm.primary->debugfs_root);
	if (IS_ERR(root))
		return;

	intel_gt_engines_debugfs_register(gt, root);
	debugfs_gt_pm_register(gt, root);
	intel_sseu_debugfs_register(gt, root);

	intel_uc_debugfs_register(&gt->uc, root);
}

void intel_gt_debugfs_register_files(struct dentry *root,
				     const struct intel_gt_debugfs_file *files,
				     unsigned long count, void *data)
{
	while (count--) {
		umode_t mode = files->fops->write ? 0644 : 0444;

		if (!files->eval || files->eval(data))
			debugfs_create_file(files->name,
					    mode, root, data,
					    files->fops);

		files++;
	}
}
