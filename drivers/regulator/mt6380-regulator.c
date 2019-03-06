/*
 * Copyright (c) 2017 MediaTek Inc.
 * Author: Chenglin Xu <chenglin.xu@mediatek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/mt6380-regulator.h>
#include <linux/regulator/of_regulator.h>

/* PMIC Registers */
#define MT6380_ALDO_CON_0                         0x0000
#define MT6380_BTLDO_CON_0                        0x0004
#define MT6380_COMP_CON_0                         0x0008
#define MT6380_CPUBUCK_CON_0                      0x000C
#define MT6380_CPUBUCK_CON_1                      0x0010
#define MT6380_CPUBUCK_CON_2                      0x0014
#define MT6380_DDRLDO_CON_0                       0x0018
#define MT6380_MLDO_CON_0                         0x001C
#define MT6380_PALDO_CON_0                        0x0020
#define MT6380_PHYLDO_CON_0                       0x0024
#define MT6380_SIDO_CON_0                         0x0028
#define MT6380_SIDO_CON_1                         0x002C
#define MT6380_SIDO_CON_2                         0x0030
#define MT6380_SLDO_CON_0                         0x0034
#define MT6380_TLDO_CON_0                         0x0038
#define MT6380_STARTUP_CON_0                      0x003C
#define MT6380_STARTUP_CON_1                      0x0040
#define MT6380_SMPS_TOP_CON_0                     0x0044
#define MT6380_SMPS_TOP_CON_1                     0x0048
#define MT6380_ANA_CTRL_0                         0x0050
#define MT6380_ANA_CTRL_1                         0x0054
#define MT6380_ANA_CTRL_2                         0x0058
#define MT6380_ANA_CTRL_3                         0x005C
#define MT6380_ANA_CTRL_4                         0x0060
#define MT6380_SPK_CON9                           0x0064
#define MT6380_SPK_CON11                          0x0068
#define MT6380_SPK_CON12                          0x006A
#define MT6380_CLK_CTRL                           0x0070
#define MT6380_PINMUX_CTRL                        0x0074
#define MT6380_IO_CTRL                            0x0078
#define MT6380_SLP_MODE_CTRL_0                    0x007C
#define MT6380_SLP_MODE_CTRL_1                    0x0080
#define MT6380_SLP_MODE_CTRL_2                    0x0084
#define MT6380_SLP_MODE_CTRL_3                    0x0088
#define MT6380_SLP_MODE_CTRL_4                    0x008C
#define MT6380_SLP_MODE_CTRL_5                    0x0090
#define MT6380_SLP_MODE_CTRL_6                    0x0094
#define MT6380_SLP_MODE_CTRL_7                    0x0098
#define MT6380_SLP_MODE_CTRL_8                    0x009C
#define MT6380_FCAL_CTRL_0                        0x00A0
#define MT6380_FCAL_CTRL_1                        0x00A4
#define MT6380_LDO_CTRL_0                         0x00A8
#define MT6380_LDO_CTRL_1                         0x00AC
#define MT6380_LDO_CTRL_2                         0x00B0
#define MT6380_LDO_CTRL_3                         0x00B4
#define MT6380_LDO_CTRL_4                         0x00B8
#define MT6380_DEBUG_CTRL_0                       0x00BC
#define MT6380_EFU_CTRL_0                         0x0200
#define MT6380_EFU_CTRL_1                         0x0201
#define MT6380_EFU_CTRL_2                         0x0202
#define MT6380_EFU_CTRL_3                         0x0203
#define MT6380_EFU_CTRL_4                         0x0204
#define MT6380_EFU_CTRL_5                         0x0205
#define MT6380_EFU_CTRL_6                         0x0206
#define MT6380_EFU_CTRL_7                         0x0207
#define MT6380_EFU_CTRL_8                         0x0208

#define MT6380_REGULATOR_MODE_AUTO	0
#define MT6380_REGULATOR_MODE_FORCE_PWM	1

/*
 * mt6380 regulators' information
 *
 * @desc: standard fields of regulator description
 * @vselon_reg: Register sections for hardware control mode of bucks
 * @modeset_reg: Register for controlling the buck/LDO control mode
 * @modeset_mask: Mask for controlling the buck/LDO control mode
 */
struct mt6380_regulator_info {
	struct regulator_desc desc;
	u32 vselon_reg;
	u32 modeset_reg;
	u32 modeset_mask;
};

#define MT6380_BUCK(match, vreg, min, max, step, volt_ranges, enreg,	\
		    vosel, vosel_mask, enbit, voselon, _modeset_reg,	\
		    _modeset_mask)					\
