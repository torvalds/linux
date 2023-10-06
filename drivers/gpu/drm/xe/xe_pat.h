/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_PAT_H_
#define _XE_PAT_H_

struct drm_printer;
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

/**
 * xe_pat_dump - Dump PAT table
 * @gt: GT structure
 * @p: Printer to dump info to
 */
void xe_pat_dump(struct xe_gt *gt, struct drm_printer *p);

#endif
