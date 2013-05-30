/*
 * Driver for Regulator part of Palmas PMIC Chips
 *
 * Copyright 2011-2013 Texas Instruments Inc.
 *
 * Author: Graeme Gregory <gg@slimlogic.co.uk>
 * Author: Ian Lartey <ian@slimlogic.co.uk>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under  the terms of the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the License, or (at your
 *  option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/slab.h>
#include <linux/regmap.h>
#include <linux/mfd/palmas.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/regulator/of_regulator.h>

struct regs_info {
	char	*name;
	char	*sname;
	u8	vsel_addr;
	u8	ctrl_addr;
	u8	tstep_addr;
};

static const struct regs_info palmas_regs_info[] = {
	{
		.name		= "SMPS12",
		.sname		= "smps1-in",
		.vsel_addr	= PALMAS_SMPS12_VOLTAGE,
		.ctrl_addr	= PALMAS_SMPS12_CTRL,
		.tstep_addr	= PALMAS_SMPS12_TSTEP,
	},
	{
		.name		= "SMPS123",
		.sname		= "smps1-in",
		.vsel_addr	= PALMAS_SMPS12_VOLTAGE,
		.ctrl_addr	= PALMAS_SMPS12_CTRL,
		.tstep_addr	= PALMAS_SMPS12_TSTEP,
	},
	{
		.name		= "SMPS3",
		.sname		= "smps3-in",
		.vsel_addr	= PALMAS_SMPS3_VOLTAGE,
		.ctrl_addr	= PALMAS_SMPS3_CTRL,
	},
	{
		.name		= "SMPS45",
		.sname		= "smps4-in",
		.vsel_addr	= PALMAS_SMPS45_VOLTAGE,
		.ctrl_addr	= PALMAS_SMPS45_CTRL,
		.tstep_addr	= PALMAS_SMPS45_TSTEP,
	},
	{
		.name		= "SMPS457",
		.sname		= "smps4-in",
		.vsel_addr	= PALMAS_SMPS45_VOLTAGE,
		.ctrl_addr	= PALMAS_SMPS45_CTRL,
		.tstep_addr	= PALMAS_SMPS45_TSTEP,
	},
	{
		.name		= "SMPS6",
		.sname		= "smps6-in",
		.vsel_addr	= PALMAS_SMPS6_VOLTAGE,
		.ctrl_addr	= PALMAS_SMPS6_CTRL,
		.tstep_addr	= PALMAS_SMPS6_TSTEP,
	},
	{
		.name		= "SMPS7",
		.sname		= "smps7-in",
		.vsel_addr	= PALMAS_SMPS7_VOLTAGE,
		.ctrl_addr	= PALMAS_SMPS7_CTRL,
	},
	{
		.name		= "SMPS8",
		.sname		= "smps8-in",
		.vsel_addr	= PALMAS_SMPS8_VOLTAGE,
		.ctrl_addr	= PALMAS_SMPS8_CTRL,
		.tstep_addr	= PALMAS_SMPS8_TSTEP,
	},
	{
		.name		= "SMPS9",
		.sname		= "smps9-in",
		.vsel_addr	= PALMAS_SMPS9_VOLTAGE,
		.ctrl_addr	= PALMAS_SMPS9_CTRL,
	},
	{
		.name		= "SMPS10",
		.sname		= "smps10-in",
		.ctrl_addr	= PALMAS_SMPS10_CTRL,
	},
	{
		.name		= "LDO1",
		.sname		= "ldo1-in",
		.vsel_addr	= PALMAS_LDO1_VOLTAGE,
		.ctrl_addr	= PALMAS_LDO1_CTRL,
	},
	{
		.name		= "LDO2",
		.sname		= "ldo2-in",
		.vsel_addr	= PALMAS_LDO2_VOLTAGE,
		.ctrl_addr	= PALMAS_LDO2_CTRL,
	},
	{
		.name		= "LDO3",
		.sname		= "ldo3-in",
		.vsel_addr	= PALMAS_LDO3_VOLTAGE,
		.ctrl_addr	= PALMAS_LDO3_CTRL,
	},
	{
		.name		= "LDO4",
		.sname		= "ldo4-in",
		.vsel_addr	= PALMAS_LDO4_VOLTAGE,
		.ctrl_addr	= PALMAS_LDO4_CTRL,
	},
	{
		.name		= "LDO5",
		.sname		= "ldo5-in",
		.vsel_addr	= PALMAS_LDO5_VOLTAGE,
		.ctrl_addr	= PALMAS_LDO5_CTRL,
	},
	{
		.name		= "LDO6",
		.sname		= "ldo6-in",
		.vsel_addr	= PALMAS_LDO6_VOLTAGE,
		.ctrl_addr	= PALMAS_LDO6_CTRL,
	},
	{
		.name		= "LDO7",
		.sname		= "ldo7-in",
		.vsel_addr	= PALMAS_LDO7_VOLTAGE,
		.ctrl_addr	= PALMAS_LDO7_CTRL,
	},
	{
		.name		= "LDO8",
		.sname		= "ldo8-in",
		.vsel_addr	= PALMAS_LDO8_VOLTAGE,
		.ctrl_addr	= PALMAS_LDO8_CTRL,
	},
	{
		.name		= "LDO9",
		.sname		= "ldo9-in",
		.vsel_addr	= PALMAS_LDO9_VOLTAGE,
		.ctrl_addr	= PALMAS_LDO9_CTRL,
	},
	{
		.name		= "LDOLN",
		.sname		= "ldoln-in",
		.vsel_addr	= PALMAS_LDOLN_VOLTAGE,
		.ctrl_addr	= PALMAS_LDOLN_CTRL,
	},
	{
		.name		= "LDOUSB",
		.sname		= "ldousb-in",
		.vsel_addr	= PALMAS_LDOUSB_VOLTAGE,
		.ctrl_addr	= PALMAS_LDOUSB_CTRL,
	},
	{
		.name		= "REGEN1",
		.ctrl_addr	= PALMAS_REGEN1_CTRL,
	},
	{
		.name		= "REGEN2",
		.ctrl_addr	= PALMAS_REGEN2_CTRL,
	},
	{
		.name		= "REGEN3",
		.ctrl_addr	= PALMAS_REGEN3_CTRL,
	},
	{
		.name		= "SYSEN1",
		.ctrl_addr	= PALMAS_SYSEN1_CTRL,
	},
	{
		.name		= "SYSEN2",
		.ctrl_addr	= PALMAS_SYSEN2_CTRL,
	},
};

static unsigned int palmas_smps_ramp_delay[4] = {0, 10000, 5000, 2500};

#define SMPS_CTRL_MODE_OFF		0x00
#define SMPS_CTRL_MODE_ON		0x01
#define SMPS_CTRL_MODE_ECO		0x02
#define SMPS_CTRL_MODE_PWM		0x03

/* These values are derived from the data sheet. And are the number of steps
 * where there is a voltage change, the ranges at beginning and end of register
 * max/min values where there are no change are ommitted.
 *
 * So they are basically (maxV-minV)/stepV
 */
