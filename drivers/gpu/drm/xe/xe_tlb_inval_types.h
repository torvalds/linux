/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_TLB_INVAL_TYPES_H_
#define _XE_TLB_INVAL_TYPES_H_

#include <linux/workqueue.h>
#include <linux/dma-fence.h>

/** struct xe_tlb_inval - TLB invalidation client */
struct xe_tlb_inval {
	/** @private: Backend private pointer */
	void *private;
	/** @tlb_inval.seqno: TLB invalidation seqno, protected by CT lock */
#define TLB_INVALIDATION_SEQNO_MAX	0x100000
	int seqno;
	/** @tlb_invalidation.seqno_lock: protects @tlb_invalidation.seqno */
	struct mutex seqno_lock;
	/**
	 * @seqno_recv: last received TLB invalidation seqno, protected by
	 * CT lock
	 */
	int seqno_recv;
	/**
	 * @pending_fences: list of pending fences waiting TLB invaliations,
	 * protected CT lock
	 */
	struct list_head pending_fences;
	/**
	 * @pending_lock: protects @pending_fences and updating @seqno_recv.
	 */
	spinlock_t pending_lock;
	/**
	 * @fence_tdr: schedules a delayed call to xe_tlb_fence_timeout after
	 * the timeout interval is over.
	 */
	struct delayed_work fence_tdr;
	/** @job_wq: schedules TLB invalidation jobs */
	struct workqueue_struct *job_wq;
	/** @tlb_inval.lock: protects TLB invalidation fences */
	spinlock_t lock;
};

/**
 * struct xe_tlb_inval_fence - TLB invalidation fence
 *
 * Optionally passed to xe_tlb_inval* functions and will be signaled upon TLB
 * invalidation completion.
 */
struct xe_tlb_inval_fence {
	/** @base: dma fence base */
	struct dma_fence base;
	/** @tlb_inval: TLB invalidation client which fence belong to */
	struct xe_tlb_inval *tlb_inval;
	/** @link: link into list of pending tlb fences */
	struct list_head link;
	/** @seqno: seqno of TLB invalidation to signal fence one */
	int seqno;
	/** @inval_time: time of TLB invalidation */
	ktime_t inval_time;
};

#endif
