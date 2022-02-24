// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include <linux/debugfs.h>
#include <drm/drm_print.h>

#include "gt/intel_gt_debugfs.h"
#include "intel_guc_debugfs.h"
#include "intel_huc_debugfs.h"
#include "intel_uc.h"
#include "intel_uc_debugfs.h"

static int uc_usage_show(struct seq_file *m, void *data)
{
	struct intel_uc *uc = m->private;
	struct drm_printer p = drm_seq_file_printer(m);

	drm_printf(&p, "[guc] supported:%s wanted:%s used:%s\n",
		   yesno(intel_uc_supports_guc(uc)),
		   yesno(intel_uc_wants_guc(uc)),
		   yesno(intel_uc_uses_guc(uc)));
	drm_printf(&p, "[huc] supported:%s wanted:%s used:%s\n",
		   yesno(intel_uc_supports_huc(uc)),
		   yesno(intel_uc_wants_huc(uc)),
		   yesno(intel_uc_uses_huc(uc)));
	drm_printf(&p, "[submission] supported:%s wanted:%s used:%s\n",
		   yesno(intel_uc_supports_guc_submission(uc)),
		   yesno(intel_uc_wants_guc_submission(uc)),
		   yesno(intel_uc_uses_guc_submission(uc)));

	return 0;
}
DEFINE_INTEL_GT_DEBUGFS_ATTRIBUTE(uc_usage);

void intel_uc_debugfs_register(struct intel_uc *uc, struct dentry *gt_root)
{
	static const struct intel_gt_debugfs_file files[] = {
		{ "usage", &uc_usage_fops, NULL },
	};
	struct dentry *root;

	if (!gt_root)
		return;

	/* GuC and HuC go always in pair, no need to check both */
	if (!intel_uc_supports_guc(uc))
		return;

	root = debugfs_create_dir("uc", gt_root);
	if (IS_ERR(root))
		return;

	intel_gt_debugfs_register_files(root, files, ARRAY_SIZE(files), uc);

	intel_guc_debugfs_register(&uc->guc, root);
	intel_huc_debugfs_register(&uc->huc, root);
}
