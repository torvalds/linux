/*
 * Version 2.24
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
static const struct ide_port_info *amd_chipset;
static unsigned int amd_80w;
static unsigned int amd_clock;

static char *amd_dma[] = { "16", "25", "33", "44", "66", "100", "133" };
static unsigned char amd_cyc2udma[] = { 6, 6, 5, 4, 0, 1, 1, 2, 2, 3, 3, 3, 3, 3, 3, 7 };

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
 * amd_set_drive() computes timing values and configures the chipset
 * to a desired transfer mode.  It also can be called by upper layers.
 */

static void amd_set_drive(ide_drive_t *drive, const u8 speed)
{
	ide_drive_t *peer = HWIF(drive)->drives + (~drive->dn & 1);
	struct ide_timing t, p;
	int T, UT;

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
}

/*
 * amd_set_pio_mode() is a callback from upper layers for PIO-only tuning.
 */

static void amd_set_pio_mode(ide_drive_t *drive, const u8 pio)
{
	amd_set_drive(drive, XFER_PIO_0 + pio);
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

	printk(KERN_INFO "%s: %s (rev %02x) UDMA%s controller\n",
		amd_chipset->name, pci_name(dev), dev->revision,
		amd_dma[fls(amd_config->udma_mask) - 1]);

	return dev->irq;
}

static void __devinit init_hwif_amd74xx(ide_hwif_t *hwif)
{
	if (hwif->irq == 0) /* 0 is bogus but will do for now */
		hwif->irq = pci_get_legacy_ide_irq(hwif->pci_dev, hwif->channel);

	hwif->set_pio_mode = &amd_set_pio_mode;
	hwif->set_dma_mode = &amd_set_drive;

	if (!hwif->dma_base)
		return;

	hwif->ultra_mask = amd_config->udma_mask;
	if (amd_config->flags & AMD_BAD_SWDMA)
		hwif->swdma_mask = 0x00;

	if (hwif->cbl != ATA_CBL_PATA40_SHORT) {
		if ((amd_80w >> hwif->channel) & 1)
			hwif->cbl = ATA_CBL_PATA80;
		else
			hwif->cbl = ATA_CBL_PATA40;
	}
}

#define IDE_HFLAGS_AMD \
	(IDE_HFLAG_PIO_NO_BLACKLIST | \
	 IDE_HFLAG_PIO_NO_DOWNGRADE | \
	 IDE_HFLAG_POST_SET_MODE | \
	 IDE_HFLAG_IO_32BIT | \
	 IDE_HFLAG_UNMASK_IRQS | \
	 IDE_HFLAG_BOOTABLE)

#define DECLARE_AMD_DEV(name_str)					\
	{								\
		.name		= name_str,				\
		.init_chipset	= init_chipset_amd74xx,			\
		.init_hwif	= init_hwif_amd74xx,			\
		.enablebits	= {{0x40,0x02,0x02}, {0x40,0x01,0x01}},	\
		.host_flags	= IDE_HFLAGS_AMD,			\
		.pio_mask	= ATA_PIO5,				\
		.swdma_mask	= ATA_SWDMA2,				\
		.mwdma_mask	= ATA_MWDMA2,				\
	}

#define DECLARE_NV_DEV(name_str)					\
	{								\
		.name		= name_str,				\
		.init_chipset	= init_chipset_amd74xx,			\
		.init_hwif	= init_hwif_amd74xx,			\
		.enablebits	= {{0x50,0x02,0x02}, {0x50,0x01,0x01}},	\
		.host_flags	= IDE_HFLAGS_AMD,			\
		.pio_mask	= ATA_PIO5,				\
		.swdma_mask	= ATA_SWDMA2,				\
		.mwdma_mask	= ATA_MWDMA2,				\
	}

static const struct ide_port_info amd74xx_chipsets[] __devinitdata = {
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

static const struct pci_device_id amd74xx_pci_tbl[] = {
	{ PCI_VDEVICE(AMD,	PCI_DEVICE_ID_AMD_COBRA_7401),		 0 },
	{ PCI_VDEVICE(AMD,	PCI_DEVICE_ID_AMD_VIPER_7409),		 1 },
	{ PCI_VDEVICE(AMD,	PCI_DEVICE_ID_AMD_VIPER_7411),		 2 },
	{ PCI_VDEVICE(AMD,	PCI_DEVICE_ID_AMD_OPUS_7441),		 3 },
	{ PCI_VDEVICE(AMD,	PCI_DEVICE_ID_AMD_8111_IDE),		 4 },
	{ PCI_VDEVICE(NVIDIA,	PCI_DEVICE_ID_NVIDIA_NFORCE_IDE),	 5 },
	{ PCI_VDEVICE(NVIDIA,	PCI_DEVICE_ID_NVIDIA_NFORCE2_IDE),	 6 },
	{ PCI_VDEVICE(NVIDIA,	PCI_DEVICE_ID_NVIDIA_NFORCE2S_IDE),	 7 },
#ifdef CONFIG_BLK_DEV_IDE_SATA
	{ PCI_VDEVICE(NVIDIA,	PCI_DEVICE_ID_NVIDIA_NFORCE2S_SATA),	 8 },
#endif
	{ PCI_VDEVICE(NVIDIA,	PCI_DEVICE_ID_NVIDIA_NFORCE3_IDE),	 9 },
	{ PCI_VDEVICE(NVIDIA,	PCI_DEVICE_ID_NVIDIA_NFORCE3S_IDE),	10 },
#ifdef CONFIG_BLK_DEV_IDE_SATA
	{ PCI_VDEVICE(NVIDIA,	PCI_DEVICE_ID_NVIDIA_NFORCE3S_SATA),	11 },
	{ PCI_VDEVICE(NVIDIA,	PCI_DEVICE_ID_NVIDIA_NFORCE3S_SATA2),	12 },
#endif
	{ PCI_VDEVICE(NVIDIA,	PCI_DEVICE_ID_NVIDIA_NFORCE_CK804_IDE),	13 },
	{ PCI_VDEVICE(NVIDIA,	PCI_DEVICE_ID_NVIDIA_NFORCE_MCP04_IDE),	14 },
	{ PCI_VDEVICE(NVIDIA,	PCI_DEVICE_ID_NVIDIA_NFORCE_MCP51_IDE),	15 },
	{ PCI_VDEVICE(NVIDIA,	PCI_DEVICE_ID_NVIDIA_NFORCE_MCP55_IDE),	16 },
	{ PCI_VDEVICE(NVIDIA,	PCI_DEVICE_ID_NVIDIA_NFORCE_MCP61_IDE),	17 },
	{ PCI_VDEVICE(NVIDIA,	PCI_DEVICE_ID_NVIDIA_NFORCE_MCP65_IDE),	18 },
	{ PCI_VDEVICE(NVIDIA,	PCI_DEVICE_ID_NVIDIA_NFORCE_MCP67_IDE),	19 },
	{ PCI_VDEVICE(NVIDIA,	PCI_DEVICE_ID_NVIDIA_NFORCE_MCP73_IDE),	20 },
	{ PCI_VDEVICE(NVIDIA,	PCI_DEVICE_ID_NVIDIA_NFORCE_MCP77_IDE),	21 },
	{ PCI_VDEVICE(AMD,	PCI_DEVICE_ID_AMD_CS5536_IDE),		22 },
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
