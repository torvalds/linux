// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include <drm/drm_device.h>
#include <linux/sysfs.h>
#include <linux/printk.h>

#include "i915_drv.h"
#include "i915_sysfs.h"
#include "intel_gt.h"
#include "intel_gt_regs.h"
#include "intel_gt_sysfs.h"
#include "intel_gt_sysfs_pm.h"
#include "intel_rc6.h"

#ifdef CONFIG_PM
enum intel_gt_sysfs_op {
	INTEL_GT_SYSFS_MIN = 0,
	INTEL_GT_SYSFS_MAX,
};

static u32
sysfs_gt_attribute_r_func(struct device *dev, struct device_attribute *attr,
			  u32 (func)(struct intel_gt *gt),
			  enum intel_gt_sysfs_op op)
{
	struct intel_gt *gt;
	u32 ret;

	ret = (op == INTEL_GT_SYSFS_MAX) ? 0 : (u32) -1;

	if (!is_object_gt(&dev->kobj)) {
		int i;
		struct drm_i915_private *i915 = kdev_minor_to_i915(dev);

		for_each_gt(gt, i915, i) {
			u32 val = func(gt);

			switch (op) {
			case INTEL_GT_SYSFS_MIN:
				if (val < ret)
					ret = val;
				break;

			case INTEL_GT_SYSFS_MAX:
				if (val > ret)
					ret = val;
				break;
			}
		}
	} else {
		gt = intel_gt_sysfs_get_drvdata(dev, attr->attr.name);
		ret = func(gt);
	}

	return ret;
}

/* RC6 interfaces will show the minimum RC6 residency value */
#define sysfs_gt_attribute_r_min_func(d, a, f) \
		sysfs_gt_attribute_r_func(d, a, f, INTEL_GT_SYSFS_MIN)

#define sysfs_gt_attribute_r_max_func(d, a, f) \
		sysfs_gt_attribute_r_func(d, a, f, INTEL_GT_SYSFS_MAX)

static u32 get_residency(struct intel_gt *gt, i915_reg_t reg)
{
	intel_wakeref_t wakeref;
	u64 res = 0;

	with_intel_runtime_pm(gt->uncore->rpm, wakeref)
		res = intel_rc6_residency_us(&gt->rc6, reg);

	return DIV_ROUND_CLOSEST_ULL(res, 1000);
}

static ssize_t rc6_enable_show(struct device *dev,
			       struct device_attribute *attr,
			       char *buff)
{
	struct intel_gt *gt = intel_gt_sysfs_get_drvdata(dev, attr->attr.name);
	u8 mask = 0;

	if (HAS_RC6(gt->i915))
		mask |= BIT(0);
	if (HAS_RC6p(gt->i915))
		mask |= BIT(1);
	if (HAS_RC6pp(gt->i915))
		mask |= BIT(2);

	return sysfs_emit(buff, "%x\n", mask);
}

static u32 __rc6_residency_ms_show(struct intel_gt *gt)
{
	return get_residency(gt, GEN6_GT_GFX_RC6);
}

static ssize_t rc6_residency_ms_show(struct device *dev,
				     struct device_attribute *attr,
				     char *buff)
{
	u32 rc6_residency = sysfs_gt_attribute_r_min_func(dev, attr,
						      __rc6_residency_ms_show);

	return sysfs_emit(buff, "%u\n", rc6_residency);
}

static u32 __rc6p_residency_ms_show(struct intel_gt *gt)
{
	return get_residency(gt, GEN6_GT_GFX_RC6p);
}

static ssize_t rc6p_residency_ms_show(struct device *dev,
				      struct device_attribute *attr,
				      char *buff)
{
	u32 rc6p_residency = sysfs_gt_attribute_r_min_func(dev, attr,
						__rc6p_residency_ms_show);

	return sysfs_emit(buff, "%u\n", rc6p_residency);
}

static u32 __rc6pp_residency_ms_show(struct intel_gt *gt)
{
	return get_residency(gt, GEN6_GT_GFX_RC6pp);
}

