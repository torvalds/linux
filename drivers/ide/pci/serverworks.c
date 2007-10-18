/*
 * linux/drivers/ide/pci/serverworks.c		Version 0.22	Jun 27 2007
 *
 * Copyright (C) 1998-2000 Michel Aubry
 * Copyright (C) 1998-2000 Andrzej Krzysztofowicz
 * Copyright (C) 1998-2000 Andre Hedrick <andre@linux-ide.org>
 * Copyright (C)      2007 Bartlomiej Zolnierkiewicz
 * Portions copyright (c) 2001 Sun Microsystems
 *
 *
 * RCC/ServerWorks IDE driver for Linux
 *
 *   OSB4: `Open South Bridge' IDE Interface (fn 1)
 *         supports UDMA mode 2 (33 MB/s)
 *
 *   CSB5: `Champion South Bridge' IDE Interface (fn 1)
 *         all revisions support UDMA mode 4 (66 MB/s)
 *         revision A2.0 and up support UDMA mode 5 (100 MB/s)
 *
 *         *** The CSB5 does not provide ANY register ***
 *         *** to detect 80-conductor cable presence. ***
 *
 *   CSB6: `Champion South Bridge' IDE Interface (optional: third channel)
 *
 *   HT1000: AKA BCM5785 - Hypertransport Southbridge for Opteron systems. IDE
 *   controller same as the CSB6. Single channel ATA100 only.
 *
 * Documentation:
 *	Available under NDA only. Errata info very hard to get.
 *
 */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/hdreg.h>
#include <linux/ide.h>
#include <linux/init.h>
#include <linux/delay.h>

#include <asm/io.h>

#define SVWKS_CSB5_REVISION_NEW	0x92 /* min PCI_REVISION_ID for UDMA5 (A2.0) */
#define SVWKS_CSB6_REVISION	0xa0 /* min PCI_REVISION_ID for UDMA4 (A1.0) */

/* Seagate Barracuda ATA IV Family drives in UDMA mode 5
 * can overrun their FIFOs when used with the CSB5 */
static const char *svwks_bad_ata100[] = {
	"ST320011A",
	"ST340016A",
	"ST360021A",
	"ST380021A",
	NULL
};

static struct pci_dev *isa_dev;

static int check_in_drive_lists (ide_drive_t *drive, const char **list)
{
	while (*list)
		if (!strcmp(*list++, drive->id->model))
			return 1;
	return 0;
}

static u8 svwks_udma_filter(ide_drive_t *drive)
{
	struct pci_dev *dev     = HWIF(drive)->pci_dev;
	u8 mask = 0;

	if (dev->device == PCI_DEVICE_ID_SERVERWORKS_HT1000IDE)
		return 0x1f;
	if (dev->device == PCI_DEVICE_ID_SERVERWORKS_OSB4IDE) {
		u32 reg = 0;
		if (isa_dev)
			pci_read_config_dword(isa_dev, 0x64, &reg);
			
		/*
		 *	Don't enable UDMA on disk devices for the moment
		 */
		if(drive->media == ide_disk)
			return 0;
		/* Check the OSB4 DMA33 enable bit */
		return ((reg & 0x00004000) == 0x00004000) ? 0x07 : 0;
	} else if (dev->revision < SVWKS_CSB5_REVISION_NEW) {
		return 0x07;
	} else if (dev->revision >= SVWKS_CSB5_REVISION_NEW) {
		u8 btr = 0, mode;
		pci_read_config_byte(dev, 0x5A, &btr);
		mode = btr & 0x3;

		/* If someone decides to do UDMA133 on CSB5 the same
		   issue will bite so be inclusive */
		if (mode > 2 && check_in_drive_lists(drive, svwks_bad_ata100))
			mode = 2;

		switch(mode) {
		case 3:	 mask = 0x3f; break;
		case 2:	 mask = 0x1f; break;
		case 1:	 mask = 0x07; break;
		default: mask = 0x00; break;
		}
	}
	if (((dev->device == PCI_DEVICE_ID_SERVERWORKS_CSB6IDE) ||
	     (dev->device == PCI_DEVICE_ID_SERVERWORKS_CSB6IDE2)) &&
	    (!(PCI_FUNC(dev->devfn) & 1)))
		mask = 0x1f;

	return mask;
}

