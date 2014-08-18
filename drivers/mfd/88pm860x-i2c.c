/*
 * I2C driver for Marvell 88PM860x
 *
 * Copyright (C) 2009 Marvell International Ltd.
 *
 * Author: Haojian Zhuang <haojian.zhuang@marvell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/mfd/88pm860x.h>

int pm860x_reg_read(struct i2c_client *i2c, int reg)
{
	struct pm860x_chip *chip = i2c_get_clientdata(i2c);
	struct regmap *map = (i2c == chip->client) ? chip->regmap
				: chip->regmap_companion;
	unsigned int data;
	int ret;

	ret = regmap_read(map, reg, &data);
	if (ret < 0)
		return ret;
	else
		return (int)data;
}
EXPORT_SYMBOL(pm860x_reg_read);

int pm860x_reg_write(struct i2c_client *i2c, int reg,
		     unsigned char data)
{
	struct pm860x_chip *chip = i2c_get_clientdata(i2c);
	struct regmap *map = (i2c == chip->client) ? chip->regmap
				: chip->regmap_companion;
	int ret;

	ret = regmap_write(map, reg, data);
	return ret;
}
EXPORT_SYMBOL(pm860x_reg_write);

int pm860x_bulk_read(struct i2c_client *i2c, int reg,
		     int count, unsigned char *buf)
{
	struct pm860x_chip *chip = i2c_get_clientdata(i2c);
	struct regmap *map = (i2c == chip->client) ? chip->regmap
				: chip->regmap_companion;
	int ret;

	ret = regmap_raw_read(map, reg, buf, count);
	return ret;
}
EXPORT_SYMBOL(pm860x_bulk_read);

int pm860x_bulk_write(struct i2c_client *i2c, int reg,
		      int count, unsigned char *buf)
{
	struct pm860x_chip *chip = i2c_get_clientdata(i2c);
	struct regmap *map = (i2c == chip->client) ? chip->regmap
				: chip->regmap_companion;
	int ret;

	ret = regmap_raw_write(map, reg, buf, count);
	return ret;
}
EXPORT_SYMBOL(pm860x_bulk_write);

int pm860x_set_bits(struct i2c_client *i2c, int reg,
		    unsigned char mask, unsigned char data)
{
	struct pm860x_chip *chip = i2c_get_clientdata(i2c);
	struct regmap *map = (i2c == chip->client) ? chip->regmap
				: chip->regmap_companion;
	int ret;

	ret = regmap_update_bits(map, reg, mask, data);
	return ret;
}
EXPORT_SYMBOL(pm860x_set_bits);

static int read_device(struct i2c_client *i2c, int reg,
		       int bytes, void *dest)
{
	unsigned char msgbuf0[I2C_SMBUS_BLOCK_MAX + 3];
	unsigned char msgbuf1[I2C_SMBUS_BLOCK_MAX + 2];
	struct i2c_adapter *adap = i2c->adapter;
	struct i2c_msg msg[2] = {
					{
						.addr = i2c->addr,
						.flags = 0,
						.len = 1,
						.buf = msgbuf0
					},
					{	.addr = i2c->addr,
						.flags = I2C_M_RD,
						.len = 0,
						.buf = msgbuf1
					},
				};
	int num = 1, ret = 0;

	if (dest == NULL)
		return -EINVAL;
	msgbuf0[0] = (unsigned char)reg;	/* command */
	msg[1].len = bytes;

	/* if data needs to read back, num should be 2 */
	if (bytes > 0)
		num = 2;
	ret = adap->algo->master_xfer(adap, msg, num);
	memcpy(dest, msgbuf1, bytes);
	if (ret < 0)
		return ret;
	return 0;
}

static int write_device(struct i2c_client *i2c, int reg,
			int bytes, void *src)
{
	unsigned char buf[bytes + 1];
	struct i2c_adapter *adap = i2c->adapter;
	struct i2c_msg msg;
	int ret;

	buf[0] = (unsigned char)reg;
	memcpy(&buf[1], src, bytes);
	msg.addr = i2c->addr;
	msg.flags = 0;
	msg.len = bytes + 1;
	msg.buf = buf;

	ret = adap->algo->master_xfer(adap, &msg, 1);
	if (ret < 0)
		return ret;
	return 0;
}

