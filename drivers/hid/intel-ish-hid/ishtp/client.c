// SPDX-License-Identifier: GPL-2.0-only
/*
 * ISHTP client logic
 *
 * Copyright (c) 2003-2016, Intel Corporation.
 */

#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include "hbm.h"
#include "client.h"

int ishtp_cl_get_tx_free_buffer_size(struct ishtp_cl *cl)
{
	unsigned long tx_free_flags;
	int size;

	spin_lock_irqsave(&cl->tx_free_list_spinlock, tx_free_flags);
	size = cl->tx_ring_free_size * cl->device->fw_client->props.max_msg_length;
	spin_unlock_irqrestore(&cl->tx_free_list_spinlock, tx_free_flags);

	return size;
}
EXPORT_SYMBOL(ishtp_cl_get_tx_free_buffer_size);

int ishtp_cl_get_tx_free_rings(struct ishtp_cl *cl)
{
	return cl->tx_ring_free_size;
}
EXPORT_SYMBOL(ishtp_cl_get_tx_free_rings);

/**
 * ishtp_read_list_flush() - Flush read queue
 * @cl: ishtp client instance
 *
 * Used to remove all entries from read queue for a client
 */
static void ishtp_read_list_flush(struct ishtp_cl *cl)
{
	struct ishtp_cl_rb *rb;
	struct ishtp_cl_rb *next;
	unsigned long	flags;

	spin_lock_irqsave(&cl->dev->read_list_spinlock, flags);
	list_for_each_entry_safe(rb, next, &cl->dev->read_list.list, list)
		if (rb->cl && ishtp_cl_cmp_id(cl, rb->cl)) {
			list_del(&rb->list);
			ishtp_io_rb_free(rb);
		}
	spin_unlock_irqrestore(&cl->dev->read_list_spinlock, flags);
}

/**
 * ishtp_cl_flush_queues() - Flush all queues for a client
 * @cl: ishtp client instance
 *
 * Used to remove all queues for a client. This is called when a client device
 * needs reset due to error, S3 resume or during module removal
 *
 * Return: 0 on success else -EINVAL if device is NULL
 */
int ishtp_cl_flush_queues(struct ishtp_cl *cl)
{
	if (WARN_ON(!cl || !cl->dev))
		return -EINVAL;

	ishtp_read_list_flush(cl);

	return 0;
}
EXPORT_SYMBOL(ishtp_cl_flush_queues);

/**
 * ishtp_cl_init() - Initialize all fields of a client device
 * @cl: ishtp client instance
 * @dev: ishtp device
 *
 * Initializes a client device fields: Init spinlocks, init queues etc.
 * This function is called during new client creation
 */
static void ishtp_cl_init(struct ishtp_cl *cl, struct ishtp_device *dev)
{
	memset(cl, 0, sizeof(struct ishtp_cl));
	init_waitqueue_head(&cl->wait_ctrl_res);
	spin_lock_init(&cl->free_list_spinlock);
	spin_lock_init(&cl->in_process_spinlock);
	spin_lock_init(&cl->tx_list_spinlock);
	spin_lock_init(&cl->tx_free_list_spinlock);
	spin_lock_init(&cl->fc_spinlock);
	INIT_LIST_HEAD(&cl->link);
	cl->dev = dev;

	INIT_LIST_HEAD(&cl->free_rb_list.list);
	INIT_LIST_HEAD(&cl->tx_list.list);
	INIT_LIST_HEAD(&cl->tx_free_list.list);
	INIT_LIST_HEAD(&cl->in_process_list.list);

	cl->rx_ring_size = CL_DEF_RX_RING_SIZE;
	cl->tx_ring_size = CL_DEF_TX_RING_SIZE;
	cl->tx_ring_free_size = cl->tx_ring_size;

	/* dma */
	cl->last_tx_path = CL_TX_PATH_IPC;
	cl->last_dma_acked = 1;
	cl->last_dma_addr = NULL;
	cl->last_ipc_acked = 1;
}

/**
 * ishtp_cl_allocate() - allocates client structure and sets it up.
 * @dev: ishtp device
 *
 * Allocate memory for new client device and call to initialize each field.
 *
 * Return: The allocated client instance or NULL on failure
 */
struct ishtp_cl *ishtp_cl_allocate(struct ishtp_cl_device *cl_device)
{
	struct ishtp_cl *cl;

	cl = kmalloc(sizeof(struct ishtp_cl), GFP_KERNEL);
	if (!cl)
		return NULL;

	ishtp_cl_init(cl, cl_device->ishtp_dev);
	return cl;
}
EXPORT_SYMBOL(ishtp_cl_allocate);

/**
 * ishtp_cl_free() - Frees a client device
 * @cl: client device instance
 *
 * Frees a client device
 */
