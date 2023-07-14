// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2022 Collabora Ltd.
// Author: AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>
//
// Based on mt6323-regulator.c,
//     Copyright (c) 2016 MediaTek Inc.
//

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/mfd/mt6397/core.h>
#include <linux/mfd/mt6331/registers.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/mt6331-regulator.h>
#include <linux/regulator/of_regulator.h>

#define MT6331_LDO_MODE_NORMAL	0
#define MT6331_LDO_MODE_LP	1

/*
 * MT6331 regulators information
 *
 * @desc: standard fields of regulator description.
 * @qi: Mask for query enable signal status of regulators
 * @vselon_reg: Register sections for hardware control mode of bucks
 * @vselctrl_reg: Register for controlling the buck control mode.
 * @vselctrl_mask: Mask for query buck's voltage control mode.
 * @status_reg: Register for regulator enable status where qi unavailable
 * @status_mask: Mask for querying regulator enable status
 */
struct mt6331_regulator_info {
	struct regulator_desc desc;
	u32 qi;
	u32 vselon_reg;
	u32 vselctrl_reg;
	u32 vselctrl_mask;
	u32 modeset_reg;
	u32 modeset_mask;
	u32 status_reg;
	u32 status_mask;
};

#define MT6331_BUCK(match, vreg, min, max, step, volt_ranges, enreg,	\
		vosel, vosel_mask, voselon, vosel_ctrl)			\
[MT6331_ID_##vreg] = {							\
	.desc = {							\
		.name = #vreg,						\
		.of_match = of_match_ptr(match),			\
		.ops = &mt6331_volt_range_ops,				\
		.type = REGULATOR_VOLTAGE,				\
		.id = MT6331_ID_##vreg,					\
		.owner = THIS_MODULE,					\
		.n_voltages = (max - min)/step + 1,			\
		.linear_ranges = volt_ranges,				\
		.n_linear_ranges = ARRAY_SIZE(volt_ranges),		\
		.vsel_reg = vosel,					\
		.vsel_mask = vosel_mask,				\
		.enable_reg = enreg,					\
		.enable_mask = BIT(0),					\
	},								\
	.qi = BIT(13),							\
	.vselon_reg = voselon,						\
	.vselctrl_reg = vosel_ctrl,					\
	.vselctrl_mask = BIT(1),					\
	.status_mask = 0,						\
}

#define MT6331_LDO_AO(match, vreg, ldo_volt_table, vosel, vosel_mask)	\
[MT6331_ID_##vreg] = {							\
	.desc = {							\
		.name = #vreg,						\
		.of_match = of_match_ptr(match),			\
		.ops = &mt6331_volt_table_ao_ops,			\
		.type = REGULATOR_VOLTAGE,				\
		.id = MT6331_ID_##vreg,					\
		.owner = THIS_MODULE,					\
		.n_voltages = ARRAY_SIZE(ldo_volt_table),		\
		.volt_table = ldo_volt_table,				\
		.vsel_reg = vosel,					\
		.vsel_mask = vosel_mask,				\
	},								\
}

#define MT6331_LDO_S(match, vreg, ldo_volt_table, enreg, enbit, vosel,	\
		     vosel_mask, _modeset_reg, _modeset_mask,		\
		     _status_reg, _status_mask)				\
[MT6331_ID_##vreg] = {							\
	.desc = {							\
		.name = #vreg,						\
		.of_match = of_match_ptr(match),			\
		.ops = &mt6331_volt_table_no_qi_ops,			\
		.type = REGULATOR_VOLTAGE,				\
		.id = MT6331_ID_##vreg,					\
		.owner = THIS_MODULE,					\
		.n_voltages = ARRAY_SIZE(ldo_volt_table),		\
		.volt_table = ldo_volt_table,				\
		.vsel_reg = vosel,					\
		.vsel_mask = vosel_mask,				\
		.enable_reg = enreg,					\
		.enable_mask = BIT(enbit),				\
	},								\
	.modeset_reg = _modeset_reg,					\
	.modeset_mask = _modeset_mask,					\
	.status_reg = _status_reg,					\
	.status_mask = _status_mask,					\
}

#define MT6331_LDO(match, vreg, ldo_volt_table, enreg, enbit, vosel,	\
		   vosel_mask, _modeset_reg, _modeset_mask)		\
