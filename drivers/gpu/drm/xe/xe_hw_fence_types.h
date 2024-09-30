/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_HW_FENCE_TYPES_H_
#define _XE_HW_FENCE_TYPES_H_

#include <linux/dma-fence.h>
#include <linux/iosys-map.h>
#include <linux/irq_work.h>
#include <linux/list.h>
#include <linux/spinlock.h>

struct xe_device;
struct xe_gt;

/**
 * struct xe_hw_fence_irq - hardware fence IRQ handler
 *
 * One per engine class, signals completed xe_hw_fences, triggered via hw engine
 * interrupt. On each trigger, search list of pending fences and signal.
 */
struct xe_hw_fence_irq {
	/** @lock: protects all xe_hw_fences + pending list */
	spinlock_t lock;
	/** @work: IRQ worker run to signal the fences */
	struct irq_work work;
	/** @pending: list of pending xe_hw_fences */
	struct list_head pending;
	/** @enabled: fence signaling enabled */
	bool enabled;
};

#define MAX_FENCE_NAME_LEN	16

/**
 * struct xe_hw_fence_ctx - hardware fence context
 *
 * The context for a hardware fence. 1 to 1 relationship with xe_engine. Points
 * to a xe_hw_fence_irq, maintains serial seqno.
 */
struct xe_hw_fence_ctx {
	/** @gt: graphics tile of hardware fence context */
	struct xe_gt *gt;
	/** @irq: fence irq handler */
	struct xe_hw_fence_irq *irq;
	/** @dma_fence_ctx: dma fence context for hardware fence */
	u64 dma_fence_ctx;
	/** @next_seqno: next seqno for hardware fence */
	u32 next_seqno;
	/** @name: name of hardware fence context */
	char name[MAX_FENCE_NAME_LEN];
};

/**
 * struct xe_hw_fence - hardware fence
 *
 * Used to indicate a xe_sched_job is complete via a seqno written to memory.
 * Signals on error or seqno past.
 */
struct xe_hw_fence {
	/** @dma: base dma fence for hardware fence context */
	struct dma_fence dma;
	/** @xe: Xe device for hw fence driver name */
	struct xe_device *xe;
	/** @name: name of hardware fence context */
	char name[MAX_FENCE_NAME_LEN];
	/** @seqno_map: I/O map for seqno */
	struct iosys_map seqno_map;
	/** @irq_link: Link in struct xe_hw_fence_irq.pending */
	struct list_head irq_link;
};

#endif
