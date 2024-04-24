/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_GT_THROTTLE_SYSFS_H_
#define _XE_GT_THROTTLE_SYSFS_H_

#include <drm/drm_managed.h>

struct xe_gt;

int xe_gt_throttle_sysfs_init(struct xe_gt *gt);

#endif /* _XE_GT_THROTTLE_SYSFS_H_ */

