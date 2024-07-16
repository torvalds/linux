// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2014 MediaTek Inc.
// Author: Flora Fu <flora.fu@mediatek.com>

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/mfd/mt6397/core.h>
#include <linux/mfd/mt6397/registers.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/mt6397-regulator.h>
#include <linux/regulator/of_regulator.h>
#include <dt-bindings/regulator/mediatek,mt6397-regulator.h>

/*
 * MT6397 regulators' information
 *
 * @desc: standard fields of regulator description.
 * @qi: Mask for query enable signal status of regulators
 * @vselon_reg: Register sections for hardware control mode of bucks
 * @vselctrl_reg: Register for controlling the buck control mode.
 * @vselctrl_mask: Mask for query buck's voltage control mode.
 */
struct mt6397_regulator_info {
	struct regulator_desc desc;
	u32 qi;
	u32 vselon_reg;
	u32 vselctrl_reg;
	u32 vselctrl_mask;
	u32 modeset_reg;
	u32 modeset_mask;
};

#define MT6397_BUCK(match, vreg, min, max, step, volt_ranges, enreg,	\
		vosel, vosel_mask, voselon, vosel_ctrl, _modeset_reg,	\
		_modeset_shift)					\
[MT6397_ID_##vreg] = {							\
	.desc = {							\
		.name = #vreg,						\
		.of_match = of_match_ptr(match),			\
		.ops = &mt6397_volt_range_ops,				\
		.type = REGULATOR_VOLTAGE,				\
		.id = MT6397_ID_##vreg,					\
		.owner = THIS_MODULE,					\
		.n_voltages = (max - min)/step + 1,			\
		.linear_ranges = volt_ranges,				\
		.n_linear_ranges = ARRAY_SIZE(volt_ranges),		\
		.vsel_reg = vosel,					\
		.vsel_mask = vosel_mask,				\
		.enable_reg = enreg,					\
		.enable_mask = BIT(0),					\
		.of_map_mode = mt6397_map_mode,				\
	},								\
	.qi = BIT(13),							\
	.vselon_reg = voselon,						\
	.vselctrl_reg = vosel_ctrl,					\
	.vselctrl_mask = BIT(1),					\
	.modeset_reg = _modeset_reg,					\
	.modeset_mask = BIT(_modeset_shift),				\
}

#define MT6397_LDO(match, vreg, ldo_volt_table, enreg, enbit, vosel,	\
		vosel_mask)						\
[MT6397_ID_##vreg] = {							\
	.desc = {							\
		.name = #vreg,						\
		.of_match = of_match_ptr(match),			\
		.ops = &mt6397_volt_table_ops,				\
		.type = REGULATOR_VOLTAGE,				\
		.id = MT6397_ID_##vreg,					\
		.owner = THIS_MODULE,					\
		.n_voltages = ARRAY_SIZE(ldo_volt_table),		\
		.volt_table = ldo_volt_table,				\
		.vsel_reg = vosel,					\
		.vsel_mask = vosel_mask,				\
		.enable_reg = enreg,					\
		.enable_mask = BIT(enbit),				\
	},								\
	.qi = BIT(15),							\
}

#define MT6397_REG_FIXED(match, vreg, enreg, enbit, volt)		\
[MT6397_ID_##vreg] = {							\
	.desc = {							\
		.name = #vreg,						\
		.of_match = of_match_ptr(match),			\
		.ops = &mt6397_volt_fixed_ops,				\
		.type = REGULATOR_VOLTAGE,				\
		.id = MT6397_ID_##vreg,					\
		.owner = THIS_MODULE,					\
		.n_voltages = 1,					\
		.enable_reg = enreg,					\
		.enable_mask = BIT(enbit),				\
		.min_uV = volt,						\
	},								\
	.qi = BIT(15),							\
}

static const struct linear_range buck_volt_range1[] = {
	REGULATOR_LINEAR_RANGE(700000, 0, 0x7f, 6250),
};

static const struct linear_range buck_volt_range2[] = {
	REGULATOR_LINEAR_RANGE(800000, 0, 0x7f, 6250),
};

static const struct linear_range buck_volt_range3[] = {
	REGULATOR_LINEAR_RANGE(1500000, 0, 0x1f, 20000),
};

static const unsigned int ldo_volt_table1[] = {
	1500000, 1800000, 2500000, 2800000,
};

static const unsigned int ldo_volt_table2[] = {
	1800000, 3300000,
};

