/*
 *	IDE I/O functions
 *
 *	Basic PIO and command management functionality.
 *
 * This code was split off from ide.c. See ide.c for history and original
 * copyrights.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * For the avoidance of doubt the "preferred form" of this code is one which
 * is in an open non patent encumbered format. Where cryptographic key signing
 * forms part of the process of creating an executable the information
 * including keys needed to generate an equivalently functional executable
 * are deemed to be part of the source code.
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
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/ide.h>
#include <linux/completion.h>
#include <linux/reboot.h>
#include <linux/cdrom.h>
#include <linux/seq_file.h>
#include <linux/device.h>
#include <linux/kmod.h>
#include <linux/scatterlist.h>

#include <asm/byteorder.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/bitops.h>

static int __ide_end_request(ide_drive_t *drive, struct request *rq,
			     int uptodate, unsigned int nr_bytes)
{
	int ret = 1;

	/*
	 * if failfast is set on a request, override number of sectors and
	 * complete the whole request right now
	 */
	if (blk_noretry_request(rq) && end_io_error(uptodate))
		nr_bytes = rq->hard_nr_sectors << 9;

	if (!blk_fs_request(rq) && end_io_error(uptodate) && !rq->errors)
		rq->errors = -EIO;

	/*
	 * decide whether to reenable DMA -- 3 is a random magic for now,
	 * if we DMA timeout more than 3 times, just stay in PIO
	 */
	if (drive->state == DMA_PIO_RETRY && drive->retry_pio <= 3) {
		drive->state = 0;
		HWGROUP(drive)->hwif->ide_dma_on(drive);
	}

	if (!end_that_request_chunk(rq, uptodate, nr_bytes)) {
		add_disk_randomness(rq->rq_disk);
		if (!list_empty(&rq->queuelist))
			blkdev_dequeue_request(rq);
		HWGROUP(drive)->rq = NULL;
		end_that_request_last(rq, uptodate);
		ret = 0;
	}

	return ret;
}

/**
 *	ide_end_request		-	complete an IDE I/O
 *	@drive: IDE device for the I/O
 *	@uptodate:
 *	@nr_sectors: number of sectors completed
 *
 *	This is our end_request wrapper function. We complete the I/O
 *	update random number input and dequeue the request, which if
 *	it was tagged may be out of order.
 */

int ide_end_request (ide_drive_t *drive, int uptodate, int nr_sectors)
{
	unsigned int nr_bytes = nr_sectors << 9;
	struct request *rq;
	unsigned long flags;
	int ret = 1;

	/*
	 * room for locking improvements here, the calls below don't
	 * need the queue lock held at all
	 */
	spin_lock_irqsave(&ide_lock, flags);
	rq = HWGROUP(drive)->rq;

	if (!nr_bytes) {
		if (blk_pc_request(rq))
			nr_bytes = rq->data_len;
		else
			nr_bytes = rq->hard_cur_sectors << 9;
	}

	ret = __ide_end_request(drive, rq, uptodate, nr_bytes);

	spin_unlock_irqrestore(&ide_lock, flags);
	return ret;
}
EXPORT_SYMBOL(ide_end_request);

/*
 * Power Management state machine. This one is rather trivial for now,
 * we should probably add more, like switching back to PIO on suspend
 * to help some BIOSes, re-do the door locking on resume, etc...
 */

enum {
	ide_pm_flush_cache	= ide_pm_state_start_suspend,
	idedisk_pm_standby,

	idedisk_pm_restore_pio	= ide_pm_state_start_resume,
	idedisk_pm_idle,
	ide_pm_restore_dma,
};

static void ide_complete_power_step(ide_drive_t *drive, struct request *rq, u8 stat, u8 error)
{
	struct request_pm_state *pm = rq->data;

	if (drive->media != ide_disk)
		return;

	switch (pm->pm_step) {
	case ide_pm_flush_cache:	/* Suspend step 1 (flush cache) complete */
		if (pm->pm_state == PM_EVENT_FREEZE)
			pm->pm_step = ide_pm_state_completed;
		else
			pm->pm_step = idedisk_pm_standby;
		break;
	case idedisk_pm_standby:	/* Suspend step 2 (standby) complete */
		pm->pm_step = ide_pm_state_completed;
		break;
	case idedisk_pm_restore_pio:	/* Resume step 1 complete */
		pm->pm_step = idedisk_pm_idle;
		break;
	case idedisk_pm_idle:		/* Resume step 2 (idle) complete */
		pm->pm_step = ide_pm_restore_dma;
		break;
	}
}

static ide_startstop_t ide_start_power_step(ide_drive_t *drive, struct request *rq)
{
	struct request_pm_state *pm = rq->data;
	ide_task_t *args = rq->special;

	memset(args, 0, sizeof(*args));

	switch (pm->pm_step) {
	case ide_pm_flush_cache:	/* Suspend step 1 (flush cache) */
		if (drive->media != ide_disk)
			break;
		/* Not supported? Switch to next step now. */
		if (!drive->wcache || !ide_id_has_flush_cache(drive->id)) {
			ide_complete_power_step(drive, rq, 0, 0);
			return ide_stopped;
		}
		if (ide_id_has_flush_cache_ext(drive->id))
			args->tfRegister[IDE_COMMAND_OFFSET] = WIN_FLUSH_CACHE_EXT;
		else
			args->tfRegister[IDE_COMMAND_OFFSET] = WIN_FLUSH_CACHE;
		args->command_type = IDE_DRIVE_TASK_NO_DATA;
		args->handler	   = &task_no_data_intr;
		return do_rw_taskfile(drive, args);

	case idedisk_pm_standby:	/* Suspend step 2 (standby) */
		args->tfRegister[IDE_COMMAND_OFFSET] = WIN_STANDBYNOW1;
		args->command_type = IDE_DRIVE_TASK_NO_DATA;
		args->handler	   = &task_no_data_intr;
		return do_rw_taskfile(drive, args);

	case idedisk_pm_restore_pio:	/* Resume step 1 (restore PIO) */
		if (drive->hwif->tuneproc != NULL)
			drive->hwif->tuneproc(drive, 255);
		/*
		 * skip idedisk_pm_idle for ATAPI devices
		 */
		if (drive->media != ide_disk)
			pm->pm_step = ide_pm_restore_dma;
		else
			ide_complete_power_step(drive, rq, 0, 0);
		return ide_stopped;

	case idedisk_pm_idle:		/* Resume step 2 (idle) */
		args->tfRegister[IDE_COMMAND_OFFSET] = WIN_IDLEIMMEDIATE;
		args->command_type = IDE_DRIVE_TASK_NO_DATA;
		args->handler = task_no_data_intr;
		return do_rw_taskfile(drive, args);

	case ide_pm_restore_dma:	/* Resume step 3 (restore DMA) */
		/*
		 * Right now, all we do is call hwif->ide_dma_check(drive),
		 * we could be smarter and check for current xfer_speed
		 * in struct drive etc...
		 */
		if (drive->hwif->ide_dma_check == NULL)
			break;
		drive->hwif->dma_off_quietly(drive);
		/*
		 * TODO: respect ->using_dma setting
		 */
		ide_set_dma(drive);
		break;
	}
	pm->pm_step = ide_pm_state_completed;
	return ide_stopped;
}

/**
 *	ide_end_dequeued_request	-	complete an IDE I/O
 *	@drive: IDE device for the I/O
 *	@uptodate:
 *	@nr_sectors: number of sectors completed
 *
 *	Complete an I/O that is no longer on the request queue. This
 *	typically occurs when we pull the request and issue a REQUEST_SENSE.
 *	We must still finish the old request but we must not tamper with the
 *	queue in the meantime.
 *
 *	NOTE: This path does not handle barrier, but barrier is not supported
 *	on ide-cd anyway.
 */

