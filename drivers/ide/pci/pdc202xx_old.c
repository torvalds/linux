/*
 *  linux/drivers/ide/pci/pdc202xx_old.c	Version 0.50	Mar 3, 2007
 *
 *  Copyright (C) 1998-2002		Andre Hedrick <andre@linux-ide.org>
 *  Copyright (C) 2006-2007		MontaVista Software, Inc.
 *  Copyright (C) 2007			Bartlomiej Zolnierkiewicz
 *
 *  Promise Ultra33 cards with BIOS v1.20 through 1.28 will need this
 *  compiled into the kernel if you have more than one card installed.
 *  Note that BIOS v1.29 is reported to fix the problem.  Since this is
 *  safe chipset tuning, including this support is harmless
 *
 *  Promise Ultra66 cards with BIOS v1.11 this
 *  compiled into the kernel if you have more than one card installed.
 *
 *  Promise Ultra100 cards.
 *
 *  The latest chipset code will support the following ::
 *  Three Ultra33 controllers and 12 drives.
 *  8 are UDMA supported and 4 are limited to DMA mode 2 multi-word.
 *  The 8/4 ratio is a BIOS code limit by promise.
 *
 *  UNLESS you enable "CONFIG_PDC202XX_BURST"
 *
 */

/*
 *  Portions Copyright (C) 1999 Promise Technology, Inc.
 *  Author: Frank Tiernan (frankt@promise.com)
 *  Released under terms of General Public License
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
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/ide.h>

#include <asm/io.h>
#include <asm/irq.h>

#define PDC202XX_DEBUG_DRIVE_INFO	0

static const char *pdc_quirk_drives[] = {
	"QUANTUM FIREBALLlct08 08",
	"QUANTUM FIREBALLP KA6.4",
	"QUANTUM FIREBALLP KA9.1",
	"QUANTUM FIREBALLP LM20.4",
	"QUANTUM FIREBALLP KX13.6",
	"QUANTUM FIREBALLP KX20.5",
	"QUANTUM FIREBALLP KX27.3",
	"QUANTUM FIREBALLP LM20.5",
	NULL
};

static void pdc_old_disable_66MHz_clock(ide_hwif_t *);

static int pdc202xx_tune_chipset (ide_drive_t *drive, u8 xferspeed)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;
	u8 drive_pci		= 0x60 + (drive->dn << 2);
	u8 speed		= ide_rate_filter(drive, xferspeed);

	u8			AP = 0, BP = 0, CP = 0;
	u8			TA = 0, TB = 0, TC = 0;

#if PDC202XX_DEBUG_DRIVE_INFO
	u32			drive_conf = 0;
	pci_read_config_dword(dev, drive_pci, &drive_conf);
#endif

	/*
	 * TODO: do this once per channel
	 */
	if (dev->device != PCI_DEVICE_ID_PROMISE_20246)
		pdc_old_disable_66MHz_clock(hwif);

	pci_read_config_byte(dev, drive_pci,     &AP);
	pci_read_config_byte(dev, drive_pci + 1, &BP);
	pci_read_config_byte(dev, drive_pci + 2, &CP);

	switch(speed) {
		case XFER_UDMA_5:
		case XFER_UDMA_4:	TB = 0x20; TC = 0x01; break;
		case XFER_UDMA_2:	TB = 0x20; TC = 0x01; break;
		case XFER_UDMA_3:
		case XFER_UDMA_1:	TB = 0x40; TC = 0x02; break;
		case XFER_UDMA_0:
		case XFER_MW_DMA_2:	TB = 0x60; TC = 0x03; break;
		case XFER_MW_DMA_1:	TB = 0x60; TC = 0x04; break;
		case XFER_MW_DMA_0:	TB = 0xE0; TC = 0x0F; break;
		case XFER_SW_DMA_2:	TB = 0x60; TC = 0x05; break;
		case XFER_SW_DMA_1:	TB = 0x80; TC = 0x06; break;
		case XFER_SW_DMA_0:	TB = 0xC0; TC = 0x0B; break;
		case XFER_PIO_4:	TA = 0x01; TB = 0x04; break;
		case XFER_PIO_3:	TA = 0x02; TB = 0x06; break;
		case XFER_PIO_2:	TA = 0x03; TB = 0x08; break;
		case XFER_PIO_1:	TA = 0x05; TB = 0x0C; break;
		case XFER_PIO_0:
		default:		TA = 0x09; TB = 0x13; break;
	}

	if (speed < XFER_SW_DMA_0) {
		/*
		 * preserve SYNC_INT / ERDDY_EN bits while clearing
		 * Prefetch_EN / IORDY_EN / PA[3:0] bits of register A
		 */
		AP &= ~0x3f;
		if (drive->id->capability & 4)
			AP |= 0x20;	/* set IORDY_EN bit */
		if (drive->media == ide_disk)
			AP |= 0x10;	/* set Prefetch_EN bit */
		/* clear PB[4:0] bits of register B */
		BP &= ~0x1f;
		pci_write_config_byte(dev, drive_pci,     AP | TA);
		pci_write_config_byte(dev, drive_pci + 1, BP | TB);
	} else {
		/* clear MB[2:0] bits of register B */
		BP &= ~0xe0;
		/* clear MC[3:0] bits of register C */
		CP &= ~0x0f;
		pci_write_config_byte(dev, drive_pci + 1, BP | TB);
		pci_write_config_byte(dev, drive_pci + 2, CP | TC);
	}

