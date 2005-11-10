/*
 * linux/drivers/ide/ide-taskfile.c	Version 0.38	March 05, 2003
 *
 *  Copyright (C) 2000-2002	Michael Cornwell <cornwell@acm.org>
 *  Copyright (C) 2000-2002	Andre Hedrick <andre@linux-ide.org>
 *  Copyright (C) 2001-2002	Klaus Smolin
 *					IBM Storage Technology Division
 *  Copyright (C) 2003-2004	Bartlomiej Zolnierkiewicz
 *
 *  The big the bad and the ugly.
 *
 *  Problems to be fixed because of BH interface or the lack therefore.
 *
 *  Fill me in stupid !!!
 *
 *  HOST:
 *	General refers to the Controller and Driver "pair".
 *  DATA HANDLER:
 *	Under the context of Linux it generally refers to an interrupt handler.
 *	However, it correctly describes the 'HOST'
 *  DATA BLOCK:
 *	The amount of data needed to be transfered as predefined in the
 *	setup of the device.
 *  STORAGE ATOMIC:
 *	The 'DATA BLOCK' associated to the 'DATA HANDLER', and can be as
 *	small as a single sector or as large as the entire command block
 *	request.
 */

#include <linux/config.h>
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
#include <linux/hdreg.h>
#include <linux/ide.h>
#include <linux/bitops.h>

#include <asm/byteorder.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#define DEBUG_TASKFILE	0	/* unset when fixed */

static void ata_bswap_data (void *buffer, int wcount)
{
	u16 *p = buffer;

	while (wcount--) {
		*p = *p << 8 | *p >> 8; p++;
		*p = *p << 8 | *p >> 8; p++;
	}
}

static void taskfile_input_data(ide_drive_t *drive, void *buffer, u32 wcount)
{
	HWIF(drive)->ata_input_data(drive, buffer, wcount);
	if (drive->bswap)
		ata_bswap_data(buffer, wcount);
}

static void taskfile_output_data(ide_drive_t *drive, void *buffer, u32 wcount)
{
	if (drive->bswap) {
		ata_bswap_data(buffer, wcount);
		HWIF(drive)->ata_output_data(drive, buffer, wcount);
		ata_bswap_data(buffer, wcount);
	} else {
		HWIF(drive)->ata_output_data(drive, buffer, wcount);
	}
}

int taskfile_lib_get_identify (ide_drive_t *drive, u8 *buf)
{
	ide_task_t args;
	memset(&args, 0, sizeof(ide_task_t));
	args.tfRegister[IDE_NSECTOR_OFFSET]	= 0x01;
	if (drive->media == ide_disk)
		args.tfRegister[IDE_COMMAND_OFFSET]	= WIN_IDENTIFY;
	else
		args.tfRegister[IDE_COMMAND_OFFSET]	= WIN_PIDENTIFY;
	args.command_type = IDE_DRIVE_TASK_IN;
	args.data_phase   = TASKFILE_IN;
	args.handler	  = &task_in_intr;
	return ide_raw_taskfile(drive, &args, buf);
}

