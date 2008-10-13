/*
 *  Copyright (C) 1996-1998  Linus Torvalds & authors (see below)
 */

/*
 * Authors:
 * Jaromir Koutek <miri@punknet.cz>,
 * Jan Harkes <jaharkes@cwi.nl>,
 * Mark Lord <mlord@pobox.com>
 * Some parts of code are from ali14xx.c and from rz1000.c.
 *
 * OPTi is trademark of OPTi, Octek is trademark of Octek.
 *
 * I used docs from OPTi databook, from ftp.opti.com, file 9123-0002.ps
 * and disassembled/traced setupvic.exe (DOS program).
 * It increases kernel code about 2 kB.
 * I don't have this card no more, but I hope I can get some in case
 * of needed development.
 * My card is Octek PIDE 1.01 (on card) or OPTiViC (program).
 * It has a place for a secondary connector in circuit, but nothing
 * is there. Also BIOS says no address for
 * secondary controller (see bellow in ide_init_opti621).
 * I've only tested this on my system, which only has one disk.
 * It's Western Digital WDAC2850, with PIO mode 3. The PCI bus
 * is at 20 MHz (I have DX2/80, I tried PCI at 40, but I got random
 * lockups). I tried the OCTEK double speed CD-ROM and
 * it does not work! But I can't boot DOS also, so it's probably
 * hardware fault. I have connected Conner 80MB, the Seagate 850MB (no
 * problems) and Seagate 1GB (as slave, WD as master). My experiences
 * with the third, 1GB drive: I got 3MB/s (hdparm), but sometimes
 * it slows to about 100kB/s! I don't know why and I have
 * not this drive now, so I can't try it again.
 * I write this driver because I lost the paper ("manual") with
 * settings of jumpers on the card and I have to boot Linux with
 * Loadlin except LILO, cause I have to run the setupvic.exe program
 * already or I get disk errors (my test: rpm -Vf
 * /usr/X11R6/bin/XF86_SVGA - or any big file).
 * Some numbers from hdparm -t /dev/hda:
 * Timing buffer-cache reads:   32 MB in  3.02 seconds =10.60 MB/sec
 * Timing buffered disk reads:  16 MB in  5.52 seconds = 2.90 MB/sec
 * I have 4 Megs/s before, but I don't know why (maybe changes
 * in hdparm test).
 * After release of 0.1, I got some successful reports, so it might work.
 *
 * The main problem with OPTi is that some timings for master
 * and slave must be the same. For example, if you have master
 * PIO 3 and slave PIO 0, driver have to set some timings of
 * master for PIO 0. Second problem is that opti621_set_pio_mode
 * got only one drive to set, but have to set both drives.
 * This is solved in compute_pios. If you don't set
 * the second drive, compute_pios use ide_get_best_pio_mode
 * for autoselect mode (you can change it to PIO 0, if you want).
 * If you then set the second drive to another PIO, the old value
 * (automatically selected) will be overrided by yours.
 * There is a 25/33MHz switch in configuration
 * register, but driver is written for use at any frequency.
 *
 * Version 0.1, Nov 8, 1996
 * by Jaromir Koutek, for 2.1.8.
 * Initial version of driver.
 *
 * Version 0.2
 * Number 0.2 skipped.
 *
 * Version 0.3, Nov 29, 1997
 * by Mark Lord (probably), for 2.1.68
 * Updates for use with new IDE block driver.
 *
 * Version 0.4, Dec 14, 1997
 * by Jan Harkes
 * Fixed some errors and cleaned the code.
 *
 * Version 0.5, Jan 2, 1998
 * by Jaromir Koutek
 * Updates for use with (again) new IDE block driver.
 * Update of documentation.
 *
 * Version 0.6, Jan 2, 1999
 * by Jaromir Koutek
 * Reversed to version 0.3 of the driver, because
 * 0.5 doesn't work.
 */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/ide.h>

#include <asm/io.h>

#define DRV_NAME "opti621"

#define READ_REG 0	/* index of Read cycle timing register */
#define WRITE_REG 1	/* index of Write cycle timing register */
#define CNTRL_REG 3	/* index of Control register */
#define STRAP_REG 5	/* index of Strap register */
#define MISC_REG 6	/* index of Miscellaneous register */

static int reg_base;

static DEFINE_SPINLOCK(opti621_lock);

/* Write value to register reg, base of register
 * is at reg_base (0x1f0 primary, 0x170 secondary,
 * if not changed by PCI configuration).
 * This is from setupvic.exe program.
 */
static void write_reg(u8 value, int reg)
{
	inw(reg_base + 1);
	inw(reg_base + 1);
	outb(3, reg_base + 2);
	outb(value, reg_base + reg);
	outb(0x83, reg_base + 2);
}

