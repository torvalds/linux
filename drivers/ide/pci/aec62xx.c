/*
 * linux/drivers/ide/pci/aec62xx.c		Version 0.11	March 27, 2002
 *
 * Copyright (C) 1999-2002	Andre Hedrick <andre@linux-ide.org>
 *
 */

#include <linux/module.h>
#include <linux/config.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/hdreg.h>
#include <linux/ide.h>
#include <linux/init.h>

#include <asm/io.h>

struct chipset_bus_clock_list_entry {
	u8 xfer_speed;
	u8 chipset_settings;
	u8 ultra_settings;
};

static struct chipset_bus_clock_list_entry aec6xxx_33_base [] = {
	{	XFER_UDMA_6,	0x31,	0x07	},
	{	XFER_UDMA_5,	0x31,	0x06	},
	{	XFER_UDMA_4,	0x31,	0x05	},
	{	XFER_UDMA_3,	0x31,	0x04	},
	{	XFER_UDMA_2,	0x31,	0x03	},
	{	XFER_UDMA_1,	0x31,	0x02	},
	{	XFER_UDMA_0,	0x31,	0x01	},

	{	XFER_MW_DMA_2,	0x31,	0x00	},
	{	XFER_MW_DMA_1,	0x31,	0x00	},
	{	XFER_MW_DMA_0,	0x0a,	0x00	},
	{	XFER_PIO_4,	0x31,	0x00	},
	{	XFER_PIO_3,	0x33,	0x00	},
	{	XFER_PIO_2,	0x08,	0x00	},
	{	XFER_PIO_1,	0x0a,	0x00	},
	{	XFER_PIO_0,	0x00,	0x00	},
	{	0,		0x00,	0x00	}
};

static struct chipset_bus_clock_list_entry aec6xxx_34_base [] = {
	{	XFER_UDMA_6,	0x41,	0x06	},
	{	XFER_UDMA_5,	0x41,	0x05	},
	{	XFER_UDMA_4,	0x41,	0x04	},
	{	XFER_UDMA_3,	0x41,	0x03	},
	{	XFER_UDMA_2,	0x41,	0x02	},
	{	XFER_UDMA_1,	0x41,	0x01	},
	{	XFER_UDMA_0,	0x41,	0x01	},

	{	XFER_MW_DMA_2,	0x41,	0x00	},
	{	XFER_MW_DMA_1,	0x42,	0x00	},
	{	XFER_MW_DMA_0,	0x7a,	0x00	},
	{	XFER_PIO_4,	0x41,	0x00	},
	{	XFER_PIO_3,	0x43,	0x00	},
	{	XFER_PIO_2,	0x78,	0x00	},
	{	XFER_PIO_1,	0x7a,	0x00	},
	{	XFER_PIO_0,	0x70,	0x00	},
	{	0,		0x00,	0x00	}
};

#define BUSCLOCK(D)	\
	((struct chipset_bus_clock_list_entry *) pci_get_drvdata((D)))

