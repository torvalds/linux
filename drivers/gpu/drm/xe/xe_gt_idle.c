// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include <drm/drm_managed.h>

#include "xe_force_wake.h"
#include "xe_device.h"
#include "xe_gt.h"
#include "xe_gt_idle.h"
#include "xe_gt_sysfs.h"
#include "xe_guc_pc.h"
#include "regs/xe_gt_regs.h"
#include "xe_macros.h"
#include "xe_mmio.h"
#include "xe_pm.h"
#include "xe_sriov.h"

/**
 * DOC: Xe GT Idle
 *
 * Contains functions that init GT idle features like C6
 *
 * device/gt#/gtidle/name - name of the state
 * device/gt#/gtidle/idle_residency_ms - Provides residency of the idle state in ms
 * device/gt#/gtidle/idle_status - Provides current idle state
 */

static struct xe_gt_idle *dev_to_gtidle(struct device *dev)
{
	struct kobject *kobj = &dev->kobj;

	return &kobj_to_gt(kobj->parent)->gtidle;
}

static struct xe_gt *gtidle_to_gt(struct xe_gt_idle *gtidle)
{
	return container_of(gtidle, struct xe_gt, gtidle);
}

static struct xe_guc_pc *gtidle_to_pc(struct xe_gt_idle *gtidle)
{
	return &gtidle_to_gt(gtidle)->uc.guc.pc;
}

static struct xe_device *
pc_to_xe(struct xe_guc_pc *pc)
{
	struct xe_guc *guc = container_of(pc, struct xe_guc, pc);
	struct xe_gt *gt = container_of(guc, struct xe_gt, uc.guc);

	return gt_to_xe(gt);
}

static const char *gt_idle_state_to_string(enum xe_gt_idle_state state)
{
	switch (state) {
	case GT_IDLE_C0:
		return "gt-c0";
	case GT_IDLE_C6:
		return "gt-c6";
	default:
		return "unknown";
	}
}

static u64 get_residency_ms(struct xe_gt_idle *gtidle, u64 cur_residency)
{
	u64 delta, overflow_residency, prev_residency;

	lockdep_assert_held(&gtidle->lock);

	overflow_residency = BIT_ULL(32);

	/*
	 * Counter wrap handling
	 * Store previous hw counter values for counter wrap-around handling
	 * Relying on sufficient frequency of queries otherwise counters can still wrap.
	 */
	prev_residency = gtidle->prev_residency;
	gtidle->prev_residency = cur_residency;

	/* delta */
	if (cur_residency >= prev_residency)
		delta = cur_residency - prev_residency;
	else
		delta = cur_residency + (overflow_residency - prev_residency);

	/* Add delta to extended raw driver copy of idle residency */
	cur_residency = gtidle->cur_residency + delta;
	gtidle->cur_residency = cur_residency;

	/* residency multiplier in ns, convert to ms */
	cur_residency = mul_u64_u32_div(cur_residency, gtidle->residency_multiplier, 1e6);

	return cur_residency;
}

