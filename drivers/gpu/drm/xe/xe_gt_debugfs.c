// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_gt_debugfs.h"

#include <linux/debugfs.h>

#include <drm/drm_debugfs.h>
#include <drm/drm_managed.h>

#include "xe_device.h"
#include "xe_force_wake.h"
#include "xe_gt.h"
#include "xe_gt_mcr.h"
#include "xe_gt_idle.h"
#include "xe_gt_sriov_pf_debugfs.h"
#include "xe_gt_sriov_vf_debugfs.h"
#include "xe_gt_stats.h"
#include "xe_gt_topology.h"
#include "xe_guc_hwconfig.h"
#include "xe_hw_engine.h"
#include "xe_lrc.h"
#include "xe_mocs.h"
#include "xe_pat.h"
#include "xe_pm.h"
#include "xe_reg_sr.h"
#include "xe_reg_whitelist.h"
#include "xe_sa.h"
#include "xe_sriov.h"
#include "xe_sriov_vf_ccs.h"
#include "xe_tuning.h"
#include "xe_uc_debugfs.h"
#include "xe_wa.h"

static struct xe_gt *node_to_gt(struct drm_info_node *node)
{
	return node->dent->d_parent->d_inode->i_private;
}

/**
 * xe_gt_debugfs_simple_show - A show callback for struct drm_info_list
 * @m: the &seq_file
 * @data: data used by the drm debugfs helpers
 *
 * This callback can be used in struct drm_info_list to describe debugfs
 * files that are &xe_gt specific.
 *
 * It is assumed that those debugfs files will be created on directory entry
 * which struct dentry d_inode->i_private points to &xe_gt.
 *
 * This function assumes that &m->private will be set to the &struct
 * drm_info_node corresponding to the instance of the info on a given &struct
 * drm_minor (see struct drm_info_list.show for details).
 *
 * This function also assumes that struct drm_info_list.data will point to the
 * function code that will actually print a file content::
 *
 *   int (*print)(struct xe_gt *, struct drm_printer *)
 *
 * Example::
 *
 *    int foo(struct xe_gt *gt, struct drm_printer *p)
 *    {
 *        drm_printf(p, "GT%u\n", gt->info.id);
 *        return 0;
 *    }
 *
 *    static const struct drm_info_list bar[] = {
 *        { name = "foo", .show = xe_gt_debugfs_simple_show, .data = foo },
 *    };
 *
 *    dir = debugfs_create_dir("gt", parent);
 *    dir->d_inode->i_private = gt;
 *    drm_debugfs_create_files(bar, ARRAY_SIZE(bar), dir, minor);
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_gt_debugfs_simple_show(struct seq_file *m, void *data)
{
	struct drm_printer p = drm_seq_file_printer(m);
	struct drm_info_node *node = m->private;
	struct xe_gt *gt = node_to_gt(node);
	int (*print)(struct xe_gt *, struct drm_printer *) = node->info_ent->data;

	if (WARN_ON(!print))
		return -EINVAL;

	return print(gt, &p);
}

/**
 * xe_gt_debugfs_show_with_rpm - A show callback for struct drm_info_list
 * @m: the &seq_file
 * @data: data used by the drm debugfs helpers
 *
 * Similar to xe_gt_debugfs_simple_show() but implicitly takes a RPM ref.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_gt_debugfs_show_with_rpm(struct seq_file *m, void *data)
{
	struct drm_info_node *node = m->private;
	struct xe_gt *gt = node_to_gt(node);
	struct xe_device *xe = gt_to_xe(gt);

	guard(xe_pm_runtime)(xe);
	return xe_gt_debugfs_simple_show(m, data);
}

static int hw_engines(struct xe_gt *gt, struct drm_printer *p)
{
	struct xe_hw_engine *hwe;
	enum xe_hw_engine_id id;

	CLASS(xe_force_wake, fw_ref)(gt_to_fw(gt), XE_FORCEWAKE_ALL);
	if (!xe_force_wake_ref_has_domain(fw_ref.domains, XE_FORCEWAKE_ALL))
		return -ETIMEDOUT;

	for_each_hw_engine(hwe, gt, id)
		xe_hw_engine_print(hwe, p);

	return 0;
}

static int steering(struct xe_gt *gt, struct drm_printer *p)
{
	xe_gt_mcr_steering_dump(gt, p);
	return 0;
}

static int register_save_restore(struct xe_gt *gt, struct drm_printer *p)
{
	struct xe_hw_engine *hwe;
	enum xe_hw_engine_id id;

	xe_reg_sr_dump(&gt->reg_sr, p);
	drm_printf(p, "\n");

	drm_printf(p, "Engine\n");
	for_each_hw_engine(hwe, gt, id)
		xe_reg_sr_dump(&hwe->reg_sr, p);
	drm_printf(p, "\n");

	drm_printf(p, "LRC\n");
	for_each_hw_engine(hwe, gt, id)
		xe_reg_sr_dump(&hwe->reg_lrc, p);
	drm_printf(p, "\n");

	drm_printf(p, "Whitelist\n");
	for_each_hw_engine(hwe, gt, id)
		xe_reg_whitelist_dump(&hwe->reg_whitelist, p);

	return 0;
}

static int rcs_default_lrc(struct xe_gt *gt, struct drm_printer *p)
{
	xe_lrc_dump_default(p, gt, XE_ENGINE_CLASS_RENDER);
	return 0;
}

static int ccs_default_lrc(struct xe_gt *gt, struct drm_printer *p)
{
	xe_lrc_dump_default(p, gt, XE_ENGINE_CLASS_COMPUTE);
	return 0;
}

static int bcs_default_lrc(struct xe_gt *gt, struct drm_printer *p)
{
	xe_lrc_dump_default(p, gt, XE_ENGINE_CLASS_COPY);
	return 0;
}

static int vcs_default_lrc(struct xe_gt *gt, struct drm_printer *p)
{
	xe_lrc_dump_default(p, gt, XE_ENGINE_CLASS_VIDEO_DECODE);
	return 0;
}

static int vecs_default_lrc(struct xe_gt *gt, struct drm_printer *p)
{
	xe_lrc_dump_default(p, gt, XE_ENGINE_CLASS_VIDEO_ENHANCE);
	return 0;
}

static int hwconfig(struct xe_gt *gt, struct drm_printer *p)
{
	xe_guc_hwconfig_dump(&gt->uc.guc, p);
	return 0;
}

/*
 * only for GT debugfs files which can be safely used on the VF as well:
 * - without access to the GT privileged registers
 * - without access to the PF specific data
 */
