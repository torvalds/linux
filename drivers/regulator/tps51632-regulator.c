// SPDX-License-Identifier: GPL-2.0-only
/*
 * tps51632-regulator.c -- TI TPS51632
 *
 * Regulator driver for TPS51632 3-2-1 Phase D-Cap Step Down Driverless
 * Controller with serial VID control and DVFS.
 *
 * Copyright (c) 2012, NVIDIA Corporation.
 *
 * Author: Laxman Dewangan <ldewangan@nvidia.com>
 */

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/tps51632-regulator.h>
#include <linux/slab.h>

/* Register definitions */
#define TPS51632_VOLTAGE_SELECT_REG		0x0
#define TPS51632_VOLTAGE_BASE_REG		0x1
#define TPS51632_OFFSET_REG			0x2
#define TPS51632_IMON_REG			0x3
#define TPS51632_VMAX_REG			0x4
#define TPS51632_DVFS_CONTROL_REG		0x5
#define TPS51632_POWER_STATE_REG		0x6
#define TPS51632_SLEW_REGS			0x7
#define TPS51632_FAULT_REG			0x14

#define TPS51632_MAX_REG			0x15

#define TPS51632_VOUT_MASK			0x7F
#define TPS51632_VOUT_OFFSET_MASK		0x1F
#define TPS51632_VMAX_MASK			0x7F
#define TPS51632_VMAX_LOCK			0x80

/* TPS51632_DVFS_CONTROL_REG */
#define TPS51632_DVFS_PWMEN			0x1
#define TPS51632_DVFS_STEP_20			0x2
#define TPS51632_DVFS_VMAX_PG			0x4
#define TPS51632_DVFS_PWMRST			0x8
#define TPS51632_DVFS_OCA_EN			0x10
#define TPS51632_DVFS_FCCM			0x20

/* TPS51632_POWER_STATE_REG */
#define TPS51632_POWER_STATE_MASK		0x03
#define TPS51632_POWER_STATE_MULTI_PHASE_CCM	0x0
#define TPS51632_POWER_STATE_SINGLE_PHASE_CCM	0x1
#define TPS51632_POWER_STATE_SINGLE_PHASE_DCM	0x2

#define TPS51632_MIN_VOLTAGE			500000
#define TPS51632_MAX_VOLTAGE			1520000
#define TPS51632_VOLTAGE_STEP_10mV		10000
#define TPS51632_VOLTAGE_STEP_20mV		20000
#define TPS51632_MAX_VSEL			0x7F
#define TPS51632_MIN_VSEL			0x19
#define TPS51632_DEFAULT_RAMP_DELAY		6000
#define TPS51632_VOLT_VSEL(uV)					\
		(DIV_ROUND_UP(uV - TPS51632_MIN_VOLTAGE,	\
			TPS51632_VOLTAGE_STEP_10mV) +		\
			TPS51632_MIN_VSEL)

/* TPS51632 chip information */
struct tps51632_chip {
	struct device *dev;
	struct regulator_desc desc;
	struct regulator_dev *rdev;
	struct regmap *regmap;
};

static int tps51632_dcdc_set_ramp_delay(struct regulator_dev *rdev,
		int ramp_delay)
{
	struct tps51632_chip *tps = rdev_get_drvdata(rdev);
	int bit;
	int ret;

	if (ramp_delay == 0)
		bit = 0;
	else
		bit = DIV_ROUND_UP(ramp_delay, 6000) - 1;

	ret = regmap_write(tps->regmap, TPS51632_SLEW_REGS, BIT(bit));
	if (ret < 0)
		dev_err(tps->dev, "SLEW reg write failed, err %d\n", ret);
	return ret;
}

static const struct regulator_ops tps51632_dcdc_ops = {
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.list_voltage		= regulator_list_voltage_linear,
	.set_voltage_time_sel	= regulator_set_voltage_time_sel,
	.set_ramp_delay		= tps51632_dcdc_set_ramp_delay,
};

static int tps51632_init_dcdc(struct tps51632_chip *tps,
		struct tps51632_regulator_platform_data *pdata)
{
	int ret;
	uint8_t	control = 0;
	int vsel;

	if (!pdata->enable_pwm_dvfs)
		goto skip_pwm_config;

