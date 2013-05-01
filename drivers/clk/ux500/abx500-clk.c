/*
 * abx500 clock implementation for ux500 platform.
 *
 * Copyright (C) 2012 ST-Ericsson SA
 * Author: Ulf Hansson <ulf.hansson@linaro.org>
 *
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/err.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/mfd/abx500/ab8500.h>

/* TODO: Add clock implementations here */


/* Clock definitions for ab8500 */
static int ab8500_reg_clks(struct device *dev)
{
	return 0;
}

/* Clock definitions for ab8540 */
static int ab8540_reg_clks(struct device *dev)
{
	return 0;
}

/* Clock definitions for ab9540 */
static int ab9540_reg_clks(struct device *dev)
{
	return 0;
}

static int abx500_clk_probe(struct platform_device *pdev)
{
	struct ab8500 *parent = dev_get_drvdata(pdev->dev.parent);
	int ret;

	if (is_ab8500(parent) || is_ab8505(parent)) {
		ret = ab8500_reg_clks(&pdev->dev);
	} else if (is_ab8540(parent)) {
		ret = ab8540_reg_clks(&pdev->dev);
	} else if (is_ab9540(parent)) {
		ret = ab9540_reg_clks(&pdev->dev);
	} else {
		dev_err(&pdev->dev, "non supported plf id\n");
		return -ENODEV;
	}

	return ret;
}

static struct platform_driver abx500_clk_driver = {
	.driver = {
		.name = "abx500-clk",
		.owner = THIS_MODULE,
	},
	.probe	= abx500_clk_probe,
};

static int __init abx500_clk_init(void)
{
	return platform_driver_register(&abx500_clk_driver);
}

arch_initcall(abx500_clk_init);

MODULE_AUTHOR("Ulf Hansson <ulf.hansson@linaro.org");
MODULE_DESCRIPTION("ABX500 clk driver");
MODULE_LICENSE("GPL v2");
