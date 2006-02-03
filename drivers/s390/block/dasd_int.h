/* 
 * File...........: linux/drivers/s390/block/dasd_int.h
 * Author(s)......: Holger Smolinski <Holger.Smolinski@de.ibm.com>
 *                  Horst Hummel <Horst.Hummel@de.ibm.com> 
 *		    Martin Schwidefsky <schwidefsky@de.ibm.com>
 * Bugreports.to..: <Linux390@de.ibm.com>
 * (C) IBM Corporation, IBM Deutschland Entwicklung GmbH, 1999,2000
 *
 */

#ifndef DASD_INT_H
#define DASD_INT_H

#ifdef __KERNEL__

/* erp debugging in dasd.c and dasd_3990_erp.c */
#define ERP_DEBUG


/* we keep old device allocation scheme; IOW, minors are still in 0..255 */
#define DASD_PER_MAJOR (1U << (MINORBITS - DASD_PARTN_BITS))
#define DASD_PARTN_MASK ((1 << DASD_PARTN_BITS) - 1)

/*
 * States a dasd device can have:
 *   new: the dasd_device structure is allocated.
 *   known: the discipline for the device is identified.
 *   basic: the device can do basic i/o.
 *   accept: the device is analysed (format is known).
 *   ready: partition detection is done and the device is can do block io.
 *   online: the device accepts requests from the block device queue.
 *
 * Things to do for startup state transitions:
 *   new -> known: find discipline for the device and create devfs entries.
 *   known -> basic: request irq line for the device.
 *   basic -> ready: do the initial analysis, e.g. format detection,
 *                   do block device setup and detect partitions.
 *   ready -> online: schedule the device tasklet.
 * Things to do for shutdown state transitions:
 *   online -> ready: just set the new device state.
 *   ready -> basic: flush requests from the block device layer, clear
 *                   partition information and reset format information.
 *   basic -> known: terminate all requests and free irq.
 *   known -> new: remove devfs entries and forget discipline.
 */

#define DASD_STATE_NEW	  0
#define DASD_STATE_KNOWN  1
#define DASD_STATE_BASIC  2
#define DASD_STATE_READY  3
#define DASD_STATE_ONLINE 4

#include <linux/module.h>
#include <linux/wait.h>
#include <linux/blkdev.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/genhd.h>
#include <linux/hdreg.h>
#include <linux/interrupt.h>
#include <asm/ccwdev.h>
#include <linux/workqueue.h>
#include <asm/debug.h>
#include <asm/dasd.h>
#include <asm/idals.h>

/*
 * SECTION: Type definitions
 */
struct dasd_device;

typedef int (*dasd_ioctl_fn_t) (struct block_device *bdev, int no, long args);

struct dasd_ioctl {
	struct list_head list;
	struct module *owner;
	int no;
	dasd_ioctl_fn_t handler;
};

typedef enum {
	dasd_era_fatal = -1,	/* no chance to recover		     */
	dasd_era_none = 0,	/* don't recover, everything alright */
	dasd_era_msg = 1,	/* don't recover, just report...     */
	dasd_era_recover = 2	/* recovery action recommended	     */
} dasd_era_t;

/* BIT DEFINITIONS FOR SENSE DATA */
#define DASD_SENSE_BIT_0 0x80
#define DASD_SENSE_BIT_1 0x40
#define DASD_SENSE_BIT_2 0x20
#define DASD_SENSE_BIT_3 0x10

/*
 * SECTION: MACROs for klogd and s390 debug feature (dbf)
 */
#define DBF_DEV_EVENT(d_level, d_device, d_str, d_data...) \
do { \
	debug_sprintf_event(d_device->debug_area, \
			    d_level, \
			    d_str "\n", \
			    d_data); \
} while(0)

#define DBF_DEV_EXC(d_level, d_device, d_str, d_data...) \
do { \
	debug_sprintf_exception(d_device->debug_area, \
				d_level, \
				d_str "\n", \
				d_data); \
} while(0)

