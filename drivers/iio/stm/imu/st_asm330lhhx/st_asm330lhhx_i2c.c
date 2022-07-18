/*
 * STMicroelectronics st_asm330lhhx i2c driver
 *
 * Copyright 2019 STMicroelectronics Inc.
 *
 * Lorenzo Bianconi <lorenzo.bianconi@st.com>
 * Mario Tesi <mario.tesi@st.com>
 *
 * Licensed under the GPL-2.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/of.h>

#include "st_asm330lhhx.h"

static int st_asm330lhhx_i2c_read(struct device *dev, u8 addr, int len, u8 *data)
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

static int st_asm330lhhx_i2c_write(struct device *dev, u8 addr, int len,
				const u8 *data)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct i2c_msg msg;
	u8 send[len + 1];

	send[0] = addr;
	memcpy(&send[1], data, len * sizeof(u8));

	msg.addr = client->addr;
	msg.flags = client->flags;
	msg.len = len + 1;
	msg.buf = send;

	return i2c_transfer(client->adapter, &msg, 1);
}

static const struct st_asm330lhhx_transfer_function st_asm330lhhx_transfer_fn = {
	.read = st_asm330lhhx_i2c_read,
	.write = st_asm330lhhx_i2c_write,
};

static int st_asm330lhhx_i2c_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	int hw_id = id->driver_data;

	return st_asm330lhhx_probe(&client->dev, client->irq, hw_id,
				   &st_asm330lhhx_transfer_fn);
}

static const struct of_device_id st_asm330lhhx_i2c_of_match[] = {
	{
		.compatible = "st,asm330lhhx",
		.data = (void *)ST_ASM330LHHX_ID,
	},
	{
		.compatible = "st,asm330lhh",
		.data = (void *)ST_ASM330LHH_ID,
	},
	{},
};
MODULE_DEVICE_TABLE(of, st_asm330lhhx_i2c_of_match);

static const struct i2c_device_id st_asm330lhhx_i2c_id_table[] = {
	{ ST_ASM330LHHX_DEV_NAME, ST_ASM330LHHX_ID },
	{ ST_ASM330LHH_DEV_NAME , ST_ASM330LHH_ID },
	{},
};
MODULE_DEVICE_TABLE(i2c, st_asm330lhhx_i2c_id_table);

static struct i2c_driver st_asm330lhhx_driver = {
	.driver = {
		.name = "st_asm330lhhx_i2c",
		.pm = &st_asm330lhhx_pm_ops,
		.of_match_table = st_asm330lhhx_i2c_of_match,
	},
	.probe = st_asm330lhhx_i2c_probe,
	.id_table = st_asm330lhhx_i2c_id_table,
};
module_i2c_driver(st_asm330lhhx_driver);

MODULE_AUTHOR("Lorenzo Bianconi <lorenzo.bianconi@st.com>");
MODULE_AUTHOR("Mario Tesi <mario.tesi@st.com>");
MODULE_DESCRIPTION("STMicroelectronics st_asm330lhhx i2c driver");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(ST_ASM330LHHX_DRV_VERSION);