int pm860x_page_reg_read(struct i2c_client *i2c, int reg)
{
	unsigned char zero = 0;
	unsigned char data;
	int ret;

	i2c_lock_adapter(i2c->adapter);
	read_device(i2c, 0xFA, 0, &zero);
	read_device(i2c, 0xFB, 0, &zero);
	read_device(i2c, 0xFF, 0, &zero);
	ret = read_device(i2c, reg, 1, &data);
	if (ret >= 0)
		ret = (int)data;
	read_device(i2c, 0xFE, 0, &zero);
	read_device(i2c, 0xFC, 0, &zero);
	i2c_unlock_adapter(i2c->adapter);
	return ret;
}
EXPORT_SYMBOL(pm860x_page_reg_read);

int pm860x_page_reg_write(struct i2c_client *i2c, int reg,
			  unsigned char data)
{
	unsigned char zero;
	int ret;

	i2c_lock_adapter(i2c->adapter);
	read_device(i2c, 0xFA, 0, &zero);
	read_device(i2c, 0xFB, 0, &zero);
	read_device(i2c, 0xFF, 0, &zero);
	ret = write_device(i2c, reg, 1, &data);
	read_device(i2c, 0xFE, 0, &zero);
	read_device(i2c, 0xFC, 0, &zero);
	i2c_unlock_adapter(i2c->adapter);
	return ret;
}
EXPORT_SYMBOL(pm860x_page_reg_write);

int pm860x_page_bulk_read(struct i2c_client *i2c, int reg,
			  int count, unsigned char *buf)
{
	unsigned char zero = 0;
	int ret;

	i2c_lock_adapter(i2c->adapter);
	read_device(i2c, 0xfa, 0, &zero);
	read_device(i2c, 0xfb, 0, &zero);
	read_device(i2c, 0xff, 0, &zero);
	ret = read_device(i2c, reg, count, buf);
	read_device(i2c, 0xFE, 0, &zero);
	read_device(i2c, 0xFC, 0, &zero);
	i2c_unlock_adapter(i2c->adapter);
	return ret;
}
EXPORT_SYMBOL(pm860x_page_bulk_read);

int pm860x_page_bulk_write(struct i2c_client *i2c, int reg,
			   int count, unsigned char *buf)
{
	unsigned char zero = 0;
	int ret;

	i2c_lock_adapter(i2c->adapter);
	read_device(i2c, 0xFA, 0, &zero);
	read_device(i2c, 0xFB, 0, &zero);
	read_device(i2c, 0xFF, 0, &zero);
	ret = write_device(i2c, reg, count, buf);
	read_device(i2c, 0xFE, 0, &zero);
	read_device(i2c, 0xFC, 0, &zero);
	i2c_unlock_adapter(i2c->adapter);
	i2c_unlock_adapter(i2c->adapter);
	return ret;
}
EXPORT_SYMBOL(pm860x_page_bulk_write);

int pm860x_page_set_bits(struct i2c_client *i2c, int reg,
			 unsigned char mask, unsigned char data)
{
	unsigned char zero;
	unsigned char value;
	int ret;

	i2c_lock_adapter(i2c->adapter);
	read_device(i2c, 0xFA, 0, &zero);
	read_device(i2c, 0xFB, 0, &zero);
	read_device(i2c, 0xFF, 0, &zero);
	ret = read_device(i2c, reg, 1, &value);
	if (ret < 0)
		goto out;
	value &= ~mask;
	value |= data;
	ret = write_device(i2c, reg, 1, &value);
out:
	read_device(i2c, 0xFE, 0, &zero);
	read_device(i2c, 0xFC, 0, &zero);
	i2c_unlock_adapter(i2c->adapter);
	return ret;
}
EXPORT_SYMBOL(pm860x_page_set_bits);