void	ishtp_cl_free(struct ishtp_cl *cl)
{
	struct ishtp_device *dev;
	unsigned long flags;

	if (!cl)
		return;

	dev = cl->dev;
	if (!dev)
		return;

	spin_lock_irqsave(&dev->cl_list_lock, flags);
	ishtp_cl_free_rx_ring(cl);
	ishtp_cl_free_tx_ring(cl);
	kfree(cl);
	spin_unlock_irqrestore(&dev->cl_list_lock, flags);
}
EXPORT_SYMBOL(ishtp_cl_free);

/**
 * ishtp_cl_link() - Reserve a host id and link the client instance
 * @cl: client device instance
 *
 * This allocates a single bit in the hostmap. This function will make sure
 * that not many client sessions are opened at the same time. Once allocated
 * the client device instance is added to the ishtp device in the current
 * client list
 *
 * Return: 0 or error code on failure
 */
int ishtp_cl_link(struct ishtp_cl *cl)
{
	struct ishtp_device *dev;
	unsigned long flags, flags_cl;
	int id, ret = 0;

	if (WARN_ON(!cl || !cl->dev))
		return -EINVAL;

	dev = cl->dev;

	spin_lock_irqsave(&dev->device_lock, flags);

	if (dev->open_handle_count >= ISHTP_MAX_OPEN_HANDLE_COUNT) {
		ret = -EMFILE;
		goto unlock_dev;
	}

	id = find_first_zero_bit(dev->host_clients_map, ISHTP_CLIENTS_MAX);

	if (id >= ISHTP_CLIENTS_MAX) {
		spin_unlock_irqrestore(&dev->device_lock, flags);
		dev_err(&cl->device->dev, "id exceeded %d", ISHTP_CLIENTS_MAX);
		return -ENOENT;
	}

	dev->open_handle_count++;
	cl->host_client_id = id;
	spin_lock_irqsave(&dev->cl_list_lock, flags_cl);
	if (dev->dev_state != ISHTP_DEV_ENABLED) {
		ret = -ENODEV;
		goto unlock_cl;
	}
	list_add_tail(&cl->link, &dev->cl_list);
	set_bit(id, dev->host_clients_map);
	cl->state = ISHTP_CL_INITIALIZING;

unlock_cl:
	spin_unlock_irqrestore(&dev->cl_list_lock, flags_cl);
unlock_dev:
	spin_unlock_irqrestore(&dev->device_lock, flags);
	return ret;
}
EXPORT_SYMBOL(ishtp_cl_link);

/**
 * ishtp_cl_unlink() - remove fw_cl from the client device list
 * @cl: client device instance
 *
 * Remove a previously linked device to a ishtp device
 */
void ishtp_cl_unlink(struct ishtp_cl *cl)
{
	struct ishtp_device *dev;
	struct ishtp_cl *pos;
	unsigned long	flags;

	/* don't shout on error exit path */
	if (!cl || !cl->dev)
		return;

	dev = cl->dev;

	spin_lock_irqsave(&dev->device_lock, flags);
	if (dev->open_handle_count > 0) {
		clear_bit(cl->host_client_id, dev->host_clients_map);
		dev->open_handle_count--;
	}
	spin_unlock_irqrestore(&dev->device_lock, flags);

	/*
	 * This checks that 'cl' is actually linked into device's structure,
	 * before attempting 'list_del'
	 */
	spin_lock_irqsave(&dev->cl_list_lock, flags);
	list_for_each_entry(pos, &dev->cl_list, link)
		if (cl->host_client_id == pos->host_client_id) {
			list_del_init(&pos->link);
			break;
		}
	spin_unlock_irqrestore(&dev->cl_list_lock, flags);
}
EXPORT_SYMBOL(ishtp_cl_unlink);

/**
 * ishtp_cl_disconnect() - Send disconnect request to firmware
 * @cl: client device instance
 *
 * Send a disconnect request for a client to firmware.
 *
 * Return: 0 if successful disconnect response from the firmware or error
 * code on failure
 */
int ishtp_cl_disconnect(struct ishtp_cl *cl)
{
	struct ishtp_device *dev;
	int err;

	if (WARN_ON(!cl || !cl->dev))
		return -ENODEV;

	dev = cl->dev;

	dev->print_log(dev, "%s() state %d\n", __func__, cl->state);

	if (cl->state != ISHTP_CL_DISCONNECTING) {
		dev->print_log(dev, "%s() Disconnect in progress\n", __func__);
		return 0;
	}

	if (ishtp_hbm_cl_disconnect_req(dev, cl)) {
		dev->print_log(dev, "%s() Failed to disconnect\n", __func__);
		dev_err(&cl->device->dev, "failed to disconnect.\n");
		return -ENODEV;
	}

	err = wait_event_interruptible_timeout(cl->wait_ctrl_res,
			(dev->dev_state != ISHTP_DEV_ENABLED ||
			cl->state == ISHTP_CL_DISCONNECTED),
			ishtp_secs_to_jiffies(ISHTP_CL_CONNECT_TIMEOUT));

	/*
	 * If FW reset arrived, this will happen. Don't check cl->,
	 * as 'cl' may be freed already
	 */
	if (dev->dev_state != ISHTP_DEV_ENABLED) {
		dev->print_log(dev, "%s() dev_state != ISHTP_DEV_ENABLED\n",
			       __func__);
		return -ENODEV;
	}

	if (cl->state == ISHTP_CL_DISCONNECTED) {
		dev->print_log(dev, "%s() successful\n", __func__);
		return 0;
	}

	return -ENODEV;
}
EXPORT_SYMBOL(ishtp_cl_disconnect);

