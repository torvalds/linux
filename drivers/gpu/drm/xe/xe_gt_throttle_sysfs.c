// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include <drm/drm_managed.h>

#include <regs/xe_gt_regs.h>
#include "xe_device.h"
#include "xe_gt.h"
#include "xe_gt_sysfs.h"
#include "xe_gt_throttle_sysfs.h"
#include "xe_mmio.h"

/**
 * DOC: Xe GT Throttle
 *
 * Provides sysfs entries for frequency throttle reasons in GT
 *
 * device/gt#/freq0/throttle/status - Overall status
 * device/gt#/freq0/throttle/reason_pl1 - Frequency throttle due to PL1
 * device/gt#/freq0/throttle/reason_pl2 - Frequency throttle due to PL2
 * device/gt#/freq0/throttle/reason_pl4 - Frequency throttle due to PL4, Iccmax etc.
 * device/gt#/freq0/throttle/reason_thermal - Frequency throttle due to thermal
 * device/gt#/freq0/throttle/reason_prochot - Frequency throttle due to prochot
 * device/gt#/freq0/throttle/reason_ratl - Frequency throttle due to RATL
 * device/gt#/freq0/throttle/reason_vr_thermalert - Frequency throttle due to VR THERMALERT
 * device/gt#/freq0/throttle/reason_vr_tdc -  Frequency throttle due to VR TDC
 */

static struct xe_gt *
dev_to_gt(struct device *dev)
{
	return kobj_to_gt(dev->kobj.parent);
}

static u32 read_perf_limit_reasons(struct xe_gt *gt)
{
	u32 reg;

	if (xe_gt_is_media_type(gt))
		reg = xe_mmio_read32(gt, MTL_MEDIA_PERF_LIMIT_REASONS);
	else
		reg = xe_mmio_read32(gt, GT0_PERF_LIMIT_REASONS);

	return reg;
}

static u32 read_status(struct xe_gt *gt)
{
	u32 status = read_perf_limit_reasons(gt) & GT0_PERF_LIMIT_REASONS_MASK;

	return status;
}

static u32 read_reason_pl1(struct xe_gt *gt)
{
	u32 pl1 = read_perf_limit_reasons(gt) & POWER_LIMIT_1_MASK;

	return pl1;
}

static u32 read_reason_pl2(struct xe_gt *gt)
{
	u32 pl2 = read_perf_limit_reasons(gt) & POWER_LIMIT_2_MASK;

	return pl2;
}

static u32 read_reason_pl4(struct xe_gt *gt)
{
	u32 pl4 = read_perf_limit_reasons(gt) & POWER_LIMIT_4_MASK;

	return pl4;
}

static u32 read_reason_thermal(struct xe_gt *gt)
{
	u32 thermal = read_perf_limit_reasons(gt) & THERMAL_LIMIT_MASK;

	return thermal;
}

static u32 read_reason_prochot(struct xe_gt *gt)
{
	u32 prochot = read_perf_limit_reasons(gt) & PROCHOT_MASK;

	return prochot;
}

static u32 read_reason_ratl(struct xe_gt *gt)
{
	u32 ratl = read_perf_limit_reasons(gt) & RATL_MASK;

	return ratl;
}

static u32 read_reason_vr_thermalert(struct xe_gt *gt)
{
	u32 thermalert = read_perf_limit_reasons(gt) & VR_THERMALERT_MASK;

	return thermalert;
}

static u32 read_reason_vr_tdc(struct xe_gt *gt)
{
	u32 tdc = read_perf_limit_reasons(gt) & VR_TDC_MASK;

	return tdc;
}

static ssize_t status_show(struct device *dev,
			   struct device_attribute *attr,
			   char *buff)
{
	struct xe_gt *gt = dev_to_gt(dev);
	bool status = !!read_status(gt);

	return sysfs_emit(buff, "%u\n", status);
}
static DEVICE_ATTR_RO(status);

static ssize_t reason_pl1_show(struct device *dev,
			       struct device_attribute *attr,
			       char *buff)
{
	struct xe_gt *gt = dev_to_gt(dev);
	bool pl1 = !!read_reason_pl1(gt);

	return sysfs_emit(buff, "%u\n", pl1);
}
static DEVICE_ATTR_RO(reason_pl1);

