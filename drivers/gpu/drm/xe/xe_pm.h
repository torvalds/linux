/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_PM_H_
#define _XE_PM_H_

#include <linux/pm_runtime.h>

/*
 * TODO: Threshold = 0 will block D3Cold.
 *       Before we can move this to a higher value (like 300), we need to:
 *           1. rewrite the VRAM save / restore to avoid buffer object locks
 */
#define DEFAULT_VRAM_THRESHOLD 0 /* in MB */

struct xe_device;

int xe_pm_suspend(struct xe_device *xe);
int xe_pm_resume(struct xe_device *xe);

void xe_pm_init_early(struct xe_device *xe);
void xe_pm_init(struct xe_device *xe);
void xe_pm_runtime_fini(struct xe_device *xe);
int xe_pm_runtime_suspend(struct xe_device *xe);
int xe_pm_runtime_resume(struct xe_device *xe);
int xe_pm_runtime_get(struct xe_device *xe);
int xe_pm_runtime_put(struct xe_device *xe);
int xe_pm_runtime_get_if_active(struct xe_device *xe);
void xe_pm_assert_unbounded_bridge(struct xe_device *xe);
int xe_pm_set_vram_threshold(struct xe_device *xe, u32 threshold);
void xe_pm_d3cold_allowed_toggle(struct xe_device *xe);
struct task_struct *xe_pm_read_callback_task(struct xe_device *xe);

#endif
