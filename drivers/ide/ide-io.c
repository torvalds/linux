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
#include <linux/bitops.h>

#include <asm/byteorder.h>
#include <asm/irq.h>
#include <linux/uaccess.h>
#include <asm/io.h>

int ide_end_rq(ide_drive_t *drive, struct request *rq, blk_status_t error,
	       unsigned int nr_bytes)
{
	/*
	 * decide whether to reenable DMA -- 3 is a random magic for now,
	 * if we DMA timeout more than 3 times, just stay in PIO
	 */
	if ((drive->dev_flags & IDE_DFLAG_DMA_PIO_RETRY) &&
	    drive->retry_pio <= 3) {
		drive->dev_flags &= ~IDE_DFLAG_DMA_PIO_RETRY;
		ide_dma_on(drive);
	}

	if (!blk_update_request(rq, error, nr_bytes)) {
		if (rq == drive->sense_rq) {
			drive->sense_rq = NULL;
			drive->sense_rq_active = false;
		}

		__blk_mq_end_request(rq, error);
		return 0;
	}

	return 1;
}
EXPORT_SYMBOL_GPL(ide_end_rq);

void ide_complete_cmd(ide_drive_t *drive, struct ide_cmd *cmd, u8 stat, u8 err)
{
	const struct ide_tp_ops *tp_ops = drive->hwif->tp_ops;
	struct ide_taskfile *tf = &cmd->tf;
	struct request *rq = cmd->rq;
	u8 tf_cmd = tf->command;

	tf->error = err;
	tf->status = stat;

	if (cmd->ftf_flags & IDE_FTFLAG_IN_DATA) {
		u8 data[2];

		tp_ops->input_data(drive, cmd, data, 2);

		cmd->tf.data  = data[0];
		cmd->hob.data = data[1];
	}

	ide_tf_readback(drive, cmd);

	if ((cmd->tf_flags & IDE_TFLAG_CUSTOM_HANDLER) &&
	    tf_cmd == ATA_CMD_IDLEIMMEDIATE) {
		if (tf->lbal != 0xc4) {
			printk(KERN_ERR "%s: head unload failed!\n",
			       drive->name);
			ide_tf_dump(drive->name, cmd);
		} else
			drive->dev_flags |= IDE_DFLAG_PARKED;
	}

	if (rq && ata_taskfile_request(rq)) {
		struct ide_cmd *orig_cmd = ide_req(rq)->special;

		if (cmd->tf_flags & IDE_TFLAG_DYN)
			kfree(orig_cmd);
		else if (cmd != orig_cmd)
			memcpy(orig_cmd, cmd, sizeof(*cmd));
	}
}

int ide_complete_rq(ide_drive_t *drive, blk_status_t error, unsigned int nr_bytes)
{
	ide_hwif_t *hwif = drive->hwif;
	struct request *rq = hwif->rq;
	int rc;

	/*
	 * if failfast is set on a request, override number of sectors
	 * and complete the whole request right now
	 */
	if (blk_noretry_request(rq) && error)
		nr_bytes = blk_rq_sectors(rq) << 9;

	rc = ide_end_rq(drive, rq, error, nr_bytes);
	if (rc == 0)
		hwif->rq = NULL;

	return rc;
}
EXPORT_SYMBOL(ide_complete_rq);

void ide_kill_rq(ide_drive_t *drive, struct request *rq)
{
	u8 drv_req = ata_misc_request(rq) && rq->rq_disk;
	u8 media = drive->media;

	drive->failed_pc = NULL;

	if ((media == ide_floppy || media == ide_tape) && drv_req) {
		scsi_req(rq)->result = 0;
	} else {
		if (media == ide_tape)
			scsi_req(rq)->result = IDE_DRV_ERROR_GENERAL;
		else if (blk_rq_is_passthrough(rq) && scsi_req(rq)->result == 0)
			scsi_req(rq)->result = -EIO;
	}

	ide_complete_rq(drive, BLK_STS_IOERR, blk_rq_bytes(rq));
}

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

/**
 *	do_special		-	issue some special commands
 *	@drive: drive the command is for
 *
 *	do_special() is used to issue ATA_CMD_INIT_DEV_PARAMS,
 *	ATA_CMD_RESTORE and ATA_CMD_SET_MULTI commands to a drive.
 */