#define PALMAS_SMPS_NUM_VOLTAGES	117
#define PALMAS_SMPS10_NUM_VOLTAGES	2
#define PALMAS_LDO_NUM_VOLTAGES		50

#define SMPS10_VSEL			(1<<3)
#define SMPS10_BOOST_EN			(1<<2)
#define SMPS10_BYPASS_EN		(1<<1)
#define SMPS10_SWITCH_EN		(1<<0)

#define REGULATOR_SLAVE			0

static int palmas_smps_read(struct palmas *palmas, unsigned int reg,
		unsigned int *dest)
{
	unsigned int addr;

	addr = PALMAS_BASE_TO_REG(PALMAS_SMPS_BASE, reg);

	return regmap_read(palmas->regmap[REGULATOR_SLAVE], addr, dest);
}

static int palmas_smps_write(struct palmas *palmas, unsigned int reg,
		unsigned int value)
{
	unsigned int addr;

	addr = PALMAS_BASE_TO_REG(PALMAS_SMPS_BASE, reg);

	return regmap_write(palmas->regmap[REGULATOR_SLAVE], addr, value);
}

static int palmas_ldo_read(struct palmas *palmas, unsigned int reg,
		unsigned int *dest)
{
	unsigned int addr;

	addr = PALMAS_BASE_TO_REG(PALMAS_LDO_BASE, reg);

	return regmap_read(palmas->regmap[REGULATOR_SLAVE], addr, dest);
}

static int palmas_ldo_write(struct palmas *palmas, unsigned int reg,
		unsigned int value)
{
	unsigned int addr;

	addr = PALMAS_BASE_TO_REG(PALMAS_LDO_BASE, reg);

	return regmap_write(palmas->regmap[REGULATOR_SLAVE], addr, value);
}

static int palmas_is_enabled_smps(struct regulator_dev *dev)
{
	struct palmas_pmic *pmic = rdev_get_drvdata(dev);
	int id = rdev_get_id(dev);
	unsigned int reg;

	palmas_smps_read(pmic->palmas, palmas_regs_info[id].ctrl_addr, &reg);

	reg &= PALMAS_SMPS12_CTRL_STATUS_MASK;
	reg >>= PALMAS_SMPS12_CTRL_STATUS_SHIFT;

	return !!(reg);
}

