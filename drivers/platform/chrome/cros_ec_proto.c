/*
 * ChromeOS EC communication protocol helper functions
 *
 * Copyright (C) 2015 Google, Inc
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/mfd/cros_ec.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <asm/unaligned.h>

#define EC_COMMAND_RETRIES	50

static int prepare_packet(struct cros_ec_device *ec_dev,
			  struct cros_ec_command *msg)
{
	struct ec_host_request *request;
	u8 *out;
	int i;
	u8 csum = 0;

	BUG_ON(ec_dev->proto_version != EC_HOST_REQUEST_VERSION);
	BUG_ON(msg->outsize + sizeof(*request) > ec_dev->dout_size);

	out = ec_dev->dout;
	request = (struct ec_host_request *)out;
	request->struct_version = EC_HOST_REQUEST_VERSION;
	request->checksum = 0;
	request->command = msg->command;
	request->command_version = msg->version;
	request->reserved = 0;
	request->data_len = msg->outsize;

	for (i = 0; i < sizeof(*request); i++)
		csum += out[i];

	/* Copy data and update checksum */
	memcpy(out + sizeof(*request), msg->data, msg->outsize);
	for (i = 0; i < msg->outsize; i++)
		csum += msg->data[i];

	request->checksum = -csum;

	return sizeof(*request) + msg->outsize;
}

static int send_command(struct cros_ec_device *ec_dev,
			struct cros_ec_command *msg)
{
	int ret;
	int (*xfer_fxn)(struct cros_ec_device *ec, struct cros_ec_command *msg);

	if (ec_dev->proto_version > 2)
		xfer_fxn = ec_dev->pkt_xfer;
	else
		xfer_fxn = ec_dev->cmd_xfer;

	ret = (*xfer_fxn)(ec_dev, msg);
	if (msg->result == EC_RES_IN_PROGRESS) {
		int i;
		struct cros_ec_command *status_msg;
		struct ec_response_get_comms_status *status;

		status_msg = kmalloc(sizeof(*status_msg) + sizeof(*status),
				     GFP_KERNEL);
		if (!status_msg)
			return -ENOMEM;

		status_msg->version = 0;
		status_msg->command = EC_CMD_GET_COMMS_STATUS;
		status_msg->insize = sizeof(*status);
		status_msg->outsize = 0;

		/*
		 * Query the EC's status until it's no longer busy or
		 * we encounter an error.
		 */
		for (i = 0; i < EC_COMMAND_RETRIES; i++) {
			usleep_range(10000, 11000);

			ret = (*xfer_fxn)(ec_dev, status_msg);
			if (ret == -EAGAIN)
				continue;
			if (ret < 0)
				break;

			msg->result = status_msg->result;
			if (status_msg->result != EC_RES_SUCCESS)
				break;

			status = (struct ec_response_get_comms_status *)
				 status_msg->data;
			if (!(status->flags & EC_COMMS_STATUS_PROCESSING))
				break;
		}

		kfree(status_msg);
	}

	return ret;
}

int cros_ec_prepare_tx(struct cros_ec_device *ec_dev,
		       struct cros_ec_command *msg)
{
	u8 *out;
	u8 csum;
	int i;

	if (ec_dev->proto_version > 2)
		return prepare_packet(ec_dev, msg);

	BUG_ON(msg->outsize > EC_PROTO2_MAX_PARAM_SIZE);
	out = ec_dev->dout;
	out[0] = EC_CMD_VERSION0 + msg->version;
	out[1] = msg->command;
	out[2] = msg->outsize;
	csum = out[0] + out[1] + out[2];
	for (i = 0; i < msg->outsize; i++)
		csum += out[EC_MSG_TX_HEADER_BYTES + i] = msg->data[i];
	out[EC_MSG_TX_HEADER_BYTES + msg->outsize] = csum;

	return EC_MSG_TX_PROTO_BYTES + msg->outsize;
}
EXPORT_SYMBOL(cros_ec_prepare_tx);

int cros_ec_check_result(struct cros_ec_device *ec_dev,
			 struct cros_ec_command *msg)
{
	switch (msg->result) {
	case EC_RES_SUCCESS:
		return 0;
	case EC_RES_IN_PROGRESS:
		dev_dbg(ec_dev->dev, "command 0x%02x in progress\n",
			msg->command);
		return -EAGAIN;
	default:
		dev_dbg(ec_dev->dev, "command 0x%02x returned %d\n",
			msg->command, msg->result);
		return 0;
	}
}
EXPORT_SYMBOL(cros_ec_check_result);

/*
 * cros_ec_get_host_event_wake_mask
 *
 * Get the mask of host events that cause wake from suspend.
 *
 * @ec_dev: EC device to call
 * @msg: message structure to use
 * @mask: result when function returns >=0.
 *
 * LOCKING:
 * the caller has ec_dev->lock mutex, or the caller knows there is
 * no other command in progress.
 */
