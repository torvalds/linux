// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2021 MediaTek Inc.

#include <linux/platform_device.h>
#include <linux/mfd/mt6359/registers.h>
#include <linux/mfd/mt6359p/registers.h>
#include <linux/mfd/mt6397/core.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/mt6359-regulator.h>
#include <linux/regulator/of_regulator.h>

#define MT6359_BUCK_MODE_AUTO		0
#define MT6359_BUCK_MODE_FORCE_PWM	1
#define MT6359_BUCK_MODE_NORMAL		0
#define MT6359_BUCK_MODE_LP		2

/*
 * MT6359 regulators' information
 *
 * @desc: standard fields of regulator description.
 * @status_reg: for query status of regulators.
 * @qi: Mask for query enable signal status of regulators.
 * @modeset_reg: for operating AUTO/PWM mode register.
 * @modeset_mask: MASK for operating modeset register.
 */
struct mt6359_regulator_info {
	struct regulator_desc desc;
	u32 status_reg;
	u32 qi;
	u32 modeset_reg;
	u32 modeset_mask;
	u32 lp_mode_reg;
	u32 lp_mode_mask;
};

#define MT6359_BUCK(match, _name, supply, min, max, step,	\
	_enable_reg, _status_reg,				\
	_vsel_reg, _vsel_mask,					\
	_lp_mode_reg, _lp_mode_shift,				\
	_modeset_reg, _modeset_shift)				\
[MT6359_ID_##_name] = {						\
	.desc = {						\
		.name = #_name,					\
		.supply_name = supply,				\
		.of_match = of_match_ptr(match),		\
		.regulators_node = of_match_ptr("regulators"),	\
		.ops = &mt6359_volt_linear_ops,			\
		.type = REGULATOR_VOLTAGE,			\
		.id = MT6359_ID_##_name,			\
		.owner = THIS_MODULE,				\
		.uV_step = (step),				\
		.n_voltages = ((max) - (min)) / (step) + 1,	\
		.min_uV = (min),				\
		.vsel_reg = _vsel_reg,				\
		.vsel_mask = _vsel_mask,			\
		.enable_reg = _enable_reg,			\
		.enable_mask = BIT(0),				\
		.of_map_mode = mt6359_map_mode,			\
	},							\
	.status_reg = _status_reg,				\
	.qi = BIT(0),						\
	.lp_mode_reg = _lp_mode_reg,				\
	.lp_mode_mask = BIT(_lp_mode_shift),			\
	.modeset_reg = _modeset_reg,				\
	.modeset_mask = BIT(_modeset_shift),			\
}

#define MT6359_LDO_LINEAR(match, _name, supply, min, max, step,	\
	_enable_reg, _status_reg, _vsel_reg, _vsel_mask)	\
[MT6359_ID_##_name] = {						\
	.desc = {						\
		.name = #_name,					\
		.supply_name = supply,				\
		.of_match = of_match_ptr(match),		\
		.regulators_node = of_match_ptr("regulators"),	\
		.ops = &mt6359_volt_linear_ops,			\
		.type = REGULATOR_VOLTAGE,			\
		.id = MT6359_ID_##_name,			\
		.owner = THIS_MODULE,				\
		.uV_step = (step),				\
		.n_voltages = ((max) - (min)) / (step) + 1,	\
		.min_uV = (min),				\
		.vsel_reg = _vsel_reg,				\
		.vsel_mask = _vsel_mask,			\
		.enable_reg = _enable_reg,			\
		.enable_mask = BIT(0),				\
	},							\
	.status_reg = _status_reg,				\
	.qi = BIT(0),						\
}

#define MT6359_LDO(match, _name, supply, _volt_table,		\
	_enable_reg, _enable_mask, _status_reg,			\
	_vsel_reg, _vsel_mask, _en_delay)			\
[MT6359_ID_##_name] = {						\
	.desc = {						\
		.name = #_name,					\
		.supply_name = supply,				\
		.of_match = of_match_ptr(match),		\
		.regulators_node = of_match_ptr("regulators"),	\
		.ops = &mt6359_volt_table_ops,			\
		.type = REGULATOR_VOLTAGE,			\
		.id = MT6359_ID_##_name,			\
		.owner = THIS_MODULE,				\
		.n_voltages = ARRAY_SIZE(_volt_table),		\
		.volt_table = _volt_table,			\
		.vsel_reg = _vsel_reg,				\
		.vsel_mask = _vsel_mask,			\
		.enable_reg = _enable_reg,			\
		.enable_mask = BIT(_enable_mask),		\
		.enable_time = _en_delay,			\
	},							\
	.status_reg = _status_reg,				\
	.qi = BIT(0),						\
}

#define MT6359_REG_FIXED(match, _name, supply,		\
			 _enable_reg, _status_reg,	\
			 _fixed_volt)			\
[MT6359_ID_##_name] = {					\
	.desc = {					\
		.name = #_name,				\
		.supply_name = supply,			\
		.of_match = of_match_ptr(match),	\
		.regulators_node = of_match_ptr("regulators"),	\
		.ops = &mt6359_volt_fixed_ops,		\
		.type = REGULATOR_VOLTAGE,		\
		.id = MT6359_ID_##_name,		\
		.owner = THIS_MODULE,			\
		.n_voltages = 1,			\
		.enable_reg = _enable_reg,		\
		.enable_mask = BIT(0),			\
		.fixed_uV = (_fixed_volt),		\
	},						\
	.status_reg = _status_reg,			\
	.qi = BIT(0),					\
}

#define MT6359P_LDO1(match, _name, supply, _ops,	\
		     _volt_table, _enable_reg,		\
		     _enable_mask, _status_reg,		\
		     _vsel_reg, _vsel_mask)		\
[MT6359_ID_##_name] = {					\
	.desc = {					\
		.name = #_name,				\
		.supply_name = supply,			\
		.of_match = of_match_ptr(match),	\
		.regulators_node = of_match_ptr("regulators"),	\
		.ops = &_ops,				\
		.type = REGULATOR_VOLTAGE,		\
		.id = MT6359_ID_##_name,		\
		.owner = THIS_MODULE,			\
		.n_voltages = ARRAY_SIZE(_volt_table),	\
		.volt_table = _volt_table,		\
		.vsel_reg = _vsel_reg,			\
		.vsel_mask = _vsel_mask,		\
		.enable_reg = _enable_reg,		\
		.enable_mask = BIT(_enable_mask),	\
	},						\
	.status_reg = _status_reg,			\
	.qi = BIT(0),					\
}

#define MT6359_LDO_NOOP(match, _name, supply)		\
[MT6359_ID_##_name] = {					\
	.desc = {					\
		.name = #_name,				\
		.supply_name = supply,			\
		.of_match = of_match_ptr(match),	\
		.regulators_node = of_match_ptr("regulators"),	\
		.ops = &mt6359_noop_ops,		\
		.type = REGULATOR_VOLTAGE,		\
		.id = MT6359_ID_##_name,		\
		.owner = THIS_MODULE,			\
	},						\
}

static const unsigned int vsim1_voltages[] = {
	0, 0, 0, 1700000, 1800000, 0, 0, 0, 2700000, 0, 0, 3000000, 3100000,
};

static const unsigned int vibr_voltages[] = {
	1200000, 1300000, 1500000, 0, 1800000, 2000000, 0, 0, 2700000, 2800000,
	0, 3000000, 0, 3300000,
};

static const unsigned int vrf12_voltages[] = {
	0, 0, 1100000, 1200000,	1300000,
};

static const unsigned int volt18_voltages[] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1700000, 1800000, 1900000,
};

static const unsigned int vcn13_voltages[] = {
	900000, 1000000, 0, 1200000, 1300000,
};

static const unsigned int vcn33_voltages[] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 2800000, 0, 0, 0, 3300000, 3400000, 3500000,
};

static const unsigned int vefuse_voltages[] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1700000, 1800000, 1900000, 2000000,
};

static const unsigned int vxo22_voltages[] = {
	1800000, 0, 0, 0, 2200000,
};

static const unsigned int vrfck_voltages[] = {
	0, 0, 1500000, 0, 0, 0, 0, 1600000, 0, 0, 0, 0, 1700000,
};

static const unsigned int vrfck_voltages_1[] = {
	1240000, 1600000,
};

static const unsigned int vio28_voltages[] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 2800000, 2900000, 3000000, 3100000, 3300000,
};

static const unsigned int vemc_voltages[] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2900000, 3000000, 0, 3300000,
};

static const unsigned int vemc_voltages_1[] = {
	0, 0, 0, 0, 0, 0, 0, 0, 2500000, 2800000, 2900000, 3000000, 3100000,
	3300000,
};

static const unsigned int va12_voltages[] = {
	0, 0, 0, 0, 0, 0, 1200000, 1300000,
};

static const unsigned int va09_voltages[] = {
	0, 0, 800000, 900000, 0, 0, 1200000,
};

static const unsigned int vrf18_voltages[] = {
	0, 0, 0, 0, 0, 1700000, 1800000, 1810000,
};

