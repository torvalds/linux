// SPDX-License-Identifier: GPL-2.0-only
/*
 * TI LM363X Regulator Driver
 *
 * Copyright 2015 Texas Instruments
 *
 * Author: Milo Kim <milo.kim@ti.com>
 */

#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/mfd/ti-lmu.h>
#include <linux/mfd/ti-lmu-register.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/gpio/consumer.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/slab.h>

/* LM3631 */
#define LM3631_BOOST_VSEL_MAX		0x25
#define LM3631_LDO_VSEL_MAX		0x28
#define LM3631_CONT_VSEL_MAX		0x03
#define LM3631_VBOOST_MIN		4500000
#define LM3631_VCONT_MIN		1800000
#define LM3631_VLDO_MIN			4000000
#define ENABLE_TIME_USEC		1000

/* LM3632 */
#define LM3632_BOOST_VSEL_MAX		0x26
#define LM3632_LDO_VSEL_MAX		0x29
#define LM3632_VBOOST_MIN		4500000
#define LM3632_VLDO_MIN			4000000

/* LM36274 */
#define LM36274_BOOST_VSEL_MAX		0x3f
#define LM36274_LDO_VSEL_MAX		0x34
#define LM36274_VOLTAGE_MIN		4000000

/* Common */
#define LM363X_STEP_50mV		50000
#define LM363X_STEP_500mV		500000

static const int ldo_cont_enable_time[] = {
	0, 2000, 5000, 10000, 20000, 50000, 100000, 200000,
};

static int lm363x_regulator_enable_time(struct regulator_dev *rdev)
{
	enum lm363x_regulator_id id = rdev_get_id(rdev);
	unsigned int val, addr, mask;

	switch (id) {
	case LM3631_LDO_CONT:
		addr = LM3631_REG_ENTIME_VCONT;
		mask = LM3631_ENTIME_CONT_MASK;
		break;
	case LM3631_LDO_OREF:
		addr = LM3631_REG_ENTIME_VOREF;
		mask = LM3631_ENTIME_MASK;
		break;
	case LM3631_LDO_POS:
		addr = LM3631_REG_ENTIME_VPOS;
		mask = LM3631_ENTIME_MASK;
		break;
	case LM3631_LDO_NEG:
		addr = LM3631_REG_ENTIME_VNEG;
		mask = LM3631_ENTIME_MASK;
		break;
	default:
		return 0;
	}

	if (regmap_read(rdev->regmap, addr, &val))
		return -EINVAL;

	val = (val & mask) >> LM3631_ENTIME_SHIFT;

	if (id == LM3631_LDO_CONT)
		return ldo_cont_enable_time[val];
	else
		return ENABLE_TIME_USEC * val;
}

static const struct regulator_ops lm363x_boost_voltage_table_ops = {
	.list_voltage     = regulator_list_voltage_linear,
	.set_voltage_sel  = regulator_set_voltage_sel_regmap,
	.get_voltage_sel  = regulator_get_voltage_sel_regmap,
};

static const struct regulator_ops lm363x_regulator_voltage_table_ops = {
	.list_voltage     = regulator_list_voltage_linear,
	.set_voltage_sel  = regulator_set_voltage_sel_regmap,
	.get_voltage_sel  = regulator_get_voltage_sel_regmap,
	.enable           = regulator_enable_regmap,
	.disable          = regulator_disable_regmap,
	.is_enabled       = regulator_is_enabled_regmap,
	.enable_time      = lm363x_regulator_enable_time,
};

