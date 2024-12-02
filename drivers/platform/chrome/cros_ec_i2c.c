// SPDX-License-Identifier: GPL-2.0
// I2C interface for ChromeOS Embedded Controller
//
// Copyright (C) 2012 Google, Inc

#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/platform_data/cros_ec_commands.h>
#include <linux/platform_data/cros_ec_proto.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "cros_ec.h"

/*
 * Request format for protocol v3
 * byte 0	0xda (EC_COMMAND_PROTOCOL_3)
 * byte 1-8	struct ec_host_request
 * byte 10-	response data
 */
struct ec_host_request_i2c {
	/* Always 0xda to backward compatible with v2 struct */
	uint8_t  command_protocol;
	struct ec_host_request ec_request;
} __packed;


/*
 * Response format for protocol v3
 * byte 0	result code
 * byte 1	packet_length
 * byte 2-9	struct ec_host_response
 * byte 10-	response data
 */
struct ec_host_response_i2c {
	uint8_t result;
	uint8_t packet_length;
	struct ec_host_response ec_response;
} __packed;

static inline struct cros_ec_device *to_ec_dev(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);

	return i2c_get_clientdata(client);
}

static int cros_ec_pkt_xfer_i2c(struct cros_ec_device *ec_dev,
				struct cros_ec_command *msg)
{
	struct i2c_client *client = ec_dev->priv;
	int ret = -ENOMEM;
	int i;
	int packet_len;
	u8 *out_buf = NULL;
	u8 *in_buf = NULL;
	u8 sum;
	struct i2c_msg i2c_msg[2];
	struct ec_host_response *ec_response;
	struct ec_host_request_i2c *ec_request_i2c;
	struct ec_host_response_i2c *ec_response_i2c;
	int request_header_size = sizeof(struct ec_host_request_i2c);
	int response_header_size = sizeof(struct ec_host_response_i2c);

	i2c_msg[0].addr = client->addr;
	i2c_msg[0].flags = 0;
	i2c_msg[1].addr = client->addr;
	i2c_msg[1].flags = I2C_M_RD;

	packet_len = msg->insize + response_header_size;
	if (packet_len > ec_dev->din_size) {
		ret = -EINVAL;
		goto done;
	}
	in_buf = ec_dev->din;
	i2c_msg[1].len = packet_len;
	i2c_msg[1].buf = (char *) in_buf;

	packet_len = msg->outsize + request_header_size;
	if (packet_len > ec_dev->dout_size) {
		ret = -EINVAL;
		goto done;
	}
	out_buf = ec_dev->dout;
	i2c_msg[0].len = packet_len;
	i2c_msg[0].buf = (char *) out_buf;

	/* create request data */
	ec_request_i2c = (struct ec_host_request_i2c *) out_buf;
	ec_request_i2c->command_protocol = EC_COMMAND_PROTOCOL_3;

	ec_dev->dout++;
	ret = cros_ec_prepare_tx(ec_dev, msg);
	if (ret < 0)
		goto done;
	ec_dev->dout--;

	/* send command to EC and read answer */
	ret = i2c_transfer(client->adapter, i2c_msg, 2);
	if (ret < 0) {
		dev_dbg(ec_dev->dev, "i2c transfer failed: %d\n", ret);
		goto done;
	} else if (ret != 2) {
		dev_err(ec_dev->dev, "failed to get response: %d\n", ret);
		ret = -EIO;
		goto done;
	}

	ec_response_i2c = (struct ec_host_response_i2c *) in_buf;
	msg->result = ec_response_i2c->result;
	ec_response = &ec_response_i2c->ec_response;

	switch (msg->result) {
	case EC_RES_SUCCESS:
		break;
	case EC_RES_IN_PROGRESS:
		ret = -EAGAIN;
		dev_dbg(ec_dev->dev, "command 0x%02x in progress\n",
			msg->command);
		goto done;

	default:
		dev_dbg(ec_dev->dev, "command 0x%02x returned %d\n",
			msg->command, msg->result);
		/*
		 * When we send v3 request to v2 ec, ec won't recognize the
		 * 0xda (EC_COMMAND_PROTOCOL_3) and will return with status
		 * EC_RES_INVALID_COMMAND with zero data length.
		 *
		 * In case of invalid command for v3 protocol the data length
		 * will be at least sizeof(struct ec_host_response)
		 */
		if (ec_response_i2c->result == EC_RES_INVALID_COMMAND &&
		    ec_response_i2c->packet_length == 0) {
			ret = -EPROTONOSUPPORT;
			goto done;
		}
	}

