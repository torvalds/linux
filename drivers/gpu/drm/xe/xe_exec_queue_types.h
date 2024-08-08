/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_EXEC_QUEUE_TYPES_H_
#define _XE_EXEC_QUEUE_TYPES_H_

#include <linux/kref.h>

#include <drm/gpu_scheduler.h>

#include "xe_gpu_scheduler_types.h"
#include "xe_hw_engine_types.h"
#include "xe_hw_fence_types.h"
#include "xe_lrc_types.h"

struct xe_execlist_exec_queue;
struct xe_gt;
struct xe_guc_exec_queue;
struct xe_hw_engine;
struct xe_vm;

enum xe_exec_queue_priority {
	XE_EXEC_QUEUE_PRIORITY_UNSET = -2, /* For execlist usage only */
	XE_EXEC_QUEUE_PRIORITY_LOW = 0,
	XE_EXEC_QUEUE_PRIORITY_NORMAL,
	XE_EXEC_QUEUE_PRIORITY_HIGH,
	XE_EXEC_QUEUE_PRIORITY_KERNEL,

	XE_EXEC_QUEUE_PRIORITY_COUNT
};

/**
 * struct xe_exec_queue - Execution queue
 *
 * Contains all state necessary for submissions. Can either be a user object or
 * a kernel object.
 */
struct xe_exec_queue {
	/** @xef: Back pointer to xe file if this is user created exec queue */
	struct xe_file *xef;

	/** @gt: graphics tile this exec queue can submit to */
	struct xe_gt *gt;
	/**
	 * @hwe: A hardware of the same class. May (physical engine) or may not
	 * (virtual engine) be where jobs actual engine up running. Should never
	 * really be used for submissions.
	 */
	struct xe_hw_engine *hwe;
	/** @refcount: ref count of this exec queue */
	struct kref refcount;
	/** @vm: VM (address space) for this exec queue */
	struct xe_vm *vm;
	/** @class: class of this exec queue */
	enum xe_engine_class class;
	/**
	 * @logical_mask: logical mask of where job submitted to exec queue can run
	 */
	u32 logical_mask;
	/** @name: name of this exec queue */
	char name[MAX_FENCE_NAME_LEN];
	/** @width: width (number BB submitted per exec) of this exec queue */
	u16 width;
	/** @fence_irq: fence IRQ used to signal job completion */
	struct xe_hw_fence_irq *fence_irq;

	/**
	 * @last_fence: last fence on exec queue, protected by vm->lock in write
	 * mode if bind exec queue, protected by dma resv lock if non-bind exec
	 * queue
	 */
	struct dma_fence *last_fence;

/* queue used for kernel submission only */
#define EXEC_QUEUE_FLAG_KERNEL			BIT(0)
/* kernel engine only destroyed at driver unload */
#define EXEC_QUEUE_FLAG_PERMANENT		BIT(1)
/* for VM jobs. Caller needs to hold rpm ref when creating queue with this flag */
#define EXEC_QUEUE_FLAG_VM			BIT(2)
/* child of VM queue for multi-tile VM jobs */
#define EXEC_QUEUE_FLAG_BIND_ENGINE_CHILD	BIT(3)
/* kernel exec_queue only, set priority to highest level */
#define EXEC_QUEUE_FLAG_HIGH_PRIORITY		BIT(4)

	/**
	 * @flags: flags for this exec queue, should statically setup aside from ban
	 * bit
	 */
	unsigned long flags;

	union {
		/** @multi_gt_list: list head for VM bind engines if multi-GT */
		struct list_head multi_gt_list;
		/** @multi_gt_link: link for VM bind engines if multi-GT */
		struct list_head multi_gt_link;
	};

	union {
		/** @execlist: execlist backend specific state for exec queue */
		struct xe_execlist_exec_queue *execlist;
		/** @guc: GuC backend specific state for exec queue */
		struct xe_guc_exec_queue *guc;
	};

	/** @sched_props: scheduling properties */
	struct {
		/** @sched_props.timeslice_us: timeslice period in micro-seconds */
		u32 timeslice_us;
		/** @sched_props.preempt_timeout_us: preemption timeout in micro-seconds */
		u32 preempt_timeout_us;
		/** @sched_props.job_timeout_ms: job timeout in milliseconds */
		u32 job_timeout_ms;
		/** @sched_props.priority: priority of this exec queue */
		enum xe_exec_queue_priority priority;
	} sched_props;

	/** @lr: long-running exec queue state */
	struct {
		/** @lr.pfence: preemption fence */
		struct dma_fence *pfence;
		/** @lr.context: preemption fence context */
		u64 context;
		/** @lr.seqno: preemption fence seqno */
		u32 seqno;
		/** @lr.link: link into VM's list of exec queues */
		struct list_head link;
	} lr;

	/** @ops: submission backend exec queue operations */
	const struct xe_exec_queue_ops *ops;

	/** @ring_ops: ring operations for this exec queue */
	const struct xe_ring_ops *ring_ops;
	/** @entity: DRM sched entity for this exec queue (1 to 1 relationship) */
	struct drm_sched_entity *entity;
	/**
	 * @tlb_flush_seqno: The seqno of the last rebind tlb flush performed
	 * Protected by @vm's resv. Unused if @vm == NULL.
	 */
	u64 tlb_flush_seqno;
	/** @lrc: logical ring context for this exec queue */
	struct xe_lrc *lrc[];
};

/**
 * struct xe_exec_queue_ops - Submission backend exec queue operations
 */
struct xe_exec_queue_ops {
	/** @init: Initialize exec queue for submission backend */
	int (*init)(struct xe_exec_queue *q);
	/** @kill: Kill inflight submissions for backend */
	void (*kill)(struct xe_exec_queue *q);
	/** @fini: Fini exec queue for submission backend */
	void (*fini)(struct xe_exec_queue *q);
	/** @set_priority: Set priority for exec queue */
	int (*set_priority)(struct xe_exec_queue *q,
			    enum xe_exec_queue_priority priority);
	/** @set_timeslice: Set timeslice for exec queue */
	int (*set_timeslice)(struct xe_exec_queue *q, u32 timeslice_us);
	/** @set_preempt_timeout: Set preemption timeout for exec queue */
	int (*set_preempt_timeout)(struct xe_exec_queue *q, u32 preempt_timeout_us);
	/**
	 * @suspend: Suspend exec queue from executing, allowed to be called
	 * multiple times in a row before resume with the caveat that
	 * suspend_wait returns before calling suspend again.
	 */
	int (*suspend)(struct xe_exec_queue *q);
	/**
	 * @suspend_wait: Wait for an exec queue to suspend executing, should be
	 * call after suspend.
	 */
	void (*suspend_wait)(struct xe_exec_queue *q);
	/**
	 * @resume: Resume exec queue execution, exec queue must be in a suspended
	 * state and dma fence returned from most recent suspend call must be
	 * signalled when this function is called.
	 */
	void (*resume)(struct xe_exec_queue *q);
	/** @reset_status: check exec queue reset status */
	bool (*reset_status)(struct xe_exec_queue *q);
};

#endif