void xe_gt_idle_enable_pg(struct xe_gt *gt)
{
	struct xe_device *xe = gt_to_xe(gt);
	struct xe_gt_idle *gtidle = &gt->gtidle;
	struct xe_mmio *mmio = &gt->mmio;
	u32 vcs_mask, vecs_mask;
	unsigned int fw_ref;
	int i, j;

	if (IS_SRIOV_VF(xe))
		return;

	/* Disable CPG for PVC */
	if (xe->info.platform == XE_PVC)
		return;

	xe_device_assert_mem_access(gt_to_xe(gt));

	vcs_mask = xe_hw_engine_mask_per_class(gt, XE_ENGINE_CLASS_VIDEO_DECODE);
	vecs_mask = xe_hw_engine_mask_per_class(gt, XE_ENGINE_CLASS_VIDEO_ENHANCE);

	if (vcs_mask || vecs_mask)
		gtidle->powergate_enable = MEDIA_POWERGATE_ENABLE;

	if (!xe_gt_is_media_type(gt))
		gtidle->powergate_enable |= RENDER_POWERGATE_ENABLE;

	if (xe->info.platform != XE_DG1) {
		for (i = XE_HW_ENGINE_VCS0, j = 0; i <= XE_HW_ENGINE_VCS7; ++i, ++j) {
			if ((gt->info.engine_mask & BIT(i)))
				gtidle->powergate_enable |= (VDN_HCP_POWERGATE_ENABLE(j) |
							     VDN_MFXVDENC_POWERGATE_ENABLE(j));
		}
	}

	fw_ref = xe_force_wake_get(gt_to_fw(gt), XE_FW_GT);
	if (xe->info.skip_guc_pc) {
		/*
		 * GuC sets the hysteresis value when GuC PC is enabled
		 * else set it to 25 (25 * 1.28us)
		 */
		xe_mmio_write32(mmio, MEDIA_POWERGATE_IDLE_HYSTERESIS, 25);
		xe_mmio_write32(mmio, RENDER_POWERGATE_IDLE_HYSTERESIS, 25);
	}

	xe_mmio_write32(mmio, POWERGATE_ENABLE, gtidle->powergate_enable);
	xe_force_wake_put(gt_to_fw(gt), fw_ref);
}

void xe_gt_idle_disable_pg(struct xe_gt *gt)
{
	struct xe_gt_idle *gtidle = &gt->gtidle;
	unsigned int fw_ref;

	if (IS_SRIOV_VF(gt_to_xe(gt)))
		return;

	xe_device_assert_mem_access(gt_to_xe(gt));
	gtidle->powergate_enable = 0;

	fw_ref = xe_force_wake_get(gt_to_fw(gt), XE_FW_GT);
	xe_mmio_write32(&gt->mmio, POWERGATE_ENABLE, gtidle->powergate_enable);
	xe_force_wake_put(gt_to_fw(gt), fw_ref);
}

/**
 * xe_gt_idle_pg_print - Xe powergating info
 * @gt: GT object
 * @p: drm_printer.
 *
 * This function prints the powergating information
 *
 * Return: 0 on success, negative error code otherwise
 */
