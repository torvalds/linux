// SPDX-License-Identifier: GPL-2.0-only
/*
 * Linear Technology LTC4306 and LTC4305 I2C multiplexer/switch
 *
 * Copyright (C) 2017 Analog Devices Inc.
 *
 * Based on: i2c-mux-pca954x.c
 *
 * Datasheet: http://cds.linear.com/docs/en/datasheet/4306.pdf
 */

#include <linux/gpio/consumer.h>
#include <linux/gpio/driver.h>
#include <linux/i2c-mux.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#define LTC4305_MAX_NCHANS 2
#define LTC4306_MAX_NCHANS 4

#define LTC_REG_STATUS	0x0
#define LTC_REG_CONFIG	0x1
#define LTC_REG_MODE	0x2
#define LTC_REG_SWITCH	0x3

#define LTC_DOWNSTREAM_ACCL_EN	BIT(6)
#define LTC_UPSTREAM_ACCL_EN	BIT(7)

#define LTC_GPIO_ALL_INPUT	0xC0
#define LTC_SWITCH_MASK		0xF0

enum ltc_type {
	ltc_4305,
	ltc_4306,
};

struct chip_desc {
	u8 nchans;
	u8 num_gpios;
};

struct ltc4306 {
	struct regmap *regmap;
	struct gpio_chip gpiochip;
	const struct chip_desc *chip;
};

static const struct chip_desc chips[] = {
	[ltc_4305] = {
		.nchans = LTC4305_MAX_NCHANS,
	},
	[ltc_4306] = {
		.nchans = LTC4306_MAX_NCHANS,
		.num_gpios = 2,
	},
};

static bool ltc4306_is_volatile_reg(struct device *dev, unsigned int reg)
{
	return reg == LTC_REG_CONFIG;
}

static const struct regmap_config ltc4306_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = LTC_REG_SWITCH,
	.volatile_reg = ltc4306_is_volatile_reg,
	.cache_type = REGCACHE_FLAT,
};

static int ltc4306_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct ltc4306 *data = gpiochip_get_data(chip);
	unsigned int val;
	int ret;

	ret = regmap_read(data->regmap, LTC_REG_CONFIG, &val);
	if (ret < 0)
		return ret;

	return !!(val & BIT(1 - offset));
}

static void ltc4306_gpio_set(struct gpio_chip *chip, unsigned int offset,
			     int value)
{
	struct ltc4306 *data = gpiochip_get_data(chip);

	regmap_update_bits(data->regmap, LTC_REG_CONFIG, BIT(5 - offset),
			   value ? BIT(5 - offset) : 0);
}

static int ltc4306_gpio_get_direction(struct gpio_chip *chip,
				      unsigned int offset)
{
	struct ltc4306 *data = gpiochip_get_data(chip);
	unsigned int val;
	int ret;

	ret = regmap_read(data->regmap, LTC_REG_MODE, &val);
	if (ret < 0)
		return ret;

	return !!(val & BIT(7 - offset));
}

static int ltc4306_gpio_direction_input(struct gpio_chip *chip,
					unsigned int offset)
{
	struct ltc4306 *data = gpiochip_get_data(chip);

	return regmap_update_bits(data->regmap, LTC_REG_MODE,
				  BIT(7 - offset), BIT(7 - offset));
}

static int ltc4306_gpio_direction_output(struct gpio_chip *chip,
					 unsigned int offset, int value)
{
	struct ltc4306 *data = gpiochip_get_data(chip);

	ltc4306_gpio_set(chip, offset, value);
	return regmap_update_bits(data->regmap, LTC_REG_MODE,
				  BIT(7 - offset), 0);
}

static int ltc4306_gpio_set_config(struct gpio_chip *chip,
				   unsigned int offset, unsigned long config)
{
	struct ltc4306 *data = gpiochip_get_data(chip);
	unsigned int val;

	switch (pinconf_to_config_param(config)) {
	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		val = 0;
		break;
	case PIN_CONFIG_DRIVE_PUSH_PULL:
		val = BIT(4 - offset);
		break;
	default:
		return -ENOTSUPP;
	}

	return regmap_update_bits(data->regmap, LTC_REG_MODE,
				  BIT(4 - offset), val);
}

static int ltc4306_gpio_init(struct ltc4306 *data)
{
	struct device *dev = regmap_get_device(data->regmap);

	if (!data->chip->num_gpios)
		return 0;

	data->gpiochip.label = dev_name(dev);
	data->gpiochip.base = -1;
	data->gpiochip.ngpio = data->chip->num_gpios;
	data->gpiochip.parent = dev;
	data->gpiochip.can_sleep = true;
	data->gpiochip.get_direction = ltc4306_gpio_get_direction;
	data->gpiochip.direction_input = ltc4306_gpio_direction_input;
	data->gpiochip.direction_output = ltc4306_gpio_direction_output;
	data->gpiochip.get = ltc4306_gpio_get;
	data->gpiochip.set = ltc4306_gpio_set;
	data->gpiochip.set_config = ltc4306_gpio_set_config;
	data->gpiochip.owner = THIS_MODULE;

	/* gpiolib assumes all GPIOs default input */
	regmap_write(data->regmap, LTC_REG_MODE, LTC_GPIO_ALL_INPUT);

	return devm_gpiochip_add_data(dev, &data->gpiochip, data);
}