#define DBF_EVENT(d_level, d_str, d_data...)\
do { \
	debug_sprintf_event(dasd_debug_area, \
			    d_level,\
			    d_str "\n", \
			    d_data); \
} while(0)

#define DBF_EXC(d_level, d_str, d_data...)\
do { \
	debug_sprintf_exception(dasd_debug_area, \
				d_level,\
				d_str "\n", \
				d_data); \
} while(0)

/* definition of dbf debug levels */
#define	DBF_EMERG	0	/* system is unusable			*/
#define	DBF_ALERT	1	/* action must be taken immediately	*/
#define	DBF_CRIT	2	/* critical conditions			*/
#define	DBF_ERR		3	/* error conditions			*/
#define	DBF_WARNING	4	/* warning conditions			*/
#define	DBF_NOTICE	5	/* normal but significant condition	*/
#define	DBF_INFO	6	/* informational			*/
#define	DBF_DEBUG	6	/* debug-level messages			*/

/* messages to be written via klogd and dbf */
#define DEV_MESSAGE(d_loglevel,d_device,d_string,d_args...)\
do { \
	printk(d_loglevel PRINTK_HEADER " %s: " d_string "\n", \
	       d_device->cdev->dev.bus_id, d_args); \
	DBF_DEV_EVENT(DBF_ALERT, d_device, d_string, d_args); \
} while(0)

#define MESSAGE(d_loglevel,d_string,d_args...)\
do { \
	printk(d_loglevel PRINTK_HEADER " " d_string "\n", d_args); \
	DBF_EVENT(DBF_ALERT, d_string, d_args); \
} while(0)

/* messages to be written via klogd only */
#define DEV_MESSAGE_LOG(d_loglevel,d_device,d_string,d_args...)\
do { \
	printk(d_loglevel PRINTK_HEADER " %s: " d_string "\n", \
	       d_device->cdev->dev.bus_id, d_args); \
} while(0)

#define MESSAGE_LOG(d_loglevel,d_string,d_args...)\
do { \
	printk(d_loglevel PRINTK_HEADER " " d_string "\n", d_args); \
} while(0)

struct dasd_ccw_req {
	unsigned int magic;		/* Eye catcher */
        struct list_head list;		/* list_head for request queueing. */

	/* Where to execute what... */
	struct dasd_device *device;	/* device the request is for */
	struct ccw1 *cpaddr;		/* address of channel program */
	char status;	        	/* status of this request */
	short retries;			/* A retry counter */
	unsigned long flags;        	/* flags of this request */

	/* ... and how */
	unsigned long starttime;	/* jiffies time of request start */
	int expires;			/* expiration period in jiffies */
	char lpm;               	/* logical path mask */
	void *data;			/* pointer to data area */

	/* these are important for recovering erroneous requests          */
	struct irb irb;			/* device status in case of an error */
	struct dasd_ccw_req *refers;	/* ERP-chain queueing. */
	void *function; 		/* originating ERP action */

	/* these are for statistics only */
	unsigned long long buildclk;	/* TOD-clock of request generation */
	unsigned long long startclk;	/* TOD-clock of request start */
	unsigned long long stopclk;	/* TOD-clock of request interrupt */
	unsigned long long endclk;	/* TOD-clock of request termination */

        /* Callback that is called after reaching final status. */
        void (*callback)(struct dasd_ccw_req *, void *data);
        void *callback_data;
};

/* 
 * dasd_ccw_req -> status can be:
 */
#define DASD_CQR_FILLED   0x00	/* request is ready to be processed */
#define DASD_CQR_QUEUED   0x01	/* request is queued to be processed */
#define DASD_CQR_IN_IO    0x02	/* request is currently in IO */
#define DASD_CQR_DONE     0x03	/* request is completed successfully */
#define DASD_CQR_ERROR    0x04	/* request is completed with error */
#define DASD_CQR_FAILED   0x05	/* request is finally failed */
#define DASD_CQR_CLEAR    0x06	/* request is clear pending */

