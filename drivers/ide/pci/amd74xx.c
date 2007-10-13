/*
 * Version 2.22
 *
 * AMD 755/756/766/8111 and nVidia nForce/2/2s/3/3s/CK804/MCP04
 * IDE driver for Linux.
 *
 * Copyright (c) 2000-2002 Vojtech Pavlik
 * Copyright (c) 2007 Bartlomiej Zolnierkiewicz
 *
 * Based on the work of:
 *      Andre Hedrick
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

#include "ide-timing.h"

#define DISPLAY_AMD_TIMINGS

#define AMD_IDE_ENABLE		(0x00 + amd_config->base)
#define AMD_IDE_CONFIG		(0x01 + amd_config->base)
#define AMD_CABLE_DETECT	(0x02 + amd_config->base)
#define AMD_DRIVE_TIMING	(0x08 + amd_config->base)
#define AMD_8BIT_TIMING		(0x0e + amd_config->base)
#define AMD_ADDRESS_SETUP	(0x0c + amd_config->base)
#define AMD_UDMA_TIMING		(0x10 + amd_config->base)

#define AMD_CHECK_SWDMA		0x08
#define AMD_BAD_SWDMA		0x10
#define AMD_BAD_FIFO		0x20
#define AMD_CHECK_SERENADE	0x40

/*
 * AMD SouthBridge chips.
 */

static struct amd_ide_chip {
	unsigned short id;
	u8 base;
	u8 udma_mask;
	u8 flags;
} amd_ide_chips[] = {
	{ PCI_DEVICE_ID_AMD_COBRA_7401,		 0x40, ATA_UDMA2, AMD_BAD_SWDMA },
	{ PCI_DEVICE_ID_AMD_VIPER_7409,		 0x40, ATA_UDMA4, AMD_CHECK_SWDMA },
	{ PCI_DEVICE_ID_AMD_VIPER_7411,		 0x40, ATA_UDMA5, AMD_BAD_FIFO },
	{ PCI_DEVICE_ID_AMD_OPUS_7441,		 0x40, ATA_UDMA5, },
	{ PCI_DEVICE_ID_AMD_8111_IDE,		 0x40, ATA_UDMA6, AMD_CHECK_SERENADE },
	{ PCI_DEVICE_ID_NVIDIA_NFORCE_IDE,	 0x50, ATA_UDMA5, },
	{ PCI_DEVICE_ID_NVIDIA_NFORCE2_IDE,	 0x50, ATA_UDMA6, },
	{ PCI_DEVICE_ID_NVIDIA_NFORCE2S_IDE,	 0x50, ATA_UDMA6, },
	{ PCI_DEVICE_ID_NVIDIA_NFORCE2S_SATA,	 0x50, ATA_UDMA6, },
	{ PCI_DEVICE_ID_NVIDIA_NFORCE3_IDE,	 0x50, ATA_UDMA6, },
	{ PCI_DEVICE_ID_NVIDIA_NFORCE3S_IDE,	 0x50, ATA_UDMA6, },
	{ PCI_DEVICE_ID_NVIDIA_NFORCE3S_SATA,	 0x50, ATA_UDMA6, },
	{ PCI_DEVICE_ID_NVIDIA_NFORCE3S_SATA2,	 0x50, ATA_UDMA6, },
	{ PCI_DEVICE_ID_NVIDIA_NFORCE_CK804_IDE, 0x50, ATA_UDMA6, },
	{ PCI_DEVICE_ID_NVIDIA_NFORCE_MCP04_IDE, 0x50, ATA_UDMA6, },
	{ PCI_DEVICE_ID_NVIDIA_NFORCE_MCP51_IDE, 0x50, ATA_UDMA6, },
	{ PCI_DEVICE_ID_NVIDIA_NFORCE_MCP55_IDE, 0x50, ATA_UDMA6, },
	{ PCI_DEVICE_ID_NVIDIA_NFORCE_MCP61_IDE, 0x50, ATA_UDMA6, },
	{ PCI_DEVICE_ID_NVIDIA_NFORCE_MCP65_IDE, 0x50, ATA_UDMA6, },
	{ PCI_DEVICE_ID_NVIDIA_NFORCE_MCP67_IDE, 0x50, ATA_UDMA6, },
	{ PCI_DEVICE_ID_NVIDIA_NFORCE_MCP73_IDE, 0x50, ATA_UDMA6, },
	{ PCI_DEVICE_ID_NVIDIA_NFORCE_MCP77_IDE, 0x50, ATA_UDMA6, },
	{ PCI_DEVICE_ID_AMD_CS5536_IDE,		 0x40, ATA_UDMA5, },
	{ 0 }
};

