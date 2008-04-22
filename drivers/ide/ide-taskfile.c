/*
 *  Copyright (C) 2000-2002	   Michael Cornwell <cornwell@acm.org>
 *  Copyright (C) 2000-2002	   Andre Hedrick <andre@linux-ide.org>
 *  Copyright (C) 2001-2002	   Klaus Smolin
 *					IBM Storage Technology Division
 *  Copyright (C) 2003-2004, 2007  Bartlomiej Zolnierkiewicz
 *
 *  The big the bad and the ugly.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/major.h>
#include <linux/errno.h>
#include <linux/genhd.h>
#include <linux/blkpg.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/hdreg.h>
#include <linux/ide.h>
#include <linux/bitops.h>
#include <linux/scatterlist.h>

#include <asm/byteorder.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/io.h>

void ide_tf_load(ide_drive_t *drive, ide_task_t *task)
{
	ide_hwif_t *hwif = drive->hwif;
	struct ide_taskfile *tf = &task->tf;
	u8 HIHI = (task->tf_flags & IDE_TFLAG_LBA48) ? 0xE0 : 0xEF;

	if (task->tf_flags & IDE_TFLAG_FLAGGED)
		HIHI = 0xFF;

#ifdef DEBUG
	printk("%s: tf: feat 0x%02x nsect 0x%02x lbal 0x%02x "
		"lbam 0x%02x lbah 0x%02x dev 0x%02x cmd 0x%02x\n",
		drive->name, tf->feature, tf->nsect, tf->lbal,
		tf->lbam, tf->lbah, tf->device, tf->command);
	printk("%s: hob: nsect 0x%02x lbal 0x%02x "
		"lbam 0x%02x lbah 0x%02x\n",
		drive->name, tf->hob_nsect, tf->hob_lbal,
		tf->hob_lbam, tf->hob_lbah);
#endif

	ide_set_irq(drive, 1);

	if ((task->tf_flags & IDE_TFLAG_NO_SELECT_MASK) == 0)
		SELECT_MASK(drive, 0);

	if (task->tf_flags & IDE_TFLAG_OUT_DATA)
		hwif->OUTW((tf->hob_data << 8) | tf->data,
			   hwif->io_ports[IDE_DATA_OFFSET]);

	if (task->tf_flags & IDE_TFLAG_OUT_HOB_FEATURE)
		hwif->OUTB(tf->hob_feature, hwif->io_ports[IDE_FEATURE_OFFSET]);
	if (task->tf_flags & IDE_TFLAG_OUT_HOB_NSECT)
		hwif->OUTB(tf->hob_nsect, hwif->io_ports[IDE_NSECTOR_OFFSET]);
	if (task->tf_flags & IDE_TFLAG_OUT_HOB_LBAL)
		hwif->OUTB(tf->hob_lbal, hwif->io_ports[IDE_SECTOR_OFFSET]);
	if (task->tf_flags & IDE_TFLAG_OUT_HOB_LBAM)
		hwif->OUTB(tf->hob_lbam, hwif->io_ports[IDE_LCYL_OFFSET]);
	if (task->tf_flags & IDE_TFLAG_OUT_HOB_LBAH)
		hwif->OUTB(tf->hob_lbah, hwif->io_ports[IDE_HCYL_OFFSET]);

	if (task->tf_flags & IDE_TFLAG_OUT_FEATURE)
		hwif->OUTB(tf->feature, hwif->io_ports[IDE_FEATURE_OFFSET]);
	if (task->tf_flags & IDE_TFLAG_OUT_NSECT)
		hwif->OUTB(tf->nsect, hwif->io_ports[IDE_NSECTOR_OFFSET]);
	if (task->tf_flags & IDE_TFLAG_OUT_LBAL)
		hwif->OUTB(tf->lbal, hwif->io_ports[IDE_SECTOR_OFFSET]);
	if (task->tf_flags & IDE_TFLAG_OUT_LBAM)
		hwif->OUTB(tf->lbam, hwif->io_ports[IDE_LCYL_OFFSET]);
	if (task->tf_flags & IDE_TFLAG_OUT_LBAH)
		hwif->OUTB(tf->lbah, hwif->io_ports[IDE_HCYL_OFFSET]);

	if (task->tf_flags & IDE_TFLAG_OUT_DEVICE)
		hwif->OUTB((tf->device & HIHI) | drive->select.all,
			   hwif->io_ports[IDE_SELECT_OFFSET]);
}

int taskfile_lib_get_identify (ide_drive_t *drive, u8 *buf)
{
	ide_task_t args;

	memset(&args, 0, sizeof(ide_task_t));
	args.tf.nsect = 0x01;
	if (drive->media == ide_disk)
		args.tf.command = WIN_IDENTIFY;
	else
		args.tf.command = WIN_PIDENTIFY;
	args.tf_flags	= IDE_TFLAG_TF | IDE_TFLAG_DEVICE;
	args.data_phase	= TASKFILE_IN;
	return ide_raw_taskfile(drive, &args, buf, 1);
}

static int inline task_dma_ok(ide_task_t *task)
{
	if (blk_fs_request(task->rq) || (task->tf_flags & IDE_TFLAG_FLAGGED))
		return 1;

	switch (task->tf.command) {
		case WIN_WRITEDMA_ONCE:
		case WIN_WRITEDMA:
		case WIN_WRITEDMA_EXT:
		case WIN_READDMA_ONCE:
		case WIN_READDMA:
		case WIN_READDMA_EXT:
		case WIN_IDENTIFY_DMA:
			return 1;
	}

	return 0;
}

static ide_startstop_t task_no_data_intr(ide_drive_t *);
static ide_startstop_t set_geometry_intr(ide_drive_t *);
static ide_startstop_t recal_intr(ide_drive_t *);
static ide_startstop_t set_multmode_intr(ide_drive_t *);
static ide_startstop_t pre_task_out_intr(ide_drive_t *, struct request *);
static ide_startstop_t task_in_intr(ide_drive_t *);

ide_startstop_t do_rw_taskfile (ide_drive_t *drive, ide_task_t *task)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct ide_taskfile *tf = &task->tf;
	ide_handler_t *handler = NULL;

	if (task->data_phase == TASKFILE_MULTI_IN ||
	    task->data_phase == TASKFILE_MULTI_OUT) {
		if (!drive->mult_count) {
			printk(KERN_ERR "%s: multimode not set!\n",
					drive->name);
			return ide_stopped;
		}
	}

	if (task->tf_flags & IDE_TFLAG_FLAGGED)
		task->tf_flags |= IDE_TFLAG_FLAGGED_SET_IN_FLAGS;

	if ((task->tf_flags & IDE_TFLAG_DMA_PIO_FALLBACK) == 0)
		ide_tf_load(drive, task);

	switch (task->data_phase) {
	case TASKFILE_MULTI_OUT:
	case TASKFILE_OUT:
		hwif->OUTBSYNC(drive, tf->command,
			       hwif->io_ports[IDE_COMMAND_OFFSET]);
		ndelay(400);	/* FIXME */
		return pre_task_out_intr(drive, task->rq);
	case TASKFILE_MULTI_IN:
	case TASKFILE_IN:
		handler = task_in_intr;
		/* fall-through */
	case TASKFILE_NO_DATA:
		if (handler == NULL)
			handler = task_no_data_intr;
		/* WIN_{SPECIFY,RESTORE,SETMULT} use custom handlers */
		if (task->tf_flags & IDE_TFLAG_CUSTOM_HANDLER) {
			switch (tf->command) {
			case WIN_SPECIFY: handler = set_geometry_intr;	break;
			case WIN_RESTORE: handler = recal_intr;		break;
			case WIN_SETMULT: handler = set_multmode_intr;	break;
			}
		}
		ide_execute_command(drive, tf->command, handler,
				    WAIT_WORSTCASE, NULL);
		return ide_started;
	default:
		if (task_dma_ok(task) == 0 || drive->using_dma == 0 ||
		    hwif->dma_setup(drive))
			return ide_stopped;
		hwif->dma_exec_cmd(drive, tf->command);
		hwif->dma_start(drive);
		return ide_started;
	}
}
EXPORT_SYMBOL_GPL(do_rw_taskfile);