int ide_end_dequeued_request(ide_drive_t *drive, struct request *rq,
			     int uptodate, int nr_sectors)
{
	unsigned long flags;
	int ret = 1;

	spin_lock_irqsave(&ide_lock, flags);

	BUG_ON(!blk_rq_started(rq));

	/*
	 * if failfast is set on a request, override number of sectors and
	 * complete the whole request right now
	 */
	if (blk_noretry_request(rq) && end_io_error(uptodate))
		nr_sectors = rq->hard_nr_sectors;

	if (!blk_fs_request(rq) && end_io_error(uptodate) && !rq->errors)
		rq->errors = -EIO;

	/*
	 * decide whether to reenable DMA -- 3 is a random magic for now,
	 * if we DMA timeout more than 3 times, just stay in PIO
	 */
	if (drive->state == DMA_PIO_RETRY && drive->retry_pio <= 3) {
		drive->state = 0;
		HWGROUP(drive)->hwif->ide_dma_on(drive);
	}

	if (!end_that_request_first(rq, uptodate, nr_sectors)) {
		add_disk_randomness(rq->rq_disk);
		if (blk_rq_tagged(rq))
			blk_queue_end_tag(drive->queue, rq);
		end_that_request_last(rq, uptodate);
		ret = 0;
	}
	spin_unlock_irqrestore(&ide_lock, flags);
	return ret;
}
EXPORT_SYMBOL_GPL(ide_end_dequeued_request);


/**
 *	ide_complete_pm_request - end the current Power Management request
 *	@drive: target drive
 *	@rq: request
 *
 *	This function cleans up the current PM request and stops the queue
 *	if necessary.
 */
static void ide_complete_pm_request (ide_drive_t *drive, struct request *rq)
{
	unsigned long flags;

#ifdef DEBUG_PM
	printk("%s: completing PM request, %s\n", drive->name,
	       blk_pm_suspend_request(rq) ? "suspend" : "resume");
#endif
	spin_lock_irqsave(&ide_lock, flags);
	if (blk_pm_suspend_request(rq)) {
		blk_stop_queue(drive->queue);
	} else {
		drive->blocked = 0;
		blk_start_queue(drive->queue);
	}
	blkdev_dequeue_request(rq);
	HWGROUP(drive)->rq = NULL;
	end_that_request_last(rq, 1);
	spin_unlock_irqrestore(&ide_lock, flags);
}

/*
 * FIXME: probably move this somewhere else, name is bad too :)
 */
u64 ide_get_error_location(ide_drive_t *drive, char *args)
{
	u32 high, low;
	u8 hcyl, lcyl, sect;
	u64 sector;

	high = 0;
	hcyl = args[5];
	lcyl = args[4];
	sect = args[3];

	if (ide_id_has_flush_cache_ext(drive->id)) {
		low = (hcyl << 16) | (lcyl << 8) | sect;
		HWIF(drive)->OUTB(drive->ctl|0x80, IDE_CONTROL_REG);
		high = ide_read_24(drive);
	} else {
		u8 cur = HWIF(drive)->INB(IDE_SELECT_REG);
		if (cur & 0x40) {
			high = cur & 0xf;
			low = (hcyl << 16) | (lcyl << 8) | sect;
		} else {
			low = hcyl * drive->head * drive->sect;
			low += lcyl * drive->sect;
			low += sect - 1;
		}
	}

	sector = ((u64) high << 24) | low;
	return sector;
}
EXPORT_SYMBOL(ide_get_error_location);

/**
 *	ide_end_drive_cmd	-	end an explicit drive command
 *	@drive: command 
 *	@stat: status bits
 *	@err: error bits
 *
 *	Clean up after success/failure of an explicit drive command.
 *	These get thrown onto the queue so they are synchronized with
 *	real I/O operations on the drive.
 *
 *	In LBA48 mode we have to read the register set twice to get
 *	all the extra information out.
 */
 
void ide_end_drive_cmd (ide_drive_t *drive, u8 stat, u8 err)
{
	ide_hwif_t *hwif = HWIF(drive);
	unsigned long flags;
	struct request *rq;

	spin_lock_irqsave(&ide_lock, flags);
	rq = HWGROUP(drive)->rq;
	spin_unlock_irqrestore(&ide_lock, flags);

	if (rq->cmd_type == REQ_TYPE_ATA_CMD) {
		u8 *args = (u8 *) rq->buffer;
		if (rq->errors == 0)
			rq->errors = !OK_STAT(stat,READY_STAT,BAD_STAT);

		if (args) {
			args[0] = stat;
			args[1] = err;
			args[2] = hwif->INB(IDE_NSECTOR_REG);
		}
	} else if (rq->cmd_type == REQ_TYPE_ATA_TASK) {
		u8 *args = (u8 *) rq->buffer;
		if (rq->errors == 0)
			rq->errors = !OK_STAT(stat,READY_STAT,BAD_STAT);

		if (args) {
			args[0] = stat;
			args[1] = err;
			args[2] = hwif->INB(IDE_NSECTOR_REG);
			args[3] = hwif->INB(IDE_SECTOR_REG);
			args[4] = hwif->INB(IDE_LCYL_REG);
			args[5] = hwif->INB(IDE_HCYL_REG);
			args[6] = hwif->INB(IDE_SELECT_REG);
		}
	} else if (rq->cmd_type == REQ_TYPE_ATA_TASKFILE) {
		ide_task_t *args = (ide_task_t *) rq->special;
		if (rq->errors == 0)
			rq->errors = !OK_STAT(stat,READY_STAT,BAD_STAT);
			
		if (args) {
			if (args->tf_in_flags.b.data) {
				u16 data				= hwif->INW(IDE_DATA_REG);
				args->tfRegister[IDE_DATA_OFFSET]	= (data) & 0xFF;
				args->hobRegister[IDE_DATA_OFFSET]	= (data >> 8) & 0xFF;
			}
			args->tfRegister[IDE_ERROR_OFFSET]   = err;
			/* be sure we're looking at the low order bits */
			hwif->OUTB(drive->ctl & ~0x80, IDE_CONTROL_REG);
			args->tfRegister[IDE_NSECTOR_OFFSET] = hwif->INB(IDE_NSECTOR_REG);
			args->tfRegister[IDE_SECTOR_OFFSET]  = hwif->INB(IDE_SECTOR_REG);
			args->tfRegister[IDE_LCYL_OFFSET]    = hwif->INB(IDE_LCYL_REG);
			args->tfRegister[IDE_HCYL_OFFSET]    = hwif->INB(IDE_HCYL_REG);
			args->tfRegister[IDE_SELECT_OFFSET]  = hwif->INB(IDE_SELECT_REG);
			args->tfRegister[IDE_STATUS_OFFSET]  = stat;

			if (drive->addressing == 1) {
				hwif->OUTB(drive->ctl|0x80, IDE_CONTROL_REG);
				args->hobRegister[IDE_FEATURE_OFFSET]	= hwif->INB(IDE_FEATURE_REG);
				args->hobRegister[IDE_NSECTOR_OFFSET]	= hwif->INB(IDE_NSECTOR_REG);
				args->hobRegister[IDE_SECTOR_OFFSET]	= hwif->INB(IDE_SECTOR_REG);
				args->hobRegister[IDE_LCYL_OFFSET]	= hwif->INB(IDE_LCYL_REG);
				args->hobRegister[IDE_HCYL_OFFSET]	= hwif->INB(IDE_HCYL_REG);
			}
		}
	} else if (blk_pm_request(rq)) {
		struct request_pm_state *pm = rq->data;
#ifdef DEBUG_PM
		printk("%s: complete_power_step(step: %d, stat: %x, err: %x)\n",
			drive->name, rq->pm->pm_step, stat, err);
#endif
		ide_complete_power_step(drive, rq, stat, err);
		if (pm->pm_step == ide_pm_state_completed)
			ide_complete_pm_request(drive, rq);
		return;
	}

	spin_lock_irqsave(&ide_lock, flags);
	blkdev_dequeue_request(rq);
	HWGROUP(drive)->rq = NULL;
	rq->errors = err;
	end_that_request_last(rq, !rq->errors);
	spin_unlock_irqrestore(&ide_lock, flags);
}