static const struct regulator_desc lm363x_regulator_desc[] = {
	/* LM3631 */
	{
		.name           = "vboost",
		.of_match	= "vboost",
		.id             = LM3631_BOOST,
		.ops            = &lm363x_boost_voltage_table_ops,
		.n_voltages     = LM3631_BOOST_VSEL_MAX + 1,
		.min_uV         = LM3631_VBOOST_MIN,
		.uV_step        = LM363X_STEP_50mV,
		.type           = REGULATOR_VOLTAGE,
		.owner          = THIS_MODULE,
		.vsel_reg       = LM3631_REG_VOUT_BOOST,
		.vsel_mask      = LM3631_VOUT_MASK,
	},
	{
		.name           = "ldo_cont",
		.of_match	= "vcont",
		.id             = LM3631_LDO_CONT,
		.ops            = &lm363x_regulator_voltage_table_ops,
		.n_voltages     = LM3631_CONT_VSEL_MAX + 1,
		.min_uV         = LM3631_VCONT_MIN,
		.uV_step        = LM363X_STEP_500mV,
		.type           = REGULATOR_VOLTAGE,
		.owner          = THIS_MODULE,
		.vsel_reg       = LM3631_REG_VOUT_CONT,
		.vsel_mask      = LM3631_VOUT_CONT_MASK,
		.enable_reg     = LM3631_REG_LDO_CTRL2,
		.enable_mask    = LM3631_EN_CONT_MASK,
	},
	{
		.name           = "ldo_oref",
		.of_match	= "voref",
		.id             = LM3631_LDO_OREF,
		.ops            = &lm363x_regulator_voltage_table_ops,
		.n_voltages     = LM3631_LDO_VSEL_MAX + 1,
		.min_uV         = LM3631_VLDO_MIN,
		.uV_step        = LM363X_STEP_50mV,
		.type           = REGULATOR_VOLTAGE,
		.owner          = THIS_MODULE,
		.vsel_reg       = LM3631_REG_VOUT_OREF,
		.vsel_mask      = LM3631_VOUT_MASK,
		.enable_reg     = LM3631_REG_LDO_CTRL1,
		.enable_mask    = LM3631_EN_OREF_MASK,
	},
	{
		.name           = "ldo_vpos",
		.of_match	= "vpos",
		.id             = LM3631_LDO_POS,
		.ops            = &lm363x_regulator_voltage_table_ops,
		.n_voltages     = LM3631_LDO_VSEL_MAX + 1,
		.min_uV         = LM3631_VLDO_MIN,
		.uV_step        = LM363X_STEP_50mV,
		.type           = REGULATOR_VOLTAGE,
		.owner          = THIS_MODULE,
		.vsel_reg       = LM3631_REG_VOUT_POS,
		.vsel_mask      = LM3631_VOUT_MASK,
		.enable_reg     = LM3631_REG_LDO_CTRL1,
		.enable_mask    = LM3631_EN_VPOS_MASK,
	},
	{
		.name           = "ldo_vneg",
		.of_match	= "vneg",
		.id             = LM3631_LDO_NEG,
		.ops            = &lm363x_regulator_voltage_table_ops,
		.n_voltages     = LM3631_LDO_VSEL_MAX + 1,
		.min_uV         = LM3631_VLDO_MIN,
		.uV_step        = LM363X_STEP_50mV,
		.type           = REGULATOR_VOLTAGE,
		.owner          = THIS_MODULE,
		.vsel_reg       = LM3631_REG_VOUT_NEG,
		.vsel_mask      = LM3631_VOUT_MASK,
		.enable_reg     = LM3631_REG_LDO_CTRL1,
		.enable_mask    = LM3631_EN_VNEG_MASK,
	},
	/* LM3632 */
	{
		.name           = "vboost",
		.of_match	= "vboost",
		.id             = LM3632_BOOST,
		.ops            = &lm363x_boost_voltage_table_ops,
		.n_voltages     = LM3632_BOOST_VSEL_MAX + 1,
		.min_uV         = LM3632_VBOOST_MIN,
		.uV_step        = LM363X_STEP_50mV,
		.type           = REGULATOR_VOLTAGE,
		.owner          = THIS_MODULE,
		.vsel_reg       = LM3632_REG_VOUT_BOOST,
		.vsel_mask      = LM3632_VOUT_MASK,
	},
	{
		.name           = "ldo_vpos",
		.of_match	= "vpos",
		.id             = LM3632_LDO_POS,
		.ops            = &lm363x_regulator_voltage_table_ops,
		.n_voltages     = LM3632_LDO_VSEL_MAX + 1,
		.min_uV         = LM3632_VLDO_MIN,
		.uV_step        = LM363X_STEP_50mV,
		.type           = REGULATOR_VOLTAGE,
		.owner          = THIS_MODULE,
		.vsel_reg       = LM3632_REG_VOUT_POS,
		.vsel_mask      = LM3632_VOUT_MASK,
		.enable_reg     = LM3632_REG_BIAS_CONFIG,
		.enable_mask    = LM3632_EN_VPOS_MASK,
	},
	{
		.name           = "ldo_vneg",
		.of_match	= "vneg",
		.id             = LM3632_LDO_NEG,
		.ops            = &lm363x_regulator_voltage_table_ops,
		.n_voltages     = LM3632_LDO_VSEL_MAX + 1,
		.min_uV         = LM3632_VLDO_MIN,
		.uV_step        = LM363X_STEP_50mV,
		.type           = REGULATOR_VOLTAGE,
		.owner          = THIS_MODULE,
		.vsel_reg       = LM3632_REG_VOUT_NEG,
		.vsel_mask      = LM3632_VOUT_MASK,
		.enable_reg     = LM3632_REG_BIAS_CONFIG,
		.enable_mask    = LM3632_EN_VNEG_MASK,
	},

	/* LM36274 */
	{
		.name           = "vboost",
		.of_match	= "vboost",
		.id             = LM36274_BOOST,
		.ops            = &lm363x_boost_voltage_table_ops,
		.n_voltages     = LM36274_BOOST_VSEL_MAX,
		.min_uV         = LM36274_VOLTAGE_MIN,
		.uV_step        = LM363X_STEP_50mV,
		.type           = REGULATOR_VOLTAGE,
		.owner          = THIS_MODULE,
		.vsel_reg       = LM36274_REG_VOUT_BOOST,
		.vsel_mask      = LM36274_VOUT_MASK,
	},
	{
		.name           = "ldo_vpos",
		.of_match	= "vpos",
		.id             = LM36274_LDO_POS,
		.ops            = &lm363x_regulator_voltage_table_ops,
		.n_voltages     = LM36274_LDO_VSEL_MAX,
		.min_uV         = LM36274_VOLTAGE_MIN,
		.uV_step        = LM363X_STEP_50mV,
		.type           = REGULATOR_VOLTAGE,
		.owner          = THIS_MODULE,
		.vsel_reg       = LM36274_REG_VOUT_POS,
		.vsel_mask      = LM36274_VOUT_MASK,
		.enable_reg     = LM36274_REG_BIAS_CONFIG_1,
		.enable_mask    = LM36274_EN_VPOS_MASK,
	},
	{
		.name           = "ldo_vneg",
		.of_match	= "vneg",
		.id             = LM36274_LDO_NEG,
		.ops            = &lm363x_regulator_voltage_table_ops,
		.n_voltages     = LM36274_LDO_VSEL_MAX,
		.min_uV         = LM36274_VOLTAGE_MIN,
		.uV_step        = LM363X_STEP_50mV,
		.type           = REGULATOR_VOLTAGE,
		.owner          = THIS_MODULE,
		.vsel_reg       = LM36274_REG_VOUT_NEG,
		.vsel_mask      = LM36274_VOUT_MASK,
		.enable_reg     = LM36274_REG_BIAS_CONFIG_1,
		.enable_mask    = LM36274_EN_VNEG_MASK,
	},
};

