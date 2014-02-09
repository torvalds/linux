/*
 *
 * Intel Management Engine Interface (Intel MEI) Linux driver
 * Copyright (c) 2003-2012, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/delay.h>

#include <linux/mei.h>

#include "mei_dev.h"
#include "hbm.h"
#include "client.h"

/**
 * mei_me_cl_by_uuid - locate index of me client
 *
 * @dev: mei device
 * returns me client index or -ENOENT if not found
 */
int mei_me_cl_by_uuid(const struct mei_device *dev, const uuid_le *uuid)
{
	int i, res = -ENOENT;

	for (i = 0; i < dev->me_clients_num; ++i)
		if (uuid_le_cmp(*uuid,
				dev->me_clients[i].props.protocol_name) == 0) {
			res = i;
			break;
		}

	return res;
}


/**
 * mei_me_cl_by_id return index to me_clients for client_id
 *
 * @dev: the device structure
 * @client_id: me client id
 *
 * Locking: called under "dev->device_lock" lock
 *
 * returns index on success, -ENOENT on failure.
 */

int mei_me_cl_by_id(struct mei_device *dev, u8 client_id)
{
	int i;
	for (i = 0; i < dev->me_clients_num; i++)
		if (dev->me_clients[i].client_id == client_id)
			break;
	if (WARN_ON(dev->me_clients[i].client_id != client_id))
		return -ENOENT;

	if (i == dev->me_clients_num)
		return -ENOENT;

	return i;
}


/**
 * mei_io_list_flush - removes list entry belonging to cl.
 *
 * @list:  An instance of our list structure
 * @cl: host client
 */
void mei_io_list_flush(struct mei_cl_cb *list, struct mei_cl *cl)
{
	struct mei_cl_cb *cb;
	struct mei_cl_cb *next;

	list_for_each_entry_safe(cb, next, &list->list, list) {
		if (cb->cl && mei_cl_cmp_id(cl, cb->cl))
			list_del(&cb->list);
	}
}

/**
 * mei_io_cb_free - free mei_cb_private related memory
 *
 * @cb: mei callback struct
 */
void mei_io_cb_free(struct mei_cl_cb *cb)
{
	if (cb == NULL)
		return;

	kfree(cb->request_buffer.data);
	kfree(cb->response_buffer.data);
	kfree(cb);
}

/**
 * mei_io_cb_init - allocate and initialize io callback
 *
 * @cl - mei client
 * @fp: pointer to file structure
 *
 * returns mei_cl_cb pointer or NULL;
 */
struct mei_cl_cb *mei_io_cb_init(struct mei_cl *cl, struct file *fp)
{
	struct mei_cl_cb *cb;

	cb = kzalloc(sizeof(struct mei_cl_cb), GFP_KERNEL);
	if (!cb)
		return NULL;

	mei_io_list_init(cb);

	cb->file_object = fp;
	cb->cl = cl;
	cb->buf_idx = 0;
	return cb;
}

/**
 * mei_io_cb_alloc_req_buf - allocate request buffer
 *
 * @cb: io callback structure
 * @length: size of the buffer
 *
 * returns 0 on success
 *         -EINVAL if cb is NULL
 *         -ENOMEM if allocation failed
 */
int mei_io_cb_alloc_req_buf(struct mei_cl_cb *cb, size_t length)
{
	if (!cb)
		return -EINVAL;

	if (length == 0)
		return 0;

	cb->request_buffer.data = kmalloc(length, GFP_KERNEL);
	if (!cb->request_buffer.data)
		return -ENOMEM;
	cb->request_buffer.size = length;
	return 0;
}
/**
 * mei_io_cb_alloc_resp_buf - allocate response buffer
 *
 * @cb: io callback structure
 * @length: size of the buffer
 *
 * returns 0 on success
 *         -EINVAL if cb is NULL
 *         -ENOMEM if allocation failed
 */
