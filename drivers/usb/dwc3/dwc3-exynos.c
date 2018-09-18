// SPDX-License-Identifier: GPL-2.0
/**
 * dwc3-exynos.c - Samsung EXYNOS DWC3 Specific Glue layer
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Anton Tikhomirov <av.tikhomirov@samsung.com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/regulator/consumer.h>

struct dwc3_exynos {
	struct device		*dev;

	struct clk		*clk;
	struct clk		*susp_clk;
	struct clk		*axius_clk;

	struct regulator	*vdd33;
	struct regulator	*vdd10;
};

static int dwc3_exynos_remove_child(struct device *dev, void *unused)
{
	struct platform_device *pdev = to_platform_device(dev);

	platform_device_unregister(pdev);

	return 0;
}

static int dwc3_exynos_probe(struct platform_device *pdev)
{
	struct dwc3_exynos	*exynos;
	struct device		*dev = &pdev->dev;
	struct device_node	*node = dev->of_node;

	int			ret;

	exynos = devm_kzalloc(dev, sizeof(*exynos), GFP_KERNEL);
	if (!exynos)
		return -ENOMEM;

	platform_set_drvdata(pdev, exynos);

	exynos->dev	= dev;

	exynos->clk = devm_clk_get(dev, "usbdrd30");
	if (IS_ERR(exynos->clk)) {
		dev_err(dev, "couldn't get clock\n");
		return -EINVAL;
	}
	ret = clk_prepare_enable(exynos->clk);
	if (ret)
		return ret;

	exynos->susp_clk = devm_clk_get(dev, "usbdrd30_susp_clk");
	if (IS_ERR(exynos->susp_clk))
		exynos->susp_clk = NULL;
	ret = clk_prepare_enable(exynos->susp_clk);
	if (ret)
		goto susp_clk_err;

	if (of_device_is_compatible(node, "samsung,exynos7-dwusb3")) {
		exynos->axius_clk = devm_clk_get(dev, "usbdrd30_axius_clk");
		if (IS_ERR(exynos->axius_clk)) {
			dev_err(dev, "no AXI UpScaler clk specified\n");
			ret = -ENODEV;
			goto axius_clk_err;
		}
		ret = clk_prepare_enable(exynos->axius_clk);
		if (ret)
			goto axius_clk_err;
	} else {
		exynos->axius_clk = NULL;
	}

	exynos->vdd33 = devm_regulator_get(dev, "vdd33");
	if (IS_ERR(exynos->vdd33)) {
		ret = PTR_ERR(exynos->vdd33);
		goto vdd33_err;
	}
	ret = regulator_enable(exynos->vdd33);
	if (ret) {
		dev_err(dev, "Failed to enable VDD33 supply\n");
		goto vdd33_err;
	}

	exynos->vdd10 = devm_regulator_get(dev, "vdd10");
	if (IS_ERR(exynos->vdd10)) {
		ret = PTR_ERR(exynos->vdd10);
		goto vdd10_err;
	}
	ret = regulator_enable(exynos->vdd10);
	if (ret) {
		dev_err(dev, "Failed to enable VDD10 supply\n");
		goto vdd10_err;
	}

	if (node) {
		ret = of_platform_populate(node, NULL, NULL, dev);
		if (ret) {
			dev_err(dev, "failed to add dwc3 core\n");
			goto populate_err;
		}
	} else {
		dev_err(dev, "no device node, failed to add dwc3 core\n");
		ret = -ENODEV;
		goto populate_err;
	}

	return 0;

populate_err:
	regulator_disable(exynos->vdd10);
vdd10_err:
	regulator_disable(exynos->vdd33);
vdd33_err:
	clk_disable_unprepare(exynos->axius_clk);
axius_clk_err:
	clk_disable_unprepare(exynos->susp_clk);
susp_clk_err:
	clk_disable_unprepare(exynos->clk);
	return ret;
}

static int dwc3_exynos_remove(struct platform_device *pdev)
{
	struct dwc3_exynos	*exynos = platform_get_drvdata(pdev);

	device_for_each_child(&pdev->dev, NULL, dwc3_exynos_remove_child);

	clk_disable_unprepare(exynos->axius_clk);
	clk_disable_unprepare(exynos->susp_clk);
	clk_disable_unprepare(exynos->clk);

	regulator_disable(exynos->vdd33);
	regulator_disable(exynos->vdd10);

	return 0;
}

static const struct of_device_id exynos_dwc3_match[] = {
	{ .compatible = "samsung,exynos5250-dwusb3" },
	{ .compatible = "samsung,exynos7-dwusb3" },
	{},
};
MODULE_DEVICE_TABLE(of, exynos_dwc3_match);

#ifdef CONFIG_PM_SLEEP
static int dwc3_exynos_suspend(struct device *dev)
{
	struct dwc3_exynos *exynos = dev_get_drvdata(dev);

	clk_disable(exynos->axius_clk);
	clk_disable(exynos->clk);

	regulator_disable(exynos->vdd33);
	regulator_disable(exynos->vdd10);

	return 0;
}

static int dwc3_exynos_resume(struct device *dev)
{
	struct dwc3_exynos *exynos = dev_get_drvdata(dev);
	int ret;

	ret = regulator_enable(exynos->vdd33);
	if (ret) {
		dev_err(dev, "Failed to enable VDD33 supply\n");
		return ret;
	}
	ret = regulator_enable(exynos->vdd10);
	if (ret) {
		dev_err(dev, "Failed to enable VDD10 supply\n");
		return ret;
	}

	clk_enable(exynos->clk);
	clk_enable(exynos->axius_clk);

	return 0;
}

static const struct dev_pm_ops dwc3_exynos_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(dwc3_exynos_suspend, dwc3_exynos_resume)
};

#define DEV_PM_OPS	(&dwc3_exynos_dev_pm_ops)
#else
#define DEV_PM_OPS	NULL
#endif /* CONFIG_PM_SLEEP */

static struct platform_driver dwc3_exynos_driver = {
	.probe		= dwc3_exynos_probe,
	.remove		= dwc3_exynos_remove,
	.driver		= {
		.name	= "exynos-dwc3",
		.of_match_table = exynos_dwc3_match,
		.pm	= DEV_PM_OPS,
	},
};

module_platform_driver(dwc3_exynos_driver);

MODULE_AUTHOR("Anton Tikhomirov <av.tikhomirov@samsung.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("DesignWare USB3 EXYNOS Glue Layer");
