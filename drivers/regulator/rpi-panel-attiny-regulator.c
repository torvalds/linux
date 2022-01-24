// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Marek Vasut <marex@denx.de>
 *
 * Based on rpi_touchscreen.c by Eric Anholt <eric@anholt.net>
 */

#include <linux/backlight.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/slab.h>

/* I2C registers of the Atmel microcontroller. */
#define REG_ID		0x80
#define REG_PORTA	0x81
#define REG_PORTA_HF	BIT(2)
#define REG_PORTA_VF	BIT(3)
#define REG_PORTB	0x82
#define REG_POWERON	0x85
#define REG_PWM		0x86

static const struct regmap_config attiny_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = REG_PWM,
	.cache_type = REGCACHE_NONE,
};

static int attiny_lcd_power_enable(struct regulator_dev *rdev)
{
	unsigned int data;

	regmap_write(rdev->regmap, REG_POWERON, 1);
	/* Wait for nPWRDWN to go low to indicate poweron is done. */
	regmap_read_poll_timeout(rdev->regmap, REG_PORTB, data,
					data & BIT(0), 10, 1000000);

	/* Default to the same orientation as the closed source
	 * firmware used for the panel.  Runtime rotation
	 * configuration will be supported using VC4's plane
	 * orientation bits.
	 */
	regmap_write(rdev->regmap, REG_PORTA, BIT(2));

	return 0;
}

static int attiny_lcd_power_disable(struct regulator_dev *rdev)
{
	regmap_write(rdev->regmap, REG_PWM, 0);
	regmap_write(rdev->regmap, REG_POWERON, 0);
	udelay(1);
	return 0;
}

static int attiny_lcd_power_is_enabled(struct regulator_dev *rdev)
{
	unsigned int data;
	int ret;

	ret = regmap_read(rdev->regmap, REG_POWERON, &data);
	if (ret < 0)
		return ret;

	if (!(data & BIT(0)))
		return 0;

	ret = regmap_read(rdev->regmap, REG_PORTB, &data);
	if (ret < 0)
		return ret;

	return data & BIT(0);
}

static const struct regulator_init_data attiny_regulator_default = {
	.constraints = {
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
	},
};

static const struct regulator_ops attiny_regulator_ops = {
	.enable = attiny_lcd_power_enable,
	.disable = attiny_lcd_power_disable,
	.is_enabled = attiny_lcd_power_is_enabled,
};

static const struct regulator_desc attiny_regulator = {
	.name	= "tc358762-power",
	.ops	= &attiny_regulator_ops,
	.type	= REGULATOR_VOLTAGE,
	.owner	= THIS_MODULE,
};

static int attiny_update_status(struct backlight_device *bl)
{
	struct regmap *regmap = bl_get_data(bl);
	int brightness = bl->props.brightness;

	if (bl->props.power != FB_BLANK_UNBLANK ||
	    bl->props.fb_blank != FB_BLANK_UNBLANK)
		brightness = 0;

	return regmap_write(regmap, REG_PWM, brightness);
}

static int attiny_get_brightness(struct backlight_device *bl)
{
	struct regmap *regmap = bl_get_data(bl);
	int ret, brightness;

	ret = regmap_read(regmap, REG_PWM, &brightness);
	if (ret)
		return ret;

	return brightness;
}

static const struct backlight_ops attiny_bl = {
	.update_status	= attiny_update_status,
	.get_brightness	= attiny_get_brightness,
};

/*
 * I2C driver interface functions
 */
static int attiny_i2c_probe(struct i2c_client *i2c,
		const struct i2c_device_id *id)
{
	struct backlight_properties props = { };
	struct regulator_config config = { };
	struct backlight_device *bl;
	struct regulator_dev *rdev;
	struct regmap *regmap;
	unsigned int data;
	int ret;

	regmap = devm_regmap_init_i2c(i2c, &attiny_regmap_config);
	if (IS_ERR(regmap)) {
		ret = PTR_ERR(regmap);
		dev_err(&i2c->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	ret = regmap_read(regmap, REG_ID, &data);
	if (ret < 0) {
		dev_err(&i2c->dev, "Failed to read REG_ID reg: %d\n", ret);
		return ret;
	}

	switch (data) {
	case 0xde: /* ver 1 */
	case 0xc3: /* ver 2 */
		break;
	default:
		dev_err(&i2c->dev, "Unknown Atmel firmware revision: 0x%02x\n", data);
		return -ENODEV;
	}

	regmap_write(regmap, REG_POWERON, 0);
	mdelay(1);

	config.dev = &i2c->dev;
	config.regmap = regmap;
	config.of_node = i2c->dev.of_node;
	config.init_data = &attiny_regulator_default;

	rdev = devm_regulator_register(&i2c->dev, &attiny_regulator, &config);
	if (IS_ERR(rdev)) {
		dev_err(&i2c->dev, "Failed to register ATTINY regulator\n");
		return PTR_ERR(rdev);
	}

	props.type = BACKLIGHT_RAW;
	props.max_brightness = 0xff;
	bl = devm_backlight_device_register(&i2c->dev, dev_name(&i2c->dev),
					    &i2c->dev, regmap, &attiny_bl,
					    &props);
	if (IS_ERR(bl))
		return PTR_ERR(bl);

	bl->props.brightness = 0xff;

	return 0;
}

static const struct of_device_id attiny_dt_ids[] = {
	{ .compatible = "raspberrypi,7inch-touchscreen-panel-regulator" },
	{},
};
MODULE_DEVICE_TABLE(of, attiny_dt_ids);

static struct i2c_driver attiny_regulator_driver = {
	.driver = {
		.name = "rpi_touchscreen_attiny",
		.of_match_table = of_match_ptr(attiny_dt_ids),
	},
	.probe = attiny_i2c_probe,
};

module_i2c_driver(attiny_regulator_driver);

MODULE_AUTHOR("Marek Vasut <marex@denx.de>");
MODULE_DESCRIPTION("Regulator device driver for Raspberry Pi 7-inch touchscreen");
MODULE_LICENSE("GPL v2");
