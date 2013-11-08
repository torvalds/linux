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

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/aio.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/ioctl.h>
#include <linux/cdev.h>
#include <linux/list.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/uuid.h>
#include <linux/jiffies.h>
#include <linux/uaccess.h>

#include <linux/mei.h>

#include "mei_dev.h"
#include "hbm.h"
#include "hw-me.h"
#include "client.h"

const uuid_le mei_amthif_guid  = UUID_LE(0x12f80028, 0xb4b7, 0x4b2d,
					 0xac, 0xa8, 0x46, 0xe0,
					 0xff, 0x65, 0x81, 0x4c);

/**
 * mei_amthif_reset_params - initializes mei device iamthif
 *
 * @dev: the device structure
 */
void mei_amthif_reset_params(struct mei_device *dev)
{
	/* reset iamthif parameters. */
	dev->iamthif_current_cb = NULL;
	dev->iamthif_msg_buf_size = 0;
	dev->iamthif_msg_buf_index = 0;
	dev->iamthif_canceled = false;
	dev->iamthif_ioctl = false;
	dev->iamthif_state = MEI_IAMTHIF_IDLE;
	dev->iamthif_timer = 0;
	dev->iamthif_stall_timer = 0;
}

/**
 * mei_amthif_host_init - mei initialization amthif client.
 *
 * @dev: the device structure
 *
 */
int mei_amthif_host_init(struct mei_device *dev)
{
	struct mei_cl *cl = &dev->iamthif_cl;
	unsigned char *msg_buf;
	int ret, i;

	dev->iamthif_state = MEI_IAMTHIF_IDLE;

	mei_cl_init(cl, dev);

	i = mei_me_cl_by_uuid(dev, &mei_amthif_guid);
	if (i < 0) {
		dev_info(&dev->pdev->dev, "amthif: failed to find the client\n");
		return -ENOENT;
	}

	cl->me_client_id = dev->me_clients[i].client_id;

	/* Assign iamthif_mtu to the value received from ME  */

	dev->iamthif_mtu = dev->me_clients[i].props.max_msg_length;
	dev_dbg(&dev->pdev->dev, "IAMTHIF_MTU = %d\n",
			dev->me_clients[i].props.max_msg_length);

	kfree(dev->iamthif_msg_buf);
	dev->iamthif_msg_buf = NULL;

	/* allocate storage for ME message buffer */
	msg_buf = kcalloc(dev->iamthif_mtu,
			sizeof(unsigned char), GFP_KERNEL);
	if (!msg_buf) {
		dev_err(&dev->pdev->dev, "amthif: memory allocation for ME message buffer failed.\n");
		return -ENOMEM;
	}

	dev->iamthif_msg_buf = msg_buf;

	ret = mei_cl_link(cl, MEI_IAMTHIF_HOST_CLIENT_ID);

	if (ret < 0) {
		dev_err(&dev->pdev->dev, "amthif: failed link client\n");
		return -ENOENT;
	}

	cl->state = MEI_FILE_CONNECTING;

	if (mei_hbm_cl_connect_req(dev, cl)) {
		dev_dbg(&dev->pdev->dev, "amthif: Failed to connect to ME client\n");
		cl->state = MEI_FILE_DISCONNECTED;
		cl->host_client_id = 0;
	} else {
		cl->timer_count = MEI_CONNECT_TIMEOUT;
	}
	return 0;
}

/**
 * mei_amthif_find_read_list_entry - finds a amthilist entry for current file
 *
 * @dev: the device structure
 * @file: pointer to file object
 *
 * returns   returned a list entry on success, NULL on failure.
 */
struct mei_cl_cb *mei_amthif_find_read_list_entry(struct mei_device *dev,
						struct file *file)
{
	struct mei_cl_cb *pos = NULL;
	struct mei_cl_cb *next = NULL;

	list_for_each_entry_safe(pos, next,
				&dev->amthif_rd_complete_list.list, list) {
		if (pos->cl && pos->cl == &dev->iamthif_cl &&
			pos->file_object == file)
			return pos;
	}
	return NULL;
}


