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
#include "xe_ggtt.h"
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
#include "xe_macros.h"
#include "xe_mocs.h"
#include "xe_pat.h"
#include "xe_pm.h"
#include "xe_reg_sr.h"
#include "xe_reg_whitelist.h"
#include "xe_sriov.h"
#include "xe_tuning.h"
#include "xe_uc_debugfs.h"
#include "xe_wa.h"

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
	struct dentry *parent = node->dent->d_parent;
	struct xe_gt *gt = parent->d_inode->i_private;
	int (*print)(struct xe_gt *, struct drm_printer *) = node->info_ent->data;

	if (WARN_ON(!print))
		return -EINVAL;

	return print(gt, &p);
}

static int hw_engines(struct xe_gt *gt, struct drm_printer *p)
{
	struct xe_device *xe = gt_to_xe(gt);
	struct xe_hw_engine *hwe;
	enum xe_hw_engine_id id;
	unsigned int fw_ref;
	int ret = 0;

	xe_pm_runtime_get(xe);
	fw_ref = xe_force_wake_get(gt_to_fw(gt), XE_FORCEWAKE_ALL);
	if (!xe_force_wake_ref_has_domain(fw_ref, XE_FORCEWAKE_ALL)) {
		ret = -ETIMEDOUT;
		goto fw_put;
	}

	for_each_hw_engine(hwe, gt, id)
		xe_hw_engine_print(hwe, p);

fw_put:
	xe_force_wake_put(gt_to_fw(gt), fw_ref);
	xe_pm_runtime_put(xe);

	return ret;
}

static int powergate_info(struct xe_gt *gt, struct drm_printer *p)
{
	int ret;

	xe_pm_runtime_get(gt_to_xe(gt));
	ret = xe_gt_idle_pg_print(gt, p);
	xe_pm_runtime_put(gt_to_xe(gt));

	return ret;
}

static int force_reset(struct xe_gt *gt, struct drm_printer *p)
{
	xe_pm_runtime_get(gt_to_xe(gt));
	xe_gt_reset_async(gt);
	xe_pm_runtime_put(gt_to_xe(gt));

	return 0;
}

static int force_reset_sync(struct xe_gt *gt, struct drm_printer *p)
{
	xe_pm_runtime_get(gt_to_xe(gt));
	xe_gt_reset(gt);
	xe_pm_runtime_put(gt_to_xe(gt));

	return 0;
}

static int sa_info(struct xe_gt *gt, struct drm_printer *p)
{
	struct xe_tile *tile = gt_to_tile(gt);

	xe_pm_runtime_get(gt_to_xe(gt));
	drm_suballoc_dump_debug_info(&tile->mem.kernel_bb_pool->base, p,
				     tile->mem.kernel_bb_pool->gpu_addr);
	xe_pm_runtime_put(gt_to_xe(gt));

	return 0;
}

static int topology(struct xe_gt *gt, struct drm_printer *p)
{
	xe_pm_runtime_get(gt_to_xe(gt));
	xe_gt_topology_dump(gt, p);
	xe_pm_runtime_put(gt_to_xe(gt));

	return 0;
}

static int steering(struct xe_gt *gt, struct drm_printer *p)
{
	xe_pm_runtime_get(gt_to_xe(gt));
	xe_gt_mcr_steering_dump(gt, p);
	xe_pm_runtime_put(gt_to_xe(gt));

	return 0;
}

static int ggtt(struct xe_gt *gt, struct drm_printer *p)
{
	int ret;

	xe_pm_runtime_get(gt_to_xe(gt));
	ret = xe_ggtt_dump(gt_to_tile(gt)->mem.ggtt, p);
	xe_pm_runtime_put(gt_to_xe(gt));

	return ret;
}

static int register_save_restore(struct xe_gt *gt, struct drm_printer *p)
{
	struct xe_hw_engine *hwe;
	enum xe_hw_engine_id id;

	xe_pm_runtime_get(gt_to_xe(gt));

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

	xe_pm_runtime_put(gt_to_xe(gt));

	return 0;
}

static int workarounds(struct xe_gt *gt, struct drm_printer *p)
{
	xe_pm_runtime_get(gt_to_xe(gt));
	xe_wa_dump(gt, p);
	xe_pm_runtime_put(gt_to_xe(gt));

	return 0;
}

static int tunings(struct xe_gt *gt, struct drm_printer *p)
{
	xe_pm_runtime_get(gt_to_xe(gt));
	xe_tuning_dump(gt, p);
	xe_pm_runtime_put(gt_to_xe(gt));

	return 0;
}

static int pat(struct xe_gt *gt, struct drm_printer *p)
{
	xe_pm_runtime_get(gt_to_xe(gt));
	xe_pat_dump(gt, p);
	xe_pm_runtime_put(gt_to_xe(gt));

	return 0;
}

static int mocs(struct xe_gt *gt, struct drm_printer *p)
{
	xe_pm_runtime_get(gt_to_xe(gt));
	xe_mocs_dump(gt, p);
	xe_pm_runtime_put(gt_to_xe(gt));

	return 0;
}

