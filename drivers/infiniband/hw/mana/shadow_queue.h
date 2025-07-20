/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/*
 * Copyright (c) 2024, Microsoft Corporation. All rights reserved.
 */

#ifndef _MANA_SHADOW_QUEUE_H_
#define _MANA_SHADOW_QUEUE_H_

struct shadow_wqe_header {
	u16 opcode;
	u16 error_code;
	u32 posted_wqe_size;
	u64 wr_id;
};

struct ud_rq_shadow_wqe {
	struct shadow_wqe_header header;
	u32 byte_len;
	u32 src_qpn;
};

struct ud_sq_shadow_wqe {
	struct shadow_wqe_header header;
};

struct shadow_queue {
	/* Unmasked producer index, Incremented on wqe posting */
	u64 prod_idx;
	/* Unmasked consumer index, Incremented on cq polling */
	u64 cons_idx;
	/* Unmasked index of next-to-complete (from HW) shadow WQE */
	u64 next_to_complete_idx;
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

static inline void destroy_shadow_queue(struct shadow_queue *queue)
{
	kvfree(queue->buffer);
}

static inline bool shadow_queue_full(struct shadow_queue *queue)
{
	return (queue->prod_idx - queue->cons_idx) >= queue->length;
}

static inline bool shadow_queue_empty(struct shadow_queue *queue)
{
	return queue->prod_idx == queue->cons_idx;
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
	if (queue->cons_idx == queue->next_to_complete_idx)
		return NULL;

	return shadow_queue_get_element(queue, queue->cons_idx);
}

static inline void *
shadow_queue_get_next_to_complete(struct shadow_queue *queue)
{
	if (queue->next_to_complete_idx == queue->prod_idx)
		return NULL;

	return shadow_queue_get_element(queue, queue->next_to_complete_idx);
}

static inline void shadow_queue_advance_producer(struct shadow_queue *queue)
{
	queue->prod_idx++;
}

static inline void shadow_queue_advance_consumer(struct shadow_queue *queue)
{
	queue->cons_idx++;
}

static inline void shadow_queue_advance_next_to_complete(struct shadow_queue *queue)
{
	queue->next_to_complete_idx++;
}

#endif
