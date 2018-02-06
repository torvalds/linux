/*
 * Driver for rockchip rk1000 core controller
 *  Copyright (C) 2017 Fuzhou Rockchip Electronics Co.Ltd
 *  Author:
 *      Algea Cao <algea.cao@rock-chips.com>
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/gpio/consumer.h>
#include <linux/mfd/core.h>

#define CTRL_ADC 0x00
#define ADC_OFF 0x88
#define CTRL_CODEC 0x01
#define CODEC_OFF 0x0d
#define CTRL_I2C 0x02
#define I2C_TIMEOUT_PERIOD 0x22
#define CTRL_TVE 0x03
#define TVE_OFF 0x00

struct rk1000 {
	struct i2c_client *client;
	struct device *dev;
	struct regmap *regmap;
	struct gpio_desc *io_reset;
	struct clk *mclk;
};

static const struct mfd_cell rk1000_devs[] = {
	{
		.name = "rk1000-tve",
		.of_compatible = "rockchip,rk1000-tve",
	},
};

static const struct regmap_range rk1000_ctl_volatile_ranges[] = {
	{ .range_min = 0x00, .range_max = 0x05 },
};

static const struct regmap_access_table rk1000_ctl_reg_table = {
	.yes_ranges = rk1000_ctl_volatile_ranges,
	.n_yes_ranges = ARRAY_SIZE(rk1000_ctl_volatile_ranges),
};

struct regmap_config rk1000_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.volatile_table = &rk1000_ctl_reg_table,
};

static int rk1000_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	bool uboot_logo;
	int ret, val = 0;
	struct rk1000 *rk1000 = devm_kzalloc(&client->dev, sizeof(*rk1000),
					     GFP_KERNEL);
	if (!rk1000)
		return -ENOMEM;

	rk1000->client = client;
	rk1000->dev = &client->dev;

	rk1000->regmap = devm_regmap_init_i2c(client, &rk1000_regmap_config);
	if (IS_ERR(rk1000->regmap))
		return PTR_ERR(rk1000->regmap);

	ret = regmap_read(rk1000->regmap, CTRL_TVE, &val);

	/*
	 * If rk1000's registers can be read and rk1000 cvbs output is
	 * enabled, we think uboot logo is on.
	 */
	if (!ret && val & BIT(6))
		uboot_logo = true;
	else
		uboot_logo = false;

	if (!uboot_logo) {
		/********Get reset pin***********/
		rk1000->io_reset = devm_gpiod_get_optional(rk1000->dev, "reset",
							   GPIOD_OUT_LOW);
		if (IS_ERR(rk1000->io_reset)) {
			dev_err(rk1000->dev, "can't get rk1000 reset gpio\n");
			return PTR_ERR(rk1000->io_reset);
		}

		gpiod_set_value(rk1000->io_reset, 0);
		usleep_range(500, 1000);
		gpiod_set_value(rk1000->io_reset, 1);
		usleep_range(500, 1000);
		gpiod_set_value(rk1000->io_reset, 0);
	}

	rk1000->mclk = devm_clk_get(rk1000->dev, "mclk");
	if (IS_ERR(rk1000->mclk)) {
		dev_err(rk1000->dev, "get mclk err\n");
		return PTR_ERR(rk1000->mclk);
	}

	ret = clk_prepare_enable(rk1000->mclk);
	if (ret < 0) {
		dev_err(rk1000->dev, "prepare mclk err\n");
		goto clk_err;
	}

	regmap_write(rk1000->regmap, CTRL_ADC, ADC_OFF);
	regmap_write(rk1000->regmap, CTRL_CODEC, CODEC_OFF);
	regmap_write(rk1000->regmap, CTRL_I2C, I2C_TIMEOUT_PERIOD);
	if (!uboot_logo)
		regmap_write(rk1000->regmap, CTRL_TVE, TVE_OFF);

	ret = mfd_add_devices(rk1000->dev, -1, rk1000_devs,
			      ARRAY_SIZE(rk1000_devs), NULL, 0, NULL);
	if (ret) {
		dev_err(rk1000->dev, "rk1000 mfd_add_devices failed\n");
		goto mfd_add_err;
	}

	i2c_set_clientdata(client, rk1000);
	dev_dbg(rk1000->dev, "rk1000 probe ok!\n");
	return 0;

mfd_add_err:
	mfd_remove_devices(rk1000->dev);
clk_err:
	clk_disable_unprepare(rk1000->mclk);
	return ret;
}

static int rk1000_remove(struct i2c_client *client)
{
	struct rk1000 *rk1000 = i2c_get_clientdata(client);

	clk_disable_unprepare(rk1000->mclk);
	mfd_remove_devices(rk1000->dev);

	return 0;
}

static const struct i2c_device_id rk1000_id[] = {
	{ "rk1000-ctl", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, rk1000_id);

static const struct of_device_id rk1000_ctl_dt_ids[] = {
	{ .compatible = "rockchip,rk1000-ctl" },
	{ }
};

MODULE_DEVICE_TABLE(of, rk1000_ctl_dt_ids);

static struct i2c_driver rk1000_driver = {
	.driver = {
		.name = "rk1000-ctl",
	},
	.probe = rk1000_probe,
	.remove = rk1000_remove,
	.id_table = rk1000_id,
};

module_i2c_driver(rk1000_driver);

MODULE_DESCRIPTION("RK1000 core control driver");
MODULE_AUTHOR("Algea Cao <algea.cao@rock-chips.com>");
MODULE_LICENSE("GPL");
