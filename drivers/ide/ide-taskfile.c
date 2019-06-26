// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Copyright (C) 2000-2002	   Michael Cornwell <cornwell@acm.org>
 *  Copyright (C) 2000-2002	   Andre Hedrick <andre@linux-ide.org>
 *  Copyright (C) 2001-2002	   Klaus Smolin
 *					IBM Storage Technology Division
 *  Copyright (C) 2003-2004, 2007  Bartlomiej Zolnierkiewicz
 *
 *  The big the bad and the ugly.
 */

#include <linux/types.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/hdreg.h>
#include <linux/ide.h>
#include <linux/nmi.h>
#include <linux/scatterlist.h>
#include <linux/uaccess.h>

#include <asm/io.h>

void ide_tf_readback(ide_drive_t *drive, struct ide_cmd *cmd)
{
	ide_hwif_t *hwif = drive->hwif;
	const struct ide_tp_ops *tp_ops = hwif->tp_ops;

	/* Be sure we're looking at the low order bytes */
	tp_ops->write_devctl(hwif, ATA_DEVCTL_OBS);

	tp_ops->tf_read(drive, &cmd->tf, cmd->valid.in.tf);

	if (cmd->tf_flags & IDE_TFLAG_LBA48) {
		tp_ops->write_devctl(hwif, ATA_HOB | ATA_DEVCTL_OBS);

		tp_ops->tf_read(drive, &cmd->hob, cmd->valid.in.hob);
	}
}

void ide_tf_dump(const char *s, struct ide_cmd *cmd)
{
#ifdef DEBUG
	printk("%s: tf: feat 0x%02x nsect 0x%02x lbal 0x%02x "
		"lbam 0x%02x lbah 0x%02x dev 0x%02x cmd 0x%02x\n",
	       s, cmd->tf.feature, cmd->tf.nsect,
	       cmd->tf.lbal, cmd->tf.lbam, cmd->tf.lbah,
	       cmd->tf.device, cmd->tf.command);
	printk("%s: hob: nsect 0x%02x lbal 0x%02x lbam 0x%02x lbah 0x%02x\n",
	       s, cmd->hob.nsect, cmd->hob.lbal, cmd->hob.lbam, cmd->hob.lbah);
#endif
}

int taskfile_lib_get_identify(ide_drive_t *drive, u8 *buf)
{
	struct ide_cmd cmd;

	memset(&cmd, 0, sizeof(cmd));
	cmd.tf.nsect = 0x01;
	if (drive->media == ide_disk)
		cmd.tf.command = ATA_CMD_ID_ATA;
	else
		cmd.tf.command = ATA_CMD_ID_ATAPI;
	cmd.valid.out.tf = IDE_VALID_OUT_TF | IDE_VALID_DEVICE;
	cmd.valid.in.tf  = IDE_VALID_IN_TF  | IDE_VALID_DEVICE;
	cmd.protocol = ATA_PROT_PIO;

	return ide_raw_taskfile(drive, &cmd, buf, 1);
}

static ide_startstop_t task_no_data_intr(ide_drive_t *);
static ide_startstop_t pre_task_out_intr(ide_drive_t *, struct ide_cmd *);
static ide_startstop_t task_pio_intr(ide_drive_t *);

