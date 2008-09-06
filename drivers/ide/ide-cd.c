/*
 * ATAPI CD-ROM driver.
 *
 * Copyright (C) 1994-1996   Scott Snyder <snyder@fnald0.fnal.gov>
 * Copyright (C) 1996-1998   Erik Andersen <andersee@debian.org>
 * Copyright (C) 1998-2000   Jens Axboe <axboe@suse.de>
 * Copyright (C) 2005, 2007  Bartlomiej Zolnierkiewicz
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * See Documentation/cdrom/ide-cd for usage information.
 *
 * Suggestions are welcome. Patches that work are more welcome though. ;-)
 * For those wishing to work on this driver, please be sure you download
 * and comply with the latest Mt. Fuji (SFF8090 version 4) and ATAPI
 * (SFF-8020i rev 2.6) standards. These documents can be obtained by
 * anonymous ftp from:
 * ftp://fission.dt.wdc.com/pub/standards/SFF_atapi/spec/SFF8020-r2.6/PS/8020r26.ps
 * ftp://ftp.avc-pioneer.com/Mtfuji4/Spec/Fuji4r10.pdf
 *
 * For historical changelog please see:
 *	Documentation/ide/ChangeLog.ide-cd.1994-2004
 */

#define IDECD_VERSION "5.00"

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/cdrom.h>
#include <linux/ide.h>
#include <linux/completion.h>
#include <linux/mutex.h>
#include <linux/bcd.h>

/* For SCSI -> ATAPI command conversion */
#include <scsi/scsi.h>

#include <linux/irq.h>
#include <linux/io.h>
#include <asm/byteorder.h>
#include <linux/uaccess.h>
#include <asm/unaligned.h>

#include "ide-cd.h"

static DEFINE_MUTEX(idecd_ref_mutex);

#define to_ide_cd(obj) container_of(obj, struct cdrom_info, kref)

#define ide_cd_g(disk) \
	container_of((disk)->private_data, struct cdrom_info, driver)

static void ide_cd_release(struct kref *);

static struct cdrom_info *ide_cd_get(struct gendisk *disk)
{
	struct cdrom_info *cd = NULL;

	mutex_lock(&idecd_ref_mutex);
	cd = ide_cd_g(disk);
	if (cd) {
		if (ide_device_get(cd->drive))
			cd = NULL;
		else
			kref_get(&cd->kref);

	}
	mutex_unlock(&idecd_ref_mutex);
	return cd;
}

static void ide_cd_put(struct cdrom_info *cd)
{
	ide_drive_t *drive = cd->drive;

	mutex_lock(&idecd_ref_mutex);
	kref_put(&cd->kref, ide_cd_release);
	ide_device_put(drive);
	mutex_unlock(&idecd_ref_mutex);
}

/*
 * Generic packet command support and error handling routines.
 */

/* Mark that we've seen a media change and invalidate our internal buffers. */
static void cdrom_saw_media_change(ide_drive_t *drive)
{
	drive->atapi_flags |= IDE_AFLAG_MEDIA_CHANGED;
	drive->atapi_flags &= ~IDE_AFLAG_TOC_VALID;
}

static int cdrom_log_sense(ide_drive_t *drive, struct request *rq,
			   struct request_sense *sense)
{
	int log = 0;

	if (!sense || !rq || (rq->cmd_flags & REQ_QUIET))
		return 0;

	switch (sense->sense_key) {
	case NO_SENSE:
	case RECOVERED_ERROR:
		break;
	case NOT_READY:
		/*
		 * don't care about tray state messages for e.g. capacity
		 * commands or in-progress or becoming ready
		 */
		if (sense->asc == 0x3a || sense->asc == 0x04)
			break;
		log = 1;
		break;
	case ILLEGAL_REQUEST:
		/*
		 * don't log START_STOP unit with LoEj set, since we cannot
		 * reliably check if drive can auto-close
		 */
		if (rq->cmd[0] == GPCMD_START_STOP_UNIT && sense->asc == 0x24)
			break;
		log = 1;
		break;
	case UNIT_ATTENTION:
		/*
		 * Make good and sure we've seen this potential media change.
		 * Some drives (i.e. Creative) fail to present the correct sense
		 * key in the error register.
		 */
		cdrom_saw_media_change(drive);
		break;
	default:
		log = 1;
		break;
	}
	return log;
}

static void cdrom_analyze_sense_data(ide_drive_t *drive,
			      struct request *failed_command,
			      struct request_sense *sense)
{
	unsigned long sector;
	unsigned long bio_sectors;
	struct cdrom_info *info = drive->driver_data;

	if (!cdrom_log_sense(drive, failed_command, sense))
		return;

	/*
	 * If a read toc is executed for a CD-R or CD-RW medium where the first
	 * toc has not been recorded yet, it will fail with 05/24/00 (which is a
	 * confusing error)
	 */
	if (failed_command && failed_command->cmd[0] == GPCMD_READ_TOC_PMA_ATIP)
		if (sense->sense_key == 0x05 && sense->asc == 0x24)
			return;

	/* current error */
	if (sense->error_code == 0x70) {
		switch (sense->sense_key) {
		case MEDIUM_ERROR:
		case VOLUME_OVERFLOW:
		case ILLEGAL_REQUEST:
			if (!sense->valid)
				break;
			if (failed_command == NULL ||
					!blk_fs_request(failed_command))
				break;
			sector = (sense->information[0] << 24) |
				 (sense->information[1] << 16) |
				 (sense->information[2] <<  8) |
				 (sense->information[3]);

			if (drive->queue->hardsect_size == 2048)
				/* device sector size is 2K */
				sector <<= 2;

			bio_sectors = max(bio_sectors(failed_command->bio), 4U);
			sector &= ~(bio_sectors - 1);

			if (sector < get_capacity(info->disk) &&
			    drive->probed_capacity - sector < 4 * 75)
				set_capacity(info->disk, sector);
		}
	}

	ide_cd_log_error(drive->name, failed_command, sense);
}

static void cdrom_queue_request_sense(ide_drive_t *drive, void *sense,
				      struct request *failed_command)
{
	struct cdrom_info *info		= drive->driver_data;
	struct request *rq		= &info->request_sense_request;

	if (sense == NULL)
		sense = &info->sense_data;

	/* stuff the sense request in front of our current request */
	blk_rq_init(NULL, rq);
	rq->cmd_type = REQ_TYPE_ATA_PC;
	rq->rq_disk = info->disk;

	rq->data = sense;
	rq->cmd[0] = GPCMD_REQUEST_SENSE;
	rq->cmd[4] = 18;
	rq->data_len = 18;

	rq->cmd_type = REQ_TYPE_SENSE;
	rq->cmd_flags |= REQ_PREEMPT;

	/* NOTE! Save the failed command in "rq->buffer" */
	rq->buffer = (void *) failed_command;

	ide_do_drive_cmd(drive, rq);
}

static void cdrom_end_request(ide_drive_t *drive, int uptodate)
{
	struct request *rq = HWGROUP(drive)->rq;
	int nsectors = rq->hard_cur_sectors;

	if (blk_sense_request(rq) && uptodate) {
		/*
		 * For REQ_TYPE_SENSE, "rq->buffer" points to the original
		 * failed request
		 */
		struct request *failed = (struct request *) rq->buffer;
		struct cdrom_info *info = drive->driver_data;
		void *sense = &info->sense_data;
		unsigned long flags;

		if (failed) {
			if (failed->sense) {
				sense = failed->sense;
				failed->sense_len = rq->sense_len;
			}
			cdrom_analyze_sense_data(drive, failed, sense);
			/*
			 * now end the failed request
			 */
			if (blk_fs_request(failed)) {
				if (ide_end_dequeued_request(drive, failed, 0,
						failed->hard_nr_sectors))
					BUG();
			} else {
				spin_lock_irqsave(&ide_lock, flags);
				if (__blk_end_request(failed, -EIO,
						      failed->data_len))
					BUG();
				spin_unlock_irqrestore(&ide_lock, flags);
			}
		} else
			cdrom_analyze_sense_data(drive, NULL, sense);
	}

	if (!rq->current_nr_sectors && blk_fs_request(rq))
		uptodate = 1;
	/* make sure it's fully ended */
	if (blk_pc_request(rq))
		nsectors = (rq->data_len + 511) >> 9;
	if (!nsectors)
		nsectors = 1;

	ide_end_request(drive, uptodate, nsectors);
}

static void ide_dump_status_no_sense(ide_drive_t *drive, const char *msg, u8 st)
{
	if (st & 0x80)
		return;
	ide_dump_status(drive, msg, st);
}

/*
 * Returns:
 * 0: if the request should be continued.
 * 1: if the request was ended.
 */
