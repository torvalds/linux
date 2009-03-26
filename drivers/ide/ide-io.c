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
#include <linux/hdreg.h>
#include <linux/completion.h>
#include <linux/reboot.h>
#include <linux/cdrom.h>
#include <linux/seq_file.h>
#include <linux/device.h>
#include <linux/kmod.h>
#include <linux/scatterlist.h>
#include <linux/bitops.h>

#include <asm/byteorder.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/io.h>

static int __ide_end_request(ide_drive_t *drive, struct request *rq,
			     int uptodate, unsigned int nr_bytes, int dequeue)
{
	int ret = 1;
	int error = 0;

	if (uptodate <= 0)
		error = uptodate ? uptodate : -EIO;

	/*
	 * if failfast is set on a request, override number of sectors and
	 * complete the whole request right now
	 */
	if (blk_noretry_request(rq) && error)
		nr_bytes = rq->hard_nr_sectors << 9;

	if (!blk_fs_request(rq) && error && !rq->errors)
		rq->errors = -EIO;

	/*
	 * decide whether to reenable DMA -- 3 is a random magic for now,
	 * if we DMA timeout more than 3 times, just stay in PIO
	 */
	if ((drive->dev_flags & IDE_DFLAG_DMA_PIO_RETRY) &&
	    drive->retry_pio <= 3) {
		drive->dev_flags &= ~IDE_DFLAG_DMA_PIO_RETRY;
		ide_dma_on(drive);
	}

	if (!blk_end_request(rq, error, nr_bytes))
		ret = 0;

	if (ret == 0 && dequeue)
		drive->hwif->rq = NULL;

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
	struct request *rq = drive->hwif->rq;

	if (!nr_bytes) {
		if (blk_pc_request(rq))
			nr_bytes = rq->data_len;
		else
			nr_bytes = rq->hard_cur_sectors << 9;
	}

	return __ide_end_request(drive, rq, uptodate, nr_bytes, 1);
}
EXPORT_SYMBOL(ide_end_request);

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
	BUG_ON(!blk_rq_started(rq));

	return __ide_end_request(drive, rq, uptodate, nr_sectors << 9, 0);
}
EXPORT_SYMBOL_GPL(ide_end_dequeued_request);

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
	ide_hwif_t *hwif = drive->hwif;
	struct request *rq = hwif->rq;

	if (rq->cmd_type == REQ_TYPE_ATA_TASKFILE) {
		ide_task_t *task = (ide_task_t *)rq->special;

		if (task) {
			struct ide_taskfile *tf = &task->tf;

			tf->error = err;
			tf->status = stat;

			drive->hwif->tp_ops->tf_read(drive, task);

			if (task->tf_flags & IDE_TFLAG_DYN)
				kfree(task);
		}
	} else if (blk_pm_request(rq)) {
		struct request_pm_state *pm = rq->data;

		ide_complete_power_step(drive, rq);
		if (pm->pm_step == IDE_PM_COMPLETED)
			ide_complete_pm_request(drive, rq);
		return;
	}

	hwif->rq = NULL;

	rq->errors = err;

	if (unlikely(blk_end_request(rq, (rq->errors ? -EIO : 0),
				     blk_rq_bytes(rq))))
		BUG();
}
EXPORT_SYMBOL(ide_end_drive_cmd);

static void ide_kill_rq(ide_drive_t *drive, struct request *rq)
{
	if (rq->rq_disk) {
		struct ide_driver *drv;

		drv = *(struct ide_driver **)rq->rq_disk->private_data;
		drv->end_request(drive, 0, 0);
	} else
		ide_end_request(drive, 0, 0);
}

