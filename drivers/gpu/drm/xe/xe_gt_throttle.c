// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include <drm/drm_managed.h>

#include <regs/xe_gt_regs.h>
#include "xe_device.h"
#include "xe_gt.h"
#include "xe_gt_printk.h"
#include "xe_gt_sysfs.h"
#include "xe_gt_throttle.h"
#include "xe_mmio.h"
#include "xe_platform_types.h"
#include "xe_pm.h"

/**
 * DOC: Xe GT Throttle
 *
 * Provides sysfs entries and other helpers for frequency throttle reasons in GT
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
 *
 * The following attributes are available on Crescent Island platform:
 * device/gt#/freq0/throttle/status - Overall throttle status
 * device/gt#/freq0/throttle/reason_pl1 - Frequency throttle due to package PL1
 * device/gt#/freq0/throttle/reason_pl2 - Frequency throttle due to package PL2
 * device/gt#/freq0/throttle/reason_pl4 - Frequency throttle due to PL4
 * device/gt#/freq0/throttle/reason_prochot - Frequency throttle due to prochot
 * device/gt#/freq0/throttle/reason_soc_thermal - Frequency throttle due to SoC thermal
 * device/gt#/freq0/throttle/reason_mem_thermal - Frequency throttle due to memory thermal
 * device/gt#/freq0/throttle/reason_vr_thermal - Frequency throttle due to VR thermal
 * device/gt#/freq0/throttle/reason_iccmax - Frequency throttle due to ICCMAX
 * device/gt#/freq0/throttle/reason_ratl - Frequency throttle due to RATL thermal algorithm
 * device/gt#/freq0/throttle/reason_soc_avg_thermal - Frequency throttle due to SoC average temp
 * device/gt#/freq0/throttle/reason_fastvmode - Frequency throttle due to VR is hitting FastVMode
 * device/gt#/freq0/throttle/reason_psys_pl1 - Frequency throttle due to PSYS PL1
 * device/gt#/freq0/throttle/reason_psys_pl2 - Frequency throttle due to PSYS PL2
 * device/gt#/freq0/throttle/reason_p0_freq - Frequency throttle due to P0 frequency
 * device/gt#/freq0/throttle/reason_psys_crit - Frequency throttle due to PSYS critical
 */

static struct xe_gt *
dev_to_gt(struct device *dev)
{
	return kobj_to_gt(dev->kobj.parent);
}

u32 xe_gt_throttle_get_limit_reasons(struct xe_gt *gt)
{
	u32 reg;

	xe_pm_runtime_get(gt_to_xe(gt));
	if (xe_gt_is_media_type(gt))
		reg = xe_mmio_read32(&gt->mmio, MTL_MEDIA_PERF_LIMIT_REASONS);
	else
		reg = xe_mmio_read32(&gt->mmio, GT0_PERF_LIMIT_REASONS);
	xe_pm_runtime_put(gt_to_xe(gt));

	return reg;
}

static u32 read_status(struct xe_gt *gt)
{
	struct xe_device *xe = gt_to_xe(gt);
	u32 status, mask;

	if (xe->info.platform == XE_CRESCENTISLAND)
		mask = CRI_PERF_LIMIT_REASONS_MASK;
	else
		mask = GT0_PERF_LIMIT_REASONS_MASK;

	status = xe_gt_throttle_get_limit_reasons(gt) & mask;
	xe_gt_dbg(gt, "throttle reasons: 0x%08x\n", status);

	return status;
}

static u32 read_reason_pl1(struct xe_gt *gt)
{
	u32 pl1 = xe_gt_throttle_get_limit_reasons(gt) & POWER_LIMIT_1_MASK;

	return pl1;
}

static u32 read_reason_pl2(struct xe_gt *gt)
{
	u32 pl2 = xe_gt_throttle_get_limit_reasons(gt) & POWER_LIMIT_2_MASK;

	return pl2;
}

static u32 read_reason_pl4(struct xe_gt *gt)
{
	u32 pl4 = xe_gt_throttle_get_limit_reasons(gt) & POWER_LIMIT_4_MASK;

	return pl4;
}

static u32 read_reason_thermal(struct xe_gt *gt)
{
	u32 thermal = xe_gt_throttle_get_limit_reasons(gt) & THERMAL_LIMIT_MASK;

	return thermal;
}

