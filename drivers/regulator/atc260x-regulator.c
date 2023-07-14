// SPDX-License-Identifier: GPL-2.0+
//
// Regulator driver for ATC260x PMICs
//
// Copyright (C) 2019 Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>
// Copyright (C) 2020 Cristian Ciocaltea <cristian.ciocaltea@gmail.com>

#include <linux/mfd/atc260x/core.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>

struct atc260x_regulator_data {
	int voltage_time_dcdc;
	int voltage_time_ldo;
};

static const struct linear_range atc2603c_dcdc_voltage_ranges[] = {
	REGULATOR_LINEAR_RANGE(1300000, 0, 13, 50000),
	REGULATOR_LINEAR_RANGE(1950000, 14, 15, 100000),
};

static const struct linear_range atc2609a_dcdc_voltage_ranges[] = {
	REGULATOR_LINEAR_RANGE(600000, 0, 127, 6250),
	REGULATOR_LINEAR_RANGE(1400000, 128, 232, 25000),
};

static const struct linear_range atc2609a_ldo_voltage_ranges0[] = {
	REGULATOR_LINEAR_RANGE(700000, 0, 15, 100000),
	REGULATOR_LINEAR_RANGE(2100000, 0, 12, 100000),
};

static const struct linear_range atc2609a_ldo_voltage_ranges1[] = {
	REGULATOR_LINEAR_RANGE(850000, 0, 15, 100000),
	REGULATOR_LINEAR_RANGE(2100000, 0, 11, 100000),
};

static const unsigned int atc260x_ldo_voltage_range_sel[] = {
	0x0, 0x20,
};

static int atc260x_dcdc_set_voltage_time_sel(struct regulator_dev *rdev,
					     unsigned int old_selector,
					     unsigned int new_selector)
{
	struct atc260x_regulator_data *data = rdev_get_drvdata(rdev);

	if (new_selector > old_selector)
		return data->voltage_time_dcdc;

	return 0;
}

static int atc260x_ldo_set_voltage_time_sel(struct regulator_dev *rdev,
					    unsigned int old_selector,
					    unsigned int new_selector)
{
	struct atc260x_regulator_data *data = rdev_get_drvdata(rdev);

	if (new_selector > old_selector)
		return data->voltage_time_ldo;

	return 0;
}

static const struct regulator_ops atc260x_dcdc_ops = {
	.enable	= regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = regulator_list_voltage_linear,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_time_sel = atc260x_dcdc_set_voltage_time_sel,
};

static const struct regulator_ops atc260x_ldo_ops = {
	.enable	= regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = regulator_list_voltage_linear,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_time_sel = atc260x_ldo_set_voltage_time_sel,
};

static const struct regulator_ops atc260x_ldo_bypass_ops = {
	.enable	= regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = regulator_list_voltage_linear,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_time_sel = atc260x_ldo_set_voltage_time_sel,
	.set_bypass = regulator_set_bypass_regmap,
	.get_bypass = regulator_get_bypass_regmap,
};

static const struct regulator_ops atc260x_ldo_bypass_discharge_ops = {
	.enable	= regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = regulator_list_voltage_linear,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_time_sel = atc260x_ldo_set_voltage_time_sel,
	.set_bypass = regulator_set_bypass_regmap,
	.get_bypass = regulator_get_bypass_regmap,
	.set_active_discharge = regulator_set_active_discharge_regmap,
};

static const struct regulator_ops atc260x_dcdc_range_ops = {
	.enable	= regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = regulator_list_voltage_linear_range,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_time_sel = atc260x_dcdc_set_voltage_time_sel,
};

static const struct regulator_ops atc260x_ldo_range_pick_ops = {
	.enable	= regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = regulator_list_voltage_pickable_linear_range,
	.set_voltage_sel = regulator_set_voltage_sel_pickable_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_pickable_regmap,
	.set_voltage_time_sel = atc260x_ldo_set_voltage_time_sel,
};

static const struct regulator_ops atc260x_dcdc_fixed_ops = {
	.list_voltage = regulator_list_voltage_linear,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_time_sel = atc260x_dcdc_set_voltage_time_sel,
};

static const struct regulator_ops atc260x_ldo_fixed_ops = {
	.list_voltage = regulator_list_voltage_linear,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_time_sel = atc260x_ldo_set_voltage_time_sel,
};

static const struct regulator_ops atc260x_no_ops = {
};

/*
 * Note LDO8 is not documented in datasheet (v2.4), but supported
 * in the vendor's driver implementation (xapp-le-kernel).
 */