static const unsigned int vbbck_voltages[] = {
	0, 0, 0, 0, 1100000, 0, 0, 0, 1150000, 0, 0, 0, 1200000,
};

static const unsigned int vsim2_voltages[] = {
	0, 0, 0, 1700000, 1800000, 0, 0, 0, 2700000, 0, 0, 3000000, 3100000,
};

static inline unsigned int mt6359_map_mode(unsigned int mode)
{
	switch (mode) {
	case MT6359_BUCK_MODE_NORMAL:
		return REGULATOR_MODE_NORMAL;
	case MT6359_BUCK_MODE_FORCE_PWM:
		return REGULATOR_MODE_FAST;
	case MT6359_BUCK_MODE_LP:
		return REGULATOR_MODE_IDLE;
	default:
		return REGULATOR_MODE_INVALID;
	}
}

static int mt6359_get_status(struct regulator_dev *rdev)
{
	int ret;
	u32 regval;
	const struct mt6359_regulator_info *info = rdev_get_drvdata(rdev);

	ret = regmap_read(rdev->regmap, info->status_reg, &regval);
	if (ret != 0) {
		dev_err(&rdev->dev, "Failed to get enable reg: %d\n", ret);
		return ret;
	}

	if (regval & info->qi)
		return REGULATOR_STATUS_ON;
	else
		return REGULATOR_STATUS_OFF;
}

static unsigned int mt6359_regulator_get_mode(struct regulator_dev *rdev)
{
	const struct mt6359_regulator_info *info = rdev_get_drvdata(rdev);
	int ret, regval;

	ret = regmap_read(rdev->regmap, info->modeset_reg, &regval);
	if (ret != 0) {
		dev_err(&rdev->dev,
			"Failed to get mt6359 buck mode: %d\n", ret);
		return ret;
	}

	regval &= info->modeset_mask;
	regval >>= ffs(info->modeset_mask) - 1;

	if (regval == MT6359_BUCK_MODE_FORCE_PWM)
		return REGULATOR_MODE_FAST;

	ret = regmap_read(rdev->regmap, info->lp_mode_reg, &regval);
	if (ret != 0) {
		dev_err(&rdev->dev,
			"Failed to get mt6359 buck lp mode: %d\n", ret);
		return ret;
	}

	if (regval & info->lp_mode_mask)
		return REGULATOR_MODE_IDLE;
	else
		return REGULATOR_MODE_NORMAL;
}

static int mt6359_regulator_set_mode(struct regulator_dev *rdev,
				     unsigned int mode)
{
	const struct mt6359_regulator_info *info = rdev_get_drvdata(rdev);
	int ret = 0, val;
	int curr_mode;

	curr_mode = mt6359_regulator_get_mode(rdev);
	switch (mode) {
	case REGULATOR_MODE_FAST:
		val = MT6359_BUCK_MODE_FORCE_PWM;
		val <<= ffs(info->modeset_mask) - 1;
		ret = regmap_update_bits(rdev->regmap,
					 info->modeset_reg,
					 info->modeset_mask,
					 val);
		break;
	case REGULATOR_MODE_NORMAL:
		if (curr_mode == REGULATOR_MODE_FAST) {
			val = MT6359_BUCK_MODE_AUTO;
			val <<= ffs(info->modeset_mask) - 1;
			ret = regmap_update_bits(rdev->regmap,
						 info->modeset_reg,
						 info->modeset_mask,
						 val);
		} else if (curr_mode == REGULATOR_MODE_IDLE) {
			val = MT6359_BUCK_MODE_NORMAL;
			val <<= ffs(info->lp_mode_mask) - 1;
			ret = regmap_update_bits(rdev->regmap,
						 info->lp_mode_reg,
						 info->lp_mode_mask,
						 val);
			udelay(100);
		}
		break;
	case REGULATOR_MODE_IDLE:
		val = MT6359_BUCK_MODE_LP >> 1;
		val <<= ffs(info->lp_mode_mask) - 1;
		ret = regmap_update_bits(rdev->regmap,
					 info->lp_mode_reg,
					 info->lp_mode_mask,
					 val);
		break;
	default:
		return -EINVAL;
	}

	if (ret != 0) {
		dev_err(&rdev->dev,
			"Failed to set mt6359 buck mode: %d\n", ret);
	}

	return ret;
}

static int mt6359p_vemc_set_voltage_sel(struct regulator_dev *rdev,
					u32 sel)
{
	const struct mt6359_regulator_info *info = rdev_get_drvdata(rdev);
	int ret;
	u32 val = 0;

	sel <<= ffs(info->desc.vsel_mask) - 1;
	ret = regmap_write(rdev->regmap, MT6359P_TMA_KEY_ADDR, TMA_KEY);
	if (ret)
		return ret;

	ret = regmap_read(rdev->regmap, MT6359P_VM_MODE_ADDR, &val);
	if (ret)
		return ret;

	switch (val) {
	case 0:
		/* If HW trapping is 0, use VEMC_VOSEL_0 */
		ret = regmap_update_bits(rdev->regmap,
					 info->desc.vsel_reg,
					 info->desc.vsel_mask, sel);
		break;
	case 1:
		/* If HW trapping is 1, use VEMC_VOSEL_1 */
		ret = regmap_update_bits(rdev->regmap,
					 info->desc.vsel_reg + 0x2,
					 info->desc.vsel_mask, sel);
		break;
	default:
		return -EINVAL;
	}

	if (ret)
		return ret;

	ret = regmap_write(rdev->regmap, MT6359P_TMA_KEY_ADDR, 0);
	return ret;
}

static int mt6359p_vemc_get_voltage_sel(struct regulator_dev *rdev)
{
	const struct mt6359_regulator_info *info = rdev_get_drvdata(rdev);
	int ret;
	u32 val = 0;

	ret = regmap_read(rdev->regmap, MT6359P_VM_MODE_ADDR, &val);
	if (ret)
		return ret;
	switch (val) {
	case 0:
		/* If HW trapping is 0, use VEMC_VOSEL_0 */
		ret = regmap_read(rdev->regmap,
				  info->desc.vsel_reg, &val);
		break;
	case 1:
		/* If HW trapping is 1, use VEMC_VOSEL_1 */
		ret = regmap_read(rdev->regmap,
				  info->desc.vsel_reg + 0x2, &val);
		break;
	default:
		return -EINVAL;
	}
	if (ret)
		return ret;

	val &= info->desc.vsel_mask;
	val >>= ffs(info->desc.vsel_mask) - 1;

	return val;
}

static const struct regulator_ops mt6359_volt_linear_ops = {
	.list_voltage = regulator_list_voltage_linear,
	.map_voltage = regulator_map_voltage_linear,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.get_status = mt6359_get_status,
	.set_mode = mt6359_regulator_set_mode,
	.get_mode = mt6359_regulator_get_mode,
};

static const struct regulator_ops mt6359_volt_table_ops = {
	.list_voltage = regulator_list_voltage_table,
	.map_voltage = regulator_map_voltage_iterate,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.get_status = mt6359_get_status,
};

static const struct regulator_ops mt6359_volt_fixed_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.get_status = mt6359_get_status,
};

static const struct regulator_ops mt6359p_vemc_ops = {
	.list_voltage = regulator_list_voltage_table,
	.map_voltage = regulator_map_voltage_iterate,
	.set_voltage_sel = mt6359p_vemc_set_voltage_sel,
	.get_voltage_sel = mt6359p_vemc_get_voltage_sel,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.get_status = mt6359_get_status,
};

/* Used for backward-compatible placeholder regulators */
static const struct regulator_ops mt6359_noop_ops = {};

