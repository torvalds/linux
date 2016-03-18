/*
 * Regulator driver for mp8865
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for  more details.
 */

#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>

#define MP8865_MAX_VSEL			0x7f
#define VOL_MIN_IDX			0x00
#define VOL_MAX_IDX			0x7f
#define MP8865_ENABLE_TIME		150

/* Register definitions */
#define MP8865_VOUT_REG			0
#define MP8865_SYSCNTL_REG		1
#define MP8865_ID_REG			2
#define MP8865_STATUS_REG		3
#define MP8865_MAX_REG			4

#define MP8865_VOUT_MASK		0x7f
#define MP8865_VBOOT_MASK		0x80

#define MP8865_ENABLE			0x80
#define MP8865_GO_BIT			0x40
#define MP8865_SLEW_RATE_MASK		0x38
#define MP8865_SWITCH_FRE_MASK		0x06
#define MP8865_PFM_MODE			0X01

/* mp8865 chip information */
struct mp8865_chip {
	struct device *dev;
	struct regulator_desc desc;
	struct regulator_dev  *rdev;
	struct regmap *regmap;
};

struct mp8865_platform_data {
	struct regulator_init_data *mp8865_init_data;
	struct device_node *of_node;
};

static const struct linear_range mp8865_voltage_ranges[] = {
	REGULATOR_LINEAR_RANGE(600000, VOL_MIN_IDX, VOL_MAX_IDX, 10000),
};

static int mp8865_set_voltage(struct regulator_dev *rdev, unsigned sel)
{
	int ret;

	ret = regmap_update_bits(rdev->regmap, MP8865_SYSCNTL_REG,
				 MP8865_GO_BIT, MP8865_GO_BIT);
	if (ret)
		return ret;

	sel <<= ffs(rdev->desc->vsel_mask) - 1;
	return regmap_write(rdev->regmap, MP8865_VOUT_REG, sel);
}

static bool is_write_reg(struct device *dev, unsigned int reg)
{
	return (reg < MP8865_ID_REG) ? true : false;
}

static bool is_volatile_reg(struct device *dev, unsigned int reg)
{
	return (reg == MP8865_STATUS_REG) ? true : false;
}

static const struct regmap_config mp8865_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.writeable_reg = is_write_reg,
	.volatile_reg = is_volatile_reg,
	.max_register = MP8865_MAX_REG,
	.cache_type = REGCACHE_RBTREE,
};

static struct regulator_ops mp8865_dcdc_ops = {
	.list_voltage = regulator_list_voltage_linear_range,
	.map_voltage = regulator_map_voltage_linear_range,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_sel = mp8865_set_voltage,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
};

static const struct of_device_id mp8865_of_match[] = {
	{.compatible =  "mps,mp8865", .data = (void *)0},
	{},
};
MODULE_DEVICE_TABLE(of, mp8865_of_match);

static struct of_regulator_match mp8865_matches = {
	.name = "mp8865_dcdc1",
	.driver_data = (void *)0,
};

static struct mp8865_platform_data *
	mp8865_parse_dt(struct i2c_client *client,
			struct of_regulator_match **mp8865_reg_matches)
{
	struct mp8865_platform_data *pdata;
	struct device_node *np, *regulators;
	int ret;

	pdata = devm_kzalloc(&client->dev, sizeof(struct mp8865_platform_data),
			     GFP_KERNEL);
	if (!pdata)
		return NULL;

	np = of_node_get(client->dev.of_node);
	regulators = of_find_node_by_name(np, "regulators");
	if (!regulators) {
		dev_err(&client->dev, "regulator node not found\n");
		return NULL;
	}

	ret = of_regulator_match(&client->dev, regulators, &mp8865_matches, 1);
	if (ret < 0) {
		dev_err(&client->dev, "Error parsing regulators init data\n");
		return NULL;
	}
	*mp8865_reg_matches = &mp8865_matches;
	of_node_put(regulators);

	pdata->mp8865_init_data = mp8865_matches.init_data;
	pdata->of_node = mp8865_matches.of_node;

