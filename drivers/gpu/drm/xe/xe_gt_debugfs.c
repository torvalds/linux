// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_gt_debugfs.h"

#include <drm/drm_debugfs.h>
#include <drm/drm_managed.h>

#include "xe_device.h"
#include "xe_force_wake.h"
#include "xe_ggtt.h"
#include "xe_gt.h"
#include "xe_gt_mcr.h"
#include "xe_gt_topology.h"
#include "xe_hw_engine.h"
#include "xe_lrc.h"
#include "xe_macros.h"
#include "xe_pat.h"
#include "xe_reg_sr.h"
#include "xe_reg_whitelist.h"
#include "xe_uc_debugfs.h"
#include "xe_wa.h"

static struct xe_gt *node_to_gt(struct drm_info_node *node)
{
	return node->info_ent->data;
}

static int hw_engines(struct seq_file *m, void *data)
{
	struct xe_gt *gt = node_to_gt(m->private);
	struct xe_device *xe = gt_to_xe(gt);
	struct drm_printer p = drm_seq_file_printer(m);
	struct xe_hw_engine *hwe;
	enum xe_hw_engine_id id;
	int err;

	xe_device_mem_access_get(xe);
	err = xe_force_wake_get(gt_to_fw(gt), XE_FORCEWAKE_ALL);
	if (err) {
		xe_device_mem_access_put(xe);
		return err;
	}

	for_each_hw_engine(hwe, gt, id)
		xe_hw_engine_print(hwe, &p);

	err = xe_force_wake_put(gt_to_fw(gt), XE_FORCEWAKE_ALL);
	xe_device_mem_access_put(xe);
	if (err)
		return err;

	return 0;
}

static int force_reset(struct seq_file *m, void *data)
{
	struct xe_gt *gt = node_to_gt(m->private);

	xe_gt_reset_async(gt);

	return 0;
}

static int sa_info(struct seq_file *m, void *data)
{
	struct xe_tile *tile = gt_to_tile(node_to_gt(m->private));
	struct drm_printer p = drm_seq_file_printer(m);

	drm_suballoc_dump_debug_info(&tile->mem.kernel_bb_pool->base, &p,
				     tile->mem.kernel_bb_pool->gpu_addr);

	return 0;
}

static int topology(struct seq_file *m, void *data)
{
	struct xe_gt *gt = node_to_gt(m->private);
	struct drm_printer p = drm_seq_file_printer(m);

	xe_gt_topology_dump(gt, &p);

	return 0;
}

static int steering(struct seq_file *m, void *data)
{
	struct xe_gt *gt = node_to_gt(m->private);
	struct drm_printer p = drm_seq_file_printer(m);

	xe_gt_mcr_steering_dump(gt, &p);

	return 0;
}

static int ggtt(struct seq_file *m, void *data)
{
	struct xe_gt *gt = node_to_gt(m->private);
	struct drm_printer p = drm_seq_file_printer(m);

	return xe_ggtt_dump(gt_to_tile(gt)->mem.ggtt, &p);
}

static int register_save_restore(struct seq_file *m, void *data)
{
	struct xe_gt *gt = node_to_gt(m->private);
	struct drm_printer p = drm_seq_file_printer(m);
	struct xe_hw_engine *hwe;
	enum xe_hw_engine_id id;

	xe_reg_sr_dump(&gt->reg_sr, &p);
	drm_printf(&p, "\n");

	drm_printf(&p, "Engine\n");
	for_each_hw_engine(hwe, gt, id)
		xe_reg_sr_dump(&hwe->reg_sr, &p);
	drm_printf(&p, "\n");

	drm_printf(&p, "LRC\n");
	for_each_hw_engine(hwe, gt, id)
		xe_reg_sr_dump(&hwe->reg_lrc, &p);
	drm_printf(&p, "\n");

	drm_printf(&p, "Whitelist\n");
	for_each_hw_engine(hwe, gt, id)
		xe_reg_whitelist_dump(&hwe->reg_whitelist, &p);

	return 0;
}

