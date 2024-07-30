// SPDX-License-Identifier: GPL-2.0
// ChromeOS EC communication protocol helper functions
//
// Copyright (C) 2015 Google, Inc

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/platform_data/cros_ec_commands.h>
#include <linux/platform_data/cros_ec_proto.h>
#include <linux/slab.h>
#include <asm/unaligned.h>

#include "cros_ec_trace.h"

#define EC_COMMAND_RETRIES	50

static const int cros_ec_error_map[] = {
	[EC_RES_INVALID_COMMAND] = -EOPNOTSUPP,
	[EC_RES_ERROR] = -EIO,
	[EC_RES_INVALID_PARAM] = -EINVAL,
	[EC_RES_ACCESS_DENIED] = -EACCES,
	[EC_RES_INVALID_RESPONSE] = -EPROTO,
	[EC_RES_INVALID_VERSION] = -ENOPROTOOPT,
	[EC_RES_INVALID_CHECKSUM] = -EBADMSG,
	[EC_RES_IN_PROGRESS] = -EINPROGRESS,
	[EC_RES_UNAVAILABLE] = -ENODATA,
	[EC_RES_TIMEOUT] = -ETIMEDOUT,
	[EC_RES_OVERFLOW] = -EOVERFLOW,
	[EC_RES_INVALID_HEADER] = -EBADR,
	[EC_RES_REQUEST_TRUNCATED] = -EBADR,
	[EC_RES_RESPONSE_TOO_BIG] = -EFBIG,
	[EC_RES_BUS_ERROR] = -EFAULT,
	[EC_RES_BUSY] = -EBUSY,
	[EC_RES_INVALID_HEADER_VERSION] = -EBADMSG,
	[EC_RES_INVALID_HEADER_CRC] = -EBADMSG,
	[EC_RES_INVALID_DATA_CRC] = -EBADMSG,
	[EC_RES_DUP_UNAVAILABLE] = -ENODATA,
};

static int cros_ec_map_error(uint32_t result)
{
	int ret = 0;

	if (result != EC_RES_SUCCESS) {
		if (result < ARRAY_SIZE(cros_ec_error_map) && cros_ec_error_map[result])
			ret = cros_ec_error_map[result];
		else
			ret = -EPROTO;
	}

	return ret;
}

static int prepare_tx(struct cros_ec_device *ec_dev,
		      struct cros_ec_command *msg)
{
	struct ec_host_request *request;
	u8 *out;
	int i;
	u8 csum = 0;

	if (msg->outsize + sizeof(*request) > ec_dev->dout_size)
		return -EINVAL;

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

static int prepare_tx_legacy(struct cros_ec_device *ec_dev,
			     struct cros_ec_command *msg)
{
	u8 *out;
	u8 csum;
	int i;