/* per dasd_ccw_req flags */
#define DASD_CQR_FLAGS_USE_ERP   0	/* use ERP for this request */
#define DASD_CQR_FLAGS_FAILFAST  1	/* FAILFAST */

/* Signature for error recovery functions. */
typedef struct dasd_ccw_req *(*dasd_erp_fn_t) (struct dasd_ccw_req *);

/*
 * the struct dasd_discipline is
 * sth like a table of virtual functions, if you think of dasd_eckd
 * inheriting dasd...
 * no, currently we are not planning to reimplement the driver in C++
 */
struct dasd_discipline {
	struct module *owner;
	char ebcname[8];	/* a name used for tagging and printks */
	char name[8];		/* a name used for tagging and printks */
	int max_blocks;		/* maximum number of blocks to be chained */

	struct list_head list;	/* used for list of disciplines */

        /*
         * Device recognition functions. check_device is used to verify
         * the sense data and the information returned by read device
         * characteristics. It returns 0 if the discipline can be used
         * for the device in question.
         * do_analysis is used in the step from device state "basic" to
         * state "accept". It returns 0 if the device can be made ready,
         * it returns -EMEDIUMTYPE if the device can't be made ready or
         * -EAGAIN if do_analysis started a ccw that needs to complete
         * before the analysis may be repeated.
         */
        int (*check_device)(struct dasd_device *);
	int (*do_analysis) (struct dasd_device *);

        /*
         * Device operation functions. build_cp creates a ccw chain for
         * a block device request, start_io starts the request and
         * term_IO cancels it (e.g. in case of a timeout). format_device
         * returns a ccw chain to be used to format the device.
         */
	struct dasd_ccw_req *(*build_cp) (struct dasd_device *,
					  struct request *);
	int (*start_IO) (struct dasd_ccw_req *);
	int (*term_IO) (struct dasd_ccw_req *);
	struct dasd_ccw_req *(*format_device) (struct dasd_device *,
					       struct format_data_t *);
	int (*free_cp) (struct dasd_ccw_req *, struct request *);
        /*
         * Error recovery functions. examine_error() returns a value that
         * indicates what to do for an error condition. If examine_error()
         * returns 'dasd_era_recover' erp_action() is called to create a 
         * special error recovery ccw. erp_postaction() is called after
         * an error recovery ccw has finished its execution. dump_sense
         * is called for every error condition to print the sense data
         * to the console.
         */
	dasd_era_t(*examine_error) (struct dasd_ccw_req *, struct irb *);
	dasd_erp_fn_t(*erp_action) (struct dasd_ccw_req *);
	dasd_erp_fn_t(*erp_postaction) (struct dasd_ccw_req *);
	void (*dump_sense) (struct dasd_device *, struct dasd_ccw_req *,
			    struct irb *);

        /* i/o control functions. */
	int (*fill_geometry) (struct dasd_device *, struct hd_geometry *);
	int (*fill_info) (struct dasd_device *, struct dasd_information2_t *);
};

extern struct dasd_discipline *dasd_diag_discipline_pointer;


/*
 * Notification numbers for extended error reporting notifications:
 * The DASD_EER_DISABLE notification is sent before a dasd_device (and it's
 * eer pointer) is freed. The error reporting module needs to do all necessary
 * cleanup steps.
 * The DASD_EER_TRIGGER notification sends the actual error reports (triggers).
 */
#define DASD_EER_DISABLE 0
#define DASD_EER_TRIGGER 1

/* Trigger IDs for extended error reporting DASD_EER_TRIGGER notification */
#define DASD_EER_FATALERROR  1
#define DASD_EER_NOPATH      2
#define DASD_EER_STATECHANGE 3
#define DASD_EER_PPRCSUSPEND 4

