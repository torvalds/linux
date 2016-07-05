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
#include <linux/ioctl.h>
#include <linux/cdev.h>
#include <linux/list.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/uuid.h>
#include <linux/jiffies.h>
#include <linux/uaccess.h>
#include <linux/slab.h>

#include <linux/mei.h>

#include "mei_dev.h"
#include "hbm.h"
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
	dev->iamthif_canceled = false;
	dev->iamthif_state = MEI_IAMTHIF_IDLE;
	dev->iamthif_timer = 0;
	dev->iamthif_stall_timer = 0;
	dev->iamthif_open_count = 0;
}

/**
 * mei_amthif_host_init - mei initialization amthif client.
 *
 * @dev: the device structure
 * @me_cl: me client
 *
 * Return: 0 on success, <0 on failure.
 */
int mei_amthif_host_init(struct mei_device *dev, struct mei_me_client *me_cl)
{
	struct mei_cl *cl = &dev->iamthif_cl;
	int ret;

	dev->iamthif_state = MEI_IAMTHIF_IDLE;

	mei_cl_init(cl, dev);

	ret = mei_cl_link(cl, MEI_IAMTHIF_HOST_CLIENT_ID);
	if (ret < 0) {
		dev_err(dev->dev, "amthif: failed cl_link %d\n", ret);
		return ret;
	}

	ret = mei_cl_connect(cl, me_cl, NULL);

	dev->iamthif_state = MEI_IAMTHIF_IDLE;

	return ret;
}

/**
 * mei_amthif_find_read_list_entry - finds a amthilist entry for current file
 *
 * @dev: the device structure
 * @file: pointer to file object
 *
 * Return:   returned a list entry on success, NULL on failure.
 */
struct mei_cl_cb *mei_amthif_find_read_list_entry(struct mei_device *dev,
						struct file *file)
{
	struct mei_cl_cb *cb;

	list_for_each_entry(cb, &dev->amthif_rd_complete_list.list, list)
		if (cb->file_object == file)
			return cb;
	return NULL;
}


/**
 * mei_amthif_read - read data from AMTHIF client
 *
 * @dev: the device structure
 * @file: pointer to file object
 * @ubuf: pointer to user data in user space
 * @length: data length to read
 * @offset: data read offset
 *
 * Locking: called under "dev->device_lock" lock
 *
 * Return:
 *  returned data length on success,
 *  zero if no data to read,
 *  negative on failure.
 */
int mei_amthif_read(struct mei_device *dev, struct file *file,
	       char __user *ubuf, size_t length, loff_t *offset)
{
	struct mei_cl *cl = file->private_data;
	struct mei_cl_cb *cb;
	unsigned long timeout;
	int rets;
	int wait_ret;

	/* Only possible if we are in timeout */
	if (!cl) {
		dev_err(dev->dev, "bad file ext.\n");
		return -ETIME;
	}

	dev_dbg(dev->dev, "checking amthif data\n");
	cb = mei_amthif_find_read_list_entry(dev, file);

	/* Check for if we can block or not*/
	if (cb == NULL && file->f_flags & O_NONBLOCK)
		return -EAGAIN;


	dev_dbg(dev->dev, "waiting for amthif data\n");
	while (cb == NULL) {
		/* unlock the Mutex */
		mutex_unlock(&dev->device_lock);

		wait_ret = wait_event_interruptible(dev->iamthif_cl.wait,
			(cb = mei_amthif_find_read_list_entry(dev, file)));

		/* Locking again the Mutex */
		mutex_lock(&dev->device_lock);

		if (wait_ret)
			return -ERESTARTSYS;

		dev_dbg(dev->dev, "woke up from sleep\n");
	}

	if (cb->status) {
		rets = cb->status;
		dev_dbg(dev->dev, "read operation failed %d\n", rets);
		goto free;
	}

	dev_dbg(dev->dev, "Got amthif data\n");
	dev->iamthif_timer = 0;

	timeout = cb->read_time +
		mei_secs_to_jiffies(MEI_IAMTHIF_READ_TIMER);
	dev_dbg(dev->dev, "amthif timeout = %lud\n",
			timeout);

	if  (time_after(jiffies, timeout)) {
		dev_dbg(dev->dev, "amthif Time out\n");
		/* 15 sec for the message has expired */
		list_del_init(&cb->list);
		rets = -ETIME;
		goto free;
	}
	/* if the whole message will fit remove it from the list */
	if (cb->buf_idx >= *offset && length >= (cb->buf_idx - *offset))
		list_del_init(&cb->list);
	else if (cb->buf_idx > 0 && cb->buf_idx <= *offset) {
		/* end of the message has been reached */
		list_del_init(&cb->list);
		rets = 0;
		goto free;
	}
		/* else means that not full buffer will be read and do not
		 * remove message from deletion list
		 */

	dev_dbg(dev->dev, "amthif cb->buf size - %d\n",
	    cb->buf.size);
	dev_dbg(dev->dev, "amthif cb->buf_idx - %lu\n", cb->buf_idx);

	/* length is being truncated to PAGE_SIZE, however,
	 * the buf_idx may point beyond */
	length = min_t(size_t, length, (cb->buf_idx - *offset));

	if (copy_to_user(ubuf, cb->buf.data + *offset, length)) {
		dev_dbg(dev->dev, "failed to copy data to userland\n");
		rets = -EFAULT;
	} else {
		rets = length;
		if ((*offset + length) < cb->buf_idx) {
			*offset += length;
			goto out;
		}
	}
free:
	dev_dbg(dev->dev, "free amthif cb memory.\n");
	*offset = 0;
	mei_io_cb_free(cb);
out:
	return rets;
}

