// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2006-2007 PA Semi, Inc
 *
 * SMBus host driver for PA Semi PWRficient
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/stddef.h>
#include <linux/sched.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/io.h>

#include "i2c-pasemi-core.h"

#define CLK_100K_DIV	84
#define CLK_400K_DIV	21

static struct pci_driver pasemi_smb_pci_driver;

static int pasemi_smb_pci_probe(struct pci_dev *dev,
				      const struct pci_device_id *id)
{
	struct pasemi_smbus *smbus;
	unsigned long base;
	int size;
	int error;

	if (!(pci_resource_flags(dev, 0) & IORESOURCE_IO))
		return -ENODEV;

	smbus = devm_kzalloc(&dev->dev, sizeof(*smbus), GFP_KERNEL);
	if (!smbus)
		return -ENOMEM;

	smbus->dev = &dev->dev;
	base = pci_resource_start(dev, 0);
	size = pci_resource_len(dev, 0);
	smbus->clk_div = CLK_100K_DIV;

	/*
	 * The original PASemi PCI controllers don't have a register for
	 * their HW revision.
	 */
	smbus->hw_rev = PASEMI_HW_REV_PCI;

	if (!devm_request_region(&dev->dev, base, size,
			    pasemi_smb_pci_driver.name))
		return -EBUSY;

	smbus->ioaddr = pcim_iomap(dev, 0, 0);
	if (!smbus->ioaddr)
		return -EBUSY;

	smbus->adapter.class = I2C_CLASS_HWMON;
	error = pasemi_i2c_common_probe(smbus);
	if (error)
		return error;

	pci_set_drvdata(dev, smbus);

	return 0;
}

static const struct pci_device_id pasemi_smb_pci_ids[] = {
	{ PCI_DEVICE(0x1959, 0xa003) },
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, pasemi_smb_pci_ids);

static struct pci_driver pasemi_smb_pci_driver = {
	.name		= "i2c-pasemi",
	.id_table	= pasemi_smb_pci_ids,
	.probe		= pasemi_smb_pci_probe,
};

module_pci_driver(pasemi_smb_pci_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Olof Johansson <olof@lixom.net>");
MODULE_DESCRIPTION("PA Semi PWRficient SMBus driver");