EXPORT_SYMBOL(ide_end_drive_cmd);

/**
 *	try_to_flush_leftover_data	-	flush junk
 *	@drive: drive to flush
 *
 *	try_to_flush_leftover_data() is invoked in response to a drive
 *	unexpectedly having its DRQ_STAT bit set.  As an alternative to
 *	resetting the drive, this routine tries to clear the condition
 *	by read a sector's worth of data from the drive.  Of course,
 *	this may not help if the drive is *waiting* for data from *us*.
 */
static void try_to_flush_leftover_data (ide_drive_t *drive)
{
	int i = (drive->mult_count ? drive->mult_count : 1) * SECTOR_WORDS;

	if (drive->media != ide_disk)
		return;
	while (i > 0) {
		u32 buffer[16];
		u32 wcount = (i > 16) ? 16 : i;

		i -= wcount;
		HWIF(drive)->ata_input_data(drive, buffer, wcount);
	}
}

static void ide_kill_rq(ide_drive_t *drive, struct request *rq)
{
	if (rq->rq_disk) {
		ide_driver_t *drv;

		drv = *(ide_driver_t **)rq->rq_disk->private_data;
		drv->end_request(drive, 0, 0);
	} else
		ide_end_request(drive, 0, 0);
}

static ide_startstop_t ide_ata_error(ide_drive_t *drive, struct request *rq, u8 stat, u8 err)
{
	ide_hwif_t *hwif = drive->hwif;

	if (stat & BUSY_STAT || ((stat & WRERR_STAT) && !drive->nowerr)) {
		/* other bits are useless when BUSY */
		rq->errors |= ERROR_RESET;
	} else if (stat & ERR_STAT) {
		/* err has different meaning on cdrom and tape */
		if (err == ABRT_ERR) {
			if (drive->select.b.lba &&
			    /* some newer drives don't support WIN_SPECIFY */
			    hwif->INB(IDE_COMMAND_REG) == WIN_SPECIFY)
				return ide_stopped;
		} else if ((err & BAD_CRC) == BAD_CRC) {
			/* UDMA crc error, just retry the operation */
			drive->crc_count++;
		} else if (err & (BBD_ERR | ECC_ERR)) {
			/* retries won't help these */
			rq->errors = ERROR_MAX;
		} else if (err & TRK0_ERR) {
			/* help it find track zero */
			rq->errors |= ERROR_RECAL;
		}
	}

	if ((stat & DRQ_STAT) && rq_data_dir(rq) == READ && hwif->err_stops_fifo == 0)
		try_to_flush_leftover_data(drive);

	if (rq->errors >= ERROR_MAX || blk_noretry_request(rq)) {
		ide_kill_rq(drive, rq);
		return ide_stopped;
	}

	if (hwif->INB(IDE_STATUS_REG) & (BUSY_STAT|DRQ_STAT))
		rq->errors |= ERROR_RESET;

	if ((rq->errors & ERROR_RESET) == ERROR_RESET) {
		++rq->errors;
		return ide_do_reset(drive);
	}

	if ((rq->errors & ERROR_RECAL) == ERROR_RECAL)
		drive->special.b.recalibrate = 1;

	++rq->errors;

	return ide_stopped;
}

static ide_startstop_t ide_atapi_error(ide_drive_t *drive, struct request *rq, u8 stat, u8 err)
{
	ide_hwif_t *hwif = drive->hwif;

	if (stat & BUSY_STAT || ((stat & WRERR_STAT) && !drive->nowerr)) {
		/* other bits are useless when BUSY */
		rq->errors |= ERROR_RESET;
	} else {
		/* add decoding error stuff */
	}

	if (hwif->INB(IDE_STATUS_REG) & (BUSY_STAT|DRQ_STAT))
		/* force an abort */
		hwif->OUTB(WIN_IDLEIMMEDIATE, IDE_COMMAND_REG);

	if (rq->errors >= ERROR_MAX) {
		ide_kill_rq(drive, rq);
	} else {
		if ((rq->errors & ERROR_RESET) == ERROR_RESET) {
			++rq->errors;
			return ide_do_reset(drive);
		}
		++rq->errors;
	}

	return ide_stopped;
}

ide_startstop_t
__ide_error(ide_drive_t *drive, struct request *rq, u8 stat, u8 err)
{
	if (drive->media == ide_disk)
		return ide_ata_error(drive, rq, stat, err);
	return ide_atapi_error(drive, rq, stat, err);
}

EXPORT_SYMBOL_GPL(__ide_error);

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
 
ide_startstop_t ide_error (ide_drive_t *drive, const char *msg, u8 stat)
{
	struct request *rq;
	u8 err;

	err = ide_dump_status(drive, msg, stat);

	if ((rq = HWGROUP(drive)->rq) == NULL)
		return ide_stopped;

	/* retry only "normal" I/O: */
	if (!blk_fs_request(rq)) {
		rq->errors = 1;
		ide_end_drive_cmd(drive, stat, err);
		return ide_stopped;
	}

	if (rq->rq_disk) {
		ide_driver_t *drv;

		drv = *(ide_driver_t **)rq->rq_disk->private_data;
		return drv->error(drive, rq, stat, err);
	} else
		return __ide_error(drive, rq, stat, err);
}

EXPORT_SYMBOL_GPL(ide_error);

ide_startstop_t __ide_abort(ide_drive_t *drive, struct request *rq)
{
	if (drive->media != ide_disk)
		rq->errors |= ERROR_RESET;

	ide_kill_rq(drive, rq);

	return ide_stopped;
}

EXPORT_SYMBOL_GPL(__ide_abort);

/**
 *	ide_abort	-	abort pending IDE operations
 *	@drive: drive the error occurred on
 *	@msg: message to report
 *
 *	ide_abort kills and cleans up when we are about to do a 
 *	host initiated reset on active commands. Longer term we
 *	want handlers to have sensible abort handling themselves
 *
 *	This differs fundamentally from ide_error because in 
 *	this case the command is doing just fine when we
 *	blow it away.
 */
 
ide_startstop_t ide_abort(ide_drive_t *drive, const char *msg)
{
	struct request *rq;

	if (drive == NULL || (rq = HWGROUP(drive)->rq) == NULL)
		return ide_stopped;

	/* retry only "normal" I/O: */
	if (!blk_fs_request(rq)) {
		rq->errors = 1;
		ide_end_drive_cmd(drive, BUSY_STAT, 0);
		return ide_stopped;
	}

	if (rq->rq_disk) {
		ide_driver_t *drv;

		drv = *(ide_driver_t **)rq->rq_disk->private_data;
		return drv->abort(drive, rq);
	} else
		return __ide_abort(drive, rq);
}

/**
 *	ide_cmd		-	issue a simple drive command
 *	@drive: drive the command is for
 *	@cmd: command byte
 *	@nsect: sector byte
 *	@handler: handler for the command completion
 *
 *	Issue a simple drive command with interrupts.
 *	The drive must be selected beforehand.
 */

static void ide_cmd (ide_drive_t *drive, u8 cmd, u8 nsect,
		ide_handler_t *handler)
{
	ide_hwif_t *hwif = HWIF(drive);
	if (IDE_CONTROL_REG)
		hwif->OUTB(drive->ctl,IDE_CONTROL_REG);	/* clear nIEN */
	SELECT_MASK(drive,0);
	hwif->OUTB(nsect,IDE_NSECTOR_REG);
	ide_execute_command(drive, cmd, handler, WAIT_CMD, NULL);
}

/**
 *	drive_cmd_intr		- 	drive command completion interrupt
 *	@drive: drive the completion interrupt occurred on
 *
 *	drive_cmd_intr() is invoked on completion of a special DRIVE_CMD.
 *	We do any necessary data reading and then wait for the drive to
 *	go non busy. At that point we may read the error data and complete
 *	the request
 */
 