static ide_startstop_t do_special(ide_drive_t *drive)
{
	struct ide_cmd cmd;

#ifdef DEBUG
	printk(KERN_DEBUG "%s: %s: 0x%02x\n", drive->name, __func__,
		drive->special_flags);
#endif
	if (drive->media != ide_disk) {
		drive->special_flags = 0;
		drive->mult_req = 0;
		return ide_stopped;
	}

	memset(&cmd, 0, sizeof(cmd));
	cmd.protocol = ATA_PROT_NODATA;

	if (drive->special_flags & IDE_SFLAG_SET_GEOMETRY) {
		drive->special_flags &= ~IDE_SFLAG_SET_GEOMETRY;
		ide_tf_set_specify_cmd(drive, &cmd.tf);
	} else if (drive->special_flags & IDE_SFLAG_RECALIBRATE) {
		drive->special_flags &= ~IDE_SFLAG_RECALIBRATE;
		ide_tf_set_restore_cmd(drive, &cmd.tf);
	} else if (drive->special_flags & IDE_SFLAG_SET_MULTMODE) {
		drive->special_flags &= ~IDE_SFLAG_SET_MULTMODE;
		ide_tf_set_setmult_cmd(drive, &cmd.tf);
	} else
		BUG();

	cmd.valid.out.tf = IDE_VALID_OUT_TF | IDE_VALID_DEVICE;
	cmd.valid.in.tf  = IDE_VALID_IN_TF  | IDE_VALID_DEVICE;
	cmd.tf_flags = IDE_TFLAG_CUSTOM_HANDLER;

	do_rw_taskfile(drive, &cmd);

	return ide_started;
}

void ide_map_sg(ide_drive_t *drive, struct ide_cmd *cmd)
{
	ide_hwif_t *hwif = drive->hwif;
	struct scatterlist *sg = hwif->sg_table, *last_sg = NULL;
	struct request *rq = cmd->rq;

	cmd->sg_nents = __blk_rq_map_sg(drive->queue, rq, sg, &last_sg);
	if (blk_rq_bytes(rq) && (blk_rq_bytes(rq) & rq->q->dma_pad_mask))
		last_sg->length +=
			(rq->q->dma_pad_mask & ~blk_rq_bytes(rq)) + 1;
}
EXPORT_SYMBOL_GPL(ide_map_sg);

void ide_init_sg_cmd(struct ide_cmd *cmd, unsigned int nr_bytes)
{
	cmd->nbytes = cmd->nleft = nr_bytes;
	cmd->cursg_ofs = 0;
	cmd->cursg = NULL;
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
	struct ide_cmd *cmd = ide_req(rq)->special;

	if (cmd) {
		if (cmd->protocol == ATA_PROT_PIO) {
			ide_init_sg_cmd(cmd, blk_rq_sectors(rq) << 9);
			ide_map_sg(drive, cmd);
		}

		return do_rw_taskfile(drive, cmd);
	}

 	/*
 	 * NULL is actually a valid way of waiting for
 	 * all current requests to be flushed from the queue.
 	 */
#ifdef DEBUG
 	printk("%s: DRIVE_CMD (null)\n", drive->name);
#endif
	scsi_req(rq)->result = 0;
	ide_complete_rq(drive, BLK_STS_OK, blk_rq_bytes(rq));

 	return ide_stopped;
}

static ide_startstop_t ide_special_rq(ide_drive_t *drive, struct request *rq)
{
	u8 cmd = scsi_req(rq)->cmd[0];

	switch (cmd) {
	case REQ_PARK_HEADS:
	case REQ_UNPARK_HEADS:
		return ide_do_park_unpark(drive, rq);
	case REQ_DEVSET_EXEC:
		return ide_do_devset(drive, rq);
	case REQ_DRIVE_RESET:
		return ide_do_reset(drive);
	default:
		BUG();
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

#ifdef DEBUG
	printk("%s: start_request: current=0x%08lx\n",
		drive->hwif->name, (unsigned long) rq);
#endif

	/* bail early if we've exceeded max_failures */
	if (drive->max_failures && (drive->failures > drive->max_failures)) {
		rq->rq_flags |= RQF_FAILED;
		goto kill_rq;
	}

	if (drive->prep_rq && !drive->prep_rq(drive, rq))
		return ide_stopped;

	if (ata_pm_request(rq))
		ide_check_pm_state(drive, rq);

	drive->hwif->tp_ops->dev_select(drive);
	if (ide_wait_stat(&startstop, drive, drive->ready_stat,
			  ATA_BUSY | ATA_DRQ, WAIT_READY)) {
		printk(KERN_ERR "%s: drive not ready for command\n", drive->name);
		return startstop;
	}

	if (drive->special_flags == 0) {
		struct ide_driver *drv;

		/*
		 * We reset the drive so we need to issue a SETFEATURES.
		 * Do it _after_ do_special() restored device parameters.
		 */
		if (drive->current_speed == 0xff)
			ide_config_drive_speed(drive, drive->desired_speed);

		if (ata_taskfile_request(rq))
			return execute_drive_cmd(drive, rq);
		else if (ata_pm_request(rq)) {
			struct ide_pm_state *pm = ide_req(rq)->special;
#ifdef DEBUG_PM
			printk("%s: start_power_step(step: %d)\n",
				drive->name, pm->pm_step);
#endif
			startstop = ide_start_power_step(drive, rq);
			if (startstop == ide_stopped &&
			    pm->pm_step == IDE_PM_COMPLETED)
				ide_complete_pm_rq(drive, rq);
			return startstop;
		} else if (!rq->rq_disk && ata_misc_request(rq))
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

		return drv->do_request(drive, rq, blk_rq_pos(rq));
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
			if (host->get_lock)
				host->get_lock(ide_intr, hwif);
		}
	}
	return rc;
}

