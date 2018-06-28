/*
 * Copyright (C) 2015 IT University of Copenhagen (rrpc.h)
 * Copyright (C) 2016 CNEX Labs
 * Initial release: Matias Bjorling <matias@cnexlabs.com>
 * Write buffering: Javier Gonzalez <javier@cnexlabs.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * Implementation of a Physical Block-device target for Open-channel SSDs.
 *
 */

#ifndef PBLK_H_
#define PBLK_H_

#include <linux/blkdev.h>
#include <linux/blk-mq.h>
#include <linux/bio.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/vmalloc.h>
#include <linux/crc32.h>
#include <linux/uuid.h>

#include <linux/lightnvm.h>

/* Run only GC if less than 1/X blocks are free */
#define GC_LIMIT_INVERSE 5
#define GC_TIME_MSECS 1000

#define PBLK_SECTOR (512)
#define PBLK_EXPOSED_PAGE_SIZE (4096)
#define PBLK_MAX_REQ_ADDRS (64)
#define PBLK_MAX_REQ_ADDRS_PW (6)

#define PBLK_NR_CLOSE_JOBS (4)

#define PBLK_CACHE_NAME_LEN (DISK_NAME_LEN + 16)

#define PBLK_COMMAND_TIMEOUT_MS 30000

/* Max 512 LUNs per device */
#define PBLK_MAX_LUNS_BITMAP (4)

#define NR_PHY_IN_LOG (PBLK_EXPOSED_PAGE_SIZE / PBLK_SECTOR)

/* Static pool sizes */
#define PBLK_GEN_WS_POOL_SIZE (2)

#define PBLK_DEFAULT_OP (11)

enum {
	PBLK_READ		= READ,
	PBLK_WRITE		= WRITE,/* Write from write buffer */
	PBLK_WRITE_INT,			/* Internal write - no write buffer */
	PBLK_READ_RECOV,		/* Recovery read - errors allowed */
	PBLK_ERASE,
};

enum {
	/* IO Types */
	PBLK_IOTYPE_USER	= 1 << 0,
	PBLK_IOTYPE_GC		= 1 << 1,

	/* Write buffer flags */
	PBLK_FLUSH_ENTRY	= 1 << 2,
	PBLK_WRITTEN_DATA	= 1 << 3,
	PBLK_SUBMITTED_ENTRY	= 1 << 4,
	PBLK_WRITABLE_ENTRY	= 1 << 5,
};

enum {
	PBLK_BLK_ST_OPEN =	0x1,
	PBLK_BLK_ST_CLOSED =	0x2,
};

struct pblk_sec_meta {
	u64 reserved;
	__le64 lba;
};

/* The number of GC lists and the rate-limiter states go together. This way the
 * rate-limiter can dictate how much GC is needed based on resource utilization.
 */
#define PBLK_GC_NR_LISTS 4

enum {
	PBLK_RL_OFF = 0,
	PBLK_RL_WERR = 1,
	PBLK_RL_HIGH = 2,
	PBLK_RL_MID = 3,
	PBLK_RL_LOW = 4
};

#define pblk_dma_meta_size (sizeof(struct pblk_sec_meta) * PBLK_MAX_REQ_ADDRS)
#define pblk_dma_ppa_size (sizeof(u64) * PBLK_MAX_REQ_ADDRS)

/* write buffer completion context */
struct pblk_c_ctx {
	struct list_head list;		/* Head for out-of-order completion */

	unsigned long *lun_bitmap;	/* Luns used on current request */
	unsigned int sentry;
	unsigned int nr_valid;
	unsigned int nr_padded;
};

/* read context */
struct pblk_g_ctx {
	void *private;
	unsigned long start_time;
	u64 lba;
};

/* Pad context */
struct pblk_pad_rq {
	struct pblk *pblk;
	struct completion wait;
	struct kref ref;
};

/* Recovery context */
struct pblk_rec_ctx {
	struct pblk *pblk;
	struct nvm_rq *rqd;
	struct work_struct ws_rec;
};

/* Write context */
struct pblk_w_ctx {
	struct bio_list bios;		/* Original bios - used for completion
					 * in REQ_FUA, REQ_FLUSH case
					 */
	u64 lba;			/* Logic addr. associated with entry */
	struct ppa_addr ppa;		/* Physic addr. associated with entry */
	int flags;			/* Write context flags */
};

struct pblk_rb_entry {
	struct ppa_addr cacheline;	/* Cacheline for this entry */
	void *data;			/* Pointer to data on this entry */
	struct pblk_w_ctx w_ctx;	/* Context for this entry */
	struct list_head index;		/* List head to enable indexes */
};

#define EMPTY_ENTRY (~0U)

struct pblk_rb_pages {
	struct page *pages;
	int order;
	struct list_head list;
};

struct pblk_rb {
	struct pblk_rb_entry *entries;	/* Ring buffer entries */
	unsigned int mem;		/* Write offset - points to next
					 * writable entry in memory
					 */
	unsigned int subm;		/* Read offset - points to last entry
					 * that has been submitted to the media
					 * to be persisted
					 */
	unsigned int sync;		/* Synced - backpointer that signals
					 * the last submitted entry that has
					 * been successfully persisted to media
					 */
	unsigned int flush_point;	/* Sync point - last entry that must be
					 * flushed to the media. Used with
					 * REQ_FLUSH and REQ_FUA
					 */
	unsigned int l2p_update;	/* l2p update point - next entry for
					 * which l2p mapping will be updated to
					 * contain a device ppa address (instead
					 * of a cacheline
					 */
	unsigned int nr_entries;	/* Number of entries in write buffer -
					 * must be a power of two
					 */
	unsigned int seg_size;		/* Size of the data segments being
					 * stored on each entry. Typically this
					 * will be 4KB
					 */

	struct list_head pages;		/* List of data pages */

	spinlock_t w_lock;		/* Write lock */
	spinlock_t s_lock;		/* Sync lock */

#ifdef CONFIG_NVM_DEBUG
	atomic_t inflight_flush_point;	/* Not served REQ_FLUSH | REQ_FUA */
#endif
};

#define PBLK_RECOVERY_SECTORS 16

struct pblk_lun {
	struct ppa_addr bppa;
	struct semaphore wr_sem;
};

struct pblk_gc_rq {
	struct pblk_line *line;
	void *data;
	u64 paddr_list[PBLK_MAX_REQ_ADDRS];
	u64 lba_list[PBLK_MAX_REQ_ADDRS];
	int nr_secs;
	int secs_to_gc;
	struct list_head list;
};

struct pblk_gc {
	/* These states are not protected by a lock since (i) they are in the
	 * fast path, and (ii) they are not critical.
	 */
	int gc_active;
	int gc_enabled;
	int gc_forced;