static ide_startstop_t drive_cmd_intr (ide_drive_t *drive)
{
	struct request *rq = HWGROUP(drive)->rq;
	ide_hwif_t *hwif = HWIF(drive);
	u8 *args = (u8 *) rq->buffer;
	u8 stat = hwif->INB(IDE_STATUS_REG);
	int retries = 10;

	local_irq_enable_in_hardirq();
	if ((stat & DRQ_STAT) && args && args[3]) {
		u8 io_32bit = drive->io_32bit;
		drive->io_32bit = 0;
		hwif->ata_input_data(drive, &args[4], args[3] * SECTOR_WORDS);
		drive->io_32bit = io_32bit;
		while (((stat = hwif->INB(IDE_STATUS_REG)) & BUSY_STAT) && retries--)
			udelay(100);
	}

	if (!OK_STAT(stat, READY_STAT, BAD_STAT))
		return ide_error(drive, "drive_cmd", stat);
		/* calls ide_end_drive_cmd */
	ide_end_drive_cmd(drive, stat, hwif->INB(IDE_ERROR_REG));
	return ide_stopped;
}

static void ide_init_specify_cmd(ide_drive_t *drive, ide_task_t *task)
{
	task->tfRegister[IDE_NSECTOR_OFFSET] = drive->sect;
	task->tfRegister[IDE_SECTOR_OFFSET]  = drive->sect;
	task->tfRegister[IDE_LCYL_OFFSET]    = drive->cyl;
	task->tfRegister[IDE_HCYL_OFFSET]    = drive->cyl>>8;
	task->tfRegister[IDE_SELECT_OFFSET]  = ((drive->head-1)|drive->select.all)&0xBF;
	task->tfRegister[IDE_COMMAND_OFFSET] = WIN_SPECIFY;

	task->handler = &set_geometry_intr;
}

static void ide_init_restore_cmd(ide_drive_t *drive, ide_task_t *task)
{
	task->tfRegister[IDE_NSECTOR_OFFSET] = drive->sect;
	task->tfRegister[IDE_COMMAND_OFFSET] = WIN_RESTORE;

	task->handler = &recal_intr;
}

static void ide_init_setmult_cmd(ide_drive_t *drive, ide_task_t *task)
{
	task->tfRegister[IDE_NSECTOR_OFFSET] = drive->mult_req;
	task->tfRegister[IDE_COMMAND_OFFSET] = WIN_SETMULT;

	task->handler = &set_multmode_intr;
}

static ide_startstop_t ide_disk_special(ide_drive_t *drive)
{
	special_t *s = &drive->special;
	ide_task_t args;

	memset(&args, 0, sizeof(ide_task_t));
	args.command_type = IDE_DRIVE_TASK_NO_DATA;

	if (s->b.set_geometry) {
		s->b.set_geometry = 0;
		ide_init_specify_cmd(drive, &args);
	} else if (s->b.recalibrate) {
		s->b.recalibrate = 0;
		ide_init_restore_cmd(drive, &args);
	} else if (s->b.set_multmode) {
		s->b.set_multmode = 0;
		if (drive->mult_req > drive->id->max_multsect)
			drive->mult_req = drive->id->max_multsect;
		ide_init_setmult_cmd(drive, &args);
	} else if (s->all) {
		int special = s->all;
		s->all = 0;
		printk(KERN_ERR "%s: bad special flag: 0x%02x\n", drive->name, special);
		return ide_stopped;
	}

	do_rw_taskfile(drive, &args);

	return ide_started;
}

/**
 *	do_special		-	issue some special commands
 *	@drive: drive the command is for
 *
 *	do_special() is used to issue WIN_SPECIFY, WIN_RESTORE, and WIN_SETMULT
 *	commands to a drive.  It used to do much more, but has been scaled
 *	back.
 */

static ide_startstop_t do_special (ide_drive_t *drive)
{
	special_t *s = &drive->special;

#ifdef DEBUG
	printk("%s: do_special: 0x%02x\n", drive->name, s->all);
#endif
	if (s->b.set_tune) {
		s->b.set_tune = 0;
		if (HWIF(drive)->tuneproc != NULL)
			HWIF(drive)->tuneproc(drive, drive->tune_req);
		return ide_stopped;
	} else {
		if (drive->media == ide_disk)
			return ide_disk_special(drive);

		s->all = 0;
		drive->mult_req = 0;
		return ide_stopped;
	}
}

void ide_map_sg(ide_drive_t *drive, struct request *rq)
{
	ide_hwif_t *hwif = drive->hwif;
	struct scatterlist *sg = hwif->sg_table;

	if (hwif->sg_mapped)	/* needed by ide-scsi */
		return;

	if (rq->cmd_type != REQ_TYPE_ATA_TASKFILE) {
		hwif->sg_nents = blk_rq_map_sg(drive->queue, rq, sg);
	} else {
		sg_init_one(sg, rq->buffer, rq->nr_sectors * SECTOR_SIZE);
		hwif->sg_nents = 1;
	}
}

EXPORT_SYMBOL_GPL(ide_map_sg);

void ide_init_sg_cmd(ide_drive_t *drive, struct request *rq)
{
	ide_hwif_t *hwif = drive->hwif;

	hwif->nsect = hwif->nleft = rq->nr_sectors;
	hwif->cursg = hwif->cursg_ofs = 0;
}

EXPORT_SYMBOL_GPL(ide_init_sg_cmd);

/**
 *	execute_drive_command	-	issue special drive command
 *	@drive: the drive to issue the command on
 *	@rq: the request structure holding the command
 *
 *	execute_drive_cmd() issues a special drive command,  usually 
 *	initiated by ioctl() from the external hdparm program. The
 *	command can be a drive command, drive task or taskfile 
 *	operation. Weirdly you can call it with NULL to wait for
 *	all commands to finish. Don't do this as that is due to change
 */

static ide_startstop_t execute_drive_cmd (ide_drive_t *drive,
		struct request *rq)
{
	ide_hwif_t *hwif = HWIF(drive);
	if (rq->cmd_type == REQ_TYPE_ATA_TASKFILE) {
 		ide_task_t *args = rq->special;
 
		if (!args)
			goto done;

		hwif->data_phase = args->data_phase;

		switch (hwif->data_phase) {
		case TASKFILE_MULTI_OUT:
		case TASKFILE_OUT:
		case TASKFILE_MULTI_IN:
		case TASKFILE_IN:
			ide_init_sg_cmd(drive, rq);
			ide_map_sg(drive, rq);
		default:
			break;
		}

		if (args->tf_out_flags.all != 0) 
			return flagged_taskfile(drive, args);
		return do_rw_taskfile(drive, args);
	} else if (rq->cmd_type == REQ_TYPE_ATA_TASK) {
		u8 *args = rq->buffer;
		u8 sel;
 
		if (!args)
			goto done;
#ifdef DEBUG
 		printk("%s: DRIVE_TASK_CMD ", drive->name);
 		printk("cmd=0x%02x ", args[0]);
 		printk("fr=0x%02x ", args[1]);
 		printk("ns=0x%02x ", args[2]);
 		printk("sc=0x%02x ", args[3]);
 		printk("lcyl=0x%02x ", args[4]);
 		printk("hcyl=0x%02x ", args[5]);
 		printk("sel=0x%02x\n", args[6]);
#endif
 		hwif->OUTB(args[1], IDE_FEATURE_REG);
 		hwif->OUTB(args[3], IDE_SECTOR_REG);
 		hwif->OUTB(args[4], IDE_LCYL_REG);
 		hwif->OUTB(args[5], IDE_HCYL_REG);
 		sel = (args[6] & ~0x10);
 		if (drive->select.b.unit)
 			sel |= 0x10;
 		hwif->OUTB(sel, IDE_SELECT_REG);
 		ide_cmd(drive, args[0], args[2], &drive_cmd_intr);
 		return ide_started;
 	} else if (rq->cmd_type == REQ_TYPE_ATA_CMD) {
 		u8 *args = rq->buffer;

		if (!args)
			goto done;
#ifdef DEBUG
 		printk("%s: DRIVE_CMD ", drive->name);
 		printk("cmd=0x%02x ", args[0]);
 		printk("sc=0x%02x ", args[1]);
 		printk("fr=0x%02x ", args[2]);
 		printk("xx=0x%02x\n", args[3]);
#endif
 		if (args[0] == WIN_SMART) {
 			hwif->OUTB(0x4f, IDE_LCYL_REG);
 			hwif->OUTB(0xc2, IDE_HCYL_REG);
 			hwif->OUTB(args[2],IDE_FEATURE_REG);
 			hwif->OUTB(args[1],IDE_SECTOR_REG);
 			ide_cmd(drive, args[0], args[3], &drive_cmd_intr);
 			return ide_started;
 		}
 		hwif->OUTB(args[2],IDE_FEATURE_REG);
 		ide_cmd(drive, args[0], args[1], &drive_cmd_intr);
 		return ide_started;
 	}

done:
 	/*
 	 * NULL is actually a valid way of waiting for
 	 * all current requests to be flushed from the queue.
 	 */
#ifdef DEBUG
 	printk("%s: DRIVE_CMD (null)\n", drive->name);
#endif
 	ide_end_drive_cmd(drive,
			hwif->INB(IDE_STATUS_REG),
			hwif->INB(IDE_ERROR_REG));
 	return ide_stopped;
}

