// SPDX-License-Identifier: GPL-2.0+
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>

enum fan53880_regulator_ids {
	FAN53880_LDO1,
	FAN53880_LDO2,
	FAN53880_LDO3,
	FAN53880_LDO4,
	FAN53880_BUCK,
	FAN53880_BOOST,
};

enum fan53880_registers {
	FAN53880_PRODUCT_ID = 0x00,
	FAN53880_SILICON_REV,
	FAN53880_BUCKVOUT,
	FAN53880_BOOSTVOUT,
	FAN53880_LDO1VOUT,
	FAN53880_LDO2VOUT,
	FAN53880_LDO3VOUT,
	FAN53880_LDO4VOUT,
	FAN53880_IOUT,
	FAN53880_ENABLE,
	FAN53880_ENABLE_BOOST,
};

#define FAN53880_ID	0x01

static const struct regulator_ops fan53880_ops = {
	.list_voltage = regulator_list_voltage_linear_range,
	.map_voltage = regulator_map_voltage_linear_range,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
};

#define FAN53880_LDO(_num, _supply, _default)				\
	[FAN53880_LDO ## _num] = {					\
		.name =		   "LDO"#_num,				\
		.of_match =	   of_match_ptr("LDO"#_num),		\
		.regulators_node = of_match_ptr("regulators"),		\
		.type =		   REGULATOR_VOLTAGE,			\
		.owner =	   THIS_MODULE,				\
		.linear_ranges =   (struct linear_range[]) {		\
		      REGULATOR_LINEAR_RANGE(_default, 0x0, 0x0, 0),	\
		      REGULATOR_LINEAR_RANGE(800000, 0xf, 0x73, 25000),	\
		},							\
		.n_linear_ranges = 2,					\
		.n_voltages =	   0x74,				\
		.vsel_reg =	   FAN53880_LDO ## _num ## VOUT,	\
		.vsel_mask =	   0x7f,				\
		.enable_reg =	   FAN53880_ENABLE,			\
		.enable_mask =	   BIT(_num - 1),			\
		.enable_time =	   150,					\
		.supply_name =	   _supply,				\
		.ops =		   &fan53880_ops,			\
	}

static const struct regulator_desc fan53880_regulators[] = {
	FAN53880_LDO(1, "VIN12", 2800000),
	FAN53880_LDO(2, "VIN12", 2800000),
	FAN53880_LDO(3, "VIN3", 1800000),
	FAN53880_LDO(4, "VIN4", 1800000),
	[FAN53880_BUCK] = {
		.name =		   "BUCK",
		.of_match =	   of_match_ptr("BUCK"),
		.regulators_node = of_match_ptr("regulators"),
		.type =		   REGULATOR_VOLTAGE,
		.owner =	   THIS_MODULE,
		.linear_ranges =   (struct linear_range[]) {
		      REGULATOR_LINEAR_RANGE(1100000, 0x0, 0x0, 0),
		      REGULATOR_LINEAR_RANGE(600000, 0x1f, 0xf7, 12500),
		},
		.n_linear_ranges = 2,
		.n_voltages =	   0xf8,
		.vsel_reg =	   FAN53880_BUCKVOUT,
		.vsel_mask =	   0x7f,
		.enable_reg =	   FAN53880_ENABLE,
		.enable_mask =	   0x10,
		.enable_time =	   480,
		.supply_name =	   "PVIN",
		.ops =		   &fan53880_ops,
	},
	[FAN53880_BOOST] = {
		.name =		   "BOOST",
		.of_match =	   of_match_ptr("BOOST"),
		.regulators_node = of_match_ptr("regulators"),
		.type =		   REGULATOR_VOLTAGE,
		.owner =	   THIS_MODULE,
		.linear_ranges =   (struct linear_range[]) {
		      REGULATOR_LINEAR_RANGE(5000000, 0x0, 0x0, 0),
		      REGULATOR_LINEAR_RANGE(3000000, 0x4, 0x70, 25000),
		},
		.n_linear_ranges = 2,
		.n_voltages =	   0x71,
		.vsel_reg =	   FAN53880_BOOSTVOUT,
		.vsel_mask =	   0x7f,
		.enable_reg =	   FAN53880_ENABLE_BOOST,
		.enable_mask =	   0xff,
		.enable_time =	   580,
		.supply_name =	   "PVIN",
		.ops =		   &fan53880_ops,
	},
};

static const struct regmap_config fan53880_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = FAN53880_ENABLE_BOOST,
};

static int fan53880_i2c_probe(struct i2c_client *i2c,
			     const struct i2c_device_id *id)
{
	struct regulator_config config = { };
	struct regulator_dev *rdev;
	struct regmap *regmap;
	int i, ret;
	unsigned int data;

	regmap = devm_regmap_init_i2c(i2c, &fan53880_regmap);
	if (IS_ERR(regmap)) {
		ret = PTR_ERR(regmap);
		dev_err(&i2c->dev, "Failed to create regmap: %d\n", ret);
		return ret;
	}

	ret = regmap_read(regmap, FAN53880_PRODUCT_ID, &data);
	if (ret < 0) {
		dev_err(&i2c->dev, "Failed to read PRODUCT_ID: %d\n", ret);
		return ret;
	}
	if (data != FAN53880_ID) {
		dev_err(&i2c->dev, "Unsupported device id: 0x%x.\n", data);
		return -ENODEV;
	}

	config.dev = &i2c->dev;
	config.init_data = NULL;

	for (i = 0; i < ARRAY_SIZE(fan53880_regulators); i++) {
		rdev = devm_regulator_register(&i2c->dev,
					       &fan53880_regulators[i],
					       &config);
		if (IS_ERR(rdev)) {
			ret = PTR_ERR(rdev);
			dev_err(&i2c->dev, "Failed to register %s: %d\n",
				fan53880_regulators[i].name, ret);
			return ret;
		}
	}

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id fan53880_dt_ids[] = {
	{ .compatible = "onnn,fan53880", },
	{}
};
MODULE_DEVICE_TABLE(of, fan53880_dt_ids);
#endif

static const struct i2c_device_id fan53880_i2c_id[] = {
	{ "fan53880", },
	{}
};
MODULE_DEVICE_TABLE(i2c, fan53880_i2c_id);

static struct i2c_driver fan53880_regulator_driver = {
	.driver = {
		.name = "fan53880",
		.of_match_table	= of_match_ptr(fan53880_dt_ids),
	},
	.probe = fan53880_i2c_probe,
	.id_table = fan53880_i2c_id,
};
module_i2c_driver(fan53880_regulator_driver);

MODULE_DESCRIPTION("FAN53880 PMIC voltage regulator driver");
MODULE_AUTHOR("Christoph Fritz <chf.fritz@googlemail.com>");
MODULE_LICENSE("GPL");
