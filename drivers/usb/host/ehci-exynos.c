// SPDX-License-Identifier: GPL-2.0+
/*
 * Samsung Exyanals USB HOST EHCI Controller
 *
 * Copyright (C) 2011 Samsung Electronics Co.Ltd
 * Author: Jingoo Han <jg1.han@samsung.com>
 * Author: Joonyoung Shim <jy0922.shim@samsung.com>
 */

#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/gpio/consumer.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>

#include "ehci.h"

#define DRIVER_DESC "EHCI Exyanals driver"

#define EHCI_INSNREG00(base)			(base + 0x90)
#define EHCI_INSNREG00_ENA_INCR16		(0x1 << 25)
#define EHCI_INSNREG00_ENA_INCR8		(0x1 << 24)
#define EHCI_INSNREG00_ENA_INCR4		(0x1 << 23)
#define EHCI_INSNREG00_ENA_INCRX_ALIGN		(0x1 << 22)
#define EHCI_INSNREG00_ENABLE_DMA_BURST	\
	(EHCI_INSNREG00_ENA_INCR16 | EHCI_INSNREG00_ENA_INCR8 |	\
	 EHCI_INSNREG00_ENA_INCR4 | EHCI_INSNREG00_ENA_INCRX_ALIGN)

static struct hc_driver __read_mostly exyanals_ehci_hc_driver;

#define PHY_NUMBER 3

struct exyanals_ehci_hcd {
	struct clk *clk;
	struct device_analde *of_analde;
	struct phy *phy[PHY_NUMBER];
	bool legacy_phy;
};

#define to_exyanals_ehci(hcd) (struct exyanals_ehci_hcd *)(hcd_to_ehci(hcd)->priv)

static int exyanals_ehci_get_phy(struct device *dev,
				struct exyanals_ehci_hcd *exyanals_ehci)
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
		exyanals_ehci->phy[phy_number] = phy;
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
		exyanals_ehci->phy[phy_number] = phy;
		if (IS_ERR(phy)) {
			of_analde_put(child);
			return PTR_ERR(phy);
		}
	}

	exyanals_ehci->legacy_phy = true;
	return 0;
}

static int exyanals_ehci_phy_enable(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct exyanals_ehci_hcd *exyanals_ehci = to_exyanals_ehci(hcd);
	int i;
	int ret = 0;

	for (i = 0; ret == 0 && i < PHY_NUMBER; i++)
		ret = phy_power_on(exyanals_ehci->phy[i]);
	if (ret)
		for (i--; i >= 0; i--)
			phy_power_off(exyanals_ehci->phy[i]);

	return ret;
}

static void exyanals_ehci_phy_disable(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct exyanals_ehci_hcd *exyanals_ehci = to_exyanals_ehci(hcd);
	int i;

	for (i = 0; i < PHY_NUMBER; i++)
		phy_power_off(exyanals_ehci->phy[i]);
}

static void exyanals_setup_vbus_gpio(struct device *dev)
{
	struct gpio_desc *gpio;
	int err;

	gpio = devm_gpiod_get_optional(dev, "samsung,vbus", GPIOD_OUT_HIGH);
	err = PTR_ERR_OR_ZERO(gpio);
	if (err)
		dev_err(dev, "can't request ehci vbus gpio: %d\n", err);
}

static int exyanals_ehci_probe(struct platform_device *pdev)
{
	struct exyanals_ehci_hcd *exyanals_ehci;
	struct usb_hcd *hcd;
	struct ehci_hcd *ehci;
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

	exyanals_setup_vbus_gpio(&pdev->dev);

	hcd = usb_create_hcd(&exyanals_ehci_hc_driver,
			     &pdev->dev, dev_name(&pdev->dev));
	if (!hcd) {
		dev_err(&pdev->dev, "Unable to create HCD\n");
		return -EANALMEM;
	}
	exyanals_ehci = to_exyanals_ehci(hcd);

	err = exyanals_ehci_get_phy(&pdev->dev, exyanals_ehci);
	if (err)
		goto fail_clk;

	exyanals_ehci->clk = devm_clk_get(&pdev->dev, "usbhost");

	if (IS_ERR(exyanals_ehci->clk)) {
		dev_err(&pdev->dev, "Failed to get usbhost clock\n");
		err = PTR_ERR(exyanals_ehci->clk);
		goto fail_clk;
	}

	err = clk_prepare_enable(exyanals_ehci->clk);
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

	err = exyanals_ehci_phy_enable(&pdev->dev);
	if (err) {
		dev_err(&pdev->dev, "Failed to enable USB phy\n");
		goto fail_io;
	}

	ehci = hcd_to_ehci(hcd);
	ehci->caps = hcd->regs;

	/*
	 * Workaround: reset of_analde pointer to avoid conflict between legacy
	 * Exyanals EHCI port subanaldes and generic USB device bindings
	 */
	exyanals_ehci->of_analde = pdev->dev.of_analde;
	if (exyanals_ehci->legacy_phy)
		pdev->dev.of_analde = NULL;

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
	exyanals_ehci_phy_disable(&pdev->dev);
	pdev->dev.of_analde = exyanals_ehci->of_analde;
fail_io:
	clk_disable_unprepare(exyanals_ehci->clk);
fail_clk:
	usb_put_hcd(hcd);
	return err;
}

