/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_PM_H_
#define _XE_PM_H_

#include <linux/cleanup.h>
#include <linux/pm_runtime.h>

#define DEFAULT_VRAM_THRESHOLD 300 /* in MB */

struct xe_device;

int xe_pm_suspend(struct xe_device *xe);
int xe_pm_resume(struct xe_device *xe);

int xe_pm_init_early(struct xe_device *xe);
int xe_pm_init(struct xe_device *xe);
void xe_pm_fini(struct xe_device *xe);
bool xe_pm_runtime_suspended(struct xe_device *xe);
int xe_pm_runtime_suspend(struct xe_device *xe);
int xe_pm_runtime_resume(struct xe_device *xe);
void xe_pm_runtime_get(struct xe_device *xe);
int xe_pm_runtime_get_ioctl(struct xe_device *xe);
void xe_pm_runtime_put(struct xe_device *xe);
bool xe_pm_runtime_get_if_active(struct xe_device *xe);
bool xe_pm_runtime_get_if_in_use(struct xe_device *xe);
void xe_pm_runtime_get_noresume(struct xe_device *xe);
bool xe_pm_runtime_resume_and_get(struct xe_device *xe);
void xe_pm_assert_unbounded_bridge(struct xe_device *xe);
int xe_pm_set_vram_threshold(struct xe_device *xe, u32 threshold);
void xe_pm_d3cold_allowed_toggle(struct xe_device *xe);
bool xe_rpm_reclaim_safe(const struct xe_device *xe);
struct task_struct *xe_pm_read_callback_task(struct xe_device *xe);
int xe_pm_block_on_suspend(struct xe_device *xe);
void xe_pm_might_block_on_suspend(void);
int xe_pm_module_init(void);

static inline void __xe_pm_runtime_noop(struct xe_device *xe) {}

DEFINE_GUARD(xe_pm_runtime, struct xe_device *,
	     xe_pm_runtime_get(_T), xe_pm_runtime_put(_T))
DEFINE_GUARD(xe_pm_runtime_noresume, struct xe_device *,
	     xe_pm_runtime_get_noresume(_T), xe_pm_runtime_put(_T))
DEFINE_GUARD_COND(xe_pm_runtime, _ioctl, xe_pm_runtime_get_ioctl(_T), _RET >= 0)

/*
 * Used when a function needs to release runtime PM in all possible cases
 * and error paths, but the wakeref was already acquired by a different
 * function (i.e., get() has already happened so only a put() is needed).
 */
DEFINE_GUARD(xe_pm_runtime_release_only, struct xe_device *,
	     __xe_pm_runtime_noop(_T), xe_pm_runtime_put(_T));

#endif