/**
 * mei_amthif_read - read data from AMTHIF client
 *
 * @dev: the device structure
 * @if_num:  minor number
 * @file: pointer to file object
 * @*ubuf: pointer to user data in user space
 * @length: data length to read
 * @offset: data read offset
 *
 * Locking: called under "dev->device_lock" lock
 *
 * returns
 *  returned data length on success,
 *  zero if no data to read,
 *  negative on failure.
 */
int mei_amthif_read(struct mei_device *dev, struct file *file,
	       char __user *ubuf, size_t length, loff_t *offset)
{
	int rets;
	int wait_ret;
	struct mei_cl_cb *cb = NULL;
	struct mei_cl *cl = file->private_data;
	unsigned long timeout;
	int i;

	/* Only Posible if we are in timeout */
	if (!cl || cl != &dev->iamthif_cl) {
		dev_dbg(&dev->pdev->dev, "bad file ext.\n");
		return -ETIMEDOUT;
	}

	i = mei_me_cl_by_id(dev, dev->iamthif_cl.me_client_id);

	if (i < 0) {
		dev_dbg(&dev->pdev->dev, "amthif client not found.\n");
		return -ENODEV;
	}
	dev_dbg(&dev->pdev->dev, "checking amthif data\n");
	cb = mei_amthif_find_read_list_entry(dev, file);

	/* Check for if we can block or not*/
	if (cb == NULL && file->f_flags & O_NONBLOCK)
		return -EAGAIN;


	dev_dbg(&dev->pdev->dev, "waiting for amthif data\n");
	while (cb == NULL) {
		/* unlock the Mutex */
		mutex_unlock(&dev->device_lock);

		wait_ret = wait_event_interruptible(dev->iamthif_cl.wait,
			(cb = mei_amthif_find_read_list_entry(dev, file)));

		/* Locking again the Mutex */
		mutex_lock(&dev->device_lock);

		if (wait_ret)
			return -ERESTARTSYS;

		dev_dbg(&dev->pdev->dev, "woke up from sleep\n");
	}


	dev_dbg(&dev->pdev->dev, "Got amthif data\n");
	dev->iamthif_timer = 0;

	if (cb) {
		timeout = cb->read_time +
			mei_secs_to_jiffies(MEI_IAMTHIF_READ_TIMER);
		dev_dbg(&dev->pdev->dev, "amthif timeout = %lud\n",
				timeout);

		if  (time_after(jiffies, timeout)) {
			dev_dbg(&dev->pdev->dev, "amthif Time out\n");
			/* 15 sec for the message has expired */
			list_del(&cb->list);
			rets = -ETIMEDOUT;
			goto free;
		}
	}
	/* if the whole message will fit remove it from the list */
	if (cb->buf_idx >= *offset && length >= (cb->buf_idx - *offset))
		list_del(&cb->list);
	else if (cb->buf_idx > 0 && cb->buf_idx <= *offset) {
		/* end of the message has been reached */
		list_del(&cb->list);
		rets = 0;
		goto free;
	}
		/* else means that not full buffer will be read and do not
		 * remove message from deletion list
		 */

	dev_dbg(&dev->pdev->dev, "amthif cb->response_buffer size - %d\n",
	    cb->response_buffer.size);
	dev_dbg(&dev->pdev->dev, "amthif cb->buf_idx - %lu\n", cb->buf_idx);

	/* length is being turncated to PAGE_SIZE, however,
	 * the buf_idx may point beyond */
	length = min_t(size_t, length, (cb->buf_idx - *offset));

	if (copy_to_user(ubuf, cb->response_buffer.data + *offset, length))
		rets = -EFAULT;
	else {
		rets = length;
		if ((*offset + length) < cb->buf_idx) {
			*offset += length;
			goto out;
		}
	}
free:
	dev_dbg(&dev->pdev->dev, "free amthif cb memory.\n");
	*offset = 0;
	mei_io_cb_free(cb);
out:
	return rets;
}