#if PDC202XX_DEBUG_DRIVE_INFO
	printk(KERN_DEBUG "%s: %s drive%d 0x%08x ",
		drive->name, ide_xfer_verbose(speed),
		drive->dn, drive_conf);
	pci_read_config_dword(dev, drive_pci, &drive_conf);
	printk("0x%08x\n", drive_conf);
#endif

	return ide_config_drive_speed(drive, speed);
}

static void pdc202xx_tune_drive(ide_drive_t *drive, u8 pio)
{
	pio = ide_get_best_pio_mode(drive, pio, 4);
	pdc202xx_tune_chipset(drive, XFER_PIO_0 + pio);
}

static u8 pdc202xx_old_cable_detect (ide_hwif_t *hwif)
{
	u16 CIS = 0, mask = (hwif->channel) ? (1<<11) : (1<<10);

	pci_read_config_word(hwif->pci_dev, 0x50, &CIS);

	return (CIS & mask) ? ATA_CBL_PATA40 : ATA_CBL_PATA80;
}

/*
 * Set the control register to use the 66MHz system
 * clock for UDMA 3/4/5 mode operation when necessary.
 *
 * FIXME: this register is shared by both channels, some locking is needed
 *
 * It may also be possible to leave the 66MHz clock on
 * and readjust the timing parameters.
 */
static void pdc_old_enable_66MHz_clock(ide_hwif_t *hwif)
{
	unsigned long clock_reg = hwif->dma_master + 0x11;
	u8 clock = inb(clock_reg);

	outb(clock | (hwif->channel ? 0x08 : 0x02), clock_reg);
}

static void pdc_old_disable_66MHz_clock(ide_hwif_t *hwif)
{
	unsigned long clock_reg = hwif->dma_master + 0x11;
	u8 clock = inb(clock_reg);

	outb(clock & ~(hwif->channel ? 0x08 : 0x02), clock_reg);
}

static int pdc202xx_config_drive_xfer_rate (ide_drive_t *drive)
{
	drive->init_speed = 0;

	if (ide_tune_dma(drive))
		return 0;

	if (ide_use_fast_pio(drive))
		pdc202xx_tune_drive(drive, 255);

	return -1;
}

static int pdc202xx_quirkproc (ide_drive_t *drive)
{
	const char **list, *model = drive->id->model;

	for (list = pdc_quirk_drives; *list != NULL; list++)
		if (strstr(model, *list) != NULL)
			return 2;
	return 0;
}

static void pdc202xx_old_ide_dma_start(ide_drive_t *drive)
{
	if (drive->current_speed > XFER_UDMA_2)
		pdc_old_enable_66MHz_clock(drive->hwif);
	if (drive->media != ide_disk || drive->addressing == 1) {
		struct request *rq	= HWGROUP(drive)->rq;
		ide_hwif_t *hwif	= HWIF(drive);
		unsigned long high_16   = hwif->dma_master;
		unsigned long atapi_reg	= high_16 + (hwif->channel ? 0x24 : 0x20);
		u32 word_count	= 0;
		u8 clock = inb(high_16 + 0x11);

		outb(clock | (hwif->channel ? 0x08 : 0x02), high_16 + 0x11);
		word_count = (rq->nr_sectors << 8);
		word_count = (rq_data_dir(rq) == READ) ?
					word_count | 0x05000000 :
					word_count | 0x06000000;
		outl(word_count, atapi_reg);
	}
	ide_dma_start(drive);
}

