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
#include <linux/kthread.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/jiffies.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>

#include <linux/mei.h>

#include "mei_dev.h"
#include "hbm.h"
#include "client.h"


/**
 * mei_irq_compl_handler - dispatch complete handlers
 *	for the completed callbacks
 *
 * @dev: mei device
 * @cmpl_list: list of completed cbs
 */
void mei_irq_compl_handler(struct mei_device *dev, struct list_head *cmpl_list)
{
	struct mei_cl_cb *cb, *next;
	struct mei_cl *cl;

	list_for_each_entry_safe(cb, next, cmpl_list, list) {
		cl = cb->cl;
		list_del_init(&cb->list);

		dev_dbg(dev->dev, "completing call back.\n");
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
 * Return: true if matches, false otherwise
 */
static inline int mei_cl_hbm_equal(struct mei_cl *cl,
			struct mei_msg_hdr *mei_hdr)
{
	return  mei_cl_host_addr(cl) == mei_hdr->host_addr &&
		mei_cl_me_id(cl) == mei_hdr->me_addr;
}

/**
 * mei_irq_discard_msg  - discard received message
 *
 * @dev: mei device
 * @hdr: message header
 */
static void mei_irq_discard_msg(struct mei_device *dev, struct mei_msg_hdr *hdr)
{
	/*
	 * no need to check for size as it is guarantied
	 * that length fits into rd_msg_buf
	 */
	mei_read_slots(dev, dev->rd_msg_buf, hdr->length);
	dev_dbg(dev->dev, "discarding message " MEI_HDR_FMT "\n",
		MEI_HDR_PRM(hdr));
}

/**
 * mei_cl_irq_read_msg - process client message
 *
 * @cl: reading client
 * @mei_hdr: header of mei client message
 * @cmpl_list: completion list
 *
 * Return: always 0
 */
static int mei_cl_irq_read_msg(struct mei_cl *cl,
			       struct mei_msg_hdr *mei_hdr,
			       struct list_head *cmpl_list)
{
	struct mei_device *dev = cl->dev;
	struct mei_cl_cb *cb;
	size_t buf_sz;

	cb = list_first_entry_or_null(&cl->rd_pending, struct mei_cl_cb, list);
	if (!cb) {
		if (!mei_cl_is_fixed_address(cl)) {
			cl_err(dev, cl, "pending read cb not found\n");
			goto discard;
		}
		cb = mei_cl_alloc_cb(cl, mei_cl_mtu(cl), MEI_FOP_READ, cl->fp);
		if (!cb)
			goto discard;
		list_add_tail(&cb->list, &cl->rd_pending);
	}

	if (!mei_cl_is_connected(cl)) {
		cl_dbg(dev, cl, "not connected\n");
		cb->status = -ENODEV;
		goto discard;
	}

	buf_sz = mei_hdr->length + cb->buf_idx;
	/* catch for integer overflow */
	if (buf_sz < cb->buf_idx) {
		cl_err(dev, cl, "message is too big len %d idx %zu\n",
		       mei_hdr->length, cb->buf_idx);
		cb->status = -EMSGSIZE;
		goto discard;
	}

	if (cb->buf.size < buf_sz) {
		cl_dbg(dev, cl, "message overflow. size %zu len %d idx %zu\n",
			cb->buf.size, mei_hdr->length, cb->buf_idx);
		cb->status = -EMSGSIZE;
		goto discard;
	}

	mei_read_slots(dev, cb->buf.data + cb->buf_idx, mei_hdr->length);

	cb->buf_idx += mei_hdr->length;

	if (mei_hdr->msg_complete) {
		cl_dbg(dev, cl, "completed read length = %zu\n", cb->buf_idx);
		list_move_tail(&cb->list, cmpl_list);
	} else {
		pm_runtime_mark_last_busy(dev->dev);
		pm_request_autosuspend(dev->dev);
	}

	return 0;

discard:
	if (cb)
		list_move_tail(&cb->list, cmpl_list);
	mei_irq_discard_msg(dev, mei_hdr);
	return 0;
}

/**
 * mei_cl_irq_disconnect_rsp - send disconnection response message
 *
 * @cl: client
 * @cb: callback block.
 * @cmpl_list: complete list.
 *
 * Return: 0, OK; otherwise, error.
 */
static int mei_cl_irq_disconnect_rsp(struct mei_cl *cl, struct mei_cl_cb *cb,
				     struct list_head *cmpl_list)
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
	list_move_tail(&cb->list, cmpl_list);

	return ret;
}

/**
 * mei_cl_irq_read - processes client read related operation from the
 *	interrupt thread context - request for flow control credits
 *
 * @cl: client
 * @cb: callback block.
 * @cmpl_list: complete list.
 *
 * Return: 0, OK; otherwise, error.
 */
static int mei_cl_irq_read(struct mei_cl *cl, struct mei_cl_cb *cb,
			   struct list_head *cmpl_list)
{
	struct mei_device *dev = cl->dev;
	u32 msg_slots;
	int slots;
	int ret;

	if (!list_empty(&cl->rd_pending))
		return 0;

	msg_slots = mei_data2slots(sizeof(struct hbm_flow_control));
	slots = mei_hbuf_empty_slots(dev);

	if (slots < msg_slots)
		return -EMSGSIZE;

	ret = mei_hbm_cl_flow_control_req(dev, cl);
	if (ret) {
		cl->status = ret;
		cb->buf_idx = 0;
		list_move_tail(&cb->list, cmpl_list);
		return ret;
	}

	list_move_tail(&cb->list, &cl->rd_pending);

	return 0;
}

static inline bool hdr_is_hbm(struct mei_msg_hdr *mei_hdr)
{
	return mei_hdr->host_addr == 0 && mei_hdr->me_addr == 0;
}

static inline bool hdr_is_fixed(struct mei_msg_hdr *mei_hdr)
{
	return mei_hdr->host_addr == 0 && mei_hdr->me_addr != 0;
}

static inline int hdr_is_valid(u32 msg_hdr)
{
	struct mei_msg_hdr *mei_hdr;

	mei_hdr = (struct mei_msg_hdr *)&msg_hdr;
	if (!msg_hdr || mei_hdr->reserved)
		return -EBADMSG;

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
 * Return: 0 on success, <0 on failure.
 */
int mei_irq_read_handler(struct mei_device *dev,
			 struct list_head *cmpl_list, s32 *slots)
{
	struct mei_msg_hdr *mei_hdr;
	struct mei_cl *cl;
	int ret;

	if (!dev->rd_msg_hdr) {
		dev->rd_msg_hdr = mei_read_hdr(dev);
		(*slots)--;
		dev_dbg(dev->dev, "slots =%08x.\n", *slots);

		ret = hdr_is_valid(dev->rd_msg_hdr);
		if (ret) {
			dev_err(dev->dev, "corrupted message header 0x%08X\n",
				dev->rd_msg_hdr);
			goto end;
		}
	}

	mei_hdr = (struct mei_msg_hdr *)&dev->rd_msg_hdr;
	dev_dbg(dev->dev, MEI_HDR_FMT, MEI_HDR_PRM(mei_hdr));

	if (mei_slots2data(*slots) < mei_hdr->length) {
		dev_err(dev->dev, "less data available than length=%08x.\n",
				*slots);
		/* we can't read the message */
		ret = -ENODATA;
		goto end;
	}

	/*  HBM message */
	if (hdr_is_hbm(mei_hdr)) {
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
		/* A message for not connected fixed address clients
		 * should be silently discarded
		 * On power down client may be force cleaned,
		 * silently discard such messages
		 */
		if (hdr_is_fixed(mei_hdr) ||
		    dev->dev_state == MEI_DEV_POWER_DOWN) {
			mei_irq_discard_msg(dev, mei_hdr);
			ret = 0;
			goto reset_slots;
		}
		dev_err(dev->dev, "no destination client found 0x%08X\n",
				dev->rd_msg_hdr);
		ret = -EBADMSG;
		goto end;
	}

	ret = mei_cl_irq_read_msg(cl, mei_hdr, cmpl_list);


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
 * Return: 0 on success, <0 on failure.
 */
int mei_irq_write_handler(struct mei_device *dev, struct list_head *cmpl_list)
{

	struct mei_cl *cl;
	struct mei_cl_cb *cb, *next;
	s32 slots;
	int ret;


	if (!mei_hbuf_acquire(dev))
		return 0;

	slots = mei_hbuf_empty_slots(dev);
	if (slots <= 0)
		return -EMSGSIZE;

	/* complete all waiting for write CB */
	dev_dbg(dev->dev, "complete all waiting for write cb.\n");

	list_for_each_entry_safe(cb, next, &dev->write_waiting_list, list) {
		cl = cb->cl;

		cl->status = 0;
		cl_dbg(dev, cl, "MEI WRITE COMPLETE\n");
		cl->writing_state = MEI_WRITE_COMPLETE;
		list_move_tail(&cb->list, cmpl_list);
	}

	/* complete control write list CB */
	dev_dbg(dev->dev, "complete control write list cb.\n");
	list_for_each_entry_safe(cb, next, &dev->ctrl_wr_list, list) {
		cl = cb->cl;
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

		case MEI_FOP_NOTIFY_START:
		case MEI_FOP_NOTIFY_STOP:
			ret = mei_cl_irq_notify(cl, cb, cmpl_list);
			if (ret)
				return ret;
			break;
		default:
			BUG();
		}

	}
	/* complete  write list CB */
	dev_dbg(dev->dev, "complete write list cb.\n");
	list_for_each_entry_safe(cb, next, &dev->write_list, list) {
		cl = cb->cl;
		ret = mei_cl_irq_write(cl, cb, cmpl_list);
		if (ret)
			return ret;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(mei_irq_write_handler);


/**
 * mei_connect_timeout  - connect/disconnect timeouts
 *
 * @cl: host client
 */
static void mei_connect_timeout(struct mei_cl *cl)
{
	struct mei_device *dev = cl->dev;

	if (cl->state == MEI_FILE_CONNECTING) {
		if (dev->hbm_f_dot_supported) {
			cl->state = MEI_FILE_DISCONNECT_REQUIRED;
			wake_up(&cl->wait);
			return;
		}
	}
	mei_reset(dev);
}

#define MEI_STALL_TIMER_FREQ (2 * HZ)
/**
 * mei_schedule_stall_timer - re-arm stall_timer work
 *
 * Schedule stall timer
 *
 * @dev: the device structure
 */
void mei_schedule_stall_timer(struct mei_device *dev)
{
	schedule_delayed_work(&dev->timer_work, MEI_STALL_TIMER_FREQ);
}

/**
 * mei_timer - timer function.
 *
 * @work: pointer to the work_struct structure
 *
 */
void mei_timer(struct work_struct *work)
{
	struct mei_cl *cl;
	struct mei_device *dev = container_of(work,
					struct mei_device, timer_work.work);
	bool reschedule_timer = false;

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
			reschedule_timer = true;
		}
	}

	if (dev->dev_state != MEI_DEV_ENABLED)
		goto out;

	/*** connect/disconnect timeouts ***/
	list_for_each_entry(cl, &dev->file_list, link) {
		if (cl->timer_count) {
			if (--cl->timer_count == 0) {
				dev_err(dev->dev, "timer: connect/disconnect timeout.\n");
				mei_connect_timeout(cl);
				goto out;
			}
			reschedule_timer = true;
		}
	}

out:
	if (dev->dev_state != MEI_DEV_DISABLED && reschedule_timer)
		mei_schedule_stall_timer(dev);

	mutex_unlock(&dev->device_lock);
}
