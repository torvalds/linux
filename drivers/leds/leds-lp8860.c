// SPDX-License-Identifier: GPL-2.0-only
/*
 * TI LP8860 4-Channel LED Driver
 *
 * Copyright (C) 2014 Texas Instruments
 *
 * Author: Dan Murphy <dmurphy@ti.com>
 */

#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/leds.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/gpio/consumer.h>
#include <linux/slab.h>

#define LP8860_DISP_CL1_BRT_MSB		0x00
#define LP8860_DISP_CL1_BRT_LSB		0x01
#define LP8860_DISP_CL1_CURR_MSB	0x02
#define LP8860_DISP_CL1_CURR_LSB	0x03
#define LP8860_CL2_BRT_MSB		0x04
#define LP8860_CL2_BRT_LSB		0x05
#define LP8860_CL2_CURRENT		0x06
#define LP8860_CL3_BRT_MSB		0x07
#define LP8860_CL3_BRT_LSB		0x08
#define LP8860_CL3_CURRENT		0x09
#define LP8860_CL4_BRT_MSB		0x0a
#define LP8860_CL4_BRT_LSB		0x0b
#define LP8860_CL4_CURRENT		0x0c
#define LP8860_CONFIG			0x0d
#define LP8860_STATUS			0x0e
#define LP8860_FAULT			0x0f
#define LP8860_LED_FAULT		0x10
#define LP8860_FAULT_CLEAR		0x11
#define LP8860_ID			0x12
#define LP8860_TEMP_MSB			0x13
#define LP8860_TEMP_LSB			0x14
#define LP8860_DISP_LED_CURR_MSB	0x15
#define LP8860_DISP_LED_CURR_LSB	0x16
#define LP8860_DISP_LED_PWM_MSB		0x17
#define LP8860_DISP_LED_PWM_LSB		0x18
#define LP8860_EEPROM_CNTRL		0x19
#define LP8860_EEPROM_UNLOCK		0x1a

#define LP8860_EEPROM_REG_0		0x60
#define LP8860_EEPROM_REG_1		0x61
#define LP8860_EEPROM_REG_2		0x62
#define LP8860_EEPROM_REG_3		0x63
#define LP8860_EEPROM_REG_4		0x64
#define LP8860_EEPROM_REG_5		0x65
#define LP8860_EEPROM_REG_6		0x66
#define LP8860_EEPROM_REG_7		0x67
#define LP8860_EEPROM_REG_8		0x68
#define LP8860_EEPROM_REG_9		0x69
#define LP8860_EEPROM_REG_10		0x6a
#define LP8860_EEPROM_REG_11		0x6b
#define LP8860_EEPROM_REG_12		0x6c
#define LP8860_EEPROM_REG_13		0x6d
#define LP8860_EEPROM_REG_14		0x6e
#define LP8860_EEPROM_REG_15		0x6f
#define LP8860_EEPROM_REG_16		0x70
#define LP8860_EEPROM_REG_17		0x71
#define LP8860_EEPROM_REG_18		0x72
#define LP8860_EEPROM_REG_19		0x73
#define LP8860_EEPROM_REG_20		0x74
#define LP8860_EEPROM_REG_21		0x75
#define LP8860_EEPROM_REG_22		0x76
#define LP8860_EEPROM_REG_23		0x77
#define LP8860_EEPROM_REG_24		0x78

#define LP8860_LOCK_EEPROM		0x00
#define LP8860_UNLOCK_EEPROM		0x01
#define LP8860_PROGRAM_EEPROM		0x02
#define LP8860_EEPROM_CODE_1		0x08
#define LP8860_EEPROM_CODE_2		0xba
#define LP8860_EEPROM_CODE_3		0xef

#define LP8860_CLEAR_FAULTS		0x01

#define LP8860_NAME			"lp8860"

/**
 * struct lp8860_led
 * @lock: Lock for reading/writing the device
 * @client: Pointer to the I2C client
 * @led_dev: led class device pointer
 * @regmap: Devices register map
 * @eeprom_regmap: EEPROM register map
 */
struct lp8860_led {
	struct mutex lock;
	struct i2c_client *client;
	struct led_classdev led_dev;
	struct regmap *regmap;
	struct regmap *eeprom_regmap;
};

