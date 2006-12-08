/*
 *  Promise TX2/TX4/TX2000/133 IDE driver
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 *  Split from:
 *  linux/drivers/ide/pdc202xx.c	Version 0.35	Mar. 30, 2002
 *  Copyright (C) 1998-2002		Andre Hedrick <andre@linux-ide.org>
 *  Portions Copyright (C) 1999 Promise Technology, Inc.
 *  Author: Frank Tiernan (frankt@promise.com)
 *  Released under terms of General Public License
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

#ifdef CONFIG_PPC_PMAC
#include <asm/prom.h>
#include <asm/pci-bridge.h>
#endif

#define PDC202_DEBUG_CABLE	0

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

#define set_2regs(a, b)					\
	do {						\
		hwif->OUTB((a + adj), indexreg);	\
		hwif->OUTB(b, datareg);			\
	} while(0)

#define set_ultra(a, b, c)				\
	do {						\
		set_2regs(0x10,(a));			\
		set_2regs(0x11,(b));			\
		set_2regs(0x12,(c));			\
	} while(0)

#define set_ata2(a, b)					\
	do {						\
		set_2regs(0x0e,(a));			\
		set_2regs(0x0f,(b));			\
	} while(0)

#define set_pio(a, b, c)				\
	do { 						\
		set_2regs(0x0c,(a));			\
		set_2regs(0x0d,(b));			\
		set_2regs(0x13,(c));			\
	} while(0)

static u8 pdcnew_ratemask (ide_drive_t *drive)
{
	u8 mode;

	switch(HWIF(drive)->pci_dev->device) {
		case PCI_DEVICE_ID_PROMISE_20277:
		case PCI_DEVICE_ID_PROMISE_20276:
		case PCI_DEVICE_ID_PROMISE_20275:
		case PCI_DEVICE_ID_PROMISE_20271:
		case PCI_DEVICE_ID_PROMISE_20269:
			mode = 4;
			break;
		case PCI_DEVICE_ID_PROMISE_20270:
		case PCI_DEVICE_ID_PROMISE_20268:
			mode = 3;
			break;
		default:
			return 0;
	}
	if (!eighty_ninty_three(drive))
		mode = min(mode, (u8)1);
	return mode;
}

static int check_in_drive_lists (ide_drive_t *drive, const char **list)
{
	struct hd_driveid *id = drive->id;

	if (pdc_quirk_drives == list) {
		while (*list) {
			if (strstr(id->model, *list++)) {
				return 2;
			}
		}
	} else {
		while (*list) {
			if (!strcmp(*list++,id->model)) {
				return 1;
			}
		}
	}
	return 0;
}

static int pdcnew_new_tune_chipset (ide_drive_t *drive, u8 xferspeed)
{
	ide_hwif_t *hwif	= HWIF(drive);
	unsigned long indexreg	= hwif->dma_vendor1;
	unsigned long datareg	= hwif->dma_vendor3;
	u8 thold		= 0x10;
	u8 adj			= (drive->dn%2) ? 0x08 : 0x00;
	u8 speed		= ide_rate_filter(pdcnew_ratemask(drive), xferspeed);

	if (speed == XFER_UDMA_2) {
		hwif->OUTB((thold + adj), indexreg);
		hwif->OUTB((hwif->INB(datareg) & 0x7f), datareg);
	}

	switch (speed) {
		case XFER_UDMA_7:
			speed = XFER_UDMA_6;
		case XFER_UDMA_6:	set_ultra(0x1a, 0x01, 0xcb); break;
		case XFER_UDMA_5:	set_ultra(0x1a, 0x02, 0xcb); break;
		case XFER_UDMA_4:	set_ultra(0x1a, 0x03, 0xcd); break;
		case XFER_UDMA_3:	set_ultra(0x1a, 0x05, 0xcd); break;
		case XFER_UDMA_2:	set_ultra(0x2a, 0x07, 0xcd); break;
		case XFER_UDMA_1:	set_ultra(0x3a, 0x0a, 0xd0); break;
		case XFER_UDMA_0:	set_ultra(0x4a, 0x0f, 0xd5); break;
		case XFER_MW_DMA_2:	set_ata2(0x69, 0x25); break;
		case XFER_MW_DMA_1:	set_ata2(0x6b, 0x27); break;
		case XFER_MW_DMA_0:	set_ata2(0xdf, 0x5f); break;
		case XFER_PIO_4:	set_pio(0x23, 0x09, 0x25); break;
		case XFER_PIO_3:	set_pio(0x27, 0x0d, 0x35); break;
		case XFER_PIO_2:	set_pio(0x23, 0x26, 0x64); break;
		case XFER_PIO_1:	set_pio(0x46, 0x29, 0xa4); break;
		case XFER_PIO_0:	set_pio(0xfb, 0x2b, 0xac); break;
		default:
			;
	}

	return (ide_config_drive_speed(drive, speed));
}

/*   0    1    2    3    4    5    6   7   8
 * 960, 480, 390, 300, 240, 180, 120, 90, 60
 *           180, 150, 120,  90,  60
 * DMA_Speed
 * 180, 120,  90,  90,  90,  60,  30
 *  11,   5,   4,   3,   2,   1,   0
 */
