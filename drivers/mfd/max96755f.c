// SPDX-License-Identifier: GPL-2.0
/*
 * Maxim max96755f MFD driver
 *
 * Copyright (C) 2022 Rockchip Electronics Co. Ltd.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/i2c-mux.h>
#include <linux/gpio/consumer.h>
#include <linux/regmap.h>
#include <linux/mfd/core.h>
#include <linux/regulator/consumer.h>
#include <linux/mfd/max96755f.h>

struct max96755f {
	struct device *dev;
	struct regmap *regmap;
	struct i2c_mux_core *muxc;
	struct gpio_desc *enable_gpio;
	struct gpio_desc *reset_gpio;
	struct regulator *supply;
	struct gpio_desc *pwdnb_gpio;
	struct gpio_desc *lock_gpio;
	bool split_mode;
};

static const struct mfd_cell max96755f_devs[] = {
	{
		.name = "max96755f-pinctrl",
		.of_compatible = "maxim,max96755f-pinctrl",
	}, {
		.name = "max96755f-bridge",
		.of_compatible = "maxim,max96755f-bridge",
	},
};

static const struct regmap_config max96755f_regmap_config = {
	.name = "max96755f",
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = 0x1b17,
};

static int max96755f_select(struct i2c_mux_core *muxc, u32 chan)
{
	struct max96755f *max96755f = dev_get_drvdata(muxc->dev);
	u32 link_cfg, val;
	int ret;

	regmap_update_bits(max96755f->regmap, 0x0001, DIS_REM_CC,
			   FIELD_PREP(DIS_REM_CC, 0));

	if (!max96755f->split_mode)
		return 0;

	regmap_read(max96755f->regmap, 0x0010, &link_cfg);
	if ((link_cfg & LINK_CFG) == SPLITTER_MODE)
		return 0;

	if (chan == 0 && (link_cfg & LINK_CFG) != LINKA) {
		regmap_update_bits(max96755f->regmap, 0x0010,
				   RESET_ONESHOT | AUTO_LINK | LINK_CFG,
				   FIELD_PREP(RESET_ONESHOT, 1) |
				   FIELD_PREP(AUTO_LINK, 0) |
				   FIELD_PREP(LINK_CFG, LINKA));
	} else if (chan == 1 && (link_cfg & LINK_CFG) != LINKB) {
		regmap_update_bits(max96755f->regmap, 0x0010,
				   RESET_ONESHOT | AUTO_LINK | LINK_CFG,
				   FIELD_PREP(RESET_ONESHOT, 1) |
				   FIELD_PREP(AUTO_LINK, 0) |
				   FIELD_PREP(LINK_CFG, LINKB));
	}

	ret = regmap_read_poll_timeout(max96755f->regmap, 0x0013, val,
				       val & LOCKED, 100,
				       50 * USEC_PER_MSEC);
	if (ret < 0) {
		dev_err(max96755f->dev, "GMSL2 link lock timeout\n");
		return ret;
	}

	return 0;
}

static int max96755f_deselect(struct i2c_mux_core *muxc, u32 chan)
{
	struct max96755f *max96755f = dev_get_drvdata(muxc->dev);

	regmap_update_bits(max96755f->regmap, 0x0001, DIS_REM_CC,
			   FIELD_PREP(DIS_REM_CC, 1));

	return 0;
}

static void max96755f_power_off(void *data)
{
	struct max96755f *max96755f = data;

	if (max96755f->reset_gpio)
		gpiod_direction_output(max96755f->reset_gpio, 1);

	if (max96755f->enable_gpio)
		gpiod_direction_output(max96755f->enable_gpio, 0);

	if (max96755f->supply)
		regulator_disable(max96755f->supply);
}

static int max96755f_power_on(struct max96755f *max96755f)
{
	int ret;

	if (max96755f->supply) {
		ret = regulator_enable(max96755f->supply);
		if (ret < 0)
			return ret;
	}

	if (max96755f->enable_gpio) {
		gpiod_direction_output(max96755f->enable_gpio, 1);
		msleep(100);
	}

	if (max96755f->reset_gpio) {
		gpiod_direction_output(max96755f->reset_gpio, 0);
		msleep(100);
		gpiod_direction_output(max96755f->reset_gpio, 1);
		msleep(100);
		gpiod_direction_output(max96755f->reset_gpio, 0);
		msleep(100);
	}

	regmap_update_bits(max96755f->regmap, 0x0001, DIS_REM_CC,
			   FIELD_PREP(DIS_REM_CC, 1));
	return 0;
}

static int max96755f_i2c_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct device_node *child;
	struct max96755f *max96755f;
	unsigned int nr = 0;
	int ret;

	for_each_available_child_of_node(dev->of_node, child) {
		if (!of_find_property(child, "reg", NULL))
			continue;

		nr++;
	}

	max96755f = devm_kzalloc(dev, sizeof(*max96755f), GFP_KERNEL);
	if (!max96755f)
		return -ENOMEM;

	max96755f->muxc = i2c_mux_alloc(client->adapter, dev, nr, 0,
				       I2C_MUX_LOCKED, max96755f_select,
				       max96755f_deselect);
	if (!max96755f->muxc)
		return -ENOMEM;

	if (nr == 2)
		max96755f->split_mode = true;

	max96755f->dev = dev;
	i2c_set_clientdata(client, max96755f);

	max96755f->supply = devm_regulator_get(dev, "power");
	if (IS_ERR(max96755f->supply))
		return dev_err_probe(dev, PTR_ERR(max96755f->supply),
				     "failed to get power supply\n");

	max96755f->lock_gpio = devm_gpiod_get_optional(dev, "lock", GPIOD_IN);
	if (IS_ERR(max96755f->lock_gpio))
		return dev_err_probe(dev, PTR_ERR(max96755f->lock_gpio),
				     "failed to get lock GPIO\n");

	max96755f->enable_gpio = devm_gpiod_get_optional(dev, "enable", GPIOD_ASIS);
	if (IS_ERR(max96755f->enable_gpio)) {
		return dev_err_probe(dev, PTR_ERR(max96755f->enable_gpio),
				     "failed to get enable GPIO\n");
	}

	max96755f->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_ASIS);
	if (IS_ERR(max96755f->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(max96755f->reset_gpio),
				     "failed to get reset GPIO\n");

	max96755f->regmap = devm_regmap_init_i2c(client, &max96755f_regmap_config);
	if (IS_ERR(max96755f->regmap))
		return dev_err_probe(dev, PTR_ERR(max96755f->regmap),
				     "failed to initialize regmap");

	ret = max96755f_power_on(max96755f);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(dev, max96755f_power_off, max96755f);
	if (ret)
		return ret;

	ret = devm_mfd_add_devices(dev, PLATFORM_DEVID_AUTO, max96755f_devs,
				   ARRAY_SIZE(max96755f_devs), NULL, 0, NULL);
	if (ret)
		return ret;

	for_each_available_child_of_node(dev->of_node, child) {
		if (of_property_read_u32(child, "reg", &nr))
			continue;

		ret = i2c_mux_add_adapter(max96755f->muxc, 0, nr, 0);
		if (ret) {
			i2c_mux_del_adapters(max96755f->muxc);
			return ret;
		}
	}

	return 0;
}

static int max96755f_i2c_remove(struct i2c_client *client)
{
	struct max96755f *max96755f = i2c_get_clientdata(client);

	i2c_mux_del_adapters(max96755f->muxc);

	return 0;
}

static void max96755f_i2c_shutdown(struct i2c_client *client)
{
	struct max96755f *max96755f = i2c_get_clientdata(client);

	max96755f_power_off(max96755f);
}

static const struct of_device_id max96755f_of_match[] = {
	{ .compatible = "maxim,max96755f", },
	{}
};
MODULE_DEVICE_TABLE(of, max96755f_of_match);

static struct i2c_driver max96755f_i2c_driver = {
	.driver = {
		.name = "max96755f",
		.of_match_table = max96755f_of_match,
	},
	.probe_new = max96755f_i2c_probe,
	.remove = max96755f_i2c_remove,
	.shutdown = max96755f_i2c_shutdown,
};

module_i2c_driver(max96755f_i2c_driver);

MODULE_AUTHOR("Guochun Huang<hero.huang@rock-chips.com>");
MODULE_DESCRIPTION("Maxim max96755f MFD driver");
MODULE_LICENSE("GPL");
