/*
 * OHCI HCD(Host Controller Driver) for USB.
 *
 *(C) Copyright 1999 Roman Weissgaerber <weissg@vienna.at>
 *(C) Copyright 2000-2002 David Brownell <dbrownell@users.sourceforge.net>
 *(C) Copyright 2002 Hewlett-Packard Company
 *
 * Bus glue for Toshiba Mobile IO(TMIO) Controller's OHCI core
 * (C) Copyright 2005 Chris Humbert <mahadri-usb@drigon.com>
 * (C) Copyright 2007, 2008 Dmitry Baryshkov <dbaryshkov@gmail.com>
 *
 * This is known to work with the following variants:
 *	TC6393XB revision 3	(32kB SRAM)
 *
 * The TMIO's OHCI core DMAs through a small internal buffer that
 * is directly addressable by the CPU.
 *
 * Written from sparse documentation from Toshiba and Sharp's driver
 * for the 2.4 kernel,
 *	usb-ohci-tc6393.c(C) Copyright 2004 Lineo Solutions, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/pagemap.h>
#include <linux/namei.h>
#include <linux/sched.h>*/
#include <linux/platform_device.h>
#include <linux/mfd/core.h>
#include <linux/mfd/tmio.h>
#include <linux/dma-mapping.h>

/*-------------------------------------------------------------------------*/

/*
 * USB Host Controller Configuration Register
 */
#define CCR_REVID	0x08	/* b Revision ID				*/
#define CCR_BASE	0x10	/* l USB Control Register Base Address Low	*/
#define CCR_ILME	0x40	/* b Internal Local Memory Enable		*/
#define CCR_PM		0x4c	/* w Power Management			*/
#define CCR_INTC	0x50	/* b INT Control				*/
#define CCR_LMW1L	0x54	/* w Local Memory Window 1 LMADRS Low	*/
#define CCR_LMW1H	0x56	/* w Local Memory Window 1 LMADRS High	*/
#define CCR_LMW1BL	0x58	/* w Local Memory Window 1 Base Address Low	*/
#define CCR_LMW1BH	0x5A	/* w Local Memory Window 1 Base Address High	*/
#define CCR_LMW2L	0x5C	/* w Local Memory Window 2 LMADRS Low	*/
#define CCR_LMW2H	0x5E	/* w Local Memory Window 2 LMADRS High	*/
#define CCR_LMW2BL	0x60	/* w Local Memory Window 2 Base Address Low	*/
#define CCR_LMW2BH	0x62	/* w Local Memory Window 2 Base Address High	*/
#define CCR_MISC	0xFC	/* b MISC					*/

#define CCR_PM_GKEN      0x0001
#define CCR_PM_CKRNEN    0x0002
#define CCR_PM_USBPW1    0x0004
#define CCR_PM_USBPW2    0x0008
#define CCR_PM_USBPW3    0x0010
#define CCR_PM_PMEE      0x0100
#define CCR_PM_PMES      0x8000

/*-------------------------------------------------------------------------*/

struct tmio_hcd {
	void __iomem		*ccr;
	spinlock_t		lock; /* protects RMW cycles */
};

#define hcd_to_tmio(hcd)	((struct tmio_hcd *)(hcd_to_ohci(hcd) + 1))

/*-------------------------------------------------------------------------*/

static void tmio_write_pm(struct platform_device *dev)
{
	struct usb_hcd *hcd = platform_get_drvdata(dev);
	struct tmio_hcd *tmio = hcd_to_tmio(hcd);
	u16 pm;
	unsigned long flags;

	spin_lock_irqsave(&tmio->lock, flags);

	pm = CCR_PM_GKEN | CCR_PM_CKRNEN |
	     CCR_PM_PMEE | CCR_PM_PMES;

	tmio_iowrite16(pm, tmio->ccr + CCR_PM);
	spin_unlock_irqrestore(&tmio->lock, flags);
}

static void tmio_stop_hc(struct platform_device *dev)
{
	struct usb_hcd *hcd = platform_get_drvdata(dev);
	struct ohci_hcd *ohci = hcd_to_ohci(hcd);
	struct tmio_hcd *tmio = hcd_to_tmio(hcd);
	u16 pm;

	pm = CCR_PM_GKEN | CCR_PM_CKRNEN;
	switch (ohci->num_ports) {
		default:
			dev_err(&dev->dev, "Unsupported amount of ports: %d\n", ohci->num_ports);
		case 3:
			pm |= CCR_PM_USBPW3;
		case 2:
			pm |= CCR_PM_USBPW2;
		case 1:
			pm |= CCR_PM_USBPW1;
	}
	tmio_iowrite8(0, tmio->ccr + CCR_INTC);
	tmio_iowrite8(0, tmio->ccr + CCR_ILME);
	tmio_iowrite16(0, tmio->ccr + CCR_BASE);
	tmio_iowrite16(0, tmio->ccr + CCR_BASE + 2);
	tmio_iowrite16(pm, tmio->ccr + CCR_PM);
}

