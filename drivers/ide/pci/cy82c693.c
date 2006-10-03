/*
 * linux/drivers/ide/pci/cy82c693.c		Version 0.40	Sep. 10, 2002
 *
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
 * - use idebus=xx parameter to set PCI bus speed - needed to calc
 *   timings for PIO modes (default will be 40)
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
#include <linux/delay.h>
#include <linux/ide.h>
#include <linux/init.h>

#include <asm/io.h>

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

/* the max PIO mode - from datasheet */
#define CY82C693_MAX_PIO	4

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
static int calc_clk (int time, int bus_speed)
{
	int clocks;

	clocks = (time*bus_speed+999)/1000 -1;

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
static void compute_clocks (u8 pio, pio_clocks_t *p_pclk)
{
	int clk1, clk2;
	int bus_speed = system_bus_clock();	/* get speed of PCI bus */

	/* we don't check against CY82C693's min and max speed,
	 * so you can play with the idebus=xx parameter
	 */

	if (pio > CY82C693_MAX_PIO)
		pio = CY82C693_MAX_PIO;

	/* let's calc the address setup time clocks */
	p_pclk->address_time = (u8)calc_clk(ide_pio_timings[pio].setup_time, bus_speed);

	/* let's calc the active and recovery time clocks */
	clk1 = calc_clk(ide_pio_timings[pio].active_time, bus_speed);

	/* calc recovery timing */
	clk2 =	ide_pio_timings[pio].cycle_time -
		ide_pio_timings[pio].active_time -
		ide_pio_timings[pio].setup_time;

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

static void cy82c693_dma_enable (ide_drive_t *drive, int mode, int single)
{
	u8 index = 0, data = 0;

	if (mode>2)	/* make sure we set a valid mode */
		mode = 2;
			   
	if (mode > drive->id->tDMA)  /* to be absolutly sure we have a valid mode */
		mode = drive->id->tDMA;
	
	index = (HWIF(drive)->channel==0) ? CY82_INDEX_CHANNEL0 : CY82_INDEX_CHANNEL1;

#if CY82C693_DEBUG_LOGS
	/* for debug let's show the previous values */

	HWIF(drive)->OUTB(index, CY82_INDEX_PORT);
	data = HWIF(drive)->INB(CY82_DATA_PORT);

	printk (KERN_INFO "%s (ch=%d, dev=%d): DMA mode is %d (single=%d)\n",
		drive->name, HWIF(drive)->channel, drive->select.b.unit,
		(data&0x3), ((data>>2)&1));
#endif /* CY82C693_DEBUG_LOGS */

	data = (u8)mode|(u8)(single<<2);

	HWIF(drive)->OUTB(index, CY82_INDEX_PORT);
	HWIF(drive)->OUTB(data, CY82_DATA_PORT);

#if CY82C693_DEBUG_INFO
	printk(KERN_INFO "%s (ch=%d, dev=%d): set DMA mode to %d (single=%d)\n",
		drive->name, HWIF(drive)->channel, drive->select.b.unit,
		mode, single);
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
	HWIF(drive)->OUTB(CY82_INDEX_TIMEOUT, CY82_INDEX_PORT);
	HWIF(drive)->OUTB(data, CY82_DATA_PORT);

#if CY82C693_DEBUG_INFO	
	printk (KERN_INFO "%s: Set IDE Bus Master TimeOut Register to 0x%X\n",
		drive->name, data);
#endif /* CY82C693_DEBUG_INFO */
}

/* 
 * used to set DMA mode for CY82C693 (single and multi modes)
 */
static int cy82c693_ide_dma_on (ide_drive_t *drive)
{
	struct hd_driveid *id = drive->id;

#if CY82C693_DEBUG_INFO
	printk (KERN_INFO "dma_on: %s\n", drive->name);
#endif /* CY82C693_DEBUG_INFO */

	if (id != NULL) {		
		/* Enable DMA on any drive that has DMA
		 * (multi or single) enabled
		 */
		if (id->field_valid & 2) {	/* regular DMA */
			int mmode, smode;

			mmode = id->dma_mword & (id->dma_mword >> 8);
			smode = id->dma_1word & (id->dma_1word >> 8);
			       		      
			if (mmode != 0) {
				/* enable multi */
				cy82c693_dma_enable(drive, (mmode >> 1), 0);
			} else if (smode != 0) {
				/* enable single */
				cy82c693_dma_enable(drive, (smode >> 1), 1);
			}
		}
	}
        return __ide_dma_on(drive);
}

/*
 * tune ide drive - set PIO mode
 */
static void cy82c693_tune_drive (ide_drive_t *drive, u8 pio)
{
	ide_hwif_t *hwif = HWIF(drive);
	struct pci_dev *dev = hwif->pci_dev;
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

	/* first let's calc the pio modes */
	pio = ide_get_best_pio_mode(drive, pio, CY82C693_MAX_PIO, NULL);

#if CY82C693_DEBUG_INFO
	printk (KERN_INFO "%s: Selected PIO mode %d\n", drive->name, pio);
#endif /* CY82C693_DEBUG_INFO */

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
static unsigned int __devinit init_chipset_cy82c693(struct pci_dev *dev, const char *name)
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
	printk(KERN_INFO "%s: Peripheral Configuration Register: 0x%X\n",
		name, data);
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
	printk (KERN_INFO "%s: New Peripheral Configuration Register: 0x%X\n",
		name, data);
#endif /* CY82C693_DEBUG_INFO */

#endif /* CY82C693_SETDMA_CLOCK */
	return 0;
}

/*
 * the init function - called for each ide channel once
 */
static void __devinit init_hwif_cy82c693(ide_hwif_t *hwif)
{
	hwif->autodma = 0;

	hwif->chipset = ide_cy82c693;
	hwif->tuneproc = &cy82c693_tune_drive;

	if (!hwif->dma_base) {
		hwif->drives[0].autotune = 1;
		hwif->drives[1].autotune = 1;
		return;
	}

	hwif->atapi_dma = 1;
	hwif->mwdma_mask = 0x04;
	hwif->swdma_mask = 0x04;

	hwif->ide_dma_on = &cy82c693_ide_dma_on;
	if (!noautodma)
		hwif->autodma = 1;
	hwif->drives[0].autodma = hwif->autodma;
	hwif->drives[1].autodma = hwif->autodma;
}

static __devinitdata ide_hwif_t *primary;

static void __devinit init_iops_cy82c693(ide_hwif_t *hwif)
{
	if (PCI_FUNC(hwif->pci_dev->devfn) == 1)
		primary = hwif;
	else {
		hwif->mate = primary;
		hwif->channel = 1;
	}
}

static ide_pci_device_t cy82c693_chipsets[] __devinitdata = {
	{	/* 0 */
		.name		= "CY82C693",
		.init_chipset	= init_chipset_cy82c693,
		.init_iops	= init_iops_cy82c693,
		.init_hwif	= init_hwif_cy82c693,
		.channels	= 1,
		.autodma	= AUTODMA,
		.bootable	= ON_BOARD,
	}
};

static int __devinit cy82c693_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	ide_pci_device_t *d = &cy82c693_chipsets[id->driver_data];
	struct pci_dev *dev2;
	int ret = -ENODEV;

	/* CY82C693 is more than only a IDE controller.
	   Function 1 is primary IDE channel, function 2 - secondary. */
        if ((dev->class >> 8) == PCI_CLASS_STORAGE_IDE &&
	    PCI_FUNC(dev->devfn) == 1) {
		dev2 = pci_get_slot(dev->bus, dev->devfn + 1);
		ret = ide_setup_pci_devices(dev, dev2, d);
		/* We leak pci refs here but thats ok - we can't be unloaded */
	}
	return ret;
}

static struct pci_device_id cy82c693_pci_tbl[] = {
	{ PCI_VENDOR_ID_CONTAQ, PCI_DEVICE_ID_CONTAQ_82C693, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, cy82c693_pci_tbl);

static struct pci_driver driver = {
	.name		= "Cypress_IDE",
	.id_table	= cy82c693_pci_tbl,
	.probe		= cy82c693_init_one,
};

static int cy82c693_ide_init(void)
{
	return ide_pci_register_driver(&driver);
}

module_init(cy82c693_ide_init);

MODULE_AUTHOR("Andreas Krebs, Andre Hedrick");
MODULE_DESCRIPTION("PCI driver module for the Cypress CY82C693 IDE");
MODULE_LICENSE("GPL");
