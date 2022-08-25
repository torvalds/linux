// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2016 Mellanox Technologies Ltd. All rights reserved.
 * Copyright (c) 2015 System Fabric Works, Inc. All rights reserved.
 */

#include <linux/vmalloc.h>
#include "rxe.h"
#include "rxe_loc.h"
#include "rxe_queue.h"

int do_mmap_info(struct rxe_dev *rxe, struct mminfo __user *outbuf,
		 struct ib_udata *udata, struct rxe_queue_buf *buf,
		 size_t buf_size, struct rxe_mmap_info **ip_p)
{
	int err;
	struct rxe_mmap_info *ip = NULL;

	if (outbuf) {
		ip = rxe_create_mmap_info(rxe, buf_size, udata, buf);
		if (IS_ERR(ip)) {
			err = PTR_ERR(ip);
			goto err1;
		}

		if (copy_to_user(outbuf, &ip->info, sizeof(ip->info))) {
			err = -EFAULT;
			goto err2;
		}

		spin_lock_bh(&rxe->pending_lock);
		list_add(&ip->pending_mmaps, &rxe->pending_mmaps);
		spin_unlock_bh(&rxe->pending_lock);
	}

	*ip_p = ip;

	return 0;

err2:
	kfree(ip);
err1:
	return err;
}

inline void rxe_queue_reset(struct rxe_queue *q)
{
	/* queue is comprised from header and the memory
	 * of the actual queue. See "struct rxe_queue_buf" in rxe_queue.h
	 * reset only the queue itself and not the management header
	 */
	memset(q->buf->data, 0, q->buf_size - sizeof(struct rxe_queue_buf));
}

struct rxe_queue *rxe_queue_init(struct rxe_dev *rxe, int *num_elem,
			unsigned int elem_size, enum queue_type type)
{
	struct rxe_queue *q;
	size_t buf_size;
	unsigned int num_slots;

	/* num_elem == 0 is allowed, but uninteresting */
	if (*num_elem < 0)
		goto err1;

	q = kzalloc(sizeof(*q), GFP_KERNEL);
	if (!q)
		goto err1;

	q->rxe = rxe;
	q->type = type;

	/* used in resize, only need to copy used part of queue */
	q->elem_size = elem_size;

	/* pad element up to at least a cacheline and always a power of 2 */
	if (elem_size < cache_line_size())
		elem_size = cache_line_size();
	elem_size = roundup_pow_of_two(elem_size);

	q->log2_elem_size = order_base_2(elem_size);

	num_slots = *num_elem + 1;
	num_slots = roundup_pow_of_two(num_slots);
	q->index_mask = num_slots - 1;

	buf_size = sizeof(struct rxe_queue_buf) + num_slots * elem_size;

	q->buf = vmalloc_user(buf_size);
	if (!q->buf)
		goto err2;

	q->buf->log2_elem_size = q->log2_elem_size;
	q->buf->index_mask = q->index_mask;

	q->buf_size = buf_size;

	*num_elem = num_slots - 1;
	return q;

err2:
	kfree(q);
err1:
	return NULL;
}

/* copies elements from original q to new q and then swaps the contents of the
 * two q headers. This is so that if anyone is holding a pointer to q it will
 * still work
 */
static int resize_finish(struct rxe_queue *q, struct rxe_queue *new_q,
			 unsigned int num_elem)
{
	enum queue_type type = q->type;
	u32 new_prod;
	u32 prod;
	u32 cons;

	if (!queue_empty(q, q->type) && (num_elem < queue_count(q, type)))
		return -EINVAL;

	new_prod = queue_get_producer(new_q, type);
	prod = queue_get_producer(q, type);
	cons = queue_get_consumer(q, type);

	while ((prod - cons) & q->index_mask) {
		memcpy(queue_addr_from_index(new_q, new_prod),
		       queue_addr_from_index(q, cons), new_q->elem_size);
		new_prod = queue_next_index(new_q, new_prod);
		cons = queue_next_index(q, cons);
	}

	new_q->buf->producer_index = new_prod;
	q->buf->consumer_index = cons;

	/* update private index copies */
	if (type == QUEUE_TYPE_TO_CLIENT)
		new_q->index = new_q->buf->producer_index;
	else
		q->index = q->buf->consumer_index;

	/* exchange rxe_queue headers */
	swap(*q, *new_q);

	return 0;
}

int rxe_queue_resize(struct rxe_queue *q, unsigned int *num_elem_p,
		     unsigned int elem_size, struct ib_udata *udata,
		     struct mminfo __user *outbuf, spinlock_t *producer_lock,
		     spinlock_t *consumer_lock)
{
	struct rxe_queue *new_q;
	unsigned int num_elem = *num_elem_p;
	int err;
	unsigned long producer_flags;
	unsigned long consumer_flags;

	new_q = rxe_queue_init(q->rxe, &num_elem, elem_size, q->type);
	if (!new_q)
		return -ENOMEM;

	err = do_mmap_info(new_q->rxe, outbuf, udata, new_q->buf,
			   new_q->buf_size, &new_q->ip);
	if (err) {
		vfree(new_q->buf);
		kfree(new_q);
		goto err1;
	}

	spin_lock_irqsave(consumer_lock, consumer_flags);

	if (producer_lock) {
		spin_lock_irqsave(producer_lock, producer_flags);
		err = resize_finish(q, new_q, num_elem);
		spin_unlock_irqrestore(producer_lock, producer_flags);
	} else {
		err = resize_finish(q, new_q, num_elem);
	}

	spin_unlock_irqrestore(consumer_lock, consumer_flags);

	rxe_queue_cleanup(new_q);	/* new/old dep on err */
	if (err)
		goto err1;

	*num_elem_p = num_elem;
	return 0;

err1:
	return err;
}

void rxe_queue_cleanup(struct rxe_queue *q)
{
	if (q->ip)
		kref_put(&q->ip->ref, rxe_mmap_release);
	else
		vfree(q->buf);

	kfree(q);
}
