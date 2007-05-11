/*
 *  linux/drivers/ide/ide-probe.c	Version 1.11	Mar 05, 2003
 *
 *  Copyright (C) 1994-1998  Linus Torvalds & authors (see below)
 */

/*
 *  Mostly written by Mark Lord <mlord@pobox.com>
 *                and Gadi Oxman <gadio@netvision.net.il>
 *                and Andre Hedrick <andre@linux-ide.org>
 *
 *  See linux/MAINTAINERS for address of current maintainer.
 *
 * This is the IDE probe module, as evolved from hd.c and ide.c.
 *
 * Version 1.00		move drive probing code from ide.c to ide-probe.c
 * Version 1.01		fix compilation problem for m68k
 * Version 1.02		increase WAIT_PIDENTIFY to avoid CD-ROM locking at boot
 *			 by Andrea Arcangeli
 * Version 1.03		fix for (hwif->chipset == ide_4drives)
 * Version 1.04		fixed buggy treatments of known flash memory cards
 *
 * Version 1.05		fix for (hwif->chipset == ide_pdc4030)
 *			added ide6/7/8/9
 *			allowed for secondary flash card to be detectable
 *			 with new flag : drive->ata_flash : 1;
 * Version 1.06		stream line request queue and prep for cascade project.
 * Version 1.07		max_sect <= 255; slower disks would get behind and
 * 			then fall over when they get to 256.	Paul G.
 * Version 1.10		Update set for new IDE. drive->id is now always
 *			valid after probe time even with noprobe
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/major.h>
#include <linux/errno.h>
#include <linux/genhd.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/ide.h>
#include <linux/spinlock.h>
#include <linux/kmod.h>
#include <linux/pci.h>

#include <asm/byteorder.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/io.h>

/**
 *	generic_id		-	add a generic drive id
 *	@drive:	drive to make an ID block for
 *	
 *	Add a fake id field to the drive we are passed. This allows
 *	use to skip a ton of NULL checks (which people always miss) 
 *	and make drive properties unconditional outside of this file
 */
 
static void generic_id(ide_drive_t *drive)
{
	drive->id->cyls = drive->cyl;
	drive->id->heads = drive->head;
	drive->id->sectors = drive->sect;
	drive->id->cur_cyls = drive->cyl;
	drive->id->cur_heads = drive->head;
	drive->id->cur_sectors = drive->sect;
}

static void ide_disk_init_chs(ide_drive_t *drive)
{
	struct hd_driveid *id = drive->id;

	/* Extract geometry if we did not already have one for the drive */
	if (!drive->cyl || !drive->head || !drive->sect) {
		drive->cyl  = drive->bios_cyl  = id->cyls;
		drive->head = drive->bios_head = id->heads;
		drive->sect = drive->bios_sect = id->sectors;
	}

	/* Handle logical geometry translation by the drive */
	if ((id->field_valid & 1) && id->cur_cyls &&
	    id->cur_heads && (id->cur_heads <= 16) && id->cur_sectors) {
		drive->cyl  = id->cur_cyls;
		drive->head = id->cur_heads;
		drive->sect = id->cur_sectors;
	}

	/* Use physical geometry if what we have still makes no sense */
	if (drive->head > 16 && id->heads && id->heads <= 16) {
		drive->cyl  = id->cyls;
		drive->head = id->heads;
		drive->sect = id->sectors;
	}
}

static void ide_disk_init_mult_count(ide_drive_t *drive)
{
	struct hd_driveid *id = drive->id;

	drive->mult_count = 0;
	if (id->max_multsect) {
#ifdef CONFIG_IDEDISK_MULTI_MODE
		id->multsect = ((id->max_multsect/2) > 1) ? id->max_multsect : 0;
		id->multsect_valid = id->multsect ? 1 : 0;
		drive->mult_req = id->multsect_valid ? id->max_multsect : INITIAL_MULT_COUNT;
		drive->special.b.set_multmode = drive->mult_req ? 1 : 0;
#else	/* original, pre IDE-NFG, per request of AC */
		drive->mult_req = INITIAL_MULT_COUNT;
		if (drive->mult_req > id->max_multsect)
			drive->mult_req = id->max_multsect;
		if (drive->mult_req || ((id->multsect_valid & 1) && id->multsect))
			drive->special.b.set_multmode = 1;
#endif
	}
}

/**
 *	do_identify	-	identify a drive
 *	@drive: drive to identify 
 *	@cmd: command used
 *
 *	Called when we have issued a drive identify command to
 *	read and parse the results. This function is run with
 *	interrupts disabled. 
 */
 
