/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_SCHED_JOB_TYPES_H_
#define _XE_SCHED_JOB_TYPES_H_

#include <linux/kref.h>

#include <drm/gpu_scheduler.h>

struct dma_fence;
struct dma_fence_chain;

struct xe_exec_queue;
struct xe_cpu_bind_pt_update_ops;
struct xe_pt_job_ops;
struct xe_tile;
struct xe_vm;

/**
 * struct xe_pt_update_args - PT update arguments
 */
struct xe_pt_update_args {
	/** @vm: VM which is being bound */
	struct xe_vm *vm;
	/** @ops: CPU bind PT update ops */
	const struct xe_cpu_bind_pt_update_ops *ops;
#define XE_PT_UPDATE_JOB_OPS_COUNT	2
	/** @pt_job_ops: PT job ops state */
	struct xe_pt_job_ops *pt_job_ops[XE_PT_UPDATE_JOB_OPS_COUNT];
};

/**
 * struct xe_job_ptrs - Per hw engine instance data
 */
struct xe_job_ptrs {
	/** @lrc_fence: Pre-allocated uninitialized lrc fence.*/
	struct dma_fence *lrc_fence;
	/** @chain_fence: Pre-allocated uninitialized fence chain node. */
	struct dma_fence_chain *chain_fence;
	/** @batch_addr: Batch buffer address. */
	u64 batch_addr;
	/**
	 * @head: The tail pointer of the LRC (so head pointer of job) when the
	 * job was submitted
	 */
	u32 head;
};

/**
 * enum xe_ulls_state - ULLS state of a migration job
 *
 * Describes where a job sits in a ULLS (Ultra Low Latency Submission)
 * sequence. See the ULLS documentation in xe_migrate.c.
 */
enum xe_ulls_state {
	/** @ULLS_NONE: Not a ULLS job */
	ULLS_NONE = 0,
	/** @ULLS_ENTER: Job which enters ULLS mode */
	ULLS_ENTER,
	/** @ULLS_ACTIVE: Job submitted while in ULLS mode */
	ULLS_ACTIVE,
	/** @ULLS_EXIT: Job which exits ULLS mode */
	ULLS_EXIT,
};

/**
 * struct xe_sched_job - Xe schedule job (batch buffer tracking)
 */
struct xe_sched_job {
	/** @drm: base DRM scheduler job */
	struct drm_sched_job drm;
	/** @q: Exec queue */
	struct xe_exec_queue *q;
	/** @refcount: ref count of this job */
	struct kref refcount;
	/**
	 * @fence: dma fence to indicate completion. 1 way relationship - job
	 * can safely reference fence, fence cannot safely reference job.
	 */
	struct dma_fence *fence;
	/** @user_fence: write back value when BB is complete */
	struct {
		/** @user_fence.used: user fence is used */
		bool used;
		/** @user_fence.addr: address to write to */
		u64 addr;
		/** @user_fence.value: write back value */
		u64 value;
	} user_fence;
	/** @lrc_seqno: LRC seqno */
	u32 lrc_seqno;
	/** @migrate_flush_flags: Additional flush flags for migration jobs */
	u32 migrate_flush_flags;
	/** @sample_timestamp: Sampling of job timestamp in TDR */
	u64 sample_timestamp;
	/** @ulls: ULLS state of this job */
	enum xe_ulls_state ulls;
	/** @ring_ops_flush_tlb: The ring ops need to flush TLB before payload. */
	bool ring_ops_flush_tlb;
	/** @ring_ops_force_reset: The ring ops need to trigger a reset before payload. */
	bool ring_ops_force_reset;
	/** @ggtt: mapped in ggtt. */
	bool ggtt;
	/** @restore_replay: job being replayed for restore */
	bool restore_replay;
	/** @last_replay: last job being replayed */
	bool last_replay;
	/** @is_pt_job: is a PT job */
	bool is_pt_job;
	union {
		/** @ptrs: per instance pointers. */
		DECLARE_FLEX_ARRAY(struct xe_job_ptrs, ptrs);
		/** @pt_update: PT update arguments */
		DECLARE_FLEX_ARRAY(struct xe_pt_update_args, pt_update);
	};
};

struct xe_sched_job_snapshot {
	u16 batch_addr_len;
	u64 batch_addr[] __counted_by(batch_addr_len);
};

#endif
