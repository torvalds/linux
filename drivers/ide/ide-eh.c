
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/ide.h>
#include <linux/delay.h>

static ide_startstop_t ide_ata_error(ide_drive_t *drive, struct request *rq,
				     u8 stat, u8 err)
{
	ide_hwif_t *hwif = drive->hwif;

	if ((stat & ATA_BUSY) ||
	    ((stat & ATA_DF) && (drive->dev_flags & IDE_DFLAG_NOWERR) == 0)) {
		/* other bits are useless when BUSY */
		scsi_req(rq)->result |= ERROR_RESET;
	} else if (stat & ATA_ERR) {
		/* err has different meaning on cdrom and tape */
		if (err == ATA_ABORTED) {
			if ((drive->dev_flags & IDE_DFLAG_LBA) &&
			    /* some newer drives don't support ATA_CMD_INIT_DEV_PARAMS */
			    hwif->tp_ops->read_status(hwif) == ATA_CMD_INIT_DEV_PARAMS)
				return ide_stopped;
		} else if ((err & BAD_CRC) == BAD_CRC) {
			/* UDMA crc error, just retry the operation */
			drive->crc_count++;
		} else if (err & (ATA_BBK | ATA_UNC)) {
			/* retries won't help these */
			scsi_req(rq)->result = ERROR_MAX;
		} else if (err & ATA_TRK0NF) {
			/* help it find track zero */
			scsi_req(rq)->result |= ERROR_RECAL;
		}
	}

	if ((stat & ATA_DRQ) && rq_data_dir(rq) == READ &&
	    (hwif->host_flags & IDE_HFLAG_ERROR_STOPS_FIFO) == 0) {
		int nsect = drive->mult_count ? drive->mult_count : 1;

		ide_pad_transfer(drive, READ, nsect * SECTOR_SIZE);
	}

	if (scsi_req(rq)->result >= ERROR_MAX || blk_noretry_request(rq)) {
		ide_kill_rq(drive, rq);
		return ide_stopped;
	}

	if (hwif->tp_ops->read_status(hwif) & (ATA_BUSY | ATA_DRQ))
		scsi_req(rq)->result |= ERROR_RESET;

	if ((scsi_req(rq)->result & ERROR_RESET) == ERROR_RESET) {
		++scsi_req(rq)->result;
		return ide_do_reset(drive);
	}

	if ((scsi_req(rq)->result & ERROR_RECAL) == ERROR_RECAL)
		drive->special_flags |= IDE_SFLAG_RECALIBRATE;

	++scsi_req(rq)->result;

	return ide_stopped;
}

static ide_startstop_t ide_atapi_error(ide_drive_t *drive, struct request *rq,
				       u8 stat, u8 err)
{
	ide_hwif_t *hwif = drive->hwif;

	if ((stat & ATA_BUSY) ||
	    ((stat & ATA_DF) && (drive->dev_flags & IDE_DFLAG_NOWERR) == 0)) {
		/* other bits are useless when BUSY */
		scsi_req(rq)->result |= ERROR_RESET;
	} else {
		/* add decoding error stuff */
	}

	if (hwif->tp_ops->read_status(hwif) & (ATA_BUSY | ATA_DRQ))
		/* force an abort */
		hwif->tp_ops->exec_command(hwif, ATA_CMD_IDLEIMMEDIATE);

	if (scsi_req(rq)->result >= ERROR_MAX) {
		ide_kill_rq(drive, rq);
	} else {
		if ((scsi_req(rq)->result & ERROR_RESET) == ERROR_RESET) {
			++scsi_req(rq)->result;
			return ide_do_reset(drive);
		}
		++scsi_req(rq)->result;
	}

	return ide_stopped;
}

static ide_startstop_t __ide_error(ide_drive_t *drive, struct request *rq,
				   u8 stat, u8 err)
{
	if (drive->media == ide_disk)
		return ide_ata_error(drive, rq, stat, err);
	return ide_atapi_error(drive, rq, stat, err);
}