static inline void do_identify (ide_drive_t *drive, u8 cmd)
{
	ide_hwif_t *hwif = HWIF(drive);
	int bswap = 1;
	struct hd_driveid *id;

	id = drive->id;
	/* read 512 bytes of id info */
	hwif->ata_input_data(drive, id, SECTOR_WORDS);

	drive->id_read = 1;
	local_irq_enable();
	ide_fix_driveid(id);

#if defined (CONFIG_SCSI_EATA_DMA) || defined (CONFIG_SCSI_EATA_PIO) || defined (CONFIG_SCSI_EATA)
	/*
	 * EATA SCSI controllers do a hardware ATA emulation:
	 * Ignore them if there is a driver for them available.
	 */
	if ((id->model[0] == 'P' && id->model[1] == 'M') ||
	    (id->model[0] == 'S' && id->model[1] == 'K')) {
		printk("%s: EATA SCSI HBA %.10s\n", drive->name, id->model);
		goto err_misc;
	}
#endif /* CONFIG_SCSI_EATA_DMA || CONFIG_SCSI_EATA_PIO */

	/*
	 *  WIN_IDENTIFY returns little-endian info,
	 *  WIN_PIDENTIFY *usually* returns little-endian info.
	 */
	if (cmd == WIN_PIDENTIFY) {
		if ((id->model[0] == 'N' && id->model[1] == 'E') /* NEC */
		 || (id->model[0] == 'F' && id->model[1] == 'X') /* Mitsumi */
		 || (id->model[0] == 'P' && id->model[1] == 'i'))/* Pioneer */
			/* Vertos drives may still be weird */
			bswap ^= 1;	
	}
	ide_fixstring(id->model,     sizeof(id->model),     bswap);
	ide_fixstring(id->fw_rev,    sizeof(id->fw_rev),    bswap);
	ide_fixstring(id->serial_no, sizeof(id->serial_no), bswap);

	if (strstr(id->model, "E X A B Y T E N E S T"))
		goto err_misc;

	/* we depend on this a lot! */
	id->model[sizeof(id->model)-1] = '\0';
	printk("%s: %s, ", drive->name, id->model);
	drive->present = 1;
	drive->dead = 0;

	/*
	 * Check for an ATAPI device
	 */
	if (cmd == WIN_PIDENTIFY) {
		u8 type = (id->config >> 8) & 0x1f;
		printk("ATAPI ");
		switch (type) {
			case ide_floppy:
				if (!strstr(id->model, "CD-ROM")) {
					if (!strstr(id->model, "oppy") &&
					    !strstr(id->model, "poyp") &&
					    !strstr(id->model, "ZIP"))
						printk("cdrom or floppy?, assuming ");
					if (drive->media != ide_cdrom) {
						printk ("FLOPPY");
						drive->removable = 1;
						break;
					}
				}
				/* Early cdrom models used zero */
				type = ide_cdrom;
			case ide_cdrom:
				drive->removable = 1;
#ifdef CONFIG_PPC
				/* kludge for Apple PowerBook internal zip */
				if (!strstr(id->model, "CD-ROM") &&
				    strstr(id->model, "ZIP")) {
					printk ("FLOPPY");
					type = ide_floppy;
					break;
				}
#endif
				printk ("CD/DVD-ROM");
				break;
			case ide_tape:
				printk ("TAPE");
				break;
			case ide_optical:
				printk ("OPTICAL");
				drive->removable = 1;
				break;
			default:
				printk("UNKNOWN (type %d)", type);
				break;
		}
		printk (" drive\n");
		drive->media = type;
		/* an ATAPI device ignores DRDY */
		drive->ready_stat = 0;
		return;
	}

	/*
	 * Not an ATAPI device: looks like a "regular" hard disk
	 */

	/*
	 * 0x848a = CompactFlash device
	 * These are *not* removable in Linux definition of the term
	 */

	if ((id->config != 0x848a) && (id->config & (1<<7)))
		drive->removable = 1;

	drive->media = ide_disk;
	printk("%s DISK drive\n", (id->config == 0x848a) ? "CFA" : "ATA" );
	QUIRK_LIST(drive);
	return;

err_misc:
	kfree(id);
	drive->present = 0;
	return;
}

/**
 *	actual_try_to_identify	-	send ata/atapi identify
 *	@drive: drive to identify
 *	@cmd: command to use
 *
 *	try_to_identify() sends an ATA(PI) IDENTIFY request to a drive
 *	and waits for a response.  It also monitors irqs while this is
 *	happening, in hope of automatically determining which one is
 *	being used by the interface.
 *
 *	Returns:	0  device was identified
 *			1  device timed-out (no response to identify request)
 *			2  device aborted the command (refused to identify itself)
 */

static int actual_try_to_identify (ide_drive_t *drive, u8 cmd)
{
	ide_hwif_t *hwif = HWIF(drive);
	int rc;
	unsigned long hd_status;
	unsigned long timeout;
	u8 s = 0, a = 0;

	/* take a deep breath */
	msleep(50);

	if (IDE_CONTROL_REG) {
		a = hwif->INB(IDE_ALTSTATUS_REG);
		s = hwif->INB(IDE_STATUS_REG);
		if ((a ^ s) & ~INDEX_STAT) {
			printk(KERN_INFO "%s: probing with STATUS(0x%02x) instead of "
				"ALTSTATUS(0x%02x)\n", drive->name, s, a);
			/* ancient Seagate drives, broken interfaces */
			hd_status = IDE_STATUS_REG;
		} else {
			/* use non-intrusive polling */
			hd_status = IDE_ALTSTATUS_REG;
		}
	} else
		hd_status = IDE_STATUS_REG;

	/* set features register for atapi
	 * identify command to be sure of reply
	 */
	if ((cmd == WIN_PIDENTIFY))
		/* disable dma & overlap */
		hwif->OUTB(0, IDE_FEATURE_REG);

	/* ask drive for ID */
	hwif->OUTB(cmd, IDE_COMMAND_REG);

	timeout = ((cmd == WIN_IDENTIFY) ? WAIT_WORSTCASE : WAIT_PIDENTIFY) / 2;
	timeout += jiffies;
	do {
		if (time_after(jiffies, timeout)) {
			/* drive timed-out */
			return 1;
		}
		/* give drive a breather */
		msleep(50);
	} while ((hwif->INB(hd_status)) & BUSY_STAT);

	/* wait for IRQ and DRQ_STAT */
	msleep(50);
	if (OK_STAT((hwif->INB(IDE_STATUS_REG)), DRQ_STAT, BAD_R_STAT)) {
		unsigned long flags;

		/* local CPU only; some systems need this */
		local_irq_save(flags);
		/* drive returned ID */
		do_identify(drive, cmd);
		/* drive responded with ID */
		rc = 0;
		/* clear drive IRQ */
		(void) hwif->INB(IDE_STATUS_REG);
		local_irq_restore(flags);
	} else {
		/* drive refused ID */
		rc = 2;
	}
	return rc;
}

/**
 *	try_to_identify	-	try to identify a drive
 *	@drive: drive to probe
 *	@cmd: command to use
 *
 *	Issue the identify command and then do IRQ probing to
 *	complete the identification when needed by finding the
 *	IRQ the drive is attached to
 */
 
