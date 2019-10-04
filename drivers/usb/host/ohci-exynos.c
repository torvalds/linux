// SPDX-License-Identifier: GPL-2.0+
/*
 * SAMSUNG EXYNOS USB HOST OHCI Controller
 *
 * Copyright (C) 2011 Samsung Electronics Co.Ltd
 * Author: Jingoo Han <jg1.han@samsung.com>
 */

#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/phy/phy.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>

#include "ohci.h"

#define DRIVER_DESC "OHCI EXYNOS driver"

static const char hcd_name[] = "ohci-exynos";
static struct hc_driver __read_mostly exynos_ohci_hc_driver;

#define to_exynos_ohci(hcd) (struct exynos_ohci_hcd *)(hcd_to_ohci(hcd)->priv)

#define PHY_NUMBER 3

struct exynos_ohci_hcd {
	struct clk *clk;
	struct device_node *of_node;
	struct phy *phy[PHY_NUMBER];
	bool legacy_phy;
};

static int exynos_ohci_get_phy(struct device *dev,
				struct exynos_ohci_hcd *exynos_ohci)
{
	struct device_node *child;
	struct phy *phy;
	int phy_number, num_phys;
	int ret;

	/* Get PHYs for the controller */
	num_phys = of_count_phandle_with_args(dev->of_node, "phys",
					      "#phy-cells");
	for (phy_number = 0; phy_number < num_phys; phy_number++) {
		phy = devm_of_phy_get_by_index(dev, dev->of_node, phy_number);
		if (IS_ERR(phy))
			return PTR_ERR(phy);
		exynos_ohci->phy[phy_number] = phy;
	}
	if (num_phys > 0)
		return 0;

	/* Get PHYs using legacy bindings */
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
		exynos_ohci->phy[phy_number] = phy;
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

	exynos_ohci->legacy_phy = true;
	return 0;
}

static int exynos_ohci_phy_enable(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct exynos_ohci_hcd *exynos_ohci = to_exynos_ohci(hcd);
	int i;
	int ret = 0;

	for (i = 0; ret == 0 && i < PHY_NUMBER; i++)
		if (!IS_ERR(exynos_ohci->phy[i]))
			ret = phy_power_on(exynos_ohci->phy[i]);
	if (ret)
		for (i--; i >= 0; i--)
			if (!IS_ERR(exynos_ohci->phy[i]))
				phy_power_off(exynos_ohci->phy[i]);

	return ret;
}

static void exynos_ohci_phy_disable(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct exynos_ohci_hcd *exynos_ohci = to_exynos_ohci(hcd);
	int i;

	for (i = 0; i < PHY_NUMBER; i++)
		if (!IS_ERR(exynos_ohci->phy[i]))
			phy_power_off(exynos_ohci->phy[i]);
}

static int exynos_ohci_probe(struct platform_device *pdev)
{
	struct exynos_ohci_hcd *exynos_ohci;
	struct usb_hcd *hcd;
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

	hcd = usb_create_hcd(&exynos_ohci_hc_driver,
				&pdev->dev, dev_name(&pdev->dev));
	if (!hcd) {
		dev_err(&pdev->dev, "Unable to create HCD\n");
		return -ENOMEM;
	}

	exynos_ohci = to_exynos_ohci(hcd);

	err = exynos_ohci_get_phy(&pdev->dev, exynos_ohci);
	if (err)
		goto fail_clk;

	exynos_ohci->clk = devm_clk_get(&pdev->dev, "usbhost");

	if (IS_ERR(exynos_ohci->clk)) {
		dev_err(&pdev->dev, "Failed to get usbhost clock\n");
		err = PTR_ERR(exynos_ohci->clk);
		goto fail_clk;
	}

	err = clk_prepare_enable(exynos_ohci->clk);
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

	platform_set_drvdata(pdev, hcd);

	err = exynos_ohci_phy_enable(&pdev->dev);
	if (err) {
		dev_err(&pdev->dev, "Failed to enable USB phy\n");
		goto fail_io;
	}

	/*
	 * Workaround: reset of_node pointer to avoid conflict between legacy
	 * Exynos OHCI port subnodes and generic USB device bindings
	 */
	exynos_ohci->of_node = pdev->dev.of_node;
	if (exynos_ohci->legacy_phy)
		pdev->dev.of_node = NULL;

	err = usb_add_hcd(hcd, irq, IRQF_SHARED);
	if (err) {
		dev_err(&pdev->dev, "Failed to add USB HCD\n");
		goto fail_add_hcd;
	}
	device_wakeup_enable(hcd->self.controller);
	return 0;

fail_add_hcd:
	exynos_ohci_phy_disable(&pdev->dev);
	pdev->dev.of_node = exynos_ohci->of_node;
fail_io:
	clk_disable_unprepare(exynos_ohci->clk);
fail_clk:
	usb_put_hcd(hcd);
	return err;
}

