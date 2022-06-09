// SPDX-License-Identifier: GPL-2.0
// TI LP50XX LED chip family driver
// Copyright (C) 2018-20 Texas Instruments Incorporated - https://www.ti.com/

#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/leds.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <uapi/linux/uleds.h>

#include <linux/led-class-multicolor.h>

#include "leds.h"

#define LP50XX_DEV_CFG0		0x00
#define LP50XX_DEV_CFG1		0x01
#define LP50XX_LED_CFG0		0x02

/* LP5009 and LP5012 registers */
#define LP5012_BNK_BRT		0x03
#define LP5012_BNKA_CLR		0x04
#define LP5012_BNKB_CLR		0x05
#define LP5012_BNKC_CLR		0x06
#define LP5012_LED0_BRT		0x07
#define LP5012_OUT0_CLR		0x0b
#define LP5012_RESET		0x17

/* LP5018 and LP5024 registers */
#define LP5024_BNK_BRT		0x03
#define LP5024_BNKA_CLR		0x04
#define LP5024_BNKB_CLR		0x05
#define LP5024_BNKC_CLR		0x06
#define LP5024_LED0_BRT		0x07
#define LP5024_OUT0_CLR		0x0f
#define LP5024_RESET		0x27

/* LP5030 and LP5036 registers */
#define LP5036_LED_CFG1		0x03
#define LP5036_BNK_BRT		0x04
#define LP5036_BNKA_CLR		0x05
#define LP5036_BNKB_CLR		0x06
#define LP5036_BNKC_CLR		0x07
#define LP5036_LED0_BRT		0x08
#define LP5036_OUT0_CLR		0x14
#define LP5036_RESET		0x38

#define LP50XX_SW_RESET		0xff
#define LP50XX_CHIP_EN		BIT(6)

/* There are 3 LED outputs per bank */
#define LP50XX_LEDS_PER_MODULE	3

#define LP5009_MAX_LED_MODULES	2
#define LP5012_MAX_LED_MODULES	4
#define LP5018_MAX_LED_MODULES	6
#define LP5024_MAX_LED_MODULES	8
#define LP5030_MAX_LED_MODULES	10
#define LP5036_MAX_LED_MODULES	12

static const struct reg_default lp5012_reg_defs[] = {
	{LP50XX_DEV_CFG0, 0x0},
	{LP50XX_DEV_CFG1, 0x3c},
	{LP50XX_LED_CFG0, 0x0},
	{LP5012_BNK_BRT, 0xff},
	{LP5012_BNKA_CLR, 0x0f},
	{LP5012_BNKB_CLR, 0x0f},
	{LP5012_BNKC_CLR, 0x0f},
	{LP5012_LED0_BRT, 0x0f},
	/* LEDX_BRT registers are all 0xff for defaults */
	{0x08, 0xff}, {0x09, 0xff}, {0x0a, 0xff},
	{LP5012_OUT0_CLR, 0x0f},
	/* OUTX_CLR registers are all 0x0 for defaults */
	{0x0c, 0x00}, {0x0d, 0x00}, {0x0e, 0x00}, {0x0f, 0x00}, {0x10, 0x00},
	{0x11, 0x00}, {0x12, 0x00}, {0x13, 0x00}, {0x14, 0x00},	{0x15, 0x00},
	{0x16, 0x00},
	{LP5012_RESET, 0x00}
};