#if 0
		if (dev->device == PCI_DEVICE_ID_ARTOP_ATP850UF) {
			(void) pci_read_config_byte(dev, 0x54, &art);
			p += sprintf(p, "DMA Mode:       %s(%s)",
				(c0&0x20)?((art&0x03)?"UDMA":" DMA"):" PIO",
				(art&0x02)?"2":(art&0x01)?"1":"0");
			p += sprintf(p, "          %s(%s)",
				(c0&0x40)?((art&0x0c)?"UDMA":" DMA"):" PIO",
				(art&0x08)?"2":(art&0x04)?"1":"0");
			p += sprintf(p, "         %s(%s)",
				(c1&0x20)?((art&0x30)?"UDMA":" DMA"):" PIO",
				(art&0x20)?"2":(art&0x10)?"1":"0");
			p += sprintf(p, "           %s(%s)\n",
				(c1&0x40)?((art&0xc0)?"UDMA":" DMA"):" PIO",
				(art&0x80)?"2":(art&0x40)?"1":"0");
		} else {
#endif

/*
 * TO DO: active tuning and correction of cards without a bios.
 */
static u8 pci_bus_clock_list (u8 speed, struct chipset_bus_clock_list_entry * chipset_table)
{
	for ( ; chipset_table->xfer_speed ; chipset_table++)
		if (chipset_table->xfer_speed == speed) {
			return chipset_table->chipset_settings;
		}
	return chipset_table->chipset_settings;
}

static u8 pci_bus_clock_list_ultra (u8 speed, struct chipset_bus_clock_list_entry * chipset_table)
{
	for ( ; chipset_table->xfer_speed ; chipset_table++)
		if (chipset_table->xfer_speed == speed) {
			return chipset_table->ultra_settings;
		}
	return chipset_table->ultra_settings;
}

static u8 aec62xx_ratemask (ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);
	u8 mode;

	switch(hwif->pci_dev->device) {
		case PCI_DEVICE_ID_ARTOP_ATP865:
		case PCI_DEVICE_ID_ARTOP_ATP865R:
#if 0
			mode = (hwif->INB(hwif->dma_master) & 0x10) ? 4 : 3;
#else
			mode = (hwif->INB(((hwif->channel) ?
					hwif->mate->dma_status :
					hwif->dma_status)) & 0x10) ? 4 : 3;
#endif
			break;
		case PCI_DEVICE_ID_ARTOP_ATP860:
		case PCI_DEVICE_ID_ARTOP_ATP860R:
			mode = 2;
			break;
		case PCI_DEVICE_ID_ARTOP_ATP850UF:
		default:
			return 1;
	}

	if (!eighty_ninty_three(drive))
		mode = min(mode, (u8)1);
	return mode;
}

static int aec6210_tune_chipset (ide_drive_t *drive, u8 xferspeed)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;
	u16 d_conf		= 0;
	u8 speed	= ide_rate_filter(aec62xx_ratemask(drive), xferspeed);
	u8 ultra = 0, ultra_conf = 0;
	u8 tmp0 = 0, tmp1 = 0, tmp2 = 0;
	unsigned long flags;

	local_irq_save(flags);
	/* 0x40|(2*drive->dn): Active, 0x41|(2*drive->dn): Recovery */
	pci_read_config_word(dev, 0x40|(2*drive->dn), &d_conf);
	tmp0 = pci_bus_clock_list(speed, BUSCLOCK(dev));
	d_conf = ((tmp0 & 0xf0) << 4) | (tmp0 & 0xf);
	pci_write_config_word(dev, 0x40|(2*drive->dn), d_conf);

	tmp1 = 0x00;
	tmp2 = 0x00;
	pci_read_config_byte(dev, 0x54, &ultra);
	tmp1 = ((0x00 << (2*drive->dn)) | (ultra & ~(3 << (2*drive->dn))));
	ultra_conf = pci_bus_clock_list_ultra(speed, BUSCLOCK(dev));
	tmp2 = ((ultra_conf << (2*drive->dn)) | (tmp1 & ~(3 << (2*drive->dn))));
	pci_write_config_byte(dev, 0x54, tmp2);
	local_irq_restore(flags);
	return(ide_config_drive_speed(drive, speed));
}

static int aec6260_tune_chipset (ide_drive_t *drive, u8 xferspeed)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;
	u8 speed	= ide_rate_filter(aec62xx_ratemask(drive), xferspeed);
	u8 unit		= (drive->select.b.unit & 0x01);
	u8 tmp1 = 0, tmp2 = 0;
	u8 ultra = 0, drive_conf = 0, ultra_conf = 0;
	unsigned long flags;

	local_irq_save(flags);
	/* high 4-bits: Active, low 4-bits: Recovery */
	pci_read_config_byte(dev, 0x40|drive->dn, &drive_conf);
	drive_conf = pci_bus_clock_list(speed, BUSCLOCK(dev));
	pci_write_config_byte(dev, 0x40|drive->dn, drive_conf);

	pci_read_config_byte(dev, (0x44|hwif->channel), &ultra);
	tmp1 = ((0x00 << (4*unit)) | (ultra & ~(7 << (4*unit))));
	ultra_conf = pci_bus_clock_list_ultra(speed, BUSCLOCK(dev));
	tmp2 = ((ultra_conf << (4*unit)) | (tmp1 & ~(7 << (4*unit))));
	pci_write_config_byte(dev, (0x44|hwif->channel), tmp2);
	local_irq_restore(flags);
	return(ide_config_drive_speed(drive, speed));
}

