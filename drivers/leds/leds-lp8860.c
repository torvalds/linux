/*
 * TI LP8860 4-Channel LED Driver
 *
 * Copyright (C) 2014 Texas Instruments
 *
 * Author: Dan Murphy <dmurphy@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 */

#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/leds.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
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

#define LP8860_DISP_LED_NAME		"display_cluster"

/**
 * struct lp8860_led -
 * @lock - Lock for reading/writing the device
 * @work - Work item used to off load the brightness register writes
 * @client - Pointer to the I2C client
 * @led_dev - led class device pointer
 * @regmap - Devices register map
 * @eeprom_regmap - EEPROM register map
 * @enable_gpio - VDDIO/EN gpio to enable communication interface
 * @regulator - LED supply regulator pointer
 * @brightness - Current brightness value requested
 * @label - LED label
**/
struct lp8860_led {
	struct mutex lock;
	struct work_struct work;
	struct i2c_client *client;
	struct led_classdev led_dev;
	struct regmap *regmap;
	struct regmap *eeprom_regmap;
	struct gpio_desc *enable_gpio;
	struct regulator *regulator;
	enum led_brightness brightness;
	const char *label;
};

struct lp8860_eeprom_reg {
	uint8_t reg;
	uint8_t value;
};

static struct lp8860_eeprom_reg lp8860_eeprom_disp_regs[] = {
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

static int lp8860_unlock_eeprom(struct lp8860_led *led, int lock)
{
	int ret;

	mutex_lock(&led->lock);

	if (lock == LP8860_UNLOCK_EEPROM) {
		ret = regmap_write(led->regmap,
			LP8860_EEPROM_UNLOCK,
			LP8860_EEPROM_CODE_1);
		if (ret) {
			dev_err(&led->client->dev, "EEPROM Unlock failed\n");
			goto out;
		}

		ret = regmap_write(led->regmap,
			LP8860_EEPROM_UNLOCK,
			LP8860_EEPROM_CODE_2);
		if (ret) {
			dev_err(&led->client->dev, "EEPROM Unlock failed\n");
			goto out;
		}
		ret = regmap_write(led->regmap,
			LP8860_EEPROM_UNLOCK,
			LP8860_EEPROM_CODE_3);
		if (ret) {
			dev_err(&led->client->dev, "EEPROM Unlock failed\n");
			goto out;
		}
	} else {
		ret = regmap_write(led->regmap,
			LP8860_EEPROM_UNLOCK,
			LP8860_LOCK_EEPROM);
	}

out:
	mutex_unlock(&led->lock);
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

static void lp8860_led_brightness_work(struct work_struct *work)
{
	struct lp8860_led *led = container_of(work, struct lp8860_led, work);
	int ret;
	int disp_brightness = led->brightness * 255;

	mutex_lock(&led->lock);

	ret = lp8860_fault_check(led);
	if (ret) {
		dev_err(&led->client->dev, "Cannot read/clear faults\n");
		goto out;
	}

	ret = regmap_write(led->regmap, LP8860_DISP_CL1_BRT_MSB,
			(disp_brightness & 0xff00) >> 8);
	if (ret) {
		dev_err(&led->client->dev, "Cannot write CL1 MSB\n");
		goto out;
	}

	ret = regmap_write(led->regmap, LP8860_DISP_CL1_BRT_LSB,
			disp_brightness & 0xff);
	if (ret) {
		dev_err(&led->client->dev, "Cannot write CL1 LSB\n");
		goto out;
	}
out:
	mutex_unlock(&led->lock);
}

static void lp8860_brightness_set(struct led_classdev *led_cdev,
				enum led_brightness brt_val)
{
	struct lp8860_led *led =
			container_of(led_cdev, struct lp8860_led, led_dev);

	led->brightness = brt_val;
	schedule_work(&led->work);
}

static int lp8860_init(struct lp8860_led *led)
{
	unsigned int read_buf;
	int ret, i, reg_count;

	if (led->enable_gpio)
		gpiod_direction_output(led->enable_gpio, 1);

	ret = lp8860_fault_check(led);
	if (ret)
		goto out;

	ret = regmap_read(led->regmap, LP8860_STATUS, &read_buf);
	if (ret)
		goto out;

	ret = lp8860_unlock_eeprom(led, LP8860_UNLOCK_EEPROM);
	if (ret) {
		dev_err(&led->client->dev, "Failed unlocking EEPROM\n");
		goto out;
	}

	reg_count = ARRAY_SIZE(lp8860_eeprom_disp_regs) / sizeof(lp8860_eeprom_disp_regs[0]);
	for (i = 0; i < reg_count; i++) {
		ret = regmap_write(led->eeprom_regmap,
				lp8860_eeprom_disp_regs[i].reg,
				lp8860_eeprom_disp_regs[i].value);
		if (ret) {
			dev_err(&led->client->dev, "Failed writing EEPROM\n");
			goto out;
		}
	}

	ret = lp8860_unlock_eeprom(led, LP8860_LOCK_EEPROM);
	if (ret)
		goto out;

	ret = regmap_write(led->regmap,
			LP8860_EEPROM_CNTRL,
			LP8860_PROGRAM_EEPROM);
	if (ret)
		dev_err(&led->client->dev, "Failed programming EEPROM\n");
out:
	if (ret)
		if (led->enable_gpio)
			gpiod_direction_output(led->enable_gpio, 0);
	return ret;
}

static struct reg_default lp8860_reg_defs[] = {
	{ LP8860_DISP_CL1_BRT_MSB, 0x00},
	{ LP8860_DISP_CL1_BRT_LSB, 0x00},
	{ LP8860_DISP_CL1_CURR_MSB, 0x00},
	{ LP8860_DISP_CL1_CURR_LSB, 0x00},
	{ LP8860_CL2_BRT_MSB, 0x00},
	{ LP8860_CL2_BRT_LSB, 0x00},
	{ LP8860_CL2_CURRENT, 0x00},
	{ LP8860_CL3_BRT_MSB, 0x00},
	{ LP8860_CL3_BRT_LSB, 0x00},
	{ LP8860_CL3_CURRENT, 0x00},
	{ LP8860_CL4_BRT_MSB, 0x00},
	{ LP8860_CL4_BRT_LSB, 0x00},
	{ LP8860_CL4_CURRENT, 0x00},
	{ LP8860_CONFIG, 0x00},
	{ LP8860_FAULT_CLEAR, 0x00},
	{ LP8860_EEPROM_CNTRL, 0x80},
	{ LP8860_EEPROM_UNLOCK, 0x00},
};

static const struct regmap_config lp8860_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = LP8860_EEPROM_UNLOCK,
	.reg_defaults = lp8860_reg_defs,
	.num_reg_defaults = ARRAY_SIZE(lp8860_reg_defs),
	.cache_type = REGCACHE_NONE,
};

static struct reg_default lp8860_eeprom_defs[] = {
	{ LP8860_EEPROM_REG_0, 0x00 },
	{ LP8860_EEPROM_REG_1, 0x00 },
	{ LP8860_EEPROM_REG_2, 0x00 },
	{ LP8860_EEPROM_REG_3, 0x00 },
	{ LP8860_EEPROM_REG_4, 0x00 },
	{ LP8860_EEPROM_REG_5, 0x00 },
	{ LP8860_EEPROM_REG_6, 0x00 },
	{ LP8860_EEPROM_REG_7, 0x00 },
	{ LP8860_EEPROM_REG_8, 0x00 },
	{ LP8860_EEPROM_REG_9, 0x00 },
	{ LP8860_EEPROM_REG_10, 0x00 },
	{ LP8860_EEPROM_REG_11, 0x00 },
	{ LP8860_EEPROM_REG_12, 0x00 },
	{ LP8860_EEPROM_REG_13, 0x00 },
	{ LP8860_EEPROM_REG_14, 0x00 },
	{ LP8860_EEPROM_REG_15, 0x00 },
	{ LP8860_EEPROM_REG_16, 0x00 },
	{ LP8860_EEPROM_REG_17, 0x00 },
	{ LP8860_EEPROM_REG_18, 0x00 },
	{ LP8860_EEPROM_REG_19, 0x00 },
	{ LP8860_EEPROM_REG_20, 0x00 },
	{ LP8860_EEPROM_REG_21, 0x00 },
	{ LP8860_EEPROM_REG_22, 0x00 },
	{ LP8860_EEPROM_REG_23, 0x00 },
	{ LP8860_EEPROM_REG_24, 0x00 },
};

static const struct regmap_config lp8860_eeprom_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = LP8860_EEPROM_REG_24,
	.reg_defaults = lp8860_eeprom_defs,
	.num_reg_defaults = ARRAY_SIZE(lp8860_eeprom_defs),
	.cache_type = REGCACHE_NONE,
};

