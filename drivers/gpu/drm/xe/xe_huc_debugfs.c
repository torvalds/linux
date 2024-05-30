// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_huc_debugfs.h"

#include <drm/drm_debugfs.h>
#include <drm/drm_managed.h>

#include "xe_device.h"
#include "xe_gt.h"
#include "xe_huc.h"
#include "xe_macros.h"
#include "xe_pm.h"

static struct xe_gt *
huc_to_gt(struct xe_huc *huc)
{
	return container_of(huc, struct xe_gt, uc.huc);
}

static struct xe_device *
huc_to_xe(struct xe_huc *huc)
{
	return gt_to_xe(huc_to_gt(huc));
}

static struct xe_huc *node_to_huc(struct drm_info_node *node)
{
	return node->info_ent->data;
}

static int huc_info(struct seq_file *m, void *data)
{
	struct xe_huc *huc = node_to_huc(m->private);
	struct xe_device *xe = huc_to_xe(huc);
	struct drm_printer p = drm_seq_file_printer(m);

	xe_pm_runtime_get(xe);
	xe_huc_print_info(huc, &p);
	xe_pm_runtime_put(xe);

	return 0;
}

static const struct drm_info_list debugfs_list[] = {
	{"huc_info", huc_info, 0},
};

void xe_huc_debugfs_register(struct xe_huc *huc, struct dentry *parent)
{
	struct drm_minor *minor = huc_to_xe(huc)->drm.primary;
	struct drm_info_list *local;
	int i;

#define DEBUGFS_SIZE	(ARRAY_SIZE(debugfs_list) * sizeof(struct drm_info_list))
	local = drmm_kmalloc(&huc_to_xe(huc)->drm, DEBUGFS_SIZE, GFP_KERNEL);
	if (!local)
		return;

	memcpy(local, debugfs_list, DEBUGFS_SIZE);
#undef DEBUGFS_SIZE

	for (i = 0; i < ARRAY_SIZE(debugfs_list); ++i)
		local[i].data = huc;

	drm_debugfs_create_files(local,
				 ARRAY_SIZE(debugfs_list),
				 parent, minor);
}