/**
 * mei_amthif_send_cmd - send amthif command to the ME
 *
 * @dev: the device structure
 * @cb: mei call back struct
 *
 * returns 0 on success, <0 on failure.
 *
 */
static int mei_amthif_send_cmd(struct mei_device *dev, struct mei_cl_cb *cb)
{
	struct mei_msg_hdr mei_hdr;
	int ret;

	if (!dev || !cb)
		return -ENODEV;

	dev_dbg(&dev->pdev->dev, "write data to amthif client.\n");

	dev->iamthif_state = MEI_IAMTHIF_WRITING;
	dev->iamthif_current_cb = cb;
	dev->iamthif_file_object = cb->file_object;
	dev->iamthif_canceled = false;
	dev->iamthif_ioctl = true;
	dev->iamthif_msg_buf_size = cb->request_buffer.size;
	memcpy(dev->iamthif_msg_buf, cb->request_buffer.data,
	       cb->request_buffer.size);

	ret = mei_cl_flow_ctrl_creds(&dev->iamthif_cl);
	if (ret < 0)
		return ret;

	if (ret && dev->hbuf_is_ready) {
		ret = 0;
		dev->hbuf_is_ready = false;
		if (cb->request_buffer.size > mei_hbuf_max_len(dev)) {
			mei_hdr.length = mei_hbuf_max_len(dev);
			mei_hdr.msg_complete = 0;
		} else {
			mei_hdr.length = cb->request_buffer.size;
			mei_hdr.msg_complete = 1;
		}

		mei_hdr.host_addr = dev->iamthif_cl.host_client_id;
		mei_hdr.me_addr = dev->iamthif_cl.me_client_id;
		mei_hdr.reserved = 0;
		dev->iamthif_msg_buf_index += mei_hdr.length;
		if (mei_write_message(dev, &mei_hdr,
					(unsigned char *)dev->iamthif_msg_buf))
			return -ENODEV;

		if (mei_hdr.msg_complete) {
			if (mei_cl_flow_ctrl_reduce(&dev->iamthif_cl))
				return -ENODEV;
			dev->iamthif_flow_control_pending = true;
			dev->iamthif_state = MEI_IAMTHIF_FLOW_CONTROL;
			dev_dbg(&dev->pdev->dev, "add amthif cb to write waiting list\n");
			dev->iamthif_current_cb = cb;
			dev->iamthif_file_object = cb->file_object;
			list_add_tail(&cb->list, &dev->write_waiting_list.list);
		} else {
			dev_dbg(&dev->pdev->dev, "message does not complete, so add amthif cb to write list.\n");
			list_add_tail(&cb->list, &dev->write_list.list);
		}
	} else {
		if (!dev->hbuf_is_ready)
			dev_dbg(&dev->pdev->dev, "host buffer is not empty");

		dev_dbg(&dev->pdev->dev, "No flow control credentials, so add iamthif cb to write list.\n");
		list_add_tail(&cb->list, &dev->write_list.list);
	}
	return 0;
}

/**
 * mei_amthif_write - write amthif data to amthif client
 *
 * @dev: the device structure
 * @cb: mei call back struct
 *
 * returns 0 on success, <0 on failure.
 *
 */
int mei_amthif_write(struct mei_device *dev, struct mei_cl_cb *cb)
{
	int ret;

	if (!dev || !cb)
		return -ENODEV;

	ret = mei_io_cb_alloc_resp_buf(cb, dev->iamthif_mtu);
	if (ret)
		return ret;

	cb->fop_type = MEI_FOP_IOCTL;

	if (!list_empty(&dev->amthif_cmd_list.list) ||
	    dev->iamthif_state != MEI_IAMTHIF_IDLE) {
		dev_dbg(&dev->pdev->dev,
			"amthif state = %d\n", dev->iamthif_state);
		dev_dbg(&dev->pdev->dev, "AMTHIF: add cb to the wait list\n");
		list_add_tail(&cb->list, &dev->amthif_cmd_list.list);
		return 0;
	}
	return mei_amthif_send_cmd(dev, cb);
}
/**
 * mei_amthif_run_next_cmd
 *
 * @dev: the device structure
 *
 * returns 0 on success, <0 on failure.
 */
