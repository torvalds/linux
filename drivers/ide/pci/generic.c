/*
 *  linux/drivers/ide/pci/generic.c	Version 0.11	December 30, 2002
 *
 *  Copyright (C) 2001-2002	Andre Hedrick <andre@linux-ide.org>
 *  Portions (C) Copyright 2002  Red Hat Inc <alan@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * For the avoidance of doubt the "preferred form" of this code is one which
 * is in an open non patent encumbered format. Where cryptographic key signing
 * forms part of the process of creating an executable the information
 * including keys needed to generate an equivalently functional executable
 * are deemed to be part of the source code.
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

static int ide_generic_all;		/* Set to claim all devices */

/*
 * the module_param_named() was added for the modular case
 * the __setup() is left as compatibility for existing setups
 */
#ifndef MODULE
static int __init ide_generic_all_on(char *unused)
{
	ide_generic_all = 1;
	printk(KERN_INFO "IDE generic will claim all unknown PCI IDE storage controllers.\n");
	return 1;
}
__setup("all-generic-ide", ide_generic_all_on);
#endif
module_param_named(all_generic_ide, ide_generic_all, bool, 0444);
MODULE_PARM_DESC(all_generic_ide, "IDE generic will claim all unknown PCI IDE storage controllers.");

static void __devinit init_hwif_generic (ide_hwif_t *hwif)
{
	switch(hwif->pci_dev->device) {
		case PCI_DEVICE_ID_UMC_UM8673F:
		case PCI_DEVICE_ID_UMC_UM8886A:
		case PCI_DEVICE_ID_UMC_UM8886BF:
			hwif->irq = hwif->channel ? 15 : 14;
			break;
		default:
			break;
	}

	if (!(hwif->dma_base))
		return;

	hwif->atapi_dma = 1;
	hwif->ultra_mask = 0x7f;
	hwif->mwdma_mask = 0x07;
	hwif->swdma_mask = 0x07;

	if (!noautodma)
		hwif->autodma = 1;
	hwif->drives[0].autodma = hwif->autodma;
	hwif->drives[1].autodma = hwif->autodma;
}

#if 0
	/* Logic to add back later on */

	if ((dev->class >> 8) == PCI_CLASS_STORAGE_IDE) {
		ide_pci_device_t *unknown = unknown_chipset;
		init_setup_unknown(dev, unknown);
		return 1;
	}
	return 0;
#endif	

static ide_pci_device_t generic_chipsets[] __devinitdata = {
	{	/* 0 */
		.name		= "Unknown",
		.init_hwif	= init_hwif_generic,
		.channels	= 2,
		.autodma	= AUTODMA,
		.bootable	= ON_BOARD,
	},{	/* 1 */
		.name		= "NS87410",
		.init_hwif	= init_hwif_generic,
		.channels	= 2,
		.autodma	= AUTODMA,
		.enablebits	= {{0x43,0x08,0x08}, {0x47,0x08,0x08}},
		.bootable	= ON_BOARD,
        },{	/* 2 */
		.name		= "SAMURAI",
		.init_hwif	= init_hwif_generic,
		.channels	= 2,
		.autodma	= AUTODMA,
		.bootable	= ON_BOARD,
	},{	/* 3 */
		.name		= "HT6565",
		.init_hwif	= init_hwif_generic,
		.channels	= 2,
		.autodma	= AUTODMA,
		.bootable	= ON_BOARD,
	},{	/* 4 */
		.name		= "UM8673F",
		.init_hwif	= init_hwif_generic,
		.channels	= 2,
		.autodma	= NODMA,
		.bootable	= ON_BOARD,
	},{	/* 5 */
		.name		= "UM8886A",
		.init_hwif	= init_hwif_generic,
		.channels	= 2,
		.autodma	= NODMA,
		.bootable	= ON_BOARD,
	},{	/* 6 */
		.name		= "UM8886BF",
		.init_hwif	= init_hwif_generic,
		.channels	= 2,
		.autodma	= NODMA,
		.bootable	= ON_BOARD,
	},{	/* 7 */
		.name		= "HINT_IDE",
		.init_hwif	= init_hwif_generic,
		.channels	= 2,
		.autodma	= AUTODMA,
		.bootable	= ON_BOARD,
	},{	/* 8 */
		.name		= "VIA_IDE",
		.init_hwif	= init_hwif_generic,
		.channels	= 2,
		.autodma	= NOAUTODMA,
		.bootable	= ON_BOARD,
	},{	/* 9 */
		.name		= "OPTI621V",
		.init_hwif	= init_hwif_generic,
		.channels	= 2,
		.autodma	= NOAUTODMA,
		.bootable	= ON_BOARD,
	},{	/* 10 */
		.name		= "VIA8237SATA",
		.init_hwif	= init_hwif_generic,
		.channels	= 2,
		.autodma	= AUTODMA,
		.bootable	= OFF_BOARD,
	},{	/* 11 */
		.name 		= "Piccolo0102",
		.init_hwif	= init_hwif_generic,
		.channels	= 2,
		.autodma	= NOAUTODMA,
		.bootable	= ON_BOARD,
	},{	/* 12 */
		.name 		= "Piccolo0103",
		.init_hwif	= init_hwif_generic,
		.channels	= 2,
		.autodma	= NOAUTODMA,
		.bootable	= ON_BOARD,
	},{	/* 13 */
		.name 		= "Piccolo0105",
		.init_hwif	= init_hwif_generic,
		.channels	= 2,
		.autodma	= NOAUTODMA,
		.bootable	= ON_BOARD,
	},{	/* 14 */
		.name		= "Revolution",
		.init_hwif	= init_hwif_generic,
		.channels	= 2,
		.autodma	= AUTODMA,
		.bootable	= OFF_BOARD,
	}
};

