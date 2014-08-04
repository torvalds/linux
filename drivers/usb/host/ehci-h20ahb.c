/*
 * Copyright (C) 2007-2013 Texas Instruments, Inc.
 *	Author: Vikram Pandita <vikram.pandita@ti.com>
 *	Author: Anand Gadiyar <gadiyar@ti.com>
 *	Author: Keshava Munegowda <keshava_mgowda@ti.com>
 *	Author: Roger Quadros <rogerq@ti.com>
 *
 * Copyright (C) 2009 Nokia Corporation
 *	Contact: Felipe Balbi <felipe.balbi@nokia.com>
 *
 * Based on ehci-omap.c - driver for USBHOST on OMAP3/4 processors
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/usb/ulpi.h>
#include <linux/pm_runtime.h>
#include <linux/gpio.h>
#include <linux/clk.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>
#include <linux/of.h>
#include <linux/dma-mapping.h>

#include "ehci.h"

#define H20AHB_HS_USB_PORTS	1

/* EHCI Synopsys-specific Register Set */
#define EHCI_INSNREG04					(0xA0)
#define EHCI_INSNREG04_DISABLE_UNSUSPEND		(1 << 5)
#define	EHCI_INSNREG05_ULPI				(0xA4)
#define	EHCI_INSNREG05_ULPI_CONTROL_SHIFT		31
#define	EHCI_INSNREG05_ULPI_PORTSEL_SHIFT		24
#define	EHCI_INSNREG05_ULPI_OPSEL_SHIFT			22
#define	EHCI_INSNREG05_ULPI_REGADD_SHIFT		16
#define	EHCI_INSNREG05_ULPI_EXTREGADD_SHIFT		8
#define	EHCI_INSNREG05_ULPI_WRDATA_SHIFT		0

#define DRIVER_DESC "H20AHB-EHCI Host Controller driver"

static const char hcd_name[] = "ehci-h20ahb";

/*-------------------------------------------------------------------------*/

struct h20ahb_hcd {
	struct usb_phy *phy[H20AHB_HS_USB_PORTS]; /* one PHY for each port */
	int nports;
};

static inline void ehci_write(void __iomem *base, u32 reg, u32 val)
{
	writel_relaxed(val, base + reg);
}

static inline u32 ehci_read(void __iomem *base, u32 reg)
{
	return readl_relaxed(base + reg);
}

/* configure so an HC device and id are always provided */
/* always called with process context; sleeping is OK */

static struct hc_driver __read_mostly ehci_h20ahb_hc_driver;

static const struct ehci_driver_overrides ehci_h20ahb_overrides __initdata = {
	.extra_priv_size = sizeof(struct h20ahb_hcd),
};

static int ehci_h20ahb_phy_read(struct usb_phy *x, u32 reg)
{
	u32 val = (1 << EHCI_INSNREG05_ULPI_CONTROL_SHIFT) |
		(1 << EHCI_INSNREG05_ULPI_PORTSEL_SHIFT) |
		(3 << EHCI_INSNREG05_ULPI_OPSEL_SHIFT) |
		(reg << EHCI_INSNREG05_ULPI_REGADD_SHIFT);
	ehci_write(x->io_priv, 0, val);
	while ((val = ehci_read(x->io_priv, 0)) &
		(1 << EHCI_INSNREG05_ULPI_CONTROL_SHIFT));
	return val & 0xff;
}

static int ehci_h20ahb_phy_write(struct usb_phy *x, u32 val, u32 reg)
{
	u32 v = (1 << EHCI_INSNREG05_ULPI_CONTROL_SHIFT) |
		(1 << EHCI_INSNREG05_ULPI_PORTSEL_SHIFT) |
		(2 << EHCI_INSNREG05_ULPI_OPSEL_SHIFT) |
		(reg << EHCI_INSNREG05_ULPI_REGADD_SHIFT) |
		(val & 0xff);
	ehci_write(x->io_priv, 0, v);
	while ((v = ehci_read(x->io_priv, 0)) &
		(1 << EHCI_INSNREG05_ULPI_CONTROL_SHIFT));
	return 0;
}

static struct usb_phy_io_ops ehci_h20ahb_phy_io_ops = {
	.read = ehci_h20ahb_phy_read,
	.write = ehci_h20ahb_phy_write,
};


/**
 * ehci_hcd_h20ahb_probe - initialize Synopsis-based HCDs
 *
 * Allocates basic resources for this USB host controller, and
 * then invokes the start() method for the HCD associated with it
 * through the hotplug entry's driver_data.
 */
