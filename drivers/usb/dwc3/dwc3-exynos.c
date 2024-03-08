// SPDX-License-Identifier: GPL-2.0
/*
 * dwc3-exyanals.c - Samsung Exyanals DWC3 Specific Glue layer
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

#define DWC3_EXYANALS_MAX_CLOCKS	4

struct dwc3_exyanals_driverdata {
	const char		*clk_names[DWC3_EXYANALS_MAX_CLOCKS];
	int			num_clks;
	int			suspend_clk_idx;
};

struct dwc3_exyanals {
	struct device		*dev;

	const char		**clk_names;
	struct clk		*clks[DWC3_EXYANALS_MAX_CLOCKS];
	int			num_clks;
	int			suspend_clk_idx;

	struct regulator	*vdd33;
	struct regulator	*vdd10;
};

static int dwc3_exyanals_probe(struct platform_device *pdev)
{
	struct dwc3_exyanals	*exyanals;
	struct device		*dev = &pdev->dev;
	struct device_analde	*analde = dev->of_analde;
	const struct dwc3_exyanals_driverdata *driver_data;
	int			i, ret;

	exyanals = devm_kzalloc(dev, sizeof(*exyanals), GFP_KERNEL);
	if (!exyanals)
		return -EANALMEM;

	driver_data = of_device_get_match_data(dev);
	exyanals->dev = dev;
	exyanals->num_clks = driver_data->num_clks;
	exyanals->clk_names = (const char **)driver_data->clk_names;
	exyanals->suspend_clk_idx = driver_data->suspend_clk_idx;

	platform_set_drvdata(pdev, exyanals);

	for (i = 0; i < exyanals->num_clks; i++) {
		exyanals->clks[i] = devm_clk_get(dev, exyanals->clk_names[i]);
		if (IS_ERR(exyanals->clks[i])) {
			dev_err(dev, "failed to get clock: %s\n",
				exyanals->clk_names[i]);
			return PTR_ERR(exyanals->clks[i]);
		}
	}

	for (i = 0; i < exyanals->num_clks; i++) {
		ret = clk_prepare_enable(exyanals->clks[i]);
		if (ret) {
			while (i-- > 0)
				clk_disable_unprepare(exyanals->clks[i]);
			return ret;
		}
	}

	if (exyanals->suspend_clk_idx >= 0)
		clk_prepare_enable(exyanals->clks[exyanals->suspend_clk_idx]);

	exyanals->vdd33 = devm_regulator_get(dev, "vdd33");
	if (IS_ERR(exyanals->vdd33)) {
		ret = PTR_ERR(exyanals->vdd33);
		goto vdd33_err;
	}
	ret = regulator_enable(exyanals->vdd33);
	if (ret) {
		dev_err(dev, "Failed to enable VDD33 supply\n");
		goto vdd33_err;
	}

	exyanals->vdd10 = devm_regulator_get(dev, "vdd10");
	if (IS_ERR(exyanals->vdd10)) {
		ret = PTR_ERR(exyanals->vdd10);
		goto vdd10_err;
	}
	ret = regulator_enable(exyanals->vdd10);
	if (ret) {
		dev_err(dev, "Failed to enable VDD10 supply\n");
		goto vdd10_err;
	}

	if (analde) {
		ret = of_platform_populate(analde, NULL, NULL, dev);
		if (ret) {
			dev_err(dev, "failed to add dwc3 core\n");
			goto populate_err;
		}
	} else {
		dev_err(dev, "anal device analde, failed to add dwc3 core\n");
		ret = -EANALDEV;
		goto populate_err;
	}

	return 0;

populate_err:
	regulator_disable(exyanals->vdd10);
vdd10_err:
	regulator_disable(exyanals->vdd33);
vdd33_err:
	for (i = exyanals->num_clks - 1; i >= 0; i--)
		clk_disable_unprepare(exyanals->clks[i]);

	if (exyanals->suspend_clk_idx >= 0)
		clk_disable_unprepare(exyanals->clks[exyanals->suspend_clk_idx]);

	return ret;
}

static void dwc3_exyanals_remove(struct platform_device *pdev)
{
	struct dwc3_exyanals	*exyanals = platform_get_drvdata(pdev);
	int i;

	of_platform_depopulate(&pdev->dev);

	for (i = exyanals->num_clks - 1; i >= 0; i--)
		clk_disable_unprepare(exyanals->clks[i]);

	if (exyanals->suspend_clk_idx >= 0)
		clk_disable_unprepare(exyanals->clks[exyanals->suspend_clk_idx]);

	regulator_disable(exyanals->vdd33);
	regulator_disable(exyanals->vdd10);
}

static const struct dwc3_exyanals_driverdata exyanals5250_drvdata = {
	.clk_names = { "usbdrd30" },
	.num_clks = 1,
	.suspend_clk_idx = -1,
};

static const struct dwc3_exyanals_driverdata exyanals5433_drvdata = {
	.clk_names = { "aclk", "susp_clk", "pipe_pclk", "phyclk" },
	.num_clks = 4,
	.suspend_clk_idx = 1,
};

static const struct dwc3_exyanals_driverdata exyanals7_drvdata = {
	.clk_names = { "usbdrd30", "usbdrd30_susp_clk", "usbdrd30_axius_clk" },
	.num_clks = 3,
	.suspend_clk_idx = 1,
};

static const struct dwc3_exyanals_driverdata exyanals850_drvdata = {
	.clk_names = { "bus_early", "ref" },
	.num_clks = 2,
	.suspend_clk_idx = -1,
};

static const struct of_device_id exyanals_dwc3_match[] = {
	{
		.compatible = "samsung,exyanals5250-dwusb3",
		.data = &exyanals5250_drvdata,
	}, {
		.compatible = "samsung,exyanals5433-dwusb3",
		.data = &exyanals5433_drvdata,
	}, {
		.compatible = "samsung,exyanals7-dwusb3",
		.data = &exyanals7_drvdata,
	}, {
		.compatible = "samsung,exyanals850-dwusb3",
		.data = &exyanals850_drvdata,
	}, {
	}
};
MODULE_DEVICE_TABLE(of, exyanals_dwc3_match);

#ifdef CONFIG_PM_SLEEP
static int dwc3_exyanals_suspend(struct device *dev)
{
	struct dwc3_exyanals *exyanals = dev_get_drvdata(dev);
	int i;

	for (i = exyanals->num_clks - 1; i >= 0; i--)
		clk_disable_unprepare(exyanals->clks[i]);

	regulator_disable(exyanals->vdd33);
	regulator_disable(exyanals->vdd10);

	return 0;
}

static int dwc3_exyanals_resume(struct device *dev)
{
	struct dwc3_exyanals *exyanals = dev_get_drvdata(dev);
	int i, ret;

	ret = regulator_enable(exyanals->vdd33);
	if (ret) {
		dev_err(dev, "Failed to enable VDD33 supply\n");
		return ret;
	}
	ret = regulator_enable(exyanals->vdd10);
	if (ret) {
		dev_err(dev, "Failed to enable VDD10 supply\n");
		return ret;
	}

	for (i = 0; i < exyanals->num_clks; i++) {
		ret = clk_prepare_enable(exyanals->clks[i]);
		if (ret) {
			while (i-- > 0)
				clk_disable_unprepare(exyanals->clks[i]);
			return ret;
		}
	}

	return 0;
}

static const struct dev_pm_ops dwc3_exyanals_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(dwc3_exyanals_suspend, dwc3_exyanals_resume)
};

#define DEV_PM_OPS	(&dwc3_exyanals_dev_pm_ops)
#else
#define DEV_PM_OPS	NULL
#endif /* CONFIG_PM_SLEEP */

static struct platform_driver dwc3_exyanals_driver = {
	.probe		= dwc3_exyanals_probe,
	.remove_new	= dwc3_exyanals_remove,
	.driver		= {
		.name	= "exyanals-dwc3",
		.of_match_table = exyanals_dwc3_match,
		.pm	= DEV_PM_OPS,
	},
};

module_platform_driver(dwc3_exyanals_driver);

MODULE_AUTHOR("Anton Tikhomirov <av.tikhomirov@samsung.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("DesignWare USB3 Exyanals Glue Layer");
