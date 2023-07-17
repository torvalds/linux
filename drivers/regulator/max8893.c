// SPDX-License-Identifier: GPL-2.0+
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>

static const struct regulator_ops max8893_ops = {
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
};

static const struct regulator_desc max8893_regulators[] = {
	{
		.name = "BUCK",
		.supply_name = "in-buck",
		.of_match = of_match_ptr("buck"),
		.regulators_node = of_match_ptr("regulators"),
		.n_voltages = 0x11,
		.id = 6,
		.ops = &max8893_ops,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
		.min_uV = 800000,
		.uV_step = 100000,
		.vsel_reg = 0x4,
		.vsel_mask = 0x1f,
		.enable_reg = 0x0,
		.enable_mask = BIT(7),
	},
	{
		.name = "LDO1",
		.supply_name = "in-ldo1",
		.of_match = of_match_ptr("ldo1"),
		.regulators_node = of_match_ptr("regulators"),
		.n_voltages = 0x12,
		.id = 1,
		.ops = &max8893_ops,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
		.min_uV = 1600000,
		.uV_step = 100000,
		.vsel_reg = 0x5,
		.vsel_mask = 0x1f,
		.enable_reg = 0x0,
		.enable_mask = BIT(5),
	},
	{
		.name = "LDO2",
		.supply_name = "in-ldo2",
		.of_match = of_match_ptr("ldo2"),
		.regulators_node = of_match_ptr("regulators"),
		.n_voltages = 0x16,
		.id = 2,
		.ops = &max8893_ops,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
		.min_uV = 1200000,
		.uV_step = 100000,
		.vsel_reg = 0x6,
		.vsel_mask = 0x1f,
		.enable_reg = 0x0,
		.enable_mask = BIT(4),
	},
	{
		.name = "LDO3",
		.supply_name = "in-ldo3",
		.of_match = of_match_ptr("ldo3"),
		.regulators_node = of_match_ptr("regulators"),
		.n_voltages = 0x12,
		.id = 3,
		.ops = &max8893_ops,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
		.min_uV = 1600000,
		.uV_step = 100000,
		.vsel_reg = 0x7,
		.vsel_mask = 0x1f,
		.enable_reg = 0x0,
		.enable_mask = BIT(3),
	},
	{
		.name = "LDO4",
		.supply_name = "in-ldo4",
		.of_match = of_match_ptr("ldo4"),
		.regulators_node = of_match_ptr("regulators"),
		.n_voltages = 0x1a,
		.id = 4,
		.ops = &max8893_ops,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
		.min_uV = 800000,
		.uV_step = 100000,
		.vsel_reg = 0x8,
		.vsel_mask = 0x1f,
		.enable_reg = 0x0,
		.enable_mask = BIT(2),
	},
	{
		.name = "LDO5",
		.supply_name = "in-ldo5",
		.of_match = of_match_ptr("ldo5"),
		.regulators_node = of_match_ptr("regulators"),
		.n_voltages = 0x1a,
		.id = 5,
		.ops = &max8893_ops,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
		.min_uV = 800000,
		.uV_step = 100000,
		.vsel_reg = 0x9,
		.vsel_mask = 0x1f,
		.enable_reg = 0x0,
		.enable_mask = BIT(1),
	}
};

static const struct regmap_config max8893_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
};

static int max8893_probe_new(struct i2c_client *i2c)
{
	int id, ret;
	struct regulator_config config = {.dev = &i2c->dev};
	struct regmap *regmap = devm_regmap_init_i2c(i2c, &max8893_regmap);

	if (IS_ERR(regmap)) {
		ret = PTR_ERR(regmap);
		dev_err(&i2c->dev, "regmap init failed: %d\n", ret);
		return ret;
	}

	for (id = 0; id < ARRAY_SIZE(max8893_regulators); id++) {
		struct regulator_dev *rdev;
		rdev = devm_regulator_register(&i2c->dev,
					       &max8893_regulators[id],
					       &config);
		if (IS_ERR(rdev)) {
			ret = PTR_ERR(rdev);
			dev_err(&i2c->dev, "failed to register %s: %d\n",
				max8893_regulators[id].name, ret);
			return ret;
		}
	}

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id max8893_dt_match[] = {
	{ .compatible = "maxim,max8893" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, max8893_dt_match);
#endif

static const struct i2c_device_id max8893_ids[] = {
	{ "max8893", 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, max8893_ids);

static struct i2c_driver max8893_driver = {
	.probe_new	= max8893_probe_new,
	.driver		= {
		.name	= "max8893",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table = of_match_ptr(max8893_dt_match),
	},
	.id_table	= max8893_ids,
};

module_i2c_driver(max8893_driver);

MODULE_DESCRIPTION("Maxim MAX8893 PMIC driver");
MODULE_AUTHOR("Sergey Larin <cerg2010cerg2010@mail.ru>");
MODULE_LICENSE("GPL");