/*
 * set_multmode_intr() is invoked on completion of a WIN_SETMULT cmd.
 */
static ide_startstop_t set_multmode_intr(ide_drive_t *drive)
{
	u8 stat = ide_read_status(drive);

	if (OK_STAT(stat, READY_STAT, BAD_STAT))
		drive->mult_count = drive->mult_req;
	else {
		drive->mult_req = drive->mult_count = 0;
		drive->special.b.recalibrate = 1;
		(void) ide_dump_status(drive, "set_multmode", stat);
	}
	return ide_stopped;
}

/*
 * set_geometry_intr() is invoked on completion of a WIN_SPECIFY cmd.
 */
static ide_startstop_t set_geometry_intr(ide_drive_t *drive)
{
	int retries = 5;
	u8 stat;

	while (((stat = ide_read_status(drive)) & BUSY_STAT) && retries--)
		udelay(10);

	if (OK_STAT(stat, READY_STAT, BAD_STAT))
		return ide_stopped;

	if (stat & (ERR_STAT|DRQ_STAT))
		return ide_error(drive, "set_geometry_intr", stat);

	BUG_ON(HWGROUP(drive)->handler != NULL);
	ide_set_handler(drive, &set_geometry_intr, WAIT_WORSTCASE, NULL);
	return ide_started;
}