static const unsigned int ldo_volt_table3[] = {
	3000000, 3300000,
};

static const unsigned int ldo_volt_table4[] = {
	1220000, 1300000, 1500000, 1800000, 2500000, 2800000, 3000000, 3300000,
};

static const unsigned int ldo_volt_table5[] = {
	1200000, 1300000, 1500000, 1800000, 2500000, 2800000, 3000000, 3300000,
};

static const unsigned int ldo_volt_table5_v2[] = {
	1200000, 1000000, 1500000, 1800000, 2500000, 2800000, 3000000, 3300000,
};

static const unsigned int ldo_volt_table6[] = {
	1200000, 1300000, 1500000, 1800000, 2500000, 2800000, 3000000, 2000000,
};

static const unsigned int ldo_volt_table7[] = {
	1300000, 1500000, 1800000, 2000000, 2500000, 2800000, 3000000, 3300000,
};

static unsigned int mt6397_map_mode(unsigned int mode)
{
	switch (mode) {
	case MT6397_BUCK_MODE_AUTO:
		return REGULATOR_MODE_NORMAL;
	case MT6397_BUCK_MODE_FORCE_PWM:
		return REGULATOR_MODE_FAST;
	default:
		return REGULATOR_MODE_INVALID;
	}
}

static int mt6397_regulator_set_mode(struct regulator_dev *rdev,
				     unsigned int mode)
{
	struct mt6397_regulator_info *info = rdev_get_drvdata(rdev);
	int ret, val;

	switch (mode) {
	case REGULATOR_MODE_FAST:
		val = MT6397_BUCK_MODE_FORCE_PWM;
		break;
	case REGULATOR_MODE_NORMAL:
		val = MT6397_BUCK_MODE_AUTO;
		break;
	default:
		ret = -EINVAL;
		goto err_mode;
	}

	dev_dbg(&rdev->dev, "mt6397 buck set_mode %#x, %#x, %#x\n",
		info->modeset_reg, info->modeset_mask, val);

	val <<= ffs(info->modeset_mask) - 1;

	ret = regmap_update_bits(rdev->regmap, info->modeset_reg,
				 info->modeset_mask, val);
err_mode:
	if (ret != 0) {
		dev_err(&rdev->dev,
			"Failed to set mt6397 buck mode: %d\n", ret);
		return ret;
	}

	return 0;
}

static unsigned int mt6397_regulator_get_mode(struct regulator_dev *rdev)
{
	struct mt6397_regulator_info *info = rdev_get_drvdata(rdev);
	int ret, regval;

	ret = regmap_read(rdev->regmap, info->modeset_reg, &regval);
	if (ret != 0) {
		dev_err(&rdev->dev,
			"Failed to get mt6397 buck mode: %d\n", ret);
		return ret;
	}

	regval &= info->modeset_mask;
	regval >>= ffs(info->modeset_mask) - 1;

	switch (regval) {
	case MT6397_BUCK_MODE_AUTO:
		return REGULATOR_MODE_NORMAL;
	case MT6397_BUCK_MODE_FORCE_PWM:
		return REGULATOR_MODE_FAST;
	default:
		return -EINVAL;
	}
}

static int mt6397_get_status(struct regulator_dev *rdev)
{
	int ret;
	u32 regval;
	struct mt6397_regulator_info *info = rdev_get_drvdata(rdev);

	ret = regmap_read(rdev->regmap, info->desc.enable_reg, &regval);
	if (ret != 0) {
		dev_err(&rdev->dev, "Failed to get enable reg: %d\n", ret);
		return ret;
	}

	return (regval & info->qi) ? REGULATOR_STATUS_ON : REGULATOR_STATUS_OFF;
}

static const struct regulator_ops mt6397_volt_range_ops = {
	.list_voltage = regulator_list_voltage_linear_range,
	.map_voltage = regulator_map_voltage_linear_range,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.get_status = mt6397_get_status,
	.set_mode = mt6397_regulator_set_mode,
	.get_mode = mt6397_regulator_get_mode,
};

static const struct regulator_ops mt6397_volt_table_ops = {
	.list_voltage = regulator_list_voltage_table,
	.map_voltage = regulator_map_voltage_iterate,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.get_status = mt6397_get_status,
};

static const struct regulator_ops mt6397_volt_fixed_ops = {
	.list_voltage = regulator_list_voltage_linear,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.get_status = mt6397_get_status,
};

