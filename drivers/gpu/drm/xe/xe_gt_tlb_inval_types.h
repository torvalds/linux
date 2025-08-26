/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_GT_TLB_INVAL_TYPES_H_
#define _XE_GT_TLB_INVAL_TYPES_H_

#include <linux/dma-fence.h>

struct xe_gt;

/**
 * struct xe_gt_tlb_inval_fence - XE GT TLB invalidation fence
 *
 * Optionally passed to xe_gt_tlb_inval and will be signaled upon TLB
 * invalidation completion.
 */
struct xe_gt_tlb_inval_fence {
	/** @base: dma fence base */
	struct dma_fence base;
	/** @gt: GT which fence belong to */
	struct xe_gt *gt;
	/** @link: link into list of pending tlb fences */
	struct list_head link;
	/** @seqno: seqno of TLB invalidation to signal fence one */
	int seqno;
	/** @inval_time: time of TLB invalidation */
	ktime_t inval_time;
};

#endif