enum atc2603c_reg_ids {
	ATC2603C_ID_DCDC1,
	ATC2603C_ID_DCDC2,
	ATC2603C_ID_DCDC3,
	ATC2603C_ID_LDO1,
	ATC2603C_ID_LDO2,
	ATC2603C_ID_LDO3,
	ATC2603C_ID_LDO5,
	ATC2603C_ID_LDO6,
	ATC2603C_ID_LDO7,
	ATC2603C_ID_LDO8,
	ATC2603C_ID_LDO11,
	ATC2603C_ID_LDO12,
	ATC2603C_ID_SWITCHLDO1,
	ATC2603C_ID_MAX,
};

#define atc2603c_reg_desc_dcdc(num, min, step, n_volt, vsel_h, vsel_l) { \
	.name = "DCDC"#num, \
	.supply_name = "dcdc"#num, \
	.of_match = of_match_ptr("dcdc"#num), \
	.regulators_node = of_match_ptr("regulators"), \
	.id = ATC2603C_ID_DCDC##num, \
	.ops = &atc260x_dcdc_ops, \
	.type = REGULATOR_VOLTAGE, \
	.min_uV = min, \
	.uV_step = step, \
	.n_voltages = n_volt, \
	.vsel_reg = ATC2603C_PMU_DC##num##_CTL0, \
	.vsel_mask = GENMASK(vsel_h, vsel_l), \
	.enable_reg = ATC2603C_PMU_DC##num##_CTL0, \
	.enable_mask = BIT(15), \
	.enable_time = 800, \
	.owner = THIS_MODULE, \
}

#define atc2603c_reg_desc_dcdc_range(num, vsel_h, vsel_l) { \
	.name = "DCDC"#num, \
	.supply_name = "dcdc"#num, \
	.of_match = of_match_ptr("dcdc"#num), \
	.regulators_node = of_match_ptr("regulators"), \
	.id = ATC2603C_ID_DCDC##num, \
	.ops = &atc260x_dcdc_range_ops, \
	.type = REGULATOR_VOLTAGE, \
	.n_voltages = 16, \
	.linear_ranges = atc2603c_dcdc_voltage_ranges, \
	.n_linear_ranges = ARRAY_SIZE(atc2603c_dcdc_voltage_ranges), \
	.vsel_reg = ATC2603C_PMU_DC##num##_CTL0, \
	.vsel_mask = GENMASK(vsel_h, vsel_l), \
	.enable_reg = ATC2603C_PMU_DC##num##_CTL0, \
	.enable_mask = BIT(15), \
	.enable_time = 800, \
	.owner = THIS_MODULE, \
}

#define atc2603c_reg_desc_dcdc_fixed(num, min, step, n_volt, vsel_h, vsel_l) { \
	.name = "DCDC"#num, \
	.supply_name = "dcdc"#num, \
	.of_match = of_match_ptr("dcdc"#num), \
	.regulators_node = of_match_ptr("regulators"), \
	.id = ATC2603C_ID_DCDC##num, \
	.ops = &atc260x_dcdc_fixed_ops, \
	.type = REGULATOR_VOLTAGE, \
	.min_uV = min, \
	.uV_step = step, \
	.n_voltages = n_volt, \
	.vsel_reg = ATC2603C_PMU_DC##num##_CTL0, \
	.vsel_mask = GENMASK(vsel_h, vsel_l), \
	.enable_time = 800, \
	.owner = THIS_MODULE, \
}

#define atc2603c_reg_desc_ldo(num, min, step, n_volt, vsel_h, vsel_l) { \
	.name = "LDO"#num, \
	.supply_name = "ldo"#num, \
	.of_match = of_match_ptr("ldo"#num), \
	.regulators_node = of_match_ptr("regulators"), \
	.id = ATC2603C_ID_LDO##num, \
	.ops = &atc260x_ldo_ops, \
	.type = REGULATOR_VOLTAGE, \
	.min_uV = min, \
	.uV_step = step, \
	.n_voltages = n_volt, \
	.vsel_reg = ATC2603C_PMU_LDO##num##_CTL, \
	.vsel_mask = GENMASK(vsel_h, vsel_l), \
	.enable_reg = ATC2603C_PMU_LDO##num##_CTL, \
	.enable_mask = BIT(0), \
	.enable_time = 2000, \
	.owner = THIS_MODULE, \
}

