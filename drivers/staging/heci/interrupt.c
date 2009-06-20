/*
 * Part of Intel(R) Manageability Engine Interface Linux driver
 *
 * Copyright (c) 2003 - 2008 Intel Corp.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 */

#include <linux/kthread.h>

#include "heci.h"
#include "heci_interface.h"

/*
 *  interrupt function prototypes
 */
static void heci_bh_handler(struct work_struct *work);
static int heci_bh_read_handler(struct io_heci_list *complete_list,
		struct iamt_heci_device *dev,
		__s32 *slots);
static int heci_bh_write_handler(struct io_heci_list *complete_list,
		struct iamt_heci_device *dev,
		__s32 *slots);
static void heci_bh_read_bus_message(struct iamt_heci_device *dev,
		struct heci_msg_hdr *heci_hdr);
static int heci_bh_read_pthi_message(struct io_heci_list *complete_list,
		struct iamt_heci_device *dev,
		struct heci_msg_hdr *heci_hdr);
static int heci_bh_read_client_message(struct io_heci_list *complete_list,
		struct iamt_heci_device *dev,
		struct heci_msg_hdr *heci_hdr);
static void heci_client_connect_response(struct iamt_heci_device *dev,
		struct hbm_client_connect_response *connect_res);
static void heci_client_disconnect_response(struct iamt_heci_device *dev,
		struct hbm_client_connect_response *disconnect_res);
static void heci_client_flow_control_response(struct iamt_heci_device *dev,
		struct hbm_flow_control *flow_control);
static void heci_client_disconnect_request(struct iamt_heci_device *dev,
		struct hbm_client_disconnect_request *disconnect_req);


/**
 * heci_isr_interrupt - The ISR of the HECI device
 *
 * @irq: The irq number
 * @dev_id: pointer to the device structure
 *
 * returns irqreturn_t
 */
irqreturn_t heci_isr_interrupt(int irq, void *dev_id)
{
	int err;
	struct iamt_heci_device *dev = (struct iamt_heci_device *) dev_id;

	dev->host_hw_state = read_heci_register(dev, H_CSR);

	if ((dev->host_hw_state & H_IS) != H_IS)
		return IRQ_NONE;

	/* disable interrupts */
	heci_csr_disable_interrupts(dev);

	/* clear H_IS bit in H_CSR */
	heci_csr_clear_his(dev);

	/*
	 * Our device interrupted, schedule work the heci_bh_handler
	 * to handle the interrupt processing. This needs to be a
	 * workqueue item since the handler can sleep.
	 */
	PREPARE_WORK(&dev->work, heci_bh_handler);
	DBG("schedule work the heci_bh_handler.\n");
	err = schedule_work(&dev->work);
	if (!err)
		DBG("heci_bh_handler was already on the workqueue.\n");
	return IRQ_HANDLED;
}

/**
 * _heci_cmpl - process completed operation.
 *
 * @file_ext: private data of the file object.
 * @priv_cb_pos: callback block.
 */
static void _heci_cmpl(struct heci_file_private *file_ext,
				struct heci_cb_private *priv_cb_pos)
{
	if (priv_cb_pos->major_file_operations == HECI_WRITE) {
		heci_free_cb_private(priv_cb_pos);
		DBG("completing write call back.\n");
		file_ext->writing_state = HECI_WRITE_COMPLETE;
		if ((&file_ext->tx_wait) &&
		    waitqueue_active(&file_ext->tx_wait))
			wake_up_interruptible(&file_ext->tx_wait);

	} else if (priv_cb_pos->major_file_operations == HECI_READ
				&& HECI_READING == file_ext->reading_state) {
		DBG("completing read call back information= %lu\n",
				priv_cb_pos->information);
		file_ext->reading_state = HECI_READ_COMPLETE;
		if ((&file_ext->rx_wait) &&
		    waitqueue_active(&file_ext->rx_wait))
			wake_up_interruptible(&file_ext->rx_wait);

	}
}

/**
 * _heci_cmpl_iamthif - process completed iamthif operation.
 *
 * @dev: Device object for our driver.
 * @priv_cb_pos: callback block.
 */
static void _heci_cmpl_iamthif(struct iamt_heci_device *dev,
				struct heci_cb_private *priv_cb_pos)
{
	if (dev->iamthif_canceled != 1) {
		dev->iamthif_state = HECI_IAMTHIF_READ_COMPLETE;
		dev->iamthif_stall_timer = 0;
		memcpy(priv_cb_pos->response_buffer.data,
				dev->iamthif_msg_buf,
				dev->iamthif_msg_buf_index);
		list_add_tail(&priv_cb_pos->cb_list,
				&dev->pthi_read_complete_list.heci_cb.cb_list);
		DBG("pthi read completed.\n");
	} else {
		run_next_iamthif_cmd(dev);
	}
	if (&dev->iamthif_file_ext.wait) {
		DBG("completing pthi call back.\n");
		wake_up_interruptible(&dev->iamthif_file_ext.wait);
	}
}
/**
 * heci_bh_handler - function called after ISR to handle the interrupt
 * processing.
 *
 * @work: pointer to the work structure
 *
 * NOTE: This function is called by schedule work
 */
