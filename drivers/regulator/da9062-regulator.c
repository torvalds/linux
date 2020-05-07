// SPDX-License-Identifier: GPL-2.0+
//
// Regulator device driver for DA9061 and DA9062.
// Copyright (C) 2015-2017  Dialog Semiconductor

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/mfd/da9062/core.h>
#include <linux/mfd/da9062/registers.h>
#include <dt-bindings/regulator/dlg,da9063-regulator.h>

/* Regulator IDs */
enum {
	DA9061_ID_BUCK1,
	DA9061_ID_BUCK2,
	DA9061_ID_BUCK3,
	DA9061_ID_LDO1,
	DA9061_ID_LDO2,
	DA9061_ID_LDO3,
	DA9061_ID_LDO4,
	DA9061_MAX_REGULATORS,
};

enum {
	DA9062_ID_BUCK1,
	DA9062_ID_BUCK2,
	DA9062_ID_BUCK3,
	DA9062_ID_BUCK4,
	DA9062_ID_LDO1,
	DA9062_ID_LDO2,
	DA9062_ID_LDO3,
	DA9062_ID_LDO4,
	DA9062_MAX_REGULATORS,
};

/* Regulator capabilities and registers description */
struct da9062_regulator_info {
	struct regulator_desc desc;
	/* Main register fields */
	struct reg_field mode;
	struct reg_field suspend;
	struct reg_field sleep;
	struct reg_field suspend_sleep;
	unsigned int suspend_vsel_reg;
	/* Event detection bit */
	struct reg_field oc_event;
};

/* Single regulator settings */
struct da9062_regulator {
	struct regulator_desc			desc;
	struct regulator_dev			*rdev;
	struct da9062				*hw;
	const struct da9062_regulator_info	*info;

	struct regmap_field			*mode;
	struct regmap_field			*suspend;
	struct regmap_field			*sleep;
	struct regmap_field			*suspend_sleep;
};

/* Encapsulates all information for the regulators driver */
struct da9062_regulators {
	int					irq_ldo_lim;
	unsigned				n_regulators;
	/* Array size to be defined during init. Keep at end. */
	struct da9062_regulator			regulator[];
};

/* Regulator operations */

/* Current limits array (in uA)
 * - DA9061_ID_[BUCK1|BUCK3]
 * - DA9062_ID_[BUCK1|BUCK2|BUCK4]
 * Entry indexes corresponds to register values.
 */
static const unsigned int da9062_buck_a_limits[] = {
	 500000,  600000,  700000,  800000,  900000, 1000000, 1100000, 1200000,
	1300000, 1400000, 1500000, 1600000, 1700000, 1800000, 1900000, 2000000
};

/* Current limits array (in uA)
 * - DA9061_ID_BUCK2
 * - DA9062_ID_BUCK3
 * Entry indexes corresponds to register values.
 */
static const unsigned int da9062_buck_b_limits[] = {
	1500000, 1600000, 1700000, 1800000, 1900000, 2000000, 2100000, 2200000,
	2300000, 2400000, 2500000, 2600000, 2700000, 2800000, 2900000, 3000000
};

static unsigned int da9062_map_buck_mode(unsigned int mode)
{
	switch (mode) {
	case DA9063_BUCK_MODE_SLEEP:
		return REGULATOR_MODE_STANDBY;
	case DA9063_BUCK_MODE_SYNC:
		return REGULATOR_MODE_FAST;
	case DA9063_BUCK_MODE_AUTO:
		return REGULATOR_MODE_NORMAL;
	default:
		return REGULATOR_MODE_INVALID;
	}
}

static int da9062_buck_set_mode(struct regulator_dev *rdev, unsigned mode)
{
	struct da9062_regulator *regl = rdev_get_drvdata(rdev);
	unsigned val;

	switch (mode) {
	case REGULATOR_MODE_FAST:
		val = DA9063_BUCK_MODE_SYNC;
		break;
	case REGULATOR_MODE_NORMAL:
		val = DA9063_BUCK_MODE_AUTO;
		break;
	case REGULATOR_MODE_STANDBY:
		val = DA9063_BUCK_MODE_SLEEP;
		break;
	default:
		return -EINVAL;
	}

	return regmap_field_write(regl->mode, val);
}

/*
 * Bucks use single mode register field for normal operation
 * and suspend state.
 * There are 3 modes to map to: FAST, NORMAL, and STANDBY.
 */

static unsigned da9062_buck_get_mode(struct regulator_dev *rdev)
{
	struct da9062_regulator *regl = rdev_get_drvdata(rdev);
	unsigned int val;
	int ret;

	ret = regmap_field_read(regl->mode, &val);
	if (ret < 0)
		return ret;

	switch (val) {
	default:
		/* Sleep flag bit decides the mode */
		break;
	case DA9063_BUCK_MODE_SLEEP:
		return REGULATOR_MODE_STANDBY;
	case DA9063_BUCK_MODE_SYNC:
		return REGULATOR_MODE_FAST;
	case DA9063_BUCK_MODE_AUTO:
		return REGULATOR_MODE_NORMAL;
	}

	ret = regmap_field_read(regl->sleep, &val);
	if (ret < 0)
		return 0;

	if (val)
		return REGULATOR_MODE_STANDBY;
	else
		return REGULATOR_MODE_FAST;
}

/*
 * LDOs use sleep flags - one for normal and one for suspend state.
 * There are 2 modes to map to: NORMAL and STANDBY (sleep) for each state.
 */

static int da9062_ldo_set_mode(struct regulator_dev *rdev, unsigned mode)
{
	struct da9062_regulator *regl = rdev_get_drvdata(rdev);
	unsigned val;

	switch (mode) {
	case REGULATOR_MODE_NORMAL:
		val = 0;
		break;
	case REGULATOR_MODE_STANDBY:
		val = 1;
		break;
	default:
		return -EINVAL;
	}

	return regmap_field_write(regl->sleep, val);
}

static unsigned da9062_ldo_get_mode(struct regulator_dev *rdev)
{
	struct da9062_regulator *regl = rdev_get_drvdata(rdev);
	int ret, val;

	ret = regmap_field_read(regl->sleep, &val);
	if (ret < 0)
		return 0;

	if (val)
		return REGULATOR_MODE_STANDBY;
	else
		return REGULATOR_MODE_NORMAL;
}

