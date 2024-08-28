// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_gsc_debugfs.h"

#include <drm/drm_debugfs.h>
#include <drm/drm_managed.h>

#include "xe_device.h"
#include "xe_gt.h"
#include "xe_gsc.h"
#include "xe_macros.h"
#include "xe_pm.h"

static struct xe_gt *
gsc_to_gt(struct xe_gsc *gsc)
{
	return container_of(gsc, struct xe_gt, uc.gsc);
}

static struct xe_device *
gsc_to_xe(struct xe_gsc *gsc)
{
	return gt_to_xe(gsc_to_gt(gsc));
}

static struct xe_gsc *node_to_gsc(struct drm_info_node *node)
{
	return node->info_ent->data;
}

static int gsc_info(struct seq_file *m, void *data)
{
	struct xe_gsc *gsc = node_to_gsc(m->private);
	struct xe_device *xe = gsc_to_xe(gsc);
	struct drm_printer p = drm_seq_file_printer(m);

	xe_pm_runtime_get(xe);
	xe_gsc_print_info(gsc, &p);
	xe_pm_runtime_put(xe);

	return 0;
}

static const struct drm_info_list debugfs_list[] = {
	{"gsc_info", gsc_info, 0},
};

void xe_gsc_debugfs_register(struct xe_gsc *gsc, struct dentry *parent)
{
	struct drm_minor *minor = gsc_to_xe(gsc)->drm.primary;
	struct drm_info_list *local;
	int i;

#define DEBUGFS_SIZE	(ARRAY_SIZE(debugfs_list) * sizeof(struct drm_info_list))
	local = drmm_kmalloc(&gsc_to_xe(gsc)->drm, DEBUGFS_SIZE, GFP_KERNEL);
	if (!local)
		return;

	memcpy(local, debugfs_list, DEBUGFS_SIZE);
#undef DEBUGFS_SIZE

	for (i = 0; i < ARRAY_SIZE(debugfs_list); ++i)
		local[i].data = gsc;

	drm_debugfs_create_files(local,
				 ARRAY_SIZE(debugfs_list),
				 parent, minor);
}
