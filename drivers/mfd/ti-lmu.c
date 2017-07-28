/*
 * TI LMU (Lighting Management Unit) Core Driver
 *
 * Copyright 2017 Texas Instruments
 *
 * Author: Milo Kim <milo.kim@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/mfd/core.h>
#include <linux/mfd/ti-lmu.h>
#include <linux/mfd/ti-lmu-register.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/slab.h>

struct ti_lmu_data {
	struct mfd_cell *cells;
	int num_cells;
	unsigned int max_register;
};

static int ti_lmu_enable_hw(struct ti_lmu *lmu, enum ti_lmu_id id)
{
	int ret;

	if (gpio_is_valid(lmu->en_gpio)) {
		ret = devm_gpio_request_one(lmu->dev, lmu->en_gpio,
					    GPIOF_OUT_INIT_HIGH, "lmu_hwen");
		if (ret) {
			dev_err(lmu->dev, "Can not request enable GPIO: %d\n",
				ret);
			return ret;
		}
	}

	/* Delay about 1ms after HW enable pin control */
	usleep_range(1000, 1500);

	/* LM3631 has additional power up sequence - enable LCD_EN bit. */
	if (id == LM3631) {
		return regmap_update_bits(lmu->regmap, LM3631_REG_DEVCTRL,
					  LM3631_LCD_EN_MASK,
					  LM3631_LCD_EN_MASK);
	}

	return 0;
}

static void ti_lmu_disable_hw(struct ti_lmu *lmu)
{
	if (gpio_is_valid(lmu->en_gpio))
		gpio_set_value(lmu->en_gpio, 0);
}

static struct mfd_cell lm3532_devices[] = {
	{
		.name          = "ti-lmu-backlight",
		.id            = LM3532,
		.of_compatible = "ti,lm3532-backlight",
	},
};

#define LM363X_REGULATOR(_id)			\
{						\
	.name          = "lm363x-regulator",	\
	.id            = _id,			\
	.of_compatible = "ti,lm363x-regulator",	\
}						\

static struct mfd_cell lm3631_devices[] = {
	LM363X_REGULATOR(LM3631_BOOST),
	LM363X_REGULATOR(LM3631_LDO_CONT),
	LM363X_REGULATOR(LM3631_LDO_OREF),
	LM363X_REGULATOR(LM3631_LDO_POS),
	LM363X_REGULATOR(LM3631_LDO_NEG),
	{
		.name          = "ti-lmu-backlight",
		.id            = LM3631,
		.of_compatible = "ti,lm3631-backlight",
	},
};

static struct mfd_cell lm3632_devices[] = {
	LM363X_REGULATOR(LM3632_BOOST),
	LM363X_REGULATOR(LM3632_LDO_POS),
	LM363X_REGULATOR(LM3632_LDO_NEG),
	{
		.name          = "ti-lmu-backlight",
		.id            = LM3632,
		.of_compatible = "ti,lm3632-backlight",
	},
};

static struct mfd_cell lm3633_devices[] = {
	{
		.name          = "ti-lmu-backlight",
		.id            = LM3633,
		.of_compatible = "ti,lm3633-backlight",
	},
	{
		.name          = "lm3633-leds",
		.of_compatible = "ti,lm3633-leds",
	},
	/* Monitoring driver for open/short circuit detection */
	{
		.name          = "ti-lmu-fault-monitor",
		.id            = LM3633,
		.of_compatible = "ti,lm3633-fault-monitor",
	},
};

static struct mfd_cell lm3695_devices[] = {
	{
		.name          = "ti-lmu-backlight",
		.id            = LM3695,
		.of_compatible = "ti,lm3695-backlight",
	},
};

static struct mfd_cell lm3697_devices[] = {
	{
		.name          = "ti-lmu-backlight",
		.id            = LM3697,
		.of_compatible = "ti,lm3697-backlight",
	},
	/* Monitoring driver for open/short circuit detection */
	{
		.name          = "ti-lmu-fault-monitor",
		.id            = LM3697,
		.of_compatible = "ti,lm3697-fault-monitor",
	},
};

