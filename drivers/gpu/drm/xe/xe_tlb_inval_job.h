/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef _XE_TLB_INVAL_JOB_H_
#define _XE_TLB_INVAL_JOB_H_

#include <linux/types.h>

struct dma_fence;
struct xe_dep_scheduler;
struct xe_exec_queue;
struct xe_migrate;
struct xe_page_reclaim_list;
struct xe_tlb_inval;
struct xe_tlb_inval_job;
struct xe_vm;

struct xe_tlb_inval_job *
xe_tlb_inval_job_create(struct xe_exec_queue *q, struct xe_tlb_inval *tlb_inval,
			struct xe_dep_scheduler *dep_scheduler,
			struct xe_vm *vm, u64 start, u64 end, int type);

void xe_tlb_inval_job_add_page_reclaim(struct xe_tlb_inval_job *job,
				       struct xe_page_reclaim_list *prl);

int xe_tlb_inval_job_alloc_dep(struct xe_tlb_inval_job *job);

struct dma_fence *xe_tlb_inval_job_push(struct xe_tlb_inval_job *job,
					struct xe_migrate *m,
					struct dma_fence *fence);

void xe_tlb_inval_job_get(struct xe_tlb_inval_job *job);

void xe_tlb_inval_job_put(struct xe_tlb_inval_job *job);

#endif