static int palmas_enable_smps(struct regulator_dev *dev)
{
	struct palmas_pmic *pmic = rdev_get_drvdata(dev);
	int id = rdev_get_id(dev);
	unsigned int reg;

	palmas_smps_read(pmic->palmas, palmas_regs_info[id].ctrl_addr, &reg);

	reg &= ~PALMAS_SMPS12_CTRL_MODE_ACTIVE_MASK;
	if (pmic->current_reg_mode[id])
		reg |= pmic->current_reg_mode[id];
	else
		reg |= SMPS_CTRL_MODE_ON;

	palmas_smps_write(pmic->palmas, palmas_regs_info[id].ctrl_addr, reg);

	return 0;
}

static int palmas_disable_smps(struct regulator_dev *dev)
{
	struct palmas_pmic *pmic = rdev_get_drvdata(dev);
	int id = rdev_get_id(dev);
	unsigned int reg;

	palmas_smps_read(pmic->palmas, palmas_regs_info[id].ctrl_addr, &reg);

	reg &= ~PALMAS_SMPS12_CTRL_MODE_ACTIVE_MASK;

	palmas_smps_write(pmic->palmas, palmas_regs_info[id].ctrl_addr, reg);

	return 0;
}

static int palmas_set_mode_smps(struct regulator_dev *dev, unsigned int mode)
{
	struct palmas_pmic *pmic = rdev_get_drvdata(dev);
	int id = rdev_get_id(dev);
	unsigned int reg;
	bool rail_enable = true;

	palmas_smps_read(pmic->palmas, palmas_regs_info[id].ctrl_addr, &reg);
	reg &= ~PALMAS_SMPS12_CTRL_MODE_ACTIVE_MASK;

	if (reg == SMPS_CTRL_MODE_OFF)
		rail_enable = false;

	switch (mode) {
	case REGULATOR_MODE_NORMAL:
		reg |= SMPS_CTRL_MODE_ON;
		break;
	case REGULATOR_MODE_IDLE:
		reg |= SMPS_CTRL_MODE_ECO;
		break;
	case REGULATOR_MODE_FAST:
		reg |= SMPS_CTRL_MODE_PWM;
		break;
	default:
		return -EINVAL;
	}

	pmic->current_reg_mode[id] = reg & PALMAS_SMPS12_CTRL_MODE_ACTIVE_MASK;
	if (rail_enable)
		palmas_smps_write(pmic->palmas,
			palmas_regs_info[id].ctrl_addr, reg);
	return 0;
}

static unsigned int palmas_get_mode_smps(struct regulator_dev *dev)
{
	struct palmas_pmic *pmic = rdev_get_drvdata(dev);
	int id = rdev_get_id(dev);
	unsigned int reg;

	reg = pmic->current_reg_mode[id] & PALMAS_SMPS12_CTRL_MODE_ACTIVE_MASK;

	switch (reg) {
	case SMPS_CTRL_MODE_ON:
		return REGULATOR_MODE_NORMAL;
	case SMPS_CTRL_MODE_ECO:
		return REGULATOR_MODE_IDLE;
	case SMPS_CTRL_MODE_PWM:
		return REGULATOR_MODE_FAST;
	}

	return 0;
}

static int palmas_list_voltage_smps(struct regulator_dev *dev,
					unsigned selector)
{
	struct palmas_pmic *pmic = rdev_get_drvdata(dev);
	int id = rdev_get_id(dev);
	int mult = 1;

	/* Read the multiplier set in VSEL register to return
	 * the correct voltage.
	 */
	if (pmic->range[id])
		mult = 2;

	if (selector == 0)
		return 0;
	else if (selector < 6)
		return 500000 * mult;
	else
		/* Voltage is linear mapping starting from selector 6,
		 * volt = (0.49V + ((selector - 5) * 0.01V)) * RANGE
		 * RANGE is either x1 or x2
		 */
		return (490000 + ((selector - 5) * 10000)) * mult;
}

static int palmas_map_voltage_smps(struct regulator_dev *rdev,
		int min_uV, int max_uV)
{
	struct palmas_pmic *pmic = rdev_get_drvdata(rdev);
	int id = rdev_get_id(rdev);
	int ret, voltage;

	if (min_uV == 0)
		return 0;

	if (pmic->range[id]) { /* RANGE is x2 */
		if (min_uV < 1000000)
			min_uV = 1000000;
		ret = DIV_ROUND_UP(min_uV - 1000000, 20000) + 6;
	} else {		/* RANGE is x1 */
		if (min_uV < 500000)
			min_uV = 500000;
		ret = DIV_ROUND_UP(min_uV - 500000, 10000) + 6;
	}

	/* Map back into a voltage to verify we're still in bounds */
	voltage = palmas_list_voltage_smps(rdev, ret);
	if (voltage < min_uV || voltage > max_uV)
		return -EINVAL;

	return ret;
}

