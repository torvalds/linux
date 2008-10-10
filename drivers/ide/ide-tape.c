/*
 * IDE ATAPI streaming tape driver.
 *
 * Copyright (C) 1995-1999  Gadi Oxman <gadio@netvision.net.il>
 * Copyright (C) 2003-2005  Bartlomiej Zolnierkiewicz
 *
 * This driver was constructed as a student project in the software laboratory
 * of the faculty of electrical engineering in the Technion - Israel's
 * Institute Of Technology, with the guide of Avner Lottem and Dr. Ilana David.
 *
 * It is hereby placed under the terms of the GNU general public license.
 * (See linux/COPYING).
 *
 * For a historical changelog see
 * Documentation/ide/ChangeLog.ide-tape.1995-2002
 */

#define DRV_NAME "ide-tape"

#define IDETAPE_VERSION "1.20"

#include <linux/module.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/major.h>
#include <linux/errno.h>
#include <linux/genhd.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/ide.h>
#include <linux/smp_lock.h>
#include <linux/completion.h>
#include <linux/bitops.h>
#include <linux/mutex.h>
#include <scsi/scsi.h>

#include <asm/byteorder.h>
#include <linux/irq.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <asm/unaligned.h>
#include <linux/mtio.h>

enum {
	/* output errors only */
	DBG_ERR =		(1 << 0),
	/* output all sense key/asc */
	DBG_SENSE =		(1 << 1),
	/* info regarding all chrdev-related procedures */
	DBG_CHRDEV =		(1 << 2),
	/* all remaining procedures */
	DBG_PROCS =		(1 << 3),
	/* buffer alloc info (pc_stack) */
	DBG_PC_STACK =	(1 << 4),
};

/* define to see debug info */
#define IDETAPE_DEBUG_LOG		0