static int cdrom_decode_status(ide_drive_t *drive, int good_stat, int *stat_ret)
{
	ide_hwif_t *hwif = drive->hwif;
	struct request *rq = hwif->hwgroup->rq;
	int stat, err, sense_key;

	/* check for errors */
	stat = hwif->tp_ops->read_status(hwif);

	if (stat_ret)
		*stat_ret = stat;

	if (OK_STAT(stat, good_stat, BAD_R_STAT))
		return 0;

	/* get the IDE error register */
	err = ide_read_error(drive);
	sense_key = err >> 4;

	if (rq == NULL) {
		printk(KERN_ERR "%s: missing rq in %s\n",
				drive->name, __func__);
		return 1;
	}

	if (blk_sense_request(rq)) {
		/*
		 * We got an error trying to get sense info from the drive
		 * (probably while trying to recover from a former error).
		 * Just give up.
		 */
		rq->cmd_flags |= REQ_FAILED;
		cdrom_end_request(drive, 0);
		ide_error(drive, "request sense failure", stat);
		return 1;

	} else if (blk_pc_request(rq) || rq->cmd_type == REQ_TYPE_ATA_PC) {
		/* All other functions, except for READ. */

		/*
		 * if we have an error, pass back CHECK_CONDITION as the
		 * scsi status byte
		 */
		if (blk_pc_request(rq) && !rq->errors)
			rq->errors = SAM_STAT_CHECK_CONDITION;

		/* check for tray open */
		if (sense_key == NOT_READY) {
			cdrom_saw_media_change(drive);
		} else if (sense_key == UNIT_ATTENTION) {
			/* check for media change */
			cdrom_saw_media_change(drive);
			return 0;
		} else if (sense_key == ILLEGAL_REQUEST &&
			   rq->cmd[0] == GPCMD_START_STOP_UNIT) {
			/*
			 * Don't print error message for this condition--
			 * SFF8090i indicates that 5/24/00 is the correct
			 * response to a request to close the tray if the
			 * drive doesn't have that capability.
			 * cdrom_log_sense() knows this!
			 */
		} else if (!(rq->cmd_flags & REQ_QUIET)) {
			/* otherwise, print an error */
			ide_dump_status(drive, "packet command error", stat);
		}

		rq->cmd_flags |= REQ_FAILED;

		/*
		 * instead of playing games with moving completions around,
		 * remove failed request completely and end it when the
		 * request sense has completed
		 */
		goto end_request;

	} else if (blk_fs_request(rq)) {
		int do_end_request = 0;

		/* handle errors from READ and WRITE requests */

		if (blk_noretry_request(rq))
			do_end_request = 1;

		if (sense_key == NOT_READY) {
			/* tray open */
			if (rq_data_dir(rq) == READ) {
				cdrom_saw_media_change(drive);

				/* fail the request */
				printk(KERN_ERR "%s: tray open\n", drive->name);
				do_end_request = 1;
			} else {
				struct cdrom_info *info = drive->driver_data;

				/*
				 * Allow the drive 5 seconds to recover, some
				 * devices will return this error while flushing
				 * data from cache.
				 */
				if (!rq->errors)
					info->write_timeout = jiffies +
							ATAPI_WAIT_WRITE_BUSY;
				rq->errors = 1;
				if (time_after(jiffies, info->write_timeout))
					do_end_request = 1;
				else {
					unsigned long flags;

					/*
					 * take a breather relying on the unplug
					 * timer to kick us again
					 */
					spin_lock_irqsave(&ide_lock, flags);
					blk_plug_device(drive->queue);
					spin_unlock_irqrestore(&ide_lock,
								flags);
					return 1;
				}
			}
		} else if (sense_key == UNIT_ATTENTION) {
			/* media change */
			cdrom_saw_media_change(drive);

			/*
			 * Arrange to retry the request but be sure to give up
			 * if we've retried too many times.
			 */
			if (++rq->errors > ERROR_MAX)
				do_end_request = 1;
		} else if (sense_key == ILLEGAL_REQUEST ||
			   sense_key == DATA_PROTECT) {
			/*
			 * No point in retrying after an illegal request or data
			 * protect error.
			 */
			ide_dump_status_no_sense(drive, "command error", stat);
			do_end_request = 1;
		} else if (sense_key == MEDIUM_ERROR) {
			/*
			 * No point in re-trying a zillion times on a bad
			 * sector. If we got here the error is not correctable.
			 */
			ide_dump_status_no_sense(drive,
						 "media error (bad sector)",
						 stat);
			do_end_request = 1;
		} else if (sense_key == BLANK_CHECK) {
			/* disk appears blank ?? */
			ide_dump_status_no_sense(drive, "media error (blank)",
						 stat);
			do_end_request = 1;
		} else if ((err & ~ABRT_ERR) != 0) {
			/* go to the default handler for other errors */
			ide_error(drive, "cdrom_decode_status", stat);
			return 1;
		} else if ((++rq->errors > ERROR_MAX)) {
			/* we've racked up too many retries, abort */
			do_end_request = 1;
		}

		/*
		 * End a request through request sense analysis when we have
		 * sense data. We need this in order to perform end of media
		 * processing.
		 */
		if (do_end_request)
			goto end_request;

		/*
		 * If we got a CHECK_CONDITION status, queue
		 * a request sense command.
		 */
		if (stat & ERR_STAT)
			cdrom_queue_request_sense(drive, NULL, NULL);
	} else {
		blk_dump_rq_flags(rq, "ide-cd: bad rq");
		cdrom_end_request(drive, 0);
	}

	/* retry, or handle the next request */
	return 1;

end_request:
	if (stat & ERR_STAT) {
		unsigned long flags;

		spin_lock_irqsave(&ide_lock, flags);
		blkdev_dequeue_request(rq);
		HWGROUP(drive)->rq = NULL;
		spin_unlock_irqrestore(&ide_lock, flags);

		cdrom_queue_request_sense(drive, rq->sense, rq);
	} else
		cdrom_end_request(drive, 0);

	return 1;
}

static int cdrom_timer_expiry(ide_drive_t *drive)
{
	struct request *rq = HWGROUP(drive)->rq;
	unsigned long wait = 0;

	/*
	 * Some commands are *slow* and normally take a long time to complete.
	 * Usually we can use the ATAPI "disconnect" to bypass this, but not all
	 * commands/drives support that. Let ide_timer_expiry keep polling us
	 * for these.
	 */
	switch (rq->cmd[0]) {
	case GPCMD_BLANK:
	case GPCMD_FORMAT_UNIT:
	case GPCMD_RESERVE_RZONE_TRACK:
	case GPCMD_CLOSE_TRACK:
	case GPCMD_FLUSH_CACHE:
		wait = ATAPI_WAIT_PC;
		break;
	default:
		if (!(rq->cmd_flags & REQ_QUIET))
			printk(KERN_INFO "ide-cd: cmd 0x%x timed out\n",
					 rq->cmd[0]);
		wait = 0;
		break;
	}
	return wait;
}

/*
 * Set up the device registers for transferring a packet command on DEV,
 * expecting to later transfer XFERLEN bytes.  HANDLER is the routine
 * which actually transfers the command to the drive.  If this is a
 * drq_interrupt device, this routine will arrange for HANDLER to be
 * called when the interrupt from the drive arrives.  Otherwise, HANDLER
 * will be called immediately after the drive is prepared for the transfer.
 */
static ide_startstop_t cdrom_start_packet_command(ide_drive_t *drive,
						  int xferlen,
						  ide_handler_t *handler)
{
	struct cdrom_info *info = drive->driver_data;
	ide_hwif_t *hwif = drive->hwif;

	/* FIXME: for Virtual DMA we must check harder */
	if (info->dma)
		info->dma = !hwif->dma_ops->dma_setup(drive);

	/* set up the controller registers */
	ide_pktcmd_tf_load(drive, IDE_TFLAG_OUT_NSECT | IDE_TFLAG_OUT_LBAL,
			   xferlen, info->dma);

	if (drive->atapi_flags & IDE_AFLAG_DRQ_INTERRUPT) {
		/* waiting for CDB interrupt, not DMA yet. */
		if (info->dma)
			drive->waiting_for_dma = 0;

		/* packet command */
		ide_execute_command(drive, WIN_PACKETCMD, handler,
				    ATAPI_WAIT_PC, cdrom_timer_expiry);
		return ide_started;
	} else {
		ide_execute_pkt_cmd(drive);

		return (*handler) (drive);
	}
}

/*
 * Send a packet command to DRIVE described by CMD_BUF and CMD_LEN. The device
 * registers must have already been prepared by cdrom_start_packet_command.
 * HANDLER is the interrupt handler to call when the command completes or
 * there's data ready.
 */
#define ATAPI_MIN_CDB_BYTES 12
static ide_startstop_t cdrom_transfer_packet_command(ide_drive_t *drive,
					  struct request *rq,
					  ide_handler_t *handler)
{
	ide_hwif_t *hwif = drive->hwif;
	int cmd_len;
	struct cdrom_info *info = drive->driver_data;
	ide_startstop_t startstop;

	if (drive->atapi_flags & IDE_AFLAG_DRQ_INTERRUPT) {
		/*
		 * Here we should have been called after receiving an interrupt
		 * from the device.  DRQ should how be set.
		 */

		/* check for errors */
		if (cdrom_decode_status(drive, DRQ_STAT, NULL))
			return ide_stopped;

		/* ok, next interrupt will be DMA interrupt */
		if (info->dma)
			drive->waiting_for_dma = 1;
	} else {
		/* otherwise, we must wait for DRQ to get set */
		if (ide_wait_stat(&startstop, drive, DRQ_STAT,
				BUSY_STAT, WAIT_READY))
			return startstop;
	}

	/* arm the interrupt handler */
	ide_set_handler(drive, handler, rq->timeout, cdrom_timer_expiry);

	/* ATAPI commands get padded out to 12 bytes minimum */
	cmd_len = COMMAND_SIZE(rq->cmd[0]);
	if (cmd_len < ATAPI_MIN_CDB_BYTES)
		cmd_len = ATAPI_MIN_CDB_BYTES;

	/* send the command to the device */
	hwif->tp_ops->output_data(drive, NULL, rq->cmd, cmd_len);

	/* start the DMA if need be */
	if (info->dma)
		hwif->dma_ops->dma_start(drive);

	return ide_started;
}

