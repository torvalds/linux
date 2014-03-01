/* Copyright 2012 STEC, Inc.
 *
 * This file is licensed under the terms of the 3-clause
 * BSD License (http://opensource.org/licenses/BSD-3-Clause)
 * or the GNU GPL-2.0 (http://www.gnu.org/licenses/gpl-2.0.html),
 * at your option. Both licenses are also available in the LICENSE file
 * distributed with this project. This file may not be copied, modified,
 * or distributed except in accordance with those terms.
 * Gordoni Waidhofer <gwaidhofer@stec-inc.com>
 * Initial Driver Design!
 * Thomas Swann <tswann@stec-inc.com>
 * Interrupt handling.
 * Ramprasad Chinthekindi <rchinthekindi@stec-inc.com>
 * biomode implementation.
 * Akhil Bhansali <abhansali@stec-inc.com>
 * Added support for DISCARD / FLUSH and FUA.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/blkdev.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/compiler.h>
#include <linux/workqueue.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/time.h>
#include <linux/hdreg.h>
#include <linux/dma-mapping.h>
#include <linux/completion.h>
#include <linux/scatterlist.h>
#include <linux/version.h>
#include <linux/err.h>
#include <linux/scatterlist.h>
#include <linux/aer.h>
#include <linux/ctype.h>
#include <linux/wait.h>
#include <linux/uio.h>
#include <scsi/scsi.h>
#include <scsi/sg.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <asm/unaligned.h>

#include "skd_s1120.h"

static int skd_dbg_level;
static int skd_isr_comp_limit = 4;

enum {
	STEC_LINK_2_5GTS = 0,
	STEC_LINK_5GTS = 1,
	STEC_LINK_8GTS = 2,
	STEC_LINK_UNKNOWN = 0xFF
};

enum {
	SKD_FLUSH_INITIALIZER,
	SKD_FLUSH_ZERO_SIZE_FIRST,
	SKD_FLUSH_DATA_SECOND,
};

#define SKD_ASSERT(expr) \
	do { \
		if (unlikely(!(expr))) { \
			pr_err("Assertion failed! %s,%s,%s,line=%d\n",	\
			       # expr, __FILE__, __func__, __LINE__); \
		} \
	} while (0)

#define DRV_NAME "skd"
#define DRV_VERSION "2.2.1"
#define DRV_BUILD_ID "0260"
#define PFX DRV_NAME ": "
#define DRV_BIN_VERSION 0x100
#define DRV_VER_COMPL   "2.2.1." DRV_BUILD_ID

MODULE_AUTHOR("bug-reports: support@stec-inc.com");
MODULE_LICENSE("Dual BSD/GPL");

MODULE_DESCRIPTION("STEC s1120 PCIe SSD block driver (b" DRV_BUILD_ID ")");
MODULE_VERSION(DRV_VERSION "-" DRV_BUILD_ID);

#define PCI_VENDOR_ID_STEC      0x1B39
#define PCI_DEVICE_ID_S1120     0x0001

#define SKD_FUA_NV		(1 << 1)
#define SKD_MINORS_PER_DEVICE   16

#define SKD_MAX_QUEUE_DEPTH     200u

#define SKD_PAUSE_TIMEOUT       (5 * 1000)

#define SKD_N_FITMSG_BYTES      (512u)

#define SKD_N_SPECIAL_CONTEXT   32u
#define SKD_N_SPECIAL_FITMSG_BYTES      (128u)

/* SG elements are 32 bytes, so we can make this 4096 and still be under the
 * 128KB limit.  That allows 4096*4K = 16M xfer size
 */
#define SKD_N_SG_PER_REQ_DEFAULT 256u
#define SKD_N_SG_PER_SPECIAL    256u

#define SKD_N_COMPLETION_ENTRY  256u
#define SKD_N_READ_CAP_BYTES    (8u)

#define SKD_N_INTERNAL_BYTES    (512u)

/* 5 bits of uniqifier, 0xF800 */
#define SKD_ID_INCR             (0x400)
#define SKD_ID_TABLE_MASK       (3u << 8u)
#define  SKD_ID_RW_REQUEST      (0u << 8u)
#define  SKD_ID_INTERNAL        (1u << 8u)
#define  SKD_ID_SPECIAL_REQUEST (2u << 8u)
#define  SKD_ID_FIT_MSG         (3u << 8u)
#define SKD_ID_SLOT_MASK        0x00FFu
#define SKD_ID_SLOT_AND_TABLE_MASK 0x03FFu

#define SKD_N_TIMEOUT_SLOT      4u
#define SKD_TIMEOUT_SLOT_MASK   3u

#define SKD_N_MAX_SECTORS 2048u

#define SKD_MAX_RETRIES 2u

#define SKD_TIMER_SECONDS(seconds) (seconds)
#define SKD_TIMER_MINUTES(minutes) ((minutes) * (60))

#define INQ_STD_NBYTES 36
#define SKD_DISCARD_CDB_LENGTH	24

enum skd_drvr_state {
	SKD_DRVR_STATE_LOAD,
	SKD_DRVR_STATE_IDLE,
	SKD_DRVR_STATE_BUSY,
	SKD_DRVR_STATE_STARTING,
	SKD_DRVR_STATE_ONLINE,
	SKD_DRVR_STATE_PAUSING,
	SKD_DRVR_STATE_PAUSED,
	SKD_DRVR_STATE_DRAINING_TIMEOUT,
	SKD_DRVR_STATE_RESTARTING,
	SKD_DRVR_STATE_RESUMING,
	SKD_DRVR_STATE_STOPPING,
	SKD_DRVR_STATE_FAULT,
	SKD_DRVR_STATE_DISAPPEARED,
	SKD_DRVR_STATE_PROTOCOL_MISMATCH,
	SKD_DRVR_STATE_BUSY_ERASE,
	SKD_DRVR_STATE_BUSY_SANITIZE,
	SKD_DRVR_STATE_BUSY_IMMINENT,
	SKD_DRVR_STATE_WAIT_BOOT,
	SKD_DRVR_STATE_SYNCING,
};

#define SKD_WAIT_BOOT_TIMO      SKD_TIMER_SECONDS(90u)
#define SKD_STARTING_TIMO       SKD_TIMER_SECONDS(8u)
#define SKD_RESTARTING_TIMO     SKD_TIMER_MINUTES(4u)
#define SKD_DRAINING_TIMO       SKD_TIMER_SECONDS(6u)
#define SKD_BUSY_TIMO           SKD_TIMER_MINUTES(20u)
#define SKD_STARTED_BUSY_TIMO   SKD_TIMER_SECONDS(60u)
#define SKD_START_WAIT_SECONDS  90u

enum skd_req_state {
	SKD_REQ_STATE_IDLE,
	SKD_REQ_STATE_SETUP,
	SKD_REQ_STATE_BUSY,
	SKD_REQ_STATE_COMPLETED,
	SKD_REQ_STATE_TIMEOUT,
	SKD_REQ_STATE_ABORTED,
};

enum skd_fit_msg_state {
	SKD_MSG_STATE_IDLE,
	SKD_MSG_STATE_BUSY,
};

enum skd_check_status_action {
	SKD_CHECK_STATUS_REPORT_GOOD,
	SKD_CHECK_STATUS_REPORT_SMART_ALERT,
	SKD_CHECK_STATUS_REQUEUE_REQUEST,
	SKD_CHECK_STATUS_REPORT_ERROR,
	SKD_CHECK_STATUS_BUSY_IMMINENT,
};

struct skd_fitmsg_context {
	enum skd_fit_msg_state state;

	struct skd_fitmsg_context *next;

	u32 id;
	u16 outstanding;

	u32 length;
	u32 offset;

	u8 *msg_buf;
	dma_addr_t mb_dma_address;
};

struct skd_request_context {
	enum skd_req_state state;

	struct skd_request_context *next;

	u16 id;
	u32 fitmsg_id;

	struct request *req;
	u8 flush_cmd;
	u8 discard_page;

	u32 timeout_stamp;
	u8 sg_data_dir;
	struct scatterlist *sg;
	u32 n_sg;
	u32 sg_byte_count;

	struct fit_sg_descriptor *sksg_list;
	dma_addr_t sksg_dma_address;

	struct fit_completion_entry_v1 completion;

	struct fit_comp_error_info err_info;

};
#define SKD_DATA_DIR_HOST_TO_CARD       1
#define SKD_DATA_DIR_CARD_TO_HOST       2
#define SKD_DATA_DIR_NONE		3	/* especially for DISCARD requests. */

struct skd_special_context {
	struct skd_request_context req;

	u8 orphaned;

	void *data_buf;
	dma_addr_t db_dma_address;

	u8 *msg_buf;
	dma_addr_t mb_dma_address;
};

struct skd_sg_io {
	fmode_t mode;
	void __user *argp;

	struct sg_io_hdr sg;

	u8 cdb[16];

	u32 dxfer_len;
	u32 iovcnt;
	struct sg_iovec *iov;
	struct sg_iovec no_iov_iov;

	struct skd_special_context *skspcl;
};

typedef enum skd_irq_type {
	SKD_IRQ_LEGACY,
	SKD_IRQ_MSI,
	SKD_IRQ_MSIX
} skd_irq_type_t;

#define SKD_MAX_BARS                    2

struct skd_device {
	volatile void __iomem *mem_map[SKD_MAX_BARS];
	resource_size_t mem_phys[SKD_MAX_BARS];
	u32 mem_size[SKD_MAX_BARS];

	skd_irq_type_t irq_type;
	u32 msix_count;
	struct skd_msix_entry *msix_entries;

	struct pci_dev *pdev;
	int pcie_error_reporting_is_enabled;

	spinlock_t lock;
	struct gendisk *disk;
	struct request_queue *queue;
	struct device *class_dev;
	int gendisk_on;
	int sync_done;

	atomic_t device_count;
	u32 devno;
	u32 major;
	char name[32];
	char isr_name[30];

	enum skd_drvr_state state;
	u32 drive_state;

	u32 in_flight;
	u32 cur_max_queue_depth;
	u32 queue_low_water_mark;
	u32 dev_max_queue_depth;

	u32 num_fitmsg_context;
	u32 num_req_context;

	u32 timeout_slot[SKD_N_TIMEOUT_SLOT];
	u32 timeout_stamp;
	struct skd_fitmsg_context *skmsg_free_list;
	struct skd_fitmsg_context *skmsg_table;

	struct skd_request_context *skreq_free_list;
	struct skd_request_context *skreq_table;

	struct skd_special_context *skspcl_free_list;
	struct skd_special_context *skspcl_table;

	struct skd_special_context internal_skspcl;
	u32 read_cap_blocksize;
	u32 read_cap_last_lba;
	int read_cap_is_valid;
	int inquiry_is_valid;
	u8 inq_serial_num[13];  /*12 chars plus null term */
	u8 id_str[80];          /* holds a composite name (pci + sernum) */

	u8 skcomp_cycle;
	u32 skcomp_ix;
	struct fit_completion_entry_v1 *skcomp_table;
	struct fit_comp_error_info *skerr_table;
	dma_addr_t cq_dma_address;

	wait_queue_head_t waitq;

	struct timer_list timer;
	u32 timer_countdown;
	u32 timer_substate;

	int n_special;
	int sgs_per_request;
	u32 last_mtd;

	u32 proto_ver;

	int dbg_level;
	u32 connect_time_stamp;
	int connect_retries;
#define SKD_MAX_CONNECT_RETRIES 16
	u32 drive_jiffies;

	u32 timo_slot;


	struct work_struct completion_worker;
};

#define SKD_WRITEL(DEV, VAL, OFF) skd_reg_write32(DEV, VAL, OFF)
#define SKD_READL(DEV, OFF)      skd_reg_read32(DEV, OFF)
#define SKD_WRITEQ(DEV, VAL, OFF) skd_reg_write64(DEV, VAL, OFF)

static inline u32 skd_reg_read32(struct skd_device *skdev, u32 offset)
{
	u32 val;

	if (likely(skdev->dbg_level < 2))
		return readl(skdev->mem_map[1] + offset);
	else {
		barrier();
		val = readl(skdev->mem_map[1] + offset);
		barrier();
		pr_debug("%s:%s:%d offset %x = %x\n",
			 skdev->name, __func__, __LINE__, offset, val);
		return val;
	}

}

static inline void skd_reg_write32(struct skd_device *skdev, u32 val,
				   u32 offset)
{
	if (likely(skdev->dbg_level < 2)) {
		writel(val, skdev->mem_map[1] + offset);
		barrier();
	} else {
		barrier();
		writel(val, skdev->mem_map[1] + offset);
		barrier();
		pr_debug("%s:%s:%d offset %x = %x\n",
			 skdev->name, __func__, __LINE__, offset, val);
	}
}

static inline void skd_reg_write64(struct skd_device *skdev, u64 val,
				   u32 offset)
{
	if (likely(skdev->dbg_level < 2)) {
		writeq(val, skdev->mem_map[1] + offset);
		barrier();
	} else {
		barrier();
		writeq(val, skdev->mem_map[1] + offset);
		barrier();
		pr_debug("%s:%s:%d offset %x = %016llx\n",
			 skdev->name, __func__, __LINE__, offset, val);
	}
}


#define SKD_IRQ_DEFAULT SKD_IRQ_MSI
static int skd_isr_type = SKD_IRQ_DEFAULT;

module_param(skd_isr_type, int, 0444);
MODULE_PARM_DESC(skd_isr_type, "Interrupt type capability."
		 " (0==legacy, 1==MSI, 2==MSI-X, default==1)");

#define SKD_MAX_REQ_PER_MSG_DEFAULT 1
static int skd_max_req_per_msg = SKD_MAX_REQ_PER_MSG_DEFAULT;

module_param(skd_max_req_per_msg, int, 0444);
MODULE_PARM_DESC(skd_max_req_per_msg,
		 "Maximum SCSI requests packed in a single message."
		 " (1-14, default==1)");

#define SKD_MAX_QUEUE_DEPTH_DEFAULT 64
#define SKD_MAX_QUEUE_DEPTH_DEFAULT_STR "64"
static int skd_max_queue_depth = SKD_MAX_QUEUE_DEPTH_DEFAULT;

module_param(skd_max_queue_depth, int, 0444);
MODULE_PARM_DESC(skd_max_queue_depth,
		 "Maximum SCSI requests issued to s1120."
		 " (1-200, default==" SKD_MAX_QUEUE_DEPTH_DEFAULT_STR ")");

static int skd_sgs_per_request = SKD_N_SG_PER_REQ_DEFAULT;
module_param(skd_sgs_per_request, int, 0444);
MODULE_PARM_DESC(skd_sgs_per_request,
		 "Maximum SG elements per block request."
		 " (1-4096, default==256)");

static int skd_max_pass_thru = SKD_N_SPECIAL_CONTEXT;
module_param(skd_max_pass_thru, int, 0444);
MODULE_PARM_DESC(skd_max_pass_thru,
		 "Maximum SCSI pass-thru at a time." " (1-50, default==32)");

module_param(skd_dbg_level, int, 0444);
MODULE_PARM_DESC(skd_dbg_level, "s1120 debug level (0,1,2)");

module_param(skd_isr_comp_limit, int, 0444);
MODULE_PARM_DESC(skd_isr_comp_limit, "s1120 isr comp limit (0=none) default=4");

/* Major device number dynamically assigned. */
static u32 skd_major;

static void skd_destruct(struct skd_device *skdev);
static const struct block_device_operations skd_blockdev_ops;
static void skd_send_fitmsg(struct skd_device *skdev,
			    struct skd_fitmsg_context *skmsg);
static void skd_send_special_fitmsg(struct skd_device *skdev,
				    struct skd_special_context *skspcl);
static void skd_request_fn(struct request_queue *rq);
static void skd_end_request(struct skd_device *skdev,
			    struct skd_request_context *skreq, int error);
static int skd_preop_sg_list(struct skd_device *skdev,
			     struct skd_request_context *skreq);
static void skd_postop_sg_list(struct skd_device *skdev,
			       struct skd_request_context *skreq);

static void skd_restart_device(struct skd_device *skdev);
static int skd_quiesce_dev(struct skd_device *skdev);
static int skd_unquiesce_dev(struct skd_device *skdev);
static void skd_release_special(struct skd_device *skdev,
				struct skd_special_context *skspcl);
static void skd_disable_interrupts(struct skd_device *skdev);
static void skd_isr_fwstate(struct skd_device *skdev);
static void skd_recover_requests(struct skd_device *skdev, int requeue);
static void skd_soft_reset(struct skd_device *skdev);

static const char *skd_name(struct skd_device *skdev);
const char *skd_drive_state_to_str(int state);
const char *skd_skdev_state_to_str(enum skd_drvr_state state);
static void skd_log_skdev(struct skd_device *skdev, const char *event);
static void skd_log_skmsg(struct skd_device *skdev,
			  struct skd_fitmsg_context *skmsg, const char *event);
static void skd_log_skreq(struct skd_device *skdev,
			  struct skd_request_context *skreq, const char *event);

/*
 *****************************************************************************
 * READ/WRITE REQUESTS
 *****************************************************************************
 */
static void skd_fail_all_pending(struct skd_device *skdev)
{
	struct request_queue *q = skdev->queue;
	struct request *req;

	for (;; ) {
		req = blk_peek_request(q);
		if (req == NULL)
			break;
		blk_start_request(req);
		__blk_end_request_all(req, -EIO);
	}
}

static void
skd_prep_rw_cdb(struct skd_scsi_request *scsi_req,
		int data_dir, unsigned lba,
		unsigned count)
{
	if (data_dir == READ)
		scsi_req->cdb[0] = 0x28;
	else
		scsi_req->cdb[0] = 0x2a;

	scsi_req->cdb[1] = 0;
	scsi_req->cdb[2] = (lba & 0xff000000) >> 24;
	scsi_req->cdb[3] = (lba & 0xff0000) >> 16;
	scsi_req->cdb[4] = (lba & 0xff00) >> 8;
	scsi_req->cdb[5] = (lba & 0xff);
	scsi_req->cdb[6] = 0;
	scsi_req->cdb[7] = (count & 0xff00) >> 8;
	scsi_req->cdb[8] = count & 0xff;
	scsi_req->cdb[9] = 0;
}

static void
skd_prep_zerosize_flush_cdb(struct skd_scsi_request *scsi_req,
			    struct skd_request_context *skreq)
{
	skreq->flush_cmd = 1;

	scsi_req->cdb[0] = 0x35;
	scsi_req->cdb[1] = 0;
	scsi_req->cdb[2] = 0;
	scsi_req->cdb[3] = 0;
	scsi_req->cdb[4] = 0;
	scsi_req->cdb[5] = 0;
	scsi_req->cdb[6] = 0;
	scsi_req->cdb[7] = 0;
	scsi_req->cdb[8] = 0;
	scsi_req->cdb[9] = 0;
}

static void
skd_prep_discard_cdb(struct skd_scsi_request *scsi_req,
		     struct skd_request_context *skreq,
		     struct page *page,
		     u32 lba, u32 count)
{
	char *buf;
	unsigned long len;
	struct request *req;

	buf = page_address(page);
	len = SKD_DISCARD_CDB_LENGTH;

	scsi_req->cdb[0] = UNMAP;
	scsi_req->cdb[8] = len;

	put_unaligned_be16(6 + 16, &buf[0]);
	put_unaligned_be16(16, &buf[2]);
	put_unaligned_be64(lba, &buf[8]);
	put_unaligned_be32(count, &buf[16]);

	req = skreq->req;
	blk_add_request_payload(req, page, len);
	req->buffer = buf;
}

static void skd_request_fn_not_online(struct request_queue *q);

