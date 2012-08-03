/*
 * tps65217.c
 *
 * TPS65217 chip family multi-function driver
 *
 * Copyright (C) 2011 Texas Instruments Incorporated - http://www.ti.com/
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/regmap.h>
#include <linux/err.h>
#include <linux/regulator/of_regulator.h>

#include <linux/mfd/core.h>
#include <linux/mfd/tps65217.h>

/**
 * tps65217_reg_read: Read a single tps65217 register.
 *
 * @tps: Device to read from.
 * @reg: Register to read.
 * @val: Contians the value
 */
int tps65217_reg_read(struct tps65217 *tps, unsigned int reg,
			unsigned int *val)
{
	return regmap_read(tps->regmap, reg, val);
}
EXPORT_SYMBOL_GPL(tps65217_reg_read);

/**
 * tps65217_reg_write: Write a single tps65217 register.
 *
 * @tps65217: Device to write to.
 * @reg: Register to write to.
 * @val: Value to write.
 * @level: Password protected level
 */
int tps65217_reg_write(struct tps65217 *tps, unsigned int reg,
			unsigned int val, unsigned int level)
{
	int ret;
	unsigned int xor_reg_val;

	switch (level) {
	case TPS65217_PROTECT_NONE:
		return regmap_write(tps->regmap, reg, val);
	case TPS65217_PROTECT_L1:
		xor_reg_val = reg ^ TPS65217_PASSWORD_REGS_UNLOCK;
		ret = regmap_write(tps->regmap, TPS65217_REG_PASSWORD,
							xor_reg_val);
		if (ret < 0)
			return ret;

		return regmap_write(tps->regmap, reg, val);
	case TPS65217_PROTECT_L2:
		xor_reg_val = reg ^ TPS65217_PASSWORD_REGS_UNLOCK;
		ret = regmap_write(tps->regmap, TPS65217_REG_PASSWORD,
							xor_reg_val);
		if (ret < 0)
			return ret;
		ret = regmap_write(tps->regmap, reg, val);
		if (ret < 0)
			return ret;
		ret = regmap_write(tps->regmap, TPS65217_REG_PASSWORD,
							xor_reg_val);
		if (ret < 0)
			return ret;
		return regmap_write(tps->regmap, reg, val);
	default:
		return -EINVAL;
	}
}
EXPORT_SYMBOL_GPL(tps65217_reg_write);

/**
 * tps65217_update_bits: Modify bits w.r.t mask, val and level.
 *
 * @tps65217: Device to write to.
 * @reg: Register to read-write to.
 * @mask: Mask.
 * @val: Value to write.
 * @level: Password protected level
 */
static int tps65217_update_bits(struct tps65217 *tps, unsigned int reg,
		unsigned int mask, unsigned int val, unsigned int level)
{
	int ret;
	unsigned int data;

	ret = tps65217_reg_read(tps, reg, &data);
	if (ret) {
		dev_err(tps->dev, "Read from reg 0x%x failed\n", reg);
		return ret;
	}

	data &= ~mask;
	data |= val & mask;

	ret = tps65217_reg_write(tps, reg, data, level);
	if (ret)
		dev_err(tps->dev, "Write for reg 0x%x failed\n", reg);

	return ret;
}

int tps65217_set_bits(struct tps65217 *tps, unsigned int reg,
		unsigned int mask, unsigned int val, unsigned int level)
{
	return tps65217_update_bits(tps, reg, mask, val, level);
}
EXPORT_SYMBOL_GPL(tps65217_set_bits);

int tps65217_clear_bits(struct tps65217 *tps, unsigned int reg,
		unsigned int mask, unsigned int level)
{
	return tps65217_update_bits(tps, reg, mask, 0, level);
}
EXPORT_SYMBOL_GPL(tps65217_clear_bits);

#ifdef CONFIG_OF
static struct of_regulator_match reg_matches[] = {
	{ .name = "dcdc1", .driver_data = (void *)TPS65217_DCDC_1 },
	{ .name = "dcdc2", .driver_data = (void *)TPS65217_DCDC_2 },
	{ .name = "dcdc3", .driver_data = (void *)TPS65217_DCDC_3 },
	{ .name = "ldo1", .driver_data = (void *)TPS65217_LDO_1 },
	{ .name = "ldo2", .driver_data = (void *)TPS65217_LDO_2 },
	{ .name = "ldo3", .driver_data = (void *)TPS65217_LDO_3 },
	{ .name = "ldo4", .driver_data = (void *)TPS65217_LDO_4 },
};

