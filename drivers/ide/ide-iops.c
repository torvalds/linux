/*
 *  Copyright (C) 2000-2002	Andre Hedrick <andre@linux-ide.org>
 *  Copyright (C) 2003		Red Hat
 *
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
#include <linux/blkpg.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/ide.h>
#include <linux/bitops.h>
#include <linux/nmi.h>

#include <asm/byteorder.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/io.h>

void SELECT_DRIVE(ide_drive_t *drive)
{
	ide_hwif_t *hwif = drive->hwif;
	const struct ide_port_ops *port_ops = hwif->port_ops;
	ide_task_t task;

	if (port_ops && port_ops->selectproc)
		port_ops->selectproc(drive);

	memset(&task, 0, sizeof(task));
	task.tf_flags = IDE_TFLAG_OUT_DEVICE;

	drive->hwif->tp_ops->tf_load(drive, &task);
}

void SELECT_MASK(ide_drive_t *drive, int mask)
{
	const struct ide_port_ops *port_ops = drive->hwif->port_ops;

	if (port_ops && port_ops->maskproc)
		port_ops->maskproc(drive, mask);
}

u8 ide_read_error(ide_drive_t *drive)
{
	ide_task_t task;

	memset(&task, 0, sizeof(task));
	task.tf_flags = IDE_TFLAG_IN_FEATURE;

	drive->hwif->tp_ops->tf_read(drive, &task);

	return task.tf.error;
}
EXPORT_SYMBOL_GPL(ide_read_error);

void ide_fix_driveid(u16 *id)
{
#ifndef __LITTLE_ENDIAN
# ifdef __BIG_ENDIAN
	int i;

	for (i = 0; i < 256; i++)
		id[i] = __le16_to_cpu(id[i]);
# else
#  error "Please fix <asm/byteorder.h>"
# endif
#endif
}

/*
 * ide_fixstring() cleans up and (optionally) byte-swaps a text string,
 * removing leading/trailing blanks and compressing internal blanks.
 * It is primarily used to tidy up the model name/number fields as
 * returned by the ATA_CMD_ID_ATA[PI] commands.
 */

void ide_fixstring(u8 *s, const int bytecount, const int byteswap)
{
	u8 *p, *end = &s[bytecount & ~1]; /* bytecount must be even */

	if (byteswap) {
		/* convert from big-endian to host byte order */
		for (p = s ; p != end ; p += 2)
			be16_to_cpus((u16 *) p);
	}

	/* strip leading blanks */
	p = s;
	while (s != end && *s == ' ')
		++s;
	/* compress internal blanks and strip trailing blanks */
	while (s != end && *s) {
		if (*s++ != ' ' || (s != end && *s && *s != ' '))
			*p++ = *(s-1);
	}
	/* wipe out trailing garbage */
	while (p != end)
		*p++ = '\0';
}
EXPORT_SYMBOL(ide_fixstring);

/*
 * This routine busy-waits for the drive status to be not "busy".
 * It then checks the status for all of the "good" bits and none
 * of the "bad" bits, and if all is okay it returns 0.  All other
 * cases return error -- caller may then invoke ide_error().
 *
 * This routine should get fixed to not hog the cpu during extra long waits..
 * That could be done by busy-waiting for the first jiffy or two, and then
 * setting a timer to wake up at half second intervals thereafter,
 * until timeout is achieved, before timing out.
 */
static int __ide_wait_stat(ide_drive_t *drive, u8 good, u8 bad,
			   unsigned long timeout, u8 *rstat)
{
	ide_hwif_t *hwif = drive->hwif;
	const struct ide_tp_ops *tp_ops = hwif->tp_ops;
	unsigned long flags;
	int i;
	u8 stat;

	udelay(1);	/* spec allows drive 400ns to assert "BUSY" */
	stat = tp_ops->read_status(hwif);

	if (stat & ATA_BUSY) {
		local_save_flags(flags);
		local_irq_enable_in_hardirq();
		timeout += jiffies;
		while ((stat = tp_ops->read_status(hwif)) & ATA_BUSY) {
			if (time_after(jiffies, timeout)) {
				/*
				 * One last read after the timeout in case
				 * heavy interrupt load made us not make any
				 * progress during the timeout..
				 */
				stat = tp_ops->read_status(hwif);
				if ((stat & ATA_BUSY) == 0)
					break;

				local_irq_restore(flags);
				*rstat = stat;
				return -EBUSY;
			}
		}
		local_irq_restore(flags);
	}
	/*
	 * Allow status to settle, then read it again.
	 * A few rare drives vastly violate the 400ns spec here,
	 * so we'll wait up to 10usec for a "good" status
	 * rather than expensively fail things immediately.
	 * This fix courtesy of Matthew Faupel & Niccolo Rigacci.
	 */
	for (i = 0; i < 10; i++) {
		udelay(1);
		stat = tp_ops->read_status(hwif);

		if (OK_STAT(stat, good, bad)) {
			*rstat = stat;
			return 0;
		}
	}
	*rstat = stat;
	return -EFAULT;
}

