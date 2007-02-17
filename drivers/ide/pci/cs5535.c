/*
 * linux/drivers/ide/pci/cs5535.c
 *
 * Copyright (C) 2004-2005 Advanced Micro Devices, Inc.
 *
 * History:
 * 09/20/2005 - Jaya Kumar <jayakumar.ide@gmail.com>
 * - Reworked tuneproc, set_drive, misc mods to prep for mainline
 * - Work was sponsored by CIS (M) Sdn Bhd.
 * Ported to Kernel 2.6.11 on June 26, 2005 by
 *   Wolfgang Zuleger <wolfgang.zuleger@gmx.de>
 *   Alexander Kiausch <alex.kiausch@t-online.de>
 * Originally developed by AMD for 2.4/2.6
 *
 * Development of this chipset driver was funded
 * by the nice folks at National Semiconductor/AMD.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * Documentation:
 *  CS5535 documentation available from AMD
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/ide.h>

#include "ide-timing.h"

#define MSR_ATAC_BASE		0x51300000
#define ATAC_GLD_MSR_CAP	(MSR_ATAC_BASE+0)
#define ATAC_GLD_MSR_CONFIG	(MSR_ATAC_BASE+0x01)
#define ATAC_GLD_MSR_SMI	(MSR_ATAC_BASE+0x02)
#define ATAC_GLD_MSR_ERROR	(MSR_ATAC_BASE+0x03)
#define ATAC_GLD_MSR_PM		(MSR_ATAC_BASE+0x04)
#define ATAC_GLD_MSR_DIAG	(MSR_ATAC_BASE+0x05)
#define ATAC_IO_BAR		(MSR_ATAC_BASE+0x08)
#define ATAC_RESET		(MSR_ATAC_BASE+0x10)
#define ATAC_CH0D0_PIO		(MSR_ATAC_BASE+0x20)
#define ATAC_CH0D0_DMA		(MSR_ATAC_BASE+0x21)
#define ATAC_CH0D1_PIO		(MSR_ATAC_BASE+0x22)
#define ATAC_CH0D1_DMA		(MSR_ATAC_BASE+0x23)
#define ATAC_PCI_ABRTERR	(MSR_ATAC_BASE+0x24)
#define ATAC_BM0_CMD_PRIM	0x00
#define ATAC_BM0_STS_PRIM	0x02
#define ATAC_BM0_PRD		0x04
#define CS5535_CABLE_DETECT	0x48

/* Format I PIO settings. We seperate out cmd and data for safer timings */

static unsigned int cs5535_pio_cmd_timings[5] =
{ 0xF7F4, 0x53F3, 0x13F1, 0x5131, 0x1131 };
static unsigned int cs5535_pio_dta_timings[5] =
{ 0xF7F4, 0xF173, 0x8141, 0x5131, 0x1131 };

static unsigned int cs5535_mwdma_timings[3] =
{ 0x7F0FFFF3, 0x7F035352, 0x7f024241 };

static unsigned int cs5535_udma_timings[5] =
{ 0x7F7436A1, 0x7F733481, 0x7F723261, 0x7F713161, 0x7F703061 };

/* Macros to check if the register is the reset value -  reset value is an
   invalid timing and indicates the register has not been set previously */

#define CS5535_BAD_PIO(timings) ( (timings&~0x80000000UL) == 0x00009172 )
#define CS5535_BAD_DMA(timings) ( (timings & 0x000FFFFF) == 0x00077771 )

/****
 *	cs5535_set_speed         -     Configure the chipset to the new speed
 *	@drive: Drive to set up
 *	@speed: desired speed
 *
 *	cs5535_set_speed() configures the chipset to a new speed.
 */
static void cs5535_set_speed(ide_drive_t *drive, u8 speed)
{

	u32 reg = 0, dummy;
	int unit = drive->select.b.unit;


	/* Set the PIO timings */
	if ((speed & XFER_MODE) == XFER_PIO) {
		u8 pioa;
		u8 piob;
		u8 cmd;

		pioa = speed - XFER_PIO_0;
		piob = ide_get_best_pio_mode(&(drive->hwif->drives[!unit]),
						255, 4, NULL);
		cmd = pioa < piob ? pioa : piob;

		/* Write the speed of the current drive */
		reg = (cs5535_pio_cmd_timings[cmd] << 16) |
			cs5535_pio_dta_timings[pioa];
		wrmsr(unit ? ATAC_CH0D1_PIO : ATAC_CH0D0_PIO, reg, 0);

		/* And if nessesary - change the speed of the other drive */
		rdmsr(unit ?  ATAC_CH0D0_PIO : ATAC_CH0D1_PIO, reg, dummy);

		if (((reg >> 16) & cs5535_pio_cmd_timings[cmd]) !=
			cs5535_pio_cmd_timings[cmd]) {
			reg &= 0x0000FFFF;
			reg |= cs5535_pio_cmd_timings[cmd] << 16;
			wrmsr(unit ? ATAC_CH0D0_PIO : ATAC_CH0D1_PIO, reg, 0);
		}

		/* Set bit 31 of the DMA register for PIO format 1 timings */
		rdmsr(unit ?  ATAC_CH0D1_DMA : ATAC_CH0D0_DMA, reg, dummy);
		wrmsr(unit ? ATAC_CH0D1_DMA : ATAC_CH0D0_DMA,
					reg | 0x80000000UL, 0);
	} else {
		rdmsr(unit ? ATAC_CH0D1_DMA : ATAC_CH0D0_DMA, reg, dummy);

		reg &= 0x80000000UL;  /* Preserve the PIO format bit */

		if (speed >= XFER_UDMA_0 && speed <= XFER_UDMA_7)
			reg |= cs5535_udma_timings[speed - XFER_UDMA_0];
		else if (speed >= XFER_MW_DMA_0 && speed <= XFER_MW_DMA_2)
			reg |= cs5535_mwdma_timings[speed - XFER_MW_DMA_0];
		else
			return;

		wrmsr(unit ? ATAC_CH0D1_DMA : ATAC_CH0D0_DMA, reg, 0);
	}
}

