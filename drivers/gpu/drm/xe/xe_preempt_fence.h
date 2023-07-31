/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_PREEMPT_FENCE_H_
#define _XE_PREEMPT_FENCE_H_

#include "xe_preempt_fence_types.h"

struct list_head;

struct dma_fence *
xe_preempt_fence_create(struct xe_exec_queue *q,
			u64 context, u32 seqno);

struct xe_preempt_fence *xe_preempt_fence_alloc(void);

void xe_preempt_fence_free(struct xe_preempt_fence *pfence);

struct dma_fence *
xe_preempt_fence_arm(struct xe_preempt_fence *pfence, struct xe_exec_queue *q,
		     u64 context, u32 seqno);

static inline struct xe_preempt_fence *
to_preempt_fence(struct dma_fence *fence)
{
	return container_of(fence, struct xe_preempt_fence, base);
}

/**
 * xe_preempt_fence_link() - Return a link used to keep unarmed preempt
 * fences on a list.
 * @pfence: Pointer to the preempt fence.
 *
 * The link is embedded in the struct xe_preempt_fence. Use
 * link_to_preempt_fence() to convert back to the preempt fence.
 *
 * Return: A pointer to an embedded struct list_head.
 */
static inline struct list_head *
xe_preempt_fence_link(struct xe_preempt_fence *pfence)
{
	return &pfence->link;
}

/**
 * to_preempt_fence_from_link() - Convert back to a preempt fence pointer
 * from a link obtained with xe_preempt_fence_link().
 * @link: The struct list_head obtained from xe_preempt_fence_link().
 *
 * Return: A pointer to the embedding struct xe_preempt_fence.
 */
static inline struct xe_preempt_fence *
to_preempt_fence_from_link(struct list_head *link)
{
	return container_of(link, struct xe_preempt_fence, link);
}

bool xe_fence_is_xe_preempt(const struct dma_fence *fence);
#endif
