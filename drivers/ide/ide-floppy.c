/*
 * IDE ATAPI floppy driver.
 *
 * Copyright (C) 1996-1999  Gadi Oxman <gadio@netvision.net.il>
 * Copyright (C) 2000-2002  Paul Bristow <paul@paulbristow.net>
 * Copyright (C) 2005       Bartlomiej Zolnierkiewicz
 *
 * This driver supports the following IDE floppy drives:
 *
 * LS-120/240 SuperDisk
 * Iomega Zip 100/250
 * Iomega PC Card Clik!/PocketZip
 *
 * For a historical changelog see
 * Documentation/ide/ChangeLog.ide-floppy.1996-2002
 */

#define IDEFLOPPY_VERSION "1.00"

#include <linux/module.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/major.h>
#include <linux/errno.h>
#include <linux/genhd.h>
#include <linux/slab.h>
#include <linux/cdrom.h>
#include <linux/ide.h>
#include <linux/bitops.h>
#include <linux/mutex.h>

#include <scsi/scsi_ioctl.h>

#include <asm/byteorder.h>
#include <linux/irq.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <asm/unaligned.h>

/* define to see debug info */
#define IDEFLOPPY_DEBUG_LOG		0

/* #define IDEFLOPPY_DEBUG(fmt, args...) printk(KERN_INFO fmt, ## args) */
#define IDEFLOPPY_DEBUG(fmt, args...)