int xe_gt_idle_pg_print(struct xe_gt *gt, struct drm_printer *p)
{
	struct xe_gt_idle *gtidle = &gt->gtidle;
	struct xe_device *xe = gt_to_xe(gt);
	enum xe_gt_idle_state state;
	u32 pg_enabled, pg_status = 0;
	u32 vcs_mask, vecs_mask;
	unsigned int fw_ref;
	int n;
	/*
	 * Media Slices
	 *
	 * Slice 0: VCS0, VCS1, VECS0
	 * Slice 1: VCS2, VCS3, VECS1
	 * Slice 2: VCS4, VCS5, VECS2
	 * Slice 3: VCS6, VCS7, VECS3
	 */
	static const struct {
		u64 engines;
		u32 status_bit;
	} media_slices[] = {
		{(BIT(XE_HW_ENGINE_VCS0) | BIT(XE_HW_ENGINE_VCS1) |
		  BIT(XE_HW_ENGINE_VECS0)), MEDIA_SLICE0_AWAKE_STATUS},

		{(BIT(XE_HW_ENGINE_VCS2) | BIT(XE_HW_ENGINE_VCS3) |
		   BIT(XE_HW_ENGINE_VECS1)), MEDIA_SLICE1_AWAKE_STATUS},

		{(BIT(XE_HW_ENGINE_VCS4) | BIT(XE_HW_ENGINE_VCS5) |
		   BIT(XE_HW_ENGINE_VECS2)), MEDIA_SLICE2_AWAKE_STATUS},

		{(BIT(XE_HW_ENGINE_VCS6) | BIT(XE_HW_ENGINE_VCS7) |
		   BIT(XE_HW_ENGINE_VECS3)), MEDIA_SLICE3_AWAKE_STATUS},
	};

	if (xe->info.platform == XE_PVC) {
		drm_printf(p, "Power Gating not supported\n");
		return 0;
	}

	state = gtidle->idle_status(gtidle_to_pc(gtidle));
	pg_enabled = gtidle->powergate_enable;

	/* Do not wake the GT to read powergating status */
	if (state != GT_IDLE_C6) {
		fw_ref = xe_force_wake_get(gt_to_fw(gt), XE_FW_GT);
		if (!fw_ref)
			return -ETIMEDOUT;

		pg_enabled = xe_mmio_read32(&gt->mmio, POWERGATE_ENABLE);
		pg_status = xe_mmio_read32(&gt->mmio, POWERGATE_DOMAIN_STATUS);

		xe_force_wake_put(gt_to_fw(gt), fw_ref);
	}

	if (gt->info.engine_mask & XE_HW_ENGINE_RCS_MASK) {
		drm_printf(p, "Render Power Gating Enabled: %s\n",
			   str_yes_no(pg_enabled & RENDER_POWERGATE_ENABLE));

		drm_printf(p, "Render Power Gate Status: %s\n",
			   str_up_down(pg_status & RENDER_AWAKE_STATUS));
	}

	vcs_mask = xe_hw_engine_mask_per_class(gt, XE_ENGINE_CLASS_VIDEO_DECODE);
	vecs_mask = xe_hw_engine_mask_per_class(gt, XE_ENGINE_CLASS_VIDEO_ENHANCE);

	/* Print media CPG status only if media is present */
	if (vcs_mask || vecs_mask) {
		drm_printf(p, "Media Power Gating Enabled: %s\n",
			   str_yes_no(pg_enabled & MEDIA_POWERGATE_ENABLE));

		for (n = 0; n < ARRAY_SIZE(media_slices); n++)
			if (gt->info.engine_mask & media_slices[n].engines)
				drm_printf(p, "Media Slice%d Power Gate Status: %s\n", n,
					   str_up_down(pg_status & media_slices[n].status_bit));
	}
	return 0;
}

static ssize_t name_show(struct kobject *kobj,
			 struct kobj_attribute *attr, char *buff)
{
	struct device *dev = kobj_to_dev(kobj);
	struct xe_gt_idle *gtidle = dev_to_gtidle(dev);
	struct xe_guc_pc *pc = gtidle_to_pc(gtidle);
	ssize_t ret;

	xe_pm_runtime_get(pc_to_xe(pc));
	ret = sysfs_emit(buff, "%s\n", gtidle->name);
	xe_pm_runtime_put(pc_to_xe(pc));

	return ret;
}
static struct kobj_attribute name_attr = __ATTR_RO(name);

static ssize_t idle_status_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *buff)
{
	struct device *dev = kobj_to_dev(kobj);
	struct xe_gt_idle *gtidle = dev_to_gtidle(dev);
	struct xe_guc_pc *pc = gtidle_to_pc(gtidle);
	enum xe_gt_idle_state state;

	xe_pm_runtime_get(pc_to_xe(pc));
	state = gtidle->idle_status(pc);
	xe_pm_runtime_put(pc_to_xe(pc));

	return sysfs_emit(buff, "%s\n", gt_idle_state_to_string(state));
}
static struct kobj_attribute idle_status_attr = __ATTR_RO(idle_status);

u64 xe_gt_idle_residency_msec(struct xe_gt_idle *gtidle)
{
	struct xe_guc_pc *pc = gtidle_to_pc(gtidle);
	u64 residency;
	unsigned long flags;

	raw_spin_lock_irqsave(&gtidle->lock, flags);
	residency = get_residency_ms(gtidle, gtidle->idle_residency(pc));
	raw_spin_unlock_irqrestore(&gtidle->lock, flags);

	return residency;
}


