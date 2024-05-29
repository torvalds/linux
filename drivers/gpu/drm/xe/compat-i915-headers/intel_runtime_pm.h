/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef __INTEL_RUNTIME_PM_H__
#define __INTEL_RUNTIME_PM_H__

#include "intel_wakeref.h"
#include "xe_device_types.h"
#include "xe_pm.h"

#define intel_runtime_pm xe_runtime_pm

static inline void disable_rpm_wakeref_asserts(void *rpm)
{
}

static inline void enable_rpm_wakeref_asserts(void *rpm)
{
}

static inline intel_wakeref_t intel_runtime_pm_get(struct xe_runtime_pm *pm)
{
	struct xe_device *xe = container_of(pm, struct xe_device, runtime_pm);

	return xe_pm_runtime_resume_and_get(xe);
}

static inline intel_wakeref_t intel_runtime_pm_get_if_in_use(struct xe_runtime_pm *pm)
{
	struct xe_device *xe = container_of(pm, struct xe_device, runtime_pm);

	return xe_pm_runtime_get_if_in_use(xe);
}

static inline intel_wakeref_t intel_runtime_pm_get_noresume(struct xe_runtime_pm *pm)
{
	struct xe_device *xe = container_of(pm, struct xe_device, runtime_pm);

	xe_pm_runtime_get_noresume(xe);
	return true;
}

static inline void intel_runtime_pm_put_unchecked(struct xe_runtime_pm *pm)
{
	struct xe_device *xe = container_of(pm, struct xe_device, runtime_pm);

	xe_pm_runtime_put(xe);
}

static inline void intel_runtime_pm_put(struct xe_runtime_pm *pm, intel_wakeref_t wakeref)
{
	if (wakeref)
		intel_runtime_pm_put_unchecked(pm);
}

#define intel_runtime_pm_get_raw intel_runtime_pm_get
#define intel_runtime_pm_put_raw intel_runtime_pm_put
#define assert_rpm_wakelock_held(x) do { } while (0)
#define assert_rpm_raw_wakeref_held(x) do { } while (0)

#define with_intel_runtime_pm(rpm, wf) \
	for ((wf) = intel_runtime_pm_get(rpm); (wf); \
	     intel_runtime_pm_put((rpm), (wf)), (wf) = 0)

#endif