[MT6380_ID_##vreg] = {							\
	.desc = {							\
		.name = #vreg,						\
		.of_match = of_match_ptr(match),			\
		.ops = &mt6380_volt_range_ops,				\
		.type = REGULATOR_VOLTAGE,				\
		.id = MT6380_ID_##vreg,					\
		.owner = THIS_MODULE,					\
		.n_voltages = ((max) - (min)) / (step) + 1,		\
		.linear_ranges = volt_ranges,				\
		.n_linear_ranges = ARRAY_SIZE(volt_ranges),		\
		.vsel_reg = vosel,					\
		.vsel_mask = vosel_mask,				\
		.enable_reg = enreg,					\
		.enable_mask = BIT(enbit),				\
	},								\
	.vselon_reg = voselon,						\
	.modeset_reg = _modeset_reg,					\
	.modeset_mask = _modeset_mask,					\
}

#define MT6380_LDO(match, vreg, ldo_volt_table, enreg, enbit, vosel,	\
		   vosel_mask, _modeset_reg, _modeset_mask)		\
[MT6380_ID_##vreg] = {							\
	.desc = {							\
		.name = #vreg,						\
		.of_match = of_match_ptr(match),			\
		.ops = &mt6380_volt_table_ops,				\
		.type = REGULATOR_VOLTAGE,				\
		.id = MT6380_ID_##vreg,					\
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
}

#define MT6380_REG_FIXED(match, vreg, enreg, enbit, volt,		\
			 _modeset_reg, _modeset_mask)			\
[MT6380_ID_##vreg] = {							\
	.desc = {							\
		.name = #vreg,						\
		.of_match = of_match_ptr(match),			\
		.ops = &mt6380_volt_fixed_ops,				\
		.type = REGULATOR_VOLTAGE,				\
		.id = MT6380_ID_##vreg,					\
		.owner = THIS_MODULE,					\
		.n_voltages = 1,					\
		.enable_reg = enreg,					\
		.enable_mask = BIT(enbit),				\
		.min_uV = volt,						\
	},								\
	.modeset_reg = _modeset_reg,					\
	.modeset_mask = _modeset_mask,					\
}

static const struct regulator_linear_range buck_volt_range1[] = {
	REGULATOR_LINEAR_RANGE(600000, 0, 0xfe, 6250),
};

static const struct regulator_linear_range buck_volt_range2[] = {
	REGULATOR_LINEAR_RANGE(600000, 0, 0xfe, 6250),
};

static const struct regulator_linear_range buck_volt_range3[] = {
	REGULATOR_LINEAR_RANGE(1200000, 0, 0x3c, 25000),
};

static const unsigned int ldo_volt_table1[] = {
	1400000, 1350000, 1300000, 1250000, 1200000, 1150000, 1100000, 1050000,
};

static const unsigned int ldo_volt_table2[] = {
	2200000, 3300000,
};

static const unsigned int ldo_volt_table3[] = {
	1240000, 1390000, 1540000, 1840000,
};

static const unsigned int ldo_volt_table4[] = {
	2200000, 3300000,
};

static int mt6380_regulator_set_mode(struct regulator_dev *rdev,
				     unsigned int mode)
{
	int ret, val = 0;
	struct mt6380_regulator_info *info = rdev_get_drvdata(rdev);

	switch (mode) {
	case REGULATOR_MODE_NORMAL:
		val = MT6380_REGULATOR_MODE_AUTO;
		break;
	case REGULATOR_MODE_FAST:
		val = MT6380_REGULATOR_MODE_FORCE_PWM;
		break;
	default:
		return -EINVAL;
	}

	val <<= ffs(info->modeset_mask) - 1;

	ret = regmap_update_bits(rdev->regmap, info->modeset_reg,
				 info->modeset_mask, val);

	return ret;
}

static unsigned int mt6380_regulator_get_mode(struct regulator_dev *rdev)
{
	unsigned int val;
	unsigned int mode;
	int ret;
	struct mt6380_regulator_info *info = rdev_get_drvdata(rdev);

	ret = regmap_read(rdev->regmap, info->modeset_reg, &val);
	if (ret < 0)
		return ret;

	val &= info->modeset_mask;
	val >>= ffs(info->modeset_mask) - 1;

	switch (val) {
	case MT6380_REGULATOR_MODE_AUTO:
		mode = REGULATOR_MODE_NORMAL;
		break;
	case MT6380_REGULATOR_MODE_FORCE_PWM:
		mode = REGULATOR_MODE_FAST;
		break;
	default:
		return -EINVAL;
	}

	return mode;
}