static void skd_request_fn(struct request_queue *q)
{
	struct skd_device *skdev = q->queuedata;
	struct skd_fitmsg_context *skmsg = NULL;
	struct fit_msg_hdr *fmh = NULL;
	struct skd_request_context *skreq;
	struct request *req = NULL;
	struct skd_scsi_request *scsi_req;
	struct page *page;
	unsigned long io_flags;
	int error;
	u32 lba;
	u32 count;
	int data_dir;
	u32 be_lba;
	u32 be_count;
	u64 be_dmaa;
	u64 cmdctxt;
	u32 timo_slot;
	void *cmd_ptr;
	int flush, fua;

	if (skdev->state != SKD_DRVR_STATE_ONLINE) {
		skd_request_fn_not_online(q);
		return;
	}

	if (blk_queue_stopped(skdev->queue)) {
		if (skdev->skmsg_free_list == NULL ||
		    skdev->skreq_free_list == NULL ||
		    skdev->in_flight >= skdev->queue_low_water_mark)
			/* There is still some kind of shortage */
			return;

		queue_flag_clear(QUEUE_FLAG_STOPPED, skdev->queue);
	}

	/*
	 * Stop conditions:
	 *  - There are no more native requests
	 *  - There are already the maximum number of requests in progress
	 *  - There are no more skd_request_context entries
	 *  - There are no more FIT msg buffers
	 */
	for (;; ) {

		flush = fua = 0;

		req = blk_peek_request(q);

		/* Are there any native requests to start? */
		if (req == NULL)
			break;

		lba = (u32)blk_rq_pos(req);
		count = blk_rq_sectors(req);
		data_dir = rq_data_dir(req);
		io_flags = req->cmd_flags;

		if (io_flags & REQ_FLUSH)
			flush++;

		if (io_flags & REQ_FUA)
			fua++;

		pr_debug("%s:%s:%d new req=%p lba=%u(0x%x) "
			 "count=%u(0x%x) dir=%d\n",
			 skdev->name, __func__, __LINE__,
			 req, lba, lba, count, count, data_dir);

		/* At this point we know there is a request */

		/* Are too many requets already in progress? */
		if (skdev->in_flight >= skdev->cur_max_queue_depth) {
			pr_debug("%s:%s:%d qdepth %d, limit %d\n",
				 skdev->name, __func__, __LINE__,
				 skdev->in_flight, skdev->cur_max_queue_depth);
			break;
		}

		/* Is a skd_request_context available? */
		skreq = skdev->skreq_free_list;
		if (skreq == NULL) {
			pr_debug("%s:%s:%d Out of req=%p\n",
				 skdev->name, __func__, __LINE__, q);
			break;
		}
		SKD_ASSERT(skreq->state == SKD_REQ_STATE_IDLE);
		SKD_ASSERT((skreq->id & SKD_ID_INCR) == 0);

		/* Now we check to see if we can get a fit msg */
		if (skmsg == NULL) {
			if (skdev->skmsg_free_list == NULL) {
				pr_debug("%s:%s:%d Out of msg\n",
					 skdev->name, __func__, __LINE__);
				break;
			}
		}

		skreq->flush_cmd = 0;
		skreq->n_sg = 0;
		skreq->sg_byte_count = 0;
		skreq->discard_page = 0;

		/*
		 * OK to now dequeue request from q.
		 *
		 * At this point we are comitted to either start or reject
		 * the native request. Note that skd_request_context is
		 * available but is still at the head of the free list.
		 */
		blk_start_request(req);
		skreq->req = req;
		skreq->fitmsg_id = 0;

		/* Either a FIT msg is in progress or we have to start one. */
		if (skmsg == NULL) {
			/* Are there any FIT msg buffers available? */
			skmsg = skdev->skmsg_free_list;
			if (skmsg == NULL) {
				pr_debug("%s:%s:%d Out of msg skdev=%p\n",
					 skdev->name, __func__, __LINE__,
					 skdev);
				break;
			}
			SKD_ASSERT(skmsg->state == SKD_MSG_STATE_IDLE);
			SKD_ASSERT((skmsg->id & SKD_ID_INCR) == 0);

			skdev->skmsg_free_list = skmsg->next;

			skmsg->state = SKD_MSG_STATE_BUSY;
			skmsg->id += SKD_ID_INCR;

			/* Initialize the FIT msg header */
			fmh = (struct fit_msg_hdr *)skmsg->msg_buf;
			memset(fmh, 0, sizeof(*fmh));
			fmh->protocol_id = FIT_PROTOCOL_ID_SOFIT;
			skmsg->length = sizeof(*fmh);
		}

		skreq->fitmsg_id = skmsg->id;

		/*
		 * Note that a FIT msg may have just been started
		 * but contains no SoFIT requests yet.
		 */

		/*
		 * Transcode the request, checking as we go. The outcome of
		 * the transcoding is represented by the error variable.
		 */
		cmd_ptr = &skmsg->msg_buf[skmsg->length];
		memset(cmd_ptr, 0, 32);

		be_lba = cpu_to_be32(lba);
		be_count = cpu_to_be32(count);
		be_dmaa = cpu_to_be64((u64)skreq->sksg_dma_address);
		cmdctxt = skreq->id + SKD_ID_INCR;

		scsi_req = cmd_ptr;
		scsi_req->hdr.tag = cmdctxt;
		scsi_req->hdr.sg_list_dma_address = be_dmaa;

		if (data_dir == READ)
			skreq->sg_data_dir = SKD_DATA_DIR_CARD_TO_HOST;
		else
			skreq->sg_data_dir = SKD_DATA_DIR_HOST_TO_CARD;

		if (io_flags & REQ_DISCARD) {
			page = alloc_page(GFP_ATOMIC | __GFP_ZERO);
			if (!page) {
				pr_err("request_fn:Page allocation failed.\n");
				skd_end_request(skdev, skreq, -ENOMEM);
				break;
			}
			skreq->discard_page = 1;
			skd_prep_discard_cdb(scsi_req, skreq, page, lba, count);

		} else if (flush == SKD_FLUSH_ZERO_SIZE_FIRST) {
			skd_prep_zerosize_flush_cdb(scsi_req, skreq);
			SKD_ASSERT(skreq->flush_cmd == 1);

		} else {
			skd_prep_rw_cdb(scsi_req, data_dir, lba, count);
		}

		if (fua)
			scsi_req->cdb[1] |= SKD_FUA_NV;

		if (!req->bio)
			goto skip_sg;

		error = skd_preop_sg_list(skdev, skreq);

		if (error != 0) {
			/*
			 * Complete the native request with error.
			 * Note that the request context is still at the
			 * head of the free list, and that the SoFIT request
			 * was encoded into the FIT msg buffer but the FIT
			 * msg length has not been updated. In short, the
			 * only resource that has been allocated but might
			 * not be used is that the FIT msg could be empty.
			 */
			pr_debug("%s:%s:%d error Out\n",
				 skdev->name, __func__, __LINE__);
			skd_end_request(skdev, skreq, error);
			continue;
		}

skip_sg:
		scsi_req->hdr.sg_list_len_bytes =
			cpu_to_be32(skreq->sg_byte_count);

		/* Complete resource allocations. */
		skdev->skreq_free_list = skreq->next;
		skreq->state = SKD_REQ_STATE_BUSY;
		skreq->id += SKD_ID_INCR;

		skmsg->length += sizeof(struct skd_scsi_request);
		fmh->num_protocol_cmds_coalesced++;

		/*
		 * Update the active request counts.
		 * Capture the timeout timestamp.
		 */
		skreq->timeout_stamp = skdev->timeout_stamp;
		timo_slot = skreq->timeout_stamp & SKD_TIMEOUT_SLOT_MASK;
		skdev->timeout_slot[timo_slot]++;
		skdev->in_flight++;
		pr_debug("%s:%s:%d req=0x%x busy=%d\n",
			 skdev->name, __func__, __LINE__,
			 skreq->id, skdev->in_flight);

		/*
		 * If the FIT msg buffer is full send it.
		 */
		if (skmsg->length >= SKD_N_FITMSG_BYTES ||
		    fmh->num_protocol_cmds_coalesced >= skd_max_req_per_msg) {
			skd_send_fitmsg(skdev, skmsg);
			skmsg = NULL;
			fmh = NULL;
		}
	}

	/*
	 * Is a FIT msg in progress? If it is empty put the buffer back
	 * on the free list. If it is non-empty send what we got.
	 * This minimizes latency when there are fewer requests than
	 * what fits in a FIT msg.
	 */
	if (skmsg != NULL) {
		/* Bigger than just a FIT msg header? */
		if (skmsg->length > sizeof(struct fit_msg_hdr)) {
			pr_debug("%s:%s:%d sending msg=%p, len %d\n",
				 skdev->name, __func__, __LINE__,
				 skmsg, skmsg->length);
			skd_send_fitmsg(skdev, skmsg);
		} else {
			/*
			 * The FIT msg is empty. It means we got started
			 * on the msg, but the requests were rejected.
			 */
			skmsg->state = SKD_MSG_STATE_IDLE;
			skmsg->id += SKD_ID_INCR;
			skmsg->next = skdev->skmsg_free_list;
			skdev->skmsg_free_list = skmsg;
		}
		skmsg = NULL;
		fmh = NULL;
	}

	/*
	 * If req is non-NULL it means there is something to do but
	 * we are out of a resource.
	 */
	if (req)
		blk_stop_queue(skdev->queue);
}

static void skd_end_request(struct skd_device *skdev,
			    struct skd_request_context *skreq, int error)
{
	struct request *req = skreq->req;
	unsigned int io_flags = req->cmd_flags;

	if ((io_flags & REQ_DISCARD) &&
		(skreq->discard_page == 1)) {
		pr_debug("%s:%s:%d, free the page!",
			 skdev->name, __func__, __LINE__);
		free_page((unsigned long)req->buffer);
		req->buffer = NULL;
	}

	if (unlikely(error)) {
		struct request *req = skreq->req;
		char *cmd = (rq_data_dir(req) == READ) ? "read" : "write";
		u32 lba = (u32)blk_rq_pos(req);
		u32 count = blk_rq_sectors(req);

		pr_err("(%s): Error cmd=%s sect=%u count=%u id=0x%x\n",
		       skd_name(skdev), cmd, lba, count, skreq->id);
	} else
		pr_debug("%s:%s:%d id=0x%x error=%d\n",
			 skdev->name, __func__, __LINE__, skreq->id, error);

	__blk_end_request_all(skreq->req, error);
}

static int skd_preop_sg_list(struct skd_device *skdev,
			     struct skd_request_context *skreq)
{
	struct request *req = skreq->req;
	int writing = skreq->sg_data_dir == SKD_DATA_DIR_HOST_TO_CARD;
	int pci_dir = writing ? PCI_DMA_TODEVICE : PCI_DMA_FROMDEVICE;
	struct scatterlist *sg = &skreq->sg[0];
	int n_sg;
	int i;

	skreq->sg_byte_count = 0;

	/* SKD_ASSERT(skreq->sg_data_dir == SKD_DATA_DIR_HOST_TO_CARD ||
		   skreq->sg_data_dir == SKD_DATA_DIR_CARD_TO_HOST); */

	n_sg = blk_rq_map_sg(skdev->queue, req, sg);
	if (n_sg <= 0)
		return -EINVAL;

	/*
	 * Map scatterlist to PCI bus addresses.
	 * Note PCI might change the number of entries.
	 */
	n_sg = pci_map_sg(skdev->pdev, sg, n_sg, pci_dir);
	if (n_sg <= 0)
		return -EINVAL;

	SKD_ASSERT(n_sg <= skdev->sgs_per_request);

	skreq->n_sg = n_sg;

	for (i = 0; i < n_sg; i++) {
		struct fit_sg_descriptor *sgd = &skreq->sksg_list[i];
		u32 cnt = sg_dma_len(&sg[i]);
		uint64_t dma_addr = sg_dma_address(&sg[i]);

		sgd->control = FIT_SGD_CONTROL_NOT_LAST;
		sgd->byte_count = cnt;
		skreq->sg_byte_count += cnt;
		sgd->host_side_addr = dma_addr;
		sgd->dev_side_addr = 0;
	}

	skreq->sksg_list[n_sg - 1].next_desc_ptr = 0LL;
	skreq->sksg_list[n_sg - 1].control = FIT_SGD_CONTROL_LAST;

	if (unlikely(skdev->dbg_level > 1)) {
		pr_debug("%s:%s:%d skreq=%x sksg_list=%p sksg_dma=%llx\n",
			 skdev->name, __func__, __LINE__,
			 skreq->id, skreq->sksg_list, skreq->sksg_dma_address);
		for (i = 0; i < n_sg; i++) {
			struct fit_sg_descriptor *sgd = &skreq->sksg_list[i];
			pr_debug("%s:%s:%d   sg[%d] count=%u ctrl=0x%x "
				 "addr=0x%llx next=0x%llx\n",
				 skdev->name, __func__, __LINE__,
				 i, sgd->byte_count, sgd->control,
				 sgd->host_side_addr, sgd->next_desc_ptr);
		}
	}

	return 0;
}

static void skd_postop_sg_list(struct skd_device *skdev,
			       struct skd_request_context *skreq)
{
	int writing = skreq->sg_data_dir == SKD_DATA_DIR_HOST_TO_CARD;
	int pci_dir = writing ? PCI_DMA_TODEVICE : PCI_DMA_FROMDEVICE;

	/*
	 * restore the next ptr for next IO request so we
	 * don't have to set it every time.
	 */
	skreq->sksg_list[skreq->n_sg - 1].next_desc_ptr =
		skreq->sksg_dma_address +
		((skreq->n_sg) * sizeof(struct fit_sg_descriptor));
	pci_unmap_sg(skdev->pdev, &skreq->sg[0], skreq->n_sg, pci_dir);
}

static void skd_request_fn_not_online(struct request_queue *q)
{
	struct skd_device *skdev = q->queuedata;
	int error;

	SKD_ASSERT(skdev->state != SKD_DRVR_STATE_ONLINE);

	skd_log_skdev(skdev, "req_not_online");
	switch (skdev->state) {
	case SKD_DRVR_STATE_PAUSING:
	case SKD_DRVR_STATE_PAUSED:
	case SKD_DRVR_STATE_STARTING:
	case SKD_DRVR_STATE_RESTARTING:
	case SKD_DRVR_STATE_WAIT_BOOT:
	/* In case of starting, we haven't started the queue,
	 * so we can't get here... but requests are
	 * possibly hanging out waiting for us because we
	 * reported the dev/skd0 already.  They'll wait
	 * forever if connect doesn't complete.
	 * What to do??? delay dev/skd0 ??
	 */
	case SKD_DRVR_STATE_BUSY:
	case SKD_DRVR_STATE_BUSY_IMMINENT:
	case SKD_DRVR_STATE_BUSY_ERASE:
	case SKD_DRVR_STATE_DRAINING_TIMEOUT:
		return;

	case SKD_DRVR_STATE_BUSY_SANITIZE:
	case SKD_DRVR_STATE_STOPPING:
	case SKD_DRVR_STATE_SYNCING:
	case SKD_DRVR_STATE_FAULT:
	case SKD_DRVR_STATE_DISAPPEARED:
	default:
		error = -EIO;
		break;
	}

	/* If we get here, terminate all pending block requeusts
	 * with EIO and any scsi pass thru with appropriate sense
	 */

	skd_fail_all_pending(skdev);
}

/*
 *****************************************************************************
 * TIMER
 *****************************************************************************
 */

static void skd_timer_tick_not_online(struct skd_device *skdev);

static void skd_timer_tick(ulong arg)
{
	struct skd_device *skdev = (struct skd_device *)arg;

	u32 timo_slot;
	u32 overdue_timestamp;
	unsigned long reqflags;
	u32 state;

	if (skdev->state == SKD_DRVR_STATE_FAULT)
		/* The driver has declared fault, and we want it to
		 * stay that way until driver is reloaded.
		 */
		return;

	spin_lock_irqsave(&skdev->lock, reqflags);

	state = SKD_READL(skdev, FIT_STATUS);
	state &= FIT_SR_DRIVE_STATE_MASK;
	if (state != skdev->drive_state)
		skd_isr_fwstate(skdev);

	if (skdev->state != SKD_DRVR_STATE_ONLINE) {
		skd_timer_tick_not_online(skdev);
		goto timer_func_out;
	}
	skdev->timeout_stamp++;
	timo_slot = skdev->timeout_stamp & SKD_TIMEOUT_SLOT_MASK;

	/*
	 * All requests that happened during the previous use of
	 * this slot should be done by now. The previous use was
	 * over 7 seconds ago.
	 */
	if (skdev->timeout_slot[timo_slot] == 0)
		goto timer_func_out;

	/* Something is overdue */
	overdue_timestamp = skdev->timeout_stamp - SKD_N_TIMEOUT_SLOT;

	pr_debug("%s:%s:%d found %d timeouts, draining busy=%d\n",
		 skdev->name, __func__, __LINE__,
		 skdev->timeout_slot[timo_slot], skdev->in_flight);
	pr_err("(%s): Overdue IOs (%d), busy %d\n",
	       skd_name(skdev), skdev->timeout_slot[timo_slot],
	       skdev->in_flight);

	skdev->timer_countdown = SKD_DRAINING_TIMO;
	skdev->state = SKD_DRVR_STATE_DRAINING_TIMEOUT;
	skdev->timo_slot = timo_slot;
	blk_stop_queue(skdev->queue);

timer_func_out:
	mod_timer(&skdev->timer, (jiffies + HZ));

	spin_unlock_irqrestore(&skdev->lock, reqflags);
}

static void skd_timer_tick_not_online(struct skd_device *skdev)
{
	switch (skdev->state) {
	case SKD_DRVR_STATE_IDLE:
	case SKD_DRVR_STATE_LOAD:
		break;
	case SKD_DRVR_STATE_BUSY_SANITIZE:
		pr_debug("%s:%s:%d drive busy sanitize[%x], driver[%x]\n",
			 skdev->name, __func__, __LINE__,
			 skdev->drive_state, skdev->state);
		/* If we've been in sanitize for 3 seconds, we figure we're not
		 * going to get anymore completions, so recover requests now
		 */
		if (skdev->timer_countdown > 0) {
			skdev->timer_countdown--;
			return;
		}
		skd_recover_requests(skdev, 0);
		break;

	case SKD_DRVR_STATE_BUSY:
	case SKD_DRVR_STATE_BUSY_IMMINENT:
	case SKD_DRVR_STATE_BUSY_ERASE:
		pr_debug("%s:%s:%d busy[%x], countdown=%d\n",
			 skdev->name, __func__, __LINE__,
			 skdev->state, skdev->timer_countdown);
		if (skdev->timer_countdown > 0) {
			skdev->timer_countdown--;
			return;
		}
		pr_debug("%s:%s:%d busy[%x], timedout=%d, restarting device.",
			 skdev->name, __func__, __LINE__,
			 skdev->state, skdev->timer_countdown);
		skd_restart_device(skdev);
		break;

	case SKD_DRVR_STATE_WAIT_BOOT:
	case SKD_DRVR_STATE_STARTING:
		if (skdev->timer_countdown > 0) {
			skdev->timer_countdown--;
			return;
		}
		/* For now, we fault the drive.  Could attempt resets to
		 * revcover at some point. */
		skdev->state = SKD_DRVR_STATE_FAULT;

		pr_err("(%s): DriveFault Connect Timeout (%x)\n",
		       skd_name(skdev), skdev->drive_state);

		/*start the queue so we can respond with error to requests */
		/* wakeup anyone waiting for startup complete */
		blk_start_queue(skdev->queue);
		skdev->gendisk_on = -1;
		wake_up_interruptible(&skdev->waitq);
		break;

	case SKD_DRVR_STATE_ONLINE:
		/* shouldn't get here. */
		break;

	case SKD_DRVR_STATE_PAUSING:
	case SKD_DRVR_STATE_PAUSED:
		break;

	case SKD_DRVR_STATE_DRAINING_TIMEOUT:
		pr_debug("%s:%s:%d "
			 "draining busy [%d] tick[%d] qdb[%d] tmls[%d]\n",
			 skdev->name, __func__, __LINE__,
			 skdev->timo_slot,
			 skdev->timer_countdown,
			 skdev->in_flight,
			 skdev->timeout_slot[skdev->timo_slot]);
		/* if the slot has cleared we can let the I/O continue */
		if (skdev->timeout_slot[skdev->timo_slot] == 0) {
			pr_debug("%s:%s:%d Slot drained, starting queue.\n",
				 skdev->name, __func__, __LINE__);
			skdev->state = SKD_DRVR_STATE_ONLINE;
			blk_start_queue(skdev->queue);
			return;
		}
		if (skdev->timer_countdown > 0) {
			skdev->timer_countdown--;
			return;
		}
		skd_restart_device(skdev);
		break;

	case SKD_DRVR_STATE_RESTARTING:
		if (skdev->timer_countdown > 0) {
			skdev->timer_countdown--;
			return;
		}
		/* For now, we fault the drive. Could attempt resets to
		 * revcover at some point. */
		skdev->state = SKD_DRVR_STATE_FAULT;
		pr_err("(%s): DriveFault Reconnect Timeout (%x)\n",
		       skd_name(skdev), skdev->drive_state);

		/*
		 * Recovering does two things:
		 * 1. completes IO with error
		 * 2. reclaims dma resources
		 * When is it safe to recover requests?
		 * - if the drive state is faulted
		 * - if the state is still soft reset after out timeout
		 * - if the drive registers are dead (state = FF)
		 * If it is "unsafe", we still need to recover, so we will
		 * disable pci bus mastering and disable our interrupts.
		 */

		if ((skdev->drive_state == FIT_SR_DRIVE_SOFT_RESET) ||
		    (skdev->drive_state == FIT_SR_DRIVE_FAULT) ||
		    (skdev->drive_state == FIT_SR_DRIVE_STATE_MASK))
			/* It never came out of soft reset. Try to
			 * recover the requests and then let them
			 * fail. This is to mitigate hung processes. */
			skd_recover_requests(skdev, 0);
		else {
			pr_err("(%s): Disable BusMaster (%x)\n",
			       skd_name(skdev), skdev->drive_state);
			pci_disable_device(skdev->pdev);
			skd_disable_interrupts(skdev);
			skd_recover_requests(skdev, 0);
		}

		/*start the queue so we can respond with error to requests */
		/* wakeup anyone waiting for startup complete */
		blk_start_queue(skdev->queue);
		skdev->gendisk_on = -1;
		wake_up_interruptible(&skdev->waitq);
		break;

	case SKD_DRVR_STATE_RESUMING:
	case SKD_DRVR_STATE_STOPPING:
	case SKD_DRVR_STATE_SYNCING:
	case SKD_DRVR_STATE_FAULT:
	case SKD_DRVR_STATE_DISAPPEARED:
	default:
		break;
	}
}

static int skd_start_timer(struct skd_device *skdev)
{
	int rc;

	init_timer(&skdev->timer);
	setup_timer(&skdev->timer, skd_timer_tick, (ulong)skdev);

	rc = mod_timer(&skdev->timer, (jiffies + HZ));
	if (rc)
		pr_err("%s: failed to start timer %d\n",
		       __func__, rc);
	return rc;
}

static void skd_kill_timer(struct skd_device *skdev)
{
	del_timer_sync(&skdev->timer);
}

/*
 *****************************************************************************
 * IOCTL
 *****************************************************************************
 */
static int skd_ioctl_sg_io(struct skd_device *skdev,
			   fmode_t mode, void __user *argp);
static int skd_sg_io_get_and_check_args(struct skd_device *skdev,
					struct skd_sg_io *sksgio);
static int skd_sg_io_obtain_skspcl(struct skd_device *skdev,
				   struct skd_sg_io *sksgio);
static int skd_sg_io_prep_buffering(struct skd_device *skdev,
				    struct skd_sg_io *sksgio);
static int skd_sg_io_copy_buffer(struct skd_device *skdev,
				 struct skd_sg_io *sksgio, int dxfer_dir);
static int skd_sg_io_send_fitmsg(struct skd_device *skdev,
				 struct skd_sg_io *sksgio);
static int skd_sg_io_await(struct skd_device *skdev, struct skd_sg_io *sksgio);
static int skd_sg_io_release_skspcl(struct skd_device *skdev,
				    struct skd_sg_io *sksgio);
static int skd_sg_io_put_status(struct skd_device *skdev,
				struct skd_sg_io *sksgio);

static void skd_complete_special(struct skd_device *skdev,
				 volatile struct fit_completion_entry_v1
				 *skcomp,
				 volatile struct fit_comp_error_info *skerr,
				 struct skd_special_context *skspcl);

static int skd_bdev_ioctl(struct block_device *bdev, fmode_t mode,
			  uint cmd_in, ulong arg)
{
	int rc = 0;
	struct gendisk *disk = bdev->bd_disk;
	struct skd_device *skdev = disk->private_data;
	void __user *p = (void *)arg;

	pr_debug("%s:%s:%d %s: CMD[%s] ioctl  mode 0x%x, cmd 0x%x arg %0lx\n",
		 skdev->name, __func__, __LINE__,
		 disk->disk_name, current->comm, mode, cmd_in, arg);

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	switch (cmd_in) {
	case SG_SET_TIMEOUT:
	case SG_GET_TIMEOUT:
	case SG_GET_VERSION_NUM:
		rc = scsi_cmd_ioctl(disk->queue, disk, mode, cmd_in, p);
		break;
	case SG_IO:
		rc = skd_ioctl_sg_io(skdev, mode, p);
		break;

	default:
		rc = -ENOTTY;
		break;
	}

	pr_debug("%s:%s:%d %s:  completion rc %d\n",
		 skdev->name, __func__, __LINE__, disk->disk_name, rc);
	return rc;
}

static int skd_ioctl_sg_io(struct skd_device *skdev, fmode_t mode,
			   void __user *argp)
{
	int rc;
	struct skd_sg_io sksgio;

	memset(&sksgio, 0, sizeof(sksgio));
	sksgio.mode = mode;
	sksgio.argp = argp;
	sksgio.iov = &sksgio.no_iov_iov;

	switch (skdev->state) {
	case SKD_DRVR_STATE_ONLINE:
	case SKD_DRVR_STATE_BUSY_IMMINENT:
		break;

	default:
		pr_debug("%s:%s:%d drive not online\n",
			 skdev->name, __func__, __LINE__);
		rc = -ENXIO;
		goto out;
	}

	rc = skd_sg_io_get_and_check_args(skdev, &sksgio);
	if (rc)
		goto out;

	rc = skd_sg_io_obtain_skspcl(skdev, &sksgio);
	if (rc)
		goto out;

	rc = skd_sg_io_prep_buffering(skdev, &sksgio);
	if (rc)
		goto out;

	rc = skd_sg_io_copy_buffer(skdev, &sksgio, SG_DXFER_TO_DEV);
	if (rc)
		goto out;

	rc = skd_sg_io_send_fitmsg(skdev, &sksgio);
	if (rc)
		goto out;

	rc = skd_sg_io_await(skdev, &sksgio);
	if (rc)
		goto out;

	rc = skd_sg_io_copy_buffer(skdev, &sksgio, SG_DXFER_FROM_DEV);
	if (rc)
		goto out;

	rc = skd_sg_io_put_status(skdev, &sksgio);
	if (rc)
		goto out;

	rc = 0;

out:
	skd_sg_io_release_skspcl(skdev, &sksgio);

	if (sksgio.iov != NULL && sksgio.iov != &sksgio.no_iov_iov)
		kfree(sksgio.iov);
	return rc;
}

static int skd_sg_io_get_and_check_args(struct skd_device *skdev,
					struct skd_sg_io *sksgio)
{
	struct sg_io_hdr *sgp = &sksgio->sg;
	int i, acc;

	if (!access_ok(VERIFY_WRITE, sksgio->argp, sizeof(sg_io_hdr_t))) {
		pr_debug("%s:%s:%d access sg failed %p\n",
			 skdev->name, __func__, __LINE__, sksgio->argp);
		return -EFAULT;
	}

	if (__copy_from_user(sgp, sksgio->argp, sizeof(sg_io_hdr_t))) {
		pr_debug("%s:%s:%d copy_from_user sg failed %p\n",
			 skdev->name, __func__, __LINE__, sksgio->argp);
		return -EFAULT;
	}

