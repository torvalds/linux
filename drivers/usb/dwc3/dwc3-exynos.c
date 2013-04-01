/**
 * dwc3-exynos.c - Samsung EXYNOS DWC3 Specific Glue layer
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Anton Tikhomirov <av.tikhomirov@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/platform_data/dwc3-exynos.h>
#include <linux/dma-mapping.h>
#include <linux/clk.h>
#include <linux/usb/otg.h>
#include <linux/usb/nop-usb-xceiv.h>
#include <linux/of.h>

#include "core.h"

struct dwc3_exynos {
	struct platform_device	*dwc3;
	struct platform_device	*usb2_phy;
	struct platform_device	*usb3_phy;
	struct device		*dev;

	struct clk		*clk;
};

static int dwc3_exynos_register_phys(struct dwc3_exynos *exynos)
{
	struct nop_usb_xceiv_platform_data pdata;
	struct platform_device	*pdev;
	int			ret;

	memset(&pdata, 0x00, sizeof(pdata));

	pdev = platform_device_alloc("nop_usb_xceiv", PLATFORM_DEVID_AUTO);
	if (!pdev)
		return -ENOMEM;

	exynos->usb2_phy = pdev;
	pdata.type = USB_PHY_TYPE_USB2;

	ret = platform_device_add_data(exynos->usb2_phy, &pdata, sizeof(pdata));
	if (ret)
		goto err1;

	pdev = platform_device_alloc("nop_usb_xceiv", PLATFORM_DEVID_AUTO);
	if (!pdev) {
		ret = -ENOMEM;
		goto err1;
	}

	exynos->usb3_phy = pdev;
	pdata.type = USB_PHY_TYPE_USB3;

	ret = platform_device_add_data(exynos->usb3_phy, &pdata, sizeof(pdata));
	if (ret)
		goto err2;

	ret = platform_device_add(exynos->usb2_phy);
	if (ret)
		goto err2;

	ret = platform_device_add(exynos->usb3_phy);
	if (ret)
		goto err3;

	return 0;

err3:
	platform_device_del(exynos->usb2_phy);

err2:
	platform_device_put(exynos->usb3_phy);

err1:
	platform_device_put(exynos->usb2_phy);

	return ret;
}

static u64 dwc3_exynos_dma_mask = DMA_BIT_MASK(32);

static int dwc3_exynos_probe(struct platform_device *pdev)
{
	struct platform_device	*dwc3;
	struct dwc3_exynos	*exynos;
	struct clk		*clk;
	struct device		*dev = &pdev->dev;

	int			ret = -ENOMEM;

	exynos = devm_kzalloc(dev, sizeof(*exynos), GFP_KERNEL);
	if (!exynos) {
		dev_err(dev, "not enough memory\n");
		return -ENOMEM;
	}

	/*
	 * Right now device-tree probed devices don't get dma_mask set.
	 * Since shared usb code relies on it, set it here for now.
	 * Once we move to full device tree support this will vanish off.
	 */
	if (!pdev->dev.dma_mask)
		pdev->dev.dma_mask = &dwc3_exynos_dma_mask;

	platform_set_drvdata(pdev, exynos);

	ret = dwc3_exynos_register_phys(exynos);
	if (ret) {
		dev_err(dev, "couldn't register PHYs\n");
		return ret;
	}

	dwc3 = platform_device_alloc("dwc3", PLATFORM_DEVID_AUTO);
	if (!dwc3) {
		dev_err(dev, "couldn't allocate dwc3 device\n");
		return -ENOMEM;
	}

	clk = devm_clk_get(dev, "usbdrd30");
	if (IS_ERR(clk)) {
		dev_err(dev, "couldn't get clock\n");
		ret = -EINVAL;
		goto err1;
	}

	dma_set_coherent_mask(&dwc3->dev, dev->coherent_dma_mask);

	dwc3->dev.parent = dev;
	dwc3->dev.dma_mask = dev->dma_mask;
	dwc3->dev.dma_parms = dev->dma_parms;
	exynos->dwc3	= dwc3;
	exynos->dev	= dev;
	exynos->clk	= clk;

	clk_enable(exynos->clk);

	ret = platform_device_add_resources(dwc3, pdev->resource,
			pdev->num_resources);
	if (ret) {
		dev_err(dev, "couldn't add resources to dwc3 device\n");
		goto err2;
	}

	ret = platform_device_add(dwc3);
	if (ret) {
		dev_err(dev, "failed to register dwc3 device\n");
		goto err2;
	}

	return 0;

err2:
	clk_disable(clk);
err1:
	platform_device_put(dwc3);

	return ret;
}

static int dwc3_exynos_remove(struct platform_device *pdev)
{
	struct dwc3_exynos	*exynos = platform_get_drvdata(pdev);

	platform_device_unregister(exynos->dwc3);
	platform_device_unregister(exynos->usb2_phy);
	platform_device_unregister(exynos->usb3_phy);

	clk_disable(exynos->clk);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id exynos_dwc3_match[] = {
	{ .compatible = "samsung,exynos5250-dwusb3" },
	{},
};
MODULE_DEVICE_TABLE(of, exynos_dwc3_match);
#endif

static struct platform_driver dwc3_exynos_driver = {
	.probe		= dwc3_exynos_probe,
	.remove		= dwc3_exynos_remove,
	.driver		= {
		.name	= "exynos-dwc3",
		.of_match_table = of_match_ptr(exynos_dwc3_match),
	},
};

module_platform_driver(dwc3_exynos_driver);

MODULE_ALIAS("platform:exynos-dwc3");
MODULE_AUTHOR("Anton Tikhomirov <av.tikhomirov@samsung.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("DesignWare USB3 EXYNOS Glue Layer");
