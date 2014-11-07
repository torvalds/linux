/*
 * sky81452-regulator.c	SKY81452 regulator driver
 *
 * Copyright 2014 Skyworks Solutions Inc.
 * Author : Gyungoh Yoo <jack.yoo@skyworksinc.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>

/* registers */
#define SKY81452_REG1	0x01
#define SKY81452_REG3	0x03

/* bit mask */
#define SKY81452_LEN	0x40
#define SKY81452_LOUT	0x1F

static struct regulator_ops sky81452_reg_ops = {
	.list_voltage = regulator_list_voltage_linear_range,
	.map_voltage = regulator_map_voltage_linear_range,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
};

static const struct regulator_linear_range sky81452_reg_ranges[] = {
	REGULATOR_LINEAR_RANGE(4500000, 0, 14, 250000),
	REGULATOR_LINEAR_RANGE(9000000, 15, 31, 1000000),
};

static const struct regulator_desc sky81452_reg = {
	.name = "LOUT",
	.ops = &sky81452_reg_ops,
	.type = REGULATOR_VOLTAGE,
	.owner = THIS_MODULE,
	.n_voltages = SKY81452_LOUT + 1,
	.linear_ranges = sky81452_reg_ranges,
	.n_linear_ranges = ARRAY_SIZE(sky81452_reg_ranges),
	.vsel_reg = SKY81452_REG3,
	.vsel_mask = SKY81452_LOUT,
	.enable_reg = SKY81452_REG1,
	.enable_mask = SKY81452_LEN,
};

#ifdef CONFIG_OF
static struct regulator_init_data *sky81452_reg_parse_dt(struct device *dev)
{
	struct regulator_init_data *init_data;
	struct device_node *np;

	np = of_get_child_by_name(dev->parent->of_node, "regulator");
	if (unlikely(!np)) {
		dev_err(dev, "regulator node not found");
		return NULL;
	}

	init_data = of_get_regulator_init_data(dev, np);

	of_node_put(np);
	return init_data;
}
#else
static struct regulator_init_data *sky81452_reg_parse_dt(struct device *dev)
{
	return ERR_PTR(-EINVAL);
}
#endif

static int sky81452_reg_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct regulator_init_data *init_data = dev_get_platdata(dev);
	struct regulator_config config = { };
	struct regulator_dev *rdev;

	if (!init_data) {
		init_data = sky81452_reg_parse_dt(dev);
		if (IS_ERR(init_data))
			return PTR_ERR(init_data);
	}

	config.dev = dev;
	config.init_data = init_data;
	config.of_node = dev->of_node;
	config.regmap = dev_get_drvdata(dev->parent);

	rdev = devm_regulator_register(dev, &sky81452_reg, &config);
	if (IS_ERR(rdev))
		return PTR_ERR(rdev);

	platform_set_drvdata(pdev, rdev);

	return 0;
}

static struct platform_driver sky81452_reg_driver = {
	.driver = {
		.name = "sky81452-regulator",
	},
	.probe = sky81452_reg_probe,
};

module_platform_driver(sky81452_reg_driver);

MODULE_DESCRIPTION("Skyworks SKY81452 Regulator driver");
MODULE_AUTHOR("Gyungoh Yoo <jack.yoo@skyworksinc.com>");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0");
