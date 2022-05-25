// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include <drm/drm_device.h>
#include <linux/device.h>
#include <linux/kobject.h>
#include <linux/printk.h>
#include <linux/sysfs.h>

#include "i915_drv.h"
#include "i915_sysfs.h"
#include "intel_gt.h"
#include "intel_gt_sysfs.h"
#include "intel_gt_sysfs_pm.h"
#include "intel_gt_types.h"
#include "intel_rc6.h"

bool is_object_gt(struct kobject *kobj)
{
	return !strncmp(kobj->name, "gt", 2);
}

static struct intel_gt *kobj_to_gt(struct kobject *kobj)
{
	return container_of(kobj, struct kobj_gt, base)->gt;
}

struct intel_gt *intel_gt_sysfs_get_drvdata(struct device *dev,
					    const char *name)
{
	struct kobject *kobj = &dev->kobj;

	/*
	 * We are interested at knowing from where the interface
	 * has been called, whether it's called from gt/ or from
	 * the parent directory.
	 * From the interface position it depends also the value of
	 * the private data.
	 * If the interface is called from gt/ then private data is
	 * of the "struct intel_gt *" type, otherwise it's * a
	 * "struct drm_i915_private *" type.
	 */
	if (!is_object_gt(kobj)) {
		struct drm_i915_private *i915 = kdev_minor_to_i915(dev);

		return to_gt(i915);
	}

	return kobj_to_gt(kobj);
}

static struct kobject *gt_get_parent_obj(struct intel_gt *gt)
{
	return &gt->i915->drm.primary->kdev->kobj;
}

static ssize_t id_show(struct device *dev,
		       struct device_attribute *attr,
		       char *buf)
{
	struct intel_gt *gt = intel_gt_sysfs_get_drvdata(dev, attr->attr.name);

	return sysfs_emit(buf, "%u\n", gt->info.id);
}
static DEVICE_ATTR_RO(id);

static struct attribute *id_attrs[] = {
	&dev_attr_id.attr,
	NULL,
};
ATTRIBUTE_GROUPS(id);

static void kobj_gt_release(struct kobject *kobj)
{
	kfree(kobj);
}

static struct kobj_type kobj_gt_type = {
	.release = kobj_gt_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = id_groups,
};

void intel_gt_sysfs_register(struct intel_gt *gt)
{
	struct kobj_gt *kg;

	/*
	 * We need to make things right with the
	 * ABI compatibility. The files were originally
	 * generated under the parent directory.
	 *
	 * We generate the files only for gt 0
	 * to avoid duplicates.
	 */
	if (gt_is_root(gt))
		intel_gt_sysfs_pm_init(gt, gt_get_parent_obj(gt));

	kg = kzalloc(sizeof(*kg), GFP_KERNEL);
	if (!kg)
		goto exit_fail;

	kobject_init(&kg->base, &kobj_gt_type);
	kg->gt = gt;

	/* xfer ownership to sysfs tree */
	if (kobject_add(&kg->base, gt->i915->sysfs_gt, "gt%d", gt->info.id))
		goto exit_kobj_put;

	intel_gt_sysfs_pm_init(gt, &kg->base);

	return;

exit_kobj_put:
	kobject_put(&kg->base);

exit_fail:
	drm_warn(&gt->i915->drm,
		 "failed to initialize gt%d sysfs root\n", gt->info.id);
}