static void pdcnew_tune_drive(ide_drive_t *drive, u8 pio)
{
	u8 speed;

	if (pio == 5) pio = 4;
	speed = XFER_PIO_0 + ide_get_best_pio_mode(drive, 255, pio, NULL);

	(void)pdcnew_new_tune_chipset(drive, speed);
}

static u8 pdcnew_new_cable_detect (ide_hwif_t *hwif)
{
	hwif->OUTB(0x0b, hwif->dma_vendor1);
	return ((u8)((hwif->INB(hwif->dma_vendor3) & 0x04)));
}
static int config_chipset_for_dma (ide_drive_t *drive)
{
	struct hd_driveid *id	= drive->id;
	ide_hwif_t *hwif	= HWIF(drive);
	u8 speed		= -1;
	u8 cable;

	u8 ultra_66		= ((id->dma_ultra & 0x0010) ||
				   (id->dma_ultra & 0x0008)) ? 1 : 0;

	cable = pdcnew_new_cable_detect(hwif);

	if (ultra_66 && cable) {
		printk(KERN_WARNING "Warning: %s channel requires an 80-pin cable for operation.\n", hwif->channel ? "Secondary":"Primary");
		printk(KERN_WARNING "%s reduced to Ultra33 mode.\n", drive->name);
	}

	if (drive->media != ide_disk)
		return 0;
	if (id->capability & 4) {	/* IORDY_EN & PREFETCH_EN */
		hwif->OUTB((0x13 + ((drive->dn%2) ? 0x08 : 0x00)), hwif->dma_vendor1);
		hwif->OUTB((hwif->INB(hwif->dma_vendor3)|0x03), hwif->dma_vendor3);
	}

	speed = ide_dma_speed(drive, pdcnew_ratemask(drive));

	if (!(speed)) {
		hwif->tuneproc(drive, 5);
		return 0;
	}

	(void) hwif->speedproc(drive, speed);
	return ide_dma_enable(drive);
}

static int pdcnew_config_drive_xfer_rate (ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct hd_driveid *id	= drive->id;

	drive->init_speed = 0;

	if (id && (id->capability & 1) && drive->autodma) {

		if (ide_use_dma(drive)) {
			if (config_chipset_for_dma(drive))
				return hwif->ide_dma_on(drive);
		}

		goto fast_ata_pio;

	} else if ((id->capability & 8) || (id->field_valid & 2)) {
fast_ata_pio:
		hwif->tuneproc(drive, 5);
		return hwif->ide_dma_off_quietly(drive);
	}
	/* IORDY not supported */
	return 0;
}

static int pdcnew_quirkproc (ide_drive_t *drive)
{
	return ((int) check_in_drive_lists(drive, pdc_quirk_drives));
}

static int pdcnew_ide_dma_lostirq(ide_drive_t *drive)
{
	if (HWIF(drive)->resetproc != NULL)
		HWIF(drive)->resetproc(drive);
	return __ide_dma_lostirq(drive);
}

static int pdcnew_ide_dma_timeout(ide_drive_t *drive)
{
	if (HWIF(drive)->resetproc != NULL)
		HWIF(drive)->resetproc(drive);
	return __ide_dma_timeout(drive);
}

static void pdcnew_new_reset (ide_drive_t *drive)
{
	/*
	 * Deleted this because it is redundant from the caller.
	 */
	printk(KERN_WARNING "PDC202XX: %s channel reset.\n",
		HWIF(drive)->channel ? "Secondary" : "Primary");
}

