/*
 * Copyright 2009-2010 Creative Product Design
 * Marc Reilly marc@cpdesign.com.au
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/mfd/core.h>
#include <linux/mfd/mc13xxx.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/i2c.h>
#include <linux/err.h>

#include "mc13xxx.h"

static const struct i2c_device_id mc13xxx_i2c_device_id[] = {
	{
		.name = "mc13892",
		.driver_data = (kernel_ulong_t)&mc13xxx_variant_mc13892,
	}, {
		.name = "mc34708",
		.driver_data = (kernel_ulong_t)&mc13xxx_variant_mc34708,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(i2c, mc13xxx_i2c_device_id);

static const struct of_device_id mc13xxx_dt_ids[] = {
	{
		.compatible = "fsl,mc13892",
		.data = &mc13xxx_variant_mc13892,
	}, {
		.compatible = "fsl,mc34708",
		.data = &mc13xxx_variant_mc34708,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, mc13xxx_dt_ids);

static struct regmap_config mc13xxx_regmap_i2c_config = {
	.reg_bits = 8,
	.val_bits = 24,

	.max_register = MC13XXX_NUMREGS,

	.cache_type = REGCACHE_NONE,
};

static int mc13xxx_i2c_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct mc13xxx *mc13xxx;
	struct mc13xxx_platform_data *pdata = dev_get_platdata(&client->dev);
	int ret;

	mc13xxx = devm_kzalloc(&client->dev, sizeof(*mc13xxx), GFP_KERNEL);
	if (!mc13xxx)
		return -ENOMEM;

	dev_set_drvdata(&client->dev, mc13xxx);

	mc13xxx->dev = &client->dev;
	mutex_init(&mc13xxx->lock);

	mc13xxx->regmap = devm_regmap_init_i2c(client,
					       &mc13xxx_regmap_i2c_config);
	if (IS_ERR(mc13xxx->regmap)) {
		ret = PTR_ERR(mc13xxx->regmap);
		dev_err(mc13xxx->dev, "Failed to initialize register map: %d\n",
				ret);
		dev_set_drvdata(&client->dev, NULL);
		return ret;
	}

	if (client->dev.of_node) {
		const struct of_device_id *of_id =
			of_match_device(mc13xxx_dt_ids, &client->dev);
		mc13xxx->variant = of_id->data;
	} else {
		mc13xxx->variant = (void *)id->driver_data;
	}

	ret = mc13xxx_common_init(mc13xxx, pdata, client->irq);

	return ret;
}

static int mc13xxx_i2c_remove(struct i2c_client *client)
{
	struct mc13xxx *mc13xxx = dev_get_drvdata(&client->dev);

	mc13xxx_common_cleanup(mc13xxx);

	return 0;
}

static struct i2c_driver mc13xxx_i2c_driver = {
	.id_table = mc13xxx_i2c_device_id,
	.driver = {
		.owner = THIS_MODULE,
		.name = "mc13xxx",
		.of_match_table = mc13xxx_dt_ids,
	},
	.probe = mc13xxx_i2c_probe,
	.remove = mc13xxx_i2c_remove,
};

static int __init mc13xxx_i2c_init(void)
{
	return i2c_add_driver(&mc13xxx_i2c_driver);
}
subsys_initcall(mc13xxx_i2c_init);

static void __exit mc13xxx_i2c_exit(void)
{
	i2c_del_driver(&mc13xxx_i2c_driver);
}
module_exit(mc13xxx_i2c_exit);

MODULE_DESCRIPTION("i2c driver for Freescale MC13XXX PMIC");
MODULE_AUTHOR("Marc Reilly <marc@cpdesign.com.au");
MODULE_LICENSE("GPL v2");
