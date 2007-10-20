/*
 *  linux/drivers/ide/legacy/dtc2278.c		Version 0.02	Feb 10, 1996
 *
 *  Copyright (C) 1996  Linus Torvalds & author (see below)
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/ide.h>
#include <linux/init.h>

#include <asm/io.h>

/*
 * Changing this #undef to #define may solve start up problems in some systems.
 */
#undef ALWAYS_SET_DTC2278_PIO_MODE

/*
 * From: andy@cercle.cts.com (Dyan Wile)
 *
 * Below is a patch for DTC-2278 - alike software-programmable controllers
 * The code enables the secondary IDE controller and the PIO4 (3?) timings on
 * the primary (EIDE). You may probably have to enable the 32-bit support to
 * get the full speed. You better get the disk interrupts disabled ( hdparm -u0
 * /dev/hd.. ) for the drives connected to the EIDE interface. (I get my
 * filesystem  corrupted with -u1, but under heavy disk load only :-)
 *
 * This card is now forced to use the "serialize" feature,
 * and irq-unmasking is disallowed.  If io_32bit is enabled,
 * it must be done for BOTH drives on each interface.
 *
 * This code was written for the DTC2278E, but might work with any of these:
 *
 * DTC2278S has only a single IDE interface.
 * DTC2278D has two IDE interfaces and is otherwise identical to the S version.
 * DTC2278E also has serial ports and a printer port
 * DTC2278EB: has onboard BIOS, and "works like a charm" -- Kent Bradford <kent@theory.caltech.edu>
 *
 * There may be a fourth controller type. The S and D versions use the
 * Winbond chip, and I think the E version does also.
 *
 */

static void sub22 (char b, char c)
{
	int i;

	for(i = 0; i < 3; ++i) {
		inb(0x3f6);
		outb_p(b,0xb0);
		inb(0x3f6);
		outb_p(c,0xb4);
		inb(0x3f6);
		if(inb(0xb4) == c) {
			outb_p(7,0xb0);
			inb(0x3f6);
			return;	/* success */
		}
	}
}

static DEFINE_SPINLOCK(dtc2278_lock);

static void dtc2278_set_pio_mode(ide_drive_t *drive, const u8 pio)
{
	unsigned long flags;

	if (pio >= 3) {
		spin_lock_irqsave(&dtc2278_lock, flags);
		/*
		 * This enables PIO mode4 (3?) on the first interface
		 */
		sub22(1,0xc3);
		sub22(0,0xa0);
		spin_unlock_irqrestore(&dtc2278_lock, flags);
	} else {
		/* we don't know how to set it back again.. */
		/* Actually we do - there is a data sheet available for the
		   Winbond but does anyone actually care */
	}

	/*
	 * 32bit I/O has to be enabled for *both* drives at the same time.
	 */
	drive->io_32bit = 1;
	HWIF(drive)->drives[!drive->select.b.unit].io_32bit = 1;
}

static int __init dtc2278_probe(void)
{
	unsigned long flags;
	ide_hwif_t *hwif, *mate;
	static u8 idx[4] = { 0, 1, 0xff, 0xff };

	hwif = &ide_hwifs[0];
	mate = &ide_hwifs[1];

	if (hwif->chipset != ide_unknown || mate->chipset != ide_unknown)
		return 1;

	local_irq_save(flags);
	/*
	 * This enables the second interface
	 */
	outb_p(4,0xb0);
	inb(0x3f6);
	outb_p(0x20,0xb4);
	inb(0x3f6);
#ifdef ALWAYS_SET_DTC2278_PIO_MODE
	/*
	 * This enables PIO mode4 (3?) on the first interface
	 * and may solve start-up problems for some people.
	 */
	sub22(1,0xc3);
	sub22(0,0xa0);
#endif
	local_irq_restore(flags);

	hwif->serialized = 1;
	hwif->chipset = ide_dtc2278;
	hwif->pio_mask = ATA_PIO4;
	hwif->set_pio_mode = &dtc2278_set_pio_mode;
	hwif->drives[0].no_unmask = 1;
	hwif->drives[1].no_unmask = 1;
	hwif->mate = mate;

	mate->serialized = 1;
	mate->chipset = ide_dtc2278;
	mate->pio_mask = ATA_PIO4;
	mate->drives[0].no_unmask = 1;
	mate->drives[1].no_unmask = 1;
	mate->mate = hwif;
	mate->channel = 1;

	ide_device_add(idx);

	return 0;
}

int probe_dtc2278 = 0;

module_param_named(probe, probe_dtc2278, bool, 0);
MODULE_PARM_DESC(probe, "probe for DTC2278xx chipsets");

/* Can be called directly from ide.c. */
int __init dtc2278_init(void)
{
	if (probe_dtc2278 == 0)
		return -ENODEV;

	if (dtc2278_probe()) {
		printk(KERN_ERR "dtc2278: ide interfaces already in use!\n");
		return -EBUSY;
	}
	return 0;
}

#ifdef MODULE
module_init(dtc2278_init);
#endif

MODULE_AUTHOR("See Local File");
MODULE_DESCRIPTION("support of DTC-2278 VLB IDE chipsets");
MODULE_LICENSE("GPL");
