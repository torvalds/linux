/*
 * linux/drivers/ide/pci/serverworks.c		Version 0.8	 25 Ebr 2003
 *
 * Copyright (C) 1998-2000 Michel Aubry
 * Copyright (C) 1998-2000 Andrzej Krzysztofowicz
 * Copyright (C) 1998-2000 Andre Hedrick <andre@linux-ide.org>
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

#include <linux/config.h>
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

static u8 svwks_revision = 0;
static struct pci_dev *isa_dev;

static int check_in_drive_lists (ide_drive_t *drive, const char **list)
{
	while (*list)
		if (!strcmp(*list++, drive->id->model))
			return 1;
	return 0;
}

static u8 svwks_ratemask (ide_drive_t *drive)
{
	struct pci_dev *dev     = HWIF(drive)->pci_dev;
	u8 mode;

	if (!svwks_revision)
		pci_read_config_byte(dev, PCI_REVISION_ID, &svwks_revision);

	if (dev->device == PCI_DEVICE_ID_SERVERWORKS_HT1000IDE)
		return 2;
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
		return ((reg & 0x00004000) == 0x00004000) ? 1 : 0;
	} else if (svwks_revision < SVWKS_CSB5_REVISION_NEW) {
		return 1;
	} else if (svwks_revision >= SVWKS_CSB5_REVISION_NEW) {
		u8 btr = 0;
		pci_read_config_byte(dev, 0x5A, &btr);
		mode = btr & 0x3;
		if (!eighty_ninty_three(drive))
			mode = min(mode, (u8)1);
		/* If someone decides to do UDMA133 on CSB5 the same
		   issue will bite so be inclusive */
		if (mode > 2 && check_in_drive_lists(drive, svwks_bad_ata100))
			mode = 2;
	}
	if (((dev->device == PCI_DEVICE_ID_SERVERWORKS_CSB6IDE) ||
	     (dev->device == PCI_DEVICE_ID_SERVERWORKS_CSB6IDE2)) &&
	    (!(PCI_FUNC(dev->devfn) & 1)))
		mode = 2;
	return mode;
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
static int svwks_tune_chipset (ide_drive_t *drive, u8 xferspeed)
{
	u8 udma_modes[]		= { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05 };
	u8 dma_modes[]		= { 0x77, 0x21, 0x20 };
	u8 pio_modes[]		= { 0x5d, 0x47, 0x34, 0x22, 0x20 };
	u8 drive_pci[]		= { 0x41, 0x40, 0x43, 0x42 };
	u8 drive_pci2[]		= { 0x45, 0x44, 0x47, 0x46 };

	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;
	u8 speed;
	u8 pio			= ide_get_best_pio_mode(drive, 255, 5, NULL);
	u8 unit			= (drive->select.b.unit & 0x01);
	u8 csb5			= svwks_csb_check(dev);
	u8 ultra_enable		= 0, ultra_timing = 0;
	u8 dma_timing		= 0, pio_timing = 0;
	u16 csb5_pio		= 0;

	if (xferspeed == 255)	/* PIO auto-tuning */
		speed = XFER_PIO_0 + pio;
	else
		speed = ide_rate_filter(svwks_ratemask(drive), xferspeed);

	/* If we are about to put a disk into UDMA mode we screwed up.
	   Our code assumes we never _ever_ do this on an OSB4 */
	   
	if(dev->device == PCI_DEVICE_ID_SERVERWORKS_OSB4 &&
		drive->media == ide_disk && speed >= XFER_UDMA_0)
			BUG();
			
	pci_read_config_byte(dev, drive_pci[drive->dn], &pio_timing);
	pci_read_config_byte(dev, drive_pci2[drive->dn], &dma_timing);
	pci_read_config_byte(dev, (0x56|hwif->channel), &ultra_timing);
	pci_read_config_word(dev, 0x4A, &csb5_pio);
	pci_read_config_byte(dev, 0x54, &ultra_enable);

	/* Per Specified Design by OEM, and ASIC Architect */
	if ((dev->device == PCI_DEVICE_ID_SERVERWORKS_CSB6IDE) ||
	    (dev->device == PCI_DEVICE_ID_SERVERWORKS_CSB6IDE2)) {
		if (!drive->init_speed) {
			u8 dma_stat = hwif->INB(hwif->dma_status);

dma_pio:
			if (((ultra_enable << (7-drive->dn) & 0x80) == 0x80) &&
			    ((dma_stat & (1<<(5+unit))) == (1<<(5+unit)))) {
				drive->current_speed = drive->init_speed = XFER_UDMA_0 + udma_modes[(ultra_timing >> (4*unit)) & ~(0xF0)];
				return 0;
			} else if ((dma_timing) &&
				   ((dma_stat&(1<<(5+unit)))==(1<<(5+unit)))) {
				u8 dmaspeed = dma_timing;

				dma_timing &= ~0xFF;
				if ((dmaspeed & 0x20) == 0x20)
					dmaspeed = XFER_MW_DMA_2;
				else if ((dmaspeed & 0x21) == 0x21)
					dmaspeed = XFER_MW_DMA_1;
				else if ((dmaspeed & 0x77) == 0x77)
					dmaspeed = XFER_MW_DMA_0;
				else
					goto dma_pio;
				drive->current_speed = drive->init_speed = dmaspeed;
				return 0;
			} else if (pio_timing) {
				u8 piospeed = pio_timing;

				pio_timing &= ~0xFF;
				if ((piospeed & 0x20) == 0x20)
					piospeed = XFER_PIO_4;
				else if ((piospeed & 0x22) == 0x22)
					piospeed = XFER_PIO_3;
				else if ((piospeed & 0x34) == 0x34)
					piospeed = XFER_PIO_2;
				else if ((piospeed & 0x47) == 0x47)
					piospeed = XFER_PIO_1;
				else if ((piospeed & 0x5d) == 0x5d)
					piospeed = XFER_PIO_0;
				else
					goto oem_setup_failed;
				drive->current_speed = drive->init_speed = piospeed;
				return 0;
			}
		}
	}

oem_setup_failed:

	pio_timing	&= ~0xFF;
	dma_timing	&= ~0xFF;
	ultra_timing	&= ~(0x0F << (4*unit));
	ultra_enable	&= ~(0x01 << drive->dn);
	csb5_pio	&= ~(0x0F << (4*drive->dn));

	switch(speed) {
		case XFER_PIO_4:
		case XFER_PIO_3:
		case XFER_PIO_2:
		case XFER_PIO_1:
		case XFER_PIO_0:
			pio_timing |= pio_modes[speed - XFER_PIO_0];
			csb5_pio   |= ((speed - XFER_PIO_0) << (4*drive->dn));
			break;

		case XFER_MW_DMA_2:
		case XFER_MW_DMA_1:
		case XFER_MW_DMA_0:
			pio_timing |= pio_modes[pio];
			csb5_pio   |= (pio << (4*drive->dn));
			dma_timing |= dma_modes[speed - XFER_MW_DMA_0];
			break;

		case XFER_UDMA_5:
		case XFER_UDMA_4:
		case XFER_UDMA_3:
		case XFER_UDMA_2:
		case XFER_UDMA_1:
		case XFER_UDMA_0:
			pio_timing   |= pio_modes[pio];
			csb5_pio     |= (pio << (4*drive->dn));
			dma_timing   |= dma_modes[2];
			ultra_timing |= ((udma_modes[speed - XFER_UDMA_0]) << (4*unit));
			ultra_enable |= (0x01 << drive->dn);
		default:
			break;
	}

	pci_write_config_byte(dev, drive_pci[drive->dn], pio_timing);
	if (csb5)
		pci_write_config_word(dev, 0x4A, csb5_pio);

	pci_write_config_byte(dev, drive_pci2[drive->dn], dma_timing);
	pci_write_config_byte(dev, (0x56|hwif->channel), ultra_timing);
	pci_write_config_byte(dev, 0x54, ultra_enable);

	return (ide_config_drive_speed(drive, speed));
}

