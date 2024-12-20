/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_GT_TLB_INVALIDATION_H_
#define _XE_GT_TLB_INVALIDATION_H_

#include <linux/types.h>

#include "xe_gt_tlb_invalidation_types.h"

struct xe_gt;
struct xe_guc;
struct xe_vma;

int xe_gt_tlb_invalidation_init(struct xe_gt *gt);
void xe_gt_tlb_invalidation_reset(struct xe_gt *gt);
int xe_gt_tlb_invalidation_ggtt(struct xe_gt *gt);
int xe_gt_tlb_invalidation_vma(struct xe_gt *gt,
			       struct xe_gt_tlb_invalidation_fence *fence,
			       struct xe_vma *vma);
int xe_gt_tlb_invalidation_range(struct xe_gt *gt,
				 struct xe_gt_tlb_invalidation_fence *fence,
				 u64 start, u64 end, u32 asid);
int xe_guc_tlb_invalidation_done_handler(struct xe_guc *guc, u32 *msg, u32 len);

void xe_gt_tlb_invalidation_fence_init(struct xe_gt *gt,
				       struct xe_gt_tlb_invalidation_fence *fence,
				       bool stack);
void xe_gt_tlb_invalidation_fence_signal(struct xe_gt_tlb_invalidation_fence *fence);

static inline void
xe_gt_tlb_invalidation_fence_wait(struct xe_gt_tlb_invalidation_fence *fence)
{
	dma_fence_wait(&fence->base, false);
}

#endif	/* _XE_GT_TLB_INVALIDATION_ */