static int try_to_identify (ide_drive_t *drive, u8 cmd)
{
	ide_hwif_t *hwif = HWIF(drive);
	int retval;
	int autoprobe = 0;
	unsigned long cookie = 0;

	/*
	 * Disable device irq unless we need to
	 * probe for it. Otherwise we'll get spurious
	 * interrupts during the identify-phase that
	 * the irq handler isn't expecting.
	 */
	if (IDE_CONTROL_REG) {
		u8 ctl = drive->ctl | 2;
		if (!hwif->irq) {
			autoprobe = 1;
			cookie = probe_irq_on();
			/* enable device irq */
			ctl &= ~2;
		}
		hwif->OUTB(ctl, IDE_CONTROL_REG);
	}

	retval = actual_try_to_identify(drive, cmd);

	if (autoprobe) {
		int irq;
		/* mask device irq */
		hwif->OUTB(drive->ctl|2, IDE_CONTROL_REG);
		/* clear drive IRQ */
		(void) hwif->INB(IDE_STATUS_REG);
		udelay(5);
		irq = probe_irq_off(cookie);
		if (!hwif->irq) {
			if (irq > 0) {
				hwif->irq = irq;
			} else {
				/* Mmmm.. multiple IRQs..
				 * don't know which was ours
				 */
				printk("%s: IRQ probe failed (0x%lx)\n",
					drive->name, cookie);
			}
		}
	}
	return retval;
}


/**
 *	do_probe		-	probe an IDE device
 *	@drive: drive to probe
 *	@cmd: command to use
 *
 *	do_probe() has the difficult job of finding a drive if it exists,
 *	without getting hung up if it doesn't exist, without trampling on
 *	ethernet cards, and without leaving any IRQs dangling to haunt us later.
 *
 *	If a drive is "known" to exist (from CMOS or kernel parameters),
 *	but does not respond right away, the probe will "hang in there"
 *	for the maximum wait time (about 30 seconds), otherwise it will
 *	exit much more quickly.
 *
 * Returns:	0  device was identified
 *		1  device timed-out (no response to identify request)
 *		2  device aborted the command (refused to identify itself)
 *		3  bad status from device (possible for ATAPI drives)
 *		4  probe was not attempted because failure was obvious
 */

static int do_probe (ide_drive_t *drive, u8 cmd)
{
	int rc;
	ide_hwif_t *hwif = HWIF(drive);

	if (drive->present) {
		/* avoid waiting for inappropriate probes */
		if ((drive->media != ide_disk) && (cmd == WIN_IDENTIFY))
			return 4;
	}
#ifdef DEBUG
	printk("probing for %s: present=%d, media=%d, probetype=%s\n",
		drive->name, drive->present, drive->media,
		(cmd == WIN_IDENTIFY) ? "ATA" : "ATAPI");
#endif

	/* needed for some systems
	 * (e.g. crw9624 as drive0 with disk as slave)
	 */
	msleep(50);
	SELECT_DRIVE(drive);
	msleep(50);
	if (hwif->INB(IDE_SELECT_REG) != drive->select.all && !drive->present) {
		if (drive->select.b.unit != 0) {
			/* exit with drive0 selected */
			SELECT_DRIVE(&hwif->drives[0]);
			/* allow BUSY_STAT to assert & clear */
			msleep(50);
		}
		/* no i/f present: mmm.. this should be a 4 -ml */
		return 3;
	}

	if (OK_STAT((hwif->INB(IDE_STATUS_REG)), READY_STAT, BUSY_STAT) ||
	    drive->present || cmd == WIN_PIDENTIFY) {
		/* send cmd and wait */
		if ((rc = try_to_identify(drive, cmd))) {
			/* failed: try again */
			rc = try_to_identify(drive,cmd);
		}
		if (hwif->INB(IDE_STATUS_REG) == (BUSY_STAT|READY_STAT))
			return 4;

		if ((rc == 1 && cmd == WIN_PIDENTIFY) &&
			((drive->autotune == IDE_TUNE_DEFAULT) ||
			(drive->autotune == IDE_TUNE_AUTO))) {
			unsigned long timeout;
			printk("%s: no response (status = 0x%02x), "
				"resetting drive\n", drive->name,
				hwif->INB(IDE_STATUS_REG));
			msleep(50);
			hwif->OUTB(drive->select.all, IDE_SELECT_REG);
			msleep(50);
			hwif->OUTB(WIN_SRST, IDE_COMMAND_REG);
			timeout = jiffies;
			while (((hwif->INB(IDE_STATUS_REG)) & BUSY_STAT) &&
			       time_before(jiffies, timeout + WAIT_WORSTCASE))
				msleep(50);
			rc = try_to_identify(drive, cmd);
		}
		if (rc == 1)
			printk("%s: no response (status = 0x%02x)\n",
				drive->name, hwif->INB(IDE_STATUS_REG));
		/* ensure drive irq is clear */
		(void) hwif->INB(IDE_STATUS_REG);
	} else {
		/* not present or maybe ATAPI */
		rc = 3;
	}
	if (drive->select.b.unit != 0) {
		/* exit with drive0 selected */
		SELECT_DRIVE(&hwif->drives[0]);
		msleep(50);
		/* ensure drive irq is clear */
		(void) hwif->INB(IDE_STATUS_REG);
	}
	return rc;
}

/*
 *
 */
static void enable_nest (ide_drive_t *drive)
{
	ide_hwif_t *hwif = HWIF(drive);
	unsigned long timeout;

	printk("%s: enabling %s -- ", hwif->name, drive->id->model);
	SELECT_DRIVE(drive);
	msleep(50);
	hwif->OUTB(EXABYTE_ENABLE_NEST, IDE_COMMAND_REG);
	timeout = jiffies + WAIT_WORSTCASE;
	do {
		if (time_after(jiffies, timeout)) {
			printk("failed (timeout)\n");
			return;
		}
		msleep(50);
	} while ((hwif->INB(IDE_STATUS_REG)) & BUSY_STAT);

	msleep(50);

	if (!OK_STAT((hwif->INB(IDE_STATUS_REG)), 0, BAD_STAT)) {
		printk("failed (status = 0x%02x)\n", hwif->INB(IDE_STATUS_REG));
	} else {
		printk("success\n");
	}

	/* if !(success||timed-out) */
	if (do_probe(drive, WIN_IDENTIFY) >= 2) {
		/* look for ATAPI device */
		(void) do_probe(drive, WIN_PIDENTIFY);
	}
}