static ide_startstop_t ide_ata_error(ide_drive_t *drive, struct request *rq, u8 stat, u8 err)
{
	ide_hwif_t *hwif = drive->hwif;

	if ((stat & ATA_BUSY) ||
	    ((stat & ATA_DF) && (drive->dev_flags & IDE_DFLAG_NOWERR) == 0)) {
		/* other bits are useless when BUSY */
		rq->errors |= ERROR_RESET;
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
			rq->errors = ERROR_MAX;
		} else if (err & ATA_TRK0NF) {
			/* help it find track zero */
			rq->errors |= ERROR_RECAL;
		}
	}

	if ((stat & ATA_DRQ) && rq_data_dir(rq) == READ &&
	    (hwif->host_flags & IDE_HFLAG_ERROR_STOPS_FIFO) == 0) {
		int nsect = drive->mult_count ? drive->mult_count : 1;

		ide_pad_transfer(drive, READ, nsect * SECTOR_SIZE);
	}

	if (rq->errors >= ERROR_MAX || blk_noretry_request(rq)) {
		ide_kill_rq(drive, rq);
		return ide_stopped;
	}

	if (hwif->tp_ops->read_status(hwif) & (ATA_BUSY | ATA_DRQ))
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

	if ((stat & ATA_BUSY) ||
	    ((stat & ATA_DF) && (drive->dev_flags & IDE_DFLAG_NOWERR) == 0)) {
		/* other bits are useless when BUSY */
		rq->errors |= ERROR_RESET;
	} else {
		/* add decoding error stuff */
	}

	if (hwif->tp_ops->read_status(hwif) & (ATA_BUSY | ATA_DRQ))
		/* force an abort */
		hwif->tp_ops->exec_command(hwif, ATA_CMD_IDLEIMMEDIATE);

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

static ide_startstop_t
__ide_error(ide_drive_t *drive, struct request *rq, u8 stat, u8 err)
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
 
ide_startstop_t ide_error (ide_drive_t *drive, const char *msg, u8 stat)
{
	struct request *rq;
	u8 err;

	err = ide_dump_status(drive, msg, stat);

	rq = drive->hwif->rq;
	if (rq == NULL)
		return ide_stopped;

	/* retry only "normal" I/O: */
	if (!blk_fs_request(rq)) {
		rq->errors = 1;
		ide_end_drive_cmd(drive, stat, err);
		return ide_stopped;
	}

	return __ide_error(drive, rq, stat, err);
}
EXPORT_SYMBOL_GPL(ide_error);

static void ide_tf_set_specify_cmd(ide_drive_t *drive, struct ide_taskfile *tf)
{
	tf->nsect   = drive->sect;
	tf->lbal    = drive->sect;
	tf->lbam    = drive->cyl;
	tf->lbah    = drive->cyl >> 8;
	tf->device  = (drive->head - 1) | drive->select;
	tf->command = ATA_CMD_INIT_DEV_PARAMS;
}

static void ide_tf_set_restore_cmd(ide_drive_t *drive, struct ide_taskfile *tf)
{
	tf->nsect   = drive->sect;
	tf->command = ATA_CMD_RESTORE;
}

static void ide_tf_set_setmult_cmd(ide_drive_t *drive, struct ide_taskfile *tf)
{
	tf->nsect   = drive->mult_req;
	tf->command = ATA_CMD_SET_MULTI;
}

static ide_startstop_t ide_disk_special(ide_drive_t *drive)
{
	special_t *s = &drive->special;
	ide_task_t args;

	memset(&args, 0, sizeof(ide_task_t));
	args.data_phase = TASKFILE_NO_DATA;

	if (s->b.set_geometry) {
		s->b.set_geometry = 0;
		ide_tf_set_specify_cmd(drive, &args.tf);
	} else if (s->b.recalibrate) {
		s->b.recalibrate = 0;
		ide_tf_set_restore_cmd(drive, &args.tf);
	} else if (s->b.set_multmode) {
		s->b.set_multmode = 0;
		ide_tf_set_setmult_cmd(drive, &args.tf);
	} else if (s->all) {
		int special = s->all;
		s->all = 0;
		printk(KERN_ERR "%s: bad special flag: 0x%02x\n", drive->name, special);
		return ide_stopped;
	}

	args.tf_flags = IDE_TFLAG_TF | IDE_TFLAG_DEVICE |
			IDE_TFLAG_CUSTOM_HANDLER;

	do_rw_taskfile(drive, &args);

	return ide_started;
}

