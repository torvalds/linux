/*
 *
 * Version 3.40
 *
 * VIA IDE driver for Linux. Supported southbridges:
 *
 *   vt82c576, vt82c586, vt82c586a, vt82c586b, vt82c596a, vt82c596b,
 *   vt82c686, vt82c686a, vt82c686b, vt8231, vt8233, vt8233c, vt8233a,
 *   vt8235, vt8237, vt8237a
 *
 * Copyright (c) 2000-2002 Vojtech Pavlik
 * Copyright (c) 2007 Bartlomiej Zolnierkiewicz
 *
 * Based on the work of:
 *	Michel Aubry
 *	Jeff Garzik
 *	Andre Hedrick
 *
 * Documentation:
 *	Obsolete device documentation publically available from via.com.tw
 *	Current device documentation available under NDA only
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/blkdev.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/ide.h>
#include <asm/io.h>

#ifdef CONFIG_PPC_CHRP
#include <asm/processor.h>
#endif

#include "ide-timing.h"

#define VIA_IDE_ENABLE		0x40
#define VIA_IDE_CONFIG		0x41
#define VIA_FIFO_CONFIG		0x43
#define VIA_MISC_1		0x44
#define VIA_MISC_2		0x45
#define VIA_MISC_3		0x46
#define VIA_DRIVE_TIMING	0x48
#define VIA_8BIT_TIMING		0x4e
#define VIA_ADDRESS_SETUP	0x4c
#define VIA_UDMA_TIMING		0x50

#define VIA_BAD_PREQ		0x01 /* Crashes if PREQ# till DDACK# set */
#define VIA_BAD_CLK66		0x02 /* 66 MHz clock doesn't work correctly */
#define VIA_SET_FIFO		0x04 /* Needs to have FIFO split set */
#define VIA_NO_UNMASK		0x08 /* Doesn't work with IRQ unmasking on */
#define VIA_BAD_ID		0x10 /* Has wrong vendor ID (0x1107) */
#define VIA_BAD_AST		0x20 /* Don't touch Address Setup Timing */

/*
 * VIA SouthBridge chips.
 */

static struct via_isa_bridge {
	char *name;
	u16 id;
	u8 rev_min;
	u8 rev_max;
	u8 udma_mask;
	u8 flags;
} via_isa_bridges[] = {
	{ "cx700",	PCI_DEVICE_ID_VIA_CX700,    0x00, 0x2f, ATA_UDMA6, VIA_BAD_AST },
	{ "vt8237s",	PCI_DEVICE_ID_VIA_8237S,    0x00, 0x2f, ATA_UDMA6, VIA_BAD_AST },
	{ "vt6410",	PCI_DEVICE_ID_VIA_6410,     0x00, 0x2f, ATA_UDMA6, VIA_BAD_AST },
	{ "vt8251",	PCI_DEVICE_ID_VIA_8251,     0x00, 0x2f, ATA_UDMA6, VIA_BAD_AST },
	{ "vt8237",	PCI_DEVICE_ID_VIA_8237,     0x00, 0x2f, ATA_UDMA6, VIA_BAD_AST },
	{ "vt8237a",	PCI_DEVICE_ID_VIA_8237A,    0x00, 0x2f, ATA_UDMA6, VIA_BAD_AST },
	{ "vt8235",	PCI_DEVICE_ID_VIA_8235,     0x00, 0x2f, ATA_UDMA6, VIA_BAD_AST },
	{ "vt8233a",	PCI_DEVICE_ID_VIA_8233A,    0x00, 0x2f, ATA_UDMA6, VIA_BAD_AST },
	{ "vt8233c",	PCI_DEVICE_ID_VIA_8233C_0,  0x00, 0x2f, ATA_UDMA5, },
	{ "vt8233",	PCI_DEVICE_ID_VIA_8233_0,   0x00, 0x2f, ATA_UDMA5, },
	{ "vt8231",	PCI_DEVICE_ID_VIA_8231,     0x00, 0x2f, ATA_UDMA5, },
	{ "vt82c686b",	PCI_DEVICE_ID_VIA_82C686,   0x40, 0x4f, ATA_UDMA5, },
	{ "vt82c686a",	PCI_DEVICE_ID_VIA_82C686,   0x10, 0x2f, ATA_UDMA4, },
	{ "vt82c686",	PCI_DEVICE_ID_VIA_82C686,   0x00, 0x0f, ATA_UDMA2, VIA_BAD_CLK66 },
	{ "vt82c596b",	PCI_DEVICE_ID_VIA_82C596,   0x10, 0x2f, ATA_UDMA4, },
	{ "vt82c596a",	PCI_DEVICE_ID_VIA_82C596,   0x00, 0x0f, ATA_UDMA2, VIA_BAD_CLK66 },
	{ "vt82c586b",	PCI_DEVICE_ID_VIA_82C586_0, 0x47, 0x4f, ATA_UDMA2, VIA_SET_FIFO },
	{ "vt82c586b",	PCI_DEVICE_ID_VIA_82C586_0, 0x40, 0x46, ATA_UDMA2, VIA_SET_FIFO | VIA_BAD_PREQ },
	{ "vt82c586b",	PCI_DEVICE_ID_VIA_82C586_0, 0x30, 0x3f, ATA_UDMA2, VIA_SET_FIFO },
	{ "vt82c586a",	PCI_DEVICE_ID_VIA_82C586_0, 0x20, 0x2f, ATA_UDMA2, VIA_SET_FIFO },
	{ "vt82c586",	PCI_DEVICE_ID_VIA_82C586_0, 0x00, 0x0f,      0x00, VIA_SET_FIFO },
	{ "vt82c576",	PCI_DEVICE_ID_VIA_82C576,   0x00, 0x2f,      0x00, VIA_SET_FIFO | VIA_NO_UNMASK },
	{ "vt82c576",	PCI_DEVICE_ID_VIA_82C576,   0x00, 0x2f,      0x00, VIA_SET_FIFO | VIA_NO_UNMASK | VIA_BAD_ID },
	{ NULL }
};