static void heci_bh_handler(struct work_struct *work)
{
	struct iamt_heci_device *dev =
		container_of(work, struct iamt_heci_device, work);
	struct io_heci_list complete_list;
	__s32 slots;
	int rets;
	struct heci_cb_private *cb_pos = NULL, *cb_next = NULL;
	struct heci_file_private *file_ext;
	int bus_message_received = 0;
	struct task_struct *tsk;

	DBG("function called after ISR to handle the interrupt processing.\n");
	/* initialize our complete list */
	spin_lock_bh(&dev->device_lock);
	heci_initialize_list(&complete_list, dev);
	dev->host_hw_state = read_heci_register(dev, H_CSR);
	dev->me_hw_state = read_heci_register(dev, ME_CSR_HA);

	/* check if ME wants a reset */
	if (((dev->me_hw_state & ME_RDY_HRA) == 0)
	    && (dev->heci_state != HECI_RESETING)
	    && (dev->heci_state != HECI_INITIALIZING)) {
		DBG("FW not ready.\n");
		heci_reset(dev, 1);
		spin_unlock_bh(&dev->device_lock);
		return;
	}

	/*  check if we need to start the dev */
	if ((dev->host_hw_state & H_RDY) == 0) {
		if ((dev->me_hw_state & ME_RDY_HRA) == ME_RDY_HRA) {
			DBG("we need to start the dev.\n");
			dev->host_hw_state |= (H_IE | H_IG | H_RDY);
			heci_set_csr_register(dev);
			if (dev->heci_state == HECI_INITIALIZING) {
				dev->recvd_msg = 1;
				spin_unlock_bh(&dev->device_lock);
				wake_up_interruptible(&dev->wait_recvd_msg);
				return;

			} else {
				spin_unlock_bh(&dev->device_lock);
				tsk = kthread_run(heci_task_initialize_clients,
						  dev, "heci_reinit");
				if (IS_ERR(tsk)) {
					int rc = PTR_ERR(tsk);
					printk(KERN_WARNING "heci: Unable to"
					"start the heci thread: %d\n", rc);
				}
				return;
			}
		} else {
			DBG("enable interrupt FW not ready.\n");
			heci_csr_enable_interrupts(dev);
			spin_unlock_bh(&dev->device_lock);
			return;
		}
	}
	/* check slots avalable for reading */
	slots = count_full_read_slots(dev);
	DBG("slots =%08x  extra_write_index =%08x.\n",
		slots, dev->extra_write_index);
	while ((slots > 0) && (!dev->extra_write_index)) {
		DBG("slots =%08x  extra_write_index =%08x.\n", slots,
				dev->extra_write_index);
		DBG("call heci_bh_read_handler.\n");
		rets = heci_bh_read_handler(&complete_list, dev, &slots);
		if (rets != 0)
			goto end;
	}
	rets = heci_bh_write_handler(&complete_list, dev, &slots);
end:
	DBG("end of bottom half function.\n");
	dev->host_hw_state = read_heci_register(dev, H_CSR);
	dev->host_buffer_is_empty = host_buffer_is_empty(dev);

	if ((dev->host_hw_state & H_IS) == H_IS) {
		/* acknowledge interrupt and disable interrupts */
		heci_csr_disable_interrupts(dev);

		/* clear H_IS bit in H_CSR */
		heci_csr_clear_his(dev);

		PREPARE_WORK(&dev->work, heci_bh_handler);
		DBG("schedule work the heci_bh_handler.\n");
		rets = schedule_work(&dev->work);
		if (!rets)
			DBG("heci_bh_handler was already queued.\n");
	} else {
		heci_csr_enable_interrupts(dev);
	}

	if (dev->recvd_msg && waitqueue_active(&dev->wait_recvd_msg)) {
		DBG("received waiting bus message\n");
		bus_message_received = 1;
	}
	spin_unlock_bh(&dev->device_lock);
	if (bus_message_received) {
		DBG("wake up dev->wait_recvd_msg\n");
		wake_up_interruptible(&dev->wait_recvd_msg);
		bus_message_received = 0;
	}
	if ((complete_list.status != 0)
	    || list_empty(&complete_list.heci_cb.cb_list))
		return;


	list_for_each_entry_safe(cb_pos, cb_next,
			&complete_list.heci_cb.cb_list, cb_list) {
		file_ext = (struct heci_file_private *)cb_pos->file_private;
		list_del(&cb_pos->cb_list);
		if (file_ext != NULL) {
			if (file_ext != &dev->iamthif_file_ext) {
				DBG("completing call back.\n");
				_heci_cmpl(file_ext, cb_pos);
				cb_pos = NULL;
			} else if (file_ext == &dev->iamthif_file_ext) {
				_heci_cmpl_iamthif(dev, cb_pos);
			}
		}
	}
}


/**
 * heci_bh_read_handler - bottom half read routine after ISR to
 * handle the read processing.
 *
 * @cmpl_list: An instance of our list structure
 * @dev: Device object for our driver
 * @slots: slots to read.
 *
 * returns 0 on success, <0 on failure.
 */
static int heci_bh_read_handler(struct io_heci_list *cmpl_list,
		struct iamt_heci_device *dev,
		__s32 *slots)
{
	struct heci_msg_hdr *heci_hdr;
	int ret = 0;
	struct heci_file_private *file_pos = NULL;
	struct heci_file_private *file_next = NULL;

	if (!dev->rd_msg_hdr) {
		dev->rd_msg_hdr = read_heci_register(dev, ME_CB_RW);
		DBG("slots=%08x.\n", *slots);
		(*slots)--;
		DBG("slots=%08x.\n", *slots);
	}
	heci_hdr = (struct heci_msg_hdr *) &dev->rd_msg_hdr;
	DBG("heci_hdr->length =%d\n", heci_hdr->length);

	if ((heci_hdr->reserved) || !(dev->rd_msg_hdr)) {
		DBG("corrupted message header.\n");
		ret = -ECORRUPTED_MESSAGE_HEADER;
		goto end;
	}

	if ((heci_hdr->host_addr) || (heci_hdr->me_addr)) {
		list_for_each_entry_safe(file_pos, file_next,
				&dev->file_list, link) {
			DBG("list_for_each_entry_safe read host"
					" client = %d, ME client = %d\n",
					file_pos->host_client_id,
					file_pos->me_client_id);
			if ((file_pos->host_client_id == heci_hdr->host_addr)
			    && (file_pos->me_client_id == heci_hdr->me_addr))
				break;
		}

		if (&file_pos->link == &dev->file_list) {
			DBG("corrupted message header\n");
			ret = -ECORRUPTED_MESSAGE_HEADER;
			goto end;
		}
	}
	if (((*slots) * sizeof(__u32)) < heci_hdr->length) {
		DBG("we can't read the message slots=%08x.\n", *slots);
		/* we can't read the message */
		ret = -ERANGE;
		goto end;
	}

	/* decide where to read the message too */
	if (!heci_hdr->host_addr) {
		DBG("call heci_bh_read_bus_message.\n");
		heci_bh_read_bus_message(dev, heci_hdr);
		DBG("end heci_bh_read_bus_message.\n");
	} else if ((heci_hdr->host_addr == dev->iamthif_file_ext.host_client_id)
		   && (HECI_FILE_CONNECTED == dev->iamthif_file_ext.state)
		   && (dev->iamthif_state == HECI_IAMTHIF_READING)) {
		DBG("call heci_bh_read_iamthif_message.\n");
		DBG("heci_hdr->length =%d\n", heci_hdr->length);
		ret = heci_bh_read_pthi_message(cmpl_list, dev, heci_hdr);
		if (ret != 0)
			goto end;

	} else {
		DBG("call heci_bh_read_client_message.\n");
		ret = heci_bh_read_client_message(cmpl_list, dev, heci_hdr);
		if (ret != 0)
			goto end;

	}

	/* reset the number of slots and header */
	*slots = count_full_read_slots(dev);
	dev->rd_msg_hdr = 0;

	if (*slots == -ESLOTS_OVERFLOW) {
		/* overflow - reset */
		DBG("reseting due to slots overflow.\n");
		/* set the event since message has been read */
		ret = -ERANGE;
		goto end;
	}
end:
	return ret;
}


/**
 * heci_bh_read_bus_message - bottom half read routine after ISR to
 * handle the read bus message cmd  processing.
 *
 * @dev: Device object for our driver
 * @heci_hdr: header of bus message
 */
