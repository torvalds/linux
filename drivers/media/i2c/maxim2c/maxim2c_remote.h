/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2023 Rockchip Electronics Co., Ltd.
 *
 */

#ifndef __MAXIM2C_REMOTE_H__
#define __MAXIM2C_REMOTE_H__

#include "maxim2c_i2c.h"

struct maxim2c_remote;

struct maxim2c_remote_ops {
	int (*remote_init)(struct maxim2c_remote *remote);
	int (*remote_deinit)(struct maxim2c_remote *remote);
};

typedef struct maxim2c_remote {
	struct i2c_client *client;
	struct device *dev;
	void *local;
	const struct maxim2c_remote_ops *remote_ops;
	struct maxim2c_i2c_init_seq remote_init_seq;

	u8 remote_id;
	u8 remote_enable;

	u8 ser_i2c_addr_def;
	u8 ser_i2c_addr_map;

	u8 cam_i2c_addr_def;
	u8 cam_i2c_addr_map;
} maxim2c_remote_t;

#endif /* __MAXIM2C_REMOTE_H__ */