/*
 * recal_intr() is invoked on completion of a WIN_RESTORE (recalibrate) cmd.
 */
static ide_startstop_t recal_intr(ide_drive_t *drive)
{
	u8 stat = ide_read_status(drive);

	if (!OK_STAT(stat, READY_STAT, BAD_STAT))
		return ide_error(drive, "recal_intr", stat);
	return ide_stopped;
}

/*
 * Handler for commands without a data phase
 */
static ide_startstop_t task_no_data_intr(ide_drive_t *drive)
{
	ide_task_t *args	= HWGROUP(drive)->rq->special;
	u8 stat;

	local_irq_enable_in_hardirq();
	stat = ide_read_status(drive);

	if (!OK_STAT(stat, READY_STAT, BAD_STAT))
		return ide_error(drive, "task_no_data_intr", stat);
		/* calls ide_end_drive_cmd */

	if (args)
		ide_end_drive_cmd(drive, stat, ide_read_error(drive));

	return ide_stopped;
}

static u8 wait_drive_not_busy(ide_drive_t *drive)
{
	int retries;
	u8 stat;

	/*
	 * Last sector was transfered, wait until drive is ready.
	 * This can take up to 10 usec, but we will wait max 1 ms.
	 */
	for (retries = 0; retries < 100; retries++) {
		stat = ide_read_status(drive);

		if (stat & BUSY_STAT)
			udelay(10);
		else
			break;
	}

	if (stat & BUSY_STAT)
		printk(KERN_ERR "%s: drive still BUSY!\n", drive->name);

	return stat;
}

static void ide_pio_sector(ide_drive_t *drive, unsigned int write)
{
	ide_hwif_t *hwif = drive->hwif;
	struct scatterlist *sg = hwif->sg_table;
	struct scatterlist *cursg = hwif->cursg;
	struct page *page;
#ifdef CONFIG_HIGHMEM
	unsigned long flags;
#endif
	unsigned int offset;
	u8 *buf;

	cursg = hwif->cursg;
	if (!cursg) {
		cursg = sg;
		hwif->cursg = sg;
	}

	page = sg_page(cursg);
	offset = cursg->offset + hwif->cursg_ofs * SECTOR_SIZE;

	/* get the current page and offset */
	page = nth_page(page, (offset >> PAGE_SHIFT));
	offset %= PAGE_SIZE;

#ifdef CONFIG_HIGHMEM
	local_irq_save(flags);
#endif
	buf = kmap_atomic(page, KM_BIO_SRC_IRQ) + offset;

	hwif->nleft--;
	hwif->cursg_ofs++;

	if ((hwif->cursg_ofs * SECTOR_SIZE) == cursg->length) {
		hwif->cursg = sg_next(hwif->cursg);
		hwif->cursg_ofs = 0;
	}

	/* do the actual data transfer */
	if (write)
		hwif->ata_output_data(drive, buf, SECTOR_WORDS);
	else
		hwif->ata_input_data(drive, buf, SECTOR_WORDS);

	kunmap_atomic(buf, KM_BIO_SRC_IRQ);
#ifdef CONFIG_HIGHMEM
	local_irq_restore(flags);
#endif
}