static int aec62xx_tune_chipset (ide_drive_t *drive, u8 speed)
{
	switch (HWIF(drive)->pci_dev->device) {
		case PCI_DEVICE_ID_ARTOP_ATP865:
		case PCI_DEVICE_ID_ARTOP_ATP865R:
		case PCI_DEVICE_ID_ARTOP_ATP860:
		case PCI_DEVICE_ID_ARTOP_ATP860R:
			return ((int) aec6260_tune_chipset(drive, speed));
		case PCI_DEVICE_ID_ARTOP_ATP850UF:
			return ((int) aec6210_tune_chipset(drive, speed));
		default:
			return -1;
	}
}

static int config_chipset_for_dma (ide_drive_t *drive)
{
	u8 speed = ide_dma_speed(drive, aec62xx_ratemask(drive));	

	if (!(speed))
		return 0;

	(void) aec62xx_tune_chipset(drive, speed);
	return ide_dma_enable(drive);
}

static void aec62xx_tune_drive (ide_drive_t *drive, u8 pio)
{
	u8 speed = 0;
	u8 new_pio = XFER_PIO_0 + ide_get_best_pio_mode(drive, 255, 5, NULL);

	switch(pio) {
		case 5:		speed = new_pio; break;
		case 4:		speed = XFER_PIO_4; break;
		case 3:		speed = XFER_PIO_3; break;
		case 2:		speed = XFER_PIO_2; break;
		case 1:		speed = XFER_PIO_1; break;
		default:	speed = XFER_PIO_0; break;
	}
	(void) aec62xx_tune_chipset(drive, speed);
}

static int aec62xx_config_drive_xfer_rate (ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct hd_driveid *id	= drive->id;

	if ((id->capability & 1) && drive->autodma) {

		if (ide_use_dma(drive)) {
			if (config_chipset_for_dma(drive))
				return hwif->ide_dma_on(drive);
		}

		goto fast_ata_pio;

	} else if ((id->capability & 8) || (id->field_valid & 2)) {
fast_ata_pio:
		aec62xx_tune_drive(drive, 5);
		return hwif->ide_dma_off_quietly(drive);
	}
	/* IORDY not supported */
	return 0;
}

static int aec62xx_irq_timeout (ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;

	switch(dev->device) {
		case PCI_DEVICE_ID_ARTOP_ATP860:
		case PCI_DEVICE_ID_ARTOP_ATP860R:
		case PCI_DEVICE_ID_ARTOP_ATP865:
		case PCI_DEVICE_ID_ARTOP_ATP865R:
			printk(" AEC62XX time out ");
#if 0
			{
				int i = 0;
				u8 reg49h = 0;
				pci_read_config_byte(HWIF(drive)->pci_dev, 0x49, &reg49h);
				for (i=0;i<256;i++)
					pci_write_config_byte(HWIF(drive)->pci_dev, 0x49, reg49h|0x10);
				pci_write_config_byte(HWIF(drive)->pci_dev, 0x49, reg49h & ~0x10);
			}
			return 0;
#endif
		default:
			break;
	}
#if 0
	{
		ide_hwif_t *hwif	= HWIF(drive);
		struct pci_dev *dev	= hwif->pci_dev;
		u8 tmp1 = 0, tmp2 = 0, mode6 = 0;

		pci_read_config_byte(dev, 0x44, &tmp1);
		pci_read_config_byte(dev, 0x45, &tmp2);
		printk(" AEC6280 r44=%x r45=%x ",tmp1,tmp2);
		mode6 = HWIF(drive)->INB(((hwif->channel) ?
					   hwif->mate->dma_status :
					   hwif->dma_status));
		printk(" AEC6280 133=%x ", (mode6 & 0x10));
	}
#endif
	return 0;
}

