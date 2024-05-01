/*
 * TI TPS65132 Regulator driver
 *
 * Copyright (C) 2017 NVIDIA CORPORATION. All rights reserved.
 *
 * Author: Venkat Reddy Talla <vreddytalla@nvidia.com>
 *		Laxman Dewangan <ldewangan@nvidia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any kind,
 * whether express or implied; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>

#define TPS65132_REG_VPOS		0x00
#define TPS65132_REG_VNEG		0x01
#define TPS65132_REG_APPS_DISP_DISN	0x03
#define TPS65132_REG_CONTROL		0x0FF

#define TPS65132_VOUT_MASK		0x1F
#define TPS65132_VOUT_N_VOLTAGE		0x15
#define TPS65132_VOUT_VMIN		4000000
#define TPS65132_VOUT_VMAX		6000000
#define TPS65132_VOUT_STEP		100000

#define TPS65132_REG_APPS_DIS_VPOS		BIT(0)
#define TPS65132_REG_APPS_DIS_VNEG		BIT(1)

#define TPS65132_REGULATOR_ID_VPOS	0
#define TPS65132_REGULATOR_ID_VNEG	1
#define TPS65132_MAX_REGULATORS		2

#define TPS65132_ACT_DIS_TIME_SLACK		1000

struct tps65132_reg_pdata {
	struct gpio_desc *en_gpiod;
	struct gpio_desc *act_dis_gpiod;
	unsigned int act_dis_time_us;
	int ena_gpio_state;
};

struct tps65132_regulator {
	struct device *dev;
	struct tps65132_reg_pdata reg_pdata[TPS65132_MAX_REGULATORS];
};

static int tps65132_regulator_enable(struct regulator_dev *rdev)
{
	struct tps65132_regulator *tps = rdev_get_drvdata(rdev);
	int id = rdev_get_id(rdev);
	struct tps65132_reg_pdata *rpdata = &tps->reg_pdata[id];
	int ret;

	if (!IS_ERR(rpdata->en_gpiod)) {
		gpiod_set_value_cansleep(rpdata->en_gpiod, 1);
		rpdata->ena_gpio_state = 1;
	}

	/* Hardware automatically enable discharge bit in enable */
	if (rdev->constraints->active_discharge ==
			REGULATOR_ACTIVE_DISCHARGE_DISABLE) {
		ret = regulator_set_active_discharge_regmap(rdev, false);
		if (ret < 0) {
			dev_err(tps->dev, "Failed to disable active discharge: %d\n",
				ret);
			return ret;
		}
	}

	return 0;
}

static int tps65132_regulator_disable(struct regulator_dev *rdev)
{
	struct tps65132_regulator *tps = rdev_get_drvdata(rdev);
	int id = rdev_get_id(rdev);
	struct tps65132_reg_pdata *rpdata = &tps->reg_pdata[id];

	if (!IS_ERR(rpdata->en_gpiod)) {
		gpiod_set_value_cansleep(rpdata->en_gpiod, 0);
		rpdata->ena_gpio_state = 0;
	}

	if (!IS_ERR(rpdata->act_dis_gpiod)) {
		gpiod_set_value_cansleep(rpdata->act_dis_gpiod, 1);
		usleep_range(rpdata->act_dis_time_us, rpdata->act_dis_time_us +
			     TPS65132_ACT_DIS_TIME_SLACK);
		gpiod_set_value_cansleep(rpdata->act_dis_gpiod, 0);
	}

	return 0;
}

static int tps65132_regulator_is_enabled(struct regulator_dev *rdev)
{
	struct tps65132_regulator *tps = rdev_get_drvdata(rdev);
	int id = rdev_get_id(rdev);
	struct tps65132_reg_pdata *rpdata = &tps->reg_pdata[id];

	if (!IS_ERR(rpdata->en_gpiod))
		return rpdata->ena_gpio_state;

	return 1;
}

static const struct regulator_ops tps65132_regulator_ops = {
	.enable = tps65132_regulator_enable,
	.disable = tps65132_regulator_disable,
	.is_enabled = tps65132_regulator_is_enabled,
	.list_voltage = regulator_list_voltage_linear,
	.map_voltage = regulator_map_voltage_linear,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.set_active_discharge = regulator_set_active_discharge_regmap,
};

static int tps65132_of_parse_cb(struct device_node *np,
				const struct regulator_desc *desc,
				struct regulator_config *config)
{
	struct tps65132_regulator *tps = config->driver_data;
	struct tps65132_reg_pdata *rpdata = &tps->reg_pdata[desc->id];
	int ret;

	rpdata->en_gpiod = devm_fwnode_gpiod_get(tps->dev, of_fwnode_handle(np),
						 "enable", GPIOD_ASIS,
						 "enable");
	if (IS_ERR(rpdata->en_gpiod)) {
		ret = PTR_ERR(rpdata->en_gpiod);

		/* Ignore the error other than probe defer */
		if (ret == -EPROBE_DEFER)
			return ret;
		return 0;
	}

