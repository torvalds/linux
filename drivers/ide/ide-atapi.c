/*
 * ATAPI support.
 */

#include <linux/kernel.h>
#include <linux/cdrom.h>
#include <linux/delay.h>
#include <linux/ide.h>
#include <linux/scatterlist.h>

#include <scsi/scsi.h>

#ifdef DEBUG
#define debug_log(fmt, args...) \
	printk(KERN_INFO "ide: " fmt, ## args)
#else
#define debug_log(fmt, args...) do {} while (0)
#endif

#define ATAPI_MIN_CDB_BYTES	12

static inline int dev_is_idecd(ide_drive_t *drive)
{
	return drive->media == ide_cdrom || drive->media == ide_optical;
}

/*
 * Check whether we can support a device,
 * based on the ATAPI IDENTIFY command results.
 */
int ide_check_atapi_device(ide_drive_t *drive, const char *s)
{
	u16 *id = drive->id;
	u8 gcw[2], protocol, device_type, removable, drq_type, packet_size;

	*((u16 *)&gcw) = id[ATA_ID_CONFIG];

	protocol    = (gcw[1] & 0xC0) >> 6;
	device_type =  gcw[1] & 0x1F;
	removable   = (gcw[0] & 0x80) >> 7;
	drq_type    = (gcw[0] & 0x60) >> 5;
	packet_size =  gcw[0] & 0x03;

#ifdef CONFIG_PPC
	/* kludge for Apple PowerBook internal zip */
	if (drive->media == ide_floppy && device_type == 5 &&
	    !strstr((char *)&id[ATA_ID_PROD], "CD-ROM") &&
	    strstr((char *)&id[ATA_ID_PROD], "ZIP"))
		device_type = 0;
#endif

	if (protocol != 2)
		printk(KERN_ERR "%s: %s: protocol (0x%02x) is not ATAPI\n",
			s, drive->name, protocol);
	else if ((drive->media == ide_floppy && device_type != 0) ||
		 (drive->media == ide_tape && device_type != 1))
		printk(KERN_ERR "%s: %s: invalid device type (0x%02x)\n",
			s, drive->name, device_type);
	else if (removable == 0)
		printk(KERN_ERR "%s: %s: the removable flag is not set\n",
			s, drive->name);
	else if (drive->media == ide_floppy && drq_type == 3)
		printk(KERN_ERR "%s: %s: sorry, DRQ type (0x%02x) not "
			"supported\n", s, drive->name, drq_type);
	else if (packet_size != 0)
		printk(KERN_ERR "%s: %s: packet size (0x%02x) is not 12 "
			"bytes\n", s, drive->name, packet_size);
	else
		return 1;
	return 0;
}
EXPORT_SYMBOL_GPL(ide_check_atapi_device);

void ide_init_pc(struct ide_atapi_pc *pc)
{
	memset(pc, 0, sizeof(*pc));
	pc->buf = pc->pc_buf;
	pc->buf_size = IDE_PC_BUFFER_SIZE;
}
EXPORT_SYMBOL_GPL(ide_init_pc);

/*
 * Add a special packet command request to the tail of the request queue,
 * and wait for it to be serviced.
 */
int ide_queue_pc_tail(ide_drive_t *drive, struct gendisk *disk,
		      struct ide_atapi_pc *pc)
{
	struct request *rq;
	int error;

	rq = blk_get_request(drive->queue, READ, __GFP_WAIT);
	rq->cmd_type = REQ_TYPE_SPECIAL;
	rq->special = (char *)pc;

	if (pc->req_xfer) {
		error = blk_rq_map_kern(drive->queue, rq, pc->buf, pc->req_xfer,
					GFP_NOIO);
		if (error)
			goto put_req;
	}

	memcpy(rq->cmd, pc->c, 12);
	if (drive->media == ide_tape)
		rq->cmd[13] = REQ_IDETAPE_PC1;
	error = blk_execute_rq(drive->queue, disk, rq, 0);
put_req:
	blk_put_request(rq);
	return error;
}
EXPORT_SYMBOL_GPL(ide_queue_pc_tail);

int ide_do_test_unit_ready(ide_drive_t *drive, struct gendisk *disk)
{
	struct ide_atapi_pc pc;

	ide_init_pc(&pc);
	pc.c[0] = TEST_UNIT_READY;

	return ide_queue_pc_tail(drive, disk, &pc);
}
EXPORT_SYMBOL_GPL(ide_do_test_unit_ready);

int ide_do_start_stop(ide_drive_t *drive, struct gendisk *disk, int start)
{
	struct ide_atapi_pc pc;

	ide_init_pc(&pc);
	pc.c[0] = START_STOP;
	pc.c[4] = start;

	if (drive->media == ide_tape)
		pc.flags |= PC_FLAG_WAIT_FOR_DSC;

	return ide_queue_pc_tail(drive, disk, &pc);
}
EXPORT_SYMBOL_GPL(ide_do_start_stop);

int ide_set_media_lock(ide_drive_t *drive, struct gendisk *disk, int on)
{
	struct ide_atapi_pc pc;

	if ((drive->dev_flags & IDE_DFLAG_DOORLOCKING) == 0)
		return 0;

	ide_init_pc(&pc);
	pc.c[0] = ALLOW_MEDIUM_REMOVAL;
	pc.c[4] = on;

	return ide_queue_pc_tail(drive, disk, &pc);
}
EXPORT_SYMBOL_GPL(ide_set_media_lock);

void ide_create_request_sense_cmd(ide_drive_t *drive, struct ide_atapi_pc *pc)
{
	ide_init_pc(pc);
	pc->c[0] = REQUEST_SENSE;
	if (drive->media == ide_floppy) {
		pc->c[4] = 255;
		pc->req_xfer = 18;
	} else {
		pc->c[4] = 20;
		pc->req_xfer = 20;
	}
}
EXPORT_SYMBOL_GPL(ide_create_request_sense_cmd);

void ide_prep_sense(ide_drive_t *drive, struct request *rq)
{
	struct request_sense *sense = &drive->sense_data;
	struct request *sense_rq = &drive->sense_rq;
	unsigned int cmd_len, sense_len;
	int err;

	debug_log("%s: enter\n", __func__);

	switch (drive->media) {
	case ide_floppy:
		cmd_len = 255;
		sense_len = 18;
		break;
	case ide_tape:
		cmd_len = 20;
		sense_len = 20;
		break;
	default:
		cmd_len = 18;
		sense_len = 18;
	}

	BUG_ON(sense_len > sizeof(*sense));

	if (blk_sense_request(rq) || drive->sense_rq_armed)
		return;

	memset(sense, 0, sizeof(*sense));

	blk_rq_init(rq->q, sense_rq);

	err = blk_rq_map_kern(drive->queue, sense_rq, sense, sense_len,
			      GFP_NOIO);
	if (unlikely(err)) {
		if (printk_ratelimit())
			printk(KERN_WARNING "%s: failed to map sense buffer\n",
			       drive->name);
		return;
	}

	sense_rq->rq_disk = rq->rq_disk;
	sense_rq->cmd[0] = GPCMD_REQUEST_SENSE;
	sense_rq->cmd[4] = cmd_len;
	sense_rq->cmd_type = REQ_TYPE_SENSE;
	sense_rq->cmd_flags |= REQ_PREEMPT;

	if (drive->media == ide_tape)
		sense_rq->cmd[13] = REQ_IDETAPE_PC1;

	drive->sense_rq_armed = true;
}
EXPORT_SYMBOL_GPL(ide_prep_sense);

int ide_queue_sense_rq(ide_drive_t *drive, void *special)
{
	/* deferred failure from ide_prep_sense() */
	if (!drive->sense_rq_armed) {
		printk(KERN_WARNING "%s: failed queue sense request\n",
		       drive->name);
		return -ENOMEM;
	}

	drive->sense_rq.special = special;
	drive->sense_rq_armed = false;

	drive->hwif->rq = NULL;

	elv_add_request(drive->queue, &drive->sense_rq,
			ELEVATOR_INSERT_FRONT, 0);
	return 0;
}
EXPORT_SYMBOL_GPL(ide_queue_sense_rq);

/*
 * Called when an error was detected during the last packet command.
 * We queue a request sense packet command at the head of the request
 * queue.
 */
void ide_retry_pc(ide_drive_t *drive)
{
	struct request *failed_rq = drive->hwif->rq;
	struct request *sense_rq = &drive->sense_rq;
	struct ide_atapi_pc *pc = &drive->request_sense_pc;

	(void)ide_read_error(drive);

	/* init pc from sense_rq */
	ide_init_pc(pc);
	memcpy(pc->c, sense_rq->cmd, 12);
	pc->buf = bio_data(sense_rq->bio);	/* pointer to mapped address */
	pc->req_xfer = blk_rq_bytes(sense_rq);

	if (drive->media == ide_tape)
		drive->atapi_flags |= IDE_AFLAG_IGNORE_DSC;

	/*
	 * Push back the failed request and put request sense on top
	 * of it.  The failed command will be retried after sense data
	 * is acquired.
	 */
	blk_requeue_request(failed_rq->q, failed_rq);
	drive->hwif->rq = NULL;
	if (ide_queue_sense_rq(drive, pc)) {
		blk_start_request(failed_rq);
		ide_complete_rq(drive, -EIO, blk_rq_bytes(failed_rq));
	}
}
EXPORT_SYMBOL_GPL(ide_retry_pc);

int ide_cd_expiry(ide_drive_t *drive)
{
	struct request *rq = drive->hwif->rq;
	unsigned long wait = 0;

	debug_log("%s: rq->cmd[0]: 0x%x\n", __func__, rq->cmd[0]);

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
			printk(KERN_INFO "cmd 0x%x timed out\n",
					 rq->cmd[0]);
		wait = 0;
		break;
	}
	return wait;
}
EXPORT_SYMBOL_GPL(ide_cd_expiry);

