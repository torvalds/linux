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
 * _mei_cmpl - processes completed operation.
 *
 * @cl: private data of the file object.
 * @cb_pos: callback block.
 */
static void _mei_cmpl(struct mei_cl *cl, struct mei_cl_cb *cb_pos)
{
	if (cb_pos->fop_type == MEI_FOP_WRITE) {
		mei_io_cb_free(cb_pos);
		cb_pos = NULL;
		cl->writing_state = MEI_WRITE_COMPLETE;
		if (waitqueue_active(&cl->tx_wait))
			wake_up_interruptible(&cl->tx_wait);

	} else if (cb_pos->fop_type == MEI_FOP_READ &&
			MEI_READING == cl->reading_state) {
		cl->reading_state = MEI_READ_COMPLETE;
		if (waitqueue_active(&cl->rx_wait))
			wake_up_interruptible(&cl->rx_wait);

	}
}

/**
 * _mei_irq_thread_state_ok - checks if mei header matches file private data
 *
 * @cl: private data of the file object
 * @mei_hdr: header of mei client message
 *
 * returns !=0 if matches, 0 if no match.
 */
static int _mei_irq_thread_state_ok(struct mei_cl *cl,
				struct mei_msg_hdr *mei_hdr)
{
	return (cl->host_client_id == mei_hdr->host_addr &&
		cl->me_client_id == mei_hdr->me_addr &&
		cl->state == MEI_FILE_CONNECTED &&
		MEI_READ_COMPLETE != cl->reading_state);
}

/**
 * mei_irq_thread_read_client_message - bottom half read routine after ISR to
 * handle the read mei client message data processing.
 *
 * @complete_list: An instance of our list structure
 * @dev: the device structure
 * @mei_hdr: header of mei client message
 *
 * returns 0 on success, <0 on failure.
 */
static int mei_irq_thread_read_client_message(struct mei_cl_cb *complete_list,
		struct mei_device *dev,
		struct mei_msg_hdr *mei_hdr)
{
	struct mei_cl *cl;
	struct mei_cl_cb *cb_pos = NULL, *cb_next = NULL;
	unsigned char *buffer = NULL;

	dev_dbg(&dev->pdev->dev, "start client msg\n");
	if (list_empty(&dev->read_list.list))
		goto quit;

	list_for_each_entry_safe(cb_pos, cb_next, &dev->read_list.list, list) {
		cl = cb_pos->cl;
		if (cl && _mei_irq_thread_state_ok(cl, mei_hdr)) {
			cl->reading_state = MEI_READING;
			buffer = cb_pos->response_buffer.data + cb_pos->buf_idx;

			if (cb_pos->response_buffer.size <
					mei_hdr->length + cb_pos->buf_idx) {
				dev_dbg(&dev->pdev->dev, "message overflow.\n");
				list_del(&cb_pos->list);
				return -ENOMEM;
			}
			if (buffer)
				mei_read_slots(dev, buffer, mei_hdr->length);

			cb_pos->buf_idx += mei_hdr->length;
			if (mei_hdr->msg_complete) {
				cl->status = 0;
				list_del(&cb_pos->list);
				dev_dbg(&dev->pdev->dev,
					"completed read H cl = %d, ME cl = %d, length = %lu\n",
					cl->host_client_id,
					cl->me_client_id,
					cb_pos->buf_idx);

				list_add_tail(&cb_pos->list,
						&complete_list->list);
			}

			break;
		}

	}

quit:
	dev_dbg(&dev->pdev->dev, "message read\n");
	if (!buffer) {
		mei_read_slots(dev, dev->rd_msg_buf, mei_hdr->length);
		dev_dbg(&dev->pdev->dev, "discarding message " MEI_HDR_FMT "\n",
				MEI_HDR_PRM(mei_hdr));
	}

	return 0;
}

/**
 * _mei_irq_thread_close - processes close related operation.
 *
 * @dev: the device structure.
 * @slots: free slots.
 * @cb_pos: callback block.
 * @cl: private data of the file object.
 * @cmpl_list: complete list.
 *
 * returns 0, OK; otherwise, error.
 */
static int _mei_irq_thread_close(struct mei_device *dev, s32 *slots,
				struct mei_cl_cb *cb_pos,
				struct mei_cl *cl,
				struct mei_cl_cb *cmpl_list)
{
	if ((*slots * sizeof(u32)) < (sizeof(struct mei_msg_hdr) +
			sizeof(struct hbm_client_connect_request)))
		return -EBADMSG;