/**
 * ishtp_cl_is_other_connecting() - Check other client is connecting
 * @cl: client device instance
 *
 * Checks if other client with the same fw client id is connecting
 *
 * Return: true if other client is connected else false
 */
static bool ishtp_cl_is_other_connecting(struct ishtp_cl *cl)
{
	struct ishtp_device *dev;
	struct ishtp_cl *pos;
	unsigned long	flags;

	if (WARN_ON(!cl || !cl->dev))
		return false;

	dev = cl->dev;
	spin_lock_irqsave(&dev->cl_list_lock, flags);
	list_for_each_entry(pos, &dev->cl_list, link) {
		if ((pos->state == ISHTP_CL_CONNECTING) && (pos != cl) &&
				cl->fw_client_id == pos->fw_client_id) {
			spin_unlock_irqrestore(&dev->cl_list_lock, flags);
			return true;
		}
	}
	spin_unlock_irqrestore(&dev->cl_list_lock, flags);

	return false;
}

/**
 * ishtp_cl_connect() - Send connect request to firmware
 * @cl: client device instance
 *
 * Send a connect request for a client to firmware. If successful it will
 * RX and TX ring buffers
 *
 * Return: 0 if successful connect response from the firmware and able
 * to bind and allocate ring buffers or error code on failure
 */
int ishtp_cl_connect(struct ishtp_cl *cl)
{
	struct ishtp_device *dev;
	int rets;

	if (WARN_ON(!cl || !cl->dev))
		return -ENODEV;

	dev = cl->dev;

	dev->print_log(dev, "%s() current_state = %d\n", __func__, cl->state);

	if (ishtp_cl_is_other_connecting(cl)) {
		dev->print_log(dev, "%s() Busy\n", __func__);
		return	-EBUSY;
	}

	if (ishtp_hbm_cl_connect_req(dev, cl)) {
		dev->print_log(dev, "%s() HBM connect req fail\n", __func__);
		return -ENODEV;
	}

	rets = wait_event_interruptible_timeout(cl->wait_ctrl_res,
				(dev->dev_state == ISHTP_DEV_ENABLED &&
				(cl->state == ISHTP_CL_CONNECTED ||
				 cl->state == ISHTP_CL_DISCONNECTED)),
				ishtp_secs_to_jiffies(
					ISHTP_CL_CONNECT_TIMEOUT));
	/*
	 * If FW reset arrived, this will happen. Don't check cl->,
	 * as 'cl' may be freed already
	 */
	if (dev->dev_state != ISHTP_DEV_ENABLED) {
		dev->print_log(dev, "%s() dev_state != ISHTP_DEV_ENABLED\n",
			       __func__);
		return -EFAULT;
	}

	if (cl->state != ISHTP_CL_CONNECTED) {
		dev->print_log(dev, "%s() state != ISHTP_CL_CONNECTED\n",
			       __func__);
		return -EFAULT;
	}

	rets = cl->status;
	if (rets) {
		dev->print_log(dev, "%s() Invalid status\n", __func__);
		return rets;
	}

	rets = ishtp_cl_device_bind(cl);
	if (rets) {
		dev->print_log(dev, "%s() Bind error\n", __func__);
		ishtp_cl_disconnect(cl);
		return rets;
	}

	rets = ishtp_cl_alloc_rx_ring(cl);
	if (rets) {
		dev->print_log(dev, "%s() Alloc RX ring failed\n", __func__);
		/* if failed allocation, disconnect */
		ishtp_cl_disconnect(cl);
		return rets;
	}

	rets = ishtp_cl_alloc_tx_ring(cl);
	if (rets) {
		dev->print_log(dev, "%s() Alloc TX ring failed\n", __func__);
		/* if failed allocation, disconnect */
		ishtp_cl_free_rx_ring(cl);
		ishtp_cl_disconnect(cl);
		return rets;
	}

	/* Upon successful connection and allocation, emit flow-control */
	rets = ishtp_cl_read_start(cl);

	dev->print_log(dev, "%s() successful\n", __func__);

	return rets;
}
EXPORT_SYMBOL(ishtp_cl_connect);

