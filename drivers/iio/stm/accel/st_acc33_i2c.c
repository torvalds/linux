// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics st_acc33 i2c driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2016 STMicroelectronics Inc.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/slab.h>

#include "st_acc33.h"

#define I2C_AUTO_INCREMENT	BIT(7)

static int st_acc33_i2c_read(struct device *dev, u8 addr, int len, u8 *data)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct i2c_msg msg[2];

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

static int st_acc33_i2c_write(struct device *dev, u8 addr, int len, u8 *data)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct i2c_msg msg;
	u8 send[4];

	if (len >= ARRAY_SIZE(send))
		return -ENOMEM;

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

static const struct st_acc33_transfer_function st_acc33_transfer_fn = {
	.read = st_acc33_i2c_read,
	.write = st_acc33_i2c_write,
};

static int st_acc33_i2c_probe(struct i2c_client *client,
			      const struct i2c_device_id *id)
{
	return st_acc33_probe(&client->dev, client->irq, client->name,
			      &st_acc33_transfer_fn);
}

static const struct of_device_id st_acc33_i2c_of_match[] = {
	{
		.compatible = "st,lis2dh_accel",
		.data = LIS2DH_DEV_NAME,
	},
	{
		.compatible = "st,lis2dh12_accel",
		.data = LIS2DH12_DEV_NAME,
	},
	{
		.compatible = "st,lis3dh_accel",
		.data = LIS3DH_DEV_NAME,
	},
	{
		.compatible = "st,lsm303agr_accel",
		.data = LSM303AGR_DEV_NAME,
	},
	{
		.compatible = "st,iis2dh_accel",
		.data = IIS2DH_DEV_NAME,
	},
	{},
};
MODULE_DEVICE_TABLE(of, st_acc33_i2c_of_match);

static const struct i2c_device_id st_acc33_i2c_id_table[] = {
	{ LIS2DH_DEV_NAME },
	{ LIS2DH12_DEV_NAME },
	{ LIS3DH_DEV_NAME },
	{ LSM303AGR_DEV_NAME },
	{ IIS2DH_DEV_NAME },
	{},
};
MODULE_DEVICE_TABLE(i2c, st_acc33_i2c_id_table);

static struct i2c_driver st_acc33_driver = {
	.driver = {
		.name = "st_acc33_i2c",
		.of_match_table = of_match_ptr(st_acc33_i2c_of_match),
	},
	.probe = st_acc33_i2c_probe,
	.id_table = st_acc33_i2c_id_table,
};
module_i2c_driver(st_acc33_driver);

MODULE_AUTHOR("MEMS Software Solutions Team");
MODULE_DESCRIPTION("STMicroelectronics st_acc33 i2c driver");
MODULE_LICENSE("GPL v2");
