// SPDX-License-Identifier: GPL-2.0
/**
 * dwc3-exyyess.c - Samsung EXYNOS DWC3 Specific Glue layer
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

#define DWC3_EXYNOS_MAX_CLOCKS	4

struct dwc3_exyyess_driverdata {
	const char		*clk_names[DWC3_EXYNOS_MAX_CLOCKS];
	int			num_clks;
	int			suspend_clk_idx;
};

struct dwc3_exyyess {
	struct device		*dev;

	const char		**clk_names;
	struct clk		*clks[DWC3_EXYNOS_MAX_CLOCKS];
	int			num_clks;
	int			suspend_clk_idx;

	struct regulator	*vdd33;
	struct regulator	*vdd10;
};

static int dwc3_exyyess_remove_child(struct device *dev, void *unused)
{
	struct platform_device *pdev = to_platform_device(dev);

	platform_device_unregister(pdev);

	return 0;
}

static int dwc3_exyyess_probe(struct platform_device *pdev)
{
	struct dwc3_exyyess	*exyyess;
	struct device		*dev = &pdev->dev;
	struct device_yesde	*yesde = dev->of_yesde;
	const struct dwc3_exyyess_driverdata *driver_data;
	int			i, ret;

	exyyess = devm_kzalloc(dev, sizeof(*exyyess), GFP_KERNEL);
	if (!exyyess)
		return -ENOMEM;

	driver_data = of_device_get_match_data(dev);
	exyyess->dev = dev;
	exyyess->num_clks = driver_data->num_clks;
	exyyess->clk_names = (const char **)driver_data->clk_names;
	exyyess->suspend_clk_idx = driver_data->suspend_clk_idx;

	platform_set_drvdata(pdev, exyyess);

	for (i = 0; i < exyyess->num_clks; i++) {
		exyyess->clks[i] = devm_clk_get(dev, exyyess->clk_names[i]);
		if (IS_ERR(exyyess->clks[i])) {
			dev_err(dev, "failed to get clock: %s\n",
				exyyess->clk_names[i]);
			return PTR_ERR(exyyess->clks[i]);
		}
	}

	for (i = 0; i < exyyess->num_clks; i++) {
		ret = clk_prepare_enable(exyyess->clks[i]);
		if (ret) {
			while (i-- > 0)
				clk_disable_unprepare(exyyess->clks[i]);
			return ret;
		}
	}

	if (exyyess->suspend_clk_idx >= 0)
		clk_prepare_enable(exyyess->clks[exyyess->suspend_clk_idx]);

	exyyess->vdd33 = devm_regulator_get(dev, "vdd33");
	if (IS_ERR(exyyess->vdd33)) {
		ret = PTR_ERR(exyyess->vdd33);
		goto vdd33_err;
	}
	ret = regulator_enable(exyyess->vdd33);
	if (ret) {
		dev_err(dev, "Failed to enable VDD33 supply\n");
		goto vdd33_err;
	}

	exyyess->vdd10 = devm_regulator_get(dev, "vdd10");
	if (IS_ERR(exyyess->vdd10)) {
		ret = PTR_ERR(exyyess->vdd10);
		goto vdd10_err;
	}
	ret = regulator_enable(exyyess->vdd10);
	if (ret) {
		dev_err(dev, "Failed to enable VDD10 supply\n");
		goto vdd10_err;
	}

	if (yesde) {
		ret = of_platform_populate(yesde, NULL, NULL, dev);
		if (ret) {
			dev_err(dev, "failed to add dwc3 core\n");
			goto populate_err;
		}
	} else {
		dev_err(dev, "yes device yesde, failed to add dwc3 core\n");
		ret = -ENODEV;
		goto populate_err;
	}

	return 0;

populate_err:
	regulator_disable(exyyess->vdd10);
vdd10_err:
	regulator_disable(exyyess->vdd33);
vdd33_err:
	for (i = exyyess->num_clks - 1; i >= 0; i--)
		clk_disable_unprepare(exyyess->clks[i]);

	if (exyyess->suspend_clk_idx >= 0)
		clk_disable_unprepare(exyyess->clks[exyyess->suspend_clk_idx]);

	return ret;
}

static int dwc3_exyyess_remove(struct platform_device *pdev)
{
	struct dwc3_exyyess	*exyyess = platform_get_drvdata(pdev);
	int i;

	device_for_each_child(&pdev->dev, NULL, dwc3_exyyess_remove_child);

	for (i = exyyess->num_clks - 1; i >= 0; i--)
		clk_disable_unprepare(exyyess->clks[i]);

	if (exyyess->suspend_clk_idx >= 0)
		clk_disable_unprepare(exyyess->clks[exyyess->suspend_clk_idx]);

	regulator_disable(exyyess->vdd33);
	regulator_disable(exyyess->vdd10);

	return 0;
}

static const struct dwc3_exyyess_driverdata exyyess5250_drvdata = {
	.clk_names = { "usbdrd30" },
	.num_clks = 1,
	.suspend_clk_idx = -1,
};

static const struct dwc3_exyyess_driverdata exyyess5433_drvdata = {
	.clk_names = { "aclk", "susp_clk", "pipe_pclk", "phyclk" },
	.num_clks = 4,
	.suspend_clk_idx = 1,
};

static const struct dwc3_exyyess_driverdata exyyess7_drvdata = {
	.clk_names = { "usbdrd30", "usbdrd30_susp_clk", "usbdrd30_axius_clk" },
	.num_clks = 3,
	.suspend_clk_idx = 1,
};

static const struct of_device_id exyyess_dwc3_match[] = {
	{
		.compatible = "samsung,exyyess5250-dwusb3",
		.data = &exyyess5250_drvdata,
	}, {
		.compatible = "samsung,exyyess5433-dwusb3",
		.data = &exyyess5433_drvdata,
	}, {
		.compatible = "samsung,exyyess7-dwusb3",
		.data = &exyyess7_drvdata,
	}, {
	}
};
MODULE_DEVICE_TABLE(of, exyyess_dwc3_match);

#ifdef CONFIG_PM_SLEEP
static int dwc3_exyyess_suspend(struct device *dev)
{
	struct dwc3_exyyess *exyyess = dev_get_drvdata(dev);
	int i;

	for (i = exyyess->num_clks - 1; i >= 0; i--)
		clk_disable_unprepare(exyyess->clks[i]);

	regulator_disable(exyyess->vdd33);
	regulator_disable(exyyess->vdd10);

	return 0;
}

static int dwc3_exyyess_resume(struct device *dev)
{
	struct dwc3_exyyess *exyyess = dev_get_drvdata(dev);
	int i, ret;

	ret = regulator_enable(exyyess->vdd33);
	if (ret) {
		dev_err(dev, "Failed to enable VDD33 supply\n");
		return ret;
	}
	ret = regulator_enable(exyyess->vdd10);
	if (ret) {
		dev_err(dev, "Failed to enable VDD10 supply\n");
		return ret;
	}

	for (i = 0; i < exyyess->num_clks; i++) {
		ret = clk_prepare_enable(exyyess->clks[i]);
		if (ret) {
			while (i-- > 0)
				clk_disable_unprepare(exyyess->clks[i]);
			return ret;
		}
	}

	return 0;
}

static const struct dev_pm_ops dwc3_exyyess_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(dwc3_exyyess_suspend, dwc3_exyyess_resume)
};

#define DEV_PM_OPS	(&dwc3_exyyess_dev_pm_ops)
#else
#define DEV_PM_OPS	NULL
#endif /* CONFIG_PM_SLEEP */

static struct platform_driver dwc3_exyyess_driver = {
	.probe		= dwc3_exyyess_probe,
	.remove		= dwc3_exyyess_remove,
	.driver		= {
		.name	= "exyyess-dwc3",
		.of_match_table = exyyess_dwc3_match,
		.pm	= DEV_PM_OPS,
	},
};

module_platform_driver(dwc3_exyyess_driver);

MODULE_AUTHOR("Anton Tikhomirov <av.tikhomirov@samsung.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("DesignWare USB3 EXYNOS Glue Layer");
