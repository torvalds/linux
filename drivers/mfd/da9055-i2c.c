// SPDX-License-Identifier: GPL-2.0-or-later
 /* I2C access for DA9055 PMICs.
 *
 * Copyright(c) 2012 Dialog Semiconductor Ltd.
 *
 * Author: David Dajun Chen <dchen@diasemi.com>
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/of_device.h>

#include <linux/mfd/da9055/core.h>

static int da9055_i2c_probe(struct i2c_client *i2c,
				      const struct i2c_device_id *id)
{
	struct da9055 *da9055;
	int ret;

	da9055 = devm_kzalloc(&i2c->dev, sizeof(struct da9055), GFP_KERNEL);
	if (!da9055)
		return -ENOMEM;

	da9055->regmap = devm_regmap_init_i2c(i2c, &da9055_regmap_config);
	if (IS_ERR(da9055->regmap)) {
		ret = PTR_ERR(da9055->regmap);
		dev_err(&i2c->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	da9055->dev = &i2c->dev;
	da9055->chip_irq = i2c->irq;

	i2c_set_clientdata(i2c, da9055);

	return da9055_device_init(da9055);
}

static void da9055_i2c_remove(struct i2c_client *i2c)
{
	struct da9055 *da9055 = i2c_get_clientdata(i2c);

	da9055_device_exit(da9055);
}

/*
 * DO NOT change the device Ids. The naming is intentionally specific as both
 * the PMIC and CODEC parts of this chip are instantiated separately as I2C
 * devices (both have configurable I2C addresses, and are to all intents and
 * purposes separate). As a result there are specific DA9055 ids for PMIC
 * and CODEC, which must be different to operate together.
 */
static const struct i2c_device_id da9055_i2c_id[] = {
	{"da9055-pmic", 0},
	{ }
};
MODULE_DEVICE_TABLE(i2c, da9055_i2c_id);

static const struct of_device_id da9055_of_match[] = {
	{ .compatible = "dlg,da9055-pmic", },
	{ }
};

static struct i2c_driver da9055_i2c_driver = {
	.probe = da9055_i2c_probe,
	.remove = da9055_i2c_remove,
	.id_table = da9055_i2c_id,
	.driver = {
		.name = "da9055-pmic",
		.of_match_table = da9055_of_match,
	},
};

static int __init da9055_i2c_init(void)
{
	int ret;

	ret = i2c_add_driver(&da9055_i2c_driver);
	if (ret != 0) {
		pr_err("DA9055 I2C registration failed %d\n", ret);
		return ret;
	}

	return 0;
}
subsys_initcall(da9055_i2c_init);

static void __exit da9055_i2c_exit(void)
{
	i2c_del_driver(&da9055_i2c_driver);
}
module_exit(da9055_i2c_exit);

MODULE_AUTHOR("David Dajun Chen <dchen@diasemi.com>");
MODULE_DESCRIPTION("I2C driver for Dialog DA9055 PMIC");
MODULE_LICENSE("GPL");
