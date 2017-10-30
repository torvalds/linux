/*
 * Maxim MAX77620 Regulator driver
 *
 * Copyright (c) 2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * Author: Mallikarjun Kasoju <mkasoju@nvidia.com>
 *	Laxman Dewangan <ldewangan@nvidia.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/mfd/max77620.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>

#define max77620_rails(_name)	"max77620-"#_name

/* Power Mode */
#define MAX77620_POWER_MODE_NORMAL		3
#define MAX77620_POWER_MODE_LPM			2
#define MAX77620_POWER_MODE_GLPM		1
#define MAX77620_POWER_MODE_DISABLE		0

/* SD Slew Rate */
#define MAX77620_SD_SR_13_75			0
#define MAX77620_SD_SR_27_5			1
#define MAX77620_SD_SR_55			2
#define MAX77620_SD_SR_100			3

enum max77620_regulators {
	MAX77620_REGULATOR_ID_SD0,
	MAX77620_REGULATOR_ID_SD1,
	MAX77620_REGULATOR_ID_SD2,
	MAX77620_REGULATOR_ID_SD3,
	MAX77620_REGULATOR_ID_SD4,
	MAX77620_REGULATOR_ID_LDO0,
	MAX77620_REGULATOR_ID_LDO1,
	MAX77620_REGULATOR_ID_LDO2,
	MAX77620_REGULATOR_ID_LDO3,
	MAX77620_REGULATOR_ID_LDO4,
	MAX77620_REGULATOR_ID_LDO5,
	MAX77620_REGULATOR_ID_LDO6,
	MAX77620_REGULATOR_ID_LDO7,
	MAX77620_REGULATOR_ID_LDO8,
	MAX77620_NUM_REGS,
};

/* Regulator types */
enum max77620_regulator_type {
	MAX77620_REGULATOR_TYPE_SD,
	MAX77620_REGULATOR_TYPE_LDO_N,
	MAX77620_REGULATOR_TYPE_LDO_P,
};

struct max77620_regulator_info {
	u8 type;
	u8 fps_addr;
	u8 volt_addr;
	u8 cfg_addr;
	u8 power_mode_mask;
	u8 power_mode_shift;
	u8 remote_sense_addr;
	u8 remote_sense_mask;
	struct regulator_desc desc;
};

struct max77620_regulator_pdata {
	int active_fps_src;
	int active_fps_pd_slot;
	int active_fps_pu_slot;
	int suspend_fps_src;
	int suspend_fps_pd_slot;
	int suspend_fps_pu_slot;
	int current_mode;
	int power_ok;
	int ramp_rate_setting;
};

struct max77620_regulator {
	struct device *dev;
	struct regmap *rmap;
	struct max77620_regulator_info *rinfo[MAX77620_NUM_REGS];
	struct max77620_regulator_pdata reg_pdata[MAX77620_NUM_REGS];
	int enable_power_mode[MAX77620_NUM_REGS];
	int current_power_mode[MAX77620_NUM_REGS];
	int active_fps_src[MAX77620_NUM_REGS];
};

#define fps_src_name(fps_src)	\
	(fps_src == MAX77620_FPS_SRC_0 ? "FPS_SRC_0" :	\
	fps_src == MAX77620_FPS_SRC_1 ? "FPS_SRC_1" :	\
	fps_src == MAX77620_FPS_SRC_2 ? "FPS_SRC_2" : "FPS_SRC_NONE")

static int max77620_regulator_get_fps_src(struct max77620_regulator *pmic,
					  int id)
{
	struct max77620_regulator_info *rinfo = pmic->rinfo[id];
	unsigned int val;
	int ret;

	ret = regmap_read(pmic->rmap, rinfo->fps_addr, &val);
	if (ret < 0) {
		dev_err(pmic->dev, "Reg 0x%02x read failed %d\n",
			rinfo->fps_addr, ret);
		return ret;
	}

	return (val & MAX77620_FPS_SRC_MASK) >> MAX77620_FPS_SRC_SHIFT;
}