static void tmio_start_hc(struct platform_device *dev)
{
	struct usb_hcd *hcd = platform_get_drvdata(dev);
	struct tmio_hcd *tmio = hcd_to_tmio(hcd);
	unsigned long base = hcd->rsrc_start;

	tmio_write_pm(dev);
	tmio_iowrite16(base, tmio->ccr + CCR_BASE);
	tmio_iowrite16(base >> 16, tmio->ccr + CCR_BASE + 2);
	tmio_iowrite8(1, tmio->ccr + CCR_ILME);
	tmio_iowrite8(2, tmio->ccr + CCR_INTC);

	dev_info(&dev->dev, "revision %d @ 0x%08llx, irq %d\n",
			tmio_ioread8(tmio->ccr + CCR_REVID),
			(u64) hcd->rsrc_start, hcd->irq);
}

static int ohci_tmio_start(struct usb_hcd *hcd)
{
	struct ohci_hcd *ohci = hcd_to_ohci(hcd);
	int ret;

	if ((ret = ohci_init(ohci)) < 0)
		return ret;

	if ((ret = ohci_run(ohci)) < 0) {
		dev_err(hcd->self.controller, "can't start %s\n",
			hcd->self.bus_name);
		ohci_stop(hcd);
		return ret;
	}

	return 0;
}

static const struct hc_driver ohci_tmio_hc_driver = {
	.description =		hcd_name,
	.product_desc =		"TMIO OHCI USB Host Controller",
	.hcd_priv_size =	sizeof(struct ohci_hcd) + sizeof (struct tmio_hcd),

	/* generic hardware linkage */
	.irq =			ohci_irq,
	.flags =		HCD_USB11 | HCD_MEMORY | HCD_LOCAL_MEM,

	/* basic lifecycle operations */
	.start =		ohci_tmio_start,
	.stop =			ohci_stop,
	.shutdown =		ohci_shutdown,

	/* managing i/o requests and associated device resources */
	.urb_enqueue =		ohci_urb_enqueue,
	.urb_dequeue =		ohci_urb_dequeue,
	.endpoint_disable =	ohci_endpoint_disable,

	/* scheduling support */
	.get_frame_number =	ohci_get_frame,

	/* root hub support */
	.hub_status_data =	ohci_hub_status_data,
	.hub_control =		ohci_hub_control,
#ifdef	CONFIG_PM
	.bus_suspend =		ohci_bus_suspend,
	.bus_resume =		ohci_bus_resume,
#endif
	.start_port_reset =	ohci_start_port_reset,
};

/*-------------------------------------------------------------------------*/
static struct platform_driver ohci_hcd_tmio_driver;

static int ohci_hcd_tmio_drv_probe(struct platform_device *dev)
{
	const struct mfd_cell *cell = mfd_get_cell(dev);
	struct resource *regs = platform_get_resource(dev, IORESOURCE_MEM, 0);
	struct resource *config = platform_get_resource(dev, IORESOURCE_MEM, 1);
	struct resource *sram = platform_get_resource(dev, IORESOURCE_MEM, 2);
	int irq = platform_get_irq(dev, 0);
	struct tmio_hcd *tmio;
	struct ohci_hcd *ohci;
	struct usb_hcd *hcd;
	int ret;

	if (usb_disabled())
		return -ENODEV;

	if (!cell)
		return -EINVAL;

	hcd = usb_create_hcd(&ohci_tmio_hc_driver, &dev->dev, dev_name(&dev->dev));
	if (!hcd) {
		ret = -ENOMEM;
		goto err_usb_create_hcd;
	}

	hcd->rsrc_start = regs->start;
	hcd->rsrc_len = resource_size(regs);

	tmio = hcd_to_tmio(hcd);

	spin_lock_init(&tmio->lock);

	tmio->ccr = ioremap(config->start, resource_size(config));
	if (!tmio->ccr) {
		ret = -ENOMEM;
		goto err_ioremap_ccr;
	}

	hcd->regs = ioremap(hcd->rsrc_start, hcd->rsrc_len);
	if (!hcd->regs) {
		ret = -ENOMEM;
		goto err_ioremap_regs;
	}

	ret = dma_declare_coherent_memory(&dev->dev, sram->start, sram->start,
				resource_size(sram), DMA_MEMORY_EXCLUSIVE);
	if (ret)
		goto err_dma_declare;

	if (cell->enable) {
		ret = cell->enable(dev);
		if (ret)
			goto err_enable;
	}

	tmio_start_hc(dev);
	ohci = hcd_to_ohci(hcd);
	ohci_hcd_init(ohci);

	ret = usb_add_hcd(hcd, irq, 0);
	if (ret)
		goto err_add_hcd;

	device_wakeup_enable(hcd->self.controller);
	if (ret == 0)
		return ret;

	usb_remove_hcd(hcd);

err_add_hcd:
	tmio_stop_hc(dev);
	if (cell->disable)
		cell->disable(dev);
err_enable:
	dma_release_declared_memory(&dev->dev);
err_dma_declare:
	iounmap(hcd->regs);
err_ioremap_regs:
	iounmap(tmio->ccr);
err_ioremap_ccr:
	usb_put_hcd(hcd);
err_usb_create_hcd:

	return ret;
}