static const struct regulator_ops mt6380_volt_range_ops = {
	.list_voltage = regulator_list_voltage_linear_range,
	.map_voltage = regulator_map_voltage_linear_range,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.set_mode = mt6380_regulator_set_mode,
	.get_mode = mt6380_regulator_get_mode,
};

static const struct regulator_ops mt6380_volt_table_ops = {
	.list_voltage = regulator_list_voltage_table,
	.map_voltage = regulator_map_voltage_iterate,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.set_mode = mt6380_regulator_set_mode,
	.get_mode = mt6380_regulator_get_mode,
};

static const struct regulator_ops mt6380_volt_fixed_ops = {
	.list_voltage = regulator_list_voltage_linear,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.set_mode = mt6380_regulator_set_mode,
	.get_mode = mt6380_regulator_get_mode,
};

/* The array is indexed by id(MT6380_ID_XXX) */
static struct mt6380_regulator_info mt6380_regulators[] = {
	MT6380_BUCK("buck-vcore1", VCPU, 600000, 1393750, 6250,
		    buck_volt_range1, MT6380_ANA_CTRL_3, MT6380_ANA_CTRL_1,
		    0xfe, 3, MT6380_ANA_CTRL_1,
		    MT6380_CPUBUCK_CON_0, 0x8000000),
	MT6380_BUCK("buck-vcore", VCORE, 600000, 1393750, 6250,
		    buck_volt_range2, MT6380_ANA_CTRL_3, MT6380_ANA_CTRL_2,
		    0xfe, 2, MT6380_ANA_CTRL_2, MT6380_SIDO_CON_0, 0x1000000),
	MT6380_BUCK("buck-vrf", VRF, 1200000, 1575000, 25000,
		    buck_volt_range3, MT6380_ANA_CTRL_3, MT6380_SIDO_CON_0,
		    0x78, 1, MT6380_SIDO_CON_0, MT6380_SIDO_CON_0, 0x8000),
	MT6380_LDO("ldo-vm", VMLDO, ldo_volt_table1, MT6380_LDO_CTRL_0,
		   1, MT6380_MLDO_CON_0, 0xE000, MT6380_ANA_CTRL_1, 0x4000000),
	MT6380_LDO("ldo-va", VALDO, ldo_volt_table2, MT6380_LDO_CTRL_0,
		   2, MT6380_ALDO_CON_0, 0x400, MT6380_ALDO_CON_0, 0x20),
	MT6380_REG_FIXED("ldo-vphy", VPHYLDO, MT6380_LDO_CTRL_0, 7, 1800000,
			 MT6380_PHYLDO_CON_0, 0x80),
	MT6380_LDO("ldo-vddr", VDDRLDO, ldo_volt_table3, MT6380_LDO_CTRL_0,
		   8, MT6380_DDRLDO_CON_0, 0x3000, MT6380_DDRLDO_CON_0, 0x80),
	MT6380_LDO("ldo-vt", VTLDO, ldo_volt_table4, MT6380_LDO_CTRL_0, 3,
		   MT6380_TLDO_CON_0, 0x400, MT6380_TLDO_CON_0, 0x20),
};

static int mt6380_regulator_probe(struct platform_device *pdev)
{
	struct regmap *regmap = dev_get_regmap(pdev->dev.parent, NULL);
	struct regulator_config config = {};
	struct regulator_dev *rdev;
	int i;

	for (i = 0; i < MT6380_MAX_REGULATOR; i++) {
		config.dev = &pdev->dev;
		config.driver_data = &mt6380_regulators[i];
		config.regmap = regmap;
		rdev = devm_regulator_register(&pdev->dev,
					       &mt6380_regulators[i].desc,
				&config);
		if (IS_ERR(rdev)) {
			dev_err(&pdev->dev, "failed to register %s\n",
				mt6380_regulators[i].desc.name);
			return PTR_ERR(rdev);
		}
	}
	return 0;
}

static const struct platform_device_id mt6380_platform_ids[] = {
	{"mt6380-regulator", 0},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(platform, mt6380_platform_ids);

static const struct of_device_id mt6380_of_match[] = {
	{ .compatible = "mediatek,mt6380-regulator", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, mt6380_of_match);

static struct platform_driver mt6380_regulator_driver = {
	.driver = {
		.name = "mt6380-regulator",
		.of_match_table = of_match_ptr(mt6380_of_match),
	},
	.probe = mt6380_regulator_probe,
	.id_table = mt6380_platform_ids,
};

module_platform_driver(mt6380_regulator_driver);

MODULE_AUTHOR("Chenglin Xu <chenglin.xu@mediatek.com>");
MODULE_DESCRIPTION("Regulator Driver for MediaTek MT6380 PMIC");
MODULE_LICENSE("GPL v2");
