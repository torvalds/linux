/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef _XE_TLB_INVAL_H_
#define _XE_TLB_INVAL_H_

#include <linux/types.h>

#include "xe_tlb_inval_types.h"

struct xe_gt;
struct xe_guc;
struct xe_vm;

int xe_gt_tlb_inval_init_early(struct xe_gt *gt);
void xe_gt_tlb_inval_fini(struct xe_gt *gt);

void xe_tlb_inval_reset(struct xe_tlb_inval *tlb_inval);
int xe_tlb_inval_ggtt(struct xe_tlb_inval *tlb_inval);
void xe_tlb_inval_vm(struct xe_tlb_inval *tlb_inval, struct xe_vm *vm);
int xe_tlb_inval_all(struct xe_tlb_inval *tlb_inval,
		     struct xe_tlb_inval_fence *fence);
int xe_tlb_inval_range(struct xe_tlb_inval *tlb_inval,
		       struct xe_tlb_inval_fence *fence,
		       u64 start, u64 end, u32 asid);
int xe_guc_tlb_inval_done_handler(struct xe_guc *guc, u32 *msg, u32 len);

void xe_tlb_inval_fence_init(struct xe_tlb_inval *tlb_inval,
			     struct xe_tlb_inval_fence *fence,
			     bool stack);
void xe_tlb_inval_fence_signal(struct xe_tlb_inval_fence *fence);

static inline void
xe_tlb_inval_fence_wait(struct xe_tlb_inval_fence *fence)
{
	dma_fence_wait(&fence->base, false);
}

#endif	/* _XE_TLB_INVAL_ */
