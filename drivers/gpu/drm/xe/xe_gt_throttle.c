// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include <drm/drm_managed.h>

#include <regs/xe_gt_regs.h>
#include "xe_device_types.h"
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
 * The ``reasons`` attribute can be used by sysadmin to monitor all possible
 * reasons for throttling and report them. It's preferred over monitoring
 * ``status`` and then reading the reason from individual attributes since that
 * is racy. If there's no throttling happening, "none" is returned.
 *
 * The following attributes are available on Crescent Island platform:
 *
 * - ``status``: Overall throttle status (0: no throttling, 1: throttling)
 * - ``reasons``: Array of reasons causing throttling separated by space
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
 * - ``status``: Overall throttle status (0: no throttling, 1: throttling)
 * - ``reasons``: Array of reasons causing throttling separated by space
 * - ``reason_pl1``: package PL1
 * - ``reason_pl2``: package PL2
 * - ``reason_pl4``: package PL4, Iccmax etc.
 * - ``reason_thermal``: thermal
 * - ``reason_prochot``: prochot
 * - ``reason_ratl``: RATL hermal algorithm
 * - ``reason_vr_thermalert``: VR THERMALERT
 * - ``reason_vr_tdc``: VR TDC
 */

struct throttle_attribute {
	struct kobj_attribute attr;
	u32 mask;
};

static struct xe_gt *dev_to_gt(struct device *dev)
{
	return kobj_to_gt(dev->kobj.parent);
}

static struct xe_gt *throttle_to_gt(struct kobject *kobj)
{
	return dev_to_gt(kobj_to_dev(kobj));
}

static struct throttle_attribute *kobj_attribute_to_throttle(struct kobj_attribute *attr)
{
	return container_of(attr, struct throttle_attribute, attr);
}

u32 xe_gt_throttle_get_limit_reasons(struct xe_gt *gt)
{
	struct xe_device *xe = gt_to_xe(gt);
	struct xe_reg reg;
	u32 mask;

	if (xe_gt_is_media_type(gt))
		reg = MTL_MEDIA_PERF_LIMIT_REASONS;
	else
		reg = GT0_PERF_LIMIT_REASONS;

	if (xe->info.platform == XE_CRESCENTISLAND)
		mask = CRI_PERF_LIMIT_REASONS_MASK;
	else
		mask = GT0_PERF_LIMIT_REASONS_MASK;

	guard(xe_pm_runtime)(xe);
	return xe_mmio_read32(&gt->mmio, reg) & mask;
}

static bool is_throttled_by(struct xe_gt *gt, u32 mask)
{
	return xe_gt_throttle_get_limit_reasons(gt) & mask;
}

static ssize_t reason_show(struct kobject *kobj,
			   struct kobj_attribute *attr, char *buff)
{
	struct throttle_attribute *ta = kobj_attribute_to_throttle(attr);
	struct xe_gt *gt = throttle_to_gt(kobj);

	return sysfs_emit(buff, "%u\n", is_throttled_by(gt, ta->mask));
}

static const struct attribute_group *get_platform_throttle_group(struct xe_device *xe);

static ssize_t reasons_show(struct kobject *kobj,
			    struct kobj_attribute *attr, char *buff)
{
	struct xe_gt *gt = throttle_to_gt(kobj);
	struct xe_device *xe = gt_to_xe(gt);
	const struct attribute_group *group;
	struct attribute **pother;
	ssize_t ret = 0;
	u32 reasons;

	reasons = xe_gt_throttle_get_limit_reasons(gt);
	if (!reasons)
		goto ret_none;

	group = get_platform_throttle_group(xe);
	for (pother = group->attrs; *pother; pother++) {
		struct kobj_attribute *kattr = container_of(*pother, struct kobj_attribute, attr);
		struct throttle_attribute *other_ta = kobj_attribute_to_throttle(kattr);

		if (other_ta->mask != U32_MAX && reasons & other_ta->mask)
			ret += sysfs_emit_at(buff, ret, "%s ", (*pother)->name + strlen("reason_"));
	}

	if (drm_WARN_ONCE(&xe->drm, !ret, "Unknown reason: %#x\n", reasons))
		goto ret_none;

