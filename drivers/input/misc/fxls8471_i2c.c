/*
 *  fxls8471-i2c.c - Linux kernel modules for 3-Axis Smart Orientation
 *  /Motion Sensor
 *  Version		: 01.00
 *  Time		: Dec.26, 2012
 *  Author		: rick zhang <rick.zhang@freescale.com>
 *
 *  Copyright (C) 2010-2011 Freescale Semiconductor.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include "fxls8471.h"

static s32 fxls8471_i2c_write(struct fxls8471_data *pdata, u8 reg, u8 val)
{
	struct i2c_client *client = (struct i2c_client *)pdata->bus_priv;
	return i2c_smbus_write_byte_data(client, reg, val);
}

static int fxls8471_i2c_read(struct fxls8471_data *pdata, u8 reg)
{
	struct i2c_client *client = (struct i2c_client *)pdata->bus_priv;
	return i2c_smbus_read_byte_data(client, reg);
}

static int fxls8471_i2c_read_block(struct fxls8471_data *pdata, u8 reg, u8 len,
				   u8 *val)
{
	struct i2c_client *client = (struct i2c_client *)pdata->bus_priv;
	return i2c_smbus_read_i2c_block_data(client, reg, len, val);
}

static int fxls8471_i2c_probe(struct i2c_client *client,
			      const struct i2c_device_id *id)
{
	fxls8471_dev.bus_priv = client;
	fxls8471_dev.bus_type = BUS_I2C;
	fxls8471_dev.irq = client->irq;
	fxls8471_dev.read = fxls8471_i2c_read;
	fxls8471_dev.write = fxls8471_i2c_write;
	fxls8471_dev.read_block = fxls8471_i2c_read_block;
	i2c_set_clientdata(client, &fxls8471_dev);
	return fxls8471_driver_init(&fxls8471_dev);
}

static int fxls8471_i2c_remove(struct i2c_client *client)
{
	return fxls8471_driver_remove(&fxls8471_dev);
}

#ifdef CONFIG_PM
static int fxls8471_i2c_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	return fxls8471_driver_suspend(i2c_get_clientdata(client));
}

static int fxls8471_i2c_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	return fxls8471_driver_resume(i2c_get_clientdata(client));
}

#else /*  */
#define fxls8471_i2c_suspend	NULL
#define fxls8471_i2c_resume	NULL
#endif /*  */
static const struct i2c_device_id fxls8471_i2c_id[] = {
	{"fxls8471", 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, fxls8471_i2c_id);

static SIMPLE_DEV_PM_OPS(fxls8471_pm_ops, fxls8471_i2c_suspend,
			 fxls8471_i2c_resume);

static struct i2c_driver fxls8471_i2c_driver = {
	.driver = {
		   .name = "fxls8471",
		   .owner = THIS_MODULE,
		   .pm = &fxls8471_pm_ops,
		   },
	.probe = fxls8471_i2c_probe,
	.remove = fxls8471_i2c_remove,
	.id_table = fxls8471_i2c_id,
};

module_i2c_driver(fxls8471_i2c_driver);
MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("FXLS8471 3-Axis Acc Sensor driver");
MODULE_LICENSE("GPL");