	rpdata->act_dis_gpiod = devm_fwnode_gpiod_get(tps->dev,
						      of_fwnode_handle(np),
						      "active-discharge",
						      GPIOD_ASIS,
						      "active-discharge");
	if (IS_ERR(rpdata->act_dis_gpiod)) {
		ret = PTR_ERR(rpdata->act_dis_gpiod);

		/* Ignore the error other than probe defer */
		if (ret == -EPROBE_DEFER)
			return ret;

		return 0;
	}

	ret = of_property_read_u32(np, "ti,active-discharge-time-us",
				   &rpdata->act_dis_time_us);
	if (ret < 0) {
		dev_err(tps->dev, "Failed to read active discharge time:%d\n",
			ret);
		return ret;
	}

	return 0;
}

#define TPS65132_REGULATOR_DESC(_id, _name)		\
	[TPS65132_REGULATOR_ID_##_id] = {		\
		.name = "tps65132-"#_name,		\
		.supply_name = "vin",			\
		.id = TPS65132_REGULATOR_ID_##_id,	\
		.of_match = of_match_ptr(#_name),	\
		.of_parse_cb	= tps65132_of_parse_cb,	\
		.ops = &tps65132_regulator_ops,		\
		.n_voltages = TPS65132_VOUT_N_VOLTAGE,	\
		.min_uV = TPS65132_VOUT_VMIN,		\
		.uV_step = TPS65132_VOUT_STEP,		\
		.enable_time = 500,			\
		.vsel_mask = TPS65132_VOUT_MASK,	\
		.vsel_reg = TPS65132_REG_##_id,		\
		.active_discharge_off = 0,			\
		.active_discharge_on = TPS65132_REG_APPS_DIS_##_id, \
		.active_discharge_mask = TPS65132_REG_APPS_DIS_##_id, \
		.active_discharge_reg = TPS65132_REG_APPS_DISP_DISN, \
		.type = REGULATOR_VOLTAGE,		\
		.owner = THIS_MODULE,			\
	}

static const struct regulator_desc tps_regs_desc[TPS65132_MAX_REGULATORS] = {
	TPS65132_REGULATOR_DESC(VPOS, outp),
	TPS65132_REGULATOR_DESC(VNEG, outn),
};

static const struct regmap_range tps65132_no_reg_ranges[] = {
	regmap_reg_range(TPS65132_REG_APPS_DISP_DISN + 1,
			 TPS65132_REG_CONTROL - 1),
};

static const struct regmap_access_table tps65132_no_reg_table = {
	.no_ranges = tps65132_no_reg_ranges,
	.n_no_ranges = ARRAY_SIZE(tps65132_no_reg_ranges),
};

static const struct regmap_config tps65132_regmap_config = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.max_register	= TPS65132_REG_CONTROL,
	.cache_type	= REGCACHE_NONE,
	.rd_table	= &tps65132_no_reg_table,
	.wr_table	= &tps65132_no_reg_table,
};

static int tps65132_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct tps65132_regulator *tps;
	struct regulator_dev *rdev;
	struct regmap *rmap;
	struct regulator_config config = { };
	int id;
	int ret;

	tps = devm_kzalloc(dev, sizeof(*tps), GFP_KERNEL);
	if (!tps)
		return -ENOMEM;

	rmap = devm_regmap_init_i2c(client, &tps65132_regmap_config);
	if (IS_ERR(rmap)) {
		ret = PTR_ERR(rmap);
		dev_err(dev, "regmap init failed: %d\n", ret);
		return ret;
	}

	i2c_set_clientdata(client, tps);
	tps->dev = dev;

	for (id = 0; id < TPS65132_MAX_REGULATORS; ++id) {
		config.regmap = rmap;
		config.dev = dev;
		config.driver_data = tps;

		rdev = devm_regulator_register(dev, &tps_regs_desc[id],
					       &config);
		if (IS_ERR(rdev)) {
			ret = PTR_ERR(rdev);
			dev_err(dev, "regulator %s register failed: %d\n",
				tps_regs_desc[id].name, ret);
			return ret;
		}
	}
	return 0;
}

static const struct i2c_device_id tps65132_id[] = {
	{.name = "tps65132",},
	{},
};
MODULE_DEVICE_TABLE(i2c, tps65132_id);

static const struct of_device_id __maybe_unused tps65132_of_match[] = {
	{ .compatible = "ti,tps65132" },
	{},
};
MODULE_DEVICE_TABLE(of, tps65132_of_match);

static struct i2c_driver tps65132_i2c_driver = {
	.driver = {
		.name = "tps65132",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table = of_match_ptr(tps65132_of_match),
	},
	.probe = tps65132_probe,
	.id_table = tps65132_id,
};

module_i2c_driver(tps65132_i2c_driver);

MODULE_DESCRIPTION("tps65132 regulator driver");
MODULE_AUTHOR("Venkat Reddy Talla <vreddytalla@nvidia.com>");
MODULE_AUTHOR("Laxman Dewangan <ldewangan@nvidia.com>");
MODULE_LICENSE("GPL v2");