	if (ec_response_i2c->packet_length < sizeof(struct ec_host_response)) {
		dev_err(ec_dev->dev,
			"response of %u bytes too short; not a full header\n",
			ec_response_i2c->packet_length);
		ret = -EBADMSG;
		goto done;
	}

	if (msg->insize < ec_response->data_len) {
		dev_err(ec_dev->dev,
			"response data size is too large: expected %u, got %u\n",
			msg->insize,
			ec_response->data_len);
		ret = -EMSGSIZE;
		goto done;
	}

	/* copy response packet payload and compute checksum */
	sum = 0;
	for (i = 0; i < sizeof(struct ec_host_response); i++)
		sum += ((u8 *)ec_response)[i];

	memcpy(msg->data,
	       in_buf + response_header_size,
	       ec_response->data_len);
	for (i = 0; i < ec_response->data_len; i++)
		sum += msg->data[i];

	/* All bytes should sum to zero */
	if (sum) {
		dev_err(ec_dev->dev, "bad packet checksum\n");
		ret = -EBADMSG;
		goto done;
	}

	ret = ec_response->data_len;

done:
	if (msg->command == EC_CMD_REBOOT_EC)
		msleep(EC_REBOOT_DELAY_MS);

	return ret;
}

static int cros_ec_cmd_xfer_i2c(struct cros_ec_device *ec_dev,
				struct cros_ec_command *msg)
{
	struct i2c_client *client = ec_dev->priv;
	int ret = -ENOMEM;
	int i;
	int len;
	int packet_len;
	u8 *out_buf = NULL;
	u8 *in_buf = NULL;
	u8 sum;
	struct i2c_msg i2c_msg[2];

	i2c_msg[0].addr = client->addr;
	i2c_msg[0].flags = 0;
	i2c_msg[1].addr = client->addr;
	i2c_msg[1].flags = I2C_M_RD;

	/*
	 * allocate larger packet (one byte for checksum, one byte for
	 * length, and one for result code)
	 */
	packet_len = msg->insize + 3;
	in_buf = kzalloc(packet_len, GFP_KERNEL);
	if (!in_buf)
		goto done;
	i2c_msg[1].len = packet_len;
	i2c_msg[1].buf = (char *)in_buf;

	/*
	 * allocate larger packet (one byte for checksum, one for
	 * command code, one for length, and one for command version)
	 */
	packet_len = msg->outsize + 4;
	out_buf = kzalloc(packet_len, GFP_KERNEL);
	if (!out_buf)
		goto done;
	i2c_msg[0].len = packet_len;
	i2c_msg[0].buf = (char *)out_buf;

	out_buf[0] = EC_CMD_VERSION0 + msg->version;
	out_buf[1] = msg->command;
	out_buf[2] = msg->outsize;

	/* copy message payload and compute checksum */
	sum = out_buf[0] + out_buf[1] + out_buf[2];
	for (i = 0; i < msg->outsize; i++) {
		out_buf[3 + i] = msg->data[i];
		sum += out_buf[3 + i];
	}
	out_buf[3 + msg->outsize] = sum;

	/* send command to EC and read answer */
	ret = i2c_transfer(client->adapter, i2c_msg, 2);
	if (ret < 0) {
		dev_err(ec_dev->dev, "i2c transfer failed: %d\n", ret);
		goto done;
	} else if (ret != 2) {
		dev_err(ec_dev->dev, "failed to get response: %d\n", ret);
		ret = -EIO;
		goto done;
	}

	/* check response error code */
	msg->result = i2c_msg[1].buf[0];
	ret = cros_ec_check_result(ec_dev, msg);
	if (ret)
		goto done;