int ide_cd_get_xferlen(struct request *rq)
{
	if (blk_fs_request(rq))
		return 32768;
	else if (blk_sense_request(rq) || blk_pc_request(rq) ||
			 rq->cmd_type == REQ_TYPE_ATA_PC)
		return blk_rq_bytes(rq);
	else
		return 0;
}
EXPORT_SYMBOL_GPL(ide_cd_get_xferlen);

void ide_read_bcount_and_ireason(ide_drive_t *drive, u16 *bcount, u8 *ireason)
{
	struct ide_taskfile tf;

	drive->hwif->tp_ops->tf_read(drive, &tf, IDE_VALID_NSECT |
				     IDE_VALID_LBAM | IDE_VALID_LBAH);

	*bcount = (tf.lbah << 8) | tf.lbam;
	*ireason = tf.nsect & 3;
}
EXPORT_SYMBOL_GPL(ide_read_bcount_and_ireason);

/*
 * This is the usual interrupt handler which will be called during a packet
 * command.  We will transfer some of the data (as requested by the drive)
 * and will re-point interrupt handler to us.
 */
static ide_startstop_t ide_pc_intr(ide_drive_t *drive)
{
	struct ide_atapi_pc *pc = drive->pc;
	ide_hwif_t *hwif = drive->hwif;
	struct ide_cmd *cmd = &hwif->cmd;
	struct request *rq = hwif->rq;
	const struct ide_tp_ops *tp_ops = hwif->tp_ops;
	unsigned int timeout, done;
	u16 bcount;
	u8 stat, ireason, dsc = 0;
	u8 write = !!(pc->flags & PC_FLAG_WRITING);

	debug_log("Enter %s - interrupt handler\n", __func__);

	timeout = (drive->media == ide_floppy) ? WAIT_FLOPPY_CMD
					       : WAIT_TAPE_CMD;

	/* Clear the interrupt */
	stat = tp_ops->read_status(hwif);

	if (pc->flags & PC_FLAG_DMA_IN_PROGRESS) {
		int rc;

		drive->waiting_for_dma = 0;
		rc = hwif->dma_ops->dma_end(drive);
		ide_dma_unmap_sg(drive, cmd);

		if (rc || (drive->media == ide_tape && (stat & ATA_ERR))) {
			if (drive->media == ide_floppy)
				printk(KERN_ERR "%s: DMA %s error\n",
					drive->name, rq_data_dir(pc->rq)
						     ? "write" : "read");
			pc->flags |= PC_FLAG_DMA_ERROR;
		} else
			pc->xferred = pc->req_xfer;
		debug_log("%s: DMA finished\n", drive->name);
	}

	/* No more interrupts */
	if ((stat & ATA_DRQ) == 0) {
		int uptodate, error;

		debug_log("Packet command completed, %d bytes transferred\n",
			  pc->xferred);

		pc->flags &= ~PC_FLAG_DMA_IN_PROGRESS;

		local_irq_enable_in_hardirq();

		if (drive->media == ide_tape &&
		    (stat & ATA_ERR) && rq->cmd[0] == REQUEST_SENSE)
			stat &= ~ATA_ERR;

		if ((stat & ATA_ERR) || (pc->flags & PC_FLAG_DMA_ERROR)) {
			/* Error detected */
			debug_log("%s: I/O error\n", drive->name);

			if (drive->media != ide_tape)
				pc->rq->errors++;

			if (rq->cmd[0] == REQUEST_SENSE) {
				printk(KERN_ERR "%s: I/O error in request sense"
						" command\n", drive->name);
				return ide_do_reset(drive);
			}

			debug_log("[cmd %x]: check condition\n", rq->cmd[0]);

			/* Retry operation */
			ide_retry_pc(drive);

			/* queued, but not started */
			return ide_stopped;
		}
		pc->error = 0;

		if ((pc->flags & PC_FLAG_WAIT_FOR_DSC) && (stat & ATA_DSC) == 0)
			dsc = 1;

		/*
		 * ->pc_callback() might change rq->data_len for
		 * residual count, cache total length.
		 */
		done = blk_rq_bytes(rq);

		/* Command finished - Call the callback function */
		uptodate = drive->pc_callback(drive, dsc);

		if (uptodate == 0)
			drive->failed_pc = NULL;

		if (blk_special_request(rq)) {
			rq->errors = 0;
			error = 0;
		} else {

			if (blk_fs_request(rq) == 0 && uptodate <= 0) {
				if (rq->errors == 0)
					rq->errors = -EIO;
			}

			error = uptodate ? 0 : -EIO;
		}

		ide_complete_rq(drive, error, blk_rq_bytes(rq));
		return ide_stopped;
	}

	if (pc->flags & PC_FLAG_DMA_IN_PROGRESS) {
		pc->flags &= ~PC_FLAG_DMA_IN_PROGRESS;
		printk(KERN_ERR "%s: The device wants to issue more interrupts "
				"in DMA mode\n", drive->name);
		ide_dma_off(drive);
		return ide_do_reset(drive);
	}

	/* Get the number of bytes to transfer on this interrupt. */
	ide_read_bcount_and_ireason(drive, &bcount, &ireason);

	if (ireason & ATAPI_COD) {
		printk(KERN_ERR "%s: CoD != 0 in %s\n", drive->name, __func__);
		return ide_do_reset(drive);
	}

	if (((ireason & ATAPI_IO) == ATAPI_IO) == write) {
		/* Hopefully, we will never get here */
		printk(KERN_ERR "%s: We wanted to %s, but the device wants us "
				"to %s!\n", drive->name,
				(ireason & ATAPI_IO) ? "Write" : "Read",
				(ireason & ATAPI_IO) ? "Read" : "Write");
		return ide_do_reset(drive);
	}

	done = min_t(unsigned int, bcount, cmd->nleft);
	ide_pio_bytes(drive, cmd, write, done);

	/* Update transferred byte count */
	pc->xferred += done;

	bcount -= done;

	if (bcount)
		ide_pad_transfer(drive, write, bcount);

	debug_log("[cmd %x] transferred %d bytes, padded %d bytes\n",
		  rq->cmd[0], done, bcount);

	/* And set the interrupt handler again */
	ide_set_handler(drive, ide_pc_intr, timeout);
	return ide_started;
}

