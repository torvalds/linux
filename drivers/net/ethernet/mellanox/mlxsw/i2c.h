/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/* Copyright (c) 2016-2018 Mellanox Technologies. All rights reserved */

#ifndef _MLXSW_I2C_H
#define _MLXSW_I2C_H

#include <linux/i2c.h>

#if IS_ENABLED(CONFIG_MLXSW_I2C)

int mlxsw_i2c_driver_register(struct i2c_driver *i2c_driver);
void mlxsw_i2c_driver_unregister(struct i2c_driver *i2c_driver);

#else

static inline int
mlxsw_i2c_driver_register(struct i2c_driver *i2c_driver)
{
	return -ENODEV;
}

static inline void
mlxsw_i2c_driver_unregister(struct i2c_driver *i2c_driver)
{
}

#endif

#endif
