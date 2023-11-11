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

struct intel_gt *intel_gt_sysfs_get_drvdata(struct kobject *kobj,
					    const char *name)
{
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
		struct device *dev = kobj_to_dev(kobj);
		struct drm_i915_private *i915 = kdev_minor_to_i915(dev);

		return to_gt(i915);
	}

	return kobj_to_gt(kobj);
}

static struct kobject *gt_get_parent_obj(struct intel_gt *gt)
{
	return &gt->i915->drm.primary->kdev->kobj;
}

static ssize_t id_show(struct kobject *kobj,
		       struct kobj_attribute *attr,
		       char *buf)
{
	struct intel_gt *gt = intel_gt_sysfs_get_drvdata(kobj, attr->attr.name);

	return sysfs_emit(buf, "%u\n", gt->info.id);
}
static struct kobj_attribute attr_id = __ATTR_RO(id);

static struct attribute *id_attrs[] = {
	&attr_id.attr,
	NULL,
};
ATTRIBUTE_GROUPS(id);

/* A kobject needs a release() method even if it does nothing */
static void kobj_gt_release(struct kobject *kobj)
{
}

static struct kobj_type kobj_gt_type = {
	.release = kobj_gt_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = id_groups,
};

void intel_gt_sysfs_register(struct intel_gt *gt)
{
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

	/* init and xfer ownership to sysfs tree */
	if (kobject_init_and_add(&gt->sysfs_gt, &kobj_gt_type,
				 gt->i915->sysfs_gt, "gt%d", gt->info.id))
		goto exit_fail;

	gt->sysfs_defaults = kobject_create_and_add(".defaults", &gt->sysfs_gt);
	if (!gt->sysfs_defaults)
		goto exit_fail;

	intel_gt_sysfs_pm_init(gt, &gt->sysfs_gt);

	return;

exit_fail:
	kobject_put(&gt->sysfs_gt);
	drm_warn(&gt->i915->drm,
		 "failed to initialize gt%d sysfs root\n", gt->info.id);
}

void intel_gt_sysfs_unregister(struct intel_gt *gt)
{
	kobject_put(gt->sysfs_defaults);
	kobject_put(&gt->sysfs_gt);
}