/**
 * ishtp_cl_read_start() - Prepare to read client message
 * @cl: client device instance
 *
 * Get a free buffer from pool of free read buffers and add to read buffer
 * pool to add contents. Send a flow control request to firmware to be able
 * send next message.
 *
 * Return: 0 if successful or error code on failure
 */
int ishtp_cl_read_start(struct ishtp_cl *cl)
{
	struct ishtp_device *dev;
	struct ishtp_cl_rb *rb;
	int rets;
	int i;
	unsigned long	flags;
	unsigned long	dev_flags;

	if (WARN_ON(!cl || !cl->dev))
		return -ENODEV;

	dev = cl->dev;

	if (cl->state != ISHTP_CL_CONNECTED)
		return -ENODEV;

	if (dev->dev_state != ISHTP_DEV_ENABLED)
		return -ENODEV;

	i = ishtp_fw_cl_by_id(dev, cl->fw_client_id);
	if (i < 0) {
		dev_err(&cl->device->dev, "no such fw client %d\n",
			cl->fw_client_id);
		return -ENODEV;
	}

	/* The current rb is the head of the free rb list */
	spin_lock_irqsave(&cl->free_list_spinlock, flags);
	if (list_empty(&cl->free_rb_list.list)) {
		dev_warn(&cl->device->dev,
			 "[ishtp-ish] Rx buffers pool is empty\n");
		rets = -ENOMEM;
		rb = NULL;
		spin_unlock_irqrestore(&cl->free_list_spinlock, flags);
		goto out;
	}
	rb = list_entry(cl->free_rb_list.list.next, struct ishtp_cl_rb, list);
	list_del_init(&rb->list);
	spin_unlock_irqrestore(&cl->free_list_spinlock, flags);

	rb->cl = cl;
	rb->buf_idx = 0;

	INIT_LIST_HEAD(&rb->list);
	rets = 0;

	/*
	 * This must be BEFORE sending flow control -
	 * response in ISR may come too fast...
	 */
	spin_lock_irqsave(&dev->read_list_spinlock, dev_flags);
	list_add_tail(&rb->list, &dev->read_list.list);
	spin_unlock_irqrestore(&dev->read_list_spinlock, dev_flags);
	if (ishtp_hbm_cl_flow_control_req(dev, cl)) {
		rets = -ENODEV;
		goto out;
	}
out:
	/* if ishtp_hbm_cl_flow_control_req failed, return rb to free list */
	if (rets && rb) {
		spin_lock_irqsave(&dev->read_list_spinlock, dev_flags);
		list_del(&rb->list);
		spin_unlock_irqrestore(&dev->read_list_spinlock, dev_flags);

		spin_lock_irqsave(&cl->free_list_spinlock, flags);
		list_add_tail(&rb->list, &cl->free_rb_list.list);
		spin_unlock_irqrestore(&cl->free_list_spinlock, flags);
	}
	return rets;
}

/**
 * ishtp_cl_send() - Send a message to firmware
 * @cl: client device instance
 * @buf: message buffer
 * @length: length of message
 *
 * If the client is correct state to send message, this function gets a buffer
 * from tx ring buffers, copy the message data and call to send the message
 * using ishtp_cl_send_msg()
 *
 * Return: 0 if successful or error code on failure
 */
int ishtp_cl_send(struct ishtp_cl *cl, uint8_t *buf, size_t length)
{
	struct ishtp_device	*dev;
	int	id;
	struct ishtp_cl_tx_ring	*cl_msg;
	int	have_msg_to_send = 0;
	unsigned long	tx_flags, tx_free_flags;

	if (WARN_ON(!cl || !cl->dev))
		return -ENODEV;

	dev = cl->dev;

	if (cl->state != ISHTP_CL_CONNECTED) {
		++cl->err_send_msg;
		return -EPIPE;
	}

	if (dev->dev_state != ISHTP_DEV_ENABLED) {
		++cl->err_send_msg;
		return -ENODEV;
	}

	/* Check if we have fw client device */
	id = ishtp_fw_cl_by_id(dev, cl->fw_client_id);
	if (id < 0) {
		++cl->err_send_msg;
		return -ENOENT;
	}

	if (length > dev->fw_clients[id].props.max_msg_length) {
		++cl->err_send_msg;
		return -EMSGSIZE;
	}

	/* No free bufs */
	spin_lock_irqsave(&cl->tx_free_list_spinlock, tx_free_flags);
	if (list_empty(&cl->tx_free_list.list)) {
		spin_unlock_irqrestore(&cl->tx_free_list_spinlock,
			tx_free_flags);
		++cl->err_send_msg;
		return	-ENOMEM;
	}

	cl_msg = list_first_entry(&cl->tx_free_list.list,
		struct ishtp_cl_tx_ring, list);
	if (!cl_msg->send_buf.data) {
		spin_unlock_irqrestore(&cl->tx_free_list_spinlock,
			tx_free_flags);
		return	-EIO;
		/* Should not happen, as free list is pre-allocated */
	}
	/*
	 * This is safe, as 'length' is already checked for not exceeding
	 * max ISHTP message size per client
	 */
	list_del_init(&cl_msg->list);
	--cl->tx_ring_free_size;

	spin_unlock_irqrestore(&cl->tx_free_list_spinlock, tx_free_flags);
	memcpy(cl_msg->send_buf.data, buf, length);
	cl_msg->send_buf.size = length;
	spin_lock_irqsave(&cl->tx_list_spinlock, tx_flags);
	have_msg_to_send = !list_empty(&cl->tx_list.list);
	list_add_tail(&cl_msg->list, &cl->tx_list.list);
	spin_unlock_irqrestore(&cl->tx_list_spinlock, tx_flags);

	if (!have_msg_to_send && cl->ishtp_flow_ctrl_creds > 0)
		ishtp_cl_send_msg(dev, cl);

	return	0;
}
EXPORT_SYMBOL(ishtp_cl_send);