	if (msg->outsize > EC_PROTO2_MAX_PARAM_SIZE)
		return -EINVAL;

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

static int cros_ec_xfer_command(struct cros_ec_device *ec_dev, struct cros_ec_command *msg)
{
	int ret;
	int (*xfer_fxn)(struct cros_ec_device *ec, struct cros_ec_command *msg);

	if (ec_dev->proto_version > 2)
		xfer_fxn = ec_dev->pkt_xfer;
	else
		xfer_fxn = ec_dev->cmd_xfer;

	if (!xfer_fxn) {
		/*
		 * This error can happen if a communication error happened and
		 * the EC is trying to use protocol v2, on an underlying
		 * communication mechanism that does not support v2.
		 */
		dev_err_once(ec_dev->dev, "missing EC transfer API, cannot send command\n");
		return -EIO;
	}

	trace_cros_ec_request_start(msg);
	ret = (*xfer_fxn)(ec_dev, msg);
	trace_cros_ec_request_done(msg, ret);

	return ret;
}

static int cros_ec_wait_until_complete(struct cros_ec_device *ec_dev, uint32_t *result)
{
	struct {
		struct cros_ec_command msg;
		struct ec_response_get_comms_status status;
	} __packed buf;
	struct cros_ec_command *msg = &buf.msg;
	struct ec_response_get_comms_status *status = &buf.status;
	int ret = 0, i;

	msg->version = 0;
	msg->command = EC_CMD_GET_COMMS_STATUS;
	msg->insize = sizeof(*status);
	msg->outsize = 0;

	/* Query the EC's status until it's no longer busy or we encounter an error. */
	for (i = 0; i < EC_COMMAND_RETRIES; ++i) {
		usleep_range(10000, 11000);

		ret = cros_ec_xfer_command(ec_dev, msg);
		if (ret == -EAGAIN)
			continue;
		if (ret < 0)
			return ret;

		*result = msg->result;
		if (msg->result != EC_RES_SUCCESS)
			return ret;

		if (ret == 0) {
			ret = -EPROTO;
			break;
		}

		if (!(status->flags & EC_COMMS_STATUS_PROCESSING))
			return ret;
	}

	if (i >= EC_COMMAND_RETRIES)
		ret = -EAGAIN;

	return ret;
}

static int cros_ec_send_command(struct cros_ec_device *ec_dev, struct cros_ec_command *msg)
{
	int ret = cros_ec_xfer_command(ec_dev, msg);

	if (msg->result == EC_RES_IN_PROGRESS)
		ret = cros_ec_wait_until_complete(ec_dev, &msg->result);

	return ret;
}

/**
 * cros_ec_prepare_tx() - Prepare an outgoing message in the output buffer.
 * @ec_dev: Device to register.
 * @msg: Message to write.
 *
 * This is used by all ChromeOS EC drivers to prepare the outgoing message
 * according to different protocol versions.
 *
 * Return: number of prepared bytes on success or negative error code.
 */
int cros_ec_prepare_tx(struct cros_ec_device *ec_dev,
		       struct cros_ec_command *msg)
{
	if (ec_dev->proto_version > 2)
		return prepare_tx(ec_dev, msg);