static const struct reg_sequence lp8860_eeprom_disp_regs[] = {
	{ LP8860_EEPROM_REG_0, 0xed },
	{ LP8860_EEPROM_REG_1, 0xdf },
	{ LP8860_EEPROM_REG_2, 0xdc },
	{ LP8860_EEPROM_REG_3, 0xf0 },
	{ LP8860_EEPROM_REG_4, 0xdf },
	{ LP8860_EEPROM_REG_5, 0xe5 },
	{ LP8860_EEPROM_REG_6, 0xf2 },
	{ LP8860_EEPROM_REG_7, 0x77 },
	{ LP8860_EEPROM_REG_8, 0x77 },
	{ LP8860_EEPROM_REG_9, 0x71 },
	{ LP8860_EEPROM_REG_10, 0x3f },
	{ LP8860_EEPROM_REG_11, 0xb7 },
	{ LP8860_EEPROM_REG_12, 0x17 },
	{ LP8860_EEPROM_REG_13, 0xef },
	{ LP8860_EEPROM_REG_14, 0xb0 },
	{ LP8860_EEPROM_REG_15, 0x87 },
	{ LP8860_EEPROM_REG_16, 0xce },
	{ LP8860_EEPROM_REG_17, 0x72 },
	{ LP8860_EEPROM_REG_18, 0xe5 },
	{ LP8860_EEPROM_REG_19, 0xdf },
	{ LP8860_EEPROM_REG_20, 0x35 },
	{ LP8860_EEPROM_REG_21, 0x06 },
	{ LP8860_EEPROM_REG_22, 0xdc },
	{ LP8860_EEPROM_REG_23, 0x88 },
	{ LP8860_EEPROM_REG_24, 0x3E },
};

static int lp8860_unlock_eeprom(struct lp8860_led *led)
{
	int ret;

	guard(mutex)(&led->lock);

	ret = regmap_write(led->regmap, LP8860_EEPROM_UNLOCK, LP8860_EEPROM_CODE_1);
	if (ret) {
		dev_err(&led->client->dev, "EEPROM Unlock failed\n");
		return ret;
	}

	ret = regmap_write(led->regmap, LP8860_EEPROM_UNLOCK, LP8860_EEPROM_CODE_2);
	if (ret) {
		dev_err(&led->client->dev, "EEPROM Unlock failed\n");
		return ret;
	}
	ret = regmap_write(led->regmap, LP8860_EEPROM_UNLOCK, LP8860_EEPROM_CODE_3);
	if (ret) {
		dev_err(&led->client->dev, "EEPROM Unlock failed\n");
		return ret;
	}

	return ret;
}

static int lp8860_fault_check(struct lp8860_led *led)
{
	int ret, fault;
	unsigned int read_buf;

	ret = regmap_read(led->regmap, LP8860_LED_FAULT, &read_buf);
	if (ret)
		goto out;

	fault = read_buf;

	ret = regmap_read(led->regmap, LP8860_FAULT, &read_buf);
	if (ret)
		goto out;

	fault |= read_buf;

	/* Attempt to clear any faults */
	if (fault)
		ret = regmap_write(led->regmap, LP8860_FAULT_CLEAR,
			LP8860_CLEAR_FAULTS);
out:
	return ret;
}

static int lp8860_brightness_set(struct led_classdev *led_cdev,
				enum led_brightness brt_val)
{
	struct lp8860_led *led =
			container_of(led_cdev, struct lp8860_led, led_dev);
	int disp_brightness = brt_val * 255;
	int ret;

	guard(mutex)(&led->lock);

	ret = lp8860_fault_check(led);
	if (ret) {
		dev_err(&led->client->dev, "Cannot read/clear faults\n");
		return ret;
	}

	ret = regmap_write(led->regmap, LP8860_DISP_CL1_BRT_MSB,
			(disp_brightness & 0xff00) >> 8);
	if (ret) {
		dev_err(&led->client->dev, "Cannot write CL1 MSB\n");
		return ret;
	}

	ret = regmap_write(led->regmap, LP8860_DISP_CL1_BRT_LSB,
			disp_brightness & 0xff);
	if (ret) {
		dev_err(&led->client->dev, "Cannot write CL1 LSB\n");
		return ret;
	}

	return 0;
}