static const struct reg_default lp5024_reg_defs[] = {
	{LP50XX_DEV_CFG0, 0x0},
	{LP50XX_DEV_CFG1, 0x3c},
	{LP50XX_LED_CFG0, 0x0},
	{LP5024_BNK_BRT, 0xff},
	{LP5024_BNKA_CLR, 0x0f},
	{LP5024_BNKB_CLR, 0x0f},
	{LP5024_BNKC_CLR, 0x0f},
	{LP5024_LED0_BRT, 0x0f},
	/* LEDX_BRT registers are all 0xff for defaults */
	{0x08, 0xff}, {0x09, 0xff}, {0x0a, 0xff}, {0x0b, 0xff}, {0x0c, 0xff},
	{0x0d, 0xff}, {0x0e, 0xff},
	{LP5024_OUT0_CLR, 0x0f},
	/* OUTX_CLR registers are all 0x0 for defaults */
	{0x10, 0x00}, {0x11, 0x00}, {0x12, 0x00}, {0x13, 0x00}, {0x14, 0x00},
	{0x15, 0x00}, {0x16, 0x00}, {0x17, 0x00}, {0x18, 0x00}, {0x19, 0x00},
	{0x1a, 0x00}, {0x1b, 0x00}, {0x1c, 0x00}, {0x1d, 0x00}, {0x1e, 0x00},
	{0x1f, 0x00}, {0x20, 0x00}, {0x21, 0x00}, {0x22, 0x00}, {0x23, 0x00},
	{0x24, 0x00}, {0x25, 0x00}, {0x26, 0x00},
	{LP5024_RESET, 0x00}
};

static const struct reg_default lp5036_reg_defs[] = {
	{LP50XX_DEV_CFG0, 0x0},
	{LP50XX_DEV_CFG1, 0x3c},
	{LP50XX_LED_CFG0, 0x0},
	{LP5036_LED_CFG1, 0x0},
	{LP5036_BNK_BRT, 0xff},
	{LP5036_BNKA_CLR, 0x0f},
	{LP5036_BNKB_CLR, 0x0f},
	{LP5036_BNKC_CLR, 0x0f},
	{LP5036_LED0_BRT, 0x0f},
	/* LEDX_BRT registers are all 0xff for defaults */
	{0x08, 0xff}, {0x09, 0xff}, {0x0a, 0xff}, {0x0b, 0xff}, {0x0c, 0xff},
	{0x0d, 0xff}, {0x0e, 0xff}, {0x0f, 0xff}, {0x10, 0xff}, {0x11, 0xff},
	{0x12, 0xff}, {0x13, 0xff},
	{LP5036_OUT0_CLR, 0x0f},
	/* OUTX_CLR registers are all 0x0 for defaults */
	{0x15, 0x00}, {0x16, 0x00}, {0x17, 0x00}, {0x18, 0x00}, {0x19, 0x00},
	{0x1a, 0x00}, {0x1b, 0x00}, {0x1c, 0x00}, {0x1d, 0x00}, {0x1e, 0x00},
	{0x1f, 0x00}, {0x20, 0x00}, {0x21, 0x00}, {0x22, 0x00}, {0x23, 0x00},
	{0x24, 0x00}, {0x25, 0x00}, {0x26, 0x00}, {0x27, 0x00}, {0x28, 0x00},
	{0x29, 0x00}, {0x2a, 0x00}, {0x2b, 0x00}, {0x2c, 0x00}, {0x2d, 0x00},
	{0x2e, 0x00}, {0x2f, 0x00}, {0x30, 0x00}, {0x31, 0x00}, {0x32, 0x00},
	{0x33, 0x00}, {0x34, 0x00}, {0x35, 0x00}, {0x36, 0x00}, {0x37, 0x00},
	{LP5036_RESET, 0x00}
};

static const struct regmap_config lp5012_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = LP5012_RESET,
	.reg_defaults = lp5012_reg_defs,
	.num_reg_defaults = ARRAY_SIZE(lp5012_reg_defs),
	.cache_type = REGCACHE_FLAT,
};

static const struct regmap_config lp5024_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = LP5024_RESET,
	.reg_defaults = lp5024_reg_defs,
	.num_reg_defaults = ARRAY_SIZE(lp5024_reg_defs),
	.cache_type = REGCACHE_FLAT,
};

static const struct regmap_config lp5036_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = LP5036_RESET,
	.reg_defaults = lp5036_reg_defs,
	.num_reg_defaults = ARRAY_SIZE(lp5036_reg_defs),
	.cache_type = REGCACHE_FLAT,
};

enum lp50xx_model {
	LP5009,
	LP5012,
	LP5018,
	LP5024,
	LP5030,
	LP5036,
};

