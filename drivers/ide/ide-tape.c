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
#include <linux/seq_file.h>
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

/* define to see debug info */
#undef IDETAPE_DEBUG_LOG

#ifdef IDETAPE_DEBUG_LOG
#define ide_debug_log(lvl, fmt, args...) __ide_debug_log(lvl, fmt, ## args)
#else
#define ide_debug_log(lvl, fmt, args...) do {} while (0)
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

/* Structures related to the SELECT SENSE / MODE SENSE packet commands. */
#define IDETAPE_BLOCK_DESCRIPTOR	0
#define IDETAPE_CAPABILITIES_PAGE	0x2a

/*
 * Most of our global data which we need to save even as we leave the driver due
 * to an interrupt or a timer event is stored in the struct defined below.
 */
typedef struct ide_tape_obj {
	ide_drive_t		*drive;
	struct ide_driver	*driver;
	struct gendisk		*disk;
	struct device		dev;

	/* used by REQ_IDETAPE_{READ,WRITE} requests */
	struct ide_atapi_pc queued_pc;

	/*
	 * DSC polling variables.
	 *
	 * While polling for DSC we use postponed_rq to postpone the current
	 * request so that ide.c will be able to service pending requests on the
	 * other device. Note that at most we will have only one DSC (usually
	 * data transfer) request in the device request queue.
	 */
	bool postponed_rq;

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
	/* Staging buffer of buffer_size bytes */
	void *buf;
	/* The read/write cursor */
	void *cur;
	/* The number of valid bytes in buf */
	size_t valid;

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
} idetape_tape_t;

static DEFINE_MUTEX(idetape_ref_mutex);

static struct class *idetape_sysfs_class;

static void ide_tape_release(struct device *);

static struct ide_tape_obj *idetape_devs[MAX_HWIFS * MAX_DRIVES];

static struct ide_tape_obj *ide_tape_get(struct gendisk *disk, bool cdev,
					 unsigned int i)
{
	struct ide_tape_obj *tape = NULL;

	mutex_lock(&idetape_ref_mutex);

	if (cdev)
		tape = idetape_devs[i];
	else
		tape = ide_drv_g(disk, ide_tape_obj);

	if (tape) {
		if (ide_device_get(tape->drive))
			tape = NULL;
		else
			get_device(&tape->dev);
	}

	mutex_unlock(&idetape_ref_mutex);
	return tape;
}

static void ide_tape_put(struct ide_tape_obj *tape)
{
	ide_drive_t *drive = tape->drive;

	mutex_lock(&idetape_ref_mutex);
	put_device(&tape->dev);
	ide_device_put(drive);
	mutex_unlock(&idetape_ref_mutex);
}

/*
 * called on each failed packet command retry to analyze the request sense. We
 * currently do not utilize this information.
 */
static void idetape_analyze_error(ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;
	struct ide_atapi_pc *pc = drive->failed_pc;
	struct request *rq = drive->hwif->rq;
	u8 *sense = bio_data(rq->bio);

	tape->sense_key = sense[2] & 0xF;
	tape->asc       = sense[12];
	tape->ascq      = sense[13];

	ide_debug_log(IDE_DBG_FUNC,
		      "cmd: 0x%x, sense key = %x, asc = %x, ascq = %x",
		      rq->cmd[0], tape->sense_key, tape->asc, tape->ascq);

	/* correct remaining bytes to transfer */
	if (pc->flags & PC_FLAG_DMA_ERROR)
		rq->resid_len = tape->blk_size * get_unaligned_be32(&sense[3]);

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
		pc->error = IDE_DRV_ERROR_FILEMARK;
		pc->flags |= PC_FLAG_ABORT;
	}
	if (pc->c[0] == WRITE_6) {
		if ((sense[2] & 0x40) || (tape->sense_key == 0xd
		     && tape->asc == 0x0 && tape->ascq == 0x2)) {
			pc->error = IDE_DRV_ERROR_EOD;
			pc->flags |= PC_FLAG_ABORT;
		}
	}
	if (pc->c[0] == READ_6 || pc->c[0] == WRITE_6) {
		if (tape->sense_key == 8) {
			pc->error = IDE_DRV_ERROR_EOD;
			pc->flags |= PC_FLAG_ABORT;
		}
		if (!(pc->flags & PC_FLAG_ABORT) &&
		    (blk_rq_bytes(rq) - rq->resid_len))
			pc->retries = IDETAPE_MAX_PC_RETRIES + 1;
	}
}

static void ide_tape_handle_dsc(ide_drive_t *);

static int ide_tape_callback(ide_drive_t *drive, int dsc)
{
	idetape_tape_t *tape = drive->driver_data;
	struct ide_atapi_pc *pc = drive->pc;
	struct request *rq = drive->hwif->rq;
	int uptodate = pc->error ? 0 : 1;
	int err = uptodate ? 0 : IDE_DRV_ERROR_GENERAL;

	ide_debug_log(IDE_DBG_FUNC, "cmd: 0x%x, dsc: %d, err: %d", rq->cmd[0],
		      dsc, err);

	if (dsc)
		ide_tape_handle_dsc(drive);

	if (drive->failed_pc == pc)
		drive->failed_pc = NULL;

	if (pc->c[0] == REQUEST_SENSE) {
		if (uptodate)
			idetape_analyze_error(drive);
		else
			printk(KERN_ERR "ide-tape: Error in REQUEST SENSE "
					"itself - Aborting request!\n");
	} else if (pc->c[0] == READ_6 || pc->c[0] == WRITE_6) {
		unsigned int blocks =
			(blk_rq_bytes(rq) - rq->resid_len) / tape->blk_size;

		tape->avg_size += blocks * tape->blk_size;

		if (time_after_eq(jiffies, tape->avg_time + HZ)) {
			tape->avg_speed = tape->avg_size * HZ /
				(jiffies - tape->avg_time) / 1024;
			tape->avg_size = 0;
			tape->avg_time = jiffies;
		}

		tape->first_frame += blocks;

		if (pc->error) {
			uptodate = 0;
			err = pc->error;
		}
	}
	rq->errors = err;

	return uptodate;
}