/*
 * In case of error returns error value after doing "*startstop = ide_error()".
 * The caller should return the updated value of "startstop" in this case,
 * "startstop" is unchanged when the function returns 0.
 */
int ide_wait_stat(ide_startstop_t *startstop, ide_drive_t *drive, u8 good,
		  u8 bad, unsigned long timeout)
{
	int err;
	u8 stat;

	/* bail early if we've exceeded max_failures */
	if (drive->max_failures && (drive->failures > drive->max_failures)) {
		*startstop = ide_stopped;
		return 1;
	}

	err = __ide_wait_stat(drive, good, bad, timeout, &stat);

	if (err) {
		char *s = (err == -EBUSY) ? "status timeout" : "status error";
		*startstop = ide_error(drive, s, stat);
	}

	return err;
}
EXPORT_SYMBOL(ide_wait_stat);

/**
 *	ide_in_drive_list	-	look for drive in black/white list
 *	@id: drive identifier
 *	@table: list to inspect
 *
 *	Look for a drive in the blacklist and the whitelist tables
 *	Returns 1 if the drive is found in the table.
 */

int ide_in_drive_list(u16 *id, const struct drive_list_entry *table)
{
	for ( ; table->id_model; table++)
		if ((!strcmp(table->id_model, (char *)&id[ATA_ID_PROD])) &&
		    (!table->id_firmware ||
		     strstr((char *)&id[ATA_ID_FW_REV], table->id_firmware)))
			return 1;
	return 0;
}
EXPORT_SYMBOL_GPL(ide_in_drive_list);

/*
 * Early UDMA66 devices don't set bit14 to 1, only bit13 is valid.
 * We list them here and depend on the device side cable detection for them.
 *
 * Some optical devices with the buggy firmwares have the same problem.
 */
static const struct drive_list_entry ivb_list[] = {
	{ "QUANTUM FIREBALLlct10 05"	, "A03.0900"	},
	{ "TSSTcorp CDDVDW SH-S202J"	, "SB00"	},
	{ "TSSTcorp CDDVDW SH-S202J"	, "SB01"	},
	{ "TSSTcorp CDDVDW SH-S202N"	, "SB00"	},
	{ "TSSTcorp CDDVDW SH-S202N"	, "SB01"	},
	{ "TSSTcorp CDDVDW SH-S202H"	, "SB00"	},
	{ "TSSTcorp CDDVDW SH-S202H"	, "SB01"	},
	{ "SAMSUNG SP0822N"		, "WA100-10"	},
	{ NULL				, NULL		}
};

/*
 *  All hosts that use the 80c ribbon must use!
 *  The name is derived from upper byte of word 93 and the 80c ribbon.
 */
u8 eighty_ninty_three(ide_drive_t *drive)
{
	ide_hwif_t *hwif = drive->hwif;
	u16 *id = drive->id;
	int ivb = ide_in_drive_list(id, ivb_list);

	if (hwif->cbl == ATA_CBL_PATA40_SHORT)
		return 1;

	if (ivb)
		printk(KERN_DEBUG "%s: skipping word 93 validity check\n",
				  drive->name);

	if (ata_id_is_sata(id) && !ivb)
		return 1;

	if (hwif->cbl != ATA_CBL_PATA80 && !ivb)
		goto no_80w;

	/*
	 * FIXME:
	 * - change master/slave IDENTIFY order
	 * - force bit13 (80c cable present) check also for !ivb devices
	 *   (unless the slave device is pre-ATA3)
	 */
	if ((id[ATA_ID_HW_CONFIG] & 0x4000) ||
	    (ivb && (id[ATA_ID_HW_CONFIG] & 0x2000)))
		return 1;

no_80w:
	if (drive->dev_flags & IDE_DFLAG_UDMA33_WARNED)
		return 0;

	printk(KERN_WARNING "%s: %s side 80-wire cable detection failed, "
			    "limiting max speed to UDMA33\n",
			    drive->name,
			    hwif->cbl == ATA_CBL_PATA80 ? "drive" : "host");

	drive->dev_flags |= IDE_DFLAG_UDMA33_WARNED;

	return 0;
}