/**
 * mei_amthif_read_start - queue message for sending read credential
 *
 * @cl: host client
 * @file: file pointer of message recipient
 *
 * Return: 0 on success, <0 on failure.
 */
static int mei_amthif_read_start(struct mei_cl *cl, struct file *file)
{
	struct mei_device *dev = cl->dev;
	struct mei_cl_cb *cb;
	int rets;

	cb = mei_io_cb_init(cl, MEI_FOP_READ, file);
	if (!cb) {
		rets = -ENOMEM;
		goto err;
	}

	rets = mei_io_cb_alloc_buf(cb, mei_cl_mtu(cl));
	if (rets)
		goto err;

	list_add_tail(&cb->list, &dev->ctrl_wr_list.list);

	dev->iamthif_state = MEI_IAMTHIF_READING;
	dev->iamthif_file_object = cb->file_object;
	dev->iamthif_current_cb = cb;

	return 0;
err:
	mei_io_cb_free(cb);
	return rets;
}

/**
 * mei_amthif_send_cmd - send amthif command to the ME
 *
 * @cl: the host client
 * @cb: mei call back struct
 *
 * Return: 0 on success, <0 on failure.
 */
static int mei_amthif_send_cmd(struct mei_cl *cl, struct mei_cl_cb *cb)
{
	struct mei_device *dev;
	int ret;

	if (!cl->dev || !cb)
		return -ENODEV;

	dev = cl->dev;

	dev->iamthif_state = MEI_IAMTHIF_WRITING;
	dev->iamthif_current_cb = cb;
	dev->iamthif_file_object = cb->file_object;
	dev->iamthif_canceled = false;

	ret = mei_cl_write(cl, cb, false);
	if (ret < 0)
		return ret;

	if (cb->completed)
		cb->status = mei_amthif_read_start(cl, cb->file_object);

	return 0;
}

/**
 * mei_amthif_run_next_cmd - send next amt command from queue
 *
 * @dev: the device structure
 *
 * Return: 0 on success, <0 on failure.
 */
int mei_amthif_run_next_cmd(struct mei_device *dev)
{
	struct mei_cl *cl = &dev->iamthif_cl;
	struct mei_cl_cb *cb;

	dev->iamthif_canceled = false;
	dev->iamthif_state = MEI_IAMTHIF_IDLE;
	dev->iamthif_timer = 0;
	dev->iamthif_file_object = NULL;

	dev_dbg(dev->dev, "complete amthif cmd_list cb.\n");

	cb = list_first_entry_or_null(&dev->amthif_cmd_list.list,
					typeof(*cb), list);
	if (!cb)
		return 0;

	list_del_init(&cb->list);
	return mei_amthif_send_cmd(cl, cb);
}

/**
 * mei_amthif_write - write amthif data to amthif client
 *
 * @cl: host client
 * @cb: mei call back struct
 *
 * Return: 0 on success, <0 on failure.
 */
int mei_amthif_write(struct mei_cl *cl, struct mei_cl_cb *cb)
{

	struct mei_device *dev;

	if (WARN_ON(!cl || !cl->dev))
		return -ENODEV;

	if (WARN_ON(!cb))
		return -EINVAL;

	dev = cl->dev;

	list_add_tail(&cb->list, &dev->amthif_cmd_list.list);
	return mei_amthif_run_next_cmd(dev);
}

/**
 * mei_amthif_poll - the amthif poll function
 *
 * @dev: the device structure
 * @file: pointer to file structure
 * @wait: pointer to poll_table structure
 *
 * Return: poll mask
 *
 * Locking: called under "dev->device_lock" lock
 */

unsigned int mei_amthif_poll(struct mei_device *dev,
		struct file *file, poll_table *wait)
{
	unsigned int mask = 0;

	poll_wait(file, &dev->iamthif_cl.wait, wait);

	if (dev->iamthif_state == MEI_IAMTHIF_READ_COMPLETE &&
	    dev->iamthif_file_object == file) {

		mask |= POLLIN | POLLRDNORM;
		mei_amthif_run_next_cmd(dev);
	}