static int max77620_regulator_set_fps_src(struct max77620_regulator *pmic,
					  int fps_src, int id)
{
	struct max77620_regulator_info *rinfo = pmic->rinfo[id];
	unsigned int val;
	int ret;

	if (!rinfo)
		return 0;

	switch (fps_src) {
	case MAX77620_FPS_SRC_0:
	case MAX77620_FPS_SRC_1:
	case MAX77620_FPS_SRC_2:
	case MAX77620_FPS_SRC_NONE:
		break;

	case MAX77620_FPS_SRC_DEF:
		ret = regmap_read(pmic->rmap, rinfo->fps_addr, &val);
		if (ret < 0) {
			dev_err(pmic->dev, "Reg 0x%02x read failed %d\n",
				rinfo->fps_addr, ret);
			return ret;
		}
		ret = (val & MAX77620_FPS_SRC_MASK) >> MAX77620_FPS_SRC_SHIFT;
		pmic->active_fps_src[id] = ret;
		return 0;

	default:
		dev_err(pmic->dev, "Invalid FPS %d for regulator %d\n",
			fps_src, id);
		return -EINVAL;
	}

	ret = regmap_update_bits(pmic->rmap, rinfo->fps_addr,
				 MAX77620_FPS_SRC_MASK,
				 fps_src << MAX77620_FPS_SRC_SHIFT);
	if (ret < 0) {
		dev_err(pmic->dev, "Reg 0x%02x update failed %d\n",
			rinfo->fps_addr, ret);
		return ret;
	}
	pmic->active_fps_src[id] = fps_src;

	return 0;
}

static int max77620_regulator_set_fps_slots(struct max77620_regulator *pmic,
					    int id, bool is_suspend)
{
	struct max77620_regulator_pdata *rpdata = &pmic->reg_pdata[id];
	struct max77620_regulator_info *rinfo = pmic->rinfo[id];
	unsigned int val = 0;
	unsigned int mask = 0;
	int pu = rpdata->active_fps_pu_slot;
	int pd = rpdata->active_fps_pd_slot;
	int ret = 0;

	if (!rinfo)
		return 0;

	if (is_suspend) {
		pu = rpdata->suspend_fps_pu_slot;
		pd = rpdata->suspend_fps_pd_slot;
	}

	/* FPS power up period setting */
	if (pu >= 0) {
		val |= (pu << MAX77620_FPS_PU_PERIOD_SHIFT);
		mask |= MAX77620_FPS_PU_PERIOD_MASK;
	}

	/* FPS power down period setting */
	if (pd >= 0) {
		val |= (pd << MAX77620_FPS_PD_PERIOD_SHIFT);
		mask |= MAX77620_FPS_PD_PERIOD_MASK;
	}

	if (mask) {
		ret = regmap_update_bits(pmic->rmap, rinfo->fps_addr,
					 mask, val);
		if (ret < 0) {
			dev_err(pmic->dev, "Reg 0x%02x update failed: %d\n",
				rinfo->fps_addr, ret);
			return ret;
		}
	}

	return ret;
}

static int max77620_regulator_set_power_mode(struct max77620_regulator *pmic,
					     int power_mode, int id)
{
	struct max77620_regulator_info *rinfo = pmic->rinfo[id];
	u8 mask = rinfo->power_mode_mask;
	u8 shift = rinfo->power_mode_shift;
	u8 addr;
	int ret;

	switch (rinfo->type) {
	case MAX77620_REGULATOR_TYPE_SD:
		addr = rinfo->cfg_addr;
		break;
	default:
		addr = rinfo->volt_addr;
		break;
	}

	ret = regmap_update_bits(pmic->rmap, addr, mask, power_mode << shift);
	if (ret < 0) {
		dev_err(pmic->dev, "Regulator %d mode set failed: %d\n",
			id, ret);
		return ret;
	}
	pmic->current_power_mode[id] = power_mode;

	return ret;
}