static int cros_ec_get_host_event_wake_mask(struct cros_ec_device *ec_dev,
					    struct cros_ec_command *msg,
					    uint32_t *mask)
{
	struct ec_response_host_event_mask *r;
	int ret;

	msg->command = EC_CMD_HOST_EVENT_GET_WAKE_MASK;
	msg->version = 0;
	msg->outsize = 0;
	msg->insize = sizeof(*r);

	ret = send_command(ec_dev, msg);
	if (ret > 0) {
		r = (struct ec_response_host_event_mask *)msg->data;
		*mask = r->mask;
	}

	return ret;
}

static int cros_ec_host_command_proto_query(struct cros_ec_device *ec_dev,
					    int devidx,
					    struct cros_ec_command *msg)
{
	/*
	 * Try using v3+ to query for supported protocols. If this
	 * command fails, fall back to v2. Returns the highest protocol
	 * supported by the EC.
	 * Also sets the max request/response/passthru size.
	 */
	int ret;

	if (!ec_dev->pkt_xfer)
		return -EPROTONOSUPPORT;

	memset(msg, 0, sizeof(*msg));
	msg->command = EC_CMD_PASSTHRU_OFFSET(devidx) | EC_CMD_GET_PROTOCOL_INFO;
	msg->insize = sizeof(struct ec_response_get_protocol_info);

	ret = send_command(ec_dev, msg);

	if (ret < 0) {
		dev_dbg(ec_dev->dev,
			"failed to check for EC[%d] protocol version: %d\n",
			devidx, ret);
		return ret;
	}

	if (devidx > 0 && msg->result == EC_RES_INVALID_COMMAND)
		return -ENODEV;
	else if (msg->result != EC_RES_SUCCESS)
		return msg->result;

	return 0;
}

