/*
 * MDIO I2C bridge
 *
 * Copyright (C) 2015 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef MDIO_I2C_H
#define MDIO_I2C_H

struct device;
struct i2c_adapter;
struct mii_bus;

struct mii_bus *mdio_i2c_alloc(struct device *parent, struct i2c_adapter *i2c);

#endif
