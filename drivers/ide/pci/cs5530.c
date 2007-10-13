/*
 * linux/drivers/ide/pci/cs5530.c		Version 0.74	Jul 28 2007
 *
 * Copyright (C) 2000			Andre Hedrick <andre@linux-ide.org>
 * Copyright (C) 2000			Mark Lord <mlord@pobox.com>
 * Copyright (C) 2007			Bartlomiej Zolnierkiewicz
 *
 * May be copied or modified under the terms of the GNU General Public License
 *
 * Development of this chipset driver was funded
 * by the nice folks at National Semiconductor.
 *
 * Documentation:
 *	CS5530 documentation available from National Semiconductor.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/ide.h>
#include <asm/io.h>
#include <asm/irq.h>

/**
 *	cs5530_xfer_set_mode	-	set a new transfer mode at the drive
 *	@drive: drive to tune
 *	@mode: new mode
 *
 *	Logging wrapper to the IDE driver speed configuration. This can
 *	probably go away now.
 */
 
static int cs5530_set_xfer_mode (ide_drive_t *drive, u8 mode)
{
	printk(KERN_DEBUG "%s: cs5530_set_xfer_mode(%s)\n",
		drive->name, ide_xfer_verbose(mode));
	return (ide_config_drive_speed(drive, mode));
}

/*
 * Here are the standard PIO mode 0-4 timings for each "format".
 * Format-0 uses fast data reg timings, with slower command reg timings.
 * Format-1 uses fast timings for all registers, but won't work with all drives.
 */
static unsigned int cs5530_pio_timings[2][5] = {
	{0x00009172, 0x00012171, 0x00020080, 0x00032010, 0x00040010},
	{0xd1329172, 0x71212171, 0x30200080, 0x20102010, 0x00100010}
};

/*
 * After chip reset, the PIO timings are set to 0x0000e132, which is not valid.
 */
#define CS5530_BAD_PIO(timings) (((timings)&~0x80000000)==0x0000e132)
#define CS5530_BASEREG(hwif)	(((hwif)->dma_base & ~0xf) + ((hwif)->channel ? 0x30 : 0x20))

static void cs5530_tunepio(ide_drive_t *drive, u8 pio)
{
	unsigned long basereg = CS5530_BASEREG(drive->hwif);
	unsigned int format = (inl(basereg + 4) >> 31) & 1;

	outl(cs5530_pio_timings[format][pio], basereg + ((drive->dn & 1)<<3));
}

/**
 *	cs5530_set_pio_mode	-	set PIO mode
 *	@drive: drive
 *	@pio: PIO mode number
 *
 *	Handles setting of PIO mode for both the chipset and drive.
 *
 *	The init_hwif_cs5530() routine guarantees that all drives
 *	will have valid default PIO timings set up before we get here.
 */

static void cs5530_set_pio_mode(ide_drive_t *drive, const u8 pio)
{
	if (cs5530_set_xfer_mode(drive, XFER_PIO_0 + pio) == 0)
		cs5530_tunepio(drive, pio);
}

/**
 *	cs5530_udma_filter	-	UDMA filter
 *	@drive: drive
 *
 *	cs5530_udma_filter() does UDMA mask filtering for the given drive
 *	taking into the consideration capabilities of the mate device.
 *
 *	The CS5530 specifies that two drives sharing a cable cannot mix
 *	UDMA/MDMA.  It has to be one or the other, for the pair, though
 *	different timings can still be chosen for each drive.  We could
 *	set the appropriate timing bits on the fly, but that might be
 *	a bit confusing.  So, for now we statically handle this requirement
 *	by looking at our mate drive to see what it is capable of, before
 *	choosing a mode for our own drive.
 *
 *	Note: This relies on the fact we never fail from UDMA to MWDMA2
 *	but instead drop to PIO.
 */

static u8 cs5530_udma_filter(ide_drive_t *drive)
{
	ide_hwif_t *hwif = drive->hwif;
	ide_drive_t *mate = &hwif->drives[(drive->dn & 1) ^ 1];
	struct hd_driveid *mateid = mate->id;
	u8 mask = hwif->ultra_mask;

	if (mate->present == 0)
		goto out;

	if ((mateid->capability & 1) && __ide_dma_bad_drive(mate) == 0) {
		if ((mateid->field_valid & 4) && (mateid->dma_ultra & 7))
			goto out;
		if ((mateid->field_valid & 2) && (mateid->dma_mword & 7))
			mask = 0;
	}
out:
	return mask;
}