static struct amd_ide_chip *amd_config;
static ide_pci_device_t *amd_chipset;
static unsigned int amd_80w;
static unsigned int amd_clock;

static char *amd_dma[] = { "16", "25", "33", "44", "66", "100", "133" };
static unsigned char amd_cyc2udma[] = { 6, 6, 5, 4, 0, 1, 1, 2, 2, 3, 3, 3, 3, 3, 3, 7 };

/*
 * AMD /proc entry.
 */

#ifdef CONFIG_IDE_PROC_FS

#include <linux/stat.h>
#include <linux/proc_fs.h>

static u8 amd74xx_proc;

static unsigned char amd_udma2cyc[] = { 4, 6, 8, 10, 3, 2, 1, 15 };
static unsigned long amd_base;
static struct pci_dev *bmide_dev;
extern int (*amd74xx_display_info)(char *, char **, off_t, int); /* ide-proc.c */

#define amd_print(format, arg...) p += sprintf(p, format "\n" , ## arg)
#define amd_print_drive(name, format, arg...)\
	p += sprintf(p, name); for (i = 0; i < 4; i++) p += sprintf(p, format, ## arg); p += sprintf(p, "\n");

static int amd74xx_get_info(char *buffer, char **addr, off_t offset, int count)
{
	int speed[4], cycle[4], setup[4], active[4], recover[4], den[4],
		 uen[4], udma[4], active8b[4], recover8b[4];
	struct pci_dev *dev = bmide_dev;
	unsigned int v, u, i;
	unsigned short c, w;
	unsigned char t;
	int len;
	char *p = buffer;

	amd_print("----------AMD BusMastering IDE Configuration----------------");

	amd_print("Driver Version:                     2.13");
	amd_print("South Bridge:                       %s", pci_name(bmide_dev));

	amd_print("Revision:                           IDE %#x", dev->revision);
	amd_print("Highest DMA rate:                   UDMA%s", amd_dma[fls(amd_config->udma_mask) - 1]);

	amd_print("BM-DMA base:                        %#lx", amd_base);
	amd_print("PCI clock:                          %d.%dMHz", amd_clock / 1000, amd_clock / 100 % 10);
	
	amd_print("-----------------------Primary IDE-------Secondary IDE------");

	pci_read_config_byte(dev, AMD_IDE_CONFIG, &t);
	amd_print("Prefetch Buffer:       %10s%20s", (t & 0x80) ? "yes" : "no", (t & 0x20) ? "yes" : "no");
	amd_print("Post Write Buffer:     %10s%20s", (t & 0x40) ? "yes" : "no", (t & 0x10) ? "yes" : "no");

	pci_read_config_byte(dev, AMD_IDE_ENABLE, &t);
	amd_print("Enabled:               %10s%20s", (t & 0x02) ? "yes" : "no", (t & 0x01) ? "yes" : "no");

	c = inb(amd_base + 0x02) | (inb(amd_base + 0x0a) << 8);
	amd_print("Simplex only:          %10s%20s", (c & 0x80) ? "yes" : "no", (c & 0x8000) ? "yes" : "no");

	amd_print("Cable Type:            %10s%20s", (amd_80w & 1) ? "80w" : "40w", (amd_80w & 2) ? "80w" : "40w");

	if (!amd_clock)
                return p - buffer;

	amd_print("-------------------drive0----drive1----drive2----drive3-----");

	pci_read_config_byte(dev, AMD_ADDRESS_SETUP, &t);
	pci_read_config_dword(dev, AMD_DRIVE_TIMING, &v);
	pci_read_config_word(dev, AMD_8BIT_TIMING, &w);
	pci_read_config_dword(dev, AMD_UDMA_TIMING, &u);

	for (i = 0; i < 4; i++) {
		setup[i]     = ((t >> ((3 - i) << 1)) & 0x3) + 1;
		recover8b[i] = ((w >> ((1 - (i >> 1)) << 3)) & 0xf) + 1;
		active8b[i]  = ((w >> (((1 - (i >> 1)) << 3) + 4)) & 0xf) + 1;
		active[i]    = ((v >> (((3 - i) << 3) + 4)) & 0xf) + 1;
		recover[i]   = ((v >> ((3 - i) << 3)) & 0xf) + 1;

		udma[i] = amd_udma2cyc[((u >> ((3 - i) << 3)) & 0x7)];
		uen[i]  = ((u >> ((3 - i) << 3)) & 0x40) ? 1 : 0;
		den[i]  = (c & ((i & 1) ? 0x40 : 0x20) << ((i & 2) << 2));

		if (den[i] && uen[i] && udma[i] == 1) {
			speed[i] = amd_clock * 3;
			cycle[i] = 666666 / amd_clock;
			continue;
		}

		if (den[i] && uen[i] && udma[i] == 15) {
			speed[i] = amd_clock * 4;
			cycle[i] = 500000 / amd_clock;
			continue;
		}

		speed[i] = 4 * amd_clock / ((den[i] && uen[i]) ? udma[i] : (active[i] + recover[i]) * 2);
		cycle[i] = 1000000 * ((den[i] && uen[i]) ? udma[i] : (active[i] + recover[i]) * 2) / amd_clock / 2;
	}

	amd_print_drive("Transfer Mode: ", "%10s", den[i] ? (uen[i] ? "UDMA" : "DMA") : "PIO");

	amd_print_drive("Address Setup: ", "%8dns", 1000000 * setup[i] / amd_clock);
	amd_print_drive("Cmd Active:    ", "%8dns", 1000000 * active8b[i] / amd_clock);
	amd_print_drive("Cmd Recovery:  ", "%8dns", 1000000 * recover8b[i] / amd_clock);
	amd_print_drive("Data Active:   ", "%8dns", 1000000 * active[i] / amd_clock);
	amd_print_drive("Data Recovery: ", "%8dns", 1000000 * recover[i] / amd_clock);
	amd_print_drive("Cycle Time:    ", "%8dns", cycle[i]);
	amd_print_drive("Transfer Rate: ", "%4d.%dMB/s", speed[i] / 1000, speed[i] / 100 % 10);

	/* hoping p - buffer is less than 4K... */
	len = (p - buffer) - offset;
	*addr = buffer + offset;
	
	return len > count ? count : len;
}

#endif

/*
 * amd_set_speed() writes timing values to the chipset registers
 */

static void amd_set_speed(struct pci_dev *dev, unsigned char dn, struct ide_timing *timing)
{
	unsigned char t;

	pci_read_config_byte(dev, AMD_ADDRESS_SETUP, &t);
	t = (t & ~(3 << ((3 - dn) << 1))) | ((FIT(timing->setup, 1, 4) - 1) << ((3 - dn) << 1));
	pci_write_config_byte(dev, AMD_ADDRESS_SETUP, t);

	pci_write_config_byte(dev, AMD_8BIT_TIMING + (1 - (dn >> 1)),
		((FIT(timing->act8b, 1, 16) - 1) << 4) | (FIT(timing->rec8b, 1, 16) - 1));

	pci_write_config_byte(dev, AMD_DRIVE_TIMING + (3 - dn),
		((FIT(timing->active, 1, 16) - 1) << 4) | (FIT(timing->recover, 1, 16) - 1));

	switch (amd_config->udma_mask) {
	case ATA_UDMA2: t = timing->udma ? (0xc0 | (FIT(timing->udma, 2, 5) - 2)) : 0x03; break;
	case ATA_UDMA4: t = timing->udma ? (0xc0 | amd_cyc2udma[FIT(timing->udma, 2, 10)]) : 0x03; break;
	case ATA_UDMA5: t = timing->udma ? (0xc0 | amd_cyc2udma[FIT(timing->udma, 1, 10)]) : 0x03; break;
	case ATA_UDMA6: t = timing->udma ? (0xc0 | amd_cyc2udma[FIT(timing->udma, 1, 15)]) : 0x03; break;
	default: return;
	}

	pci_write_config_byte(dev, AMD_UDMA_TIMING + (3 - dn), t);
}

/*
 * amd_set_drive() computes timing values configures the drive and
 * the chipset to a desired transfer mode. It also can be called
 * by upper layers.
 */

static int amd_set_drive(ide_drive_t *drive, const u8 speed)
{
	ide_drive_t *peer = HWIF(drive)->drives + (~drive->dn & 1);
	struct ide_timing t, p;
	int T, UT;

	if (speed != XFER_PIO_SLOW)
		ide_config_drive_speed(drive, speed);

	T = 1000000000 / amd_clock;
	UT = (amd_config->udma_mask == ATA_UDMA2) ? T : (T / 2);

	ide_timing_compute(drive, speed, &t, T, UT);

	if (peer->present) {
		ide_timing_compute(peer, peer->current_speed, &p, T, UT);
		ide_timing_merge(&p, &t, &t, IDE_TIMING_8BIT);
	}

	if (speed == XFER_UDMA_5 && amd_clock <= 33333) t.udma = 1;
	if (speed == XFER_UDMA_6 && amd_clock <= 33333) t.udma = 15;

	amd_set_speed(HWIF(drive)->pci_dev, drive->dn, &t);

	if (!drive->init_speed)	
		drive->init_speed = speed;
	drive->current_speed = speed;

	return 0;
}

/*
 * amd_set_pio_mode() is a callback from upper layers for PIO-only tuning.
 */

static void amd_set_pio_mode(ide_drive_t *drive, const u8 pio)
{
	amd_set_drive(drive, XFER_PIO_0 + pio);
}

static int amd74xx_ide_dma_check(ide_drive_t *drive)
{
	if (ide_tune_dma(drive))
		return 0;

	ide_set_max_pio(drive);

	return -1;
}

/*
 * The initialization callback. Here we determine the IDE chip type
 * and initialize its drive independent registers.
 */

static unsigned int __devinit init_chipset_amd74xx(struct pci_dev *dev, const char *name)
{
	unsigned char t;
	unsigned int u;
	int i;

/*
 * Check for bad SWDMA.
 */

	if (amd_config->flags & AMD_CHECK_SWDMA) {
		if (dev->revision <= 7)
			amd_config->flags |= AMD_BAD_SWDMA;
	}

/*
 * Check 80-wire cable presence.
 */

	switch (amd_config->udma_mask) {

		case ATA_UDMA6:
		case ATA_UDMA5:
			pci_read_config_byte(dev, AMD_CABLE_DETECT, &t);
			pci_read_config_dword(dev, AMD_UDMA_TIMING, &u);
			amd_80w = ((t & 0x3) ? 1 : 0) | ((t & 0xc) ? 2 : 0);
			for (i = 24; i >= 0; i -= 8)
				if (((u >> i) & 4) && !(amd_80w & (1 << (1 - (i >> 4))))) {
					printk(KERN_WARNING "%s: BIOS didn't set cable bits correctly. Enabling workaround.\n",
						amd_chipset->name);
					amd_80w |= (1 << (1 - (i >> 4)));
				}
			break;

		case ATA_UDMA4:
			/* no host side cable detection */
			amd_80w = 0x03;
			break;
	}

/*
 * Take care of prefetch & postwrite.
 */

	pci_read_config_byte(dev, AMD_IDE_CONFIG, &t);
	pci_write_config_byte(dev, AMD_IDE_CONFIG,
		(amd_config->flags & AMD_BAD_FIFO) ? (t & 0x0f) : (t | 0xf0));

/*
 * Take care of incorrectly wired Serenade mainboards.
 */

	if ((amd_config->flags & AMD_CHECK_SERENADE) &&
		dev->subsystem_vendor == PCI_VENDOR_ID_AMD &&
		dev->subsystem_device == PCI_DEVICE_ID_AMD_SERENADE)
			amd_config->udma_mask = ATA_UDMA5;

/*
 * Determine the system bus clock.
 */

	amd_clock = system_bus_clock() * 1000;

	switch (amd_clock) {
		case 33000: amd_clock = 33333; break;
		case 37000: amd_clock = 37500; break;
		case 41000: amd_clock = 41666; break;
	}

	if (amd_clock < 20000 || amd_clock > 50000) {
		printk(KERN_WARNING "%s: User given PCI clock speed impossible (%d), using 33 MHz instead.\n",
			amd_chipset->name, amd_clock);
		amd_clock = 33333;
	}

/*
 * Print the boot message.
 */

	pci_read_config_byte(dev, PCI_REVISION_ID, &t);
	printk(KERN_INFO "%s: %s (rev %02x) UDMA%s controller\n",
		amd_chipset->name, pci_name(dev), dev->revision,
		amd_dma[fls(amd_config->udma_mask) - 1]);

/*
 * Register /proc/ide/amd74xx entry
 */

#if defined(DISPLAY_AMD_TIMINGS) && defined(CONFIG_IDE_PROC_FS)
        if (!amd74xx_proc) {
                amd_base = pci_resource_start(dev, 4);
                bmide_dev = dev;
		ide_pci_create_host_proc("amd74xx", amd74xx_get_info);
                amd74xx_proc = 1;
        }
#endif /* DISPLAY_AMD_TIMINGS && CONFIG_IDE_PROC_FS */

	return dev->irq;
}

static void __devinit init_hwif_amd74xx(ide_hwif_t *hwif)
{
	int i;

	if (hwif->irq == 0) /* 0 is bogus but will do for now */
		hwif->irq = pci_get_legacy_ide_irq(hwif->pci_dev, hwif->channel);

	hwif->autodma = 0;

	hwif->set_pio_mode = &amd_set_pio_mode;
	hwif->speedproc = &amd_set_drive;

	for (i = 0; i < 2; i++) {
		hwif->drives[i].io_32bit = 1;
		hwif->drives[i].unmask = 1;
		hwif->drives[i].autotune = 1;
		hwif->drives[i].dn = hwif->channel * 2 + i;
	}

	if (!hwif->dma_base)
		return;

        hwif->atapi_dma = 1;

	hwif->ultra_mask = amd_config->udma_mask;
	hwif->mwdma_mask = 0x07;
	if ((amd_config->flags & AMD_BAD_SWDMA) == 0)
		hwif->swdma_mask = 0x07;

	if (hwif->cbl != ATA_CBL_PATA40_SHORT) {
		if ((amd_80w >> hwif->channel) & 1)
			hwif->cbl = ATA_CBL_PATA80;
		else
			hwif->cbl = ATA_CBL_PATA40;
	}

        hwif->ide_dma_check = &amd74xx_ide_dma_check;
        if (!noautodma)
                hwif->autodma = 1;
        hwif->drives[0].autodma = hwif->autodma;
        hwif->drives[1].autodma = hwif->autodma;
}

#define DECLARE_AMD_DEV(name_str)					\
	{								\
		.name		= name_str,				\
		.init_chipset	= init_chipset_amd74xx,			\
		.init_hwif	= init_hwif_amd74xx,			\
		.autodma	= AUTODMA,				\
		.enablebits	= {{0x40,0x02,0x02}, {0x40,0x01,0x01}},	\
		.bootable	= ON_BOARD,				\
		.host_flags	= IDE_HFLAG_PIO_NO_BLACKLIST		\
				| IDE_HFLAG_PIO_NO_DOWNGRADE,		\
		.pio_mask	= ATA_PIO5,				\
	}

#define DECLARE_NV_DEV(name_str)					\
	{								\
		.name		= name_str,				\
		.init_chipset	= init_chipset_amd74xx,			\
		.init_hwif	= init_hwif_amd74xx,			\
		.autodma	= AUTODMA,				\
		.enablebits	= {{0x50,0x02,0x02}, {0x50,0x01,0x01}},	\
		.bootable	= ON_BOARD,				\
		.host_flags	= IDE_HFLAG_PIO_NO_BLACKLIST		\
				| IDE_HFLAG_PIO_NO_DOWNGRADE,		\
		.pio_mask	= ATA_PIO5,				\
	}

static ide_pci_device_t amd74xx_chipsets[] __devinitdata = {
	/*  0 */ DECLARE_AMD_DEV("AMD7401"),
	/*  1 */ DECLARE_AMD_DEV("AMD7409"),
	/*  2 */ DECLARE_AMD_DEV("AMD7411"),
	/*  3 */ DECLARE_AMD_DEV("AMD7441"),
	/*  4 */ DECLARE_AMD_DEV("AMD8111"),

	/*  5 */ DECLARE_NV_DEV("NFORCE"),
	/*  6 */ DECLARE_NV_DEV("NFORCE2"),
	/*  7 */ DECLARE_NV_DEV("NFORCE2-U400R"),
	/*  8 */ DECLARE_NV_DEV("NFORCE2-U400R-SATA"),
	/*  9 */ DECLARE_NV_DEV("NFORCE3-150"),
	/* 10 */ DECLARE_NV_DEV("NFORCE3-250"),
	/* 11 */ DECLARE_NV_DEV("NFORCE3-250-SATA"),
	/* 12 */ DECLARE_NV_DEV("NFORCE3-250-SATA2"),
	/* 13 */ DECLARE_NV_DEV("NFORCE-CK804"),
	/* 14 */ DECLARE_NV_DEV("NFORCE-MCP04"),
	/* 15 */ DECLARE_NV_DEV("NFORCE-MCP51"),
	/* 16 */ DECLARE_NV_DEV("NFORCE-MCP55"),
	/* 17 */ DECLARE_NV_DEV("NFORCE-MCP61"),
	/* 18 */ DECLARE_NV_DEV("NFORCE-MCP65"),
	/* 19 */ DECLARE_NV_DEV("NFORCE-MCP67"),
	/* 20 */ DECLARE_NV_DEV("NFORCE-MCP73"),
	/* 21 */ DECLARE_NV_DEV("NFORCE-MCP77"),
	/* 22 */ DECLARE_AMD_DEV("AMD5536"),
};

static int __devinit amd74xx_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	amd_chipset = amd74xx_chipsets + id->driver_data;
	amd_config = amd_ide_chips + id->driver_data;
	if (dev->device != amd_config->id) {
		printk(KERN_ERR "%s: assertion 0x%02x == 0x%02x failed !\n",
		       pci_name(dev), dev->device, amd_config->id);
		return -ENODEV;
	}
	return ide_setup_pci_device(dev, amd_chipset);
}

