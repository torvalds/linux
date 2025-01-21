// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2022 MediaTek Inc.
// Copyright (c) 2022 BayLibre, SAS.
// Author: Chen Zhong <chen.zhong@mediatek.com>
// Author: Fabien Parent <fparent@baylibre.com>
// Author: Alexandre Mergnat <amergnat@baylibre.com>
//
// Based on mt6397-regulator.c
//

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/mfd/mt6397/core.h>
#include <linux/mfd/mt6357/registers.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/mt6357-regulator.h>
#include <linux/regulator/of_regulator.h>

/*
 * MT6357 regulators' information
 *
 * @desc: standard fields of regulator description.
 * @da_vsel_reg: Monitor register for query buck's voltage.
 * @da_vsel_mask: Mask for query buck's voltage.
 */
struct mt6357_regulator_info {
	struct regulator_desc desc;
	u32 da_vsel_reg;
	u32 da_vsel_mask;
};

#define MT6357_BUCK(match, vreg, min, max, step,		\
	volt_ranges, vosel_reg, vosel_mask, _da_vsel_mask)	\
[MT6357_ID_##vreg] = {		\
	.desc = {		\
		.name = #vreg,	\
		.of_match = of_match_ptr(match),		\
		.regulators_node = "regulators",		\
		.ops = &mt6357_volt_range_ops,	\
		.type = REGULATOR_VOLTAGE,	\
		.id = MT6357_ID_##vreg,		\
		.owner = THIS_MODULE,		\
		.n_voltages = ((max) - (min)) / (step) + 1,	\
		.linear_ranges = volt_ranges,			\
		.n_linear_ranges = ARRAY_SIZE(volt_ranges),	\
		.vsel_reg = vosel_reg,		\
		.vsel_mask = vosel_mask,	\
		.enable_reg = MT6357_BUCK_##vreg##_CON0,	\
		.enable_mask = BIT(0),		\
	},	\
	.da_vsel_reg = MT6357_BUCK_##vreg##_DBG0,		\
	.da_vsel_mask = vosel_mask,		\
}

#define MT6357_LDO(match, vreg, ldo_volt_table,	\
	enreg, vosel, vosel_mask)		\
[MT6357_ID_##vreg] = {		\
	.desc = {		\
		.name = #vreg,	\
		.of_match = of_match_ptr(match),		\
		.regulators_node = "regulators",		\
		.ops = &mt6357_volt_table_ops,	\
		.type = REGULATOR_VOLTAGE,	\
		.id = MT6357_ID_##vreg,		\
		.owner = THIS_MODULE,		\
		.n_voltages = ARRAY_SIZE(ldo_volt_table),	\
		.volt_table = ldo_volt_table,	\
		.vsel_reg = vosel,		\
		.vsel_mask = vosel_mask,	\
		.enable_reg = enreg,		\
		.enable_mask = BIT(0),		\
	},	\
}

#define MT6357_LDO1(match, vreg, min, max, step, volt_ranges,	\
	enreg, vosel, vosel_mask)		\
[MT6357_ID_##vreg] = {		\
	.desc = {		\
		.name = #vreg,	\
		.of_match = of_match_ptr(match),		\
		.regulators_node = "regulators",		\
		.ops = &mt6357_volt_range_ops,	\
		.type = REGULATOR_VOLTAGE,	\
		.id = MT6357_ID_##vreg,		\
		.owner = THIS_MODULE,		\
		.n_voltages = ((max) - (min)) / (step) + 1,	\
		.linear_ranges = volt_ranges,	\
		.n_linear_ranges = ARRAY_SIZE(volt_ranges),	\
		.vsel_reg = vosel,		\
		.vsel_mask = vosel_mask,	\
		.enable_reg = enreg,		\
		.enable_mask = BIT(0),		\
	},	\
	.da_vsel_reg = MT6357_LDO_##vreg##_DBG0,		\
	.da_vsel_mask = 0x7f00,	\
}

