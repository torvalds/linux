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

void xe_tlb_inval_reset(struct xe_tlb_inval *tlb_inval);
int xe_tlb_inval_all(struct xe_tlb_inval *tlb_inval,
		     struct xe_tlb_inval_fence *fence);
int xe_tlb_inval_ggtt(struct xe_tlb_inval *tlb_inval);
void xe_tlb_inval_vm(struct xe_tlb_inval *tlb_inval, struct xe_vm *vm);
int xe_tlb_inval_range(struct xe_tlb_inval *tlb_inval,
		       struct xe_tlb_inval_fence *fence,
		       u64 start, u64 end, u32 asid);

void xe_tlb_inval_fence_init(struct xe_tlb_inval *tlb_inval,
			     struct xe_tlb_inval_fence *fence,
			     bool stack);

/**
 * xe_tlb_inval_fence_wait() - TLB invalidiation fence wait
 * @fence: TLB invalidation fence to wait on
 *
 * Wait on a TLB invalidiation fence until it signals, non interruptable
 */
static inline void
xe_tlb_inval_fence_wait(struct xe_tlb_inval_fence *fence)
{
	dma_fence_wait(&fence->base, false);
}

void xe_tlb_inval_done_handler(struct xe_tlb_inval *tlb_inval, int seqno);

#endif	/* _XE_TLB_INVAL_ */
