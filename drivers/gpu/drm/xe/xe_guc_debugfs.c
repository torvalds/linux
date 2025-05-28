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
#include "xe_guc_pc.h"
#include "xe_macros.h"
#include "xe_pm.h"

/*
 * guc_debugfs_show - A show callback for struct drm_info_list
 * @m: the &seq_file
 * @data: data used by the drm debugfs helpers
 *
 * This callback can be used in struct drm_info_list to describe debugfs
 * files that are &xe_guc specific in similar way how we handle &xe_gt
 * specific files using &xe_gt_debugfs_simple_show.
 *
 * It is assumed that those debugfs files will be created on directory entry
 * which grandparent struct dentry d_inode->i_private points to &xe_gt.
 *
 *      /sys/kernel/debug/dri/0/
 *      ├── gt0			# dent->d_parent->d_parent (d_inode->i_private == gt)
 *      │   ├── uc		# dent->d_parent
 *      │   │   ├── guc_info	# dent
 *      │   │   ├── guc_...
 *
 * This function assumes that &m->private will be set to the &struct
 * drm_info_node corresponding to the instance of the info on a given &struct
 * drm_minor (see struct drm_info_list.show for details).
 *
 * This function also assumes that struct drm_info_list.data will point to the
 * function code that will actually print a file content::
 *
 *    int (*print)(struct xe_guc *, struct drm_printer *)
 *
 * Example::
 *
 *    int foo(struct xe_guc *guc, struct drm_printer *p)
 *    {
 *        drm_printf(p, "enabled %d\n", guc->submission_state.enabled);
 *        return 0;
 *    }
 *
 *    static const struct drm_info_list bar[] = {
 *        { name = "foo", .show = guc_debugfs_show, .data = foo },
 *    };
 *
 *    parent = debugfs_create_dir("uc", gtdir);
 *    drm_debugfs_create_files(bar, ARRAY_SIZE(bar), parent, minor);
 *
 * Return: 0 on success or a negative error code on failure.
 */
static int guc_debugfs_show(struct seq_file *m, void *data)
{
	struct drm_printer p = drm_seq_file_printer(m);
	struct drm_info_node *node = m->private;
	struct dentry *parent = node->dent->d_parent;
	struct dentry *grandparent = parent->d_parent;
	struct xe_gt *gt = grandparent->d_inode->i_private;
	struct xe_device *xe = gt_to_xe(gt);
	int (*print)(struct xe_guc *, struct drm_printer *) = node->info_ent->data;
	int ret;

	xe_pm_runtime_get(xe);
	ret = print(&gt->uc.guc, &p);
	xe_pm_runtime_put(xe);

	return ret;
}

static int guc_log(struct xe_guc *guc, struct drm_printer *p)
{
	xe_guc_log_print(&guc->log, p);
	return 0;
}

static int guc_log_dmesg(struct xe_guc *guc, struct drm_printer *p)
{
	xe_guc_log_print_dmesg(&guc->log);
	return 0;
}

static int guc_ctb(struct xe_guc *guc, struct drm_printer *p)
{
	xe_guc_ct_print(&guc->ct, p, true);
	return 0;
}

static int guc_pc(struct xe_guc *guc, struct drm_printer *p)
{
	xe_guc_pc_print(&guc->pc, p);
	return 0;
}

/*
 * only for GuC debugfs files which can be safely used on the VF as well:
 * - without access to the GuC privileged registers
 * - without access to the PF specific GuC objects
 */
static const struct drm_info_list vf_safe_debugfs_list[] = {
	{ "guc_info", .show = guc_debugfs_show, .data = xe_guc_print_info },
	{ "guc_ctb", .show = guc_debugfs_show, .data = guc_ctb },
};

/* For GuC debugfs files that require the SLPC support */
static const struct drm_info_list slpc_debugfs_list[] = {
	{ "guc_pc", .show = guc_debugfs_show, .data = guc_pc },
};

/* everything else should be added here */
static const struct drm_info_list pf_only_debugfs_list[] = {
	{ "guc_log", .show = guc_debugfs_show, .data = guc_log },
	{ "guc_log_dmesg", .show = guc_debugfs_show, .data = guc_log_dmesg },
};

void xe_guc_debugfs_register(struct xe_guc *guc, struct dentry *parent)
{
	struct xe_device *xe =  guc_to_xe(guc);
	struct drm_minor *minor = xe->drm.primary;

	drm_debugfs_create_files(vf_safe_debugfs_list,
				 ARRAY_SIZE(vf_safe_debugfs_list),
				 parent, minor);

	if (!IS_SRIOV_VF(xe)) {
		drm_debugfs_create_files(pf_only_debugfs_list,
					 ARRAY_SIZE(pf_only_debugfs_list),
					 parent, minor);

		if (!xe->info.skip_guc_pc)
			drm_debugfs_create_files(slpc_debugfs_list,
						 ARRAY_SIZE(slpc_debugfs_list),
						 parent, minor);
	}
}
