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
 *
 * Notes:
 *   - Kernel space indices are always masked off to q->index_mask
 *   before storing so do not need to be checked on reads.
 *   - User space indices may be out of range and must be
 *   masked before use when read.
 *   - The kernel indices for shared queues must not be written
 *   by user space so a local copy is used and a shared copy is
 *   stored when the local copy changes.
 *   - By passing the type in the parameter list separate from q
 *   the compiler can eliminate the switch statement when the
 *   actual queue type is known when the function is called.
 *   In the performance path this is done. In less critical
 *   paths just q->type is passed.
 */

/* type of queue */
enum queue_type {
	QUEUE_TYPE_KERNEL,
	QUEUE_TYPE_TO_USER,
	QUEUE_TYPE_FROM_USER,
};

struct rxe_queue {
	struct rxe_dev		*rxe;
	struct rxe_queue_buf	*buf;
	struct rxe_mmap_info	*ip;
	size_t			buf_size;
	size_t			elem_size;
	unsigned int		log2_elem_size;
	u32			index_mask;
	enum queue_type		type;
	/* private copy of index for shared queues between
	 * kernel space and user space. Kernel reads and writes
	 * this copy and then replicates to rxe_queue_buf
	 * for read access by user space.
	 */
	u32			index;
};

int do_mmap_info(struct rxe_dev *rxe, struct mminfo __user *outbuf,
		 struct ib_udata *udata, struct rxe_queue_buf *buf,
		 size_t buf_size, struct rxe_mmap_info **ip_p);

void rxe_queue_reset(struct rxe_queue *q);

struct rxe_queue *rxe_queue_init(struct rxe_dev *rxe, int *num_elem,
			unsigned int elem_size, enum queue_type type);

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

static inline int queue_empty(struct rxe_queue *q, enum queue_type type)
{
	u32 prod;
	u32 cons;

	switch (type) {
	case QUEUE_TYPE_FROM_USER:
		/* protect user space index */
		prod = smp_load_acquire(&q->buf->producer_index);
		cons = q->index;
		break;
	case QUEUE_TYPE_TO_USER:
		prod = q->index;
		/* protect user space index */
		cons = smp_load_acquire(&q->buf->consumer_index);
		break;
	case QUEUE_TYPE_KERNEL:
		prod = q->buf->producer_index;
		cons = q->buf->consumer_index;
		break;
	}

	return ((prod - cons) & q->index_mask) == 0;
}

static inline int queue_full(struct rxe_queue *q, enum queue_type type)
{
	u32 prod;
	u32 cons;

	switch (type) {
	case QUEUE_TYPE_FROM_USER:
		/* protect user space index */
		prod = smp_load_acquire(&q->buf->producer_index);
		cons = q->index;
		break;
	case QUEUE_TYPE_TO_USER:
		prod = q->index;
		/* protect user space index */
		cons = smp_load_acquire(&q->buf->consumer_index);
		break;
	case QUEUE_TYPE_KERNEL:
		prod = q->buf->producer_index;
		cons = q->buf->consumer_index;
		break;
	}

	return ((prod + 1 - cons) & q->index_mask) == 0;
}

static inline unsigned int queue_count(const struct rxe_queue *q,
					enum queue_type type)
{
	u32 prod;
	u32 cons;

	switch (type) {
	case QUEUE_TYPE_FROM_USER:
		/* protect user space index */
		prod = smp_load_acquire(&q->buf->producer_index);
		cons = q->index;
		break;
	case QUEUE_TYPE_TO_USER:
		prod = q->index;
		/* protect user space index */
		cons = smp_load_acquire(&q->buf->consumer_index);
		break;
	case QUEUE_TYPE_KERNEL:
		prod = q->buf->producer_index;
		cons = q->buf->consumer_index;
		break;
	}

	return (prod - cons) & q->index_mask;
}