int mei_io_cb_alloc_resp_buf(struct mei_cl_cb *cb, size_t length)
{
	if (!cb)
		return -EINVAL;

	if (length == 0)
		return 0;

	cb->response_buffer.data = kmalloc(length, GFP_KERNEL);
	if (!cb->response_buffer.data)
		return -ENOMEM;
	cb->response_buffer.size = length;
	return 0;
}



/**
 * mei_cl_flush_queues - flushes queue lists belonging to cl.
 *
 * @cl: host client
 */
int mei_cl_flush_queues(struct mei_cl *cl)
{
	struct mei_device *dev;

	if (WARN_ON(!cl || !cl->dev))
		return -EINVAL;

	dev = cl->dev;

	cl_dbg(dev, cl, "remove list entry belonging to cl\n");
	mei_io_list_flush(&cl->dev->read_list, cl);
	mei_io_list_flush(&cl->dev->write_list, cl);
	mei_io_list_flush(&cl->dev->write_waiting_list, cl);
	mei_io_list_flush(&cl->dev->ctrl_wr_list, cl);
	mei_io_list_flush(&cl->dev->ctrl_rd_list, cl);
	mei_io_list_flush(&cl->dev->amthif_cmd_list, cl);
	mei_io_list_flush(&cl->dev->amthif_rd_complete_list, cl);
	return 0;
}


/**
 * mei_cl_init - initializes cl.
 *
 * @cl: host client to be initialized
 * @dev: mei device
 */
void mei_cl_init(struct mei_cl *cl, struct mei_device *dev)
{
	memset(cl, 0, sizeof(struct mei_cl));
	init_waitqueue_head(&cl->wait);
	init_waitqueue_head(&cl->rx_wait);
	init_waitqueue_head(&cl->tx_wait);
	INIT_LIST_HEAD(&cl->link);
	INIT_LIST_HEAD(&cl->device_link);
	cl->reading_state = MEI_IDLE;
	cl->writing_state = MEI_IDLE;
	cl->dev = dev;
}

/**
 * mei_cl_allocate - allocates cl  structure and sets it up.
 *
 * @dev: mei device
 * returns  The allocated file or NULL on failure
 */
struct mei_cl *mei_cl_allocate(struct mei_device *dev)
{
	struct mei_cl *cl;

	cl = kmalloc(sizeof(struct mei_cl), GFP_KERNEL);
	if (!cl)
		return NULL;

	mei_cl_init(cl, dev);

	return cl;
}

/**
 * mei_cl_find_read_cb - find this cl's callback in the read list
 *
 * @cl: host client
 *
 * returns cb on success, NULL on error
 */
struct mei_cl_cb *mei_cl_find_read_cb(struct mei_cl *cl)
{
	struct mei_device *dev = cl->dev;
	struct mei_cl_cb *cb = NULL;
	struct mei_cl_cb *next = NULL;

	list_for_each_entry_safe(cb, next, &dev->read_list.list, list)
		if (mei_cl_cmp_id(cl, cb->cl))
			return cb;
	return NULL;
}

/** mei_cl_link: allocate host id in the host map
 *
 * @cl - host client
 * @id - fixed host id or -1 for generic one
 *
 * returns 0 on success
 *	-EINVAL on incorrect values
 *	-ENONET if client not found
 */
