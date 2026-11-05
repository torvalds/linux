// SPDX-License-Identifier: GPL-2.0-only
/*
 * Nexperia NEX10000UB Regulator driver
 *
 * Copyright (C) 2026 Linaro Limited.
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>

#define NEX10000UB_REG_VPOS		0x00
#define NEX10000UB_REG_VNEG		0x01
#define NEX10000UB_REG_ID		0x02

#define NEX10000UB_VOUT_MASK		0x1F
#define NEX10000UB_VOUT_N_VOLTAGE	0x15
#define NEX10000UB_VOUT_VMIN		4000000
#define NEX10000UB_VOUT_VMAX		6000000
#define NEX10000UB_VOUT_STEP		100000

#define NEX10000UB_REGULATOR_ID_VPOS	0
#define NEX10000UB_REGULATOR_ID_VNEG	1
#define NEX10000UB_MAX_REGULATORS	2

static const struct regulator_ops nex10000ub_regulator_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = regulator_list_voltage_linear,
	.map_voltage = regulator_map_voltage_linear,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
};

static int nex10000ub_of_parse_cb(struct device_node *np,
				  const struct regulator_desc *desc,
				  struct regulator_config *config)
{
	struct gpio_desc *ena_gpiod;

	ena_gpiod = fwnode_gpiod_get_index(of_fwnode_handle(np), "enable", 0,
					   GPIOD_OUT_LOW, desc->name);
	if (IS_ERR(ena_gpiod))
		return PTR_ERR(ena_gpiod);

	config->ena_gpiod = ena_gpiod;

	return 0;
}

#define NEX10000UB_REGULATOR_DESC(_id, _name)			\
	[NEX10000UB_REGULATOR_ID_##_id] = {			\
		.name = "nex10000ub-"#_name,			\
		.supply_name = "vin",				\
		.id = NEX10000UB_REGULATOR_ID_##_id,		\
		.of_match = of_match_ptr(#_name),		\
		.of_parse_cb = nex10000ub_of_parse_cb,		\
		.ops = &nex10000ub_regulator_ops,		\
		.n_voltages = NEX10000UB_VOUT_N_VOLTAGE,	\
		.min_uV = NEX10000UB_VOUT_VMIN,			\
		.uV_step = NEX10000UB_VOUT_STEP,		\
		.enable_time = 2000,				\
		.vsel_mask = NEX10000UB_VOUT_MASK,		\
		.vsel_reg = NEX10000UB_REG_##_id,		\
		.type = REGULATOR_VOLTAGE,			\
		.owner = THIS_MODULE,				\
	}

static const struct regulator_desc nex10000_regs_desc[NEX10000UB_MAX_REGULATORS] = {
	NEX10000UB_REGULATOR_DESC(VPOS, vpos),
	NEX10000UB_REGULATOR_DESC(VNEG, vneg),
};

static const struct regmap_config nex10000ub_regmap_config = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.max_register	= NEX10000UB_REG_ID,
	.cache_type	= REGCACHE_NONE,
};

static int nex10000ub_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct regmap *map;
	int id;
	int ret;

	map = devm_regmap_init_i2c(client, &nex10000ub_regmap_config);
	if (IS_ERR(map)) {
		ret = PTR_ERR(map);
		dev_err(dev, "regmap init failed: %d\n", ret);
		return ret;
	}

	for (id = 0; id < NEX10000UB_MAX_REGULATORS; ++id) {
		struct regulator_config config = { };
		struct regulator_dev *rdev;

		config.regmap = map;
		config.dev = dev;

		rdev = devm_regulator_register(dev, &nex10000_regs_desc[id],
					       &config);
		if (IS_ERR(rdev)) {
			ret = PTR_ERR(rdev);
			dev_err(dev, "regulator %s register failed: %d\n",
				nex10000_regs_desc[id].name, ret);
			return ret;
		}
	}

	return 0;
}

static const struct i2c_device_id nex10000ub_id[] = {
	{ .name = "nex10000ub" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, nex10000ub_id);

static const struct of_device_id __maybe_unused nex10000ub_of_match[] = {
	{ .compatible = "nexperia,nex10000ub" },
	{},
};
MODULE_DEVICE_TABLE(of, nex10000ub_of_match);

static struct i2c_driver nex10000ub_i2c_driver = {
	.driver = {
		.name = "nex10000ub",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table = of_match_ptr(nex10000ub_of_match),
	},
	.probe = nex10000ub_probe,
	.id_table = nex10000ub_id,
};

module_i2c_driver(nex10000ub_i2c_driver);

MODULE_DESCRIPTION("NEX10000UB regulator driver");
MODULE_AUTHOR("Neil Armstrong <neil.armstrong@linaro.org>");
MODULE_LICENSE("GPL");