static const struct drm_info_list vf_safe_debugfs_list[] = {
	{ "topology", .show = xe_gt_debugfs_show_with_rpm, .data = xe_gt_topology_dump },
	{ "register-save-restore",
		.show = xe_gt_debugfs_show_with_rpm, .data = register_save_restore },
	{ "workarounds", .show = xe_gt_debugfs_show_with_rpm, .data = xe_wa_gt_dump },
	{ "tunings", .show = xe_gt_debugfs_show_with_rpm, .data = xe_tuning_dump },
	{ "default_lrc_rcs", .show = xe_gt_debugfs_show_with_rpm, .data = rcs_default_lrc },
	{ "default_lrc_ccs", .show = xe_gt_debugfs_show_with_rpm, .data = ccs_default_lrc },
	{ "default_lrc_bcs", .show = xe_gt_debugfs_show_with_rpm, .data = bcs_default_lrc },
	{ "default_lrc_vcs", .show = xe_gt_debugfs_show_with_rpm, .data = vcs_default_lrc },
	{ "default_lrc_vecs", .show = xe_gt_debugfs_show_with_rpm, .data = vecs_default_lrc },
	{ "hwconfig", .show = xe_gt_debugfs_show_with_rpm, .data = hwconfig },
	{ "pat_sw_config", .show = xe_gt_debugfs_simple_show, .data = xe_pat_dump_sw_config },
};

/* everything else should be added here */
static const struct drm_info_list pf_only_debugfs_list[] = {
	{ "hw_engines", .show = xe_gt_debugfs_show_with_rpm, .data = hw_engines },
	{ "mocs", .show = xe_gt_debugfs_show_with_rpm, .data = xe_mocs_dump },
	{ "pat", .show = xe_gt_debugfs_show_with_rpm, .data = xe_pat_dump },
	{ "powergate_info", .show = xe_gt_debugfs_show_with_rpm, .data = xe_gt_idle_pg_print },
	{ "steering", .show = xe_gt_debugfs_show_with_rpm, .data = steering },
};

static ssize_t write_to_gt_call(const char __user *userbuf, size_t count, loff_t *ppos,
				void (*call)(struct xe_gt *), struct xe_gt *gt)
{
	bool yes;
	int ret;

	if (*ppos)
		return -EINVAL;
	ret = kstrtobool_from_user(userbuf, count, &yes);
	if (ret < 0)
		return ret;
	if (yes)
		call(gt);
	return count;
}