/**
 *	probe_for_drives	-	upper level drive probe
 *	@drive: drive to probe for
 *
 *	probe_for_drive() tests for existence of a given drive using do_probe()
 *	and presents things to the user as needed.
 *
 *	Returns:	0  no device was found
 *			1  device was found (note: drive->present might
 *			   still be 0)
 */
 
static inline u8 probe_for_drive (ide_drive_t *drive)
{
	/*
	 *	In order to keep things simple we have an id
	 *	block for all drives at all times. If the device
	 *	is pre ATA or refuses ATA/ATAPI identify we
	 *	will add faked data to this.
	 *
	 *	Also note that 0 everywhere means "can't do X"
	 */
 
	drive->id = kzalloc(SECTOR_WORDS *4, GFP_KERNEL);
	drive->id_read = 0;
	if(drive->id == NULL)
	{
		printk(KERN_ERR "ide: out of memory for id data.\n");
		return 0;
	}
	strcpy(drive->id->model, "UNKNOWN");
	
	/* skip probing? */
	if (!drive->noprobe)
	{
		/* if !(success||timed-out) */
		if (do_probe(drive, WIN_IDENTIFY) >= 2) {
			/* look for ATAPI device */
			(void) do_probe(drive, WIN_PIDENTIFY);
		}
		if (strstr(drive->id->model, "E X A B Y T E N E S T"))
			enable_nest(drive);
		if (!drive->present)
			/* drive not found */
			return 0;
	
		/* identification failed? */
		if (!drive->id_read) {
			if (drive->media == ide_disk) {
				printk(KERN_INFO "%s: non-IDE drive, CHS=%d/%d/%d\n",
					drive->name, drive->cyl,
					drive->head, drive->sect);
			} else if (drive->media == ide_cdrom) {
				printk(KERN_INFO "%s: ATAPI cdrom (?)\n", drive->name);
			} else {
				/* nuke it */
				printk(KERN_WARNING "%s: Unknown device on bus refused identification. Ignoring.\n", drive->name);
				drive->present = 0;
			}
		}
		/* drive was found */
	}
	if(!drive->present)
		return 0;
	/* The drive wasn't being helpful. Add generic info only */
	if (drive->id_read == 0) {
		generic_id(drive);
		return 1;
	}

	if (drive->media == ide_disk) {
		ide_disk_init_chs(drive);
		ide_disk_init_mult_count(drive);
	}

	return drive->present;
}

static void hwif_release_dev (struct device *dev)
{
	ide_hwif_t *hwif = container_of(dev, ide_hwif_t, gendev);

	complete(&hwif->gendev_rel_comp);
}

static void hwif_register (ide_hwif_t *hwif)
{
	int ret;

	/* register with global device tree */
	strlcpy(hwif->gendev.bus_id,hwif->name,BUS_ID_SIZE);
	hwif->gendev.driver_data = hwif;
	if (hwif->gendev.parent == NULL) {
		if (hwif->pci_dev)
			hwif->gendev.parent = &hwif->pci_dev->dev;
		else
			/* Would like to do = &device_legacy */
			hwif->gendev.parent = NULL;
	}
	hwif->gendev.release = hwif_release_dev;
	ret = device_register(&hwif->gendev);
	if (ret < 0)
		printk(KERN_WARNING "IDE: %s: device_register error: %d\n",
			__FUNCTION__, ret);
}

static int wait_hwif_ready(ide_hwif_t *hwif)
{
	int rc;

	printk(KERN_DEBUG "Probing IDE interface %s...\n", hwif->name);

	/* Let HW settle down a bit from whatever init state we
	 * come from */
	mdelay(2);

	/* Wait for BSY bit to go away, spec timeout is 30 seconds,
	 * I know of at least one disk who takes 31 seconds, I use 35
	 * here to be safe
	 */
	rc = ide_wait_not_busy(hwif, 35000);
	if (rc)
		return rc;

	/* Now make sure both master & slave are ready */
	SELECT_DRIVE(&hwif->drives[0]);
	hwif->OUTB(8, hwif->io_ports[IDE_CONTROL_OFFSET]);
	mdelay(2);
	rc = ide_wait_not_busy(hwif, 35000);
	if (rc)
		return rc;
	SELECT_DRIVE(&hwif->drives[1]);
	hwif->OUTB(8, hwif->io_ports[IDE_CONTROL_OFFSET]);
	mdelay(2);
	rc = ide_wait_not_busy(hwif, 35000);

	/* Exit function with master reselected (let's be sane) */
	SELECT_DRIVE(&hwif->drives[0]);
	
	return rc;
}

/**
 *	ide_undecoded_slave	-	look for bad CF adapters
 *	@hwif: interface
 *
 *	Analyse the drives on the interface and attempt to decide if we
 *	have the same drive viewed twice. This occurs with crap CF adapters
 *	and PCMCIA sometimes.
 */

void ide_undecoded_slave(ide_hwif_t *hwif)
{
	ide_drive_t *drive0 = &hwif->drives[0];
	ide_drive_t *drive1 = &hwif->drives[1];

	if (drive0->present == 0 || drive1->present == 0)
		return;

	/* If the models don't match they are not the same product */
	if (strcmp(drive0->id->model, drive1->id->model))
		return;

	/* Serial numbers do not match */
	if (strncmp(drive0->id->serial_no, drive1->id->serial_no, 20))
		return;

	/* No serial number, thankfully very rare for CF */
	if (drive0->id->serial_no[0] == 0)
		return;

	/* Appears to be an IDE flash adapter with decode bugs */
	printk(KERN_WARNING "ide-probe: ignoring undecoded slave\n");

	drive1->present = 0;
}

EXPORT_SYMBOL_GPL(ide_undecoded_slave);

/*
 * This routine only knows how to look for drive units 0 and 1
 * on an interface, so any setting of MAX_DRIVES > 2 won't work here.
 */