/* The array is indexed by id(MT6359_ID_XXX) */
static const struct mt6359_regulator_info mt6359_regulators[] = {
	MT6359_BUCK("buck_vs1", VS1, "vsys-vs1", 800000, 2200000, 12500,
		    MT6359_RG_BUCK_VS1_EN_ADDR,
		    MT6359_DA_VS1_EN_ADDR, MT6359_RG_BUCK_VS1_VOSEL_ADDR,
		    MT6359_RG_BUCK_VS1_VOSEL_MASK <<
		    MT6359_RG_BUCK_VS1_VOSEL_SHIFT,
		    MT6359_RG_BUCK_VS1_LP_ADDR, MT6359_RG_BUCK_VS1_LP_SHIFT,
		    MT6359_RG_VS1_FPWM_ADDR, MT6359_RG_VS1_FPWM_SHIFT),
	MT6359_BUCK("buck_vgpu11", VGPU11, "vsys-vgpu11", 400000, 1193750, 6250,
		    MT6359_RG_BUCK_VGPU11_EN_ADDR,
		    MT6359_DA_VGPU11_EN_ADDR, MT6359_RG_BUCK_VGPU11_VOSEL_ADDR,
		    MT6359_RG_BUCK_VGPU11_VOSEL_MASK <<
		    MT6359_RG_BUCK_VGPU11_VOSEL_SHIFT,
		    MT6359_RG_BUCK_VGPU11_LP_ADDR,
		    MT6359_RG_BUCK_VGPU11_LP_SHIFT,
		    MT6359_RG_VGPU11_FCCM_ADDR, MT6359_RG_VGPU11_FCCM_SHIFT),
	MT6359_BUCK("buck_vmodem", VMODEM, "vsys-vmodem", 400000, 1100000, 6250,
		    MT6359_RG_BUCK_VMODEM_EN_ADDR,
		    MT6359_DA_VMODEM_EN_ADDR, MT6359_RG_BUCK_VMODEM_VOSEL_ADDR,
		    MT6359_RG_BUCK_VMODEM_VOSEL_MASK <<
		    MT6359_RG_BUCK_VMODEM_VOSEL_SHIFT,
		    MT6359_RG_BUCK_VMODEM_LP_ADDR,
		    MT6359_RG_BUCK_VMODEM_LP_SHIFT,
		    MT6359_RG_VMODEM_FCCM_ADDR, MT6359_RG_VMODEM_FCCM_SHIFT),
	MT6359_BUCK("buck_vpu", VPU, "vsys-vpu", 400000, 1193750, 6250,
		    MT6359_RG_BUCK_VPU_EN_ADDR,
		    MT6359_DA_VPU_EN_ADDR, MT6359_RG_BUCK_VPU_VOSEL_ADDR,
		    MT6359_RG_BUCK_VPU_VOSEL_MASK <<
		    MT6359_RG_BUCK_VPU_VOSEL_SHIFT,
		    MT6359_RG_BUCK_VPU_LP_ADDR, MT6359_RG_BUCK_VPU_LP_SHIFT,
		    MT6359_RG_VPU_FCCM_ADDR, MT6359_RG_VPU_FCCM_SHIFT),
	MT6359_BUCK("buck_vcore", VCORE, "vsys-vcore", 400000, 1193750, 6250,
		    MT6359_RG_BUCK_VCORE_EN_ADDR,
		    MT6359_DA_VCORE_EN_ADDR, MT6359_RG_BUCK_VCORE_VOSEL_ADDR,
		    MT6359_RG_BUCK_VCORE_VOSEL_MASK <<
		    MT6359_RG_BUCK_VCORE_VOSEL_SHIFT,
		    MT6359_RG_BUCK_VCORE_LP_ADDR, MT6359_RG_BUCK_VCORE_LP_SHIFT,
		    MT6359_RG_VCORE_FCCM_ADDR, MT6359_RG_VCORE_FCCM_SHIFT),
	MT6359_BUCK("buck_vs2", VS2, "vsys-vs2", 800000, 1600000, 12500,
		    MT6359_RG_BUCK_VS2_EN_ADDR,
		    MT6359_DA_VS2_EN_ADDR, MT6359_RG_BUCK_VS2_VOSEL_ADDR,
		    MT6359_RG_BUCK_VS2_VOSEL_MASK <<
		    MT6359_RG_BUCK_VS2_VOSEL_SHIFT,
		    MT6359_RG_BUCK_VS2_LP_ADDR, MT6359_RG_BUCK_VS2_LP_SHIFT,
		    MT6359_RG_VS2_FPWM_ADDR, MT6359_RG_VS2_FPWM_SHIFT),
	MT6359_BUCK("buck_vpa", VPA, "vsys-vpa", 500000, 3650000, 50000,
		    MT6359_RG_BUCK_VPA_EN_ADDR,
		    MT6359_DA_VPA_EN_ADDR, MT6359_RG_BUCK_VPA_VOSEL_ADDR,
		    MT6359_RG_BUCK_VPA_VOSEL_MASK <<
		    MT6359_RG_BUCK_VPA_VOSEL_SHIFT,
		    MT6359_RG_BUCK_VPA_LP_ADDR, MT6359_RG_BUCK_VPA_LP_SHIFT,
		    MT6359_RG_VPA_MODESET_ADDR, MT6359_RG_VPA_MODESET_SHIFT),
	MT6359_BUCK("buck_vproc2", VPROC2, "vsys-vproc2", 400000, 1193750, 6250,
		    MT6359_RG_BUCK_VPROC2_EN_ADDR,
		    MT6359_DA_VPROC2_EN_ADDR, MT6359_RG_BUCK_VPROC2_VOSEL_ADDR,
		    MT6359_RG_BUCK_VPROC2_VOSEL_MASK <<
		    MT6359_RG_BUCK_VPROC2_VOSEL_SHIFT,
		    MT6359_RG_BUCK_VPROC2_LP_ADDR,
		    MT6359_RG_BUCK_VPROC2_LP_SHIFT,
		    MT6359_RG_VPROC2_FCCM_ADDR, MT6359_RG_VPROC2_FCCM_SHIFT),
	MT6359_BUCK("buck_vproc1", VPROC1, "vsys-vproc1", 400000, 1193750, 6250,
		    MT6359_RG_BUCK_VPROC1_EN_ADDR,
		    MT6359_DA_VPROC1_EN_ADDR, MT6359_RG_BUCK_VPROC1_VOSEL_ADDR,
		    MT6359_RG_BUCK_VPROC1_VOSEL_MASK <<
		    MT6359_RG_BUCK_VPROC1_VOSEL_SHIFT,
		    MT6359_RG_BUCK_VPROC1_LP_ADDR,
		    MT6359_RG_BUCK_VPROC1_LP_SHIFT,
		    MT6359_RG_VPROC1_FCCM_ADDR, MT6359_RG_VPROC1_FCCM_SHIFT),
	MT6359_BUCK("buck_vcore_sshub", VCORE_SSHUB, "vsys-vcore", 400000, 1193750, 6250,
		    MT6359_RG_BUCK_VCORE_SSHUB_EN_ADDR,
		    MT6359_DA_VCORE_EN_ADDR,
		    MT6359_RG_BUCK_VCORE_SSHUB_VOSEL_ADDR,
		    MT6359_RG_BUCK_VCORE_SSHUB_VOSEL_MASK <<
		    MT6359_RG_BUCK_VCORE_SSHUB_VOSEL_SHIFT,
		    MT6359_RG_BUCK_VCORE_LP_ADDR, MT6359_RG_BUCK_VCORE_LP_SHIFT,
		    MT6359_RG_VCORE_FCCM_ADDR, MT6359_RG_VCORE_FCCM_SHIFT),
	MT6359_REG_FIXED("ldo_vaud18", VAUD18, "vs1-ldo1", MT6359_RG_LDO_VAUD18_EN_ADDR,
			 MT6359_DA_VAUD18_B_EN_ADDR, 1800000),
	MT6359_LDO("ldo_vsim1", VSIM1, "vsys-ldo2", vsim1_voltages,
		   MT6359_RG_LDO_VSIM1_EN_ADDR, MT6359_RG_LDO_VSIM1_EN_SHIFT,
		   MT6359_DA_VSIM1_B_EN_ADDR, MT6359_RG_VSIM1_VOSEL_ADDR,
		   MT6359_RG_VSIM1_VOSEL_MASK << MT6359_RG_VSIM1_VOSEL_SHIFT,
		   480),
	MT6359_LDO("ldo_vibr", VIBR, "vsys-ldo1", vibr_voltages,
		   MT6359_RG_LDO_VIBR_EN_ADDR, MT6359_RG_LDO_VIBR_EN_SHIFT,
		   MT6359_DA_VIBR_B_EN_ADDR, MT6359_RG_VIBR_VOSEL_ADDR,
		   MT6359_RG_VIBR_VOSEL_MASK << MT6359_RG_VIBR_VOSEL_SHIFT,
		   240),
	MT6359_LDO("ldo_vrf12", VRF12, "vs2-ldo2", vrf12_voltages,
		   MT6359_RG_LDO_VRF12_EN_ADDR, MT6359_RG_LDO_VRF12_EN_SHIFT,
		   MT6359_DA_VRF12_B_EN_ADDR, MT6359_RG_VRF12_VOSEL_ADDR,
		   MT6359_RG_VRF12_VOSEL_MASK << MT6359_RG_VRF12_VOSEL_SHIFT,
		   120),
	MT6359_REG_FIXED("ldo_vusb", VUSB, "vsys-ldo2", MT6359_RG_LDO_VUSB_EN_0_ADDR,
			 MT6359_DA_VUSB_B_EN_ADDR, 3000000),
	MT6359_LDO_LINEAR("ldo_vsram_proc2", VSRAM_PROC2, "vs2-ldo1", 500000, 1293750, 6250,
			  MT6359_RG_LDO_VSRAM_PROC2_EN_ADDR,
			  MT6359_DA_VSRAM_PROC2_B_EN_ADDR,
			  MT6359_RG_LDO_VSRAM_PROC2_VOSEL_ADDR,
			  MT6359_RG_LDO_VSRAM_PROC2_VOSEL_MASK <<
			  MT6359_RG_LDO_VSRAM_PROC2_VOSEL_SHIFT),
	MT6359_LDO("ldo_vio18", VIO18, "vs1-ldo2", volt18_voltages,
		   MT6359_RG_LDO_VIO18_EN_ADDR, MT6359_RG_LDO_VIO18_EN_SHIFT,
		   MT6359_DA_VIO18_B_EN_ADDR, MT6359_RG_VIO18_VOSEL_ADDR,
		   MT6359_RG_VIO18_VOSEL_MASK << MT6359_RG_VIO18_VOSEL_SHIFT,
		   960),
	MT6359_LDO("ldo_vcamio", VCAMIO, "vs1-ldo1", volt18_voltages,
		   MT6359_RG_LDO_VCAMIO_EN_ADDR, MT6359_RG_LDO_VCAMIO_EN_SHIFT,
		   MT6359_DA_VCAMIO_B_EN_ADDR, MT6359_RG_VCAMIO_VOSEL_ADDR,
		   MT6359_RG_VCAMIO_VOSEL_MASK << MT6359_RG_VCAMIO_VOSEL_SHIFT,
		   1290),
	MT6359_REG_FIXED("ldo_vcn18", VCN18, "vs1-ldo2", MT6359_RG_LDO_VCN18_EN_ADDR,
			 MT6359_DA_VCN18_B_EN_ADDR, 1800000),
	MT6359_REG_FIXED("ldo_vfe28", VFE28, "vsys-ldo1", MT6359_RG_LDO_VFE28_EN_ADDR,
			 MT6359_DA_VFE28_B_EN_ADDR, 2800000),
	MT6359_LDO("ldo_vcn13", VCN13, "vs2-ldo2", vcn13_voltages,
		   MT6359_RG_LDO_VCN13_EN_ADDR, MT6359_RG_LDO_VCN13_EN_SHIFT,
		   MT6359_DA_VCN13_B_EN_ADDR, MT6359_RG_VCN13_VOSEL_ADDR,
		   MT6359_RG_VCN13_VOSEL_MASK << MT6359_RG_VCN13_VOSEL_SHIFT,
		   240),
	MT6359_LDO("ldo_vcn33_1", VCN33_1, "vsys-ldo1", vcn33_voltages,
		   MT6359_RG_LDO_VCN33_1_EN_0_ADDR,
		   MT6359_RG_LDO_VCN33_1_EN_0_SHIFT,
		   MT6359_DA_VCN33_1_B_EN_ADDR, MT6359_RG_VCN33_1_VOSEL_ADDR,
		   MT6359_RG_VCN33_1_VOSEL_MASK <<
		   MT6359_RG_VCN33_1_VOSEL_SHIFT, 240),
	MT6359_REG_FIXED("ldo_vaux18", VAUX18, "vsys-ldo2", MT6359_RG_LDO_VAUX18_EN_ADDR,
			 MT6359_DA_VAUX18_B_EN_ADDR, 1800000),
	MT6359_LDO_LINEAR("ldo_vsram_others", VSRAM_OTHERS, "vs2-ldo1", 500000, 1293750,
			  6250,
			  MT6359_RG_LDO_VSRAM_OTHERS_EN_ADDR,
			  MT6359_DA_VSRAM_OTHERS_B_EN_ADDR,
			  MT6359_RG_LDO_VSRAM_OTHERS_VOSEL_ADDR,
			  MT6359_RG_LDO_VSRAM_OTHERS_VOSEL_MASK <<
			  MT6359_RG_LDO_VSRAM_OTHERS_VOSEL_SHIFT),
	MT6359_LDO("ldo_vefuse", VEFUSE, "vs1-ldo2", vefuse_voltages,
		   MT6359_RG_LDO_VEFUSE_EN_ADDR, MT6359_RG_LDO_VEFUSE_EN_SHIFT,
		   MT6359_DA_VEFUSE_B_EN_ADDR, MT6359_RG_VEFUSE_VOSEL_ADDR,
		   MT6359_RG_VEFUSE_VOSEL_MASK << MT6359_RG_VEFUSE_VOSEL_SHIFT,
		   240),
	MT6359_LDO("ldo_vxo22", VXO22, "vsys-ldo2", vxo22_voltages,
		   MT6359_RG_LDO_VXO22_EN_ADDR, MT6359_RG_LDO_VXO22_EN_SHIFT,
		   MT6359_DA_VXO22_B_EN_ADDR, MT6359_RG_VXO22_VOSEL_ADDR,
		   MT6359_RG_VXO22_VOSEL_MASK << MT6359_RG_VXO22_VOSEL_SHIFT,
		   120),
	MT6359_LDO("ldo_vrfck", VRFCK, "vsys-ldo2", vrfck_voltages,
		   MT6359_RG_LDO_VRFCK_EN_ADDR, MT6359_RG_LDO_VRFCK_EN_SHIFT,
		   MT6359_DA_VRFCK_B_EN_ADDR, MT6359_RG_VRFCK_VOSEL_ADDR,
		   MT6359_RG_VRFCK_VOSEL_MASK << MT6359_RG_VRFCK_VOSEL_SHIFT,
		   480),
	MT6359_REG_FIXED("ldo_vbif28", VBIF28, "vsys-ldo2", MT6359_RG_LDO_VBIF28_EN_ADDR,
			 MT6359_DA_VBIF28_B_EN_ADDR, 2800000),
	MT6359_LDO("ldo_vio28", VIO28, "vsys-ldo2", vio28_voltages,
		   MT6359_RG_LDO_VIO28_EN_ADDR, MT6359_RG_LDO_VIO28_EN_SHIFT,
		   MT6359_DA_VIO28_B_EN_ADDR, MT6359_RG_VIO28_VOSEL_ADDR,
		   MT6359_RG_VIO28_VOSEL_MASK << MT6359_RG_VIO28_VOSEL_SHIFT,
		   240),
	MT6359_LDO("ldo_vemc", VEMC, "vsys-ldo2", vemc_voltages,
		   MT6359_RG_LDO_VEMC_EN_ADDR, MT6359_RG_LDO_VEMC_EN_SHIFT,
		   MT6359_DA_VEMC_B_EN_ADDR, MT6359_RG_VEMC_VOSEL_ADDR,
		   MT6359_RG_VEMC_VOSEL_MASK << MT6359_RG_VEMC_VOSEL_SHIFT,
		   240),
	MT6359_LDO("ldo_vcn33_2", VCN33_2, "vsys-ldo1", vcn33_voltages,
		   MT6359_RG_LDO_VCN33_2_EN_0_ADDR,
		   MT6359_RG_LDO_VCN33_2_EN_0_SHIFT,
		   MT6359_DA_VCN33_2_B_EN_ADDR, MT6359_RG_VCN33_2_VOSEL_ADDR,
		   MT6359_RG_VCN33_2_VOSEL_MASK <<
		   MT6359_RG_VCN33_2_VOSEL_SHIFT, 240),
	MT6359_LDO("ldo_va12", VA12, "vs2-ldo2", va12_voltages,
		   MT6359_RG_LDO_VA12_EN_ADDR, MT6359_RG_LDO_VA12_EN_SHIFT,
		   MT6359_DA_VA12_B_EN_ADDR, MT6359_RG_VA12_VOSEL_ADDR,
		   MT6359_RG_VA12_VOSEL_MASK << MT6359_RG_VA12_VOSEL_SHIFT,
		   240),
	MT6359_LDO("ldo_va09", VA09, "vs2-ldo2", va09_voltages,
		   MT6359_RG_LDO_VA09_EN_ADDR, MT6359_RG_LDO_VA09_EN_SHIFT,
		   MT6359_DA_VA09_B_EN_ADDR, MT6359_RG_VA09_VOSEL_ADDR,
		   MT6359_RG_VA09_VOSEL_MASK << MT6359_RG_VA09_VOSEL_SHIFT,
		   240),
	MT6359_LDO("ldo_vrf18", VRF18, "vs1-ldo2", vrf18_voltages,
		   MT6359_RG_LDO_VRF18_EN_ADDR, MT6359_RG_LDO_VRF18_EN_SHIFT,
		   MT6359_DA_VRF18_B_EN_ADDR, MT6359_RG_VRF18_VOSEL_ADDR,
		   MT6359_RG_VRF18_VOSEL_MASK << MT6359_RG_VRF18_VOSEL_SHIFT,
		   120),
	MT6359_LDO_LINEAR("ldo_vsram_md", VSRAM_MD, "vs2-ldo1", 500000, 1100000, 6250,
			  MT6359_RG_LDO_VSRAM_MD_EN_ADDR,
			  MT6359_DA_VSRAM_MD_B_EN_ADDR,
			  MT6359_RG_LDO_VSRAM_MD_VOSEL_ADDR,
			  MT6359_RG_LDO_VSRAM_MD_VOSEL_MASK <<
			  MT6359_RG_LDO_VSRAM_MD_VOSEL_SHIFT),
	MT6359_LDO("ldo_vufs", VUFS, "vs1-ldo1", volt18_voltages,
		   MT6359_RG_LDO_VUFS_EN_ADDR, MT6359_RG_LDO_VUFS_EN_SHIFT,
		   MT6359_DA_VUFS_B_EN_ADDR, MT6359_RG_VUFS_VOSEL_ADDR,
		   MT6359_RG_VUFS_VOSEL_MASK << MT6359_RG_VUFS_VOSEL_SHIFT,
		   1920),
	MT6359_LDO("ldo_vm18", VM18, "vs1-ldo1", volt18_voltages,
		   MT6359_RG_LDO_VM18_EN_ADDR, MT6359_RG_LDO_VM18_EN_SHIFT,
		   MT6359_DA_VM18_B_EN_ADDR, MT6359_RG_VM18_VOSEL_ADDR,
		   MT6359_RG_VM18_VOSEL_MASK << MT6359_RG_VM18_VOSEL_SHIFT,
		   1920),
	/* vbbck is fed from vio18 internally. */
	MT6359_LDO("ldo_vbbck", VBBCK, "LDO_VIO18", vbbck_voltages,
		   MT6359_RG_LDO_VBBCK_EN_ADDR, MT6359_RG_LDO_VBBCK_EN_SHIFT,
		   MT6359_DA_VBBCK_B_EN_ADDR, MT6359_RG_VBBCK_VOSEL_ADDR,
		   MT6359_RG_VBBCK_VOSEL_MASK << MT6359_RG_VBBCK_VOSEL_SHIFT,
		   240),
	MT6359_LDO_LINEAR("ldo_vsram_proc1", VSRAM_PROC1, "vs2-ldo1", 500000, 1293750, 6250,
			  MT6359_RG_LDO_VSRAM_PROC1_EN_ADDR,
			  MT6359_DA_VSRAM_PROC1_B_EN_ADDR,
			  MT6359_RG_LDO_VSRAM_PROC1_VOSEL_ADDR,
			  MT6359_RG_LDO_VSRAM_PROC1_VOSEL_MASK <<
			  MT6359_RG_LDO_VSRAM_PROC1_VOSEL_SHIFT),
	MT6359_LDO("ldo_vsim2", VSIM2, "vsys-ldo2", vsim2_voltages,
		   MT6359_RG_LDO_VSIM2_EN_ADDR, MT6359_RG_LDO_VSIM2_EN_SHIFT,
		   MT6359_DA_VSIM2_B_EN_ADDR, MT6359_RG_VSIM2_VOSEL_ADDR,
		   MT6359_RG_VSIM2_VOSEL_MASK << MT6359_RG_VSIM2_VOSEL_SHIFT,
		   480),
	MT6359_LDO_LINEAR("ldo_vsram_others_sshub", VSRAM_OTHERS_SSHUB, "vs2-ldo1",
			  500000, 1293750, 6250,
			  MT6359_RG_LDO_VSRAM_OTHERS_SSHUB_EN_ADDR,
			  MT6359_DA_VSRAM_OTHERS_B_EN_ADDR,
			  MT6359_RG_LDO_VSRAM_OTHERS_SSHUB_VOSEL_ADDR,
			  MT6359_RG_LDO_VSRAM_OTHERS_SSHUB_VOSEL_MASK <<
			  MT6359_RG_LDO_VSRAM_OTHERS_SSHUB_VOSEL_SHIFT),
	/* Placeholders for DT backward compatibility */
	MT6359_LDO_NOOP("ldo_vcn33_1_bt",   VCN33_1_BT,   "LDO_VCN33_1"),
	MT6359_LDO_NOOP("ldo_vcn33_1_wifi", VCN33_1_WIFI, "LDO_VCN33_1"),
	MT6359_LDO_NOOP("ldo_vcn33_2_bt",   VCN33_2_BT,   "LDO_VCN33_2"),
	MT6359_LDO_NOOP("ldo_vcn33_2_wifi", VCN33_2_WIFI, "LDO_VCN33_2"),
};

