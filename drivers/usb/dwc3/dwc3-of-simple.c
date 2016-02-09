/**
 * dwc3-of-simple.c - OF glue layer for simple integrations
 *
 * Copyright (c) 2015 Texas Instruments Incorporated - http://www.ti.com
 *
 * Author: Felipe Balbi <balbi@ti.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2  of
 * the License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * This is a combination of the old dwc3-qcom.c by Ivan T. Ivanov
 * <iivanov@mm-sol.com> and the original patch adding support for Xilinx' SoC
 * by Subbaraya Sundeep Bhatta <subbaraya.sundeep.bhatta@xilinx.com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/pm_runtime.h>

struct dwc3_of_simple {
	struct device		*dev;
	struct clk		**clks;
	int			num_clocks;
};

static int dwc3_of_simple_probe(struct platform_device *pdev)
{
	struct dwc3_of_simple	*simple;
	struct device		*dev = &pdev->dev;
	struct device_node	*np = dev->of_node;

	int			ret;
	int			i;

	simple = devm_kzalloc(dev, sizeof(*simple), GFP_KERNEL);
	if (!simple)
		return -ENOMEM;

	ret = of_clk_get_parent_count(np);
	if (ret < 0)
		return ret;

	simple->num_clocks = ret;

	simple->clks = devm_kcalloc(dev, simple->num_clocks,
			sizeof(struct clk *), GFP_KERNEL);
	if (!simple->clks)
		return -ENOMEM;

	simple->dev = dev;

	for (i = 0; i < simple->num_clocks; i++) {
		struct clk	*clk;

		clk = of_clk_get(np, i);
		if (IS_ERR(clk)) {
			while (--i >= 0)
				clk_put(simple->clks[i]);
			return PTR_ERR(clk);
		}

		ret = clk_prepare_enable(clk);
		if (ret < 0) {
			while (--i >= 0) {
				clk_disable_unprepare(simple->clks[i]);
				clk_put(simple->clks[i]);
			}
			clk_put(clk);

			return ret;
		}

		simple->clks[i] = clk;
	}

	ret = of_platform_populate(np, NULL, NULL, dev);
	if (ret) {
		for (i = 0; i < simple->num_clocks; i++) {
			clk_disable_unprepare(simple->clks[i]);
			clk_put(simple->clks[i]);
		}

		return ret;
	}

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_get_sync(dev);

	return 0;
}

static int dwc3_of_simple_remove(struct platform_device *pdev)
{
	struct dwc3_of_simple	*simple = platform_get_drvdata(pdev);
	struct device		*dev = &pdev->dev;
	int			i;

	for (i = 0; i < simple->num_clocks; i++) {
		clk_unprepare(simple->clks[i]);
		clk_put(simple->clks[i]);
	}

	of_platform_depopulate(dev);

	pm_runtime_put_sync(dev);
	pm_runtime_disable(dev);

	return 0;
}

#ifdef CONFIG_PM
static int dwc3_of_simple_runtime_suspend(struct device *dev)
{
	struct dwc3_of_simple	*simple = dev_get_drvdata(dev);
	int			i;

	for (i = 0; i < simple->num_clocks; i++)
		clk_disable(simple->clks[i]);

	return 0;
}

static int dwc3_of_simple_runtime_resume(struct device *dev)
{
	struct dwc3_of_simple	*simple = dev_get_drvdata(dev);
	int			ret;
	int			i;

	for (i = 0; i < simple->num_clocks; i++) {
		ret = clk_enable(simple->clks[i]);
		if (ret < 0) {
			while (--i >= 0)
				clk_disable(simple->clks[i]);
			return ret;
		}
	}

	return 0;
}
#endif

static const struct dev_pm_ops dwc3_of_simple_dev_pm_ops = {
	SET_RUNTIME_PM_OPS(dwc3_of_simple_runtime_suspend,
			dwc3_of_simple_runtime_resume, NULL)
};

static const struct of_device_id of_dwc3_simple_match[] = {
	{ .compatible = "qcom,dwc3" },
	{ .compatible = "xlnx,zynqmp-dwc3" },
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_dwc3_simple_match);

static struct platform_driver dwc3_of_simple_driver = {
	.probe		= dwc3_of_simple_probe,
	.remove		= dwc3_of_simple_remove,
	.driver		= {
		.name	= "dwc3-of-simple",
		.of_match_table = of_dwc3_simple_match,
	},
};

module_platform_driver(dwc3_of_simple_driver);
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("DesignWare USB3 OF Simple Glue Layer");
MODULE_AUTHOR("Felipe Balbi <balbi@ti.com>");