static void heci_bh_read_bus_message(struct iamt_heci_device *dev,
		struct heci_msg_hdr *heci_hdr)
{
	struct heci_bus_message *heci_msg;
	struct hbm_host_version_response *version_res;
	struct hbm_client_connect_response *connect_res;
	struct hbm_client_connect_response *disconnect_res;
	struct hbm_flow_control *flow_control;
	struct hbm_props_response *props_res;
	struct hbm_host_enum_response *enum_res;
	struct hbm_client_disconnect_request *disconnect_req;
	struct hbm_host_stop_request *h_stop_req;
	int i;
	unsigned char *buffer;

	/*  read the message to our buffer */
	buffer = (unsigned char *) dev->rd_msg_buf;
	BUG_ON(heci_hdr->length >= sizeof(dev->rd_msg_buf));
	heci_read_slots(dev, buffer, heci_hdr->length);
	heci_msg = (struct heci_bus_message *) buffer;

	switch (*(__u8 *) heci_msg) {
	case HOST_START_RES_CMD:
		version_res = (struct hbm_host_version_response *) heci_msg;
		if (version_res->host_version_supported) {
			dev->version.major_version = HBM_MAJOR_VERSION;
			dev->version.minor_version = HBM_MINOR_VERSION;
		} else {
			dev->version = version_res->me_max_version;
		}
		dev->recvd_msg = 1;
		DBG("host start response message received.\n");
		break;

	case CLIENT_CONNECT_RES_CMD:
		connect_res =
			(struct hbm_client_connect_response *) heci_msg;
		heci_client_connect_response(dev, connect_res);
		DBG("client connect response message received.\n");
		wake_up(&dev->wait_recvd_msg);
		break;

	case CLIENT_DISCONNECT_RES_CMD:
		disconnect_res =
			(struct hbm_client_connect_response *) heci_msg;
		heci_client_disconnect_response(dev,	 disconnect_res);
		DBG("client disconnect response message received.\n");
		wake_up(&dev->wait_recvd_msg);
		break;

	case HECI_FLOW_CONTROL_CMD:
		flow_control = (struct hbm_flow_control *) heci_msg;
		heci_client_flow_control_response(dev, flow_control);
		DBG("client flow control response message received.\n");
		break;

	case HOST_CLIENT_PROPERTEIS_RES_CMD:
		props_res = (struct hbm_props_response *) heci_msg;
		if (props_res->status != 0) {
			BUG();
			break;
		}
		for (i = 0; i < dev->num_heci_me_clients; i++) {
			if (dev->me_clients[i].client_id ==
					props_res->address) {
				dev->me_clients[i].props =
					props_res->client_properties;
				break;
			}

		}
		dev->recvd_msg = 1;
		break;

	case HOST_ENUM_RES_CMD:
		enum_res = (struct hbm_host_enum_response *) heci_msg;
		memcpy(dev->heci_me_clients, enum_res->valid_addresses, 32);
		dev->recvd_msg = 1;
		break;

	case HOST_STOP_RES_CMD:
		dev->heci_state = HECI_DISABLED;
		DBG("reseting because of FW stop response.\n");
		heci_reset(dev, 1);
		break;

	case CLIENT_DISCONNECT_REQ_CMD:
		/* search for client */
		disconnect_req =
			(struct hbm_client_disconnect_request *) heci_msg;
		heci_client_disconnect_request(dev, disconnect_req);
		break;

	case ME_STOP_REQ_CMD:
		/* prepare stop request */
		heci_hdr = (struct heci_msg_hdr *) &dev->ext_msg_buf[0];
		heci_hdr->host_addr = 0;
		heci_hdr->me_addr = 0;
		heci_hdr->length = sizeof(struct hbm_host_stop_request);
		heci_hdr->msg_complete = 1;
		heci_hdr->reserved = 0;
		h_stop_req =
			(struct hbm_host_stop_request *) &dev->ext_msg_buf[1];
		memset(h_stop_req, 0, sizeof(struct hbm_host_stop_request));
		h_stop_req->cmd.cmd = HOST_STOP_REQ_CMD;
		h_stop_req->reason = DRIVER_STOP_REQUEST;
		h_stop_req->reserved[0] = 0;
		h_stop_req->reserved[1] = 0;
		dev->extra_write_index = 2;
		break;

	default:
		BUG();
		break;

	}
}

/**
 * heci_bh_read_pthi_message - bottom half read routine after ISR to
 * handle the read pthi message data processing.
 *
 * @complete_list: An instance of our list structure
 * @dev: Device object for our driver
 * @heci_hdr: header of pthi message
 *
 * returns 0 on success, <0 on failure.
 */
static int heci_bh_read_pthi_message(struct io_heci_list *complete_list,
		struct iamt_heci_device *dev,
		struct heci_msg_hdr *heci_hdr)
{
	struct heci_file_private *file_ext;
	struct heci_cb_private *priv_cb;
	unsigned char *buffer;

	BUG_ON(heci_hdr->me_addr != dev->iamthif_file_ext.me_client_id);
	BUG_ON(dev->iamthif_state != HECI_IAMTHIF_READING);

	buffer = (unsigned char *) (dev->iamthif_msg_buf +
			dev->iamthif_msg_buf_index);
	BUG_ON(sizeof(dev->iamthif_msg_buf) <
			(dev->iamthif_msg_buf_index + heci_hdr->length));

	heci_read_slots(dev, buffer, heci_hdr->length);

	dev->iamthif_msg_buf_index += heci_hdr->length;

	if (!(heci_hdr->msg_complete))
		return 0;

	DBG("pthi_message_buffer_index=%d\n", heci_hdr->length);
	DBG("completed pthi read.\n ");
	if (!dev->iamthif_current_cb)
		return -ENODEV;

	priv_cb = dev->iamthif_current_cb;
	dev->iamthif_current_cb = NULL;

	file_ext = (struct heci_file_private *)priv_cb->file_private;
	if (!file_ext)
		return -ENODEV;

	dev->iamthif_stall_timer = 0;
	priv_cb->information =	dev->iamthif_msg_buf_index;
	priv_cb->read_time = get_seconds();
	if ((dev->iamthif_ioctl) && (file_ext == &dev->iamthif_file_ext)) {
		/* found the iamthif cb */
		DBG("complete the pthi read cb.\n ");
		if (&dev->iamthif_file_ext) {
			DBG("add the pthi read cb to complete.\n ");
			list_add_tail(&priv_cb->cb_list,
				      &complete_list->heci_cb.cb_list);
		}
	}
	return 0;
}

/**
 * _heci_bh_state_ok - check if heci header matches file private data
 *
 * @file_ext: private data of the file object
 * @heci_hdr: header of heci client message
 *
 * returns  !=0 if matches, 0 if no match.
 */
