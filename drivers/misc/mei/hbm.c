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
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>

#include <linux/mei.h>

#include "mei_dev.h"
#include "hbm.h"
#include "client.h"

static const char *mei_hbm_status_str(enum mei_hbm_status status)
{
#define MEI_HBM_STATUS(status) case MEI_HBMS_##status: return #status
	switch (status) {
	MEI_HBM_STATUS(SUCCESS);
	MEI_HBM_STATUS(CLIENT_NOT_FOUND);
	MEI_HBM_STATUS(ALREADY_EXISTS);
	MEI_HBM_STATUS(REJECTED);
	MEI_HBM_STATUS(INVALID_PARAMETER);
	MEI_HBM_STATUS(NOT_ALLOWED);
	MEI_HBM_STATUS(ALREADY_STARTED);
	MEI_HBM_STATUS(NOT_STARTED);
	default: return "unknown";
	}
#undef MEI_HBM_STATUS
};

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

const char *mei_hbm_state_str(enum mei_hbm_state state)
{
#define MEI_HBM_STATE(state) case MEI_HBM_##state: return #state
	switch (state) {
	MEI_HBM_STATE(IDLE);
	MEI_HBM_STATE(STARTING);
	MEI_HBM_STATE(STARTED);
	MEI_HBM_STATE(ENUM_CLIENTS);
	MEI_HBM_STATE(CLIENT_PROPERTIES);
	MEI_HBM_STATE(STOPPED);
	default:
		return "unknown";
	}
#undef MEI_HBM_STATE
}

/**
 * mei_cl_conn_status_to_errno - convert client connect response
 * status to error code
 *
 * @status: client connect response status
 *
 * Return: corresponding error code
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
 * mei_hbm_idle - set hbm to idle state
 *
 * @dev: the device structure
 */
void mei_hbm_idle(struct mei_device *dev)
{
	dev->init_clients_timer = 0;
	dev->hbm_state = MEI_HBM_IDLE;
}

/**
 * mei_me_cl_remove_all - remove all me clients
 *
 * @dev: the device structure
 */
static void mei_me_cl_remove_all(struct mei_device *dev)
{
	struct mei_me_client *me_cl, *next;

	list_for_each_entry_safe(me_cl, next, &dev->me_clients, list) {
			list_del(&me_cl->list);
			kfree(me_cl);
	}
}

/**
 * mei_hbm_reset - reset hbm counters and book keeping data structurs
 *
 * @dev: the device structure
 */
void mei_hbm_reset(struct mei_device *dev)
{
	dev->me_client_index = 0;

	mei_me_cl_remove_all(dev);

	mei_hbm_idle(dev);
}

/**
 * mei_hbm_hdr - construct hbm header
 *
 * @hdr: hbm header
 * @length: payload length
 */

static inline void mei_hbm_hdr(struct mei_msg_hdr *hdr, size_t length)
{
	hdr->host_addr = 0;
	hdr->me_addr = 0;
	hdr->length = length;
	hdr->msg_complete = 1;
	hdr->reserved = 0;
}

/**
 * mei_hbm_cl_hdr - construct client hbm header
 *
 * @cl: client
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
 * mei_hbm_cl_write - write simple hbm client message
 *
 * @dev: the device structure
 * @cl: client
 * @hbm_cmd: host bus message command
 * @len: buffer length
 *
 * Return: 0 on success, <0 on failure.
 */
static inline
int mei_hbm_cl_write(struct mei_device *dev,
		     struct mei_cl *cl, u8 hbm_cmd, size_t len)
{
	struct mei_msg_hdr *mei_hdr = &dev->wr_msg.hdr;

	mei_hbm_hdr(mei_hdr, len);
	mei_hbm_cl_hdr(cl, hbm_cmd, dev->wr_msg.data, len);

	return mei_write_message(dev, mei_hdr, dev->wr_msg.data);
}

/**
 * mei_hbm_cl_addr_equal - check if the client's and
 *	the message address match
 *
 * @cl: client
 * @cmd: hbm client message
 *
 * Return: true if addresses are the same
 */
static inline
bool mei_hbm_cl_addr_equal(struct mei_cl *cl, struct mei_hbm_cl_cmd *cmd)
{
	return cl->host_client_id == cmd->host_addr &&
		cl->me_client_id == cmd->me_addr;
}

