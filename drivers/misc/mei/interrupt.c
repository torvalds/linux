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


#include <linux/export.h>
#include <linux/pci.h>
#include <linux/kthread.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/jiffies.h>

#include <linux/mei.h>

#include "mei_dev.h"
#include "hbm.h"
#include "hw-me.h"
#include "client.h"


/**
 * mei_irq_compl_handler - dispatch complete handlers
 *	for the completed callbacks
 *
 * @dev - mei device
 * @compl_list - list of completed cbs
 */
void mei_irq_compl_handler(struct mei_device *dev, struct mei_cl_cb *compl_list)
{
	struct mei_cl_cb *cb, *next;
	struct mei_cl *cl;

	list_for_each_entry_safe(cb, next, &compl_list->list, list) {
		cl = cb->cl;
		list_del(&cb->list);
		if (!cl)
			continue;

		dev_dbg(&dev->pdev->dev, "completing call back.\n");
		if (cl == &dev->iamthif_cl)
			mei_amthif_complete(dev, cb);
		else
			mei_cl_complete(cl, cb);
	}
}
EXPORT_SYMBOL_GPL(mei_irq_compl_handler);

/**
 * mei_cl_hbm_equal - check if hbm is addressed to the client
 *
 * @cl: host client
 * @mei_hdr: header of mei client message
 *
 * returns true if matches, false otherwise
 */
static inline int mei_cl_hbm_equal(struct mei_cl *cl,
			struct mei_msg_hdr *mei_hdr)
{
	return cl->host_client_id == mei_hdr->host_addr &&
		cl->me_client_id == mei_hdr->me_addr;
}
/**
 * mei_cl_is_reading - checks if the client
		is the one to read this message
 *
 * @cl: mei client
 * @mei_hdr: header of mei message
 *
 * returns true on match and false otherwise
 */
static bool mei_cl_is_reading(struct mei_cl *cl, struct mei_msg_hdr *mei_hdr)
{
	return mei_cl_hbm_equal(cl, mei_hdr) &&
		cl->state == MEI_FILE_CONNECTED &&
		cl->reading_state != MEI_READ_COMPLETE;
}

/**
 * mei_irq_read_client_message - process client message
 *
 * @dev: the device structure
 * @mei_hdr: header of mei client message
 * @complete_list: An instance of our list structure
 *
 * returns 0 on success, <0 on failure.
 */
static int mei_cl_irq_read_msg(struct mei_device *dev,
			       struct mei_msg_hdr *mei_hdr,
			       struct mei_cl_cb *complete_list)
{
	struct mei_cl *cl;
	struct mei_cl_cb *cb, *next;
	unsigned char *buffer = NULL;

	list_for_each_entry_safe(cb, next, &dev->read_list.list, list) {
		cl = cb->cl;
		if (!cl || !mei_cl_is_reading(cl, mei_hdr))
			continue;

		cl->reading_state = MEI_READING;

		if (cb->response_buffer.size == 0 ||
		    cb->response_buffer.data == NULL) {
			cl_err(dev, cl, "response buffer is not allocated.\n");
			list_del(&cb->list);
			return -ENOMEM;
		}

		if (cb->response_buffer.size < mei_hdr->length + cb->buf_idx) {
			cl_dbg(dev, cl, "message overflow. size %d len %d idx %ld\n",
				cb->response_buffer.size,
				mei_hdr->length, cb->buf_idx);
			buffer = krealloc(cb->response_buffer.data,
					  mei_hdr->length + cb->buf_idx,
					  GFP_KERNEL);

			if (!buffer) {
				cl_err(dev, cl, "allocation failed.\n");
				list_del(&cb->list);
				return -ENOMEM;
			}
			cb->response_buffer.data = buffer;
			cb->response_buffer.size =
				mei_hdr->length + cb->buf_idx;
		}

		buffer = cb->response_buffer.data + cb->buf_idx;
		mei_read_slots(dev, buffer, mei_hdr->length);

		cb->buf_idx += mei_hdr->length;
		if (mei_hdr->msg_complete) {
			cl->status = 0;
			list_del(&cb->list);
			cl_dbg(dev, cl, "completed read length = %lu\n",
				cb->buf_idx);
			list_add_tail(&cb->list, &complete_list->list);
		}
		break;
	}

