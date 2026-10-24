// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_gt_debugfs.h"

#include <linux/debugfs.h>
#include <linux/panic.h>
#include <linux/string.h>

#include <drm/drm_debugfs.h>
#include <drm/drm_managed.h>
#include <linux/math.h>

#include "regs/xe_engine_regs.h"
#include "regs/xe_gt_regs.h"
#include "xe_device.h"
#include "xe_force_wake.h"
#include "xe_gt.h"
#include "xe_gt_mcr.h"
#include "xe_gt_idle.h"
#include "xe_gt_printk.h"
#include "xe_gt_sriov_pf_debugfs.h"
#include "xe_gt_sriov_vf_debugfs.h"
#include "xe_gt_stats.h"
#include "xe_gt_topology.h"
#include "xe_guc_hwconfig.h"
#include "xe_guc_submit.h"
#include "xe_hw_engine.h"
#include "xe_lrc.h"
#include "xe_mmio.h"
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
	static const struct {
		struct xe_reg reg;
		bool has_fw;
	} msg_idle_reg[XE_NUM_HW_ENGINES] = {
		[XE_HW_ENGINE_RCS0] = { MSG_IDLE_CS, true },
		[XE_HW_ENGINE_BCS0] = { MSG_IDLE_BCS0, true },
		[XE_HW_ENGINE_BCS1] = { MSG_IDLE_BCS1, false },
		[XE_HW_ENGINE_BCS2] = { MSG_IDLE_BCS2, false },
		[XE_HW_ENGINE_BCS3] = { MSG_IDLE_BCS3, false },
		[XE_HW_ENGINE_BCS4] = { MSG_IDLE_BCS4, false },
		[XE_HW_ENGINE_BCS5] = { MSG_IDLE_BCS5, false },
		[XE_HW_ENGINE_BCS6] = { MSG_IDLE_BCS6, false },
		[XE_HW_ENGINE_BCS7] = { MSG_IDLE_BCS7, false },
		[XE_HW_ENGINE_BCS8] = { MSG_IDLE_BCS8, false },
		[XE_HW_ENGINE_VCS0] = { MSG_IDLE_VCS0, true },
		[XE_HW_ENGINE_VCS1] = { MSG_IDLE_VCS1, true },
		[XE_HW_ENGINE_VCS2] = { MSG_IDLE_VCS2, true },
		[XE_HW_ENGINE_VCS3] = { MSG_IDLE_VCS3, true },
		[XE_HW_ENGINE_VCS4] = { MSG_IDLE_VCS4, true },
		[XE_HW_ENGINE_VCS5] = { MSG_IDLE_VCS5, true },
		[XE_HW_ENGINE_VCS6] = { MSG_IDLE_VCS6, true },
		[XE_HW_ENGINE_VCS7] = { MSG_IDLE_VCS7, true },
		[XE_HW_ENGINE_VECS0] = { MSG_IDLE_VECS0, true },
		[XE_HW_ENGINE_VECS1] = { MSG_IDLE_VECS1, true },
		[XE_HW_ENGINE_VECS2] = { MSG_IDLE_VECS2, true },
		[XE_HW_ENGINE_VECS3] = { MSG_IDLE_VECS3, true },
		[XE_HW_ENGINE_CCS0] = { MSG_IDLE_CS, true },
		[XE_HW_ENGINE_CCS1] = { MSG_IDLE_CS, true },
		[XE_HW_ENGINE_CCS2] = { MSG_IDLE_CS, true },
		[XE_HW_ENGINE_CCS3] = { MSG_IDLE_CS, true },
		[XE_HW_ENGINE_GSCCS0] = { MSG_IDLE_GSCCS0, false },
	};
	struct xe_hw_engine *hwe;
	enum xe_hw_engine_id id;

	CLASS(xe_force_wake, fw_ref)(gt_to_fw(gt), XE_FORCEWAKE_ALL);
	if (!xe_force_wake_ref_has_domain(fw_ref.domains, XE_FORCEWAKE_ALL))
		return -ETIMEDOUT;

	for_each_hw_engine(hwe, gt, id) {
		struct xe_reg reg = msg_idle_reg[id].reg;
		u32 val;

		xe_hw_engine_print(hwe, p);

		val = xe_mmio_read32(&gt->mmio, reg);

		if (msg_idle_reg[id].has_fw)
			drm_printf(p, "\tpipe %s, C6 %s, fw_req 0x%02x\n",
				   val & MSG_IDLE_INDICATION ? "idle" : "busy",
				   val & MSG_IDLE_C6_ALLOWED ? "allowed" : "not allowed",
				   REG_FIELD_GET(MSG_IDLE_FW_REQ, val));
		else
			drm_printf(p, "\tpipe %s, C6 %s\n",
				   val & MSG_IDLE_INDICATION ? "idle" : "busy",
				   val & MSG_IDLE_C6_ALLOWED ? "allowed" : "not allowed");

		drm_puts(p, "\n");
	}

	return 0;
}

