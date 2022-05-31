// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include <linux/debugfs.h>
#include <drm/drm_print.h>

#include "gt/intel_gt_debugfs.h"
#include "pxp/intel_pxp.h"
#include "pxp/intel_pxp_irq.h"
#include "i915_drv.h"

static int pxp_info_show(struct seq_file *m, void *data)
{
	struct intel_pxp *pxp = m->private;
	struct drm_printer p = drm_seq_file_printer(m);
	bool enabled = intel_pxp_is_enabled(pxp);

	if (!enabled) {
		drm_printf(&p, "pxp disabled\n");
		return 0;
	}

	drm_printf(&p, "active: %s\n", yesno(intel_pxp_is_active(pxp)));
	drm_printf(&p, "instance counter: %u\n", pxp->key_instance);

	return 0;
}
DEFINE_INTEL_GT_DEBUGFS_ATTRIBUTE(pxp_info);

static int pxp_terminate_get(void *data, u64 *val)
{
	/* nothing to read */
	return -EPERM;
}

static int pxp_terminate_set(void *data, u64 val)
{
	struct intel_pxp *pxp = data;
	struct intel_gt *gt = pxp_to_gt(pxp);

	if (!intel_pxp_is_active(pxp))
		return -ENODEV;

	/* simulate a termination interrupt */
	spin_lock_irq(&gt->irq_lock);
	intel_pxp_irq_handler(pxp, GEN12_DISPLAY_PXP_STATE_TERMINATED_INTERRUPT);
	spin_unlock_irq(&gt->irq_lock);

	if (!wait_for_completion_timeout(&pxp->termination,
					 msecs_to_jiffies(100)))
		return -ETIMEDOUT;

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(pxp_terminate_fops, pxp_terminate_get, pxp_terminate_set, "%llx\n");
void intel_pxp_debugfs_register(struct intel_pxp *pxp, struct dentry *gt_root)
{
	static const struct intel_gt_debugfs_file files[] = {
		{ "info", &pxp_info_fops, NULL },
		{ "terminate_state", &pxp_terminate_fops, NULL },
	};
	struct dentry *root;

	if (!gt_root)
		return;

	if (!HAS_PXP((pxp_to_gt(pxp)->i915)))
		return;

	root = debugfs_create_dir("pxp", gt_root);
	if (IS_ERR(root))
		return;

	intel_gt_debugfs_register_files(root, files, ARRAY_SIZE(files), pxp);
}
