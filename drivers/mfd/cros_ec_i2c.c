/*
 * ChromeOS EC multi-function device (I2C)
 *
 * Copyright (C) 2012 Google, Inc
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/mfd/cros_ec.h>
#include <linux/mfd/cros_ec_commands.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

static inline struct cros_ec_device *to_ec_dev(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);

	return i2c_get_clientdata(client);
}

static int cros_ec_cmd_xfer_i2c(struct cros_ec_device *ec_dev,
				struct cros_ec_msg *msg)
{
	struct i2c_client *client = ec_dev->priv;
	int ret = -ENOMEM;
	int i;
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
	packet_len = msg->in_len + 3;
	in_buf = kzalloc(packet_len, GFP_KERNEL);
	if (!in_buf)
		goto done;
	i2c_msg[1].len = packet_len;
	i2c_msg[1].buf = (char *)in_buf;

	/*
	 * allocate larger packet (one byte for checksum, one for
	 * command code, one for length, and one for command version)
	 */
	packet_len = msg->out_len + 4;
	out_buf = kzalloc(packet_len, GFP_KERNEL);
	if (!out_buf)
		goto done;
	i2c_msg[0].len = packet_len;
	i2c_msg[0].buf = (char *)out_buf;

	out_buf[0] = EC_CMD_VERSION0 + msg->version;
	out_buf[1] = msg->cmd;
	out_buf[2] = msg->out_len;

	/* copy message payload and compute checksum */
	sum = out_buf[0] + out_buf[1] + out_buf[2];
	for (i = 0; i < msg->out_len; i++) {
		out_buf[3 + i] = msg->out_buf[i];
		sum += out_buf[3 + i];
	}
	out_buf[3 + msg->out_len] = sum;

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
	if (i2c_msg[1].buf[0]) {
		dev_warn(ec_dev->dev, "command 0x%02x returned an error %d\n",
			 msg->cmd, i2c_msg[1].buf[0]);
		ret = -EINVAL;
		goto done;
	}

	/* copy response packet payload and compute checksum */
	sum = in_buf[0] + in_buf[1];
	for (i = 0; i < msg->in_len; i++) {
		msg->in_buf[i] = in_buf[2 + i];
		sum += in_buf[2 + i];
	}
	dev_dbg(ec_dev->dev, "packet: %*ph, sum = %02x\n",
		i2c_msg[1].len, in_buf, sum);
	if (sum != in_buf[2 + msg->in_len]) {
		dev_err(ec_dev->dev, "bad packet checksum\n");
		ret = -EBADMSG;
		goto done;
	}

	ret = 0;
 done:
	kfree(in_buf);
	kfree(out_buf);
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
	ec_dev->name = "I2C";
	ec_dev->dev = dev;
	ec_dev->priv = client;
	ec_dev->irq = client->irq;
	ec_dev->cmd_xfer = cros_ec_cmd_xfer_i2c;
	ec_dev->ec_name = client->name;
	ec_dev->phys_name = client->adapter->name;
	ec_dev->parent = &client->dev;

	err = cros_ec_register(ec_dev);
	if (err) {
		dev_err(dev, "cannot register EC\n");
		return err;
	}

	return 0;
}

static int cros_ec_i2c_remove(struct i2c_client *client)
{
	struct cros_ec_device *ec_dev = i2c_get_clientdata(client);

	cros_ec_remove(ec_dev);

	return 0;
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

static SIMPLE_DEV_PM_OPS(cros_ec_i2c_pm_ops, cros_ec_i2c_suspend,
			  cros_ec_i2c_resume);

static const struct i2c_device_id cros_ec_i2c_id[] = {
	{ "cros-ec-i2c", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, cros_ec_i2c_id);

static struct i2c_driver cros_ec_driver = {
	.driver	= {
		.name	= "cros-ec-i2c",
		.owner	= THIS_MODULE,
		.pm	= &cros_ec_i2c_pm_ops,
	},
	.probe		= cros_ec_i2c_probe,
	.remove		= cros_ec_i2c_remove,
	.id_table	= cros_ec_i2c_id,
};

module_i2c_driver(cros_ec_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ChromeOS EC multi function device");