/*
 * Postpone the current request so that ide.c will be able to service requests
 * from another device on the same port while we are polling for DSC.
 */
static void ide_tape_stall_queue(ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;

	ide_debug_log(IDE_DBG_FUNC, "cmd: 0x%x, dsc_poll_freq: %lu",
		      drive->hwif->rq->cmd[0], tape->dsc_poll_freq);

	tape->postponed_rq = true;

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
	ide_tape_stall_queue(drive);
}

/*
 * Packet Command Interface
 *
 * The current Packet Command is available in drive->pc, and will not change
 * until we finish handling it. Each packet command is associated with a
 * callback function that will be called when the command is finished.
 *
 * The handling will be done in three stages:
 *
 * 1. ide_tape_issue_pc will send the packet command to the drive, and will set
 * the interrupt handler to ide_pc_intr.
 *
 * 2. On each interrupt, ide_pc_intr will be called. This step will be
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

static ide_startstop_t ide_tape_issue_pc(ide_drive_t *drive,
					 struct ide_cmd *cmd,
					 struct ide_atapi_pc *pc)
{
	idetape_tape_t *tape = drive->driver_data;
	struct request *rq = drive->hwif->rq;

	if (drive->failed_pc == NULL && pc->c[0] != REQUEST_SENSE)
		drive->failed_pc = pc;

	/* Set the current packet command */
	drive->pc = pc;

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
			pc->error = IDE_DRV_ERROR_GENERAL;
		}

		drive->failed_pc = NULL;
		drive->pc_callback(drive, 0);
		ide_complete_rq(drive, -EIO, blk_rq_bytes(rq));
		return ide_stopped;
	}
	ide_debug_log(IDE_DBG_SENSE, "retry #%d, cmd: 0x%02x", pc->retries,
		      pc->c[0]);

	pc->retries++;

	return ide_issue_pc(drive, cmd);
}

/* A mode sense command is used to "sense" tape parameters. */
static void idetape_create_mode_sense_cmd(struct ide_atapi_pc *pc, u8 page_code)
{
	ide_init_pc(pc);
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
	struct ide_atapi_pc *pc = drive->pc;
	u8 stat;

	stat = hwif->tp_ops->read_status(hwif);

	if (stat & ATA_DSC) {
		if (stat & ATA_ERR) {
			/* Error detected */
			if (pc->c[0] != TEST_UNIT_READY)
				printk(KERN_ERR "ide-tape: %s: I/O error, ",
						tape->name);
			/* Retry operation */
			ide_retry_pc(drive);
			return ide_stopped;
		}
		pc->error = 0;
	} else {
		pc->error = IDE_DRV_ERROR_GENERAL;
		drive->failed_pc = NULL;
	}
	drive->pc_callback(drive, 0);
	return ide_stopped;
}

static void ide_tape_create_rw_cmd(idetape_tape_t *tape,
				   struct ide_atapi_pc *pc, struct request *rq,
				   u8 opcode)
{
	unsigned int length = blk_rq_sectors(rq) / (tape->blk_size >> 9);

	ide_init_pc(pc);
	put_unaligned(cpu_to_be32(length), (unsigned int *) &pc->c[1]);
	pc->c[1] = 1;

	if (blk_rq_bytes(rq) == tape->buffer_size)
		pc->flags |= PC_FLAG_DMA_OK;

	if (opcode == READ_6)
		pc->c[0] = READ_6;
	else if (opcode == WRITE_6) {
		pc->c[0] = WRITE_6;
		pc->flags |= PC_FLAG_WRITING;
	}

	memcpy(rq->cmd, pc->c, 12);
}

static ide_startstop_t idetape_do_request(ide_drive_t *drive,
					  struct request *rq, sector_t block)
{
	ide_hwif_t *hwif = drive->hwif;
	idetape_tape_t *tape = drive->driver_data;
	struct ide_atapi_pc *pc = NULL;
	struct ide_cmd cmd;
	u8 stat;

	ide_debug_log(IDE_DBG_RQ, "cmd: 0x%x, sector: %llu, nr_sectors: %u",
		      rq->cmd[0], (unsigned long long)blk_rq_pos(rq),
		      blk_rq_sectors(rq));

	BUG_ON(!(blk_special_request(rq) || blk_sense_request(rq)));

	/* Retry a failed packet command */
	if (drive->failed_pc && drive->pc->c[0] == REQUEST_SENSE) {
		pc = drive->failed_pc;
		goto out;
	}

	/*
	 * If the tape is still busy, postpone our request and service
	 * the other device meanwhile.
	 */
	stat = hwif->tp_ops->read_status(hwif);

	if ((drive->dev_flags & IDE_DFLAG_DSC_OVERLAP) == 0 &&
	    (rq->cmd[13] & REQ_IDETAPE_PC2) == 0)
		drive->atapi_flags |= IDE_AFLAG_IGNORE_DSC;

	if (drive->dev_flags & IDE_DFLAG_POST_RESET) {
		drive->atapi_flags |= IDE_AFLAG_IGNORE_DSC;
		drive->dev_flags &= ~IDE_DFLAG_POST_RESET;
	}