[MT6331_ID_##vreg] = {							\
	.desc = {							\
		.name = #vreg,						\
		.of_match = of_match_ptr(match),			\
		.ops = (_modeset_reg ?					\
			&mt6331_volt_table_ops :			\
			&mt6331_volt_table_no_ms_ops),			\
		.type = REGULATOR_VOLTAGE,				\
		.id = MT6331_ID_##vreg,					\
		.owner = THIS_MODULE,					\
		.n_voltages = ARRAY_SIZE(ldo_volt_table),		\
		.volt_table = ldo_volt_table,				\
		.vsel_reg = vosel,					\
		.vsel_mask = vosel_mask,				\
		.enable_reg = enreg,					\
		.enable_mask = BIT(enbit),				\
	},								\
	.qi = BIT(15),							\
	.modeset_reg = _modeset_reg,					\
	.modeset_mask = _modeset_mask,					\
}

#define MT6331_REG_FIXED(match, vreg, enreg, enbit, qibit, volt,	\
			 _modeset_reg, _modeset_mask)			\
[MT6331_ID_##vreg] = {							\
	.desc = {							\
		.name = #vreg,						\
		.of_match = of_match_ptr(match),			\
		.ops = (_modeset_reg ?					\
			&mt6331_volt_fixed_ops :			\
			&mt6331_volt_fixed_no_ms_ops),			\
		.type = REGULATOR_VOLTAGE,				\
		.id = MT6331_ID_##vreg,					\
		.owner = THIS_MODULE,					\
		.n_voltages = 1,					\
		.enable_reg = enreg,					\
		.enable_mask = BIT(enbit),				\
		.min_uV = volt,						\
	},								\
	.qi = BIT(qibit),						\
	.modeset_reg = _modeset_reg,					\
	.modeset_mask = _modeset_mask,					\
}

static const struct linear_range buck_volt_range[] = {
	REGULATOR_LINEAR_RANGE(700000, 0, 0x7f, 6250),
};

static const unsigned int ldo_volt_table1[] = {
	2800000, 3000000, 0, 3200000
};

static const unsigned int ldo_volt_table2[] = {
	1500000, 1800000, 2500000, 2800000,
};

static const unsigned int ldo_volt_table3[] = {
	1200000, 1300000, 1500000, 1800000, 2000000, 2800000, 3000000, 3300000,
};

static const unsigned int ldo_volt_table4[] = {
	0, 0, 1700000, 1800000, 1860000, 2760000, 3000000, 3100000,
};

static const unsigned int ldo_volt_table5[] = {
	1800000, 3300000, 1800000, 3300000,
};

static const unsigned int ldo_volt_table6[] = {
	3000000, 3300000,
};

static const unsigned int ldo_volt_table7[] = {
	1200000, 1600000, 1700000, 1800000, 1900000, 2000000, 2100000, 2200000,
};

static const unsigned int ldo_volt_table8[] = {
	900000, 1000000, 1100000, 1220000, 1300000, 1500000, 1500000, 1500000,
};

static const unsigned int ldo_volt_table9[] = {
	1000000, 1050000, 1100000, 1150000, 1200000, 1250000, 1300000, 1300000,
};

static const unsigned int ldo_volt_table10[] = {
	1200000, 1300000, 1500000, 1800000,
};

static const unsigned int ldo_volt_table11[] = {
	1200000, 1300000, 1400000, 1500000, 1600000, 1700000, 1800000, 1800000,
};

static int mt6331_get_status(struct regulator_dev *rdev)
{
	struct mt6331_regulator_info *info = rdev_get_drvdata(rdev);
	u32 regval;
	int ret;

	ret = regmap_read(rdev->regmap, info->desc.enable_reg, &regval);
	if (ret != 0) {
		dev_err(&rdev->dev, "Failed to get enable reg: %d\n", ret);
		return ret;
	}

	return (regval & info->qi) ? REGULATOR_STATUS_ON : REGULATOR_STATUS_OFF;
}

static int mt6331_ldo_set_mode(struct regulator_dev *rdev, unsigned int mode)
{
	struct mt6331_regulator_info *info = rdev_get_drvdata(rdev);
	int val;

	switch (mode) {
	case REGULATOR_MODE_STANDBY:
		val = MT6331_LDO_MODE_LP;
		break;
	case REGULATOR_MODE_NORMAL:
		val = MT6331_LDO_MODE_NORMAL;
		break;
	default:
		return -EINVAL;
	}

	val <<= ffs(info->modeset_mask) - 1;

	return regmap_update_bits(rdev->regmap, info->modeset_reg,
				  info->modeset_mask, val);
}

static unsigned int mt6331_ldo_get_mode(struct regulator_dev *rdev)
{
	struct mt6331_regulator_info *info = rdev_get_drvdata(rdev);
	unsigned int val;
	int ret;

	ret = regmap_read(rdev->regmap, info->modeset_reg, &val);
	if (ret < 0)
		return ret;

	val &= info->modeset_mask;
	val >>= ffs(info->modeset_mask) - 1;

	return (val & BIT(0)) ? REGULATOR_MODE_STANDBY : REGULATOR_MODE_NORMAL;
}

