/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_GT_TLB_INVAL_TYPES_H_
#define _XE_GT_TLB_INVAL_TYPES_H_

#include <linux/workqueue.h>
#include <linux/dma-fence.h>

struct xe_gt;

/** struct xe_tlb_inval - TLB invalidation client */
struct xe_tlb_inval {
	/** @tlb_inval.seqno: TLB invalidation seqno, protected by CT lock */
#define TLB_INVALIDATION_SEQNO_MAX	0x100000
	int seqno;
	/** @tlb_invalidation.seqno_lock: protects @tlb_invalidation.seqno */
	struct mutex seqno_lock;
	/**
	 * @tlb_inval.seqno_recv: last received TLB invalidation seqno,
	 * protected by CT lock
	 */
	int seqno_recv;
	/**
	 * @tlb_inval.pending_fences: list of pending fences waiting TLB
	 * invaliations, protected by CT lock
	 */
	struct list_head pending_fences;
	/**
	 * @tlb_inval.pending_lock: protects @tlb_inval.pending_fences
	 * and updating @tlb_inval.seqno_recv.
	 */
	spinlock_t pending_lock;
	/**
	 * @tlb_inval.fence_tdr: schedules a delayed call to
	 * xe_gt_tlb_fence_timeout after the timeut interval is over.
	 */
	struct delayed_work fence_tdr;
	/** @wtlb_invalidation.wq: schedules GT TLB invalidation jobs */
	struct workqueue_struct *job_wq;
	/** @tlb_inval.lock: protects TLB invalidation fences */
	spinlock_t lock;
};

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
