// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/regulator/driver.h>
#include <linux/regmap.h>

static const struct regulator_ops pg86x_ops = {
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.list_voltage = regulator_list_voltage_linear_range,
};

static const struct linear_range pg86x_buck1_ranges[] = {
	REGULATOR_LINEAR_RANGE(      0,  0, 10,     0),
	REGULATOR_LINEAR_RANGE(1000000, 11, 34, 25000),
	REGULATOR_LINEAR_RANGE(1600000, 35, 47, 50000),
};

static const struct linear_range pg86x_buck2_ranges[] = {
	REGULATOR_LINEAR_RANGE(      0,  0, 15,     0),
	REGULATOR_LINEAR_RANGE(1000000, 16, 39, 25000),
	REGULATOR_LINEAR_RANGE(1600000, 40, 52, 50000),
};

static const struct regulator_desc pg86x_regulators[] = {
	{
		.id = 0,
		.type = REGULATOR_VOLTAGE,
		.name = "buck1",
		.of_match = of_match_ptr("buck1"),
		.n_voltages = 11 + 24 + 13,
		.linear_ranges = pg86x_buck1_ranges,
		.n_linear_ranges = 3,
		.vsel_reg  = 0x24,
		.vsel_mask = 0xff,
		.ops = &pg86x_ops,
		.owner = THIS_MODULE
	},
	{
		.id = 1,
		.type = REGULATOR_VOLTAGE,
		.name = "buck2",
		.of_match = of_match_ptr("buck2"),
		.n_voltages = 16 + 24 + 13,
		.linear_ranges = pg86x_buck2_ranges,
		.n_linear_ranges = 3,
		.vsel_reg  = 0x13,
		.vsel_mask = 0xff,
		.ops = &pg86x_ops,
		.owner = THIS_MODULE
	},
};

static const struct regmap_config pg86x_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
};

static int pg86x_i2c_probe(struct i2c_client *i2c)
{
	int id, ret;
	struct regulator_config config = {.dev = &i2c->dev};
	struct regmap *regmap = devm_regmap_init_i2c(i2c, &pg86x_regmap);

	if (IS_ERR(regmap)) {
		ret = PTR_ERR(regmap);
		dev_err(&i2c->dev, "regmap init failed: %d\n", ret);
		return ret;
	}

	for (id = 0; id < ARRAY_SIZE(pg86x_regulators); id++) {
		struct regulator_dev *rdev;
		rdev = devm_regulator_register(&i2c->dev,
					       &pg86x_regulators[id],
					       &config);
		if (IS_ERR(rdev)) {
			ret = PTR_ERR(rdev);
			dev_err(&i2c->dev, "failed to register %s: %d\n",
				pg86x_regulators[id].name, ret);
			return ret;
		}
	}
	return 0;
}

static const struct of_device_id pg86x_dt_ids [] = {
	{ .compatible = "marvell,88pg867" },
	{ .compatible = "marvell,88pg868" },
	{ }
};
MODULE_DEVICE_TABLE(of, pg86x_dt_ids);

static const struct i2c_device_id pg86x_i2c_id[] = {
	{ "88pg867", },
	{ "88pg868", },
	{ }
};
MODULE_DEVICE_TABLE(i2c, pg86x_i2c_id);

static struct i2c_driver pg86x_regulator_driver = {
	.driver = {
		.name = "88pg86x",
		.of_match_table = of_match_ptr(pg86x_dt_ids),
	},
	.probe_new = pg86x_i2c_probe,
	.id_table = pg86x_i2c_id,
};

module_i2c_driver(pg86x_regulator_driver);

MODULE_DESCRIPTION("Marvell 88PG86X voltage regulator");
MODULE_AUTHOR("Alexander Monakov <amonakov@gmail.com>");
MODULE_LICENSE("GPL");