static void ide_init_packet_cmd(struct ide_cmd *cmd, u8 valid_tf,
				u16 bcount, u8 dma)
{
	cmd->protocol = dma ? ATAPI_PROT_DMA : ATAPI_PROT_PIO;
	cmd->valid.out.tf = IDE_VALID_LBAH | IDE_VALID_LBAM |
			    IDE_VALID_FEATURE | valid_tf;
	cmd->tf.command = ATA_CMD_PACKET;
	cmd->tf.feature = dma;		/* Use PIO/DMA */
	cmd->tf.lbam    = bcount & 0xff;
	cmd->tf.lbah    = (bcount >> 8) & 0xff;
}

static u8 ide_read_ireason(ide_drive_t *drive)
{
	struct ide_taskfile tf;

	drive->hwif->tp_ops->tf_read(drive, &tf, IDE_VALID_NSECT);

	return tf.nsect & 3;
}

static u8 ide_wait_ireason(ide_drive_t *drive, u8 ireason)
{
	int retries = 100;

	while (retries-- && ((ireason & ATAPI_COD) == 0 ||
		(ireason & ATAPI_IO))) {
		printk(KERN_ERR "%s: (IO,CoD != (0,1) while issuing "
				"a packet command, retrying\n", drive->name);
		udelay(100);
		ireason = ide_read_ireason(drive);
		if (retries == 0) {
			printk(KERN_ERR "%s: (IO,CoD != (0,1) while issuing "
					"a packet command, ignoring\n",
					drive->name);
			ireason |= ATAPI_COD;
			ireason &= ~ATAPI_IO;
		}
	}

	return ireason;
}

