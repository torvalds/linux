/*
 *  Copyright (C) 2001-2002	Andre Hedrick <andre@linux-ide.org>
 *  Portions (C) Copyright 2002  Red Hat Inc
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
#include <linux/pci.h>
#include <linux/ide.h>
#include <linux/init.h>

#define DRV_NAME "ide_pci_generic"

static bool ide_generic_all;		/* Set to claim all devices */

module_param_named(all_generic_ide, ide_generic_all, bool, 0444);
MODULE_PARM_DESC(all_generic_ide, "IDE generic will claim all unknown PCI IDE storage controllers.");

static void netcell_quirkproc(ide_drive_t *drive)
{
	/* mark words 85-87 as valid */
	drive->id[ATA_ID_CSF_DEFAULT] |= 0x4000;
}

static const struct ide_port_ops netcell_port_ops = {
	.quirkproc		= netcell_quirkproc,
};

#define DECLARE_GENERIC_PCI_DEV(extra_flags) \
	{ \
		.name		= DRV_NAME, \
		.host_flags	= IDE_HFLAG_TRUST_BIOS_FOR_DMA | \
				  extra_flags, \
		.swdma_mask	= ATA_SWDMA2, \
		.mwdma_mask	= ATA_MWDMA2, \
		.udma_mask	= ATA_UDMA6, \
	}

static const struct ide_port_info generic_chipsets[] __devinitconst = {
	/*  0: Unknown */
	DECLARE_GENERIC_PCI_DEV(0),

	{	/* 1: NS87410 */
		.name		= DRV_NAME,
		.enablebits	= { {0x43, 0x08, 0x08}, {0x47, 0x08, 0x08} },
		.host_flags	= IDE_HFLAG_TRUST_BIOS_FOR_DMA,
		.swdma_mask	= ATA_SWDMA2,
		.mwdma_mask	= ATA_MWDMA2,
		.udma_mask	= ATA_UDMA6,
	},

	/*  2: SAMURAI / HT6565 / HINT_IDE */
	DECLARE_GENERIC_PCI_DEV(0),
	/*  3: UM8673F / UM8886A / UM8886BF */
	DECLARE_GENERIC_PCI_DEV(IDE_HFLAG_NO_DMA),
	/*  4: VIA_IDE / OPTI621V / Piccolo010{2,3,5} */
	DECLARE_GENERIC_PCI_DEV(IDE_HFLAG_NO_AUTODMA),

	{	/* 5: VIA8237SATA */
		.name		= DRV_NAME,
		.host_flags	= IDE_HFLAG_TRUST_BIOS_FOR_DMA |
				  IDE_HFLAG_OFF_BOARD,
		.swdma_mask	= ATA_SWDMA2,
		.mwdma_mask	= ATA_MWDMA2,
		.udma_mask	= ATA_UDMA6,
	},

	{	/* 6: Revolution */
		.name		= DRV_NAME,
		.port_ops	= &netcell_port_ops,
		.host_flags	= IDE_HFLAG_CLEAR_SIMPLEX |
				  IDE_HFLAG_TRUST_BIOS_FOR_DMA |
				  IDE_HFLAG_OFF_BOARD,
		.swdma_mask	= ATA_SWDMA2,
		.mwdma_mask	= ATA_MWDMA2,
		.udma_mask	= ATA_UDMA6,
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
	const struct ide_port_info *d = &generic_chipsets[id->driver_data];
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
			printk(KERN_INFO "%s %s: skipping disabled "
				"controller\n", d->name, pci_name(dev));
			goto out;
		}
	}
	ret = ide_pci_init_one(dev, d, NULL);
out:
	return ret;
}

static const struct pci_device_id generic_pci_tbl[] = {
	{ PCI_VDEVICE(NS,	PCI_DEVICE_ID_NS_87410),		 1 },
	{ PCI_VDEVICE(PCTECH,	PCI_DEVICE_ID_PCTECH_SAMURAI_IDE),	 2 },
	{ PCI_VDEVICE(HOLTEK,	PCI_DEVICE_ID_HOLTEK_6565),		 2 },
	{ PCI_VDEVICE(UMC,	PCI_DEVICE_ID_UMC_UM8673F),		 3 },
	{ PCI_VDEVICE(UMC,	PCI_DEVICE_ID_UMC_UM8886A),		 3 },
	{ PCI_VDEVICE(UMC,	PCI_DEVICE_ID_UMC_UM8886BF),		 3 },
	{ PCI_VDEVICE(HINT,	PCI_DEVICE_ID_HINT_VXPROII_IDE),	 2 },
	{ PCI_VDEVICE(VIA,	PCI_DEVICE_ID_VIA_82C561),		 4 },
	{ PCI_VDEVICE(OPTI,	PCI_DEVICE_ID_OPTI_82C558),		 4 },
#ifdef CONFIG_BLK_DEV_IDE_SATA
	{ PCI_VDEVICE(VIA,	PCI_DEVICE_ID_VIA_8237_SATA),		 5 },
#endif
	{ PCI_VDEVICE(TOSHIBA,	PCI_DEVICE_ID_TOSHIBA_PICCOLO_1),	 4 },
	{ PCI_VDEVICE(TOSHIBA,	PCI_DEVICE_ID_TOSHIBA_PICCOLO_2),	 4 },
	{ PCI_VDEVICE(TOSHIBA,	PCI_DEVICE_ID_TOSHIBA_PICCOLO_3),	 4 },
	{ PCI_VDEVICE(TOSHIBA,	PCI_DEVICE_ID_TOSHIBA_PICCOLO_5),	 4 },
	{ PCI_VDEVICE(NETCELL,	PCI_DEVICE_ID_REVOLUTION),		 6 },
	/*
	 * Must come last.  If you add entries adjust
	 * this table and generic_chipsets[] appropriately.
	 */
	{ PCI_ANY_ID, PCI_ANY_ID, PCI_ANY_ID, PCI_ANY_ID, PCI_CLASS_STORAGE_IDE << 8, 0xFFFFFF00UL, 0 },
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, generic_pci_tbl);

static struct pci_driver generic_pci_driver = {
	.name		= "PCI_IDE",
	.id_table	= generic_pci_tbl,
	.probe		= generic_init_one,
	.remove		= ide_pci_remove,
	.suspend	= ide_pci_suspend,
	.resume		= ide_pci_resume,
};

static int __init generic_ide_init(void)
{
	return ide_pci_register_driver(&generic_pci_driver);
}

static void __exit generic_ide_exit(void)
{
	pci_unregister_driver(&generic_pci_driver);
}

module_init(generic_ide_init);
module_exit(generic_ide_exit);

MODULE_AUTHOR("Andre Hedrick");
MODULE_DESCRIPTION("PCI driver module for generic PCI IDE");
MODULE_LICENSE("GPL");