#define atc2603c_reg_desc_ldo_fixed(num, min, step, n_volt, vsel_h, vsel_l) { \
	.name = "LDO"#num, \
	.supply_name = "ldo"#num, \
	.of_match = of_match_ptr("ldo"#num), \
	.regulators_node = of_match_ptr("regulators"), \
	.id = ATC2603C_ID_LDO##num, \
	.ops = &atc260x_ldo_fixed_ops, \
	.type = REGULATOR_VOLTAGE, \
	.min_uV = min, \
	.uV_step = step, \
	.n_voltages = n_volt, \
	.vsel_reg = ATC2603C_PMU_LDO##num##_CTL, \
	.vsel_mask = GENMASK(vsel_h, vsel_l), \
	.enable_time = 2000, \
	.owner = THIS_MODULE, \
}

#define atc2603c_reg_desc_ldo_noops(num, vfixed) { \
	.name = "LDO"#num, \
	.supply_name = "ldo"#num, \
	.of_match = of_match_ptr("ldo"#num), \
	.regulators_node = of_match_ptr("regulators"), \
	.id = ATC2603C_ID_LDO##num, \
	.ops = &atc260x_no_ops, \
	.type = REGULATOR_VOLTAGE, \
	.fixed_uV = vfixed, \
	.n_voltages = 1, \
	.owner = THIS_MODULE, \
}

#define atc2603c_reg_desc_ldo_switch(num, min, step, n_volt, vsel_h, vsel_l) { \
	.name = "SWITCHLDO"#num, \
	.supply_name = "switchldo"#num, \
	.of_match = of_match_ptr("switchldo"#num), \
	.regulators_node = of_match_ptr("regulators"), \
	.id = ATC2603C_ID_SWITCHLDO##num, \
	.ops = &atc260x_ldo_bypass_discharge_ops, \
	.type = REGULATOR_VOLTAGE, \
	.min_uV = min, \
	.uV_step = step, \
	.n_voltages = n_volt, \
	.vsel_reg = ATC2603C_PMU_SWITCH_CTL, \
	.vsel_mask = GENMASK(vsel_h, vsel_l), \
	.enable_reg = ATC2603C_PMU_SWITCH_CTL, \
	.enable_mask = BIT(15), \
	.enable_is_inverted = true, \
	.enable_time = 2000, \
	.bypass_reg = ATC2603C_PMU_SWITCH_CTL, \
	.bypass_mask = BIT(5), \
	.active_discharge_reg = ATC2603C_PMU_SWITCH_CTL, \
	.active_discharge_mask = BIT(1), \
	.active_discharge_on = BIT(1), \
	.owner = THIS_MODULE, \
}

static const struct regulator_desc atc2603c_reg[] = {
	atc2603c_reg_desc_dcdc_fixed(1, 700000, 25000, 29, 11, 7),
	atc2603c_reg_desc_dcdc_range(2, 12, 8),
	atc2603c_reg_desc_dcdc_fixed(3, 2600000, 100000, 8, 11, 9),
	atc2603c_reg_desc_ldo_fixed(1, 2600000, 100000, 8, 15, 13),
	atc2603c_reg_desc_ldo_fixed(2, 2600000, 100000, 8, 15, 13),
	atc2603c_reg_desc_ldo_fixed(3, 1500000, 100000, 6, 15, 13),
	atc2603c_reg_desc_ldo(5, 2600000, 100000, 8, 15, 13),
	atc2603c_reg_desc_ldo_fixed(6, 700000, 25000, 29, 15, 11),
	atc2603c_reg_desc_ldo(7, 1500000, 100000, 6, 15, 13),
	atc2603c_reg_desc_ldo(8, 2300000, 100000, 11, 15, 12),
	atc2603c_reg_desc_ldo_fixed(11, 2600000, 100000, 8, 15, 13),
	atc2603c_reg_desc_ldo_noops(12, 1800000),
	atc2603c_reg_desc_ldo_switch(1, 3000000, 100000, 4, 4, 3),
};

static const struct regulator_desc atc2603c_reg_dcdc2_ver_b =
	atc2603c_reg_desc_dcdc(2, 1000000, 50000, 18, 12, 8);

enum atc2609a_reg_ids {
	ATC2609A_ID_DCDC0,
	ATC2609A_ID_DCDC1,
	ATC2609A_ID_DCDC2,
	ATC2609A_ID_DCDC3,
	ATC2609A_ID_DCDC4,
	ATC2609A_ID_LDO0,
	ATC2609A_ID_LDO1,
	ATC2609A_ID_LDO2,
	ATC2609A_ID_LDO3,
	ATC2609A_ID_LDO4,
	ATC2609A_ID_LDO5,
	ATC2609A_ID_LDO6,
	ATC2609A_ID_LDO7,
	ATC2609A_ID_LDO8,
	ATC2609A_ID_LDO9,
	ATC2609A_ID_MAX,
};