/*
 * The dasd_eer_trigger structure contains all data that we need to send
 * along with an DASD_EER_TRIGGER notification.
 */
struct dasd_eer_trigger {
	unsigned int id;
	struct dasd_device *device;
	struct dasd_ccw_req *cqr;
};


struct dasd_device {
	/* Block device stuff. */
	struct gendisk *gdp;
	request_queue_t *request_queue;
	spinlock_t request_queue_lock;
	struct block_device *bdev;
        unsigned int devindex;
	unsigned long blocks;		/* size of volume in blocks */
	unsigned int bp_block;		/* bytes per block */
	unsigned int s2b_shift;		/* log2 (bp_block/512) */
	unsigned long flags;		/* per device flags */
	unsigned short features;        /* copy of devmap-features (read-only!) */

	/* extended error reporting stuff (eer) */
	void *eer;

	/* Device discipline stuff. */
	struct dasd_discipline *discipline;
	char *private;

	/* Device state and target state. */
	int state, target;
	int stopped;		/* device (ccw_device_start) was stopped */

	/* Open and reference count. */
        atomic_t ref_count;
	atomic_t open_count;

	/* ccw queue and memory for static ccw/erp buffers. */
	struct list_head ccw_queue;
	spinlock_t mem_lock;
	void *ccw_mem;
	void *erp_mem;
	struct list_head ccw_chunks;
	struct list_head erp_chunks;

	atomic_t tasklet_scheduled;
        struct tasklet_struct tasklet;
	struct work_struct kick_work;
	struct timer_list timer;

	debug_info_t *debug_area;

	struct ccw_device *cdev;

#ifdef CONFIG_DASD_PROFILE
	struct dasd_profile_info_t profile;
#endif
};

/* reasons why device (ccw_device_start) was stopped */
#define DASD_STOPPED_NOT_ACC 1         /* not accessible */
#define DASD_STOPPED_QUIESCE 2         /* Quiesced */
#define DASD_STOPPED_PENDING 4         /* long busy */
#define DASD_STOPPED_DC_WAIT 8         /* disconnected, wait */
#define DASD_STOPPED_DC_EIO  16        /* disconnected, return -EIO */

/* per device flags */
#define DASD_FLAG_DSC_ERROR	2	/* return -EIO when disconnected */
#define DASD_FLAG_OFFLINE	3	/* device is in offline processing */

void dasd_put_device_wake(struct dasd_device *);

/*
 * Reference count inliners
 */
static inline void
dasd_get_device(struct dasd_device *device)
{
	atomic_inc(&device->ref_count);
}

static inline void
dasd_put_device(struct dasd_device *device)
{
	if (atomic_dec_return(&device->ref_count) == 0)
		dasd_put_device_wake(device);
}

/*
 * The static memory in ccw_mem and erp_mem is managed by a sorted
 * list of free memory chunks.
 */
struct dasd_mchunk
{
	struct list_head list;
	unsigned long size;
} __attribute__ ((aligned(8)));

static inline void
dasd_init_chunklist(struct list_head *chunk_list, void *mem,
		    unsigned long size)
{
	struct dasd_mchunk *chunk;

	INIT_LIST_HEAD(chunk_list);
	chunk = (struct dasd_mchunk *) mem;
	chunk->size = size - sizeof(struct dasd_mchunk);
	list_add(&chunk->list, chunk_list);
}

static inline void *
dasd_alloc_chunk(struct list_head *chunk_list, unsigned long size)
{
	struct dasd_mchunk *chunk, *tmp;

	size = (size + 7L) & -8L;
	list_for_each_entry(chunk, chunk_list, list) {
		if (chunk->size < size)
			continue;
		if (chunk->size > size + sizeof(struct dasd_mchunk)) {
			char *endaddr = (char *) (chunk + 1) + chunk->size;
			tmp = (struct dasd_mchunk *) (endaddr - size) - 1;
			tmp->size = size;
			chunk->size -= size + sizeof(struct dasd_mchunk);
			chunk = tmp;
		} else
			list_del(&chunk->list);
		return (void *) (chunk + 1);
	}
	return NULL;
}