static int lp8860_init(struct lp8860_led *led)
{
	unsigned int read_buf;
	int ret, reg_count;

	ret = lp8860_fault_check(led);
	if (ret)
		goto out;

	ret = regmap_read(led->regmap, LP8860_STATUS, &read_buf);
	if (ret)
		goto out;

	ret = lp8860_unlock_eeprom(led);
	if (ret) {
		dev_err(&led->client->dev, "Failed unlocking EEPROM\n");
		goto out;
	}

	reg_count = ARRAY_SIZE(lp8860_eeprom_disp_regs);
	ret = regmap_multi_reg_write(led->eeprom_regmap, lp8860_eeprom_disp_regs, reg_count);
	if (ret) {
		dev_err(&led->client->dev, "Failed writing EEPROM\n");
		goto out;
	}

	ret = regmap_write(led->regmap, LP8860_EEPROM_UNLOCK, LP8860_LOCK_EEPROM);
	if (ret)
		goto out;

	ret = regmap_write(led->regmap,
			LP8860_EEPROM_CNTRL,
			LP8860_PROGRAM_EEPROM);
	if (ret) {
		dev_err(&led->client->dev, "Failed programming EEPROM\n");
		goto out;
	}

	return ret;

out:
	return ret;
}

static const struct regmap_config lp8860_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = LP8860_EEPROM_UNLOCK,
};

static const struct regmap_config lp8860_eeprom_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = LP8860_EEPROM_REG_24,
};

static void lp8860_disable_gpio(void *data)
{
	struct gpio_desc *gpio = data;

	gpiod_set_value(gpio, 0);
}

static int lp8860_probe(struct i2c_client *client)
{
	int ret;
	struct lp8860_led *led;
	struct device_node *np = dev_of_node(&client->dev);
	struct device_node *child_node;
	struct led_init_data init_data = {};
	struct gpio_desc *enable_gpio;

	led = devm_kzalloc(&client->dev, sizeof(*led), GFP_KERNEL);
	if (!led)
		return -ENOMEM;

	child_node = of_get_next_available_child(np, NULL);
	if (!child_node)
		return -EINVAL;

	enable_gpio = devm_gpiod_get_optional(&client->dev, "enable", GPIOD_OUT_LOW);
	if (IS_ERR(enable_gpio))
		return dev_err_probe(&client->dev, PTR_ERR(enable_gpio),
				     "Failed to get enable GPIO\n");
	devm_add_action_or_reset(&client->dev, lp8860_disable_gpio, enable_gpio);

	ret = devm_regulator_get_enable_optional(&client->dev, "vled");
	if (ret && ret != -ENODEV)
		return dev_err_probe(&client->dev, ret,
				     "Failed to enable vled regulator\n");

	led->client = client;
	led->led_dev.brightness_set_blocking = lp8860_brightness_set;

	devm_mutex_init(&client->dev, &led->lock);

	led->regmap = devm_regmap_init_i2c(client, &lp8860_regmap_config);
	if (IS_ERR(led->regmap)) {
		ret = PTR_ERR(led->regmap);
		dev_err(&client->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	led->eeprom_regmap = devm_regmap_init_i2c(client, &lp8860_eeprom_regmap_config);
	if (IS_ERR(led->eeprom_regmap)) {
		ret = PTR_ERR(led->eeprom_regmap);
		dev_err(&client->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	ret = lp8860_init(led);
	if (ret)
		return ret;

	init_data.fwnode = of_fwnode_handle(child_node);
	init_data.devicename = LP8860_NAME;
	init_data.default_label = ":display_cluster";

	ret = devm_led_classdev_register_ext(&client->dev, &led->led_dev,
					     &init_data);
	if (ret) {
		dev_err(&client->dev, "led register err: %d\n", ret);
		return ret;
	}

	return 0;
}

static const struct i2c_device_id lp8860_id[] = {
	{ "lp8860" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, lp8860_id);

static const struct of_device_id of_lp8860_leds_match[] = {
	{ .compatible = "ti,lp8860", },
	{},
};
MODULE_DEVICE_TABLE(of, of_lp8860_leds_match);

static struct i2c_driver lp8860_driver = {
	.driver = {
		.name	= "lp8860",
		.of_match_table = of_lp8860_leds_match,
	},
	.probe		= lp8860_probe,
	.id_table	= lp8860_id,
};
module_i2c_driver(lp8860_driver);

MODULE_DESCRIPTION("Texas Instruments LP8860 LED driver");
MODULE_AUTHOR("Dan Murphy <dmurphy@ti.com>");
MODULE_LICENSE("GPL v2");