static const struct mt6359_regulator_info mt6359p_regulators[] = {
	MT6359_BUCK("buck_vs1", VS1, "vsys-vs1", 800000, 2200000, 12500,
		    MT6359_RG_BUCK_VS1_EN_ADDR,
		    MT6359_DA_VS1_EN_ADDR, MT6359_RG_BUCK_VS1_VOSEL_ADDR,
		    MT6359_RG_BUCK_VS1_VOSEL_MASK <<
		    MT6359_RG_BUCK_VS1_VOSEL_SHIFT,
		    MT6359_RG_BUCK_VS1_LP_ADDR, MT6359_RG_BUCK_VS1_LP_SHIFT,
		    MT6359_RG_VS1_FPWM_ADDR, MT6359_RG_VS1_FPWM_SHIFT),
	MT6359_BUCK("buck_vgpu11", VGPU11, "vsys-vgpu11", 400000, 1193750, 6250,
		    MT6359_RG_BUCK_VGPU11_EN_ADDR,
		    MT6359_DA_VGPU11_EN_ADDR, MT6359P_RG_BUCK_VGPU11_VOSEL_ADDR,
		    MT6359_RG_BUCK_VGPU11_VOSEL_MASK <<
		    MT6359_RG_BUCK_VGPU11_VOSEL_SHIFT,
		    MT6359_RG_BUCK_VGPU11_LP_ADDR,
		    MT6359_RG_BUCK_VGPU11_LP_SHIFT,
		    MT6359_RG_VGPU11_FCCM_ADDR, MT6359_RG_VGPU11_FCCM_SHIFT),
	MT6359_BUCK("buck_vmodem", VMODEM, "vsys-vmodem", 400000, 1100000, 6250,
		    MT6359_RG_BUCK_VMODEM_EN_ADDR,
		    MT6359_DA_VMODEM_EN_ADDR, MT6359_RG_BUCK_VMODEM_VOSEL_ADDR,
		    MT6359_RG_BUCK_VMODEM_VOSEL_MASK <<
		    MT6359_RG_BUCK_VMODEM_VOSEL_SHIFT,
		    MT6359_RG_BUCK_VMODEM_LP_ADDR,
		    MT6359_RG_BUCK_VMODEM_LP_SHIFT,
		    MT6359_RG_VMODEM_FCCM_ADDR, MT6359_RG_VMODEM_FCCM_SHIFT),
	MT6359_BUCK("buck_vpu", VPU, "vsys-vpu", 400000, 1193750, 6250,
		    MT6359_RG_BUCK_VPU_EN_ADDR,
		    MT6359_DA_VPU_EN_ADDR, MT6359_RG_BUCK_VPU_VOSEL_ADDR,
		    MT6359_RG_BUCK_VPU_VOSEL_MASK <<
		    MT6359_RG_BUCK_VPU_VOSEL_SHIFT,
		    MT6359_RG_BUCK_VPU_LP_ADDR, MT6359_RG_BUCK_VPU_LP_SHIFT,
		    MT6359_RG_VPU_FCCM_ADDR, MT6359_RG_VPU_FCCM_SHIFT),
	MT6359_BUCK("buck_vcore", VCORE, "vsys-vcore", 506250, 1300000, 6250,
		    MT6359_RG_BUCK_VCORE_EN_ADDR,
		    MT6359_DA_VCORE_EN_ADDR, MT6359P_RG_BUCK_VCORE_VOSEL_ADDR,
		    MT6359_RG_BUCK_VCORE_VOSEL_MASK <<
		    MT6359_RG_BUCK_VCORE_VOSEL_SHIFT,
		    MT6359_RG_BUCK_VCORE_LP_ADDR, MT6359_RG_BUCK_VCORE_LP_SHIFT,
		    MT6359_RG_VCORE_FCCM_ADDR, MT6359_RG_VCORE_FCCM_SHIFT),
	MT6359_BUCK("buck_vs2", VS2, "vsys-vs2", 800000, 1600000, 12500,
		    MT6359_RG_BUCK_VS2_EN_ADDR,
		    MT6359_DA_VS2_EN_ADDR, MT6359_RG_BUCK_VS2_VOSEL_ADDR,
		    MT6359_RG_BUCK_VS2_VOSEL_MASK <<
		    MT6359_RG_BUCK_VS2_VOSEL_SHIFT,
		    MT6359_RG_BUCK_VS2_LP_ADDR, MT6359_RG_BUCK_VS2_LP_SHIFT,
		    MT6359_RG_VS2_FPWM_ADDR, MT6359_RG_VS2_FPWM_SHIFT),
	MT6359_BUCK("buck_vpa", VPA, "vsys-vpa", 500000, 3650000, 50000,
		    MT6359_RG_BUCK_VPA_EN_ADDR,
		    MT6359_DA_VPA_EN_ADDR, MT6359_RG_BUCK_VPA_VOSEL_ADDR,
		    MT6359_RG_BUCK_VPA_VOSEL_MASK <<
		    MT6359_RG_BUCK_VPA_VOSEL_SHIFT,
		    MT6359_RG_BUCK_VPA_LP_ADDR, MT6359_RG_BUCK_VPA_LP_SHIFT,
		    MT6359_RG_VPA_MODESET_ADDR, MT6359_RG_VPA_MODESET_SHIFT),
	MT6359_BUCK("buck_vproc2", VPROC2, "vsys-vproc2", 400000, 1193750, 6250,
		    MT6359_RG_BUCK_VPROC2_EN_ADDR,
		    MT6359_DA_VPROC2_EN_ADDR, MT6359_RG_BUCK_VPROC2_VOSEL_ADDR,
		    MT6359_RG_BUCK_VPROC2_VOSEL_MASK <<
		    MT6359_RG_BUCK_VPROC2_VOSEL_SHIFT,
		    MT6359_RG_BUCK_VPROC2_LP_ADDR,
		    MT6359_RG_BUCK_VPROC2_LP_SHIFT,
		    MT6359_RG_VPROC2_FCCM_ADDR, MT6359_RG_VPROC2_FCCM_SHIFT),
	MT6359_BUCK("buck_vproc1", VPROC1, "vsys-vproc1", 400000, 1193750, 6250,
		    MT6359_RG_BUCK_VPROC1_EN_ADDR,
		    MT6359_DA_VPROC1_EN_ADDR, MT6359_RG_BUCK_VPROC1_VOSEL_ADDR,
		    MT6359_RG_BUCK_VPROC1_VOSEL_MASK <<
		    MT6359_RG_BUCK_VPROC1_VOSEL_SHIFT,
		    MT6359_RG_BUCK_VPROC1_LP_ADDR,
		    MT6359_RG_BUCK_VPROC1_LP_SHIFT,
		    MT6359_RG_VPROC1_FCCM_ADDR, MT6359_RG_VPROC1_FCCM_SHIFT),
	MT6359_BUCK("buck_vgpu11_sshub", VGPU11_SSHUB, "vsys-vgpu11", 400000, 1193750, 6250,
		    MT6359P_RG_BUCK_VGPU11_SSHUB_EN_ADDR,
		    MT6359_DA_VGPU11_EN_ADDR,
		    MT6359P_RG_BUCK_VGPU11_SSHUB_VOSEL_ADDR,
		    MT6359P_RG_BUCK_VGPU11_SSHUB_VOSEL_MASK <<
		    MT6359P_RG_BUCK_VGPU11_SSHUB_VOSEL_SHIFT,
		    MT6359_RG_BUCK_VGPU11_LP_ADDR,
		    MT6359_RG_BUCK_VGPU11_LP_SHIFT,
		    MT6359_RG_VGPU11_FCCM_ADDR, MT6359_RG_VGPU11_FCCM_SHIFT),
	MT6359_REG_FIXED("ldo_vaud18", VAUD18, "vs1-ldo1", MT6359P_RG_LDO_VAUD18_EN_ADDR,
			 MT6359P_DA_VAUD18_B_EN_ADDR, 1800000),
	MT6359_LDO("ldo_vsim1", VSIM1, "vsys-ldo2", vsim1_voltages,
		   MT6359P_RG_LDO_VSIM1_EN_ADDR, MT6359P_RG_LDO_VSIM1_EN_SHIFT,
		   MT6359P_DA_VSIM1_B_EN_ADDR, MT6359P_RG_VSIM1_VOSEL_ADDR,
		   MT6359_RG_VSIM1_VOSEL_MASK << MT6359_RG_VSIM1_VOSEL_SHIFT,
		   480),
	MT6359_LDO("ldo_vibr", VIBR, "vsys-ldo1", vibr_voltages,
		   MT6359P_RG_LDO_VIBR_EN_ADDR, MT6359P_RG_LDO_VIBR_EN_SHIFT,
		   MT6359P_DA_VIBR_B_EN_ADDR, MT6359P_RG_VIBR_VOSEL_ADDR,
		   MT6359_RG_VIBR_VOSEL_MASK << MT6359_RG_VIBR_VOSEL_SHIFT,
		   240),
	MT6359_LDO("ldo_vrf12", VRF12, "vs2-ldo2", vrf12_voltages,
		   MT6359P_RG_LDO_VRF12_EN_ADDR, MT6359P_RG_LDO_VRF12_EN_SHIFT,
		   MT6359P_DA_VRF12_B_EN_ADDR, MT6359P_RG_VRF12_VOSEL_ADDR,
		   MT6359_RG_VRF12_VOSEL_MASK << MT6359_RG_VRF12_VOSEL_SHIFT,
		   480),
	MT6359_REG_FIXED("ldo_vusb", VUSB, "vsys-ldo2", MT6359P_RG_LDO_VUSB_EN_0_ADDR,
			 MT6359P_DA_VUSB_B_EN_ADDR, 3000000),
	MT6359_LDO_LINEAR("ldo_vsram_proc2", VSRAM_PROC2, "vs2-ldo1", 500000, 1293750, 6250,
			  MT6359P_RG_LDO_VSRAM_PROC2_EN_ADDR,
			  MT6359P_DA_VSRAM_PROC2_B_EN_ADDR,
			  MT6359P_RG_LDO_VSRAM_PROC2_VOSEL_ADDR,
			  MT6359_RG_LDO_VSRAM_PROC2_VOSEL_MASK <<
			  MT6359_RG_LDO_VSRAM_PROC2_VOSEL_SHIFT),
	MT6359_LDO("ldo_vio18", VIO18, "vs1-ldo2", volt18_voltages,
		   MT6359P_RG_LDO_VIO18_EN_ADDR, MT6359P_RG_LDO_VIO18_EN_SHIFT,
		   MT6359P_DA_VIO18_B_EN_ADDR, MT6359P_RG_VIO18_VOSEL_ADDR,
		   MT6359_RG_VIO18_VOSEL_MASK << MT6359_RG_VIO18_VOSEL_SHIFT,
		   960),
	MT6359_LDO("ldo_vcamio", VCAMIO, "vs1-ldo1", volt18_voltages,
		   MT6359P_RG_LDO_VCAMIO_EN_ADDR,
		   MT6359P_RG_LDO_VCAMIO_EN_SHIFT,
		   MT6359P_DA_VCAMIO_B_EN_ADDR, MT6359P_RG_VCAMIO_VOSEL_ADDR,
		   MT6359_RG_VCAMIO_VOSEL_MASK << MT6359_RG_VCAMIO_VOSEL_SHIFT,
		   1290),
	MT6359_REG_FIXED("ldo_vcn18", VCN18, "vs1-ldo2", MT6359P_RG_LDO_VCN18_EN_ADDR,
			 MT6359P_DA_VCN18_B_EN_ADDR, 1800000),
	MT6359_REG_FIXED("ldo_vfe28", VFE28, "vsys-ldo1", MT6359P_RG_LDO_VFE28_EN_ADDR,
			 MT6359P_DA_VFE28_B_EN_ADDR, 2800000),
	MT6359_LDO("ldo_vcn13", VCN13, "vs2-ldo2", vcn13_voltages,
		   MT6359P_RG_LDO_VCN13_EN_ADDR, MT6359P_RG_LDO_VCN13_EN_SHIFT,
		   MT6359P_DA_VCN13_B_EN_ADDR, MT6359P_RG_VCN13_VOSEL_ADDR,
		   MT6359_RG_VCN13_VOSEL_MASK << MT6359_RG_VCN13_VOSEL_SHIFT,
		   240),
	MT6359_LDO("ldo_vcn33_1", VCN33_1, "vsys-ldo1", vcn33_voltages,
		   MT6359P_RG_LDO_VCN33_1_EN_0_ADDR,
		   MT6359_RG_LDO_VCN33_1_EN_0_SHIFT,
		   MT6359P_DA_VCN33_1_B_EN_ADDR, MT6359P_RG_VCN33_1_VOSEL_ADDR,
		   MT6359_RG_VCN33_1_VOSEL_MASK <<
		   MT6359_RG_VCN33_1_VOSEL_SHIFT, 240),
	MT6359_REG_FIXED("ldo_vaux18", VAUX18, "vsys-ldo2", MT6359P_RG_LDO_VAUX18_EN_ADDR,
			 MT6359P_DA_VAUX18_B_EN_ADDR, 1800000),
	MT6359_LDO_LINEAR("ldo_vsram_others", VSRAM_OTHERS, "vs2-ldo1", 500000, 1293750,
			  6250,
			  MT6359P_RG_LDO_VSRAM_OTHERS_EN_ADDR,
			  MT6359P_DA_VSRAM_OTHERS_B_EN_ADDR,
			  MT6359P_RG_LDO_VSRAM_OTHERS_VOSEL_ADDR,
			  MT6359_RG_LDO_VSRAM_OTHERS_VOSEL_MASK <<
			  MT6359_RG_LDO_VSRAM_OTHERS_VOSEL_SHIFT),
	MT6359_LDO("ldo_vefuse", VEFUSE, "vs1-ldo2", vefuse_voltages,
		   MT6359P_RG_LDO_VEFUSE_EN_ADDR,
		   MT6359P_RG_LDO_VEFUSE_EN_SHIFT,
		   MT6359P_DA_VEFUSE_B_EN_ADDR, MT6359P_RG_VEFUSE_VOSEL_ADDR,
		   MT6359_RG_VEFUSE_VOSEL_MASK << MT6359_RG_VEFUSE_VOSEL_SHIFT,
		   240),
	MT6359_LDO("ldo_vxo22", VXO22, "vsys-ldo2", vxo22_voltages,
		   MT6359P_RG_LDO_VXO22_EN_ADDR, MT6359P_RG_LDO_VXO22_EN_SHIFT,
		   MT6359P_DA_VXO22_B_EN_ADDR, MT6359P_RG_VXO22_VOSEL_ADDR,
		   MT6359_RG_VXO22_VOSEL_MASK << MT6359_RG_VXO22_VOSEL_SHIFT,
		   480),
	MT6359_LDO("ldo_vrfck_1", VRFCK, "vsys-ldo2", vrfck_voltages_1,
		   MT6359P_RG_LDO_VRFCK_EN_ADDR, MT6359P_RG_LDO_VRFCK_EN_SHIFT,
		   MT6359P_DA_VRFCK_B_EN_ADDR, MT6359P_RG_VRFCK_VOSEL_ADDR,
		   MT6359_RG_VRFCK_VOSEL_MASK << MT6359_RG_VRFCK_VOSEL_SHIFT,
		   480),
	MT6359_REG_FIXED("ldo_vbif28", VBIF28, "vsys-ldo2", MT6359P_RG_LDO_VBIF28_EN_ADDR,
			 MT6359P_DA_VBIF28_B_EN_ADDR, 2800000),
	MT6359_LDO("ldo_vio28", VIO28, "vsys-ldo2", vio28_voltages,
		   MT6359P_RG_LDO_VIO28_EN_ADDR, MT6359P_RG_LDO_VIO28_EN_SHIFT,
		   MT6359P_DA_VIO28_B_EN_ADDR, MT6359P_RG_VIO28_VOSEL_ADDR,
		   MT6359_RG_VIO28_VOSEL_MASK << MT6359_RG_VIO28_VOSEL_SHIFT,
		   1920),
	MT6359P_LDO1("ldo_vemc_1", VEMC, "vsys-ldo2", mt6359p_vemc_ops, vemc_voltages_1,
		     MT6359P_RG_LDO_VEMC_EN_ADDR, MT6359P_RG_LDO_VEMC_EN_SHIFT,
		     MT6359P_DA_VEMC_B_EN_ADDR,
		     MT6359P_RG_LDO_VEMC_VOSEL_0_ADDR,
		     MT6359P_RG_LDO_VEMC_VOSEL_0_MASK <<
		     MT6359P_RG_LDO_VEMC_VOSEL_0_SHIFT),
	MT6359_LDO("ldo_vcn33_2", VCN33_2, "vsys-ldo1", vcn33_voltages,
		   MT6359P_RG_LDO_VCN33_2_EN_0_ADDR,
		   MT6359P_RG_LDO_VCN33_2_EN_0_SHIFT,
		   MT6359P_DA_VCN33_2_B_EN_ADDR, MT6359P_RG_VCN33_2_VOSEL_ADDR,
		   MT6359_RG_VCN33_2_VOSEL_MASK <<
		   MT6359_RG_VCN33_2_VOSEL_SHIFT, 240),
	MT6359_LDO("ldo_va12", VA12, "vs2-ldo2", va12_voltages,
		   MT6359P_RG_LDO_VA12_EN_ADDR, MT6359P_RG_LDO_VA12_EN_SHIFT,
		   MT6359P_DA_VA12_B_EN_ADDR, MT6359P_RG_VA12_VOSEL_ADDR,
		   MT6359_RG_VA12_VOSEL_MASK << MT6359_RG_VA12_VOSEL_SHIFT,
		   960),
	MT6359_LDO("ldo_va09", VA09, "vs2-ldo2", va09_voltages,
		   MT6359P_RG_LDO_VA09_EN_ADDR, MT6359P_RG_LDO_VA09_EN_SHIFT,
		   MT6359P_DA_VA09_B_EN_ADDR, MT6359P_RG_VA09_VOSEL_ADDR,
		   MT6359_RG_VA09_VOSEL_MASK << MT6359_RG_VA09_VOSEL_SHIFT,
		   960),
	MT6359_LDO("ldo_vrf18", VRF18, "vs1-ldo2", vrf18_voltages,
		   MT6359P_RG_LDO_VRF18_EN_ADDR, MT6359P_RG_LDO_VRF18_EN_SHIFT,
		   MT6359P_DA_VRF18_B_EN_ADDR, MT6359P_RG_VRF18_VOSEL_ADDR,
		   MT6359_RG_VRF18_VOSEL_MASK << MT6359_RG_VRF18_VOSEL_SHIFT,
		   240),
	MT6359_LDO_LINEAR("ldo_vsram_md", VSRAM_MD, "vs2-ldo1", 500000, 1293750, 6250,
			  MT6359P_RG_LDO_VSRAM_MD_EN_ADDR,
			  MT6359P_DA_VSRAM_MD_B_EN_ADDR,
			  MT6359P_RG_LDO_VSRAM_MD_VOSEL_ADDR,
			  MT6359_RG_LDO_VSRAM_MD_VOSEL_MASK <<
			  MT6359_RG_LDO_VSRAM_MD_VOSEL_SHIFT),
	MT6359_LDO("ldo_vufs", VUFS, "vs1-ldo1", volt18_voltages,
		   MT6359P_RG_LDO_VUFS_EN_ADDR, MT6359P_RG_LDO_VUFS_EN_SHIFT,
		   MT6359P_DA_VUFS_B_EN_ADDR, MT6359P_RG_VUFS_VOSEL_ADDR,
		   MT6359_RG_VUFS_VOSEL_MASK << MT6359_RG_VUFS_VOSEL_SHIFT,
		   1920),
	MT6359_LDO("ldo_vm18", VM18, "vs1-ldo1", volt18_voltages,
		   MT6359P_RG_LDO_VM18_EN_ADDR, MT6359P_RG_LDO_VM18_EN_SHIFT,
		   MT6359P_DA_VM18_B_EN_ADDR, MT6359P_RG_VM18_VOSEL_ADDR,
		   MT6359_RG_VM18_VOSEL_MASK << MT6359_RG_VM18_VOSEL_SHIFT,
		   1920),
	/* vbbck is fed from vio18 internally. */
	MT6359_LDO("ldo_vbbck", VBBCK, "LDO_VIO18", vbbck_voltages,
		   MT6359P_RG_LDO_VBBCK_EN_ADDR, MT6359P_RG_LDO_VBBCK_EN_SHIFT,
		   MT6359P_DA_VBBCK_B_EN_ADDR, MT6359P_RG_VBBCK_VOSEL_ADDR,
		   MT6359P_RG_VBBCK_VOSEL_MASK << MT6359P_RG_VBBCK_VOSEL_SHIFT,
		   480),
	MT6359_LDO_LINEAR("ldo_vsram_proc1", VSRAM_PROC1, "vs2-ldo1", 500000, 1293750, 6250,
			  MT6359P_RG_LDO_VSRAM_PROC1_EN_ADDR,
			  MT6359P_DA_VSRAM_PROC1_B_EN_ADDR,
			  MT6359P_RG_LDO_VSRAM_PROC1_VOSEL_ADDR,
			  MT6359_RG_LDO_VSRAM_PROC1_VOSEL_MASK <<
			  MT6359_RG_LDO_VSRAM_PROC1_VOSEL_SHIFT),
	MT6359_LDO("ldo_vsim2", VSIM2, "vsys-ldo2", vsim2_voltages,
		   MT6359P_RG_LDO_VSIM2_EN_ADDR, MT6359P_RG_LDO_VSIM2_EN_SHIFT,
		   MT6359P_DA_VSIM2_B_EN_ADDR, MT6359P_RG_VSIM2_VOSEL_ADDR,
		   MT6359_RG_VSIM2_VOSEL_MASK << MT6359_RG_VSIM2_VOSEL_SHIFT,
		   480),
	MT6359_LDO_LINEAR("ldo_vsram_others_sshub", VSRAM_OTHERS_SSHUB, "vs2-ldo1",
			  500000, 1293750, 6250,
			  MT6359P_RG_LDO_VSRAM_OTHERS_SSHUB_EN_ADDR,
			  MT6359P_DA_VSRAM_OTHERS_B_EN_ADDR,
			  MT6359P_RG_LDO_VSRAM_OTHERS_SSHUB_VOSEL_ADDR,
			  MT6359_RG_LDO_VSRAM_OTHERS_SSHUB_VOSEL_MASK <<
			  MT6359_RG_LDO_VSRAM_OTHERS_SSHUB_VOSEL_SHIFT),
	/* Placeholders for DT backward compatibility */
	MT6359_LDO_NOOP("ldo_vcn33_1_bt",   VCN33_1_BT,   "LDO_VCN33_1"),
	MT6359_LDO_NOOP("ldo_vcn33_1_wifi", VCN33_1_WIFI, "LDO_VCN33_1"),
	MT6359_LDO_NOOP("ldo_vcn33_2_bt",   VCN33_2_BT,   "LDO_VCN33_2"),
	MT6359_LDO_NOOP("ldo_vcn33_2_wifi", VCN33_2_WIFI, "LDO_VCN33_2"),
};

