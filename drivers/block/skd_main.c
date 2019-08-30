// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for sTec s1120 PCIe SSDs. sTec was acquired in 2013 by HGST and HGST
 * was acquired by Western Digital in 2012.
 *
 * Copyright 2012 sTec, Inc.
 * Copyright (c) 2017 Western Digital Corporation or its affiliates.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/blkdev.h>
#include <linux/blk-mq.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/compiler.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/time.h>
#include <linux/hdreg.h>
#include <linux/dma-mapping.h>
#include <linux/completion.h>
#include <linux/scatterlist.h>
#include <linux/version.h>
#include <linux/err.h>
#include <linux/aer.h>
#include <linux/wait.h>
#include <linux/stringify.h>
#include <scsi/scsi.h>
#include <scsi/sg.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <asm/unaligned.h>

#include "skd_s1120.h"

static int skd_dbg_level;
static int skd_isr_comp_limit = 4;

#define SKD_ASSERT(expr) \
	do { \
		if (unlikely(!(expr))) { \
			pr_err("Assertion failed! %s,%s,%s,line=%d\n",	\
			       # expr, __FILE__, __func__, __LINE__); \
		} \
	} while (0)

#define DRV_NAME "skd"
#define PFX DRV_NAME ": "

MODULE_LICENSE("GPL");

MODULE_DESCRIPTION("STEC s1120 PCIe SSD block driver");

#define PCI_VENDOR_ID_STEC      0x1B39
#define PCI_DEVICE_ID_S1120     0x0001

#define SKD_FUA_NV		(1 << 1)
#define SKD_MINORS_PER_DEVICE   16

#define SKD_MAX_QUEUE_DEPTH     200u

#define SKD_PAUSE_TIMEOUT       (5 * 1000)

#define SKD_N_FITMSG_BYTES      (512u)
#define SKD_MAX_REQ_PER_MSG	14

#define SKD_N_SPECIAL_FITMSG_BYTES      (128u)

/* SG elements are 32 bytes, so we can make this 4096 and still be under the
 * 128KB limit.  That allows 4096*4K = 16M xfer size
 */
#define SKD_N_SG_PER_REQ_DEFAULT 256u

#define SKD_N_COMPLETION_ENTRY  256u
#define SKD_N_READ_CAP_BYTES    (8u)

#define SKD_N_INTERNAL_BYTES    (512u)

#define SKD_SKCOMP_SIZE							\
	((sizeof(struct fit_completion_entry_v1) +			\
	  sizeof(struct fit_comp_error_info)) * SKD_N_COMPLETION_ENTRY)

/* 5 bits of uniqifier, 0xF800 */
#define SKD_ID_TABLE_MASK       (3u << 8u)
#define  SKD_ID_RW_REQUEST      (0u << 8u)
#define  SKD_ID_INTERNAL        (1u << 8u)
#define  SKD_ID_FIT_MSG         (3u << 8u)
#define SKD_ID_SLOT_MASK        0x00FFu
#define SKD_ID_SLOT_AND_TABLE_MASK 0x03FFu

#define SKD_N_MAX_SECTORS 2048u

#define SKD_MAX_RETRIES 2u

#define SKD_TIMER_SECONDS(seconds) (seconds)
#define SKD_TIMER_MINUTES(minutes) ((minutes) * (60))

#define INQ_STD_NBYTES 36

enum skd_drvr_state {
	SKD_DRVR_STATE_LOAD,
	SKD_DRVR_STATE_IDLE,
	SKD_DRVR_STATE_BUSY,
	SKD_DRVR_STATE_STARTING,
	SKD_DRVR_STATE_ONLINE,
	SKD_DRVR_STATE_PAUSING,
	SKD_DRVR_STATE_PAUSED,
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
#define SKD_BUSY_TIMO           SKD_TIMER_MINUTES(20u)
#define SKD_STARTED_BUSY_TIMO   SKD_TIMER_SECONDS(60u)
#define SKD_START_WAIT_SECONDS  90u

enum skd_req_state {
	SKD_REQ_STATE_IDLE,
	SKD_REQ_STATE_SETUP,
	SKD_REQ_STATE_BUSY,
	SKD_REQ_STATE_COMPLETED,
	SKD_REQ_STATE_TIMEOUT,
};

enum skd_check_status_action {
	SKD_CHECK_STATUS_REPORT_GOOD,
	SKD_CHECK_STATUS_REPORT_SMART_ALERT,
	SKD_CHECK_STATUS_REQUEUE_REQUEST,
	SKD_CHECK_STATUS_REPORT_ERROR,
	SKD_CHECK_STATUS_BUSY_IMMINENT,
};

struct skd_msg_buf {
	struct fit_msg_hdr	fmh;
	struct skd_scsi_request	scsi[SKD_MAX_REQ_PER_MSG];
};

struct skd_fitmsg_context {
	u32 id;

	u32 length;

	struct skd_msg_buf *msg_buf;
	dma_addr_t mb_dma_address;
};

struct skd_request_context {
	enum skd_req_state state;

	u16 id;
	u32 fitmsg_id;

	u8 flush_cmd;

	enum dma_data_direction data_dir;
	struct scatterlist *sg;
	u32 n_sg;
	u32 sg_byte_count;

	struct fit_sg_descriptor *sksg_list;
	dma_addr_t sksg_dma_address;

	struct fit_completion_entry_v1 completion;

	struct fit_comp_error_info err_info;
	int retries;

	blk_status_t status;
};

struct skd_special_context {
	struct skd_request_context req;

	void *data_buf;
	dma_addr_t db_dma_address;

	struct skd_msg_buf *msg_buf;
	dma_addr_t mb_dma_address;
};

typedef enum skd_irq_type {
	SKD_IRQ_LEGACY,
	SKD_IRQ_MSI,
	SKD_IRQ_MSIX
} skd_irq_type_t;

#define SKD_MAX_BARS                    2

struct skd_device {
	void __iomem *mem_map[SKD_MAX_BARS];
	resource_size_t mem_phys[SKD_MAX_BARS];
	u32 mem_size[SKD_MAX_BARS];

	struct skd_msix_entry *msix_entries;

	struct pci_dev *pdev;
	int pcie_error_reporting_is_enabled;

	spinlock_t lock;
	struct gendisk *disk;
	struct blk_mq_tag_set tag_set;
	struct request_queue *queue;
	struct skd_fitmsg_context *skmsg;
	struct device *class_dev;
	int gendisk_on;
	int sync_done;

	u32 devno;
	u32 major;
	char isr_name[30];

	enum skd_drvr_state state;
	u32 drive_state;

	u32 cur_max_queue_depth;
	u32 queue_low_water_mark;
	u32 dev_max_queue_depth;

	u32 num_fitmsg_context;
	u32 num_req_context;

	struct skd_fitmsg_context *skmsg_table;

	struct skd_special_context internal_skspcl;
	u32 read_cap_blocksize;
	u32 read_cap_last_lba;
	int read_cap_is_valid;
	int inquiry_is_valid;
	u8 inq_serial_num[13];  /*12 chars plus null term */

	u8 skcomp_cycle;
	u32 skcomp_ix;
	struct kmem_cache *msgbuf_cache;
	struct kmem_cache *sglist_cache;
	struct kmem_cache *databuf_cache;
	struct fit_completion_entry_v1 *skcomp_table;
	struct fit_comp_error_info *skerr_table;
	dma_addr_t cq_dma_address;

	wait_queue_head_t waitq;

	struct timer_list timer;
	u32 timer_countdown;
	u32 timer_substate;

	int sgs_per_request;
	u32 last_mtd;

	u32 proto_ver;

	int dbg_level;
	u32 connect_time_stamp;
	int connect_retries;
#define SKD_MAX_CONNECT_RETRIES 16
	u32 drive_jiffies;

	u32 timo_slot;

	struct work_struct start_queue;
	struct work_struct completion_worker;
};

#define SKD_WRITEL(DEV, VAL, OFF) skd_reg_write32(DEV, VAL, OFF)
#define SKD_READL(DEV, OFF)      skd_reg_read32(DEV, OFF)
#define SKD_WRITEQ(DEV, VAL, OFF) skd_reg_write64(DEV, VAL, OFF)

static inline u32 skd_reg_read32(struct skd_device *skdev, u32 offset)
{
	u32 val = readl(skdev->mem_map[1] + offset);

	if (unlikely(skdev->dbg_level >= 2))
		dev_dbg(&skdev->pdev->dev, "offset %x = %x\n", offset, val);
	return val;
}

static inline void skd_reg_write32(struct skd_device *skdev, u32 val,
				   u32 offset)
{
	writel(val, skdev->mem_map[1] + offset);
	if (unlikely(skdev->dbg_level >= 2))
		dev_dbg(&skdev->pdev->dev, "offset %x = %x\n", offset, val);
}

static inline void skd_reg_write64(struct skd_device *skdev, u64 val,
				   u32 offset)
{
	writeq(val, skdev->mem_map[1] + offset);
	if (unlikely(skdev->dbg_level >= 2))
		dev_dbg(&skdev->pdev->dev, "offset %x = %016llx\n", offset,
			val);
}


#define SKD_IRQ_DEFAULT SKD_IRQ_MSIX
static int skd_isr_type = SKD_IRQ_DEFAULT;

module_param(skd_isr_type, int, 0444);
MODULE_PARM_DESC(skd_isr_type, "Interrupt type capability."
		 " (0==legacy, 1==MSI, 2==MSI-X, default==1)");

#define SKD_MAX_REQ_PER_MSG_DEFAULT 1
static int skd_max_req_per_msg = SKD_MAX_REQ_PER_MSG_DEFAULT;

module_param(skd_max_req_per_msg, int, 0444);
MODULE_PARM_DESC(skd_max_req_per_msg,
		 "Maximum SCSI requests packed in a single message."
		 " (1-" __stringify(SKD_MAX_REQ_PER_MSG) ", default==1)");

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

static int skd_max_pass_thru = 1;
module_param(skd_max_pass_thru, int, 0444);
MODULE_PARM_DESC(skd_max_pass_thru,
		 "Maximum SCSI pass-thru at a time. IGNORED");

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
static bool skd_preop_sg_list(struct skd_device *skdev,
			     struct skd_request_context *skreq);
static void skd_postop_sg_list(struct skd_device *skdev,
			       struct skd_request_context *skreq);

static void skd_restart_device(struct skd_device *skdev);
static int skd_quiesce_dev(struct skd_device *skdev);
static int skd_unquiesce_dev(struct skd_device *skdev);
static void skd_disable_interrupts(struct skd_device *skdev);
static void skd_isr_fwstate(struct skd_device *skdev);
static void skd_recover_requests(struct skd_device *skdev);
static void skd_soft_reset(struct skd_device *skdev);

const char *skd_drive_state_to_str(int state);
const char *skd_skdev_state_to_str(enum skd_drvr_state state);
static void skd_log_skdev(struct skd_device *skdev, const char *event);
static void skd_log_skreq(struct skd_device *skdev,
			  struct skd_request_context *skreq, const char *event);

/*
 *****************************************************************************
 * READ/WRITE REQUESTS
 *****************************************************************************
 */
static bool skd_inc_in_flight(struct request *rq, void *data, bool reserved)
{
	int *count = data;

	count++;
	return true;
}

static int skd_in_flight(struct skd_device *skdev)
{
	int count = 0;

	blk_mq_tagset_busy_iter(&skdev->tag_set, skd_inc_in_flight, &count);

	return count;
}

static void
skd_prep_rw_cdb(struct skd_scsi_request *scsi_req,
		int data_dir, unsigned lba,
		unsigned count)
{
	if (data_dir == READ)
		scsi_req->cdb[0] = READ_10;
	else
		scsi_req->cdb[0] = WRITE_10;

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

	scsi_req->cdb[0] = SYNCHRONIZE_CACHE;
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

/*
 * Return true if and only if all pending requests should be failed.
 */
static bool skd_fail_all(struct request_queue *q)
{
	struct skd_device *skdev = q->queuedata;

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
		return false;

	case SKD_DRVR_STATE_BUSY_SANITIZE:
	case SKD_DRVR_STATE_STOPPING:
	case SKD_DRVR_STATE_SYNCING:
	case SKD_DRVR_STATE_FAULT:
	case SKD_DRVR_STATE_DISAPPEARED:
	default:
		return true;
	}
}

static blk_status_t skd_mq_queue_rq(struct blk_mq_hw_ctx *hctx,
				    const struct blk_mq_queue_data *mqd)
{
	struct request *const req = mqd->rq;
	struct request_queue *const q = req->q;
	struct skd_device *skdev = q->queuedata;
	struct skd_fitmsg_context *skmsg;
	struct fit_msg_hdr *fmh;
	const u32 tag = blk_mq_unique_tag(req);
	struct skd_request_context *const skreq = blk_mq_rq_to_pdu(req);
	struct skd_scsi_request *scsi_req;
	unsigned long flags = 0;
	const u32 lba = blk_rq_pos(req);
	const u32 count = blk_rq_sectors(req);
	const int data_dir = rq_data_dir(req);

	if (unlikely(skdev->state != SKD_DRVR_STATE_ONLINE))
		return skd_fail_all(q) ? BLK_STS_IOERR : BLK_STS_RESOURCE;

	if (!(req->rq_flags & RQF_DONTPREP)) {
		skreq->retries = 0;
		req->rq_flags |= RQF_DONTPREP;
	}

	blk_mq_start_request(req);

	WARN_ONCE(tag >= skd_max_queue_depth, "%#x > %#x (nr_requests = %lu)\n",
		  tag, skd_max_queue_depth, q->nr_requests);

	SKD_ASSERT(skreq->state == SKD_REQ_STATE_IDLE);

	dev_dbg(&skdev->pdev->dev,
		"new req=%p lba=%u(0x%x) count=%u(0x%x) dir=%d\n", req, lba,
		lba, count, count, data_dir);

	skreq->id = tag + SKD_ID_RW_REQUEST;
	skreq->flush_cmd = 0;
	skreq->n_sg = 0;
	skreq->sg_byte_count = 0;

	skreq->fitmsg_id = 0;

	skreq->data_dir = data_dir == READ ? DMA_FROM_DEVICE : DMA_TO_DEVICE;

	if (req->bio && !skd_preop_sg_list(skdev, skreq)) {
		dev_dbg(&skdev->pdev->dev, "error Out\n");
		skreq->status = BLK_STS_RESOURCE;
		blk_mq_complete_request(req);
		return BLK_STS_OK;
	}

	dma_sync_single_for_device(&skdev->pdev->dev, skreq->sksg_dma_address,
				   skreq->n_sg *
				   sizeof(struct fit_sg_descriptor),
				   DMA_TO_DEVICE);

	/* Either a FIT msg is in progress or we have to start one. */
	if (skd_max_req_per_msg == 1) {
		skmsg = NULL;
	} else {
		spin_lock_irqsave(&skdev->lock, flags);
		skmsg = skdev->skmsg;
	}
	if (!skmsg) {
		skmsg = &skdev->skmsg_table[tag];
		skdev->skmsg = skmsg;

		/* Initialize the FIT msg header */
		fmh = &skmsg->msg_buf->fmh;
		memset(fmh, 0, sizeof(*fmh));
		fmh->protocol_id = FIT_PROTOCOL_ID_SOFIT;
		skmsg->length = sizeof(*fmh);
	} else {
		fmh = &skmsg->msg_buf->fmh;
	}

	skreq->fitmsg_id = skmsg->id;

	scsi_req = &skmsg->msg_buf->scsi[fmh->num_protocol_cmds_coalesced];
	memset(scsi_req, 0, sizeof(*scsi_req));

	scsi_req->hdr.tag = skreq->id;
	scsi_req->hdr.sg_list_dma_address =
		cpu_to_be64(skreq->sksg_dma_address);

	if (req_op(req) == REQ_OP_FLUSH) {
		skd_prep_zerosize_flush_cdb(scsi_req, skreq);
		SKD_ASSERT(skreq->flush_cmd == 1);
	} else {
		skd_prep_rw_cdb(scsi_req, data_dir, lba, count);
	}

	if (req->cmd_flags & REQ_FUA)
		scsi_req->cdb[1] |= SKD_FUA_NV;

	scsi_req->hdr.sg_list_len_bytes = cpu_to_be32(skreq->sg_byte_count);

	/* Complete resource allocations. */
	skreq->state = SKD_REQ_STATE_BUSY;

	skmsg->length += sizeof(struct skd_scsi_request);
	fmh->num_protocol_cmds_coalesced++;

	dev_dbg(&skdev->pdev->dev, "req=0x%x busy=%d\n", skreq->id,
		skd_in_flight(skdev));

	/*
	 * If the FIT msg buffer is full send it.
	 */
	if (skd_max_req_per_msg == 1) {
		skd_send_fitmsg(skdev, skmsg);
	} else {
		if (mqd->last ||
		    fmh->num_protocol_cmds_coalesced >= skd_max_req_per_msg) {
			skd_send_fitmsg(skdev, skmsg);
			skdev->skmsg = NULL;
		}
		spin_unlock_irqrestore(&skdev->lock, flags);
	}

	return BLK_STS_OK;
}

static enum blk_eh_timer_return skd_timed_out(struct request *req,
					      bool reserved)
{
	struct skd_device *skdev = req->q->queuedata;

	dev_err(&skdev->pdev->dev, "request with tag %#x timed out\n",
		blk_mq_unique_tag(req));

	return BLK_EH_RESET_TIMER;
}

static void skd_complete_rq(struct request *req)
{
	struct skd_request_context *skreq = blk_mq_rq_to_pdu(req);

	blk_mq_end_request(req, skreq->status);
}

static bool skd_preop_sg_list(struct skd_device *skdev,
			     struct skd_request_context *skreq)
{
	struct request *req = blk_mq_rq_from_pdu(skreq);
	struct scatterlist *sgl = &skreq->sg[0], *sg;
	int n_sg;
	int i;

	skreq->sg_byte_count = 0;

	WARN_ON_ONCE(skreq->data_dir != DMA_TO_DEVICE &&
		     skreq->data_dir != DMA_FROM_DEVICE);

	n_sg = blk_rq_map_sg(skdev->queue, req, sgl);
	if (n_sg <= 0)
		return false;

	/*
	 * Map scatterlist to PCI bus addresses.
	 * Note PCI might change the number of entries.
	 */
	n_sg = dma_map_sg(&skdev->pdev->dev, sgl, n_sg, skreq->data_dir);
	if (n_sg <= 0)
		return false;

	SKD_ASSERT(n_sg <= skdev->sgs_per_request);

	skreq->n_sg = n_sg;

	for_each_sg(sgl, sg, n_sg, i) {
		struct fit_sg_descriptor *sgd = &skreq->sksg_list[i];
		u32 cnt = sg_dma_len(sg);
		uint64_t dma_addr = sg_dma_address(sg);

		sgd->control = FIT_SGD_CONTROL_NOT_LAST;
		sgd->byte_count = cnt;
		skreq->sg_byte_count += cnt;
		sgd->host_side_addr = dma_addr;
		sgd->dev_side_addr = 0;
	}

	skreq->sksg_list[n_sg - 1].next_desc_ptr = 0LL;
	skreq->sksg_list[n_sg - 1].control = FIT_SGD_CONTROL_LAST;

	if (unlikely(skdev->dbg_level > 1)) {
		dev_dbg(&skdev->pdev->dev,
			"skreq=%x sksg_list=%p sksg_dma=%pad\n",
			skreq->id, skreq->sksg_list, &skreq->sksg_dma_address);
		for (i = 0; i < n_sg; i++) {
			struct fit_sg_descriptor *sgd = &skreq->sksg_list[i];

			dev_dbg(&skdev->pdev->dev,
				"  sg[%d] count=%u ctrl=0x%x addr=0x%llx next=0x%llx\n",
				i, sgd->byte_count, sgd->control,
				sgd->host_side_addr, sgd->next_desc_ptr);
		}
	}

	return true;
}

static void skd_postop_sg_list(struct skd_device *skdev,
			       struct skd_request_context *skreq)
{
	/*
	 * restore the next ptr for next IO request so we
	 * don't have to set it every time.
	 */
	skreq->sksg_list[skreq->n_sg - 1].next_desc_ptr =
		skreq->sksg_dma_address +
		((skreq->n_sg) * sizeof(struct fit_sg_descriptor));
	dma_unmap_sg(&skdev->pdev->dev, &skreq->sg[0], skreq->n_sg,
		     skreq->data_dir);
}

/*
 *****************************************************************************
 * TIMER
 *****************************************************************************
 */

static void skd_timer_tick_not_online(struct skd_device *skdev);

static void skd_start_queue(struct work_struct *work)
{
	struct skd_device *skdev = container_of(work, typeof(*skdev),
						start_queue);

	/*
	 * Although it is safe to call blk_start_queue() from interrupt
	 * context, blk_mq_start_hw_queues() must not be called from
	 * interrupt context.
	 */
	blk_mq_start_hw_queues(skdev->queue);
}

static void skd_timer_tick(struct timer_list *t)
{
	struct skd_device *skdev = from_timer(skdev, t, timer);
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

	if (skdev->state != SKD_DRVR_STATE_ONLINE)
		skd_timer_tick_not_online(skdev);

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
		dev_dbg(&skdev->pdev->dev,
			"drive busy sanitize[%x], driver[%x]\n",
			skdev->drive_state, skdev->state);
		/* If we've been in sanitize for 3 seconds, we figure we're not
		 * going to get anymore completions, so recover requests now
		 */
		if (skdev->timer_countdown > 0) {
			skdev->timer_countdown--;
			return;
		}
		skd_recover_requests(skdev);
		break;

	case SKD_DRVR_STATE_BUSY:
	case SKD_DRVR_STATE_BUSY_IMMINENT:
	case SKD_DRVR_STATE_BUSY_ERASE:
		dev_dbg(&skdev->pdev->dev, "busy[%x], countdown=%d\n",
			skdev->state, skdev->timer_countdown);
		if (skdev->timer_countdown > 0) {
			skdev->timer_countdown--;
			return;
		}
		dev_dbg(&skdev->pdev->dev,
			"busy[%x], timedout=%d, restarting device.",
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

		dev_err(&skdev->pdev->dev, "DriveFault Connect Timeout (%x)\n",
			skdev->drive_state);

		/*start the queue so we can respond with error to requests */
		/* wakeup anyone waiting for startup complete */
		schedule_work(&skdev->start_queue);
		skdev->gendisk_on = -1;
		wake_up_interruptible(&skdev->waitq);
		break;

	case SKD_DRVR_STATE_ONLINE:
		/* shouldn't get here. */
		break;

	case SKD_DRVR_STATE_PAUSING:
	case SKD_DRVR_STATE_PAUSED:
		break;

	case SKD_DRVR_STATE_RESTARTING:
		if (skdev->timer_countdown > 0) {
			skdev->timer_countdown--;
			return;
		}
		/* For now, we fault the drive. Could attempt resets to
		 * revcover at some point. */
		skdev->state = SKD_DRVR_STATE_FAULT;
		dev_err(&skdev->pdev->dev,
			"DriveFault Reconnect Timeout (%x)\n",
			skdev->drive_state);

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
			skd_recover_requests(skdev);
		else {
			dev_err(&skdev->pdev->dev, "Disable BusMaster (%x)\n",
				skdev->drive_state);
			pci_disable_device(skdev->pdev);
			skd_disable_interrupts(skdev);
			skd_recover_requests(skdev);
		}

		/*start the queue so we can respond with error to requests */
		/* wakeup anyone waiting for startup complete */
		schedule_work(&skdev->start_queue);
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

	timer_setup(&skdev->timer, skd_timer_tick, 0);

	rc = mod_timer(&skdev->timer, (jiffies + HZ));
	if (rc)
		dev_err(&skdev->pdev->dev, "failed to start timer %d\n", rc);
	return rc;
}

static void skd_kill_timer(struct skd_device *skdev)
{
	del_timer_sync(&skdev->timer);
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

	fmh = &skspcl->msg_buf->fmh;
	fmh->protocol_id = FIT_PROTOCOL_ID_SOFIT;
	fmh->num_protocol_cmds_coalesced = 1;

	scsi = &skspcl->msg_buf->scsi[0];
	memset(scsi, 0, sizeof(*scsi));
	dma_address = skspcl->req.sksg_dma_address;
	scsi->hdr.sg_list_dma_address = cpu_to_be64(dma_address);
	skspcl->req.n_sg = 1;
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

	skspcl->req.state = SKD_REQ_STATE_BUSY;

	scsi = &skspcl->msg_buf->scsi[0];
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
		dev_err(&skdev->pdev->dev,
			"*** LOST_WRITE_DATA ERROR *** key/asc/ascq/fruc %02x/%02x/%02x/%02x\n",
			key, code, qual, fruc);
	}
}

static void skd_complete_internal(struct skd_device *skdev,
				  struct fit_completion_entry_v1 *skcomp,
				  struct fit_comp_error_info *skerr,
				  struct skd_special_context *skspcl)
{
	u8 *buf = skspcl->data_buf;
	u8 status;
	int i;
	struct skd_scsi_request *scsi = &skspcl->msg_buf->scsi[0];

	lockdep_assert_held(&skdev->lock);

	SKD_ASSERT(skspcl == &skdev->internal_skspcl);

	dev_dbg(&skdev->pdev->dev, "complete internal %x\n", scsi->cdb[0]);

	dma_sync_single_for_cpu(&skdev->pdev->dev,
				skspcl->db_dma_address,
				skspcl->req.sksg_list[0].byte_count,
				DMA_BIDIRECTIONAL);

	skspcl->req.completion = *skcomp;
	skspcl->req.state = SKD_REQ_STATE_IDLE;

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
				dev_dbg(&skdev->pdev->dev,
					"TUR failed, don't send anymore state 0x%x\n",
					skdev->state);
				return;
			}
			dev_dbg(&skdev->pdev->dev,
				"**** TUR failed, retry skerr\n");
			skd_send_internal_skspcl(skdev, skspcl,
						 TEST_UNIT_READY);
		}
		break;

	case WRITE_BUFFER:
		if (status == SAM_STAT_GOOD)
			skd_send_internal_skspcl(skdev, skspcl, READ_BUFFER);
		else {
			if (skdev->state == SKD_DRVR_STATE_STOPPING) {
				dev_dbg(&skdev->pdev->dev,
					"write buffer failed, don't send anymore state 0x%x\n",
					skdev->state);
				return;
			}
			dev_dbg(&skdev->pdev->dev,
				"**** write buffer failed, retry skerr\n");
			skd_send_internal_skspcl(skdev, skspcl,
						 TEST_UNIT_READY);
		}
		break;

	case READ_BUFFER:
		if (status == SAM_STAT_GOOD) {
			if (skd_chk_read_buf(skdev, skspcl) == 0)
				skd_send_internal_skspcl(skdev, skspcl,
							 READ_CAPACITY);
			else {
				dev_err(&skdev->pdev->dev,
					"*** W/R Buffer mismatch %d ***\n",
					skdev->connect_retries);
				if (skdev->connect_retries <
				    SKD_MAX_CONNECT_RETRIES) {
					skdev->connect_retries++;
					skd_soft_reset(skdev);
				} else {
					dev_err(&skdev->pdev->dev,
						"W/R Buffer Connect Error\n");
					return;
				}
			}

		} else {
			if (skdev->state == SKD_DRVR_STATE_STOPPING) {
				dev_dbg(&skdev->pdev->dev,
					"read buffer failed, don't send anymore state 0x%x\n",
					skdev->state);
				return;
			}
			dev_dbg(&skdev->pdev->dev,
				"**** read buffer failed, retry skerr\n");
			skd_send_internal_skspcl(skdev, skspcl,
						 TEST_UNIT_READY);
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

			dev_dbg(&skdev->pdev->dev, "last lba %d, bs %d\n",
				skdev->read_cap_last_lba,
				skdev->read_cap_blocksize);

			set_capacity(skdev->disk, skdev->read_cap_last_lba + 1);

			skdev->read_cap_is_valid = 1;

			skd_send_internal_skspcl(skdev, skspcl, INQUIRY);
		} else if ((status == SAM_STAT_CHECK_CONDITION) &&
			   (skerr->key == MEDIUM_ERROR)) {
			skdev->read_cap_last_lba = ~0;
			set_capacity(skdev->disk, skdev->read_cap_last_lba + 1);
			dev_dbg(&skdev->pdev->dev, "**** MEDIUM ERROR caused READCAP to fail, ignore failure and continue to inquiry\n");
			skd_send_internal_skspcl(skdev, skspcl, INQUIRY);
		} else {
			dev_dbg(&skdev->pdev->dev, "**** READCAP failed, retry TUR\n");
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
			dev_dbg(&skdev->pdev->dev, "**** failed, to ONLINE device\n");
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

	dev_dbg(&skdev->pdev->dev, "dma address %pad, busy=%d\n",
		&skmsg->mb_dma_address, skd_in_flight(skdev));
	dev_dbg(&skdev->pdev->dev, "msg_buf %p\n", skmsg->msg_buf);

	qcmd = skmsg->mb_dma_address;
	qcmd |= FIT_QCMD_QID_NORMAL;

	if (unlikely(skdev->dbg_level > 1)) {
		u8 *bp = (u8 *)skmsg->msg_buf;
		int i;
		for (i = 0; i < skmsg->length; i += 8) {
			dev_dbg(&skdev->pdev->dev, "msg[%2d] %8ph\n", i,
				&bp[i]);
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

	dma_sync_single_for_device(&skdev->pdev->dev, skmsg->mb_dma_address,
				   skmsg->length, DMA_TO_DEVICE);

	/* Make sure skd_msg_buf is written before the doorbell is triggered. */
	smp_wmb();

	SKD_WRITEQ(skdev, qcmd, FIT_Q_COMMAND);
}

static void skd_send_special_fitmsg(struct skd_device *skdev,
				    struct skd_special_context *skspcl)
{
	u64 qcmd;

	WARN_ON_ONCE(skspcl->req.n_sg != 1);

	if (unlikely(skdev->dbg_level > 1)) {
		u8 *bp = (u8 *)skspcl->msg_buf;
		int i;

		for (i = 0; i < SKD_N_SPECIAL_FITMSG_BYTES; i += 8) {
			dev_dbg(&skdev->pdev->dev, " spcl[%2d] %8ph\n", i,
				&bp[i]);
			if (i == 0)
				i = 64 - 8;
		}

		dev_dbg(&skdev->pdev->dev,
			"skspcl=%p id=%04x sksg_list=%p sksg_dma=%pad\n",
			skspcl, skspcl->req.id, skspcl->req.sksg_list,
			&skspcl->req.sksg_dma_address);
		for (i = 0; i < skspcl->req.n_sg; i++) {
			struct fit_sg_descriptor *sgd =
				&skspcl->req.sksg_list[i];

			dev_dbg(&skdev->pdev->dev,
				"  sg[%d] count=%u ctrl=0x%x addr=0x%llx next=0x%llx\n",
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

	dma_sync_single_for_device(&skdev->pdev->dev, skspcl->mb_dma_address,
				   SKD_N_SPECIAL_FITMSG_BYTES, DMA_TO_DEVICE);
	dma_sync_single_for_device(&skdev->pdev->dev,
				   skspcl->req.sksg_dma_address,
				   1 * sizeof(struct fit_sg_descriptor),
				   DMA_TO_DEVICE);
	dma_sync_single_for_device(&skdev->pdev->dev,
				   skspcl->db_dma_address,
				   skspcl->req.sksg_list[0].byte_count,
				   DMA_BIDIRECTIONAL);

	/* Make sure skd_msg_buf is written before the doorbell is triggered. */
	smp_wmb();

	SKD_WRITEQ(skdev, qcmd, FIT_Q_COMMAND);
}

/*
 *****************************************************************************
 * COMPLETION QUEUE
 *****************************************************************************
 */

static void skd_complete_other(struct skd_device *skdev,
			       struct fit_completion_entry_v1 *skcomp,
			       struct fit_comp_error_info *skerr);

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
		 u8 cmp_status, struct fit_comp_error_info *skerr)
{
	int i;

	dev_err(&skdev->pdev->dev, "key/asc/ascq/fruc %02x/%02x/%02x/%02x\n",
		skerr->key, skerr->code, skerr->qual, skerr->fruc);

	dev_dbg(&skdev->pdev->dev,
		"stat: t=%02x stat=%02x k=%02x c=%02x q=%02x fruc=%02x\n",
		skerr->type, cmp_status, skerr->key, skerr->code, skerr->qual,
		skerr->fruc);

	/* Does the info match an entry in the good category? */
	for (i = 0; i < ARRAY_SIZE(skd_chkstat_table); i++) {
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
			dev_err(&skdev->pdev->dev,
				"SMART Alert: sense key/asc/ascq %02x/%02x/%02x\n",
				skerr->key, skerr->code, skerr->qual);
		}
		return sns->action;
	}

	/* No other match, so nonzero status means error,
	 * zero status means good
	 */
	if (cmp_status) {
		dev_dbg(&skdev->pdev->dev, "status check: error\n");
		return SKD_CHECK_STATUS_REPORT_ERROR;
	}

	dev_dbg(&skdev->pdev->dev, "status check good default\n");
	return SKD_CHECK_STATUS_REPORT_GOOD;
}

static void skd_resolve_req_exception(struct skd_device *skdev,
				      struct skd_request_context *skreq,
				      struct request *req)
{
	u8 cmp_status = skreq->completion.status;

	switch (skd_check_status(skdev, cmp_status, &skreq->err_info)) {
	case SKD_CHECK_STATUS_REPORT_GOOD:
	case SKD_CHECK_STATUS_REPORT_SMART_ALERT:
		skreq->status = BLK_STS_OK;
		blk_mq_complete_request(req);
		break;

	case SKD_CHECK_STATUS_BUSY_IMMINENT:
		skd_log_skreq(skdev, skreq, "retry(busy)");
		blk_mq_requeue_request(req, true);
		dev_info(&skdev->pdev->dev, "drive BUSY imminent\n");
		skdev->state = SKD_DRVR_STATE_BUSY_IMMINENT;
		skdev->timer_countdown = SKD_TIMER_MINUTES(20);
		skd_quiesce_dev(skdev);
		break;

	case SKD_CHECK_STATUS_REQUEUE_REQUEST:
		if (++skreq->retries < SKD_MAX_RETRIES) {
			skd_log_skreq(skdev, skreq, "retry");
			blk_mq_requeue_request(req, true);
			break;
		}
		/* fall through */

	case SKD_CHECK_STATUS_REPORT_ERROR:
	default:
		skreq->status = BLK_STS_IOERR;
		blk_mq_complete_request(req);
		break;
	}
}

static void skd_release_skreq(struct skd_device *skdev,
			      struct skd_request_context *skreq)
{
	/*
	 * Reclaim the skd_request_context
	 */
	skreq->state = SKD_REQ_STATE_IDLE;
}

static int skd_isr_completion_posted(struct skd_device *skdev,
					int limit, int *enqueued)
{
	struct fit_completion_entry_v1 *skcmp;
	struct fit_comp_error_info *skerr;
	u16 req_id;
	u32 tag;
	u16 hwq = 0;
	struct request *rq;
	struct skd_request_context *skreq;
	u16 cmp_cntxt;
	u8 cmp_status;
	u8 cmp_cycle;
	u32 cmp_bytes;
	int rc = 0;
	int processed = 0;

	lockdep_assert_held(&skdev->lock);

	for (;; ) {
		SKD_ASSERT(skdev->skcomp_ix < SKD_N_COMPLETION_ENTRY);

		skcmp = &skdev->skcomp_table[skdev->skcomp_ix];
		cmp_cycle = skcmp->cycle;
		cmp_cntxt = skcmp->tag;
		cmp_status = skcmp->status;
		cmp_bytes = be32_to_cpu(skcmp->num_returned_bytes);

		skerr = &skdev->skerr_table[skdev->skcomp_ix];

		dev_dbg(&skdev->pdev->dev,
			"cycle=%d ix=%d got cycle=%d cmdctxt=0x%x stat=%d busy=%d rbytes=0x%x proto=%d\n",
			skdev->skcomp_cycle, skdev->skcomp_ix, cmp_cycle,
			cmp_cntxt, cmp_status, skd_in_flight(skdev),
			cmp_bytes, skdev->proto_ver);

		if (cmp_cycle != skdev->skcomp_cycle) {
			dev_dbg(&skdev->pdev->dev, "end of completions\n");
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
		tag = req_id & SKD_ID_SLOT_AND_TABLE_MASK;

		/* Is this other than a r/w request? */
		if (tag >= skdev->num_req_context) {
			/*
			 * This is not a completion for a r/w request.
			 */
			WARN_ON_ONCE(blk_mq_tag_to_rq(skdev->tag_set.tags[hwq],
						      tag));
			skd_complete_other(skdev, skcmp, skerr);
			continue;
		}

		rq = blk_mq_tag_to_rq(skdev->tag_set.tags[hwq], tag);
		if (WARN(!rq, "No request for tag %#x -> %#x\n", cmp_cntxt,
			 tag))
			continue;
		skreq = blk_mq_rq_to_pdu(rq);

		/*
		 * Make sure the request ID for the slot matches.
		 */
		if (skreq->id != req_id) {
			dev_err(&skdev->pdev->dev,
				"Completion mismatch comp_id=0x%04x skreq=0x%04x new=0x%04x\n",
				req_id, skreq->id, cmp_cntxt);

			continue;
		}

		SKD_ASSERT(skreq->state == SKD_REQ_STATE_BUSY);

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

		skd_release_skreq(skdev, skreq);

		/*
		 * Capture the outcome and post it back to the native request.
		 */
		if (likely(cmp_status == SAM_STAT_GOOD)) {
			skreq->status = BLK_STS_OK;
			blk_mq_complete_request(rq);
		} else {
			skd_resolve_req_exception(skdev, skreq, rq);
		}

		/* skd_isr_comp_limit equal zero means no limit */
		if (limit) {
			if (++processed >= limit) {
				rc = 1;
				break;
			}
		}
	}

	if (skdev->state == SKD_DRVR_STATE_PAUSING &&
	    skd_in_flight(skdev) == 0) {
		skdev->state = SKD_DRVR_STATE_PAUSED;
		wake_up_interruptible(&skdev->waitq);
	}

	return rc;
}

static void skd_complete_other(struct skd_device *skdev,
			       struct fit_completion_entry_v1 *skcomp,
			       struct fit_comp_error_info *skerr)
{
	u32 req_id = 0;
	u32 req_table;
	u32 req_slot;
	struct skd_special_context *skspcl;

	lockdep_assert_held(&skdev->lock);

	req_id = skcomp->tag;
	req_table = req_id & SKD_ID_TABLE_MASK;
	req_slot = req_id & SKD_ID_SLOT_MASK;

	dev_dbg(&skdev->pdev->dev, "table=0x%x id=0x%x slot=%d\n", req_table,
		req_id, req_slot);

	/*
	 * Based on the request id, determine how to dispatch this completion.
	 * This swich/case is finding the good cases and forwarding the
	 * completion entry. Errors are reported below the switch.
	 */
	switch (req_table) {
	case SKD_ID_RW_REQUEST:
		/*
		 * The caller, skd_isr_completion_posted() above,
		 * handles r/w requests. The only way we get here
		 * is if the req_slot is out of bounds.
		 */
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

static void skd_reset_skcomp(struct skd_device *skdev)
{
	memset(skdev->skcomp_table, 0, SKD_SKCOMP_SIZE);

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
	schedule_work(&skdev->start_queue);

	spin_unlock_irqrestore(&skdev->lock, flags);
}

static void skd_isr_msg_from_dev(struct skd_device *skdev);

static irqreturn_t
skd_isr(int irq, void *ptr)
{
	struct skd_device *skdev = ptr;
	u32 intstat;
	u32 ack;
	int rc = 0;
	int deferred = 0;
	int flush_enqueued = 0;

	spin_lock(&skdev->lock);

	for (;; ) {
		intstat = SKD_READL(skdev, FIT_INT_STATUS_HOST);

		ack = FIT_INT_DEF_MASK;
		ack &= intstat;

		dev_dbg(&skdev->pdev->dev, "intstat=0x%x ack=0x%x\n", intstat,
			ack);

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
		schedule_work(&skdev->start_queue);

	if (deferred)
		schedule_work(&skdev->completion_worker);
	else if (!flush_enqueued)
		schedule_work(&skdev->start_queue);

	spin_unlock(&skdev->lock);

	return rc;
}

static void skd_drive_fault(struct skd_device *skdev)
{
	skdev->state = SKD_DRVR_STATE_FAULT;
	dev_err(&skdev->pdev->dev, "Drive FAULT\n");
}

static void skd_drive_disappeared(struct skd_device *skdev)
{
	skdev->state = SKD_DRVR_STATE_DISAPPEARED;
	dev_err(&skdev->pdev->dev, "Drive DISAPPEARED\n");
}

static void skd_isr_fwstate(struct skd_device *skdev)
{
	u32 sense;
	u32 state;
	u32 mtd;
	int prev_driver_state = skdev->state;

	sense = SKD_READL(skdev, FIT_STATUS);
	state = sense & FIT_SR_DRIVE_STATE_MASK;

	dev_err(&skdev->pdev->dev, "s1120 state %s(%d)=>%s(%d)\n",
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
			skd_recover_requests(skdev);
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
		dev_info(&skdev->pdev->dev,
			 "Queue depth limit=%d dev=%d lowat=%d\n",
			 skdev->cur_max_queue_depth,
			 skdev->dev_max_queue_depth,
			 skdev->queue_low_water_mark);

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
		schedule_work(&skdev->start_queue);
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
		dev_dbg(&skdev->pdev->dev, "ISR FIT_SR_DRIVE_FW_BOOTING\n");
		skdev->state = SKD_DRVR_STATE_WAIT_BOOT;
		skdev->timer_countdown = SKD_WAIT_BOOT_TIMO;
		break;

	case FIT_SR_DRIVE_DEGRADED:
	case FIT_SR_PCIE_LINK_DOWN:
	case FIT_SR_DRIVE_NEED_FW_DOWNLOAD:
		break;

	case FIT_SR_DRIVE_FAULT:
		skd_drive_fault(skdev);
		skd_recover_requests(skdev);
		schedule_work(&skdev->start_queue);
		break;

	/* PCIe bus returned all Fs? */
	case 0xFF:
		dev_info(&skdev->pdev->dev, "state=0x%x sense=0x%x\n", state,
			 sense);
		skd_drive_disappeared(skdev);
		skd_recover_requests(skdev);
		schedule_work(&skdev->start_queue);
		break;
	default:
		/*
		 * Uknown FW State. Wait for a state we recognize.
		 */
		break;
	}
	dev_err(&skdev->pdev->dev, "Driver state %s(%d)=>%s(%d)\n",
		skd_skdev_state_to_str(prev_driver_state), prev_driver_state,
		skd_skdev_state_to_str(skdev->state), skdev->state);
}

static bool skd_recover_request(struct request *req, void *data, bool reserved)
{
	struct skd_device *const skdev = data;
	struct skd_request_context *skreq = blk_mq_rq_to_pdu(req);

	if (skreq->state != SKD_REQ_STATE_BUSY)
		return true;

	skd_log_skreq(skdev, skreq, "recover");

	/* Release DMA resources for the request. */
	if (skreq->n_sg > 0)
		skd_postop_sg_list(skdev, skreq);

	skreq->state = SKD_REQ_STATE_IDLE;
	skreq->status = BLK_STS_IOERR;
	blk_mq_complete_request(req);
	return true;
}

static void skd_recover_requests(struct skd_device *skdev)
{
	blk_mq_tagset_busy_iter(&skdev->tag_set, skd_recover_request, skdev);
}

static void skd_isr_msg_from_dev(struct skd_device *skdev)
{
	u32 mfd;
	u32 mtd;
	u32 data;

	mfd = SKD_READL(skdev, FIT_MSG_FROM_DEVICE);

	dev_dbg(&skdev->pdev->dev, "mfd=0x%x last_mtd=0x%x\n", mfd,
		skdev->last_mtd);

	/* ignore any mtd that is an ack for something we didn't send */
	if (FIT_MXD_TYPE(mfd) != FIT_MXD_TYPE(skdev->last_mtd))
		return;

	switch (FIT_MXD_TYPE(mfd)) {
	case FIT_MTD_FITFW_INIT:
		skdev->proto_ver = FIT_PROTOCOL_MAJOR_VER(mfd);

		if (skdev->proto_ver != FIT_PROTOCOL_VERSION_1) {
			dev_err(&skdev->pdev->dev, "protocol mismatch\n");
			dev_err(&skdev->pdev->dev, "  got=%d support=%d\n",
				skdev->proto_ver, FIT_PROTOCOL_VERSION_1);
			dev_err(&skdev->pdev->dev, "  please upgrade driver\n");
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
		/* hardware interface overflows in y2106 */
		skdev->connect_time_stamp = (u32)ktime_get_real_seconds();
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

		dev_err(&skdev->pdev->dev, "Time sync driver=0x%x device=0x%x\n",
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
	dev_dbg(&skdev->pdev->dev, "sense 0x%x\n", sense);

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
	dev_dbg(&skdev->pdev->dev, "interrupt mask=0x%x\n", ~val);

	val = SKD_READL(skdev, FIT_CONTROL);
	val |= FIT_CR_ENABLE_INTERRUPTS;
	dev_dbg(&skdev->pdev->dev, "control=0x%x\n", val);
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
	dev_dbg(&skdev->pdev->dev, "control=0x%x\n", val);
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

	dev_dbg(&skdev->pdev->dev, "initial status=0x%x\n", sense);

	state = sense & FIT_SR_DRIVE_STATE_MASK;
	skdev->drive_state = state;
	skdev->last_mtd = 0;

	skdev->state = SKD_DRVR_STATE_STARTING;
	skdev->timer_countdown = SKD_STARTING_TIMO;

	skd_enable_interrupts(skdev);

	switch (skdev->drive_state) {
	case FIT_SR_DRIVE_OFFLINE:
		dev_err(&skdev->pdev->dev, "Drive offline...\n");
		break;

	case FIT_SR_DRIVE_FW_BOOTING:
		dev_dbg(&skdev->pdev->dev, "FIT_SR_DRIVE_FW_BOOTING\n");
		skdev->state = SKD_DRVR_STATE_WAIT_BOOT;
		skdev->timer_countdown = SKD_WAIT_BOOT_TIMO;
		break;

	case FIT_SR_DRIVE_BUSY_SANITIZE:
		dev_info(&skdev->pdev->dev, "Start: BUSY_SANITIZE\n");
		skdev->state = SKD_DRVR_STATE_BUSY_SANITIZE;
		skdev->timer_countdown = SKD_STARTED_BUSY_TIMO;
		break;

	case FIT_SR_DRIVE_BUSY_ERASE:
		dev_info(&skdev->pdev->dev, "Start: BUSY_ERASE\n");
		skdev->state = SKD_DRVR_STATE_BUSY_ERASE;
		skdev->timer_countdown = SKD_STARTED_BUSY_TIMO;
		break;

	case FIT_SR_DRIVE_INIT:
	case FIT_SR_DRIVE_ONLINE:
		skd_soft_reset(skdev);
		break;

	case FIT_SR_DRIVE_BUSY:
		dev_err(&skdev->pdev->dev, "Drive Busy...\n");
		skdev->state = SKD_DRVR_STATE_BUSY;
		skdev->timer_countdown = SKD_STARTED_BUSY_TIMO;
		break;

	case FIT_SR_DRIVE_SOFT_RESET:
		dev_err(&skdev->pdev->dev, "drive soft reset in prog\n");
		break;

	case FIT_SR_DRIVE_FAULT:
		/* Fault state is bad...soft reset won't do it...
		 * Hard reset, maybe, but does it work on device?
		 * For now, just fault so the system doesn't hang.
		 */
		skd_drive_fault(skdev);
		/*start the queue so we can respond with error to requests */
		dev_dbg(&skdev->pdev->dev, "starting queue\n");
		schedule_work(&skdev->start_queue);
		skdev->gendisk_on = -1;
		wake_up_interruptible(&skdev->waitq);
		break;

	case 0xFF:
		/* Most likely the device isn't there or isn't responding
		 * to the BAR1 addresses. */
		skd_drive_disappeared(skdev);
		/*start the queue so we can respond with error to requests */
		dev_dbg(&skdev->pdev->dev,
			"starting queue to error-out reqs\n");
		schedule_work(&skdev->start_queue);
		skdev->gendisk_on = -1;
		wake_up_interruptible(&skdev->waitq);
		break;

	default:
		dev_err(&skdev->pdev->dev, "Start: unknown state %x\n",
			skdev->drive_state);
		break;
	}

	state = SKD_READL(skdev, FIT_CONTROL);
	dev_dbg(&skdev->pdev->dev, "FIT Control Status=0x%x\n", state);

	state = SKD_READL(skdev, FIT_INT_STATUS_HOST);
	dev_dbg(&skdev->pdev->dev, "Intr Status=0x%x\n", state);

	state = SKD_READL(skdev, FIT_INT_MASK_HOST);
	dev_dbg(&skdev->pdev->dev, "Intr Mask=0x%x\n", state);

	state = SKD_READL(skdev, FIT_MSG_FROM_DEVICE);
	dev_dbg(&skdev->pdev->dev, "Msg from Dev=0x%x\n", state);

	state = SKD_READL(skdev, FIT_HW_VERSION);
	dev_dbg(&skdev->pdev->dev, "HW version=0x%x\n", state);

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
		dev_err(&skdev->pdev->dev, "%s not online no sync\n", __func__);
		goto stop_out;
	}

	if (skspcl->req.state != SKD_REQ_STATE_IDLE) {
		dev_err(&skdev->pdev->dev, "%s no special\n", __func__);
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
		dev_err(&skdev->pdev->dev, "%s no sync\n", __func__);
		break;
	case 1:
		dev_err(&skdev->pdev->dev, "%s sync done\n", __func__);
		break;
	default:
		dev_err(&skdev->pdev->dev, "%s sync error\n", __func__);
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
		dev_err(&skdev->pdev->dev, "%s state error 0x%02x\n", __func__,
			dev_state);
}

/* assume spinlock is held */
static void skd_restart_device(struct skd_device *skdev)
{
	u32 state;

	/* ack all ghost interrupts */
	SKD_WRITEL(skdev, FIT_INT_DEF_MASK, FIT_INT_STATUS_HOST);

	state = SKD_READL(skdev, FIT_STATUS);

	dev_dbg(&skdev->pdev->dev, "drive status=0x%x\n", state);

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
		dev_dbg(&skdev->pdev->dev, "stopping queue\n");
		blk_mq_stop_hw_queues(skdev->queue);
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
		dev_dbg(&skdev->pdev->dev, "state [%d] not implemented\n",
			skdev->state);
	}
	return rc;
}

/* assume spinlock is held */
static int skd_unquiesce_dev(struct skd_device *skdev)
{
	int prev_driver_state = skdev->state;

	skd_log_skdev(skdev, "unquiesce");
	if (skdev->state == SKD_DRVR_STATE_ONLINE) {
		dev_dbg(&skdev->pdev->dev, "**** device already ONLINE\n");
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
		dev_dbg(&skdev->pdev->dev, "drive BUSY state\n");
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
		dev_err(&skdev->pdev->dev, "Driver state %s(%d)=>%s(%d)\n",
			skd_skdev_state_to_str(prev_driver_state),
			prev_driver_state, skd_skdev_state_to_str(skdev->state),
			skdev->state);
		dev_dbg(&skdev->pdev->dev,
			"**** device ONLINE...starting block queue\n");
		dev_dbg(&skdev->pdev->dev, "starting queue\n");
		dev_info(&skdev->pdev->dev, "STEC s1120 ONLINE\n");
		schedule_work(&skdev->start_queue);
		skdev->gendisk_on = 1;
		wake_up_interruptible(&skdev->waitq);
		break;

	case SKD_DRVR_STATE_DISAPPEARED:
	default:
		dev_dbg(&skdev->pdev->dev,
			"**** driver state %d, not implemented\n",
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
	dev_dbg(&skdev->pdev->dev, "MSIX = 0x%x\n",
		SKD_READL(skdev, FIT_INT_STATUS_HOST));
	dev_err(&skdev->pdev->dev, "MSIX reserved irq %d = 0x%x\n", irq,
		SKD_READL(skdev, FIT_INT_STATUS_HOST));
	SKD_WRITEL(skdev, FIT_INT_RESERVED_MASK, FIT_INT_STATUS_HOST);
	spin_unlock_irqrestore(&skdev->lock, flags);
	return IRQ_HANDLED;
}

static irqreturn_t skd_statec_isr(int irq, void *skd_host_data)
{
	struct skd_device *skdev = skd_host_data;
	unsigned long flags;

	spin_lock_irqsave(&skdev->lock, flags);
	dev_dbg(&skdev->pdev->dev, "MSIX = 0x%x\n",
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
	dev_dbg(&skdev->pdev->dev, "MSIX = 0x%x\n",
		SKD_READL(skdev, FIT_INT_STATUS_HOST));
	SKD_WRITEL(skdev, FIT_ISH_COMPLETION_POSTED, FIT_INT_STATUS_HOST);
	deferred = skd_isr_completion_posted(skdev, skd_isr_comp_limit,
						&flush_enqueued);
	if (flush_enqueued)
		schedule_work(&skdev->start_queue);

	if (deferred)
		schedule_work(&skdev->completion_worker);
	else if (!flush_enqueued)
		schedule_work(&skdev->start_queue);

	spin_unlock_irqrestore(&skdev->lock, flags);

	return IRQ_HANDLED;
}

static irqreturn_t skd_msg_isr(int irq, void *skd_host_data)
{
	struct skd_device *skdev = skd_host_data;
	unsigned long flags;

	spin_lock_irqsave(&skdev->lock, flags);
	dev_dbg(&skdev->pdev->dev, "MSIX = 0x%x\n",
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
	dev_dbg(&skdev->pdev->dev, "MSIX = 0x%x\n",
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

static int skd_acquire_msix(struct skd_device *skdev)
{
	int i, rc;
	struct pci_dev *pdev = skdev->pdev;

	rc = pci_alloc_irq_vectors(pdev, SKD_MAX_MSIX_COUNT, SKD_MAX_MSIX_COUNT,
			PCI_IRQ_MSIX);
	if (rc < 0) {
		dev_err(&skdev->pdev->dev, "failed to enable MSI-X %d\n", rc);
		goto out;
	}

	skdev->msix_entries = kcalloc(SKD_MAX_MSIX_COUNT,
			sizeof(struct skd_msix_entry), GFP_KERNEL);
	if (!skdev->msix_entries) {
		rc = -ENOMEM;
		dev_err(&skdev->pdev->dev, "msix table allocation error\n");
		goto out;
	}

	/* Enable MSI-X vectors for the base queue */
	for (i = 0; i < SKD_MAX_MSIX_COUNT; i++) {
		struct skd_msix_entry *qentry = &skdev->msix_entries[i];

		snprintf(qentry->isr_name, sizeof(qentry->isr_name),
			 "%s%d-msix %s", DRV_NAME, skdev->devno,
			 msix_entries[i].name);

		rc = devm_request_irq(&skdev->pdev->dev,
				pci_irq_vector(skdev->pdev, i),
				msix_entries[i].handler, 0,
				qentry->isr_name, skdev);
		if (rc) {
			dev_err(&skdev->pdev->dev,
				"Unable to register(%d) MSI-X handler %d: %s\n",
				rc, i, qentry->isr_name);
			goto msix_out;
		}
	}

	dev_dbg(&skdev->pdev->dev, "%d msix irq(s) enabled\n",
		SKD_MAX_MSIX_COUNT);
	return 0;

msix_out:
	while (--i >= 0)
		devm_free_irq(&pdev->dev, pci_irq_vector(pdev, i), skdev);
out:
	kfree(skdev->msix_entries);
	skdev->msix_entries = NULL;
	return rc;
}

static int skd_acquire_irq(struct skd_device *skdev)
{
	struct pci_dev *pdev = skdev->pdev;
	unsigned int irq_flag = PCI_IRQ_LEGACY;
	int rc;

	if (skd_isr_type == SKD_IRQ_MSIX) {
		rc = skd_acquire_msix(skdev);
		if (!rc)
			return 0;

		dev_err(&skdev->pdev->dev,
			"failed to enable MSI-X, re-trying with MSI %d\n", rc);
	}

	snprintf(skdev->isr_name, sizeof(skdev->isr_name), "%s%d", DRV_NAME,
			skdev->devno);

	if (skd_isr_type != SKD_IRQ_LEGACY)
		irq_flag |= PCI_IRQ_MSI;
	rc = pci_alloc_irq_vectors(pdev, 1, 1, irq_flag);
	if (rc < 0) {
		dev_err(&skdev->pdev->dev,
			"failed to allocate the MSI interrupt %d\n", rc);
		return rc;
	}

	rc = devm_request_irq(&pdev->dev, pdev->irq, skd_isr,
			pdev->msi_enabled ? 0 : IRQF_SHARED,
			skdev->isr_name, skdev);
	if (rc) {
		pci_free_irq_vectors(pdev);
		dev_err(&skdev->pdev->dev, "failed to allocate interrupt %d\n",
			rc);
		return rc;
	}

	return 0;
}

static void skd_release_irq(struct skd_device *skdev)
{
	struct pci_dev *pdev = skdev->pdev;

	if (skdev->msix_entries) {
		int i;

		for (i = 0; i < SKD_MAX_MSIX_COUNT; i++) {
			devm_free_irq(&pdev->dev, pci_irq_vector(pdev, i),
					skdev);
		}

		kfree(skdev->msix_entries);
		skdev->msix_entries = NULL;
	} else {
		devm_free_irq(&pdev->dev, pdev->irq, skdev);
	}

	pci_free_irq_vectors(pdev);
}

/*
 *****************************************************************************
 * CONSTRUCT
 *****************************************************************************
 */

static void *skd_alloc_dma(struct skd_device *skdev, struct kmem_cache *s,
			   dma_addr_t *dma_handle, gfp_t gfp,
			   enum dma_data_direction dir)
{
	struct device *dev = &skdev->pdev->dev;
	void *buf;

	buf = kmem_cache_alloc(s, gfp);
	if (!buf)
		return NULL;
	*dma_handle = dma_map_single(dev, buf,
				     kmem_cache_size(s), dir);
	if (dma_mapping_error(dev, *dma_handle)) {
		kmem_cache_free(s, buf);
		buf = NULL;
	}
	return buf;
}

static void skd_free_dma(struct skd_device *skdev, struct kmem_cache *s,
			 void *vaddr, dma_addr_t dma_handle,
			 enum dma_data_direction dir)
{
	if (!vaddr)
		return;

	dma_unmap_single(&skdev->pdev->dev, dma_handle,
			 kmem_cache_size(s), dir);
	kmem_cache_free(s, vaddr);
}

static int skd_cons_skcomp(struct skd_device *skdev)
{
	int rc = 0;
	struct fit_completion_entry_v1 *skcomp;

	dev_dbg(&skdev->pdev->dev,
		"comp pci_alloc, total bytes %zd entries %d\n",
		SKD_SKCOMP_SIZE, SKD_N_COMPLETION_ENTRY);

	skcomp = dma_alloc_coherent(&skdev->pdev->dev, SKD_SKCOMP_SIZE,
				    &skdev->cq_dma_address, GFP_KERNEL);

	if (skcomp == NULL) {
		rc = -ENOMEM;
		goto err_out;
	}

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

	dev_dbg(&skdev->pdev->dev,
		"skmsg_table kcalloc, struct %lu, count %u total %lu\n",
		sizeof(struct skd_fitmsg_context), skdev->num_fitmsg_context,
		sizeof(struct skd_fitmsg_context) * skdev->num_fitmsg_context);

	skdev->skmsg_table = kcalloc(skdev->num_fitmsg_context,
				     sizeof(struct skd_fitmsg_context),
				     GFP_KERNEL);
	if (skdev->skmsg_table == NULL) {
		rc = -ENOMEM;
		goto err_out;
	}

	for (i = 0; i < skdev->num_fitmsg_context; i++) {
		struct skd_fitmsg_context *skmsg;

		skmsg = &skdev->skmsg_table[i];

		skmsg->id = i + SKD_ID_FIT_MSG;

		skmsg->msg_buf = dma_alloc_coherent(&skdev->pdev->dev,
						    SKD_N_FITMSG_BYTES,
						    &skmsg->mb_dma_address,
						    GFP_KERNEL);
		if (skmsg->msg_buf == NULL) {
			rc = -ENOMEM;
			goto err_out;
		}

		WARN(((uintptr_t)skmsg->msg_buf | skmsg->mb_dma_address) &
		     (FIT_QCMD_ALIGN - 1),
		     "not aligned: msg_buf %p mb_dma_address %pad\n",
		     skmsg->msg_buf, &skmsg->mb_dma_address);
		memset(skmsg->msg_buf, 0, SKD_N_FITMSG_BYTES);
	}

err_out:
	return rc;
}

static struct fit_sg_descriptor *skd_cons_sg_list(struct skd_device *skdev,
						  u32 n_sg,
						  dma_addr_t *ret_dma_addr)
{
	struct fit_sg_descriptor *sg_list;

	sg_list = skd_alloc_dma(skdev, skdev->sglist_cache, ret_dma_addr,
				GFP_DMA | __GFP_ZERO, DMA_TO_DEVICE);

	if (sg_list != NULL) {
		uint64_t dma_address = *ret_dma_addr;
		u32 i;

		for (i = 0; i < n_sg - 1; i++) {
			uint64_t ndp_off;
			ndp_off = (i + 1) * sizeof(struct fit_sg_descriptor);

			sg_list[i].next_desc_ptr = dma_address + ndp_off;
		}
		sg_list[i].next_desc_ptr = 0LL;
	}

	return sg_list;
}

static void skd_free_sg_list(struct skd_device *skdev,
			     struct fit_sg_descriptor *sg_list,
			     dma_addr_t dma_addr)
{
	if (WARN_ON_ONCE(!sg_list))
		return;

	skd_free_dma(skdev, skdev->sglist_cache, sg_list, dma_addr,
		     DMA_TO_DEVICE);
}

static int skd_init_request(struct blk_mq_tag_set *set, struct request *rq,
			    unsigned int hctx_idx, unsigned int numa_node)
{
	struct skd_device *skdev = set->driver_data;
	struct skd_request_context *skreq = blk_mq_rq_to_pdu(rq);

	skreq->state = SKD_REQ_STATE_IDLE;
	skreq->sg = (void *)(skreq + 1);
	sg_init_table(skreq->sg, skd_sgs_per_request);
	skreq->sksg_list = skd_cons_sg_list(skdev, skd_sgs_per_request,
					    &skreq->sksg_dma_address);

	return skreq->sksg_list ? 0 : -ENOMEM;
}

static void skd_exit_request(struct blk_mq_tag_set *set, struct request *rq,
			     unsigned int hctx_idx)
{
	struct skd_device *skdev = set->driver_data;
	struct skd_request_context *skreq = blk_mq_rq_to_pdu(rq);

	skd_free_sg_list(skdev, skreq->sksg_list, skreq->sksg_dma_address);
}

static int skd_cons_sksb(struct skd_device *skdev)
{
	int rc = 0;
	struct skd_special_context *skspcl;

	skspcl = &skdev->internal_skspcl;

	skspcl->req.id = 0 + SKD_ID_INTERNAL;
	skspcl->req.state = SKD_REQ_STATE_IDLE;

	skspcl->data_buf = skd_alloc_dma(skdev, skdev->databuf_cache,
					 &skspcl->db_dma_address,
					 GFP_DMA | __GFP_ZERO,
					 DMA_BIDIRECTIONAL);
	if (skspcl->data_buf == NULL) {
		rc = -ENOMEM;
		goto err_out;
	}

	skspcl->msg_buf = skd_alloc_dma(skdev, skdev->msgbuf_cache,
					&skspcl->mb_dma_address,
					GFP_DMA | __GFP_ZERO, DMA_TO_DEVICE);
	if (skspcl->msg_buf == NULL) {
		rc = -ENOMEM;
		goto err_out;
	}

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

static const struct blk_mq_ops skd_mq_ops = {
	.queue_rq	= skd_mq_queue_rq,
	.complete	= skd_complete_rq,
	.timeout	= skd_timed_out,
	.init_request	= skd_init_request,
	.exit_request	= skd_exit_request,
};

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

	memset(&skdev->tag_set, 0, sizeof(skdev->tag_set));
	skdev->tag_set.ops = &skd_mq_ops;
	skdev->tag_set.nr_hw_queues = 1;
	skdev->tag_set.queue_depth = skd_max_queue_depth;
	skdev->tag_set.cmd_size = sizeof(struct skd_request_context) +
		skdev->sgs_per_request * sizeof(struct scatterlist);
	skdev->tag_set.numa_node = NUMA_NO_NODE;
	skdev->tag_set.flags = BLK_MQ_F_SHOULD_MERGE |
		BLK_ALLOC_POLICY_TO_MQ_FLAG(BLK_TAG_ALLOC_FIFO);
	skdev->tag_set.driver_data = skdev;
	rc = blk_mq_alloc_tag_set(&skdev->tag_set);
	if (rc)
		goto err_out;
	q = blk_mq_init_queue(&skdev->tag_set);
	if (IS_ERR(q)) {
		blk_mq_free_tag_set(&skdev->tag_set);
		rc = PTR_ERR(q);
		goto err_out;
	}
	q->queuedata = skdev;

	skdev->queue = q;
	disk->queue = q;

	blk_queue_write_cache(q, true, true);
	blk_queue_max_segments(q, skdev->sgs_per_request);
	blk_queue_max_hw_sectors(q, SKD_N_MAX_SECTORS);

	/* set optimal I/O size to 8KB */
	blk_queue_io_opt(q, 8192);

	blk_queue_flag_set(QUEUE_FLAG_NONROT, q);
	blk_queue_flag_clear(QUEUE_FLAG_ADD_RANDOM, q);

	blk_queue_rq_timeout(q, 8 * HZ);

	spin_lock_irqsave(&skdev->lock, flags);
	dev_dbg(&skdev->pdev->dev, "stopping queue\n");
	blk_mq_stop_hw_queues(skdev->queue);
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
	size_t size;
	int rc;

	skdev = kzalloc(sizeof(*skdev), GFP_KERNEL);

	if (!skdev) {
		dev_err(&pdev->dev, "memory alloc failure\n");
		return NULL;
	}

	skdev->state = SKD_DRVR_STATE_LOAD;
	skdev->pdev = pdev;
	skdev->devno = skd_next_devno++;
	skdev->major = blk_major;
	skdev->dev_max_queue_depth = 0;

	skdev->num_req_context = skd_max_queue_depth;
	skdev->num_fitmsg_context = skd_max_queue_depth;
	skdev->cur_max_queue_depth = 1;
	skdev->queue_low_water_mark = 1;
	skdev->proto_ver = 99;
	skdev->sgs_per_request = skd_sgs_per_request;
	skdev->dbg_level = skd_dbg_level;

	spin_lock_init(&skdev->lock);

	INIT_WORK(&skdev->start_queue, skd_start_queue);
	INIT_WORK(&skdev->completion_worker, skd_completion_worker);

	size = max(SKD_N_FITMSG_BYTES, SKD_N_SPECIAL_FITMSG_BYTES);
	skdev->msgbuf_cache = kmem_cache_create("skd-msgbuf", size, 0,
						SLAB_HWCACHE_ALIGN, NULL);
	if (!skdev->msgbuf_cache)
		goto err_out;
	WARN_ONCE(kmem_cache_size(skdev->msgbuf_cache) < size,
		  "skd-msgbuf: %d < %zd\n",
		  kmem_cache_size(skdev->msgbuf_cache), size);
	size = skd_sgs_per_request * sizeof(struct fit_sg_descriptor);
	skdev->sglist_cache = kmem_cache_create("skd-sglist", size, 0,
						SLAB_HWCACHE_ALIGN, NULL);
	if (!skdev->sglist_cache)
		goto err_out;
	WARN_ONCE(kmem_cache_size(skdev->sglist_cache) < size,
		  "skd-sglist: %d < %zd\n",
		  kmem_cache_size(skdev->sglist_cache), size);
	size = SKD_N_INTERNAL_BYTES;
	skdev->databuf_cache = kmem_cache_create("skd-databuf", size, 0,
						 SLAB_HWCACHE_ALIGN, NULL);
	if (!skdev->databuf_cache)
		goto err_out;
	WARN_ONCE(kmem_cache_size(skdev->databuf_cache) < size,
		  "skd-databuf: %d < %zd\n",
		  kmem_cache_size(skdev->databuf_cache), size);

	dev_dbg(&skdev->pdev->dev, "skcomp\n");
	rc = skd_cons_skcomp(skdev);
	if (rc < 0)
		goto err_out;

	dev_dbg(&skdev->pdev->dev, "skmsg\n");
	rc = skd_cons_skmsg(skdev);
	if (rc < 0)
		goto err_out;

	dev_dbg(&skdev->pdev->dev, "sksb\n");
	rc = skd_cons_sksb(skdev);
	if (rc < 0)
		goto err_out;

	dev_dbg(&skdev->pdev->dev, "disk\n");
	rc = skd_cons_disk(skdev);
	if (rc < 0)
		goto err_out;

	dev_dbg(&skdev->pdev->dev, "VICTORY\n");
	return skdev;

err_out:
	dev_dbg(&skdev->pdev->dev, "construct failed\n");
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
	if (skdev->skcomp_table)
		dma_free_coherent(&skdev->pdev->dev, SKD_SKCOMP_SIZE,
				  skdev->skcomp_table, skdev->cq_dma_address);

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
			dma_free_coherent(&skdev->pdev->dev, SKD_N_FITMSG_BYTES,
					  skmsg->msg_buf,
					    skmsg->mb_dma_address);
		}
		skmsg->msg_buf = NULL;
		skmsg->mb_dma_address = 0;
	}

	kfree(skdev->skmsg_table);
	skdev->skmsg_table = NULL;
}

static void skd_free_sksb(struct skd_device *skdev)
{
	struct skd_special_context *skspcl = &skdev->internal_skspcl;

	skd_free_dma(skdev, skdev->databuf_cache, skspcl->data_buf,
		     skspcl->db_dma_address, DMA_BIDIRECTIONAL);

	skspcl->data_buf = NULL;
	skspcl->db_dma_address = 0;

	skd_free_dma(skdev, skdev->msgbuf_cache, skspcl->msg_buf,
		     skspcl->mb_dma_address, DMA_TO_DEVICE);

	skspcl->msg_buf = NULL;
	skspcl->mb_dma_address = 0;

	skd_free_sg_list(skdev, skspcl->req.sksg_list,
			 skspcl->req.sksg_dma_address);

	skspcl->req.sksg_list = NULL;
	skspcl->req.sksg_dma_address = 0;
}

static void skd_free_disk(struct skd_device *skdev)
{
	struct gendisk *disk = skdev->disk;

	if (disk && (disk->flags & GENHD_FL_UP))
		del_gendisk(disk);

	if (skdev->queue) {
		blk_cleanup_queue(skdev->queue);
		skdev->queue = NULL;
		if (disk)
			disk->queue = NULL;
	}

	if (skdev->tag_set.tags)
		blk_mq_free_tag_set(&skdev->tag_set);

	put_disk(disk);
	skdev->disk = NULL;
}

static void skd_destruct(struct skd_device *skdev)
{
	if (skdev == NULL)
		return;

	cancel_work_sync(&skdev->start_queue);

	dev_dbg(&skdev->pdev->dev, "disk\n");
	skd_free_disk(skdev);

	dev_dbg(&skdev->pdev->dev, "sksb\n");
	skd_free_sksb(skdev);

	dev_dbg(&skdev->pdev->dev, "skmsg\n");
	skd_free_skmsg(skdev);

	dev_dbg(&skdev->pdev->dev, "skcomp\n");
	skd_free_skcomp(skdev);

	kmem_cache_destroy(skdev->databuf_cache);
	kmem_cache_destroy(skdev->sglist_cache);
	kmem_cache_destroy(skdev->msgbuf_cache);

	dev_dbg(&skdev->pdev->dev, "skdev\n");
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

	dev_dbg(&skdev->pdev->dev, "%s: CMD[%s] getgeo device\n",
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

static int skd_bdev_attach(struct device *parent, struct skd_device *skdev)
{
	dev_dbg(&skdev->pdev->dev, "add_disk\n");
	device_add_disk(parent, skdev->disk, NULL);
	return 0;
}

static const struct block_device_operations skd_blockdev_ops = {
	.owner		= THIS_MODULE,
	.getgeo		= skd_bdev_getgeo,
};

/*
 *****************************************************************************
 * PCIe DRIVER GLUE
 *****************************************************************************
 */

static const struct pci_device_id skd_pci_tbl[] = {
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

	dev_dbg(&pdev->dev, "vendor=%04X device=%04x\n", pdev->vendor,
		pdev->device);

	rc = pci_enable_device(pdev);
	if (rc)
		return rc;
	rc = pci_request_regions(pdev, DRV_NAME);
	if (rc)
		goto err_out;
	rc = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (rc)
		rc = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (rc) {
		dev_err(&pdev->dev, "DMA mask error %d\n", rc);
		goto err_out_regions;
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
	dev_info(&pdev->dev, "%s 64bit\n", pci_str);

	pci_set_master(pdev);
	rc = pci_enable_pcie_error_reporting(pdev);
	if (rc) {
		dev_err(&pdev->dev,
			"bad enable of PCIe error reporting rc=%d\n", rc);
		skdev->pcie_error_reporting_is_enabled = 0;
	} else
		skdev->pcie_error_reporting_is_enabled = 1;

	pci_set_drvdata(pdev, skdev);

	for (i = 0; i < SKD_MAX_BARS; i++) {
		skdev->mem_phys[i] = pci_resource_start(pdev, i);
		skdev->mem_size[i] = (u32)pci_resource_len(pdev, i);
		skdev->mem_map[i] = ioremap(skdev->mem_phys[i],
					    skdev->mem_size[i]);
		if (!skdev->mem_map[i]) {
			dev_err(&pdev->dev,
				"Unable to map adapter memory!\n");
			rc = -ENODEV;
			goto err_out_iounmap;
		}
		dev_dbg(&pdev->dev, "mem_map=%p, phyd=%016llx, size=%d\n",
			skdev->mem_map[i], (uint64_t)skdev->mem_phys[i],
			skdev->mem_size[i]);
	}

	rc = skd_acquire_irq(skdev);
	if (rc) {
		dev_err(&pdev->dev, "interrupt resource error %d\n", rc);
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
		skd_bdev_attach(&pdev->dev, skdev);
		rc = 0;
	} else {
		/* we timed out, something is wrong with the device,
		   don't add the disk structure */
		dev_err(&pdev->dev, "error: waiting for s1120 timed out %d!\n",
			rc);
		/* in case of no error; we timeout with ENXIO */
		if (!rc)
			rc = -ENXIO;
		goto err_out_timer;
	}

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
		dev_err(&pdev->dev, "no device data for PCI\n");
		return;
	}
	skd_stop_device(skdev);
	skd_release_irq(skdev);

	for (i = 0; i < SKD_MAX_BARS; i++)
		if (skdev->mem_map[i])
			iounmap(skdev->mem_map[i]);

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
		dev_err(&pdev->dev, "no device data for PCI\n");
		return -EIO;
	}

	skd_stop_device(skdev);

	skd_release_irq(skdev);

	for (i = 0; i < SKD_MAX_BARS; i++)
		if (skdev->mem_map[i])
			iounmap(skdev->mem_map[i]);

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
		dev_err(&pdev->dev, "no device data for PCI\n");
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
	rc = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (rc)
		rc = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (rc) {
		dev_err(&pdev->dev, "DMA mask error %d\n", rc);
		goto err_out_regions;
	}

	pci_set_master(pdev);
	rc = pci_enable_pcie_error_reporting(pdev);
	if (rc) {
		dev_err(&pdev->dev,
			"bad enable of PCIe error reporting rc=%d\n", rc);
		skdev->pcie_error_reporting_is_enabled = 0;
	} else
		skdev->pcie_error_reporting_is_enabled = 1;

	for (i = 0; i < SKD_MAX_BARS; i++) {

		skdev->mem_phys[i] = pci_resource_start(pdev, i);
		skdev->mem_size[i] = (u32)pci_resource_len(pdev, i);
		skdev->mem_map[i] = ioremap(skdev->mem_phys[i],
					    skdev->mem_size[i]);
		if (!skdev->mem_map[i]) {
			dev_err(&pdev->dev, "Unable to map adapter memory!\n");
			rc = -ENODEV;
			goto err_out_iounmap;
		}
		dev_dbg(&pdev->dev, "mem_map=%p, phyd=%016llx, size=%d\n",
			skdev->mem_map[i], (uint64_t)skdev->mem_phys[i],
			skdev->mem_size[i]);
	}
	rc = skd_acquire_irq(skdev);
	if (rc) {
		dev_err(&pdev->dev, "interrupt resource error %d\n", rc);
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

	dev_err(&pdev->dev, "%s called\n", __func__);

	skdev = pci_get_drvdata(pdev);
	if (!skdev) {
		dev_err(&pdev->dev, "no device data for PCI\n");
		return;
	}

	dev_err(&pdev->dev, "calling stop\n");
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
	default:
		return "???";
	}
}

static void skd_log_skdev(struct skd_device *skdev, const char *event)
{
	dev_dbg(&skdev->pdev->dev, "skdev=%p event='%s'\n", skdev, event);
	dev_dbg(&skdev->pdev->dev, "  drive_state=%s(%d) driver_state=%s(%d)\n",
		skd_drive_state_to_str(skdev->drive_state), skdev->drive_state,
		skd_skdev_state_to_str(skdev->state), skdev->state);
	dev_dbg(&skdev->pdev->dev, "  busy=%d limit=%d dev=%d lowat=%d\n",
		skd_in_flight(skdev), skdev->cur_max_queue_depth,
		skdev->dev_max_queue_depth, skdev->queue_low_water_mark);
	dev_dbg(&skdev->pdev->dev, "  cycle=%d cycle_ix=%d\n",
		skdev->skcomp_cycle, skdev->skcomp_ix);
}

static void skd_log_skreq(struct skd_device *skdev,
			  struct skd_request_context *skreq, const char *event)
{
	struct request *req = blk_mq_rq_from_pdu(skreq);
	u32 lba = blk_rq_pos(req);
	u32 count = blk_rq_sectors(req);

	dev_dbg(&skdev->pdev->dev, "skreq=%p event='%s'\n", skreq, event);
	dev_dbg(&skdev->pdev->dev, "  state=%s(%d) id=0x%04x fitmsg=0x%04x\n",
		skd_skreq_state_to_str(skreq->state), skreq->state, skreq->id,
		skreq->fitmsg_id);
	dev_dbg(&skdev->pdev->dev, "  sg_dir=%d n_sg=%d\n",
		skreq->data_dir, skreq->n_sg);

	dev_dbg(&skdev->pdev->dev,
		"req=%p lba=%u(0x%x) count=%u(0x%x) dir=%d\n", req, lba, lba,
		count, count, (int)rq_data_dir(req));
}

/*
 *****************************************************************************
 * MODULE GLUE
 *****************************************************************************
 */

static int __init skd_init(void)
{
	BUILD_BUG_ON(sizeof(struct fit_completion_entry_v1) != 8);
	BUILD_BUG_ON(sizeof(struct fit_comp_error_info) != 32);
	BUILD_BUG_ON(sizeof(struct skd_command_header) != 16);
	BUILD_BUG_ON(sizeof(struct skd_scsi_request) != 32);
	BUILD_BUG_ON(sizeof(struct driver_inquiry_data) != 44);
	BUILD_BUG_ON(offsetof(struct skd_msg_buf, fmh) != 0);
	BUILD_BUG_ON(offsetof(struct skd_msg_buf, scsi) != 64);
	BUILD_BUG_ON(sizeof(struct skd_msg_buf) != SKD_N_FITMSG_BYTES);

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

	if (skd_max_req_per_msg < 1 ||
	    skd_max_req_per_msg > SKD_MAX_REQ_PER_MSG) {
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

	return pci_register_driver(&skd_driver);
}

static void __exit skd_exit(void)
{
	pci_unregister_driver(&skd_driver);

	if (skd_major)
		unregister_blkdev(skd_major, DRV_NAME);
}

module_init(skd_init);
module_exit(skd_exit);
