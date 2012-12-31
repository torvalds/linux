/* xhci-exynos.c - Driver for USB HOST on Samsung EXYNOS platform device
 *
 * Bus Glue for SAMSUNG EXYNOS USB HOST xHCI Controller
 * xHCI host controller driver
 *
 * Copyright (C) 2008 Intel Corp.
 * Copyright (C) 2011 Samsung Electronics Co.Ltd
 *
 * Author: Yulgon Kim <yulgon.kim@samsung.com>
 *
 * Based on "xhci-pci.c" by Sarah Sharp
 * Modified for SAMSUNG EXYNOS XHCI by Yulgon Kim <yulgon.kim@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/clk.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/dma-mapping.h>
#include <linux/usb/exynos_usb3_drd.h>
#include <linux/platform_data/exynos_usb3_drd.h>

#include "xhci.h"

struct exynos_xhci_hcd {
	struct device *dev;
	struct usb_hcd *hcd;
	struct clk *clk;
	int irq;
};

struct xhci_hcd *exynos_xhci_dbg;

static const char hcd_name[] = "xhci_hcd";

static inline void __orr32(void __iomem *ptr, u32 val)
{
	writel(readl(ptr) | val, ptr);
}

static inline void __bic32(void __iomem *ptr, u32 val)
{
	writel(readl(ptr) & ~val, ptr);
}

static u32 exynos_xhci_change_mode(struct usb_hcd *hcd)
{
	u32 gctl;

	gctl = readl(hcd->regs + EXYNOS_USB3_GCTL);
	gctl &= ~(EXYNOS_USB3_GCTL_PrtCapDir_MASK |
		EXYNOS_USB3_GCTL_FRMSCLDWN_MASK |
		EXYNOS_USB3_GCTL_RAMClkSel_MASK);
	gctl |= (EXYNOS_USB3_GCTL_FRMSCLDWN(0x1e85) | /* Power Down Scale */
		EXYNOS_USB3_GCTL_PrtCapDir(0x1) | /* 0x1 : Host 0x2 : Device */
		EXYNOS_USB3_GCTL_PrtCapDir(0x1) | /* 0x1 : Host 0x2 : Device */
		EXYNOS_USB3_GCTL_RAMClkSel(0x2) | /* Ram Clock Select */
		EXYNOS_USB3_GCTL_DisScramble);

	writel(gctl, hcd->regs + EXYNOS_USB3_GCTL);

	printk(KERN_INFO "Change xHCI host mode %x\n", gctl);

	return gctl;
}

static void exynos_xhci_phy_set(struct platform_device *pdev)
{
	struct exynos_usb3_drd_pdata *pdata = pdev->dev.platform_data;
	struct exynos_xhci_hcd *exynos_xhci = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = exynos_xhci->hcd;
	/* The reset values:
	 *	GUSB2PHYCFG(0)	= 0x00002400
	 *	GUSB3PIPECTL(0)	= 0x00260002
	 */

	__orr32(hcd->regs + EXYNOS_USB3_GCTL, EXYNOS_USB3_GCTL_CoreSoftReset);
	__orr32(hcd->regs + EXYNOS_USB3_GUSB2PHYCFG(0),
			    EXYNOS_USB3_GUSB2PHYCFGx_PHYSoftRst);
	__orr32(hcd->regs + EXYNOS_USB3_GUSB3PIPECTL(0),
			    EXYNOS_USB3_GUSB3PIPECTLx_PHYSoftRst);

	/* PHY initialization */
	if (pdata && pdata->phy_init)
		pdata->phy_init(pdev, pdata->phy_type);

	__bic32(hcd->regs + EXYNOS_USB3_GUSB2PHYCFG(0),
			    EXYNOS_USB3_GUSB2PHYCFGx_PHYSoftRst);
	__bic32(hcd->regs + EXYNOS_USB3_GUSB3PIPECTL(0),
			    EXYNOS_USB3_GUSB3PIPECTLx_PHYSoftRst);
	__bic32(hcd->regs + EXYNOS_USB3_GCTL, EXYNOS_USB3_GCTL_CoreSoftReset);


	__bic32(hcd->regs + EXYNOS_USB3_GUSB2PHYCFG(0),
		EXYNOS_USB3_GUSB2PHYCFGx_SusPHY |
		EXYNOS_USB3_GUSB2PHYCFGx_EnblSlpM |
		EXYNOS_USB3_GUSB2PHYCFGx_USBTrdTim_MASK);
	__orr32(hcd->regs + EXYNOS_USB3_GUSB2PHYCFG(0),
		EXYNOS_USB3_GUSB2PHYCFGx_USBTrdTim(9));

	__bic32(hcd->regs + EXYNOS_USB3_GUSB3PIPECTL(0),
			    EXYNOS_USB3_GUSB3PIPECTLx_SuspSSPhy);

	dev_dbg(exynos_xhci->dev, "GUSB2PHYCFG(0)=0x%08x, GUSB3PIPECTL(0)=0x%08x",
		readl(hcd->regs + EXYNOS_USB3_GUSB2PHYCFG(0)),
		readl(hcd->regs + EXYNOS_USB3_GUSB3PIPECTL(0)));

	/* Global core init */
	writel(EXYNOS_USB3_GSBUSCFG0_INCR16BrstEna |
		EXYNOS_USB3_GSBUSCFG0_INCR8BrstEna |
		EXYNOS_USB3_GSBUSCFG0_INCR4BrstEna,
		hcd->regs + EXYNOS_USB3_GSBUSCFG0);

	writel(EXYNOS_USB3_GSBUSCFG1_BREQLIMIT(0x3),
		hcd->regs + EXYNOS_USB3_GSBUSCFG1);

	writel(0x0, hcd->regs + EXYNOS_USB3_GTXTHRCFG);
	writel(0x0, hcd->regs + EXYNOS_USB3_GRXTHRCFG);
}