/**
 *	do_special		-	issue some special commands
 *	@drive: drive the command is for
 *
 *	do_special() is used to issue ATA_CMD_INIT_DEV_PARAMS,
 *	ATA_CMD_RESTORE and ATA_CMD_SET_MULTI commands to a drive.
 *
 *	It used to do much more, but has been scaled back.
 */

static ide_startstop_t do_special (ide_drive_t *drive)
{
	special_t *s = &drive->special;

#ifdef DEBUG
	printk("%s: do_special: 0x%02x\n", drive->name, s->all);
#endif
	if (drive->media == ide_disk)
		return ide_disk_special(drive);

	s->all = 0;
	drive->mult_req = 0;
	return ide_stopped;
}

void ide_map_sg(ide_drive_t *drive, struct request *rq)
{
	ide_hwif_t *hwif = drive->hwif;
	struct scatterlist *sg = hwif->sg_table;

	if (rq->cmd_type == REQ_TYPE_ATA_TASKFILE) {
		sg_init_one(sg, rq->buffer, rq->nr_sectors * SECTOR_SIZE);
		hwif->sg_nents = 1;
	} else if (!rq->bio) {
		sg_init_one(sg, rq->data, rq->data_len);
		hwif->sg_nents = 1;
	} else {
		hwif->sg_nents = blk_rq_map_sg(drive->queue, rq, sg);
	}
}

EXPORT_SYMBOL_GPL(ide_map_sg);

void ide_init_sg_cmd(ide_drive_t *drive, struct request *rq)
{
	ide_hwif_t *hwif = drive->hwif;

	hwif->nsect = hwif->nleft = rq->nr_sectors;
	hwif->cursg_ofs = 0;
	hwif->cursg = NULL;
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
	ide_hwif_t *hwif = drive->hwif;
	ide_task_t *task = rq->special;

	if (task) {
		hwif->data_phase = task->data_phase;

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

		return do_rw_taskfile(drive, task);
	}

 	/*
 	 * NULL is actually a valid way of waiting for
 	 * all current requests to be flushed from the queue.
 	 */
#ifdef DEBUG
 	printk("%s: DRIVE_CMD (null)\n", drive->name);
#endif
	ide_end_drive_cmd(drive, hwif->tp_ops->read_status(hwif),
			  ide_read_error(drive));

 	return ide_stopped;
}

int ide_devset_execute(ide_drive_t *drive, const struct ide_devset *setting,
		       int arg)
{
	struct request_queue *q = drive->queue;
	struct request *rq;
	int ret = 0;

	if (!(setting->flags & DS_SYNC))
		return setting->set(drive, arg);

	rq = blk_get_request(q, READ, __GFP_WAIT);
	rq->cmd_type = REQ_TYPE_SPECIAL;
	rq->cmd_len = 5;
	rq->cmd[0] = REQ_DEVSET_EXEC;
	*(int *)&rq->cmd[1] = arg;
	rq->special = setting->set;

	if (blk_execute_rq(q, NULL, rq, 0))
		ret = rq->errors;
	blk_put_request(rq);

	return ret;
}
EXPORT_SYMBOL_GPL(ide_devset_execute);