	struct task_struct *gc_ts;
	struct task_struct *gc_writer_ts;
	struct task_struct *gc_reader_ts;

	struct workqueue_struct *gc_line_reader_wq;
	struct workqueue_struct *gc_reader_wq;

	struct timer_list gc_timer;

	struct semaphore gc_sem;
	atomic_t read_inflight_gc; /* Number of lines with inflight GC reads */
	atomic_t pipeline_gc;	   /* Number of lines in the GC pipeline -
				    * started reads to finished writes
				    */
	int w_entries;

	struct list_head w_list;
	struct list_head r_list;

	spinlock_t lock;
	spinlock_t w_lock;
	spinlock_t r_lock;
};

struct pblk_rl {
	unsigned int high;	/* Upper threshold for rate limiter (free run -
				 * user I/O rate limiter
				 */
	unsigned int high_pw;	/* High rounded up as a power of 2 */

#define PBLK_USER_HIGH_THRS 8	/* Begin write limit at 12% available blks */
#define PBLK_USER_LOW_THRS 10	/* Aggressive GC at 10% available blocks */

	int rb_windows_pw;	/* Number of rate windows in the write buffer
				 * given as a power-of-2. This guarantees that
				 * when user I/O is being rate limited, there
				 * will be reserved enough space for the GC to
				 * place its payload. A window is of
				 * pblk->max_write_pgs size, which in NVMe is
				 * 64, i.e., 256kb.
				 */
	int rb_budget;		/* Total number of entries available for I/O */
	int rb_user_max;	/* Max buffer entries available for user I/O */
	int rb_gc_max;		/* Max buffer entries available for GC I/O */
	int rb_gc_rsv;		/* Reserved buffer entries for GC I/O */
	int rb_state;		/* Rate-limiter current state */
	int rb_max_io;		/* Maximum size for an I/O giving the config */

	atomic_t rb_user_cnt;	/* User I/O buffer counter */
	atomic_t rb_gc_cnt;	/* GC I/O buffer counter */
	atomic_t rb_space;	/* Space limit in case of reaching capacity */

	int rsv_blocks;		/* Reserved blocks for GC */

	int rb_user_active;
	int rb_gc_active;

	atomic_t werr_lines;	/* Number of write error lines that needs gc */

	struct timer_list u_timer;

	unsigned long long nr_secs;
	unsigned long total_blocks;

	atomic_t free_blocks;		/* Total number of free blocks (+ OP) */
	atomic_t free_user_blocks;	/* Number of user free blocks (no OP) */
};

#define PBLK_LINE_EMPTY (~0U)

enum {
	/* Line Types */
	PBLK_LINETYPE_FREE = 0,
	PBLK_LINETYPE_LOG = 1,
	PBLK_LINETYPE_DATA = 2,

	/* Line state */
	PBLK_LINESTATE_NEW = 9,
	PBLK_LINESTATE_FREE = 10,
	PBLK_LINESTATE_OPEN = 11,
	PBLK_LINESTATE_CLOSED = 12,
	PBLK_LINESTATE_GC = 13,
	PBLK_LINESTATE_BAD = 14,
	PBLK_LINESTATE_CORRUPT = 15,

	/* GC group */
	PBLK_LINEGC_NONE = 20,
	PBLK_LINEGC_EMPTY = 21,
	PBLK_LINEGC_LOW = 22,
	PBLK_LINEGC_MID = 23,
	PBLK_LINEGC_HIGH = 24,
	PBLK_LINEGC_FULL = 25,
	PBLK_LINEGC_WERR = 26
};

#define PBLK_MAGIC 0x70626c6b /*pblk*/

/* emeta/smeta persistent storage format versions:
 * Changes in major version requires offline migration.
 * Changes in minor version are handled automatically during
 * recovery.
 */

#define SMETA_VERSION_MAJOR (0)
#define SMETA_VERSION_MINOR (1)

#define EMETA_VERSION_MAJOR (0)
#define EMETA_VERSION_MINOR (2)

struct line_header {
	__le32 crc;
	__le32 identifier;	/* pblk identifier */
	__u8 uuid[16];		/* instance uuid */
	__le16 type;		/* line type */
	__u8 version_major;	/* version major */
	__u8 version_minor;	/* version minor */
	__le32 id;		/* line id for current line */
};

struct line_smeta {
	struct line_header header;

	__le32 crc;		/* Full structure including struct crc */
	/* Previous line metadata */
	__le32 prev_id;		/* Line id for previous line */

	/* Current line metadata */
	__le64 seq_nr;		/* Sequence number for current line */

	/* Active writers */
	__le32 window_wr_lun;	/* Number of parallel LUNs to write */

	__le32 rsvd[2];

	__le64 lun_bitmap[];
};


/*
 * Metadata layout in media:
 *	First sector:
 *		1. struct line_emeta
 *		2. bad block bitmap (u64 * window_wr_lun)
 *		3. write amplification counters
 *	Mid sectors (start at lbas_sector):
 *		3. nr_lbas (u64) forming lba list
 *	Last sectors (start at vsc_sector):
 *		4. u32 valid sector count (vsc) for all lines (~0U: free line)
 */
struct line_emeta {
	struct line_header header;

	__le32 crc;		/* Full structure including struct crc */

	/* Previous line metadata */
	__le32 prev_id;		/* Line id for prev line */

	/* Current line metadata */
	__le64 seq_nr;		/* Sequence number for current line */

	/* Active writers */
	__le32 window_wr_lun;	/* Number of parallel LUNs to write */

	/* Bookkeeping for recovery */
	__le32 next_id;		/* Line id for next line */
	__le64 nr_lbas;		/* Number of lbas mapped in line */
	__le64 nr_valid_lbas;	/* Number of valid lbas mapped in line */
	__le64 bb_bitmap[];     /* Updated bad block bitmap for line */
};


/* Write amplification counters stored on media */
struct wa_counters {
	__le64 user;		/* Number of user written sectors */
	__le64 gc;		/* Number of sectors written by GC*/
	__le64 pad;		/* Number of padded sectors */
};

struct pblk_emeta {
	struct line_emeta *buf;		/* emeta buffer in media format */
	int mem;			/* Write offset - points to next
					 * writable entry in memory
					 */
	atomic_t sync;			/* Synced - backpointer that signals the
					 * last entry that has been successfully
					 * persisted to media
					 */
	unsigned int nr_entries;	/* Number of emeta entries */
};

struct pblk_smeta {
	struct line_smeta *buf;		/* smeta buffer in persistent format */
};

struct pblk_w_err_gc {
	int has_write_err;
	__le64 *lba_list;
};

