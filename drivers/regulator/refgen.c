// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017, 2019-2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/regulator/debug-regulator.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/proxy-consumer.h>

#define REFGEN_REG_BIAS_EN		0x08
#define REFGEN_BIAS_EN_MASK		GENMASK(2, 0)
#define REFGEN_BIAS_EN_ENABLE		0x7
#define REFGEN_BIAS_EN_DISABLE		0x6

#define REFGEN_REG_BG_CTRL		0x14
#define REFGEN_BG_CTRL_MASK		GENMASK(2, 1)
#define REFGEN_BG_CTRL_ENABLE		0x6
#define REFGEN_BG_CTRL_DISABLE		0x4

#define REFGEN_REG_PWRDWN_CTRL5		0x80
#define REFGEN_PWRDWN_CTRL5_MASK	GENMASK(0, 0)
#define REFGEN_PWRDWN_CTRL5_ENABLE	0x1
#define REFGEN_PWRDWN_CTRL5_DISABLE	0x0

struct refgen {
	struct regulator_desc	rdesc;
	struct regulator_dev	*rdev;
	void __iomem		*addr;
};

static void masked_writel(u32 val, u32 mask, void __iomem *addr)
{
	u32 reg;

	reg = readl_relaxed(addr);
	reg = (reg & ~mask) | (val & mask);
	writel_relaxed(reg, addr);
}

static int refgen_enable(struct regulator_dev *rdev)
{
	struct refgen *vreg = rdev_get_drvdata(rdev);

	masked_writel(REFGEN_BG_CTRL_ENABLE, REFGEN_BG_CTRL_MASK,
			vreg->addr + REFGEN_REG_BG_CTRL);
	writel_relaxed(REFGEN_BIAS_EN_ENABLE, vreg->addr + REFGEN_REG_BIAS_EN);

	return 0;
}

static int refgen_disable(struct regulator_dev *rdev)
{
	struct refgen *vreg = rdev_get_drvdata(rdev);

	writel_relaxed(REFGEN_BIAS_EN_DISABLE, vreg->addr + REFGEN_REG_BIAS_EN);
	masked_writel(REFGEN_BG_CTRL_DISABLE, REFGEN_BG_CTRL_MASK,
			vreg->addr + REFGEN_REG_BG_CTRL);

	return 0;
}

static int refgen_is_enabled(struct regulator_dev *rdev)
{
	struct refgen *vreg = rdev_get_drvdata(rdev);
	u32 val;

	val = readl_relaxed(vreg->addr + REFGEN_REG_BG_CTRL);
	if ((val & REFGEN_BG_CTRL_MASK) != REFGEN_BG_CTRL_ENABLE)
		return 0;

	val = readl_relaxed(vreg->addr + REFGEN_REG_BIAS_EN);
	if ((val & REFGEN_BIAS_EN_MASK) != REFGEN_BIAS_EN_ENABLE)
		return 0;

	return 1;
}

static const struct regulator_ops refgen_ops = {
	.enable		= refgen_enable,
	.disable	= refgen_disable,
	.is_enabled	= refgen_is_enabled,
};

static int refgen_kona_enable(struct regulator_dev *rdev)
{
	struct refgen *vreg = rdev_get_drvdata(rdev);

	masked_writel(REFGEN_PWRDWN_CTRL5_ENABLE, REFGEN_PWRDWN_CTRL5_MASK,
			vreg->addr + REFGEN_REG_PWRDWN_CTRL5);

	return 0;
}

static int refgen_kona_disable(struct regulator_dev *rdev)
{
	struct refgen *vreg = rdev_get_drvdata(rdev);

	masked_writel(REFGEN_PWRDWN_CTRL5_DISABLE, REFGEN_PWRDWN_CTRL5_MASK,
			vreg->addr + REFGEN_REG_PWRDWN_CTRL5);

	return 0;
}

