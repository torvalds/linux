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
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/mei.h>
#include <linux/pm_runtime.h>

#include "mei_dev.h"
#include "hbm.h"
#include "client.h"

static const char *mei_cl_conn_status_str(enum mei_cl_connect_status status)
{
#define MEI_CL_CS(status) case MEI_CL_CONN_##status: return #status
	switch (status) {
	MEI_CL_CS(SUCCESS);
	MEI_CL_CS(NOT_FOUND);
	MEI_CL_CS(ALREADY_STARTED);
	MEI_CL_CS(OUT_OF_RESOURCES);
	MEI_CL_CS(MESSAGE_SMALL);
	default: return "unknown";
	}
#undef MEI_CL_CCS
}

/**
 * mei_cl_conn_status_to_errno - convert client connect response
 * status to error code
 *
 * @status: client connect response status
 *
 * returns corresponding error code
 */
static int mei_cl_conn_status_to_errno(enum mei_cl_connect_status status)
{
	switch (status) {
	case MEI_CL_CONN_SUCCESS:          return 0;
	case MEI_CL_CONN_NOT_FOUND:        return -ENOTTY;
	case MEI_CL_CONN_ALREADY_STARTED:  return -EBUSY;
	case MEI_CL_CONN_OUT_OF_RESOURCES: return -EBUSY;
	case MEI_CL_CONN_MESSAGE_SMALL:    return -EINVAL;
	default:                           return -EINVAL;
	}
}

/**
 * mei_hbm_me_cl_allocate - allocates storage for me clients
 *
 * @dev: the device structure
 *
 * returns 0 on success -ENOMEM on allocation failure
 */
static int mei_hbm_me_cl_allocate(struct mei_device *dev)
{
	struct mei_me_client *clients;
	int b;

	dev->me_clients_num = 0;
	dev->me_client_presentation_num = 0;
	dev->me_client_index = 0;

	/* count how many ME clients we have */
	for_each_set_bit(b, dev->me_clients_map, MEI_CLIENTS_MAX)
		dev->me_clients_num++;

	if (dev->me_clients_num == 0)
		return 0;

	kfree(dev->me_clients);
	dev->me_clients = NULL;

	dev_dbg(&dev->pdev->dev, "memory allocation for ME clients size=%ld.\n",
		dev->me_clients_num * sizeof(struct mei_me_client));
	/* allocate storage for ME clients representation */
	clients = kcalloc(dev->me_clients_num,
			sizeof(struct mei_me_client), GFP_KERNEL);
	if (!clients) {
		dev_err(&dev->pdev->dev, "memory allocation for ME clients failed.\n");
		return -ENOMEM;
	}
	dev->me_clients = clients;
	return 0;
}

/**
 * mei_hbm_cl_hdr - construct client hbm header
 *
 * @cl: - client
 * @hbm_cmd: host bus message command
 * @buf: buffer for cl header
 * @len: buffer length
 */
static inline
void mei_hbm_cl_hdr(struct mei_cl *cl, u8 hbm_cmd, void *buf, size_t len)
{
	struct mei_hbm_cl_cmd *cmd = buf;

	memset(cmd, 0, len);

	cmd->hbm_cmd = hbm_cmd;
	cmd->host_addr = cl->host_client_id;
	cmd->me_addr = cl->me_client_id;
}

/**
 * mei_hbm_cl_addr_equal - tells if they have the same address
 *
 * @cl: - client
 * @buf: buffer with cl header
 *
 * returns true if addresses are the same
 */
static inline
bool mei_hbm_cl_addr_equal(struct mei_cl *cl, void *buf)
{
	struct mei_hbm_cl_cmd *cmd = buf;
	return cl->host_client_id == cmd->host_addr &&
		cl->me_client_id == cmd->me_addr;
}


/**
 * mei_hbm_idle - set hbm to idle state
 *
 * @dev: the device structure
 */
void mei_hbm_idle(struct mei_device *dev)
{
	dev->init_clients_timer = 0;
	dev->hbm_state = MEI_HBM_IDLE;
}

