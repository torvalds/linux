// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Rockchip Electronics Co. Ltd.
 *
 * Author: Algea Cao <algea.cao@rock-chips.com>
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/mfd/rk630.h>

static int
rk630_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct rk630 *rk630;
	int ret;

	rk630 = devm_kzalloc(dev, sizeof(*rk630), GFP_KERNEL);
	if (!rk630)
		return -ENOMEM;

	rk630->dev = dev;
	rk630->client = client;
	i2c_set_clientdata(client, rk630);

	rk630->grf = devm_regmap_init_i2c(client, &rk630_grf_regmap_config);
	if (IS_ERR(rk630->grf)) {
		ret = PTR_ERR(rk630->grf);
		dev_err(dev, "failed to allocate grf register map: %d\n", ret);
		return ret;
	}

	rk630->cru = devm_regmap_init_i2c(client, &rk630_cru_regmap_config);
	if (IS_ERR(rk630->cru)) {
		ret = PTR_ERR(rk630->cru);
		dev_err(dev, "failed to allocate cru register map: %d\n", ret);
		return ret;
	}

	rk630->tve = devm_regmap_init_i2c(client, &rk630_tve_regmap_config);
	if (IS_ERR(rk630->tve)) {
		ret = PTR_ERR(rk630->tve);
		dev_err(rk630->dev, "Failed to initialize tve regmap: %d\n",
			ret);
		return ret;
	}

	return rk630_core_probe(rk630);
}

static const struct of_device_id rk630_i2c_of_match[] = {
	{ .compatible = "rockchip,rk630", },
	{}
};
MODULE_DEVICE_TABLE(of, rk630_i2c_of_match);

static const struct i2c_device_id rk630_i2c_id[] = {
	{ "rk630", 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, rk630_i2c_id);

static struct i2c_driver rk630_i2c_driver = {
	.driver = {
		.name = "rk630",
		.of_match_table = of_match_ptr(rk630_i2c_of_match),
	},
	.probe = rk630_i2c_probe,
	.id_table = rk630_i2c_id,
};
module_i2c_driver(rk630_i2c_driver);

MODULE_AUTHOR("Algea Cao <Algea.cao@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip rk630 MFD I2C driver");
MODULE_LICENSE("GPL v2");
