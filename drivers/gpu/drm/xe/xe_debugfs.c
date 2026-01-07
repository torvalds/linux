// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_debugfs.h"

#include <linux/debugfs.h>
#include <linux/fault-inject.h>
#include <linux/string_helpers.h>

#include <drm/drm_debugfs.h>

#include "regs/xe_pmt.h"
#include "xe_bo.h"
#include "xe_device.h"
#include "xe_force_wake.h"
#include "xe_gt_debugfs.h"
#include "xe_gt_printk.h"
#include "xe_guc_ads.h"
#include "xe_mmio.h"
#include "xe_pm.h"
#include "xe_psmi.h"
#include "xe_pxp_debugfs.h"
#include "xe_sriov.h"
#include "xe_sriov_pf_debugfs.h"
#include "xe_sriov_vf.h"
#include "xe_step.h"
#include "xe_tile_debugfs.h"
#include "xe_vsec.h"
#include "xe_wa.h"

#ifdef CONFIG_DRM_XE_DEBUG
#include "xe_bo_evict.h"
#include "xe_migrate.h"
#include "xe_vm.h"
#endif

DECLARE_FAULT_ATTR(gt_reset_failure);
DECLARE_FAULT_ATTR(inject_csc_hw_error);

static void read_residency_counter(struct xe_device *xe, struct xe_mmio *mmio,
				   u32 offset, const char *name, struct drm_printer *p)
{
	u64 residency = 0;
	int ret;

	ret = xe_pmt_telem_read(to_pci_dev(xe->drm.dev),
				xe_mmio_read32(mmio, PUNIT_TELEMETRY_GUID),
				&residency, offset, sizeof(residency));
	if (ret != sizeof(residency)) {
		drm_warn(&xe->drm, "%s counter failed to read, ret %d\n", name, ret);
		return;
	}

	drm_printf(p, "%s : %llu\n", name, residency);
}

static struct xe_device *node_to_xe(struct drm_info_node *node)
{
	return to_xe_device(node->minor->dev);
}

static int info(struct seq_file *m, void *data)
{
	struct xe_device *xe = node_to_xe(m->private);
	struct drm_printer p = drm_seq_file_printer(m);
	struct xe_gt *gt;
	u8 id;

	guard(xe_pm_runtime)(xe);

	drm_printf(&p, "graphics_verx100 %d\n", xe->info.graphics_verx100);
	drm_printf(&p, "media_verx100 %d\n", xe->info.media_verx100);
	drm_printf(&p, "stepping G:%s M:%s B:%s\n",
		   xe_step_name(xe->info.step.graphics),
		   xe_step_name(xe->info.step.media),
		   xe_step_name(xe->info.step.basedie));
	drm_printf(&p, "is_dgfx %s\n", str_yes_no(xe->info.is_dgfx));
	drm_printf(&p, "platform %d\n", xe->info.platform);
	drm_printf(&p, "subplatform %d\n",
		   xe->info.subplatform > XE_SUBPLATFORM_NONE ? xe->info.subplatform : 0);
	drm_printf(&p, "devid 0x%x\n", xe->info.devid);
	drm_printf(&p, "revid %d\n", xe->info.revid);
	drm_printf(&p, "tile_count %d\n", xe->info.tile_count);
	drm_printf(&p, "vm_max_level %d\n", xe->info.vm_max_level);
	drm_printf(&p, "force_execlist %s\n", str_yes_no(xe->info.force_execlist));
	drm_printf(&p, "has_flat_ccs %s\n", str_yes_no(xe->info.has_flat_ccs));
	drm_printf(&p, "has_usm %s\n", str_yes_no(xe->info.has_usm));
	drm_printf(&p, "skip_guc_pc %s\n", str_yes_no(xe->info.skip_guc_pc));
	for_each_gt(gt, xe, id) {
		drm_printf(&p, "gt%d force wake %d\n", id,
			   xe_force_wake_ref(gt_to_fw(gt), XE_FW_GT));
		drm_printf(&p, "gt%d engine_mask 0x%llx\n", id,
			   gt->info.engine_mask);
		drm_printf(&p, "gt%d multi_queue_engine_class_mask 0x%x\n", id,
			   gt->info.multi_queue_engine_class_mask);
	}

	return 0;
}