	if (sgp->interface_id != SG_INTERFACE_ID_ORIG) {
		pr_debug("%s:%s:%d interface_id invalid 0x%x\n",
			 skdev->name, __func__, __LINE__, sgp->interface_id);
		return -EINVAL;
	}

	if (sgp->cmd_len > sizeof(sksgio->cdb)) {
		pr_debug("%s:%s:%d cmd_len invalid %d\n",
			 skdev->name, __func__, __LINE__, sgp->cmd_len);
		return -EINVAL;
	}

	if (sgp->iovec_count > 256) {
		pr_debug("%s:%s:%d iovec_count invalid %d\n",
			 skdev->name, __func__, __LINE__, sgp->iovec_count);
		return -EINVAL;
	}

	if (sgp->dxfer_len > (PAGE_SIZE * SKD_N_SG_PER_SPECIAL)) {
		pr_debug("%s:%s:%d dxfer_len invalid %d\n",
			 skdev->name, __func__, __LINE__, sgp->dxfer_len);
		return -EINVAL;
	}

	switch (sgp->dxfer_direction) {
	case SG_DXFER_NONE:
		acc = -1;
		break;

	case SG_DXFER_TO_DEV:
		acc = VERIFY_READ;
		break;

	case SG_DXFER_FROM_DEV:
	case SG_DXFER_TO_FROM_DEV:
		acc = VERIFY_WRITE;
		break;

	default:
		pr_debug("%s:%s:%d dxfer_dir invalid %d\n",
			 skdev->name, __func__, __LINE__, sgp->dxfer_direction);
		return -EINVAL;
	}

	if (copy_from_user(sksgio->cdb, sgp->cmdp, sgp->cmd_len)) {
		pr_debug("%s:%s:%d copy_from_user cmdp failed %p\n",
			 skdev->name, __func__, __LINE__, sgp->cmdp);
		return -EFAULT;
	}

	if (sgp->mx_sb_len != 0) {
		if (!access_ok(VERIFY_WRITE, sgp->sbp, sgp->mx_sb_len)) {
			pr_debug("%s:%s:%d access sbp failed %p\n",
				 skdev->name, __func__, __LINE__, sgp->sbp);
			return -EFAULT;
		}
	}

	if (sgp->iovec_count == 0) {
		sksgio->iov[0].iov_base = sgp->dxferp;
		sksgio->iov[0].iov_len = sgp->dxfer_len;
		sksgio->iovcnt = 1;
		sksgio->dxfer_len = sgp->dxfer_len;
	} else {
		struct sg_iovec *iov;
		uint nbytes = sizeof(*iov) * sgp->iovec_count;
		size_t iov_data_len;

		iov = kmalloc(nbytes, GFP_KERNEL);
		if (iov == NULL) {
			pr_debug("%s:%s:%d alloc iovec failed %d\n",
				 skdev->name, __func__, __LINE__,
				 sgp->iovec_count);
			return -ENOMEM;
		}
		sksgio->iov = iov;
		sksgio->iovcnt = sgp->iovec_count;

		if (copy_from_user(iov, sgp->dxferp, nbytes)) {
			pr_debug("%s:%s:%d copy_from_user iovec failed %p\n",
				 skdev->name, __func__, __LINE__, sgp->dxferp);
			return -EFAULT;
		}

		/*
		 * Sum up the vecs, making sure they don't overflow
		 */
		iov_data_len = 0;
		for (i = 0; i < sgp->iovec_count; i++) {
			if (iov_data_len + iov[i].iov_len < iov_data_len)
				return -EINVAL;
			iov_data_len += iov[i].iov_len;
		}

		/* SG_IO howto says that the shorter of the two wins */
		if (sgp->dxfer_len < iov_data_len) {
			sksgio->iovcnt = iov_shorten((struct iovec *)iov,
						     sgp->iovec_count,
						     sgp->dxfer_len);
			sksgio->dxfer_len = sgp->dxfer_len;
		} else
			sksgio->dxfer_len = iov_data_len;
	}

	if (sgp->dxfer_direction != SG_DXFER_NONE) {
		struct sg_iovec *iov = sksgio->iov;
		for (i = 0; i < sksgio->iovcnt; i++, iov++) {
			if (!access_ok(acc, iov->iov_base, iov->iov_len)) {
				pr_debug("%s:%s:%d access data failed %p/%d\n",
					 skdev->name, __func__, __LINE__,
					 iov->iov_base, (int)iov->iov_len);
				return -EFAULT;
			}
		}
	}

	return 0;
}

static int skd_sg_io_obtain_skspcl(struct skd_device *skdev,
				   struct skd_sg_io *sksgio)
{
	struct skd_special_context *skspcl = NULL;
	int rc;

	for (;;) {
		ulong flags;

		spin_lock_irqsave(&skdev->lock, flags);
		skspcl = skdev->skspcl_free_list;
		if (skspcl != NULL) {
			skdev->skspcl_free_list =
				(struct skd_special_context *)skspcl->req.next;
			skspcl->req.id += SKD_ID_INCR;
			skspcl->req.state = SKD_REQ_STATE_SETUP;
			skspcl->orphaned = 0;
			skspcl->req.n_sg = 0;
		}
		spin_unlock_irqrestore(&skdev->lock, flags);

		if (skspcl != NULL) {
			rc = 0;
			break;
		}

		pr_debug("%s:%s:%d blocking\n",
			 skdev->name, __func__, __LINE__);

		rc = wait_event_interruptible_timeout(
				skdev->waitq,
				(skdev->skspcl_free_list != NULL),
				msecs_to_jiffies(sksgio->sg.timeout));

		pr_debug("%s:%s:%d unblocking, rc=%d\n",
			 skdev->name, __func__, __LINE__, rc);

		if (rc <= 0) {
			if (rc == 0)
				rc = -ETIMEDOUT;
			else
				rc = -EINTR;
			break;
		}
		/*
		 * If we get here rc > 0 meaning the timeout to
		 * wait_event_interruptible_timeout() had time left, hence the
		 * sought event -- non-empty free list -- happened.
		 * Retry the allocation.
		 */
	}
	sksgio->skspcl = skspcl;

	return rc;
}

static int skd_skreq_prep_buffering(struct skd_device *skdev,
				    struct skd_request_context *skreq,
				    u32 dxfer_len)
{
	u32 resid = dxfer_len;

	/*
	 * The DMA engine must have aligned addresses and byte counts.
	 */
	resid += (-resid) & 3;
	skreq->sg_byte_count = resid;

	skreq->n_sg = 0;

	while (resid > 0) {
		u32 nbytes = PAGE_SIZE;
		u32 ix = skreq->n_sg;
		struct scatterlist *sg = &skreq->sg[ix];
		struct fit_sg_descriptor *sksg = &skreq->sksg_list[ix];
		struct page *page;

		if (nbytes > resid)
			nbytes = resid;

		page = alloc_page(GFP_KERNEL);
		if (page == NULL)
			return -ENOMEM;

		sg_set_page(sg, page, nbytes, 0);

		/* TODO: This should be going through a pci_???()
		 * routine to do proper mapping. */
		sksg->control = FIT_SGD_CONTROL_NOT_LAST;
		sksg->byte_count = nbytes;

		sksg->host_side_addr = sg_phys(sg);

		sksg->dev_side_addr = 0;
		sksg->next_desc_ptr = skreq->sksg_dma_address +
				      (ix + 1) * sizeof(*sksg);

		skreq->n_sg++;
		resid -= nbytes;
	}

	if (skreq->n_sg > 0) {
		u32 ix = skreq->n_sg - 1;
		struct fit_sg_descriptor *sksg = &skreq->sksg_list[ix];

		sksg->control = FIT_SGD_CONTROL_LAST;
		sksg->next_desc_ptr = 0;
	}

	if (unlikely(skdev->dbg_level > 1)) {
		u32 i;

		pr_debug("%s:%s:%d skreq=%x sksg_list=%p sksg_dma=%llx\n",
			 skdev->name, __func__, __LINE__,
			 skreq->id, skreq->sksg_list, skreq->sksg_dma_address);
		for (i = 0; i < skreq->n_sg; i++) {
			struct fit_sg_descriptor *sgd = &skreq->sksg_list[i];

			pr_debug("%s:%s:%d   sg[%d] count=%u ctrl=0x%x "
				 "addr=0x%llx next=0x%llx\n",
				 skdev->name, __func__, __LINE__,
				 i, sgd->byte_count, sgd->control,
				 sgd->host_side_addr, sgd->next_desc_ptr);
		}
	}

	return 0;
}

static int skd_sg_io_prep_buffering(struct skd_device *skdev,
				    struct skd_sg_io *sksgio)
{
	struct skd_special_context *skspcl = sksgio->skspcl;
	struct skd_request_context *skreq = &skspcl->req;
	u32 dxfer_len = sksgio->dxfer_len;
	int rc;

	rc = skd_skreq_prep_buffering(skdev, skreq, dxfer_len);
	/*
	 * Eventually, errors or not, skd_release_special() is called
	 * to recover allocations including partial allocations.
	 */
	return rc;
}

static int skd_sg_io_copy_buffer(struct skd_device *skdev,
				 struct skd_sg_io *sksgio, int dxfer_dir)
{
	struct skd_special_context *skspcl = sksgio->skspcl;
	u32 iov_ix = 0;
	struct sg_iovec curiov;
	u32 sksg_ix = 0;
	u8 *bufp = NULL;
	u32 buf_len = 0;
	u32 resid = sksgio->dxfer_len;
	int rc;

	curiov.iov_len = 0;
	curiov.iov_base = NULL;

	if (dxfer_dir != sksgio->sg.dxfer_direction) {
		if (dxfer_dir != SG_DXFER_TO_DEV ||
		    sksgio->sg.dxfer_direction != SG_DXFER_TO_FROM_DEV)
			return 0;
	}

	while (resid > 0) {
		u32 nbytes = PAGE_SIZE;

		if (curiov.iov_len == 0) {
			curiov = sksgio->iov[iov_ix++];
			continue;
		}

		if (buf_len == 0) {
			struct page *page;
			page = sg_page(&skspcl->req.sg[sksg_ix++]);
			bufp = page_address(page);
			buf_len = PAGE_SIZE;
		}

		nbytes = min_t(u32, nbytes, resid);
		nbytes = min_t(u32, nbytes, curiov.iov_len);
		nbytes = min_t(u32, nbytes, buf_len);

		if (dxfer_dir == SG_DXFER_TO_DEV)
			rc = __copy_from_user(bufp, curiov.iov_base, nbytes);
		else
			rc = __copy_to_user(curiov.iov_base, bufp, nbytes);

		if (rc)
			return -EFAULT;

		resid -= nbytes;
		curiov.iov_len -= nbytes;
		curiov.iov_base += nbytes;
		buf_len -= nbytes;
	}

	return 0;
}

static int skd_sg_io_send_fitmsg(struct skd_device *skdev,
				 struct skd_sg_io *sksgio)
{
	struct skd_special_context *skspcl = sksgio->skspcl;
	struct fit_msg_hdr *fmh = (struct fit_msg_hdr *)skspcl->msg_buf;
	struct skd_scsi_request *scsi_req = (struct skd_scsi_request *)&fmh[1];

	memset(skspcl->msg_buf, 0, SKD_N_SPECIAL_FITMSG_BYTES);

	/* Initialize the FIT msg header */
	fmh->protocol_id = FIT_PROTOCOL_ID_SOFIT;
	fmh->num_protocol_cmds_coalesced = 1;

	/* Initialize the SCSI request */
	if (sksgio->sg.dxfer_direction != SG_DXFER_NONE)
		scsi_req->hdr.sg_list_dma_address =
			cpu_to_be64(skspcl->req.sksg_dma_address);
	scsi_req->hdr.tag = skspcl->req.id;
	scsi_req->hdr.sg_list_len_bytes =
		cpu_to_be32(skspcl->req.sg_byte_count);
	memcpy(scsi_req->cdb, sksgio->cdb, sizeof(scsi_req->cdb));

	skspcl->req.state = SKD_REQ_STATE_BUSY;
	skd_send_special_fitmsg(skdev, skspcl);

	return 0;
}

static int skd_sg_io_await(struct skd_device *skdev, struct skd_sg_io *sksgio)
{
	unsigned long flags;
	int rc;

	rc = wait_event_interruptible_timeout(skdev->waitq,
					      (sksgio->skspcl->req.state !=
					       SKD_REQ_STATE_BUSY),
					      msecs_to_jiffies(sksgio->sg.
							       timeout));

	spin_lock_irqsave(&skdev->lock, flags);

	if (sksgio->skspcl->req.state == SKD_REQ_STATE_ABORTED) {
		pr_debug("%s:%s:%d skspcl %p aborted\n",
			 skdev->name, __func__, __LINE__, sksgio->skspcl);

		/* Build check cond, sense and let command finish. */
		/* For a timeout, we must fabricate completion and sense
		 * data to complete the command */
		sksgio->skspcl->req.completion.status =
			SAM_STAT_CHECK_CONDITION;

		memset(&sksgio->skspcl->req.err_info, 0,
		       sizeof(sksgio->skspcl->req.err_info));
		sksgio->skspcl->req.err_info.type = 0x70;
		sksgio->skspcl->req.err_info.key = ABORTED_COMMAND;
		sksgio->skspcl->req.err_info.code = 0x44;
		sksgio->skspcl->req.err_info.qual = 0;
		rc = 0;
	} else if (sksgio->skspcl->req.state != SKD_REQ_STATE_BUSY)
		/* No longer on the adapter. We finish. */
		rc = 0;
	else {
		/* Something's gone wrong. Still busy. Timeout or
		 * user interrupted (control-C). Mark as an orphan
		 * so it will be disposed when completed. */
		sksgio->skspcl->orphaned = 1;
		sksgio->skspcl = NULL;
		if (rc == 0) {
			pr_debug("%s:%s:%d timed out %p (%u ms)\n",
				 skdev->name, __func__, __LINE__,
				 sksgio, sksgio->sg.timeout);
			rc = -ETIMEDOUT;
		} else {
			pr_debug("%s:%s:%d cntlc %p\n",
				 skdev->name, __func__, __LINE__, sksgio);
			rc = -EINTR;
		}
	}

	spin_unlock_irqrestore(&skdev->lock, flags);

	return rc;
}

static int skd_sg_io_put_status(struct skd_device *skdev,
				struct skd_sg_io *sksgio)
{
	struct sg_io_hdr *sgp = &sksgio->sg;
	struct skd_special_context *skspcl = sksgio->skspcl;
	int resid = 0;

	u32 nb = be32_to_cpu(skspcl->req.completion.num_returned_bytes);

	sgp->status = skspcl->req.completion.status;
	resid = sksgio->dxfer_len - nb;

	sgp->masked_status = sgp->status & STATUS_MASK;
	sgp->msg_status = 0;
	sgp->host_status = 0;
	sgp->driver_status = 0;
	sgp->resid = resid;
	if (sgp->masked_status || sgp->host_status || sgp->driver_status)
		sgp->info |= SG_INFO_CHECK;

	pr_debug("%s:%s:%d status %x masked %x resid 0x%x\n",
		 skdev->name, __func__, __LINE__,
		 sgp->status, sgp->masked_status, sgp->resid);

	if (sgp->masked_status == SAM_STAT_CHECK_CONDITION) {
		if (sgp->mx_sb_len > 0) {
			struct fit_comp_error_info *ei = &skspcl->req.err_info;
			u32 nbytes = sizeof(*ei);

			nbytes = min_t(u32, nbytes, sgp->mx_sb_len);

			sgp->sb_len_wr = nbytes;

			if (__copy_to_user(sgp->sbp, ei, nbytes)) {
				pr_debug("%s:%s:%d copy_to_user sense failed %p\n",
					 skdev->name, __func__, __LINE__,
					 sgp->sbp);
				return -EFAULT;
			}
		}
	}

	if (__copy_to_user(sksgio->argp, sgp, sizeof(sg_io_hdr_t))) {
		pr_debug("%s:%s:%d copy_to_user sg failed %p\n",
			 skdev->name, __func__, __LINE__, sksgio->argp);
		return -EFAULT;
	}

	return 0;
}

static int skd_sg_io_release_skspcl(struct skd_device *skdev,
				    struct skd_sg_io *sksgio)
{
	struct skd_special_context *skspcl = sksgio->skspcl;

	if (skspcl != NULL) {
		ulong flags;

		sksgio->skspcl = NULL;

		spin_lock_irqsave(&skdev->lock, flags);
		skd_release_special(skdev, skspcl);
		spin_unlock_irqrestore(&skdev->lock, flags);
	}

	return 0;
}

/*
 *****************************************************************************
 * INTERNAL REQUESTS -- generated by driver itself
 *****************************************************************************
 */

static int skd_format_internal_skspcl(struct skd_device *skdev)
{
	struct skd_special_context *skspcl = &skdev->internal_skspcl;
	struct fit_sg_descriptor *sgd = &skspcl->req.sksg_list[0];
	struct fit_msg_hdr *fmh;
	uint64_t dma_address;
	struct skd_scsi_request *scsi;

	fmh = (struct fit_msg_hdr *)&skspcl->msg_buf[0];
	fmh->protocol_id = FIT_PROTOCOL_ID_SOFIT;
	fmh->num_protocol_cmds_coalesced = 1;

	scsi = (struct skd_scsi_request *)&skspcl->msg_buf[64];
	memset(scsi, 0, sizeof(*scsi));
	dma_address = skspcl->req.sksg_dma_address;
	scsi->hdr.sg_list_dma_address = cpu_to_be64(dma_address);
	sgd->control = FIT_SGD_CONTROL_LAST;
	sgd->byte_count = 0;
	sgd->host_side_addr = skspcl->db_dma_address;
	sgd->dev_side_addr = 0;
	sgd->next_desc_ptr = 0LL;

	return 1;
}

#define WR_BUF_SIZE SKD_N_INTERNAL_BYTES

static void skd_send_internal_skspcl(struct skd_device *skdev,
				     struct skd_special_context *skspcl,
				     u8 opcode)
{
	struct fit_sg_descriptor *sgd = &skspcl->req.sksg_list[0];
	struct skd_scsi_request *scsi;
	unsigned char *buf = skspcl->data_buf;
	int i;

	if (skspcl->req.state != SKD_REQ_STATE_IDLE)
		/*
		 * A refresh is already in progress.
		 * Just wait for it to finish.
		 */
		return;

	SKD_ASSERT((skspcl->req.id & SKD_ID_INCR) == 0);
	skspcl->req.state = SKD_REQ_STATE_BUSY;
	skspcl->req.id += SKD_ID_INCR;

	scsi = (struct skd_scsi_request *)&skspcl->msg_buf[64];
	scsi->hdr.tag = skspcl->req.id;

	memset(scsi->cdb, 0, sizeof(scsi->cdb));

	switch (opcode) {
	case TEST_UNIT_READY:
		scsi->cdb[0] = TEST_UNIT_READY;
		sgd->byte_count = 0;
		scsi->hdr.sg_list_len_bytes = 0;
		break;

	case READ_CAPACITY:
		scsi->cdb[0] = READ_CAPACITY;
		sgd->byte_count = SKD_N_READ_CAP_BYTES;
		scsi->hdr.sg_list_len_bytes = cpu_to_be32(sgd->byte_count);
		break;

	case INQUIRY:
		scsi->cdb[0] = INQUIRY;
		scsi->cdb[1] = 0x01;    /* evpd */
		scsi->cdb[2] = 0x80;    /* serial number page */
		scsi->cdb[4] = 0x10;
		sgd->byte_count = 16;
		scsi->hdr.sg_list_len_bytes = cpu_to_be32(sgd->byte_count);
		break;

	case SYNCHRONIZE_CACHE:
		scsi->cdb[0] = SYNCHRONIZE_CACHE;
		sgd->byte_count = 0;
		scsi->hdr.sg_list_len_bytes = 0;
		break;

	case WRITE_BUFFER:
		scsi->cdb[0] = WRITE_BUFFER;
		scsi->cdb[1] = 0x02;
		scsi->cdb[7] = (WR_BUF_SIZE & 0xFF00) >> 8;
		scsi->cdb[8] = WR_BUF_SIZE & 0xFF;
		sgd->byte_count = WR_BUF_SIZE;
		scsi->hdr.sg_list_len_bytes = cpu_to_be32(sgd->byte_count);
		/* fill incrementing byte pattern */
		for (i = 0; i < sgd->byte_count; i++)
			buf[i] = i & 0xFF;
		break;

	case READ_BUFFER:
		scsi->cdb[0] = READ_BUFFER;
		scsi->cdb[1] = 0x02;
		scsi->cdb[7] = (WR_BUF_SIZE & 0xFF00) >> 8;
		scsi->cdb[8] = WR_BUF_SIZE & 0xFF;
		sgd->byte_count = WR_BUF_SIZE;
		scsi->hdr.sg_list_len_bytes = cpu_to_be32(sgd->byte_count);
		memset(skspcl->data_buf, 0, sgd->byte_count);
		break;

	default:
		SKD_ASSERT("Don't know what to send");
		return;

	}
	skd_send_special_fitmsg(skdev, skspcl);
}

static void skd_refresh_device_data(struct skd_device *skdev)
{
	struct skd_special_context *skspcl = &skdev->internal_skspcl;

	skd_send_internal_skspcl(skdev, skspcl, TEST_UNIT_READY);
}

static int skd_chk_read_buf(struct skd_device *skdev,
			    struct skd_special_context *skspcl)
{
	unsigned char *buf = skspcl->data_buf;
	int i;

	/* check for incrementing byte pattern */
	for (i = 0; i < WR_BUF_SIZE; i++)
		if (buf[i] != (i & 0xFF))
			return 1;

	return 0;
}

static void skd_log_check_status(struct skd_device *skdev, u8 status, u8 key,
				 u8 code, u8 qual, u8 fruc)
{
	/* If the check condition is of special interest, log a message */
	if ((status == SAM_STAT_CHECK_CONDITION) && (key == 0x02)
	    && (code == 0x04) && (qual == 0x06)) {
		pr_err("(%s): *** LOST_WRITE_DATA ERROR *** key/asc/"
		       "ascq/fruc %02x/%02x/%02x/%02x\n",
		       skd_name(skdev), key, code, qual, fruc);
	}
}

static void skd_complete_internal(struct skd_device *skdev,
				  volatile struct fit_completion_entry_v1
				  *skcomp,
				  volatile struct fit_comp_error_info *skerr,
				  struct skd_special_context *skspcl)
{
	u8 *buf = skspcl->data_buf;
	u8 status;
	int i;
	struct skd_scsi_request *scsi =
		(struct skd_scsi_request *)&skspcl->msg_buf[64];

	SKD_ASSERT(skspcl == &skdev->internal_skspcl);

	pr_debug("%s:%s:%d complete internal %x\n",
		 skdev->name, __func__, __LINE__, scsi->cdb[0]);

	skspcl->req.completion = *skcomp;
	skspcl->req.state = SKD_REQ_STATE_IDLE;
	skspcl->req.id += SKD_ID_INCR;

	status = skspcl->req.completion.status;

	skd_log_check_status(skdev, status, skerr->key, skerr->code,
			     skerr->qual, skerr->fruc);