/* Read value from register reg, base of register
 * is at reg_base (0x1f0 primary, 0x170 secondary,
 * if not changed by PCI configuration).
 * This is from setupvic.exe program.
 */
static u8 read_reg(int reg)
{
	u8 ret = 0;

	inw(reg_base + 1);
	inw(reg_base + 1);
	outb(3, reg_base + 2);
	ret = inb(reg_base + reg);
	outb(0x83, reg_base + 2);

	return ret;
}

static void opti621_set_pio_mode(ide_drive_t *drive, const u8 pio)
{
	ide_hwif_t *hwif = drive->hwif;
	ide_drive_t *pair = ide_get_pair_dev(drive);
	unsigned long flags;
	u8 tim, misc, addr_pio = pio, clk;

	/* DRDY is default 2 (by OPTi Databook) */
	static const u8 addr_timings[2][5] = {
		{ 0x20, 0x10, 0x00, 0x00, 0x00 },	/* 33 MHz */
		{ 0x10, 0x10, 0x00, 0x00, 0x00 },	/* 25 MHz */
	};
	static const u8 data_rec_timings[2][5] = {
		{ 0x5b, 0x45, 0x32, 0x21, 0x20 },	/* 33 MHz */
		{ 0x48, 0x34, 0x21, 0x10, 0x10 }	/* 25 MHz */
	};

	drive->drive_data = XFER_PIO_0 + pio;

	if (pair) {
		if (pair->drive_data && pair->drive_data < drive->drive_data)
			addr_pio = pair->drive_data - XFER_PIO_0;
	}

	spin_lock_irqsave(&opti621_lock, flags);

	reg_base = hwif->io_ports.data_addr;

	/* allow Register-B */
	outb(0xc0, reg_base + CNTRL_REG);
	/* hmm, setupvic.exe does this ;-) */
	outb(0xff, reg_base + 5);
	/* if reads 0xff, adapter not exist? */
	(void)inb(reg_base + CNTRL_REG);
	/* if reads 0xc0, no interface exist? */
	read_reg(CNTRL_REG);

	/* check CLK speed */
	clk = read_reg(STRAP_REG) & 1;

	printk(KERN_INFO "%s: CLK = %d MHz\n", hwif->name, clk ? 25 : 33);

	tim  = data_rec_timings[clk][pio];
	misc = addr_timings[clk][addr_pio];

	/* select Index-0/1 for Register-A/B */
	write_reg(drive->dn & 1, MISC_REG);
	/* set read cycle timings */
	write_reg(tim, READ_REG);
	/* set write cycle timings */
	write_reg(tim, WRITE_REG);

	/* use Register-A for drive 0 */
	/* use Register-B for drive 1 */
	write_reg(0x85, CNTRL_REG);

	/* set address setup, DRDY timings,   */
	/*  and read prefetch for both drives */
	write_reg(misc, MISC_REG);

	spin_unlock_irqrestore(&opti621_lock, flags);
}

static const struct ide_port_ops opti621_port_ops = {
	.set_pio_mode		= opti621_set_pio_mode,
};

static const struct ide_port_info opti621_chipset __devinitdata = {
	.name		= DRV_NAME,
	.enablebits	= { {0x45, 0x80, 0x00}, {0x40, 0x08, 0x00} },
	.port_ops	= &opti621_port_ops,
	.host_flags	= IDE_HFLAG_NO_DMA,
	.pio_mask	= ATA_PIO4,
};

static int __devinit opti621_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	return ide_pci_init_one(dev, &opti621_chipset, NULL);
}

static const struct pci_device_id opti621_pci_tbl[] = {
	{ PCI_VDEVICE(OPTI, PCI_DEVICE_ID_OPTI_82C621), 0 },
	{ PCI_VDEVICE(OPTI, PCI_DEVICE_ID_OPTI_82C825), 0 },
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, opti621_pci_tbl);

static struct pci_driver opti621_pci_driver = {
	.name		= "Opti621_IDE",
	.id_table	= opti621_pci_tbl,
	.probe		= opti621_init_one,
	.remove		= ide_pci_remove,
	.suspend	= ide_pci_suspend,
	.resume		= ide_pci_resume,
};

static int __init opti621_ide_init(void)
{
	return ide_pci_register_driver(&opti621_pci_driver);
}

static void __exit opti621_ide_exit(void)
{
	pci_unregister_driver(&opti621_pci_driver);
}

module_init(opti621_ide_init);
module_exit(opti621_ide_exit);

MODULE_AUTHOR("Jaromir Koutek, Jan Harkes, Mark Lord");
MODULE_DESCRIPTION("PCI driver module for Opti621 IDE");
MODULE_LICENSE("GPL");
