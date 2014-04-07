/*
 *  ata_generic.c - Generic PATA/SATA controller driver.
 *  Copyright 2005 Red Hat Inc, all rights reserved.
 *
 *  Elements from ide/pci/generic.c
 *	    Copyright (C) 2001-2002	Andre Hedrick <andre@linux-ide.org>
 *	    Portions (C) Copyright 2002  Red Hat Inc <alan@redhat.com>
 *
 *  May be copied or modified under the terms of the GNU General Public License
 *
 *  Driver for PCI IDE interfaces implementing the standard bus mastering
 *  interface functionality. This assumes the BIOS did the drive set up and
 *  tuning for us. By default we do not grab all IDE class devices as they
 *  may have other drivers or need fixups to avoid problems. Instead we keep
 *  a default list of stuff without documentation/driver that appears to
 *  work.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <scsi/scsi_host.h>
#include <linux/libata.h>

#define DRV_NAME "ata_generic"
#define DRV_VERSION "0.2.15"

/*
 *	A generic parallel ATA driver using libata
 */

enum {
	ATA_GEN_CLASS_MATCH		= (1 << 0),
	ATA_GEN_FORCE_DMA		= (1 << 1),
	ATA_GEN_INTEL_IDER		= (1 << 2),
};

/**
 *	generic_set_mode	-	mode setting
 *	@link: link to set up
 *	@unused: returned device on error
 *
 *	Use a non standard set_mode function. We don't want to be tuned.
 *	The BIOS configured everything. Our job is not to fiddle. We
 *	read the dma enabled bits from the PCI configuration of the device
 *	and respect them.
 */

static int generic_set_mode(struct ata_link *link, struct ata_device **unused)
{
	struct ata_port *ap = link->ap;
	const struct pci_device_id *id = ap->host->private_data;
	int dma_enabled = 0;
	struct ata_device *dev;

	if (id->driver_data & ATA_GEN_FORCE_DMA) {
		dma_enabled = 0xff;
	} else if (ap->ioaddr.bmdma_addr) {
		/* Bits 5 and 6 indicate if DMA is active on master/slave */
		dma_enabled = ioread8(ap->ioaddr.bmdma_addr + ATA_DMA_STATUS);
	}

	ata_for_each_dev(dev, link, ENABLED) {
		/* We don't really care */
		dev->pio_mode = XFER_PIO_0;
		dev->dma_mode = XFER_MW_DMA_0;
		/* We do need the right mode information for DMA or PIO
		   and this comes from the current configuration flags */
		if (dma_enabled & (1 << (5 + dev->devno))) {
			unsigned int xfer_mask = ata_id_xfermask(dev->id);
			const char *name;

			if (xfer_mask & (ATA_MASK_MWDMA | ATA_MASK_UDMA))
				name = ata_mode_string(xfer_mask);
			else {
				/* SWDMA perhaps? */
				name = "DMA";
				xfer_mask |= ata_xfer_mode2mask(XFER_MW_DMA_0);
			}

			ata_dev_info(dev, "configured for %s\n", name);

			dev->xfer_mode = ata_xfer_mask2mode(xfer_mask);
			dev->xfer_shift = ata_xfer_mode2shift(dev->xfer_mode);
			dev->flags &= ~ATA_DFLAG_PIO;
		} else {
			ata_dev_info(dev, "configured for PIO\n");
			dev->xfer_mode = XFER_PIO_0;
			dev->xfer_shift = ATA_SHIFT_PIO;
			dev->flags |= ATA_DFLAG_PIO;
		}
	}
	return 0;
}

static struct scsi_host_template generic_sht = {
	ATA_BMDMA_SHT(DRV_NAME),
};

static struct ata_port_operations generic_port_ops = {
	.inherits	= &ata_bmdma_port_ops,
	.cable_detect	= ata_cable_unknown,
	.set_mode	= generic_set_mode,
};

static int all_generic_ide;		/* Set to claim all devices */

/**
 *	is_intel_ider		-	identify intel IDE-R devices
 *	@dev: PCI device
 *
 *	Distinguish Intel IDE-R controller devices from other Intel IDE
 *	devices. IDE-R devices have no timing registers and are in
 *	most respects virtual. They should be driven by the ata_generic
 *	driver.
 *
 *	IDE-R devices have PCI offset 0xF8.L as zero, later Intel ATA has
 *	it non zero. All Intel ATA has 0x40 writable (timing), but it is
 *	not writable on IDE-R devices (this is guaranteed).
 */

static int is_intel_ider(struct pci_dev *dev)
{
	/* For Intel IDE the value at 0xF8 is only zero on IDE-R
	   interfaces */
	u32 r;
	u16 t;

	/* Check the manufacturing ID, it will be zero for IDE-R */
	pci_read_config_dword(dev, 0xF8, &r);
	/* Not IDE-R: punt so that ata_(old)piix gets it */
	if (r != 0)
		return 0;
	/* 0xF8 will also be zero on some early Intel IDE devices
	   but they will have a sane timing register */
	pci_read_config_word(dev, 0x40, &t);
	if (t != 0)
		return 0;
	/* Finally check if the timing register is writable so that
	   we eliminate any early devices hot-docked in a docking
	   station */
	pci_write_config_word(dev, 0x40, 1);
	pci_read_config_word(dev, 0x40, &t);
	if (t) {
		pci_write_config_word(dev, 0x40, 0);
		return 0;
	}
	return 1;
}

