// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics lis2dw12 i2c driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2016 STMicroelectronics Inc.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/of.h>

#include "st_lis2dw12.h"

static int st_lis2dw12_i2c_read(struct device *dev, u8 addr, int len, u8 *data)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct i2c_msg msg[2];

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

static int st_lis2dw12_i2c_write(struct device *dev, u8 addr, int len, u8 *data)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct i2c_msg msg;
	u8 send[4];

	if (len >= ARRAY_SIZE(send))
		return -ENOMEM;

	send[0] = addr;
	memcpy(&send[1], data, len * sizeof(u8));

	msg.addr = client->addr;
	msg.flags = client->flags;
	msg.len = len + 1;
	msg.buf = send;

	return i2c_transfer(client->adapter, &msg, 1);
}

static const struct st_lis2dw12_transfer_function st_lis2dw12_transfer_fn = {
	.read = st_lis2dw12_i2c_read,
	.write = st_lis2dw12_i2c_write,
};

static int st_lis2dw12_i2c_probe(struct i2c_client *client,
				 const struct i2c_device_id *id)
{
	return st_lis2dw12_probe(&client->dev, client->irq,
				 &st_lis2dw12_transfer_fn);
}

static const struct of_device_id st_lis2dw12_i2c_of_match[] = {
	{
		.compatible = "st,lis2dw12",
		.data = ST_LIS2DW12_DEV_NAME,
	},
	{
		.compatible = "st,iis2dlpc",
		.data = ST_IIS2DLPC_DEV_NAME,
	},
	{
		.compatible = "st,ais2ih",
		.data = ST_AIS2IH_DEV_NAME,
	},
	{},
};
MODULE_DEVICE_TABLE(of, st_lis2dw12_i2c_of_match);

static const struct i2c_device_id st_lis2dw12_i2c_id_table[] = {
	{ ST_LIS2DW12_DEV_NAME },
	{ ST_AIS2IH_DEV_NAME },
	{},
};
MODULE_DEVICE_TABLE(i2c, st_lis2dw12_i2c_id_table);

static struct i2c_driver st_lis2dw12_driver = {
	.driver = {
		.name = "st_lis2dw12_i2c",
		.of_match_table = of_match_ptr(st_lis2dw12_i2c_of_match),
	},
	.probe = st_lis2dw12_i2c_probe,
	.id_table = st_lis2dw12_i2c_id_table,
};
module_i2c_driver(st_lis2dw12_driver);

MODULE_AUTHOR("MEMS Software Solutions Team");
MODULE_DESCRIPTION("STMicroelectronics st_lis2dw12 i2c driver");
MODULE_LICENSE("GPL v2");
