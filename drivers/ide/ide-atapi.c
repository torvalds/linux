/*
 * ATAPI support.
 */

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ide.h>
#include <scsi/scsi.h>

#ifdef DEBUG
#define debug_log(fmt, args...) \
	printk(KERN_INFO "ide: " fmt, ## args)
#else
#define debug_log(fmt, args...) do {} while (0)
#endif

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

/* TODO: unify the code thus making some arguments go away */
ide_startstop_t ide_pc_intr(ide_drive_t *drive, struct ide_atapi_pc *pc,
	ide_handler_t *handler, unsigned int timeout, ide_expiry_t *expiry,
	void (*update_buffers)(ide_drive_t *, struct ide_atapi_pc *),
	void (*retry_pc)(ide_drive_t *), void (*dsc_handle)(ide_drive_t *),
	void (*io_buffers)(ide_drive_t *, struct ide_atapi_pc *, unsigned, int))
{
	ide_hwif_t *hwif = drive->hwif;
	struct request *rq = hwif->hwgroup->rq;
	const struct ide_tp_ops *tp_ops = hwif->tp_ops;
	xfer_func_t *xferfunc;
	unsigned int temp;
	u16 bcount;
	u8 stat, ireason, scsi = drive->scsi;

	debug_log("Enter %s - interrupt handler\n", __func__);

	if (pc->flags & PC_FLAG_TIMEDOUT) {
		drive->pc_callback(drive);
		return ide_stopped;
	}

	/* Clear the interrupt */
	stat = tp_ops->read_status(hwif);

	if (pc->flags & PC_FLAG_DMA_IN_PROGRESS) {
		if (hwif->dma_ops->dma_end(drive) ||
		    (drive->media == ide_tape && !scsi && (stat & ATA_ERR))) {
			if (drive->media == ide_floppy && !scsi)
				printk(KERN_ERR "%s: DMA %s error\n",
					drive->name, rq_data_dir(pc->rq)
						     ? "write" : "read");
			pc->flags |= PC_FLAG_DMA_ERROR;
		} else {
			pc->xferred = pc->req_xfer;
			if (update_buffers)
				update_buffers(drive, pc);
		}
		debug_log("%s: DMA finished\n", drive->name);
	}

	/* No more interrupts */
	if ((stat & ATA_DRQ) == 0) {
		debug_log("Packet command completed, %d bytes transferred\n",
			  pc->xferred);

		pc->flags &= ~PC_FLAG_DMA_IN_PROGRESS;

		local_irq_enable_in_hardirq();

		if (drive->media == ide_tape && !scsi &&
		    (stat & ATA_ERR) && rq->cmd[0] == REQUEST_SENSE)
			stat &= ~ATA_ERR;

		if ((stat & ATA_ERR) || (pc->flags & PC_FLAG_DMA_ERROR)) {
			/* Error detected */
			debug_log("%s: I/O error\n", drive->name);

			if (drive->media != ide_tape || scsi) {
				pc->rq->errors++;
				if (scsi)
					goto cmd_finished;
			}

			if (rq->cmd[0] == REQUEST_SENSE) {
				printk(KERN_ERR "%s: I/O error in request sense"
						" command\n", drive->name);
				return ide_do_reset(drive);
			}

			debug_log("[cmd %x]: check condition\n", rq->cmd[0]);

			/* Retry operation */
			retry_pc(drive);

			/* queued, but not started */
			return ide_stopped;
		}
cmd_finished:
		pc->error = 0;
		if ((pc->flags & PC_FLAG_WAIT_FOR_DSC) &&
		    (stat & ATA_DSC) == 0) {
			dsc_handle(drive);
			return ide_stopped;
		}

		/* Command finished - Call the callback function */
		drive->pc_callback(drive);

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

	if (((ireason & ATAPI_IO) == ATAPI_IO) ==
		!!(pc->flags & PC_FLAG_WRITING)) {
		/* Hopefully, we will never get here */
		printk(KERN_ERR "%s: We wanted to %s, but the device wants us "
				"to %s!\n", drive->name,
				(ireason & ATAPI_IO) ? "Write" : "Read",
				(ireason & ATAPI_IO) ? "Read" : "Write");
		return ide_do_reset(drive);
	}

	if (!(pc->flags & PC_FLAG_WRITING)) {
		/* Reading - Check that we have enough space */
		temp = pc->xferred + bcount;
		if (temp > pc->req_xfer) {
			if (temp > pc->buf_size) {
				printk(KERN_ERR "%s: The device wants to send "
						"us more data than expected - "
						"discarding data\n",
						drive->name);
				if (scsi)
					temp = pc->buf_size - pc->xferred;
				else
					temp = 0;
				if (temp) {
					if (pc->sg)
						io_buffers(drive, pc, temp, 0);
					else
						tp_ops->input_data(drive, NULL,
							pc->cur_pos, temp);
					printk(KERN_ERR "%s: transferred %d of "
							"%d bytes\n",
							drive->name,
							temp, bcount);
				}
				pc->xferred += temp;
				pc->cur_pos += temp;
				ide_pad_transfer(drive, 0, bcount - temp);
				ide_set_handler(drive, handler, timeout,
						expiry);
				return ide_started;
			}
			debug_log("The device wants to send us more data than "
				  "expected - allowing transfer\n");
		}
		xferfunc = tp_ops->input_data;
	} else
		xferfunc = tp_ops->output_data;

	if ((drive->media == ide_floppy && !scsi && !pc->buf) ||
	    (drive->media == ide_tape && !scsi && pc->bh) ||
	    (scsi && pc->sg))
		io_buffers(drive, pc, bcount, !!(pc->flags & PC_FLAG_WRITING));
	else
		xferfunc(drive, NULL, pc->cur_pos, bcount);

	/* Update the current position */
	pc->xferred += bcount;
	pc->cur_pos += bcount;

	debug_log("[cmd %x] transferred %d bytes on that intr.\n",
		  rq->cmd[0], bcount);

	/* And set the interrupt handler again */
	ide_set_handler(drive, handler, timeout, expiry);
	return ide_started;
}
EXPORT_SYMBOL_GPL(ide_pc_intr);

static u8 ide_read_ireason(ide_drive_t *drive)
{
	ide_task_t task;

	memset(&task, 0, sizeof(task));
	task.tf_flags = IDE_TFLAG_IN_NSECT;

	drive->hwif->tp_ops->tf_read(drive, &task);

	return task.tf.nsect & 3;
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

ide_startstop_t ide_transfer_pc(ide_drive_t *drive, struct ide_atapi_pc *pc,
				ide_handler_t *handler, unsigned int timeout,
				ide_expiry_t *expiry)
{
	ide_hwif_t *hwif = drive->hwif;
	struct request *rq = hwif->hwgroup->rq;
	ide_startstop_t startstop;
	u8 ireason;

	if (ide_wait_stat(&startstop, drive, ATA_DRQ, ATA_BUSY, WAIT_READY)) {
		printk(KERN_ERR "%s: Strange, packet command initiated yet "
				"DRQ isn't asserted\n", drive->name);
		return startstop;
	}

	ireason = ide_read_ireason(drive);
	if (drive->media == ide_tape && !drive->scsi)
		ireason = ide_wait_ireason(drive, ireason);

	if ((ireason & ATAPI_COD) == 0 || (ireason & ATAPI_IO)) {
		printk(KERN_ERR "%s: (IO,CoD) != (0,1) while issuing "
				"a packet command\n", drive->name);
		return ide_do_reset(drive);
	}

	/* Set the interrupt routine */
	ide_set_handler(drive, handler, timeout, expiry);

	/* Begin DMA, if necessary */
	if (pc->flags & PC_FLAG_DMA_OK) {
		pc->flags |= PC_FLAG_DMA_IN_PROGRESS;
		hwif->dma_ops->dma_start(drive);
	}

	/* Send the actual packet */
	if ((drive->atapi_flags & IDE_AFLAG_ZIP_DRIVE) == 0)
		hwif->tp_ops->output_data(drive, NULL, rq->cmd, 12);

	return ide_started;
}
EXPORT_SYMBOL_GPL(ide_transfer_pc);

ide_startstop_t ide_issue_pc(ide_drive_t *drive, struct ide_atapi_pc *pc,
			     ide_handler_t *handler, unsigned int timeout,
			     ide_expiry_t *expiry)
{
	ide_hwif_t *hwif = drive->hwif;
	u16 bcount;
	u8 dma = 0;

	/* We haven't transferred any data yet */
	pc->xferred = 0;
	pc->cur_pos = pc->buf;

	/* Request to transfer the entire buffer at once */
	if (drive->media == ide_tape && !drive->scsi)
		bcount = pc->req_xfer;
	else
		bcount = min(pc->req_xfer, 63 * 1024);

	if (pc->flags & PC_FLAG_DMA_ERROR) {
		pc->flags &= ~PC_FLAG_DMA_ERROR;
		ide_dma_off(drive);
	}

	if ((pc->flags & PC_FLAG_DMA_OK) && drive->using_dma) {
		if (drive->scsi)
			hwif->sg_mapped = 1;
		dma = !hwif->dma_ops->dma_setup(drive);
		if (drive->scsi)
			hwif->sg_mapped = 0;
	}

	if (!dma)
		pc->flags &= ~PC_FLAG_DMA_OK;

	ide_pktcmd_tf_load(drive, drive->scsi ? 0 : IDE_TFLAG_OUT_DEVICE,
			   bcount, dma);

	/* Issue the packet command */
	if (drive->atapi_flags & IDE_AFLAG_DRQ_INTERRUPT) {
		ide_execute_command(drive, ATA_CMD_PACKET, handler,
				    timeout, NULL);
		return ide_started;
	} else {
		ide_execute_pkt_cmd(drive);
		return (*handler)(drive);
	}
}
EXPORT_SYMBOL_GPL(ide_issue_pc);