#if IDEFLOPPY_DEBUG_LOG
#define debug_log(fmt, args...) \
	printk(KERN_INFO "ide-floppy: " fmt, ## args)
#else
#define debug_log(fmt, args...) do {} while (0)
#endif


/* Some drives require a longer irq timeout. */
#define IDEFLOPPY_WAIT_CMD		(5 * WAIT_CMD)

/*
 * After each failed packet command we issue a request sense command and retry
 * the packet command IDEFLOPPY_MAX_PC_RETRIES times.
 */
#define IDEFLOPPY_MAX_PC_RETRIES	3

/*
 * With each packet command, we allocate a buffer of IDEFLOPPY_PC_BUFFER_SIZE
 * bytes.
 */
#define IDEFLOPPY_PC_BUFFER_SIZE	256

/*
 * In various places in the driver, we need to allocate storage for packet
 * commands and requests, which will remain valid while	we leave the driver to
 * wait for an interrupt or a timeout event.
 */
#define IDEFLOPPY_PC_STACK		(10 + IDEFLOPPY_MAX_PC_RETRIES)

/* format capacities descriptor codes */
#define CAPACITY_INVALID	0x00
#define CAPACITY_UNFORMATTED	0x01
#define CAPACITY_CURRENT	0x02
#define CAPACITY_NO_CARTRIDGE	0x03

/*
 * Most of our global data which we need to save even as we leave the driver
 * due to an interrupt or a timer event is stored in a variable of type
 * idefloppy_floppy_t, defined below.
 */
typedef struct ide_floppy_obj {
	ide_drive_t	*drive;
	ide_driver_t	*driver;
	struct gendisk	*disk;
	struct kref	kref;
	unsigned int	openers;	/* protected by BKL for now */

	/* Current packet command */
	struct ide_atapi_pc *pc;
	/* Last failed packet command */
	struct ide_atapi_pc *failed_pc;
	/* Packet command stack */
	struct ide_atapi_pc pc_stack[IDEFLOPPY_PC_STACK];
	/* Next free packet command storage space */
	int pc_stack_index;
	struct request rq_stack[IDEFLOPPY_PC_STACK];
	/* We implement a circular array */
	int rq_stack_index;

	/* Last error information */
	u8 sense_key, asc, ascq;
	/* delay this long before sending packet command */
	u8 ticks;
	int progress_indication;

	/* Device information */
	/* Current format */
	int blocks, block_size, bs_factor;
	/* Last format capacity descriptor */
	u8 cap_desc[8];
	/* Copy of the flexible disk page */
	u8 flexible_disk_page[32];
	/* Write protect */
	int wp;
	/* Supports format progress report */
	int srfp;
	/* Status/Action flags */
	unsigned long flags;
} idefloppy_floppy_t;

#define IDEFLOPPY_TICKS_DELAY	HZ/20	/* default delay for ZIP 100 (50ms) */

/* Floppy flag bits values. */
enum {
	/* DRQ interrupt device */
	IDEFLOPPY_FLAG_DRQ_INTERRUPT		= (1 <<	0),
	/* Media may have changed */
	IDEFLOPPY_FLAG_MEDIA_CHANGED		= (1 << 1),
	/* Format in progress */
	IDEFLOPPY_FLAG_FORMAT_IN_PROGRESS	= (1 << 2),
	/* Avoid commands not supported in Clik drive */
	IDEFLOPPY_FLAG_CLIK_DRIVE		= (1 << 3),
	/* Requires BH algorithm for packets */
	IDEFLOPPY_FLAG_ZIP_DRIVE		= (1 << 4),
};

/* Defines for the MODE SENSE command */
#define MODE_SENSE_CURRENT		0x00
#define MODE_SENSE_CHANGEABLE		0x01
#define MODE_SENSE_DEFAULT		0x02
#define MODE_SENSE_SAVED		0x03

/* IOCTLs used in low-level formatting. */
#define	IDEFLOPPY_IOCTL_FORMAT_SUPPORTED	0x4600
#define	IDEFLOPPY_IOCTL_FORMAT_GET_CAPACITY	0x4601
#define	IDEFLOPPY_IOCTL_FORMAT_START		0x4602
#define IDEFLOPPY_IOCTL_FORMAT_GET_PROGRESS	0x4603

/* Error code returned in rq->errors to the higher part of the driver. */
#define	IDEFLOPPY_ERROR_GENERAL		101

/*
 * Pages of the SELECT SENSE / MODE SENSE packet commands.
 * See SFF-8070i spec.
 */
#define	IDEFLOPPY_CAPABILITIES_PAGE	0x1b
#define IDEFLOPPY_FLEXIBLE_DISK_PAGE	0x05

static DEFINE_MUTEX(idefloppy_ref_mutex);

#define to_ide_floppy(obj) container_of(obj, struct ide_floppy_obj, kref)

#define ide_floppy_g(disk) \
	container_of((disk)->private_data, struct ide_floppy_obj, driver)

static struct ide_floppy_obj *ide_floppy_get(struct gendisk *disk)
{
	struct ide_floppy_obj *floppy = NULL;

	mutex_lock(&idefloppy_ref_mutex);
	floppy = ide_floppy_g(disk);
	if (floppy)
		kref_get(&floppy->kref);
	mutex_unlock(&idefloppy_ref_mutex);
	return floppy;
}

static void idefloppy_cleanup_obj(struct kref *);

static void ide_floppy_put(struct ide_floppy_obj *floppy)
{
	mutex_lock(&idefloppy_ref_mutex);
	kref_put(&floppy->kref, idefloppy_cleanup_obj);
	mutex_unlock(&idefloppy_ref_mutex);
}

/*
 * Used to finish servicing a request. For read/write requests, we will call
 * ide_end_request to pass to the next buffer.
 */
static int idefloppy_end_request(ide_drive_t *drive, int uptodate, int nsecs)
{
	idefloppy_floppy_t *floppy = drive->driver_data;
	struct request *rq = HWGROUP(drive)->rq;
	int error;

	debug_log("Reached %s\n", __func__);

	switch (uptodate) {
	case 0: error = IDEFLOPPY_ERROR_GENERAL; break;
	case 1: error = 0; break;
	default: error = uptodate;
	}
	if (error)
		floppy->failed_pc = NULL;
	/* Why does this happen? */
	if (!rq)
		return 0;
	if (!blk_special_request(rq)) {
		/* our real local end request function */
		ide_end_request(drive, uptodate, nsecs);
		return 0;
	}
	rq->errors = error;
	/* fixme: need to move this local also */
	ide_end_drive_cmd(drive, 0, 0);
	return 0;
}

static void ide_floppy_io_buffers(ide_drive_t *drive, struct ide_atapi_pc *pc,
				  unsigned int bcount, int direction)
{
	ide_hwif_t *hwif = drive->hwif;
	struct request *rq = pc->rq;
	struct req_iterator iter;
	struct bio_vec *bvec;
	unsigned long flags;
	int count, done = 0;
	char *data;

	rq_for_each_segment(bvec, rq, iter) {
		if (!bcount)
			break;

		count = min(bvec->bv_len, bcount);

		data = bvec_kmap_irq(bvec, &flags);
		if (direction)
			hwif->output_data(drive, NULL, data, count);
		else
			hwif->input_data(drive, NULL, data, count);
		bvec_kunmap_irq(data, &flags);

		bcount -= count;
		pc->b_count += count;
		done += count;
	}

	idefloppy_end_request(drive, 1, done >> 9);

	if (bcount) {
		printk(KERN_ERR "%s: leftover data in %s, bcount == %d\n",
				drive->name, __func__, bcount);
		ide_pad_transfer(drive, direction, bcount);
	}
}

static void idefloppy_update_buffers(ide_drive_t *drive,
				struct ide_atapi_pc *pc)
{
	struct request *rq = pc->rq;
	struct bio *bio = rq->bio;

	while ((bio = rq->bio) != NULL)
		idefloppy_end_request(drive, 1, 0);
}

/*
 * Generate a new packet command request in front of the request queue, before
 * the current request so that it will be processed immediately, on the next
 * pass through the driver.
 */
static void idefloppy_queue_pc_head(ide_drive_t *drive, struct ide_atapi_pc *pc,
		struct request *rq)
{
	struct ide_floppy_obj *floppy = drive->driver_data;

	ide_init_drive_cmd(rq);
	rq->buffer = (char *) pc;
	rq->cmd_type = REQ_TYPE_SPECIAL;
	rq->rq_disk = floppy->disk;
	(void) ide_do_drive_cmd(drive, rq, ide_preempt);
}

static struct ide_atapi_pc *idefloppy_next_pc_storage(ide_drive_t *drive)
{
	idefloppy_floppy_t *floppy = drive->driver_data;

	if (floppy->pc_stack_index == IDEFLOPPY_PC_STACK)
		floppy->pc_stack_index = 0;
	return (&floppy->pc_stack[floppy->pc_stack_index++]);
}

static struct request *idefloppy_next_rq_storage(ide_drive_t *drive)
{
	idefloppy_floppy_t *floppy = drive->driver_data;

	if (floppy->rq_stack_index == IDEFLOPPY_PC_STACK)
		floppy->rq_stack_index = 0;
	return (&floppy->rq_stack[floppy->rq_stack_index++]);
}

static void idefloppy_request_sense_callback(ide_drive_t *drive)
{
	idefloppy_floppy_t *floppy = drive->driver_data;
	u8 *buf = floppy->pc->buf;

	debug_log("Reached %s\n", __func__);

	if (!floppy->pc->error) {
		floppy->sense_key = buf[2] & 0x0F;
		floppy->asc = buf[12];
		floppy->ascq = buf[13];
		floppy->progress_indication = buf[15] & 0x80 ?
			(u16)get_unaligned((u16 *)&buf[16]) : 0x10000;

		if (floppy->failed_pc)
			debug_log("pc = %x, sense key = %x, asc = %x,"
					" ascq = %x\n",
					floppy->failed_pc->c[0],
					floppy->sense_key,
					floppy->asc,
					floppy->ascq);
		else
			debug_log("sense key = %x, asc = %x, ascq = %x\n",
					floppy->sense_key,
					floppy->asc,
					floppy->ascq);


		idefloppy_end_request(drive, 1, 0);
	} else {
		printk(KERN_ERR "Error in REQUEST SENSE itself - Aborting"
				" request!\n");
		idefloppy_end_request(drive, 0, 0);
	}
}

/* General packet command callback function. */
static void idefloppy_pc_callback(ide_drive_t *drive)
{
	idefloppy_floppy_t *floppy = drive->driver_data;

	debug_log("Reached %s\n", __func__);

	idefloppy_end_request(drive, floppy->pc->error ? 0 : 1, 0);
}

static void idefloppy_init_pc(struct ide_atapi_pc *pc)
{
	memset(pc->c, 0, 12);
	pc->retries = 0;
	pc->flags = 0;
	pc->req_xfer = 0;
	pc->buf = pc->pc_buf;
	pc->buf_size = IDEFLOPPY_PC_BUFFER_SIZE;
	pc->idefloppy_callback = &idefloppy_pc_callback;
}

static void idefloppy_create_request_sense_cmd(struct ide_atapi_pc *pc)
{
	idefloppy_init_pc(pc);
	pc->c[0] = GPCMD_REQUEST_SENSE;
	pc->c[4] = 255;
	pc->req_xfer = 18;
	pc->idefloppy_callback = &idefloppy_request_sense_callback;
}

/*
 * Called when an error was detected during the last packet command. We queue a
 * request sense packet command in the head of the request list.
 */
static void idefloppy_retry_pc(ide_drive_t *drive)
{
	struct ide_atapi_pc *pc;
	struct request *rq;

	(void)ide_read_error(drive);
	pc = idefloppy_next_pc_storage(drive);
	rq = idefloppy_next_rq_storage(drive);
	idefloppy_create_request_sense_cmd(pc);
	idefloppy_queue_pc_head(drive, pc, rq);
}

/* The usual interrupt handler called during a packet command. */
static ide_startstop_t idefloppy_pc_intr(ide_drive_t *drive)
{
	idefloppy_floppy_t *floppy = drive->driver_data;
	ide_hwif_t *hwif = drive->hwif;
	struct ide_atapi_pc *pc = floppy->pc;
	struct request *rq = pc->rq;
	xfer_func_t *xferfunc;
	unsigned int temp;
	int dma_error = 0;
	u16 bcount;
	u8 stat, ireason;

	debug_log("Reached %s interrupt handler\n", __func__);

	if (pc->flags & PC_FLAG_DMA_IN_PROGRESS) {
		dma_error = hwif->dma_ops->dma_end(drive);
		if (dma_error) {
			printk(KERN_ERR "%s: DMA %s error\n", drive->name,
					rq_data_dir(rq) ? "write" : "read");
			pc->flags |= PC_FLAG_DMA_ERROR;
		} else {
			pc->xferred = pc->req_xfer;
			idefloppy_update_buffers(drive, pc);
		}
		debug_log("DMA finished\n");
	}

	/* Clear the interrupt */
	stat = ide_read_status(drive);

	/* No more interrupts */
	if ((stat & DRQ_STAT) == 0) {
		debug_log("Packet command completed, %d bytes transferred\n",
				pc->xferred);
		pc->flags &= ~PC_FLAG_DMA_IN_PROGRESS;

		local_irq_enable_in_hardirq();

		if ((stat & ERR_STAT) || (pc->flags & PC_FLAG_DMA_ERROR)) {
			/* Error detected */
			debug_log("%s: I/O error\n", drive->name);
			rq->errors++;
			if (pc->c[0] == GPCMD_REQUEST_SENSE) {
				printk(KERN_ERR "ide-floppy: I/O error in "
					"request sense command\n");
				return ide_do_reset(drive);
			}
			/* Retry operation */
			idefloppy_retry_pc(drive);
			/* queued, but not started */
			return ide_stopped;
		}
		pc->error = 0;
		if (floppy->failed_pc == pc)
			floppy->failed_pc = NULL;
		/* Command finished - Call the callback function */
		pc->idefloppy_callback(drive);
		return ide_stopped;
	}

	if (pc->flags & PC_FLAG_DMA_IN_PROGRESS) {
		pc->flags &= ~PC_FLAG_DMA_IN_PROGRESS;
		printk(KERN_ERR "ide-floppy: The floppy wants to issue "
			"more interrupts in DMA mode\n");
		ide_dma_off(drive);
		return ide_do_reset(drive);
	}

	/* Get the number of bytes to transfer */
	bcount = (hwif->INB(hwif->io_ports.lbah_addr) << 8) |
		  hwif->INB(hwif->io_ports.lbam_addr);
	/* on this interrupt */
	ireason = hwif->INB(hwif->io_ports.nsect_addr);

	if (ireason & CD) {
		printk(KERN_ERR "ide-floppy: CoD != 0 in %s\n", __func__);
		return ide_do_reset(drive);
	}
	if (((ireason & IO) == IO) == !!(pc->flags & PC_FLAG_WRITING)) {
		/* Hopefully, we will never get here */
		printk(KERN_ERR "ide-floppy: We wanted to %s, ",
				(ireason & IO) ? "Write" : "Read");
		printk(KERN_ERR "but the floppy wants us to %s !\n",
				(ireason & IO) ? "Read" : "Write");
		return ide_do_reset(drive);
	}
	if (!(pc->flags & PC_FLAG_WRITING)) {
		/* Reading - Check that we have enough space */
		temp = pc->xferred + bcount;
		if (temp > pc->req_xfer) {
			if (temp > pc->buf_size) {
				printk(KERN_ERR "ide-floppy: The floppy wants "
					"to send us more data than expected "
					"- discarding data\n");
				ide_pad_transfer(drive, 0, bcount);

				ide_set_handler(drive,
						&idefloppy_pc_intr,
						IDEFLOPPY_WAIT_CMD,
						NULL);
				return ide_started;
			}
			debug_log("The floppy wants to send us more data than"
					" expected - allowing transfer\n");
		}
	}
	if (pc->flags & PC_FLAG_WRITING)
		xferfunc = hwif->output_data;
	else
		xferfunc = hwif->input_data;

	if (pc->buf)
		xferfunc(drive, NULL, pc->cur_pos, bcount);
	else
		ide_floppy_io_buffers(drive, pc, bcount,
				      !!(pc->flags & PC_FLAG_WRITING));

	/* Update the current position */
	pc->xferred += bcount;
	pc->cur_pos += bcount;

	/* And set the interrupt handler again */
	ide_set_handler(drive, &idefloppy_pc_intr, IDEFLOPPY_WAIT_CMD, NULL);
	return ide_started;
}

/*
 * This is the original routine that did the packet transfer.
 * It fails at high speeds on the Iomega ZIP drive, so there's a slower version
 * for that drive below. The algorithm is chosen based on drive type
 */
static ide_startstop_t idefloppy_transfer_pc(ide_drive_t *drive)
{
	ide_hwif_t *hwif = drive->hwif;
	ide_startstop_t startstop;
	idefloppy_floppy_t *floppy = drive->driver_data;
	u8 ireason;

	if (ide_wait_stat(&startstop, drive, DRQ_STAT, BUSY_STAT, WAIT_READY)) {
		printk(KERN_ERR "ide-floppy: Strange, packet command "
				"initiated yet DRQ isn't asserted\n");
		return startstop;
	}
	ireason = hwif->INB(hwif->io_ports.nsect_addr);
	if ((ireason & CD) == 0 || (ireason & IO)) {
		printk(KERN_ERR "ide-floppy: (IO,CoD) != (0,1) while "
				"issuing a packet command\n");
		return ide_do_reset(drive);
	}

	/* Set the interrupt routine */
	ide_set_handler(drive, &idefloppy_pc_intr, IDEFLOPPY_WAIT_CMD, NULL);

	/* Send the actual packet */
	hwif->output_data(drive, NULL, floppy->pc->c, 12);

	return ide_started;
}


/*
 * What we have here is a classic case of a top half / bottom half interrupt
 * service routine. In interrupt mode, the device sends an interrupt to signal
 * that it is ready to receive a packet. However, we need to delay about 2-3
 * ticks before issuing the packet or we gets in trouble.
 *
 * So, follow carefully. transfer_pc1 is called as an interrupt (or directly).
 * In either case, when the device says it's ready for a packet, we schedule
 * the packet transfer to occur about 2-3 ticks later in transfer_pc2.
 */
static int idefloppy_transfer_pc2(ide_drive_t *drive)
{
	idefloppy_floppy_t *floppy = drive->driver_data;

	/* Send the actual packet */
	drive->hwif->output_data(drive, NULL, floppy->pc->c, 12);

	/* Timeout for the packet command */
	return IDEFLOPPY_WAIT_CMD;
}

static ide_startstop_t idefloppy_transfer_pc1(ide_drive_t *drive)
{
	ide_hwif_t *hwif = drive->hwif;
	idefloppy_floppy_t *floppy = drive->driver_data;
	ide_startstop_t startstop;
	u8 ireason;

	if (ide_wait_stat(&startstop, drive, DRQ_STAT, BUSY_STAT, WAIT_READY)) {
		printk(KERN_ERR "ide-floppy: Strange, packet command "
				"initiated yet DRQ isn't asserted\n");
		return startstop;
	}
	ireason = hwif->INB(hwif->io_ports.nsect_addr);
	if ((ireason & CD) == 0 || (ireason & IO)) {
		printk(KERN_ERR "ide-floppy: (IO,CoD) != (0,1) "
				"while issuing a packet command\n");
		return ide_do_reset(drive);
	}
	/*
	 * The following delay solves a problem with ATAPI Zip 100 drives
	 * where the Busy flag was apparently being deasserted before the
	 * unit was ready to receive data. This was happening on a
	 * 1200 MHz Athlon system. 10/26/01 25msec is too short,
	 * 40 and 50msec work well. idefloppy_pc_intr will not be actually
	 * used until after the packet is moved in about 50 msec.
	 */

	ide_set_handler(drive, &idefloppy_pc_intr, floppy->ticks,
			&idefloppy_transfer_pc2);
	return ide_started;
}

static void ide_floppy_report_error(idefloppy_floppy_t *floppy,
				    struct ide_atapi_pc *pc)
{
	/* supress error messages resulting from Medium not present */
	if (floppy->sense_key == 0x02 &&
	    floppy->asc       == 0x3a &&
	    floppy->ascq      == 0x00)
		return;

	printk(KERN_ERR "ide-floppy: %s: I/O error, pc = %2x, key = %2x, "
			"asc = %2x, ascq = %2x\n",
			floppy->drive->name, pc->c[0], floppy->sense_key,
			floppy->asc, floppy->ascq);

}

static ide_startstop_t idefloppy_issue_pc(ide_drive_t *drive,
		struct ide_atapi_pc *pc)
{
	idefloppy_floppy_t *floppy = drive->driver_data;
	ide_hwif_t *hwif = drive->hwif;
	ide_handler_t *pkt_xfer_routine;
	u16 bcount;
	u8 dma;

	if (floppy->failed_pc == NULL &&
	    pc->c[0] != GPCMD_REQUEST_SENSE)
		floppy->failed_pc = pc;
	/* Set the current packet command */
	floppy->pc = pc;

	if (pc->retries > IDEFLOPPY_MAX_PC_RETRIES) {
		if (!(pc->flags & PC_FLAG_SUPPRESS_ERROR))
			ide_floppy_report_error(floppy, pc);
		/* Giving up */
		pc->error = IDEFLOPPY_ERROR_GENERAL;

		floppy->failed_pc = NULL;
		pc->idefloppy_callback(drive);
		return ide_stopped;
	}

	debug_log("Retry number - %d\n", pc->retries);

	pc->retries++;
	/* We haven't transferred any data yet */
	pc->xferred = 0;
	pc->cur_pos = pc->buf;
	bcount = min(pc->req_xfer, 63 * 1024);

	if (pc->flags & PC_FLAG_DMA_ERROR) {
		pc->flags &= ~PC_FLAG_DMA_ERROR;
		ide_dma_off(drive);
	}
	dma = 0;

	if ((pc->flags & PC_FLAG_DMA_RECOMMENDED) && drive->using_dma)
		dma = !hwif->dma_ops->dma_setup(drive);

	ide_pktcmd_tf_load(drive, IDE_TFLAG_NO_SELECT_MASK |
			   IDE_TFLAG_OUT_DEVICE, bcount, dma);

	if (dma) {
		/* Begin DMA, if necessary */
		pc->flags |= PC_FLAG_DMA_IN_PROGRESS;
		hwif->dma_ops->dma_start(drive);
	}

	/* Can we transfer the packet when we get the interrupt or wait? */
	if (floppy->flags & IDEFLOPPY_FLAG_ZIP_DRIVE) {
		/* wait */
		pkt_xfer_routine = &idefloppy_transfer_pc1;
	} else {
		/* immediate */
		pkt_xfer_routine = &idefloppy_transfer_pc;
	}

	if (floppy->flags & IDEFLOPPY_FLAG_DRQ_INTERRUPT) {
		/* Issue the packet command */
		ide_execute_command(drive, WIN_PACKETCMD,
				pkt_xfer_routine,
				IDEFLOPPY_WAIT_CMD,
				NULL);
		return ide_started;
	} else {
		/* Issue the packet command */
		ide_execute_pkt_cmd(drive);
		return (*pkt_xfer_routine) (drive);
	}
}

static void idefloppy_rw_callback(ide_drive_t *drive)
{
	debug_log("Reached %s\n", __func__);

	idefloppy_end_request(drive, 1, 0);
	return;
}

static void idefloppy_create_prevent_cmd(struct ide_atapi_pc *pc, int prevent)
{
	debug_log("creating prevent removal command, prevent = %d\n", prevent);

	idefloppy_init_pc(pc);
	pc->c[0] = GPCMD_PREVENT_ALLOW_MEDIUM_REMOVAL;
	pc->c[4] = prevent;
}

static void idefloppy_create_read_capacity_cmd(struct ide_atapi_pc *pc)
{
	idefloppy_init_pc(pc);
	pc->c[0] = GPCMD_READ_FORMAT_CAPACITIES;
	pc->c[7] = 255;
	pc->c[8] = 255;
	pc->req_xfer = 255;
}

static void idefloppy_create_format_unit_cmd(struct ide_atapi_pc *pc, int b,
		int l, int flags)
{
	idefloppy_init_pc(pc);
	pc->c[0] = GPCMD_FORMAT_UNIT;
	pc->c[1] = 0x17;

	memset(pc->buf, 0, 12);
	pc->buf[1] = 0xA2;
	/* Default format list header, u8 1: FOV/DCRT/IMM bits set */

	if (flags & 1)				/* Verify bit on... */
		pc->buf[1] ^= 0x20;		/* ... turn off DCRT bit */
	pc->buf[3] = 8;

	put_unaligned(cpu_to_be32(b), (unsigned int *)(&pc->buf[4]));
	put_unaligned(cpu_to_be32(l), (unsigned int *)(&pc->buf[8]));
	pc->buf_size = 12;
	pc->flags |= PC_FLAG_WRITING;
}

/* A mode sense command is used to "sense" floppy parameters. */
static void idefloppy_create_mode_sense_cmd(struct ide_atapi_pc *pc,
		u8 page_code, u8 type)
{
	u16 length = 8; /* sizeof(Mode Parameter Header) = 8 Bytes */

	idefloppy_init_pc(pc);
	pc->c[0] = GPCMD_MODE_SENSE_10;
	pc->c[1] = 0;
	pc->c[2] = page_code + (type << 6);

	switch (page_code) {
	case IDEFLOPPY_CAPABILITIES_PAGE:
		length += 12;
		break;
	case IDEFLOPPY_FLEXIBLE_DISK_PAGE:
		length += 32;
		break;
	default:
		printk(KERN_ERR "ide-floppy: unsupported page code "
				"in create_mode_sense_cmd\n");
	}
	put_unaligned(cpu_to_be16(length), (u16 *) &pc->c[7]);
	pc->req_xfer = length;
}

static void idefloppy_create_start_stop_cmd(struct ide_atapi_pc *pc, int start)
{
	idefloppy_init_pc(pc);
	pc->c[0] = GPCMD_START_STOP_UNIT;
	pc->c[4] = start;
}

static void idefloppy_create_test_unit_ready_cmd(struct ide_atapi_pc *pc)
{
	idefloppy_init_pc(pc);
	pc->c[0] = GPCMD_TEST_UNIT_READY;
}

static void idefloppy_create_rw_cmd(idefloppy_floppy_t *floppy,
				    struct ide_atapi_pc *pc, struct request *rq,
				    unsigned long sector)
{
	int block = sector / floppy->bs_factor;
	int blocks = rq->nr_sectors / floppy->bs_factor;
	int cmd = rq_data_dir(rq);

	debug_log("create_rw10_cmd: block == %d, blocks == %d\n",
		block, blocks);

	idefloppy_init_pc(pc);
	pc->c[0] = cmd == READ ? GPCMD_READ_10 : GPCMD_WRITE_10;
	put_unaligned(cpu_to_be16(blocks), (unsigned short *)&pc->c[7]);
	put_unaligned(cpu_to_be32(block), (unsigned int *) &pc->c[2]);

	pc->idefloppy_callback = &idefloppy_rw_callback;
	pc->rq = rq;
	pc->b_count = cmd == READ ? 0 : rq->bio->bi_size;
	if (rq->cmd_flags & REQ_RW)
		pc->flags |= PC_FLAG_WRITING;
	pc->buf = NULL;
	pc->req_xfer = pc->buf_size = blocks * floppy->block_size;
	pc->flags |= PC_FLAG_DMA_RECOMMENDED;
}

static void idefloppy_blockpc_cmd(idefloppy_floppy_t *floppy,
		struct ide_atapi_pc *pc, struct request *rq)
{
	idefloppy_init_pc(pc);
	pc->idefloppy_callback = &idefloppy_rw_callback;
	memcpy(pc->c, rq->cmd, sizeof(pc->c));
	pc->rq = rq;
	pc->b_count = rq->data_len;
	if (rq->data_len && rq_data_dir(rq) == WRITE)
		pc->flags |= PC_FLAG_WRITING;
	pc->buf = rq->data;
	if (rq->bio)
		pc->flags |= PC_FLAG_DMA_RECOMMENDED;
	/*
	 * possibly problematic, doesn't look like ide-floppy correctly
	 * handled scattered requests if dma fails...
	 */
	pc->req_xfer = pc->buf_size = rq->data_len;
}

static ide_startstop_t idefloppy_do_request(ide_drive_t *drive,
		struct request *rq, sector_t block_s)
{
	idefloppy_floppy_t *floppy = drive->driver_data;
	struct ide_atapi_pc *pc;
	unsigned long block = (unsigned long)block_s;

	debug_log("dev: %s, cmd_type: %x, errors: %d\n",
			rq->rq_disk ? rq->rq_disk->disk_name : "?",
			rq->cmd_type, rq->errors);
	debug_log("sector: %ld, nr_sectors: %ld, "
			"current_nr_sectors: %d\n", (long)rq->sector,
			rq->nr_sectors, rq->current_nr_sectors);

	if (rq->errors >= ERROR_MAX) {
		if (floppy->failed_pc)
			ide_floppy_report_error(floppy, floppy->failed_pc);
		else
			printk(KERN_ERR "ide-floppy: %s: I/O error\n",
				drive->name);
		idefloppy_end_request(drive, 0, 0);
		return ide_stopped;
	}
	if (blk_fs_request(rq)) {
		if (((long)rq->sector % floppy->bs_factor) ||
		    (rq->nr_sectors % floppy->bs_factor)) {
			printk(KERN_ERR "%s: unsupported r/w request size\n",
					drive->name);
			idefloppy_end_request(drive, 0, 0);
			return ide_stopped;
		}
		pc = idefloppy_next_pc_storage(drive);
		idefloppy_create_rw_cmd(floppy, pc, rq, block);
	} else if (blk_special_request(rq)) {
		pc = (struct ide_atapi_pc *) rq->buffer;
	} else if (blk_pc_request(rq)) {
		pc = idefloppy_next_pc_storage(drive);
		idefloppy_blockpc_cmd(floppy, pc, rq);
	} else {
		blk_dump_rq_flags(rq,
			"ide-floppy: unsupported command in queue");
		idefloppy_end_request(drive, 0, 0);
		return ide_stopped;
	}

	pc->rq = rq;
	return idefloppy_issue_pc(drive, pc);
}

/*
 * Add a special packet command request to the tail of the request queue,
 * and wait for it to be serviced.
 */
static int idefloppy_queue_pc_tail(ide_drive_t *drive, struct ide_atapi_pc *pc)
{
	struct ide_floppy_obj *floppy = drive->driver_data;
	struct request rq;

	ide_init_drive_cmd(&rq);
	rq.buffer = (char *) pc;
	rq.cmd_type = REQ_TYPE_SPECIAL;
	rq.rq_disk = floppy->disk;

	return ide_do_drive_cmd(drive, &rq, ide_wait);
}

/*
 * Look at the flexible disk page parameters. We ignore the CHS capacity
 * parameters and use the LBA parameters instead.
 */
static int ide_floppy_get_flexible_disk_page(ide_drive_t *drive)
{
	idefloppy_floppy_t *floppy = drive->driver_data;
	struct ide_atapi_pc pc;
	u8 *page;
	int capacity, lba_capacity;
	u16 transfer_rate, sector_size, cyls, rpm;
	u8 heads, sectors;

	idefloppy_create_mode_sense_cmd(&pc, IDEFLOPPY_FLEXIBLE_DISK_PAGE,
					MODE_SENSE_CURRENT);

	if (idefloppy_queue_pc_tail(drive, &pc)) {
		printk(KERN_ERR "ide-floppy: Can't get flexible disk page"
				" parameters\n");
		return 1;
	}
	floppy->wp = !!(pc.buf[3] & 0x80);
	set_disk_ro(floppy->disk, floppy->wp);
	page = &pc.buf[8];

	transfer_rate = be16_to_cpu(*(u16 *)&pc.buf[8 + 2]);
	sector_size   = be16_to_cpu(*(u16 *)&pc.buf[8 + 6]);
	cyls          = be16_to_cpu(*(u16 *)&pc.buf[8 + 8]);
	rpm           = be16_to_cpu(*(u16 *)&pc.buf[8 + 28]);
	heads         = pc.buf[8 + 4];
	sectors       = pc.buf[8 + 5];

	capacity = cyls * heads * sectors * sector_size;

	if (memcmp(page, &floppy->flexible_disk_page, 32))
		printk(KERN_INFO "%s: %dkB, %d/%d/%d CHS, %d kBps, "
				"%d sector size, %d rpm\n",
				drive->name, capacity / 1024, cyls, heads,
				sectors, transfer_rate / 8, sector_size, rpm);

	memcpy(&floppy->flexible_disk_page, page, 32);
	drive->bios_cyl = cyls;
	drive->bios_head = heads;
	drive->bios_sect = sectors;
	lba_capacity = floppy->blocks * floppy->block_size;

	if (capacity < lba_capacity) {
		printk(KERN_NOTICE "%s: The disk reports a capacity of %d "
			"bytes, but the drive only handles %d\n",
			drive->name, lba_capacity, capacity);
		floppy->blocks = floppy->block_size ?
			capacity / floppy->block_size : 0;
	}
	return 0;
}

static int idefloppy_get_sfrp_bit(ide_drive_t *drive)
{
	idefloppy_floppy_t *floppy = drive->driver_data;
	struct ide_atapi_pc pc;

	floppy->srfp = 0;
	idefloppy_create_mode_sense_cmd(&pc, IDEFLOPPY_CAPABILITIES_PAGE,
						 MODE_SENSE_CURRENT);

	pc.flags |= PC_FLAG_SUPPRESS_ERROR;
	if (idefloppy_queue_pc_tail(drive, &pc))
		return 1;

	floppy->srfp = pc.buf[8 + 2] & 0x40;
	return (0);
}

/*
 * Determine if a media is present in the floppy drive, and if so, its LBA
 * capacity.
 */
static int ide_floppy_get_capacity(ide_drive_t *drive)
{
	idefloppy_floppy_t *floppy = drive->driver_data;
	struct ide_atapi_pc pc;
	u8 *cap_desc;
	u8 header_len, desc_cnt;
	int i, rc = 1, blocks, length;

	drive->bios_cyl = 0;
	drive->bios_head = drive->bios_sect = 0;
	floppy->blocks = 0;
	floppy->bs_factor = 1;
	set_capacity(floppy->disk, 0);

	idefloppy_create_read_capacity_cmd(&pc);
	if (idefloppy_queue_pc_tail(drive, &pc)) {
		printk(KERN_ERR "ide-floppy: Can't get floppy parameters\n");
		return 1;
	}
	header_len = pc.buf[3];
	cap_desc = &pc.buf[4];
	desc_cnt = header_len / 8; /* capacity descriptor of 8 bytes */

	for (i = 0; i < desc_cnt; i++) {
		unsigned int desc_start = 4 + i*8;

		blocks = be32_to_cpu(*(u32 *)&pc.buf[desc_start]);
		length = be16_to_cpu(*(u16 *)&pc.buf[desc_start + 6]);

		debug_log("Descriptor %d: %dkB, %d blocks, %d sector size\n",
				i, blocks * length / 1024, blocks, length);

		if (i)
			continue;
		/*
		 * the code below is valid only for the 1st descriptor, ie i=0
		 */

		switch (pc.buf[desc_start + 4] & 0x03) {
		/* Clik! drive returns this instead of CAPACITY_CURRENT */
		case CAPACITY_UNFORMATTED:
			if (!(floppy->flags & IDEFLOPPY_FLAG_CLIK_DRIVE))
				/*
				 * If it is not a clik drive, break out
				 * (maintains previous driver behaviour)
				 */
				break;
		case CAPACITY_CURRENT:
			/* Normal Zip/LS-120 disks */
			if (memcmp(cap_desc, &floppy->cap_desc, 8))
				printk(KERN_INFO "%s: %dkB, %d blocks, %d "
					"sector size\n", drive->name,
					blocks * length / 1024, blocks, length);
			memcpy(&floppy->cap_desc, cap_desc, 8);

			if (!length || length % 512) {
				printk(KERN_NOTICE "%s: %d bytes block size "
					"not supported\n", drive->name, length);
			} else {
				floppy->blocks = blocks;
				floppy->block_size = length;
				floppy->bs_factor = length / 512;
				if (floppy->bs_factor != 1)
					printk(KERN_NOTICE "%s: warning: non "
						"512 bytes block size not "
						"fully supported\n",
						drive->name);
				rc = 0;
			}
			break;
		case CAPACITY_NO_CARTRIDGE:
			/*
			 * This is a KERN_ERR so it appears on screen
			 * for the user to see
			 */
			printk(KERN_ERR "%s: No disk in drive\n", drive->name);
			break;
		case CAPACITY_INVALID:
			printk(KERN_ERR "%s: Invalid capacity for disk "
				"in drive\n", drive->name);
			break;
		}
		debug_log("Descriptor 0 Code: %d\n",
			  pc.buf[desc_start + 4] & 0x03);
	}

	/* Clik! disk does not support get_flexible_disk_page */
	if (!(floppy->flags & IDEFLOPPY_FLAG_CLIK_DRIVE))
		(void) ide_floppy_get_flexible_disk_page(drive);

	set_capacity(floppy->disk, floppy->blocks * floppy->bs_factor);
	return rc;
}

/*
 * Obtain the list of formattable capacities.
 * Very similar to ide_floppy_get_capacity, except that we push the capacity
 * descriptors to userland, instead of our own structures.
 *
 * Userland gives us the following structure:
 *
 * struct idefloppy_format_capacities {
 *	int nformats;
 *	struct {
 *		int nblocks;
 *		int blocksize;
 *	} formats[];
 * };
 *
 * userland initializes nformats to the number of allocated formats[] records.
 * On exit we set nformats to the number of records we've actually initialized.
 */

static int ide_floppy_get_format_capacities(ide_drive_t *drive, int __user *arg)
{
	struct ide_atapi_pc pc;
	u8 header_len, desc_cnt;
	int i, blocks, length, u_array_size, u_index;
	int __user *argp;

	if (get_user(u_array_size, arg))
		return (-EFAULT);

	if (u_array_size <= 0)
		return (-EINVAL);

	idefloppy_create_read_capacity_cmd(&pc);
	if (idefloppy_queue_pc_tail(drive, &pc)) {
		printk(KERN_ERR "ide-floppy: Can't get floppy parameters\n");
		return (-EIO);
	}
	header_len = pc.buf[3];
	desc_cnt = header_len / 8; /* capacity descriptor of 8 bytes */

	u_index = 0;
	argp = arg + 1;

	/*
	 * We always skip the first capacity descriptor.  That's the current
	 * capacity.  We are interested in the remaining descriptors, the
	 * formattable capacities.
	 */
	for (i = 1; i < desc_cnt; i++) {
		unsigned int desc_start = 4 + i*8;

		if (u_index >= u_array_size)
			break;	/* User-supplied buffer too small */

		blocks = be32_to_cpu(*(u32 *)&pc.buf[desc_start]);
		length = be16_to_cpu(*(u16 *)&pc.buf[desc_start + 6]);

		if (put_user(blocks, argp))
			return(-EFAULT);
		++argp;

		if (put_user(length, argp))
			return (-EFAULT);
		++argp;

		++u_index;
	}

	if (put_user(u_index, arg))
		return (-EFAULT);
	return (0);
}

/*
 * Get ATAPI_FORMAT_UNIT progress indication.
 *
 * Userland gives a pointer to an int.  The int is set to a progress
 * indicator 0-65536, with 65536=100%.
 *
 * If the drive does not support format progress indication, we just check
 * the dsc bit, and return either 0 or 65536.
 */

static int idefloppy_get_format_progress(ide_drive_t *drive, int __user *arg)
{
	idefloppy_floppy_t *floppy = drive->driver_data;
	struct ide_atapi_pc pc;
	int progress_indication = 0x10000;

	if (floppy->srfp) {
		idefloppy_create_request_sense_cmd(&pc);
		if (idefloppy_queue_pc_tail(drive, &pc))
			return (-EIO);

		if (floppy->sense_key == 2 &&
		    floppy->asc == 4 &&
		    floppy->ascq == 4)
			progress_indication = floppy->progress_indication;

		/* Else assume format_unit has finished, and we're at 0x10000 */
	} else {
		unsigned long flags;
		u8 stat;

		local_irq_save(flags);
		stat = ide_read_status(drive);
		local_irq_restore(flags);

		progress_indication = ((stat & SEEK_STAT) == 0) ? 0 : 0x10000;
	}
	if (put_user(progress_indication, arg))
		return (-EFAULT);

	return (0);
}

static sector_t idefloppy_capacity(ide_drive_t *drive)
{
	idefloppy_floppy_t *floppy = drive->driver_data;
	unsigned long capacity = floppy->blocks * floppy->bs_factor;

	return capacity;
}

/*
 * Check whether we can support a drive, based on the ATAPI IDENTIFY command
 * results.
 */
static int idefloppy_identify_device(ide_drive_t *drive, struct hd_driveid *id)
{
	u8 gcw[2];
	u8 device_type, protocol, removable, drq_type, packet_size;

	*((u16 *) &gcw) = id->config;

	device_type =  gcw[1] & 0x1F;
	removable   = (gcw[0] & 0x80) >> 7;
	protocol    = (gcw[1] & 0xC0) >> 6;
	drq_type    = (gcw[0] & 0x60) >> 5;
	packet_size =  gcw[0] & 0x03;

#ifdef CONFIG_PPC
	/* kludge for Apple PowerBook internal zip */
	if (device_type == 5 &&
	    !strstr(id->model, "CD-ROM") && strstr(id->model, "ZIP"))
		device_type = 0;
#endif

	if (protocol != 2)
		printk(KERN_ERR "ide-floppy: Protocol (0x%02x) is not ATAPI\n",
			protocol);
	else if (device_type != 0)
		printk(KERN_ERR "ide-floppy: Device type (0x%02x) is not set "
				"to floppy\n", device_type);
	else if (!removable)
		printk(KERN_ERR "ide-floppy: The removable flag is not set\n");
	else if (drq_type == 3)
		printk(KERN_ERR "ide-floppy: Sorry, DRQ type (0x%02x) not "
				"supported\n", drq_type);
	else if (packet_size != 0)
		printk(KERN_ERR "ide-floppy: Packet size (0x%02x) is not 12 "
				"bytes\n", packet_size);
	else
		return 1;
	return 0;
}

#ifdef CONFIG_IDE_PROC_FS
static void idefloppy_add_settings(ide_drive_t *drive)
{
	idefloppy_floppy_t *floppy = drive->driver_data;

	ide_add_setting(drive, "bios_cyl", SETTING_RW, TYPE_INT, 0, 1023, 1, 1,
			&drive->bios_cyl, NULL);
	ide_add_setting(drive, "bios_head", SETTING_RW, TYPE_BYTE, 0, 255, 1, 1,
			&drive->bios_head, NULL);
	ide_add_setting(drive, "bios_sect", SETTING_RW,	TYPE_BYTE, 0,  63, 1, 1,
			&drive->bios_sect, NULL);
	ide_add_setting(drive, "ticks",	   SETTING_RW, TYPE_BYTE, 0, 255, 1, 1,
			&floppy->ticks,	 NULL);
}
#else
static inline void idefloppy_add_settings(ide_drive_t *drive) { ; }
#endif

static void idefloppy_setup(ide_drive_t *drive, idefloppy_floppy_t *floppy)
{
	u8 gcw[2];

	*((u16 *) &gcw) = drive->id->config;
	floppy->pc = floppy->pc_stack;

	if (((gcw[0] & 0x60) >> 5) == 1)
		floppy->flags |= IDEFLOPPY_FLAG_DRQ_INTERRUPT;
	/*
	 * We used to check revisions here. At this point however I'm giving up.
	 * Just assume they are all broken, its easier.
	 *
	 * The actual reason for the workarounds was likely a driver bug after
	 * all rather than a firmware bug, and the workaround below used to hide
	 * it. It should be fixed as of version 1.9, but to be on the safe side
	 * we'll leave the limitation below for the 2.2.x tree.
	 */
	if (!strncmp(drive->id->model, "IOMEGA ZIP 100 ATAPI", 20)) {
		floppy->flags |= IDEFLOPPY_FLAG_ZIP_DRIVE;
		/* This value will be visible in the /proc/ide/hdx/settings */
		floppy->ticks = IDEFLOPPY_TICKS_DELAY;
		blk_queue_max_sectors(drive->queue, 64);
	}

	/*
	 * Guess what? The IOMEGA Clik! drive also needs the above fix. It makes
	 * nasty clicking noises without it, so please don't remove this.
	 */
	if (strncmp(drive->id->model, "IOMEGA Clik!", 11) == 0) {
		blk_queue_max_sectors(drive->queue, 64);
		floppy->flags |= IDEFLOPPY_FLAG_CLIK_DRIVE;
	}

	(void) ide_floppy_get_capacity(drive);
	idefloppy_add_settings(drive);
}

static void ide_floppy_remove(ide_drive_t *drive)
{
	idefloppy_floppy_t *floppy = drive->driver_data;
	struct gendisk *g = floppy->disk;

	ide_proc_unregister_driver(drive, floppy->driver);

	del_gendisk(g);

	ide_floppy_put(floppy);
}

static void idefloppy_cleanup_obj(struct kref *kref)
{
	struct ide_floppy_obj *floppy = to_ide_floppy(kref);
	ide_drive_t *drive = floppy->drive;
	struct gendisk *g = floppy->disk;

	drive->driver_data = NULL;
	g->private_data = NULL;
	put_disk(g);
	kfree(floppy);
}

#ifdef CONFIG_IDE_PROC_FS
static int proc_idefloppy_read_capacity(char *page, char **start, off_t off,
		int count, int *eof, void *data)
{
	ide_drive_t*drive = (ide_drive_t *)data;
	int len;

	len = sprintf(page, "%llu\n", (long long)idefloppy_capacity(drive));
	PROC_IDE_READ_RETURN(page, start, off, count, eof, len);
}

static ide_proc_entry_t idefloppy_proc[] = {
	{ "capacity",	S_IFREG|S_IRUGO, proc_idefloppy_read_capacity,	NULL },
	{ "geometry",	S_IFREG|S_IRUGO, proc_ide_read_geometry,	NULL },
	{ NULL, 0, NULL, NULL }
};
#endif	/* CONFIG_IDE_PROC_FS */

static int ide_floppy_probe(ide_drive_t *);

static ide_driver_t idefloppy_driver = {
	.gen_driver = {
		.owner		= THIS_MODULE,
		.name		= "ide-floppy",
		.bus		= &ide_bus_type,
	},
	.probe			= ide_floppy_probe,
	.remove			= ide_floppy_remove,
	.version		= IDEFLOPPY_VERSION,
	.media			= ide_floppy,
	.supports_dsc_overlap	= 0,
	.do_request		= idefloppy_do_request,
	.end_request		= idefloppy_end_request,
	.error			= __ide_error,
	.abort			= __ide_abort,
#ifdef CONFIG_IDE_PROC_FS
	.proc			= idefloppy_proc,
#endif
};

static int idefloppy_open(struct inode *inode, struct file *filp)
{
	struct gendisk *disk = inode->i_bdev->bd_disk;
	struct ide_floppy_obj *floppy;
	ide_drive_t *drive;
	struct ide_atapi_pc pc;
	int ret = 0;

	debug_log("Reached %s\n", __func__);

	floppy = ide_floppy_get(disk);
	if (!floppy)
		return -ENXIO;

	drive = floppy->drive;

	floppy->openers++;

	if (floppy->openers == 1) {
		floppy->flags &= ~IDEFLOPPY_FLAG_FORMAT_IN_PROGRESS;
		/* Just in case */

		idefloppy_create_test_unit_ready_cmd(&pc);
		if (idefloppy_queue_pc_tail(drive, &pc)) {
			idefloppy_create_start_stop_cmd(&pc, 1);
			(void) idefloppy_queue_pc_tail(drive, &pc);
		}

		if (ide_floppy_get_capacity(drive)
		   && (filp->f_flags & O_NDELAY) == 0
		    /*
		     * Allow O_NDELAY to open a drive without a disk, or with an
		     * unreadable disk, so that we can get the format capacity
		     * of the drive or begin the format - Sam
		     */
		    ) {
			ret = -EIO;
			goto out_put_floppy;
		}

		if (floppy->wp && (filp->f_mode & 2)) {
			ret = -EROFS;
			goto out_put_floppy;
		}
		floppy->flags |= IDEFLOPPY_FLAG_MEDIA_CHANGED;
		/* IOMEGA Clik! drives do not support lock/unlock commands */
		if (!(floppy->flags & IDEFLOPPY_FLAG_CLIK_DRIVE)) {
			idefloppy_create_prevent_cmd(&pc, 1);
			(void) idefloppy_queue_pc_tail(drive, &pc);
		}
		check_disk_change(inode->i_bdev);
	} else if (floppy->flags & IDEFLOPPY_FLAG_FORMAT_IN_PROGRESS) {
		ret = -EBUSY;
		goto out_put_floppy;
	}
	return 0;

out_put_floppy:
	floppy->openers--;
	ide_floppy_put(floppy);
	return ret;
}

static int idefloppy_release(struct inode *inode, struct file *filp)
{
	struct gendisk *disk = inode->i_bdev->bd_disk;
	struct ide_floppy_obj *floppy = ide_floppy_g(disk);
	ide_drive_t *drive = floppy->drive;
	struct ide_atapi_pc pc;

	debug_log("Reached %s\n", __func__);

	if (floppy->openers == 1) {
		/* IOMEGA Clik! drives do not support lock/unlock commands */
		if (!(floppy->flags & IDEFLOPPY_FLAG_CLIK_DRIVE)) {
			idefloppy_create_prevent_cmd(&pc, 0);
			(void) idefloppy_queue_pc_tail(drive, &pc);
		}

		floppy->flags &= ~IDEFLOPPY_FLAG_FORMAT_IN_PROGRESS;
	}

	floppy->openers--;

	ide_floppy_put(floppy);

	return 0;
}

static int idefloppy_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	struct ide_floppy_obj *floppy = ide_floppy_g(bdev->bd_disk);
	ide_drive_t *drive = floppy->drive;

	geo->heads = drive->bios_head;
	geo->sectors = drive->bios_sect;
	geo->cylinders = (u16)drive->bios_cyl; /* truncate */
	return 0;
}