static struct pci_device_id amd74xx_pci_tbl[] = {
	{ PCI_VENDOR_ID_AMD,	PCI_DEVICE_ID_AMD_COBRA_7401,		PCI_ANY_ID, PCI_ANY_ID, 0, 0,  0 },
	{ PCI_VENDOR_ID_AMD,	PCI_DEVICE_ID_AMD_VIPER_7409,		PCI_ANY_ID, PCI_ANY_ID, 0, 0,  1 },
	{ PCI_VENDOR_ID_AMD,	PCI_DEVICE_ID_AMD_VIPER_7411,		PCI_ANY_ID, PCI_ANY_ID, 0, 0,  2 },
	{ PCI_VENDOR_ID_AMD,	PCI_DEVICE_ID_AMD_OPUS_7441,		PCI_ANY_ID, PCI_ANY_ID, 0, 0,  3 },
	{ PCI_VENDOR_ID_AMD,	PCI_DEVICE_ID_AMD_8111_IDE,		PCI_ANY_ID, PCI_ANY_ID, 0, 0,  4 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE_IDE,	PCI_ANY_ID, PCI_ANY_ID, 0, 0,  5 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE2_IDE,	PCI_ANY_ID, PCI_ANY_ID, 0, 0,  6 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE2S_IDE,	PCI_ANY_ID, PCI_ANY_ID, 0, 0,  7 },
#ifdef CONFIG_BLK_DEV_IDE_SATA
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE2S_SATA,	PCI_ANY_ID, PCI_ANY_ID, 0, 0,  8 },
#endif
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE3_IDE,	PCI_ANY_ID, PCI_ANY_ID, 0, 0,  9 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE3S_IDE,	PCI_ANY_ID, PCI_ANY_ID, 0, 0, 10 },
#ifdef CONFIG_BLK_DEV_IDE_SATA
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE3S_SATA,	PCI_ANY_ID, PCI_ANY_ID, 0, 0, 11 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE3S_SATA2,	PCI_ANY_ID, PCI_ANY_ID, 0, 0, 12 },
#endif
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE_CK804_IDE,	PCI_ANY_ID, PCI_ANY_ID, 0, 0, 13 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE_MCP04_IDE,	PCI_ANY_ID, PCI_ANY_ID, 0, 0, 14 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE_MCP51_IDE,	PCI_ANY_ID, PCI_ANY_ID, 0, 0, 15 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE_MCP55_IDE,	PCI_ANY_ID, PCI_ANY_ID, 0, 0, 16 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE_MCP61_IDE,	PCI_ANY_ID, PCI_ANY_ID, 0, 0, 17 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE_MCP65_IDE,  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 18 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE_MCP67_IDE,  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 19 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE_MCP73_IDE,  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 20 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE_MCP77_IDE,  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 21 },
	{ PCI_VENDOR_ID_AMD,	PCI_DEVICE_ID_AMD_CS5536_IDE,		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 22 },
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, amd74xx_pci_tbl);

static struct pci_driver driver = {
	.name		= "AMD_IDE",
	.id_table	= amd74xx_pci_tbl,
	.probe		= amd74xx_probe,
};

static int __init amd74xx_ide_init(void)
{
	return ide_pci_register_driver(&driver);
}

module_init(amd74xx_ide_init);

MODULE_AUTHOR("Vojtech Pavlik");
MODULE_DESCRIPTION("AMD PCI IDE driver");
MODULE_LICENSE("GPL");
