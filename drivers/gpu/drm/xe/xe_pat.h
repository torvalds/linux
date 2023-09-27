/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_PAT_H_
#define _XE_PAT_H_

struct xe_gt;
struct xe_device;

/**
 * xe_pat_init_early - SW initialization, setting up data based on device
 * @xe: xe device
 */
void xe_pat_init_early(struct xe_device *xe);

/**
 * xe_pat_init - Program HW PAT table
 * @gt: GT structure
 */
void xe_pat_init(struct xe_gt *gt);

#endif
