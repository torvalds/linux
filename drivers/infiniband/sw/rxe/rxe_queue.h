/*
 * Copyright (c) 2016 Mellanox Technologies Ltd. All rights reserved.
 * Copyright (c) 2015 System Fabric Works, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *	- Redistributions of source code must retain the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer.
 *
 *	- Redistributions in binary form must reproduce the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer in the documentation and/or other materials
 *	  provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef RXE_QUEUE_H
#define RXE_QUEUE_H

/* implements a simple circular buffer that can optionally be
 * shared between user space and the kernel and can be resized

 * the requested element size is rounded up to a power of 2
 * and the number of elements in the buffer is also rounded
 * up to a power of 2. Since the queue is empty when the
 * producer and consumer indices match the maximum capacity
 * of the queue is one less than the number of element slots
 */

/* this data structure is shared between user space and kernel
 * space for those cases where the queue is shared. It contains
 * the producer and consumer indices. Is also contains a copy
 * of the queue size parameters for user space to use but the
 * kernel must use the parameters in the rxe_queue struct
 * this MUST MATCH the corresponding librxe struct
 * for performance reasons arrange to have producer and consumer
 * pointers in separate cache lines
 * the kernel should always mask the indices to avoid accessing
 * memory outside of the data area
 */
struct rxe_queue_buf {
	__u32			log2_elem_size;
	__u32			index_mask;
	__u32			pad_1[30];
	__u32			producer_index;
	__u32			pad_2[31];
	__u32			consumer_index;
	__u32			pad_3[31];
	__u8			data[0];
};

struct rxe_queue {
	struct rxe_dev		*rxe;
	struct rxe_queue_buf	*buf;
	struct rxe_mmap_info	*ip;
	size_t			buf_size;
	size_t			elem_size;
	unsigned int		log2_elem_size;
	unsigned int		index_mask;
};

int do_mmap_info(struct rxe_dev *rxe,
		 struct ib_udata *udata,
		 bool is_req,
		 struct ib_ucontext *context,
		 struct rxe_queue_buf *buf,
		 size_t buf_size,
		 struct rxe_mmap_info **ip_p);

struct rxe_queue *rxe_queue_init(struct rxe_dev *rxe,
				 int *num_elem,
				 unsigned int elem_size);

int rxe_queue_resize(struct rxe_queue *q,
		     unsigned int *num_elem_p,
		     unsigned int elem_size,
		     struct ib_ucontext *context,
		     struct ib_udata *udata,
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
	return ((q->buf->producer_index - q->buf->consumer_index)
			& q->index_mask) == 0;
}

static inline int queue_full(struct rxe_queue *q)
{
	return ((q->buf->producer_index + 1 - q->buf->consumer_index)
			& q->index_mask) == 0;
}

static inline void advance_producer(struct rxe_queue *q)
{
	q->buf->producer_index = (q->buf->producer_index + 1)
			& q->index_mask;
}

static inline void advance_consumer(struct rxe_queue *q)
{
	q->buf->consumer_index = (q->buf->consumer_index + 1)
			& q->index_mask;
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
	return q->buf->producer_index;
}

static inline unsigned int consumer_index(struct rxe_queue *q)
{
	return q->buf->consumer_index;
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