struct pblk_line {
	struct pblk *pblk;
	unsigned int id;		/* Line number corresponds to the
					 * block line
					 */
	unsigned int seq_nr;		/* Unique line sequence number */

	int state;			/* PBLK_LINESTATE_X */
	int type;			/* PBLK_LINETYPE_X */
	int gc_group;			/* PBLK_LINEGC_X */
	struct list_head list;		/* Free, GC lists */

	unsigned long *lun_bitmap;	/* Bitmap for LUNs mapped in line */

	struct nvm_chk_meta *chks;	/* Chunks forming line */

	struct pblk_smeta *smeta;	/* Start metadata */
	struct pblk_emeta *emeta;	/* End medatada */

	int meta_line;			/* Metadata line id */
	int meta_distance;		/* Distance between data and metadata */

	u64 smeta_ssec;			/* Sector where smeta starts */
	u64 emeta_ssec;			/* Sector where emeta starts */

	unsigned int sec_in_line;	/* Number of usable secs in line */

	atomic_t blk_in_line;		/* Number of good blocks in line */
	unsigned long *blk_bitmap;	/* Bitmap for valid/invalid blocks */
	unsigned long *erase_bitmap;	/* Bitmap for erased blocks */

	unsigned long *map_bitmap;	/* Bitmap for mapped sectors in line */
	unsigned long *invalid_bitmap;	/* Bitmap for invalid sectors in line */

	atomic_t left_eblks;		/* Blocks left for erasing */
	atomic_t left_seblks;		/* Blocks left for sync erasing */

	int left_msecs;			/* Sectors left for mapping */
	unsigned int cur_sec;		/* Sector map pointer */
	unsigned int nr_valid_lbas;	/* Number of valid lbas in line */

	__le32 *vsc;			/* Valid sector count in line */

	struct kref ref;		/* Write buffer L2P references */

	struct pblk_w_err_gc *w_err_gc;	/* Write error gc recovery metadata */

	spinlock_t lock;		/* Necessary for invalid_bitmap only */
};

#define PBLK_DATA_LINES 4

enum {
	PBLK_KMALLOC_META = 1,
	PBLK_VMALLOC_META = 2,
};

enum {
	PBLK_EMETA_TYPE_HEADER = 1,	/* struct line_emeta first sector */
	PBLK_EMETA_TYPE_LLBA = 2,	/* lba list - type: __le64 */
	PBLK_EMETA_TYPE_VSC = 3,	/* vsc list - type: __le32 */
};

struct pblk_line_mgmt {
	int nr_lines;			/* Total number of full lines */
	int nr_free_lines;		/* Number of full lines in free list */

	/* Free lists - use free_lock */
	struct list_head free_list;	/* Full lines ready to use */
	struct list_head corrupt_list;	/* Full lines corrupted */
	struct list_head bad_list;	/* Full lines bad */

	/* GC lists - use gc_lock */
	struct list_head *gc_lists[PBLK_GC_NR_LISTS];
	struct list_head gc_high_list;	/* Full lines ready to GC, high isc */
	struct list_head gc_mid_list;	/* Full lines ready to GC, mid isc */
	struct list_head gc_low_list;	/* Full lines ready to GC, low isc */

	struct list_head gc_werr_list;  /* Write err recovery list */

	struct list_head gc_full_list;	/* Full lines ready to GC, no valid */
	struct list_head gc_empty_list;	/* Full lines close, all valid */

	struct pblk_line *log_line;	/* Current FTL log line */
	struct pblk_line *data_line;	/* Current data line */
	struct pblk_line *log_next;	/* Next FTL log line */
	struct pblk_line *data_next;	/* Next data line */

	struct list_head emeta_list;	/* Lines queued to schedule emeta */

	__le32 *vsc_list;		/* Valid sector counts for all lines */

	/* Metadata allocation type: VMALLOC | KMALLOC */
	int emeta_alloc_type;

	/* Pre-allocated metadata for data lines */
	struct pblk_smeta *sline_meta[PBLK_DATA_LINES];
	struct pblk_emeta *eline_meta[PBLK_DATA_LINES];
	unsigned long meta_bitmap;

	/* Helpers for fast bitmap calculations */
	unsigned long *bb_template;
	unsigned long *bb_aux;

	unsigned long d_seq_nr;		/* Data line unique sequence number */
	unsigned long l_seq_nr;		/* Log line unique sequence number */

	spinlock_t free_lock;
	spinlock_t close_lock;
	spinlock_t gc_lock;
};

struct pblk_line_meta {
	unsigned int smeta_len;		/* Total length for smeta */
	unsigned int smeta_sec;		/* Sectors needed for smeta */

	unsigned int emeta_len[4];	/* Lengths for emeta:
					 *  [0]: Total
					 *  [1]: struct line_emeta +
					 *       bb_bitmap + struct wa_counters
					 *  [2]: L2P portion
					 *  [3]: vsc
					 */
	unsigned int emeta_sec[4];	/* Sectors needed for emeta. Same layout
					 * as emeta_len
					 */

	unsigned int emeta_bb;		/* Boundary for bb that affects emeta */

	unsigned int vsc_list_len;	/* Length for vsc list */
	unsigned int sec_bitmap_len;	/* Length for sector bitmap in line */
	unsigned int blk_bitmap_len;	/* Length for block bitmap in line */
	unsigned int lun_bitmap_len;	/* Length for lun bitmap in line */

	unsigned int blk_per_line;	/* Number of blocks in a full line */
	unsigned int sec_per_line;	/* Number of sectors in a line */
	unsigned int dsec_per_line;	/* Number of data sectors in a line */
	unsigned int min_blk_line;	/* Min. number of good blocks in line */

	unsigned int mid_thrs;		/* Threshold for GC mid list */
	unsigned int high_thrs;		/* Threshold for GC high list */

	unsigned int meta_distance;	/* Distance between data and metadata */
};

enum {
	PBLK_STATE_RUNNING = 0,
	PBLK_STATE_STOPPING = 1,
	PBLK_STATE_RECOVERING = 2,
	PBLK_STATE_STOPPED = 3,
};

/* Internal format to support not power-of-2 device formats */
struct pblk_addrf {
	/* gen to dev */
	int sec_stripe;
	int ch_stripe;
	int lun_stripe;

	/* dev to gen */
	int sec_lun_stripe;
	int sec_ws_stripe;
};

struct pblk {
	struct nvm_tgt_dev *dev;
	struct gendisk *disk;

	struct kobject kobj;

	struct pblk_lun *luns;

	struct pblk_line *lines;		/* Line array */
	struct pblk_line_mgmt l_mg;		/* Line management */
	struct pblk_line_meta lm;		/* Line metadata */

