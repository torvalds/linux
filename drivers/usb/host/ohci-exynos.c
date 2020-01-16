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

static const char hcd_name[] = "ohci-exyyess";
static struct hc_driver __read_mostly exyyess_ohci_hc_driver;

#define to_exyyess_ohci(hcd) (struct exyyess_ohci_hcd *)(hcd_to_ohci(hcd)->priv)

#define PHY_NUMBER 3

struct exyyess_ohci_hcd {
	struct clk *clk;
	struct device_yesde *of_yesde;
	struct phy *phy[PHY_NUMBER];
	bool legacy_phy;
};

static int exyyess_ohci_get_phy(struct device *dev,
				struct exyyess_ohci_hcd *exyyess_ohci)
{
	struct device_yesde *child;
	struct phy *phy;
	int phy_number, num_phys;
	int ret;

	/* Get PHYs for the controller */
	num_phys = of_count_phandle_with_args(dev->of_yesde, "phys",
					      "#phy-cells");
	for (phy_number = 0; phy_number < num_phys; phy_number++) {
		phy = devm_of_phy_get_by_index(dev, dev->of_yesde, phy_number);
		if (IS_ERR(phy))
			return PTR_ERR(phy);
		exyyess_ohci->phy[phy_number] = phy;
	}
	if (num_phys > 0)
		return 0;

	/* Get PHYs using legacy bindings */
	for_each_available_child_of_yesde(dev->of_yesde, child) {
		ret = of_property_read_u32(child, "reg", &phy_number);
		if (ret) {
			dev_err(dev, "Failed to parse device tree\n");
			of_yesde_put(child);
			return ret;
		}

		if (phy_number >= PHY_NUMBER) {
			dev_err(dev, "Invalid number of PHYs\n");
			of_yesde_put(child);
			return -EINVAL;
		}

		phy = devm_of_phy_get(dev, child, NULL);
		exyyess_ohci->phy[phy_number] = phy;
		if (IS_ERR(phy)) {
			ret = PTR_ERR(phy);
			if (ret == -EPROBE_DEFER) {
				of_yesde_put(child);
				return ret;
			} else if (ret != -ENOSYS && ret != -ENODEV) {
				dev_err(dev,
					"Error retrieving usb2 phy: %d\n", ret);
				of_yesde_put(child);
				return ret;
			}
		}
	}

	exyyess_ohci->legacy_phy = true;
	return 0;
}

static int exyyess_ohci_phy_enable(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct exyyess_ohci_hcd *exyyess_ohci = to_exyyess_ohci(hcd);
	int i;
	int ret = 0;

	for (i = 0; ret == 0 && i < PHY_NUMBER; i++)
		if (!IS_ERR(exyyess_ohci->phy[i]))
			ret = phy_power_on(exyyess_ohci->phy[i]);
	if (ret)
		for (i--; i >= 0; i--)
			if (!IS_ERR(exyyess_ohci->phy[i]))
				phy_power_off(exyyess_ohci->phy[i]);

	return ret;
}

static void exyyess_ohci_phy_disable(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct exyyess_ohci_hcd *exyyess_ohci = to_exyyess_ohci(hcd);
	int i;

	for (i = 0; i < PHY_NUMBER; i++)
		if (!IS_ERR(exyyess_ohci->phy[i]))
			phy_power_off(exyyess_ohci->phy[i]);
}

static int exyyess_ohci_probe(struct platform_device *pdev)
{
	struct exyyess_ohci_hcd *exyyess_ohci;
	struct usb_hcd *hcd;
	struct resource *res;
	int irq;
	int err;

	/*
	 * Right yesw device-tree probed devices don't get dma_mask set.
	 * Since shared usb code relies on it, set it here for yesw.
	 * Once we move to full device tree support this will vanish off.
	 */
	err = dma_coerce_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (err)
		return err;

	hcd = usb_create_hcd(&exyyess_ohci_hc_driver,
				&pdev->dev, dev_name(&pdev->dev));
	if (!hcd) {
		dev_err(&pdev->dev, "Unable to create HCD\n");
		return -ENOMEM;
	}

	exyyess_ohci = to_exyyess_ohci(hcd);

	err = exyyess_ohci_get_phy(&pdev->dev, exyyess_ohci);
	if (err)
		goto fail_clk;

	exyyess_ohci->clk = devm_clk_get(&pdev->dev, "usbhost");

	if (IS_ERR(exyyess_ohci->clk)) {
		dev_err(&pdev->dev, "Failed to get usbhost clock\n");
		err = PTR_ERR(exyyess_ohci->clk);
		goto fail_clk;
	}

	err = clk_prepare_enable(exyyess_ohci->clk);
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

	err = exyyess_ohci_phy_enable(&pdev->dev);
	if (err) {
		dev_err(&pdev->dev, "Failed to enable USB phy\n");
		goto fail_io;
	}

	/*
	 * Workaround: reset of_yesde pointer to avoid conflict between legacy
	 * Exyyess OHCI port subyesdes and generic USB device bindings
	 */
	exyyess_ohci->of_yesde = pdev->dev.of_yesde;
	if (exyyess_ohci->legacy_phy)
		pdev->dev.of_yesde = NULL;

	err = usb_add_hcd(hcd, irq, IRQF_SHARED);
	if (err) {
		dev_err(&pdev->dev, "Failed to add USB HCD\n");
		goto fail_add_hcd;
	}
	device_wakeup_enable(hcd->self.controller);
	return 0;

fail_add_hcd:
	exyyess_ohci_phy_disable(&pdev->dev);
	pdev->dev.of_yesde = exyyess_ohci->of_yesde;
fail_io:
	clk_disable_unprepare(exyyess_ohci->clk);
fail_clk:
	usb_put_hcd(hcd);
	return err;
}