ide_startstop_t do_rw_taskfile(ide_drive_t *drive, struct ide_cmd *orig_cmd)
{
	ide_hwif_t *hwif = drive->hwif;
	struct ide_cmd *cmd = &hwif->cmd;
	struct ide_taskfile *tf = &cmd->tf;
	ide_handler_t *handler = NULL;
	const struct ide_tp_ops *tp_ops = hwif->tp_ops;
	const struct ide_dma_ops *dma_ops = hwif->dma_ops;

	if (orig_cmd->protocol == ATA_PROT_PIO &&
	    (orig_cmd->tf_flags & IDE_TFLAG_MULTI_PIO) &&
	    drive->mult_count == 0) {
		pr_err("%s: multimode not set!\n", drive->name);
		return ide_stopped;
	}

	if (orig_cmd->ftf_flags & IDE_FTFLAG_FLAGGED)
		orig_cmd->ftf_flags |= IDE_FTFLAG_SET_IN_FLAGS;

	memcpy(cmd, orig_cmd, sizeof(*cmd));

	if ((cmd->tf_flags & IDE_TFLAG_DMA_PIO_FALLBACK) == 0) {
		ide_tf_dump(drive->name, cmd);
		tp_ops->write_devctl(hwif, ATA_DEVCTL_OBS);

		if (cmd->ftf_flags & IDE_FTFLAG_OUT_DATA) {
			u8 data[2] = { cmd->tf.data, cmd->hob.data };

			tp_ops->output_data(drive, cmd, data, 2);
		}

		if (cmd->valid.out.tf & IDE_VALID_DEVICE) {
			u8 HIHI = (cmd->tf_flags & IDE_TFLAG_LBA48) ?
				  0xE0 : 0xEF;

			if (!(cmd->ftf_flags & IDE_FTFLAG_FLAGGED))
				cmd->tf.device &= HIHI;
			cmd->tf.device |= drive->select;
		}

		tp_ops->tf_load(drive, &cmd->hob, cmd->valid.out.hob);
		tp_ops->tf_load(drive, &cmd->tf,  cmd->valid.out.tf);
	}

	switch (cmd->protocol) {
	case ATA_PROT_PIO:
		if (cmd->tf_flags & IDE_TFLAG_WRITE) {
			tp_ops->exec_command(hwif, tf->command);
			ndelay(400);	/* FIXME */
			return pre_task_out_intr(drive, cmd);
		}
		handler = task_pio_intr;
		/* fall through */
	case ATA_PROT_NODATA:
		if (handler == NULL)
			handler = task_no_data_intr;
		ide_execute_command(drive, cmd, handler, WAIT_WORSTCASE);
		return ide_started;
	case ATA_PROT_DMA:
		if (ide_dma_prepare(drive, cmd))
			return ide_stopped;
		hwif->expiry = dma_ops->dma_timer_expiry;
		ide_execute_command(drive, cmd, ide_dma_intr, 2 * WAIT_CMD);
		dma_ops->dma_start(drive);
		/* fall through */
	default:
		return ide_started;
	}
}
EXPORT_SYMBOL_GPL(do_rw_taskfile);

static ide_startstop_t task_no_data_intr(ide_drive_t *drive)
{
	ide_hwif_t *hwif = drive->hwif;
	struct ide_cmd *cmd = &hwif->cmd;
	struct ide_taskfile *tf = &cmd->tf;
	int custom = (cmd->tf_flags & IDE_TFLAG_CUSTOM_HANDLER) ? 1 : 0;
	int retries = (custom && tf->command == ATA_CMD_INIT_DEV_PARAMS) ? 5 : 1;
	u8 stat;

	local_irq_enable_in_hardirq();

	while (1) {
		stat = hwif->tp_ops->read_status(hwif);
		if ((stat & ATA_BUSY) == 0 || retries-- == 0)
			break;
		udelay(10);
	};

	if (!OK_STAT(stat, ATA_DRDY, BAD_STAT)) {
		if (custom && tf->command == ATA_CMD_SET_MULTI) {
			drive->mult_req = drive->mult_count = 0;
			drive->special_flags |= IDE_SFLAG_RECALIBRATE;
			(void)ide_dump_status(drive, __func__, stat);
			return ide_stopped;
		} else if (custom && tf->command == ATA_CMD_INIT_DEV_PARAMS) {
			if ((stat & (ATA_ERR | ATA_DRQ)) == 0) {
				ide_set_handler(drive, &task_no_data_intr,
						WAIT_WORSTCASE);
				return ide_started;
			}
		}
		return ide_error(drive, "task_no_data_intr", stat);
	}

	if (custom && tf->command == ATA_CMD_SET_MULTI)
		drive->mult_count = drive->mult_req;

	if (custom == 0 || tf->command == ATA_CMD_IDLEIMMEDIATE ||
	    tf->command == ATA_CMD_CHK_POWER) {
		struct request *rq = hwif->rq;

		if (ata_pm_request(rq))
			ide_complete_pm_rq(drive, rq);
		else
			ide_finish_cmd(drive, cmd, stat);
	}

	return ide_stopped;
}

