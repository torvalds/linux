/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2021-2023 Digiteq Automotive
 *     author: Martin Tuma <martin.tuma@digiteqautomotive.com>
 */

#ifndef __MGB4_I2C_H__
#define __MGB4_I2C_H__

#include <linux/i2c.h>

struct mgb4_i2c_client {
	struct i2c_client *client;
	int addr_size;
};

struct mgb4_i2c_kv {
	u16 reg;
	u8 mask;
	u8 val;
};

int mgb4_i2c_init(struct mgb4_i2c_client *client, struct i2c_adapter *adap,
		  struct i2c_board_info const *info, int addr_size);
void mgb4_i2c_free(struct mgb4_i2c_client *client);

s32 mgb4_i2c_read_byte(struct mgb4_i2c_client *client, u16 reg);
s32 mgb4_i2c_write_byte(struct mgb4_i2c_client *client, u16 reg, u8 val);
s32 mgb4_i2c_mask_byte(struct mgb4_i2c_client *client, u16 reg, u8 mask,
		       u8 val);

int mgb4_i2c_configure(struct mgb4_i2c_client *client,
		       const struct mgb4_i2c_kv *values, size_t count);

#endif