static struct gpio_desc *lm363x_regulator_of_get_enable_gpio(struct device *dev, int id)
{
	/*
	 * Check LCM_EN1/2_GPIO is configured.
	 * Those pins are used for enabling VPOS/VNEG LDOs.
	 * Do not use devm* here: the regulator core takes over the
	 * lifecycle management of the GPIO descriptor.
	 */
	switch (id) {
	case LM3632_LDO_POS:
	case LM36274_LDO_POS:
		return gpiod_get_index_optional(dev, "enable", 0,
				GPIOD_OUT_LOW | GPIOD_FLAGS_BIT_NONEXCLUSIVE);
	case LM3632_LDO_NEG:
	case LM36274_LDO_NEG:
		return gpiod_get_index_optional(dev, "enable", 1,
				GPIOD_OUT_LOW | GPIOD_FLAGS_BIT_NONEXCLUSIVE);
	default:
		return NULL;
	}
}

static int lm363x_regulator_set_ext_en(struct regmap *regmap, int id)
{
	int ext_en_mask = 0;

	switch (id) {
	case LM3632_LDO_POS:
	case LM3632_LDO_NEG:
		ext_en_mask = LM3632_EXT_EN_MASK;
		break;
	case LM36274_LDO_POS:
	case LM36274_LDO_NEG:
		ext_en_mask = LM36274_EXT_EN_MASK;
		break;
	default:
		return -ENODEV;
	}

	return regmap_update_bits(regmap, lm363x_regulator_desc[id].enable_reg,
				 ext_en_mask, ext_en_mask);
}

static int lm363x_regulator_probe(struct platform_device *pdev)
{
	struct ti_lmu *lmu = dev_get_drvdata(pdev->dev.parent);
	struct regmap *regmap = lmu->regmap;
	struct regulator_config cfg = { };
	struct regulator_dev *rdev;
	struct device *dev = &pdev->dev;
	int id = pdev->id;
	struct gpio_desc *gpiod;
	int ret;

	cfg.dev = dev;
	cfg.regmap = regmap;

	/*
	 * LM3632 LDOs can be controlled by external pin.
	 * Register update is required if the pin is used.
	 */
	gpiod = lm363x_regulator_of_get_enable_gpio(dev, id);
	if (IS_ERR(gpiod))
		return PTR_ERR(gpiod);

	if (gpiod) {
		cfg.ena_gpiod = gpiod;
		ret = lm363x_regulator_set_ext_en(regmap, id);
		if (ret) {
			gpiod_put(gpiod);
			dev_err(dev, "External pin err: %d\n", ret);
			return ret;
		}
	}

	rdev = devm_regulator_register(dev, &lm363x_regulator_desc[id], &cfg);
	if (IS_ERR(rdev)) {
		ret = PTR_ERR(rdev);
		dev_err(dev, "[%d] regulator register err: %d\n", id, ret);
		return ret;
	}

	return 0;
}

static struct platform_driver lm363x_regulator_driver = {
	.probe = lm363x_regulator_probe,
	.driver = {
		.name = "lm363x-regulator",
	},
};

module_platform_driver(lm363x_regulator_driver);

MODULE_DESCRIPTION("TI LM363X Regulator Driver");
MODULE_AUTHOR("Milo Kim");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:lm363x-regulator");