/**
 * struct lp50xx_chip_info -
 * @lp50xx_regmap_config: regmap register configuration
 * @model_id: LED device model
 * @max_modules: total number of supported LED modules
 * @num_leds: number of LED outputs available on the device
 * @led_brightness0_reg: first brightness register of the device
 * @mix_out0_reg: first color mix register of the device
 * @bank_brt_reg: bank brightness register
 * @bank_mix_reg: color mix register
 * @reset_reg: device reset register
 */
struct lp50xx_chip_info {
	const struct regmap_config *lp50xx_regmap_config;
	int model_id;
	u8 max_modules;
	u8 num_leds;
	u8 led_brightness0_reg;
	u8 mix_out0_reg;
	u8 bank_brt_reg;
	u8 bank_mix_reg;
	u8 reset_reg;
};

static const struct lp50xx_chip_info lp50xx_chip_info_tbl[] = {
	[LP5009] = {
		.model_id = LP5009,
		.max_modules = LP5009_MAX_LED_MODULES,
		.num_leds = LP5009_MAX_LED_MODULES * LP50XX_LEDS_PER_MODULE,
		.led_brightness0_reg = LP5012_LED0_BRT,
		.mix_out0_reg = LP5012_OUT0_CLR,
		.bank_brt_reg = LP5012_BNK_BRT,
		.bank_mix_reg = LP5012_BNKA_CLR,
		.reset_reg = LP5012_RESET,
		.lp50xx_regmap_config = &lp5012_regmap_config,
	},
	[LP5012] = {
		.model_id = LP5012,
		.max_modules = LP5012_MAX_LED_MODULES,
		.num_leds = LP5012_MAX_LED_MODULES * LP50XX_LEDS_PER_MODULE,
		.led_brightness0_reg = LP5012_LED0_BRT,
		.mix_out0_reg = LP5012_OUT0_CLR,
		.bank_brt_reg = LP5012_BNK_BRT,
		.bank_mix_reg = LP5012_BNKA_CLR,
		.reset_reg = LP5012_RESET,
		.lp50xx_regmap_config = &lp5012_regmap_config,
	},
	[LP5018] = {
		.model_id = LP5018,
		.max_modules = LP5018_MAX_LED_MODULES,
		.num_leds = LP5018_MAX_LED_MODULES * LP50XX_LEDS_PER_MODULE,
		.led_brightness0_reg = LP5024_LED0_BRT,
		.mix_out0_reg = LP5024_OUT0_CLR,
		.bank_brt_reg = LP5024_BNK_BRT,
		.bank_mix_reg = LP5024_BNKA_CLR,
		.reset_reg = LP5024_RESET,
		.lp50xx_regmap_config = &lp5024_regmap_config,
	},
	[LP5024] = {
		.model_id = LP5024,
		.max_modules = LP5024_MAX_LED_MODULES,
		.num_leds = LP5024_MAX_LED_MODULES * LP50XX_LEDS_PER_MODULE,
		.led_brightness0_reg = LP5024_LED0_BRT,
		.mix_out0_reg = LP5024_OUT0_CLR,
		.bank_brt_reg = LP5024_BNK_BRT,
		.bank_mix_reg = LP5024_BNKA_CLR,
		.reset_reg = LP5024_RESET,
		.lp50xx_regmap_config = &lp5024_regmap_config,
	},
	[LP5030] = {
		.model_id = LP5030,
		.max_modules = LP5030_MAX_LED_MODULES,
		.num_leds = LP5030_MAX_LED_MODULES * LP50XX_LEDS_PER_MODULE,
		.led_brightness0_reg = LP5036_LED0_BRT,
		.mix_out0_reg = LP5036_OUT0_CLR,
		.bank_brt_reg = LP5036_BNK_BRT,
		.bank_mix_reg = LP5036_BNKA_CLR,
		.reset_reg = LP5036_RESET,
		.lp50xx_regmap_config = &lp5036_regmap_config,
	},
	[LP5036] = {
		.model_id = LP5036,
		.max_modules = LP5036_MAX_LED_MODULES,
		.num_leds = LP5036_MAX_LED_MODULES * LP50XX_LEDS_PER_MODULE,
		.led_brightness0_reg = LP5036_LED0_BRT,
		.mix_out0_reg = LP5036_OUT0_CLR,
		.bank_brt_reg = LP5036_BNK_BRT,
		.bank_mix_reg = LP5036_BNKA_CLR,
		.reset_reg = LP5036_RESET,
		.lp50xx_regmap_config = &lp5036_regmap_config,
	},
};