static ssize_t stats_write(struct file *file, const char __user *userbuf,
			   size_t count, loff_t *ppos)
{
	struct seq_file *s = file->private_data;
	struct xe_gt *gt = s->private;

	return write_to_gt_call(userbuf, count, ppos, xe_gt_stats_clear, gt);
}

static int stats_show(struct seq_file *s, void *unused)
{
	struct drm_printer p = drm_seq_file_printer(s);
	struct xe_gt *gt = s->private;

	return xe_gt_stats_print_info(gt, &p);
}
DEFINE_SHOW_STORE_ATTRIBUTE(stats);

static void force_reset(struct xe_gt *gt)
{
	struct xe_device *xe = gt_to_xe(gt);

	guard(xe_pm_runtime)(xe);
	xe_gt_reset_async(gt);
}

static ssize_t force_reset_write(struct file *file,
				 const char __user *userbuf,
				 size_t count, loff_t *ppos)
{
	struct seq_file *s = file->private_data;
	struct xe_gt *gt = s->private;

	return write_to_gt_call(userbuf, count, ppos, force_reset, gt);
}

static int force_reset_show(struct seq_file *s, void *unused)
{
	struct xe_gt *gt = s->private;

	force_reset(gt); /* to be deprecated! */
	return 0;
}
DEFINE_SHOW_STORE_ATTRIBUTE(force_reset);

static void force_reset_sync(struct xe_gt *gt)
{
	struct xe_device *xe = gt_to_xe(gt);

	guard(xe_pm_runtime)(xe);
	xe_gt_reset(gt);
}

static ssize_t force_reset_sync_write(struct file *file,
				      const char __user *userbuf,
				      size_t count, loff_t *ppos)
{
	struct seq_file *s = file->private_data;
	struct xe_gt *gt = s->private;

	return write_to_gt_call(userbuf, count, ppos, force_reset_sync, gt);
}

static int force_reset_sync_show(struct seq_file *s, void *unused)
{
	struct xe_gt *gt = s->private;

	force_reset_sync(gt); /* to be deprecated! */
	return 0;
}
DEFINE_SHOW_STORE_ATTRIBUTE(force_reset_sync);

void xe_gt_debugfs_register(struct xe_gt *gt)
{
	struct xe_device *xe = gt_to_xe(gt);
	struct drm_minor *minor = gt_to_xe(gt)->drm.primary;
	struct dentry *parent = gt->tile->debugfs;
	struct dentry *root;
	char symlink[16];
	char name[8];

	xe_gt_assert(gt, minor->debugfs_root);

	if (IS_ERR(parent))
		return;

	snprintf(name, sizeof(name), "gt%d", gt->info.id);
	root = debugfs_create_dir(name, parent);
	if (IS_ERR(root)) {
		drm_warn(&xe->drm, "Create GT directory failed");
		return;
	}

	/*
	 * Store the xe_gt pointer as private data of the gt/ directory node
	 * so other GT specific attributes under that directory may refer to
	 * it by looking at its parent node private data.
	 */
	root->d_inode->i_private = gt;

	/* VF safe */
	debugfs_create_file("stats", 0600, root, gt, &stats_fops);
	debugfs_create_file("force_reset", 0600, root, gt, &force_reset_fops);
	debugfs_create_file("force_reset_sync", 0600, root, gt, &force_reset_sync_fops);

	drm_debugfs_create_files(vf_safe_debugfs_list,
				 ARRAY_SIZE(vf_safe_debugfs_list),
				 root, minor);

	if (!IS_SRIOV_VF(xe))
		drm_debugfs_create_files(pf_only_debugfs_list,
					 ARRAY_SIZE(pf_only_debugfs_list),
					 root, minor);

	xe_uc_debugfs_register(&gt->uc, root);

	if (IS_SRIOV_PF(xe))
		xe_gt_sriov_pf_debugfs_register(gt, root);
	else if (IS_SRIOV_VF(xe))
		xe_gt_sriov_vf_debugfs_register(gt, root);

	/*
	 * Backwards compatibility only: create a link for the legacy clients
	 * who may expect gt/ directory at the root level, not the tile level.
	 */
	snprintf(symlink, sizeof(symlink), "tile%u/%s", gt->tile->id, name);
	debugfs_create_symlink(name, minor->debugfs_root, symlink);
}