int mei_hbm_start_wait(struct mei_device *dev)
{
	int ret;
	if (dev->hbm_state > MEI_HBM_START)
		return 0;

	mutex_unlock(&dev->device_lock);
	ret = wait_event_interruptible_timeout(dev->wait_recvd_msg,
			dev->hbm_state == MEI_HBM_IDLE ||
			dev->hbm_state >= MEI_HBM_STARTED,
			mei_secs_to_jiffies(MEI_HBM_TIMEOUT));
	mutex_lock(&dev->device_lock);

	if (ret <= 0 && (dev->hbm_state <= MEI_HBM_START)) {
		dev->hbm_state = MEI_HBM_IDLE;
		dev_err(&dev->pdev->dev, "waiting for mei start failed\n");
		return -ETIME;
	}
	return 0;
}

/**
 * mei_hbm_start_req - sends start request message.
 *
 * @dev: the device structure
 *
 * returns 0 on success and < 0 on failure
 */
int mei_hbm_start_req(struct mei_device *dev)
{
	struct mei_msg_hdr *mei_hdr = &dev->wr_msg.hdr;
	struct hbm_host_version_request *start_req;
	const size_t len = sizeof(struct hbm_host_version_request);
	int ret;

	mei_hbm_hdr(mei_hdr, len);

	/* host start message */
	start_req = (struct hbm_host_version_request *)dev->wr_msg.data;
	memset(start_req, 0, len);
	start_req->hbm_cmd = HOST_START_REQ_CMD;
	start_req->host_version.major_version = HBM_MAJOR_VERSION;
	start_req->host_version.minor_version = HBM_MINOR_VERSION;

	dev->hbm_state = MEI_HBM_IDLE;
	ret = mei_write_message(dev, mei_hdr, dev->wr_msg.data);
	if (ret) {
		dev_err(&dev->pdev->dev, "version message write failed: ret = %d\n",
			ret);
		return ret;
	}

	dev->hbm_state = MEI_HBM_START;
	dev->init_clients_timer = MEI_CLIENTS_INIT_TIMEOUT;
	return 0;
}

/*
 * mei_hbm_enum_clients_req - sends enumeration client request message.
 *
 * @dev: the device structure
 *
 * returns 0 on success and < 0 on failure
 */
static int mei_hbm_enum_clients_req(struct mei_device *dev)
{
	struct mei_msg_hdr *mei_hdr = &dev->wr_msg.hdr;
	struct hbm_host_enum_request *enum_req;
	const size_t len = sizeof(struct hbm_host_enum_request);
	int ret;

	/* enumerate clients */
	mei_hbm_hdr(mei_hdr, len);

	enum_req = (struct hbm_host_enum_request *)dev->wr_msg.data;
	memset(enum_req, 0, len);
	enum_req->hbm_cmd = HOST_ENUM_REQ_CMD;

	ret = mei_write_message(dev, mei_hdr, dev->wr_msg.data);
	if (ret) {
		dev_err(&dev->pdev->dev, "enumeration request write failed: ret = %d.\n",
			ret);
		return ret;
	}
	dev->hbm_state = MEI_HBM_ENUM_CLIENTS;
	dev->init_clients_timer = MEI_CLIENTS_INIT_TIMEOUT;
	return 0;
}

/**
 * mei_hbm_prop_req - request property for a single client
 *
 * @dev: the device structure
 *
 * returns 0 on success and < 0 on failure
 */

static int mei_hbm_prop_req(struct mei_device *dev)
{

	struct mei_msg_hdr *mei_hdr = &dev->wr_msg.hdr;
	struct hbm_props_request *prop_req;
	const size_t len = sizeof(struct hbm_props_request);
	unsigned long next_client_index;
	unsigned long client_num;
	int ret;

	client_num = dev->me_client_presentation_num;

	next_client_index = find_next_bit(dev->me_clients_map, MEI_CLIENTS_MAX,
					  dev->me_client_index);

	/* We got all client properties */
	if (next_client_index == MEI_CLIENTS_MAX) {
		dev->hbm_state = MEI_HBM_STARTED;
		schedule_work(&dev->init_work);

		return 0;
	}

	dev->me_clients[client_num].client_id = next_client_index;
	dev->me_clients[client_num].mei_flow_ctrl_creds = 0;

	mei_hbm_hdr(mei_hdr, len);
	prop_req = (struct hbm_props_request *)dev->wr_msg.data;

	memset(prop_req, 0, sizeof(struct hbm_props_request));


	prop_req->hbm_cmd = HOST_CLIENT_PROPERTIES_REQ_CMD;
	prop_req->address = next_client_index;

	ret = mei_write_message(dev, mei_hdr, dev->wr_msg.data);
	if (ret) {
		dev_err(&dev->pdev->dev, "properties request write failed: ret = %d\n",
			ret);
		return ret;
	}

	dev->init_clients_timer = MEI_CLIENTS_INIT_TIMEOUT;
	dev->me_client_index = next_client_index;

	return 0;
}

