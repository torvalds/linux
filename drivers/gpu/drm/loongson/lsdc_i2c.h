/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2023 Loongson Technology Corporation Limited
 */

#ifndef __LSDC_I2C_H__
#define __LSDC_I2C_H__

#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>

struct lsdc_i2c {
	struct i2c_adapter adapter;
	struct i2c_algo_bit_data bit;
	struct drm_device *ddev;
	void __iomem *dir_reg;
	void __iomem *dat_reg;
	/* pin bit mask */
	u8 sda;
	u8 scl;
};

struct lsdc_display_pipe;

int lsdc_create_i2c_chan(struct drm_device *ddev,
			 struct lsdc_display_pipe *dispipe,
			 unsigned int index);

#endif
