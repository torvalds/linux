/*
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
 *	Not publicly available.
 */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/ide.h>
#include <linux/init.h>

#define DRV_NAME "triflex"

static void triflex_set_mode(ide_hwif_t *hwif, ide_drive_t *drive)
{
	struct pci_dev *dev = to_pci_dev(hwif->dev);
	u32 triflex_timings = 0;
	u16 timing = 0;
	u8 channel_offset = hwif->channel ? 0x74 : 0x70, unit = drive->dn & 1;

	pci_read_config_dword(dev, channel_offset, &triflex_timings);

	switch (drive->dma_mode) {
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
	}

	triflex_timings &= ~(0xFFFF << (16 * unit));
	triflex_timings |= (timing << (16 * unit));
	
	pci_write_config_dword(dev, channel_offset, triflex_timings);
}

static void triflex_set_pio_mode(ide_hwif_t *hwif, ide_drive_t *drive)
{
	drive->dma_mode = drive->pio_mode;
	triflex_set_mode(hwif, drive);
}

static const struct ide_port_ops triflex_port_ops = {
	.set_pio_mode		= triflex_set_pio_mode,
	.set_dma_mode		= triflex_set_mode,
};

static const struct ide_port_info triflex_device __devinitdata = {
	.name		= DRV_NAME,
	.enablebits	= {{0x80, 0x01, 0x01}, {0x80, 0x02, 0x02}},
	.port_ops	= &triflex_port_ops,
	.pio_mask	= ATA_PIO4,
	.swdma_mask	= ATA_SWDMA2,
	.mwdma_mask	= ATA_MWDMA2,
};

static int __devinit triflex_init_one(struct pci_dev *dev, 
		const struct pci_device_id *id)
{
	return ide_pci_init_one(dev, &triflex_device, NULL);
}

static const struct pci_device_id triflex_pci_tbl[] = {
	{ PCI_VDEVICE(COMPAQ, PCI_DEVICE_ID_COMPAQ_TRIFLEX_IDE), 0 },
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, triflex_pci_tbl);

static struct pci_driver triflex_pci_driver = {
	.name		= "TRIFLEX_IDE",
	.id_table	= triflex_pci_tbl,
	.probe		= triflex_init_one,
	.remove		= ide_pci_remove,
	.suspend	= ide_pci_suspend,
	.resume		= ide_pci_resume,
};

static int __init triflex_ide_init(void)
{
	return ide_pci_register_driver(&triflex_pci_driver);
}

static void __exit triflex_ide_exit(void)
{
	pci_unregister_driver(&triflex_pci_driver);
}

module_init(triflex_ide_init);
module_exit(triflex_ide_exit);

MODULE_AUTHOR("Torben Mathiasen");
MODULE_DESCRIPTION("PCI driver module for Compaq Triflex IDE");
MODULE_LICENSE("GPL");


