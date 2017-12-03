/*
 * STMicroelectronics uvis25 i2c driver
 *
 * Copyright 2017 STMicroelectronics Inc.
 *
 * Lorenzo Bianconi <lorenzo.bianconi83@gmail.com>
 *
 * Licensed under the GPL-2.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/acpi.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/regmap.h>

#include "st_uvis25.h"

#define UVIS25_I2C_AUTO_INCREMENT	BIT(7)

const struct regmap_config st_uvis25_i2c_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.write_flag_mask = UVIS25_I2C_AUTO_INCREMENT,
	.read_flag_mask = UVIS25_I2C_AUTO_INCREMENT,
};

static int st_uvis25_i2c_probe(struct i2c_client *client,
			       const struct i2c_device_id *id)
{
	struct regmap *regmap;

	regmap = devm_regmap_init_i2c(client, &st_uvis25_i2c_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&client->dev, "Failed to register i2c regmap %d\n",
			(int)PTR_ERR(regmap));
		return PTR_ERR(regmap);
	}

	return st_uvis25_probe(&client->dev, client->irq, regmap);
}

static const struct of_device_id st_uvis25_i2c_of_match[] = {
	{ .compatible = "st,uvis25", },
	{},
};
MODULE_DEVICE_TABLE(of, st_uvis25_i2c_of_match);

static const struct i2c_device_id st_uvis25_i2c_id_table[] = {
	{ ST_UVIS25_DEV_NAME },
	{},
};
MODULE_DEVICE_TABLE(i2c, st_uvis25_i2c_id_table);

static struct i2c_driver st_uvis25_driver = {
	.driver = {
		.name = "st_uvis25_i2c",
		.pm = &st_uvis25_pm_ops,
		.of_match_table = of_match_ptr(st_uvis25_i2c_of_match),
	},
	.probe = st_uvis25_i2c_probe,
	.id_table = st_uvis25_i2c_id_table,
};
module_i2c_driver(st_uvis25_driver);

MODULE_AUTHOR("Lorenzo Bianconi <lorenzo.bianconi83@gmail.com>");
MODULE_DESCRIPTION("STMicroelectronics uvis25 i2c driver");
MODULE_LICENSE("GPL v2");