static void ide_check_pm_state(ide_drive_t *drive, struct request *rq)
{
	struct request_pm_state *pm = rq->data;

	if (blk_pm_suspend_request(rq) &&
	    pm->pm_step == ide_pm_state_start_suspend)
		/* Mark drive blocked when starting the suspend sequence. */
		drive->blocked = 1;
	else if (blk_pm_resume_request(rq) &&
		 pm->pm_step == ide_pm_state_start_resume) {
		/* 
		 * The first thing we do on wakeup is to wait for BSY bit to
		 * go away (with a looong timeout) as a drive on this hwif may
		 * just be POSTing itself.
		 * We do that before even selecting as the "other" device on
		 * the bus may be broken enough to walk on our toes at this
		 * point.
		 */
		int rc;
#ifdef DEBUG_PM
		printk("%s: Wakeup request inited, waiting for !BSY...\n", drive->name);
#endif
		rc = ide_wait_not_busy(HWIF(drive), 35000);
		if (rc)
			printk(KERN_WARNING "%s: bus not ready on wakeup\n", drive->name);
		SELECT_DRIVE(drive);
		HWIF(drive)->OUTB(8, HWIF(drive)->io_ports[IDE_CONTROL_OFFSET]);
		rc = ide_wait_not_busy(HWIF(drive), 100000);
		if (rc)
			printk(KERN_WARNING "%s: drive not ready on wakeup\n", drive->name);
	}
}

/**
 *	start_request	-	start of I/O and command issuing for IDE
 *
 *	start_request() initiates handling of a new I/O request. It
 *	accepts commands and I/O (read/write) requests. It also does
 *	the final remapping for weird stuff like EZDrive. Once 
 *	device mapper can work sector level the EZDrive stuff can go away
 *
 *	FIXME: this function needs a rename
 */
 
static ide_startstop_t start_request (ide_drive_t *drive, struct request *rq)
{
	ide_startstop_t startstop;
	sector_t block;

	BUG_ON(!blk_rq_started(rq));

#ifdef DEBUG
	printk("%s: start_request: current=0x%08lx\n",
		HWIF(drive)->name, (unsigned long) rq);
#endif

	/* bail early if we've exceeded max_failures */
	if (drive->max_failures && (drive->failures > drive->max_failures)) {
		goto kill_rq;
	}

	block    = rq->sector;
	if (blk_fs_request(rq) &&
	    (drive->media == ide_disk || drive->media == ide_floppy)) {
		block += drive->sect0;
	}
	/* Yecch - this will shift the entire interval,
	   possibly killing some innocent following sector */
	if (block == 0 && drive->remap_0_to_1 == 1)
		block = 1;  /* redirect MBR access to EZ-Drive partn table */

	if (blk_pm_request(rq))
		ide_check_pm_state(drive, rq);

	SELECT_DRIVE(drive);
	if (ide_wait_stat(&startstop, drive, drive->ready_stat, BUSY_STAT|DRQ_STAT, WAIT_READY)) {
		printk(KERN_ERR "%s: drive not ready for command\n", drive->name);
		return startstop;
	}
	if (!drive->special.all) {
		ide_driver_t *drv;

		/*
		 * We reset the drive so we need to issue a SETFEATURES.
		 * Do it _after_ do_special() restored device parameters.
		 */
		if (drive->current_speed == 0xff)
			ide_config_drive_speed(drive, drive->desired_speed);

		if (rq->cmd_type == REQ_TYPE_ATA_CMD ||
		    rq->cmd_type == REQ_TYPE_ATA_TASK ||
		    rq->cmd_type == REQ_TYPE_ATA_TASKFILE)
			return execute_drive_cmd(drive, rq);
		else if (blk_pm_request(rq)) {
			struct request_pm_state *pm = rq->data;
#ifdef DEBUG_PM
			printk("%s: start_power_step(step: %d)\n",
				drive->name, rq->pm->pm_step);
#endif
			startstop = ide_start_power_step(drive, rq);
			if (startstop == ide_stopped &&
			    pm->pm_step == ide_pm_state_completed)
				ide_complete_pm_request(drive, rq);
			return startstop;
		}

		drv = *(ide_driver_t **)rq->rq_disk->private_data;
		return drv->do_request(drive, rq, block);
	}
	return do_special(drive);
kill_rq:
	ide_kill_rq(drive, rq);
	return ide_stopped;
}

/**
 *	ide_stall_queue		-	pause an IDE device
 *	@drive: drive to stall
 *	@timeout: time to stall for (jiffies)
 *
 *	ide_stall_queue() can be used by a drive to give excess bandwidth back
 *	to the hwgroup by sleeping for timeout jiffies.
 */
 
void ide_stall_queue (ide_drive_t *drive, unsigned long timeout)
{
	if (timeout > WAIT_WORSTCASE)
		timeout = WAIT_WORSTCASE;
	drive->sleep = timeout + jiffies;
	drive->sleeping = 1;
}

EXPORT_SYMBOL(ide_stall_queue);

#define WAKEUP(drive)	((drive)->service_start + 2 * (drive)->service_time)

/**
 *	choose_drive		-	select a drive to service
 *	@hwgroup: hardware group to select on
 *
 *	choose_drive() selects the next drive which will be serviced.
 *	This is necessary because the IDE layer can't issue commands
 *	to both drives on the same cable, unlike SCSI.
 */
 
static inline ide_drive_t *choose_drive (ide_hwgroup_t *hwgroup)
{
	ide_drive_t *drive, *best;

repeat:	
	best = NULL;
	drive = hwgroup->drive;

	/*
	 * drive is doing pre-flush, ordered write, post-flush sequence. even
	 * though that is 3 requests, it must be seen as a single transaction.
	 * we must not preempt this drive until that is complete
	 */
	if (blk_queue_flushing(drive->queue)) {
		/*
		 * small race where queue could get replugged during
		 * the 3-request flush cycle, just yank the plug since
		 * we want it to finish asap
		 */
		blk_remove_plug(drive->queue);
		return drive;
	}

	do {
		if ((!drive->sleeping || time_after_eq(jiffies, drive->sleep))
		    && !elv_queue_empty(drive->queue)) {
			if (!best
			 || (drive->sleeping && (!best->sleeping || time_before(drive->sleep, best->sleep)))
			 || (!best->sleeping && time_before(WAKEUP(drive), WAKEUP(best))))
			{
				if (!blk_queue_plugged(drive->queue))
					best = drive;
			}
		}
	} while ((drive = drive->next) != hwgroup->drive);
	if (best && best->nice1 && !best->sleeping && best != hwgroup->drive && best->service_time > WAIT_MIN_SLEEP) {
		long t = (signed long)(WAKEUP(best) - jiffies);
		if (t >= WAIT_MIN_SLEEP) {
		/*
		 * We *may* have some time to spare, but first let's see if
		 * someone can potentially benefit from our nice mood today..
		 */
			drive = best->next;
			do {
				if (!drive->sleeping
				 && time_before(jiffies - best->service_time, WAKEUP(drive))
				 && time_before(WAKEUP(drive), jiffies + t))
				{
					ide_stall_queue(best, min_t(long, t, 10 * WAIT_MIN_SLEEP));
					goto repeat;
				}
			} while ((drive = drive->next) != best);
		}
	}
	return best;
}