struct mt6359_vcn33_regs {
	u32 wifi_en_reg;
	u32 wifi_en_mask;
	u32 bt_en_reg;
	u32 bt_en_mask;
};

static const struct mt6359_vcn33_regs vcn33_regs[][2] = {
	{ /* MT6359 */
		{
			.wifi_en_reg = MT6359_RG_LDO_VCN33_1_EN_1_ADDR,
			.wifi_en_mask = BIT(MT6359_RG_LDO_VCN33_1_EN_1_SHIFT),
			.bt_en_reg = MT6359_RG_LDO_VCN33_1_EN_0_ADDR,
			.bt_en_mask = BIT(MT6359_RG_LDO_VCN33_1_EN_0_SHIFT),
		}, {
			.wifi_en_reg = MT6359_RG_LDO_VCN33_2_EN_1_ADDR,
			.wifi_en_mask = BIT(MT6359_RG_LDO_VCN33_2_EN_1_SHIFT),
			.bt_en_reg = MT6359_RG_LDO_VCN33_2_EN_0_ADDR,
			.bt_en_mask = BIT(MT6359_RG_LDO_VCN33_2_EN_0_SHIFT),
		}
	}, { /* MT6359P */
		{
			.wifi_en_reg = MT6359P_RG_LDO_VCN33_1_EN_1_ADDR,
			.wifi_en_mask = BIT(MT6359P_RG_LDO_VCN33_1_EN_1_SHIFT),
			.bt_en_reg = MT6359P_RG_LDO_VCN33_1_EN_0_ADDR,
			.bt_en_mask = BIT(MT6359_RG_LDO_VCN33_1_EN_0_SHIFT),
		}, {
			.wifi_en_reg = MT6359P_RG_LDO_VCN33_2_EN_1_ADDR,
			.wifi_en_mask = BIT(MT6359_RG_LDO_VCN33_2_EN_1_SHIFT),
			.bt_en_reg = MT6359P_RG_LDO_VCN33_2_EN_0_ADDR,
			.bt_en_mask = BIT(MT6359P_RG_LDO_VCN33_2_EN_0_SHIFT),
		}
	}
};