static void config_chipset_for_pio (ide_drive_t *drive)
{
	u16 eide_pio_timing[6] = {960, 480, 240, 180, 120, 90};
	u16 xfer_pio = drive->id->eide_pio_modes;
	u8 timing, speed, pio;

	pio = ide_get_best_pio_mode(drive, 255, 5, NULL);

	if (xfer_pio > 4)
		xfer_pio = 0;

	if (drive->id->eide_pio_iordy > 0)
		for (xfer_pio = 5;
			xfer_pio>0 &&
			drive->id->eide_pio_iordy>eide_pio_timing[xfer_pio];
			xfer_pio--);
	else
		xfer_pio = (drive->id->eide_pio_modes & 4) ? 0x05 :
			   (drive->id->eide_pio_modes & 2) ? 0x04 :
			   (drive->id->eide_pio_modes & 1) ? 0x03 :
			   (drive->id->tPIO & 2) ? 0x02 :
			   (drive->id->tPIO & 1) ? 0x01 : xfer_pio;

	timing = (xfer_pio >= pio) ? xfer_pio : pio;

	switch(timing) {
		case 4: speed = XFER_PIO_4;break;
		case 3: speed = XFER_PIO_3;break;
		case 2: speed = XFER_PIO_2;break;
		case 1: speed = XFER_PIO_1;break;
		default:
			speed = (!drive->id->tPIO) ? XFER_PIO_0 : XFER_PIO_SLOW;
			break;
	}
	(void) svwks_tune_chipset(drive, speed);
	drive->current_speed = speed;
}