	struct nvm_addrf addrf;		/* Aligned address format */
	struct pblk_addrf uaddrf;	/* Unaligned address format */
	int addrf_len;

	struct pblk_rb rwb;

	int state;			/* pblk line state */

	int min_write_pgs; /* Minimum amount of pages required by controller */
	int max_write_pgs; /* Maximum amount of pages supported by controller */
	int pgs_in_buffer; /* Number of pages that need to be held in buffer to
			    * guarantee successful reads.
			    */

	sector_t capacity; /* Device capacity when bad blocks are subtracted */

	int op;      /* Percentage of device used for over-provisioning */
	int op_blks; /* Number of blocks used for over-provisioning */

	/* pblk provisioning values. Used by rate limiter */
	struct pblk_rl rl;

	int sec_per_write;

	unsigned char instance_uuid[16];

	/* Persistent write amplification counters, 4kb sector I/Os */
	atomic64_t user_wa;		/* Sectors written by user */
	atomic64_t gc_wa;		/* Sectors written by GC */
	atomic64_t pad_wa;		/* Padded sectors written */

	/* Reset values for delta write amplification measurements */
	u64 user_rst_wa;
	u64 gc_rst_wa;
	u64 pad_rst_wa;

	/* Counters used for calculating padding distribution */
	atomic64_t *pad_dist;		/* Padding distribution buckets */
	u64 nr_flush_rst;		/* Flushes reset value for pad dist.*/
	atomic64_t nr_flush;		/* Number of flush/fua I/O */

#ifdef CONFIG_NVM_DEBUG
	/* Non-persistent debug counters, 4kb sector I/Os */
	atomic_long_t inflight_writes;	/* Inflight writes (user and gc) */
	atomic_long_t padded_writes;	/* Sectors padded due to flush/fua */
	atomic_long_t padded_wb;	/* Sectors padded in write buffer */
	atomic_long_t req_writes;	/* Sectors stored on write buffer */
	atomic_long_t sub_writes;	/* Sectors submitted from buffer */
	atomic_long_t sync_writes;	/* Sectors synced to media */
	atomic_long_t inflight_reads;	/* Inflight sector read requests */
	atomic_long_t cache_reads;	/* Read requests that hit the cache */
	atomic_long_t sync_reads;	/* Completed sector read requests */
	atomic_long_t recov_writes;	/* Sectors submitted from recovery */
	atomic_long_t recov_gc_writes;	/* Sectors submitted from write GC */
	atomic_long_t recov_gc_reads;	/* Sectors submitted from read GC */
#endif

	spinlock_t lock;

	atomic_long_t read_failed;
	atomic_long_t read_empty;
	atomic_long_t read_high_ecc;
	atomic_long_t read_failed_gc;
	atomic_long_t write_failed;
	atomic_long_t erase_failed;

	atomic_t inflight_io;		/* General inflight I/O counter */

	struct task_struct *writer_ts;

	/* Simple translation map of logical addresses to physical addresses.
	 * The logical addresses is known by the host system, while the physical
	 * addresses are used when writing to the disk block device.
	 */
	unsigned char *trans_map;
	spinlock_t trans_lock;

	struct list_head compl_list;

	spinlock_t resubmit_lock;	 /* Resubmit list lock */
	struct list_head resubmit_list; /* Resubmit list for failed writes*/

	mempool_t page_bio_pool;
	mempool_t gen_ws_pool;
	mempool_t rec_pool;
	mempool_t r_rq_pool;
	mempool_t w_rq_pool;
	mempool_t e_rq_pool;

	struct workqueue_struct *close_wq;
	struct workqueue_struct *bb_wq;
	struct workqueue_struct *r_end_wq;

	struct timer_list wtimer;

	struct pblk_gc gc;
};

struct pblk_line_ws {
	struct pblk *pblk;
	struct pblk_line *line;
	void *priv;
	struct work_struct ws;
};

#define pblk_g_rq_size (sizeof(struct nvm_rq) + sizeof(struct pblk_g_ctx))
#define pblk_w_rq_size (sizeof(struct nvm_rq) + sizeof(struct pblk_c_ctx))

/*
 * pblk ring buffer operations
 */
int pblk_rb_init(struct pblk_rb *rb, struct pblk_rb_entry *rb_entry_base,
		 unsigned int power_size, unsigned int power_seg_sz);
unsigned int pblk_rb_calculate_size(unsigned int nr_entries);
void *pblk_rb_entries_ref(struct pblk_rb *rb);
int pblk_rb_may_write_user(struct pblk_rb *rb, struct bio *bio,
			   unsigned int nr_entries, unsigned int *pos);
int pblk_rb_may_write_gc(struct pblk_rb *rb, unsigned int nr_entries,
			 unsigned int *pos);
void pblk_rb_write_entry_user(struct pblk_rb *rb, void *data,
			      struct pblk_w_ctx w_ctx, unsigned int pos);
void pblk_rb_write_entry_gc(struct pblk_rb *rb, void *data,
			    struct pblk_w_ctx w_ctx, struct pblk_line *line,
			    u64 paddr, unsigned int pos);
struct pblk_w_ctx *pblk_rb_w_ctx(struct pblk_rb *rb, unsigned int pos);
void pblk_rb_flush(struct pblk_rb *rb);

void pblk_rb_sync_l2p(struct pblk_rb *rb);
unsigned int pblk_rb_read_to_bio(struct pblk_rb *rb, struct nvm_rq *rqd,
				 unsigned int pos, unsigned int nr_entries,
				 unsigned int count);
int pblk_rb_copy_to_bio(struct pblk_rb *rb, struct bio *bio, sector_t lba,
			struct ppa_addr ppa, int bio_iter, bool advanced_bio);
unsigned int pblk_rb_read_commit(struct pblk_rb *rb, unsigned int entries);

unsigned int pblk_rb_sync_init(struct pblk_rb *rb, unsigned long *flags);
unsigned int pblk_rb_sync_advance(struct pblk_rb *rb, unsigned int nr_entries);
struct pblk_rb_entry *pblk_rb_sync_scan_entry(struct pblk_rb *rb,
					      struct ppa_addr *ppa);
void pblk_rb_sync_end(struct pblk_rb *rb, unsigned long *flags);
unsigned int pblk_rb_flush_point_count(struct pblk_rb *rb);

unsigned int pblk_rb_read_count(struct pblk_rb *rb);
unsigned int pblk_rb_sync_count(struct pblk_rb *rb);
unsigned int pblk_rb_wrap_pos(struct pblk_rb *rb, unsigned int pos);