/*
 * Issue a new request to a drive from hwgroup
 * Caller must have already done spin_lock_irqsave(&ide_lock, ..);
 *
 * A hwgroup is a serialized group of IDE interfaces.  Usually there is
 * exactly one hwif (interface) per hwgroup, but buggy controllers (eg. CMD640)
 * may have both interfaces in a single hwgroup to "serialize" access.
 * Or possibly multiple ISA interfaces can share a common IRQ by being grouped
 * together into one hwgroup for serialized access.
 *
 * Note also that several hwgroups can end up sharing a single IRQ,
 * possibly along with many other devices.  This is especially common in
 * PCI-based systems with off-board IDE controller cards.
 *
 * The IDE driver uses the single global ide_lock spinlock to protect
 * access to the request queues, and to protect the hwgroup->busy flag.
 *
 * The first thread into the driver for a particular hwgroup sets the
 * hwgroup->busy flag to indicate that this hwgroup is now active,
 * and then initiates processing of the top request from the request queue.
 *
 * Other threads attempting entry notice the busy setting, and will simply
 * queue their new requests and exit immediately.  Note that hwgroup->busy
 * remains set even when the driver is merely awaiting the next interrupt.
 * Thus, the meaning is "this hwgroup is busy processing a request".
 *
 * When processing of a request completes, the completing thread or IRQ-handler
 * will start the next request from the queue.  If no more work remains,
 * the driver will clear the hwgroup->busy flag and exit.
 *
 * The ide_lock (spinlock) is used to protect all access to the
 * hwgroup->busy flag, but is otherwise not needed for most processing in
 * the driver.  This makes the driver much more friendlier to shared IRQs
 * than previous designs, while remaining 100% (?) SMP safe and capable.
 */
static void ide_do_request (ide_hwgroup_t *hwgroup, int masked_irq)
{
	ide_drive_t	*drive;
	ide_hwif_t	*hwif;
	struct request	*rq;
	ide_startstop_t	startstop;
	int             loops = 0;

	/* for atari only: POSSIBLY BROKEN HERE(?) */
	ide_get_lock(ide_intr, hwgroup);

	/* caller must own ide_lock */
	BUG_ON(!irqs_disabled());

	while (!hwgroup->busy) {
		hwgroup->busy = 1;
		drive = choose_drive(hwgroup);
		if (drive == NULL) {
			int sleeping = 0;
			unsigned long sleep = 0; /* shut up, gcc */
			hwgroup->rq = NULL;
			drive = hwgroup->drive;
			do {
				if (drive->sleeping && (!sleeping || time_before(drive->sleep, sleep))) {
					sleeping = 1;
					sleep = drive->sleep;
				}
			} while ((drive = drive->next) != hwgroup->drive);
			if (sleeping) {
		/*
		 * Take a short snooze, and then wake up this hwgroup again.
		 * This gives other hwgroups on the same a chance to
		 * play fairly with us, just in case there are big differences
		 * in relative throughputs.. don't want to hog the cpu too much.
		 */
				if (time_before(sleep, jiffies + WAIT_MIN_SLEEP))
					sleep = jiffies + WAIT_MIN_SLEEP;
#if 1
				if (timer_pending(&hwgroup->timer))
					printk(KERN_CRIT "ide_set_handler: timer already active\n");
#endif
				/* so that ide_timer_expiry knows what to do */
				hwgroup->sleeping = 1;
				hwgroup->req_gen_timer = hwgroup->req_gen;
				mod_timer(&hwgroup->timer, sleep);
				/* we purposely leave hwgroup->busy==1
				 * while sleeping */
			} else {
				/* Ugly, but how can we sleep for the lock
				 * otherwise? perhaps from tq_disk?
				 */

				/* for atari only */
				ide_release_lock();
				hwgroup->busy = 0;
			}

			/* no more work for this hwgroup (for now) */
			return;
		}
	again:
		hwif = HWIF(drive);
		if (hwgroup->hwif->sharing_irq &&
		    hwif != hwgroup->hwif &&
		    hwif->io_ports[IDE_CONTROL_OFFSET]) {
			/* set nIEN for previous hwif */
			SELECT_INTERRUPT(drive);
		}
		hwgroup->hwif = hwif;
		hwgroup->drive = drive;
		drive->sleeping = 0;
		drive->service_start = jiffies;

		if (blk_queue_plugged(drive->queue)) {
			printk(KERN_ERR "ide: huh? queue was plugged!\n");
			break;
		}

		/*
		 * we know that the queue isn't empty, but this can happen
		 * if the q->prep_rq_fn() decides to kill a request
		 */
		rq = elv_next_request(drive->queue);
		if (!rq) {
			hwgroup->busy = 0;
			break;
		}

		/*
		 * Sanity: don't accept a request that isn't a PM request
		 * if we are currently power managed. This is very important as
		 * blk_stop_queue() doesn't prevent the elv_next_request()
		 * above to return us whatever is in the queue. Since we call
		 * ide_do_request() ourselves, we end up taking requests while
		 * the queue is blocked...
		 * 
		 * We let requests forced at head of queue with ide-preempt
		 * though. I hope that doesn't happen too much, hopefully not
		 * unless the subdriver triggers such a thing in its own PM
		 * state machine.
		 *
		 * We count how many times we loop here to make sure we service
		 * all drives in the hwgroup without looping for ever
		 */
		if (drive->blocked && !blk_pm_request(rq) && !(rq->cmd_flags & REQ_PREEMPT)) {
			drive = drive->next ? drive->next : hwgroup->drive;
			if (loops++ < 4 && !blk_queue_plugged(drive->queue))
				goto again;
			/* We clear busy, there should be no pending ATA command at this point. */
			hwgroup->busy = 0;
			break;
		}

		hwgroup->rq = rq;

		/*
		 * Some systems have trouble with IDE IRQs arriving while
		 * the driver is still setting things up.  So, here we disable
		 * the IRQ used by this interface while the request is being started.
		 * This may look bad at first, but pretty much the same thing
		 * happens anyway when any interrupt comes in, IDE or otherwise
		 *  -- the kernel masks the IRQ while it is being handled.
		 */
		if (masked_irq != IDE_NO_IRQ && hwif->irq != masked_irq)
			disable_irq_nosync(hwif->irq);
		spin_unlock(&ide_lock);
		local_irq_enable_in_hardirq();
			/* allow other IRQs while we start this request */
		startstop = start_request(drive, rq);
		spin_lock_irq(&ide_lock);
		if (masked_irq != IDE_NO_IRQ && hwif->irq != masked_irq)
			enable_irq(hwif->irq);
		if (startstop == ide_stopped)
			hwgroup->busy = 0;
	}
}

/*
 * Passes the stuff to ide_do_request
 */
void do_ide_request(request_queue_t *q)
{
	ide_drive_t *drive = q->queuedata;

	ide_do_request(HWGROUP(drive), IDE_NO_IRQ);
}

/*
 * un-busy the hwgroup etc, and clear any pending DMA status. we want to
 * retry the current request in pio mode instead of risking tossing it
 * all away
 */