	switch (scsi->cdb[0]) {
	case TEST_UNIT_READY:
		if (status == SAM_STAT_GOOD)
			skd_send_internal_skspcl(skdev, skspcl, WRITE_BUFFER);
		else if ((status == SAM_STAT_CHECK_CONDITION) &&
			 (skerr->key == MEDIUM_ERROR))
			skd_send_internal_skspcl(skdev, skspcl, WRITE_BUFFER);
		else {
			if (skdev->state == SKD_DRVR_STATE_STOPPING) {
				pr_debug("%s:%s:%d TUR failed, don't send anymore state 0x%x\n",
					 skdev->name, __func__, __LINE__,
					 skdev->state);
				return;
			}
			pr_debug("%s:%s:%d **** TUR failed, retry skerr\n",
				 skdev->name, __func__, __LINE__);
			skd_send_internal_skspcl(skdev, skspcl, 0x00);
		}
		break;

	case WRITE_BUFFER:
		if (status == SAM_STAT_GOOD)
			skd_send_internal_skspcl(skdev, skspcl, READ_BUFFER);
		else {
			if (skdev->state == SKD_DRVR_STATE_STOPPING) {
				pr_debug("%s:%s:%d write buffer failed, don't send anymore state 0x%x\n",
					 skdev->name, __func__, __LINE__,
					 skdev->state);
				return;
			}
			pr_debug("%s:%s:%d **** write buffer failed, retry skerr\n",
				 skdev->name, __func__, __LINE__);
			skd_send_internal_skspcl(skdev, skspcl, 0x00);
		}
		break;

	case READ_BUFFER:
		if (status == SAM_STAT_GOOD) {
			if (skd_chk_read_buf(skdev, skspcl) == 0)
				skd_send_internal_skspcl(skdev, skspcl,
							 READ_CAPACITY);
			else {
				pr_err(
				       "(%s):*** W/R Buffer mismatch %d ***\n",
				       skd_name(skdev), skdev->connect_retries);
				if (skdev->connect_retries <
				    SKD_MAX_CONNECT_RETRIES) {
					skdev->connect_retries++;
					skd_soft_reset(skdev);
				} else {
					pr_err(
					       "(%s): W/R Buffer Connect Error\n",
					       skd_name(skdev));
					return;
				}
			}

		} else {
			if (skdev->state == SKD_DRVR_STATE_STOPPING) {
				pr_debug("%s:%s:%d "
					 "read buffer failed, don't send anymore state 0x%x\n",
					 skdev->name, __func__, __LINE__,
					 skdev->state);
				return;
			}
			pr_debug("%s:%s:%d "
				 "**** read buffer failed, retry skerr\n",
				 skdev->name, __func__, __LINE__);
			skd_send_internal_skspcl(skdev, skspcl, 0x00);
		}
		break;

	case READ_CAPACITY:
		skdev->read_cap_is_valid = 0;
		if (status == SAM_STAT_GOOD) {
			skdev->read_cap_last_lba =
				(buf[0] << 24) | (buf[1] << 16) |
				(buf[2] << 8) | buf[3];
			skdev->read_cap_blocksize =
				(buf[4] << 24) | (buf[5] << 16) |
				(buf[6] << 8) | buf[7];

			pr_debug("%s:%s:%d last lba %d, bs %d\n",
				 skdev->name, __func__, __LINE__,
				 skdev->read_cap_last_lba,
				 skdev->read_cap_blocksize);

			set_capacity(skdev->disk, skdev->read_cap_last_lba + 1);

			skdev->read_cap_is_valid = 1;

			skd_send_internal_skspcl(skdev, skspcl, INQUIRY);
		} else if ((status == SAM_STAT_CHECK_CONDITION) &&
			   (skerr->key == MEDIUM_ERROR)) {
			skdev->read_cap_last_lba = ~0;
			set_capacity(skdev->disk, skdev->read_cap_last_lba + 1);
			pr_debug("%s:%s:%d "
				 "**** MEDIUM ERROR caused READCAP to fail, ignore failure and continue to inquiry\n",
				 skdev->name, __func__, __LINE__);
			skd_send_internal_skspcl(skdev, skspcl, INQUIRY);
		} else {
			pr_debug("%s:%s:%d **** READCAP failed, retry TUR\n",
				 skdev->name, __func__, __LINE__);
			skd_send_internal_skspcl(skdev, skspcl,
						 TEST_UNIT_READY);
		}
		break;

	case INQUIRY:
		skdev->inquiry_is_valid = 0;
		if (status == SAM_STAT_GOOD) {
			skdev->inquiry_is_valid = 1;

			for (i = 0; i < 12; i++)
				skdev->inq_serial_num[i] = buf[i + 4];
			skdev->inq_serial_num[12] = 0;
		}

		if (skd_unquiesce_dev(skdev) < 0)
			pr_debug("%s:%s:%d **** failed, to ONLINE device\n",
				 skdev->name, __func__, __LINE__);
		 /* connection is complete */
		skdev->connect_retries = 0;
		break;

	case SYNCHRONIZE_CACHE:
		if (status == SAM_STAT_GOOD)
			skdev->sync_done = 1;
		else
			skdev->sync_done = -1;
		wake_up_interruptible(&skdev->waitq);
		break;

	default:
		SKD_ASSERT("we didn't send this");
	}
}

/*
 *****************************************************************************
 * FIT MESSAGES
 *****************************************************************************
 */

static void skd_send_fitmsg(struct skd_device *skdev,
			    struct skd_fitmsg_context *skmsg)
{
	u64 qcmd;
	struct fit_msg_hdr *fmh;

	pr_debug("%s:%s:%d dma address 0x%llx, busy=%d\n",
		 skdev->name, __func__, __LINE__,
		 skmsg->mb_dma_address, skdev->in_flight);
	pr_debug("%s:%s:%d msg_buf 0x%p, offset %x\n",
		 skdev->name, __func__, __LINE__,
		 skmsg->msg_buf, skmsg->offset);

	qcmd = skmsg->mb_dma_address;
	qcmd |= FIT_QCMD_QID_NORMAL;

	fmh = (struct fit_msg_hdr *)skmsg->msg_buf;
	skmsg->outstanding = fmh->num_protocol_cmds_coalesced;

	if (unlikely(skdev->dbg_level > 1)) {
		u8 *bp = (u8 *)skmsg->msg_buf;
		int i;
		for (i = 0; i < skmsg->length; i += 8) {
			pr_debug("%s:%s:%d msg[%2d] %02x %02x %02x %02x "
				 "%02x %02x %02x %02x\n",
				 skdev->name, __func__, __LINE__,
				 i, bp[i + 0], bp[i + 1], bp[i + 2],
				 bp[i + 3], bp[i + 4], bp[i + 5],
				 bp[i + 6], bp[i + 7]);
			if (i == 0)
				i = 64 - 8;
		}
	}

	if (skmsg->length > 256)
		qcmd |= FIT_QCMD_MSGSIZE_512;
	else if (skmsg->length > 128)
		qcmd |= FIT_QCMD_MSGSIZE_256;
	else if (skmsg->length > 64)
		qcmd |= FIT_QCMD_MSGSIZE_128;
	else
		/*
		 * This makes no sense because the FIT msg header is
		 * 64 bytes. If the msg is only 64 bytes long it has
		 * no payload.
		 */
		qcmd |= FIT_QCMD_MSGSIZE_64;

	SKD_WRITEQ(skdev, qcmd, FIT_Q_COMMAND);

}

static void skd_send_special_fitmsg(struct skd_device *skdev,
				    struct skd_special_context *skspcl)
{
	u64 qcmd;

	if (unlikely(skdev->dbg_level > 1)) {
		u8 *bp = (u8 *)skspcl->msg_buf;
		int i;

		for (i = 0; i < SKD_N_SPECIAL_FITMSG_BYTES; i += 8) {
			pr_debug("%s:%s:%d  spcl[%2d] %02x %02x %02x %02x  "
				 "%02x %02x %02x %02x\n",
				 skdev->name, __func__, __LINE__, i,
				 bp[i + 0], bp[i + 1], bp[i + 2], bp[i + 3],
				 bp[i + 4], bp[i + 5], bp[i + 6], bp[i + 7]);
			if (i == 0)
				i = 64 - 8;
		}

		pr_debug("%s:%s:%d skspcl=%p id=%04x sksg_list=%p sksg_dma=%llx\n",
			 skdev->name, __func__, __LINE__,
			 skspcl, skspcl->req.id, skspcl->req.sksg_list,
			 skspcl->req.sksg_dma_address);
		for (i = 0; i < skspcl->req.n_sg; i++) {
			struct fit_sg_descriptor *sgd =
				&skspcl->req.sksg_list[i];

			pr_debug("%s:%s:%d   sg[%d] count=%u ctrl=0x%x "
				 "addr=0x%llx next=0x%llx\n",
				 skdev->name, __func__, __LINE__,
				 i, sgd->byte_count, sgd->control,
				 sgd->host_side_addr, sgd->next_desc_ptr);
		}
	}

	/*
	 * Special FIT msgs are always 128 bytes: a 64-byte FIT hdr
	 * and one 64-byte SSDI command.
	 */
	qcmd = skspcl->mb_dma_address;
	qcmd |= FIT_QCMD_QID_NORMAL + FIT_QCMD_MSGSIZE_128;

	SKD_WRITEQ(skdev, qcmd, FIT_Q_COMMAND);
}

/*
 *****************************************************************************
 * COMPLETION QUEUE
 *****************************************************************************
 */

static void skd_complete_other(struct skd_device *skdev,
			       volatile struct fit_completion_entry_v1 *skcomp,
			       volatile struct fit_comp_error_info *skerr);

struct sns_info {
	u8 type;
	u8 stat;
	u8 key;
	u8 asc;
	u8 ascq;
	u8 mask;
	enum skd_check_status_action action;
};

static struct sns_info skd_chkstat_table[] = {
	/* Good */
	{ 0x70, 0x02, RECOVERED_ERROR, 0,    0,	   0x1c,
	  SKD_CHECK_STATUS_REPORT_GOOD },

	/* Smart alerts */
	{ 0x70, 0x02, NO_SENSE,	       0x0B, 0x00, 0x1E,	/* warnings */
	  SKD_CHECK_STATUS_REPORT_SMART_ALERT },
	{ 0x70, 0x02, NO_SENSE,	       0x5D, 0x00, 0x1E,	/* thresholds */
	  SKD_CHECK_STATUS_REPORT_SMART_ALERT },
	{ 0x70, 0x02, RECOVERED_ERROR, 0x0B, 0x01, 0x1F,        /* temperature over trigger */
	  SKD_CHECK_STATUS_REPORT_SMART_ALERT },

	/* Retry (with limits) */
	{ 0x70, 0x02, 0x0B,	       0,    0,	   0x1C,        /* This one is for DMA ERROR */
	  SKD_CHECK_STATUS_REQUEUE_REQUEST },
	{ 0x70, 0x02, 0x06,	       0x0B, 0x00, 0x1E,        /* warnings */
	  SKD_CHECK_STATUS_REQUEUE_REQUEST },
	{ 0x70, 0x02, 0x06,	       0x5D, 0x00, 0x1E,        /* thresholds */
	  SKD_CHECK_STATUS_REQUEUE_REQUEST },
	{ 0x70, 0x02, 0x06,	       0x80, 0x30, 0x1F,        /* backup power */
	  SKD_CHECK_STATUS_REQUEUE_REQUEST },

	/* Busy (or about to be) */
	{ 0x70, 0x02, 0x06,	       0x3f, 0x01, 0x1F, /* fw changed */
	  SKD_CHECK_STATUS_BUSY_IMMINENT },
};

/*
 * Look up status and sense data to decide how to handle the error
 * from the device.
 * mask says which fields must match e.g., mask=0x18 means check
 * type and stat, ignore key, asc, ascq.
 */

static enum skd_check_status_action
skd_check_status(struct skd_device *skdev,
		 u8 cmp_status, volatile struct fit_comp_error_info *skerr)
{
	int i, n;

	pr_err("(%s): key/asc/ascq/fruc %02x/%02x/%02x/%02x\n",
	       skd_name(skdev), skerr->key, skerr->code, skerr->qual,
	       skerr->fruc);

	pr_debug("%s:%s:%d stat: t=%02x stat=%02x k=%02x c=%02x q=%02x fruc=%02x\n",
		 skdev->name, __func__, __LINE__, skerr->type, cmp_status,
		 skerr->key, skerr->code, skerr->qual, skerr->fruc);

	/* Does the info match an entry in the good category? */
	n = sizeof(skd_chkstat_table) / sizeof(skd_chkstat_table[0]);
	for (i = 0; i < n; i++) {
		struct sns_info *sns = &skd_chkstat_table[i];

		if (sns->mask & 0x10)
			if (skerr->type != sns->type)
				continue;

		if (sns->mask & 0x08)
			if (cmp_status != sns->stat)
				continue;

		if (sns->mask & 0x04)
			if (skerr->key != sns->key)
				continue;

		if (sns->mask & 0x02)
			if (skerr->code != sns->asc)
				continue;

		if (sns->mask & 0x01)
			if (skerr->qual != sns->ascq)
				continue;

		if (sns->action == SKD_CHECK_STATUS_REPORT_SMART_ALERT) {
			pr_err("(%s): SMART Alert: sense key/asc/ascq "
			       "%02x/%02x/%02x\n",
			       skd_name(skdev), skerr->key,
			       skerr->code, skerr->qual);
		}
		return sns->action;
	}

	/* No other match, so nonzero status means error,
	 * zero status means good
	 */
	if (cmp_status) {
		pr_debug("%s:%s:%d status check: error\n",
			 skdev->name, __func__, __LINE__);
		return SKD_CHECK_STATUS_REPORT_ERROR;
	}

	pr_debug("%s:%s:%d status check good default\n",
		 skdev->name, __func__, __LINE__);
	return SKD_CHECK_STATUS_REPORT_GOOD;
}

static void skd_resolve_req_exception(struct skd_device *skdev,
				      struct skd_request_context *skreq)
{
	u8 cmp_status = skreq->completion.status;

	switch (skd_check_status(skdev, cmp_status, &skreq->err_info)) {
	case SKD_CHECK_STATUS_REPORT_GOOD:
	case SKD_CHECK_STATUS_REPORT_SMART_ALERT:
		skd_end_request(skdev, skreq, 0);
		break;

	case SKD_CHECK_STATUS_BUSY_IMMINENT:
		skd_log_skreq(skdev, skreq, "retry(busy)");
		blk_requeue_request(skdev->queue, skreq->req);
		pr_info("(%s) drive BUSY imminent\n", skd_name(skdev));
		skdev->state = SKD_DRVR_STATE_BUSY_IMMINENT;
		skdev->timer_countdown = SKD_TIMER_MINUTES(20);
		skd_quiesce_dev(skdev);
		break;

	case SKD_CHECK_STATUS_REQUEUE_REQUEST:
		if ((unsigned long) ++skreq->req->special < SKD_MAX_RETRIES) {
			skd_log_skreq(skdev, skreq, "retry");
			blk_requeue_request(skdev->queue, skreq->req);
			break;
		}
	/* fall through to report error */

	case SKD_CHECK_STATUS_REPORT_ERROR:
	default:
		skd_end_request(skdev, skreq, -EIO);
		break;
	}
}

/* assume spinlock is already held */
static void skd_release_skreq(struct skd_device *skdev,
			      struct skd_request_context *skreq)
{
	u32 msg_slot;
	struct skd_fitmsg_context *skmsg;

	u32 timo_slot;

	/*
	 * Reclaim the FIT msg buffer if this is
	 * the first of the requests it carried to
	 * be completed. The FIT msg buffer used to
	 * send this request cannot be reused until
	 * we are sure the s1120 card has copied
	 * it to its memory. The FIT msg might have
	 * contained several requests. As soon as
	 * any of them are completed we know that
	 * the entire FIT msg was transferred.
	 * Only the first completed request will
	 * match the FIT msg buffer id. The FIT
	 * msg buffer id is immediately updated.
	 * When subsequent requests complete the FIT
	 * msg buffer id won't match, so we know
	 * quite cheaply that it is already done.
	 */
	msg_slot = skreq->fitmsg_id & SKD_ID_SLOT_MASK;
	SKD_ASSERT(msg_slot < skdev->num_fitmsg_context);

	skmsg = &skdev->skmsg_table[msg_slot];
	if (skmsg->id == skreq->fitmsg_id) {
		SKD_ASSERT(skmsg->state == SKD_MSG_STATE_BUSY);
		SKD_ASSERT(skmsg->outstanding > 0);
		skmsg->outstanding--;
		if (skmsg->outstanding == 0) {
			skmsg->state = SKD_MSG_STATE_IDLE;
			skmsg->id += SKD_ID_INCR;
			skmsg->next = skdev->skmsg_free_list;
			skdev->skmsg_free_list = skmsg;
		}
	}

	/*
	 * Decrease the number of active requests.
	 * Also decrements the count in the timeout slot.
	 */
	SKD_ASSERT(skdev->in_flight > 0);
	skdev->in_flight -= 1;

	timo_slot = skreq->timeout_stamp & SKD_TIMEOUT_SLOT_MASK;
	SKD_ASSERT(skdev->timeout_slot[timo_slot] > 0);
	skdev->timeout_slot[timo_slot] -= 1;

	/*
	 * Reset backpointer
	 */
	skreq->req = NULL;

	/*
	 * Reclaim the skd_request_context
	 */
	skreq->state = SKD_REQ_STATE_IDLE;
	skreq->id += SKD_ID_INCR;
	skreq->next = skdev->skreq_free_list;
	skdev->skreq_free_list = skreq;
}

#define DRIVER_INQ_EVPD_PAGE_CODE   0xDA

static void skd_do_inq_page_00(struct skd_device *skdev,
			       volatile struct fit_completion_entry_v1 *skcomp,
			       volatile struct fit_comp_error_info *skerr,
			       uint8_t *cdb, uint8_t *buf)
{
	uint16_t insert_pt, max_bytes, drive_pages, drive_bytes, new_size;

	/* Caller requested "supported pages".  The driver needs to insert
	 * its page.
	 */
	pr_debug("%s:%s:%d skd_do_driver_inquiry: modify supported pages.\n",
		 skdev->name, __func__, __LINE__);

	/* If the device rejected the request because the CDB was
	 * improperly formed, then just leave.
	 */
	if (skcomp->status == SAM_STAT_CHECK_CONDITION &&
	    skerr->key == ILLEGAL_REQUEST && skerr->code == 0x24)
		return;

	/* Get the amount of space the caller allocated */
	max_bytes = (cdb[3] << 8) | cdb[4];

	/* Get the number of pages actually returned by the device */
	drive_pages = (buf[2] << 8) | buf[3];
	drive_bytes = drive_pages + 4;
	new_size = drive_pages + 1;

	/* Supported pages must be in numerical order, so find where
	 * the driver page needs to be inserted into the list of
	 * pages returned by the device.
	 */
	for (insert_pt = 4; insert_pt < drive_bytes; insert_pt++) {
		if (buf[insert_pt] == DRIVER_INQ_EVPD_PAGE_CODE)
			return; /* Device using this page code. abort */
		else if (buf[insert_pt] > DRIVER_INQ_EVPD_PAGE_CODE)
			break;
	}

	if (insert_pt < max_bytes) {
		uint16_t u;

		/* Shift everything up one byte to make room. */
		for (u = new_size + 3; u > insert_pt; u--)
			buf[u] = buf[u - 1];
		buf[insert_pt] = DRIVER_INQ_EVPD_PAGE_CODE;

		/* SCSI byte order increment of num_returned_bytes by 1 */
		skcomp->num_returned_bytes =
			be32_to_cpu(skcomp->num_returned_bytes) + 1;
		skcomp->num_returned_bytes =
			be32_to_cpu(skcomp->num_returned_bytes);
	}

	/* update page length field to reflect the driver's page too */
	buf[2] = (uint8_t)((new_size >> 8) & 0xFF);
	buf[3] = (uint8_t)((new_size >> 0) & 0xFF);
}

static void skd_get_link_info(struct pci_dev *pdev, u8 *speed, u8 *width)
{
	int pcie_reg;
	u16 pci_bus_speed;
	u8 pci_lanes;

	pcie_reg = pci_find_capability(pdev, PCI_CAP_ID_EXP);
	if (pcie_reg) {
		u16 linksta;
		pci_read_config_word(pdev, pcie_reg + PCI_EXP_LNKSTA, &linksta);

		pci_bus_speed = linksta & 0xF;
		pci_lanes = (linksta & 0x3F0) >> 4;
	} else {
		*speed = STEC_LINK_UNKNOWN;
		*width = 0xFF;
		return;
	}

	switch (pci_bus_speed) {
	case 1:
		*speed = STEC_LINK_2_5GTS;
		break;
	case 2:
		*speed = STEC_LINK_5GTS;
		break;
	case 3:
		*speed = STEC_LINK_8GTS;
		break;
	default:
		*speed = STEC_LINK_UNKNOWN;
		break;
	}

	if (pci_lanes <= 0x20)
		*width = pci_lanes;
	else
		*width = 0xFF;
}

static void skd_do_inq_page_da(struct skd_device *skdev,
			       volatile struct fit_completion_entry_v1 *skcomp,
			       volatile struct fit_comp_error_info *skerr,
			       uint8_t *cdb, uint8_t *buf)
{
	struct pci_dev *pdev = skdev->pdev;
	unsigned max_bytes;
	struct driver_inquiry_data inq;
	u16 val;

	pr_debug("%s:%s:%d skd_do_driver_inquiry: return driver page\n",
		 skdev->name, __func__, __LINE__);

	memset(&inq, 0, sizeof(inq));

	inq.page_code = DRIVER_INQ_EVPD_PAGE_CODE;

	skd_get_link_info(pdev, &inq.pcie_link_speed, &inq.pcie_link_lanes);
	inq.pcie_bus_number = cpu_to_be16(pdev->bus->number);
	inq.pcie_device_number = PCI_SLOT(pdev->devfn);
	inq.pcie_function_number = PCI_FUNC(pdev->devfn);

	pci_read_config_word(pdev, PCI_VENDOR_ID, &val);
	inq.pcie_vendor_id = cpu_to_be16(val);

	pci_read_config_word(pdev, PCI_DEVICE_ID, &val);
	inq.pcie_device_id = cpu_to_be16(val);

	pci_read_config_word(pdev, PCI_SUBSYSTEM_VENDOR_ID, &val);
	inq.pcie_subsystem_vendor_id = cpu_to_be16(val);

	pci_read_config_word(pdev, PCI_SUBSYSTEM_ID, &val);
	inq.pcie_subsystem_device_id = cpu_to_be16(val);

	/* Driver version, fixed lenth, padded with spaces on the right */
	inq.driver_version_length = sizeof(inq.driver_version);
	memset(&inq.driver_version, ' ', sizeof(inq.driver_version));
	memcpy(inq.driver_version, DRV_VER_COMPL,
	       min(sizeof(inq.driver_version), strlen(DRV_VER_COMPL)));

	inq.page_length = cpu_to_be16((sizeof(inq) - 4));

	/* Clear the error set by the device */
	skcomp->status = SAM_STAT_GOOD;
	memset((void *)skerr, 0, sizeof(*skerr));

	/* copy response into output buffer */
	max_bytes = (cdb[3] << 8) | cdb[4];
	memcpy(buf, &inq, min_t(unsigned, max_bytes, sizeof(inq)));

	skcomp->num_returned_bytes =
		be32_to_cpu(min_t(uint16_t, max_bytes, sizeof(inq)));
}

static void skd_do_driver_inq(struct skd_device *skdev,
			      volatile struct fit_completion_entry_v1 *skcomp,
			      volatile struct fit_comp_error_info *skerr,
			      uint8_t *cdb, uint8_t *buf)
{
	if (!buf)
		return;
	else if (cdb[0] != INQUIRY)
		return;         /* Not an INQUIRY */
	else if ((cdb[1] & 1) == 0)
		return;         /* EVPD not set */
	else if (cdb[2] == 0)
		/* Need to add driver's page to supported pages list */
		skd_do_inq_page_00(skdev, skcomp, skerr, cdb, buf);
	else if (cdb[2] == DRIVER_INQ_EVPD_PAGE_CODE)
		/* Caller requested driver's page */
		skd_do_inq_page_da(skdev, skcomp, skerr, cdb, buf);
}

static unsigned char *skd_sg_1st_page_ptr(struct scatterlist *sg)
{
	if (!sg)
		return NULL;
	if (!sg_page(sg))
		return NULL;
	return sg_virt(sg);
}

static void skd_process_scsi_inq(struct skd_device *skdev,
				 volatile struct fit_completion_entry_v1
				 *skcomp,
				 volatile struct fit_comp_error_info *skerr,
				 struct skd_special_context *skspcl)
{
	uint8_t *buf;
	struct fit_msg_hdr *fmh = (struct fit_msg_hdr *)skspcl->msg_buf;
	struct skd_scsi_request *scsi_req = (struct skd_scsi_request *)&fmh[1];