	/* Drop extra space from last iteration above */
	ret--;
	ret += sysfs_emit_at(buff, ret, "\n");

	return ret;

ret_none:
	return sysfs_emit(buff, "none\n");
}

#define THROTTLE_ATTR_RO(name, _mask)				\
	struct throttle_attribute attr_##name =	{		\
		.attr = __ATTR(name, 0444, reason_show, NULL),	\
		.mask = _mask,					\
	}

#define THROTTLE_ATTR_RO_FUNC(name, _mask, _show)		\
	struct throttle_attribute attr_##name =	{		\
		.attr = __ATTR(name, 0444, _show, NULL),	\
		.mask = _mask,					\
	}

static THROTTLE_ATTR_RO_FUNC(reasons, 0, reasons_show);
static THROTTLE_ATTR_RO(status, U32_MAX);
static THROTTLE_ATTR_RO(reason_pl1, POWER_LIMIT_1_MASK);
static THROTTLE_ATTR_RO(reason_pl2, POWER_LIMIT_2_MASK);
static THROTTLE_ATTR_RO(reason_pl4, POWER_LIMIT_4_MASK);
static THROTTLE_ATTR_RO(reason_thermal, THERMAL_LIMIT_MASK);
static THROTTLE_ATTR_RO(reason_prochot, PROCHOT_MASK);
static THROTTLE_ATTR_RO(reason_ratl, RATL_MASK);
static THROTTLE_ATTR_RO(reason_vr_thermalert, VR_THERMALERT_MASK);
static THROTTLE_ATTR_RO(reason_vr_tdc, VR_TDC_MASK);

static struct attribute *throttle_attrs[] = {
	&attr_reasons.attr.attr,
	&attr_status.attr.attr,
	&attr_reason_pl1.attr.attr,
	&attr_reason_pl2.attr.attr,
	&attr_reason_pl4.attr.attr,
	&attr_reason_thermal.attr.attr,
	&attr_reason_prochot.attr.attr,
	&attr_reason_ratl.attr.attr,
	&attr_reason_vr_thermalert.attr.attr,
	&attr_reason_vr_tdc.attr.attr,
	NULL
};

static THROTTLE_ATTR_RO(reason_vr_thermal, VR_THERMAL_MASK);
static THROTTLE_ATTR_RO(reason_soc_thermal, SOC_THERMAL_LIMIT_MASK);
static THROTTLE_ATTR_RO(reason_mem_thermal, MEM_THERMAL_MASK);
static THROTTLE_ATTR_RO(reason_iccmax, ICCMAX_MASK);
static THROTTLE_ATTR_RO(reason_soc_avg_thermal, SOC_AVG_THERMAL_MASK);
static THROTTLE_ATTR_RO(reason_fastvmode, FASTVMODE_MASK);
static THROTTLE_ATTR_RO(reason_psys_pl1, PSYS_PL1_MASK);
static THROTTLE_ATTR_RO(reason_psys_pl2, PSYS_PL2_MASK);
static THROTTLE_ATTR_RO(reason_p0_freq, P0_FREQ_MASK);
static THROTTLE_ATTR_RO(reason_psys_crit, PSYS_CRIT_MASK);

static struct attribute *cri_throttle_attrs[] = {
	/* Common */
	&attr_reasons.attr.attr,
	&attr_status.attr.attr,
	&attr_reason_pl1.attr.attr,
	&attr_reason_pl2.attr.attr,
	&attr_reason_pl4.attr.attr,
	&attr_reason_prochot.attr.attr,
	&attr_reason_ratl.attr.attr,
	/* CRI */
	&attr_reason_vr_thermal.attr.attr,
	&attr_reason_soc_thermal.attr.attr,
	&attr_reason_mem_thermal.attr.attr,
	&attr_reason_iccmax.attr.attr,
	&attr_reason_soc_avg_thermal.attr.attr,
	&attr_reason_fastvmode.attr.attr,
	&attr_reason_psys_pl1.attr.attr,
	&attr_reason_psys_pl2.attr.attr,
	&attr_reason_p0_freq.attr.attr,
	&attr_reason_psys_crit.attr.attr,
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
