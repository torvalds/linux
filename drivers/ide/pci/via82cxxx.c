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

static struct via_isa_bridge *via_config;
static unsigned int via_80w;
static unsigned int via_clock;
static char *via_dma[] = { "MWDMA16", "UDMA33", "UDMA66", "UDMA100", "UDMA133" };

/*
 * VIA /proc entry.
 */

#if defined(DISPLAY_VIA_TIMINGS) && defined(CONFIG_PROC_FS)

#include <linux/stat.h>
#include <linux/proc_fs.h>

static u8 via_proc = 0;
static unsigned long via_base;
static struct pci_dev *bmide_dev, *isa_dev;

static char *via_control3[] = { "No limit", "64", "128", "192" };

#define via_print(format, arg...) p += sprintf(p, format "\n" , ## arg)
#define via_print_drive(name, format, arg...)\
	p += sprintf(p, name); for (i = 0; i < 4; i++) p += sprintf(p, format, ## arg); p += sprintf(p, "\n");


/**
 *	via_get_info		-	generate via /proc file 
 *	@buffer: buffer for data
 *	@addr: set to start of data to use
 *	@offset: current file offset
 *	@count: size of read
 *
 *	Fills in buffer with the debugging/configuration information for
 *	the VIA chipset tuning and attached drives
 */
 
static int via_get_info(char *buffer, char **addr, off_t offset, int count)
{
	int speed[4], cycle[4], setup[4], active[4], recover[4], den[4],
		 uen[4], udma[4], umul[4], active8b[4], recover8b[4];
	struct pci_dev *dev = bmide_dev;
	unsigned int v, u, i;
	int len;
	u16 c, w;
	u8 t, x;
	char *p = buffer;

	via_print("----------VIA BusMastering IDE Configuration"
		"----------------");

	via_print("Driver Version:                     3.38");
	via_print("South Bridge:                       VIA %s",
		via_config->name);

	pci_read_config_byte(isa_dev, PCI_REVISION_ID, &t);
	pci_read_config_byte(dev, PCI_REVISION_ID, &x);
	via_print("Revision:                           ISA %#x IDE %#x", t, x);
	via_print("Highest DMA rate:                   %s",
		via_dma[via_config->flags & VIA_UDMA]);

	via_print("BM-DMA base:                        %#lx", via_base);
	via_print("PCI clock:                          %d.%dMHz",
		via_clock / 1000, via_clock / 100 % 10);

	pci_read_config_byte(dev, VIA_MISC_1, &t);
	via_print("Master Read  Cycle IRDY:            %dws",
		(t & 64) >> 6);
	via_print("Master Write Cycle IRDY:            %dws",
		(t & 32) >> 5);
	via_print("BM IDE Status Register Read Retry:  %s",
		(t & 8) ? "yes" : "no");

	pci_read_config_byte(dev, VIA_MISC_3, &t);
	via_print("Max DRDY Pulse Width:               %s%s",
		via_control3[(t & 0x03)], (t & 0x03) ? " PCI clocks" : "");

	via_print("-----------------------Primary IDE"
		"-------Secondary IDE------");
	via_print("Read DMA FIFO flush:   %10s%20s",
		(t & 0x80) ? "yes" : "no", (t & 0x40) ? "yes" : "no");
	via_print("End Sector FIFO flush: %10s%20s",
		(t & 0x20) ? "yes" : "no", (t & 0x10) ? "yes" : "no");

	pci_read_config_byte(dev, VIA_IDE_CONFIG, &t);
	via_print("Prefetch Buffer:       %10s%20s",
		(t & 0x80) ? "yes" : "no", (t & 0x20) ? "yes" : "no");
	via_print("Post Write Buffer:     %10s%20s",
		(t & 0x40) ? "yes" : "no", (t & 0x10) ? "yes" : "no");

	pci_read_config_byte(dev, VIA_IDE_ENABLE, &t);
	via_print("Enabled:               %10s%20s",
		(t & 0x02) ? "yes" : "no", (t & 0x01) ? "yes" : "no");

	c = inb(via_base + 0x02) | (inb(via_base + 0x0a) << 8);
	via_print("Simplex only:          %10s%20s",
		(c & 0x80) ? "yes" : "no", (c & 0x8000) ? "yes" : "no");

	via_print("Cable Type:            %10s%20s",
		(via_80w & 1) ? "80w" : "40w", (via_80w & 2) ? "80w" : "40w");

	via_print("-------------------drive0----drive1"
		"----drive2----drive3-----");

	pci_read_config_byte(dev, VIA_ADDRESS_SETUP, &t);
	pci_read_config_dword(dev, VIA_DRIVE_TIMING, &v);
	pci_read_config_word(dev, VIA_8BIT_TIMING, &w);

	if (via_config->flags & VIA_UDMA)
		pci_read_config_dword(dev, VIA_UDMA_TIMING, &u);
	else u = 0;

	for (i = 0; i < 4; i++) {

		setup[i]     = ((t >> ((3 - i) << 1)) & 0x3) + 1;
		recover8b[i] = ((w >> ((1 - (i >> 1)) << 3)) & 0xf) + 1;
		active8b[i]  = ((w >> (((1 - (i >> 1)) << 3) + 4)) & 0xf) + 1;
		active[i]    = ((v >> (((3 - i) << 3) + 4)) & 0xf) + 1;
		recover[i]   = ((v >> ((3 - i) << 3)) & 0xf) + 1;
		udma[i]      = ((u >> ((3 - i) << 3)) & 0x7) + 2;
		umul[i]      = ((u >> (((3 - i) & 2) << 3)) & 0x8) ? 1 : 2;
		uen[i]       = ((u >> ((3 - i) << 3)) & 0x20);
		den[i]       = (c & ((i & 1) ? 0x40 : 0x20) << ((i & 2) << 2));

		speed[i] = 2 * via_clock / (active[i] + recover[i]);
		cycle[i] = 1000000 * (active[i] + recover[i]) / via_clock;

		if (!uen[i] || !den[i])
			continue;

		switch (via_config->flags & VIA_UDMA) {

			case VIA_UDMA_33:
				speed[i] = 2 * via_clock / udma[i];
				cycle[i] = 1000000 * udma[i] / via_clock;
				break;

			case VIA_UDMA_66:
				speed[i] = 4 * via_clock / (udma[i] * umul[i]);
				cycle[i] = 500000 * (udma[i] * umul[i]) / via_clock;
				break;

			case VIA_UDMA_100:
				speed[i] = 6 * via_clock / udma[i];
				cycle[i] = 333333 * udma[i] / via_clock;
				break;

			case VIA_UDMA_133:
				speed[i] = 8 * via_clock / udma[i];
				cycle[i] = 250000 * udma[i] / via_clock;
				break;
		}
	}

	via_print_drive("Transfer Mode: ", "%10s",
		den[i] ? (uen[i] ? "UDMA" : "DMA") : "PIO");

	via_print_drive("Address Setup: ", "%8dns",
		1000000 * setup[i] / via_clock);
	via_print_drive("Cmd Active:    ", "%8dns",
		1000000 * active8b[i] / via_clock);
	via_print_drive("Cmd Recovery:  ", "%8dns",
		1000000 * recover8b[i] / via_clock);
	via_print_drive("Data Active:   ", "%8dns",
		1000000 * active[i] / via_clock);
	via_print_drive("Data Recovery: ", "%8dns",
		1000000 * recover[i] / via_clock);
	via_print_drive("Cycle Time:    ", "%8dns",
		cycle[i]);
	via_print_drive("Transfer Rate: ", "%4d.%dMB/s",
		speed[i] / 1000, speed[i] / 100 % 10);

	/* hoping it is less than 4K... */
	len = (p - buffer) - offset;
	*addr = buffer + offset;

	return len > count ? count : len;
}

#endif /* DISPLAY_VIA_TIMINGS && CONFIG_PROC_FS */

/**
 *	via_set_speed			-	write timing registers
 *	@dev: PCI device
 *	@dn: device
 *	@timing: IDE timing data to use
 *
 *	via_set_speed writes timing values to the chipset registers
 */

static void via_set_speed(struct pci_dev *dev, u8 dn, struct ide_timing *timing)
{
	u8 t;

	if (~via_config->flags & VIA_BAD_AST) {
		pci_read_config_byte(dev, VIA_ADDRESS_SETUP, &t);
		t = (t & ~(3 << ((3 - dn) << 1))) | ((FIT(timing->setup, 1, 4) - 1) << ((3 - dn) << 1));
		pci_write_config_byte(dev, VIA_ADDRESS_SETUP, t);
	}

	pci_write_config_byte(dev, VIA_8BIT_TIMING + (1 - (dn >> 1)),
		((FIT(timing->act8b, 1, 16) - 1) << 4) | (FIT(timing->rec8b, 1, 16) - 1));

	pci_write_config_byte(dev, VIA_DRIVE_TIMING + (3 - dn),
		((FIT(timing->active, 1, 16) - 1) << 4) | (FIT(timing->recover, 1, 16) - 1));

	switch (via_config->flags & VIA_UDMA) {
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
	struct ide_timing t, p;
	unsigned int T, UT;

	if (speed != XFER_PIO_SLOW)
		ide_config_drive_speed(drive, speed);

	T = 1000000000 / via_clock;

	switch (via_config->flags & VIA_UDMA) {
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

	via_set_speed(HWIF(drive)->pci_dev, drive->dn, &t);

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
	u16 w80 = HWIF(drive)->udma_four;

	u16 speed = ide_find_best_mode(drive,
		XFER_PIO | XFER_EPIO | XFER_SWDMA | XFER_MWDMA |
		(via_config->flags & VIA_UDMA ? XFER_UDMA : 0) |
		(w80 && (via_config->flags & VIA_UDMA) >= VIA_UDMA_66 ? XFER_UDMA_66 : 0) |
		(w80 && (via_config->flags & VIA_UDMA) >= VIA_UDMA_100 ? XFER_UDMA_100 : 0) |
		(w80 && (via_config->flags & VIA_UDMA) >= VIA_UDMA_133 ? XFER_UDMA_133 : 0));

	via_set_drive(drive, speed);

	if (drive->autodma && (speed & XFER_MODE) != XFER_PIO)
		return HWIF(drive)->ide_dma_on(drive);
	return HWIF(drive)->ide_dma_off_quietly(drive);
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
	u8 t, v;
	unsigned int u;
	int i;

	/*
	 * Find the ISA bridge to see how good the IDE is.
	 */

	for (via_config = via_isa_bridges; via_config->id; via_config++)
		if ((isa = pci_find_device(PCI_VENDOR_ID_VIA +
			!!(via_config->flags & VIA_BAD_ID),
			via_config->id, NULL))) {

			pci_read_config_byte(isa, PCI_REVISION_ID, &t);
			if (t >= via_config->rev_min &&
			    t <= via_config->rev_max)
				break;
		}

	if (!via_config->id) {
		printk(KERN_WARNING "VP_IDE: Unknown VIA SouthBridge, disabling DMA.\n");
		return -ENODEV;
	}

	/*
	 * Check 80-wire cable presence and setup Clk66.
	 */

	switch (via_config->flags & VIA_UDMA) {

		case VIA_UDMA_66:
			/* Enable Clk66 */
			pci_read_config_dword(dev, VIA_UDMA_TIMING, &u);
			pci_write_config_dword(dev, VIA_UDMA_TIMING, u|0x80008);
			for (i = 24; i >= 0; i -= 8)
				if (((u >> (i & 16)) & 8) &&
				    ((u >> i) & 0x20) &&
				     (((u >> i) & 7) < 2)) {
					/*
					 * 2x PCI clock and
					 * UDMA w/ < 3T/cycle
					 */
					via_80w |= (1 << (1 - (i >> 4)));
				}
			break;

		case VIA_UDMA_100:
			pci_read_config_dword(dev, VIA_UDMA_TIMING, &u);
			for (i = 24; i >= 0; i -= 8)
				if (((u >> i) & 0x10) ||
				    (((u >> i) & 0x20) &&
				     (((u >> i) & 7) < 4))) {
					/* BIOS 80-wire bit or
					 * UDMA w/ < 60ns/cycle
					 */
					via_80w |= (1 << (1 - (i >> 4)));
				}
			break;

		case VIA_UDMA_133:
			pci_read_config_dword(dev, VIA_UDMA_TIMING, &u);
			for (i = 24; i >= 0; i -= 8)
				if (((u >> i) & 0x10) ||
				    (((u >> i) & 0x20) &&
				     (((u >> i) & 7) < 6))) {
					/* BIOS 80-wire bit or
					 * UDMA w/ < 60ns/cycle
					 */
					via_80w |= (1 << (1 - (i >> 4)));
				}
			break;

	}

	/* Disable Clk66 */
	if (via_config->flags & VIA_BAD_CLK66) {
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

	/*
	 * Setup /proc/ide/via entry.
	 */

#if defined(DISPLAY_VIA_TIMINGS) && defined(CONFIG_PROC_FS)
	if (!via_proc) {
		via_base = pci_resource_start(dev, 4);
		bmide_dev = dev;
		isa_dev = isa;
		ide_pci_create_host_proc("via", via_get_info);
		via_proc = 1;
	}
#endif /* DISPLAY_VIA_TIMINGS && CONFIG_PROC_FS */
	return 0;
}

static void __devinit init_hwif_via82cxxx(ide_hwif_t *hwif)
{
	int i;

	hwif->autodma = 0;

	hwif->tuneproc = &via82cxxx_tune_drive;
	hwif->speedproc = &via_set_drive;


#if defined(CONFIG_PPC_MULTIPLATFORM) && defined(CONFIG_PPC32)
	if(_machine == _MACH_chrp && _chrp_type == _CHRP_Pegasos) {
		hwif->irq = hwif->channel ? 15 : 14;
	}
#endif

	for (i = 0; i < 2; i++) {
		hwif->drives[i].io_32bit = 1;
		hwif->drives[i].unmask = (via_config->flags & VIA_NO_UNMASK) ? 0 : 1;
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
		hwif->udma_four = (via_80w >> hwif->channel) & 1;
	hwif->ide_dma_check = &via82cxxx_ide_dma_check;
	if (!noautodma)
		hwif->autodma = 1;
	hwif->drives[0].autodma = hwif->autodma;
	hwif->drives[1].autodma = hwif->autodma;
}

static ide_pci_device_t via82cxxx_chipset __devinitdata = {
	.name		= "VP_IDE",
	.init_chipset	= init_chipset_via82cxxx,
	.init_hwif	= init_hwif_via82cxxx,
	.channels	= 2,
	.autodma	= NOAUTODMA,
	.enablebits	= {{0x40,0x02,0x02}, {0x40,0x01,0x01}},
	.bootable	= ON_BOARD,
};

static int __devinit via_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	return ide_setup_pci_device(dev, &via82cxxx_chipset);
}

static struct pci_device_id via_pci_tbl[] = {
	{ PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_82C576_1, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{ PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_82C586_1, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
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
