/*
 * OHCI HCD (Host Controller Driver) for USB.
 *
 * (C) Copyright 1999 Roman Weissgaerber <weissg@vienna.at>
 * (C) Copyright 2000-2002 David Brownell <dbrownell@users.sourceforge.net>
 * (C) Copyright 2002 Hewlett-Packard Company
 *
 * Bus Glue for AMD Alchemy Au1xxx
 *
 * Written by Christopher Hoover <ch@hpl.hp.com>
 * Based on fragments of previous driver by Russell King et al.
 *
 * Modified for LH7A404 from ohci-sa1111.c
 *  by Durgesh Pattamatta <pattamattad@sharpsec.com>
 * Modified for AMD Alchemy Au1xxx
 *  by Matt Porter <mporter@kernel.crashing.org>
 *
 * This file is licenced under the GPL.
 */

#include <linux/platform_device.h>
#include <linux/signal.h>

#include <asm/mach-au1x00/au1000.h>

#ifndef	CONFIG_SOC_AU1200

#define USBH_ENABLE_BE (1<<0)
#define USBH_ENABLE_C  (1<<1)
#define USBH_ENABLE_E  (1<<2)
#define USBH_ENABLE_CE (1<<3)
#define USBH_ENABLE_RD (1<<4)

#ifdef __LITTLE_ENDIAN
#define USBH_ENABLE_INIT (USBH_ENABLE_CE | USBH_ENABLE_E | USBH_ENABLE_C)
#elif __BIG_ENDIAN
#define USBH_ENABLE_INIT (USBH_ENABLE_CE | USBH_ENABLE_E | USBH_ENABLE_C | \
			  USBH_ENABLE_BE)
#else
#error not byte order defined
#endif

#else   /* Au1200 */

#define USB_HOST_CONFIG    (USB_MSR_BASE + USB_MSR_MCFG)
#define USB_MCFG_PFEN     (1<<31)
#define USB_MCFG_RDCOMB   (1<<30)
#define USB_MCFG_SSDEN    (1<<23)
#define USB_MCFG_OHCCLKEN (1<<16)
#ifdef CONFIG_DMA_COHERENT
#define USB_MCFG_UCAM     (1<<7)
#else
#define USB_MCFG_UCAM     (0)
#endif
#define USB_MCFG_OBMEN    (1<<1)
#define USB_MCFG_OMEMEN   (1<<0)

#define USBH_ENABLE_CE    USB_MCFG_OHCCLKEN

#define USBH_ENABLE_INIT  (USB_MCFG_PFEN  | USB_MCFG_RDCOMB 	|	\
			   USBH_ENABLE_CE | USB_MCFG_SSDEN	|	\
			   USB_MCFG_UCAM  |				\
			   USB_MCFG_OBMEN | USB_MCFG_OMEMEN)

#define USBH_DISABLE      (USB_MCFG_OBMEN | USB_MCFG_OMEMEN)

#endif  /* Au1200 */

extern int usb_disabled(void);

static void au1xxx_start_ohc(void)
{
	/* enable host controller */
#ifndef CONFIG_SOC_AU1200
	au_writel(USBH_ENABLE_CE, USB_HOST_CONFIG);
	au_sync();
	udelay(1000);

	au_writel(au_readl(USB_HOST_CONFIG) | USBH_ENABLE_INIT, USB_HOST_CONFIG);
	au_sync();
	udelay(1000);

	/* wait for reset complete (read register twice; see au1500 errata) */
	while (au_readl(USB_HOST_CONFIG),
		!(au_readl(USB_HOST_CONFIG) & USBH_ENABLE_RD))
		udelay(1000);

#else   /* Au1200 */
	au_writel(au_readl(USB_HOST_CONFIG) | USBH_ENABLE_CE, USB_HOST_CONFIG);
	au_sync();
	udelay(1000);

	au_writel(au_readl(USB_HOST_CONFIG) | USBH_ENABLE_INIT, USB_HOST_CONFIG);
	au_sync();
	udelay(2000);
#endif  /* Au1200 */
}

static void au1xxx_stop_ohc(void)
{
#ifdef CONFIG_SOC_AU1200
	/* Disable mem */
	au_writel(au_readl(USB_HOST_CONFIG) & ~USBH_DISABLE, USB_HOST_CONFIG);
	au_sync();
	udelay(1000);
#endif
	/* Disable clock */
	au_writel(au_readl(USB_HOST_CONFIG) & ~USBH_ENABLE_CE, USB_HOST_CONFIG);
	au_sync();
}

static int __devinit ohci_au1xxx_start(struct usb_hcd *hcd)
{
	struct ohci_hcd	*ohci = hcd_to_ohci(hcd);
	int ret;

	ohci_dbg(ohci, "ohci_au1xxx_start, ohci:%p", ohci);

	if ((ret = ohci_init(ohci)) < 0)
		return ret;

	if ((ret = ohci_run(ohci)) < 0) {
		err ("can't start %s", hcd->self.bus_name);
		ohci_stop(hcd);
		return ret;
	}

	return 0;
}