#define MT6357_REG_FIXED(match, vreg, volt)	\
[MT6357_ID_##vreg] = {					\
	.desc = {					\
		.name = #vreg,				\
		.of_match = of_match_ptr(match),	\
		.regulators_node = "regulators",	\
		.ops = &mt6357_volt_fixed_ops,		\
		.type = REGULATOR_VOLTAGE,		\
		.id = MT6357_ID_##vreg,			\
		.owner = THIS_MODULE,			\
		.n_voltages = 1,			\
		.enable_reg = MT6357_LDO_##vreg##_CON0,	\
		.enable_mask = BIT(0),			\
		.min_uV = volt,				\
	},						\
}

/**
 * mt6357_get_buck_voltage_sel - get_voltage_sel for regmap users
 *
 * @rdev: regulator to operate on
 *
 * Regulators that use regmap for their register I/O can set the
 * da_vsel_reg and da_vsel_mask fields in the info structure and
 * then use this as their get_voltage_sel operation.
 */
static int mt6357_get_buck_voltage_sel(struct regulator_dev *rdev)
{
	int ret, regval;
	struct mt6357_regulator_info *info = rdev_get_drvdata(rdev);

	ret = regmap_read(rdev->regmap, info->da_vsel_reg, &regval);
	if (ret != 0) {
		dev_err(&rdev->dev,
			"Failed to get mt6357 Buck %s vsel reg: %d\n",
			info->desc.name, ret);
		return ret;
	}

	regval &= info->da_vsel_mask;
	regval >>= ffs(info->da_vsel_mask) - 1;

	return regval;
}

static const struct regulator_ops mt6357_volt_range_ops = {
	.list_voltage = regulator_list_voltage_linear_range,
	.map_voltage = regulator_map_voltage_linear_range,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = mt6357_get_buck_voltage_sel,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
};

static const struct regulator_ops mt6357_volt_table_ops = {
	.list_voltage = regulator_list_voltage_table,
	.map_voltage = regulator_map_voltage_iterate,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
};

static const struct regulator_ops mt6357_volt_fixed_ops = {
	.list_voltage = regulator_list_voltage_linear,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
};

static const int vxo22_voltages[] = {
	2200000,
	0,
	2400000,
};

static const int vefuse_voltages[] = {
	1200000,
	1300000,
	1500000,
	0,
	1800000,
	0,
	0,
	0,
	0,
	2800000,
	2900000,
	3000000,
	0,
	3300000,
};

static const int vcn33_voltages[] = {
	0,
	3300000,
	3400000,
	3500000,
};

static const int vcama_voltages[] = {
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	2500000,
	0,
	0,
	2800000,
};

static const int vcamd_voltages[] = {
	0,
	0,
	0,
	0,
	1000000,
	1100000,
	1200000,
	1300000,
	0,
	1500000,
	0,
	0,
	1800000,
};

static const int vldo28_voltages[] = {
	0,
	2800000,
	0,
	3000000,
};

static const int vdram_voltages[] = {
	0,
	1100000,
	1200000,
};

static const int vsim_voltages[] = {
	0,
	0,
	0,
	1700000,
	1800000,
	0,
	0,
	0,
	2700000,
	0,
	0,
	3000000,
	3100000,
};

static const int vibr_voltages[] = {
	1200000,
	1300000,
	1500000,
	0,
	1800000,
	2000000,
	0,
	0,
	0,
	2800000,
	0,
	3000000,
	0,
	3300000,
};

static const int vmc_voltages[] = {
	0,
	0,
	0,
	0,
	1800000,
	0,
	0,
	0,
	0,
	0,
	2900000,
	3000000,
	0,
	3300000,
};

static const int vmch_voltages[] = {
	0,
	0,
	2900000,
	3000000,
	0,
	3300000,
};

static const int vemc_voltages[] = {
	0,
	0,
	2900000,
	3000000,
	0,
	3300000,
};

static const int vusb_voltages[] = {
	0,
	0,
	0,
	3000000,
	3100000,
};