	dev_dbg(&dev->pdev->dev, "message read\n");
	if (!buffer) {
		mei_read_slots(dev, dev->rd_msg_buf, mei_hdr->length);
		dev_dbg(&dev->pdev->dev, "discarding message " MEI_HDR_FMT "\n",
				MEI_HDR_PRM(mei_hdr));
	}

	return 0;
}

/**
 * mei_cl_irq_close - processes close related operation from
 *	interrupt thread context - send disconnect request
 *
 * @cl: client
 * @cb: callback block.
 * @slots: free slots.
 * @cmpl_list: complete list.
 *
 * returns 0, OK; otherwise, error.
 */
static int mei_cl_irq_close(struct mei_cl *cl, struct mei_cl_cb *cb,
			s32 *slots, struct mei_cl_cb *cmpl_list)
{
	struct mei_device *dev = cl->dev;

	u32 msg_slots =
		mei_data2slots(sizeof(struct hbm_client_connect_request));

	if (*slots < msg_slots)
		return -EMSGSIZE;

	*slots -= msg_slots;

	if (mei_hbm_cl_disconnect_req(dev, cl)) {
		cl->status = 0;
		cb->buf_idx = 0;
		list_move_tail(&cb->list, &cmpl_list->list);
		return -EIO;
	}

	cl->state = MEI_FILE_DISCONNECTING;
	cl->status = 0;
	cb->buf_idx = 0;
	list_move_tail(&cb->list, &dev->ctrl_rd_list.list);
	cl->timer_count = MEI_CONNECT_TIMEOUT;

	return 0;
}


/**
 * mei_cl_irq_close - processes client read related operation from the
 *	interrupt thread context - request for flow control credits
 *
 * @cl: client
 * @cb: callback block.
 * @slots: free slots.
 * @cmpl_list: complete list.
 *
 * returns 0, OK; otherwise, error.
 */
static int mei_cl_irq_read(struct mei_cl *cl, struct mei_cl_cb *cb,
			   s32 *slots, struct mei_cl_cb *cmpl_list)
{
	struct mei_device *dev = cl->dev;
	u32 msg_slots = mei_data2slots(sizeof(struct hbm_flow_control));

	int ret;


	if (*slots < msg_slots) {
		/* return the cancel routine */
		list_del(&cb->list);
		return -EMSGSIZE;
	}

	*slots -= msg_slots;

	ret = mei_hbm_cl_flow_control_req(dev, cl);
	if (ret) {
		cl->status = ret;
		cb->buf_idx = 0;
		list_move_tail(&cb->list, &cmpl_list->list);
		return ret;
	}

	list_move_tail(&cb->list, &dev->read_list.list);

	return 0;
}


/**
 * mei_cl_irq_ioctl - processes client ioctl related operation from the
 *	interrupt thread context -   send connection request
 *
 * @cl: client
 * @cb: callback block.
 * @slots: free slots.
 * @cmpl_list: complete list.
 *
 * returns 0, OK; otherwise, error.
 */
static int mei_cl_irq_ioctl(struct mei_cl *cl, struct mei_cl_cb *cb,
			   s32 *slots, struct mei_cl_cb *cmpl_list)
{
	struct mei_device *dev = cl->dev;
	int ret;

	u32 msg_slots =
		mei_data2slots(sizeof(struct hbm_client_connect_request));

	if (*slots < msg_slots) {
		/* return the cancel routine */
		list_del(&cb->list);
		return -EMSGSIZE;
	}

	*slots -=  msg_slots;

	cl->state = MEI_FILE_CONNECTING;

	ret = mei_hbm_cl_connect_req(dev, cl);
	if (ret) {
		cl->status = ret;
		cb->buf_idx = 0;
		list_del(&cb->list);
		return ret;
	}

	list_move_tail(&cb->list, &dev->ctrl_rd_list.list);
	cl->timer_count = MEI_CONNECT_TIMEOUT;
	return 0;
}