/**
 * ishtp_cl_read_complete() - read complete
 * @rb: Pointer to client request block
 *
 * If the message is completely received call ishtp_cl_bus_rx_event()
 * to process message
 */
static void ishtp_cl_read_complete(struct ishtp_cl_rb *rb)
{
	unsigned long	flags;
	int	schedule_work_flag = 0;
	struct ishtp_cl	*cl = rb->cl;

	spin_lock_irqsave(&cl->in_process_spinlock, flags);
	/*
	 * if in-process list is empty, then need to schedule
	 * the processing thread
	 */
	schedule_work_flag = list_empty(&cl->in_process_list.list);
	list_add_tail(&rb->list, &cl->in_process_list.list);
	spin_unlock_irqrestore(&cl->in_process_spinlock, flags);

	if (schedule_work_flag)
		ishtp_cl_bus_rx_event(cl->device);
}

/**
 * ipc_tx_callback() - IPC tx callback function
 * @prm: Pointer to client device instance
 *
 * Send message over IPC either first time or on callback on previous message
 * completion
 */
static void ipc_tx_callback(void *prm)
{
	struct ishtp_cl	*cl = prm;
	struct ishtp_cl_tx_ring	*cl_msg;
	size_t	rem;
	struct ishtp_device	*dev = (cl ? cl->dev : NULL);
	struct ishtp_msg_hdr	ishtp_hdr;
	unsigned long	tx_flags, tx_free_flags;
	unsigned char	*pmsg;

	if (!dev)
		return;

	/*
	 * Other conditions if some critical error has
	 * occurred before this callback is called
	 */
	if (dev->dev_state != ISHTP_DEV_ENABLED)
		return;

	if (cl->state != ISHTP_CL_CONNECTED)
		return;

	spin_lock_irqsave(&cl->tx_list_spinlock, tx_flags);
	if (list_empty(&cl->tx_list.list)) {
		spin_unlock_irqrestore(&cl->tx_list_spinlock, tx_flags);
		return;
	}

	if (cl->ishtp_flow_ctrl_creds != 1 && !cl->sending) {
		spin_unlock_irqrestore(&cl->tx_list_spinlock, tx_flags);
		return;
	}

	if (!cl->sending) {
		--cl->ishtp_flow_ctrl_creds;
		cl->last_ipc_acked = 0;
		cl->last_tx_path = CL_TX_PATH_IPC;
		cl->sending = 1;
	}

	cl_msg = list_entry(cl->tx_list.list.next, struct ishtp_cl_tx_ring,
			    list);
	rem = cl_msg->send_buf.size - cl->tx_offs;

	ishtp_hdr.host_addr = cl->host_client_id;
	ishtp_hdr.fw_addr = cl->fw_client_id;
	ishtp_hdr.reserved = 0;
	pmsg = cl_msg->send_buf.data + cl->tx_offs;

	if (rem <= dev->mtu) {
		ishtp_hdr.length = rem;
		ishtp_hdr.msg_complete = 1;
		cl->sending = 0;
		list_del_init(&cl_msg->list);	/* Must be before write */
		spin_unlock_irqrestore(&cl->tx_list_spinlock, tx_flags);
		/* Submit to IPC queue with no callback */
		ishtp_write_message(dev, &ishtp_hdr, pmsg);
		spin_lock_irqsave(&cl->tx_free_list_spinlock, tx_free_flags);
		list_add_tail(&cl_msg->list, &cl->tx_free_list.list);
		++cl->tx_ring_free_size;
		spin_unlock_irqrestore(&cl->tx_free_list_spinlock,
			tx_free_flags);
	} else {
		/* Send IPC fragment */
		spin_unlock_irqrestore(&cl->tx_list_spinlock, tx_flags);
		cl->tx_offs += dev->mtu;
		ishtp_hdr.length = dev->mtu;
		ishtp_hdr.msg_complete = 0;
		ishtp_send_msg(dev, &ishtp_hdr, pmsg, ipc_tx_callback, cl);
	}
}