static inline void
dasd_free_chunk(struct list_head *chunk_list, void *mem)
{
	struct dasd_mchunk *chunk, *tmp;
	struct list_head *p, *left;

	chunk = (struct dasd_mchunk *)
		((char *) mem - sizeof(struct dasd_mchunk));
	/* Find out the left neighbour in chunk_list. */
	left = chunk_list;
	list_for_each(p, chunk_list) {
		if (list_entry(p, struct dasd_mchunk, list) > chunk)
			break;
		left = p;
	}
	/* Try to merge with right neighbour = next element from left. */
	if (left->next != chunk_list) {
		tmp = list_entry(left->next, struct dasd_mchunk, list);
		if ((char *) (chunk + 1) + chunk->size == (char *) tmp) {
			list_del(&tmp->list);
			chunk->size += tmp->size + sizeof(struct dasd_mchunk);
		}
	}
	/* Try to merge with left neighbour. */
	if (left != chunk_list) {
		tmp = list_entry(left, struct dasd_mchunk, list);
		if ((char *) (tmp + 1) + tmp->size == (char *) chunk) {
			tmp->size += chunk->size + sizeof(struct dasd_mchunk);
			return;
		}
	}
	__list_add(&chunk->list, left, left->next);
}

/*
 * Check if bsize is in { 512, 1024, 2048, 4096 }
 */
static inline int
dasd_check_blocksize(int bsize)
{
	if (bsize < 512 || bsize > 4096 || (bsize & (bsize - 1)) != 0)
		return -EMEDIUMTYPE;
	return 0;
}

/* externals in dasd.c */
#define DASD_PROFILE_ON	 1
#define DASD_PROFILE_OFF 0

extern debug_info_t *dasd_debug_area;
extern struct dasd_profile_info_t dasd_global_profile;
extern unsigned int dasd_profile_level;
extern struct block_device_operations dasd_device_operations;

extern kmem_cache_t *dasd_page_cache;

struct dasd_ccw_req *
dasd_kmalloc_request(char *, int, int, struct dasd_device *);
struct dasd_ccw_req *
dasd_smalloc_request(char *, int, int, struct dasd_device *);
void dasd_kfree_request(struct dasd_ccw_req *, struct dasd_device *);
void dasd_sfree_request(struct dasd_ccw_req *, struct dasd_device *);

static inline int
dasd_kmalloc_set_cda(struct ccw1 *ccw, void *cda, struct dasd_device *device)
{
	return set_normalized_cda(ccw, cda);
}

struct dasd_device *dasd_alloc_device(void);
void dasd_free_device(struct dasd_device *);

void dasd_enable_device(struct dasd_device *);
void dasd_set_target_state(struct dasd_device *, int);
void dasd_kick_device(struct dasd_device *);

void dasd_add_request_head(struct dasd_ccw_req *);
void dasd_add_request_tail(struct dasd_ccw_req *);
int  dasd_start_IO(struct dasd_ccw_req *);
int  dasd_term_IO(struct dasd_ccw_req *);
void dasd_schedule_bh(struct dasd_device *);
int  dasd_sleep_on(struct dasd_ccw_req *);
int  dasd_sleep_on_immediatly(struct dasd_ccw_req *);
int  dasd_sleep_on_interruptible(struct dasd_ccw_req *);
void dasd_set_timer(struct dasd_device *, int);
void dasd_clear_timer(struct dasd_device *);
int  dasd_cancel_req(struct dasd_ccw_req *);
int dasd_generic_probe (struct ccw_device *, struct dasd_discipline *);
void dasd_generic_remove (struct ccw_device *cdev);
int dasd_generic_set_online(struct ccw_device *, struct dasd_discipline *);
int dasd_generic_set_offline (struct ccw_device *cdev);
int dasd_generic_notify(struct ccw_device *, int);
void dasd_generic_auto_online (struct ccw_driver *);
int dasd_register_eer_notifier(struct notifier_block *);
int dasd_unregister_eer_notifier(struct notifier_block *);
void dasd_write_eer_trigger(unsigned int , struct dasd_device *,
			struct dasd_ccw_req *);