static u8 svwks_csb_check (struct pci_dev *dev)
{
	switch (dev->device) {
		case PCI_DEVICE_ID_SERVERWORKS_CSB5IDE:
		case PCI_DEVICE_ID_SERVERWORKS_CSB6IDE:
		case PCI_DEVICE_ID_SERVERWORKS_CSB6IDE2:
		case PCI_DEVICE_ID_SERVERWORKS_HT1000IDE:
			return 1;
		default:
			break;
	}
	return 0;
}

static void svwks_set_pio_mode(ide_drive_t *drive, const u8 pio)
{
	static const u8 pio_modes[] = { 0x5d, 0x47, 0x34, 0x22, 0x20 };
	static const u8 drive_pci[] = { 0x41, 0x40, 0x43, 0x42 };

	struct pci_dev *dev = drive->hwif->pci_dev;

	pci_write_config_byte(dev, drive_pci[drive->dn], pio_modes[pio]);

	if (svwks_csb_check(dev)) {
		u16 csb_pio = 0;

		pci_read_config_word(dev, 0x4a, &csb_pio);

		csb_pio &= ~(0x0f << (4 * drive->dn));
		csb_pio |= (pio << (4 * drive->dn));

		pci_write_config_word(dev, 0x4a, csb_pio);
	}
}

static void svwks_set_dma_mode(ide_drive_t *drive, const u8 speed)
{
	static const u8 udma_modes[]		= { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05 };
	static const u8 dma_modes[]		= { 0x77, 0x21, 0x20 };
	static const u8 drive_pci2[]		= { 0x45, 0x44, 0x47, 0x46 };

	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;
	u8 unit			= (drive->select.b.unit & 0x01);

	u8 ultra_enable	 = 0, ultra_timing = 0, dma_timing = 0;

	/* If we are about to put a disk into UDMA mode we screwed up.
	   Our code assumes we never _ever_ do this on an OSB4 */
	   
	if(dev->device == PCI_DEVICE_ID_SERVERWORKS_OSB4 &&
		drive->media == ide_disk && speed >= XFER_UDMA_0)
			BUG();

	pci_read_config_byte(dev, (0x56|hwif->channel), &ultra_timing);
	pci_read_config_byte(dev, 0x54, &ultra_enable);

	ultra_timing	&= ~(0x0F << (4*unit));
	ultra_enable	&= ~(0x01 << drive->dn);

	switch(speed) {
		case XFER_MW_DMA_2:
		case XFER_MW_DMA_1:
		case XFER_MW_DMA_0:
			dma_timing |= dma_modes[speed - XFER_MW_DMA_0];
			break;

		case XFER_UDMA_5:
		case XFER_UDMA_4:
		case XFER_UDMA_3:
		case XFER_UDMA_2:
		case XFER_UDMA_1:
		case XFER_UDMA_0:
			dma_timing   |= dma_modes[2];
			ultra_timing |= ((udma_modes[speed - XFER_UDMA_0]) << (4*unit));
			ultra_enable |= (0x01 << drive->dn);
		default:
			break;
	}

	pci_write_config_byte(dev, drive_pci2[drive->dn], dma_timing);
	pci_write_config_byte(dev, (0x56|hwif->channel), ultra_timing);
	pci_write_config_byte(dev, 0x54, ultra_enable);
}

