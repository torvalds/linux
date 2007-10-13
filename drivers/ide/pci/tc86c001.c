/*
 * drivers/ide/pci/tc86c001.c	Version 1.00	Dec 12, 2006
 *
 * Copyright (C) 2002 Toshiba Corporation
 * Copyright (C) 2005-2006 MontaVista Software, Inc. <source@mvista.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/types.h>
#include <linux/pci.h>
#include <linux/ide.h>

static void tc86c001_set_mode(ide_drive_t *drive, const u8 speed)
{
	ide_hwif_t *hwif	= HWIF(drive);
	unsigned long scr_port	= hwif->config_data + (drive->dn ? 0x02 : 0x00);
	u16 mode, scr		= hwif->INW(scr_port);

	switch (speed) {
		case XFER_UDMA_4:	mode = 0x00c0; break;
		case XFER_UDMA_3:	mode = 0x00b0; break;
		case XFER_UDMA_2:	mode = 0x00a0; break;
		case XFER_UDMA_1:	mode = 0x0090; break;
		case XFER_UDMA_0:	mode = 0x0080; break;
		case XFER_MW_DMA_2:	mode = 0x0070; break;
		case XFER_MW_DMA_1:	mode = 0x0060; break;
		case XFER_MW_DMA_0:	mode = 0x0050; break;
		case XFER_PIO_4:	mode = 0x0400; break;
		case XFER_PIO_3:	mode = 0x0300; break;
		case XFER_PIO_2:	mode = 0x0200; break;
		case XFER_PIO_1:	mode = 0x0100; break;
		case XFER_PIO_0:
		default:		mode = 0x0000; break;
	}

	scr &= (speed < XFER_MW_DMA_0) ? 0xf8ff : 0xff0f;
	scr |= mode;
	outw(scr, scr_port);
}

static void tc86c001_set_pio_mode(ide_drive_t *drive, const u8 pio)
{
	tc86c001_set_mode(drive, XFER_PIO_0 + pio);
}

/*
 * HACKITY HACK
 *
 * This is a workaround for the limitation 5 of the TC86C001 IDE controller:
 * if a DMA transfer terminates prematurely, the controller leaves the device's
 * interrupt request (INTRQ) pending and does not generate a PCI interrupt (or
 * set the interrupt bit in the DMA status register), thus no PCI interrupt
 * will occur until a DMA transfer has been successfully completed.
 *
 * We work around this by initiating dummy, zero-length DMA transfer on
 * a DMA timeout expiration. I found no better way to do this with the current
 * IDE core than to temporarily replace a higher level driver's timer expiry
 * handler with our own backing up to that handler in case our recovery fails.
 */
static int tc86c001_timer_expiry(ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);
	ide_expiry_t *expiry	= ide_get_hwifdata(hwif);
	ide_hwgroup_t *hwgroup	= HWGROUP(drive);
	u8 dma_stat		= hwif->INB(hwif->dma_status);

	/* Restore a higher level driver's expiry handler first. */
	hwgroup->expiry	= expiry;

	if ((dma_stat & 5) == 1) {	/* DMA active and no interrupt */
		unsigned long sc_base	= hwif->config_data;
		unsigned long twcr_port	= sc_base + (drive->dn ? 0x06 : 0x04);
		u8 dma_cmd		= hwif->INB(hwif->dma_command);

		printk(KERN_WARNING "%s: DMA interrupt possibly stuck, "
		       "attempting recovery...\n", drive->name);

		/* Stop DMA */
		outb(dma_cmd & ~0x01, hwif->dma_command);

		/* Setup the dummy DMA transfer */
		outw(0, sc_base + 0x0a);	/* Sector Count */
		outw(0, twcr_port);	/* Transfer Word Count 1 or 2 */

		/* Start the dummy DMA transfer */
		outb(0x00, hwif->dma_command); /* clear R_OR_WCTR for write */
		outb(0x01, hwif->dma_command); /* set START_STOPBM */

		/*
		 * If an interrupt was pending, it should come thru shortly.
		 * If not, a higher level driver's expiry handler should
		 * eventually cause some kind of recovery from the DMA stall.
		 */
		return WAIT_MIN_SLEEP;
	}

	/* Chain to the restored expiry handler if DMA wasn't active. */
	if (likely(expiry != NULL))
		return expiry(drive);

	/* If there was no handler, "emulate" that for ide_timer_expiry()... */
	return -1;
}

static void tc86c001_dma_start(ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);
	ide_hwgroup_t *hwgroup	= HWGROUP(drive);
	unsigned long sc_base	= hwif->config_data;
	unsigned long twcr_port	= sc_base + (drive->dn ? 0x06 : 0x04);
	unsigned long nsectors	= hwgroup->rq->nr_sectors;

	/*
	 * We have to manually load the sector count and size into
	 * the appropriate system control registers for DMA to work
	 * with LBA48 and ATAPI devices...
	 */
	outw(nsectors, sc_base + 0x0a);	/* Sector Count */
	outw(SECTOR_SIZE / 2, twcr_port); /* Transfer Word Count 1/2 */

	/* Install our timeout expiry hook, saving the current handler... */
	ide_set_hwifdata(hwif, hwgroup->expiry);
	hwgroup->expiry = &tc86c001_timer_expiry;

	ide_dma_start(drive);
}

