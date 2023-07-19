// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_guc_debugfs.h"

#include <drm/drm_debugfs.h>
#include <drm/drm_managed.h>

#include "xe_device.h"
#include "xe_gt.h"
#include "xe_guc.h"
#include "xe_guc_ct.h"
#include "xe_guc_log.h"
#include "xe_macros.h"

static struct xe_gt *
guc_to_gt(struct xe_guc *guc)
{
	return container_of(guc, struct xe_gt, uc.guc);
}

static struct xe_device *
guc_to_xe(struct xe_guc *guc)
{
	return gt_to_xe(guc_to_gt(guc));
}

static struct xe_guc *node_to_guc(struct drm_info_node *node)
{
	return node->info_ent->data;
}

static int guc_info(struct seq_file *m, void *data)
{
	struct xe_guc *guc = node_to_guc(m->private);
	struct xe_device *xe = guc_to_xe(guc);
	struct drm_printer p = drm_seq_file_printer(m);

	xe_device_mem_access_get(xe);
	xe_guc_print_info(guc, &p);
	xe_device_mem_access_put(xe);

	return 0;
}

static int guc_log(struct seq_file *m, void *data)
{
	struct xe_guc *guc = node_to_guc(m->private);
	struct xe_device *xe = guc_to_xe(guc);
	struct drm_printer p = drm_seq_file_printer(m);

	xe_device_mem_access_get(xe);
	xe_guc_log_print(&guc->log, &p);
	xe_device_mem_access_put(xe);

	return 0;
}

#ifdef XE_GUC_CT_SELFTEST
static int guc_ct_selftest(struct seq_file *m, void *data)
{
	struct xe_guc *guc = node_to_guc(m->private);
	struct xe_device *xe = guc_to_xe(guc);
	struct drm_printer p = drm_seq_file_printer(m);

	xe_device_mem_access_get(xe);
	xe_guc_ct_selftest(&guc->ct, &p);
	xe_device_mem_access_put(xe);

	return 0;
}
#endif

static const struct drm_info_list debugfs_list[] = {
	{"guc_info", guc_info, 0},
	{"guc_log", guc_log, 0},
#ifdef XE_GUC_CT_SELFTEST
	{"guc_ct_selftest", guc_ct_selftest, 0},
#endif
};

void xe_guc_debugfs_register(struct xe_guc *guc, struct dentry *parent)
{
	struct drm_minor *minor = guc_to_xe(guc)->drm.primary;
	struct drm_info_list *local;
	int i;

#define DEBUGFS_SIZE	(ARRAY_SIZE(debugfs_list) * sizeof(struct drm_info_list))
	local = drmm_kmalloc(&guc_to_xe(guc)->drm, DEBUGFS_SIZE, GFP_KERNEL);
	if (!local)
		return;

	memcpy(local, debugfs_list, DEBUGFS_SIZE);
#undef DEBUGFS_SIZE

	for (i = 0; i < ARRAY_SIZE(debugfs_list); ++i)
		local[i].data = guc;

	drm_debugfs_create_files(local,
				 ARRAY_SIZE(debugfs_list),
				 parent, minor);
}
