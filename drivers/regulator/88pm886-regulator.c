// SPDX-License-Identifier: GPL-2.0-only
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>

#include <linux/mfd/88pm886.h>

static const struct regmap_config pm886_regulator_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = PM886_REG_BUCK5_VOUT,
};

static const struct regulator_ops pm886_ldo_ops = {
	.list_voltage = regulator_list_voltage_table,
	.map_voltage = regulator_map_voltage_iterate,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
};

static const struct regulator_ops pm886_buck_ops = {
	.list_voltage = regulator_list_voltage_linear_range,
	.map_voltage = regulator_map_voltage_linear_range,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
};

static const unsigned int pm886_ldo_volt_table1[] = {
	1700000, 1800000, 1900000, 2500000, 2800000, 2900000, 3100000, 3300000,
};

static const unsigned int pm886_ldo_volt_table2[] = {
	1200000, 1250000, 1700000, 1800000, 1850000, 1900000, 2500000, 2600000,
	2700000, 2750000, 2800000, 2850000, 2900000, 3000000, 3100000, 3300000,
};

static const unsigned int pm886_ldo_volt_table3[] = {
	1700000, 1800000, 1900000, 2000000, 2100000, 2500000, 2700000, 2800000,
};

static const struct linear_range pm886_buck_volt_ranges1[] = {
	REGULATOR_LINEAR_RANGE(600000, 0, 79, 12500),
	REGULATOR_LINEAR_RANGE(1600000, 80, 84, 50000),
};

static const struct linear_range pm886_buck_volt_ranges2[] = {
	REGULATOR_LINEAR_RANGE(600000, 0, 79, 12500),
	REGULATOR_LINEAR_RANGE(1600000, 80, 114, 50000),
};