struct lp50xx_led {
	struct led_classdev_mc mc_cdev;
	struct lp50xx *priv;
	unsigned long bank_modules;
	u8 ctrl_bank_enabled;
	int led_number;
};

/**
 * struct lp50xx -
 * @enable_gpio: hardware enable gpio
 * @regulator: LED supply regulator pointer
 * @client: pointer to the I2C client
 * @regmap: device register map
 * @dev: pointer to the devices device struct
 * @lock: lock for reading/writing the device
 * @chip_info: chip specific information (ie num_leds)
 * @num_of_banked_leds: holds the number of banked LEDs
 * @leds: array of LED strings
 */
struct lp50xx {
	struct gpio_desc *enable_gpio;
	struct regulator *regulator;
	struct i2c_client *client;
	struct regmap *regmap;
	struct device *dev;
	struct mutex lock;
	const struct lp50xx_chip_info *chip_info;
	int num_of_banked_leds;

	/* This needs to be at the end of the struct */
	struct lp50xx_led leds[];
};

static struct lp50xx_led *mcled_cdev_to_led(struct led_classdev_mc *mc_cdev)
{
	return container_of(mc_cdev, struct lp50xx_led, mc_cdev);
}

static int lp50xx_brightness_set(struct led_classdev *cdev,
			     enum led_brightness brightness)
{
	struct led_classdev_mc *mc_dev = lcdev_to_mccdev(cdev);
	struct lp50xx_led *led = mcled_cdev_to_led(mc_dev);
	const struct lp50xx_chip_info *led_chip = led->priv->chip_info;
	u8 led_offset, reg_val;
	int ret = 0;
	int i;

	mutex_lock(&led->priv->lock);
	if (led->ctrl_bank_enabled)
		reg_val = led_chip->bank_brt_reg;
	else
		reg_val = led_chip->led_brightness0_reg +
			  led->led_number;

	ret = regmap_write(led->priv->regmap, reg_val, brightness);
	if (ret) {
		dev_err(led->priv->dev,
			"Cannot write brightness value %d\n", ret);
		goto out;
	}

	for (i = 0; i < led->mc_cdev.num_colors; i++) {
		if (led->ctrl_bank_enabled) {
			reg_val = led_chip->bank_mix_reg + i;
		} else {
			led_offset = (led->led_number * 3) + i;
			reg_val = led_chip->mix_out0_reg + led_offset;
		}

		ret = regmap_write(led->priv->regmap, reg_val,
				   mc_dev->subled_info[i].intensity);
		if (ret) {
			dev_err(led->priv->dev,
				"Cannot write intensity value %d\n", ret);
			goto out;
		}
	}
out:
	mutex_unlock(&led->priv->lock);
	return ret;
}

static int lp50xx_set_banks(struct lp50xx *priv, u32 led_banks[])
{
	u8 led_config_lo, led_config_hi;
	u32 bank_enable_mask = 0;
	int ret;
	int i;

	for (i = 0; i < priv->chip_info->max_modules; i++) {
		if (led_banks[i])
			bank_enable_mask |= (1 << led_banks[i]);
	}

	led_config_lo = bank_enable_mask;
	led_config_hi = bank_enable_mask >> 8;

	ret = regmap_write(priv->regmap, LP50XX_LED_CFG0, led_config_lo);
	if (ret)
		return ret;

	if (priv->chip_info->model_id >= LP5030)
		ret = regmap_write(priv->regmap, LP5036_LED_CFG1, led_config_hi);

	return ret;
}

static int lp50xx_reset(struct lp50xx *priv)
{
	return regmap_write(priv->regmap, priv->chip_info->reset_reg, LP50XX_SW_RESET);
}