void mei_amthif_run_next_cmd(struct mei_device *dev)
{
	struct mei_cl_cb *pos = NULL;
	struct mei_cl_cb *next = NULL;
	int status;

	if (!dev)
		return;

	dev->iamthif_msg_buf_size = 0;
	dev->iamthif_msg_buf_index = 0;
	dev->iamthif_canceled = false;
	dev->iamthif_ioctl = true;
	dev->iamthif_state = MEI_IAMTHIF_IDLE;
	dev->iamthif_timer = 0;
	dev->iamthif_file_object = NULL;

	dev_dbg(&dev->pdev->dev, "complete amthif cmd_list cb.\n");

	list_for_each_entry_safe(pos, next, &dev->amthif_cmd_list.list, list) {
		list_del(&pos->list);

		if (pos->cl && pos->cl == &dev->iamthif_cl) {
			status = mei_amthif_send_cmd(dev, pos);
			if (status) {
				dev_dbg(&dev->pdev->dev,
					"amthif write failed status = %d\n",
						status);
				return;
			}
			break;
		}
	}
}


unsigned int mei_amthif_poll(struct mei_device *dev,
		struct file *file, poll_table *wait)
{
	unsigned int mask = 0;

	poll_wait(file, &dev->iamthif_cl.wait, wait);

	mutex_lock(&dev->device_lock);
	if (!mei_cl_is_connected(&dev->iamthif_cl)) {

		mask = POLLERR;

	} else if (dev->iamthif_state == MEI_IAMTHIF_READ_COMPLETE &&
		   dev->iamthif_file_object == file) {

		mask |= (POLLIN | POLLRDNORM);
		dev_dbg(&dev->pdev->dev, "run next amthif cb\n");
		mei_amthif_run_next_cmd(dev);
	}
	mutex_unlock(&dev->device_lock);

	return mask;
}



/**
 * mei_amthif_irq_write_completed - processes completed iamthif operation.
 *
 * @dev: the device structure.
 * @slots: free slots.
 * @cb_pos: callback block.
 * @cl: private data of the file object.
 * @cmpl_list: complete list.
 *
 * returns 0, OK; otherwise, error.
 */
int mei_amthif_irq_write_complete(struct mei_cl *cl, struct mei_cl_cb *cb,
				  s32 *slots, struct mei_cl_cb *cmpl_list)
{
	struct mei_device *dev = cl->dev;
	struct mei_msg_hdr mei_hdr;
	size_t len = dev->iamthif_msg_buf_size - dev->iamthif_msg_buf_index;
	u32 msg_slots = mei_data2slots(len);

	mei_hdr.host_addr = cl->host_client_id;
	mei_hdr.me_addr = cl->me_client_id;
	mei_hdr.reserved = 0;

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

	dev_dbg(&dev->pdev->dev, MEI_HDR_FMT,  MEI_HDR_PRM(&mei_hdr));

	*slots -=  msg_slots;
	if (mei_write_message(dev, &mei_hdr,
		dev->iamthif_msg_buf + dev->iamthif_msg_buf_index)) {
			dev->iamthif_state = MEI_IAMTHIF_IDLE;
			cl->status = -ENODEV;
			list_del(&cb->list);
			return -ENODEV;
	}

	if (mei_cl_flow_ctrl_reduce(cl))
		return -ENODEV;

	dev->iamthif_msg_buf_index += mei_hdr.length;
	cl->status = 0;

	if (mei_hdr.msg_complete) {
		dev->iamthif_state = MEI_IAMTHIF_FLOW_CONTROL;
		dev->iamthif_flow_control_pending = true;

		/* save iamthif cb sent to amthif client */
		cb->buf_idx = dev->iamthif_msg_buf_index;
		dev->iamthif_current_cb = cb;

		list_move_tail(&cb->list, &dev->write_waiting_list.list);
	}


	return 0;
}