static ide_startstop_t ide_special_rq(ide_drive_t *drive, struct request *rq)
{
	u8 cmd = rq->cmd[0];

	if (cmd == REQ_PARK_HEADS || cmd == REQ_UNPARK_HEADS) {
		ide_task_t task;
		struct ide_taskfile *tf = &task.tf;

		memset(&task, 0, sizeof(task));
		if (cmd == REQ_PARK_HEADS) {
			drive->sleep = *(unsigned long *)rq->special;
			drive->dev_flags |= IDE_DFLAG_SLEEPING;
			tf->command = ATA_CMD_IDLEIMMEDIATE;
			tf->feature = 0x44;
			tf->lbal = 0x4c;
			tf->lbam = 0x4e;
			tf->lbah = 0x55;
			task.tf_flags |= IDE_TFLAG_CUSTOM_HANDLER;
		} else		/* cmd == REQ_UNPARK_HEADS */
			tf->command = ATA_CMD_CHK_POWER;

		task.tf_flags |= IDE_TFLAG_TF | IDE_TFLAG_DEVICE;
		task.rq = rq;
		drive->hwif->data_phase = task.data_phase = TASKFILE_NO_DATA;
		return do_rw_taskfile(drive, &task);
	}

	switch (cmd) {
	case REQ_DEVSET_EXEC:
	{
		int err, (*setfunc)(ide_drive_t *, int) = rq->special;

		err = setfunc(drive, *(int *)&rq->cmd[1]);
		if (err)
			rq->errors = err;
		else
			err = 1;
		ide_end_request(drive, err, 0);
		return ide_stopped;
	}
	case REQ_DRIVE_RESET:
		return ide_do_reset(drive);
	default:
		blk_dump_rq_flags(rq, "ide_special_rq - bad request");
		ide_end_request(drive, 0, 0);
		return ide_stopped;
	}
}

/**
 *	start_request	-	start of I/O and command issuing for IDE
 *
 *	start_request() initiates handling of a new I/O request. It
 *	accepts commands and I/O (read/write) requests.
 *
 *	FIXME: this function needs a rename
 */
 