#define TI_LMU_DATA(chip, max_reg)		\
static const struct ti_lmu_data chip##_data =	\
{						\
	.cells = chip##_devices,		\
	.num_cells = ARRAY_SIZE(chip##_devices),\
	.max_register = max_reg,		\
}						\

TI_LMU_DATA(lm3532, LM3532_MAX_REG);
TI_LMU_DATA(lm3631, LM3631_MAX_REG);
TI_LMU_DATA(lm3632, LM3632_MAX_REG);
TI_LMU_DATA(lm3633, LM3633_MAX_REG);
TI_LMU_DATA(lm3695, LM3695_MAX_REG);
TI_LMU_DATA(lm3697, LM3697_MAX_REG);

static const struct of_device_id ti_lmu_of_match[] = {
	{ .compatible = "ti,lm3532", .data = &lm3532_data },
	{ .compatible = "ti,lm3631", .data = &lm3631_data },
	{ .compatible = "ti,lm3632", .data = &lm3632_data },
	{ .compatible = "ti,lm3633", .data = &lm3633_data },
	{ .compatible = "ti,lm3695", .data = &lm3695_data },
	{ .compatible = "ti,lm3697", .data = &lm3697_data },
	{ }
};
MODULE_DEVICE_TABLE(of, ti_lmu_of_match);

static int ti_lmu_probe(struct i2c_client *cl, const struct i2c_device_id *id)
{
	struct device *dev = &cl->dev;
	const struct of_device_id *match;
	const struct ti_lmu_data *data;
	struct regmap_config regmap_cfg;
	struct ti_lmu *lmu;
	int ret;

	match = of_match_device(ti_lmu_of_match, dev);
	if (!match)
		return -ENODEV;
	/*
	 * Get device specific data from of_match table.
	 * This data is defined by using TI_LMU_DATA() macro.
	 */
	data = (struct ti_lmu_data *)match->data;

	lmu = devm_kzalloc(dev, sizeof(*lmu), GFP_KERNEL);
	if (!lmu)
		return -ENOMEM;

	lmu->dev = &cl->dev;

	/* Setup regmap */
	memset(&regmap_cfg, 0, sizeof(struct regmap_config));
	regmap_cfg.reg_bits = 8;
	regmap_cfg.val_bits = 8;
	regmap_cfg.name = id->name;
	regmap_cfg.max_register = data->max_register;

	lmu->regmap = devm_regmap_init_i2c(cl, &regmap_cfg);
	if (IS_ERR(lmu->regmap))
		return PTR_ERR(lmu->regmap);

	/* HW enable pin control and additional power up sequence if required */
	lmu->en_gpio = of_get_named_gpio(dev->of_node, "enable-gpios", 0);
	ret = ti_lmu_enable_hw(lmu, id->driver_data);
	if (ret)
		return ret;

	/*
	 * Fault circuit(open/short) can be detected by ti-lmu-fault-monitor.
	 * After fault detection is done, some devices should re-initialize
	 * configuration. The notifier enables such kind of handling.
	 */
	BLOCKING_INIT_NOTIFIER_HEAD(&lmu->notifier);

	i2c_set_clientdata(cl, lmu);

	return mfd_add_devices(lmu->dev, 0, data->cells,
			       data->num_cells, NULL, 0, NULL);
}

static int ti_lmu_remove(struct i2c_client *cl)
{
	struct ti_lmu *lmu = i2c_get_clientdata(cl);

	ti_lmu_disable_hw(lmu);
	mfd_remove_devices(lmu->dev);
	return 0;
}

static const struct i2c_device_id ti_lmu_ids[] = {
	{ "lm3532", LM3532 },
	{ "lm3631", LM3631 },
	{ "lm3632", LM3632 },
	{ "lm3633", LM3633 },
	{ "lm3695", LM3695 },
	{ "lm3697", LM3697 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ti_lmu_ids);

static struct i2c_driver ti_lmu_driver = {
	.probe = ti_lmu_probe,
	.remove = ti_lmu_remove,
	.driver = {
		.name = "ti-lmu",
		.of_match_table = ti_lmu_of_match,
	},
	.id_table = ti_lmu_ids,
};

module_i2c_driver(ti_lmu_driver);

MODULE_DESCRIPTION("TI LMU MFD Core Driver");
MODULE_AUTHOR("Milo Kim");
MODULE_LICENSE("GPL v2");