static unsigned int __devinit init_chipset_svwks (struct pci_dev *dev, const char *name)
{
	unsigned int reg;
	u8 btr;

	/* force Master Latency Timer value to 64 PCICLKs */
	pci_write_config_byte(dev, PCI_LATENCY_TIMER, 0x40);

	/* OSB4 : South Bridge and IDE */
	if (dev->device == PCI_DEVICE_ID_SERVERWORKS_OSB4IDE) {
		isa_dev = pci_get_device(PCI_VENDOR_ID_SERVERWORKS,
			  PCI_DEVICE_ID_SERVERWORKS_OSB4, NULL);
		if (isa_dev) {
			pci_read_config_dword(isa_dev, 0x64, &reg);
			reg &= ~0x00002000; /* disable 600ns interrupt mask */
			if(!(reg & 0x00004000))
				printk(KERN_DEBUG "%s: UDMA not BIOS enabled.\n", name);
			reg |=  0x00004000; /* enable UDMA/33 support */
			pci_write_config_dword(isa_dev, 0x64, reg);
		}
	}

	/* setup CSB5/CSB6 : South Bridge and IDE option RAID */
	else if ((dev->device == PCI_DEVICE_ID_SERVERWORKS_CSB5IDE) ||
		 (dev->device == PCI_DEVICE_ID_SERVERWORKS_CSB6IDE) ||
		 (dev->device == PCI_DEVICE_ID_SERVERWORKS_CSB6IDE2)) {

		/* Third Channel Test */
		if (!(PCI_FUNC(dev->devfn) & 1)) {
			struct pci_dev * findev = NULL;
			u32 reg4c = 0;
			findev = pci_get_device(PCI_VENDOR_ID_SERVERWORKS,
				PCI_DEVICE_ID_SERVERWORKS_CSB5, NULL);
			if (findev) {
				pci_read_config_dword(findev, 0x4C, &reg4c);
				reg4c &= ~0x000007FF;
				reg4c |=  0x00000040;
				reg4c |=  0x00000020;
				pci_write_config_dword(findev, 0x4C, reg4c);
				pci_dev_put(findev);
			}
			outb_p(0x06, 0x0c00);
			dev->irq = inb_p(0x0c01);
		} else {
			struct pci_dev * findev = NULL;
			u8 reg41 = 0;

			findev = pci_get_device(PCI_VENDOR_ID_SERVERWORKS,
					PCI_DEVICE_ID_SERVERWORKS_CSB6, NULL);
			if (findev) {
				pci_read_config_byte(findev, 0x41, &reg41);
				reg41 &= ~0x40;
				pci_write_config_byte(findev, 0x41, reg41);
				pci_dev_put(findev);
			}
			/*
			 * This is a device pin issue on CSB6.
			 * Since there will be a future raid mode,
			 * early versions of the chipset require the
			 * interrupt pin to be set, and it is a compatibility
			 * mode issue.
			 */
			if ((dev->class >> 8) == PCI_CLASS_STORAGE_IDE)
				dev->irq = 0;
		}
//		pci_read_config_dword(dev, 0x40, &pioreg)
//		pci_write_config_dword(dev, 0x40, 0x99999999);
//		pci_read_config_dword(dev, 0x44, &dmareg);
//		pci_write_config_dword(dev, 0x44, 0xFFFFFFFF);
		/* setup the UDMA Control register
		 *
		 * 1. clear bit 6 to enable DMA
		 * 2. enable DMA modes with bits 0-1
		 * 	00 : legacy
		 * 	01 : udma2
		 * 	10 : udma2/udma4
		 * 	11 : udma2/udma4/udma5
		 */
		pci_read_config_byte(dev, 0x5A, &btr);
		btr &= ~0x40;
		if (!(PCI_FUNC(dev->devfn) & 1))
			btr |= 0x2;
		else
			btr |= (dev->revision >= SVWKS_CSB5_REVISION_NEW) ? 0x3 : 0x2;
		pci_write_config_byte(dev, 0x5A, btr);
	}
	/* Setup HT1000 SouthBridge Controller - Single Channel Only */
	else if (dev->device == PCI_DEVICE_ID_SERVERWORKS_HT1000IDE) {
		pci_read_config_byte(dev, 0x5A, &btr);
		btr &= ~0x40;
		btr |= 0x3;
		pci_write_config_byte(dev, 0x5A, btr);
	}

	return dev->irq;
}

static u8 __devinit ata66_svwks_svwks(ide_hwif_t *hwif)
{
	return ATA_CBL_PATA80;
}

