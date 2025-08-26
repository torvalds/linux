/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_TLB_INVAL_TYPES_H_
#define _XE_TLB_INVAL_TYPES_H_

#include <linux/workqueue.h>
#include <linux/dma-fence.h>

struct xe_tlb_inval;

/** struct xe_tlb_inval_ops - TLB invalidation ops (backend) */
struct xe_tlb_inval_ops {
	/**
	 * @all: Invalidate all TLBs
	 * @tlb_inval: TLB invalidation client
	 * @seqno: Seqno of TLB invalidation
	 *
	 * Return 0 on success, -ECANCELED if backend is mid-reset, error on
	 * failure
	 */
	int (*all)(struct xe_tlb_inval *tlb_inval, u32 seqno);

	/**
	 * @ggtt: Invalidate global translation TLBs
	 * @tlb_inval: TLB invalidation client
	 * @seqno: Seqno of TLB invalidation
	 *
	 * Return 0 on success, -ECANCELED if backend is mid-reset, error on
	 * failure
	 */
	int (*ggtt)(struct xe_tlb_inval *tlb_inval, u32 seqno);

	/**
	 * @ppgtt: Invalidate per-process translation TLBs
	 * @tlb_inval: TLB invalidation client
	 * @seqno: Seqno of TLB invalidation
	 * @start: Start address
	 * @end: End address
	 * @asid: Address space ID
	 *
	 * Return 0 on success, -ECANCELED if backend is mid-reset, error on
	 * failure
	 */
	int (*ppgtt)(struct xe_tlb_inval *tlb_inval, u32 seqno, u64 start,
		     u64 end, u32 asid);

	/**
	 * @initialized: Backend is initialized
	 * @tlb_inval: TLB invalidation client
	 *
	 * Return: True if back is initialized, False otherwise
	 */
	bool (*initialized)(struct xe_tlb_inval *tlb_inval);

	/**
	 * @flush: Flush pending TLB invalidations
	 * @tlb_inval: TLB invalidation client
	 */
	void (*flush)(struct xe_tlb_inval *tlb_inval);

	/**
	 * @timeout_delay: Timeout delay for TLB invalidation
	 * @tlb_inval: TLB invalidation client
	 *
	 * Return: Timeout delay for TLB invalidation in jiffies
	 */
	long (*timeout_delay)(struct xe_tlb_inval *tlb_inval);
};

/** struct xe_tlb_inval - TLB invalidation client (frontend) */
struct xe_tlb_inval {
	/** @private: Backend private pointer */
	void *private;
	/** @xe: Pointer to Xe device */
	struct xe_device *xe;
	/** @ops: TLB invalidation ops */
	const struct xe_tlb_inval_ops *ops;
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