/*
 * mei_hbm_pg - sends pg command
 *
 * @dev: the device structure
 * @pg_cmd: the pg command code
 *
 * This function returns -EIO on write failure
 */
int mei_hbm_pg(struct mei_device *dev, u8 pg_cmd)
{
	struct mei_msg_hdr *mei_hdr = &dev->wr_msg.hdr;
	struct hbm_power_gate *req;
	const size_t len = sizeof(struct hbm_power_gate);
	int ret;

	mei_hbm_hdr(mei_hdr, len);

	req = (struct hbm_power_gate *)dev->wr_msg.data;
	memset(req, 0, len);
	req->hbm_cmd = pg_cmd;

	ret = mei_write_message(dev, mei_hdr, dev->wr_msg.data);
	if (ret)
		dev_err(&dev->pdev->dev, "power gate command write failed.\n");
	return ret;
}
EXPORT_SYMBOL_GPL(mei_hbm_pg);

/**
 * mei_hbm_stop_req - send stop request message
 *
 * @dev - mei device
 * @cl: client info
 *
 * This function returns -EIO on write failure
 */
static int mei_hbm_stop_req(struct mei_device *dev)
{
	struct mei_msg_hdr *mei_hdr = &dev->wr_msg.hdr;
	struct hbm_host_stop_request *req =
			(struct hbm_host_stop_request *)dev->wr_msg.data;
	const size_t len = sizeof(struct hbm_host_stop_request);

	mei_hbm_hdr(mei_hdr, len);

	memset(req, 0, len);
	req->hbm_cmd = HOST_STOP_REQ_CMD;
	req->reason = DRIVER_STOP_REQUEST;

	return mei_write_message(dev, mei_hdr, dev->wr_msg.data);
}

/**
 * mei_hbm_cl_flow_control_req - sends flow control request.
 *
 * @dev: the device structure
 * @cl: client info
 *
 * This function returns -EIO on write failure
 */
int mei_hbm_cl_flow_control_req(struct mei_device *dev, struct mei_cl *cl)
{
	struct mei_msg_hdr *mei_hdr = &dev->wr_msg.hdr;
	const size_t len = sizeof(struct hbm_flow_control);

	mei_hbm_hdr(mei_hdr, len);
	mei_hbm_cl_hdr(cl, MEI_FLOW_CONTROL_CMD, dev->wr_msg.data, len);

	cl_dbg(dev, cl, "sending flow control\n");

	return mei_write_message(dev, mei_hdr, dev->wr_msg.data);
}

/**
 * mei_hbm_add_single_flow_creds - adds single buffer credentials.
 *
 * @dev: the device structure
 * @flow: flow control.
 *
 * return 0 on success, < 0 otherwise
 */
static int mei_hbm_add_single_flow_creds(struct mei_device *dev,
				  struct hbm_flow_control *flow)
{
	struct mei_me_client *me_cl;
	int id;

	id = mei_me_cl_by_id(dev, flow->me_addr);
	if (id < 0) {
		dev_err(&dev->pdev->dev, "no such me client %d\n",
			flow->me_addr);
		return id;
	}

	me_cl = &dev->me_clients[id];
	if (me_cl->props.single_recv_buf) {
		me_cl->mei_flow_ctrl_creds++;
		dev_dbg(&dev->pdev->dev, "recv flow ctrl msg ME %d (single).\n",
		    flow->me_addr);
		dev_dbg(&dev->pdev->dev, "flow control credentials =%d.\n",
		    me_cl->mei_flow_ctrl_creds);
	} else {
		BUG();	/* error in flow control */
	}

