// SPDX-License-Identifier: GPL-2.0+
/*
 * ehci-omap.c - driver for USBHOST on OMAP3/4 processors
 *
 * Bus Glue for the EHCI controllers in OMAP3/4
 * Tested on several OMAP3 boards, and OMAP4 Pandaboard
 *
 * Copyright (C) 2007-2013 Texas Instruments, Inc.
 *	Author: Vikram Pandita <vikram.pandita@ti.com>
 *	Author: Anand Gadiyar <gadiyar@ti.com>
 *	Author: Keshava Munegowda <keshava_mgowda@ti.com>
 *	Author: Roger Quadros <rogerq@ti.com>
 *
 * Copyright (C) 2009 Nokia Corporation
 *	Contact: Felipe Balbi <felipe.balbi@nokia.com>
 *
 * Based on "ehci-fsl.c" and "ehci-au1xxx.c" ehci glue layers
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/usb/ulpi.h>
#include <linux/pm_runtime.h>
#include <linux/clk.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>
#include <linux/of.h>
#include <linux/dma-mapping.h>

#include "ehci.h"

#include <linux/platform_data/usb-omap.h>

/* EHCI Register Set */
#define EHCI_INSNREG04					(0xA0)
#define EHCI_INSNREG04_DISABLE_UNSUSPEND		(1 << 5)
#define	EHCI_INSNREG05_ULPI				(0xA4)
#define	EHCI_INSNREG05_ULPI_CONTROL_SHIFT		31
#define	EHCI_INSNREG05_ULPI_PORTSEL_SHIFT		24
#define	EHCI_INSNREG05_ULPI_OPSEL_SHIFT			22
#define	EHCI_INSNREG05_ULPI_REGADD_SHIFT		16
#define	EHCI_INSNREG05_ULPI_EXTREGADD_SHIFT		8
#define	EHCI_INSNREG05_ULPI_WRDATA_SHIFT		0

#define DRIVER_DESC "OMAP-EHCI Host Controller driver"

static const char hcd_name[] = "ehci-omap";

/*-------------------------------------------------------------------------*/

struct omap_hcd {
	struct usb_phy *phy[OMAP3_HS_USB_PORTS]; /* one PHY for each port */
	int nports;
};

static inline void ehci_write(void __iomem *base, u32 reg, u32 val)
{
	__raw_writel(val, base + reg);
}

/* configure so an HC device and id are always provided */
/* always called with process context; sleeping is OK */

static struct hc_driver __read_mostly ehci_omap_hc_driver;

static const struct ehci_driver_overrides ehci_omap_overrides __initconst = {
	.extra_priv_size = sizeof(struct omap_hcd),
};

/**
 * ehci_hcd_omap_probe - initialize TI-based HCDs
 * @pdev: Pointer to this platform device's information
 *
 * Allocates basic resources for this USB host controller, and
 * then invokes the start() method for the HCD associated with it
 * through the hotplug entry's driver_data.
 */