/*
 * Check the contents of the interrupt reason register from the cdrom
 * and attempt to recover if there are problems.  Returns  0 if everything's
 * ok; nonzero if the request has been terminated.
 */
static int ide_cd_check_ireason(ide_drive_t *drive, struct request *rq,
				int len, int ireason, int rw)
{
	ide_hwif_t *hwif = drive->hwif;

	/*
	 * ireason == 0: the drive wants to receive data from us
	 * ireason == 2: the drive is expecting to transfer data to us
	 */
	if (ireason == (!rw << 1))
		return 0;
	else if (ireason == (rw << 1)) {

		/* whoops... */
		printk(KERN_ERR "%s: %s: wrong transfer direction!\n",
				drive->name, __func__);

		ide_pad_transfer(drive, rw, len);
	} else  if (rw == 0 && ireason == 1) {
		/*
		 * Some drives (ASUS) seem to tell us that status info is
		 * available.  Just get it and ignore.
		 */
		(void)hwif->tp_ops->read_status(hwif);
		return 0;
	} else {
		/* drive wants a command packet, or invalid ireason... */
		printk(KERN_ERR "%s: %s: bad interrupt reason 0x%02x\n",
				drive->name, __func__, ireason);
	}

	if (rq->cmd_type == REQ_TYPE_ATA_PC)
		rq->cmd_flags |= REQ_FAILED;

	cdrom_end_request(drive, 0);
	return -1;
}

/*
 * Assume that the drive will always provide data in multiples of at least
 * SECTOR_SIZE, as it gets hairy to keep track of the transfers otherwise.
 */
static int ide_cd_check_transfer_size(ide_drive_t *drive, int len)
{
	if ((len % SECTOR_SIZE) == 0)
		return 0;

	printk(KERN_ERR "%s: %s: Bad transfer size %d\n",
			drive->name, __func__, len);

	if (drive->atapi_flags & IDE_AFLAG_LIMIT_NFRAMES)
		printk(KERN_ERR "  This drive is not supported by "
				"this version of the driver\n");
	else {
		printk(KERN_ERR "  Trying to limit transfer sizes\n");
		drive->atapi_flags |= IDE_AFLAG_LIMIT_NFRAMES;
	}

	return 1;
}

static ide_startstop_t cdrom_newpc_intr(ide_drive_t *);

static ide_startstop_t ide_cd_prepare_rw_request(ide_drive_t *drive,
						 struct request *rq)
{
	if (rq_data_dir(rq) == READ) {
		unsigned short sectors_per_frame =
			queue_hardsect_size(drive->queue) >> SECTOR_BITS;
		int nskip = rq->sector & (sectors_per_frame - 1);

		/*
		 * If the requested sector doesn't start on a frame boundary,
		 * we must adjust the start of the transfer so that it does,
		 * and remember to skip the first few sectors.
		 *
		 * If the rq->current_nr_sectors field is larger than the size
		 * of the buffer, it will mean that we're to skip a number of
		 * sectors equal to the amount by which rq->current_nr_sectors
		 * is larger than the buffer size.
		 */
		if (nskip > 0) {
			/* sanity check... */
			if (rq->current_nr_sectors !=
			    bio_cur_sectors(rq->bio)) {
				printk(KERN_ERR "%s: %s: buffer botch (%u)\n",
						drive->name, __func__,
						rq->current_nr_sectors);
				cdrom_end_request(drive, 0);
				return ide_stopped;
			}
			rq->current_nr_sectors += nskip;
		}
	}
#if 0
	else
		/* the immediate bit */
		rq->cmd[1] = 1 << 3;
#endif
	/* set up the command */
	rq->timeout = ATAPI_WAIT_PC;

	return ide_started;
}

/*
 * Routine to send a read/write packet command to the drive. This is usually
 * called directly from cdrom_start_{read,write}(). However, for drq_interrupt
 * devices, it is called from an interrupt when the drive is ready to accept
 * the command.
 */
static ide_startstop_t cdrom_start_rw_cont(ide_drive_t *drive)
{
	struct request *rq = drive->hwif->hwgroup->rq;

	/* send the command to the drive and return */
	return cdrom_transfer_packet_command(drive, rq, cdrom_newpc_intr);
}

#define IDECD_SEEK_THRESHOLD	(1000)			/* 1000 blocks */
#define IDECD_SEEK_TIMER	(5 * WAIT_MIN_SLEEP)	/* 100 ms */
#define IDECD_SEEK_TIMEOUT	(2 * WAIT_CMD)		/* 20 sec */

static ide_startstop_t cdrom_seek_intr(ide_drive_t *drive)
{
	struct cdrom_info *info = drive->driver_data;
	int stat;
	static int retry = 10;

	if (cdrom_decode_status(drive, 0, &stat))
		return ide_stopped;

	drive->atapi_flags |= IDE_AFLAG_SEEKING;

	if (retry && time_after(jiffies, info->start_seek + IDECD_SEEK_TIMER)) {
		if (--retry == 0)
			drive->dsc_overlap = 0;
	}
	return ide_stopped;
}

static void ide_cd_prepare_seek_request(ide_drive_t *drive, struct request *rq)
{
	sector_t frame = rq->sector;

	sector_div(frame, queue_hardsect_size(drive->queue) >> SECTOR_BITS);

	memset(rq->cmd, 0, BLK_MAX_CDB);
	rq->cmd[0] = GPCMD_SEEK;
	put_unaligned(cpu_to_be32(frame), (unsigned int *) &rq->cmd[2]);

	rq->timeout = ATAPI_WAIT_PC;
}

static ide_startstop_t cdrom_start_seek_continuation(ide_drive_t *drive)
{
	struct request *rq = drive->hwif->hwgroup->rq;

	return cdrom_transfer_packet_command(drive, rq, &cdrom_seek_intr);
}

/*
 * Fix up a possibly partially-processed request so that we can start it over
 * entirely, or even put it back on the request queue.
 */
static void restore_request(struct request *rq)
{
	if (rq->buffer != bio_data(rq->bio)) {
		sector_t n =
			(rq->buffer - (char *)bio_data(rq->bio)) / SECTOR_SIZE;

		rq->buffer = bio_data(rq->bio);
		rq->nr_sectors += n;
		rq->sector -= n;
	}
	rq->current_nr_sectors = bio_cur_sectors(rq->bio);
	rq->hard_cur_sectors = rq->current_nr_sectors;
	rq->hard_nr_sectors = rq->nr_sectors;
	rq->hard_sector = rq->sector;
	rq->q->prep_rq_fn(rq->q, rq);
}

/*
 * All other packet commands.
 */
static void ide_cd_request_sense_fixup(struct request *rq)
{
	/*
	 * Some of the trailing request sense fields are optional,
	 * and some drives don't send them.  Sigh.
	 */
	if (rq->cmd[0] == GPCMD_REQUEST_SENSE &&
	    rq->data_len > 0 && rq->data_len <= 5)
		while (rq->data_len > 0) {
			*(u8 *)rq->data++ = 0;
			--rq->data_len;
		}
}

int ide_cd_queue_pc(ide_drive_t *drive, const unsigned char *cmd,
		    int write, void *buffer, unsigned *bufflen,
		    struct request_sense *sense, int timeout,
		    unsigned int cmd_flags)
{
	struct cdrom_info *info = drive->driver_data;
	struct request_sense local_sense;
	int retries = 10;
	unsigned int flags = 0;

	if (!sense)
		sense = &local_sense;

	/* start of retry loop */
	do {
		struct request *rq;
		int error;

		rq = blk_get_request(drive->queue, write, __GFP_WAIT);

		memcpy(rq->cmd, cmd, BLK_MAX_CDB);
		rq->cmd_type = REQ_TYPE_ATA_PC;
		rq->sense = sense;
		rq->cmd_flags |= cmd_flags;
		rq->timeout = timeout;
		if (buffer) {
			rq->data = buffer;
			rq->data_len = *bufflen;
		}

		error = blk_execute_rq(drive->queue, info->disk, rq, 0);

		if (buffer)
			*bufflen = rq->data_len;

		flags = rq->cmd_flags;
		blk_put_request(rq);

		/*
		 * FIXME: we should probably abort/retry or something in case of
		 * failure.
		 */
		if (flags & REQ_FAILED) {
			/*
			 * The request failed.  Retry if it was due to a unit
			 * attention status (usually means media was changed).
			 */
			struct request_sense *reqbuf = sense;

			if (reqbuf->sense_key == UNIT_ATTENTION)
				cdrom_saw_media_change(drive);
			else if (reqbuf->sense_key == NOT_READY &&
				 reqbuf->asc == 4 && reqbuf->ascq != 4) {
				/*
				 * The drive is in the process of loading
				 * a disk.  Retry, but wait a little to give
				 * the drive time to complete the load.
				 */
				ssleep(2);
			} else {
				/* otherwise, don't retry */
				retries = 0;
			}
			--retries;
		}

		/* end of retry loop */
	} while ((flags & REQ_FAILED) && retries >= 0);

	/* return an error if the command failed */
	return (flags & REQ_FAILED) ? -EIO : 0;
}

/*
 * Called from blk_end_request_callback() after the data of the request is
 * completed and before the request itself is completed. By returning value '1',
 * blk_end_request_callback() returns immediately without completing it.
 */