static void exynos_xhci_phy_unset(struct platform_device *pdev)
{
	struct exynos_usb3_drd_pdata *pdata = pdev->dev.platform_data;
	struct exynos_xhci_hcd *exynos_xhci = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = exynos_xhci->hcd;

	__orr32(hcd->regs + EXYNOS_USB3_GUSB2PHYCFG(0),
		EXYNOS_USB3_GUSB2PHYCFGx_SusPHY |
		EXYNOS_USB3_GUSB2PHYCFGx_EnblSlpM);

	__orr32(hcd->regs + EXYNOS_USB3_GUSB3PIPECTL(0),
			    EXYNOS_USB3_GUSB3PIPECTLx_SuspSSPhy);

	dev_dbg(exynos_xhci->dev, "GUSB2PHYCFG(0)=0x%08x, GUSB3PIPECTL(0)=0x%08x",
		readl(hcd->regs + EXYNOS_USB3_GUSB2PHYCFG(0)),
		readl(hcd->regs + EXYNOS_USB3_GUSB3PIPECTL(0)));

	/* PHY shutdown */
	if (pdata && pdata->phy_exit)
		pdata->phy_exit(pdev, pdata->phy_type);
}

#ifdef CONFIG_PM
static int exynos_xhci_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct exynos_xhci_hcd	*exynos_xhci;
	struct usb_hcd		*hcd;
	struct xhci_hcd		*xhci;
	int			retval = 0;

	exynos_xhci = dev_get_drvdata(dev);
	if (!exynos_xhci)
		return -EINVAL;

	hcd = exynos_xhci->hcd;
	if (!hcd)
		return -EINVAL;

	xhci = hcd_to_xhci(hcd);

	if (hcd->state != HC_STATE_SUSPENDED ||
			xhci->shared_hcd->state != HC_STATE_SUSPENDED)
		return -EINVAL;

	retval = xhci_suspend(xhci);

	exynos_xhci_phy_unset(pdev);
	clk_disable(exynos_xhci->clk);

	return retval;
}

static int exynos_xhci_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct exynos_xhci_hcd	*exynos_xhci;
	struct usb_hcd		*hcd;
	struct xhci_hcd		*xhci;
	int			retval = 0;

	exynos_xhci = dev_get_drvdata(dev);
	if (!exynos_xhci)
		return -EINVAL;

	hcd = exynos_xhci->hcd;
	if (!hcd)
		return -EINVAL;

	clk_enable(exynos_xhci->clk);
	exynos_xhci_phy_set(pdev);

	exynos_xhci_change_mode(hcd);

	xhci = hcd_to_xhci(hcd);
	retval = xhci_resume(xhci, 0);

	return retval;
}
#else
#define exynos_xhci_suspend	NULL
#define exynos_xhci_resume	NULL
#endif

#ifdef CONFIG_USB_SUSPEND
static int exynos_xhci_runtime_suspend(struct device *dev)
{
	return 0;
}

static int exynos_xhci_runtime_resume(struct device *dev)
{
	return 0;
}
#else
#define exynos_xhci_runtime_suspend	NULL
#define exynos_xhci_runtime_resume	NULL
#endif

static void exynos_xhci_quirks(struct device *dev, struct xhci_hcd *xhci)
{
	/* Don't use MSI interrupt */
	xhci->quirks |= XHCI_BROKEN_MSI;
}

/* called during probe() after chip reset completes */
static int exynos_xhci_setup(struct usb_hcd *hcd)
{
	struct xhci_hcd		*xhci;
	int			retval;

	retval = xhci_gen_setup(hcd, exynos_xhci_quirks);
	if (retval)
		return retval;

	xhci = hcd_to_xhci(hcd);
	if (!usb_hcd_is_primary_hcd(hcd))
		return 0;

	xhci->sbrn = HCD_USB3;
	xhci_dbg(xhci, "Got SBRN %u\n", (unsigned int) xhci->sbrn);

	return retval;
}


