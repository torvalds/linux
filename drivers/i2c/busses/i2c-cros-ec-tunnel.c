// SPDX-License-Identifier: GPL-2.0+
// Expose an I2C passthrough to the ChromeOS EC.
//
// Copyright (C) 2013 Google, Inc.

#include <linux/acpi.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/platform_data/cros_ec_commands.h>
#include <linux/platform_data/cros_ec_proto.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#define I2C_MAX_RETRIES 3

/**
 * struct ec_i2c_device - Driver data for I2C tunnel
 *
 * @dev: Device node
 * @adap: I2C adapter
 * @ec: Pointer to EC device
 * @remote_bus: The EC bus number we tunnel to on the other side.
 * @request_buf: Buffer for transmitting data; we expect most transfers to fit.
 * @response_buf: Buffer for receiving data; we expect most transfers to fit.
 */

struct ec_i2c_device {
	struct device *dev;
	struct i2c_adapter adap;
	struct cros_ec_device *ec;

	u16 remote_bus;

	u8 request_buf[256];
	u8 response_buf[256];
};

/**
 * ec_i2c_count_message - Count bytes needed for ec_i2c_construct_message
 *
 * @i2c_msgs: The i2c messages to read
 * @num: The number of i2c messages.
 *
 * Returns the number of bytes the messages will take up.
 */
static int ec_i2c_count_message(const struct i2c_msg i2c_msgs[], int num)
{
	int i;
	int size;

	size = sizeof(struct ec_params_i2c_passthru);
	size += num * sizeof(struct ec_params_i2c_passthru_msg);
	for (i = 0; i < num; i++)
		if (!(i2c_msgs[i].flags & I2C_M_RD))
			size += i2c_msgs[i].len;

	return size;
}

/**
 * ec_i2c_construct_message - construct a message to go to the EC
 *
 * This function effectively stuffs the standard i2c_msg format of Linux into
 * a format that the EC understands.
 *
 * @buf: The buffer to fill.  We assume that the buffer is big enough.
 * @i2c_msgs: The i2c messages to read.
 * @num: The number of i2c messages.
 * @bus_num: The remote bus number we want to talk to.
 *
 * Returns 0 or a negative error number.
 */
static int ec_i2c_construct_message(u8 *buf, const struct i2c_msg i2c_msgs[],
				    int num, u16 bus_num)
{
	struct ec_params_i2c_passthru *params;
	u8 *out_data;
	int i;

	out_data = buf + sizeof(struct ec_params_i2c_passthru) +
		   num * sizeof(struct ec_params_i2c_passthru_msg);

	params = (struct ec_params_i2c_passthru *)buf;
	params->port = bus_num;
	params->num_msgs = num;
	for (i = 0; i < num; i++) {
		const struct i2c_msg *i2c_msg = &i2c_msgs[i];
		struct ec_params_i2c_passthru_msg *msg = &params->msg[i];

		msg->len = i2c_msg->len;
		msg->addr_flags = i2c_msg->addr;

		if (i2c_msg->flags & I2C_M_TEN)
			return -EINVAL;

		if (i2c_msg->flags & I2C_M_RD) {
			msg->addr_flags |= EC_I2C_FLAG_READ;
		} else {
			memcpy(out_data, i2c_msg->buf, msg->len);
			out_data += msg->len;
		}
	}

	return 0;
}

/**
 * ec_i2c_count_response - Count bytes needed for ec_i2c_parse_response
 *
 * @i2c_msgs: The i2c messages to fill up.
 * @num: The number of i2c messages expected.
 *
 * Returns the number of response bytes expeced.
 */
static int ec_i2c_count_response(struct i2c_msg i2c_msgs[], int num)
{
	int size;
	int i;

	size = sizeof(struct ec_response_i2c_passthru);
	for (i = 0; i < num; i++)
		if (i2c_msgs[i].flags & I2C_M_RD)
			size += i2c_msgs[i].len;

	return size;
}

/**
 * ec_i2c_parse_response - Parse a response from the EC
 *
 * We'll take the EC's response and copy it back into msgs.
 *
 * @buf: The buffer to parse.
 * @i2c_msgs: The i2c messages to fill up.
 * @num: The number of i2c messages; will be modified to include the actual
 *	 number received.
 *
 * Returns 0 or a negative error number.
 */
static int ec_i2c_parse_response(const u8 *buf, struct i2c_msg i2c_msgs[],
				 int *num)
{
	const struct ec_response_i2c_passthru *resp;
	const u8 *in_data;
	int i;

	in_data = buf + sizeof(struct ec_response_i2c_passthru);

	resp = (const struct ec_response_i2c_passthru *)buf;
	if (resp->i2c_status & EC_I2C_STATUS_TIMEOUT)
		return -ETIMEDOUT;
	else if (resp->i2c_status & EC_I2C_STATUS_NAK)
		return -ENXIO;
	else if (resp->i2c_status & EC_I2C_STATUS_ERROR)
		return -EIO;

	/* Other side could send us back fewer messages, but not more */
	if (resp->num_msgs > *num)
		return -EPROTO;
	*num = resp->num_msgs;

	for (i = 0; i < *num; i++) {
		struct i2c_msg *i2c_msg = &i2c_msgs[i];

		if (i2c_msgs[i].flags & I2C_M_RD) {
			memcpy(i2c_msg->buf, in_data, i2c_msg->len);
			in_data += i2c_msg->len;
		}
	}

	return 0;
}