static int lp50xx_enable_disable(struct lp50xx *priv, int enable_disable)
{
	int ret;

	ret = gpiod_direction_output(priv->enable_gpio, enable_disable);
	if (ret)
		return ret;

	if (enable_disable)
		return regmap_write(priv->regmap, LP50XX_DEV_CFG0, LP50XX_CHIP_EN);
	else
		return regmap_write(priv->regmap, LP50XX_DEV_CFG0, 0);

}

static int lp50xx_probe_leds(struct fwnode_handle *child, struct lp50xx *priv,
			     struct lp50xx_led *led, int num_leds)
{
	u32 led_banks[LP5036_MAX_LED_MODULES] = {0};
	int led_number;
	int ret;

	if (num_leds > 1) {
		if (num_leds > priv->chip_info->max_modules) {
			dev_err(priv->dev, "reg property is invalid\n");
			return -EINVAL;
		}

		priv->num_of_banked_leds = num_leds;

		ret = fwnode_property_read_u32_array(child, "reg", led_banks, num_leds);
		if (ret) {
			dev_err(priv->dev, "reg property is missing\n");
			return ret;
		}

		ret = lp50xx_set_banks(priv, led_banks);
		if (ret) {
			dev_err(priv->dev, "Cannot setup banked LEDs\n");
			return ret;
		}

		led->ctrl_bank_enabled = 1;
	} else {
		ret = fwnode_property_read_u32(child, "reg", &led_number);
		if (ret) {
			dev_err(priv->dev, "led reg property missing\n");
			return ret;
		}

		if (led_number > priv->chip_info->num_leds) {
			dev_err(priv->dev, "led-sources property is invalid\n");
			return -EINVAL;
		}

		led->led_number = led_number;
	}

	return 0;
}

static int lp50xx_probe_dt(struct lp50xx *priv)
{
	struct fwnode_handle *child = NULL;
	struct fwnode_handle *led_node = NULL;
	struct led_init_data init_data = {};
	struct led_classdev *led_cdev;
	struct mc_subled *mc_led_info;
	struct lp50xx_led *led;
	int ret = -EINVAL;
	int num_colors;
	u32 color_id;
	int i = 0;

	priv->enable_gpio = devm_gpiod_get_optional(priv->dev, "enable", GPIOD_OUT_LOW);
	if (IS_ERR(priv->enable_gpio))
		return dev_err_probe(priv->dev, PTR_ERR(priv->enable_gpio),
				     "Failed to get enable GPIO\n");

	priv->regulator = devm_regulator_get(priv->dev, "vled");
	if (IS_ERR(priv->regulator))
		priv->regulator = NULL;

	device_for_each_child_node(priv->dev, child) {
		led = &priv->leds[i];
		ret = fwnode_property_count_u32(child, "reg");
		if (ret < 0) {
			dev_err(priv->dev, "reg property is invalid\n");
			goto child_out;
		}

		ret = lp50xx_probe_leds(child, priv, led, ret);
		if (ret)
			goto child_out;

		init_data.fwnode = child;
		num_colors = 0;

		/*
		 * There are only 3 LEDs per module otherwise they should be
		 * banked which also is presented as 3 LEDs.
		 */
		mc_led_info = devm_kcalloc(priv->dev, LP50XX_LEDS_PER_MODULE,
					   sizeof(*mc_led_info), GFP_KERNEL);
		if (!mc_led_info) {
			ret = -ENOMEM;
			goto child_out;
		}

		fwnode_for_each_child_node(child, led_node) {
			ret = fwnode_property_read_u32(led_node, "color",
						       &color_id);
			if (ret) {
				fwnode_handle_put(led_node);
				dev_err(priv->dev, "Cannot read color\n");
				goto child_out;
			}

			mc_led_info[num_colors].color_index = color_id;
			num_colors++;
		}

		led->priv = priv;
		led->mc_cdev.num_colors = num_colors;
		led->mc_cdev.subled_info = mc_led_info;
		led_cdev = &led->mc_cdev.led_cdev;
		led_cdev->brightness_set_blocking = lp50xx_brightness_set;

		ret = devm_led_classdev_multicolor_register_ext(priv->dev,
						       &led->mc_cdev,
						       &init_data);
		if (ret) {
			dev_err(priv->dev, "led register err: %d\n", ret);
			goto child_out;
		}
		i++;
	}

	return 0;

child_out:
	fwnode_handle_put(child);
	return ret;
}