static inline void ide_unlock_host(struct ide_host *host)
{
	if (host->host_flags & IDE_HFLAG_SERIALIZE) {
		if (host->release_lock)
			host->release_lock();
		clear_bit_unlock(IDE_HOST_BUSY, &host->host_busy);
	}
}

void ide_requeue_and_plug(ide_drive_t *drive, struct request *rq)
{
	struct request_queue *q = drive->queue;

	/* Use 3ms as that was the old plug delay */
	if (rq) {
		blk_mq_requeue_request(rq, false);
		blk_mq_delay_kick_requeue_list(q, 3);
	} else
		blk_mq_delay_run_hw_queue(q->queue_hw_ctx[0], 3);
}

blk_status_t ide_issue_rq(ide_drive_t *drive, struct request *rq,
			  bool local_requeue)
{
	ide_hwif_t *hwif = drive->hwif;
	struct ide_host *host = hwif->host;
	ide_startstop_t	startstop;

	if (!blk_rq_is_passthrough(rq) && !(rq->rq_flags & RQF_DONTPREP)) {
		rq->rq_flags |= RQF_DONTPREP;
		ide_req(rq)->special = NULL;
	}

	/* HLD do_request() callback might sleep, make sure it's okay */
	might_sleep();

	if (ide_lock_host(host, hwif))
		return BLK_STS_DEV_RESOURCE;

	spin_lock_irq(&hwif->lock);

	if (!ide_lock_port(hwif)) {
		ide_hwif_t *prev_port;

		WARN_ON_ONCE(hwif->rq);
repeat:
		prev_port = hwif->host->cur_port;
		if (drive->dev_flags & IDE_DFLAG_SLEEPING &&
		    time_after(drive->sleep, jiffies)) {
			ide_unlock_port(hwif);
			goto plug_device;
		}

		if ((hwif->host->host_flags & IDE_HFLAG_SERIALIZE) &&
		    hwif != prev_port) {
			ide_drive_t *cur_dev =
				prev_port ? prev_port->cur_dev : NULL;

			/*
			 * set nIEN for previous port, drives in the
			 * quirk list may not like intr setups/cleanups
			 */
			if (cur_dev &&
			    (cur_dev->dev_flags & IDE_DFLAG_NIEN_QUIRK) == 0)
				prev_port->tp_ops->write_devctl(prev_port,
								ATA_NIEN |
								ATA_DEVCTL_OBS);

			hwif->host->cur_port = hwif;
		}
		hwif->cur_dev = drive;
		drive->dev_flags &= ~(IDE_DFLAG_SLEEPING | IDE_DFLAG_PARKED);

		/*
		 * Sanity: don't accept a request that isn't a PM request
		 * if we are currently power managed. This is very important as
		 * blk_stop_queue() doesn't prevent the blk_fetch_request()
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
		    ata_pm_request(rq) == 0 &&
		    (rq->rq_flags & RQF_PREEMPT) == 0) {
			/* there should be no pending command at this point */
			ide_unlock_port(hwif);
			goto plug_device;
		}

		scsi_req(rq)->resid_len = blk_rq_bytes(rq);
		hwif->rq = rq;

		spin_unlock_irq(&hwif->lock);
		startstop = start_request(drive, rq);
		spin_lock_irq(&hwif->lock);

		if (startstop == ide_stopped) {
			rq = hwif->rq;
			hwif->rq = NULL;
			if (rq)
				goto repeat;
			ide_unlock_port(hwif);
			goto out;
		}
	} else {
plug_device:
		if (local_requeue)
			list_add(&rq->queuelist, &drive->rq_list);
		spin_unlock_irq(&hwif->lock);
		ide_unlock_host(host);
		if (!local_requeue)
			ide_requeue_and_plug(drive, rq);
		return BLK_STS_OK;
	}

out:
	spin_unlock_irq(&hwif->lock);
	if (rq == NULL)
		ide_unlock_host(host);
	return BLK_STS_OK;
}

/*
 * Issue a new request to a device.
 */