static int ec_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg i2c_msgs[],
		       int num)
{
	struct ec_i2c_device *bus = adap->algo_data;
	struct device *dev = bus->dev;
	const u16 bus_num = bus->remote_bus;
	int request_len;
	int response_len;
	int alloc_size;
	int result;
	struct cros_ec_command *msg;

	request_len = ec_i2c_count_message(i2c_msgs, num);
	if (request_len < 0) {
		dev_warn(dev, "Error constructing message %d\n", request_len);
		return request_len;
	}

	response_len = ec_i2c_count_response(i2c_msgs, num);
	if (response_len < 0) {
		/* Unexpected; no errors should come when NULL response */
		dev_warn(dev, "Error preparing response %d\n", response_len);
		return response_len;
	}

	alloc_size = max(request_len, response_len);
	msg = kmalloc(sizeof(*msg) + alloc_size, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	result = ec_i2c_construct_message(msg->data, i2c_msgs, num, bus_num);
	if (result) {
		dev_err(dev, "Error constructing EC i2c message %d\n", result);
		goto exit;
	}

	msg->version = 0;
	msg->command = EC_CMD_I2C_PASSTHRU;
	msg->outsize = request_len;
	msg->insize = response_len;

	result = cros_ec_cmd_xfer_status(bus->ec, msg);
	if (result < 0) {
		dev_err(dev, "Error transferring EC i2c message %d\n", result);
		goto exit;
	}

	result = ec_i2c_parse_response(msg->data, i2c_msgs, &num);
	if (result < 0)
		goto exit;

	/* Indicate success by saying how many messages were sent */
	result = num;
exit:
	kfree(msg);
	return result;
}

static u32 ec_i2c_functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm ec_i2c_algorithm = {
	.master_xfer	= ec_i2c_xfer,
	.functionality	= ec_i2c_functionality,
};

static int ec_i2c_probe(struct platform_device *pdev)
{
	struct cros_ec_device *ec = dev_get_drvdata(pdev->dev.parent);
	struct device *dev = &pdev->dev;
	struct ec_i2c_device *bus = NULL;
	u32 remote_bus;
	int err;

	if (!ec->cmd_xfer) {
		dev_err(dev, "Missing sendrecv\n");
		return -EINVAL;
	}

	bus = devm_kzalloc(dev, sizeof(*bus), GFP_KERNEL);
	if (bus == NULL)
		return -ENOMEM;

	err = device_property_read_u32(dev, "google,remote-bus", &remote_bus);
	if (err) {
		dev_err(dev, "Couldn't read remote-bus property\n");
		return err;
	}
	bus->remote_bus = remote_bus;

	bus->ec = ec;
	bus->dev = dev;

	bus->adap.owner = THIS_MODULE;
	strscpy(bus->adap.name, "cros-ec-i2c-tunnel", sizeof(bus->adap.name));
	bus->adap.algo = &ec_i2c_algorithm;
	bus->adap.algo_data = bus;
	bus->adap.dev.parent = &pdev->dev;
	bus->adap.dev.of_node = pdev->dev.of_node;
	bus->adap.retries = I2C_MAX_RETRIES;
	ACPI_COMPANION_SET(&bus->adap.dev, ACPI_COMPANION(&pdev->dev));

	err = i2c_add_adapter(&bus->adap);
	if (err)
		return err;
	platform_set_drvdata(pdev, bus);

	return err;
}

static int ec_i2c_remove(struct platform_device *dev)
{
	struct ec_i2c_device *bus = platform_get_drvdata(dev);

	i2c_del_adapter(&bus->adap);

	return 0;
}

static const struct of_device_id cros_ec_i2c_of_match[] __maybe_unused = {
	{ .compatible = "google,cros-ec-i2c-tunnel" },
	{},
};
MODULE_DEVICE_TABLE(of, cros_ec_i2c_of_match);

static const struct acpi_device_id cros_ec_i2c_tunnel_acpi_id[] __maybe_unused = {
	{ "GOOG0012", 0 },
	{ }
};
MODULE_DEVICE_TABLE(acpi, cros_ec_i2c_tunnel_acpi_id);

static struct platform_driver ec_i2c_tunnel_driver = {
	.probe = ec_i2c_probe,
	.remove = ec_i2c_remove,
	.driver = {
		.name = "cros-ec-i2c-tunnel",
		.acpi_match_table = ACPI_PTR(cros_ec_i2c_tunnel_acpi_id),
		.of_match_table = of_match_ptr(cros_ec_i2c_of_match),
	},
};

module_platform_driver(ec_i2c_tunnel_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("EC I2C tunnel driver");
MODULE_ALIAS("platform:cros-ec-i2c-tunnel");