static struct regulator_desc pm886_regulators[] = {
	{
		.name = "LDO1",
		.regulators_node = "regulators",
		.of_match = "ldo1",
		.ops = &pm886_ldo_ops,
		.type = REGULATOR_VOLTAGE,
		.enable_reg = PM886_REG_LDO_EN1,
		.enable_mask = BIT(0),
		.volt_table = pm886_ldo_volt_table1,
		.n_voltages = ARRAY_SIZE(pm886_ldo_volt_table1),
		.vsel_reg = PM886_REG_LDO1_VOUT,
		.vsel_mask = PM886_LDO_VSEL_MASK,
	},
	{
		.name = "LDO2",
		.regulators_node = "regulators",
		.of_match = "ldo2",
		.ops = &pm886_ldo_ops,
		.type = REGULATOR_VOLTAGE,
		.enable_reg = PM886_REG_LDO_EN1,
		.enable_mask = BIT(1),
		.volt_table = pm886_ldo_volt_table1,
		.n_voltages = ARRAY_SIZE(pm886_ldo_volt_table1),
		.vsel_reg = PM886_REG_LDO2_VOUT,
		.vsel_mask = PM886_LDO_VSEL_MASK,
	},
	{
		.name = "LDO3",
		.regulators_node = "regulators",
		.of_match = "ldo3",
		.ops = &pm886_ldo_ops,
		.type = REGULATOR_VOLTAGE,
		.enable_reg = PM886_REG_LDO_EN1,
		.enable_mask = BIT(2),
		.volt_table = pm886_ldo_volt_table1,
		.n_voltages = ARRAY_SIZE(pm886_ldo_volt_table1),
		.vsel_reg = PM886_REG_LDO3_VOUT,
		.vsel_mask = PM886_LDO_VSEL_MASK,
	},
	{
		.name = "LDO4",
		.regulators_node = "regulators",
		.of_match = "ldo4",
		.ops = &pm886_ldo_ops,
		.type = REGULATOR_VOLTAGE,
		.enable_reg = PM886_REG_LDO_EN1,
		.enable_mask = BIT(3),
		.volt_table = pm886_ldo_volt_table2,
		.n_voltages = ARRAY_SIZE(pm886_ldo_volt_table2),
		.vsel_reg = PM886_REG_LDO4_VOUT,
		.vsel_mask = PM886_LDO_VSEL_MASK,
	},
	{
		.name = "LDO5",
		.regulators_node = "regulators",
		.of_match = "ldo5",
		.ops = &pm886_ldo_ops,
		.type = REGULATOR_VOLTAGE,
		.enable_reg = PM886_REG_LDO_EN1,
		.enable_mask = BIT(4),
		.volt_table = pm886_ldo_volt_table2,
		.n_voltages = ARRAY_SIZE(pm886_ldo_volt_table2),
		.vsel_reg = PM886_REG_LDO5_VOUT,
		.vsel_mask = PM886_LDO_VSEL_MASK,
	},
	{
		.name = "LDO6",
		.regulators_node = "regulators",
		.of_match = "ldo6",
		.ops = &pm886_ldo_ops,
		.type = REGULATOR_VOLTAGE,
		.enable_reg = PM886_REG_LDO_EN1,
		.enable_mask = BIT(5),
		.volt_table = pm886_ldo_volt_table2,
		.n_voltages = ARRAY_SIZE(pm886_ldo_volt_table2),
		.vsel_reg = PM886_REG_LDO6_VOUT,
		.vsel_mask = PM886_LDO_VSEL_MASK,
	},
	{
		.name = "LDO7",
		.regulators_node = "regulators",
		.of_match = "ldo7",
		.ops = &pm886_ldo_ops,
		.type = REGULATOR_VOLTAGE,
		.enable_reg = PM886_REG_LDO_EN1,
		.enable_mask = BIT(6),
		.volt_table = pm886_ldo_volt_table2,
		.n_voltages = ARRAY_SIZE(pm886_ldo_volt_table2),
		.vsel_reg = PM886_REG_LDO7_VOUT,
		.vsel_mask = PM886_LDO_VSEL_MASK,
	},
	{
		.name = "LDO8",
		.regulators_node = "regulators",
		.of_match = "ldo8",
		.ops = &pm886_ldo_ops,
		.type = REGULATOR_VOLTAGE,
		.enable_reg = PM886_REG_LDO_EN1,
		.enable_mask = BIT(7),
		.volt_table = pm886_ldo_volt_table2,
		.n_voltages = ARRAY_SIZE(pm886_ldo_volt_table2),
		.vsel_reg = PM886_REG_LDO8_VOUT,
		.vsel_mask = PM886_LDO_VSEL_MASK,
	},
	{
		.name = "LDO9",
		.regulators_node = "regulators",
		.of_match = "ldo9",
		.ops = &pm886_ldo_ops,
		.type = REGULATOR_VOLTAGE,
		.enable_reg = PM886_REG_LDO_EN2,
		.enable_mask = BIT(0),
		.volt_table = pm886_ldo_volt_table2,
		.n_voltages = ARRAY_SIZE(pm886_ldo_volt_table2),
		.vsel_reg = PM886_REG_LDO9_VOUT,
		.vsel_mask = PM886_LDO_VSEL_MASK,
	},
	{
		.name = "LDO10",
		.regulators_node = "regulators",
		.of_match = "ldo10",
		.ops = &pm886_ldo_ops,
		.type = REGULATOR_VOLTAGE,
		.enable_reg = PM886_REG_LDO_EN2,
		.enable_mask = BIT(1),
		.volt_table = pm886_ldo_volt_table2,
		.n_voltages = ARRAY_SIZE(pm886_ldo_volt_table2),
		.vsel_reg = PM886_REG_LDO10_VOUT,
		.vsel_mask = PM886_LDO_VSEL_MASK,
	},
	{
		.name = "LDO11",
		.regulators_node = "regulators",
		.of_match = "ldo11",
		.ops = &pm886_ldo_ops,
		.type = REGULATOR_VOLTAGE,
		.enable_reg = PM886_REG_LDO_EN2,
		.enable_mask = BIT(2),
		.volt_table = pm886_ldo_volt_table2,
		.n_voltages = ARRAY_SIZE(pm886_ldo_volt_table2),
		.vsel_reg = PM886_REG_LDO11_VOUT,
		.vsel_mask = PM886_LDO_VSEL_MASK,
	},
	{
		.name = "LDO12",
		.regulators_node = "regulators",
		.of_match = "ldo12",
		.ops = &pm886_ldo_ops,
		.type = REGULATOR_VOLTAGE,
		.enable_reg = PM886_REG_LDO_EN2,
		.enable_mask = BIT(3),
		.volt_table = pm886_ldo_volt_table2,
		.n_voltages = ARRAY_SIZE(pm886_ldo_volt_table2),
		.vsel_reg = PM886_REG_LDO12_VOUT,
		.vsel_mask = PM886_LDO_VSEL_MASK,
	},
	{
		.name = "LDO13",
		.regulators_node = "regulators",
		.of_match = "ldo13",
		.ops = &pm886_ldo_ops,
		.type = REGULATOR_VOLTAGE,
		.enable_reg = PM886_REG_LDO_EN2,
		.enable_mask = BIT(4),
		.volt_table = pm886_ldo_volt_table2,
		.n_voltages = ARRAY_SIZE(pm886_ldo_volt_table2),
		.vsel_reg = PM886_REG_LDO13_VOUT,
		.vsel_mask = PM886_LDO_VSEL_MASK,
	},
	{
		.name = "LDO14",
		.regulators_node = "regulators",
		.of_match = "ldo14",
		.ops = &pm886_ldo_ops,
		.type = REGULATOR_VOLTAGE,
		.enable_reg = PM886_REG_LDO_EN2,
		.enable_mask = BIT(5),
		.volt_table = pm886_ldo_volt_table2,
		.n_voltages = ARRAY_SIZE(pm886_ldo_volt_table2),
		.vsel_reg = PM886_REG_LDO14_VOUT,
		.vsel_mask = PM886_LDO_VSEL_MASK,
	},
	{
		.name = "LDO15",
		.regulators_node = "regulators",
		.of_match = "ldo15",
		.ops = &pm886_ldo_ops,
		.type = REGULATOR_VOLTAGE,
		.enable_reg = PM886_REG_LDO_EN2,
		.enable_mask = BIT(6),
		.volt_table = pm886_ldo_volt_table2,
		.n_voltages = ARRAY_SIZE(pm886_ldo_volt_table2),
		.vsel_reg = PM886_REG_LDO15_VOUT,
		.vsel_mask = PM886_LDO_VSEL_MASK,
	},
	{
		.name = "LDO16",
		.regulators_node = "regulators",
		.of_match = "ldo16",
		.ops = &pm886_ldo_ops,
		.type = REGULATOR_VOLTAGE,
		.enable_reg = PM886_REG_LDO_EN2,
		.enable_mask = BIT(7),
		.volt_table = pm886_ldo_volt_table3,
		.n_voltages = ARRAY_SIZE(pm886_ldo_volt_table3),
		.vsel_reg = PM886_REG_LDO16_VOUT,
		.vsel_mask = PM886_LDO_VSEL_MASK,
	},
	{
		.name = "buck1",
		.regulators_node = "regulators",
		.of_match = "buck1",
		.ops = &pm886_buck_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = 85,
		.linear_ranges = pm886_buck_volt_ranges1,
		.n_linear_ranges = ARRAY_SIZE(pm886_buck_volt_ranges1),
		.vsel_reg = PM886_REG_BUCK1_VOUT,
		.vsel_mask = PM886_BUCK_VSEL_MASK,
		.enable_reg = PM886_REG_BUCK_EN,
		.enable_mask = BIT(0),
	},
	{
		.name = "buck2",
		.regulators_node = "regulators",
		.of_match = "buck2",
		.ops = &pm886_buck_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = 115,
		.linear_ranges = pm886_buck_volt_ranges2,
		.n_linear_ranges = ARRAY_SIZE(pm886_buck_volt_ranges2),
		.vsel_reg = PM886_REG_BUCK2_VOUT,
		.vsel_mask = PM886_BUCK_VSEL_MASK,
		.enable_reg = PM886_REG_BUCK_EN,
		.enable_mask = BIT(1),
	},
	{
		.name = "buck3",
		.regulators_node = "regulators",
		.of_match = "buck3",
		.ops = &pm886_buck_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = 115,
		.linear_ranges = pm886_buck_volt_ranges2,
		.n_linear_ranges = ARRAY_SIZE(pm886_buck_volt_ranges2),
		.vsel_reg = PM886_REG_BUCK3_VOUT,
		.vsel_mask = PM886_BUCK_VSEL_MASK,
		.enable_reg = PM886_REG_BUCK_EN,
		.enable_mask = BIT(2),
	},
	{
		.name = "buck4",
		.regulators_node = "regulators",
		.of_match = "buck4",
		.ops = &pm886_buck_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = 115,
		.linear_ranges = pm886_buck_volt_ranges2,
		.n_linear_ranges = ARRAY_SIZE(pm886_buck_volt_ranges2),
		.vsel_reg = PM886_REG_BUCK4_VOUT,
		.vsel_mask = PM886_BUCK_VSEL_MASK,
		.enable_reg = PM886_REG_BUCK_EN,
		.enable_mask = BIT(3),
	},
	{
		.name = "buck5",
		.regulators_node = "regulators",
		.of_match = "buck5",
		.ops = &pm886_buck_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = 115,
		.linear_ranges = pm886_buck_volt_ranges2,
		.n_linear_ranges = ARRAY_SIZE(pm886_buck_volt_ranges2),
		.vsel_reg = PM886_REG_BUCK5_VOUT,
		.vsel_mask = PM886_BUCK_VSEL_MASK,
		.enable_reg = PM886_REG_BUCK_EN,
		.enable_mask = BIT(4),
	},
};