static int lp50xx_probe(struct i2c_client *client)
{
	struct lp50xx *led;
	int count;
	int ret;

	count = device_get_child_node_count(&client->dev);
	if (!count) {
		dev_err(&client->dev, "LEDs are not defined in device tree!");
		return -ENODEV;
	}

	led = devm_kzalloc(&client->dev, struct_size(led, leds, count),
			   GFP_KERNEL);
	if (!led)
		return -ENOMEM;

	mutex_init(&led->lock);
	led->client = client;
	led->dev = &client->dev;
	led->chip_info = device_get_match_data(&client->dev);
	i2c_set_clientdata(client, led);
	led->regmap = devm_regmap_init_i2c(client,
					led->chip_info->lp50xx_regmap_config);
	if (IS_ERR(led->regmap)) {
		ret = PTR_ERR(led->regmap);
		dev_err(&client->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	ret = lp50xx_reset(led);
	if (ret)
		return ret;

	ret = lp50xx_enable_disable(led, 1);
	if (ret)
		return ret;

	return lp50xx_probe_dt(led);
}

static int lp50xx_remove(struct i2c_client *client)
{
	struct lp50xx *led = i2c_get_clientdata(client);
	int ret;

	ret = lp50xx_enable_disable(led, 0);
	if (ret)
		dev_err(led->dev, "Failed to disable chip\n");

	if (led->regulator) {
		ret = regulator_disable(led->regulator);
		if (ret)
			dev_err(led->dev, "Failed to disable regulator\n");
	}

	mutex_destroy(&led->lock);

	return 0;
}

static const struct i2c_device_id lp50xx_id[] = {
	{ "lp5009", (kernel_ulong_t)&lp50xx_chip_info_tbl[LP5009] },
	{ "lp5012", (kernel_ulong_t)&lp50xx_chip_info_tbl[LP5012] },
	{ "lp5018", (kernel_ulong_t)&lp50xx_chip_info_tbl[LP5018] },
	{ "lp5024", (kernel_ulong_t)&lp50xx_chip_info_tbl[LP5024] },
	{ "lp5030", (kernel_ulong_t)&lp50xx_chip_info_tbl[LP5030] },
	{ "lp5036", (kernel_ulong_t)&lp50xx_chip_info_tbl[LP5036] },
	{ }
};
MODULE_DEVICE_TABLE(i2c, lp50xx_id);

static const struct of_device_id of_lp50xx_leds_match[] = {
	{ .compatible = "ti,lp5009", .data = &lp50xx_chip_info_tbl[LP5009] },
	{ .compatible = "ti,lp5012", .data = &lp50xx_chip_info_tbl[LP5012] },
	{ .compatible = "ti,lp5018", .data = &lp50xx_chip_info_tbl[LP5018] },
	{ .compatible = "ti,lp5024", .data = &lp50xx_chip_info_tbl[LP5024] },
	{ .compatible = "ti,lp5030", .data = &lp50xx_chip_info_tbl[LP5030] },
	{ .compatible = "ti,lp5036", .data = &lp50xx_chip_info_tbl[LP5036] },
	{}
};
MODULE_DEVICE_TABLE(of, of_lp50xx_leds_match);

static struct i2c_driver lp50xx_driver = {
	.driver = {
		.name	= "lp50xx",
		.of_match_table = of_lp50xx_leds_match,
	},
	.probe_new	= lp50xx_probe,
	.remove		= lp50xx_remove,
	.id_table	= lp50xx_id,
};
module_i2c_driver(lp50xx_driver);

MODULE_DESCRIPTION("Texas Instruments LP50XX LED driver");
MODULE_AUTHOR("Dan Murphy <dmurphy@ti.com>");
MODULE_LICENSE("GPL v2");
