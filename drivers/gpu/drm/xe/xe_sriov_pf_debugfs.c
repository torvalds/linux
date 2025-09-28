// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include <linux/debugfs.h>
#include <drm/drm_debugfs.h>

#include "xe_device_types.h"
#include "xe_sriov_pf.h"
#include "xe_sriov_pf_debugfs.h"
#include "xe_sriov_pf_service.h"

static int simple_show(struct seq_file *m, void *data)
{
	struct drm_printer p = drm_seq_file_printer(m);
	struct drm_info_node *node = m->private;
	struct dentry *parent = node->dent->d_parent;
	struct xe_device *xe = parent->d_inode->i_private;
	void (*print)(struct xe_device *, struct drm_printer *) = node->info_ent->data;

	print(xe, &p);
	return 0;
}

static const struct drm_info_list debugfs_list[] = {
	{ .name = "vfs", .show = simple_show, .data = xe_sriov_pf_print_vfs_summary },
	{ .name = "versions", .show = simple_show, .data = xe_sriov_pf_service_print_versions },
};

/**
 * xe_sriov_pf_debugfs_register - Register PF debugfs attributes.
 * @xe: the &xe_device
 * @root: the root &dentry
 *
 * Prepare debugfs attributes exposed by the PF.
 */
void xe_sriov_pf_debugfs_register(struct xe_device *xe, struct dentry *root)
{
	struct drm_minor *minor = xe->drm.primary;
	struct dentry *parent;

	/*
	 *      /sys/kernel/debug/dri/0/
	 *      ├── pf
	 *      │   ├── ...
	 */
	parent = debugfs_create_dir("pf", root);
	if (IS_ERR(parent))
		return;
	parent->d_inode->i_private = xe;

	drm_debugfs_create_files(debugfs_list, ARRAY_SIZE(debugfs_list), parent, minor);
}