static int cdrom_newpc_intr_dummy_cb(struct request *rq)
{
	return 1;
}

static ide_startstop_t cdrom_newpc_intr(ide_drive_t *drive)
{
	ide_hwif_t *hwif = drive->hwif;
	struct cdrom_info *info = drive->driver_data;
	struct request *rq = HWGROUP(drive)->rq;
	xfer_func_t *xferfunc;
	ide_expiry_t *expiry = NULL;
	int dma_error = 0, dma, stat, thislen, uptodate = 0;
	int write = (rq_data_dir(rq) == WRITE) ? 1 : 0;
	unsigned int timeout;
	u16 len;
	u8 ireason;

	/* check for errors */
	dma = info->dma;
	if (dma) {
		info->dma = 0;
		dma_error = hwif->dma_ops->dma_end(drive);
		if (dma_error) {
			printk(KERN_ERR "%s: DMA %s error\n", drive->name,
					write ? "write" : "read");
			ide_dma_off(drive);
		}
	}

	if (cdrom_decode_status(drive, 0, &stat))
		return ide_stopped;

	/* using dma, transfer is complete now */
	if (dma) {
		if (dma_error)
			return ide_error(drive, "dma error", stat);
		if (blk_fs_request(rq)) {
			ide_end_request(drive, 1, rq->nr_sectors);
			return ide_stopped;
		}
		goto end_request;
	}

	ide_read_bcount_and_ireason(drive, &len, &ireason);

	thislen = blk_fs_request(rq) ? len : rq->data_len;
	if (thislen > len)
		thislen = len;

	/* If DRQ is clear, the command has completed. */
	if ((stat & DRQ_STAT) == 0) {
		if (blk_fs_request(rq)) {
			/*
			 * If we're not done reading/writing, complain.
			 * Otherwise, complete the command normally.
			 */
			uptodate = 1;
			if (rq->current_nr_sectors > 0) {
				printk(KERN_ERR "%s: %s: data underrun "
						"(%d blocks)\n",
						drive->name, __func__,
						rq->current_nr_sectors);
				if (!write)
					rq->cmd_flags |= REQ_FAILED;
				uptodate = 0;
			}
			cdrom_end_request(drive, uptodate);
			return ide_stopped;
		} else if (!blk_pc_request(rq)) {
			ide_cd_request_sense_fixup(rq);
			/* complain if we still have data left to transfer */
			uptodate = rq->data_len ? 0 : 1;
		}
		goto end_request;
	}

	/* check which way to transfer data */
	if (ide_cd_check_ireason(drive, rq, len, ireason, write))
		return ide_stopped;

	if (blk_fs_request(rq)) {
		if (write == 0) {
			int nskip;

			if (ide_cd_check_transfer_size(drive, len)) {
				cdrom_end_request(drive, 0);
				return ide_stopped;
			}

			/*
			 * First, figure out if we need to bit-bucket
			 * any of the leading sectors.
			 */
			nskip = min_t(int, rq->current_nr_sectors
					   - bio_cur_sectors(rq->bio),
					   thislen >> 9);
			if (nskip > 0) {
				ide_pad_transfer(drive, write, nskip << 9);
				rq->current_nr_sectors -= nskip;
				thislen -= (nskip << 9);
			}
		}
	}

	if (ireason == 0) {
		write = 1;
		xferfunc = hwif->tp_ops->output_data;
	} else {
		write = 0;
		xferfunc = hwif->tp_ops->input_data;
	}

	/* transfer data */
	while (thislen > 0) {
		u8 *ptr = blk_fs_request(rq) ? NULL : rq->data;
		int blen = rq->data_len;

		/* bio backed? */
		if (rq->bio) {
			if (blk_fs_request(rq)) {
				ptr = rq->buffer;
				blen = rq->current_nr_sectors << 9;
			} else {
				ptr = bio_data(rq->bio);
				blen = bio_iovec(rq->bio)->bv_len;
			}
		}

		if (!ptr) {
			if (blk_fs_request(rq) && !write)
				/*
				 * If the buffers are full, pipe the rest into
				 * oblivion.
				 */
				ide_pad_transfer(drive, 0, thislen);
			else {
				printk(KERN_ERR "%s: confused, missing data\n",
						drive->name);
				blk_dump_rq_flags(rq, rq_data_dir(rq)
						  ? "cdrom_newpc_intr, write"
						  : "cdrom_newpc_intr, read");
			}
			break;
		}

		if (blen > thislen)
			blen = thislen;

		xferfunc(drive, NULL, ptr, blen);

		thislen -= blen;
		len -= blen;

		if (blk_fs_request(rq)) {
			rq->buffer += blen;
			rq->nr_sectors -= (blen >> 9);
			rq->current_nr_sectors -= (blen >> 9);
			rq->sector += (blen >> 9);

			if (rq->current_nr_sectors == 0 && rq->nr_sectors)
				cdrom_end_request(drive, 1);
		} else {
			rq->data_len -= blen;

			/*
			 * The request can't be completed until DRQ is cleared.
			 * So complete the data, but don't complete the request
			 * using the dummy function for the callback feature
			 * of blk_end_request_callback().
			 */
			if (rq->bio)
				blk_end_request_callback(rq, 0, blen,
						 cdrom_newpc_intr_dummy_cb);
			else
				rq->data += blen;
		}
		if (!write && blk_sense_request(rq))
			rq->sense_len += blen;
	}

	/* pad, if necessary */
	if (!blk_fs_request(rq) && len > 0)
		ide_pad_transfer(drive, write, len);

	if (blk_pc_request(rq)) {
		timeout = rq->timeout;
	} else {
		timeout = ATAPI_WAIT_PC;
		if (!blk_fs_request(rq))
			expiry = cdrom_timer_expiry;
	}

	ide_set_handler(drive, cdrom_newpc_intr, timeout, expiry);
	return ide_started;

end_request:
	if (blk_pc_request(rq)) {
		unsigned long flags;
		unsigned int dlen = rq->data_len;

		if (dma)
			rq->data_len = 0;

		spin_lock_irqsave(&ide_lock, flags);
		if (__blk_end_request(rq, 0, dlen))
			BUG();
		HWGROUP(drive)->rq = NULL;
		spin_unlock_irqrestore(&ide_lock, flags);
	} else {
		if (!uptodate)
			rq->cmd_flags |= REQ_FAILED;
		cdrom_end_request(drive, uptodate);
	}
	return ide_stopped;
}

static ide_startstop_t cdrom_start_rw(ide_drive_t *drive, struct request *rq)
{
	struct cdrom_info *cd = drive->driver_data;
	int write = rq_data_dir(rq) == WRITE;
	unsigned short sectors_per_frame =
		queue_hardsect_size(drive->queue) >> SECTOR_BITS;

	if (write) {
		/* disk has become write protected */
		if (cd->disk->policy) {
			cdrom_end_request(drive, 0);
			return ide_stopped;
		}
	} else {
		/*
		 * We may be retrying this request after an error.  Fix up any
		 * weirdness which might be present in the request packet.
		 */
		restore_request(rq);
	}

	/* use DMA, if possible / writes *must* be hardware frame aligned */
	if ((rq->nr_sectors & (sectors_per_frame - 1)) ||
	    (rq->sector & (sectors_per_frame - 1))) {
		if (write) {
			cdrom_end_request(drive, 0);
			return ide_stopped;
		}
		cd->dma = 0;
	} else
		cd->dma = drive->using_dma;

	if (write)
		cd->devinfo.media_written = 1;

	return ide_started;
}

static ide_startstop_t cdrom_do_newpc_cont(ide_drive_t *drive)
{
	struct request *rq = HWGROUP(drive)->rq;

	return cdrom_transfer_packet_command(drive, rq, cdrom_newpc_intr);
}

static void cdrom_do_block_pc(ide_drive_t *drive, struct request *rq)
{
	struct cdrom_info *info = drive->driver_data;

	if (blk_pc_request(rq))
		rq->cmd_flags |= REQ_QUIET;
	else
		rq->cmd_flags &= ~REQ_FAILED;

	info->dma = 0;

	/* sg request */
	if (rq->bio || ((rq->cmd_type == REQ_TYPE_ATA_PC) && rq->data_len)) {
		struct request_queue *q = drive->queue;
		unsigned int alignment;
		unsigned long addr;
		unsigned long stack_mask = ~(THREAD_SIZE - 1);

		if (rq->bio)
			addr = (unsigned long)bio_data(rq->bio);
		else
			addr = (unsigned long)rq->data;

		info->dma = drive->using_dma;

		/*
		 * check if dma is safe
		 *
		 * NOTE! The "len" and "addr" checks should possibly have
		 * separate masks.
		 */
		alignment = queue_dma_alignment(q) | q->dma_pad_mask;
		if (addr & alignment || rq->data_len & alignment)
			info->dma = 0;

		if (!((addr & stack_mask) ^
		      ((unsigned long)current->stack & stack_mask)))
			info->dma = 0;
	}
}

/*
 * cdrom driver request routine.
 */
