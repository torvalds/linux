/*
 * TI lm3692x LED Driver
 *
 * Copyright (C) 2017 Texas Instruments
 *
 * Author: Dan Murphy <dmurphy@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * Data sheet is located
 * http://www.ti.com/lit/ds/snvsa29/snvsa29.pdf
 */

#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <uapi/linux/uleds.h>

#define LM3692X_REV		0x0
#define LM3692X_RESET		0x1
#define LM3692X_EN		0x10
#define LM3692X_BRT_CTRL	0x11
#define LM3692X_PWM_CTRL	0x12
#define LM3692X_BOOST_CTRL	0x13
#define LM3692X_AUTO_FREQ_HI	0x15
#define LM3692X_AUTO_FREQ_LO	0x16
#define LM3692X_BL_ADJ_THRESH	0x17
#define LM3692X_BRT_LSB		0x18
#define LM3692X_BRT_MSB		0x19
#define LM3692X_FAULT_CTRL	0x1e
#define LM3692X_FAULT_FLAGS	0x1f

#define LM3692X_SW_RESET	BIT(0)
#define LM3692X_DEVICE_EN	BIT(0)
#define LM3692X_LED1_EN		BIT(1)
#define LM3692X_LED2_EN		BIT(2)

/* Brightness Control Bits */
#define LM3692X_BL_ADJ_POL	BIT(0)
#define LM3692X_RAMP_RATE_125us	0x00
#define LM3692X_RAMP_RATE_250us	BIT(1)
#define LM3692X_RAMP_RATE_500us BIT(2)
#define LM3692X_RAMP_RATE_1ms	(BIT(1) | BIT(2))
#define LM3692X_RAMP_RATE_2ms	BIT(3)
#define LM3692X_RAMP_RATE_4ms	(BIT(3) | BIT(1))
#define LM3692X_RAMP_RATE_8ms	(BIT(2) | BIT(3))
#define LM3692X_RAMP_RATE_16ms	(BIT(1) | BIT(2) | BIT(3))
#define LM3692X_RAMP_EN		BIT(4)
#define LM3692X_BRHT_MODE_REG	0x00
#define LM3692X_BRHT_MODE_PWM	BIT(5)
#define LM3692X_BRHT_MODE_MULTI_RAMP BIT(6)
#define LM3692X_BRHT_MODE_RAMP_MULTI (BIT(5) | BIT(6))
#define LM3692X_MAP_MODE_EXP	BIT(7)

/* PWM Register Bits */
#define LM3692X_PWM_FILTER_100	BIT(0)
#define LM3692X_PWM_FILTER_150	BIT(1)
#define LM3692X_PWM_FILTER_200	(BIT(0) | BIT(1))
#define LM3692X_PWM_HYSTER_1LSB BIT(2)
#define LM3692X_PWM_HYSTER_2LSB	BIT(3)
#define LM3692X_PWM_HYSTER_3LSB (BIT(3) | BIT(2))
#define LM3692X_PWM_HYSTER_4LSB BIT(4)
#define LM3692X_PWM_HYSTER_5LSB (BIT(4) | BIT(2))
#define LM3692X_PWM_HYSTER_6LSB (BIT(4) | BIT(3))
#define LM3692X_PWM_POLARITY	BIT(5)
#define LM3692X_PWM_SAMP_4MHZ	BIT(6)
#define LM3692X_PWM_SAMP_24MHZ	BIT(7)

/* Boost Control Bits */
#define LM3692X_OCP_PROT_1A	BIT(0)
#define LM3692X_OCP_PROT_1_25A	BIT(1)
#define LM3692X_OCP_PROT_1_5A	(BIT(0) | BIT(1))
#define LM3692X_OVP_21V		BIT(2)
#define LM3692X_OVP_25V		BIT(3)
#define LM3692X_OVP_29V		(BIT(2) | BIT(3))
#define LM3692X_MIN_IND_22UH	BIT(4)
#define LM3692X_BOOST_SW_1MHZ	BIT(5)
#define LM3692X_BOOST_SW_NO_SHIFT	BIT(6)

/* Fault Control Bits */
#define LM3692X_FAULT_CTRL_OVP BIT(0)
#define LM3692X_FAULT_CTRL_OCP BIT(1)
#define LM3692X_FAULT_CTRL_TSD BIT(2)
#define LM3692X_FAULT_CTRL_OPEN BIT(3)

