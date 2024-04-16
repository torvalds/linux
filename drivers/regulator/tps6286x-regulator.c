// SPDX-License-Identifier: GPL-2.0-only
// Copyright Axis Communications AB

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/driver.h>

#include <dt-bindings/regulator/ti,tps62864.h>

#define TPS6286X_VOUT1		0x01
#define TPS6286X_VOUT1_VO1_SET	GENMASK(7, 0)

#define TPS6286X_CONTROL	0x03
#define TPS6286X_CONTROL_FPWM	BIT(4)
#define TPS6286X_CONTROL_SWEN	BIT(5)

#define TPS6286X_MIN_MV		400
#define TPS6286X_MAX_MV		1675
#define TPS6286X_STEP_MV	5

static const struct regmap_config tps6286x_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static int tps6286x_set_mode(struct regulator_dev *rdev, unsigned int mode)
{
	unsigned int val;

	switch (mode) {
	case REGULATOR_MODE_NORMAL:
		val = 0;
		break;
	case REGULATOR_MODE_FAST:
		val = TPS6286X_CONTROL_FPWM;
		break;
	default:
		return -EINVAL;
	}

	return regmap_update_bits(rdev->regmap, TPS6286X_CONTROL,
				  TPS6286X_CONTROL_FPWM, val);
}

static unsigned int tps6286x_get_mode(struct regulator_dev *rdev)
{
	unsigned int val;
	int ret;

	ret = regmap_read(rdev->regmap, TPS6286X_CONTROL, &val);
	if (ret < 0)
		return 0;

	return (val & TPS6286X_CONTROL_FPWM) ? REGULATOR_MODE_FAST : REGULATOR_MODE_NORMAL;
}

static const struct regulator_ops tps6286x_regulator_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.set_mode = tps6286x_set_mode,
	.get_mode = tps6286x_get_mode,
	.is_enabled = regulator_is_enabled_regmap,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.list_voltage = regulator_list_voltage_linear,
};

static unsigned int tps6286x_of_map_mode(unsigned int mode)
{
	switch (mode) {
	case TPS62864_MODE_NORMAL:
		return REGULATOR_MODE_NORMAL;
	case TPS62864_MODE_FPWM:
		return REGULATOR_MODE_FAST;
	default:
		return REGULATOR_MODE_INVALID;
	}
}

static const struct regulator_desc tps6286x_reg = {
	.name = "tps6286x",
	.of_match = of_match_ptr("SW"),
	.owner = THIS_MODULE,
	.ops = &tps6286x_regulator_ops,
	.of_map_mode = tps6286x_of_map_mode,
	.regulators_node = of_match_ptr("regulators"),
	.type = REGULATOR_VOLTAGE,
	.n_voltages = ((TPS6286X_MAX_MV - TPS6286X_MIN_MV) / TPS6286X_STEP_MV) + 1,
	.min_uV = TPS6286X_MIN_MV * 1000,
	.uV_step = TPS6286X_STEP_MV * 1000,
	.vsel_reg = TPS6286X_VOUT1,
	.vsel_mask = TPS6286X_VOUT1_VO1_SET,
	.enable_reg = TPS6286X_CONTROL,
	.enable_mask = TPS6286X_CONTROL_SWEN,
	.ramp_delay = 1000,
	/* tDelay + tRamp, rounded up */
	.enable_time = 3000,
};

static const struct of_device_id tps6286x_dt_ids[] = {
	{ .compatible = "ti,tps62864", },
	{ .compatible = "ti,tps62866", },
	{ .compatible = "ti,tps62868", },
	{ .compatible = "ti,tps62869", },
	{ }
};
MODULE_DEVICE_TABLE(of, tps6286x_dt_ids);

static int tps6286x_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct device *dev = &i2c->dev;
	struct regulator_config config = {};
	struct regulator_dev *rdev;
	struct regmap *regmap;

	regmap = devm_regmap_init_i2c(i2c, &tps6286x_regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	config.dev = &i2c->dev;
	config.of_node = dev->of_node;
	config.regmap = regmap;

	rdev = devm_regulator_register(&i2c->dev, &tps6286x_reg, &config);
	if (IS_ERR(rdev)) {
		dev_err(&i2c->dev, "Failed to register tps6286x regulator\n");
		return PTR_ERR(rdev);
	}

	return 0;
}

static const struct i2c_device_id tps6286x_i2c_id[] = {
	{ "tps62864", 0 },
	{ "tps62866", 0 },
	{ "tps62868", 0 },
	{ "tps62869", 0 },
	{},
};
MODULE_DEVICE_TABLE(i2c, tps6286x_i2c_id);

static struct i2c_driver tps6286x_regulator_driver = {
	.driver = {
		.name = "tps6286x",
		.of_match_table = of_match_ptr(tps6286x_dt_ids),
	},
	.probe = tps6286x_i2c_probe,
	.id_table = tps6286x_i2c_id,
};

module_i2c_driver(tps6286x_regulator_driver);

MODULE_LICENSE("GPL v2");