static int workarounds(struct seq_file *m, void *data)
{
	struct xe_gt *gt = node_to_gt(m->private);
	struct drm_printer p = drm_seq_file_printer(m);

	xe_wa_dump(gt, &p);

	return 0;
}

static int pat(struct seq_file *m, void *data)
{
	struct xe_gt *gt = node_to_gt(m->private);
	struct drm_printer p = drm_seq_file_printer(m);

	xe_pat_dump(gt, &p);

	return 0;
}

static int rcs_default_lrc(struct seq_file *m, void *data)
{
	struct drm_printer p = drm_seq_file_printer(m);

	xe_lrc_dump_default(&p, node_to_gt(m->private), XE_ENGINE_CLASS_RENDER);
	return 0;
}

static int ccs_default_lrc(struct seq_file *m, void *data)
{
	struct drm_printer p = drm_seq_file_printer(m);

	xe_lrc_dump_default(&p, node_to_gt(m->private), XE_ENGINE_CLASS_COMPUTE);
	return 0;
}

static int bcs_default_lrc(struct seq_file *m, void *data)
{
	struct drm_printer p = drm_seq_file_printer(m);

	xe_lrc_dump_default(&p, node_to_gt(m->private), XE_ENGINE_CLASS_COPY);
	return 0;
}

static int vcs_default_lrc(struct seq_file *m, void *data)
{
	struct drm_printer p = drm_seq_file_printer(m);

	xe_lrc_dump_default(&p, node_to_gt(m->private), XE_ENGINE_CLASS_VIDEO_DECODE);
	return 0;
}

static int vecs_default_lrc(struct seq_file *m, void *data)
{
	struct drm_printer p = drm_seq_file_printer(m);

	xe_lrc_dump_default(&p, node_to_gt(m->private), XE_ENGINE_CLASS_VIDEO_ENHANCE);
	return 0;
}

static const struct drm_info_list debugfs_list[] = {
	{"hw_engines", hw_engines, 0},
	{"force_reset", force_reset, 0},
	{"sa_info", sa_info, 0},
	{"topology", topology, 0},
	{"steering", steering, 0},
	{"ggtt", ggtt, 0},
	{"register-save-restore", register_save_restore, 0},
	{"workarounds", workarounds, 0},
	{"pat", pat, 0},
	{"default_lrc_rcs", rcs_default_lrc},
	{"default_lrc_ccs", ccs_default_lrc},
	{"default_lrc_bcs", bcs_default_lrc},
	{"default_lrc_vcs", vcs_default_lrc},
	{"default_lrc_vecs", vecs_default_lrc},
};

void xe_gt_debugfs_register(struct xe_gt *gt)
{
	struct xe_device *xe = gt_to_xe(gt);
	struct drm_minor *minor = gt_to_xe(gt)->drm.primary;
	struct dentry *root;
	struct drm_info_list *local;
	char name[8];
	int i;

	xe_gt_assert(gt, minor->debugfs_root);

	sprintf(name, "gt%d", gt->info.id);
	root = debugfs_create_dir(name, minor->debugfs_root);
	if (IS_ERR(root)) {
		drm_warn(&xe->drm, "Create GT directory failed");
		return;
	}

	/*
	 * Allocate local copy as we need to pass in the GT to the debugfs
	 * entry and drm_debugfs_create_files just references the drm_info_list
	 * passed in (e.g. can't define this on the stack).
	 */
#define DEBUGFS_SIZE	(ARRAY_SIZE(debugfs_list) * sizeof(struct drm_info_list))
	local = drmm_kmalloc(&xe->drm, DEBUGFS_SIZE, GFP_KERNEL);
	if (!local)
		return;

	memcpy(local, debugfs_list, DEBUGFS_SIZE);
#undef DEBUGFS_SIZE

	for (i = 0; i < ARRAY_SIZE(debugfs_list); ++i)
		local[i].data = gt;

	drm_debugfs_create_files(local,
				 ARRAY_SIZE(debugfs_list),
				 root, minor);

	xe_uc_debugfs_register(&gt->uc, root);
}