int ide_driveid_update(ide_drive_t *drive)
{
	ide_hwif_t *hwif = drive->hwif;
	const struct ide_tp_ops *tp_ops = hwif->tp_ops;
	u16 *id;
	unsigned long flags;
	int use_altstatus = 0, rc;
	u8 a, uninitialized_var(s);

	id = kmalloc(SECTOR_SIZE, GFP_ATOMIC);
	if (id == NULL)
		return 0;

	/*
	 * Re-read drive->id for possible DMA mode
	 * change (copied from ide-probe.c)
	 */

	SELECT_MASK(drive, 1);
	tp_ops->set_irq(hwif, 0);
	msleep(50);

	if (hwif->io_ports.ctl_addr &&
	    (hwif->host_flags & IDE_HFLAG_BROKEN_ALTSTATUS) == 0) {
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

	tp_ops->exec_command(hwif, ATA_CMD_ID_ATA);

	if (ide_busy_sleep(hwif, WAIT_WORSTCASE / 2, use_altstatus)) {
		rc = 1;
		goto out_err;
	}

	msleep(50);	/* wait for IRQ and ATA_DRQ */

	s = tp_ops->read_status(hwif);

	if (!OK_STAT(s, ATA_DRQ, BAD_R_STAT)) {
		rc = 2;
		goto out_err;
	}

	local_irq_save(flags);
	tp_ops->input_data(drive, NULL, id, SECTOR_SIZE);
	local_irq_restore(flags);

	(void)tp_ops->read_status(hwif); /* clear drive IRQ */

	ide_fix_driveid(id);

	SELECT_MASK(drive, 0);

	drive->id[ATA_ID_UDMA_MODES]  = id[ATA_ID_UDMA_MODES];
	drive->id[ATA_ID_MWDMA_MODES] = id[ATA_ID_MWDMA_MODES];
	drive->id[ATA_ID_SWDMA_MODES] = id[ATA_ID_SWDMA_MODES];
	/* anything more ? */

	kfree(id);

	if ((drive->dev_flags & IDE_DFLAG_USING_DMA) && ide_id_dma_bug(drive))
		ide_dma_off(drive);

	return 1;
out_err:
	SELECT_MASK(drive, 0);
	if (rc == 2)
		printk(KERN_ERR "%s: %s: bad status\n", drive->name, __func__);
	kfree(id);
	return 0;
}

int ide_config_drive_speed(ide_drive_t *drive, u8 speed)
{
	ide_hwif_t *hwif = drive->hwif;
	const struct ide_tp_ops *tp_ops = hwif->tp_ops;
	u16 *id = drive->id, i;
	int error = 0;
	u8 stat;
	ide_task_t task;

#ifdef CONFIG_BLK_DEV_IDEDMA
	if (hwif->dma_ops)	/* check if host supports DMA */
		hwif->dma_ops->dma_host_set(drive, 0);
#endif

	/* Skip setting PIO flow-control modes on pre-EIDE drives */
	if ((speed & 0xf8) == XFER_PIO_0 && ata_id_has_iordy(drive->id) == 0)
		goto skip;

	/*
	 * Don't use ide_wait_cmd here - it will
	 * attempt to set_geometry and recalibrate,
	 * but for some reason these don't work at
	 * this point (lost interrupt).
	 */

	/*
	 *	FIXME: we race against the running IRQ here if
	 *	this is called from non IRQ context. If we use
	 *	disable_irq() we hang on the error path. Work
	 *	is needed.
	 */
	disable_irq_nosync(hwif->irq);

	udelay(1);
	SELECT_DRIVE(drive);
	SELECT_MASK(drive, 1);
	udelay(1);
	tp_ops->set_irq(hwif, 0);

	memset(&task, 0, sizeof(task));
	task.tf_flags = IDE_TFLAG_OUT_FEATURE | IDE_TFLAG_OUT_NSECT;
	task.tf.feature = SETFEATURES_XFER;
	task.tf.nsect   = speed;

	tp_ops->tf_load(drive, &task);

	tp_ops->exec_command(hwif, ATA_CMD_SET_FEATURES);

	if (drive->quirk_list == 2)
		tp_ops->set_irq(hwif, 1);

	error = __ide_wait_stat(drive, drive->ready_stat,
				ATA_BUSY | ATA_DRQ | ATA_ERR,
				WAIT_CMD, &stat);

	SELECT_MASK(drive, 0);

	enable_irq(hwif->irq);

	if (error) {
		(void) ide_dump_status(drive, "set_drive_speed_status", stat);
		return error;
	}

	id[ATA_ID_UDMA_MODES]  &= ~0xFF00;
	id[ATA_ID_MWDMA_MODES] &= ~0x0F00;
	id[ATA_ID_SWDMA_MODES] &= ~0x0F00;

 skip:
#ifdef CONFIG_BLK_DEV_IDEDMA
	if (speed >= XFER_SW_DMA_0 && (drive->dev_flags & IDE_DFLAG_USING_DMA))
		hwif->dma_ops->dma_host_set(drive, 1);
	else if (hwif->dma_ops)	/* check if host supports DMA */
		ide_dma_off_quietly(drive);
#endif

	if (speed >= XFER_UDMA_0) {
		i = 1 << (speed - XFER_UDMA_0);
		id[ATA_ID_UDMA_MODES] |= (i << 8 | i);
	} else if (speed >= XFER_MW_DMA_0) {
		i = 1 << (speed - XFER_MW_DMA_0);
		id[ATA_ID_MWDMA_MODES] |= (i << 8 | i);
	} else if (speed >= XFER_SW_DMA_0) {
		i = 1 << (speed - XFER_SW_DMA_0);
		id[ATA_ID_SWDMA_MODES] |= (i << 8 | i);
	}

	if (!drive->init_speed)
		drive->init_speed = speed;
	drive->current_speed = speed;
	return error;
}

/*
 * This should get invoked any time we exit the driver to
 * wait for an interrupt response from a drive.  handler() points
 * at the appropriate code to handle the next interrupt, and a
 * timer is started to prevent us from waiting forever in case
 * something goes wrong (see the ide_timer_expiry() handler later on).
 *
 * See also ide_execute_command
 */
void __ide_set_handler(ide_drive_t *drive, ide_handler_t *handler,
		       unsigned int timeout, ide_expiry_t *expiry)
{
	ide_hwif_t *hwif = drive->hwif;

	BUG_ON(hwif->handler);
	hwif->handler		= handler;
	hwif->expiry		= expiry;
	hwif->timer.expires	= jiffies + timeout;
	hwif->req_gen_timer	= hwif->req_gen;
	add_timer(&hwif->timer);
}

void ide_set_handler (ide_drive_t *drive, ide_handler_t *handler,
		      unsigned int timeout, ide_expiry_t *expiry)
{
	ide_hwif_t *hwif = drive->hwif;
	unsigned long flags;

	spin_lock_irqsave(&hwif->lock, flags);
	__ide_set_handler(drive, handler, timeout, expiry);
	spin_unlock_irqrestore(&hwif->lock, flags);
}
EXPORT_SYMBOL(ide_set_handler);

/**
 *	ide_execute_command	-	execute an IDE command
 *	@drive: IDE drive to issue the command against
 *	@command: command byte to write
 *	@handler: handler for next phase
 *	@timeout: timeout for command
 *	@expiry:  handler to run on timeout
 *
 *	Helper function to issue an IDE command. This handles the
 *	atomicity requirements, command timing and ensures that the
 *	handler and IRQ setup do not race. All IDE command kick off
 *	should go via this function or do equivalent locking.
 */

void ide_execute_command(ide_drive_t *drive, u8 cmd, ide_handler_t *handler,
			 unsigned timeout, ide_expiry_t *expiry)
{
	ide_hwif_t *hwif = drive->hwif;
	unsigned long flags;

	spin_lock_irqsave(&hwif->lock, flags);
	__ide_set_handler(drive, handler, timeout, expiry);
	hwif->tp_ops->exec_command(hwif, cmd);
	/*
	 * Drive takes 400nS to respond, we must avoid the IRQ being
	 * serviced before that.
	 *
	 * FIXME: we could skip this delay with care on non shared devices
	 */
	ndelay(400);
	spin_unlock_irqrestore(&hwif->lock, flags);
}
EXPORT_SYMBOL(ide_execute_command);

void ide_execute_pkt_cmd(ide_drive_t *drive)
{
	ide_hwif_t *hwif = drive->hwif;
	unsigned long flags;

	spin_lock_irqsave(&hwif->lock, flags);
	hwif->tp_ops->exec_command(hwif, ATA_CMD_PACKET);
	ndelay(400);
	spin_unlock_irqrestore(&hwif->lock, flags);
}
EXPORT_SYMBOL_GPL(ide_execute_pkt_cmd);

/*
 * ide_wait_not_busy() waits for the currently selected device on the hwif
 * to report a non-busy status, see comments in ide_probe_port().
 */
int ide_wait_not_busy(ide_hwif_t *hwif, unsigned long timeout)
{
	u8 stat = 0;

	while (timeout--) {
		/*
		 * Turn this into a schedule() sleep once I'm sure
		 * about locking issues (2.5 work ?).
		 */
		mdelay(1);
		stat = hwif->tp_ops->read_status(hwif);
		if ((stat & ATA_BUSY) == 0)
			return 0;
		/*
		 * Assume a value of 0xff means nothing is connected to
		 * the interface and it doesn't implement the pull-down
		 * resistor on D7.
		 */
		if (stat == 0xff)
			return -ENODEV;
		touch_softlockup_watchdog();
		touch_nmi_watchdog();
	}
	return -EBUSY;
}
