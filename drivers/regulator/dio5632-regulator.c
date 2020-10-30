// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Rockchip Electronics Co. Ltd.
 *
 * Author: Guochun Huang <hero.huang@rock-chips.com>
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>

#define DIO5632_REG_VPOS		0x00
#define DIO5632_REG_VNEG		0x01
#define DIO5632_REG_APPS_DISP_DISN	0x03
#define DIO5632_REG_CONTROL		0x0FF

#define DIO5632_VOUT_MASK		0x1F
#define DIO5632_VOUT_N_VOLTAGE		0x15
#define DIO5632_VOUT_VMIN		4000000
#define DIO5632_VOUT_VMAX		6000000
#define DIO5632_VOUT_STEP		100000

#define DIO5632_REG_DIS_VPOS		BIT(1)
#define DIO5632_REG_DIS_VNEG		BIT(0)

#define DIO5632_REGULATOR_ID_VPOS	0
#define DIO5632_REGULATOR_ID_VNEG	1
#define DIO5632_MAX_REGULATORS		2

#define DIO5632_ACT_DIS_TIME_SLACK		1000

struct DIO5632_reg_pdata {
	struct gpio_desc *en_gpiod;
	int ena_gpio_state;
};

struct DIO5632_regulator {
	struct device *dev;
	struct DIO5632_reg_pdata reg_pdata[DIO5632_MAX_REGULATORS];
};

static int DIO5632_regulator_enable(struct regulator_dev *rdev)
{
	struct DIO5632_regulator *dio = rdev_get_drvdata(rdev);
	int id = rdev_get_id(rdev);
	struct DIO5632_reg_pdata *rpdata = &dio->reg_pdata[id];
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
			dev_err(dio->dev, "Failed to disable active discharge: %d\n",
				ret);
			return ret;
		}
	}

	return 0;
}

static int DIO5632_regulator_disable(struct regulator_dev *rdev)
{
	struct DIO5632_regulator *dio = rdev_get_drvdata(rdev);
	int id = rdev_get_id(rdev);
	struct DIO5632_reg_pdata *rpdata = &dio->reg_pdata[id];

	if (!IS_ERR(rpdata->en_gpiod)) {
		gpiod_set_value_cansleep(rpdata->en_gpiod, 0);
		rpdata->ena_gpio_state = 0;
	}

	return 0;
}

static int DIO5632_regulator_is_enabled(struct regulator_dev *rdev)
{
	struct DIO5632_regulator *dio = rdev_get_drvdata(rdev);
	int id = rdev_get_id(rdev);
	struct DIO5632_reg_pdata *rpdata = &dio->reg_pdata[id];

	if (!IS_ERR(rpdata->en_gpiod))
		return rpdata->ena_gpio_state;

	return 1;
}

static const struct regulator_ops DIO5632_regulator_ops = {
	.enable = DIO5632_regulator_enable,
	.disable = DIO5632_regulator_disable,
	.is_enabled = DIO5632_regulator_is_enabled,
	.list_voltage = regulator_list_voltage_linear,
	.map_voltage = regulator_map_voltage_linear,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.set_active_discharge = regulator_set_active_discharge_regmap,
};

static int DIO5632_of_parse_cb(struct device_node *np,
				const struct regulator_desc *desc,
				struct regulator_config *config)
{
	struct DIO5632_regulator *dio = config->driver_data;
	struct DIO5632_reg_pdata *rpdata = &dio->reg_pdata[desc->id];
	int ret;

	rpdata->en_gpiod = devm_fwnode_get_index_gpiod_from_child(dio->dev,
					"enable", 0, &np->fwnode,
					GPIOD_OUT_HIGH, "enable");
	if (IS_ERR_OR_NULL(rpdata->en_gpiod)) {
		ret = PTR_ERR(rpdata->en_gpiod);

		/* Ignore the error other than probe defer */
		if (ret == -EPROBE_DEFER)
			return ret;
		return 0;
	}

	return 0;
}