int mei_cl_link(struct mei_cl *cl, int id)
{
	struct mei_device *dev;
	long open_handle_count;

	if (WARN_ON(!cl || !cl->dev))
		return -EINVAL;

	dev = cl->dev;

	/* If Id is not assigned get one*/
	if (id == MEI_HOST_CLIENT_ID_ANY)
		id = find_first_zero_bit(dev->host_clients_map,
					MEI_CLIENTS_MAX);

	if (id >= MEI_CLIENTS_MAX) {
		dev_err(&dev->pdev->dev, "id exceeded %d", MEI_CLIENTS_MAX);
		return -EMFILE;
	}

	open_handle_count = dev->open_handle_count + dev->iamthif_open_count;
	if (open_handle_count >= MEI_MAX_OPEN_HANDLE_COUNT) {
		dev_err(&dev->pdev->dev, "open_handle_count exceeded %d",
			MEI_MAX_OPEN_HANDLE_COUNT);
		return -EMFILE;
	}

	dev->open_handle_count++;

	cl->host_client_id = id;
	list_add_tail(&cl->link, &dev->file_list);

	set_bit(id, dev->host_clients_map);

	cl->state = MEI_FILE_INITIALIZING;

	cl_dbg(dev, cl, "link cl\n");
	return 0;
}

/**
 * mei_cl_unlink - remove me_cl from the list
 *
 * @cl: host client
 */
int mei_cl_unlink(struct mei_cl *cl)
{
	struct mei_device *dev;

	/* don't shout on error exit path */
	if (!cl)
		return 0;

	/* wd and amthif might not be initialized */
	if (!cl->dev)
		return 0;

	dev = cl->dev;

	cl_dbg(dev, cl, "unlink client");

	if (dev->open_handle_count > 0)
		dev->open_handle_count--;

	/* never clear the 0 bit */
	if (cl->host_client_id)
		clear_bit(cl->host_client_id, dev->host_clients_map);

	list_del_init(&cl->link);

	cl->state = MEI_FILE_INITIALIZING;

	return 0;
}


void mei_host_client_init(struct work_struct *work)
{
	struct mei_device *dev = container_of(work,
					      struct mei_device, init_work);
	struct mei_client_properties *client_props;
	int i;

	mutex_lock(&dev->device_lock);

	for (i = 0; i < dev->me_clients_num; i++) {
		client_props = &dev->me_clients[i].props;

		if (!uuid_le_cmp(client_props->protocol_name, mei_amthif_guid))
			mei_amthif_host_init(dev);
		else if (!uuid_le_cmp(client_props->protocol_name, mei_wd_guid))
			mei_wd_host_init(dev);
		else if (!uuid_le_cmp(client_props->protocol_name, mei_nfc_guid))
			mei_nfc_host_init(dev);

	}

	dev->dev_state = MEI_DEV_ENABLED;
	dev->reset_count = 0;

	mutex_unlock(&dev->device_lock);
}


/**
 * mei_cl_disconnect - disconnect host client from the me one
 *
 * @cl: host client
 *
 * Locking: called under "dev->device_lock" lock
 *
 * returns 0 on success, <0 on failure.
 */
int mei_cl_disconnect(struct mei_cl *cl)
{
	struct mei_device *dev;
	struct mei_cl_cb *cb;
	int rets, err;

	if (WARN_ON(!cl || !cl->dev))
		return -ENODEV;

	dev = cl->dev;

	cl_dbg(dev, cl, "disconnecting");

	if (cl->state != MEI_FILE_DISCONNECTING)
		return 0;

	cb = mei_io_cb_init(cl, NULL);
	if (!cb)
		return -ENOMEM;

	cb->fop_type = MEI_FOP_CLOSE;
	if (dev->hbuf_is_ready) {
		dev->hbuf_is_ready = false;
		if (mei_hbm_cl_disconnect_req(dev, cl)) {
			rets = -ENODEV;
			cl_err(dev, cl, "failed to disconnect.\n");
			goto free;
		}
		mdelay(10); /* Wait for hardware disconnection ready */
		list_add_tail(&cb->list, &dev->ctrl_rd_list.list);
	} else {
		cl_dbg(dev, cl, "add disconnect cb to control write list\n");
		list_add_tail(&cb->list, &dev->ctrl_wr_list.list);

	}
	mutex_unlock(&dev->device_lock);

	err = wait_event_timeout(dev->wait_recvd_msg,
			MEI_FILE_DISCONNECTED == cl->state,
			mei_secs_to_jiffies(MEI_CL_CONNECT_TIMEOUT));

	mutex_lock(&dev->device_lock);
	if (MEI_FILE_DISCONNECTED == cl->state) {
		rets = 0;
		cl_dbg(dev, cl, "successfully disconnected from FW client.\n");
	} else {
		rets = -ENODEV;
		if (MEI_FILE_DISCONNECTED != cl->state)
			cl_err(dev, cl, "wrong status client disconnect.\n");

		if (err)
			cl_dbg(dev, cl, "wait failed disconnect err=%08x\n",
					err);

		cl_err(dev, cl, "failed to disconnect from FW client.\n");
	}

	mei_io_list_flush(&dev->ctrl_rd_list, cl);
	mei_io_list_flush(&dev->ctrl_wr_list, cl);
free:
	mei_io_cb_free(cb);
	return rets;
}