/**
 *	ata_generic_init		-	attach generic IDE
 *	@dev: PCI device found
 *	@id: match entry
 *
 *	Called each time a matching IDE interface is found. We check if the
 *	interface is one we wish to claim and if so we perform any chip
 *	specific hacks then let the ATA layer do the heavy lifting.
 */

static int ata_generic_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	u16 command;
	static const struct ata_port_info info = {
		.flags = ATA_FLAG_SLAVE_POSS,
		.pio_mask = ATA_PIO4,
		.mwdma_mask = ATA_MWDMA2,
		.udma_mask = ATA_UDMA5,
		.port_ops = &generic_port_ops
	};
	const struct ata_port_info *ppi[] = { &info, NULL };

	/* Don't use the generic entry unless instructed to do so */
	if ((id->driver_data & ATA_GEN_CLASS_MATCH) && all_generic_ide == 0)
		return -ENODEV;

	if ((id->driver_data & ATA_GEN_INTEL_IDER) && !all_generic_ide)
		if (!is_intel_ider(dev))
			return -ENODEV;

	/* Devices that need care */
	if (dev->vendor == PCI_VENDOR_ID_UMC &&
	    dev->device == PCI_DEVICE_ID_UMC_UM8886A &&
	    (!(PCI_FUNC(dev->devfn) & 1)))
		return -ENODEV;

	if (dev->vendor == PCI_VENDOR_ID_OPTI &&
	    dev->device == PCI_DEVICE_ID_OPTI_82C558 &&
	    (!(PCI_FUNC(dev->devfn) & 1)))
		return -ENODEV;

	/* Don't re-enable devices in generic mode or we will break some
	   motherboards with disabled and unused IDE controllers */
	pci_read_config_word(dev, PCI_COMMAND, &command);
	if (!(command & PCI_COMMAND_IO))
		return -ENODEV;

	if (dev->vendor == PCI_VENDOR_ID_AL)
		ata_pci_bmdma_clear_simplex(dev);

	if (dev->vendor == PCI_VENDOR_ID_ATI) {
		int rc = pcim_enable_device(dev);
		if (rc < 0)
			return rc;
		pcim_pin_device(dev);
	}
	return ata_pci_bmdma_init_one(dev, ppi, &generic_sht, (void *)id, 0);
}

static struct pci_device_id ata_generic[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_PCTECH, PCI_DEVICE_ID_PCTECH_SAMURAI_IDE), },
	{ PCI_DEVICE(PCI_VENDOR_ID_HOLTEK, PCI_DEVICE_ID_HOLTEK_6565), },
	{ PCI_DEVICE(PCI_VENDOR_ID_UMC,    PCI_DEVICE_ID_UMC_UM8673F), },
	{ PCI_DEVICE(PCI_VENDOR_ID_UMC,    PCI_DEVICE_ID_UMC_UM8886A), },
	{ PCI_DEVICE(PCI_VENDOR_ID_UMC,    PCI_DEVICE_ID_UMC_UM8886BF), },
	{ PCI_DEVICE(PCI_VENDOR_ID_HINT,   PCI_DEVICE_ID_HINT_VXPROII_IDE), },
	{ PCI_DEVICE(PCI_VENDOR_ID_VIA,    PCI_DEVICE_ID_VIA_82C561), },
	{ PCI_DEVICE(PCI_VENDOR_ID_OPTI,   PCI_DEVICE_ID_OPTI_82C558), },
	{ PCI_DEVICE(PCI_VENDOR_ID_CENATEK,PCI_DEVICE_ID_CENATEK_IDE),
	  .driver_data = ATA_GEN_FORCE_DMA },
#if !defined(CONFIG_PATA_TOSHIBA) && !defined(CONFIG_PATA_TOSHIBA_MODULE)
	{ PCI_DEVICE(PCI_VENDOR_ID_TOSHIBA,PCI_DEVICE_ID_TOSHIBA_PICCOLO_1), },
	{ PCI_DEVICE(PCI_VENDOR_ID_TOSHIBA,PCI_DEVICE_ID_TOSHIBA_PICCOLO_2),  },
	{ PCI_DEVICE(PCI_VENDOR_ID_TOSHIBA,PCI_DEVICE_ID_TOSHIBA_PICCOLO_3),  },
	{ PCI_DEVICE(PCI_VENDOR_ID_TOSHIBA,PCI_DEVICE_ID_TOSHIBA_PICCOLO_5),  },
#endif
	/* Intel, IDE class device */
	{ PCI_VENDOR_ID_INTEL, PCI_ANY_ID, PCI_ANY_ID, PCI_ANY_ID,
	  PCI_CLASS_STORAGE_IDE << 8, 0xFFFFFF00UL,
	  .driver_data = ATA_GEN_INTEL_IDER },
	/* Must come last. If you add entries adjust this table appropriately */
	{ PCI_DEVICE_CLASS(PCI_CLASS_STORAGE_IDE << 8, 0xFFFFFF00UL),
	  .driver_data = ATA_GEN_CLASS_MATCH },
	{ 0, },
};

static struct pci_driver ata_generic_pci_driver = {
	.name 		= DRV_NAME,
	.id_table	= ata_generic,
	.probe 		= ata_generic_init_one,
	.remove		= ata_pci_remove_one,
#ifdef CONFIG_PM
	.suspend	= ata_pci_device_suspend,
	.resume		= ata_pci_device_resume,
#endif
};

module_pci_driver(ata_generic_pci_driver);

MODULE_AUTHOR("Alan Cox");
MODULE_DESCRIPTION("low-level driver for generic ATA");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, ata_generic);
MODULE_VERSION(DRV_VERSION);

module_param(all_generic_ide, int, 0);