#ifdef CONFIG_PPC_PMAC
static void __devinit apple_kiwi_init(struct pci_dev *pdev)
{
	struct device_node *np = pci_device_to_OF_node(pdev);
	unsigned int class_rev = 0;
	void __iomem *mmio;
	u8 conf;

	if (np == NULL || !device_is_compatible(np, "kiwi-root"))
		return;

	pci_read_config_dword(pdev, PCI_CLASS_REVISION, &class_rev);
	class_rev &= 0xff;

	if (class_rev >= 0x03) {
		/* Setup chip magic config stuff (from darwin) */
		pci_read_config_byte(pdev, 0x40, &conf);
		pci_write_config_byte(pdev, 0x40, conf | 0x01);
	}
	mmio = ioremap(pci_resource_start(pdev, 5),
				      pci_resource_len(pdev, 5));

	/* Setup some PLL stuffs */
	switch (pdev->device) {
	case PCI_DEVICE_ID_PROMISE_20270:
		writew(0x0d2b, mmio + 0x1202);
		mdelay(30);
		break;
	case PCI_DEVICE_ID_PROMISE_20271:
		writew(0x0826, mmio + 0x1202);
		mdelay(30);
		break;
	}

	iounmap(mmio);
}
#endif /* CONFIG_PPC_PMAC */

static unsigned int __devinit init_chipset_pdcnew(struct pci_dev *dev, const char *name)
{
	if (dev->resource[PCI_ROM_RESOURCE].start) {
		pci_write_config_dword(dev, PCI_ROM_ADDRESS,
			dev->resource[PCI_ROM_RESOURCE].start | PCI_ROM_ADDRESS_ENABLE);
		printk(KERN_INFO "%s: ROM enabled at 0x%08lx\n", name,
			(unsigned long)dev->resource[PCI_ROM_RESOURCE].start);
	}

#ifdef CONFIG_PPC_PMAC
	apple_kiwi_init(dev);
#endif

	return dev->irq;
}

static void __devinit init_hwif_pdc202new(ide_hwif_t *hwif)
{
	hwif->autodma = 0;

	hwif->tuneproc  = &pdcnew_tune_drive;
	hwif->quirkproc = &pdcnew_quirkproc;
	hwif->speedproc = &pdcnew_new_tune_chipset;
	hwif->resetproc = &pdcnew_new_reset;

	hwif->drives[0].autotune = hwif->drives[1].autotune = 1;

	hwif->ultra_mask = 0x7f;
	hwif->mwdma_mask = 0x07;

	hwif->err_stops_fifo = 1;

	hwif->ide_dma_check = &pdcnew_config_drive_xfer_rate;
	hwif->ide_dma_lostirq = &pdcnew_ide_dma_lostirq;
	hwif->ide_dma_timeout = &pdcnew_ide_dma_timeout;
	if (!(hwif->udma_four))
		hwif->udma_four = (pdcnew_new_cable_detect(hwif)) ? 0 : 1;
	if (!noautodma)
		hwif->autodma = 1;
	hwif->drives[0].autodma = hwif->drives[1].autodma = hwif->autodma;
#if PDC202_DEBUG_CABLE
	printk(KERN_DEBUG "%s: %s-pin cable\n",
		hwif->name, hwif->udma_four ? "80" : "40");
#endif /* PDC202_DEBUG_CABLE */
}

static int __devinit init_setup_pdcnew(struct pci_dev *dev, ide_pci_device_t *d)
{
	return ide_setup_pci_device(dev, d);
}

static int __devinit init_setup_pdc20270(struct pci_dev *dev,
					 ide_pci_device_t *d)
{
	struct pci_dev *findev = NULL;
	int ret;

	if ((dev->bus->self &&
	     dev->bus->self->vendor == PCI_VENDOR_ID_DEC) &&
	    (dev->bus->self->device == PCI_DEVICE_ID_DEC_21150)) {
		if (PCI_SLOT(dev->devfn) & 2)
			return -ENODEV;
		d->extra = 0;
		while ((findev = pci_get_device(PCI_ANY_ID, PCI_ANY_ID, findev)) != NULL) {
			if ((findev->vendor == dev->vendor) &&
			    (findev->device == dev->device) &&
			    (PCI_SLOT(findev->devfn) & 2)) {
				if (findev->irq != dev->irq) {
					findev->irq = dev->irq;
				}
				ret = ide_setup_pci_devices(dev, findev, d);
				pci_dev_put(findev);
				return ret;
			}
		}
	}
	return ide_setup_pci_device(dev, d);
}

static int __devinit init_setup_pdc20276(struct pci_dev *dev,
					 ide_pci_device_t *d)
{
	if ((dev->bus->self) &&
	    (dev->bus->self->vendor == PCI_VENDOR_ID_INTEL) &&
	    ((dev->bus->self->device == PCI_DEVICE_ID_INTEL_I960) ||
	     (dev->bus->self->device == PCI_DEVICE_ID_INTEL_I960RM))) {
		printk(KERN_INFO "ide: Skipping Promise PDC20276 "
			"attached to I2O RAID controller.\n");
		return -ENODEV;
	}
	return ide_setup_pci_device(dev, d);
}