/**
 * mei_cl_is_other_connecting - checks if other
 *    client with the same me client id is connecting
 *
 * @cl: private data of the file object
 *
 * returns true if other client is connected, false - otherwise.
 */
bool mei_cl_is_other_connecting(struct mei_cl *cl)
{
	struct mei_device *dev;
	struct mei_cl *pos;
	struct mei_cl *next;

	if (WARN_ON(!cl || !cl->dev))
		return false;

	dev = cl->dev;

	list_for_each_entry_safe(pos, next, &dev->file_list, link) {
		if ((pos->state == MEI_FILE_CONNECTING) &&
		    (pos != cl) && cl->me_client_id == pos->me_client_id)
			return true;

	}

	return false;
}

/**
 * mei_cl_connect - connect host client to the me one
 *
 * @cl: host client
 *
 * Locking: called under "dev->device_lock" lock
 *
 * returns 0 on success, <0 on failure.
 */
int mei_cl_connect(struct mei_cl *cl, struct file *file)
{
	struct mei_device *dev;
	struct mei_cl_cb *cb;
	int rets;

	if (WARN_ON(!cl || !cl->dev))
		return -ENODEV;

	dev = cl->dev;

	cb = mei_io_cb_init(cl, file);
	if (!cb) {
		rets = -ENOMEM;
		goto out;
	}

	cb->fop_type = MEI_FOP_IOCTL;

	if (dev->hbuf_is_ready && !mei_cl_is_other_connecting(cl)) {
		dev->hbuf_is_ready = false;

		if (mei_hbm_cl_connect_req(dev, cl)) {
			rets = -ENODEV;
			goto out;
		}
		cl->timer_count = MEI_CONNECT_TIMEOUT;
		list_add_tail(&cb->list, &dev->ctrl_rd_list.list);
	} else {
		list_add_tail(&cb->list, &dev->ctrl_wr_list.list);
	}

	mutex_unlock(&dev->device_lock);
	rets = wait_event_timeout(dev->wait_recvd_msg,
				 (cl->state == MEI_FILE_CONNECTED ||
				  cl->state == MEI_FILE_DISCONNECTED),
				 mei_secs_to_jiffies(MEI_CL_CONNECT_TIMEOUT));
	mutex_lock(&dev->device_lock);

	if (cl->state != MEI_FILE_CONNECTED) {
		rets = -EFAULT;

		mei_io_list_flush(&dev->ctrl_rd_list, cl);
		mei_io_list_flush(&dev->ctrl_wr_list, cl);
		goto out;
	}

	rets = cl->status;

out:
	mei_io_cb_free(cb);
	return rets;
}

/**
 * mei_cl_flow_ctrl_creds - checks flow_control credits for cl.
 *
 * @cl: private data of the file object
 *
 * returns 1 if mei_flow_ctrl_creds >0, 0 - otherwise.
 *	-ENOENT if mei_cl is not present
 *	-EINVAL if single_recv_buf == 0
 */