/**
 * mei_irq_read_handler - bottom half read routine after ISR to
 * handle the read processing.
 *
 * @dev: the device structure
 * @cmpl_list: An instance of our list structure
 * @slots: slots to read.
 *
 * returns 0 on success, <0 on failure.
 */
int mei_irq_read_handler(struct mei_device *dev,
		struct mei_cl_cb *cmpl_list, s32 *slots)
{
	struct mei_msg_hdr *mei_hdr;
	struct mei_cl *cl;
	int ret;

	if (!dev->rd_msg_hdr) {
		dev->rd_msg_hdr = mei_read_hdr(dev);
		(*slots)--;
		dev_dbg(&dev->pdev->dev, "slots =%08x.\n", *slots);
	}
	mei_hdr = (struct mei_msg_hdr *) &dev->rd_msg_hdr;
	dev_dbg(&dev->pdev->dev, MEI_HDR_FMT, MEI_HDR_PRM(mei_hdr));

	if (mei_hdr->reserved || !dev->rd_msg_hdr) {
		dev_err(&dev->pdev->dev, "corrupted message header 0x%08X\n",
				dev->rd_msg_hdr);
		ret = -EBADMSG;
		goto end;
	}

	if (mei_slots2data(*slots) < mei_hdr->length) {
		dev_err(&dev->pdev->dev, "less data available than length=%08x.\n",
				*slots);
		/* we can't read the message */
		ret = -ERANGE;
		goto end;
	}

	/*  HBM message */
	if (mei_hdr->host_addr == 0 && mei_hdr->me_addr == 0) {
		ret = mei_hbm_dispatch(dev, mei_hdr);
		if (ret) {
			dev_dbg(&dev->pdev->dev, "mei_hbm_dispatch failed ret = %d\n",
					ret);
			goto end;
		}
		goto reset_slots;
	}

	/* find recipient cl */
	list_for_each_entry(cl, &dev->file_list, link) {
		if (mei_cl_hbm_equal(cl, mei_hdr)) {
			cl_dbg(dev, cl, "got a message\n");
			break;
		}
	}

	/* if no recipient cl was found we assume corrupted header */
	if (&cl->link == &dev->file_list) {
		dev_err(&dev->pdev->dev, "no destination client found 0x%08X\n",
				dev->rd_msg_hdr);
		ret = -EBADMSG;
		goto end;
	}

	if (mei_hdr->host_addr == dev->iamthif_cl.host_client_id &&
	    MEI_FILE_CONNECTED == dev->iamthif_cl.state &&
	    dev->iamthif_state == MEI_IAMTHIF_READING) {

		ret = mei_amthif_irq_read_msg(dev, mei_hdr, cmpl_list);
		if (ret) {
			dev_err(&dev->pdev->dev, "mei_amthif_irq_read_msg failed = %d\n",
					ret);
			goto end;
		}
	} else {
		ret = mei_cl_irq_read_msg(dev, mei_hdr, cmpl_list);
		if (ret) {
			dev_err(&dev->pdev->dev, "mei_cl_irq_read_msg failed = %d\n",
					ret);
			goto end;
		}
	}

reset_slots:
	/* reset the number of slots and header */
	*slots = mei_count_full_read_slots(dev);
	dev->rd_msg_hdr = 0;

	if (*slots == -EOVERFLOW) {
		/* overflow - reset */
		dev_err(&dev->pdev->dev, "resetting due to slots overflow.\n");
		/* set the event since message has been read */
		ret = -ERANGE;
		goto end;
	}
end:
	return ret;
}
EXPORT_SYMBOL_GPL(mei_irq_read_handler);


/**
 * mei_irq_write_handler -  dispatch write requests
 *  after irq received
 *
 * @dev: the device structure
 * @cmpl_list: An instance of our list structure
 *
 * returns 0 on success, <0 on failure.
 */
