/*
 * SAMSUNG EXYNOS USB HOST EHCI Controller
 *
 * Copyright (C) 2011 Samsung Electronics Co.Ltd
 * Author: Jingoo Han <jg1.han@samsung.com>
 * Author: Joonyoung Shim <jy0922.shim@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>

#include "ehci.h"

#define DRIVER_DESC "EHCI EXYNOS driver"

#define EHCI_INSNREG00(base)			(base + 0x90)
#define EHCI_INSNREG00_ENA_INCR16		(0x1 << 25)
#define EHCI_INSNREG00_ENA_INCR8		(0x1 << 24)
#define EHCI_INSNREG00_ENA_INCR4		(0x1 << 23)
#define EHCI_INSNREG00_ENA_INCRX_ALIGN		(0x1 << 22)
#define EHCI_INSNREG00_ENABLE_DMA_BURST	\
	(EHCI_INSNREG00_ENA_INCR16 | EHCI_INSNREG00_ENA_INCR8 |	\
	 EHCI_INSNREG00_ENA_INCR4 | EHCI_INSNREG00_ENA_INCRX_ALIGN)

static const char hcd_name[] = "ehci-exynos";
static struct hc_driver __read_mostly exynos_ehci_hc_driver;

#define PHY_NUMBER 3

struct exynos_ehci_hcd {
	struct clk *clk;
	struct phy *phy[PHY_NUMBER];
};

#define to_exynos_ehci(hcd) (struct exynos_ehci_hcd *)(hcd_to_ehci(hcd)->priv)

static int exynos_ehci_get_phy(struct device *dev,
				struct exynos_ehci_hcd *exynos_ehci)
{
	struct device_node *child;
	struct phy *phy;
	int phy_number;
	int ret;

	/* Get PHYs for the controller */
	for_each_available_child_of_node(dev->of_node, child) {
		ret = of_property_read_u32(child, "reg", &phy_number);
		if (ret) {
			dev_err(dev, "Failed to parse device tree\n");
			of_node_put(child);
			return ret;
		}

		if (phy_number >= PHY_NUMBER) {
			dev_err(dev, "Invalid number of PHYs\n");
			of_node_put(child);
			return -EINVAL;
		}

		phy = devm_of_phy_get(dev, child, NULL);
		exynos_ehci->phy[phy_number] = phy;
		if (IS_ERR(phy)) {
			ret = PTR_ERR(phy);
			if (ret == -EPROBE_DEFER) {
				of_node_put(child);
				return ret;
			} else if (ret != -ENOSYS && ret != -ENODEV) {
				dev_err(dev,
					"Error retrieving usb2 phy: %d\n", ret);
				of_node_put(child);
				return ret;
			}
		}
	}

	return 0;
}

static int exynos_ehci_phy_enable(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct exynos_ehci_hcd *exynos_ehci = to_exynos_ehci(hcd);
	int i;
	int ret = 0;

	for (i = 0; ret == 0 && i < PHY_NUMBER; i++)
		if (!IS_ERR(exynos_ehci->phy[i]))
			ret = phy_power_on(exynos_ehci->phy[i]);
	if (ret)
		for (i--; i >= 0; i--)
			if (!IS_ERR(exynos_ehci->phy[i]))
				phy_power_off(exynos_ehci->phy[i]);

	return ret;
}

static void exynos_ehci_phy_disable(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct exynos_ehci_hcd *exynos_ehci = to_exynos_ehci(hcd);
	int i;

	for (i = 0; i < PHY_NUMBER; i++)
		if (!IS_ERR(exynos_ehci->phy[i]))
			phy_power_off(exynos_ehci->phy[i]);
}

static void exynos_setup_vbus_gpio(struct device *dev)
{
	int err;
	int gpio;

	if (!dev->of_node)
		return;

	gpio = of_get_named_gpio(dev->of_node, "samsung,vbus-gpio", 0);
	if (!gpio_is_valid(gpio))
		return;

	err = devm_gpio_request_one(dev, gpio, GPIOF_OUT_INIT_HIGH,
				    "ehci_vbus_gpio");
	if (err)
		dev_err(dev, "can't request ehci vbus gpio %d", gpio);
}

