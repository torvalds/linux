// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2016 Texas Instruments Incorporated - https://www.ti.com/
 *
 * Author: Keerthy <j-keerthy@ti.com>
 */

#include <linux/interrupt.h>
#include <linux/mfd/core.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regmap.h>

#include <linux/mfd/lp873x.h>

static const struct regmap_config lp873x_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = LP873X_REG_MAX,
};

static const struct mfd_cell lp873x_cells[] = {
	{ .name = "lp873x-regulator", },
	{ .name = "lp873x-gpio", },
};

static int lp873x_probe(struct i2c_client *client)
{
	struct lp873x *lp873;
	int ret;
	unsigned int otpid;

	lp873 = devm_kzalloc(&client->dev, sizeof(*lp873), GFP_KERNEL);
	if (!lp873)
		return -ENOMEM;

	lp873->dev = &client->dev;

	lp873->regmap = devm_regmap_init_i2c(client, &lp873x_regmap_config);
	if (IS_ERR(lp873->regmap)) {
		ret = PTR_ERR(lp873->regmap);
		dev_err(lp873->dev,
			"Failed to initialize register map: %d\n", ret);
		return ret;
	}

	ret = regmap_read(lp873->regmap, LP873X_REG_OTP_REV, &otpid);
	if (ret) {
		dev_err(lp873->dev, "Failed to read OTP ID\n");
		return ret;
	}

	lp873->rev = otpid & LP873X_OTP_REV_OTP_ID;

	i2c_set_clientdata(client, lp873);

	ret = mfd_add_devices(lp873->dev, PLATFORM_DEVID_AUTO, lp873x_cells,
			      ARRAY_SIZE(lp873x_cells), NULL, 0, NULL);

	return ret;
}

static const struct of_device_id of_lp873x_match_table[] = {
	{ .compatible = "ti,lp8733", },
	{ .compatible = "ti,lp8732", },
	{}
};
MODULE_DEVICE_TABLE(of, of_lp873x_match_table);

static const struct i2c_device_id lp873x_id_table[] = {
	{ "lp873x", 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, lp873x_id_table);

static struct i2c_driver lp873x_driver = {
	.driver	= {
		.name	= "lp873x",
		.of_match_table = of_lp873x_match_table,
	},
	.probe		= lp873x_probe,
	.id_table	= lp873x_id_table,
};
module_i2c_driver(lp873x_driver);

MODULE_AUTHOR("J Keerthy <j-keerthy@ti.com>");
MODULE_DESCRIPTION("LP873X chip family Multi-Function Device driver");
MODULE_LICENSE("GPL v2");
