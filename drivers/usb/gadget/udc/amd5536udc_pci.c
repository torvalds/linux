/*
 * amd5536udc_pci.c -- AMD 5536 UDC high/full speed USB device controller
 *
 * Copyright (C) 2005-2007 AMD (http://www.amd.com)
 * Author: Thomas Dahlmann
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/*
 * The AMD5536 UDC is part of the x86 southbridge AMD Geode CS5536.
 * It is a USB Highspeed DMA capable USB device controller. Beside ep0 it
 * provides 4 IN and 4 OUT endpoints (bulk or interrupt type).
 *
 * Make sure that UDC is assigned to port 4 by BIOS settings (port can also
 * be used as host port) and UOC bits PAD_EN and APU are set (should be done
 * by BIOS init).
 *
 * UDC DMA requires 32-bit aligned buffers so DMA with gadget ether does not
 * work without updating NET_IP_ALIGN. Or PIO mode (module param "use_dma=0")
 * can be used with gadget ether.
 *
 * This file does pci device registration, and the core driver implementation
 * is done in amd5536udc.c
 *
 * The driver is split so as to use the core UDC driver which is based on
 * Synopsys device controller IP (different than HS OTG IP) in UDCs
 * integrated to SoC platforms.
 *
 */

/* Driver strings */
#define UDC_MOD_DESCRIPTION		"AMD 5536 UDC - USB Device Controller"

/* system */
#include <linux/device.h>
#include <linux/dmapool.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/prefetch.h>
#include <linux/pci.h>

/* udc specific */
#include "amd5536udc.h"

/* pointer to device object */
static struct udc *udc;

/* description */
static const char mod_desc[] = UDC_MOD_DESCRIPTION;
static const char name[] = "amd5536udc-pci";

/* Reset all pci context */
static void udc_pci_remove(struct pci_dev *pdev)
{
	struct udc		*dev;

	dev = pci_get_drvdata(pdev);

	usb_del_gadget_udc(&udc->gadget);
	/* gadget driver must not be registered */
	if (WARN_ON(dev->driver))
		return;

	/* dma pool cleanup */
	free_dma_pools(dev);

	/* reset controller */
	writel(AMD_BIT(UDC_DEVCFG_SOFTRESET), &dev->regs->cfg);
	free_irq(pdev->irq, dev);
	iounmap(dev->virt_addr);
	release_mem_region(pci_resource_start(pdev, 0),
			   pci_resource_len(pdev, 0));
	pci_disable_device(pdev);

	udc_remove(dev);
}

/* Called by pci bus driver to init pci context */
static int udc_pci_probe(
	struct pci_dev *pdev,
	const struct pci_device_id *id
)
{
	struct udc		*dev;
	unsigned long		resource;
	unsigned long		len;
	int			retval = 0;

	/* one udc only */
	if (udc) {
		dev_dbg(&pdev->dev, "already probed\n");
		return -EBUSY;
	}

	/* init */
	dev = kzalloc(sizeof(struct udc), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	/* pci setup */
	if (pci_enable_device(pdev) < 0) {
		retval = -ENODEV;
		goto err_pcidev;
	}

	/* PCI resource allocation */
	resource = pci_resource_start(pdev, 0);
	len = pci_resource_len(pdev, 0);

	if (!request_mem_region(resource, len, name)) {
		dev_dbg(&pdev->dev, "pci device used already\n");
		retval = -EBUSY;
		goto err_memreg;
	}

	dev->virt_addr = ioremap_nocache(resource, len);
	if (!dev->virt_addr) {
		dev_dbg(&pdev->dev, "start address cannot be mapped\n");
		retval = -EFAULT;
		goto err_ioremap;
	}

	if (!pdev->irq) {
		dev_err(&pdev->dev, "irq not set\n");
		retval = -ENODEV;
		goto err_irq;
	}

	spin_lock_init(&dev->lock);
	/* udc csr registers base */
	dev->csr = dev->virt_addr + UDC_CSR_ADDR;
	/* dev registers base */
	dev->regs = dev->virt_addr + UDC_DEVCFG_ADDR;
	/* ep registers base */
	dev->ep_regs = dev->virt_addr + UDC_EPREGS_ADDR;
	/* fifo's base */
	dev->rxfifo = (u32 __iomem *)(dev->virt_addr + UDC_RXFIFO_ADDR);
	dev->txfifo = (u32 __iomem *)(dev->virt_addr + UDC_TXFIFO_ADDR);

	if (request_irq(pdev->irq, udc_irq, IRQF_SHARED, name, dev) != 0) {
		dev_dbg(&pdev->dev, "request_irq(%d) fail\n", pdev->irq);
		retval = -EBUSY;
		goto err_irq;
	}

	pci_set_drvdata(pdev, dev);

	/* chip revision for Hs AMD5536 */
	dev->chiprev = pdev->revision;

	pci_set_master(pdev);
	pci_try_set_mwi(pdev);

	/* init dma pools */
	if (use_dma) {
		retval = init_dma_pools(dev);
		if (retval != 0)
			goto err_dma;
	}

	dev->phys_addr = resource;
	dev->irq = pdev->irq;
	dev->pdev = pdev;

	/* general probing */
	if (udc_probe(dev)) {
		retval = -ENODEV;
		goto err_probe;
	}
	return 0;

err_probe:
	if (use_dma)
		free_dma_pools(dev);
err_dma:
	free_irq(pdev->irq, dev);
err_irq:
	iounmap(dev->virt_addr);
err_ioremap:
	release_mem_region(resource, len);
err_memreg:
	pci_disable_device(pdev);
err_pcidev:
	kfree(dev);
	return retval;
}

/* PCI device parameters */
static const struct pci_device_id pci_id[] = {
	{
		PCI_DEVICE(PCI_VENDOR_ID_AMD, 0x2096),
		.class =	PCI_CLASS_SERIAL_USB_DEVICE,
		.class_mask =	0xffffffff,
	},
	{},
};
MODULE_DEVICE_TABLE(pci, pci_id);

/* PCI functions */
static struct pci_driver udc_pci_driver = {
	.name =		(char *) name,
	.id_table =	pci_id,
	.probe =	udc_pci_probe,
	.remove =	udc_pci_remove,
};
module_pci_driver(udc_pci_driver);

MODULE_DESCRIPTION(UDC_MOD_DESCRIPTION);
MODULE_AUTHOR("Thomas Dahlmann");
MODULE_LICENSE("GPL");
