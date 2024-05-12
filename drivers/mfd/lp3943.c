// SPDX-License-Identifier: GPL-2.0-only
/*
 * TI/National Semiconductor LP3943 MFD Core Driver
 *
 * Copyright 2013 Texas Instruments
 *
 * Author: Milo Kim <milo.kim@ti.com>
 *
 * Driver structure:
 *   LP3943 is an integrated device capable of driving 16 output channels.
 *   It can be used for a GPIO expander and PWM generators.
 *
 *                                   LED control    General usage for a device
 *                                   ___________   ____________________________
 *
 *   LP3943 MFD ---- GPIO expander    leds-gpio        eg) HW enable pin
 *               |
 *               --- PWM generator    leds-pwm         eg) PWM input
 *
 *   Internal two PWM channels are used for LED dimming effect.
 *   And each output pin can be used as a GPIO as well.
 *   The LED functionality can work with GPIOs or PWMs.
 *   LEDs can be controlled with legacy leds-gpio(static brightness) or
 *   leds-pwm drivers(dynamic brightness control).
 *   Alternatively, it can be used for generic GPIO and PWM controller.
 *   For example, a GPIO is HW enable pin of a device.
 *   A PWM is input pin of a backlight device.
 */

#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/mfd/core.h>
#include <linux/mfd/lp3943.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>

#define LP3943_MAX_REGISTERS		0x09

/* Register configuration for pin MUX */
static const struct lp3943_reg_cfg lp3943_mux_cfg[] = {
	/* address, mask, shift */
	{ LP3943_REG_MUX0, 0x03, 0 },
	{ LP3943_REG_MUX0, 0x0C, 2 },
	{ LP3943_REG_MUX0, 0x30, 4 },
	{ LP3943_REG_MUX0, 0xC0, 6 },
	{ LP3943_REG_MUX1, 0x03, 0 },
	{ LP3943_REG_MUX1, 0x0C, 2 },
	{ LP3943_REG_MUX1, 0x30, 4 },
	{ LP3943_REG_MUX1, 0xC0, 6 },
	{ LP3943_REG_MUX2, 0x03, 0 },
	{ LP3943_REG_MUX2, 0x0C, 2 },
	{ LP3943_REG_MUX2, 0x30, 4 },
	{ LP3943_REG_MUX2, 0xC0, 6 },
	{ LP3943_REG_MUX3, 0x03, 0 },
	{ LP3943_REG_MUX3, 0x0C, 2 },
	{ LP3943_REG_MUX3, 0x30, 4 },
	{ LP3943_REG_MUX3, 0xC0, 6 },
};

static const struct mfd_cell lp3943_devs[] = {
	{
		.name = "lp3943-pwm",
		.of_compatible = "ti,lp3943-pwm",
	},
	{
		.name = "lp3943-gpio",
		.of_compatible = "ti,lp3943-gpio",
	},
};

int lp3943_read_byte(struct lp3943 *lp3943, u8 reg, u8 *read)
{
	int ret;
	unsigned int val;

	ret = regmap_read(lp3943->regmap, reg, &val);
	if (ret < 0)
		return ret;

	*read = (u8)val;
	return 0;
}
EXPORT_SYMBOL_GPL(lp3943_read_byte);

int lp3943_write_byte(struct lp3943 *lp3943, u8 reg, u8 data)
{
	return regmap_write(lp3943->regmap, reg, data);
}
EXPORT_SYMBOL_GPL(lp3943_write_byte);

int lp3943_update_bits(struct lp3943 *lp3943, u8 reg, u8 mask, u8 data)
{
	return regmap_update_bits(lp3943->regmap, reg, mask, data);
}
EXPORT_SYMBOL_GPL(lp3943_update_bits);

static const struct regmap_config lp3943_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = LP3943_MAX_REGISTERS,
};

static int lp3943_probe(struct i2c_client *cl)
{
	struct lp3943 *lp3943;
	struct device *dev = &cl->dev;

	lp3943 = devm_kzalloc(dev, sizeof(*lp3943), GFP_KERNEL);
	if (!lp3943)
		return -ENOMEM;

	lp3943->regmap = devm_regmap_init_i2c(cl, &lp3943_regmap_config);
	if (IS_ERR(lp3943->regmap))
		return PTR_ERR(lp3943->regmap);

	lp3943->pdata = dev_get_platdata(dev);
	lp3943->dev = dev;
	lp3943->mux_cfg = lp3943_mux_cfg;
	i2c_set_clientdata(cl, lp3943);

	return devm_mfd_add_devices(dev, -1, lp3943_devs,
				    ARRAY_SIZE(lp3943_devs),
				    NULL, 0, NULL);
}

static const struct i2c_device_id lp3943_ids[] = {
	{ "lp3943", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, lp3943_ids);

#ifdef CONFIG_OF
static const struct of_device_id lp3943_of_match[] = {
	{ .compatible = "ti,lp3943", },
	{ }
};
MODULE_DEVICE_TABLE(of, lp3943_of_match);
#endif

static struct i2c_driver lp3943_driver = {
	.probe_new = lp3943_probe,
	.driver = {
		.name = "lp3943",
		.of_match_table = of_match_ptr(lp3943_of_match),
	},
	.id_table = lp3943_ids,
};

module_i2c_driver(lp3943_driver);

MODULE_DESCRIPTION("LP3943 MFD Core Driver");
MODULE_AUTHOR("Milo Kim");
MODULE_LICENSE("GPL");
