/*
 * Copyright (c) 2017-2018 Rockchip Electronics Co. Ltd.
 * Author: XiaoDong Huang <derrick.huang@rock-chips.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/bug.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/driver.h>
#include <linux/regmap.h>
#include <linux/types.h>
#include <linux/uaccess.h>

#define TPS549b22_REG_OPERATION		0x01
#define TPS549b22_REG_ON_OFF_CFG	0x02
#define TPS549b22_REG_WRITE_PROTECT	0x10
#define TPS549b22_REG_COMMAND		0x21
#define TPS549b22_REG_MRG_H		0x25
#define TPS549b22_REG_MRG_L		0x26
#define TPS549b22_REG_ST_BYTE		0x78
#define TPS549b22_REG_MFR_SPC_44	0xfc

#define TPS549b22_ID			0x0200

#define VOL_MSK				0x3ff
#define VOL_OFF_MSK			0x40
#define OPERATION_ON_MSK		0x80
#define OPERATION_MRG_MSK		0x3c
#define ON_OFF_CFG_OPT_MSK		0x0c
#define VOL_MIN_IDX			0x133
#define VOL_MAX_IDX			0x266
#define VOL_STEP			2500

#define VOL2REG(vol_sel) \
		(((vol_sel) / VOL_STEP) & VOL_MSK)
#define REG2VOL(val) \
		(VOL_STEP * ((val) & VOL_MSK))

#define TPS549b22_NUM_REGULATORS	1

struct tps549b22 {
	struct device *dev;
	struct i2c_client *i2c;
	int num_regulators;
	struct regulator_dev **rdev;
	struct regmap *regmap_8bits;
	struct regmap *regmap_16bits;
};

struct tps549b22_board {
	struct regulator_init_data
		*tps549b22_init_data[TPS549b22_NUM_REGULATORS];
	struct device_node *of_node[TPS549b22_NUM_REGULATORS];
};

struct tps549b22_regulator_subdev {
	int id;
	struct regulator_init_data *initdata;
	struct device_node *reg_node;
};

static int tps549b22_dcdc_list_voltage(struct regulator_dev *rdev,
				       unsigned int index)
{
	if (index + VOL_MIN_IDX > VOL_MAX_IDX)
		return -EINVAL;

	return REG2VOL(index + VOL_MIN_IDX);
}

static int tps549b22_reg_init(struct tps549b22 *tps549b22)
{
	if (regmap_update_bits(tps549b22->regmap_8bits,
			       TPS549b22_REG_OPERATION,
			       OPERATION_ON_MSK,
			       0x80) == 0)
		return regmap_update_bits(tps549b22->regmap_8bits,
					  TPS549b22_REG_ON_OFF_CFG,
					  ON_OFF_CFG_OPT_MSK,
					  0x0c);

	dev_err(tps549b22->dev, "regulator init err\n");

	return -1;
}

static int tps549b22dcdc_is_enabled(struct regulator_dev *rdev)
{
	struct tps549b22 *tps549b22 = rdev_get_drvdata(rdev);
	int err;
	u32 val;

	err = regmap_read(tps549b22->regmap_8bits, TPS549b22_REG_ST_BYTE, &val);
	if (err)
		return 0;

	return !(val & VOL_OFF_MSK);
}

static int tps549b22dcdc_enable(struct regulator_dev *rdev)
{
	struct tps549b22 *tps549b22 = rdev_get_drvdata(rdev);

	return regmap_update_bits(tps549b22->regmap_8bits,
				  TPS549b22_REG_OPERATION,
				  OPERATION_ON_MSK,
				  0x80);
}

static int tps549b22dcdc_disable(struct regulator_dev *rdev)
{
	struct tps549b22 *tps549b22 = rdev_get_drvdata(rdev);

	return regmap_update_bits(tps549b22->regmap_8bits,
				  TPS549b22_REG_OPERATION,
				  OPERATION_ON_MSK,
				  0);
}

static int tps549b22dcdc_get_voltage(struct regulator_dev *rdev)
{
	struct tps549b22 *tps549b22 = rdev_get_drvdata(rdev);
	int err;
	u32 val = 0;

	err = regmap_read(tps549b22->regmap_16bits,
			  TPS549b22_REG_COMMAND,
			  &val);
	if (!err)
		return REG2VOL(val);

	return -1;
}

static int tps549b22dcdc_set_voltage(struct regulator_dev *rdev,
				     int min_uV,
				     int max_uV,
				     unsigned int *selector)
{
	struct tps549b22 *tps549b22 = rdev_get_drvdata(rdev);
	u32 val;
	int err;

	if (min_uV < REG2VOL(VOL_MIN_IDX) ||
	    min_uV > REG2VOL(VOL_MAX_IDX)) {
		dev_warn(rdev_get_dev(rdev),
			 "this voltage is out of limit! voltage set is %d mv\n",
			 REG2VOL(min_uV));
		return -EINVAL;
	}

	for (val = VOL_MIN_IDX; val <= VOL_MAX_IDX; val++)
		if (REG2VOL(val) >= min_uV)
			break;

	if (REG2VOL(val) > max_uV)
		dev_warn(rdev_get_dev(rdev),
			 "this voltage is not support! voltage set is %d mv\n",
			 REG2VOL(val));

	err = regmap_update_bits(tps549b22->regmap_16bits,
				 TPS549b22_REG_COMMAND,
				 VOL_MSK,
				 val);
	if (err)
		dev_err(rdev_get_dev(rdev),
			"set voltage is error! voltage set is %d mv\n",
			REG2VOL(val));

	return err;
}

static struct regulator_ops tps549b22dcdc_ops = {
	.set_voltage = tps549b22dcdc_set_voltage,
	.get_voltage = tps549b22dcdc_get_voltage,
	.is_enabled = tps549b22dcdc_is_enabled,
	.enable = tps549b22dcdc_enable,
	.disable = tps549b22dcdc_disable,
	.list_voltage = tps549b22_dcdc_list_voltage,
};

static struct regulator_desc regulators[] = {
	{
		.name = "tps549b22_DCDC1",
		.id = 0,
		.ops = &tps549b22dcdc_ops,
		.n_voltages = VOL_MAX_IDX - VOL_MIN_IDX + 1,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
};

static struct of_regulator_match tps549b22_reg_matches[] = {
	{ .name = "tps549b22_dcdc1", .driver_data = (void *)0},
};

static struct tps549b22_board *
tps549b22_parse_dt(struct tps549b22 *tps549b22)
{
	struct tps549b22_board *pdata;
	struct device_node *regs;
	struct device_node *tps549b22_np;
	int count;

	tps549b22_np = of_node_get(tps549b22->dev->of_node);
	if (!tps549b22_np) {
		pr_err("could not find pmic sub-node\n");
		goto err;
	}

	regs = of_get_child_by_name(tps549b22_np, "regulators");
	if (!regs)
		goto err;

	count = of_regulator_match(tps549b22->dev,
				   regs,
				   tps549b22_reg_matches,
				   TPS549b22_NUM_REGULATORS);
	of_node_put(regs);
	of_node_put(tps549b22_np);

	if (count <= 0)
		goto err;

	pdata = devm_kzalloc(tps549b22->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		goto err;

	pdata->tps549b22_init_data[0] = tps549b22_reg_matches[0].init_data;
	pdata->of_node[0] = tps549b22_reg_matches[0].of_node;

	return pdata;

err:
	return NULL;
}

static const struct of_device_id tps549b22_of_match[] = {
	{.compatible = "ti,tps549b22"},
	{ },
};
MODULE_DEVICE_TABLE(of, tps549b22_of_match);

static const struct regmap_config tps549b22_8bits_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = TPS549b22_REG_MFR_SPC_44 + 1,
	.cache_type = REGCACHE_NONE,
};

static const struct regmap_config tps549b22_16bits_regmap_config = {
	.reg_bits = 8,
	.val_bits = 16,
	.max_register = TPS549b22_REG_MFR_SPC_44 + 1,
	.cache_type = REGCACHE_NONE,
	.val_format_endian = REGMAP_ENDIAN_LITTLE,
};

static int tps549b22_i2c_probe(struct i2c_client *i2c,
			       const struct i2c_device_id *id)
{
	struct tps549b22 *tps549b22;
	struct tps549b22_board *pdev = NULL;
	const struct of_device_id *match;
	struct regulator_config config = { };
	struct regulator_dev *rdev;
	struct regulator_init_data *reg_data;
	int ret;
	u32 val;

	if (i2c->dev.of_node) {
		match = of_match_device(tps549b22_of_match, &i2c->dev);
		if (!match) {
			pr_err("Failed to find matching dt id\n");
			return -EINVAL;
		}
	}

	tps549b22 = devm_kzalloc(&i2c->dev,
				 sizeof(struct tps549b22),
				 GFP_KERNEL);
	if (!tps549b22) {
		ret = -ENOMEM;
		goto err;
	}

	tps549b22->regmap_8bits =
		devm_regmap_init_i2c(i2c, &tps549b22_8bits_regmap_config);
	if (IS_ERR(tps549b22->regmap_8bits)) {
		dev_err(&i2c->dev, "8 bits regmap initialization failed\n");
		return PTR_ERR(tps549b22->regmap_8bits);
	}

	tps549b22->regmap_16bits =
		devm_regmap_init_i2c(i2c, &tps549b22_16bits_regmap_config);
	if (IS_ERR(tps549b22->regmap_16bits)) {
		dev_err(&i2c->dev, "16 bits regmap initialization failed\n");
		return PTR_ERR(tps549b22->regmap_16bits);
	}

	tps549b22->i2c = i2c;
	tps549b22->dev = &i2c->dev;
	i2c_set_clientdata(i2c, tps549b22);

	ret = regmap_read(tps549b22->regmap_16bits,
			  TPS549b22_REG_MFR_SPC_44,
			  &val);
	if (!ret) {
		if (val != TPS549b22_ID)
			dev_warn(tps549b22->dev,
				 "The device is not tps549b22 0x%x\n",
				 val);
	} else {
		dev_err(tps549b22->dev,
			"Tps549b22_reg_read err, ret = %d\n",
			ret);
		return -EINVAL;
	}

	tps549b22_reg_init(tps549b22);

	if (tps549b22->dev->of_node)
		pdev = tps549b22_parse_dt(tps549b22);

	if (pdev) {
		tps549b22->num_regulators = TPS549b22_NUM_REGULATORS;
		tps549b22->rdev =
			devm_kmalloc_array(tps549b22->dev,
					   TPS549b22_NUM_REGULATORS,
					   sizeof(struct regulator_dev *),
					   GFP_KERNEL);
		if (!tps549b22->rdev)
			return -ENOMEM;

		/* Instantiate the regulators */
		reg_data = pdev->tps549b22_init_data[0];
		config.dev = tps549b22->dev;
		config.driver_data = tps549b22;
		if (tps549b22->dev->of_node)
			config.of_node = pdev->of_node[0];

		config.init_data = reg_data;

		rdev = devm_regulator_register(tps549b22->dev,
					       &regulators[0],
					       &config);
		if (IS_ERR(rdev)) {
			pr_err("failed to register regulator\n");
			goto err;
		}

		tps549b22->rdev[0] = rdev;
	}

	return 0;
