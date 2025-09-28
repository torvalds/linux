// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include <linux/debugfs.h>
#include <drm/drm_debugfs.h>

#include "xe_device.h"
#include "xe_device_types.h"
#include "xe_sriov_pf.h"
#include "xe_sriov_pf_debugfs.h"
#include "xe_sriov_pf_helpers.h"
#include "xe_sriov_pf_service.h"
#include "xe_sriov_printk.h"
#include "xe_tile_sriov_pf_debugfs.h"

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

static void pf_populate_pf(struct xe_device *xe, struct dentry *pfdent)
{
	struct drm_minor *minor = xe->drm.primary;

	drm_debugfs_create_files(debugfs_list, ARRAY_SIZE(debugfs_list), pfdent, minor);
}

static void pf_populate_with_tiles(struct xe_device *xe, struct dentry *dent, unsigned int vfid)
{
	struct xe_tile *tile;
	unsigned int id;

	for_each_tile(tile, xe, id)
		xe_tile_sriov_pf_debugfs_populate(tile, dent, vfid);
}

/**
 * xe_sriov_pf_debugfs_register - Register PF debugfs attributes.
 * @xe: the &xe_device
 * @root: the root &dentry
 *
 * Create separate directory that will contain all SR-IOV related files,
 * organized per each SR-IOV function (PF, VF1, VF2, ..., VFn).
 */
void xe_sriov_pf_debugfs_register(struct xe_device *xe, struct dentry *root)
{
	int totalvfs = xe_sriov_pf_get_totalvfs(xe);
	struct dentry *pfdent;
	struct dentry *vfdent;
	struct dentry *dent;
	char vfname[16]; /* should be more than enough for "vf%u\0" and VFID(UINT_MAX) */
	unsigned int n;

	/*
	 *      /sys/kernel/debug/dri/BDF/
	 *      ├── sriov		# d_inode->i_private = (xe_device*)
	 *      │   ├── ...
	 */
	dent = debugfs_create_dir("sriov", root);
	if (IS_ERR(dent))
		return;
	dent->d_inode->i_private = xe;

	/*
	 *      /sys/kernel/debug/dri/BDF/
	 *      ├── sriov		# d_inode->i_private = (xe_device*)
	 *      │   ├── pf		# d_inode->i_private = (xe_device*)
	 *      │   │   ├── ...
	 */
	pfdent = debugfs_create_dir("pf", dent);
	if (IS_ERR(pfdent))
		return;
	pfdent->d_inode->i_private = xe;

	pf_populate_pf(xe, pfdent);
	pf_populate_with_tiles(xe, pfdent, PFID);

	/*
	 *      /sys/kernel/debug/dri/BDF/
	 *      ├── sriov		# d_inode->i_private = (xe_device*)
	 *      │   ├── vf1		# d_inode->i_private = VFID(1)
	 *      │   ├── vf2		# d_inode->i_private = VFID(2)
	 *      │   ├── ...
	 */
	for (n = 1; n <= totalvfs; n++) {
		snprintf(vfname, sizeof(vfname), "vf%u", VFID(n));
		vfdent = debugfs_create_dir(vfname, dent);
		if (IS_ERR(vfdent))
			return;
		vfdent->d_inode->i_private = (void *)(uintptr_t)VFID(n);

		pf_populate_with_tiles(xe, vfdent, VFID(n));
	}
}
