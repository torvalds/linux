/*
 *  Copyright (C) 1994-1998   Linus Torvalds & authors (see below)
 *  Copyright (C) 2005, 2007  Bartlomiej Zolnierkiewicz
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
 * -- increase WAIT_PIDENTIFY to avoid CD-ROM locking at boot
 *	 by Andrea Arcangeli
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
#include <linux/scatterlist.h>

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
	u16 *id = drive->id;

	id[ATA_ID_CUR_CYLS]	= id[ATA_ID_CYLS]	= drive->cyl;
	id[ATA_ID_CUR_HEADS]	= id[ATA_ID_HEADS]	= drive->head;
	id[ATA_ID_CUR_SECTORS]	= id[ATA_ID_SECTORS]	= drive->sect;
}

static void ide_disk_init_chs(ide_drive_t *drive)
{
	u16 *id = drive->id;

	/* Extract geometry if we did not already have one for the drive */
	if (!drive->cyl || !drive->head || !drive->sect) {
		drive->cyl  = drive->bios_cyl  = id[ATA_ID_CYLS];
		drive->head = drive->bios_head = id[ATA_ID_HEADS];
		drive->sect = drive->bios_sect = id[ATA_ID_SECTORS];
	}

	/* Handle logical geometry translation by the drive */
	if (ata_id_current_chs_valid(id)) {
		drive->cyl  = id[ATA_ID_CUR_CYLS];
		drive->head = id[ATA_ID_CUR_HEADS];
		drive->sect = id[ATA_ID_CUR_SECTORS];
	}

	/* Use physical geometry if what we have still makes no sense */
	if (drive->head > 16 && id[ATA_ID_HEADS] && id[ATA_ID_HEADS] <= 16) {
		drive->cyl  = id[ATA_ID_CYLS];
		drive->head = id[ATA_ID_HEADS];
		drive->sect = id[ATA_ID_SECTORS];
	}
}

