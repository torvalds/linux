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
 * Struct guc_submit_parallel_scratch - A scratch shared mapped buffer.
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

#endif