static int lp8860_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int ret;
	struct lp8860_led *led;
	struct device_node *np = client->dev.of_node;

	led = devm_kzalloc(&client->dev, sizeof(*led), GFP_KERNEL);
	if (!led)
		return -ENOMEM;

	led->label = LP8860_DISP_LED_NAME;

	if (client->dev.of_node) {
		ret = of_property_read_string(np, "label", &led->label);
		if (ret) {
			dev_err(&client->dev, "Missing label in dt\n");
			return -EINVAL;
		}
	}

	led->enable_gpio = devm_gpiod_get(&client->dev, "enable");
	if (IS_ERR(led->enable_gpio))
		led->enable_gpio = NULL;
	else
		gpiod_direction_output(led->enable_gpio, 0);

	led->regulator = devm_regulator_get(&client->dev, "vled");
	if (IS_ERR(led->regulator))
		led->regulator = NULL;

	led->client = client;
	led->led_dev.name = led->label;
	led->led_dev.max_brightness = LED_FULL;
	led->led_dev.brightness_set = lp8860_brightness_set;

	mutex_init(&led->lock);
	INIT_WORK(&led->work, lp8860_led_brightness_work);

	i2c_set_clientdata(client, led);

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

	ret = led_classdev_register(&client->dev, &led->led_dev);
	if (ret) {
		dev_err(&client->dev, "led register err: %d\n", ret);
		return ret;
	}

	return 0;
}