static int pm886_regulator_probe(struct platform_device *pdev)
{
	struct pm886_chip *chip = dev_get_drvdata(pdev->dev.parent);
	struct regulator_config rcfg = { };
	struct device *dev = &pdev->dev;
	struct regulator_desc *rdesc;
	struct regulator_dev *rdev;
	struct i2c_client *page;
	struct regmap *regmap;

	page = devm_i2c_new_dummy_device(dev, chip->client->adapter,
			chip->client->addr + PM886_PAGE_OFFSET_REGULATORS);
	if (IS_ERR(page))
		return dev_err_probe(dev, PTR_ERR(page),
				"Failed to initialize regulators client\n");

	regmap = devm_regmap_init_i2c(page, &pm886_regulator_regmap_config);
	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap),
				"Failed to initialize regulators regmap\n");
	rcfg.regmap = regmap;

	rcfg.dev = dev->parent;

	for (int i = 0; i < ARRAY_SIZE(pm886_regulators); i++) {
		rdesc = &pm886_regulators[i];
		rdev = devm_regulator_register(dev, rdesc, &rcfg);
		if (IS_ERR(rdev))
			return dev_err_probe(dev, PTR_ERR(rdev),
					"Failed to register %s\n", rdesc->name);
	}

	return 0;
}

static const struct platform_device_id pm886_regulator_id_table[] = {
	{ "88pm886-regulator", },
	{ }
};
MODULE_DEVICE_TABLE(platform, pm886_regulator_id_table);

static struct platform_driver pm886_regulator_driver = {
	.driver = {
		.name = "88pm886-regulator",
	},
	.probe = pm886_regulator_probe,
	.id_table = pm886_regulator_id_table,
};
module_platform_driver(pm886_regulator_driver);

MODULE_DESCRIPTION("Marvell 88PM886 PMIC regulator driver");
MODULE_AUTHOR("Karel Balej <balejk@matfyz.cz>");
MODULE_LICENSE("GPL");