	return 0;
}

/**
 * mei_hbm_cl_flow_control_res - flow control response from me
 *
 * @dev: the device structure
 * @flow_control: flow control response bus message
 */
static void mei_hbm_cl_flow_control_res(struct mei_device *dev,
		struct hbm_flow_control *flow_control)
{
	struct mei_cl *cl;

	if (!flow_control->host_addr) {
		/* single receive buffer */
		mei_hbm_add_single_flow_creds(dev, flow_control);
		return;
	}

	/* normal connection */
	list_for_each_entry(cl, &dev->file_list, link) {
		if (mei_hbm_cl_addr_equal(cl, flow_control)) {
			cl->mei_flow_ctrl_creds++;
			dev_dbg(&dev->pdev->dev, "flow ctrl msg for host %d ME %d.\n",
				flow_control->host_addr, flow_control->me_addr);
			dev_dbg(&dev->pdev->dev, "flow control credentials = %d.\n",
				    cl->mei_flow_ctrl_creds);
				break;
		}
	}
}


/**
 * mei_hbm_cl_disconnect_req - sends disconnect message to fw.
 *
 * @dev: the device structure
 * @cl: a client to disconnect from
 *
 * This function returns -EIO on write failure
 */
int mei_hbm_cl_disconnect_req(struct mei_device *dev, struct mei_cl *cl)
{
	struct mei_msg_hdr *mei_hdr = &dev->wr_msg.hdr;
	const size_t len = sizeof(struct hbm_client_connect_request);

	mei_hbm_hdr(mei_hdr, len);
	mei_hbm_cl_hdr(cl, CLIENT_DISCONNECT_REQ_CMD, dev->wr_msg.data, len);

	return mei_write_message(dev, mei_hdr, dev->wr_msg.data);
}

/**
 * mei_hbm_cl_disconnect_rsp - sends disconnect respose to the FW
 *
 * @dev: the device structure
 * @cl: a client to disconnect from
 *
 * This function returns -EIO on write failure
 */
int mei_hbm_cl_disconnect_rsp(struct mei_device *dev, struct mei_cl *cl)
{
	struct mei_msg_hdr *mei_hdr = &dev->wr_msg.hdr;
	const size_t len = sizeof(struct hbm_client_connect_response);

	mei_hbm_hdr(mei_hdr, len);
	mei_hbm_cl_hdr(cl, CLIENT_DISCONNECT_RES_CMD, dev->wr_msg.data, len);

	return mei_write_message(dev, mei_hdr, dev->wr_msg.data);
}

/**
 * mei_hbm_cl_disconnect_res - disconnect response from ME
 *
 * @dev: the device structure
 * @rs: disconnect response bus message
 */
static void mei_hbm_cl_disconnect_res(struct mei_device *dev,
		struct hbm_client_connect_response *rs)
{
	struct mei_cl *cl;
	struct mei_cl_cb *cb, *next;

	dev_dbg(&dev->pdev->dev, "hbm: disconnect response cl:host=%02d me=%02d status=%d\n",
			rs->me_addr, rs->host_addr, rs->status);

	list_for_each_entry_safe(cb, next, &dev->ctrl_rd_list.list, list) {
		cl = cb->cl;

		/* this should not happen */
		if (WARN_ON(!cl)) {
			list_del(&cb->list);
			return;
		}

		if (mei_hbm_cl_addr_equal(cl, rs)) {
			list_del(&cb->list);
			if (rs->status == MEI_CL_DISCONN_SUCCESS)
				cl->state = MEI_FILE_DISCONNECTED;

			cl->status = 0;
			cl->timer_count = 0;
			break;
		}
	}
}

/**
 * mei_hbm_cl_connect_req - send connection request to specific me client
 *
 * @dev: the device structure
 * @cl: a client to connect to
 *
 * returns -EIO on write failure
 */