static const struct hc_driver ohci_au1xxx_hc_driver = {
	.description =		hcd_name,
	.product_desc =		"Au1xxx OHCI",
	.hcd_priv_size =	sizeof(struct ohci_hcd),

	/*
	 * generic hardware linkage
	 */
	.irq =			ohci_irq,
	.flags =		HCD_USB11 | HCD_MEMORY,

	/*
	 * basic lifecycle operations
	 */
	.start =		ohci_au1xxx_start,
	.stop =			ohci_stop,
	.shutdown =		ohci_shutdown,

	/*
	 * managing i/o requests and associated device resources
	 */
	.urb_enqueue =		ohci_urb_enqueue,
	.urb_dequeue =		ohci_urb_dequeue,
	.endpoint_disable =	ohci_endpoint_disable,

	/*
	 * scheduling support
	 */
	.get_frame_number =	ohci_get_frame,

	/*
	 * root hub support
	 */
	.hub_status_data =	ohci_hub_status_data,
	.hub_control =		ohci_hub_control,
#ifdef	CONFIG_PM
	.bus_suspend =		ohci_bus_suspend,
	.bus_resume =		ohci_bus_resume,
#endif
	.start_port_reset =	ohci_start_port_reset,
};

static int ohci_hcd_au1xxx_drv_probe(struct platform_device *pdev)
{
	int ret;
	struct usb_hcd *hcd;

	if (usb_disabled())
		return -ENODEV;

#if defined(CONFIG_SOC_AU1200) && defined(CONFIG_DMA_COHERENT)
	/* Au1200 AB USB does not support coherent memory */
	if (!(read_c0_prid() & 0xff)) {
		printk(KERN_INFO "%s: this is chip revision AB !!\n",
			pdev->name);
		printk(KERN_INFO "%s: update your board or re-configure "
				 "the kernel\n", pdev->name);
		return -ENODEV;
	}
#endif

	if (pdev->resource[1].flags != IORESOURCE_IRQ) {
		pr_debug("resource[1] is not IORESOURCE_IRQ\n");
		return -ENOMEM;
	}

	hcd = usb_create_hcd(&ohci_au1xxx_hc_driver, &pdev->dev, "au1xxx");
	if (!hcd)
		return -ENOMEM;

	hcd->rsrc_start = pdev->resource[0].start;
	hcd->rsrc_len = pdev->resource[0].end - pdev->resource[0].start + 1;

	if (!request_mem_region(hcd->rsrc_start, hcd->rsrc_len, hcd_name)) {
		pr_debug("request_mem_region failed\n");
		ret = -EBUSY;
		goto err1;
	}

	hcd->regs = ioremap(hcd->rsrc_start, hcd->rsrc_len);
	if (!hcd->regs) {
		pr_debug("ioremap failed\n");
		ret = -ENOMEM;
		goto err2;
	}

	au1xxx_start_ohc();
	ohci_hcd_init(hcd_to_ohci(hcd));

	ret = usb_add_hcd(hcd, pdev->resource[1].start,
			  IRQF_DISABLED | IRQF_SHARED);
	if (ret == 0) {
		platform_set_drvdata(pdev, hcd);
		return ret;
	}

	au1xxx_stop_ohc();
	iounmap(hcd->regs);
err2:
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
err1:
	usb_put_hcd(hcd);
	return ret;
}

static int ohci_hcd_au1xxx_drv_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);

	usb_remove_hcd(hcd);
	au1xxx_stop_ohc();
	iounmap(hcd->regs);
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
	usb_put_hcd(hcd);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

#ifdef CONFIG_PM
static int ohci_hcd_au1xxx_drv_suspend(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct ohci_hcd	*ohci = hcd_to_ohci(hcd);
	unsigned long flags;
	int rc;

	rc = 0;

	/* Root hub was already suspended. Disable irq emission and
	 * mark HW unaccessible, bail out if RH has been resumed. Use
	 * the spinlock to properly synchronize with possible pending
	 * RH suspend or resume activity.
	 *
	 * This is still racy as hcd->state is manipulated outside of
	 * any locks =P But that will be a different fix.
	 */
	spin_lock_irqsave(&ohci->lock, flags);
	if (hcd->state != HC_STATE_SUSPENDED) {
		rc = -EINVAL;
		goto bail;
	}
	ohci_writel(ohci, OHCI_INTR_MIE, &ohci->regs->intrdisable);
	(void)ohci_readl(ohci, &ohci->regs->intrdisable);

	clear_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);

	au1xxx_stop_ohc();
bail:
	spin_unlock_irqrestore(&ohci->lock, flags);

	return rc;
}

static int ohci_hcd_au1xxx_drv_resume(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);

	au1xxx_start_ohc();

	set_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);
	ohci_finish_controller_resume(hcd);

	return 0;
}

static const struct dev_pm_ops au1xxx_ohci_pmops = {
	.suspend	= ohci_hcd_au1xxx_drv_suspend,
	.resume		= ohci_hcd_au1xxx_drv_resume,
};

#define AU1XXX_OHCI_PMOPS &au1xxx_ohci_pmops

#else
#define AU1XXX_OHCI_PMOPS NULL
#endif

static struct platform_driver ohci_hcd_au1xxx_driver = {
	.probe		= ohci_hcd_au1xxx_drv_probe,
	.remove		= ohci_hcd_au1xxx_drv_remove,
	.shutdown	= usb_hcd_platform_shutdown,
	.driver		= {
		.name	= "au1xxx-ohci",
		.owner	= THIS_MODULE,
		.pm	= AU1XXX_OHCI_PMOPS,
	},
};

MODULE_ALIAS("platform:au1xxx-ohci");