static int exynos_ehci_probe(struct platform_device *pdev)
{
	struct exynos_ehci_hcd *exynos_ehci;
	struct usb_hcd *hcd;
	struct ehci_hcd *ehci;
	struct resource *res;
	int irq;
	int err;

	/*
	 * Right now device-tree probed devices don't get dma_mask set.
	 * Since shared usb code relies on it, set it here for now.
	 * Once we move to full device tree support this will vanish off.
	 */
	err = dma_coerce_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (err)
		return err;

	exynos_setup_vbus_gpio(&pdev->dev);

	hcd = usb_create_hcd(&exynos_ehci_hc_driver,
			     &pdev->dev, dev_name(&pdev->dev));
	if (!hcd) {
		dev_err(&pdev->dev, "Unable to create HCD\n");
		return -ENOMEM;
	}
	exynos_ehci = to_exynos_ehci(hcd);

	if (of_device_is_compatible(pdev->dev.of_node,
					"samsung,exynos5440-ehci"))
		goto skip_phy;

	err = exynos_ehci_get_phy(&pdev->dev, exynos_ehci);
	if (err)
		goto fail_clk;

skip_phy:

	exynos_ehci->clk = devm_clk_get(&pdev->dev, "usbhost");

	if (IS_ERR(exynos_ehci->clk)) {
		dev_err(&pdev->dev, "Failed to get usbhost clock\n");
		err = PTR_ERR(exynos_ehci->clk);
		goto fail_clk;
	}

	err = clk_prepare_enable(exynos_ehci->clk);
	if (err)
		goto fail_clk;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	hcd->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(hcd->regs)) {
		err = PTR_ERR(hcd->regs);
		goto fail_io;
	}

	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_size(res);

	irq = platform_get_irq(pdev, 0);
	if (!irq) {
		dev_err(&pdev->dev, "Failed to get IRQ\n");
		err = -ENODEV;
		goto fail_io;
	}

	err = exynos_ehci_phy_enable(&pdev->dev);
	if (err) {
		dev_err(&pdev->dev, "Failed to enable USB phy\n");
		goto fail_io;
	}

	ehci = hcd_to_ehci(hcd);
	ehci->caps = hcd->regs;

	/* DMA burst Enable */
	writel(EHCI_INSNREG00_ENABLE_DMA_BURST, EHCI_INSNREG00(hcd->regs));

	err = usb_add_hcd(hcd, irq, IRQF_SHARED);
	if (err) {
		dev_err(&pdev->dev, "Failed to add USB HCD\n");
		goto fail_add_hcd;
	}
	device_wakeup_enable(hcd->self.controller);

	platform_set_drvdata(pdev, hcd);

	return 0;

fail_add_hcd:
	exynos_ehci_phy_disable(&pdev->dev);
fail_io:
	clk_disable_unprepare(exynos_ehci->clk);
fail_clk:
	usb_put_hcd(hcd);
	return err;
}

static int exynos_ehci_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);
	struct exynos_ehci_hcd *exynos_ehci = to_exynos_ehci(hcd);

	usb_remove_hcd(hcd);

	exynos_ehci_phy_disable(&pdev->dev);

	clk_disable_unprepare(exynos_ehci->clk);

	usb_put_hcd(hcd);

	return 0;
}

#ifdef CONFIG_PM
static int exynos_ehci_suspend(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct exynos_ehci_hcd *exynos_ehci = to_exynos_ehci(hcd);

	bool do_wakeup = device_may_wakeup(dev);
	int rc;

	rc = ehci_suspend(hcd, do_wakeup);
	if (rc)
		return rc;

	exynos_ehci_phy_disable(dev);

	clk_disable_unprepare(exynos_ehci->clk);

	return rc;
}

static int exynos_ehci_resume(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct exynos_ehci_hcd *exynos_ehci = to_exynos_ehci(hcd);
	int ret;

	clk_prepare_enable(exynos_ehci->clk);

	ret = exynos_ehci_phy_enable(dev);
	if (ret) {
		dev_err(dev, "Failed to enable USB phy\n");
		clk_disable_unprepare(exynos_ehci->clk);
		return ret;
	}

	/* DMA burst Enable */
	writel(EHCI_INSNREG00_ENABLE_DMA_BURST, EHCI_INSNREG00(hcd->regs));

	ehci_resume(hcd, false);
	return 0;
}
#else
#define exynos_ehci_suspend	NULL
#define exynos_ehci_resume	NULL
#endif

static const struct dev_pm_ops exynos_ehci_pm_ops = {
	.suspend	= exynos_ehci_suspend,
	.resume		= exynos_ehci_resume,
};

#ifdef CONFIG_OF
static const struct of_device_id exynos_ehci_match[] = {
	{ .compatible = "samsung,exynos4210-ehci" },
	{ .compatible = "samsung,exynos5440-ehci" },
	{},
};
MODULE_DEVICE_TABLE(of, exynos_ehci_match);
#endif

static struct platform_driver exynos_ehci_driver = {
	.probe		= exynos_ehci_probe,
	.remove		= exynos_ehci_remove,
	.shutdown	= usb_hcd_platform_shutdown,
	.driver = {
		.name	= "exynos-ehci",
		.pm	= &exynos_ehci_pm_ops,
		.of_match_table = of_match_ptr(exynos_ehci_match),
	}
};
static const struct ehci_driver_overrides exynos_overrides __initdata = {
	.extra_priv_size = sizeof(struct exynos_ehci_hcd),
};

static int __init ehci_exynos_init(void)
{
	if (usb_disabled())
		return -ENODEV;

	pr_info("%s: " DRIVER_DESC "\n", hcd_name);
	ehci_init_driver(&exynos_ehci_hc_driver, &exynos_overrides);
	return platform_driver_register(&exynos_ehci_driver);
}
module_init(ehci_exynos_init);

static void __exit ehci_exynos_cleanup(void)
{
	platform_driver_unregister(&exynos_ehci_driver);
}
module_exit(ehci_exynos_cleanup);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_ALIAS("platform:exynos-ehci");
MODULE_AUTHOR("Jingoo Han");
MODULE_AUTHOR("Joonyoung Shim");
MODULE_LICENSE("GPL v2");
