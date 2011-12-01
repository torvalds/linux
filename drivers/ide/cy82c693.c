/*
 *  Copyright (C) 1998-2000 Andreas S. Krebs (akrebs@altavista.net), Maintainer
 *  Copyright (C) 1998-2002 Andre Hedrick <andre@linux-ide.org>, Integrator
 *  Copyright (C) 2007-2010 Bartlomiej Zolnierkiewicz
 *
 * CYPRESS CY82C693 chipset IDE controller
 *
 * The CY82C693 chipset is used on Digital's PC-Alpha 164SX boards.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/ide.h>
#include <linux/init.h>

#include <asm/io.h>

#define DRV_NAME "cy82c693"

/*
 *	NOTE: the value for busmaster timeout is tricky and I got it by
 *	trial and error!  By using a to low value will cause DMA timeouts
 *	and drop IDE performance, and by using a to high value will cause
 *	audio playback to scatter.
 *	If you know a better value or how to calc it, please let me know.
 */

/* twice the value written in cy82c693ub datasheet */
#define BUSMASTER_TIMEOUT	0x50
/*
 * the value above was tested on my machine and it seems to work okay
 */

/* here are the offset definitions for the registers */
#define CY82_IDE_CMDREG		0x04
#define CY82_IDE_ADDRSETUP	0x48
#define CY82_IDE_MASTER_IOR	0x4C
#define CY82_IDE_MASTER_IOW	0x4D
#define CY82_IDE_SLAVE_IOR	0x4E
#define CY82_IDE_SLAVE_IOW	0x4F
#define CY82_IDE_MASTER_8BIT	0x50
#define CY82_IDE_SLAVE_8BIT	0x51

#define CY82_INDEX_PORT		0x22
#define CY82_DATA_PORT		0x23

#define CY82_INDEX_CHANNEL0	0x30
#define CY82_INDEX_CHANNEL1	0x31
#define CY82_INDEX_TIMEOUT	0x32

/*
 * set DMA mode a specific channel for CY82C693
 */

static void cy82c693_set_dma_mode(ide_hwif_t *hwif, ide_drive_t *drive)
{
	const u8 mode = drive->dma_mode;
	u8 single = (mode & 0x10) >> 4, index = 0, data = 0;

	index = hwif->channel ? CY82_INDEX_CHANNEL1 : CY82_INDEX_CHANNEL0;

	data = (mode & 3) | (single << 2);

	outb(index, CY82_INDEX_PORT);
	outb(data, CY82_DATA_PORT);

	/*
	 * note: below we set the value for Bus Master IDE TimeOut Register
	 * I'm not absolutely sure what this does, but it solved my problem
	 * with IDE DMA and sound, so I now can play sound and work with
	 * my IDE driver at the same time :-)
	 *
	 * If you know the correct (best) value for this register please
	 * let me know - ASK
	 */

	data = BUSMASTER_TIMEOUT;
	outb(CY82_INDEX_TIMEOUT, CY82_INDEX_PORT);
	outb(data, CY82_DATA_PORT);
}

static void cy82c693_set_pio_mode(ide_hwif_t *hwif, ide_drive_t *drive)
{
	struct pci_dev *dev = to_pci_dev(hwif->dev);
	int bus_speed = ide_pci_clk ? ide_pci_clk : 33;
	const unsigned long T = 1000000 / bus_speed;
	unsigned int addrCtrl;
	struct ide_timing t;
	u8 time_16, time_8;

	/* select primary or secondary channel */
	if (hwif->index > 0) {  /* drive is on the secondary channel */
		dev = pci_get_slot(dev->bus, dev->devfn+1);
		if (!dev) {
			printk(KERN_ERR "%s: tune_drive: "
				"Cannot find secondary interface!\n",
				drive->name);
			return;
		}
	}

	ide_timing_compute(drive, drive->pio_mode, &t, T, 1);

	time_16 = clamp_val(t.recover - 1, 0, 15) |
		  (clamp_val(t.active - 1, 0, 15) << 4);
	time_8 = clamp_val(t.act8b - 1, 0, 15) |
		 (clamp_val(t.rec8b - 1, 0, 15) << 4);

	/* now let's write  the clocks registers */
	if ((drive->dn & 1) == 0) {
		/*
		 * set master drive
		 * address setup control register
		 * is 32 bit !!!
		 */
		pci_read_config_dword(dev, CY82_IDE_ADDRSETUP, &addrCtrl);

		addrCtrl &= (~0xF);
		addrCtrl |= clamp_val(t.setup - 1, 0, 15);
		pci_write_config_dword(dev, CY82_IDE_ADDRSETUP, addrCtrl);

		/* now let's set the remaining registers */
		pci_write_config_byte(dev, CY82_IDE_MASTER_IOR, time_16);
		pci_write_config_byte(dev, CY82_IDE_MASTER_IOW, time_16);
		pci_write_config_byte(dev, CY82_IDE_MASTER_8BIT, time_8);
	} else {
		/*
		 * set slave drive
		 * address setup control register
		 * is 32 bit !!!
		 */
		pci_read_config_dword(dev, CY82_IDE_ADDRSETUP, &addrCtrl);

		addrCtrl &= (~0xF0);
		addrCtrl |= (clamp_val(t.setup - 1, 0, 15) << 4);
		pci_write_config_dword(dev, CY82_IDE_ADDRSETUP, addrCtrl);

		/* now let's set the remaining registers */
		pci_write_config_byte(dev, CY82_IDE_SLAVE_IOR, time_16);
		pci_write_config_byte(dev, CY82_IDE_SLAVE_IOW, time_16);
		pci_write_config_byte(dev, CY82_IDE_SLAVE_8BIT, time_8);
	}
	if (hwif->index > 0)
		pci_dev_put(dev);
}