/**
 *	ide_error	-	handle an error on the IDE
 *	@drive: drive the error occurred on
 *	@msg: message to report
 *	@stat: status bits
 *
 *	ide_error() takes action based on the error returned by the drive.
 *	For normal I/O that may well include retries. We deal with
 *	both new-style (taskfile) and old style command handling here.
 *	In the case of taskfile command handling there is work left to
 *	do
 */

ide_startstop_t ide_error(ide_drive_t *drive, const char *msg, u8 stat)
{
	struct request *rq;
	u8 err;

	err = ide_dump_status(drive, msg, stat);

	rq = drive->hwif->rq;
	if (rq == NULL)
		return ide_stopped;

	/* retry only "normal" I/O: */
	if (blk_rq_is_passthrough(rq)) {
		if (ata_taskfile_request(rq)) {
			struct ide_cmd *cmd = ide_req(rq)->special;

			if (cmd)
				ide_complete_cmd(drive, cmd, stat, err);
		} else if (ata_pm_request(rq)) {
			scsi_req(rq)->result = 1;
			ide_complete_pm_rq(drive, rq);
			return ide_stopped;
		}
		scsi_req(rq)->result = err;
		ide_complete_rq(drive, err ? BLK_STS_IOERR : BLK_STS_OK, blk_rq_bytes(rq));
		return ide_stopped;
	}

	return __ide_error(drive, rq, stat, err);
}
EXPORT_SYMBOL_GPL(ide_error);

static inline void ide_complete_drive_reset(ide_drive_t *drive, blk_status_t err)
{
	struct request *rq = drive->hwif->rq;

	if (rq && ata_misc_request(rq) &&
	    scsi_req(rq)->cmd[0] == REQ_DRIVE_RESET) {
		if (err <= 0 && scsi_req(rq)->result == 0)
			scsi_req(rq)->result = -EIO;
		ide_complete_rq(drive, err, blk_rq_bytes(rq));
	}
}

/* needed below */
static ide_startstop_t do_reset1(ide_drive_t *, int);

/*
 * atapi_reset_pollfunc() gets invoked to poll the interface for completion
 * every 50ms during an atapi drive reset operation.  If the drive has not yet
 * responded, and we have not yet hit our maximum waiting time, then the timer
 * is restarted for another 50ms.
 */
static ide_startstop_t atapi_reset_pollfunc(ide_drive_t *drive)
{
	ide_hwif_t *hwif = drive->hwif;
	const struct ide_tp_ops *tp_ops = hwif->tp_ops;
	u8 stat;

	tp_ops->dev_select(drive);
	udelay(10);
	stat = tp_ops->read_status(hwif);

	if (OK_STAT(stat, 0, ATA_BUSY))
		printk(KERN_INFO "%s: ATAPI reset complete\n", drive->name);
	else {
		if (time_before(jiffies, hwif->poll_timeout)) {
			ide_set_handler(drive, &atapi_reset_pollfunc, HZ/20);
			/* continue polling */
			return ide_started;
		}
		/* end of polling */
		hwif->polling = 0;
		printk(KERN_ERR "%s: ATAPI reset timed-out, status=0x%02x\n",
			drive->name, stat);
		/* do it the old fashioned way */
		return do_reset1(drive, 1);
	}
	/* done polling */
	hwif->polling = 0;
	ide_complete_drive_reset(drive, BLK_STS_OK);
	return ide_stopped;
}

static void ide_reset_report_error(ide_hwif_t *hwif, u8 err)
{
	static const char *err_master_vals[] =
		{ NULL, "passed", "formatter device error",
		  "sector buffer error", "ECC circuitry error",
		  "controlling MPU error" };

	u8 err_master = err & 0x7f;

	printk(KERN_ERR "%s: reset: master: ", hwif->name);
	if (err_master && err_master < 6)
		printk(KERN_CONT "%s", err_master_vals[err_master]);
	else
		printk(KERN_CONT "error (0x%02x?)", err);
	if (err & 0x80)
		printk(KERN_CONT "; slave: failed");
	printk(KERN_CONT "\n");
}

/*
 * reset_pollfunc() gets invoked to poll the interface for completion every 50ms
 * during an ide reset operation. If the drives have not yet responded,
 * and we have not yet hit our maximum waiting time, then the timer is restarted
 * for another 50ms.
 */