static void svwks_tune_drive (ide_drive_t *drive, u8 pio)
{
	if(pio == 255)
		(void) svwks_tune_chipset(drive, 255);
	else
		(void) svwks_tune_chipset(drive, (XFER_PIO_0 + pio));
}

static int config_chipset_for_dma (ide_drive_t *drive)
{
	u8 speed = ide_dma_speed(drive, svwks_ratemask(drive));

	if (!(speed))
		speed = XFER_PIO_0 + ide_get_best_pio_mode(drive, 255, 5, NULL);

	(void) svwks_tune_chipset(drive, speed);
	return ide_dma_enable(drive);
}

static int svwks_config_drive_xfer_rate (ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct hd_driveid *id	= drive->id;

	drive->init_speed = 0;

	if ((id->capability & 1) && drive->autodma) {

		if (ide_use_dma(drive)) {
			if (config_chipset_for_dma(drive))
				return hwif->ide_dma_on(drive);
		}

		goto fast_ata_pio;

	} else if ((id->capability & 8) || (id->field_valid & 2)) {
fast_ata_pio:
		config_chipset_for_pio(drive);
		//	hwif->tuneproc(drive, 5);
		return hwif->ide_dma_off_quietly(drive);
	}
	/* IORDY not supported */
	return 0;
}

/* This can go soon */

static int svwks_ide_dma_end (ide_drive_t *drive)
{
	return __ide_dma_end(drive);
}