	*slots -= mei_data2slots(sizeof(struct hbm_client_connect_request));

	if (mei_hbm_cl_disconnect_req(dev, cl)) {
		cl->status = 0;
		cb_pos->buf_idx = 0;
		list_move_tail(&cb_pos->list, &cmpl_list->list);
		return -EMSGSIZE;
	} else {
		cl->state = MEI_FILE_DISCONNECTING;
		cl->status = 0;
		cb_pos->buf_idx = 0;
		list_move_tail(&cb_pos->list, &dev->ctrl_rd_list.list);
		cl->timer_count = MEI_CONNECT_TIMEOUT;
	}

	return 0;
}


/**
 * _mei_hb_read - processes read related operation.
 *
 * @dev: the device structure.
 * @slots: free slots.
 * @cb_pos: callback block.
 * @cl: private data of the file object.
 * @cmpl_list: complete list.
 *
 * returns 0, OK; otherwise, error.
 */
static int _mei_irq_thread_read(struct mei_device *dev,	s32 *slots,
			struct mei_cl_cb *cb_pos,
			struct mei_cl *cl,
			struct mei_cl_cb *cmpl_list)
{
	if ((*slots * sizeof(u32)) < (sizeof(struct mei_msg_hdr) +
			sizeof(struct hbm_flow_control))) {
		/* return the cancel routine */
		list_del(&cb_pos->list);
		return -EBADMSG;
	}

	*slots -= mei_data2slots(sizeof(struct hbm_flow_control));

	if (mei_hbm_cl_flow_control_req(dev, cl)) {
		cl->status = -ENODEV;
		cb_pos->buf_idx = 0;
		list_move_tail(&cb_pos->list, &cmpl_list->list);
		return -ENODEV;
	}
	list_move_tail(&cb_pos->list, &dev->read_list.list);

	return 0;
}


/**
 * _mei_irq_thread_ioctl - processes ioctl related operation.
 *
 * @dev: the device structure.
 * @slots: free slots.
 * @cb_pos: callback block.
 * @cl: private data of the file object.
 * @cmpl_list: complete list.
 *
 * returns 0, OK; otherwise, error.
 */
static int _mei_irq_thread_ioctl(struct mei_device *dev, s32 *slots,
			struct mei_cl_cb *cb_pos,
			struct mei_cl *cl,
			struct mei_cl_cb *cmpl_list)
{
	if ((*slots * sizeof(u32)) < (sizeof(struct mei_msg_hdr) +
			sizeof(struct hbm_client_connect_request))) {
		/* return the cancel routine */
		list_del(&cb_pos->list);
		return -EBADMSG;
	}

	cl->state = MEI_FILE_CONNECTING;
	*slots -= mei_data2slots(sizeof(struct hbm_client_connect_request));
	if (mei_hbm_cl_connect_req(dev, cl)) {
		cl->status = -ENODEV;
		cb_pos->buf_idx = 0;
		list_del(&cb_pos->list);
		return -ENODEV;
	} else {
		list_move_tail(&cb_pos->list, &dev->ctrl_rd_list.list);
		cl->timer_count = MEI_CONNECT_TIMEOUT;
	}
	return 0;
}

/**
 * mei_irq_thread_write_complete - write messages to device.
 *
 * @dev: the device structure.
 * @slots: free slots.
 * @cb: callback block.
 * @cmpl_list: complete list.
 *
 * returns 0, OK; otherwise, error.
 */
static int mei_irq_thread_write_complete(struct mei_device *dev, s32 *slots,
			struct mei_cl_cb *cb, struct mei_cl_cb *cmpl_list)
{
	struct mei_msg_hdr mei_hdr;
	struct mei_cl *cl = cb->cl;
	size_t len = cb->request_buffer.size - cb->buf_idx;
	size_t msg_slots = mei_data2slots(len);

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

	dev_dbg(&dev->pdev->dev, "buf: size = %d idx = %lu\n",
			cb->request_buffer.size, cb->buf_idx);
	dev_dbg(&dev->pdev->dev, MEI_HDR_FMT, MEI_HDR_PRM(&mei_hdr));