ide_startstop_t do_rw_taskfile (ide_drive_t *drive, ide_task_t *task)
{
	ide_hwif_t *hwif	= HWIF(drive);
	task_struct_t *taskfile	= (task_struct_t *) task->tfRegister;
	hob_struct_t *hobfile	= (hob_struct_t *) task->hobRegister;
	u8 HIHI			= (drive->addressing == 1) ? 0xE0 : 0xEF;

	/* ALL Command Block Executions SHALL clear nIEN, unless otherwise */
	if (IDE_CONTROL_REG) {
		/* clear nIEN */
		hwif->OUTB(drive->ctl, IDE_CONTROL_REG);
	}
	SELECT_MASK(drive, 0);

	if (drive->addressing == 1) {
		hwif->OUTB(hobfile->feature, IDE_FEATURE_REG);
		hwif->OUTB(hobfile->sector_count, IDE_NSECTOR_REG);
		hwif->OUTB(hobfile->sector_number, IDE_SECTOR_REG);
		hwif->OUTB(hobfile->low_cylinder, IDE_LCYL_REG);
		hwif->OUTB(hobfile->high_cylinder, IDE_HCYL_REG);
	}

	hwif->OUTB(taskfile->feature, IDE_FEATURE_REG);
	hwif->OUTB(taskfile->sector_count, IDE_NSECTOR_REG);
	hwif->OUTB(taskfile->sector_number, IDE_SECTOR_REG);
	hwif->OUTB(taskfile->low_cylinder, IDE_LCYL_REG);
	hwif->OUTB(taskfile->high_cylinder, IDE_HCYL_REG);

	hwif->OUTB((taskfile->device_head & HIHI) | drive->select.all, IDE_SELECT_REG);

	if (task->handler != NULL) {
		if (task->prehandler != NULL) {
			hwif->OUTBSYNC(drive, taskfile->command, IDE_COMMAND_REG);
			ndelay(400);	/* FIXME */
			return task->prehandler(drive, task->rq);
		}
		ide_execute_command(drive, taskfile->command, task->handler, WAIT_WORSTCASE, NULL);
		return ide_started;
	}

	if (!drive->using_dma)
		return ide_stopped;

	switch (taskfile->command) {
		case WIN_WRITEDMA_ONCE:
		case WIN_WRITEDMA:
		case WIN_WRITEDMA_EXT:
		case WIN_READDMA_ONCE:
		case WIN_READDMA:
		case WIN_READDMA_EXT:
		case WIN_IDENTIFY_DMA:
			if (!hwif->dma_setup(drive)) {
				hwif->dma_exec_cmd(drive, taskfile->command);
				hwif->dma_start(drive);
				return ide_started;
			}
			break;
		default:
			if (task->handler == NULL)
				return ide_stopped;
	}

	return ide_stopped;
}

/*
 * set_multmode_intr() is invoked on completion of a WIN_SETMULT cmd.
 */
ide_startstop_t set_multmode_intr (ide_drive_t *drive)
{
	ide_hwif_t *hwif = HWIF(drive);
	u8 stat;

	if (OK_STAT(stat = hwif->INB(IDE_STATUS_REG),READY_STAT,BAD_STAT)) {
		drive->mult_count = drive->mult_req;
	} else {
		drive->mult_req = drive->mult_count = 0;
		drive->special.b.recalibrate = 1;
		(void) ide_dump_status(drive, "set_multmode", stat);
	}
	return ide_stopped;
}

/*
 * set_geometry_intr() is invoked on completion of a WIN_SPECIFY cmd.
 */
ide_startstop_t set_geometry_intr (ide_drive_t *drive)
{
	ide_hwif_t *hwif = HWIF(drive);
	int retries = 5;
	u8 stat;

	while (((stat = hwif->INB(IDE_STATUS_REG)) & BUSY_STAT) && retries--)
		udelay(10);

	if (OK_STAT(stat, READY_STAT, BAD_STAT))
		return ide_stopped;

	if (stat & (ERR_STAT|DRQ_STAT))
		return ide_error(drive, "set_geometry_intr", stat);

	if (HWGROUP(drive)->handler != NULL)
		BUG();
	ide_set_handler(drive, &set_geometry_intr, WAIT_WORSTCASE, NULL);
	return ide_started;
}

/*
 * recal_intr() is invoked on completion of a WIN_RESTORE (recalibrate) cmd.
 */
ide_startstop_t recal_intr (ide_drive_t *drive)
{
	ide_hwif_t *hwif = HWIF(drive);
	u8 stat;

	if (!OK_STAT(stat = hwif->INB(IDE_STATUS_REG), READY_STAT, BAD_STAT))
		return ide_error(drive, "recal_intr", stat);
	return ide_stopped;
}

/*
 * Handler for commands without a data phase
 */
ide_startstop_t task_no_data_intr (ide_drive_t *drive)
{
	ide_task_t *args	= HWGROUP(drive)->rq->special;
	ide_hwif_t *hwif	= HWIF(drive);
	u8 stat;

	local_irq_enable();
	if (!OK_STAT(stat = hwif->INB(IDE_STATUS_REG),READY_STAT,BAD_STAT)) {
		return ide_error(drive, "task_no_data_intr", stat);
		/* calls ide_end_drive_cmd */
	}
	if (args)
		ide_end_drive_cmd(drive, stat, hwif->INB(IDE_ERROR_REG));

	return ide_stopped;
}

EXPORT_SYMBOL(task_no_data_intr);

static u8 wait_drive_not_busy(ide_drive_t *drive)
{
	ide_hwif_t *hwif = HWIF(drive);
	int retries = 100;
	u8 stat;

	/*
	 * Last sector was transfered, wait until drive is ready.
	 * This can take up to 10 usec, but we will wait max 1 ms
	 * (drive_cmd_intr() waits that long).
	 */
	while (((stat = hwif->INB(IDE_STATUS_REG)) & BUSY_STAT) && retries--)
		udelay(10);

	if (!retries)
		printk(KERN_ERR "%s: drive still BUSY!\n", drive->name);

	return stat;
}