static int rcs_default_lrc(struct xe_gt *gt, struct drm_printer *p)
{
	xe_pm_runtime_get(gt_to_xe(gt));
	xe_lrc_dump_default(p, gt, XE_ENGINE_CLASS_RENDER);
	xe_pm_runtime_put(gt_to_xe(gt));

	return 0;
}

static int ccs_default_lrc(struct xe_gt *gt, struct drm_printer *p)
{
	xe_pm_runtime_get(gt_to_xe(gt));
	xe_lrc_dump_default(p, gt, XE_ENGINE_CLASS_COMPUTE);
	xe_pm_runtime_put(gt_to_xe(gt));

	return 0;
}

static int bcs_default_lrc(struct xe_gt *gt, struct drm_printer *p)
{
	xe_pm_runtime_get(gt_to_xe(gt));
	xe_lrc_dump_default(p, gt, XE_ENGINE_CLASS_COPY);
	xe_pm_runtime_put(gt_to_xe(gt));

	return 0;
}

static int vcs_default_lrc(struct xe_gt *gt, struct drm_printer *p)
{
	xe_pm_runtime_get(gt_to_xe(gt));
	xe_lrc_dump_default(p, gt, XE_ENGINE_CLASS_VIDEO_DECODE);
	xe_pm_runtime_put(gt_to_xe(gt));

	return 0;
}

static int vecs_default_lrc(struct xe_gt *gt, struct drm_printer *p)
{
	xe_pm_runtime_get(gt_to_xe(gt));
	xe_lrc_dump_default(p, gt, XE_ENGINE_CLASS_VIDEO_ENHANCE);
	xe_pm_runtime_put(gt_to_xe(gt));

	return 0;
}

static int hwconfig(struct xe_gt *gt, struct drm_printer *p)
{
	xe_pm_runtime_get(gt_to_xe(gt));
	xe_guc_hwconfig_dump(&gt->uc.guc, p);
	xe_pm_runtime_put(gt_to_xe(gt));

	return 0;
}

static const struct drm_info_list debugfs_list[] = {
	{"hw_engines", .show = xe_gt_debugfs_simple_show, .data = hw_engines},
	{"force_reset", .show = xe_gt_debugfs_simple_show, .data = force_reset},
	{"force_reset_sync", .show = xe_gt_debugfs_simple_show, .data = force_reset_sync},
	{"sa_info", .show = xe_gt_debugfs_simple_show, .data = sa_info},
	{"topology", .show = xe_gt_debugfs_simple_show, .data = topology},
	{"steering", .show = xe_gt_debugfs_simple_show, .data = steering},
	{"ggtt", .show = xe_gt_debugfs_simple_show, .data = ggtt},
	{"powergate_info", .show = xe_gt_debugfs_simple_show, .data = powergate_info},
	{"register-save-restore", .show = xe_gt_debugfs_simple_show, .data = register_save_restore},
	{"workarounds", .show = xe_gt_debugfs_simple_show, .data = workarounds},
	{"tunings", .show = xe_gt_debugfs_simple_show, .data = tunings},
	{"pat", .show = xe_gt_debugfs_simple_show, .data = pat},
	{"mocs", .show = xe_gt_debugfs_simple_show, .data = mocs},
	{"default_lrc_rcs", .show = xe_gt_debugfs_simple_show, .data = rcs_default_lrc},
	{"default_lrc_ccs", .show = xe_gt_debugfs_simple_show, .data = ccs_default_lrc},
	{"default_lrc_bcs", .show = xe_gt_debugfs_simple_show, .data = bcs_default_lrc},
	{"default_lrc_vcs", .show = xe_gt_debugfs_simple_show, .data = vcs_default_lrc},
	{"default_lrc_vecs", .show = xe_gt_debugfs_simple_show, .data = vecs_default_lrc},
	{"stats", .show = xe_gt_debugfs_simple_show, .data = xe_gt_stats_print_info},
	{"hwconfig", .show = xe_gt_debugfs_simple_show, .data = hwconfig},
};

void xe_gt_debugfs_register(struct xe_gt *gt)
{
	struct xe_device *xe = gt_to_xe(gt);
	struct drm_minor *minor = gt_to_xe(gt)->drm.primary;
	struct dentry *root;
	char name[8];

	xe_gt_assert(gt, minor->debugfs_root);

	snprintf(name, sizeof(name), "gt%d", gt->info.id);
	root = debugfs_create_dir(name, minor->debugfs_root);
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

	drm_debugfs_create_files(debugfs_list,
				 ARRAY_SIZE(debugfs_list),
				 root, minor);

	xe_uc_debugfs_register(&gt->uc, root);

	if (IS_SRIOV_PF(xe))
		xe_gt_sriov_pf_debugfs_register(gt, root);
	else if (IS_SRIOV_VF(xe))
		xe_gt_sriov_vf_debugfs_register(gt, root);
}
