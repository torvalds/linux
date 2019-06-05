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
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/mfd/core.h>
#include <linux/mfd/ti-lmu.h>
#include <linux/mfd/ti-lmu-register.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/slab.h>

struct ti_lmu_data {
	const struct mfd_cell *cells;
	int num_cells;
	unsigned int max_register;
};

static int ti_lmu_enable_hw(struct ti_lmu *lmu, enum ti_lmu_id id)
{
	if (lmu->en_gpio)
		gpiod_set_value(lmu->en_gpio, 1);

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

static void ti_lmu_disable_hw(void *data)
{
	struct ti_lmu *lmu = data;
	if (lmu->en_gpio)
		gpiod_set_value(lmu->en_gpio, 0);
}

#define LM363X_REGULATOR(_id)			\
{						\
	.name          = "lm363x-regulator",	\
	.id            = _id,			\
	.of_compatible = "ti,lm363x-regulator",	\
}						\

static const struct mfd_cell lm3631_devices[] = {
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

static const struct mfd_cell lm3632_devices[] = {
	LM363X_REGULATOR(LM3632_BOOST),
	LM363X_REGULATOR(LM3632_LDO_POS),
	LM363X_REGULATOR(LM3632_LDO_NEG),
	{
		.name          = "ti-lmu-backlight",
		.id            = LM3632,
		.of_compatible = "ti,lm3632-backlight",
	},
};

static const struct mfd_cell lm3633_devices[] = {
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

static const struct mfd_cell lm3695_devices[] = {
	{
		.name          = "ti-lmu-backlight",
		.id            = LM3695,
		.of_compatible = "ti,lm3695-backlight",
	},
};

static const struct mfd_cell lm36274_devices[] = {
	LM363X_REGULATOR(LM36274_BOOST),
	LM363X_REGULATOR(LM36274_LDO_POS),
	LM363X_REGULATOR(LM36274_LDO_NEG),
	{
		.name          = "lm36274-leds",
		.id            = LM36274,
		.of_compatible = "ti,lm36274-backlight",
	},
};

#define TI_LMU_DATA(chip, max_reg)		\
static const struct ti_lmu_data chip##_data =	\
{						\
	.cells = chip##_devices,		\
	.num_cells = ARRAY_SIZE(chip##_devices),\
	.max_register = max_reg,		\
}						\

TI_LMU_DATA(lm3631, LM3631_MAX_REG);
TI_LMU_DATA(lm3632, LM3632_MAX_REG);
TI_LMU_DATA(lm3633, LM3633_MAX_REG);
TI_LMU_DATA(lm3695, LM3695_MAX_REG);
TI_LMU_DATA(lm36274, LM36274_MAX_REG);

static int ti_lmu_probe(struct i2c_client *cl, const struct i2c_device_id *id)
{
	struct device *dev = &cl->dev;
	const struct ti_lmu_data *data;
	struct regmap_config regmap_cfg;
	struct ti_lmu *lmu;
	int ret;

	/*
	 * Get device specific data from of_match table.
	 * This data is defined by using TI_LMU_DATA() macro.
	 */
	data = of_device_get_match_data(dev);
	if (!data)
		return -ENODEV;

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
	lmu->en_gpio = devm_gpiod_get_optional(dev, "enable", GPIOD_OUT_HIGH);
	if (IS_ERR(lmu->en_gpio)) {
		ret = PTR_ERR(lmu->en_gpio);
		dev_err(dev, "Can not request enable GPIO: %d\n", ret);
		return ret;
	}

	ret = ti_lmu_enable_hw(lmu, id->driver_data);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(dev, ti_lmu_disable_hw, lmu);
	if (ret)
		return ret;

	/*
	 * Fault circuit(open/short) can be detected by ti-lmu-fault-monitor.
	 * After fault detection is done, some devices should re-initialize
	 * configuration. The notifier enables such kind of handling.
	 */
	BLOCKING_INIT_NOTIFIER_HEAD(&lmu->notifier);

	i2c_set_clientdata(cl, lmu);

	return devm_mfd_add_devices(lmu->dev, 0, data->cells,
				    data->num_cells, NULL, 0, NULL);
}

static const struct of_device_id ti_lmu_of_match[] = {
	{ .compatible = "ti,lm3631", .data = &lm3631_data },
	{ .compatible = "ti,lm3632", .data = &lm3632_data },
	{ .compatible = "ti,lm3633", .data = &lm3633_data },
	{ .compatible = "ti,lm3695", .data = &lm3695_data },
	{ .compatible = "ti,lm36274", .data = &lm36274_data },
	{ }
};
MODULE_DEVICE_TABLE(of, ti_lmu_of_match);

static const struct i2c_device_id ti_lmu_ids[] = {
	{ "lm3631", LM3631 },
	{ "lm3632", LM3632 },
	{ "lm3633", LM3633 },
	{ "lm3695", LM3695 },
	{ "lm36274", LM36274 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ti_lmu_ids);

static struct i2c_driver ti_lmu_driver = {
	.probe = ti_lmu_probe,
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