	*slots -=  msg_slots;
	if (mei_write_message(dev, &mei_hdr,
			cb->request_buffer.data + cb->buf_idx)) {
		cl->status = -ENODEV;
		list_move_tail(&cb->list, &cmpl_list->list);
		return -ENODEV;
	}

	if (mei_cl_flow_ctrl_reduce(cl))
		return -ENODEV;

	cl->status = 0;
	cb->buf_idx += mei_hdr.length;
	if (mei_hdr.msg_complete)
		list_move_tail(&cb->list, &dev->write_waiting_list.list);

	return 0;
}

/**
 * mei_irq_thread_read_handler - bottom half read routine after ISR to
 * handle the read processing.
 *
 * @cmpl_list: An instance of our list structure
 * @dev: the device structure
 * @slots: slots to read.
 *
 * returns 0 on success, <0 on failure.
 */
static int mei_irq_thread_read_handler(struct mei_cl_cb *cmpl_list,
		struct mei_device *dev,
		s32 *slots)
{
	struct mei_msg_hdr *mei_hdr;
	struct mei_cl *cl_pos = NULL;
	struct mei_cl *cl_next = NULL;
	int ret = 0;

	if (!dev->rd_msg_hdr) {
		dev->rd_msg_hdr = mei_read_hdr(dev);
		dev_dbg(&dev->pdev->dev, "slots =%08x.\n", *slots);
		(*slots)--;
		dev_dbg(&dev->pdev->dev, "slots =%08x.\n", *slots);
	}
	mei_hdr = (struct mei_msg_hdr *) &dev->rd_msg_hdr;
	dev_dbg(&dev->pdev->dev, MEI_HDR_FMT, MEI_HDR_PRM(mei_hdr));

	if (mei_hdr->reserved || !dev->rd_msg_hdr) {
		dev_dbg(&dev->pdev->dev, "corrupted message header.\n");
		ret = -EBADMSG;
		goto end;
	}

	if (mei_hdr->host_addr || mei_hdr->me_addr) {
		list_for_each_entry_safe(cl_pos, cl_next,
					&dev->file_list, link) {
			dev_dbg(&dev->pdev->dev,
					"list_for_each_entry_safe read host"
					" client = %d, ME client = %d\n",
					cl_pos->host_client_id,
					cl_pos->me_client_id);
			if (cl_pos->host_client_id == mei_hdr->host_addr &&
			    cl_pos->me_client_id == mei_hdr->me_addr)
				break;
		}

		if (&cl_pos->link == &dev->file_list) {
			dev_dbg(&dev->pdev->dev, "corrupted message header\n");
			ret = -EBADMSG;
			goto end;
		}
	}
	if (((*slots) * sizeof(u32)) < mei_hdr->length) {
		dev_dbg(&dev->pdev->dev,
				"we can't read the message slots =%08x.\n",
				*slots);
		/* we can't read the message */
		ret = -ERANGE;
		goto end;
	}

	/* decide where to read the message too */
	if (!mei_hdr->host_addr) {
		dev_dbg(&dev->pdev->dev, "call mei_irq_thread_read_bus_message.\n");
		mei_hbm_dispatch(dev, mei_hdr);
		dev_dbg(&dev->pdev->dev, "end mei_irq_thread_read_bus_message.\n");
	} else if (mei_hdr->host_addr == dev->iamthif_cl.host_client_id &&
		   (MEI_FILE_CONNECTED == dev->iamthif_cl.state) &&
		   (dev->iamthif_state == MEI_IAMTHIF_READING)) {
		dev_dbg(&dev->pdev->dev, "call mei_irq_thread_read_iamthif_message.\n");

		dev_dbg(&dev->pdev->dev, MEI_HDR_FMT, MEI_HDR_PRM(mei_hdr));

		ret = mei_amthif_irq_read_message(cmpl_list, dev, mei_hdr);
		if (ret)
			goto end;
	} else {
		dev_dbg(&dev->pdev->dev, "call mei_irq_thread_read_client_message.\n");
		ret = mei_irq_thread_read_client_message(cmpl_list,
							 dev, mei_hdr);
		if (ret)
			goto end;

	}

	/* reset the number of slots and header */
	*slots = mei_count_full_read_slots(dev);
	dev->rd_msg_hdr = 0;

	if (*slots == -EOVERFLOW) {
		/* overflow - reset */
		dev_dbg(&dev->pdev->dev, "resetting due to slots overflow.\n");
		/* set the event since message has been read */
		ret = -ERANGE;
		goto end;
	}
end:
	return ret;
}