static void probe_hwif(ide_hwif_t *hwif)
{
	unsigned int unit;
	unsigned long flags;
	unsigned int irqd;

	if (hwif->noprobe)
		return;

	if ((hwif->chipset != ide_4drives || !hwif->mate || !hwif->mate->present) &&
	    (ide_hwif_request_regions(hwif))) {
		u16 msgout = 0;
		for (unit = 0; unit < MAX_DRIVES; ++unit) {
			ide_drive_t *drive = &hwif->drives[unit];
			if (drive->present) {
				drive->present = 0;
				printk(KERN_ERR "%s: ERROR, PORTS ALREADY IN USE\n",
					drive->name);
				msgout = 1;
			}
		}
		if (!msgout)
			printk(KERN_ERR "%s: ports already in use, skipping probe\n",
				hwif->name);
		return;	
	}

	/*
	 * We must always disable IRQ, as probe_for_drive will assert IRQ, but
	 * we'll install our IRQ driver much later...
	 */
	irqd = hwif->irq;
	if (irqd)
		disable_irq(hwif->irq);

	local_irq_set(flags);

	/* This is needed on some PPCs and a bunch of BIOS-less embedded
	 * platforms. Typical cases are:
	 * 
	 *  - The firmware hard reset the disk before booting the kernel,
	 *    the drive is still doing it's poweron-reset sequence, that
	 *    can take up to 30 seconds
	 *  - The firmware does nothing (or no firmware), the device is
	 *    still in POST state (same as above actually).
	 *  - Some CD/DVD/Writer combo drives tend to drive the bus during
	 *    their reset sequence even when they are non-selected slave
	 *    devices, thus preventing discovery of the main HD
	 *    
	 *  Doing this wait-for-busy should not harm any existing configuration
	 *  (at least things won't be worse than what current code does, that
	 *  is blindly go & talk to the drive) and fix some issues like the
	 *  above.
	 *  
	 *  BenH.
	 */
	if (wait_hwif_ready(hwif) == -EBUSY)
		printk(KERN_DEBUG "%s: Wait for ready failed before probe !\n", hwif->name);

	/*
	 * Second drive should only exist if first drive was found,
	 * but a lot of cdrom drives are configured as single slaves.
	 */
	for (unit = 0; unit < MAX_DRIVES; ++unit) {
		ide_drive_t *drive = &hwif->drives[unit];
		drive->dn = (hwif->channel ? 2 : 0) + unit;
		(void) probe_for_drive(drive);
		if (drive->present && !hwif->present) {
			hwif->present = 1;
			if (hwif->chipset != ide_4drives ||
			    !hwif->mate || 
			    !hwif->mate->present) {
				hwif_register(hwif);
			}
		}
	}
	if (hwif->io_ports[IDE_CONTROL_OFFSET] && hwif->reset) {
		unsigned long timeout = jiffies + WAIT_WORSTCASE;
		u8 stat;

		printk(KERN_WARNING "%s: reset\n", hwif->name);
		hwif->OUTB(12, hwif->io_ports[IDE_CONTROL_OFFSET]);
		udelay(10);
		hwif->OUTB(8, hwif->io_ports[IDE_CONTROL_OFFSET]);
		do {
			msleep(50);
			stat = hwif->INB(hwif->io_ports[IDE_STATUS_OFFSET]);
		} while ((stat & BUSY_STAT) && time_after(timeout, jiffies));

	}
	local_irq_restore(flags);
	/*
	 * Use cached IRQ number. It might be (and is...) changed by probe
	 * code above
	 */
	if (irqd)
		enable_irq(irqd);

	if (!hwif->present) {
		ide_hwif_release_regions(hwif);
		return;
	}

	for (unit = 0; unit < MAX_DRIVES; ++unit) {
		ide_drive_t *drive = &hwif->drives[unit];

		if (drive->present) {
			if (hwif->tuneproc != NULL && 
				drive->autotune == IDE_TUNE_AUTO)
				/* auto-tune PIO mode */
				hwif->tuneproc(drive, 255);

			if (drive->autotune != IDE_TUNE_DEFAULT &&
			    drive->autotune != IDE_TUNE_AUTO)
				continue;

			drive->nice1 = 1;

			/*
			 * MAJOR HACK BARF :-/
			 *
			 * FIXME: chipsets own this cruft!
			 */
			/*
			 * Move here to prevent module loading clashing.
			 */
	//		drive->autodma = hwif->autodma;
			if (hwif->ide_dma_check) {
				/*
				 * Force DMAing for the beginning of the check.
				 * Some chipsets appear to do interesting
				 * things, if not checked and cleared.
				 *   PARANOIA!!!
				 */
				hwif->dma_off_quietly(drive);
#ifdef CONFIG_IDEDMA_ONLYDISK
				if (drive->media == ide_disk)
#endif
					ide_set_dma(drive);
			}
		}
	}

	for (unit = 0; unit < MAX_DRIVES; ++unit) {
		ide_drive_t *drive = &hwif->drives[unit];

		if (hwif->no_io_32bit)
			drive->no_io_32bit = 1;
		else
			drive->no_io_32bit = drive->id->dword_io ? 1 : 0;
	}
}

static int hwif_init(ide_hwif_t *hwif);

int probe_hwif_init_with_fixup(ide_hwif_t *hwif, void (*fixup)(ide_hwif_t *hwif))
{
	probe_hwif(hwif);

	if (fixup)
		fixup(hwif);

	if (!hwif_init(hwif)) {
		printk(KERN_INFO "%s: failed to initialize IDE interface\n",
				 hwif->name);
		return -1;
	}

	if (hwif->present) {
		u16 unit = 0;
		int ret;

		for (unit = 0; unit < MAX_DRIVES; ++unit) {
			ide_drive_t *drive = &hwif->drives[unit];
			/* For now don't attach absent drives, we may
			   want them on default or a new "empty" class
			   for hotplug reprobing ? */
			if (drive->present) {
				ret = device_register(&drive->gendev);
				if (ret < 0)
					printk(KERN_WARNING "IDE: %s: "
						"device_register error: %d\n",
						__FUNCTION__, ret);
			}
		}
	}
	return 0;
}