static int palma_smps_set_voltage_smps_time_sel(struct regulator_dev *rdev,
	unsigned int old_selector, unsigned int new_selector)
{
	struct palmas_pmic *pmic = rdev_get_drvdata(rdev);
	int id = rdev_get_id(rdev);
	int old_uv, new_uv;
	unsigned int ramp_delay = pmic->ramp_delay[id];

	if (!ramp_delay)
		return 0;

	old_uv = palmas_list_voltage_smps(rdev, old_selector);
	if (old_uv < 0)
		return old_uv;

	new_uv = palmas_list_voltage_smps(rdev, new_selector);
	if (new_uv < 0)
		return new_uv;

	return DIV_ROUND_UP(abs(old_uv - new_uv), ramp_delay);
}

static int palmas_smps_set_ramp_delay(struct regulator_dev *rdev,
		 int ramp_delay)
{
	struct palmas_pmic *pmic = rdev_get_drvdata(rdev);
	int id = rdev_get_id(rdev);
	unsigned int reg = 0;
	unsigned int addr = palmas_regs_info[id].tstep_addr;
	int ret;

	/* SMPS3 and SMPS7 do not have tstep_addr setting */
	switch (id) {
	case PALMAS_REG_SMPS3:
	case PALMAS_REG_SMPS7:
		return 0;
	}

	if (ramp_delay <= 0)
		reg = 0;
	else if (ramp_delay <= 2500)
		reg = 3;
	else if (ramp_delay <= 5000)
		reg = 2;
	else
		reg = 1;

	ret = palmas_smps_write(pmic->palmas, addr, reg);
	if (ret < 0) {
		dev_err(pmic->palmas->dev, "TSTEP write failed: %d\n", ret);
		return ret;
	}

	pmic->ramp_delay[id] = palmas_smps_ramp_delay[reg];
	return ret;
}

static struct regulator_ops palmas_ops_smps = {
	.is_enabled		= palmas_is_enabled_smps,
	.enable			= palmas_enable_smps,
	.disable		= palmas_disable_smps,
	.set_mode		= palmas_set_mode_smps,
	.get_mode		= palmas_get_mode_smps,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.list_voltage		= palmas_list_voltage_smps,
	.map_voltage		= palmas_map_voltage_smps,
	.set_voltage_time_sel	= palma_smps_set_voltage_smps_time_sel,
	.set_ramp_delay		= palmas_smps_set_ramp_delay,
};

static struct regulator_ops palmas_ops_smps10 = {
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
};

static int palmas_is_enabled_ldo(struct regulator_dev *dev)
{
	struct palmas_pmic *pmic = rdev_get_drvdata(dev);
	int id = rdev_get_id(dev);
	unsigned int reg;

	palmas_ldo_read(pmic->palmas, palmas_regs_info[id].ctrl_addr, &reg);

	reg &= PALMAS_LDO1_CTRL_STATUS;

	return !!(reg);
}

static struct regulator_ops palmas_ops_ldo = {
	.is_enabled		= palmas_is_enabled_ldo,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
};

static struct regulator_ops palmas_ops_extreg = {
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
};

/*
 * setup the hardware based sleep configuration of the SMPS/LDO regulators
 * from the platform data. This is different to the software based control
 * supported by the regulator framework as it is controlled by toggling
 * pins on the PMIC such as PREQ, SYSEN, ...
 */
static int palmas_smps_init(struct palmas *palmas, int id,
		struct palmas_reg_init *reg_init)
{
	unsigned int reg;
	unsigned int addr;
	int ret;

	addr = palmas_regs_info[id].ctrl_addr;

	ret = palmas_smps_read(palmas, addr, &reg);
	if (ret)
		return ret;

