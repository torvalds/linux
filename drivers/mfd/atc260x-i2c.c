// SPDX-License-Identifier: GPL-2.0+
/*
 * I2C bus interface for ATC260x PMICs
 *
 * Copyright (C) 2019 Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>
 * Copyright (C) 2020 Cristian Ciocaltea <cristian.ciocaltea@gmail.com>
 */

#include <linux/i2c.h>
#include <linux/mfd/atc260x/core.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regmap.h>

static int atc260x_i2c_probe(struct i2c_client *client,
			     const struct i2c_device_id *id)
{
	struct atc260x *atc260x;
	struct regmap_config regmap_cfg;
	int ret;

	atc260x = devm_kzalloc(&client->dev, sizeof(*atc260x), GFP_KERNEL);
	if (!atc260x)
		return -ENOMEM;

	atc260x->dev = &client->dev;
	atc260x->irq = client->irq;

	ret = atc260x_match_device(atc260x, &regmap_cfg);
	if (ret)
		return ret;

	i2c_set_clientdata(client, atc260x);

	atc260x->regmap = devm_regmap_init_i2c(client, &regmap_cfg);
	if (IS_ERR(atc260x->regmap)) {
		ret = PTR_ERR(atc260x->regmap);
		dev_err(&client->dev, "failed to init regmap: %d\n", ret);
		return ret;
	}

	return atc260x_device_probe(atc260x);
}

const struct of_device_id atc260x_i2c_of_match[] = {
	{ .compatible = "actions,atc2603c", .data = (void *)ATC2603C },
	{ .compatible = "actions,atc2609a", .data = (void *)ATC2609A },
	{ }
};
MODULE_DEVICE_TABLE(of, atc260x_i2c_of_match);

static struct i2c_driver atc260x_i2c_driver = {
	.driver = {
		.name = "atc260x",
		.of_match_table	= of_match_ptr(atc260x_i2c_of_match),
	},
	.probe = atc260x_i2c_probe,
};
module_i2c_driver(atc260x_i2c_driver);

MODULE_DESCRIPTION("ATC260x PMICs I2C bus interface");
MODULE_AUTHOR("Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>");
MODULE_AUTHOR("Cristian Ciocaltea <cristian.ciocaltea@gmail.com>");
MODULE_LICENSE("GPL");