int probe_hwif_init(ide_hwif_t *hwif)
{
	return probe_hwif_init_with_fixup(hwif, NULL);
}

EXPORT_SYMBOL(probe_hwif_init);

#if MAX_HWIFS > 1
/*
 * save_match() is used to simplify logic in init_irq() below.
 *
 * A loophole here is that we may not know about a particular
 * hwif's irq until after that hwif is actually probed/initialized..
 * This could be a problem for the case where an hwif is on a
 * dual interface that requires serialization (eg. cmd640) and another
 * hwif using one of the same irqs is initialized beforehand.
 *
 * This routine detects and reports such situations, but does not fix them.
 */
static void save_match(ide_hwif_t *hwif, ide_hwif_t *new, ide_hwif_t **match)
{
	ide_hwif_t *m = *match;

	if (m && m->hwgroup && m->hwgroup != new->hwgroup) {
		if (!new->hwgroup)
			return;
		printk("%s: potential irq problem with %s and %s\n",
			hwif->name, new->name, m->name);
	}
	if (!m || m->irq != hwif->irq) /* don't undo a prior perfect match */
		*match = new;
}
#endif /* MAX_HWIFS > 1 */

/*
 * init request queue
 */
static int ide_init_queue(ide_drive_t *drive)
{
	request_queue_t *q;
	ide_hwif_t *hwif = HWIF(drive);
	int max_sectors = 256;
	int max_sg_entries = PRD_ENTRIES;

	/*
	 *	Our default set up assumes the normal IDE case,
	 *	that is 64K segmenting, standard PRD setup
	 *	and LBA28. Some drivers then impose their own
	 *	limits and LBA48 we could raise it but as yet
	 *	do not.
	 */

	q = blk_init_queue_node(do_ide_request, &ide_lock, hwif_to_node(hwif));
	if (!q)
		return 1;

	q->queuedata = drive;
	blk_queue_segment_boundary(q, 0xffff);

	if (!hwif->rqsize) {
		if (hwif->no_lba48 || hwif->no_lba48_dma)
			hwif->rqsize = 256;
		else
			hwif->rqsize = 65536;
	}
	if (hwif->rqsize < max_sectors)
		max_sectors = hwif->rqsize;
	blk_queue_max_sectors(q, max_sectors);

#ifdef CONFIG_PCI
	/* When we have an IOMMU, we may have a problem where pci_map_sg()
	 * creates segments that don't completely match our boundary
	 * requirements and thus need to be broken up again. Because it
	 * doesn't align properly either, we may actually have to break up
	 * to more segments than what was we got in the first place, a max
	 * worst case is twice as many.
	 * This will be fixed once we teach pci_map_sg() about our boundary
	 * requirements, hopefully soon. *FIXME*
	 */
	if (!PCI_DMA_BUS_IS_PHYS)
		max_sg_entries >>= 1;
#endif /* CONFIG_PCI */

	blk_queue_max_hw_segments(q, max_sg_entries);
	blk_queue_max_phys_segments(q, max_sg_entries);

	/* assign drive queue */
	drive->queue = q;

	/* needs drive->queue to be set */
	ide_toggle_bounce(drive, 1);

	return 0;
}

/*
 * This routine sets up the irq for an ide interface, and creates a new
 * hwgroup for the irq/hwif if none was previously assigned.
 *
 * Much of the code is for correctly detecting/handling irq sharing
 * and irq serialization situations.  This is somewhat complex because
 * it handles static as well as dynamic (PCMCIA) IDE interfaces.
 *
 * The IRQF_DISABLED in sa_flags means ide_intr() is always entered with
 * interrupts completely disabled.  This can be bad for interrupt latency,
 * but anything else has led to problems on some machines.  We re-enable
 * interrupts as much as we can safely do in most places.
 */
