/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef _XE_HW_FENCE_H_
#define _XE_HW_FENCE_H_

#include "xe_hw_fence_types.h"

/* Cause an early wrap to catch wrapping errors */
#define XE_FENCE_INITIAL_SEQNO (-127)

int xe_hw_fence_module_init(void);
void xe_hw_fence_module_exit(void);

void xe_hw_fence_irq_init(struct xe_hw_fence_irq *irq);
void xe_hw_fence_irq_finish(struct xe_hw_fence_irq *irq);
void xe_hw_fence_irq_run(struct xe_hw_fence_irq *irq);
void xe_hw_fence_irq_stop(struct xe_hw_fence_irq *irq);
void xe_hw_fence_irq_start(struct xe_hw_fence_irq *irq);

void xe_hw_fence_ctx_init(struct xe_hw_fence_ctx *ctx, struct xe_gt *gt,
			  struct xe_hw_fence_irq *irq, const char *name);
void xe_hw_fence_ctx_finish(struct xe_hw_fence_ctx *ctx);

struct dma_fence *xe_hw_fence_alloc(void);

void xe_hw_fence_free(struct dma_fence *fence);

void xe_hw_fence_init(struct dma_fence *fence, struct xe_hw_fence_ctx *ctx,
		      struct iosys_map seqno_map);
#endif
