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
	int error;

	if (!(pci_resource_flags(dev, 0) & IORESOURCE_IO))
		return -ENODEV;

	smbus = kzalloc(sizeof(struct pasemi_smbus), GFP_KERNEL);
	if (!smbus)
		return -ENOMEM;

	smbus->dev = &dev->dev;
	smbus->base = pci_resource_start(dev, 0);
	smbus->size = pci_resource_len(dev, 0);
	smbus->clk_div = CLK_100K_DIV;

	if (!request_region(smbus->base, smbus->size,
			    pasemi_smb_pci_driver.name)) {
		error = -EBUSY;
		goto out_kfree;
	}

	smbus->ioaddr = pci_iomap(dev, 0, 0);
	if (!smbus->ioaddr) {
		error = -EBUSY;
		goto out_release_region;
	}

	error = pasemi_i2c_common_probe(smbus);
	if (error)
		goto out_ioport_unmap;

	pci_set_drvdata(dev, smbus);

	return 0;

 out_ioport_unmap:
	pci_iounmap(dev, smbus->ioaddr);
 out_release_region:
	release_region(smbus->base, smbus->size);
 out_kfree:
	kfree(smbus);
	return error;
}

static void pasemi_smb_pci_remove(struct pci_dev *dev)
{
	struct pasemi_smbus *smbus = pci_get_drvdata(dev);

	i2c_del_adapter(&smbus->adapter);
	pci_iounmap(dev, smbus->ioaddr);
	release_region(smbus->base, smbus->size);
	kfree(smbus);
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
	.remove		= pasemi_smb_pci_remove,
};

module_pci_driver(pasemi_smb_pci_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Olof Johansson <olof@lixom.net>");
MODULE_DESCRIPTION("PA Semi PWRficient SMBus driver");
