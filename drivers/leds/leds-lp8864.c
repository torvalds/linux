// SPDX-License-Identifier: GPL-2.0-only
/*
 * TI LP8864/LP8866 4/6 Channel LED Driver
 *
 * Copyright (C) 2024 Siemens AG
 *
 * Based on LP8860 driver by Dan Murphy <dmurphy@ti.com>
 */

#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>

#define LP8864_BRT_CONTROL		0x00
#define LP8864_USER_CONFIG1		0x04
#define   LP8864_BRT_MODE_MASK		GENMASK(9, 8)
#define   LP8864_BRT_MODE_REG		BIT(9)		/* Brightness control by DISPLAY_BRT reg */
#define LP8864_SUPPLY_STATUS		0x0e
#define LP8864_BOOST_STATUS		0x10
#define LP8864_LED_STATUS		0x12
#define   LP8864_LED_STATUS_WR_MASK	GENMASK(14, 9)	/* Writeable bits in the LED_STATUS reg */

/* Textual meaning for status bits, starting from bit 1 */
static const char *const lp8864_supply_status_msg[] = {
	"Vin under-voltage fault",
	"Vin over-voltage fault",
	"Vdd under-voltage fault",
	"Vin over-current fault",
	"Missing charge pump fault",
	"Charge pump fault",
	"Missing boost sync fault",
	"CRC error fault ",
};

/* Textual meaning for status bits, starting from bit 1 */
static const char *const lp8864_boost_status_msg[] = {
	"Boost OVP low fault",
	"Boost OVP high fault",
	"Boost over-current fault",
	"Missing boost FSET resistor fault",
	"Missing MODE SEL resistor fault",
	"Missing LED resistor fault",
	"ISET resistor short to ground fault",
	"Thermal shutdown fault",
};

/* Textual meaning for every register bit */
static const char *const lp8864_led_status_msg[] = {
	"LED 1 fault",
	"LED 2 fault",
	"LED 3 fault",
	"LED 4 fault",
	"LED 5 fault",
	"LED 6 fault",
	"LED open fault",
	"LED internal short fault",
	"LED short to GND fault",
	NULL, NULL, NULL,
	"Invalid string configuration fault",
	NULL,
	"I2C time out fault",
};

/**
 * struct lp8864_led
 * @client: Pointer to the I2C client
 * @led_dev: led class device pointer
 * @regmap: Devices register map
 * @led_status_mask: Helps to report LED fault only once
 */
struct lp8864_led {
	struct i2c_client *client;
	struct led_classdev led_dev;
	struct regmap *regmap;
	u16 led_status_mask;
};

static int lp8864_fault_check(struct lp8864_led *led)
{
	int ret, i;
	unsigned int val;

	ret = regmap_read(led->regmap, LP8864_SUPPLY_STATUS, &val);
	if (ret)
		goto err;

	/* Odd bits are status bits, even bits are clear bits */
	for (i = 0; i < ARRAY_SIZE(lp8864_supply_status_msg); i++)
		if (val & BIT(i * 2 + 1))
			dev_warn(&led->client->dev, "%s\n", lp8864_supply_status_msg[i]);

	/*
	 * Clear bits have an index preceding the corresponding Status bits;
	 * both have to be written "1" simultaneously to clear the corresponding
	 * Status bit.
	 */
	if (val)
		ret = regmap_write(led->regmap, LP8864_SUPPLY_STATUS, val >> 1 | val);
	if (ret)
		goto err;

	ret = regmap_read(led->regmap, LP8864_BOOST_STATUS, &val);
	if (ret)
		goto err;

	/* Odd bits are status bits, even bits are clear bits */
	for (i = 0; i < ARRAY_SIZE(lp8864_boost_status_msg); i++)
		if (val & BIT(i * 2 + 1))
			dev_warn(&led->client->dev, "%s\n", lp8864_boost_status_msg[i]);

	if (val)
		ret = regmap_write(led->regmap, LP8864_BOOST_STATUS, val >> 1 | val);
	if (ret)
		goto err;

	ret = regmap_read(led->regmap, LP8864_LED_STATUS, &val);
	if (ret)
		goto err;

	/*
	 * Clear already reported faults that maintain their value until device
	 * power-down
	 */
	val &= ~led->led_status_mask;

	for (i = 0; i < ARRAY_SIZE(lp8864_led_status_msg); i++)
		if (lp8864_led_status_msg[i] && val & BIT(i))
			dev_warn(&led->client->dev, "%s\n", lp8864_led_status_msg[i]);

	/*
	 * Mark those which maintain their value until device power-down as
	 * "already reported"
	 */
	led->led_status_mask |= val & ~LP8864_LED_STATUS_WR_MASK;

	/*
	 * Only bits 14, 12, 10 have to be cleared here, but others are RO,
	 * we don't care what we write to them.
	 */
	if (val & LP8864_LED_STATUS_WR_MASK)
		ret = regmap_write(led->regmap, LP8864_LED_STATUS, val >> 1 | val);
	if (ret)
		goto err;

	return 0;

err:
	dev_err(&led->client->dev, "Failed to read/clear faults (%pe)\n", ERR_PTR(ret));

	return ret;
}