static int tc86c001_busproc(ide_drive_t *drive, int state)
{
	ide_hwif_t *hwif	= HWIF(drive);
	unsigned long sc_base	= hwif->config_data;
	u16 scr1;

	/* System Control 1 Register bit 11 (ATA Hard Reset) read */
	scr1 = hwif->INW(sc_base + 0x00);

	switch (state) {
		case BUSSTATE_ON:
			if (!(scr1 & 0x0800))
				return 0;
			scr1 &= ~0x0800;

			hwif->drives[0].failures = hwif->drives[1].failures = 0;
			break;
		case BUSSTATE_OFF:
			if (scr1 & 0x0800)
				return 0;
			scr1 |= 0x0800;

			hwif->drives[0].failures = hwif->drives[0].max_failures + 1;
			hwif->drives[1].failures = hwif->drives[1].max_failures + 1;
			break;
		default:
			return -EINVAL;
	}

	/* System Control 1 Register bit 11 (ATA Hard Reset) write */
	outw(scr1, sc_base + 0x00);
	return 0;
}

static int tc86c001_config_drive_xfer_rate(ide_drive_t *drive)
{
	if (ide_tune_dma(drive))
		return 0;

	if (ide_use_fast_pio(drive))
		ide_set_max_pio(drive);

	return -1;
}

static void __devinit init_hwif_tc86c001(ide_hwif_t *hwif)
{
	unsigned long sc_base	= pci_resource_start(hwif->pci_dev, 5);
	u16 scr1		= hwif->INW(sc_base + 0x00);;

	/* System Control 1 Register bit 15 (Soft Reset) set */
	outw(scr1 |  0x8000, sc_base + 0x00);

	/* System Control 1 Register bit 14 (FIFO Reset) set */
	outw(scr1 |  0x4000, sc_base + 0x00);

	/* System Control 1 Register: reset clear */
	outw(scr1 & ~0xc000, sc_base + 0x00);

	/* Store the system control register base for convenience... */
	hwif->config_data = sc_base;

	hwif->set_pio_mode = &tc86c001_set_pio_mode;
	hwif->set_dma_mode = &tc86c001_set_mode;

	hwif->busproc	= &tc86c001_busproc;

	hwif->drives[0].autotune = hwif->drives[1].autotune = 1;

	if (!hwif->dma_base)
		return;

	/*
	 * Sector Count Control Register bits 0 and 1 set:
	 * software sets Sector Count Register for master and slave device
	 */
	outw(0x0003, sc_base + 0x0c);

	/* Sector Count Register limit */
	hwif->rqsize	 = 0xffff;

	hwif->atapi_dma  = 1;
	hwif->ultra_mask = 0x1f;
	hwif->mwdma_mask = 0x07;

	hwif->ide_dma_check	= &tc86c001_config_drive_xfer_rate;
	hwif->dma_start 	= &tc86c001_dma_start;

	if (hwif->cbl != ATA_CBL_PATA40_SHORT) {
		/*
		 * System Control  1 Register bit 13 (PDIAGN):
		 * 0=80-pin cable, 1=40-pin cable
		 */
		scr1 = hwif->INW(sc_base + 0x00);
		hwif->cbl = (scr1 & 0x2000) ? ATA_CBL_PATA40 : ATA_CBL_PATA80;
	}

	if (!noautodma)
		hwif->autodma = 1;
	hwif->drives[0].autodma = hwif->drives[1].autodma = hwif->autodma;
}

static unsigned int __devinit init_chipset_tc86c001(struct pci_dev *dev,
							const char *name)
{
	int err = pci_request_region(dev, 5, name);

	if (err)
		printk(KERN_ERR "%s: system control regs already in use", name);
	return err;
}

static ide_pci_device_t tc86c001_chipset __devinitdata = {
	.name		= "TC86C001",
	.init_chipset	= init_chipset_tc86c001,
	.init_hwif	= init_hwif_tc86c001,
	.autodma	= AUTODMA,
	.bootable	= OFF_BOARD,
	.host_flags	= IDE_HFLAG_SINGLE,
	.pio_mask	= ATA_PIO4,
};

static int __devinit tc86c001_init_one(struct pci_dev *dev,
				       const struct pci_device_id *id)
{
	return ide_setup_pci_device(dev, &tc86c001_chipset);
}

static struct pci_device_id tc86c001_pci_tbl[] = {
	{ PCI_VENDOR_ID_TOSHIBA_2, PCI_DEVICE_ID_TOSHIBA_TC86C001_IDE,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, tc86c001_pci_tbl);

static struct pci_driver driver = {
	.name		= "TC86C001",
	.id_table	= tc86c001_pci_tbl,
	.probe		= tc86c001_init_one
};

static int __init tc86c001_ide_init(void)
{
	return ide_pci_register_driver(&driver);
}
module_init(tc86c001_ide_init);

MODULE_AUTHOR("MontaVista Software, Inc. <source@mvista.com>");
MODULE_DESCRIPTION("PCI driver module for TC86C001 IDE");
MODULE_LICENSE("GPL");