static const struct regulator_ops mt6331_volt_range_ops = {
	.list_voltage = regulator_list_voltage_linear_range,
	.map_voltage = regulator_map_voltage_linear_range,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.get_status = mt6331_get_status,
};

static const struct regulator_ops mt6331_volt_table_no_ms_ops = {
	.list_voltage = regulator_list_voltage_table,
	.map_voltage = regulator_map_voltage_iterate,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.get_status = mt6331_get_status,
};

static const struct regulator_ops mt6331_volt_table_no_qi_ops = {
	.list_voltage = regulator_list_voltage_table,
	.map_voltage = regulator_map_voltage_iterate,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.set_mode = mt6331_ldo_set_mode,
	.get_mode = mt6331_ldo_get_mode,
};

static const struct regulator_ops mt6331_volt_table_ops = {
	.list_voltage = regulator_list_voltage_table,
	.map_voltage = regulator_map_voltage_iterate,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.get_status = mt6331_get_status,
	.set_mode = mt6331_ldo_set_mode,
	.get_mode = mt6331_ldo_get_mode,
};

static const struct regulator_ops mt6331_volt_table_ao_ops = {
	.list_voltage = regulator_list_voltage_table,
	.map_voltage = regulator_map_voltage_iterate,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
};

static const struct regulator_ops mt6331_volt_fixed_no_ms_ops = {
	.list_voltage = regulator_list_voltage_linear,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.get_status = mt6331_get_status,
};

static const struct regulator_ops mt6331_volt_fixed_ops = {
	.list_voltage = regulator_list_voltage_linear,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.get_status = mt6331_get_status,
	.set_mode = mt6331_ldo_set_mode,
	.get_mode = mt6331_ldo_get_mode,
};

