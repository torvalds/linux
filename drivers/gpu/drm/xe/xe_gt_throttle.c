// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include <drm/drm_managed.h>

#include <regs/xe_gt_regs.h>
#include "xe_device.h"
#include "xe_gt.h"
#include "xe_gt_sysfs.h"
#include "xe_gt_throttle.h"
#include "xe_mmio.h"
#include "xe_platform_types.h"
#include "xe_pm.h"

/**
 * DOC: Xe GT Throttle
 *
 * The GT frequency may be throttled by hardware/firmware for various reasons
 * that are provided through attributes under the ``freq0/throttle/`` directory.
 * Their availability depend on the platform and some may not be visible if that
 * reason is not available.
 *
 * The following attributes are available on Crescent Island platform:
 *
 * - ``status``: Overall throttle status
 * - ``reason_pl1``: package PL1
 * - ``reason_pl2``: package PL2
 * - ``reason_pl4``: package PL4
 * - ``reason_prochot``: prochot
 * - ``reason_soc_thermal``: SoC thermal
 * - ``reason_mem_thermal``: Memory thermal
 * - ``reason_vr_thermal``: VR thermal
 * - ``reason_iccmax``: ICCMAX
 * - ``reason_ratl``: RATL thermal algorithm
 * - ``reason_soc_avg_thermal``: SoC average temp
 * - ``reason_fastvmode``: VR is hitting FastVMode
 * - ``reason_psys_pl1``: PSYS PL1
 * - ``reason_psys_pl2``: PSYS PL2
 * - ``reason_p0_freq``: P0 frequency
 * - ``reason_psys_crit``: PSYS critical
 *
 * Other platforms support the following reasons:
 *
 * - ``status``: Overall status
 * - ``reason_pl1``: package PL1
 * - ``reason_pl2``: package PL2
 * - ``reason_pl4``: package PL4, Iccmax etc.
 * - ``reason_thermal``: thermal
 * - ``reason_prochot``: prochot
 * - ``reason_ratl``: RATL hermal algorithm
 * - ``reason_vr_thermalert``: VR THERMALERT
 * - ``reason_vr_tdc``: VR TDC
 */

static struct xe_gt *dev_to_gt(struct device *dev)
{
	return kobj_to_gt(dev->kobj.parent);
}

static struct xe_gt *throttle_to_gt(struct kobject *kobj)
{
	return dev_to_gt(kobj_to_dev(kobj));
}

u32 xe_gt_throttle_get_limit_reasons(struct xe_gt *gt)
{
	struct xe_device *xe = gt_to_xe(gt);
	struct xe_reg reg;
	u32 val, mask;

	if (xe_gt_is_media_type(gt))
		reg = MTL_MEDIA_PERF_LIMIT_REASONS;
	else
		reg = GT0_PERF_LIMIT_REASONS;

	if (xe->info.platform == XE_CRESCENTISLAND)
		mask = CRI_PERF_LIMIT_REASONS_MASK;
	else
		mask = GT0_PERF_LIMIT_REASONS_MASK;

	xe_pm_runtime_get(xe);
	val = xe_mmio_read32(&gt->mmio, reg) & mask;
	xe_pm_runtime_put(xe);

	return val;
}

static bool is_throttled_by(struct xe_gt *gt, u32 mask)
{
	return xe_gt_throttle_get_limit_reasons(gt) & mask;
}

static ssize_t status_show(struct kobject *kobj,
			   struct kobj_attribute *attr, char *buff)
{
	struct xe_gt *gt = throttle_to_gt(kobj);

	return sysfs_emit(buff, "%u\n", is_throttled_by(gt, U32_MAX));
}

static ssize_t reason_pl1_show(struct kobject *kobj,
			       struct kobj_attribute *attr, char *buff)
{
	struct xe_gt *gt = throttle_to_gt(kobj);

	return sysfs_emit(buff, "%u\n", is_throttled_by(gt, POWER_LIMIT_1_MASK));
}

static ssize_t reason_pl2_show(struct kobject *kobj,
			       struct kobj_attribute *attr, char *buff)
{
	struct xe_gt *gt = throttle_to_gt(kobj);

	return sysfs_emit(buff, "%u\n", is_throttled_by(gt, POWER_LIMIT_2_MASK));
}

