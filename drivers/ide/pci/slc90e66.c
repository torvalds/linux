/*
 *  linux/drivers/ide/pci/slc90e66.c	Version 0.14	February 8, 2007
 *
 *  Copyright (C) 2000-2002 Andre Hedrick <andre@linux-ide.org>
 *  Copyright (C) 2006-2007 MontaVista Software, Inc. <source@mvista.com>
 *
 * This is a look-alike variation of the ICH0 PIIX4 Ultra-66,
 * but this keeps the ISA-Bridge and slots alive.
 *
 */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/hdreg.h>
#include <linux/ide.h>
#include <linux/delay.h>
#include <linux/init.h>

#include <asm/io.h>

static u8 slc90e66_dma_2_pio (u8 xfer_rate) {
	switch(xfer_rate) {
		case XFER_UDMA_4:
		case XFER_UDMA_3:
		case XFER_UDMA_2:
		case XFER_UDMA_1:
		case XFER_UDMA_0:
		case XFER_MW_DMA_2:
		case XFER_PIO_4:
			return 4;
		case XFER_MW_DMA_1:
		case XFER_PIO_3:
			return 3;
		case XFER_SW_DMA_2:
		case XFER_PIO_2:
			return 2;
		case XFER_MW_DMA_0:
		case XFER_SW_DMA_1:
		case XFER_SW_DMA_0:
		case XFER_PIO_1:
		case XFER_PIO_0:
		case XFER_PIO_SLOW:
		default:
			return 0;
	}
}

static void slc90e66_tune_pio (ide_drive_t *drive, u8 pio)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;
	int is_slave		= drive->dn & 1;
	int master_port		= hwif->channel ? 0x42 : 0x40;
	int slave_port		= 0x44;
	unsigned long flags;
	u16 master_data;
	u8 slave_data;
 	int control = 0;
				     /* ISP  RTC */
	static const u8 timings[][2]= {
					{ 0, 0 },
					{ 0, 0 },
					{ 1, 0 },
					{ 2, 1 },
					{ 2, 3 }, };

	spin_lock_irqsave(&ide_lock, flags);
	pci_read_config_word(dev, master_port, &master_data);

	if (pio > 1)
		control |= 1;	/* Programmable timing on */
	if (drive->media == ide_disk)
		control |= 4;	/* Prefetch, post write */
	if (pio > 2)
		control |= 2;	/* IORDY */
	if (is_slave) {
		master_data |=  0x4000;
		master_data &= ~0x0070;
		if (pio > 1) {
			/* Set PPE, IE and TIME */
			master_data |= control << 4;
		}
		pci_read_config_byte(dev, slave_port, &slave_data);
		slave_data &= hwif->channel ? 0x0f : 0xf0;
		slave_data |= ((timings[pio][0] << 2) | timings[pio][1]) <<
			       (hwif->channel ? 4 : 0);
	} else {
		master_data &= ~0x3307;
		if (pio > 1) {
			/* enable PPE, IE and TIME */
			master_data |= control;
		}
		master_data |= (timings[pio][0] << 12) | (timings[pio][1] << 8);
	}
	pci_write_config_word(dev, master_port, master_data);
	if (is_slave)
		pci_write_config_byte(dev, slave_port, slave_data);
	spin_unlock_irqrestore(&ide_lock, flags);
}

static void slc90e66_tune_drive (ide_drive_t *drive, u8 pio)
{
	pio = ide_get_best_pio_mode(drive, pio, 4, NULL);
	slc90e66_tune_pio(drive, pio);
	(void) ide_config_drive_speed(drive, XFER_PIO_0 + pio);
}

