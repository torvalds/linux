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

static void triflex_set_mode(ide_drive_t *drive, const u8 speed)
{
	ide_hwif_t *hwif = HWIF(drive);
	struct pci_dev *dev = hwif->pci_dev;
	u8 channel_offset = hwif->channel ? 0x74 : 0x70;
	u16 timing = 0;
	u32 triflex_timings = 0;
	u8 unit = (drive->select.b.unit & 0x01);
	
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
			return;
	}

	triflex_timings &= ~(0xFFFF << (16 * unit));
	triflex_timings |= (timing << (16 * unit));
	
	pci_write_config_dword(dev, channel_offset, triflex_timings);
}

static void triflex_set_pio_mode(ide_drive_t *drive, const u8 pio)
{
	triflex_set_mode(drive, XFER_PIO_0 + pio);
}

static void __devinit init_hwif_triflex(ide_hwif_t *hwif)
{
	hwif->set_pio_mode = &triflex_set_pio_mode;
	hwif->set_dma_mode = &triflex_set_mode;
}

static ide_pci_device_t triflex_device __devinitdata = {
	.name		= "TRIFLEX",
	.init_hwif	= init_hwif_triflex,
	.enablebits	= {{0x80, 0x01, 0x01}, {0x80, 0x02, 0x02}},
	.host_flags	= IDE_HFLAG_BOOTABLE,
	.pio_mask	= ATA_PIO4,
	.swdma_mask	= ATA_SWDMA2,
	.mwdma_mask	= ATA_MWDMA2,
};

static int __devinit triflex_init_one(struct pci_dev *dev, 
		const struct pci_device_id *id)
{
	return ide_setup_pci_device(dev, &triflex_device);
}

static const struct pci_device_id triflex_pci_tbl[] = {
	{ PCI_VDEVICE(COMPAQ, PCI_DEVICE_ID_COMPAQ_TRIFLEX_IDE), 0 },
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