static unsigned int __devinit init_chipset_svwks (struct pci_dev *dev, const char *name)
{
	unsigned int reg;
	u8 btr;

	/* save revision id to determine DMA capability */
	pci_read_config_byte(dev, PCI_REVISION_ID, &svwks_revision);

	/* force Master Latency Timer value to 64 PCICLKs */
	pci_write_config_byte(dev, PCI_LATENCY_TIMER, 0x40);

	/* OSB4 : South Bridge and IDE */
	if (dev->device == PCI_DEVICE_ID_SERVERWORKS_OSB4IDE) {
		isa_dev = pci_find_device(PCI_VENDOR_ID_SERVERWORKS,
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
			findev = pci_find_device(PCI_VENDOR_ID_SERVERWORKS,
				PCI_DEVICE_ID_SERVERWORKS_CSB5, NULL);
			if (findev) {
				pci_read_config_dword(findev, 0x4C, &reg4c);
				reg4c &= ~0x000007FF;
				reg4c |=  0x00000040;
				reg4c |=  0x00000020;
				pci_write_config_dword(findev, 0x4C, reg4c);
			}
			outb_p(0x06, 0x0c00);
			dev->irq = inb_p(0x0c01);
#if 0
			printk("%s: device class (0x%04x)\n",
				name, dev->class);
			if ((dev->class >> 8) != PCI_CLASS_STORAGE_IDE) {
				dev->class &= ~0x000F0F00;
		//		dev->class |= ~0x00000400;
				dev->class |= ~0x00010100;
				/**/
			}
#endif
		} else {
			struct pci_dev * findev = NULL;
			u8 reg41 = 0;

			findev = pci_find_device(PCI_VENDOR_ID_SERVERWORKS,
					PCI_DEVICE_ID_SERVERWORKS_CSB6, NULL);
			if (findev) {
				pci_read_config_byte(findev, 0x41, &reg41);
				reg41 &= ~0x40;
				pci_write_config_byte(findev, 0x41, reg41);
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
			btr |= (svwks_revision >= SVWKS_CSB5_REVISION_NEW) ? 0x3 : 0x2;
		pci_write_config_byte(dev, 0x5A, btr);
	}
	/* Setup HT1000 SouthBridge Controller - Single Channel Only */
	else if (dev->device == PCI_DEVICE_ID_SERVERWORKS_HT1000IDE) {
		pci_read_config_byte(dev, 0x5A, &btr);
		btr &= ~0x40;
		btr |= 0x3;
		pci_write_config_byte(dev, 0x5A, btr);
	}

	return (dev->irq) ? dev->irq : 0;
}

static unsigned int __devinit ata66_svwks_svwks (ide_hwif_t *hwif)
{
	return 1;
}

/* On Dell PowerEdge servers with a CSB5/CSB6, the top two bits
 * of the subsystem device ID indicate presence of an 80-pin cable.
 * Bit 15 clear = secondary IDE channel does not have 80-pin cable.
 * Bit 15 set   = secondary IDE channel has 80-pin cable.
 * Bit 14 clear = primary IDE channel does not have 80-pin cable.
 * Bit 14 set   = primary IDE channel has 80-pin cable.
 */
static unsigned int __devinit ata66_svwks_dell (ide_hwif_t *hwif)
{
	struct pci_dev *dev = hwif->pci_dev;
	if (dev->subsystem_vendor == PCI_VENDOR_ID_DELL &&
	    dev->vendor	== PCI_VENDOR_ID_SERVERWORKS &&
	    (dev->device == PCI_DEVICE_ID_SERVERWORKS_CSB5IDE ||
	     dev->device == PCI_DEVICE_ID_SERVERWORKS_CSB6IDE))
		return ((1 << (hwif->channel + 14)) &
			dev->subsystem_device) ? 1 : 0;
	return 0;
}

/* Sun Cobalt Alpine hardware avoids the 80-pin cable
 * detect issue by attaching the drives directly to the board.
 * This check follows the Dell precedent (how scary is that?!)
 *
 * WARNING: this only works on Alpine hardware!
 */
static unsigned int __devinit ata66_svwks_cobalt (ide_hwif_t *hwif)
{
	struct pci_dev *dev = hwif->pci_dev;
	if (dev->subsystem_vendor == PCI_VENDOR_ID_SUN &&
	    dev->vendor	== PCI_VENDOR_ID_SERVERWORKS &&
	    dev->device == PCI_DEVICE_ID_SERVERWORKS_CSB5IDE)
		return ((1 << (hwif->channel + 14)) &
			dev->subsystem_device) ? 1 : 0;
	return 0;
}

static unsigned int __devinit ata66_svwks (ide_hwif_t *hwif)
{
	struct pci_dev *dev = hwif->pci_dev;

	/* Per Specified Design by OEM, and ASIC Architect */
	if ((dev->device == PCI_DEVICE_ID_SERVERWORKS_CSB6IDE) ||
	    (dev->device == PCI_DEVICE_ID_SERVERWORKS_CSB6IDE2))
		return 1;

	/* Server Works */
	if (dev->subsystem_vendor == PCI_VENDOR_ID_SERVERWORKS)
		return ata66_svwks_svwks (hwif);
	
	/* Dell PowerEdge */
	if (dev->subsystem_vendor == PCI_VENDOR_ID_DELL)
		return ata66_svwks_dell (hwif);

	/* Cobalt Alpine */
	if (dev->subsystem_vendor == PCI_VENDOR_ID_SUN)
		return ata66_svwks_cobalt (hwif);

	return 0;
}

#undef CAN_SW_DMA
static void __devinit init_hwif_svwks (ide_hwif_t *hwif)
{
	u8 dma_stat = 0;

	if (!hwif->irq)
		hwif->irq = hwif->channel ? 15 : 14;

	hwif->tuneproc = &svwks_tune_drive;
	hwif->speedproc = &svwks_tune_chipset;

	hwif->atapi_dma = 1;

	if (hwif->pci_dev->device != PCI_DEVICE_ID_SERVERWORKS_OSB4IDE)
		hwif->ultra_mask = 0x3f;

	hwif->mwdma_mask = 0x07;
#ifdef CAN_SW_DMA
	hwif->swdma_mask = 0x07;
#endif /* CAN_SW_DMA */

	hwif->autodma = 0;

	if (!hwif->dma_base) {
		hwif->drives[0].autotune = 1;
		hwif->drives[1].autotune = 1;
		return;
	}

	hwif->ide_dma_check = &svwks_config_drive_xfer_rate;
	if (hwif->pci_dev->device == PCI_DEVICE_ID_SERVERWORKS_OSB4IDE)
		hwif->ide_dma_end = &svwks_ide_dma_end;
	else if (!(hwif->udma_four))
		hwif->udma_four = ata66_svwks(hwif);
	if (!noautodma)
		hwif->autodma = 1;

	dma_stat = hwif->INB(hwif->dma_status);
	hwif->drives[0].autodma = (dma_stat & 0x20);
	hwif->drives[1].autodma = (dma_stat & 0x40);
	hwif->drives[0].autotune = (!(dma_stat & 0x20));
	hwif->drives[1].autotune = (!(dma_stat & 0x40));
//	hwif->drives[0].autodma = hwif->autodma;
//	hwif->drives[1].autodma = hwif->autodma;
}

/*
 * We allow the BM-DMA driver to only work on enabled interfaces.
 */
static void __devinit init_dma_svwks (ide_hwif_t *hwif, unsigned long dmabase)
{
	struct pci_dev *dev = hwif->pci_dev;

	if (((dev->device == PCI_DEVICE_ID_SERVERWORKS_CSB6IDE) ||
	     (dev->device == PCI_DEVICE_ID_SERVERWORKS_CSB6IDE2)) &&
	    (!(PCI_FUNC(dev->devfn) & 1)) && (hwif->channel))
		return;

	ide_setup_dma(hwif, dmabase, 8);
}

static int __devinit init_setup_svwks (struct pci_dev *dev, ide_pci_device_t *d)
{
	return ide_setup_pci_device(dev, d);
}

static int __devinit init_setup_csb6 (struct pci_dev *dev, ide_pci_device_t *d)
{
	if (!(PCI_FUNC(dev->devfn) & 1)) {
		d->bootable = NEVER_BOARD;
		if (dev->resource[0].start == 0x01f1)
			d->bootable = ON_BOARD;
	}
#if 0
	if ((IDE_PCI_DEVID_EQ(d->devid, DEVID_CSB6) &&
             (!(PCI_FUNC(dev->devfn) & 1)))
		d->autodma = AUTODMA;
#endif

	d->channels = ((dev->device == PCI_DEVICE_ID_SERVERWORKS_CSB6IDE ||
			dev->device == PCI_DEVICE_ID_SERVERWORKS_CSB6IDE2) &&
		       (!(PCI_FUNC(dev->devfn) & 1))) ? 1 : 2;

	return ide_setup_pci_device(dev, d);
}

static ide_pci_device_t serverworks_chipsets[] __devinitdata = {
	{	/* 0 */
		.name		= "SvrWks OSB4",
		.init_setup	= init_setup_svwks,
		.init_chipset	= init_chipset_svwks,
		.init_hwif	= init_hwif_svwks,
		.channels	= 2,
		.autodma	= AUTODMA,
		.bootable	= ON_BOARD,
	},{	/* 1 */
		.name		= "SvrWks CSB5",
		.init_setup	= init_setup_svwks,
		.init_chipset	= init_chipset_svwks,
		.init_hwif	= init_hwif_svwks,
		.init_dma	= init_dma_svwks,
		.channels	= 2,
		.autodma	= AUTODMA,
		.bootable	= ON_BOARD,
	},{	/* 2 */
		.name		= "SvrWks CSB6",
		.init_setup	= init_setup_csb6,
		.init_chipset	= init_chipset_svwks,
		.init_hwif	= init_hwif_svwks,
		.init_dma	= init_dma_svwks,
		.channels	= 2,
		.autodma	= AUTODMA,
		.bootable	= ON_BOARD,
	},{	/* 3 */
		.name		= "SvrWks CSB6",
		.init_setup	= init_setup_csb6,
		.init_chipset	= init_chipset_svwks,
		.init_hwif	= init_hwif_svwks,
		.init_dma	= init_dma_svwks,
		.channels	= 1,	/* 2 */
		.autodma	= AUTODMA,
		.bootable	= ON_BOARD,
	},{	/* 4 */
		.name		= "SvrWks HT1000",
		.init_setup	= init_setup_svwks,
		.init_chipset	= init_chipset_svwks,
		.init_hwif	= init_hwif_svwks,
		.init_dma	= init_dma_svwks,
		.channels	= 1,	/* 2 */
		.autodma	= AUTODMA,
		.bootable	= ON_BOARD,
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
	ide_pci_device_t *d = &serverworks_chipsets[id->driver_data];

	return d->init_setup(dev, d);
}

static struct pci_device_id svwks_pci_tbl[] = {
	{ PCI_VENDOR_ID_SERVERWORKS, PCI_DEVICE_ID_SERVERWORKS_OSB4IDE, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{ PCI_VENDOR_ID_SERVERWORKS, PCI_DEVICE_ID_SERVERWORKS_CSB5IDE, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 1},
	{ PCI_VENDOR_ID_SERVERWORKS, PCI_DEVICE_ID_SERVERWORKS_CSB6IDE, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 2},
	{ PCI_VENDOR_ID_SERVERWORKS, PCI_DEVICE_ID_SERVERWORKS_CSB6IDE2, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 3},
	{ PCI_VENDOR_ID_SERVERWORKS, PCI_DEVICE_ID_SERVERWORKS_HT1000IDE, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 4},
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, svwks_pci_tbl);

static struct pci_driver driver = {
	.name		= "Serverworks_IDE",
	.id_table	= svwks_pci_tbl,
	.probe		= svwks_init_one,
};

static int svwks_ide_init(void)
{
	return ide_pci_register_driver(&driver);
}

module_init(svwks_ide_init);

MODULE_AUTHOR("Michael Aubry. Andrzej Krzysztofowicz, Andre Hedrick");
MODULE_DESCRIPTION("PCI driver module for Serverworks OSB4/CSB5/CSB6 IDE");
MODULE_LICENSE("GPL");
