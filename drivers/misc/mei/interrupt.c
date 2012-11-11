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

#include "mei_dev.h"
#include <linux/mei.h>
#include "hw.h"
#include "interface.h"


/**
 * mei_interrupt_quick_handler - The ISR of the MEI device
 *
 * @irq: The irq number
 * @dev_id: pointer to the device structure
 *
 * returns irqreturn_t
 */
irqreturn_t mei_interrupt_quick_handler(int irq, void *dev_id)
{
	struct mei_device *dev = (struct mei_device *) dev_id;
	u32 csr_reg = mei_hcsr_read(dev);

	if ((csr_reg & H_IS) != H_IS)
		return IRQ_NONE;

	/* clear H_IS bit in H_CSR */
	mei_reg_write(dev, H_CSR, csr_reg);

	return IRQ_WAKE_THREAD;
}

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
		dev_dbg(&dev->pdev->dev, "discarding message, header =%08x.\n",
				*(u32 *) dev->rd_msg_buf);
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

	if (mei_disconnect(dev, cl)) {
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
 * is_treat_specially_client - checks if the message belongs
 * to the file private data.
 *
 * @cl: private data of the file object
 * @rs: connect response bus message
 *
 */
static bool is_treat_specially_client(struct mei_cl *cl,
		struct hbm_client_connect_response *rs)
{

	if (cl->host_client_id == rs->host_addr &&
	    cl->me_client_id == rs->me_addr) {
		if (!rs->status) {
			cl->state = MEI_FILE_CONNECTED;
			cl->status = 0;

		} else {
			cl->state = MEI_FILE_DISCONNECTED;
			cl->status = -ENODEV;
		}
		cl->timer_count = 0;

		return true;
	}
	return false;
}

/**
 * mei_client_connect_response - connects to response irq routine
 *
 * @dev: the device structure
 * @rs: connect response bus message
 */
static void mei_client_connect_response(struct mei_device *dev,
		struct hbm_client_connect_response *rs)
{

	struct mei_cl *cl;
	struct mei_cl_cb *pos = NULL, *next = NULL;

	dev_dbg(&dev->pdev->dev,
			"connect_response:\n"
			"ME Client = %d\n"
			"Host Client = %d\n"
			"Status = %d\n",
			rs->me_addr,
			rs->host_addr,
			rs->status);

	/* if WD or iamthif client treat specially */

	if (is_treat_specially_client(&(dev->wd_cl), rs)) {
		dev_dbg(&dev->pdev->dev, "successfully connected to WD client.\n");
		mei_watchdog_register(dev);

		/* next step in the state maching */
		mei_amthif_host_init(dev);
		return;
	}

	if (is_treat_specially_client(&(dev->iamthif_cl), rs)) {
		dev->iamthif_state = MEI_IAMTHIF_IDLE;
		return;
	}
	list_for_each_entry_safe(pos, next, &dev->ctrl_rd_list.list, list) {

		cl = pos->cl;
		if (!cl) {
			list_del(&pos->list);
			return;
		}
		if (pos->fop_type == MEI_FOP_IOCTL) {
			if (is_treat_specially_client(cl, rs)) {
				list_del(&pos->list);
				cl->status = 0;
				cl->timer_count = 0;
				break;
			}
		}
	}
}

/**
 * mei_client_disconnect_response - disconnects from response irq routine
 *
 * @dev: the device structure
 * @rs: disconnect response bus message
 */
static void mei_client_disconnect_response(struct mei_device *dev,
					struct hbm_client_connect_response *rs)
{
	struct mei_cl *cl;
	struct mei_cl_cb *pos = NULL, *next = NULL;

	dev_dbg(&dev->pdev->dev,
			"disconnect_response:\n"
			"ME Client = %d\n"
			"Host Client = %d\n"
			"Status = %d\n",
			rs->me_addr,
			rs->host_addr,
			rs->status);

	list_for_each_entry_safe(pos, next, &dev->ctrl_rd_list.list, list) {
		cl = pos->cl;

		if (!cl) {
			list_del(&pos->list);
			return;
		}

		dev_dbg(&dev->pdev->dev, "list_for_each_entry_safe in ctrl_rd_list.\n");
		if (cl->host_client_id == rs->host_addr &&
		    cl->me_client_id == rs->me_addr) {

			list_del(&pos->list);
			if (!rs->status)
				cl->state = MEI_FILE_DISCONNECTED;

			cl->status = 0;
			cl->timer_count = 0;
			break;
		}
	}
}

/**
 * same_flow_addr - tells if they have the same address.
 *
 * @file: private data of the file object.
 * @flow: flow control.
 *
 * returns  !=0, same; 0,not.
 */
static int same_flow_addr(struct mei_cl *cl, struct hbm_flow_control *flow)
{
	return (cl->host_client_id == flow->host_addr &&
		cl->me_client_id == flow->me_addr);
}

/**
 * add_single_flow_creds - adds single buffer credentials.
 *
 * @file: private data ot the file object.
 * @flow: flow control.
 */
static void add_single_flow_creds(struct mei_device *dev,
				  struct hbm_flow_control *flow)
{
	struct mei_me_client *client;
	int i;

	for (i = 0; i < dev->me_clients_num; i++) {
		client = &dev->me_clients[i];
		if (client && flow->me_addr == client->client_id) {
			if (client->props.single_recv_buf) {
				client->mei_flow_ctrl_creds++;
				dev_dbg(&dev->pdev->dev, "recv flow ctrl msg ME %d (single).\n",
				    flow->me_addr);
				dev_dbg(&dev->pdev->dev, "flow control credentials =%d.\n",
				    client->mei_flow_ctrl_creds);
			} else {
				BUG();	/* error in flow control */
			}
		}
	}
}

/**
 * mei_client_flow_control_response - flow control response irq routine
 *
 * @dev: the device structure
 * @flow_control: flow control response bus message
 */
static void mei_client_flow_control_response(struct mei_device *dev,
		struct hbm_flow_control *flow_control)
{
	struct mei_cl *cl_pos = NULL;
	struct mei_cl *cl_next = NULL;

	if (!flow_control->host_addr) {
		/* single receive buffer */
		add_single_flow_creds(dev, flow_control);
	} else {
		/* normal connection */
		list_for_each_entry_safe(cl_pos, cl_next,
				&dev->file_list, link) {
			dev_dbg(&dev->pdev->dev, "list_for_each_entry_safe in file_list\n");

			dev_dbg(&dev->pdev->dev, "cl of host client %d ME client %d.\n",
			    cl_pos->host_client_id,
			    cl_pos->me_client_id);
			dev_dbg(&dev->pdev->dev, "flow ctrl msg for host %d ME %d.\n",
			    flow_control->host_addr,
			    flow_control->me_addr);
			if (same_flow_addr(cl_pos, flow_control)) {
				dev_dbg(&dev->pdev->dev, "recv ctrl msg for host  %d ME %d.\n",
				    flow_control->host_addr,
				    flow_control->me_addr);
				cl_pos->mei_flow_ctrl_creds++;
				dev_dbg(&dev->pdev->dev, "flow control credentials = %d.\n",
				    cl_pos->mei_flow_ctrl_creds);
				break;
			}
		}
	}
}

/**
 * same_disconn_addr - tells if they have the same address
 *
 * @file: private data of the file object.
 * @disconn: disconnection request.
 *
 * returns !=0, same; 0,not.
 */
static int same_disconn_addr(struct mei_cl *cl,
			     struct hbm_client_connect_request *req)
{
	return (cl->host_client_id == req->host_addr &&
		cl->me_client_id == req->me_addr);
}

/**
 * mei_client_disconnect_request - disconnects from request irq routine
 *
 * @dev: the device structure.
 * @disconnect_req: disconnect request bus message.
 */
static void mei_client_disconnect_request(struct mei_device *dev,
		struct hbm_client_connect_request *disconnect_req)
{
	struct mei_msg_hdr *mei_hdr;
	struct hbm_client_connect_response *disconnect_res;
	struct mei_cl *cl_pos = NULL;
	struct mei_cl *cl_next = NULL;

	list_for_each_entry_safe(cl_pos, cl_next, &dev->file_list, link) {
		if (same_disconn_addr(cl_pos, disconnect_req)) {
			dev_dbg(&dev->pdev->dev, "disconnect request host client %d ME client %d.\n",
					disconnect_req->host_addr,
					disconnect_req->me_addr);
			cl_pos->state = MEI_FILE_DISCONNECTED;
			cl_pos->timer_count = 0;
			if (cl_pos == &dev->wd_cl)
				dev->wd_pending = false;
			else if (cl_pos == &dev->iamthif_cl)
				dev->iamthif_timer = 0;

			/* prepare disconnect response */
			mei_hdr =
				(struct mei_msg_hdr *) &dev->ext_msg_buf[0];
			mei_hdr->host_addr = 0;
			mei_hdr->me_addr = 0;
			mei_hdr->length =
				sizeof(struct hbm_client_connect_response);
			mei_hdr->msg_complete = 1;
			mei_hdr->reserved = 0;

			disconnect_res =
				(struct hbm_client_connect_response *)
				&dev->ext_msg_buf[1];
			disconnect_res->host_addr = cl_pos->host_client_id;
			disconnect_res->me_addr = cl_pos->me_client_id;
			disconnect_res->hbm_cmd = CLIENT_DISCONNECT_RES_CMD;
			disconnect_res->status = 0;
			dev->extra_write_index = 2;
			break;
		}
	}
}


/**
 * mei_irq_thread_read_bus_message - bottom half read routine after ISR to
 * handle the read bus message cmd processing.
 *
 * @dev: the device structure
 * @mei_hdr: header of bus message
 */
static void mei_irq_thread_read_bus_message(struct mei_device *dev,
		struct mei_msg_hdr *mei_hdr)
{
	struct mei_bus_message *mei_msg;
	struct hbm_host_version_response *version_res;
	struct hbm_client_connect_response *connect_res;
	struct hbm_client_connect_response *disconnect_res;
	struct hbm_client_connect_request *disconnect_req;
	struct hbm_flow_control *flow_control;
	struct hbm_props_response *props_res;
	struct hbm_host_enum_response *enum_res;
	struct hbm_host_stop_request *host_stop_req;
	int res;


	/* read the message to our buffer */
	BUG_ON(mei_hdr->length >= sizeof(dev->rd_msg_buf));
	mei_read_slots(dev, dev->rd_msg_buf, mei_hdr->length);
	mei_msg = (struct mei_bus_message *)dev->rd_msg_buf;

	switch (mei_msg->hbm_cmd) {
	case HOST_START_RES_CMD:
		version_res = (struct hbm_host_version_response *) mei_msg;
		if (version_res->host_version_supported) {
			dev->version.major_version = HBM_MAJOR_VERSION;
			dev->version.minor_version = HBM_MINOR_VERSION;
			if (dev->dev_state == MEI_DEV_INIT_CLIENTS &&
			    dev->init_clients_state == MEI_START_MESSAGE) {
				dev->init_clients_timer = 0;
				mei_host_enum_clients_message(dev);
			} else {
				dev->recvd_msg = false;
				dev_dbg(&dev->pdev->dev, "IMEI reset due to received host start response bus message.\n");
				mei_reset(dev, 1);
				return;
			}
		} else {
			dev->version = version_res->me_max_version;
			/* send stop message */
			mei_hdr = (struct mei_msg_hdr *)&dev->wr_msg_buf[0];
			mei_hdr->host_addr = 0;
			mei_hdr->me_addr = 0;
			mei_hdr->length = sizeof(struct hbm_host_stop_request);
			mei_hdr->msg_complete = 1;
			mei_hdr->reserved = 0;

			host_stop_req = (struct hbm_host_stop_request *)
							&dev->wr_msg_buf[1];

			memset(host_stop_req,
					0,
					sizeof(struct hbm_host_stop_request));
			host_stop_req->hbm_cmd = HOST_STOP_REQ_CMD;
			host_stop_req->reason = DRIVER_STOP_REQUEST;
			mei_write_message(dev, mei_hdr,
					   (unsigned char *) (host_stop_req),
					   mei_hdr->length);
			dev_dbg(&dev->pdev->dev, "version mismatch.\n");
			return;
		}

		dev->recvd_msg = true;
		dev_dbg(&dev->pdev->dev, "host start response message received.\n");
		break;

	case CLIENT_CONNECT_RES_CMD:
		connect_res =
			(struct hbm_client_connect_response *) mei_msg;
		mei_client_connect_response(dev, connect_res);
		dev_dbg(&dev->pdev->dev, "client connect response message received.\n");
		wake_up(&dev->wait_recvd_msg);
		break;

	case CLIENT_DISCONNECT_RES_CMD:
		disconnect_res =
			(struct hbm_client_connect_response *) mei_msg;
		mei_client_disconnect_response(dev, disconnect_res);
		dev_dbg(&dev->pdev->dev, "client disconnect response message received.\n");
		wake_up(&dev->wait_recvd_msg);
		break;

	case MEI_FLOW_CONTROL_CMD:
		flow_control = (struct hbm_flow_control *) mei_msg;
		mei_client_flow_control_response(dev, flow_control);
		dev_dbg(&dev->pdev->dev, "client flow control response message received.\n");
		break;

	case HOST_CLIENT_PROPERTIES_RES_CMD:
		props_res = (struct hbm_props_response *)mei_msg;
		if (props_res->status || !dev->me_clients) {
			dev_dbg(&dev->pdev->dev, "reset due to received host client properties response bus message wrong status.\n");
			mei_reset(dev, 1);
			return;
		}
		if (dev->me_clients[dev->me_client_presentation_num]
					.client_id == props_res->address) {

			dev->me_clients[dev->me_client_presentation_num].props
						= props_res->client_properties;

			if (dev->dev_state == MEI_DEV_INIT_CLIENTS &&
			    dev->init_clients_state ==
					MEI_CLIENT_PROPERTIES_MESSAGE) {
				dev->me_client_index++;
				dev->me_client_presentation_num++;

				/** Send Client Properties request **/
				res = mei_host_client_properties(dev);
				if (res < 0) {
					dev_dbg(&dev->pdev->dev, "mei_host_client_properties() failed");
					return;
				} else if (!res) {
					/*
					 * No more clients to send to.
					 * Clear Map for indicating now ME clients
					 * with associated host client
					 */
					bitmap_zero(dev->host_clients_map, MEI_CLIENTS_MAX);
					dev->open_handle_count = 0;

					/*
					 * Reserving the first three client IDs
					 * Client Id 0 - Reserved for MEI Bus Message communications
					 * Client Id 1 - Reserved for Watchdog
					 * Client ID 2 - Reserved for AMTHI
					 */
					bitmap_set(dev->host_clients_map, 0, 3);
					dev->dev_state = MEI_DEV_ENABLED;

					/* if wd initialization fails, initialization the AMTHI client,
					 * otherwise the AMTHI client will be initialized after the WD client connect response
					 * will be received
					 */
					if (mei_wd_host_init(dev))
						mei_amthif_host_init(dev);
				}

			} else {
				dev_dbg(&dev->pdev->dev, "reset due to received host client properties response bus message");
				mei_reset(dev, 1);
				return;
			}
		} else {
			dev_dbg(&dev->pdev->dev, "reset due to received host client properties response bus message for wrong client ID\n");
			mei_reset(dev, 1);
			return;
		}
		break;

	case HOST_ENUM_RES_CMD:
		enum_res = (struct hbm_host_enum_response *) mei_msg;
		memcpy(dev->me_clients_map, enum_res->valid_addresses, 32);
		if (dev->dev_state == MEI_DEV_INIT_CLIENTS &&
		    dev->init_clients_state == MEI_ENUM_CLIENTS_MESSAGE) {
				dev->init_clients_timer = 0;
				dev->me_client_presentation_num = 0;
				dev->me_client_index = 0;
				mei_allocate_me_clients_storage(dev);
				dev->init_clients_state =
					MEI_CLIENT_PROPERTIES_MESSAGE;
				mei_host_client_properties(dev);
		} else {
			dev_dbg(&dev->pdev->dev, "reset due to received host enumeration clients response bus message.\n");
			mei_reset(dev, 1);
			return;
		}
		break;

	case HOST_STOP_RES_CMD:
		dev->dev_state = MEI_DEV_DISABLED;
		dev_dbg(&dev->pdev->dev, "resetting because of FW stop response.\n");
		mei_reset(dev, 1);
		break;

	case CLIENT_DISCONNECT_REQ_CMD:
		/* search for client */
		disconnect_req = (struct hbm_client_connect_request *)mei_msg;
		mei_client_disconnect_request(dev, disconnect_req);
		break;

	case ME_STOP_REQ_CMD:
		/* prepare stop request */
		mei_hdr = (struct mei_msg_hdr *) &dev->ext_msg_buf[0];
		mei_hdr->host_addr = 0;
		mei_hdr->me_addr = 0;
		mei_hdr->length = sizeof(struct hbm_host_stop_request);
		mei_hdr->msg_complete = 1;
		mei_hdr->reserved = 0;
		host_stop_req =
			(struct hbm_host_stop_request *) &dev->ext_msg_buf[1];
		memset(host_stop_req, 0, sizeof(struct hbm_host_stop_request));
		host_stop_req->hbm_cmd = HOST_STOP_REQ_CMD;
		host_stop_req->reason = DRIVER_STOP_REQUEST;
		host_stop_req->reserved[0] = 0;
		host_stop_req->reserved[1] = 0;
		dev->extra_write_index = 2;
		break;

	default:
		BUG();
		break;

	}
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

	if (mei_send_flow_control(dev, cl)) {
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
	if (mei_connect(dev, cl)) {
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
 * _mei_irq_thread_cmpl - processes completed and no-iamthif operation.
 *
 * @dev: the device structure.
 * @slots: free slots.
 * @cb_pos: callback block.
 * @cl: private data of the file object.
 * @cmpl_list: complete list.
 *
 * returns 0, OK; otherwise, error.
 */
static int _mei_irq_thread_cmpl(struct mei_device *dev,	s32 *slots,
			struct mei_cl_cb *cb_pos,
			struct mei_cl *cl,
			struct mei_cl_cb *cmpl_list)
{
	struct mei_msg_hdr *mei_hdr;

	if ((*slots * sizeof(u32)) >= (sizeof(struct mei_msg_hdr) +
			(cb_pos->request_buffer.size - cb_pos->buf_idx))) {
		mei_hdr = (struct mei_msg_hdr *) &dev->wr_msg_buf[0];
		mei_hdr->host_addr = cl->host_client_id;
		mei_hdr->me_addr = cl->me_client_id;
		mei_hdr->length = cb_pos->request_buffer.size - cb_pos->buf_idx;
		mei_hdr->msg_complete = 1;
		mei_hdr->reserved = 0;
		dev_dbg(&dev->pdev->dev, "cb_pos->request_buffer.size =%d"
			"mei_hdr->msg_complete = %d\n",
				cb_pos->request_buffer.size,
				mei_hdr->msg_complete);
		dev_dbg(&dev->pdev->dev, "cb_pos->buf_idx  =%lu\n",
				cb_pos->buf_idx);
		dev_dbg(&dev->pdev->dev, "mei_hdr->length  =%d\n",
				mei_hdr->length);
		*slots -= mei_data2slots(mei_hdr->length);
		if (mei_write_message(dev, mei_hdr,
				(unsigned char *)
				(cb_pos->request_buffer.data +
				cb_pos->buf_idx),
				mei_hdr->length)) {
			cl->status = -ENODEV;
			list_move_tail(&cb_pos->list, &cmpl_list->list);
			return -ENODEV;
		} else {
			if (mei_flow_ctrl_reduce(dev, cl))
				return -ENODEV;
			cl->status = 0;
			cb_pos->buf_idx += mei_hdr->length;
			list_move_tail(&cb_pos->list, &dev->write_waiting_list.list);
		}
	} else if (*slots == dev->hbuf_depth) {
		/* buffer is still empty */
		mei_hdr = (struct mei_msg_hdr *) &dev->wr_msg_buf[0];
		mei_hdr->host_addr = cl->host_client_id;
		mei_hdr->me_addr = cl->me_client_id;
		mei_hdr->length =
			(*slots * sizeof(u32)) - sizeof(struct mei_msg_hdr);
		mei_hdr->msg_complete = 0;
		mei_hdr->reserved = 0;
		*slots -= mei_data2slots(mei_hdr->length);
		if (mei_write_message(dev, mei_hdr,
					(unsigned char *)
					(cb_pos->request_buffer.data +
					cb_pos->buf_idx),
					mei_hdr->length)) {
			cl->status = -ENODEV;
			list_move_tail(&cb_pos->list, &cmpl_list->list);
			return -ENODEV;
		} else {
			cb_pos->buf_idx += mei_hdr->length;
			dev_dbg(&dev->pdev->dev,
					"cb_pos->request_buffer.size =%d"
					" mei_hdr->msg_complete = %d\n",
					cb_pos->request_buffer.size,
					mei_hdr->msg_complete);
			dev_dbg(&dev->pdev->dev, "cb_pos->buf_idx  =%lu\n",
					cb_pos->buf_idx);
			dev_dbg(&dev->pdev->dev, "mei_hdr->length  =%d\n",
					mei_hdr->length);
		}
		return -EMSGSIZE;
	} else {
		return -EBADMSG;
	}

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
		dev->rd_msg_hdr = mei_mecbrw_read(dev);
		dev_dbg(&dev->pdev->dev, "slots =%08x.\n", *slots);
		(*slots)--;
		dev_dbg(&dev->pdev->dev, "slots =%08x.\n", *slots);
	}
	mei_hdr = (struct mei_msg_hdr *) &dev->rd_msg_hdr;
	dev_dbg(&dev->pdev->dev, "mei_hdr->length =%d\n", mei_hdr->length);

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
		mei_irq_thread_read_bus_message(dev, mei_hdr);
		dev_dbg(&dev->pdev->dev, "end mei_irq_thread_read_bus_message.\n");
	} else if (mei_hdr->host_addr == dev->iamthif_cl.host_client_id &&
		   (MEI_FILE_CONNECTED == dev->iamthif_cl.state) &&
		   (dev->iamthif_state == MEI_IAMTHIF_READING)) {
		dev_dbg(&dev->pdev->dev, "call mei_irq_thread_read_iamthif_message.\n");
		dev_dbg(&dev->pdev->dev, "mei_hdr->length =%d\n",
				mei_hdr->length);

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
 * @cmpl_list: An instance of our list structure
 * @dev: the device structure
 * @slots: slots to write.
 *
 * returns 0 on success, <0 on failure.
 */
static int mei_irq_thread_write_handler(struct mei_cl_cb *cmpl_list,
		struct mei_device *dev, s32 *slots)
{

	struct mei_cl *cl;
	struct mei_cl_cb *pos = NULL, *next = NULL;
	struct mei_cl_cb *list;
	int ret;

	if (!mei_hbuf_is_empty(dev)) {
		dev_dbg(&dev->pdev->dev, "host buffer is not empty.\n");
		return 0;
	}
	*slots = mei_hbuf_empty_slots(dev);
	if (*slots <= 0)
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
				ret = mei_amthif_irq_read(dev, slots);
				if (ret)
					return ret;
			}
		}
	}

	if (dev->wd_state == MEI_WD_STOPPING) {
		dev->wd_state = MEI_WD_IDLE;
		wake_up_interruptible(&dev->wait_stop_wd);
	}

	if (dev->extra_write_index) {
		dev_dbg(&dev->pdev->dev, "extra_write_index =%d.\n",
				dev->extra_write_index);
		mei_write_message(dev,
				(struct mei_msg_hdr *) &dev->ext_msg_buf[0],
				(unsigned char *) &dev->ext_msg_buf[1],
				(dev->extra_write_index - 1) * sizeof(u32));
		*slots -= dev->extra_write_index;
		dev->extra_write_index = 0;
	}
	if (dev->dev_state == MEI_DEV_ENABLED) {
		if (dev->wd_pending &&
		    mei_flow_ctrl_creds(dev, &dev->wd_cl) > 0) {
			if (mei_wd_send(dev))
				dev_dbg(&dev->pdev->dev, "wd send failed.\n");
			else if (mei_flow_ctrl_reduce(dev, &dev->wd_cl))
				return -ENODEV;

			dev->wd_pending = false;

			if (dev->wd_state == MEI_WD_RUNNING)
				*slots -= mei_data2slots(MEI_WD_START_MSG_SIZE);
			else
				*slots -= mei_data2slots(MEI_WD_STOP_MSG_SIZE);
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
			ret = _mei_irq_thread_close(dev, slots, pos, cl, cmpl_list);
			if (ret)
				return ret;

			break;
		case MEI_FOP_READ:
			/* send flow control message */
			ret = _mei_irq_thread_read(dev, slots, pos, cl, cmpl_list);
			if (ret)
				return ret;

			break;
		case MEI_FOP_IOCTL:
			/* connect message */
			if (mei_other_client_is_connecting(dev, cl))
				continue;
			ret = _mei_irq_thread_ioctl(dev, slots, pos, cl, cmpl_list);
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

		if (cl != &dev->iamthif_cl) {
			if (mei_flow_ctrl_creds(dev, cl) <= 0) {
				dev_dbg(&dev->pdev->dev,
					"No flow control credentials for client %d, not sending.\n",
					cl->host_client_id);
				continue;
			}
			ret = _mei_irq_thread_cmpl(dev, slots, pos,
						cl, cmpl_list);
			if (ret)
				return ret;

		} else if (cl == &dev->iamthif_cl) {
			/* IAMTHIF IOCTL */
			dev_dbg(&dev->pdev->dev, "complete amthi write cb.\n");
			if (mei_flow_ctrl_creds(dev, cl) <= 0) {
				dev_dbg(&dev->pdev->dev,
					"No flow control credentials for amthi client %d.\n",
					cl->host_client_id);
				continue;
			}
			ret = mei_amthif_irq_process_completed(dev, slots, pos,
								cl, cmpl_list);
			if (ret)
				return ret;

		}

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
	dev->host_hw_state = mei_hcsr_read(dev);

	/* Ack the interrupt here
	 * In case of MSI we don't go through the quick handler */
	if (pci_dev_msi_enabled(dev->pdev))
		mei_reg_write(dev, H_CSR, dev->host_hw_state);

	dev->me_hw_state = mei_mecsr_read(dev);

	/* check if ME wants a reset */
	if ((dev->me_hw_state & ME_RDY_HRA) == 0 &&
	    dev->dev_state != MEI_DEV_RESETING &&
	    dev->dev_state != MEI_DEV_INITIALIZING) {
		dev_dbg(&dev->pdev->dev, "FW not ready.\n");
		mei_reset(dev, 1);
		mutex_unlock(&dev->device_lock);
		return IRQ_HANDLED;
	}

	/*  check if we need to start the dev */
	if ((dev->host_hw_state & H_RDY) == 0) {
		if ((dev->me_hw_state & ME_RDY_HRA) == ME_RDY_HRA) {
			dev_dbg(&dev->pdev->dev, "we need to start the dev.\n");
			dev->host_hw_state |= (H_IE | H_IG | H_RDY);
			mei_hcsr_set(dev);
			dev->dev_state = MEI_DEV_INIT_CLIENTS;
			dev_dbg(&dev->pdev->dev, "link is established start sending messages.\n");
			/* link is established
			 * start sending messages.
			 */
			mei_host_start_message(dev);
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
	dev_dbg(&dev->pdev->dev, "slots =%08x  extra_write_index =%08x.\n",
		slots, dev->extra_write_index);
	while (slots > 0 && !dev->extra_write_index) {
		dev_dbg(&dev->pdev->dev, "slots =%08x  extra_write_index =%08x.\n",
				slots, dev->extra_write_index);
		dev_dbg(&dev->pdev->dev, "call mei_irq_thread_read_handler.\n");
		rets = mei_irq_thread_read_handler(&complete_list, dev, &slots);
		if (rets)
			goto end;
	}
	rets = mei_irq_thread_write_handler(&complete_list, dev, &slots);
end:
	dev_dbg(&dev->pdev->dev, "end of bottom half function.\n");
	dev->host_hw_state = mei_hcsr_read(dev);
	dev->mei_host_buffer_is_empty = mei_hbuf_is_empty(dev);

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
