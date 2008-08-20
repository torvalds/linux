/*
 *  Copyright (C) 1998-2000 Andreas S. Krebs (akrebs@altavista.net), Maintainer
 *  Copyright (C) 1998-2002 Andre Hedrick <andre@linux-ide.org>, Integrator
 *
 * CYPRESS CY82C693 chipset IDE controller
 *
 * The CY82C693 chipset is used on Digital's PC-Alpha 164SX boards.
 * Writing the driver was quite simple, since most of the job is
 * done by the generic pci-ide support.
 * The hard part was finding the CY82C693's datasheet on Cypress's
 * web page :-(. But Altavista solved this problem :-).
 *
 *
 * Notes:
 * - I recently got a 16.8G IBM DTTA, so I was able to test it with
 *   a large and fast disk - the results look great, so I'd say the
 *   driver is working fine :-)
 *   hdparm -t reports 8.17 MB/sec at about 6% CPU usage for the DTTA
 * - this is my first linux driver, so there's probably a lot  of room
 *   for optimizations and bug fixing, so feel free to do it.
 * - if using PIO mode it's a good idea to set the PIO mode and
 *   32-bit I/O support (if possible), e.g. hdparm -p2 -c1 /dev/hda
 * - I had some problems with my IBM DHEA with PIO modes < 2
 *   (lost interrupts) ?????
 * - first tests with DMA look okay, they seem to work, but there is a
 *   problem with sound - the BusMaster IDE TimeOut should fixed this
 *
 * Ancient History:
 * AMH@1999-08-24: v0.34 init_cy82c693_chip moved to pci_init_cy82c693
 * ASK@1999-01-23: v0.33 made a few minor code clean ups
 *                       removed DMA clock speed setting by default
 *                       added boot message
 * ASK@1998-11-01: v0.32 added support to set BusMaster IDE TimeOut
 *                       added support to set DMA Controller Clock Speed
 * ASK@1998-10-31: v0.31 fixed problem with setting to high DMA modes
 *                       on some drives.
 * ASK@1998-10-29: v0.3 added support to set DMA modes
 * ASK@1998-10-28: v0.2 added support to set PIO modes
 * ASK@1998-10-27: v0.1 first version - chipset detection
 *
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/ide.h>
#include <linux/init.h>

#include <asm/io.h>

#define DRV_NAME "cy82c693"

/* the current version */
#define CY82_VERSION	"CY82C693U driver v0.34 99-13-12 Andreas S. Krebs (akrebs@altavista.net)"

/*
 *	The following are used to debug the driver.
 */
#define CY82C693_DEBUG_LOGS	0
#define CY82C693_DEBUG_INFO	0

/* define CY82C693_SETDMA_CLOCK to set DMA Controller Clock Speed to ATCLK */
#undef CY82C693_SETDMA_CLOCK

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

#define CY82_INDEX_CTRLREG1	0x01
#define CY82_INDEX_CHANNEL0	0x30
#define CY82_INDEX_CHANNEL1	0x31
#define CY82_INDEX_TIMEOUT	0x32

/* the min and max PCI bus speed in MHz - from datasheet */
#define CY82C963_MIN_BUS_SPEED	25
#define CY82C963_MAX_BUS_SPEED	33

/* the struct for the PIO mode timings */
typedef struct pio_clocks_s {
	u8	address_time;	/* Address setup (clocks) */
	u8	time_16r;	/* clocks for 16bit IOR (0xF0=Active/data, 0x0F=Recovery) */
	u8	time_16w;	/* clocks for 16bit IOW (0xF0=Active/data, 0x0F=Recovery) */
	u8	time_8;		/* clocks for 8bit (0xF0=Active/data, 0x0F=Recovery) */
} pio_clocks_t;

/*
 * calc clocks using bus_speed
 * returns (rounded up) time in bus clocks for time in ns
 */
static int calc_clk(int time, int bus_speed)
{
	int clocks;

	clocks = (time*bus_speed+999)/1000 - 1;

	if (clocks < 0)
		clocks = 0;

	if (clocks > 0x0F)
		clocks = 0x0F;

	return clocks;
}

/*
 * compute the values for the clock registers for PIO
 * mode and pci_clk [MHz] speed
 *
 * NOTE: for mode 0,1 and 2 drives 8-bit IDE command control registers are used
 *       for mode 3 and 4 drives 8 and 16-bit timings are the same
 *
 */