static unsigned int via_clock;
static char *via_dma[] = { "16", "25", "33", "44", "66", "100", "133" };

struct via82cxxx_dev
{
	struct via_isa_bridge *via_config;
	unsigned int via_80w;
};

/**
 *	via_set_speed			-	write timing registers
 *	@dev: PCI device
 *	@dn: device
 *	@timing: IDE timing data to use
 *
 *	via_set_speed writes timing values to the chipset registers
 */

static void via_set_speed(ide_hwif_t *hwif, u8 dn, struct ide_timing *timing)
{
	struct pci_dev *dev = hwif->pci_dev;
	struct via82cxxx_dev *vdev = pci_get_drvdata(hwif->pci_dev);
	u8 t;

	if (~vdev->via_config->flags & VIA_BAD_AST) {
		pci_read_config_byte(dev, VIA_ADDRESS_SETUP, &t);
		t = (t & ~(3 << ((3 - dn) << 1))) | ((FIT(timing->setup, 1, 4) - 1) << ((3 - dn) << 1));
		pci_write_config_byte(dev, VIA_ADDRESS_SETUP, t);
	}

	pci_write_config_byte(dev, VIA_8BIT_TIMING + (1 - (dn >> 1)),
		((FIT(timing->act8b, 1, 16) - 1) << 4) | (FIT(timing->rec8b, 1, 16) - 1));

	pci_write_config_byte(dev, VIA_DRIVE_TIMING + (3 - dn),
		((FIT(timing->active, 1, 16) - 1) << 4) | (FIT(timing->recover, 1, 16) - 1));

	switch (vdev->via_config->udma_mask) {
	case ATA_UDMA2: t = timing->udma ? (0xe0 | (FIT(timing->udma, 2, 5) - 2)) : 0x03; break;
	case ATA_UDMA4: t = timing->udma ? (0xe8 | (FIT(timing->udma, 2, 9) - 2)) : 0x0f; break;
	case ATA_UDMA5: t = timing->udma ? (0xe0 | (FIT(timing->udma, 2, 9) - 2)) : 0x07; break;
	case ATA_UDMA6: t = timing->udma ? (0xe0 | (FIT(timing->udma, 2, 9) - 2)) : 0x07; break;
	default: return;
	}

	pci_write_config_byte(dev, VIA_UDMA_TIMING + (3 - dn), t);
}