static unsigned int __devinit init_chipset_aec62xx(struct pci_dev *dev, const char *name)
{
	int bus_speed = system_bus_clock();

	if (dev->resource[PCI_ROM_RESOURCE].start) {
		pci_write_config_dword(dev, PCI_ROM_ADDRESS, dev->resource[PCI_ROM_RESOURCE].start | PCI_ROM_ADDRESS_ENABLE);
		printk(KERN_INFO "%s: ROM enabled at 0x%08lx\n", name, dev->resource[PCI_ROM_RESOURCE].start);
	}

	if (bus_speed <= 33)
		pci_set_drvdata(dev, (void *) aec6xxx_33_base);
	else
		pci_set_drvdata(dev, (void *) aec6xxx_34_base);

	return dev->irq;
}

static void __devinit init_hwif_aec62xx(ide_hwif_t *hwif)
{
	hwif->autodma = 0;
	hwif->tuneproc = &aec62xx_tune_drive;
	hwif->speedproc = &aec62xx_tune_chipset;

	if (hwif->pci_dev->device == PCI_DEVICE_ID_ARTOP_ATP850UF) {
		hwif->serialized = hwif->channel;
		hwif->no_dsc = 1;
	}

	if (hwif->mate)
		hwif->mate->serialized = hwif->serialized;

	if (!hwif->dma_base) {
		hwif->drives[0].autotune = 1;
		hwif->drives[1].autotune = 1;
		return;
	}

	hwif->ultra_mask = 0x7f;
	hwif->mwdma_mask = 0x07;
	hwif->swdma_mask = 0x07;

	hwif->ide_dma_check	= &aec62xx_config_drive_xfer_rate;
	hwif->ide_dma_lostirq	= &aec62xx_irq_timeout;
	hwif->ide_dma_timeout	= &aec62xx_irq_timeout;
	if (!noautodma)
		hwif->autodma = 1;
	hwif->drives[0].autodma = hwif->autodma;
	hwif->drives[1].autodma = hwif->autodma;
}

static void __devinit init_dma_aec62xx(ide_hwif_t *hwif, unsigned long dmabase)
{
	struct pci_dev *dev	= hwif->pci_dev;

	if (dev->device == PCI_DEVICE_ID_ARTOP_ATP850UF) {
		u8 reg54h = 0;
		unsigned long flags;

		spin_lock_irqsave(&ide_lock, flags);
		pci_read_config_byte(dev, 0x54, &reg54h);
		pci_write_config_byte(dev, 0x54, reg54h & ~(hwif->channel ? 0xF0 : 0x0F));
		spin_unlock_irqrestore(&ide_lock, flags);
	} else {
		u8 ata66	= 0;
		pci_read_config_byte(hwif->pci_dev, 0x49, &ata66);
	        if (!(hwif->udma_four))
			hwif->udma_four = (ata66&(hwif->channel?0x02:0x01))?0:1;
	}

	ide_setup_dma(hwif, dmabase, 8);
}

static int __devinit init_setup_aec62xx(struct pci_dev *dev, ide_pci_device_t *d)
{
	return ide_setup_pci_device(dev, d);
}

static int __devinit init_setup_aec6x80(struct pci_dev *dev, ide_pci_device_t *d)
{
	unsigned long bar4reg = pci_resource_start(dev, 4);

	if (inb(bar4reg+2) & 0x10) {
		strcpy(d->name, "AEC6880");
		if (dev->device == PCI_DEVICE_ID_ARTOP_ATP865R)
			strcpy(d->name, "AEC6880R");
	} else {
		strcpy(d->name, "AEC6280");
		if (dev->device == PCI_DEVICE_ID_ARTOP_ATP865R)
			strcpy(d->name, "AEC6280R");
	}

	return ide_setup_pci_device(dev, d);
}