static ide_startstop_t start_request (ide_drive_t *drive, struct request *rq)
{
	ide_startstop_t startstop;

	BUG_ON(!blk_rq_started(rq));

#ifdef DEBUG
	printk("%s: start_request: current=0x%08lx\n",
		drive->hwif->name, (unsigned long) rq);
#endif

	/* bail early if we've exceeded max_failures */
	if (drive->max_failures && (drive->failures > drive->max_failures)) {
		rq->cmd_flags |= REQ_FAILED;
		goto kill_rq;
	}

	if (blk_pm_request(rq))
		ide_check_pm_state(drive, rq);

	SELECT_DRIVE(drive);
	if (ide_wait_stat(&startstop, drive, drive->ready_stat,
			  ATA_BUSY | ATA_DRQ, WAIT_READY)) {
		printk(KERN_ERR "%s: drive not ready for command\n", drive->name);
		return startstop;
	}
	if (!drive->special.all) {
		struct ide_driver *drv;

		/*
		 * We reset the drive so we need to issue a SETFEATURES.
		 * Do it _after_ do_special() restored device parameters.
		 */
		if (drive->current_speed == 0xff)
			ide_config_drive_speed(drive, drive->desired_speed);

		if (rq->cmd_type == REQ_TYPE_ATA_TASKFILE)
			return execute_drive_cmd(drive, rq);
		else if (blk_pm_request(rq)) {
			struct request_pm_state *pm = rq->data;
#ifdef DEBUG_PM
			printk("%s: start_power_step(step: %d)\n",
				drive->name, pm->pm_step);
#endif
			startstop = ide_start_power_step(drive, rq);
			if (startstop == ide_stopped &&
			    pm->pm_step == IDE_PM_COMPLETED)
				ide_complete_pm_request(drive, rq);
			return startstop;
		} else if (!rq->rq_disk && blk_special_request(rq))
			/*
			 * TODO: Once all ULDs have been modified to
			 * check for specific op codes rather than
			 * blindly accepting any special request, the
			 * check for ->rq_disk above may be replaced
			 * by a more suitable mechanism or even
			 * dropped entirely.
			 */
			return ide_special_rq(drive, rq);

		drv = *(struct ide_driver **)rq->rq_disk->private_data;

		return drv->do_request(drive, rq, rq->sector);
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
 *	to the port by sleeping for timeout jiffies.
 */
 
void ide_stall_queue (ide_drive_t *drive, unsigned long timeout)
{
	if (timeout > WAIT_WORSTCASE)
		timeout = WAIT_WORSTCASE;
	drive->sleep = timeout + jiffies;
	drive->dev_flags |= IDE_DFLAG_SLEEPING;
}
EXPORT_SYMBOL(ide_stall_queue);

static inline int ide_lock_port(ide_hwif_t *hwif)
{
	if (hwif->busy)
		return 1;

	hwif->busy = 1;

	return 0;
}

static inline void ide_unlock_port(ide_hwif_t *hwif)
{
	hwif->busy = 0;
}

static inline int ide_lock_host(struct ide_host *host, ide_hwif_t *hwif)
{
	int rc = 0;

	if (host->host_flags & IDE_HFLAG_SERIALIZE) {
		rc = test_and_set_bit_lock(IDE_HOST_BUSY, &host->host_busy);
		if (rc == 0) {
			/* for atari only */
			ide_get_lock(ide_intr, hwif);
		}
	}
	return rc;
}

static inline void ide_unlock_host(struct ide_host *host)
{
	if (host->host_flags & IDE_HFLAG_SERIALIZE) {
		/* for atari only */
		ide_release_lock();
		clear_bit_unlock(IDE_HOST_BUSY, &host->host_busy);
	}
}

/*
 * Issue a new request to a device.
 */
void do_ide_request(struct request_queue *q)
{
	ide_drive_t	*drive = q->queuedata;
	ide_hwif_t	*hwif = drive->hwif;
	struct ide_host *host = hwif->host;
	struct request	*rq = NULL;
	ide_startstop_t	startstop;

	/*
	 * drive is doing pre-flush, ordered write, post-flush sequence. even
	 * though that is 3 requests, it must be seen as a single transaction.
	 * we must not preempt this drive until that is complete
	 */
	if (blk_queue_flushing(q))
		/*
		 * small race where queue could get replugged during
		 * the 3-request flush cycle, just yank the plug since
		 * we want it to finish asap
		 */
		blk_remove_plug(q);

	spin_unlock_irq(q->queue_lock);

	if (ide_lock_host(host, hwif))
		goto plug_device_2;

	spin_lock_irq(&hwif->lock);

	if (!ide_lock_port(hwif)) {
		ide_hwif_t *prev_port;
repeat:
		prev_port = hwif->host->cur_port;
		hwif->rq = NULL;

		if (drive->dev_flags & IDE_DFLAG_SLEEPING) {
			if (time_before(drive->sleep, jiffies)) {
				ide_unlock_port(hwif);
				goto plug_device;
			}
		}

		if ((hwif->host->host_flags & IDE_HFLAG_SERIALIZE) &&
		    hwif != prev_port) {
			/*
			 * set nIEN for previous port, drives in the
			 * quirk_list may not like intr setups/cleanups
			 */
			if (prev_port && prev_port->cur_dev->quirk_list == 0)
				prev_port->tp_ops->set_irq(prev_port, 0);

			hwif->host->cur_port = hwif;
		}
		hwif->cur_dev = drive;
		drive->dev_flags &= ~(IDE_DFLAG_SLEEPING | IDE_DFLAG_PARKED);

		spin_unlock_irq(&hwif->lock);
		spin_lock_irq(q->queue_lock);
		/*
		 * we know that the queue isn't empty, but this can happen
		 * if the q->prep_rq_fn() decides to kill a request
		 */
		rq = elv_next_request(drive->queue);
		spin_unlock_irq(q->queue_lock);
		spin_lock_irq(&hwif->lock);

		if (!rq) {
			ide_unlock_port(hwif);
			goto out;
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
		 */
		if ((drive->dev_flags & IDE_DFLAG_BLOCKED) &&
		    blk_pm_request(rq) == 0 &&
		    (rq->cmd_flags & REQ_PREEMPT) == 0) {
			/* there should be no pending command at this point */
			ide_unlock_port(hwif);
			goto plug_device;
		}

		hwif->rq = rq;

		spin_unlock_irq(&hwif->lock);
		startstop = start_request(drive, rq);
		spin_lock_irq(&hwif->lock);

		if (startstop == ide_stopped)
			goto repeat;
	} else
		goto plug_device;
out:
	spin_unlock_irq(&hwif->lock);
	if (rq == NULL)
		ide_unlock_host(host);
	spin_lock_irq(q->queue_lock);
	return;

plug_device:
	spin_unlock_irq(&hwif->lock);
	ide_unlock_host(host);
plug_device_2:
	spin_lock_irq(q->queue_lock);

	if (!elv_queue_empty(q))
		blk_plug_device(q);
}

/*
 * un-busy the port etc, and clear any pending DMA status. we want to
 * retry the current request in pio mode instead of risking tossing it
 * all away
 */
static ide_startstop_t ide_dma_timeout_retry(ide_drive_t *drive, int error)
{
	ide_hwif_t *hwif = drive->hwif;
	struct request *rq;
	ide_startstop_t ret = ide_stopped;

	/*
	 * end current dma transaction
	 */

	if (error < 0) {
		printk(KERN_WARNING "%s: DMA timeout error\n", drive->name);
		(void)hwif->dma_ops->dma_end(drive);
		ret = ide_error(drive, "dma timeout error",
				hwif->tp_ops->read_status(hwif));
	} else {
		printk(KERN_WARNING "%s: DMA timeout retry\n", drive->name);
		hwif->dma_ops->dma_timeout(drive);
	}

	/*
	 * disable dma for now, but remember that we did so because of
	 * a timeout -- we'll reenable after we finish this next request
	 * (or rather the first chunk of it) in pio.
	 */
	drive->dev_flags |= IDE_DFLAG_DMA_PIO_RETRY;
	drive->retry_pio++;
	ide_dma_off_quietly(drive);

	/*
	 * un-busy drive etc and make sure request is sane
	 */

	rq = hwif->rq;
	if (!rq)
		goto out;

	hwif->rq = NULL;

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

static void ide_plug_device(ide_drive_t *drive)
{
	struct request_queue *q = drive->queue;
	unsigned long flags;

	spin_lock_irqsave(q->queue_lock, flags);
	if (!elv_queue_empty(q))
		blk_plug_device(q);
	spin_unlock_irqrestore(q->queue_lock, flags);
}

/**
 *	ide_timer_expiry	-	handle lack of an IDE interrupt
 *	@data: timer callback magic (hwif)
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
	ide_hwif_t	*hwif = (ide_hwif_t *)data;
	ide_drive_t	*uninitialized_var(drive);
	ide_handler_t	*handler;
	unsigned long	flags;
	int		wait = -1;
	int		plug_device = 0;

	spin_lock_irqsave(&hwif->lock, flags);

	handler = hwif->handler;

	if (handler == NULL || hwif->req_gen != hwif->req_gen_timer) {
		/*
		 * Either a marginal timeout occurred
		 * (got the interrupt just as timer expired),
		 * or we were "sleeping" to give other devices a chance.
		 * Either way, we don't really want to complain about anything.
		 */
	} else {
		ide_expiry_t *expiry = hwif->expiry;
		ide_startstop_t startstop = ide_stopped;

		drive = hwif->cur_dev;

		if (expiry) {
			wait = expiry(drive);
			if (wait > 0) { /* continue */
				/* reset timer */
				hwif->timer.expires = jiffies + wait;
				hwif->req_gen_timer = hwif->req_gen;
				add_timer(&hwif->timer);
				spin_unlock_irqrestore(&hwif->lock, flags);
				return;
			}
		}
		hwif->handler = NULL;
		/*
		 * We need to simulate a real interrupt when invoking
		 * the handler() function, which means we need to
		 * globally mask the specific IRQ:
		 */
		spin_unlock(&hwif->lock);
		/* disable_irq_nosync ?? */
		disable_irq(hwif->irq);
		/* local CPU only, as if we were handling an interrupt */
		local_irq_disable();
		if (hwif->polling) {
			startstop = handler(drive);
		} else if (drive_is_ready(drive)) {
			if (drive->waiting_for_dma)
				hwif->dma_ops->dma_lost_irq(drive);
			(void)ide_ack_intr(hwif);
			printk(KERN_WARNING "%s: lost interrupt\n",
				drive->name);
			startstop = handler(drive);
		} else {
			if (drive->waiting_for_dma)
				startstop = ide_dma_timeout_retry(drive, wait);
			else
				startstop = ide_error(drive, "irq timeout",
					hwif->tp_ops->read_status(hwif));
		}
		spin_lock_irq(&hwif->lock);
		enable_irq(hwif->irq);
		if (startstop == ide_stopped) {
			ide_unlock_port(hwif);
			plug_device = 1;
		}
	}
	spin_unlock_irqrestore(&hwif->lock, flags);

	if (plug_device) {
		ide_unlock_host(hwif->host);
		ide_plug_device(drive);
	}
}

/**
 *	unexpected_intr		-	handle an unexpected IDE interrupt
 *	@irq: interrupt line
 *	@hwif: port being processed
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
 */

static void unexpected_intr(int irq, ide_hwif_t *hwif)
{
	u8 stat = hwif->tp_ops->read_status(hwif);

	if (!OK_STAT(stat, ATA_DRDY, BAD_STAT)) {
		/* Try to not flood the console with msgs */
		static unsigned long last_msgtime, count;
		++count;

		if (time_after(jiffies, last_msgtime + HZ)) {
			last_msgtime = jiffies;
			printk(KERN_ERR "%s: unexpected interrupt, "
				"status=0x%02x, count=%ld\n",
				hwif->name, stat, count);
		}
	}
}

/**
 *	ide_intr	-	default IDE interrupt handler
 *	@irq: interrupt number
 *	@dev_id: hwif
 *	@regs: unused weirdness from the kernel irq layer
 *
 *	This is the default IRQ handler for the IDE layer. You should
 *	not need to override it. If you do be aware it is subtle in
 *	places
 *
 *	hwif is the interface in the group currently performing
 *	a command. hwif->cur_dev is the drive and hwif->handler is
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
 *	on the port and the process begins again.
 */

irqreturn_t ide_intr (int irq, void *dev_id)
{
	ide_hwif_t *hwif = (ide_hwif_t *)dev_id;
	ide_drive_t *uninitialized_var(drive);
	ide_handler_t *handler;
	unsigned long flags;
	ide_startstop_t startstop;
	irqreturn_t irq_ret = IRQ_NONE;
	int plug_device = 0;

	if (hwif->host->host_flags & IDE_HFLAG_SERIALIZE) {
		if (hwif != hwif->host->cur_port)
			goto out_early;
	}

	spin_lock_irqsave(&hwif->lock, flags);

	if (!ide_ack_intr(hwif))
		goto out;

	handler = hwif->handler;

	if (handler == NULL || hwif->polling) {
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
		if (hwif->chipset != ide_pci)
#endif	/* CONFIG_BLK_DEV_IDEPCI */
		{
			/*
			 * Probably not a shared PCI interrupt,
			 * so we can safely try to do something about it:
			 */
			unexpected_intr(irq, hwif);
#ifdef CONFIG_BLK_DEV_IDEPCI
		} else {
			/*
			 * Whack the status register, just in case
			 * we have a leftover pending IRQ.
			 */
			(void)hwif->tp_ops->read_status(hwif);
#endif /* CONFIG_BLK_DEV_IDEPCI */
		}
		goto out;
	}

	drive = hwif->cur_dev;

	if (!drive_is_ready(drive))
		/*
		 * This happens regularly when we share a PCI IRQ with
		 * another device.  Unfortunately, it can also happen
		 * with some buggy drives that trigger the IRQ before
		 * their status register is up to date.  Hopefully we have
		 * enough advance overhead that the latter isn't a problem.
		 */
		goto out;

	hwif->handler = NULL;
	hwif->req_gen++;
	del_timer(&hwif->timer);
	spin_unlock(&hwif->lock);

	if (hwif->port_ops && hwif->port_ops->clear_irq)
		hwif->port_ops->clear_irq(drive);

	if (drive->dev_flags & IDE_DFLAG_UNMASK)
		local_irq_enable_in_hardirq();

	/* service this interrupt, may set handler for next interrupt */
	startstop = handler(drive);

	spin_lock_irq(&hwif->lock);
	/*
	 * Note that handler() may have set things up for another
	 * interrupt to occur soon, but it cannot happen until
	 * we exit from this routine, because it will be the
	 * same irq as is currently being serviced here, and Linux
	 * won't allow another of the same (on any CPU) until we return.
	 */
	if (startstop == ide_stopped) {
		BUG_ON(hwif->handler);
		ide_unlock_port(hwif);
		plug_device = 1;
	}
	irq_ret = IRQ_HANDLED;
out:
	spin_unlock_irqrestore(&hwif->lock, flags);
out_early:
	if (plug_device) {
		ide_unlock_host(hwif->host);
		ide_plug_device(drive);
	}

	return irq_ret;
}
EXPORT_SYMBOL_GPL(ide_intr);

/**
 *	ide_do_drive_cmd	-	issue IDE special command
 *	@drive: device to issue command
 *	@rq: request to issue
 *
 *	This function issues a special IDE device request
 *	onto the request queue.
 *
 *	the rq is queued at the head of the request queue, displacing
 *	the currently-being-processed request and this function
 *	returns immediately without waiting for the new rq to be
 *	completed.  This is VERY DANGEROUS, and is intended for
 *	careful use by the ATAPI tape/cdrom driver code.
 */

void ide_do_drive_cmd(ide_drive_t *drive, struct request *rq)
{
	struct request_queue *q = drive->queue;
	unsigned long flags;

	drive->hwif->rq = NULL;

	spin_lock_irqsave(q->queue_lock, flags);
	__elv_add_request(q, rq, ELEVATOR_INSERT_FRONT, 0);
	spin_unlock_irqrestore(q->queue_lock, flags);
}
EXPORT_SYMBOL(ide_do_drive_cmd);

void ide_pktcmd_tf_load(ide_drive_t *drive, u32 tf_flags, u16 bcount, u8 dma)
{
	ide_hwif_t *hwif = drive->hwif;
	ide_task_t task;

	memset(&task, 0, sizeof(task));
	task.tf_flags = IDE_TFLAG_OUT_LBAH | IDE_TFLAG_OUT_LBAM |
			IDE_TFLAG_OUT_FEATURE | tf_flags;
	task.tf.feature = dma;		/* Use PIO/DMA */
	task.tf.lbam    = bcount & 0xff;
	task.tf.lbah    = (bcount >> 8) & 0xff;

	ide_tf_dump(drive->name, &task.tf);
	hwif->tp_ops->set_irq(hwif, 1);
	SELECT_MASK(drive, 0);
	hwif->tp_ops->tf_load(drive, &task);
}

EXPORT_SYMBOL_GPL(ide_pktcmd_tf_load);

void ide_pad_transfer(ide_drive_t *drive, int write, int len)
{
	ide_hwif_t *hwif = drive->hwif;
	u8 buf[4] = { 0 };

	while (len > 0) {
		if (write)
			hwif->tp_ops->output_data(drive, NULL, buf, min(4, len));
		else
			hwif->tp_ops->input_data(drive, NULL, buf, min(4, len));
		len -= 4;
	}
}
EXPORT_SYMBOL_GPL(ide_pad_transfer);