/**
 * mei_amthif_irq_read_message - read routine after ISR to
 *			handle the read amthif message
 *
 * @dev: the device structure
 * @mei_hdr: header of amthif message
 * @complete_list: An instance of our list structure
 *
 * returns 0 on success, <0 on failure.
 */
int mei_amthif_irq_read_msg(struct mei_device *dev,
			    struct mei_msg_hdr *mei_hdr,
			    struct mei_cl_cb *complete_list)
{
	struct mei_cl_cb *cb;
	unsigned char *buffer;

	BUG_ON(mei_hdr->me_addr != dev->iamthif_cl.me_client_id);
	BUG_ON(dev->iamthif_state != MEI_IAMTHIF_READING);

	buffer = dev->iamthif_msg_buf + dev->iamthif_msg_buf_index;
	BUG_ON(dev->iamthif_mtu < dev->iamthif_msg_buf_index + mei_hdr->length);

	mei_read_slots(dev, buffer, mei_hdr->length);

	dev->iamthif_msg_buf_index += mei_hdr->length;

	if (!mei_hdr->msg_complete)
		return 0;

	dev_dbg(&dev->pdev->dev, "amthif_message_buffer_index =%d\n",
			mei_hdr->length);

	dev_dbg(&dev->pdev->dev, "completed amthif read.\n ");
	if (!dev->iamthif_current_cb)
		return -ENODEV;

	cb = dev->iamthif_current_cb;
	dev->iamthif_current_cb = NULL;

	if (!cb->cl)
		return -ENODEV;

	dev->iamthif_stall_timer = 0;
	cb->buf_idx = dev->iamthif_msg_buf_index;
	cb->read_time = jiffies;
	if (dev->iamthif_ioctl && cb->cl == &dev->iamthif_cl) {
		/* found the iamthif cb */
		dev_dbg(&dev->pdev->dev, "complete the amthif read cb.\n ");
		dev_dbg(&dev->pdev->dev, "add the amthif read cb to complete.\n ");
		list_add_tail(&cb->list, &complete_list->list);
	}
	return 0;
}

/**
 * mei_amthif_irq_read - prepares to read amthif data.
 *
 * @dev: the device structure.
 * @slots: free slots.
 *
 * returns 0, OK; otherwise, error.
 */
int mei_amthif_irq_read(struct mei_device *dev, s32 *slots)
{
	u32 msg_slots = mei_data2slots(sizeof(struct hbm_flow_control));

	if (*slots < msg_slots)
		return -EMSGSIZE;

	*slots -= msg_slots;

	if (mei_hbm_cl_flow_control_req(dev, &dev->iamthif_cl)) {
		dev_dbg(&dev->pdev->dev, "iamthif flow control failed\n");
		return -EIO;
	}

	dev_dbg(&dev->pdev->dev, "iamthif flow control success\n");
	dev->iamthif_state = MEI_IAMTHIF_READING;
	dev->iamthif_flow_control_pending = false;
	dev->iamthif_msg_buf_index = 0;
	dev->iamthif_msg_buf_size = 0;
	dev->iamthif_stall_timer = MEI_IAMTHIF_STALL_TIMER;
	dev->hbuf_is_ready = mei_hbuf_is_ready(dev);
	return 0;
}

/**
 * mei_amthif_complete - complete amthif callback.
 *
 * @dev: the device structure.
 * @cb_pos: callback block.
 */
void mei_amthif_complete(struct mei_device *dev, struct mei_cl_cb *cb)
{
	if (dev->iamthif_canceled != 1) {
		dev->iamthif_state = MEI_IAMTHIF_READ_COMPLETE;
		dev->iamthif_stall_timer = 0;
		memcpy(cb->response_buffer.data,
				dev->iamthif_msg_buf,
				dev->iamthif_msg_buf_index);
		list_add_tail(&cb->list, &dev->amthif_rd_complete_list.list);
		dev_dbg(&dev->pdev->dev, "amthif read completed\n");
		dev->iamthif_timer = jiffies;
		dev_dbg(&dev->pdev->dev, "dev->iamthif_timer = %ld\n",
				dev->iamthif_timer);
	} else {
		mei_amthif_run_next_cmd(dev);
	}

	dev_dbg(&dev->pdev->dev, "completing amthif call back.\n");
	wake_up_interruptible(&dev->iamthif_cl.wait);
}

