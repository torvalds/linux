// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include <linux/debugfs.h>

#include "i915_drv.h"
#include "intel_gt.h"
#include "intel_gt_debugfs.h"
#include "intel_gt_engines_debugfs.h"
#include "intel_gt_pm_debugfs.h"
#include "intel_sseu_debugfs.h"
#include "pxp/intel_pxp_debugfs.h"
#include "uc/intel_uc_debugfs.h"

int intel_gt_debugfs_reset_show(struct intel_gt *gt, u64 *val)
{
	int ret = intel_gt_terminally_wedged(gt);

	switch (ret) {
	case -EIO:
		*val = 1;
		return 0;
	case 0:
		*val = 0;
		return 0;
	default:
		return ret;
	}
}

void intel_gt_debugfs_reset_store(struct intel_gt *gt, u64 val)
{
	/* Flush any previous reset before applying for a new one */
	wait_event(gt->reset.queue,
		   !test_bit(I915_RESET_BACKOFF, &gt->reset.flags));

	intel_gt_handle_error(gt, val, I915_ERROR_CAPTURE,
			      "Manually reset engine mask to %llx", val);
}

/*
 * keep the interface clean where the first parameter
 * is a 'struct intel_gt *' instead of 'void *'
 */
static int __intel_gt_debugfs_reset_show(void *data, u64 *val)
{
	return intel_gt_debugfs_reset_show(data, val);
}

static int __intel_gt_debugfs_reset_store(void *data, u64 val)
{
	intel_gt_debugfs_reset_store(data, val);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(reset_fops, __intel_gt_debugfs_reset_show,
			__intel_gt_debugfs_reset_store, "%llu\n");

static int steering_show(struct seq_file *m, void *data)
{
	struct drm_printer p = drm_seq_file_printer(m);
	struct intel_gt *gt = m->private;

	intel_gt_report_steering(&p, gt, true);

	return 0;
}
DEFINE_INTEL_GT_DEBUGFS_ATTRIBUTE(steering);

static void gt_debugfs_register(struct intel_gt *gt, struct dentry *root)
{
	static const struct intel_gt_debugfs_file files[] = {
		{ "reset", &reset_fops, NULL },
		{ "steering", &steering_fops },
	};

	intel_gt_debugfs_register_files(root, files, ARRAY_SIZE(files), gt);
}

void intel_gt_debugfs_register(struct intel_gt *gt)
{
	struct dentry *root;

	if (!gt->i915->drm.primary->debugfs_root)
		return;

	root = debugfs_create_dir("gt", gt->i915->drm.primary->debugfs_root);
	if (IS_ERR(root))
		return;

	gt_debugfs_register(gt, root);

	intel_gt_engines_debugfs_register(gt, root);
	intel_gt_pm_debugfs_register(gt, root);
	intel_sseu_debugfs_register(gt, root);

	intel_uc_debugfs_register(&gt->uc, root);
	intel_pxp_debugfs_register(&gt->pxp, root);
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