static ide_pci_device_t aec62xx_chipsets[] __devinitdata = {
	{	/* 0 */
		.name		= "AEC6210",
		.init_setup	= init_setup_aec62xx,
		.init_chipset	= init_chipset_aec62xx,
		.init_hwif	= init_hwif_aec62xx,
		.init_dma	= init_dma_aec62xx,
		.channels	= 2,
		.autodma	= AUTODMA,
		.enablebits	= {{0x4a,0x02,0x02}, {0x4a,0x04,0x04}},
		.bootable	= OFF_BOARD,
	},{	/* 1 */
		.name		= "AEC6260",
		.init_setup	= init_setup_aec62xx,
		.init_chipset	= init_chipset_aec62xx,
		.init_hwif	= init_hwif_aec62xx,
		.init_dma	= init_dma_aec62xx,
		.channels	= 2,
		.autodma	= NOAUTODMA,
		.bootable	= OFF_BOARD,
	},{	/* 2 */
		.name		= "AEC6260R",
		.init_setup	= init_setup_aec62xx,
		.init_chipset	= init_chipset_aec62xx,
		.init_hwif	= init_hwif_aec62xx,
		.init_dma	= init_dma_aec62xx,
		.channels	= 2,
		.autodma	= AUTODMA,
		.enablebits	= {{0x4a,0x02,0x02}, {0x4a,0x04,0x04}},
		.bootable	= NEVER_BOARD,
	},{	/* 3 */
		.name		= "AEC6X80",
		.init_setup	= init_setup_aec6x80,
		.init_chipset	= init_chipset_aec62xx,
		.init_hwif	= init_hwif_aec62xx,
		.init_dma	= init_dma_aec62xx,
		.channels	= 2,
		.autodma	= AUTODMA,
		.bootable	= OFF_BOARD,
	},{	/* 4 */
		.name		= "AEC6X80R",
		.init_setup	= init_setup_aec6x80,
		.init_chipset	= init_chipset_aec62xx,
		.init_hwif	= init_hwif_aec62xx,
		.init_dma	= init_dma_aec62xx,
		.channels	= 2,
		.autodma	= AUTODMA,
		.enablebits	= {{0x4a,0x02,0x02}, {0x4a,0x04,0x04}},
		.bootable	= OFF_BOARD,
	}
};

/**
 *	aec62xx_init_one	-	called when a AEC is found
 *	@dev: the aec62xx device
 *	@id: the matching pci id
 *
 *	Called when the PCI registration layer (or the IDE initialization)
 *	finds a device matching our IDE device tables.
 */
 
static int __devinit aec62xx_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	ide_pci_device_t *d = &aec62xx_chipsets[id->driver_data];

	return d->init_setup(dev, d);
}

static struct pci_device_id aec62xx_pci_tbl[] = {
	{ PCI_VENDOR_ID_ARTOP, PCI_DEVICE_ID_ARTOP_ATP850UF, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ PCI_VENDOR_ID_ARTOP, PCI_DEVICE_ID_ARTOP_ATP860,   PCI_ANY_ID, PCI_ANY_ID, 0, 0, 1 },
	{ PCI_VENDOR_ID_ARTOP, PCI_DEVICE_ID_ARTOP_ATP860R,  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 2 },
	{ PCI_VENDOR_ID_ARTOP, PCI_DEVICE_ID_ARTOP_ATP865,   PCI_ANY_ID, PCI_ANY_ID, 0, 0, 3 },
	{ PCI_VENDOR_ID_ARTOP, PCI_DEVICE_ID_ARTOP_ATP865R,  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 4 },
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, aec62xx_pci_tbl);

static struct pci_driver driver = {
	.name		= "AEC62xx_IDE",
	.id_table	= aec62xx_pci_tbl,
	.probe		= aec62xx_init_one,
};

static int aec62xx_ide_init(void)
{
	return ide_pci_register_driver(&driver);
}

module_init(aec62xx_ide_init);

MODULE_AUTHOR("Andre Hedrick");
MODULE_DESCRIPTION("PCI driver module for ARTOP AEC62xx IDE");
MODULE_LICENSE("GPL");
