/*
 *  linux/arch/arm/include/asm/perf_event.h
 *
 *  Copyright (C) 2009 picoChip Designs Ltd, Jamie Iles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef __ARM_PERF_EVENT_H__
#define __ARM_PERF_EVENT_H__

/* ARM performance counters start from 1 (in the cp15 accesses) so use the
 * same indexes here for consistency. */
#define PERF_EVENT_INDEX_OFFSET 1

/* ARM perf PMU IDs for use by internal perf clients. */
enum arm_perf_pmu_ids {
	ARM_PERF_PMU_ID_XSCALE1	= 0,
	ARM_PERF_PMU_ID_XSCALE2,
	ARM_PERF_PMU_ID_V6,
	ARM_PERF_PMU_ID_V6MP,
	ARM_PERF_PMU_ID_CA8,
	ARM_PERF_PMU_ID_CA9,
	ARM_PERF_PMU_ID_CA5,
	ARM_PERF_PMU_ID_CA15,
	ARM_NUM_PMU_IDS,
};

extern enum arm_perf_pmu_ids
armpmu_get_pmu_id(void);

extern int
armpmu_get_max_events(void);

#endif /* __ARM_PERF_EVENT_H__ */