static int sriov_info(struct seq_file *m, void *data)
{
	struct xe_device *xe = node_to_xe(m->private);
	struct drm_printer p = drm_seq_file_printer(m);

	xe_sriov_print_info(xe, &p);
	return 0;
}

static int workarounds(struct xe_device *xe, struct drm_printer *p)
{
	guard(xe_pm_runtime)(xe);
	xe_wa_device_dump(xe, p);

	return 0;
}

static int workaround_info(struct seq_file *m, void *data)
{
	struct xe_device *xe = node_to_xe(m->private);
	struct drm_printer p = drm_seq_file_printer(m);

	workarounds(xe, &p);
	return 0;
}

static int dgfx_pkg_residencies_show(struct seq_file *m, void *data)
{
	struct xe_device *xe;
	struct xe_mmio *mmio;
	struct drm_printer p;

	xe = node_to_xe(m->private);
	p = drm_seq_file_printer(m);
	guard(xe_pm_runtime)(xe);
	mmio = xe_root_tile_mmio(xe);
	static const struct {
		u32 offset;
		const char *name;
	} residencies[] = {
		{BMG_G2_RESIDENCY_OFFSET, "Package G2"},
		{BMG_G6_RESIDENCY_OFFSET, "Package G6"},
		{BMG_G7_RESIDENCY_OFFSET, "Package G7"},
		{BMG_G8_RESIDENCY_OFFSET, "Package G8"},
		{BMG_G10_RESIDENCY_OFFSET, "Package G10"},
		{BMG_MODS_RESIDENCY_OFFSET, "Package ModS"}
	};

	for (int i = 0; i < ARRAY_SIZE(residencies); i++)
		read_residency_counter(xe, mmio, residencies[i].offset, residencies[i].name, &p);

	return 0;
}

static int dgfx_pcie_link_residencies_show(struct seq_file *m, void *data)
{
	struct xe_device *xe;
	struct xe_mmio *mmio;
	struct drm_printer p;

	xe = node_to_xe(m->private);
	p = drm_seq_file_printer(m);
	guard(xe_pm_runtime)(xe);
	mmio = xe_root_tile_mmio(xe);

	static const struct {
		u32 offset;
		const char *name;
	} residencies[] = {
		{BMG_PCIE_LINK_L0_RESIDENCY_OFFSET, "PCIE LINK L0 RESIDENCY"},
		{BMG_PCIE_LINK_L1_RESIDENCY_OFFSET, "PCIE LINK L1 RESIDENCY"},
		{BMG_PCIE_LINK_L1_2_RESIDENCY_OFFSET, "PCIE LINK L1.2 RESIDENCY"}
	};

	for (int i = 0; i < ARRAY_SIZE(residencies); i++)
		read_residency_counter(xe, mmio, residencies[i].offset, residencies[i].name, &p);

	return 0;
}

static const struct drm_info_list debugfs_list[] = {
	{"info", info, 0},
	{ .name = "sriov_info", .show = sriov_info, },
	{ .name = "workarounds", .show = workaround_info, },
};

static const struct drm_info_list debugfs_residencies[] = {
	{ .name = "dgfx_pkg_residencies", .show = dgfx_pkg_residencies_show, },
	{ .name = "dgfx_pcie_link_residencies", .show = dgfx_pcie_link_residencies_show, },
};

static int forcewake_open(struct inode *inode, struct file *file)
{
	struct xe_device *xe = inode->i_private;
	struct xe_gt *gt;
	u8 id, last_gt;
	unsigned int fw_ref;

	xe_pm_runtime_get(xe);
	for_each_gt(gt, xe, id) {
		last_gt = id;

		fw_ref = xe_force_wake_get(gt_to_fw(gt), XE_FORCEWAKE_ALL);
		if (!xe_force_wake_ref_has_domain(fw_ref, XE_FORCEWAKE_ALL))
			goto err_fw_get;
	}

	return 0;

err_fw_get:
	for_each_gt(gt, xe, id) {
		if (id < last_gt)
			xe_force_wake_put(gt_to_fw(gt), XE_FORCEWAKE_ALL);
		else if (id == last_gt)
			xe_force_wake_put(gt_to_fw(gt), fw_ref);
		else
			break;
	}

	xe_pm_runtime_put(xe);
	return -ETIMEDOUT;
}