static int lp8864_brightness_set(struct led_classdev *led_cdev,
				 enum led_brightness brt_val)
{
	struct lp8864_led *led = container_of(led_cdev, struct lp8864_led, led_dev);
	/* Scale 0..LED_FULL into 16-bit HW brightness */
	unsigned int val = brt_val * 0xffff / LED_FULL;
	int ret;

	ret = lp8864_fault_check(led);
	if (ret)
		return ret;

	ret = regmap_write(led->regmap, LP8864_BRT_CONTROL, val);
	if (ret)
		dev_err(&led->client->dev, "Failed to write brightness value\n");

	return ret;
}

static enum led_brightness lp8864_brightness_get(struct led_classdev *led_cdev)
{
	struct lp8864_led *led = container_of(led_cdev, struct lp8864_led, led_dev);
	unsigned int val;
	int ret;

	ret = regmap_read(led->regmap, LP8864_BRT_CONTROL, &val);
	if (ret) {
		dev_err(&led->client->dev, "Failed to read brightness value\n");
		return ret;
	}

	/* Scale 16-bit HW brightness into 0..LED_FULL */
	return val * LED_FULL / 0xffff;
}

static const struct regmap_config lp8864_regmap_config = {
	.reg_bits		= 8,
	.val_bits		= 16,
	.val_format_endian	= REGMAP_ENDIAN_LITTLE,
};

static void lp8864_disable_gpio(void *data)
{
	struct gpio_desc *gpio = data;

	gpiod_set_value(gpio, 0);
}

static int lp8864_probe(struct i2c_client *client)
{
	int ret;
	struct lp8864_led *led;
	struct device_node *np = dev_of_node(&client->dev);
	struct device_node *child_node;
	struct led_init_data init_data = {};
	struct gpio_desc *enable_gpio;

	led = devm_kzalloc(&client->dev, sizeof(*led), GFP_KERNEL);
	if (!led)
		return -ENOMEM;

	child_node = of_get_next_available_child(np, NULL);
	if (!child_node) {
		dev_err(&client->dev, "No LED function defined\n");
		return -EINVAL;
	}

	ret = devm_regulator_get_enable_optional(&client->dev, "vled");
	if (ret && ret != -ENODEV)
		return dev_err_probe(&client->dev, ret, "Failed to enable vled regulator\n");

	enable_gpio = devm_gpiod_get_optional(&client->dev, "enable", GPIOD_OUT_HIGH);
	if (IS_ERR(enable_gpio))
		return dev_err_probe(&client->dev, PTR_ERR(enable_gpio),
				     "Failed to get enable GPIO\n");

	ret = devm_add_action_or_reset(&client->dev, lp8864_disable_gpio, enable_gpio);
	if (ret)
		return ret;

	led->client = client;
	led->led_dev.brightness_set_blocking = lp8864_brightness_set;
	led->led_dev.brightness_get = lp8864_brightness_get;

	led->regmap = devm_regmap_init_i2c(client, &lp8864_regmap_config);
	if (IS_ERR(led->regmap))
		return dev_err_probe(&client->dev, PTR_ERR(led->regmap),
				     "Failed to allocate regmap\n");

	/* Control brightness by DISPLAY_BRT register */
	ret = regmap_update_bits(led->regmap, LP8864_USER_CONFIG1, LP8864_BRT_MODE_MASK,
								   LP8864_BRT_MODE_REG);
	if (ret) {
		dev_err(&led->client->dev, "Failed to set brightness control mode\n");
		return ret;
	}

	ret = lp8864_fault_check(led);
	if (ret)
		return ret;

	init_data.fwnode = of_fwnode_handle(child_node);
	init_data.devicename = "lp8864";
	init_data.default_label = ":display_cluster";

	ret = devm_led_classdev_register_ext(&client->dev, &led->led_dev, &init_data);
	if (ret)
		dev_err(&client->dev, "Failed to register LED device (%pe)\n", ERR_PTR(ret));

	return ret;
}

static const struct i2c_device_id lp8864_id[] = {
	{ "lp8864" },
	{}
};
MODULE_DEVICE_TABLE(i2c, lp8864_id);

static const struct of_device_id of_lp8864_leds_match[] = {
	{ .compatible = "ti,lp8864" },
	{}
};
MODULE_DEVICE_TABLE(of, of_lp8864_leds_match);

static struct i2c_driver lp8864_driver = {
	.driver = {
		.name	= "lp8864",
		.of_match_table = of_lp8864_leds_match,
	},
	.probe		= lp8864_probe,
	.id_table	= lp8864_id,
};
module_i2c_driver(lp8864_driver);

MODULE_DESCRIPTION("Texas Instruments LP8864/LP8866 LED driver");
MODULE_AUTHOR("Alexander Sverdlin <alexander.sverdlin@siemens.com>");
MODULE_LICENSE("GPL");
