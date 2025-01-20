/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _ST_H
#define _ST_H

#include <linux/completion.h>
#include <linux/mutex.h>
#include <linux/kref.h>
#include <scsi/scsi_cmnd.h>

/* Descriptor for analyzed sense data */
struct st_cmdstatus {
	int midlevel_result;
	struct scsi_sense_hdr sense_hdr;
	int have_sense;
	int residual;
	u64 uremainder64;
	u8 flags;
	u8 remainder_valid;
	u8 fixed_format;
	u8 deferred;
};

struct scsi_tape;

/* scsi tape command */
struct st_request {
	unsigned char cmd[MAX_COMMAND_SIZE];
	unsigned char sense[SCSI_SENSE_BUFFERSIZE];
	int result;
	struct scsi_tape *stp;
	struct completion *waiting;
	struct bio *bio;
};

/* The tape buffer descriptor. */
struct st_buffer {
	unsigned char cleared;  /* internal buffer cleared after open? */
	unsigned short do_dio;  /* direct i/o set up? */
	int buffer_size;
	int buffer_blocks;
	int buffer_bytes;
	int read_pointer;
	int writing;
	int syscall_result;
	struct st_request *last_SRpnt;
	struct st_cmdstatus cmdstat;
	struct page **reserved_pages;
	int reserved_page_order;
	struct page **mapped_pages;
	struct rq_map_data map_data;
	unsigned char *b_data;
	unsigned short use_sg;	/* zero or max number of s/g segments for this adapter */
	unsigned short sg_segs;		/* number of segments in s/g list */
	unsigned short frp_segs;	/* number of buffer segments */
};

/* The tape mode definition */
struct st_modedef {
	unsigned char defined;
	unsigned char sysv;	/* SYS V semantics? */
	unsigned char do_async_writes;
	unsigned char do_buffer_writes;
	unsigned char do_read_ahead;
	unsigned char defaults_for_writes;
	unsigned char default_compression;	/* 0 = don't touch, etc */
	short default_density;	/* Forced density, -1 = no value */
	int default_blksize;	/* Forced blocksize, -1 = no value */
	struct scsi_tape *tape;
	struct device *devs[2];  /* Auto-rewind and non-rewind devices */
	struct cdev *cdevs[2];  /* Auto-rewind and non-rewind devices */
};

/* Number of modes can be changed by changing ST_NBR_MODE_BITS. The maximum
   number of modes is 16 (ST_NBR_MODE_BITS 4) */
#define ST_NBR_MODE_BITS 2
#define ST_NBR_MODES (1 << ST_NBR_MODE_BITS)
#define ST_MODE_SHIFT (7 - ST_NBR_MODE_BITS)
#define ST_MODE_MASK ((ST_NBR_MODES - 1) << ST_MODE_SHIFT)

#define ST_MAX_TAPES (1 << (20 - (ST_NBR_MODE_BITS + 1)))
#define ST_MAX_TAPE_ENTRIES  (ST_MAX_TAPES << (ST_NBR_MODE_BITS + 1))

/* The status related to each partition */
struct st_partstat {
	unsigned char rw;
	unsigned char eof;
	unsigned char at_sm;
	unsigned char last_block_valid;
	u32 last_block_visited;
	int drv_block;		/* The block where the drive head is */
	int drv_file;
};

/* Tape statistics */
struct scsi_tape_stats {
	atomic64_t read_byte_cnt;  /* bytes read */
	atomic64_t write_byte_cnt; /* bytes written */
	atomic64_t in_flight;      /* Number of I/Os in flight */
	atomic64_t read_cnt;       /* Count of read requests */
	atomic64_t write_cnt;      /* Count of write requests */
	atomic64_t other_cnt;      /* Count of other requests either
				    * implicit or from user space
				    * ioctl. */
	atomic64_t resid_cnt;      /* Count of resid_len > 0 */
	atomic64_t tot_read_time;  /* ktime spent completing reads */
	atomic64_t tot_write_time; /* ktime spent completing writes */
	atomic64_t tot_io_time;    /* ktime spent doing any I/O */
	ktime_t read_time;         /* holds ktime request was queued */
	ktime_t write_time;        /* holds ktime request was queued */
	ktime_t other_time;        /* holds ktime request was queued */
	atomic_t last_read_size;   /* Number of bytes issued for last read */
	atomic_t last_write_size;  /* Number of bytes issued for last write */
};

#define ST_NBR_PARTITIONS 4