static const struct linear_range buck_volt_range1[] = {
	REGULATOR_LINEAR_RANGE(518750, 0, 0x7f, 6250),
};

static const struct linear_range buck_volt_range2[] = {
	REGULATOR_LINEAR_RANGE(500000, 0, 0x7f, 6250),
};

static const struct linear_range buck_volt_range3[] = {
	REGULATOR_LINEAR_RANGE(500000, 0, 0x3f, 50000),
};

static const struct linear_range buck_volt_range4[] = {
	REGULATOR_LINEAR_RANGE(1200000, 0, 0x7f, 12500),
};

/* The array is indexed by id(MT6357_ID_XXX) */
static struct mt6357_regulator_info mt6357_regulators[] = {
	/* Bucks */
	MT6357_BUCK("buck-vcore", VCORE, 518750, 1312500, 6250,
		buck_volt_range1, MT6357_BUCK_VCORE_ELR0, 0x7f, 0x7f),
	MT6357_BUCK("buck-vproc", VPROC, 518750, 1312500, 6250,
		buck_volt_range1, MT6357_BUCK_VPROC_ELR0, 0x7f, 0x7f),
	MT6357_BUCK("buck-vmodem", VMODEM, 500000, 1293750, 6250,
		buck_volt_range2, MT6357_BUCK_VMODEM_ELR0, 0x7f, 0x7f),
	MT6357_BUCK("buck-vpa", VPA, 500000, 3650000, 50000,
		buck_volt_range3, MT6357_BUCK_VPA_CON1, 0x3f, 0x3f),
	MT6357_BUCK("buck-vs1", VS1, 1200000, 2787500, 12500,
		buck_volt_range4, MT6357_BUCK_VS1_ELR0, 0x7f, 0x7f),

	/* LDOs */
	MT6357_LDO("ldo-vcama", VCAMA, vcama_voltages,
		   MT6357_LDO_VCAMA_CON0, MT6357_VCAMA_ANA_CON0, 0xf00),
	MT6357_LDO("ldo-vcamd", VCAMD, vcamd_voltages,
		   MT6357_LDO_VCAMD_CON0, MT6357_VCAMD_ANA_CON0, 0xf00),
	MT6357_LDO("ldo-vcn33-bt", VCN33_BT, vcn33_voltages,
		   MT6357_LDO_VCN33_CON0_0, MT6357_VCN33_ANA_CON0, 0x300),
	MT6357_LDO("ldo-vcn33-wifi", VCN33_WIFI, vcn33_voltages,
		   MT6357_LDO_VCN33_CON0_1, MT6357_VCN33_ANA_CON0, 0x300),
	MT6357_LDO("ldo-vdram", VDRAM, vdram_voltages,
		   MT6357_LDO_VDRAM_CON0, MT6357_VDRAM_ELR_2, 0x300),
	MT6357_LDO("ldo-vefuse", VEFUSE, vefuse_voltages,
		   MT6357_LDO_VEFUSE_CON0, MT6357_VEFUSE_ANA_CON0, 0xf00),
	MT6357_LDO("ldo-vemc", VEMC, vemc_voltages,
		   MT6357_LDO_VEMC_CON0, MT6357_VEMC_ANA_CON0, 0x700),
	MT6357_LDO("ldo-vibr", VIBR, vibr_voltages,
		   MT6357_LDO_VIBR_CON0, MT6357_VIBR_ANA_CON0, 0xf00),
	MT6357_LDO("ldo-vldo28", VLDO28, vldo28_voltages,
		   MT6357_LDO_VLDO28_CON0_0, MT6357_VLDO28_ANA_CON0, 0x300),
	MT6357_LDO("ldo-vmc", VMC, vmc_voltages,
		   MT6357_LDO_VMC_CON0, MT6357_VMC_ANA_CON0, 0xf00),
	MT6357_LDO("ldo-vmch", VMCH, vmch_voltages,
		   MT6357_LDO_VMCH_CON0, MT6357_VMCH_ANA_CON0, 0x700),
	MT6357_LDO("ldo-vsim1", VSIM1, vsim_voltages,
		   MT6357_LDO_VSIM1_CON0, MT6357_VSIM1_ANA_CON0, 0xf00),
	MT6357_LDO("ldo-vsim2", VSIM2, vsim_voltages,
		   MT6357_LDO_VSIM2_CON0, MT6357_VSIM2_ANA_CON0, 0xf00),
	MT6357_LDO("ldo-vusb33", VUSB33, vusb_voltages,
		   MT6357_LDO_VUSB33_CON0_0, MT6357_VUSB33_ANA_CON0, 0x700),
	MT6357_LDO("ldo-vxo22", VXO22, vxo22_voltages,
		   MT6357_LDO_VXO22_CON0, MT6357_VXO22_ANA_CON0, 0x300),

