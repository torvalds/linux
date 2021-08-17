// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2021 MediaTek Inc.

#include <linux/platform_device.h>
#include <linux/mfd/mt6359/registers.h>
#include <linux/mfd/mt6359p/registers.h>
#include <linux/mfd/mt6397/core.h>
#include <linux/module.h>
#include <linux/of_device.h>
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
 * @modeset_shift: SHIFT for operating modeset register.
 */
struct mt6359_regulator_info {
	struct regulator_desc desc;
	u32 status_reg;
	u32 qi;
	u32 modeset_reg;
	u32 modeset_mask;
	u32 modeset_shift;
	u32 lp_mode_reg;
	u32 lp_mode_mask;
	u32 lp_mode_shift;
};

#define MT6359_BUCK(match, _name, min, max, step,		\
	_enable_reg, _status_reg,				\
	_vsel_reg, _vsel_mask,					\
	_lp_mode_reg, _lp_mode_shift,				\
	_modeset_reg, _modeset_shift)				\
[MT6359_ID_##_name] = {						\
	.desc = {						\
		.name = #_name,					\
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
	.lp_mode_shift = _lp_mode_shift,			\
	.modeset_reg = _modeset_reg,				\
	.modeset_mask = BIT(_modeset_shift),			\
	.modeset_shift = _modeset_shift				\
}

#define MT6359_LDO_LINEAR(match, _name, min, max, step,		\
	_enable_reg, _status_reg, _vsel_reg, _vsel_mask)	\