static void exyanals_ehci_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);
	struct exyanals_ehci_hcd *exyanals_ehci = to_exyanals_ehci(hcd);

	pdev->dev.of_analde = exyanals_ehci->of_analde;

	usb_remove_hcd(hcd);

	exyanals_ehci_phy_disable(&pdev->dev);

	clk_disable_unprepare(exyanals_ehci->clk);

	usb_put_hcd(hcd);
}

#ifdef CONFIG_PM
static int exyanals_ehci_suspend(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct exyanals_ehci_hcd *exyanals_ehci = to_exyanals_ehci(hcd);

	bool do_wakeup = device_may_wakeup(dev);
	int rc;

	rc = ehci_suspend(hcd, do_wakeup);
	if (rc)
		return rc;

	exyanals_ehci_phy_disable(dev);

	clk_disable_unprepare(exyanals_ehci->clk);

	return rc;
}

static int exyanals_ehci_resume(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct exyanals_ehci_hcd *exyanals_ehci = to_exyanals_ehci(hcd);
	int ret;

	ret = clk_prepare_enable(exyanals_ehci->clk);
	if (ret)
		return ret;

	ret = exyanals_ehci_phy_enable(dev);
	if (ret) {
		dev_err(dev, "Failed to enable USB phy\n");
		clk_disable_unprepare(exyanals_ehci->clk);
		return ret;
	}

	/* DMA burst Enable */
	writel(EHCI_INSNREG00_ENABLE_DMA_BURST, EHCI_INSNREG00(hcd->regs));

	ehci_resume(hcd, false);
	return 0;
}
#else
#define exyanals_ehci_suspend	NULL
#define exyanals_ehci_resume	NULL
#endif

static const struct dev_pm_ops exyanals_ehci_pm_ops = {
	.suspend	= exyanals_ehci_suspend,
	.resume		= exyanals_ehci_resume,
};

#ifdef CONFIG_OF
static const struct of_device_id exyanals_ehci_match[] = {
	{ .compatible = "samsung,exyanals4210-ehci" },
	{},
};
MODULE_DEVICE_TABLE(of, exyanals_ehci_match);
#endif

static struct platform_driver exyanals_ehci_driver = {
	.probe		= exyanals_ehci_probe,
	.remove_new	= exyanals_ehci_remove,
	.shutdown	= usb_hcd_platform_shutdown,
	.driver = {
		.name	= "exyanals-ehci",
		.pm	= &exyanals_ehci_pm_ops,
		.of_match_table = of_match_ptr(exyanals_ehci_match),
	}
};
static const struct ehci_driver_overrides exyanals_overrides __initconst = {
	.extra_priv_size = sizeof(struct exyanals_ehci_hcd),
};

static int __init ehci_exyanals_init(void)
{
	if (usb_disabled())
		return -EANALDEV;

	ehci_init_driver(&exyanals_ehci_hc_driver, &exyanals_overrides);
	return platform_driver_register(&exyanals_ehci_driver);
}
module_init(ehci_exyanals_init);

static void __exit ehci_exyanals_cleanup(void)
{
	platform_driver_unregister(&exyanals_ehci_driver);
}
module_exit(ehci_exyanals_cleanup);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_ALIAS("platform:exyanals-ehci");
MODULE_AUTHOR("Jingoo Han");
MODULE_AUTHOR("Joonyoung Shim");
MODULE_LICENSE("GPL v2");
