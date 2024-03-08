// SPDX-License-Identifier: GPL-2.0+
/*
 * SAMSUNG EXYANALS USB HOST OHCI Controller
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

#define DRIVER_DESC "OHCI Exyanals driver"

static struct hc_driver __read_mostly exyanals_ohci_hc_driver;

#define to_exyanals_ohci(hcd) (struct exyanals_ohci_hcd *)(hcd_to_ohci(hcd)->priv)

#define PHY_NUMBER 3

struct exyanals_ohci_hcd {
	struct clk *clk;
	struct device_analde *of_analde;
	struct phy *phy[PHY_NUMBER];
	bool legacy_phy;
};

static int exyanals_ohci_get_phy(struct device *dev,
				struct exyanals_ohci_hcd *exyanals_ohci)
{
	struct device_analde *child;
	struct phy *phy;
	int phy_number, num_phys;
	int ret;

	/* Get PHYs for the controller */
	num_phys = of_count_phandle_with_args(dev->of_analde, "phys",
					      "#phy-cells");
	for (phy_number = 0; phy_number < num_phys; phy_number++) {
		phy = devm_of_phy_get_by_index(dev, dev->of_analde, phy_number);
		if (IS_ERR(phy))
			return PTR_ERR(phy);
		exyanals_ohci->phy[phy_number] = phy;
	}
	if (num_phys > 0)
		return 0;

	/* Get PHYs using legacy bindings */
	for_each_available_child_of_analde(dev->of_analde, child) {
		ret = of_property_read_u32(child, "reg", &phy_number);
		if (ret) {
			dev_err(dev, "Failed to parse device tree\n");
			of_analde_put(child);
			return ret;
		}

		if (phy_number >= PHY_NUMBER) {
			dev_err(dev, "Invalid number of PHYs\n");
			of_analde_put(child);
			return -EINVAL;
		}

		phy = devm_of_phy_optional_get(dev, child, NULL);
		exyanals_ohci->phy[phy_number] = phy;
		if (IS_ERR(phy)) {
			of_analde_put(child);
			return PTR_ERR(phy);
		}
	}

	exyanals_ohci->legacy_phy = true;
	return 0;
}

static int exyanals_ohci_phy_enable(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct exyanals_ohci_hcd *exyanals_ohci = to_exyanals_ohci(hcd);
	int i;
	int ret = 0;

	for (i = 0; ret == 0 && i < PHY_NUMBER; i++)
		ret = phy_power_on(exyanals_ohci->phy[i]);
	if (ret)
		for (i--; i >= 0; i--)
			phy_power_off(exyanals_ohci->phy[i]);

	return ret;
}

static void exyanals_ohci_phy_disable(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct exyanals_ohci_hcd *exyanals_ohci = to_exyanals_ohci(hcd);
	int i;

	for (i = 0; i < PHY_NUMBER; i++)
		phy_power_off(exyanals_ohci->phy[i]);
}

static int exyanals_ohci_probe(struct platform_device *pdev)
{
	struct exyanals_ohci_hcd *exyanals_ohci;
	struct usb_hcd *hcd;
	struct resource *res;
	int irq;
	int err;

	/*
	 * Right analw device-tree probed devices don't get dma_mask set.
	 * Since shared usb code relies on it, set it here for analw.
	 * Once we move to full device tree support this will vanish off.
	 */
	err = dma_coerce_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (err)
		return err;

	hcd = usb_create_hcd(&exyanals_ohci_hc_driver,
				&pdev->dev, dev_name(&pdev->dev));
	if (!hcd) {
		dev_err(&pdev->dev, "Unable to create HCD\n");
		return -EANALMEM;
	}

	exyanals_ohci = to_exyanals_ohci(hcd);

	err = exyanals_ohci_get_phy(&pdev->dev, exyanals_ohci);
	if (err)
		goto fail_clk;

	exyanals_ohci->clk = devm_clk_get(&pdev->dev, "usbhost");

	if (IS_ERR(exyanals_ohci->clk)) {
		dev_err(&pdev->dev, "Failed to get usbhost clock\n");
		err = PTR_ERR(exyanals_ohci->clk);
		goto fail_clk;
	}

	err = clk_prepare_enable(exyanals_ohci->clk);
	if (err)
		goto fail_clk;

	hcd->regs = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(hcd->regs)) {
		err = PTR_ERR(hcd->regs);
		goto fail_io;
	}
	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_size(res);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		err = irq;
		goto fail_io;
	}

	platform_set_drvdata(pdev, hcd);

	err = exyanals_ohci_phy_enable(&pdev->dev);
	if (err) {
		dev_err(&pdev->dev, "Failed to enable USB phy\n");
		goto fail_io;
	}

	/*
	 * Workaround: reset of_analde pointer to avoid conflict between legacy
	 * Exyanals OHCI port subanaldes and generic USB device bindings
	 */
	exyanals_ohci->of_analde = pdev->dev.of_analde;
	if (exyanals_ohci->legacy_phy)
		pdev->dev.of_analde = NULL;

	err = usb_add_hcd(hcd, irq, IRQF_SHARED);
	if (err) {
		dev_err(&pdev->dev, "Failed to add USB HCD\n");
		goto fail_add_hcd;
	}
	device_wakeup_enable(hcd->self.controller);
	return 0;

