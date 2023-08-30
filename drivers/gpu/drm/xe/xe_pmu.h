/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_PMU_H_
#define _XE_PMU_H_

#include "xe_gt_types.h"
#include "xe_pmu_types.h"

#if IS_ENABLED(CONFIG_PERF_EVENTS)
int xe_pmu_init(void);
void xe_pmu_exit(void);
void xe_pmu_register(struct xe_pmu *pmu);
void xe_pmu_suspend(struct xe_gt *gt);
#else
static inline int xe_pmu_init(void) { return 0; }
static inline void xe_pmu_exit(void) {}
static inline void xe_pmu_register(struct xe_pmu *pmu) {}
static inline void xe_pmu_suspend(struct xe_gt *gt) {}
#endif

#endif