/**
 * mei_hbm_cl_find_by_cmd - find recipient client
 *
 * @dev: the device structure
 * @buf: a buffer with hbm cl command
 *
 * Return: the recipient client or NULL if not found
 */
static inline
struct mei_cl *mei_hbm_cl_find_by_cmd(struct mei_device *dev, void *buf)
{
	struct mei_hbm_cl_cmd *cmd = (struct mei_hbm_cl_cmd *)buf;
	struct mei_cl *cl;

	list_for_each_entry(cl, &dev->file_list, link)
		if (mei_hbm_cl_addr_equal(cl, cmd))
			return cl;
	return NULL;
}


/**
 * mei_hbm_start_wait - wait for start response message.
 *
 * @dev: the device structure
 *
 * Return: 0 on success and < 0 on failure
 */
int mei_hbm_start_wait(struct mei_device *dev)
{
	int ret;

	if (dev->hbm_state > MEI_HBM_STARTING)
		return 0;

	mutex_unlock(&dev->device_lock);
	ret = wait_event_timeout(dev->wait_hbm_start,
			dev->hbm_state != MEI_HBM_STARTING,
			mei_secs_to_jiffies(MEI_HBM_TIMEOUT));
	mutex_lock(&dev->device_lock);

	if (ret == 0 && (dev->hbm_state <= MEI_HBM_STARTING)) {
		dev->hbm_state = MEI_HBM_IDLE;
		dev_err(dev->dev, "waiting for mei start failed\n");
		return -ETIME;
	}
	return 0;
}

/**
 * mei_hbm_start_req - sends start request message.
 *
 * @dev: the device structure
 *
 * Return: 0 on success and < 0 on failure
 */
int mei_hbm_start_req(struct mei_device *dev)
{
	struct mei_msg_hdr *mei_hdr = &dev->wr_msg.hdr;
	struct hbm_host_version_request *start_req;
	const size_t len = sizeof(struct hbm_host_version_request);
	int ret;

	mei_hbm_reset(dev);

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
		dev_err(dev->dev, "version message write failed: ret = %d\n",
			ret);
		return ret;
	}

	dev->hbm_state = MEI_HBM_STARTING;
	dev->init_clients_timer = MEI_CLIENTS_INIT_TIMEOUT;
	return 0;
}

/*
 * mei_hbm_enum_clients_req - sends enumeration client request message.
 *
 * @dev: the device structure
 *
 * Return: 0 on success and < 0 on failure
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
		dev_err(dev->dev, "enumeration request write failed: ret = %d.\n",
			ret);
		return ret;
	}
	dev->hbm_state = MEI_HBM_ENUM_CLIENTS;
	dev->init_clients_timer = MEI_CLIENTS_INIT_TIMEOUT;
	return 0;
}

/*
 * mei_hbm_me_cl_add - add new me client to the list
 *
 * @dev: the device structure
 * @res: hbm property response
 *
 * Return: 0 on success and -ENOMEM on allocation failure
 */

static int mei_hbm_me_cl_add(struct mei_device *dev,
			     struct hbm_props_response *res)
{
	struct mei_me_client *me_cl;

	me_cl = kzalloc(sizeof(struct mei_me_client), GFP_KERNEL);
	if (!me_cl)
		return -ENOMEM;

	me_cl->props = res->client_properties;
	me_cl->client_id = res->me_addr;
	me_cl->mei_flow_ctrl_creds = 0;

	list_add(&me_cl->list, &dev->me_clients);
	return 0;
}

/**
 * mei_hbm_prop_req - request property for a single client
 *
 * @dev: the device structure
 *
 * Return: 0 on success and < 0 on failure
 */

