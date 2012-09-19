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

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/usb/gadget.h>
#include <linux/usb/chipidea.h>

/* driver name */
#define UDC_DRIVER_NAME   "ci13xxx_pci"

/******************************************************************************
 * PCI block
 *****************************************************************************/
struct ci13xxx_platform_data pci_platdata = {
	.name		= UDC_DRIVER_NAME,
	.capoffset	= DEF_CAPOFFSET,
};

struct ci13xxx_platform_data langwell_pci_platdata = {
	.name		= UDC_DRIVER_NAME,
	.capoffset	= 0,
};

struct ci13xxx_platform_data penwell_pci_platdata = {
	.name		= UDC_DRIVER_NAME,
	.capoffset	= 0,
	.power_budget	= 200,
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
	struct ci13xxx_platform_data *platdata = (void *)id->driver_data;
	struct platform_device *plat_ci;
	struct resource res[3];
	int retval = 0, nres = 2;

	if (!platdata) {
		dev_err(&pdev->dev, "device doesn't provide driver data\n");
		return -ENODEV;
	}

	retval = pci_enable_device(pdev);
	if (retval)
		goto done;

	if (!pdev->irq) {
		dev_err(&pdev->dev, "No IRQ, check BIOS/PCI setup!");
		retval = -ENODEV;
		goto disable_device;
	}

	pci_set_power_state(pdev, PCI_D0);
	pci_set_master(pdev);
	pci_try_set_mwi(pdev);

	memset(res, 0, sizeof(res));
	res[0].start	= pci_resource_start(pdev, 0);
	res[0].end	= pci_resource_end(pdev, 0);
	res[0].flags	= IORESOURCE_MEM;
	res[1].start	= pdev->irq;
	res[1].flags	= IORESOURCE_IRQ;

	plat_ci = ci13xxx_add_device(&pdev->dev, res, nres, platdata);
	if (IS_ERR(plat_ci)) {
		dev_err(&pdev->dev, "ci13xxx_add_device failed!\n");
		retval = PTR_ERR(plat_ci);
		goto disable_device;
	}

	pci_set_drvdata(pdev, plat_ci);

	return 0;

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
	struct platform_device *plat_ci = pci_get_drvdata(pdev);

	ci13xxx_remove_device(plat_ci);
	pci_set_drvdata(pdev, NULL);
	pci_disable_device(pdev);
}

/**
 * PCI device table
 * PCI device structure
 *
 * Check "pci.h" for details
 */
static DEFINE_PCI_DEVICE_TABLE(ci13xxx_pci_id_table) = {
	{
		PCI_DEVICE(0x153F, 0x1004),
		.driver_data = (kernel_ulong_t)&pci_platdata,
	},
	{
		PCI_DEVICE(0x153F, 0x1006),
		.driver_data = (kernel_ulong_t)&pci_platdata,
	},
	{
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x0811),
		.driver_data = (kernel_ulong_t)&langwell_pci_platdata,
	},
	{
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x0829),
		.driver_data = (kernel_ulong_t)&penwell_pci_platdata,
	},
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