int mei_cl_flow_ctrl_creds(struct mei_cl *cl)
{
	struct mei_device *dev;
	int i;

	if (WARN_ON(!cl || !cl->dev))
		return -EINVAL;

	dev = cl->dev;

	if (!dev->me_clients_num)
		return 0;

	if (cl->mei_flow_ctrl_creds > 0)
		return 1;

	for (i = 0; i < dev->me_clients_num; i++) {
		struct mei_me_client  *me_cl = &dev->me_clients[i];
		if (me_cl->client_id == cl->me_client_id) {
			if (me_cl->mei_flow_ctrl_creds) {
				if (WARN_ON(me_cl->props.single_recv_buf == 0))
					return -EINVAL;
				return 1;
			} else {
				return 0;
			}
		}
	}
	return -ENOENT;
}

/**
 * mei_cl_flow_ctrl_reduce - reduces flow_control.
 *
 * @cl: private data of the file object
 *
 * @returns
 *	0 on success
 *	-ENOENT when me client is not found
 *	-EINVAL when ctrl credits are <= 0
 */
int mei_cl_flow_ctrl_reduce(struct mei_cl *cl)
{
	struct mei_device *dev;
	int i;

	if (WARN_ON(!cl || !cl->dev))
		return -EINVAL;

	dev = cl->dev;

	if (!dev->me_clients_num)
		return -ENOENT;

	for (i = 0; i < dev->me_clients_num; i++) {
		struct mei_me_client  *me_cl = &dev->me_clients[i];
		if (me_cl->client_id == cl->me_client_id) {
			if (me_cl->props.single_recv_buf != 0) {
				if (WARN_ON(me_cl->mei_flow_ctrl_creds <= 0))
					return -EINVAL;
				dev->me_clients[i].mei_flow_ctrl_creds--;
			} else {
				if (WARN_ON(cl->mei_flow_ctrl_creds <= 0))
					return -EINVAL;
				cl->mei_flow_ctrl_creds--;
			}
			return 0;
		}
	}
	return -ENOENT;
}

/**
 * mei_cl_read_start - the start read client message function.
 *
 * @cl: host client
 *
 * returns 0 on success, <0 on failure.
 */
int mei_cl_read_start(struct mei_cl *cl, size_t length)
{
	struct mei_device *dev;
	struct mei_cl_cb *cb;
	int rets;
	int i;

	if (WARN_ON(!cl || !cl->dev))
		return -ENODEV;

	dev = cl->dev;

	if (!mei_cl_is_connected(cl))
		return -ENODEV;

	if (cl->read_cb) {
		cl_dbg(dev, cl, "read is pending.\n");
		return -EBUSY;
	}
	i = mei_me_cl_by_id(dev, cl->me_client_id);
	if (i < 0) {
		cl_err(dev, cl, "no such me client %d\n", cl->me_client_id);
		return  -ENODEV;
	}

	cb = mei_io_cb_init(cl, NULL);
	if (!cb)
		return -ENOMEM;

	/* always allocate at least client max message */
	length = max_t(size_t, length, dev->me_clients[i].props.max_msg_length);
	rets = mei_io_cb_alloc_resp_buf(cb, length);
	if (rets)
		goto err;

	cb->fop_type = MEI_FOP_READ;
	cl->read_cb = cb;
	if (dev->hbuf_is_ready) {
		dev->hbuf_is_ready = false;
		if (mei_hbm_cl_flow_control_req(dev, cl)) {
			cl_err(dev, cl, "flow control send failed\n");
			rets = -ENODEV;
			goto err;
		}
		list_add_tail(&cb->list, &dev->read_list.list);
	} else {
		list_add_tail(&cb->list, &dev->ctrl_wr_list.list);
	}
	return rets;
err:
	mei_io_cb_free(cb);
	return rets;
}