int pblk_rb_tear_down_check(struct pblk_rb *rb);
int pblk_rb_pos_oob(struct pblk_rb *rb, u64 pos);
void pblk_rb_data_free(struct pblk_rb *rb);
ssize_t pblk_rb_sysfs(struct pblk_rb *rb, char *buf);

/*
 * pblk core
 */
struct nvm_rq *pblk_alloc_rqd(struct pblk *pblk, int type);
void pblk_free_rqd(struct pblk *pblk, struct nvm_rq *rqd, int type);
void pblk_set_sec_per_write(struct pblk *pblk, int sec_per_write);
int pblk_setup_w_rec_rq(struct pblk *pblk, struct nvm_rq *rqd,
			struct pblk_c_ctx *c_ctx);
void pblk_discard(struct pblk *pblk, struct bio *bio);
struct nvm_chk_meta *pblk_chunk_get_info(struct pblk *pblk);
struct nvm_chk_meta *pblk_chunk_get_off(struct pblk *pblk,
					      struct nvm_chk_meta *lp,
					      struct ppa_addr ppa);
void pblk_log_write_err(struct pblk *pblk, struct nvm_rq *rqd);
void pblk_log_read_err(struct pblk *pblk, struct nvm_rq *rqd);
int pblk_submit_io(struct pblk *pblk, struct nvm_rq *rqd);
int pblk_submit_io_sync(struct pblk *pblk, struct nvm_rq *rqd);
int pblk_submit_meta_io(struct pblk *pblk, struct pblk_line *meta_line);
struct bio *pblk_bio_map_addr(struct pblk *pblk, void *data,
			      unsigned int nr_secs, unsigned int len,
			      int alloc_type, gfp_t gfp_mask);
struct pblk_line *pblk_line_get(struct pblk *pblk);
struct pblk_line *pblk_line_get_first_data(struct pblk *pblk);
struct pblk_line *pblk_line_replace_data(struct pblk *pblk);
int pblk_line_recov_alloc(struct pblk *pblk, struct pblk_line *line);
void pblk_line_recov_close(struct pblk *pblk, struct pblk_line *line);
struct pblk_line *pblk_line_get_data(struct pblk *pblk);
struct pblk_line *pblk_line_get_erase(struct pblk *pblk);
int pblk_line_erase(struct pblk *pblk, struct pblk_line *line);
int pblk_line_is_full(struct pblk_line *line);
void pblk_line_free(struct pblk_line *line);
void pblk_line_close_meta(struct pblk *pblk, struct pblk_line *line);
void pblk_line_close(struct pblk *pblk, struct pblk_line *line);
void pblk_line_close_ws(struct work_struct *work);
void pblk_pipeline_stop(struct pblk *pblk);
void __pblk_pipeline_stop(struct pblk *pblk);
void __pblk_pipeline_flush(struct pblk *pblk);
void pblk_gen_run_ws(struct pblk *pblk, struct pblk_line *line, void *priv,
		     void (*work)(struct work_struct *), gfp_t gfp_mask,
		     struct workqueue_struct *wq);
u64 pblk_line_smeta_start(struct pblk *pblk, struct pblk_line *line);
int pblk_line_read_smeta(struct pblk *pblk, struct pblk_line *line);
int pblk_line_read_emeta(struct pblk *pblk, struct pblk_line *line,
			 void *emeta_buf);
int pblk_blk_erase_async(struct pblk *pblk, struct ppa_addr erase_ppa);
void pblk_line_put(struct kref *ref);
void pblk_line_put_wq(struct kref *ref);
struct list_head *pblk_line_gc_list(struct pblk *pblk, struct pblk_line *line);
u64 pblk_lookup_page(struct pblk *pblk, struct pblk_line *line);
void pblk_dealloc_page(struct pblk *pblk, struct pblk_line *line, int nr_secs);
u64 pblk_alloc_page(struct pblk *pblk, struct pblk_line *line, int nr_secs);
u64 __pblk_alloc_page(struct pblk *pblk, struct pblk_line *line, int nr_secs);
int pblk_calc_secs(struct pblk *pblk, unsigned long secs_avail,
		   unsigned long secs_to_flush);
void pblk_up_page(struct pblk *pblk, struct ppa_addr *ppa_list, int nr_ppas);
void pblk_down_rq(struct pblk *pblk, struct ppa_addr *ppa_list, int nr_ppas,
		  unsigned long *lun_bitmap);
void pblk_down_page(struct pblk *pblk, struct ppa_addr *ppa_list, int nr_ppas);
void pblk_up_rq(struct pblk *pblk, struct ppa_addr *ppa_list, int nr_ppas,
		unsigned long *lun_bitmap);
int pblk_bio_add_pages(struct pblk *pblk, struct bio *bio, gfp_t flags,
		       int nr_pages);
void pblk_bio_free_pages(struct pblk *pblk, struct bio *bio, int off,
			 int nr_pages);
void pblk_map_invalidate(struct pblk *pblk, struct ppa_addr ppa);
void __pblk_map_invalidate(struct pblk *pblk, struct pblk_line *line,
			   u64 paddr);
void pblk_update_map(struct pblk *pblk, sector_t lba, struct ppa_addr ppa);
void pblk_update_map_cache(struct pblk *pblk, sector_t lba,
			   struct ppa_addr ppa);
void pblk_update_map_dev(struct pblk *pblk, sector_t lba,
			 struct ppa_addr ppa, struct ppa_addr entry_line);
int pblk_update_map_gc(struct pblk *pblk, sector_t lba, struct ppa_addr ppa,
		       struct pblk_line *gc_line, u64 paddr);
void pblk_lookup_l2p_rand(struct pblk *pblk, struct ppa_addr *ppas,
			  u64 *lba_list, int nr_secs);
void pblk_lookup_l2p_seq(struct pblk *pblk, struct ppa_addr *ppas,
			 sector_t blba, int nr_secs);

/*
 * pblk user I/O write path
 */
int pblk_write_to_cache(struct pblk *pblk, struct bio *bio,
			unsigned long flags);
int pblk_write_gc_to_cache(struct pblk *pblk, struct pblk_gc_rq *gc_rq);

/*
 * pblk map
 */
void pblk_map_erase_rq(struct pblk *pblk, struct nvm_rq *rqd,
		       unsigned int sentry, unsigned long *lun_bitmap,
		       unsigned int valid_secs, struct ppa_addr *erase_ppa);
void pblk_map_rq(struct pblk *pblk, struct nvm_rq *rqd, unsigned int sentry,
		 unsigned long *lun_bitmap, unsigned int valid_secs,
		 unsigned int off);

/*
 * pblk write thread
 */
int pblk_write_ts(void *data);
void pblk_write_timer_fn(struct timer_list *t);
void pblk_write_should_kick(struct pblk *pblk);
void pblk_write_kick(struct pblk *pblk);

