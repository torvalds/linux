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
#include <linux/mei.h>

#include "mei_dev.h"
#include "interface.h"

/**
 * host_start_message - mei host sends start message.
 *
 * @dev: the device structure
 *
 * returns none.
 */
void mei_host_start_message(struct mei_device *dev)
{
	struct mei_msg_hdr *mei_hdr;
	struct hbm_host_version_request *start_req;
	const size_t len = sizeof(struct hbm_host_version_request);

	mei_hdr = mei_hbm_hdr(&dev->wr_msg_buf[0], len);

	/* host start message */
	start_req = (struct hbm_host_version_request *)&dev->wr_msg_buf[1];
	memset(start_req, 0, len);
	start_req->hbm_cmd = HOST_START_REQ_CMD;
	start_req->host_version.major_version = HBM_MAJOR_VERSION;
	start_req->host_version.minor_version = HBM_MINOR_VERSION;

	dev->recvd_msg = false;
	if (mei_write_message(dev, mei_hdr, (unsigned char *)start_req)) {
		dev_dbg(&dev->pdev->dev, "write send version message to FW fail.\n");
		dev->dev_state = MEI_DEV_RESETING;
		mei_reset(dev, 1);
	}
	dev->init_clients_state = MEI_START_MESSAGE;
	dev->init_clients_timer = MEI_CLIENTS_INIT_TIMEOUT;
	return ;
}

/**
 * host_enum_clients_message - host sends enumeration client request message.
 *
 * @dev: the device structure
 *
 * returns none.
 */
void mei_host_enum_clients_message(struct mei_device *dev)
{
	struct mei_msg_hdr *mei_hdr;
	struct hbm_host_enum_request *enum_req;
	const size_t len = sizeof(struct hbm_host_enum_request);
	/* enumerate clients */
	mei_hdr = mei_hbm_hdr(&dev->wr_msg_buf[0], len);

	enum_req = (struct hbm_host_enum_request *) &dev->wr_msg_buf[1];
	memset(enum_req, 0, sizeof(struct hbm_host_enum_request));
	enum_req->hbm_cmd = HOST_ENUM_REQ_CMD;

	if (mei_write_message(dev, mei_hdr, (unsigned char *)enum_req)) {
		dev->dev_state = MEI_DEV_RESETING;
		dev_dbg(&dev->pdev->dev, "write send enumeration request message to FW fail.\n");
		mei_reset(dev, 1);
	}
	dev->init_clients_state = MEI_ENUM_CLIENTS_MESSAGE;
	dev->init_clients_timer = MEI_CLIENTS_INIT_TIMEOUT;
	return;
}


int mei_host_client_enumerate(struct mei_device *dev)
{

	struct mei_msg_hdr *mei_hdr;
	struct hbm_props_request *prop_req;
	const size_t len = sizeof(struct hbm_props_request);
	unsigned long next_client_index;
	u8 client_num;


	client_num = dev->me_client_presentation_num;

	next_client_index = find_next_bit(dev->me_clients_map, MEI_CLIENTS_MAX,
					  dev->me_client_index);

	/* We got all client properties */
	if (next_client_index == MEI_CLIENTS_MAX) {
		schedule_work(&dev->init_work);

		return 0;
	}

	dev->me_clients[client_num].client_id = next_client_index;
	dev->me_clients[client_num].mei_flow_ctrl_creds = 0;

	mei_hdr = mei_hbm_hdr(&dev->wr_msg_buf[0], len);
	prop_req = (struct hbm_props_request *)&dev->wr_msg_buf[1];

	memset(prop_req, 0, sizeof(struct hbm_props_request));


	prop_req->hbm_cmd = HOST_CLIENT_PROPERTIES_REQ_CMD;
	prop_req->address = next_client_index;

	if (mei_write_message(dev, mei_hdr, (unsigned char *) prop_req)) {
		dev->dev_state = MEI_DEV_RESETING;
		dev_err(&dev->pdev->dev, "Properties request command failed\n");
		mei_reset(dev, 1);

		return -EIO;
	}

	dev->init_clients_timer = MEI_CLIENTS_INIT_TIMEOUT;
	dev->me_client_index = next_client_index;

	return 0;
}

