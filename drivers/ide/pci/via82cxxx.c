/*
 *
 * Version 3.38
 *
 * VIA IDE driver for Linux. Supported southbridges:
 *
 *   vt82c576, vt82c586, vt82c586a, vt82c586b, vt82c596a, vt82c596b,
 *   vt82c686, vt82c686a, vt82c686b, vt8231, vt8233, vt8233c, vt8233a,
 *   vt8235, vt8237
 *
 * Copyright (c) 2000-2002 Vojtech Pavlik
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

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/blkdev.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/ide.h>
#include <asm/io.h>

#ifdef CONFIG_PPC_MULTIPLATFORM
#include <asm/processor.h>
#endif

#include "ide-timing.h"

#define DISPLAY_VIA_TIMINGS

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

#define VIA_UDMA		0x007
#define VIA_UDMA_NONE		0x000
#define VIA_UDMA_33		0x001
#define VIA_UDMA_66		0x002
#define VIA_UDMA_100		0x003
#define VIA_UDMA_133		0x004
#define VIA_BAD_PREQ		0x010	/* Crashes if PREQ# till DDACK# set */
#define VIA_BAD_CLK66		0x020	/* 66 MHz clock doesn't work correctly */
#define VIA_SET_FIFO		0x040	/* Needs to have FIFO split set */
#define VIA_NO_UNMASK		0x080	/* Doesn't work with IRQ unmasking on */
#define VIA_BAD_ID		0x100	/* Has wrong vendor ID (0x1107) */
#define VIA_BAD_AST		0x200	/* Don't touch Address Setup Timing */

/*
 * VIA SouthBridge chips.
 */

static struct via_isa_bridge {
	char *name;
	u16 id;
	u8 rev_min;
	u8 rev_max;
	u16 flags;
} via_isa_bridges[] = {
	{ "vt6410",	PCI_DEVICE_ID_VIA_6410,     0x00, 0x2f, VIA_UDMA_133 | VIA_BAD_AST },
	{ "vt8251",	PCI_DEVICE_ID_VIA_8251,     0x00, 0x2f, VIA_UDMA_133 | VIA_BAD_AST },
	{ "vt8237",	PCI_DEVICE_ID_VIA_8237,     0x00, 0x2f, VIA_UDMA_133 | VIA_BAD_AST },
	{ "vt8235",	PCI_DEVICE_ID_VIA_8235,     0x00, 0x2f, VIA_UDMA_133 | VIA_BAD_AST },
	{ "vt8233a",	PCI_DEVICE_ID_VIA_8233A,    0x00, 0x2f, VIA_UDMA_133 | VIA_BAD_AST },
	{ "vt8233c",	PCI_DEVICE_ID_VIA_8233C_0,  0x00, 0x2f, VIA_UDMA_100 },
	{ "vt8233",	PCI_DEVICE_ID_VIA_8233_0,   0x00, 0x2f, VIA_UDMA_100 },
	{ "vt8231",	PCI_DEVICE_ID_VIA_8231,     0x00, 0x2f, VIA_UDMA_100 },
	{ "vt82c686b",	PCI_DEVICE_ID_VIA_82C686,   0x40, 0x4f, VIA_UDMA_100 },
	{ "vt82c686a",	PCI_DEVICE_ID_VIA_82C686,   0x10, 0x2f, VIA_UDMA_66 },
	{ "vt82c686",	PCI_DEVICE_ID_VIA_82C686,   0x00, 0x0f, VIA_UDMA_33 | VIA_BAD_CLK66 },
	{ "vt82c596b",	PCI_DEVICE_ID_VIA_82C596,   0x10, 0x2f, VIA_UDMA_66 },
	{ "vt82c596a",	PCI_DEVICE_ID_VIA_82C596,   0x00, 0x0f, VIA_UDMA_33 | VIA_BAD_CLK66 },
	{ "vt82c586b",	PCI_DEVICE_ID_VIA_82C586_0, 0x47, 0x4f, VIA_UDMA_33 | VIA_SET_FIFO },
	{ "vt82c586b",	PCI_DEVICE_ID_VIA_82C586_0, 0x40, 0x46, VIA_UDMA_33 | VIA_SET_FIFO | VIA_BAD_PREQ },
	{ "vt82c586b",	PCI_DEVICE_ID_VIA_82C586_0, 0x30, 0x3f, VIA_UDMA_33 | VIA_SET_FIFO },
	{ "vt82c586a",	PCI_DEVICE_ID_VIA_82C586_0, 0x20, 0x2f, VIA_UDMA_33 | VIA_SET_FIFO },
	{ "vt82c586",	PCI_DEVICE_ID_VIA_82C586_0, 0x00, 0x0f, VIA_UDMA_NONE | VIA_SET_FIFO },
	{ "vt82c576",	PCI_DEVICE_ID_VIA_82C576,   0x00, 0x2f, VIA_UDMA_NONE | VIA_SET_FIFO | VIA_NO_UNMASK },
	{ "vt82c576",	PCI_DEVICE_ID_VIA_82C576,   0x00, 0x2f, VIA_UDMA_NONE | VIA_SET_FIFO | VIA_NO_UNMASK | VIA_BAD_ID },
	{ NULL }
};