static int da9062_buck_get_status(struct regulator_dev *rdev)
{
	int ret = regulator_is_enabled_regmap(rdev);

	if (ret == 0) {
		ret = REGULATOR_STATUS_OFF;
	} else if (ret > 0) {
		ret = da9062_buck_get_mode(rdev);
		if (ret > 0)
			ret = regulator_mode_to_status(ret);
		else if (ret == 0)
			ret = -EIO;
	}

	return ret;
}

static int da9062_ldo_get_status(struct regulator_dev *rdev)
{
	int ret = regulator_is_enabled_regmap(rdev);

	if (ret == 0) {
		ret = REGULATOR_STATUS_OFF;
	} else if (ret > 0) {
		ret = da9062_ldo_get_mode(rdev);
		if (ret > 0)
			ret = regulator_mode_to_status(ret);
		else if (ret == 0)
			ret = -EIO;
	}

	return ret;
}

static int da9062_set_suspend_voltage(struct regulator_dev *rdev, int uv)
{
	struct da9062_regulator *regl = rdev_get_drvdata(rdev);
	const struct da9062_regulator_info *rinfo = regl->info;
	int ret, sel;

	sel = regulator_map_voltage_linear(rdev, uv, uv);
	if (sel < 0)
		return sel;

	sel <<= ffs(rdev->desc->vsel_mask) - 1;

	ret = regmap_update_bits(regl->hw->regmap, rinfo->suspend_vsel_reg,
				 rdev->desc->vsel_mask, sel);

	return ret;
}

static int da9062_suspend_enable(struct regulator_dev *rdev)
{
	struct da9062_regulator *regl = rdev_get_drvdata(rdev);

	return regmap_field_write(regl->suspend, 1);
}

static int da9062_suspend_disable(struct regulator_dev *rdev)
{
	struct da9062_regulator *regl = rdev_get_drvdata(rdev);

	return regmap_field_write(regl->suspend, 0);
}

static int da9062_buck_set_suspend_mode(struct regulator_dev *rdev,
					unsigned mode)
{
	struct da9062_regulator *regl = rdev_get_drvdata(rdev);
	int val;

	switch (mode) {
	case REGULATOR_MODE_FAST:
		val = DA9063_BUCK_MODE_SYNC;
		break;
	case REGULATOR_MODE_NORMAL:
		val = DA9063_BUCK_MODE_AUTO;
		break;
	case REGULATOR_MODE_STANDBY:
		val = DA9063_BUCK_MODE_SLEEP;
		break;
	default:
		return -EINVAL;
	}

	return regmap_field_write(regl->mode, val);
}

static int da9062_ldo_set_suspend_mode(struct regulator_dev *rdev,
						unsigned mode)
{
	struct da9062_regulator *regl = rdev_get_drvdata(rdev);
	unsigned val;

	switch (mode) {
	case REGULATOR_MODE_NORMAL:
		val = 0;
		break;
	case REGULATOR_MODE_STANDBY:
		val = 1;
		break;
	default:
		return -EINVAL;
	}

	return regmap_field_write(regl->suspend_sleep, val);
}

static const struct regulator_ops da9062_buck_ops = {
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.list_voltage		= regulator_list_voltage_linear,
	.set_current_limit	= regulator_set_current_limit_regmap,
	.get_current_limit	= regulator_get_current_limit_regmap,
	.set_mode		= da9062_buck_set_mode,
	.get_mode		= da9062_buck_get_mode,
	.get_status		= da9062_buck_get_status,
	.set_suspend_voltage	= da9062_set_suspend_voltage,
	.set_suspend_enable	= da9062_suspend_enable,
	.set_suspend_disable	= da9062_suspend_disable,
	.set_suspend_mode	= da9062_buck_set_suspend_mode,
};

static const struct regulator_ops da9062_ldo_ops = {
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.list_voltage		= regulator_list_voltage_linear,
	.set_mode		= da9062_ldo_set_mode,
	.get_mode		= da9062_ldo_get_mode,
	.get_status		= da9062_ldo_get_status,
	.set_suspend_voltage	= da9062_set_suspend_voltage,
	.set_suspend_enable	= da9062_suspend_enable,
	.set_suspend_disable	= da9062_suspend_disable,
	.set_suspend_mode	= da9062_ldo_set_suspend_mode,
};