/**
 * mei_send_flow_control - sends flow control to fw.
 *
 * @dev: the device structure
 * @cl: private data of the file object
 *
 * This function returns -EIO on write failure
 */
int mei_send_flow_control(struct mei_device *dev, struct mei_cl *cl)
{
	struct mei_msg_hdr *mei_hdr;
	struct hbm_flow_control *flow_ctrl;
	const size_t len = sizeof(struct hbm_flow_control);

	mei_hdr = mei_hbm_hdr(&dev->wr_msg_buf[0], len);

	flow_ctrl = (struct hbm_flow_control *)&dev->wr_msg_buf[1];
	memset(flow_ctrl, 0, len);
	flow_ctrl->hbm_cmd = MEI_FLOW_CONTROL_CMD;
	flow_ctrl->host_addr = cl->host_client_id;
	flow_ctrl->me_addr = cl->me_client_id;
	/* FIXME: reserved !? */
	memset(flow_ctrl->reserved, 0, sizeof(flow_ctrl->reserved));
	dev_dbg(&dev->pdev->dev, "sending flow control host client = %d, ME client = %d\n",
		cl->host_client_id, cl->me_client_id);

	return mei_write_message(dev, mei_hdr, (unsigned char *) flow_ctrl);
}

/**
 * mei_disconnect - sends disconnect message to fw.
 *
 * @dev: the device structure
 * @cl: private data of the file object
 *
 * This function returns -EIO on write failure
 */
int mei_disconnect(struct mei_device *dev, struct mei_cl *cl)
{
	struct mei_msg_hdr *hdr;
	struct hbm_client_connect_request *req;
	const size_t len = sizeof(struct hbm_client_connect_request);

	hdr = mei_hbm_hdr(&dev->wr_msg_buf[0], len);

	req = (struct hbm_client_connect_request *)&dev->wr_msg_buf[1];
	memset(req, 0, len);
	req->hbm_cmd = CLIENT_DISCONNECT_REQ_CMD;
	req->host_addr = cl->host_client_id;
	req->me_addr = cl->me_client_id;
	req->reserved = 0;

	return mei_write_message(dev, hdr, (unsigned char *)req);
}

/**
 * mei_connect - sends connect message to fw.
 *
 * @dev: the device structure
 * @cl: private data of the file object
 *
 * This function returns -EIO on write failure
 */
int mei_connect(struct mei_device *dev, struct mei_cl *cl)
{
	struct mei_msg_hdr *hdr;
	struct hbm_client_connect_request *req;
	const size_t len = sizeof(struct hbm_client_connect_request);

	hdr = mei_hbm_hdr(&dev->wr_msg_buf[0], len);

	req = (struct hbm_client_connect_request *) &dev->wr_msg_buf[1];
	req->hbm_cmd = CLIENT_CONNECT_REQ_CMD;
	req->host_addr = cl->host_client_id;
	req->me_addr = cl->me_client_id;
	req->reserved = 0;

	return mei_write_message(dev, hdr, (unsigned char *) req);
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
	struct hbm_client_connect_response *disconnect_res;
	struct mei_cl *pos, *next;
	const size_t len = sizeof(struct hbm_client_connect_response);

	list_for_each_entry_safe(pos, next, &dev->file_list, link) {
		if (same_disconn_addr(pos, disconnect_req)) {
			dev_dbg(&dev->pdev->dev, "disconnect request host client %d ME client %d.\n",
					disconnect_req->host_addr,
					disconnect_req->me_addr);
			pos->state = MEI_FILE_DISCONNECTED;
			pos->timer_count = 0;
			if (pos == &dev->wd_cl)
				dev->wd_pending = false;
			else if (pos == &dev->iamthif_cl)
				dev->iamthif_timer = 0;

			/* prepare disconnect response */
			(void)mei_hbm_hdr((u32 *)&dev->wr_ext_msg.hdr, len);
			disconnect_res =
				(struct hbm_client_connect_response *)
				&dev->wr_ext_msg.data;
			disconnect_res->hbm_cmd = CLIENT_DISCONNECT_RES_CMD;
			disconnect_res->host_addr = pos->host_client_id;
			disconnect_res->me_addr = pos->me_client_id;
			disconnect_res->status = 0;
			break;
		}
	}
}