	return prepare_tx_legacy(ec_dev, msg);
}
EXPORT_SYMBOL(cros_ec_prepare_tx);

/**
 * cros_ec_check_result() - Check ec_msg->result.
 * @ec_dev: EC device.
 * @msg: Message to check.
 *
 * This is used by ChromeOS EC drivers to check the ec_msg->result for
 * EC_RES_IN_PROGRESS and to warn about them.
 *
 * The function should not check for furthermore error codes.  Otherwise,
 * it would break the ABI.
 *
 * Return: -EAGAIN if ec_msg->result == EC_RES_IN_PROGRESS.  Otherwise, 0.
 */
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
 * @mask: result when function returns 0.
 *
 * LOCKING:
 * the caller has ec_dev->lock mutex, or the caller knows there is
 * no other command in progress.
 */
static int cros_ec_get_host_event_wake_mask(struct cros_ec_device *ec_dev, uint32_t *mask)
{
	struct cros_ec_command *msg;
	struct ec_response_host_event_mask *r;
	int ret, mapped;

	msg = kzalloc(sizeof(*msg) + sizeof(*r), GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	msg->command = EC_CMD_HOST_EVENT_GET_WAKE_MASK;
	msg->insize = sizeof(*r);

	ret = cros_ec_send_command(ec_dev, msg);
	if (ret < 0)
		goto exit;

	mapped = cros_ec_map_error(msg->result);
	if (mapped) {
		ret = mapped;
		goto exit;
	}

	if (ret == 0) {
		ret = -EPROTO;
		goto exit;
	}

	r = (struct ec_response_host_event_mask *)msg->data;
	*mask = r->mask;
	ret = 0;
exit:
	kfree(msg);
	return ret;
}

static int cros_ec_get_proto_info(struct cros_ec_device *ec_dev, int devidx)
{
	struct cros_ec_command *msg;
	struct ec_response_get_protocol_info *info;
	int ret, mapped;

	ec_dev->proto_version = 3;
	if (devidx > 0)
		ec_dev->max_passthru = 0;

	msg = kzalloc(sizeof(*msg) + sizeof(*info), GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	msg->command = EC_CMD_PASSTHRU_OFFSET(devidx) | EC_CMD_GET_PROTOCOL_INFO;
	msg->insize = sizeof(*info);

	ret = cros_ec_send_command(ec_dev, msg);
	/*
	 * Send command once again when timeout occurred.
	 * Fingerprint MCU (FPMCU) is restarted during system boot which
	 * introduces small window in which FPMCU won't respond for any
	 * messages sent by kernel. There is no need to wait before next
	 * attempt because we waited at least EC_MSG_DEADLINE_MS.
	 */
	if (ret == -ETIMEDOUT)
		ret = cros_ec_send_command(ec_dev, msg);

	if (ret < 0) {
		dev_dbg(ec_dev->dev,
			"failed to check for EC[%d] protocol version: %d\n",
			devidx, ret);
		goto exit;
	}

	mapped = cros_ec_map_error(msg->result);
	if (mapped) {
		ret = mapped;
		goto exit;
	}

	if (ret == 0) {
		ret = -EPROTO;
		goto exit;
	}

	info = (struct ec_response_get_protocol_info *)msg->data;

	switch (devidx) {
	case CROS_EC_DEV_EC_INDEX:
		ec_dev->max_request = info->max_request_packet_size -
						sizeof(struct ec_host_request);
		ec_dev->max_response = info->max_response_packet_size -
						sizeof(struct ec_host_response);
		ec_dev->proto_version = min(EC_HOST_REQUEST_VERSION,
					    fls(info->protocol_versions) - 1);
		ec_dev->din_size = info->max_response_packet_size + EC_MAX_RESPONSE_OVERHEAD;
		ec_dev->dout_size = info->max_request_packet_size + EC_MAX_REQUEST_OVERHEAD;

		dev_dbg(ec_dev->dev, "using proto v%u\n", ec_dev->proto_version);
		break;
	case CROS_EC_DEV_PD_INDEX:
		ec_dev->max_passthru = info->max_request_packet_size -
						sizeof(struct ec_host_request);

		dev_dbg(ec_dev->dev, "found PD chip\n");
		break;
	default:
		dev_dbg(ec_dev->dev, "unknown passthru index: %d\n", devidx);
		break;
	}

	ret = 0;
exit:
	kfree(msg);
	return ret;
}

static int cros_ec_get_proto_info_legacy(struct cros_ec_device *ec_dev)
{
	struct cros_ec_command *msg;
	struct ec_params_hello *params;
	struct ec_response_hello *response;
	int ret, mapped;

	ec_dev->proto_version = 2;

	msg = kzalloc(sizeof(*msg) + max(sizeof(*params), sizeof(*response)), GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	msg->command = EC_CMD_HELLO;
	msg->insize = sizeof(*response);
	msg->outsize = sizeof(*params);

	params = (struct ec_params_hello *)msg->data;
	params->in_data = 0xa0b0c0d0;

	ret = cros_ec_send_command(ec_dev, msg);
	if (ret < 0) {
		dev_dbg(ec_dev->dev, "EC failed to respond to v2 hello: %d\n", ret);
		goto exit;
	}

	mapped = cros_ec_map_error(msg->result);
	if (mapped) {
		ret = mapped;
		dev_err(ec_dev->dev, "EC responded to v2 hello with error: %d\n", msg->result);
		goto exit;
	}

	if (ret == 0) {
		ret = -EPROTO;
		goto exit;
	}

	response = (struct ec_response_hello *)msg->data;
	if (response->out_data != 0xa1b2c3d4) {
		dev_err(ec_dev->dev,
			"EC responded to v2 hello with bad result: %u\n",
			response->out_data);
		ret = -EBADMSG;
		goto exit;
	}

	ec_dev->max_request = EC_PROTO2_MAX_PARAM_SIZE;
	ec_dev->max_response = EC_PROTO2_MAX_PARAM_SIZE;
	ec_dev->max_passthru = 0;
	ec_dev->pkt_xfer = NULL;
	ec_dev->din_size = EC_PROTO2_MSG_BYTES;
	ec_dev->dout_size = EC_PROTO2_MSG_BYTES;

	dev_dbg(ec_dev->dev, "falling back to proto v2\n");
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
static int cros_ec_get_host_command_version_mask(struct cros_ec_device *ec_dev, u16 cmd, u32 *mask)
{
	struct ec_params_get_cmd_versions *pver;
	struct ec_response_get_cmd_versions *rver;
	struct cros_ec_command *msg;
	int ret, mapped;

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

	ret = cros_ec_send_command(ec_dev, msg);
	if (ret < 0)
		goto exit;

	mapped = cros_ec_map_error(msg->result);
	if (mapped) {
		ret = mapped;
		goto exit;
	}

	if (ret == 0) {
		ret = -EPROTO;
		goto exit;
	}

	rver = (struct ec_response_get_cmd_versions *)msg->data;
	*mask = rver->version_mask;
	ret = 0;
exit:
	kfree(msg);
	return ret;
}

/**
 * cros_ec_query_all() -  Query the protocol version supported by the
 *         ChromeOS EC.
 * @ec_dev: Device to register.
 *
 * Return: 0 on success or negative error code.
 */
int cros_ec_query_all(struct cros_ec_device *ec_dev)
{
	struct device *dev = ec_dev->dev;
	u32 ver_mask;
	int ret;

	/* First try sending with proto v3. */
	if (!cros_ec_get_proto_info(ec_dev, CROS_EC_DEV_EC_INDEX)) {
		/* Check for PD. */
		cros_ec_get_proto_info(ec_dev, CROS_EC_DEV_PD_INDEX);
	} else {
		/* Try querying with a v2 hello message. */
		ret = cros_ec_get_proto_info_legacy(ec_dev);
		if (ret) {
			/*
			 * It's possible for a test to occur too early when
			 * the EC isn't listening. If this happens, we'll
			 * test later when the first command is run.
			 */
			ec_dev->proto_version = EC_PROTO_VERSION_UNKNOWN;
			dev_dbg(ec_dev->dev, "EC query failed: %d\n", ret);
			return ret;
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
	ret = cros_ec_get_host_command_version_mask(ec_dev, EC_CMD_GET_NEXT_EVENT, &ver_mask);
	if (ret < 0 || ver_mask == 0) {
		ec_dev->mkbp_event_supported = 0;
	} else {
		ec_dev->mkbp_event_supported = fls(ver_mask);

		dev_dbg(ec_dev->dev, "MKBP support version %u\n", ec_dev->mkbp_event_supported - 1);
	}

	/* Probe if host sleep v1 is supported for S0ix failure detection. */
	ret = cros_ec_get_host_command_version_mask(ec_dev, EC_CMD_HOST_SLEEP_EVENT, &ver_mask);
	ec_dev->host_sleep_v1 = (ret == 0 && (ver_mask & EC_VER_MASK(1)));

	/* Get host event wake mask. */
	ret = cros_ec_get_host_event_wake_mask(ec_dev, &ec_dev->host_event_wake_mask);
	if (ret < 0) {
		/*
		 * If the EC doesn't support EC_CMD_HOST_EVENT_GET_WAKE_MASK,
		 * use a reasonable default. Note that we ignore various
		 * battery, AC status, and power-state events, because (a)
		 * those can be quite common (e.g., when sitting at full
		 * charge, on AC) and (b) these are not actionable wake events;
		 * if anything, we'd like to continue suspending (to save
		 * power), not wake up.
		 */
		ec_dev->host_event_wake_mask = U32_MAX &
			~(EC_HOST_EVENT_MASK(EC_HOST_EVENT_LID_CLOSED) |
			  EC_HOST_EVENT_MASK(EC_HOST_EVENT_AC_DISCONNECTED) |
			  EC_HOST_EVENT_MASK(EC_HOST_EVENT_BATTERY_LOW) |
			  EC_HOST_EVENT_MASK(EC_HOST_EVENT_BATTERY_CRITICAL) |
			  EC_HOST_EVENT_MASK(EC_HOST_EVENT_BATTERY) |
			  EC_HOST_EVENT_MASK(EC_HOST_EVENT_PD_MCU) |
			  EC_HOST_EVENT_MASK(EC_HOST_EVENT_BATTERY_STATUS));
		/*
		 * Old ECs may not support this command. Complain about all
		 * other errors.
		 */
		if (ret != -EOPNOTSUPP)
			dev_err(ec_dev->dev,
				"failed to retrieve wake mask: %d\n", ret);
	}

	ret = 0;

exit:
	return ret;
}
EXPORT_SYMBOL(cros_ec_query_all);

/**
 * cros_ec_cmd_xfer() - Send a command to the ChromeOS EC.
 * @ec_dev: EC device.
 * @msg: Message to write.
 *
 * Call this to send a command to the ChromeOS EC. This should be used instead
 * of calling the EC's cmd_xfer() callback directly. This function does not
 * convert EC command execution error codes to Linux error codes. Most
 * in-kernel users will want to use cros_ec_cmd_xfer_status() instead since
 * that function implements the conversion.
 *
 * Return:
 * >0 - EC command was executed successfully. The return value is the number
 *      of bytes returned by the EC (excluding the header).
 * =0 - EC communication was successful. EC command execution results are
 *      reported in msg->result. The result will be EC_RES_SUCCESS if the
 *      command was executed successfully or report an EC command execution
 *      error.
 * <0 - EC communication error. Return value is the Linux error code.
 */
int cros_ec_cmd_xfer(struct cros_ec_device *ec_dev, struct cros_ec_command *msg)
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

	if (msg->command < EC_CMD_PASSTHRU_OFFSET(CROS_EC_DEV_PD_INDEX)) {
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

	ret = cros_ec_send_command(ec_dev, msg);
	mutex_unlock(&ec_dev->lock);

	return ret;
}
EXPORT_SYMBOL(cros_ec_cmd_xfer);

/**
 * cros_ec_cmd_xfer_status() - Send a command to the ChromeOS EC.
 * @ec_dev: EC device.
 * @msg: Message to write.
 *
 * Call this to send a command to the ChromeOS EC. This should be used instead of calling the EC's
 * cmd_xfer() callback directly. It returns success status only if both the command was transmitted
 * successfully and the EC replied with success status.
 *
 * Return:
 * >=0 - The number of bytes transferred.
 * <0 - Linux error code
 */
int cros_ec_cmd_xfer_status(struct cros_ec_device *ec_dev,
			    struct cros_ec_command *msg)
{
	int ret, mapped;

	ret = cros_ec_cmd_xfer(ec_dev, msg);
	if (ret < 0)
		return ret;

	mapped = cros_ec_map_error(msg->result);
	if (mapped) {
		dev_dbg(ec_dev->dev, "Command result (err: %d [%d])\n",
			msg->result, mapped);
		ret = mapped;
	}

	return ret;
}
EXPORT_SYMBOL(cros_ec_cmd_xfer_status);

static int get_next_event_xfer(struct cros_ec_device *ec_dev,
			       struct cros_ec_command *msg,
			       struct ec_response_get_next_event_v1 *event,
			       int version, uint32_t size)
{
	int ret;

	msg->version = version;
	msg->command = EC_CMD_GET_NEXT_EVENT;
	msg->insize = size;
	msg->outsize = 0;

	ret = cros_ec_cmd_xfer_status(ec_dev, msg);
	if (ret > 0) {
		ec_dev->event_size = ret - 1;
		ec_dev->event_data = *event;
	}

	return ret;
}

static int get_next_event(struct cros_ec_device *ec_dev)
{
	struct {
		struct cros_ec_command msg;
		struct ec_response_get_next_event_v1 event;
	} __packed buf;
	struct cros_ec_command *msg = &buf.msg;
	struct ec_response_get_next_event_v1 *event = &buf.event;
	const int cmd_version = ec_dev->mkbp_event_supported - 1;

	memset(msg, 0, sizeof(*msg));
	if (ec_dev->suspended) {
		dev_dbg(ec_dev->dev, "Device suspended.\n");
		return -EHOSTDOWN;
	}

	if (cmd_version == 0)
		return get_next_event_xfer(ec_dev, msg, event, 0,
				  sizeof(struct ec_response_get_next_event));

	return get_next_event_xfer(ec_dev, msg, event, cmd_version,
				sizeof(struct ec_response_get_next_event_v1));
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

	ec_dev->event_size = cros_ec_cmd_xfer_status(ec_dev, msg);
	ec_dev->event_data.event_type = EC_MKBP_EVENT_KEY_MATRIX;
	memcpy(&ec_dev->event_data.data, msg->data,
	       sizeof(ec_dev->event_data.data));

	return ec_dev->event_size;
}

/**
 * cros_ec_get_next_event() - Fetch next event from the ChromeOS EC.
 * @ec_dev: Device to fetch event from.
 * @wake_event: Pointer to a bool set to true upon return if the event might be
 *              treated as a wake event. Ignored if null.
 * @has_more_events: Pointer to bool set to true if more than one event is
 *              pending.
 *              Some EC will set this flag to indicate cros_ec_get_next_event()
 *              can be called multiple times in a row.
 *              It is an optimization to prevent issuing a EC command for
 *              nothing or wait for another interrupt from the EC to process
 *              the next message.
 *              Ignored if null.
 *
 * Return: negative error code on errors; 0 for no data; or else number of
 * bytes received (i.e., an event was retrieved successfully). Event types are
 * written out to @ec_dev->event_data.event_type on success.
 */
int cros_ec_get_next_event(struct cros_ec_device *ec_dev,
			   bool *wake_event,
			   bool *has_more_events)
{
	u8 event_type;
	u32 host_event;
	int ret;
	u32 ver_mask;

	/*
	 * Default value for wake_event.
	 * Wake up on keyboard event, wake up for spurious interrupt or link
	 * error to the EC.
	 */
	if (wake_event)
		*wake_event = true;

	/*
	 * Default value for has_more_events.
	 * EC will raise another interrupt if AP does not process all events
	 * anyway.
	 */
	if (has_more_events)
		*has_more_events = false;

	if (!ec_dev->mkbp_event_supported)
		return get_keyboard_state_event(ec_dev);

	ret = get_next_event(ec_dev);
	/*
	 * -ENOPROTOOPT is returned when EC returns EC_RES_INVALID_VERSION.
	 * This can occur when EC based device (e.g. Fingerprint MCU) jumps to
	 * the RO image which doesn't support newer version of the command. In
	 * this case we will attempt to update maximum supported version of the
	 * EC_CMD_GET_NEXT_EVENT.
	 */
	if (ret == -ENOPROTOOPT) {
		dev_dbg(ec_dev->dev,
			"GET_NEXT_EVENT returned invalid version error.\n");
		mutex_lock(&ec_dev->lock);
		ret = cros_ec_get_host_command_version_mask(ec_dev,
							EC_CMD_GET_NEXT_EVENT,
							&ver_mask);
		mutex_unlock(&ec_dev->lock);
		if (ret < 0 || ver_mask == 0)
			/*
			 * Do not change the MKBP supported version if we can't
			 * obtain supported version correctly. Please note that
			 * calling EC_CMD_GET_NEXT_EVENT returned
			 * EC_RES_INVALID_VERSION which means that the command
			 * is present.
			 */
			return -ENOPROTOOPT;

		ec_dev->mkbp_event_supported = fls(ver_mask);
		dev_dbg(ec_dev->dev, "MKBP support version changed to %u\n",
			ec_dev->mkbp_event_supported - 1);

		/* Try to get next event with new MKBP support version set. */
		ret = get_next_event(ec_dev);
	}

	if (ret <= 0)
		return ret;

	if (has_more_events)
		*has_more_events = ec_dev->event_data.event_type &
			EC_MKBP_HAS_MORE_EVENTS;
	ec_dev->event_data.event_type &= EC_MKBP_EVENT_TYPE_MASK;

	if (wake_event) {
		event_type = ec_dev->event_data.event_type;
		host_event = cros_ec_get_host_event(ec_dev);

		/*
		 * Sensor events need to be parsed by the sensor sub-device.
		 * Defer them, and don't report the wakeup here.
		 */
		if (event_type == EC_MKBP_EVENT_SENSOR_FIFO) {
			*wake_event = false;
		} else if (host_event) {
			/* rtc_update_irq() already handles wakeup events. */
			if (host_event & EC_HOST_EVENT_MASK(EC_HOST_EVENT_RTC))
				*wake_event = false;
			/* Masked host-events should not count as wake events. */
			if (!(host_event & ec_dev->host_event_wake_mask))
				*wake_event = false;
		}
	}

	return ret;
}
EXPORT_SYMBOL(cros_ec_get_next_event);

/**
 * cros_ec_get_host_event() - Return a mask of event set by the ChromeOS EC.
 * @ec_dev: Device to fetch event from.
 *
 * When MKBP is supported, when the EC raises an interrupt, we collect the
 * events raised and call the functions in the ec notifier. This function
 * is a helper to know which events are raised.
 *
 * Return: 0 on error or non-zero bitmask of one or more EC_HOST_EVENT_*.
 */
u32 cros_ec_get_host_event(struct cros_ec_device *ec_dev)
{
	u32 host_event;

	if (!ec_dev->mkbp_event_supported)
		return 0;

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

/**
 * cros_ec_check_features() - Test for the presence of EC features
 *
 * @ec: EC device, does not have to be connected directly to the AP,
 *      can be daisy chained through another device.
 * @feature: One of ec_feature_code bit.
 *
 * Call this function to test whether the ChromeOS EC supports a feature.
 *
 * Return: true if supported, false if not (or if an error was encountered).
 */
bool cros_ec_check_features(struct cros_ec_dev *ec, int feature)
{
	struct ec_response_get_features *features = &ec->features;
	int ret;

	if (features->flags[0] == -1U && features->flags[1] == -1U) {
		/* features bitmap not read yet */
		ret = cros_ec_cmd(ec->ec_dev, 0, EC_CMD_GET_FEATURES + ec->cmd_offset,
				  NULL, 0, features, sizeof(*features));
		if (ret < 0) {
			dev_warn(ec->dev, "cannot get EC features: %d\n", ret);
			memset(features, 0, sizeof(*features));
		}

		dev_dbg(ec->dev, "EC features %08x %08x\n",
			features->flags[0], features->flags[1]);
	}

	return !!(features->flags[feature / 32] & EC_FEATURE_MASK_0(feature));
}
EXPORT_SYMBOL_GPL(cros_ec_check_features);

/**
 * cros_ec_get_sensor_count() - Return the number of MEMS sensors supported.
 *
 * @ec: EC device, does not have to be connected directly to the AP,
 *      can be daisy chained through another device.
 * Return: < 0 in case of error.
 */
int cros_ec_get_sensor_count(struct cros_ec_dev *ec)
{
	/*
	 * Issue a command to get the number of sensor reported.
	 * If not supported, check for legacy mode.
	 */
	int ret, sensor_count;
	struct ec_params_motion_sense *params;
	struct ec_response_motion_sense *resp;
	struct cros_ec_command *msg;
	struct cros_ec_device *ec_dev = ec->ec_dev;
	u8 status;

	msg = kzalloc(sizeof(*msg) + max(sizeof(*params), sizeof(*resp)),
		      GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	msg->version = 1;
	msg->command = EC_CMD_MOTION_SENSE_CMD + ec->cmd_offset;
	msg->outsize = sizeof(*params);
	msg->insize = sizeof(*resp);

	params = (struct ec_params_motion_sense *)msg->data;
	params->cmd = MOTIONSENSE_CMD_DUMP;

	ret = cros_ec_cmd_xfer_status(ec->ec_dev, msg);
	if (ret < 0) {
		sensor_count = ret;
	} else {
		resp = (struct ec_response_motion_sense *)msg->data;
		sensor_count = resp->dump.sensor_count;
	}
	kfree(msg);

	/*
	 * Check legacy mode: Let's find out if sensors are accessible
	 * via LPC interface.
	 */
	if (sensor_count < 0 && ec->cmd_offset == 0 && ec_dev->cmd_readmem) {
		ret = ec_dev->cmd_readmem(ec_dev, EC_MEMMAP_ACC_STATUS,
				1, &status);
		if (ret >= 0 &&
		    (status & EC_MEMMAP_ACC_STATUS_PRESENCE_BIT)) {
			/*
			 * We have 2 sensors, one in the lid, one in the base.
			 */
			sensor_count = 2;
		} else {
			/*
			 * EC uses LPC interface and no sensors are presented.
			 */
			sensor_count = 0;
		}
	}
	return sensor_count;
}
EXPORT_SYMBOL_GPL(cros_ec_get_sensor_count);

/**
 * cros_ec_cmd - Send a command to the EC.
 *
 * @ec_dev: EC device
 * @version: EC command version
 * @command: EC command
 * @outdata: EC command output data
 * @outsize: Size of outdata
 * @indata: EC command input data
 * @insize: Size of indata
 *
 * Return: >= 0 on success, negative error number on failure.
 */
int cros_ec_cmd(struct cros_ec_device *ec_dev,
		unsigned int version,
		int command,
		void *outdata,
		size_t outsize,
		void *indata,
		size_t insize)
{
	struct cros_ec_command *msg;
	int ret;

	msg = kzalloc(sizeof(*msg) + max(insize, outsize), GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	msg->version = version;
	msg->command = command;
	msg->outsize = outsize;
	msg->insize = insize;

	if (outsize)
		memcpy(msg->data, outdata, outsize);

	ret = cros_ec_cmd_xfer_status(ec_dev, msg);
	if (ret < 0)
		goto error;

	if (insize)
		memcpy(indata, msg->data, insize);
error:
	kfree(msg);
	return ret;
}
EXPORT_SYMBOL_GPL(cros_ec_cmd);
