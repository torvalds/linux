/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef _XE_GT_TLB_INVAL_JOB_H_
#define _XE_GT_TLB_INVAL_JOB_H_

#include <linux/types.h>

struct dma_fence;
struct drm_sched_job;
struct kref;
struct xe_exec_queue;
struct xe_gt;
struct xe_gt_tlb_inval_job;
struct xe_migrate;

struct xe_gt_tlb_inval_job *xe_gt_tlb_inval_job_create(struct xe_exec_queue *q,
						       struct xe_gt *gt,
						       u64 start, u64 end,
						       u32 asid);

int xe_gt_tlb_inval_job_alloc_dep(struct xe_gt_tlb_inval_job *job);

struct dma_fence *xe_gt_tlb_inval_job_push(struct xe_gt_tlb_inval_job *job,
					   struct xe_migrate *m,
					   struct dma_fence *fence);

void xe_gt_tlb_inval_job_get(struct xe_gt_tlb_inval_job *job);

void xe_gt_tlb_inval_job_put(struct xe_gt_tlb_inval_job *job);

#endif