static int ide_delayed_transfer_pc(ide_drive_t *drive)
{
	/* Send the actual packet */
	drive->hwif->tp_ops->output_data(drive, NULL, drive->pc->c, 12);

	/* Timeout for the packet command */
	return WAIT_FLOPPY_CMD;
}

static ide_startstop_t ide_transfer_pc(ide_drive_t *drive)
{
	struct ide_atapi_pc *uninitialized_var(pc);
	ide_hwif_t *hwif = drive->hwif;
	struct request *rq = hwif->rq;
	ide_expiry_t *expiry;
	unsigned int timeout;
	int cmd_len;
	ide_startstop_t startstop;
	u8 ireason;

	if (ide_wait_stat(&startstop, drive, ATA_DRQ, ATA_BUSY, WAIT_READY)) {
		printk(KERN_ERR "%s: Strange, packet command initiated yet "
				"DRQ isn't asserted\n", drive->name);
		return startstop;
	}

	if (drive->atapi_flags & IDE_AFLAG_DRQ_INTERRUPT) {
		if (drive->dma)
			drive->waiting_for_dma = 1;
	}

	if (dev_is_idecd(drive)) {
		/* ATAPI commands get padded out to 12 bytes minimum */
		cmd_len = COMMAND_SIZE(rq->cmd[0]);
		if (cmd_len < ATAPI_MIN_CDB_BYTES)
			cmd_len = ATAPI_MIN_CDB_BYTES;

		timeout = rq->timeout;
		expiry  = ide_cd_expiry;
	} else {
		pc = drive->pc;

		cmd_len = ATAPI_MIN_CDB_BYTES;

		/*
		 * If necessary schedule the packet transfer to occur 'timeout'
		 * milliseconds later in ide_delayed_transfer_pc() after the
		 * device says it's ready for a packet.
		 */
		if (drive->atapi_flags & IDE_AFLAG_ZIP_DRIVE) {
			timeout = drive->pc_delay;
			expiry = &ide_delayed_transfer_pc;
		} else {
			timeout = (drive->media == ide_floppy) ? WAIT_FLOPPY_CMD
							       : WAIT_TAPE_CMD;
			expiry = NULL;
		}

		ireason = ide_read_ireason(drive);
		if (drive->media == ide_tape)
			ireason = ide_wait_ireason(drive, ireason);

		if ((ireason & ATAPI_COD) == 0 || (ireason & ATAPI_IO)) {
			printk(KERN_ERR "%s: (IO,CoD) != (0,1) while issuing "
					"a packet command\n", drive->name);

			return ide_do_reset(drive);
		}
	}

	hwif->expiry = expiry;

	/* Set the interrupt routine */
	ide_set_handler(drive,
			(dev_is_idecd(drive) ? drive->irq_handler
					     : ide_pc_intr),
			timeout);

	/* Send the actual packet */
	if ((drive->atapi_flags & IDE_AFLAG_ZIP_DRIVE) == 0)
		hwif->tp_ops->output_data(drive, NULL, rq->cmd, cmd_len);

	/* Begin DMA, if necessary */
	if (dev_is_idecd(drive)) {
		if (drive->dma)
			hwif->dma_ops->dma_start(drive);
	} else {
		if (pc->flags & PC_FLAG_DMA_OK) {
			pc->flags |= PC_FLAG_DMA_IN_PROGRESS;
			hwif->dma_ops->dma_start(drive);
		}
	}

	return ide_started;
}

