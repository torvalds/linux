/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_GT_SYSFS_H_
#define _XE_GT_SYSFS_H_

#include "xe_gt_sysfs_types.h"

void xe_gt_sysfs_init(struct xe_gt *gt);

static inline struct xe_gt *
kobj_to_gt(struct kobject *kobj)
{
	return container_of(kobj, struct kobj_gt, base)->gt;
}

#endif /* _XE_GT_SYSFS_H_ */