static ide_startstop_t ide_dma_timeout_retry(ide_drive_t *drive, int error)
{
	ide_hwif_t *hwif = HWIF(drive);
	struct request *rq;
	ide_startstop_t ret = ide_stopped;

	/*
	 * end current dma transaction
	 */

	if (error < 0) {
		printk(KERN_WARNING "%s: DMA timeout error\n", drive->name);
		(void)HWIF(drive)->ide_dma_end(drive);
		ret = ide_error(drive, "dma timeout error",
						hwif->INB(IDE_STATUS_REG));
	} else {
		printk(KERN_WARNING "%s: DMA timeout retry\n", drive->name);
		hwif->dma_timeout(drive);
	}

	/*
	 * disable dma for now, but remember that we did so because of
	 * a timeout -- we'll reenable after we finish this next request
	 * (or rather the first chunk of it) in pio.
	 */
	drive->retry_pio++;
	drive->state = DMA_PIO_RETRY;
	hwif->dma_off_quietly(drive);

	/*
	 * un-busy drive etc (hwgroup->busy is cleared on return) and
	 * make sure request is sane
	 */
	rq = HWGROUP(drive)->rq;

	if (!rq)
		goto out;

	HWGROUP(drive)->rq = NULL;

	rq->errors = 0;

	if (!rq->bio)
		goto out;

	rq->sector = rq->bio->bi_sector;
	rq->current_nr_sectors = bio_iovec(rq->bio)->bv_len >> 9;
	rq->hard_cur_sectors = rq->current_nr_sectors;
	rq->buffer = bio_data(rq->bio);
out:
	return ret;
}

/**
 *	ide_timer_expiry	-	handle lack of an IDE interrupt
 *	@data: timer callback magic (hwgroup)
 *
 *	An IDE command has timed out before the expected drive return
 *	occurred. At this point we attempt to clean up the current
 *	mess. If the current handler includes an expiry handler then
 *	we invoke the expiry handler, and providing it is happy the
 *	work is done. If that fails we apply generic recovery rules
 *	invoking the handler and checking the drive DMA status. We
 *	have an excessively incestuous relationship with the DMA
 *	logic that wants cleaning up.
 */
 
void ide_timer_expiry (unsigned long data)
{
	ide_hwgroup_t	*hwgroup = (ide_hwgroup_t *) data;
	ide_handler_t	*handler;
	ide_expiry_t	*expiry;
	unsigned long	flags;
	unsigned long	wait = -1;

	spin_lock_irqsave(&ide_lock, flags);

	if (((handler = hwgroup->handler) == NULL) ||
	    (hwgroup->req_gen != hwgroup->req_gen_timer)) {
		/*
		 * Either a marginal timeout occurred
		 * (got the interrupt just as timer expired),
		 * or we were "sleeping" to give other devices a chance.
		 * Either way, we don't really want to complain about anything.
		 */
		if (hwgroup->sleeping) {
			hwgroup->sleeping = 0;
			hwgroup->busy = 0;
		}
	} else {
		ide_drive_t *drive = hwgroup->drive;
		if (!drive) {
			printk(KERN_ERR "ide_timer_expiry: hwgroup->drive was NULL\n");
			hwgroup->handler = NULL;
		} else {
			ide_hwif_t *hwif;
			ide_startstop_t startstop = ide_stopped;
			if (!hwgroup->busy) {
				hwgroup->busy = 1;	/* paranoia */
				printk(KERN_ERR "%s: ide_timer_expiry: hwgroup->busy was 0 ??\n", drive->name);
			}
			if ((expiry = hwgroup->expiry) != NULL) {
				/* continue */
				if ((wait = expiry(drive)) > 0) {
					/* reset timer */
					hwgroup->timer.expires  = jiffies + wait;
					hwgroup->req_gen_timer = hwgroup->req_gen;
					add_timer(&hwgroup->timer);
					spin_unlock_irqrestore(&ide_lock, flags);
					return;
				}
			}
			hwgroup->handler = NULL;
			/*
			 * We need to simulate a real interrupt when invoking
			 * the handler() function, which means we need to
			 * globally mask the specific IRQ:
			 */
			spin_unlock(&ide_lock);
			hwif  = HWIF(drive);
#if DISABLE_IRQ_NOSYNC
			disable_irq_nosync(hwif->irq);
#else
			/* disable_irq_nosync ?? */
			disable_irq(hwif->irq);
#endif /* DISABLE_IRQ_NOSYNC */
			/* local CPU only,
			 * as if we were handling an interrupt */
			local_irq_disable();
			if (hwgroup->polling) {
				startstop = handler(drive);
			} else if (drive_is_ready(drive)) {
				if (drive->waiting_for_dma)
					hwgroup->hwif->dma_lost_irq(drive);
				(void)ide_ack_intr(hwif);
				printk(KERN_WARNING "%s: lost interrupt\n", drive->name);
				startstop = handler(drive);
			} else {
				if (drive->waiting_for_dma) {
					startstop = ide_dma_timeout_retry(drive, wait);
				} else
					startstop =
					ide_error(drive, "irq timeout", hwif->INB(IDE_STATUS_REG));
			}
			drive->service_time = jiffies - drive->service_start;
			spin_lock_irq(&ide_lock);
			enable_irq(hwif->irq);
			if (startstop == ide_stopped)
				hwgroup->busy = 0;
		}
	}
	ide_do_request(hwgroup, IDE_NO_IRQ);
	spin_unlock_irqrestore(&ide_lock, flags);
}

/**
 *	unexpected_intr		-	handle an unexpected IDE interrupt
 *	@irq: interrupt line
 *	@hwgroup: hwgroup being processed
 *
 *	There's nothing really useful we can do with an unexpected interrupt,
 *	other than reading the status register (to clear it), and logging it.
 *	There should be no way that an irq can happen before we're ready for it,
 *	so we needn't worry much about losing an "important" interrupt here.
 *
 *	On laptops (and "green" PCs), an unexpected interrupt occurs whenever
 *	the drive enters "idle", "standby", or "sleep" mode, so if the status
 *	looks "good", we just ignore the interrupt completely.
 *
 *	This routine assumes __cli() is in effect when called.
 *
 *	If an unexpected interrupt happens on irq15 while we are handling irq14
 *	and if the two interfaces are "serialized" (CMD640), then it looks like
 *	we could screw up by interfering with a new request being set up for 
 *	irq15.
 *
 *	In reality, this is a non-issue.  The new command is not sent unless 
 *	the drive is ready to accept one, in which case we know the drive is
 *	not trying to interrupt us.  And ide_set_handler() is always invoked
 *	before completing the issuance of any new drive command, so we will not
 *	be accidentally invoked as a result of any valid command completion
 *	interrupt.
 *
 *	Note that we must walk the entire hwgroup here. We know which hwif
 *	is doing the current command, but we don't know which hwif burped
 *	mysteriously.
 */
 
static void unexpected_intr (int irq, ide_hwgroup_t *hwgroup)
{
	u8 stat;
	ide_hwif_t *hwif = hwgroup->hwif;

	/*
	 * handle the unexpected interrupt
	 */
	do {
		if (hwif->irq == irq) {
			stat = hwif->INB(hwif->io_ports[IDE_STATUS_OFFSET]);
			if (!OK_STAT(stat, READY_STAT, BAD_STAT)) {
				/* Try to not flood the console with msgs */
				static unsigned long last_msgtime, count;
				++count;
				if (time_after(jiffies, last_msgtime + HZ)) {
					last_msgtime = jiffies;
					printk(KERN_ERR "%s%s: unexpected interrupt, "
						"status=0x%02x, count=%ld\n",
						hwif->name,
						(hwif->next==hwgroup->hwif) ? "" : "(?)", stat, count);
				}
			}
		}
	} while ((hwif = hwif->next) != hwgroup->hwif);
}

/**
 *	ide_intr	-	default IDE interrupt handler
 *	@irq: interrupt number
 *	@dev_id: hwif group
 *	@regs: unused weirdness from the kernel irq layer
 *
 *	This is the default IRQ handler for the IDE layer. You should
 *	not need to override it. If you do be aware it is subtle in
 *	places
 *
 *	hwgroup->hwif is the interface in the group currently performing
 *	a command. hwgroup->drive is the drive and hwgroup->handler is
 *	the IRQ handler to call. As we issue a command the handlers
 *	step through multiple states, reassigning the handler to the
 *	next step in the process. Unlike a smart SCSI controller IDE
 *	expects the main processor to sequence the various transfer
 *	stages. We also manage a poll timer to catch up with most
 *	timeout situations. There are still a few where the handlers
 *	don't ever decide to give up.
 *
 *	The handler eventually returns ide_stopped to indicate the
 *	request completed. At this point we issue the next request
 *	on the hwgroup and the process begins again.
 */
 
