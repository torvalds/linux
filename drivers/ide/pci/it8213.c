/*
 * ITE 8213 IDE driver
 *
 * Copyright (C) 2006 Jack Lee
 * Copyright (C) 2006 Alan Cox
 * Copyright (C) 2007 Bartlomiej Zolnierkiewicz
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/hdreg.h>
#include <linux/ide.h>
#include <linux/init.h>

#include <asm/io.h>

/**
 *	it8213_dma_2_pio		-	return the PIO mode matching DMA
 *	@xfer_rate: transfer speed
 *
 *	Returns the nearest equivalent PIO timing for the PIO or DMA
 *	mode requested by the controller.
 */

static u8 it8213_dma_2_pio (u8 xfer_rate) {
	switch(xfer_rate) {
		case XFER_UDMA_6:
		case XFER_UDMA_5:
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
 *	it8213_tuneproc	-	tune a drive
 *	@drive: drive to tune
 *	@pio: desired PIO mode
 *
 *	Set the interface PIO mode.
 */

static void it8213_tuneproc (ide_drive_t *drive, u8 pio)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;
	int is_slave		= drive->dn & 1;
	int master_port		= 0x40;
	int slave_port		= 0x44;
	unsigned long flags;
	u16 master_data;
	u8 slave_data;
	static DEFINE_SPINLOCK(tune_lock);
	int control = 0;

	static const u8 timings[][2]= {
					{ 0, 0 },
					{ 0, 0 },
					{ 1, 0 },
					{ 2, 1 },
					{ 2, 3 }, };

	pio = ide_get_best_pio_mode(drive, pio, 4, NULL);

	spin_lock_irqsave(&tune_lock, flags);
	pci_read_config_word(dev, master_port, &master_data);

	if (pio > 1)
		control |= 1;	/* Programmable timing on */
	if (drive->media != ide_disk)
		control |= 4;	/* ATAPI */
	if (pio > 2)
		control |= 2;	/* IORDY */
	if (is_slave) {
		master_data |=  0x4000;
		master_data &= ~0x0070;
		if (pio > 1)
			master_data = master_data | (control << 4);
		pci_read_config_byte(dev, slave_port, &slave_data);
		slave_data = slave_data & 0xf0;
		slave_data = slave_data | (timings[pio][0] << 2) | timings[pio][1];
	} else {
		master_data &= ~0x3307;
		if (pio > 1)
			master_data = master_data | control;
		master_data = master_data | (timings[pio][0] << 12) | (timings[pio][1] << 8);
	}
	pci_write_config_word(dev, master_port, master_data);
	if (is_slave)
		pci_write_config_byte(dev, slave_port, slave_data);
	spin_unlock_irqrestore(&tune_lock, flags);
}

/**
 *	it8213_tune_chipset	-	set controller timings
 *	@drive: Drive to set up
 *	@xferspeed: speed we want to achieve
 *
 *	Tune the ITE chipset for the desired mode. If we can't achieve
 *	the desired mode then tune for a lower one, but ultimately
 *	make the thing work.
 */

static int it8213_tune_chipset (ide_drive_t *drive, u8 xferspeed)
{

	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;
	u8 maslave		= 0x40;
	u8 speed		= ide_rate_filter(drive, xferspeed);
	int a_speed		= 3 << (drive->dn * 4);
	int u_flag		= 1 << drive->dn;
	int v_flag		= 0x01 << drive->dn;
	int w_flag		= 0x10 << drive->dn;
	int u_speed		= 0;
	u16			reg4042, reg4a;
	u8			reg48, reg54, reg55;

	pci_read_config_word(dev, maslave, &reg4042);
	pci_read_config_byte(dev, 0x48, &reg48);
	pci_read_config_word(dev, 0x4a, &reg4a);
	pci_read_config_byte(dev, 0x54, &reg54);
	pci_read_config_byte(dev, 0x55, &reg55);

	switch(speed) {
		case XFER_UDMA_6:
		case XFER_UDMA_4:
		case XFER_UDMA_2:	u_speed = 2 << (drive->dn * 4); break;
		case XFER_UDMA_5:
		case XFER_UDMA_3:
		case XFER_UDMA_1:	u_speed = 1 << (drive->dn * 4); break;
		case XFER_UDMA_0:	u_speed = 0 << (drive->dn * 4); break;
			break;
		case XFER_MW_DMA_2:
		case XFER_MW_DMA_1:
		case XFER_SW_DMA_2:
			break;
		case XFER_PIO_4:
		case XFER_PIO_3:
		case XFER_PIO_2:
		case XFER_PIO_1:
		case XFER_PIO_0:
			break;
		default:
			return -1;
	}

	if (speed >= XFER_UDMA_0) {
		if (!(reg48 & u_flag))
			pci_write_config_byte(dev, 0x48, reg48 | u_flag);
		if (speed >= XFER_UDMA_5) {
			pci_write_config_byte(dev, 0x55, (u8) reg55|w_flag);
		} else {
			pci_write_config_byte(dev, 0x55, (u8) reg55 & ~w_flag);
		}

		if ((reg4a & a_speed) != u_speed)
			pci_write_config_word(dev, 0x4a, (reg4a & ~a_speed) | u_speed);
		if (speed > XFER_UDMA_2) {
			if (!(reg54 & v_flag))
				pci_write_config_byte(dev, 0x54, reg54 | v_flag);
		} else
			pci_write_config_byte(dev, 0x54, reg54 & ~v_flag);
	} else {
		if (reg48 & u_flag)
			pci_write_config_byte(dev, 0x48, reg48 & ~u_flag);
		if (reg4a & a_speed)
			pci_write_config_word(dev, 0x4a, reg4a & ~a_speed);
		if (reg54 & v_flag)
			pci_write_config_byte(dev, 0x54, reg54 & ~v_flag);
		if (reg55 & w_flag)
			pci_write_config_byte(dev, 0x55, (u8) reg55 & ~w_flag);
	}
	it8213_tuneproc(drive, it8213_dma_2_pio(speed));
	return ide_config_drive_speed(drive, speed);
}

/**
 *	it8213_configure_drive_for_dma	-	set up for DMA transfers
 *	@drive: drive we are going to set up
 *
 *	Set up the drive for DMA, tune the controller and drive as
 *	required. If the drive isn't suitable for DMA or we hit
 *	other problems then we will drop down to PIO and set up
 *	PIO appropriately
 */

static int it8213_config_drive_for_dma (ide_drive_t *drive)
{
	u8 pio;

	if (ide_tune_dma(drive))
		return 0;

	pio = ide_get_best_pio_mode(drive, 255, 4, NULL);
	it8213_tune_chipset(drive, XFER_PIO_0 + pio);

	return -1;
}

/**
 *	init_hwif_it8213	-	set up hwif structs
 *	@hwif: interface to set up
 *
 *	We do the basic set up of the interface structure. The IT8212
 *	requires several custom handlers so we override the default
 *	ide DMA handlers appropriately
 */

static void __devinit init_hwif_it8213(ide_hwif_t *hwif)
{
	u8 reg42h = 0, ata66 = 0;

	hwif->speedproc = &it8213_tune_chipset;
	hwif->tuneproc	= &it8213_tuneproc;

	hwif->autodma = 0;

	hwif->drives[0].autotune = 1;
	hwif->drives[1].autotune = 1;

	if (!hwif->dma_base)
		return;

	hwif->atapi_dma = 1;
	hwif->ultra_mask = 0x7f;
	hwif->mwdma_mask = 0x06;
	hwif->swdma_mask = 0x04;

	pci_read_config_byte(hwif->pci_dev, 0x42, &reg42h);
	ata66 = (reg42h & 0x02) ? 0 : 1;

	hwif->ide_dma_check = &it8213_config_drive_for_dma;
	if (!(hwif->udma_four))
		hwif->udma_four = ata66;

	/*
	 *	The BIOS often doesn't set up DMA on this controller
	 *	so we always do it.
	 */
	if (!noautodma)
		hwif->autodma = 1;

	hwif->drives[0].autodma = hwif->autodma;
	hwif->drives[1].autodma = hwif->autodma;
}


#define DECLARE_ITE_DEV(name_str)			\
	{						\
		.name		= name_str,		\
		.init_hwif	= init_hwif_it8213,	\
		.channels	= 1,			\
		.autodma	= AUTODMA,		\
		.enablebits	= {{0x41,0x80,0x80}}, \
		.bootable	= ON_BOARD,		\
	}

static ide_pci_device_t it8213_chipsets[] __devinitdata = {
	/* 0 */ DECLARE_ITE_DEV("IT8213"),
};


/**
 *	it8213_init_one	-	pci layer discovery entry
 *	@dev: PCI device
 *	@id: ident table entry
 *
 *	Called by the PCI code when it finds an ITE8213 controller. As
 *	this device follows the standard interfaces we can use the
 *	standard helper functions to do almost all the work for us.
 */

static int __devinit it8213_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	ide_setup_pci_device(dev, &it8213_chipsets[id->driver_data]);
	return 0;
}


static struct pci_device_id it8213_pci_tbl[] = {
	{ PCI_VENDOR_ID_ITE, 0x8213, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ 0, },
};

MODULE_DEVICE_TABLE(pci, it8213_pci_tbl);

static struct pci_driver driver = {
	.name		= "ITE8213_IDE",
	.id_table	= it8213_pci_tbl,
	.probe		= it8213_init_one,
};

static int __init it8213_ide_init(void)
{
	return ide_pci_register_driver(&driver);
}

module_init(it8213_ide_init);

MODULE_AUTHOR("Jack Lee, Alan Cox");
MODULE_DESCRIPTION("PCI driver module for the ITE 8213");
MODULE_LICENSE("GPL");
