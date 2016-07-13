/* ehci-msm.c - HSUSB Host Controller Driver Implementation
 *
 * Copyright (c) 2008-2011, Code Aurora Forum. All rights reserved.
 *
 * Partly derived from ehci-fsl.c and ehci-hcd.c
 * Copyright (c) 2000-2004 by David Brownell
 * Copyright (c) 2005 MontaVista Software
 *
 * All source code in this file is licensed under the following license except
 * where indicated.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can find it at http://www.fsf.org
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/usb/otg.h>
#include <linux/usb/msm_hsusb_hw.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>
#include <linux/acpi.h>

#include "ehci.h"

#define MSM_USB_BASE (hcd->regs)

#define DRIVER_DESC "Qualcomm On-Chip EHCI Host Controller"

static const char hcd_name[] = "ehci-msm";
static struct hc_driver __read_mostly msm_hc_driver;

static int ehci_msm_reset(struct usb_hcd *hcd)
{
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	int retval;

	ehci->caps = USB_CAPLENGTH;
	hcd->has_tt = 1;

	retval = ehci_setup(hcd);
	if (retval)
		return retval;

	/* select ULPI phy and clear other status/control bits in PORTSC */
	writel(PORTSC_PTS_ULPI, USB_PORTSC);
	/* bursts of unspecified length. */
	writel(0, USB_AHBBURST);
	/* Use the AHB transactor, allow posted data writes */
	writel(0x8, USB_AHBMODE);
	/* Disable streaming mode and select host mode */
	writel(0x13, USB_USBMODE);
	/* Disable ULPI_TX_PKT_EN_CLR_FIX which is valid only for HSIC */
	writel(readl(USB_GENCONFIG_2) & ~ULPI_TX_PKT_EN_CLR_FIX, USB_GENCONFIG_2);

	return 0;
}

static int ehci_msm_probe(struct platform_device *pdev)
{
	struct usb_hcd *hcd;
	struct resource *res;
	struct usb_phy *phy;
	int ret;

	dev_dbg(&pdev->dev, "ehci_msm proble\n");

	hcd = usb_create_hcd(&msm_hc_driver, &pdev->dev, dev_name(&pdev->dev));
	if (!hcd) {
		dev_err(&pdev->dev, "Unable to create HCD\n");
		return  -ENOMEM;
	}

	ret = platform_get_irq(pdev, 0);
	if (ret < 0) {
		dev_err(&pdev->dev, "Unable to get IRQ resource\n");
		goto put_hcd;
	}
	hcd->irq = ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "Unable to get memory resource\n");
		ret = -ENODEV;
		goto put_hcd;
	}

	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_size(res);
	hcd->regs = devm_ioremap(&pdev->dev, hcd->rsrc_start, hcd->rsrc_len);
	if (!hcd->regs) {
		dev_err(&pdev->dev, "ioremap failed\n");
		ret = -ENOMEM;
		goto put_hcd;
	}

	/*
	 * If there is an OTG driver, let it take care of PHY initialization,
	 * clock management, powering up VBUS, mapping of registers address
	 * space and power management.
	 */
	if (pdev->dev.of_node)
		phy = devm_usb_get_phy_by_phandle(&pdev->dev, "usb-phy", 0);
	else
		phy = devm_usb_get_phy(&pdev->dev, USB_PHY_TYPE_USB2);

	if (IS_ERR(phy)) {
		if (PTR_ERR(phy) == -EPROBE_DEFER) {
			dev_err(&pdev->dev, "unable to find transceiver\n");
			ret = -EPROBE_DEFER;
			goto put_hcd;
		}
		phy = NULL;
	}

	hcd->usb_phy = phy;
	device_init_wakeup(&pdev->dev, 1);

	if (phy && phy->otg) {
		/*
		 * MSM OTG driver takes care of adding the HCD and
		 * placing hardware into low power mode via runtime PM.
		 */
		ret = otg_set_host(phy->otg, &hcd->self);
		if (ret < 0) {
			dev_err(&pdev->dev, "unable to register with transceiver\n");
			goto put_hcd;
		}

		pm_runtime_no_callbacks(&pdev->dev);
		pm_runtime_enable(&pdev->dev);
	} else {
		ret = usb_add_hcd(hcd, hcd->irq, IRQF_SHARED);
		if (ret)
			goto put_hcd;
	}

	return 0;

put_hcd:
	usb_put_hcd(hcd);

	return ret;
}

static int ehci_msm_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);

	device_init_wakeup(&pdev->dev, 0);
	pm_runtime_disable(&pdev->dev);
	pm_runtime_set_suspended(&pdev->dev);

	if (hcd->usb_phy && hcd->usb_phy->otg)
		otg_set_host(hcd->usb_phy->otg, NULL);
	else
		usb_remove_hcd(hcd);

	usb_put_hcd(hcd);

	return 0;
}

#ifdef CONFIG_PM
static int ehci_msm_pm_suspend(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	bool do_wakeup = device_may_wakeup(dev);

	dev_dbg(dev, "ehci-msm PM suspend\n");

	/* Only call ehci_suspend if ehci_setup has been done */
	if (ehci->sbrn)
		return ehci_suspend(hcd, do_wakeup);

	return 0;
}

static int ehci_msm_pm_resume(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);

	dev_dbg(dev, "ehci-msm PM resume\n");

	/* Only call ehci_resume if ehci_setup has been done */
	if (ehci->sbrn)
		ehci_resume(hcd, false);

	return 0;
}

#else
#define ehci_msm_pm_suspend	NULL
#define ehci_msm_pm_resume	NULL
#endif

static const struct dev_pm_ops ehci_msm_dev_pm_ops = {
	.suspend         = ehci_msm_pm_suspend,
	.resume          = ehci_msm_pm_resume,
};

static const struct acpi_device_id msm_ehci_acpi_ids[] = {
	{ "QCOM8040", 0 },
	{ }
};
MODULE_DEVICE_TABLE(acpi, msm_ehci_acpi_ids);

static const struct of_device_id msm_ehci_dt_match[] = {
	{ .compatible = "qcom,ehci-host", },
	{}
};
MODULE_DEVICE_TABLE(of, msm_ehci_dt_match);

static struct platform_driver ehci_msm_driver = {
	.probe	= ehci_msm_probe,
	.remove	= ehci_msm_remove,
	.shutdown = usb_hcd_platform_shutdown,
	.driver = {
		   .name = "msm_hsusb_host",
		   .pm = &ehci_msm_dev_pm_ops,
		   .of_match_table = msm_ehci_dt_match,
		   .acpi_match_table = ACPI_PTR(msm_ehci_acpi_ids),
	},
};

static const struct ehci_driver_overrides msm_overrides __initconst = {
	.reset = ehci_msm_reset,
};

static int __init ehci_msm_init(void)
{
	if (usb_disabled())
		return -ENODEV;

	pr_info("%s: " DRIVER_DESC "\n", hcd_name);
	ehci_init_driver(&msm_hc_driver, &msm_overrides);
	return platform_driver_register(&ehci_msm_driver);
}
module_init(ehci_msm_init);

static void __exit ehci_msm_cleanup(void)
{
	platform_driver_unregister(&ehci_msm_driver);
}
module_exit(ehci_msm_cleanup);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_ALIAS("platform:msm-ehci");
MODULE_LICENSE("GPL");
