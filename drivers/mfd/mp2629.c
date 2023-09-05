// SPDX-License-Identifier: GPL-2.0+
/*
 * MP2629 parent driver for ADC and battery charger
 *
 * Copyright 2020 Monolithic Power Systems, Inc
 *
 * Author: Saravanan Sekar <sravanhome@gmail.com>
 */

#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/mfd/core.h>
#include <linux/mfd/mp2629.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>

static const struct mfd_cell mp2629_cell[] = {
	{
		.name = "mp2629_adc",
		.of_compatible = "mps,mp2629_adc",
	},
	{
		.name = "mp2629_charger",
		.of_compatible = "mps,mp2629_charger",
	}
};

static const struct regmap_config mp2629_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0x17,
};

static int mp2629_probe(struct i2c_client *client)
{
	struct mp2629_data *ddata;
	int ret;

	ddata = devm_kzalloc(&client->dev, sizeof(*ddata), GFP_KERNEL);
	if (!ddata)
		return -ENOMEM;

	ddata->dev = &client->dev;
	i2c_set_clientdata(client, ddata);

	ddata->regmap = devm_regmap_init_i2c(client, &mp2629_regmap_config);
	if (IS_ERR(ddata->regmap)) {
		dev_err(ddata->dev, "Failed to allocate regmap\n");
		return PTR_ERR(ddata->regmap);
	}

	ret = devm_mfd_add_devices(ddata->dev, PLATFORM_DEVID_AUTO, mp2629_cell,
				   ARRAY_SIZE(mp2629_cell), NULL, 0, NULL);
	if (ret)
		dev_err(ddata->dev, "Failed to register sub-devices %d\n", ret);

	return ret;
}

static const struct of_device_id mp2629_of_match[] = {
	{ .compatible = "mps,mp2629"},
	{ }
};
MODULE_DEVICE_TABLE(of, mp2629_of_match);

static struct i2c_driver mp2629_driver = {
	.driver = {
		.name = "mp2629",
		.of_match_table = mp2629_of_match,
	},
	.probe		= mp2629_probe,
};
module_i2c_driver(mp2629_driver);

MODULE_AUTHOR("Saravanan Sekar <sravanhome@gmail.com>");
MODULE_DESCRIPTION("MP2629 Battery charger parent driver");
MODULE_LICENSE("GPL");