/*
 * pblk read path
 */
extern struct bio_set pblk_bio_set;
int pblk_submit_read(struct pblk *pblk, struct bio *bio);
int pblk_submit_read_gc(struct pblk *pblk, struct pblk_gc_rq *gc_rq);
/*
 * pblk recovery
 */
struct pblk_line *pblk_recov_l2p(struct pblk *pblk);
int pblk_recov_pad(struct pblk *pblk);
int pblk_recov_check_emeta(struct pblk *pblk, struct line_emeta *emeta);

/*
 * pblk gc
 */
#define PBLK_GC_MAX_READERS 8	/* Max number of outstanding GC reader jobs */
#define PBLK_GC_RQ_QD 128	/* Queue depth for inflight GC requests */
#define PBLK_GC_L_QD 4		/* Queue depth for inflight GC lines */
#define PBLK_GC_RSV_LINE 1	/* Reserved lines for GC */

int pblk_gc_init(struct pblk *pblk);
void pblk_gc_exit(struct pblk *pblk, bool graceful);
void pblk_gc_should_start(struct pblk *pblk);
void pblk_gc_should_stop(struct pblk *pblk);
void pblk_gc_should_kick(struct pblk *pblk);
void pblk_gc_free_full_lines(struct pblk *pblk);
void pblk_gc_sysfs_state_show(struct pblk *pblk, int *gc_enabled,
			      int *gc_active);
int pblk_gc_sysfs_force(struct pblk *pblk, int force);

/*
 * pblk rate limiter
 */
void pblk_rl_init(struct pblk_rl *rl, int budget);
void pblk_rl_free(struct pblk_rl *rl);
void pblk_rl_update_rates(struct pblk_rl *rl);
int pblk_rl_high_thrs(struct pblk_rl *rl);
unsigned long pblk_rl_nr_free_blks(struct pblk_rl *rl);
unsigned long pblk_rl_nr_user_free_blks(struct pblk_rl *rl);
int pblk_rl_user_may_insert(struct pblk_rl *rl, int nr_entries);
void pblk_rl_inserted(struct pblk_rl *rl, int nr_entries);
void pblk_rl_user_in(struct pblk_rl *rl, int nr_entries);
int pblk_rl_gc_may_insert(struct pblk_rl *rl, int nr_entries);
void pblk_rl_gc_in(struct pblk_rl *rl, int nr_entries);
void pblk_rl_out(struct pblk_rl *rl, int nr_user, int nr_gc);
int pblk_rl_max_io(struct pblk_rl *rl);
void pblk_rl_free_lines_inc(struct pblk_rl *rl, struct pblk_line *line);
void pblk_rl_free_lines_dec(struct pblk_rl *rl, struct pblk_line *line,
			    bool used);
int pblk_rl_is_limit(struct pblk_rl *rl);

void pblk_rl_werr_line_in(struct pblk_rl *rl);
void pblk_rl_werr_line_out(struct pblk_rl *rl);

/*
 * pblk sysfs
 */
int pblk_sysfs_init(struct gendisk *tdisk);
void pblk_sysfs_exit(struct gendisk *tdisk);

static inline void *pblk_malloc(size_t size, int type, gfp_t flags)
{
	if (type == PBLK_KMALLOC_META)
		return kmalloc(size, flags);
	return vmalloc(size);
}

static inline void pblk_mfree(void *ptr, int type)
{
	if (type == PBLK_KMALLOC_META)
		kfree(ptr);
	else
		vfree(ptr);
}

static inline struct nvm_rq *nvm_rq_from_c_ctx(void *c_ctx)
{
	return c_ctx - sizeof(struct nvm_rq);
}

static inline void *emeta_to_bb(struct line_emeta *emeta)
{
	return emeta->bb_bitmap;
}

static inline void *emeta_to_wa(struct pblk_line_meta *lm,
				struct line_emeta *emeta)
{
	return emeta->bb_bitmap + lm->blk_bitmap_len;
}

static inline void *emeta_to_lbas(struct pblk *pblk, struct line_emeta *emeta)
{
	return ((void *)emeta + pblk->lm.emeta_len[1]);
}

static inline void *emeta_to_vsc(struct pblk *pblk, struct line_emeta *emeta)
{
	return (emeta_to_lbas(pblk, emeta) + pblk->lm.emeta_len[2]);
}

static inline int pblk_line_vsc(struct pblk_line *line)
{
	return le32_to_cpu(*line->vsc);
}

static inline int pblk_pad_distance(struct pblk *pblk)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;

	return geo->mw_cunits * geo->all_luns * geo->ws_opt;
}

static inline int pblk_ppa_to_line(struct ppa_addr p)
{
	return p.a.blk;
}

static inline int pblk_ppa_to_pos(struct nvm_geo *geo, struct ppa_addr p)
{
	return p.a.lun * geo->num_ch + p.a.ch;
}

static inline struct ppa_addr addr_to_gen_ppa(struct pblk *pblk, u64 paddr,
					      u64 line_id)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	struct ppa_addr ppa;

	if (geo->version == NVM_OCSSD_SPEC_12) {
		struct nvm_addrf_12 *ppaf = (struct nvm_addrf_12 *)&pblk->addrf;

		ppa.ppa = 0;
		ppa.g.blk = line_id;
		ppa.g.pg = (paddr & ppaf->pg_mask) >> ppaf->pg_offset;
		ppa.g.lun = (paddr & ppaf->lun_mask) >> ppaf->lun_offset;
		ppa.g.ch = (paddr & ppaf->ch_mask) >> ppaf->ch_offset;
		ppa.g.pl = (paddr & ppaf->pln_mask) >> ppaf->pln_offset;
		ppa.g.sec = (paddr & ppaf->sec_mask) >> ppaf->sec_offset;
	} else {
		struct pblk_addrf *uaddrf = &pblk->uaddrf;
		int secs, chnls, luns;

		ppa.ppa = 0;

		ppa.m.chk = line_id;

		paddr = div_u64_rem(paddr, uaddrf->sec_stripe, &secs);
		ppa.m.sec = secs;

		paddr = div_u64_rem(paddr, uaddrf->ch_stripe, &chnls);
		ppa.m.grp = chnls;

		paddr = div_u64_rem(paddr, uaddrf->lun_stripe, &luns);
		ppa.m.pu = luns;

		ppa.m.sec += uaddrf->sec_stripe * paddr;
	}

	return ppa;
}