static u32 read_reason_soc_thermal(struct xe_gt *gt)
{
	u32 thermal = xe_gt_throttle_get_limit_reasons(gt) & SOC_THERMAL_LIMIT_MASK;

	return thermal;
}

static u32 read_reason_prochot(struct xe_gt *gt)
{
	u32 prochot = xe_gt_throttle_get_limit_reasons(gt) & PROCHOT_MASK;

	return prochot;
}

static u32 read_reason_ratl(struct xe_gt *gt)
{
	u32 ratl = xe_gt_throttle_get_limit_reasons(gt) & RATL_MASK;

	return ratl;
}

static u32 read_reason_vr_thermalert(struct xe_gt *gt)
{
	u32 thermalert = xe_gt_throttle_get_limit_reasons(gt) & VR_THERMALERT_MASK;

	return thermalert;
}

static u32 read_reason_soc_avg_thermal(struct xe_gt *gt)
{
	u32 soc_avg_thermal = xe_gt_throttle_get_limit_reasons(gt) & SOC_AVG_THERMAL_MASK;

	return soc_avg_thermal;
}

static u32 read_reason_vr_tdc(struct xe_gt *gt)
{
	u32 tdc = xe_gt_throttle_get_limit_reasons(gt) & VR_TDC_MASK;

	return tdc;
}

static u32 read_reason_fastvmode(struct xe_gt *gt)
{
	u32 fastvmode = xe_gt_throttle_get_limit_reasons(gt) & FASTVMODE_MASK;

	return fastvmode;
}

static u32 read_reason_mem_thermal(struct xe_gt *gt)
{
	u32 mem_thermal = xe_gt_throttle_get_limit_reasons(gt) & MEM_THERMAL_MASK;

	return mem_thermal;
}

static u32 read_reason_vr_thermal(struct xe_gt *gt)
{
	u32 vr_thermal = xe_gt_throttle_get_limit_reasons(gt) & VR_THERMAL_MASK;

	return vr_thermal;
}

static u32 read_reason_iccmax(struct xe_gt *gt)
{
	u32 iccmax = xe_gt_throttle_get_limit_reasons(gt) & ICCMAX_MASK;

	return iccmax;
}

static u32 read_reason_psys_pl1(struct xe_gt *gt)
{
	u32 psys_pl1 = xe_gt_throttle_get_limit_reasons(gt) & PSYS_PL1_MASK;

	return psys_pl1;
}

static u32 read_reason_psys_pl2(struct xe_gt *gt)
{
	u32 psys_pl2 = xe_gt_throttle_get_limit_reasons(gt) & PSYS_PL2_MASK;

	return psys_pl2;
}

static u32 read_reason_p0_freq(struct xe_gt *gt)
{
	u32 p0_freq = xe_gt_throttle_get_limit_reasons(gt) & P0_FREQ_MASK;

	return p0_freq;
}

static u32 read_reason_psys_crit(struct xe_gt *gt)
{
	u32 psys_crit = xe_gt_throttle_get_limit_reasons(gt) & PSYS_CRIT_MASK;

	return psys_crit;
}

static ssize_t status_show(struct kobject *kobj,
			   struct kobj_attribute *attr, char *buff)
{
	struct device *dev = kobj_to_dev(kobj);
	struct xe_gt *gt = dev_to_gt(dev);
	bool status = !!read_status(gt);

	return sysfs_emit(buff, "%u\n", status);
}
static struct kobj_attribute attr_status = __ATTR_RO(status);

static ssize_t reason_pl1_show(struct kobject *kobj,
			       struct kobj_attribute *attr, char *buff)
{
	struct device *dev = kobj_to_dev(kobj);
	struct xe_gt *gt = dev_to_gt(dev);
	bool pl1 = !!read_reason_pl1(gt);

	return sysfs_emit(buff, "%u\n", pl1);
}
static struct kobj_attribute attr_reason_pl1 = __ATTR_RO(reason_pl1);

static ssize_t reason_pl2_show(struct kobject *kobj,
			       struct kobj_attribute *attr, char *buff)
{
	struct device *dev = kobj_to_dev(kobj);
	struct xe_gt *gt = dev_to_gt(dev);
	bool pl2 = !!read_reason_pl2(gt);