static u8 wait_drive_not_busy(ide_drive_t *drive)
{
	ide_hwif_t *hwif = drive->hwif;
	int retries;
	u8 stat;

	/*
	 * Last sector was transferred, wait until device is ready.  This can
	 * take up to 6 ms on some ATAPI devices, so we will wait max 10 ms.
	 */
	for (retries = 0; retries < 1000; retries++) {
		stat = hwif->tp_ops->read_status(hwif);

		if (stat & ATA_BUSY)
			udelay(10);
		else
			break;
	}

	if (stat & ATA_BUSY)
		pr_err("%s: drive still BUSY!\n", drive->name);

	return stat;
}

void ide_pio_bytes(ide_drive_t *drive, struct ide_cmd *cmd,
		   unsigned int write, unsigned int len)
{
	ide_hwif_t *hwif = drive->hwif;
	struct scatterlist *sg = hwif->sg_table;
	struct scatterlist *cursg = cmd->cursg;
	unsigned long uninitialized_var(flags);
	struct page *page;
	unsigned int offset;
	u8 *buf;

	if (cursg == NULL)
		cursg = cmd->cursg = sg;

	while (len) {
		unsigned nr_bytes = min(len, cursg->length - cmd->cursg_ofs);

		page = sg_page(cursg);
		offset = cursg->offset + cmd->cursg_ofs;

		/* get the current page and offset */
		page = nth_page(page, (offset >> PAGE_SHIFT));
		offset %= PAGE_SIZE;

		nr_bytes = min_t(unsigned, nr_bytes, (PAGE_SIZE - offset));

		buf = kmap_atomic(page) + offset;

		cmd->nleft -= nr_bytes;
		cmd->cursg_ofs += nr_bytes;

		if (cmd->cursg_ofs == cursg->length) {
			cursg = cmd->cursg = sg_next(cmd->cursg);
			cmd->cursg_ofs = 0;
		}

		/* do the actual data transfer */
		if (write)
			hwif->tp_ops->output_data(drive, cmd, buf, nr_bytes);
		else
			hwif->tp_ops->input_data(drive, cmd, buf, nr_bytes);

		kunmap_atomic(buf);

		len -= nr_bytes;
	}
}
EXPORT_SYMBOL_GPL(ide_pio_bytes);

static void ide_pio_datablock(ide_drive_t *drive, struct ide_cmd *cmd,
			      unsigned int write)
{
	unsigned int nr_bytes;

	u8 saved_io_32bit = drive->io_32bit;

	if (cmd->tf_flags & IDE_TFLAG_FS)
		scsi_req(cmd->rq)->result = 0;

	if (cmd->tf_flags & IDE_TFLAG_IO_16BIT)
		drive->io_32bit = 0;

	touch_softlockup_watchdog();

	if (cmd->tf_flags & IDE_TFLAG_MULTI_PIO)
		nr_bytes = min_t(unsigned, cmd->nleft, drive->mult_count << 9);
	else
		nr_bytes = SECTOR_SIZE;

	ide_pio_bytes(drive, cmd, write, nr_bytes);

	drive->io_32bit = saved_io_32bit;
}

static void ide_error_cmd(ide_drive_t *drive, struct ide_cmd *cmd)
{
	if (cmd->tf_flags & IDE_TFLAG_FS) {
		int nr_bytes = cmd->nbytes - cmd->nleft;

		if (cmd->protocol == ATA_PROT_PIO &&
		    ((cmd->tf_flags & IDE_TFLAG_WRITE) || cmd->nleft == 0)) {
			if (cmd->tf_flags & IDE_TFLAG_MULTI_PIO)
				nr_bytes -= drive->mult_count << 9;
			else
				nr_bytes -= SECTOR_SIZE;
		}

		if (nr_bytes > 0)
			ide_complete_rq(drive, BLK_STS_OK, nr_bytes);
	}
}

void ide_finish_cmd(ide_drive_t *drive, struct ide_cmd *cmd, u8 stat)
{
	struct request *rq = drive->hwif->rq;
	u8 err = ide_read_error(drive), nsect = cmd->tf.nsect;
	u8 set_xfer = !!(cmd->tf_flags & IDE_TFLAG_SET_XFER);

	ide_complete_cmd(drive, cmd, stat, err);
	scsi_req(rq)->result = err;

	if (err == 0 && set_xfer) {
		ide_set_xfer_rate(drive, nsect);
		ide_driveid_update(drive);
	}

	ide_complete_rq(drive, err ? BLK_STS_IOERR : BLK_STS_OK, blk_rq_bytes(rq));
}