static inline u64 pblk_dev_ppa_to_line_addr(struct pblk *pblk,
							struct ppa_addr p)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	u64 paddr;

	if (geo->version == NVM_OCSSD_SPEC_12) {
		struct nvm_addrf_12 *ppaf = (struct nvm_addrf_12 *)&pblk->addrf;

		paddr = (u64)p.g.ch << ppaf->ch_offset;
		paddr |= (u64)p.g.lun << ppaf->lun_offset;
		paddr |= (u64)p.g.pg << ppaf->pg_offset;
		paddr |= (u64)p.g.pl << ppaf->pln_offset;
		paddr |= (u64)p.g.sec << ppaf->sec_offset;
	} else {
		struct pblk_addrf *uaddrf = &pblk->uaddrf;
		u64 secs = p.m.sec;
		int sec_stripe;

		paddr = (u64)p.m.grp * uaddrf->sec_stripe;
		paddr += (u64)p.m.pu * uaddrf->sec_lun_stripe;

		secs = div_u64_rem(secs, uaddrf->sec_stripe, &sec_stripe);
		paddr += secs * uaddrf->sec_ws_stripe;
		paddr += sec_stripe;
	}

	return paddr;
}

static inline struct ppa_addr pblk_ppa32_to_ppa64(struct pblk *pblk, u32 ppa32)
{
	struct ppa_addr ppa64;

	ppa64.ppa = 0;

	if (ppa32 == -1) {
		ppa64.ppa = ADDR_EMPTY;
	} else if (ppa32 & (1U << 31)) {
		ppa64.c.line = ppa32 & ((~0U) >> 1);
		ppa64.c.is_cached = 1;
	} else {
		struct nvm_tgt_dev *dev = pblk->dev;
		struct nvm_geo *geo = &dev->geo;

		if (geo->version == NVM_OCSSD_SPEC_12) {
			struct nvm_addrf_12 *ppaf =
					(struct nvm_addrf_12 *)&pblk->addrf;

			ppa64.g.ch = (ppa32 & ppaf->ch_mask) >>
							ppaf->ch_offset;
			ppa64.g.lun = (ppa32 & ppaf->lun_mask) >>
							ppaf->lun_offset;
			ppa64.g.blk = (ppa32 & ppaf->blk_mask) >>
							ppaf->blk_offset;
			ppa64.g.pg = (ppa32 & ppaf->pg_mask) >>
							ppaf->pg_offset;
			ppa64.g.pl = (ppa32 & ppaf->pln_mask) >>
							ppaf->pln_offset;
			ppa64.g.sec = (ppa32 & ppaf->sec_mask) >>
							ppaf->sec_offset;
		} else {
			struct nvm_addrf *lbaf = &pblk->addrf;

			ppa64.m.grp = (ppa32 & lbaf->ch_mask) >>
							lbaf->ch_offset;
			ppa64.m.pu = (ppa32 & lbaf->lun_mask) >>
							lbaf->lun_offset;
			ppa64.m.chk = (ppa32 & lbaf->chk_mask) >>
							lbaf->chk_offset;
			ppa64.m.sec = (ppa32 & lbaf->sec_mask) >>
							lbaf->sec_offset;
		}
	}

	return ppa64;
}

static inline u32 pblk_ppa64_to_ppa32(struct pblk *pblk, struct ppa_addr ppa64)
{
	u32 ppa32 = 0;

	if (ppa64.ppa == ADDR_EMPTY) {
		ppa32 = ~0U;
	} else if (ppa64.c.is_cached) {
		ppa32 |= ppa64.c.line;
		ppa32 |= 1U << 31;
	} else {
		struct nvm_tgt_dev *dev = pblk->dev;
		struct nvm_geo *geo = &dev->geo;

		if (geo->version == NVM_OCSSD_SPEC_12) {
			struct nvm_addrf_12 *ppaf =
					(struct nvm_addrf_12 *)&pblk->addrf;

			ppa32 |= ppa64.g.ch << ppaf->ch_offset;
			ppa32 |= ppa64.g.lun << ppaf->lun_offset;
			ppa32 |= ppa64.g.blk << ppaf->blk_offset;
			ppa32 |= ppa64.g.pg << ppaf->pg_offset;
			ppa32 |= ppa64.g.pl << ppaf->pln_offset;
			ppa32 |= ppa64.g.sec << ppaf->sec_offset;
		} else {
			struct nvm_addrf *lbaf = &pblk->addrf;

			ppa32 |= ppa64.m.grp << lbaf->ch_offset;
			ppa32 |= ppa64.m.pu << lbaf->lun_offset;
			ppa32 |= ppa64.m.chk << lbaf->chk_offset;
			ppa32 |= ppa64.m.sec << lbaf->sec_offset;
		}
	}

	return ppa32;
}

static inline struct ppa_addr pblk_trans_map_get(struct pblk *pblk,
								sector_t lba)
{
	struct ppa_addr ppa;

	if (pblk->addrf_len < 32) {
		u32 *map = (u32 *)pblk->trans_map;

		ppa = pblk_ppa32_to_ppa64(pblk, map[lba]);
	} else {
		struct ppa_addr *map = (struct ppa_addr *)pblk->trans_map;

		ppa = map[lba];
	}

	return ppa;
}

static inline void pblk_trans_map_set(struct pblk *pblk, sector_t lba,
						struct ppa_addr ppa)
{
	if (pblk->addrf_len < 32) {
		u32 *map = (u32 *)pblk->trans_map;

		map[lba] = pblk_ppa64_to_ppa32(pblk, ppa);
	} else {
		u64 *map = (u64 *)pblk->trans_map;

		map[lba] = ppa.ppa;
	}
}

static inline int pblk_ppa_empty(struct ppa_addr ppa_addr)
{
	return (ppa_addr.ppa == ADDR_EMPTY);
}

static inline void pblk_ppa_set_empty(struct ppa_addr *ppa_addr)
{
	ppa_addr->ppa = ADDR_EMPTY;
}

static inline bool pblk_ppa_comp(struct ppa_addr lppa, struct ppa_addr rppa)
{
	return (lppa.ppa == rppa.ppa);
}

static inline int pblk_addr_in_cache(struct ppa_addr ppa)
{
	return (ppa.ppa != ADDR_EMPTY && ppa.c.is_cached);
}

static inline int pblk_addr_to_cacheline(struct ppa_addr ppa)
{
	return ppa.c.line;
}

static inline struct ppa_addr pblk_cacheline_to_addr(int addr)
{
	struct ppa_addr p;

	p.c.line = addr;
	p.c.is_cached = 1;

	return p;
}

static inline u32 pblk_calc_meta_header_crc(struct pblk *pblk,
					    struct line_header *header)
{
	u32 crc = ~(u32)0;

	crc = crc32_le(crc, (unsigned char *)header + sizeof(crc),
				sizeof(struct line_header) - sizeof(crc));