/* On Dell PowerEdge servers with a CSB5/CSB6, the top two bits
 * of the subsystem device ID indicate presence of an 80-pin cable.
 * Bit 15 clear = secondary IDE channel does not have 80-pin cable.
 * Bit 15 set   = secondary IDE channel has 80-pin cable.
 * Bit 14 clear = primary IDE channel does not have 80-pin cable.
 * Bit 14 set   = primary IDE channel has 80-pin cable.
 */
static u8 __devinit ata66_svwks_dell(ide_hwif_t *hwif)
{
	struct pci_dev *dev = hwif->pci_dev;
	if (dev->subsystem_vendor == PCI_VENDOR_ID_DELL &&
	    dev->vendor	== PCI_VENDOR_ID_SERVERWORKS &&
	    (dev->device == PCI_DEVICE_ID_SERVERWORKS_CSB5IDE ||
	     dev->device == PCI_DEVICE_ID_SERVERWORKS_CSB6IDE))
		return ((1 << (hwif->channel + 14)) &
			dev->subsystem_device) ? ATA_CBL_PATA80 : ATA_CBL_PATA40;
	return ATA_CBL_PATA40;
}

/* Sun Cobalt Alpine hardware avoids the 80-pin cable
 * detect issue by attaching the drives directly to the board.
 * This check follows the Dell precedent (how scary is that?!)
 *
 * WARNING: this only works on Alpine hardware!
 */
static u8 __devinit ata66_svwks_cobalt(ide_hwif_t *hwif)
{
	struct pci_dev *dev = hwif->pci_dev;
	if (dev->subsystem_vendor == PCI_VENDOR_ID_SUN &&
	    dev->vendor	== PCI_VENDOR_ID_SERVERWORKS &&
	    dev->device == PCI_DEVICE_ID_SERVERWORKS_CSB5IDE)
		return ((1 << (hwif->channel + 14)) &
			dev->subsystem_device) ? ATA_CBL_PATA80 : ATA_CBL_PATA40;
	return ATA_CBL_PATA40;
}

static u8 __devinit ata66_svwks(ide_hwif_t *hwif)
{
	struct pci_dev *dev = hwif->pci_dev;

	/* Server Works */
	if (dev->subsystem_vendor == PCI_VENDOR_ID_SERVERWORKS)
		return ata66_svwks_svwks (hwif);
	
	/* Dell PowerEdge */
	if (dev->subsystem_vendor == PCI_VENDOR_ID_DELL)
		return ata66_svwks_dell (hwif);

	/* Cobalt Alpine */
	if (dev->subsystem_vendor == PCI_VENDOR_ID_SUN)
		return ata66_svwks_cobalt (hwif);

	/* Per Specified Design by OEM, and ASIC Architect */
	if ((dev->device == PCI_DEVICE_ID_SERVERWORKS_CSB6IDE) ||
	    (dev->device == PCI_DEVICE_ID_SERVERWORKS_CSB6IDE2))
		return ATA_CBL_PATA80;

	return ATA_CBL_PATA40;
}

static void __devinit init_hwif_svwks (ide_hwif_t *hwif)
{
	hwif->set_pio_mode = &svwks_set_pio_mode;
	hwif->set_dma_mode = &svwks_set_dma_mode;
	hwif->udma_filter = &svwks_udma_filter;

	if (!hwif->dma_base)
		return;

	if (hwif->pci_dev->device != PCI_DEVICE_ID_SERVERWORKS_OSB4IDE) {
		if (hwif->cbl != ATA_CBL_PATA40_SHORT)
			hwif->cbl = ata66_svwks(hwif);
	}
}

