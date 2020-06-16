// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include <linux/debugfs.h>

#include "intel_guc_debugfs.h"
#include "intel_huc_debugfs.h"
#include "intel_uc.h"
#include "intel_uc_debugfs.h"

void intel_uc_debugfs_register(struct intel_uc *uc, struct dentry *gt_root)
{
	struct dentry *root;

	if (!gt_root)
		return;

	/* GuC and HuC go always in pair, no need to check both */
	if (!intel_uc_supports_guc(uc))
		return;

	root = debugfs_create_dir("uc", gt_root);
	if (IS_ERR(root))
		return;

	intel_guc_debugfs_register(&uc->guc, root);
	intel_huc_debugfs_register(&uc->huc, root);
}