	MT6357_LDO1("ldo-vsram-proc", VSRAM_PROC, 518750, 1312500, 6250,
		   buck_volt_range1, MT6357_LDO_VSRAM_PROC_CON0,
		   MT6357_LDO_VSRAM_CON0, 0x7f00),
	MT6357_LDO1("ldo-vsram-others", VSRAM_OTHERS, 518750, 1312500, 6250,
		   buck_volt_range1, MT6357_LDO_VSRAM_OTHERS_CON0,
		   MT6357_LDO_VSRAM_CON1, 0x7f00),

	MT6357_REG_FIXED("ldo-vaud28", VAUD28, 2800000),
	MT6357_REG_FIXED("ldo-vaux18", VAUX18, 1800000),
	MT6357_REG_FIXED("ldo-vcamio18", VCAMIO, 1800000),
	MT6357_REG_FIXED("ldo-vcn18", VCN18, 1800000),
	MT6357_REG_FIXED("ldo-vcn28", VCN28, 2800000),
	MT6357_REG_FIXED("ldo-vfe28", VFE28, 2800000),
	MT6357_REG_FIXED("ldo-vio18", VIO18, 1800000),
	MT6357_REG_FIXED("ldo-vio28", VIO28, 2800000),
	MT6357_REG_FIXED("ldo-vrf12", VRF12, 1200000),
	MT6357_REG_FIXED("ldo-vrf18", VRF18, 1800000),
};

static int mt6357_regulator_probe(struct platform_device *pdev)
{
	struct mt6397_chip *mt6357 = dev_get_drvdata(pdev->dev.parent);
	struct regulator_config config = {};
	struct regulator_dev *rdev;
	int i;

	pdev->dev.of_node = pdev->dev.parent->of_node;

	for (i = 0; i < MT6357_MAX_REGULATOR; i++) {
		config.dev = &pdev->dev;
		config.driver_data = &mt6357_regulators[i];
		config.regmap = mt6357->regmap;

		rdev = devm_regulator_register(&pdev->dev,
					       &mt6357_regulators[i].desc,
					       &config);
		if (IS_ERR(rdev)) {
			dev_err(&pdev->dev, "failed to register %s\n",
				mt6357_regulators[i].desc.name);
			return PTR_ERR(rdev);
		}
	}

	return 0;
}

static const struct platform_device_id mt6357_platform_ids[] = {
	{ "mt6357-regulator" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(platform, mt6357_platform_ids);

static struct platform_driver mt6357_regulator_driver = {
	.driver = {
		.name = "mt6357-regulator",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.probe = mt6357_regulator_probe,
	.id_table = mt6357_platform_ids,
};

module_platform_driver(mt6357_regulator_driver);

MODULE_AUTHOR("Chen Zhong <chen.zhong@mediatek.com>");
MODULE_AUTHOR("Fabien Parent <fabien.parent@linaro.org>");
MODULE_AUTHOR("Alexandre Mergnat <amergnat@baylibre.com>");
MODULE_DESCRIPTION("Regulator Driver for MediaTek MT6357 PMIC");
MODULE_LICENSE("GPL");