static int forcewake_release(struct inode *inode, struct file *file)
{
	struct xe_device *xe = inode->i_private;
	struct xe_gt *gt;
	u8 id;

	for_each_gt(gt, xe, id)
		xe_force_wake_put(gt_to_fw(gt), XE_FORCEWAKE_ALL);
	xe_pm_runtime_put(xe);

	return 0;
}

static const struct file_operations forcewake_all_fops = {
	.owner = THIS_MODULE,
	.open = forcewake_open,
	.release = forcewake_release,
};

static ssize_t wedged_mode_show(struct file *f, char __user *ubuf,
				size_t size, loff_t *pos)
{
	struct xe_device *xe = file_inode(f)->i_private;
	char buf[32];
	int len = 0;

	len = scnprintf(buf, sizeof(buf), "%d\n", xe->wedged.mode);

	return simple_read_from_buffer(ubuf, size, pos, buf, len);
}

static int __wedged_mode_set_reset_policy(struct xe_gt *gt, enum xe_wedged_mode mode)
{
	bool enable_engine_reset;
	int ret;

	enable_engine_reset = (mode != XE_WEDGED_MODE_UPON_ANY_HANG_NO_RESET);
	ret = xe_guc_ads_scheduler_policy_toggle_reset(&gt->uc.guc.ads,
						       enable_engine_reset);
	if (ret)
		xe_gt_err(gt, "Failed to update GuC ADS scheduler policy (%pe)\n", ERR_PTR(ret));

	return ret;
}

static int wedged_mode_set_reset_policy(struct xe_device *xe, enum xe_wedged_mode mode)
{
	struct xe_gt *gt;
	int ret;
	u8 id;

	guard(xe_pm_runtime)(xe);
	for_each_gt(gt, xe, id) {
		ret = __wedged_mode_set_reset_policy(gt, mode);
		if (ret) {
			if (id > 0) {
				xe->wedged.inconsistent_reset = true;
				drm_err(&xe->drm, "Inconsistent reset policy state between GTs\n");
			}
			return ret;
		}
	}

	xe->wedged.inconsistent_reset = false;

	return 0;
}

static bool wedged_mode_needs_policy_update(struct xe_device *xe, enum xe_wedged_mode mode)
{
	if (xe->wedged.inconsistent_reset)
		return true;

	if (xe->wedged.mode == mode)
		return false;

	if (xe->wedged.mode == XE_WEDGED_MODE_UPON_ANY_HANG_NO_RESET ||
	    mode == XE_WEDGED_MODE_UPON_ANY_HANG_NO_RESET)
		return true;

	return false;
}

static ssize_t wedged_mode_set(struct file *f, const char __user *ubuf,
			       size_t size, loff_t *pos)
{
	struct xe_device *xe = file_inode(f)->i_private;
	u32 wedged_mode;
	ssize_t ret;

	ret = kstrtouint_from_user(ubuf, size, 0, &wedged_mode);
	if (ret)
		return ret;

	ret = xe_device_validate_wedged_mode(xe, wedged_mode);
	if (ret)
		return ret;

	if (wedged_mode_needs_policy_update(xe, wedged_mode)) {
		ret = wedged_mode_set_reset_policy(xe, wedged_mode);
		if (ret)
			return ret;
	}

	xe->wedged.mode = wedged_mode;

	return size;
}

static const struct file_operations wedged_mode_fops = {
	.owner = THIS_MODULE,
	.read = wedged_mode_show,
	.write = wedged_mode_set,
};