/**
 * ishtp_cl_send_msg_ipc() -Send message using IPC
 * @dev: ISHTP device instance
 * @cl: Pointer to client device instance
 *
 * Send message over IPC not using DMA
 */
static void ishtp_cl_send_msg_ipc(struct ishtp_device *dev,
				  struct ishtp_cl *cl)
{
	/* If last DMA message wasn't acked yet, leave this one in Tx queue */
	if (cl->last_tx_path == CL_TX_PATH_DMA && cl->last_dma_acked == 0)
		return;

	cl->tx_offs = 0;
	ipc_tx_callback(cl);
	++cl->send_msg_cnt_ipc;
}

/**
 * ishtp_cl_send_msg_dma() -Send message using DMA
 * @dev: ISHTP device instance
 * @cl: Pointer to client device instance
 *
 * Send message using DMA
 */
static void ishtp_cl_send_msg_dma(struct ishtp_device *dev,
	struct ishtp_cl *cl)
{
	struct ishtp_msg_hdr	hdr;
	struct dma_xfer_hbm	dma_xfer;
	unsigned char	*msg_addr;
	int off;
	struct ishtp_cl_tx_ring	*cl_msg;
	unsigned long tx_flags, tx_free_flags;

	/* If last IPC message wasn't acked yet, leave this one in Tx queue */
	if (cl->last_tx_path == CL_TX_PATH_IPC && cl->last_ipc_acked == 0)
		return;

	spin_lock_irqsave(&cl->tx_list_spinlock, tx_flags);
	if (list_empty(&cl->tx_list.list)) {
		spin_unlock_irqrestore(&cl->tx_list_spinlock, tx_flags);
		return;
	}

	cl_msg = list_entry(cl->tx_list.list.next, struct ishtp_cl_tx_ring,
		list);

	msg_addr = ishtp_cl_get_dma_send_buf(dev, cl_msg->send_buf.size);
	if (!msg_addr) {
		spin_unlock_irqrestore(&cl->tx_list_spinlock, tx_flags);
		if (dev->transfer_path == CL_TX_PATH_DEFAULT)
			ishtp_cl_send_msg_ipc(dev, cl);
		return;
	}

	list_del_init(&cl_msg->list);	/* Must be before write */
	spin_unlock_irqrestore(&cl->tx_list_spinlock, tx_flags);

	--cl->ishtp_flow_ctrl_creds;
	cl->last_dma_acked = 0;
	cl->last_dma_addr = msg_addr;
	cl->last_tx_path = CL_TX_PATH_DMA;

	/* write msg to dma buf */
	memcpy(msg_addr, cl_msg->send_buf.data, cl_msg->send_buf.size);

	/* send dma_xfer hbm msg */
	off = msg_addr - (unsigned char *)dev->ishtp_host_dma_tx_buf;
	ishtp_hbm_hdr(&hdr, sizeof(struct dma_xfer_hbm));
	dma_xfer.hbm = DMA_XFER;
	dma_xfer.fw_client_id = cl->fw_client_id;
	dma_xfer.host_client_id = cl->host_client_id;
	dma_xfer.reserved = 0;
	dma_xfer.msg_addr = dev->ishtp_host_dma_tx_buf_phys + off;
	dma_xfer.msg_length = cl_msg->send_buf.size;
	dma_xfer.reserved2 = 0;
	ishtp_write_message(dev, &hdr, (unsigned char *)&dma_xfer);
	spin_lock_irqsave(&cl->tx_free_list_spinlock, tx_free_flags);
	list_add_tail(&cl_msg->list, &cl->tx_free_list.list);
	++cl->tx_ring_free_size;
	spin_unlock_irqrestore(&cl->tx_free_list_spinlock, tx_free_flags);
	++cl->send_msg_cnt_dma;
}

/**
 * ishtp_cl_send_msg() -Send message using DMA or IPC
 * @dev: ISHTP device instance
 * @cl: Pointer to client device instance
 *
 * Send message using DMA or IPC based on transfer_path
 */
void ishtp_cl_send_msg(struct ishtp_device *dev, struct ishtp_cl *cl)
{
	if (dev->transfer_path == CL_TX_PATH_DMA)
		ishtp_cl_send_msg_dma(dev, cl);
	else
		ishtp_cl_send_msg_ipc(dev, cl);
}

/**
 * recv_ishtp_cl_msg() -Receive client message
 * @dev: ISHTP device instance
 * @ishtp_hdr: Pointer to message header
 *
 * Receive and dispatch ISHTP client messages. This function executes in ISR
 * or work queue context
 */