static int ohci_hcd_tmio_drv_remove(struct platform_device *dev)
{
	struct usb_hcd *hcd = platform_get_drvdata(dev);
	struct tmio_hcd *tmio = hcd_to_tmio(hcd);
	const struct mfd_cell *cell = mfd_get_cell(dev);

	usb_remove_hcd(hcd);
	tmio_stop_hc(dev);
	if (cell->disable)
		cell->disable(dev);
	dma_release_declared_memory(&dev->dev);
	iounmap(hcd->regs);
	iounmap(tmio->ccr);
	usb_put_hcd(hcd);

	return 0;
}

#ifdef CONFIG_PM
static int ohci_hcd_tmio_drv_suspend(struct platform_device *dev, pm_message_t state)
{
	const struct mfd_cell *cell = mfd_get_cell(dev);
	struct usb_hcd *hcd = platform_get_drvdata(dev);
	struct ohci_hcd *ohci = hcd_to_ohci(hcd);
	struct tmio_hcd *tmio = hcd_to_tmio(hcd);
	unsigned long flags;
	u8 misc;
	int ret;

	if (time_before(jiffies, ohci->next_statechange))
		msleep(5);
	ohci->next_statechange = jiffies;

	spin_lock_irqsave(&tmio->lock, flags);

	misc = tmio_ioread8(tmio->ccr + CCR_MISC);
	misc |= 1 << 3; /* USSUSP */
	tmio_iowrite8(misc, tmio->ccr + CCR_MISC);

	spin_unlock_irqrestore(&tmio->lock, flags);

	if (cell->suspend) {
		ret = cell->suspend(dev);
		if (ret)
			return ret;
	}
	return 0;
}

static int ohci_hcd_tmio_drv_resume(struct platform_device *dev)
{
	const struct mfd_cell *cell = mfd_get_cell(dev);
	struct usb_hcd *hcd = platform_get_drvdata(dev);
	struct ohci_hcd *ohci = hcd_to_ohci(hcd);
	struct tmio_hcd *tmio = hcd_to_tmio(hcd);
	unsigned long flags;
	u8 misc;
	int ret;

	if (time_before(jiffies, ohci->next_statechange))
		msleep(5);
	ohci->next_statechange = jiffies;

	if (cell->resume) {
		ret = cell->resume(dev);
		if (ret)
			return ret;
	}

	tmio_start_hc(dev);

	spin_lock_irqsave(&tmio->lock, flags);

	misc = tmio_ioread8(tmio->ccr + CCR_MISC);
	misc &= ~(1 << 3); /* USSUSP */
	tmio_iowrite8(misc, tmio->ccr + CCR_MISC);

	spin_unlock_irqrestore(&tmio->lock, flags);

	ohci_resume(hcd, false);

	return 0;
}
#else
#define ohci_hcd_tmio_drv_suspend NULL
#define ohci_hcd_tmio_drv_resume NULL
#endif

static struct platform_driver ohci_hcd_tmio_driver = {
	.probe		= ohci_hcd_tmio_drv_probe,
	.remove		= ohci_hcd_tmio_drv_remove,
	.shutdown	= usb_hcd_platform_shutdown,
	.suspend	= ohci_hcd_tmio_drv_suspend,
	.resume		= ohci_hcd_tmio_drv_resume,
	.driver		= {
		.name	= "tmio-ohci",
	},
};