static int max77620_regulator_get_power_mode(struct max77620_regulator *pmic,
					     int id)
{
	struct max77620_regulator_info *rinfo = pmic->rinfo[id];
	unsigned int val, addr;
	u8 mask = rinfo->power_mode_mask;
	u8 shift = rinfo->power_mode_shift;
	int ret;

	switch (rinfo->type) {
	case MAX77620_REGULATOR_TYPE_SD:
		addr = rinfo->cfg_addr;
		break;
	default:
		addr = rinfo->volt_addr;
		break;
	}

	ret = regmap_read(pmic->rmap, addr, &val);
	if (ret < 0) {
		dev_err(pmic->dev, "Regulator %d: Reg 0x%02x read failed: %d\n",
			id, addr, ret);
		return ret;
	}

	return (val & mask) >> shift;
}

static int max77620_read_slew_rate(struct max77620_regulator *pmic, int id)
{
	struct max77620_regulator_info *rinfo = pmic->rinfo[id];
	unsigned int rval;
	int slew_rate;
	int ret;

	ret = regmap_read(pmic->rmap, rinfo->cfg_addr, &rval);
	if (ret < 0) {
		dev_err(pmic->dev, "Register 0x%02x read failed: %d\n",
			rinfo->cfg_addr, ret);
		return ret;
	}

	switch (rinfo->type) {
	case MAX77620_REGULATOR_TYPE_SD:
		slew_rate = (rval >> MAX77620_SD_SR_SHIFT) & 0x3;
		switch (slew_rate) {
		case 0:
			slew_rate = 13750;
			break;
		case 1:
			slew_rate = 27500;
			break;
		case 2:
			slew_rate = 55000;
			break;
		case 3:
			slew_rate = 100000;
			break;
		}
		rinfo->desc.ramp_delay = slew_rate;
		break;
	default:
		slew_rate = rval & 0x1;
		switch (slew_rate) {
		case 0:
			slew_rate = 100000;
			break;
		case 1:
			slew_rate = 5000;
			break;
		}
		rinfo->desc.ramp_delay = slew_rate;
		break;
	}

	return 0;
}

static int max77620_set_slew_rate(struct max77620_regulator *pmic, int id,
				  int slew_rate)
{
	struct max77620_regulator_info *rinfo = pmic->rinfo[id];
	unsigned int val;
	int ret;
	u8 mask;

	if (rinfo->type == MAX77620_REGULATOR_TYPE_SD) {
		if (slew_rate <= 13750)
			val = 0;
		else if (slew_rate <= 27500)
			val = 1;
		else if (slew_rate <= 55000)
			val = 2;
		else
			val = 3;
		val <<= MAX77620_SD_SR_SHIFT;
		mask = MAX77620_SD_SR_MASK;
	} else {
		if (slew_rate <= 5000)
			val = 1;
		else
			val = 0;
		mask = MAX77620_LDO_SLEW_RATE_MASK;
	}

	ret = regmap_update_bits(pmic->rmap, rinfo->cfg_addr, mask, val);
	if (ret < 0) {
		dev_err(pmic->dev, "Regulator %d slew rate set failed: %d\n",
			id, ret);
		return ret;
	}

	return 0;
}

static int max77620_config_power_ok(struct max77620_regulator *pmic, int id)
{
	struct max77620_regulator_pdata *rpdata = &pmic->reg_pdata[id];
	struct max77620_regulator_info *rinfo = pmic->rinfo[id];
	struct max77620_chip *chip = dev_get_drvdata(pmic->dev->parent);
	u8 val, mask;
	int ret;

	switch (chip->chip_id) {
	case MAX20024:
		if (rpdata->power_ok >= 0) {
			if (rinfo->type == MAX77620_REGULATOR_TYPE_SD)
				mask = MAX20024_SD_CFG1_MPOK_MASK;
			else
				mask = MAX20024_LDO_CFG2_MPOK_MASK;

			val = rpdata->power_ok ? mask : 0;

			ret = regmap_update_bits(pmic->rmap, rinfo->cfg_addr,
						 mask, val);
			if (ret < 0) {
				dev_err(pmic->dev, "Reg 0x%02x update failed %d\n",
					rinfo->cfg_addr, ret);
				return ret;
			}
		}
		break;

	default:
		break;
	}

	return 0;
}

