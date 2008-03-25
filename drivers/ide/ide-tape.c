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
	/* buffer alloc info (pc_stack & rq_stack) */
	DBG_PCRQ_STACK =	(1 << 4),
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
 * Pipelined mode parameters.
 *
 * We try to use the minimum number of stages which is enough to keep the tape
 * constantly streaming. To accomplish that, we implement a feedback loop around
 * the maximum number of stages:
 *
 * We start from MIN maximum stages (we will not even use MIN stages if we don't
 * need them), increment it by RATE*(MAX-MIN) whenever we sense that the
 * pipeline is empty, until we reach the optimum value or until we reach MAX.
 *
 * Setting the following parameter to 0 is illegal: the pipelined mode cannot be
 * disabled (idetape_calculate_speeds() divides by tape->max_stages.)
 */
#define IDETAPE_MIN_PIPELINE_STAGES	  1
#define IDETAPE_MAX_PIPELINE_STAGES	400
#define IDETAPE_INCREASE_STAGES_RATE	 20

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
 *	In various places in the driver, we need to allocate storage
 *	for packet commands and requests, which will remain valid while
 *	we leave the driver to wait for an interrupt or a timeout event.
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

/* Read/Write error simulation */
#define SIMULATE_ERRORS			0

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

typedef struct idetape_packet_command_s {
	/* Actual packet bytes */
	u8 c[12];
	/* On each retry, we increment retries */
	int retries;
	/* Error code */
	int error;
	/* Bytes to transfer */
	int request_transfer;
	/* Bytes actually transferred */
	int actually_transferred;
	/* Size of our data buffer */
	int buffer_size;
	struct idetape_bh *bh;
	char *b_data;
	int b_count;
	/* Data buffer */
	u8 *buffer;
	/* Pointer into the above buffer */
	u8 *current_position;
	/* Called when this packet command is completed */
	ide_startstop_t (*callback) (ide_drive_t *);
	/* Temporary buffer */
	u8 pc_buffer[IDETAPE_PC_BUFFER_SIZE];
	/* Status/Action bit flags: long for set_bit */
	unsigned long flags;
} idetape_pc_t;

/*
 *	Packet command flag bits.
 */
/* Set when an error is considered normal - We won't retry */
#define	PC_ABORT			0
/* 1 When polling for DSC on a media access command */
#define PC_WAIT_FOR_DSC			1
/* 1 when we prefer to use DMA if possible */
#define PC_DMA_RECOMMENDED		2
/* 1 while DMA in progress */
#define	PC_DMA_IN_PROGRESS		3
/* 1 when encountered problem during DMA */
#define	PC_DMA_ERROR			4
/* Data direction */
#define	PC_WRITING			5

/* A pipeline stage. */
typedef struct idetape_stage_s {
	struct request rq;			/* The corresponding request */
	struct idetape_bh *bh;			/* The data buffers */
	struct idetape_stage_s *next;		/* Pointer to the next stage */
} idetape_stage_t;

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
	idetape_pc_t *pc;
	/* Last failed packet command */
	idetape_pc_t *failed_pc;
	/* Packet command stack */
	idetape_pc_t pc_stack[IDETAPE_PC_STACK];
	/* Next free packet command storage space */
	int pc_stack_index;
	struct request rq_stack[IDETAPE_PC_STACK];
	/* We implement a circular array */
	int rq_stack_index;

	/*
	 * DSC polling variables.
	 *
	 * While polling for DSC we use postponed_rq to postpone the current
	 * request so that ide.c will be able to service pending requests on the
	 * other device. Note that at most we will have only one DSC (usually
	 * data transfer) request in the device request queue. Additional
	 * requests can be queued in our internal pipeline, but they will be
	 * visible to ide.c only one at a time.
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
	 * In the pipelined operation mode, we use our internal pipeline
	 * structure to hold more data requests. The data buffer size is chosen
	 * based on the tape's recommendation.
	 */
	/* ptr to the request which is waiting in the device request queue */
	struct request *active_data_rq;
	/* Data buffer size chosen based on the tape's recommendation */
	int stage_size;
	idetape_stage_t *merge_stage;
	int merge_stage_size;
	struct idetape_bh *bh;
	char *b_data;
	int b_count;

	/*
	 * Pipeline parameters.
	 *
	 * To accomplish non-pipelined mode, we simply set the following
	 * variables to zero (or NULL, where appropriate).
	 */
	/* Number of currently used stages */
	int nr_stages;
	/* Number of pending stages */
	int nr_pending_stages;
	/* We will not allocate more than this number of stages */
	int max_stages, min_pipeline, max_pipeline;
	/* The first stage which will be removed from the pipeline */
	idetape_stage_t *first_stage;
	/* The currently active stage */
	idetape_stage_t *active_stage;
	/* Will be serviced after the currently active request */
	idetape_stage_t *next_stage;
	/* New requests will be added to the pipeline here */
	idetape_stage_t *last_stage;
	/* Optional free stage which we can use */
	idetape_stage_t *cache_stage;
	int pages_per_stage;
	/* Wasted space in each stage */
	int excess_bh_size;

	/* Status/Action flags: long for set_bit */
	unsigned long flags;
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

	/*
	 * Limit the number of times a request can be postponed, to avoid an
	 * infinite postpone deadlock.
	 */
	int postpone_cnt;

	/*
	 * Measures number of frames:
	 *
	 * 1. written/read to/from the driver pipeline (pipeline_head).
	 * 2. written/read to/from the tape buffers (idetape_bh).
	 * 3. written/read by the tape to/from the media (tape_head).
	 */
	int pipeline_head;
	int buffer_head;
	int tape_head;
	int last_tape_head;

	/* Speed control at the tape buffers input/output */
	unsigned long insert_time;
	int insert_size;
	int insert_speed;
	int max_insert_speed;
	int measure_insert_time;

	/* Speed regulation negative feedback loop */
	int speed_control;
	int pipeline_head_speed;
	int controlled_pipeline_head_speed;
	int uncontrolled_pipeline_head_speed;
	int controlled_last_pipeline_head;
	unsigned long uncontrolled_pipeline_head_time;
	unsigned long controlled_pipeline_head_time;
	int controlled_previous_pipeline_head;
	int uncontrolled_previous_pipeline_head;
	unsigned long controlled_previous_head_time;
	unsigned long uncontrolled_previous_head_time;
	int restart_speed_control_req;

	u32 debug_mask;
} idetape_tape_t;

static DEFINE_MUTEX(idetape_ref_mutex);

static struct class *idetape_sysfs_class;

#define to_ide_tape(obj) container_of(obj, struct ide_tape_obj, kref)

#define ide_tape_g(disk) \
	container_of((disk)->private_data, struct ide_tape_obj, driver)

static struct ide_tape_obj *ide_tape_get(struct gendisk *disk)
{
	struct ide_tape_obj *tape = NULL;

	mutex_lock(&idetape_ref_mutex);
	tape = ide_tape_g(disk);
	if (tape)
		kref_get(&tape->kref);
	mutex_unlock(&idetape_ref_mutex);
	return tape;
}

static void ide_tape_release(struct kref *);

static void ide_tape_put(struct ide_tape_obj *tape)
{
	mutex_lock(&idetape_ref_mutex);
	kref_put(&tape->kref, ide_tape_release);
	mutex_unlock(&idetape_ref_mutex);
}

/* Tape door status */
#define DOOR_UNLOCKED			0
#define DOOR_LOCKED			1
#define DOOR_EXPLICITLY_LOCKED		2

/*
 *	Tape flag bits values.
 */
#define IDETAPE_IGNORE_DSC		0
#define IDETAPE_ADDRESS_VALID		1	/* 0 When the tape position is unknown */
#define IDETAPE_BUSY			2	/* Device already opened */
#define IDETAPE_PIPELINE_ERROR		3	/* Error detected in a pipeline stage */
#define IDETAPE_DETECT_BS		4	/* Attempt to auto-detect the current user block size */
#define IDETAPE_FILEMARK		5	/* Currently on a filemark */
#define IDETAPE_DRQ_INTERRUPT		6	/* DRQ interrupt device */
#define IDETAPE_READ_ERROR		7
#define IDETAPE_PIPELINE_ACTIVE		8	/* pipeline active */
/* 0 = no tape is loaded, so we don't rewind after ejecting */
#define IDETAPE_MEDIUM_PRESENT		9

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
#define	IDETAPE_ERROR_GENERAL		101
#define	IDETAPE_ERROR_FILEMARK		102
#define	IDETAPE_ERROR_EOD		103

/* Structures related to the SELECT SENSE / MODE SENSE packet commands. */
#define IDETAPE_BLOCK_DESCRIPTOR	0
#define	IDETAPE_CAPABILITIES_PAGE	0x2a

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

/*
 * Too bad. The drive wants to send us data which we are not ready to accept.
 * Just throw it away.
 */
static void idetape_discard_data(ide_drive_t *drive, unsigned int bcount)
{
	while (bcount--)
		(void) HWIF(drive)->INB(IDE_DATA_REG);
}