	dma_sync_sg_for_cpu(skdev->class_dev, skspcl->req.sg, skspcl->req.n_sg,
			    skspcl->req.sg_data_dir);
	buf = skd_sg_1st_page_ptr(skspcl->req.sg);

	if (buf)
		skd_do_driver_inq(skdev, skcomp, skerr, scsi_req->cdb, buf);
}


static int skd_isr_completion_posted(struct skd_device *skdev,
					int limit, int *enqueued)
{
	volatile struct fit_completion_entry_v1 *skcmp = NULL;
	volatile struct fit_comp_error_info *skerr;
	u16 req_id;
	u32 req_slot;
	struct skd_request_context *skreq;
	u16 cmp_cntxt = 0;
	u8 cmp_status = 0;
	u8 cmp_cycle = 0;
	u32 cmp_bytes = 0;
	int rc = 0;
	int processed = 0;

	for (;; ) {
		SKD_ASSERT(skdev->skcomp_ix < SKD_N_COMPLETION_ENTRY);

		skcmp = &skdev->skcomp_table[skdev->skcomp_ix];
		cmp_cycle = skcmp->cycle;
		cmp_cntxt = skcmp->tag;
		cmp_status = skcmp->status;
		cmp_bytes = be32_to_cpu(skcmp->num_returned_bytes);

		skerr = &skdev->skerr_table[skdev->skcomp_ix];

		pr_debug("%s:%s:%d "
			 "cycle=%d ix=%d got cycle=%d cmdctxt=0x%x stat=%d "
			 "busy=%d rbytes=0x%x proto=%d\n",
			 skdev->name, __func__, __LINE__, skdev->skcomp_cycle,
			 skdev->skcomp_ix, cmp_cycle, cmp_cntxt, cmp_status,
			 skdev->in_flight, cmp_bytes, skdev->proto_ver);

		if (cmp_cycle != skdev->skcomp_cycle) {
			pr_debug("%s:%s:%d end of completions\n",
				 skdev->name, __func__, __LINE__);
			break;
		}
		/*
		 * Update the completion queue head index and possibly
		 * the completion cycle count. 8-bit wrap-around.
		 */
		skdev->skcomp_ix++;
		if (skdev->skcomp_ix >= SKD_N_COMPLETION_ENTRY) {
			skdev->skcomp_ix = 0;
			skdev->skcomp_cycle++;
		}

		/*
		 * The command context is a unique 32-bit ID. The low order
		 * bits help locate the request. The request is usually a
		 * r/w request (see skd_start() above) or a special request.
		 */
		req_id = cmp_cntxt;
		req_slot = req_id & SKD_ID_SLOT_AND_TABLE_MASK;

		/* Is this other than a r/w request? */
		if (req_slot >= skdev->num_req_context) {
			/*
			 * This is not a completion for a r/w request.
			 */
			skd_complete_other(skdev, skcmp, skerr);
			continue;
		}

		skreq = &skdev->skreq_table[req_slot];

		/*
		 * Make sure the request ID for the slot matches.
		 */
		if (skreq->id != req_id) {
			pr_debug("%s:%s:%d mismatch comp_id=0x%x req_id=0x%x\n",
				 skdev->name, __func__, __LINE__,
				 req_id, skreq->id);
			{
				u16 new_id = cmp_cntxt;
				pr_err("(%s): Completion mismatch "
				       "comp_id=0x%04x skreq=0x%04x new=0x%04x\n",
				       skd_name(skdev), req_id,
				       skreq->id, new_id);

				continue;
			}
		}

		SKD_ASSERT(skreq->state == SKD_REQ_STATE_BUSY);

		if (skreq->state == SKD_REQ_STATE_ABORTED) {
			pr_debug("%s:%s:%d reclaim req %p id=%04x\n",
				 skdev->name, __func__, __LINE__,
				 skreq, skreq->id);
			/* a previously timed out command can
			 * now be cleaned up */
			skd_release_skreq(skdev, skreq);
			continue;
		}

		skreq->completion = *skcmp;
		if (unlikely(cmp_status == SAM_STAT_CHECK_CONDITION)) {
			skreq->err_info = *skerr;
			skd_log_check_status(skdev, cmp_status, skerr->key,
					     skerr->code, skerr->qual,
					     skerr->fruc);
		}
		/* Release DMA resources for the request. */
		if (skreq->n_sg > 0)
			skd_postop_sg_list(skdev, skreq);

		if (!skreq->req) {
			pr_debug("%s:%s:%d NULL backptr skdreq %p, "
				 "req=0x%x req_id=0x%x\n",
				 skdev->name, __func__, __LINE__,
				 skreq, skreq->id, req_id);
		} else {
			/*
			 * Capture the outcome and post it back to the
			 * native request.
			 */
			if (likely(cmp_status == SAM_STAT_GOOD))
				skd_end_request(skdev, skreq, 0);
			else
				skd_resolve_req_exception(skdev, skreq);
		}

		/*
		 * Release the skreq, its FIT msg (if one), timeout slot,
		 * and queue depth.
		 */
		skd_release_skreq(skdev, skreq);

		/* skd_isr_comp_limit equal zero means no limit */
		if (limit) {
			if (++processed >= limit) {
				rc = 1;
				break;
			}
		}
	}

	if ((skdev->state == SKD_DRVR_STATE_PAUSING)
		&& (skdev->in_flight) == 0) {
		skdev->state = SKD_DRVR_STATE_PAUSED;
		wake_up_interruptible(&skdev->waitq);
	}

	return rc;
}

static void skd_complete_other(struct skd_device *skdev,
			       volatile struct fit_completion_entry_v1 *skcomp,
			       volatile struct fit_comp_error_info *skerr)
{
	u32 req_id = 0;
	u32 req_table;
	u32 req_slot;
	struct skd_special_context *skspcl;

	req_id = skcomp->tag;
	req_table = req_id & SKD_ID_TABLE_MASK;
	req_slot = req_id & SKD_ID_SLOT_MASK;

	pr_debug("%s:%s:%d table=0x%x id=0x%x slot=%d\n",
		 skdev->name, __func__, __LINE__,
		 req_table, req_id, req_slot);

	/*
	 * Based on the request id, determine how to dispatch this completion.
	 * This swich/case is finding the good cases and forwarding the
	 * completion entry. Errors are reported below the switch.
	 */
	switch (req_table) {
	case SKD_ID_RW_REQUEST:
		/*
		 * The caller, skd_completion_posted_isr() above,
		 * handles r/w requests. The only way we get here
		 * is if the req_slot is out of bounds.
		 */
		break;

	case SKD_ID_SPECIAL_REQUEST:
		/*
		 * Make sure the req_slot is in bounds and that the id
		 * matches.
		 */
		if (req_slot < skdev->n_special) {
			skspcl = &skdev->skspcl_table[req_slot];
			if (skspcl->req.id == req_id &&
			    skspcl->req.state == SKD_REQ_STATE_BUSY) {
				skd_complete_special(skdev,
						     skcomp, skerr, skspcl);
				return;
			}
		}
		break;

	case SKD_ID_INTERNAL:
		if (req_slot == 0) {
			skspcl = &skdev->internal_skspcl;
			if (skspcl->req.id == req_id &&
			    skspcl->req.state == SKD_REQ_STATE_BUSY) {
				skd_complete_internal(skdev,
						      skcomp, skerr, skspcl);
				return;
			}
		}
		break;

	case SKD_ID_FIT_MSG:
		/*
		 * These id's should never appear in a completion record.
		 */
		break;

	default:
		/*
		 * These id's should never appear anywhere;
		 */
		break;
	}

	/*
	 * If we get here it is a bad or stale id.
	 */
}

static void skd_complete_special(struct skd_device *skdev,
				 volatile struct fit_completion_entry_v1
				 *skcomp,
				 volatile struct fit_comp_error_info *skerr,
				 struct skd_special_context *skspcl)
{
	pr_debug("%s:%s:%d  completing special request %p\n",
		 skdev->name, __func__, __LINE__, skspcl);
	if (skspcl->orphaned) {
		/* Discard orphaned request */
		/* ?: Can this release directly or does it need
		 * to use a worker? */
		pr_debug("%s:%s:%d release orphaned %p\n",
			 skdev->name, __func__, __LINE__, skspcl);
		skd_release_special(skdev, skspcl);
		return;
	}

	skd_process_scsi_inq(skdev, skcomp, skerr, skspcl);

	skspcl->req.state = SKD_REQ_STATE_COMPLETED;
	skspcl->req.completion = *skcomp;
	skspcl->req.err_info = *skerr;

	skd_log_check_status(skdev, skspcl->req.completion.status, skerr->key,
			     skerr->code, skerr->qual, skerr->fruc);

	wake_up_interruptible(&skdev->waitq);
}

/* assume spinlock is already held */
static void skd_release_special(struct skd_device *skdev,
				struct skd_special_context *skspcl)
{
	int i, was_depleted;

	for (i = 0; i < skspcl->req.n_sg; i++) {
		struct page *page = sg_page(&skspcl->req.sg[i]);
		__free_page(page);
	}

	was_depleted = (skdev->skspcl_free_list == NULL);

	skspcl->req.state = SKD_REQ_STATE_IDLE;
	skspcl->req.id += SKD_ID_INCR;
	skspcl->req.next =
		(struct skd_request_context *)skdev->skspcl_free_list;
	skdev->skspcl_free_list = (struct skd_special_context *)skspcl;

	if (was_depleted) {
		pr_debug("%s:%s:%d skspcl was depleted\n",
			 skdev->name, __func__, __LINE__);
		/* Free list was depleted. Their might be waiters. */
		wake_up_interruptible(&skdev->waitq);
	}
}

static void skd_reset_skcomp(struct skd_device *skdev)
{
	u32 nbytes;
	struct fit_completion_entry_v1 *skcomp;

	nbytes = sizeof(*skcomp) * SKD_N_COMPLETION_ENTRY;
	nbytes += sizeof(struct fit_comp_error_info) * SKD_N_COMPLETION_ENTRY;

	memset(skdev->skcomp_table, 0, nbytes);

	skdev->skcomp_ix = 0;
	skdev->skcomp_cycle = 1;
}

/*
 *****************************************************************************
 * INTERRUPTS
 *****************************************************************************
 */
static void skd_completion_worker(struct work_struct *work)
{
	struct skd_device *skdev =
		container_of(work, struct skd_device, completion_worker);
	unsigned long flags;
	int flush_enqueued = 0;

	spin_lock_irqsave(&skdev->lock, flags);

	/*
	 * pass in limit=0, which means no limit..
	 * process everything in compq
	 */
	skd_isr_completion_posted(skdev, 0, &flush_enqueued);
	skd_request_fn(skdev->queue);

	spin_unlock_irqrestore(&skdev->lock, flags);
}

static void skd_isr_msg_from_dev(struct skd_device *skdev);

irqreturn_t
static skd_isr(int irq, void *ptr)
{
	struct skd_device *skdev;
	u32 intstat;
	u32 ack;
	int rc = 0;
	int deferred = 0;
	int flush_enqueued = 0;

	skdev = (struct skd_device *)ptr;
	spin_lock(&skdev->lock);

	for (;; ) {
		intstat = SKD_READL(skdev, FIT_INT_STATUS_HOST);

		ack = FIT_INT_DEF_MASK;
		ack &= intstat;

		pr_debug("%s:%s:%d intstat=0x%x ack=0x%x\n",
			 skdev->name, __func__, __LINE__, intstat, ack);

		/* As long as there is an int pending on device, keep
		 * running loop.  When none, get out, but if we've never
		 * done any processing, call completion handler?
		 */
		if (ack == 0) {
			/* No interrupts on device, but run the completion
			 * processor anyway?
			 */
			if (rc == 0)
				if (likely (skdev->state
					== SKD_DRVR_STATE_ONLINE))
					deferred = 1;
			break;
		}

		rc = IRQ_HANDLED;

		SKD_WRITEL(skdev, ack, FIT_INT_STATUS_HOST);

		if (likely((skdev->state != SKD_DRVR_STATE_LOAD) &&
			   (skdev->state != SKD_DRVR_STATE_STOPPING))) {
			if (intstat & FIT_ISH_COMPLETION_POSTED) {
				/*
				 * If we have already deferred completion
				 * processing, don't bother running it again
				 */
				if (deferred == 0)
					deferred =
						skd_isr_completion_posted(skdev,
						skd_isr_comp_limit, &flush_enqueued);
			}

			if (intstat & FIT_ISH_FW_STATE_CHANGE) {
				skd_isr_fwstate(skdev);
				if (skdev->state == SKD_DRVR_STATE_FAULT ||
				    skdev->state ==
				    SKD_DRVR_STATE_DISAPPEARED) {
					spin_unlock(&skdev->lock);
					return rc;
				}
			}

			if (intstat & FIT_ISH_MSG_FROM_DEV)
				skd_isr_msg_from_dev(skdev);
		}
	}

	if (unlikely(flush_enqueued))
		skd_request_fn(skdev->queue);

	if (deferred)
		schedule_work(&skdev->completion_worker);
	else if (!flush_enqueued)
		skd_request_fn(skdev->queue);

	spin_unlock(&skdev->lock);

	return rc;
}

static void skd_drive_fault(struct skd_device *skdev)
{
	skdev->state = SKD_DRVR_STATE_FAULT;
	pr_err("(%s): Drive FAULT\n", skd_name(skdev));
}

static void skd_drive_disappeared(struct skd_device *skdev)
{
	skdev->state = SKD_DRVR_STATE_DISAPPEARED;
	pr_err("(%s): Drive DISAPPEARED\n", skd_name(skdev));
}

static void skd_isr_fwstate(struct skd_device *skdev)
{
	u32 sense;
	u32 state;
	u32 mtd;
	int prev_driver_state = skdev->state;

	sense = SKD_READL(skdev, FIT_STATUS);
	state = sense & FIT_SR_DRIVE_STATE_MASK;

	pr_err("(%s): s1120 state %s(%d)=>%s(%d)\n",
	       skd_name(skdev),
	       skd_drive_state_to_str(skdev->drive_state), skdev->drive_state,
	       skd_drive_state_to_str(state), state);

	skdev->drive_state = state;

	switch (skdev->drive_state) {
	case FIT_SR_DRIVE_INIT:
		if (skdev->state == SKD_DRVR_STATE_PROTOCOL_MISMATCH) {
			skd_disable_interrupts(skdev);
			break;
		}
		if (skdev->state == SKD_DRVR_STATE_RESTARTING)
			skd_recover_requests(skdev, 0);
		if (skdev->state == SKD_DRVR_STATE_WAIT_BOOT) {
			skdev->timer_countdown = SKD_STARTING_TIMO;
			skdev->state = SKD_DRVR_STATE_STARTING;
			skd_soft_reset(skdev);
			break;
		}
		mtd = FIT_MXD_CONS(FIT_MTD_FITFW_INIT, 0, 0);
		SKD_WRITEL(skdev, mtd, FIT_MSG_TO_DEVICE);
		skdev->last_mtd = mtd;
		break;

	case FIT_SR_DRIVE_ONLINE:
		skdev->cur_max_queue_depth = skd_max_queue_depth;
		if (skdev->cur_max_queue_depth > skdev->dev_max_queue_depth)
			skdev->cur_max_queue_depth = skdev->dev_max_queue_depth;

		skdev->queue_low_water_mark =
			skdev->cur_max_queue_depth * 2 / 3 + 1;
		if (skdev->queue_low_water_mark < 1)
			skdev->queue_low_water_mark = 1;
		pr_info(
		       "(%s): Queue depth limit=%d dev=%d lowat=%d\n",
		       skd_name(skdev),
		       skdev->cur_max_queue_depth,
		       skdev->dev_max_queue_depth, skdev->queue_low_water_mark);

		skd_refresh_device_data(skdev);
		break;

	case FIT_SR_DRIVE_BUSY:
		skdev->state = SKD_DRVR_STATE_BUSY;
		skdev->timer_countdown = SKD_BUSY_TIMO;
		skd_quiesce_dev(skdev);
		break;
	case FIT_SR_DRIVE_BUSY_SANITIZE:
		/* set timer for 3 seconds, we'll abort any unfinished
		 * commands after that expires
		 */
		skdev->state = SKD_DRVR_STATE_BUSY_SANITIZE;
		skdev->timer_countdown = SKD_TIMER_SECONDS(3);
		blk_start_queue(skdev->queue);
		break;
	case FIT_SR_DRIVE_BUSY_ERASE:
		skdev->state = SKD_DRVR_STATE_BUSY_ERASE;
		skdev->timer_countdown = SKD_BUSY_TIMO;
		break;
	case FIT_SR_DRIVE_OFFLINE:
		skdev->state = SKD_DRVR_STATE_IDLE;
		break;
	case FIT_SR_DRIVE_SOFT_RESET:
		switch (skdev->state) {
		case SKD_DRVR_STATE_STARTING:
		case SKD_DRVR_STATE_RESTARTING:
			/* Expected by a caller of skd_soft_reset() */
			break;
		default:
			skdev->state = SKD_DRVR_STATE_RESTARTING;
			break;
		}
		break;
	case FIT_SR_DRIVE_FW_BOOTING:
		pr_debug("%s:%s:%d ISR FIT_SR_DRIVE_FW_BOOTING %s\n",
			 skdev->name, __func__, __LINE__, skdev->name);
		skdev->state = SKD_DRVR_STATE_WAIT_BOOT;
		skdev->timer_countdown = SKD_WAIT_BOOT_TIMO;
		break;

	case FIT_SR_DRIVE_DEGRADED:
	case FIT_SR_PCIE_LINK_DOWN:
	case FIT_SR_DRIVE_NEED_FW_DOWNLOAD:
		break;

	case FIT_SR_DRIVE_FAULT:
		skd_drive_fault(skdev);
		skd_recover_requests(skdev, 0);
		blk_start_queue(skdev->queue);
		break;

	/* PCIe bus returned all Fs? */
	case 0xFF:
		pr_info("(%s): state=0x%x sense=0x%x\n",
		       skd_name(skdev), state, sense);
		skd_drive_disappeared(skdev);
		skd_recover_requests(skdev, 0);
		blk_start_queue(skdev->queue);
		break;
	default:
		/*
		 * Uknown FW State. Wait for a state we recognize.
		 */
		break;
	}
	pr_err("(%s): Driver state %s(%d)=>%s(%d)\n",
	       skd_name(skdev),
	       skd_skdev_state_to_str(prev_driver_state), prev_driver_state,
	       skd_skdev_state_to_str(skdev->state), skdev->state);
}

static void skd_recover_requests(struct skd_device *skdev, int requeue)
{
	int i;

	for (i = 0; i < skdev->num_req_context; i++) {
		struct skd_request_context *skreq = &skdev->skreq_table[i];

		if (skreq->state == SKD_REQ_STATE_BUSY) {
			skd_log_skreq(skdev, skreq, "recover");

			SKD_ASSERT((skreq->id & SKD_ID_INCR) != 0);
			SKD_ASSERT(skreq->req != NULL);

			/* Release DMA resources for the request. */
			if (skreq->n_sg > 0)
				skd_postop_sg_list(skdev, skreq);

			if (requeue &&
			    (unsigned long) ++skreq->req->special <
			    SKD_MAX_RETRIES)
				blk_requeue_request(skdev->queue, skreq->req);
			else
				skd_end_request(skdev, skreq, -EIO);

			skreq->req = NULL;

			skreq->state = SKD_REQ_STATE_IDLE;
			skreq->id += SKD_ID_INCR;
		}
		if (i > 0)
			skreq[-1].next = skreq;
		skreq->next = NULL;
	}
	skdev->skreq_free_list = skdev->skreq_table;

	for (i = 0; i < skdev->num_fitmsg_context; i++) {
		struct skd_fitmsg_context *skmsg = &skdev->skmsg_table[i];

		if (skmsg->state == SKD_MSG_STATE_BUSY) {
			skd_log_skmsg(skdev, skmsg, "salvaged");
			SKD_ASSERT((skmsg->id & SKD_ID_INCR) != 0);
			skmsg->state = SKD_MSG_STATE_IDLE;
			skmsg->id += SKD_ID_INCR;
		}
		if (i > 0)
			skmsg[-1].next = skmsg;
		skmsg->next = NULL;
	}
	skdev->skmsg_free_list = skdev->skmsg_table;

	for (i = 0; i < skdev->n_special; i++) {
		struct skd_special_context *skspcl = &skdev->skspcl_table[i];

		/* If orphaned, reclaim it because it has already been reported
		 * to the process as an error (it was just waiting for
		 * a completion that didn't come, and now it will never come)
		 * If busy, change to a state that will cause it to error
		 * out in the wait routine and let it do the normal
		 * reporting and reclaiming
		 */
		if (skspcl->req.state == SKD_REQ_STATE_BUSY) {
			if (skspcl->orphaned) {
				pr_debug("%s:%s:%d orphaned %p\n",
					 skdev->name, __func__, __LINE__,
					 skspcl);
				skd_release_special(skdev, skspcl);
			} else {
				pr_debug("%s:%s:%d not orphaned %p\n",
					 skdev->name, __func__, __LINE__,
					 skspcl);
				skspcl->req.state = SKD_REQ_STATE_ABORTED;
			}
		}
	}
	skdev->skspcl_free_list = skdev->skspcl_table;

	for (i = 0; i < SKD_N_TIMEOUT_SLOT; i++)
		skdev->timeout_slot[i] = 0;

	skdev->in_flight = 0;
}

static void skd_isr_msg_from_dev(struct skd_device *skdev)
{
	u32 mfd;
	u32 mtd;
	u32 data;

	mfd = SKD_READL(skdev, FIT_MSG_FROM_DEVICE);

	pr_debug("%s:%s:%d mfd=0x%x last_mtd=0x%x\n",
		 skdev->name, __func__, __LINE__, mfd, skdev->last_mtd);

	/* ignore any mtd that is an ack for something we didn't send */
	if (FIT_MXD_TYPE(mfd) != FIT_MXD_TYPE(skdev->last_mtd))
		return;

	switch (FIT_MXD_TYPE(mfd)) {
	case FIT_MTD_FITFW_INIT:
		skdev->proto_ver = FIT_PROTOCOL_MAJOR_VER(mfd);

		if (skdev->proto_ver != FIT_PROTOCOL_VERSION_1) {
			pr_err("(%s): protocol mismatch\n",
			       skdev->name);
			pr_err("(%s):   got=%d support=%d\n",
			       skdev->name, skdev->proto_ver,
			       FIT_PROTOCOL_VERSION_1);
			pr_err("(%s):   please upgrade driver\n",
			       skdev->name);
			skdev->state = SKD_DRVR_STATE_PROTOCOL_MISMATCH;
			skd_soft_reset(skdev);
			break;
		}
		mtd = FIT_MXD_CONS(FIT_MTD_GET_CMDQ_DEPTH, 0, 0);
		SKD_WRITEL(skdev, mtd, FIT_MSG_TO_DEVICE);
		skdev->last_mtd = mtd;
		break;

	case FIT_MTD_GET_CMDQ_DEPTH:
		skdev->dev_max_queue_depth = FIT_MXD_DATA(mfd);
		mtd = FIT_MXD_CONS(FIT_MTD_SET_COMPQ_DEPTH, 0,
				   SKD_N_COMPLETION_ENTRY);
		SKD_WRITEL(skdev, mtd, FIT_MSG_TO_DEVICE);
		skdev->last_mtd = mtd;
		break;

	case FIT_MTD_SET_COMPQ_DEPTH:
		SKD_WRITEQ(skdev, skdev->cq_dma_address, FIT_MSG_TO_DEVICE_ARG);
		mtd = FIT_MXD_CONS(FIT_MTD_SET_COMPQ_ADDR, 0, 0);
		SKD_WRITEL(skdev, mtd, FIT_MSG_TO_DEVICE);
		skdev->last_mtd = mtd;
		break;

	case FIT_MTD_SET_COMPQ_ADDR:
		skd_reset_skcomp(skdev);
		mtd = FIT_MXD_CONS(FIT_MTD_CMD_LOG_HOST_ID, 0, skdev->devno);
		SKD_WRITEL(skdev, mtd, FIT_MSG_TO_DEVICE);
		skdev->last_mtd = mtd;
		break;

	case FIT_MTD_CMD_LOG_HOST_ID:
		skdev->connect_time_stamp = get_seconds();
		data = skdev->connect_time_stamp & 0xFFFF;
		mtd = FIT_MXD_CONS(FIT_MTD_CMD_LOG_TIME_STAMP_LO, 0, data);
		SKD_WRITEL(skdev, mtd, FIT_MSG_TO_DEVICE);
		skdev->last_mtd = mtd;
		break;

	case FIT_MTD_CMD_LOG_TIME_STAMP_LO:
		skdev->drive_jiffies = FIT_MXD_DATA(mfd);
		data = (skdev->connect_time_stamp >> 16) & 0xFFFF;
		mtd = FIT_MXD_CONS(FIT_MTD_CMD_LOG_TIME_STAMP_HI, 0, data);
		SKD_WRITEL(skdev, mtd, FIT_MSG_TO_DEVICE);
		skdev->last_mtd = mtd;
		break;

	case FIT_MTD_CMD_LOG_TIME_STAMP_HI:
		skdev->drive_jiffies |= (FIT_MXD_DATA(mfd) << 16);
		mtd = FIT_MXD_CONS(FIT_MTD_ARM_QUEUE, 0, 0);
		SKD_WRITEL(skdev, mtd, FIT_MSG_TO_DEVICE);
		skdev->last_mtd = mtd;

		pr_err("(%s): Time sync driver=0x%x device=0x%x\n",
		       skd_name(skdev),
		       skdev->connect_time_stamp, skdev->drive_jiffies);
		break;

	case FIT_MTD_ARM_QUEUE:
		skdev->last_mtd = 0;
		/*
		 * State should be, or soon will be, FIT_SR_DRIVE_ONLINE.
		 */
		break;

	default:
		break;
	}
}