static void ide_pio_sector(ide_drive_t *drive, unsigned int write)
{
	ide_hwif_t *hwif = drive->hwif;
	struct scatterlist *sg = hwif->sg_table;
	struct page *page;
#ifdef CONFIG_HIGHMEM
	unsigned long flags;
#endif
	unsigned int offset;
	u8 *buf;

	page = sg[hwif->cursg].page;
	offset = sg[hwif->cursg].offset + hwif->cursg_ofs * SECTOR_SIZE;

	/* get the current page and offset */
	page = nth_page(page, (offset >> PAGE_SHIFT));
	offset %= PAGE_SIZE;

#ifdef CONFIG_HIGHMEM
	local_irq_save(flags);
#endif
	buf = kmap_atomic(page, KM_BIO_SRC_IRQ) + offset;

	hwif->nleft--;
	hwif->cursg_ofs++;

	if ((hwif->cursg_ofs * SECTOR_SIZE) == sg[hwif->cursg].length) {
		hwif->cursg++;
		hwif->cursg_ofs = 0;
	}

	/* do the actual data transfer */
	if (write)
		taskfile_output_data(drive, buf, SECTOR_WORDS);
	else
		taskfile_input_data(drive, buf, SECTOR_WORDS);

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

static inline void ide_pio_datablock(ide_drive_t *drive, struct request *rq,
				     unsigned int write)
{
	if (rq->bio)	/* fs request */
		rq->errors = 0;

	switch (drive->hwif->data_phase) {
	case TASKFILE_MULTI_IN:
	case TASKFILE_MULTI_OUT:
		ide_pio_multi(drive, write);
		break;
	default:
		ide_pio_sector(drive, write);
		break;
	}
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

static void task_end_request(ide_drive_t *drive, struct request *rq, u8 stat)
{
	if (rq->flags & REQ_DRIVE_TASKFILE) {
		ide_task_t *task = rq->special;

		if (task->tf_out_flags.all) {
			u8 err = drive->hwif->INB(IDE_ERROR_REG);
			ide_end_drive_cmd(drive, stat, err);
			return;
		}
	}

	ide_end_request(drive, 1, rq->hard_nr_sectors);
}

/*
 * Handler for command with PIO data-in phase (Read/Read Multiple).
 */
ide_startstop_t task_in_intr (ide_drive_t *drive)
{
	ide_hwif_t *hwif = drive->hwif;
	struct request *rq = HWGROUP(drive)->rq;
	u8 stat = hwif->INB(IDE_STATUS_REG);

	/* new way for dealing with premature shared PCI interrupts */
	if (!OK_STAT(stat, DATA_READY, BAD_R_STAT)) {
		if (stat & (ERR_STAT | DRQ_STAT))
			return task_error(drive, rq, __FUNCTION__, stat);
		/* No data yet, so wait for another IRQ. */
		ide_set_handler(drive, &task_in_intr, WAIT_WORSTCASE, NULL);
		return ide_started;
	}

	ide_pio_datablock(drive, rq, 0);

	/* If it was the last datablock check status and finish transfer. */
	if (!hwif->nleft) {
		stat = wait_drive_not_busy(drive);
		if (!OK_STAT(stat, 0, BAD_R_STAT))
			return task_error(drive, rq, __FUNCTION__, stat);
		task_end_request(drive, rq, stat);
		return ide_stopped;
	}

	/* Still data left to transfer. */
	ide_set_handler(drive, &task_in_intr, WAIT_WORSTCASE, NULL);

	return ide_started;
}
EXPORT_SYMBOL(task_in_intr);

/*
 * Handler for command with PIO data-out phase (Write/Write Multiple).
 */
static ide_startstop_t task_out_intr (ide_drive_t *drive)
{
	ide_hwif_t *hwif = drive->hwif;
	struct request *rq = HWGROUP(drive)->rq;
	u8 stat = hwif->INB(IDE_STATUS_REG);

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

ide_startstop_t pre_task_out_intr (ide_drive_t *drive, struct request *rq)
{
	ide_startstop_t startstop;

	if (ide_wait_stat(&startstop, drive, DATA_READY,
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
EXPORT_SYMBOL(pre_task_out_intr);

static int ide_diag_taskfile(ide_drive_t *drive, ide_task_t *args, unsigned long data_size, u8 *buf)
{
	struct request rq;

	memset(&rq, 0, sizeof(rq));
	rq.flags = REQ_DRIVE_TASKFILE;
	rq.buffer = buf;

	/*
	 * (ks) We transfer currently only whole sectors.
	 * This is suffient for now.  But, it would be great,
	 * if we would find a solution to transfer any size.
	 * To support special commands like READ LONG.
	 */
	if (args->command_type != IDE_DRIVE_TASK_NO_DATA) {
		if (data_size == 0)
			rq.nr_sectors = (args->hobRegister[IDE_NSECTOR_OFFSET] << 8) | args->tfRegister[IDE_NSECTOR_OFFSET];
		else
			rq.nr_sectors = data_size / SECTOR_SIZE;

		if (!rq.nr_sectors) {
			printk(KERN_ERR "%s: in/out command without data\n",
					drive->name);
			return -EFAULT;
		}

		rq.hard_nr_sectors = rq.nr_sectors;
		rq.hard_cur_sectors = rq.current_nr_sectors = rq.nr_sectors;

		if (args->command_type == IDE_DRIVE_TASK_RAW_WRITE)
			rq.flags |= REQ_RW;
	}

	rq.special = args;
	args->rq = &rq;
	return ide_do_drive_cmd(drive, &rq, ide_wait);
}

int ide_raw_taskfile (ide_drive_t *drive, ide_task_t *args, u8 *buf)
{
	return ide_diag_taskfile(drive, args, 0, buf);
}

EXPORT_SYMBOL(ide_raw_taskfile);

int ide_taskfile_ioctl (ide_drive_t *drive, unsigned int cmd, unsigned long arg)
{
	ide_task_request_t	*req_task;
	ide_task_t		args;
	u8 *outbuf		= NULL;
	u8 *inbuf		= NULL;
	task_ioreg_t *argsptr	= args.tfRegister;
	task_ioreg_t *hobsptr	= args.hobRegister;
	int err			= 0;
	int tasksize		= sizeof(struct ide_task_request_s);
	int taskin		= 0;
	int taskout		= 0;
	u8 io_32bit		= drive->io_32bit;
	char __user *buf = (char __user *)arg;

//	printk("IDE Taskfile ...\n");

	req_task = kzalloc(tasksize, GFP_KERNEL);
	if (req_task == NULL) return -ENOMEM;
	if (copy_from_user(req_task, buf, tasksize)) {
		kfree(req_task);
		return -EFAULT;
	}

	taskout = (int) req_task->out_size;
	taskin  = (int) req_task->in_size;

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
	memcpy(argsptr, req_task->io_ports, HDIO_DRIVE_TASK_HDR_SIZE);
	memcpy(hobsptr, req_task->hob_ports, HDIO_DRIVE_HOB_HDR_SIZE);

	args.tf_in_flags  = req_task->in_flags;
	args.tf_out_flags = req_task->out_flags;
	args.data_phase   = req_task->data_phase;
	args.command_type = req_task->req_cmd;

	drive->io_32bit = 0;
	switch(req_task->data_phase) {
		case TASKFILE_OUT_DMAQ:
		case TASKFILE_OUT_DMA:
			err = ide_diag_taskfile(drive, &args, taskout, outbuf);
			break;
		case TASKFILE_IN_DMAQ:
		case TASKFILE_IN_DMA:
			err = ide_diag_taskfile(drive, &args, taskin, inbuf);
			break;
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
			args.prehandler = &pre_task_out_intr;
			args.handler = &task_out_intr;
			err = ide_diag_taskfile(drive, &args, taskout, outbuf);
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
			args.handler = &task_in_intr;
			err = ide_diag_taskfile(drive, &args, taskin, inbuf);
			break;
		case TASKFILE_NO_DATA:
			args.handler = &task_no_data_intr;
			err = ide_diag_taskfile(drive, &args, 0, NULL);
			break;
		default:
			err = -EFAULT;
			goto abort;
	}

	memcpy(req_task->io_ports, &(args.tfRegister), HDIO_DRIVE_TASK_HDR_SIZE);
	memcpy(req_task->hob_ports, &(args.hobRegister), HDIO_DRIVE_HOB_HDR_SIZE);
	req_task->in_flags  = args.tf_in_flags;
	req_task->out_flags = args.tf_out_flags;

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

	drive->io_32bit = io_32bit;

	return err;
}

int ide_wait_cmd (ide_drive_t *drive, u8 cmd, u8 nsect, u8 feature, u8 sectors, u8 *buf)
{
	struct request rq;
	u8 buffer[4];

	if (!buf)
		buf = buffer;
	memset(buf, 0, 4 + SECTOR_WORDS * 4 * sectors);
	ide_init_drive_cmd(&rq);
	rq.buffer = buf;
	*buf++ = cmd;
	*buf++ = nsect;
	*buf++ = feature;
	*buf++ = sectors;
	return ide_do_drive_cmd(drive, &rq, ide_wait);
}

/*
 * FIXME : this needs to map into at taskfile. <andre@linux-ide.org>
 */
int ide_cmd_ioctl (ide_drive_t *drive, unsigned int cmd, unsigned long arg)
{
	int err = 0;
	u8 args[4], *argbuf = args;
	u8 xfer_rate = 0;
	int argsize = 4;
	ide_task_t tfargs;

	if (NULL == (void *) arg) {
		struct request rq;
		ide_init_drive_cmd(&rq);
		return ide_do_drive_cmd(drive, &rq, ide_wait);
	}

	if (copy_from_user(args, (void __user *)arg, 4))
		return -EFAULT;

	memset(&tfargs, 0, sizeof(ide_task_t));
	tfargs.tfRegister[IDE_FEATURE_OFFSET] = args[2];
	tfargs.tfRegister[IDE_NSECTOR_OFFSET] = args[3];
	tfargs.tfRegister[IDE_SECTOR_OFFSET]  = args[1];
	tfargs.tfRegister[IDE_LCYL_OFFSET]    = 0x00;
	tfargs.tfRegister[IDE_HCYL_OFFSET]    = 0x00;
	tfargs.tfRegister[IDE_SELECT_OFFSET]  = 0x00;
	tfargs.tfRegister[IDE_COMMAND_OFFSET] = args[0];

	if (args[3]) {
		argsize = 4 + (SECTOR_WORDS * 4 * args[3]);
		argbuf = kzalloc(argsize, GFP_KERNEL);
		if (argbuf == NULL)
			return -ENOMEM;
	}
	if (set_transfer(drive, &tfargs)) {
		xfer_rate = args[1];
		if (ide_ata66_check(drive, &tfargs))
			goto abort;
	}

	err = ide_wait_cmd(drive, args[0], args[1], args[2], args[3], argbuf);

	if (!err && xfer_rate) {
		/* active-retuning-calls future */
		ide_set_xfer_rate(drive, xfer_rate);
		ide_driveid_update(drive);
	}
abort:
	if (copy_to_user((void __user *)arg, argbuf, argsize))
		err = -EFAULT;
	if (argsize > 4)
		kfree(argbuf);
	return err;
}

static int ide_wait_cmd_task(ide_drive_t *drive, u8 *buf)
{
	struct request rq;

	ide_init_drive_cmd(&rq);
	rq.flags = REQ_DRIVE_TASK;
	rq.buffer = buf;
	return ide_do_drive_cmd(drive, &rq, ide_wait);
}

/*
 * FIXME : this needs to map into at taskfile. <andre@linux-ide.org>
 */
int ide_task_ioctl (ide_drive_t *drive, unsigned int cmd, unsigned long arg)
{
	void __user *p = (void __user *)arg;
	int err = 0;
	u8 args[7], *argbuf = args;
	int argsize = 7;

	if (copy_from_user(args, p, 7))
		return -EFAULT;
	err = ide_wait_cmd_task(drive, argbuf);
	if (copy_to_user(p, argbuf, argsize))
		err = -EFAULT;
	return err;
}

/*
 * NOTICE: This is additions from IBM to provide a discrete interface,
 * for selective taskregister access operations.  Nice JOB Klaus!!!
 * Glad to be able to work and co-develop this with you and IBM.
 */
ide_startstop_t flagged_taskfile (ide_drive_t *drive, ide_task_t *task)
{
	ide_hwif_t *hwif	= HWIF(drive);
	task_struct_t *taskfile	= (task_struct_t *) task->tfRegister;
	hob_struct_t *hobfile	= (hob_struct_t *) task->hobRegister;
#if DEBUG_TASKFILE
	u8 status;
#endif

	if (task->data_phase == TASKFILE_MULTI_IN ||
	    task->data_phase == TASKFILE_MULTI_OUT) {
		if (!drive->mult_count) {
			printk(KERN_ERR "%s: multimode not set!\n", drive->name);
			return ide_stopped;
		}
	}

	/*
	 * (ks) Check taskfile in/out flags.
	 * If set, then execute as it is defined.
	 * If not set, then define default settings.
	 * The default values are:
	 *	write and read all taskfile registers (except data) 
	 *	write and read the hob registers (sector,nsector,lcyl,hcyl)
	 */
	if (task->tf_out_flags.all == 0) {
		task->tf_out_flags.all = IDE_TASKFILE_STD_OUT_FLAGS;
		if (drive->addressing == 1)
			task->tf_out_flags.all |= (IDE_HOB_STD_OUT_FLAGS << 8);
        }

	if (task->tf_in_flags.all == 0) {
		task->tf_in_flags.all = IDE_TASKFILE_STD_IN_FLAGS;
		if (drive->addressing == 1)
			task->tf_in_flags.all |= (IDE_HOB_STD_IN_FLAGS  << 8);
        }

	/* ALL Command Block Executions SHALL clear nIEN, unless otherwise */
	if (IDE_CONTROL_REG)
		/* clear nIEN */
		hwif->OUTB(drive->ctl, IDE_CONTROL_REG);
	SELECT_MASK(drive, 0);

#if DEBUG_TASKFILE
	status = hwif->INB(IDE_STATUS_REG);
	if (status & 0x80) {
		printk("flagged_taskfile -> Bad status. Status = %02x. wait 100 usec ...\n", status);
		udelay(100);
		status = hwif->INB(IDE_STATUS_REG);
		printk("flagged_taskfile -> Status = %02x\n", status);
	}
#endif

	if (task->tf_out_flags.b.data) {
		u16 data =  taskfile->data + (hobfile->data << 8);
		hwif->OUTW(data, IDE_DATA_REG);
	}

	/* (ks) send hob registers first */
	if (task->tf_out_flags.b.nsector_hob)
		hwif->OUTB(hobfile->sector_count, IDE_NSECTOR_REG);
	if (task->tf_out_flags.b.sector_hob)
		hwif->OUTB(hobfile->sector_number, IDE_SECTOR_REG);
	if (task->tf_out_flags.b.lcyl_hob)
		hwif->OUTB(hobfile->low_cylinder, IDE_LCYL_REG);
	if (task->tf_out_flags.b.hcyl_hob)
		hwif->OUTB(hobfile->high_cylinder, IDE_HCYL_REG);

	/* (ks) Send now the standard registers */
	if (task->tf_out_flags.b.error_feature)
		hwif->OUTB(taskfile->feature, IDE_FEATURE_REG);
	/* refers to number of sectors to transfer */
	if (task->tf_out_flags.b.nsector)
		hwif->OUTB(taskfile->sector_count, IDE_NSECTOR_REG);
	/* refers to sector offset or start sector */
	if (task->tf_out_flags.b.sector)
		hwif->OUTB(taskfile->sector_number, IDE_SECTOR_REG);
	if (task->tf_out_flags.b.lcyl)
		hwif->OUTB(taskfile->low_cylinder, IDE_LCYL_REG);
	if (task->tf_out_flags.b.hcyl)
		hwif->OUTB(taskfile->high_cylinder, IDE_HCYL_REG);

        /*
	 * (ks) In the flagged taskfile approch, we will use all specified
	 * registers and the register value will not be changed, except the
	 * select bit (master/slave) in the drive_head register. We must make
	 * sure that the desired drive is selected.
	 */
	hwif->OUTB(taskfile->device_head | drive->select.all, IDE_SELECT_REG);
	switch(task->data_phase) {

   	        case TASKFILE_OUT_DMAQ:
		case TASKFILE_OUT_DMA:
		case TASKFILE_IN_DMAQ:
		case TASKFILE_IN_DMA:
			hwif->dma_setup(drive);
			hwif->dma_exec_cmd(drive, taskfile->command);
			hwif->dma_start(drive);
			break;

	        default:
 			if (task->handler == NULL)
				return ide_stopped;

			/* Issue the command */
			if (task->prehandler) {
				hwif->OUTBSYNC(drive, taskfile->command, IDE_COMMAND_REG);
				ndelay(400);	/* FIXME */
				return task->prehandler(drive, task->rq);
			}
			ide_execute_command(drive, taskfile->command, task->handler, WAIT_WORSTCASE, NULL);
	}

	return ide_started;
}