static int init_irq (ide_hwif_t *hwif)
{
	unsigned int index;
	ide_hwgroup_t *hwgroup;
	ide_hwif_t *match = NULL;


	BUG_ON(in_interrupt());
	BUG_ON(irqs_disabled());	
	BUG_ON(hwif == NULL);

	down(&ide_cfg_sem);
	hwif->hwgroup = NULL;
#if MAX_HWIFS > 1
	/*
	 * Group up with any other hwifs that share our irq(s).
	 */
	for (index = 0; index < MAX_HWIFS; index++) {
		ide_hwif_t *h = &ide_hwifs[index];
		if (h->hwgroup) {  /* scan only initialized hwif's */
			if (hwif->irq == h->irq) {
				hwif->sharing_irq = h->sharing_irq = 1;
				if (hwif->chipset != ide_pci ||
				    h->chipset != ide_pci) {
					save_match(hwif, h, &match);
				}
			}
			if (hwif->serialized) {
				if (hwif->mate && hwif->mate->irq == h->irq)
					save_match(hwif, h, &match);
			}
			if (h->serialized) {
				if (h->mate && hwif->irq == h->mate->irq)
					save_match(hwif, h, &match);
			}
		}
	}
#endif /* MAX_HWIFS > 1 */
	/*
	 * If we are still without a hwgroup, then form a new one
	 */
	if (match) {
		hwgroup = match->hwgroup;
		hwif->hwgroup = hwgroup;
		/*
		 * Link us into the hwgroup.
		 * This must be done early, do ensure that unexpected_intr
		 * can find the hwif and prevent irq storms.
		 * No drives are attached to the new hwif, choose_drive
		 * can't do anything stupid (yet).
		 * Add ourself as the 2nd entry to the hwgroup->hwif
		 * linked list, the first entry is the hwif that owns
		 * hwgroup->handler - do not change that.
		 */
		spin_lock_irq(&ide_lock);
		hwif->next = hwgroup->hwif->next;
		hwgroup->hwif->next = hwif;
		spin_unlock_irq(&ide_lock);
	} else {
		hwgroup = kmalloc_node(sizeof(ide_hwgroup_t), GFP_KERNEL,
					hwif_to_node(hwif->drives[0].hwif));
		if (!hwgroup)
	       		goto out_up;

		hwif->hwgroup = hwgroup;

		memset(hwgroup, 0, sizeof(ide_hwgroup_t));
		hwgroup->hwif     = hwif->next = hwif;
		hwgroup->rq       = NULL;
		hwgroup->handler  = NULL;
		hwgroup->drive    = NULL;
		hwgroup->busy     = 0;
		init_timer(&hwgroup->timer);
		hwgroup->timer.function = &ide_timer_expiry;
		hwgroup->timer.data = (unsigned long) hwgroup;
	}

	/*
	 * Allocate the irq, if not already obtained for another hwif
	 */
	if (!match || match->irq != hwif->irq) {
		int sa = IRQF_DISABLED;
#if defined(__mc68000__) || defined(CONFIG_APUS)
		sa = IRQF_SHARED;
#endif /* __mc68000__ || CONFIG_APUS */

		if (IDE_CHIPSET_IS_PCI(hwif->chipset)) {
			sa = IRQF_SHARED;
#ifndef CONFIG_IDEPCI_SHARE_IRQ
			sa |= IRQF_DISABLED;
#endif /* CONFIG_IDEPCI_SHARE_IRQ */
		}

		if (hwif->io_ports[IDE_CONTROL_OFFSET])
			/* clear nIEN */
			hwif->OUTB(0x08, hwif->io_ports[IDE_CONTROL_OFFSET]);

		if (request_irq(hwif->irq,&ide_intr,sa,hwif->name,hwgroup))
	       		goto out_unlink;
	}

	/*
	 * For any present drive:
	 * - allocate the block device queue
	 * - link drive into the hwgroup
	 */
	for (index = 0; index < MAX_DRIVES; ++index) {
		ide_drive_t *drive = &hwif->drives[index];
		if (!drive->present)
			continue;
		if (ide_init_queue(drive)) {
			printk(KERN_ERR "ide: failed to init %s\n",drive->name);
			continue;
		}
		spin_lock_irq(&ide_lock);
		if (!hwgroup->drive) {
			/* first drive for hwgroup. */
			drive->next = drive;
			hwgroup->drive = drive;
			hwgroup->hwif = HWIF(hwgroup->drive);
		} else {
			drive->next = hwgroup->drive->next;
			hwgroup->drive->next = drive;
		}
		spin_unlock_irq(&ide_lock);
	}

#if !defined(__mc68000__) && !defined(CONFIG_APUS)
	printk("%s at 0x%03lx-0x%03lx,0x%03lx on irq %d", hwif->name,
		hwif->io_ports[IDE_DATA_OFFSET],
		hwif->io_ports[IDE_DATA_OFFSET]+7,
		hwif->io_ports[IDE_CONTROL_OFFSET], hwif->irq);
#else
	printk("%s at 0x%08lx on irq %d", hwif->name,
		hwif->io_ports[IDE_DATA_OFFSET], hwif->irq);
#endif /* __mc68000__ && CONFIG_APUS */
	if (match)
		printk(" (%sed with %s)",
			hwif->sharing_irq ? "shar" : "serializ", match->name);
	printk("\n");
	up(&ide_cfg_sem);
	return 0;
out_unlink:
	spin_lock_irq(&ide_lock);
	if (hwif->next == hwif) {
		BUG_ON(match);
		BUG_ON(hwgroup->hwif != hwif);
		kfree(hwgroup);
	} else {
		ide_hwif_t *g;
		g = hwgroup->hwif;
		while (g->next != hwif)
			g = g->next;
		g->next = hwif->next;
		if (hwgroup->hwif == hwif) {
			/* Impossible. */
			printk(KERN_ERR "Duh. Uninitialized hwif listed as active hwif.\n");
			hwgroup->hwif = g;
		}
		BUG_ON(hwgroup->hwif == hwif);
	}
	spin_unlock_irq(&ide_lock);
out_up:
	up(&ide_cfg_sem);
	return 1;
}

static int ata_lock(dev_t dev, void *data)
{
	/* FIXME: we want to pin hwif down */
	return 0;
}

static struct kobject *ata_probe(dev_t dev, int *part, void *data)
{
	ide_hwif_t *hwif = data;
	int unit = *part >> PARTN_BITS;
	ide_drive_t *drive = &hwif->drives[unit];
	if (!drive->present)
		return NULL;

	if (drive->media == ide_disk)
		request_module("ide-disk");
	if (drive->scsi)
		request_module("ide-scsi");
	if (drive->media == ide_cdrom || drive->media == ide_optical)
		request_module("ide-cd");
	if (drive->media == ide_tape)
		request_module("ide-tape");
	if (drive->media == ide_floppy)
		request_module("ide-floppy");

	return NULL;
}

static struct kobject *exact_match(dev_t dev, int *part, void *data)
{
	struct gendisk *p = data;
	*part &= (1 << PARTN_BITS) - 1;
	return &p->kobj;
}

static int exact_lock(dev_t dev, void *data)
{
	struct gendisk *p = data;

	if (!get_disk(p))
		return -1;
	return 0;
}

void ide_register_region(struct gendisk *disk)
{
	blk_register_region(MKDEV(disk->major, disk->first_minor),
			    disk->minors, NULL, exact_match, exact_lock, disk);
}

EXPORT_SYMBOL_GPL(ide_register_region);

void ide_unregister_region(struct gendisk *disk)
{
	blk_unregister_region(MKDEV(disk->major, disk->first_minor),
			      disk->minors);
}

EXPORT_SYMBOL_GPL(ide_unregister_region);

void ide_init_disk(struct gendisk *disk, ide_drive_t *drive)
{
	ide_hwif_t *hwif = drive->hwif;
	unsigned int unit = (drive->select.all >> 4) & 1;

	disk->major = hwif->major;
	disk->first_minor = unit << PARTN_BITS;
	sprintf(disk->disk_name, "hd%c", 'a' + hwif->index * MAX_DRIVES + unit);
	disk->queue = drive->queue;
}

EXPORT_SYMBOL_GPL(ide_init_disk);