int mei_hbm_cl_connect_req(struct mei_device *dev, struct mei_cl *cl)
{
	struct mei_msg_hdr *mei_hdr = &dev->wr_msg.hdr;
	const size_t len = sizeof(struct hbm_client_connect_request);

	mei_hbm_hdr(mei_hdr, len);
	mei_hbm_cl_hdr(cl, CLIENT_CONNECT_REQ_CMD, dev->wr_msg.data, len);

	return mei_write_message(dev, mei_hdr,  dev->wr_msg.data);
}

/**
 * mei_hbm_cl_connect_res - connect response from the ME
 *
 * @dev: the device structure
 * @rs: connect response bus message
 */
static void mei_hbm_cl_connect_res(struct mei_device *dev,
		struct hbm_client_connect_response *rs)
{

	struct mei_cl *cl;
	struct mei_cl_cb *cb, *next;

	dev_dbg(&dev->pdev->dev, "hbm: connect response cl:host=%02d me=%02d status=%s\n",
			rs->me_addr, rs->host_addr,
			mei_cl_conn_status_str(rs->status));

	cl = NULL;

	list_for_each_entry_safe(cb, next, &dev->ctrl_rd_list.list, list) {

		cl = cb->cl;
		/* this should not happen */
		if (WARN_ON(!cl)) {
			list_del_init(&cb->list);
			continue;
		}

		if (cb->fop_type !=  MEI_FOP_CONNECT)
			continue;

		if (mei_hbm_cl_addr_equal(cl, rs)) {
			list_del(&cb->list);
			break;
		}
	}

	if (!cl)
		return;

	cl->timer_count = 0;
	if (rs->status == MEI_CL_CONN_SUCCESS)
		cl->state = MEI_FILE_CONNECTED;
	else
		cl->state = MEI_FILE_DISCONNECTED;
	cl->status = mei_cl_conn_status_to_errno(rs->status);
}


/**
 * mei_hbm_fw_disconnect_req - disconnect request initiated by ME firmware
 *  host sends disconnect response
 *
 * @dev: the device structure.
 * @disconnect_req: disconnect request bus message from the me
 *
 * returns -ENOMEM on allocation failure
 */
static int mei_hbm_fw_disconnect_req(struct mei_device *dev,
		struct hbm_client_connect_request *disconnect_req)
{
	struct mei_cl *cl;
	struct mei_cl_cb *cb;

	list_for_each_entry(cl, &dev->file_list, link) {
		if (mei_hbm_cl_addr_equal(cl, disconnect_req)) {
			dev_dbg(&dev->pdev->dev, "disconnect request host client %d ME client %d.\n",
					disconnect_req->host_addr,
					disconnect_req->me_addr);
			cl->state = MEI_FILE_DISCONNECTED;
			cl->timer_count = 0;

			cb = mei_io_cb_init(cl, NULL);
			if (!cb)
				return -ENOMEM;
			cb->fop_type = MEI_FOP_DISCONNECT_RSP;
			cl_dbg(dev, cl, "add disconnect response as first\n");
			list_add(&cb->list, &dev->ctrl_wr_list.list);

			break;
		}
	}
	return 0;
}


/**
 * mei_hbm_version_is_supported - checks whether the driver can
 *     support the hbm version of the device
 *
 * @dev: the device structure
 * returns true if driver can support hbm version of the device
 */
bool mei_hbm_version_is_supported(struct mei_device *dev)
{
	return	(dev->version.major_version < HBM_MAJOR_VERSION) ||
		(dev->version.major_version == HBM_MAJOR_VERSION &&
		 dev->version.minor_version <= HBM_MINOR_VERSION);
}

/**
 * mei_hbm_dispatch - bottom half read routine after ISR to
 * handle the read bus message cmd processing.
 *
 * @dev: the device structure
 * @mei_hdr: header of bus message
 *
 * returns 0 on success and < 0 on failure
 */