	switch (id) {
	case PALMAS_REG_SMPS10:
		reg &= ~PALMAS_SMPS10_CTRL_MODE_SLEEP_MASK;
		if (reg_init->mode_sleep)
			reg |= reg_init->mode_sleep <<
					PALMAS_SMPS10_CTRL_MODE_SLEEP_SHIFT;
		break;
	default:
		if (reg_init->warm_reset)
			reg |= PALMAS_SMPS12_CTRL_WR_S;
		else
			reg &= ~PALMAS_SMPS12_CTRL_WR_S;

		if (reg_init->roof_floor)
			reg |= PALMAS_SMPS12_CTRL_ROOF_FLOOR_EN;
		else
			reg &= ~PALMAS_SMPS12_CTRL_ROOF_FLOOR_EN;

		reg &= ~PALMAS_SMPS12_CTRL_MODE_SLEEP_MASK;
		if (reg_init->mode_sleep)
			reg |= reg_init->mode_sleep <<
					PALMAS_SMPS12_CTRL_MODE_SLEEP_SHIFT;
	}

	ret = palmas_smps_write(palmas, addr, reg);
	if (ret)
		return ret;

	if (palmas_regs_info[id].vsel_addr && reg_init->vsel) {
		addr = palmas_regs_info[id].vsel_addr;

		reg = reg_init->vsel;

		ret = palmas_smps_write(palmas, addr, reg);
		if (ret)
			return ret;
	}


	return 0;
}

static int palmas_ldo_init(struct palmas *palmas, int id,
		struct palmas_reg_init *reg_init)
{
	unsigned int reg;
	unsigned int addr;
	int ret;

	addr = palmas_regs_info[id].ctrl_addr;

	ret = palmas_ldo_read(palmas, addr, &reg);
	if (ret)
		return ret;

	if (reg_init->warm_reset)
		reg |= PALMAS_LDO1_CTRL_WR_S;
	else
		reg &= ~PALMAS_LDO1_CTRL_WR_S;

	if (reg_init->mode_sleep)
		reg |= PALMAS_LDO1_CTRL_MODE_SLEEP;
	else
		reg &= ~PALMAS_LDO1_CTRL_MODE_SLEEP;

	ret = palmas_ldo_write(palmas, addr, reg);
	if (ret)
		return ret;

	return 0;
}

static int palmas_extreg_init(struct palmas *palmas, int id,
		struct palmas_reg_init *reg_init)
{
	unsigned int addr;
	int ret;
	unsigned int val = 0;

	addr = palmas_regs_info[id].ctrl_addr;

	if (reg_init->mode_sleep)
		val = PALMAS_REGEN1_CTRL_MODE_SLEEP;

	ret = palmas_update_bits(palmas, PALMAS_RESOURCE_BASE,
			addr, PALMAS_REGEN1_CTRL_MODE_SLEEP, val);
	if (ret < 0) {
		dev_err(palmas->dev, "Resource reg 0x%02x update failed %d\n",
			addr, ret);
		return ret;
	}
	return 0;
}

static void palmas_enable_ldo8_track(struct palmas *palmas)
{
	unsigned int reg;
	unsigned int addr;
	int ret;

	addr = palmas_regs_info[PALMAS_REG_LDO8].ctrl_addr;

	ret = palmas_ldo_read(palmas, addr, &reg);
	if (ret) {
		dev_err(palmas->dev, "Error in reading ldo8 control reg\n");
		return;
	}

	reg |= PALMAS_LDO8_CTRL_LDO_TRACKING_EN;
	ret = palmas_ldo_write(palmas, addr, reg);
	if (ret < 0) {
		dev_err(palmas->dev, "Error in enabling tracking mode\n");
		return;
	}
	/*
	 * When SMPS45 is set to off and LDO8 tracking is enabled, the LDO8
	 * output is defined by the LDO8_VOLTAGE.VSEL register divided by two,
	 * and can be set from 0.45 to 1.65 V.
	 */
	addr = palmas_regs_info[PALMAS_REG_LDO8].vsel_addr;
	ret = palmas_ldo_read(palmas, addr, &reg);
	if (ret) {
		dev_err(palmas->dev, "Error in reading ldo8 voltage reg\n");
		return;
	}

	reg = (reg << 1) & PALMAS_LDO8_VOLTAGE_VSEL_MASK;
	ret = palmas_ldo_write(palmas, addr, reg);
	if (ret < 0)
		dev_err(palmas->dev, "Error in setting ldo8 voltage reg\n");

	return;
}

static struct of_regulator_match palmas_matches[] = {
	{ .name = "smps12", },
	{ .name = "smps123", },
	{ .name = "smps3", },
	{ .name = "smps45", },
	{ .name = "smps457", },
	{ .name = "smps6", },
	{ .name = "smps7", },
	{ .name = "smps8", },
	{ .name = "smps9", },
	{ .name = "smps10", },
	{ .name = "ldo1", },
	{ .name = "ldo2", },
	{ .name = "ldo3", },
	{ .name = "ldo4", },
	{ .name = "ldo5", },
	{ .name = "ldo6", },
	{ .name = "ldo7", },
	{ .name = "ldo8", },
	{ .name = "ldo9", },
	{ .name = "ldoln", },
	{ .name = "ldousb", },
	{ .name = "regen1", },
	{ .name = "regen2", },
	{ .name = "regen3", },
	{ .name = "sysen1", },
	{ .name = "sysen2", },
};