/**
 *	via_set_drive		-	configure transfer mode
 *	@drive: Drive to set up
 *	@speed: desired speed
 *
 *	via_set_drive() computes timing values configures the drive and
 *	the chipset to a desired transfer mode. It also can be called
 *	by upper layers.
 */

static int via_set_drive(ide_drive_t *drive, u8 speed)
{
	ide_drive_t *peer = HWIF(drive)->drives + (~drive->dn & 1);
	struct via82cxxx_dev *vdev = pci_get_drvdata(drive->hwif->pci_dev);
	struct ide_timing t, p;
	unsigned int T, UT;

	if (speed != XFER_PIO_SLOW)
		ide_config_drive_speed(drive, speed);

	T = 1000000000 / via_clock;

	switch (vdev->via_config->udma_mask) {
	case ATA_UDMA2: UT = T;   break;
	case ATA_UDMA4: UT = T/2; break;
	case ATA_UDMA5: UT = T/3; break;
	case ATA_UDMA6: UT = T/4; break;
	default:	UT = T;
	}

	ide_timing_compute(drive, speed, &t, T, UT);

	if (peer->present) {
		ide_timing_compute(peer, peer->current_speed, &p, T, UT);
		ide_timing_merge(&p, &t, &t, IDE_TIMING_8BIT);
	}

	via_set_speed(HWIF(drive), drive->dn, &t);

	if (!drive->init_speed)
		drive->init_speed = speed;
	drive->current_speed = speed;

	return 0;
}

/**
 *	via82cxxx_tune_drive	-	PIO setup
 *	@drive: drive to set up
 *	@pio: mode to use (255 for 'best possible')
 *
 *	A callback from the upper layers for PIO-only tuning.
 */

static void via82cxxx_tune_drive(ide_drive_t *drive, u8 pio)
{
	if (pio == 255) {
		via_set_drive(drive, ide_find_best_pio_mode(drive));
		return;
	}

	via_set_drive(drive, XFER_PIO_0 + min_t(u8, pio, 5));
}

/**
 *	via82cxxx_ide_dma_check		-	set up for DMA if possible
 *	@drive: IDE drive to set up
 *
 *	Set up the drive for the highest supported speed considering the
 *	driver, controller and cable
 */
 
static int via82cxxx_ide_dma_check (ide_drive_t *drive)
{
	u8 speed = ide_max_dma_mode(drive);

	if (speed == 0)
		speed = ide_find_best_pio_mode(drive);

	via_set_drive(drive, speed);

	if (drive->autodma && (speed & XFER_MODE) != XFER_PIO)
		return 0;

	return -1;
}

static struct via_isa_bridge *via_config_find(struct pci_dev **isa)
{
	struct via_isa_bridge *via_config;
	u8 t;

	for (via_config = via_isa_bridges; via_config->id; via_config++)
		if ((*isa = pci_get_device(PCI_VENDOR_ID_VIA +
			!!(via_config->flags & VIA_BAD_ID),
			via_config->id, NULL))) {

			pci_read_config_byte(*isa, PCI_REVISION_ID, &t);
			if (t >= via_config->rev_min &&
			    t <= via_config->rev_max)
				break;
			pci_dev_put(*isa);
		}

	return via_config;
}

/*
 * Check and handle 80-wire cable presence
 */