static ssize_t reason_pl2_show(struct device *dev,
			       struct device_attribute *attr,
			       char *buff)
{
	struct xe_gt *gt = dev_to_gt(dev);
	bool pl2 = !!read_reason_pl2(gt);

	return sysfs_emit(buff, "%u\n", pl2);
}
static DEVICE_ATTR_RO(reason_pl2);

static ssize_t reason_pl4_show(struct device *dev,
			       struct device_attribute *attr,
			       char *buff)
{
	struct xe_gt *gt = dev_to_gt(dev);
	bool pl4 = !!read_reason_pl4(gt);

	return sysfs_emit(buff, "%u\n", pl4);
}
static DEVICE_ATTR_RO(reason_pl4);

static ssize_t reason_thermal_show(struct device *dev,
				   struct device_attribute *attr,
				   char *buff)
{
	struct xe_gt *gt = dev_to_gt(dev);
	bool thermal = !!read_reason_thermal(gt);

	return sysfs_emit(buff, "%u\n", thermal);
}
static DEVICE_ATTR_RO(reason_thermal);

static ssize_t reason_prochot_show(struct device *dev,
				   struct device_attribute *attr,
				   char *buff)
{
	struct xe_gt *gt = dev_to_gt(dev);
	bool prochot = !!read_reason_prochot(gt);

	return sysfs_emit(buff, "%u\n", prochot);
}
static DEVICE_ATTR_RO(reason_prochot);

static ssize_t reason_ratl_show(struct device *dev,
				struct device_attribute *attr,
				char *buff)
{
	struct xe_gt *gt = dev_to_gt(dev);
	bool ratl = !!read_reason_ratl(gt);

	return sysfs_emit(buff, "%u\n", ratl);
}
static DEVICE_ATTR_RO(reason_ratl);

static ssize_t reason_vr_thermalert_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buff)
{
	struct xe_gt *gt = dev_to_gt(dev);
	bool thermalert = !!read_reason_vr_thermalert(gt);

	return sysfs_emit(buff, "%u\n", thermalert);
}
static DEVICE_ATTR_RO(reason_vr_thermalert);

static ssize_t reason_vr_tdc_show(struct device *dev,
				  struct device_attribute *attr,
				  char *buff)
{
	struct xe_gt *gt = dev_to_gt(dev);
	bool tdc = !!read_reason_vr_tdc(gt);

	return sysfs_emit(buff, "%u\n", tdc);
}
static DEVICE_ATTR_RO(reason_vr_tdc);

static struct attribute *throttle_attrs[] = {
	&dev_attr_status.attr,
	&dev_attr_reason_pl1.attr,
	&dev_attr_reason_pl2.attr,
	&dev_attr_reason_pl4.attr,
	&dev_attr_reason_thermal.attr,
	&dev_attr_reason_prochot.attr,
	&dev_attr_reason_ratl.attr,
	&dev_attr_reason_vr_thermalert.attr,
	&dev_attr_reason_vr_tdc.attr,
	NULL
};

static const struct attribute_group throttle_group_attrs = {
	.name = "throttle",
	.attrs = throttle_attrs,
};

static void gt_throttle_sysfs_fini(struct drm_device *drm, void *arg)
{
	struct xe_gt *gt = arg;

	sysfs_remove_group(gt->freq, &throttle_group_attrs);
}

void xe_gt_throttle_sysfs_init(struct xe_gt *gt)
{
	struct xe_device *xe = gt_to_xe(gt);
	int err;

	err = sysfs_create_group(gt->freq, &throttle_group_attrs);
	if (err) {
		drm_warn(&xe->drm, "failed to register throttle sysfs, err: %d\n", err);
		return;
	}

	err = drmm_add_action_or_reset(&xe->drm, gt_throttle_sysfs_fini, gt);
	if (err)
		drm_warn(&xe->drm, "%s: drmm_add_action_or_reset failed, err: %d\n",
			 __func__, err);
}
