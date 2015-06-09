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

#define EC_COMMAND_RETRIES	50

int cros_ec_prepare_tx(struct cros_ec_device *ec_dev,
		       struct cros_ec_command *msg)
{
	uint8_t *out;
	int csum, i;

	BUG_ON(msg->outsize > EC_PROTO2_MAX_PARAM_SIZE);
	out = ec_dev->dout;
	out[0] = EC_CMD_VERSION0 + msg->version;
	out[1] = msg->command;
	out[2] = msg->outsize;
	csum = out[0] + out[1] + out[2];
	for (i = 0; i < msg->outsize; i++)
		csum += out[EC_MSG_TX_HEADER_BYTES + i] = msg->data[i];
	out[EC_MSG_TX_HEADER_BYTES + msg->outsize] = (uint8_t)(csum & 0xff);

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

int cros_ec_cmd_xfer(struct cros_ec_device *ec_dev,
		     struct cros_ec_command *msg)
{
	int ret;

	mutex_lock(&ec_dev->lock);
	ret = ec_dev->cmd_xfer(ec_dev, msg);
	if (msg->result == EC_RES_IN_PROGRESS) {
		int i;
		struct cros_ec_command *status_msg;
		struct ec_response_get_comms_status *status;

		status_msg = kmalloc(sizeof(*status_msg) + sizeof(*status),
				     GFP_KERNEL);
		if (!status_msg) {
			ret = -ENOMEM;
			goto exit;
		}

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

			ret = ec_dev->cmd_xfer(ec_dev, status_msg);
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
exit:
	mutex_unlock(&ec_dev->lock);

	return ret;
}
EXPORT_SYMBOL(cros_ec_cmd_xfer);