/* The tape drive descriptor */
struct scsi_tape {
	struct scsi_device *device;
	struct mutex lock;	/* For serialization */
	struct completion wait;	/* For SCSI commands */
	struct st_buffer *buffer;
	int index;

	/* Drive characteristics */
	unsigned char omit_blklims;
	unsigned char do_auto_lock;
	unsigned char can_bsr;
	unsigned char can_partitions;
	unsigned char two_fm;
	unsigned char fast_mteom;
	unsigned char immediate;
	unsigned char scsi2_logical;
	unsigned char default_drvbuffer;	/* 0xff = don't touch, value 3 bits */
	unsigned char cln_mode;			/* 0 = none, otherwise sense byte nbr */
	unsigned char cln_sense_value;
	unsigned char cln_sense_mask;
	unsigned char use_pf;			/* Set Page Format bit in all mode selects? */
	unsigned char try_dio;			/* try direct i/o in general? */
	unsigned char try_dio_now;		/* try direct i/o before next close? */
	unsigned char c_algo;			/* compression algorithm */
	unsigned char pos_unknown;			/* after reset position unknown */
	unsigned char sili;			/* use SILI when reading in variable b mode */
	unsigned char immediate_filemark;	/* write filemark immediately */
	int tape_type;
	int long_timeout;	/* timeout for commands known to take long time */

	/* Mode characteristics */
	struct st_modedef modes[ST_NBR_MODES];
	int current_mode;

	/* Status variables */
	int partition;
	int new_partition;
	int nbr_partitions;	/* zero until partition support enabled */
	struct st_partstat ps[ST_NBR_PARTITIONS];
	unsigned char dirty;
	unsigned char ready;
	unsigned char write_prot;
	unsigned char drv_write_prot;
	unsigned char in_use;
	unsigned char blksize_changed;
	unsigned char density_changed;
	unsigned char compression_changed;
	unsigned char drv_buffer;
	unsigned char density;
	unsigned char changed_density;
	unsigned char door_locked;
	unsigned char autorew_dev;   /* auto-rewind device */
	unsigned char rew_at_close;  /* rewind necessary at close */
	unsigned char inited;
	unsigned char cleaning_req;  /* cleaning requested? */
	unsigned char first_tur;     /* first TEST UNIT READY */
	int block_size;
	int changed_blksize;
	int min_block;
	int max_block;
	int recover_count;     /* From tape opening */
	int recover_reg;       /* From last status call */

#if DEBUG
	unsigned char write_pending;
	int nbr_finished;
	int nbr_waits;
	int nbr_requests;
	int nbr_dio;
	int nbr_pages;
	unsigned char last_cmnd[6];
	unsigned char last_sense[16];
#endif
	char name[DISK_NAME_LEN];
	struct kref     kref;
	struct scsi_tape_stats *stats;
};

/* Bit masks for use_pf */
#define USE_PF      1
#define PF_TESTED   2

/* Values of eof */
#define	ST_NOEOF	0
#define ST_FM_HIT       1
#define ST_FM           2
#define ST_EOM_OK       3
#define ST_EOM_ERROR	4
#define	ST_EOD_1        5
#define ST_EOD_2        6
#define ST_EOD		7
/* EOD hit while reading => ST_EOD_1 => return zero => ST_EOD_2 =>
   return zero => ST_EOD, return ENOSPC */
/* When writing: ST_EOM_OK == early warning found, write OK
		 ST_EOD_1  == allow trying new write after early warning
		 ST_EOM_ERROR == early warning found, not able to write all */

/* Values of rw */
#define	ST_IDLE		0
#define	ST_READING	1
#define	ST_WRITING	2

/* Values of ready state */
#define ST_READY	0
#define ST_NOT_READY	1
#define ST_NO_TAPE	2

/* Values for door lock state */
#define ST_UNLOCKED	0
#define ST_LOCKED_EXPLICIT 1
#define ST_LOCKED_AUTO  2
#define ST_LOCK_FAILS   3

/* Positioning SCSI-commands for Tandberg, etc. drives */
#define	QFA_REQUEST_BLOCK	0x02
#define	QFA_SEEK_BLOCK		0x0c

/* Setting the binary options */
#define ST_DONT_TOUCH  0
#define ST_NO          1
#define ST_YES         2

#define EXTENDED_SENSE_START  18

/* Masks for some conditions in the sense data */
#define SENSE_FMK   0x80
#define SENSE_EOM   0x40
#define SENSE_ILI   0x20

#endif