static void palmas_dt_to_pdata(struct device *dev,
		struct device_node *node,
		struct palmas_pmic_platform_data *pdata)
{
	struct device_node *regulators;
	u32 prop;
	int idx, ret;

	node = of_node_get(node);
	regulators = of_find_node_by_name(node, "regulators");
	if (!regulators) {
		dev_info(dev, "regulator node not found\n");
		return;
	}

	ret = of_regulator_match(dev, regulators, palmas_matches,
			PALMAS_NUM_REGS);
	of_node_put(regulators);
	if (ret < 0) {
		dev_err(dev, "Error parsing regulator init data: %d\n", ret);
		return;
	}

	for (idx = 0; idx < PALMAS_NUM_REGS; idx++) {
		if (!palmas_matches[idx].init_data ||
				!palmas_matches[idx].of_node)
			continue;

		pdata->reg_data[idx] = palmas_matches[idx].init_data;

		pdata->reg_init[idx] = devm_kzalloc(dev,
				sizeof(struct palmas_reg_init), GFP_KERNEL);

		pdata->reg_init[idx]->warm_reset =
			of_property_read_bool(palmas_matches[idx].of_node,
					     "ti,warm-reset");

		pdata->reg_init[idx]->roof_floor =
			of_property_read_bool(palmas_matches[idx].of_node,
					      "ti,roof-floor");

		ret = of_property_read_u32(palmas_matches[idx].of_node,
				"ti,mode-sleep", &prop);
		if (!ret)
			pdata->reg_init[idx]->mode_sleep = prop;

		ret = of_property_read_bool(palmas_matches[idx].of_node,
					    "ti,smps-range");
		if (ret)
			pdata->reg_init[idx]->vsel =
				PALMAS_SMPS12_VOLTAGE_RANGE;

		if (idx == PALMAS_REG_LDO8)
			pdata->enable_ldo8_tracking = of_property_read_bool(
						palmas_matches[idx].of_node,
						"ti,enable-ldo8-tracking");
	}

	pdata->ldo6_vibrator = of_property_read_bool(node, "ti,ldo6-vibrator");
}


