// SPDX-License-Identifier: GPL-2.0
//
// Regulator Driver for Freescale MC13xxx PMIC
//
// Copyright 2010 Yong Shen <yong.shen@linaro.org>
//
// Based on mc13783 regulator driver :
// Copyright (C) 2008 Sascha Hauer, Pengutronix <s.hauer@pengutronix.de>
// Copyright 2009 Alberto Panizzo <maramaopercheseimorto@gmail.com>
//
// Regs infos taken from mc13xxx drivers from freescale and mc13xxx.pdf file
// from freescale

#include <linux/mfd/mc13xxx.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/of.h>
#include "mc13xxx.h"

static int mc13xxx_regulator_enable(struct regulator_dev *rdev)
{
	struct mc13xxx_regulator_priv *priv = rdev_get_drvdata(rdev);
	struct mc13xxx_regulator *mc13xxx_regulators = priv->mc13xxx_regulators;
	int id = rdev_get_id(rdev);

	dev_dbg(rdev_get_dev(rdev), "%s id: %d\n", __func__, id);

	return mc13xxx_reg_rmw(priv->mc13xxx, mc13xxx_regulators[id].reg,
			       mc13xxx_regulators[id].enable_bit,
			       mc13xxx_regulators[id].enable_bit);
}

static int mc13xxx_regulator_disable(struct regulator_dev *rdev)
{
	struct mc13xxx_regulator_priv *priv = rdev_get_drvdata(rdev);
	struct mc13xxx_regulator *mc13xxx_regulators = priv->mc13xxx_regulators;
	int id = rdev_get_id(rdev);

	dev_dbg(rdev_get_dev(rdev), "%s id: %d\n", __func__, id);

	return mc13xxx_reg_rmw(priv->mc13xxx, mc13xxx_regulators[id].reg,
			       mc13xxx_regulators[id].enable_bit, 0);
}

static int mc13xxx_regulator_is_enabled(struct regulator_dev *rdev)
{
	struct mc13xxx_regulator_priv *priv = rdev_get_drvdata(rdev);
	struct mc13xxx_regulator *mc13xxx_regulators = priv->mc13xxx_regulators;
	int ret, id = rdev_get_id(rdev);
	unsigned int val;

	ret = mc13xxx_reg_read(priv->mc13xxx, mc13xxx_regulators[id].reg, &val);
	if (ret)
		return ret;

	return (val & mc13xxx_regulators[id].enable_bit) != 0;
}

static int mc13xxx_regulator_set_voltage_sel(struct regulator_dev *rdev,
					     unsigned selector)
{
	struct mc13xxx_regulator_priv *priv = rdev_get_drvdata(rdev);
	struct mc13xxx_regulator *mc13xxx_regulators = priv->mc13xxx_regulators;
	int id = rdev_get_id(rdev);

	return mc13xxx_reg_rmw(priv->mc13xxx, mc13xxx_regulators[id].vsel_reg,
			       mc13xxx_regulators[id].vsel_mask,
			       selector << mc13xxx_regulators[id].vsel_shift);
}

static int mc13xxx_regulator_get_voltage(struct regulator_dev *rdev)
{
	struct mc13xxx_regulator_priv *priv = rdev_get_drvdata(rdev);
	struct mc13xxx_regulator *mc13xxx_regulators = priv->mc13xxx_regulators;
	int ret, id = rdev_get_id(rdev);
	unsigned int val;

	dev_dbg(rdev_get_dev(rdev), "%s id: %d\n", __func__, id);

	ret = mc13xxx_reg_read(priv->mc13xxx,
				mc13xxx_regulators[id].vsel_reg, &val);
	if (ret)
		return ret;

	val = (val & mc13xxx_regulators[id].vsel_mask)
		>> mc13xxx_regulators[id].vsel_shift;

	dev_dbg(rdev_get_dev(rdev), "%s id: %d val: %d\n", __func__, id, val);

	BUG_ON(val >= mc13xxx_regulators[id].desc.n_voltages);

	return rdev->desc->volt_table[val];
}

