/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2024 Intel Corporation
 */

#ifndef _XE_HW_ENGINE_GROUP_TYPES_H_
#define _XE_HW_ENGINE_GROUP_TYPES_H_

#include "xe_force_wake_types.h"
#include "xe_lrc_types.h"
#include "xe_reg_sr_types.h"

/**
 * enum xe_hw_engine_group_execution_mode - possible execution modes of a hw
 * engine group
 *
 * @EXEC_MODE_LR: execution in long-running mode
 * @EXEC_MODE_DMA_FENCE: execution in dma fence mode
 */
enum xe_hw_engine_group_execution_mode {
	EXEC_MODE_LR,
	EXEC_MODE_DMA_FENCE,
};

/**
 * struct xe_hw_engine_group - Hardware engine group
 *
 * hw engines belong to the same group if they share hardware resources in a way
 * that prevents them from making progress when one is stuck on a page fault.
 */
struct xe_hw_engine_group {
	/**
	 * @exec_queue_list: list of exec queues attached to this
	 * xe_hw_engine_group
	 */
	struct list_head exec_queue_list;
	/** @resume_work: worker to resume faulting LR exec queues */
	struct work_struct resume_work;
	/** @resume_wq: workqueue to resume faulting LR exec queues */
	struct workqueue_struct *resume_wq;
	/**
	 * @mode_sem: used to protect this group's hardware resources and ensure
	 * mutual exclusion between execution only in faulting LR mode and
	 * execution only in DMA_FENCE mode
	 */
	struct rw_semaphore mode_sem;
	/** @cur_mode: current execution mode of this hw engine group */
	enum xe_hw_engine_group_execution_mode cur_mode;
};

#endif
