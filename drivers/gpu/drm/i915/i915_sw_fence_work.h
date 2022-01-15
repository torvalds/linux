/* SPDX-License-Identifier: MIT */

/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef I915_SW_FENCE_WORK_H
#define I915_SW_FENCE_WORK_H

#include <linux/dma-fence.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

#include "i915_sw_fence.h"

struct dma_fence_work;

struct dma_fence_work_ops {
	const char *name;
	void (*work)(struct dma_fence_work *f);
	void (*release)(struct dma_fence_work *f);
};

struct dma_fence_work {
	struct dma_fence dma;
	spinlock_t lock;

	struct i915_sw_fence chain;
	struct i915_sw_dma_fence_cb cb;

	struct work_struct work;
	const struct dma_fence_work_ops *ops;
};

enum {
	DMA_FENCE_WORK_IMM = DMA_FENCE_FLAG_USER_BITS,
};

void dma_fence_work_init(struct dma_fence_work *f,
			 const struct dma_fence_work_ops *ops);
int dma_fence_work_chain(struct dma_fence_work *f, struct dma_fence *signal);

static inline void dma_fence_work_commit(struct dma_fence_work *f)
{
	i915_sw_fence_commit(&f->chain);
}

/**
 * dma_fence_work_commit_imm: Commit the fence, and if possible execute locally.
 * @f: the fenced worker
 *
 * Instead of always scheduling a worker to execute the callback (see
 * dma_fence_work_commit()), we try to execute the callback immediately in
 * the local context. It is required that the fence be committed before it
 * is published, and that no other threads try to tamper with the number
 * of asynchronous waits on the fence (or else the callback will be
 * executed in the wrong context, i.e. not the callers).
 */
static inline void dma_fence_work_commit_imm(struct dma_fence_work *f)
{
	if (atomic_read(&f->chain.pending) <= 1)
		__set_bit(DMA_FENCE_WORK_IMM, &f->dma.flags);

	dma_fence_work_commit(f);
}

#endif /* I915_SW_FENCE_WORK_H */
