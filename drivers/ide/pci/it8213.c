#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/hdreg.h>
#include <linux/ide.h>
#include <linux/init.h>

#include <asm/io.h>

/*
 *	it8213_ratemask	-	Compute available modes
 *	@drive: IDE drive
 *
 *	Compute the available speeds for the devices on the interface. This
 *	is all modes to ATA100 clipped by drive cable setup.
 */

static u8 it8213_ratemask (ide_drive_t *drive)
{
	u8 mode	= 4;
	if (!eighty_ninty_three(drive))
		mode = min(mode, (u8)1);
	return mode;
}

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

static spinlock_t tune_lock = SPIN_LOCK_UNLOCKED;

/*
 *	it8213_tuneproc	-	tune a drive
 *	@drive: drive to tune
 *	@mode_wanted: the target operating mode
 *
 *	Load the timing settings for this device mode into the
 *	controller. By the time we are called the mode has been
 *	modified as neccessary to handle the absence of seperate
 *	master/slave timers for MWDMA/PIO.
 *
 *	This code is only used in pass through mode.
 */

static void it8213_tuneproc (ide_drive_t *drive,  u8 pio)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;
	int is_slave		= (&hwif->drives[1] == drive);
	int master_port		= 0x40;
	int slave_port		= 0x44;
	unsigned long flags;
	u16 master_data;
	u8 slave_data;

	u8 timings[][2]	= { { 0, 0 },
			    { 0, 0 },
			    { 1, 0 },
			    { 2, 1 },
			    { 2, 3 }, };

	pio = ide_get_best_pio_mode(drive, pio, 5, NULL);

	spin_lock_irqsave(&tune_lock, flags);
	pci_read_config_word(dev, master_port, &master_data);
	if (is_slave) {
		master_data = master_data | 0x4000;
		if (pio > 1)

			master_data = master_data | 0x0070;
		pci_read_config_byte(dev, slave_port, &slave_data);
		slave_data = slave_data & 0xf0;
		slave_data = slave_data | (((timings[pio][0] << 2) | (timings[pio][1]) << 0));
	} else {
		master_data = master_data & 0xccf8;
		if (pio > 1)

			master_data = master_data | 0x0007;
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

static int it8213_tune_chipset (ide_drive_t *drive, byte xferspeed)
{

	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;
	u8 maslave		= 0x40;
	u8 speed		= ide_rate_filter(it8213_ratemask(drive), xferspeed);
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
		case XFER_UDMA_2:u_speed = 2 << (drive->dn * 4); break;
		case XFER_UDMA_5:
		case XFER_UDMA_3:
		case XFER_UDMA_1:u_speed = 1 << (drive->dn * 4); break;
		case XFER_UDMA_0:u_speed = 0 << (drive->dn * 4); break;
			break;
		case XFER_MW_DMA_2:
		case XFER_MW_DMA_1:
		case XFER_MW_DMA_0:
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

		if (speed >= XFER_UDMA_0)
		{
		if (!(reg48 & u_flag))
			pci_write_config_byte(dev, 0x48, reg48 | u_flag);
		if (speed >= XFER_UDMA_5) {
			pci_write_config_byte(dev, 0x55, (u8) reg55|w_flag);
		} else {
			pci_write_config_byte(dev, 0x55, (u8) reg55 & ~w_flag);
		}

		if ((reg4a & a_speed) != u_speed)
			pci_write_config_word(dev, 0x4a, (reg4a & ~a_speed) | u_speed);
		if (speed > XFER_UDMA_2)
		{
			if (!(reg54 & v_flag))
				pci_write_config_byte(dev, 0x54, reg54 | v_flag);
		} else
			pci_write_config_byte(dev, 0x54, reg54 & ~v_flag);
		} else /*if(speed >= XFER_UDMA_0)*/
		{
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


/*
 *	config_chipset_for_dma	-	configure for DMA
 *	@drive: drive to configure
 *
 *	Called by the IDE layer when it wants the timings set up.
 */

static int config_chipset_for_dma (ide_drive_t *drive)
{
	  u8 speed	= ide_dma_speed(drive, it8213_ratemask(drive));
	if (!speed)
	{
		u8 tspeed = ide_get_best_pio_mode(drive, 255, 5, NULL);
		speed = it8213_dma_2_pio(XFER_PIO_0 + tspeed);
	}
//	config_it8213_chipset_for_pio(drive, !speed);
	it8213_tune_chipset(drive, speed);
	return ide_dma_enable(drive);
}

/**
 *	config_it8213_chipset_for_pio	-	set drive timings
 *	@drive: drive to tune
 *	@speed we want
 *
 *	Compute the best pio mode we can for a given device. We must
 *	pick a speed that does not cause problems with the other device
 *	on the cable.
 */
/*
static void config_it8213_chipset_for_pio (ide_drive_t *drive, byte set_speed)
{
	ide_hwif_t *hwif	= HWIF(drive);
//	u8 unit = drive->select.b.unit;
	ide_hwif_t *hwif = drive->hwif;
	ide_drive_t *pair = &hwif->drives[1-unit];
	u8 speed = 0, set_pio	= ide_get_best_pio_mode(drive, 255, 5, NULL);
	u8 pair_pio;

	if(pair != NULL) {
		pair_pio = ide_get_best_pio_mode(pair, 255, 5, NULL);
		if(pair_pio < set_pio)
			set_pio = pair_pio;
	}
	it8213_tuneproc(drive, set_pio);
	speed = XFER_PIO_0 + set_pio;
	if (set_speed)
		(void) ide_config_drive_speed(drive, speed);
}
*/
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
//	ide_hwif_t *hwif	= drive->hwif;
//	struct hd_driveid *id = drive->id;
	ide_hwif_t *hwif	= HWIF(drive);
	if (ide_use_dma(drive)) {
		if (config_chipset_for_dma(drive))
			return hwif->ide_dma_on(drive);
	}
//	config_it8213_chipset_for_pio(drive, 1);
	hwif->tuneproc(drive, 255);
 	return hwif->ide_dma_off_quietly(drive);
}


static unsigned int __devinit init_chipset_it8213(struct pci_dev *dev, const char *name)
{
	printk(KERN_INFO "it8213: controller in IDE mode.\n");
	return 0;
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
	u8 mask = 0x02;

	hwif->atapi_dma = 1;

	hwif->speedproc = &it8213_tune_chipset;
	hwif->tuneproc	= &it8213_tuneproc;

	hwif->autodma = 0;

	hwif->drives[0].autotune = 1;
	hwif->drives[1].autotune = 1;

	if (!hwif->dma_base)
		goto fallback;
	hwif->atapi_dma = 1;
	hwif->ultra_mask = 0x7f;
	hwif->mwdma_mask = 0x07;
	hwif->swdma_mask = 0x07;

	pci_read_config_byte(hwif->pci_dev, 0x42, &reg42h);
	ata66 = (reg42h & mask) ? 0 : 1;

	hwif->ide_dma_check = &it8213_config_drive_for_dma;
	if (!(hwif->udma_four))
		hwif->udma_four = ata66;
//		hwif->udma_four = 0;

	/*
	 *	The BIOS often doesn't set up DMA on this controller
	 *	so we always do it.
	 */
	if (!noautodma)
		hwif->autodma = 1;

	hwif->drives[0].autodma = hwif->autodma;
	hwif->drives[1].autodma = hwif->autodma;
	return;
fallback:
	hwif->autodma = 0;
	return;
}


#define DECLARE_ITE_DEV(name_str)			\
	{						\
		.name		= name_str,		\
		.init_chipset	= init_chipset_it8213,	\
		.init_hwif	= init_hwif_it8213,	\
		.channels	= 1,			\
		.autodma		= AUTODMA,		\
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
	{ PCI_VENDOR_ID_ITE, PCI_DEVICE_ID_ITE_8213,  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
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
	return ide_pci_register_driver(&driver); }

module_init(it8213_ide_init);

MODULE_AUTHOR("Jack and Alan Cox");		/* Update this */
MODULE_DESCRIPTION("PCI driver module for the ITE 8213");
MODULE_LICENSE("GPL");
