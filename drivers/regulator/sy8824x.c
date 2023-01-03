// SPDX-License-Identifier: GPL-2.0
//
// SY8824C/SY8824E regulator driver
//
// Copyright (C) 2019 Synaptics Incorporated
//
// Author: Jisheng Zhang <jszhang@kernel.org>

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>

#define SY8824C_BUCK_EN		(1 << 7)
#define SY8824C_MODE		(1 << 6)

struct sy8824_config {
	/* registers */
	unsigned int vol_reg;
	unsigned int mode_reg;
	unsigned int enable_reg;
	/* Voltage range and step(linear) */
	unsigned int vsel_min;
	unsigned int vsel_step;
	unsigned int vsel_count;
	const struct regmap_config *config;
};

struct sy8824_device_info {
	struct device *dev;
	struct regulator_desc desc;
	struct regulator_init_data *regulator;
	const struct sy8824_config *cfg;
};

static int sy8824_set_mode(struct regulator_dev *rdev, unsigned int mode)
{
	struct sy8824_device_info *di = rdev_get_drvdata(rdev);
	const struct sy8824_config *cfg = di->cfg;

	switch (mode) {
	case REGULATOR_MODE_FAST:
		regmap_update_bits(rdev->regmap, cfg->mode_reg,
				   SY8824C_MODE, SY8824C_MODE);
		break;
	case REGULATOR_MODE_NORMAL:
		regmap_update_bits(rdev->regmap, cfg->mode_reg,
				   SY8824C_MODE, 0);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static unsigned int sy8824_get_mode(struct regulator_dev *rdev)
{
	struct sy8824_device_info *di = rdev_get_drvdata(rdev);
	const struct sy8824_config *cfg = di->cfg;
	u32 val;
	int ret = 0;

	ret = regmap_read(rdev->regmap, cfg->mode_reg, &val);
	if (ret < 0)
		return ret;
	if (val & SY8824C_MODE)
		return REGULATOR_MODE_FAST;
	else
		return REGULATOR_MODE_NORMAL;
}

static const struct regulator_ops sy8824_regulator_ops = {
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
	.map_voltage = regulator_map_voltage_linear,
	.list_voltage = regulator_list_voltage_linear,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.set_mode = sy8824_set_mode,
	.get_mode = sy8824_get_mode,
};

static int sy8824_regulator_register(struct sy8824_device_info *di,
			struct regulator_config *config)
{
	struct regulator_desc *rdesc = &di->desc;
	const struct sy8824_config *cfg = di->cfg;
	struct regulator_dev *rdev;

	rdesc->name = "sy8824-reg";
	rdesc->supply_name = "vin";
	rdesc->ops = &sy8824_regulator_ops;
	rdesc->type = REGULATOR_VOLTAGE;
	rdesc->n_voltages = cfg->vsel_count;
	rdesc->enable_reg = cfg->enable_reg;
	rdesc->enable_mask = SY8824C_BUCK_EN;
	rdesc->min_uV = cfg->vsel_min;
	rdesc->uV_step = cfg->vsel_step;
	rdesc->vsel_reg = cfg->vol_reg;
	rdesc->vsel_mask = cfg->vsel_count - 1;
	rdesc->owner = THIS_MODULE;

	rdev = devm_regulator_register(di->dev, &di->desc, config);
	return PTR_ERR_OR_ZERO(rdev);
}

static const struct regmap_config sy8824_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.num_reg_defaults_raw = 1,
	.cache_type = REGCACHE_FLAT,
};

static const struct regmap_config sy20276_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.num_reg_defaults_raw = 2,
	.cache_type = REGCACHE_FLAT,
};

static int sy8824_i2c_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct device_node *np = dev->of_node;
	struct sy8824_device_info *di;
	struct regulator_config config = { };
	struct regmap *regmap;
	int ret;

	di = devm_kzalloc(dev, sizeof(struct sy8824_device_info), GFP_KERNEL);
	if (!di)
		return -ENOMEM;

	di->regulator = of_get_regulator_init_data(dev, np, &di->desc);
	if (!di->regulator) {
		dev_err(dev, "Platform data not found!\n");
		return -EINVAL;
	}

	di->dev = dev;
	di->cfg = of_device_get_match_data(dev);

	regmap = devm_regmap_init_i2c(client, di->cfg->config);
	if (IS_ERR(regmap)) {
		dev_err(dev, "Failed to allocate regmap!\n");
		return PTR_ERR(regmap);
	}
	i2c_set_clientdata(client, di);

	config.dev = di->dev;
	config.init_data = di->regulator;
	config.regmap = regmap;
	config.driver_data = di;
	config.of_node = np;

	ret = sy8824_regulator_register(di, &config);
	if (ret < 0)
		dev_err(dev, "Failed to register regulator!\n");
	return ret;
}

static const struct sy8824_config sy8824c_cfg = {
	.vol_reg = 0x00,
	.mode_reg = 0x00,
	.enable_reg = 0x00,
	.vsel_min = 762500,
	.vsel_step = 12500,
	.vsel_count = 64,
	.config = &sy8824_regmap_config,
};

static const struct sy8824_config sy8824e_cfg = {
	.vol_reg = 0x00,
	.mode_reg = 0x00,
	.enable_reg = 0x00,
	.vsel_min = 700000,
	.vsel_step = 12500,
	.vsel_count = 64,
	.config = &sy8824_regmap_config,
};

static const struct sy8824_config sy20276_cfg = {
	.vol_reg = 0x00,
	.mode_reg = 0x01,
	.enable_reg = 0x01,
	.vsel_min = 600000,
	.vsel_step = 10000,
	.vsel_count = 128,
	.config = &sy20276_regmap_config,
};

static const struct sy8824_config sy20278_cfg = {
	.vol_reg = 0x00,
	.mode_reg = 0x01,
	.enable_reg = 0x01,
	.vsel_min = 762500,
	.vsel_step = 12500,
	.vsel_count = 64,
	.config = &sy20276_regmap_config,
};

static const struct of_device_id sy8824_dt_ids[] = {
	{
		.compatible = "silergy,sy8824c",
		.data = &sy8824c_cfg
	},
	{
		.compatible = "silergy,sy8824e",
		.data = &sy8824e_cfg
	},
	{
		.compatible = "silergy,sy20276",
		.data = &sy20276_cfg
	},
	{
		.compatible = "silergy,sy20278",
		.data = &sy20278_cfg
	},
	{ }
};
MODULE_DEVICE_TABLE(of, sy8824_dt_ids);

static const struct i2c_device_id sy8824_id[] = {
	{ "sy8824", },
	{ },
};
MODULE_DEVICE_TABLE(i2c, sy8824_id);

static struct i2c_driver sy8824_regulator_driver = {
	.driver = {
		.name = "sy8824-regulator",
		.of_match_table = sy8824_dt_ids,
	},
	.probe_new = sy8824_i2c_probe,
	.id_table = sy8824_id,
};
module_i2c_driver(sy8824_regulator_driver);

MODULE_AUTHOR("Jisheng Zhang <jszhang@kernel.org>");
MODULE_DESCRIPTION("SY8824C/SY8824E regulator driver");
MODULE_LICENSE("GPL v2");