static int multi_queue_active_lrca(struct xe_gt *gt, struct drm_printer *p)
{
	struct xe_guc *guc = &gt->uc.guc;
	struct xe_hw_engine *hwe;
	enum xe_hw_engine_id id;

	for_each_hw_engine(hwe, gt, id) {
		u32 cur_lrca, active_id, lrca;
		unsigned int fw_ref;

		if (!xe_gt_supports_multi_queue(gt, hwe->class))
			continue;

		/*
		 * Forcewake is dropped before xe_guc_submit_active_multi_queue_lrca()
		 * below, which takes guc->submission_state.lock, to avoid holding a
		 * GT forcewake ref across a mutex acquired elsewhere in the opposite
		 * order.
		 */
		fw_ref = xe_force_wake_get(gt_to_fw(gt), XE_FORCEWAKE_ALL);
		if (!xe_force_wake_ref_has_domain(fw_ref, XE_FORCEWAKE_ALL)) {
			drm_printf(p, "%s\tforcewake failed, skipping\n", hwe->name);
			xe_force_wake_put(gt_to_fw(gt), fw_ref);
			continue;
		}

		cur_lrca = xe_mmio_read32(&gt->mmio,
					  RING_CURRENT_LRCA(hwe->mmio_base));
		active_id = xe_lrc_get_multi_queue_active_queue_id(hwe);

		xe_force_wake_put(gt_to_fw(gt), fw_ref);

		lrca = xe_guc_submit_active_multi_queue_lrca(guc, hwe, cur_lrca,
							     active_id);

		drm_printf(p, "%s\tactive_queue_id %u\tcurrent_lrca 0x%08x\tactive_lrca 0x%08x\n",
			   hwe->name, active_id, cur_lrca, lrca);
	}

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
	for_each_hw_engine(hwe, gt, id) {
		xe_reg_whitelist_dump(&hwe->reg_whitelist, p);
		xe_reg_whitelist_dump(&hwe->oa_whitelist, p);
	}

	return 0;
}

/*
 * Check the registers referenced on a save-restore list and report any
 * save-restore entries that did not get applied.
 */
static int register_save_restore_check(struct xe_gt *gt, struct drm_printer *p)
{
	struct xe_hw_engine *hwe;
	enum xe_hw_engine_id id;

	CLASS(xe_force_wake, fw_ref)(gt_to_fw(gt), XE_FORCEWAKE_ALL);
	if (!xe_force_wake_ref_has_domain(fw_ref.domains, XE_FORCEWAKE_ALL)) {
		drm_printf(p, "ERROR: Could not acquire forcewake\n");
		return -ETIMEDOUT;
	}

	xe_reg_sr_readback_check(&gt->reg_sr, gt, p);
	for_each_hw_engine(hwe, gt, id)
		xe_reg_sr_readback_check(&hwe->reg_sr, gt, p);
	for_each_hw_engine(hwe, gt, id)
		xe_reg_sr_lrc_check(&hwe->reg_lrc, gt, hwe, p);

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
	{ "register-save-restore-check",
		.show = xe_gt_debugfs_show_with_rpm, .data = register_save_restore_check },
};

/* everything else should be added here */
static const struct drm_info_list pf_only_debugfs_list[] = {
	{ "hw_engines", .show = xe_gt_debugfs_show_with_rpm, .data = hw_engines },
	{ "mocs", .show = xe_gt_debugfs_show_with_rpm, .data = xe_mocs_dump },
	{ "pat", .show = xe_gt_debugfs_show_with_rpm, .data = xe_pat_dump },
	{ "powergate_info", .show = xe_gt_debugfs_show_with_rpm, .data = xe_gt_idle_pg_print },
	{ "steering", .show = xe_gt_debugfs_show_with_rpm, .data = steering },
};