int mei_irq_write_handler(struct mei_device *dev, struct mei_cl_cb *cmpl_list)
{

	struct mei_cl *cl;
	struct mei_cl_cb *cb, *next;
	struct mei_cl_cb *list;
	s32 slots;
	int ret;

	if (!mei_hbuf_is_ready(dev)) {
		dev_dbg(&dev->pdev->dev, "host buffer is not empty.\n");
		return 0;
	}
	slots = mei_hbuf_empty_slots(dev);
	if (slots <= 0)
		return -EMSGSIZE;

	/* complete all waiting for write CB */
	dev_dbg(&dev->pdev->dev, "complete all waiting for write cb.\n");

	list = &dev->write_waiting_list;
	list_for_each_entry_safe(cb, next, &list->list, list) {
		cl = cb->cl;
		if (cl == NULL)
			continue;

		cl->status = 0;
		list_del(&cb->list);
		if (MEI_WRITING == cl->writing_state &&
		    cb->fop_type == MEI_FOP_WRITE &&
		    cl != &dev->iamthif_cl) {
			cl_dbg(dev, cl, "MEI WRITE COMPLETE\n");
			cl->writing_state = MEI_WRITE_COMPLETE;
			list_add_tail(&cb->list, &cmpl_list->list);
		}
		if (cl == &dev->iamthif_cl) {
			cl_dbg(dev, cl, "check iamthif flow control.\n");
			if (dev->iamthif_flow_control_pending) {
				ret = mei_amthif_irq_read(dev, &slots);
				if (ret)
					return ret;
			}
		}
	}

	if (dev->wd_state == MEI_WD_STOPPING) {
		dev->wd_state = MEI_WD_IDLE;
		wake_up_interruptible(&dev->wait_stop_wd);
	}

	if (dev->wr_ext_msg.hdr.length) {
		mei_write_message(dev, &dev->wr_ext_msg.hdr,
				dev->wr_ext_msg.data);
		slots -= mei_data2slots(dev->wr_ext_msg.hdr.length);
		dev->wr_ext_msg.hdr.length = 0;
	}
	if (dev->dev_state == MEI_DEV_ENABLED) {
		if (dev->wd_pending &&
		    mei_cl_flow_ctrl_creds(&dev->wd_cl) > 0) {
			if (mei_wd_send(dev))
				dev_dbg(&dev->pdev->dev, "wd send failed.\n");
			else if (mei_cl_flow_ctrl_reduce(&dev->wd_cl))
				return -ENODEV;

			dev->wd_pending = false;

			if (dev->wd_state == MEI_WD_RUNNING)
				slots -= mei_data2slots(MEI_WD_START_MSG_SIZE);
			else
				slots -= mei_data2slots(MEI_WD_STOP_MSG_SIZE);
		}
	}

	/* complete control write list CB */
	dev_dbg(&dev->pdev->dev, "complete control write list cb.\n");
	list_for_each_entry_safe(cb, next, &dev->ctrl_wr_list.list, list) {
		cl = cb->cl;
		if (!cl) {
			list_del(&cb->list);
			return -ENODEV;
		}
		switch (cb->fop_type) {
		case MEI_FOP_CLOSE:
			/* send disconnect message */
			ret = mei_cl_irq_close(cl, cb, &slots, cmpl_list);
			if (ret)
				return ret;

			break;
		case MEI_FOP_READ:
			/* send flow control message */
			ret = mei_cl_irq_read(cl, cb, &slots, cmpl_list);
			if (ret)
				return ret;

			break;
		case MEI_FOP_IOCTL:
			/* connect message */
			if (mei_cl_is_other_connecting(cl))
				continue;
			ret = mei_cl_irq_ioctl(cl, cb, &slots, cmpl_list);
			if (ret)
				return ret;

			break;

		default:
			BUG();
		}

	}
	/* complete  write list CB */
	dev_dbg(&dev->pdev->dev, "complete write list cb.\n");
	list_for_each_entry_safe(cb, next, &dev->write_list.list, list) {
		cl = cb->cl;
		if (cl == NULL)
			continue;
		if (cl == &dev->iamthif_cl)
			ret = mei_amthif_irq_write_complete(cl, cb,
						&slots, cmpl_list);
		else
			ret = mei_cl_irq_write_complete(cl, cb,
						&slots, cmpl_list);
		if (ret)
			return ret;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(mei_irq_write_handler);



/**
 * mei_timer - timer function.
 *
 * @work: pointer to the work_struct structure
 *
 */
void mei_timer(struct work_struct *work)
{
	unsigned long timeout;
	struct mei_cl *cl_pos = NULL;
	struct mei_cl *cl_next = NULL;
	struct mei_cl_cb  *cb_pos = NULL;
	struct mei_cl_cb  *cb_next = NULL;

	struct mei_device *dev = container_of(work,
					struct mei_device, timer_work.work);


	mutex_lock(&dev->device_lock);

	/* Catch interrupt stalls during HBM init handshake */
	if (dev->dev_state == MEI_DEV_INIT_CLIENTS &&
	    dev->hbm_state != MEI_HBM_IDLE) {

		if (dev->init_clients_timer) {
			if (--dev->init_clients_timer == 0) {
				dev_err(&dev->pdev->dev, "timer: init clients timeout hbm_state = %d.\n",
					dev->hbm_state);
				mei_reset(dev);
				goto out;
			}
		}
	}

	if (dev->dev_state != MEI_DEV_ENABLED)
		goto out;

	/*** connect/disconnect timeouts ***/
	list_for_each_entry_safe(cl_pos, cl_next, &dev->file_list, link) {
		if (cl_pos->timer_count) {
			if (--cl_pos->timer_count == 0) {
				dev_err(&dev->pdev->dev, "timer: connect/disconnect timeout.\n");
				mei_reset(dev);
				goto out;
			}
		}
	}

	if (dev->iamthif_stall_timer) {
		if (--dev->iamthif_stall_timer == 0) {
			dev_err(&dev->pdev->dev, "timer: amthif  hanged.\n");
			mei_reset(dev);
			dev->iamthif_msg_buf_size = 0;
			dev->iamthif_msg_buf_index = 0;
			dev->iamthif_canceled = false;
			dev->iamthif_ioctl = true;
			dev->iamthif_state = MEI_IAMTHIF_IDLE;
			dev->iamthif_timer = 0;

			mei_io_cb_free(dev->iamthif_current_cb);
			dev->iamthif_current_cb = NULL;

			dev->iamthif_file_object = NULL;
			mei_amthif_run_next_cmd(dev);
		}
	}

	if (dev->iamthif_timer) {

		timeout = dev->iamthif_timer +
			mei_secs_to_jiffies(MEI_IAMTHIF_READ_TIMER);

		dev_dbg(&dev->pdev->dev, "dev->iamthif_timer = %ld\n",
				dev->iamthif_timer);
		dev_dbg(&dev->pdev->dev, "timeout = %ld\n", timeout);
		dev_dbg(&dev->pdev->dev, "jiffies = %ld\n", jiffies);
		if (time_after(jiffies, timeout)) {
			/*
			 * User didn't read the AMTHI data on time (15sec)
			 * freeing AMTHI for other requests
			 */

			dev_dbg(&dev->pdev->dev, "freeing AMTHI for other requests\n");

			list_for_each_entry_safe(cb_pos, cb_next,
				&dev->amthif_rd_complete_list.list, list) {

				cl_pos = cb_pos->file_object->private_data;

				/* Finding the AMTHI entry. */
				if (cl_pos == &dev->iamthif_cl)
					list_del(&cb_pos->list);
			}
			mei_io_cb_free(dev->iamthif_current_cb);
			dev->iamthif_current_cb = NULL;

			dev->iamthif_file_object->private_data = NULL;
			dev->iamthif_file_object = NULL;
			dev->iamthif_timer = 0;
			mei_amthif_run_next_cmd(dev);

		}
	}
out:
	if (dev->dev_state != MEI_DEV_DISABLED)
		schedule_delayed_work(&dev->timer_work, 2 * HZ);
	mutex_unlock(&dev->device_lock);
}

