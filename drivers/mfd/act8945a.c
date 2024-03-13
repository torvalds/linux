// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * MFD driver for Active-semi ACT8945a PMIC
 *
 * Copyright (C) 2015 Atmel Corporation.
 *
 * Author: Wenyou Yang <wenyou.yang@atmel.com>
 */

#include <linux/i2c.h>
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/regmap.h>

static const struct mfd_cell act8945a_devs[] = {
	{
		.name = "act8945a-regulator",
	},
	{
		.name = "act8945a-charger",
		.of_compatible = "active-semi,act8945a-charger",
	},
};

static const struct regmap_config act8945a_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static int act8945a_i2c_probe(struct i2c_client *i2c,
			      const struct i2c_device_id *id)
{
	int ret;
	struct regmap *regmap;

	regmap = devm_regmap_init_i2c(i2c, &act8945a_regmap_config);
	if (IS_ERR(regmap)) {
		ret = PTR_ERR(regmap);
		dev_err(&i2c->dev, "regmap init failed: %d\n", ret);
		return ret;
	}

	i2c_set_clientdata(i2c, regmap);

	ret = devm_mfd_add_devices(&i2c->dev, PLATFORM_DEVID_NONE,
				   act8945a_devs, ARRAY_SIZE(act8945a_devs),
				   NULL, 0, NULL);
	if (ret) {
		dev_err(&i2c->dev, "Failed to add sub devices\n");
		return ret;
	}

	return 0;
}

static const struct i2c_device_id act8945a_i2c_id[] = {
	{ "act8945a", 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, act8945a_i2c_id);

static const struct of_device_id act8945a_of_match[] = {
	{ .compatible = "active-semi,act8945a", },
	{},
};
MODULE_DEVICE_TABLE(of, act8945a_of_match);

static struct i2c_driver act8945a_i2c_driver = {
	.driver = {
		   .name = "act8945a",
		   .of_match_table = of_match_ptr(act8945a_of_match),
	},
	.probe = act8945a_i2c_probe,
	.id_table = act8945a_i2c_id,
};

static int __init act8945a_i2c_init(void)
{
	return i2c_add_driver(&act8945a_i2c_driver);
}
subsys_initcall(act8945a_i2c_init);

static void __exit act8945a_i2c_exit(void)
{
	i2c_del_driver(&act8945a_i2c_driver);
}
module_exit(act8945a_i2c_exit);

MODULE_DESCRIPTION("ACT8945A PMIC multi-function driver");
MODULE_AUTHOR("Wenyou Yang <wenyou.yang@atmel.com>");
MODULE_LICENSE("GPL");
