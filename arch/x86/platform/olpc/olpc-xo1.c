/*
 * Support for features of the OLPC XO-1 laptop
 *
 * Copyright (C) 2010 One Laptop per Child
 * Copyright (C) 2006 Red Hat, Inc.
 * Copyright (C) 2006 Advanced Micro Devices, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/platform_device.h>
#include <linux/pm.h>

#include <asm/io.h>
#include <asm/olpc.h>

#define DRV_NAME "olpc-xo1"

#define PMS_BAR		4
#define ACPI_BAR	5

/* PMC registers (PMS block) */
#define PM_SCLK		0x10
#define PM_IN_SLPCTL	0x20
#define PM_WKXD		0x34
#define PM_WKD		0x30
#define PM_SSC		0x54

/* PM registers (ACPI block) */
#define PM1_CNT		0x08
#define PM_GPE0_STS	0x18

static unsigned long acpi_base;
static unsigned long pms_base;

static void xo1_power_off(void)
{
	printk(KERN_INFO "OLPC XO-1 power off sequence...\n");

	/* Enable all of these controls with 0 delay */
	outl(0x40000000, pms_base + PM_SCLK);
	outl(0x40000000, pms_base + PM_IN_SLPCTL);
	outl(0x40000000, pms_base + PM_WKXD);
	outl(0x40000000, pms_base + PM_WKD);

	/* Clear status bits (possibly unnecessary) */
	outl(0x0002ffff, pms_base  + PM_SSC);
	outl(0xffffffff, acpi_base + PM_GPE0_STS);

	/* Write SLP_EN bit to start the machinery */
	outl(0x00002000, acpi_base + PM1_CNT);
}

/* Read the base addresses from the PCI BAR info */
static int __devinit setup_bases(struct pci_dev *pdev)
{
	int r;

	r = pci_enable_device_io(pdev);
	if (r) {
		dev_err(&pdev->dev, "can't enable device IO\n");
		return r;
	}

	r = pci_request_region(pdev, ACPI_BAR, DRV_NAME);
	if (r) {
		dev_err(&pdev->dev, "can't alloc PCI BAR #%d\n", ACPI_BAR);
		return r;
	}

	r = pci_request_region(pdev, PMS_BAR, DRV_NAME);
	if (r) {
		dev_err(&pdev->dev, "can't alloc PCI BAR #%d\n", PMS_BAR);
		pci_release_region(pdev, ACPI_BAR);
		return r;
	}

	acpi_base = pci_resource_start(pdev, ACPI_BAR);
	pms_base = pci_resource_start(pdev, PMS_BAR);

	return 0;
}

static int __devinit olpc_xo1_probe(struct platform_device *pdev)
{
	struct pci_dev *pcidev;
	int r;

	pcidev = pci_get_device(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_CS5536_ISA,
				NULL);
	if (!pdev)
		return -ENODEV;

	r = setup_bases(pcidev);
	if (r)
		return r;

	pm_power_off = xo1_power_off;

	printk(KERN_INFO "OLPC XO-1 support registered\n");
	return 0;
}

static int __devexit olpc_xo1_remove(struct platform_device *pdev)
{
	pm_power_off = NULL;
	return 0;
}

static struct platform_driver olpc_xo1_driver = {
	.driver = {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
	},
	.probe = olpc_xo1_probe,
	.remove = __devexit_p(olpc_xo1_remove),
};

static int __init olpc_xo1_init(void)
{
	return platform_driver_register(&olpc_xo1_driver);
}

static void __exit olpc_xo1_exit(void)
{
	platform_driver_unregister(&olpc_xo1_driver);
}

MODULE_AUTHOR("Daniel Drake <dsd@laptop.org>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:olpc-xo1");

module_init(olpc_xo1_init);
module_exit(olpc_xo1_exit);
