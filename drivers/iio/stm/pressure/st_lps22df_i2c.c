// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics lps22df i2c driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2021 STMicroelectronics Inc.
 */

#include <linux/i2c.h>

#include "st_lps22df.h"

static int st_lps22df_i2c_read(struct device *dev, u8 addr, int len, u8 *data)
{
	struct st_lps22df_hw *hw = dev_get_drvdata(dev);
	struct i2c_client *client = to_i2c_client(dev);
	struct i2c_msg msg[2];
	int ret;

	if (len >= ST_LPS22DF_RX_MAX_LENGTH)
		return -ENOMEM;

	msg[0].addr = client->addr;
	msg[0].flags = client->flags;
	msg[0].len = 1;
	msg[0].buf = &addr;

	msg[1].addr = client->addr;
	msg[1].flags = client->flags | I2C_M_RD;
	msg[1].len = len;
	msg[1].buf = hw->tb.rx_buf;

	ret = i2c_transfer(client->adapter, msg, 2);
	if (ret < 0)
		return ret;

	memcpy(data, hw->tb.rx_buf, len * sizeof(u8));

	return 0;
}

static int st_lps22df_i2c_write(struct device *dev, u8 addr, int len, u8 *data)
{
	struct st_lps22df_hw *hw = dev_get_drvdata(dev);
	struct i2c_client *client = to_i2c_client(dev);
	struct i2c_msg msg;
	int ret;

	if (len >= ST_LPS22DF_TX_MAX_LENGTH)
		return -ENOMEM;

	hw->tb.tx_buf[0] = addr;
	memcpy(&hw->tb.tx_buf[1], data, len);

	msg.addr = client->addr;
	msg.flags = client->flags;
	msg.len = len + 1;
	msg.buf = hw->tb.tx_buf;

	ret = i2c_transfer(client->adapter, &msg, 1);

	return ret < 0 ? ret : 0;
}

static const struct st_lps22df_transfer_function st_lps22df_tf_i2c = {
	.write = st_lps22df_i2c_write,
	.read = st_lps22df_i2c_read,
};

static int st_lps22df_i2c_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	int hw_id = id->driver_data;
	return st_lps22df_common_probe(&client->dev, client->irq,
				       hw_id, &st_lps22df_tf_i2c);
}

static const struct i2c_device_id st_lps22df_ids[] = {
	{ "lps22df", ST_LPS22DF_ID },
	{ "lps28dfw", ST_LPS28DFW_ID },
	{}
};
MODULE_DEVICE_TABLE(i2c, st_lps22df_ids);

static const struct of_device_id st_lps22df_id_table[] = {
	{
		.compatible = "st,lps22df",
		.data = (void *)ST_LPS22DF_ID,
	},
	{
		.compatible = "st,lps28dfw",
		.data = (void *)ST_LPS28DFW_ID,
	},
	{},
};
MODULE_DEVICE_TABLE(of, st_lps22df_id_table);

static struct i2c_driver st_lps22df_i2c_driver = {
	.driver = {
		   .owner = THIS_MODULE,
		   .name = "st_lps22df_i2c",
		   .of_match_table = of_match_ptr(st_lps22df_id_table),
	},
	.probe = st_lps22df_i2c_probe,
	.id_table = st_lps22df_ids,
};
module_i2c_driver(st_lps22df_i2c_driver);

MODULE_DESCRIPTION("STMicroelectronics lps22df i2c driver");
MODULE_AUTHOR("MEMS Software Solutions Team");
MODULE_LICENSE("GPL v2");