static int pdc202xx_old_ide_dma_end(ide_drive_t *drive)
{
	if (drive->media != ide_disk || drive->addressing == 1) {
		ide_hwif_t *hwif	= HWIF(drive);
		unsigned long high_16	= hwif->dma_master;
		unsigned long atapi_reg	= high_16 + (hwif->channel ? 0x24 : 0x20);
		u8 clock		= 0;

		outl(0, atapi_reg); /* zero out extra */
		clock = inb(high_16 + 0x11);
		outb(clock & ~(hwif->channel ? 0x08:0x02), high_16 + 0x11);
	}
	if (drive->current_speed > XFER_UDMA_2)
		pdc_old_disable_66MHz_clock(drive->hwif);
	return __ide_dma_end(drive);
}

static int pdc202xx_old_ide_dma_test_irq(ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);
	unsigned long high_16	= hwif->dma_master;
	u8 dma_stat		= inb(hwif->dma_status);
	u8 sc1d			= inb(high_16 + 0x001d);

	if (hwif->channel) {
		/* bit7: Error, bit6: Interrupting, bit5: FIFO Full, bit4: FIFO Empty */
		if ((sc1d & 0x50) == 0x50)
			goto somebody_else;
		else if ((sc1d & 0x40) == 0x40)
			return (dma_stat & 4) == 4;
	} else {
		/* bit3: Error, bit2: Interrupting, bit1: FIFO Full, bit0: FIFO Empty */
		if ((sc1d & 0x05) == 0x05)
			goto somebody_else;
		else if ((sc1d & 0x04) == 0x04)
			return (dma_stat & 4) == 4;
	}
somebody_else:
	return (dma_stat & 4) == 4;	/* return 1 if INTR asserted */
}

static void pdc202xx_dma_lost_irq(ide_drive_t *drive)
{
	ide_hwif_t *hwif = HWIF(drive);

	if (hwif->resetproc != NULL)
		hwif->resetproc(drive);

	ide_dma_lost_irq(drive);
}

static void pdc202xx_dma_timeout(ide_drive_t *drive)
{
	ide_hwif_t *hwif = HWIF(drive);

	if (hwif->resetproc != NULL)
		hwif->resetproc(drive);

	ide_dma_timeout(drive);
}

static void pdc202xx_reset_host (ide_hwif_t *hwif)
{
	unsigned long high_16	= hwif->dma_master;
	u8 udma_speed_flag	= inb(high_16 | 0x001f);

	outb(udma_speed_flag | 0x10, high_16 | 0x001f);
	mdelay(100);
	outb(udma_speed_flag & ~0x10, high_16 | 0x001f);
	mdelay(2000);	/* 2 seconds ?! */

	printk(KERN_WARNING "PDC202XX: %s channel reset.\n",
		hwif->channel ? "Secondary" : "Primary");
}

static void pdc202xx_reset (ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);
	ide_hwif_t *mate	= hwif->mate;
	
	pdc202xx_reset_host(hwif);
	pdc202xx_reset_host(mate);
	pdc202xx_tune_drive(drive, 255);
}

static unsigned int __devinit init_chipset_pdc202xx(struct pci_dev *dev,
							const char *name)
{
	return dev->irq;
}