static ide_startstop_t ide_cd_do_request(ide_drive_t *drive, struct request *rq,
					sector_t block)
{
	struct cdrom_info *info = drive->driver_data;
	ide_handler_t *fn;
	int xferlen;

	if (blk_fs_request(rq)) {
		if (drive->atapi_flags & IDE_AFLAG_SEEKING) {
			ide_hwif_t *hwif = drive->hwif;
			unsigned long elapsed = jiffies - info->start_seek;
			int stat = hwif->tp_ops->read_status(hwif);

			if ((stat & SEEK_STAT) != SEEK_STAT) {
				if (elapsed < IDECD_SEEK_TIMEOUT) {
					ide_stall_queue(drive,
							IDECD_SEEK_TIMER);
					return ide_stopped;
				}
				printk(KERN_ERR "%s: DSC timeout\n",
						drive->name);
			}
			drive->atapi_flags &= ~IDE_AFLAG_SEEKING;
		}
		if (rq_data_dir(rq) == READ &&
		    IDE_LARGE_SEEK(info->last_block, block,
			    IDECD_SEEK_THRESHOLD) &&
		    drive->dsc_overlap) {
			xferlen = 0;
			fn = cdrom_start_seek_continuation;

			info->dma = 0;
			info->start_seek = jiffies;

			ide_cd_prepare_seek_request(drive, rq);
		} else {
			xferlen = 32768;
			fn = cdrom_start_rw_cont;

			if (cdrom_start_rw(drive, rq) == ide_stopped)
				return ide_stopped;

			if (ide_cd_prepare_rw_request(drive, rq) == ide_stopped)
				return ide_stopped;
		}
		info->last_block = block;
	} else if (blk_sense_request(rq) || blk_pc_request(rq) ||
		   rq->cmd_type == REQ_TYPE_ATA_PC) {
		xferlen = rq->data_len;
		fn = cdrom_do_newpc_cont;

		if (!rq->timeout)
			rq->timeout = ATAPI_WAIT_PC;

		cdrom_do_block_pc(drive, rq);
	} else if (blk_special_request(rq)) {
		/* right now this can only be a reset... */
		cdrom_end_request(drive, 1);
		return ide_stopped;
	} else {
		blk_dump_rq_flags(rq, "ide-cd bad flags");
		cdrom_end_request(drive, 0);
		return ide_stopped;
	}

	return cdrom_start_packet_command(drive, xferlen, fn);
}

/*
 * Ioctl handling.
 *
 * Routines which queue packet commands take as a final argument a pointer to a
 * request_sense struct. If execution of the command results in an error with a
 * CHECK CONDITION status, this structure will be filled with the results of the
 * subsequent request sense command. The pointer can also be NULL, in which case
 * no sense information is returned.
 */
static void msf_from_bcd(struct atapi_msf *msf)
{
	msf->minute = bcd2bin(msf->minute);
	msf->second = bcd2bin(msf->second);
	msf->frame  = bcd2bin(msf->frame);
}

int cdrom_check_status(ide_drive_t *drive, struct request_sense *sense)
{
	struct cdrom_info *info = drive->driver_data;
	struct cdrom_device_info *cdi = &info->devinfo;
	unsigned char cmd[BLK_MAX_CDB];

	memset(cmd, 0, BLK_MAX_CDB);
	cmd[0] = GPCMD_TEST_UNIT_READY;

	/*
	 * Sanyo 3 CD changer uses byte 7 of TEST_UNIT_READY to switch CDs
	 * instead of supporting the LOAD_UNLOAD opcode.
	 */
	cmd[7] = cdi->sanyo_slot % 3;

	return ide_cd_queue_pc(drive, cmd, 0, NULL, NULL, sense, 0, REQ_QUIET);
}

static int cdrom_read_capacity(ide_drive_t *drive, unsigned long *capacity,
			       unsigned long *sectors_per_frame,
			       struct request_sense *sense)
{
	struct {
		__be32 lba;
		__be32 blocklen;
	} capbuf;

	int stat;
	unsigned char cmd[BLK_MAX_CDB];
	unsigned len = sizeof(capbuf);
	u32 blocklen;

	memset(cmd, 0, BLK_MAX_CDB);
	cmd[0] = GPCMD_READ_CDVD_CAPACITY;

	stat = ide_cd_queue_pc(drive, cmd, 0, &capbuf, &len, sense, 0,
			       REQ_QUIET);
	if (stat)
		return stat;

	/*
	 * Sanity check the given block size
	 */
	blocklen = be32_to_cpu(capbuf.blocklen);
	switch (blocklen) {
	case 512:
	case 1024:
	case 2048:
	case 4096:
		break;
	default:
		printk(KERN_ERR "%s: weird block size %u\n",
			drive->name, blocklen);
		printk(KERN_ERR "%s: default to 2kb block size\n",
			drive->name);
		blocklen = 2048;
		break;
	}

	*capacity = 1 + be32_to_cpu(capbuf.lba);
	*sectors_per_frame = blocklen >> SECTOR_BITS;
	return 0;
}

static int cdrom_read_tocentry(ide_drive_t *drive, int trackno, int msf_flag,
				int format, char *buf, int buflen,
				struct request_sense *sense)
{
	unsigned char cmd[BLK_MAX_CDB];

	memset(cmd, 0, BLK_MAX_CDB);

	cmd[0] = GPCMD_READ_TOC_PMA_ATIP;
	cmd[6] = trackno;
	cmd[7] = (buflen >> 8);
	cmd[8] = (buflen & 0xff);
	cmd[9] = (format << 6);

	if (msf_flag)
		cmd[1] = 2;

	return ide_cd_queue_pc(drive, cmd, 0, buf, &buflen, sense, 0, REQ_QUIET);
}

/* Try to read the entire TOC for the disk into our internal buffer. */
int ide_cd_read_toc(ide_drive_t *drive, struct request_sense *sense)
{
	int stat, ntracks, i;
	struct cdrom_info *info = drive->driver_data;
	struct cdrom_device_info *cdi = &info->devinfo;
	struct atapi_toc *toc = info->toc;
	struct {
		struct atapi_toc_header hdr;
		struct atapi_toc_entry  ent;
	} ms_tmp;
	long last_written;
	unsigned long sectors_per_frame = SECTORS_PER_FRAME;

	if (toc == NULL) {
		/* try to allocate space */
		toc = kmalloc(sizeof(struct atapi_toc), GFP_KERNEL);
		if (toc == NULL) {
			printk(KERN_ERR "%s: No cdrom TOC buffer!\n",
					drive->name);
			return -ENOMEM;
		}
		info->toc = toc;
	}

	/*
	 * Check to see if the existing data is still valid. If it is,
	 * just return.
	 */
	(void) cdrom_check_status(drive, sense);

	if (drive->atapi_flags & IDE_AFLAG_TOC_VALID)
		return 0;

	/* try to get the total cdrom capacity and sector size */
	stat = cdrom_read_capacity(drive, &toc->capacity, &sectors_per_frame,
				   sense);
	if (stat)
		toc->capacity = 0x1fffff;

	set_capacity(info->disk, toc->capacity * sectors_per_frame);
	/* save a private copy of the TOC capacity for error handling */
	drive->probed_capacity = toc->capacity * sectors_per_frame;

	blk_queue_hardsect_size(drive->queue,
				sectors_per_frame << SECTOR_BITS);

	/* first read just the header, so we know how long the TOC is */
	stat = cdrom_read_tocentry(drive, 0, 1, 0, (char *) &toc->hdr,
				    sizeof(struct atapi_toc_header), sense);
	if (stat)
		return stat;

	if (drive->atapi_flags & IDE_AFLAG_TOCTRACKS_AS_BCD) {
		toc->hdr.first_track = bcd2bin(toc->hdr.first_track);
		toc->hdr.last_track  = bcd2bin(toc->hdr.last_track);
	}

	ntracks = toc->hdr.last_track - toc->hdr.first_track + 1;
	if (ntracks <= 0)
		return -EIO;
	if (ntracks > MAX_TRACKS)
		ntracks = MAX_TRACKS;

	/* now read the whole schmeer */
	stat = cdrom_read_tocentry(drive, toc->hdr.first_track, 1, 0,
				  (char *)&toc->hdr,
				   sizeof(struct atapi_toc_header) +
				   (ntracks + 1) *
				   sizeof(struct atapi_toc_entry), sense);

	if (stat && toc->hdr.first_track > 1) {
		/*
		 * Cds with CDI tracks only don't have any TOC entries, despite
		 * of this the returned values are
		 * first_track == last_track = number of CDI tracks + 1,
		 * so that this case is indistinguishable from the same layout
		 * plus an additional audio track. If we get an error for the
		 * regular case, we assume a CDI without additional audio
		 * tracks. In this case the readable TOC is empty (CDI tracks
		 * are not included) and only holds the Leadout entry.
		 *
		 * Heiko Eißfeldt.
		 */
		ntracks = 0;
		stat = cdrom_read_tocentry(drive, CDROM_LEADOUT, 1, 0,
					   (char *)&toc->hdr,
					   sizeof(struct atapi_toc_header) +
					   (ntracks + 1) *
					   sizeof(struct atapi_toc_entry),
					   sense);
		if (stat)
			return stat;

		if (drive->atapi_flags & IDE_AFLAG_TOCTRACKS_AS_BCD) {
			toc->hdr.first_track = (u8)bin2bcd(CDROM_LEADOUT);
			toc->hdr.last_track = (u8)bin2bcd(CDROM_LEADOUT);
		} else {
			toc->hdr.first_track = CDROM_LEADOUT;
			toc->hdr.last_track = CDROM_LEADOUT;
		}
	}

	if (stat)
		return stat;

	toc->hdr.toc_length = be16_to_cpu(toc->hdr.toc_length);

	if (drive->atapi_flags & IDE_AFLAG_TOCTRACKS_AS_BCD) {
		toc->hdr.first_track = bcd2bin(toc->hdr.first_track);
		toc->hdr.last_track  = bcd2bin(toc->hdr.last_track);
	}

	for (i = 0; i <= ntracks; i++) {
		if (drive->atapi_flags & IDE_AFLAG_TOCADDR_AS_BCD) {
			if (drive->atapi_flags & IDE_AFLAG_TOCTRACKS_AS_BCD)
				toc->ent[i].track = bcd2bin(toc->ent[i].track);
			msf_from_bcd(&toc->ent[i].addr.msf);
		}
		toc->ent[i].addr.lba = msf_to_lba(toc->ent[i].addr.msf.minute,
						  toc->ent[i].addr.msf.second,
						  toc->ent[i].addr.msf.frame);
	}

	if (toc->hdr.first_track != CDROM_LEADOUT) {
		/* read the multisession information */
		stat = cdrom_read_tocentry(drive, 0, 0, 1, (char *)&ms_tmp,
					   sizeof(ms_tmp), sense);
		if (stat)
			return stat;

		toc->last_session_lba = be32_to_cpu(ms_tmp.ent.addr.lba);
	} else {
		ms_tmp.hdr.last_track = CDROM_LEADOUT;
		ms_tmp.hdr.first_track = ms_tmp.hdr.last_track;
		toc->last_session_lba = msf_to_lba(0, 2, 0); /* 0m 2s 0f */
	}

	if (drive->atapi_flags & IDE_AFLAG_TOCADDR_AS_BCD) {
		/* re-read multisession information using MSF format */
		stat = cdrom_read_tocentry(drive, 0, 1, 1, (char *)&ms_tmp,
					   sizeof(ms_tmp), sense);
		if (stat)
			return stat;

		msf_from_bcd(&ms_tmp.ent.addr.msf);
		toc->last_session_lba = msf_to_lba(ms_tmp.ent.addr.msf.minute,
						   ms_tmp.ent.addr.msf.second,
						   ms_tmp.ent.addr.msf.frame);
	}

	toc->xa_flag = (ms_tmp.hdr.first_track != ms_tmp.hdr.last_track);

	/* now try to get the total cdrom capacity */
	stat = cdrom_get_last_written(cdi, &last_written);
	if (!stat && (last_written > toc->capacity)) {
		toc->capacity = last_written;
		set_capacity(info->disk, toc->capacity * sectors_per_frame);
		drive->probed_capacity = toc->capacity * sectors_per_frame;
	}

	/* Remember that we've read this stuff. */
	drive->atapi_flags |= IDE_AFLAG_TOC_VALID;

	return 0;
}