[MT6359_ID_##_name] = {						\
	.desc = {						\
		.name = #_name,					\
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

#define MT6359_LDO(match, _name, _volt_table,			\
	_enable_reg, _enable_mask, _status_reg,			\
	_vsel_reg, _vsel_mask, _en_delay)			\
[MT6359_ID_##_name] = {						\
	.desc = {						\
		.name = #_name,					\
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

#define MT6359_REG_FIXED(match, _name, _enable_reg,	\
	_status_reg, _fixed_volt)			\
[MT6359_ID_##_name] = {					\
	.desc = {					\
		.name = #_name,				\
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

#define MT6359P_LDO1(match, _name, _ops, _volt_table,	\
	_enable_reg, _enable_mask, _status_reg,		\
	_vsel_reg, _vsel_mask)				\
[MT6359_ID_##_name] = {					\
	.desc = {					\
		.name = #_name,				\
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
	struct mt6359_regulator_info *info = rdev_get_drvdata(rdev);

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
	struct mt6359_regulator_info *info = rdev_get_drvdata(rdev);
	int ret, regval;

	ret = regmap_read(rdev->regmap, info->modeset_reg, &regval);
	if (ret != 0) {
		dev_err(&rdev->dev,
			"Failed to get mt6359 buck mode: %d\n", ret);
		return ret;
	}

	if ((regval & info->modeset_mask) >> info->modeset_shift ==
		MT6359_BUCK_MODE_FORCE_PWM)
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
	struct mt6359_regulator_info *info = rdev_get_drvdata(rdev);
	int ret = 0, val;
	int curr_mode;

	curr_mode = mt6359_regulator_get_mode(rdev);
	switch (mode) {
	case REGULATOR_MODE_FAST:
		val = MT6359_BUCK_MODE_FORCE_PWM;
		val <<= info->modeset_shift;
		ret = regmap_update_bits(rdev->regmap,
					 info->modeset_reg,
					 info->modeset_mask,
					 val);
		break;
	case REGULATOR_MODE_NORMAL:
		if (curr_mode == REGULATOR_MODE_FAST) {
			val = MT6359_BUCK_MODE_AUTO;
			val <<= info->modeset_shift;
			ret = regmap_update_bits(rdev->regmap,
						 info->modeset_reg,
						 info->modeset_mask,
						 val);
		} else if (curr_mode == REGULATOR_MODE_IDLE) {
			val = MT6359_BUCK_MODE_NORMAL;
			val <<= info->lp_mode_shift;
			ret = regmap_update_bits(rdev->regmap,
						 info->lp_mode_reg,
						 info->lp_mode_mask,
						 val);
			udelay(100);
		}
		break;
	case REGULATOR_MODE_IDLE:
		val = MT6359_BUCK_MODE_LP >> 1;
		val <<= info->lp_mode_shift;
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
	struct mt6359_regulator_info *info = rdev_get_drvdata(rdev);
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
	struct mt6359_regulator_info *info = rdev_get_drvdata(rdev);
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

/* The array is indexed by id(MT6359_ID_XXX) */
static struct mt6359_regulator_info mt6359_regulators[] = {
	MT6359_BUCK("buck_vs1", VS1, 800000, 2200000, 12500,
		    MT6359_RG_BUCK_VS1_EN_ADDR,
		    MT6359_DA_VS1_EN_ADDR, MT6359_RG_BUCK_VS1_VOSEL_ADDR,
		    MT6359_RG_BUCK_VS1_VOSEL_MASK <<
		    MT6359_RG_BUCK_VS1_VOSEL_SHIFT,
		    MT6359_RG_BUCK_VS1_LP_ADDR, MT6359_RG_BUCK_VS1_LP_SHIFT,
		    MT6359_RG_VS1_FPWM_ADDR, MT6359_RG_VS1_FPWM_SHIFT),
	MT6359_BUCK("buck_vgpu11", VGPU11, 400000, 1193750, 6250,
		    MT6359_RG_BUCK_VGPU11_EN_ADDR,
		    MT6359_DA_VGPU11_EN_ADDR, MT6359_RG_BUCK_VGPU11_VOSEL_ADDR,
		    MT6359_RG_BUCK_VGPU11_VOSEL_MASK <<
		    MT6359_RG_BUCK_VGPU11_VOSEL_SHIFT,
		    MT6359_RG_BUCK_VGPU11_LP_ADDR,
		    MT6359_RG_BUCK_VGPU11_LP_SHIFT,
		    MT6359_RG_VGPU11_FCCM_ADDR, MT6359_RG_VGPU11_FCCM_SHIFT),
	MT6359_BUCK("buck_vmodem", VMODEM, 400000, 1100000, 6250,
		    MT6359_RG_BUCK_VMODEM_EN_ADDR,
		    MT6359_DA_VMODEM_EN_ADDR, MT6359_RG_BUCK_VMODEM_VOSEL_ADDR,
		    MT6359_RG_BUCK_VMODEM_VOSEL_MASK <<
		    MT6359_RG_BUCK_VMODEM_VOSEL_SHIFT,
		    MT6359_RG_BUCK_VMODEM_LP_ADDR,
		    MT6359_RG_BUCK_VMODEM_LP_SHIFT,
		    MT6359_RG_VMODEM_FCCM_ADDR, MT6359_RG_VMODEM_FCCM_SHIFT),
	MT6359_BUCK("buck_vpu", VPU, 400000, 1193750, 6250,
		    MT6359_RG_BUCK_VPU_EN_ADDR,
		    MT6359_DA_VPU_EN_ADDR, MT6359_RG_BUCK_VPU_VOSEL_ADDR,
		    MT6359_RG_BUCK_VPU_VOSEL_MASK <<
		    MT6359_RG_BUCK_VPU_VOSEL_SHIFT,
		    MT6359_RG_BUCK_VPU_LP_ADDR, MT6359_RG_BUCK_VPU_LP_SHIFT,
		    MT6359_RG_VPU_FCCM_ADDR, MT6359_RG_VPU_FCCM_SHIFT),
	MT6359_BUCK("buck_vcore", VCORE, 400000, 1193750, 6250,
		    MT6359_RG_BUCK_VCORE_EN_ADDR,
		    MT6359_DA_VCORE_EN_ADDR, MT6359_RG_BUCK_VCORE_VOSEL_ADDR,
		    MT6359_RG_BUCK_VCORE_VOSEL_MASK <<
		    MT6359_RG_BUCK_VCORE_VOSEL_SHIFT,
		    MT6359_RG_BUCK_VCORE_LP_ADDR, MT6359_RG_BUCK_VCORE_LP_SHIFT,
		    MT6359_RG_VCORE_FCCM_ADDR, MT6359_RG_VCORE_FCCM_SHIFT),
	MT6359_BUCK("buck_vs2", VS2, 800000, 1600000, 12500,
		    MT6359_RG_BUCK_VS2_EN_ADDR,
		    MT6359_DA_VS2_EN_ADDR, MT6359_RG_BUCK_VS2_VOSEL_ADDR,
		    MT6359_RG_BUCK_VS2_VOSEL_MASK <<
		    MT6359_RG_BUCK_VS2_VOSEL_SHIFT,
		    MT6359_RG_BUCK_VS2_LP_ADDR, MT6359_RG_BUCK_VS2_LP_SHIFT,
		    MT6359_RG_VS2_FPWM_ADDR, MT6359_RG_VS2_FPWM_SHIFT),
	MT6359_BUCK("buck_vpa", VPA, 500000, 3650000, 50000,
		    MT6359_RG_BUCK_VPA_EN_ADDR,
		    MT6359_DA_VPA_EN_ADDR, MT6359_RG_BUCK_VPA_VOSEL_ADDR,
		    MT6359_RG_BUCK_VPA_VOSEL_MASK <<
		    MT6359_RG_BUCK_VPA_VOSEL_SHIFT,
		    MT6359_RG_BUCK_VPA_LP_ADDR, MT6359_RG_BUCK_VPA_LP_SHIFT,
		    MT6359_RG_VPA_MODESET_ADDR, MT6359_RG_VPA_MODESET_SHIFT),
	MT6359_BUCK("buck_vproc2", VPROC2, 400000, 1193750, 6250,
		    MT6359_RG_BUCK_VPROC2_EN_ADDR,
		    MT6359_DA_VPROC2_EN_ADDR, MT6359_RG_BUCK_VPROC2_VOSEL_ADDR,
		    MT6359_RG_BUCK_VPROC2_VOSEL_MASK <<
		    MT6359_RG_BUCK_VPROC2_VOSEL_SHIFT,
		    MT6359_RG_BUCK_VPROC2_LP_ADDR,
		    MT6359_RG_BUCK_VPROC2_LP_SHIFT,
		    MT6359_RG_VPROC2_FCCM_ADDR, MT6359_RG_VPROC2_FCCM_SHIFT),
	MT6359_BUCK("buck_vproc1", VPROC1, 400000, 1193750, 6250,
		    MT6359_RG_BUCK_VPROC1_EN_ADDR,
		    MT6359_DA_VPROC1_EN_ADDR, MT6359_RG_BUCK_VPROC1_VOSEL_ADDR,
		    MT6359_RG_BUCK_VPROC1_VOSEL_MASK <<
		    MT6359_RG_BUCK_VPROC1_VOSEL_SHIFT,
		    MT6359_RG_BUCK_VPROC1_LP_ADDR,
		    MT6359_RG_BUCK_VPROC1_LP_SHIFT,
		    MT6359_RG_VPROC1_FCCM_ADDR, MT6359_RG_VPROC1_FCCM_SHIFT),
	MT6359_BUCK("buck_vcore_sshub", VCORE_SSHUB, 400000, 1193750, 6250,
		    MT6359_RG_BUCK_VCORE_SSHUB_EN_ADDR,
		    MT6359_DA_VCORE_EN_ADDR,
		    MT6359_RG_BUCK_VCORE_SSHUB_VOSEL_ADDR,
		    MT6359_RG_BUCK_VCORE_SSHUB_VOSEL_MASK <<
		    MT6359_RG_BUCK_VCORE_SSHUB_VOSEL_SHIFT,
		    MT6359_RG_BUCK_VCORE_LP_ADDR, MT6359_RG_BUCK_VCORE_LP_SHIFT,
		    MT6359_RG_VCORE_FCCM_ADDR, MT6359_RG_VCORE_FCCM_SHIFT),
	MT6359_REG_FIXED("ldo_vaud18", VAUD18, MT6359_RG_LDO_VAUD18_EN_ADDR,
			 MT6359_DA_VAUD18_B_EN_ADDR, 1800000),
	MT6359_LDO("ldo_vsim1", VSIM1, vsim1_voltages,
		   MT6359_RG_LDO_VSIM1_EN_ADDR, MT6359_RG_LDO_VSIM1_EN_SHIFT,
		   MT6359_DA_VSIM1_B_EN_ADDR, MT6359_RG_VSIM1_VOSEL_ADDR,
		   MT6359_RG_VSIM1_VOSEL_MASK << MT6359_RG_VSIM1_VOSEL_SHIFT,
		   480),
	MT6359_LDO("ldo_vibr", VIBR, vibr_voltages,
		   MT6359_RG_LDO_VIBR_EN_ADDR, MT6359_RG_LDO_VIBR_EN_SHIFT,
		   MT6359_DA_VIBR_B_EN_ADDR, MT6359_RG_VIBR_VOSEL_ADDR,
		   MT6359_RG_VIBR_VOSEL_MASK << MT6359_RG_VIBR_VOSEL_SHIFT,
		   240),
	MT6359_LDO("ldo_vrf12", VRF12, vrf12_voltages,
		   MT6359_RG_LDO_VRF12_EN_ADDR, MT6359_RG_LDO_VRF12_EN_SHIFT,
		   MT6359_DA_VRF12_B_EN_ADDR, MT6359_RG_VRF12_VOSEL_ADDR,
		   MT6359_RG_VRF12_VOSEL_MASK << MT6359_RG_VRF12_VOSEL_SHIFT,
		   120),
	MT6359_REG_FIXED("ldo_vusb", VUSB, MT6359_RG_LDO_VUSB_EN_0_ADDR,
			 MT6359_DA_VUSB_B_EN_ADDR, 3000000),
	MT6359_LDO_LINEAR("ldo_vsram_proc2", VSRAM_PROC2, 500000, 1293750, 6250,
			  MT6359_RG_LDO_VSRAM_PROC2_EN_ADDR,
			  MT6359_DA_VSRAM_PROC2_B_EN_ADDR,
			  MT6359_RG_LDO_VSRAM_PROC2_VOSEL_ADDR,
			  MT6359_RG_LDO_VSRAM_PROC2_VOSEL_MASK <<
			  MT6359_RG_LDO_VSRAM_PROC2_VOSEL_SHIFT),
	MT6359_LDO("ldo_vio18", VIO18, volt18_voltages,
		   MT6359_RG_LDO_VIO18_EN_ADDR, MT6359_RG_LDO_VIO18_EN_SHIFT,
		   MT6359_DA_VIO18_B_EN_ADDR, MT6359_RG_VIO18_VOSEL_ADDR,
		   MT6359_RG_VIO18_VOSEL_MASK << MT6359_RG_VIO18_VOSEL_SHIFT,
		   960),
	MT6359_LDO("ldo_vcamio", VCAMIO, volt18_voltages,
		   MT6359_RG_LDO_VCAMIO_EN_ADDR, MT6359_RG_LDO_VCAMIO_EN_SHIFT,
		   MT6359_DA_VCAMIO_B_EN_ADDR, MT6359_RG_VCAMIO_VOSEL_ADDR,
		   MT6359_RG_VCAMIO_VOSEL_MASK << MT6359_RG_VCAMIO_VOSEL_SHIFT,
		   1290),
	MT6359_REG_FIXED("ldo_vcn18", VCN18, MT6359_RG_LDO_VCN18_EN_ADDR,
			 MT6359_DA_VCN18_B_EN_ADDR, 1800000),
	MT6359_REG_FIXED("ldo_vfe28", VFE28, MT6359_RG_LDO_VFE28_EN_ADDR,
			 MT6359_DA_VFE28_B_EN_ADDR, 2800000),
	MT6359_LDO("ldo_vcn13", VCN13, vcn13_voltages,
		   MT6359_RG_LDO_VCN13_EN_ADDR, MT6359_RG_LDO_VCN13_EN_SHIFT,
		   MT6359_DA_VCN13_B_EN_ADDR, MT6359_RG_VCN13_VOSEL_ADDR,
		   MT6359_RG_VCN13_VOSEL_MASK << MT6359_RG_VCN13_VOSEL_SHIFT,
		   240),
	MT6359_LDO("ldo_vcn33_1_bt", VCN33_1_BT, vcn33_voltages,
		   MT6359_RG_LDO_VCN33_1_EN_0_ADDR,
		   MT6359_RG_LDO_VCN33_1_EN_0_SHIFT,
		   MT6359_DA_VCN33_1_B_EN_ADDR, MT6359_RG_VCN33_1_VOSEL_ADDR,
		   MT6359_RG_VCN33_1_VOSEL_MASK <<
		   MT6359_RG_VCN33_1_VOSEL_SHIFT, 240),
	MT6359_LDO("ldo_vcn33_1_wifi", VCN33_1_WIFI, vcn33_voltages,
		   MT6359_RG_LDO_VCN33_1_EN_1_ADDR,
		   MT6359_RG_LDO_VCN33_1_EN_1_SHIFT,
		   MT6359_DA_VCN33_1_B_EN_ADDR, MT6359_RG_VCN33_1_VOSEL_ADDR,
		   MT6359_RG_VCN33_1_VOSEL_MASK <<
		   MT6359_RG_VCN33_1_VOSEL_SHIFT, 240),
	MT6359_REG_FIXED("ldo_vaux18", VAUX18, MT6359_RG_LDO_VAUX18_EN_ADDR,
			 MT6359_DA_VAUX18_B_EN_ADDR, 1800000),
	MT6359_LDO_LINEAR("ldo_vsram_others", VSRAM_OTHERS, 500000, 1293750,
			  6250,
			  MT6359_RG_LDO_VSRAM_OTHERS_EN_ADDR,
			  MT6359_DA_VSRAM_OTHERS_B_EN_ADDR,
			  MT6359_RG_LDO_VSRAM_OTHERS_VOSEL_ADDR,
			  MT6359_RG_LDO_VSRAM_OTHERS_VOSEL_MASK <<
			  MT6359_RG_LDO_VSRAM_OTHERS_VOSEL_SHIFT),
	MT6359_LDO("ldo_vefuse", VEFUSE, vefuse_voltages,
		   MT6359_RG_LDO_VEFUSE_EN_ADDR, MT6359_RG_LDO_VEFUSE_EN_SHIFT,
		   MT6359_DA_VEFUSE_B_EN_ADDR, MT6359_RG_VEFUSE_VOSEL_ADDR,
		   MT6359_RG_VEFUSE_VOSEL_MASK << MT6359_RG_VEFUSE_VOSEL_SHIFT,
		   240),
	MT6359_LDO("ldo_vxo22", VXO22, vxo22_voltages,
		   MT6359_RG_LDO_VXO22_EN_ADDR, MT6359_RG_LDO_VXO22_EN_SHIFT,
		   MT6359_DA_VXO22_B_EN_ADDR, MT6359_RG_VXO22_VOSEL_ADDR,
		   MT6359_RG_VXO22_VOSEL_MASK << MT6359_RG_VXO22_VOSEL_SHIFT,
		   120),
	MT6359_LDO("ldo_vrfck", VRFCK, vrfck_voltages,
		   MT6359_RG_LDO_VRFCK_EN_ADDR, MT6359_RG_LDO_VRFCK_EN_SHIFT,
		   MT6359_DA_VRFCK_B_EN_ADDR, MT6359_RG_VRFCK_VOSEL_ADDR,
		   MT6359_RG_VRFCK_VOSEL_MASK << MT6359_RG_VRFCK_VOSEL_SHIFT,
		   480),
	MT6359_REG_FIXED("ldo_vbif28", VBIF28, MT6359_RG_LDO_VBIF28_EN_ADDR,
			 MT6359_DA_VBIF28_B_EN_ADDR, 2800000),
	MT6359_LDO("ldo_vio28", VIO28, vio28_voltages,
		   MT6359_RG_LDO_VIO28_EN_ADDR, MT6359_RG_LDO_VIO28_EN_SHIFT,
		   MT6359_DA_VIO28_B_EN_ADDR, MT6359_RG_VIO28_VOSEL_ADDR,
		   MT6359_RG_VIO28_VOSEL_MASK << MT6359_RG_VIO28_VOSEL_SHIFT,
		   240),
	MT6359_LDO("ldo_vemc", VEMC, vemc_voltages,
		   MT6359_RG_LDO_VEMC_EN_ADDR, MT6359_RG_LDO_VEMC_EN_SHIFT,
		   MT6359_DA_VEMC_B_EN_ADDR, MT6359_RG_VEMC_VOSEL_ADDR,
		   MT6359_RG_VEMC_VOSEL_MASK << MT6359_RG_VEMC_VOSEL_SHIFT,
		   240),
	MT6359_LDO("ldo_vcn33_2_bt", VCN33_2_BT, vcn33_voltages,
		   MT6359_RG_LDO_VCN33_2_EN_0_ADDR,
		   MT6359_RG_LDO_VCN33_2_EN_0_SHIFT,
		   MT6359_DA_VCN33_2_B_EN_ADDR, MT6359_RG_VCN33_2_VOSEL_ADDR,
		   MT6359_RG_VCN33_2_VOSEL_MASK <<
		   MT6359_RG_VCN33_2_VOSEL_SHIFT, 240),
	MT6359_LDO("ldo_vcn33_2_wifi", VCN33_2_WIFI, vcn33_voltages,
		   MT6359_RG_LDO_VCN33_2_EN_1_ADDR,
		   MT6359_RG_LDO_VCN33_2_EN_1_SHIFT,
		   MT6359_DA_VCN33_2_B_EN_ADDR, MT6359_RG_VCN33_2_VOSEL_ADDR,
		   MT6359_RG_VCN33_2_VOSEL_MASK <<
		   MT6359_RG_VCN33_2_VOSEL_SHIFT, 240),
	MT6359_LDO("ldo_va12", VA12, va12_voltages,
		   MT6359_RG_LDO_VA12_EN_ADDR, MT6359_RG_LDO_VA12_EN_SHIFT,
		   MT6359_DA_VA12_B_EN_ADDR, MT6359_RG_VA12_VOSEL_ADDR,
		   MT6359_RG_VA12_VOSEL_MASK << MT6359_RG_VA12_VOSEL_SHIFT,
		   240),
	MT6359_LDO("ldo_va09", VA09, va09_voltages,
		   MT6359_RG_LDO_VA09_EN_ADDR, MT6359_RG_LDO_VA09_EN_SHIFT,
		   MT6359_DA_VA09_B_EN_ADDR, MT6359_RG_VA09_VOSEL_ADDR,
		   MT6359_RG_VA09_VOSEL_MASK << MT6359_RG_VA09_VOSEL_SHIFT,
		   240),
	MT6359_LDO("ldo_vrf18", VRF18, vrf18_voltages,
		   MT6359_RG_LDO_VRF18_EN_ADDR, MT6359_RG_LDO_VRF18_EN_SHIFT,
		   MT6359_DA_VRF18_B_EN_ADDR, MT6359_RG_VRF18_VOSEL_ADDR,
		   MT6359_RG_VRF18_VOSEL_MASK << MT6359_RG_VRF18_VOSEL_SHIFT,
		   120),
	MT6359_LDO_LINEAR("ldo_vsram_md", VSRAM_MD, 500000, 1100000, 6250,
			  MT6359_RG_LDO_VSRAM_MD_EN_ADDR,
			  MT6359_DA_VSRAM_MD_B_EN_ADDR,
			  MT6359_RG_LDO_VSRAM_MD_VOSEL_ADDR,
			  MT6359_RG_LDO_VSRAM_MD_VOSEL_MASK <<
			  MT6359_RG_LDO_VSRAM_MD_VOSEL_SHIFT),
	MT6359_LDO("ldo_vufs", VUFS, volt18_voltages,
		   MT6359_RG_LDO_VUFS_EN_ADDR, MT6359_RG_LDO_VUFS_EN_SHIFT,
		   MT6359_DA_VUFS_B_EN_ADDR, MT6359_RG_VUFS_VOSEL_ADDR,
		   MT6359_RG_VUFS_VOSEL_MASK << MT6359_RG_VUFS_VOSEL_SHIFT,
		   1920),
	MT6359_LDO("ldo_vm18", VM18, volt18_voltages,
		   MT6359_RG_LDO_VM18_EN_ADDR, MT6359_RG_LDO_VM18_EN_SHIFT,
		   MT6359_DA_VM18_B_EN_ADDR, MT6359_RG_VM18_VOSEL_ADDR,
		   MT6359_RG_VM18_VOSEL_MASK << MT6359_RG_VM18_VOSEL_SHIFT,
		   1920),
	MT6359_LDO("ldo_vbbck", VBBCK, vbbck_voltages,
		   MT6359_RG_LDO_VBBCK_EN_ADDR, MT6359_RG_LDO_VBBCK_EN_SHIFT,
		   MT6359_DA_VBBCK_B_EN_ADDR, MT6359_RG_VBBCK_VOSEL_ADDR,
		   MT6359_RG_VBBCK_VOSEL_MASK << MT6359_RG_VBBCK_VOSEL_SHIFT,
		   240),
	MT6359_LDO_LINEAR("ldo_vsram_proc1", VSRAM_PROC1, 500000, 1293750, 6250,
			  MT6359_RG_LDO_VSRAM_PROC1_EN_ADDR,
			  MT6359_DA_VSRAM_PROC1_B_EN_ADDR,
			  MT6359_RG_LDO_VSRAM_PROC1_VOSEL_ADDR,
			  MT6359_RG_LDO_VSRAM_PROC1_VOSEL_MASK <<
			  MT6359_RG_LDO_VSRAM_PROC1_VOSEL_SHIFT),
	MT6359_LDO("ldo_vsim2", VSIM2, vsim2_voltages,
		   MT6359_RG_LDO_VSIM2_EN_ADDR, MT6359_RG_LDO_VSIM2_EN_SHIFT,
		   MT6359_DA_VSIM2_B_EN_ADDR, MT6359_RG_VSIM2_VOSEL_ADDR,
		   MT6359_RG_VSIM2_VOSEL_MASK << MT6359_RG_VSIM2_VOSEL_SHIFT,
		   480),
	MT6359_LDO_LINEAR("ldo_vsram_others_sshub", VSRAM_OTHERS_SSHUB,
			  500000, 1293750, 6250,
			  MT6359_RG_LDO_VSRAM_OTHERS_SSHUB_EN_ADDR,
			  MT6359_DA_VSRAM_OTHERS_B_EN_ADDR,
			  MT6359_RG_LDO_VSRAM_OTHERS_SSHUB_VOSEL_ADDR,
			  MT6359_RG_LDO_VSRAM_OTHERS_SSHUB_VOSEL_MASK <<
			  MT6359_RG_LDO_VSRAM_OTHERS_SSHUB_VOSEL_SHIFT),
};

static struct mt6359_regulator_info mt6359p_regulators[] = {
	MT6359_BUCK("buck_vs1", VS1, 800000, 2200000, 12500,
		    MT6359_RG_BUCK_VS1_EN_ADDR,
		    MT6359_DA_VS1_EN_ADDR, MT6359_RG_BUCK_VS1_VOSEL_ADDR,
		    MT6359_RG_BUCK_VS1_VOSEL_MASK <<
		    MT6359_RG_BUCK_VS1_VOSEL_SHIFT,
		    MT6359_RG_BUCK_VS1_LP_ADDR, MT6359_RG_BUCK_VS1_LP_SHIFT,
		    MT6359_RG_VS1_FPWM_ADDR, MT6359_RG_VS1_FPWM_SHIFT),
	MT6359_BUCK("buck_vgpu11", VGPU11, 400000, 1193750, 6250,
		    MT6359_RG_BUCK_VGPU11_EN_ADDR,
		    MT6359_DA_VGPU11_EN_ADDR, MT6359P_RG_BUCK_VGPU11_VOSEL_ADDR,
		    MT6359_RG_BUCK_VGPU11_VOSEL_MASK <<
		    MT6359_RG_BUCK_VGPU11_VOSEL_SHIFT,
		    MT6359_RG_BUCK_VGPU11_LP_ADDR,
		    MT6359_RG_BUCK_VGPU11_LP_SHIFT,
		    MT6359_RG_VGPU11_FCCM_ADDR, MT6359_RG_VGPU11_FCCM_SHIFT),
	MT6359_BUCK("buck_vmodem", VMODEM, 400000, 1100000, 6250,
		    MT6359_RG_BUCK_VMODEM_EN_ADDR,
		    MT6359_DA_VMODEM_EN_ADDR, MT6359_RG_BUCK_VMODEM_VOSEL_ADDR,
		    MT6359_RG_BUCK_VMODEM_VOSEL_MASK <<
		    MT6359_RG_BUCK_VMODEM_VOSEL_SHIFT,
		    MT6359_RG_BUCK_VMODEM_LP_ADDR,
		    MT6359_RG_BUCK_VMODEM_LP_SHIFT,
		    MT6359_RG_VMODEM_FCCM_ADDR, MT6359_RG_VMODEM_FCCM_SHIFT),
	MT6359_BUCK("buck_vpu", VPU, 400000, 1193750, 6250,
		    MT6359_RG_BUCK_VPU_EN_ADDR,
		    MT6359_DA_VPU_EN_ADDR, MT6359_RG_BUCK_VPU_VOSEL_ADDR,
		    MT6359_RG_BUCK_VPU_VOSEL_MASK <<
		    MT6359_RG_BUCK_VPU_VOSEL_SHIFT,
		    MT6359_RG_BUCK_VPU_LP_ADDR, MT6359_RG_BUCK_VPU_LP_SHIFT,
		    MT6359_RG_VPU_FCCM_ADDR, MT6359_RG_VPU_FCCM_SHIFT),
	MT6359_BUCK("buck_vcore", VCORE, 506250, 1300000, 6250,
		    MT6359_RG_BUCK_VCORE_EN_ADDR,
		    MT6359_DA_VCORE_EN_ADDR, MT6359P_RG_BUCK_VCORE_VOSEL_ADDR,
		    MT6359_RG_BUCK_VCORE_VOSEL_MASK <<
		    MT6359_RG_BUCK_VCORE_VOSEL_SHIFT,
		    MT6359_RG_BUCK_VCORE_LP_ADDR, MT6359_RG_BUCK_VCORE_LP_SHIFT,
		    MT6359_RG_VCORE_FCCM_ADDR, MT6359_RG_VCORE_FCCM_SHIFT),
	MT6359_BUCK("buck_vs2", VS2, 800000, 1600000, 12500,
		    MT6359_RG_BUCK_VS2_EN_ADDR,
		    MT6359_DA_VS2_EN_ADDR, MT6359_RG_BUCK_VS2_VOSEL_ADDR,
		    MT6359_RG_BUCK_VS2_VOSEL_MASK <<
		    MT6359_RG_BUCK_VS2_VOSEL_SHIFT,
		    MT6359_RG_BUCK_VS2_LP_ADDR, MT6359_RG_BUCK_VS2_LP_SHIFT,
		    MT6359_RG_VS2_FPWM_ADDR, MT6359_RG_VS2_FPWM_SHIFT),
	MT6359_BUCK("buck_vpa", VPA, 500000, 3650000, 50000,
		    MT6359_RG_BUCK_VPA_EN_ADDR,
		    MT6359_DA_VPA_EN_ADDR, MT6359_RG_BUCK_VPA_VOSEL_ADDR,
		    MT6359_RG_BUCK_VPA_VOSEL_MASK <<
		    MT6359_RG_BUCK_VPA_VOSEL_SHIFT,
		    MT6359_RG_BUCK_VPA_LP_ADDR, MT6359_RG_BUCK_VPA_LP_SHIFT,
		    MT6359_RG_VPA_MODESET_ADDR, MT6359_RG_VPA_MODESET_SHIFT),
	MT6359_BUCK("buck_vproc2", VPROC2, 400000, 1193750, 6250,
		    MT6359_RG_BUCK_VPROC2_EN_ADDR,
		    MT6359_DA_VPROC2_EN_ADDR, MT6359_RG_BUCK_VPROC2_VOSEL_ADDR,
		    MT6359_RG_BUCK_VPROC2_VOSEL_MASK <<
		    MT6359_RG_BUCK_VPROC2_VOSEL_SHIFT,
		    MT6359_RG_BUCK_VPROC2_LP_ADDR,
		    MT6359_RG_BUCK_VPROC2_LP_SHIFT,
		    MT6359_RG_VPROC2_FCCM_ADDR, MT6359_RG_VPROC2_FCCM_SHIFT),
	MT6359_BUCK("buck_vproc1", VPROC1, 400000, 1193750, 6250,
		    MT6359_RG_BUCK_VPROC1_EN_ADDR,
		    MT6359_DA_VPROC1_EN_ADDR, MT6359_RG_BUCK_VPROC1_VOSEL_ADDR,
		    MT6359_RG_BUCK_VPROC1_VOSEL_MASK <<
		    MT6359_RG_BUCK_VPROC1_VOSEL_SHIFT,
		    MT6359_RG_BUCK_VPROC1_LP_ADDR,
		    MT6359_RG_BUCK_VPROC1_LP_SHIFT,
		    MT6359_RG_VPROC1_FCCM_ADDR, MT6359_RG_VPROC1_FCCM_SHIFT),
	MT6359_BUCK("buck_vgpu11_sshub", VGPU11_SSHUB, 400000, 1193750, 6250,
		    MT6359P_RG_BUCK_VGPU11_SSHUB_EN_ADDR,
		    MT6359_DA_VGPU11_EN_ADDR,
		    MT6359P_RG_BUCK_VGPU11_SSHUB_VOSEL_ADDR,
		    MT6359P_RG_BUCK_VGPU11_SSHUB_VOSEL_MASK <<
		    MT6359P_RG_BUCK_VGPU11_SSHUB_VOSEL_SHIFT,
		    MT6359_RG_BUCK_VGPU11_LP_ADDR,
		    MT6359_RG_BUCK_VGPU11_LP_SHIFT,
		    MT6359_RG_VGPU11_FCCM_ADDR, MT6359_RG_VGPU11_FCCM_SHIFT),
	MT6359_REG_FIXED("ldo_vaud18", VAUD18, MT6359P_RG_LDO_VAUD18_EN_ADDR,
			 MT6359P_DA_VAUD18_B_EN_ADDR, 1800000),
	MT6359_LDO("ldo_vsim1", VSIM1, vsim1_voltages,
		   MT6359P_RG_LDO_VSIM1_EN_ADDR, MT6359P_RG_LDO_VSIM1_EN_SHIFT,
		   MT6359P_DA_VSIM1_B_EN_ADDR, MT6359P_RG_VSIM1_VOSEL_ADDR,
		   MT6359_RG_VSIM1_VOSEL_MASK << MT6359_RG_VSIM1_VOSEL_SHIFT,
		   480),
	MT6359_LDO("ldo_vibr", VIBR, vibr_voltages,
		   MT6359P_RG_LDO_VIBR_EN_ADDR, MT6359P_RG_LDO_VIBR_EN_SHIFT,
		   MT6359P_DA_VIBR_B_EN_ADDR, MT6359P_RG_VIBR_VOSEL_ADDR,
		   MT6359_RG_VIBR_VOSEL_MASK << MT6359_RG_VIBR_VOSEL_SHIFT,
		   240),
	MT6359_LDO("ldo_vrf12", VRF12, vrf12_voltages,
		   MT6359P_RG_LDO_VRF12_EN_ADDR, MT6359P_RG_LDO_VRF12_EN_SHIFT,
		   MT6359P_DA_VRF12_B_EN_ADDR, MT6359P_RG_VRF12_VOSEL_ADDR,
		   MT6359_RG_VRF12_VOSEL_MASK << MT6359_RG_VRF12_VOSEL_SHIFT,
		   480),
	MT6359_REG_FIXED("ldo_vusb", VUSB, MT6359P_RG_LDO_VUSB_EN_0_ADDR,
			 MT6359P_DA_VUSB_B_EN_ADDR, 3000000),
	MT6359_LDO_LINEAR("ldo_vsram_proc2", VSRAM_PROC2, 500000, 1293750, 6250,
			  MT6359P_RG_LDO_VSRAM_PROC2_EN_ADDR,
			  MT6359P_DA_VSRAM_PROC2_B_EN_ADDR,
			  MT6359P_RG_LDO_VSRAM_PROC2_VOSEL_ADDR,
			  MT6359_RG_LDO_VSRAM_PROC2_VOSEL_MASK <<
			  MT6359_RG_LDO_VSRAM_PROC2_VOSEL_SHIFT),
	MT6359_LDO("ldo_vio18", VIO18, volt18_voltages,
		   MT6359P_RG_LDO_VIO18_EN_ADDR, MT6359P_RG_LDO_VIO18_EN_SHIFT,
		   MT6359P_DA_VIO18_B_EN_ADDR, MT6359P_RG_VIO18_VOSEL_ADDR,
		   MT6359_RG_VIO18_VOSEL_MASK << MT6359_RG_VIO18_VOSEL_SHIFT,
		   960),
	MT6359_LDO("ldo_vcamio", VCAMIO, volt18_voltages,
		   MT6359P_RG_LDO_VCAMIO_EN_ADDR,
		   MT6359P_RG_LDO_VCAMIO_EN_SHIFT,
		   MT6359P_DA_VCAMIO_B_EN_ADDR, MT6359P_RG_VCAMIO_VOSEL_ADDR,
		   MT6359_RG_VCAMIO_VOSEL_MASK << MT6359_RG_VCAMIO_VOSEL_SHIFT,
		   1290),
	MT6359_REG_FIXED("ldo_vcn18", VCN18, MT6359P_RG_LDO_VCN18_EN_ADDR,
			 MT6359P_DA_VCN18_B_EN_ADDR, 1800000),
	MT6359_REG_FIXED("ldo_vfe28", VFE28, MT6359P_RG_LDO_VFE28_EN_ADDR,
			 MT6359P_DA_VFE28_B_EN_ADDR, 2800000),
	MT6359_LDO("ldo_vcn13", VCN13, vcn13_voltages,
		   MT6359P_RG_LDO_VCN13_EN_ADDR, MT6359P_RG_LDO_VCN13_EN_SHIFT,
		   MT6359P_DA_VCN13_B_EN_ADDR, MT6359P_RG_VCN13_VOSEL_ADDR,
		   MT6359_RG_VCN13_VOSEL_MASK << MT6359_RG_VCN13_VOSEL_SHIFT,
		   240),
	MT6359_LDO("ldo_vcn33_1_bt", VCN33_1_BT, vcn33_voltages,
		   MT6359P_RG_LDO_VCN33_1_EN_0_ADDR,
		   MT6359_RG_LDO_VCN33_1_EN_0_SHIFT,
		   MT6359P_DA_VCN33_1_B_EN_ADDR, MT6359P_RG_VCN33_1_VOSEL_ADDR,
		   MT6359_RG_VCN33_1_VOSEL_MASK <<
		   MT6359_RG_VCN33_1_VOSEL_SHIFT, 240),
	MT6359_LDO("ldo_vcn33_1_wifi", VCN33_1_WIFI, vcn33_voltages,
		   MT6359P_RG_LDO_VCN33_1_EN_1_ADDR,
		   MT6359P_RG_LDO_VCN33_1_EN_1_SHIFT,
		   MT6359P_DA_VCN33_1_B_EN_ADDR, MT6359P_RG_VCN33_1_VOSEL_ADDR,
		   MT6359_RG_VCN33_1_VOSEL_MASK <<
		   MT6359_RG_VCN33_1_VOSEL_SHIFT, 240),
	MT6359_REG_FIXED("ldo_vaux18", VAUX18, MT6359P_RG_LDO_VAUX18_EN_ADDR,
			 MT6359P_DA_VAUX18_B_EN_ADDR, 1800000),
	MT6359_LDO_LINEAR("ldo_vsram_others", VSRAM_OTHERS, 500000, 1293750,
			  6250,
			  MT6359P_RG_LDO_VSRAM_OTHERS_EN_ADDR,
			  MT6359P_DA_VSRAM_OTHERS_B_EN_ADDR,
			  MT6359P_RG_LDO_VSRAM_OTHERS_VOSEL_ADDR,
			  MT6359_RG_LDO_VSRAM_OTHERS_VOSEL_MASK <<
			  MT6359_RG_LDO_VSRAM_OTHERS_VOSEL_SHIFT),
	MT6359_LDO("ldo_vefuse", VEFUSE, vefuse_voltages,
		   MT6359P_RG_LDO_VEFUSE_EN_ADDR,
		   MT6359P_RG_LDO_VEFUSE_EN_SHIFT,
		   MT6359P_DA_VEFUSE_B_EN_ADDR, MT6359P_RG_VEFUSE_VOSEL_ADDR,
		   MT6359_RG_VEFUSE_VOSEL_MASK << MT6359_RG_VEFUSE_VOSEL_SHIFT,
		   240),
	MT6359_LDO("ldo_vxo22", VXO22, vxo22_voltages,
		   MT6359P_RG_LDO_VXO22_EN_ADDR, MT6359P_RG_LDO_VXO22_EN_SHIFT,
		   MT6359P_DA_VXO22_B_EN_ADDR, MT6359P_RG_VXO22_VOSEL_ADDR,
		   MT6359_RG_VXO22_VOSEL_MASK << MT6359_RG_VXO22_VOSEL_SHIFT,
		   480),
	MT6359_LDO("ldo_vrfck_1", VRFCK, vrfck_voltages_1,
		   MT6359P_RG_LDO_VRFCK_EN_ADDR, MT6359P_RG_LDO_VRFCK_EN_SHIFT,
		   MT6359P_DA_VRFCK_B_EN_ADDR, MT6359P_RG_VRFCK_VOSEL_ADDR,
		   MT6359_RG_VRFCK_VOSEL_MASK << MT6359_RG_VRFCK_VOSEL_SHIFT,
		   480),
	MT6359_REG_FIXED("ldo_vbif28", VBIF28, MT6359P_RG_LDO_VBIF28_EN_ADDR,
			 MT6359P_DA_VBIF28_B_EN_ADDR, 2800000),
	MT6359_LDO("ldo_vio28", VIO28, vio28_voltages,
		   MT6359P_RG_LDO_VIO28_EN_ADDR, MT6359P_RG_LDO_VIO28_EN_SHIFT,
		   MT6359P_DA_VIO28_B_EN_ADDR, MT6359P_RG_VIO28_VOSEL_ADDR,
		   MT6359_RG_VIO28_VOSEL_MASK << MT6359_RG_VIO28_VOSEL_SHIFT,
		   1920),
	MT6359P_LDO1("ldo_vemc_1", VEMC, mt6359p_vemc_ops, vemc_voltages_1,
		     MT6359P_RG_LDO_VEMC_EN_ADDR, MT6359P_RG_LDO_VEMC_EN_SHIFT,
		     MT6359P_DA_VEMC_B_EN_ADDR,
		     MT6359P_RG_LDO_VEMC_VOSEL_0_ADDR,
		     MT6359P_RG_LDO_VEMC_VOSEL_0_MASK <<
		     MT6359P_RG_LDO_VEMC_VOSEL_0_SHIFT),
	MT6359_LDO("ldo_vcn33_2_bt", VCN33_2_BT, vcn33_voltages,
		   MT6359P_RG_LDO_VCN33_2_EN_0_ADDR,
		   MT6359P_RG_LDO_VCN33_2_EN_0_SHIFT,
		   MT6359P_DA_VCN33_2_B_EN_ADDR, MT6359P_RG_VCN33_2_VOSEL_ADDR,
		   MT6359_RG_VCN33_2_VOSEL_MASK <<
		   MT6359_RG_VCN33_2_VOSEL_SHIFT, 240),
	MT6359_LDO("ldo_vcn33_2_wifi", VCN33_2_WIFI, vcn33_voltages,
		   MT6359P_RG_LDO_VCN33_2_EN_1_ADDR,
		   MT6359_RG_LDO_VCN33_2_EN_1_SHIFT,
		   MT6359P_DA_VCN33_2_B_EN_ADDR, MT6359P_RG_VCN33_2_VOSEL_ADDR,
		   MT6359_RG_VCN33_2_VOSEL_MASK <<
		   MT6359_RG_VCN33_2_VOSEL_SHIFT, 240),
	MT6359_LDO("ldo_va12", VA12, va12_voltages,
		   MT6359P_RG_LDO_VA12_EN_ADDR, MT6359P_RG_LDO_VA12_EN_SHIFT,
		   MT6359P_DA_VA12_B_EN_ADDR, MT6359P_RG_VA12_VOSEL_ADDR,
		   MT6359_RG_VA12_VOSEL_MASK << MT6359_RG_VA12_VOSEL_SHIFT,
		   960),
	MT6359_LDO("ldo_va09", VA09, va09_voltages,
		   MT6359P_RG_LDO_VA09_EN_ADDR, MT6359P_RG_LDO_VA09_EN_SHIFT,
		   MT6359P_DA_VA09_B_EN_ADDR, MT6359P_RG_VA09_VOSEL_ADDR,
		   MT6359_RG_VA09_VOSEL_MASK << MT6359_RG_VA09_VOSEL_SHIFT,
		   960),
	MT6359_LDO("ldo_vrf18", VRF18, vrf18_voltages,
		   MT6359P_RG_LDO_VRF18_EN_ADDR, MT6359P_RG_LDO_VRF18_EN_SHIFT,
		   MT6359P_DA_VRF18_B_EN_ADDR, MT6359P_RG_VRF18_VOSEL_ADDR,
		   MT6359_RG_VRF18_VOSEL_MASK << MT6359_RG_VRF18_VOSEL_SHIFT,
		   240),
	MT6359_LDO_LINEAR("ldo_vsram_md", VSRAM_MD, 500000, 1293750, 6250,
			  MT6359P_RG_LDO_VSRAM_MD_EN_ADDR,
			  MT6359P_DA_VSRAM_MD_B_EN_ADDR,
			  MT6359P_RG_LDO_VSRAM_MD_VOSEL_ADDR,
			  MT6359_RG_LDO_VSRAM_MD_VOSEL_MASK <<
			  MT6359_RG_LDO_VSRAM_MD_VOSEL_SHIFT),
	MT6359_LDO("ldo_vufs", VUFS, volt18_voltages,
		   MT6359P_RG_LDO_VUFS_EN_ADDR, MT6359P_RG_LDO_VUFS_EN_SHIFT,
		   MT6359P_DA_VUFS_B_EN_ADDR, MT6359P_RG_VUFS_VOSEL_ADDR,
		   MT6359_RG_VUFS_VOSEL_MASK << MT6359_RG_VUFS_VOSEL_SHIFT,
		   1920),
	MT6359_LDO("ldo_vm18", VM18, volt18_voltages,
		   MT6359P_RG_LDO_VM18_EN_ADDR, MT6359P_RG_LDO_VM18_EN_SHIFT,
		   MT6359P_DA_VM18_B_EN_ADDR, MT6359P_RG_VM18_VOSEL_ADDR,
		   MT6359_RG_VM18_VOSEL_MASK << MT6359_RG_VM18_VOSEL_SHIFT,
		   1920),
	MT6359_LDO("ldo_vbbck", VBBCK, vbbck_voltages,
		   MT6359P_RG_LDO_VBBCK_EN_ADDR, MT6359P_RG_LDO_VBBCK_EN_SHIFT,
		   MT6359P_DA_VBBCK_B_EN_ADDR, MT6359P_RG_VBBCK_VOSEL_ADDR,
		   MT6359P_RG_VBBCK_VOSEL_MASK << MT6359P_RG_VBBCK_VOSEL_SHIFT,
		   480),
	MT6359_LDO_LINEAR("ldo_vsram_proc1", VSRAM_PROC1, 500000, 1293750, 6250,
			  MT6359P_RG_LDO_VSRAM_PROC1_EN_ADDR,
			  MT6359P_DA_VSRAM_PROC1_B_EN_ADDR,
			  MT6359P_RG_LDO_VSRAM_PROC1_VOSEL_ADDR,
			  MT6359_RG_LDO_VSRAM_PROC1_VOSEL_MASK <<
			  MT6359_RG_LDO_VSRAM_PROC1_VOSEL_SHIFT),
	MT6359_LDO("ldo_vsim2", VSIM2, vsim2_voltages,
		   MT6359P_RG_LDO_VSIM2_EN_ADDR, MT6359P_RG_LDO_VSIM2_EN_SHIFT,
		   MT6359P_DA_VSIM2_B_EN_ADDR, MT6359P_RG_VSIM2_VOSEL_ADDR,
		   MT6359_RG_VSIM2_VOSEL_MASK << MT6359_RG_VSIM2_VOSEL_SHIFT,
		   480),
	MT6359_LDO_LINEAR("ldo_vsram_others_sshub", VSRAM_OTHERS_SSHUB,
			  500000, 1293750, 6250,
			  MT6359P_RG_LDO_VSRAM_OTHERS_SSHUB_EN_ADDR,
			  MT6359P_DA_VSRAM_OTHERS_B_EN_ADDR,
			  MT6359P_RG_LDO_VSRAM_OTHERS_SSHUB_VOSEL_ADDR,
			  MT6359_RG_LDO_VSRAM_OTHERS_SSHUB_VOSEL_MASK <<
			  MT6359_RG_LDO_VSRAM_OTHERS_SSHUB_VOSEL_SHIFT),
};

static int mt6359_regulator_probe(struct platform_device *pdev)
{
	struct mt6397_chip *mt6397 = dev_get_drvdata(pdev->dev.parent);
	struct regulator_config config = {};
	struct regulator_dev *rdev;
	struct mt6359_regulator_info *mt6359_info;
	int i, hw_ver;

	regmap_read(mt6397->regmap, MT6359P_HWCID, &hw_ver);
	if (hw_ver >= MT6359P_CHIP_VER)
		mt6359_info = mt6359p_regulators;
	else
		mt6359_info = mt6359_regulators;

	config.dev = mt6397->dev;
	config.regmap = mt6397->regmap;
	for (i = 0; i < MT6359_MAX_REGULATOR; i++, mt6359_info++) {
		config.driver_data = mt6359_info;
		rdev = devm_regulator_register(&pdev->dev, &mt6359_info->desc, &config);
		if (IS_ERR(rdev)) {
			dev_err(&pdev->dev, "failed to register %s\n", mt6359_info->desc.name);
			return PTR_ERR(rdev);
		}
	}

	return 0;
}

static const struct platform_device_id mt6359_platform_ids[] = {
	{"mt6359-regulator", 0},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(platform, mt6359_platform_ids);

static struct platform_driver mt6359_regulator_driver = {
	.driver = {
		.name = "mt6359-regulator",
	},
	.probe = mt6359_regulator_probe,
	.id_table = mt6359_platform_ids,
};

module_platform_driver(mt6359_regulator_driver);

MODULE_AUTHOR("Wen Su <wen.su@mediatek.com>");
MODULE_DESCRIPTION("Regulator Driver for MediaTek MT6359 PMIC");
MODULE_LICENSE("GPL");