static int refgen_kona_is_enabled(struct regulator_dev *rdev)
{
	struct refgen *vreg = rdev_get_drvdata(rdev);
	u32 val;

	val = readl_relaxed(vreg->addr + REFGEN_REG_PWRDWN_CTRL5);

	return (val & REFGEN_PWRDWN_CTRL5_MASK) == REFGEN_PWRDWN_CTRL5_ENABLE;
}

static const struct regulator_ops refgen_kona_ops = {
	.enable		= refgen_kona_enable,
	.disable	= refgen_kona_disable,
	.is_enabled	= refgen_kona_is_enabled,
};

static const struct of_device_id refgen_match_table[] = {
	{ .compatible = "qcom,refgen-regulator", .data = &refgen_ops},
	{ .compatible = "qcom,refgen-sdm845-regulator", .data = &refgen_ops},
	{ .compatible = "qcom,refgen-kona-regulator", .data = &refgen_kona_ops},
	{}
};

static int refgen_probe(struct platform_device *pdev)
{
	struct regulator_config config = {};
	struct regulator_init_data *init_data = NULL;
	struct device *dev = &pdev->dev;
	struct regulator_desc *rdesc;
	struct resource *res;
	struct refgen *vreg;
	int rc;

	vreg = devm_kzalloc(dev, sizeof(*vreg), GFP_KERNEL);
	if (!vreg)
		return -ENOMEM;

	if (!dev->of_node) {
		dev_err(dev, "%s: device tree node missing\n", __func__);
		return -ENODEV;
	}

	vreg->rdesc.ops = of_device_get_match_data(dev);
	if (!vreg->rdesc.ops) {
		dev_err(dev, "%s: could not find compatible string match\n",
			__func__);
		return -ENODEV;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res || !res->start) {
		dev_err(dev, "reg address is missing\n");
		return -EINVAL;
	}

	vreg->addr = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(vreg->addr)) {
		rc = PTR_ERR(vreg->addr);
		dev_err(dev, "ioremap failed, rc=%d\n", rc);
		return rc;
	}

	init_data = of_get_regulator_init_data(dev, dev->of_node, &vreg->rdesc);
	if (!init_data)
		return -ENOMEM;

	if (init_data->constraints.name == NULL) {
		dev_err(dev, "%s: regulator-name not specified\n", __func__);
		return -EINVAL;
	}

	if (of_get_property(dev->of_node, "parent-supply", NULL))
		init_data->supply_regulator = "parent";

	rdesc = &vreg->rdesc;

	rdesc->name = "refgen";
	rdesc->id = pdev->id;
	rdesc->owner = THIS_MODULE;
	rdesc->type = REGULATOR_VOLTAGE;

	config.dev = dev;
	config.init_data = init_data;
	config.driver_data = vreg;
	config.of_node = dev->of_node;

	vreg->rdev = devm_regulator_register(dev, rdesc, &config);
	if (IS_ERR(vreg->rdev)) {
		rc = PTR_ERR(vreg->rdev);
		if (rc != -EPROBE_DEFER)
			dev_err(dev, "%s: regulator register failed\n",
				__func__);
		return rc;
	}

	rc = devm_regulator_proxy_consumer_register(dev, dev->of_node);
	if (rc)
		dev_err(dev, "failed to register proxy consumer, rc=%d\n", rc);

	rc = devm_regulator_debug_register(dev, vreg->rdev);
	if (rc)
		dev_err(dev, "failed to register debug regulator, rc=%d\n", rc);

	return 0;
}

static struct platform_driver refgen_driver = {
	.probe = refgen_probe,
	.driver = {
		.name = "qcom,refgen-regulator",
		.of_match_table = refgen_match_table,
		.sync_state = regulator_proxy_consumer_sync_state,
	},
};

static int __init refgen_init(void)
{
	return platform_driver_register(&refgen_driver);
}
arch_initcall(refgen_init);

static void __exit refgen_exit(void)
{
	platform_driver_unregister(&refgen_driver);
}
module_exit(refgen_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("refgen regulator driver");
