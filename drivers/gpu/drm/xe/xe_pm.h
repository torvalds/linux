/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_PM_H_
#define _XE_PM_H_

#include <linux/pm_runtime.h>

struct xe_device;

int xe_pm_suspend(struct xe_device *xe);
int xe_pm_resume(struct xe_device *xe);

void xe_pm_runtime_init(struct xe_device *xe);
int xe_pm_runtime_suspend(struct xe_device *xe);
int xe_pm_runtime_resume(struct xe_device *xe);
int xe_pm_runtime_get(struct xe_device *xe);
int xe_pm_runtime_put(struct xe_device *xe);
bool xe_pm_runtime_resume_if_suspended(struct xe_device *xe);
int xe_pm_runtime_get_if_active(struct xe_device *xe);

#endif
