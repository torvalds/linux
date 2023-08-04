// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include <drm/drm_managed.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>

#include "xe_gt.h"
#include "xe_hw_engine_class_sysfs.h"

#define MAX_ENGINE_CLASS_NAME_LEN    16
static void kobj_xe_hw_engine_release(struct kobject *kobj)
{
	kfree(kobj);
}

static const struct kobj_type kobj_xe_hw_engine_type = {
	.release = kobj_xe_hw_engine_release,
	.sysfs_ops = &kobj_sysfs_ops
};

static void kobj_xe_hw_engine_fini(struct drm_device *drm, void *arg)
{
	struct kobject *kobj = arg;

	kobject_put(kobj);
}

	static struct kobject *
kobj_xe_hw_engine(struct xe_device *xe, struct kobject *parent, char *name)
{
	struct kobject *kobj;
	int err = 0;

	kobj = kzalloc(sizeof(*kobj), GFP_KERNEL);
	if (!kobj)
		return NULL;

	kobject_init(kobj, &kobj_xe_hw_engine_type);
	if (kobject_add(kobj, parent, "%s", name)) {
		kobject_put(kobj);
		return NULL;
	}

	err = drmm_add_action_or_reset(&xe->drm, kobj_xe_hw_engine_fini,
				       kobj);
	if (err)
		drm_warn(&xe->drm,
			 "%s: drmm_add_action_or_reset failed, err: %d\n",
			 __func__, err);

	return kobj;
}

static void xe_hw_engine_sysfs_kobj_release(struct kobject *kobj)
{
	kfree(kobj);
}

static const struct kobj_type xe_hw_engine_sysfs_kobj_type = {
	.release = xe_hw_engine_sysfs_kobj_release,
	.sysfs_ops = &kobj_sysfs_ops,
};

static void hw_engine_class_sysfs_fini(struct drm_device *drm, void *arg)
{
	struct kobject *kobj = arg;

	kobject_put(kobj);
}

/**
 * xe_hw_engine_class_sysfs_init - Init HW engine classes on GT.
 * @gt: Xe GT.
 *
 * This routine creates sysfs for HW engine classes and adds methods
 * to get/set different scheduling properties for HW engines class.
 *
 * Returns: Returns error value for failure and 0 for success.
 */
int xe_hw_engine_class_sysfs_init(struct xe_gt *gt)
{
	struct xe_device *xe = gt_to_xe(gt);
	struct xe_hw_engine *hwe;
	enum xe_hw_engine_id id;
	struct kobject *kobj;
	u16 class_mask = 0;
	int err = 0;

	kobj = kzalloc(sizeof(*kobj), GFP_KERNEL);
	if (!kobj)
		return -ENOMEM;

	kobject_init(kobj, &xe_hw_engine_sysfs_kobj_type);

	err = kobject_add(kobj, gt->sysfs, "engines");
	if (err) {
		kobject_put(kobj);
		return err;
	}

	for_each_hw_engine(hwe, gt, id) {
		char name[MAX_ENGINE_CLASS_NAME_LEN];
		struct kobject *khwe;

		if (hwe->class == XE_ENGINE_CLASS_OTHER ||
		    hwe->class == XE_ENGINE_CLASS_MAX)
			continue;

		if ((class_mask >> hwe->class) & 1)
			continue;

		class_mask |= 1 << hwe->class;

		switch (hwe->class) {
		case XE_ENGINE_CLASS_RENDER:
			strcpy(name, "rcs");
			break;
		case XE_ENGINE_CLASS_VIDEO_DECODE:
			strcpy(name, "vcs");
			break;
		case XE_ENGINE_CLASS_VIDEO_ENHANCE:
			strcpy(name, "vecs");
			break;
		case XE_ENGINE_CLASS_COPY:
			strcpy(name, "bcs");
			break;
		case XE_ENGINE_CLASS_COMPUTE:
			strcpy(name, "ccs");
			break;
		default:
			kobject_put(kobj);
			return -EINVAL;
		}

		khwe = kobj_xe_hw_engine(xe, kobj, name);
		if (!khwe) {
			kobject_put(kobj);
			return -EINVAL;
		}
	}

	err = drmm_add_action_or_reset(&xe->drm, hw_engine_class_sysfs_fini,
				       kobj);
	if (err)
		drm_warn(&xe->drm,
			 "%s: drmm_add_action_or_reset failed, err: %d\n",
			 __func__, err);

	return err;
}
