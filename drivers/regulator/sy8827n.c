// SPDX-License-Identifier: GPL-2.0
//
// SY8827N regulator driver
//
// Copyright (C) 2020 Synaptics Incorporated
//
// Author: Jisheng Zhang <jszhang@kernel.org>

#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>

#define SY8827N_VSEL0		0
#define   SY8827N_BUCK_EN	(1 << 7)
#define   SY8827N_MODE		(1 << 6)
#define SY8827N_VSEL1		1
#define SY8827N_CTRL		2
#define SY8827N_ID1		3
#define SY8827N_ID2		4
#define SY8827N_PGOOD		5
#define SY8827N_MAX		(SY8827N_PGOOD + 1)

#define SY8827N_NVOLTAGES	64
#define SY8827N_VSELMIN		600000
#define SY8827N_VSELSTEP	12500

struct sy8827n_device_info {
	struct device *dev;
	struct regulator_desc desc;
	struct regulator_init_data *regulator;
	struct gpio_desc *en_gpio;
	unsigned int vsel_reg;
};

static int sy8827n_set_mode(struct regulator_dev *rdev, unsigned int mode)
{
	struct sy8827n_device_info *di = rdev_get_drvdata(rdev);

	switch (mode) {
	case REGULATOR_MODE_FAST:
		regmap_update_bits(rdev->regmap, di->vsel_reg,
				   SY8827N_MODE, SY8827N_MODE);
		break;
	case REGULATOR_MODE_NORMAL:
		regmap_update_bits(rdev->regmap, di->vsel_reg,
				   SY8827N_MODE, 0);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static unsigned int sy8827n_get_mode(struct regulator_dev *rdev)
{
	struct sy8827n_device_info *di = rdev_get_drvdata(rdev);
	u32 val;
	int ret = 0;

	ret = regmap_read(rdev->regmap, di->vsel_reg, &val);
	if (ret < 0)
		return ret;
	if (val & SY8827N_MODE)
		return REGULATOR_MODE_FAST;
	else
		return REGULATOR_MODE_NORMAL;
}

static const struct regulator_ops sy8827n_regulator_ops = {
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
	.map_voltage = regulator_map_voltage_linear,
	.list_voltage = regulator_list_voltage_linear,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.set_mode = sy8827n_set_mode,
	.get_mode = sy8827n_get_mode,
};

static int sy8827n_regulator_register(struct sy8827n_device_info *di,
			struct regulator_config *config)
{
	struct regulator_desc *rdesc = &di->desc;
	struct regulator_dev *rdev;

	rdesc->name = "sy8827n-reg";
	rdesc->supply_name = "vin";
	rdesc->ops = &sy8827n_regulator_ops;
	rdesc->type = REGULATOR_VOLTAGE;
	rdesc->n_voltages = SY8827N_NVOLTAGES;
	rdesc->enable_reg = di->vsel_reg;
	rdesc->enable_mask = SY8827N_BUCK_EN;
	rdesc->min_uV = SY8827N_VSELMIN;
	rdesc->uV_step = SY8827N_VSELSTEP;
	rdesc->vsel_reg = di->vsel_reg;
	rdesc->vsel_mask = rdesc->n_voltages - 1;
	rdesc->owner = THIS_MODULE;

	rdev = devm_regulator_register(di->dev, &di->desc, config);
	return PTR_ERR_OR_ZERO(rdev);
}

static bool sy8827n_volatile_reg(struct device *dev, unsigned int reg)
{
	if (reg == SY8827N_PGOOD)
		return true;
	return false;
}

static const struct regmap_config sy8827n_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.volatile_reg = sy8827n_volatile_reg,
	.num_reg_defaults_raw = SY8827N_MAX,
	.cache_type = REGCACHE_FLAT,
};

static int sy8827n_i2c_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct device_node *np = dev->of_node;
	struct sy8827n_device_info *di;
	struct regulator_config config = { };
	struct regmap *regmap;
	int ret;

	di = devm_kzalloc(dev, sizeof(struct sy8827n_device_info), GFP_KERNEL);
	if (!di)
		return -ENOMEM;

	di->regulator = of_get_regulator_init_data(dev, np, &di->desc);
	if (!di->regulator) {
		dev_err(dev, "Platform data not found!\n");
		return -EINVAL;
	}

	di->en_gpio = devm_gpiod_get_optional(dev, "enable", GPIOD_OUT_HIGH);
	if (IS_ERR(di->en_gpio))
		return PTR_ERR(di->en_gpio);

	if (of_property_read_bool(np, "silergy,vsel-state-high"))
		di->vsel_reg = SY8827N_VSEL1;
	else
		di->vsel_reg = SY8827N_VSEL0;

	di->dev = dev;

	regmap = devm_regmap_init_i2c(client, &sy8827n_regmap_config);
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

	ret = sy8827n_regulator_register(di, &config);
	if (ret < 0)
		dev_err(dev, "Failed to register regulator!\n");
	return ret;
}

#ifdef CONFIG_OF
static const struct of_device_id sy8827n_dt_ids[] = {
	{
		.compatible = "silergy,sy8827n",
	},
	{ }
};
MODULE_DEVICE_TABLE(of, sy8827n_dt_ids);
#endif

static const struct i2c_device_id sy8827n_id[] = {
	{ "sy8827n", },
	{ },
};
MODULE_DEVICE_TABLE(i2c, sy8827n_id);

static struct i2c_driver sy8827n_regulator_driver = {
	.driver = {
		.name = "sy8827n-regulator",
		.of_match_table = of_match_ptr(sy8827n_dt_ids),
	},
	.probe_new = sy8827n_i2c_probe,
	.id_table = sy8827n_id,
};
module_i2c_driver(sy8827n_regulator_driver);

MODULE_AUTHOR("Jisheng Zhang <jszhang@kernel.org>");
MODULE_DESCRIPTION("SY8827N regulator driver");
MODULE_LICENSE("GPL v2");