static const struct hc_driver exynos_xhci_hc_driver = {
	.description		= hcd_name,
	.product_desc		= "EXYNOS xHCI Host Controller",
	.hcd_priv_size		= sizeof(struct xhci_hcd *),

	/*
	 * generic hardware linkage
	 */
	.irq			= xhci_irq,
	.flags			= HCD_MEMORY | HCD_USB3 | HCD_SHARED,

	/*
	 * basic lifecycle operations
	 */
	.reset			= exynos_xhci_setup,
	.start			= xhci_run,
	.stop			= xhci_stop,
	.shutdown		= xhci_shutdown,

	/*
	 * managing i/o requests and associated device resources
	 */
	.urb_enqueue		= xhci_urb_enqueue,
	.urb_dequeue		= xhci_urb_dequeue,
	.alloc_dev		= xhci_alloc_dev,
	.free_dev		= xhci_free_dev,
	.alloc_streams		= xhci_alloc_streams,
	.free_streams		= xhci_free_streams,
	.add_endpoint		= xhci_add_endpoint,
	.drop_endpoint		= xhci_drop_endpoint,
	.endpoint_reset		= xhci_endpoint_reset,
	.check_bandwidth	= xhci_check_bandwidth,
	.reset_bandwidth	= xhci_reset_bandwidth,
	.address_device		= xhci_address_device,
	.update_hub_device	= xhci_update_hub_device,
	.reset_device		= xhci_discover_or_reset_device,

	/*
	 * scheduling support
	 */
	.get_frame_number	= xhci_get_frame,

	/* Root hub support */
	.hub_control		= xhci_hub_control,
	.hub_status_data	= xhci_hub_status_data,
	.bus_suspend		= xhci_bus_suspend,
	.bus_resume		= xhci_bus_resume,
};

static int usb_hcd_exynos_probe(struct platform_device *pdev, const struct hc_driver *driver)
{
	struct exynos_xhci_hcd *exynos_xhci;
	struct exynos_xhci_plat *pdata;
	struct usb_hcd		*hcd;
	struct resource *res;
	int err;

	if (usb_disabled())
		return -ENODEV;

	if (!driver)
		return -EINVAL;

	pdata = pdev->dev.platform_data;
	if (!pdata) {
		dev_err(&pdev->dev, "No platform data defined\n");
		return -EINVAL;
	}

	exynos_xhci = kzalloc(sizeof(struct exynos_xhci_hcd), GFP_KERNEL);
	if (!exynos_xhci)
		return -ENOMEM;

	hcd = usb_create_hcd(driver, &pdev->dev, dev_name(&pdev->dev));
	if (!hcd) {
		err = -ENOMEM;
		goto fail_hcd;
	}


	exynos_xhci->clk = clk_get(&pdev->dev, "usbdrd30");
	if (IS_ERR(exynos_xhci->clk)) {
		dev_err(&pdev->dev, "Failed to get usbhost clock\n");
		err = PTR_ERR(exynos_xhci->clk);
		goto fail_clk;
	}

	err = clk_enable(exynos_xhci->clk);
	if (err)
		goto fail_clken;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "Failed to get I/O memory\n");
		err = -ENXIO;
		goto fail_io;
	}

	/* EHCI, OHCI */
	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_size(res);
	hcd->regs = ioremap(res->start, resource_size(res));
	if (!hcd->regs) {
		dev_err(&pdev->dev, "Failed to remap I/O memory\n");
		err = -ENOMEM;
		goto fail_io;
	}

	exynos_xhci->irq = platform_get_irq(pdev, 0);
	if (!exynos_xhci->irq) {
		dev_err(&pdev->dev, "Failed to get IRQ\n");
		err = -ENODEV;
		goto fail;
	}

	exynos_xhci->dev = &pdev->dev;
	exynos_xhci->hcd = hcd;
	platform_set_drvdata(pdev, exynos_xhci);

	exynos_xhci_phy_set(pdev);
	exynos_xhci_change_mode(hcd);

	err = usb_add_hcd(hcd, exynos_xhci->irq, IRQF_DISABLED | IRQF_SHARED);
	if (err) {
		dev_err(&pdev->dev, "Failed to add USB HCD\n");
		goto fail;
	}

	return err;

fail:
	iounmap(hcd->regs);
fail_io:
	clk_disable(exynos_xhci->clk);
fail_clken:
	clk_put(exynos_xhci->clk);
fail_clk:
	usb_put_hcd(hcd);
fail_hcd:
	kfree(exynos_xhci);
	return err;
}

