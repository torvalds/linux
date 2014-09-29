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

		dev_dbg(dev->dev, "completing call back.\n");
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

	dev_dbg(dev->dev, "message read\n");
	if (!buffer) {
		mei_read_slots(dev, dev->rd_msg_buf, mei_hdr->length);
		dev_dbg(dev->dev, "discarding message " MEI_HDR_FMT "\n",
				MEI_HDR_PRM(mei_hdr));
	}

	return 0;
}

/**
 * mei_cl_irq_disconnect_rsp - send disconnection response message
 *
 * @cl: client
 * @cb: callback block.
 * @cmpl_list: complete list.
 *
 * returns 0, OK; otherwise, error.
 */
static int mei_cl_irq_disconnect_rsp(struct mei_cl *cl, struct mei_cl_cb *cb,
				     struct mei_cl_cb *cmpl_list)
{
	struct mei_device *dev = cl->dev;
	u32 msg_slots;
	int slots;
	int ret;

	slots = mei_hbuf_empty_slots(dev);
	msg_slots = mei_data2slots(sizeof(struct hbm_client_connect_response));

	if (slots < msg_slots)
		return -EMSGSIZE;

	ret = mei_hbm_cl_disconnect_rsp(dev, cl);

	cl->state = MEI_FILE_DISCONNECTED;
	cl->status = 0;
	list_del(&cb->list);
	mei_io_cb_free(cb);

	return ret;
}



/**
 * mei_cl_irq_disconnect - processes close related operation from
 *	interrupt thread context - send disconnect request
 *
 * @cl: client
 * @cb: callback block.
 * @cmpl_list: complete list.
 *
 * returns 0, OK; otherwise, error.
 */
static int mei_cl_irq_disconnect(struct mei_cl *cl, struct mei_cl_cb *cb,
			    struct mei_cl_cb *cmpl_list)
{
	struct mei_device *dev = cl->dev;
	u32 msg_slots;
	int slots;

	msg_slots = mei_data2slots(sizeof(struct hbm_client_connect_request));
	slots = mei_hbuf_empty_slots(dev);

	if (slots < msg_slots)
		return -EMSGSIZE;

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
 * @cmpl_list: complete list.
 *
 * returns 0, OK; otherwise, error.
 */
static int mei_cl_irq_read(struct mei_cl *cl, struct mei_cl_cb *cb,
			   struct mei_cl_cb *cmpl_list)
{
	struct mei_device *dev = cl->dev;
	u32 msg_slots;
	int slots;
	int ret;

	msg_slots = mei_data2slots(sizeof(struct hbm_flow_control));
	slots = mei_hbuf_empty_slots(dev);

	if (slots < msg_slots)
		return -EMSGSIZE;

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
 * mei_cl_irq_connect - send connect request in irq_thread context
 *
 * @cl: client
 * @cb: callback block.
 * @cmpl_list: complete list.
 *
 * returns 0, OK; otherwise, error.
 */
static int mei_cl_irq_connect(struct mei_cl *cl, struct mei_cl_cb *cb,
			      struct mei_cl_cb *cmpl_list)
{
	struct mei_device *dev = cl->dev;
	u32 msg_slots;
	int slots;
	int ret;

	msg_slots = mei_data2slots(sizeof(struct hbm_client_connect_request));
	slots = mei_hbuf_empty_slots(dev);

	if (mei_cl_is_other_connecting(cl))
		return 0;

	if (slots < msg_slots)
		return -EMSGSIZE;

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
		dev_dbg(dev->dev, "slots =%08x.\n", *slots);
	}
	mei_hdr = (struct mei_msg_hdr *) &dev->rd_msg_hdr;
	dev_dbg(dev->dev, MEI_HDR_FMT, MEI_HDR_PRM(mei_hdr));

	if (mei_hdr->reserved || !dev->rd_msg_hdr) {
		dev_err(dev->dev, "corrupted message header 0x%08X\n",
				dev->rd_msg_hdr);
		ret = -EBADMSG;
		goto end;
	}

	if (mei_slots2data(*slots) < mei_hdr->length) {
		dev_err(dev->dev, "less data available than length=%08x.\n",
				*slots);
		/* we can't read the message */
		ret = -ENODATA;
		goto end;
	}