static int ide_floppy_lockdoor(idefloppy_floppy_t *floppy,
		struct ide_atapi_pc *pc, unsigned long arg, unsigned int cmd)
{
	if (floppy->openers > 1)
		return -EBUSY;

	/* The IOMEGA Clik! Drive doesn't support this command -
	 * no room for an eject mechanism */
	if (!(floppy->flags & IDEFLOPPY_FLAG_CLIK_DRIVE)) {
		int prevent = arg ? 1 : 0;

		if (cmd == CDROMEJECT)
			prevent = 0;

		idefloppy_create_prevent_cmd(pc, prevent);
		(void) idefloppy_queue_pc_tail(floppy->drive, pc);
	}

	if (cmd == CDROMEJECT) {
		idefloppy_create_start_stop_cmd(pc, 2);
		(void) idefloppy_queue_pc_tail(floppy->drive, pc);
	}

	return 0;
}

static int ide_floppy_format_unit(idefloppy_floppy_t *floppy,
				  int __user *arg)
{
	int blocks, length, flags, err = 0;
	struct ide_atapi_pc pc;

	if (floppy->openers > 1) {
		/* Don't format if someone is using the disk */
		floppy->flags &= ~IDEFLOPPY_FLAG_FORMAT_IN_PROGRESS;
		return -EBUSY;
	}

	floppy->flags |= IDEFLOPPY_FLAG_FORMAT_IN_PROGRESS;

	/*
	 * Send ATAPI_FORMAT_UNIT to the drive.
	 *
	 * Userland gives us the following structure:
	 *
	 * struct idefloppy_format_command {
	 *        int nblocks;
	 *        int blocksize;
	 *        int flags;
	 *        } ;
	 *
	 * flags is a bitmask, currently, the only defined flag is:
	 *
	 *        0x01 - verify media after format.
	 */
	if (get_user(blocks, arg) ||
			get_user(length, arg+1) ||
			get_user(flags, arg+2)) {
		err = -EFAULT;
		goto out;
	}

	(void) idefloppy_get_sfrp_bit(floppy->drive);
	idefloppy_create_format_unit_cmd(&pc, blocks, length, flags);

	if (idefloppy_queue_pc_tail(floppy->drive, &pc))
		err = -EIO;

out:
	if (err)
		floppy->flags &= ~IDEFLOPPY_FLAG_FORMAT_IN_PROGRESS;
	return err;
}