/* externals in dasd_devmap.c */
extern int dasd_max_devindex;
extern int dasd_probeonly;
extern int dasd_autodetect;

int dasd_devmap_init(void);
void dasd_devmap_exit(void);

struct dasd_device *dasd_create_device(struct ccw_device *);
void dasd_delete_device(struct dasd_device *);

int dasd_get_feature(struct ccw_device *, int);
int dasd_set_feature(struct ccw_device *, int, int);

int dasd_add_sysfs_files(struct ccw_device *);
void dasd_remove_sysfs_files(struct ccw_device *);

struct dasd_device *dasd_device_from_cdev(struct ccw_device *);
struct dasd_device *dasd_device_from_devindex(int);

int dasd_parse(void);
int dasd_busid_known(char *);

/* externals in dasd_gendisk.c */
int  dasd_gendisk_init(void);
void dasd_gendisk_exit(void);
int dasd_gendisk_alloc(struct dasd_device *);
void dasd_gendisk_free(struct dasd_device *);
int dasd_scan_partitions(struct dasd_device *);
void dasd_destroy_partitions(struct dasd_device *);

/* externals in dasd_ioctl.c */
int  dasd_ioctl_init(void);
void dasd_ioctl_exit(void);
int  dasd_ioctl_no_register(struct module *, int, dasd_ioctl_fn_t);
int  dasd_ioctl_no_unregister(struct module *, int, dasd_ioctl_fn_t);
int  dasd_ioctl(struct inode *, struct file *, unsigned int, unsigned long);
long dasd_compat_ioctl(struct file *, unsigned int, unsigned long);

/* externals in dasd_proc.c */
int dasd_proc_init(void);
void dasd_proc_exit(void);

/* externals in dasd_erp.c */
struct dasd_ccw_req *dasd_default_erp_action(struct dasd_ccw_req *);
struct dasd_ccw_req *dasd_default_erp_postaction(struct dasd_ccw_req *);
struct dasd_ccw_req *dasd_alloc_erp_request(char *, int, int,
					    struct dasd_device *);
void dasd_free_erp_request(struct dasd_ccw_req *, struct dasd_device *);
void dasd_log_sense(struct dasd_ccw_req *, struct irb *);
void dasd_log_ccw(struct dasd_ccw_req *, int, __u32);

/* externals in dasd_3370_erp.c */
dasd_era_t dasd_3370_erp_examine(struct dasd_ccw_req *, struct irb *);

/* externals in dasd_3990_erp.c */
dasd_era_t dasd_3990_erp_examine(struct dasd_ccw_req *, struct irb *);
struct dasd_ccw_req *dasd_3990_erp_action(struct dasd_ccw_req *);

/* externals in dasd_9336_erp.c */
dasd_era_t dasd_9336_erp_examine(struct dasd_ccw_req *, struct irb *);

/* externals in dasd_9336_erp.c */
dasd_era_t dasd_9343_erp_examine(struct dasd_ccw_req *, struct irb *);
struct dasd_ccw_req *dasd_9343_erp_action(struct dasd_ccw_req *);

#endif				/* __KERNEL__ */

#endif				/* DASD_H */

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-indent-level: 4 
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -4
 * c-argdecl-indent: 4
 * c-label-offset: -4
 * c-continued-statement-offset: 4
 * c-continued-brace-offset: 0
 * indent-tabs-mode: 1
 * tab-width: 8
 * End:
 */
