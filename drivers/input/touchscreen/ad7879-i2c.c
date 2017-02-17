/*
 * AD7879-1/AD7889-1 touchscreen (I2C bus)
 *
 * Copyright (C) 2008-2010 Michael Hennerich, Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/input.h>	/* BUS_I2C */
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/of.h>
#include <linux/pm.h>
#include <linux/regmap.h>

#include "ad7879.h"

#define AD7879_DEVID		0x79	/* AD7879-1/AD7889-1 */

static const struct regmap_config ad7879_i2c_regmap_config = {
	.reg_bits = 8,
	.val_bits = 16,
	.max_register = 15,
};

static int ad7879_i2c_probe(struct i2c_client *client,
				      const struct i2c_device_id *id)
{
	struct ad7879 *ts;
	struct regmap *regmap;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_WORD_DATA)) {
		dev_err(&client->dev, "SMBUS Word Data not Supported\n");
		return -EIO;
	}

	regmap = devm_regmap_init_i2c(client, &ad7879_i2c_regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	ts = ad7879_probe(&client->dev, regmap, client->irq,
			  BUS_I2C, AD7879_DEVID);
	if (IS_ERR(ts))
		return PTR_ERR(ts);

	return 0;
}

static const struct i2c_device_id ad7879_id[] = {
	{ "ad7879", 0 },
	{ "ad7889", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ad7879_id);

#ifdef CONFIG_OF
static const struct of_device_id ad7879_i2c_dt_ids[] = {
	{ .compatible = "adi,ad7879-1", },
	{ }
};
MODULE_DEVICE_TABLE(of, ad7879_i2c_dt_ids);
#endif

static struct i2c_driver ad7879_i2c_driver = {
	.driver = {
		.name	= "ad7879",
		.pm	= &ad7879_pm_ops,
		.of_match_table = of_match_ptr(ad7879_i2c_dt_ids),
	},
	.probe		= ad7879_i2c_probe,
	.id_table	= ad7879_id,
};

module_i2c_driver(ad7879_i2c_driver);

MODULE_AUTHOR("Michael Hennerich <hennerich@blackfin.uclinux.org>");
MODULE_DESCRIPTION("AD7879(-1) touchscreen I2C bus driver");
MODULE_LICENSE("GPL");