ide_startstop_t ide_issue_pc(ide_drive_t *drive, struct ide_cmd *cmd)
{
	struct ide_atapi_pc *pc;
	ide_hwif_t *hwif = drive->hwif;
	ide_expiry_t *expiry = NULL;
	struct request *rq = hwif->rq;
	unsigned int timeout;
	u16 bcount;
	u8 valid_tf;
	u8 drq_int = !!(drive->atapi_flags & IDE_AFLAG_DRQ_INTERRUPT);

	if (dev_is_idecd(drive)) {
		valid_tf = IDE_VALID_NSECT | IDE_VALID_LBAL;
		bcount = ide_cd_get_xferlen(rq);
		expiry = ide_cd_expiry;
		timeout = ATAPI_WAIT_PC;

		if (drive->dma)
			drive->dma = !ide_dma_prepare(drive, cmd);
	} else {
		pc = drive->pc;

		/* We haven't transferred any data yet */
		pc->xferred = 0;

		valid_tf = IDE_VALID_DEVICE;
		bcount = ((drive->media == ide_tape) ?
				pc->req_xfer :
				min(pc->req_xfer, 63 * 1024));

		if (pc->flags & PC_FLAG_DMA_ERROR) {
			pc->flags &= ~PC_FLAG_DMA_ERROR;
			ide_dma_off(drive);
		}

		if (pc->flags & PC_FLAG_DMA_OK)
			drive->dma = !ide_dma_prepare(drive, cmd);

		if (!drive->dma)
			pc->flags &= ~PC_FLAG_DMA_OK;

		timeout = (drive->media == ide_floppy) ? WAIT_FLOPPY_CMD
						       : WAIT_TAPE_CMD;
	}

	ide_init_packet_cmd(cmd, valid_tf, bcount, drive->dma);

	(void)do_rw_taskfile(drive, cmd);

	if (drq_int) {
		if (drive->dma)
			drive->waiting_for_dma = 0;
		hwif->expiry = expiry;
	}

	ide_execute_command(drive, cmd, ide_transfer_pc, timeout);

	return drq_int ? ide_started : ide_transfer_pc(drive);
}
EXPORT_SYMBOL_GPL(ide_issue_pc);