static ide_startstop_t reset_pollfunc(ide_drive_t *drive)
{
	ide_hwif_t *hwif = drive->hwif;
	const struct ide_port_ops *port_ops = hwif->port_ops;
	u8 tmp;
	blk_status_t err = BLK_STS_OK;

	if (port_ops && port_ops->reset_poll) {
		err = port_ops->reset_poll(drive);
		if (err) {
			printk(KERN_ERR "%s: host reset_poll failure for %s.\n",
				hwif->name, drive->name);
			goto out;
		}
	}

	tmp = hwif->tp_ops->read_status(hwif);

	if (!OK_STAT(tmp, 0, ATA_BUSY)) {
		if (time_before(jiffies, hwif->poll_timeout)) {
			ide_set_handler(drive, &reset_pollfunc, HZ/20);
			/* continue polling */
			return ide_started;
		}
		printk(KERN_ERR "%s: reset timed-out, status=0x%02x\n",
			hwif->name, tmp);
		drive->failures++;
		err = BLK_STS_IOERR;
	} else  {
		tmp = ide_read_error(drive);

		if (tmp == 1) {
			printk(KERN_INFO "%s: reset: success\n", hwif->name);
			drive->failures = 0;
		} else {
			ide_reset_report_error(hwif, tmp);
			drive->failures++;
			err = BLK_STS_IOERR;
		}
	}
out:
	hwif->polling = 0;	/* done polling */
	ide_complete_drive_reset(drive, err);
	return ide_stopped;
}

static void ide_disk_pre_reset(ide_drive_t *drive)
{
	int legacy = (drive->id[ATA_ID_CFS_ENABLE_2] & 0x0400) ? 0 : 1;

	drive->special_flags =
		legacy ? (IDE_SFLAG_SET_GEOMETRY | IDE_SFLAG_RECALIBRATE) : 0;

	drive->mult_count = 0;
	drive->dev_flags &= ~IDE_DFLAG_PARKED;

	if ((drive->dev_flags & IDE_DFLAG_KEEP_SETTINGS) == 0 &&
	    (drive->dev_flags & IDE_DFLAG_USING_DMA) == 0)
		drive->mult_req = 0;

	if (drive->mult_req != drive->mult_count)
		drive->special_flags |= IDE_SFLAG_SET_MULTMODE;
}

static void pre_reset(ide_drive_t *drive)
{
	const struct ide_port_ops *port_ops = drive->hwif->port_ops;

	if (drive->media == ide_disk)
		ide_disk_pre_reset(drive);
	else
		drive->dev_flags |= IDE_DFLAG_POST_RESET;

	if (drive->dev_flags & IDE_DFLAG_USING_DMA) {
		if (drive->crc_count)
			ide_check_dma_crc(drive);
		else
			ide_dma_off(drive);
	}

	if ((drive->dev_flags & IDE_DFLAG_KEEP_SETTINGS) == 0) {
		if ((drive->dev_flags & IDE_DFLAG_USING_DMA) == 0) {
			drive->dev_flags &= ~IDE_DFLAG_UNMASK;
			drive->io_32bit = 0;
		}
		return;
	}

	if (port_ops && port_ops->pre_reset)
		port_ops->pre_reset(drive);

	if (drive->current_speed != 0xff)
		drive->desired_speed = drive->current_speed;
	drive->current_speed = 0xff;
}

/*
 * do_reset1() attempts to recover a confused drive by resetting it.
 * Unfortunately, resetting a disk drive actually resets all devices on
 * the same interface, so it can really be thought of as resetting the
 * interface rather than resetting the drive.
 *
 * ATAPI devices have their own reset mechanism which allows them to be
 * individually reset without clobbering other devices on the same interface.
 *
 * Unfortunately, the IDE interface does not generate an interrupt to let
 * us know when the reset operation has finished, so we must poll for this.
 * Equally poor, though, is the fact that this may a very long time to complete,
 * (up to 30 seconds worstcase).  So, instead of busy-waiting here for it,
 * we set a timer to poll at 50ms intervals.
 */