static void ide_pio_multi(ide_drive_t *drive, unsigned int write)
{
	unsigned int nsect;

	nsect = min_t(unsigned int, drive->hwif->nleft, drive->mult_count);
	while (nsect--)
		ide_pio_sector(drive, write);
}

static void ide_pio_datablock(ide_drive_t *drive, struct request *rq,
				     unsigned int write)
{
	u8 saved_io_32bit = drive->io_32bit;

	if (rq->bio)	/* fs request */
		rq->errors = 0;

	if (rq->cmd_type == REQ_TYPE_ATA_TASKFILE) {
		ide_task_t *task = rq->special;

		if (task->tf_flags & IDE_TFLAG_IO_16BIT)
			drive->io_32bit = 0;
	}

	touch_softlockup_watchdog();

	switch (drive->hwif->data_phase) {
	case TASKFILE_MULTI_IN:
	case TASKFILE_MULTI_OUT:
		ide_pio_multi(drive, write);
		break;
	default:
		ide_pio_sector(drive, write);
		break;
	}

	drive->io_32bit = saved_io_32bit;
}

static ide_startstop_t task_error(ide_drive_t *drive, struct request *rq,
				  const char *s, u8 stat)
{
	if (rq->bio) {
		ide_hwif_t *hwif = drive->hwif;
		int sectors = hwif->nsect - hwif->nleft;

		switch (hwif->data_phase) {
		case TASKFILE_IN:
			if (hwif->nleft)
				break;
			/* fall through */
		case TASKFILE_OUT:
			sectors--;
			break;
		case TASKFILE_MULTI_IN:
			if (hwif->nleft)
				break;
			/* fall through */
		case TASKFILE_MULTI_OUT:
			sectors -= drive->mult_count;
		default:
			break;
		}

		if (sectors > 0) {
			ide_driver_t *drv;

			drv = *(ide_driver_t **)rq->rq_disk->private_data;
			drv->end_request(drive, 1, sectors);
		}
	}
	return ide_error(drive, s, stat);
}

void task_end_request(ide_drive_t *drive, struct request *rq, u8 stat)
{
	if (rq->cmd_type == REQ_TYPE_ATA_TASKFILE) {
		u8 err = ide_read_error(drive);

		ide_end_drive_cmd(drive, stat, err);
		return;
	}

	if (rq->rq_disk) {
		ide_driver_t *drv;

		drv = *(ide_driver_t **)rq->rq_disk->private_data;;
		drv->end_request(drive, 1, rq->nr_sectors);
	} else
		ide_end_request(drive, 1, rq->nr_sectors);
}

/*
 * We got an interrupt on a task_in case, but no errors and no DRQ.
 *
 * It might be a spurious irq (shared irq), but it might be a
 * command that had no output.
 */
static ide_startstop_t task_in_unexpected(ide_drive_t *drive, struct request *rq, u8 stat)
{
	/* Command all done? */
	if (OK_STAT(stat, READY_STAT, BUSY_STAT)) {
		task_end_request(drive, rq, stat);
		return ide_stopped;
	}

	/* Assume it was a spurious irq */
	ide_set_handler(drive, &task_in_intr, WAIT_WORSTCASE, NULL);
	return ide_started;
}

/*
 * Handler for command with PIO data-in phase (Read/Read Multiple).
 */
static ide_startstop_t task_in_intr(ide_drive_t *drive)
{
	ide_hwif_t *hwif = drive->hwif;
	struct request *rq = HWGROUP(drive)->rq;
	u8 stat = ide_read_status(drive);

	/* Error? */
	if (stat & ERR_STAT)
		return task_error(drive, rq, __FUNCTION__, stat);

	/* Didn't want any data? Odd. */
	if (!(stat & DRQ_STAT))
		return task_in_unexpected(drive, rq, stat);

	ide_pio_datablock(drive, rq, 0);

	/* Are we done? Check status and finish transfer. */
	if (!hwif->nleft) {
		stat = wait_drive_not_busy(drive);
		if (!OK_STAT(stat, 0, BAD_STAT))
			return task_error(drive, rq, __FUNCTION__, stat);
		task_end_request(drive, rq, stat);
		return ide_stopped;
	}

	/* Still data left to transfer. */
	ide_set_handler(drive, &task_in_intr, WAIT_WORSTCASE, NULL);

	return ide_started;
}