	/*  HBM message */
	if (mei_hdr->host_addr == 0 && mei_hdr->me_addr == 0) {
		ret = mei_hbm_dispatch(dev, mei_hdr);
		if (ret) {
			dev_dbg(dev->dev, "mei_hbm_dispatch failed ret = %d\n",
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
		dev_err(dev->dev, "no destination client found 0x%08X\n",
				dev->rd_msg_hdr);
		ret = -EBADMSG;
		goto end;
	}

	if (mei_hdr->host_addr == dev->iamthif_cl.host_client_id &&
	    MEI_FILE_CONNECTED == dev->iamthif_cl.state &&
	    dev->iamthif_state == MEI_IAMTHIF_READING) {

		ret = mei_amthif_irq_read_msg(dev, mei_hdr, cmpl_list);
		if (ret) {
			dev_err(dev->dev, "mei_amthif_irq_read_msg failed = %d\n",
					ret);
			goto end;
		}
	} else {
		ret = mei_cl_irq_read_msg(dev, mei_hdr, cmpl_list);
		if (ret) {
			dev_err(dev->dev, "mei_cl_irq_read_msg failed = %d\n",
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
		dev_err(dev->dev, "resetting due to slots overflow.\n");
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


	if (!mei_hbuf_acquire(dev))
		return 0;

	slots = mei_hbuf_empty_slots(dev);
	if (slots <= 0)
		return -EMSGSIZE;

	/* complete all waiting for write CB */
	dev_dbg(dev->dev, "complete all waiting for write cb.\n");

	list = &dev->write_waiting_list;
	list_for_each_entry_safe(cb, next, &list->list, list) {
		cl = cb->cl;
		if (cl == NULL)
			continue;

		cl->status = 0;
		list_del(&cb->list);
		if (cb->fop_type == MEI_FOP_WRITE &&
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
		wake_up(&dev->wait_stop_wd);
	}

	if (mei_cl_is_connected(&dev->wd_cl)) {
		if (dev->wd_pending &&
		    mei_cl_flow_ctrl_creds(&dev->wd_cl) > 0) {
			ret = mei_wd_send(dev);
			if (ret)
				return ret;
			dev->wd_pending = false;
		}
	}

	/* complete control write list CB */
	dev_dbg(dev->dev, "complete control write list cb.\n");
	list_for_each_entry_safe(cb, next, &dev->ctrl_wr_list.list, list) {
		cl = cb->cl;
		if (!cl) {
			list_del(&cb->list);
			return -ENODEV;
		}
		switch (cb->fop_type) {
		case MEI_FOP_DISCONNECT:
			/* send disconnect message */
			ret = mei_cl_irq_disconnect(cl, cb, cmpl_list);
			if (ret)
				return ret;

			break;
		case MEI_FOP_READ:
			/* send flow control message */
			ret = mei_cl_irq_read(cl, cb, cmpl_list);
			if (ret)
				return ret;

			break;
		case MEI_FOP_CONNECT:
			/* connect message */
			ret = mei_cl_irq_connect(cl, cb, cmpl_list);
			if (ret)
				return ret;

			break;
		case MEI_FOP_DISCONNECT_RSP:
			/* send disconnect resp */
			ret = mei_cl_irq_disconnect_rsp(cl, cb, cmpl_list);
			if (ret)
				return ret;
			break;
		default:
			BUG();
		}

	}
	/* complete  write list CB */
	dev_dbg(dev->dev, "complete write list cb.\n");
	list_for_each_entry_safe(cb, next, &dev->write_list.list, list) {
		cl = cb->cl;
		if (cl == NULL)
			continue;
		if (cl == &dev->iamthif_cl)
			ret = mei_amthif_irq_write(cl, cb, cmpl_list);
		else
			ret = mei_cl_irq_write(cl, cb, cmpl_list);
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
	struct mei_cl *cl;

	struct mei_device *dev = container_of(work,
					struct mei_device, timer_work.work);


	mutex_lock(&dev->device_lock);

	/* Catch interrupt stalls during HBM init handshake */
	if (dev->dev_state == MEI_DEV_INIT_CLIENTS &&
	    dev->hbm_state != MEI_HBM_IDLE) {

		if (dev->init_clients_timer) {
			if (--dev->init_clients_timer == 0) {
				dev_err(dev->dev, "timer: init clients timeout hbm_state = %d.\n",
					dev->hbm_state);
				mei_reset(dev);
				goto out;
			}
		}
	}

	if (dev->dev_state != MEI_DEV_ENABLED)
		goto out;

	/*** connect/disconnect timeouts ***/
	list_for_each_entry(cl, &dev->file_list, link) {
		if (cl->timer_count) {
			if (--cl->timer_count == 0) {
				dev_err(dev->dev, "timer: connect/disconnect timeout.\n");
				mei_reset(dev);
				goto out;
			}
		}
	}

	if (!mei_cl_is_connected(&dev->iamthif_cl))
		goto out;

	if (dev->iamthif_stall_timer) {
		if (--dev->iamthif_stall_timer == 0) {
			dev_err(dev->dev, "timer: amthif  hanged.\n");
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

		dev_dbg(dev->dev, "dev->iamthif_timer = %ld\n",
				dev->iamthif_timer);
		dev_dbg(dev->dev, "timeout = %ld\n", timeout);
		dev_dbg(dev->dev, "jiffies = %ld\n", jiffies);
		if (time_after(jiffies, timeout)) {
			/*
			 * User didn't read the AMTHI data on time (15sec)
			 * freeing AMTHI for other requests
			 */

			dev_dbg(dev->dev, "freeing AMTHI for other requests\n");

			mei_io_list_flush(&dev->amthif_rd_complete_list,
				&dev->iamthif_cl);
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

