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
	dev->iamthif_canceled = false;
	dev->iamthif_state = MEI_IAMTHIF_IDLE;
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

	mutex_lock(&dev->device_lock);

	if (mei_cl_is_connected(cl)) {
		ret = 0;
		goto out;
	}

	dev->iamthif_state = MEI_IAMTHIF_IDLE;

	mei_cl_init(cl, dev);

	ret = mei_cl_link(cl);
	if (ret < 0) {
		dev_err(dev->dev, "amthif: failed cl_link %d\n", ret);
		goto out;
	}

	ret = mei_cl_connect(cl, me_cl, NULL);

out:
	mutex_unlock(&dev->device_lock);
	return ret;
}

/**
 * mei_amthif_read_start - queue message for sending read credential
 *
 * @cl: host client
 * @fp: file pointer of message recipient
 *
 * Return: 0 on success, <0 on failure.
 */
static int mei_amthif_read_start(struct mei_cl *cl, const struct file *fp)
{
	struct mei_device *dev = cl->dev;
	struct mei_cl_cb *cb;

	cb = mei_cl_enqueue_ctrl_wr_cb(cl, mei_cl_mtu(cl), MEI_FOP_READ, fp);
	if (!cb)
		return -ENOMEM;

	cl->rx_flow_ctrl_creds++;

	dev->iamthif_state = MEI_IAMTHIF_READING;
	cl->fp = cb->fp;

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
	int ret;

	dev->iamthif_canceled = false;

	dev_dbg(dev->dev, "complete amthif cmd_list cb.\n");

	cb = list_first_entry_or_null(&dev->amthif_cmd_list.list,
					typeof(*cb), list);
	if (!cb) {
		dev->iamthif_state = MEI_IAMTHIF_IDLE;
		cl->fp = NULL;
		return 0;
	}

	list_del_init(&cb->list);
	dev->iamthif_state = MEI_IAMTHIF_WRITING;
	cl->fp = cb->fp;

	ret = mei_cl_write(cl, cb, false);
	if (ret < 0)
		return ret;

	if (cb->completed)
		cb->status = mei_amthif_read_start(cl, cb->fp);

	return 0;
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

	struct mei_device *dev = cl->dev;

	list_add_tail(&cb->list, &dev->amthif_cmd_list.list);

	/*
	 * The previous request is still in processing, queue this one.
	 */
	if (dev->iamthif_state != MEI_IAMTHIF_IDLE)
		return 0;

	return mei_amthif_run_next_cmd(dev);
}

/**
 * mei_amthif_poll - the amthif poll function
 *
 * @file: pointer to file structure
 * @wait: pointer to poll_table structure
 *
 * Return: poll mask
 *
 * Locking: called under "dev->device_lock" lock
 */
unsigned int mei_amthif_poll(struct file *file, poll_table *wait)
{
	struct mei_cl *cl = file->private_data;
	struct mei_cl_cb *cb = mei_cl_read_cb(cl, file);
	unsigned int mask = 0;

	poll_wait(file, &cl->rx_wait, wait);
	if (cb)
		mask |= POLLIN | POLLRDNORM;

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
		cb->status = mei_amthif_read_start(cl, cb->fp);

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
	dev->iamthif_stall_timer = 0;

	return 0;
}

/**
 * mei_amthif_complete - complete amthif callback.
 *
 * @cl: host client
 * @cb: callback block.
 */
void mei_amthif_complete(struct mei_cl *cl, struct mei_cl_cb *cb)
{
	struct mei_device *dev = cl->dev;

	dev_dbg(dev->dev, "completing amthif call back.\n");
	switch (cb->fop_type) {
	case MEI_FOP_WRITE:
		if (!cb->status) {
			dev->iamthif_stall_timer = MEI_IAMTHIF_STALL_TIMER;
			mei_schedule_stall_timer(dev);
			mei_io_cb_free(cb);
			return;
		}
		dev->iamthif_state = MEI_IAMTHIF_IDLE;
		cl->fp = NULL;
		if (!dev->iamthif_canceled) {
			/*
			 * in case of error enqueue the write cb to complete
			 * read list so it can be propagated to the reader
			 */
			list_add_tail(&cb->list, &cl->rd_completed);
			wake_up_interruptible(&cl->rx_wait);
		} else {
			mei_io_cb_free(cb);
		}
		break;
	case MEI_FOP_READ:
		if (!dev->iamthif_canceled) {
			list_add_tail(&cb->list, &cl->rd_completed);
			dev_dbg(dev->dev, "amthif read completed\n");
			wake_up_interruptible(&cl->rx_wait);
		} else {
			mei_io_cb_free(cb);
		}

		dev->iamthif_stall_timer = 0;
		mei_amthif_run_next_cmd(dev);
		break;
	default:
		WARN_ON(1);
	}
}

/**
 * mei_clear_list - removes all callbacks associated with file
 *		from mei_cb_list
 *
 * @file: file structure
 * @mei_cb_list: callbacks list
 *
 * mei_clear_list is called to clear resources associated with file
 * when application calls close function or Ctrl-C was pressed
 */
static void mei_clear_list(const struct file *file,
			   struct list_head *mei_cb_list)
{
	struct mei_cl_cb *cb, *next;

	list_for_each_entry_safe(cb, next, mei_cb_list, list)
		if (file == cb->fp)
			mei_io_cb_free(cb);
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
	struct mei_cl *cl = file->private_data;

	if (dev->iamthif_open_count > 0)
		dev->iamthif_open_count--;

	if (cl->fp == file && dev->iamthif_state != MEI_IAMTHIF_IDLE) {

		dev_dbg(dev->dev, "amthif canceled iamthif state %d\n",
		    dev->iamthif_state);
		dev->iamthif_canceled = true;
	}

	mei_clear_list(file, &dev->amthif_cmd_list.list);
	mei_clear_list(file, &cl->rd_completed);
	mei_clear_list(file, &dev->ctrl_rd_list.list);

	return 0;
}