int ide_cdrom_get_capabilities(ide_drive_t *drive, u8 *buf)
{
	struct cdrom_info *info = drive->driver_data;
	struct cdrom_device_info *cdi = &info->devinfo;
	struct packet_command cgc;
	int stat, attempts = 3, size = ATAPI_CAPABILITIES_PAGE_SIZE;

	if ((drive->atapi_flags & IDE_AFLAG_FULL_CAPS_PAGE) == 0)
		size -= ATAPI_CAPABILITIES_PAGE_PAD_SIZE;

	init_cdrom_command(&cgc, buf, size, CGC_DATA_UNKNOWN);
	do {
		/* we seem to get stat=0x01,err=0x00 the first time (??) */
		stat = cdrom_mode_sense(cdi, &cgc, GPMODE_CAPABILITIES_PAGE, 0);
		if (!stat)
			break;
	} while (--attempts);
	return stat;
}

void ide_cdrom_update_speed(ide_drive_t *drive, u8 *buf)
{
	struct cdrom_info *cd = drive->driver_data;
	u16 curspeed, maxspeed;

	if (drive->atapi_flags & IDE_AFLAG_LE_SPEED_FIELDS) {
		curspeed = le16_to_cpup((__le16 *)&buf[8 + 14]);
		maxspeed = le16_to_cpup((__le16 *)&buf[8 + 8]);
	} else {
		curspeed = be16_to_cpup((__be16 *)&buf[8 + 14]);
		maxspeed = be16_to_cpup((__be16 *)&buf[8 + 8]);
	}

	cd->current_speed = (curspeed + (176/2)) / 176;
	cd->max_speed = (maxspeed + (176/2)) / 176;
}

#define IDE_CD_CAPABILITIES \
	(CDC_CLOSE_TRAY | CDC_OPEN_TRAY | CDC_LOCK | CDC_SELECT_SPEED | \
	 CDC_SELECT_DISC | CDC_MULTI_SESSION | CDC_MCN | CDC_MEDIA_CHANGED | \
	 CDC_PLAY_AUDIO | CDC_RESET | CDC_DRIVE_STATUS | CDC_CD_R | \
	 CDC_CD_RW | CDC_DVD | CDC_DVD_R | CDC_DVD_RAM | CDC_GENERIC_PACKET | \
	 CDC_MO_DRIVE | CDC_MRW | CDC_MRW_W | CDC_RAM)

static struct cdrom_device_ops ide_cdrom_dops = {
	.open			= ide_cdrom_open_real,
	.release		= ide_cdrom_release_real,
	.drive_status		= ide_cdrom_drive_status,
	.media_changed		= ide_cdrom_check_media_change_real,
	.tray_move		= ide_cdrom_tray_move,
	.lock_door		= ide_cdrom_lock_door,
	.select_speed		= ide_cdrom_select_speed,
	.get_last_session	= ide_cdrom_get_last_session,
	.get_mcn		= ide_cdrom_get_mcn,
	.reset			= ide_cdrom_reset,
	.audio_ioctl		= ide_cdrom_audio_ioctl,
	.capability		= IDE_CD_CAPABILITIES,
	.generic_packet		= ide_cdrom_packet,
};

static int ide_cdrom_register(ide_drive_t *drive, int nslots)
{
	struct cdrom_info *info = drive->driver_data;
	struct cdrom_device_info *devinfo = &info->devinfo;

	devinfo->ops = &ide_cdrom_dops;
	devinfo->speed = info->current_speed;
	devinfo->capacity = nslots;
	devinfo->handle = drive;
	strcpy(devinfo->name, drive->name);

	if (drive->atapi_flags & IDE_AFLAG_NO_SPEED_SELECT)
		devinfo->mask |= CDC_SELECT_SPEED;

	devinfo->disk = info->disk;
	return register_cdrom(devinfo);
}

static int ide_cdrom_probe_capabilities(ide_drive_t *drive)
{
	struct cdrom_info *cd = drive->driver_data;
	struct cdrom_device_info *cdi = &cd->devinfo;
	u8 buf[ATAPI_CAPABILITIES_PAGE_SIZE];
	mechtype_t mechtype;
	int nslots = 1;

	cdi->mask = (CDC_CD_R | CDC_CD_RW | CDC_DVD | CDC_DVD_R |
		     CDC_DVD_RAM | CDC_SELECT_DISC | CDC_PLAY_AUDIO |
		     CDC_MO_DRIVE | CDC_RAM);

	if (drive->media == ide_optical) {
		cdi->mask &= ~(CDC_MO_DRIVE | CDC_RAM);
		printk(KERN_ERR "%s: ATAPI magneto-optical drive\n",
				drive->name);
		return nslots;
	}

	if (drive->atapi_flags & IDE_AFLAG_PRE_ATAPI12) {
		drive->atapi_flags &= ~IDE_AFLAG_NO_EJECT;
		cdi->mask &= ~CDC_PLAY_AUDIO;
		return nslots;
	}

	/*
	 * We have to cheat a little here. the packet will eventually be queued
	 * with ide_cdrom_packet(), which extracts the drive from cdi->handle.
	 * Since this device hasn't been registered with the Uniform layer yet,
	 * it can't do this. Same goes for cdi->ops.
	 */
	cdi->handle = drive;
	cdi->ops = &ide_cdrom_dops;

	if (ide_cdrom_get_capabilities(drive, buf))
		return 0;

	if ((buf[8 + 6] & 0x01) == 0)
		drive->atapi_flags |= IDE_AFLAG_NO_DOORLOCK;
	if (buf[8 + 6] & 0x08)
		drive->atapi_flags &= ~IDE_AFLAG_NO_EJECT;
	if (buf[8 + 3] & 0x01)
		cdi->mask &= ~CDC_CD_R;
	if (buf[8 + 3] & 0x02)
		cdi->mask &= ~(CDC_CD_RW | CDC_RAM);
	if (buf[8 + 2] & 0x38)
		cdi->mask &= ~CDC_DVD;
	if (buf[8 + 3] & 0x20)
		cdi->mask &= ~(CDC_DVD_RAM | CDC_RAM);
	if (buf[8 + 3] & 0x10)
		cdi->mask &= ~CDC_DVD_R;
	if ((buf[8 + 4] & 0x01) || (drive->atapi_flags & IDE_AFLAG_PLAY_AUDIO_OK))
		cdi->mask &= ~CDC_PLAY_AUDIO;

	mechtype = buf[8 + 6] >> 5;
	if (mechtype == mechtype_caddy || mechtype == mechtype_popup)
		cdi->mask |= CDC_CLOSE_TRAY;

	if (cdi->sanyo_slot > 0) {
		cdi->mask &= ~CDC_SELECT_DISC;
		nslots = 3;
	} else if (mechtype == mechtype_individual_changer ||
		   mechtype == mechtype_cartridge_changer) {
		nslots = cdrom_number_of_slots(cdi);
		if (nslots > 1)
			cdi->mask &= ~CDC_SELECT_DISC;
	}

	ide_cdrom_update_speed(drive, buf);

	printk(KERN_INFO "%s: ATAPI", drive->name);

	/* don't print speed if the drive reported 0 */
	if (cd->max_speed)
		printk(KERN_CONT " %dX", cd->max_speed);

	printk(KERN_CONT " %s", (cdi->mask & CDC_DVD) ? "CD-ROM" : "DVD-ROM");

	if ((cdi->mask & CDC_DVD_R) == 0 || (cdi->mask & CDC_DVD_RAM) == 0)
		printk(KERN_CONT " DVD%s%s",
				 (cdi->mask & CDC_DVD_R) ? "" : "-R",
				 (cdi->mask & CDC_DVD_RAM) ? "" : "-RAM");

	if ((cdi->mask & CDC_CD_R) == 0 || (cdi->mask & CDC_CD_RW) == 0)
		printk(KERN_CONT " CD%s%s",
				 (cdi->mask & CDC_CD_R) ? "" : "-R",
				 (cdi->mask & CDC_CD_RW) ? "" : "/RW");

	if ((cdi->mask & CDC_SELECT_DISC) == 0)
		printk(KERN_CONT " changer w/%d slots", nslots);
	else
		printk(KERN_CONT " drive");

	printk(KERN_CONT ", %dkB Cache\n", be16_to_cpup((__be16 *)&buf[8 + 12]));

	return nslots;
}

