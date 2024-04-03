// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021-2023 Digiteq Automotive
 *     author: Martin Tuma <martin.tuma@digiteqautomotive.com>
 *
 * The i2c module unifies the I2C access to the serializes/deserializes. The I2C
 * chips on the GMSL module use 16b addressing, the FPDL3 chips use standard
 * 8b addressing.
 */

#include "mgb4_i2c.h"

static int read_r16(struct i2c_client *client, u16 reg, u8 *val, int len)
{
	int ret;
	u8 buf[2];
	struct i2c_msg msg[2] = {
		{
			.addr = client->addr,
			.flags = 0,
			.len = 2,
			.buf = buf,
		}, {
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = len,
			.buf = val,
		}
	};

	buf[0] = (reg >> 8) & 0xff;
	buf[1] = (reg >> 0) & 0xff;

	ret = i2c_transfer(client->adapter, msg, 2);
	if (ret < 0)
		return ret;
	else if (ret != 2)
		return -EREMOTEIO;
	else
		return 0;
}

static int write_r16(struct i2c_client *client, u16 reg, const u8 *val, int len)
{
	int ret;
	u8 buf[4];
	struct i2c_msg msg[1] = {
		{
			.addr = client->addr,
			.flags = 0,
			.len = 2 + len,
			.buf = buf,
		}
	};

	if (2 + len > sizeof(buf))
		return -EINVAL;

	buf[0] = (reg >> 8) & 0xff;
	buf[1] = (reg >> 0) & 0xff;
	memcpy(&buf[2], val, len);

	ret = i2c_transfer(client->adapter, msg, 1);
	if (ret < 0)
		return ret;
	else if (ret != 1)
		return -EREMOTEIO;
	else
		return 0;
}

int mgb4_i2c_init(struct mgb4_i2c_client *client, struct i2c_adapter *adap,
		  struct i2c_board_info const *info, int addr_size)
{
	client->client = i2c_new_client_device(adap, info);
	if (IS_ERR(client->client))
		return PTR_ERR(client->client);

	client->addr_size = addr_size;

	return 0;
}

void mgb4_i2c_free(struct mgb4_i2c_client *client)
{
	i2c_unregister_device(client->client);
}

s32 mgb4_i2c_read_byte(struct mgb4_i2c_client *client, u16 reg)
{
	int ret;
	u8 b;

	if (client->addr_size == 8)
		return i2c_smbus_read_byte_data(client->client, reg);

	ret = read_r16(client->client, reg, &b, 1);
	if (ret < 0)
		return ret;

	return (s32)b;
}

s32 mgb4_i2c_write_byte(struct mgb4_i2c_client *client, u16 reg, u8 val)
{
	if (client->addr_size == 8)
		return i2c_smbus_write_byte_data(client->client, reg, val);
	else
		return write_r16(client->client, reg, &val, 1);
}

s32 mgb4_i2c_mask_byte(struct mgb4_i2c_client *client, u16 reg, u8 mask, u8 val)
{
	s32 ret;

	if (mask != 0xFF) {
		ret = mgb4_i2c_read_byte(client, reg);
		if (ret < 0)
			return ret;
		val |= (u8)ret & ~mask;
	}

	return mgb4_i2c_write_byte(client, reg, val);
}

int mgb4_i2c_configure(struct mgb4_i2c_client *client,
		       const struct mgb4_i2c_kv *values, size_t count)
{
	size_t i;
	s32 res;

	for (i = 0; i < count; i++) {
		res = mgb4_i2c_mask_byte(client, values[i].reg, values[i].mask,
					 values[i].val);
		if (res < 0)
			return res;
	}

	return 0;
}
