/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_GT_CLOCK_H_
#define _XE_GT_CLOCK_H_

#include <linux/types.h>

struct xe_gt;

int xe_gt_clock_init(struct xe_gt *gt);

#endif
