// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2009-2010 Creative Product Design
 * Marc Reilly marc@cpdesign.com.au
 */

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mfd/core.h>
#include <linux/mfd/mc13xxx.h>
#include <linux/of.h>
#include <linux/of_device.h>
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

static const struct regmap_config mc13xxx_regmap_i2c_config = {
	.reg_bits = 8,
	.val_bits = 24,

	.max_register = MC13XXX_NUMREGS,

	.cache_type = REGCACHE_NONE,
};

static int mc13xxx_i2c_probe(struct i2c_client *client)
{
	const struct i2c_device_id *id = i2c_client_get_device_id(client);
	struct mc13xxx *mc13xxx;
	int ret;

	mc13xxx = devm_kzalloc(&client->dev, sizeof(*mc13xxx), GFP_KERNEL);
	if (!mc13xxx)
		return -ENOMEM;

	dev_set_drvdata(&client->dev, mc13xxx);

	mc13xxx->irq = client->irq;

	mc13xxx->regmap = devm_regmap_init_i2c(client,
					       &mc13xxx_regmap_i2c_config);
	if (IS_ERR(mc13xxx->regmap)) {
		ret = PTR_ERR(mc13xxx->regmap);
		dev_err(&client->dev, "Failed to initialize regmap: %d\n", ret);
		return ret;
	}

	if (client->dev.of_node) {
		const struct of_device_id *of_id =
			of_match_device(mc13xxx_dt_ids, &client->dev);
		mc13xxx->variant = of_id->data;
	} else {
		mc13xxx->variant = (void *)id->driver_data;
	}

	return mc13xxx_common_init(&client->dev);
}

static void mc13xxx_i2c_remove(struct i2c_client *client)
{
	mc13xxx_common_exit(&client->dev);
}

static struct i2c_driver mc13xxx_i2c_driver = {
	.id_table = mc13xxx_i2c_device_id,
	.driver = {
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
