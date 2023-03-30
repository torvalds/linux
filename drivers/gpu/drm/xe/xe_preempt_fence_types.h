/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_PREEMPT_FENCE_TYPES_H_
#define _XE_PREEMPT_FENCE_TYPES_H_

#include <linux/dma-fence.h>
#include <linux/workqueue.h>

struct xe_engine;

/**
 * struct xe_preempt_fence - XE preempt fence
 *
 * A preemption fence which suspends the execution of an xe_engine on the
 * hardware and triggers a callback once the xe_engine is complete.
 */
struct xe_preempt_fence {
	/** @base: dma fence base */
	struct dma_fence base;
	/** @link: link into list of pending preempt fences */
	struct list_head link;
	/** @engine: xe engine for this preempt fence */
	struct xe_engine *engine;
	/** @preempt_work: work struct which issues preemption */
	struct work_struct preempt_work;
	/** @error: preempt fence is in error state */
	int error;
};

#endif
