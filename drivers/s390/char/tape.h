/*
 *  drivers/s390/char/tape.h
 *    tape device driver for 3480/3490E/3590 tapes.
 *
 *  S390 and zSeries version
 *    Copyright IBM Corp. 2001, 2009
 *    Author(s): Carsten Otte <cotte@de.ibm.com>
 *		 Tuan Ngo-Anh <ngoanh@de.ibm.com>
 *		 Martin Schwidefsky <schwidefsky@de.ibm.com>
 *		 Stefan Bader <shbader@de.ibm.com>
 */

#ifndef _TAPE_H
#define _TAPE_H

#include <asm/ccwdev.h>
#include <asm/debug.h>
#include <asm/idals.h>
#include <linux/blkdev.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mtio.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>

struct gendisk;

/*
 * Define DBF_LIKE_HELL for lots of messages in the debug feature.
 */
#define DBF_LIKE_HELL
#ifdef  DBF_LIKE_HELL
#define DBF_LH(level, str, ...) \
do { \
	debug_sprintf_event(TAPE_DBF_AREA, level, str, ## __VA_ARGS__); \
} while (0)
#else
#define DBF_LH(level, str, ...) do {} while(0)
#endif

/*
 * macros s390 debug feature (dbf)
 */
#define DBF_EVENT(d_level, d_str...) \
do { \
	debug_sprintf_event(TAPE_DBF_AREA, d_level, d_str); \
} while (0)

#define DBF_EXCEPTION(d_level, d_str...) \
do { \
	debug_sprintf_exception(TAPE_DBF_AREA, d_level, d_str); \
} while (0)

#define TAPE_VERSION_MAJOR 2
#define TAPE_VERSION_MINOR 0
#define TAPE_MAGIC "tape"

#define TAPE_MINORS_PER_DEV 2	    /* two minors per device */
#define TAPEBLOCK_HSEC_SIZE	2048
#define TAPEBLOCK_HSEC_S2B	2
#define TAPEBLOCK_RETRIES	5

enum tape_medium_state {
	MS_UNKNOWN,
	MS_LOADED,
	MS_UNLOADED,
	MS_SIZE
};

enum tape_state {
	TS_UNUSED=0,
	TS_IN_USE,
	TS_BLKUSE,
	TS_INIT,
	TS_NOT_OPER,
	TS_SIZE
};

enum tape_op {
	TO_BLOCK,	/* Block read */
	TO_BSB,		/* Backward space block */
	TO_BSF,		/* Backward space filemark */
	TO_DSE,		/* Data security erase */
	TO_FSB,		/* Forward space block */
	TO_FSF,		/* Forward space filemark */
	TO_LBL,		/* Locate block label */
	TO_NOP,		/* No operation */
	TO_RBA,		/* Read backward */
	TO_RBI,		/* Read block information */
	TO_RFO,		/* Read forward */
	TO_REW,		/* Rewind tape */
	TO_RUN,		/* Rewind and unload tape */
	TO_WRI,		/* Write block */
	TO_WTM,		/* Write tape mark */
	TO_MSEN,	/* Medium sense */
	TO_LOAD,	/* Load tape */
	TO_READ_CONFIG, /* Read configuration data */
	TO_READ_ATTMSG, /* Read attention message */
	TO_DIS,		/* Tape display */
	TO_ASSIGN,	/* Assign tape to channel path */
	TO_UNASSIGN,	/* Unassign tape from channel path */
	TO_CRYPT_ON,	/* Enable encrpytion */
	TO_CRYPT_OFF,	/* Disable encrpytion */
	TO_KEKL_SET,	/* Set KEK label */
	TO_KEKL_QUERY,	/* Query KEK label */
	TO_RDC,		/* Read device characteristics */
	TO_SIZE,	/* #entries in tape_op_t */
};

/* Forward declaration */
struct tape_device;

/* tape_request->status can be: */
enum tape_request_status {
	TAPE_REQUEST_INIT,	/* request is ready to be processed */
	TAPE_REQUEST_QUEUED,	/* request is queued to be processed */
	TAPE_REQUEST_IN_IO,	/* request is currently in IO */
	TAPE_REQUEST_DONE,	/* request is completed. */
	TAPE_REQUEST_CANCEL,	/* request should be canceled. */
	TAPE_REQUEST_LONG_BUSY, /* request has to be restarted after long busy */
};

/* Tape CCW request */
struct tape_request {
	struct list_head list;		/* list head for request queueing. */
	struct tape_device *device;	/* tape device of this request */
	struct ccw1 *cpaddr;		/* address of the channel program. */
	void *cpdata;			/* pointer to ccw data. */
	enum tape_request_status status;/* status of this request */
	int options;			/* options for execution. */
	int retries;			/* retry counter for error recovery. */
	int rescnt;			/* residual count from devstat. */

	/* Callback for delivering final status. */
	void (*callback)(struct tape_request *, void *);
	void *callback_data;

	enum tape_op op;
	int rc;
};

/* Function type for magnetic tape commands */
typedef int (*tape_mtop_fn)(struct tape_device *, int);

/* Size of the array containing the mtops for a discipline */
#define TAPE_NR_MTOPS (MTMKPART+1)

/* Tape Discipline */
struct tape_discipline {
	struct module *owner;
	int  (*setup_device)(struct tape_device *);
	void (*cleanup_device)(struct tape_device *);
	int (*irq)(struct tape_device *, struct tape_request *, struct irb *);
	struct tape_request *(*read_block)(struct tape_device *, size_t);
	struct tape_request *(*write_block)(struct tape_device *, size_t);
	void (*process_eov)(struct tape_device*);
#ifdef CONFIG_S390_TAPE_BLOCK
	/* Block device stuff. */
	struct tape_request *(*bread)(struct tape_device *, struct request *);
	void (*check_locate)(struct tape_device *, struct tape_request *);
	void (*free_bread)(struct tape_request *);
#endif
	/* ioctl function for additional ioctls. */
	int (*ioctl_fn)(struct tape_device *, unsigned int, unsigned long);
	/* Array of tape commands with TAPE_NR_MTOPS entries */
	tape_mtop_fn *mtop_array;
};

/*
 * The discipline irq function either returns an error code (<0) which
 * means that the request has failed with an error or one of the following:
 */
#define TAPE_IO_SUCCESS		0	/* request successful */
#define TAPE_IO_PENDING		1	/* request still running */
#define TAPE_IO_RETRY		2	/* retry to current request */
#define TAPE_IO_STOP		3	/* stop the running request */
#define TAPE_IO_LONG_BUSY	4	/* delay the running request */

/* Char Frontend Data */
struct tape_char_data {
	struct idal_buffer *idal_buf;	/* idal buffer for user char data */
	int block_size;			/*   of size block_size. */
};

#ifdef CONFIG_S390_TAPE_BLOCK
/* Block Frontend Data */
struct tape_blk_data
{
	struct tape_device *	device;
	/* Block device request queue. */
	struct request_queue *	request_queue;
	spinlock_t		request_queue_lock;

	/* Task to move entries from block request to CCS request queue. */
	struct work_struct	requeue_task;
	atomic_t		requeue_scheduled;

	/* Current position on the tape. */
	long			block_position;
	int			medium_changed;
	struct gendisk *	disk;
};
#endif

/* Tape Info */
struct tape_device {
	/* entry in tape_device_list */
	struct list_head		node;

	int				cdev_id;
	struct ccw_device *		cdev;
	struct tape_class_device *	nt;
	struct tape_class_device *	rt;

	/* Device discipline information. */
	struct tape_discipline *	discipline;
	void *				discdata;

	/* Generic status flags */
	long				tape_generic_status;

	/* Device state information. */
	wait_queue_head_t		state_change_wq;
	enum tape_state			tape_state;
	enum tape_medium_state		medium_state;
	unsigned char *			modeset_byte;

	/* Reference count. */
	atomic_t			ref_count;

	/* Request queue. */
	struct list_head		req_queue;

	/* Request wait queue. */
	wait_queue_head_t		wait_queue;

	/* Each tape device has (currently) two minor numbers. */
	int				first_minor;

	/* Number of tapemarks required for correct termination. */
	int				required_tapemarks;

	/* Block ID of the BOF */
	unsigned int			bof;

	/* Character device frontend data */
	struct tape_char_data		char_data;
#ifdef CONFIG_S390_TAPE_BLOCK
	/* Block dev frontend data */
	struct tape_blk_data		blk_data;
#endif

	/* Function to start or stop the next request later. */
	struct delayed_work		tape_dnr;

	/* Timer for long busy */
	struct timer_list		lb_timeout;

};

/* Externals from tape_core.c */
extern struct tape_request *tape_alloc_request(int cplength, int datasize);
extern void tape_free_request(struct tape_request *);
extern int tape_do_io(struct tape_device *, struct tape_request *);
extern int tape_do_io_async(struct tape_device *, struct tape_request *);
extern int tape_do_io_interruptible(struct tape_device *, struct tape_request *);
extern int tape_cancel_io(struct tape_device *, struct tape_request *);
void tape_hotplug_event(struct tape_device *, int major, int action);

static inline int
tape_do_io_free(struct tape_device *device, struct tape_request *request)
{
	int rc;

	rc = tape_do_io(device, request);
	tape_free_request(request);
	return rc;
}

extern int tape_oper_handler(int irq, int status);
extern void tape_noper_handler(int irq, int status);
extern int tape_open(struct tape_device *);
extern int tape_release(struct tape_device *);
extern int tape_mtop(struct tape_device *, int, int);
extern void tape_state_set(struct tape_device *, enum tape_state);

extern int tape_generic_online(struct tape_device *, struct tape_discipline *);
extern int tape_generic_offline(struct ccw_device *);
extern int tape_generic_pm_suspend(struct ccw_device *);

/* Externals from tape_devmap.c */
extern int tape_generic_probe(struct ccw_device *);
extern void tape_generic_remove(struct ccw_device *);

extern struct tape_device *tape_get_device(int devindex);
extern struct tape_device *tape_get_device_reference(struct tape_device *);
extern struct tape_device *tape_put_device(struct tape_device *);

/* Externals from tape_char.c */
extern int tapechar_init(void);
extern void tapechar_exit(void);
extern int  tapechar_setup_device(struct tape_device *);
extern void tapechar_cleanup_device(struct tape_device *);

/* Externals from tape_block.c */
#ifdef CONFIG_S390_TAPE_BLOCK
extern int tapeblock_init (void);
extern void tapeblock_exit(void);
extern int tapeblock_setup_device(struct tape_device *);
extern void tapeblock_cleanup_device(struct tape_device *);
#else
static inline int tapeblock_init (void) {return 0;}
static inline void tapeblock_exit (void) {;}
static inline int tapeblock_setup_device(struct tape_device *t) {return 0;}
static inline void tapeblock_cleanup_device (struct tape_device *t) {;}
#endif

/* tape initialisation functions */
#ifdef CONFIG_PROC_FS
extern void tape_proc_init (void);
extern void tape_proc_cleanup (void);
#else
static inline void tape_proc_init (void) {;}
static inline void tape_proc_cleanup (void) {;}
#endif

/* a function for dumping device sense info */
extern void tape_dump_sense_dbf(struct tape_device *, struct tape_request *,
				struct irb *);

/* functions for handling the status of a device */
extern void tape_med_state_set(struct tape_device *, enum tape_medium_state);

/* The debug area */
extern debug_info_t *TAPE_DBF_AREA;

/* functions for building ccws */
static inline struct ccw1 *
tape_ccw_cc(struct ccw1 *ccw, __u8 cmd_code, __u16 memsize, void *cda)
{
	ccw->cmd_code = cmd_code;
	ccw->flags = CCW_FLAG_CC;
	ccw->count = memsize;
	ccw->cda = (__u32)(addr_t) cda;
	return ccw + 1;
}

static inline struct ccw1 *
tape_ccw_end(struct ccw1 *ccw, __u8 cmd_code, __u16 memsize, void *cda)
{
	ccw->cmd_code = cmd_code;
	ccw->flags = 0;
	ccw->count = memsize;
	ccw->cda = (__u32)(addr_t) cda;
	return ccw + 1;
}

static inline struct ccw1 *
tape_ccw_cmd(struct ccw1 *ccw, __u8 cmd_code)
{
	ccw->cmd_code = cmd_code;
	ccw->flags = 0;
	ccw->count = 0;
	ccw->cda = (__u32)(addr_t) &ccw->cmd_code;
	return ccw + 1;
}

static inline struct ccw1 *
tape_ccw_repeat(struct ccw1 *ccw, __u8 cmd_code, int count)
{
	while (count-- > 0) {
		ccw->cmd_code = cmd_code;
		ccw->flags = CCW_FLAG_CC;
		ccw->count = 0;
		ccw->cda = (__u32)(addr_t) &ccw->cmd_code;
		ccw++;
	}
	return ccw;
}

static inline struct ccw1 *
tape_ccw_cc_idal(struct ccw1 *ccw, __u8 cmd_code, struct idal_buffer *idal)
{
	ccw->cmd_code = cmd_code;
	ccw->flags    = CCW_FLAG_CC;
	idal_buffer_set_cda(idal, ccw);
	return ccw++;
}

static inline struct ccw1 *
tape_ccw_end_idal(struct ccw1 *ccw, __u8 cmd_code, struct idal_buffer *idal)
{
	ccw->cmd_code = cmd_code;
	ccw->flags    = 0;
	idal_buffer_set_cda(idal, ccw);
	return ccw++;
}

/* Global vars */
extern const char *tape_state_verbose[];
extern const char *tape_op_verbose[];

#endif /* for ifdef tape.h */
