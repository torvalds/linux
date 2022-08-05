/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef __SYSFS_GT_H__
#define __SYSFS_GT_H__

#include <linux/ctype.h>
#include <linux/kobject.h>

#include "i915_gem.h" /* GEM_BUG_ON() */

struct intel_gt;

bool is_object_gt(struct kobject *kobj);

struct drm_i915_private *kobj_to_i915(struct kobject *kobj);

struct kobject *
intel_gt_create_kobj(struct intel_gt *gt,
		     struct kobject *dir,
		     const char *name);

void intel_gt_sysfs_register(struct intel_gt *gt);
void intel_gt_sysfs_unregister(struct intel_gt *gt);
struct intel_gt *intel_gt_sysfs_get_drvdata(struct device *dev,
					    const char *name);

#endif /* SYSFS_GT_H */