/**
 * mei_irq_thread_write_handler - bottom half write routine after
 * ISR to handle the write processing.
 *
 * @dev: the device structure
 * @cmpl_list: An instance of our list structure
 *
 * returns 0 on success, <0 on failure.
 */
static int mei_irq_thread_write_handler(struct mei_device *dev,
				struct mei_cl_cb *cmpl_list)
{

	struct mei_cl *cl;
	struct mei_cl_cb *pos = NULL, *next = NULL;
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
	list_for_each_entry_safe(pos, next, &list->list, list) {
		cl = pos->cl;
		if (cl == NULL)
			continue;

		cl->status = 0;
		list_del(&pos->list);
		if (MEI_WRITING == cl->writing_state &&
		    pos->fop_type == MEI_FOP_WRITE &&
		    cl != &dev->iamthif_cl) {
			dev_dbg(&dev->pdev->dev, "MEI WRITE COMPLETE\n");
			cl->writing_state = MEI_WRITE_COMPLETE;
			list_add_tail(&pos->list, &cmpl_list->list);
		}
		if (cl == &dev->iamthif_cl) {
			dev_dbg(&dev->pdev->dev, "check iamthif flow control.\n");
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
	list_for_each_entry_safe(pos, next, &dev->ctrl_wr_list.list, list) {
		cl = pos->cl;
		if (!cl) {
			list_del(&pos->list);
			return -ENODEV;
		}
		switch (pos->fop_type) {
		case MEI_FOP_CLOSE:
			/* send disconnect message */
			ret = _mei_irq_thread_close(dev, &slots, pos,
						cl, cmpl_list);
			if (ret)
				return ret;

			break;
		case MEI_FOP_READ:
			/* send flow control message */
			ret = _mei_irq_thread_read(dev, &slots, pos,
						cl, cmpl_list);
			if (ret)
				return ret;

			break;
		case MEI_FOP_IOCTL:
			/* connect message */
			if (mei_cl_is_other_connecting(cl))
				continue;
			ret = _mei_irq_thread_ioctl(dev, &slots, pos,
						cl, cmpl_list);
			if (ret)
				return ret;

			break;

		default:
			BUG();
		}

	}
	/* complete  write list CB */
	dev_dbg(&dev->pdev->dev, "complete write list cb.\n");
	list_for_each_entry_safe(pos, next, &dev->write_list.list, list) {
		cl = pos->cl;
		if (cl == NULL)
			continue;
		if (mei_cl_flow_ctrl_creds(cl) <= 0) {
			dev_dbg(&dev->pdev->dev,
				"No flow control credentials for client %d, not sending.\n",
				cl->host_client_id);
			continue;
		}

		if (cl == &dev->iamthif_cl)
			ret = mei_amthif_irq_write_complete(dev, &slots,
							pos, cmpl_list);
		else
			ret = mei_irq_thread_write_complete(dev, &slots, pos,
						cmpl_list);
		if (ret)
			return ret;

	}
	return 0;
}



/**
 * mei_timer - timer function.
 *
 * @work: pointer to the work_struct structure
 *
 * NOTE: This function is called by timer interrupt work
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
	if (dev->dev_state != MEI_DEV_ENABLED) {
		if (dev->dev_state == MEI_DEV_INIT_CLIENTS) {
			if (dev->init_clients_timer) {
				if (--dev->init_clients_timer == 0) {
					dev_dbg(&dev->pdev->dev, "IMEI reset due to init clients timeout ,init clients state = %d.\n",
						dev->init_clients_state);
					mei_reset(dev, 1);
				}
			}
		}
		goto out;
	}
	/*** connect/disconnect timeouts ***/
	list_for_each_entry_safe(cl_pos, cl_next, &dev->file_list, link) {
		if (cl_pos->timer_count) {
			if (--cl_pos->timer_count == 0) {
				dev_dbg(&dev->pdev->dev, "HECI reset due to connect/disconnect timeout.\n");
				mei_reset(dev, 1);
				goto out;
			}
		}
	}

	if (dev->iamthif_stall_timer) {
		if (--dev->iamthif_stall_timer == 0) {
			dev_dbg(&dev->pdev->dev, "resetting because of hang to amthi.\n");
			mei_reset(dev, 1);
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
	schedule_delayed_work(&dev->timer_work, 2 * HZ);
	mutex_unlock(&dev->device_lock);
}

/**
 *  mei_interrupt_thread_handler - function called after ISR to handle the interrupt
 * processing.
 *
 * @irq: The irq number
 * @dev_id: pointer to the device structure
 *
 * returns irqreturn_t
 *
 */
irqreturn_t mei_interrupt_thread_handler(int irq, void *dev_id)
{
	struct mei_device *dev = (struct mei_device *) dev_id;
	struct mei_cl_cb complete_list;
	struct mei_cl_cb *cb_pos = NULL, *cb_next = NULL;
	struct mei_cl *cl;
	s32 slots;
	int rets;
	bool  bus_message_received;


	dev_dbg(&dev->pdev->dev, "function called after ISR to handle the interrupt processing.\n");
	/* initialize our complete list */
	mutex_lock(&dev->device_lock);
	mei_io_list_init(&complete_list);

	/* Ack the interrupt here
	 * In case of MSI we don't go through the quick handler */
	if (pci_dev_msi_enabled(dev->pdev))
		mei_clear_interrupts(dev);

	/* check if ME wants a reset */
	if (!mei_hw_is_ready(dev) &&
	    dev->dev_state != MEI_DEV_RESETING &&
	    dev->dev_state != MEI_DEV_INITIALIZING) {
		dev_dbg(&dev->pdev->dev, "FW not ready.\n");
		mei_reset(dev, 1);
		mutex_unlock(&dev->device_lock);
		return IRQ_HANDLED;
	}

	/*  check if we need to start the dev */
	if (!mei_host_is_ready(dev)) {
		if (mei_hw_is_ready(dev)) {
			dev_dbg(&dev->pdev->dev, "we need to start the dev.\n");

			mei_host_set_ready(dev);

			dev_dbg(&dev->pdev->dev, "link is established start sending messages.\n");
			/* link is established * start sending messages.  */

			dev->dev_state = MEI_DEV_INIT_CLIENTS;

			mei_hbm_start_req(dev);
			mutex_unlock(&dev->device_lock);
			return IRQ_HANDLED;
		} else {
			dev_dbg(&dev->pdev->dev, "FW not ready.\n");
			mutex_unlock(&dev->device_lock);
			return IRQ_HANDLED;
		}
	}
	/* check slots available for reading */
	slots = mei_count_full_read_slots(dev);
	while (slots > 0) {
		/* we have urgent data to send so break the read */
		if (dev->wr_ext_msg.hdr.length)
			break;
		dev_dbg(&dev->pdev->dev, "slots =%08x\n", slots);
		dev_dbg(&dev->pdev->dev, "call mei_irq_thread_read_handler.\n");
		rets = mei_irq_thread_read_handler(&complete_list, dev, &slots);
		if (rets)
			goto end;
	}
	rets = mei_irq_thread_write_handler(dev, &complete_list);
end:
	dev_dbg(&dev->pdev->dev, "end of bottom half function.\n");
	dev->mei_host_buffer_is_empty = mei_hbuf_is_ready(dev);

	bus_message_received = false;
	if (dev->recvd_msg && waitqueue_active(&dev->wait_recvd_msg)) {
		dev_dbg(&dev->pdev->dev, "received waiting bus message\n");
		bus_message_received = true;
	}
	mutex_unlock(&dev->device_lock);
	if (bus_message_received) {
		dev_dbg(&dev->pdev->dev, "wake up dev->wait_recvd_msg\n");
		wake_up_interruptible(&dev->wait_recvd_msg);
		bus_message_received = false;
	}
	if (list_empty(&complete_list.list))
		return IRQ_HANDLED;


	list_for_each_entry_safe(cb_pos, cb_next, &complete_list.list, list) {
		cl = cb_pos->cl;
		list_del(&cb_pos->list);
		if (cl) {
			if (cl != &dev->iamthif_cl) {
				dev_dbg(&dev->pdev->dev, "completing call back.\n");
				_mei_cmpl(cl, cb_pos);
				cb_pos = NULL;
			} else if (cl == &dev->iamthif_cl) {
				mei_amthif_complete(dev, cb_pos);
			}
		}
	}
	return IRQ_HANDLED;
}
