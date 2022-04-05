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
	int (*work)(struct dma_fence_work *f);
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

void dma_fence_work_init(struct dma_fence_work *f,
			 const struct dma_fence_work_ops *ops);
int dma_fence_work_chain(struct dma_fence_work *f, struct dma_fence *signal);

static inline void dma_fence_work_commit(struct dma_fence_work *f)
{
	i915_sw_fence_commit(&f->chain);
}

#endif /* I915_SW_FENCE_WORK_H */
