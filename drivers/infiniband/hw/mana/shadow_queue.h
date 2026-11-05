/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/*
 * Copyright (c) 2024, Microsoft Corporation. All rights reserved.
 */

#ifndef _MANA_SHADOW_QUEUE_H_
#define _MANA_SHADOW_QUEUE_H_

#include <linux/build_bug.h>

#define MANA_WQ_FENCE_WC		BIT(0)
#define MANA_WQ_NO_SIGNAL_WC		BIT(1)
#define MANA_WQE_OFFSET_MASK	GENMASK(23, 0)

struct shadow_wqe_header {
	u64 wr_id;
	u64 wqe_offset_or_psn : 24;
	u64 wqe_size_in_bu : 8;
	u64 fsn : 24;
	u64 send_opcode : 4;
	u64 flags : 2;
};

struct shadow_queue {
	/* Unmasked producer index, Incremented on wqe posting */
	u64 prod_idx;
	/* Unmasked consumer index, Incremented on cq polling */
	u64 cons_idx;
	/* queue size in wqes */
	u32 length;
	/* distance between elements in bytes */
	u32 stride;
	/* ring buffer holding wqes */
	void *buffer;
};

static inline int create_shadow_queue(struct shadow_queue *queue, uint32_t length, uint32_t stride)
{
	queue->buffer = kvmalloc_array(length, stride, GFP_KERNEL);
	if (!queue->buffer)
		return -ENOMEM;

	queue->length = length;
	queue->stride = stride;

	return 0;
}

static inline void reset_shadow_queue(struct shadow_queue *queue)
{
	queue->prod_idx = 0;
	queue->cons_idx = 0;
}

static inline void destroy_shadow_queue(struct shadow_queue *queue)
{
	kvfree(queue->buffer);
}

static inline bool shadow_queue_full(struct shadow_queue *queue)
{
	/* Do not reuse an entry until the poller has finished reading it. */
	return (queue->prod_idx - smp_load_acquire(&queue->cons_idx)) >= queue->length;
}

static inline bool shadow_queue_empty(struct shadow_queue *queue)
{
	/* Pair with posting's release of the initialized shadow WQE. */
	return smp_load_acquire(&queue->prod_idx) == queue->cons_idx;
}

static inline void *
shadow_queue_get_element(const struct shadow_queue *queue, u64 unmasked_index)
{
	u32 index = unmasked_index % queue->length;

	return ((u8 *)queue->buffer + index * queue->stride);
}

static inline void *
shadow_queue_producer_entry(struct shadow_queue *queue)
{
	return shadow_queue_get_element(queue, queue->prod_idx);
}

static inline void *
shadow_queue_get_next_to_consume(const struct shadow_queue *queue)
{
	/* The producer publishes the WQE before advancing prod_idx. */
	if (queue->cons_idx == smp_load_acquire(&queue->prod_idx))
		return NULL;

	return shadow_queue_get_element(queue, queue->cons_idx);
}

static inline void shadow_queue_advance_producer(struct shadow_queue *queue)
{
	/* Publish all WQE fields to CQ polling on another CPU. */
	smp_store_release(&queue->prod_idx, queue->prod_idx + 1);
}

static inline void shadow_queue_advance_consumer(struct shadow_queue *queue)
{
	/* Finish WC generation and queue-tail updates before allowing reuse. */
	smp_store_release(&queue->cons_idx, queue->cons_idx + 1);
}

#endif