irqreturn_t ide_intr (int irq, void *dev_id)
{
	unsigned long flags;
	ide_hwgroup_t *hwgroup = (ide_hwgroup_t *)dev_id;
	ide_hwif_t *hwif;
	ide_drive_t *drive;
	ide_handler_t *handler;
	ide_startstop_t startstop;

	spin_lock_irqsave(&ide_lock, flags);
	hwif = hwgroup->hwif;

	if (!ide_ack_intr(hwif)) {
		spin_unlock_irqrestore(&ide_lock, flags);
		return IRQ_NONE;
	}

	if ((handler = hwgroup->handler) == NULL || hwgroup->polling) {
		/*
		 * Not expecting an interrupt from this drive.
		 * That means this could be:
		 *	(1) an interrupt from another PCI device
		 *	sharing the same PCI INT# as us.
		 * or	(2) a drive just entered sleep or standby mode,
		 *	and is interrupting to let us know.
		 * or	(3) a spurious interrupt of unknown origin.
		 *
		 * For PCI, we cannot tell the difference,
		 * so in that case we just ignore it and hope it goes away.
		 *
		 * FIXME: unexpected_intr should be hwif-> then we can
		 * remove all the ifdef PCI crap
		 */
#ifdef CONFIG_BLK_DEV_IDEPCI
		if (hwif->pci_dev && !hwif->pci_dev->vendor)
#endif	/* CONFIG_BLK_DEV_IDEPCI */
		{
			/*
			 * Probably not a shared PCI interrupt,
			 * so we can safely try to do something about it:
			 */
			unexpected_intr(irq, hwgroup);
#ifdef CONFIG_BLK_DEV_IDEPCI
		} else {
			/*
			 * Whack the status register, just in case
			 * we have a leftover pending IRQ.
			 */
			(void) hwif->INB(hwif->io_ports[IDE_STATUS_OFFSET]);
#endif /* CONFIG_BLK_DEV_IDEPCI */
		}
		spin_unlock_irqrestore(&ide_lock, flags);
		return IRQ_NONE;
	}
	drive = hwgroup->drive;
	if (!drive) {
		/*
		 * This should NEVER happen, and there isn't much
		 * we could do about it here.
		 *
		 * [Note - this can occur if the drive is hot unplugged]
		 */
		spin_unlock_irqrestore(&ide_lock, flags);
		return IRQ_HANDLED;
	}
	if (!drive_is_ready(drive)) {
		/*
		 * This happens regularly when we share a PCI IRQ with
		 * another device.  Unfortunately, it can also happen
		 * with some buggy drives that trigger the IRQ before
		 * their status register is up to date.  Hopefully we have
		 * enough advance overhead that the latter isn't a problem.
		 */
		spin_unlock_irqrestore(&ide_lock, flags);
		return IRQ_NONE;
	}
	if (!hwgroup->busy) {
		hwgroup->busy = 1;	/* paranoia */
		printk(KERN_ERR "%s: ide_intr: hwgroup->busy was 0 ??\n", drive->name);
	}
	hwgroup->handler = NULL;
	hwgroup->req_gen++;
	del_timer(&hwgroup->timer);
	spin_unlock(&ide_lock);

	/* Some controllers might set DMA INTR no matter DMA or PIO;
	 * bmdma status might need to be cleared even for
	 * PIO interrupts to prevent spurious/lost irq.
	 */
	if (hwif->ide_dma_clear_irq && !(drive->waiting_for_dma))
		/* ide_dma_end() needs bmdma status for error checking.
		 * So, skip clearing bmdma status here and leave it
		 * to ide_dma_end() if this is dma interrupt.
		 */
		hwif->ide_dma_clear_irq(drive);

	if (drive->unmask)
		local_irq_enable_in_hardirq();
	/* service this interrupt, may set handler for next interrupt */
	startstop = handler(drive);
	spin_lock_irq(&ide_lock);

	/*
	 * Note that handler() may have set things up for another
	 * interrupt to occur soon, but it cannot happen until
	 * we exit from this routine, because it will be the
	 * same irq as is currently being serviced here, and Linux
	 * won't allow another of the same (on any CPU) until we return.
	 */
	drive->service_time = jiffies - drive->service_start;
	if (startstop == ide_stopped) {
		if (hwgroup->handler == NULL) {	/* paranoia */
			hwgroup->busy = 0;
			ide_do_request(hwgroup, hwif->irq);
		} else {
			printk(KERN_ERR "%s: ide_intr: huh? expected NULL handler "
				"on exit\n", drive->name);
		}
	}
	spin_unlock_irqrestore(&ide_lock, flags);
	return IRQ_HANDLED;
}

/**
 *	ide_init_drive_cmd	-	initialize a drive command request
 *	@rq: request object
 *
 *	Initialize a request before we fill it in and send it down to
 *	ide_do_drive_cmd. Commands must be set up by this function. Right
 *	now it doesn't do a lot, but if that changes abusers will have a
 *	nasty surprise.
 */

void ide_init_drive_cmd (struct request *rq)
{
	memset(rq, 0, sizeof(*rq));
	rq->cmd_type = REQ_TYPE_ATA_CMD;
	rq->ref_count = 1;
}

EXPORT_SYMBOL(ide_init_drive_cmd);

/**
 *	ide_do_drive_cmd	-	issue IDE special command
 *	@drive: device to issue command
 *	@rq: request to issue
 *	@action: action for processing
 *
 *	This function issues a special IDE device request
 *	onto the request queue.
 *
 *	If action is ide_wait, then the rq is queued at the end of the
 *	request queue, and the function sleeps until it has been processed.
 *	This is for use when invoked from an ioctl handler.
 *
 *	If action is ide_preempt, then the rq is queued at the head of
 *	the request queue, displacing the currently-being-processed
 *	request and this function returns immediately without waiting
 *	for the new rq to be completed.  This is VERY DANGEROUS, and is
 *	intended for careful use by the ATAPI tape/cdrom driver code.
 *
 *	If action is ide_end, then the rq is queued at the end of the
 *	request queue, and the function returns immediately without waiting
 *	for the new rq to be completed. This is again intended for careful
 *	use by the ATAPI tape/cdrom driver code.
 */
 
int ide_do_drive_cmd (ide_drive_t *drive, struct request *rq, ide_action_t action)
{
	unsigned long flags;
	ide_hwgroup_t *hwgroup = HWGROUP(drive);
	DECLARE_COMPLETION_ONSTACK(wait);
	int where = ELEVATOR_INSERT_BACK, err;
	int must_wait = (action == ide_wait || action == ide_head_wait);

	rq->errors = 0;

	/*
	 * we need to hold an extra reference to request for safe inspection
	 * after completion
	 */
	if (must_wait) {
		rq->ref_count++;
		rq->end_io_data = &wait;
		rq->end_io = blk_end_sync_rq;
	}

	spin_lock_irqsave(&ide_lock, flags);
	if (action == ide_preempt)
		hwgroup->rq = NULL;
	if (action == ide_preempt || action == ide_head_wait) {
		where = ELEVATOR_INSERT_FRONT;
		rq->cmd_flags |= REQ_PREEMPT;
	}
	__elv_add_request(drive->queue, rq, where, 0);
	ide_do_request(hwgroup, IDE_NO_IRQ);
	spin_unlock_irqrestore(&ide_lock, flags);

	err = 0;
	if (must_wait) {
		wait_for_completion(&wait);
		if (rq->errors)
			err = -EIO;

		blk_put_request(rq);
	}

	return err;
}

EXPORT_SYMBOL(ide_do_drive_cmd);