static int _heci_bh_state_ok(struct heci_file_private *file_ext,
					struct heci_msg_hdr *heci_hdr)
{
	return ((file_ext->host_client_id == heci_hdr->host_addr)
		&& (file_ext->me_client_id == heci_hdr->me_addr)
		&& (file_ext->state == HECI_FILE_CONNECTED)
		&& (HECI_READ_COMPLETE != file_ext->reading_state));
}

/**
 * heci_bh_read_client_message - bottom half read routine after ISR to
 * handle the read heci client message data  processing.
 *
 * @complete_list: An instance of our list structure
 * @dev: Device object for our driver
 * @heci_hdr: header of heci client message
 *
 * returns  0 on success, <0 on failure.
 */
static int heci_bh_read_client_message(struct io_heci_list *complete_list,
		struct iamt_heci_device *dev,
		struct heci_msg_hdr *heci_hdr)
{
	struct heci_file_private *file_ext;
	struct heci_cb_private *priv_cb_pos = NULL, *priv_cb_next = NULL;
	unsigned char *buffer = NULL;

	DBG("start client msg\n");
	if (!((dev->read_list.status == 0) &&
	      !list_empty(&dev->read_list.heci_cb.cb_list)))
		goto quit;

	list_for_each_entry_safe(priv_cb_pos, priv_cb_next,
			&dev->read_list.heci_cb.cb_list, cb_list) {
		file_ext = (struct heci_file_private *)
				priv_cb_pos->file_private;
		if ((file_ext != NULL) &&
		    (_heci_bh_state_ok(file_ext, heci_hdr))) {
			spin_lock_bh(&file_ext->read_io_lock);
			file_ext->reading_state = HECI_READING;
			buffer = (unsigned char *)
				(priv_cb_pos->response_buffer.data +
				priv_cb_pos->information);
			BUG_ON(priv_cb_pos->response_buffer.size <
					heci_hdr->length +
					priv_cb_pos->information);

			if (priv_cb_pos->response_buffer.size <
					heci_hdr->length +
					priv_cb_pos->information) {
				DBG("message overflow.\n");
				list_del(&priv_cb_pos->cb_list);
				spin_unlock_bh(&file_ext->read_io_lock);
				return -ENOMEM;
			}
			if (buffer) {
				heci_read_slots(dev, buffer,
						heci_hdr->length);
			}
			priv_cb_pos->information += heci_hdr->length;
			if (heci_hdr->msg_complete) {
				file_ext->status = 0;
				list_del(&priv_cb_pos->cb_list);
				spin_unlock_bh(&file_ext->read_io_lock);
				DBG("completed read host client = %d,"
					"ME client = %d, "
					"data length = %lu\n",
					file_ext->host_client_id,
					file_ext->me_client_id,
					priv_cb_pos->information);

				*(priv_cb_pos->response_buffer.data +
					priv_cb_pos->information) = '\0';
				DBG("priv_cb_pos->res_buffer - %s\n",
					priv_cb_pos->response_buffer.data);
				list_add_tail(&priv_cb_pos->cb_list,
					&complete_list->heci_cb.cb_list);
			} else {
				spin_unlock_bh(&file_ext->read_io_lock);
			}

			break;
		}

	}

quit:
	DBG("message read\n");
	if (!buffer) {
		heci_read_slots(dev, (unsigned char *) dev->rd_msg_buf,
						heci_hdr->length);
		DBG("discarding message, header=%08x.\n",
				*(__u32 *) dev->rd_msg_buf);
	}

	return 0;
}

/**
 * _heci_bh_iamthif_read - prepare to read iamthif data.
 *
 * @dev: Device object for our driver.
 * @slots: free slots.
 *
 * returns  0, OK; otherwise, error.
 */
static int _heci_bh_iamthif_read(struct iamt_heci_device *dev,	__s32 *slots)
{

	if (((*slots) * sizeof(__u32)) >= (sizeof(struct heci_msg_hdr)
			+ sizeof(struct hbm_flow_control))) {
		*slots -= (sizeof(struct heci_msg_hdr) +
				sizeof(struct hbm_flow_control) + 3) / 4;
		if (!heci_send_flow_control(dev, &dev->iamthif_file_ext)) {
			DBG("iamthif flow control failed\n");
		} else {
			DBG("iamthif flow control success\n");
			dev->iamthif_state = HECI_IAMTHIF_READING;
			dev->iamthif_flow_control_pending = 0;
			dev->iamthif_msg_buf_index = 0;
			dev->iamthif_msg_buf_size = 0;
			dev->iamthif_stall_timer = IAMTHIF_STALL_TIMER;
			dev->host_buffer_is_empty = host_buffer_is_empty(dev);
		}
		return 0;
	} else {
		return -ECOMPLETE_MESSAGE;
	}
}

/**
 * _heci_bh_close - process close related operation.
 *
 * @dev: Device object for our driver.
 * @slots: free slots.
 * @priv_cb_pos: callback block.
 * @file_ext: private data of the file object.
 * @cmpl_list: complete list.
 *
 * returns  0, OK; otherwise, error.
 */
static int _heci_bh_close(struct iamt_heci_device *dev,	__s32 *slots,
			struct heci_cb_private *priv_cb_pos,
			struct heci_file_private *file_ext,
			struct io_heci_list *cmpl_list)
{
	if ((*slots * sizeof(__u32)) >= (sizeof(struct heci_msg_hdr) +
			sizeof(struct hbm_client_disconnect_request))) {
		*slots -= (sizeof(struct heci_msg_hdr) +
			sizeof(struct hbm_client_disconnect_request) + 3) / 4;

		if (!heci_disconnect(dev, file_ext)) {
			file_ext->status = 0;
			priv_cb_pos->information = 0;
			list_move_tail(&priv_cb_pos->cb_list,
					&cmpl_list->heci_cb.cb_list);
			return -ECOMPLETE_MESSAGE;
		} else {
			file_ext->state = HECI_FILE_DISCONNECTING;
			file_ext->status = 0;
			priv_cb_pos->information = 0;
			list_move_tail(&priv_cb_pos->cb_list,
					&dev->ctrl_rd_list.heci_cb.cb_list);
			file_ext->timer_count = HECI_CONNECT_TIMEOUT;
		}
	} else {
		/* return the cancel routine */
		return -ECORRUPTED_MESSAGE_HEADER;
	}

	return 0;
}

/**
 * _heci_hb_close - process read related operation.
 *
 * @dev: Device object for our driver.
 * @slots: free slots.
 * @priv_cb_pos: callback block.
 * @file_ext: private data of the file object.
 * @cmpl_list: complete list.
 *
 * returns 0, OK; otherwise, error.
 */