static void ide_remove_drive_from_hwgroup(ide_drive_t *drive)
{
	ide_hwgroup_t *hwgroup = drive->hwif->hwgroup;

	if (drive == drive->next) {
		/* special case: last drive from hwgroup. */
		BUG_ON(hwgroup->drive != drive);
		hwgroup->drive = NULL;
	} else {
		ide_drive_t *walk;

		walk = hwgroup->drive;
		while (walk->next != drive)
			walk = walk->next;
		walk->next = drive->next;
		if (hwgroup->drive == drive) {
			hwgroup->drive = drive->next;
			hwgroup->hwif = hwgroup->drive->hwif;
		}
	}
	BUG_ON(hwgroup->drive == drive);
}

static void drive_release_dev (struct device *dev)
{
	ide_drive_t *drive = container_of(dev, ide_drive_t, gendev);

	spin_lock_irq(&ide_lock);
	ide_remove_drive_from_hwgroup(drive);
	kfree(drive->id);
	drive->id = NULL;
	drive->present = 0;
	/* Messed up locking ... */
	spin_unlock_irq(&ide_lock);
	blk_cleanup_queue(drive->queue);
	spin_lock_irq(&ide_lock);
	drive->queue = NULL;
	spin_unlock_irq(&ide_lock);

	complete(&drive->gendev_rel_comp);
}

/*
 * init_gendisk() (as opposed to ide_geninit) is called for each major device,
 * after probing for drives, to allocate partition tables and other data
 * structures needed for the routines in genhd.c.  ide_geninit() gets called
 * somewhat later, during the partition check.
 */
static void init_gendisk (ide_hwif_t *hwif)
{
	unsigned int unit;

	for (unit = 0; unit < MAX_DRIVES; ++unit) {
		ide_drive_t * drive = &hwif->drives[unit];
		ide_add_generic_settings(drive);
		snprintf(drive->gendev.bus_id,BUS_ID_SIZE,"%u.%u",
			 hwif->index,unit);
		drive->gendev.parent = &hwif->gendev;
		drive->gendev.bus = &ide_bus_type;
		drive->gendev.driver_data = drive;
		drive->gendev.release = drive_release_dev;
	}
	blk_register_region(MKDEV(hwif->major, 0), MAX_DRIVES << PARTN_BITS,
			THIS_MODULE, ata_probe, ata_lock, hwif);
}

static int hwif_init(ide_hwif_t *hwif)
{
	int old_irq;

	/* Return success if no device is connected */
	if (!hwif->present)
		return 1;

	if (!hwif->irq) {
		if (!(hwif->irq = ide_default_irq(hwif->io_ports[IDE_DATA_OFFSET])))
		{
			printk("%s: DISABLED, NO IRQ\n", hwif->name);
			return (hwif->present = 0);
		}
	}
#ifdef CONFIG_BLK_DEV_HD
	if (hwif->irq == HD_IRQ && hwif->io_ports[IDE_DATA_OFFSET] != HD_DATA) {
		printk("%s: CANNOT SHARE IRQ WITH OLD "
			"HARDDISK DRIVER (hd.c)\n", hwif->name);
		return (hwif->present = 0);
	}
#endif /* CONFIG_BLK_DEV_HD */

	/* we set it back to 1 if all is ok below */	
	hwif->present = 0;

	if (register_blkdev(hwif->major, hwif->name))
		return 0;

	if (!hwif->sg_max_nents)
		hwif->sg_max_nents = PRD_ENTRIES;

	hwif->sg_table = kmalloc(sizeof(struct scatterlist)*hwif->sg_max_nents,
				 GFP_KERNEL);
	if (!hwif->sg_table) {
		printk(KERN_ERR "%s: unable to allocate SG table.\n", hwif->name);
		goto out;
	}
	
	if (init_irq(hwif) == 0)
		goto done;

	old_irq = hwif->irq;
	/*
	 *	It failed to initialise. Find the default IRQ for 
	 *	this port and try that.
	 */
	if (!(hwif->irq = ide_default_irq(hwif->io_ports[IDE_DATA_OFFSET]))) {
		printk("%s: Disabled unable to get IRQ %d.\n",
			hwif->name, old_irq);
		goto out;
	}
	if (init_irq(hwif)) {
		printk("%s: probed IRQ %d and default IRQ %d failed.\n",
			hwif->name, old_irq, hwif->irq);
		goto out;
	}
	printk("%s: probed IRQ %d failed, using default.\n",
		hwif->name, hwif->irq);

done:
	init_gendisk(hwif);

	ide_acpi_init(hwif);

	hwif->present = 1;	/* success */
	return 1;

out:
	unregister_blkdev(hwif->major, hwif->name);
	return 0;
}

int ideprobe_init (void)
{
	unsigned int index;
	int probe[MAX_HWIFS];

	memset(probe, 0, MAX_HWIFS * sizeof(int));
	for (index = 0; index < MAX_HWIFS; ++index)
		probe[index] = !ide_hwifs[index].present;

	for (index = 0; index < MAX_HWIFS; ++index)
		if (probe[index])
			probe_hwif(&ide_hwifs[index]);
	for (index = 0; index < MAX_HWIFS; ++index)
		if (probe[index])
			hwif_init(&ide_hwifs[index]);
	for (index = 0; index < MAX_HWIFS; ++index) {
		if (probe[index]) {
			ide_hwif_t *hwif = &ide_hwifs[index];
			int unit;
			if (!hwif->present)
				continue;
			if (hwif->chipset == ide_unknown || hwif->chipset == ide_forced)
				hwif->chipset = ide_generic;
			for (unit = 0; unit < MAX_DRIVES; ++unit)
				if (hwif->drives[unit].present) {
					int ret = device_register(
						&hwif->drives[unit].gendev);
					if (ret < 0)
						printk(KERN_WARNING "IDE: %s: "
							"device_register error: %d\n",
							__FUNCTION__, ret);
				}
		}
	}
	for (index = 0; index < MAX_HWIFS; ++index)
		if (probe[index])
			ide_proc_register_port(&ide_hwifs[index]);
	return 0;
}

EXPORT_SYMBOL_GPL(ideprobe_init);