void usb_hcd_exynos_remove(struct platform_device *pdev)
{
	struct exynos_xhci_hcd *exynos_xhci;
	struct usb_hcd		*hcd;

	exynos_xhci = dev_get_drvdata(&pdev->dev);
	hcd = exynos_xhci->hcd;
	if (!hcd)
		return;

	/* Fake an interrupt request in order to give the driver a chance
	 * to test whether the controller hardware has been removed (e.g.,
	 * cardbus physical eject).
	 */
	local_irq_disable();
	usb_hcd_irq(0, hcd);
	local_irq_enable();

	usb_remove_hcd(hcd);

	exynos_xhci_phy_unset(pdev);

	if (hcd->driver->flags & HCD_MEMORY) {
		iounmap(hcd->regs);
		release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
	} else {
		release_region(hcd->rsrc_start, hcd->rsrc_len);
	}
	usb_put_hcd(hcd);

	kfree(exynos_xhci);
	clk_disable(exynos_xhci->clk);
	clk_put(exynos_xhci->clk);
}

static int __devinit exynos_xhci_probe(struct platform_device *pdev)
{
	struct exynos_xhci_hcd *exynos_xhci;
	struct usb_hcd *hcd;
	struct xhci_hcd *xhci;
	int err;

	/* Register the USB 2.0 roothub.
	 * FIXME: USB core must know to register the USB 2.0 roothub first.
	 * This is sort of silly, because we could just set the HCD driver flags
	 * to say USB 2.0, but I'm not sure what the implications would be in
	 * the other parts of the HCD code.
	 */
	err = usb_hcd_exynos_probe(pdev, &exynos_xhci_hc_driver);

	if (err)
		return err;

	exynos_xhci = dev_get_drvdata(&pdev->dev);
	hcd = exynos_xhci->hcd;
	xhci = hcd_to_xhci(hcd);
	xhci->shared_hcd = usb_create_shared_hcd(&exynos_xhci_hc_driver,
				&pdev->dev, dev_name(&pdev->dev), hcd);
	if (!hcd) {
		dev_err(&pdev->dev, "Unable to create HCD\n");
		err = -ENOMEM;
		goto dealloc_usb2_hcd;
	}

	exynos_xhci_dbg = xhci;
	/* Set the xHCI pointer before exynos_xhci_setup() (aka hcd_driver.reset)
	 * is called by usb_add_hcd().
	 */
	*((struct xhci_hcd **) xhci->shared_hcd->hcd_priv) = xhci;

	err = usb_add_hcd(xhci->shared_hcd, exynos_xhci->irq,
			IRQF_DISABLED | IRQF_SHARED);
	if (err)
		goto put_usb3_hcd;

	/* Roothub already marked as USB 3.0 speed */
	return err;

put_usb3_hcd:
	usb_put_hcd(xhci->shared_hcd);
dealloc_usb2_hcd:
	usb_remove_hcd(hcd);
	usb_hcd_exynos_remove(pdev);

	return err;
}

static int __devexit exynos_xhci_remove(struct platform_device *pdev)
{
	struct exynos_xhci_hcd *exynos_xhci = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = exynos_xhci->hcd;
	struct xhci_hcd *xhci;

	xhci = hcd_to_xhci(hcd);
	if (xhci->shared_hcd) {
		usb_remove_hcd(xhci->shared_hcd);
		usb_put_hcd(xhci->shared_hcd);
	}
	usb_remove_hcd(hcd);

	usb_hcd_exynos_remove(pdev);
	kfree(xhci);

	return 0;
}

static void exynos_xhci_shutdown(struct platform_device *pdev)
{
	struct exynos_xhci_hcd *s5p_xhci = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = s5p_xhci->hcd;

	if (hcd->driver->shutdown)
		hcd->driver->shutdown(hcd);
}

static const struct dev_pm_ops exynos_xhci_pm_ops = {
	.suspend		= exynos_xhci_suspend,
	.resume			= exynos_xhci_resume,
	.runtime_suspend	= exynos_xhci_runtime_suspend,
	.runtime_resume		= exynos_xhci_runtime_resume,
};

static struct platform_driver exynos_xhci_driver = {
	.probe		= exynos_xhci_probe,
	.remove		= __devexit_p(exynos_xhci_remove),
	.shutdown	= exynos_xhci_shutdown,
	.driver = {
		.name	= "exynos-xhci",
		.owner	= THIS_MODULE,
		.pm = &exynos_xhci_pm_ops,
	}
};

int xhci_register_exynos(void)
{
	return platform_driver_register(&exynos_xhci_driver);
}

void xhci_unregister_exynos(void)
{
	platform_driver_unregister(&exynos_xhci_driver);
}

MODULE_ALIAS("platform:exynos-xhci");
