/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_GUC_SUBMIT_TYPES_H_
#define _XE_GUC_SUBMIT_TYPES_H_

#include "xe_hw_engine_types.h"

/* Work item for submitting workloads into work queue of GuC. */
#define WQ_STATUS_ACTIVE		1
#define WQ_STATUS_SUSPENDED		2
#define WQ_STATUS_CMD_ERROR		3
#define WQ_STATUS_ENGINE_ID_NOT_USED	4
#define WQ_STATUS_SUSPENDED_FROM_RESET	5
#define WQ_TYPE_NOOP			0x4
#define WQ_TYPE_MULTI_LRC		0x5
#define WQ_TYPE_MASK			GENMASK(7, 0)
#define WQ_LEN_MASK			GENMASK(26, 16)

#define WQ_GUC_ID_MASK			GENMASK(15, 0)
#define WQ_RING_TAIL_MASK		GENMASK(28, 18)

#define PARALLEL_SCRATCH_SIZE	2048
#define WQ_SIZE			(PARALLEL_SCRATCH_SIZE / 2)
#define WQ_OFFSET		(PARALLEL_SCRATCH_SIZE - WQ_SIZE)
#define CACHELINE_BYTES		64

struct guc_sched_wq_desc {
	u32 head;
	u32 tail;
	u32 error_offset;
	u32 wq_status;
	u32 reserved[28];
} __packed;

struct sync_semaphore {
	u32 semaphore;
	u8 unused[CACHELINE_BYTES - sizeof(u32)];
};

/**
 * struct guc_submit_parallel_scratch - A scratch shared mapped buffer.
 */
struct guc_submit_parallel_scratch {
	/** @wq_desc: Guc scheduler workqueue descriptor */
	struct guc_sched_wq_desc wq_desc;

	/** @go: Go Semaphore */
	struct sync_semaphore go;
	/** @join: Joined semaphore for the relevant hw engine instances */
	struct sync_semaphore join[XE_HW_ENGINE_MAX_INSTANCE];

	/** @unused: Unused/Reserved memory space */
	u8 unused[WQ_OFFSET - sizeof(struct guc_sched_wq_desc) -
		  sizeof(struct sync_semaphore) *
		  (XE_HW_ENGINE_MAX_INSTANCE + 1)];

	/** @wq: Workqueue info */
	u32 wq[WQ_SIZE / sizeof(u32)];
};

struct lrc_snapshot {
	u32 context_desc;
	u32 head;
	struct {
		u32 internal;
		u32 memory;
	} tail;
	u32 start_seqno;
	u32 seqno;
};

struct pending_list_snapshot {
	u32 seqno;
	bool fence;
	bool finished;
};

/**
 * struct xe_guc_submit_exec_queue_snapshot - Snapshot for devcoredump
 */
struct xe_guc_submit_exec_queue_snapshot {
	/** @name: name of this exec queue */
	char name[MAX_FENCE_NAME_LEN];
	/** @class: class of this exec queue */
	enum xe_engine_class class;
	/**
	 * @logical_mask: logical mask of where job submitted to exec queue can run
	 */
	u32 logical_mask;
	/** @width: width (number BB submitted per exec) of this exec queue */
	u16 width;
	/** @refcount: ref count of this exec queue */
	u32 refcount;
	/**
	 * @sched_timeout: the time after which a job is removed from the
	 * scheduler.
	 */
	long sched_timeout;

	/** @sched_props: scheduling properties */
	struct {
		/** @sched_props.timeslice_us: timeslice period in micro-seconds */
		u32 timeslice_us;
		/** @sched_props.preempt_timeout_us: preemption timeout in micro-seconds */
		u32 preempt_timeout_us;
	} sched_props;

	/** @lrc: LRC Snapshot */
	struct lrc_snapshot *lrc;

	/** @schedule_state: Schedule State at the moment of Crash */
	u32 schedule_state;
	/** @exec_queue_flags: Flags of the faulty exec_queue */
	unsigned long exec_queue_flags;

	/** @guc: GuC Engine Snapshot */
	struct {
		/** @guc.wqi_head: work queue item head */
		u32 wqi_head;
		/** @guc.wqi_tail: work queue item tail */
		u32 wqi_tail;
		/** @guc.id: GuC id for this exec_queue */
		u16 id;
	} guc;

	/**
	 * @parallel_execution: Indication if the failure was during parallel
	 * execution
	 */
	bool parallel_execution;
	/** @parallel: snapshot of the useful parallel scratch */
	struct {
		/** @parallel.wq_desc: Workqueue description */
		struct {
			/** @parallel.wq_desc.head: Workqueue Head */
			u32 head;
			/** @parallel.wq_desc.tail: Workqueue Tail */
			u32 tail;
			/** @parallel.wq_desc.status: Workqueue Status */
			u32 status;
		} wq_desc;
		/** @wq: Workqueue Items */
		u32 wq[WQ_SIZE / sizeof(u32)];
	} parallel;

	/** @pending_list_size: Size of the pending list snapshot array */
	int pending_list_size;
	/** @pending_list: snapshot of the pending list info */
	struct pending_list_snapshot *pending_list;
};

#endif