static int idefloppy_ioctl(struct inode *inode, struct file *file,
			unsigned int cmd, unsigned long arg)
{
	struct block_device *bdev = inode->i_bdev;
	struct ide_floppy_obj *floppy = ide_floppy_g(bdev->bd_disk);
	ide_drive_t *drive = floppy->drive;
	struct ide_atapi_pc pc;
	void __user *argp = (void __user *)arg;
	int err;

	switch (cmd) {
	case CDROMEJECT:
		/* fall through */
	case CDROM_LOCKDOOR:
		return ide_floppy_lockdoor(floppy, &pc, arg, cmd);
	case IDEFLOPPY_IOCTL_FORMAT_SUPPORTED:
		return 0;
	case IDEFLOPPY_IOCTL_FORMAT_GET_CAPACITY:
		return ide_floppy_get_format_capacities(drive, argp);
	case IDEFLOPPY_IOCTL_FORMAT_START:
		if (!(file->f_mode & 2))
			return -EPERM;

		return ide_floppy_format_unit(floppy, (int __user *)arg);
	case IDEFLOPPY_IOCTL_FORMAT_GET_PROGRESS:
		return idefloppy_get_format_progress(drive, argp);
	}

	/*
	 * skip SCSI_IOCTL_SEND_COMMAND (deprecated)
	 * and CDROM_SEND_PACKET (legacy) ioctls
	 */
	if (cmd != CDROM_SEND_PACKET && cmd != SCSI_IOCTL_SEND_COMMAND)
		err = scsi_cmd_ioctl(file, bdev->bd_disk->queue,
					bdev->bd_disk, cmd, argp);
	else
		err = -ENOTTY;

	if (err == -ENOTTY)
		err = generic_ide_ioctl(drive, file, bdev, cmd, arg);

	return err;
}