blk_status_t ide_queue_rq(struct blk_mq_hw_ctx *hctx,
			  const struct blk_mq_queue_data *bd)
{
	ide_drive_t *drive = hctx->queue->queuedata;
	ide_hwif_t *hwif = drive->hwif;

	spin_lock_irq(&hwif->lock);
	if (drive->sense_rq_active) {
		spin_unlock_irq(&hwif->lock);
		return BLK_STS_DEV_RESOURCE;
	}
	spin_unlock_irq(&hwif->lock);

	blk_mq_start_request(bd->rq);
	return ide_issue_rq(drive, bd->rq, false);
}

static int drive_is_ready(ide_drive_t *drive)
{
	ide_hwif_t *hwif = drive->hwif;
	u8 stat = 0;

	if (drive->waiting_for_dma)
		return hwif->dma_ops->dma_test_irq(drive);

	if (hwif->io_ports.ctl_addr &&
	    (hwif->host_flags & IDE_HFLAG_BROKEN_ALTSTATUS) == 0)
		stat = hwif->tp_ops->read_altstatus(hwif);
	else
		/* Note: this may clear a pending IRQ!! */
		stat = hwif->tp_ops->read_status(hwif);

	if (stat & ATA_BUSY)
		/* drive busy: definitely not interrupting */
		return 0;

	/* drive ready: *might* be interrupting */
	return 1;
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
 
void ide_timer_expiry (struct timer_list *t)
{
	ide_hwif_t	*hwif = from_timer(hwif, t, timer);
	ide_drive_t	*uninitialized_var(drive);
	ide_handler_t	*handler;
	unsigned long	flags;
	int		wait = -1;
	int		plug_device = 0;
	struct request	*uninitialized_var(rq_in_flight);

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
		hwif->expiry = NULL;
		/*
		 * We need to simulate a real interrupt when invoking
		 * the handler() function, which means we need to
		 * globally mask the specific IRQ:
		 */
		spin_unlock(&hwif->lock);
		/* disable_irq_nosync ?? */
		disable_irq(hwif->irq);

		if (hwif->polling) {
			startstop = handler(drive);
		} else if (drive_is_ready(drive)) {
			if (drive->waiting_for_dma)
				hwif->dma_ops->dma_lost_irq(drive);
			if (hwif->port_ops && hwif->port_ops->clear_irq)
				hwif->port_ops->clear_irq(drive);

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
		/* Disable interrupts again, `handler' might have enabled it */
		spin_lock_irq(&hwif->lock);
		enable_irq(hwif->irq);
		if (startstop == ide_stopped && hwif->polling == 0) {
			rq_in_flight = hwif->rq;
			hwif->rq = NULL;
			ide_unlock_port(hwif);
			plug_device = 1;
		}
	}
	spin_unlock_irqrestore(&hwif->lock, flags);

	if (plug_device) {
		ide_unlock_host(hwif->host);
		ide_requeue_and_plug(drive, rq_in_flight);
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
	struct ide_host *host = hwif->host;
	ide_drive_t *uninitialized_var(drive);
	ide_handler_t *handler;
	unsigned long flags;
	ide_startstop_t startstop;
	irqreturn_t irq_ret = IRQ_NONE;
	int plug_device = 0;
	struct request *uninitialized_var(rq_in_flight);

	if (host->host_flags & IDE_HFLAG_SERIALIZE) {
		if (hwif != host->cur_port)
			goto out_early;
	}

	spin_lock_irqsave(&hwif->lock, flags);

	if (hwif->port_ops && hwif->port_ops->test_irq &&
	    hwif->port_ops->test_irq(hwif) == 0)
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
		 */
		if ((host->irq_flags & IRQF_SHARED) == 0) {
			/*
			 * Probably not a shared PCI interrupt,
			 * so we can safely try to do something about it:
			 */
			unexpected_intr(irq, hwif);
		} else {
			/*
			 * Whack the status register, just in case
			 * we have a leftover pending IRQ.
			 */
			(void)hwif->tp_ops->read_status(hwif);
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
	hwif->expiry = NULL;
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
	if (startstop == ide_stopped && hwif->polling == 0) {
		BUG_ON(hwif->handler);
		rq_in_flight = hwif->rq;
		hwif->rq = NULL;
		ide_unlock_port(hwif);
		plug_device = 1;
	}
	irq_ret = IRQ_HANDLED;
out:
	spin_unlock_irqrestore(&hwif->lock, flags);
out_early:
	if (plug_device) {
		ide_unlock_host(hwif->host);
		ide_requeue_and_plug(drive, rq_in_flight);
	}

	return irq_ret;
}
EXPORT_SYMBOL_GPL(ide_intr);

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

void ide_insert_request_head(ide_drive_t *drive, struct request *rq)
{
	drive->sense_rq_active = true;
	list_add_tail(&rq->queuelist, &drive->rq_list);
	kblockd_schedule_work(&drive->rq_work);
}
EXPORT_SYMBOL_GPL(ide_insert_request_head);
