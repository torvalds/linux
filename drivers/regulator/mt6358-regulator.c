// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2019 MediaTek Inc.

#include <linux/mfd/mt6358/registers.h>
#include <linux/mfd/mt6397/core.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/mt6358-regulator.h>
#include <linux/regulator/of_regulator.h>

#include <dt-bindings/regulator/mediatek,mt6397-regulator.h>

/*
 * MT6358 regulators' information
 *
 * @desc: standard fields of regulator description.
 * @qi: Mask for query enable signal status of regulators
 */
struct mt6358_regulator_info {
	struct regulator_desc desc;
	u32 status_reg;
	u32 qi;
	u32 da_vsel_reg;
	u32 da_vsel_mask;
	u32 modeset_reg;
	u32 modeset_mask;
};

#define to_regulator_info(x) container_of((x), struct mt6358_regulator_info, desc)

#define MT6358_BUCK(match, vreg, supply, min, max, step,	\
		    vosel_mask, _da_vsel_reg, _da_vsel_mask,	\
		    _modeset_reg, _modeset_shift)		\
[MT6358_ID_##vreg] = {	\
	.desc = {	\
		.name = #vreg,	\
		.supply_name = supply,		\
		.of_match = of_match_ptr(match),	\
		.ops = &mt6358_buck_ops,	\
		.type = REGULATOR_VOLTAGE,	\
		.id = MT6358_ID_##vreg,		\
		.owner = THIS_MODULE,		\
		.n_voltages = ((max) - (min)) / (step) + 1,	\
		.min_uV = (min),		\
		.uV_step = (step),		\
		.vsel_reg = MT6358_BUCK_##vreg##_ELR0,	\
		.vsel_mask = vosel_mask,	\
		.enable_reg = MT6358_BUCK_##vreg##_CON0,	\
		.enable_mask = BIT(0),	\
		.of_map_mode = mt6358_map_mode,	\
	},	\
	.status_reg = MT6358_BUCK_##vreg##_DBG1,	\
	.qi = BIT(0),	\
	.da_vsel_reg = _da_vsel_reg,	\
	.da_vsel_mask = _da_vsel_mask,	\
	.modeset_reg = _modeset_reg,	\
	.modeset_mask = BIT(_modeset_shift),	\
}

#define MT6358_LDO(match, vreg, supply, volt_ranges, enreg, enbit, vosel, vosel_mask) \
[MT6358_ID_##vreg] = {	\
	.desc = {	\
		.name = #vreg,	\
		.supply_name = supply,		\
		.of_match = of_match_ptr(match),	\
		.ops = &mt6358_volt_table_ops,	\
		.type = REGULATOR_VOLTAGE,	\
		.id = MT6358_ID_##vreg,	\
		.owner = THIS_MODULE,	\
		.n_voltages = ARRAY_SIZE(volt_ranges##_ranges) * 11,	\
		.linear_ranges = volt_ranges##_ranges,		\
		.linear_range_selectors_bitfield = volt_ranges##_selectors,	\
		.n_linear_ranges = ARRAY_SIZE(volt_ranges##_ranges),	\
		.vsel_range_reg = vosel,	\
		.vsel_range_mask = vosel_mask,	\
		.vsel_reg = MT6358_##vreg##_ANA_CON0,	\
		.vsel_mask = GENMASK(3, 0),	\
		.enable_reg = enreg,	\
		.enable_mask = BIT(enbit),	\
	},	\
	.status_reg = MT6358_LDO_##vreg##_CON1,	\
	.qi = BIT(15),	\
}

#define MT6358_LDO1(match, vreg, supply, min, max, step,	\
		    _da_vsel_reg, _da_vsel_mask, vosel, vosel_mask)	\
[MT6358_ID_##vreg] = {	\
	.desc = {	\
		.name = #vreg,	\
		.supply_name = supply,		\
		.of_match = of_match_ptr(match),	\
		.ops = &mt6358_volt_range_ops,	\
		.type = REGULATOR_VOLTAGE,	\
		.id = MT6358_ID_##vreg,	\
		.owner = THIS_MODULE,	\
		.n_voltages = ((max) - (min)) / (step) + 1,	\
		.min_uV = (min),		\
		.uV_step = (step),		\
		.vsel_reg = vosel,	\
		.vsel_mask = vosel_mask,	\
		.enable_reg = MT6358_LDO_##vreg##_CON0,	\
		.enable_mask = BIT(0),	\
	},	\
	.da_vsel_reg = _da_vsel_reg,	\
	.da_vsel_mask = _da_vsel_mask,	\
	.status_reg = MT6358_LDO_##vreg##_DBG1,	\
	.qi = BIT(0),	\
}

#define MT6358_REG_FIXED(match, vreg, supply, enreg, enbit, volt)	\
[MT6358_ID_##vreg] = {	\
	.desc = {	\
		.name = #vreg,	\
		.supply_name = supply,		\
		.of_match = of_match_ptr(match),	\
		.ops = &mt6358_volt_fixed_ops,	\
		.type = REGULATOR_VOLTAGE,	\
		.id = MT6358_ID_##vreg,	\
		.owner = THIS_MODULE,	\
		.n_voltages = 11,	\
		.vsel_reg = MT6358_##vreg##_ANA_CON0,	\
		.vsel_mask = GENMASK(3, 0),	\
		.enable_reg = enreg,	\
		.enable_mask = BIT(enbit),	\
		.min_uV = volt,	\
		.uV_step = 10000, \
	},	\
	.status_reg = MT6358_LDO_##vreg##_CON1,	\
	.qi = BIT(15),							\
}

#define MT6366_BUCK(match, vreg, min, max, step,		\
	vosel_mask, _da_vsel_reg, _da_vsel_mask,	\
	_modeset_reg, _modeset_shift)		\