static const struct drm_info_list multi_queue_debugfs_list[] = {
	{ "multi_queue_active_lrca",
		.show = xe_gt_debugfs_show_with_rpm, .data = multi_queue_active_lrca },
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

#define U1_15_ONE		0x8000
#define U1_15_INT_BITS		GENMASK(15, 15)
#define U1_15_FRACTION_BITS	GENMASK(14, 0)

static void u1_15_decode(u16 num, u16 *i, u32 *frac)
{
	/*
	 * In U1.15 format, uppermost bit is integer value and the
	 * rest 15 are the fraction.
	 */

	*i = FIELD_GET(U1_15_INT_BITS, num);
	*frac = FIELD_GET(U1_15_FRACTION_BITS, num);
}

static void u1_15_decode_decimal(u16 value, u16 *i, u32 *frac, int digits)
{
	u1_15_decode(value, i, frac);
	*frac = (*frac * int_pow(10, digits)) / (FIELD_MAX(U1_15_FRACTION_BITS) + 1);
}

static int gt_ia_bias_show(struct seq_file *s, void *unused)
{
	struct xe_gt *gt = s->private;
	struct xe_device *xe = gt_to_xe(gt);
	u32 val;
	u32 ia_frac, gt_frac;
	u16 ia_raw, gt_raw;
	u16 ia_int, gt_int;

	guard(xe_pm_runtime)(xe);
	val = xe_mmio_read32(&gt->mmio, GT_IA_PERF_BIAS_REG);

	ia_raw = REG_FIELD_GET(IA_BIAS, val);
	gt_raw = REG_FIELD_GET(GT_BIAS, val);

	u1_15_decode_decimal(ia_raw, &ia_int, &ia_frac, 4);
	u1_15_decode_decimal(gt_raw, &gt_int, &gt_frac, 4);

	seq_printf(s, "0x%x (GT: %u.%04u, IA: %u.%04u)\n",
		   val, gt_int, gt_frac, ia_int, ia_frac);

	return 0;
}

static ssize_t gt_ia_bias_write(struct file *file,
				const char __user *userbuf,
				size_t count, loff_t *ppos)
{
	struct seq_file *s = file->private_data;
	struct xe_gt *gt = s->private;
	struct xe_device *xe = gt_to_xe(gt);
	u32 val;
	int ret;

	ret = kstrtou32_from_user(userbuf, count, 0, &val);
	if (ret)
		return ret;

	if (REG_FIELD_GET(IA_BIAS, val) > U1_15_ONE ||
	    REG_FIELD_GET(GT_BIAS, val) > U1_15_ONE)
		return -EINVAL;

	if (REG_FIELD_GET(IA_BIAS, val) < IA_BIAS_DEFAULT ||
	    REG_FIELD_GET(GT_BIAS, val) < GT_BIAS_DEFAULT)
		return -EINVAL;

	guard(xe_pm_runtime)(xe);
	xe_mmio_write32(&gt->mmio, GT_IA_PERF_BIAS_REG, val);

	return count;
}
DEFINE_SHOW_STORE_ATTRIBUTE(gt_ia_bias);

static const char * const gpgpu_preemption_level_names[] = {
	[XE_GPGPU_PREEMPT_DEFAULT]      = "default",
	[XE_GPGPU_PREEMPT_MID_THREAD]   = "mid-thread",
	[XE_GPGPU_PREEMPT_THREAD_GROUP] = "thread-group",
	[XE_GPGPU_PREEMPT_COMMAND]      = "command",
};

static int gpgpu_preemption_level_show(struct seq_file *m, void *unused)
{
	struct xe_gt *gt = m->private;

	seq_printf(m, "%s\n", gpgpu_preemption_level_names[gt->gpgpu_preemption_level]);

	return 0;
}

static ssize_t gpgpu_preemption_level_write(struct file *file,
					    const char __user *ubuf,
					    size_t len, loff_t *offp)
{
	struct seq_file *m = file->private_data;
	struct xe_gt *gt = m->private;
	enum xe_gpgpu_preempt_level new_level;
	char buf[16];
	ssize_t copied;
	int idx;

	if (*offp)
		return -EINVAL;

	copied = simple_write_to_buffer(buf, sizeof(buf) - 1, offp, ubuf, len);
	if (copied < 0)
		return copied;

	buf[copied] = '\0';
	idx = sysfs_match_string(gpgpu_preemption_level_names, strim(buf));
	if (idx < 0)
		return idx;

	new_level = (enum xe_gpgpu_preempt_level)idx;

	if (new_level == XE_GPGPU_PREEMPT_MID_THREAD && gt->info.has_wmtp_disabled) {
		xe_gt_warn(gt, "MTP fused off in hardware, cannot select mid-thread\n");
		return -EINVAL;
	}

	if (new_level != XE_GPGPU_PREEMPT_DEFAULT) {
		add_taint(TAINT_USER, LOCKDEP_STILL_OK);
		xe_gt_notice(gt,
			     "GPGPU preemption overridden to '%s' (applies to new LRCs only)\n",
			     gpgpu_preemption_level_names[new_level]);
	}

	gt->gpgpu_preemption_level = new_level;

	return copied;
}
DEFINE_SHOW_STORE_ATTRIBUTE(gpgpu_preemption_level);

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

	if (GRAPHICS_VER(xe) >= 20 && (gt->info.engine_mask & XE_HW_ENGINE_RCS_MASK))
		debugfs_create_file("gpgpu_preemption_level", 0600, root,
				    gt, &gpgpu_preemption_level_fops);

	drm_debugfs_create_files(vf_safe_debugfs_list,
				 ARRAY_SIZE(vf_safe_debugfs_list),
				 root, minor);

	if (!IS_SRIOV_VF(xe))
		drm_debugfs_create_files(pf_only_debugfs_list,
					 ARRAY_SIZE(pf_only_debugfs_list),
					 root, minor);

	if (!IS_SRIOV_VF(xe) && xe_gt_has_multi_queue(gt))
		drm_debugfs_create_files(multi_queue_debugfs_list,
					 ARRAY_SIZE(multi_queue_debugfs_list),
					 root, minor);

	if (xe_gt_is_main_type(gt) && !IS_DGFX(xe) && !IS_SRIOV_VF(xe))
		debugfs_create_file("gt_ia_bias", 0600, root, gt, &gt_ia_bias_fops);

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