static u8 cs5535_ratemask(ide_drive_t *drive)
{
	/* eighty93 will return 1 if it's 80core and capable of
	exceeding udma2, 0 otherwise. we need ratemask to set
	the max speed and if we can > udma2 then we return 2
	which selects speed_max as udma4 which is the 5535's max
	speed, and 1 selects udma2 which is the max for 40c */
	if (!eighty_ninty_three(drive))
		return 1;

	return 2;
}


/****
 *	cs5535_set_drive         -     Configure the drive to the new speed
 *	@drive: Drive to set up
 *	@speed: desired speed
 *
 *	cs5535_set_drive() configures the drive and the chipset to a
 *	new speed. It also can be called by upper layers.
 */
static int cs5535_set_drive(ide_drive_t *drive, u8 speed)
{
	speed = ide_rate_filter(cs5535_ratemask(drive), speed);
	ide_config_drive_speed(drive, speed);
	cs5535_set_speed(drive, speed);

	return 0;
}

/****
 *	cs5535_tuneproc    -       PIO setup
 *	@drive: drive to set up
 *	@pio: mode to use (255 for 'best possible')
 *
 *	A callback from the upper layers for PIO-only tuning.
 */
static void cs5535_tuneproc(ide_drive_t *drive, u8 xferspeed)
{
	u8 modes[] = {	XFER_PIO_0, XFER_PIO_1, XFER_PIO_2, XFER_PIO_3,
			XFER_PIO_4 };

	/* cs5535 max pio is pio 4, best_pio will check the blacklist.
	i think we don't need to rate_filter the incoming xferspeed
	since we know we're only going to choose pio */
	xferspeed = ide_get_best_pio_mode(drive, xferspeed, 4, NULL);
	ide_config_drive_speed(drive, modes[xferspeed]);
	cs5535_set_speed(drive, xferspeed);
}

static int cs5535_config_drive_for_dma(ide_drive_t *drive)
{
	u8 speed;

	speed = ide_dma_speed(drive, cs5535_ratemask(drive));

	/* If no DMA speed was available then let dma_check hit pio */
	if (!speed) {
		return 0;
	}

	cs5535_set_drive(drive, speed);
	return ide_dma_enable(drive);
}

static int cs5535_dma_check(ide_drive_t *drive)
{
	u8 speed;

	drive->init_speed = 0;

	if (ide_use_dma(drive) && cs5535_config_drive_for_dma(drive))
		return 0;

	if (ide_use_fast_pio(drive)) {
		speed = ide_get_best_pio_mode(drive, 255, 4, NULL);
		cs5535_set_drive(drive, speed);
	}

	return -1;
}

static u8 __devinit cs5535_cable_detect(struct pci_dev *dev)
{
	u8 bit;

	/* if a 80 wire cable was detected */
	pci_read_config_byte(dev, CS5535_CABLE_DETECT, &bit);
	return (bit & 1);
}

/****
 *	init_hwif_cs5535        -       Initialize one ide cannel
 *	@hwif: Channel descriptor
 *
 *	This gets invoked by the IDE driver once for each channel. It
 *	performs channel-specific pre-initialization before drive probing.
 *
 */
static void __devinit init_hwif_cs5535(ide_hwif_t *hwif)
{
	int i;

	hwif->autodma = 0;

	hwif->tuneproc = &cs5535_tuneproc;
	hwif->speedproc = &cs5535_set_drive;
	hwif->ide_dma_check = &cs5535_dma_check;

	hwif->atapi_dma = 1;
	hwif->ultra_mask = 0x1F;
	hwif->mwdma_mask = 0x07;


	hwif->udma_four = cs5535_cable_detect(hwif->pci_dev);

	if (!noautodma)
		hwif->autodma = 1;

	/* just setting autotune and not worrying about bios timings */
	for (i = 0; i < 2; i++) {
		hwif->drives[i].autotune = 1;
		hwif->drives[i].autodma = hwif->autodma;
	}
}

static ide_pci_device_t cs5535_chipset __devinitdata = {
	.name		= "CS5535",
	.init_hwif	= init_hwif_cs5535,
	.channels	= 1,
	.autodma	= AUTODMA,
	.bootable	= ON_BOARD,
};

static int __devinit cs5535_init_one(struct pci_dev *dev,
					const struct pci_device_id *id)
{
	return ide_setup_pci_device(dev, &cs5535_chipset);
}

static struct pci_device_id cs5535_pci_tbl[] =
{
	{ PCI_VENDOR_ID_NS, PCI_DEVICE_ID_NS_CS5535_IDE, PCI_ANY_ID,
		PCI_ANY_ID, 0, 0, 0},
	{ 0, },
};

MODULE_DEVICE_TABLE(pci, cs5535_pci_tbl);

static struct pci_driver driver = {
	.name       = "CS5535_IDE",
	.id_table   = cs5535_pci_tbl,
	.probe      = cs5535_init_one,
};

static int __init cs5535_ide_init(void)
{
	return ide_pci_register_driver(&driver);
}

module_init(cs5535_ide_init);

MODULE_AUTHOR("AMD");
MODULE_DESCRIPTION("PCI driver module for AMD/NS CS5535 IDE");
MODULE_LICENSE("GPL");
