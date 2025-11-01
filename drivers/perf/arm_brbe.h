/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Branch Record Buffer Extension Helpers.
 *
 * Copyright (C) 2022-2025 ARM Limited
 *
 * Author: Anshuman Khandual <anshuman.khandual@arm.com>
 */

struct arm_pmu;
struct perf_branch_stack;
struct perf_event;

#ifdef CONFIG_ARM64_BRBE
void brbe_probe(struct arm_pmu *arm_pmu);
unsigned int brbe_num_branch_records(const struct arm_pmu *armpmu);
void brbe_invalidate(void);

void brbe_enable(const struct arm_pmu *arm_pmu);
void brbe_disable(void);

bool brbe_branch_attr_valid(struct perf_event *event);
void brbe_read_filtered_entries(struct perf_branch_stack *branch_stack,
				const struct perf_event *event);
#else
static inline void brbe_probe(struct arm_pmu *arm_pmu) { }
static inline unsigned int brbe_num_branch_records(const struct arm_pmu *armpmu)
{
	return 0;
}

static inline void brbe_invalidate(void) { }

static inline void brbe_enable(const struct arm_pmu *arm_pmu) { };
static inline void brbe_disable(void) { };

static inline bool brbe_branch_attr_valid(struct perf_event *event)
{
	WARN_ON_ONCE(!has_branch_stack(event));
	return false;
}

static void brbe_read_filtered_entries(struct perf_branch_stack *branch_stack,
				       const struct perf_event *event)
{
}
#endif