static int ehci_hcd_h20ahb_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource	*res;
	struct usb_hcd	*hcd;
	void __iomem *regs;
	int ret;
	int irq;
	int i;
	struct h20ahb_hcd	*h20ahb;

	if (usb_disabled())
		return -ENODEV;

	/* if (!dev->parent) {
		dev_err(dev, "Missing parent device\n");
		return -ENODEV;
		}*/

	/* For DT boot, get platform data from parent. i.e. usbhshost */
	/*if (dev->of_node) {
		pdata = dev_get_platdata(dev->parent);
		dev->platform_data = pdata;
	}

	if (!pdata) {
		dev_err(dev, "Missing platform data\n");
		return -ENODEV;
		}*/

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "EHCI irq failed\n");
		return -ENODEV;
	}

	res =  platform_get_resource(pdev, IORESOURCE_MEM, 0);
	regs = devm_ioremap_resource(dev, res);
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
	hcd = usb_create_hcd(&ehci_h20ahb_hc_driver, dev,
			dev_name(dev));
	if (!hcd) {
		dev_err(dev, "Failed to create HCD\n");
		return -ENOMEM;
	}

	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_size(res);
	hcd->regs = regs;
	hcd_to_ehci(hcd)->caps = regs;

	h20ahb = (struct h20ahb_hcd *)hcd_to_ehci(hcd)->priv;
	h20ahb->nports = 1;

	platform_set_drvdata(pdev, hcd);

	/* get the PHY devices if needed */
	for (i = 0 ; i < h20ahb->nports ; i++) {
		struct usb_phy *phy;

		/* get the PHY device */
#if 0
		if (dev->of_node)
			phy = devm_usb_get_phy_by_phandle(dev, "phys", i);
		else
			phy = devm_usb_get_phy_dev(dev, i);
#endif
		phy = otg_ulpi_create(&ehci_h20ahb_phy_io_ops, 0);
		if (IS_ERR(phy)) {
			ret = PTR_ERR(phy);
			dev_err(dev, "Can't get PHY device for port %d: %d\n",
					i, ret);
			goto err_phy;
		}
		phy->dev = dev;
		usb_add_phy_dev(phy);

		h20ahb->phy[i] = phy;
		phy->io_priv = hcd->regs + EHCI_INSNREG05_ULPI;

#if 0
		usb_phy_init(h20ahb->phy[i]);
		/* bring PHY out of suspend */
		usb_phy_set_suspend(h20ahb->phy[i], 0);
#endif
	}

	/* make the first port's phy the one used by hcd as well */
	hcd->phy = h20ahb->phy[0];

	pm_runtime_enable(dev);
	pm_runtime_get_sync(dev);

	/*
	 * An undocumented "feature" in the H20AHB EHCI controller,
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
	for (i = 0; i < h20ahb->nports; i++) {
		usb_phy_init(h20ahb->phy[i]);
		/* bring PHY out of suspend */
		usb_phy_set_suspend(h20ahb->phy[i], 0);
	}

	return 0;

err_pm_runtime:
	pm_runtime_put_sync(dev);

err_phy:
	for (i = 0; i < h20ahb->nports; i++) {
		if (h20ahb->phy[i])
			usb_phy_shutdown(h20ahb->phy[i]);
	}

	usb_put_hcd(hcd);

	return ret;
}


/**
 * ehci_hcd_h20ahb_remove - shutdown processing for EHCI HCDs
 * @pdev: USB Host Controller being removed
 *
 * Reverses the effect of usb_ehci_hcd_h20ahb_probe(), first invoking
 * the HCD's stop() method.  It is always called from a thread
 * context, normally "rmmod", "apmd", or something similar.
 */
static int ehci_hcd_h20ahb_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct h20ahb_hcd *h20ahb = (struct h20ahb_hcd *)hcd_to_ehci(hcd)->priv;
	int i;

	usb_remove_hcd(hcd);

	for (i = 0; i < h20ahb->nports; i++) {
		if (h20ahb->phy[i])
			usb_phy_shutdown(h20ahb->phy[i]);
	}

	usb_put_hcd(hcd);
	pm_runtime_put_sync(dev);
	pm_runtime_disable(dev);

	return 0;
}

static const struct of_device_id h20ahb_ehci_dt_ids[] = {
	{ .compatible = "snps,ehci-h20ahb" },
	{ }
};

MODULE_DEVICE_TABLE(of, h20ahb_ehci_dt_ids);

static struct platform_driver ehci_hcd_h20ahb_driver = {
	.probe			= ehci_hcd_h20ahb_probe,
	.remove			= ehci_hcd_h20ahb_remove,
	.shutdown		= usb_hcd_platform_shutdown,
	/*.suspend		= ehci_hcd_h20ahb_suspend, */
	/*.resume		= ehci_hcd_h20ahb_resume, */
	.driver = {
		.name		= hcd_name,
		.of_match_table = h20ahb_ehci_dt_ids,
	}
};

/*-------------------------------------------------------------------------*/

static int __init ehci_h20ahb_init(void)
{
	if (usb_disabled())
		return -ENODEV;

	pr_info("%s: " DRIVER_DESC "\n", hcd_name);

	ehci_init_driver(&ehci_h20ahb_hc_driver, &ehci_h20ahb_overrides);
	return platform_driver_register(&ehci_hcd_h20ahb_driver);
}
module_init(ehci_h20ahb_init);

static void __exit ehci_h20ahb_cleanup(void)
{
	platform_driver_unregister(&ehci_hcd_h20ahb_driver);
}
module_exit(ehci_h20ahb_cleanup);

MODULE_ALIAS("platform:ehci-h20ahb");
MODULE_AUTHOR("Liviu Dudau <Liviu.Dudau@arm.com>");

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