/**
 * mei_clear_list - removes all callbacks associated with file
 *		from mei_cb_list
 *
 * @dev: device structure.
 * @file: file structure
 * @mei_cb_list: callbacks list
 *
 * mei_clear_list is called to clear resources associated with file
 * when application calls close function or Ctrl-C was pressed
 *
 * returns true if callback removed from the list, false otherwise
 */
static bool mei_clear_list(struct mei_device *dev,
		const struct file *file, struct list_head *mei_cb_list)
{
	struct mei_cl_cb *cb_pos = NULL;
	struct mei_cl_cb *cb_next = NULL;
	bool removed = false;

	/* list all list member */
	list_for_each_entry_safe(cb_pos, cb_next, mei_cb_list, list) {
		/* check if list member associated with a file */
		if (file == cb_pos->file_object) {
			/* remove member from the list */
			list_del(&cb_pos->list);
			/* check if cb equal to current iamthif cb */
			if (dev->iamthif_current_cb == cb_pos) {
				dev->iamthif_current_cb = NULL;
				/* send flow control to iamthif client */
				mei_hbm_cl_flow_control_req(dev,
							&dev->iamthif_cl);
			}
			/* free all allocated buffers */
			mei_io_cb_free(cb_pos);
			cb_pos = NULL;
			removed = true;
		}
	}
	return removed;
}

/**
 * mei_clear_lists - removes all callbacks associated with file
 *
 * @dev: device structure
 * @file: file structure
 *
 * mei_clear_lists is called to clear resources associated with file
 * when application calls close function or Ctrl-C was pressed
 *
 * returns true if callback removed from the list, false otherwise
 */
static bool mei_clear_lists(struct mei_device *dev, struct file *file)
{
	bool removed = false;

	/* remove callbacks associated with a file */
	mei_clear_list(dev, file, &dev->amthif_cmd_list.list);
	if (mei_clear_list(dev, file, &dev->amthif_rd_complete_list.list))
		removed = true;

	mei_clear_list(dev, file, &dev->ctrl_rd_list.list);

	if (mei_clear_list(dev, file, &dev->ctrl_wr_list.list))
		removed = true;

	if (mei_clear_list(dev, file, &dev->write_waiting_list.list))
		removed = true;

	if (mei_clear_list(dev, file, &dev->write_list.list))
		removed = true;

	/* check if iamthif_current_cb not NULL */
	if (dev->iamthif_current_cb && !removed) {
		/* check file and iamthif current cb association */
		if (dev->iamthif_current_cb->file_object == file) {
			/* remove cb */
			mei_io_cb_free(dev->iamthif_current_cb);
			dev->iamthif_current_cb = NULL;
			removed = true;
		}
	}
	return removed;
}

/**
* mei_amthif_release - the release function
*
*  @dev: device structure
*  @file: pointer to file structure
*
*  returns 0 on success, <0 on error
*/
int mei_amthif_release(struct mei_device *dev, struct file *file)
{
	if (dev->open_handle_count > 0)
		dev->open_handle_count--;

	if (dev->iamthif_file_object == file &&
	    dev->iamthif_state != MEI_IAMTHIF_IDLE) {

		dev_dbg(&dev->pdev->dev, "amthif canceled iamthif state %d\n",
		    dev->iamthif_state);
		dev->iamthif_canceled = true;
		if (dev->iamthif_state == MEI_IAMTHIF_READ_COMPLETE) {
			dev_dbg(&dev->pdev->dev, "run next amthif iamthif cb\n");
			mei_amthif_run_next_cmd(dev);
		}
	}

	if (mei_clear_lists(dev, file))
		dev->iamthif_state = MEI_IAMTHIF_IDLE;

	return 0;
}
