/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_ENGINE_TYPES_H_
#define _XE_ENGINE_TYPES_H_

#include <linux/kref.h>

#include <drm/gpu_scheduler.h>

#include "xe_gpu_scheduler_types.h"
#include "xe_hw_engine_types.h"
#include "xe_hw_fence_types.h"
#include "xe_lrc_types.h"

struct xe_execlist_engine;
struct xe_gt;
struct xe_guc_engine;
struct xe_hw_engine;
struct xe_vm;

enum xe_engine_priority {
	XE_ENGINE_PRIORITY_UNSET = -2, /* For execlist usage only */
	XE_ENGINE_PRIORITY_LOW = 0,
	XE_ENGINE_PRIORITY_NORMAL,
	XE_ENGINE_PRIORITY_HIGH,
	XE_ENGINE_PRIORITY_KERNEL,

	XE_ENGINE_PRIORITY_COUNT
};

/**
 * struct xe_engine - Submission engine
 *
 * Contains all state necessary for submissions. Can either be a user object or
 * a kernel object.
 */
struct xe_engine {
	/** @gt: graphics tile this engine can submit to */
	struct xe_gt *gt;
	/**
	 * @hwe: A hardware of the same class. May (physical engine) or may not
	 * (virtual engine) be where jobs actual engine up running. Should never
	 * really be used for submissions.
	 */
	struct xe_hw_engine *hwe;
	/** @refcount: ref count of this engine */
	struct kref refcount;
	/** @vm: VM (address space) for this engine */
	struct xe_vm *vm;
	/** @class: class of this engine */
	enum xe_engine_class class;
	/** @priority: priority of this exec queue */
	enum xe_engine_priority priority;
	/**
	 * @logical_mask: logical mask of where job submitted to engine can run
	 */
	u32 logical_mask;
	/** @name: name of this engine */
	char name[MAX_FENCE_NAME_LEN];
	/** @width: width (number BB submitted per exec) of this engine */
	u16 width;
	/** @fence_irq: fence IRQ used to signal job completion */
	struct xe_hw_fence_irq *fence_irq;

#define ENGINE_FLAG_BANNED		BIT(0)
#define ENGINE_FLAG_KERNEL		BIT(1)
#define ENGINE_FLAG_PERSISTENT		BIT(2)
#define ENGINE_FLAG_COMPUTE_MODE	BIT(3)
#define ENGINE_FLAG_VM			BIT(4)
#define ENGINE_FLAG_BIND_ENGINE_CHILD	BIT(5)
#define ENGINE_FLAG_WA			BIT(6)

	/**
	 * @flags: flags for this engine, should statically setup aside from ban
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
		/** @execlist: execlist backend specific state for engine */
		struct xe_execlist_engine *execlist;
		/** @guc: GuC backend specific state for engine */
		struct xe_guc_engine *guc;
	};

	/**
	 * @persistent: persistent engine state
	 */
	struct {
		/** @xef: file which this engine belongs to */
		struct xe_file *xef;
		/** @link: link in list of persistent engines */
		struct list_head link;
	} persistent;

	union {
		/**
		 * @parallel: parallel submission state
		 */
		struct {
			/** @composite_fence_ctx: context composite fence */
			u64 composite_fence_ctx;
			/** @composite_fence_seqno: seqno for composite fence */
			u32 composite_fence_seqno;
		} parallel;
		/**
		 * @bind: bind submission state
		 */
		struct {
			/** @fence_ctx: context bind fence */
			u64 fence_ctx;
			/** @fence_seqno: seqno for bind fence */
			u32 fence_seqno;
		} bind;
	};

	/** @sched_props: scheduling properties */
	struct {
		/** @timeslice_us: timeslice period in micro-seconds */
		u32 timeslice_us;
		/** @preempt_timeout_us: preemption timeout in micro-seconds */
		u32 preempt_timeout_us;
	} sched_props;

	/** @compute: compute engine state */
	struct {
		/** @pfence: preemption fence */
		struct dma_fence *pfence;
		/** @context: preemption fence context */
		u64 context;
		/** @seqno: preemption fence seqno */
		u32 seqno;
		/** @link: link into VM's list of engines */
		struct list_head link;
		/** @lock: preemption fences lock */
		spinlock_t lock;
	} compute;

	/** @usm: unified shared memory state */
	struct {
		/** @acc_trigger: access counter trigger */
		u32 acc_trigger;
		/** @acc_notify: access counter notify */
		u32 acc_notify;
		/** @acc_granularity: access counter granularity */
		u32 acc_granularity;
	} usm;

	/** @ops: submission backend engine operations */
	const struct xe_engine_ops *ops;

	/** @ring_ops: ring operations for this engine */
	const struct xe_ring_ops *ring_ops;
	/** @entity: DRM sched entity for this engine (1 to 1 relationship) */
	struct drm_sched_entity *entity;
	/** @lrc: logical ring context for this engine */
	struct xe_lrc lrc[0];
};

/**
 * struct xe_engine_ops - Submission backend engine operations
 */
struct xe_engine_ops {
	/** @init: Initialize engine for submission backend */
	int (*init)(struct xe_engine *e);
	/** @kill: Kill inflight submissions for backend */
	void (*kill)(struct xe_engine *e);
	/** @fini: Fini engine for submission backend */
	void (*fini)(struct xe_engine *e);
	/** @set_priority: Set priority for engine */
	int (*set_priority)(struct xe_engine *e,
			    enum xe_engine_priority priority);
	/** @set_timeslice: Set timeslice for engine */
	int (*set_timeslice)(struct xe_engine *e, u32 timeslice_us);
	/** @set_preempt_timeout: Set preemption timeout for engine */
	int (*set_preempt_timeout)(struct xe_engine *e, u32 preempt_timeout_us);
	/** @set_job_timeout: Set job timeout for engine */
	int (*set_job_timeout)(struct xe_engine *e, u32 job_timeout_ms);
	/**
	 * @suspend: Suspend engine from executing, allowed to be called
	 * multiple times in a row before resume with the caveat that
	 * suspend_wait returns before calling suspend again.
	 */
	int (*suspend)(struct xe_engine *e);
	/**
	 * @suspend_wait: Wait for an engine to suspend executing, should be
	 * call after suspend.
	 */
	void (*suspend_wait)(struct xe_engine *e);
	/**
	 * @resume: Resume engine execution, engine must be in a suspended
	 * state and dma fence returned from most recent suspend call must be
	 * signalled when this function is called.
	 */
	void (*resume)(struct xe_engine *e);
};

#endif
