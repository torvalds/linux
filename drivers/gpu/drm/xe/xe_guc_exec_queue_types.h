/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_GUC_ENGINE_TYPES_H_
#define _XE_GUC_ENGINE_TYPES_H_

#include <linux/spinlock.h>
#include <linux/workqueue.h>

#include "xe_gpu_scheduler_types.h"

struct dma_fence;
struct xe_exec_queue;

/**
 * struct xe_guc_exec_queue - GuC specific state for an xe_exec_queue
 */
struct xe_guc_exec_queue {
	/** @q: Backpointer to parent xe_exec_queue */
	struct xe_exec_queue *q;
	/** @sched: GPU scheduler for this xe_exec_queue */
	struct xe_gpu_scheduler sched;
	/** @entity: Scheduler entity for this xe_exec_queue */
	struct xe_sched_entity entity;
	/**
	 * @static_msgs: Static messages for this xe_exec_queue, used when
	 * a message needs to sent through the GPU scheduler but memory
	 * allocations are not allowed.
	 */
#define MAX_STATIC_MSG_TYPE	3
	struct xe_sched_msg static_msgs[MAX_STATIC_MSG_TYPE];
	/** @lr_tdr: long running TDR worker */
	struct work_struct lr_tdr;
	/** @fini_async: do final fini async from this worker */
	struct work_struct fini_async;
	/** @resume_time: time of last resume */
	u64 resume_time;
	/** @state: GuC specific state for this xe_exec_queue */
	atomic_t state;
	/** @wqi_head: work queue item tail */
	u32 wqi_head;
	/** @wqi_tail: work queue item tail */
	u32 wqi_tail;
	/** @id: GuC id for this exec_queue */
	u16 id;
	/** @suspend_wait: wait queue used to wait on pending suspends */
	wait_queue_head_t suspend_wait;
	/** @suspend_pending: a suspend of the exec_queue is pending */
	bool suspend_pending;
};

#endif