/* DA9061 Regulator information */
static const struct da9062_regulator_info local_da9061_regulator_info[] = {
	{
		.desc.id = DA9061_ID_BUCK1,
		.desc.name = "DA9061 BUCK1",
		.desc.of_match = of_match_ptr("buck1"),
		.desc.regulators_node = of_match_ptr("regulators"),
		.desc.ops = &da9062_buck_ops,
		.desc.min_uV = (300) * 1000,
		.desc.uV_step = (10) * 1000,
		.desc.n_voltages = ((1570) - (300))/(10) + 1,
		.desc.curr_table = da9062_buck_a_limits,
		.desc.n_current_limits = ARRAY_SIZE(da9062_buck_a_limits),
		.desc.csel_reg = DA9062AA_BUCK_ILIM_C,
		.desc.csel_mask = DA9062AA_BUCK1_ILIM_MASK,
		.desc.enable_reg = DA9062AA_BUCK1_CONT,
		.desc.enable_mask = DA9062AA_BUCK1_EN_MASK,
		.desc.vsel_reg = DA9062AA_VBUCK1_A,
		.desc.vsel_mask = DA9062AA_VBUCK1_A_MASK,
		.desc.linear_min_sel = 0,
		.desc.of_map_mode = da9062_map_buck_mode,
		.sleep = REG_FIELD(DA9062AA_VBUCK1_A,
			__builtin_ffs((int)DA9062AA_BUCK1_SL_A_MASK) - 1,
			sizeof(unsigned int) * 8 -
			__builtin_clz((DA9062AA_BUCK1_SL_A_MASK)) - 1),
		.suspend_sleep = REG_FIELD(DA9062AA_VBUCK1_B,
			__builtin_ffs((int)DA9062AA_BUCK1_SL_B_MASK) - 1,
			sizeof(unsigned int) * 8 -
			__builtin_clz((DA9062AA_BUCK1_SL_B_MASK)) - 1),
		.suspend_vsel_reg = DA9062AA_VBUCK1_B,
		.mode = REG_FIELD(DA9062AA_BUCK1_CFG,
			__builtin_ffs((int)DA9062AA_BUCK1_MODE_MASK) - 1,
			sizeof(unsigned int) * 8 -
			__builtin_clz((DA9062AA_BUCK1_MODE_MASK)) - 1),
		.suspend = REG_FIELD(DA9062AA_BUCK1_CONT,
			__builtin_ffs((int)DA9062AA_BUCK1_CONF_MASK) - 1,
			sizeof(unsigned int) * 8 -
			__builtin_clz(DA9062AA_BUCK1_CONF_MASK) - 1),
	},
	{
		.desc.id = DA9061_ID_BUCK2,
		.desc.name = "DA9061 BUCK2",
		.desc.of_match = of_match_ptr("buck2"),
		.desc.regulators_node = of_match_ptr("regulators"),
		.desc.ops = &da9062_buck_ops,
		.desc.min_uV = (800) * 1000,
		.desc.uV_step = (20) * 1000,
		.desc.n_voltages = ((3340) - (800))/(20) + 1,
		.desc.curr_table = da9062_buck_b_limits,
		.desc.n_current_limits = ARRAY_SIZE(da9062_buck_b_limits),
		.desc.csel_reg = DA9062AA_BUCK_ILIM_A,
		.desc.csel_mask = DA9062AA_BUCK3_ILIM_MASK,
		.desc.enable_reg = DA9062AA_BUCK3_CONT,
		.desc.enable_mask = DA9062AA_BUCK3_EN_MASK,
		.desc.vsel_reg = DA9062AA_VBUCK3_A,
		.desc.vsel_mask = DA9062AA_VBUCK3_A_MASK,
		.desc.linear_min_sel = 0,
		.desc.of_map_mode = da9062_map_buck_mode,
		.sleep = REG_FIELD(DA9062AA_VBUCK3_A,
			__builtin_ffs((int)DA9062AA_BUCK3_SL_A_MASK) - 1,
			sizeof(unsigned int) * 8 -
			__builtin_clz((DA9062AA_BUCK3_SL_A_MASK)) - 1),
		.suspend_sleep = REG_FIELD(DA9062AA_VBUCK3_B,
			__builtin_ffs((int)DA9062AA_BUCK3_SL_B_MASK) - 1,
			sizeof(unsigned int) * 8 -
			__builtin_clz((DA9062AA_BUCK3_SL_B_MASK)) - 1),
		.suspend_vsel_reg = DA9062AA_VBUCK3_B,
		.mode = REG_FIELD(DA9062AA_BUCK3_CFG,
			__builtin_ffs((int)DA9062AA_BUCK3_MODE_MASK) - 1,
			sizeof(unsigned int) * 8 -
			__builtin_clz((DA9062AA_BUCK3_MODE_MASK)) - 1),
		.suspend = REG_FIELD(DA9062AA_BUCK3_CONT,
			__builtin_ffs((int)DA9062AA_BUCK3_CONF_MASK) - 1,
			sizeof(unsigned int) * 8 -
			__builtin_clz(DA9062AA_BUCK3_CONF_MASK) - 1),
	},
	{
		.desc.id = DA9061_ID_BUCK3,
		.desc.name = "DA9061 BUCK3",
		.desc.of_match = of_match_ptr("buck3"),
		.desc.regulators_node = of_match_ptr("regulators"),
		.desc.ops = &da9062_buck_ops,
		.desc.min_uV = (530) * 1000,
		.desc.uV_step = (10) * 1000,
		.desc.n_voltages = ((1800) - (530))/(10) + 1,
		.desc.curr_table = da9062_buck_a_limits,
		.desc.n_current_limits = ARRAY_SIZE(da9062_buck_a_limits),
		.desc.csel_reg = DA9062AA_BUCK_ILIM_B,
		.desc.csel_mask = DA9062AA_BUCK4_ILIM_MASK,
		.desc.enable_reg = DA9062AA_BUCK4_CONT,
		.desc.enable_mask = DA9062AA_BUCK4_EN_MASK,
		.desc.vsel_reg = DA9062AA_VBUCK4_A,
		.desc.vsel_mask = DA9062AA_VBUCK4_A_MASK,
		.desc.linear_min_sel = 0,
		.desc.of_map_mode = da9062_map_buck_mode,
		.sleep = REG_FIELD(DA9062AA_VBUCK4_A,
			__builtin_ffs((int)DA9062AA_BUCK4_SL_A_MASK) - 1,
			sizeof(unsigned int) * 8 -
			__builtin_clz((DA9062AA_BUCK4_SL_A_MASK)) - 1),
		.suspend_sleep = REG_FIELD(DA9062AA_VBUCK4_B,
			__builtin_ffs((int)DA9062AA_BUCK4_SL_B_MASK) - 1,
			sizeof(unsigned int) * 8 -
			__builtin_clz((DA9062AA_BUCK4_SL_B_MASK)) - 1),
		.suspend_vsel_reg = DA9062AA_VBUCK4_B,
		.mode = REG_FIELD(DA9062AA_BUCK4_CFG,
			__builtin_ffs((int)DA9062AA_BUCK4_MODE_MASK) - 1,
			sizeof(unsigned int) * 8 -
			__builtin_clz((DA9062AA_BUCK4_MODE_MASK)) - 1),
		.suspend = REG_FIELD(DA9062AA_BUCK4_CONT,
			__builtin_ffs((int)DA9062AA_BUCK4_CONF_MASK) - 1,
			sizeof(unsigned int) * 8 -
			__builtin_clz(DA9062AA_BUCK4_CONF_MASK) - 1),
	},
	{
		.desc.id = DA9061_ID_LDO1,
		.desc.name = "DA9061 LDO1",
		.desc.of_match = of_match_ptr("ldo1"),
		.desc.regulators_node = of_match_ptr("regulators"),
		.desc.ops = &da9062_ldo_ops,
		.desc.min_uV = (900) * 1000,
		.desc.uV_step = (50) * 1000,
		.desc.n_voltages = ((3600) - (900))/(50) + 1
				+ DA9062AA_VLDO_A_MIN_SEL,
		.desc.enable_reg = DA9062AA_LDO1_CONT,
		.desc.enable_mask = DA9062AA_LDO1_EN_MASK,
		.desc.vsel_reg = DA9062AA_VLDO1_A,
		.desc.vsel_mask = DA9062AA_VLDO1_A_MASK,
		.desc.linear_min_sel = DA9062AA_VLDO_A_MIN_SEL,
		.sleep = REG_FIELD(DA9062AA_VLDO1_A,
			__builtin_ffs((int)DA9062AA_LDO1_SL_A_MASK) - 1,
			sizeof(unsigned int) * 8 -
			__builtin_clz((DA9062AA_LDO1_SL_A_MASK)) - 1),
		.suspend_sleep = REG_FIELD(DA9062AA_VLDO1_B,
			__builtin_ffs((int)DA9062AA_LDO1_SL_B_MASK) - 1,
			sizeof(unsigned int) * 8 -
			__builtin_clz((DA9062AA_LDO1_SL_B_MASK)) - 1),
		.suspend_vsel_reg = DA9062AA_VLDO1_B,
		.suspend = REG_FIELD(DA9062AA_LDO1_CONT,
			__builtin_ffs((int)DA9062AA_LDO1_CONF_MASK) - 1,
			sizeof(unsigned int) * 8 -
			__builtin_clz(DA9062AA_LDO1_CONF_MASK) - 1),
		.oc_event = REG_FIELD(DA9062AA_STATUS_D,
			__builtin_ffs((int)DA9062AA_LDO1_ILIM_MASK) - 1,
			sizeof(unsigned int) * 8 -
			__builtin_clz((DA9062AA_LDO1_ILIM_MASK)) - 1),
	},
	{
		.desc.id = DA9061_ID_LDO2,
		.desc.name = "DA9061 LDO2",
		.desc.of_match = of_match_ptr("ldo2"),
		.desc.regulators_node = of_match_ptr("regulators"),
		.desc.ops = &da9062_ldo_ops,
		.desc.min_uV = (900) * 1000,
		.desc.uV_step = (50) * 1000,
		.desc.n_voltages = ((3600) - (900))/(50) + 1
				+ DA9062AA_VLDO_A_MIN_SEL,
		.desc.enable_reg = DA9062AA_LDO2_CONT,
		.desc.enable_mask = DA9062AA_LDO2_EN_MASK,
		.desc.vsel_reg = DA9062AA_VLDO2_A,
		.desc.vsel_mask = DA9062AA_VLDO2_A_MASK,
		.desc.linear_min_sel = DA9062AA_VLDO_A_MIN_SEL,
		.sleep = REG_FIELD(DA9062AA_VLDO2_A,
			__builtin_ffs((int)DA9062AA_LDO2_SL_A_MASK) - 1,
			sizeof(unsigned int) * 8 -
			__builtin_clz((DA9062AA_LDO2_SL_A_MASK)) - 1),
		.suspend_sleep = REG_FIELD(DA9062AA_VLDO2_B,
			__builtin_ffs((int)DA9062AA_LDO2_SL_B_MASK) - 1,
			sizeof(unsigned int) * 8 -
			__builtin_clz((DA9062AA_LDO2_SL_B_MASK)) - 1),
		.suspend_vsel_reg = DA9062AA_VLDO2_B,
		.suspend = REG_FIELD(DA9062AA_LDO2_CONT,
			__builtin_ffs((int)DA9062AA_LDO2_CONF_MASK) - 1,
			sizeof(unsigned int) * 8 -
			__builtin_clz(DA9062AA_LDO2_CONF_MASK) - 1),
		.oc_event = REG_FIELD(DA9062AA_STATUS_D,
			__builtin_ffs((int)DA9062AA_LDO2_ILIM_MASK) - 1,
			sizeof(unsigned int) * 8 -
			__builtin_clz((DA9062AA_LDO2_ILIM_MASK)) - 1),
	},
	{
		.desc.id = DA9061_ID_LDO3,
		.desc.name = "DA9061 LDO3",
		.desc.of_match = of_match_ptr("ldo3"),
		.desc.regulators_node = of_match_ptr("regulators"),
		.desc.ops = &da9062_ldo_ops,
		.desc.min_uV = (900) * 1000,
		.desc.uV_step = (50) * 1000,
		.desc.n_voltages = ((3600) - (900))/(50) + 1
				+ DA9062AA_VLDO_A_MIN_SEL,
		.desc.enable_reg = DA9062AA_LDO3_CONT,
		.desc.enable_mask = DA9062AA_LDO3_EN_MASK,
		.desc.vsel_reg = DA9062AA_VLDO3_A,
		.desc.vsel_mask = DA9062AA_VLDO3_A_MASK,
		.desc.linear_min_sel = DA9062AA_VLDO_A_MIN_SEL,
		.sleep = REG_FIELD(DA9062AA_VLDO3_A,
			__builtin_ffs((int)DA9062AA_LDO3_SL_A_MASK) - 1,
			sizeof(unsigned int) * 8 -
			__builtin_clz((DA9062AA_LDO3_SL_A_MASK)) - 1),
		.suspend_sleep = REG_FIELD(DA9062AA_VLDO3_B,
			__builtin_ffs((int)DA9062AA_LDO3_SL_B_MASK) - 1,
			sizeof(unsigned int) * 8 -
			__builtin_clz((DA9062AA_LDO3_SL_B_MASK)) - 1),
		.suspend_vsel_reg = DA9062AA_VLDO3_B,
		.suspend = REG_FIELD(DA9062AA_LDO3_CONT,
			__builtin_ffs((int)DA9062AA_LDO3_CONF_MASK) - 1,
			sizeof(unsigned int) * 8 -
			__builtin_clz(DA9062AA_LDO3_CONF_MASK) - 1),
		.oc_event = REG_FIELD(DA9062AA_STATUS_D,
			__builtin_ffs((int)DA9062AA_LDO3_ILIM_MASK) - 1,
			sizeof(unsigned int) * 8 -
			__builtin_clz((DA9062AA_LDO3_ILIM_MASK)) - 1),
	},
	{
		.desc.id = DA9061_ID_LDO4,
		.desc.name = "DA9061 LDO4",
		.desc.of_match = of_match_ptr("ldo4"),
		.desc.regulators_node = of_match_ptr("regulators"),
		.desc.ops = &da9062_ldo_ops,
		.desc.min_uV = (900) * 1000,
		.desc.uV_step = (50) * 1000,
		.desc.n_voltages = ((3600) - (900))/(50) + 1
				+ DA9062AA_VLDO_A_MIN_SEL,
		.desc.enable_reg = DA9062AA_LDO4_CONT,
		.desc.enable_mask = DA9062AA_LDO4_EN_MASK,
		.desc.vsel_reg = DA9062AA_VLDO4_A,
		.desc.vsel_mask = DA9062AA_VLDO4_A_MASK,
		.desc.linear_min_sel = DA9062AA_VLDO_A_MIN_SEL,
		.sleep = REG_FIELD(DA9062AA_VLDO4_A,
			__builtin_ffs((int)DA9062AA_LDO4_SL_A_MASK) - 1,
			sizeof(unsigned int) * 8 -
			__builtin_clz((DA9062AA_LDO4_SL_A_MASK)) - 1),
		.suspend_sleep = REG_FIELD(DA9062AA_VLDO4_B,
			__builtin_ffs((int)DA9062AA_LDO4_SL_B_MASK) - 1,
			sizeof(unsigned int) * 8 -
			__builtin_clz((DA9062AA_LDO4_SL_B_MASK)) - 1),
		.suspend_vsel_reg = DA9062AA_VLDO4_B,
		.suspend = REG_FIELD(DA9062AA_LDO4_CONT,
			__builtin_ffs((int)DA9062AA_LDO4_CONF_MASK) - 1,
			sizeof(unsigned int) * 8 -
			__builtin_clz(DA9062AA_LDO4_CONF_MASK) - 1),
		.oc_event = REG_FIELD(DA9062AA_STATUS_D,
			__builtin_ffs((int)DA9062AA_LDO4_ILIM_MASK) - 1,
			sizeof(unsigned int) * 8 -
			__builtin_clz((DA9062AA_LDO4_ILIM_MASK)) - 1),
	},
};