/* Fault Flag Bits */
#define LM3692X_FAULT_FLAG_OVP BIT(0)
#define LM3692X_FAULT_FLAG_OCP BIT(1)
#define LM3692X_FAULT_FLAG_TSD BIT(2)
#define LM3692X_FAULT_FLAG_SHRT BIT(3)
#define LM3692X_FAULT_FLAG_OPEN BIT(4)

/**
 * struct lm3692x_led -
 * @lock - Lock for reading/writing the device
 * @client - Pointer to the I2C client
 * @led_dev - LED class device pointer
 * @regmap - Devices register map
 * @enable_gpio - VDDIO/EN gpio to enable communication interface
 * @regulator - LED supply regulator pointer
 * @label - LED label
 */
struct lm3692x_led {
	struct mutex lock;
	struct i2c_client *client;
	struct led_classdev led_dev;
	struct regmap *regmap;
	struct gpio_desc *enable_gpio;
	struct regulator *regulator;
	char label[LED_MAX_NAME_SIZE];
};

static const struct reg_default lm3692x_reg_defs[] = {
	{LM3692X_EN, 0xf},
	{LM3692X_BRT_CTRL, 0x61},
	{LM3692X_PWM_CTRL, 0x73},
	{LM3692X_BOOST_CTRL, 0x6f},
	{LM3692X_AUTO_FREQ_HI, 0x0},
	{LM3692X_AUTO_FREQ_LO, 0x0},
	{LM3692X_BL_ADJ_THRESH, 0x0},
	{LM3692X_BRT_LSB, 0x7},
	{LM3692X_BRT_MSB, 0xff},
	{LM3692X_FAULT_CTRL, 0x7},
};

static const struct regmap_config lm3692x_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = LM3692X_FAULT_FLAGS,
	.reg_defaults = lm3692x_reg_defs,
	.num_reg_defaults = ARRAY_SIZE(lm3692x_reg_defs),
	.cache_type = REGCACHE_RBTREE,
};

static int lm3692x_fault_check(struct lm3692x_led *led)
{
	int ret;
	unsigned int read_buf;

	ret = regmap_read(led->regmap, LM3692X_FAULT_FLAGS, &read_buf);
	if (ret)
		return ret;

	if (read_buf)
		dev_err(&led->client->dev, "Detected a fault 0x%X\n", read_buf);

	/* The first read may clear the fault.  Check again to see if the fault
	 * still exits and return that value.
	 */
	regmap_read(led->regmap, LM3692X_FAULT_FLAGS, &read_buf);
	if (read_buf)
		dev_err(&led->client->dev, "Second read of fault flags 0x%X\n",
			read_buf);

	return read_buf;
}

static int lm3692x_brightness_set(struct led_classdev *led_cdev,
				enum led_brightness brt_val)
{
	struct lm3692x_led *led =
			container_of(led_cdev, struct lm3692x_led, led_dev);
	int ret;
	int led_brightness_lsb = (brt_val >> 5);

	mutex_lock(&led->lock);

	ret = lm3692x_fault_check(led);
	if (ret) {
		dev_err(&led->client->dev, "Cannot read/clear faults\n");
		goto out;
	}

	ret = regmap_write(led->regmap, LM3692X_BRT_MSB, brt_val);
	if (ret) {
		dev_err(&led->client->dev, "Cannot write MSB\n");
		goto out;
	}

	ret = regmap_write(led->regmap, LM3692X_BRT_LSB, led_brightness_lsb);
	if (ret) {
		dev_err(&led->client->dev, "Cannot write LSB\n");
		goto out;
	}
out:
	mutex_unlock(&led->lock);
	return ret;
}

