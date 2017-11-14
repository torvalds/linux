/*
 * STMicroelectronics hts221 i2c driver
 *
 * Copyright 2016 STMicroelectronics Inc.
 *
 * Lorenzo Bianconi <lorenzo.bianconi@st.com>
 *
 * Licensed under the GPL-2.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/acpi.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include "hts221.h"

#define I2C_AUTO_INCREMENT	0x80

static int hts221_i2c_read(struct device *dev, u8 addr, int len, u8 *data)
{
	struct i2c_msg msg[2];
	struct i2c_client *client = to_i2c_client(dev);

	if (len > 1)
		addr |= I2C_AUTO_INCREMENT;

	msg[0].addr = client->addr;
	msg[0].flags = client->flags;
	msg[0].len = 1;
	msg[0].buf = &addr;

	msg[1].addr = client->addr;
	msg[1].flags = client->flags | I2C_M_RD;
	msg[1].len = len;
	msg[1].buf = data;

	return i2c_transfer(client->adapter, msg, 2);
}

static int hts221_i2c_write(struct device *dev, u8 addr, int len, u8 *data)
{
	u8 send[len + 1];
	struct i2c_msg msg;
	struct i2c_client *client = to_i2c_client(dev);

	if (len > 1)
		addr |= I2C_AUTO_INCREMENT;

	send[0] = addr;
	memcpy(&send[1], data, len * sizeof(u8));

	msg.addr = client->addr;
	msg.flags = client->flags;
	msg.len = len + 1;
	msg.buf = send;

	return i2c_transfer(client->adapter, &msg, 1);
}

static const struct hts221_transfer_function hts221_transfer_fn = {
	.read = hts221_i2c_read,
	.write = hts221_i2c_write,
};

static int hts221_i2c_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
{
	struct hts221_hw *hw;
	struct iio_dev *iio_dev;

	iio_dev = devm_iio_device_alloc(&client->dev, sizeof(*hw));
	if (!iio_dev)
		return -ENOMEM;

	i2c_set_clientdata(client, iio_dev);

	hw = iio_priv(iio_dev);
	hw->name = client->name;
	hw->dev = &client->dev;
	hw->irq = client->irq;
	hw->tf = &hts221_transfer_fn;

	return hts221_probe(iio_dev);
}

static const struct acpi_device_id hts221_acpi_match[] = {
	{"SMO9100", 0},
	{ },
};
MODULE_DEVICE_TABLE(acpi, hts221_acpi_match);

static const struct of_device_id hts221_i2c_of_match[] = {
	{ .compatible = "st,hts221", },
	{},
};
MODULE_DEVICE_TABLE(of, hts221_i2c_of_match);

static const struct i2c_device_id hts221_i2c_id_table[] = {
	{ HTS221_DEV_NAME },
	{},
};
MODULE_DEVICE_TABLE(i2c, hts221_i2c_id_table);

static struct i2c_driver hts221_driver = {
	.driver = {
		.name = "hts221_i2c",
		.pm = &hts221_pm_ops,
		.of_match_table = of_match_ptr(hts221_i2c_of_match),
		.acpi_match_table = ACPI_PTR(hts221_acpi_match),
	},
	.probe = hts221_i2c_probe,
	.id_table = hts221_i2c_id_table,
};
module_i2c_driver(hts221_driver);

MODULE_AUTHOR("Lorenzo Bianconi <lorenzo.bianconi@st.com>");
MODULE_DESCRIPTION("STMicroelectronics hts221 i2c driver");
MODULE_LICENSE("GPL v2");
