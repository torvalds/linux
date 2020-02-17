/*
 * ISHTP Ring Buffers
 *
 * Copyright (c) 2003-2016, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 */

#include <linux/slab.h>
#include "client.h"

/**
 * ishtp_cl_alloc_rx_ring() - Allocate RX ring buffers
 * @cl: client device instance
 *
 * Allocate and initialize RX ring buffers
 *
 * Return: 0 on success else -ENOMEM
 */
int ishtp_cl_alloc_rx_ring(struct ishtp_cl *cl)
{
	size_t	len = cl->device->fw_client->props.max_msg_length;
	int	j;
	struct ishtp_cl_rb *rb;
	int	ret = 0;
	unsigned long	flags;

	for (j = 0; j < cl->rx_ring_size; ++j) {
		rb = ishtp_io_rb_init(cl);
		if (!rb) {
			ret = -ENOMEM;
			goto out;
		}
		ret = ishtp_io_rb_alloc_buf(rb, len);
		if (ret)
			goto out;
		spin_lock_irqsave(&cl->free_list_spinlock, flags);
		list_add_tail(&rb->list, &cl->free_rb_list.list);
		spin_unlock_irqrestore(&cl->free_list_spinlock, flags);
	}

	return	0;

out:
	dev_err(&cl->device->dev, "error in allocating Rx buffers\n");
	ishtp_cl_free_rx_ring(cl);
	return	ret;
}

/**
 * ishtp_cl_alloc_tx_ring() - Allocate TX ring buffers
 * @cl: client device instance
 *
 * Allocate and initialize TX ring buffers
 *
 * Return: 0 on success else -ENOMEM
 */
int ishtp_cl_alloc_tx_ring(struct ishtp_cl *cl)
{
	size_t	len = cl->device->fw_client->props.max_msg_length;
	int	j;
	unsigned long	flags;

	/* Allocate pool to free Tx bufs */
	for (j = 0; j < cl->tx_ring_size; ++j) {
		struct ishtp_cl_tx_ring	*tx_buf;

		tx_buf = kzalloc(sizeof(struct ishtp_cl_tx_ring), GFP_KERNEL);
		if (!tx_buf)
			goto	out;

		tx_buf->send_buf.data = kmalloc(len, GFP_KERNEL);
		if (!tx_buf->send_buf.data) {
			kfree(tx_buf);
			goto	out;
		}

		spin_lock_irqsave(&cl->tx_free_list_spinlock, flags);
		list_add_tail(&tx_buf->list, &cl->tx_free_list.list);
		spin_unlock_irqrestore(&cl->tx_free_list_spinlock, flags);
	}
	return	0;
out:
	dev_err(&cl->device->dev, "error in allocating Tx pool\n");
	ishtp_cl_free_tx_ring(cl);
	return	-ENOMEM;
}

/**
 * ishtp_cl_free_rx_ring() - Free RX ring buffers
 * @cl: client device instance
 *
 * Free RX ring buffers
 */
void ishtp_cl_free_rx_ring(struct ishtp_cl *cl)
{
	struct ishtp_cl_rb *rb;
	unsigned long	flags;

	/* release allocated memory - pass over free_rb_list */
	spin_lock_irqsave(&cl->free_list_spinlock, flags);
	while (!list_empty(&cl->free_rb_list.list)) {
		rb = list_entry(cl->free_rb_list.list.next, struct ishtp_cl_rb,
				list);
		list_del(&rb->list);
		kfree(rb->buffer.data);
		kfree(rb);
	}
	spin_unlock_irqrestore(&cl->free_list_spinlock, flags);
	/* release allocated memory - pass over in_process_list */
	spin_lock_irqsave(&cl->in_process_spinlock, flags);
	while (!list_empty(&cl->in_process_list.list)) {
		rb = list_entry(cl->in_process_list.list.next,
				struct ishtp_cl_rb, list);
		list_del(&rb->list);
		kfree(rb->buffer.data);
		kfree(rb);
	}
	spin_unlock_irqrestore(&cl->in_process_spinlock, flags);
}