static void ide_disk_init_mult_count(ide_drive_t *drive)
{
	u16 *id = drive->id;
	u8 max_multsect = id[ATA_ID_MAX_MULTSECT] & 0xff;

	if (max_multsect) {
		if ((max_multsect / 2) > 1)
			id[ATA_ID_MULTSECT] = max_multsect | 0x100;
		else
			id[ATA_ID_MULTSECT] &= ~0x1ff;

		drive->mult_req = id[ATA_ID_MULTSECT] & 0xff;

		if (drive->mult_req)
			drive->special.b.set_multmode = 1;
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
	u16 *id = drive->id;
	char *m = (char *)&id[ATA_ID_PROD];
	int bswap = 1, is_cfa;

	/* read 512 bytes of id info */
	hwif->tp_ops->input_data(drive, NULL, id, SECTOR_SIZE);

	drive->id_read = 1;
	local_irq_enable();
#ifdef DEBUG
	printk(KERN_INFO "%s: dumping identify data\n", drive->name);
	ide_dump_identify((u8 *)id);
#endif
	ide_fix_driveid(id);

	/*
	 *  ATA_CMD_ID_ATA returns little-endian info,
	 *  ATA_CMD_ID_ATAPI *usually* returns little-endian info.
	 */
	if (cmd == ATA_CMD_ID_ATAPI) {
		if ((m[0] == 'N' && m[1] == 'E') ||  /* NEC */
		    (m[0] == 'F' && m[1] == 'X') ||  /* Mitsumi */
		    (m[0] == 'P' && m[1] == 'i'))    /* Pioneer */
			/* Vertos drives may still be weird */
			bswap ^= 1;
	}

	ide_fixstring(m, ATA_ID_PROD_LEN, bswap);
	ide_fixstring((char *)&id[ATA_ID_FW_REV], ATA_ID_FW_REV_LEN, bswap);
	ide_fixstring((char *)&id[ATA_ID_SERNO], ATA_ID_SERNO_LEN, bswap);

	/* we depend on this a lot! */
	m[ATA_ID_PROD_LEN - 1] = '\0';

	if (strstr(m, "E X A B Y T E N E S T"))
		goto err_misc;

	printk(KERN_INFO "%s: %s, ", drive->name, m);

	drive->present = 1;
	drive->dead = 0;

	/*
	 * Check for an ATAPI device
	 */
	if (cmd == ATA_CMD_ID_ATAPI) {
		u8 type = (id[ATA_ID_CONFIG] >> 8) & 0x1f;

		printk(KERN_CONT "ATAPI ");
		switch (type) {
			case ide_floppy:
				if (!strstr(m, "CD-ROM")) {
					if (!strstr(m, "oppy") &&
					    !strstr(m, "poyp") &&
					    !strstr(m, "ZIP"))
						printk(KERN_CONT "cdrom or floppy?, assuming ");
					if (drive->media != ide_cdrom) {
						printk(KERN_CONT "FLOPPY");
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
				if (!strstr(m, "CD-ROM") && strstr(m, "ZIP")) {
					printk(KERN_CONT "FLOPPY");
					type = ide_floppy;
					break;
				}
#endif
				printk(KERN_CONT "CD/DVD-ROM");
				break;
			case ide_tape:
				printk(KERN_CONT "TAPE");
				break;
			case ide_optical:
				printk(KERN_CONT "OPTICAL");
				drive->removable = 1;
				break;
			default:
				printk(KERN_CONT "UNKNOWN (type %d)", type);
				break;
		}
		printk(KERN_CONT " drive\n");
		drive->media = type;
		/* an ATAPI device ignores DRDY */
		drive->ready_stat = 0;
		return;
	}

	/*
	 * Not an ATAPI device: looks like a "regular" hard disk
	 */

	is_cfa = ata_id_is_cfa(id);

	/* CF devices are *not* removable in Linux definition of the term */
	if (is_cfa == 0 && (id[ATA_ID_CONFIG] & (1 << 7)))
		drive->removable = 1;

	drive->media = ide_disk;

	printk(KERN_CONT "%s DISK drive\n", is_cfa ? "CFA" : "ATA");

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
	struct ide_io_ports *io_ports = &hwif->io_ports;
	const struct ide_tp_ops *tp_ops = hwif->tp_ops;
	int use_altstatus = 0, rc;
	unsigned long timeout;
	u8 s = 0, a = 0;

	/* take a deep breath */
	msleep(50);

	if (io_ports->ctl_addr) {
		a = tp_ops->read_altstatus(hwif);
		s = tp_ops->read_status(hwif);
		if ((a ^ s) & ~ATA_IDX)
			/* ancient Seagate drives, broken interfaces */
			printk(KERN_INFO "%s: probing with STATUS(0x%02x) "
					 "instead of ALTSTATUS(0x%02x)\n",
					 drive->name, s, a);
		else
			/* use non-intrusive polling */
			use_altstatus = 1;
	}

	/* set features register for atapi
	 * identify command to be sure of reply
	 */
	if (cmd == ATA_CMD_ID_ATAPI) {
		ide_task_t task;

		memset(&task, 0, sizeof(task));
		/* disable DMA & overlap */
		task.tf_flags = IDE_TFLAG_OUT_FEATURE;

		tp_ops->tf_load(drive, &task);
	}

	/* ask drive for ID */
	tp_ops->exec_command(hwif, cmd);

	timeout = ((cmd == ATA_CMD_ID_ATA) ? WAIT_WORSTCASE : WAIT_PIDENTIFY) / 2;

	if (ide_busy_sleep(hwif, timeout, use_altstatus))
		return 1;

	/* wait for IRQ and ATA_DRQ */
	msleep(50);
	s = tp_ops->read_status(hwif);

	if (OK_STAT(s, ATA_DRQ, BAD_R_STAT)) {
		unsigned long flags;

		/* local CPU only; some systems need this */
		local_irq_save(flags);
		/* drive returned ID */
		do_identify(drive, cmd);
		/* drive responded with ID */
		rc = 0;
		/* clear drive IRQ */
		(void)tp_ops->read_status(hwif);
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
	const struct ide_tp_ops *tp_ops = hwif->tp_ops;
	int retval;
	int autoprobe = 0;
	unsigned long cookie = 0;

	/*
	 * Disable device irq unless we need to
	 * probe for it. Otherwise we'll get spurious
	 * interrupts during the identify-phase that
	 * the irq handler isn't expecting.
	 */
	if (hwif->io_ports.ctl_addr) {
		if (!hwif->irq) {
			autoprobe = 1;
			cookie = probe_irq_on();
		}
		tp_ops->set_irq(hwif, autoprobe);
	}

	retval = actual_try_to_identify(drive, cmd);

	if (autoprobe) {
		int irq;

		tp_ops->set_irq(hwif, 0);
		/* clear drive IRQ */
		(void)tp_ops->read_status(hwif);
		udelay(5);
		irq = probe_irq_off(cookie);
		if (!hwif->irq) {
			if (irq > 0) {
				hwif->irq = irq;
			} else {
				/* Mmmm.. multiple IRQs..
				 * don't know which was ours
				 */
				printk(KERN_ERR "%s: IRQ probe failed (0x%lx)\n",
					drive->name, cookie);
			}
		}
	}
	return retval;
}

int ide_busy_sleep(ide_hwif_t *hwif, unsigned long timeout, int altstatus)
{
	u8 stat;

	timeout += jiffies;

	do {
		msleep(50);	/* give drive a breather */
		stat = altstatus ? hwif->tp_ops->read_altstatus(hwif)
				 : hwif->tp_ops->read_status(hwif);
		if ((stat & ATA_BUSY) == 0)
			return 0;
	} while (time_before(jiffies, timeout));

	return 1;	/* drive timed-out */
}

static u8 ide_read_device(ide_drive_t *drive)
{
	ide_task_t task;

	memset(&task, 0, sizeof(task));
	task.tf_flags = IDE_TFLAG_IN_DEVICE;

	drive->hwif->tp_ops->tf_read(drive, &task);

	return task.tf.device;
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
	ide_hwif_t *hwif = HWIF(drive);
	const struct ide_tp_ops *tp_ops = hwif->tp_ops;
	int rc;
	u8 stat;

	if (drive->present) {
		/* avoid waiting for inappropriate probes */
		if (drive->media != ide_disk && cmd == ATA_CMD_ID_ATA)
			return 4;
	}
#ifdef DEBUG
	printk(KERN_INFO "probing for %s: present=%d, media=%d, probetype=%s\n",
		drive->name, drive->present, drive->media,
		(cmd == ATA_CMD_ID_ATA) ? "ATA" : "ATAPI");
#endif

	/* needed for some systems
	 * (e.g. crw9624 as drive0 with disk as slave)
	 */
	msleep(50);
	SELECT_DRIVE(drive);
	msleep(50);

	if (ide_read_device(drive) != drive->select.all && !drive->present) {
		if (drive->select.b.unit != 0) {
			/* exit with drive0 selected */
			SELECT_DRIVE(&hwif->drives[0]);
			/* allow ATA_BUSY to assert & clear */
			msleep(50);
		}
		/* no i/f present: mmm.. this should be a 4 -ml */
		return 3;
	}

	stat = tp_ops->read_status(hwif);

	if (OK_STAT(stat, ATA_DRDY, ATA_BUSY) ||
	    drive->present || cmd == ATA_CMD_ID_ATAPI) {
		/* send cmd and wait */
		if ((rc = try_to_identify(drive, cmd))) {
			/* failed: try again */
			rc = try_to_identify(drive,cmd);
		}

		stat = tp_ops->read_status(hwif);

		if (stat == (ATA_BUSY | ATA_DRDY))
			return 4;

		if (rc == 1 && cmd == ATA_CMD_ID_ATAPI) {
			printk(KERN_ERR "%s: no response (status = 0x%02x), "
					"resetting drive\n", drive->name, stat);
			msleep(50);
			SELECT_DRIVE(drive);
			msleep(50);
			tp_ops->exec_command(hwif, ATA_CMD_DEV_RESET);
			(void)ide_busy_sleep(hwif, WAIT_WORSTCASE, 0);
			rc = try_to_identify(drive, cmd);
		}

		/* ensure drive IRQ is clear */
		stat = tp_ops->read_status(hwif);

		if (rc == 1)
			printk(KERN_ERR "%s: no response (status = 0x%02x)\n",
					drive->name, stat);
	} else {
		/* not present or maybe ATAPI */
		rc = 3;
	}
	if (drive->select.b.unit != 0) {
		/* exit with drive0 selected */
		SELECT_DRIVE(&hwif->drives[0]);
		msleep(50);
		/* ensure drive irq is clear */
		(void)tp_ops->read_status(hwif);
	}
	return rc;
}

/*
 *
 */
static void enable_nest (ide_drive_t *drive)
{
	ide_hwif_t *hwif = HWIF(drive);
	const struct ide_tp_ops *tp_ops = hwif->tp_ops;
	u8 stat;

	printk(KERN_INFO "%s: enabling %s -- ",
		hwif->name, (char *)&drive->id[ATA_ID_PROD]);

	SELECT_DRIVE(drive);
	msleep(50);
	tp_ops->exec_command(hwif, ATA_EXABYTE_ENABLE_NEST);

	if (ide_busy_sleep(hwif, WAIT_WORSTCASE, 0)) {
		printk(KERN_CONT "failed (timeout)\n");
		return;
	}

	msleep(50);

	stat = tp_ops->read_status(hwif);

	if (!OK_STAT(stat, 0, BAD_STAT))
		printk(KERN_CONT "failed (status = 0x%02x)\n", stat);
	else
		printk(KERN_CONT "success\n");
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
	char *m;

	/*
	 *	In order to keep things simple we have an id
	 *	block for all drives at all times. If the device
	 *	is pre ATA or refuses ATA/ATAPI identify we
	 *	will add faked data to this.
	 *
	 *	Also note that 0 everywhere means "can't do X"
	 */
 
	drive->id = kzalloc(SECTOR_SIZE, GFP_KERNEL);
	drive->id_read = 0;
	if(drive->id == NULL)
	{
		printk(KERN_ERR "ide: out of memory for id data.\n");
		return 0;
	}

	m = (char *)&drive->id[ATA_ID_PROD];
	strcpy(m, "UNKNOWN");

	/* skip probing? */
	if (!drive->noprobe) {
retry:
		/* if !(success||timed-out) */
		if (do_probe(drive, ATA_CMD_ID_ATA) >= 2)
			/* look for ATAPI device */
			(void)do_probe(drive, ATA_CMD_ID_ATAPI);

		if (!drive->present)
			/* drive not found */
			return 0;

		if (strstr(m, "E X A B Y T E N E S T")) {
			enable_nest(drive);
			goto retry;
		}

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

static void hwif_release_dev(struct device *dev)
{
	ide_hwif_t *hwif = container_of(dev, ide_hwif_t, gendev);

	complete(&hwif->gendev_rel_comp);
}

static int ide_register_port(ide_hwif_t *hwif)
{
	int ret;

	/* register with global device tree */
	strlcpy(hwif->gendev.bus_id,hwif->name,BUS_ID_SIZE);
	hwif->gendev.driver_data = hwif;
	if (hwif->gendev.parent == NULL) {
		if (hwif->dev)
			hwif->gendev.parent = hwif->dev;
		else
			/* Would like to do = &device_legacy */
			hwif->gendev.parent = NULL;
	}
	hwif->gendev.release = hwif_release_dev;
	ret = device_register(&hwif->gendev);
	if (ret < 0) {
		printk(KERN_WARNING "IDE: %s: device_register error: %d\n",
			__func__, ret);
		goto out;
	}

	hwif->portdev = device_create_drvdata(ide_port_class, &hwif->gendev,
					      MKDEV(0, 0), hwif, hwif->name);
	if (IS_ERR(hwif->portdev)) {
		ret = PTR_ERR(hwif->portdev);
		device_unregister(&hwif->gendev);
	}
out:
	return ret;
}

/**
 *	ide_port_wait_ready	-	wait for port to become ready
 *	@hwif: IDE port
 *
 *	This is needed on some PPCs and a bunch of BIOS-less embedded
 *	platforms.  Typical cases are:
 *
 *	- The firmware hard reset the disk before booting the kernel,
 *	  the drive is still doing it's poweron-reset sequence, that
 *	  can take up to 30 seconds.
 *
 *	- The firmware does nothing (or no firmware), the device is
 *	  still in POST state (same as above actually).
 *
 *	- Some CD/DVD/Writer combo drives tend to drive the bus during
 *	  their reset sequence even when they are non-selected slave
 *	  devices, thus preventing discovery of the main HD.
 *
 *	Doing this wait-for-non-busy should not harm any existing
 *	configuration and fix some issues like the above.
 *
 *	BenH.
 *
 *	Returns 0 on success, error code (< 0) otherwise.
 */

static int ide_port_wait_ready(ide_hwif_t *hwif)
{
	int unit, rc;

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
	for (unit = 0; unit < MAX_DRIVES; unit++) {
		ide_drive_t *drive = &hwif->drives[unit];

		/* Ignore disks that we will not probe for later. */
		if (!drive->noprobe || drive->present) {
			SELECT_DRIVE(drive);
			hwif->tp_ops->set_irq(hwif, 1);
			mdelay(2);
			rc = ide_wait_not_busy(hwif, 35000);
			if (rc)
				goto out;
		} else
			printk(KERN_DEBUG "%s: ide_wait_not_busy() skipped\n",
					  drive->name);
	}
out:
	/* Exit function with master reselected (let's be sane) */
	if (unit)
		SELECT_DRIVE(&hwif->drives[0]);

	return rc;
}

/**
 *	ide_undecoded_slave	-	look for bad CF adapters
 *	@dev1: slave device
 *
 *	Analyse the drives on the interface and attempt to decide if we
 *	have the same drive viewed twice. This occurs with crap CF adapters
 *	and PCMCIA sometimes.
 */

void ide_undecoded_slave(ide_drive_t *dev1)
{
	ide_drive_t *dev0 = &dev1->hwif->drives[0];

	if ((dev1->dn & 1) == 0 || dev0->present == 0)
		return;

	/* If the models don't match they are not the same product */
	if (strcmp((char *)&dev0->id[ATA_ID_PROD],
		   (char *)&dev1->id[ATA_ID_PROD]))
		return;

	/* Serial numbers do not match */
	if (strncmp((char *)&dev0->id[ATA_ID_SERNO],
		    (char *)&dev1->id[ATA_ID_SERNO], ATA_ID_SERNO_LEN))
		return;

	/* No serial number, thankfully very rare for CF */
	if (*(char *)&dev0->id[ATA_ID_SERNO] == 0)
		return;

	/* Appears to be an IDE flash adapter with decode bugs */
	printk(KERN_WARNING "ide-probe: ignoring undecoded slave\n");

	dev1->present = 0;
}

EXPORT_SYMBOL_GPL(ide_undecoded_slave);

static int ide_probe_port(ide_hwif_t *hwif)
{
	unsigned long flags;
	unsigned int irqd;
	int unit, rc = -ENODEV;

	BUG_ON(hwif->present);

	if (hwif->drives[0].noprobe && hwif->drives[1].noprobe)
		return -EACCES;

	/*
	 * We must always disable IRQ, as probe_for_drive will assert IRQ, but
	 * we'll install our IRQ driver much later...
	 */
	irqd = hwif->irq;
	if (irqd)
		disable_irq(hwif->irq);

	local_irq_set(flags);

	if (ide_port_wait_ready(hwif) == -EBUSY)
		printk(KERN_DEBUG "%s: Wait for ready failed before probe !\n", hwif->name);

	/*
	 * Second drive should only exist if first drive was found,
	 * but a lot of cdrom drives are configured as single slaves.
	 */
	for (unit = 0; unit < MAX_DRIVES; ++unit) {
		ide_drive_t *drive = &hwif->drives[unit];
		drive->dn = (hwif->channel ? 2 : 0) + unit;
		(void) probe_for_drive(drive);
		if (drive->present)
			rc = 0;
	}

	local_irq_restore(flags);

	/*
	 * Use cached IRQ number. It might be (and is...) changed by probe
	 * code above
	 */
	if (irqd)
		enable_irq(irqd);

	return rc;
}

static void ide_port_tune_devices(ide_hwif_t *hwif)
{
	const struct ide_port_ops *port_ops = hwif->port_ops;
	int unit;

	for (unit = 0; unit < MAX_DRIVES; unit++) {
		ide_drive_t *drive = &hwif->drives[unit];

		if (drive->present && port_ops && port_ops->quirkproc)
			port_ops->quirkproc(drive);
	}

	for (unit = 0; unit < MAX_DRIVES; ++unit) {
		ide_drive_t *drive = &hwif->drives[unit];

		if (drive->present) {
			ide_set_max_pio(drive);

			drive->nice1 = 1;

			if (hwif->dma_ops)
				ide_set_dma(drive);
		}
	}

	for (unit = 0; unit < MAX_DRIVES; ++unit) {
		ide_drive_t *drive = &hwif->drives[unit];

		if (hwif->host_flags & IDE_HFLAG_NO_IO_32BIT)
			drive->no_io_32bit = 1;
		else
			drive->no_io_32bit = drive->id[ATA_ID_DWORD_IO] ? 1 : 0;
	}
}

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
		printk(KERN_WARNING "%s: potential IRQ problem with %s and %s\n",
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
	struct request_queue *q;
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

static void ide_add_drive_to_hwgroup(ide_drive_t *drive)
{
	ide_hwgroup_t *hwgroup = drive->hwif->hwgroup;

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

/*
 * For any present drive:
 * - allocate the block device queue
 * - link drive into the hwgroup
 */
static void ide_port_setup_devices(ide_hwif_t *hwif)
{
	int i;

	mutex_lock(&ide_cfg_mtx);
	for (i = 0; i < MAX_DRIVES; i++) {
		ide_drive_t *drive = &hwif->drives[i];

		if (!drive->present)
			continue;

		if (ide_init_queue(drive)) {
			printk(KERN_ERR "ide: failed to init %s\n",
					drive->name);
			continue;
		}

		ide_add_drive_to_hwgroup(drive);
	}
	mutex_unlock(&ide_cfg_mtx);
}

static ide_hwif_t *ide_ports[MAX_HWIFS];

void ide_remove_port_from_hwgroup(ide_hwif_t *hwif)
{
	ide_hwgroup_t *hwgroup = hwif->hwgroup;

	ide_ports[hwif->index] = NULL;

	spin_lock_irq(&ide_lock);
	/*
	 * Remove us from the hwgroup, and free
	 * the hwgroup if we were the only member
	 */
	if (hwif->next == hwif) {
		BUG_ON(hwgroup->hwif != hwif);
		kfree(hwgroup);
	} else {
		/* There is another interface in hwgroup.
		 * Unlink us, and set hwgroup->drive and ->hwif to
		 * something sane.
		 */
		ide_hwif_t *g = hwgroup->hwif;

		while (g->next != hwif)
			g = g->next;
		g->next = hwif->next;
		if (hwgroup->hwif == hwif) {
			/* Chose a random hwif for hwgroup->hwif.
			 * It's guaranteed that there are no drives
			 * left in the hwgroup.
			 */
			BUG_ON(hwgroup->drive != NULL);
			hwgroup->hwif = g;
		}
		BUG_ON(hwgroup->hwif == hwif);
	}
	spin_unlock_irq(&ide_lock);
}

/*
 * This routine sets up the irq for an ide interface, and creates a new
 * hwgroup for the irq/hwif if none was previously assigned.
 *
 * Much of the code is for correctly detecting/handling irq sharing
 * and irq serialization situations.  This is somewhat complex because
 * it handles static as well as dynamic (PCMCIA) IDE interfaces.
 */
static int init_irq (ide_hwif_t *hwif)
{
	struct ide_io_ports *io_ports = &hwif->io_ports;
	unsigned int index;
	ide_hwgroup_t *hwgroup;
	ide_hwif_t *match = NULL;

	mutex_lock(&ide_cfg_mtx);
	hwif->hwgroup = NULL;
#if MAX_HWIFS > 1
	/*
	 * Group up with any other hwifs that share our irq(s).
	 */
	for (index = 0; index < MAX_HWIFS; index++) {
		ide_hwif_t *h = ide_ports[index];

		if (h && h->hwgroup) {  /* scan only initialized ports */
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
		BUG_ON(hwif->next == hwif);
		spin_unlock_irq(&ide_lock);
	} else {
		hwgroup = kmalloc_node(sizeof(*hwgroup), GFP_KERNEL|__GFP_ZERO,
				       hwif_to_node(hwif));
		if (hwgroup == NULL)
			goto out_up;

		hwif->hwgroup = hwgroup;
		hwgroup->hwif = hwif->next = hwif;

		init_timer(&hwgroup->timer);
		hwgroup->timer.function = &ide_timer_expiry;
		hwgroup->timer.data = (unsigned long) hwgroup;
	}

	ide_ports[hwif->index] = hwif;

	/*
	 * Allocate the irq, if not already obtained for another hwif
	 */
	if (!match || match->irq != hwif->irq) {
		int sa = 0;
#if defined(__mc68000__)
		sa = IRQF_SHARED;
#endif /* __mc68000__ */

		if (hwif->chipset == ide_pci || hwif->chipset == ide_cmd646 ||
		    hwif->chipset == ide_ali14xx)
			sa = IRQF_SHARED;

		if (io_ports->ctl_addr)
			hwif->tp_ops->set_irq(hwif, 1);

		if (request_irq(hwif->irq,&ide_intr,sa,hwif->name,hwgroup))
	       		goto out_unlink;
	}

	if (!hwif->rqsize) {
		if ((hwif->host_flags & IDE_HFLAG_NO_LBA48) ||
		    (hwif->host_flags & IDE_HFLAG_NO_LBA48_DMA))
			hwif->rqsize = 256;
		else
			hwif->rqsize = 65536;
	}

#if !defined(__mc68000__)
	printk(KERN_INFO "%s at 0x%03lx-0x%03lx,0x%03lx on irq %d", hwif->name,
		io_ports->data_addr, io_ports->status_addr,
		io_ports->ctl_addr, hwif->irq);
#else
	printk(KERN_INFO "%s at 0x%08lx on irq %d", hwif->name,
		io_ports->data_addr, hwif->irq);
#endif /* __mc68000__ */
	if (match)
		printk(KERN_CONT " (%sed with %s)",
			hwif->sharing_irq ? "shar" : "serializ", match->name);
	printk(KERN_CONT "\n");

	mutex_unlock(&ide_cfg_mtx);
	return 0;
out_unlink:
	ide_remove_port_from_hwgroup(hwif);
out_up:
	mutex_unlock(&ide_cfg_mtx);
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
	return &disk_to_dev(p)->kobj;
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

	ide_proc_unregister_device(drive);

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

static int hwif_init(ide_hwif_t *hwif)
{
	int old_irq;

	if (!hwif->irq) {
		hwif->irq = __ide_default_irq(hwif->io_ports.data_addr);
		if (!hwif->irq) {
			printk(KERN_ERR "%s: disabled, no IRQ\n", hwif->name);
			return 0;
		}
	}

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

	sg_init_table(hwif->sg_table, hwif->sg_max_nents);
	
	if (init_irq(hwif) == 0)
		goto done;

	old_irq = hwif->irq;
	/*
	 *	It failed to initialise. Find the default IRQ for 
	 *	this port and try that.
	 */
	hwif->irq = __ide_default_irq(hwif->io_ports.data_addr);
	if (!hwif->irq) {
		printk(KERN_ERR "%s: disabled, unable to get IRQ %d\n",
			hwif->name, old_irq);
		goto out;
	}
	if (init_irq(hwif)) {
		printk(KERN_ERR "%s: probed IRQ %d and default IRQ %d failed\n",
			hwif->name, old_irq, hwif->irq);
		goto out;
	}
	printk(KERN_WARNING "%s: probed IRQ %d failed, using default\n",
		hwif->name, hwif->irq);

done:
	blk_register_region(MKDEV(hwif->major, 0), MAX_DRIVES << PARTN_BITS,
			    THIS_MODULE, ata_probe, ata_lock, hwif);
	return 1;

out:
	unregister_blkdev(hwif->major, hwif->name);
	return 0;
}

static void hwif_register_devices(ide_hwif_t *hwif)
{
	unsigned int i;

	for (i = 0; i < MAX_DRIVES; i++) {
		ide_drive_t *drive = &hwif->drives[i];
		struct device *dev = &drive->gendev;
		int ret;

		if (!drive->present)
			continue;

		snprintf(dev->bus_id, BUS_ID_SIZE, "%u.%u", hwif->index, i);
		dev->parent = &hwif->gendev;
		dev->bus = &ide_bus_type;
		dev->driver_data = drive;
		dev->release = drive_release_dev;

		ret = device_register(dev);
		if (ret < 0)
			printk(KERN_WARNING "IDE: %s: device_register error: "
					    "%d\n", __func__, ret);
	}
}

static void ide_port_init_devices(ide_hwif_t *hwif)
{
	const struct ide_port_ops *port_ops = hwif->port_ops;
	int i;

	for (i = 0; i < MAX_DRIVES; i++) {
		ide_drive_t *drive = &hwif->drives[i];

		if (hwif->host_flags & IDE_HFLAG_IO_32BIT)
			drive->io_32bit = 1;
		if (hwif->host_flags & IDE_HFLAG_UNMASK_IRQS)
			drive->unmask = 1;
		if (hwif->host_flags & IDE_HFLAG_NO_UNMASK_IRQS)
			drive->no_unmask = 1;

		if (port_ops && port_ops->init_dev)
			port_ops->init_dev(drive);
	}
}

static void ide_init_port(ide_hwif_t *hwif, unsigned int port,
			  const struct ide_port_info *d)
{
	hwif->channel = port;

	if (d->chipset)
		hwif->chipset = d->chipset;

	if (d->init_iops)
		d->init_iops(hwif);

	if ((!hwif->irq && (d->host_flags & IDE_HFLAG_LEGACY_IRQS)) ||
	    (d->host_flags & IDE_HFLAG_FORCE_LEGACY_IRQS))
		hwif->irq = port ? 15 : 14;

	/* ->host_flags may be set by ->init_iops (or even earlier...) */
	hwif->host_flags |= d->host_flags;
	hwif->pio_mask = d->pio_mask;

	if (d->tp_ops)
		hwif->tp_ops = d->tp_ops;

	/* ->set_pio_mode for DTC2278 is currently limited to port 0 */
	if (hwif->chipset != ide_dtc2278 || hwif->channel == 0)
		hwif->port_ops = d->port_ops;

	hwif->swdma_mask = d->swdma_mask;
	hwif->mwdma_mask = d->mwdma_mask;
	hwif->ultra_mask = d->udma_mask;

	if ((d->host_flags & IDE_HFLAG_NO_DMA) == 0) {
		int rc;

		if (d->init_dma)
			rc = d->init_dma(hwif, d);
		else
			rc = ide_hwif_setup_dma(hwif, d);

		if (rc < 0) {
			printk(KERN_INFO "%s: DMA disabled\n", hwif->name);
			hwif->dma_base = 0;
			hwif->swdma_mask = 0;
			hwif->mwdma_mask = 0;
			hwif->ultra_mask = 0;
		} else if (d->dma_ops)
			hwif->dma_ops = d->dma_ops;
	}

	if ((d->host_flags & IDE_HFLAG_SERIALIZE) ||
	    ((d->host_flags & IDE_HFLAG_SERIALIZE_DMA) && hwif->dma_base)) {
		if (hwif->mate)
			hwif->mate->serialized = hwif->serialized = 1;
	}

	if (d->host_flags & IDE_HFLAG_RQSIZE_256)
		hwif->rqsize = 256;

	/* call chipset specific routine for each enabled port */
	if (d->init_hwif)
		d->init_hwif(hwif);
}

static void ide_port_cable_detect(ide_hwif_t *hwif)
{
	const struct ide_port_ops *port_ops = hwif->port_ops;

	if (port_ops && port_ops->cable_detect && (hwif->ultra_mask & 0x78)) {
		if (hwif->cbl != ATA_CBL_PATA40_SHORT)
			hwif->cbl = port_ops->cable_detect(hwif);
	}
}

static ssize_t store_delete_devices(struct device *portdev,
				    struct device_attribute *attr,
				    const char *buf, size_t n)
{
	ide_hwif_t *hwif = dev_get_drvdata(portdev);

	if (strncmp(buf, "1", n))
		return -EINVAL;

	ide_port_unregister_devices(hwif);

	return n;
};

static DEVICE_ATTR(delete_devices, S_IWUSR, NULL, store_delete_devices);

static ssize_t store_scan(struct device *portdev,
			  struct device_attribute *attr,
			  const char *buf, size_t n)
{
	ide_hwif_t *hwif = dev_get_drvdata(portdev);

	if (strncmp(buf, "1", n))
		return -EINVAL;

	ide_port_unregister_devices(hwif);
	ide_port_scan(hwif);

	return n;
};

static DEVICE_ATTR(scan, S_IWUSR, NULL, store_scan);

static struct device_attribute *ide_port_attrs[] = {
	&dev_attr_delete_devices,
	&dev_attr_scan,
	NULL
};

static int ide_sysfs_register_port(ide_hwif_t *hwif)
{
	int i, uninitialized_var(rc);

	for (i = 0; ide_port_attrs[i]; i++) {
		rc = device_create_file(hwif->portdev, ide_port_attrs[i]);
		if (rc)
			break;
	}

	return rc;
}

static unsigned int ide_indexes;

/**
 *	ide_find_port_slot	-	find free port slot
 *	@d: IDE port info
 *
 *	Return the new port slot index or -ENOENT if we are out of free slots.
 */

static int ide_find_port_slot(const struct ide_port_info *d)
{
	int idx = -ENOENT;
	u8 bootable = (d && (d->host_flags & IDE_HFLAG_NON_BOOTABLE)) ? 0 : 1;
	u8 i = (d && (d->host_flags & IDE_HFLAG_QD_2ND_PORT)) ? 1 : 0;;

	/*
	 * Claim an unassigned slot.
	 *
	 * Give preference to claiming other slots before claiming ide0/ide1,
	 * just in case there's another interface yet-to-be-scanned
	 * which uses ports 0x1f0/0x170 (the ide0/ide1 defaults).
	 *
	 * Unless there is a bootable card that does not use the standard
	 * ports 0x1f0/0x170 (the ide0/ide1 defaults).
	 */
	mutex_lock(&ide_cfg_mtx);
	if (MAX_HWIFS == 1) {
		if (ide_indexes == 0 && i == 0)
			idx = 1;
	} else {
		if (bootable) {
			if ((ide_indexes | i) != (1 << MAX_HWIFS) - 1)
				idx = ffz(ide_indexes | i);
		} else {
			if ((ide_indexes | 3) != (1 << MAX_HWIFS) - 1)
				idx = ffz(ide_indexes | 3);
			else if ((ide_indexes & 3) != 3)
				idx = ffz(ide_indexes);
		}
	}
	if (idx >= 0)
		ide_indexes |= (1 << idx);
	mutex_unlock(&ide_cfg_mtx);

	return idx;
}

static void ide_free_port_slot(int idx)
{
	mutex_lock(&ide_cfg_mtx);
	ide_indexes &= ~(1 << idx);
	mutex_unlock(&ide_cfg_mtx);
}

struct ide_host *ide_host_alloc_all(const struct ide_port_info *d,
				    hw_regs_t **hws)
{
	struct ide_host *host;
	int i;

	host = kzalloc(sizeof(*host), GFP_KERNEL);
	if (host == NULL)
		return NULL;

	for (i = 0; i < MAX_HWIFS; i++) {
		ide_hwif_t *hwif;
		int idx;

		if (hws[i] == NULL)
			continue;

		hwif = kzalloc(sizeof(*hwif), GFP_KERNEL);
		if (hwif == NULL)
			continue;

		idx = ide_find_port_slot(d);
		if (idx < 0) {
			printk(KERN_ERR "%s: no free slot for interface\n",
					d ? d->name : "ide");
			kfree(hwif);
			continue;
		}

		ide_init_port_data(hwif, idx);

		hwif->host = host;

		host->ports[i] = hwif;
		host->n_ports++;
	}

	if (host->n_ports == 0) {
		kfree(host);
		return NULL;
	}

	if (hws[0])
		host->dev[0] = hws[0]->dev;

	if (d)
		host->host_flags = d->host_flags;

	return host;
}
EXPORT_SYMBOL_GPL(ide_host_alloc_all);

struct ide_host *ide_host_alloc(const struct ide_port_info *d, hw_regs_t **hws)
{
	hw_regs_t *hws_all[MAX_HWIFS];
	int i;

	for (i = 0; i < MAX_HWIFS; i++)
		hws_all[i] = (i < 4) ? hws[i] : NULL;

	return ide_host_alloc_all(d, hws_all);
}
EXPORT_SYMBOL_GPL(ide_host_alloc);

int ide_host_register(struct ide_host *host, const struct ide_port_info *d,
		      hw_regs_t **hws)
{
	ide_hwif_t *hwif, *mate = NULL;
	int i, j = 0;

	for (i = 0; i < MAX_HWIFS; i++) {
		hwif = host->ports[i];

		if (hwif == NULL) {
			mate = NULL;
			continue;
		}

		ide_init_port_hw(hwif, hws[i]);
		ide_port_apply_params(hwif);

		if (d == NULL) {
			mate = NULL;
			continue;
		}

		if ((i & 1) && mate) {
			hwif->mate = mate;
			mate->mate = hwif;
		}

		mate = (i & 1) ? NULL : hwif;

		ide_init_port(hwif, i & 1, d);
		ide_port_cable_detect(hwif);
		ide_port_init_devices(hwif);
	}

	for (i = 0; i < MAX_HWIFS; i++) {
		hwif = host->ports[i];

		if (hwif == NULL)
			continue;

		if (ide_probe_port(hwif) == 0)
			hwif->present = 1;

		if (hwif->chipset != ide_4drives || !hwif->mate ||
		    !hwif->mate->present)
			ide_register_port(hwif);

		if (hwif->present)
			ide_port_tune_devices(hwif);
	}

	for (i = 0; i < MAX_HWIFS; i++) {
		hwif = host->ports[i];

		if (hwif == NULL)
			continue;

		if (hwif_init(hwif) == 0) {
			printk(KERN_INFO "%s: failed to initialize IDE "
					 "interface\n", hwif->name);
			hwif->present = 0;
			continue;
		}

		j++;

		if (hwif->present)
			ide_port_setup_devices(hwif);

		ide_acpi_init(hwif);

		if (hwif->present)
			ide_acpi_port_init_devices(hwif);
	}

	for (i = 0; i < MAX_HWIFS; i++) {
		hwif = host->ports[i];

		if (hwif == NULL)
			continue;

		if (hwif->chipset == ide_unknown)
			hwif->chipset = ide_generic;

		if (hwif->present)
			hwif_register_devices(hwif);
	}

	for (i = 0; i < MAX_HWIFS; i++) {
		hwif = host->ports[i];

		if (hwif == NULL)
			continue;

		ide_sysfs_register_port(hwif);
		ide_proc_register_port(hwif);

		if (hwif->present)
			ide_proc_port_register_devices(hwif);
	}

	return j ? 0 : -1;
}
EXPORT_SYMBOL_GPL(ide_host_register);

int ide_host_add(const struct ide_port_info *d, hw_regs_t **hws,
		 struct ide_host **hostp)
{
	struct ide_host *host;
	int rc;

	host = ide_host_alloc(d, hws);
	if (host == NULL)
		return -ENOMEM;

	rc = ide_host_register(host, d, hws);
	if (rc) {
		ide_host_free(host);
		return rc;
	}

	if (hostp)
		*hostp = host;

	return 0;
}
EXPORT_SYMBOL_GPL(ide_host_add);

void ide_host_free(struct ide_host *host)
{
	ide_hwif_t *hwif;
	int i;

	for (i = 0; i < MAX_HWIFS; i++) {
		hwif = host->ports[i];

		if (hwif == NULL)
			continue;

		ide_free_port_slot(hwif->index);
		kfree(hwif);
	}

	kfree(host);
}
EXPORT_SYMBOL_GPL(ide_host_free);

void ide_host_remove(struct ide_host *host)
{
	int i;

	for (i = 0; i < MAX_HWIFS; i++) {
		if (host->ports[i])
			ide_unregister(host->ports[i]);
	}

	ide_host_free(host);
}
EXPORT_SYMBOL_GPL(ide_host_remove);

void ide_port_scan(ide_hwif_t *hwif)
{
	ide_port_apply_params(hwif);
	ide_port_cable_detect(hwif);
	ide_port_init_devices(hwif);

	if (ide_probe_port(hwif) < 0)
		return;

	hwif->present = 1;

	ide_port_tune_devices(hwif);
	ide_acpi_port_init_devices(hwif);
	ide_port_setup_devices(hwif);
	hwif_register_devices(hwif);
	ide_proc_port_register_devices(hwif);
}
EXPORT_SYMBOL_GPL(ide_port_scan);

static void ide_legacy_init_one(hw_regs_t **hws, hw_regs_t *hw,
				u8 port_no, const struct ide_port_info *d,
				unsigned long config)
{
	unsigned long base, ctl;
	int irq;

	if (port_no == 0) {
		base = 0x1f0;
		ctl  = 0x3f6;
		irq  = 14;
	} else {
		base = 0x170;
		ctl  = 0x376;
		irq  = 15;
	}

	if (!request_region(base, 8, d->name)) {
		printk(KERN_ERR "%s: I/O resource 0x%lX-0x%lX not free.\n",
				d->name, base, base + 7);
		return;
	}

	if (!request_region(ctl, 1, d->name)) {
		printk(KERN_ERR "%s: I/O resource 0x%lX not free.\n",
				d->name, ctl);
		release_region(base, 8);
		return;
	}

	ide_std_init_ports(hw, base, ctl);
	hw->irq = irq;
	hw->chipset = d->chipset;
	hw->config = config;

	hws[port_no] = hw;
}

int ide_legacy_device_add(const struct ide_port_info *d, unsigned long config)
{
	hw_regs_t hw[2], *hws[] = { NULL, NULL, NULL, NULL };

	memset(&hw, 0, sizeof(hw));

	if ((d->host_flags & IDE_HFLAG_QD_2ND_PORT) == 0)
		ide_legacy_init_one(hws, &hw[0], 0, d, config);
	ide_legacy_init_one(hws, &hw[1], 1, d, config);

	if (hws[0] == NULL && hws[1] == NULL &&
	    (d->host_flags & IDE_HFLAG_SINGLE))
		return -ENOENT;

	return ide_host_add(d, hws, NULL);
}
EXPORT_SYMBOL_GPL(ide_legacy_device_add);
