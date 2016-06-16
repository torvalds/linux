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
 *	- Redistributions in binary form must retailuce the above
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

#include <linux/vmalloc.h>
#include "rxe.h"
#include "rxe_loc.h"
#include "rxe_queue.h"

int do_mmap_info(struct rxe_dev *rxe,
		 struct ib_udata *udata,
		 bool is_req,
		 struct ib_ucontext *context,
		 struct rxe_queue_buf *buf,
		 size_t buf_size,
		 struct rxe_mmap_info **ip_p)
{
	int err;
	u32 len, offset;
	struct rxe_mmap_info *ip = NULL;

	if (udata) {
		if (is_req) {
			len = udata->outlen - sizeof(struct mminfo);
			offset = sizeof(struct mminfo);
		} else {
			len = udata->outlen;
			offset = 0;
		}

		if (len < sizeof(ip->info))
			goto err1;

		ip = rxe_create_mmap_info(rxe, buf_size, context, buf);
		if (!ip)
			goto err1;

		err = copy_to_user(udata->outbuf + offset, &ip->info,
				   sizeof(ip->info));
		if (err)
			goto err2;

		spin_lock_bh(&rxe->pending_lock);
		list_add(&ip->pending_mmaps, &rxe->pending_mmaps);
		spin_unlock_bh(&rxe->pending_lock);
	}

	*ip_p = ip;

	return 0;

err2:
	kfree(ip);
err1:
	return -EINVAL;
}

struct rxe_queue *rxe_queue_init(struct rxe_dev *rxe,
				 int *num_elem,
				 unsigned int elem_size)
{
	struct rxe_queue *q;
	size_t buf_size;
	unsigned int num_slots;

	/* num_elem == 0 is allowed, but uninteresting */
	if (*num_elem < 0)
		goto err1;

	q = kmalloc(sizeof(*q), GFP_KERNEL);
	if (!q)
		goto err1;

	q->rxe = rxe;

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
	if (!queue_empty(q) && (num_elem < queue_count(q)))
		return -EINVAL;

	while (!queue_empty(q)) {
		memcpy(producer_addr(new_q), consumer_addr(q),
		       new_q->elem_size);
		advance_producer(new_q);
		advance_consumer(q);
	}

	swap(*q, *new_q);

	return 0;
}

int rxe_queue_resize(struct rxe_queue *q,
		     unsigned int *num_elem_p,
		     unsigned int elem_size,
		     struct ib_ucontext *context,
		     struct ib_udata *udata,
		     spinlock_t *producer_lock,
		     spinlock_t *consumer_lock)
{
	struct rxe_queue *new_q;
	unsigned int num_elem = *num_elem_p;
	int err;
	unsigned long flags = 0, flags1;

	new_q = rxe_queue_init(q->rxe, &num_elem, elem_size);
	if (!new_q)
		return -ENOMEM;

	err = do_mmap_info(new_q->rxe, udata, false, context, new_q->buf,
			   new_q->buf_size, &new_q->ip);
	if (err) {
		vfree(new_q->buf);
		kfree(new_q);
		goto err1;
	}

	spin_lock_irqsave(consumer_lock, flags1);

	if (producer_lock) {
		spin_lock_irqsave(producer_lock, flags);
		err = resize_finish(q, new_q, num_elem);
		spin_unlock_irqrestore(producer_lock, flags);
	} else {
		err = resize_finish(q, new_q, num_elem);
	}

	spin_unlock_irqrestore(consumer_lock, flags1);

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
