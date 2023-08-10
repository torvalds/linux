// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * wm831x-i2c.c  --  I2C access for Wolfson WM831x PMICs
 *
 * Copyright 2009,2010 Wolfson Microelectronics PLC.
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/mfd/core.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regmap.h>

#include <linux/mfd/wm831x/core.h>
#include <linux/mfd/wm831x/pdata.h>

static int wm831x_i2c_probe(struct i2c_client *i2c)
{
	const struct i2c_device_id *id = i2c_client_get_device_id(i2c);
	struct wm831x_pdata *pdata = dev_get_platdata(&i2c->dev);
	const struct of_device_id *of_id;
	struct wm831x *wm831x;
	enum wm831x_parent type;
	int ret;

	if (i2c->dev.of_node) {
		of_id = of_match_device(wm831x_of_match, &i2c->dev);
		if (!of_id) {
			dev_err(&i2c->dev, "Failed to match device\n");
			return -ENODEV;
		}
		type = (uintptr_t)of_id->data;
	} else {
		type = (enum wm831x_parent)id->driver_data;
	}

	wm831x = devm_kzalloc(&i2c->dev, sizeof(struct wm831x), GFP_KERNEL);
	if (wm831x == NULL)
		return -ENOMEM;

	i2c_set_clientdata(i2c, wm831x);
	wm831x->dev = &i2c->dev;
	wm831x->type = type;

	wm831x->regmap = devm_regmap_init_i2c(i2c, &wm831x_regmap_config);
	if (IS_ERR(wm831x->regmap)) {
		ret = PTR_ERR(wm831x->regmap);
		dev_err(wm831x->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	if (pdata)
		memcpy(&wm831x->pdata, pdata, sizeof(*pdata));

	return wm831x_device_init(wm831x, i2c->irq);
}

static int wm831x_i2c_suspend(struct device *dev)
{
	struct wm831x *wm831x = dev_get_drvdata(dev);

	return wm831x_device_suspend(wm831x);
}

static int wm831x_i2c_poweroff(struct device *dev)
{
	struct wm831x *wm831x = dev_get_drvdata(dev);

	wm831x_device_shutdown(wm831x);

	return 0;
}

static const struct i2c_device_id wm831x_i2c_id[] = {
	{ "wm8310", WM8310 },
	{ "wm8311", WM8311 },
	{ "wm8312", WM8312 },
	{ "wm8320", WM8320 },
	{ "wm8321", WM8321 },
	{ "wm8325", WM8325 },
	{ "wm8326", WM8326 },
	{ }
};

static const struct dev_pm_ops wm831x_pm_ops = {
	.suspend = wm831x_i2c_suspend,
	.poweroff = wm831x_i2c_poweroff,
};

static struct i2c_driver wm831x_i2c_driver = {
	.driver = {
		.name = "wm831x",
		.pm = &wm831x_pm_ops,
		.of_match_table = of_match_ptr(wm831x_of_match),
		.suppress_bind_attrs = true,
	},
	.probe = wm831x_i2c_probe,
	.id_table = wm831x_i2c_id,
};

static int __init wm831x_i2c_init(void)
{
	int ret;

	ret = i2c_add_driver(&wm831x_i2c_driver);
	if (ret != 0)
		pr_err("Failed to register wm831x I2C driver: %d\n", ret);

	return ret;
}
subsys_initcall(wm831x_i2c_init);