static int max77620_init_pmic(struct max77620_regulator *pmic, int id)
{
	struct max77620_regulator_pdata *rpdata = &pmic->reg_pdata[id];
	int ret;

	max77620_config_power_ok(pmic, id);

	/* Update power mode */
	ret = max77620_regulator_get_power_mode(pmic, id);
	if (ret < 0)
		return ret;

	pmic->current_power_mode[id] = ret;
	pmic->enable_power_mode[id] = MAX77620_POWER_MODE_NORMAL;

	if (rpdata->active_fps_src == MAX77620_FPS_SRC_DEF) {
		ret = max77620_regulator_get_fps_src(pmic, id);
		if (ret < 0)
			return ret;
		rpdata->active_fps_src = ret;
	}

	 /* If rails are externally control of FPS then enable it always. */
	if (rpdata->active_fps_src == MAX77620_FPS_SRC_NONE) {
		ret = max77620_regulator_set_power_mode(pmic,
					pmic->enable_power_mode[id], id);
		if (ret < 0)
			return ret;
	} else {
		if (pmic->current_power_mode[id] !=
		     pmic->enable_power_mode[id]) {
			ret = max77620_regulator_set_power_mode(pmic,
					pmic->enable_power_mode[id], id);
			if (ret < 0)
				return ret;
		}
	}

	ret = max77620_regulator_set_fps_src(pmic, rpdata->active_fps_src, id);
	if (ret < 0)
		return ret;

	ret = max77620_regulator_set_fps_slots(pmic, id, false);
	if (ret < 0)
		return ret;

	if (rpdata->ramp_rate_setting) {
		ret = max77620_set_slew_rate(pmic, id,
					     rpdata->ramp_rate_setting);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int max77620_regulator_enable(struct regulator_dev *rdev)
{
	struct max77620_regulator *pmic = rdev_get_drvdata(rdev);
	int id = rdev_get_id(rdev);

	if (pmic->active_fps_src[id] != MAX77620_FPS_SRC_NONE)
		return 0;

	return max77620_regulator_set_power_mode(pmic,
			pmic->enable_power_mode[id], id);
}

static int max77620_regulator_disable(struct regulator_dev *rdev)
{
	struct max77620_regulator *pmic = rdev_get_drvdata(rdev);
	int id = rdev_get_id(rdev);

	if (pmic->active_fps_src[id] != MAX77620_FPS_SRC_NONE)
		return 0;

	return max77620_regulator_set_power_mode(pmic,
			MAX77620_POWER_MODE_DISABLE, id);
}

static int max77620_regulator_is_enabled(struct regulator_dev *rdev)
{
	struct max77620_regulator *pmic = rdev_get_drvdata(rdev);
	int id = rdev_get_id(rdev);
	int ret = 1;

	if (pmic->active_fps_src[id] != MAX77620_FPS_SRC_NONE)
		return 1;

	ret = max77620_regulator_get_power_mode(pmic, id);
	if (ret < 0)
		return ret;

	if (ret != MAX77620_POWER_MODE_DISABLE)
		return 1;

	return 0;
}

static int max77620_regulator_set_mode(struct regulator_dev *rdev,
				       unsigned int mode)
{
	struct max77620_regulator *pmic = rdev_get_drvdata(rdev);
	int id = rdev_get_id(rdev);
	struct max77620_regulator_info *rinfo = pmic->rinfo[id];
	struct max77620_regulator_pdata *rpdata = &pmic->reg_pdata[id];
	bool fpwm = false;
	int power_mode;
	int ret;
	u8 val;

	switch (mode) {
	case REGULATOR_MODE_FAST:
		fpwm = true;
		power_mode = MAX77620_POWER_MODE_NORMAL;
		break;

	case REGULATOR_MODE_NORMAL:
		power_mode = MAX77620_POWER_MODE_NORMAL;
		break;

	case REGULATOR_MODE_IDLE:
		power_mode = MAX77620_POWER_MODE_LPM;
		break;

	default:
		dev_err(pmic->dev, "Regulator %d mode %d is invalid\n",
			id, mode);
		return -EINVAL;
	}

	if (rinfo->type != MAX77620_REGULATOR_TYPE_SD)
		goto skip_fpwm;

	val = (fpwm) ? MAX77620_SD_FPWM_MASK : 0;
	ret = regmap_update_bits(pmic->rmap, rinfo->cfg_addr,
				 MAX77620_SD_FPWM_MASK, val);
	if (ret < 0) {
		dev_err(pmic->dev, "Reg 0x%02x update failed: %d\n",
			rinfo->cfg_addr, ret);
		return ret;
	}
	rpdata->current_mode = mode;

skip_fpwm:
	ret = max77620_regulator_set_power_mode(pmic, power_mode, id);
	if (ret < 0)
		return ret;

	pmic->enable_power_mode[id] = power_mode;

	return 0;
}

static unsigned int max77620_regulator_get_mode(struct regulator_dev *rdev)
{
	struct max77620_regulator *pmic = rdev_get_drvdata(rdev);
	int id = rdev_get_id(rdev);
	struct max77620_regulator_info *rinfo = pmic->rinfo[id];
	int fpwm = 0;
	int ret;
	int pm_mode, reg_mode;
	unsigned int val;

	ret = max77620_regulator_get_power_mode(pmic, id);
	if (ret < 0)
		return 0;

	pm_mode = ret;

	if (rinfo->type == MAX77620_REGULATOR_TYPE_SD) {
		ret = regmap_read(pmic->rmap, rinfo->cfg_addr, &val);
		if (ret < 0) {
			dev_err(pmic->dev, "Reg 0x%02x read failed: %d\n",
				rinfo->cfg_addr, ret);
			return ret;
		}
		fpwm = !!(val & MAX77620_SD_FPWM_MASK);
	}

	switch (pm_mode) {
	case MAX77620_POWER_MODE_NORMAL:
	case MAX77620_POWER_MODE_DISABLE:
		if (fpwm)
			reg_mode = REGULATOR_MODE_FAST;
		else
			reg_mode = REGULATOR_MODE_NORMAL;
		break;
	case MAX77620_POWER_MODE_LPM:
	case MAX77620_POWER_MODE_GLPM:
		reg_mode = REGULATOR_MODE_IDLE;
		break;
	default:
		return 0;
	}

	return reg_mode;
}

static int max77620_regulator_set_ramp_delay(struct regulator_dev *rdev,
					     int ramp_delay)
{
	struct max77620_regulator *pmic = rdev_get_drvdata(rdev);
	int id = rdev_get_id(rdev);
	struct max77620_regulator_pdata *rpdata = &pmic->reg_pdata[id];

	/* Device specific ramp rate setting tells that platform has
	 * different ramp rate from advertised value. In this case,
	 * do not configure anything and just return success.
	 */
	if (rpdata->ramp_rate_setting)
		return 0;

	return max77620_set_slew_rate(pmic, id, ramp_delay);
}

static int max77620_of_parse_cb(struct device_node *np,
				const struct regulator_desc *desc,
				struct regulator_config *config)
{
	struct max77620_regulator *pmic = config->driver_data;
	struct max77620_regulator_pdata *rpdata = &pmic->reg_pdata[desc->id];
	u32 pval;
	int ret;

	ret = of_property_read_u32(np, "maxim,active-fps-source", &pval);
	rpdata->active_fps_src = (!ret) ? pval : MAX77620_FPS_SRC_DEF;

	ret = of_property_read_u32(np, "maxim,active-fps-power-up-slot", &pval);
	rpdata->active_fps_pu_slot = (!ret) ? pval : -1;

	ret = of_property_read_u32(
			np, "maxim,active-fps-power-down-slot", &pval);
	rpdata->active_fps_pd_slot = (!ret) ? pval : -1;

	ret = of_property_read_u32(np, "maxim,suspend-fps-source", &pval);
	rpdata->suspend_fps_src = (!ret) ? pval : -1;

	ret = of_property_read_u32(
			np, "maxim,suspend-fps-power-up-slot", &pval);
	rpdata->suspend_fps_pu_slot = (!ret) ? pval : -1;

	ret = of_property_read_u32(
			np, "maxim,suspend-fps-power-down-slot", &pval);
	rpdata->suspend_fps_pd_slot = (!ret) ? pval : -1;

	ret = of_property_read_u32(np, "maxim,power-ok-control", &pval);
	if (!ret)
		rpdata->power_ok = pval;
	else
		rpdata->power_ok = -1;

	ret = of_property_read_u32(np, "maxim,ramp-rate-setting", &pval);
	rpdata->ramp_rate_setting = (!ret) ? pval : 0;

	return max77620_init_pmic(pmic, desc->id);
}

static const struct regulator_ops max77620_regulator_ops = {
	.is_enabled = max77620_regulator_is_enabled,
	.enable = max77620_regulator_enable,
	.disable = max77620_regulator_disable,
	.list_voltage = regulator_list_voltage_linear,
	.map_voltage = regulator_map_voltage_linear,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.set_mode = max77620_regulator_set_mode,
	.get_mode = max77620_regulator_get_mode,
	.set_ramp_delay = max77620_regulator_set_ramp_delay,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
	.set_active_discharge = regulator_set_active_discharge_regmap,
};

#define MAX77620_SD_CNF2_ROVS_EN_NONE	0
#define RAIL_SD(_id, _name, _sname, _volt_mask, _min_uV, _max_uV,	\
		_step_uV, _rs_add, _rs_mask)				\
	[MAX77620_REGULATOR_ID_##_id] = {				\
		.type = MAX77620_REGULATOR_TYPE_SD,			\
		.volt_addr = MAX77620_REG_##_id,			\
		.cfg_addr = MAX77620_REG_##_id##_CFG,			\
		.fps_addr = MAX77620_REG_FPS_##_id,			\
		.remote_sense_addr = _rs_add,				\
		.remote_sense_mask = MAX77620_SD_CNF2_ROVS_EN_##_rs_mask, \
		.power_mode_mask = MAX77620_SD_POWER_MODE_MASK,		\
		.power_mode_shift = MAX77620_SD_POWER_MODE_SHIFT,	\
		.desc = {						\
			.name = max77620_rails(_name),			\
			.of_match = of_match_ptr(#_name),		\
			.regulators_node = of_match_ptr("regulators"),	\
			.of_parse_cb = max77620_of_parse_cb,		\
			.supply_name = _sname,				\
			.id = MAX77620_REGULATOR_ID_##_id,		\
			.ops = &max77620_regulator_ops,			\
			.n_voltages = ((_max_uV - _min_uV) / _step_uV) + 1, \
			.min_uV = _min_uV,				\
			.uV_step = _step_uV,				\
			.enable_time = 500,				\
			.vsel_mask = MAX77620_##_volt_mask##_VOLT_MASK,	\
			.vsel_reg = MAX77620_REG_##_id,			\
			.active_discharge_off = 0,			\
			.active_discharge_on = MAX77620_SD_CFG1_ADE_ENABLE, \
			.active_discharge_mask = MAX77620_SD_CFG1_ADE_MASK, \
			.active_discharge_reg = MAX77620_REG_##_id##_CFG, \
			.type = REGULATOR_VOLTAGE,			\
		},							\
	}

#define RAIL_LDO(_id, _name, _sname, _type, _min_uV, _max_uV, _step_uV) \
	[MAX77620_REGULATOR_ID_##_id] = {				\
		.type = MAX77620_REGULATOR_TYPE_LDO_##_type,		\
		.volt_addr = MAX77620_REG_##_id##_CFG,			\
		.cfg_addr = MAX77620_REG_##_id##_CFG2,			\
		.fps_addr = MAX77620_REG_FPS_##_id,			\
		.remote_sense_addr = 0xFF,				\
		.power_mode_mask = MAX77620_LDO_POWER_MODE_MASK,	\
		.power_mode_shift = MAX77620_LDO_POWER_MODE_SHIFT,	\
		.desc = {						\
			.name = max77620_rails(_name),			\
			.of_match = of_match_ptr(#_name),		\
			.regulators_node = of_match_ptr("regulators"),	\
			.of_parse_cb = max77620_of_parse_cb,		\
			.supply_name = _sname,				\
			.id = MAX77620_REGULATOR_ID_##_id,		\
			.ops = &max77620_regulator_ops,			\
			.n_voltages = ((_max_uV - _min_uV) / _step_uV) + 1, \
			.min_uV = _min_uV,				\
			.uV_step = _step_uV,				\
			.enable_time = 500,				\
			.vsel_mask = MAX77620_LDO_VOLT_MASK,		\
			.vsel_reg = MAX77620_REG_##_id##_CFG,		\
			.active_discharge_off = 0,			\
			.active_discharge_on = MAX77620_LDO_CFG2_ADE_ENABLE, \
			.active_discharge_mask = MAX77620_LDO_CFG2_ADE_MASK, \
			.active_discharge_reg = MAX77620_REG_##_id##_CFG2, \
			.type = REGULATOR_VOLTAGE,			\
		},							\
	}

static struct max77620_regulator_info max77620_regs_info[MAX77620_NUM_REGS] = {
	RAIL_SD(SD0, sd0, "in-sd0", SD0, 600000, 1400000, 12500, 0x22, SD0),
	RAIL_SD(SD1, sd1, "in-sd1", SD1, 600000, 1550000, 12500, 0x22, SD1),
	RAIL_SD(SD2, sd2, "in-sd2", SDX, 600000, 3787500, 12500, 0xFF, NONE),
	RAIL_SD(SD3, sd3, "in-sd3", SDX, 600000, 3787500, 12500, 0xFF, NONE),

	RAIL_LDO(LDO0, ldo0, "in-ldo0-1", N, 800000, 2375000, 25000),
	RAIL_LDO(LDO1, ldo1, "in-ldo0-1", N, 800000, 2375000, 25000),
	RAIL_LDO(LDO2, ldo2, "in-ldo2",   P, 800000, 3950000, 50000),
	RAIL_LDO(LDO3, ldo3, "in-ldo3-5", P, 800000, 3950000, 50000),
	RAIL_LDO(LDO4, ldo4, "in-ldo4-6", P, 800000, 1587500, 12500),
	RAIL_LDO(LDO5, ldo5, "in-ldo3-5", P, 800000, 3950000, 50000),
	RAIL_LDO(LDO6, ldo6, "in-ldo4-6", P, 800000, 3950000, 50000),
	RAIL_LDO(LDO7, ldo7, "in-ldo7-8", N, 800000, 3950000, 50000),
	RAIL_LDO(LDO8, ldo8, "in-ldo7-8", N, 800000, 3950000, 50000),
};

static struct max77620_regulator_info max20024_regs_info[MAX77620_NUM_REGS] = {
	RAIL_SD(SD0, sd0, "in-sd0", SD0, 800000, 1587500, 12500, 0x22, SD0),
	RAIL_SD(SD1, sd1, "in-sd1", SD1, 600000, 3387500, 12500, 0x22, SD1),
	RAIL_SD(SD2, sd2, "in-sd2", SDX, 600000, 3787500, 12500, 0xFF, NONE),
	RAIL_SD(SD3, sd3, "in-sd3", SDX, 600000, 3787500, 12500, 0xFF, NONE),
	RAIL_SD(SD4, sd4, "in-sd4", SDX, 600000, 3787500, 12500, 0xFF, NONE),

	RAIL_LDO(LDO0, ldo0, "in-ldo0-1", N, 800000, 2375000, 25000),
	RAIL_LDO(LDO1, ldo1, "in-ldo0-1", N, 800000, 2375000, 25000),
	RAIL_LDO(LDO2, ldo2, "in-ldo2",   P, 800000, 3950000, 50000),
	RAIL_LDO(LDO3, ldo3, "in-ldo3-5", P, 800000, 3950000, 50000),
	RAIL_LDO(LDO4, ldo4, "in-ldo4-6", P, 800000, 1587500, 12500),
	RAIL_LDO(LDO5, ldo5, "in-ldo3-5", P, 800000, 3950000, 50000),
	RAIL_LDO(LDO6, ldo6, "in-ldo4-6", P, 800000, 3950000, 50000),
	RAIL_LDO(LDO7, ldo7, "in-ldo7-8", N, 800000, 3950000, 50000),
	RAIL_LDO(LDO8, ldo8, "in-ldo7-8", N, 800000, 3950000, 50000),
};

static int max77620_regulator_probe(struct platform_device *pdev)
{
	struct max77620_chip *max77620_chip = dev_get_drvdata(pdev->dev.parent);
	struct max77620_regulator_info *rinfo;
	struct device *dev = &pdev->dev;
	struct regulator_config config = { };
	struct max77620_regulator *pmic;
	int ret = 0;
	int id;

	pmic = devm_kzalloc(dev, sizeof(*pmic), GFP_KERNEL);
	if (!pmic)
		return -ENOMEM;

	platform_set_drvdata(pdev, pmic);
	pmic->dev = dev;
	pmic->rmap = max77620_chip->rmap;
	if (!dev->of_node)
		dev->of_node = pdev->dev.parent->of_node;

	switch (max77620_chip->chip_id) {
	case MAX77620:
		rinfo = max77620_regs_info;
		break;
	default:
		rinfo = max20024_regs_info;
		break;
	}

	config.regmap = pmic->rmap;
	config.dev = dev;
	config.driver_data = pmic;

	for (id = 0; id < MAX77620_NUM_REGS; id++) {
		struct regulator_dev *rdev;
		struct regulator_desc *rdesc;

		if ((max77620_chip->chip_id == MAX77620) &&
		    (id == MAX77620_REGULATOR_ID_SD4))
			continue;

		rdesc = &rinfo[id].desc;
		pmic->rinfo[id] = &max77620_regs_info[id];
		pmic->enable_power_mode[id] = MAX77620_POWER_MODE_NORMAL;

		ret = max77620_read_slew_rate(pmic, id);
		if (ret < 0)
			return ret;

		rdev = devm_regulator_register(dev, rdesc, &config);
		if (IS_ERR(rdev)) {
			ret = PTR_ERR(rdev);
			dev_err(dev, "Regulator registration %s failed: %d\n",
				rdesc->name, ret);
			return ret;
		}
	}

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int max77620_regulator_suspend(struct device *dev)
{
	struct max77620_regulator *pmic = dev_get_drvdata(dev);
	struct max77620_regulator_pdata *reg_pdata;
	int id;

	for (id = 0; id < MAX77620_NUM_REGS; id++) {
		reg_pdata = &pmic->reg_pdata[id];

		max77620_regulator_set_fps_slots(pmic, id, true);
		if (reg_pdata->suspend_fps_src < 0)
			continue;

		max77620_regulator_set_fps_src(pmic, reg_pdata->suspend_fps_src,
					       id);
	}

	return 0;
}

static int max77620_regulator_resume(struct device *dev)
{
	struct max77620_regulator *pmic = dev_get_drvdata(dev);
	struct max77620_regulator_pdata *reg_pdata;
	int id;

	for (id = 0; id < MAX77620_NUM_REGS; id++) {
		reg_pdata = &pmic->reg_pdata[id];

		max77620_config_power_ok(pmic, id);

		max77620_regulator_set_fps_slots(pmic, id, false);
		if (reg_pdata->active_fps_src < 0)
			continue;
		max77620_regulator_set_fps_src(pmic, reg_pdata->active_fps_src,
					       id);
	}

	return 0;
}
#endif

static const struct dev_pm_ops max77620_regulator_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(max77620_regulator_suspend,
				max77620_regulator_resume)
};

static const struct platform_device_id max77620_regulator_devtype[] = {
	{ .name = "max77620-pmic", },
	{ .name = "max20024-pmic", },
	{},
};
MODULE_DEVICE_TABLE(platform, max77620_regulator_devtype);

static struct platform_driver max77620_regulator_driver = {
	.probe = max77620_regulator_probe,
	.id_table = max77620_regulator_devtype,
	.driver = {
		.name = "max77620-pmic",
		.pm = &max77620_regulator_pm_ops,
	},
};

module_platform_driver(max77620_regulator_driver);

MODULE_DESCRIPTION("MAX77620/MAX20024 regulator driver");
MODULE_AUTHOR("Mallikarjun Kasoju <mkasoju@nvidia.com>");
MODULE_AUTHOR("Laxman Dewangan <ldewangan@nvidia.com>");
MODULE_LICENSE("GPL v2");