static void skd_disable_interrupts(struct skd_device *skdev)
{
	u32 sense;

	sense = SKD_READL(skdev, FIT_CONTROL);
	sense &= ~FIT_CR_ENABLE_INTERRUPTS;
	SKD_WRITEL(skdev, sense, FIT_CONTROL);
	pr_debug("%s:%s:%d sense 0x%x\n",
		 skdev->name, __func__, __LINE__, sense);

	/* Note that the 1s is written. A 1-bit means
	 * disable, a 0 means enable.
	 */
	SKD_WRITEL(skdev, ~0, FIT_INT_MASK_HOST);
}

static void skd_enable_interrupts(struct skd_device *skdev)
{
	u32 val;

	/* unmask interrupts first */
	val = FIT_ISH_FW_STATE_CHANGE +
	      FIT_ISH_COMPLETION_POSTED + FIT_ISH_MSG_FROM_DEV;

	/* Note that the compliment of mask is written. A 1-bit means
	 * disable, a 0 means enable. */
	SKD_WRITEL(skdev, ~val, FIT_INT_MASK_HOST);
	pr_debug("%s:%s:%d interrupt mask=0x%x\n",
		 skdev->name, __func__, __LINE__, ~val);

	val = SKD_READL(skdev, FIT_CONTROL);
	val |= FIT_CR_ENABLE_INTERRUPTS;
	pr_debug("%s:%s:%d control=0x%x\n",
		 skdev->name, __func__, __LINE__, val);
	SKD_WRITEL(skdev, val, FIT_CONTROL);
}

/*
 *****************************************************************************
 * START, STOP, RESTART, QUIESCE, UNQUIESCE
 *****************************************************************************
 */

static void skd_soft_reset(struct skd_device *skdev)
{
	u32 val;

	val = SKD_READL(skdev, FIT_CONTROL);
	val |= (FIT_CR_SOFT_RESET);
	pr_debug("%s:%s:%d control=0x%x\n",
		 skdev->name, __func__, __LINE__, val);
	SKD_WRITEL(skdev, val, FIT_CONTROL);
}

static void skd_start_device(struct skd_device *skdev)
{
	unsigned long flags;
	u32 sense;
	u32 state;

	spin_lock_irqsave(&skdev->lock, flags);

	/* ack all ghost interrupts */
	SKD_WRITEL(skdev, FIT_INT_DEF_MASK, FIT_INT_STATUS_HOST);

	sense = SKD_READL(skdev, FIT_STATUS);

	pr_debug("%s:%s:%d initial status=0x%x\n",
		 skdev->name, __func__, __LINE__, sense);

	state = sense & FIT_SR_DRIVE_STATE_MASK;
	skdev->drive_state = state;
	skdev->last_mtd = 0;

	skdev->state = SKD_DRVR_STATE_STARTING;
	skdev->timer_countdown = SKD_STARTING_TIMO;

	skd_enable_interrupts(skdev);

	switch (skdev->drive_state) {
	case FIT_SR_DRIVE_OFFLINE:
		pr_err("(%s): Drive offline...\n", skd_name(skdev));
		break;

	case FIT_SR_DRIVE_FW_BOOTING:
		pr_debug("%s:%s:%d FIT_SR_DRIVE_FW_BOOTING %s\n",
			 skdev->name, __func__, __LINE__, skdev->name);
		skdev->state = SKD_DRVR_STATE_WAIT_BOOT;
		skdev->timer_countdown = SKD_WAIT_BOOT_TIMO;
		break;

	case FIT_SR_DRIVE_BUSY_SANITIZE:
		pr_info("(%s): Start: BUSY_SANITIZE\n",
		       skd_name(skdev));
		skdev->state = SKD_DRVR_STATE_BUSY_SANITIZE;
		skdev->timer_countdown = SKD_STARTED_BUSY_TIMO;
		break;

	case FIT_SR_DRIVE_BUSY_ERASE:
		pr_info("(%s): Start: BUSY_ERASE\n", skd_name(skdev));
		skdev->state = SKD_DRVR_STATE_BUSY_ERASE;
		skdev->timer_countdown = SKD_STARTED_BUSY_TIMO;
		break;

	case FIT_SR_DRIVE_INIT:
	case FIT_SR_DRIVE_ONLINE:
		skd_soft_reset(skdev);
		break;

	case FIT_SR_DRIVE_BUSY:
		pr_err("(%s): Drive Busy...\n", skd_name(skdev));
		skdev->state = SKD_DRVR_STATE_BUSY;
		skdev->timer_countdown = SKD_STARTED_BUSY_TIMO;
		break;

	case FIT_SR_DRIVE_SOFT_RESET:
		pr_err("(%s) drive soft reset in prog\n",
		       skd_name(skdev));
		break;

	case FIT_SR_DRIVE_FAULT:
		/* Fault state is bad...soft reset won't do it...
		 * Hard reset, maybe, but does it work on device?
		 * For now, just fault so the system doesn't hang.
		 */
		skd_drive_fault(skdev);
		/*start the queue so we can respond with error to requests */
		pr_debug("%s:%s:%d starting %s queue\n",
			 skdev->name, __func__, __LINE__, skdev->name);
		blk_start_queue(skdev->queue);
		skdev->gendisk_on = -1;
		wake_up_interruptible(&skdev->waitq);
		break;

	case 0xFF:
		/* Most likely the device isn't there or isn't responding
		 * to the BAR1 addresses. */
		skd_drive_disappeared(skdev);
		/*start the queue so we can respond with error to requests */
		pr_debug("%s:%s:%d starting %s queue to error-out reqs\n",
			 skdev->name, __func__, __LINE__, skdev->name);
		blk_start_queue(skdev->queue);
		skdev->gendisk_on = -1;
		wake_up_interruptible(&skdev->waitq);
		break;

	default:
		pr_err("(%s) Start: unknown state %x\n",
		       skd_name(skdev), skdev->drive_state);
		break;
	}

	state = SKD_READL(skdev, FIT_CONTROL);
	pr_debug("%s:%s:%d FIT Control Status=0x%x\n",
		 skdev->name, __func__, __LINE__, state);

	state = SKD_READL(skdev, FIT_INT_STATUS_HOST);
	pr_debug("%s:%s:%d Intr Status=0x%x\n",
		 skdev->name, __func__, __LINE__, state);

	state = SKD_READL(skdev, FIT_INT_MASK_HOST);
	pr_debug("%s:%s:%d Intr Mask=0x%x\n",
		 skdev->name, __func__, __LINE__, state);

	state = SKD_READL(skdev, FIT_MSG_FROM_DEVICE);
	pr_debug("%s:%s:%d Msg from Dev=0x%x\n",
		 skdev->name, __func__, __LINE__, state);

	state = SKD_READL(skdev, FIT_HW_VERSION);
	pr_debug("%s:%s:%d HW version=0x%x\n",
		 skdev->name, __func__, __LINE__, state);

	spin_unlock_irqrestore(&skdev->lock, flags);
}

static void skd_stop_device(struct skd_device *skdev)
{
	unsigned long flags;
	struct skd_special_context *skspcl = &skdev->internal_skspcl;
	u32 dev_state;
	int i;

	spin_lock_irqsave(&skdev->lock, flags);

	if (skdev->state != SKD_DRVR_STATE_ONLINE) {
		pr_err("(%s): skd_stop_device not online no sync\n",
		       skd_name(skdev));
		goto stop_out;
	}

	if (skspcl->req.state != SKD_REQ_STATE_IDLE) {
		pr_err("(%s): skd_stop_device no special\n",
		       skd_name(skdev));
		goto stop_out;
	}

	skdev->state = SKD_DRVR_STATE_SYNCING;
	skdev->sync_done = 0;

	skd_send_internal_skspcl(skdev, skspcl, SYNCHRONIZE_CACHE);

	spin_unlock_irqrestore(&skdev->lock, flags);

	wait_event_interruptible_timeout(skdev->waitq,
					 (skdev->sync_done), (10 * HZ));

	spin_lock_irqsave(&skdev->lock, flags);

	switch (skdev->sync_done) {
	case 0:
		pr_err("(%s): skd_stop_device no sync\n",
		       skd_name(skdev));
		break;
	case 1:
		pr_err("(%s): skd_stop_device sync done\n",
		       skd_name(skdev));
		break;
	default:
		pr_err("(%s): skd_stop_device sync error\n",
		       skd_name(skdev));
	}

stop_out:
	skdev->state = SKD_DRVR_STATE_STOPPING;
	spin_unlock_irqrestore(&skdev->lock, flags);

	skd_kill_timer(skdev);

	spin_lock_irqsave(&skdev->lock, flags);
	skd_disable_interrupts(skdev);

	/* ensure all ints on device are cleared */
	/* soft reset the device to unload with a clean slate */
	SKD_WRITEL(skdev, FIT_INT_DEF_MASK, FIT_INT_STATUS_HOST);
	SKD_WRITEL(skdev, FIT_CR_SOFT_RESET, FIT_CONTROL);

	spin_unlock_irqrestore(&skdev->lock, flags);

	/* poll every 100ms, 1 second timeout */
	for (i = 0; i < 10; i++) {
		dev_state =
			SKD_READL(skdev, FIT_STATUS) & FIT_SR_DRIVE_STATE_MASK;
		if (dev_state == FIT_SR_DRIVE_INIT)
			break;
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(msecs_to_jiffies(100));
	}

	if (dev_state != FIT_SR_DRIVE_INIT)
		pr_err("(%s): skd_stop_device state error 0x%02x\n",
		       skd_name(skdev), dev_state);
}

/* assume spinlock is held */
static void skd_restart_device(struct skd_device *skdev)
{
	u32 state;

	/* ack all ghost interrupts */
	SKD_WRITEL(skdev, FIT_INT_DEF_MASK, FIT_INT_STATUS_HOST);

	state = SKD_READL(skdev, FIT_STATUS);

	pr_debug("%s:%s:%d drive status=0x%x\n",
		 skdev->name, __func__, __LINE__, state);

	state &= FIT_SR_DRIVE_STATE_MASK;
	skdev->drive_state = state;
	skdev->last_mtd = 0;

	skdev->state = SKD_DRVR_STATE_RESTARTING;
	skdev->timer_countdown = SKD_RESTARTING_TIMO;

	skd_soft_reset(skdev);
}

/* assume spinlock is held */
static int skd_quiesce_dev(struct skd_device *skdev)
{
	int rc = 0;

	switch (skdev->state) {
	case SKD_DRVR_STATE_BUSY:
	case SKD_DRVR_STATE_BUSY_IMMINENT:
		pr_debug("%s:%s:%d stopping %s queue\n",
			 skdev->name, __func__, __LINE__, skdev->name);
		blk_stop_queue(skdev->queue);
		break;
	case SKD_DRVR_STATE_ONLINE:
	case SKD_DRVR_STATE_STOPPING:
	case SKD_DRVR_STATE_SYNCING:
	case SKD_DRVR_STATE_PAUSING:
	case SKD_DRVR_STATE_PAUSED:
	case SKD_DRVR_STATE_STARTING:
	case SKD_DRVR_STATE_RESTARTING:
	case SKD_DRVR_STATE_RESUMING:
	default:
		rc = -EINVAL;
		pr_debug("%s:%s:%d state [%d] not implemented\n",
			 skdev->name, __func__, __LINE__, skdev->state);
	}
	return rc;
}

/* assume spinlock is held */
static int skd_unquiesce_dev(struct skd_device *skdev)
{
	int prev_driver_state = skdev->state;

	skd_log_skdev(skdev, "unquiesce");
	if (skdev->state == SKD_DRVR_STATE_ONLINE) {
		pr_debug("%s:%s:%d **** device already ONLINE\n",
			 skdev->name, __func__, __LINE__);
		return 0;
	}
	if (skdev->drive_state != FIT_SR_DRIVE_ONLINE) {
		/*
		 * If there has been an state change to other than
		 * ONLINE, we will rely on controller state change
		 * to come back online and restart the queue.
		 * The BUSY state means that driver is ready to
		 * continue normal processing but waiting for controller
		 * to become available.
		 */
		skdev->state = SKD_DRVR_STATE_BUSY;
		pr_debug("%s:%s:%d drive BUSY state\n",
			 skdev->name, __func__, __LINE__);
		return 0;
	}

	/*
	 * Drive has just come online, driver is either in startup,
	 * paused performing a task, or bust waiting for hardware.
	 */
	switch (skdev->state) {
	case SKD_DRVR_STATE_PAUSED:
	case SKD_DRVR_STATE_BUSY:
	case SKD_DRVR_STATE_BUSY_IMMINENT:
	case SKD_DRVR_STATE_BUSY_ERASE:
	case SKD_DRVR_STATE_STARTING:
	case SKD_DRVR_STATE_RESTARTING:
	case SKD_DRVR_STATE_FAULT:
	case SKD_DRVR_STATE_IDLE:
	case SKD_DRVR_STATE_LOAD:
		skdev->state = SKD_DRVR_STATE_ONLINE;
		pr_err("(%s): Driver state %s(%d)=>%s(%d)\n",
		       skd_name(skdev),
		       skd_skdev_state_to_str(prev_driver_state),
		       prev_driver_state, skd_skdev_state_to_str(skdev->state),
		       skdev->state);
		pr_debug("%s:%s:%d **** device ONLINE...starting block queue\n",
			 skdev->name, __func__, __LINE__);
		pr_debug("%s:%s:%d starting %s queue\n",
			 skdev->name, __func__, __LINE__, skdev->name);
		pr_info("(%s): STEC s1120 ONLINE\n", skd_name(skdev));
		blk_start_queue(skdev->queue);
		skdev->gendisk_on = 1;
		wake_up_interruptible(&skdev->waitq);
		break;

	case SKD_DRVR_STATE_DISAPPEARED:
	default:
		pr_debug("%s:%s:%d **** driver state %d, not implemented \n",
			 skdev->name, __func__, __LINE__,
			 skdev->state);
		return -EBUSY;
	}
	return 0;
}

/*
 *****************************************************************************
 * PCIe MSI/MSI-X INTERRUPT HANDLERS
 *****************************************************************************
 */

static irqreturn_t skd_reserved_isr(int irq, void *skd_host_data)
{
	struct skd_device *skdev = skd_host_data;
	unsigned long flags;

	spin_lock_irqsave(&skdev->lock, flags);
	pr_debug("%s:%s:%d MSIX = 0x%x\n",
		 skdev->name, __func__, __LINE__,
		 SKD_READL(skdev, FIT_INT_STATUS_HOST));
	pr_err("(%s): MSIX reserved irq %d = 0x%x\n", skd_name(skdev),
	       irq, SKD_READL(skdev, FIT_INT_STATUS_HOST));
	SKD_WRITEL(skdev, FIT_INT_RESERVED_MASK, FIT_INT_STATUS_HOST);
	spin_unlock_irqrestore(&skdev->lock, flags);
	return IRQ_HANDLED;
}

static irqreturn_t skd_statec_isr(int irq, void *skd_host_data)
{
	struct skd_device *skdev = skd_host_data;
	unsigned long flags;

	spin_lock_irqsave(&skdev->lock, flags);
	pr_debug("%s:%s:%d MSIX = 0x%x\n",
		 skdev->name, __func__, __LINE__,
		 SKD_READL(skdev, FIT_INT_STATUS_HOST));
	SKD_WRITEL(skdev, FIT_ISH_FW_STATE_CHANGE, FIT_INT_STATUS_HOST);
	skd_isr_fwstate(skdev);
	spin_unlock_irqrestore(&skdev->lock, flags);
	return IRQ_HANDLED;
}

static irqreturn_t skd_comp_q(int irq, void *skd_host_data)
{
	struct skd_device *skdev = skd_host_data;
	unsigned long flags;
	int flush_enqueued = 0;
	int deferred;

	spin_lock_irqsave(&skdev->lock, flags);
	pr_debug("%s:%s:%d MSIX = 0x%x\n",
		 skdev->name, __func__, __LINE__,
		 SKD_READL(skdev, FIT_INT_STATUS_HOST));
	SKD_WRITEL(skdev, FIT_ISH_COMPLETION_POSTED, FIT_INT_STATUS_HOST);
	deferred = skd_isr_completion_posted(skdev, skd_isr_comp_limit,
						&flush_enqueued);
	if (flush_enqueued)
		skd_request_fn(skdev->queue);

	if (deferred)
		schedule_work(&skdev->completion_worker);
	else if (!flush_enqueued)
		skd_request_fn(skdev->queue);

	spin_unlock_irqrestore(&skdev->lock, flags);

	return IRQ_HANDLED;
}

static irqreturn_t skd_msg_isr(int irq, void *skd_host_data)
{
	struct skd_device *skdev = skd_host_data;
	unsigned long flags;

	spin_lock_irqsave(&skdev->lock, flags);
	pr_debug("%s:%s:%d MSIX = 0x%x\n",
		 skdev->name, __func__, __LINE__,
		 SKD_READL(skdev, FIT_INT_STATUS_HOST));
	SKD_WRITEL(skdev, FIT_ISH_MSG_FROM_DEV, FIT_INT_STATUS_HOST);
	skd_isr_msg_from_dev(skdev);
	spin_unlock_irqrestore(&skdev->lock, flags);
	return IRQ_HANDLED;
}

static irqreturn_t skd_qfull_isr(int irq, void *skd_host_data)
{
	struct skd_device *skdev = skd_host_data;
	unsigned long flags;

	spin_lock_irqsave(&skdev->lock, flags);
	pr_debug("%s:%s:%d MSIX = 0x%x\n",
		 skdev->name, __func__, __LINE__,
		 SKD_READL(skdev, FIT_INT_STATUS_HOST));
	SKD_WRITEL(skdev, FIT_INT_QUEUE_FULL, FIT_INT_STATUS_HOST);
	spin_unlock_irqrestore(&skdev->lock, flags);
	return IRQ_HANDLED;
}

/*
 *****************************************************************************
 * PCIe MSI/MSI-X SETUP
 *****************************************************************************
 */

struct skd_msix_entry {
	int have_irq;
	u32 vector;
	u32 entry;
	struct skd_device *rsp;
	char isr_name[30];
};

struct skd_init_msix_entry {
	const char *name;
	irq_handler_t handler;
};

#define SKD_MAX_MSIX_COUNT              13
#define SKD_MIN_MSIX_COUNT              7
#define SKD_BASE_MSIX_IRQ               4

static struct skd_init_msix_entry msix_entries[SKD_MAX_MSIX_COUNT] = {
	{ "(DMA 0)",	    skd_reserved_isr },
	{ "(DMA 1)",	    skd_reserved_isr },
	{ "(DMA 2)",	    skd_reserved_isr },
	{ "(DMA 3)",	    skd_reserved_isr },
	{ "(State Change)", skd_statec_isr   },
	{ "(COMPL_Q)",	    skd_comp_q	     },
	{ "(MSG)",	    skd_msg_isr	     },
	{ "(Reserved)",	    skd_reserved_isr },
	{ "(Reserved)",	    skd_reserved_isr },
	{ "(Queue Full 0)", skd_qfull_isr    },
	{ "(Queue Full 1)", skd_qfull_isr    },
	{ "(Queue Full 2)", skd_qfull_isr    },
	{ "(Queue Full 3)", skd_qfull_isr    },
};

static void skd_release_msix(struct skd_device *skdev)
{
	struct skd_msix_entry *qentry;
	int i;

	if (skdev->msix_entries == NULL)
		return;
	for (i = 0; i < skdev->msix_count; i++) {
		qentry = &skdev->msix_entries[i];
		skdev = qentry->rsp;

		if (qentry->have_irq)
			devm_free_irq(&skdev->pdev->dev,
				      qentry->vector, qentry->rsp);
	}
	pci_disable_msix(skdev->pdev);
	kfree(skdev->msix_entries);
	skdev->msix_count = 0;
	skdev->msix_entries = NULL;
}

static int skd_acquire_msix(struct skd_device *skdev)
{
	int i, rc;
	struct pci_dev *pdev;
	struct msix_entry *entries = NULL;
	struct skd_msix_entry *qentry;

	pdev = skdev->pdev;
	skdev->msix_count = SKD_MAX_MSIX_COUNT;
	entries = kzalloc(sizeof(struct msix_entry) * SKD_MAX_MSIX_COUNT,
			  GFP_KERNEL);
	if (!entries)
		return -ENOMEM;

	for (i = 0; i < SKD_MAX_MSIX_COUNT; i++)
		entries[i].entry = i;

	rc = pci_enable_msix(pdev, entries, SKD_MAX_MSIX_COUNT);
	if (rc < 0)
		goto msix_out;
	if (rc) {
		if (rc < SKD_MIN_MSIX_COUNT) {
			pr_err("(%s): failed to enable MSI-X %d\n",
			       skd_name(skdev), rc);
			goto msix_out;
		}
		pr_debug("%s:%s:%d %s: <%s> allocated %d MSI-X vectors\n",
			 skdev->name, __func__, __LINE__,
			 pci_name(pdev), skdev->name, rc);

		skdev->msix_count = rc;
		rc = pci_enable_msix(pdev, entries, skdev->msix_count);
		if (rc) {
			pr_err("(%s): failed to enable MSI-X "
			       "support (%d) %d\n",
			       skd_name(skdev), skdev->msix_count, rc);
			goto msix_out;
		}
	}
	skdev->msix_entries = kzalloc(sizeof(struct skd_msix_entry) *
				      skdev->msix_count, GFP_KERNEL);
	if (!skdev->msix_entries) {
		rc = -ENOMEM;
		skdev->msix_count = 0;
		pr_err("(%s): msix table allocation error\n",
		       skd_name(skdev));
		goto msix_out;
	}

	qentry = skdev->msix_entries;
	for (i = 0; i < skdev->msix_count; i++) {
		qentry->vector = entries[i].vector;
		qentry->entry = entries[i].entry;
		qentry->rsp = NULL;
		qentry->have_irq = 0;
		pr_debug("%s:%s:%d %s: <%s> msix (%d) vec %d, entry %x\n",
			 skdev->name, __func__, __LINE__,
			 pci_name(pdev), skdev->name,
			 i, qentry->vector, qentry->entry);
		qentry++;
	}

	/* Enable MSI-X vectors for the base queue */
	for (i = 0; i < SKD_MAX_MSIX_COUNT; i++) {
		qentry = &skdev->msix_entries[i];
		snprintf(qentry->isr_name, sizeof(qentry->isr_name),
			 "%s%d-msix %s", DRV_NAME, skdev->devno,
			 msix_entries[i].name);
		rc = devm_request_irq(&skdev->pdev->dev, qentry->vector,
				      msix_entries[i].handler, 0,
				      qentry->isr_name, skdev);
		if (rc) {
			pr_err("(%s): Unable to register(%d) MSI-X "
			       "handler %d: %s\n",
			       skd_name(skdev), rc, i, qentry->isr_name);
			goto msix_out;
		} else {
			qentry->have_irq = 1;
			qentry->rsp = skdev;
		}
	}
	pr_debug("%s:%s:%d %s: <%s> msix %d irq(s) enabled\n",
		 skdev->name, __func__, __LINE__,
		 pci_name(pdev), skdev->name, skdev->msix_count);
	return 0;

msix_out:
	if (entries)
		kfree(entries);
	skd_release_msix(skdev);
	return rc;
}