/**
 * mei_hbm_dispatch - bottom half read routine after ISR to
 * handle the read bus message cmd processing.
 *
 * @dev: the device structure
 * @mei_hdr: header of bus message
 */
void mei_hbm_dispatch(struct mei_device *dev, struct mei_msg_hdr *hdr)
{
	struct mei_bus_message *mei_msg;
	struct mei_me_client *me_client;
	struct hbm_host_version_response *version_res;
	struct hbm_client_connect_response *connect_res;
	struct hbm_client_connect_response *disconnect_res;
	struct hbm_client_connect_request *disconnect_req;
	struct hbm_flow_control *flow_control;
	struct hbm_props_response *props_res;
	struct hbm_host_enum_response *enum_res;
	struct hbm_host_stop_request *stop_req;

	/* read the message to our buffer */
	BUG_ON(hdr->length >= sizeof(dev->rd_msg_buf));
	mei_read_slots(dev, dev->rd_msg_buf, hdr->length);
	mei_msg = (struct mei_bus_message *)dev->rd_msg_buf;

	switch (mei_msg->hbm_cmd) {
	case HOST_START_RES_CMD:
		version_res = (struct hbm_host_version_response *)mei_msg;
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
			u32 *buf = dev->wr_msg_buf;
			const size_t len = sizeof(struct hbm_host_stop_request);

			dev->version = version_res->me_max_version;

			/* send stop message */
			hdr = mei_hbm_hdr(&buf[0], len);
			stop_req = (struct hbm_host_stop_request *)&buf[1];
			memset(stop_req, 0, len);
			stop_req->hbm_cmd = HOST_STOP_REQ_CMD;
			stop_req->reason = DRIVER_STOP_REQUEST;

			mei_write_message(dev, hdr, (unsigned char *)stop_req);
			dev_dbg(&dev->pdev->dev, "version mismatch.\n");
			return;
		}

		dev->recvd_msg = true;
		dev_dbg(&dev->pdev->dev, "host start response message received.\n");
		break;

	case CLIENT_CONNECT_RES_CMD:
		connect_res = (struct hbm_client_connect_response *) mei_msg;
		mei_client_connect_response(dev, connect_res);
		dev_dbg(&dev->pdev->dev, "client connect response message received.\n");
		wake_up(&dev->wait_recvd_msg);
		break;

	case CLIENT_DISCONNECT_RES_CMD:
		disconnect_res = (struct hbm_client_connect_response *) mei_msg;
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
		me_client = &dev->me_clients[dev->me_client_presentation_num];

		if (props_res->status || !dev->me_clients) {
			dev_dbg(&dev->pdev->dev, "reset due to received host client properties response bus message wrong status.\n");
			mei_reset(dev, 1);
			return;
		}

		if (me_client->client_id != props_res->address) {
			dev_err(&dev->pdev->dev,
				"Host client properties reply mismatch\n");
			mei_reset(dev, 1);

			return;
		}

		if (dev->dev_state != MEI_DEV_INIT_CLIENTS ||
		    dev->init_clients_state != MEI_CLIENT_PROPERTIES_MESSAGE) {
			dev_err(&dev->pdev->dev,
				"Unexpected client properties reply\n");
			mei_reset(dev, 1);

			return;
		}

		me_client->props = props_res->client_properties;
		dev->me_client_index++;
		dev->me_client_presentation_num++;

		mei_host_client_enumerate(dev);

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

				mei_host_client_enumerate(dev);
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
	{
		/* prepare stop request: sent in next interrupt event */

		const size_t len = sizeof(struct hbm_host_stop_request);

		hdr = mei_hbm_hdr((u32 *)&dev->wr_ext_msg.hdr, len);
		stop_req = (struct hbm_host_stop_request *)&dev->wr_ext_msg.data;
		memset(stop_req, 0, len);
		stop_req->hbm_cmd = HOST_STOP_REQ_CMD;
		stop_req->reason = DRIVER_STOP_REQUEST;
		break;
	}
	default:
		BUG();
		break;

	}
}