static int ehci_hcd_omap_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct usbhs_omap_platform_data *pdata = dev_get_platdata(dev);
	struct resource	*res;
	struct usb_hcd	*hcd;
	void __iomem *regs;
	int ret;
	int irq;
	int i;
	struct omap_hcd	*omap;

	if (usb_disabled())
		return -ENODEV;

	if (!dev->parent) {
		dev_err(dev, "Missing parent device\n");
		return -ENODEV;
	}

	/* For DT boot, get platform data from parent. i.e. usbhshost */
	if (dev->of_node) {
		pdata = dev_get_platdata(dev->parent);
		dev->platform_data = pdata;
	}

	if (!pdata) {
		dev_err(dev, "Missing platform data\n");
		return -ENODEV;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	regs = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	/*
	 * Right now device-tree probed devices don't get dma_mask set.
	 * Since shared usb code relies on it, set it here for now.
	 * Once we have dma capability bindings this can go away.
	 */
	ret = dma_coerce_mask_and_coherent(dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	ret = -ENODEV;
	hcd = usb_create_hcd(&ehci_omap_hc_driver, dev,
			dev_name(dev));
	if (!hcd) {
		dev_err(dev, "Failed to create HCD\n");
		return -ENOMEM;
	}

	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_size(res);
	hcd->regs = regs;
	hcd_to_ehci(hcd)->caps = regs;

	omap = (struct omap_hcd *)hcd_to_ehci(hcd)->priv;
	omap->nports = pdata->nports;

	platform_set_drvdata(pdev, hcd);

	/* get the PHY devices if needed */
	for (i = 0 ; i < omap->nports ; i++) {
		struct usb_phy *phy;

		/* get the PHY device */
		phy = devm_usb_get_phy_by_phandle(dev, "phys", i);
		if (IS_ERR(phy)) {
			ret = PTR_ERR(phy);
			if (ret == -ENODEV) { /* no PHY */
				phy = NULL;
				continue;
			}

			if (ret != -EPROBE_DEFER)
				dev_err(dev, "Can't get PHY for port %d: %d\n",
					i, ret);
			goto err_phy;
		}

		omap->phy[i] = phy;

		if (pdata->port_mode[i] == OMAP_EHCI_PORT_MODE_PHY) {
			usb_phy_init(omap->phy[i]);
			/* bring PHY out of suspend */
			usb_phy_set_suspend(omap->phy[i], 0);
		}
	}

	pm_runtime_enable(dev);
	pm_runtime_get_sync(dev);

	/*
	 * An undocumented "feature" in the OMAP3 EHCI controller,
	 * causes suspended ports to be taken out of suspend when
	 * the USBCMD.Run/Stop bit is cleared (for example when
	 * we do ehci_bus_suspend).
	 * This breaks suspend-resume if the root-hub is allowed
	 * to suspend. Writing 1 to this undocumented register bit
	 * disables this feature and restores normal behavior.
	 */
	ehci_write(regs, EHCI_INSNREG04,
				EHCI_INSNREG04_DISABLE_UNSUSPEND);

	ret = usb_add_hcd(hcd, irq, IRQF_SHARED);
	if (ret) {
		dev_err(dev, "failed to add hcd with err %d\n", ret);
		goto err_pm_runtime;
	}
	device_wakeup_enable(hcd->self.controller);

	/*
	 * Bring PHYs out of reset for non PHY modes.
	 * Even though HSIC mode is a PHY-less mode, the reset
	 * line exists between the chips and can be modelled
	 * as a PHY device for reset control.
	 */
	for (i = 0; i < omap->nports; i++) {
		if (!omap->phy[i] ||
		     pdata->port_mode[i] == OMAP_EHCI_PORT_MODE_PHY)
			continue;

		usb_phy_init(omap->phy[i]);
		/* bring PHY out of suspend */
		usb_phy_set_suspend(omap->phy[i], 0);
	}

	return 0;

err_pm_runtime:
	pm_runtime_put_sync(dev);
	pm_runtime_disable(dev);

err_phy:
	for (i = 0; i < omap->nports; i++) {
		if (omap->phy[i])
			usb_phy_shutdown(omap->phy[i]);
	}

	usb_put_hcd(hcd);

	return ret;
}


/**
 * ehci_hcd_omap_remove - shutdown processing for EHCI HCDs
 * @pdev: USB Host Controller being removed
 *
 * Reverses the effect of usb_ehci_hcd_omap_probe(), first invoking
 * the HCD's stop() method.  It is always called from a thread
 * context, normally "rmmod", "apmd", or something similar.
 */
static void ehci_hcd_omap_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct omap_hcd *omap = (struct omap_hcd *)hcd_to_ehci(hcd)->priv;
	int i;

	usb_remove_hcd(hcd);

	for (i = 0; i < omap->nports; i++) {
		if (omap->phy[i])
			usb_phy_shutdown(omap->phy[i]);
	}

	usb_put_hcd(hcd);
	pm_runtime_put_sync(dev);
	pm_runtime_disable(dev);
}

static const struct of_device_id omap_ehci_dt_ids[] = {
	{ .compatible = "ti,ehci-omap" },
	{ }
};

MODULE_DEVICE_TABLE(of, omap_ehci_dt_ids);

static struct platform_driver ehci_hcd_omap_driver = {
	.probe			= ehci_hcd_omap_probe,
	.remove_new		= ehci_hcd_omap_remove,
	.shutdown		= usb_hcd_platform_shutdown,
	/*.suspend		= ehci_hcd_omap_suspend, */
	/*.resume		= ehci_hcd_omap_resume, */
	.driver = {
		.name		= hcd_name,
		.of_match_table = omap_ehci_dt_ids,
	}
};

/*-------------------------------------------------------------------------*/

static int __init ehci_omap_init(void)
{
	if (usb_disabled())
		return -ENODEV;

	ehci_init_driver(&ehci_omap_hc_driver, &ehci_omap_overrides);
	return platform_driver_register(&ehci_hcd_omap_driver);
}
module_init(ehci_omap_init);

static void __exit ehci_omap_cleanup(void)
{
	platform_driver_unregister(&ehci_hcd_omap_driver);
}
module_exit(ehci_omap_cleanup);

MODULE_ALIAS("platform:ehci-omap");
MODULE_AUTHOR("Texas Instruments, Inc.");
MODULE_AUTHOR("Felipe Balbi <felipe.balbi@nokia.com>");
MODULE_AUTHOR("Roger Quadros <rogerq@ti.com>");

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