static ssize_t page_reclaim_hw_assist_show(struct file *f, char __user *ubuf,
					   size_t size, loff_t *pos)
{
	struct xe_device *xe = file_inode(f)->i_private;
	char buf[8];
	int len;

	len = scnprintf(buf, sizeof(buf), "%d\n", xe->info.has_page_reclaim_hw_assist);
	return simple_read_from_buffer(ubuf, size, pos, buf, len);
}

static ssize_t page_reclaim_hw_assist_set(struct file *f, const char __user *ubuf,
					  size_t size, loff_t *pos)
{
	struct xe_device *xe = file_inode(f)->i_private;
	bool val;
	ssize_t ret;

	ret = kstrtobool_from_user(ubuf, size, &val);
	if (ret)
		return ret;

	xe->info.has_page_reclaim_hw_assist = val;

	return size;
}

static const struct file_operations page_reclaim_hw_assist_fops = {
	.owner = THIS_MODULE,
	.read = page_reclaim_hw_assist_show,
	.write = page_reclaim_hw_assist_set,
};

static ssize_t atomic_svm_timeslice_ms_show(struct file *f, char __user *ubuf,
					    size_t size, loff_t *pos)
{
	struct xe_device *xe = file_inode(f)->i_private;
	char buf[32];
	int len = 0;

	len = scnprintf(buf, sizeof(buf), "%d\n", xe->atomic_svm_timeslice_ms);

	return simple_read_from_buffer(ubuf, size, pos, buf, len);
}

static ssize_t atomic_svm_timeslice_ms_set(struct file *f,
					   const char __user *ubuf,
					   size_t size, loff_t *pos)
{
	struct xe_device *xe = file_inode(f)->i_private;
	u32 atomic_svm_timeslice_ms;
	ssize_t ret;

	ret = kstrtouint_from_user(ubuf, size, 0, &atomic_svm_timeslice_ms);
	if (ret)
		return ret;

	xe->atomic_svm_timeslice_ms = atomic_svm_timeslice_ms;

	return size;
}

static const struct file_operations atomic_svm_timeslice_ms_fops = {
	.owner = THIS_MODULE,
	.read = atomic_svm_timeslice_ms_show,
	.write = atomic_svm_timeslice_ms_set,
};

static ssize_t min_run_period_lr_ms_show(struct file *f, char __user *ubuf,
					 size_t size, loff_t *pos)
{
	struct xe_device *xe = file_inode(f)->i_private;
	char buf[32];
	int len = 0;

	len = scnprintf(buf, sizeof(buf), "%d\n", xe->min_run_period_lr_ms);

	return simple_read_from_buffer(ubuf, size, pos, buf, len);
}

static ssize_t min_run_period_lr_ms_set(struct file *f, const char __user *ubuf,
					size_t size, loff_t *pos)
{
	struct xe_device *xe = file_inode(f)->i_private;
	u32 min_run_period_lr_ms;
	ssize_t ret;

	ret = kstrtouint_from_user(ubuf, size, 0, &min_run_period_lr_ms);
	if (ret)
		return ret;

	xe->min_run_period_lr_ms = min_run_period_lr_ms;

	return size;
}

static const struct file_operations min_run_period_lr_ms_fops = {
	.owner = THIS_MODULE,
	.read = min_run_period_lr_ms_show,
	.write = min_run_period_lr_ms_set,
};

static ssize_t min_run_period_pf_ms_show(struct file *f, char __user *ubuf,
					 size_t size, loff_t *pos)
{
	struct xe_device *xe = file_inode(f)->i_private;
	char buf[32];
	int len = 0;

	len = scnprintf(buf, sizeof(buf), "%d\n", xe->min_run_period_pf_ms);

	return simple_read_from_buffer(ubuf, size, pos, buf, len);
}

static ssize_t min_run_period_pf_ms_set(struct file *f, const char __user *ubuf,
					size_t size, loff_t *pos)
{
	struct xe_device *xe = file_inode(f)->i_private;
	u32 min_run_period_pf_ms;
	ssize_t ret;

	ret = kstrtouint_from_user(ubuf, size, 0, &min_run_period_pf_ms);
	if (ret)
		return ret;

	xe->min_run_period_pf_ms = min_run_period_pf_ms;

	return size;
}

