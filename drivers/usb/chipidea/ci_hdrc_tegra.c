// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2016, NVIDIA Corporation
 */

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/reset.h>

#include <linux/usb/chipidea.h>

#include "ci.h"

struct tegra_udc {
	struct ci_hdrc_platform_data data;
	struct platform_device *dev;

	struct usb_phy *phy;
	struct clk *clk;
};

struct tegra_udc_soc_info {
	unsigned long flags;
};

static const struct tegra_udc_soc_info tegra20_udc_soc_info = {
	.flags = CI_HDRC_REQUIRES_ALIGNED_DMA,
};

static const struct tegra_udc_soc_info tegra30_udc_soc_info = {
	.flags = CI_HDRC_REQUIRES_ALIGNED_DMA,
};

static const struct tegra_udc_soc_info tegra114_udc_soc_info = {
	.flags = 0,
};

static const struct tegra_udc_soc_info tegra124_udc_soc_info = {
	.flags = 0,
};

static const struct of_device_id tegra_udc_of_match[] = {
	{
		.compatible = "nvidia,tegra20-udc",
		.data = &tegra20_udc_soc_info,
	}, {
		.compatible = "nvidia,tegra30-udc",
		.data = &tegra30_udc_soc_info,
	}, {
		.compatible = "nvidia,tegra114-udc",
		.data = &tegra114_udc_soc_info,
	}, {
		.compatible = "nvidia,tegra124-udc",
		.data = &tegra124_udc_soc_info,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, tegra_udc_of_match);

static int tegra_udc_probe(struct platform_device *pdev)
{
	const struct tegra_udc_soc_info *soc;
	struct tegra_udc *udc;
	int err;

	udc = devm_kzalloc(&pdev->dev, sizeof(*udc), GFP_KERNEL);
	if (!udc)
		return -ENOMEM;

	soc = of_device_get_match_data(&pdev->dev);
	if (!soc) {
		dev_err(&pdev->dev, "failed to match OF data\n");
		return -EINVAL;
	}

	udc->phy = devm_usb_get_phy_by_phandle(&pdev->dev, "nvidia,phy", 0);
	if (IS_ERR(udc->phy)) {
		err = PTR_ERR(udc->phy);
		dev_err(&pdev->dev, "failed to get PHY: %d\n", err);
		return err;
	}

	udc->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(udc->clk)) {
		err = PTR_ERR(udc->clk);
		dev_err(&pdev->dev, "failed to get clock: %d\n", err);
		return err;
	}

	err = clk_prepare_enable(udc->clk);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to enable clock: %d\n", err);
		return err;
	}

	/*
	 * Tegra's USB PHY driver doesn't implement optional phy_init()
	 * hook, so we have to power on UDC controller before ChipIdea
	 * driver initialization kicks in.
	 */
	usb_phy_set_suspend(udc->phy, 0);

	/* setup and register ChipIdea HDRC device */
	udc->data.name = "tegra-udc";
	udc->data.flags = soc->flags;
	udc->data.usb_phy = udc->phy;
	udc->data.capoffset = DEF_CAPOFFSET;

	udc->dev = ci_hdrc_add_device(&pdev->dev, pdev->resource,
				      pdev->num_resources, &udc->data);
	if (IS_ERR(udc->dev)) {
		err = PTR_ERR(udc->dev);
		dev_err(&pdev->dev, "failed to add HDRC device: %d\n", err);
		goto fail_power_off;
	}

	platform_set_drvdata(pdev, udc);

	return 0;

fail_power_off:
	usb_phy_set_suspend(udc->phy, 1);
	clk_disable_unprepare(udc->clk);
	return err;
}

static int tegra_udc_remove(struct platform_device *pdev)
{
	struct tegra_udc *udc = platform_get_drvdata(pdev);

	usb_phy_set_suspend(udc->phy, 1);
	clk_disable_unprepare(udc->clk);

	return 0;
}

static struct platform_driver tegra_udc_driver = {
	.driver = {
		.name = "tegra-udc",
		.of_match_table = tegra_udc_of_match,
	},
	.probe = tegra_udc_probe,
	.remove = tegra_udc_remove,
};
module_platform_driver(tegra_udc_driver);

MODULE_DESCRIPTION("NVIDIA Tegra USB device mode driver");
MODULE_AUTHOR("Thierry Reding <treding@nvidia.com>");
MODULE_ALIAS("platform:tegra-udc");
MODULE_LICENSE("GPL v2");