int mei_hbm_dispatch(struct mei_device *dev, struct mei_msg_hdr *hdr)
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

	/* read the message to our buffer */
	BUG_ON(hdr->length >= sizeof(dev->rd_msg_buf));
	mei_read_slots(dev, dev->rd_msg_buf, hdr->length);
	mei_msg = (struct mei_bus_message *)dev->rd_msg_buf;

	/* ignore spurious message and prevent reset nesting
	 * hbm is put to idle during system reset
	 */
	if (dev->hbm_state == MEI_HBM_IDLE) {
		dev_dbg(&dev->pdev->dev, "hbm: state is idle ignore spurious messages\n");
		return 0;
	}

	switch (mei_msg->hbm_cmd) {
	case HOST_START_RES_CMD:
		dev_dbg(&dev->pdev->dev, "hbm: start: response message received.\n");

		dev->init_clients_timer = 0;

		version_res = (struct hbm_host_version_response *)mei_msg;

		dev_dbg(&dev->pdev->dev, "HBM VERSION: DRIVER=%02d:%02d DEVICE=%02d:%02d\n",
				HBM_MAJOR_VERSION, HBM_MINOR_VERSION,
				version_res->me_max_version.major_version,
				version_res->me_max_version.minor_version);

		if (version_res->host_version_supported) {
			dev->version.major_version = HBM_MAJOR_VERSION;
			dev->version.minor_version = HBM_MINOR_VERSION;
		} else {
			dev->version.major_version =
				version_res->me_max_version.major_version;
			dev->version.minor_version =
				version_res->me_max_version.minor_version;
		}

		if (!mei_hbm_version_is_supported(dev)) {
			dev_warn(&dev->pdev->dev, "hbm: start: version mismatch - stopping the driver.\n");

			dev->hbm_state = MEI_HBM_STOPPED;
			if (mei_hbm_stop_req(dev)) {
				dev_err(&dev->pdev->dev, "hbm: start: failed to send stop request\n");
				return -EIO;
			}
			break;
		}

		if (dev->dev_state != MEI_DEV_INIT_CLIENTS ||
		    dev->hbm_state != MEI_HBM_START) {
			dev_err(&dev->pdev->dev, "hbm: start: state mismatch, [%d, %d]\n",
				dev->dev_state, dev->hbm_state);
			return -EPROTO;
		}

		dev->hbm_state = MEI_HBM_STARTED;

		if (mei_hbm_enum_clients_req(dev)) {
			dev_err(&dev->pdev->dev, "hbm: start: failed to send enumeration request\n");
			return -EIO;
		}

		wake_up_interruptible(&dev->wait_recvd_msg);
		break;

	case CLIENT_CONNECT_RES_CMD:
		dev_dbg(&dev->pdev->dev, "hbm: client connect response: message received.\n");

		connect_res = (struct hbm_client_connect_response *) mei_msg;
		mei_hbm_cl_connect_res(dev, connect_res);
		wake_up(&dev->wait_recvd_msg);
		break;

	case CLIENT_DISCONNECT_RES_CMD:
		dev_dbg(&dev->pdev->dev, "hbm: client disconnect response: message received.\n");

		disconnect_res = (struct hbm_client_connect_response *) mei_msg;
		mei_hbm_cl_disconnect_res(dev, disconnect_res);
		wake_up(&dev->wait_recvd_msg);
		break;

	case MEI_FLOW_CONTROL_CMD:
		dev_dbg(&dev->pdev->dev, "hbm: client flow control response: message received.\n");

		flow_control = (struct hbm_flow_control *) mei_msg;
		mei_hbm_cl_flow_control_res(dev, flow_control);
		break;

	case MEI_PG_ISOLATION_ENTRY_RES_CMD:
		dev_dbg(&dev->pdev->dev, "power gate isolation entry response received\n");
		dev->pg_event = MEI_PG_EVENT_RECEIVED;
		if (waitqueue_active(&dev->wait_pg))
			wake_up(&dev->wait_pg);
		break;

	case MEI_PG_ISOLATION_EXIT_REQ_CMD:
		dev_dbg(&dev->pdev->dev, "power gate isolation exit request received\n");
		dev->pg_event = MEI_PG_EVENT_RECEIVED;
		if (waitqueue_active(&dev->wait_pg))
			wake_up(&dev->wait_pg);
		else
			/*
			* If the driver is not waiting on this then
			* this is HW initiated exit from PG.
			* Start runtime pm resume sequence to exit from PG.
			*/
			pm_request_resume(&dev->pdev->dev);
		break;

	case HOST_CLIENT_PROPERTIES_RES_CMD:
		dev_dbg(&dev->pdev->dev, "hbm: properties response: message received.\n");

		dev->init_clients_timer = 0;

		if (dev->me_clients == NULL) {
			dev_err(&dev->pdev->dev, "hbm: properties response: mei_clients not allocated\n");
			return -EPROTO;
		}

		props_res = (struct hbm_props_response *)mei_msg;
		me_client = &dev->me_clients[dev->me_client_presentation_num];

		if (props_res->status) {
			dev_err(&dev->pdev->dev, "hbm: properties response: wrong status = %d\n",
				props_res->status);
			return -EPROTO;
		}

		if (me_client->client_id != props_res->address) {
			dev_err(&dev->pdev->dev, "hbm: properties response: address mismatch %d ?= %d\n",
				me_client->client_id, props_res->address);
			return -EPROTO;
		}

		if (dev->dev_state != MEI_DEV_INIT_CLIENTS ||
		    dev->hbm_state != MEI_HBM_CLIENT_PROPERTIES) {
			dev_err(&dev->pdev->dev, "hbm: properties response: state mismatch, [%d, %d]\n",
				dev->dev_state, dev->hbm_state);
			return -EPROTO;
		}

		me_client->props = props_res->client_properties;
		dev->me_client_index++;
		dev->me_client_presentation_num++;

		/* request property for the next client */
		if (mei_hbm_prop_req(dev))
			return -EIO;

		break;

	case HOST_ENUM_RES_CMD:
		dev_dbg(&dev->pdev->dev, "hbm: enumeration response: message received\n");

		dev->init_clients_timer = 0;

		enum_res = (struct hbm_host_enum_response *) mei_msg;
		BUILD_BUG_ON(sizeof(dev->me_clients_map)
				< sizeof(enum_res->valid_addresses));
		memcpy(dev->me_clients_map, enum_res->valid_addresses,
			sizeof(enum_res->valid_addresses));

		if (dev->dev_state != MEI_DEV_INIT_CLIENTS ||
		    dev->hbm_state != MEI_HBM_ENUM_CLIENTS) {
			dev_err(&dev->pdev->dev, "hbm: enumeration response: state mismatch, [%d, %d]\n",
				dev->dev_state, dev->hbm_state);
			return -EPROTO;
		}

		if (mei_hbm_me_cl_allocate(dev)) {
			dev_err(&dev->pdev->dev, "hbm: enumeration response: cannot allocate clients array\n");
			return -ENOMEM;
		}

		dev->hbm_state = MEI_HBM_CLIENT_PROPERTIES;

		/* first property request */
		if (mei_hbm_prop_req(dev))
			return -EIO;

		break;

	case HOST_STOP_RES_CMD:
		dev_dbg(&dev->pdev->dev, "hbm: stop response: message received\n");

		dev->init_clients_timer = 0;

		if (dev->hbm_state != MEI_HBM_STOPPED) {
			dev_err(&dev->pdev->dev, "hbm: stop response: state mismatch, [%d, %d]\n",
				dev->dev_state, dev->hbm_state);
			return -EPROTO;
		}

		dev->dev_state = MEI_DEV_POWER_DOWN;
		dev_info(&dev->pdev->dev, "hbm: stop response: resetting.\n");
		/* force the reset */
		return -EPROTO;
		break;

	case CLIENT_DISCONNECT_REQ_CMD:
		dev_dbg(&dev->pdev->dev, "hbm: disconnect request: message received\n");

		disconnect_req = (struct hbm_client_connect_request *)mei_msg;
		mei_hbm_fw_disconnect_req(dev, disconnect_req);
		break;

	case ME_STOP_REQ_CMD:
		dev_dbg(&dev->pdev->dev, "hbm: stop request: message received\n");
		dev->hbm_state = MEI_HBM_STOPPED;
		if (mei_hbm_stop_req(dev)) {
			dev_err(&dev->pdev->dev, "hbm: start: failed to send stop request\n");
			return -EIO;
		}
		break;
	default:
		BUG();
		break;

	}
	return 0;
}