static void __devinit init_iops_cy82c693(ide_hwif_t *hwif)
{
	static ide_hwif_t *primary;
	struct pci_dev *dev = to_pci_dev(hwif->dev);

	if (PCI_FUNC(dev->devfn) == 1)
		primary = hwif;
	else {
		hwif->mate = primary;
		hwif->channel = 1;
	}
}

static const struct ide_port_ops cy82c693_port_ops = {
	.set_pio_mode		= cy82c693_set_pio_mode,
	.set_dma_mode		= cy82c693_set_dma_mode,
};

static const struct ide_port_info cy82c693_chipset __devinitdata = {
	.name		= DRV_NAME,
	.init_iops	= init_iops_cy82c693,
	.port_ops	= &cy82c693_port_ops,
	.host_flags	= IDE_HFLAG_SINGLE,
	.pio_mask	= ATA_PIO4,
	.swdma_mask	= ATA_SWDMA2,
	.mwdma_mask	= ATA_MWDMA2,
};

static int __devinit cy82c693_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	struct pci_dev *dev2;
	int ret = -ENODEV;

	/* CY82C693 is more than only a IDE controller.
	   Function 1 is primary IDE channel, function 2 - secondary. */
	if ((dev->class >> 8) == PCI_CLASS_STORAGE_IDE &&
	    PCI_FUNC(dev->devfn) == 1) {
		dev2 = pci_get_slot(dev->bus, dev->devfn + 1);
		ret = ide_pci_init_two(dev, dev2, &cy82c693_chipset, NULL);
		if (ret)
			pci_dev_put(dev2);
	}
	return ret;
}

static void __devexit cy82c693_remove(struct pci_dev *dev)
{
	struct ide_host *host = pci_get_drvdata(dev);
	struct pci_dev *dev2 = host->dev[1] ? to_pci_dev(host->dev[1]) : NULL;

	ide_pci_remove(dev);
	pci_dev_put(dev2);
}

static const struct pci_device_id cy82c693_pci_tbl[] = {
	{ PCI_VDEVICE(CONTAQ, PCI_DEVICE_ID_CONTAQ_82C693), 0 },
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, cy82c693_pci_tbl);

static struct pci_driver cy82c693_pci_driver = {
	.name		= "Cypress_IDE",
	.id_table	= cy82c693_pci_tbl,
	.probe		= cy82c693_init_one,
	.remove		= __devexit_p(cy82c693_remove),
	.suspend	= ide_pci_suspend,
	.resume		= ide_pci_resume,
};

static int __init cy82c693_ide_init(void)
{
	return ide_pci_register_driver(&cy82c693_pci_driver);
}

static void __exit cy82c693_ide_exit(void)
{
	pci_unregister_driver(&cy82c693_pci_driver);
}

module_init(cy82c693_ide_init);
module_exit(cy82c693_ide_exit);

MODULE_AUTHOR("Andreas Krebs, Andre Hedrick, Bartlomiej Zolnierkiewicz");
MODULE_DESCRIPTION("PCI driver module for the Cypress CY82C693 IDE");
MODULE_LICENSE("GPL");