static inline void advance_producer(struct rxe_queue *q, enum queue_type type)
{
	u32 prod;

	switch (type) {
	case QUEUE_TYPE_FROM_USER:
		pr_warn_once("Normally kernel should not write user space index\n");
		/* protect user space index */
		prod = smp_load_acquire(&q->buf->producer_index);
		prod = (prod + 1) & q->index_mask;
		/* same */
		smp_store_release(&q->buf->producer_index, prod);
		break;
	case QUEUE_TYPE_TO_USER:
		prod = q->index;
		q->index = (prod + 1) & q->index_mask;
		q->buf->producer_index = q->index;
		break;
	case QUEUE_TYPE_KERNEL:
		prod = q->buf->producer_index;
		q->buf->producer_index = (prod + 1) & q->index_mask;
		break;
	}
}

static inline void advance_consumer(struct rxe_queue *q, enum queue_type type)
{
	u32 cons;

	switch (type) {
	case QUEUE_TYPE_FROM_USER:
		cons = q->index;
		q->index = (cons + 1) & q->index_mask;
		q->buf->consumer_index = q->index;
		break;
	case QUEUE_TYPE_TO_USER:
		pr_warn_once("Normally kernel should not write user space index\n");
		/* protect user space index */
		cons = smp_load_acquire(&q->buf->consumer_index);
		cons = (cons + 1) & q->index_mask;
		/* same */
		smp_store_release(&q->buf->consumer_index, cons);
		break;
	case QUEUE_TYPE_KERNEL:
		cons = q->buf->consumer_index;
		q->buf->consumer_index = (cons + 1) & q->index_mask;
		break;
	}
}

static inline void *producer_addr(struct rxe_queue *q, enum queue_type type)
{
	u32 prod;

	switch (type) {
	case QUEUE_TYPE_FROM_USER:
		/* protect user space index */
		prod = smp_load_acquire(&q->buf->producer_index);
		prod &= q->index_mask;
		break;
	case QUEUE_TYPE_TO_USER:
		prod = q->index;
		break;
	case QUEUE_TYPE_KERNEL:
		prod = q->buf->producer_index;
		break;
	}

	return q->buf->data + (prod << q->log2_elem_size);
}

static inline void *consumer_addr(struct rxe_queue *q, enum queue_type type)
{
	u32 cons;

	switch (type) {
	case QUEUE_TYPE_FROM_USER:
		cons = q->index;
		break;
	case QUEUE_TYPE_TO_USER:
		/* protect user space index */
		cons = smp_load_acquire(&q->buf->consumer_index);
		cons &= q->index_mask;
		break;
	case QUEUE_TYPE_KERNEL:
		cons = q->buf->consumer_index;
		break;
	}

	return q->buf->data + (cons << q->log2_elem_size);
}

static inline unsigned int producer_index(struct rxe_queue *q,
						enum queue_type type)
{
	u32 prod;

	switch (type) {
	case QUEUE_TYPE_FROM_USER:
		/* protect user space index */
		prod = smp_load_acquire(&q->buf->producer_index);
		prod &= q->index_mask;
		break;
	case QUEUE_TYPE_TO_USER:
		prod = q->index;
		break;
	case QUEUE_TYPE_KERNEL:
		prod = q->buf->producer_index;
		break;
	}

	return prod;
}

static inline unsigned int consumer_index(struct rxe_queue *q,
						enum queue_type type)
{
	u32 cons;

	switch (type) {
	case QUEUE_TYPE_FROM_USER:
		cons = q->index;
		break;
	case QUEUE_TYPE_TO_USER:
		/* protect user space index */
		cons = smp_load_acquire(&q->buf->consumer_index);
		cons &= q->index_mask;
		break;
	case QUEUE_TYPE_KERNEL:
		cons = q->buf->consumer_index;
		break;
	}

	return cons;
}

static inline void *addr_from_index(struct rxe_queue *q,
				unsigned int index)
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

static inline void *queue_head(struct rxe_queue *q, enum queue_type type)
{
	return queue_empty(q, type) ? NULL : consumer_addr(q, type);
}

#endif /* RXE_QUEUE_H */
