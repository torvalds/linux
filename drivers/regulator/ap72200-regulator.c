// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2023-2024, Qualcomm Innovation Center, Inc. All rights reserved. */

#define pr_fmt(fmt) "ap72200-reg: %s: " fmt, __func__

#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/regmap.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/debug-regulator.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/proxy-consumer.h>

#define AP72200_CFG_REG2_ADDR	0x3
#define AP72200_VSEL_REG_ADDR	0x4

#define AP72200_BB_EN_BIT	BIT(6)

#define AP72200_MIN_UV	2600000
#define AP72200_MAX_UV	5140000
#define AP72200_STEP_UV	20000

struct ap72200_vreg {
	struct device		*dev;
	struct device_node	*of_node;
	struct regulator_desc	rdesc;
	struct regulator_dev	*rdev;
	struct regmap		*regmap;
	struct gpio_desc	*ena_gpiod;
	bool			is_enabled;
};

static const struct regmap_config ap27700_regmap_config = {
	.reg_bits		= 8,
	.val_bits		= 8,
	.max_register		= 0x4,
};

#define AP72200_MAX_WRITE_RETRIES	4

static int ap72200_vreg_enable(struct regulator_dev *rdev)
{
	struct ap72200_vreg *vreg = rdev_get_drvdata(rdev);
	int rc, val, retries;

	gpiod_set_value_cansleep(vreg->ena_gpiod, 1);

	val = DIV_ROUND_UP(vreg->rdesc.fixed_uV - AP72200_MIN_UV, AP72200_STEP_UV);

	retries = AP72200_MAX_WRITE_RETRIES;
	do {
		/* Set the voltage */
		rc = regmap_write(vreg->regmap, AP72200_VSEL_REG_ADDR,
					val);
		if (!rc)
			break;
	} while (retries--);

	if (rc) {
		dev_err(vreg->dev, "Failed to set voltage rc: %d\n", rc);
		return rc;
	}

	/* Enable VOUT */
	rc = regmap_update_bits(vreg->regmap, AP72200_CFG_REG2_ADDR,
				AP72200_BB_EN_BIT, AP72200_BB_EN_BIT);
	if (rc) {
		dev_err(vreg->dev, "Failed to enable VOUT rc :%d\n", rc);
		return rc;
	}

	vreg->is_enabled = true;

	return rc;
}

static int ap72200_vreg_disable(struct regulator_dev *rdev)
{
	struct ap72200_vreg *vreg = rdev_get_drvdata(rdev);
	int rc;

	/* Disable VOUT */
	rc = regmap_update_bits(vreg->regmap, AP72200_CFG_REG2_ADDR,
					AP72200_BB_EN_BIT, 0);
	if (rc) {
		dev_err(vreg->dev, "Failed to disable VOUT rc :%d\n", rc);
		return rc;
	}

	vreg->is_enabled = false;

	gpiod_set_value_cansleep(vreg->ena_gpiod, 0);

	return rc;
}

static int ap72200_is_enabled(struct regulator_dev *rdev)
{
	struct ap72200_vreg *vreg = rdev_get_drvdata(rdev);

	return vreg->is_enabled;
}

static const struct regulator_ops ap72200_ops = {
	.enable			= ap72200_vreg_enable,
	.disable		= ap72200_vreg_disable,
	.is_enabled		= ap72200_is_enabled,
};

static int ap72200_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	struct ap72200_vreg *vreg;
	struct regulator_config reg_config = {};
	struct regulator_init_data *init_data;
	int ret;

	vreg = devm_kzalloc(&client->dev, sizeof(*vreg), GFP_KERNEL);
	if (!vreg)
		return -ENOMEM;

	vreg->regmap = devm_regmap_init_i2c(client, &ap27700_regmap_config);
	if (IS_ERR(vreg->regmap)) {
		ret = PTR_ERR(vreg->regmap);
		dev_err(&client->dev, "regmap init failed, err %d\n", ret);
		return ret;
	}

	vreg->dev               = &client->dev;
	vreg->of_node           = client->dev.of_node;
	vreg->rdesc.ops         = &ap72200_ops;
	vreg->rdesc.owner       = THIS_MODULE;
	vreg->rdesc.type        = REGULATOR_VOLTAGE;

	init_data = of_get_regulator_init_data(vreg->dev,
						vreg->of_node, &vreg->rdesc);
	if (init_data == NULL)
		return -ENODATA;

	if (init_data->constraints.name == NULL) {
		dev_err(&client->dev, "%s: regulator name not specified\n", __func__);
		return -EINVAL;
	}

	if (init_data->constraints.min_uV != init_data->constraints.max_uV) {
		dev_err(&client->dev,
			"Fixed regulator specified with variable voltages\n");
		return -EINVAL;
	}

	init_data->constraints.min_uV   = max(init_data->constraints.min_uV, AP72200_MIN_UV);
	init_data->constraints.min_uV   = min(init_data->constraints.min_uV, AP72200_MAX_UV);

	vreg->rdesc.n_voltages	= 1;
	vreg->rdesc.fixed_uV	= init_data->constraints.min_uV;
	vreg->rdesc.name	= init_data->constraints.name;

	reg_config.dev		= vreg->dev;
	reg_config.init_data	= init_data;
	reg_config.of_node	= vreg->of_node;
	reg_config.driver_data	= vreg;

	vreg->ena_gpiod = gpiod_get(vreg->dev, "enable", GPIOD_OUT_HIGH);
	if (IS_ERR(vreg->ena_gpiod)) {
		dev_err(&client->dev, "Failed to get enable GPIO\n");
		return PTR_ERR(vreg->ena_gpiod);
	}

	/* Keep the EN pin of this regulator low initially */
	gpiod_set_value_cansleep(vreg->ena_gpiod, 0);

	vreg->rdev = devm_regulator_register(vreg->dev, &vreg->rdesc, &reg_config);
	if (IS_ERR(vreg->rdev)) {
		ret = PTR_ERR(vreg->rdev);
		dev_err(vreg->dev, "Failed to register regulator: %d\n", ret);
		return ret;
	}

	ret = devm_regulator_debug_register(vreg->dev, vreg->rdev);
	if (ret)
		dev_err(vreg->dev, "Failed to register debug regulator, rc=%d\n", ret);

	return 0;
}

static const struct of_device_id ap72200_match_tbl[] = {
	{ .compatible = "diodes,ap72200", },
	{ }
};
MODULE_DEVICE_TABLE(of, ap72200_match_tbl);

static const struct i2c_device_id ap72200_id[] = {
	{ "ap72200", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ap72200_id);

static struct i2c_driver ap72200_i2c_driver = {
	.driver = {
		.name		= "ap72200",
		.of_match_table	= ap72200_match_tbl,
	},
	.probe		= ap72200_probe,
	.id_table	= ap72200_id,
};
module_i2c_driver(ap72200_i2c_driver);

MODULE_LICENSE("GPL");
MODULE_ALIAS("i2c:ap72200-regulator");