/*
 * Handler for command with PIO data phase.
 */
static ide_startstop_t task_pio_intr(ide_drive_t *drive)
{
	ide_hwif_t *hwif = drive->hwif;
	struct ide_cmd *cmd = &drive->hwif->cmd;
	u8 stat = hwif->tp_ops->read_status(hwif);
	u8 write = !!(cmd->tf_flags & IDE_TFLAG_WRITE);

	if (write == 0) {
		/* Error? */
		if (stat & ATA_ERR)
			goto out_err;

		/* Didn't want any data? Odd. */
		if ((stat & ATA_DRQ) == 0) {
			/* Command all done? */
			if (OK_STAT(stat, ATA_DRDY, ATA_BUSY))
				goto out_end;

			/* Assume it was a spurious irq */
			goto out_wait;
		}
	} else {
		if (!OK_STAT(stat, DRIVE_READY, drive->bad_wstat))
			goto out_err;

		/* Deal with unexpected ATA data phase. */
		if (((stat & ATA_DRQ) == 0) ^ (cmd->nleft == 0))
			goto out_err;
	}

	if (write && cmd->nleft == 0)
		goto out_end;

	/* Still data left to transfer. */
	ide_pio_datablock(drive, cmd, write);

	/* Are we done? Check status and finish transfer. */
	if (write == 0 && cmd->nleft == 0) {
		stat = wait_drive_not_busy(drive);
		if (!OK_STAT(stat, 0, BAD_STAT))
			goto out_err;

		goto out_end;
	}
out_wait:
	/* Still data left to transfer. */
	ide_set_handler(drive, &task_pio_intr, WAIT_WORSTCASE);
	return ide_started;
out_end:
	if ((cmd->tf_flags & IDE_TFLAG_FS) == 0)
		ide_finish_cmd(drive, cmd, stat);
	else
		ide_complete_rq(drive, BLK_STS_OK, blk_rq_sectors(cmd->rq) << 9);
	return ide_stopped;
out_err:
	ide_error_cmd(drive, cmd);
	return ide_error(drive, __func__, stat);
}

static ide_startstop_t pre_task_out_intr(ide_drive_t *drive,
					 struct ide_cmd *cmd)
{
	ide_startstop_t startstop;

	if (ide_wait_stat(&startstop, drive, ATA_DRQ,
			  drive->bad_wstat, WAIT_DRQ)) {
		pr_err("%s: no DRQ after issuing %sWRITE%s\n", drive->name,
			(cmd->tf_flags & IDE_TFLAG_MULTI_PIO) ? "MULT" : "",
			(drive->dev_flags & IDE_DFLAG_LBA48) ? "_EXT" : "");
		return startstop;
	}

	if (!force_irqthreads && (drive->dev_flags & IDE_DFLAG_UNMASK) == 0)
		local_irq_disable();

	ide_set_handler(drive, &task_pio_intr, WAIT_WORSTCASE);

	ide_pio_datablock(drive, cmd, 1);

	return ide_started;
}

int ide_raw_taskfile(ide_drive_t *drive, struct ide_cmd *cmd, u8 *buf,
		     u16 nsect)
{
	struct request *rq;
	int error;

	rq = blk_get_request(drive->queue,
		(cmd->tf_flags & IDE_TFLAG_WRITE) ?
			REQ_OP_DRV_OUT : REQ_OP_DRV_IN, 0);
	ide_req(rq)->type = ATA_PRIV_TASKFILE;

	/*
	 * (ks) We transfer currently only whole sectors.
	 * This is suffient for now.  But, it would be great,
	 * if we would find a solution to transfer any size.
	 * To support special commands like READ LONG.
	 */
	if (nsect) {
		error = blk_rq_map_kern(drive->queue, rq, buf,
					nsect * SECTOR_SIZE, GFP_NOIO);
		if (error)
			goto put_req;
	}

	ide_req(rq)->special = cmd;
	cmd->rq = rq;

	blk_execute_rq(drive->queue, NULL, rq, 0);
	error = scsi_req(rq)->result ? -EIO : 0;
put_req:
	blk_put_request(rq);
	return error;
}
EXPORT_SYMBOL(ide_raw_taskfile);