/* The array is indexed by id(MT6331_ID_XXX) */
static struct mt6331_regulator_info mt6331_regulators[] = {
	MT6331_BUCK("buck-vdvfs11", VDVFS11, 700000, 1493750, 6250,
		    buck_volt_range, MT6331_VDVFS11_CON9,
		    MT6331_VDVFS11_CON11, GENMASK(6, 0),
		    MT6331_VDVFS11_CON12, MT6331_VDVFS11_CON7),
	MT6331_BUCK("buck-vdvfs12", VDVFS12, 700000, 1493750, 6250,
		    buck_volt_range, MT6331_VDVFS12_CON9,
		    MT6331_VDVFS12_CON11, GENMASK(6, 0),
		    MT6331_VDVFS12_CON12, MT6331_VDVFS12_CON7),
	MT6331_BUCK("buck-vdvfs13", VDVFS13, 700000, 1493750, 6250,
		    buck_volt_range, MT6331_VDVFS13_CON9,
		    MT6331_VDVFS13_CON11, GENMASK(6, 0),
		    MT6331_VDVFS13_CON12, MT6331_VDVFS13_CON7),
	MT6331_BUCK("buck-vdvfs14", VDVFS14, 700000, 1493750, 6250,
		    buck_volt_range, MT6331_VDVFS14_CON9,
		    MT6331_VDVFS14_CON11, GENMASK(6, 0),
		    MT6331_VDVFS14_CON12, MT6331_VDVFS14_CON7),
	MT6331_BUCK("buck-vcore2", VCORE2, 700000, 1493750, 6250,
		    buck_volt_range, MT6331_VCORE2_CON9,
		    MT6331_VCORE2_CON11, GENMASK(6, 0),
		    MT6331_VCORE2_CON12, MT6331_VCORE2_CON7),
	MT6331_REG_FIXED("buck-vio18", VIO18, MT6331_VIO18_CON9, 0, 13, 1800000, 0, 0),
	MT6331_REG_FIXED("ldo-vrtc", VRTC, MT6331_DIGLDO_CON11, 8, 15, 2800000, 0, 0),
	MT6331_REG_FIXED("ldo-vtcxo1", VTCXO1, MT6331_ANALDO_CON1, 10, 15, 2800000,
			 MT6331_ANALDO_CON1, GENMASK(1, 0)),
	MT6331_REG_FIXED("ldo-vtcxo2", VTCXO2, MT6331_ANALDO_CON2, 10, 15, 2800000,
			 MT6331_ANALDO_CON2, GENMASK(1, 0)),
	MT6331_REG_FIXED("ldo-vsram", VSRAM_DVFS1, MT6331_SYSLDO_CON4, 10, 15, 1012500,
			 MT6331_SYSLDO_CON4, GENMASK(1, 0)),
	MT6331_REG_FIXED("ldo-vio28", VIO28, MT6331_DIGLDO_CON1, 10, 15, 2800000,
			 MT6331_DIGLDO_CON1, GENMASK(1, 0)),
	MT6331_LDO("ldo-avdd32aud", AVDD32_AUD, ldo_volt_table1, MT6331_ANALDO_CON3, 10,
		   MT6331_ANALDO_CON10, GENMASK(6, 5), MT6331_ANALDO_CON3, GENMASK(1, 0)),
	MT6331_LDO("ldo-vauxa32", VAUXA32, ldo_volt_table1, MT6331_ANALDO_CON4, 10,
		   MT6331_ANALDO_CON6, GENMASK(6, 5), MT6331_ANALDO_CON4, GENMASK(1, 0)),
	MT6331_LDO("ldo-vemc33", VEMC33, ldo_volt_table6, MT6331_DIGLDO_CON5, 10,
		   MT6331_DIGLDO_CON17, BIT(6), MT6331_DIGLDO_CON5, GENMASK(1, 0)),
	MT6331_LDO("ldo-vibr", VIBR, ldo_volt_table3, MT6331_DIGLDO_CON12, 10,
		   MT6331_DIGLDO_CON20, GENMASK(6, 4), MT6331_DIGLDO_CON12, GENMASK(1, 0)),
	MT6331_LDO("ldo-vmc", VMC, ldo_volt_table5, MT6331_DIGLDO_CON3, 10,
		   MT6331_DIGLDO_CON15, GENMASK(5, 4), MT6331_DIGLDO_CON3, GENMASK(1, 0)),
	MT6331_LDO("ldo-vmch", VMCH, ldo_volt_table6, MT6331_DIGLDO_CON4, 10,
		   MT6331_DIGLDO_CON16, BIT(6), MT6331_DIGLDO_CON4, GENMASK(1, 0)),
	MT6331_LDO("ldo-vmipi", VMIPI, ldo_volt_table3, MT6331_SYSLDO_CON5, 10,
		   MT6331_SYSLDO_CON13, GENMASK(5, 3), MT6331_SYSLDO_CON5, GENMASK(1, 0)),
	MT6331_LDO("ldo-vsim1", VSIM1, ldo_volt_table4, MT6331_DIGLDO_CON8, 10,
		   MT6331_DIGLDO_CON21, GENMASK(6, 4), MT6331_DIGLDO_CON8, GENMASK(1, 0)),
	MT6331_LDO("ldo-vsim2", VSIM2, ldo_volt_table4, MT6331_DIGLDO_CON9, 10,
		   MT6331_DIGLDO_CON22, GENMASK(6, 4), MT6331_DIGLDO_CON9, GENMASK(1, 0)),
	MT6331_LDO("ldo-vusb10", VUSB10, ldo_volt_table9, MT6331_SYSLDO_CON2, 10,
		   MT6331_SYSLDO_CON10, GENMASK(5, 3), MT6331_SYSLDO_CON2, GENMASK(1, 0)),
	MT6331_LDO("ldo-vcama", VCAMA, ldo_volt_table2, MT6331_ANALDO_CON5, 15,
		   MT6331_ANALDO_CON9, GENMASK(5, 4), 0, 0),
	MT6331_LDO_S("ldo-vcamaf", VCAM_AF, ldo_volt_table3, MT6331_DIGLDO_CON2, 10,
		     MT6331_DIGLDO_CON14, GENMASK(6, 4), MT6331_DIGLDO_CON2, GENMASK(1, 0),
		     MT6331_EN_STATUS1, BIT(0)),
	MT6331_LDO_S("ldo-vcamd", VCAMD, ldo_volt_table8, MT6331_SYSLDO_CON1, 15,
		     MT6331_SYSLDO_CON9, GENMASK(6, 4), MT6331_SYSLDO_CON1, GENMASK(1, 0),
		     MT6331_EN_STATUS1, BIT(11)),
	MT6331_LDO_S("ldo-vcamio", VCAM_IO,  ldo_volt_table10, MT6331_SYSLDO_CON3, 10,
		     MT6331_SYSLDO_CON11, GENMASK(4, 3), MT6331_SYSLDO_CON3, GENMASK(1, 0),
		     MT6331_EN_STATUS1, BIT(13)),
	MT6331_LDO_S("ldo-vgp1", VGP1, ldo_volt_table3, MT6331_DIGLDO_CON6, 10,
		     MT6331_DIGLDO_CON19, GENMASK(6, 4), MT6331_DIGLDO_CON6, GENMASK(1, 0),
		     MT6331_EN_STATUS1, BIT(4)),
	MT6331_LDO_S("ldo-vgp2", VGP2, ldo_volt_table10, MT6331_SYSLDO_CON6, 10,
		     MT6331_SYSLDO_CON14, GENMASK(4, 3), MT6331_SYSLDO_CON6, GENMASK(1, 0),
		     MT6331_EN_STATUS1, BIT(15)),
	MT6331_LDO_S("ldo-vgp3", VGP3, ldo_volt_table10, MT6331_SYSLDO_CON7, 10,
		     MT6331_SYSLDO_CON15, GENMASK(4, 3), MT6331_SYSLDO_CON7, GENMASK(1, 0),
		     MT6331_EN_STATUS2, BIT(0)),
	MT6331_LDO_S("ldo-vgp4", VGP4, ldo_volt_table7, MT6331_DIGLDO_CON7, 10,
		     MT6331_DIGLDO_CON18, GENMASK(6, 4), MT6331_DIGLDO_CON7, GENMASK(1, 0),
		     MT6331_EN_STATUS1, BIT(5)),
	MT6331_LDO_AO("ldo-vdig18", VDIG18, ldo_volt_table11,
		      MT6331_DIGLDO_CON28, GENMASK(14, 12)),
};