/**
 * mei_cl_irq_write_complete - write a message to device
 *	from the interrupt thread context
 *
 * @cl: client
 * @cb: callback block.
 * @slots: free slots.
 * @cmpl_list: complete list.
 *
 * returns 0, OK; otherwise error.
 */
int mei_cl_irq_write_complete(struct mei_cl *cl, struct mei_cl_cb *cb,
				     s32 *slots, struct mei_cl_cb *cmpl_list)
{
	struct mei_device *dev;
	struct mei_msg_data *buf;
	struct mei_msg_hdr mei_hdr;
	size_t len;
	u32 msg_slots;
	int rets;


	if (WARN_ON(!cl || !cl->dev))
		return -ENODEV;

	dev = cl->dev;

	buf = &cb->request_buffer;

	rets = mei_cl_flow_ctrl_creds(cl);
	if (rets < 0)
		return rets;

	if (rets == 0) {
		cl_dbg(dev, cl,	"No flow control credentials: not sending.\n");
		return 0;
	}

	len = buf->size - cb->buf_idx;
	msg_slots = mei_data2slots(len);

	mei_hdr.host_addr = cl->host_client_id;
	mei_hdr.me_addr = cl->me_client_id;
	mei_hdr.reserved = 0;
	mei_hdr.internal = cb->internal;

	if (*slots >= msg_slots) {
		mei_hdr.length = len;
		mei_hdr.msg_complete = 1;
	/* Split the message only if we can write the whole host buffer */
	} else if (*slots == dev->hbuf_depth) {
		msg_slots = *slots;
		len = (*slots * sizeof(u32)) - sizeof(struct mei_msg_hdr);
		mei_hdr.length = len;
		mei_hdr.msg_complete = 0;
	} else {
		/* wait for next time the host buffer is empty */
		return 0;
	}

	cl_dbg(dev, cl, "buf: size = %d idx = %lu\n",
			cb->request_buffer.size, cb->buf_idx);

	*slots -=  msg_slots;
	rets = mei_write_message(dev, &mei_hdr, buf->data + cb->buf_idx);
	if (rets) {
		cl->status = rets;
		list_move_tail(&cb->list, &cmpl_list->list);
		return rets;
	}

	cl->status = 0;
	cl->writing_state = MEI_WRITING;
	cb->buf_idx += mei_hdr.length;

	if (mei_hdr.msg_complete) {
		if (mei_cl_flow_ctrl_reduce(cl))
			return -EIO;
		list_move_tail(&cb->list, &dev->write_waiting_list.list);
	}

	return 0;
}

/**
 * mei_cl_write - submit a write cb to mei device
	assumes device_lock is locked
 *
 * @cl: host client
 * @cl: write callback with filled data
 *
 * returns number of bytes sent on success, <0 on failure.
 */