static int _heci_bh_read(struct iamt_heci_device *dev,	__s32 *slots,
			struct heci_cb_private *priv_cb_pos,
			struct heci_file_private *file_ext,
			struct io_heci_list *cmpl_list)
{
	if ((*slots * sizeof(__u32)) >= (sizeof(struct heci_msg_hdr) +
			sizeof(struct hbm_flow_control))) {
		*slots -= (sizeof(struct heci_msg_hdr) +
			sizeof(struct hbm_flow_control) + 3) / 4;
		if (!heci_send_flow_control(dev, file_ext)) {
			file_ext->status = -ENODEV;
			priv_cb_pos->information = 0;
			list_move_tail(&priv_cb_pos->cb_list,
					&cmpl_list->heci_cb.cb_list);
			return -ENODEV;
		} else {
			list_move_tail(&priv_cb_pos->cb_list,
					&dev->read_list.heci_cb.cb_list);
		}
	} else {
		/* return the cancel routine */
		list_del(&priv_cb_pos->cb_list);
		return -ECORRUPTED_MESSAGE_HEADER;
	}

	return 0;
}


/**
 * _heci_bh_ioctl - process ioctl related operation.
 *
 * @dev: Device object for our driver.
 * @slots: free slots.
 * @priv_cb_pos: callback block.
 * @file_ext: private data of the file object.
 * @cmpl_list: complete list.
 *
 * returns  0, OK; otherwise, error.
 */
static int _heci_bh_ioctl(struct iamt_heci_device *dev,	__s32 *slots,
			struct heci_cb_private *priv_cb_pos,
			struct heci_file_private *file_ext,
			struct io_heci_list *cmpl_list)
{
	if ((*slots * sizeof(__u32)) >= (sizeof(struct heci_msg_hdr) +
			sizeof(struct hbm_client_connect_request))) {
		file_ext->state = HECI_FILE_CONNECTING;
		*slots -= (sizeof(struct heci_msg_hdr) +
			sizeof(struct hbm_client_connect_request) + 3) / 4;
		if (!heci_connect(dev, file_ext)) {
			file_ext->status = -ENODEV;
			priv_cb_pos->information = 0;
			list_del(&priv_cb_pos->cb_list);
			return -ENODEV;
		} else {
			list_move_tail(&priv_cb_pos->cb_list,
				&dev->ctrl_rd_list.heci_cb.cb_list);
			file_ext->timer_count = HECI_CONNECT_TIMEOUT;
		}
	} else {
		/* return the cancel routine */
		list_del(&priv_cb_pos->cb_list);
		return -ECORRUPTED_MESSAGE_HEADER;
	}

	return 0;
}

/**
 * _heci_bh_cmpl - process completed and no-iamthif operation.
 *
 * @dev: Device object for our driver.
 * @slots: free slots.
 * @priv_cb_pos: callback block.
 * @file_ext: private data of the file object.
 * @cmpl_list: complete list.
 *
 * returns  0, OK; otherwise, error.
 */
static int _heci_bh_cmpl(struct iamt_heci_device *dev,	__s32 *slots,
			struct heci_cb_private *priv_cb_pos,
			struct heci_file_private *file_ext,
			struct io_heci_list *cmpl_list)
{
	struct heci_msg_hdr *heci_hdr;

	if ((*slots * sizeof(__u32)) >= (sizeof(struct heci_msg_hdr) +
			(priv_cb_pos->request_buffer.size -
			priv_cb_pos->information))) {
		heci_hdr = (struct heci_msg_hdr *) &dev->wr_msg_buf[0];
		heci_hdr->host_addr = file_ext->host_client_id;
		heci_hdr->me_addr = file_ext->me_client_id;
		heci_hdr->length = ((priv_cb_pos->request_buffer.size) -
				(priv_cb_pos->information));
		heci_hdr->msg_complete = 1;
		heci_hdr->reserved = 0;
		DBG("priv_cb_pos->request_buffer.size =%d"
			"heci_hdr->msg_complete= %d\n",
				priv_cb_pos->request_buffer.size,
				heci_hdr->msg_complete);
		DBG("priv_cb_pos->information  =%lu\n",
				priv_cb_pos->information);
		DBG("heci_hdr->length  =%d\n",
				heci_hdr->length);
		*slots -= (sizeof(struct heci_msg_hdr) +
				heci_hdr->length + 3) / 4;
		if (!heci_write_message(dev, heci_hdr,
				(unsigned char *)
				(priv_cb_pos->request_buffer.data +
				priv_cb_pos->information),
				heci_hdr->length)) {
			file_ext->status = -ENODEV;
			list_move_tail(&priv_cb_pos->cb_list,
				&cmpl_list->heci_cb.cb_list);
			return -ENODEV;
		} else {
			flow_ctrl_reduce(dev, file_ext);
			file_ext->status = 0;
			priv_cb_pos->information += heci_hdr->length;
			list_move_tail(&priv_cb_pos->cb_list,
				&dev->write_waiting_list.heci_cb.cb_list);
		}
	} else if (*slots == ((dev->host_hw_state & H_CBD) >> 24)) {
		/* buffer is still empty */
		heci_hdr = (struct heci_msg_hdr *) &dev->wr_msg_buf[0];
		heci_hdr->host_addr = file_ext->host_client_id;
		heci_hdr->me_addr = file_ext->me_client_id;
		heci_hdr->length =
			(*slots * sizeof(__u32)) - sizeof(struct heci_msg_hdr);
		heci_hdr->msg_complete = 0;
		heci_hdr->reserved = 0;

		(*slots) -= (sizeof(struct heci_msg_hdr) +
				heci_hdr->length + 3) / 4;
		if (!heci_write_message(dev, heci_hdr,
					(unsigned char *)
					(priv_cb_pos->request_buffer.data +
					priv_cb_pos->information),
					heci_hdr->length)) {
			file_ext->status = -ENODEV;
			list_move_tail(&priv_cb_pos->cb_list,
				&cmpl_list->heci_cb.cb_list);
			return -ENODEV;
		} else {
			priv_cb_pos->information += heci_hdr->length;
			DBG("priv_cb_pos->request_buffer.size =%d"
					" heci_hdr->msg_complete= %d\n",
					priv_cb_pos->request_buffer.size,
					heci_hdr->msg_complete);
			DBG("priv_cb_pos->information  =%lu\n",
					priv_cb_pos->information);
			DBG("heci_hdr->length  =%d\n", heci_hdr->length);
		}
		return -ECOMPLETE_MESSAGE;
	} else {
		return -ECORRUPTED_MESSAGE_HEADER;
	}

	return 0;
}

/**
 * _heci_bh_cmpl_iamthif - process completed iamthif operation.
 *
 * @dev: Device object for our driver.
 * @slots: free slots.
 * @priv_cb_pos: callback block.
 * @file_ext: private data of the file object.
 * @cmpl_list: complete list.
 *
 * returns  0, OK; otherwise, error.
 */
static int _heci_bh_cmpl_iamthif(struct iamt_heci_device *dev, __s32 *slots,
			struct heci_cb_private *priv_cb_pos,
			struct heci_file_private *file_ext,
			struct io_heci_list *cmpl_list)
{
	struct heci_msg_hdr *heci_hdr;