static ide_pci_device_t pdcnew_chipsets[] __devinitdata = {
	{	/* 0 */
		.name		= "PDC20268",
		.init_setup	= init_setup_pdcnew,
		.init_chipset	= init_chipset_pdcnew,
		.init_hwif	= init_hwif_pdc202new,
		.channels	= 2,
		.autodma	= AUTODMA,
		.bootable	= OFF_BOARD,
	},{	/* 1 */
		.name		= "PDC20269",
		.init_setup	= init_setup_pdcnew,
		.init_chipset	= init_chipset_pdcnew,
		.init_hwif	= init_hwif_pdc202new,
		.channels	= 2,
		.autodma	= AUTODMA,
		.bootable	= OFF_BOARD,
	},{	/* 2 */
		.name		= "PDC20270",
		.init_setup	= init_setup_pdc20270,
		.init_chipset	= init_chipset_pdcnew,
		.init_hwif	= init_hwif_pdc202new,
		.channels	= 2,
		.autodma	= AUTODMA,
		.bootable	= OFF_BOARD,
	},{	/* 3 */
		.name		= "PDC20271",
		.init_setup	= init_setup_pdcnew,
		.init_chipset	= init_chipset_pdcnew,
		.init_hwif	= init_hwif_pdc202new,
		.channels	= 2,
		.autodma	= AUTODMA,
		.bootable	= OFF_BOARD,
	},{	/* 4 */
		.name		= "PDC20275",
		.init_setup	= init_setup_pdcnew,
		.init_chipset	= init_chipset_pdcnew,
		.init_hwif	= init_hwif_pdc202new,
		.channels	= 2,
		.autodma	= AUTODMA,
		.bootable	= OFF_BOARD,
	},{	/* 5 */
		.name		= "PDC20276",
		.init_setup	= init_setup_pdc20276,
		.init_chipset	= init_chipset_pdcnew,
		.init_hwif	= init_hwif_pdc202new,
		.channels	= 2,
		.autodma	= AUTODMA,
		.bootable	= OFF_BOARD,
	},{	/* 6 */
		.name		= "PDC20277",
		.init_setup	= init_setup_pdcnew,
		.init_chipset	= init_chipset_pdcnew,
		.init_hwif	= init_hwif_pdc202new,
		.channels	= 2,
		.autodma	= AUTODMA,
		.bootable	= OFF_BOARD,
	}
};

/**
 *	pdc202new_init_one	-	called when a pdc202xx is found
 *	@dev: the pdc202new device
 *	@id: the matching pci id
 *
 *	Called when the PCI registration layer (or the IDE initialization)
 *	finds a device matching our IDE device tables.
 */
 
static int __devinit pdc202new_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	ide_pci_device_t *d = &pdcnew_chipsets[id->driver_data];

	return d->init_setup(dev, d);
}

static struct pci_device_id pdc202new_pci_tbl[] = {
	{ PCI_VENDOR_ID_PROMISE, PCI_DEVICE_ID_PROMISE_20268, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{ PCI_VENDOR_ID_PROMISE, PCI_DEVICE_ID_PROMISE_20269, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 1},
	{ PCI_VENDOR_ID_PROMISE, PCI_DEVICE_ID_PROMISE_20270, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 2},
	{ PCI_VENDOR_ID_PROMISE, PCI_DEVICE_ID_PROMISE_20271, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 3},
	{ PCI_VENDOR_ID_PROMISE, PCI_DEVICE_ID_PROMISE_20275, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 4},
	{ PCI_VENDOR_ID_PROMISE, PCI_DEVICE_ID_PROMISE_20276, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 5},
	{ PCI_VENDOR_ID_PROMISE, PCI_DEVICE_ID_PROMISE_20277, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 6},
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, pdc202new_pci_tbl);

static struct pci_driver driver = {
	.name		= "Promise_IDE",
	.id_table	= pdc202new_pci_tbl,
	.probe		= pdc202new_init_one,
};

static int pdc202new_ide_init(void)
{
	return ide_pci_register_driver(&driver);
}

module_init(pdc202new_ide_init);

MODULE_AUTHOR("Andre Hedrick, Frank Tiernan");
MODULE_DESCRIPTION("PCI driver module for Promise PDC20268 and higher");
MODULE_LICENSE("GPL");