	return sysfs_emit(buff, "%u\n", pl2);
}
static struct kobj_attribute attr_reason_pl2 = __ATTR_RO(reason_pl2);

static ssize_t reason_pl4_show(struct kobject *kobj,
			       struct kobj_attribute *attr, char *buff)
{
	struct device *dev = kobj_to_dev(kobj);
	struct xe_gt *gt = dev_to_gt(dev);
	bool pl4 = !!read_reason_pl4(gt);

	return sysfs_emit(buff, "%u\n", pl4);
}
static struct kobj_attribute attr_reason_pl4 = __ATTR_RO(reason_pl4);

static ssize_t reason_thermal_show(struct kobject *kobj,
				   struct kobj_attribute *attr, char *buff)
{
	struct device *dev = kobj_to_dev(kobj);
	struct xe_gt *gt = dev_to_gt(dev);
	bool thermal = !!read_reason_thermal(gt);

	return sysfs_emit(buff, "%u\n", thermal);
}
static struct kobj_attribute attr_reason_thermal = __ATTR_RO(reason_thermal);

static ssize_t reason_soc_thermal_show(struct kobject *kobj,
				       struct kobj_attribute *attr, char *buff)
{
	struct device *dev = kobj_to_dev(kobj);
	struct xe_gt *gt = dev_to_gt(dev);
	bool thermal = !!read_reason_soc_thermal(gt);

	return sysfs_emit(buff, "%u\n", thermal);
}
static struct kobj_attribute attr_reason_soc_thermal = __ATTR_RO(reason_soc_thermal);

static ssize_t reason_prochot_show(struct kobject *kobj,
				   struct kobj_attribute *attr, char *buff)
{
	struct device *dev = kobj_to_dev(kobj);
	struct xe_gt *gt = dev_to_gt(dev);
	bool prochot = !!read_reason_prochot(gt);

	return sysfs_emit(buff, "%u\n", prochot);
}
static struct kobj_attribute attr_reason_prochot = __ATTR_RO(reason_prochot);

static ssize_t reason_ratl_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *buff)
{
	struct device *dev = kobj_to_dev(kobj);
	struct xe_gt *gt = dev_to_gt(dev);
	bool ratl = !!read_reason_ratl(gt);

	return sysfs_emit(buff, "%u\n", ratl);
}
static struct kobj_attribute attr_reason_ratl = __ATTR_RO(reason_ratl);

static ssize_t reason_vr_thermalert_show(struct kobject *kobj,
					 struct kobj_attribute *attr, char *buff)
{
	struct device *dev = kobj_to_dev(kobj);
	struct xe_gt *gt = dev_to_gt(dev);
	bool thermalert = !!read_reason_vr_thermalert(gt);

	return sysfs_emit(buff, "%u\n", thermalert);
}
static struct kobj_attribute attr_reason_vr_thermalert = __ATTR_RO(reason_vr_thermalert);

static ssize_t reason_soc_avg_thermal_show(struct kobject *kobj,
					   struct kobj_attribute *attr, char *buff)
{
	struct device *dev = kobj_to_dev(kobj);
	struct xe_gt *gt = dev_to_gt(dev);
	bool avg_thermalert = !!read_reason_soc_avg_thermal(gt);

	return sysfs_emit(buff, "%u\n", avg_thermalert);
}
static struct kobj_attribute attr_reason_soc_avg_thermal = __ATTR_RO(reason_soc_avg_thermal);

static ssize_t reason_vr_tdc_show(struct kobject *kobj,
				  struct kobj_attribute *attr, char *buff)
{
	struct device *dev = kobj_to_dev(kobj);
	struct xe_gt *gt = dev_to_gt(dev);
	bool tdc = !!read_reason_vr_tdc(gt);

	return sysfs_emit(buff, "%u\n", tdc);
}
static struct kobj_attribute attr_reason_vr_tdc = __ATTR_RO(reason_vr_tdc);

static ssize_t reason_fastvmode_show(struct kobject *kobj,
				     struct kobj_attribute *attr, char *buff)
{
	struct device *dev = kobj_to_dev(kobj);
	struct xe_gt *gt = dev_to_gt(dev);
	bool fastvmode = !!read_reason_fastvmode(gt);