static int exynos_ohci_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);
	struct exynos_ohci_hcd *exynos_ohci = to_exynos_ohci(hcd);

	pdev->dev.of_node = exynos_ohci->of_node;

	usb_remove_hcd(hcd);

	exynos_ohci_phy_disable(&pdev->dev);

	clk_disable_unprepare(exynos_ohci->clk);

	usb_put_hcd(hcd);

	return 0;
}

static void exynos_ohci_shutdown(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);

	if (hcd->driver->shutdown)
		hcd->driver->shutdown(hcd);
}

#ifdef CONFIG_PM
static int exynos_ohci_suspend(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct exynos_ohci_hcd *exynos_ohci = to_exynos_ohci(hcd);
	bool do_wakeup = device_may_wakeup(dev);
	int rc = ohci_suspend(hcd, do_wakeup);

	if (rc)
		return rc;

	exynos_ohci_phy_disable(dev);

	clk_disable_unprepare(exynos_ohci->clk);

	return 0;
}

static int exynos_ohci_resume(struct device *dev)
{
	struct usb_hcd *hcd			= dev_get_drvdata(dev);
	struct exynos_ohci_hcd *exynos_ohci	= to_exynos_ohci(hcd);
	int ret;

	clk_prepare_enable(exynos_ohci->clk);

	ret = exynos_ohci_phy_enable(dev);
	if (ret) {
		dev_err(dev, "Failed to enable USB phy\n");
		clk_disable_unprepare(exynos_ohci->clk);
		return ret;
	}

	ohci_resume(hcd, false);

	return 0;
}
#else
#define exynos_ohci_suspend	NULL
#define exynos_ohci_resume	NULL
#endif

static const struct ohci_driver_overrides exynos_overrides __initconst = {
	.extra_priv_size =	sizeof(struct exynos_ohci_hcd),
};

static const struct dev_pm_ops exynos_ohci_pm_ops = {
	.suspend	= exynos_ohci_suspend,
	.resume		= exynos_ohci_resume,
};

#ifdef CONFIG_OF
static const struct of_device_id exynos_ohci_match[] = {
	{ .compatible = "samsung,exynos4210-ohci" },
	{},
};
MODULE_DEVICE_TABLE(of, exynos_ohci_match);
#endif

static struct platform_driver exynos_ohci_driver = {
	.probe		= exynos_ohci_probe,
	.remove		= exynos_ohci_remove,
	.shutdown	= exynos_ohci_shutdown,
	.driver = {
		.name	= "exynos-ohci",
		.pm	= &exynos_ohci_pm_ops,
		.of_match_table	= of_match_ptr(exynos_ohci_match),
	}
};
static int __init ohci_exynos_init(void)
{
	if (usb_disabled())
		return -ENODEV;

	pr_info("%s: " DRIVER_DESC "\n", hcd_name);
	ohci_init_driver(&exynos_ohci_hc_driver, &exynos_overrides);
	return platform_driver_register(&exynos_ohci_driver);
}
module_init(ohci_exynos_init);

static void __exit ohci_exynos_cleanup(void)
{
	platform_driver_unregister(&exynos_ohci_driver);
}
module_exit(ohci_exynos_cleanup);

MODULE_ALIAS("platform:exynos-ohci");
MODULE_AUTHOR("Jingoo Han <jg1.han@samsung.com>");
MODULE_LICENSE("GPL v2");
