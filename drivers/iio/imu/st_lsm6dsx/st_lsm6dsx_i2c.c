/*
 * STMicroelectronics st_lsm6dsx i2c driver
 *
 * Copyright 2016 STMicroelectronics Inc.
 *
 * Lorenzo Bianconi <lorenzo.bianconi@st.com>
 * Denis Ciocca <denis.ciocca@st.com>
 *
 * Licensed under the GPL-2.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/of.h>

#include "st_lsm6dsx.h"

static int st_lsm6dsx_i2c_read(struct device *dev, u8 addr, int len, u8 *data)
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

static int st_lsm6dsx_i2c_write(struct device *dev, u8 addr, int len, u8 *data)
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

static const struct st_lsm6dsx_transfer_function st_lsm6dsx_transfer_fn = {
	.read = st_lsm6dsx_i2c_read,
	.write = st_lsm6dsx_i2c_write,
};

static int st_lsm6dsx_i2c_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	return st_lsm6dsx_probe(&client->dev, client->irq,
				(int)id->driver_data, id->name,
				&st_lsm6dsx_transfer_fn);
}

static const struct of_device_id st_lsm6dsx_i2c_of_match[] = {
	{
		.compatible = "st,lsm6ds3",
		.data = (void *)ST_LSM6DS3_ID,
	},
	{
		.compatible = "st,lsm6ds3h",
		.data = (void *)ST_LSM6DS3H_ID,
	},
	{
		.compatible = "st,lsm6dsl",
		.data = (void *)ST_LSM6DSL_ID,
	},
	{
		.compatible = "st,lsm6dsm",
		.data = (void *)ST_LSM6DSM_ID,
	},
	{},
};
MODULE_DEVICE_TABLE(of, st_lsm6dsx_i2c_of_match);

static const struct i2c_device_id st_lsm6dsx_i2c_id_table[] = {
	{ ST_LSM6DS3_DEV_NAME, ST_LSM6DS3_ID },
	{ ST_LSM6DS3H_DEV_NAME, ST_LSM6DS3H_ID },
	{ ST_LSM6DSL_DEV_NAME, ST_LSM6DSL_ID },
	{ ST_LSM6DSM_DEV_NAME, ST_LSM6DSM_ID },
	{},
};
MODULE_DEVICE_TABLE(i2c, st_lsm6dsx_i2c_id_table);

static struct i2c_driver st_lsm6dsx_driver = {
	.driver = {
		.name = "st_lsm6dsx_i2c",
		.pm = &st_lsm6dsx_pm_ops,
		.of_match_table = of_match_ptr(st_lsm6dsx_i2c_of_match),
	},
	.probe = st_lsm6dsx_i2c_probe,
	.id_table = st_lsm6dsx_i2c_id_table,
};
module_i2c_driver(st_lsm6dsx_driver);

MODULE_AUTHOR("Lorenzo Bianconi <lorenzo.bianconi@st.com>");
MODULE_AUTHOR("Denis Ciocca <denis.ciocca@st.com>");
MODULE_DESCRIPTION("STMicroelectronics st_lsm6dsx i2c driver");
MODULE_LICENSE("GPL v2");
