// SPDX-License-Identifier: MIT
/*
 * Copyright © 2024 Intel Corporation
 */

#include <linux/atomic.h>

#include <drm/drm_print.h>

#include "xe_gt.h"
#include "xe_gt_stats.h"

/**
 * xe_gt_stats_incr - Increments the specified stats counter
 * @gt: GT structure
 * @id: xe_gt_stats_id type id that needs to be incremented
 * @incr: value to be incremented with
 *
 * Increments the specified stats counter.
 */
void xe_gt_stats_incr(struct xe_gt *gt, const enum xe_gt_stats_id id, int incr)
{
	if (id >= __XE_GT_STATS_NUM_IDS)
		return;

	atomic64_add(incr, &gt->stats.counters[id]);
}

#define DEF_STAT_STR(ID, name) [XE_GT_STATS_ID_##ID] = name

static const char *const stat_description[__XE_GT_STATS_NUM_IDS] = {
	DEF_STAT_STR(SVM_PAGEFAULT_COUNT, "svm_pagefault_count"),
	DEF_STAT_STR(TLB_INVAL, "tlb_inval_count"),
	DEF_STAT_STR(SVM_TLB_INVAL_COUNT, "svm_tlb_inval_count"),
	DEF_STAT_STR(SVM_TLB_INVAL_US, "svm_tlb_inval_us"),
	DEF_STAT_STR(VMA_PAGEFAULT_COUNT, "vma_pagefault_count"),
	DEF_STAT_STR(VMA_PAGEFAULT_KB, "vma_pagefault_kb"),
	DEF_STAT_STR(SVM_4K_PAGEFAULT_COUNT, "svm_4K_pagefault_count"),
	DEF_STAT_STR(SVM_64K_PAGEFAULT_COUNT, "svm_64K_pagefault_count"),
	DEF_STAT_STR(SVM_2M_PAGEFAULT_COUNT, "svm_2M_pagefault_count"),
	DEF_STAT_STR(SVM_4K_VALID_PAGEFAULT_COUNT, "svm_4K_valid_pagefault_count"),
	DEF_STAT_STR(SVM_64K_VALID_PAGEFAULT_COUNT, "svm_64K_valid_pagefault_count"),
	DEF_STAT_STR(SVM_2M_VALID_PAGEFAULT_COUNT, "svm_2M_valid_pagefault_count"),
	DEF_STAT_STR(SVM_4K_PAGEFAULT_US, "svm_4K_pagefault_us"),
	DEF_STAT_STR(SVM_64K_PAGEFAULT_US, "svm_64K_pagefault_us"),
	DEF_STAT_STR(SVM_2M_PAGEFAULT_US, "svm_2M_pagefault_us"),
	DEF_STAT_STR(SVM_4K_MIGRATE_COUNT, "svm_4K_migrate_count"),
	DEF_STAT_STR(SVM_64K_MIGRATE_COUNT, "svm_64K_migrate_count"),
	DEF_STAT_STR(SVM_2M_MIGRATE_COUNT, "svm_2M_migrate_count"),
	DEF_STAT_STR(SVM_4K_MIGRATE_US, "svm_4K_migrate_us"),
	DEF_STAT_STR(SVM_64K_MIGRATE_US, "svm_64K_migrate_us"),
	DEF_STAT_STR(SVM_2M_MIGRATE_US, "svm_2M_migrate_us"),
	DEF_STAT_STR(SVM_DEVICE_COPY_US, "svm_device_copy_us"),
	DEF_STAT_STR(SVM_4K_DEVICE_COPY_US, "svm_4K_device_copy_us"),
	DEF_STAT_STR(SVM_64K_DEVICE_COPY_US, "svm_64K_device_copy_us"),
	DEF_STAT_STR(SVM_2M_DEVICE_COPY_US, "svm_2M_device_copy_us"),
	DEF_STAT_STR(SVM_CPU_COPY_US, "svm_cpu_copy_us"),
	DEF_STAT_STR(SVM_4K_CPU_COPY_US, "svm_4K_cpu_copy_us"),
	DEF_STAT_STR(SVM_64K_CPU_COPY_US, "svm_64K_cpu_copy_us"),
	DEF_STAT_STR(SVM_2M_CPU_COPY_US, "svm_2M_cpu_copy_us"),
	DEF_STAT_STR(SVM_DEVICE_COPY_KB, "svm_device_copy_kb"),
	DEF_STAT_STR(SVM_CPU_COPY_KB, "svm_cpu_copy_kb"),
	DEF_STAT_STR(SVM_4K_GET_PAGES_US, "svm_4K_get_pages_us"),
	DEF_STAT_STR(SVM_64K_GET_PAGES_US, "svm_64K_get_pages_us"),
	DEF_STAT_STR(SVM_2M_GET_PAGES_US, "svm_2M_get_pages_us"),
	DEF_STAT_STR(SVM_4K_BIND_US, "svm_4K_bind_us"),
	DEF_STAT_STR(SVM_64K_BIND_US, "svm_64K_bind_us"),
	DEF_STAT_STR(SVM_2M_BIND_US, "svm_2M_bind_us"),
};

/**
 * xe_gt_stats_print_info - Print the GT stats
 * @gt: GT structure
 * @p: drm_printer where it will be printed out.
 *
 * This prints out all the available GT stats.
 */
int xe_gt_stats_print_info(struct xe_gt *gt, struct drm_printer *p)
{
	enum xe_gt_stats_id id;

	for (id = 0; id < __XE_GT_STATS_NUM_IDS; ++id)
		drm_printf(p, "%s: %lld\n", stat_description[id],
			   atomic64_read(&gt->stats.counters[id]));

	return 0;
}

/**
 * xe_gt_stats_clear - Clear the GT stats
 * @gt: GT structure
 *
 * This clear (zeros) all the available GT stats.
 */
void xe_gt_stats_clear(struct xe_gt *gt)
{
	int id;

	for (id = 0; id < ARRAY_SIZE(gt->stats.counters); ++id)
		atomic64_set(&gt->stats.counters[id], 0);
}