/**
 *	cs5530_config_dma	-	set DMA/UDMA mode
 *	@drive: drive to tune
 *
 *	cs5530_config_dma() handles setting of DMA/UDMA mode
 *	for both the chipset and drive.
 */

static int cs5530_config_dma(ide_drive_t *drive)
{
	if (ide_tune_dma(drive))
		return 0;

	return 1;
}

static int cs5530_tune_chipset(ide_drive_t *drive, const u8 mode)
{
	unsigned long basereg;
	unsigned int reg, timings = 0;

	/*
	 * Tell the drive to switch to the new mode; abort on failure.
	 */
	if (cs5530_set_xfer_mode(drive, mode))
		return 1;	/* failure */

	/*
	 * Now tune the chipset to match the drive:
	 */
	switch (mode) {
		case XFER_UDMA_0:	timings = 0x00921250; break;
		case XFER_UDMA_1:	timings = 0x00911140; break;
		case XFER_UDMA_2:	timings = 0x00911030; break;
		case XFER_MW_DMA_0:	timings = 0x00077771; break;
		case XFER_MW_DMA_1:	timings = 0x00012121; break;
		case XFER_MW_DMA_2:	timings = 0x00002020; break;
		default:
			BUG();
			break;
	}
	basereg = CS5530_BASEREG(drive->hwif);
	reg = inl(basereg + 4);			/* get drive0 config register */
	timings |= reg & 0x80000000;		/* preserve PIO format bit */
	if ((drive-> dn & 1) == 0) {		/* are we configuring drive0? */
		outl(timings, basereg + 4);	/* write drive0 config register */
	} else {
		if (timings & 0x00100000)
			reg |=  0x00100000;	/* enable UDMA timings for both drives */
		else
			reg &= ~0x00100000;	/* disable UDMA timings for both drives */
		outl(reg, basereg + 4);		/* write drive0 config register */
		outl(timings, basereg + 12);	/* write drive1 config register */
	}

	return 0;	/* success */
}

/**
 *	init_chipset_5530	-	set up 5530 bridge
 *	@dev: PCI device
 *	@name: device name
 *
 *	Initialize the cs5530 bridge for reliable IDE DMA operation.
 */

static unsigned int __devinit init_chipset_cs5530 (struct pci_dev *dev, const char *name)
{
	struct pci_dev *master_0 = NULL, *cs5530_0 = NULL;
	unsigned long flags;

	if (pci_resource_start(dev, 4) == 0)
		return -EFAULT;

	dev = NULL;
	while ((dev = pci_get_device(PCI_VENDOR_ID_CYRIX, PCI_ANY_ID, dev)) != NULL) {
		switch (dev->device) {
			case PCI_DEVICE_ID_CYRIX_PCI_MASTER:
				master_0 = pci_dev_get(dev);
				break;
			case PCI_DEVICE_ID_CYRIX_5530_LEGACY:
				cs5530_0 = pci_dev_get(dev);
				break;
		}
	}
	if (!master_0) {
		printk(KERN_ERR "%s: unable to locate PCI MASTER function\n", name);
		goto out;
	}
	if (!cs5530_0) {
		printk(KERN_ERR "%s: unable to locate CS5530 LEGACY function\n", name);
		goto out;
	}

	spin_lock_irqsave(&ide_lock, flags);
		/* all CPUs (there should only be one CPU with this chipset) */

	/*
	 * Enable BusMaster and MemoryWriteAndInvalidate for the cs5530:
	 * -->  OR 0x14 into 16-bit PCI COMMAND reg of function 0 of the cs5530
	 */

	pci_set_master(cs5530_0);
	pci_try_set_mwi(cs5530_0);

	/*
	 * Set PCI CacheLineSize to 16-bytes:
	 * --> Write 0x04 into 8-bit PCI CACHELINESIZE reg of function 0 of the cs5530
	 */

	pci_write_config_byte(cs5530_0, PCI_CACHE_LINE_SIZE, 0x04);

	/*
	 * Disable trapping of UDMA register accesses (Win98 hack):
	 * --> Write 0x5006 into 16-bit reg at offset 0xd0 of function 0 of the cs5530
	 */

	pci_write_config_word(cs5530_0, 0xd0, 0x5006);

	/*
	 * Bit-1 at 0x40 enables MemoryWriteAndInvalidate on internal X-bus:
	 * The other settings are what is necessary to get the register
	 * into a sane state for IDE DMA operation.
	 */

	pci_write_config_byte(master_0, 0x40, 0x1e);

	/* 
	 * Set max PCI burst size (16-bytes seems to work best):
	 *	   16bytes: set bit-1 at 0x41 (reg value of 0x16)
	 *	all others: clear bit-1 at 0x41, and do:
	 *	  128bytes: OR 0x00 at 0x41
	 *	  256bytes: OR 0x04 at 0x41
	 *	  512bytes: OR 0x08 at 0x41
	 *	 1024bytes: OR 0x0c at 0x41
	 */

	pci_write_config_byte(master_0, 0x41, 0x14);

	/*
	 * These settings are necessary to get the chip
	 * into a sane state for IDE DMA operation.
	 */

	pci_write_config_byte(master_0, 0x42, 0x00);
	pci_write_config_byte(master_0, 0x43, 0xc1);

	spin_unlock_irqrestore(&ide_lock, flags);

out:
	pci_dev_put(master_0);
	pci_dev_put(cs5530_0);
	return 0;
}