void recv_ishtp_cl_msg(struct ishtp_device *dev,
		       struct ishtp_msg_hdr *ishtp_hdr)
{
	struct ishtp_cl *cl;
	struct ishtp_cl_rb *rb;
	struct ishtp_cl_rb *new_rb;
	unsigned char *buffer = NULL;
	struct ishtp_cl_rb *complete_rb = NULL;
	unsigned long	flags;
	int	rb_count;

	if (ishtp_hdr->reserved) {
		dev_err(dev->devc, "corrupted message header.\n");
		goto	eoi;
	}

	if (ishtp_hdr->length > IPC_PAYLOAD_SIZE) {
		dev_err(dev->devc,
			"ISHTP message length in hdr exceeds IPC MTU\n");
		goto	eoi;
	}

	spin_lock_irqsave(&dev->read_list_spinlock, flags);
	rb_count = -1;
	list_for_each_entry(rb, &dev->read_list.list, list) {
		++rb_count;
		cl = rb->cl;
		if (!cl || !(cl->host_client_id == ishtp_hdr->host_addr &&
				cl->fw_client_id == ishtp_hdr->fw_addr) ||
				!(cl->state == ISHTP_CL_CONNECTED))
			continue;

		 /* If no Rx buffer is allocated, disband the rb */
		if (rb->buffer.size == 0 || rb->buffer.data == NULL) {
			spin_unlock_irqrestore(&dev->read_list_spinlock, flags);
			dev_err(&cl->device->dev,
				"Rx buffer is not allocated.\n");
			list_del(&rb->list);
			ishtp_io_rb_free(rb);
			cl->status = -ENOMEM;
			goto	eoi;
		}

		/*
		 * If message buffer overflown (exceeds max. client msg
		 * size, drop message and return to free buffer.
		 * Do we need to disconnect such a client? (We don't send
		 * back FC, so communication will be stuck anyway)
		 */
		if (rb->buffer.size < ishtp_hdr->length + rb->buf_idx) {
			spin_unlock_irqrestore(&dev->read_list_spinlock, flags);
			dev_err(&cl->device->dev,
				"message overflow. size %d len %d idx %ld\n",
				rb->buffer.size, ishtp_hdr->length,
				rb->buf_idx);
			list_del(&rb->list);
			ishtp_cl_io_rb_recycle(rb);
			cl->status = -EIO;
			goto	eoi;
		}

		buffer = rb->buffer.data + rb->buf_idx;
		dev->ops->ishtp_read(dev, buffer, ishtp_hdr->length);

		rb->buf_idx += ishtp_hdr->length;
		if (ishtp_hdr->msg_complete) {
			/* Last fragment in message - it's complete */
			cl->status = 0;
			list_del(&rb->list);
			complete_rb = rb;

			--cl->out_flow_ctrl_creds;
			/*
			 * the whole msg arrived, send a new FC, and add a new
			 * rb buffer for the next coming msg
			 */
			spin_lock(&cl->free_list_spinlock);

			if (!list_empty(&cl->free_rb_list.list)) {
				new_rb = list_entry(cl->free_rb_list.list.next,
					struct ishtp_cl_rb, list);
				list_del_init(&new_rb->list);
				spin_unlock(&cl->free_list_spinlock);
				new_rb->cl = cl;
				new_rb->buf_idx = 0;
				INIT_LIST_HEAD(&new_rb->list);
				list_add_tail(&new_rb->list,
					&dev->read_list.list);

				ishtp_hbm_cl_flow_control_req(dev, cl);
			} else {
				spin_unlock(&cl->free_list_spinlock);
			}
		}
		/* One more fragment in message (even if this was last) */
		++cl->recv_msg_num_frags;

		/*
		 * We can safely break here (and in BH too),
		 * a single input message can go only to a single request!
		 */
		break;
	}

	spin_unlock_irqrestore(&dev->read_list_spinlock, flags);
	/* If it's nobody's message, just read and discard it */
	if (!buffer) {
		uint8_t	rd_msg_buf[ISHTP_RD_MSG_BUF_SIZE];

		dev_err(dev->devc, "Dropped Rx msg - no request\n");
		dev->ops->ishtp_read(dev, rd_msg_buf, ishtp_hdr->length);
		goto	eoi;
	}

	if (complete_rb) {
		cl = complete_rb->cl;
		cl->ts_rx = ktime_get();
		++cl->recv_msg_cnt_ipc;
		ishtp_cl_read_complete(complete_rb);
	}
eoi:
	return;
}

/**
 * recv_ishtp_cl_msg_dma() -Receive client message
 * @dev: ISHTP device instance
 * @msg: message pointer
 * @hbm: hbm buffer
 *
 * Receive and dispatch ISHTP client messages using DMA. This function executes
 * in ISR or work queue context
 */
void recv_ishtp_cl_msg_dma(struct ishtp_device *dev, void *msg,
			   struct dma_xfer_hbm *hbm)
{
	struct ishtp_cl *cl;
	struct ishtp_cl_rb *rb;
	struct ishtp_cl_rb *new_rb;
	unsigned char *buffer = NULL;
	struct ishtp_cl_rb *complete_rb = NULL;
	unsigned long	flags;