/* DA9062 Regulator information */
static const struct da9062_regulator_info local_da9062_regulator_info[] = {
	{
		.desc.id = DA9062_ID_BUCK1,
		.desc.name = "DA9062 BUCK1",
		.desc.of_match = of_match_ptr("buck1"),
		.desc.regulators_node = of_match_ptr("regulators"),
		.desc.ops = &da9062_buck_ops,
		.desc.min_uV = (300) * 1000,
		.desc.uV_step = (10) * 1000,
		.desc.n_voltages = ((1570) - (300))/(10) + 1,
		.desc.curr_table = da9062_buck_a_limits,
		.desc.n_current_limits = ARRAY_SIZE(da9062_buck_a_limits),
		.desc.csel_reg = DA9062AA_BUCK_ILIM_C,
		.desc.csel_mask = DA9062AA_BUCK1_ILIM_MASK,
		.desc.enable_reg = DA9062AA_BUCK1_CONT,
		.desc.enable_mask = DA9062AA_BUCK1_EN_MASK,
		.desc.vsel_reg = DA9062AA_VBUCK1_A,
		.desc.vsel_mask = DA9062AA_VBUCK1_A_MASK,
		.desc.linear_min_sel = 0,
		.desc.of_map_mode = da9062_map_buck_mode,
		.sleep = REG_FIELD(DA9062AA_VBUCK1_A,
			__builtin_ffs((int)DA9062AA_BUCK1_SL_A_MASK) - 1,
			sizeof(unsigned int) * 8 -
			__builtin_clz((DA9062AA_BUCK1_SL_A_MASK)) - 1),
		.suspend_sleep = REG_FIELD(DA9062AA_VBUCK1_B,
			__builtin_ffs((int)DA9062AA_BUCK1_SL_B_MASK) - 1,
			sizeof(unsigned int) * 8 -
			__builtin_clz((DA9062AA_BUCK1_SL_B_MASK)) - 1),
		.suspend_vsel_reg = DA9062AA_VBUCK1_B,
		.mode = REG_FIELD(DA9062AA_BUCK1_CFG,
			__builtin_ffs((int)DA9062AA_BUCK1_MODE_MASK) - 1,
			sizeof(unsigned int) * 8 -
			__builtin_clz((DA9062AA_BUCK1_MODE_MASK)) - 1),
		.suspend = REG_FIELD(DA9062AA_BUCK1_CONT,
			__builtin_ffs((int)DA9062AA_BUCK1_CONF_MASK) - 1,
			sizeof(unsigned int) * 8 -
			__builtin_clz(DA9062AA_BUCK1_CONF_MASK) - 1),
	},
	{
		.desc.id = DA9062_ID_BUCK2,
		.desc.name = "DA9062 BUCK2",
		.desc.of_match = of_match_ptr("buck2"),
		.desc.regulators_node = of_match_ptr("regulators"),
		.desc.ops = &da9062_buck_ops,
		.desc.min_uV = (300) * 1000,
		.desc.uV_step = (10) * 1000,
		.desc.n_voltages = ((1570) - (300))/(10) + 1,
		.desc.curr_table = da9062_buck_a_limits,
		.desc.n_current_limits = ARRAY_SIZE(da9062_buck_a_limits),
		.desc.csel_reg = DA9062AA_BUCK_ILIM_C,
		.desc.csel_mask = DA9062AA_BUCK2_ILIM_MASK,
		.desc.enable_reg = DA9062AA_BUCK2_CONT,
		.desc.enable_mask = DA9062AA_BUCK2_EN_MASK,
		.desc.vsel_reg = DA9062AA_VBUCK2_A,
		.desc.vsel_mask = DA9062AA_VBUCK2_A_MASK,
		.desc.linear_min_sel = 0,
		.desc.of_map_mode = da9062_map_buck_mode,
		.sleep = REG_FIELD(DA9062AA_VBUCK2_A,
			__builtin_ffs((int)DA9062AA_BUCK2_SL_A_MASK) - 1,
			sizeof(unsigned int) * 8 -
			__builtin_clz((DA9062AA_BUCK2_SL_A_MASK)) - 1),
		.suspend_sleep = REG_FIELD(DA9062AA_VBUCK2_B,
			__builtin_ffs((int)DA9062AA_BUCK2_SL_B_MASK) - 1,
			sizeof(unsigned int) * 8 -
			__builtin_clz((DA9062AA_BUCK2_SL_B_MASK)) - 1),
		.suspend_vsel_reg = DA9062AA_VBUCK2_B,
		.mode = REG_FIELD(DA9062AA_BUCK2_CFG,
			__builtin_ffs((int)DA9062AA_BUCK2_MODE_MASK) - 1,
			sizeof(unsigned int) * 8 -
			__builtin_clz((DA9062AA_BUCK2_MODE_MASK)) - 1),
		.suspend = REG_FIELD(DA9062AA_BUCK2_CONT,
			__builtin_ffs((int)DA9062AA_BUCK2_CONF_MASK) - 1,
			sizeof(unsigned int) * 8 -
			__builtin_clz(DA9062AA_BUCK2_CONF_MASK) - 1),
	},
	{
		.desc.id = DA9062_ID_BUCK3,
		.desc.name = "DA9062 BUCK3",
		.desc.of_match = of_match_ptr("buck3"),
		.desc.regulators_node = of_match_ptr("regulators"),
		.desc.ops = &da9062_buck_ops,
		.desc.min_uV = (800) * 1000,
		.desc.uV_step = (20) * 1000,
		.desc.n_voltages = ((3340) - (800))/(20) + 1,
		.desc.curr_table = da9062_buck_b_limits,
		.desc.n_current_limits = ARRAY_SIZE(da9062_buck_b_limits),
		.desc.csel_reg = DA9062AA_BUCK_ILIM_A,
		.desc.csel_mask = DA9062AA_BUCK3_ILIM_MASK,
		.desc.enable_reg = DA9062AA_BUCK3_CONT,
		.desc.enable_mask = DA9062AA_BUCK3_EN_MASK,
		.desc.vsel_reg = DA9062AA_VBUCK3_A,
		.desc.vsel_mask = DA9062AA_VBUCK3_A_MASK,
		.desc.linear_min_sel = 0,
		.desc.of_map_mode = da9062_map_buck_mode,
		.sleep = REG_FIELD(DA9062AA_VBUCK3_A,
			__builtin_ffs((int)DA9062AA_BUCK3_SL_A_MASK) - 1,
			sizeof(unsigned int) * 8 -
			__builtin_clz((DA9062AA_BUCK3_SL_A_MASK)) - 1),
		.suspend_sleep = REG_FIELD(DA9062AA_VBUCK3_B,
			__builtin_ffs((int)DA9062AA_BUCK3_SL_B_MASK) - 1,
			sizeof(unsigned int) * 8 -
			__builtin_clz((DA9062AA_BUCK3_SL_B_MASK)) - 1),
		.suspend_vsel_reg = DA9062AA_VBUCK3_B,
		.mode = REG_FIELD(DA9062AA_BUCK3_CFG,
			__builtin_ffs((int)DA9062AA_BUCK3_MODE_MASK) - 1,
			sizeof(unsigned int) * 8 -
			__builtin_clz((DA9062AA_BUCK3_MODE_MASK)) - 1),
		.suspend = REG_FIELD(DA9062AA_BUCK3_CONT,
			__builtin_ffs((int)DA9062AA_BUCK3_CONF_MASK) - 1,
			sizeof(unsigned int) * 8 -
			__builtin_clz(DA9062AA_BUCK3_CONF_MASK) - 1),
	},
	{
		.desc.id = DA9062_ID_BUCK4,
		.desc.name = "DA9062 BUCK4",
		.desc.of_match = of_match_ptr("buck4"),
		.desc.regulators_node = of_match_ptr("regulators"),
		.desc.ops = &da9062_buck_ops,
		.desc.min_uV = (530) * 1000,
		.desc.uV_step = (10) * 1000,
		.desc.n_voltages = ((1800) - (530))/(10) + 1,
		.desc.curr_table = da9062_buck_a_limits,
		.desc.n_current_limits = ARRAY_SIZE(da9062_buck_a_limits),
		.desc.csel_reg = DA9062AA_BUCK_ILIM_B,
		.desc.csel_mask = DA9062AA_BUCK4_ILIM_MASK,
		.desc.enable_reg = DA9062AA_BUCK4_CONT,
		.desc.enable_mask = DA9062AA_BUCK4_EN_MASK,
		.desc.vsel_reg = DA9062AA_VBUCK4_A,
		.desc.vsel_mask = DA9062AA_VBUCK4_A_MASK,
		.desc.linear_min_sel = 0,
		.desc.of_map_mode = da9062_map_buck_mode,
		.sleep = REG_FIELD(DA9062AA_VBUCK4_A,
			__builtin_ffs((int)DA9062AA_BUCK4_SL_A_MASK) - 1,
			sizeof(unsigned int) * 8 -
			__builtin_clz((DA9062AA_BUCK4_SL_A_MASK)) - 1),
		.suspend_sleep = REG_FIELD(DA9062AA_VBUCK4_B,
			__builtin_ffs((int)DA9062AA_BUCK4_SL_B_MASK) - 1,
			sizeof(unsigned int) * 8 -
			__builtin_clz((DA9062AA_BUCK4_SL_B_MASK)) - 1),
		.suspend_vsel_reg = DA9062AA_VBUCK4_B,
		.mode = REG_FIELD(DA9062AA_BUCK4_CFG,
			__builtin_ffs((int)DA9062AA_BUCK4_MODE_MASK) - 1,
			sizeof(unsigned int) * 8 -
			__builtin_clz((DA9062AA_BUCK4_MODE_MASK)) - 1),
		.suspend = REG_FIELD(DA9062AA_BUCK4_CONT,
			__builtin_ffs((int)DA9062AA_BUCK4_CONF_MASK) - 1,
			sizeof(unsigned int) * 8 -
			__builtin_clz(DA9062AA_BUCK4_CONF_MASK) - 1),
	},
	{
		.desc.id = DA9062_ID_LDO1,
		.desc.name = "DA9062 LDO1",
		.desc.of_match = of_match_ptr("ldo1"),
		.desc.regulators_node = of_match_ptr("regulators"),
		.desc.ops = &da9062_ldo_ops,
		.desc.min_uV = (900) * 1000,
		.desc.uV_step = (50) * 1000,
		.desc.n_voltages = ((3600) - (900))/(50) + 1
				+ DA9062AA_VLDO_A_MIN_SEL,
		.desc.enable_reg = DA9062AA_LDO1_CONT,
		.desc.enable_mask = DA9062AA_LDO1_EN_MASK,
		.desc.vsel_reg = DA9062AA_VLDO1_A,
		.desc.vsel_mask = DA9062AA_VLDO1_A_MASK,
		.desc.linear_min_sel = DA9062AA_VLDO_A_MIN_SEL,
		.sleep = REG_FIELD(DA9062AA_VLDO1_A,
			__builtin_ffs((int)DA9062AA_LDO1_SL_A_MASK) - 1,
			sizeof(unsigned int) * 8 -
			__builtin_clz((DA9062AA_LDO1_SL_A_MASK)) - 1),
		.suspend_sleep = REG_FIELD(DA9062AA_VLDO1_B,
			__builtin_ffs((int)DA9062AA_LDO1_SL_B_MASK) - 1,
			sizeof(unsigned int) * 8 -
			__builtin_clz((DA9062AA_LDO1_SL_B_MASK)) - 1),
		.suspend_vsel_reg = DA9062AA_VLDO1_B,
		.suspend = REG_FIELD(DA9062AA_LDO1_CONT,
			__builtin_ffs((int)DA9062AA_LDO1_CONF_MASK) - 1,
			sizeof(unsigned int) * 8 -
			__builtin_clz(DA9062AA_LDO1_CONF_MASK) - 1),
		.oc_event = REG_FIELD(DA9062AA_STATUS_D,
			__builtin_ffs((int)DA9062AA_LDO1_ILIM_MASK) - 1,
			sizeof(unsigned int) * 8 -
			__builtin_clz((DA9062AA_LDO1_ILIM_MASK)) - 1),
	},
	{
		.desc.id = DA9062_ID_LDO2,
		.desc.name = "DA9062 LDO2",
		.desc.of_match = of_match_ptr("ldo2"),
		.desc.regulators_node = of_match_ptr("regulators"),
		.desc.ops = &da9062_ldo_ops,
		.desc.min_uV = (900) * 1000,
		.desc.uV_step = (50) * 1000,
		.desc.n_voltages = ((3600) - (900))/(50) + 1
				+ DA9062AA_VLDO_A_MIN_SEL,
		.desc.enable_reg = DA9062AA_LDO2_CONT,
		.desc.enable_mask = DA9062AA_LDO2_EN_MASK,
		.desc.vsel_reg = DA9062AA_VLDO2_A,
		.desc.vsel_mask = DA9062AA_VLDO2_A_MASK,
		.desc.linear_min_sel = DA9062AA_VLDO_A_MIN_SEL,
		.sleep = REG_FIELD(DA9062AA_VLDO2_A,
			__builtin_ffs((int)DA9062AA_LDO2_SL_A_MASK) - 1,
			sizeof(unsigned int) * 8 -
			__builtin_clz((DA9062AA_LDO2_SL_A_MASK)) - 1),
		.suspend_sleep = REG_FIELD(DA9062AA_VLDO2_B,
			__builtin_ffs((int)DA9062AA_LDO2_SL_B_MASK) - 1,
			sizeof(unsigned int) * 8 -
			__builtin_clz((DA9062AA_LDO2_SL_B_MASK)) - 1),
		.suspend_vsel_reg = DA9062AA_VLDO2_B,
		.suspend = REG_FIELD(DA9062AA_LDO2_CONT,
			__builtin_ffs((int)DA9062AA_LDO2_CONF_MASK) - 1,
			sizeof(unsigned int) * 8 -
			__builtin_clz(DA9062AA_LDO2_CONF_MASK) - 1),
		.oc_event = REG_FIELD(DA9062AA_STATUS_D,
			__builtin_ffs((int)DA9062AA_LDO2_ILIM_MASK) - 1,
			sizeof(unsigned int) * 8 -
			__builtin_clz((DA9062AA_LDO2_ILIM_MASK)) - 1),
	},
	{
		.desc.id = DA9062_ID_LDO3,
		.desc.name = "DA9062 LDO3",
		.desc.of_match = of_match_ptr("ldo3"),
		.desc.regulators_node = of_match_ptr("regulators"),
		.desc.ops = &da9062_ldo_ops,
		.desc.min_uV = (900) * 1000,
		.desc.uV_step = (50) * 1000,
		.desc.n_voltages = ((3600) - (900))/(50) + 1
				+ DA9062AA_VLDO_A_MIN_SEL,
		.desc.enable_reg = DA9062AA_LDO3_CONT,
		.desc.enable_mask = DA9062AA_LDO3_EN_MASK,
		.desc.vsel_reg = DA9062AA_VLDO3_A,
		.desc.vsel_mask = DA9062AA_VLDO3_A_MASK,
		.desc.linear_min_sel = DA9062AA_VLDO_A_MIN_SEL,
		.sleep = REG_FIELD(DA9062AA_VLDO3_A,
			__builtin_ffs((int)DA9062AA_LDO3_SL_A_MASK) - 1,
			sizeof(unsigned int) * 8 -
			__builtin_clz((DA9062AA_LDO3_SL_A_MASK)) - 1),
		.suspend_sleep = REG_FIELD(DA9062AA_VLDO3_B,
			__builtin_ffs((int)DA9062AA_LDO3_SL_B_MASK) - 1,
			sizeof(unsigned int) * 8 -
			__builtin_clz((DA9062AA_LDO3_SL_B_MASK)) - 1),
		.suspend_vsel_reg = DA9062AA_VLDO3_B,
		.suspend = REG_FIELD(DA9062AA_LDO3_CONT,
			__builtin_ffs((int)DA9062AA_LDO3_CONF_MASK) - 1,
			sizeof(unsigned int) * 8 -
			__builtin_clz(DA9062AA_LDO3_CONF_MASK) - 1),
		.oc_event = REG_FIELD(DA9062AA_STATUS_D,
			__builtin_ffs((int)DA9062AA_LDO3_ILIM_MASK) - 1,
			sizeof(unsigned int) * 8 -
			__builtin_clz((DA9062AA_LDO3_ILIM_MASK)) - 1),
	},
	{
		.desc.id = DA9062_ID_LDO4,
		.desc.name = "DA9062 LDO4",
		.desc.of_match = of_match_ptr("ldo4"),
		.desc.regulators_node = of_match_ptr("regulators"),
		.desc.ops = &da9062_ldo_ops,
		.desc.min_uV = (900) * 1000,
		.desc.uV_step = (50) * 1000,
		.desc.n_voltages = ((3600) - (900))/(50) + 1
				+ DA9062AA_VLDO_A_MIN_SEL,
		.desc.enable_reg = DA9062AA_LDO4_CONT,
		.desc.enable_mask = DA9062AA_LDO4_EN_MASK,
		.desc.vsel_reg = DA9062AA_VLDO4_A,
		.desc.vsel_mask = DA9062AA_VLDO4_A_MASK,
		.desc.linear_min_sel = DA9062AA_VLDO_A_MIN_SEL,
		.sleep = REG_FIELD(DA9062AA_VLDO4_A,
			__builtin_ffs((int)DA9062AA_LDO4_SL_A_MASK) - 1,
			sizeof(unsigned int) * 8 -
			__builtin_clz((DA9062AA_LDO4_SL_A_MASK)) - 1),
		.suspend_sleep = REG_FIELD(DA9062AA_VLDO4_B,
			__builtin_ffs((int)DA9062AA_LDO4_SL_B_MASK) - 1,
			sizeof(unsigned int) * 8 -
			__builtin_clz((DA9062AA_LDO4_SL_B_MASK)) - 1),
		.suspend_vsel_reg = DA9062AA_VLDO4_B,
		.suspend = REG_FIELD(DA9062AA_LDO4_CONT,
			__builtin_ffs((int)DA9062AA_LDO4_CONF_MASK) - 1,
			sizeof(unsigned int) * 8 -
			__builtin_clz(DA9062AA_LDO4_CONF_MASK) - 1),
		.oc_event = REG_FIELD(DA9062AA_STATUS_D,
			__builtin_ffs((int)DA9062AA_LDO4_ILIM_MASK) - 1,
			sizeof(unsigned int) * 8 -
			__builtin_clz((DA9062AA_LDO4_ILIM_MASK)) - 1),
	},
};

