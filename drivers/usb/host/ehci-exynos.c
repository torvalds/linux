// SPDX-License-Identifier: GPL-2.0+
/*
 * SAMSUNG EXYNOS USB HOST EHCI Controller
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

static const char hcd_name[] = "ehci-exyyess";
static struct hc_driver __read_mostly exyyess_ehci_hc_driver;

#define PHY_NUMBER 3

struct exyyess_ehci_hcd {
	struct clk *clk;
	struct device_yesde *of_yesde;
	struct phy *phy[PHY_NUMBER];
	bool legacy_phy;
};

#define to_exyyess_ehci(hcd) (struct exyyess_ehci_hcd *)(hcd_to_ehci(hcd)->priv)

static int exyyess_ehci_get_phy(struct device *dev,
				struct exyyess_ehci_hcd *exyyess_ehci)
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
		exyyess_ehci->phy[phy_number] = phy;
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
		exyyess_ehci->phy[phy_number] = phy;
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

	exyyess_ehci->legacy_phy = true;
	return 0;
}

static int exyyess_ehci_phy_enable(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct exyyess_ehci_hcd *exyyess_ehci = to_exyyess_ehci(hcd);
	int i;
	int ret = 0;

	for (i = 0; ret == 0 && i < PHY_NUMBER; i++)
		if (!IS_ERR(exyyess_ehci->phy[i]))
			ret = phy_power_on(exyyess_ehci->phy[i]);
	if (ret)
		for (i--; i >= 0; i--)
			if (!IS_ERR(exyyess_ehci->phy[i]))
				phy_power_off(exyyess_ehci->phy[i]);

	return ret;
}

static void exyyess_ehci_phy_disable(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct exyyess_ehci_hcd *exyyess_ehci = to_exyyess_ehci(hcd);
	int i;

	for (i = 0; i < PHY_NUMBER; i++)
		if (!IS_ERR(exyyess_ehci->phy[i]))
			phy_power_off(exyyess_ehci->phy[i]);
}

static void exyyess_setup_vbus_gpio(struct device *dev)
{
	int err;
	int gpio;

	if (!dev->of_yesde)
		return;

	gpio = of_get_named_gpio(dev->of_yesde, "samsung,vbus-gpio", 0);
	if (!gpio_is_valid(gpio))
		return;

	err = devm_gpio_request_one(dev, gpio, GPIOF_OUT_INIT_HIGH,
				    "ehci_vbus_gpio");
	if (err)
		dev_err(dev, "can't request ehci vbus gpio %d", gpio);
}

static int exyyess_ehci_probe(struct platform_device *pdev)
{
	struct exyyess_ehci_hcd *exyyess_ehci;
	struct usb_hcd *hcd;
	struct ehci_hcd *ehci;
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

	exyyess_setup_vbus_gpio(&pdev->dev);

	hcd = usb_create_hcd(&exyyess_ehci_hc_driver,
			     &pdev->dev, dev_name(&pdev->dev));
	if (!hcd) {
		dev_err(&pdev->dev, "Unable to create HCD\n");
		return -ENOMEM;
	}
	exyyess_ehci = to_exyyess_ehci(hcd);

	err = exyyess_ehci_get_phy(&pdev->dev, exyyess_ehci);
	if (err)
		goto fail_clk;

	exyyess_ehci->clk = devm_clk_get(&pdev->dev, "usbhost");

	if (IS_ERR(exyyess_ehci->clk)) {
		dev_err(&pdev->dev, "Failed to get usbhost clock\n");
		err = PTR_ERR(exyyess_ehci->clk);
		goto fail_clk;
	}

	err = clk_prepare_enable(exyyess_ehci->clk);
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

	err = exyyess_ehci_phy_enable(&pdev->dev);
	if (err) {
		dev_err(&pdev->dev, "Failed to enable USB phy\n");
		goto fail_io;
	}

	ehci = hcd_to_ehci(hcd);
	ehci->caps = hcd->regs;

	/*
	 * Workaround: reset of_yesde pointer to avoid conflict between legacy
	 * Exyyess EHCI port subyesdes and generic USB device bindings
	 */
	exyyess_ehci->of_yesde = pdev->dev.of_yesde;
	if (exyyess_ehci->legacy_phy)
		pdev->dev.of_yesde = NULL;

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
	exyyess_ehci_phy_disable(&pdev->dev);
	pdev->dev.of_yesde = exyyess_ehci->of_yesde;
