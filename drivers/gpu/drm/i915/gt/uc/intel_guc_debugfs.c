// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include <drm/drm_print.h>

#include "gt/debugfs_gt.h"
#include "intel_guc.h"
#include "intel_guc_debugfs.h"
#include "intel_guc_log_debugfs.h"

static int guc_info_show(struct seq_file *m, void *data)
{
	struct intel_guc *guc = m->private;
	struct drm_printer p = drm_seq_file_printer(m);

	if (!intel_guc_is_supported(guc))
		return -ENODEV;

	intel_guc_load_status(guc, &p);
	drm_puts(&p, "\n");
	intel_guc_log_info(&guc->log, &p);

	/* Add more as required ... */

	return 0;
}
DEFINE_GT_DEBUGFS_ATTRIBUTE(guc_info);

void intel_guc_debugfs_register(struct intel_guc *guc, struct dentry *root)
{
	static const struct debugfs_gt_file files[] = {
		{ "guc_info", &guc_info_fops, NULL },
	};

	if (!intel_guc_is_supported(guc))
		return;

	intel_gt_debugfs_register_files(root, files, ARRAY_SIZE(files), guc);
	intel_guc_log_debugfs_register(&guc->log, root);
}
