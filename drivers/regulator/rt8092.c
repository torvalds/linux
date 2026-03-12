// SPDX-License-Identifier: GPL-2.0+
//
// Copyright (c) 2025 Richtek Technology Corp.
//
// Author: ChiYuan Huang <cy_huang@richtek.com>

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>

#define RT8092_REG_MNTRPT	0x00
#define RT8092_REG_VOUTH	0x10
#define RT8092_REG_VOUTL	0x11
#define RT8092_REG_PWMMODE	0x14
#define RT8092_REG_EVENT	0x18
#define RT8092_REG_VBANKH	0x1C
#define RT8092_REG_VBANKL	0x1D
#define RT8092_REG_VBOUND	0x1E

#define RT8092_TSDEVT_MASK	BIT(7)
#define RT8092_PGEVT_MASK	BIT(0)
#define RT8092_VSEL_MASK	GENMASK(6, 0)
#define RT8092_VOUTEN_MASK	BIT(7)
#define RT8092_FPWML_MASK	BIT(7)
#define RT8092_FPWMH_MASK	BIT(6)
#define RT8092_OCPEVT_MASK	BIT(7)
#define RT8092_SCPEVT_MASK	BIT(4)
#define RT8092_VINUVEVT_MASK	BIT(1)
#define RT8092_VBANK_MASK	GENMASK(1, 0)

#define RT8092_MODE_AUTO	0
#define RT8092_MODE_FPWM	1
#define RT8092_VOUT_BASEUV	303125
#define RT8092_VOUT_STEPUV	3125
#define RT8092_VOUT_MINSEL	15
#define RT8092_NUM_VOLTS	128
#define RT8092_INITSS_US	400

static int rt8092_get_vbank_index(struct regmap *regmap, bool vsel_high, unsigned int *vbank_idx)
{
	unsigned int vbank_reg = vsel_high ? RT8092_REG_VBANKH : RT8092_REG_VBANKL;
	unsigned int index;
	int ret;

	ret = regmap_read(regmap, vbank_reg, &index);
	if (ret)
		return ret;

	*vbank_idx = FIELD_GET(RT8092_VBANK_MASK, index);
	return 0;
}

static int rt8092_set_operating_mode(struct regulator_dev *rdev, unsigned int mode)
{
	const struct regulator_desc *desc = rdev->desc;
	struct regmap *regmap = rdev_get_regmap(rdev);
	unsigned int mode_mask, mode_val;

	mode_mask = desc->vsel_reg == RT8092_REG_VOUTH ? RT8092_FPWMH_MASK : RT8092_FPWML_MASK;

	switch (mode) {
	case REGULATOR_MODE_FAST:
		mode_val = mode_mask;
		break;
	case REGULATOR_MODE_NORMAL:
		mode_val = 0;
		break;
	default:
		return -EINVAL;
	}

	return regmap_update_bits(regmap, RT8092_REG_PWMMODE, mode_mask, mode_val);
}

static unsigned int rt8092_get_operating_mode(struct regulator_dev *rdev)
{
	const struct regulator_desc *desc = rdev->desc;
	struct regmap *regmap = rdev_get_regmap(rdev);
	unsigned int mode_mask, mode_val;
	int ret;

	mode_mask = desc->vsel_reg == RT8092_REG_VOUTH ? RT8092_FPWMH_MASK : RT8092_FPWML_MASK;

	ret = regmap_read(regmap, RT8092_REG_PWMMODE, &mode_val);
	if (ret)
		return REGULATOR_MODE_INVALID;

	return mode_val & mode_mask ? REGULATOR_MODE_FAST : REGULATOR_MODE_NORMAL;
}