/* standard prep_rq_fn that builds 10 byte cmds */
static int ide_cdrom_prep_fs(struct request_queue *q, struct request *rq)
{
	int hard_sect = queue_hardsect_size(q);
	long block = (long)rq->hard_sector / (hard_sect >> 9);
	unsigned long blocks = rq->hard_nr_sectors / (hard_sect >> 9);

	memset(rq->cmd, 0, BLK_MAX_CDB);

	if (rq_data_dir(rq) == READ)
		rq->cmd[0] = GPCMD_READ_10;
	else
		rq->cmd[0] = GPCMD_WRITE_10;

	/*
	 * fill in lba
	 */
	rq->cmd[2] = (block >> 24) & 0xff;
	rq->cmd[3] = (block >> 16) & 0xff;
	rq->cmd[4] = (block >>  8) & 0xff;
	rq->cmd[5] = block & 0xff;

	/*
	 * and transfer length
	 */
	rq->cmd[7] = (blocks >> 8) & 0xff;
	rq->cmd[8] = blocks & 0xff;
	rq->cmd_len = 10;
	return BLKPREP_OK;
}

/*
 * Most of the SCSI commands are supported directly by ATAPI devices.
 * This transform handles the few exceptions.
 */
static int ide_cdrom_prep_pc(struct request *rq)
{
	u8 *c = rq->cmd;

	/* transform 6-byte read/write commands to the 10-byte version */
	if (c[0] == READ_6 || c[0] == WRITE_6) {
		c[8] = c[4];
		c[5] = c[3];
		c[4] = c[2];
		c[3] = c[1] & 0x1f;
		c[2] = 0;
		c[1] &= 0xe0;
		c[0] += (READ_10 - READ_6);
		rq->cmd_len = 10;
		return BLKPREP_OK;
	}

	/*
	 * it's silly to pretend we understand 6-byte sense commands, just
	 * reject with ILLEGAL_REQUEST and the caller should take the
	 * appropriate action
	 */
	if (c[0] == MODE_SENSE || c[0] == MODE_SELECT) {
		rq->errors = ILLEGAL_REQUEST;
		return BLKPREP_KILL;
	}

	return BLKPREP_OK;
}

static int ide_cdrom_prep_fn(struct request_queue *q, struct request *rq)
{
	if (blk_fs_request(rq))
		return ide_cdrom_prep_fs(q, rq);
	else if (blk_pc_request(rq))
		return ide_cdrom_prep_pc(rq);

	return 0;
}

struct cd_list_entry {
	const char	*id_model;
	const char	*id_firmware;
	unsigned int	cd_flags;
};

#ifdef CONFIG_IDE_PROC_FS
static sector_t ide_cdrom_capacity(ide_drive_t *drive)
{
	unsigned long capacity, sectors_per_frame;

	if (cdrom_read_capacity(drive, &capacity, &sectors_per_frame, NULL))
		return 0;

	return capacity * sectors_per_frame;
}

static int proc_idecd_read_capacity(char *page, char **start, off_t off,
					int count, int *eof, void *data)
{
	ide_drive_t *drive = data;
	int len;

	len = sprintf(page, "%llu\n", (long long)ide_cdrom_capacity(drive));
	PROC_IDE_READ_RETURN(page, start, off, count, eof, len);
}

static ide_proc_entry_t idecd_proc[] = {
	{ "capacity", S_IFREG|S_IRUGO, proc_idecd_read_capacity, NULL },
	{ NULL, 0, NULL, NULL }
};

static void ide_cdrom_add_settings(ide_drive_t *drive)
{
	ide_add_setting(drive, "dsc_overlap", SETTING_RW, TYPE_BYTE, 0, 1, 1, 1,
			&drive->dsc_overlap, NULL);
}
#else
static inline void ide_cdrom_add_settings(ide_drive_t *drive) { ; }
#endif

static const struct cd_list_entry ide_cd_quirks_list[] = {
	/* Limit transfer size per interrupt. */
	{ "SAMSUNG CD-ROM SCR-2430", NULL,   IDE_AFLAG_LIMIT_NFRAMES	     },
	{ "SAMSUNG CD-ROM SCR-2432", NULL,   IDE_AFLAG_LIMIT_NFRAMES	     },
	/* SCR-3231 doesn't support the SET_CD_SPEED command. */
	{ "SAMSUNG CD-ROM SCR-3231", NULL,   IDE_AFLAG_NO_SPEED_SELECT	     },
	/* Old NEC260 (not R) was released before ATAPI 1.2 spec. */
	{ "NEC CD-ROM DRIVE:260",    "1.01", IDE_AFLAG_TOCADDR_AS_BCD |
					     IDE_AFLAG_PRE_ATAPI12,	     },
	/* Vertos 300, some versions of this drive like to talk BCD. */
	{ "V003S0DS",		     NULL,   IDE_AFLAG_VERTOS_300_SSD,	     },
	/* Vertos 600 ESD. */
	{ "V006E0DS",		     NULL,   IDE_AFLAG_VERTOS_600_ESD,	     },
	/*
	 * Sanyo 3 CD changer uses a non-standard command for CD changing
	 * (by default standard ATAPI support for CD changers is used).
	 */
	{ "CD-ROM CDR-C3 G",	     NULL,   IDE_AFLAG_SANYO_3CD	     },
	{ "CD-ROM CDR-C3G",	     NULL,   IDE_AFLAG_SANYO_3CD	     },
	{ "CD-ROM CDR_C36",	     NULL,   IDE_AFLAG_SANYO_3CD	     },
	/* Stingray 8X CD-ROM. */
	{ "STINGRAY 8422 IDE 8X CD-ROM 7-27-95", NULL, IDE_AFLAG_PRE_ATAPI12 },
	/*
	 * ACER 50X CD-ROM and WPI 32X CD-ROM require the full spec length
	 * mode sense page capabilities size, but older drives break.
	 */
	{ "ATAPI CD ROM DRIVE 50X MAX",	NULL,	IDE_AFLAG_FULL_CAPS_PAGE     },
	{ "WPI CDS-32X",		NULL,	IDE_AFLAG_FULL_CAPS_PAGE     },
	/* ACER/AOpen 24X CD-ROM has the speed fields byte-swapped. */
	{ "",			     "241N", IDE_AFLAG_LE_SPEED_FIELDS       },
	/*
	 * Some drives used by Apple don't advertise audio play
	 * but they do support reading TOC & audio datas.
	 */
	{ "MATSHITADVD-ROM SR-8187", NULL,   IDE_AFLAG_PLAY_AUDIO_OK	     },
	{ "MATSHITADVD-ROM SR-8186", NULL,   IDE_AFLAG_PLAY_AUDIO_OK	     },
	{ "MATSHITADVD-ROM SR-8176", NULL,   IDE_AFLAG_PLAY_AUDIO_OK	     },
	{ "MATSHITADVD-ROM SR-8174", NULL,   IDE_AFLAG_PLAY_AUDIO_OK	     },
	{ "Optiarc DVD RW AD-5200A", NULL,   IDE_AFLAG_PLAY_AUDIO_OK	     },
	{ NULL, NULL, 0 }
};

static unsigned int ide_cd_flags(struct hd_driveid *id)
{
	const struct cd_list_entry *cle = ide_cd_quirks_list;

	while (cle->id_model) {
		if (strcmp(cle->id_model, id->model) == 0 &&
		    (cle->id_firmware == NULL ||
		     strstr(id->fw_rev, cle->id_firmware)))
			return cle->cd_flags;
		cle++;
	}

	return 0;
}

