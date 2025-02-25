/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2024 Intel Corporation
 */

#ifndef _XE_GT_STATS_TYPES_H_
#define _XE_GT_STATS_TYPES_H_

enum xe_gt_stats_id {
	XE_GT_STATS_ID_TLB_INVAL,
	XE_GT_STATS_ID_VMA_PAGEFAULT_COUNT,
	XE_GT_STATS_ID_VMA_PAGEFAULT_KB,
	/* must be the last entry */
	__XE_GT_STATS_NUM_IDS,
};

#endif