	spin_lock_irqsave(&dev->read_list_spinlock, flags);

	list_for_each_entry(rb, &dev->read_list.list, list) {
		cl = rb->cl;
		if (!cl || !(cl->host_client_id == hbm->host_client_id &&
				cl->fw_client_id == hbm->fw_client_id) ||
				!(cl->state == ISHTP_CL_CONNECTED))
			continue;

		/*
		 * If no Rx buffer is allocated, disband the rb
		 */
		if (rb->buffer.size == 0 || rb->buffer.data == NULL) {
			spin_unlock_irqrestore(&dev->read_list_spinlock, flags);
			dev_err(&cl->device->dev,
				"response buffer is not allocated.\n");
			list_del(&rb->list);
			ishtp_io_rb_free(rb);
			cl->status = -ENOMEM;
			goto	eoi;
		}

		/*
		 * If message buffer overflown (exceeds max. client msg
		 * size, drop message and return to free buffer.
		 * Do we need to disconnect such a client? (We don't send
		 * back FC, so communication will be stuck anyway)
		 */
		if (rb->buffer.size < hbm->msg_length) {
			spin_unlock_irqrestore(&dev->read_list_spinlock, flags);
			dev_err(&cl->device->dev,
				"message overflow. size %d len %d idx %ld\n",
				rb->buffer.size, hbm->msg_length, rb->buf_idx);
			list_del(&rb->list);
			ishtp_cl_io_rb_recycle(rb);
			cl->status = -EIO;
			goto	eoi;
		}

		buffer = rb->buffer.data;
		memcpy(buffer, msg, hbm->msg_length);
		rb->buf_idx = hbm->msg_length;

		/* Last fragment in message - it's complete */
		cl->status = 0;
		list_del(&rb->list);
		complete_rb = rb;

		--cl->out_flow_ctrl_creds;
		/*
		 * the whole msg arrived, send a new FC, and add a new
		 * rb buffer for the next coming msg
		 */
		spin_lock(&cl->free_list_spinlock);

		if (!list_empty(&cl->free_rb_list.list)) {
			new_rb = list_entry(cl->free_rb_list.list.next,
				struct ishtp_cl_rb, list);
			list_del_init(&new_rb->list);
			spin_unlock(&cl->free_list_spinlock);
			new_rb->cl = cl;
			new_rb->buf_idx = 0;
			INIT_LIST_HEAD(&new_rb->list);
			list_add_tail(&new_rb->list,
				&dev->read_list.list);

			ishtp_hbm_cl_flow_control_req(dev, cl);
		} else {
			spin_unlock(&cl->free_list_spinlock);
		}

		/* One more fragment in message (this is always last) */
		++cl->recv_msg_num_frags;

		/*
		 * We can safely break here (and in BH too),
		 * a single input message can go only to a single request!
		 */
		break;
	}

	spin_unlock_irqrestore(&dev->read_list_spinlock, flags);
	/* If it's nobody's message, just read and discard it */
	if (!buffer) {
		dev_err(dev->devc, "Dropped Rx (DMA) msg - no request\n");
		goto	eoi;
	}

	if (complete_rb) {
		cl = complete_rb->cl;
		cl->ts_rx = ktime_get();
		++cl->recv_msg_cnt_dma;
		ishtp_cl_read_complete(complete_rb);
	}
eoi:
	return;
}

void *ishtp_get_client_data(struct ishtp_cl *cl)
{
	return cl->client_data;
}
EXPORT_SYMBOL(ishtp_get_client_data);

void ishtp_set_client_data(struct ishtp_cl *cl, void *data)
{
	cl->client_data = data;
}
EXPORT_SYMBOL(ishtp_set_client_data);

struct ishtp_device *ishtp_get_ishtp_device(struct ishtp_cl *cl)
{
	return cl->dev;
}
EXPORT_SYMBOL(ishtp_get_ishtp_device);

void ishtp_set_tx_ring_size(struct ishtp_cl *cl, int size)
{
	cl->tx_ring_size = size;
}
EXPORT_SYMBOL(ishtp_set_tx_ring_size);

void ishtp_set_rx_ring_size(struct ishtp_cl *cl, int size)
{
	cl->rx_ring_size = size;
}
EXPORT_SYMBOL(ishtp_set_rx_ring_size);

void ishtp_set_connection_state(struct ishtp_cl *cl, int state)
{
	cl->state = state;
}
EXPORT_SYMBOL(ishtp_set_connection_state);

void ishtp_cl_set_fw_client_id(struct ishtp_cl *cl, int fw_client_id)
{
	cl->fw_client_id = fw_client_id;
}
EXPORT_SYMBOL(ishtp_cl_set_fw_client_id);