static void compute_clocks(u8 pio, pio_clocks_t *p_pclk)
{
	struct ide_timing *t = ide_timing_find_mode(XFER_PIO_0 + pio);
	int clk1, clk2;
	int bus_speed = ide_pci_clk ? ide_pci_clk : 33;

	/* we don't check against CY82C693's min and max speed,
	 * so you can play with the idebus=xx parameter
	 */

	/* let's calc the address setup time clocks */
	p_pclk->address_time = (u8)calc_clk(t->setup, bus_speed);

	/* let's calc the active and recovery time clocks */
	clk1 = calc_clk(t->active, bus_speed);

	/* calc recovery timing */
	clk2 = t->cycle - t->active - t->setup;

	clk2 = calc_clk(clk2, bus_speed);

	clk1 = (clk1<<4)|clk2;	/* combine active and recovery clocks */

	/* note: we use the same values for 16bit IOR and IOW
	 *	those are all the same, since I don't have other
	 *	timings than those from ide-lib.c
	 */

	p_pclk->time_16r = (u8)clk1;
	p_pclk->time_16w = (u8)clk1;

	/* what are good values for 8bit ?? */
	p_pclk->time_8 = (u8)clk1;
}

/*
 * set DMA mode a specific channel for CY82C693
 */

static void cy82c693_set_dma_mode(ide_drive_t *drive, const u8 mode)
{
	ide_hwif_t *hwif = drive->hwif;
	u8 single = (mode & 0x10) >> 4, index = 0, data = 0;

	index = hwif->channel ? CY82_INDEX_CHANNEL1 : CY82_INDEX_CHANNEL0;

#if CY82C693_DEBUG_LOGS
	/* for debug let's show the previous values */

	outb(index, CY82_INDEX_PORT);
	data = inb(CY82_DATA_PORT);

	printk(KERN_INFO "%s (ch=%d, dev=%d): DMA mode is %d (single=%d)\n",
		drive->name, HWIF(drive)->channel, drive->select.b.unit,
		(data&0x3), ((data>>2)&1));
#endif /* CY82C693_DEBUG_LOGS */

	data = (mode & 3) | (single << 2);

	outb(index, CY82_INDEX_PORT);
	outb(data, CY82_DATA_PORT);

#if CY82C693_DEBUG_INFO
	printk(KERN_INFO "%s (ch=%d, dev=%d): set DMA mode to %d (single=%d)\n",
		drive->name, HWIF(drive)->channel, drive->select.b.unit,
		mode & 3, single);
#endif /* CY82C693_DEBUG_INFO */

	/*
	 * note: below we set the value for Bus Master IDE TimeOut Register
	 * I'm not absolutly sure what this does, but it solved my problem
	 * with IDE DMA and sound, so I now can play sound and work with
	 * my IDE driver at the same time :-)
	 *
	 * If you know the correct (best) value for this register please
	 * let me know - ASK
	 */

	data = BUSMASTER_TIMEOUT;
	outb(CY82_INDEX_TIMEOUT, CY82_INDEX_PORT);
	outb(data, CY82_DATA_PORT);

#if CY82C693_DEBUG_INFO
	printk(KERN_INFO "%s: Set IDE Bus Master TimeOut Register to 0x%X\n",
		drive->name, data);
#endif /* CY82C693_DEBUG_INFO */
}

