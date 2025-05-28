/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef _XE_PMU_H_
#define _XE_PMU_H_

#include "xe_pmu_types.h"

#if IS_ENABLED(CONFIG_PERF_EVENTS)
int xe_pmu_register(struct xe_pmu *pmu);
#else
static inline int xe_pmu_register(struct xe_pmu *pmu) { return 0; }
#endif

#endif

