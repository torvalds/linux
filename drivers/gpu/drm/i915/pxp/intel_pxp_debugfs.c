// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include <linux/debugfs.h>
#include <linux/string_helpers.h>

#include <drm/drm_print.h>

#include "gt/intel_gt_debugfs.h"

#include "i915_drv.h"

#include "intel_pxp.h"
#include "intel_pxp_debugfs.h"
#include "intel_pxp_gsccs.h"
#include "intel_pxp_irq.h"
#include "intel_pxp_types.h"

static int pxp_info_show(struct seq_file *m, void *data)
{
	struct intel_pxp *pxp = m->private;
	struct drm_printer p = drm_seq_file_printer(m);

	if (!intel_pxp_is_enabled(pxp)) {
		drm_printf(&p, "pxp disabled\n");
		return 0;
	}

	drm_printf(&p, "active: %s\n", str_yes_no(intel_pxp_is_active(pxp)));
	drm_printf(&p, "instance counter: %u\n", pxp->key_instance);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(pxp_info);

static int pxp_terminate_get(void *data, u64 *val)
{
	/* nothing to read */
	return -EPERM;
}

static int pxp_terminate_set(void *data, u64 val)
{
	struct intel_pxp *pxp = data;
	struct intel_gt *gt = pxp->ctrl_gt;
	int timeout_ms;

	if (!intel_pxp_is_active(pxp))
		return -ENODEV;

	/* simulate a termination interrupt */
	spin_lock_irq(gt->irq_lock);
	intel_pxp_irq_handler(pxp, GEN12_DISPLAY_PXP_STATE_TERMINATED_INTERRUPT);
	spin_unlock_irq(gt->irq_lock);

	timeout_ms = intel_pxp_get_backend_timeout_ms(pxp);

	if (!wait_for_completion_timeout(&pxp->termination,
					 msecs_to_jiffies(timeout_ms)))
		return -ETIMEDOUT;

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(pxp_terminate_fops, pxp_terminate_get, pxp_terminate_set, "%llx\n");

void intel_pxp_debugfs_register(struct intel_pxp *pxp)
{
	struct dentry *debugfs_root;
	struct dentry *pxproot;

	if (!intel_pxp_is_supported(pxp))
		return;

	debugfs_root = pxp->ctrl_gt->i915->drm.debugfs_root;
	if (!debugfs_root)
		return;

	pxproot = debugfs_create_dir("pxp", debugfs_root);
	if (IS_ERR(pxproot))
		return;

	debugfs_create_file("info", 0444, pxproot,
			    pxp, &pxp_info_fops);

	debugfs_create_file("terminate_state", 0644, pxproot,
			    pxp, &pxp_terminate_fops);
}