static struct tps65217_board *tps65217_parse_dt(struct i2c_client *client)
{
	struct device_node *node = client->dev.of_node;
	struct tps65217_board *pdata;
	struct device_node *regs;
	int count = ARRAY_SIZE(reg_matches);
	int ret, i;

	regs = of_find_node_by_name(node, "regulators");
	if (!regs)
		return NULL;

	ret = of_regulator_match(&client->dev, regs, reg_matches, count);
	of_node_put(regs);
	if ((ret < 0) || (ret > count))
		return NULL;

	count = ret;
	pdata = devm_kzalloc(&client->dev, count * sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return NULL;

	for (i = 0; i < count; i++) {
		if (!reg_matches[i].init_data || !reg_matches[i].of_node)
			continue;

		pdata->tps65217_init_data[i] = reg_matches[i].init_data;
		pdata->of_node[i] = reg_matches[i].of_node;
	}

	return pdata;
}

static struct of_device_id tps65217_of_match[] = {
	{ .compatible = "ti,tps65217", },
	{ },
};
#else
static struct tps65217_board *tps65217_parse_dt(struct i2c_client *client)
{
	return NULL;
}
#endif

static struct regmap_config tps65217_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static int __devinit tps65217_probe(struct i2c_client *client,
				const struct i2c_device_id *ids)
{
	struct tps65217 *tps;
	struct regulator_init_data *reg_data;
	struct tps65217_board *pdata = client->dev.platform_data;
	int i, ret;
	unsigned int version;

	if (!pdata && client->dev.of_node)
		pdata = tps65217_parse_dt(client);

	tps = devm_kzalloc(&client->dev, sizeof(*tps), GFP_KERNEL);
	if (!tps)
		return -ENOMEM;

	tps->pdata = pdata;
	tps->regmap = devm_regmap_init_i2c(client, &tps65217_regmap_config);
	if (IS_ERR(tps->regmap)) {
		ret = PTR_ERR(tps->regmap);
		dev_err(tps->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	i2c_set_clientdata(client, tps);
	tps->dev = &client->dev;

	ret = tps65217_reg_read(tps, TPS65217_REG_CHIPID, &version);
	if (ret < 0) {
		dev_err(tps->dev, "Failed to read revision register: %d\n",
			ret);
		return ret;
	}

	dev_info(tps->dev, "TPS65217 ID %#x version 1.%d\n",
			(version & TPS65217_CHIPID_CHIP_MASK) >> 4,
			version & TPS65217_CHIPID_REV_MASK);

	for (i = 0; i < TPS65217_NUM_REGULATOR; i++) {
		struct platform_device *pdev;

		pdev = platform_device_alloc("tps65217-pmic", i);
		if (!pdev) {
			dev_err(tps->dev, "Cannot create regulator %d\n", i);
			continue;
		}

		pdev->dev.parent = tps->dev;
		pdev->dev.of_node = pdata->of_node[i];
		reg_data = pdata->tps65217_init_data[i];
		platform_device_add_data(pdev, reg_data, sizeof(*reg_data));
		tps->regulator_pdev[i] = pdev;

		platform_device_add(pdev);
	}

	return 0;
}

static int __devexit tps65217_remove(struct i2c_client *client)
{
	struct tps65217 *tps = i2c_get_clientdata(client);
	int i;

	for (i = 0; i < TPS65217_NUM_REGULATOR; i++)
		platform_device_unregister(tps->regulator_pdev[i]);

	return 0;
}

static const struct i2c_device_id tps65217_id_table[] = {
	{"tps65217", 0xF0},
	{/* end of list */}
};
MODULE_DEVICE_TABLE(i2c, tps65217_id_table);

static struct i2c_driver tps65217_driver = {
	.driver		= {
		.name	= "tps65217",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(tps65217_of_match),
	},
	.id_table	= tps65217_id_table,
	.probe		= tps65217_probe,
	.remove		= __devexit_p(tps65217_remove),
};

static int __init tps65217_init(void)
{
	return i2c_add_driver(&tps65217_driver);
}
subsys_initcall(tps65217_init);

static void __exit tps65217_exit(void)
{
	i2c_del_driver(&tps65217_driver);
}
module_exit(tps65217_exit);

MODULE_AUTHOR("AnilKumar Ch <anilkumar@ti.com>");
MODULE_DESCRIPTION("TPS65217 chip family multi-function driver");
MODULE_LICENSE("GPL v2");
