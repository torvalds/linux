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
	u32 pg_enable;
	int i, j;

	if (IS_SRIOV_VF(xe))
		return;

	/* Disable CPG for PVC */
	if (xe->info.platform == XE_PVC)
		return;

	xe_device_assert_mem_access(gt_to_xe(gt));

	pg_enable = RENDER_POWERGATE_ENABLE | MEDIA_POWERGATE_ENABLE;

	for (i = XE_HW_ENGINE_VCS0, j = 0; i <= XE_HW_ENGINE_VCS7; ++i, ++j) {
		if ((gt->info.engine_mask & BIT(i)))
			pg_enable |= (VDN_HCP_POWERGATE_ENABLE(j) |
				      VDN_MFXVDENC_POWERGATE_ENABLE(j));
	}

	XE_WARN_ON(xe_force_wake_get(gt_to_fw(gt), XE_FW_GT));
	if (xe->info.skip_guc_pc) {
		/*
		 * GuC sets the hysteresis value when GuC PC is enabled
		 * else set it to 25 (25 * 1.28us)
		 */
		xe_mmio_write32(gt, MEDIA_POWERGATE_IDLE_HYSTERESIS, 25);
		xe_mmio_write32(gt, RENDER_POWERGATE_IDLE_HYSTERESIS, 25);
	}

	xe_mmio_write32(gt, POWERGATE_ENABLE, pg_enable);
	XE_WARN_ON(xe_force_wake_put(gt_to_fw(gt), XE_FW_GT));
}

void xe_gt_idle_disable_pg(struct xe_gt *gt)
{
	if (IS_SRIOV_VF(gt_to_xe(gt)))
		return;

	xe_device_assert_mem_access(gt_to_xe(gt));
	XE_WARN_ON(xe_force_wake_get(gt_to_fw(gt), XE_FW_GT));

	xe_mmio_write32(gt, POWERGATE_ENABLE, 0);

	XE_WARN_ON(xe_force_wake_put(gt_to_fw(gt), XE_FW_GT));
}

static ssize_t name_show(struct device *dev,
			 struct device_attribute *attr, char *buff)
{
	struct xe_gt_idle *gtidle = dev_to_gtidle(dev);
	struct xe_guc_pc *pc = gtidle_to_pc(gtidle);
	ssize_t ret;

	xe_pm_runtime_get(pc_to_xe(pc));
	ret = sysfs_emit(buff, "%s\n", gtidle->name);
	xe_pm_runtime_put(pc_to_xe(pc));

	return ret;
}
static DEVICE_ATTR_RO(name);

static ssize_t idle_status_show(struct device *dev,
				struct device_attribute *attr, char *buff)
{
	struct xe_gt_idle *gtidle = dev_to_gtidle(dev);
	struct xe_guc_pc *pc = gtidle_to_pc(gtidle);
	enum xe_gt_idle_state state;

	xe_pm_runtime_get(pc_to_xe(pc));
	state = gtidle->idle_status(pc);
	xe_pm_runtime_put(pc_to_xe(pc));

	return sysfs_emit(buff, "%s\n", gt_idle_state_to_string(state));
}
static DEVICE_ATTR_RO(idle_status);

static ssize_t idle_residency_ms_show(struct device *dev,
				      struct device_attribute *attr, char *buff)
{
	struct xe_gt_idle *gtidle = dev_to_gtidle(dev);
	struct xe_guc_pc *pc = gtidle_to_pc(gtidle);
	u64 residency;

	xe_pm_runtime_get(pc_to_xe(pc));
	residency = gtidle->idle_residency(pc);
	xe_pm_runtime_put(pc_to_xe(pc));

	return sysfs_emit(buff, "%llu\n", get_residency_ms(gtidle, residency));
}
static DEVICE_ATTR_RO(idle_residency_ms);

static const struct attribute *gt_idle_attrs[] = {
	&dev_attr_name.attr,
	&dev_attr_idle_status.attr,
	&dev_attr_idle_residency_ms.attr,
	NULL,
};

static void gt_idle_fini(void *arg)
{
	struct kobject *kobj = arg;
	struct xe_gt *gt = kobj_to_gt(kobj->parent);

	xe_gt_idle_disable_pg(gt);

	if (gt_to_xe(gt)->info.skip_guc_pc) {
		XE_WARN_ON(xe_force_wake_get(gt_to_fw(gt), XE_FW_GT));
		xe_gt_idle_disable_c6(gt);
		xe_force_wake_put(gt_to_fw(gt), XE_FW_GT);
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
	xe_mmio_write32(gt, RC_IDLE_HYSTERSIS, 0x3B9ACA);
	/* Enable RC6 */
	xe_mmio_write32(gt, RC_CONTROL,
			RC_CTL_HW_ENABLE | RC_CTL_TO_MODE | RC_CTL_RC6_ENABLE);
}

void xe_gt_idle_disable_c6(struct xe_gt *gt)
{
	xe_device_assert_mem_access(gt_to_xe(gt));
	xe_force_wake_assert_held(gt_to_fw(gt), XE_FW_GT);

	if (IS_SRIOV_VF(gt_to_xe(gt)))
		return;

	xe_mmio_write32(gt, RC_CONTROL, 0);
	xe_mmio_write32(gt, RC_STATE, 0);
}
