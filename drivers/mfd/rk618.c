/*
 * Copyright (c) 2017 Rockchip Electronics Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/mfd/core.h>
#include <linux/mfd/rk618.h>

static const struct mfd_cell rk618_devs[] = {
	{
		.name = "rk618-codec",
		.of_compatible = "rockchip,rk618-codec",
	}, {
		.name = "rk618-cru",
		.of_compatible = "rockchip,rk618-cru",
	}, {
		.name = "rk618-dsi",
		.of_compatible = "rockchip,rk618-dsi",
	}, {
		.name = "rk618-hdmi",
		.of_compatible = "rockchip,rk618-hdmi",
	}, {
		.name = "rk618-lvds",
		.of_compatible = "rockchip,rk618-lvds",
	}, {
		.name = "rk618-rgb",
		.of_compatible = "rockchip,rk618-rgb",
	}, {
		.name = "rk618-scaler",
		.of_compatible = "rockchip,rk618-scaler",
	}, {
		.name = "rk618-vif",
		.of_compatible = "rockchip,rk618-vif",
	},
};

static int rk618_power_on(struct rk618 *rk618)
{
	u32 reg;
	int ret;

	ret = regulator_enable(rk618->supply);
	if (ret < 0) {
		dev_err(rk618->dev, "failed to enable supply: %d\n", ret);
		return ret;
	}

	if (rk618->enable_gpio)
		gpiod_direction_output(rk618->enable_gpio, 1);

	usleep_range(1000, 2000);

	ret = regmap_read(rk618->regmap, 0x0000, &reg);
	if (ret) {
		gpiod_direction_output(rk618->reset_gpio, 0);
		usleep_range(2000, 4000);
		gpiod_direction_output(rk618->reset_gpio, 1);
		usleep_range(50000, 60000);
		gpiod_direction_output(rk618->reset_gpio, 0);
	}

	return 0;
}

static void rk618_power_off(struct rk618 *rk618)
{
	gpiod_direction_output(rk618->reset_gpio, 1);

	if (rk618->enable_gpio)
		gpiod_direction_output(rk618->enable_gpio, 0);

	regulator_disable(rk618->supply);
}

static const struct regmap_config rk618_regmap_config = {
	.name = "core",
	.reg_bits = 16,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = 0x9c,
	.reg_format_endian = REGMAP_ENDIAN_NATIVE,
	.val_format_endian = REGMAP_ENDIAN_NATIVE,
};

static int
rk618_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct rk618 *rk618;
	int ret;

	rk618 = devm_kzalloc(dev, sizeof(*rk618), GFP_KERNEL);
	if (!rk618)
		return -ENOMEM;

	rk618->dev = dev;
	rk618->client = client;
	i2c_set_clientdata(client, rk618);

	rk618->supply = devm_regulator_get(dev, "power");
	if (IS_ERR(rk618->supply))
		return PTR_ERR(rk618->supply);

	rk618->enable_gpio = devm_gpiod_get_optional(dev, "enable", 0);
	if (IS_ERR(rk618->enable_gpio)) {
		ret = PTR_ERR(rk618->enable_gpio);
		dev_err(dev, "failed to request enable GPIO: %d\n", ret);
		return ret;
	}

	rk618->reset_gpio = devm_gpiod_get(dev, "reset", 0);
	if (IS_ERR(rk618->reset_gpio)) {
		ret = PTR_ERR(rk618->reset_gpio);
		dev_err(dev, "failed to request reset GPIO: %d\n", ret);
		return ret;
	}

	rk618->clkin = devm_clk_get(dev, "clkin");
	if (IS_ERR(rk618->clkin)) {
		dev_err(dev, "failed to get clock\n");
		return PTR_ERR(rk618->clkin);
	}

	ret = clk_prepare_enable(rk618->clkin);
	if (ret) {
		dev_err(dev, "unable to enable clock: %d\n", ret);
		return ret;
	}

	rk618->regmap = devm_regmap_init_i2c(client, &rk618_regmap_config);
	if (IS_ERR(rk618->regmap)) {
		ret = PTR_ERR(rk618->regmap);
		dev_err(dev, "failed to allocate register map: %d\n", ret);
		goto err_clk_disable;
	}

	ret = rk618_power_on(rk618);
	if (ret)
		goto err_clk_disable;

	ret = mfd_add_devices(dev, -1, rk618_devs, ARRAY_SIZE(rk618_devs),
			      NULL, 0, NULL);
	if (ret) {
		dev_err(dev, "failed to add subdev: %d\n", ret);
		goto err_clk_disable;
	}

	return 0;

err_clk_disable:
	clk_disable_unprepare(rk618->clkin);
	return ret;
}

static int rk618_remove(struct i2c_client *client)
{
	struct rk618 *rk618 = i2c_get_clientdata(client);

	mfd_remove_devices(rk618->dev);
	rk618_power_off(rk618);
	clk_disable_unprepare(rk618->clkin);

	return 0;
}

static void rk618_shutdown(struct i2c_client *client)
{
	struct rk618 *rk618 = i2c_get_clientdata(client);

	rk618_power_off(rk618);
	clk_disable_unprepare(rk618->clkin);
}

static int __maybe_unused rk618_suspend(struct device *dev)
{
	pinctrl_pm_select_sleep_state(dev);

	return 0;
}

static int __maybe_unused rk618_resume(struct device *dev)
{
	pinctrl_pm_select_default_state(dev);

	return 0;
}

static const struct dev_pm_ops rk618_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(rk618_suspend, rk618_resume)
};

static const struct of_device_id rk618_of_match[] = {
	{ .compatible = "rockchip,rk618", },
	{}
};
MODULE_DEVICE_TABLE(of, rk618_of_match);

static const struct i2c_device_id rk618_i2c_id[] = {
	{ "rk618", 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, rk618_i2c_id);

static struct i2c_driver rk618_driver = {
	.driver = {
		.name = "rk618",
		.of_match_table = of_match_ptr(rk618_of_match),
		.pm = &rk618_pm_ops,
	},
	.probe = rk618_probe,
	.remove = rk618_remove,
	.shutdown = rk618_shutdown,
	.id_table = rk618_i2c_id,
};
module_i2c_driver(rk618_driver);

MODULE_AUTHOR("Wyon Bi <bivvy.bi@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip rk618 i2c driver");
MODULE_LICENSE("GPL v2");