static void __devinit via_cable_detect(struct via82cxxx_dev *vdev, u32 u)
{
	int i;

	switch (vdev->via_config->udma_mask) {
		case ATA_UDMA4:
			for (i = 24; i >= 0; i -= 8)
				if (((u >> (i & 16)) & 8) &&
				    ((u >> i) & 0x20) &&
				     (((u >> i) & 7) < 2)) {
					/*
					 * 2x PCI clock and
					 * UDMA w/ < 3T/cycle
					 */
					vdev->via_80w |= (1 << (1 - (i >> 4)));
				}
			break;

		case ATA_UDMA5:
			for (i = 24; i >= 0; i -= 8)
				if (((u >> i) & 0x10) ||
				    (((u >> i) & 0x20) &&
				     (((u >> i) & 7) < 4))) {
					/* BIOS 80-wire bit or
					 * UDMA w/ < 60ns/cycle
					 */
					vdev->via_80w |= (1 << (1 - (i >> 4)));
				}
			break;

		case ATA_UDMA6:
			for (i = 24; i >= 0; i -= 8)
				if (((u >> i) & 0x10) ||
				    (((u >> i) & 0x20) &&
				     (((u >> i) & 7) < 6))) {
					/* BIOS 80-wire bit or
					 * UDMA w/ < 60ns/cycle
					 */
					vdev->via_80w |= (1 << (1 - (i >> 4)));
				}
			break;
	}
}

/**
 *	init_chipset_via82cxxx	-	initialization handler
 *	@dev: PCI device
 *	@name: Name of interface
 *
 *	The initialization callback. Here we determine the IDE chip type
 *	and initialize its drive independent registers.
 */

static unsigned int __devinit init_chipset_via82cxxx(struct pci_dev *dev, const char *name)
{
	struct pci_dev *isa = NULL;
	struct via82cxxx_dev *vdev;
	struct via_isa_bridge *via_config;
	u8 t, v;
	u32 u;

	vdev = kzalloc(sizeof(*vdev), GFP_KERNEL);
	if (!vdev) {
		printk(KERN_ERR "VP_IDE: out of memory :(\n");
		return -ENOMEM;
	}
	pci_set_drvdata(dev, vdev);

	/*
	 * Find the ISA bridge to see how good the IDE is.
	 */
	vdev->via_config = via_config = via_config_find(&isa);

	/* We checked this earlier so if it fails here deeep badness
	   is involved */

	BUG_ON(!via_config->id);

	/*
	 * Detect cable and configure Clk66
	 */
	pci_read_config_dword(dev, VIA_UDMA_TIMING, &u);

	via_cable_detect(vdev, u);

	if (via_config->udma_mask == ATA_UDMA4) {
		/* Enable Clk66 */
		pci_write_config_dword(dev, VIA_UDMA_TIMING, u|0x80008);
	} else if (via_config->flags & VIA_BAD_CLK66) {
		/* Would cause trouble on 596a and 686 */
		pci_write_config_dword(dev, VIA_UDMA_TIMING, u & ~0x80008);
	}

	/*
	 * Check whether interfaces are enabled.
	 */

	pci_read_config_byte(dev, VIA_IDE_ENABLE, &v);

	/*
	 * Set up FIFO sizes and thresholds.
	 */

	pci_read_config_byte(dev, VIA_FIFO_CONFIG, &t);

	/* Disable PREQ# till DDACK# */
	if (via_config->flags & VIA_BAD_PREQ) {
		/* Would crash on 586b rev 41 */
		t &= 0x7f;
	}

	/* Fix FIFO split between channels */
	if (via_config->flags & VIA_SET_FIFO) {
		t &= (t & 0x9f);
		switch (v & 3) {
			case 2: t |= 0x00; break;	/* 16 on primary */
			case 1: t |= 0x60; break;	/* 16 on secondary */
			case 3: t |= 0x20; break;	/* 8 pri 8 sec */
		}
	}

	pci_write_config_byte(dev, VIA_FIFO_CONFIG, t);

	/*
	 * Determine system bus clock.
	 */

	via_clock = system_bus_clock() * 1000;

	switch (via_clock) {
		case 33000: via_clock = 33333; break;
		case 37000: via_clock = 37500; break;
		case 41000: via_clock = 41666; break;
	}

	if (via_clock < 20000 || via_clock > 50000) {
		printk(KERN_WARNING "VP_IDE: User given PCI clock speed "
			"impossible (%d), using 33 MHz instead.\n", via_clock);
		printk(KERN_WARNING "VP_IDE: Use ide0=ata66 if you want "
			"to assume 80-wire cable.\n");
		via_clock = 33333;
	}

	/*
	 * Print the boot message.
	 */

	pci_read_config_byte(isa, PCI_REVISION_ID, &t);
	printk(KERN_INFO "VP_IDE: VIA %s (rev %02x) IDE %sDMA%s "
		"controller on pci%s\n",
		via_config->name, t,
		via_config->udma_mask ? "U" : "MW",
		via_dma[via_config->udma_mask ?
			(fls(via_config->udma_mask) - 1) : 0],
		pci_name(dev));

	pci_dev_put(isa);
	return 0;
}