static int rt8092_get_error_flags(struct regulator_dev *rdev, unsigned int *flags)
{
	struct regmap *regmap = rdev_get_regmap(rdev);
	unsigned int mntrpt, evtrpt, events = 0;
	int ret;

	ret = regmap_read(regmap, RT8092_REG_MNTRPT, &mntrpt);
	if (ret)
		return ret;

	ret = regmap_read(regmap, RT8092_REG_EVENT, &evtrpt);
	if (ret)
		return ret;

	if (!(mntrpt & RT8092_PGEVT_MASK) || evtrpt & RT8092_VINUVEVT_MASK)
		events |= REGULATOR_ERROR_UNDER_VOLTAGE;

	if (mntrpt & RT8092_TSDEVT_MASK)
		events |= REGULATOR_ERROR_OVER_TEMP;

	if (evtrpt & RT8092_OCPEVT_MASK)
		events |= REGULATOR_ERROR_OVER_CURRENT;

	if (evtrpt & RT8092_SCPEVT_MASK)
		events |= REGULATOR_ERROR_FAIL;

	*flags = events;
	return 0;
}


static int rt8092_set_suspend_voltage(struct regulator_dev *rdev, int uV)
{
	const struct regulator_desc *desc = rdev->desc;
	struct regmap *regmap = rdev_get_regmap(rdev);
	unsigned int vsel_reg, vsel_val, vbank_idx;
	bool vsel_high;
	int ret;

	vsel_reg = desc->vsel_reg == RT8092_REG_VOUTH ? RT8092_REG_VOUTL : RT8092_REG_VOUTH;
	vsel_high = desc->vsel_reg == RT8092_REG_VOUTH;

	ret = rt8092_get_vbank_index(regmap, vsel_high, &vbank_idx);
	if (ret)
		return ret;

	/* VOUT  = (BASEUV + STEPUV * VSEL) * 2^vbank_idx */
	uV >>= vbank_idx;
	if (uV < RT8092_VOUT_BASEUV)
		return -EINVAL;

	vsel_val = (uV - RT8092_VOUT_BASEUV) / RT8092_VOUT_STEPUV;
	if (vsel_val < RT8092_VOUT_MINSEL || vsel_val >= RT8092_NUM_VOLTS)
		return -EINVAL;

	return regmap_update_bits(regmap, vsel_reg, RT8092_VSEL_MASK, vsel_val);
}

static int rt8092_set_suspend_enable(struct regulator_dev *rdev)
{
	const struct regulator_desc *desc = rdev->desc;
	struct regmap *regmap = rdev_get_regmap(rdev);
	unsigned int enable_reg;

	enable_reg = desc->vsel_reg == RT8092_REG_VOUTH ? RT8092_REG_VOUTL : RT8092_REG_VOUTH;
	return regmap_set_bits(regmap, enable_reg, RT8092_VOUTEN_MASK);
}

static int rt8092_set_suspend_disable(struct regulator_dev *rdev)
{
	const struct regulator_desc *desc = rdev->desc;
	struct regmap *regmap = rdev_get_regmap(rdev);
	unsigned int enable_reg;

	enable_reg = desc->vsel_reg == RT8092_REG_VOUTH ? RT8092_REG_VOUTL : RT8092_REG_VOUTH;
	return regmap_clear_bits(regmap, enable_reg, RT8092_VOUTEN_MASK);
}

static int rt8092_set_suspend_mode(struct regulator_dev *rdev, unsigned int mode)
{
	const struct regulator_desc *desc = rdev->desc;
	struct regmap *regmap = rdev_get_regmap(rdev);
	unsigned int mode_mask, mode_val;

	mode_mask = desc->vsel_reg == RT8092_REG_VOUTH ? RT8092_FPWML_MASK : RT8092_FPWMH_MASK;

	switch (mode) {
	case REGULATOR_MODE_FAST:
		mode_val = mode_mask;
		break;
	case REGULATOR_MODE_NORMAL:
		mode_val = 0;
		break;
	default:
		return -EINVAL;
	}

	return regmap_update_bits(regmap, RT8092_REG_PWMMODE, mode_mask, mode_val);
}

static const struct regulator_ops rt8092_regulator_ops = {
	.list_voltage = regulator_list_voltage_linear,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.set_mode = rt8092_set_operating_mode,
	.get_mode = rt8092_get_operating_mode,
	.get_error_flags = rt8092_get_error_flags,
	.set_suspend_voltage = rt8092_set_suspend_voltage,
	.set_suspend_enable = rt8092_set_suspend_enable,
	.set_suspend_disable = rt8092_set_suspend_disable,
	.set_suspend_mode = rt8092_set_suspend_mode,
};