int ide_no_data_taskfile(ide_drive_t *drive, struct ide_cmd *cmd)
{
	cmd->protocol = ATA_PROT_NODATA;

	return ide_raw_taskfile(drive, cmd, NULL, 0);
}
EXPORT_SYMBOL_GPL(ide_no_data_taskfile);

#ifdef CONFIG_IDE_TASK_IOCTL
int ide_taskfile_ioctl(ide_drive_t *drive, unsigned long arg)
{
	ide_task_request_t	*req_task;
	struct ide_cmd		cmd;
	u8 *outbuf		= NULL;
	u8 *inbuf		= NULL;
	u8 *data_buf		= NULL;
	int err			= 0;
	int tasksize		= sizeof(struct ide_task_request_s);
	unsigned int taskin	= 0;
	unsigned int taskout	= 0;
	u16 nsect		= 0;
	char __user *buf = (char __user *)arg;

	req_task = memdup_user(buf, tasksize);
	if (IS_ERR(req_task))
		return PTR_ERR(req_task);

	taskout = req_task->out_size;
	taskin  = req_task->in_size;

	if (taskin > 65536 || taskout > 65536) {
		err = -EINVAL;
		goto abort;
	}

	if (taskout) {
		int outtotal = tasksize;
		outbuf = kzalloc(taskout, GFP_KERNEL);
		if (outbuf == NULL) {
			err = -ENOMEM;
			goto abort;
		}
		if (copy_from_user(outbuf, buf + outtotal, taskout)) {
			err = -EFAULT;
			goto abort;
		}
	}

	if (taskin) {
		int intotal = tasksize + taskout;
		inbuf = kzalloc(taskin, GFP_KERNEL);
		if (inbuf == NULL) {
			err = -ENOMEM;
			goto abort;
		}
		if (copy_from_user(inbuf, buf + intotal, taskin)) {
			err = -EFAULT;
			goto abort;
		}
	}

	memset(&cmd, 0, sizeof(cmd));

	memcpy(&cmd.hob, req_task->hob_ports, HDIO_DRIVE_HOB_HDR_SIZE - 2);
	memcpy(&cmd.tf,  req_task->io_ports,  HDIO_DRIVE_TASK_HDR_SIZE);

	cmd.valid.out.tf = IDE_VALID_DEVICE;
	cmd.valid.in.tf  = IDE_VALID_DEVICE | IDE_VALID_IN_TF;
	cmd.tf_flags = IDE_TFLAG_IO_16BIT;

	if (drive->dev_flags & IDE_DFLAG_LBA48) {
		cmd.tf_flags |= IDE_TFLAG_LBA48;
		cmd.valid.in.hob = IDE_VALID_IN_HOB;
	}

	if (req_task->out_flags.all) {
		cmd.ftf_flags |= IDE_FTFLAG_FLAGGED;

		if (req_task->out_flags.b.data)
			cmd.ftf_flags |= IDE_FTFLAG_OUT_DATA;

		if (req_task->out_flags.b.nsector_hob)
			cmd.valid.out.hob |= IDE_VALID_NSECT;
		if (req_task->out_flags.b.sector_hob)
			cmd.valid.out.hob |= IDE_VALID_LBAL;
		if (req_task->out_flags.b.lcyl_hob)
			cmd.valid.out.hob |= IDE_VALID_LBAM;
		if (req_task->out_flags.b.hcyl_hob)
			cmd.valid.out.hob |= IDE_VALID_LBAH;

		if (req_task->out_flags.b.error_feature)
			cmd.valid.out.tf  |= IDE_VALID_FEATURE;
		if (req_task->out_flags.b.nsector)
			cmd.valid.out.tf  |= IDE_VALID_NSECT;
		if (req_task->out_flags.b.sector)
			cmd.valid.out.tf  |= IDE_VALID_LBAL;
		if (req_task->out_flags.b.lcyl)
			cmd.valid.out.tf  |= IDE_VALID_LBAM;
		if (req_task->out_flags.b.hcyl)
			cmd.valid.out.tf  |= IDE_VALID_LBAH;
	} else {
		cmd.valid.out.tf |= IDE_VALID_OUT_TF;
		if (cmd.tf_flags & IDE_TFLAG_LBA48)
			cmd.valid.out.hob |= IDE_VALID_OUT_HOB;
	}

	if (req_task->in_flags.b.data)
		cmd.ftf_flags |= IDE_FTFLAG_IN_DATA;

	if (req_task->req_cmd == IDE_DRIVE_TASK_RAW_WRITE) {
		/* fixup data phase if needed */
		if (req_task->data_phase == TASKFILE_IN_DMAQ ||
		    req_task->data_phase == TASKFILE_IN_DMA)
			cmd.tf_flags |= IDE_TFLAG_WRITE;
	}

	cmd.protocol = ATA_PROT_DMA;

	switch (req_task->data_phase) {
	case TASKFILE_MULTI_OUT:
		if (!drive->mult_count) {
			/* (hs): give up if multcount is not set */
			pr_err("%s: %s Multimode Write multcount is not set\n",
				drive->name, __func__);
			err = -EPERM;
			goto abort;
		}
		cmd.tf_flags |= IDE_TFLAG_MULTI_PIO;
		/* fall through */
	case TASKFILE_OUT:
		cmd.protocol = ATA_PROT_PIO;
		/* fall through */
	case TASKFILE_OUT_DMAQ:
	case TASKFILE_OUT_DMA:
		cmd.tf_flags |= IDE_TFLAG_WRITE;
		nsect = taskout / SECTOR_SIZE;
		data_buf = outbuf;
		break;
	case TASKFILE_MULTI_IN:
		if (!drive->mult_count) {
			/* (hs): give up if multcount is not set */
			pr_err("%s: %s Multimode Read multcount is not set\n",
				drive->name, __func__);
			err = -EPERM;
			goto abort;
		}
		cmd.tf_flags |= IDE_TFLAG_MULTI_PIO;
		/* fall through */
	case TASKFILE_IN:
		cmd.protocol = ATA_PROT_PIO;
		/* fall through */
	case TASKFILE_IN_DMAQ:
	case TASKFILE_IN_DMA:
		nsect = taskin / SECTOR_SIZE;
		data_buf = inbuf;
		break;
	case TASKFILE_NO_DATA:
		cmd.protocol = ATA_PROT_NODATA;
		break;
	default:
		err = -EFAULT;
		goto abort;
	}

	if (req_task->req_cmd == IDE_DRIVE_TASK_NO_DATA)
		nsect = 0;
	else if (!nsect) {
		nsect = (cmd.hob.nsect << 8) | cmd.tf.nsect;

		if (!nsect) {
			pr_err("%s: in/out command without data\n",
					drive->name);
			err = -EFAULT;
			goto abort;
		}
	}

	err = ide_raw_taskfile(drive, &cmd, data_buf, nsect);

	memcpy(req_task->hob_ports, &cmd.hob, HDIO_DRIVE_HOB_HDR_SIZE - 2);
	memcpy(req_task->io_ports,  &cmd.tf,  HDIO_DRIVE_TASK_HDR_SIZE);

	if ((cmd.ftf_flags & IDE_FTFLAG_SET_IN_FLAGS) &&
	    req_task->in_flags.all == 0) {
		req_task->in_flags.all = IDE_TASKFILE_STD_IN_FLAGS;
		if (drive->dev_flags & IDE_DFLAG_LBA48)
			req_task->in_flags.all |= (IDE_HOB_STD_IN_FLAGS << 8);
	}

	if (copy_to_user(buf, req_task, tasksize)) {
		err = -EFAULT;
		goto abort;
	}
	if (taskout) {
		int outtotal = tasksize;
		if (copy_to_user(buf + outtotal, outbuf, taskout)) {
			err = -EFAULT;
			goto abort;
		}
	}
	if (taskin) {
		int intotal = tasksize + taskout;
		if (copy_to_user(buf + intotal, inbuf, taskin)) {
			err = -EFAULT;
			goto abort;
		}
	}
abort:
	kfree(req_task);
	kfree(outbuf);
	kfree(inbuf);

	return err;
}
#endif