#if IDETAPE_DEBUG_LOG
#define debug_log(lvl, fmt, args...)			\
{							\
	if (tape->debug_mask & lvl)			\
	printk(KERN_INFO "ide-tape: " fmt, ## args);	\
}
#else
#define debug_log(lvl, fmt, args...) do {} while (0)
#endif

/**************************** Tunable parameters *****************************/
/*
 * After each failed packet command we issue a request sense command and retry
 * the packet command IDETAPE_MAX_PC_RETRIES times.
 *
 * Setting IDETAPE_MAX_PC_RETRIES to 0 will disable retries.
 */
#define IDETAPE_MAX_PC_RETRIES		3

/*
 * With each packet command, we allocate a buffer of IDETAPE_PC_BUFFER_SIZE
 * bytes. This is used for several packet commands (Not for READ/WRITE commands)
 */
#define IDETAPE_PC_BUFFER_SIZE		256

/*
 * In various places in the driver, we need to allocate storage for packet
 * commands, which will remain valid while we leave the driver to wait for
 * an interrupt or a timeout event.
 */
#define IDETAPE_PC_STACK		(10 + IDETAPE_MAX_PC_RETRIES)

/*
 * Some drives (for example, Seagate STT3401A Travan) require a very long
 * timeout, because they don't return an interrupt or clear their busy bit
 * until after the command completes (even retension commands).
 */
#define IDETAPE_WAIT_CMD		(900*HZ)

/*
 * The following parameter is used to select the point in the internal tape fifo
 * in which we will start to refill the buffer. Decreasing the following
 * parameter will improve the system's latency and interactive response, while
 * using a high value might improve system throughput.
 */
#define IDETAPE_FIFO_THRESHOLD		2

/*
 * DSC polling parameters.
 *
 * Polling for DSC (a single bit in the status register) is a very important
 * function in ide-tape. There are two cases in which we poll for DSC:
 *
 * 1. Before a read/write packet command, to ensure that we can transfer data
 * from/to the tape's data buffers, without causing an actual media access.
 * In case the tape is not ready yet, we take out our request from the device
 * request queue, so that ide.c could service requests from the other device
 * on the same interface in the meantime.
 *
 * 2. After the successful initialization of a "media access packet command",
 * which is a command that can take a long time to complete (the interval can
 * range from several seconds to even an hour). Again, we postpone our request
 * in the middle to free the bus for the other device. The polling frequency
 * here should be lower than the read/write frequency since those media access
 * commands are slow. We start from a "fast" frequency - IDETAPE_DSC_MA_FAST
 * (1 second), and if we don't receive DSC after IDETAPE_DSC_MA_THRESHOLD
 * (5 min), we switch it to a lower frequency - IDETAPE_DSC_MA_SLOW (1 min).
 *
 * We also set a timeout for the timer, in case something goes wrong. The
 * timeout should be longer then the maximum execution time of a tape operation.
 */

/* DSC timings. */
#define IDETAPE_DSC_RW_MIN		5*HZ/100	/* 50 msec */
#define IDETAPE_DSC_RW_MAX		40*HZ/100	/* 400 msec */
#define IDETAPE_DSC_RW_TIMEOUT		2*60*HZ		/* 2 minutes */
#define IDETAPE_DSC_MA_FAST		2*HZ		/* 2 seconds */
#define IDETAPE_DSC_MA_THRESHOLD	5*60*HZ		/* 5 minutes */
#define IDETAPE_DSC_MA_SLOW		30*HZ		/* 30 seconds */
#define IDETAPE_DSC_MA_TIMEOUT		2*60*60*HZ	/* 2 hours */

/*************************** End of tunable parameters ***********************/

/* tape directions */
enum {
	IDETAPE_DIR_NONE  = (1 << 0),
	IDETAPE_DIR_READ  = (1 << 1),
	IDETAPE_DIR_WRITE = (1 << 2),
};

struct idetape_bh {
	u32 b_size;
	atomic_t b_count;
	struct idetape_bh *b_reqnext;
	char *b_data;
};

/* Tape door status */
#define DOOR_UNLOCKED			0
#define DOOR_LOCKED			1
#define DOOR_EXPLICITLY_LOCKED		2

/* Some defines for the SPACE command */
#define IDETAPE_SPACE_OVER_FILEMARK	1
#define IDETAPE_SPACE_TO_EOD		3

/* Some defines for the LOAD UNLOAD command */
#define IDETAPE_LU_LOAD_MASK		1
#define IDETAPE_LU_RETENSION_MASK	2
#define IDETAPE_LU_EOT_MASK		4

/*
 * Special requests for our block device strategy routine.
 *
 * In order to service a character device command, we add special requests to
 * the tail of our block device request queue and wait for their completion.
 */

enum {
	REQ_IDETAPE_PC1		= (1 << 0), /* packet command (first stage) */
	REQ_IDETAPE_PC2		= (1 << 1), /* packet command (second stage) */
	REQ_IDETAPE_READ	= (1 << 2),
	REQ_IDETAPE_WRITE	= (1 << 3),
};

/* Error codes returned in rq->errors to the higher part of the driver. */
#define IDETAPE_ERROR_GENERAL		101
#define IDETAPE_ERROR_FILEMARK		102
#define IDETAPE_ERROR_EOD		103

/* Structures related to the SELECT SENSE / MODE SENSE packet commands. */
#define IDETAPE_BLOCK_DESCRIPTOR	0
#define IDETAPE_CAPABILITIES_PAGE	0x2a

/*
 * Most of our global data which we need to save even as we leave the driver due
 * to an interrupt or a timer event is stored in the struct defined below.
 */
typedef struct ide_tape_obj {
	ide_drive_t	*drive;
	ide_driver_t	*driver;
	struct gendisk	*disk;
	struct kref	kref;

	/*
	 *	Since a typical character device operation requires more
	 *	than one packet command, we provide here enough memory
	 *	for the maximum of interconnected packet commands.
	 *	The packet commands are stored in the circular array pc_stack.
	 *	pc_stack_index points to the last used entry, and warps around
	 *	to the start when we get to the last array entry.
	 *
	 *	pc points to the current processed packet command.
	 *
	 *	failed_pc points to the last failed packet command, or contains
	 *	NULL if we do not need to retry any packet command. This is
	 *	required since an additional packet command is needed before the
	 *	retry, to get detailed information on what went wrong.
	 */
	/* Current packet command */
	struct ide_atapi_pc *pc;
	/* Last failed packet command */
	struct ide_atapi_pc *failed_pc;
	/* Packet command stack */
	struct ide_atapi_pc pc_stack[IDETAPE_PC_STACK];
	/* Next free packet command storage space */
	int pc_stack_index;

	struct request request_sense_rq;

	/*
	 * DSC polling variables.
	 *
	 * While polling for DSC we use postponed_rq to postpone the current
	 * request so that ide.c will be able to service pending requests on the
	 * other device. Note that at most we will have only one DSC (usually
	 * data transfer) request in the device request queue.
	 */
	struct request *postponed_rq;
	/* The time in which we started polling for DSC */
	unsigned long dsc_polling_start;
	/* Timer used to poll for dsc */
	struct timer_list dsc_timer;
	/* Read/Write dsc polling frequency */
	unsigned long best_dsc_rw_freq;
	unsigned long dsc_poll_freq;
	unsigned long dsc_timeout;

	/* Read position information */
	u8 partition;
	/* Current block */
	unsigned int first_frame;

	/* Last error information */
	u8 sense_key, asc, ascq;

	/* Character device operation */
	unsigned int minor;
	/* device name */
	char name[4];
	/* Current character device data transfer direction */
	u8 chrdev_dir;

	/* tape block size, usually 512 or 1024 bytes */
	unsigned short blk_size;
	int user_bs_factor;

	/* Copy of the tape's Capabilities and Mechanical Page */
	u8 caps[20];

	/*
	 * Active data transfer request parameters.
	 *
	 * At most, there is only one ide-tape originated data transfer request
	 * in the device request queue. This allows ide.c to easily service
	 * requests from the other device when we postpone our active request.
	 */

	/* Data buffer size chosen based on the tape's recommendation */
	int buffer_size;
	/* merge buffer */
	struct idetape_bh *merge_bh;
	/* size of the merge buffer */
	int merge_bh_size;
	/* pointer to current buffer head within the merge buffer */
	struct idetape_bh *bh;
	char *b_data;
	int b_count;

	int pages_per_buffer;
	/* Wasted space in each stage */
	int excess_bh_size;

	/* protects the ide-tape queue */
	spinlock_t lock;

	/* Measures average tape speed */
	unsigned long avg_time;
	int avg_size;
	int avg_speed;

	/* the door is currently locked */
	int door_locked;
	/* the tape hardware is write protected */
	char drv_write_prot;
	/* the tape is write protected (hardware or opened as read-only) */
	char write_prot;

	u32 debug_mask;
} idetape_tape_t;

static DEFINE_MUTEX(idetape_ref_mutex);

static struct class *idetape_sysfs_class;

#define to_ide_tape(obj) container_of(obj, struct ide_tape_obj, kref)

#define ide_tape_g(disk) \
	container_of((disk)->private_data, struct ide_tape_obj, driver)

static void ide_tape_release(struct kref *);

static struct ide_tape_obj *ide_tape_get(struct gendisk *disk)
{
	struct ide_tape_obj *tape = NULL;

	mutex_lock(&idetape_ref_mutex);
	tape = ide_tape_g(disk);
	if (tape) {
		if (ide_device_get(tape->drive))
			tape = NULL;
		else
			kref_get(&tape->kref);
	}
	mutex_unlock(&idetape_ref_mutex);
	return tape;
}

static void ide_tape_put(struct ide_tape_obj *tape)
{
	ide_drive_t *drive = tape->drive;

	mutex_lock(&idetape_ref_mutex);
	kref_put(&tape->kref, ide_tape_release);
	ide_device_put(drive);
	mutex_unlock(&idetape_ref_mutex);
}

/*
 * The variables below are used for the character device interface. Additional
 * state variables are defined in our ide_drive_t structure.
 */
static struct ide_tape_obj *idetape_devs[MAX_HWIFS * MAX_DRIVES];

#define ide_tape_f(file) ((file)->private_data)

static struct ide_tape_obj *ide_tape_chrdev_get(unsigned int i)
{
	struct ide_tape_obj *tape = NULL;

	mutex_lock(&idetape_ref_mutex);
	tape = idetape_devs[i];
	if (tape)
		kref_get(&tape->kref);
	mutex_unlock(&idetape_ref_mutex);
	return tape;
}

static void idetape_input_buffers(ide_drive_t *drive, struct ide_atapi_pc *pc,
				  unsigned int bcount)
{
	struct idetape_bh *bh = pc->bh;
	int count;

	while (bcount) {
		if (bh == NULL) {
			printk(KERN_ERR "ide-tape: bh == NULL in "
				"idetape_input_buffers\n");
			ide_pad_transfer(drive, 0, bcount);
			return;
		}
		count = min(
			(unsigned int)(bh->b_size - atomic_read(&bh->b_count)),
			bcount);
		drive->hwif->tp_ops->input_data(drive, NULL, bh->b_data +
					atomic_read(&bh->b_count), count);
		bcount -= count;
		atomic_add(count, &bh->b_count);
		if (atomic_read(&bh->b_count) == bh->b_size) {
			bh = bh->b_reqnext;
			if (bh)
				atomic_set(&bh->b_count, 0);
		}
	}
	pc->bh = bh;
}

static void idetape_output_buffers(ide_drive_t *drive, struct ide_atapi_pc *pc,
				   unsigned int bcount)
{
	struct idetape_bh *bh = pc->bh;
	int count;

	while (bcount) {
		if (bh == NULL) {
			printk(KERN_ERR "ide-tape: bh == NULL in %s\n",
					__func__);
			return;
		}
		count = min((unsigned int)pc->b_count, (unsigned int)bcount);
		drive->hwif->tp_ops->output_data(drive, NULL, pc->b_data, count);
		bcount -= count;
		pc->b_data += count;
		pc->b_count -= count;
		if (!pc->b_count) {
			bh = bh->b_reqnext;
			pc->bh = bh;
			if (bh) {
				pc->b_data = bh->b_data;
				pc->b_count = atomic_read(&bh->b_count);
			}
		}
	}
}

static void idetape_update_buffers(ide_drive_t *drive, struct ide_atapi_pc *pc)
{
	struct idetape_bh *bh = pc->bh;
	int count;
	unsigned int bcount = pc->xferred;

	if (pc->flags & PC_FLAG_WRITING)
		return;
	while (bcount) {
		if (bh == NULL) {
			printk(KERN_ERR "ide-tape: bh == NULL in %s\n",
					__func__);
			return;
		}
		count = min((unsigned int)bh->b_size, (unsigned int)bcount);
		atomic_set(&bh->b_count, count);
		if (atomic_read(&bh->b_count) == bh->b_size)
			bh = bh->b_reqnext;
		bcount -= count;
	}
	pc->bh = bh;
}

/*
 *	idetape_next_pc_storage returns a pointer to a place in which we can
 *	safely store a packet command, even though we intend to leave the
 *	driver. A storage space for a maximum of IDETAPE_PC_STACK packet
 *	commands is allocated at initialization time.
 */
static struct ide_atapi_pc *idetape_next_pc_storage(ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;

	debug_log(DBG_PC_STACK, "pc_stack_index=%d\n", tape->pc_stack_index);

	if (tape->pc_stack_index == IDETAPE_PC_STACK)
		tape->pc_stack_index = 0;
	return (&tape->pc_stack[tape->pc_stack_index++]);
}

/*
 * called on each failed packet command retry to analyze the request sense. We
 * currently do not utilize this information.
 */
static void idetape_analyze_error(ide_drive_t *drive, u8 *sense)
{
	idetape_tape_t *tape = drive->driver_data;
	struct ide_atapi_pc *pc = tape->failed_pc;

	tape->sense_key = sense[2] & 0xF;
	tape->asc       = sense[12];
	tape->ascq      = sense[13];

	debug_log(DBG_ERR, "pc = %x, sense key = %x, asc = %x, ascq = %x\n",
		 pc->c[0], tape->sense_key, tape->asc, tape->ascq);

	/* Correct pc->xferred by asking the tape.	 */
	if (pc->flags & PC_FLAG_DMA_ERROR) {
		pc->xferred = pc->req_xfer -
			tape->blk_size *
			get_unaligned_be32(&sense[3]);
		idetape_update_buffers(drive, pc);
	}

	/*
	 * If error was the result of a zero-length read or write command,
	 * with sense key=5, asc=0x22, ascq=0, let it slide.  Some drives
	 * (i.e. Seagate STT3401A Travan) don't support 0-length read/writes.
	 */
	if ((pc->c[0] == READ_6 || pc->c[0] == WRITE_6)
	    /* length == 0 */
	    && pc->c[4] == 0 && pc->c[3] == 0 && pc->c[2] == 0) {
		if (tape->sense_key == 5) {
			/* don't report an error, everything's ok */
			pc->error = 0;
			/* don't retry read/write */
			pc->flags |= PC_FLAG_ABORT;
		}
	}
	if (pc->c[0] == READ_6 && (sense[2] & 0x80)) {
		pc->error = IDETAPE_ERROR_FILEMARK;
		pc->flags |= PC_FLAG_ABORT;
	}
	if (pc->c[0] == WRITE_6) {
		if ((sense[2] & 0x40) || (tape->sense_key == 0xd
		     && tape->asc == 0x0 && tape->ascq == 0x2)) {
			pc->error = IDETAPE_ERROR_EOD;
			pc->flags |= PC_FLAG_ABORT;
		}
	}
	if (pc->c[0] == READ_6 || pc->c[0] == WRITE_6) {
		if (tape->sense_key == 8) {
			pc->error = IDETAPE_ERROR_EOD;
			pc->flags |= PC_FLAG_ABORT;
		}
		if (!(pc->flags & PC_FLAG_ABORT) &&
		    pc->xferred)
			pc->retries = IDETAPE_MAX_PC_RETRIES + 1;
	}
}

/* Free data buffers completely. */
static void ide_tape_kfree_buffer(idetape_tape_t *tape)
{
	struct idetape_bh *prev_bh, *bh = tape->merge_bh;

	while (bh) {
		u32 size = bh->b_size;

		while (size) {
			unsigned int order = fls(size >> PAGE_SHIFT)-1;

			if (bh->b_data)
				free_pages((unsigned long)bh->b_data, order);

			size &= (order-1);
			bh->b_data += (1 << order) * PAGE_SIZE;
		}
		prev_bh = bh;
		bh = bh->b_reqnext;
		kfree(prev_bh);
	}
}

static int idetape_end_request(ide_drive_t *drive, int uptodate, int nr_sects)
{
	struct request *rq = HWGROUP(drive)->rq;
	idetape_tape_t *tape = drive->driver_data;
	unsigned long flags;
	int error;

	debug_log(DBG_PROCS, "Enter %s\n", __func__);

	switch (uptodate) {
	case 0:	error = IDETAPE_ERROR_GENERAL; break;
	case 1: error = 0; break;
	default: error = uptodate;
	}
	rq->errors = error;
	if (error)
		tape->failed_pc = NULL;

	if (!blk_special_request(rq)) {
		ide_end_request(drive, uptodate, nr_sects);
		return 0;
	}

	spin_lock_irqsave(&tape->lock, flags);

	ide_end_drive_cmd(drive, 0, 0);

	spin_unlock_irqrestore(&tape->lock, flags);
	return 0;
}

static void ide_tape_callback(ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;
	struct ide_atapi_pc *pc = tape->pc;
	int uptodate = pc->error ? 0 : 1;

	debug_log(DBG_PROCS, "Enter %s\n", __func__);

	if (tape->failed_pc == pc)
		tape->failed_pc = NULL;

	if (pc->c[0] == REQUEST_SENSE) {
		if (uptodate)
			idetape_analyze_error(drive, pc->buf);
		else
			printk(KERN_ERR "ide-tape: Error in REQUEST SENSE "
					"itself - Aborting request!\n");
	} else if (pc->c[0] == READ_6 || pc->c[0] == WRITE_6) {
		struct request *rq = drive->hwif->hwgroup->rq;
		int blocks = pc->xferred / tape->blk_size;

		tape->avg_size += blocks * tape->blk_size;

		if (time_after_eq(jiffies, tape->avg_time + HZ)) {
			tape->avg_speed = tape->avg_size * HZ /
				(jiffies - tape->avg_time) / 1024;
			tape->avg_size = 0;
			tape->avg_time = jiffies;
		}

		tape->first_frame += blocks;
		rq->current_nr_sectors -= blocks;

		if (pc->error)
			uptodate = pc->error;
	} else if (pc->c[0] == READ_POSITION && uptodate) {
		u8 *readpos = tape->pc->buf;

		debug_log(DBG_SENSE, "BOP - %s\n",
				(readpos[0] & 0x80) ? "Yes" : "No");
		debug_log(DBG_SENSE, "EOP - %s\n",
				(readpos[0] & 0x40) ? "Yes" : "No");

		if (readpos[0] & 0x4) {
			printk(KERN_INFO "ide-tape: Block location is unknown"
					 "to the tape\n");
			clear_bit(IDE_AFLAG_ADDRESS_VALID, &drive->atapi_flags);
			uptodate = 0;
		} else {
			debug_log(DBG_SENSE, "Block Location - %u\n",
					be32_to_cpup((__be32 *)&readpos[4]));

			tape->partition = readpos[1];
			tape->first_frame = be32_to_cpup((__be32 *)&readpos[4]);
			set_bit(IDE_AFLAG_ADDRESS_VALID, &drive->atapi_flags);
		}
	}

	idetape_end_request(drive, uptodate, 0);
}

static void idetape_init_pc(struct ide_atapi_pc *pc)
{
	memset(pc->c, 0, 12);
	pc->retries = 0;
	pc->flags = 0;
	pc->req_xfer = 0;
	pc->buf = pc->pc_buf;
	pc->buf_size = IDETAPE_PC_BUFFER_SIZE;
	pc->bh = NULL;
	pc->b_data = NULL;
}

static void idetape_create_request_sense_cmd(struct ide_atapi_pc *pc)
{
	idetape_init_pc(pc);
	pc->c[0] = REQUEST_SENSE;
	pc->c[4] = 20;
	pc->req_xfer = 20;
}

/*
 * Generate a new packet command request in front of the request queue, before
 * the current request, so that it will be processed immediately, on the next
 * pass through the driver.
 */
static void idetape_queue_pc_head(ide_drive_t *drive, struct ide_atapi_pc *pc,
				  struct request *rq)
{
	struct ide_tape_obj *tape = drive->driver_data;

	blk_rq_init(NULL, rq);
	rq->cmd_type = REQ_TYPE_SPECIAL;
	rq->cmd_flags |= REQ_PREEMPT;
	rq->buffer = (char *) pc;
	rq->rq_disk = tape->disk;
	memcpy(rq->cmd, pc->c, 12);
	rq->cmd[13] = REQ_IDETAPE_PC1;
	ide_do_drive_cmd(drive, rq);
}

/*
 *	idetape_retry_pc is called when an error was detected during the
 *	last packet command. We queue a request sense packet command in
 *	the head of the request list.
 */
static void idetape_retry_pc(ide_drive_t *drive)
{
	struct ide_tape_obj *tape = drive->driver_data;
	struct request *rq = &tape->request_sense_rq;
	struct ide_atapi_pc *pc;

	(void)ide_read_error(drive);
	pc = idetape_next_pc_storage(drive);
	idetape_create_request_sense_cmd(pc);
	set_bit(IDE_AFLAG_IGNORE_DSC, &drive->atapi_flags);
	idetape_queue_pc_head(drive, pc, rq);
}

/*
 * Postpone the current request so that ide.c will be able to service requests
 * from another device on the same hwgroup while we are polling for DSC.
 */
static void idetape_postpone_request(ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;

	debug_log(DBG_PROCS, "Enter %s\n", __func__);

	tape->postponed_rq = HWGROUP(drive)->rq;
	ide_stall_queue(drive, tape->dsc_poll_freq);
}

static void ide_tape_handle_dsc(ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;

	/* Media access command */
	tape->dsc_polling_start = jiffies;
	tape->dsc_poll_freq = IDETAPE_DSC_MA_FAST;
	tape->dsc_timeout = jiffies + IDETAPE_DSC_MA_TIMEOUT;
	/* Allow ide.c to handle other requests */
	idetape_postpone_request(drive);
}

static void ide_tape_io_buffers(ide_drive_t *drive, struct ide_atapi_pc *pc,
				unsigned int bcount, int write)
{
	if (write)
		idetape_output_buffers(drive, pc, bcount);
	else
		idetape_input_buffers(drive, pc, bcount);
}

/*
 * This is the usual interrupt handler which will be called during a packet
 * command. We will transfer some of the data (as requested by the drive) and
 * will re-point interrupt handler to us. When data transfer is finished, we
 * will act according to the algorithm described before
 * idetape_issue_pc.
 */
static ide_startstop_t idetape_pc_intr(ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;

	return ide_pc_intr(drive, tape->pc, idetape_pc_intr, IDETAPE_WAIT_CMD,
			   NULL, idetape_update_buffers, idetape_retry_pc,
			   ide_tape_handle_dsc, ide_tape_io_buffers);
}

/*
 * Packet Command Interface
 *
 * The current Packet Command is available in tape->pc, and will not change
 * until we finish handling it. Each packet command is associated with a
 * callback function that will be called when the command is finished.
 *
 * The handling will be done in three stages:
 *
 * 1. idetape_issue_pc will send the packet command to the drive, and will set
 * the interrupt handler to idetape_pc_intr.
 *
 * 2. On each interrupt, idetape_pc_intr will be called. This step will be
 * repeated until the device signals us that no more interrupts will be issued.
 *
 * 3. ATAPI Tape media access commands have immediate status with a delayed
 * process. In case of a successful initiation of a media access packet command,
 * the DSC bit will be set when the actual execution of the command is finished.
 * Since the tape drive will not issue an interrupt, we have to poll for this
 * event. In this case, we define the request as "low priority request" by
 * setting rq_status to IDETAPE_RQ_POSTPONED, set a timer to poll for DSC and
 * exit the driver.
 *
 * ide.c will then give higher priority to requests which originate from the
 * other device, until will change rq_status to RQ_ACTIVE.
 *
 * 4. When the packet command is finished, it will be checked for errors.
 *
 * 5. In case an error was found, we queue a request sense packet command in
 * front of the request queue and retry the operation up to
 * IDETAPE_MAX_PC_RETRIES times.
 *
 * 6. In case no error was found, or we decided to give up and not to retry
 * again, the callback function will be called and then we will handle the next
 * request.
 */
static ide_startstop_t idetape_transfer_pc(ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;

	return ide_transfer_pc(drive, tape->pc, idetape_pc_intr,
			       IDETAPE_WAIT_CMD, NULL);
}

static ide_startstop_t idetape_issue_pc(ide_drive_t *drive,
		struct ide_atapi_pc *pc)
{
	idetape_tape_t *tape = drive->driver_data;

	if (tape->pc->c[0] == REQUEST_SENSE &&
	    pc->c[0] == REQUEST_SENSE) {
		printk(KERN_ERR "ide-tape: possible ide-tape.c bug - "
			"Two request sense in serial were issued\n");
	}

	if (tape->failed_pc == NULL && pc->c[0] != REQUEST_SENSE)
		tape->failed_pc = pc;
	/* Set the current packet command */
	tape->pc = pc;

	if (pc->retries > IDETAPE_MAX_PC_RETRIES ||
		(pc->flags & PC_FLAG_ABORT)) {
		/*
		 * We will "abort" retrying a packet command in case legitimate
		 * error code was received (crossing a filemark, or end of the
		 * media, for example).
		 */
		if (!(pc->flags & PC_FLAG_ABORT)) {
			if (!(pc->c[0] == TEST_UNIT_READY &&
			      tape->sense_key == 2 && tape->asc == 4 &&
			     (tape->ascq == 1 || tape->ascq == 8))) {
				printk(KERN_ERR "ide-tape: %s: I/O error, "
						"pc = %2x, key = %2x, "
						"asc = %2x, ascq = %2x\n",
						tape->name, pc->c[0],
						tape->sense_key, tape->asc,
						tape->ascq);
			}
			/* Giving up */
			pc->error = IDETAPE_ERROR_GENERAL;
		}
		tape->failed_pc = NULL;
		drive->pc_callback(drive);
		return ide_stopped;
	}
	debug_log(DBG_SENSE, "Retry #%d, cmd = %02X\n", pc->retries, pc->c[0]);

	pc->retries++;

	return ide_issue_pc(drive, pc, idetape_transfer_pc,
			    IDETAPE_WAIT_CMD, NULL);
}

/* A mode sense command is used to "sense" tape parameters. */
static void idetape_create_mode_sense_cmd(struct ide_atapi_pc *pc, u8 page_code)
{
	idetape_init_pc(pc);
	pc->c[0] = MODE_SENSE;
	if (page_code != IDETAPE_BLOCK_DESCRIPTOR)
		/* DBD = 1 - Don't return block descriptors */
		pc->c[1] = 8;
	pc->c[2] = page_code;
	/*
	 * Changed pc->c[3] to 0 (255 will at best return unused info).
	 *
	 * For SCSI this byte is defined as subpage instead of high byte
	 * of length and some IDE drives seem to interpret it this way
	 * and return an error when 255 is used.
	 */
	pc->c[3] = 0;
	/* We will just discard data in that case */
	pc->c[4] = 255;
	if (page_code == IDETAPE_BLOCK_DESCRIPTOR)
		pc->req_xfer = 12;
	else if (page_code == IDETAPE_CAPABILITIES_PAGE)
		pc->req_xfer = 24;
	else
		pc->req_xfer = 50;
}

static ide_startstop_t idetape_media_access_finished(ide_drive_t *drive)
{
	ide_hwif_t *hwif = drive->hwif;
	idetape_tape_t *tape = drive->driver_data;
	struct ide_atapi_pc *pc = tape->pc;
	u8 stat;

	stat = hwif->tp_ops->read_status(hwif);

	if (stat & ATA_DSC) {
		if (stat & ATA_ERR) {
			/* Error detected */
			if (pc->c[0] != TEST_UNIT_READY)
				printk(KERN_ERR "ide-tape: %s: I/O error, ",
						tape->name);
			/* Retry operation */
			idetape_retry_pc(drive);
			return ide_stopped;
		}
		pc->error = 0;
	} else {
		pc->error = IDETAPE_ERROR_GENERAL;
		tape->failed_pc = NULL;
	}
	drive->pc_callback(drive);
	return ide_stopped;
}

static void ide_tape_create_rw_cmd(idetape_tape_t *tape,
				   struct ide_atapi_pc *pc, struct request *rq,
				   u8 opcode)
{
	struct idetape_bh *bh = (struct idetape_bh *)rq->special;
	unsigned int length = rq->current_nr_sectors;

	idetape_init_pc(pc);
	put_unaligned(cpu_to_be32(length), (unsigned int *) &pc->c[1]);
	pc->c[1] = 1;
	pc->bh = bh;
	pc->buf = NULL;
	pc->buf_size = length * tape->blk_size;
	pc->req_xfer = pc->buf_size;
	if (pc->req_xfer == tape->buffer_size)
		pc->flags |= PC_FLAG_DMA_OK;

	if (opcode == READ_6) {
		pc->c[0] = READ_6;
		atomic_set(&bh->b_count, 0);
	} else if (opcode == WRITE_6) {
		pc->c[0] = WRITE_6;
		pc->flags |= PC_FLAG_WRITING;
		pc->b_data = bh->b_data;
		pc->b_count = atomic_read(&bh->b_count);
	}

	memcpy(rq->cmd, pc->c, 12);
}

static ide_startstop_t idetape_do_request(ide_drive_t *drive,
					  struct request *rq, sector_t block)
{
	ide_hwif_t *hwif = drive->hwif;
	idetape_tape_t *tape = drive->driver_data;
	struct ide_atapi_pc *pc = NULL;
	struct request *postponed_rq = tape->postponed_rq;
	u8 stat;

	debug_log(DBG_SENSE, "sector: %llu, nr_sectors: %lu,"
			" current_nr_sectors: %u\n",
			(unsigned long long)rq->sector, rq->nr_sectors,
			rq->current_nr_sectors);

	if (!blk_special_request(rq)) {
		/* We do not support buffer cache originated requests. */
		printk(KERN_NOTICE "ide-tape: %s: Unsupported request in "
			"request queue (%d)\n", drive->name, rq->cmd_type);
		ide_end_request(drive, 0, 0);
		return ide_stopped;
	}

	/* Retry a failed packet command */
	if (tape->failed_pc && tape->pc->c[0] == REQUEST_SENSE) {
		pc = tape->failed_pc;
		goto out;
	}

	if (postponed_rq != NULL)
		if (rq != postponed_rq) {
			printk(KERN_ERR "ide-tape: ide-tape.c bug - "
					"Two DSC requests were queued\n");
			idetape_end_request(drive, 0, 0);
			return ide_stopped;
		}

	tape->postponed_rq = NULL;

	/*
	 * If the tape is still busy, postpone our request and service
	 * the other device meanwhile.
	 */
	stat = hwif->tp_ops->read_status(hwif);

	if (!drive->dsc_overlap && !(rq->cmd[13] & REQ_IDETAPE_PC2))
		set_bit(IDE_AFLAG_IGNORE_DSC, &drive->atapi_flags);

	if (drive->post_reset == 1) {
		set_bit(IDE_AFLAG_IGNORE_DSC, &drive->atapi_flags);
		drive->post_reset = 0;
	}

	if (!test_and_clear_bit(IDE_AFLAG_IGNORE_DSC, &drive->atapi_flags) &&
	    (stat & ATA_DSC) == 0) {
		if (postponed_rq == NULL) {
			tape->dsc_polling_start = jiffies;
			tape->dsc_poll_freq = tape->best_dsc_rw_freq;
			tape->dsc_timeout = jiffies + IDETAPE_DSC_RW_TIMEOUT;
		} else if (time_after(jiffies, tape->dsc_timeout)) {
			printk(KERN_ERR "ide-tape: %s: DSC timeout\n",
				tape->name);
			if (rq->cmd[13] & REQ_IDETAPE_PC2) {
				idetape_media_access_finished(drive);
				return ide_stopped;
			} else {
				return ide_do_reset(drive);
			}
		} else if (time_after(jiffies,
					tape->dsc_polling_start +
					IDETAPE_DSC_MA_THRESHOLD))
			tape->dsc_poll_freq = IDETAPE_DSC_MA_SLOW;
		idetape_postpone_request(drive);
		return ide_stopped;
	}
	if (rq->cmd[13] & REQ_IDETAPE_READ) {
		pc = idetape_next_pc_storage(drive);
		ide_tape_create_rw_cmd(tape, pc, rq, READ_6);
		goto out;
	}
	if (rq->cmd[13] & REQ_IDETAPE_WRITE) {
		pc = idetape_next_pc_storage(drive);
		ide_tape_create_rw_cmd(tape, pc, rq, WRITE_6);
		goto out;
	}
	if (rq->cmd[13] & REQ_IDETAPE_PC1) {
		pc = (struct ide_atapi_pc *) rq->buffer;
		rq->cmd[13] &= ~(REQ_IDETAPE_PC1);
		rq->cmd[13] |= REQ_IDETAPE_PC2;
		goto out;
	}
	if (rq->cmd[13] & REQ_IDETAPE_PC2) {
		idetape_media_access_finished(drive);
		return ide_stopped;
	}
	BUG();

out:
	return idetape_issue_pc(drive, pc);
}

/*
 * The function below uses __get_free_pages to allocate a data buffer of size
 * tape->buffer_size (or a bit more). We attempt to combine sequential pages as
 * much as possible.
 *
 * It returns a pointer to the newly allocated buffer, or NULL in case of
 * failure.
 */
static struct idetape_bh *ide_tape_kmalloc_buffer(idetape_tape_t *tape,
						  int full, int clear)
{
	struct idetape_bh *prev_bh, *bh, *merge_bh;
	int pages = tape->pages_per_buffer;
	unsigned int order, b_allocd;
	char *b_data = NULL;

	merge_bh = kmalloc(sizeof(struct idetape_bh), GFP_KERNEL);
	bh = merge_bh;
	if (bh == NULL)
		goto abort;

	order = fls(pages) - 1;
	bh->b_data = (char *) __get_free_pages(GFP_KERNEL, order);
	if (!bh->b_data)
		goto abort;
	b_allocd = (1 << order) * PAGE_SIZE;
	pages &= (order-1);

	if (clear)
		memset(bh->b_data, 0, b_allocd);
	bh->b_reqnext = NULL;
	bh->b_size = b_allocd;
	atomic_set(&bh->b_count, full ? bh->b_size : 0);

	while (pages) {
		order = fls(pages) - 1;
		b_data = (char *) __get_free_pages(GFP_KERNEL, order);
		if (!b_data)
			goto abort;
		b_allocd = (1 << order) * PAGE_SIZE;

		if (clear)
			memset(b_data, 0, b_allocd);

		/* newly allocated page frames below buffer header or ...*/
		if (bh->b_data == b_data + b_allocd) {
			bh->b_size += b_allocd;
			bh->b_data -= b_allocd;
			if (full)
				atomic_add(b_allocd, &bh->b_count);
			continue;
		}
		/* they are above the header */
		if (b_data == bh->b_data + bh->b_size) {
			bh->b_size += b_allocd;
			if (full)
				atomic_add(b_allocd, &bh->b_count);
			continue;
		}
		prev_bh = bh;
		bh = kmalloc(sizeof(struct idetape_bh), GFP_KERNEL);
		if (!bh) {
			free_pages((unsigned long) b_data, order);
			goto abort;
		}
		bh->b_reqnext = NULL;
		bh->b_data = b_data;
		bh->b_size = b_allocd;
		atomic_set(&bh->b_count, full ? bh->b_size : 0);
		prev_bh->b_reqnext = bh;

		pages &= (order-1);
	}

	bh->b_size -= tape->excess_bh_size;
	if (full)
		atomic_sub(tape->excess_bh_size, &bh->b_count);
	return merge_bh;
abort:
	ide_tape_kfree_buffer(tape);
	return NULL;
}

static int idetape_copy_stage_from_user(idetape_tape_t *tape,
					const char __user *buf, int n)
{
	struct idetape_bh *bh = tape->bh;
	int count;
	int ret = 0;

	while (n) {
		if (bh == NULL) {
			printk(KERN_ERR "ide-tape: bh == NULL in %s\n",
					__func__);
			return 1;
		}
		count = min((unsigned int)
				(bh->b_size - atomic_read(&bh->b_count)),
				(unsigned int)n);
		if (copy_from_user(bh->b_data + atomic_read(&bh->b_count), buf,
				count))
			ret = 1;
		n -= count;
		atomic_add(count, &bh->b_count);
		buf += count;
		if (atomic_read(&bh->b_count) == bh->b_size) {
			bh = bh->b_reqnext;
			if (bh)
				atomic_set(&bh->b_count, 0);
		}
	}
	tape->bh = bh;
	return ret;
}

static int idetape_copy_stage_to_user(idetape_tape_t *tape, char __user *buf,
				      int n)
{
	struct idetape_bh *bh = tape->bh;
	int count;
	int ret = 0;

	while (n) {
		if (bh == NULL) {
			printk(KERN_ERR "ide-tape: bh == NULL in %s\n",
					__func__);
			return 1;
		}
		count = min(tape->b_count, n);
		if  (copy_to_user(buf, tape->b_data, count))
			ret = 1;
		n -= count;
		tape->b_data += count;
		tape->b_count -= count;
		buf += count;
		if (!tape->b_count) {
			bh = bh->b_reqnext;
			tape->bh = bh;
			if (bh) {
				tape->b_data = bh->b_data;
				tape->b_count = atomic_read(&bh->b_count);
			}
		}
	}
	return ret;
}

static void idetape_init_merge_buffer(idetape_tape_t *tape)
{
	struct idetape_bh *bh = tape->merge_bh;
	tape->bh = tape->merge_bh;

	if (tape->chrdev_dir == IDETAPE_DIR_WRITE)
		atomic_set(&bh->b_count, 0);
	else {
		tape->b_data = bh->b_data;
		tape->b_count = atomic_read(&bh->b_count);
	}
}

/*
 * Write a filemark if write_filemark=1. Flush the device buffers without
 * writing a filemark otherwise.
 */
static void idetape_create_write_filemark_cmd(ide_drive_t *drive,
		struct ide_atapi_pc *pc, int write_filemark)
{
	idetape_init_pc(pc);
	pc->c[0] = WRITE_FILEMARKS;
	pc->c[4] = write_filemark;
	pc->flags |= PC_FLAG_WAIT_FOR_DSC;
}

static void idetape_create_test_unit_ready_cmd(struct ide_atapi_pc *pc)
{
	idetape_init_pc(pc);
	pc->c[0] = TEST_UNIT_READY;
}

/*
 * We add a special packet command request to the tail of the request queue, and
 * wait for it to be serviced.
 */
static int idetape_queue_pc_tail(ide_drive_t *drive, struct ide_atapi_pc *pc)
{
	struct ide_tape_obj *tape = drive->driver_data;
	struct request *rq;
	int error;

	rq = blk_get_request(drive->queue, READ, __GFP_WAIT);
	rq->cmd_type = REQ_TYPE_SPECIAL;
	rq->cmd[13] = REQ_IDETAPE_PC1;
	rq->buffer = (char *)pc;
	memcpy(rq->cmd, pc->c, 12);
	error = blk_execute_rq(drive->queue, tape->disk, rq, 0);
	blk_put_request(rq);
	return error;
}

static void idetape_create_load_unload_cmd(ide_drive_t *drive,
		struct ide_atapi_pc *pc, int cmd)
{
	idetape_init_pc(pc);
	pc->c[0] = START_STOP;
	pc->c[4] = cmd;
	pc->flags |= PC_FLAG_WAIT_FOR_DSC;
}

static int idetape_wait_ready(ide_drive_t *drive, unsigned long timeout)
{
	idetape_tape_t *tape = drive->driver_data;
	struct ide_atapi_pc pc;
	int load_attempted = 0;

	/* Wait for the tape to become ready */
	set_bit(IDE_AFLAG_MEDIUM_PRESENT, &drive->atapi_flags);
	timeout += jiffies;
	while (time_before(jiffies, timeout)) {
		idetape_create_test_unit_ready_cmd(&pc);
		if (!idetape_queue_pc_tail(drive, &pc))
			return 0;
		if ((tape->sense_key == 2 && tape->asc == 4 && tape->ascq == 2)
		    || (tape->asc == 0x3A)) {
			/* no media */
			if (load_attempted)
				return -ENOMEDIUM;
			idetape_create_load_unload_cmd(drive, &pc,
							IDETAPE_LU_LOAD_MASK);
			idetape_queue_pc_tail(drive, &pc);
			load_attempted = 1;
		/* not about to be ready */
		} else if (!(tape->sense_key == 2 && tape->asc == 4 &&
			     (tape->ascq == 1 || tape->ascq == 8)))
			return -EIO;
		msleep(100);
	}
	return -EIO;
}

static int idetape_flush_tape_buffers(ide_drive_t *drive)
{
	struct ide_atapi_pc pc;
	int rc;

	idetape_create_write_filemark_cmd(drive, &pc, 0);
	rc = idetape_queue_pc_tail(drive, &pc);
	if (rc)
		return rc;
	idetape_wait_ready(drive, 60 * 5 * HZ);
	return 0;
}

static void idetape_create_read_position_cmd(struct ide_atapi_pc *pc)
{
	idetape_init_pc(pc);
	pc->c[0] = READ_POSITION;
	pc->req_xfer = 20;
}

static int idetape_read_position(ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;
	struct ide_atapi_pc pc;
	int position;

	debug_log(DBG_PROCS, "Enter %s\n", __func__);

	idetape_create_read_position_cmd(&pc);
	if (idetape_queue_pc_tail(drive, &pc))
		return -1;
	position = tape->first_frame;
	return position;
}

static void idetape_create_locate_cmd(ide_drive_t *drive,
		struct ide_atapi_pc *pc,
		unsigned int block, u8 partition, int skip)
{
	idetape_init_pc(pc);
	pc->c[0] = POSITION_TO_ELEMENT;
	pc->c[1] = 2;
	put_unaligned(cpu_to_be32(block), (unsigned int *) &pc->c[3]);
	pc->c[8] = partition;
	pc->flags |= PC_FLAG_WAIT_FOR_DSC;
}

static int idetape_create_prevent_cmd(ide_drive_t *drive,
		struct ide_atapi_pc *pc, int prevent)
{
	idetape_tape_t *tape = drive->driver_data;

	/* device supports locking according to capabilities page */
	if (!(tape->caps[6] & 0x01))
		return 0;

	idetape_init_pc(pc);
	pc->c[0] = ALLOW_MEDIUM_REMOVAL;
	pc->c[4] = prevent;
	return 1;
}

static void __ide_tape_discard_merge_buffer(ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;

	if (tape->chrdev_dir != IDETAPE_DIR_READ)
		return;

	clear_bit(IDE_AFLAG_FILEMARK, &drive->atapi_flags);
	tape->merge_bh_size = 0;
	if (tape->merge_bh != NULL) {
		ide_tape_kfree_buffer(tape);
		tape->merge_bh = NULL;
	}

	tape->chrdev_dir = IDETAPE_DIR_NONE;
}

/*
 * Position the tape to the requested block using the LOCATE packet command.
 * A READ POSITION command is then issued to check where we are positioned. Like
 * all higher level operations, we queue the commands at the tail of the request
 * queue and wait for their completion.
 */
static int idetape_position_tape(ide_drive_t *drive, unsigned int block,
		u8 partition, int skip)
{
	idetape_tape_t *tape = drive->driver_data;
	int retval;
	struct ide_atapi_pc pc;

	if (tape->chrdev_dir == IDETAPE_DIR_READ)
		__ide_tape_discard_merge_buffer(drive);
	idetape_wait_ready(drive, 60 * 5 * HZ);
	idetape_create_locate_cmd(drive, &pc, block, partition, skip);
	retval = idetape_queue_pc_tail(drive, &pc);
	if (retval)
		return (retval);

	idetape_create_read_position_cmd(&pc);
	return (idetape_queue_pc_tail(drive, &pc));
}

static void ide_tape_discard_merge_buffer(ide_drive_t *drive,
					  int restore_position)
{
	idetape_tape_t *tape = drive->driver_data;
	int seek, position;

	__ide_tape_discard_merge_buffer(drive);
	if (restore_position) {
		position = idetape_read_position(drive);
		seek = position > 0 ? position : 0;
		if (idetape_position_tape(drive, seek, 0, 0)) {
			printk(KERN_INFO "ide-tape: %s: position_tape failed in"
					 " %s\n", tape->name, __func__);
			return;
		}
	}
}

/*
 * Generate a read/write request for the block device interface and wait for it
 * to be serviced.
 */
static int idetape_queue_rw_tail(ide_drive_t *drive, int cmd, int blocks,
				 struct idetape_bh *bh)
{
	idetape_tape_t *tape = drive->driver_data;
	struct request *rq;
	int ret, errors;

	debug_log(DBG_SENSE, "%s: cmd=%d\n", __func__, cmd);

	rq = blk_get_request(drive->queue, READ, __GFP_WAIT);
	rq->cmd_type = REQ_TYPE_SPECIAL;
	rq->cmd[13] = cmd;
	rq->rq_disk = tape->disk;
	rq->special = (void *)bh;
	rq->sector = tape->first_frame;
	rq->nr_sectors = blocks;
	rq->current_nr_sectors = blocks;
	blk_execute_rq(drive->queue, tape->disk, rq, 0);

	errors = rq->errors;
	ret = tape->blk_size * (blocks - rq->current_nr_sectors);
	blk_put_request(rq);

	if ((cmd & (REQ_IDETAPE_READ | REQ_IDETAPE_WRITE)) == 0)
		return 0;

	if (tape->merge_bh)
		idetape_init_merge_buffer(tape);
	if (errors == IDETAPE_ERROR_GENERAL)
		return -EIO;
	return ret;
}

static void idetape_create_inquiry_cmd(struct ide_atapi_pc *pc)
{
	idetape_init_pc(pc);
	pc->c[0] = INQUIRY;
	pc->c[4] = 254;
	pc->req_xfer = 254;
}

static void idetape_create_rewind_cmd(ide_drive_t *drive,
		struct ide_atapi_pc *pc)
{
	idetape_init_pc(pc);
	pc->c[0] = REZERO_UNIT;
	pc->flags |= PC_FLAG_WAIT_FOR_DSC;
}

static void idetape_create_erase_cmd(struct ide_atapi_pc *pc)
{
	idetape_init_pc(pc);
	pc->c[0] = ERASE;
	pc->c[1] = 1;
	pc->flags |= PC_FLAG_WAIT_FOR_DSC;
}

static void idetape_create_space_cmd(struct ide_atapi_pc *pc, int count, u8 cmd)
{
	idetape_init_pc(pc);
	pc->c[0] = SPACE;
	put_unaligned(cpu_to_be32(count), (unsigned int *) &pc->c[1]);
	pc->c[1] = cmd;
	pc->flags |= PC_FLAG_WAIT_FOR_DSC;
}

/* Queue up a character device originated write request. */
static int idetape_add_chrdev_write_request(ide_drive_t *drive, int blocks)
{
	idetape_tape_t *tape = drive->driver_data;

	debug_log(DBG_CHRDEV, "Enter %s\n", __func__);

	return idetape_queue_rw_tail(drive, REQ_IDETAPE_WRITE,
				     blocks, tape->merge_bh);
}

static void ide_tape_flush_merge_buffer(ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;
	int blocks, min;
	struct idetape_bh *bh;

	if (tape->chrdev_dir != IDETAPE_DIR_WRITE) {
		printk(KERN_ERR "ide-tape: bug: Trying to empty merge buffer"
				" but we are not writing.\n");
		return;
	}
	if (tape->merge_bh_size > tape->buffer_size) {
		printk(KERN_ERR "ide-tape: bug: merge_buffer too big\n");
		tape->merge_bh_size = tape->buffer_size;
	}
	if (tape->merge_bh_size) {
		blocks = tape->merge_bh_size / tape->blk_size;
		if (tape->merge_bh_size % tape->blk_size) {
			unsigned int i;

			blocks++;
			i = tape->blk_size - tape->merge_bh_size %
				tape->blk_size;
			bh = tape->bh->b_reqnext;
			while (bh) {
				atomic_set(&bh->b_count, 0);
				bh = bh->b_reqnext;
			}
			bh = tape->bh;
			while (i) {
				if (bh == NULL) {
					printk(KERN_INFO "ide-tape: bug,"
							 " bh NULL\n");
					break;
				}
				min = min(i, (unsigned int)(bh->b_size -
						atomic_read(&bh->b_count)));
				memset(bh->b_data + atomic_read(&bh->b_count),
						0, min);
				atomic_add(min, &bh->b_count);
				i -= min;
				bh = bh->b_reqnext;
			}
		}
		(void) idetape_add_chrdev_write_request(drive, blocks);
		tape->merge_bh_size = 0;
	}
	if (tape->merge_bh != NULL) {
		ide_tape_kfree_buffer(tape);
		tape->merge_bh = NULL;
	}
	tape->chrdev_dir = IDETAPE_DIR_NONE;
}

static int idetape_init_read(ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;
	int bytes_read;

	/* Initialize read operation */
	if (tape->chrdev_dir != IDETAPE_DIR_READ) {
		if (tape->chrdev_dir == IDETAPE_DIR_WRITE) {
			ide_tape_flush_merge_buffer(drive);
			idetape_flush_tape_buffers(drive);
		}
		if (tape->merge_bh || tape->merge_bh_size) {
			printk(KERN_ERR "ide-tape: merge_bh_size should be"
					 " 0 now\n");
			tape->merge_bh_size = 0;
		}
		tape->merge_bh = ide_tape_kmalloc_buffer(tape, 0, 0);
		if (!tape->merge_bh)
			return -ENOMEM;
		tape->chrdev_dir = IDETAPE_DIR_READ;

		/*
		 * Issue a read 0 command to ensure that DSC handshake is
		 * switched from completion mode to buffer available mode.
		 * No point in issuing this if DSC overlap isn't supported, some
		 * drives (Seagate STT3401A) will return an error.
		 */
		if (drive->dsc_overlap) {
			bytes_read = idetape_queue_rw_tail(drive,
							REQ_IDETAPE_READ, 0,
							tape->merge_bh);
			if (bytes_read < 0) {
				ide_tape_kfree_buffer(tape);
				tape->merge_bh = NULL;
				tape->chrdev_dir = IDETAPE_DIR_NONE;
				return bytes_read;
			}
		}
	}

	return 0;
}

/* called from idetape_chrdev_read() to service a chrdev read request. */
static int idetape_add_chrdev_read_request(ide_drive_t *drive, int blocks)
{
	idetape_tape_t *tape = drive->driver_data;

	debug_log(DBG_PROCS, "Enter %s, %d blocks\n", __func__, blocks);

	/* If we are at a filemark, return a read length of 0 */
	if (test_bit(IDE_AFLAG_FILEMARK, &drive->atapi_flags))
		return 0;

	idetape_init_read(drive);

	return idetape_queue_rw_tail(drive, REQ_IDETAPE_READ, blocks,
				     tape->merge_bh);
}

static void idetape_pad_zeros(ide_drive_t *drive, int bcount)
{
	idetape_tape_t *tape = drive->driver_data;
	struct idetape_bh *bh;
	int blocks;

	while (bcount) {
		unsigned int count;

		bh = tape->merge_bh;
		count = min(tape->buffer_size, bcount);
		bcount -= count;
		blocks = count / tape->blk_size;
		while (count) {
			atomic_set(&bh->b_count,
				   min(count, (unsigned int)bh->b_size));
			memset(bh->b_data, 0, atomic_read(&bh->b_count));
			count -= atomic_read(&bh->b_count);
			bh = bh->b_reqnext;
		}
		idetape_queue_rw_tail(drive, REQ_IDETAPE_WRITE, blocks,
				      tape->merge_bh);
	}
}

/*
 * Rewinds the tape to the Beginning Of the current Partition (BOP). We
 * currently support only one partition.
 */
static int idetape_rewind_tape(ide_drive_t *drive)
{
	int retval;
	struct ide_atapi_pc pc;
	idetape_tape_t *tape;
	tape = drive->driver_data;

	debug_log(DBG_SENSE, "Enter %s\n", __func__);

	idetape_create_rewind_cmd(drive, &pc);
	retval = idetape_queue_pc_tail(drive, &pc);
	if (retval)
		return retval;

	idetape_create_read_position_cmd(&pc);
	retval = idetape_queue_pc_tail(drive, &pc);
	if (retval)
		return retval;
	return 0;
}

/* mtio.h compatible commands should be issued to the chrdev interface. */
static int idetape_blkdev_ioctl(ide_drive_t *drive, unsigned int cmd,
				unsigned long arg)
{
	idetape_tape_t *tape = drive->driver_data;
	void __user *argp = (void __user *)arg;

	struct idetape_config {
		int dsc_rw_frequency;
		int dsc_media_access_frequency;
		int nr_stages;
	} config;

	debug_log(DBG_PROCS, "Enter %s\n", __func__);

	switch (cmd) {
	case 0x0340:
		if (copy_from_user(&config, argp, sizeof(config)))
			return -EFAULT;
		tape->best_dsc_rw_freq = config.dsc_rw_frequency;
		break;
	case 0x0350:
		config.dsc_rw_frequency = (int) tape->best_dsc_rw_freq;
		config.nr_stages = 1;
		if (copy_to_user(argp, &config, sizeof(config)))
			return -EFAULT;
		break;
	default:
		return -EIO;
	}
	return 0;
}

static int idetape_space_over_filemarks(ide_drive_t *drive, short mt_op,
					int mt_count)
{
	idetape_tape_t *tape = drive->driver_data;
	struct ide_atapi_pc pc;
	int retval, count = 0;
	int sprev = !!(tape->caps[4] & 0x20);

	if (mt_count == 0)
		return 0;
	if (MTBSF == mt_op || MTBSFM == mt_op) {
		if (!sprev)
			return -EIO;
		mt_count = -mt_count;
	}

	if (tape->chrdev_dir == IDETAPE_DIR_READ) {
		tape->merge_bh_size = 0;
		if (test_and_clear_bit(IDE_AFLAG_FILEMARK, &drive->atapi_flags))
			++count;
		ide_tape_discard_merge_buffer(drive, 0);
	}

	switch (mt_op) {
	case MTFSF:
	case MTBSF:
		idetape_create_space_cmd(&pc, mt_count - count,
					 IDETAPE_SPACE_OVER_FILEMARK);
		return idetape_queue_pc_tail(drive, &pc);
	case MTFSFM:
	case MTBSFM:
		if (!sprev)
			return -EIO;
		retval = idetape_space_over_filemarks(drive, MTFSF,
						      mt_count - count);
		if (retval)
			return retval;
		count = (MTBSFM == mt_op ? 1 : -1);
		return idetape_space_over_filemarks(drive, MTFSF, count);
	default:
		printk(KERN_ERR "ide-tape: MTIO operation %d not supported\n",
				mt_op);
		return -EIO;
	}
}

/*
 * Our character device read / write functions.
 *
 * The tape is optimized to maximize throughput when it is transferring an
 * integral number of the "continuous transfer limit", which is a parameter of
 * the specific tape (26kB on my particular tape, 32kB for Onstream).
 *
 * As of version 1.3 of the driver, the character device provides an abstract
 * continuous view of the media - any mix of block sizes (even 1 byte) on the
 * same backup/restore procedure is supported. The driver will internally
 * convert the requests to the recommended transfer unit, so that an unmatch
 * between the user's block size to the recommended size will only result in a
 * (slightly) increased driver overhead, but will no longer hit performance.
 * This is not applicable to Onstream.
 */
static ssize_t idetape_chrdev_read(struct file *file, char __user *buf,
				   size_t count, loff_t *ppos)
{
	struct ide_tape_obj *tape = ide_tape_f(file);
	ide_drive_t *drive = tape->drive;
	ssize_t bytes_read, temp, actually_read = 0, rc;
	ssize_t ret = 0;
	u16 ctl = *(u16 *)&tape->caps[12];

	debug_log(DBG_CHRDEV, "Enter %s, count %Zd\n", __func__, count);

	if (tape->chrdev_dir != IDETAPE_DIR_READ) {
		if (test_bit(IDE_AFLAG_DETECT_BS, &drive->atapi_flags))
			if (count > tape->blk_size &&
			    (count % tape->blk_size) == 0)
				tape->user_bs_factor = count / tape->blk_size;
	}
	rc = idetape_init_read(drive);
	if (rc < 0)
		return rc;
	if (count == 0)
		return (0);
	if (tape->merge_bh_size) {
		actually_read = min((unsigned int)(tape->merge_bh_size),
				    (unsigned int)count);
		if (idetape_copy_stage_to_user(tape, buf, actually_read))
			ret = -EFAULT;
		buf += actually_read;
		tape->merge_bh_size -= actually_read;
		count -= actually_read;
	}
	while (count >= tape->buffer_size) {
		bytes_read = idetape_add_chrdev_read_request(drive, ctl);
		if (bytes_read <= 0)
			goto finish;
		if (idetape_copy_stage_to_user(tape, buf, bytes_read))
			ret = -EFAULT;
		buf += bytes_read;
		count -= bytes_read;
		actually_read += bytes_read;
	}
	if (count) {
		bytes_read = idetape_add_chrdev_read_request(drive, ctl);
		if (bytes_read <= 0)
			goto finish;
		temp = min((unsigned long)count, (unsigned long)bytes_read);
		if (idetape_copy_stage_to_user(tape, buf, temp))
			ret = -EFAULT;
		actually_read += temp;
		tape->merge_bh_size = bytes_read-temp;
	}
finish:
	if (!actually_read && test_bit(IDE_AFLAG_FILEMARK, &drive->atapi_flags)) {
		debug_log(DBG_SENSE, "%s: spacing over filemark\n", tape->name);

		idetape_space_over_filemarks(drive, MTFSF, 1);
		return 0;
	}

	return ret ? ret : actually_read;
}

static ssize_t idetape_chrdev_write(struct file *file, const char __user *buf,
				     size_t count, loff_t *ppos)
{
	struct ide_tape_obj *tape = ide_tape_f(file);
	ide_drive_t *drive = tape->drive;
	ssize_t actually_written = 0;
	ssize_t ret = 0;
	u16 ctl = *(u16 *)&tape->caps[12];

	/* The drive is write protected. */
	if (tape->write_prot)
		return -EACCES;

	debug_log(DBG_CHRDEV, "Enter %s, count %Zd\n", __func__, count);

	/* Initialize write operation */
	if (tape->chrdev_dir != IDETAPE_DIR_WRITE) {
		if (tape->chrdev_dir == IDETAPE_DIR_READ)
			ide_tape_discard_merge_buffer(drive, 1);
		if (tape->merge_bh || tape->merge_bh_size) {
			printk(KERN_ERR "ide-tape: merge_bh_size "
				"should be 0 now\n");
			tape->merge_bh_size = 0;
		}
		tape->merge_bh = ide_tape_kmalloc_buffer(tape, 0, 0);
		if (!tape->merge_bh)
			return -ENOMEM;
		tape->chrdev_dir = IDETAPE_DIR_WRITE;
		idetape_init_merge_buffer(tape);

		/*
		 * Issue a write 0 command to ensure that DSC handshake is
		 * switched from completion mode to buffer available mode. No
		 * point in issuing this if DSC overlap isn't supported, some
		 * drives (Seagate STT3401A) will return an error.
		 */
		if (drive->dsc_overlap) {
			ssize_t retval = idetape_queue_rw_tail(drive,
							REQ_IDETAPE_WRITE, 0,
							tape->merge_bh);
			if (retval < 0) {
				ide_tape_kfree_buffer(tape);
				tape->merge_bh = NULL;
				tape->chrdev_dir = IDETAPE_DIR_NONE;
				return retval;
			}
		}
	}
	if (count == 0)
		return (0);
	if (tape->merge_bh_size) {
		if (tape->merge_bh_size >= tape->buffer_size) {
			printk(KERN_ERR "ide-tape: bug: merge buf too big\n");
			tape->merge_bh_size = 0;
		}
		actually_written = min((unsigned int)
				(tape->buffer_size - tape->merge_bh_size),
				(unsigned int)count);
		if (idetape_copy_stage_from_user(tape, buf, actually_written))
				ret = -EFAULT;
		buf += actually_written;
		tape->merge_bh_size += actually_written;
		count -= actually_written;

		if (tape->merge_bh_size == tape->buffer_size) {
			ssize_t retval;
			tape->merge_bh_size = 0;
			retval = idetape_add_chrdev_write_request(drive, ctl);
			if (retval <= 0)
				return (retval);
		}
	}
	while (count >= tape->buffer_size) {
		ssize_t retval;
		if (idetape_copy_stage_from_user(tape, buf, tape->buffer_size))
			ret = -EFAULT;
		buf += tape->buffer_size;
		count -= tape->buffer_size;
		retval = idetape_add_chrdev_write_request(drive, ctl);
		actually_written += tape->buffer_size;
		if (retval <= 0)
			return (retval);
	}
	if (count) {
		actually_written += count;
		if (idetape_copy_stage_from_user(tape, buf, count))
			ret = -EFAULT;
		tape->merge_bh_size += count;
	}
	return ret ? ret : actually_written;
}

static int idetape_write_filemark(ide_drive_t *drive)
{
	struct ide_atapi_pc pc;

	/* Write a filemark */
	idetape_create_write_filemark_cmd(drive, &pc, 1);
	if (idetape_queue_pc_tail(drive, &pc)) {
		printk(KERN_ERR "ide-tape: Couldn't write a filemark\n");
		return -EIO;
	}
	return 0;
}

/*
 * Called from idetape_chrdev_ioctl when the general mtio MTIOCTOP ioctl is
 * requested.
 *
 * Note: MTBSF and MTBSFM are not supported when the tape doesn't support
 * spacing over filemarks in the reverse direction. In this case, MTFSFM is also
 * usually not supported.
 *
 * The following commands are currently not supported:
 *
 * MTFSS, MTBSS, MTWSM, MTSETDENSITY, MTSETDRVBUFFER, MT_ST_BOOLEANS,
 * MT_ST_WRITE_THRESHOLD.
 */
static int idetape_mtioctop(ide_drive_t *drive, short mt_op, int mt_count)
{
	idetape_tape_t *tape = drive->driver_data;
	struct ide_atapi_pc pc;
	int i, retval;

	debug_log(DBG_ERR, "Handling MTIOCTOP ioctl: mt_op=%d, mt_count=%d\n",
			mt_op, mt_count);

	switch (mt_op) {
	case MTFSF:
	case MTFSFM:
	case MTBSF:
	case MTBSFM:
		if (!mt_count)
			return 0;
		return idetape_space_over_filemarks(drive, mt_op, mt_count);
	default:
		break;
	}

	switch (mt_op) {
	case MTWEOF:
		if (tape->write_prot)
			return -EACCES;
		ide_tape_discard_merge_buffer(drive, 1);
		for (i = 0; i < mt_count; i++) {
			retval = idetape_write_filemark(drive);
			if (retval)
				return retval;
		}
		return 0;
	case MTREW:
		ide_tape_discard_merge_buffer(drive, 0);
		if (idetape_rewind_tape(drive))
			return -EIO;
		return 0;
	case MTLOAD:
		ide_tape_discard_merge_buffer(drive, 0);
		idetape_create_load_unload_cmd(drive, &pc,
					       IDETAPE_LU_LOAD_MASK);
		return idetape_queue_pc_tail(drive, &pc);
	case MTUNLOAD:
	case MTOFFL:
		/*
		 * If door is locked, attempt to unlock before
		 * attempting to eject.
		 */
		if (tape->door_locked) {
			if (idetape_create_prevent_cmd(drive, &pc, 0))
				if (!idetape_queue_pc_tail(drive, &pc))
					tape->door_locked = DOOR_UNLOCKED;
		}
		ide_tape_discard_merge_buffer(drive, 0);
		idetape_create_load_unload_cmd(drive, &pc,
					      !IDETAPE_LU_LOAD_MASK);
		retval = idetape_queue_pc_tail(drive, &pc);
		if (!retval)
			clear_bit(IDE_AFLAG_MEDIUM_PRESENT, &drive->atapi_flags);
		return retval;
	case MTNOP:
		ide_tape_discard_merge_buffer(drive, 0);
		return idetape_flush_tape_buffers(drive);
	case MTRETEN:
		ide_tape_discard_merge_buffer(drive, 0);
		idetape_create_load_unload_cmd(drive, &pc,
			IDETAPE_LU_RETENSION_MASK | IDETAPE_LU_LOAD_MASK);
		return idetape_queue_pc_tail(drive, &pc);
	case MTEOM:
		idetape_create_space_cmd(&pc, 0, IDETAPE_SPACE_TO_EOD);
		return idetape_queue_pc_tail(drive, &pc);
	case MTERASE:
		(void)idetape_rewind_tape(drive);
		idetape_create_erase_cmd(&pc);
		return idetape_queue_pc_tail(drive, &pc);
	case MTSETBLK:
		if (mt_count) {
			if (mt_count < tape->blk_size ||
			    mt_count % tape->blk_size)
				return -EIO;
			tape->user_bs_factor = mt_count / tape->blk_size;
			clear_bit(IDE_AFLAG_DETECT_BS, &drive->atapi_flags);
		} else
			set_bit(IDE_AFLAG_DETECT_BS, &drive->atapi_flags);
		return 0;
	case MTSEEK:
		ide_tape_discard_merge_buffer(drive, 0);
		return idetape_position_tape(drive,
			mt_count * tape->user_bs_factor, tape->partition, 0);
	case MTSETPART:
		ide_tape_discard_merge_buffer(drive, 0);
		return idetape_position_tape(drive, 0, mt_count, 0);
	case MTFSR:
	case MTBSR:
	case MTLOCK:
		if (!idetape_create_prevent_cmd(drive, &pc, 1))
			return 0;
		retval = idetape_queue_pc_tail(drive, &pc);
		if (retval)
			return retval;
		tape->door_locked = DOOR_EXPLICITLY_LOCKED;
		return 0;
	case MTUNLOCK:
		if (!idetape_create_prevent_cmd(drive, &pc, 0))
			return 0;
		retval = idetape_queue_pc_tail(drive, &pc);
		if (retval)
			return retval;
		tape->door_locked = DOOR_UNLOCKED;
		return 0;
	default:
		printk(KERN_ERR "ide-tape: MTIO operation %d not supported\n",
				mt_op);
		return -EIO;
	}
}

/*
 * Our character device ioctls. General mtio.h magnetic io commands are
 * supported here, and not in the corresponding block interface. Our own
 * ide-tape ioctls are supported on both interfaces.
 */
static int idetape_chrdev_ioctl(struct inode *inode, struct file *file,
				unsigned int cmd, unsigned long arg)
{
	struct ide_tape_obj *tape = ide_tape_f(file);
	ide_drive_t *drive = tape->drive;
	struct mtop mtop;
	struct mtget mtget;
	struct mtpos mtpos;
	int block_offset = 0, position = tape->first_frame;
	void __user *argp = (void __user *)arg;

	debug_log(DBG_CHRDEV, "Enter %s, cmd=%u\n", __func__, cmd);

	if (tape->chrdev_dir == IDETAPE_DIR_WRITE) {
		ide_tape_flush_merge_buffer(drive);
		idetape_flush_tape_buffers(drive);
	}
	if (cmd == MTIOCGET || cmd == MTIOCPOS) {
		block_offset = tape->merge_bh_size /
			(tape->blk_size * tape->user_bs_factor);
		position = idetape_read_position(drive);
		if (position < 0)
			return -EIO;
	}
	switch (cmd) {
	case MTIOCTOP:
		if (copy_from_user(&mtop, argp, sizeof(struct mtop)))
			return -EFAULT;
		return idetape_mtioctop(drive, mtop.mt_op, mtop.mt_count);
	case MTIOCGET:
		memset(&mtget, 0, sizeof(struct mtget));
		mtget.mt_type = MT_ISSCSI2;
		mtget.mt_blkno = position / tape->user_bs_factor - block_offset;
		mtget.mt_dsreg =
			((tape->blk_size * tape->user_bs_factor)
			 << MT_ST_BLKSIZE_SHIFT) & MT_ST_BLKSIZE_MASK;

		if (tape->drv_write_prot)
			mtget.mt_gstat |= GMT_WR_PROT(0xffffffff);

		if (copy_to_user(argp, &mtget, sizeof(struct mtget)))
			return -EFAULT;
		return 0;
	case MTIOCPOS:
		mtpos.mt_blkno = position / tape->user_bs_factor - block_offset;
		if (copy_to_user(argp, &mtpos, sizeof(struct mtpos)))
			return -EFAULT;
		return 0;
	default:
		if (tape->chrdev_dir == IDETAPE_DIR_READ)
			ide_tape_discard_merge_buffer(drive, 1);
		return idetape_blkdev_ioctl(drive, cmd, arg);
	}
}

/*
 * Do a mode sense page 0 with block descriptor and if it succeeds set the tape
 * block size with the reported value.
 */
static void ide_tape_get_bsize_from_bdesc(ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;
	struct ide_atapi_pc pc;

	idetape_create_mode_sense_cmd(&pc, IDETAPE_BLOCK_DESCRIPTOR);
	if (idetape_queue_pc_tail(drive, &pc)) {
		printk(KERN_ERR "ide-tape: Can't get block descriptor\n");
		if (tape->blk_size == 0) {
			printk(KERN_WARNING "ide-tape: Cannot deal with zero "
					    "block size, assuming 32k\n");
			tape->blk_size = 32768;
		}
		return;
	}
	tape->blk_size = (pc.buf[4 + 5] << 16) +
				(pc.buf[4 + 6] << 8)  +
				 pc.buf[4 + 7];
	tape->drv_write_prot = (pc.buf[2] & 0x80) >> 7;
}

static int idetape_chrdev_open(struct inode *inode, struct file *filp)
{
	unsigned int minor = iminor(inode), i = minor & ~0xc0;
	ide_drive_t *drive;
	idetape_tape_t *tape;
	struct ide_atapi_pc pc;
	int retval;

	if (i >= MAX_HWIFS * MAX_DRIVES)
		return -ENXIO;

	lock_kernel();
	tape = ide_tape_chrdev_get(i);
	if (!tape) {
		unlock_kernel();
		return -ENXIO;
	}

	debug_log(DBG_CHRDEV, "Enter %s\n", __func__);

	/*
	 * We really want to do nonseekable_open(inode, filp); here, but some
	 * versions of tar incorrectly call lseek on tapes and bail out if that
	 * fails.  So we disallow pread() and pwrite(), but permit lseeks.
	 */
	filp->f_mode &= ~(FMODE_PREAD | FMODE_PWRITE);

	drive = tape->drive;

	filp->private_data = tape;

	if (test_and_set_bit(IDE_AFLAG_BUSY, &drive->atapi_flags)) {
		retval = -EBUSY;
		goto out_put_tape;
	}

	retval = idetape_wait_ready(drive, 60 * HZ);
	if (retval) {
		clear_bit(IDE_AFLAG_BUSY, &drive->atapi_flags);
		printk(KERN_ERR "ide-tape: %s: drive not ready\n", tape->name);
		goto out_put_tape;
	}

	idetape_read_position(drive);
	if (!test_bit(IDE_AFLAG_ADDRESS_VALID, &drive->atapi_flags))
		(void)idetape_rewind_tape(drive);

	/* Read block size and write protect status from drive. */
	ide_tape_get_bsize_from_bdesc(drive);

	/* Set write protect flag if device is opened as read-only. */
	if ((filp->f_flags & O_ACCMODE) == O_RDONLY)
		tape->write_prot = 1;
	else
		tape->write_prot = tape->drv_write_prot;

	/* Make sure drive isn't write protected if user wants to write. */
	if (tape->write_prot) {
		if ((filp->f_flags & O_ACCMODE) == O_WRONLY ||
		    (filp->f_flags & O_ACCMODE) == O_RDWR) {
			clear_bit(IDE_AFLAG_BUSY, &drive->atapi_flags);
			retval = -EROFS;
			goto out_put_tape;
		}
	}

	/* Lock the tape drive door so user can't eject. */
	if (tape->chrdev_dir == IDETAPE_DIR_NONE) {
		if (idetape_create_prevent_cmd(drive, &pc, 1)) {
			if (!idetape_queue_pc_tail(drive, &pc)) {
				if (tape->door_locked != DOOR_EXPLICITLY_LOCKED)
					tape->door_locked = DOOR_LOCKED;
			}
		}
	}
	unlock_kernel();
	return 0;

out_put_tape:
	ide_tape_put(tape);
	unlock_kernel();
	return retval;
}

static void idetape_write_release(ide_drive_t *drive, unsigned int minor)
{
	idetape_tape_t *tape = drive->driver_data;

	ide_tape_flush_merge_buffer(drive);
	tape->merge_bh = ide_tape_kmalloc_buffer(tape, 1, 0);
	if (tape->merge_bh != NULL) {
		idetape_pad_zeros(drive, tape->blk_size *
				(tape->user_bs_factor - 1));
		ide_tape_kfree_buffer(tape);
		tape->merge_bh = NULL;
	}
	idetape_write_filemark(drive);
	idetape_flush_tape_buffers(drive);
	idetape_flush_tape_buffers(drive);
}

static int idetape_chrdev_release(struct inode *inode, struct file *filp)
{
	struct ide_tape_obj *tape = ide_tape_f(filp);
	ide_drive_t *drive = tape->drive;
	struct ide_atapi_pc pc;
	unsigned int minor = iminor(inode);

	lock_kernel();
	tape = drive->driver_data;

	debug_log(DBG_CHRDEV, "Enter %s\n", __func__);

	if (tape->chrdev_dir == IDETAPE_DIR_WRITE)
		idetape_write_release(drive, minor);
	if (tape->chrdev_dir == IDETAPE_DIR_READ) {
		if (minor < 128)
			ide_tape_discard_merge_buffer(drive, 1);
	}

	if (minor < 128 && test_bit(IDE_AFLAG_MEDIUM_PRESENT, &drive->atapi_flags))
		(void) idetape_rewind_tape(drive);
	if (tape->chrdev_dir == IDETAPE_DIR_NONE) {
		if (tape->door_locked == DOOR_LOCKED) {
			if (idetape_create_prevent_cmd(drive, &pc, 0)) {
				if (!idetape_queue_pc_tail(drive, &pc))
					tape->door_locked = DOOR_UNLOCKED;
			}
		}
	}
	clear_bit(IDE_AFLAG_BUSY, &drive->atapi_flags);
	ide_tape_put(tape);
	unlock_kernel();
	return 0;
}

static void idetape_get_inquiry_results(ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;
	struct ide_atapi_pc pc;
	char fw_rev[4], vendor_id[8], product_id[16];

	idetape_create_inquiry_cmd(&pc);
	if (idetape_queue_pc_tail(drive, &pc)) {
		printk(KERN_ERR "ide-tape: %s: can't get INQUIRY results\n",
				tape->name);
		return;
	}
	memcpy(vendor_id, &pc.buf[8], 8);
	memcpy(product_id, &pc.buf[16], 16);
	memcpy(fw_rev, &pc.buf[32], 4);

	ide_fixstring(vendor_id, 8, 0);
	ide_fixstring(product_id, 16, 0);
	ide_fixstring(fw_rev, 4, 0);

	printk(KERN_INFO "ide-tape: %s <-> %s: %.8s %.16s rev %.4s\n",
			drive->name, tape->name, vendor_id, product_id, fw_rev);
}

/*
 * Ask the tape about its various parameters. In particular, we will adjust our
 * data transfer buffer	size to the recommended value as returned by the tape.
 */
static void idetape_get_mode_sense_results(ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;
	struct ide_atapi_pc pc;
	u8 *caps;
	u8 speed, max_speed;

	idetape_create_mode_sense_cmd(&pc, IDETAPE_CAPABILITIES_PAGE);
	if (idetape_queue_pc_tail(drive, &pc)) {
		printk(KERN_ERR "ide-tape: Can't get tape parameters - assuming"
				" some default values\n");
		tape->blk_size = 512;
		put_unaligned(52,   (u16 *)&tape->caps[12]);
		put_unaligned(540,  (u16 *)&tape->caps[14]);
		put_unaligned(6*52, (u16 *)&tape->caps[16]);
		return;
	}
	caps = pc.buf + 4 + pc.buf[3];

	/* convert to host order and save for later use */
	speed = be16_to_cpup((__be16 *)&caps[14]);
	max_speed = be16_to_cpup((__be16 *)&caps[8]);

	*(u16 *)&caps[8] = max_speed;
	*(u16 *)&caps[12] = be16_to_cpup((__be16 *)&caps[12]);
	*(u16 *)&caps[14] = speed;
	*(u16 *)&caps[16] = be16_to_cpup((__be16 *)&caps[16]);

	if (!speed) {
		printk(KERN_INFO "ide-tape: %s: invalid tape speed "
				"(assuming 650KB/sec)\n", drive->name);
		*(u16 *)&caps[14] = 650;
	}
	if (!max_speed) {
		printk(KERN_INFO "ide-tape: %s: invalid max_speed "
				"(assuming 650KB/sec)\n", drive->name);
		*(u16 *)&caps[8] = 650;
	}

	memcpy(&tape->caps, caps, 20);
	if (caps[7] & 0x02)
		tape->blk_size = 512;
	else if (caps[7] & 0x04)
		tape->blk_size = 1024;
}

#ifdef CONFIG_IDE_PROC_FS
#define ide_tape_devset_get(name, field) \
static int get_##name(ide_drive_t *drive) \
{ \
	idetape_tape_t *tape = drive->driver_data; \
	return tape->field; \
}

#define ide_tape_devset_set(name, field) \
static int set_##name(ide_drive_t *drive, int arg) \
{ \
	idetape_tape_t *tape = drive->driver_data; \
	tape->field = arg; \
	return 0; \
}

#define ide_tape_devset_rw(_name, _min, _max, _field, _mulf, _divf) \
ide_tape_devset_get(_name, _field) \
ide_tape_devset_set(_name, _field) \
__IDE_DEVSET(_name, S_RW, _min, _max, get_##_name, set_##_name, _mulf, _divf)

#define ide_tape_devset_r(_name, _min, _max, _field, _mulf, _divf) \
ide_tape_devset_get(_name, _field) \
__IDE_DEVSET(_name, S_READ, _min, _max, get_##_name, NULL, _mulf, _divf)

static int mulf_tdsc(ide_drive_t *drive)	{ return 1000; }
static int divf_tdsc(ide_drive_t *drive)	{ return   HZ; }
static int divf_buffer(ide_drive_t *drive)	{ return    2; }
static int divf_buffer_size(ide_drive_t *drive)	{ return 1024; }

ide_devset_rw(dsc_overlap,	0,	1, dsc_overlap);

ide_tape_devset_rw(debug_mask,	0, 0xffff, debug_mask,  NULL, NULL);
ide_tape_devset_rw(tdsc, IDETAPE_DSC_RW_MIN, IDETAPE_DSC_RW_MAX,
		   best_dsc_rw_freq, mulf_tdsc, divf_tdsc);

ide_tape_devset_r(avg_speed,	0, 0xffff, avg_speed,   NULL, NULL);
ide_tape_devset_r(speed,	0, 0xffff, caps[14],    NULL, NULL);
ide_tape_devset_r(buffer,	0, 0xffff, caps[16],    NULL, divf_buffer);
ide_tape_devset_r(buffer_size,	0, 0xffff, buffer_size, NULL, divf_buffer_size);

static const struct ide_devset *idetape_settings[] = {
	&ide_devset_avg_speed,
	&ide_devset_buffer,
	&ide_devset_buffer_size,
	&ide_devset_debug_mask,
	&ide_devset_dsc_overlap,
	&ide_devset_speed,
	&ide_devset_tdsc,
	NULL
};
#endif

/*
 * The function below is called to:
 *
 * 1. Initialize our various state variables.
 * 2. Ask the tape for its capabilities.
 * 3. Allocate a buffer which will be used for data transfer. The buffer size
 * is chosen based on the recommendation which we received in step 2.
 *
 * Note that at this point ide.c already assigned us an irq, so that we can
 * queue requests here and wait for their completion.
 */
static void idetape_setup(ide_drive_t *drive, idetape_tape_t *tape, int minor)
{
	unsigned long t;
	int speed;
	int buffer_size;
	u8 gcw[2];
	u16 *ctl = (u16 *)&tape->caps[12];

	drive->pc_callback = ide_tape_callback;

	spin_lock_init(&tape->lock);
	drive->dsc_overlap = 1;
	if (drive->hwif->host_flags & IDE_HFLAG_NO_DSC) {
		printk(KERN_INFO "ide-tape: %s: disabling DSC overlap\n",
				 tape->name);
		drive->dsc_overlap = 0;
	}
	/* Seagate Travan drives do not support DSC overlap. */
	if (strstr((char *)&drive->id[ATA_ID_PROD], "Seagate STT3401"))
		drive->dsc_overlap = 0;
	tape->minor = minor;
	tape->name[0] = 'h';
	tape->name[1] = 't';
	tape->name[2] = '0' + minor;
	tape->chrdev_dir = IDETAPE_DIR_NONE;
	tape->pc = tape->pc_stack;

	*((u16 *)&gcw) = drive->id[ATA_ID_CONFIG];

	/* Command packet DRQ type */
	if (((gcw[0] & 0x60) >> 5) == 1)
		set_bit(IDE_AFLAG_DRQ_INTERRUPT, &drive->atapi_flags);

	idetape_get_inquiry_results(drive);
	idetape_get_mode_sense_results(drive);
	ide_tape_get_bsize_from_bdesc(drive);
	tape->user_bs_factor = 1;
	tape->buffer_size = *ctl * tape->blk_size;
	while (tape->buffer_size > 0xffff) {
		printk(KERN_NOTICE "ide-tape: decreasing stage size\n");
		*ctl /= 2;
		tape->buffer_size = *ctl * tape->blk_size;
	}
	buffer_size = tape->buffer_size;
	tape->pages_per_buffer = buffer_size / PAGE_SIZE;
	if (buffer_size % PAGE_SIZE) {
		tape->pages_per_buffer++;
		tape->excess_bh_size = PAGE_SIZE - buffer_size % PAGE_SIZE;
	}

	/* select the "best" DSC read/write polling freq */
	speed = max(*(u16 *)&tape->caps[14], *(u16 *)&tape->caps[8]);

	t = (IDETAPE_FIFO_THRESHOLD * tape->buffer_size * HZ) / (speed * 1000);

	/*
	 * Ensure that the number we got makes sense; limit it within
	 * IDETAPE_DSC_RW_MIN and IDETAPE_DSC_RW_MAX.
	 */
	tape->best_dsc_rw_freq = clamp_t(unsigned long, t, IDETAPE_DSC_RW_MIN,
					 IDETAPE_DSC_RW_MAX);
	printk(KERN_INFO "ide-tape: %s <-> %s: %dKBps, %d*%dkB buffer, "
		"%lums tDSC%s\n",
		drive->name, tape->name, *(u16 *)&tape->caps[14],
		(*(u16 *)&tape->caps[16] * 512) / tape->buffer_size,
		tape->buffer_size / 1024,
		tape->best_dsc_rw_freq * 1000 / HZ,
		drive->using_dma ? ", DMA":"");

	ide_proc_register_driver(drive, tape->driver);
}

static void ide_tape_remove(ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;

	ide_proc_unregister_driver(drive, tape->driver);

	ide_unregister_region(tape->disk);

	ide_tape_put(tape);
}

static void ide_tape_release(struct kref *kref)
{
	struct ide_tape_obj *tape = to_ide_tape(kref);
	ide_drive_t *drive = tape->drive;
	struct gendisk *g = tape->disk;

	BUG_ON(tape->merge_bh_size);

	drive->dsc_overlap = 0;
	drive->driver_data = NULL;
	device_destroy(idetape_sysfs_class, MKDEV(IDETAPE_MAJOR, tape->minor));
	device_destroy(idetape_sysfs_class,
			MKDEV(IDETAPE_MAJOR, tape->minor + 128));
	idetape_devs[tape->minor] = NULL;
	g->private_data = NULL;
	put_disk(g);
	kfree(tape);
}

#ifdef CONFIG_IDE_PROC_FS
static int proc_idetape_read_name
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_drive_t	*drive = (ide_drive_t *) data;
	idetape_tape_t	*tape = drive->driver_data;
	char		*out = page;
	int		len;

	len = sprintf(out, "%s\n", tape->name);
	PROC_IDE_READ_RETURN(page, start, off, count, eof, len);
}

static ide_proc_entry_t idetape_proc[] = {
	{ "capacity",	S_IFREG|S_IRUGO,	proc_ide_read_capacity, NULL },
	{ "name",	S_IFREG|S_IRUGO,	proc_idetape_read_name,	NULL },
	{ NULL, 0, NULL, NULL }
};
#endif

static int ide_tape_probe(ide_drive_t *);

static ide_driver_t idetape_driver = {
	.gen_driver = {
		.owner		= THIS_MODULE,
		.name		= "ide-tape",
		.bus		= &ide_bus_type,
	},
	.probe			= ide_tape_probe,
	.remove			= ide_tape_remove,
	.version		= IDETAPE_VERSION,
	.media			= ide_tape,
	.do_request		= idetape_do_request,
	.end_request		= idetape_end_request,
	.error			= __ide_error,
#ifdef CONFIG_IDE_PROC_FS
	.proc			= idetape_proc,
	.settings		= idetape_settings,
#endif
};

/* Our character device supporting functions, passed to register_chrdev. */
static const struct file_operations idetape_fops = {
	.owner		= THIS_MODULE,
	.read		= idetape_chrdev_read,
	.write		= idetape_chrdev_write,
	.ioctl		= idetape_chrdev_ioctl,
	.open		= idetape_chrdev_open,
	.release	= idetape_chrdev_release,
};

static int idetape_open(struct inode *inode, struct file *filp)
{
	struct gendisk *disk = inode->i_bdev->bd_disk;
	struct ide_tape_obj *tape;

	tape = ide_tape_get(disk);
	if (!tape)
		return -ENXIO;

	return 0;
}

static int idetape_release(struct inode *inode, struct file *filp)
{
	struct gendisk *disk = inode->i_bdev->bd_disk;
	struct ide_tape_obj *tape = ide_tape_g(disk);

	ide_tape_put(tape);

	return 0;
}

static int idetape_ioctl(struct inode *inode, struct file *file,
			unsigned int cmd, unsigned long arg)
{
	struct block_device *bdev = inode->i_bdev;
	struct ide_tape_obj *tape = ide_tape_g(bdev->bd_disk);
	ide_drive_t *drive = tape->drive;
	int err = generic_ide_ioctl(drive, file, bdev, cmd, arg);
	if (err == -EINVAL)
		err = idetape_blkdev_ioctl(drive, cmd, arg);
	return err;
}

static struct block_device_operations idetape_block_ops = {
	.owner		= THIS_MODULE,
	.open		= idetape_open,
	.release	= idetape_release,
	.ioctl		= idetape_ioctl,
};

static int ide_tape_probe(ide_drive_t *drive)
{
	idetape_tape_t *tape;
	struct gendisk *g;
	int minor;

	if (!strstr("ide-tape", drive->driver_req))
		goto failed;

	if (drive->media != ide_tape)
		goto failed;

	if (drive->id_read == 1 && !ide_check_atapi_device(drive, DRV_NAME)) {
		printk(KERN_ERR "ide-tape: %s: not supported by this version of"
				" the driver\n", drive->name);
		goto failed;
	}
	tape = kzalloc(sizeof(idetape_tape_t), GFP_KERNEL);
	if (tape == NULL) {
		printk(KERN_ERR "ide-tape: %s: Can't allocate a tape struct\n",
				drive->name);
		goto failed;
	}

	g = alloc_disk(1 << PARTN_BITS);
	if (!g)
		goto out_free_tape;

	ide_init_disk(g, drive);

	kref_init(&tape->kref);

	tape->drive = drive;
	tape->driver = &idetape_driver;
	tape->disk = g;

	g->private_data = &tape->driver;

	drive->driver_data = tape;

	mutex_lock(&idetape_ref_mutex);
	for (minor = 0; idetape_devs[minor]; minor++)
		;
	idetape_devs[minor] = tape;
	mutex_unlock(&idetape_ref_mutex);

	idetape_setup(drive, tape, minor);

	device_create_drvdata(idetape_sysfs_class, &drive->gendev,
			      MKDEV(IDETAPE_MAJOR, minor), NULL,
			      "%s", tape->name);
	device_create_drvdata(idetape_sysfs_class, &drive->gendev,
			      MKDEV(IDETAPE_MAJOR, minor + 128), NULL,
			      "n%s", tape->name);

	g->fops = &idetape_block_ops;
	ide_register_region(g);

	return 0;

out_free_tape:
	kfree(tape);
failed:
	return -ENODEV;
}

static void __exit idetape_exit(void)
{
	driver_unregister(&idetape_driver.gen_driver);
	class_destroy(idetape_sysfs_class);
	unregister_chrdev(IDETAPE_MAJOR, "ht");
}

static int __init idetape_init(void)
{
	int error = 1;
	idetape_sysfs_class = class_create(THIS_MODULE, "ide_tape");
	if (IS_ERR(idetape_sysfs_class)) {
		idetape_sysfs_class = NULL;
		printk(KERN_ERR "Unable to create sysfs class for ide tapes\n");
		error = -EBUSY;
		goto out;
	}

	if (register_chrdev(IDETAPE_MAJOR, "ht", &idetape_fops)) {
		printk(KERN_ERR "ide-tape: Failed to register chrdev"
				" interface\n");
		error = -EBUSY;
		goto out_free_class;
	}

	error = driver_register(&idetape_driver.gen_driver);
	if (error)
		goto out_free_driver;

	return 0;

out_free_driver:
	driver_unregister(&idetape_driver.gen_driver);
out_free_class:
	class_destroy(idetape_sysfs_class);
out:
	return error;
}

MODULE_ALIAS("ide:*m-tape*");
module_init(idetape_init);
module_exit(idetape_exit);
MODULE_ALIAS_CHARDEV_MAJOR(IDETAPE_MAJOR);
MODULE_DESCRIPTION("ATAPI Streaming TAPE Driver");
MODULE_LICENSE("GPL");