static void __devinit init_hwif_pdc202xx(ide_hwif_t *hwif)
{
	struct pci_dev *dev = hwif->pci_dev;

	/* PDC20265 has problems with large LBA48 requests */
	if ((dev->device == PCI_DEVICE_ID_PROMISE_20267) ||
	    (dev->device == PCI_DEVICE_ID_PROMISE_20265))
		hwif->rqsize = 256;

	hwif->autodma = 0;
	hwif->tuneproc  = &pdc202xx_tune_drive;
	hwif->quirkproc = &pdc202xx_quirkproc;

	if (hwif->pci_dev->device != PCI_DEVICE_ID_PROMISE_20246)
		hwif->resetproc = &pdc202xx_reset;

	hwif->speedproc = &pdc202xx_tune_chipset;

	hwif->drives[0].autotune = hwif->drives[1].autotune = 1;

	hwif->ultra_mask = hwif->cds->udma_mask;
	hwif->mwdma_mask = 0x07;
	hwif->swdma_mask = 0x07;
	hwif->atapi_dma = 1;

	hwif->err_stops_fifo = 1;

	hwif->ide_dma_check = &pdc202xx_config_drive_xfer_rate;
	hwif->dma_lost_irq = &pdc202xx_dma_lost_irq;
	hwif->dma_timeout = &pdc202xx_dma_timeout;

	if (hwif->pci_dev->device != PCI_DEVICE_ID_PROMISE_20246) {
		if (hwif->cbl != ATA_CBL_PATA40_SHORT)
			hwif->cbl = pdc202xx_old_cable_detect(hwif);

		hwif->dma_start = &pdc202xx_old_ide_dma_start;
		hwif->ide_dma_end = &pdc202xx_old_ide_dma_end;
	} 
	hwif->ide_dma_test_irq = &pdc202xx_old_ide_dma_test_irq;

	if (!noautodma)
		hwif->autodma = 1;
	hwif->drives[0].autodma = hwif->drives[1].autodma = hwif->autodma;
}

static void __devinit init_dma_pdc202xx(ide_hwif_t *hwif, unsigned long dmabase)
{
	u8 udma_speed_flag = 0, primary_mode = 0, secondary_mode = 0;

	if (hwif->channel) {
		ide_setup_dma(hwif, dmabase, 8);
		return;
	}

	udma_speed_flag	= inb(dmabase | 0x1f);
	primary_mode	= inb(dmabase | 0x1a);
	secondary_mode	= inb(dmabase | 0x1b);
	printk(KERN_INFO "%s: (U)DMA Burst Bit %sABLED " \
		"Primary %s Mode " \
		"Secondary %s Mode.\n", hwif->cds->name,
		(udma_speed_flag & 1) ? "EN" : "DIS",
		(primary_mode & 1) ? "MASTER" : "PCI",
		(secondary_mode & 1) ? "MASTER" : "PCI" );

#ifdef CONFIG_PDC202XX_BURST
	if (!(udma_speed_flag & 1)) {
		printk(KERN_INFO "%s: FORCING BURST BIT 0x%02x->0x%02x ",
			hwif->cds->name, udma_speed_flag,
			(udma_speed_flag|1));
		outb(udma_speed_flag | 1, dmabase | 0x1f);
		printk("%sACTIVE\n", (inb(dmabase | 0x1f) & 1) ? "" : "IN");
	}
#endif /* CONFIG_PDC202XX_BURST */

	ide_setup_dma(hwif, dmabase, 8);
}

static int __devinit init_setup_pdc202ata4(struct pci_dev *dev,
					   ide_pci_device_t *d)
{
	if ((dev->class >> 8) != PCI_CLASS_STORAGE_IDE) {
		u8 irq = 0, irq2 = 0;
		pci_read_config_byte(dev, PCI_INTERRUPT_LINE, &irq);
		/* 0xbc */
		pci_read_config_byte(dev, (PCI_INTERRUPT_LINE)|0x80, &irq2);
		if (irq != irq2) {
			pci_write_config_byte(dev,
				(PCI_INTERRUPT_LINE)|0x80, irq);     /* 0xbc */
			printk(KERN_INFO "%s: pci-config space interrupt "
				"mirror fixed.\n", d->name);
		}
	}
	return ide_setup_pci_device(dev, d);
}

static int __devinit init_setup_pdc20265(struct pci_dev *dev,
					 ide_pci_device_t *d)
{
	if ((dev->bus->self) &&
	    (dev->bus->self->vendor == PCI_VENDOR_ID_INTEL) &&
	    ((dev->bus->self->device == PCI_DEVICE_ID_INTEL_I960) ||
	     (dev->bus->self->device == PCI_DEVICE_ID_INTEL_I960RM))) {
		printk(KERN_INFO "ide: Skipping Promise PDC20265 "
			"attached to I2O RAID controller.\n");
		return -ENODEV;
	}
	return ide_setup_pci_device(dev, d);
}

static int __devinit init_setup_pdc202xx(struct pci_dev *dev,
					 ide_pci_device_t *d)
{
	return ide_setup_pci_device(dev, d);
}