static int skd_acquire_irq(struct skd_device *skdev)
{
	int rc;
	struct pci_dev *pdev;

	pdev = skdev->pdev;
	skdev->msix_count = 0;

RETRY_IRQ_TYPE:
	switch (skdev->irq_type) {
	case SKD_IRQ_MSIX:
		rc = skd_acquire_msix(skdev);
		if (!rc)
			pr_info("(%s): MSI-X %d irqs enabled\n",
			       skd_name(skdev), skdev->msix_count);
		else {
			pr_err(
			       "(%s): failed to enable MSI-X, re-trying with MSI %d\n",
			       skd_name(skdev), rc);
			skdev->irq_type = SKD_IRQ_MSI;
			goto RETRY_IRQ_TYPE;
		}
		break;
	case SKD_IRQ_MSI:
		snprintf(skdev->isr_name, sizeof(skdev->isr_name), "%s%d-msi",
			 DRV_NAME, skdev->devno);
		rc = pci_enable_msi(pdev);
		if (!rc) {
			rc = devm_request_irq(&pdev->dev, pdev->irq, skd_isr, 0,
					      skdev->isr_name, skdev);
			if (rc) {
				pci_disable_msi(pdev);
				pr_err(
				       "(%s): failed to allocate the MSI interrupt %d\n",
				       skd_name(skdev), rc);
				goto RETRY_IRQ_LEGACY;
			}
			pr_info("(%s): MSI irq %d enabled\n",
			       skd_name(skdev), pdev->irq);
		} else {
RETRY_IRQ_LEGACY:
			pr_err(
			       "(%s): failed to enable MSI, re-trying with LEGACY %d\n",
			       skd_name(skdev), rc);
			skdev->irq_type = SKD_IRQ_LEGACY;
			goto RETRY_IRQ_TYPE;
		}
		break;
	case SKD_IRQ_LEGACY:
		snprintf(skdev->isr_name, sizeof(skdev->isr_name),
			 "%s%d-legacy", DRV_NAME, skdev->devno);
		rc = devm_request_irq(&pdev->dev, pdev->irq, skd_isr,
				      IRQF_SHARED, skdev->isr_name, skdev);
		if (!rc)
			pr_info("(%s): LEGACY irq %d enabled\n",
			       skd_name(skdev), pdev->irq);
		else
			pr_err("(%s): request LEGACY irq error %d\n",
			       skd_name(skdev), rc);
		break;
	default:
		pr_info("(%s): irq_type %d invalid, re-set to %d\n",
		       skd_name(skdev), skdev->irq_type, SKD_IRQ_DEFAULT);
		skdev->irq_type = SKD_IRQ_LEGACY;
		goto RETRY_IRQ_TYPE;
	}
	return rc;
}

static void skd_release_irq(struct skd_device *skdev)
{
	switch (skdev->irq_type) {
	case SKD_IRQ_MSIX:
		skd_release_msix(skdev);
		break;
	case SKD_IRQ_MSI:
		devm_free_irq(&skdev->pdev->dev, skdev->pdev->irq, skdev);
		pci_disable_msi(skdev->pdev);
		break;
	case SKD_IRQ_LEGACY:
		devm_free_irq(&skdev->pdev->dev, skdev->pdev->irq, skdev);
		break;
	default:
		pr_err("(%s): wrong irq type %d!",
		       skd_name(skdev), skdev->irq_type);
		break;
	}
}

/*
 *****************************************************************************
 * CONSTRUCT
 *****************************************************************************
 */

static int skd_cons_skcomp(struct skd_device *skdev)
{
	int rc = 0;
	struct fit_completion_entry_v1 *skcomp;
	u32 nbytes;

	nbytes = sizeof(*skcomp) * SKD_N_COMPLETION_ENTRY;
	nbytes += sizeof(struct fit_comp_error_info) * SKD_N_COMPLETION_ENTRY;

	pr_debug("%s:%s:%d comp pci_alloc, total bytes %d entries %d\n",
		 skdev->name, __func__, __LINE__,
		 nbytes, SKD_N_COMPLETION_ENTRY);

	skcomp = pci_alloc_consistent(skdev->pdev, nbytes,
				      &skdev->cq_dma_address);

	if (skcomp == NULL) {
		rc = -ENOMEM;
		goto err_out;
	}

	memset(skcomp, 0, nbytes);

	skdev->skcomp_table = skcomp;
	skdev->skerr_table = (struct fit_comp_error_info *)((char *)skcomp +
							   sizeof(*skcomp) *
							   SKD_N_COMPLETION_ENTRY);

err_out:
	return rc;
}

static int skd_cons_skmsg(struct skd_device *skdev)
{
	int rc = 0;
	u32 i;

	pr_debug("%s:%s:%d skmsg_table kzalloc, struct %lu, count %u total %lu\n",
		 skdev->name, __func__, __LINE__,
		 sizeof(struct skd_fitmsg_context),
		 skdev->num_fitmsg_context,
		 sizeof(struct skd_fitmsg_context) * skdev->num_fitmsg_context);

	skdev->skmsg_table = kzalloc(sizeof(struct skd_fitmsg_context)
				     *skdev->num_fitmsg_context, GFP_KERNEL);
	if (skdev->skmsg_table == NULL) {
		rc = -ENOMEM;
		goto err_out;
	}

	for (i = 0; i < skdev->num_fitmsg_context; i++) {
		struct skd_fitmsg_context *skmsg;

		skmsg = &skdev->skmsg_table[i];

		skmsg->id = i + SKD_ID_FIT_MSG;

		skmsg->state = SKD_MSG_STATE_IDLE;
		skmsg->msg_buf = pci_alloc_consistent(skdev->pdev,
						      SKD_N_FITMSG_BYTES + 64,
						      &skmsg->mb_dma_address);

		if (skmsg->msg_buf == NULL) {
			rc = -ENOMEM;
			goto err_out;
		}

		skmsg->offset = (u32)((u64)skmsg->msg_buf &
				      (~FIT_QCMD_BASE_ADDRESS_MASK));
		skmsg->msg_buf += ~FIT_QCMD_BASE_ADDRESS_MASK;
		skmsg->msg_buf = (u8 *)((u64)skmsg->msg_buf &
				       FIT_QCMD_BASE_ADDRESS_MASK);
		skmsg->mb_dma_address += ~FIT_QCMD_BASE_ADDRESS_MASK;
		skmsg->mb_dma_address &= FIT_QCMD_BASE_ADDRESS_MASK;
		memset(skmsg->msg_buf, 0, SKD_N_FITMSG_BYTES);

		skmsg->next = &skmsg[1];
	}

	/* Free list is in order starting with the 0th entry. */
	skdev->skmsg_table[i - 1].next = NULL;
	skdev->skmsg_free_list = skdev->skmsg_table;

err_out:
	return rc;
}

static struct fit_sg_descriptor *skd_cons_sg_list(struct skd_device *skdev,
						  u32 n_sg,
						  dma_addr_t *ret_dma_addr)
{
	struct fit_sg_descriptor *sg_list;
	u32 nbytes;

	nbytes = sizeof(*sg_list) * n_sg;

	sg_list = pci_alloc_consistent(skdev->pdev, nbytes, ret_dma_addr);

	if (sg_list != NULL) {
		uint64_t dma_address = *ret_dma_addr;
		u32 i;

		memset(sg_list, 0, nbytes);

		for (i = 0; i < n_sg - 1; i++) {
			uint64_t ndp_off;
			ndp_off = (i + 1) * sizeof(struct fit_sg_descriptor);

			sg_list[i].next_desc_ptr = dma_address + ndp_off;
		}
		sg_list[i].next_desc_ptr = 0LL;
	}

	return sg_list;
}

static int skd_cons_skreq(struct skd_device *skdev)
{
	int rc = 0;
	u32 i;

	pr_debug("%s:%s:%d skreq_table kzalloc, struct %lu, count %u total %lu\n",
		 skdev->name, __func__, __LINE__,
		 sizeof(struct skd_request_context),
		 skdev->num_req_context,
		 sizeof(struct skd_request_context) * skdev->num_req_context);

	skdev->skreq_table = kzalloc(sizeof(struct skd_request_context)
				     * skdev->num_req_context, GFP_KERNEL);
	if (skdev->skreq_table == NULL) {
		rc = -ENOMEM;
		goto err_out;
	}

	pr_debug("%s:%s:%d alloc sg_table sg_per_req %u scatlist %lu total %lu\n",
		 skdev->name, __func__, __LINE__,
		 skdev->sgs_per_request, sizeof(struct scatterlist),
		 skdev->sgs_per_request * sizeof(struct scatterlist));

	for (i = 0; i < skdev->num_req_context; i++) {
		struct skd_request_context *skreq;

		skreq = &skdev->skreq_table[i];

		skreq->id = i + SKD_ID_RW_REQUEST;
		skreq->state = SKD_REQ_STATE_IDLE;

		skreq->sg = kzalloc(sizeof(struct scatterlist) *
				    skdev->sgs_per_request, GFP_KERNEL);
		if (skreq->sg == NULL) {
			rc = -ENOMEM;
			goto err_out;
		}
		sg_init_table(skreq->sg, skdev->sgs_per_request);

		skreq->sksg_list = skd_cons_sg_list(skdev,
						    skdev->sgs_per_request,
						    &skreq->sksg_dma_address);

		if (skreq->sksg_list == NULL) {
			rc = -ENOMEM;
			goto err_out;
		}

		skreq->next = &skreq[1];
	}

	/* Free list is in order starting with the 0th entry. */
	skdev->skreq_table[i - 1].next = NULL;
	skdev->skreq_free_list = skdev->skreq_table;

err_out:
	return rc;
}

static int skd_cons_skspcl(struct skd_device *skdev)
{
	int rc = 0;
	u32 i, nbytes;

	pr_debug("%s:%s:%d skspcl_table kzalloc, struct %lu, count %u total %lu\n",
		 skdev->name, __func__, __LINE__,
		 sizeof(struct skd_special_context),
		 skdev->n_special,
		 sizeof(struct skd_special_context) * skdev->n_special);

	skdev->skspcl_table = kzalloc(sizeof(struct skd_special_context)
				      * skdev->n_special, GFP_KERNEL);
	if (skdev->skspcl_table == NULL) {
		rc = -ENOMEM;
		goto err_out;
	}

	for (i = 0; i < skdev->n_special; i++) {
		struct skd_special_context *skspcl;

		skspcl = &skdev->skspcl_table[i];

		skspcl->req.id = i + SKD_ID_SPECIAL_REQUEST;
		skspcl->req.state = SKD_REQ_STATE_IDLE;

		skspcl->req.next = &skspcl[1].req;

		nbytes = SKD_N_SPECIAL_FITMSG_BYTES;

		skspcl->msg_buf = pci_alloc_consistent(skdev->pdev, nbytes,
						       &skspcl->mb_dma_address);
		if (skspcl->msg_buf == NULL) {
			rc = -ENOMEM;
			goto err_out;
		}

		memset(skspcl->msg_buf, 0, nbytes);

		skspcl->req.sg = kzalloc(sizeof(struct scatterlist) *
					 SKD_N_SG_PER_SPECIAL, GFP_KERNEL);
		if (skspcl->req.sg == NULL) {
			rc = -ENOMEM;
			goto err_out;
		}

		skspcl->req.sksg_list = skd_cons_sg_list(skdev,
							 SKD_N_SG_PER_SPECIAL,
							 &skspcl->req.
							 sksg_dma_address);
		if (skspcl->req.sksg_list == NULL) {
			rc = -ENOMEM;
			goto err_out;
		}
	}

	/* Free list is in order starting with the 0th entry. */
	skdev->skspcl_table[i - 1].req.next = NULL;
	skdev->skspcl_free_list = skdev->skspcl_table;

	return rc;

err_out:
	return rc;
}

static int skd_cons_sksb(struct skd_device *skdev)
{
	int rc = 0;
	struct skd_special_context *skspcl;
	u32 nbytes;

	skspcl = &skdev->internal_skspcl;

	skspcl->req.id = 0 + SKD_ID_INTERNAL;
	skspcl->req.state = SKD_REQ_STATE_IDLE;

	nbytes = SKD_N_INTERNAL_BYTES;

	skspcl->data_buf = pci_alloc_consistent(skdev->pdev, nbytes,
						&skspcl->db_dma_address);
	if (skspcl->data_buf == NULL) {
		rc = -ENOMEM;
		goto err_out;
	}

	memset(skspcl->data_buf, 0, nbytes);

	nbytes = SKD_N_SPECIAL_FITMSG_BYTES;
	skspcl->msg_buf = pci_alloc_consistent(skdev->pdev, nbytes,
					       &skspcl->mb_dma_address);
	if (skspcl->msg_buf == NULL) {
		rc = -ENOMEM;
		goto err_out;
	}

	memset(skspcl->msg_buf, 0, nbytes);

	skspcl->req.sksg_list = skd_cons_sg_list(skdev, 1,
						 &skspcl->req.sksg_dma_address);
	if (skspcl->req.sksg_list == NULL) {
		rc = -ENOMEM;
		goto err_out;
	}

	if (!skd_format_internal_skspcl(skdev)) {
		rc = -EINVAL;
		goto err_out;
	}

err_out:
	return rc;
}

static int skd_cons_disk(struct skd_device *skdev)
{
	int rc = 0;
	struct gendisk *disk;
	struct request_queue *q;
	unsigned long flags;

	disk = alloc_disk(SKD_MINORS_PER_DEVICE);
	if (!disk) {
		rc = -ENOMEM;
		goto err_out;
	}

	skdev->disk = disk;
	sprintf(disk->disk_name, DRV_NAME "%u", skdev->devno);

	disk->major = skdev->major;
	disk->first_minor = skdev->devno * SKD_MINORS_PER_DEVICE;
	disk->fops = &skd_blockdev_ops;
	disk->private_data = skdev;

	q = blk_init_queue(skd_request_fn, &skdev->lock);
	if (!q) {
		rc = -ENOMEM;
		goto err_out;
	}

	skdev->queue = q;
	disk->queue = q;
	q->queuedata = skdev;

	blk_queue_flush(q, REQ_FLUSH | REQ_FUA);
	blk_queue_max_segments(q, skdev->sgs_per_request);
	blk_queue_max_hw_sectors(q, SKD_N_MAX_SECTORS);

	/* set sysfs ptimal_io_size to 8K */
	blk_queue_io_opt(q, 8192);

	/* DISCARD Flag initialization. */
	q->limits.discard_granularity = 8192;
	q->limits.discard_alignment = 0;
	q->limits.max_discard_sectors = UINT_MAX >> 9;
	q->limits.discard_zeroes_data = 1;
	queue_flag_set_unlocked(QUEUE_FLAG_DISCARD, q);
	queue_flag_set_unlocked(QUEUE_FLAG_NONROT, q);

	spin_lock_irqsave(&skdev->lock, flags);
	pr_debug("%s:%s:%d stopping %s queue\n",
		 skdev->name, __func__, __LINE__, skdev->name);
	blk_stop_queue(skdev->queue);
	spin_unlock_irqrestore(&skdev->lock, flags);

err_out:
	return rc;
}

#define SKD_N_DEV_TABLE         16u
static u32 skd_next_devno;

static struct skd_device *skd_construct(struct pci_dev *pdev)
{
	struct skd_device *skdev;
	int blk_major = skd_major;
	int rc;

	skdev = kzalloc(sizeof(*skdev), GFP_KERNEL);

	if (!skdev) {
		pr_err(PFX "(%s): memory alloc failure\n",
		       pci_name(pdev));
		return NULL;
	}

	skdev->state = SKD_DRVR_STATE_LOAD;
	skdev->pdev = pdev;
	skdev->devno = skd_next_devno++;
	skdev->major = blk_major;
	skdev->irq_type = skd_isr_type;
	sprintf(skdev->name, DRV_NAME "%d", skdev->devno);
	skdev->dev_max_queue_depth = 0;

	skdev->num_req_context = skd_max_queue_depth;
	skdev->num_fitmsg_context = skd_max_queue_depth;
	skdev->n_special = skd_max_pass_thru;
	skdev->cur_max_queue_depth = 1;
	skdev->queue_low_water_mark = 1;
	skdev->proto_ver = 99;
	skdev->sgs_per_request = skd_sgs_per_request;
	skdev->dbg_level = skd_dbg_level;

	atomic_set(&skdev->device_count, 0);

	spin_lock_init(&skdev->lock);

	INIT_WORK(&skdev->completion_worker, skd_completion_worker);

	pr_debug("%s:%s:%d skcomp\n", skdev->name, __func__, __LINE__);
	rc = skd_cons_skcomp(skdev);
	if (rc < 0)
		goto err_out;

	pr_debug("%s:%s:%d skmsg\n", skdev->name, __func__, __LINE__);
	rc = skd_cons_skmsg(skdev);
	if (rc < 0)
		goto err_out;

	pr_debug("%s:%s:%d skreq\n", skdev->name, __func__, __LINE__);
	rc = skd_cons_skreq(skdev);
	if (rc < 0)
		goto err_out;

	pr_debug("%s:%s:%d skspcl\n", skdev->name, __func__, __LINE__);
	rc = skd_cons_skspcl(skdev);
	if (rc < 0)
		goto err_out;

	pr_debug("%s:%s:%d sksb\n", skdev->name, __func__, __LINE__);
	rc = skd_cons_sksb(skdev);
	if (rc < 0)
		goto err_out;

	pr_debug("%s:%s:%d disk\n", skdev->name, __func__, __LINE__);
	rc = skd_cons_disk(skdev);
	if (rc < 0)
		goto err_out;

	pr_debug("%s:%s:%d VICTORY\n", skdev->name, __func__, __LINE__);
	return skdev;

err_out:
	pr_debug("%s:%s:%d construct failed\n",
		 skdev->name, __func__, __LINE__);
	skd_destruct(skdev);
	return NULL;
}

/*
 *****************************************************************************
 * DESTRUCT (FREE)
 *****************************************************************************
 */

static void skd_free_skcomp(struct skd_device *skdev)
{
	if (skdev->skcomp_table != NULL) {
		u32 nbytes;

		nbytes = sizeof(skdev->skcomp_table[0]) *
			 SKD_N_COMPLETION_ENTRY;
		pci_free_consistent(skdev->pdev, nbytes,
				    skdev->skcomp_table, skdev->cq_dma_address);
	}

	skdev->skcomp_table = NULL;
	skdev->cq_dma_address = 0;
}

static void skd_free_skmsg(struct skd_device *skdev)
{
	u32 i;

	if (skdev->skmsg_table == NULL)
		return;

	for (i = 0; i < skdev->num_fitmsg_context; i++) {
		struct skd_fitmsg_context *skmsg;

		skmsg = &skdev->skmsg_table[i];

		if (skmsg->msg_buf != NULL) {
			skmsg->msg_buf += skmsg->offset;
			skmsg->mb_dma_address += skmsg->offset;
			pci_free_consistent(skdev->pdev, SKD_N_FITMSG_BYTES,
					    skmsg->msg_buf,
					    skmsg->mb_dma_address);
		}
		skmsg->msg_buf = NULL;
		skmsg->mb_dma_address = 0;
	}

	kfree(skdev->skmsg_table);
	skdev->skmsg_table = NULL;
}

static void skd_free_sg_list(struct skd_device *skdev,
			     struct fit_sg_descriptor *sg_list,
			     u32 n_sg, dma_addr_t dma_addr)
{
	if (sg_list != NULL) {
		u32 nbytes;

		nbytes = sizeof(*sg_list) * n_sg;

		pci_free_consistent(skdev->pdev, nbytes, sg_list, dma_addr);
	}
}

static void skd_free_skreq(struct skd_device *skdev)
{
	u32 i;

	if (skdev->skreq_table == NULL)
		return;

	for (i = 0; i < skdev->num_req_context; i++) {
		struct skd_request_context *skreq;

		skreq = &skdev->skreq_table[i];

		skd_free_sg_list(skdev, skreq->sksg_list,
				 skdev->sgs_per_request,
				 skreq->sksg_dma_address);

		skreq->sksg_list = NULL;
		skreq->sksg_dma_address = 0;

		kfree(skreq->sg);
	}

	kfree(skdev->skreq_table);
	skdev->skreq_table = NULL;
}

static void skd_free_skspcl(struct skd_device *skdev)
{
	u32 i;
	u32 nbytes;

	if (skdev->skspcl_table == NULL)
		return;

	for (i = 0; i < skdev->n_special; i++) {
		struct skd_special_context *skspcl;

		skspcl = &skdev->skspcl_table[i];

		if (skspcl->msg_buf != NULL) {
			nbytes = SKD_N_SPECIAL_FITMSG_BYTES;
			pci_free_consistent(skdev->pdev, nbytes,
					    skspcl->msg_buf,
					    skspcl->mb_dma_address);
		}

		skspcl->msg_buf = NULL;
		skspcl->mb_dma_address = 0;

		skd_free_sg_list(skdev, skspcl->req.sksg_list,
				 SKD_N_SG_PER_SPECIAL,
				 skspcl->req.sksg_dma_address);

		skspcl->req.sksg_list = NULL;
		skspcl->req.sksg_dma_address = 0;

		kfree(skspcl->req.sg);
	}

	kfree(skdev->skspcl_table);
	skdev->skspcl_table = NULL;
}

static void skd_free_sksb(struct skd_device *skdev)
{
	struct skd_special_context *skspcl;
	u32 nbytes;

	skspcl = &skdev->internal_skspcl;

	if (skspcl->data_buf != NULL) {
		nbytes = SKD_N_INTERNAL_BYTES;

		pci_free_consistent(skdev->pdev, nbytes,
				    skspcl->data_buf, skspcl->db_dma_address);
	}

	skspcl->data_buf = NULL;
	skspcl->db_dma_address = 0;

	if (skspcl->msg_buf != NULL) {
		nbytes = SKD_N_SPECIAL_FITMSG_BYTES;
		pci_free_consistent(skdev->pdev, nbytes,
				    skspcl->msg_buf, skspcl->mb_dma_address);
	}

	skspcl->msg_buf = NULL;
	skspcl->mb_dma_address = 0;

	skd_free_sg_list(skdev, skspcl->req.sksg_list, 1,
			 skspcl->req.sksg_dma_address);

	skspcl->req.sksg_list = NULL;
	skspcl->req.sksg_dma_address = 0;
}

static void skd_free_disk(struct skd_device *skdev)
{
	struct gendisk *disk = skdev->disk;

	if (disk != NULL) {
		struct request_queue *q = disk->queue;

		if (disk->flags & GENHD_FL_UP)
			del_gendisk(disk);
		if (q)
			blk_cleanup_queue(q);
		put_disk(disk);
	}
	skdev->disk = NULL;
}

