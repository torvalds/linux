// SPDX-License-Identifier: GPL-2.0-only
/*
 * Implements I2C interface for VTI CMA300_D0x Accelerometer driver
 *
 * Copyright (C) 2010 Texas Instruments
 * Author: Hemanth V <hemanthv@ti.com>
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/input/cma3000.h>
#include "cma3000_d0x.h"

static int cma3000_i2c_set(struct device *dev,
			   u8 reg, u8 val, char *msg)
{
	struct i2c_client *client = to_i2c_client(dev);
	int ret;

	ret = i2c_smbus_write_byte_data(client, reg, val);
	if (ret < 0)
		dev_err(&client->dev,
			"%s failed (%s, %d)\n", __func__, msg, ret);
	return ret;
}

static int cma3000_i2c_read(struct device *dev, u8 reg, char *msg)
{
	struct i2c_client *client = to_i2c_client(dev);
	int ret;

	ret = i2c_smbus_read_byte_data(client, reg);
	if (ret < 0)
		dev_err(&client->dev,
			"%s failed (%s, %d)\n", __func__, msg, ret);
	return ret;
}

static const struct cma3000_bus_ops cma3000_i2c_bops = {
	.bustype	= BUS_I2C,
#define CMA3000_BUSI2C     (0 << 4)
	.ctrl_mod	= CMA3000_BUSI2C,
	.read		= cma3000_i2c_read,
	.write		= cma3000_i2c_set,
};

static int cma3000_i2c_probe(struct i2c_client *client,
					const struct i2c_device_id *id)
{
	struct cma3000_accl_data *data;

	data = cma3000_init(&client->dev, client->irq, &cma3000_i2c_bops);
	if (IS_ERR(data))
		return PTR_ERR(data);

	i2c_set_clientdata(client, data);

	return 0;
}

static void cma3000_i2c_remove(struct i2c_client *client)
{
	struct cma3000_accl_data *data = i2c_get_clientdata(client);

	cma3000_exit(data);
}

#ifdef CONFIG_PM
static int cma3000_i2c_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct cma3000_accl_data *data = i2c_get_clientdata(client);

	cma3000_suspend(data);

	return 0;
}

static int cma3000_i2c_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct cma3000_accl_data *data = i2c_get_clientdata(client);

	cma3000_resume(data);

	return 0;
}

static const struct dev_pm_ops cma3000_i2c_pm_ops = {
	.suspend	= cma3000_i2c_suspend,
	.resume		= cma3000_i2c_resume,
};
#endif

static const struct i2c_device_id cma3000_i2c_id[] = {
	{ "cma3000_d01", 0 },
	{ },
};

MODULE_DEVICE_TABLE(i2c, cma3000_i2c_id);

static struct i2c_driver cma3000_i2c_driver = {
	.probe		= cma3000_i2c_probe,
	.remove		= cma3000_i2c_remove,
	.id_table	= cma3000_i2c_id,
	.driver = {
		.name	= "cma3000_i2c_accl",
#ifdef CONFIG_PM
		.pm	= &cma3000_i2c_pm_ops,
#endif
	},
};

module_i2c_driver(cma3000_i2c_driver);

MODULE_DESCRIPTION("CMA3000-D0x Accelerometer I2C Driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Hemanth V <hemanthv@ti.com>");