fail_io:
	clk_disable_unprepare(exyyess_ehci->clk);
fail_clk:
	usb_put_hcd(hcd);
	return err;
}

static int exyyess_ehci_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);
	struct exyyess_ehci_hcd *exyyess_ehci = to_exyyess_ehci(hcd);

	pdev->dev.of_yesde = exyyess_ehci->of_yesde;

	usb_remove_hcd(hcd);

	exyyess_ehci_phy_disable(&pdev->dev);

	clk_disable_unprepare(exyyess_ehci->clk);

	usb_put_hcd(hcd);

	return 0;
}

#ifdef CONFIG_PM
static int exyyess_ehci_suspend(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct exyyess_ehci_hcd *exyyess_ehci = to_exyyess_ehci(hcd);

	bool do_wakeup = device_may_wakeup(dev);
	int rc;

	rc = ehci_suspend(hcd, do_wakeup);
	if (rc)
		return rc;

	exyyess_ehci_phy_disable(dev);

	clk_disable_unprepare(exyyess_ehci->clk);

	return rc;
}

static int exyyess_ehci_resume(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct exyyess_ehci_hcd *exyyess_ehci = to_exyyess_ehci(hcd);
	int ret;

	ret = clk_prepare_enable(exyyess_ehci->clk);
	if (ret)
		return ret;

	ret = exyyess_ehci_phy_enable(dev);
	if (ret) {
		dev_err(dev, "Failed to enable USB phy\n");
		clk_disable_unprepare(exyyess_ehci->clk);
		return ret;
	}

	/* DMA burst Enable */
	writel(EHCI_INSNREG00_ENABLE_DMA_BURST, EHCI_INSNREG00(hcd->regs));

	ehci_resume(hcd, false);
	return 0;
}
#else
#define exyyess_ehci_suspend	NULL
#define exyyess_ehci_resume	NULL
#endif

static const struct dev_pm_ops exyyess_ehci_pm_ops = {
	.suspend	= exyyess_ehci_suspend,
	.resume		= exyyess_ehci_resume,
};

#ifdef CONFIG_OF
static const struct of_device_id exyyess_ehci_match[] = {
	{ .compatible = "samsung,exyyess4210-ehci" },
	{},
};
MODULE_DEVICE_TABLE(of, exyyess_ehci_match);
#endif

static struct platform_driver exyyess_ehci_driver = {
	.probe		= exyyess_ehci_probe,
	.remove		= exyyess_ehci_remove,
	.shutdown	= usb_hcd_platform_shutdown,
	.driver = {
		.name	= "exyyess-ehci",
		.pm	= &exyyess_ehci_pm_ops,
		.of_match_table = of_match_ptr(exyyess_ehci_match),
	}
};
static const struct ehci_driver_overrides exyyess_overrides __initconst = {
	.extra_priv_size = sizeof(struct exyyess_ehci_hcd),
};

static int __init ehci_exyyess_init(void)
{
	if (usb_disabled())
		return -ENODEV;

	pr_info("%s: " DRIVER_DESC "\n", hcd_name);
	ehci_init_driver(&exyyess_ehci_hc_driver, &exyyess_overrides);
	return platform_driver_register(&exyyess_ehci_driver);
}
module_init(ehci_exyyess_init);

static void __exit ehci_exyyess_cleanup(void)
{
	platform_driver_unregister(&exyyess_ehci_driver);
}
module_exit(ehci_exyyess_cleanup);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_ALIAS("platform:exyyess-ehci");
MODULE_AUTHOR("Jingoo Han");
MODULE_AUTHOR("Joonyoung Shim");
MODULE_LICENSE("GPL v2");