#define atc2609a_reg_desc_dcdc(num, en_bit) { \
	.name = "DCDC"#num, \
	.supply_name = "dcdc"#num, \
	.of_match = of_match_ptr("dcdc"#num), \
	.regulators_node = of_match_ptr("regulators"), \
	.id = ATC2609A_ID_DCDC##num, \
	.ops = &atc260x_dcdc_ops, \
	.type = REGULATOR_VOLTAGE, \
	.min_uV = 600000, \
	.uV_step = 6250, \
	.n_voltages = 256, \
	.vsel_reg = ATC2609A_PMU_DC##num##_CTL0, \
	.vsel_mask = GENMASK(15, 8), \
	.enable_reg = ATC2609A_PMU_DC_OSC, \
	.enable_mask = BIT(en_bit), \
	.enable_time = 800, \
	.owner = THIS_MODULE, \
}

#define atc2609a_reg_desc_dcdc_range(num, en_bit) { \
	.name = "DCDC"#num, \
	.supply_name = "dcdc"#num, \
	.of_match = of_match_ptr("dcdc"#num), \
	.regulators_node = of_match_ptr("regulators"), \
	.id = ATC2609A_ID_DCDC##num, \
	.ops = &atc260x_dcdc_range_ops, \
	.type = REGULATOR_VOLTAGE, \
	.n_voltages = 233, \
	.linear_ranges = atc2609a_dcdc_voltage_ranges, \
	.n_linear_ranges = ARRAY_SIZE(atc2609a_dcdc_voltage_ranges), \
	.vsel_reg = ATC2609A_PMU_DC##num##_CTL0, \
	.vsel_mask = GENMASK(15, 8), \
	.enable_reg = ATC2609A_PMU_DC_OSC, \
	.enable_mask = BIT(en_bit), \
	.enable_time = 800, \
	.owner = THIS_MODULE, \
}

#define atc2609a_reg_desc_ldo(num) { \
	.name = "LDO"#num, \
	.supply_name = "ldo"#num, \
	.of_match = of_match_ptr("ldo"#num), \
	.regulators_node = of_match_ptr("regulators"), \
	.id = ATC2609A_ID_LDO##num, \
	.ops = &atc260x_ldo_ops, \
	.type = REGULATOR_VOLTAGE, \
	.min_uV = 700000, \
	.uV_step = 100000, \
	.n_voltages = 16, \
	.vsel_reg = ATC2609A_PMU_LDO##num##_CTL0, \
	.vsel_mask = GENMASK(4, 1), \
	.enable_reg = ATC2609A_PMU_LDO##num##_CTL0, \
	.enable_mask = BIT(0), \
	.enable_time = 2000, \
	.owner = THIS_MODULE, \
}

#define atc2609a_reg_desc_ldo_bypass(num) { \
	.name = "LDO"#num, \
	.supply_name = "ldo"#num, \
	.of_match = of_match_ptr("ldo"#num), \
	.regulators_node = of_match_ptr("regulators"), \
	.id = ATC2609A_ID_LDO##num, \
	.ops = &atc260x_ldo_bypass_ops, \
	.type = REGULATOR_VOLTAGE, \
	.min_uV = 2300000, \
	.uV_step = 100000, \
	.n_voltages = 12, \
	.vsel_reg = ATC2609A_PMU_LDO##num##_CTL0, \
	.vsel_mask = GENMASK(5, 2), \
	.enable_reg = ATC2609A_PMU_LDO##num##_CTL0, \
	.enable_mask = BIT(0), \
	.enable_time = 2000, \
	.bypass_reg = ATC2609A_PMU_LDO##num##_CTL0, \
	.bypass_mask = BIT(1), \
	.owner = THIS_MODULE, \
}

