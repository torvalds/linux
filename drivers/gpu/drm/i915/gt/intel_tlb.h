/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef INTEL_TLB_H
#define INTEL_TLB_H

#include <linux/seqlock.h>
#include <linux/types.h>

#include "intel_gt_types.h"

void intel_gt_invalidate_tlb_full(struct intel_gt *gt, u32 seqno);

void intel_gt_init_tlb(struct intel_gt *gt);
void intel_gt_fini_tlb(struct intel_gt *gt);

static inline u32 intel_gt_tlb_seqno(const struct intel_gt *gt)
{
	return seqprop_sequence(&gt->tlb.seqno);
}

static inline u32 intel_gt_next_invalidate_tlb_full(const struct intel_gt *gt)
{
	return intel_gt_tlb_seqno(gt) | 1;
}

#endif /* INTEL_TLB_H */