static int palmas_regulators_probe(struct platform_device *pdev)
{
	struct palmas *palmas = dev_get_drvdata(pdev->dev.parent);
	struct palmas_pmic_platform_data *pdata = pdev->dev.platform_data;
	struct device_node *node = pdev->dev.of_node;
	struct regulator_dev *rdev;
	struct regulator_config config = { };
	struct palmas_pmic *pmic;
	struct palmas_reg_init *reg_init;
	int id = 0, ret;
	unsigned int addr, reg;

	if (node && !pdata) {
		pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);

		if (!pdata)
			return -ENOMEM;

		palmas_dt_to_pdata(&pdev->dev, node, pdata);
	}

	pmic = devm_kzalloc(&pdev->dev, sizeof(*pmic), GFP_KERNEL);
	if (!pmic)
		return -ENOMEM;

	pmic->dev = &pdev->dev;
	pmic->palmas = palmas;
	palmas->pmic = pmic;
	platform_set_drvdata(pdev, pmic);

	ret = palmas_smps_read(palmas, PALMAS_SMPS_CTRL, &reg);
	if (ret)
		return ret;

	if (reg & PALMAS_SMPS_CTRL_SMPS12_SMPS123_EN)
		pmic->smps123 = 1;

	if (reg & PALMAS_SMPS_CTRL_SMPS45_SMPS457_EN)
		pmic->smps457 = 1;

	config.regmap = palmas->regmap[REGULATOR_SLAVE];
	config.dev = &pdev->dev;
	config.driver_data = pmic;

	for (id = 0; id < PALMAS_REG_LDO1; id++) {
		bool ramp_delay_support = false;

		/*
		 * Miss out regulators which are not available due
		 * to slaving configurations.
		 */
		switch (id) {
		case PALMAS_REG_SMPS12:
		case PALMAS_REG_SMPS3:
			if (pmic->smps123)
				continue;
			if (id == PALMAS_REG_SMPS12)
				ramp_delay_support = true;
			break;
		case PALMAS_REG_SMPS123:
			if (!pmic->smps123)
				continue;
			ramp_delay_support = true;
			break;
		case PALMAS_REG_SMPS45:
		case PALMAS_REG_SMPS7:
			if (pmic->smps457)
				continue;
			if (id == PALMAS_REG_SMPS45)
				ramp_delay_support = true;
			break;
		case PALMAS_REG_SMPS457:
			if (!pmic->smps457)
				continue;
			ramp_delay_support = true;
			break;
		}

		if ((id == PALMAS_REG_SMPS6) || (id == PALMAS_REG_SMPS8))
			ramp_delay_support = true;

		if (ramp_delay_support) {
			addr = palmas_regs_info[id].tstep_addr;
			ret = palmas_smps_read(pmic->palmas, addr, &reg);
			if (ret < 0) {
				dev_err(&pdev->dev,
					"reading TSTEP reg failed: %d\n", ret);
				goto err_unregister_regulator;
			}
			pmic->desc[id].ramp_delay =
					palmas_smps_ramp_delay[reg & 0x3];
			pmic->ramp_delay[id] = pmic->desc[id].ramp_delay;
		}

		/* Initialise sleep/init values from platform data */
		if (pdata && pdata->reg_init[id]) {
			reg_init = pdata->reg_init[id];
			ret = palmas_smps_init(palmas, id, reg_init);
			if (ret)
				goto err_unregister_regulator;
		}

		/* Register the regulators */
		pmic->desc[id].name = palmas_regs_info[id].name;
		pmic->desc[id].id = id;

		switch (id) {
		case PALMAS_REG_SMPS10:
			pmic->desc[id].n_voltages = PALMAS_SMPS10_NUM_VOLTAGES;
			pmic->desc[id].ops = &palmas_ops_smps10;
			pmic->desc[id].vsel_reg =
					PALMAS_BASE_TO_REG(PALMAS_SMPS_BASE,
							PALMAS_SMPS10_CTRL);
			pmic->desc[id].vsel_mask = SMPS10_VSEL;
			pmic->desc[id].enable_reg =
					PALMAS_BASE_TO_REG(PALMAS_SMPS_BASE,
							PALMAS_SMPS10_CTRL);
			pmic->desc[id].enable_mask = SMPS10_BOOST_EN;
			pmic->desc[id].min_uV = 3750000;
			pmic->desc[id].uV_step = 1250000;
			break;
		default:
			/*
			 * Read and store the RANGE bit for later use
			 * This must be done before regulator is probed,
			 * otherwise we error in probe with unsupportable
			 * ranges. Read the current smps mode for later use.
			 */
			addr = palmas_regs_info[id].vsel_addr;

			ret = palmas_smps_read(pmic->palmas, addr, &reg);
			if (ret)
				goto err_unregister_regulator;
			if (reg & PALMAS_SMPS12_VOLTAGE_RANGE)
				pmic->range[id] = 1;

			pmic->desc[id].ops = &palmas_ops_smps;
			pmic->desc[id].n_voltages = PALMAS_SMPS_NUM_VOLTAGES;
			pmic->desc[id].vsel_reg =
					PALMAS_BASE_TO_REG(PALMAS_SMPS_BASE,
						palmas_regs_info[id].vsel_addr);
			pmic->desc[id].vsel_mask =
					PALMAS_SMPS12_VOLTAGE_VSEL_MASK;

			/* Read the smps mode for later use. */
			addr = palmas_regs_info[id].ctrl_addr;
			ret = palmas_smps_read(pmic->palmas, addr, &reg);
			if (ret)
				goto err_unregister_regulator;
			pmic->current_reg_mode[id] = reg &
					PALMAS_SMPS12_CTRL_MODE_ACTIVE_MASK;
		}

		pmic->desc[id].type = REGULATOR_VOLTAGE;
		pmic->desc[id].owner = THIS_MODULE;

		if (pdata)
			config.init_data = pdata->reg_data[id];
		else
			config.init_data = NULL;

		pmic->desc[id].supply_name = palmas_regs_info[id].sname;
		config.of_node = palmas_matches[id].of_node;

		rdev = regulator_register(&pmic->desc[id], &config);
		if (IS_ERR(rdev)) {
			dev_err(&pdev->dev,
				"failed to register %s regulator\n",
				pdev->name);
			ret = PTR_ERR(rdev);
			goto err_unregister_regulator;
		}

		/* Save regulator for cleanup */
		pmic->rdev[id] = rdev;
	}

	/* Start this loop from the id left from previous loop */
	for (; id < PALMAS_NUM_REGS; id++) {

		/* Miss out regulators which are not available due
		 * to alternate functions.
		 */

		/* Register the regulators */
		pmic->desc[id].name = palmas_regs_info[id].name;
		pmic->desc[id].id = id;
		pmic->desc[id].type = REGULATOR_VOLTAGE;
		pmic->desc[id].owner = THIS_MODULE;

		if (id < PALMAS_REG_REGEN1) {
			pmic->desc[id].n_voltages = PALMAS_LDO_NUM_VOLTAGES;
			pmic->desc[id].ops = &palmas_ops_ldo;
			pmic->desc[id].min_uV = 900000;
			pmic->desc[id].uV_step = 50000;
			pmic->desc[id].linear_min_sel = 1;
			pmic->desc[id].vsel_reg =
					PALMAS_BASE_TO_REG(PALMAS_LDO_BASE,
						palmas_regs_info[id].vsel_addr);
			pmic->desc[id].vsel_mask =
					PALMAS_LDO1_VOLTAGE_VSEL_MASK;
			pmic->desc[id].enable_reg =
					PALMAS_BASE_TO_REG(PALMAS_LDO_BASE,
						palmas_regs_info[id].ctrl_addr);
			pmic->desc[id].enable_mask =
					PALMAS_LDO1_CTRL_MODE_ACTIVE;

			/* Check if LDO8 is in tracking mode or not */
			if (pdata && (id == PALMAS_REG_LDO8) &&
					pdata->enable_ldo8_tracking) {
				palmas_enable_ldo8_track(palmas);
				pmic->desc[id].min_uV = 450000;
				pmic->desc[id].uV_step = 25000;
			}
		} else {
			pmic->desc[id].n_voltages = 1;
			pmic->desc[id].ops = &palmas_ops_extreg;
			pmic->desc[id].enable_reg =
					PALMAS_BASE_TO_REG(PALMAS_RESOURCE_BASE,
						palmas_regs_info[id].ctrl_addr);
			pmic->desc[id].enable_mask =
					PALMAS_REGEN1_CTRL_MODE_ACTIVE;
		}

		if (pdata)
			config.init_data = pdata->reg_data[id];
		else
			config.init_data = NULL;

		pmic->desc[id].supply_name = palmas_regs_info[id].sname;
		config.of_node = palmas_matches[id].of_node;

		rdev = regulator_register(&pmic->desc[id], &config);
		if (IS_ERR(rdev)) {
			dev_err(&pdev->dev,
				"failed to register %s regulator\n",
				pdev->name);
			ret = PTR_ERR(rdev);
			goto err_unregister_regulator;
		}

		/* Save regulator for cleanup */
		pmic->rdev[id] = rdev;

		/* Initialise sleep/init values from platform data */
		if (pdata) {
			reg_init = pdata->reg_init[id];
			if (reg_init) {
				if (id < PALMAS_REG_REGEN1)
					ret = palmas_ldo_init(palmas,
							id, reg_init);
				else
					ret = palmas_extreg_init(palmas,
							id, reg_init);
				if (ret) {
					regulator_unregister(pmic->rdev[id]);
					goto err_unregister_regulator;
				}
			}
		}
	}


	return 0;