#define atc2609a_reg_desc_ldo_range_pick(num, n_range, n_volt) { \
	.name = "LDO"#num, \
	.supply_name = "ldo"#num, \
	.of_match = of_match_ptr("ldo"#num), \
	.regulators_node = of_match_ptr("regulators"), \
	.id = ATC2609A_ID_LDO##num, \
	.ops = &atc260x_ldo_range_pick_ops, \
	.type = REGULATOR_VOLTAGE, \
	.linear_ranges = atc2609a_ldo_voltage_ranges##n_range, \
	.n_linear_ranges = ARRAY_SIZE(atc2609a_ldo_voltage_ranges##n_range), \
	.n_voltages = n_volt, \
	.vsel_reg = ATC2609A_PMU_LDO##num##_CTL0, \
	.vsel_mask = GENMASK(4, 1), \
	.vsel_range_reg = ATC2609A_PMU_LDO##num##_CTL0, \
	.vsel_range_mask = BIT(5), \
	.linear_range_selectors = atc260x_ldo_voltage_range_sel, \
	.enable_reg = ATC2609A_PMU_LDO##num##_CTL0, \
	.enable_mask = BIT(0), \
	.enable_time = 2000, \
	.owner = THIS_MODULE, \
}

#define atc2609a_reg_desc_ldo_fixed(num) { \
	.name = "LDO"#num, \
	.supply_name = "ldo"#num, \
	.of_match = of_match_ptr("ldo"#num), \
	.regulators_node = of_match_ptr("regulators"), \
	.id = ATC2609A_ID_LDO##num, \
	.ops = &atc260x_ldo_fixed_ops, \
	.type = REGULATOR_VOLTAGE, \
	.min_uV = 2600000, \
	.uV_step = 100000, \
	.n_voltages = 8, \
	.vsel_reg = ATC2609A_PMU_LDO##num##_CTL, \
	.vsel_mask = GENMASK(15, 13), \
	.enable_time = 2000, \
	.owner = THIS_MODULE, \
}

static const struct regulator_desc atc2609a_reg[] = {
	atc2609a_reg_desc_dcdc(0, 4),
	atc2609a_reg_desc_dcdc(1, 5),
	atc2609a_reg_desc_dcdc(2, 6),
	atc2609a_reg_desc_dcdc_range(3, 7),
	atc2609a_reg_desc_dcdc(4, 8),
	atc2609a_reg_desc_ldo_bypass(0),
	atc2609a_reg_desc_ldo_bypass(1),
	atc2609a_reg_desc_ldo_bypass(2),
	atc2609a_reg_desc_ldo_range_pick(3, 0, 29),
	atc2609a_reg_desc_ldo_range_pick(4, 0, 29),
	atc2609a_reg_desc_ldo(5),
	atc2609a_reg_desc_ldo_range_pick(6, 1, 28),
	atc2609a_reg_desc_ldo_range_pick(7, 0, 29),
	atc2609a_reg_desc_ldo_range_pick(8, 0, 29),
	atc2609a_reg_desc_ldo_fixed(9),
};

static int atc260x_regulator_probe(struct platform_device *pdev)
{
	struct atc260x *atc260x = dev_get_drvdata(pdev->dev.parent);
	struct device *dev = atc260x->dev;
	struct atc260x_regulator_data *atc260x_data;
	struct regulator_config config = {};
	struct regulator_dev *atc260x_rdev;
	const struct regulator_desc *regulators;
	bool atc2603c_ver_b = false;
	int i, nregulators;

	atc260x_data = devm_kzalloc(&pdev->dev, sizeof(*atc260x_data), GFP_KERNEL);
	if (!atc260x_data)
		return -ENOMEM;

	atc260x_data->voltage_time_dcdc = 350;
	atc260x_data->voltage_time_ldo = 800;

	switch (atc260x->ic_type) {
	case ATC2603C:
		regulators = atc2603c_reg;
		nregulators = ATC2603C_ID_MAX;
		atc2603c_ver_b = atc260x->ic_ver == ATC260X_B;
		break;
	case ATC2609A:
		atc260x_data->voltage_time_dcdc = 250;
		regulators = atc2609a_reg;
		nregulators = ATC2609A_ID_MAX;
		break;
	default:
		dev_err(dev, "unsupported ATC260X ID %d\n", atc260x->ic_type);
		return -EINVAL;
	}

	config.dev = dev;
	config.regmap = atc260x->regmap;
	config.driver_data = atc260x_data;

	/* Instantiate the regulators */
	for (i = 0; i < nregulators; i++) {
		if (atc2603c_ver_b && regulators[i].id == ATC2603C_ID_DCDC2)
			atc260x_rdev = devm_regulator_register(&pdev->dev,
							       &atc2603c_reg_dcdc2_ver_b,
							       &config);
		else
			atc260x_rdev = devm_regulator_register(&pdev->dev,
							       &regulators[i],
							       &config);
		if (IS_ERR(atc260x_rdev)) {
			dev_err(dev, "failed to register regulator: %d\n", i);
			return PTR_ERR(atc260x_rdev);
		}
	}

	return 0;
}

static struct platform_driver atc260x_regulator_driver = {
	.probe = atc260x_regulator_probe,
	.driver = {
		.name = "atc260x-regulator",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
};

module_platform_driver(atc260x_regulator_driver);

MODULE_DESCRIPTION("Regulator driver for ATC260x PMICs");
MODULE_AUTHOR("Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>");
MODULE_AUTHOR("Cristian Ciocaltea <cristian.ciocaltea@gmail.com>");
MODULE_LICENSE("GPL");