	if ((*slots * sizeof(__u32)) >= (sizeof(struct heci_msg_hdr) +
			dev->iamthif_msg_buf_size -
			dev->iamthif_msg_buf_index)) {
		heci_hdr = (struct heci_msg_hdr *) &dev->wr_msg_buf[0];
		heci_hdr->host_addr = file_ext->host_client_id;
		heci_hdr->me_addr = file_ext->me_client_id;
		heci_hdr->length = dev->iamthif_msg_buf_size -
			dev->iamthif_msg_buf_index;
		heci_hdr->msg_complete = 1;
		heci_hdr->reserved = 0;

		*slots -= (sizeof(struct heci_msg_hdr) +
				heci_hdr->length + 3) / 4;

		if (!heci_write_message(dev, heci_hdr,
					(dev->iamthif_msg_buf +
					dev->iamthif_msg_buf_index),
					heci_hdr->length)) {
			dev->iamthif_state = HECI_IAMTHIF_IDLE;
			file_ext->status = -ENODEV;
			list_del(&priv_cb_pos->cb_list);
			return -ENODEV;
		} else {
			flow_ctrl_reduce(dev, file_ext);
			dev->iamthif_msg_buf_index += heci_hdr->length;
			priv_cb_pos->information = dev->iamthif_msg_buf_index;
			file_ext->status = 0;
			dev->iamthif_state = HECI_IAMTHIF_FLOW_CONTROL;
			dev->iamthif_flow_control_pending = 1;
			/* save iamthif cb sent to pthi client */
			dev->iamthif_current_cb = priv_cb_pos;
			list_move_tail(&priv_cb_pos->cb_list,
				&dev->write_waiting_list.heci_cb.cb_list);

		}
	} else if (*slots == ((dev->host_hw_state & H_CBD) >> 24)) {
			/* buffer is still empty */
		heci_hdr = (struct heci_msg_hdr *) &dev->wr_msg_buf[0];
		heci_hdr->host_addr = file_ext->host_client_id;
		heci_hdr->me_addr = file_ext->me_client_id;
		heci_hdr->length =
			(*slots * sizeof(__u32)) - sizeof(struct heci_msg_hdr);
		heci_hdr->msg_complete = 0;
		heci_hdr->reserved = 0;

		*slots -= (sizeof(struct heci_msg_hdr) +
				heci_hdr->length + 3) / 4;

		if (!heci_write_message(dev, heci_hdr,
					(dev->iamthif_msg_buf +
					dev->iamthif_msg_buf_index),
					heci_hdr->length)) {
			file_ext->status = -ENODEV;
			list_del(&priv_cb_pos->cb_list);
		} else {
			dev->iamthif_msg_buf_index += heci_hdr->length;
		}
		return -ECOMPLETE_MESSAGE;
	} else {
		return -ECORRUPTED_MESSAGE_HEADER;
	}

	return 0;
}

/**
 * heci_bh_write_handler - bottom half write routine after
 * ISR to handle the write processing.
 *
 * @cmpl_list: An instance of our list structure
 * @dev: Device object for our driver
 * @slots: slots to write.
 *
 * returns 0 on success, <0 on failure.
 */
static int heci_bh_write_handler(struct io_heci_list *cmpl_list,
		struct iamt_heci_device *dev,
		__s32 *slots)
{

	struct heci_file_private *file_ext;
	struct heci_cb_private *priv_cb_pos = NULL, *priv_cb_next = NULL;
	struct io_heci_list *list;
	int ret;

	if (!host_buffer_is_empty(dev)) {
		DBG("host buffer is not empty.\n");
		return 0;
	}
	dev->write_hang = -1;
	*slots = count_empty_write_slots(dev);
	/* complete all waiting for write CB */
	DBG("complete all waiting for write cb.\n");

	list = &dev->write_waiting_list;
	if ((list->status == 0)
	    && !list_empty(&list->heci_cb.cb_list)) {
		list_for_each_entry_safe(priv_cb_pos, priv_cb_next,
				&list->heci_cb.cb_list, cb_list) {
			file_ext = (struct heci_file_private *)
					priv_cb_pos->file_private;
			if (file_ext != NULL) {
				file_ext->status = 0;
				list_del(&priv_cb_pos->cb_list);
				if ((HECI_WRITING == file_ext->writing_state) &&
					(priv_cb_pos->major_file_operations ==
						HECI_WRITE) &&
					(file_ext != &dev->iamthif_file_ext)) {
					DBG("HECI WRITE COMPLETE\n");
					file_ext->writing_state =
						HECI_WRITE_COMPLETE;
					list_add_tail(&priv_cb_pos->cb_list,
						&cmpl_list->heci_cb.cb_list);
				}
				if (file_ext == &dev->iamthif_file_ext) {
					DBG("check iamthif flow control.\n");
					if (dev->iamthif_flow_control_pending) {
						ret = _heci_bh_iamthif_read(dev,
									slots);
						if (ret != 0)
							return ret;
					}
				}
			}

		}
	}

	if ((dev->stop) && (!dev->wd_pending)) {
		dev->wd_stoped = 1;
		wake_up_interruptible(&dev->wait_stop_wd);
		return 0;
	}

	if (dev->extra_write_index != 0) {
		DBG("extra_write_index =%d.\n",	dev->extra_write_index);
		heci_write_message(dev,
				(struct heci_msg_hdr *) &dev->ext_msg_buf[0],
				(unsigned char *) &dev->ext_msg_buf[1],
				(dev->extra_write_index - 1) * sizeof(__u32));
		*slots -= dev->extra_write_index;
		dev->extra_write_index = 0;
	}
	if (dev->heci_state == HECI_ENABLED) {
		if ((dev->wd_pending)
		    && flow_ctrl_creds(dev, &dev->wd_file_ext)) {
			if (!heci_send_wd(dev))
				DBG("wd send failed.\n");
			else
				flow_ctrl_reduce(dev, &dev->wd_file_ext);

			dev->wd_pending = 0;

			if (dev->wd_timeout != 0) {
				*slots -= (sizeof(struct heci_msg_hdr) +
					 HECI_START_WD_DATA_SIZE + 3) / 4;
				dev->wd_due_counter = 2;
			} else {
				*slots -= (sizeof(struct heci_msg_hdr) +
					 HECI_WD_PARAMS_SIZE + 3) / 4;
				dev->wd_due_counter = 0;
			}

		}
	}
	if (dev->stop)
		return ~ENODEV;

	/* complete control write list CB */
	if (dev->ctrl_wr_list.status == 0) {
		/* complete control write list CB */
		DBG("complete control write list cb.\n");
		list_for_each_entry_safe(priv_cb_pos, priv_cb_next,
				&dev->ctrl_wr_list.heci_cb.cb_list, cb_list) {
			file_ext = (struct heci_file_private *)
				priv_cb_pos->file_private;
			if (file_ext == NULL) {
				list_del(&priv_cb_pos->cb_list);
				return -ENODEV;
			}
			switch (priv_cb_pos->major_file_operations) {
			case HECI_CLOSE:
				/* send disconnect message */
				ret = _heci_bh_close(dev, slots,
						     priv_cb_pos,
						     file_ext, cmpl_list);
				if (ret != 0)
					return ret;

				break;
			case HECI_READ:
				/* send flow control message */
				ret = _heci_bh_read(dev, slots,
						    priv_cb_pos,
						    file_ext, cmpl_list);
				if (ret != 0)
					return ret;

				break;
			case HECI_IOCTL:
				/* connect message */
				if (!other_client_is_connecting(dev, file_ext))
					continue;
				ret = _heci_bh_ioctl(dev, slots,
						     priv_cb_pos,
						     file_ext, cmpl_list);
				if (ret != 0)
					return ret;

				break;

			default:
				BUG();
			}

		}
	}
	/* complete  write list CB */
	if ((dev->write_list.status == 0)
	    && !list_empty(&dev->write_list.heci_cb.cb_list)) {
		DBG("complete write list cb.\n");
		list_for_each_entry_safe(priv_cb_pos, priv_cb_next,
				&dev->write_list.heci_cb.cb_list, cb_list) {
			file_ext = (struct heci_file_private *)
					priv_cb_pos->file_private;

			if (file_ext != NULL) {
				if (file_ext != &dev->iamthif_file_ext) {
					if (!flow_ctrl_creds(dev, file_ext)) {
						DBG("No flow control"
						    " credentials for client"
						    " %d, not sending.\n",
						    file_ext->host_client_id);
						continue;
					}
					ret = _heci_bh_cmpl(dev, slots,
							    priv_cb_pos,
							    file_ext,
							    cmpl_list);
					if (ret != 0)
						return ret;

				} else if (file_ext == &dev->iamthif_file_ext) {
					/* IAMTHIF IOCTL */
					DBG("complete pthi write cb.\n");
					if (!flow_ctrl_creds(dev, file_ext)) {
						DBG("No flow control"
						    " credentials for pthi"
						    " client %d.\n",
						    file_ext->host_client_id);
						continue;
					}
					ret = _heci_bh_cmpl_iamthif(dev, slots,
								   priv_cb_pos,
								   file_ext,
								   cmpl_list);
					if (ret != 0)
						return ret;

				}
			}

		}
	}
	return 0;
}