static ide_pci_device_t serverworks_chipsets[] __devinitdata = {
	{	/* 0 */
		.name		= "SvrWks OSB4",
		.init_chipset	= init_chipset_svwks,
		.init_hwif	= init_hwif_svwks,
		.host_flags	= IDE_HFLAG_LEGACY_IRQS | IDE_HFLAG_BOOTABLE,
		.pio_mask	= ATA_PIO4,
		.mwdma_mask	= ATA_MWDMA2,
		.udma_mask	= 0x00, /* UDMA is problematic on OSB4 */
	},{	/* 1 */
		.name		= "SvrWks CSB5",
		.init_chipset	= init_chipset_svwks,
		.init_hwif	= init_hwif_svwks,
		.host_flags	= IDE_HFLAG_LEGACY_IRQS | IDE_HFLAG_BOOTABLE,
		.pio_mask	= ATA_PIO4,
		.mwdma_mask	= ATA_MWDMA2,
		.udma_mask	= ATA_UDMA5,
	},{	/* 2 */
		.name		= "SvrWks CSB6",
		.init_chipset	= init_chipset_svwks,
		.init_hwif	= init_hwif_svwks,
		.host_flags	= IDE_HFLAG_LEGACY_IRQS | IDE_HFLAG_BOOTABLE,
		.pio_mask	= ATA_PIO4,
		.mwdma_mask	= ATA_MWDMA2,
		.udma_mask	= ATA_UDMA5,
	},{	/* 3 */
		.name		= "SvrWks CSB6",
		.init_chipset	= init_chipset_svwks,
		.init_hwif	= init_hwif_svwks,
		.host_flags	= IDE_HFLAG_LEGACY_IRQS | IDE_HFLAG_SINGLE |
				  IDE_HFLAG_BOOTABLE,
		.pio_mask	= ATA_PIO4,
		.mwdma_mask	= ATA_MWDMA2,
		.udma_mask	= ATA_UDMA5,
	},{	/* 4 */
		.name		= "SvrWks HT1000",
		.init_chipset	= init_chipset_svwks,
		.init_hwif	= init_hwif_svwks,
		.host_flags	= IDE_HFLAG_LEGACY_IRQS | IDE_HFLAG_SINGLE |
				  IDE_HFLAG_BOOTABLE,
		.pio_mask	= ATA_PIO4,
		.mwdma_mask	= ATA_MWDMA2,
		.udma_mask	= ATA_UDMA5,
	}
};

/**
 *	svwks_init_one	-	called when a OSB/CSB is found
 *	@dev: the svwks device
 *	@id: the matching pci id
 *
 *	Called when the PCI registration layer (or the IDE initialization)
 *	finds a device matching our IDE device tables.
 */
 
static int __devinit svwks_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	ide_pci_device_t d;
	u8 idx = id->driver_data;

	d = serverworks_chipsets[idx];

	if (idx == 2 || idx == 3) {
		if ((PCI_FUNC(dev->devfn) & 1) == 0) {
			if (pci_resource_start(dev, 0) != 0x01f1)
				d.host_flags &= ~IDE_HFLAG_BOOTABLE;
			d.host_flags |= IDE_HFLAG_SINGLE;
		} else
			d.host_flags &= ~IDE_HFLAG_SINGLE;
	}

	return ide_setup_pci_device(dev, &d);
}

static const struct pci_device_id svwks_pci_tbl[] = {
	{ PCI_VDEVICE(SERVERWORKS, PCI_DEVICE_ID_SERVERWORKS_OSB4IDE),   0 },
	{ PCI_VDEVICE(SERVERWORKS, PCI_DEVICE_ID_SERVERWORKS_CSB5IDE),   1 },
	{ PCI_VDEVICE(SERVERWORKS, PCI_DEVICE_ID_SERVERWORKS_CSB6IDE),   2 },
	{ PCI_VDEVICE(SERVERWORKS, PCI_DEVICE_ID_SERVERWORKS_CSB6IDE2),  3 },
	{ PCI_VDEVICE(SERVERWORKS, PCI_DEVICE_ID_SERVERWORKS_HT1000IDE), 4 },
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, svwks_pci_tbl);

static struct pci_driver driver = {
	.name		= "Serverworks_IDE",
	.id_table	= svwks_pci_tbl,
	.probe		= svwks_init_one,
};

static int __init svwks_ide_init(void)
{
	return ide_pci_register_driver(&driver);
}

module_init(svwks_ide_init);

MODULE_AUTHOR("Michael Aubry. Andrzej Krzysztofowicz, Andre Hedrick");
MODULE_DESCRIPTION("PCI driver module for Serverworks OSB4/CSB5/CSB6 IDE");
MODULE_LICENSE("GPL");