/**
 *	init_hwif_cs5530	-	initialise an IDE channel
 *	@hwif: IDE to initialize
 *
 *	This gets invoked by the IDE driver once for each channel. It
 *	performs channel-specific pre-initialization before drive probing.
 */

static void __devinit init_hwif_cs5530 (ide_hwif_t *hwif)
{
	unsigned long basereg;
	u32 d0_timings;
	hwif->autodma = 0;

	if (hwif->mate)
		hwif->serialized = hwif->mate->serialized = 1;

	hwif->set_pio_mode = &cs5530_set_pio_mode;
	hwif->speedproc = &cs5530_tune_chipset;

	basereg = CS5530_BASEREG(hwif);
	d0_timings = inl(basereg + 0);
	if (CS5530_BAD_PIO(d0_timings)) {
		/* PIO timings not initialized? */
		outl(cs5530_pio_timings[(d0_timings >> 31) & 1][0], basereg + 0);
		if (!hwif->drives[0].autotune)
			hwif->drives[0].autotune = 1;
			/* needs autotuning later */
	}
	if (CS5530_BAD_PIO(inl(basereg + 8))) {
		/* PIO timings not initialized? */
		outl(cs5530_pio_timings[(d0_timings >> 31) & 1][0], basereg + 8);
		if (!hwif->drives[1].autotune)
			hwif->drives[1].autotune = 1;
			/* needs autotuning later */
	}

	if (hwif->dma_base == 0)
		return;

	hwif->atapi_dma = 1;
	hwif->ultra_mask = 0x07;
	hwif->mwdma_mask = 0x07;

	hwif->udma_filter = cs5530_udma_filter;
	hwif->ide_dma_check = &cs5530_config_dma;
	if (!noautodma)
		hwif->autodma = 1;
	hwif->drives[0].autodma = hwif->autodma;
	hwif->drives[1].autodma = hwif->autodma;
}

static ide_pci_device_t cs5530_chipset __devinitdata = {
	.name		= "CS5530",
	.init_chipset	= init_chipset_cs5530,
	.init_hwif	= init_hwif_cs5530,
	.autodma	= AUTODMA,
	.bootable	= ON_BOARD,
	.pio_mask	= ATA_PIO4,
};

static int __devinit cs5530_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	return ide_setup_pci_device(dev, &cs5530_chipset);
}

static struct pci_device_id cs5530_pci_tbl[] = {
	{ PCI_VENDOR_ID_CYRIX, PCI_DEVICE_ID_CYRIX_5530_IDE, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, cs5530_pci_tbl);

static struct pci_driver driver = {
	.name		= "CS5530 IDE",
	.id_table	= cs5530_pci_tbl,
	.probe		= cs5530_init_one,
};

static int __init cs5530_ide_init(void)
{
	return ide_pci_register_driver(&driver);
}

module_init(cs5530_ide_init);

MODULE_AUTHOR("Mark Lord");
MODULE_DESCRIPTION("PCI driver module for Cyrix/NS 5530 IDE");
MODULE_LICENSE("GPL");