static ide_pci_device_t pdc202xx_chipsets[] __devinitdata = {
	{	/* 0 */
		.name		= "PDC20246",
		.init_setup	= init_setup_pdc202ata4,
		.init_chipset	= init_chipset_pdc202xx,
		.init_hwif	= init_hwif_pdc202xx,
		.init_dma	= init_dma_pdc202xx,
		.autodma	= AUTODMA,
		.bootable	= OFF_BOARD,
		.extra		= 16,
		.pio_mask	= ATA_PIO4,
		.udma_mask	= 0x07, /* udma0-2 */
	},{	/* 1 */
		.name		= "PDC20262",
		.init_setup	= init_setup_pdc202ata4,
		.init_chipset	= init_chipset_pdc202xx,
		.init_hwif	= init_hwif_pdc202xx,
		.init_dma	= init_dma_pdc202xx,
		.autodma	= AUTODMA,
		.bootable	= OFF_BOARD,
		.extra		= 48,
		.pio_mask	= ATA_PIO4,
		.udma_mask	= 0x1f, /* udma0-4 */
	},{	/* 2 */
		.name		= "PDC20263",
		.init_setup	= init_setup_pdc202ata4,
		.init_chipset	= init_chipset_pdc202xx,
		.init_hwif	= init_hwif_pdc202xx,
		.init_dma	= init_dma_pdc202xx,
		.autodma	= AUTODMA,
		.bootable	= OFF_BOARD,
		.extra		= 48,
		.pio_mask	= ATA_PIO4,
		.udma_mask	= 0x1f, /* udma0-4 */
	},{	/* 3 */
		.name		= "PDC20265",
		.init_setup	= init_setup_pdc20265,
		.init_chipset	= init_chipset_pdc202xx,
		.init_hwif	= init_hwif_pdc202xx,
		.init_dma	= init_dma_pdc202xx,
		.autodma	= AUTODMA,
		.bootable	= OFF_BOARD,
		.extra		= 48,
		.pio_mask	= ATA_PIO4,
		.udma_mask	= 0x3f, /* udma0-5 */
	},{	/* 4 */
		.name		= "PDC20267",
		.init_setup	= init_setup_pdc202xx,
		.init_chipset	= init_chipset_pdc202xx,
		.init_hwif	= init_hwif_pdc202xx,
		.init_dma	= init_dma_pdc202xx,
		.autodma	= AUTODMA,
		.bootable	= OFF_BOARD,
		.extra		= 48,
		.pio_mask	= ATA_PIO4,
		.udma_mask	= 0x3f, /* udma0-5 */
	}
};

/**
 *	pdc202xx_init_one	-	called when a PDC202xx is found
 *	@dev: the pdc202xx device
 *	@id: the matching pci id
 *
 *	Called when the PCI registration layer (or the IDE initialization)
 *	finds a device matching our IDE device tables.
 */
 
static int __devinit pdc202xx_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	ide_pci_device_t *d = &pdc202xx_chipsets[id->driver_data];

	return d->init_setup(dev, d);
}

static struct pci_device_id pdc202xx_pci_tbl[] = {
	{ PCI_VENDOR_ID_PROMISE, PCI_DEVICE_ID_PROMISE_20246, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{ PCI_VENDOR_ID_PROMISE, PCI_DEVICE_ID_PROMISE_20262, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 1},
	{ PCI_VENDOR_ID_PROMISE, PCI_DEVICE_ID_PROMISE_20263, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 2},
	{ PCI_VENDOR_ID_PROMISE, PCI_DEVICE_ID_PROMISE_20265, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 3},
	{ PCI_VENDOR_ID_PROMISE, PCI_DEVICE_ID_PROMISE_20267, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 4},
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, pdc202xx_pci_tbl);

static struct pci_driver driver = {
	.name		= "Promise_Old_IDE",
	.id_table	= pdc202xx_pci_tbl,
	.probe		= pdc202xx_init_one,
};

static int __init pdc202xx_ide_init(void)
{
	return ide_pci_register_driver(&driver);
}

module_init(pdc202xx_ide_init);

MODULE_AUTHOR("Andre Hedrick, Frank Tiernan");
MODULE_DESCRIPTION("PCI driver module for older Promise IDE");
MODULE_LICENSE("GPL");