	control |= TPS51632_DVFS_PWMEN;
	vsel = TPS51632_VOLT_VSEL(pdata->base_voltage_uV);
	ret = regmap_write(tps->regmap, TPS51632_VOLTAGE_BASE_REG, vsel);
	if (ret < 0) {
		dev_err(tps->dev, "BASE reg write failed, err %d\n", ret);
		return ret;
	}

	if (pdata->dvfs_step_20mV)
		control |= TPS51632_DVFS_STEP_20;

	if (pdata->max_voltage_uV) {
		unsigned int vmax;
		/**
		 * TPS51632 hw behavior: VMAX register can be write only
		 * once as it get locked after first write. The lock get
		 * reset only when device is power-reset.
		 * Write register only when lock bit is not enabled.
		 */
		ret = regmap_read(tps->regmap, TPS51632_VMAX_REG, &vmax);
		if (ret < 0) {
			dev_err(tps->dev, "VMAX read failed, err %d\n", ret);
			return ret;
		}
		if (!(vmax & TPS51632_VMAX_LOCK)) {
			vsel = TPS51632_VOLT_VSEL(pdata->max_voltage_uV);
			ret = regmap_write(tps->regmap, TPS51632_VMAX_REG,
					vsel);
			if (ret < 0) {
				dev_err(tps->dev,
					"VMAX write failed, err %d\n", ret);
				return ret;
			}
		}
	}

skip_pwm_config:
	ret = regmap_write(tps->regmap, TPS51632_DVFS_CONTROL_REG, control);
	if (ret < 0)
		dev_err(tps->dev, "DVFS reg write failed, err %d\n", ret);
	return ret;
}

static bool is_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TPS51632_OFFSET_REG:
	case TPS51632_FAULT_REG:
	case TPS51632_IMON_REG:
		return true;
	default:
		return false;
	}
}

static bool is_read_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case 0x08 ... 0x0F:
		return false;
	default:
		return true;
	}
}

static bool is_write_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TPS51632_VOLTAGE_SELECT_REG:
	case TPS51632_VOLTAGE_BASE_REG:
	case TPS51632_VMAX_REG:
	case TPS51632_DVFS_CONTROL_REG:
	case TPS51632_POWER_STATE_REG:
	case TPS51632_SLEW_REGS:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config tps51632_regmap_config = {
	.reg_bits		= 8,
	.val_bits		= 8,
	.writeable_reg		= is_write_reg,
	.readable_reg		= is_read_reg,
	.volatile_reg		= is_volatile_reg,
	.max_register		= TPS51632_MAX_REG - 1,
	.cache_type		= REGCACHE_RBTREE,
};

#if defined(CONFIG_OF)
static const struct of_device_id tps51632_of_match[] = {
	{ .compatible = "ti,tps51632",},
	{},
};
MODULE_DEVICE_TABLE(of, tps51632_of_match);

static struct tps51632_regulator_platform_data *
	of_get_tps51632_platform_data(struct device *dev,
				      const struct regulator_desc *desc)
{
	struct tps51632_regulator_platform_data *pdata;
	struct device_node *np = dev->of_node;

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return NULL;

	pdata->reg_init_data = of_get_regulator_init_data(dev, dev->of_node,
							  desc);
	if (!pdata->reg_init_data) {
		dev_err(dev, "Not able to get OF regulator init data\n");
		return NULL;
	}

	pdata->enable_pwm_dvfs =
			of_property_read_bool(np, "ti,enable-pwm-dvfs");
	pdata->dvfs_step_20mV = of_property_read_bool(np, "ti,dvfs-step-20mV");

	pdata->base_voltage_uV = pdata->reg_init_data->constraints.min_uV ? :
					TPS51632_MIN_VOLTAGE;
	pdata->max_voltage_uV = pdata->reg_init_data->constraints.max_uV ? :
					TPS51632_MAX_VOLTAGE;
	return pdata;
}
#else
static struct tps51632_regulator_platform_data *
	of_get_tps51632_platform_data(struct device *dev,
				      const struct regulator_desc *desc)
{
	return NULL;
}
#endif