static void skd_destruct(struct skd_device *skdev)
{
	if (skdev == NULL)
		return;


	pr_debug("%s:%s:%d disk\n", skdev->name, __func__, __LINE__);
	skd_free_disk(skdev);

	pr_debug("%s:%s:%d sksb\n", skdev->name, __func__, __LINE__);
	skd_free_sksb(skdev);

	pr_debug("%s:%s:%d skspcl\n", skdev->name, __func__, __LINE__);
	skd_free_skspcl(skdev);

	pr_debug("%s:%s:%d skreq\n", skdev->name, __func__, __LINE__);
	skd_free_skreq(skdev);

	pr_debug("%s:%s:%d skmsg\n", skdev->name, __func__, __LINE__);
	skd_free_skmsg(skdev);

	pr_debug("%s:%s:%d skcomp\n", skdev->name, __func__, __LINE__);
	skd_free_skcomp(skdev);

	pr_debug("%s:%s:%d skdev\n", skdev->name, __func__, __LINE__);
	kfree(skdev);
}

/*
 *****************************************************************************
 * BLOCK DEVICE (BDEV) GLUE
 *****************************************************************************
 */

static int skd_bdev_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	struct skd_device *skdev;
	u64 capacity;

	skdev = bdev->bd_disk->private_data;

	pr_debug("%s:%s:%d %s: CMD[%s] getgeo device\n",
		 skdev->name, __func__, __LINE__,
		 bdev->bd_disk->disk_name, current->comm);

	if (skdev->read_cap_is_valid) {
		capacity = get_capacity(skdev->disk);
		geo->heads = 64;
		geo->sectors = 255;
		geo->cylinders = (capacity) / (255 * 64);

		return 0;
	}
	return -EIO;
}

static int skd_bdev_attach(struct skd_device *skdev)
{
	pr_debug("%s:%s:%d add_disk\n", skdev->name, __func__, __LINE__);
	add_disk(skdev->disk);
	return 0;
}

static const struct block_device_operations skd_blockdev_ops = {
	.owner		= THIS_MODULE,
	.ioctl		= skd_bdev_ioctl,
	.getgeo		= skd_bdev_getgeo,
};


/*
 *****************************************************************************
 * PCIe DRIVER GLUE
 *****************************************************************************
 */

static DEFINE_PCI_DEVICE_TABLE(skd_pci_tbl) = {
	{ PCI_VENDOR_ID_STEC, PCI_DEVICE_ID_S1120,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, },
	{ 0 }                     /* terminate list */
};

MODULE_DEVICE_TABLE(pci, skd_pci_tbl);

static char *skd_pci_info(struct skd_device *skdev, char *str)
{
	int pcie_reg;

	strcpy(str, "PCIe (");
	pcie_reg = pci_find_capability(skdev->pdev, PCI_CAP_ID_EXP);

	if (pcie_reg) {

		char lwstr[6];
		uint16_t pcie_lstat, lspeed, lwidth;

		pcie_reg += 0x12;
		pci_read_config_word(skdev->pdev, pcie_reg, &pcie_lstat);
		lspeed = pcie_lstat & (0xF);
		lwidth = (pcie_lstat & 0x3F0) >> 4;

		if (lspeed == 1)
			strcat(str, "2.5GT/s ");
		else if (lspeed == 2)
			strcat(str, "5.0GT/s ");
		else
			strcat(str, "<unknown> ");
		snprintf(lwstr, sizeof(lwstr), "%dX)", lwidth);
		strcat(str, lwstr);
	}
	return str;
}

static int skd_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	int i;
	int rc = 0;
	char pci_str[32];
	struct skd_device *skdev;

	pr_info("STEC s1120 Driver(%s) version %s-b%s\n",
	       DRV_NAME, DRV_VERSION, DRV_BUILD_ID);
	pr_info("(skd?:??:[%s]): vendor=%04X device=%04x\n",
	       pci_name(pdev), pdev->vendor, pdev->device);

	rc = pci_enable_device(pdev);
	if (rc)
		return rc;
	rc = pci_request_regions(pdev, DRV_NAME);
	if (rc)
		goto err_out;
	rc = pci_set_dma_mask(pdev, DMA_BIT_MASK(64));
	if (!rc) {
		if (pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64))) {

			pr_err("(%s): consistent DMA mask error %d\n",
			       pci_name(pdev), rc);
		}
	} else {
		(rc = pci_set_dma_mask(pdev, DMA_BIT_MASK(32)));
		if (rc) {

			pr_err("(%s): DMA mask error %d\n",
			       pci_name(pdev), rc);
			goto err_out_regions;
		}
	}

	if (!skd_major) {
		rc = register_blkdev(0, DRV_NAME);
		if (rc < 0)
			goto err_out_regions;
		BUG_ON(!rc);
		skd_major = rc;
	}

	skdev = skd_construct(pdev);
	if (skdev == NULL) {
		rc = -ENOMEM;
		goto err_out_regions;
	}

	skd_pci_info(skdev, pci_str);
	pr_info("(%s): %s 64bit\n", skd_name(skdev), pci_str);

	pci_set_master(pdev);
	rc = pci_enable_pcie_error_reporting(pdev);
	if (rc) {
		pr_err(
		       "(%s): bad enable of PCIe error reporting rc=%d\n",
		       skd_name(skdev), rc);
		skdev->pcie_error_reporting_is_enabled = 0;
	} else
		skdev->pcie_error_reporting_is_enabled = 1;


	pci_set_drvdata(pdev, skdev);

	skdev->disk->driverfs_dev = &pdev->dev;

	for (i = 0; i < SKD_MAX_BARS; i++) {
		skdev->mem_phys[i] = pci_resource_start(pdev, i);
		skdev->mem_size[i] = (u32)pci_resource_len(pdev, i);
		skdev->mem_map[i] = ioremap(skdev->mem_phys[i],
					    skdev->mem_size[i]);
		if (!skdev->mem_map[i]) {
			pr_err("(%s): Unable to map adapter memory!\n",
			       skd_name(skdev));
			rc = -ENODEV;
			goto err_out_iounmap;
		}
		pr_debug("%s:%s:%d mem_map=%p, phyd=%016llx, size=%d\n",
			 skdev->name, __func__, __LINE__,
			 skdev->mem_map[i],
			 (uint64_t)skdev->mem_phys[i], skdev->mem_size[i]);
	}

	rc = skd_acquire_irq(skdev);
	if (rc) {
		pr_err("(%s): interrupt resource error %d\n",
		       skd_name(skdev), rc);
		goto err_out_iounmap;
	}

	rc = skd_start_timer(skdev);
	if (rc)
		goto err_out_timer;

	init_waitqueue_head(&skdev->waitq);

	skd_start_device(skdev);

	rc = wait_event_interruptible_timeout(skdev->waitq,
					      (skdev->gendisk_on),
					      (SKD_START_WAIT_SECONDS * HZ));
	if (skdev->gendisk_on > 0) {
		/* device came on-line after reset */
		skd_bdev_attach(skdev);
		rc = 0;
	} else {
		/* we timed out, something is wrong with the device,
		   don't add the disk structure */
		pr_err(
		       "(%s): error: waiting for s1120 timed out %d!\n",
		       skd_name(skdev), rc);
		/* in case of no error; we timeout with ENXIO */
		if (!rc)
			rc = -ENXIO;
		goto err_out_timer;
	}


#ifdef SKD_VMK_POLL_HANDLER
	if (skdev->irq_type == SKD_IRQ_MSIX) {
		/* MSIX completion handler is being used for coredump */
		vmklnx_scsi_register_poll_handler(skdev->scsi_host,
						  skdev->msix_entries[5].vector,
						  skd_comp_q, skdev);
	} else {
		vmklnx_scsi_register_poll_handler(skdev->scsi_host,
						  skdev->pdev->irq, skd_isr,
						  skdev);
	}
#endif  /* SKD_VMK_POLL_HANDLER */

	return rc;

err_out_timer:
	skd_stop_device(skdev);
	skd_release_irq(skdev);

err_out_iounmap:
	for (i = 0; i < SKD_MAX_BARS; i++)
		if (skdev->mem_map[i])
			iounmap(skdev->mem_map[i]);

	if (skdev->pcie_error_reporting_is_enabled)
		pci_disable_pcie_error_reporting(pdev);

	skd_destruct(skdev);

err_out_regions:
	pci_release_regions(pdev);

err_out:
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
	return rc;
}

static void skd_pci_remove(struct pci_dev *pdev)
{
	int i;
	struct skd_device *skdev;

	skdev = pci_get_drvdata(pdev);
	if (!skdev) {
		pr_err("%s: no device data for PCI\n", pci_name(pdev));
		return;
	}
	skd_stop_device(skdev);
	skd_release_irq(skdev);

	for (i = 0; i < SKD_MAX_BARS; i++)
		if (skdev->mem_map[i])
			iounmap((u32 *)skdev->mem_map[i]);

	if (skdev->pcie_error_reporting_is_enabled)
		pci_disable_pcie_error_reporting(pdev);

	skd_destruct(skdev);

	pci_release_regions(pdev);
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);

	return;
}

static int skd_pci_suspend(struct pci_dev *pdev, pm_message_t state)
{
	int i;
	struct skd_device *skdev;

	skdev = pci_get_drvdata(pdev);
	if (!skdev) {
		pr_err("%s: no device data for PCI\n", pci_name(pdev));
		return -EIO;
	}

	skd_stop_device(skdev);

	skd_release_irq(skdev);

	for (i = 0; i < SKD_MAX_BARS; i++)
		if (skdev->mem_map[i])
			iounmap((u32 *)skdev->mem_map[i]);

	if (skdev->pcie_error_reporting_is_enabled)
		pci_disable_pcie_error_reporting(pdev);

	pci_release_regions(pdev);
	pci_save_state(pdev);
	pci_disable_device(pdev);
	pci_set_power_state(pdev, pci_choose_state(pdev, state));
	return 0;
}

static int skd_pci_resume(struct pci_dev *pdev)
{
	int i;
	int rc = 0;
	struct skd_device *skdev;

	skdev = pci_get_drvdata(pdev);
	if (!skdev) {
		pr_err("%s: no device data for PCI\n", pci_name(pdev));
		return -1;
	}

	pci_set_power_state(pdev, PCI_D0);
	pci_enable_wake(pdev, PCI_D0, 0);
	pci_restore_state(pdev);

	rc = pci_enable_device(pdev);
	if (rc)
		return rc;
	rc = pci_request_regions(pdev, DRV_NAME);
	if (rc)
		goto err_out;
	rc = pci_set_dma_mask(pdev, DMA_BIT_MASK(64));
	if (!rc) {
		if (pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64))) {

			pr_err("(%s): consistent DMA mask error %d\n",
			       pci_name(pdev), rc);
		}
	} else {
		rc = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
		if (rc) {

			pr_err("(%s): DMA mask error %d\n",
			       pci_name(pdev), rc);
			goto err_out_regions;
		}
	}

	pci_set_master(pdev);
	rc = pci_enable_pcie_error_reporting(pdev);
	if (rc) {
		pr_err("(%s): bad enable of PCIe error reporting rc=%d\n",
		       skdev->name, rc);
		skdev->pcie_error_reporting_is_enabled = 0;
	} else
		skdev->pcie_error_reporting_is_enabled = 1;

	for (i = 0; i < SKD_MAX_BARS; i++) {

		skdev->mem_phys[i] = pci_resource_start(pdev, i);
		skdev->mem_size[i] = (u32)pci_resource_len(pdev, i);
		skdev->mem_map[i] = ioremap(skdev->mem_phys[i],
					    skdev->mem_size[i]);
		if (!skdev->mem_map[i]) {
			pr_err("(%s): Unable to map adapter memory!\n",
			       skd_name(skdev));
			rc = -ENODEV;
			goto err_out_iounmap;
		}
		pr_debug("%s:%s:%d mem_map=%p, phyd=%016llx, size=%d\n",
			 skdev->name, __func__, __LINE__,
			 skdev->mem_map[i],
			 (uint64_t)skdev->mem_phys[i], skdev->mem_size[i]);
	}
	rc = skd_acquire_irq(skdev);
	if (rc) {

		pr_err("(%s): interrupt resource error %d\n",
		       pci_name(pdev), rc);
		goto err_out_iounmap;
	}

	rc = skd_start_timer(skdev);
	if (rc)
		goto err_out_timer;

	init_waitqueue_head(&skdev->waitq);

	skd_start_device(skdev);

	return rc;

err_out_timer:
	skd_stop_device(skdev);
	skd_release_irq(skdev);

err_out_iounmap:
	for (i = 0; i < SKD_MAX_BARS; i++)
		if (skdev->mem_map[i])
			iounmap(skdev->mem_map[i]);

	if (skdev->pcie_error_reporting_is_enabled)
		pci_disable_pcie_error_reporting(pdev);

err_out_regions:
	pci_release_regions(pdev);

err_out:
	pci_disable_device(pdev);
	return rc;
}

static void skd_pci_shutdown(struct pci_dev *pdev)
{
	struct skd_device *skdev;

	pr_err("skd_pci_shutdown called\n");

	skdev = pci_get_drvdata(pdev);
	if (!skdev) {
		pr_err("%s: no device data for PCI\n", pci_name(pdev));
		return;
	}

	pr_err("%s: calling stop\n", skd_name(skdev));
	skd_stop_device(skdev);
}

static struct pci_driver skd_driver = {
	.name		= DRV_NAME,
	.id_table	= skd_pci_tbl,
	.probe		= skd_pci_probe,
	.remove		= skd_pci_remove,
	.suspend	= skd_pci_suspend,
	.resume		= skd_pci_resume,
	.shutdown	= skd_pci_shutdown,
};

/*
 *****************************************************************************
 * LOGGING SUPPORT
 *****************************************************************************
 */

static const char *skd_name(struct skd_device *skdev)
{
	memset(skdev->id_str, 0, sizeof(skdev->id_str));

	if (skdev->inquiry_is_valid)
		snprintf(skdev->id_str, sizeof(skdev->id_str), "%s:%s:[%s]",
			 skdev->name, skdev->inq_serial_num,
			 pci_name(skdev->pdev));
	else
		snprintf(skdev->id_str, sizeof(skdev->id_str), "%s:??:[%s]",
			 skdev->name, pci_name(skdev->pdev));

	return skdev->id_str;
}

const char *skd_drive_state_to_str(int state)
{
	switch (state) {
	case FIT_SR_DRIVE_OFFLINE:
		return "OFFLINE";
	case FIT_SR_DRIVE_INIT:
		return "INIT";
	case FIT_SR_DRIVE_ONLINE:
		return "ONLINE";
	case FIT_SR_DRIVE_BUSY:
		return "BUSY";
	case FIT_SR_DRIVE_FAULT:
		return "FAULT";
	case FIT_SR_DRIVE_DEGRADED:
		return "DEGRADED";
	case FIT_SR_PCIE_LINK_DOWN:
		return "INK_DOWN";
	case FIT_SR_DRIVE_SOFT_RESET:
		return "SOFT_RESET";
	case FIT_SR_DRIVE_NEED_FW_DOWNLOAD:
		return "NEED_FW";
	case FIT_SR_DRIVE_INIT_FAULT:
		return "INIT_FAULT";
	case FIT_SR_DRIVE_BUSY_SANITIZE:
		return "BUSY_SANITIZE";
	case FIT_SR_DRIVE_BUSY_ERASE:
		return "BUSY_ERASE";
	case FIT_SR_DRIVE_FW_BOOTING:
		return "FW_BOOTING";
	default:
		return "???";
	}
}

const char *skd_skdev_state_to_str(enum skd_drvr_state state)
{
	switch (state) {
	case SKD_DRVR_STATE_LOAD:
		return "LOAD";
	case SKD_DRVR_STATE_IDLE:
		return "IDLE";
	case SKD_DRVR_STATE_BUSY:
		return "BUSY";
	case SKD_DRVR_STATE_STARTING:
		return "STARTING";
	case SKD_DRVR_STATE_ONLINE:
		return "ONLINE";
	case SKD_DRVR_STATE_PAUSING:
		return "PAUSING";
	case SKD_DRVR_STATE_PAUSED:
		return "PAUSED";
	case SKD_DRVR_STATE_DRAINING_TIMEOUT:
		return "DRAINING_TIMEOUT";
	case SKD_DRVR_STATE_RESTARTING:
		return "RESTARTING";
	case SKD_DRVR_STATE_RESUMING:
		return "RESUMING";
	case SKD_DRVR_STATE_STOPPING:
		return "STOPPING";
	case SKD_DRVR_STATE_SYNCING:
		return "SYNCING";
	case SKD_DRVR_STATE_FAULT:
		return "FAULT";
	case SKD_DRVR_STATE_DISAPPEARED:
		return "DISAPPEARED";
	case SKD_DRVR_STATE_BUSY_ERASE:
		return "BUSY_ERASE";
	case SKD_DRVR_STATE_BUSY_SANITIZE:
		return "BUSY_SANITIZE";
	case SKD_DRVR_STATE_BUSY_IMMINENT:
		return "BUSY_IMMINENT";
	case SKD_DRVR_STATE_WAIT_BOOT:
		return "WAIT_BOOT";

	default:
		return "???";
	}
}

static const char *skd_skmsg_state_to_str(enum skd_fit_msg_state state)
{
	switch (state) {
	case SKD_MSG_STATE_IDLE:
		return "IDLE";
	case SKD_MSG_STATE_BUSY:
		return "BUSY";
	default:
		return "???";
	}
}

static const char *skd_skreq_state_to_str(enum skd_req_state state)
{
	switch (state) {
	case SKD_REQ_STATE_IDLE:
		return "IDLE";
	case SKD_REQ_STATE_SETUP:
		return "SETUP";
	case SKD_REQ_STATE_BUSY:
		return "BUSY";
	case SKD_REQ_STATE_COMPLETED:
		return "COMPLETED";
	case SKD_REQ_STATE_TIMEOUT:
		return "TIMEOUT";
	case SKD_REQ_STATE_ABORTED:
		return "ABORTED";
	default:
		return "???";
	}
}

static void skd_log_skdev(struct skd_device *skdev, const char *event)
{
	pr_debug("%s:%s:%d (%s) skdev=%p event='%s'\n",
		 skdev->name, __func__, __LINE__, skdev->name, skdev, event);
	pr_debug("%s:%s:%d   drive_state=%s(%d) driver_state=%s(%d)\n",
		 skdev->name, __func__, __LINE__,
		 skd_drive_state_to_str(skdev->drive_state), skdev->drive_state,
		 skd_skdev_state_to_str(skdev->state), skdev->state);
	pr_debug("%s:%s:%d   busy=%d limit=%d dev=%d lowat=%d\n",
		 skdev->name, __func__, __LINE__,
		 skdev->in_flight, skdev->cur_max_queue_depth,
		 skdev->dev_max_queue_depth, skdev->queue_low_water_mark);
	pr_debug("%s:%s:%d   timestamp=0x%x cycle=%d cycle_ix=%d\n",
		 skdev->name, __func__, __LINE__,
		 skdev->timeout_stamp, skdev->skcomp_cycle, skdev->skcomp_ix);
}

static void skd_log_skmsg(struct skd_device *skdev,
			  struct skd_fitmsg_context *skmsg, const char *event)
{
	pr_debug("%s:%s:%d (%s) skmsg=%p event='%s'\n",
		 skdev->name, __func__, __LINE__, skdev->name, skmsg, event);
	pr_debug("%s:%s:%d   state=%s(%d) id=0x%04x length=%d\n",
		 skdev->name, __func__, __LINE__,
		 skd_skmsg_state_to_str(skmsg->state), skmsg->state,
		 skmsg->id, skmsg->length);
}

static void skd_log_skreq(struct skd_device *skdev,
			  struct skd_request_context *skreq, const char *event)
{
	pr_debug("%s:%s:%d (%s) skreq=%p event='%s'\n",
		 skdev->name, __func__, __LINE__, skdev->name, skreq, event);
	pr_debug("%s:%s:%d   state=%s(%d) id=0x%04x fitmsg=0x%04x\n",
		 skdev->name, __func__, __LINE__,
		 skd_skreq_state_to_str(skreq->state), skreq->state,
		 skreq->id, skreq->fitmsg_id);
	pr_debug("%s:%s:%d   timo=0x%x sg_dir=%d n_sg=%d\n",
		 skdev->name, __func__, __LINE__,
		 skreq->timeout_stamp, skreq->sg_data_dir, skreq->n_sg);

	if (skreq->req != NULL) {
		struct request *req = skreq->req;
		u32 lba = (u32)blk_rq_pos(req);
		u32 count = blk_rq_sectors(req);

		pr_debug("%s:%s:%d "
			 "req=%p lba=%u(0x%x) count=%u(0x%x) dir=%d\n",
			 skdev->name, __func__, __LINE__,
			 req, lba, lba, count, count,
			 (int)rq_data_dir(req));
	} else
		pr_debug("%s:%s:%d req=NULL\n",
			 skdev->name, __func__, __LINE__);
}

/*
 *****************************************************************************
 * MODULE GLUE
 *****************************************************************************
 */

static int __init skd_init(void)
{
	pr_info(PFX " v%s-b%s loaded\n", DRV_VERSION, DRV_BUILD_ID);

	switch (skd_isr_type) {
	case SKD_IRQ_LEGACY:
	case SKD_IRQ_MSI:
	case SKD_IRQ_MSIX:
		break;
	default:
		pr_err(PFX "skd_isr_type %d invalid, re-set to %d\n",
		       skd_isr_type, SKD_IRQ_DEFAULT);
		skd_isr_type = SKD_IRQ_DEFAULT;
	}

	if (skd_max_queue_depth < 1 ||
	    skd_max_queue_depth > SKD_MAX_QUEUE_DEPTH) {
		pr_err(PFX "skd_max_queue_depth %d invalid, re-set to %d\n",
		       skd_max_queue_depth, SKD_MAX_QUEUE_DEPTH_DEFAULT);
		skd_max_queue_depth = SKD_MAX_QUEUE_DEPTH_DEFAULT;
	}

	if (skd_max_req_per_msg < 1 || skd_max_req_per_msg > 14) {
		pr_err(PFX "skd_max_req_per_msg %d invalid, re-set to %d\n",
		       skd_max_req_per_msg, SKD_MAX_REQ_PER_MSG_DEFAULT);
		skd_max_req_per_msg = SKD_MAX_REQ_PER_MSG_DEFAULT;
	}

	if (skd_sgs_per_request < 1 || skd_sgs_per_request > 4096) {
		pr_err(PFX "skd_sg_per_request %d invalid, re-set to %d\n",
		       skd_sgs_per_request, SKD_N_SG_PER_REQ_DEFAULT);
		skd_sgs_per_request = SKD_N_SG_PER_REQ_DEFAULT;
	}

	if (skd_dbg_level < 0 || skd_dbg_level > 2) {
		pr_err(PFX "skd_dbg_level %d invalid, re-set to %d\n",
		       skd_dbg_level, 0);
		skd_dbg_level = 0;
	}

	if (skd_isr_comp_limit < 0) {
		pr_err(PFX "skd_isr_comp_limit %d invalid, set to %d\n",
		       skd_isr_comp_limit, 0);
		skd_isr_comp_limit = 0;
	}

	if (skd_max_pass_thru < 1 || skd_max_pass_thru > 50) {
		pr_err(PFX "skd_max_pass_thru %d invalid, re-set to %d\n",
		       skd_max_pass_thru, SKD_N_SPECIAL_CONTEXT);
		skd_max_pass_thru = SKD_N_SPECIAL_CONTEXT;
	}

	return pci_register_driver(&skd_driver);
}

static void __exit skd_exit(void)
{
	pr_info(PFX " v%s-b%s unloading\n", DRV_VERSION, DRV_BUILD_ID);

	pci_unregister_driver(&skd_driver);

	if (skd_major)
		unregister_blkdev(skd_major, DRV_NAME);
}

module_init(skd_init);
module_exit(skd_exit);