static int slc90e66_tune_chipset (ide_drive_t *drive, u8 xferspeed)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;
	u8 maslave		= hwif->channel ? 0x42 : 0x40;
	u8 speed		= ide_rate_filter(drive, xferspeed);
	int sitre = 0, a_speed	= 7 << (drive->dn * 4);
	int u_speed = 0, u_flag = 1 << drive->dn;
	u16			reg4042, reg44, reg48, reg4a;

	pci_read_config_word(dev, maslave, &reg4042);
	sitre = (reg4042 & 0x4000) ? 1 : 0;
	pci_read_config_word(dev, 0x44, &reg44);
	pci_read_config_word(dev, 0x48, &reg48);
	pci_read_config_word(dev, 0x4a, &reg4a);

	switch(speed) {
		case XFER_UDMA_4:	u_speed = 4 << (drive->dn * 4); break;
		case XFER_UDMA_3:	u_speed = 3 << (drive->dn * 4); break;
		case XFER_UDMA_2:	u_speed = 2 << (drive->dn * 4); break;
		case XFER_UDMA_1:	u_speed = 1 << (drive->dn * 4); break;
		case XFER_UDMA_0:	u_speed = 0 << (drive->dn * 4); break;
		case XFER_MW_DMA_2:
		case XFER_MW_DMA_1:
		case XFER_SW_DMA_2:	break;
		case XFER_PIO_4:
		case XFER_PIO_3:
		case XFER_PIO_2:
		case XFER_PIO_0:        break;
		default:		return -1;
	}

	if (speed >= XFER_UDMA_0) {
		if (!(reg48 & u_flag))
			pci_write_config_word(dev, 0x48, reg48|u_flag);
		/* FIXME: (reg4a & a_speed) ? */
		if ((reg4a & u_speed) != u_speed) {
			pci_write_config_word(dev, 0x4a, reg4a & ~a_speed);
			pci_read_config_word(dev, 0x4a, &reg4a);
			pci_write_config_word(dev, 0x4a, reg4a|u_speed);
		}
	} else {
		if (reg48 & u_flag)
			pci_write_config_word(dev, 0x48, reg48 & ~u_flag);
		if (reg4a & a_speed)
			pci_write_config_word(dev, 0x4a, reg4a & ~a_speed);
	}

	slc90e66_tune_pio(drive, slc90e66_dma_2_pio(speed));
	return ide_config_drive_speed(drive, speed);
}

static int slc90e66_config_drive_xfer_rate (ide_drive_t *drive)
{
	drive->init_speed = 0;

	if (ide_tune_dma(drive))
		return 0;

	if (ide_use_fast_pio(drive))
		slc90e66_tune_drive(drive, 255);

	return -1;
}

static void __devinit init_hwif_slc90e66 (ide_hwif_t *hwif)
{
	u8 reg47 = 0;
	u8 mask = hwif->channel ? 0x01 : 0x02;  /* bit0:Primary */

	hwif->autodma = 0;

	if (!hwif->irq)
		hwif->irq = hwif->channel ? 15 : 14;

	hwif->speedproc = &slc90e66_tune_chipset;
	hwif->tuneproc	= &slc90e66_tune_drive;

	pci_read_config_byte(hwif->pci_dev, 0x47, &reg47);

	if (!hwif->dma_base) {
		hwif->drives[0].autotune = 1;
		hwif->drives[1].autotune = 1;
		return;
	}

	hwif->atapi_dma = 1;
	hwif->ultra_mask = 0x1f;
	hwif->mwdma_mask = 0x06;
	hwif->swdma_mask = 0x04;

	if (hwif->cbl != ATA_CBL_PATA40_SHORT)
		/* bit[0(1)]: 0:80, 1:40 */
		hwif->cbl = (reg47 & mask) ? ATA_CBL_PATA40 : ATA_CBL_PATA80;

	hwif->ide_dma_check = &slc90e66_config_drive_xfer_rate;

	if (!noautodma)
		hwif->autodma = 1;
	hwif->drives[0].autodma = hwif->autodma;
	hwif->drives[1].autodma = hwif->autodma;
}

static ide_pci_device_t slc90e66_chipset __devinitdata = {
	.name		= "SLC90E66",
	.init_hwif	= init_hwif_slc90e66,
	.channels	= 2,
	.autodma	= AUTODMA,
	.enablebits	= {{0x41,0x80,0x80}, {0x43,0x80,0x80}},
	.bootable	= ON_BOARD,
};

static int __devinit slc90e66_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	return ide_setup_pci_device(dev, &slc90e66_chipset);
}

static struct pci_device_id slc90e66_pci_tbl[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_EFAR, PCI_DEVICE_ID_EFAR_SLC90E66_1), 0},
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, slc90e66_pci_tbl);

static struct pci_driver driver = {
	.name		= "SLC90e66_IDE",
	.id_table	= slc90e66_pci_tbl,
	.probe		= slc90e66_init_one,
};

static int __init slc90e66_ide_init(void)
{
	return ide_pci_register_driver(&driver);
}

module_init(slc90e66_ide_init);

MODULE_AUTHOR("Andre Hedrick");
MODULE_DESCRIPTION("PCI driver module for SLC90E66 IDE");
MODULE_LICENSE("GPL");
