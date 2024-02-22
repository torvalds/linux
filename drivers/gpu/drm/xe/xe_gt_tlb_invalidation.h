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
int xe_gt_tlb_invalidation_wait(struct xe_gt *gt, int seqno);
int xe_guc_tlb_invalidation_done_handler(struct xe_guc *guc, u32 *msg, u32 len);

#endif	/* _XE_GT_TLB_INVALIDATION_ */