static int mt6331_set_buck_vosel_reg(struct platform_device *pdev)
{
	struct mt6397_chip *mt6331 = dev_get_drvdata(pdev->dev.parent);
	int i;
	u32 regval;

	for (i = 0; i < MT6331_ID_VREG_MAX; i++) {
		if (mt6331_regulators[i].vselctrl_reg) {
			if (regmap_read(mt6331->regmap,
				mt6331_regulators[i].vselctrl_reg,
				&regval) < 0) {
				dev_err(&pdev->dev,
					"Failed to read buck ctrl\n");
				return -EIO;
			}

			if (regval & mt6331_regulators[i].vselctrl_mask) {
				mt6331_regulators[i].desc.vsel_reg =
				mt6331_regulators[i].vselon_reg;
			}
		}
	}

	return 0;
}

static int mt6331_regulator_probe(struct platform_device *pdev)
{
	struct mt6397_chip *mt6331 = dev_get_drvdata(pdev->dev.parent);
	struct regulator_config config = {};
	struct regulator_dev *rdev;
	int i;
	u32 reg_value;

	/* Query buck controller to select activated voltage register part */
	if (mt6331_set_buck_vosel_reg(pdev))
		return -EIO;

	/* Read PMIC chip revision to update constraints and voltage table */
	if (regmap_read(mt6331->regmap, MT6331_HWCID, &reg_value) < 0) {
		dev_err(&pdev->dev, "Failed to read Chip ID\n");
		return -EIO;
	}
	reg_value &= GENMASK(7, 0);

	dev_info(&pdev->dev, "Chip ID = 0x%x\n", reg_value);

	/*
	 * ChipID 0x10 is "MT6331 E1", has a different voltage table and
	 * it's currently not supported in this driver. Upon detection of
	 * this ID, refuse to register the regulators, as we will wrongly
	 * interpret the VSEL for this revision, potentially overvolting
	 * some device.
	 */
	if (reg_value == 0x10) {
		dev_err(&pdev->dev, "Chip version not supported. Bailing out.\n");
		return -EINVAL;
	}

	for (i = 0; i < MT6331_ID_VREG_MAX; i++) {
		config.dev = &pdev->dev;
		config.driver_data = &mt6331_regulators[i];
		config.regmap = mt6331->regmap;
		rdev = devm_regulator_register(&pdev->dev,
				&mt6331_regulators[i].desc, &config);
		if (IS_ERR(rdev)) {
			dev_err(&pdev->dev, "failed to register %s\n",
				mt6331_regulators[i].desc.name);
			return PTR_ERR(rdev);
		}
	}
	return 0;
}

static const struct platform_device_id mt6331_platform_ids[] = {
	{"mt6331-regulator", 0},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(platform, mt6331_platform_ids);

static struct platform_driver mt6331_regulator_driver = {
	.driver = {
		.name = "mt6331-regulator",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.probe = mt6331_regulator_probe,
	.id_table = mt6331_platform_ids,
};

module_platform_driver(mt6331_regulator_driver);

MODULE_AUTHOR("AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>");
MODULE_DESCRIPTION("Regulator Driver for MediaTek MT6331 PMIC");
MODULE_LICENSE("GPL");