#define DIO5632_REGULATOR_DESC(_id, _name)		\
	[DIO5632_REGULATOR_ID_##_id] = {		\
		.name = "DIO5632-"#_name,		\
		.supply_name = "vin",			\
		.id = DIO5632_REGULATOR_ID_##_id,	\
		.of_match = of_match_ptr(#_name),	\
		.of_parse_cb	= DIO5632_of_parse_cb,	\
		.ops = &DIO5632_regulator_ops,		\
		.n_voltages = DIO5632_VOUT_N_VOLTAGE,	\
		.min_uV = DIO5632_VOUT_VMIN,		\
		.uV_step = DIO5632_VOUT_STEP,		\
		.enable_time = 500,			\
		.vsel_mask = DIO5632_VOUT_MASK,	\
		.vsel_reg = DIO5632_REG_##_id,		\
		.active_discharge_off = 0,			\
		.active_discharge_on = DIO5632_REG_DIS_##_id, \
		.active_discharge_mask = DIO5632_REG_DIS_##_id, \
		.active_discharge_reg = DIO5632_REG_APPS_DISP_DISN, \
		.type = REGULATOR_VOLTAGE,		\
		.owner = THIS_MODULE,			\
	}

static const struct regulator_desc dio_regs_desc[DIO5632_MAX_REGULATORS] = {
	DIO5632_REGULATOR_DESC(VPOS, outp),
	DIO5632_REGULATOR_DESC(VNEG, outn),
};

static const struct regmap_range DIO5632_no_reg_ranges[] = {
	regmap_reg_range(DIO5632_REG_APPS_DISP_DISN + 1,
			 DIO5632_REG_CONTROL - 1),
};

static const struct regmap_access_table DIO5632_no_reg_table = {
	.no_ranges = DIO5632_no_reg_ranges,
	.n_no_ranges = ARRAY_SIZE(DIO5632_no_reg_ranges),
};

static const struct regmap_config DIO5632_regmap_config = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.max_register	= DIO5632_REG_CONTROL,
	.cache_type	= REGCACHE_NONE,
	.rd_table	= &DIO5632_no_reg_table,
	.wr_table	= &DIO5632_no_reg_table,
};

static int DIO5632_probe(struct i2c_client *client,
			  const struct i2c_device_id *client_id)
{
	struct device *dev = &client->dev;
	struct DIO5632_regulator *dio;
	struct regulator_dev *rdev;
	struct regmap *rmap;
	struct regulator_config config = { };
	int id;
	int ret;

	dio = devm_kzalloc(dev, sizeof(*dio), GFP_KERNEL);
	if (!dio)
		return -ENOMEM;

	rmap = devm_regmap_init_i2c(client, &DIO5632_regmap_config);
	if (IS_ERR(rmap)) {
		ret = PTR_ERR(rmap);
		dev_err(dev, "regmap init failed: %d\n", ret);
		return ret;
	}

	i2c_set_clientdata(client, dio);
	dio->dev = dev;

	for (id = 0; id < DIO5632_MAX_REGULATORS; ++id) {
		config.regmap = rmap;
		config.dev = dev;
		config.driver_data = dio;

		rdev = devm_regulator_register(dev, &dio_regs_desc[id],
					       &config);
		if (IS_ERR(rdev)) {
			ret = PTR_ERR(rdev);
			dev_err(dev, "regulator %s register failed: %d\n",
				dio_regs_desc[id].name, ret);
			return ret;
		}
	}
	return 0;
}

static const struct i2c_device_id DIO5632_id[] = {
	{.name = "DIO5632",},
	{},
};
MODULE_DEVICE_TABLE(i2c, DIO5632_id);

static struct i2c_driver DIO5632_i2c_driver = {
	.driver = {
		.name = "DIO5632",
	},
	.probe = DIO5632_probe,
	.id_table = DIO5632_id,
};

module_i2c_driver(DIO5632_i2c_driver);

MODULE_DESCRIPTION("DIO5632 regulator driver");
MODULE_AUTHOR("Guochun Huang <hero.huang@rockchips.com>");
MODULE_LICENSE("GPL v2");