/*
 * Handler for command with PIO data-out phase (Write/Write Multiple).
 */
static ide_startstop_t task_out_intr (ide_drive_t *drive)
{
	ide_hwif_t *hwif = drive->hwif;
	struct request *rq = HWGROUP(drive)->rq;
	u8 stat = ide_read_status(drive);

	if (!OK_STAT(stat, DRIVE_READY, drive->bad_wstat))
		return task_error(drive, rq, __FUNCTION__, stat);

	/* Deal with unexpected ATA data phase. */
	if (((stat & DRQ_STAT) == 0) ^ !hwif->nleft)
		return task_error(drive, rq, __FUNCTION__, stat);

	if (!hwif->nleft) {
		task_end_request(drive, rq, stat);
		return ide_stopped;
	}

	/* Still data left to transfer. */
	ide_pio_datablock(drive, rq, 1);
	ide_set_handler(drive, &task_out_intr, WAIT_WORSTCASE, NULL);

	return ide_started;
}

static ide_startstop_t pre_task_out_intr(ide_drive_t *drive, struct request *rq)
{
	ide_startstop_t startstop;

	if (ide_wait_stat(&startstop, drive, DRQ_STAT,
			  drive->bad_wstat, WAIT_DRQ)) {
		printk(KERN_ERR "%s: no DRQ after issuing %sWRITE%s\n",
				drive->name,
				drive->hwif->data_phase ? "MULT" : "",
				drive->addressing ? "_EXT" : "");
		return startstop;
	}

	if (!drive->unmask)
		local_irq_disable();

	ide_set_handler(drive, &task_out_intr, WAIT_WORSTCASE, NULL);
	ide_pio_datablock(drive, rq, 1);

	return ide_started;
}

int ide_raw_taskfile(ide_drive_t *drive, ide_task_t *task, u8 *buf, u16 nsect)
{
	struct request rq;

	memset(&rq, 0, sizeof(rq));
	rq.ref_count = 1;
	rq.cmd_type = REQ_TYPE_ATA_TASKFILE;
	rq.buffer = buf;

	/*
	 * (ks) We transfer currently only whole sectors.
	 * This is suffient for now.  But, it would be great,
	 * if we would find a solution to transfer any size.
	 * To support special commands like READ LONG.
	 */
	rq.hard_nr_sectors = rq.nr_sectors = nsect;
	rq.hard_cur_sectors = rq.current_nr_sectors = nsect;

	if (task->tf_flags & IDE_TFLAG_WRITE)
		rq.cmd_flags |= REQ_RW;

	rq.special = task;
	task->rq = &rq;

	return ide_do_drive_cmd(drive, &rq, ide_wait);
}

EXPORT_SYMBOL(ide_raw_taskfile);

int ide_no_data_taskfile(ide_drive_t *drive, ide_task_t *task)
{
	task->data_phase = TASKFILE_NO_DATA;

	return ide_raw_taskfile(drive, task, NULL, 0);
}
EXPORT_SYMBOL_GPL(ide_no_data_taskfile);