/**
 * ishtp_cl_free_tx_ring() - Free TX ring buffers
 * @cl: client device instance
 *
 * Free TX ring buffers
 */
void ishtp_cl_free_tx_ring(struct ishtp_cl *cl)
{
	struct ishtp_cl_tx_ring	*tx_buf;
	unsigned long	flags;

	spin_lock_irqsave(&cl->tx_free_list_spinlock, flags);
	/* release allocated memory - pass over tx_free_list */
	while (!list_empty(&cl->tx_free_list.list)) {
		tx_buf = list_entry(cl->tx_free_list.list.next,
				    struct ishtp_cl_tx_ring, list);
		list_del(&tx_buf->list);
		kfree(tx_buf->send_buf.data);
		kfree(tx_buf);
	}
	spin_unlock_irqrestore(&cl->tx_free_list_spinlock, flags);

	spin_lock_irqsave(&cl->tx_list_spinlock, flags);
	/* release allocated memory - pass over tx_list */
	while (!list_empty(&cl->tx_list.list)) {
		tx_buf = list_entry(cl->tx_list.list.next,
				    struct ishtp_cl_tx_ring, list);
		list_del(&tx_buf->list);
		kfree(tx_buf->send_buf.data);
		kfree(tx_buf);
	}
	spin_unlock_irqrestore(&cl->tx_list_spinlock, flags);
}

/**
 * ishtp_io_rb_free() - Free IO request block
 * @rb: IO request block
 *
 * Free io request block memory
 */
void ishtp_io_rb_free(struct ishtp_cl_rb *rb)
{
	if (rb == NULL)
		return;

	kfree(rb->buffer.data);
	kfree(rb);
}

/**
 * ishtp_io_rb_init() - Allocate and init IO request block
 * @cl: client device instance
 *
 * Allocate and initialize request block
 *
 * Return: Allocted IO request block pointer
 */
struct ishtp_cl_rb *ishtp_io_rb_init(struct ishtp_cl *cl)
{
	struct ishtp_cl_rb *rb;

	rb = kzalloc(sizeof(struct ishtp_cl_rb), GFP_KERNEL);
	if (!rb)
		return NULL;

	INIT_LIST_HEAD(&rb->list);
	rb->cl = cl;
	rb->buf_idx = 0;
	return rb;
}

/**
 * ishtp_io_rb_alloc_buf() - Allocate and init response buffer
 * @rb: IO request block
 * @length: length of response buffer
 *
 * Allocate respose buffer
 *
 * Return: 0 on success else -ENOMEM
 */
int ishtp_io_rb_alloc_buf(struct ishtp_cl_rb *rb, size_t length)
{
	if (!rb)
		return -EINVAL;

	if (length == 0)
		return 0;

	rb->buffer.data = kmalloc(length, GFP_KERNEL);
	if (!rb->buffer.data)
		return -ENOMEM;

	rb->buffer.size = length;
	return 0;
}

/**
 * ishtp_cl_io_rb_recycle() - Recycle IO request blocks
 * @rb: IO request block
 *
 * Re-append rb to its client's free list and send flow control if needed
 *
 * Return: 0 on success else -EFAULT
 */
int ishtp_cl_io_rb_recycle(struct ishtp_cl_rb *rb)
{
	struct ishtp_cl *cl;
	int	rets = 0;
	unsigned long	flags;

	if (!rb || !rb->cl)
		return	-EFAULT;

	cl = rb->cl;
	spin_lock_irqsave(&cl->free_list_spinlock, flags);
	list_add_tail(&rb->list, &cl->free_rb_list.list);
	spin_unlock_irqrestore(&cl->free_list_spinlock, flags);

	/*
	 * If we returned the first buffer to empty 'free' list,
	 * send flow control
	 */
	if (!cl->out_flow_ctrl_creds)
		rets = ishtp_cl_read_start(cl);

	return	rets;
}
EXPORT_SYMBOL(ishtp_cl_io_rb_recycle);
