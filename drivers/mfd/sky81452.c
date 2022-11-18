// SPDX-License-Identifier: GPL-2.0-only
/*
 * sky81452.c	SKY81452 MFD driver
 *
 * Copyright 2014 Skyworks Solutions Inc.
 * Author : Gyungoh Yoo <jack.yoo@skyworksinc.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/mfd/core.h>
#include <linux/mfd/sky81452.h>

static const struct regmap_config sky81452_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static int sky81452_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	const struct sky81452_platform_data *pdata = dev_get_platdata(dev);
	struct mfd_cell cells[2];
	struct regmap *regmap;
	int ret;

	if (!pdata) {
		pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
		if (!pdata)
			return -ENOMEM;
	}

	regmap = devm_regmap_init_i2c(client, &sky81452_config);
	if (IS_ERR(regmap)) {
		dev_err(dev, "failed to initialize.err=%ld\n", PTR_ERR(regmap));
		return PTR_ERR(regmap);
	}

	i2c_set_clientdata(client, regmap);

	memset(cells, 0, sizeof(cells));
	cells[0].name = "sky81452-backlight";
	cells[0].of_compatible = "skyworks,sky81452-backlight";
	cells[1].name = "sky81452-regulator";
	cells[1].platform_data = pdata->regulator_init_data;
	cells[1].pdata_size = sizeof(*pdata->regulator_init_data);

	ret = devm_mfd_add_devices(dev, -1, cells, ARRAY_SIZE(cells),
				   NULL, 0, NULL);
	if (ret)
		dev_err(dev, "failed to add child devices. err=%d\n", ret);

	return ret;
}

static const struct i2c_device_id sky81452_ids[] = {
	{ "sky81452" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, sky81452_ids);

#ifdef CONFIG_OF
static const struct of_device_id sky81452_of_match[] = {
	{ .compatible = "skyworks,sky81452", },
	{ }
};
MODULE_DEVICE_TABLE(of, sky81452_of_match);
#endif

static struct i2c_driver sky81452_driver = {
	.driver = {
		.name = "sky81452",
		.of_match_table = of_match_ptr(sky81452_of_match),
	},
	.probe_new = sky81452_probe,
	.id_table = sky81452_ids,
};

module_i2c_driver(sky81452_driver);

MODULE_DESCRIPTION("Skyworks SKY81452 MFD driver");
MODULE_AUTHOR("Gyungoh Yoo <jack.yoo@skyworksinc.com>");
MODULE_LICENSE("GPL v2");