err:
	return ret;
}

static int tps549b22_i2c_remove(struct i2c_client *i2c)
{
	i2c_set_clientdata(i2c, NULL);

	return 0;
}

static const struct i2c_device_id tps549b22_i2c_id[] = {
	{"tps549b22", 0},
	{ }
};

MODULE_DEVICE_TABLE(i2c, tps549b22_i2c_id);

static struct i2c_driver tps549b22_i2c_driver = {
	.driver = {
		.name = "tps549b22",
		.of_match_table = of_match_ptr(tps549b22_of_match),
	},
	.probe = tps549b22_i2c_probe,
	.remove = tps549b22_i2c_remove,
	.id_table = tps549b22_i2c_id,
};

static int __init tps549b22_module_init(void)
{
	int ret;

	ret = i2c_add_driver(&tps549b22_i2c_driver);

	if (ret != 0)
		pr_err("Failed to register I2C driver: %d\n", ret);

	return ret;
}
subsys_initcall_sync(tps549b22_module_init);

static void __exit tps549b22_module_exit(void)
{
	i2c_del_driver(&tps549b22_i2c_driver);
}
module_exit(tps549b22_module_exit);

MODULE_LICENSE("GPL");

MODULE_AUTHOR("derrick.huang@rock-chips.com");
MODULE_DESCRIPTION("   tps549b22 dcdc driver");