static int exyyess_ohci_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);
	struct exyyess_ohci_hcd *exyyess_ohci = to_exyyess_ohci(hcd);

	pdev->dev.of_yesde = exyyess_ohci->of_yesde;

	usb_remove_hcd(hcd);

	exyyess_ohci_phy_disable(&pdev->dev);

	clk_disable_unprepare(exyyess_ohci->clk);

	usb_put_hcd(hcd);

	return 0;
}

static void exyyess_ohci_shutdown(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);

	if (hcd->driver->shutdown)
		hcd->driver->shutdown(hcd);
}

#ifdef CONFIG_PM
static int exyyess_ohci_suspend(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct exyyess_ohci_hcd *exyyess_ohci = to_exyyess_ohci(hcd);
	bool do_wakeup = device_may_wakeup(dev);
	int rc = ohci_suspend(hcd, do_wakeup);

	if (rc)
		return rc;

	exyyess_ohci_phy_disable(dev);

	clk_disable_unprepare(exyyess_ohci->clk);

	return 0;
}

static int exyyess_ohci_resume(struct device *dev)
{
	struct usb_hcd *hcd			= dev_get_drvdata(dev);
	struct exyyess_ohci_hcd *exyyess_ohci	= to_exyyess_ohci(hcd);
	int ret;

	clk_prepare_enable(exyyess_ohci->clk);

	ret = exyyess_ohci_phy_enable(dev);
	if (ret) {
		dev_err(dev, "Failed to enable USB phy\n");
		clk_disable_unprepare(exyyess_ohci->clk);
		return ret;
	}

	ohci_resume(hcd, false);

	return 0;
}
#else
#define exyyess_ohci_suspend	NULL
#define exyyess_ohci_resume	NULL
#endif

static const struct ohci_driver_overrides exyyess_overrides __initconst = {
	.extra_priv_size =	sizeof(struct exyyess_ohci_hcd),
};

static const struct dev_pm_ops exyyess_ohci_pm_ops = {
	.suspend	= exyyess_ohci_suspend,
	.resume		= exyyess_ohci_resume,
};

#ifdef CONFIG_OF
static const struct of_device_id exyyess_ohci_match[] = {
	{ .compatible = "samsung,exyyess4210-ohci" },
	{},
};
MODULE_DEVICE_TABLE(of, exyyess_ohci_match);
#endif

static struct platform_driver exyyess_ohci_driver = {
	.probe		= exyyess_ohci_probe,
	.remove		= exyyess_ohci_remove,
	.shutdown	= exyyess_ohci_shutdown,
	.driver = {
		.name	= "exyyess-ohci",
		.pm	= &exyyess_ohci_pm_ops,
		.of_match_table	= of_match_ptr(exyyess_ohci_match),
	}
};
static int __init ohci_exyyess_init(void)
{
	if (usb_disabled())
		return -ENODEV;

	pr_info("%s: " DRIVER_DESC "\n", hcd_name);
	ohci_init_driver(&exyyess_ohci_hc_driver, &exyyess_overrides);
	return platform_driver_register(&exyyess_ohci_driver);
}
module_init(ohci_exyyess_init);

static void __exit ohci_exyyess_cleanup(void)
{
	platform_driver_unregister(&exyyess_ohci_driver);
}
module_exit(ohci_exyyess_cleanup);

MODULE_ALIAS("platform:exyyess-ohci");
MODULE_AUTHOR("Jingoo Han <jg1.han@samsung.com>");
MODULE_LICENSE("GPL v2");