static ssize_t reason_pl4_show(struct kobject *kobj,
			       struct kobj_attribute *attr, char *buff)
{
	struct xe_gt *gt = throttle_to_gt(kobj);

	return sysfs_emit(buff, "%u\n", is_throttled_by(gt, POWER_LIMIT_4_MASK));
}

static ssize_t reason_thermal_show(struct kobject *kobj,
				   struct kobj_attribute *attr, char *buff)
{
	struct xe_gt *gt = throttle_to_gt(kobj);

	return sysfs_emit(buff, "%u\n", is_throttled_by(gt, THERMAL_LIMIT_MASK));
}

static ssize_t reason_soc_thermal_show(struct kobject *kobj,
				       struct kobj_attribute *attr, char *buff)
{
	struct xe_gt *gt = throttle_to_gt(kobj);

	return sysfs_emit(buff, "%u\n", is_throttled_by(gt, SOC_THERMAL_LIMIT_MASK));
}

static ssize_t reason_prochot_show(struct kobject *kobj,
				   struct kobj_attribute *attr, char *buff)
{
	struct xe_gt *gt = throttle_to_gt(kobj);

	return sysfs_emit(buff, "%u\n", is_throttled_by(gt, PROCHOT_MASK));
}

static ssize_t reason_ratl_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *buff)
{
	struct xe_gt *gt = throttle_to_gt(kobj);

	return sysfs_emit(buff, "%u\n", is_throttled_by(gt, RATL_MASK));
}

static ssize_t reason_vr_thermalert_show(struct kobject *kobj,
					 struct kobj_attribute *attr, char *buff)
{
	struct xe_gt *gt = throttle_to_gt(kobj);

	return sysfs_emit(buff, "%u\n", is_throttled_by(gt, VR_THERMALERT_MASK));
}

static ssize_t reason_soc_avg_thermal_show(struct kobject *kobj,
					   struct kobj_attribute *attr, char *buff)
{
	struct xe_gt *gt = throttle_to_gt(kobj);

	return sysfs_emit(buff, "%u\n", is_throttled_by(gt, SOC_AVG_THERMAL_MASK));
}

static ssize_t reason_vr_tdc_show(struct kobject *kobj,
				  struct kobj_attribute *attr, char *buff)
{
	struct xe_gt *gt = throttle_to_gt(kobj);

	return sysfs_emit(buff, "%u\n", is_throttled_by(gt, VR_TDC_MASK));
}

static ssize_t reason_fastvmode_show(struct kobject *kobj,
				     struct kobj_attribute *attr, char *buff)
{
	struct xe_gt *gt = throttle_to_gt(kobj);

	return sysfs_emit(buff, "%u\n", is_throttled_by(gt, FASTVMODE_MASK));
}

static ssize_t reason_mem_thermal_show(struct kobject *kobj,
				       struct kobj_attribute *attr, char *buff)
{
	struct xe_gt *gt = throttle_to_gt(kobj);

	return sysfs_emit(buff, "%u\n", is_throttled_by(gt, MEM_THERMAL_MASK));
}

static ssize_t reason_vr_thermal_show(struct kobject *kobj,
				      struct kobj_attribute *attr, char *buff)
{
	struct xe_gt *gt = throttle_to_gt(kobj);

	return sysfs_emit(buff, "%u\n", is_throttled_by(gt, VR_THERMAL_MASK));
}

static ssize_t reason_iccmax_show(struct kobject *kobj,
				  struct kobj_attribute *attr, char *buff)
{
	struct xe_gt *gt = throttle_to_gt(kobj);

	return sysfs_emit(buff, "%u\n", is_throttled_by(gt, ICCMAX_MASK));
}

static ssize_t reason_psys_pl1_show(struct kobject *kobj,
				    struct kobj_attribute *attr, char *buff)
{
	struct xe_gt *gt = throttle_to_gt(kobj);

	return sysfs_emit(buff, "%u\n", is_throttled_by(gt, PSYS_PL1_MASK));
}

static ssize_t reason_psys_pl2_show(struct kobject *kobj,
				    struct kobj_attribute *attr, char *buff)
{
	struct xe_gt *gt = throttle_to_gt(kobj);

	return sysfs_emit(buff, "%u\n", is_throttled_by(gt, PSYS_PL2_MASK));
}

