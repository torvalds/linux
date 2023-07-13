// SPDX-License-Identifier: GPL-2.0
/**
 * cdns3-starfive.c - StarFive specific Glue layer for Cadence USB Controller
 *
 * Copyright (C) 2023 StarFive Technology Co., Ltd.
 *
 * Author:	Minda Chen <minda.chen@starfivetech.com>
 */

#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/mfd/syscon.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/of_platform.h>
#include <linux/reset.h>
#include <linux/regmap.h>
#include <linux/usb/otg.h>
#include "core.h"

#define USB_STRAP_HOST			BIT(17)
#define USB_STRAP_DEVICE		BIT(18)
#define USB_STRAP_MASK			GENMASK(18, 16)

#define USB_SUSPENDM_HOST		BIT(19)
#define USB_SUSPENDM_MASK		BIT(19)

#define USB_MISC_CFG_MASK		GENMASK(23, 20)
#define USB_SUSPENDM_BYPS		BIT(20)
#define USB_PLL_EN			BIT(22)
#define USB_REFCLK_MODE			BIT(23)

struct cdns_starfive {
	struct device *dev;
	struct regmap *stg_syscon;
	struct reset_control *resets;
	struct clk_bulk_data *clks;
	int num_clks;
	u32 stg_usb_mode;
};

static void cdns_mode_init(struct platform_device *pdev,
			   struct cdns_starfive *data)
{
	enum usb_dr_mode mode;

	regmap_update_bits(data->stg_syscon, data->stg_usb_mode,
			   USB_MISC_CFG_MASK,
			   USB_SUSPENDM_BYPS | USB_PLL_EN | USB_REFCLK_MODE);

	/* dr mode setting */
	mode = usb_get_dr_mode(&pdev->dev);

	switch (mode) {
	case USB_DR_MODE_HOST:
		regmap_update_bits(data->stg_syscon,
				   data->stg_usb_mode,
				   USB_STRAP_MASK,
				   USB_STRAP_HOST);
		regmap_update_bits(data->stg_syscon,
				   data->stg_usb_mode,
				   USB_SUSPENDM_MASK,
				   USB_SUSPENDM_HOST);
		break;

	case USB_DR_MODE_PERIPHERAL:
		regmap_update_bits(data->stg_syscon, data->stg_usb_mode,
				   USB_STRAP_MASK, USB_STRAP_DEVICE);
		regmap_update_bits(data->stg_syscon, data->stg_usb_mode,
				   USB_SUSPENDM_MASK, 0);
		break;
	default:
		break;
	}
}

static int cdns_clk_rst_init(struct cdns_starfive *data)
{
	int ret;

	ret = clk_bulk_prepare_enable(data->num_clks, data->clks);
	if (ret)
		return dev_err_probe(data->dev, ret,
				     "failed to enable clocks\n");

	ret = reset_control_deassert(data->resets);
	if (ret) {
		dev_err(data->dev, "failed to reset clocks\n");
		goto err_clk_init;
	}

	return ret;

err_clk_init:
	clk_bulk_disable_unprepare(data->num_clks, data->clks);
	return ret;
}

static void cdns_clk_rst_deinit(struct cdns_starfive *data)
{
	reset_control_assert(data->resets);
	clk_bulk_disable_unprepare(data->num_clks, data->clks);
}

static int cdns_starfive_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cdns_starfive *data;
	unsigned int args;
	int ret;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->dev = dev;

	data->stg_syscon =
		syscon_regmap_lookup_by_phandle_args(pdev->dev.of_node,
						     "starfive,stg-syscon", 1, &args);

	if (IS_ERR(data->stg_syscon))
		return dev_err_probe(dev, PTR_ERR(data->stg_syscon),
				     "Failed to parse starfive,stg-syscon\n");

	data->stg_usb_mode = args;

	data->num_clks = devm_clk_bulk_get_all(data->dev, &data->clks);
	if (data->num_clks < 0)
		return dev_err_probe(data->dev, -ENODEV,
				     "Failed to get clocks\n");

	data->resets = devm_reset_control_array_get_exclusive(data->dev);
	if (IS_ERR(data->resets))
		return dev_err_probe(data->dev, PTR_ERR(data->resets),
				     "Failed to get resets");

	cdns_mode_init(pdev, data);
	ret = cdns_clk_rst_init(data);
	if (ret)
		return ret;

	ret = of_platform_populate(dev->of_node, NULL, NULL, dev);
	if (ret) {
		dev_err(dev, "Failed to create children\n");
		cdns_clk_rst_deinit(data);
		return ret;
	}

	device_set_wakeup_capable(dev, true);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	platform_set_drvdata(pdev, data);

	return 0;
}

static int cdns_starfive_remove_core(struct device *dev, void *c)
{
	struct platform_device *pdev = to_platform_device(dev);

	platform_device_unregister(pdev);

	return 0;
}

static int cdns_starfive_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cdns_starfive *data = dev_get_drvdata(dev);

	pm_runtime_get_sync(dev);
	device_for_each_child(dev, NULL, cdns_starfive_remove_core);

	pm_runtime_disable(dev);
	pm_runtime_put_noidle(dev);
	cdns_clk_rst_deinit(data);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

#ifdef CONFIG_PM
static int cdns_starfive_runtime_resume(struct device *dev)
{
	struct cdns_starfive *data = dev_get_drvdata(dev);

	return clk_bulk_prepare_enable(data->num_clks, data->clks);
}

static int cdns_starfive_runtime_suspend(struct device *dev)
{
	struct cdns_starfive *data = dev_get_drvdata(dev);

	clk_bulk_disable_unprepare(data->num_clks, data->clks);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int cdns_starfive_resume(struct device *dev)
{
	struct cdns_starfive *data = dev_get_drvdata(dev);

	return cdns_clk_rst_init(data);
}

static int cdns_starfive_suspend(struct device *dev)
{
	struct cdns_starfive *data = dev_get_drvdata(dev);

	cdns_clk_rst_deinit(data);

	return 0;
}
#endif
#endif

static const struct dev_pm_ops cdns_starfive_pm_ops = {
	SET_RUNTIME_PM_OPS(cdns_starfive_runtime_suspend,
			   cdns_starfive_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(cdns_starfive_suspend, cdns_starfive_resume)
};

static const struct of_device_id cdns_starfive_of_match[] = {
	{ .compatible = "starfive,jh7110-usb", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, cdns_starfive_of_match);

static struct platform_driver cdns_starfive_driver = {
	.probe		= cdns_starfive_probe,
	.remove		= cdns_starfive_remove,
	.driver		= {
		.name	= "cdns3-starfive",
		.of_match_table	= cdns_starfive_of_match,
		.pm	= &cdns_starfive_pm_ops,
	},
};
module_platform_driver(cdns_starfive_driver);

MODULE_ALIAS("platform:cdns3-starfive");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Cadence USB3 StarFive Glue Layer");
