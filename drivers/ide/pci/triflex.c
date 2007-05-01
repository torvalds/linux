/*
 * triflex.c
 * 
 * IDE Chipset driver for the Compaq TriFlex IDE controller.
 * 
 * Known to work with the Compaq Workstation 5x00 series.
 *
 * Copyright (C) 2002 Hewlett-Packard Development Group, L.P.
 * Author: Torben Mathiasen <torben.mathiasen@hp.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 * 
 * Loosely based on the piix & svwks drivers.
 *
 * Documentation:
 *	Not publically available.
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
#include <linux/pci.h>
#include <linux/ide.h>
#include <linux/init.h>

static int triflex_tune_chipset(ide_drive_t *drive, u8 xferspeed)
{
	ide_hwif_t *hwif = HWIF(drive);
	struct pci_dev *dev = hwif->pci_dev;
	u8 channel_offset = hwif->channel ? 0x74 : 0x70;
	u16 timing = 0;
	u32 triflex_timings = 0;
	u8 unit = (drive->select.b.unit & 0x01);
	u8 speed = ide_rate_filter(0, xferspeed);
	
	pci_read_config_dword(dev, channel_offset, &triflex_timings);
	
	switch(speed) {
		case XFER_MW_DMA_2:
			timing = 0x0103; 
			break;
		case XFER_MW_DMA_1:
			timing = 0x0203;
			break;
		case XFER_MW_DMA_0:
			timing = 0x0808;
			break;
		case XFER_SW_DMA_2:
		case XFER_SW_DMA_1:
		case XFER_SW_DMA_0:
			timing = 0x0f0f;
			break;
		case XFER_PIO_4:
			timing = 0x0202;
			break;
		case XFER_PIO_3:
			timing = 0x0204;
			break;
		case XFER_PIO_2:
			timing = 0x0404;
			break;
		case XFER_PIO_1:
			timing = 0x0508;
			break;
		case XFER_PIO_0:
			timing = 0x0808;
			break;
		default:
			return -1;
	}

	triflex_timings &= ~(0xFFFF << (16 * unit));
	triflex_timings |= (timing << (16 * unit));
	
	pci_write_config_dword(dev, channel_offset, triflex_timings);
	
	return (ide_config_drive_speed(drive, speed));
}

static void triflex_tune_drive(ide_drive_t *drive, u8 pio)
{
	int use_pio = ide_get_best_pio_mode(drive, pio, 4, NULL);
	(void) triflex_tune_chipset(drive, (XFER_PIO_0 + use_pio));
}

static int triflex_config_drive_for_dma(ide_drive_t *drive)
{
	int speed = ide_dma_speed(drive, 0); /* No ultra speeds */

	if (!speed)
		return 0;

	(void) triflex_tune_chipset(drive, speed);
	 return ide_dma_enable(drive);
}

static int triflex_config_drive_xfer_rate(ide_drive_t *drive)
{
	if (ide_use_dma(drive) && triflex_config_drive_for_dma(drive))
		return 0;

	triflex_tune_drive(drive, 255);

	return -1;
}

static void __devinit init_hwif_triflex(ide_hwif_t *hwif)
{
	hwif->tuneproc = &triflex_tune_drive;
	hwif->speedproc = &triflex_tune_chipset;

	hwif->atapi_dma  = 1;
	hwif->mwdma_mask = 0x07;
	hwif->swdma_mask = 0x07;
	hwif->ide_dma_check = &triflex_config_drive_xfer_rate;
	
	if (!noautodma)
		hwif->autodma = 1;
	hwif->drives[0].autodma = hwif->autodma;
	hwif->drives[1].autodma = hwif->autodma;
}

static ide_pci_device_t triflex_device __devinitdata = {
	.name		= "TRIFLEX",
	.init_hwif	= init_hwif_triflex,
	.channels	= 2,
	.autodma	= AUTODMA,
	.enablebits	= {{0x80, 0x01, 0x01}, {0x80, 0x02, 0x02}},
	.bootable	= ON_BOARD,
};

static int __devinit triflex_init_one(struct pci_dev *dev, 
		const struct pci_device_id *id)
{
	return ide_setup_pci_device(dev, &triflex_device);
}

static struct pci_device_id triflex_pci_tbl[] = {
	{ PCI_VENDOR_ID_COMPAQ, PCI_DEVICE_ID_COMPAQ_TRIFLEX_IDE,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, triflex_pci_tbl);

static struct pci_driver driver = {
	.name		= "TRIFLEX_IDE",
	.id_table	= triflex_pci_tbl,
	.probe		= triflex_init_one,
};

static int __init triflex_ide_init(void)
{
	return ide_pci_register_driver(&driver);
}

module_init(triflex_ide_init);

MODULE_AUTHOR("Torben Mathiasen");
MODULE_DESCRIPTION("PCI driver module for Compaq Triflex IDE");
MODULE_LICENSE("GPL");