static int mt6359_sync_vcn33_setting(struct device *dev, unsigned int idx)
{
	struct mt6397_chip *mt6397 = dev_get_drvdata(dev->parent);
	unsigned int val;
	int ret;

	/*
	 * VCN33_[12]_WIFI and VCN33_[12]_BT are two separate enable bits for
	 * the same regulator. They share the same voltage setting and output
	 * pin. Instead of having two potentially conflicting regulators, just
	 * have one regulator. Sync the two enable bits and only use one in
	 * the regulator device.
	 */
	for (unsigned int i = 0; i < ARRAY_SIZE(vcn33_regs[0]); i++) {
		u32 bt_en_mask = vcn33_regs[idx][i].bt_en_mask;
		u32 wifi_en_mask = vcn33_regs[idx][i].wifi_en_mask;

		ret = regmap_read(mt6397->regmap, vcn33_regs[idx][i].wifi_en_reg, &val);
		if (ret)
			return dev_err_probe(dev, ret, "Failed to read VCN33_%u_WIFI setting\n",
					     i + 1);

		if (!(val & wifi_en_mask))
			continue;

		/* Sync VCN33_[12]_WIFI enable status to VCN33_[12]_BT */
		ret = regmap_update_bits(mt6397->regmap, vcn33_regs[idx][i].bt_en_reg,
					 bt_en_mask, bt_en_mask);
		if (ret)
			return dev_err_probe(dev, ret,
					     "Failed to sync VCN33_%u_WIFI setting to VCN33_%u_BT\n",
					     i + 1, i + 1);

		/* Disable VCN33_[12]_WIFI */
		ret = regmap_update_bits(mt6397->regmap, vcn33_regs[idx][i].wifi_en_reg,
					 wifi_en_mask, 0);
		if (ret)
			return dev_err_probe(dev, ret, "Failed to disable VCN33_%u_WIFI\n", i + 1);
	}

	return 0;
}