static void cy82c693_set_pio_mode(ide_drive_t *drive, const u8 pio)
{
	ide_hwif_t *hwif = HWIF(drive);
	struct pci_dev *dev = to_pci_dev(hwif->dev);
	pio_clocks_t pclk;
	unsigned int addrCtrl;

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

#if CY82C693_DEBUG_LOGS
	/* for debug let's show the register values */

	if (drive->select.b.unit == 0) {
		/*
		 * get master drive registers
		 * address setup control register
		 * is 32 bit !!!
		 */
		pci_read_config_dword(dev, CY82_IDE_ADDRSETUP, &addrCtrl);
		addrCtrl &= 0x0F;

		/* now let's get the remaining registers */
		pci_read_config_byte(dev, CY82_IDE_MASTER_IOR, &pclk.time_16r);
		pci_read_config_byte(dev, CY82_IDE_MASTER_IOW, &pclk.time_16w);
		pci_read_config_byte(dev, CY82_IDE_MASTER_8BIT, &pclk.time_8);
	} else {
		/*
		 * set slave drive registers
		 * address setup control register
		 * is 32 bit !!!
		 */
		pci_read_config_dword(dev, CY82_IDE_ADDRSETUP, &addrCtrl);

		addrCtrl &= 0xF0;
		addrCtrl >>= 4;

		/* now let's get the remaining registers */
		pci_read_config_byte(dev, CY82_IDE_SLAVE_IOR, &pclk.time_16r);
		pci_read_config_byte(dev, CY82_IDE_SLAVE_IOW, &pclk.time_16w);
		pci_read_config_byte(dev, CY82_IDE_SLAVE_8BIT, &pclk.time_8);
	}

	printk(KERN_INFO "%s (ch=%d, dev=%d): PIO timing is "
		"(addr=0x%X, ior=0x%X, iow=0x%X, 8bit=0x%X)\n",
		drive->name, hwif->channel, drive->select.b.unit,
		addrCtrl, pclk.time_16r, pclk.time_16w, pclk.time_8);
#endif /* CY82C693_DEBUG_LOGS */

	/* let's calc the values for this PIO mode */
	compute_clocks(pio, &pclk);

	/* now let's write  the clocks registers */
	if (drive->select.b.unit == 0) {
		/*
		 * set master drive
		 * address setup control register
		 * is 32 bit !!!
		 */
		pci_read_config_dword(dev, CY82_IDE_ADDRSETUP, &addrCtrl);

		addrCtrl &= (~0xF);
		addrCtrl |= (unsigned int)pclk.address_time;
		pci_write_config_dword(dev, CY82_IDE_ADDRSETUP, addrCtrl);

		/* now let's set the remaining registers */
		pci_write_config_byte(dev, CY82_IDE_MASTER_IOR, pclk.time_16r);
		pci_write_config_byte(dev, CY82_IDE_MASTER_IOW, pclk.time_16w);
		pci_write_config_byte(dev, CY82_IDE_MASTER_8BIT, pclk.time_8);

		addrCtrl &= 0xF;
	} else {
		/*
		 * set slave drive
		 * address setup control register
		 * is 32 bit !!!
		 */
		pci_read_config_dword(dev, CY82_IDE_ADDRSETUP, &addrCtrl);

		addrCtrl &= (~0xF0);
		addrCtrl |= ((unsigned int)pclk.address_time<<4);
		pci_write_config_dword(dev, CY82_IDE_ADDRSETUP, addrCtrl);

		/* now let's set the remaining registers */
		pci_write_config_byte(dev, CY82_IDE_SLAVE_IOR, pclk.time_16r);
		pci_write_config_byte(dev, CY82_IDE_SLAVE_IOW, pclk.time_16w);
		pci_write_config_byte(dev, CY82_IDE_SLAVE_8BIT, pclk.time_8);

		addrCtrl >>= 4;
		addrCtrl &= 0xF;
	}

#if CY82C693_DEBUG_INFO
	printk(KERN_INFO "%s (ch=%d, dev=%d): set PIO timing to "
		"(addr=0x%X, ior=0x%X, iow=0x%X, 8bit=0x%X)\n",
		drive->name, hwif->channel, drive->select.b.unit,
		addrCtrl, pclk.time_16r, pclk.time_16w, pclk.time_8);
#endif /* CY82C693_DEBUG_INFO */
}

/*
 * this function is called during init and is used to setup the cy82c693 chip
 */
static unsigned int __devinit init_chipset_cy82c693(struct pci_dev *dev)
{
	if (PCI_FUNC(dev->devfn) != 1)
		return 0;

#ifdef CY82C693_SETDMA_CLOCK
	u8 data = 0;
#endif /* CY82C693_SETDMA_CLOCK */

	/* write info about this verion of the driver */
	printk(KERN_INFO CY82_VERSION "\n");

#ifdef CY82C693_SETDMA_CLOCK
       /* okay let's set the DMA clock speed */

	outb(CY82_INDEX_CTRLREG1, CY82_INDEX_PORT);
	data = inb(CY82_DATA_PORT);

#if CY82C693_DEBUG_INFO
	printk(KERN_INFO DRV_NAME ": Peripheral Configuration Register: 0x%X\n",
		data);
#endif /* CY82C693_DEBUG_INFO */

	/*
	 * for some reason sometimes the DMA controller
	 * speed is set to ATCLK/2 ???? - we fix this here
	 *
	 * note: i don't know what causes this strange behaviour,
	 *       but even changing the dma speed doesn't solve it :-(
	 *       the ide performance is still only half the normal speed
	 *
	 *       if anybody knows what goes wrong with my machine, please
	 *       let me know - ASK
	 */

	data |= 0x03;

	outb(CY82_INDEX_CTRLREG1, CY82_INDEX_PORT);
	outb(data, CY82_DATA_PORT);

#if CY82C693_DEBUG_INFO
	printk(KERN_INFO ": New Peripheral Configuration Register: 0x%X\n",
		data);
#endif /* CY82C693_DEBUG_INFO */

#endif /* CY82C693_SETDMA_CLOCK */
	return 0;
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
	.init_chipset	= init_chipset_cy82c693,
	.init_iops	= init_iops_cy82c693,
	.port_ops	= &cy82c693_port_ops,
	.chipset	= ide_cy82c693,
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

static struct pci_driver driver = {
	.name		= "Cypress_IDE",
	.id_table	= cy82c693_pci_tbl,
	.probe		= cy82c693_init_one,
	.remove		= __devexit_p(cy82c693_remove),
};

static int __init cy82c693_ide_init(void)
{
	return ide_pci_register_driver(&driver);
}

static void __exit cy82c693_ide_exit(void)
{
	pci_unregister_driver(&driver);
}

module_init(cy82c693_ide_init);
module_exit(cy82c693_ide_exit);

MODULE_AUTHOR("Andreas Krebs, Andre Hedrick");
MODULE_DESCRIPTION("PCI driver module for the Cypress CY82C693 IDE");
MODULE_LICENSE("GPL");