static unsigned int rt8092_of_map_mode(unsigned int mode)
{
	switch (mode) {
	case RT8092_MODE_AUTO:
		return REGULATOR_MODE_NORMAL;
	case RT8092_MODE_FPWM:
		return REGULATOR_MODE_FAST;
	default:
		return REGULATOR_MODE_INVALID;
	}
}

static const struct regmap_config rt8092_regmap_cfg = {
	.name =	"rt8092",
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = RT8092_REG_VBOUND,
};

static int rt8092_probe(struct i2c_client *i2c)
{
	unsigned int vbank_idx, min_uV, step_uV;
	struct regulator_config cfg = {};
	struct device *dev = &i2c->dev;
	struct regulator_desc *desc;
	struct regulator_dev *rdev;
	struct gpio_desc *enable;
	struct regmap *regmap;
	bool vsel_high;
	int ret;

	desc = devm_kzalloc(dev, sizeof(*desc), GFP_KERNEL);
	if (!desc)
		return -ENOMEM;

	enable = devm_gpiod_get_optional(dev, "enable", GPIOD_OUT_HIGH);
	if (IS_ERR(enable))
		return dev_err_probe(dev, PTR_ERR(enable), "Failed get 'enable' gpio\n");

	regmap = devm_regmap_init_i2c(i2c, &rt8092_regmap_cfg);
	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap), "Failed to init regmap\n");

	vsel_high = device_property_read_bool(dev, "richtek,vsel-active-high");

	ret = rt8092_get_vbank_index(regmap, vsel_high, &vbank_idx);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get VOUT bank index\n");

	/*
	 * step VOUT = STEP_UV * 2^vbank_idx
	 * min VOUT  = (BASEUV + STEPUV * VMIN_SEL) * 2^vbank_idx
	 */
	step_uV = RT8092_VOUT_STEPUV << vbank_idx;
	min_uV = (RT8092_VOUT_BASEUV + RT8092_VOUT_STEPUV * RT8092_VOUT_MINSEL) << vbank_idx;

	desc->name = "rt8092";
	desc->owner = THIS_MODULE;
	desc->type = REGULATOR_VOLTAGE;
	desc->ops = &rt8092_regulator_ops;
	desc->n_voltages = RT8092_NUM_VOLTS;
	desc->min_uV = min_uV;
	desc->uV_step = step_uV;
	desc->linear_min_sel = RT8092_VOUT_MINSEL;
	desc->enable_reg = desc->vsel_reg = vsel_high ? RT8092_REG_VOUTH : RT8092_REG_VOUTL;
	desc->vsel_mask = RT8092_VSEL_MASK;
	desc->enable_mask = RT8092_VOUTEN_MASK;
	desc->enable_time = RT8092_INITSS_US;
	desc->of_map_mode = rt8092_of_map_mode;

	cfg.dev = dev;
	cfg.of_node = dev_of_node(dev);
	cfg.init_data = of_get_regulator_init_data(dev, dev_of_node(dev), desc);

	rdev = devm_regulator_register(dev, desc, &cfg);
	if (IS_ERR(rdev))
		return dev_err_probe(dev, PTR_ERR(rdev), "Failed to register regulator\n");

	return 0;
}

static const struct of_device_id rt8092_device_tables[] = {
	{ .compatible = "richtek,rt8092" },
	{}
};
MODULE_DEVICE_TABLE(of, rt8092_device_tables);

static struct i2c_driver rt8092_driver = {
	.driver = {
		.name = "rt8092",
		.of_match_table = rt8092_device_tables,
	},
	.probe = rt8092_probe,
};
module_i2c_driver(rt8092_driver);

MODULE_AUTHOR("ChiYuan Huang <cy_huang@richtek.com>");
MODULE_DESCRIPTION("Richtek RT8092 Regulator Driver");
MODULE_LICENSE("GPL");