[MT6366_ID_##vreg] = {	\
	.desc = {	\
		.name = #vreg,	\
		.supply_name = "vsys-" match,		\
		.of_match = of_match_ptr(match),	\
		.ops = &mt6358_buck_ops,	\
		.type = REGULATOR_VOLTAGE,	\
		.id = MT6366_ID_##vreg,		\
		.owner = THIS_MODULE,		\
		.n_voltages = ((max) - (min)) / (step) + 1,	\
		.min_uV = (min),		\
		.uV_step = (step),		\
		.vsel_reg = MT6358_BUCK_##vreg##_ELR0,	\
		.vsel_mask = vosel_mask,	\
		.enable_reg = MT6358_BUCK_##vreg##_CON0,	\
		.enable_mask = BIT(0),	\
		.of_map_mode = mt6358_map_mode,	\
	},	\
	.status_reg = MT6358_BUCK_##vreg##_DBG1,	\
	.qi = BIT(0),	\
	.da_vsel_reg = _da_vsel_reg,	\
	.da_vsel_mask = _da_vsel_mask,	\
	.modeset_reg = _modeset_reg,	\
	.modeset_mask = BIT(_modeset_shift),	\
}

#define MT6366_LDO(match, vreg, volt_ranges, supply, enreg, enbit, vosel, vosel_mask) \
[MT6366_ID_##vreg] = {	\
	.desc = {	\
		.name = #vreg,	\
		.supply_name = supply,		\
		.of_match = of_match_ptr(match),	\
		.ops = &mt6358_volt_table_ops,	\
		.type = REGULATOR_VOLTAGE,	\
		.id = MT6366_ID_##vreg,	\
		.owner = THIS_MODULE,	\
		.n_voltages = ARRAY_SIZE(volt_ranges##_ranges) * 11,	\
		.linear_ranges = volt_ranges##_ranges,		\
		.linear_range_selectors_bitfield = volt_ranges##_selectors,	\
		.n_linear_ranges = ARRAY_SIZE(volt_ranges##_ranges),	\
		.vsel_range_reg = vosel,	\
		.vsel_range_mask = vosel_mask,	\
		.vsel_reg = MT6358_##vreg##_ANA_CON0,	\
		.vsel_mask = GENMASK(3, 0),	\
		.enable_reg = enreg,	\
		.enable_mask = BIT(enbit),	\
	},	\
	.status_reg = MT6358_LDO_##vreg##_CON1,	\
	.qi = BIT(15),	\
}

#define MT6366_LDO1(match, vreg, supply, min, max, step,	\
		    _da_vsel_reg, _da_vsel_mask, vosel, vosel_mask)	\
[MT6366_ID_##vreg] = {	\
	.desc = {	\
		.name = #vreg,	\
		.supply_name = supply,		\
		.of_match = of_match_ptr(match),	\
		.ops = &mt6358_volt_range_ops,	\
		.type = REGULATOR_VOLTAGE,	\
		.id = MT6366_ID_##vreg,	\
		.owner = THIS_MODULE,	\
		.n_voltages = ((max) - (min)) / (step) + 1,	\
		.min_uV = (min),		\
		.uV_step = (step),		\
		.vsel_reg = vosel,	\
		.vsel_mask = vosel_mask,	\
		.enable_reg = MT6358_LDO_##vreg##_CON0,	\
		.enable_mask = BIT(0),	\
	},	\
	.da_vsel_reg = _da_vsel_reg,	\
	.da_vsel_mask = _da_vsel_mask,	\
	.status_reg = MT6358_LDO_##vreg##_DBG1,	\
	.qi = BIT(0),	\
}

#define MT6366_REG_FIXED(match, vreg, supply, enreg, enbit, volt)	\
[MT6366_ID_##vreg] = {	\
	.desc = {	\
		.name = #vreg,	\
		.supply_name = supply,		\
		.of_match = of_match_ptr(match),	\
		.ops = &mt6358_volt_fixed_ops,	\
		.type = REGULATOR_VOLTAGE,	\
		.id = MT6366_ID_##vreg,	\
		.owner = THIS_MODULE,	\
		.n_voltages = 11,	\
		.vsel_reg = MT6358_##vreg##_ANA_CON0,	\
		.vsel_mask = GENMASK(3, 0),	\
		.enable_reg = enreg,	\
		.enable_mask = BIT(enbit),	\
		.min_uV = volt,	\
		.uV_step = 10000, \
	},	\
	.status_reg = MT6358_LDO_##vreg##_CON1,	\
	.qi = BIT(15),							\
}


/* VDRAM2 voltage selector not shown in datasheet */
static const unsigned int vdram2_selectors[] = { 0, 12 };
static const struct linear_range vdram2_ranges[] = {
	REGULATOR_LINEAR_RANGE(600000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(1800000, 0, 10, 10000),
};

static const unsigned int vsim_selectors[] = { 3, 4, 8, 11, 12 };
static const struct linear_range vsim_ranges[] = {
	REGULATOR_LINEAR_RANGE(1700000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(1800000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(2700000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(3000000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(3100000, 0, 10, 10000),
};

static const unsigned int vibr_selectors[] = { 0, 1, 2, 4, 5, 9, 11, 13 };
static const struct linear_range vibr_ranges[] = {
	REGULATOR_LINEAR_RANGE(1200000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(1300000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(1500000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(1800000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(2000000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(2800000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(3000000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(3300000, 0, 10, 10000),
};

/* VUSB voltage selector not shown in datasheet */
static const unsigned int vusb_selectors[] = { 3, 4 };
static const struct linear_range vusb_ranges[] = {
	REGULATOR_LINEAR_RANGE(3000000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(3100000, 0, 10, 10000),
};

static const unsigned int vcamd_selectors[] = { 3, 4, 5, 6, 7, 9, 12 };
static const struct linear_range vcamd_ranges[] = {
	REGULATOR_LINEAR_RANGE(900000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(1000000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(1100000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(1200000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(1300000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(1500000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(1800000, 0, 10, 10000),
};

static const unsigned int vefuse_selectors[] = { 11, 12, 13 };
static const struct linear_range vefuse_ranges[] = {
	REGULATOR_LINEAR_RANGE(1700000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(1800000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(1900000, 0, 10, 10000),
};

static const unsigned int vmch_vemc_selectors[] = { 2, 3, 5 };
static const struct linear_range vmch_vemc_ranges[] = {
	REGULATOR_LINEAR_RANGE(2900000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(3000000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(3300000, 0, 10, 10000),
};

static const unsigned int vcama_selectors[] = { 0, 7, 9, 10, 11, 12 };
static const struct linear_range vcama_ranges[] = {
	REGULATOR_LINEAR_RANGE(1800000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(2500000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(2700000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(2800000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(2900000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(3000000, 0, 10, 10000),
};

static const unsigned int vcn33_selectors[] = { 1, 2, 3 };
static const struct linear_range vcn33_ranges[] = {
	REGULATOR_LINEAR_RANGE(3300000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(3400000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(3500000, 0, 10, 10000),
};

static const unsigned int vmc_selectors[] = { 4, 10, 11, 13 };
static const struct linear_range vmc_ranges[] = {
	REGULATOR_LINEAR_RANGE(1800000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(2900000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(3000000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(3300000, 0, 10, 10000),
};

static const unsigned int vldo28_selectors[] = { 1, 3 };
static const struct linear_range vldo28_ranges[] = {
	REGULATOR_LINEAR_RANGE(2800000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(3000000, 0, 10, 10000),
};

static const unsigned int mt6366_vmddr_selectors[] = { 0, 1, 2, 3, 4, 5, 6, 7, 9, 12 };
static const struct linear_range mt6366_vmddr_ranges[] = {
	REGULATOR_LINEAR_RANGE(600000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(700000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(800000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(900000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(1000000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(1100000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(1200000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(1300000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(1500000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(1800000, 0, 10, 10000),
};

static const unsigned int mt6366_vcn18_vm18_selectors[] = {
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
static const struct linear_range mt6366_vcn18_vm18_ranges[] = {
	REGULATOR_LINEAR_RANGE(600000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(700000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(800000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(900000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(1000000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(1100000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(1200000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(1300000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(1400000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(1500000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(1600000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(1700000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(1800000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(1900000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(2000000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(2100000, 0, 10, 10000),
};

static unsigned int mt6358_map_mode(unsigned int mode)
{
	return mode == MT6397_BUCK_MODE_AUTO ?
		REGULATOR_MODE_NORMAL : REGULATOR_MODE_FAST;
}

static int mt6358_get_buck_voltage_sel(struct regulator_dev *rdev)
{
	const struct mt6358_regulator_info *info = to_regulator_info(rdev->desc);
	int ret, regval;

	ret = regmap_read(rdev->regmap, info->da_vsel_reg, &regval);
	if (ret != 0) {
		dev_err(&rdev->dev,
			"Failed to get mt6358 Buck %s vsel reg: %d\n",
			info->desc.name, ret);
		return ret;
	}

	ret = (regval & info->da_vsel_mask) >> (ffs(info->da_vsel_mask) - 1);

	return ret;
}

static int mt6358_get_status(struct regulator_dev *rdev)
{
	const struct mt6358_regulator_info *info = to_regulator_info(rdev->desc);
	int ret;
	u32 regval;

	ret = regmap_read(rdev->regmap, info->status_reg, &regval);
	if (ret != 0) {
		dev_info(&rdev->dev, "Failed to get enable reg: %d\n", ret);
		return ret;
	}

	return (regval & info->qi) ? REGULATOR_STATUS_ON : REGULATOR_STATUS_OFF;
}

static int mt6358_regulator_set_mode(struct regulator_dev *rdev,
				     unsigned int mode)
{
	const struct mt6358_regulator_info *info = to_regulator_info(rdev->desc);
	int val;

	switch (mode) {
	case REGULATOR_MODE_FAST:
		val = MT6397_BUCK_MODE_FORCE_PWM;
		break;
	case REGULATOR_MODE_NORMAL:
		val = MT6397_BUCK_MODE_AUTO;
		break;
	default:
		return -EINVAL;
	}

	dev_dbg(&rdev->dev, "mt6358 buck set_mode %#x, %#x, %#x\n",
		info->modeset_reg, info->modeset_mask, val);

	val <<= ffs(info->modeset_mask) - 1;

	return regmap_update_bits(rdev->regmap, info->modeset_reg,
				  info->modeset_mask, val);
}

static unsigned int mt6358_regulator_get_mode(struct regulator_dev *rdev)
{
	const struct mt6358_regulator_info *info = to_regulator_info(rdev->desc);
	int ret, regval;

	ret = regmap_read(rdev->regmap, info->modeset_reg, &regval);
	if (ret != 0) {
		dev_err(&rdev->dev,
			"Failed to get mt6358 buck mode: %d\n", ret);
		return ret;
	}

	switch ((regval & info->modeset_mask) >> (ffs(info->modeset_mask) - 1)) {
	case MT6397_BUCK_MODE_AUTO:
		return REGULATOR_MODE_NORMAL;
	case MT6397_BUCK_MODE_FORCE_PWM:
		return REGULATOR_MODE_FAST;
	default:
		return -EINVAL;
	}
}

static const struct regulator_ops mt6358_buck_ops = {
	.list_voltage = regulator_list_voltage_linear,
	.map_voltage = regulator_map_voltage_linear,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = mt6358_get_buck_voltage_sel,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.get_status = mt6358_get_status,
	.set_mode = mt6358_regulator_set_mode,
	.get_mode = mt6358_regulator_get_mode,
};

static const struct regulator_ops mt6358_volt_range_ops = {
	.list_voltage = regulator_list_voltage_linear,
	.map_voltage = regulator_map_voltage_linear,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = mt6358_get_buck_voltage_sel,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.get_status = mt6358_get_status,
};

static const struct regulator_ops mt6358_volt_table_ops = {
	.list_voltage = regulator_list_voltage_pickable_linear_range,
	.map_voltage = regulator_map_voltage_pickable_linear_range,
	.set_voltage_sel = regulator_set_voltage_sel_pickable_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_pickable_regmap,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.get_status = mt6358_get_status,
};

/* "Fixed" LDOs with output voltage calibration +0 ~ +10 mV */
static const struct regulator_ops mt6358_volt_fixed_ops = {
	.list_voltage = regulator_list_voltage_linear,
	.map_voltage = regulator_map_voltage_linear,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = mt6358_get_buck_voltage_sel,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.get_status = mt6358_get_status,
};

/* The array is indexed by id(MT6358_ID_XXX) */
static const struct mt6358_regulator_info mt6358_regulators[] = {
	MT6358_BUCK("buck_vdram1", VDRAM1, "vsys-vdram1", 500000, 2087500, 12500,
		    0x7f, MT6358_BUCK_VDRAM1_DBG0, 0x7f, MT6358_VDRAM1_ANA_CON0, 8),
	MT6358_BUCK("buck_vcore", VCORE, "vsys-vcore", 500000, 1293750, 6250,
		    0x7f, MT6358_BUCK_VCORE_DBG0, 0x7f, MT6358_VCORE_VGPU_ANA_CON0, 1),
	MT6358_BUCK("buck_vpa", VPA, "vsys-vpa", 500000, 3650000, 50000,
		    0x3f, MT6358_BUCK_VPA_DBG0, 0x3f, MT6358_VPA_ANA_CON0, 3),
	MT6358_BUCK("buck_vproc11", VPROC11, "vsys-vproc11", 500000, 1293750, 6250,
		    0x7f, MT6358_BUCK_VPROC11_DBG0, 0x7f, MT6358_VPROC_ANA_CON0, 1),
	MT6358_BUCK("buck_vproc12", VPROC12, "vsys-vproc12", 500000, 1293750, 6250,
		    0x7f, MT6358_BUCK_VPROC12_DBG0, 0x7f, MT6358_VPROC_ANA_CON0, 2),
	MT6358_BUCK("buck_vgpu", VGPU, "vsys-vgpu", 500000, 1293750, 6250,
		    0x7f, MT6358_BUCK_VGPU_ELR0, 0x7f, MT6358_VCORE_VGPU_ANA_CON0, 2),
	MT6358_BUCK("buck_vs2", VS2, "vsys-vs2", 500000, 2087500, 12500,
		    0x7f, MT6358_BUCK_VS2_DBG0, 0x7f, MT6358_VS2_ANA_CON0, 8),
	MT6358_BUCK("buck_vmodem", VMODEM, "vsys-vmodem", 500000, 1293750, 6250,
		    0x7f, MT6358_BUCK_VMODEM_DBG0, 0x7f, MT6358_VMODEM_ANA_CON0, 8),
	MT6358_BUCK("buck_vs1", VS1, "vsys-vs1", 1000000, 2587500, 12500,
		    0x7f, MT6358_BUCK_VS1_DBG0, 0x7f, MT6358_VS1_ANA_CON0, 8),
	MT6358_REG_FIXED("ldo_vrf12", VRF12, "vs2-ldo2", MT6358_LDO_VRF12_CON0, 0, 1200000),
	MT6358_REG_FIXED("ldo_vio18", VIO18, "vs1-ldo1", MT6358_LDO_VIO18_CON0, 0, 1800000),
	MT6358_REG_FIXED("ldo_vcamio", VCAMIO, "vs1-ldo1", MT6358_LDO_VCAMIO_CON0, 0, 1800000),
	MT6358_REG_FIXED("ldo_vcn18", VCN18, "vs1-ldo1", MT6358_LDO_VCN18_CON0, 0, 1800000),
	MT6358_REG_FIXED("ldo_vfe28", VFE28, "vsys-ldo1", MT6358_LDO_VFE28_CON0, 0, 2800000),
	MT6358_REG_FIXED("ldo_vcn28", VCN28, "vsys-ldo1", MT6358_LDO_VCN28_CON0, 0, 2800000),
	MT6358_REG_FIXED("ldo_vxo22", VXO22, "vsys-ldo1", MT6358_LDO_VXO22_CON0, 0, 2200000),
	MT6358_REG_FIXED("ldo_vaux18", VAUX18, "vsys-ldo1", MT6358_LDO_VAUX18_CON0, 0, 1800000),
	MT6358_REG_FIXED("ldo_vbif28", VBIF28, "vsys-ldo1", MT6358_LDO_VBIF28_CON0, 0, 2800000),
	MT6358_REG_FIXED("ldo_vio28", VIO28, "vsys-ldo2", MT6358_LDO_VIO28_CON0, 0, 2800000),
	MT6358_REG_FIXED("ldo_va12", VA12, "vs2-ldo2", MT6358_LDO_VA12_CON0, 0, 1200000),
	MT6358_REG_FIXED("ldo_vrf18", VRF18, "vs1-ldo1", MT6358_LDO_VRF18_CON0, 0, 1800000),
	MT6358_REG_FIXED("ldo_vaud28", VAUD28, "vsys-ldo1", MT6358_LDO_VAUD28_CON0, 0, 2800000),
	MT6358_LDO("ldo_vdram2", VDRAM2, "vs2-ldo1", vdram2,
		   MT6358_LDO_VDRAM2_CON0, 0, MT6358_LDO_VDRAM2_ELR0, 0xf),
	MT6358_LDO("ldo_vsim1", VSIM1, "vsys-ldo1", vsim,
		   MT6358_LDO_VSIM1_CON0, 0, MT6358_VSIM1_ANA_CON0, 0xf00),
	MT6358_LDO("ldo_vibr", VIBR, "vsys-ldo3", vibr,
		   MT6358_LDO_VIBR_CON0, 0, MT6358_VIBR_ANA_CON0, 0xf00),
	MT6358_LDO("ldo_vusb", VUSB, "vsys-ldo1", vusb,
		   MT6358_LDO_VUSB_CON0_0, 0, MT6358_VUSB_ANA_CON0, 0x700),
	MT6358_LDO("ldo_vcamd", VCAMD, "vs2-ldo4", vcamd,
		   MT6358_LDO_VCAMD_CON0, 0, MT6358_VCAMD_ANA_CON0, 0xf00),
	MT6358_LDO("ldo_vefuse", VEFUSE, "vs1-ldo1", vefuse,
		   MT6358_LDO_VEFUSE_CON0, 0, MT6358_VEFUSE_ANA_CON0, 0xf00),
	MT6358_LDO("ldo_vmch", VMCH, "vsys-ldo2", vmch_vemc,
		   MT6358_LDO_VMCH_CON0, 0, MT6358_VMCH_ANA_CON0, 0x700),
	MT6358_LDO("ldo_vcama1", VCAMA1, "vsys-ldo3", vcama,
		   MT6358_LDO_VCAMA1_CON0, 0, MT6358_VCAMA1_ANA_CON0, 0xf00),
	MT6358_LDO("ldo_vemc", VEMC, "vsys-ldo2", vmch_vemc,
		   MT6358_LDO_VEMC_CON0, 0, MT6358_VEMC_ANA_CON0, 0x700),
	MT6358_LDO("ldo_vcn33", VCN33, "vsys-ldo3", vcn33,
		   MT6358_LDO_VCN33_CON0_0, 0, MT6358_VCN33_ANA_CON0, 0x300),
	MT6358_LDO("ldo_vcama2", VCAMA2, "vsys-ldo3", vcama,
		   MT6358_LDO_VCAMA2_CON0, 0, MT6358_VCAMA2_ANA_CON0, 0xf00),
	MT6358_LDO("ldo_vmc", VMC, "vsys-ldo2", vmc,
		   MT6358_LDO_VMC_CON0, 0, MT6358_VMC_ANA_CON0, 0xf00),
	MT6358_LDO("ldo_vldo28", VLDO28, "vsys-ldo2", vldo28,
		   MT6358_LDO_VLDO28_CON0_0, 0,
		   MT6358_VLDO28_ANA_CON0, 0x300),
	MT6358_LDO("ldo_vsim2", VSIM2, "vsys-ldo2", vsim,
		   MT6358_LDO_VSIM2_CON0, 0, MT6358_VSIM2_ANA_CON0, 0xf00),
	MT6358_LDO1("ldo_vsram_proc11", VSRAM_PROC11, "vs2-ldo3", 500000, 1293750, 6250,
		    MT6358_LDO_VSRAM_PROC11_DBG0, 0x7f00, MT6358_LDO_VSRAM_CON0, 0x7f),
	MT6358_LDO1("ldo_vsram_others", VSRAM_OTHERS, "vs2-ldo3", 500000, 1293750, 6250,
		    MT6358_LDO_VSRAM_OTHERS_DBG0, 0x7f00, MT6358_LDO_VSRAM_CON2, 0x7f),
	MT6358_LDO1("ldo_vsram_gpu", VSRAM_GPU, "vs2-ldo3", 500000, 1293750, 6250,
		    MT6358_LDO_VSRAM_GPU_DBG0, 0x7f00, MT6358_LDO_VSRAM_CON3, 0x7f),
	MT6358_LDO1("ldo_vsram_proc12", VSRAM_PROC12, "vs2-ldo3", 500000, 1293750, 6250,
		    MT6358_LDO_VSRAM_PROC12_DBG0, 0x7f00, MT6358_LDO_VSRAM_CON1, 0x7f),
};

/* The array is indexed by id(MT6366_ID_XXX) */
static const struct mt6358_regulator_info mt6366_regulators[] = {
	MT6366_BUCK("vdram1", VDRAM1, 500000, 2087500, 12500,
		    0x7f, MT6358_BUCK_VDRAM1_DBG0, 0x7f, MT6358_VDRAM1_ANA_CON0, 8),
	MT6366_BUCK("vcore", VCORE, 500000, 1293750, 6250,
		    0x7f, MT6358_BUCK_VCORE_DBG0, 0x7f, MT6358_VCORE_VGPU_ANA_CON0, 1),
	MT6366_BUCK("vpa", VPA, 500000, 3650000, 50000,
		    0x3f, MT6358_BUCK_VPA_DBG0, 0x3f, MT6358_VPA_ANA_CON0, 3),
	MT6366_BUCK("vproc11", VPROC11, 500000, 1293750, 6250,
		    0x7f, MT6358_BUCK_VPROC11_DBG0, 0x7f, MT6358_VPROC_ANA_CON0, 1),
	MT6366_BUCK("vproc12", VPROC12, 500000, 1293750, 6250,
		    0x7f, MT6358_BUCK_VPROC12_DBG0, 0x7f, MT6358_VPROC_ANA_CON0, 2),
	MT6366_BUCK("vgpu", VGPU, 500000, 1293750, 6250,
		    0x7f, MT6358_BUCK_VGPU_ELR0, 0x7f, MT6358_VCORE_VGPU_ANA_CON0, 2),
	MT6366_BUCK("vs2", VS2, 500000, 2087500, 12500,
		    0x7f, MT6358_BUCK_VS2_DBG0, 0x7f, MT6358_VS2_ANA_CON0, 8),
	MT6366_BUCK("vmodem", VMODEM, 500000, 1293750, 6250,
		    0x7f, MT6358_BUCK_VMODEM_DBG0, 0x7f, MT6358_VMODEM_ANA_CON0, 8),
	MT6366_BUCK("vs1", VS1, 1000000, 2587500, 12500,
		    0x7f, MT6358_BUCK_VS1_DBG0, 0x7f, MT6358_VS1_ANA_CON0, 8),
	MT6366_REG_FIXED("vrf12", VRF12, "vs2-ldo2", MT6358_LDO_VRF12_CON0, 0, 1200000),
	MT6366_REG_FIXED("vio18", VIO18, "vs1-ldo1", MT6358_LDO_VIO18_CON0, 0, 1800000),
	MT6366_REG_FIXED("vfe28", VFE28, "vsys-ldo1", MT6358_LDO_VFE28_CON0, 0, 2800000),
	MT6366_REG_FIXED("vcn28", VCN28, "vsys-ldo1", MT6358_LDO_VCN28_CON0, 0, 2800000),
	MT6366_REG_FIXED("vxo22", VXO22, "vsys-ldo1", MT6358_LDO_VXO22_CON0, 0, 2200000),
	MT6366_REG_FIXED("vaux18", VAUX18, "vsys-ldo1", MT6358_LDO_VAUX18_CON0, 0, 1800000),
	MT6366_REG_FIXED("vbif28", VBIF28, "vsys-ldo1", MT6358_LDO_VBIF28_CON0, 0, 2800000),
	MT6366_REG_FIXED("vio28", VIO28, "vsys-ldo2", MT6358_LDO_VIO28_CON0, 0, 2800000),
	MT6366_REG_FIXED("va12", VA12, "vs2-ldo2", MT6358_LDO_VA12_CON0, 0, 1200000),
	MT6366_REG_FIXED("vrf18", VRF18, "vs1-ldo1", MT6358_LDO_VRF18_CON0, 0, 1800000),
	MT6366_REG_FIXED("vaud28", VAUD28, "vsys-ldo1", MT6358_LDO_VAUD28_CON0, 0, 2800000),
	MT6366_LDO("vdram2", VDRAM2, vdram2, "vs2-ldo1",
		   MT6358_LDO_VDRAM2_CON0, 0, MT6358_LDO_VDRAM2_ELR0, 0x10),
	MT6366_LDO("vsim1", VSIM1, vsim, "vsys-ldo1",
		   MT6358_LDO_VSIM1_CON0, 0, MT6358_VSIM1_ANA_CON0, 0xf00),
	MT6366_LDO("vibr", VIBR, vibr, "vsys-ldo3",
		   MT6358_LDO_VIBR_CON0, 0, MT6358_VIBR_ANA_CON0, 0xf00),
	MT6366_LDO("vusb", VUSB, vusb, "vsys-ldo1",
		   MT6358_LDO_VUSB_CON0_0, 0, MT6358_VUSB_ANA_CON0, 0x700),
	MT6366_LDO("vefuse", VEFUSE, vefuse, "vs1-ldo1",
		   MT6358_LDO_VEFUSE_CON0, 0, MT6358_VEFUSE_ANA_CON0, 0xf00),
	MT6366_LDO("vmch", VMCH, vmch_vemc, "vsys-ldo2",
		   MT6358_LDO_VMCH_CON0, 0, MT6358_VMCH_ANA_CON0, 0x700),
	MT6366_LDO("vemc", VEMC, vmch_vemc, "vsys-ldo3",
		   MT6358_LDO_VEMC_CON0, 0, MT6358_VEMC_ANA_CON0, 0x700),
	MT6366_LDO("vcn33", VCN33, vcn33, "vsys-ldo3",
		   MT6358_LDO_VCN33_CON0_0, 0, MT6358_VCN33_ANA_CON0, 0x300),
	MT6366_LDO("vmc", VMC, vmc, "vsys-ldo2",
		   MT6358_LDO_VMC_CON0, 0, MT6358_VMC_ANA_CON0, 0xf00),
	MT6366_LDO("vsim2", VSIM2, vsim, "vsys-ldo2",
		   MT6358_LDO_VSIM2_CON0, 0, MT6358_VSIM2_ANA_CON0, 0xf00),
	MT6366_LDO("vcn18", VCN18, mt6366_vcn18_vm18, "vs1-ldo1",
		   MT6358_LDO_VCN18_CON0, 0, MT6358_VCN18_ANA_CON0, 0xf00),
	MT6366_LDO("vm18", VM18, mt6366_vcn18_vm18, "vs1-ldo1",
		   MT6358_LDO_VM18_CON0, 0, MT6358_VM18_ANA_CON0, 0xf00),
	MT6366_LDO("vmddr", VMDDR, mt6366_vmddr, "vs2-ldo1",
		   MT6358_LDO_VMDDR_CON0, 0, MT6358_VMDDR_ANA_CON0, 0xf00),
	MT6366_LDO1("vsram-proc11", VSRAM_PROC11, "vs2-ldo3", 500000, 1293750, 6250,
		    MT6358_LDO_VSRAM_PROC11_DBG0, 0x7f00, MT6358_LDO_VSRAM_CON0, 0x7f),
	MT6366_LDO1("vsram-others", VSRAM_OTHERS, "vs2-ldo3", 500000, 1293750, 6250,
		    MT6358_LDO_VSRAM_OTHERS_DBG0, 0x7f00, MT6358_LDO_VSRAM_CON2, 0x7f),
	MT6366_LDO1("vsram-gpu", VSRAM_GPU, "vs2-ldo3", 500000, 1293750, 6250,
		    MT6358_LDO_VSRAM_GPU_DBG0, 0x7f00, MT6358_LDO_VSRAM_CON3, 0x7f),
	MT6366_LDO1("vsram-proc12", VSRAM_PROC12, "vs2-ldo3", 500000, 1293750, 6250,
		    MT6358_LDO_VSRAM_PROC12_DBG0, 0x7f00, MT6358_LDO_VSRAM_CON1, 0x7f),
	MT6366_LDO1("vsram-core", VSRAM_CORE, "vs2-ldo3", 500000, 1293750, 6250,
		    MT6358_LDO_VSRAM_CORE_DBG0, 0x7f00, MT6358_LDO_VSRAM_CON5, 0x7f),
};

static int mt6358_sync_vcn33_setting(struct device *dev)
{
	struct mt6397_chip *mt6397 = dev_get_drvdata(dev->parent);
	unsigned int val;
	int ret;

	/*
	 * VCN33_WIFI and VCN33_BT are two separate enable bits for the same
	 * regulator. They share the same voltage setting and output pin.
	 * Instead of having two potentially conflicting regulators, just have
	 * one VCN33 regulator. Sync the two enable bits and only use one in
	 * the regulator device.
	 */
	ret = regmap_read(mt6397->regmap, MT6358_LDO_VCN33_CON0_1, &val);
	if (ret) {
		dev_err(dev, "Failed to read VCN33_WIFI setting\n");
		return ret;
	}

	if (!(val & BIT(0)))
		return 0;

	/* Sync VCN33_WIFI enable status to VCN33_BT */
	ret = regmap_update_bits(mt6397->regmap, MT6358_LDO_VCN33_CON0_0, BIT(0), BIT(0));
	if (ret) {
		dev_err(dev, "Failed to sync VCN33_WIFI setting to VCN33_BT\n");
		return ret;
	}

	/* Disable VCN33_WIFI */
	ret = regmap_update_bits(mt6397->regmap, MT6358_LDO_VCN33_CON0_1, BIT(0), 0);
	if (ret) {
		dev_err(dev, "Failed to disable VCN33_WIFI\n");
		return ret;
	}

	return 0;
}

static int mt6358_regulator_probe(struct platform_device *pdev)
{
	struct mt6397_chip *mt6397 = dev_get_drvdata(pdev->dev.parent);
	struct regulator_config config = {};
	struct regulator_dev *rdev;
	const struct mt6358_regulator_info *mt6358_info;
	int i, max_regulator, ret;

	switch (mt6397->chip_id) {
	case MT6358_CHIP_ID:
		max_regulator = MT6358_MAX_REGULATOR;
		mt6358_info = mt6358_regulators;
		break;
	case MT6366_CHIP_ID:
		max_regulator = MT6366_MAX_REGULATOR;
		mt6358_info = mt6366_regulators;
		break;
	default:
		dev_err(&pdev->dev, "unsupported chip ID: %d\n", mt6397->chip_id);
		return -EINVAL;
	}

	ret = mt6358_sync_vcn33_setting(&pdev->dev);
	if (ret)
		return ret;

	for (i = 0; i < max_regulator; i++) {
		config.dev = &pdev->dev;
		config.regmap = mt6397->regmap;

		rdev = devm_regulator_register(&pdev->dev,
					       &mt6358_info[i].desc,
					       &config);
		if (IS_ERR(rdev)) {
			dev_err(&pdev->dev, "failed to register %s\n",
				mt6358_info[i].desc.name);
			return PTR_ERR(rdev);
		}
	}

	return 0;
}

static const struct platform_device_id mt6358_platform_ids[] = {
	{"mt6358-regulator", 0},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(platform, mt6358_platform_ids);

static struct platform_driver mt6358_regulator_driver = {
	.driver = {
		.name = "mt6358-regulator",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.probe = mt6358_regulator_probe,
	.id_table = mt6358_platform_ids,
};

module_platform_driver(mt6358_regulator_driver);

MODULE_AUTHOR("Hsin-Hsiung Wang <hsin-hsiung.wang@mediatek.com>");
MODULE_DESCRIPTION("Regulator Driver for MediaTek MT6358 PMIC");
MODULE_LICENSE("GPL");