static ide_startstop_t do_reset1(ide_drive_t *drive, int do_not_try_atapi)
{
	ide_hwif_t *hwif = drive->hwif;
	struct ide_io_ports *io_ports = &hwif->io_ports;
	const struct ide_tp_ops *tp_ops = hwif->tp_ops;
	const struct ide_port_ops *port_ops;
	ide_drive_t *tdrive;
	unsigned long flags, timeout;
	int i;
	DEFINE_WAIT(wait);

	spin_lock_irqsave(&hwif->lock, flags);

	/* We must not reset with running handlers */
	BUG_ON(hwif->handler != NULL);

	/* For an ATAPI device, first try an ATAPI SRST. */
	if (drive->media != ide_disk && !do_not_try_atapi) {
		pre_reset(drive);
		tp_ops->dev_select(drive);
		udelay(20);
		tp_ops->exec_command(hwif, ATA_CMD_DEV_RESET);
		ndelay(400);
		hwif->poll_timeout = jiffies + WAIT_WORSTCASE;
		hwif->polling = 1;
		__ide_set_handler(drive, &atapi_reset_pollfunc, HZ/20);
		spin_unlock_irqrestore(&hwif->lock, flags);
		return ide_started;
	}

	/* We must not disturb devices in the IDE_DFLAG_PARKED state. */
	do {
		unsigned long now;

		prepare_to_wait(&ide_park_wq, &wait, TASK_UNINTERRUPTIBLE);
		timeout = jiffies;
		ide_port_for_each_present_dev(i, tdrive, hwif) {
			if ((tdrive->dev_flags & IDE_DFLAG_PARKED) &&
			    time_after(tdrive->sleep, timeout))
				timeout = tdrive->sleep;
		}

		now = jiffies;
		if (time_before_eq(timeout, now))
			break;

		spin_unlock_irqrestore(&hwif->lock, flags);
		timeout = schedule_timeout_uninterruptible(timeout - now);
		spin_lock_irqsave(&hwif->lock, flags);
	} while (timeout);
	finish_wait(&ide_park_wq, &wait);

	/*
	 * First, reset any device state data we were maintaining
	 * for any of the drives on this interface.
	 */
	ide_port_for_each_dev(i, tdrive, hwif)
		pre_reset(tdrive);

	if (io_ports->ctl_addr == 0) {
		spin_unlock_irqrestore(&hwif->lock, flags);
		ide_complete_drive_reset(drive, BLK_STS_IOERR);
		return ide_stopped;
	}

	/*
	 * Note that we also set nIEN while resetting the device,
	 * to mask unwanted interrupts from the interface during the reset.
	 * However, due to the design of PC hardware, this will cause an
	 * immediate interrupt due to the edge transition it produces.
	 * This single interrupt gives us a "fast poll" for drives that
	 * recover from reset very quickly, saving us the first 50ms wait time.
	 */
	/* set SRST and nIEN */
	tp_ops->write_devctl(hwif, ATA_SRST | ATA_NIEN | ATA_DEVCTL_OBS);
	/* more than enough time */
	udelay(10);
	/* clear SRST, leave nIEN (unless device is on the quirk list) */
	tp_ops->write_devctl(hwif,
		((drive->dev_flags & IDE_DFLAG_NIEN_QUIRK) ? 0 : ATA_NIEN) |
		 ATA_DEVCTL_OBS);
	/* more than enough time */
	udelay(10);
	hwif->poll_timeout = jiffies + WAIT_WORSTCASE;
	hwif->polling = 1;
	__ide_set_handler(drive, &reset_pollfunc, HZ/20);

	/*
	 * Some weird controller like resetting themselves to a strange
	 * state when the disks are reset this way. At least, the Winbond
	 * 553 documentation says that
	 */
	port_ops = hwif->port_ops;
	if (port_ops && port_ops->resetproc)
		port_ops->resetproc(drive);

	spin_unlock_irqrestore(&hwif->lock, flags);
	return ide_started;
}

/*
 * ide_do_reset() is the entry point to the drive/interface reset code.
 */

ide_startstop_t ide_do_reset(ide_drive_t *drive)
{
	return do_reset1(drive, 0);
}
EXPORT_SYMBOL(ide_do_reset);