static int idefloppy_media_changed(struct gendisk *disk)
{
	struct ide_floppy_obj *floppy = ide_floppy_g(disk);
	ide_drive_t *drive = floppy->drive;
	int ret;

	/* do not scan partitions twice if this is a removable device */
	if (drive->attach) {
		drive->attach = 0;
		return 0;
	}
	ret = !!(floppy->flags & IDEFLOPPY_FLAG_MEDIA_CHANGED);
	floppy->flags &= ~IDEFLOPPY_FLAG_MEDIA_CHANGED;
	return ret;
}

static int idefloppy_revalidate_disk(struct gendisk *disk)
{
	struct ide_floppy_obj *floppy = ide_floppy_g(disk);
	set_capacity(disk, idefloppy_capacity(floppy->drive));
	return 0;
}

static struct block_device_operations idefloppy_ops = {
	.owner			= THIS_MODULE,
	.open			= idefloppy_open,
	.release		= idefloppy_release,
	.ioctl			= idefloppy_ioctl,
	.getgeo			= idefloppy_getgeo,
	.media_changed		= idefloppy_media_changed,
	.revalidate_disk	= idefloppy_revalidate_disk
};

static int ide_floppy_probe(ide_drive_t *drive)
{
	idefloppy_floppy_t *floppy;
	struct gendisk *g;

	if (!strstr("ide-floppy", drive->driver_req))
		goto failed;
	if (!drive->present)
		goto failed;
	if (drive->media != ide_floppy)
		goto failed;
	if (!idefloppy_identify_device(drive, drive->id)) {
		printk(KERN_ERR "ide-floppy: %s: not supported by this version"
				" of ide-floppy\n", drive->name);
		goto failed;
	}
	if (drive->scsi) {
		printk(KERN_INFO "ide-floppy: passing drive %s to ide-scsi"
				" emulation.\n", drive->name);
		goto failed;
	}
	floppy = kzalloc(sizeof(idefloppy_floppy_t), GFP_KERNEL);
	if (!floppy) {
		printk(KERN_ERR "ide-floppy: %s: Can't allocate a floppy"
				" structure\n", drive->name);
		goto failed;
	}

	g = alloc_disk(1 << PARTN_BITS);
	if (!g)
		goto out_free_floppy;

	ide_init_disk(g, drive);

	ide_proc_register_driver(drive, &idefloppy_driver);

	kref_init(&floppy->kref);

	floppy->drive = drive;
	floppy->driver = &idefloppy_driver;
	floppy->disk = g;

	g->private_data = &floppy->driver;

	drive->driver_data = floppy;

	idefloppy_setup(drive, floppy);

	g->minors = 1 << PARTN_BITS;
	g->driverfs_dev = &drive->gendev;
	g->flags = drive->removable ? GENHD_FL_REMOVABLE : 0;
	g->fops = &idefloppy_ops;
	drive->attach = 1;
	add_disk(g);
	return 0;

out_free_floppy:
	kfree(floppy);
failed:
	return -ENODEV;
}

static void __exit idefloppy_exit(void)
{
	driver_unregister(&idefloppy_driver.gen_driver);
}

static int __init idefloppy_init(void)
{
	printk("ide-floppy driver " IDEFLOPPY_VERSION "\n");
	return driver_register(&idefloppy_driver.gen_driver);
}

MODULE_ALIAS("ide:*m-floppy*");
module_init(idefloppy_init);
module_exit(idefloppy_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ATAPI FLOPPY Driver");