/**
 * is_treat_specially_client  - check if the message belong
 * to the file private data.
 *
 * @file_ext: private data of the file object
 * @rs: connect response bus message
 * @dev: Device object for our driver
 *
 * returns 0 on success, <0 on failure.
 */
static int is_treat_specially_client(struct heci_file_private *file_ext,
		struct hbm_client_connect_response *rs)
{
	int ret = 0;

	if ((file_ext->host_client_id == rs->host_addr) &&
	    (file_ext->me_client_id == rs->me_addr)) {
		if (rs->status == 0) {
			DBG("client connect status = 0x%08x.\n", rs->status);
			file_ext->state = HECI_FILE_CONNECTED;
			file_ext->status = 0;
		} else {
			DBG("client connect status = 0x%08x.\n", rs->status);
			file_ext->state = HECI_FILE_DISCONNECTED;
			file_ext->status = -ENODEV;
		}
		ret = 1;
	}
	DBG("client state = %d.\n", file_ext->state);
	return ret;
}

/**
 * heci_client_connect_response  - connect response bh routine
 *
 * @dev: Device object for our driver
 * @rs: connect response bus message
 */
static void heci_client_connect_response(struct iamt_heci_device *dev,
		struct hbm_client_connect_response *rs)
{

	struct heci_file_private *file_ext;
	struct heci_cb_private *priv_cb_pos = NULL, *priv_cb_next = NULL;

	/* if WD or iamthif client treat specially */

	if ((is_treat_specially_client(&(dev->wd_file_ext), rs)) ||
	    (is_treat_specially_client(&(dev->iamthif_file_ext), rs)))
		return;

	if (dev->ctrl_rd_list.status == 0
	    && !list_empty(&dev->ctrl_rd_list.heci_cb.cb_list)) {
		list_for_each_entry_safe(priv_cb_pos, priv_cb_next,
			&dev->ctrl_rd_list.heci_cb.cb_list, cb_list) {
			file_ext = (struct heci_file_private *)
					priv_cb_pos->file_private;
			if (file_ext == NULL) {
				list_del(&priv_cb_pos->cb_list);
				return;
			}
			if (HECI_IOCTL == priv_cb_pos->major_file_operations) {
				if (is_treat_specially_client(file_ext, rs)) {
					list_del(&priv_cb_pos->cb_list);
					file_ext->status = 0;
					file_ext->timer_count = 0;
					break;
				}
			}
		}
	}
}

/**
 * heci_client_disconnect_response  - disconnect response bh routine
 *
 * @dev: Device object for our driver
 * @rs: disconnect response bus message
 */
static void heci_client_disconnect_response(struct iamt_heci_device *dev,
					struct hbm_client_connect_response *rs)
{
	struct heci_file_private *file_ext;
	struct heci_cb_private *priv_cb_pos = NULL, *priv_cb_next = NULL;

	if (dev->ctrl_rd_list.status == 0
	    && !list_empty(&dev->ctrl_rd_list.heci_cb.cb_list)) {
		list_for_each_entry_safe(priv_cb_pos, priv_cb_next,
				&dev->ctrl_rd_list.heci_cb.cb_list, cb_list) {
			file_ext = (struct heci_file_private *)
				priv_cb_pos->file_private;

			if (file_ext == NULL) {
				list_del(&priv_cb_pos->cb_list);
				return;
			}

			DBG("list_for_each_entry_safe in ctrl_rd_list.\n");
			if ((file_ext->host_client_id == rs->host_addr) &&
				(file_ext->me_client_id == rs->me_addr)) {

				list_del(&priv_cb_pos->cb_list);
				if (rs->status == 0) {
					file_ext->state =
					    HECI_FILE_DISCONNECTED;
				}

				file_ext->status = 0;
				file_ext->timer_count = 0;
				break;
			}
		}
	}
}

/**
 * same_flow_addr - tell they have same address.
 *
 * @file: private data of the file object.
 * @flow: flow control.
 *
 * returns  !=0, same; 0,not.
 */
static int same_flow_addr(struct heci_file_private *file,
					struct hbm_flow_control *flow)
{
	return ((file->host_client_id == flow->host_addr)
		&& (file->me_client_id == flow->me_addr));
}

/**
 * add_single_flow_creds - add single buffer credentials.
 *
 * @file: private data ot the file object.
 * @flow: flow control.
 */
static void add_single_flow_creds(struct iamt_heci_device *dev,
				  struct hbm_flow_control *flow)
{
	struct heci_me_client *client;
	int i;

