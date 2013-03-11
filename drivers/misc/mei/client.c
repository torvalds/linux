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
 * @file: pointer to file structure
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
 * @cb -  io callback structure
 * @size: size of the buffer
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
 * mei_io_cb_alloc_req_buf - allocate respose buffer
 *
 * @cb -  io callback structure
 * @size: size of the buffer
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
 * @dev: the device structure
 * @cl: host client
 */
int mei_cl_flush_queues(struct mei_cl *cl)
{
	if (WARN_ON(!cl || !cl->dev))
		return -EINVAL;

	dev_dbg(&cl->dev->pdev->dev, "remove list entry belonging to cl\n");
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
 * mei_cl_init - initializes intialize cl.
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
 * @dev: device structure
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

/** mei_cl_link: allocte host id in the host map
 *
 * @cl - host client
 * @id - fixed host id or -1 for genereting one
 * returns 0 on success
 *	-EINVAL on incorrect values
 *	-ENONET if client not found
 */
int mei_cl_link(struct mei_cl *cl, int id)
{
	struct mei_device *dev;

	if (WARN_ON(!cl || !cl->dev))
		return -EINVAL;

	dev = cl->dev;

	/* If Id is not asigned get one*/
	if (id == MEI_HOST_CLIENT_ID_ANY)
		id = find_first_zero_bit(dev->host_clients_map,
					MEI_CLIENTS_MAX);

	if (id >= MEI_CLIENTS_MAX) {
		dev_err(&dev->pdev->dev, "id exceded %d", MEI_CLIENTS_MAX) ;
		return -ENOENT;
	}

	dev->open_handle_count++;

	cl->host_client_id = id;
	list_add_tail(&cl->link, &dev->file_list);

	set_bit(id, dev->host_clients_map);

	cl->state = MEI_FILE_INITIALIZING;

	dev_dbg(&dev->pdev->dev, "link cl host id = %d\n", cl->host_client_id);
	return 0;
}

/**
 * mei_cl_unlink - remove me_cl from the list
 *
 * @dev: the device structure
 */
int mei_cl_unlink(struct mei_cl *cl)
{
	struct mei_device *dev;
	struct mei_cl *pos, *next;

	/* don't shout on error exit path */
	if (!cl)
		return 0;

	/* wd and amthif might not be initialized */
	if (!cl->dev)
		return 0;

	dev = cl->dev;

	list_for_each_entry_safe(pos, next, &dev->file_list, link) {
		if (cl->host_client_id == pos->host_client_id) {
			dev_dbg(&dev->pdev->dev, "remove host client = %d, ME client = %d\n",
				pos->host_client_id, pos->me_client_id);
			list_del_init(&pos->link);
			break;
		}
	}
	return 0;
}


void mei_host_client_init(struct work_struct *work)
{
	struct mei_device *dev = container_of(work,
					      struct mei_device, init_work);
	struct mei_client_properties *client_props;
	int i;

	mutex_lock(&dev->device_lock);

	bitmap_zero(dev->host_clients_map, MEI_CLIENTS_MAX);
	dev->open_handle_count = 0;

	/*
	 * Reserving the first three client IDs
	 * 0: Reserved for MEI Bus Message communications
	 * 1: Reserved for Watchdog
	 * 2: Reserved for AMTHI
	 */
	bitmap_set(dev->host_clients_map, 0, 3);

	for (i = 0; i < dev->me_clients_num; i++) {
		client_props = &dev->me_clients[i].props;

		if (!uuid_le_cmp(client_props->protocol_name, mei_amthif_guid))
			mei_amthif_host_init(dev);
		else if (!uuid_le_cmp(client_props->protocol_name, mei_wd_guid))
			mei_wd_host_init(dev);
	}

	dev->dev_state = MEI_DEV_ENABLED;

	mutex_unlock(&dev->device_lock);
}


/**
 * mei_cl_disconnect - disconnect host clinet form the me one
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
			dev_err(&dev->pdev->dev, "failed to disconnect.\n");
			goto free;
		}
		mdelay(10); /* Wait for hardware disconnection ready */
		list_add_tail(&cb->list, &dev->ctrl_rd_list.list);
	} else {
		dev_dbg(&dev->pdev->dev, "add disconnect cb to control write list\n");
		list_add_tail(&cb->list, &dev->ctrl_wr_list.list);

	}
	mutex_unlock(&dev->device_lock);

	err = wait_event_timeout(dev->wait_recvd_msg,
			MEI_FILE_DISCONNECTED == cl->state,
			mei_secs_to_jiffies(MEI_CL_CONNECT_TIMEOUT));

	mutex_lock(&dev->device_lock);
	if (MEI_FILE_DISCONNECTED == cl->state) {
		rets = 0;
		dev_dbg(&dev->pdev->dev, "successfully disconnected from FW client.\n");
	} else {
		rets = -ENODEV;
		if (MEI_FILE_DISCONNECTED != cl->state)
			dev_dbg(&dev->pdev->dev, "wrong status client disconnect.\n");

		if (err)
			dev_dbg(&dev->pdev->dev,
					"wait failed disconnect err=%08x\n",
					err);

		dev_dbg(&dev->pdev->dev, "failed to disconnect from FW client.\n");
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
 * returns ture if other client is connected, 0 - otherwise.
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
 * mei_cl_connect - connect host clinet to the me one
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
	long timeout = mei_secs_to_jiffies(MEI_CL_CONNECT_TIMEOUT);
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
				 timeout * HZ);
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
 * @dev: the device structure
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
 * @dev: the device structure
 * @cl: private data of the file object
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
 * mei_cl_start_read - the start read client message function.
 *
 * @cl: host client
 *
 * returns 0 on success, <0 on failure.
 */
int mei_cl_read_start(struct mei_cl *cl)
{
	struct mei_device *dev;
	struct mei_cl_cb *cb;
	int rets;
	int i;

	if (WARN_ON(!cl || !cl->dev))
		return -ENODEV;

	dev = cl->dev;

	if (cl->state != MEI_FILE_CONNECTED)
		return -ENODEV;

	if (dev->dev_state != MEI_DEV_ENABLED)
		return -ENODEV;

	if (cl->read_cb) {
		dev_dbg(&dev->pdev->dev, "read is pending.\n");
		return -EBUSY;
	}
	i = mei_me_cl_by_id(dev, cl->me_client_id);
	if (i < 0) {
		dev_err(&dev->pdev->dev, "no such me client %d\n",
			cl->me_client_id);
		return  -ENODEV;
	}

	cb = mei_io_cb_init(cl, NULL);
	if (!cb)
		return -ENOMEM;

	rets = mei_io_cb_alloc_resp_buf(cb,
			dev->me_clients[i].props.max_msg_length);
	if (rets)
		goto err;

	cb->fop_type = MEI_FOP_READ;
	cl->read_cb = cb;
	if (dev->hbuf_is_ready) {
		dev->hbuf_is_ready = false;
		if (mei_hbm_cl_flow_control_req(dev, cl)) {
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
 * mei_cl_all_read_wakeup  - wake up all readings so they can be interrupted
 *
 * @dev  - mei device
 */
void mei_cl_all_read_wakeup(struct mei_device *dev)
{
	struct mei_cl *cl, *next;
	list_for_each_entry_safe(cl, next, &dev->file_list, link) {
		if (waitqueue_active(&cl->rx_wait)) {
			dev_dbg(&dev->pdev->dev, "Waking up client!\n");
			wake_up_interruptible(&cl->rx_wait);
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