static void __devinit init_hwif_via82cxxx(ide_hwif_t *hwif)
{
	struct via82cxxx_dev *vdev = pci_get_drvdata(hwif->pci_dev);
	int i;

	hwif->autodma = 0;

	hwif->tuneproc = &via82cxxx_tune_drive;
	hwif->speedproc = &via_set_drive;


#ifdef CONFIG_PPC_CHRP
	if(machine_is(chrp) && _chrp_type == _CHRP_Pegasos) {
		hwif->irq = hwif->channel ? 15 : 14;
	}
#endif

	for (i = 0; i < 2; i++) {
		hwif->drives[i].io_32bit = 1;
		hwif->drives[i].unmask = (vdev->via_config->flags & VIA_NO_UNMASK) ? 0 : 1;
		hwif->drives[i].autotune = 1;
		hwif->drives[i].dn = hwif->channel * 2 + i;
	}

	if (!hwif->dma_base)
		return;

	hwif->atapi_dma = 1;

	hwif->ultra_mask = vdev->via_config->udma_mask;
	hwif->mwdma_mask = 0x07;
	hwif->swdma_mask = 0x07;

	if (!hwif->udma_four)
		hwif->udma_four = (vdev->via_80w >> hwif->channel) & 1;
	hwif->ide_dma_check = &via82cxxx_ide_dma_check;
	if (!noautodma)
		hwif->autodma = 1;
	hwif->drives[0].autodma = hwif->autodma;
	hwif->drives[1].autodma = hwif->autodma;
}

static ide_pci_device_t via82cxxx_chipsets[] __devinitdata = {
	{	/* 0 */
		.name		= "VP_IDE",
		.init_chipset	= init_chipset_via82cxxx,
		.init_hwif	= init_hwif_via82cxxx,
		.channels	= 2,
		.autodma	= NOAUTODMA,
		.enablebits	= {{0x40,0x02,0x02}, {0x40,0x01,0x01}},
		.bootable	= ON_BOARD
	},{	/* 1 */
		.name		= "VP_IDE",
		.init_chipset	= init_chipset_via82cxxx,
		.init_hwif	= init_hwif_via82cxxx,
		.channels	= 2,
		.autodma	= AUTODMA,
		.enablebits	= {{0x00,0x00,0x00}, {0x00,0x00,0x00}},
		.bootable	= ON_BOARD,
	}
};

static int __devinit via_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	struct pci_dev *isa = NULL;
	struct via_isa_bridge *via_config;
	/*
	 * Find the ISA bridge and check we know what it is.
	 */
	via_config = via_config_find(&isa);
	pci_dev_put(isa);
	if (!via_config->id) {
		printk(KERN_WARNING "VP_IDE: Unknown VIA SouthBridge, disabling DMA.\n");
		return -ENODEV;
	}
	return ide_setup_pci_device(dev, &via82cxxx_chipsets[id->driver_data]);
}

static struct pci_device_id via_pci_tbl[] = {
	{ PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_82C576_1, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{ PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_82C586_1, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{ PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_6410,     PCI_ANY_ID, PCI_ANY_ID, 0, 0, 1},
	{ PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_SATA_EIDE,     PCI_ANY_ID, PCI_ANY_ID, 0, 0, 1},
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, via_pci_tbl);

static struct pci_driver driver = {
	.name 		= "VIA_IDE",
	.id_table 	= via_pci_tbl,
	.probe 		= via_init_one,
};

static int __init via_ide_init(void)
{
	return ide_pci_register_driver(&driver);
}

module_init(via_ide_init);

MODULE_AUTHOR("Vojtech Pavlik, Michel Aubry, Jeff Garzik, Andre Hedrick");
MODULE_DESCRIPTION("PCI driver module for VIA IDE");
MODULE_LICENSE("GPL");