#ifdef CONFIG_IDE_TASK_IOCTL
int ide_taskfile_ioctl (ide_drive_t *drive, unsigned int cmd, unsigned long arg)
{
	ide_task_request_t	*req_task;
	ide_task_t		args;
	u8 *outbuf		= NULL;
	u8 *inbuf		= NULL;
	u8 *data_buf		= NULL;
	int err			= 0;
	int tasksize		= sizeof(struct ide_task_request_s);
	unsigned int taskin	= 0;
	unsigned int taskout	= 0;
	u16 nsect		= 0;
	char __user *buf = (char __user *)arg;

//	printk("IDE Taskfile ...\n");

	req_task = kzalloc(tasksize, GFP_KERNEL);
	if (req_task == NULL) return -ENOMEM;
	if (copy_from_user(req_task, buf, tasksize)) {
		kfree(req_task);
		return -EFAULT;
	}

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

	memset(&args, 0, sizeof(ide_task_t));

	memcpy(&args.tf_array[0], req_task->hob_ports, HDIO_DRIVE_HOB_HDR_SIZE - 2);
	memcpy(&args.tf_array[6], req_task->io_ports, HDIO_DRIVE_TASK_HDR_SIZE);

	args.data_phase = req_task->data_phase;

	args.tf_flags = IDE_TFLAG_IO_16BIT | IDE_TFLAG_DEVICE |
			IDE_TFLAG_IN_TF;
	if (drive->addressing == 1)
		args.tf_flags |= (IDE_TFLAG_LBA48 | IDE_TFLAG_IN_HOB);

	if (req_task->out_flags.all) {
		args.tf_flags |= IDE_TFLAG_FLAGGED;

		if (req_task->out_flags.b.data)
			args.tf_flags |= IDE_TFLAG_OUT_DATA;

		if (req_task->out_flags.b.nsector_hob)
			args.tf_flags |= IDE_TFLAG_OUT_HOB_NSECT;
		if (req_task->out_flags.b.sector_hob)
			args.tf_flags |= IDE_TFLAG_OUT_HOB_LBAL;
		if (req_task->out_flags.b.lcyl_hob)
			args.tf_flags |= IDE_TFLAG_OUT_HOB_LBAM;
		if (req_task->out_flags.b.hcyl_hob)
			args.tf_flags |= IDE_TFLAG_OUT_HOB_LBAH;

		if (req_task->out_flags.b.error_feature)
			args.tf_flags |= IDE_TFLAG_OUT_FEATURE;
		if (req_task->out_flags.b.nsector)
			args.tf_flags |= IDE_TFLAG_OUT_NSECT;
		if (req_task->out_flags.b.sector)
			args.tf_flags |= IDE_TFLAG_OUT_LBAL;
		if (req_task->out_flags.b.lcyl)
			args.tf_flags |= IDE_TFLAG_OUT_LBAM;
		if (req_task->out_flags.b.hcyl)
			args.tf_flags |= IDE_TFLAG_OUT_LBAH;
	} else {
		args.tf_flags |= IDE_TFLAG_OUT_TF;
		if (args.tf_flags & IDE_TFLAG_LBA48)
			args.tf_flags |= IDE_TFLAG_OUT_HOB;
	}

	if (req_task->in_flags.b.data)
		args.tf_flags |= IDE_TFLAG_IN_DATA;

	switch(req_task->data_phase) {
		case TASKFILE_MULTI_OUT:
			if (!drive->mult_count) {
				/* (hs): give up if multcount is not set */
				printk(KERN_ERR "%s: %s Multimode Write " \
					"multcount is not set\n",
					drive->name, __FUNCTION__);
				err = -EPERM;
				goto abort;
			}
			/* fall through */
		case TASKFILE_OUT:
			/* fall through */
		case TASKFILE_OUT_DMAQ:
		case TASKFILE_OUT_DMA:
			nsect = taskout / SECTOR_SIZE;
			data_buf = outbuf;
			break;
		case TASKFILE_MULTI_IN:
			if (!drive->mult_count) {
				/* (hs): give up if multcount is not set */
				printk(KERN_ERR "%s: %s Multimode Read failure " \
					"multcount is not set\n",
					drive->name, __FUNCTION__);
				err = -EPERM;
				goto abort;
			}
			/* fall through */
		case TASKFILE_IN:
			/* fall through */
		case TASKFILE_IN_DMAQ:
		case TASKFILE_IN_DMA:
			nsect = taskin / SECTOR_SIZE;
			data_buf = inbuf;
			break;
		case TASKFILE_NO_DATA:
			break;
		default:
			err = -EFAULT;
			goto abort;
	}

	if (req_task->req_cmd == IDE_DRIVE_TASK_NO_DATA)
		nsect = 0;
	else if (!nsect) {
		nsect = (args.tf.hob_nsect << 8) | args.tf.nsect;

		if (!nsect) {
			printk(KERN_ERR "%s: in/out command without data\n",
					drive->name);
			err = -EFAULT;
			goto abort;
		}
	}

	if (req_task->req_cmd == IDE_DRIVE_TASK_RAW_WRITE)
		args.tf_flags |= IDE_TFLAG_WRITE;

	err = ide_raw_taskfile(drive, &args, data_buf, nsect);

	memcpy(req_task->hob_ports, &args.tf_array[0], HDIO_DRIVE_HOB_HDR_SIZE - 2);
	memcpy(req_task->io_ports, &args.tf_array[6], HDIO_DRIVE_TASK_HDR_SIZE);

	if ((args.tf_flags & IDE_TFLAG_FLAGGED_SET_IN_FLAGS) &&
	    req_task->in_flags.all == 0) {
		req_task->in_flags.all = IDE_TASKFILE_STD_IN_FLAGS;
		if (drive->addressing == 1)
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

//	printk("IDE Taskfile ioctl ended. rc = %i\n", err);

	return err;
}
#endif

int ide_cmd_ioctl (ide_drive_t *drive, unsigned int cmd, unsigned long arg)
{
	u8 *buf = NULL;
	int bufsize = 0, err = 0;
	u8 args[4], xfer_rate = 0;
	ide_task_t tfargs;
	struct ide_taskfile *tf = &tfargs.tf;
	struct hd_driveid *id = drive->id;

	if (NULL == (void *) arg) {
		struct request rq;

		ide_init_drive_cmd(&rq);
		rq.cmd_type = REQ_TYPE_ATA_TASKFILE;

		return ide_do_drive_cmd(drive, &rq, ide_wait);
	}

	if (copy_from_user(args, (void __user *)arg, 4))
		return -EFAULT;

	memset(&tfargs, 0, sizeof(ide_task_t));
	tf->feature = args[2];
	if (args[0] == WIN_SMART) {
		tf->nsect = args[3];
		tf->lbal  = args[1];
		tf->lbam  = 0x4f;
		tf->lbah  = 0xc2;
		tfargs.tf_flags = IDE_TFLAG_OUT_TF | IDE_TFLAG_IN_NSECT;
	} else {
		tf->nsect = args[1];
		tfargs.tf_flags = IDE_TFLAG_OUT_FEATURE |
				  IDE_TFLAG_OUT_NSECT | IDE_TFLAG_IN_NSECT;
	}
	tf->command = args[0];
	tfargs.data_phase = args[3] ? TASKFILE_IN : TASKFILE_NO_DATA;

	if (args[3]) {
		tfargs.tf_flags |= IDE_TFLAG_IO_16BIT;
		bufsize = SECTOR_WORDS * 4 * args[3];
		buf = kzalloc(bufsize, GFP_KERNEL);
		if (buf == NULL)
			return -ENOMEM;
	}

	if (tf->command == WIN_SETFEATURES &&
	    tf->feature == SETFEATURES_XFER &&
	    tf->nsect >= XFER_SW_DMA_0 &&
	    (id->dma_ultra || id->dma_mword || id->dma_1word)) {
		xfer_rate = args[1];
		if (tf->nsect > XFER_UDMA_2 && !eighty_ninty_three(drive)) {
			printk(KERN_WARNING "%s: UDMA speeds >UDMA33 cannot "
					    "be set\n", drive->name);
			goto abort;
		}
	}

	err = ide_raw_taskfile(drive, &tfargs, buf, args[3]);

	args[0] = tf->status;
	args[1] = tf->error;
	args[2] = tf->nsect;

	if (!err && xfer_rate) {
		/* active-retuning-calls future */
		ide_set_xfer_rate(drive, xfer_rate);
		ide_driveid_update(drive);
	}
abort:
	if (copy_to_user((void __user *)arg, &args, 4))
		err = -EFAULT;
	if (buf) {
		if (copy_to_user((void __user *)(arg + 4), buf, bufsize))
			err = -EFAULT;
		kfree(buf);
	}
	return err;
}

int ide_task_ioctl (ide_drive_t *drive, unsigned int cmd, unsigned long arg)
{
	void __user *p = (void __user *)arg;
	int err = 0;
	u8 args[7];
	ide_task_t task;

	if (copy_from_user(args, p, 7))
		return -EFAULT;

	memset(&task, 0, sizeof(task));
	memcpy(&task.tf_array[7], &args[1], 6);
	task.tf.command = args[0];
	task.tf_flags = IDE_TFLAG_TF | IDE_TFLAG_DEVICE;

	err = ide_no_data_taskfile(drive, &task);

	args[0] = task.tf.command;
	memcpy(&args[1], &task.tf_array[7], 6);

	if (copy_to_user(p, args, 7))
		err = -EFAULT;

	return err;
}