/**
 *	generic_init_one	-	called when a PIIX is found
 *	@dev: the generic device
 *	@id: the matching pci id
 *
 *	Called when the PCI registration layer (or the IDE initialization)
 *	finds a device matching our IDE device tables.
 */
 
static int __devinit generic_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	ide_pci_device_t *d = &generic_chipsets[id->driver_data];
	int ret = -ENODEV;

	/* Don't use the generic entry unless instructed to do so */
	if (id->driver_data == 0 && ide_generic_all == 0)
			goto out;

	switch (dev->vendor) {
	case PCI_VENDOR_ID_UMC:
		if (dev->device == PCI_DEVICE_ID_UMC_UM8886A &&
				!(PCI_FUNC(dev->devfn) & 1))
			goto out; /* UM8886A/BF pair */
		break;
	case PCI_VENDOR_ID_OPTI:
		if (dev->device == PCI_DEVICE_ID_OPTI_82C558 &&
				!(PCI_FUNC(dev->devfn) & 1))
			goto out;
		break;
	case PCI_VENDOR_ID_JMICRON:
		if (dev->device != PCI_DEVICE_ID_JMICRON_JMB368 &&
				PCI_FUNC(dev->devfn) != 1)
			goto out;
		break;
	case PCI_VENDOR_ID_NS:
		if (dev->device == PCI_DEVICE_ID_NS_87410 &&
				(dev->class >> 8) != PCI_CLASS_STORAGE_IDE)
			goto out;
		break;
	}

	if (dev->vendor != PCI_VENDOR_ID_JMICRON) {
		u16 command;
		pci_read_config_word(dev, PCI_COMMAND, &command);
		if (!(command & PCI_COMMAND_IO)) {
			printk(KERN_INFO "Skipping disabled %s IDE "
					"controller.\n", d->name);
			goto out;
		}
	}
	ret = ide_setup_pci_device(dev, d);
out:
	return ret;
}

static struct pci_device_id generic_pci_tbl[] = {
	{ PCI_VENDOR_ID_NS,     PCI_DEVICE_ID_NS_87410,            PCI_ANY_ID, PCI_ANY_ID, 0, 0, 1},
	{ PCI_VENDOR_ID_PCTECH, PCI_DEVICE_ID_PCTECH_SAMURAI_IDE,  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 2},
	{ PCI_VENDOR_ID_HOLTEK, PCI_DEVICE_ID_HOLTEK_6565,         PCI_ANY_ID, PCI_ANY_ID, 0, 0, 3},
	{ PCI_VENDOR_ID_UMC,    PCI_DEVICE_ID_UMC_UM8673F,         PCI_ANY_ID, PCI_ANY_ID, 0, 0, 4},
	{ PCI_VENDOR_ID_UMC,    PCI_DEVICE_ID_UMC_UM8886A,         PCI_ANY_ID, PCI_ANY_ID, 0, 0, 5},
	{ PCI_VENDOR_ID_UMC,    PCI_DEVICE_ID_UMC_UM8886BF,        PCI_ANY_ID, PCI_ANY_ID, 0, 0, 6},
	{ PCI_VENDOR_ID_HINT,   PCI_DEVICE_ID_HINT_VXPROII_IDE,    PCI_ANY_ID, PCI_ANY_ID, 0, 0, 7},
	{ PCI_VENDOR_ID_VIA,    PCI_DEVICE_ID_VIA_82C561,          PCI_ANY_ID, PCI_ANY_ID, 0, 0, 8},
	{ PCI_VENDOR_ID_OPTI,   PCI_DEVICE_ID_OPTI_82C558,         PCI_ANY_ID, PCI_ANY_ID, 0, 0, 9},
#ifdef CONFIG_BLK_DEV_IDE_SATA
	{ PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_8237_SATA,	   PCI_ANY_ID, PCI_ANY_ID, 0, 0, 10},
#endif
	{ PCI_VENDOR_ID_TOSHIBA,PCI_DEVICE_ID_TOSHIBA_PICCOLO,     PCI_ANY_ID, PCI_ANY_ID, 0, 0, 11},
	{ PCI_VENDOR_ID_TOSHIBA,PCI_DEVICE_ID_TOSHIBA_PICCOLO_1,   PCI_ANY_ID, PCI_ANY_ID, 0, 0, 12},
	{ PCI_VENDOR_ID_TOSHIBA,PCI_DEVICE_ID_TOSHIBA_PICCOLO_2,   PCI_ANY_ID, PCI_ANY_ID, 0, 0, 13},
	{ PCI_VENDOR_ID_NETCELL,PCI_DEVICE_ID_REVOLUTION,          PCI_ANY_ID, PCI_ANY_ID, 0, 0, 14},
	/* Must come last. If you add entries adjust this table appropriately and the init_one code */
	{ PCI_ANY_ID,		PCI_ANY_ID,			   PCI_ANY_ID, PCI_ANY_ID, PCI_CLASS_STORAGE_IDE << 8, 0xFFFFFF00UL, 0},
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, generic_pci_tbl);

static struct pci_driver driver = {
	.name		= "PCI_IDE",
	.id_table	= generic_pci_tbl,
	.probe		= generic_init_one,
};

static int __init generic_ide_init(void)
{
	return ide_pci_register_driver(&driver);
}

module_init(generic_ide_init);

MODULE_AUTHOR("Andre Hedrick");
MODULE_DESCRIPTION("PCI driver module for generic PCI IDE");
MODULE_LICENSE("GPL");