static int cros_ec_host_command_proto_query_v2(struct cros_ec_device *ec_dev)
{
	struct cros_ec_command *msg;
	struct ec_params_hello *hello_params;
	struct ec_response_hello *hello_response;
	int ret;
	int len = max(sizeof(*hello_params), sizeof(*hello_response));

	msg = kmalloc(sizeof(*msg) + len, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	msg->version = 0;
	msg->command = EC_CMD_HELLO;
	hello_params = (struct ec_params_hello *)msg->data;
	msg->outsize = sizeof(*hello_params);
	hello_response = (struct ec_response_hello *)msg->data;
	msg->insize = sizeof(*hello_response);

	hello_params->in_data = 0xa0b0c0d0;

	ret = send_command(ec_dev, msg);

	if (ret < 0) {
		dev_dbg(ec_dev->dev,
			"EC failed to respond to v2 hello: %d\n",
			ret);
		goto exit;
	} else if (msg->result != EC_RES_SUCCESS) {
		dev_err(ec_dev->dev,
			"EC responded to v2 hello with error: %d\n",
			msg->result);
		ret = msg->result;
		goto exit;
	} else if (hello_response->out_data != 0xa1b2c3d4) {
		dev_err(ec_dev->dev,
			"EC responded to v2 hello with bad result: %u\n",
			hello_response->out_data);
		ret = -EBADMSG;
		goto exit;
	}

	ret = 0;

 exit:
	kfree(msg);
	return ret;
}

/*
 * cros_ec_get_host_command_version_mask
 *
 * Get the version mask of a given command.
 *
 * @ec_dev: EC device to call
 * @msg: message structure to use
 * @cmd: command to get the version of.
 * @mask: result when function returns 0.
 *
 * @return 0 on success, error code otherwise
 *
 * LOCKING:
 * the caller has ec_dev->lock mutex or the caller knows there is
 * no other command in progress.
 */
static int cros_ec_get_host_command_version_mask(struct cros_ec_device *ec_dev,
	u16 cmd, u32 *mask)
{
	struct ec_params_get_cmd_versions *pver;
	struct ec_response_get_cmd_versions *rver;
	struct cros_ec_command *msg;
	int ret;

	msg = kmalloc(sizeof(*msg) + max(sizeof(*rver), sizeof(*pver)),
		      GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	msg->version = 0;
	msg->command = EC_CMD_GET_CMD_VERSIONS;
	msg->insize = sizeof(*rver);
	msg->outsize = sizeof(*pver);

	pver = (struct ec_params_get_cmd_versions *)msg->data;
	pver->cmd = cmd;

	ret = send_command(ec_dev, msg);
	if (ret > 0) {
		rver = (struct ec_response_get_cmd_versions *)msg->data;
		*mask = rver->version_mask;
	}

	kfree(msg);

	return ret;
}

int cros_ec_query_all(struct cros_ec_device *ec_dev)
{
	struct device *dev = ec_dev->dev;
	struct cros_ec_command *proto_msg;
	struct ec_response_get_protocol_info *proto_info;
	u32 ver_mask = 0;
	int ret;

	proto_msg = kzalloc(sizeof(*proto_msg) + sizeof(*proto_info),
			    GFP_KERNEL);
	if (!proto_msg)
		return -ENOMEM;

	/* First try sending with proto v3. */
	ec_dev->proto_version = 3;
	ret = cros_ec_host_command_proto_query(ec_dev, 0, proto_msg);

	if (ret == 0) {
		proto_info = (struct ec_response_get_protocol_info *)
			proto_msg->data;
		ec_dev->max_request = proto_info->max_request_packet_size -
			sizeof(struct ec_host_request);
		ec_dev->max_response = proto_info->max_response_packet_size -
			sizeof(struct ec_host_response);
		ec_dev->proto_version =
			min(EC_HOST_REQUEST_VERSION,
					fls(proto_info->protocol_versions) - 1);
		dev_dbg(ec_dev->dev,
			"using proto v%u\n",
			ec_dev->proto_version);

		ec_dev->din_size = ec_dev->max_response +
			sizeof(struct ec_host_response) +
			EC_MAX_RESPONSE_OVERHEAD;
		ec_dev->dout_size = ec_dev->max_request +
			sizeof(struct ec_host_request) +
			EC_MAX_REQUEST_OVERHEAD;

		/*
		 * Check for PD
		 */
		ret = cros_ec_host_command_proto_query(ec_dev, 1, proto_msg);

		if (ret) {
			dev_dbg(ec_dev->dev, "no PD chip found: %d\n", ret);
			ec_dev->max_passthru = 0;
		} else {
			dev_dbg(ec_dev->dev, "found PD chip\n");
			ec_dev->max_passthru =
				proto_info->max_request_packet_size -
				sizeof(struct ec_host_request);
		}
	} else {
		/* Try querying with a v2 hello message. */
		ec_dev->proto_version = 2;
		ret = cros_ec_host_command_proto_query_v2(ec_dev);

		if (ret == 0) {
			/* V2 hello succeeded. */
			dev_dbg(ec_dev->dev, "falling back to proto v2\n");

			ec_dev->max_request = EC_PROTO2_MAX_PARAM_SIZE;
			ec_dev->max_response = EC_PROTO2_MAX_PARAM_SIZE;
			ec_dev->max_passthru = 0;
			ec_dev->pkt_xfer = NULL;
			ec_dev->din_size = EC_PROTO2_MSG_BYTES;
			ec_dev->dout_size = EC_PROTO2_MSG_BYTES;
		} else {
			/*
			 * It's possible for a test to occur too early when
			 * the EC isn't listening. If this happens, we'll
			 * test later when the first command is run.
			 */
			ec_dev->proto_version = EC_PROTO_VERSION_UNKNOWN;
			dev_dbg(ec_dev->dev, "EC query failed: %d\n", ret);
			goto exit;
		}
	}

	devm_kfree(dev, ec_dev->din);
	devm_kfree(dev, ec_dev->dout);

	ec_dev->din = devm_kzalloc(dev, ec_dev->din_size, GFP_KERNEL);
	if (!ec_dev->din) {
		ret = -ENOMEM;
		goto exit;
	}

	ec_dev->dout = devm_kzalloc(dev, ec_dev->dout_size, GFP_KERNEL);
	if (!ec_dev->dout) {
		devm_kfree(dev, ec_dev->din);
		ret = -ENOMEM;
		goto exit;
	}

	/* Probe if MKBP event is supported */
	ret = cros_ec_get_host_command_version_mask(ec_dev,
						    EC_CMD_GET_NEXT_EVENT,
						    &ver_mask);
	if (ret < 0 || ver_mask == 0)
		ec_dev->mkbp_event_supported = 0;
	else
		ec_dev->mkbp_event_supported = 1;

	/*
	 * Get host event wake mask, assume all events are wake events
	 * if unavailable.
	 */
	ret = cros_ec_get_host_event_wake_mask(ec_dev, proto_msg,
					       &ec_dev->host_event_wake_mask);
	if (ret < 0)
		ec_dev->host_event_wake_mask = U32_MAX;

	ret = 0;

exit:
	kfree(proto_msg);
	return ret;
}
EXPORT_SYMBOL(cros_ec_query_all);

int cros_ec_cmd_xfer(struct cros_ec_device *ec_dev,
		     struct cros_ec_command *msg)
{
	int ret;

	mutex_lock(&ec_dev->lock);
	if (ec_dev->proto_version == EC_PROTO_VERSION_UNKNOWN) {
		ret = cros_ec_query_all(ec_dev);
		if (ret) {
			dev_err(ec_dev->dev,
				"EC version unknown and query failed; aborting command\n");
			mutex_unlock(&ec_dev->lock);
			return ret;
		}
	}

	if (msg->insize > ec_dev->max_response) {
		dev_dbg(ec_dev->dev, "clamping message receive buffer\n");
		msg->insize = ec_dev->max_response;
	}

	if (msg->command < EC_CMD_PASSTHRU_OFFSET(1)) {
		if (msg->outsize > ec_dev->max_request) {
			dev_err(ec_dev->dev,
				"request of size %u is too big (max: %u)\n",
				msg->outsize,
				ec_dev->max_request);
			mutex_unlock(&ec_dev->lock);
			return -EMSGSIZE;
		}
	} else {
		if (msg->outsize > ec_dev->max_passthru) {
			dev_err(ec_dev->dev,
				"passthru rq of size %u is too big (max: %u)\n",
				msg->outsize,
				ec_dev->max_passthru);
			mutex_unlock(&ec_dev->lock);
			return -EMSGSIZE;
		}
	}
	ret = send_command(ec_dev, msg);
	mutex_unlock(&ec_dev->lock);

	return ret;
}
EXPORT_SYMBOL(cros_ec_cmd_xfer);

int cros_ec_cmd_xfer_status(struct cros_ec_device *ec_dev,
			    struct cros_ec_command *msg)
{
	int ret;

	ret = cros_ec_cmd_xfer(ec_dev, msg);
	if (ret < 0) {
		dev_err(ec_dev->dev, "Command xfer error (err:%d)\n", ret);
	} else if (msg->result != EC_RES_SUCCESS) {
		dev_dbg(ec_dev->dev, "Command result (err: %d)\n", msg->result);
		return -EPROTO;
	}

	return ret;
}
EXPORT_SYMBOL(cros_ec_cmd_xfer_status);

static int get_next_event(struct cros_ec_device *ec_dev)
{
	u8 buffer[sizeof(struct cros_ec_command) + sizeof(ec_dev->event_data)];
	struct cros_ec_command *msg = (struct cros_ec_command *)&buffer;
	int ret;

	if (ec_dev->suspended) {
		dev_dbg(ec_dev->dev, "Device suspended.\n");
		return -EHOSTDOWN;
	}

	msg->version = 0;
	msg->command = EC_CMD_GET_NEXT_EVENT;
	msg->insize = sizeof(ec_dev->event_data);
	msg->outsize = 0;

	ret = cros_ec_cmd_xfer(ec_dev, msg);
	if (ret > 0) {
		ec_dev->event_size = ret - 1;
		memcpy(&ec_dev->event_data, msg->data,
		       sizeof(ec_dev->event_data));
	}

	return ret;
}

static int get_keyboard_state_event(struct cros_ec_device *ec_dev)
{
	u8 buffer[sizeof(struct cros_ec_command) +
		  sizeof(ec_dev->event_data.data)];
	struct cros_ec_command *msg = (struct cros_ec_command *)&buffer;

	msg->version = 0;
	msg->command = EC_CMD_MKBP_STATE;
	msg->insize = sizeof(ec_dev->event_data.data);
	msg->outsize = 0;

	ec_dev->event_size = cros_ec_cmd_xfer(ec_dev, msg);
	ec_dev->event_data.event_type = EC_MKBP_EVENT_KEY_MATRIX;
	memcpy(&ec_dev->event_data.data, msg->data,
	       sizeof(ec_dev->event_data.data));

	return ec_dev->event_size;
}

int cros_ec_get_next_event(struct cros_ec_device *ec_dev, bool *wake_event)
{
	u32 host_event;
	int ret;

	if (!ec_dev->mkbp_event_supported) {
		ret = get_keyboard_state_event(ec_dev);
		if (ret < 0)
			return ret;

		if (wake_event)
			*wake_event = true;

		return ret;
	}

	ret = get_next_event(ec_dev);
	if (ret < 0)
		return ret;

	if (wake_event) {
		host_event = cros_ec_get_host_event(ec_dev);

		/* Consider non-host_event as wake event */
		*wake_event = !host_event ||
			      !!(host_event & ec_dev->host_event_wake_mask);
	}

	return ret;
}
EXPORT_SYMBOL(cros_ec_get_next_event);

u32 cros_ec_get_host_event(struct cros_ec_device *ec_dev)
{
	u32 host_event;

	BUG_ON(!ec_dev->mkbp_event_supported);

	if (ec_dev->event_data.event_type != EC_MKBP_EVENT_HOST_EVENT)
		return 0;

	if (ec_dev->event_size != sizeof(host_event)) {
		dev_warn(ec_dev->dev, "Invalid host event size\n");
		return 0;
	}

	host_event = get_unaligned_le32(&ec_dev->event_data.data.host_event);

	return host_event;
}
EXPORT_SYMBOL(cros_ec_get_host_event);
