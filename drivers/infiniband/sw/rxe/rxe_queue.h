/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/*
 * Copyright (c) 2016 Mellanox Technologies Ltd. All rights reserved.
 * Copyright (c) 2015 System Fabric Works, Inc. All rights reserved.
 */

#ifndef RXE_QUEUE_H
#define RXE_QUEUE_H

/* for definition of shared struct rxe_queue_buf */
#include <uapi/rdma/rdma_user_rxe.h>

/* implements a simple circular buffer that can optionally be
 * shared between user space and the kernel and can be resized
 * the requested element size is rounded up to a power of 2
 * and the number of elements in the buffer is also rounded
 * up to a power of 2. Since the queue is empty when the
 * producer and consumer indices match the maximum capacity
 * of the queue is one less than the number of element slots
 */

struct rxe_queue {
	struct rxe_dev		*rxe;
	struct rxe_queue_buf	*buf;
	struct rxe_mmap_info	*ip;
	size_t			buf_size;
	size_t			elem_size;
	unsigned int		log2_elem_size;
	u32			index_mask;
};

int do_mmap_info(struct rxe_dev *rxe, struct mminfo __user *outbuf,
		 struct ib_udata *udata, struct rxe_queue_buf *buf,
		 size_t buf_size, struct rxe_mmap_info **ip_p);

void rxe_queue_reset(struct rxe_queue *q);

struct rxe_queue *rxe_queue_init(struct rxe_dev *rxe,
				 int *num_elem,
				 unsigned int elem_size);

int rxe_queue_resize(struct rxe_queue *q, unsigned int *num_elem_p,
		     unsigned int elem_size, struct ib_udata *udata,
		     struct mminfo __user *outbuf,
		     /* Protect producers while resizing queue */
		     spinlock_t *producer_lock,
		     /* Protect consumers while resizing queue */
		     spinlock_t *consumer_lock);

void rxe_queue_cleanup(struct rxe_queue *queue);

static inline int next_index(struct rxe_queue *q, int index)
{
	return (index + 1) & q->buf->index_mask;
}

static inline int queue_empty(struct rxe_queue *q)
{
	u32 prod;
	u32 cons;

	/* make sure all changes to queue complete before
	 * testing queue empty
	 */
	prod = smp_load_acquire(&q->buf->producer_index);
	/* same */
	cons = smp_load_acquire(&q->buf->consumer_index);

	return ((prod - cons) & q->index_mask) == 0;
}

static inline int queue_full(struct rxe_queue *q)
{
	u32 prod;
	u32 cons;

	/* make sure all changes to queue complete before
	 * testing queue full
	 */
	prod = smp_load_acquire(&q->buf->producer_index);
	/* same */
	cons = smp_load_acquire(&q->buf->consumer_index);

	return ((prod + 1 - cons) & q->index_mask) == 0;
}

static inline void advance_producer(struct rxe_queue *q)
{
	u32 prod;

	prod = (q->buf->producer_index + 1) & q->index_mask;

	/* make sure all changes to queue complete before
	 * changing producer index
	 */
	smp_store_release(&q->buf->producer_index, prod);
}

static inline void advance_consumer(struct rxe_queue *q)
{
	u32 cons;

	cons = (q->buf->consumer_index + 1) & q->index_mask;

	/* make sure all changes to queue complete before
	 * changing consumer index
	 */
	smp_store_release(&q->buf->consumer_index, cons);
}

static inline void *producer_addr(struct rxe_queue *q)
{
	return q->buf->data + ((q->buf->producer_index & q->index_mask)
				<< q->log2_elem_size);
}

static inline void *consumer_addr(struct rxe_queue *q)
{
	return q->buf->data + ((q->buf->consumer_index & q->index_mask)
				<< q->log2_elem_size);
}

static inline unsigned int producer_index(struct rxe_queue *q)
{
	u32 index;

	/* make sure all changes to queue
	 * complete before getting producer index
	 */
	index = smp_load_acquire(&q->buf->producer_index);
	index &= q->index_mask;

	return index;
}

static inline unsigned int consumer_index(struct rxe_queue *q)
{
	u32 index;

	/* make sure all changes to queue
	 * complete before getting consumer index
	 */
	index = smp_load_acquire(&q->buf->consumer_index);
	index &= q->index_mask;

	return index;
}

static inline void *addr_from_index(struct rxe_queue *q, unsigned int index)
{
	return q->buf->data + ((index & q->index_mask)
				<< q->buf->log2_elem_size);
}

static inline unsigned int index_from_addr(const struct rxe_queue *q,
					   const void *addr)
{
	return (((u8 *)addr - q->buf->data) >> q->log2_elem_size)
		& q->index_mask;
}

static inline unsigned int queue_count(const struct rxe_queue *q)
{
	return (q->buf->producer_index - q->buf->consumer_index)
		& q->index_mask;
}

static inline void *queue_head(struct rxe_queue *q)
{
	return queue_empty(q) ? NULL : consumer_addr(q);
}

#endif /* RXE_QUEUE_H */