	return sysfs_emit(buff, "%u\n", fastvmode);
}
static struct kobj_attribute attr_reason_fastvmode = __ATTR_RO(reason_fastvmode);

static ssize_t reason_mem_thermal_show(struct kobject *kobj,
				       struct kobj_attribute *attr, char *buff)
{
	struct device *dev = kobj_to_dev(kobj);
	struct xe_gt *gt = dev_to_gt(dev);
	bool mem_thermal = !!read_reason_mem_thermal(gt);

	return sysfs_emit(buff, "%u\n", mem_thermal);
}
static struct kobj_attribute attr_reason_mem_thermal = __ATTR_RO(reason_mem_thermal);

static ssize_t reason_vr_thermal_show(struct kobject *kobj,
				      struct kobj_attribute *attr, char *buff)
{
	struct device *dev = kobj_to_dev(kobj);
	struct xe_gt *gt = dev_to_gt(dev);
	bool vr_thermal = !!read_reason_vr_thermal(gt);

	return sysfs_emit(buff, "%u\n", vr_thermal);
}
static struct kobj_attribute attr_reason_vr_thermal = __ATTR_RO(reason_vr_thermal);

static ssize_t reason_iccmax_show(struct kobject *kobj,
				  struct kobj_attribute *attr, char *buff)
{
	struct device *dev = kobj_to_dev(kobj);
	struct xe_gt *gt = dev_to_gt(dev);
	bool iccmax = !!read_reason_iccmax(gt);

	return sysfs_emit(buff, "%u\n", iccmax);
}
static struct kobj_attribute attr_reason_iccmax = __ATTR_RO(reason_iccmax);

static ssize_t reason_psys_pl1_show(struct kobject *kobj,
				    struct kobj_attribute *attr, char *buff)
{
	struct device *dev = kobj_to_dev(kobj);
	struct xe_gt *gt = dev_to_gt(dev);
	bool psys_pl1 = !!read_reason_psys_pl1(gt);

	return sysfs_emit(buff, "%u\n", psys_pl1);
}
static struct kobj_attribute attr_reason_psys_pl1 = __ATTR_RO(reason_psys_pl1);

static ssize_t reason_psys_pl2_show(struct kobject *kobj,
				    struct kobj_attribute *attr, char *buff)
{
	struct device *dev = kobj_to_dev(kobj);
	struct xe_gt *gt = dev_to_gt(dev);
	bool psys_pl2 = !!read_reason_psys_pl2(gt);

	return sysfs_emit(buff, "%u\n", psys_pl2);
}
static struct kobj_attribute attr_reason_psys_pl2 = __ATTR_RO(reason_psys_pl2);

static ssize_t reason_p0_freq_show(struct kobject *kobj,
				   struct kobj_attribute *attr, char *buff)
{
	struct device *dev = kobj_to_dev(kobj);
	struct xe_gt *gt = dev_to_gt(dev);
	bool p0_freq = !!read_reason_p0_freq(gt);

	return sysfs_emit(buff, "%u\n", p0_freq);
}
static struct kobj_attribute attr_reason_p0_freq = __ATTR_RO(reason_p0_freq);

static ssize_t reason_psys_crit_show(struct kobject *kobj,
				     struct kobj_attribute *attr, char *buff)
{
	struct device *dev = kobj_to_dev(kobj);
	struct xe_gt *gt = dev_to_gt(dev);
	bool psys_crit = !!read_reason_psys_crit(gt);

	return sysfs_emit(buff, "%u\n", psys_crit);
}
static struct kobj_attribute attr_reason_psys_crit = __ATTR_RO(reason_psys_crit);

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

static struct attribute *cri_throttle_attrs[] = {
	&attr_status.attr,
	&attr_reason_prochot.attr,
	&attr_reason_soc_thermal.attr,
	&attr_reason_mem_thermal.attr,
	&attr_reason_vr_thermal.attr,
	&attr_reason_iccmax.attr,
	&attr_reason_ratl.attr,
	&attr_reason_soc_avg_thermal.attr,
	&attr_reason_fastvmode.attr,
	&attr_reason_pl4.attr,
	&attr_reason_pl1.attr,
	&attr_reason_pl2.attr,
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
