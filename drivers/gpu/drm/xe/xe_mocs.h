/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_MOCS_H_
#define _XE_MOCS_H_

#include <linux/types.h>

struct xe_exec_queue;
struct xe_gt;
struct drm_printer;

void xe_mocs_init_early(struct xe_gt *gt);
void xe_mocs_init(struct xe_gt *gt);

/**
 * xe_mocs_dump - Dump mocs table
 * @gt: GT structure
 * @p: Printer to dump info to
 */
void xe_mocs_dump(struct xe_gt *gt, struct drm_printer *p);

#endif