static ssize_t rc6pp_residency_ms_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buff)
{
	u32 rc6pp_residency = sysfs_gt_attribute_r_min_func(dev, attr,
						__rc6pp_residency_ms_show);

	return sysfs_emit(buff, "%u\n", rc6pp_residency);
}

static u32 __media_rc6_residency_ms_show(struct intel_gt *gt)
{
	return get_residency(gt, VLV_GT_MEDIA_RC6);
}

static ssize_t media_rc6_residency_ms_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buff)
{
	u32 rc6_residency = sysfs_gt_attribute_r_min_func(dev, attr,
						__media_rc6_residency_ms_show);

	return sysfs_emit(buff, "%u\n", rc6_residency);
}

static DEVICE_ATTR_RO(rc6_enable);
static DEVICE_ATTR_RO(rc6_residency_ms);
static DEVICE_ATTR_RO(rc6p_residency_ms);
static DEVICE_ATTR_RO(rc6pp_residency_ms);
static DEVICE_ATTR_RO(media_rc6_residency_ms);

static struct attribute *rc6_attrs[] = {
	&dev_attr_rc6_enable.attr,
	&dev_attr_rc6_residency_ms.attr,
	NULL
};

static struct attribute *rc6p_attrs[] = {
	&dev_attr_rc6p_residency_ms.attr,
	&dev_attr_rc6pp_residency_ms.attr,
	NULL
};

static struct attribute *media_rc6_attrs[] = {
	&dev_attr_media_rc6_residency_ms.attr,
	NULL
};

static const struct attribute_group rc6_attr_group[] = {
	{ .attrs = rc6_attrs, },
	{ .name = power_group_name, .attrs = rc6_attrs, },
};

static const struct attribute_group rc6p_attr_group[] = {
	{ .attrs = rc6p_attrs, },
	{ .name = power_group_name, .attrs = rc6p_attrs, },
};

static const struct attribute_group media_rc6_attr_group[] = {
	{ .attrs = media_rc6_attrs, },
	{ .name = power_group_name, .attrs = media_rc6_attrs, },
};

static int __intel_gt_sysfs_create_group(struct kobject *kobj,
					 const struct attribute_group *grp)
{
	return is_object_gt(kobj) ?
	       sysfs_create_group(kobj, &grp[0]) :
	       sysfs_merge_group(kobj, &grp[1]);
}

static void intel_sysfs_rc6_init(struct intel_gt *gt, struct kobject *kobj)
{
	int ret;

	if (!HAS_RC6(gt->i915))
		return;

	ret = __intel_gt_sysfs_create_group(kobj, rc6_attr_group);
	if (ret)
		drm_warn(&gt->i915->drm,
			 "failed to create gt%u RC6 sysfs files (%pe)\n",
			 gt->info.id, ERR_PTR(ret));

	/*
	 * cannot use the is_visible() attribute because
	 * the upper object inherits from the parent group.
	 */
	if (HAS_RC6p(gt->i915)) {
		ret = __intel_gt_sysfs_create_group(kobj, rc6p_attr_group);
		if (ret)
			drm_warn(&gt->i915->drm,
				 "failed to create gt%u RC6p sysfs files (%pe)\n",
				 gt->info.id, ERR_PTR(ret));
	}

	if (IS_VALLEYVIEW(gt->i915) || IS_CHERRYVIEW(gt->i915)) {
		ret = __intel_gt_sysfs_create_group(kobj, media_rc6_attr_group);
		if (ret)
			drm_warn(&gt->i915->drm,
				 "failed to create media %u RC6 sysfs files (%pe)\n",
				 gt->info.id, ERR_PTR(ret));
	}
}
#else
static void intel_sysfs_rc6_init(struct intel_gt *gt, struct kobject *kobj)
{
}
#endif /* CONFIG_PM */

void intel_gt_sysfs_pm_init(struct intel_gt *gt, struct kobject *kobj)
{
	intel_sysfs_rc6_init(gt, kobj);
}