static int lp8860_remove(struct i2c_client *client)
{
	struct lp8860_led *led = i2c_get_clientdata(client);
	int ret;

	led_classdev_unregister(&led->led_dev);
	cancel_work_sync(&led->work);

	if (led->enable_gpio)
		gpiod_direction_output(led->enable_gpio, 0);

	if (led->regulator) {
		ret = regulator_disable(led->regulator);
		if (ret)
			dev_err(&led->client->dev,
				"Failed to disable regulator\n");
	}

	return 0;
}

static const struct i2c_device_id lp8860_id[] = {
	{ "lp8860", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, lp8860_id);

#ifdef CONFIG_OF
static const struct of_device_id of_lp8860_leds_match[] = {
	{ .compatible = "ti,lp8860", },
	{},
};
MODULE_DEVICE_TABLE(of, of_lp8860_leds_match);
#endif

static struct i2c_driver lp8860_driver = {
	.driver = {
		.name	= "lp8860",
		.of_match_table = of_match_ptr(of_lp8860_leds_match),
	},
	.probe		= lp8860_probe,
	.remove		= lp8860_remove,
	.id_table	= lp8860_id,
};
module_i2c_driver(lp8860_driver);

MODULE_DESCRIPTION("Texas Instruments LP8860 LED drvier");
MODULE_AUTHOR("Dan Murphy <dmurphy@ti.com>");
MODULE_LICENSE("GPL");
