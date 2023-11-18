// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include <drm/drm_managed.h>

#include "xe_device.h"
#include "xe_gt.h"
#include "xe_gt_idle.h"
#include "xe_gt_sysfs.h"
#include "xe_guc_pc.h"
#include "regs/xe_gt_regs.h"
#include "xe_mmio.h"

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

static ssize_t name_show(struct device *dev,
			 struct device_attribute *attr, char *buff)
{
	struct xe_gt_idle *gtidle = dev_to_gtidle(dev);

	return sysfs_emit(buff, "%s\n", gtidle->name);
}
static DEVICE_ATTR_RO(name);

static ssize_t idle_status_show(struct device *dev,
				struct device_attribute *attr, char *buff)
{
	struct xe_gt_idle *gtidle = dev_to_gtidle(dev);
	struct xe_guc_pc *pc = gtidle_to_pc(gtidle);
	enum xe_gt_idle_state state;

	state = gtidle->idle_status(pc);

	return sysfs_emit(buff, "%s\n", gt_idle_state_to_string(state));
}
static DEVICE_ATTR_RO(idle_status);

static ssize_t idle_residency_ms_show(struct device *dev,
				      struct device_attribute *attr, char *buff)
{
	struct xe_gt_idle *gtidle = dev_to_gtidle(dev);
	struct xe_guc_pc *pc = gtidle_to_pc(gtidle);
	u64 residency;

	residency = gtidle->idle_residency(pc);
	return sysfs_emit(buff, "%llu\n", get_residency_ms(gtidle, residency));
}
static DEVICE_ATTR_RO(idle_residency_ms);

static const struct attribute *gt_idle_attrs[] = {
	&dev_attr_name.attr,
	&dev_attr_idle_status.attr,
	&dev_attr_idle_residency_ms.attr,
	NULL,
};

static void gt_idle_sysfs_fini(struct drm_device *drm, void *arg)
{
	struct kobject *kobj = arg;

	sysfs_remove_files(kobj, gt_idle_attrs);
	kobject_put(kobj);
}

void xe_gt_idle_sysfs_init(struct xe_gt_idle *gtidle)
{
	struct xe_gt *gt = gtidle_to_gt(gtidle);
	struct xe_device *xe = gt_to_xe(gt);
	struct kobject *kobj;
	int err;

	kobj = kobject_create_and_add("gtidle", gt->sysfs);
	if (!kobj) {
		drm_warn(&xe->drm, "%s failed, err: %d\n", __func__, -ENOMEM);
		return;
	}

	if (xe_gt_is_media_type(gt)) {
		sprintf(gtidle->name, "gt%d-mc\n", gt->info.id);
		gtidle->idle_residency = xe_guc_pc_mc6_residency;
	} else {
		sprintf(gtidle->name, "gt%d-rc\n", gt->info.id);
		gtidle->idle_residency = xe_guc_pc_rc6_residency;
	}

	/* Multiplier for Residency counter in units of 1.28us */
	gtidle->residency_multiplier = 1280;
	gtidle->idle_status = xe_guc_pc_c_status;

	err = sysfs_create_files(kobj, gt_idle_attrs);
	if (err) {
		kobject_put(kobj);
		drm_warn(&xe->drm, "failed to register gtidle sysfs, err: %d\n", err);
		return;
	}

	err = drmm_add_action_or_reset(&xe->drm, gt_idle_sysfs_fini, kobj);
	if (err)
		drm_warn(&xe->drm, "%s: drmm_add_action_or_reset failed, err: %d\n",
			 __func__, err);
}

void xe_gt_idle_enable_c6(struct xe_gt *gt)
{
	xe_device_assert_mem_access(gt_to_xe(gt));
	xe_force_wake_assert_held(gt_to_fw(gt), XE_FW_GT);

	/* Units of 1280 ns for a total of 5s */
	xe_mmio_write32(gt, RC_IDLE_HYSTERSIS, 0x3B9ACA);
	/* Enable RC6 */
	xe_mmio_write32(gt, RC_CONTROL,
			RC_CTL_HW_ENABLE | RC_CTL_TO_MODE | RC_CTL_RC6_ENABLE);
}

void xe_gt_idle_disable_c6(struct xe_gt *gt)
{
	xe_device_assert_mem_access(gt_to_xe(gt));
	xe_force_wake_assert_held(gt_to_fw(gt), XE_FORCEWAKE_ALL);

	xe_mmio_write32(gt, PG_ENABLE, 0);
	xe_mmio_write32(gt, RC_CONTROL, 0);
	xe_mmio_write32(gt, RC_STATE, 0);
}