static ssize_t reason_p0_freq_show(struct kobject *kobj,
				   struct kobj_attribute *attr, char *buff)
{
	struct xe_gt *gt = throttle_to_gt(kobj);

	return sysfs_emit(buff, "%u\n", is_throttled_by(gt, P0_FREQ_MASK));
}

static ssize_t reason_psys_crit_show(struct kobject *kobj,
				     struct kobj_attribute *attr, char *buff)
{
	struct xe_gt *gt = throttle_to_gt(kobj);

	return sysfs_emit(buff, "%u\n", is_throttled_by(gt, PSYS_CRIT_MASK));
}

#define THROTTLE_ATTR_RO(name) \
	struct kobj_attribute attr_##name = __ATTR_RO(name)

static THROTTLE_ATTR_RO(status);
static THROTTLE_ATTR_RO(reason_pl1);
static THROTTLE_ATTR_RO(reason_pl2);
static THROTTLE_ATTR_RO(reason_pl4);
static THROTTLE_ATTR_RO(reason_thermal);
static THROTTLE_ATTR_RO(reason_prochot);
static THROTTLE_ATTR_RO(reason_ratl);
static THROTTLE_ATTR_RO(reason_vr_thermalert);
static THROTTLE_ATTR_RO(reason_vr_tdc);

static struct attribute *throttle_attrs[] = {
	&attr_status.attr,
	&attr_reason_pl1.attr,
	&attr_reason_pl2.attr,
	&attr_reason_pl4.attr,
	&attr_reason_thermal.attr,
	&attr_reason_prochot.attr,
	&attr_reason_ratl.attr,
	&attr_reason_vr_thermalert.attr,
	&attr_reason_vr_tdc.attr,
	NULL
};

static THROTTLE_ATTR_RO(reason_vr_thermal);
static THROTTLE_ATTR_RO(reason_soc_thermal);
static THROTTLE_ATTR_RO(reason_mem_thermal);
static THROTTLE_ATTR_RO(reason_iccmax);
static THROTTLE_ATTR_RO(reason_soc_avg_thermal);
static THROTTLE_ATTR_RO(reason_fastvmode);
static THROTTLE_ATTR_RO(reason_psys_pl1);
static THROTTLE_ATTR_RO(reason_psys_pl2);
static THROTTLE_ATTR_RO(reason_p0_freq);
static THROTTLE_ATTR_RO(reason_psys_crit);

static struct attribute *cri_throttle_attrs[] = {
	/* Common */
	&attr_status.attr,
	&attr_reason_pl1.attr,
	&attr_reason_pl2.attr,
	&attr_reason_pl4.attr,
	&attr_reason_prochot.attr,
	&attr_reason_ratl.attr,
	/* CRI */
	&attr_reason_vr_thermal.attr,
	&attr_reason_soc_thermal.attr,
	&attr_reason_mem_thermal.attr,
	&attr_reason_iccmax.attr,
	&attr_reason_soc_avg_thermal.attr,
	&attr_reason_fastvmode.attr,
	&attr_reason_psys_pl1.attr,
	&attr_reason_psys_pl2.attr,
	&attr_reason_p0_freq.attr,
	&attr_reason_psys_crit.attr,
	NULL
};

static const struct attribute_group throttle_group_attrs = {
	.name = "throttle",
	.attrs = throttle_attrs,
};

static const struct attribute_group cri_throttle_group_attrs = {
	.name = "throttle",
	.attrs = cri_throttle_attrs,
};

static const struct attribute_group *get_platform_throttle_group(struct xe_device *xe)
{
	switch (xe->info.platform) {
	case XE_CRESCENTISLAND:
		return &cri_throttle_group_attrs;
	default:
		return &throttle_group_attrs;
	}
}

static void gt_throttle_sysfs_fini(void *arg)
{
	struct xe_gt *gt = arg;
	struct xe_device *xe = gt_to_xe(gt);
	const struct attribute_group *group = get_platform_throttle_group(xe);

	sysfs_remove_group(gt->freq, group);
}

int xe_gt_throttle_init(struct xe_gt *gt)
{
	struct xe_device *xe = gt_to_xe(gt);
	const struct attribute_group *group = get_platform_throttle_group(xe);
	int err;

	err = sysfs_create_group(gt->freq, group);
	if (err)
		return err;

	return devm_add_action_or_reset(xe->drm.dev, gt_throttle_sysfs_fini, gt);
}