static int ide_cdrom_setup(ide_drive_t *drive)
{
	struct cdrom_info *cd = drive->driver_data;
	struct cdrom_device_info *cdi = &cd->devinfo;
	struct hd_driveid *id = drive->id;
	int nslots;

	blk_queue_prep_rq(drive->queue, ide_cdrom_prep_fn);
	blk_queue_dma_alignment(drive->queue, 31);
	blk_queue_update_dma_pad(drive->queue, 15);
	drive->queue->unplug_delay = (1 * HZ) / 1000;
	if (!drive->queue->unplug_delay)
		drive->queue->unplug_delay = 1;

	drive->special.all	= 0;

	drive->atapi_flags = IDE_AFLAG_MEDIA_CHANGED | IDE_AFLAG_NO_EJECT |
		       ide_cd_flags(id);

	if ((id->config & 0x0060) == 0x20)
		drive->atapi_flags |= IDE_AFLAG_DRQ_INTERRUPT;

	if ((drive->atapi_flags & IDE_AFLAG_VERTOS_300_SSD) &&
	    id->fw_rev[4] == '1' && id->fw_rev[6] <= '2')
		drive->atapi_flags |= (IDE_AFLAG_TOCTRACKS_AS_BCD |
				     IDE_AFLAG_TOCADDR_AS_BCD);
	else if ((drive->atapi_flags & IDE_AFLAG_VERTOS_600_ESD) &&
		 id->fw_rev[4] == '1' && id->fw_rev[6] <= '2')
		drive->atapi_flags |= IDE_AFLAG_TOCTRACKS_AS_BCD;
	else if (drive->atapi_flags & IDE_AFLAG_SANYO_3CD)
		/* 3 => use CD in slot 0 */
		cdi->sanyo_slot = 3;

	nslots = ide_cdrom_probe_capabilities(drive);

	/* set correct block size */
	blk_queue_hardsect_size(drive->queue, CD_FRAMESIZE);

	drive->dsc_overlap = (drive->next != drive);

	if (ide_cdrom_register(drive, nslots)) {
		printk(KERN_ERR "%s: %s failed to register device with the"
				" cdrom driver.\n", drive->name, __func__);
		cd->devinfo.handle = NULL;
		return 1;
	}
	ide_cdrom_add_settings(drive);
	return 0;
}

static void ide_cd_remove(ide_drive_t *drive)
{
	struct cdrom_info *info = drive->driver_data;

	ide_proc_unregister_driver(drive, info->driver);

	blk_unregister_filter(info->disk);
	del_gendisk(info->disk);

	ide_cd_put(info);
}

static void ide_cd_release(struct kref *kref)
{
	struct cdrom_info *info = to_ide_cd(kref);
	struct cdrom_device_info *devinfo = &info->devinfo;
	ide_drive_t *drive = info->drive;
	struct gendisk *g = info->disk;

	kfree(info->toc);
	if (devinfo->handle == drive)
		unregister_cdrom(devinfo);
	drive->dsc_overlap = 0;
	drive->driver_data = NULL;
	blk_queue_prep_rq(drive->queue, NULL);
	g->private_data = NULL;
	put_disk(g);
	kfree(info);
}

static int ide_cd_probe(ide_drive_t *);

static ide_driver_t ide_cdrom_driver = {
	.gen_driver = {
		.owner		= THIS_MODULE,
		.name		= "ide-cdrom",
		.bus		= &ide_bus_type,
	},
	.probe			= ide_cd_probe,
	.remove			= ide_cd_remove,
	.version		= IDECD_VERSION,
	.media			= ide_cdrom,
	.supports_dsc_overlap	= 1,
	.do_request		= ide_cd_do_request,
	.end_request		= ide_end_request,
	.error			= __ide_error,
#ifdef CONFIG_IDE_PROC_FS
	.proc			= idecd_proc,
#endif
};

static int idecd_open(struct inode *inode, struct file *file)
{
	struct gendisk *disk = inode->i_bdev->bd_disk;
	struct cdrom_info *info;
	int rc = -ENOMEM;

	info = ide_cd_get(disk);
	if (!info)
		return -ENXIO;

	rc = cdrom_open(&info->devinfo, inode, file);

	if (rc < 0)
		ide_cd_put(info);

	return rc;
}

static int idecd_release(struct inode *inode, struct file *file)
{
	struct gendisk *disk = inode->i_bdev->bd_disk;
	struct cdrom_info *info = ide_cd_g(disk);

	cdrom_release(&info->devinfo, file);

	ide_cd_put(info);

	return 0;
}

static int idecd_set_spindown(struct cdrom_device_info *cdi, unsigned long arg)
{
	struct packet_command cgc;
	char buffer[16];
	int stat;
	char spindown;

	if (copy_from_user(&spindown, (void __user *)arg, sizeof(char)))
		return -EFAULT;

	init_cdrom_command(&cgc, buffer, sizeof(buffer), CGC_DATA_UNKNOWN);

	stat = cdrom_mode_sense(cdi, &cgc, GPMODE_CDROM_PAGE, 0);
	if (stat)
		return stat;

	buffer[11] = (buffer[11] & 0xf0) | (spindown & 0x0f);
	return cdrom_mode_select(cdi, &cgc);
}

static int idecd_get_spindown(struct cdrom_device_info *cdi, unsigned long arg)
{
	struct packet_command cgc;
	char buffer[16];
	int stat;
	char spindown;

	init_cdrom_command(&cgc, buffer, sizeof(buffer), CGC_DATA_UNKNOWN);

	stat = cdrom_mode_sense(cdi, &cgc, GPMODE_CDROM_PAGE, 0);
	if (stat)
		return stat;

	spindown = buffer[11] & 0x0f;
	if (copy_to_user((void __user *)arg, &spindown, sizeof(char)))
		return -EFAULT;
	return 0;
}

static int idecd_ioctl(struct inode *inode, struct file *file,
			unsigned int cmd, unsigned long arg)
{
	struct block_device *bdev = inode->i_bdev;
	struct cdrom_info *info = ide_cd_g(bdev->bd_disk);
	int err;

	switch (cmd) {
	case CDROMSETSPINDOWN:
		return idecd_set_spindown(&info->devinfo, arg);
	case CDROMGETSPINDOWN:
		return idecd_get_spindown(&info->devinfo, arg);
	default:
		break;
	}

	err = generic_ide_ioctl(info->drive, file, bdev, cmd, arg);
	if (err == -EINVAL)
		err = cdrom_ioctl(file, &info->devinfo, inode, cmd, arg);

	return err;
}

static int idecd_media_changed(struct gendisk *disk)
{
	struct cdrom_info *info = ide_cd_g(disk);
	return cdrom_media_changed(&info->devinfo);
}

static int idecd_revalidate_disk(struct gendisk *disk)
{
	struct cdrom_info *info = ide_cd_g(disk);
	struct request_sense sense;

	ide_cd_read_toc(info->drive, &sense);

	return  0;
}

static struct block_device_operations idecd_ops = {
	.owner			= THIS_MODULE,
	.open			= idecd_open,
	.release		= idecd_release,
	.ioctl			= idecd_ioctl,
	.media_changed		= idecd_media_changed,
	.revalidate_disk	= idecd_revalidate_disk
};

/* module options */
static char *ignore;

module_param(ignore, charp, 0400);
MODULE_DESCRIPTION("ATAPI CD-ROM Driver");

static int ide_cd_probe(ide_drive_t *drive)
{
	struct cdrom_info *info;
	struct gendisk *g;
	struct request_sense sense;

	if (!strstr("ide-cdrom", drive->driver_req))
		goto failed;
	if (!drive->present)
		goto failed;
	if (drive->media != ide_cdrom && drive->media != ide_optical)
		goto failed;
	/* skip drives that we were told to ignore */
	if (ignore != NULL) {
		if (strstr(ignore, drive->name)) {
			printk(KERN_INFO "ide-cd: ignoring drive %s\n",
					 drive->name);
			goto failed;
		}
	}
	info = kzalloc(sizeof(struct cdrom_info), GFP_KERNEL);
	if (info == NULL) {
		printk(KERN_ERR "%s: Can't allocate a cdrom structure\n",
				drive->name);
		goto failed;
	}

	g = alloc_disk(1 << PARTN_BITS);
	if (!g)
		goto out_free_cd;

	ide_init_disk(g, drive);

	ide_proc_register_driver(drive, &ide_cdrom_driver);

	kref_init(&info->kref);

	info->drive = drive;
	info->driver = &ide_cdrom_driver;
	info->disk = g;

	g->private_data = &info->driver;

	drive->driver_data = info;

	g->minors = 1;
	g->driverfs_dev = &drive->gendev;
	g->flags = GENHD_FL_CD | GENHD_FL_REMOVABLE;
	if (ide_cdrom_setup(drive)) {
		ide_proc_unregister_driver(drive, &ide_cdrom_driver);
		ide_cd_release(&info->kref);
		goto failed;
	}

	ide_cd_read_toc(drive, &sense);
	g->fops = &idecd_ops;
	g->flags |= GENHD_FL_REMOVABLE;
	add_disk(g);
	blk_register_filter(g);
	return 0;

out_free_cd:
	kfree(info);
failed:
	return -ENODEV;
}

static void __exit ide_cdrom_exit(void)
{
	driver_unregister(&ide_cdrom_driver.gen_driver);
}

static int __init ide_cdrom_init(void)
{
	return driver_register(&ide_cdrom_driver.gen_driver);
}

MODULE_ALIAS("ide:*m-cdrom*");
MODULE_ALIAS("ide-cd");
module_init(ide_cdrom_init);
module_exit(ide_cdrom_exit);
MODULE_LICENSE("GPL");