static unsigned int via_clock;
static char *via_dma[] = { "MWDMA16", "UDMA33", "UDMA66", "UDMA100", "UDMA133" };

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
	struct via82cxxx_dev *vdev = ide_get_hwifdata(hwif);
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

	switch (vdev->via_config->flags & VIA_UDMA) {
		case VIA_UDMA_33:  t = timing->udma ? (0xe0 | (FIT(timing->udma, 2, 5) - 2)) : 0x03; break;
		case VIA_UDMA_66:  t = timing->udma ? (0xe8 | (FIT(timing->udma, 2, 9) - 2)) : 0x0f; break;
		case VIA_UDMA_100: t = timing->udma ? (0xe0 | (FIT(timing->udma, 2, 9) - 2)) : 0x07; break;
		case VIA_UDMA_133: t = timing->udma ? (0xe0 | (FIT(timing->udma, 2, 9) - 2)) : 0x07; break;
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
	struct via82cxxx_dev *vdev = ide_get_hwifdata(drive->hwif);
	struct ide_timing t, p;
	unsigned int T, UT;

	if (speed != XFER_PIO_SLOW)
		ide_config_drive_speed(drive, speed);

	T = 1000000000 / via_clock;

	switch (vdev->via_config->flags & VIA_UDMA) {
		case VIA_UDMA_33:   UT = T;   break;
		case VIA_UDMA_66:   UT = T/2; break;
		case VIA_UDMA_100:  UT = T/3; break;
		case VIA_UDMA_133:  UT = T/4; break;
		default: UT = T;
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
		via_set_drive(drive,
			ide_find_best_mode(drive, XFER_PIO | XFER_EPIO));
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
	ide_hwif_t *hwif = HWIF(drive);
	struct via82cxxx_dev *vdev = ide_get_hwifdata(hwif);
	u16 w80 = hwif->udma_four;

	u16 speed = ide_find_best_mode(drive,
		XFER_PIO | XFER_EPIO | XFER_SWDMA | XFER_MWDMA |
		(vdev->via_config->flags & VIA_UDMA ? XFER_UDMA : 0) |
		(w80 && (vdev->via_config->flags & VIA_UDMA) >= VIA_UDMA_66 ? XFER_UDMA_66 : 0) |
		(w80 && (vdev->via_config->flags & VIA_UDMA) >= VIA_UDMA_100 ? XFER_UDMA_100 : 0) |
		(w80 && (vdev->via_config->flags & VIA_UDMA) >= VIA_UDMA_133 ? XFER_UDMA_133 : 0));

	via_set_drive(drive, speed);

	if (drive->autodma && (speed & XFER_MODE) != XFER_PIO)
		return hwif->ide_dma_on(drive);
	return hwif->ide_dma_off_quietly(drive);
}

static struct via_isa_bridge *via_config_find(struct pci_dev **isa)
{
	struct via_isa_bridge *via_config;
	u8 t;

	for (via_config = via_isa_bridges; via_config->id; via_config++)
		if ((*isa = pci_find_device(PCI_VENDOR_ID_VIA +
			!!(via_config->flags & VIA_BAD_ID),
			via_config->id, NULL))) {

			pci_read_config_byte(*isa, PCI_REVISION_ID, &t);
			if (t >= via_config->rev_min &&
			    t <= via_config->rev_max)
				break;
		}

	return via_config;
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
	struct via_isa_bridge *via_config;
	u8 t, v;
	unsigned int u;

	/*
	 * Find the ISA bridge to see how good the IDE is.
	 */
	via_config = via_config_find(&isa);
	if (!via_config->id) {
		printk(KERN_WARNING "VP_IDE: Unknown VIA SouthBridge, disabling DMA.\n");
		return -ENODEV;
	}

	/*
	 * Setup or disable Clk66 if appropriate
	 */

	if ((via_config->flags & VIA_UDMA) == VIA_UDMA_66) {
		/* Enable Clk66 */
		pci_read_config_dword(dev, VIA_UDMA_TIMING, &u);
		pci_write_config_dword(dev, VIA_UDMA_TIMING, u|0x80008);
	} else if (via_config->flags & VIA_BAD_CLK66) {
		/* Would cause trouble on 596a and 686 */
		pci_read_config_dword(dev, VIA_UDMA_TIMING, &u);
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
	printk(KERN_INFO "VP_IDE: VIA %s (rev %02x) IDE %s "
		"controller on pci%s\n",
		via_config->name, t,
		via_dma[via_config->flags & VIA_UDMA],
		pci_name(dev));

	return 0;
}

/*
 * Check and handle 80-wire cable presence
 */
static void __devinit via_cable_detect(struct pci_dev *dev, struct via82cxxx_dev *vdev)
{
	unsigned int u;
	int i;
	pci_read_config_dword(dev, VIA_UDMA_TIMING, &u);

	switch (vdev->via_config->flags & VIA_UDMA) {

		case VIA_UDMA_66:
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

		case VIA_UDMA_100:
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

		case VIA_UDMA_133:
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

static void __devinit init_hwif_via82cxxx(ide_hwif_t *hwif)
{
	struct via82cxxx_dev *vdev = kmalloc(sizeof(struct via82cxxx_dev),
		GFP_KERNEL);
	struct pci_dev *isa = NULL;
	int i;

	if (vdev == NULL) {
		printk(KERN_ERR "VP_IDE: out of memory :(\n");
		return;
	}

	memset(vdev, 0, sizeof(struct via82cxxx_dev));
	ide_set_hwifdata(hwif, vdev);

	vdev->via_config = via_config_find(&isa);
	via_cable_detect(hwif->pci_dev, vdev);

	hwif->autodma = 0;

	hwif->tuneproc = &via82cxxx_tune_drive;
	hwif->speedproc = &via_set_drive;


#if defined(CONFIG_PPC_CHRP) && defined(CONFIG_PPC32)
	if(_machine == _MACH_chrp && _chrp_type == _CHRP_Pegasos) {
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
	hwif->ultra_mask = 0x7f;
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
	return ide_setup_pci_device(dev, &via82cxxx_chipsets[id->driver_data]);
}

static struct pci_device_id via_pci_tbl[] = {
	{ PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_82C576_1, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{ PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_82C586_1, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{ PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_6410,     PCI_ANY_ID, PCI_ANY_ID, 0, 0, 1},
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, via_pci_tbl);

static struct pci_driver driver = {
	.name 		= "VIA_IDE",
	.id_table 	= via_pci_tbl,
	.probe 		= via_init_one,
};

static int via_ide_init(void)
{
	return ide_pci_register_driver(&driver);
}

module_init(via_ide_init);

MODULE_AUTHOR("Vojtech Pavlik, Michel Aubry, Jeff Garzik, Andre Hedrick");
MODULE_DESCRIPTION("PCI driver module for VIA IDE");
MODULE_LICENSE("GPL");
