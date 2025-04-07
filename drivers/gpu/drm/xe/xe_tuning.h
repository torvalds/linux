/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_TUNING_
#define _XE_TUNING_

struct drm_printer;
struct xe_gt;
struct xe_hw_engine;

int xe_tuning_init(struct xe_gt *gt);
void xe_tuning_process_gt(struct xe_gt *gt);
void xe_tuning_process_engine(struct xe_hw_engine *hwe);
void xe_tuning_process_lrc(struct xe_hw_engine *hwe);
void xe_tuning_dump(struct xe_gt *gt, struct drm_printer *p);

#endif
