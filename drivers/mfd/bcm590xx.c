/*
 * Broadcom BCM590xx PMU
 *
 * Copyright 2014 Linaro Limited
 * Author: Matt Porter <mporter@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under  the terms of the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the License, or (at your
 * option) any later version.
 */

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/mfd/bcm590xx.h>
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>

static const struct mfd_cell bcm590xx_devs[] = {
	{
		.name = "bcm590xx-vregs",
	},
};

static const struct regmap_config bcm590xx_regmap_config = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.max_register	= BCM590XX_MAX_REGISTER,
	.cache_type	= REGCACHE_RBTREE,
};

static int bcm590xx_i2c_probe(struct i2c_client *i2c,
			      const struct i2c_device_id *id)
{
	struct bcm590xx *bcm590xx;
	int ret;

	bcm590xx = devm_kzalloc(&i2c->dev, sizeof(*bcm590xx), GFP_KERNEL);
	if (!bcm590xx)
		return -ENOMEM;

	i2c_set_clientdata(i2c, bcm590xx);
	bcm590xx->dev = &i2c->dev;
	bcm590xx->i2c_client = i2c;

	bcm590xx->regmap = devm_regmap_init_i2c(i2c, &bcm590xx_regmap_config);
	if (IS_ERR(bcm590xx->regmap)) {
		ret = PTR_ERR(bcm590xx->regmap);
		dev_err(&i2c->dev, "regmap initialization failed: %d\n", ret);
		return ret;
	}

	ret = mfd_add_devices(&i2c->dev, -1, bcm590xx_devs,
			      ARRAY_SIZE(bcm590xx_devs), NULL, 0, NULL);
	if (ret < 0)
		dev_err(&i2c->dev, "failed to add sub-devices: %d\n", ret);

	return ret;
}

static const struct of_device_id bcm590xx_of_match[] = {
	{ .compatible = "brcm,bcm59056" },
	{ }
};
MODULE_DEVICE_TABLE(of, bcm590xx_of_match);

static const struct i2c_device_id bcm590xx_i2c_id[] = {
	{ "bcm59056" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, bcm590xx_i2c_id);

static struct i2c_driver bcm590xx_i2c_driver = {
	.driver = {
		   .name = "bcm590xx",
		   .owner = THIS_MODULE,
		   .of_match_table = of_match_ptr(bcm590xx_of_match),
	},
	.probe = bcm590xx_i2c_probe,
	.id_table = bcm590xx_i2c_id,
};
module_i2c_driver(bcm590xx_i2c_driver);

MODULE_AUTHOR("Matt Porter <mporter@linaro.org>");
MODULE_DESCRIPTION("BCM590xx multi-function driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:bcm590xx");