static int lm3692x_init(struct lm3692x_led *led)
{
	int ret;

	if (led->regulator) {
		ret = regulator_enable(led->regulator);
		if (ret) {
			dev_err(&led->client->dev,
				"Failed to enable regulator\n");
			return ret;
		}
	}

	if (led->enable_gpio)
		gpiod_direction_output(led->enable_gpio, 1);

	ret = lm3692x_fault_check(led);
	if (ret) {
		dev_err(&led->client->dev, "Cannot read/clear faults\n");
		goto out;
	}

	ret = regmap_write(led->regmap, LM3692X_BRT_CTRL, 0x00);
	if (ret)
		goto out;

	/*
	 * For glitch free operation, the following data should
	 * only be written while device enable bit is 0
	 * per Section 7.5.14 of the data sheet
	 */
	ret = regmap_write(led->regmap, LM3692X_PWM_CTRL,
		LM3692X_PWM_FILTER_100 | LM3692X_PWM_SAMP_24MHZ);
	if (ret)
		goto out;

	ret = regmap_write(led->regmap, LM3692X_BOOST_CTRL,
			LM3692X_BRHT_MODE_RAMP_MULTI |
			LM3692X_BL_ADJ_POL |
			LM3692X_RAMP_RATE_250us);
	if (ret)
		goto out;

	ret = regmap_write(led->regmap, LM3692X_AUTO_FREQ_HI, 0x00);
	if (ret)
		goto out;

	ret = regmap_write(led->regmap, LM3692X_AUTO_FREQ_LO, 0x00);
	if (ret)
		goto out;

	ret = regmap_write(led->regmap, LM3692X_BL_ADJ_THRESH, 0x00);
	if (ret)
		goto out;

	ret = regmap_write(led->regmap, LM3692X_BRT_CTRL,
			LM3692X_BL_ADJ_POL | LM3692X_PWM_HYSTER_4LSB);
	if (ret)
		goto out;

	return ret;
out:
	dev_err(&led->client->dev, "Fail writing initialization values\n");

	if (led->enable_gpio)
		gpiod_direction_output(led->enable_gpio, 0);

	if (led->regulator) {
		ret = regulator_disable(led->regulator);
		if (ret)
			dev_err(&led->client->dev,
				"Failed to disable regulator\n");
	}

	return ret;
}

static int lm3692x_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int ret;
	struct lm3692x_led *led;
	struct device_node *np = client->dev.of_node;
	struct device_node *child_node;
	const char *name;

	led = devm_kzalloc(&client->dev, sizeof(*led), GFP_KERNEL);
	if (!led)
		return -ENOMEM;

	for_each_available_child_of_node(np, child_node) {
		led->led_dev.default_trigger = of_get_property(child_node,
						    "linux,default-trigger",
						    NULL);

		ret = of_property_read_string(child_node, "label", &name);
		if (!ret)
			snprintf(led->label, sizeof(led->label),
				 "%s:%s", id->name, name);
		else
			snprintf(led->label, sizeof(led->label),
				 "%s::backlight_cluster", id->name);
	};

	led->enable_gpio = devm_gpiod_get_optional(&client->dev,
						   "enable", GPIOD_OUT_LOW);
	if (IS_ERR(led->enable_gpio)) {
		ret = PTR_ERR(led->enable_gpio);
		dev_err(&client->dev, "Failed to get enable gpio: %d\n", ret);
		return ret;
	}

	led->regulator = devm_regulator_get(&client->dev, "vled");
	if (IS_ERR(led->regulator))
		led->regulator = NULL;

	led->client = client;
	led->led_dev.name = led->label;
	led->led_dev.brightness_set_blocking = lm3692x_brightness_set;

	mutex_init(&led->lock);

	i2c_set_clientdata(client, led);

	led->regmap = devm_regmap_init_i2c(client, &lm3692x_regmap_config);
	if (IS_ERR(led->regmap)) {
		ret = PTR_ERR(led->regmap);
		dev_err(&client->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	ret = lm3692x_init(led);
	if (ret)
		return ret;

	ret = devm_led_classdev_register(&client->dev, &led->led_dev);
	if (ret) {
		dev_err(&client->dev, "led register err: %d\n", ret);
		return ret;
	}

	return 0;
}

static int lm3692x_remove(struct i2c_client *client)
{
	struct lm3692x_led *led = i2c_get_clientdata(client);
	int ret;

	if (led->enable_gpio)
		gpiod_direction_output(led->enable_gpio, 0);

	if (led->regulator) {
		ret = regulator_disable(led->regulator);
		if (ret)
			dev_err(&led->client->dev,
				"Failed to disable regulator\n");
	}

	mutex_destroy(&led->lock);

	return 0;
}

static const struct i2c_device_id lm3692x_id[] = {
	{ "lm36922", 0 },
	{ "lm36923", 1 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, lm3692x_id);

static const struct of_device_id of_lm3692x_leds_match[] = {
	{ .compatible = "ti,lm36922", },
	{ .compatible = "ti,lm36923", },
	{},
};
MODULE_DEVICE_TABLE(of, of_lm3692x_leds_match);

static struct i2c_driver lm3692x_driver = {
	.driver = {
		.name	= "lm3692x",
		.of_match_table = of_lm3692x_leds_match,
	},
	.probe		= lm3692x_probe,
	.remove		= lm3692x_remove,
	.id_table	= lm3692x_id,
};
module_i2c_driver(lm3692x_driver);

MODULE_DESCRIPTION("Texas Instruments LM3692X LED driver");
MODULE_AUTHOR("Dan Murphy <dmurphy@ti.com>");
MODULE_LICENSE("GPL v2");
