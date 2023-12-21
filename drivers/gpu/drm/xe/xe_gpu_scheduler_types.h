/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_GPU_SCHEDULER_TYPES_H_
#define _XE_GPU_SCHEDULER_TYPES_H_

#include <drm/gpu_scheduler.h>

/**
 * struct xe_sched_msg - an in-band (relative to GPU scheduler run queue)
 * message
 *
 * Generic enough for backend defined messages, backend can expand if needed.
 */
struct xe_sched_msg {
	/** @link: list link into the gpu scheduler list of messages */
	struct list_head		link;
	/**
	 * @private_data: opaque pointer to message private data (backend defined)
	 */
	void				*private_data;
	/** @opcode: opcode of message (backend defined) */
	unsigned int			opcode;
};

/**
 * struct xe_sched_backend_ops - Define the backend operations called by the
 * scheduler
 */
struct xe_sched_backend_ops {
	/**
	 * @process_msg: Process a message. Allowed to block, it is this
	 * function's responsibility to free message if dynamically allocated.
	 */
	void (*process_msg)(struct xe_sched_msg *msg);
};

/**
 * struct xe_gpu_scheduler - Xe GPU scheduler
 */
struct xe_gpu_scheduler {
	/** @base: DRM GPU scheduler */
	struct drm_gpu_scheduler		base;
	/** @ops: Xe scheduler ops */
	const struct xe_sched_backend_ops	*ops;
	/** @msgs: list of messages to be processed in @work_process_msg */
	struct list_head			msgs;
	/** @work_process_msg: processes messages */
	struct work_struct		work_process_msg;
};

#define xe_sched_entity		drm_sched_entity
#define xe_sched_policy		drm_sched_policy

#endif
