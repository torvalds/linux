// SPDX-License-Identifier: MIT
/*
 * Copyright © 2024 Intel Corporation
 */

#include "xe_pxp_debugfs.h"

#include <linux/debugfs.h>

#include <drm/drm_debugfs.h>
#include <drm/drm_managed.h>
#include <drm/drm_print.h>

#include "xe_device.h"
#include "xe_pxp.h"
#include "xe_pxp_types.h"
#include "regs/xe_irq_regs.h"

static struct xe_pxp *node_to_pxp(struct drm_info_node *node)
{
	return node->info_ent->data;
}

static const char *pxp_status_to_str(struct xe_pxp *pxp)
{
	lockdep_assert_held(&pxp->mutex);

	switch (pxp->status) {
	case XE_PXP_ERROR:
		return "error";
	case XE_PXP_NEEDS_TERMINATION:
		return "needs termination";
	case XE_PXP_TERMINATION_IN_PROGRESS:
		return "termination in progress";
	case XE_PXP_READY_TO_START:
		return "ready to start";
	case XE_PXP_ACTIVE:
		return "active";
	case XE_PXP_SUSPENDED:
		return "suspended";
	default:
		return "unknown";
	}
};

static int pxp_info(struct seq_file *m, void *data)
{
	struct xe_pxp *pxp = node_to_pxp(m->private);
	struct drm_printer p = drm_seq_file_printer(m);
	const char *status;

	if (!xe_pxp_is_enabled(pxp))
		return -ENODEV;

	mutex_lock(&pxp->mutex);
	status = pxp_status_to_str(pxp);

	drm_printf(&p, "status: %s\n", status);
	drm_printf(&p, "instance counter: %u\n", pxp->key_instance);
	mutex_unlock(&pxp->mutex);

	return 0;
}

static int pxp_terminate(struct seq_file *m, void *data)
{
	struct xe_pxp *pxp = node_to_pxp(m->private);
	struct drm_printer p = drm_seq_file_printer(m);
	int ready = xe_pxp_get_readiness_status(pxp);

	if (ready < 0)
		return ready; /* disabled or error occurred */
	else if (!ready)
		return -EBUSY; /* init still in progress */

	/* no need for a termination if PXP is not active */
	if (pxp->status != XE_PXP_ACTIVE) {
		drm_printf(&p, "PXP not active\n");
		return 0;
	}

	/* simulate a termination interrupt */
	spin_lock_irq(&pxp->xe->irq.lock);
	xe_pxp_irq_handler(pxp->xe, KCR_PXP_STATE_TERMINATED_INTERRUPT);
	spin_unlock_irq(&pxp->xe->irq.lock);

	drm_printf(&p, "PXP termination queued\n");

	return 0;
}

static const struct drm_info_list debugfs_list[] = {
	{"info", pxp_info, 0},
	{"terminate", pxp_terminate, 0},
};

void xe_pxp_debugfs_register(struct xe_pxp *pxp)
{
	struct drm_minor *minor;
	struct drm_info_list *local;
	struct dentry *root;
	int i;

	if (!xe_pxp_is_enabled(pxp))
		return;

	minor = pxp->xe->drm.primary;
	if (!minor->debugfs_root)
		return;

#define DEBUGFS_SIZE	(ARRAY_SIZE(debugfs_list) * sizeof(struct drm_info_list))
	local = drmm_kmalloc(&pxp->xe->drm, DEBUGFS_SIZE, GFP_KERNEL);
	if (!local)
		return;

	memcpy(local, debugfs_list, DEBUGFS_SIZE);
#undef DEBUGFS_SIZE

	for (i = 0; i < ARRAY_SIZE(debugfs_list); ++i)
		local[i].data = pxp;

	root = debugfs_create_dir("pxp", minor->debugfs_root);
	if (IS_ERR(root))
		return;

	drm_debugfs_create_files(local,
				 ARRAY_SIZE(debugfs_list),
				 root, minor);
}