static const struct file_operations min_run_period_pf_ms_fops = {
	.owner = THIS_MODULE,
	.read = min_run_period_pf_ms_show,
	.write = min_run_period_pf_ms_set,
};

static ssize_t disable_late_binding_show(struct file *f, char __user *ubuf,
					 size_t size, loff_t *pos)
{
	struct xe_device *xe = file_inode(f)->i_private;
	struct xe_late_bind *late_bind = &xe->late_bind;
	char buf[32];
	int len;

	len = scnprintf(buf, sizeof(buf), "%d\n", late_bind->disable);

	return simple_read_from_buffer(ubuf, size, pos, buf, len);
}

static ssize_t disable_late_binding_set(struct file *f, const char __user *ubuf,
					size_t size, loff_t *pos)
{
	struct xe_device *xe = file_inode(f)->i_private;
	struct xe_late_bind *late_bind = &xe->late_bind;
	bool val;
	int ret;

	ret = kstrtobool_from_user(ubuf, size, &val);
	if (ret)
		return ret;

	late_bind->disable = val;
	return size;
}

static const struct file_operations disable_late_binding_fops = {
	.owner = THIS_MODULE,
	.read = disable_late_binding_show,
	.write = disable_late_binding_set,
};

void xe_debugfs_register(struct xe_device *xe)
{
	struct ttm_device *bdev = &xe->ttm;
	struct drm_minor *minor = xe->drm.primary;
	struct dentry *root = minor->debugfs_root;
	struct ttm_resource_manager *man;
	struct xe_tile *tile;
	struct xe_gt *gt;
	u8 tile_id;
	u8 id;

	drm_debugfs_create_files(debugfs_list,
				 ARRAY_SIZE(debugfs_list),
				 root, minor);

	if (xe->info.platform == XE_BATTLEMAGE && !IS_SRIOV_VF(xe)) {
		drm_debugfs_create_files(debugfs_residencies,
					 ARRAY_SIZE(debugfs_residencies),
					 root, minor);
		fault_create_debugfs_attr("inject_csc_hw_error", root,
					  &inject_csc_hw_error);
	}

	debugfs_create_file("forcewake_all", 0400, root, xe,
			    &forcewake_all_fops);

	debugfs_create_file("wedged_mode", 0600, root, xe,
			    &wedged_mode_fops);

	debugfs_create_file("atomic_svm_timeslice_ms", 0600, root, xe,
			    &atomic_svm_timeslice_ms_fops);

	debugfs_create_file("min_run_period_lr_ms", 0600, root, xe,
			    &min_run_period_lr_ms_fops);

	debugfs_create_file("min_run_period_pf_ms", 0600, root, xe,
			    &min_run_period_pf_ms_fops);

	debugfs_create_file("disable_late_binding", 0600, root, xe,
			    &disable_late_binding_fops);

	/*
	 * Don't expose page reclaim configuration file if not supported by the
	 * hardware initially.
	 */
	if (xe->info.has_page_reclaim_hw_assist)
		debugfs_create_file("page_reclaim_hw_assist", 0600, root, xe,
				    &page_reclaim_hw_assist_fops);

	man = ttm_manager_type(bdev, XE_PL_TT);
	ttm_resource_manager_create_debugfs(man, root, "gtt_mm");

	man = ttm_manager_type(bdev, XE_PL_STOLEN);
	if (man)
		ttm_resource_manager_create_debugfs(man, root, "stolen_mm");

	for_each_tile(tile, xe, tile_id)
		xe_tile_debugfs_register(tile);

	for_each_gt(gt, xe, id)
		xe_gt_debugfs_register(gt);

	xe_pxp_debugfs_register(xe->pxp);

	xe_psmi_debugfs_register(xe);

	fault_create_debugfs_attr("fail_gt_reset", root, &gt_reset_failure);

	if (IS_SRIOV_PF(xe))
		xe_sriov_pf_debugfs_register(xe, root);
	else if (IS_SRIOV_VF(xe))
		xe_sriov_vf_debugfs_register(xe, root);
}
