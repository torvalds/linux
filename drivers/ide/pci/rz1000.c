/*
 *  linux/drivers/ide/pci/rz1000.c	Version 0.06	January 12, 2003
 *
 *  Copyright (C) 1995-1998  Linus Torvalds & author (see below)
 */

/*
 *  Principal Author:  mlord@pobox.com (Mark Lord)
 *
 *  See linux/MAINTAINERS for address of current maintainer.
 *
 *  This file provides support for disabling the buggy read-ahead
 *  mode of the RZ1000 IDE chipset, commonly used on Intel motherboards.
 *
 *  Dunno if this fixes both ports, or only the primary port (?).
 */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/pci.h>
#include <linux/ide.h>
#include <linux/init.h>

#include <asm/io.h>

static void __devinit init_hwif_rz1000 (ide_hwif_t *hwif)
{
	u16 reg;
	struct pci_dev *dev = hwif->pci_dev;

	hwif->chipset = ide_rz1000;
	if (!pci_read_config_word (dev, 0x40, &reg) &&
	    !pci_write_config_word(dev, 0x40, reg & 0xdfff)) {
		printk(KERN_INFO "%s: disabled chipset read-ahead "
			"(buggy RZ1000/RZ1001)\n", hwif->name);
	} else {
		hwif->serialized = 1;
		hwif->drives[0].no_unmask = 1;
		hwif->drives[1].no_unmask = 1;
		printk(KERN_INFO "%s: serialized, disabled unmasking "
			"(buggy RZ1000/RZ1001)\n", hwif->name);
	}
}

static ide_pci_device_t rz1000_chipset __devinitdata = {
	.name		= "RZ100x",
	.init_hwif	= init_hwif_rz1000,
	.autodma	= NODMA,
	.bootable	= ON_BOARD,
};

static int __devinit rz1000_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	return ide_setup_pci_device(dev, &rz1000_chipset);
}

static struct pci_device_id rz1000_pci_tbl[] = {
	{ PCI_VENDOR_ID_PCTECH, PCI_DEVICE_ID_PCTECH_RZ1000, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{ PCI_VENDOR_ID_PCTECH, PCI_DEVICE_ID_PCTECH_RZ1001, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, rz1000_pci_tbl);

static struct pci_driver driver = {
	.name		= "RZ1000_IDE",
	.id_table	= rz1000_pci_tbl,
	.probe		= rz1000_init_one,
};

static int __init rz1000_ide_init(void)
{
	return ide_pci_register_driver(&driver);
}

module_init(rz1000_ide_init);

MODULE_AUTHOR("Andre Hedrick");
MODULE_DESCRIPTION("PCI driver module for RZ1000 IDE");
MODULE_LICENSE("GPL");