/* The array is indexed by id(MT6397_ID_XXX) */
static struct mt6397_regulator_info mt6397_regulators[] = {
	MT6397_BUCK("buck_vpca15", VPCA15, 700000, 1493750, 6250,
		buck_volt_range1, MT6397_VCA15_CON7, MT6397_VCA15_CON9, 0x7f,
		MT6397_VCA15_CON10, MT6397_VCA15_CON5, MT6397_VCA15_CON2, 11),
	MT6397_BUCK("buck_vpca7", VPCA7, 700000, 1493750, 6250,
		buck_volt_range1, MT6397_VPCA7_CON7, MT6397_VPCA7_CON9, 0x7f,
		MT6397_VPCA7_CON10, MT6397_VPCA7_CON5, MT6397_VPCA7_CON2, 8),
	MT6397_BUCK("buck_vsramca15", VSRAMCA15, 700000, 1493750, 6250,
		buck_volt_range1, MT6397_VSRMCA15_CON7, MT6397_VSRMCA15_CON9,
		0x7f, MT6397_VSRMCA15_CON10, MT6397_VSRMCA15_CON5,
		MT6397_VSRMCA15_CON2, 8),
	MT6397_BUCK("buck_vsramca7", VSRAMCA7, 700000, 1493750, 6250,
		buck_volt_range1, MT6397_VSRMCA7_CON7, MT6397_VSRMCA7_CON9,
		0x7f, MT6397_VSRMCA7_CON10, MT6397_VSRMCA7_CON5,
		MT6397_VSRMCA7_CON2, 8),
	MT6397_BUCK("buck_vcore", VCORE, 700000, 1493750, 6250,
		buck_volt_range1, MT6397_VCORE_CON7, MT6397_VCORE_CON9, 0x7f,
		MT6397_VCORE_CON10, MT6397_VCORE_CON5, MT6397_VCORE_CON2, 8),
	MT6397_BUCK("buck_vgpu", VGPU, 700000, 1493750, 6250, buck_volt_range1,
		MT6397_VGPU_CON7, MT6397_VGPU_CON9, 0x7f,
		MT6397_VGPU_CON10, MT6397_VGPU_CON5, MT6397_VGPU_CON2, 8),
	MT6397_BUCK("buck_vdrm", VDRM, 800000, 1593750, 6250, buck_volt_range2,
		MT6397_VDRM_CON7, MT6397_VDRM_CON9, 0x7f,
		MT6397_VDRM_CON10, MT6397_VDRM_CON5, MT6397_VDRM_CON2, 8),
	MT6397_BUCK("buck_vio18", VIO18, 1500000, 2120000, 20000,
		buck_volt_range3, MT6397_VIO18_CON7, MT6397_VIO18_CON9, 0x1f,
		MT6397_VIO18_CON10, MT6397_VIO18_CON5, MT6397_VIO18_CON2, 8),
	MT6397_REG_FIXED("ldo_vtcxo", VTCXO, MT6397_ANALDO_CON0, 10, 2800000),
	MT6397_REG_FIXED("ldo_va28", VA28, MT6397_ANALDO_CON1, 14, 2800000),
	MT6397_LDO("ldo_vcama", VCAMA, ldo_volt_table1,
		MT6397_ANALDO_CON2, 15, MT6397_ANALDO_CON6, 0xC0),
	MT6397_REG_FIXED("ldo_vio28", VIO28, MT6397_DIGLDO_CON0, 14, 2800000),
	MT6397_REG_FIXED("ldo_vusb", VUSB, MT6397_DIGLDO_CON1, 14, 3300000),
	MT6397_LDO("ldo_vmc", VMC, ldo_volt_table2,
		MT6397_DIGLDO_CON2, 12, MT6397_DIGLDO_CON29, 0x10),
	MT6397_LDO("ldo_vmch", VMCH, ldo_volt_table3,
		MT6397_DIGLDO_CON3, 14, MT6397_DIGLDO_CON17, 0x80),
	MT6397_LDO("ldo_vemc3v3", VEMC3V3, ldo_volt_table3,
		MT6397_DIGLDO_CON4, 14, MT6397_DIGLDO_CON18, 0x10),
	MT6397_LDO("ldo_vgp1", VGP1, ldo_volt_table4,
		MT6397_DIGLDO_CON5, 15, MT6397_DIGLDO_CON19, 0xE0),
	MT6397_LDO("ldo_vgp2", VGP2, ldo_volt_table5,
		MT6397_DIGLDO_CON6, 15, MT6397_DIGLDO_CON20, 0xE0),
	MT6397_LDO("ldo_vgp3", VGP3, ldo_volt_table5,
		MT6397_DIGLDO_CON7, 15, MT6397_DIGLDO_CON21, 0xE0),
	MT6397_LDO("ldo_vgp4", VGP4, ldo_volt_table5,
		MT6397_DIGLDO_CON8, 15, MT6397_DIGLDO_CON22, 0xE0),
	MT6397_LDO("ldo_vgp5", VGP5, ldo_volt_table6,
		MT6397_DIGLDO_CON9, 15, MT6397_DIGLDO_CON23, 0xE0),
	MT6397_LDO("ldo_vgp6", VGP6, ldo_volt_table5,
		MT6397_DIGLDO_CON10, 15, MT6397_DIGLDO_CON33, 0xE0),
	MT6397_LDO("ldo_vibr", VIBR, ldo_volt_table7,
		MT6397_DIGLDO_CON24, 15, MT6397_DIGLDO_CON25, 0xE00),
};

