// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include <drm/drm_print.h>

#include "gt/intel_gt.h"
#include "gt/intel_gt_debugfs.h"
#include "gt/intel_gt_print.h"
#include "intel_gsc_uc.h"
#include "intel_gsc_uc_debugfs.h"
#include "i915_drv.h"

static int gsc_info_show(struct seq_file *m, void *data)
{
	struct drm_printer p = drm_seq_file_printer(m);
	struct intel_gsc_uc *gsc = m->private;

	if (!intel_gsc_uc_is_supported(gsc))
		return -ENODEV;

	intel_gsc_uc_load_status(gsc, &p);

	return 0;
}
DEFINE_INTEL_GT_DEBUGFS_ATTRIBUTE(gsc_info);

void intel_gsc_uc_debugfs_register(struct intel_gsc_uc *gsc_uc, struct dentry *root)
{
	static const struct intel_gt_debugfs_file files[] = {
		{ "gsc_info", &gsc_info_fops, NULL },
	};

	if (!intel_gsc_uc_is_supported(gsc_uc))
		return;

	intel_gt_debugfs_register_files(root, files, ARRAY_SIZE(files), gsc_uc);
}