err_unregister_regulator:
	while (--id >= 0)
		regulator_unregister(pmic->rdev[id]);
	return ret;
}

static int palmas_regulators_remove(struct platform_device *pdev)
{
	struct palmas_pmic *pmic = platform_get_drvdata(pdev);
	int id;

	for (id = 0; id < PALMAS_NUM_REGS; id++)
		regulator_unregister(pmic->rdev[id]);
	return 0;
}

static struct of_device_id of_palmas_match_tbl[] = {
	{ .compatible = "ti,palmas-pmic", },
	{ .compatible = "ti,twl6035-pmic", },
	{ .compatible = "ti,twl6036-pmic", },
	{ .compatible = "ti,twl6037-pmic", },
	{ .compatible = "ti,tps65913-pmic", },
	{ .compatible = "ti,tps65914-pmic", },
	{ .compatible = "ti,tps80036-pmic", },
	{ /* end */ }
};

static struct platform_driver palmas_driver = {
	.driver = {
		.name = "palmas-pmic",
		.of_match_table = of_palmas_match_tbl,
		.owner = THIS_MODULE,
	},
	.probe = palmas_regulators_probe,
	.remove = palmas_regulators_remove,
};

static int __init palmas_init(void)
{
	return platform_driver_register(&palmas_driver);
}
subsys_initcall(palmas_init);

static void __exit palmas_exit(void)
{
	platform_driver_unregister(&palmas_driver);
}
module_exit(palmas_exit);

MODULE_AUTHOR("Graeme Gregory <gg@slimlogic.co.uk>");
MODULE_DESCRIPTION("Palmas voltage regulator driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:palmas-pmic");
MODULE_DEVICE_TABLE(of, of_palmas_match_tbl);