static int mt6359_regulator_probe(struct platform_device *pdev)
{
	struct mt6397_chip *mt6397 = dev_get_drvdata(pdev->dev.parent);
	struct regulator_config config = {};
	struct regulator_dev *rdev;
	const struct mt6359_regulator_info *mt6359_info;
	const char *vio18_name, *vcn33_1_name, *vcn33_2_name;
	int i, hw_ver, ret;

	ret = regmap_read(mt6397->regmap, MT6359P_HWCID, &hw_ver);
	if (ret)
		return ret;

	if (hw_ver >= MT6359P_CHIP_VER) {
		mt6359_info = mt6359p_regulators;
		ret = mt6359_sync_vcn33_setting(&pdev->dev, 1);
		if (ret)
			return ret;
	} else {
		mt6359_info = mt6359_regulators;
		ret = mt6359_sync_vcn33_setting(&pdev->dev, 0);
		if (ret)
			return ret;
	}

	vio18_name = mt6359_info[MT6359_ID_VIO18].desc.name;
	vcn33_1_name = mt6359_info[MT6359_ID_VCN33_1].desc.name;
	vcn33_2_name = mt6359_info[MT6359_ID_VCN33_2].desc.name;

	config.dev = mt6397->dev;
	config.regmap = mt6397->regmap;
	for (i = 0; i < MT6359_MAX_REGULATOR; i++, mt6359_info++) {
		const struct regulator_desc *desc = &mt6359_info->desc;
		struct regulator_desc *_desc;

		/* drop const here, but all uses in the driver are const */
		config.driver_data = (void *)mt6359_info;

		/* Use vio18's actual name as supply_name for vbbck */
		if (i == MT6359_ID_VBBCK && strcmp(desc->supply_name, vio18_name) != 0) {
			_desc = devm_kzalloc(&pdev->dev, sizeof(*_desc), GFP_KERNEL);
			if (!_desc)
				return -ENOMEM;

			memcpy(_desc, desc, sizeof(*_desc));
			_desc->supply_name = vio18_name;
			desc = _desc;
		}

		/* Use vcn33_1's actual name as supply_name for vcn33_1_(bt|wifi) */
		if ((i == MT6359_ID_VCN33_1_BT || i == MT6359_ID_VCN33_1_WIFI) &&
		    strcmp(desc->supply_name, vcn33_1_name) != 0) {
			_desc = devm_kzalloc(&pdev->dev, sizeof(*_desc), GFP_KERNEL);
			if (!_desc)
				return -ENOMEM;

			memcpy(_desc, desc, sizeof(*_desc));
			_desc->supply_name = vcn33_1_name;
			desc = _desc;
		}

		/* Use vcn33_2's actual name as supply_name for vcn33_2_(bt|wifi) */
		if ((i == MT6359_ID_VCN33_2_BT || i == MT6359_ID_VCN33_2_WIFI) &&
		    strcmp(desc->supply_name, vcn33_2_name) != 0) {
			_desc = devm_kzalloc(&pdev->dev, sizeof(*_desc), GFP_KERNEL);
			if (!_desc)
				return -ENOMEM;

			memcpy(_desc, desc, sizeof(*_desc));
			_desc->supply_name = vcn33_2_name;
			desc = _desc;
		}

		rdev = devm_regulator_register(&pdev->dev, desc, &config);
		if (IS_ERR(rdev)) {
			dev_err(&pdev->dev, "failed to register %s\n", mt6359_info->desc.name);
			return PTR_ERR(rdev);
		}

		/* Save vio18 name for vbbck */
		if (i == MT6359_ID_VIO18)
			vio18_name = rdev_get_name(rdev);

		/* Save vcn33_1 name for vbbck */
		if (i == MT6359_ID_VCN33_1)
			vcn33_1_name = rdev_get_name(rdev);

		/* Save vcn33_2 name for vbbck */
		if (i == MT6359_ID_VCN33_2)
			vcn33_2_name = rdev_get_name(rdev);
	}

	return 0;
}

static const struct platform_device_id mt6359_platform_ids[] = {
	{ .name = "mt6359-regulator" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, mt6359_platform_ids);

static struct platform_driver mt6359_regulator_driver = {
	.driver = {
		.name = "mt6359-regulator",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.probe = mt6359_regulator_probe,
	.id_table = mt6359_platform_ids,
};

module_platform_driver(mt6359_regulator_driver);

MODULE_AUTHOR("Wen Su <wen.su@mediatek.com>");
MODULE_DESCRIPTION("Regulator Driver for MediaTek MT6359 PMIC");
MODULE_LICENSE("GPL");