static int tps51632_probe(struct i2c_client *client)
{
	struct tps51632_regulator_platform_data *pdata;
	struct regulator_dev *rdev;
	struct tps51632_chip *tps;
	int ret;
	struct regulator_config config = { };

	if (client->dev.of_node) {
		const struct of_device_id *match;
		match = of_match_device(of_match_ptr(tps51632_of_match),
				&client->dev);
		if (!match) {
			dev_err(&client->dev, "Error: No device match found\n");
			return -ENODEV;
		}
	}

	tps = devm_kzalloc(&client->dev, sizeof(*tps), GFP_KERNEL);
	if (!tps)
		return -ENOMEM;

	tps->dev = &client->dev;
	tps->desc.name = client->name;
	tps->desc.id = 0;
	tps->desc.ramp_delay = TPS51632_DEFAULT_RAMP_DELAY;
	tps->desc.min_uV = TPS51632_MIN_VOLTAGE;
	tps->desc.uV_step = TPS51632_VOLTAGE_STEP_10mV;
	tps->desc.linear_min_sel = TPS51632_MIN_VSEL;
	tps->desc.n_voltages = TPS51632_MAX_VSEL + 1;
	tps->desc.ops = &tps51632_dcdc_ops;
	tps->desc.type = REGULATOR_VOLTAGE;
	tps->desc.owner = THIS_MODULE;

	pdata = dev_get_platdata(&client->dev);
	if (!pdata && client->dev.of_node)
		pdata = of_get_tps51632_platform_data(&client->dev, &tps->desc);
	if (!pdata) {
		dev_err(&client->dev, "No Platform data\n");
		return -EINVAL;
	}

	if (pdata->enable_pwm_dvfs) {
		if ((pdata->base_voltage_uV < TPS51632_MIN_VOLTAGE) ||
		    (pdata->base_voltage_uV > TPS51632_MAX_VOLTAGE)) {
			dev_err(&client->dev, "Invalid base_voltage_uV setting\n");
			return -EINVAL;
		}

		if ((pdata->max_voltage_uV) &&
		    ((pdata->max_voltage_uV < TPS51632_MIN_VOLTAGE) ||
		     (pdata->max_voltage_uV > TPS51632_MAX_VOLTAGE))) {
			dev_err(&client->dev, "Invalid max_voltage_uV setting\n");
			return -EINVAL;
		}
	}

	if (pdata->enable_pwm_dvfs)
		tps->desc.vsel_reg = TPS51632_VOLTAGE_BASE_REG;
	else
		tps->desc.vsel_reg = TPS51632_VOLTAGE_SELECT_REG;
	tps->desc.vsel_mask = TPS51632_VOUT_MASK;

	tps->regmap = devm_regmap_init_i2c(client, &tps51632_regmap_config);
	if (IS_ERR(tps->regmap)) {
		ret = PTR_ERR(tps->regmap);
		dev_err(&client->dev, "regmap init failed, err %d\n", ret);
		return ret;
	}
	i2c_set_clientdata(client, tps);

	ret = tps51632_init_dcdc(tps, pdata);
	if (ret < 0) {
		dev_err(tps->dev, "Init failed, err = %d\n", ret);
		return ret;
	}

	/* Register the regulators */
	config.dev = &client->dev;
	config.init_data = pdata->reg_init_data;
	config.driver_data = tps;
	config.regmap = tps->regmap;
	config.of_node = client->dev.of_node;

	rdev = devm_regulator_register(&client->dev, &tps->desc, &config);
	if (IS_ERR(rdev)) {
		dev_err(tps->dev, "regulator register failed\n");
		return PTR_ERR(rdev);
	}

	tps->rdev = rdev;
	return 0;
}

static const struct i2c_device_id tps51632_id[] = {
	{.name = "tps51632",},
	{},
};

MODULE_DEVICE_TABLE(i2c, tps51632_id);

static struct i2c_driver tps51632_i2c_driver = {
	.driver = {
		.name = "tps51632",
		.of_match_table = of_match_ptr(tps51632_of_match),
	},
	.probe_new = tps51632_probe,
	.id_table = tps51632_id,
};

static int __init tps51632_init(void)
{
	return i2c_add_driver(&tps51632_i2c_driver);
}
subsys_initcall(tps51632_init);

static void __exit tps51632_cleanup(void)
{
	i2c_del_driver(&tps51632_i2c_driver);
}
module_exit(tps51632_cleanup);

MODULE_AUTHOR("Laxman Dewangan <ldewangan@nvidia.com>");
MODULE_DESCRIPTION("TPS51632 voltage regulator driver");
MODULE_LICENSE("GPL v2");