/* Regulator interrupt handlers */
static irqreturn_t da9062_ldo_lim_event(int irq, void *data)
{
	struct da9062_regulators *regulators = data;
	struct da9062 *hw = regulators->regulator[0].hw;
	struct da9062_regulator *regl;
	int handled = IRQ_NONE;
	int bits, i, ret;

	ret = regmap_read(hw->regmap, DA9062AA_STATUS_D, &bits);
	if (ret < 0) {
		dev_err(hw->dev,
			"Failed to read LDO overcurrent indicator\n");
		goto ldo_lim_error;
	}

	for (i = regulators->n_regulators - 1; i >= 0; i--) {
		regl = &regulators->regulator[i];
		if (regl->info->oc_event.reg != DA9062AA_STATUS_D)
			continue;

		if (BIT(regl->info->oc_event.lsb) & bits) {
			regulator_lock(regl->rdev);
			regulator_notifier_call_chain(regl->rdev,
					REGULATOR_EVENT_OVER_CURRENT, NULL);
			regulator_unlock(regl->rdev);
			handled = IRQ_HANDLED;
		}
	}

ldo_lim_error:
	return handled;
}

static int da9062_regulator_probe(struct platform_device *pdev)
{
	struct da9062 *chip = dev_get_drvdata(pdev->dev.parent);
	struct da9062_regulators *regulators;
	struct da9062_regulator *regl;
	struct regulator_config config = { };
	const struct da9062_regulator_info *rinfo;
	int irq, n, ret;
	int max_regulators;

	switch (chip->chip_type) {
	case COMPAT_TYPE_DA9061:
		max_regulators = DA9061_MAX_REGULATORS;
		rinfo = local_da9061_regulator_info;
		break;
	case COMPAT_TYPE_DA9062:
		max_regulators = DA9062_MAX_REGULATORS;
		rinfo = local_da9062_regulator_info;
		break;
	default:
		dev_err(chip->dev, "Unrecognised chip type\n");
		return -ENODEV;
	}

	/* Allocate memory required by usable regulators */
	regulators = devm_kzalloc(&pdev->dev, struct_size(regulators, regulator,
				  max_regulators), GFP_KERNEL);
	if (!regulators)
		return -ENOMEM;

	regulators->n_regulators = max_regulators;
	platform_set_drvdata(pdev, regulators);

	for (n = 0; n < regulators->n_regulators; n++) {
		/* Initialise regulator structure */
		regl = &regulators->regulator[n];
		regl->hw = chip;
		regl->info = &rinfo[n];
		regl->desc = regl->info->desc;
		regl->desc.type = REGULATOR_VOLTAGE;
		regl->desc.owner = THIS_MODULE;

		if (regl->info->mode.reg) {
			regl->mode = devm_regmap_field_alloc(
					&pdev->dev,
					chip->regmap,
					regl->info->mode);
			if (IS_ERR(regl->mode))
				return PTR_ERR(regl->mode);
		}

		if (regl->info->suspend.reg) {
			regl->suspend = devm_regmap_field_alloc(
					&pdev->dev,
					chip->regmap,
					regl->info->suspend);
			if (IS_ERR(regl->suspend))
				return PTR_ERR(regl->suspend);
		}

		if (regl->info->sleep.reg) {
			regl->sleep = devm_regmap_field_alloc(
					&pdev->dev,
					chip->regmap,
					regl->info->sleep);
			if (IS_ERR(regl->sleep))
				return PTR_ERR(regl->sleep);
		}

		if (regl->info->suspend_sleep.reg) {
			regl->suspend_sleep = devm_regmap_field_alloc(
					&pdev->dev,
					chip->regmap,
					regl->info->suspend_sleep);
			if (IS_ERR(regl->suspend_sleep))
				return PTR_ERR(regl->suspend_sleep);
		}

		/* Register regulator */
		memset(&config, 0, sizeof(config));
		config.dev = chip->dev;
		config.driver_data = regl;
		config.regmap = chip->regmap;

		regl->rdev = devm_regulator_register(&pdev->dev, &regl->desc,
						     &config);
		if (IS_ERR(regl->rdev)) {
			dev_err(&pdev->dev,
				"Failed to register %s regulator\n",
				regl->desc.name);
			return PTR_ERR(regl->rdev);
		}
	}

	/* LDOs overcurrent event support */
	irq = platform_get_irq_byname(pdev, "LDO_LIM");
	if (irq < 0)
		return irq;
	regulators->irq_ldo_lim = irq;

	ret = devm_request_threaded_irq(&pdev->dev, irq,
					NULL, da9062_ldo_lim_event,
					IRQF_TRIGGER_LOW | IRQF_ONESHOT,
					"LDO_LIM", regulators);
	if (ret) {
		dev_warn(&pdev->dev,
			 "Failed to request LDO_LIM IRQ.\n");
		regulators->irq_ldo_lim = -ENXIO;
	}

	return 0;
}

static struct platform_driver da9062_regulator_driver = {
	.driver = {
		.name = "da9062-regulators",
	},
	.probe = da9062_regulator_probe,
};

static int __init da9062_regulator_init(void)
{
	return platform_driver_register(&da9062_regulator_driver);
}
subsys_initcall(da9062_regulator_init);

static void __exit da9062_regulator_cleanup(void)
{
	platform_driver_unregister(&da9062_regulator_driver);
}
module_exit(da9062_regulator_cleanup);

/* Module information */
MODULE_AUTHOR("S Twiss <stwiss.opensource@diasemi.com>");
MODULE_DESCRIPTION("REGULATOR device driver for Dialog DA9062 and DA9061");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:da9062-regulators");