static void idetape_input_buffers(ide_drive_t *drive, idetape_pc_t *pc,
				  unsigned int bcount)
{
	struct idetape_bh *bh = pc->bh;
	int count;

	while (bcount) {
		if (bh == NULL) {
			printk(KERN_ERR "ide-tape: bh == NULL in "
				"idetape_input_buffers\n");
			idetape_discard_data(drive, bcount);
			return;
		}
		count = min(
			(unsigned int)(bh->b_size - atomic_read(&bh->b_count)),
			bcount);
		HWIF(drive)->atapi_input_bytes(drive, bh->b_data +
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

static void idetape_output_buffers(ide_drive_t *drive, idetape_pc_t *pc,
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
		HWIF(drive)->atapi_output_bytes(drive, pc->b_data, count);
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

static void idetape_update_buffers(idetape_pc_t *pc)
{
	struct idetape_bh *bh = pc->bh;
	int count;
	unsigned int bcount = pc->actually_transferred;

	if (test_bit(PC_WRITING, &pc->flags))
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
static idetape_pc_t *idetape_next_pc_storage(ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;

	debug_log(DBG_PCRQ_STACK, "pc_stack_index=%d\n", tape->pc_stack_index);

	if (tape->pc_stack_index == IDETAPE_PC_STACK)
		tape->pc_stack_index = 0;
	return (&tape->pc_stack[tape->pc_stack_index++]);
}

/*
 *	idetape_next_rq_storage is used along with idetape_next_pc_storage.
 *	Since we queue packet commands in the request queue, we need to
 *	allocate a request, along with the allocation of a packet command.
 */

/**************************************************************
 *                                                            *
 *  This should get fixed to use kmalloc(.., GFP_ATOMIC)      *
 *  followed later on by kfree().   -ml                       *
 *                                                            *
 **************************************************************/

static struct request *idetape_next_rq_storage(ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;

	debug_log(DBG_PCRQ_STACK, "rq_stack_index=%d\n", tape->rq_stack_index);

	if (tape->rq_stack_index == IDETAPE_PC_STACK)
		tape->rq_stack_index = 0;
	return (&tape->rq_stack[tape->rq_stack_index++]);
}

static void idetape_init_pc(idetape_pc_t *pc)
{
	memset(pc->c, 0, 12);
	pc->retries = 0;
	pc->flags = 0;
	pc->request_transfer = 0;
	pc->buffer = pc->pc_buffer;
	pc->buffer_size = IDETAPE_PC_BUFFER_SIZE;
	pc->bh = NULL;
	pc->b_data = NULL;
}

/*
 * called on each failed packet command retry to analyze the request sense. We
 * currently do not utilize this information.
 */
static void idetape_analyze_error(ide_drive_t *drive, u8 *sense)
{
	idetape_tape_t *tape = drive->driver_data;
	idetape_pc_t *pc = tape->failed_pc;

	tape->sense_key = sense[2] & 0xF;
	tape->asc       = sense[12];
	tape->ascq      = sense[13];

	debug_log(DBG_ERR, "pc = %x, sense key = %x, asc = %x, ascq = %x\n",
		 pc->c[0], tape->sense_key, tape->asc, tape->ascq);

	/* Correct pc->actually_transferred by asking the tape.	 */
	if (test_bit(PC_DMA_ERROR, &pc->flags)) {
		pc->actually_transferred = pc->request_transfer -
			tape->blk_size *
			be32_to_cpu(get_unaligned((u32 *)&sense[3]));
		idetape_update_buffers(pc);
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
			set_bit(PC_ABORT, &pc->flags);
		}
	}
	if (pc->c[0] == READ_6 && (sense[2] & 0x80)) {
		pc->error = IDETAPE_ERROR_FILEMARK;
		set_bit(PC_ABORT, &pc->flags);
	}
	if (pc->c[0] == WRITE_6) {
		if ((sense[2] & 0x40) || (tape->sense_key == 0xd
		     && tape->asc == 0x0 && tape->ascq == 0x2)) {
			pc->error = IDETAPE_ERROR_EOD;
			set_bit(PC_ABORT, &pc->flags);
		}
	}
	if (pc->c[0] == READ_6 || pc->c[0] == WRITE_6) {
		if (tape->sense_key == 8) {
			pc->error = IDETAPE_ERROR_EOD;
			set_bit(PC_ABORT, &pc->flags);
		}
		if (!test_bit(PC_ABORT, &pc->flags) &&
		    pc->actually_transferred)
			pc->retries = IDETAPE_MAX_PC_RETRIES + 1;
	}
}

static void idetape_activate_next_stage(ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;
	idetape_stage_t *stage = tape->next_stage;
	struct request *rq = &stage->rq;

	debug_log(DBG_PROCS, "Enter %s\n", __func__);

	if (stage == NULL) {
		printk(KERN_ERR "ide-tape: bug: Trying to activate a non"
				" existing stage\n");
		return;
	}

	rq->rq_disk = tape->disk;
	rq->buffer = NULL;
	rq->special = (void *)stage->bh;
	tape->active_data_rq = rq;
	tape->active_stage = stage;
	tape->next_stage = stage->next;
}

/* Free a stage along with its related buffers completely. */
static void __idetape_kfree_stage(idetape_stage_t *stage)
{
	struct idetape_bh *prev_bh, *bh = stage->bh;
	int size;

	while (bh != NULL) {
		if (bh->b_data != NULL) {
			size = (int) bh->b_size;
			while (size > 0) {
				free_page((unsigned long) bh->b_data);
				size -= PAGE_SIZE;
				bh->b_data += PAGE_SIZE;
			}
		}
		prev_bh = bh;
		bh = bh->b_reqnext;
		kfree(prev_bh);
	}
	kfree(stage);
}

static void idetape_kfree_stage(idetape_tape_t *tape, idetape_stage_t *stage)
{
	__idetape_kfree_stage(stage);
}

/*
 * Remove tape->first_stage from the pipeline. The caller should avoid race
 * conditions.
 */
static void idetape_remove_stage_head(ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;
	idetape_stage_t *stage;

	debug_log(DBG_PROCS, "Enter %s\n", __func__);

	if (tape->first_stage == NULL) {
		printk(KERN_ERR "ide-tape: bug: tape->first_stage is NULL\n");
		return;
	}
	if (tape->active_stage == tape->first_stage) {
		printk(KERN_ERR "ide-tape: bug: Trying to free our active "
				"pipeline stage\n");
		return;
	}
	stage = tape->first_stage;
	tape->first_stage = stage->next;
	idetape_kfree_stage(tape, stage);
	tape->nr_stages--;
	if (tape->first_stage == NULL) {
		tape->last_stage = NULL;
		if (tape->next_stage != NULL)
			printk(KERN_ERR "ide-tape: bug: tape->next_stage !="
					" NULL\n");
		if (tape->nr_stages)
			printk(KERN_ERR "ide-tape: bug: nr_stages should be 0 "
					"now\n");
	}
}

/*
 * This will free all the pipeline stages starting from new_last_stage->next
 * to the end of the list, and point tape->last_stage to new_last_stage.
 */
static void idetape_abort_pipeline(ide_drive_t *drive,
				   idetape_stage_t *new_last_stage)
{
	idetape_tape_t *tape = drive->driver_data;
	idetape_stage_t *stage = new_last_stage->next;
	idetape_stage_t *nstage;

	debug_log(DBG_PROCS, "%s: Enter %s\n", tape->name, __func__);

	while (stage) {
		nstage = stage->next;
		idetape_kfree_stage(tape, stage);
		--tape->nr_stages;
		--tape->nr_pending_stages;
		stage = nstage;
	}
	if (new_last_stage)
		new_last_stage->next = NULL;
	tape->last_stage = new_last_stage;
	tape->next_stage = NULL;
}

/*
 * Finish servicing a request and insert a pending pipeline request into the
 * main device queue.
 */
static int idetape_end_request(ide_drive_t *drive, int uptodate, int nr_sects)
{
	struct request *rq = HWGROUP(drive)->rq;
	idetape_tape_t *tape = drive->driver_data;
	unsigned long flags;
	int error;
	int remove_stage = 0;
	idetape_stage_t *active_stage;

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

	/* The request was a pipelined data transfer request */
	if (tape->active_data_rq == rq) {
		active_stage = tape->active_stage;
		tape->active_stage = NULL;
		tape->active_data_rq = NULL;
		tape->nr_pending_stages--;
		if (rq->cmd[0] & REQ_IDETAPE_WRITE) {
			remove_stage = 1;
			if (error) {
				set_bit(IDETAPE_PIPELINE_ERROR, &tape->flags);
				if (error == IDETAPE_ERROR_EOD)
					idetape_abort_pipeline(drive,
								active_stage);
			}
		} else if (rq->cmd[0] & REQ_IDETAPE_READ) {
			if (error == IDETAPE_ERROR_EOD) {
				set_bit(IDETAPE_PIPELINE_ERROR, &tape->flags);
				idetape_abort_pipeline(drive, active_stage);
			}
		}
		if (tape->next_stage != NULL) {
			idetape_activate_next_stage(drive);

			/* Insert the next request into the request queue. */
			(void)ide_do_drive_cmd(drive, tape->active_data_rq,
						ide_end);
		} else if (!error) {
			/*
			 * This is a part of the feedback loop which tries to
			 * find the optimum number of stages. We are starting
			 * from a minimum maximum number of stages, and if we
			 * sense that the pipeline is empty, we try to increase
			 * it, until we reach the user compile time memory
			 * limit.
			 */
			int i = (tape->max_pipeline - tape->min_pipeline) / 10;

			tape->max_stages += max(i, 1);
			tape->max_stages = max(tape->max_stages,
						tape->min_pipeline);
			tape->max_stages = min(tape->max_stages,
						tape->max_pipeline);
		}
	}
	ide_end_drive_cmd(drive, 0, 0);

	if (remove_stage)
		idetape_remove_stage_head(drive);
	if (tape->active_data_rq == NULL)
		clear_bit(IDETAPE_PIPELINE_ACTIVE, &tape->flags);
	spin_unlock_irqrestore(&tape->lock, flags);
	return 0;
}

static ide_startstop_t idetape_request_sense_callback(ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;

	debug_log(DBG_PROCS, "Enter %s\n", __func__);

	if (!tape->pc->error) {
		idetape_analyze_error(drive, tape->pc->buffer);
		idetape_end_request(drive, 1, 0);
	} else {
		printk(KERN_ERR "ide-tape: Error in REQUEST SENSE itself - "
				"Aborting request!\n");
		idetape_end_request(drive, 0, 0);
	}
	return ide_stopped;
}

static void idetape_create_request_sense_cmd(idetape_pc_t *pc)
{
	idetape_init_pc(pc);
	pc->c[0] = REQUEST_SENSE;
	pc->c[4] = 20;
	pc->request_transfer = 20;
	pc->callback = &idetape_request_sense_callback;
}

static void idetape_init_rq(struct request *rq, u8 cmd)
{
	memset(rq, 0, sizeof(*rq));
	rq->cmd_type = REQ_TYPE_SPECIAL;
	rq->cmd[0] = cmd;
}

/*
 * Generate a new packet command request in front of the request queue, before
 * the current request, so that it will be processed immediately, on the next
 * pass through the driver. The function below is called from the request
 * handling part of the driver (the "bottom" part). Safe storage for the request
 * should be allocated with ide_tape_next_{pc,rq}_storage() prior to that.
 *
 * Memory for those requests is pre-allocated at initialization time, and is
 * limited to IDETAPE_PC_STACK requests. We assume that we have enough space for
 * the maximum possible number of inter-dependent packet commands.
 *
 * The higher level of the driver - The ioctl handler and the character device
 * handling functions should queue request to the lower level part and wait for
 * their completion using idetape_queue_pc_tail or idetape_queue_rw_tail.
 */
static void idetape_queue_pc_head(ide_drive_t *drive, idetape_pc_t *pc,
				  struct request *rq)
{
	struct ide_tape_obj *tape = drive->driver_data;

	idetape_init_rq(rq, REQ_IDETAPE_PC1);
	rq->buffer = (char *) pc;
	rq->rq_disk = tape->disk;
	(void) ide_do_drive_cmd(drive, rq, ide_preempt);
}

/*
 *	idetape_retry_pc is called when an error was detected during the
 *	last packet command. We queue a request sense packet command in
 *	the head of the request list.
 */
static ide_startstop_t idetape_retry_pc (ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;
	idetape_pc_t *pc;
	struct request *rq;

	(void)ide_read_error(drive);
	pc = idetape_next_pc_storage(drive);
	rq = idetape_next_rq_storage(drive);
	idetape_create_request_sense_cmd(pc);
	set_bit(IDETAPE_IGNORE_DSC, &tape->flags);
	idetape_queue_pc_head(drive, pc, rq);
	return ide_stopped;
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

typedef void idetape_io_buf(ide_drive_t *, idetape_pc_t *, unsigned int);

/*
 * This is the usual interrupt handler which will be called during a packet
 * command. We will transfer some of the data (as requested by the drive) and
 * will re-point interrupt handler to us. When data transfer is finished, we
 * will act according to the algorithm described before
 * idetape_issue_pc.
 */
static ide_startstop_t idetape_pc_intr(ide_drive_t *drive)
{
	ide_hwif_t *hwif = drive->hwif;
	idetape_tape_t *tape = drive->driver_data;
	idetape_pc_t *pc = tape->pc;
	xfer_func_t *xferfunc;
	idetape_io_buf *iobuf;
	unsigned int temp;
#if SIMULATE_ERRORS
	static int error_sim_count;
#endif
	u16 bcount;
	u8 stat, ireason;

	debug_log(DBG_PROCS, "Enter %s - interrupt handler\n", __func__);

	/* Clear the interrupt */
	stat = ide_read_status(drive);

	if (test_bit(PC_DMA_IN_PROGRESS, &pc->flags)) {
		if (hwif->ide_dma_end(drive) || (stat & ERR_STAT)) {
			/*
			 * A DMA error is sometimes expected. For example,
			 * if the tape is crossing a filemark during a
			 * READ command, it will issue an irq and position
			 * itself before the filemark, so that only a partial
			 * data transfer will occur (which causes the DMA
			 * error). In that case, we will later ask the tape
			 * how much bytes of the original request were
			 * actually transferred (we can't receive that
			 * information from the DMA engine on most chipsets).
			 */

			/*
			 * On the contrary, a DMA error is never expected;
			 * it usually indicates a hardware error or abort.
			 * If the tape crosses a filemark during a READ
			 * command, it will issue an irq and position itself
			 * after the filemark (not before). Only a partial
			 * data transfer will occur, but no DMA error.
			 * (AS, 19 Apr 2001)
			 */
			set_bit(PC_DMA_ERROR, &pc->flags);
		} else {
			pc->actually_transferred = pc->request_transfer;
			idetape_update_buffers(pc);
		}
		debug_log(DBG_PROCS, "DMA finished\n");

	}

	/* No more interrupts */
	if ((stat & DRQ_STAT) == 0) {
		debug_log(DBG_SENSE, "Packet command completed, %d bytes"
				" transferred\n", pc->actually_transferred);

		clear_bit(PC_DMA_IN_PROGRESS, &pc->flags);
		local_irq_enable();

#if SIMULATE_ERRORS
		if ((pc->c[0] == WRITE_6 || pc->c[0] == READ_6) &&
		    (++error_sim_count % 100) == 0) {
			printk(KERN_INFO "ide-tape: %s: simulating error\n",
				tape->name);
			stat |= ERR_STAT;
		}
#endif
		if ((stat & ERR_STAT) && pc->c[0] == REQUEST_SENSE)
			stat &= ~ERR_STAT;
		if ((stat & ERR_STAT) || test_bit(PC_DMA_ERROR, &pc->flags)) {
			/* Error detected */
			debug_log(DBG_ERR, "%s: I/O error\n", tape->name);

			if (pc->c[0] == REQUEST_SENSE) {
				printk(KERN_ERR "ide-tape: I/O error in request"
						" sense command\n");
				return ide_do_reset(drive);
			}
			debug_log(DBG_ERR, "[cmd %x]: check condition\n",
					pc->c[0]);

			/* Retry operation */
			return idetape_retry_pc(drive);
		}
		pc->error = 0;
		if (test_bit(PC_WAIT_FOR_DSC, &pc->flags) &&
		    (stat & SEEK_STAT) == 0) {
			/* Media access command */
			tape->dsc_polling_start = jiffies;
			tape->dsc_poll_freq = IDETAPE_DSC_MA_FAST;
			tape->dsc_timeout = jiffies + IDETAPE_DSC_MA_TIMEOUT;
			/* Allow ide.c to handle other requests */
			idetape_postpone_request(drive);
			return ide_stopped;
		}
		if (tape->failed_pc == pc)
			tape->failed_pc = NULL;
		/* Command finished - Call the callback function */
		return pc->callback(drive);
	}
	if (test_and_clear_bit(PC_DMA_IN_PROGRESS, &pc->flags)) {
		printk(KERN_ERR "ide-tape: The tape wants to issue more "
				"interrupts in DMA mode\n");
		printk(KERN_ERR "ide-tape: DMA disabled, reverting to PIO\n");
		ide_dma_off(drive);
		return ide_do_reset(drive);
	}
	/* Get the number of bytes to transfer on this interrupt. */
	bcount = (hwif->INB(IDE_BCOUNTH_REG) << 8) |
		  hwif->INB(IDE_BCOUNTL_REG);

	ireason = hwif->INB(IDE_IREASON_REG);

	if (ireason & CD) {
		printk(KERN_ERR "ide-tape: CoD != 0 in %s\n", __func__);
		return ide_do_reset(drive);
	}
	if (((ireason & IO) == IO) == test_bit(PC_WRITING, &pc->flags)) {
		/* Hopefully, we will never get here */
		printk(KERN_ERR "ide-tape: We wanted to %s, ",
				(ireason & IO) ? "Write" : "Read");
		printk(KERN_ERR "ide-tape: but the tape wants us to %s !\n",
				(ireason & IO) ? "Read" : "Write");
		return ide_do_reset(drive);
	}
	if (!test_bit(PC_WRITING, &pc->flags)) {
		/* Reading - Check that we have enough space */
		temp = pc->actually_transferred + bcount;
		if (temp > pc->request_transfer) {
			if (temp > pc->buffer_size) {
				printk(KERN_ERR "ide-tape: The tape wants to "
					"send us more data than expected "
					"- discarding data\n");
				idetape_discard_data(drive, bcount);
				ide_set_handler(drive, &idetape_pc_intr,
						IDETAPE_WAIT_CMD, NULL);
				return ide_started;
			}
			debug_log(DBG_SENSE, "The tape wants to send us more "
				"data than expected - allowing transfer\n");
		}
		iobuf = &idetape_input_buffers;
		xferfunc = hwif->atapi_input_bytes;
	} else {
		iobuf = &idetape_output_buffers;
		xferfunc = hwif->atapi_output_bytes;
	}

	if (pc->bh)
		iobuf(drive, pc, bcount);
	else
		xferfunc(drive, pc->current_position, bcount);

	/* Update the current position */
	pc->actually_transferred += bcount;
	pc->current_position += bcount;

	debug_log(DBG_SENSE, "[cmd %x] transferred %d bytes on that intr.\n",
			pc->c[0], bcount);

	/* And set the interrupt handler again */
	ide_set_handler(drive, &idetape_pc_intr, IDETAPE_WAIT_CMD, NULL);
	return ide_started;
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
	ide_hwif_t *hwif = drive->hwif;
	idetape_tape_t *tape = drive->driver_data;
	idetape_pc_t *pc = tape->pc;
	int retries = 100;
	ide_startstop_t startstop;
	u8 ireason;

	if (ide_wait_stat(&startstop, drive, DRQ_STAT, BUSY_STAT, WAIT_READY)) {
		printk(KERN_ERR "ide-tape: Strange, packet command initiated "
				"yet DRQ isn't asserted\n");
		return startstop;
	}
	ireason = hwif->INB(IDE_IREASON_REG);
	while (retries-- && ((ireason & CD) == 0 || (ireason & IO))) {
		printk(KERN_ERR "ide-tape: (IO,CoD != (0,1) while issuing "
				"a packet command, retrying\n");
		udelay(100);
		ireason = hwif->INB(IDE_IREASON_REG);
		if (retries == 0) {
			printk(KERN_ERR "ide-tape: (IO,CoD != (0,1) while "
					"issuing a packet command, ignoring\n");
			ireason |= CD;
			ireason &= ~IO;
		}
	}
	if ((ireason & CD) == 0 || (ireason & IO)) {
		printk(KERN_ERR "ide-tape: (IO,CoD) != (0,1) while issuing "
				"a packet command\n");
		return ide_do_reset(drive);
	}
	/* Set the interrupt routine */
	ide_set_handler(drive, &idetape_pc_intr, IDETAPE_WAIT_CMD, NULL);
#ifdef CONFIG_BLK_DEV_IDEDMA
	/* Begin DMA, if necessary */
	if (test_bit(PC_DMA_IN_PROGRESS, &pc->flags))
		hwif->dma_start(drive);
#endif
	/* Send the actual packet */
	HWIF(drive)->atapi_output_bytes(drive, pc->c, 12);
	return ide_started;
}

static ide_startstop_t idetape_issue_pc(ide_drive_t *drive, idetape_pc_t *pc)
{
	ide_hwif_t *hwif = drive->hwif;
	idetape_tape_t *tape = drive->driver_data;
	int dma_ok = 0;
	u16 bcount;

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
	    test_bit(PC_ABORT, &pc->flags)) {
		/*
		 * We will "abort" retrying a packet command in case legitimate
		 * error code was received (crossing a filemark, or end of the
		 * media, for example).
		 */
		if (!test_bit(PC_ABORT, &pc->flags)) {
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
		return pc->callback(drive);
	}
	debug_log(DBG_SENSE, "Retry #%d, cmd = %02X\n", pc->retries, pc->c[0]);

	pc->retries++;
	/* We haven't transferred any data yet */
	pc->actually_transferred = 0;
	pc->current_position = pc->buffer;
	/* Request to transfer the entire buffer at once */
	bcount = pc->request_transfer;

	if (test_and_clear_bit(PC_DMA_ERROR, &pc->flags)) {
		printk(KERN_WARNING "ide-tape: DMA disabled, "
				"reverting to PIO\n");
		ide_dma_off(drive);
	}
	if (test_bit(PC_DMA_RECOMMENDED, &pc->flags) && drive->using_dma)
		dma_ok = !hwif->dma_setup(drive);

	ide_pktcmd_tf_load(drive, IDE_TFLAG_NO_SELECT_MASK |
			   IDE_TFLAG_OUT_DEVICE, bcount, dma_ok);

	if (dma_ok)			/* Will begin DMA later */
		set_bit(PC_DMA_IN_PROGRESS, &pc->flags);
	if (test_bit(IDETAPE_DRQ_INTERRUPT, &tape->flags)) {
		ide_execute_command(drive, WIN_PACKETCMD, &idetape_transfer_pc,
				    IDETAPE_WAIT_CMD, NULL);
		return ide_started;
	} else {
		hwif->OUTB(WIN_PACKETCMD, IDE_COMMAND_REG);
		return idetape_transfer_pc(drive);
	}
}

static ide_startstop_t idetape_pc_callback(ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;

	debug_log(DBG_PROCS, "Enter %s\n", __func__);

	idetape_end_request(drive, tape->pc->error ? 0 : 1, 0);
	return ide_stopped;
}

/* A mode sense command is used to "sense" tape parameters. */
static void idetape_create_mode_sense_cmd(idetape_pc_t *pc, u8 page_code)
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
		pc->request_transfer = 12;
	else if (page_code == IDETAPE_CAPABILITIES_PAGE)
		pc->request_transfer = 24;
	else
		pc->request_transfer = 50;
	pc->callback = &idetape_pc_callback;
}

static void idetape_calculate_speeds(ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;

	if (time_after(jiffies,
			tape->controlled_pipeline_head_time + 120 * HZ)) {
		tape->controlled_previous_pipeline_head =
			tape->controlled_last_pipeline_head;
		tape->controlled_previous_head_time =
			tape->controlled_pipeline_head_time;
		tape->controlled_last_pipeline_head = tape->pipeline_head;
		tape->controlled_pipeline_head_time = jiffies;
	}
	if (time_after(jiffies, tape->controlled_pipeline_head_time + 60 * HZ))
		tape->controlled_pipeline_head_speed = (tape->pipeline_head -
				tape->controlled_last_pipeline_head) * 32 * HZ /
				(jiffies - tape->controlled_pipeline_head_time);
	else if (time_after(jiffies, tape->controlled_previous_head_time))
		tape->controlled_pipeline_head_speed = (tape->pipeline_head -
				tape->controlled_previous_pipeline_head) * 32 *
			HZ / (jiffies - tape->controlled_previous_head_time);

	if (tape->nr_pending_stages < tape->max_stages/*- 1 */) {
		/* -1 for read mode error recovery */
		if (time_after(jiffies, tape->uncontrolled_previous_head_time +
					10 * HZ)) {
			tape->uncontrolled_pipeline_head_time = jiffies;
			tape->uncontrolled_pipeline_head_speed =
				(tape->pipeline_head -
				 tape->uncontrolled_previous_pipeline_head) *
				32 * HZ / (jiffies -
					tape->uncontrolled_previous_head_time);
		}
	} else {
		tape->uncontrolled_previous_head_time = jiffies;
		tape->uncontrolled_previous_pipeline_head = tape->pipeline_head;
		if (time_after(jiffies, tape->uncontrolled_pipeline_head_time +
					30 * HZ))
			tape->uncontrolled_pipeline_head_time = jiffies;

	}
	tape->pipeline_head_speed = max(tape->uncontrolled_pipeline_head_speed,
					tape->controlled_pipeline_head_speed);

	if (tape->speed_control == 1) {
		if (tape->nr_pending_stages >= tape->max_stages / 2)
			tape->max_insert_speed = tape->pipeline_head_speed +
				(1100 - tape->pipeline_head_speed) * 2 *
				(tape->nr_pending_stages - tape->max_stages / 2)
				/ tape->max_stages;
		else
			tape->max_insert_speed = 500 +
				(tape->pipeline_head_speed - 500) * 2 *
				tape->nr_pending_stages / tape->max_stages;

		if (tape->nr_pending_stages >= tape->max_stages * 99 / 100)
			tape->max_insert_speed = 5000;
	} else
		tape->max_insert_speed = tape->speed_control;

	tape->max_insert_speed = max(tape->max_insert_speed, 500);
}

static ide_startstop_t idetape_media_access_finished(ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;
	idetape_pc_t *pc = tape->pc;
	u8 stat;

	stat = ide_read_status(drive);

	if (stat & SEEK_STAT) {
		if (stat & ERR_STAT) {
			/* Error detected */
			if (pc->c[0] != TEST_UNIT_READY)
				printk(KERN_ERR "ide-tape: %s: I/O error, ",
						tape->name);
			/* Retry operation */
			return idetape_retry_pc(drive);
		}
		pc->error = 0;
		if (tape->failed_pc == pc)
			tape->failed_pc = NULL;
	} else {
		pc->error = IDETAPE_ERROR_GENERAL;
		tape->failed_pc = NULL;
	}
	return pc->callback(drive);
}

static ide_startstop_t idetape_rw_callback(ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;
	struct request *rq = HWGROUP(drive)->rq;
	int blocks = tape->pc->actually_transferred / tape->blk_size;

	tape->avg_size += blocks * tape->blk_size;
	tape->insert_size += blocks * tape->blk_size;
	if (tape->insert_size > 1024 * 1024)
		tape->measure_insert_time = 1;
	if (tape->measure_insert_time) {
		tape->measure_insert_time = 0;
		tape->insert_time = jiffies;
		tape->insert_size = 0;
	}
	if (time_after(jiffies, tape->insert_time))
		tape->insert_speed = tape->insert_size / 1024 * HZ /
					(jiffies - tape->insert_time);
	if (time_after_eq(jiffies, tape->avg_time + HZ)) {
		tape->avg_speed = tape->avg_size * HZ /
				(jiffies - tape->avg_time) / 1024;
		tape->avg_size = 0;
		tape->avg_time = jiffies;
	}
	debug_log(DBG_PROCS, "Enter %s\n", __func__);

	tape->first_frame += blocks;
	rq->current_nr_sectors -= blocks;

	if (!tape->pc->error)
		idetape_end_request(drive, 1, 0);
	else
		idetape_end_request(drive, tape->pc->error, 0);
	return ide_stopped;
}

static void idetape_create_read_cmd(idetape_tape_t *tape, idetape_pc_t *pc,
		unsigned int length, struct idetape_bh *bh)
{
	idetape_init_pc(pc);
	pc->c[0] = READ_6;
	put_unaligned(cpu_to_be32(length), (unsigned int *) &pc->c[1]);
	pc->c[1] = 1;
	pc->callback = &idetape_rw_callback;
	pc->bh = bh;
	atomic_set(&bh->b_count, 0);
	pc->buffer = NULL;
	pc->buffer_size = length * tape->blk_size;
	pc->request_transfer = pc->buffer_size;
	if (pc->request_transfer == tape->stage_size)
		set_bit(PC_DMA_RECOMMENDED, &pc->flags);
}

static void idetape_create_write_cmd(idetape_tape_t *tape, idetape_pc_t *pc,
		unsigned int length, struct idetape_bh *bh)
{
	idetape_init_pc(pc);
	pc->c[0] = WRITE_6;
	put_unaligned(cpu_to_be32(length), (unsigned int *) &pc->c[1]);
	pc->c[1] = 1;
	pc->callback = &idetape_rw_callback;
	set_bit(PC_WRITING, &pc->flags);
	pc->bh = bh;
	pc->b_data = bh->b_data;
	pc->b_count = atomic_read(&bh->b_count);
	pc->buffer = NULL;
	pc->buffer_size = length * tape->blk_size;
	pc->request_transfer = pc->buffer_size;
	if (pc->request_transfer == tape->stage_size)
		set_bit(PC_DMA_RECOMMENDED, &pc->flags);
}

static ide_startstop_t idetape_do_request(ide_drive_t *drive,
					  struct request *rq, sector_t block)
{
	idetape_tape_t *tape = drive->driver_data;
	idetape_pc_t *pc = NULL;
	struct request *postponed_rq = tape->postponed_rq;
	u8 stat;

	debug_log(DBG_SENSE, "sector: %ld, nr_sectors: %ld,"
			" current_nr_sectors: %d\n",
			rq->sector, rq->nr_sectors, rq->current_nr_sectors);

	if (!blk_special_request(rq)) {
		/* We do not support buffer cache originated requests. */
		printk(KERN_NOTICE "ide-tape: %s: Unsupported request in "
			"request queue (%d)\n", drive->name, rq->cmd_type);
		ide_end_request(drive, 0, 0);
		return ide_stopped;
	}

	/* Retry a failed packet command */
	if (tape->failed_pc && tape->pc->c[0] == REQUEST_SENSE)
		return idetape_issue_pc(drive, tape->failed_pc);

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
	stat = ide_read_status(drive);

	if (!drive->dsc_overlap && !(rq->cmd[0] & REQ_IDETAPE_PC2))
		set_bit(IDETAPE_IGNORE_DSC, &tape->flags);

	if (drive->post_reset == 1) {
		set_bit(IDETAPE_IGNORE_DSC, &tape->flags);
		drive->post_reset = 0;
	}

	if (time_after(jiffies, tape->insert_time))
		tape->insert_speed = tape->insert_size / 1024 * HZ /
					(jiffies - tape->insert_time);
	idetape_calculate_speeds(drive);
	if (!test_and_clear_bit(IDETAPE_IGNORE_DSC, &tape->flags) &&
	    (stat & SEEK_STAT) == 0) {
		if (postponed_rq == NULL) {
			tape->dsc_polling_start = jiffies;
			tape->dsc_poll_freq = tape->best_dsc_rw_freq;
			tape->dsc_timeout = jiffies + IDETAPE_DSC_RW_TIMEOUT;
		} else if (time_after(jiffies, tape->dsc_timeout)) {
			printk(KERN_ERR "ide-tape: %s: DSC timeout\n",
				tape->name);
			if (rq->cmd[0] & REQ_IDETAPE_PC2) {
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
	if (rq->cmd[0] & REQ_IDETAPE_READ) {
		tape->buffer_head++;
		tape->postpone_cnt = 0;
		pc = idetape_next_pc_storage(drive);
		idetape_create_read_cmd(tape, pc, rq->current_nr_sectors,
					(struct idetape_bh *)rq->special);
		goto out;
	}
	if (rq->cmd[0] & REQ_IDETAPE_WRITE) {
		tape->buffer_head++;
		tape->postpone_cnt = 0;
		pc = idetape_next_pc_storage(drive);
		idetape_create_write_cmd(tape, pc, rq->current_nr_sectors,
					 (struct idetape_bh *)rq->special);
		goto out;
	}
	if (rq->cmd[0] & REQ_IDETAPE_PC1) {
		pc = (idetape_pc_t *) rq->buffer;
		rq->cmd[0] &= ~(REQ_IDETAPE_PC1);
		rq->cmd[0] |= REQ_IDETAPE_PC2;
		goto out;
	}
	if (rq->cmd[0] & REQ_IDETAPE_PC2) {
		idetape_media_access_finished(drive);
		return ide_stopped;
	}
	BUG();
out:
	return idetape_issue_pc(drive, pc);
}

/* Pipeline related functions */
static inline int idetape_pipeline_active(idetape_tape_t *tape)
{
	int rc1, rc2;

	rc1 = test_bit(IDETAPE_PIPELINE_ACTIVE, &tape->flags);
	rc2 = (tape->active_data_rq != NULL);
	return rc1;
}

/*
 * The function below uses __get_free_page to allocate a pipeline stage, along
 * with all the necessary small buffers which together make a buffer of size
 * tape->stage_size (or a bit more). We attempt to combine sequential pages as
 * much as possible.
 *
 * It returns a pointer to the new allocated stage, or NULL if we can't (or
 * don't want to) allocate a stage.
 *
 * Pipeline stages are optional and are used to increase performance. If we
 * can't allocate them, we'll manage without them.
 */
static idetape_stage_t *__idetape_kmalloc_stage(idetape_tape_t *tape, int full,
						int clear)
{
	idetape_stage_t *stage;
	struct idetape_bh *prev_bh, *bh;
	int pages = tape->pages_per_stage;
	char *b_data = NULL;

	stage = kmalloc(sizeof(idetape_stage_t), GFP_KERNEL);
	if (!stage)
		return NULL;
	stage->next = NULL;

	stage->bh = kmalloc(sizeof(struct idetape_bh), GFP_KERNEL);
	bh = stage->bh;
	if (bh == NULL)
		goto abort;
	bh->b_reqnext = NULL;
	bh->b_data = (char *) __get_free_page(GFP_KERNEL);
	if (!bh->b_data)
		goto abort;
	if (clear)
		memset(bh->b_data, 0, PAGE_SIZE);
	bh->b_size = PAGE_SIZE;
	atomic_set(&bh->b_count, full ? bh->b_size : 0);

	while (--pages) {
		b_data = (char *) __get_free_page(GFP_KERNEL);
		if (!b_data)
			goto abort;
		if (clear)
			memset(b_data, 0, PAGE_SIZE);
		if (bh->b_data == b_data + PAGE_SIZE) {
			bh->b_size += PAGE_SIZE;
			bh->b_data -= PAGE_SIZE;
			if (full)
				atomic_add(PAGE_SIZE, &bh->b_count);
			continue;
		}
		if (b_data == bh->b_data + bh->b_size) {
			bh->b_size += PAGE_SIZE;
			if (full)
				atomic_add(PAGE_SIZE, &bh->b_count);
			continue;
		}
		prev_bh = bh;
		bh = kmalloc(sizeof(struct idetape_bh), GFP_KERNEL);
		if (!bh) {
			free_page((unsigned long) b_data);
			goto abort;
		}
		bh->b_reqnext = NULL;
		bh->b_data = b_data;
		bh->b_size = PAGE_SIZE;
		atomic_set(&bh->b_count, full ? bh->b_size : 0);
		prev_bh->b_reqnext = bh;
	}
	bh->b_size -= tape->excess_bh_size;
	if (full)
		atomic_sub(tape->excess_bh_size, &bh->b_count);
	return stage;
abort:
	__idetape_kfree_stage(stage);
	return NULL;
}

static idetape_stage_t *idetape_kmalloc_stage(idetape_tape_t *tape)
{
	idetape_stage_t *cache_stage = tape->cache_stage;

	debug_log(DBG_PROCS, "Enter %s\n", __func__);

	if (tape->nr_stages >= tape->max_stages)
		return NULL;
	if (cache_stage != NULL) {
		tape->cache_stage = NULL;
		return cache_stage;
	}
	return __idetape_kmalloc_stage(tape, 0, 0);
}

static int idetape_copy_stage_from_user(idetape_tape_t *tape,
		idetape_stage_t *stage, const char __user *buf, int n)
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
		idetape_stage_t *stage, int n)
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

static void idetape_init_merge_stage(idetape_tape_t *tape)
{
	struct idetape_bh *bh = tape->merge_stage->bh;

	tape->bh = bh;
	if (tape->chrdev_dir == IDETAPE_DIR_WRITE)
		atomic_set(&bh->b_count, 0);
	else {
		tape->b_data = bh->b_data;
		tape->b_count = atomic_read(&bh->b_count);
	}
}

static void idetape_switch_buffers(idetape_tape_t *tape, idetape_stage_t *stage)
{
	struct idetape_bh *tmp;

	tmp = stage->bh;
	stage->bh = tape->merge_stage->bh;
	tape->merge_stage->bh = tmp;
	idetape_init_merge_stage(tape);
}

/* Add a new stage at the end of the pipeline. */
static void idetape_add_stage_tail(ide_drive_t *drive, idetape_stage_t *stage)
{
	idetape_tape_t *tape = drive->driver_data;
	unsigned long flags;

	debug_log(DBG_PROCS, "Enter %s\n", __func__);

	spin_lock_irqsave(&tape->lock, flags);
	stage->next = NULL;
	if (tape->last_stage != NULL)
		tape->last_stage->next = stage;
	else
		tape->first_stage = stage;
		tape->next_stage  = stage;
	tape->last_stage = stage;
	if (tape->next_stage == NULL)
		tape->next_stage = tape->last_stage;
	tape->nr_stages++;
	tape->nr_pending_stages++;
	spin_unlock_irqrestore(&tape->lock, flags);
}

/* Install a completion in a pending request and sleep until it is serviced. The
 * caller should ensure that the request will not be serviced before we install
 * the completion (usually by disabling interrupts).
 */
static void idetape_wait_for_request(ide_drive_t *drive, struct request *rq)
{
	DECLARE_COMPLETION_ONSTACK(wait);
	idetape_tape_t *tape = drive->driver_data;

	if (rq == NULL || !blk_special_request(rq)) {
		printk(KERN_ERR "ide-tape: bug: Trying to sleep on non-valid"
				 " request\n");
		return;
	}
	rq->end_io_data = &wait;
	rq->end_io = blk_end_sync_rq;
	spin_unlock_irq(&tape->lock);
	wait_for_completion(&wait);
	/* The stage and its struct request have been deallocated */
	spin_lock_irq(&tape->lock);
}

static ide_startstop_t idetape_read_position_callback(ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;
	u8 *readpos = tape->pc->buffer;

	debug_log(DBG_PROCS, "Enter %s\n", __func__);

	if (!tape->pc->error) {
		debug_log(DBG_SENSE, "BOP - %s\n",
				(readpos[0] & 0x80) ? "Yes" : "No");
		debug_log(DBG_SENSE, "EOP - %s\n",
				(readpos[0] & 0x40) ? "Yes" : "No");

		if (readpos[0] & 0x4) {
			printk(KERN_INFO "ide-tape: Block location is unknown"
					 "to the tape\n");
			clear_bit(IDETAPE_ADDRESS_VALID, &tape->flags);
			idetape_end_request(drive, 0, 0);
		} else {
			debug_log(DBG_SENSE, "Block Location - %u\n",
					be32_to_cpu(*(u32 *)&readpos[4]));

			tape->partition = readpos[1];
			tape->first_frame =
				be32_to_cpu(*(u32 *)&readpos[4]);
			set_bit(IDETAPE_ADDRESS_VALID, &tape->flags);
			idetape_end_request(drive, 1, 0);
		}
	} else {
		idetape_end_request(drive, 0, 0);
	}
	return ide_stopped;
}

/*
 * Write a filemark if write_filemark=1. Flush the device buffers without
 * writing a filemark otherwise.
 */
static void idetape_create_write_filemark_cmd(ide_drive_t *drive,
		idetape_pc_t *pc, int write_filemark)
{
	idetape_init_pc(pc);
	pc->c[0] = WRITE_FILEMARKS;
	pc->c[4] = write_filemark;
	set_bit(PC_WAIT_FOR_DSC, &pc->flags);
	pc->callback = &idetape_pc_callback;
}

static void idetape_create_test_unit_ready_cmd(idetape_pc_t *pc)
{
	idetape_init_pc(pc);
	pc->c[0] = TEST_UNIT_READY;
	pc->callback = &idetape_pc_callback;
}

/*
 * We add a special packet command request to the tail of the request queue, and
 * wait for it to be serviced. This is not to be called from within the request
 * handling part of the driver! We allocate here data on the stack and it is
 * valid until the request is finished. This is not the case for the bottom part
 * of the driver, where we are always leaving the functions to wait for an
 * interrupt or a timer event.
 *
 * From the bottom part of the driver, we should allocate safe memory using
 * idetape_next_pc_storage() and ide_tape_next_rq_storage(), and add the request
 * to the request list without waiting for it to be serviced! In that case, we
 * usually use idetape_queue_pc_head().
 */
static int __idetape_queue_pc_tail(ide_drive_t *drive, idetape_pc_t *pc)
{
	struct ide_tape_obj *tape = drive->driver_data;
	struct request rq;

	idetape_init_rq(&rq, REQ_IDETAPE_PC1);
	rq.buffer = (char *) pc;
	rq.rq_disk = tape->disk;
	return ide_do_drive_cmd(drive, &rq, ide_wait);
}

static void idetape_create_load_unload_cmd(ide_drive_t *drive, idetape_pc_t *pc,
		int cmd)
{
	idetape_init_pc(pc);
	pc->c[0] = START_STOP;
	pc->c[4] = cmd;
	set_bit(PC_WAIT_FOR_DSC, &pc->flags);
	pc->callback = &idetape_pc_callback;
}

static int idetape_wait_ready(ide_drive_t *drive, unsigned long timeout)
{
	idetape_tape_t *tape = drive->driver_data;
	idetape_pc_t pc;
	int load_attempted = 0;

	/* Wait for the tape to become ready */
	set_bit(IDETAPE_MEDIUM_PRESENT, &tape->flags);
	timeout += jiffies;
	while (time_before(jiffies, timeout)) {
		idetape_create_test_unit_ready_cmd(&pc);
		if (!__idetape_queue_pc_tail(drive, &pc))
			return 0;
		if ((tape->sense_key == 2 && tape->asc == 4 && tape->ascq == 2)
		    || (tape->asc == 0x3A)) {
			/* no media */
			if (load_attempted)
				return -ENOMEDIUM;
			idetape_create_load_unload_cmd(drive, &pc,
							IDETAPE_LU_LOAD_MASK);
			__idetape_queue_pc_tail(drive, &pc);
			load_attempted = 1;
		/* not about to be ready */
		} else if (!(tape->sense_key == 2 && tape->asc == 4 &&
			     (tape->ascq == 1 || tape->ascq == 8)))
			return -EIO;
		msleep(100);
	}
	return -EIO;
}

static int idetape_queue_pc_tail(ide_drive_t *drive, idetape_pc_t *pc)
{
	return __idetape_queue_pc_tail(drive, pc);
}

static int idetape_flush_tape_buffers(ide_drive_t *drive)
{
	idetape_pc_t pc;
	int rc;

	idetape_create_write_filemark_cmd(drive, &pc, 0);
	rc = idetape_queue_pc_tail(drive, &pc);
	if (rc)
		return rc;
	idetape_wait_ready(drive, 60 * 5 * HZ);
	return 0;
}

static void idetape_create_read_position_cmd(idetape_pc_t *pc)
{
	idetape_init_pc(pc);
	pc->c[0] = READ_POSITION;
	pc->request_transfer = 20;
	pc->callback = &idetape_read_position_callback;
}

static int idetape_read_position(ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;
	idetape_pc_t pc;
	int position;

	debug_log(DBG_PROCS, "Enter %s\n", __func__);

	idetape_create_read_position_cmd(&pc);
	if (idetape_queue_pc_tail(drive, &pc))
		return -1;
	position = tape->first_frame;
	return position;
}

static void idetape_create_locate_cmd(ide_drive_t *drive, idetape_pc_t *pc,
		unsigned int block, u8 partition, int skip)
{
	idetape_init_pc(pc);
	pc->c[0] = POSITION_TO_ELEMENT;
	pc->c[1] = 2;
	put_unaligned(cpu_to_be32(block), (unsigned int *) &pc->c[3]);
	pc->c[8] = partition;
	set_bit(PC_WAIT_FOR_DSC, &pc->flags);
	pc->callback = &idetape_pc_callback;
}

static int idetape_create_prevent_cmd(ide_drive_t *drive, idetape_pc_t *pc,
				      int prevent)
{
	idetape_tape_t *tape = drive->driver_data;

	/* device supports locking according to capabilities page */
	if (!(tape->caps[6] & 0x01))
		return 0;

	idetape_init_pc(pc);
	pc->c[0] = ALLOW_MEDIUM_REMOVAL;
	pc->c[4] = prevent;
	pc->callback = &idetape_pc_callback;
	return 1;
}

static int __idetape_discard_read_pipeline(ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;
	unsigned long flags;
	int cnt;

	if (tape->chrdev_dir != IDETAPE_DIR_READ)
		return 0;

	/* Remove merge stage. */
	cnt = tape->merge_stage_size / tape->blk_size;
	if (test_and_clear_bit(IDETAPE_FILEMARK, &tape->flags))
		++cnt;		/* Filemarks count as 1 sector */
	tape->merge_stage_size = 0;
	if (tape->merge_stage != NULL) {
		__idetape_kfree_stage(tape->merge_stage);
		tape->merge_stage = NULL;
	}

	/* Clear pipeline flags. */
	clear_bit(IDETAPE_PIPELINE_ERROR, &tape->flags);
	tape->chrdev_dir = IDETAPE_DIR_NONE;

	/* Remove pipeline stages. */
	if (tape->first_stage == NULL)
		return 0;

	spin_lock_irqsave(&tape->lock, flags);
	tape->next_stage = NULL;
	if (idetape_pipeline_active(tape))
		idetape_wait_for_request(drive, tape->active_data_rq);
	spin_unlock_irqrestore(&tape->lock, flags);

	while (tape->first_stage != NULL) {
		struct request *rq_ptr = &tape->first_stage->rq;

		cnt += rq_ptr->nr_sectors - rq_ptr->current_nr_sectors;
		if (rq_ptr->errors == IDETAPE_ERROR_FILEMARK)
			++cnt;
		idetape_remove_stage_head(drive);
	}
	tape->nr_pending_stages = 0;
	tape->max_stages = tape->min_pipeline;
	return cnt;
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
	idetape_pc_t pc;

	if (tape->chrdev_dir == IDETAPE_DIR_READ)
		__idetape_discard_read_pipeline(drive);
	idetape_wait_ready(drive, 60 * 5 * HZ);
	idetape_create_locate_cmd(drive, &pc, block, partition, skip);
	retval = idetape_queue_pc_tail(drive, &pc);
	if (retval)
		return (retval);

	idetape_create_read_position_cmd(&pc);
	return (idetape_queue_pc_tail(drive, &pc));
}

static void idetape_discard_read_pipeline(ide_drive_t *drive,
					  int restore_position)
{
	idetape_tape_t *tape = drive->driver_data;
	int cnt;
	int seek, position;

	cnt = __idetape_discard_read_pipeline(drive);
	if (restore_position) {
		position = idetape_read_position(drive);
		seek = position > cnt ? position - cnt : 0;
		if (idetape_position_tape(drive, seek, 0, 0)) {
			printk(KERN_INFO "ide-tape: %s: position_tape failed in"
					 " discard_pipeline()\n", tape->name);
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
	struct request rq;

	debug_log(DBG_SENSE, "%s: cmd=%d\n", __func__, cmd);

	if (idetape_pipeline_active(tape)) {
		printk(KERN_ERR "ide-tape: bug: the pipeline is active in %s\n",
				__func__);
		return (0);
	}

	idetape_init_rq(&rq, cmd);
	rq.rq_disk = tape->disk;
	rq.special = (void *)bh;
	rq.sector = tape->first_frame;
	rq.nr_sectors		= blocks;
	rq.current_nr_sectors	= blocks;
	(void) ide_do_drive_cmd(drive, &rq, ide_wait);

	if ((cmd & (REQ_IDETAPE_READ | REQ_IDETAPE_WRITE)) == 0)
		return 0;

	if (tape->merge_stage)
		idetape_init_merge_stage(tape);
	if (rq.errors == IDETAPE_ERROR_GENERAL)
		return -EIO;
	return (tape->blk_size * (blocks-rq.current_nr_sectors));
}

/* start servicing the pipeline stages, starting from tape->next_stage. */
static void idetape_plug_pipeline(ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;

	if (tape->next_stage == NULL)
		return;
	if (!idetape_pipeline_active(tape)) {
		set_bit(IDETAPE_PIPELINE_ACTIVE, &tape->flags);
		idetape_activate_next_stage(drive);
		(void) ide_do_drive_cmd(drive, tape->active_data_rq, ide_end);
	}
}

static void idetape_create_inquiry_cmd(idetape_pc_t *pc)
{
	idetape_init_pc(pc);
	pc->c[0] = INQUIRY;
	pc->c[4] = 254;
	pc->request_transfer = 254;
	pc->callback = &idetape_pc_callback;
}

static void idetape_create_rewind_cmd(ide_drive_t *drive, idetape_pc_t *pc)
{
	idetape_init_pc(pc);
	pc->c[0] = REZERO_UNIT;
	set_bit(PC_WAIT_FOR_DSC, &pc->flags);
	pc->callback = &idetape_pc_callback;
}

static void idetape_create_erase_cmd(idetape_pc_t *pc)
{
	idetape_init_pc(pc);
	pc->c[0] = ERASE;
	pc->c[1] = 1;
	set_bit(PC_WAIT_FOR_DSC, &pc->flags);
	pc->callback = &idetape_pc_callback;
}

static void idetape_create_space_cmd(idetape_pc_t *pc, int count, u8 cmd)
{
	idetape_init_pc(pc);
	pc->c[0] = SPACE;
	put_unaligned(cpu_to_be32(count), (unsigned int *) &pc->c[1]);
	pc->c[1] = cmd;
	set_bit(PC_WAIT_FOR_DSC, &pc->flags);
	pc->callback = &idetape_pc_callback;
}

static void idetape_wait_first_stage(ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;
	unsigned long flags;

	if (tape->first_stage == NULL)
		return;
	spin_lock_irqsave(&tape->lock, flags);
	if (tape->active_stage == tape->first_stage)
		idetape_wait_for_request(drive, tape->active_data_rq);
	spin_unlock_irqrestore(&tape->lock, flags);
}

/*
 * Try to add a character device originated write request to our pipeline. In
 * case we don't succeed, we revert to non-pipelined operation mode for this
 * request. In order to accomplish that, we
 *
 * 1. Try to allocate a new pipeline stage.
 * 2. If we can't, wait for more and more requests to be serviced and try again
 * each time.
 * 3. If we still can't allocate a stage, fallback to non-pipelined operation
 * mode for this request.
 */
static int idetape_add_chrdev_write_request(ide_drive_t *drive, int blocks)
{
	idetape_tape_t *tape = drive->driver_data;
	idetape_stage_t *new_stage;
	unsigned long flags;
	struct request *rq;

	debug_log(DBG_CHRDEV, "Enter %s\n", __func__);

	/* Attempt to allocate a new stage. Beware possible race conditions. */
	while ((new_stage = idetape_kmalloc_stage(tape)) == NULL) {
		spin_lock_irqsave(&tape->lock, flags);
		if (idetape_pipeline_active(tape)) {
			idetape_wait_for_request(drive, tape->active_data_rq);
			spin_unlock_irqrestore(&tape->lock, flags);
		} else {
			spin_unlock_irqrestore(&tape->lock, flags);
			idetape_plug_pipeline(drive);
			if (idetape_pipeline_active(tape))
				continue;
			/*
			 * The machine is short on memory. Fallback to non-
			 * pipelined operation mode for this request.
			 */
			return idetape_queue_rw_tail(drive, REQ_IDETAPE_WRITE,
						blocks, tape->merge_stage->bh);
		}
	}
	rq = &new_stage->rq;
	idetape_init_rq(rq, REQ_IDETAPE_WRITE);
	/* Doesn't actually matter - We always assume sequential access */
	rq->sector = tape->first_frame;
	rq->current_nr_sectors = blocks;
	rq->nr_sectors = blocks;

	idetape_switch_buffers(tape, new_stage);
	idetape_add_stage_tail(drive, new_stage);
	tape->pipeline_head++;
	idetape_calculate_speeds(drive);

	/*
	 * Estimate whether the tape has stopped writing by checking if our
	 * write pipeline is currently empty. If we are not writing anymore,
	 * wait for the pipeline to be almost completely full (90%) before
	 * starting to service requests, so that we will be able to keep up with
	 * the higher speeds of the tape.
	 */
	if (!idetape_pipeline_active(tape)) {
		if (tape->nr_stages >= tape->max_stages * 9 / 10 ||
			tape->nr_stages >= tape->max_stages -
			tape->uncontrolled_pipeline_head_speed * 3 * 1024 /
			tape->blk_size) {
			tape->measure_insert_time = 1;
			tape->insert_time = jiffies;
			tape->insert_size = 0;
			tape->insert_speed = 0;
			idetape_plug_pipeline(drive);
		}
	}
	if (test_and_clear_bit(IDETAPE_PIPELINE_ERROR, &tape->flags))
		/* Return a deferred error */
		return -EIO;
	return blocks;
}

/*
 * Wait until all pending pipeline requests are serviced. Typically called on
 * device close.
 */
static void idetape_wait_for_pipeline(ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;
	unsigned long flags;

	while (tape->next_stage || idetape_pipeline_active(tape)) {
		idetape_plug_pipeline(drive);
		spin_lock_irqsave(&tape->lock, flags);
		if (idetape_pipeline_active(tape))
			idetape_wait_for_request(drive, tape->active_data_rq);
		spin_unlock_irqrestore(&tape->lock, flags);
	}
}

static void idetape_empty_write_pipeline(ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;
	int blocks, min;
	struct idetape_bh *bh;

	if (tape->chrdev_dir != IDETAPE_DIR_WRITE) {
		printk(KERN_ERR "ide-tape: bug: Trying to empty write pipeline,"
				" but we are not writing.\n");
		return;
	}
	if (tape->merge_stage_size > tape->stage_size) {
		printk(KERN_ERR "ide-tape: bug: merge_buffer too big\n");
		tape->merge_stage_size = tape->stage_size;
	}
	if (tape->merge_stage_size) {
		blocks = tape->merge_stage_size / tape->blk_size;
		if (tape->merge_stage_size % tape->blk_size) {
			unsigned int i;

			blocks++;
			i = tape->blk_size - tape->merge_stage_size %
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
		tape->merge_stage_size = 0;
	}
	idetape_wait_for_pipeline(drive);
	if (tape->merge_stage != NULL) {
		__idetape_kfree_stage(tape->merge_stage);
		tape->merge_stage = NULL;
	}
	clear_bit(IDETAPE_PIPELINE_ERROR, &tape->flags);
	tape->chrdev_dir = IDETAPE_DIR_NONE;

	/*
	 * On the next backup, perform the feedback loop again. (I don't want to
	 * keep sense information between backups, as some systems are
	 * constantly on, and the system load can be totally different on the
	 * next backup).
	 */
	tape->max_stages = tape->min_pipeline;
	if (tape->first_stage != NULL ||
	    tape->next_stage != NULL ||
	    tape->last_stage != NULL ||
	    tape->nr_stages != 0) {
		printk(KERN_ERR "ide-tape: ide-tape pipeline bug, "
			"first_stage %p, next_stage %p, "
			"last_stage %p, nr_stages %d\n",
			tape->first_stage, tape->next_stage,
			tape->last_stage, tape->nr_stages);
	}
}

static void idetape_restart_speed_control(ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;

	tape->restart_speed_control_req = 0;
	tape->pipeline_head = 0;
	tape->controlled_last_pipeline_head = 0;
	tape->controlled_previous_pipeline_head = 0;
	tape->uncontrolled_previous_pipeline_head = 0;
	tape->controlled_pipeline_head_speed = 5000;
	tape->pipeline_head_speed = 5000;
	tape->uncontrolled_pipeline_head_speed = 0;
	tape->controlled_pipeline_head_time =
		tape->uncontrolled_pipeline_head_time = jiffies;
	tape->controlled_previous_head_time =
		tape->uncontrolled_previous_head_time = jiffies;
}

static int idetape_init_read(ide_drive_t *drive, int max_stages)
{
	idetape_tape_t *tape = drive->driver_data;
	idetape_stage_t *new_stage;
	struct request rq;
	int bytes_read;
	u16 blocks = *(u16 *)&tape->caps[12];

	/* Initialize read operation */
	if (tape->chrdev_dir != IDETAPE_DIR_READ) {
		if (tape->chrdev_dir == IDETAPE_DIR_WRITE) {
			idetape_empty_write_pipeline(drive);
			idetape_flush_tape_buffers(drive);
		}
		if (tape->merge_stage || tape->merge_stage_size) {
			printk(KERN_ERR "ide-tape: merge_stage_size should be"
					 " 0 now\n");
			tape->merge_stage_size = 0;
		}
		tape->merge_stage = __idetape_kmalloc_stage(tape, 0, 0);
		if (!tape->merge_stage)
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
							tape->merge_stage->bh);
			if (bytes_read < 0) {
				__idetape_kfree_stage(tape->merge_stage);
				tape->merge_stage = NULL;
				tape->chrdev_dir = IDETAPE_DIR_NONE;
				return bytes_read;
			}
		}
	}
	if (tape->restart_speed_control_req)
		idetape_restart_speed_control(drive);
	idetape_init_rq(&rq, REQ_IDETAPE_READ);
	rq.sector = tape->first_frame;
	rq.nr_sectors = blocks;
	rq.current_nr_sectors = blocks;
	if (!test_bit(IDETAPE_PIPELINE_ERROR, &tape->flags) &&
	    tape->nr_stages < max_stages) {
		new_stage = idetape_kmalloc_stage(tape);
		while (new_stage != NULL) {
			new_stage->rq = rq;
			idetape_add_stage_tail(drive, new_stage);
			if (tape->nr_stages >= max_stages)
				break;
			new_stage = idetape_kmalloc_stage(tape);
		}
	}
	if (!idetape_pipeline_active(tape)) {
		if (tape->nr_pending_stages >= 3 * max_stages / 4) {
			tape->measure_insert_time = 1;
			tape->insert_time = jiffies;
			tape->insert_size = 0;
			tape->insert_speed = 0;
			idetape_plug_pipeline(drive);
		}
	}
	return 0;
}

/*
 * Called from idetape_chrdev_read() to service a character device read request
 * and add read-ahead requests to our pipeline.
 */
static int idetape_add_chrdev_read_request(ide_drive_t *drive, int blocks)
{
	idetape_tape_t *tape = drive->driver_data;
	unsigned long flags;
	struct request *rq_ptr;
	int bytes_read;

	debug_log(DBG_PROCS, "Enter %s, %d blocks\n", __func__, blocks);

	/* If we are at a filemark, return a read length of 0 */
	if (test_bit(IDETAPE_FILEMARK, &tape->flags))
		return 0;

	/* Wait for the next block to reach the head of the pipeline. */
	idetape_init_read(drive, tape->max_stages);
	if (tape->first_stage == NULL) {
		if (test_bit(IDETAPE_PIPELINE_ERROR, &tape->flags))
			return 0;
		return idetape_queue_rw_tail(drive, REQ_IDETAPE_READ, blocks,
					tape->merge_stage->bh);
	}
	idetape_wait_first_stage(drive);
	rq_ptr = &tape->first_stage->rq;
	bytes_read = tape->blk_size * (rq_ptr->nr_sectors -
					rq_ptr->current_nr_sectors);
	rq_ptr->nr_sectors = 0;
	rq_ptr->current_nr_sectors = 0;

	if (rq_ptr->errors == IDETAPE_ERROR_EOD)
		return 0;
	else {
		idetape_switch_buffers(tape, tape->first_stage);
		if (rq_ptr->errors == IDETAPE_ERROR_FILEMARK)
			set_bit(IDETAPE_FILEMARK, &tape->flags);
		spin_lock_irqsave(&tape->lock, flags);
		idetape_remove_stage_head(drive);
		spin_unlock_irqrestore(&tape->lock, flags);
		tape->pipeline_head++;
		idetape_calculate_speeds(drive);
	}
	if (bytes_read > blocks * tape->blk_size) {
		printk(KERN_ERR "ide-tape: bug: trying to return more bytes"
				" than requested\n");
		bytes_read = blocks * tape->blk_size;
	}
	return (bytes_read);
}

static void idetape_pad_zeros(ide_drive_t *drive, int bcount)
{
	idetape_tape_t *tape = drive->driver_data;
	struct idetape_bh *bh;
	int blocks;

	while (bcount) {
		unsigned int count;

		bh = tape->merge_stage->bh;
		count = min(tape->stage_size, bcount);
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
				      tape->merge_stage->bh);
	}
}

static int idetape_pipeline_size(ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;
	idetape_stage_t *stage;
	struct request *rq;
	int size = 0;

	idetape_wait_for_pipeline(drive);
	stage = tape->first_stage;
	while (stage != NULL) {
		rq = &stage->rq;
		size += tape->blk_size * (rq->nr_sectors -
				rq->current_nr_sectors);
		if (rq->errors == IDETAPE_ERROR_FILEMARK)
			size += tape->blk_size;
		stage = stage->next;
	}
	size += tape->merge_stage_size;
	return size;
}

/*
 * Rewinds the tape to the Beginning Of the current Partition (BOP). We
 * currently support only one partition.
 */
static int idetape_rewind_tape(ide_drive_t *drive)
{
	int retval;
	idetape_pc_t pc;
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
		tape->max_stages = config.nr_stages;
		break;
	case 0x0350:
		config.dsc_rw_frequency = (int) tape->best_dsc_rw_freq;
		config.nr_stages = tape->max_stages;
		if (copy_to_user(argp, &config, sizeof(config)))
			return -EFAULT;
		break;
	default:
		return -EIO;
	}
	return 0;
}

/*
 * The function below is now a bit more complicated than just passing the
 * command to the tape since we may have crossed some filemarks during our
 * pipelined read-ahead mode. As a minor side effect, the pipeline enables us to
 * support MTFSFM when the filemark is in our internal pipeline even if the tape
 * doesn't support spacing over filemarks in the reverse direction.
 */
static int idetape_space_over_filemarks(ide_drive_t *drive, short mt_op,
					int mt_count)
{
	idetape_tape_t *tape = drive->driver_data;
	idetape_pc_t pc;
	unsigned long flags;
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
		/* its a read-ahead buffer, scan it for crossed filemarks. */
		tape->merge_stage_size = 0;
		if (test_and_clear_bit(IDETAPE_FILEMARK, &tape->flags))
			++count;
		while (tape->first_stage != NULL) {
			if (count == mt_count) {
				if (mt_op == MTFSFM)
					set_bit(IDETAPE_FILEMARK, &tape->flags);
				return 0;
			}
			spin_lock_irqsave(&tape->lock, flags);
			if (tape->first_stage == tape->active_stage) {
				/*
				 * We have reached the active stage in the read
				 * pipeline. There is no point in allowing the
				 * drive to continue reading any farther, so we
				 * stop the pipeline.
				 *
				 * This section should be moved to a separate
				 * subroutine because similar operations are
				 * done in __idetape_discard_read_pipeline(),
				 * for example.
				 */
				tape->next_stage = NULL;
				spin_unlock_irqrestore(&tape->lock, flags);
				idetape_wait_first_stage(drive);
				tape->next_stage = tape->first_stage->next;
			} else
				spin_unlock_irqrestore(&tape->lock, flags);
			if (tape->first_stage->rq.errors ==
					IDETAPE_ERROR_FILEMARK)
				++count;
			idetape_remove_stage_head(drive);
		}
		idetape_discard_read_pipeline(drive, 0);
	}

	/*
	 * The filemark was not found in our internal pipeline;	now we can issue
	 * the space command.
	 */
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
		if (test_bit(IDETAPE_DETECT_BS, &tape->flags))
			if (count > tape->blk_size &&
			    (count % tape->blk_size) == 0)
				tape->user_bs_factor = count / tape->blk_size;
	}
	rc = idetape_init_read(drive, tape->max_stages);
	if (rc < 0)
		return rc;
	if (count == 0)
		return (0);
	if (tape->merge_stage_size) {
		actually_read = min((unsigned int)(tape->merge_stage_size),
				    (unsigned int)count);
		if (idetape_copy_stage_to_user(tape, buf, tape->merge_stage,
					       actually_read))
			ret = -EFAULT;
		buf += actually_read;
		tape->merge_stage_size -= actually_read;
		count -= actually_read;
	}
	while (count >= tape->stage_size) {
		bytes_read = idetape_add_chrdev_read_request(drive, ctl);
		if (bytes_read <= 0)
			goto finish;
		if (idetape_copy_stage_to_user(tape, buf, tape->merge_stage,
					       bytes_read))
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
		if (idetape_copy_stage_to_user(tape, buf, tape->merge_stage,
					       temp))
			ret = -EFAULT;
		actually_read += temp;
		tape->merge_stage_size = bytes_read-temp;
	}
finish:
	if (!actually_read && test_bit(IDETAPE_FILEMARK, &tape->flags)) {
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
			idetape_discard_read_pipeline(drive, 1);
		if (tape->merge_stage || tape->merge_stage_size) {
			printk(KERN_ERR "ide-tape: merge_stage_size "
				"should be 0 now\n");
			tape->merge_stage_size = 0;
		}
		tape->merge_stage = __idetape_kmalloc_stage(tape, 0, 0);
		if (!tape->merge_stage)
			return -ENOMEM;
		tape->chrdev_dir = IDETAPE_DIR_WRITE;
		idetape_init_merge_stage(tape);

		/*
		 * Issue a write 0 command to ensure that DSC handshake is
		 * switched from completion mode to buffer available mode. No
		 * point in issuing this if DSC overlap isn't supported, some
		 * drives (Seagate STT3401A) will return an error.
		 */
		if (drive->dsc_overlap) {
			ssize_t retval = idetape_queue_rw_tail(drive,
							REQ_IDETAPE_WRITE, 0,
							tape->merge_stage->bh);
			if (retval < 0) {
				__idetape_kfree_stage(tape->merge_stage);
				tape->merge_stage = NULL;
				tape->chrdev_dir = IDETAPE_DIR_NONE;
				return retval;
			}
		}
	}
	if (count == 0)
		return (0);
	if (tape->restart_speed_control_req)
		idetape_restart_speed_control(drive);
	if (tape->merge_stage_size) {
		if (tape->merge_stage_size >= tape->stage_size) {
			printk(KERN_ERR "ide-tape: bug: merge buf too big\n");
			tape->merge_stage_size = 0;
		}
		actually_written = min((unsigned int)
				(tape->stage_size - tape->merge_stage_size),
				(unsigned int)count);
		if (idetape_copy_stage_from_user(tape, tape->merge_stage, buf,
						 actually_written))
				ret = -EFAULT;
		buf += actually_written;
		tape->merge_stage_size += actually_written;
		count -= actually_written;

		if (tape->merge_stage_size == tape->stage_size) {
			ssize_t retval;
			tape->merge_stage_size = 0;
			retval = idetape_add_chrdev_write_request(drive, ctl);
			if (retval <= 0)
				return (retval);
		}
	}
	while (count >= tape->stage_size) {
		ssize_t retval;
		if (idetape_copy_stage_from_user(tape, tape->merge_stage, buf,
						 tape->stage_size))
			ret = -EFAULT;
		buf += tape->stage_size;
		count -= tape->stage_size;
		retval = idetape_add_chrdev_write_request(drive, ctl);
		actually_written += tape->stage_size;
		if (retval <= 0)
			return (retval);
	}
	if (count) {
		actually_written += count;
		if (idetape_copy_stage_from_user(tape, tape->merge_stage, buf,
						 count))
			ret = -EFAULT;
		tape->merge_stage_size += count;
	}
	return ret ? ret : actually_written;
}

static int idetape_write_filemark(ide_drive_t *drive)
{
	idetape_pc_t pc;

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
 * usually not supported (it is supported in the rare case in which we crossed
 * the filemark during our read-ahead pipelined operation mode).
 *
 * The following commands are currently not supported:
 *
 * MTFSS, MTBSS, MTWSM, MTSETDENSITY, MTSETDRVBUFFER, MT_ST_BOOLEANS,
 * MT_ST_WRITE_THRESHOLD.
 */
static int idetape_mtioctop(ide_drive_t *drive, short mt_op, int mt_count)
{
	idetape_tape_t *tape = drive->driver_data;
	idetape_pc_t pc;
	int i, retval;

	debug_log(DBG_ERR, "Handling MTIOCTOP ioctl: mt_op=%d, mt_count=%d\n",
			mt_op, mt_count);

	/* Commands which need our pipelined read-ahead stages. */
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
		idetape_discard_read_pipeline(drive, 1);
		for (i = 0; i < mt_count; i++) {
			retval = idetape_write_filemark(drive);
			if (retval)
				return retval;
		}
		return 0;
	case MTREW:
		idetape_discard_read_pipeline(drive, 0);
		if (idetape_rewind_tape(drive))
			return -EIO;
		return 0;
	case MTLOAD:
		idetape_discard_read_pipeline(drive, 0);
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
		idetape_discard_read_pipeline(drive, 0);
		idetape_create_load_unload_cmd(drive, &pc,
					      !IDETAPE_LU_LOAD_MASK);
		retval = idetape_queue_pc_tail(drive, &pc);
		if (!retval)
			clear_bit(IDETAPE_MEDIUM_PRESENT, &tape->flags);
		return retval;
	case MTNOP:
		idetape_discard_read_pipeline(drive, 0);
		return idetape_flush_tape_buffers(drive);
	case MTRETEN:
		idetape_discard_read_pipeline(drive, 0);
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
			clear_bit(IDETAPE_DETECT_BS, &tape->flags);
		} else
			set_bit(IDETAPE_DETECT_BS, &tape->flags);
		return 0;
	case MTSEEK:
		idetape_discard_read_pipeline(drive, 0);
		return idetape_position_tape(drive,
			mt_count * tape->user_bs_factor, tape->partition, 0);
	case MTSETPART:
		idetape_discard_read_pipeline(drive, 0);
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

	tape->restart_speed_control_req = 1;
	if (tape->chrdev_dir == IDETAPE_DIR_WRITE) {
		idetape_empty_write_pipeline(drive);
		idetape_flush_tape_buffers(drive);
	}
	if (cmd == MTIOCGET || cmd == MTIOCPOS) {
		block_offset = idetape_pipeline_size(drive) /
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
			idetape_discard_read_pipeline(drive, 1);
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
	idetape_pc_t pc;

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
	tape->blk_size = (pc.buffer[4 + 5] << 16) +
				(pc.buffer[4 + 6] << 8)  +
				 pc.buffer[4 + 7];
	tape->drv_write_prot = (pc.buffer[2] & 0x80) >> 7;
}

static int idetape_chrdev_open(struct inode *inode, struct file *filp)
{
	unsigned int minor = iminor(inode), i = minor & ~0xc0;
	ide_drive_t *drive;
	idetape_tape_t *tape;
	idetape_pc_t pc;
	int retval;

	if (i >= MAX_HWIFS * MAX_DRIVES)
		return -ENXIO;

	tape = ide_tape_chrdev_get(i);
	if (!tape)
		return -ENXIO;

	debug_log(DBG_CHRDEV, "Enter %s\n", __func__);

	/*
	 * We really want to do nonseekable_open(inode, filp); here, but some
	 * versions of tar incorrectly call lseek on tapes and bail out if that
	 * fails.  So we disallow pread() and pwrite(), but permit lseeks.
	 */
	filp->f_mode &= ~(FMODE_PREAD | FMODE_PWRITE);

	drive = tape->drive;

	filp->private_data = tape;

	if (test_and_set_bit(IDETAPE_BUSY, &tape->flags)) {
		retval = -EBUSY;
		goto out_put_tape;
	}

	retval = idetape_wait_ready(drive, 60 * HZ);
	if (retval) {
		clear_bit(IDETAPE_BUSY, &tape->flags);
		printk(KERN_ERR "ide-tape: %s: drive not ready\n", tape->name);
		goto out_put_tape;
	}

	idetape_read_position(drive);
	if (!test_bit(IDETAPE_ADDRESS_VALID, &tape->flags))
		(void)idetape_rewind_tape(drive);

	if (tape->chrdev_dir != IDETAPE_DIR_READ)
		clear_bit(IDETAPE_PIPELINE_ERROR, &tape->flags);

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
			clear_bit(IDETAPE_BUSY, &tape->flags);
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
	idetape_restart_speed_control(drive);
	tape->restart_speed_control_req = 0;
	return 0;

out_put_tape:
	ide_tape_put(tape);
	return retval;
}

static void idetape_write_release(ide_drive_t *drive, unsigned int minor)
{
	idetape_tape_t *tape = drive->driver_data;

	idetape_empty_write_pipeline(drive);
	tape->merge_stage = __idetape_kmalloc_stage(tape, 1, 0);
	if (tape->merge_stage != NULL) {
		idetape_pad_zeros(drive, tape->blk_size *
				(tape->user_bs_factor - 1));
		__idetape_kfree_stage(tape->merge_stage);
		tape->merge_stage = NULL;
	}
	idetape_write_filemark(drive);
	idetape_flush_tape_buffers(drive);
	idetape_flush_tape_buffers(drive);
}

static int idetape_chrdev_release(struct inode *inode, struct file *filp)
{
	struct ide_tape_obj *tape = ide_tape_f(filp);
	ide_drive_t *drive = tape->drive;
	idetape_pc_t pc;
	unsigned int minor = iminor(inode);

	lock_kernel();
	tape = drive->driver_data;

	debug_log(DBG_CHRDEV, "Enter %s\n", __func__);

	if (tape->chrdev_dir == IDETAPE_DIR_WRITE)
		idetape_write_release(drive, minor);
	if (tape->chrdev_dir == IDETAPE_DIR_READ) {
		if (minor < 128)
			idetape_discard_read_pipeline(drive, 1);
		else
			idetape_wait_for_pipeline(drive);
	}
	if (tape->cache_stage != NULL) {
		__idetape_kfree_stage(tape->cache_stage);
		tape->cache_stage = NULL;
	}
	if (minor < 128 && test_bit(IDETAPE_MEDIUM_PRESENT, &tape->flags))
		(void) idetape_rewind_tape(drive);
	if (tape->chrdev_dir == IDETAPE_DIR_NONE) {
		if (tape->door_locked == DOOR_LOCKED) {
			if (idetape_create_prevent_cmd(drive, &pc, 0)) {
				if (!idetape_queue_pc_tail(drive, &pc))
					tape->door_locked = DOOR_UNLOCKED;
			}
		}
	}
	clear_bit(IDETAPE_BUSY, &tape->flags);
	ide_tape_put(tape);
	unlock_kernel();
	return 0;
}

/*
 * check the contents of the ATAPI IDENTIFY command results. We return:
 *
 * 1 - If the tape can be supported by us, based on the information we have so
 * far.
 *
 * 0 - If this tape driver is not currently supported by us.
 */
static int idetape_identify_device(ide_drive_t *drive)
{
	u8 gcw[2], protocol, device_type, removable, packet_size;

	if (drive->id_read == 0)
		return 1;

	*((unsigned short *) &gcw) = drive->id->config;

	protocol	=   (gcw[1] & 0xC0) >> 6;
	device_type	=    gcw[1] & 0x1F;
	removable	= !!(gcw[0] & 0x80);
	packet_size	=    gcw[0] & 0x3;

	/* Check that we can support this device */
	if (protocol != 2)
		printk(KERN_ERR "ide-tape: Protocol (0x%02x) is not ATAPI\n",
				protocol);
	else if (device_type != 1)
		printk(KERN_ERR "ide-tape: Device type (0x%02x) is not set "
				"to tape\n", device_type);
	else if (!removable)
		printk(KERN_ERR "ide-tape: The removable flag is not set\n");
	else if (packet_size != 0) {
		printk(KERN_ERR "ide-tape: Packet size (0x%02x) is not 12"
				" bytes\n", packet_size);
	} else
		return 1;
	return 0;
}

static void idetape_get_inquiry_results(ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;
	idetape_pc_t pc;
	char fw_rev[6], vendor_id[10], product_id[18];

	idetape_create_inquiry_cmd(&pc);
	if (idetape_queue_pc_tail(drive, &pc)) {
		printk(KERN_ERR "ide-tape: %s: can't get INQUIRY results\n",
				tape->name);
		return;
	}
	memcpy(vendor_id, &pc.buffer[8], 8);
	memcpy(product_id, &pc.buffer[16], 16);
	memcpy(fw_rev, &pc.buffer[32], 4);

	ide_fixstring(vendor_id, 10, 0);
	ide_fixstring(product_id, 18, 0);
	ide_fixstring(fw_rev, 6, 0);

	printk(KERN_INFO "ide-tape: %s <-> %s: %s %s rev %s\n",
			drive->name, tape->name, vendor_id, product_id, fw_rev);
}

/*
 * Ask the tape about its various parameters. In particular, we will adjust our
 * data transfer buffer	size to the recommended value as returned by the tape.
 */
static void idetape_get_mode_sense_results(ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;
	idetape_pc_t pc;
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
	caps = pc.buffer + 4 + pc.buffer[3];

	/* convert to host order and save for later use */
	speed = be16_to_cpu(*(u16 *)&caps[14]);
	max_speed = be16_to_cpu(*(u16 *)&caps[8]);

	put_unaligned(max_speed, (u16 *)&caps[8]);
	put_unaligned(be16_to_cpu(*(u16 *)&caps[12]), (u16 *)&caps[12]);
	put_unaligned(speed, (u16 *)&caps[14]);
	put_unaligned(be16_to_cpu(*(u16 *)&caps[16]), (u16 *)&caps[16]);

	if (!speed) {
		printk(KERN_INFO "ide-tape: %s: invalid tape speed "
				"(assuming 650KB/sec)\n", drive->name);
		put_unaligned(650, (u16 *)&caps[14]);
	}
	if (!max_speed) {
		printk(KERN_INFO "ide-tape: %s: invalid max_speed "
				"(assuming 650KB/sec)\n", drive->name);
		put_unaligned(650, (u16 *)&caps[8]);
	}

	memcpy(&tape->caps, caps, 20);
	if (caps[7] & 0x02)
		tape->blk_size = 512;
	else if (caps[7] & 0x04)
		tape->blk_size = 1024;
}

#ifdef CONFIG_IDE_PROC_FS
static void idetape_add_settings(ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;

	ide_add_setting(drive, "buffer", SETTING_READ, TYPE_SHORT, 0, 0xffff,
			1, 2, (u16 *)&tape->caps[16], NULL);
	ide_add_setting(drive, "pipeline_min", SETTING_RW, TYPE_INT, 1, 0xffff,
			tape->stage_size / 1024, 1, &tape->min_pipeline, NULL);
	ide_add_setting(drive, "pipeline", SETTING_RW, TYPE_INT, 1, 0xffff,
			tape->stage_size / 1024, 1, &tape->max_stages, NULL);
	ide_add_setting(drive, "pipeline_max", SETTING_RW, TYPE_INT, 1,	0xffff,
			tape->stage_size / 1024, 1, &tape->max_pipeline, NULL);
	ide_add_setting(drive, "pipeline_used",	SETTING_READ, TYPE_INT, 0,
			0xffff,	tape->stage_size / 1024, 1, &tape->nr_stages,
			NULL);
	ide_add_setting(drive, "pipeline_pending", SETTING_READ, TYPE_INT, 0,
			0xffff, tape->stage_size / 1024, 1,
			&tape->nr_pending_stages, NULL);
	ide_add_setting(drive, "speed", SETTING_READ, TYPE_SHORT, 0, 0xffff,
			1, 1, (u16 *)&tape->caps[14], NULL);
	ide_add_setting(drive, "stage", SETTING_READ, TYPE_INT,	0, 0xffff, 1,
			1024, &tape->stage_size, NULL);
	ide_add_setting(drive, "tdsc", SETTING_RW, TYPE_INT, IDETAPE_DSC_RW_MIN,
			IDETAPE_DSC_RW_MAX, 1000, HZ, &tape->best_dsc_rw_freq,
			NULL);
	ide_add_setting(drive, "dsc_overlap", SETTING_RW, TYPE_BYTE, 0, 1, 1,
			1, &drive->dsc_overlap, NULL);
	ide_add_setting(drive, "pipeline_head_speed_c", SETTING_READ, TYPE_INT,
			0, 0xffff, 1, 1, &tape->controlled_pipeline_head_speed,
			NULL);
	ide_add_setting(drive, "pipeline_head_speed_u", SETTING_READ, TYPE_INT,
			0, 0xffff, 1, 1,
			&tape->uncontrolled_pipeline_head_speed, NULL);
	ide_add_setting(drive, "avg_speed", SETTING_READ, TYPE_INT, 0, 0xffff,
			1, 1, &tape->avg_speed, NULL);
	ide_add_setting(drive, "debug_mask", SETTING_RW, TYPE_INT, 0, 0xffff, 1,
			1, &tape->debug_mask, NULL);
}
#else
static inline void idetape_add_settings(ide_drive_t *drive) { ; }
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
	unsigned long t1, tmid, tn, t;
	int speed;
	int stage_size;
	u8 gcw[2];
	struct sysinfo si;
	u16 *ctl = (u16 *)&tape->caps[12];

	spin_lock_init(&tape->lock);
	drive->dsc_overlap = 1;
	if (drive->hwif->host_flags & IDE_HFLAG_NO_DSC) {
		printk(KERN_INFO "ide-tape: %s: disabling DSC overlap\n",
				 tape->name);
		drive->dsc_overlap = 0;
	}
	/* Seagate Travan drives do not support DSC overlap. */
	if (strstr(drive->id->model, "Seagate STT3401"))
		drive->dsc_overlap = 0;
	tape->minor = minor;
	tape->name[0] = 'h';
	tape->name[1] = 't';
	tape->name[2] = '0' + minor;
	tape->chrdev_dir = IDETAPE_DIR_NONE;
	tape->pc = tape->pc_stack;
	tape->max_insert_speed = 10000;
	tape->speed_control = 1;
	*((unsigned short *) &gcw) = drive->id->config;

	/* Command packet DRQ type */
	if (((gcw[0] & 0x60) >> 5) == 1)
		set_bit(IDETAPE_DRQ_INTERRUPT, &tape->flags);

	tape->min_pipeline = 10;
	tape->max_pipeline = 10;
	tape->max_stages   = 10;

	idetape_get_inquiry_results(drive);
	idetape_get_mode_sense_results(drive);
	ide_tape_get_bsize_from_bdesc(drive);
	tape->user_bs_factor = 1;
	tape->stage_size = *ctl * tape->blk_size;
	while (tape->stage_size > 0xffff) {
		printk(KERN_NOTICE "ide-tape: decreasing stage size\n");
		*ctl /= 2;
		tape->stage_size = *ctl * tape->blk_size;
	}
	stage_size = tape->stage_size;
	tape->pages_per_stage = stage_size / PAGE_SIZE;
	if (stage_size % PAGE_SIZE) {
		tape->pages_per_stage++;
		tape->excess_bh_size = PAGE_SIZE - stage_size % PAGE_SIZE;
	}

	/* Select the "best" DSC read/write polling freq and pipeline size. */
	speed = max(*(u16 *)&tape->caps[14], *(u16 *)&tape->caps[8]);

	tape->max_stages = speed * 1000 * 10 / tape->stage_size;

	/* Limit memory use for pipeline to 10% of physical memory */
	si_meminfo(&si);
	if (tape->max_stages * tape->stage_size >
			si.totalram * si.mem_unit / 10)
		tape->max_stages =
			si.totalram * si.mem_unit / (10 * tape->stage_size);

	tape->max_stages   = min(tape->max_stages, IDETAPE_MAX_PIPELINE_STAGES);
	tape->min_pipeline = min(tape->max_stages, IDETAPE_MIN_PIPELINE_STAGES);
	tape->max_pipeline =
		min(tape->max_stages * 2, IDETAPE_MAX_PIPELINE_STAGES);
	if (tape->max_stages == 0) {
		tape->max_stages   = 1;
		tape->min_pipeline = 1;
		tape->max_pipeline = 1;
	}

	t1 = (tape->stage_size * HZ) / (speed * 1000);
	tmid = (*(u16 *)&tape->caps[16] * 32 * HZ) / (speed * 125);
	tn = (IDETAPE_FIFO_THRESHOLD * tape->stage_size * HZ) / (speed * 1000);

	if (tape->max_stages)
		t = tn;
	else
		t = t1;

	/*
	 * Ensure that the number we got makes sense; limit it within
	 * IDETAPE_DSC_RW_MIN and IDETAPE_DSC_RW_MAX.
	 */
	tape->best_dsc_rw_freq = max_t(unsigned long,
				min_t(unsigned long, t, IDETAPE_DSC_RW_MAX),
				IDETAPE_DSC_RW_MIN);
	printk(KERN_INFO "ide-tape: %s <-> %s: %dKBps, %d*%dkB buffer, "
		"%dkB pipeline, %lums tDSC%s\n",
		drive->name, tape->name, *(u16 *)&tape->caps[14],
		(*(u16 *)&tape->caps[16] * 512) / tape->stage_size,
		tape->stage_size / 1024,
		tape->max_stages * tape->stage_size / 1024,
		tape->best_dsc_rw_freq * 1000 / HZ,
		drive->using_dma ? ", DMA":"");

	idetape_add_settings(drive);
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

	BUG_ON(tape->first_stage != NULL || tape->merge_stage_size);

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
	.supports_dsc_overlap 	= 1,
	.do_request		= idetape_do_request,
	.end_request		= idetape_end_request,
	.error			= __ide_error,
	.abort			= __ide_abort,
#ifdef CONFIG_IDE_PROC_FS
	.proc			= idetape_proc,
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
	if (!drive->present)
		goto failed;
	if (drive->media != ide_tape)
		goto failed;
	if (!idetape_identify_device(drive)) {
		printk(KERN_ERR "ide-tape: %s: not supported by this version of"
				" the driver\n", drive->name);
		goto failed;
	}
	if (drive->scsi) {
		printk(KERN_INFO "ide-tape: passing drive %s to ide-scsi"
				 " emulation.\n", drive->name);
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

	ide_proc_register_driver(drive, &idetape_driver);

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

	device_create(idetape_sysfs_class, &drive->gendev,
		      MKDEV(IDETAPE_MAJOR, minor), "%s", tape->name);
	device_create(idetape_sysfs_class, &drive->gendev,
			MKDEV(IDETAPE_MAJOR, minor + 128), "n%s", tape->name);

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
