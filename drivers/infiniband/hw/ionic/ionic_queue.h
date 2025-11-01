/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2018-2025, Advanced Micro Devices, Inc. */

#ifndef _IONIC_QUEUE_H_
#define _IONIC_QUEUE_H_

#include <linux/io.h>
#include <ionic_regs.h>

#define IONIC_MAX_DEPTH		0xffff
#define IONIC_MAX_CQ_DEPTH	0xffff
#define IONIC_CQ_RING_ARM	IONIC_DBELL_RING_1
#define IONIC_CQ_RING_SOL	IONIC_DBELL_RING_2

/**
 * struct ionic_queue - Ring buffer used between device and driver
 * @size:	Size of the buffer, in bytes
 * @dma:	Dma address of the buffer
 * @ptr:	Buffer virtual address
 * @prod:	Driver position in the queue
 * @cons:	Device position in the queue
 * @mask:	Capacity of the queue, subtracting the hole
 *		This value is equal to ((1 << depth_log2) - 1)
 * @depth_log2: Log base two size depth of the queue
 * @stride_log2: Log base two size of an element in the queue
 * @dbell:	Doorbell identifying bits
 */
struct ionic_queue {
	size_t size;
	dma_addr_t dma;
	void *ptr;
	u16 prod;
	u16 cons;
	u16 mask;
	u8 depth_log2;
	u8 stride_log2;
	u64 dbell;
};

/**
 * ionic_queue_init() - Initialize user space queue
 * @q:		Uninitialized queue structure
 * @dma_dev:	DMA device for mapping
 * @depth:	Depth of the queue
 * @stride:	Size of each element of the queue
 *
 * Return: status code
 */
int ionic_queue_init(struct ionic_queue *q, struct device *dma_dev,
		     int depth, size_t stride);

/**
 * ionic_queue_destroy() - Destroy user space queue
 * @q:		Queue structure
 * @dma_dev:	DMA device for mapping
 *
 * Return: status code
 */
void ionic_queue_destroy(struct ionic_queue *q, struct device *dma_dev);

/**
 * ionic_queue_empty() - Test if queue is empty
 * @q:		Queue structure
 *
 * This is only valid for to-device queues.
 *
 * Return: is empty
 */
static inline bool ionic_queue_empty(struct ionic_queue *q)
{
	return q->prod == q->cons;
}

/**
 * ionic_queue_length() - Get the current length of the queue
 * @q:		Queue structure
 *
 * This is only valid for to-device queues.
 *
 * Return: length
 */
static inline u16 ionic_queue_length(struct ionic_queue *q)
{
	return (q->prod - q->cons) & q->mask;
}

/**
 * ionic_queue_length_remaining() - Get the remaining length of the queue
 * @q:		Queue structure
 *
 * This is only valid for to-device queues.
 *
 * Return: length remaining
 */
static inline u16 ionic_queue_length_remaining(struct ionic_queue *q)
{
	return q->mask - ionic_queue_length(q);
}

/**
 * ionic_queue_full() - Test if queue is full
 * @q:		Queue structure
 *
 * This is only valid for to-device queues.
 *
 * Return: is full
 */
static inline bool ionic_queue_full(struct ionic_queue *q)
{
	return q->mask == ionic_queue_length(q);
}

/**
 * ionic_color_wrap() - Flip the color if prod is wrapped
 * @prod:	Queue index just after advancing
 * @color:	Queue color just prior to advancing the index
 *
 * Return: color after advancing the index
 */
static inline bool ionic_color_wrap(u16 prod, bool color)
{
	/* logical xor color with (prod == 0) */
	return color != (prod == 0);
}

/**
 * ionic_queue_at() - Get the element at the given index
 * @q:		Queue structure
 * @idx:	Index in the queue
 *
 * The index must be within the bounds of the queue.  It is not checked here.
 *
 * Return: pointer to element at index
 */
static inline void *ionic_queue_at(struct ionic_queue *q, u16 idx)
{
	return q->ptr + ((unsigned long)idx << q->stride_log2);
}

/**
 * ionic_queue_at_prod() - Get the element at the producer index
 * @q:		Queue structure
 *
 * Return: pointer to element at producer index
 */
static inline void *ionic_queue_at_prod(struct ionic_queue *q)
{
	return ionic_queue_at(q, q->prod);
}

/**
 * ionic_queue_at_cons() - Get the element at the consumer index
 * @q:		Queue structure
 *
 * Return: pointer to element at consumer index
 */
static inline void *ionic_queue_at_cons(struct ionic_queue *q)
{
	return ionic_queue_at(q, q->cons);
}

/**
 * ionic_queue_next() - Compute the next index
 * @q:		Queue structure
 * @idx:	Index
 *
 * Return: next index after idx
 */
static inline u16 ionic_queue_next(struct ionic_queue *q, u16 idx)
{
	return (idx + 1) & q->mask;
}

/**
 * ionic_queue_produce() - Increase the producer index
 * @q:		Queue structure
 *
 * Caller must ensure that the queue is not full.  It is not checked here.
 */
static inline void ionic_queue_produce(struct ionic_queue *q)
{
	q->prod = ionic_queue_next(q, q->prod);
}

/**
 * ionic_queue_consume() - Increase the consumer index
 * @q:		Queue structure
 *
 * Caller must ensure that the queue is not empty.  It is not checked here.
 *
 * This is only valid for to-device queues.
 */
static inline void ionic_queue_consume(struct ionic_queue *q)
{
	q->cons = ionic_queue_next(q, q->cons);
}

/**
 * ionic_queue_consume_entries() - Increase the consumer index by entries
 * @q:				Queue structure
 * @entries:		Number of entries to increment
 *
 * Caller must ensure that the queue is not empty.  It is not checked here.
 *
 * This is only valid for to-device queues.
 */
static inline void ionic_queue_consume_entries(struct ionic_queue *q,
					       u16 entries)
{
	q->cons = (q->cons + entries) & q->mask;
}

/**
 * ionic_queue_dbell_init() - Initialize doorbell bits for queue id
 * @q:		Queue structure
 * @qid:	Queue identifying number
 */
static inline void ionic_queue_dbell_init(struct ionic_queue *q, u32 qid)
{
	q->dbell = IONIC_DBELL_QID(qid);
}

/**
 * ionic_queue_dbell_val() - Get current doorbell update value
 * @q:		Queue structure
 *
 * Return: current doorbell update value
 */
static inline u64 ionic_queue_dbell_val(struct ionic_queue *q)
{
	return q->dbell | q->prod;
}

#endif /* _IONIC_QUEUE_H_ */