struct regulator_ops mc13xxx_regulator_ops = {
	.enable = mc13xxx_regulator_enable,
	.disable = mc13xxx_regulator_disable,
	.is_enabled = mc13xxx_regulator_is_enabled,
	.list_voltage = regulator_list_voltage_table,
	.set_voltage_sel = mc13xxx_regulator_set_voltage_sel,
	.get_voltage = mc13xxx_regulator_get_voltage,
};
EXPORT_SYMBOL_GPL(mc13xxx_regulator_ops);

int mc13xxx_fixed_regulator_set_voltage(struct regulator_dev *rdev, int min_uV,
	       int max_uV, unsigned *selector)
{
	int id = rdev_get_id(rdev);

	dev_dbg(rdev_get_dev(rdev), "%s id: %d min_uV: %d max_uV: %d\n",
		__func__, id, min_uV, max_uV);

	if (min_uV <= rdev->desc->volt_table[0] &&
	    rdev->desc->volt_table[0] <= max_uV) {
		*selector = 0;
		return 0;
	} else {
		return -EINVAL;
	}
}
EXPORT_SYMBOL_GPL(mc13xxx_fixed_regulator_set_voltage);

struct regulator_ops mc13xxx_fixed_regulator_ops = {
	.enable = mc13xxx_regulator_enable,
	.disable = mc13xxx_regulator_disable,
	.is_enabled = mc13xxx_regulator_is_enabled,
	.list_voltage = regulator_list_voltage_table,
	.set_voltage = mc13xxx_fixed_regulator_set_voltage,
};
EXPORT_SYMBOL_GPL(mc13xxx_fixed_regulator_ops);

#ifdef CONFIG_OF
int mc13xxx_get_num_regulators_dt(struct platform_device *pdev)
{
	struct device_node *parent;
	int num;

	if (!pdev->dev.parent->of_node)
		return -ENODEV;

	parent = of_get_child_by_name(pdev->dev.parent->of_node, "regulators");
	if (!parent)
		return -ENODEV;

	num = of_get_child_count(parent);
	of_node_put(parent);
	return num;
}
EXPORT_SYMBOL_GPL(mc13xxx_get_num_regulators_dt);

struct mc13xxx_regulator_init_data *mc13xxx_parse_regulators_dt(
	struct platform_device *pdev, struct mc13xxx_regulator *regulators,
	int num_regulators)
{
	struct mc13xxx_regulator_priv *priv = platform_get_drvdata(pdev);
	struct mc13xxx_regulator_init_data *data, *p;
	struct device_node *parent, *child;
	int i, parsed = 0;

	if (!pdev->dev.parent->of_node)
		return NULL;

	parent = of_get_child_by_name(pdev->dev.parent->of_node, "regulators");
	if (!parent)
		return NULL;

	data = devm_kcalloc(&pdev->dev, priv->num_regulators, sizeof(*data),
			    GFP_KERNEL);
	if (!data) {
		of_node_put(parent);
		return NULL;
	}

	p = data;

	for_each_child_of_node(parent, child) {
		int found = 0;

		for (i = 0; i < num_regulators; i++) {
			if (!regulators[i].desc.name)
				continue;
			if (!of_node_cmp(child->name,
					 regulators[i].desc.name)) {
				p->id = i;
				p->init_data = of_get_regulator_init_data(
							&pdev->dev, child,
							&regulators[i].desc);
				p->node = child;
				p++;

				parsed++;
				found = 1;
				break;
			}
		}

		if (!found)
			dev_warn(&pdev->dev,
				 "Unknown regulator: %s\n", child->name);
	}
	of_node_put(parent);

	priv->num_regulators = parsed;

	return data;
}
EXPORT_SYMBOL_GPL(mc13xxx_parse_regulators_dt);
#endif

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Yong Shen <yong.shen@linaro.org>");
MODULE_DESCRIPTION("Regulator Driver for Freescale MC13xxx PMIC");
MODULE_ALIAS("mc13xxx-regulator-core");
