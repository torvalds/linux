// SPDX-License-Identifier: GPL-2.0
//
// MP8867/MP8869 regulator driver
//
// Copyright (C) 2020 Synaptics Incorporated
//
// Author: Jisheng Zhang <jszhang@kernel.org>

#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>

#define MP886X_VSEL		0x00
#define  MP886X_V_BOOT		(1 << 7)
#define MP886X_SYSCNTLREG1	0x01
#define  MP886X_MODE		(1 << 0)
#define  MP886X_GO		(1 << 6)
#define  MP886X_EN		(1 << 7)

struct mp886x_device_info {
	struct device *dev;
	struct regulator_desc desc;
	struct regulator_init_data *regulator;
	struct gpio_desc *en_gpio;
	u32 r[2];
	unsigned int sel;
};

static int mp886x_set_mode(struct regulator_dev *rdev, unsigned int mode)
{
	switch (mode) {
	case REGULATOR_MODE_FAST:
		regmap_update_bits(rdev->regmap, MP886X_SYSCNTLREG1,
				   MP886X_MODE, MP886X_MODE);
		break;
	case REGULATOR_MODE_NORMAL:
		regmap_update_bits(rdev->regmap, MP886X_SYSCNTLREG1,
				   MP886X_MODE, 0);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static unsigned int mp886x_get_mode(struct regulator_dev *rdev)
{
	u32 val;
	int ret;

	ret = regmap_read(rdev->regmap, MP886X_SYSCNTLREG1, &val);
	if (ret < 0)
		return ret;
	if (val & MP886X_MODE)
		return REGULATOR_MODE_FAST;
	else
		return REGULATOR_MODE_NORMAL;
}

static int mp8869_set_voltage_sel(struct regulator_dev *rdev, unsigned int sel)
{
	int ret;

	ret = regmap_update_bits(rdev->regmap, MP886X_SYSCNTLREG1,
				 MP886X_GO, MP886X_GO);
	if (ret < 0)
		return ret;

	sel <<= ffs(rdev->desc->vsel_mask) - 1;
	return regmap_update_bits(rdev->regmap, rdev->desc->vsel_reg,
				  MP886X_V_BOOT | rdev->desc->vsel_mask, sel);
}

static inline unsigned int mp8869_scale(unsigned int uv, u32 r1, u32 r2)
{
	u32 tmp = uv * r1 / r2;

	return uv + tmp;
}

static int mp8869_get_voltage_sel(struct regulator_dev *rdev)
{
	struct mp886x_device_info *di = rdev_get_drvdata(rdev);
	int ret, uv;
	unsigned int val;
	bool fbloop;

	ret = regmap_read(rdev->regmap, rdev->desc->vsel_reg, &val);
	if (ret)
		return ret;

	fbloop = val & MP886X_V_BOOT;
	if (fbloop) {
		uv = rdev->desc->min_uV;
		uv = mp8869_scale(uv, di->r[0], di->r[1]);
		return regulator_map_voltage_linear(rdev, uv, uv);
	}

	val &= rdev->desc->vsel_mask;
	val >>= ffs(rdev->desc->vsel_mask) - 1;

	return val;
}

static const struct regulator_ops mp8869_regulator_ops = {
	.set_voltage_sel = mp8869_set_voltage_sel,
	.get_voltage_sel = mp8869_get_voltage_sel,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
	.map_voltage = regulator_map_voltage_linear,
	.list_voltage = regulator_list_voltage_linear,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.set_mode = mp886x_set_mode,
	.get_mode = mp886x_get_mode,
};

static int mp8867_set_voltage_sel(struct regulator_dev *rdev, unsigned int sel)
{
	struct mp886x_device_info *di = rdev_get_drvdata(rdev);
	int ret, delta;

	ret = mp8869_set_voltage_sel(rdev, sel);
	if (ret < 0)
		return ret;

	delta = di->sel - sel;
	if (abs(delta) <= 5)
		ret = regmap_update_bits(rdev->regmap, MP886X_SYSCNTLREG1,
					 MP886X_GO, 0);
	di->sel = sel;

	return ret;
}

static int mp8867_get_voltage_sel(struct regulator_dev *rdev)
{
	struct mp886x_device_info *di = rdev_get_drvdata(rdev);
	int ret, uv;
	unsigned int val;
	bool fbloop;

	ret = regmap_read(rdev->regmap, rdev->desc->vsel_reg, &val);
	if (ret)
		return ret;

	fbloop = val & MP886X_V_BOOT;

	val &= rdev->desc->vsel_mask;
	val >>= ffs(rdev->desc->vsel_mask) - 1;

	if (fbloop) {
		uv = regulator_list_voltage_linear(rdev, val);
		uv = mp8869_scale(uv, di->r[0], di->r[1]);
		return regulator_map_voltage_linear(rdev, uv, uv);
	}

	return val;
}

static const struct regulator_ops mp8867_regulator_ops = {
	.set_voltage_sel = mp8867_set_voltage_sel,
	.get_voltage_sel = mp8867_get_voltage_sel,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
	.map_voltage = regulator_map_voltage_linear,
	.list_voltage = regulator_list_voltage_linear,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.set_mode = mp886x_set_mode,
	.get_mode = mp886x_get_mode,
};

static int mp886x_regulator_register(struct mp886x_device_info *di,
				     struct regulator_config *config)
{
	struct regulator_desc *rdesc = &di->desc;
	struct regulator_dev *rdev;

	rdesc->name = "mp886x-reg";
	rdesc->supply_name = "vin";
	rdesc->ops = of_device_get_match_data(di->dev);
	rdesc->type = REGULATOR_VOLTAGE;
	rdesc->n_voltages = 128;
	rdesc->enable_reg = MP886X_SYSCNTLREG1;
	rdesc->enable_mask = MP886X_EN;
	rdesc->min_uV = 600000;
	rdesc->uV_step = 10000;
	rdesc->vsel_reg = MP886X_VSEL;
	rdesc->vsel_mask = 0x3f;
	rdesc->owner = THIS_MODULE;

	rdev = devm_regulator_register(di->dev, &di->desc, config);
	if (IS_ERR(rdev))
		return PTR_ERR(rdev);
	di->sel = rdesc->ops->get_voltage_sel(rdev);
	return 0;
}

static const struct regmap_config mp886x_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static int mp886x_i2c_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *np = dev->of_node;
	struct mp886x_device_info *di;
	struct regulator_config config = { };
	struct regmap *regmap;
	int ret;

	di = devm_kzalloc(dev, sizeof(struct mp886x_device_info), GFP_KERNEL);
	if (!di)
		return -ENOMEM;

	di->regulator = of_get_regulator_init_data(dev, np, &di->desc);
	if (!di->regulator) {
		dev_err(dev, "Platform data not found!\n");
		return -EINVAL;
	}

	ret = of_property_read_u32_array(np, "mps,fb-voltage-divider",
					 di->r, 2);
	if (ret)
		return ret;

	di->en_gpio = devm_gpiod_get(dev, "enable", GPIOD_OUT_HIGH);
	if (IS_ERR(di->en_gpio))
		return PTR_ERR(di->en_gpio);

	di->dev = dev;

	regmap = devm_regmap_init_i2c(client, &mp886x_regmap_config);
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

	ret = mp886x_regulator_register(di, &config);
	if (ret < 0)
		dev_err(dev, "Failed to register regulator!\n");
	return ret;
}

static const struct of_device_id mp886x_dt_ids[] = {
	{
		.compatible = "mps,mp8867",
		.data = &mp8867_regulator_ops
	},
	{
		.compatible = "mps,mp8869",
		.data = &mp8869_regulator_ops
	},
	{ }
};
MODULE_DEVICE_TABLE(of, mp886x_dt_ids);

static const struct i2c_device_id mp886x_id[] = {
	{ "mp886x", },
	{ },
};
MODULE_DEVICE_TABLE(i2c, mp886x_id);

static struct i2c_driver mp886x_regulator_driver = {
	.driver = {
		.name = "mp886x-regulator",
		.of_match_table = of_match_ptr(mp886x_dt_ids),
	},
	.probe = mp886x_i2c_probe,
	.id_table = mp886x_id,
};
module_i2c_driver(mp886x_regulator_driver);

MODULE_AUTHOR("Jisheng Zhang <jszhang@kernel.org>");
MODULE_DESCRIPTION("MP886x regulator driver");
MODULE_LICENSE("GPL v2");