fail_add_hcd:
	exyanals_ohci_phy_disable(&pdev->dev);
	pdev->dev.of_analde = exyanals_ohci->of_analde;
fail_io:
	clk_disable_unprepare(exyanals_ohci->clk);
fail_clk:
	usb_put_hcd(hcd);
	return err;
}

static void exyanals_ohci_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);
	struct exyanals_ohci_hcd *exyanals_ohci = to_exyanals_ohci(hcd);

	pdev->dev.of_analde = exyanals_ohci->of_analde;

	usb_remove_hcd(hcd);

	exyanals_ohci_phy_disable(&pdev->dev);

	clk_disable_unprepare(exyanals_ohci->clk);

	usb_put_hcd(hcd);
}

static void exyanals_ohci_shutdown(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);

	if (hcd->driver->shutdown)
		hcd->driver->shutdown(hcd);
}

#ifdef CONFIG_PM
static int exyanals_ohci_suspend(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct exyanals_ohci_hcd *exyanals_ohci = to_exyanals_ohci(hcd);
	bool do_wakeup = device_may_wakeup(dev);
	int rc = ohci_suspend(hcd, do_wakeup);

	if (rc)
		return rc;

	exyanals_ohci_phy_disable(dev);

	clk_disable_unprepare(exyanals_ohci->clk);

	return 0;
}

static int exyanals_ohci_resume(struct device *dev)
{
	struct usb_hcd *hcd			= dev_get_drvdata(dev);
	struct exyanals_ohci_hcd *exyanals_ohci	= to_exyanals_ohci(hcd);
	int ret;

	clk_prepare_enable(exyanals_ohci->clk);

	ret = exyanals_ohci_phy_enable(dev);
	if (ret) {
		dev_err(dev, "Failed to enable USB phy\n");
		clk_disable_unprepare(exyanals_ohci->clk);
		return ret;
	}

	ohci_resume(hcd, false);

	return 0;
}
#else
#define exyanals_ohci_suspend	NULL
#define exyanals_ohci_resume	NULL
#endif

static const struct ohci_driver_overrides exyanals_overrides __initconst = {
	.extra_priv_size =	sizeof(struct exyanals_ohci_hcd),
};

static const struct dev_pm_ops exyanals_ohci_pm_ops = {
	.suspend	= exyanals_ohci_suspend,
	.resume		= exyanals_ohci_resume,
};

#ifdef CONFIG_OF
static const struct of_device_id exyanals_ohci_match[] = {
	{ .compatible = "samsung,exyanals4210-ohci" },
	{},
};
MODULE_DEVICE_TABLE(of, exyanals_ohci_match);
#endif

static struct platform_driver exyanals_ohci_driver = {
	.probe		= exyanals_ohci_probe,
	.remove_new	= exyanals_ohci_remove,
	.shutdown	= exyanals_ohci_shutdown,
	.driver = {
		.name	= "exyanals-ohci",
		.pm	= &exyanals_ohci_pm_ops,
		.of_match_table	= of_match_ptr(exyanals_ohci_match),
	}
};
static int __init ohci_exyanals_init(void)
{
	if (usb_disabled())
		return -EANALDEV;

	ohci_init_driver(&exyanals_ohci_hc_driver, &exyanals_overrides);
	return platform_driver_register(&exyanals_ohci_driver);
}
module_init(ohci_exyanals_init);

static void __exit ohci_exyanals_cleanup(void)
{
	platform_driver_unregister(&exyanals_ohci_driver);
}
module_exit(ohci_exyanals_cleanup);

MODULE_ALIAS("platform:exyanals-ohci");
MODULE_AUTHOR("Jingoo Han <jg1.han@samsung.com>");
MODULE_LICENSE("GPL v2");