static int mei_hbm_prop_req(struct mei_device *dev)
{

	struct mei_msg_hdr *mei_hdr = &dev->wr_msg.hdr;
	struct hbm_props_request *prop_req;
	const size_t len = sizeof(struct hbm_props_request);
	unsigned long next_client_index;
	int ret;

	next_client_index = find_next_bit(dev->me_clients_map, MEI_CLIENTS_MAX,
					  dev->me_client_index);

	/* We got all client properties */
	if (next_client_index == MEI_CLIENTS_MAX) {
		dev->hbm_state = MEI_HBM_STARTED;
		schedule_work(&dev->init_work);

		return 0;
	}

	mei_hbm_hdr(mei_hdr, len);
	prop_req = (struct hbm_props_request *)dev->wr_msg.data;

	memset(prop_req, 0, sizeof(struct hbm_props_request));

	prop_req->hbm_cmd = HOST_CLIENT_PROPERTIES_REQ_CMD;
	prop_req->me_addr = next_client_index;

	ret = mei_write_message(dev, mei_hdr, dev->wr_msg.data);
	if (ret) {
		dev_err(dev->dev, "properties request write failed: ret = %d\n",
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
 * Return: -EIO on write failure
 *         -EOPNOTSUPP if the operation is not supported by the protocol
 */
int mei_hbm_pg(struct mei_device *dev, u8 pg_cmd)
{
	struct mei_msg_hdr *mei_hdr = &dev->wr_msg.hdr;
	struct hbm_power_gate *req;
	const size_t len = sizeof(struct hbm_power_gate);
	int ret;

	if (!dev->hbm_f_pg_supported)
		return -EOPNOTSUPP;

	mei_hbm_hdr(mei_hdr, len);

	req = (struct hbm_power_gate *)dev->wr_msg.data;
	memset(req, 0, len);
	req->hbm_cmd = pg_cmd;

	ret = mei_write_message(dev, mei_hdr, dev->wr_msg.data);
	if (ret)
		dev_err(dev->dev, "power gate command write failed.\n");
	return ret;
}
EXPORT_SYMBOL_GPL(mei_hbm_pg);

/**
 * mei_hbm_stop_req - send stop request message
 *
 * @dev: mei device
 *
 * Return: -EIO on write failure
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
 * Return: -EIO on write failure
 */
int mei_hbm_cl_flow_control_req(struct mei_device *dev, struct mei_cl *cl)
{
	const size_t len = sizeof(struct hbm_flow_control);

	cl_dbg(dev, cl, "sending flow control\n");
	return mei_hbm_cl_write(dev, cl, MEI_FLOW_CONTROL_CMD, len);
}

/**
 * mei_hbm_add_single_flow_creds - adds single buffer credentials.
 *
 * @dev: the device structure
 * @flow: flow control.
 *
 * Return: 0 on success, < 0 otherwise
 */
static int mei_hbm_add_single_flow_creds(struct mei_device *dev,
				  struct hbm_flow_control *flow)
{
	struct mei_me_client *me_cl;

	me_cl = mei_me_cl_by_id(dev, flow->me_addr);
	if (!me_cl) {
		dev_err(dev->dev, "no such me client %d\n",
			flow->me_addr);
		return -ENOENT;
	}

	if (WARN_ON(me_cl->props.single_recv_buf == 0))
		return -EINVAL;

	me_cl->mei_flow_ctrl_creds++;
	dev_dbg(dev->dev, "recv flow ctrl msg ME %d (single) creds = %d.\n",
	    flow->me_addr, me_cl->mei_flow_ctrl_creds);

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

	cl = mei_hbm_cl_find_by_cmd(dev, flow_control);
	if (cl) {
		cl->mei_flow_ctrl_creds++;
		cl_dbg(dev, cl, "flow control creds = %d.\n",
				cl->mei_flow_ctrl_creds);
	}
}


/**
 * mei_hbm_cl_disconnect_req - sends disconnect message to fw.
 *
 * @dev: the device structure
 * @cl: a client to disconnect from
 *
 * Return: -EIO on write failure
 */
int mei_hbm_cl_disconnect_req(struct mei_device *dev, struct mei_cl *cl)
{
	const size_t len = sizeof(struct hbm_client_connect_request);

	return mei_hbm_cl_write(dev, cl, CLIENT_DISCONNECT_REQ_CMD, len);
}

/**
 * mei_hbm_cl_disconnect_rsp - sends disconnect respose to the FW
 *
 * @dev: the device structure
 * @cl: a client to disconnect from
 *
 * Return: -EIO on write failure
 */
int mei_hbm_cl_disconnect_rsp(struct mei_device *dev, struct mei_cl *cl)
{
	const size_t len = sizeof(struct hbm_client_connect_response);

	return mei_hbm_cl_write(dev, cl, CLIENT_DISCONNECT_RES_CMD, len);
}

/**
 * mei_hbm_cl_disconnect_res - update the client state according
 *       disconnect response
 *
 * @dev: the device structure
 * @cl: mei host client
 * @cmd: disconnect client response host bus message
 */
static void mei_hbm_cl_disconnect_res(struct mei_device *dev, struct mei_cl *cl,
				      struct mei_hbm_cl_cmd *cmd)
{
	struct hbm_client_connect_response *rs =
		(struct hbm_client_connect_response *)cmd;

	cl_dbg(dev, cl, "hbm: disconnect response status=%d\n", rs->status);

	if (rs->status == MEI_CL_DISCONN_SUCCESS)
		cl->state = MEI_FILE_DISCONNECTED;
	cl->status = 0;
}

/**
 * mei_hbm_cl_connect_req - send connection request to specific me client
 *
 * @dev: the device structure
 * @cl: a client to connect to
 *
 * Return: -EIO on write failure
 */
int mei_hbm_cl_connect_req(struct mei_device *dev, struct mei_cl *cl)
{
	const size_t len = sizeof(struct hbm_client_connect_request);

	return mei_hbm_cl_write(dev, cl, CLIENT_CONNECT_REQ_CMD, len);
}

/**
 * mei_hbm_cl_connect_res - update the client state according
 *        connection response
 *
 * @dev: the device structure
 * @cl: mei host client
 * @cmd: connect client response host bus message
 */
static void mei_hbm_cl_connect_res(struct mei_device *dev, struct mei_cl *cl,
				   struct mei_hbm_cl_cmd *cmd)
{
	struct hbm_client_connect_response *rs =
		(struct hbm_client_connect_response *)cmd;

	cl_dbg(dev, cl, "hbm: connect response status=%s\n",
			mei_cl_conn_status_str(rs->status));

	if (rs->status == MEI_CL_CONN_SUCCESS)
		cl->state = MEI_FILE_CONNECTED;
	else
		cl->state = MEI_FILE_DISCONNECTED;
	cl->status = mei_cl_conn_status_to_errno(rs->status);
}

/**
 * mei_hbm_cl_res - process hbm response received on behalf
 *         an client
 *
 * @dev: the device structure
 * @rs:  hbm client message
 * @fop_type: file operation type
 */
static void mei_hbm_cl_res(struct mei_device *dev,
			   struct mei_hbm_cl_cmd *rs,
			   enum mei_cb_file_ops fop_type)
{
	struct mei_cl *cl;
	struct mei_cl_cb *cb, *next;

	cl = NULL;
	list_for_each_entry_safe(cb, next, &dev->ctrl_rd_list.list, list) {

		cl = cb->cl;

		if (cb->fop_type != fop_type)
			continue;

		if (mei_hbm_cl_addr_equal(cl, rs)) {
			list_del(&cb->list);
			break;
		}
	}

	if (!cl)
		return;

	switch (fop_type) {
	case MEI_FOP_CONNECT:
		mei_hbm_cl_connect_res(dev, cl, rs);
		break;
	case MEI_FOP_DISCONNECT:
		mei_hbm_cl_disconnect_res(dev, cl, rs);
		break;
	default:
		return;
	}

	cl->timer_count = 0;
	wake_up(&cl->wait);
}


/**
 * mei_hbm_fw_disconnect_req - disconnect request initiated by ME firmware
 *  host sends disconnect response
 *
 * @dev: the device structure.
 * @disconnect_req: disconnect request bus message from the me
 *
 * Return: -ENOMEM on allocation failure
 */
static int mei_hbm_fw_disconnect_req(struct mei_device *dev,
		struct hbm_client_connect_request *disconnect_req)
{
	struct mei_cl *cl;
	struct mei_cl_cb *cb;

	cl = mei_hbm_cl_find_by_cmd(dev, disconnect_req);
	if (cl) {
		cl_dbg(dev, cl, "disconnect request received\n");
		cl->state = MEI_FILE_DISCONNECTED;
		cl->timer_count = 0;

		cb = mei_io_cb_init(cl, NULL);
		if (!cb)
			return -ENOMEM;
		cb->fop_type = MEI_FOP_DISCONNECT_RSP;
		cl_dbg(dev, cl, "add disconnect response as first\n");
		list_add(&cb->list, &dev->ctrl_wr_list.list);
	}
	return 0;
}

/**
 * mei_hbm_config_features - check what hbm features and commands
 *        are supported by the fw
 *
 * @dev: the device structure
 */
static void mei_hbm_config_features(struct mei_device *dev)
{
	/* Power Gating Isolation Support */
	dev->hbm_f_pg_supported = 0;
	if (dev->version.major_version > HBM_MAJOR_VERSION_PGI)
		dev->hbm_f_pg_supported = 1;

	if (dev->version.major_version == HBM_MAJOR_VERSION_PGI &&
	    dev->version.minor_version >= HBM_MINOR_VERSION_PGI)
		dev->hbm_f_pg_supported = 1;
}

/**
 * mei_hbm_version_is_supported - checks whether the driver can
 *     support the hbm version of the device
 *
 * @dev: the device structure
 * Return: true if driver can support hbm version of the device
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
 * @hdr: header of bus message
 *
 * Return: 0 on success and < 0 on failure
 */
int mei_hbm_dispatch(struct mei_device *dev, struct mei_msg_hdr *hdr)
{
	struct mei_bus_message *mei_msg;
	struct hbm_host_version_response *version_res;
	struct hbm_props_response *props_res;
	struct hbm_host_enum_response *enum_res;

	struct mei_hbm_cl_cmd *cl_cmd;
	struct hbm_client_connect_request *disconnect_req;
	struct hbm_flow_control *flow_control;

	/* read the message to our buffer */
	BUG_ON(hdr->length >= sizeof(dev->rd_msg_buf));
	mei_read_slots(dev, dev->rd_msg_buf, hdr->length);
	mei_msg = (struct mei_bus_message *)dev->rd_msg_buf;
	cl_cmd  = (struct mei_hbm_cl_cmd *)mei_msg;

	/* ignore spurious message and prevent reset nesting
	 * hbm is put to idle during system reset
	 */
	if (dev->hbm_state == MEI_HBM_IDLE) {
		dev_dbg(dev->dev, "hbm: state is idle ignore spurious messages\n");
		return 0;
	}

	switch (mei_msg->hbm_cmd) {
	case HOST_START_RES_CMD:
		dev_dbg(dev->dev, "hbm: start: response message received.\n");

		dev->init_clients_timer = 0;

		version_res = (struct hbm_host_version_response *)mei_msg;

		dev_dbg(dev->dev, "HBM VERSION: DRIVER=%02d:%02d DEVICE=%02d:%02d\n",
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
			dev_warn(dev->dev, "hbm: start: version mismatch - stopping the driver.\n");

			dev->hbm_state = MEI_HBM_STOPPED;
			if (mei_hbm_stop_req(dev)) {
				dev_err(dev->dev, "hbm: start: failed to send stop request\n");
				return -EIO;
			}
			break;
		}

		mei_hbm_config_features(dev);

		if (dev->dev_state != MEI_DEV_INIT_CLIENTS ||
		    dev->hbm_state != MEI_HBM_STARTING) {
			dev_err(dev->dev, "hbm: start: state mismatch, [%d, %d]\n",
				dev->dev_state, dev->hbm_state);
			return -EPROTO;
		}

		if (mei_hbm_enum_clients_req(dev)) {
			dev_err(dev->dev, "hbm: start: failed to send enumeration request\n");
			return -EIO;
		}

		wake_up(&dev->wait_hbm_start);
		break;

	case CLIENT_CONNECT_RES_CMD:
		dev_dbg(dev->dev, "hbm: client connect response: message received.\n");
		mei_hbm_cl_res(dev, cl_cmd, MEI_FOP_CONNECT);
		break;

	case CLIENT_DISCONNECT_RES_CMD:
		dev_dbg(dev->dev, "hbm: client disconnect response: message received.\n");
		mei_hbm_cl_res(dev, cl_cmd, MEI_FOP_DISCONNECT);
		break;

	case MEI_FLOW_CONTROL_CMD:
		dev_dbg(dev->dev, "hbm: client flow control response: message received.\n");

		flow_control = (struct hbm_flow_control *) mei_msg;
		mei_hbm_cl_flow_control_res(dev, flow_control);
		break;

	case MEI_PG_ISOLATION_ENTRY_RES_CMD:
		dev_dbg(dev->dev, "power gate isolation entry response received\n");
		dev->pg_event = MEI_PG_EVENT_RECEIVED;
		if (waitqueue_active(&dev->wait_pg))
			wake_up(&dev->wait_pg);
		break;

	case MEI_PG_ISOLATION_EXIT_REQ_CMD:
		dev_dbg(dev->dev, "power gate isolation exit request received\n");
		dev->pg_event = MEI_PG_EVENT_RECEIVED;
		if (waitqueue_active(&dev->wait_pg))
			wake_up(&dev->wait_pg);
		else
			/*
			* If the driver is not waiting on this then
			* this is HW initiated exit from PG.
			* Start runtime pm resume sequence to exit from PG.
			*/
			pm_request_resume(dev->dev);
		break;

	case HOST_CLIENT_PROPERTIES_RES_CMD:
		dev_dbg(dev->dev, "hbm: properties response: message received.\n");

		dev->init_clients_timer = 0;

		if (dev->dev_state != MEI_DEV_INIT_CLIENTS ||
		    dev->hbm_state != MEI_HBM_CLIENT_PROPERTIES) {
			dev_err(dev->dev, "hbm: properties response: state mismatch, [%d, %d]\n",
				dev->dev_state, dev->hbm_state);
			return -EPROTO;
		}

		props_res = (struct hbm_props_response *)mei_msg;

		if (props_res->status) {
			dev_err(dev->dev, "hbm: properties response: wrong status = %d %s\n",
				props_res->status,
				mei_hbm_status_str(props_res->status));
			return -EPROTO;
		}

		mei_hbm_me_cl_add(dev, props_res);

		dev->me_client_index++;

		/* request property for the next client */
		if (mei_hbm_prop_req(dev))
			return -EIO;

		break;

	case HOST_ENUM_RES_CMD:
		dev_dbg(dev->dev, "hbm: enumeration response: message received\n");

		dev->init_clients_timer = 0;

		enum_res = (struct hbm_host_enum_response *) mei_msg;
		BUILD_BUG_ON(sizeof(dev->me_clients_map)
				< sizeof(enum_res->valid_addresses));
		memcpy(dev->me_clients_map, enum_res->valid_addresses,
				sizeof(enum_res->valid_addresses));

		if (dev->dev_state != MEI_DEV_INIT_CLIENTS ||
		    dev->hbm_state != MEI_HBM_ENUM_CLIENTS) {
			dev_err(dev->dev, "hbm: enumeration response: state mismatch, [%d, %d]\n",
				dev->dev_state, dev->hbm_state);
			return -EPROTO;
		}

		dev->hbm_state = MEI_HBM_CLIENT_PROPERTIES;

		/* first property request */
		if (mei_hbm_prop_req(dev))
			return -EIO;

		break;

	case HOST_STOP_RES_CMD:
		dev_dbg(dev->dev, "hbm: stop response: message received\n");

		dev->init_clients_timer = 0;

		if (dev->hbm_state != MEI_HBM_STOPPED) {
			dev_err(dev->dev, "hbm: stop response: state mismatch, [%d, %d]\n",
				dev->dev_state, dev->hbm_state);
			return -EPROTO;
		}

		dev->dev_state = MEI_DEV_POWER_DOWN;
		dev_info(dev->dev, "hbm: stop response: resetting.\n");
		/* force the reset */
		return -EPROTO;
		break;

	case CLIENT_DISCONNECT_REQ_CMD:
		dev_dbg(dev->dev, "hbm: disconnect request: message received\n");

		disconnect_req = (struct hbm_client_connect_request *)mei_msg;
		mei_hbm_fw_disconnect_req(dev, disconnect_req);
		break;

	case ME_STOP_REQ_CMD:
		dev_dbg(dev->dev, "hbm: stop request: message received\n");
		dev->hbm_state = MEI_HBM_STOPPED;
		if (mei_hbm_stop_req(dev)) {
			dev_err(dev->dev, "hbm: stop request: failed to send stop request\n");
			return -EIO;
		}
		break;
	default:
		BUG();
		break;

	}
	return 0;
}