	return pdata;
}

static int mp8865_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct mp8865_chip *mp8865;
	struct mp8865_platform_data *pdata;
	struct of_regulator_match *mp8865_reg_matches = NULL;
	struct regulator_dev *sy_rdev;
	struct regulator_config config;
	struct regulator_desc *rdesc;
	int ret;

	mp8865 = devm_kzalloc(&client->dev, sizeof(struct mp8865_chip),
			      GFP_KERNEL);
	if (!mp8865)
		return -ENOMEM;
	pdata = dev_get_platdata(&client->dev);
	if (!pdata)
		pdata = mp8865_parse_dt(client, &mp8865_reg_matches);

	if (!pdata || !pdata->mp8865_init_data) {
		dev_err(&client->dev, "Platform data not found!\n");
		return -ENODEV;
	}

	mp8865->dev = &client->dev;
	mp8865->regmap = devm_regmap_init_i2c(client, &mp8865_regmap_config);
	if (IS_ERR(mp8865->regmap)) {
		dev_err(&client->dev, "Failed to allocate regmap!\n");
		return PTR_ERR(mp8865->regmap);
	}

	i2c_set_clientdata(client, mp8865);

	config.dev = mp8865->dev;
	config.driver_data = mp8865;
	config.init_data = pdata->mp8865_init_data;
	config.of_node = pdata->of_node;
	config.regmap = mp8865->regmap;

	rdesc = &mp8865->desc;
	rdesc->name = "mp8865_dcdc1",
	rdesc->id = 0,
	rdesc->ops = &mp8865_dcdc_ops,
	rdesc->n_voltages = MP8865_MAX_VSEL + 1,
	rdesc->linear_ranges = mp8865_voltage_ranges,
	rdesc->n_linear_ranges = ARRAY_SIZE(mp8865_voltage_ranges),
	rdesc->vsel_reg = MP8865_VOUT_REG;
	rdesc->vsel_mask = MP8865_VOUT_MASK;
	rdesc->enable_reg = MP8865_SYSCNTL_REG;
	rdesc->enable_mask = MP8865_ENABLE;
	rdesc->enable_time = MP8865_ENABLE_TIME;
	rdesc->type = REGULATOR_VOLTAGE,
	rdesc->owner = THIS_MODULE;

	/* set slew_rate 16mV/uS */
	ret = regmap_update_bits(mp8865->regmap, MP8865_SYSCNTL_REG,
				 MP8865_SLEW_RATE_MASK, 0x10);
	if (ret) {
		dev_err(mp8865->dev, "failed to set slew_rate\n");
		return ret;
	}

	/* set switch_frequency 1.1MHz */
	ret = regmap_update_bits(mp8865->regmap, MP8865_SYSCNTL_REG,
				 MP8865_SWITCH_FRE_MASK, 0x40);
	if (ret) {
		dev_err(mp8865->dev, "failed to set switch_frequency\n");
		return ret;
	}

	sy_rdev = devm_regulator_register(mp8865->dev, &mp8865->desc,
					  &config);
	if (IS_ERR(sy_rdev)) {
		dev_err(mp8865->dev, "failed to register regulator\n");
		return PTR_ERR(sy_rdev);
	}

	dev_info(mp8865->dev, "mp8865 register successful\n");

	return 0;
}

static const struct i2c_device_id mp8865_id[] = {
	{ .name = "mps,mp8865", },
	{  },
};
MODULE_DEVICE_TABLE(i2c, mp8865_id);

static struct i2c_driver mp8865_driver = {
	.driver = {
		.name = "mp8865",
		.of_match_table = of_match_ptr(mp8865_of_match),
		.owner = THIS_MODULE,
	},
	.probe    = mp8865_probe,
	.id_table = mp8865_id,
};
module_i2c_driver(mp8865_driver);

MODULE_AUTHOR("Zain Wang <zain.wang@rock-chips.com>");
MODULE_DESCRIPTION("mp8865 voltage regulator driver");
MODULE_LICENSE("GPL v2");