int mei_cl_write(struct mei_cl *cl, struct mei_cl_cb *cb, bool blocking)
{
	struct mei_device *dev;
	struct mei_msg_data *buf;
	struct mei_msg_hdr mei_hdr;
	int rets;


	if (WARN_ON(!cl || !cl->dev))
		return -ENODEV;

	if (WARN_ON(!cb))
		return -EINVAL;

	dev = cl->dev;


	buf = &cb->request_buffer;

	cl_dbg(dev, cl, "mei_cl_write %d\n", buf->size);


	cb->fop_type = MEI_FOP_WRITE;

	rets = mei_cl_flow_ctrl_creds(cl);
	if (rets < 0)
		goto err;

	/* Host buffer is not ready, we queue the request */
	if (rets == 0 || !dev->hbuf_is_ready) {
		cb->buf_idx = 0;
		/* unseting complete will enqueue the cb for write */
		mei_hdr.msg_complete = 0;
		rets = buf->size;
		goto out;
	}

	dev->hbuf_is_ready = false;

	/* Check for a maximum length */
	if (buf->size > mei_hbuf_max_len(dev)) {
		mei_hdr.length = mei_hbuf_max_len(dev);
		mei_hdr.msg_complete = 0;
	} else {
		mei_hdr.length = buf->size;
		mei_hdr.msg_complete = 1;
	}

	mei_hdr.host_addr = cl->host_client_id;
	mei_hdr.me_addr = cl->me_client_id;
	mei_hdr.reserved = 0;
	mei_hdr.internal = cb->internal;


	rets = mei_write_message(dev, &mei_hdr, buf->data);
	if (rets)
		goto err;

	cl->writing_state = MEI_WRITING;
	cb->buf_idx = mei_hdr.length;

	rets = buf->size;
out:
	if (mei_hdr.msg_complete) {
		if (mei_cl_flow_ctrl_reduce(cl)) {
			rets = -ENODEV;
			goto err;
		}
		list_add_tail(&cb->list, &dev->write_waiting_list.list);
	} else {
		list_add_tail(&cb->list, &dev->write_list.list);
	}


	if (blocking && cl->writing_state != MEI_WRITE_COMPLETE) {

		mutex_unlock(&dev->device_lock);
		if (wait_event_interruptible(cl->tx_wait,
			cl->writing_state == MEI_WRITE_COMPLETE)) {
				if (signal_pending(current))
					rets = -EINTR;
				else
					rets = -ERESTARTSYS;
		}
		mutex_lock(&dev->device_lock);
	}
err:
	return rets;
}


/**
 * mei_cl_complete - processes completed operation for a client
 *
 * @cl: private data of the file object.
 * @cb: callback block.
 */
void mei_cl_complete(struct mei_cl *cl, struct mei_cl_cb *cb)
{
	if (cb->fop_type == MEI_FOP_WRITE) {
		mei_io_cb_free(cb);
		cb = NULL;
		cl->writing_state = MEI_WRITE_COMPLETE;
		if (waitqueue_active(&cl->tx_wait))
			wake_up_interruptible(&cl->tx_wait);

	} else if (cb->fop_type == MEI_FOP_READ &&
			MEI_READING == cl->reading_state) {
		cl->reading_state = MEI_READ_COMPLETE;
		if (waitqueue_active(&cl->rx_wait))
			wake_up_interruptible(&cl->rx_wait);
		else
			mei_cl_bus_rx_event(cl);

	}
}


/**
 * mei_cl_all_disconnect - disconnect forcefully all connected clients
 *
 * @dev - mei device
 */

void mei_cl_all_disconnect(struct mei_device *dev)
{
	struct mei_cl *cl, *next;

	list_for_each_entry_safe(cl, next, &dev->file_list, link) {
		cl->state = MEI_FILE_DISCONNECTED;
		cl->mei_flow_ctrl_creds = 0;
		cl->read_cb = NULL;
		cl->timer_count = 0;
	}
}


/**
 * mei_cl_all_wakeup  - wake up all readers and writers they can be interrupted
 *
 * @dev  - mei device
 */
void mei_cl_all_wakeup(struct mei_device *dev)
{
	struct mei_cl *cl, *next;
	list_for_each_entry_safe(cl, next, &dev->file_list, link) {
		if (waitqueue_active(&cl->rx_wait)) {
			cl_dbg(dev, cl, "Waking up reading client!\n");
			wake_up_interruptible(&cl->rx_wait);
		}
		if (waitqueue_active(&cl->tx_wait)) {
			cl_dbg(dev, cl, "Waking up writing client!\n");
			wake_up_interruptible(&cl->tx_wait);
		}
	}
}

/**
 * mei_cl_all_write_clear - clear all pending writes

 * @dev - mei device
 */
void mei_cl_all_write_clear(struct mei_device *dev)
{
	struct mei_cl_cb *cb, *next;

	list_for_each_entry_safe(cb, next, &dev->write_list.list, list) {
		list_del(&cb->list);
		mei_io_cb_free(cb);
	}
}