static int ltc4306_select_mux(struct i2c_mux_core *muxc, u32 chan)
{
	struct ltc4306 *data = i2c_mux_priv(muxc);

	return regmap_update_bits(data->regmap, LTC_REG_SWITCH,
				  LTC_SWITCH_MASK, BIT(7 - chan));
}

static int ltc4306_deselect_mux(struct i2c_mux_core *muxc, u32 chan)
{
	struct ltc4306 *data = i2c_mux_priv(muxc);

	return regmap_update_bits(data->regmap, LTC_REG_SWITCH,
				  LTC_SWITCH_MASK, 0);
}

static const struct i2c_device_id ltc4306_id[] = {
	{ "ltc4305", ltc_4305 },
	{ "ltc4306", ltc_4306 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ltc4306_id);

static const struct of_device_id ltc4306_of_match[] = {
	{ .compatible = "lltc,ltc4305", .data = &chips[ltc_4305] },
	{ .compatible = "lltc,ltc4306", .data = &chips[ltc_4306] },
	{ }
};
MODULE_DEVICE_TABLE(of, ltc4306_of_match);

static int ltc4306_probe(struct i2c_client *client)
{
	struct i2c_adapter *adap = client->adapter;
	const struct chip_desc *chip;
	struct i2c_mux_core *muxc;
	struct ltc4306 *data;
	struct gpio_desc *gpio;
	bool idle_disc;
	unsigned int val = 0;
	int num, ret;

	chip = of_device_get_match_data(&client->dev);

	if (!chip)
		chip = &chips[i2c_match_id(ltc4306_id, client)->driver_data];

	idle_disc = device_property_read_bool(&client->dev,
					      "i2c-mux-idle-disconnect");

	muxc = i2c_mux_alloc(adap, &client->dev,
			     chip->nchans, sizeof(*data),
			     I2C_MUX_LOCKED, ltc4306_select_mux,
			     idle_disc ? ltc4306_deselect_mux : NULL);
	if (!muxc)
		return -ENOMEM;
	data = i2c_mux_priv(muxc);
	data->chip = chip;

	i2c_set_clientdata(client, muxc);

	data->regmap = devm_regmap_init_i2c(client, &ltc4306_regmap_config);
	if (IS_ERR(data->regmap)) {
		ret = PTR_ERR(data->regmap);
		dev_err(&client->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	/* Reset and enable the mux if an enable GPIO is specified. */
	gpio = devm_gpiod_get_optional(&client->dev, "enable", GPIOD_OUT_LOW);
	if (IS_ERR(gpio))
		return PTR_ERR(gpio);

	if (gpio) {
		udelay(1);
		gpiod_set_value(gpio, 1);
	}

	/*
	 * Write the mux register at addr to verify
	 * that the mux is in fact present. This also
	 * initializes the mux to disconnected state.
	 */
	if (regmap_write(data->regmap, LTC_REG_SWITCH, 0) < 0) {
		dev_warn(&client->dev, "probe failed\n");
		return -ENODEV;
	}

	if (device_property_read_bool(&client->dev,
				      "ltc,downstream-accelerators-enable"))
		val |= LTC_DOWNSTREAM_ACCL_EN;

	if (device_property_read_bool(&client->dev,
				      "ltc,upstream-accelerators-enable"))
		val |= LTC_UPSTREAM_ACCL_EN;

	if (regmap_write(data->regmap, LTC_REG_CONFIG, val) < 0)
		return -ENODEV;

	ret = ltc4306_gpio_init(data);
	if (ret < 0)
		return ret;

	/* Now create an adapter for each channel */
	for (num = 0; num < chip->nchans; num++) {
		ret = i2c_mux_add_adapter(muxc, 0, num);
		if (ret) {
			i2c_mux_del_adapters(muxc);
			return ret;
		}
	}

	dev_info(&client->dev,
		 "registered %d multiplexed busses for I2C switch %s\n",
		 num, client->name);

	return 0;
}

static void ltc4306_remove(struct i2c_client *client)
{
	struct i2c_mux_core *muxc = i2c_get_clientdata(client);

	i2c_mux_del_adapters(muxc);
}

static struct i2c_driver ltc4306_driver = {
	.driver		= {
		.name	= "ltc4306",
		.of_match_table = of_match_ptr(ltc4306_of_match),
	},
	.probe		= ltc4306_probe,
	.remove		= ltc4306_remove,
	.id_table	= ltc4306_id,
};

module_i2c_driver(ltc4306_driver);

MODULE_AUTHOR("Michael Hennerich <michael.hennerich@analog.com>");
MODULE_DESCRIPTION("Linear Technology LTC4306, LTC4305 I2C mux/switch driver");
MODULE_LICENSE("GPL v2");