	for (i = 0; i < dev->num_heci_me_clients; i++) {
		client = &dev->me_clients[i];
		if ((client != NULL) &&
		    (flow->me_addr == client->client_id)) {
			if (client->props.single_recv_buf != 0) {
				client->flow_ctrl_creds++;
				DBG("recv flow ctrl msg ME %d (single).\n",
				    flow->me_addr);
				DBG("flow control credentials=%d.\n",
				    client->flow_ctrl_creds);
			} else {
				BUG();	/* error in flow control */
			}
		}
	}
}

/**
 * heci_client_flow_control_response  - flow control response bh routine
 *
 * @dev: Device object for our driver
 * @flow_control: flow control response bus message
 */
static void heci_client_flow_control_response(struct iamt_heci_device *dev,
		struct hbm_flow_control *flow_control)
{
	struct heci_file_private *file_pos = NULL;
	struct heci_file_private *file_next = NULL;

	if (flow_control->host_addr == 0) {
		/* single receive buffer */
		add_single_flow_creds(dev, flow_control);
	} else {
		/* normal connection */
		list_for_each_entry_safe(file_pos, file_next,
				&dev->file_list, link) {
			DBG("list_for_each_entry_safe in file_list\n");

			DBG("file_ext of host client %d ME client %d.\n",
			    file_pos->host_client_id,
			    file_pos->me_client_id);
			DBG("flow ctrl msg for host %d ME %d.\n",
			    flow_control->host_addr,
			    flow_control->me_addr);
			if (same_flow_addr(file_pos, flow_control)) {
				DBG("recv ctrl msg for host  %d ME %d.\n",
				    flow_control->host_addr,
				    flow_control->me_addr);
				file_pos->flow_ctrl_creds++;
				DBG("flow control credentials=%d.\n",
				    file_pos->flow_ctrl_creds);
				break;
			}
		}
	}
}

/**
 * same_disconn_addr - tell they have same address
 *
 * @file: private data of the file object.
 * @disconn: disconnection request.
 *
 * returns !=0, same; 0,not.
 */
static int same_disconn_addr(struct heci_file_private *file,
			     struct hbm_client_disconnect_request *disconn)
{
	return ((file->host_client_id == disconn->host_addr)
		&& (file->me_client_id == disconn->me_addr));
}

/**
 * heci_client_disconnect_request  - disconnect request bh routine
 *
 * @dev: Device object for our driver.
 * @disconnect_req: disconnect request bus message.
 */
static void heci_client_disconnect_request(struct iamt_heci_device *dev,
		struct hbm_client_disconnect_request *disconnect_req)
{
	struct heci_msg_hdr *heci_hdr;
	struct hbm_client_connect_response *disconnect_res;
	struct heci_file_private *file_pos = NULL;
	struct heci_file_private *file_next = NULL;

	list_for_each_entry_safe(file_pos, file_next, &dev->file_list, link) {
		if (same_disconn_addr(file_pos, disconnect_req)) {
			DBG("disconnect request host client %d ME client %d.\n",
					disconnect_req->host_addr,
					disconnect_req->me_addr);
			file_pos->state = HECI_FILE_DISCONNECTED;
			file_pos->timer_count = 0;
			if (file_pos == &dev->wd_file_ext) {
				dev->wd_due_counter = 0;
				dev->wd_pending = 0;
			} else if (file_pos == &dev->iamthif_file_ext)
				dev->iamthif_timer = 0;

			/* prepare disconnect response */
			heci_hdr =
				(struct heci_msg_hdr *) &dev->ext_msg_buf[0];
			heci_hdr->host_addr = 0;
			heci_hdr->me_addr = 0;
			heci_hdr->length =
				sizeof(struct hbm_client_connect_response);
			heci_hdr->msg_complete = 1;
			heci_hdr->reserved = 0;

			disconnect_res =
				(struct hbm_client_connect_response *)
				&dev->ext_msg_buf[1];
			disconnect_res->host_addr = file_pos->host_client_id;
			disconnect_res->me_addr = file_pos->me_client_id;
			*(__u8 *) (&disconnect_res->cmd) =
				CLIENT_DISCONNECT_RES_CMD;
			disconnect_res->status = 0;
			dev->extra_write_index = 2;
			break;
		}
	}
}

/**
 * heci_timer - timer function.
 *
 * @data: pointer to the device structure
 *
 * NOTE: This function is called by timer interrupt work
 */
void heci_wd_timer(unsigned long data)
{
	struct iamt_heci_device *dev = (struct iamt_heci_device *) data;

	DBG("send watchdog.\n");
	spin_lock_bh(&dev->device_lock);
	if (dev->heci_state != HECI_ENABLED) {
		mod_timer(&dev->wd_timer, round_jiffies(jiffies + 2 * HZ));
		spin_unlock_bh(&dev->device_lock);
		return;
	}
	if (dev->wd_file_ext.state != HECI_FILE_CONNECTED) {
		mod_timer(&dev->wd_timer, round_jiffies(jiffies + 2 * HZ));
		spin_unlock_bh(&dev->device_lock);
		return;
	}
	/* Watchdog */
	if ((dev->wd_due_counter != 0) && (dev->wd_bypass == 0)) {
		if (--dev->wd_due_counter == 0) {
			if (dev->host_buffer_is_empty &&
			    flow_ctrl_creds(dev, &dev->wd_file_ext)) {
				dev->host_buffer_is_empty = 0;
				if (!heci_send_wd(dev)) {
					DBG("wd send failed.\n");
				} else {
					flow_ctrl_reduce(dev,
							 &dev->wd_file_ext);
				}

				if (dev->wd_timeout != 0)
					dev->wd_due_counter = 2;
				else
					dev->wd_due_counter = 0;

			} else
				dev->wd_pending = 1;

		}
	}
	if (dev->iamthif_stall_timer != 0) {
		if (--dev->iamthif_stall_timer == 0) {
			DBG("reseting because of hang to PTHI.\n");
			heci_reset(dev, 1);
			dev->iamthif_msg_buf_size = 0;
			dev->iamthif_msg_buf_index = 0;
			dev->iamthif_canceled = 0;
			dev->iamthif_ioctl = 1;
			dev->iamthif_state = HECI_IAMTHIF_IDLE;
			dev->iamthif_timer = 0;
			spin_unlock_bh(&dev->device_lock);

			if (dev->iamthif_current_cb)
				heci_free_cb_private(dev->iamthif_current_cb);

			spin_lock_bh(&dev->device_lock);
			dev->iamthif_file_object = NULL;
			dev->iamthif_current_cb = NULL;
			run_next_iamthif_cmd(dev);
		}
	}
	mod_timer(&dev->wd_timer, round_jiffies(jiffies + 2 * HZ));
	spin_unlock_bh(&dev->device_lock);
}
