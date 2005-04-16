/*
 *  linux/drivers/ide/pci/slc90e66.c	Version 0.11	September 11, 2002
 *
 *  Copyright (C) 2000-2002 Andre Hedrick <andre@linux-ide.org>
 *
 * This a look-a-like variation of the ICH0 PIIX4 Ultra-66,
 * but this keeps the ISA-Bridge and slots alive.
 *
 */

#include <linux/config.h>
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

static u8 slc90e66_ratemask (ide_drive_t *drive)
{
	u8 mode	= 2;

	if (!eighty_ninty_three(drive))
		mode = min(mode, (u8)1);
	return mode;
}

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

/*
 *  Based on settings done by AMI BIOS
 *  (might be useful if drive is not registered in CMOS for any reason).
 */
static void slc90e66_tune_drive (ide_drive_t *drive, u8 pio)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;
	int is_slave		= (&hwif->drives[1] == drive);
	int master_port		= hwif->channel ? 0x42 : 0x40;
	int slave_port		= 0x44;
	unsigned long flags;
	u16 master_data;
	u8 slave_data;
				 /* ISP  RTC */
	u8 timings[][2]	= { { 0, 0 },
				    { 0, 0 },
				    { 1, 0 },
				    { 2, 1 },
				    { 2, 3 }, };

	pio = ide_get_best_pio_mode(drive, pio, 5, NULL);
	spin_lock_irqsave(&ide_lock, flags);
	pci_read_config_word(dev, master_port, &master_data);
	if (is_slave) {
		master_data = master_data | 0x4000;
		if (pio > 1)
			/* enable PPE, IE and TIME */
			master_data = master_data | 0x0070;
		pci_read_config_byte(dev, slave_port, &slave_data);
		slave_data = slave_data & (hwif->channel ? 0x0f : 0xf0);
		slave_data = slave_data | (((timings[pio][0] << 2) | timings[pio][1]) << (hwif->channel ? 4 : 0));
	} else {
		master_data = master_data & 0xccf8;
		if (pio > 1)
			/* enable PPE, IE and TIME */
			master_data = master_data | 0x0007;
		master_data = master_data | (timings[pio][0] << 12) | (timings[pio][1] << 8);
	}
	pci_write_config_word(dev, master_port, master_data);
	if (is_slave)
		pci_write_config_byte(dev, slave_port, slave_data);
	spin_unlock_irqrestore(&ide_lock, flags);
}

static int slc90e66_tune_chipset (ide_drive_t *drive, u8 xferspeed)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;
	u8 maslave		= hwif->channel ? 0x42 : 0x40;
	u8 speed	= ide_rate_filter(slc90e66_ratemask(drive), xferspeed);
	int sitre = 0, a_speed	= 7 << (drive->dn * 4);
	int u_speed = 0, u_flag = 1 << drive->dn;
	u16			reg4042, reg44, reg48, reg4a;

	pci_read_config_word(dev, maslave, &reg4042);
	sitre = (reg4042 & 0x4000) ? 1 : 0;
	pci_read_config_word(dev, 0x44, &reg44);
	pci_read_config_word(dev, 0x48, &reg48);
	pci_read_config_word(dev, 0x4a, &reg4a);

	switch(speed) {
#ifdef CONFIG_BLK_DEV_IDEDMA
		case XFER_UDMA_4:	u_speed = 4 << (drive->dn * 4); break;
		case XFER_UDMA_3:	u_speed = 3 << (drive->dn * 4); break;
		case XFER_UDMA_2:	u_speed = 2 << (drive->dn * 4); break;
		case XFER_UDMA_1:	u_speed = 1 << (drive->dn * 4); break;
		case XFER_UDMA_0:	u_speed = 0 << (drive->dn * 4); break;
		case XFER_MW_DMA_2:
		case XFER_MW_DMA_1:
		case XFER_SW_DMA_2:	break;
#endif /* CONFIG_BLK_DEV_IDEDMA */
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

	slc90e66_tune_drive(drive, slc90e66_dma_2_pio(speed));
	return (ide_config_drive_speed(drive, speed));
}

#ifdef CONFIG_BLK_DEV_IDEDMA
static int slc90e66_config_drive_for_dma (ide_drive_t *drive)
{
	u8 speed = ide_dma_speed(drive, slc90e66_ratemask(drive));

	if (!(speed)) {
		u8 tspeed = ide_get_best_pio_mode(drive, 255, 5, NULL);
		speed = slc90e66_dma_2_pio(XFER_PIO_0 + tspeed);
	}

	(void) slc90e66_tune_chipset(drive, speed);
	return ide_dma_enable(drive);
}

static int slc90e66_config_drive_xfer_rate (ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct hd_driveid *id	= drive->id;

	drive->init_speed = 0;

	if (id && (id->capability & 1) && drive->autodma) {

		if (ide_use_dma(drive)) {
			if (slc90e66_config_drive_for_dma(drive))
				return hwif->ide_dma_on(drive);
		}

		goto fast_ata_pio;

	} else if ((id->capability & 8) || (id->field_valid & 2)) {
fast_ata_pio:
		hwif->tuneproc(drive, 5);
		return hwif->ide_dma_off_quietly(drive);
	}
	/* IORDY not supported */
	return 0;
}
#endif /* CONFIG_BLK_DEV_IDEDMA */

static void __init init_hwif_slc90e66 (ide_hwif_t *hwif)
{
	u8 reg47 = 0;
	u8 mask = hwif->channel ? 0x01 : 0x02;  /* bit0:Primary */

	hwif->autodma = 0;

	if (!hwif->irq)
		hwif->irq = hwif->channel ? 15 : 14;

	hwif->speedproc = &slc90e66_tune_chipset;
	hwif->tuneproc = &slc90e66_tune_drive;

	pci_read_config_byte(hwif->pci_dev, 0x47, &reg47);

	if (!hwif->dma_base) {
		hwif->drives[0].autotune = 1;
		hwif->drives[1].autotune = 1;
		return;
	}

	hwif->atapi_dma = 1;
	hwif->ultra_mask = 0x1f;
	hwif->mwdma_mask = 0x07;
	hwif->swdma_mask = 0x07;

#ifdef CONFIG_BLK_DEV_IDEDMA 
	if (!(hwif->udma_four))
		/* bit[0(1)]: 0:80, 1:40 */
		hwif->udma_four = (reg47 & mask) ? 0 : 1;

	hwif->ide_dma_check = &slc90e66_config_drive_xfer_rate;
	if (!noautodma)
		hwif->autodma = 1;
	hwif->drives[0].autodma = hwif->autodma;
	hwif->drives[1].autodma = hwif->autodma;
#endif /* !CONFIG_BLK_DEV_IDEDMA */
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
	{ PCI_VENDOR_ID_EFAR, PCI_DEVICE_ID_EFAR_SLC90E66_1, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, slc90e66_pci_tbl);

static struct pci_driver driver = {
	.name		= "SLC90e66_IDE",
	.id_table	= slc90e66_pci_tbl,
	.probe		= slc90e66_init_one,
};

static int slc90e66_ide_init(void)
{
	return ide_pci_register_driver(&driver);
}

module_init(slc90e66_ide_init);

MODULE_AUTHOR("Andre Hedrick");
MODULE_DESCRIPTION("PCI driver module for SLC90E66 IDE");
MODULE_LICENSE("GPL");