	if (!(drive->atapi_flags & IDE_AFLAG_IGNORE_DSC) &&
	    !(stat & ATA_DSC)) {
		if (!tape->postponed_rq) {
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
		ide_tape_stall_queue(drive);
		return ide_stopped;
	} else {
		drive->atapi_flags &= ~IDE_AFLAG_IGNORE_DSC;
		tape->postponed_rq = false;
	}

	if (rq->cmd[13] & REQ_IDETAPE_READ) {
		pc = &tape->queued_pc;
		ide_tape_create_rw_cmd(tape, pc, rq, READ_6);
		goto out;
	}
	if (rq->cmd[13] & REQ_IDETAPE_WRITE) {
		pc = &tape->queued_pc;
		ide_tape_create_rw_cmd(tape, pc, rq, WRITE_6);
		goto out;
	}
	if (rq->cmd[13] & REQ_IDETAPE_PC1) {
		pc = (struct ide_atapi_pc *)rq->special;
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
	/* prepare sense request for this command */
	ide_prep_sense(drive, rq);

	memset(&cmd, 0, sizeof(cmd));

	if (rq_data_dir(rq))
		cmd.tf_flags |= IDE_TFLAG_WRITE;

	cmd.rq = rq;

	ide_init_sg_cmd(&cmd, blk_rq_bytes(rq));
	ide_map_sg(drive, &cmd);

	return ide_tape_issue_pc(drive, &cmd, pc);
}

/*
 * Write a filemark if write_filemark=1. Flush the device buffers without
 * writing a filemark otherwise.
 */
static void idetape_create_write_filemark_cmd(ide_drive_t *drive,
		struct ide_atapi_pc *pc, int write_filemark)
{
	ide_init_pc(pc);
	pc->c[0] = WRITE_FILEMARKS;
	pc->c[4] = write_filemark;
	pc->flags |= PC_FLAG_WAIT_FOR_DSC;
}

static int idetape_wait_ready(ide_drive_t *drive, unsigned long timeout)
{
	idetape_tape_t *tape = drive->driver_data;
	struct gendisk *disk = tape->disk;
	int load_attempted = 0;

	/* Wait for the tape to become ready */
	set_bit(ilog2(IDE_AFLAG_MEDIUM_PRESENT), &drive->atapi_flags);
	timeout += jiffies;
	while (time_before(jiffies, timeout)) {
		if (ide_do_test_unit_ready(drive, disk) == 0)
			return 0;
		if ((tape->sense_key == 2 && tape->asc == 4 && tape->ascq == 2)
		    || (tape->asc == 0x3A)) {
			/* no media */
			if (load_attempted)
				return -ENOMEDIUM;
			ide_do_start_stop(drive, disk, IDETAPE_LU_LOAD_MASK);
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
	struct ide_tape_obj *tape = drive->driver_data;
	struct ide_atapi_pc pc;
	int rc;

	idetape_create_write_filemark_cmd(drive, &pc, 0);
	rc = ide_queue_pc_tail(drive, tape->disk, &pc, NULL, 0);
	if (rc)
		return rc;
	idetape_wait_ready(drive, 60 * 5 * HZ);
	return 0;
}

static int ide_tape_read_position(ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;
	struct ide_atapi_pc pc;
	u8 buf[20];

	ide_debug_log(IDE_DBG_FUNC, "enter");

	/* prep cmd */
	ide_init_pc(&pc);
	pc.c[0] = READ_POSITION;
	pc.req_xfer = 20;

	if (ide_queue_pc_tail(drive, tape->disk, &pc, buf, pc.req_xfer))
		return -1;

	if (!pc.error) {
		ide_debug_log(IDE_DBG_FUNC, "BOP - %s",
				(buf[0] & 0x80) ? "Yes" : "No");
		ide_debug_log(IDE_DBG_FUNC, "EOP - %s",
				(buf[0] & 0x40) ? "Yes" : "No");

		if (buf[0] & 0x4) {
			printk(KERN_INFO "ide-tape: Block location is unknown"
					 "to the tape\n");
			clear_bit(ilog2(IDE_AFLAG_ADDRESS_VALID),
				  &drive->atapi_flags);
			return -1;
		} else {
			ide_debug_log(IDE_DBG_FUNC, "Block Location: %u",
				      be32_to_cpup((__be32 *)&buf[4]));

			tape->partition = buf[1];
			tape->first_frame = be32_to_cpup((__be32 *)&buf[4]);
			set_bit(ilog2(IDE_AFLAG_ADDRESS_VALID),
				&drive->atapi_flags);
		}
	}

	return tape->first_frame;
}

static void idetape_create_locate_cmd(ide_drive_t *drive,
		struct ide_atapi_pc *pc,
		unsigned int block, u8 partition, int skip)
{
	ide_init_pc(pc);
	pc->c[0] = POSITION_TO_ELEMENT;
	pc->c[1] = 2;
	put_unaligned(cpu_to_be32(block), (unsigned int *) &pc->c[3]);
	pc->c[8] = partition;
	pc->flags |= PC_FLAG_WAIT_FOR_DSC;
}

static void __ide_tape_discard_merge_buffer(ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;

	if (tape->chrdev_dir != IDETAPE_DIR_READ)
		return;

	clear_bit(ilog2(IDE_AFLAG_FILEMARK), &drive->atapi_flags);
	tape->valid = 0;
	if (tape->buf != NULL) {
		kfree(tape->buf);
		tape->buf = NULL;
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
	struct gendisk *disk = tape->disk;
	int ret;
	struct ide_atapi_pc pc;

	if (tape->chrdev_dir == IDETAPE_DIR_READ)
		__ide_tape_discard_merge_buffer(drive);
	idetape_wait_ready(drive, 60 * 5 * HZ);
	idetape_create_locate_cmd(drive, &pc, block, partition, skip);
	ret = ide_queue_pc_tail(drive, disk, &pc, NULL, 0);
	if (ret)
		return ret;

	ret = ide_tape_read_position(drive);
	if (ret < 0)
		return ret;
	return 0;
}

static void ide_tape_discard_merge_buffer(ide_drive_t *drive,
					  int restore_position)
{
	idetape_tape_t *tape = drive->driver_data;
	int seek, position;

	__ide_tape_discard_merge_buffer(drive);
	if (restore_position) {
		position = ide_tape_read_position(drive);
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
static int idetape_queue_rw_tail(ide_drive_t *drive, int cmd, int size)
{
	idetape_tape_t *tape = drive->driver_data;
	struct request *rq;
	int ret;

	ide_debug_log(IDE_DBG_FUNC, "cmd: 0x%x, size: %d", cmd, size);

	BUG_ON(cmd != REQ_IDETAPE_READ && cmd != REQ_IDETAPE_WRITE);
	BUG_ON(size < 0 || size % tape->blk_size);

	rq = blk_get_request(drive->queue, READ, __GFP_WAIT);
	rq->cmd_type = REQ_TYPE_SPECIAL;
	rq->cmd[13] = cmd;
	rq->rq_disk = tape->disk;
	rq->__sector = tape->first_frame;

	if (size) {
		ret = blk_rq_map_kern(drive->queue, rq, tape->buf, size,
				      __GFP_WAIT);
		if (ret)
			goto out_put;
	}

	blk_execute_rq(drive->queue, tape->disk, rq, 0);

	/* calculate the number of transferred bytes and update buffer state */
	size -= rq->resid_len;
	tape->cur = tape->buf;
	if (cmd == REQ_IDETAPE_READ)
		tape->valid = size;
	else
		tape->valid = 0;

	ret = size;
	if (rq->errors == IDE_DRV_ERROR_GENERAL)
		ret = -EIO;
out_put:
	blk_put_request(rq);
	return ret;
}

static void idetape_create_inquiry_cmd(struct ide_atapi_pc *pc)
{
	ide_init_pc(pc);
	pc->c[0] = INQUIRY;
	pc->c[4] = 254;
	pc->req_xfer = 254;
}

static void idetape_create_rewind_cmd(ide_drive_t *drive,
		struct ide_atapi_pc *pc)
{
	ide_init_pc(pc);
	pc->c[0] = REZERO_UNIT;
	pc->flags |= PC_FLAG_WAIT_FOR_DSC;
}

static void idetape_create_erase_cmd(struct ide_atapi_pc *pc)
{
	ide_init_pc(pc);
	pc->c[0] = ERASE;
	pc->c[1] = 1;
	pc->flags |= PC_FLAG_WAIT_FOR_DSC;
}

static void idetape_create_space_cmd(struct ide_atapi_pc *pc, int count, u8 cmd)
{
	ide_init_pc(pc);
	pc->c[0] = SPACE;
	put_unaligned(cpu_to_be32(count), (unsigned int *) &pc->c[1]);
	pc->c[1] = cmd;
	pc->flags |= PC_FLAG_WAIT_FOR_DSC;
}

static void ide_tape_flush_merge_buffer(ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;

	if (tape->chrdev_dir != IDETAPE_DIR_WRITE) {
		printk(KERN_ERR "ide-tape: bug: Trying to empty merge buffer"
				" but we are not writing.\n");
		return;
	}
	if (tape->buf) {
		size_t aligned = roundup(tape->valid, tape->blk_size);

		memset(tape->cur, 0, aligned - tape->valid);
		idetape_queue_rw_tail(drive, REQ_IDETAPE_WRITE, aligned);
		kfree(tape->buf);
		tape->buf = NULL;
	}
	tape->chrdev_dir = IDETAPE_DIR_NONE;
}

static int idetape_init_rw(ide_drive_t *drive, int dir)
{
	idetape_tape_t *tape = drive->driver_data;
	int rc;

	BUG_ON(dir != IDETAPE_DIR_READ && dir != IDETAPE_DIR_WRITE);

	if (tape->chrdev_dir == dir)
		return 0;

	if (tape->chrdev_dir == IDETAPE_DIR_READ)
		ide_tape_discard_merge_buffer(drive, 1);
	else if (tape->chrdev_dir == IDETAPE_DIR_WRITE) {
		ide_tape_flush_merge_buffer(drive);
		idetape_flush_tape_buffers(drive);
	}

	if (tape->buf || tape->valid) {
		printk(KERN_ERR "ide-tape: valid should be 0 now\n");
		tape->valid = 0;
	}

	tape->buf = kmalloc(tape->buffer_size, GFP_KERNEL);
	if (!tape->buf)
		return -ENOMEM;
	tape->chrdev_dir = dir;
	tape->cur = tape->buf;

	/*
	 * Issue a 0 rw command to ensure that DSC handshake is
	 * switched from completion mode to buffer available mode.  No
	 * point in issuing this if DSC overlap isn't supported, some
	 * drives (Seagate STT3401A) will return an error.
	 */
	if (drive->dev_flags & IDE_DFLAG_DSC_OVERLAP) {
		int cmd = dir == IDETAPE_DIR_READ ? REQ_IDETAPE_READ
						  : REQ_IDETAPE_WRITE;

		rc = idetape_queue_rw_tail(drive, cmd, 0);
		if (rc < 0) {
			kfree(tape->buf);
			tape->buf = NULL;
			tape->chrdev_dir = IDETAPE_DIR_NONE;
			return rc;
		}
	}

	return 0;
}

static void idetape_pad_zeros(ide_drive_t *drive, int bcount)
{
	idetape_tape_t *tape = drive->driver_data;

	memset(tape->buf, 0, tape->buffer_size);

	while (bcount) {
		unsigned int count = min(tape->buffer_size, bcount);

		idetape_queue_rw_tail(drive, REQ_IDETAPE_WRITE, count);
		bcount -= count;
	}
}

/*
 * Rewinds the tape to the Beginning Of the current Partition (BOP). We
 * currently support only one partition.
 */
static int idetape_rewind_tape(ide_drive_t *drive)
{
	struct ide_tape_obj *tape = drive->driver_data;
	struct gendisk *disk = tape->disk;
	struct ide_atapi_pc pc;
	int ret;

	ide_debug_log(IDE_DBG_FUNC, "enter");

	idetape_create_rewind_cmd(drive, &pc);
	ret = ide_queue_pc_tail(drive, disk, &pc, NULL, 0);
	if (ret)
		return ret;

	ret = ide_tape_read_position(drive);
	if (ret < 0)
		return ret;
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

	ide_debug_log(IDE_DBG_FUNC, "cmd: 0x%04x", cmd);

	switch (cmd) {
	case 0x0340:
		if (copy_from_user(&config, argp, sizeof(config)))
			return -EFAULT;
		tape->best_dsc_rw_freq = config.dsc_rw_frequency;
		break;
	case 0x0350:
		memset(&config, 0, sizeof(config));
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
	struct gendisk *disk = tape->disk;
	struct ide_atapi_pc pc;
	int retval, count = 0;
	int sprev = !!(tape->caps[4] & 0x20);


	ide_debug_log(IDE_DBG_FUNC, "mt_op: %d, mt_count: %d", mt_op, mt_count);

	if (mt_count == 0)
		return 0;
	if (MTBSF == mt_op || MTBSFM == mt_op) {
		if (!sprev)
			return -EIO;
		mt_count = -mt_count;
	}

	if (tape->chrdev_dir == IDETAPE_DIR_READ) {
		tape->valid = 0;
		if (test_and_clear_bit(ilog2(IDE_AFLAG_FILEMARK),
				       &drive->atapi_flags))
			++count;
		ide_tape_discard_merge_buffer(drive, 0);
	}

	switch (mt_op) {
	case MTFSF:
	case MTBSF:
		idetape_create_space_cmd(&pc, mt_count - count,
					 IDETAPE_SPACE_OVER_FILEMARK);
		return ide_queue_pc_tail(drive, disk, &pc, NULL, 0);
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
	struct ide_tape_obj *tape = file->private_data;
	ide_drive_t *drive = tape->drive;
	size_t done = 0;
	ssize_t ret = 0;
	int rc;

	ide_debug_log(IDE_DBG_FUNC, "count %Zd", count);

	if (tape->chrdev_dir != IDETAPE_DIR_READ) {
		if (test_bit(ilog2(IDE_AFLAG_DETECT_BS), &drive->atapi_flags))
			if (count > tape->blk_size &&
			    (count % tape->blk_size) == 0)
				tape->user_bs_factor = count / tape->blk_size;
	}

	rc = idetape_init_rw(drive, IDETAPE_DIR_READ);
	if (rc < 0)
		return rc;

	while (done < count) {
		size_t todo;

		/* refill if staging buffer is empty */
		if (!tape->valid) {
			/* If we are at a filemark, nothing more to read */
			if (test_bit(ilog2(IDE_AFLAG_FILEMARK),
				     &drive->atapi_flags))
				break;
			/* read */
			if (idetape_queue_rw_tail(drive, REQ_IDETAPE_READ,
						  tape->buffer_size) <= 0)
				break;
		}

		/* copy out */
		todo = min_t(size_t, count - done, tape->valid);
		if (copy_to_user(buf + done, tape->cur, todo))
			ret = -EFAULT;

		tape->cur += todo;
		tape->valid -= todo;
		done += todo;
	}

	if (!done && test_bit(ilog2(IDE_AFLAG_FILEMARK), &drive->atapi_flags)) {
		idetape_space_over_filemarks(drive, MTFSF, 1);
		return 0;
	}

	return ret ? ret : done;
}

static ssize_t idetape_chrdev_write(struct file *file, const char __user *buf,
				     size_t count, loff_t *ppos)
{
	struct ide_tape_obj *tape = file->private_data;
	ide_drive_t *drive = tape->drive;
	size_t done = 0;
	ssize_t ret = 0;
	int rc;

	/* The drive is write protected. */
	if (tape->write_prot)
		return -EACCES;

	ide_debug_log(IDE_DBG_FUNC, "count %Zd", count);

	/* Initialize write operation */
	rc = idetape_init_rw(drive, IDETAPE_DIR_WRITE);
	if (rc < 0)
		return rc;

	while (done < count) {
		size_t todo;

		/* flush if staging buffer is full */
		if (tape->valid == tape->buffer_size &&
		    idetape_queue_rw_tail(drive, REQ_IDETAPE_WRITE,
					  tape->buffer_size) <= 0)
			return rc;

		/* copy in */
		todo = min_t(size_t, count - done,
			     tape->buffer_size - tape->valid);
		if (copy_from_user(tape->cur, buf + done, todo))
			ret = -EFAULT;

		tape->cur += todo;
		tape->valid += todo;
		done += todo;
	}

	return ret ? ret : done;
}

static int idetape_write_filemark(ide_drive_t *drive)
{
	struct ide_tape_obj *tape = drive->driver_data;
	struct ide_atapi_pc pc;

	/* Write a filemark */
	idetape_create_write_filemark_cmd(drive, &pc, 1);
	if (ide_queue_pc_tail(drive, tape->disk, &pc, NULL, 0)) {
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
	struct gendisk *disk = tape->disk;
	struct ide_atapi_pc pc;
	int i, retval;

	ide_debug_log(IDE_DBG_FUNC, "MTIOCTOP ioctl: mt_op: %d, mt_count: %d",
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
		return ide_do_start_stop(drive, disk, IDETAPE_LU_LOAD_MASK);
	case MTUNLOAD:
	case MTOFFL:
		/*
		 * If door is locked, attempt to unlock before
		 * attempting to eject.
		 */
		if (tape->door_locked) {
			if (!ide_set_media_lock(drive, disk, 0))
				tape->door_locked = DOOR_UNLOCKED;
		}
		ide_tape_discard_merge_buffer(drive, 0);
		retval = ide_do_start_stop(drive, disk, !IDETAPE_LU_LOAD_MASK);
		if (!retval)
			clear_bit(ilog2(IDE_AFLAG_MEDIUM_PRESENT),
				  &drive->atapi_flags);
		return retval;
	case MTNOP:
		ide_tape_discard_merge_buffer(drive, 0);
		return idetape_flush_tape_buffers(drive);
	case MTRETEN:
		ide_tape_discard_merge_buffer(drive, 0);
		return ide_do_start_stop(drive, disk,
			IDETAPE_LU_RETENSION_MASK | IDETAPE_LU_LOAD_MASK);
	case MTEOM:
		idetape_create_space_cmd(&pc, 0, IDETAPE_SPACE_TO_EOD);
		return ide_queue_pc_tail(drive, disk, &pc, NULL, 0);
	case MTERASE:
		(void)idetape_rewind_tape(drive);
		idetape_create_erase_cmd(&pc);
		return ide_queue_pc_tail(drive, disk, &pc, NULL, 0);
	case MTSETBLK:
		if (mt_count) {
			if (mt_count < tape->blk_size ||
			    mt_count % tape->blk_size)
				return -EIO;
			tape->user_bs_factor = mt_count / tape->blk_size;
			clear_bit(ilog2(IDE_AFLAG_DETECT_BS),
				  &drive->atapi_flags);
		} else
			set_bit(ilog2(IDE_AFLAG_DETECT_BS),
				&drive->atapi_flags);
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
		retval = ide_set_media_lock(drive, disk, 1);
		if (retval)
			return retval;
		tape->door_locked = DOOR_EXPLICITLY_LOCKED;
		return 0;
	case MTUNLOCK:
		retval = ide_set_media_lock(drive, disk, 0);
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
	struct ide_tape_obj *tape = file->private_data;
	ide_drive_t *drive = tape->drive;
	struct mtop mtop;
	struct mtget mtget;
	struct mtpos mtpos;
	int block_offset = 0, position = tape->first_frame;
	void __user *argp = (void __user *)arg;

	ide_debug_log(IDE_DBG_FUNC, "cmd: 0x%x", cmd);

	if (tape->chrdev_dir == IDETAPE_DIR_WRITE) {
		ide_tape_flush_merge_buffer(drive);
		idetape_flush_tape_buffers(drive);
	}
	if (cmd == MTIOCGET || cmd == MTIOCPOS) {
		block_offset = tape->valid /
			(tape->blk_size * tape->user_bs_factor);
		position = ide_tape_read_position(drive);
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
	u8 buf[12];

	idetape_create_mode_sense_cmd(&pc, IDETAPE_BLOCK_DESCRIPTOR);
	if (ide_queue_pc_tail(drive, tape->disk, &pc, buf, pc.req_xfer)) {
		printk(KERN_ERR "ide-tape: Can't get block descriptor\n");
		if (tape->blk_size == 0) {
			printk(KERN_WARNING "ide-tape: Cannot deal with zero "
					    "block size, assuming 32k\n");
			tape->blk_size = 32768;
		}
		return;
	}
	tape->blk_size = (buf[4 + 5] << 16) +
				(buf[4 + 6] << 8)  +
				 buf[4 + 7];
	tape->drv_write_prot = (buf[2] & 0x80) >> 7;

	ide_debug_log(IDE_DBG_FUNC, "blk_size: %d, write_prot: %d",
		      tape->blk_size, tape->drv_write_prot);
}

static int idetape_chrdev_open(struct inode *inode, struct file *filp)
{
	unsigned int minor = iminor(inode), i = minor & ~0xc0;
	ide_drive_t *drive;
	idetape_tape_t *tape;
	int retval;

	if (i >= MAX_HWIFS * MAX_DRIVES)
		return -ENXIO;

	lock_kernel();
	tape = ide_tape_get(NULL, true, i);
	if (!tape) {
		unlock_kernel();
		return -ENXIO;
	}

	drive = tape->drive;
	filp->private_data = tape;

	ide_debug_log(IDE_DBG_FUNC, "enter");

	/*
	 * We really want to do nonseekable_open(inode, filp); here, but some
	 * versions of tar incorrectly call lseek on tapes and bail out if that
	 * fails.  So we disallow pread() and pwrite(), but permit lseeks.
	 */
	filp->f_mode &= ~(FMODE_PREAD | FMODE_PWRITE);


	if (test_and_set_bit(ilog2(IDE_AFLAG_BUSY), &drive->atapi_flags)) {
		retval = -EBUSY;
		goto out_put_tape;
	}

	retval = idetape_wait_ready(drive, 60 * HZ);
	if (retval) {
		clear_bit(ilog2(IDE_AFLAG_BUSY), &drive->atapi_flags);
		printk(KERN_ERR "ide-tape: %s: drive not ready\n", tape->name);
		goto out_put_tape;
	}

	ide_tape_read_position(drive);
	if (!test_bit(ilog2(IDE_AFLAG_ADDRESS_VALID), &drive->atapi_flags))
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
			clear_bit(ilog2(IDE_AFLAG_BUSY), &drive->atapi_flags);
			retval = -EROFS;
			goto out_put_tape;
		}
	}

	/* Lock the tape drive door so user can't eject. */
	if (tape->chrdev_dir == IDETAPE_DIR_NONE) {
		if (!ide_set_media_lock(drive, tape->disk, 1)) {
			if (tape->door_locked != DOOR_EXPLICITLY_LOCKED)
				tape->door_locked = DOOR_LOCKED;
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
	tape->buf = kmalloc(tape->buffer_size, GFP_KERNEL);
	if (tape->buf != NULL) {
		idetape_pad_zeros(drive, tape->blk_size *
				(tape->user_bs_factor - 1));
		kfree(tape->buf);
		tape->buf = NULL;
	}
	idetape_write_filemark(drive);
	idetape_flush_tape_buffers(drive);
	idetape_flush_tape_buffers(drive);
}

static int idetape_chrdev_release(struct inode *inode, struct file *filp)
{
	struct ide_tape_obj *tape = filp->private_data;
	ide_drive_t *drive = tape->drive;
	unsigned int minor = iminor(inode);

	lock_kernel();
	tape = drive->driver_data;

	ide_debug_log(IDE_DBG_FUNC, "enter");

	if (tape->chrdev_dir == IDETAPE_DIR_WRITE)
		idetape_write_release(drive, minor);
	if (tape->chrdev_dir == IDETAPE_DIR_READ) {
		if (minor < 128)
			ide_tape_discard_merge_buffer(drive, 1);
	}

	if (minor < 128 && test_bit(ilog2(IDE_AFLAG_MEDIUM_PRESENT),
				    &drive->atapi_flags))
		(void) idetape_rewind_tape(drive);

	if (tape->chrdev_dir == IDETAPE_DIR_NONE) {
		if (tape->door_locked == DOOR_LOCKED) {
			if (!ide_set_media_lock(drive, tape->disk, 0))
				tape->door_locked = DOOR_UNLOCKED;
		}
	}
	clear_bit(ilog2(IDE_AFLAG_BUSY), &drive->atapi_flags);
	ide_tape_put(tape);
	unlock_kernel();
	return 0;
}

static void idetape_get_inquiry_results(ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;
	struct ide_atapi_pc pc;
	u8 pc_buf[256];
	char fw_rev[4], vendor_id[8], product_id[16];

	idetape_create_inquiry_cmd(&pc);
	if (ide_queue_pc_tail(drive, tape->disk, &pc, pc_buf, pc.req_xfer)) {
		printk(KERN_ERR "ide-tape: %s: can't get INQUIRY results\n",
				tape->name);
		return;
	}
	memcpy(vendor_id, &pc_buf[8], 8);
	memcpy(product_id, &pc_buf[16], 16);
	memcpy(fw_rev, &pc_buf[32], 4);

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
	u8 buf[24], *caps;
	u8 speed, max_speed;

	idetape_create_mode_sense_cmd(&pc, IDETAPE_CAPABILITIES_PAGE);
	if (ide_queue_pc_tail(drive, tape->disk, &pc, buf, pc.req_xfer)) {
		printk(KERN_ERR "ide-tape: Can't get tape parameters - assuming"
				" some default values\n");
		tape->blk_size = 512;
		put_unaligned(52,   (u16 *)&tape->caps[12]);
		put_unaligned(540,  (u16 *)&tape->caps[14]);
		put_unaligned(6*52, (u16 *)&tape->caps[16]);
		return;
	}
	caps = buf + 4 + buf[3];

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

	/* device lacks locking support according to capabilities page */
	if ((caps[6] & 1) == 0)
		drive->dev_flags &= ~IDE_DFLAG_DOORLOCKING;

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

#define ide_tape_devset_rw_field(_name, _field) \
ide_tape_devset_get(_name, _field) \
ide_tape_devset_set(_name, _field) \
IDE_DEVSET(_name, DS_SYNC, get_##_name, set_##_name)

#define ide_tape_devset_r_field(_name, _field) \
ide_tape_devset_get(_name, _field) \
IDE_DEVSET(_name, 0, get_##_name, NULL)

static int mulf_tdsc(ide_drive_t *drive)	{ return 1000; }
static int divf_tdsc(ide_drive_t *drive)	{ return   HZ; }
static int divf_buffer(ide_drive_t *drive)	{ return    2; }
static int divf_buffer_size(ide_drive_t *drive)	{ return 1024; }

ide_devset_rw_flag(dsc_overlap, IDE_DFLAG_DSC_OVERLAP);

ide_tape_devset_rw_field(tdsc, best_dsc_rw_freq);

ide_tape_devset_r_field(avg_speed, avg_speed);
ide_tape_devset_r_field(speed, caps[14]);
ide_tape_devset_r_field(buffer, caps[16]);
ide_tape_devset_r_field(buffer_size, buffer_size);

static const struct ide_proc_devset idetape_settings[] = {
	__IDE_PROC_DEVSET(avg_speed,	0, 0xffff, NULL, NULL),
	__IDE_PROC_DEVSET(buffer,	0, 0xffff, NULL, divf_buffer),
	__IDE_PROC_DEVSET(buffer_size,	0, 0xffff, NULL, divf_buffer_size),
	__IDE_PROC_DEVSET(dsc_overlap,	0,      1, NULL, NULL),
	__IDE_PROC_DEVSET(speed,	0, 0xffff, NULL, NULL),
	__IDE_PROC_DEVSET(tdsc,		IDETAPE_DSC_RW_MIN, IDETAPE_DSC_RW_MAX,
					mulf_tdsc, divf_tdsc),
	{ NULL },
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
	u16 *ctl = (u16 *)&tape->caps[12];

	ide_debug_log(IDE_DBG_FUNC, "minor: %d", minor);

	drive->pc_callback = ide_tape_callback;

	drive->dev_flags |= IDE_DFLAG_DSC_OVERLAP;

	if (drive->hwif->host_flags & IDE_HFLAG_NO_DSC) {
		printk(KERN_INFO "ide-tape: %s: disabling DSC overlap\n",
				 tape->name);
		drive->dev_flags &= ~IDE_DFLAG_DSC_OVERLAP;
	}

	/* Seagate Travan drives do not support DSC overlap. */
	if (strstr((char *)&drive->id[ATA_ID_PROD], "Seagate STT3401"))
		drive->dev_flags &= ~IDE_DFLAG_DSC_OVERLAP;

	tape->minor = minor;
	tape->name[0] = 'h';
	tape->name[1] = 't';
	tape->name[2] = '0' + minor;
	tape->chrdev_dir = IDETAPE_DIR_NONE;

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
		(drive->dev_flags & IDE_DFLAG_USING_DMA) ? ", DMA" : "");

	ide_proc_register_driver(drive, tape->driver);
}

static void ide_tape_remove(ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;

	ide_proc_unregister_driver(drive, tape->driver);
	device_del(&tape->dev);
	ide_unregister_region(tape->disk);

	mutex_lock(&idetape_ref_mutex);
	put_device(&tape->dev);
	mutex_unlock(&idetape_ref_mutex);
}

static void ide_tape_release(struct device *dev)
{
	struct ide_tape_obj *tape = to_ide_drv(dev, ide_tape_obj);
	ide_drive_t *drive = tape->drive;
	struct gendisk *g = tape->disk;

	BUG_ON(tape->valid);

	drive->dev_flags &= ~IDE_DFLAG_DSC_OVERLAP;
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
static int idetape_name_proc_show(struct seq_file *m, void *v)
{
	ide_drive_t	*drive = (ide_drive_t *) m->private;
	idetape_tape_t	*tape = drive->driver_data;

	seq_printf(m, "%s\n", tape->name);
	return 0;
}

static int idetape_name_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, idetape_name_proc_show, PDE(inode)->data);
}

static const struct file_operations idetape_name_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= idetape_name_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static ide_proc_entry_t idetape_proc[] = {
	{ "capacity",	S_IFREG|S_IRUGO,	&ide_capacity_proc_fops	},
	{ "name",	S_IFREG|S_IRUGO,	&idetape_name_proc_fops	},
	{}
};

static ide_proc_entry_t *ide_tape_proc_entries(ide_drive_t *drive)
{
	return idetape_proc;
}

static const struct ide_proc_devset *ide_tape_proc_devsets(ide_drive_t *drive)
{
	return idetape_settings;
}
#endif

static int ide_tape_probe(ide_drive_t *);

static struct ide_driver idetape_driver = {
	.gen_driver = {
		.owner		= THIS_MODULE,
		.name		= "ide-tape",
		.bus		= &ide_bus_type,
	},
	.probe			= ide_tape_probe,
	.remove			= ide_tape_remove,
	.version		= IDETAPE_VERSION,
	.do_request		= idetape_do_request,
#ifdef CONFIG_IDE_PROC_FS
	.proc_entries		= ide_tape_proc_entries,
	.proc_devsets		= ide_tape_proc_devsets,
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

static int idetape_open(struct block_device *bdev, fmode_t mode)
{
	struct ide_tape_obj *tape = ide_tape_get(bdev->bd_disk, false, 0);

	if (!tape)
		return -ENXIO;

	return 0;
}

static int idetape_release(struct gendisk *disk, fmode_t mode)
{
	struct ide_tape_obj *tape = ide_drv_g(disk, ide_tape_obj);

	ide_tape_put(tape);
	return 0;
}

static int idetape_ioctl(struct block_device *bdev, fmode_t mode,
			unsigned int cmd, unsigned long arg)
{
	struct ide_tape_obj *tape = ide_drv_g(bdev->bd_disk, ide_tape_obj);
	ide_drive_t *drive = tape->drive;
	int err = generic_ide_ioctl(drive, bdev, cmd, arg);
	if (err == -EINVAL)
		err = idetape_blkdev_ioctl(drive, cmd, arg);
	return err;
}

static const struct block_device_operations idetape_block_ops = {
	.owner		= THIS_MODULE,
	.open		= idetape_open,
	.release	= idetape_release,
	.locked_ioctl	= idetape_ioctl,
};

static int ide_tape_probe(ide_drive_t *drive)
{
	idetape_tape_t *tape;
	struct gendisk *g;
	int minor;

	ide_debug_log(IDE_DBG_FUNC, "enter");

	if (!strstr(DRV_NAME, drive->driver_req))
		goto failed;

	if (drive->media != ide_tape)
		goto failed;

	if ((drive->dev_flags & IDE_DFLAG_ID_READ) &&
	    ide_check_atapi_device(drive, DRV_NAME) == 0) {
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

	tape->dev.parent = &drive->gendev;
	tape->dev.release = ide_tape_release;
	dev_set_name(&tape->dev, dev_name(&drive->gendev));

	if (device_register(&tape->dev))
		goto out_free_disk;

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

	device_create(idetape_sysfs_class, &drive->gendev,
		      MKDEV(IDETAPE_MAJOR, minor), NULL, "%s", tape->name);
	device_create(idetape_sysfs_class, &drive->gendev,
		      MKDEV(IDETAPE_MAJOR, minor + 128), NULL,
		      "n%s", tape->name);

	g->fops = &idetape_block_ops;
	ide_register_region(g);

	return 0;

out_free_disk:
	put_disk(g);
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
