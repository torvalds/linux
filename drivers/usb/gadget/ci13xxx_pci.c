/*
 * ci13xxx_pci.c - MIPS USB IP core family device controller
 *
 * Copyright (C) 2008 Chipidea - MIPS Technologies, Inc. All rights reserved.
 *
 * Author: David Lopo
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/pci.h>

#include "ci13xxx_udc.c"

/* driver name */
#define UDC_DRIVER_NAME   "ci13xxx_pci"

/******************************************************************************
 * PCI block
 *****************************************************************************/
/**
 * ci13xxx_pci_irq: interrut handler
 * @irq:  irq number
 * @pdev: USB Device Controller interrupt source
 *
 * This function returns IRQ_HANDLED if the IRQ has been handled
 * This is an ISR don't trace, use attribute interface instead
 */
static irqreturn_t ci13xxx_pci_irq(int irq, void *pdev)
{
	if (irq == 0) {
		dev_err(&((struct pci_dev *)pdev)->dev, "Invalid IRQ0 usage!");
		return IRQ_HANDLED;
	}
	return udc_irq();
}

static struct ci13xxx_udc_driver ci13xxx_pci_udc_driver = {
	.name		= UDC_DRIVER_NAME,
};

/**
 * ci13xxx_pci_probe: PCI probe
 * @pdev: USB device controller being probed
 * @id:   PCI hotplug ID connecting controller to UDC framework
 *
 * This function returns an error code
 * Allocates basic PCI resources for this USB device controller, and then
 * invokes the udc_probe() method to start the UDC associated with it
 */
static int __devinit ci13xxx_pci_probe(struct pci_dev *pdev,
				       const struct pci_device_id *id)
{
	void __iomem *regs = NULL;
	uintptr_t capoffset = DEF_CAPOFFSET;
	int retval = 0;

	if (id == NULL)
		return -EINVAL;

	retval = pci_enable_device(pdev);
	if (retval)
		goto done;

	if (!pdev->irq) {
		dev_err(&pdev->dev, "No IRQ, check BIOS/PCI setup!");
		retval = -ENODEV;
		goto disable_device;
	}

	retval = pci_request_regions(pdev, UDC_DRIVER_NAME);
	if (retval)
		goto disable_device;

	/* BAR 0 holds all the registers */
	regs = pci_iomap(pdev, 0, 0);
	if (!regs) {
		dev_err(&pdev->dev, "Error mapping memory!");
		retval = -EFAULT;
		goto release_regions;
	}
	pci_set_drvdata(pdev, (__force void *)regs);

	pci_set_master(pdev);
	pci_try_set_mwi(pdev);

	if (pdev->vendor == PCI_VENDOR_ID_INTEL)
		capoffset = 0;

	retval = udc_probe(&ci13xxx_pci_udc_driver, &pdev->dev, regs,
			   capoffset);
	if (retval)
		goto iounmap;

	/* our device does not have MSI capability */

	retval = request_irq(pdev->irq, ci13xxx_pci_irq, IRQF_SHARED,
			     UDC_DRIVER_NAME, pdev);
	if (retval)
		goto gadget_remove;

	return 0;

 gadget_remove:
	udc_remove();
 iounmap:
	pci_iounmap(pdev, regs);
 release_regions:
	pci_release_regions(pdev);
 disable_device:
	pci_disable_device(pdev);
 done:
	return retval;
}

/**
 * ci13xxx_pci_remove: PCI remove
 * @pdev: USB Device Controller being removed
 *
 * Reverses the effect of ci13xxx_pci_probe(),
 * first invoking the udc_remove() and then releases
 * all PCI resources allocated for this USB device controller
 */
static void __devexit ci13xxx_pci_remove(struct pci_dev *pdev)
{
	free_irq(pdev->irq, pdev);
	udc_remove();
	pci_iounmap(pdev, (__force void __iomem *)pci_get_drvdata(pdev));
	pci_release_regions(pdev);
	pci_disable_device(pdev);
}

/**
 * PCI device table
 * PCI device structure
 *
 * Check "pci.h" for details
 */
static DEFINE_PCI_DEVICE_TABLE(ci13xxx_pci_id_table) = {
	{ PCI_DEVICE(0x153F, 0x1004) },
	{ PCI_DEVICE(0x153F, 0x1006) },
	{ 0, 0, 0, 0, 0, 0, 0 /* end: all zeroes */ }
};
MODULE_DEVICE_TABLE(pci, ci13xxx_pci_id_table);

static struct pci_driver ci13xxx_pci_driver = {
	.name         =	UDC_DRIVER_NAME,
	.id_table     =	ci13xxx_pci_id_table,
	.probe        =	ci13xxx_pci_probe,
	.remove       =	__devexit_p(ci13xxx_pci_remove),
};

module_pci_driver(ci13xxx_pci_driver);

MODULE_AUTHOR("MIPS - David Lopo <dlopo@chipidea.mips.com>");
MODULE_DESCRIPTION("MIPS CI13XXX USB Peripheral Controller");
MODULE_LICENSE("GPL");
MODULE_VERSION("June 2008");