static ssize_t idle_residency_ms_show(struct kobject *kobj,
				      struct kobj_attribute *attr, char *buff)
{
	struct device *dev = kobj_to_dev(kobj);
	struct xe_gt_idle *gtidle = dev_to_gtidle(dev);
	struct xe_guc_pc *pc = gtidle_to_pc(gtidle);
	u64 residency;

	xe_pm_runtime_get(pc_to_xe(pc));
	residency = xe_gt_idle_residency_msec(gtidle);
	xe_pm_runtime_put(pc_to_xe(pc));

	return sysfs_emit(buff, "%llu\n", residency);
}
static struct kobj_attribute idle_residency_attr = __ATTR_RO(idle_residency_ms);

static const struct attribute *gt_idle_attrs[] = {
	&name_attr.attr,
	&idle_status_attr.attr,
	&idle_residency_attr.attr,
	NULL,
};

static void gt_idle_fini(void *arg)
{
	struct kobject *kobj = arg;
	struct xe_gt *gt = kobj_to_gt(kobj->parent);
	unsigned int fw_ref;

	xe_gt_idle_disable_pg(gt);

	if (gt_to_xe(gt)->info.skip_guc_pc) {
		fw_ref = xe_force_wake_get(gt_to_fw(gt), XE_FW_GT);
		xe_gt_idle_disable_c6(gt);
		xe_force_wake_put(gt_to_fw(gt), fw_ref);
	}

	sysfs_remove_files(kobj, gt_idle_attrs);
	kobject_put(kobj);
}

int xe_gt_idle_init(struct xe_gt_idle *gtidle)
{
	struct xe_gt *gt = gtidle_to_gt(gtidle);
	struct xe_device *xe = gt_to_xe(gt);
	struct kobject *kobj;
	int err;

	if (IS_SRIOV_VF(xe))
		return 0;

	kobj = kobject_create_and_add("gtidle", gt->sysfs);
	if (!kobj)
		return -ENOMEM;

	raw_spin_lock_init(&gtidle->lock);

	if (xe_gt_is_media_type(gt)) {
		snprintf(gtidle->name, sizeof(gtidle->name), "gt%d-mc", gt->info.id);
		gtidle->idle_residency = xe_guc_pc_mc6_residency;
	} else {
		snprintf(gtidle->name, sizeof(gtidle->name), "gt%d-rc", gt->info.id);
		gtidle->idle_residency = xe_guc_pc_rc6_residency;
	}

	/* Multiplier for Residency counter in units of 1.28us */
	gtidle->residency_multiplier = 1280;
	gtidle->idle_status = xe_guc_pc_c_status;

	err = sysfs_create_files(kobj, gt_idle_attrs);
	if (err) {
		kobject_put(kobj);
		return err;
	}

	xe_gt_idle_enable_pg(gt);

	return devm_add_action_or_reset(xe->drm.dev, gt_idle_fini, kobj);
}

void xe_gt_idle_enable_c6(struct xe_gt *gt)
{
	xe_device_assert_mem_access(gt_to_xe(gt));
	xe_force_wake_assert_held(gt_to_fw(gt), XE_FW_GT);

	if (IS_SRIOV_VF(gt_to_xe(gt)))
		return;

	/* Units of 1280 ns for a total of 5s */
	xe_mmio_write32(&gt->mmio, RC_IDLE_HYSTERSIS, 0x3B9ACA);
	/* Enable RC6 */
	xe_mmio_write32(&gt->mmio, RC_CONTROL,
			RC_CTL_HW_ENABLE | RC_CTL_TO_MODE | RC_CTL_RC6_ENABLE);
}

void xe_gt_idle_disable_c6(struct xe_gt *gt)
{
	xe_device_assert_mem_access(gt_to_xe(gt));
	xe_force_wake_assert_held(gt_to_fw(gt), XE_FW_GT);

	if (IS_SRIOV_VF(gt_to_xe(gt)))
		return;

	xe_mmio_write32(&gt->mmio, RC_CONTROL, 0);
	xe_mmio_write32(&gt->mmio, RC_STATE, 0);
}