	len = in_buf[1];
	if (len > msg->insize) {
		dev_err(ec_dev->dev, "packet too long (%d bytes, expected %d)",
			len, msg->insize);
		ret = -ENOSPC;
		goto done;
	}

	/* copy response packet payload and compute checksum */
	sum = in_buf[0] + in_buf[1];
	for (i = 0; i < len; i++) {
		msg->data[i] = in_buf[2 + i];
		sum += in_buf[2 + i];
	}
	dev_dbg(ec_dev->dev, "packet: %*ph, sum = %02x\n",
		i2c_msg[1].len, in_buf, sum);
	if (sum != in_buf[2 + len]) {
		dev_err(ec_dev->dev, "bad packet checksum\n");
		ret = -EBADMSG;
		goto done;
	}

	ret = len;
done:
	kfree(in_buf);
	kfree(out_buf);
	if (msg->command == EC_CMD_REBOOT_EC)
		msleep(EC_REBOOT_DELAY_MS);

	return ret;
}

static int cros_ec_i2c_probe(struct i2c_client *client,
			     const struct i2c_device_id *dev_id)
{
	struct device *dev = &client->dev;
	struct cros_ec_device *ec_dev = NULL;
	int err;

	ec_dev = devm_kzalloc(dev, sizeof(*ec_dev), GFP_KERNEL);
	if (!ec_dev)
		return -ENOMEM;

	i2c_set_clientdata(client, ec_dev);
	ec_dev->dev = dev;
	ec_dev->priv = client;
	ec_dev->irq = client->irq;
	ec_dev->cmd_xfer = cros_ec_cmd_xfer_i2c;
	ec_dev->pkt_xfer = cros_ec_pkt_xfer_i2c;
	ec_dev->phys_name = client->adapter->name;
	ec_dev->din_size = sizeof(struct ec_host_response_i2c) +
			   sizeof(struct ec_response_get_protocol_info);
	ec_dev->dout_size = sizeof(struct ec_host_request_i2c);

	err = cros_ec_register(ec_dev);
	if (err) {
		dev_err(dev, "cannot register EC\n");
		return err;
	}

	return 0;
}

static void cros_ec_i2c_remove(struct i2c_client *client)
{
	struct cros_ec_device *ec_dev = i2c_get_clientdata(client);

	cros_ec_unregister(ec_dev);
}

#ifdef CONFIG_PM_SLEEP
static int cros_ec_i2c_suspend(struct device *dev)
{
	struct cros_ec_device *ec_dev = to_ec_dev(dev);

	return cros_ec_suspend(ec_dev);
}

static int cros_ec_i2c_resume(struct device *dev)
{
	struct cros_ec_device *ec_dev = to_ec_dev(dev);

	return cros_ec_resume(ec_dev);
}
#endif

static const struct dev_pm_ops cros_ec_i2c_pm_ops = {
	SET_LATE_SYSTEM_SLEEP_PM_OPS(cros_ec_i2c_suspend, cros_ec_i2c_resume)
};

#ifdef CONFIG_OF
static const struct of_device_id cros_ec_i2c_of_match[] = {
	{ .compatible = "google,cros-ec-i2c", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, cros_ec_i2c_of_match);
#endif

static const struct i2c_device_id cros_ec_i2c_id[] = {
	{ "cros-ec-i2c", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, cros_ec_i2c_id);

#ifdef CONFIG_ACPI
static const struct acpi_device_id cros_ec_i2c_acpi_id[] = {
	{ "GOOG0008", 0 },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(acpi, cros_ec_i2c_acpi_id);
#endif

static struct i2c_driver cros_ec_driver = {
	.driver	= {
		.name	= "cros-ec-i2c",
		.acpi_match_table = ACPI_PTR(cros_ec_i2c_acpi_id),
		.of_match_table = of_match_ptr(cros_ec_i2c_of_match),
		.pm	= &cros_ec_i2c_pm_ops,
	},
	.probe		= cros_ec_i2c_probe,
	.remove		= cros_ec_i2c_remove,
	.id_table	= cros_ec_i2c_id,
};

module_i2c_driver(cros_ec_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("I2C interface for ChromeOS Embedded Controller");