static int mt6397_set_buck_vosel_reg(struct platform_device *pdev)
{
	struct mt6397_chip *mt6397 = dev_get_drvdata(pdev->dev.parent);
	int i;
	u32 regval;

	for (i = 0; i < MT6397_MAX_REGULATOR; i++) {
		if (mt6397_regulators[i].vselctrl_reg) {
			if (regmap_read(mt6397->regmap,
				mt6397_regulators[i].vselctrl_reg,
				&regval) < 0) {
				dev_err(&pdev->dev,
					"Failed to read buck ctrl\n");
				return -EIO;
			}

			if (regval & mt6397_regulators[i].vselctrl_mask) {
				mt6397_regulators[i].desc.vsel_reg =
				mt6397_regulators[i].vselon_reg;
			}
		}
	}

	return 0;
}

static int mt6397_regulator_probe(struct platform_device *pdev)
{
	struct mt6397_chip *mt6397 = dev_get_drvdata(pdev->dev.parent);
	struct regulator_config config = {};
	struct regulator_dev *rdev;
	int i;
	u32 reg_value, version;

	/* Query buck controller to select activated voltage register part */
	if (mt6397_set_buck_vosel_reg(pdev))
		return -EIO;

	/* Read PMIC chip revision to update constraints and voltage table */
	if (regmap_read(mt6397->regmap, MT6397_CID, &reg_value) < 0) {
		dev_err(&pdev->dev, "Failed to read Chip ID\n");
		return -EIO;
	}
	dev_info(&pdev->dev, "Chip ID = 0x%x\n", reg_value);

	version = (reg_value & 0xFF);
	switch (version) {
	case MT6397_REGULATOR_ID91:
		mt6397_regulators[MT6397_ID_VGP2].desc.volt_table =
		ldo_volt_table5_v2;
		break;
	default:
		break;
	}

	for (i = 0; i < MT6397_MAX_REGULATOR; i++) {
		config.dev = &pdev->dev;
		config.driver_data = &mt6397_regulators[i];
		config.regmap = mt6397->regmap;
		rdev = devm_regulator_register(&pdev->dev,
				&mt6397_regulators[i].desc, &config);
		if (IS_ERR(rdev)) {
			dev_err(&pdev->dev, "failed to register %s\n",
				mt6397_regulators[i].desc.name);
			return PTR_ERR(rdev);
		}
	}

	return 0;
}

static const struct platform_device_id mt6397_platform_ids[] = {
	{"mt6397-regulator", 0},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(platform, mt6397_platform_ids);

static const struct of_device_id mt6397_of_match[] = {
	{ .compatible = "mediatek,mt6397-regulator", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, mt6397_of_match);

static struct platform_driver mt6397_regulator_driver = {
	.driver = {
		.name = "mt6397-regulator",
		.of_match_table = of_match_ptr(mt6397_of_match),
	},
	.probe = mt6397_regulator_probe,
	.id_table = mt6397_platform_ids,
};

module_platform_driver(mt6397_regulator_driver);

MODULE_AUTHOR("Flora Fu <flora.fu@mediatek.com>");
MODULE_DESCRIPTION("Regulator Driver for MediaTek MT6397 PMIC");
MODULE_LICENSE("GPL");