	return mask;
}



/**
 * mei_amthif_irq_write - write iamthif command in irq thread context.
 *
 * @cl: private data of the file object.
 * @cb: callback block.
 * @cmpl_list: complete list.
 *
 * Return: 0, OK; otherwise, error.
 */
int mei_amthif_irq_write(struct mei_cl *cl, struct mei_cl_cb *cb,
			 struct mei_cl_cb *cmpl_list)
{
	int ret;

	ret = mei_cl_irq_write(cl, cb, cmpl_list);
	if (ret)
		return ret;

	if (cb->completed)
		cb->status = mei_amthif_read_start(cl, cb->file_object);

	return 0;
}

/**
 * mei_amthif_irq_read_msg - read routine after ISR to
 *			handle the read amthif message
 *
 * @cl: mei client
 * @mei_hdr: header of amthif message
 * @cmpl_list: completed callbacks list
 *
 * Return: -ENODEV if cb is NULL 0 otherwise; error message is in cb->status
 */
int mei_amthif_irq_read_msg(struct mei_cl *cl,
			    struct mei_msg_hdr *mei_hdr,
			    struct mei_cl_cb *cmpl_list)
{
	struct mei_device *dev;
	int ret;

	dev = cl->dev;

	if (dev->iamthif_state != MEI_IAMTHIF_READING) {
		mei_irq_discard_msg(dev, mei_hdr);
		return 0;
	}

	ret = mei_cl_irq_read_msg(cl, mei_hdr, cmpl_list);
	if (ret)
		return ret;

	if (!mei_hdr->msg_complete)
		return 0;

	dev_dbg(dev->dev, "completed amthif read.\n ");
	dev->iamthif_current_cb = NULL;
	dev->iamthif_stall_timer = 0;

	return 0;
}

/**
 * mei_amthif_complete - complete amthif callback.
 *
 * @dev: the device structure.
 * @cb: callback block.
 */
void mei_amthif_complete(struct mei_device *dev, struct mei_cl_cb *cb)
{

	if (cb->fop_type == MEI_FOP_WRITE) {
		if (!cb->status) {
			dev->iamthif_stall_timer = MEI_IAMTHIF_STALL_TIMER;
			mei_io_cb_free(cb);
			return;
		}
		/*
		 * in case of error enqueue the write cb to complete read list
		 * so it can be propagated to the reader
		 */
		list_add_tail(&cb->list, &dev->amthif_rd_complete_list.list);
		wake_up_interruptible(&dev->iamthif_cl.wait);
		return;
	}

	if (!dev->iamthif_canceled) {
		dev->iamthif_state = MEI_IAMTHIF_READ_COMPLETE;
		dev->iamthif_stall_timer = 0;
		list_add_tail(&cb->list, &dev->amthif_rd_complete_list.list);
		dev_dbg(dev->dev, "amthif read completed\n");
		dev->iamthif_timer = jiffies;
		dev_dbg(dev->dev, "dev->iamthif_timer = %ld\n",
			dev->iamthif_timer);
	} else {
		mei_amthif_run_next_cmd(dev);
	}

	dev_dbg(dev->dev, "completing amthif call back.\n");
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
 * Return: true if callback removed from the list, false otherwise
 */
static bool mei_clear_list(struct mei_device *dev,
		const struct file *file, struct list_head *mei_cb_list)
{
	struct mei_cl *cl = &dev->iamthif_cl;
	struct mei_cl_cb *cb, *next;
	bool removed = false;

	/* list all list member */
	list_for_each_entry_safe(cb, next, mei_cb_list, list) {
		/* check if list member associated with a file */
		if (file == cb->file_object) {
			/* check if cb equal to current iamthif cb */
			if (dev->iamthif_current_cb == cb) {
				dev->iamthif_current_cb = NULL;
				/* send flow control to iamthif client */
				mei_hbm_cl_flow_control_req(dev, cl);
			}
			/* free all allocated buffers */
			mei_io_cb_free(cb);
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
 * Return: true if callback removed from the list, false otherwise
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
*  Return: 0 on success, <0 on error
*/
int mei_amthif_release(struct mei_device *dev, struct file *file)
{
	if (dev->iamthif_open_count > 0)
		dev->iamthif_open_count--;

	if (dev->iamthif_file_object == file &&
	    dev->iamthif_state != MEI_IAMTHIF_IDLE) {

		dev_dbg(dev->dev, "amthif canceled iamthif state %d\n",
		    dev->iamthif_state);
		dev->iamthif_canceled = true;
		if (dev->iamthif_state == MEI_IAMTHIF_READ_COMPLETE) {
			dev_dbg(dev->dev, "run next amthif iamthif cb\n");
			mei_amthif_run_next_cmd(dev);
		}
	}

	if (mei_clear_lists(dev, file))
		dev->iamthif_state = MEI_IAMTHIF_IDLE;

	return 0;
}