	return crc;
}

static inline u32 pblk_calc_smeta_crc(struct pblk *pblk,
				      struct line_smeta *smeta)
{
	struct pblk_line_meta *lm = &pblk->lm;
	u32 crc = ~(u32)0;

	crc = crc32_le(crc, (unsigned char *)smeta +
				sizeof(struct line_header) + sizeof(crc),
				lm->smeta_len -
				sizeof(struct line_header) - sizeof(crc));

	return crc;
}

static inline u32 pblk_calc_emeta_crc(struct pblk *pblk,
				      struct line_emeta *emeta)
{
	struct pblk_line_meta *lm = &pblk->lm;
	u32 crc = ~(u32)0;

	crc = crc32_le(crc, (unsigned char *)emeta +
				sizeof(struct line_header) + sizeof(crc),
				lm->emeta_len[0] -
				sizeof(struct line_header) - sizeof(crc));

	return crc;
}

static inline int pblk_set_progr_mode(struct pblk *pblk, int type)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	int flags;

	if (geo->version == NVM_OCSSD_SPEC_20)
		return 0;

	flags = geo->pln_mode >> 1;

	if (type == PBLK_WRITE)
		flags |= NVM_IO_SCRAMBLE_ENABLE;

	return flags;
}

enum {
	PBLK_READ_RANDOM	= 0,
	PBLK_READ_SEQUENTIAL	= 1,
};

static inline int pblk_set_read_mode(struct pblk *pblk, int type)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	int flags;

	if (geo->version == NVM_OCSSD_SPEC_20)
		return 0;

	flags = NVM_IO_SUSPEND | NVM_IO_SCRAMBLE_ENABLE;
	if (type == PBLK_READ_SEQUENTIAL)
		flags |= geo->pln_mode >> 1;

	return flags;
}

static inline int pblk_io_aligned(struct pblk *pblk, int nr_secs)
{
	return !(nr_secs % pblk->min_write_pgs);
}

#ifdef CONFIG_NVM_DEBUG
static inline void print_ppa(struct nvm_geo *geo, struct ppa_addr *p,
			     char *msg, int error)
{
	if (p->c.is_cached) {
		pr_err("ppa: (%s: %x) cache line: %llu\n",
				msg, error, (u64)p->c.line);
	} else if (geo->version == NVM_OCSSD_SPEC_12) {
		pr_err("ppa: (%s: %x):ch:%d,lun:%d,blk:%d,pg:%d,pl:%d,sec:%d\n",
			msg, error,
			p->g.ch, p->g.lun, p->g.blk,
			p->g.pg, p->g.pl, p->g.sec);
	} else {
		pr_err("ppa: (%s: %x):ch:%d,lun:%d,chk:%d,sec:%d\n",
			msg, error,
			p->m.grp, p->m.pu, p->m.chk, p->m.sec);
	}
}

static inline void pblk_print_failed_rqd(struct pblk *pblk, struct nvm_rq *rqd,
					 int error)
{
	int bit = -1;

	if (rqd->nr_ppas ==  1) {
		print_ppa(&pblk->dev->geo, &rqd->ppa_addr, "rqd", error);
		return;
	}

	while ((bit = find_next_bit((void *)&rqd->ppa_status, rqd->nr_ppas,
						bit + 1)) < rqd->nr_ppas) {
		print_ppa(&pblk->dev->geo, &rqd->ppa_list[bit], "rqd", error);
	}

	pr_err("error:%d, ppa_status:%llx\n", error, rqd->ppa_status);
}

static inline int pblk_boundary_ppa_checks(struct nvm_tgt_dev *tgt_dev,
				       struct ppa_addr *ppas, int nr_ppas)
{
	struct nvm_geo *geo = &tgt_dev->geo;
	struct ppa_addr *ppa;
	int i;

	for (i = 0; i < nr_ppas; i++) {
		ppa = &ppas[i];

		if (geo->version == NVM_OCSSD_SPEC_12) {
			if (!ppa->c.is_cached &&
					ppa->g.ch < geo->num_ch &&
					ppa->g.lun < geo->num_lun &&
					ppa->g.pl < geo->num_pln &&
					ppa->g.blk < geo->num_chk &&
					ppa->g.pg < geo->num_pg &&
					ppa->g.sec < geo->ws_min)
				continue;
		} else {
			if (!ppa->c.is_cached &&
					ppa->m.grp < geo->num_ch &&
					ppa->m.pu < geo->num_lun &&
					ppa->m.chk < geo->num_chk &&
					ppa->m.sec < geo->clba)
				continue;
		}

		print_ppa(geo, ppa, "boundary", i);

		return 1;
	}
	return 0;
}

static inline int pblk_check_io(struct pblk *pblk, struct nvm_rq *rqd)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct ppa_addr *ppa_list;

	ppa_list = (rqd->nr_ppas > 1) ? rqd->ppa_list : &rqd->ppa_addr;

	if (pblk_boundary_ppa_checks(dev, ppa_list, rqd->nr_ppas)) {
		WARN_ON(1);
		return -EINVAL;
	}

	if (rqd->opcode == NVM_OP_PWRITE) {
		struct pblk_line *line;
		struct ppa_addr ppa;
		int i;

		for (i = 0; i < rqd->nr_ppas; i++) {
			ppa = ppa_list[i];
			line = &pblk->lines[pblk_ppa_to_line(ppa)];

			spin_lock(&line->lock);
			if (line->state != PBLK_LINESTATE_OPEN) {
				pr_err("pblk: bad ppa: line:%d,state:%d\n",
							line->id, line->state);
				WARN_ON(1);
				spin_unlock(&line->lock);
				return -EINVAL;
			}
			spin_unlock(&line->lock);
		}
	}

	return 0;
}
#endif

static inline int pblk_boundary_paddr_checks(struct pblk *pblk, u64 paddr)
{
	struct pblk_line_meta *lm = &pblk->lm;

	if (paddr > lm->sec_per_line)
		return 1;

	return 0;
}

static inline unsigned int pblk_get_bi_idx(struct bio *bio)
{
	return bio->bi_iter.bi_idx;
}

static inline sector_t pblk_get_lba(struct bio *bio)
{
	return bio->bi_iter.bi_sector / NR_PHY_IN_LOG;
}

static inline unsigned int pblk_get_secs(struct bio *bio)
{
	return  bio->bi_iter.bi_size / PBLK_EXPOSED_PAGE_SIZE;
}

static inline void pblk_setup_uuid(struct pblk *pblk)
{
	uuid_le uuid;

	uuid_le_gen(&uuid);
	memcpy(pblk->instance_uuid, uuid.b, 16);
}
#endif /* PBLK_H_ */
