/*
 * NVM Express device driver
 * Copyright (c) 2011-2014, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/nvme.h>
#include <linux/bitops.h>
#include <linux/blkdev.h>
#include <linux/blk-mq.h>
#include <linux/cpu.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/genhd.h>
#include <linux/hdreg.h>
#include <linux/idr.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kdev_t.h>
#include <linux/kthread.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/pci.h>
#include <linux/poison.h>
#include <linux/ptrace.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <scsi/sg.h>
#include <asm-generic/io-64-nonatomic-lo-hi.h>

#define NVME_Q_DEPTH		1024
#define NVME_AQ_DEPTH		64
#define SQ_SIZE(depth)		(depth * sizeof(struct nvme_command))
#define CQ_SIZE(depth)		(depth * sizeof(struct nvme_completion))
#define ADMIN_TIMEOUT		(admin_timeout * HZ)
#define SHUTDOWN_TIMEOUT	(shutdown_timeout * HZ)
#define IOD_TIMEOUT		(retry_time * HZ)

static unsigned char admin_timeout = 60;
module_param(admin_timeout, byte, 0644);
MODULE_PARM_DESC(admin_timeout, "timeout in seconds for admin commands");

unsigned char nvme_io_timeout = 30;
module_param_named(io_timeout, nvme_io_timeout, byte, 0644);
MODULE_PARM_DESC(io_timeout, "timeout in seconds for I/O");

static unsigned char retry_time = 30;
module_param(retry_time, byte, 0644);
MODULE_PARM_DESC(retry_time, "time in seconds to retry failed I/O");

static unsigned char shutdown_timeout = 5;
module_param(shutdown_timeout, byte, 0644);
MODULE_PARM_DESC(shutdown_timeout, "timeout in seconds for controller shutdown");

static int nvme_major;
module_param(nvme_major, int, 0);

static int use_threaded_interrupts;
module_param(use_threaded_interrupts, int, 0);

static DEFINE_SPINLOCK(dev_list_lock);
static LIST_HEAD(dev_list);
static struct task_struct *nvme_thread;
static struct workqueue_struct *nvme_workq;
static wait_queue_head_t nvme_kthread_wait;
static struct notifier_block nvme_nb;

static void nvme_reset_failed_dev(struct work_struct *ws);
static int nvme_process_cq(struct nvme_queue *nvmeq);

struct async_cmd_info {
	struct kthread_work work;
	struct kthread_worker *worker;
	struct request *req;
	u32 result;
	int status;
	void *ctx;
};

/*
 * An NVM Express queue.  Each device has at least two (one for admin
 * commands and one for I/O commands).
 */
struct nvme_queue {
	struct llist_node node;
	struct device *q_dmadev;
	struct nvme_dev *dev;
	char irqname[24];	/* nvme4294967295-65535\0 */
	spinlock_t q_lock;
	struct nvme_command *sq_cmds;
	volatile struct nvme_completion *cqes;
	dma_addr_t sq_dma_addr;
	dma_addr_t cq_dma_addr;
	u32 __iomem *q_db;
	u16 q_depth;
	s16 cq_vector;
	u16 sq_head;
	u16 sq_tail;
	u16 cq_head;
	u16 qid;
	u8 cq_phase;
	u8 cqe_seen;
	struct async_cmd_info cmdinfo;
	struct blk_mq_hw_ctx *hctx;
};

/*
 * Check we didin't inadvertently grow the command struct
 */
static inline void _nvme_check_size(void)
{
	BUILD_BUG_ON(sizeof(struct nvme_rw_command) != 64);
	BUILD_BUG_ON(sizeof(struct nvme_create_cq) != 64);
	BUILD_BUG_ON(sizeof(struct nvme_create_sq) != 64);
	BUILD_BUG_ON(sizeof(struct nvme_delete_queue) != 64);
	BUILD_BUG_ON(sizeof(struct nvme_features) != 64);
	BUILD_BUG_ON(sizeof(struct nvme_format_cmd) != 64);
	BUILD_BUG_ON(sizeof(struct nvme_abort_cmd) != 64);
	BUILD_BUG_ON(sizeof(struct nvme_command) != 64);
	BUILD_BUG_ON(sizeof(struct nvme_id_ctrl) != 4096);
	BUILD_BUG_ON(sizeof(struct nvme_id_ns) != 4096);
	BUILD_BUG_ON(sizeof(struct nvme_lba_range_type) != 64);
	BUILD_BUG_ON(sizeof(struct nvme_smart_log) != 512);
}

typedef void (*nvme_completion_fn)(struct nvme_queue *, void *,
						struct nvme_completion *);

struct nvme_cmd_info {
	nvme_completion_fn fn;
	void *ctx;
	int aborted;
	struct nvme_queue *nvmeq;
	struct nvme_iod iod[0];
};

/*
 * Max size of iod being embedded in the request payload
 */
#define NVME_INT_PAGES		2
#define NVME_INT_BYTES(dev)	(NVME_INT_PAGES * (dev)->page_size)

/*
 * Will slightly overestimate the number of pages needed.  This is OK
 * as it only leads to a small amount of wasted memory for the lifetime of
 * the I/O.
 */
static int nvme_npages(unsigned size, struct nvme_dev *dev)
{
	unsigned nprps = DIV_ROUND_UP(size + dev->page_size, dev->page_size);
	return DIV_ROUND_UP(8 * nprps, PAGE_SIZE - 8);
}

static unsigned int nvme_cmd_size(struct nvme_dev *dev)
{
	unsigned int ret = sizeof(struct nvme_cmd_info);

	ret += sizeof(struct nvme_iod);
	ret += sizeof(__le64 *) * nvme_npages(NVME_INT_BYTES(dev), dev);
	ret += sizeof(struct scatterlist) * NVME_INT_PAGES;

	return ret;
}

static int nvme_admin_init_hctx(struct blk_mq_hw_ctx *hctx, void *data,
				unsigned int hctx_idx)
{
	struct nvme_dev *dev = data;
	struct nvme_queue *nvmeq = dev->queues[0];

	WARN_ON(nvmeq->hctx);
	nvmeq->hctx = hctx;
	hctx->driver_data = nvmeq;
	return 0;
}

static int nvme_admin_init_request(void *data, struct request *req,
				unsigned int hctx_idx, unsigned int rq_idx,
				unsigned int numa_node)
{
	struct nvme_dev *dev = data;
	struct nvme_cmd_info *cmd = blk_mq_rq_to_pdu(req);
	struct nvme_queue *nvmeq = dev->queues[0];

	BUG_ON(!nvmeq);
	cmd->nvmeq = nvmeq;
	return 0;
}

static void nvme_exit_hctx(struct blk_mq_hw_ctx *hctx, unsigned int hctx_idx)
{
	struct nvme_queue *nvmeq = hctx->driver_data;

	nvmeq->hctx = NULL;
}

static int nvme_init_hctx(struct blk_mq_hw_ctx *hctx, void *data,
			  unsigned int hctx_idx)
{
	struct nvme_dev *dev = data;
	struct nvme_queue *nvmeq = dev->queues[
					(hctx_idx % dev->queue_count) + 1];

	if (!nvmeq->hctx)
		nvmeq->hctx = hctx;

	/* nvmeq queues are shared between namespaces. We assume here that
	 * blk-mq map the tags so they match up with the nvme queue tags. */
	WARN_ON(nvmeq->hctx->tags != hctx->tags);

	hctx->driver_data = nvmeq;
	return 0;
}

static int nvme_init_request(void *data, struct request *req,
				unsigned int hctx_idx, unsigned int rq_idx,
				unsigned int numa_node)
{
	struct nvme_dev *dev = data;
	struct nvme_cmd_info *cmd = blk_mq_rq_to_pdu(req);
	struct nvme_queue *nvmeq = dev->queues[hctx_idx + 1];

	BUG_ON(!nvmeq);
	cmd->nvmeq = nvmeq;
	return 0;
}

static void nvme_set_info(struct nvme_cmd_info *cmd, void *ctx,
				nvme_completion_fn handler)
{
	cmd->fn = handler;
	cmd->ctx = ctx;
	cmd->aborted = 0;
	blk_mq_start_request(blk_mq_rq_from_pdu(cmd));
}

static void *iod_get_private(struct nvme_iod *iod)
{
	return (void *) (iod->private & ~0x1UL);
}

/*
 * If bit 0 is set, the iod is embedded in the request payload.
 */
static bool iod_should_kfree(struct nvme_iod *iod)
{
	return (iod->private & 0x01) == 0;
}

/* Special values must be less than 0x1000 */
#define CMD_CTX_BASE		((void *)POISON_POINTER_DELTA)
#define CMD_CTX_CANCELLED	(0x30C + CMD_CTX_BASE)
#define CMD_CTX_COMPLETED	(0x310 + CMD_CTX_BASE)
#define CMD_CTX_INVALID		(0x314 + CMD_CTX_BASE)

static void special_completion(struct nvme_queue *nvmeq, void *ctx,
						struct nvme_completion *cqe)
{
	if (ctx == CMD_CTX_CANCELLED)
		return;
	if (ctx == CMD_CTX_COMPLETED) {
		dev_warn(nvmeq->q_dmadev,
				"completed id %d twice on queue %d\n",
				cqe->command_id, le16_to_cpup(&cqe->sq_id));
		return;
	}
	if (ctx == CMD_CTX_INVALID) {
		dev_warn(nvmeq->q_dmadev,
				"invalid id %d completed on queue %d\n",
				cqe->command_id, le16_to_cpup(&cqe->sq_id));
		return;
	}
	dev_warn(nvmeq->q_dmadev, "Unknown special completion %p\n", ctx);
}

static void *cancel_cmd_info(struct nvme_cmd_info *cmd, nvme_completion_fn *fn)
{
	void *ctx;

	if (fn)
		*fn = cmd->fn;
	ctx = cmd->ctx;
	cmd->fn = special_completion;
	cmd->ctx = CMD_CTX_CANCELLED;
	return ctx;
}

static void async_req_completion(struct nvme_queue *nvmeq, void *ctx,
						struct nvme_completion *cqe)
{
	struct request *req = ctx;

	u32 result = le32_to_cpup(&cqe->result);
	u16 status = le16_to_cpup(&cqe->status) >> 1;

	if (status == NVME_SC_SUCCESS || status == NVME_SC_ABORT_REQ)
		++nvmeq->dev->event_limit;
	if (status == NVME_SC_SUCCESS)
		dev_warn(nvmeq->q_dmadev,
			"async event result %08x\n", result);

	blk_mq_free_hctx_request(nvmeq->hctx, req);
}

static void abort_completion(struct nvme_queue *nvmeq, void *ctx,
						struct nvme_completion *cqe)
{
	struct request *req = ctx;

	u16 status = le16_to_cpup(&cqe->status) >> 1;
	u32 result = le32_to_cpup(&cqe->result);

	blk_mq_free_hctx_request(nvmeq->hctx, req);

	dev_warn(nvmeq->q_dmadev, "Abort status:%x result:%x", status, result);
	++nvmeq->dev->abort_limit;
}

static void async_completion(struct nvme_queue *nvmeq, void *ctx,
						struct nvme_completion *cqe)
{
	struct async_cmd_info *cmdinfo = ctx;
	cmdinfo->result = le32_to_cpup(&cqe->result);
	cmdinfo->status = le16_to_cpup(&cqe->status) >> 1;
	queue_kthread_work(cmdinfo->worker, &cmdinfo->work);
	blk_mq_free_hctx_request(nvmeq->hctx, cmdinfo->req);
}

static inline struct nvme_cmd_info *get_cmd_from_tag(struct nvme_queue *nvmeq,
				  unsigned int tag)
{
	struct blk_mq_hw_ctx *hctx = nvmeq->hctx;
	struct request *req = blk_mq_tag_to_rq(hctx->tags, tag);

	return blk_mq_rq_to_pdu(req);
}

/*
 * Called with local interrupts disabled and the q_lock held.  May not sleep.
 */
static void *nvme_finish_cmd(struct nvme_queue *nvmeq, int tag,
						nvme_completion_fn *fn)
{
	struct nvme_cmd_info *cmd = get_cmd_from_tag(nvmeq, tag);
	void *ctx;
	if (tag >= nvmeq->q_depth) {
		*fn = special_completion;
		return CMD_CTX_INVALID;
	}
	if (fn)
		*fn = cmd->fn;
	ctx = cmd->ctx;
	cmd->fn = special_completion;
	cmd->ctx = CMD_CTX_COMPLETED;
	return ctx;
}

/**
 * nvme_submit_cmd() - Copy a command into a queue and ring the doorbell
 * @nvmeq: The queue to use
 * @cmd: The command to send
 *
 * Safe to use from interrupt context
 */
static int __nvme_submit_cmd(struct nvme_queue *nvmeq, struct nvme_command *cmd)
{
	u16 tail = nvmeq->sq_tail;

	memcpy(&nvmeq->sq_cmds[tail], cmd, sizeof(*cmd));
	if (++tail == nvmeq->q_depth)
		tail = 0;
	writel(tail, nvmeq->q_db);
	nvmeq->sq_tail = tail;

	return 0;
}

static int nvme_submit_cmd(struct nvme_queue *nvmeq, struct nvme_command *cmd)
{
	unsigned long flags;
	int ret;
	spin_lock_irqsave(&nvmeq->q_lock, flags);
	ret = __nvme_submit_cmd(nvmeq, cmd);
	spin_unlock_irqrestore(&nvmeq->q_lock, flags);
	return ret;
}

static __le64 **iod_list(struct nvme_iod *iod)
{
	return ((void *)iod) + iod->offset;
}

static inline void iod_init(struct nvme_iod *iod, unsigned nbytes,
			    unsigned nseg, unsigned long private)
{
	iod->private = private;
	iod->offset = offsetof(struct nvme_iod, sg[nseg]);
	iod->npages = -1;
	iod->length = nbytes;
	iod->nents = 0;
}

static struct nvme_iod *
__nvme_alloc_iod(unsigned nseg, unsigned bytes, struct nvme_dev *dev,
		 unsigned long priv, gfp_t gfp)
{
	struct nvme_iod *iod = kmalloc(sizeof(struct nvme_iod) +
				sizeof(__le64 *) * nvme_npages(bytes, dev) +
				sizeof(struct scatterlist) * nseg, gfp);

	if (iod)
		iod_init(iod, bytes, nseg, priv);

	return iod;
}

static struct nvme_iod *nvme_alloc_iod(struct request *rq, struct nvme_dev *dev,
			               gfp_t gfp)
{
	unsigned size = !(rq->cmd_flags & REQ_DISCARD) ? blk_rq_bytes(rq) :
                                                sizeof(struct nvme_dsm_range);
	unsigned long mask = 0;
	struct nvme_iod *iod;

	if (rq->nr_phys_segments <= NVME_INT_PAGES &&
	    size <= NVME_INT_BYTES(dev)) {
		struct nvme_cmd_info *cmd = blk_mq_rq_to_pdu(rq);

		iod = cmd->iod;
		mask = 0x01;
		iod_init(iod, size, rq->nr_phys_segments,
				(unsigned long) rq | 0x01);
		return iod;
	}

	return __nvme_alloc_iod(rq->nr_phys_segments, size, dev,
				(unsigned long) rq, gfp);
}

void nvme_free_iod(struct nvme_dev *dev, struct nvme_iod *iod)
{
	const int last_prp = dev->page_size / 8 - 1;
	int i;
	__le64 **list = iod_list(iod);
	dma_addr_t prp_dma = iod->first_dma;

	if (iod->npages == 0)
		dma_pool_free(dev->prp_small_pool, list[0], prp_dma);
	for (i = 0; i < iod->npages; i++) {
		__le64 *prp_list = list[i];
		dma_addr_t next_prp_dma = le64_to_cpu(prp_list[last_prp]);
		dma_pool_free(dev->prp_page_pool, prp_list, prp_dma);
		prp_dma = next_prp_dma;
	}

	if (iod_should_kfree(iod))
		kfree(iod);
}

static int nvme_error_status(u16 status)
{
	switch (status & 0x7ff) {
	case NVME_SC_SUCCESS:
		return 0;
	case NVME_SC_CAP_EXCEEDED:
		return -ENOSPC;
	default:
		return -EIO;
	}
}

static void req_completion(struct nvme_queue *nvmeq, void *ctx,
						struct nvme_completion *cqe)
{
	struct nvme_iod *iod = ctx;
	struct request *req = iod_get_private(iod);
	struct nvme_cmd_info *cmd_rq = blk_mq_rq_to_pdu(req);

	u16 status = le16_to_cpup(&cqe->status) >> 1;

	if (unlikely(status)) {
		if (!(status & NVME_SC_DNR || blk_noretry_request(req))
		    && (jiffies - req->start_time) < req->timeout) {
			unsigned long flags;

			blk_mq_requeue_request(req);
			spin_lock_irqsave(req->q->queue_lock, flags);
			if (!blk_queue_stopped(req->q))
				blk_mq_kick_requeue_list(req->q);
			spin_unlock_irqrestore(req->q->queue_lock, flags);
			return;
		}
		req->errors = nvme_error_status(status);
	} else
		req->errors = 0;

	if (cmd_rq->aborted)
		dev_warn(&nvmeq->dev->pci_dev->dev,
			"completing aborted command with status:%04x\n",
			status);

	if (iod->nents)
		dma_unmap_sg(&nvmeq->dev->pci_dev->dev, iod->sg, iod->nents,
			rq_data_dir(req) ? DMA_TO_DEVICE : DMA_FROM_DEVICE);
	nvme_free_iod(nvmeq->dev, iod);

	blk_mq_complete_request(req);
}

/* length is in bytes.  gfp flags indicates whether we may sleep. */
int nvme_setup_prps(struct nvme_dev *dev, struct nvme_iod *iod, int total_len,
								gfp_t gfp)
{
	struct dma_pool *pool;
	int length = total_len;
	struct scatterlist *sg = iod->sg;
	int dma_len = sg_dma_len(sg);
	u64 dma_addr = sg_dma_address(sg);
	int offset = offset_in_page(dma_addr);
	__le64 *prp_list;
	__le64 **list = iod_list(iod);
	dma_addr_t prp_dma;
	int nprps, i;
	u32 page_size = dev->page_size;

	length -= (page_size - offset);
	if (length <= 0)
		return total_len;

	dma_len -= (page_size - offset);
	if (dma_len) {
		dma_addr += (page_size - offset);
	} else {
		sg = sg_next(sg);
		dma_addr = sg_dma_address(sg);
		dma_len = sg_dma_len(sg);
	}

	if (length <= page_size) {
		iod->first_dma = dma_addr;
		return total_len;
	}

	nprps = DIV_ROUND_UP(length, page_size);
	if (nprps <= (256 / 8)) {
		pool = dev->prp_small_pool;
		iod->npages = 0;
	} else {
		pool = dev->prp_page_pool;
		iod->npages = 1;
	}

	prp_list = dma_pool_alloc(pool, gfp, &prp_dma);
	if (!prp_list) {
		iod->first_dma = dma_addr;
		iod->npages = -1;
		return (total_len - length) + page_size;
	}
	list[0] = prp_list;
	iod->first_dma = prp_dma;
	i = 0;
	for (;;) {
		if (i == page_size >> 3) {
			__le64 *old_prp_list = prp_list;
			prp_list = dma_pool_alloc(pool, gfp, &prp_dma);
			if (!prp_list)
				return total_len - length;
			list[iod->npages++] = prp_list;
			prp_list[0] = old_prp_list[i - 1];
			old_prp_list[i - 1] = cpu_to_le64(prp_dma);
			i = 1;
		}
		prp_list[i++] = cpu_to_le64(dma_addr);
		dma_len -= page_size;
		dma_addr += page_size;
		length -= page_size;
		if (length <= 0)
			break;
		if (dma_len > 0)
			continue;
		BUG_ON(dma_len < 0);
		sg = sg_next(sg);
		dma_addr = sg_dma_address(sg);
		dma_len = sg_dma_len(sg);
	}

	return total_len;
}

/*
 * We reuse the small pool to allocate the 16-byte range here as it is not
 * worth having a special pool for these or additional cases to handle freeing
 * the iod.
 */
static void nvme_submit_discard(struct nvme_queue *nvmeq, struct nvme_ns *ns,
		struct request *req, struct nvme_iod *iod)
{
	struct nvme_dsm_range *range =
				(struct nvme_dsm_range *)iod_list(iod)[0];
	struct nvme_command *cmnd = &nvmeq->sq_cmds[nvmeq->sq_tail];

	range->cattr = cpu_to_le32(0);
	range->nlb = cpu_to_le32(blk_rq_bytes(req) >> ns->lba_shift);
	range->slba = cpu_to_le64(nvme_block_nr(ns, blk_rq_pos(req)));

	memset(cmnd, 0, sizeof(*cmnd));
	cmnd->dsm.opcode = nvme_cmd_dsm;
	cmnd->dsm.command_id = req->tag;
	cmnd->dsm.nsid = cpu_to_le32(ns->ns_id);
	cmnd->dsm.prp1 = cpu_to_le64(iod->first_dma);
	cmnd->dsm.nr = 0;
	cmnd->dsm.attributes = cpu_to_le32(NVME_DSMGMT_AD);

	if (++nvmeq->sq_tail == nvmeq->q_depth)
		nvmeq->sq_tail = 0;
	writel(nvmeq->sq_tail, nvmeq->q_db);
}

static void nvme_submit_flush(struct nvme_queue *nvmeq, struct nvme_ns *ns,
								int cmdid)
{
	struct nvme_command *cmnd = &nvmeq->sq_cmds[nvmeq->sq_tail];

	memset(cmnd, 0, sizeof(*cmnd));
	cmnd->common.opcode = nvme_cmd_flush;
	cmnd->common.command_id = cmdid;
	cmnd->common.nsid = cpu_to_le32(ns->ns_id);

	if (++nvmeq->sq_tail == nvmeq->q_depth)
		nvmeq->sq_tail = 0;
	writel(nvmeq->sq_tail, nvmeq->q_db);
}

static int nvme_submit_iod(struct nvme_queue *nvmeq, struct nvme_iod *iod,
							struct nvme_ns *ns)
{
	struct request *req = iod_get_private(iod);
	struct nvme_command *cmnd;
	u16 control = 0;
	u32 dsmgmt = 0;

	if (req->cmd_flags & REQ_FUA)
		control |= NVME_RW_FUA;
	if (req->cmd_flags & (REQ_FAILFAST_DEV | REQ_RAHEAD))
		control |= NVME_RW_LR;

	if (req->cmd_flags & REQ_RAHEAD)
		dsmgmt |= NVME_RW_DSM_FREQ_PREFETCH;

	cmnd = &nvmeq->sq_cmds[nvmeq->sq_tail];
	memset(cmnd, 0, sizeof(*cmnd));

	cmnd->rw.opcode = (rq_data_dir(req) ? nvme_cmd_write : nvme_cmd_read);
	cmnd->rw.command_id = req->tag;
	cmnd->rw.nsid = cpu_to_le32(ns->ns_id);
	cmnd->rw.prp1 = cpu_to_le64(sg_dma_address(iod->sg));
	cmnd->rw.prp2 = cpu_to_le64(iod->first_dma);
	cmnd->rw.slba = cpu_to_le64(nvme_block_nr(ns, blk_rq_pos(req)));
	cmnd->rw.length = cpu_to_le16((blk_rq_bytes(req) >> ns->lba_shift) - 1);
	cmnd->rw.control = cpu_to_le16(control);
	cmnd->rw.dsmgmt = cpu_to_le32(dsmgmt);

	if (++nvmeq->sq_tail == nvmeq->q_depth)
		nvmeq->sq_tail = 0;
	writel(nvmeq->sq_tail, nvmeq->q_db);

	return 0;
}

static int nvme_queue_rq(struct blk_mq_hw_ctx *hctx,
			 const struct blk_mq_queue_data *bd)
{
	struct nvme_ns *ns = hctx->queue->queuedata;
	struct nvme_queue *nvmeq = hctx->driver_data;
	struct request *req = bd->rq;
	struct nvme_cmd_info *cmd = blk_mq_rq_to_pdu(req);
	struct nvme_iod *iod;
	enum dma_data_direction dma_dir;

	iod = nvme_alloc_iod(req, ns->dev, GFP_ATOMIC);
	if (!iod)
		return BLK_MQ_RQ_QUEUE_BUSY;

	if (req->cmd_flags & REQ_DISCARD) {
		void *range;
		/*
		 * We reuse the small pool to allocate the 16-byte range here
		 * as it is not worth having a special pool for these or
		 * additional cases to handle freeing the iod.
		 */
		range = dma_pool_alloc(nvmeq->dev->prp_small_pool,
						GFP_ATOMIC,
						&iod->first_dma);
		if (!range)
			goto retry_cmd;
		iod_list(iod)[0] = (__le64 *)range;
		iod->npages = 0;
	} else if (req->nr_phys_segments) {
		dma_dir = rq_data_dir(req) ? DMA_TO_DEVICE : DMA_FROM_DEVICE;

		sg_init_table(iod->sg, req->nr_phys_segments);
		iod->nents = blk_rq_map_sg(req->q, req, iod->sg);
		if (!iod->nents)
			goto error_cmd;

		if (!dma_map_sg(nvmeq->q_dmadev, iod->sg, iod->nents, dma_dir))
			goto retry_cmd;

		if (blk_rq_bytes(req) !=
                    nvme_setup_prps(nvmeq->dev, iod, blk_rq_bytes(req), GFP_ATOMIC)) {
			dma_unmap_sg(&nvmeq->dev->pci_dev->dev, iod->sg,
					iod->nents, dma_dir);
			goto retry_cmd;
		}
	}

	nvme_set_info(cmd, iod, req_completion);
	spin_lock_irq(&nvmeq->q_lock);
	if (req->cmd_flags & REQ_DISCARD)
		nvme_submit_discard(nvmeq, ns, req, iod);
	else if (req->cmd_flags & REQ_FLUSH)
		nvme_submit_flush(nvmeq, ns, req->tag);
	else
		nvme_submit_iod(nvmeq, iod, ns);

	nvme_process_cq(nvmeq);
	spin_unlock_irq(&nvmeq->q_lock);
	return BLK_MQ_RQ_QUEUE_OK;

 error_cmd:
	nvme_free_iod(nvmeq->dev, iod);
	return BLK_MQ_RQ_QUEUE_ERROR;
 retry_cmd:
	nvme_free_iod(nvmeq->dev, iod);
	return BLK_MQ_RQ_QUEUE_BUSY;
}

static int nvme_process_cq(struct nvme_queue *nvmeq)
{
	u16 head, phase;

	head = nvmeq->cq_head;
	phase = nvmeq->cq_phase;

	for (;;) {
		void *ctx;
		nvme_completion_fn fn;
		struct nvme_completion cqe = nvmeq->cqes[head];
		if ((le16_to_cpu(cqe.status) & 1) != phase)
			break;
		nvmeq->sq_head = le16_to_cpu(cqe.sq_head);
		if (++head == nvmeq->q_depth) {
			head = 0;
			phase = !phase;
		}
		ctx = nvme_finish_cmd(nvmeq, cqe.command_id, &fn);
		fn(nvmeq, ctx, &cqe);
	}

	/* If the controller ignores the cq head doorbell and continuously
	 * writes to the queue, it is theoretically possible to wrap around
	 * the queue twice and mistakenly return IRQ_NONE.  Linux only
	 * requires that 0.1% of your interrupts are handled, so this isn't
	 * a big problem.
	 */
	if (head == nvmeq->cq_head && phase == nvmeq->cq_phase)
		return 0;

	writel(head, nvmeq->q_db + nvmeq->dev->db_stride);
	nvmeq->cq_head = head;
	nvmeq->cq_phase = phase;

	nvmeq->cqe_seen = 1;
	return 1;
}

/* Admin queue isn't initialized as a request queue. If at some point this
 * happens anyway, make sure to notify the user */
static int nvme_admin_queue_rq(struct blk_mq_hw_ctx *hctx,
			       const struct blk_mq_queue_data *bd)
{
	WARN_ON_ONCE(1);
	return BLK_MQ_RQ_QUEUE_ERROR;
}

static irqreturn_t nvme_irq(int irq, void *data)
{
	irqreturn_t result;
	struct nvme_queue *nvmeq = data;
	spin_lock(&nvmeq->q_lock);
	nvme_process_cq(nvmeq);
	result = nvmeq->cqe_seen ? IRQ_HANDLED : IRQ_NONE;
	nvmeq->cqe_seen = 0;
	spin_unlock(&nvmeq->q_lock);
	return result;
}

static irqreturn_t nvme_irq_check(int irq, void *data)
{
	struct nvme_queue *nvmeq = data;
	struct nvme_completion cqe = nvmeq->cqes[nvmeq->cq_head];
	if ((le16_to_cpu(cqe.status) & 1) != nvmeq->cq_phase)
		return IRQ_NONE;
	return IRQ_WAKE_THREAD;
}

static void nvme_abort_cmd_info(struct nvme_queue *nvmeq, struct nvme_cmd_info *
								cmd_info)
{
	spin_lock_irq(&nvmeq->q_lock);
	cancel_cmd_info(cmd_info, NULL);
	spin_unlock_irq(&nvmeq->q_lock);
}

struct sync_cmd_info {
	struct task_struct *task;
	u32 result;
	int status;
};

static void sync_completion(struct nvme_queue *nvmeq, void *ctx,
						struct nvme_completion *cqe)
{
	struct sync_cmd_info *cmdinfo = ctx;
	cmdinfo->result = le32_to_cpup(&cqe->result);
	cmdinfo->status = le16_to_cpup(&cqe->status) >> 1;
	wake_up_process(cmdinfo->task);
}

/*
 * Returns 0 on success.  If the result is negative, it's a Linux error code;
 * if the result is positive, it's an NVM Express status code
 */
static int nvme_submit_sync_cmd(struct request *req, struct nvme_command *cmd,
						u32 *result, unsigned timeout)
{
	int ret;
	struct sync_cmd_info cmdinfo;
	struct nvme_cmd_info *cmd_rq = blk_mq_rq_to_pdu(req);
	struct nvme_queue *nvmeq = cmd_rq->nvmeq;

	cmdinfo.task = current;
	cmdinfo.status = -EINTR;

	cmd->common.command_id = req->tag;

	nvme_set_info(cmd_rq, &cmdinfo, sync_completion);

	set_current_state(TASK_KILLABLE);
	ret = nvme_submit_cmd(nvmeq, cmd);
	if (ret) {
		nvme_finish_cmd(nvmeq, req->tag, NULL);
		set_current_state(TASK_RUNNING);
	}
	ret = schedule_timeout(timeout);

	/*
	 * Ensure that sync_completion has either run, or that it will
	 * never run.
	 */
	nvme_abort_cmd_info(nvmeq, blk_mq_rq_to_pdu(req));

	/*
	 * We never got the completion
	 */
	if (cmdinfo.status == -EINTR)
		return -EINTR;

	if (result)
		*result = cmdinfo.result;

	return cmdinfo.status;
}

static int nvme_submit_async_admin_req(struct nvme_dev *dev)
{
	struct nvme_queue *nvmeq = dev->queues[0];
	struct nvme_command c;
	struct nvme_cmd_info *cmd_info;
	struct request *req;

	req = blk_mq_alloc_request(dev->admin_q, WRITE, GFP_ATOMIC, false);
	if (IS_ERR(req))
		return PTR_ERR(req);

	req->cmd_flags |= REQ_NO_TIMEOUT;
	cmd_info = blk_mq_rq_to_pdu(req);
	nvme_set_info(cmd_info, req, async_req_completion);

	memset(&c, 0, sizeof(c));
	c.common.opcode = nvme_admin_async_event;
	c.common.command_id = req->tag;

	return __nvme_submit_cmd(nvmeq, &c);
}

static int nvme_submit_admin_async_cmd(struct nvme_dev *dev,
			struct nvme_command *cmd,
			struct async_cmd_info *cmdinfo, unsigned timeout)
{
	struct nvme_queue *nvmeq = dev->queues[0];
	struct request *req;
	struct nvme_cmd_info *cmd_rq;

	req = blk_mq_alloc_request(dev->admin_q, WRITE, GFP_KERNEL, false);
	if (IS_ERR(req))
		return PTR_ERR(req);

	req->timeout = timeout;
	cmd_rq = blk_mq_rq_to_pdu(req);
	cmdinfo->req = req;
	nvme_set_info(cmd_rq, cmdinfo, async_completion);
	cmdinfo->status = -EINTR;

	cmd->common.command_id = req->tag;

	return nvme_submit_cmd(nvmeq, cmd);
}

static int __nvme_submit_admin_cmd(struct nvme_dev *dev, struct nvme_command *cmd,
						u32 *result, unsigned timeout)
{
	int res;
	struct request *req;

	req = blk_mq_alloc_request(dev->admin_q, WRITE, GFP_KERNEL, false);
	if (IS_ERR(req))
		return PTR_ERR(req);
	res = nvme_submit_sync_cmd(req, cmd, result, timeout);
	blk_mq_free_request(req);
	return res;
}

int nvme_submit_admin_cmd(struct nvme_dev *dev, struct nvme_command *cmd,
								u32 *result)
{
	return __nvme_submit_admin_cmd(dev, cmd, result, ADMIN_TIMEOUT);
}

int nvme_submit_io_cmd(struct nvme_dev *dev, struct nvme_ns *ns,
					struct nvme_command *cmd, u32 *result)
{
	int res;
	struct request *req;

	req = blk_mq_alloc_request(ns->queue, WRITE, (GFP_KERNEL|__GFP_WAIT),
									false);
	if (IS_ERR(req))
		return PTR_ERR(req);
	res = nvme_submit_sync_cmd(req, cmd, result, NVME_IO_TIMEOUT);
	blk_mq_free_request(req);
	return res;
}

static int adapter_delete_queue(struct nvme_dev *dev, u8 opcode, u16 id)
{
	struct nvme_command c;

	memset(&c, 0, sizeof(c));
	c.delete_queue.opcode = opcode;
	c.delete_queue.qid = cpu_to_le16(id);

	return nvme_submit_admin_cmd(dev, &c, NULL);
}

static int adapter_alloc_cq(struct nvme_dev *dev, u16 qid,
						struct nvme_queue *nvmeq)
{
	struct nvme_command c;
	int flags = NVME_QUEUE_PHYS_CONTIG | NVME_CQ_IRQ_ENABLED;

	memset(&c, 0, sizeof(c));
	c.create_cq.opcode = nvme_admin_create_cq;
	c.create_cq.prp1 = cpu_to_le64(nvmeq->cq_dma_addr);
	c.create_cq.cqid = cpu_to_le16(qid);
	c.create_cq.qsize = cpu_to_le16(nvmeq->q_depth - 1);
	c.create_cq.cq_flags = cpu_to_le16(flags);
	c.create_cq.irq_vector = cpu_to_le16(nvmeq->cq_vector);

	return nvme_submit_admin_cmd(dev, &c, NULL);
}

static int adapter_alloc_sq(struct nvme_dev *dev, u16 qid,
						struct nvme_queue *nvmeq)
{
	struct nvme_command c;
	int flags = NVME_QUEUE_PHYS_CONTIG | NVME_SQ_PRIO_MEDIUM;

	memset(&c, 0, sizeof(c));
	c.create_sq.opcode = nvme_admin_create_sq;
	c.create_sq.prp1 = cpu_to_le64(nvmeq->sq_dma_addr);
	c.create_sq.sqid = cpu_to_le16(qid);
	c.create_sq.qsize = cpu_to_le16(nvmeq->q_depth - 1);
	c.create_sq.sq_flags = cpu_to_le16(flags);
	c.create_sq.cqid = cpu_to_le16(qid);

	return nvme_submit_admin_cmd(dev, &c, NULL);
}

static int adapter_delete_cq(struct nvme_dev *dev, u16 cqid)
{
	return adapter_delete_queue(dev, nvme_admin_delete_cq, cqid);
}

static int adapter_delete_sq(struct nvme_dev *dev, u16 sqid)
{
	return adapter_delete_queue(dev, nvme_admin_delete_sq, sqid);
}

int nvme_identify(struct nvme_dev *dev, unsigned nsid, unsigned cns,
							dma_addr_t dma_addr)
{
	struct nvme_command c;

	memset(&c, 0, sizeof(c));
	c.identify.opcode = nvme_admin_identify;
	c.identify.nsid = cpu_to_le32(nsid);
	c.identify.prp1 = cpu_to_le64(dma_addr);
	c.identify.cns = cpu_to_le32(cns);

	return nvme_submit_admin_cmd(dev, &c, NULL);
}

int nvme_get_features(struct nvme_dev *dev, unsigned fid, unsigned nsid,
					dma_addr_t dma_addr, u32 *result)
{
	struct nvme_command c;

	memset(&c, 0, sizeof(c));
	c.features.opcode = nvme_admin_get_features;
	c.features.nsid = cpu_to_le32(nsid);
	c.features.prp1 = cpu_to_le64(dma_addr);
	c.features.fid = cpu_to_le32(fid);

	return nvme_submit_admin_cmd(dev, &c, result);
}

int nvme_set_features(struct nvme_dev *dev, unsigned fid, unsigned dword11,
					dma_addr_t dma_addr, u32 *result)
{
	struct nvme_command c;

	memset(&c, 0, sizeof(c));
	c.features.opcode = nvme_admin_set_features;
	c.features.prp1 = cpu_to_le64(dma_addr);
	c.features.fid = cpu_to_le32(fid);
	c.features.dword11 = cpu_to_le32(dword11);

	return nvme_submit_admin_cmd(dev, &c, result);
}

/**
 * nvme_abort_req - Attempt aborting a request
 *
 * Schedule controller reset if the command was already aborted once before and
 * still hasn't been returned to the driver, or if this is the admin queue.
 */
static void nvme_abort_req(struct request *req)
{
	struct nvme_cmd_info *cmd_rq = blk_mq_rq_to_pdu(req);
	struct nvme_queue *nvmeq = cmd_rq->nvmeq;
	struct nvme_dev *dev = nvmeq->dev;
	struct request *abort_req;
	struct nvme_cmd_info *abort_cmd;
	struct nvme_command cmd;

	if (!nvmeq->qid || cmd_rq->aborted) {
		unsigned long flags;

		spin_lock_irqsave(&dev_list_lock, flags);
		if (work_busy(&dev->reset_work))
			goto out;
		list_del_init(&dev->node);
		dev_warn(&dev->pci_dev->dev,
			"I/O %d QID %d timeout, reset controller\n",
							req->tag, nvmeq->qid);
		dev->reset_workfn = nvme_reset_failed_dev;
		queue_work(nvme_workq, &dev->reset_work);
 out:
		spin_unlock_irqrestore(&dev_list_lock, flags);
		return;
	}

	if (!dev->abort_limit)
		return;

	abort_req = blk_mq_alloc_request(dev->admin_q, WRITE, GFP_ATOMIC,
									false);
	if (IS_ERR(abort_req))
		return;

	abort_cmd = blk_mq_rq_to_pdu(abort_req);
	nvme_set_info(abort_cmd, abort_req, abort_completion);

	memset(&cmd, 0, sizeof(cmd));
	cmd.abort.opcode = nvme_admin_abort_cmd;
	cmd.abort.cid = req->tag;
	cmd.abort.sqid = cpu_to_le16(nvmeq->qid);
	cmd.abort.command_id = abort_req->tag;

	--dev->abort_limit;
	cmd_rq->aborted = 1;

	dev_warn(nvmeq->q_dmadev, "Aborting I/O %d QID %d\n", req->tag,
							nvmeq->qid);
	if (nvme_submit_cmd(dev->queues[0], &cmd) < 0) {
		dev_warn(nvmeq->q_dmadev,
				"Could not abort I/O %d QID %d",
				req->tag, nvmeq->qid);
		blk_mq_free_request(abort_req);
	}
}

static void nvme_cancel_queue_ios(struct blk_mq_hw_ctx *hctx,
				struct request *req, void *data, bool reserved)
{
	struct nvme_queue *nvmeq = data;
	void *ctx;
	nvme_completion_fn fn;
	struct nvme_cmd_info *cmd;
	struct nvme_completion cqe;

	if (!blk_mq_request_started(req))
		return;

	cmd = blk_mq_rq_to_pdu(req);

	if (cmd->ctx == CMD_CTX_CANCELLED)
		return;

	if (blk_queue_dying(req->q))
		cqe.status = cpu_to_le16((NVME_SC_ABORT_REQ | NVME_SC_DNR) << 1);
	else
		cqe.status = cpu_to_le16(NVME_SC_ABORT_REQ << 1);


	dev_warn(nvmeq->q_dmadev, "Cancelling I/O %d QID %d\n",
						req->tag, nvmeq->qid);
	ctx = cancel_cmd_info(cmd, &fn);
	fn(nvmeq, ctx, &cqe);
}

static enum blk_eh_timer_return nvme_timeout(struct request *req, bool reserved)
{
	struct nvme_cmd_info *cmd = blk_mq_rq_to_pdu(req);
	struct nvme_queue *nvmeq = cmd->nvmeq;

	/*
	 * The aborted req will be completed on receiving the abort req.
	 * We enable the timer again. If hit twice, it'll cause a device reset,
	 * as the device then is in a faulty state.
	 */
	int ret = BLK_EH_RESET_TIMER;

	dev_warn(nvmeq->q_dmadev, "Timeout I/O %d QID %d\n", req->tag,
							nvmeq->qid);

	spin_lock_irq(&nvmeq->q_lock);
	if (!nvmeq->dev->initialized) {
		/*
		 * Force cancelled command frees the request, which requires we
		 * return BLK_EH_NOT_HANDLED.
		 */
		nvme_cancel_queue_ios(nvmeq->hctx, req, nvmeq, reserved);
		ret = BLK_EH_NOT_HANDLED;
	} else
		nvme_abort_req(req);
	spin_unlock_irq(&nvmeq->q_lock);

	return ret;
}

static void nvme_free_queue(struct nvme_queue *nvmeq)
{
	dma_free_coherent(nvmeq->q_dmadev, CQ_SIZE(nvmeq->q_depth),
				(void *)nvmeq->cqes, nvmeq->cq_dma_addr);
	dma_free_coherent(nvmeq->q_dmadev, SQ_SIZE(nvmeq->q_depth),
					nvmeq->sq_cmds, nvmeq->sq_dma_addr);
	kfree(nvmeq);
}

static void nvme_free_queues(struct nvme_dev *dev, int lowest)
{
	int i;

	for (i = dev->queue_count - 1; i >= lowest; i--) {
		struct nvme_queue *nvmeq = dev->queues[i];
		dev->queue_count--;
		dev->queues[i] = NULL;
		nvme_free_queue(nvmeq);
	}
}

/**
 * nvme_suspend_queue - put queue into suspended state
 * @nvmeq - queue to suspend
 */
static int nvme_suspend_queue(struct nvme_queue *nvmeq)
{
	int vector;

	spin_lock_irq(&nvmeq->q_lock);
	if (nvmeq->cq_vector == -1) {
		spin_unlock_irq(&nvmeq->q_lock);
		return 1;
	}
	vector = nvmeq->dev->entry[nvmeq->cq_vector].vector;
	nvmeq->dev->online_queues--;
	nvmeq->cq_vector = -1;
	spin_unlock_irq(&nvmeq->q_lock);

	irq_set_affinity_hint(vector, NULL);
	free_irq(vector, nvmeq);

	return 0;
}

static void nvme_clear_queue(struct nvme_queue *nvmeq)
{
	struct blk_mq_hw_ctx *hctx = nvmeq->hctx;

	spin_lock_irq(&nvmeq->q_lock);
	nvme_process_cq(nvmeq);
	if (hctx && hctx->tags)
		blk_mq_tag_busy_iter(hctx, nvme_cancel_queue_ios, nvmeq);
	spin_unlock_irq(&nvmeq->q_lock);
}

static void nvme_disable_queue(struct nvme_dev *dev, int qid)
{
	struct nvme_queue *nvmeq = dev->queues[qid];

	if (!nvmeq)
		return;
	if (nvme_suspend_queue(nvmeq))
		return;

	/* Don't tell the adapter to delete the admin queue.
	 * Don't tell a removed adapter to delete IO queues. */
	if (qid && readl(&dev->bar->csts) != -1) {
		adapter_delete_sq(dev, qid);
		adapter_delete_cq(dev, qid);
	}
	if (!qid && dev->admin_q)
		blk_mq_freeze_queue_start(dev->admin_q);
	nvme_clear_queue(nvmeq);
}

static struct nvme_queue *nvme_alloc_queue(struct nvme_dev *dev, int qid,
							int depth)
{
	struct device *dmadev = &dev->pci_dev->dev;
	struct nvme_queue *nvmeq = kzalloc(sizeof(*nvmeq), GFP_KERNEL);
	if (!nvmeq)
		return NULL;

	nvmeq->cqes = dma_zalloc_coherent(dmadev, CQ_SIZE(depth),
					  &nvmeq->cq_dma_addr, GFP_KERNEL);
	if (!nvmeq->cqes)
		goto free_nvmeq;

	nvmeq->sq_cmds = dma_alloc_coherent(dmadev, SQ_SIZE(depth),
					&nvmeq->sq_dma_addr, GFP_KERNEL);
	if (!nvmeq->sq_cmds)
		goto free_cqdma;

	nvmeq->q_dmadev = dmadev;
	nvmeq->dev = dev;
	snprintf(nvmeq->irqname, sizeof(nvmeq->irqname), "nvme%dq%d",
			dev->instance, qid);
	spin_lock_init(&nvmeq->q_lock);
	nvmeq->cq_head = 0;
	nvmeq->cq_phase = 1;
	nvmeq->q_db = &dev->dbs[qid * 2 * dev->db_stride];
	nvmeq->q_depth = depth;
	nvmeq->qid = qid;
	dev->queue_count++;
	dev->queues[qid] = nvmeq;

	return nvmeq;

 free_cqdma:
	dma_free_coherent(dmadev, CQ_SIZE(depth), (void *)nvmeq->cqes,
							nvmeq->cq_dma_addr);
 free_nvmeq:
	kfree(nvmeq);
	return NULL;
}

static int queue_request_irq(struct nvme_dev *dev, struct nvme_queue *nvmeq,
							const char *name)
{
	if (use_threaded_interrupts)
		return request_threaded_irq(dev->entry[nvmeq->cq_vector].vector,
					nvme_irq_check, nvme_irq, IRQF_SHARED,
					name, nvmeq);
	return request_irq(dev->entry[nvmeq->cq_vector].vector, nvme_irq,
				IRQF_SHARED, name, nvmeq);
}

static void nvme_init_queue(struct nvme_queue *nvmeq, u16 qid)
{
	struct nvme_dev *dev = nvmeq->dev;

	spin_lock_irq(&nvmeq->q_lock);
	nvmeq->sq_tail = 0;
	nvmeq->cq_head = 0;
	nvmeq->cq_phase = 1;
	nvmeq->q_db = &dev->dbs[qid * 2 * dev->db_stride];
	memset((void *)nvmeq->cqes, 0, CQ_SIZE(nvmeq->q_depth));
	dev->online_queues++;
	spin_unlock_irq(&nvmeq->q_lock);
}

static int nvme_create_queue(struct nvme_queue *nvmeq, int qid)
{
	struct nvme_dev *dev = nvmeq->dev;
	int result;

	nvmeq->cq_vector = qid - 1;
	result = adapter_alloc_cq(dev, qid, nvmeq);
	if (result < 0)
		return result;

	result = adapter_alloc_sq(dev, qid, nvmeq);
	if (result < 0)
		goto release_cq;

	result = queue_request_irq(dev, nvmeq, nvmeq->irqname);
	if (result < 0)
		goto release_sq;

	nvme_init_queue(nvmeq, qid);
	return result;

 release_sq:
	adapter_delete_sq(dev, qid);
 release_cq:
	adapter_delete_cq(dev, qid);
	return result;
}

static int nvme_wait_ready(struct nvme_dev *dev, u64 cap, bool enabled)
{
	unsigned long timeout;
	u32 bit = enabled ? NVME_CSTS_RDY : 0;

	timeout = ((NVME_CAP_TIMEOUT(cap) + 1) * HZ / 2) + jiffies;

	while ((readl(&dev->bar->csts) & NVME_CSTS_RDY) != bit) {
		msleep(100);
		if (fatal_signal_pending(current))
			return -EINTR;
		if (time_after(jiffies, timeout)) {
			dev_err(&dev->pci_dev->dev,
				"Device not ready; aborting %s\n", enabled ?
						"initialisation" : "reset");
			return -ENODEV;
		}
	}

	return 0;
}

/*
 * If the device has been passed off to us in an enabled state, just clear
 * the enabled bit.  The spec says we should set the 'shutdown notification
 * bits', but doing so may cause the device to complete commands to the
 * admin queue ... and we don't know what memory that might be pointing at!
 */
static int nvme_disable_ctrl(struct nvme_dev *dev, u64 cap)
{
	dev->ctrl_config &= ~NVME_CC_SHN_MASK;
	dev->ctrl_config &= ~NVME_CC_ENABLE;
	writel(dev->ctrl_config, &dev->bar->cc);

	return nvme_wait_ready(dev, cap, false);
}

static int nvme_enable_ctrl(struct nvme_dev *dev, u64 cap)
{
	dev->ctrl_config &= ~NVME_CC_SHN_MASK;
	dev->ctrl_config |= NVME_CC_ENABLE;
	writel(dev->ctrl_config, &dev->bar->cc);

	return nvme_wait_ready(dev, cap, true);
}

static int nvme_shutdown_ctrl(struct nvme_dev *dev)
{
	unsigned long timeout;

	dev->ctrl_config &= ~NVME_CC_SHN_MASK;
	dev->ctrl_config |= NVME_CC_SHN_NORMAL;

	writel(dev->ctrl_config, &dev->bar->cc);

	timeout = SHUTDOWN_TIMEOUT + jiffies;
	while ((readl(&dev->bar->csts) & NVME_CSTS_SHST_MASK) !=
							NVME_CSTS_SHST_CMPLT) {
		msleep(100);
		if (fatal_signal_pending(current))
			return -EINTR;
		if (time_after(jiffies, timeout)) {
			dev_err(&dev->pci_dev->dev,
				"Device shutdown incomplete; abort shutdown\n");
			return -ENODEV;
		}
	}

	return 0;
}

static struct blk_mq_ops nvme_mq_admin_ops = {
	.queue_rq	= nvme_admin_queue_rq,
	.map_queue	= blk_mq_map_queue,
	.init_hctx	= nvme_admin_init_hctx,
	.exit_hctx	= nvme_exit_hctx,
	.init_request	= nvme_admin_init_request,
	.timeout	= nvme_timeout,
};

static struct blk_mq_ops nvme_mq_ops = {
	.queue_rq	= nvme_queue_rq,
	.map_queue	= blk_mq_map_queue,
	.init_hctx	= nvme_init_hctx,
	.exit_hctx	= nvme_exit_hctx,
	.init_request	= nvme_init_request,
	.timeout	= nvme_timeout,
};

static void nvme_dev_remove_admin(struct nvme_dev *dev)
{
	if (dev->admin_q && !blk_queue_dying(dev->admin_q)) {
		blk_cleanup_queue(dev->admin_q);
		blk_mq_free_tag_set(&dev->admin_tagset);
	}
}

static int nvme_alloc_admin_tags(struct nvme_dev *dev)
{
	if (!dev->admin_q) {
		dev->admin_tagset.ops = &nvme_mq_admin_ops;
		dev->admin_tagset.nr_hw_queues = 1;
		dev->admin_tagset.queue_depth = NVME_AQ_DEPTH - 1;
		dev->admin_tagset.timeout = ADMIN_TIMEOUT;
		dev->admin_tagset.numa_node = dev_to_node(&dev->pci_dev->dev);
		dev->admin_tagset.cmd_size = nvme_cmd_size(dev);
		dev->admin_tagset.driver_data = dev;

		if (blk_mq_alloc_tag_set(&dev->admin_tagset))
			return -ENOMEM;

		dev->admin_q = blk_mq_init_queue(&dev->admin_tagset);
		if (IS_ERR(dev->admin_q)) {
			blk_mq_free_tag_set(&dev->admin_tagset);
			return -ENOMEM;
		}
		if (!blk_get_queue(dev->admin_q)) {
			nvme_dev_remove_admin(dev);
			return -ENODEV;
		}
	} else
		blk_mq_unfreeze_queue(dev->admin_q);

	return 0;
}

static int nvme_configure_admin_queue(struct nvme_dev *dev)
{
	int result;
	u32 aqa;
	u64 cap = readq(&dev->bar->cap);
	struct nvme_queue *nvmeq;
	unsigned page_shift = PAGE_SHIFT;
	unsigned dev_page_min = NVME_CAP_MPSMIN(cap) + 12;
	unsigned dev_page_max = NVME_CAP_MPSMAX(cap) + 12;

	if (page_shift < dev_page_min) {
		dev_err(&dev->pci_dev->dev,
				"Minimum device page size (%u) too large for "
				"host (%u)\n", 1 << dev_page_min,
				1 << page_shift);
		return -ENODEV;
	}
	if (page_shift > dev_page_max) {
		dev_info(&dev->pci_dev->dev,
				"Device maximum page size (%u) smaller than "
				"host (%u); enabling work-around\n",
				1 << dev_page_max, 1 << page_shift);
		page_shift = dev_page_max;
	}

	result = nvme_disable_ctrl(dev, cap);
	if (result < 0)
		return result;

	nvmeq = dev->queues[0];
	if (!nvmeq) {
		nvmeq = nvme_alloc_queue(dev, 0, NVME_AQ_DEPTH);
		if (!nvmeq)
			return -ENOMEM;
	}

	aqa = nvmeq->q_depth - 1;
	aqa |= aqa << 16;

	dev->page_size = 1 << page_shift;

	dev->ctrl_config = NVME_CC_CSS_NVM;
	dev->ctrl_config |= (page_shift - 12) << NVME_CC_MPS_SHIFT;
	dev->ctrl_config |= NVME_CC_ARB_RR | NVME_CC_SHN_NONE;
	dev->ctrl_config |= NVME_CC_IOSQES | NVME_CC_IOCQES;

	writel(aqa, &dev->bar->aqa);
	writeq(nvmeq->sq_dma_addr, &dev->bar->asq);
	writeq(nvmeq->cq_dma_addr, &dev->bar->acq);

	result = nvme_enable_ctrl(dev, cap);
	if (result)
		goto free_nvmeq;

	nvmeq->cq_vector = 0;
	result = queue_request_irq(dev, nvmeq, nvmeq->irqname);
	if (result)
		goto free_nvmeq;

	return result;

 free_nvmeq:
	nvme_free_queues(dev, 0);
	return result;
}

struct nvme_iod *nvme_map_user_pages(struct nvme_dev *dev, int write,
				unsigned long addr, unsigned length)
{
	int i, err, count, nents, offset;
	struct scatterlist *sg;
	struct page **pages;
	struct nvme_iod *iod;

	if (addr & 3)
		return ERR_PTR(-EINVAL);
	if (!length || length > INT_MAX - PAGE_SIZE)
		return ERR_PTR(-EINVAL);

	offset = offset_in_page(addr);
	count = DIV_ROUND_UP(offset + length, PAGE_SIZE);
	pages = kcalloc(count, sizeof(*pages), GFP_KERNEL);
	if (!pages)
		return ERR_PTR(-ENOMEM);

	err = get_user_pages_fast(addr, count, 1, pages);
	if (err < count) {
		count = err;
		err = -EFAULT;
		goto put_pages;
	}

	err = -ENOMEM;
	iod = __nvme_alloc_iod(count, length, dev, 0, GFP_KERNEL);
	if (!iod)
		goto put_pages;

	sg = iod->sg;
	sg_init_table(sg, count);
	for (i = 0; i < count; i++) {
		sg_set_page(&sg[i], pages[i],
			    min_t(unsigned, length, PAGE_SIZE - offset),
			    offset);
		length -= (PAGE_SIZE - offset);
		offset = 0;
	}
	sg_mark_end(&sg[i - 1]);
	iod->nents = count;

	nents = dma_map_sg(&dev->pci_dev->dev, sg, count,
				write ? DMA_TO_DEVICE : DMA_FROM_DEVICE);
	if (!nents)
		goto free_iod;

	kfree(pages);
	return iod;

 free_iod:
	kfree(iod);
 put_pages:
	for (i = 0; i < count; i++)
		put_page(pages[i]);
	kfree(pages);
	return ERR_PTR(err);
}

void nvme_unmap_user_pages(struct nvme_dev *dev, int write,
			struct nvme_iod *iod)
{
	int i;

	dma_unmap_sg(&dev->pci_dev->dev, iod->sg, iod->nents,
				write ? DMA_TO_DEVICE : DMA_FROM_DEVICE);

	for (i = 0; i < iod->nents; i++)
		put_page(sg_page(&iod->sg[i]));
}

static int nvme_submit_io(struct nvme_ns *ns, struct nvme_user_io __user *uio)
{
	struct nvme_dev *dev = ns->dev;
	struct nvme_user_io io;
	struct nvme_command c;
	unsigned length, meta_len;
	int status, i;
	struct nvme_iod *iod, *meta_iod = NULL;
	dma_addr_t meta_dma_addr;
	void *meta, *uninitialized_var(meta_mem);

	if (copy_from_user(&io, uio, sizeof(io)))
		return -EFAULT;
	length = (io.nblocks + 1) << ns->lba_shift;
	meta_len = (io.nblocks + 1) * ns->ms;

	if (meta_len && ((io.metadata & 3) || !io.metadata))
		return -EINVAL;

	switch (io.opcode) {
	case nvme_cmd_write:
	case nvme_cmd_read:
	case nvme_cmd_compare:
		iod = nvme_map_user_pages(dev, io.opcode & 1, io.addr, length);
		break;
	default:
		return -EINVAL;
	}

	if (IS_ERR(iod))
		return PTR_ERR(iod);

	memset(&c, 0, sizeof(c));
	c.rw.opcode = io.opcode;
	c.rw.flags = io.flags;
	c.rw.nsid = cpu_to_le32(ns->ns_id);
	c.rw.slba = cpu_to_le64(io.slba);
	c.rw.length = cpu_to_le16(io.nblocks);
	c.rw.control = cpu_to_le16(io.control);
	c.rw.dsmgmt = cpu_to_le32(io.dsmgmt);
	c.rw.reftag = cpu_to_le32(io.reftag);
	c.rw.apptag = cpu_to_le16(io.apptag);
	c.rw.appmask = cpu_to_le16(io.appmask);

	if (meta_len) {
		meta_iod = nvme_map_user_pages(dev, io.opcode & 1, io.metadata,
								meta_len);
		if (IS_ERR(meta_iod)) {
			status = PTR_ERR(meta_iod);
			meta_iod = NULL;
			goto unmap;
		}

		meta_mem = dma_alloc_coherent(&dev->pci_dev->dev, meta_len,
						&meta_dma_addr, GFP_KERNEL);
		if (!meta_mem) {
			status = -ENOMEM;
			goto unmap;
		}

		if (io.opcode & 1) {
			int meta_offset = 0;

			for (i = 0; i < meta_iod->nents; i++) {
				meta = kmap_atomic(sg_page(&meta_iod->sg[i])) +
						meta_iod->sg[i].offset;
				memcpy(meta_mem + meta_offset, meta,
						meta_iod->sg[i].length);
				kunmap_atomic(meta);
				meta_offset += meta_iod->sg[i].length;
			}
		}

		c.rw.metadata = cpu_to_le64(meta_dma_addr);
	}

	length = nvme_setup_prps(dev, iod, length, GFP_KERNEL);
	c.rw.prp1 = cpu_to_le64(sg_dma_address(iod->sg));
	c.rw.prp2 = cpu_to_le64(iod->first_dma);

	if (length != (io.nblocks + 1) << ns->lba_shift)
		status = -ENOMEM;
	else
		status = nvme_submit_io_cmd(dev, ns, &c, NULL);

	if (meta_len) {
		if (status == NVME_SC_SUCCESS && !(io.opcode & 1)) {
			int meta_offset = 0;

			for (i = 0; i < meta_iod->nents; i++) {
				meta = kmap_atomic(sg_page(&meta_iod->sg[i])) +
						meta_iod->sg[i].offset;
				memcpy(meta, meta_mem + meta_offset,
						meta_iod->sg[i].length);
				kunmap_atomic(meta);
				meta_offset += meta_iod->sg[i].length;
			}
		}

		dma_free_coherent(&dev->pci_dev->dev, meta_len, meta_mem,
								meta_dma_addr);
	}

 unmap:
	nvme_unmap_user_pages(dev, io.opcode & 1, iod);
	nvme_free_iod(dev, iod);

	if (meta_iod) {
		nvme_unmap_user_pages(dev, io.opcode & 1, meta_iod);
		nvme_free_iod(dev, meta_iod);
	}

	return status;
}

static int nvme_user_cmd(struct nvme_dev *dev, struct nvme_ns *ns,
			struct nvme_passthru_cmd __user *ucmd)
{
	struct nvme_passthru_cmd cmd;
	struct nvme_command c;
	int status, length;
	struct nvme_iod *uninitialized_var(iod);
	unsigned timeout;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;
	if (copy_from_user(&cmd, ucmd, sizeof(cmd)))
		return -EFAULT;

	memset(&c, 0, sizeof(c));
	c.common.opcode = cmd.opcode;
	c.common.flags = cmd.flags;
	c.common.nsid = cpu_to_le32(cmd.nsid);
	c.common.cdw2[0] = cpu_to_le32(cmd.cdw2);
	c.common.cdw2[1] = cpu_to_le32(cmd.cdw3);
	c.common.cdw10[0] = cpu_to_le32(cmd.cdw10);
	c.common.cdw10[1] = cpu_to_le32(cmd.cdw11);
	c.common.cdw10[2] = cpu_to_le32(cmd.cdw12);
	c.common.cdw10[3] = cpu_to_le32(cmd.cdw13);
	c.common.cdw10[4] = cpu_to_le32(cmd.cdw14);
	c.common.cdw10[5] = cpu_to_le32(cmd.cdw15);

	length = cmd.data_len;
	if (cmd.data_len) {
		iod = nvme_map_user_pages(dev, cmd.opcode & 1, cmd.addr,
								length);
		if (IS_ERR(iod))
			return PTR_ERR(iod);
		length = nvme_setup_prps(dev, iod, length, GFP_KERNEL);
		c.common.prp1 = cpu_to_le64(sg_dma_address(iod->sg));
		c.common.prp2 = cpu_to_le64(iod->first_dma);
	}

	timeout = cmd.timeout_ms ? msecs_to_jiffies(cmd.timeout_ms) :
								ADMIN_TIMEOUT;

	if (length != cmd.data_len)
		status = -ENOMEM;
	else if (ns) {
		struct request *req;

		req = blk_mq_alloc_request(ns->queue, WRITE,
						(GFP_KERNEL|__GFP_WAIT), false);
		if (IS_ERR(req))
			status = PTR_ERR(req);
		else {
			status = nvme_submit_sync_cmd(req, &c, &cmd.result,
								timeout);
			blk_mq_free_request(req);
		}
	} else
		status = __nvme_submit_admin_cmd(dev, &c, &cmd.result, timeout);

	if (cmd.data_len) {
		nvme_unmap_user_pages(dev, cmd.opcode & 1, iod);
		nvme_free_iod(dev, iod);
	}

	if ((status >= 0) && copy_to_user(&ucmd->result, &cmd.result,
							sizeof(cmd.result)))
		status = -EFAULT;

	return status;
}

static int nvme_ioctl(struct block_device *bdev, fmode_t mode, unsigned int cmd,
							unsigned long arg)
{
	struct nvme_ns *ns = bdev->bd_disk->private_data;

	switch (cmd) {
	case NVME_IOCTL_ID:
		force_successful_syscall_return();
		return ns->ns_id;
	case NVME_IOCTL_ADMIN_CMD:
		return nvme_user_cmd(ns->dev, NULL, (void __user *)arg);
	case NVME_IOCTL_IO_CMD:
		return nvme_user_cmd(ns->dev, ns, (void __user *)arg);
	case NVME_IOCTL_SUBMIT_IO:
		return nvme_submit_io(ns, (void __user *)arg);
	case SG_GET_VERSION_NUM:
		return nvme_sg_get_version_num((void __user *)arg);
	case SG_IO:
		return nvme_sg_io(ns, (void __user *)arg);
	default:
		return -ENOTTY;
	}
}

#ifdef CONFIG_COMPAT
static int nvme_compat_ioctl(struct block_device *bdev, fmode_t mode,
					unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case SG_IO:
		return -ENOIOCTLCMD;
	}
	return nvme_ioctl(bdev, mode, cmd, arg);
}
#else
#define nvme_compat_ioctl	NULL
#endif

static int nvme_open(struct block_device *bdev, fmode_t mode)
{
	int ret = 0;
	struct nvme_ns *ns;

	spin_lock(&dev_list_lock);
	ns = bdev->bd_disk->private_data;
	if (!ns)
		ret = -ENXIO;
	else if (!kref_get_unless_zero(&ns->dev->kref))
		ret = -ENXIO;
	spin_unlock(&dev_list_lock);

	return ret;
}

static void nvme_free_dev(struct kref *kref);

static void nvme_release(struct gendisk *disk, fmode_t mode)
{
	struct nvme_ns *ns = disk->private_data;
	struct nvme_dev *dev = ns->dev;

	kref_put(&dev->kref, nvme_free_dev);
}

static int nvme_getgeo(struct block_device *bd, struct hd_geometry *geo)
{
	/* some standard values */
	geo->heads = 1 << 6;
	geo->sectors = 1 << 5;
	geo->cylinders = get_capacity(bd->bd_disk) >> 11;
	return 0;
}

static int nvme_revalidate_disk(struct gendisk *disk)
{
	struct nvme_ns *ns = disk->private_data;
	struct nvme_dev *dev = ns->dev;
	struct nvme_id_ns *id;
	dma_addr_t dma_addr;
	int lbaf;

	id = dma_alloc_coherent(&dev->pci_dev->dev, 4096, &dma_addr,
								GFP_KERNEL);
	if (!id) {
		dev_warn(&dev->pci_dev->dev, "%s: Memory alocation failure\n",
								__func__);
		return 0;
	}

	if (nvme_identify(dev, ns->ns_id, 0, dma_addr))
		goto free;

	lbaf = id->flbas & 0xf;
	ns->lba_shift = id->lbaf[lbaf].ds;

	blk_queue_logical_block_size(ns->queue, 1 << ns->lba_shift);
	set_capacity(disk, le64_to_cpup(&id->nsze) << (ns->lba_shift - 9));
 free:
	dma_free_coherent(&dev->pci_dev->dev, 4096, id, dma_addr);
	return 0;
}

static const struct block_device_operations nvme_fops = {
	.owner		= THIS_MODULE,
	.ioctl		= nvme_ioctl,
	.compat_ioctl	= nvme_compat_ioctl,
	.open		= nvme_open,
	.release	= nvme_release,
	.getgeo		= nvme_getgeo,
	.revalidate_disk= nvme_revalidate_disk,
};

static int nvme_kthread(void *data)
{
	struct nvme_dev *dev, *next;

	while (!kthread_should_stop()) {
		set_current_state(TASK_INTERRUPTIBLE);
		spin_lock(&dev_list_lock);
		list_for_each_entry_safe(dev, next, &dev_list, node) {
			int i;
			if (readl(&dev->bar->csts) & NVME_CSTS_CFS &&
							dev->initialized) {
				if (work_busy(&dev->reset_work))
					continue;
				list_del_init(&dev->node);
				dev_warn(&dev->pci_dev->dev,
					"Failed status: %x, reset controller\n",
					readl(&dev->bar->csts));
				dev->reset_workfn = nvme_reset_failed_dev;
				queue_work(nvme_workq, &dev->reset_work);
				continue;
			}
			for (i = 0; i < dev->queue_count; i++) {
				struct nvme_queue *nvmeq = dev->queues[i];
				if (!nvmeq)
					continue;
				spin_lock_irq(&nvmeq->q_lock);
				nvme_process_cq(nvmeq);

				while ((i == 0) && (dev->event_limit > 0)) {
					if (nvme_submit_async_admin_req(dev))
						break;
					dev->event_limit--;
				}
				spin_unlock_irq(&nvmeq->q_lock);
			}
		}
		spin_unlock(&dev_list_lock);
		schedule_timeout(round_jiffies_relative(HZ));
	}
	return 0;
}

static void nvme_config_discard(struct nvme_ns *ns)
{
	u32 logical_block_size = queue_logical_block_size(ns->queue);
	ns->queue->limits.discard_zeroes_data = 0;
	ns->queue->limits.discard_alignment = logical_block_size;
	ns->queue->limits.discard_granularity = logical_block_size;
	ns->queue->limits.max_discard_sectors = 0xffffffff;
	queue_flag_set_unlocked(QUEUE_FLAG_DISCARD, ns->queue);
}

static struct nvme_ns *nvme_alloc_ns(struct nvme_dev *dev, unsigned nsid,
			struct nvme_id_ns *id, struct nvme_lba_range_type *rt)
{
	struct nvme_ns *ns;
	struct gendisk *disk;
	int node = dev_to_node(&dev->pci_dev->dev);
	int lbaf;

	if (rt->attributes & NVME_LBART_ATTRIB_HIDE)
		return NULL;

	ns = kzalloc_node(sizeof(*ns), GFP_KERNEL, node);
	if (!ns)
		return NULL;
	ns->queue = blk_mq_init_queue(&dev->tagset);
	if (IS_ERR(ns->queue))
		goto out_free_ns;
	queue_flag_set_unlocked(QUEUE_FLAG_NOMERGES, ns->queue);
	queue_flag_set_unlocked(QUEUE_FLAG_NONROT, ns->queue);
	queue_flag_set_unlocked(QUEUE_FLAG_SG_GAPS, ns->queue);
	ns->dev = dev;
	ns->queue->queuedata = ns;

	disk = alloc_disk_node(0, node);
	if (!disk)
		goto out_free_queue;

	ns->ns_id = nsid;
	ns->disk = disk;
	lbaf = id->flbas & 0xf;
	ns->lba_shift = id->lbaf[lbaf].ds;
	ns->ms = le16_to_cpu(id->lbaf[lbaf].ms);
	blk_queue_logical_block_size(ns->queue, 1 << ns->lba_shift);
	if (dev->max_hw_sectors)
		blk_queue_max_hw_sectors(ns->queue, dev->max_hw_sectors);
	if (dev->stripe_size)
		blk_queue_chunk_sectors(ns->queue, dev->stripe_size >> 9);
	if (dev->vwc & NVME_CTRL_VWC_PRESENT)
		blk_queue_flush(ns->queue, REQ_FLUSH | REQ_FUA);

	disk->major = nvme_major;
	disk->first_minor = 0;
	disk->fops = &nvme_fops;
	disk->private_data = ns;
	disk->queue = ns->queue;
	disk->driverfs_dev = &dev->pci_dev->dev;
	disk->flags = GENHD_FL_EXT_DEVT;
	sprintf(disk->disk_name, "nvme%dn%d", dev->instance, nsid);
	set_capacity(disk, le64_to_cpup(&id->nsze) << (ns->lba_shift - 9));

	if (dev->oncs & NVME_CTRL_ONCS_DSM)
		nvme_config_discard(ns);

	return ns;

 out_free_queue:
	blk_cleanup_queue(ns->queue);
 out_free_ns:
	kfree(ns);
	return NULL;
}

static void nvme_create_io_queues(struct nvme_dev *dev)
{
	unsigned i;

	for (i = dev->queue_count; i <= dev->max_qid; i++)
		if (!nvme_alloc_queue(dev, i, dev->q_depth))
			break;

	for (i = dev->online_queues; i <= dev->queue_count - 1; i++)
		if (nvme_create_queue(dev->queues[i], i))
			break;
}

static int set_queue_count(struct nvme_dev *dev, int count)
{
	int status;
	u32 result;
	u32 q_count = (count - 1) | ((count - 1) << 16);

	status = nvme_set_features(dev, NVME_FEAT_NUM_QUEUES, q_count, 0,
								&result);
	if (status < 0)
		return status;
	if (status > 0) {
		dev_err(&dev->pci_dev->dev, "Could not set queue count (%d)\n",
									status);
		return 0;
	}
	return min(result & 0xffff, result >> 16) + 1;
}

static size_t db_bar_size(struct nvme_dev *dev, unsigned nr_io_queues)
{
	return 4096 + ((nr_io_queues + 1) * 8 * dev->db_stride);
}

static int nvme_setup_io_queues(struct nvme_dev *dev)
{
	struct nvme_queue *adminq = dev->queues[0];
	struct pci_dev *pdev = dev->pci_dev;
	int result, i, vecs, nr_io_queues, size;

	nr_io_queues = num_possible_cpus();
	result = set_queue_count(dev, nr_io_queues);
	if (result <= 0)
		return result;
	if (result < nr_io_queues)
		nr_io_queues = result;

	size = db_bar_size(dev, nr_io_queues);
	if (size > 8192) {
		iounmap(dev->bar);
		do {
			dev->bar = ioremap(pci_resource_start(pdev, 0), size);
			if (dev->bar)
				break;
			if (!--nr_io_queues)
				return -ENOMEM;
			size = db_bar_size(dev, nr_io_queues);
		} while (1);
		dev->dbs = ((void __iomem *)dev->bar) + 4096;
		adminq->q_db = dev->dbs;
	}

	/* Deregister the admin queue's interrupt */
	free_irq(dev->entry[0].vector, adminq);

	/*
	 * If we enable msix early due to not intx, disable it again before
	 * setting up the full range we need.
	 */
	if (!pdev->irq)
		pci_disable_msix(pdev);

	for (i = 0; i < nr_io_queues; i++)
		dev->entry[i].entry = i;
	vecs = pci_enable_msix_range(pdev, dev->entry, 1, nr_io_queues);
	if (vecs < 0) {
		vecs = pci_enable_msi_range(pdev, 1, min(nr_io_queues, 32));
		if (vecs < 0) {
			vecs = 1;
		} else {
			for (i = 0; i < vecs; i++)
				dev->entry[i].vector = i + pdev->irq;
		}
	}

	/*
	 * Should investigate if there's a performance win from allocating
	 * more queues than interrupt vectors; it might allow the submission
	 * path to scale better, even if the receive path is limited by the
	 * number of interrupts.
	 */
	nr_io_queues = vecs;
	dev->max_qid = nr_io_queues;

	result = queue_request_irq(dev, adminq, adminq->irqname);
	if (result)
		goto free_queues;

	/* Free previously allocated queues that are no longer usable */
	nvme_free_queues(dev, nr_io_queues + 1);
	nvme_create_io_queues(dev);

	return 0;

 free_queues:
	nvme_free_queues(dev, 1);
	return result;
}

/*
 * Return: error value if an error occurred setting up the queues or calling
 * Identify Device.  0 if these succeeded, even if adding some of the
 * namespaces failed.  At the moment, these failures are silent.  TBD which
 * failures should be reported.
 */
static int nvme_dev_add(struct nvme_dev *dev)
{
	struct pci_dev *pdev = dev->pci_dev;
	int res;
	unsigned nn, i;
	struct nvme_ns *ns;
	struct nvme_id_ctrl *ctrl;
	struct nvme_id_ns *id_ns;
	void *mem;
	dma_addr_t dma_addr;
	int shift = NVME_CAP_MPSMIN(readq(&dev->bar->cap)) + 12;

	mem = dma_alloc_coherent(&pdev->dev, 8192, &dma_addr, GFP_KERNEL);
	if (!mem)
		return -ENOMEM;

	res = nvme_identify(dev, 0, 1, dma_addr);
	if (res) {
		dev_err(&pdev->dev, "Identify Controller failed (%d)\n", res);
		res = -EIO;
		goto out;
	}

	ctrl = mem;
	nn = le32_to_cpup(&ctrl->nn);
	dev->oncs = le16_to_cpup(&ctrl->oncs);
	dev->abort_limit = ctrl->acl + 1;
	dev->vwc = ctrl->vwc;
	dev->event_limit = min(ctrl->aerl + 1, 8);
	memcpy(dev->serial, ctrl->sn, sizeof(ctrl->sn));
	memcpy(dev->model, ctrl->mn, sizeof(ctrl->mn));
	memcpy(dev->firmware_rev, ctrl->fr, sizeof(ctrl->fr));
	if (ctrl->mdts)
		dev->max_hw_sectors = 1 << (ctrl->mdts + shift - 9);
	if ((pdev->vendor == PCI_VENDOR_ID_INTEL) &&
			(pdev->device == 0x0953) && ctrl->vs[3]) {
		unsigned int max_hw_sectors;

		dev->stripe_size = 1 << (ctrl->vs[3] + shift);
		max_hw_sectors = dev->stripe_size >> (shift - 9);
		if (dev->max_hw_sectors) {
			dev->max_hw_sectors = min(max_hw_sectors,
							dev->max_hw_sectors);
		} else
			dev->max_hw_sectors = max_hw_sectors;
	}

	dev->tagset.ops = &nvme_mq_ops;
	dev->tagset.nr_hw_queues = dev->online_queues - 1;
	dev->tagset.timeout = NVME_IO_TIMEOUT;
	dev->tagset.numa_node = dev_to_node(&dev->pci_dev->dev);
	dev->tagset.queue_depth =
				min_t(int, dev->q_depth, BLK_MQ_MAX_DEPTH) - 1;
	dev->tagset.cmd_size = nvme_cmd_size(dev);
	dev->tagset.flags = BLK_MQ_F_SHOULD_MERGE;
	dev->tagset.driver_data = dev;

	if (blk_mq_alloc_tag_set(&dev->tagset))
		goto out;

	id_ns = mem;
	for (i = 1; i <= nn; i++) {
		res = nvme_identify(dev, i, 0, dma_addr);
		if (res)
			continue;

		if (id_ns->ncap == 0)
			continue;

		res = nvme_get_features(dev, NVME_FEAT_LBA_RANGE, i,
							dma_addr + 4096, NULL);
		if (res)
			memset(mem + 4096, 0, 4096);

		ns = nvme_alloc_ns(dev, i, mem, mem + 4096);
		if (ns)
			list_add_tail(&ns->list, &dev->namespaces);
	}
	list_for_each_entry(ns, &dev->namespaces, list)
		add_disk(ns->disk);
	res = 0;

 out:
	dma_free_coherent(&dev->pci_dev->dev, 8192, mem, dma_addr);
	return res;
}

static int nvme_dev_map(struct nvme_dev *dev)
{
	u64 cap;
	int bars, result = -ENOMEM;
	struct pci_dev *pdev = dev->pci_dev;

	if (pci_enable_device_mem(pdev))
		return result;

	dev->entry[0].vector = pdev->irq;
	pci_set_master(pdev);
	bars = pci_select_bars(pdev, IORESOURCE_MEM);
	if (!bars)
		goto disable_pci;

	if (pci_request_selected_regions(pdev, bars, "nvme"))
		goto disable_pci;

	if (dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64)) &&
	    dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32)))
		goto disable;

	dev->bar = ioremap(pci_resource_start(pdev, 0), 8192);
	if (!dev->bar)
		goto disable;

	if (readl(&dev->bar->csts) == -1) {
		result = -ENODEV;
		goto unmap;
	}

	/*
	 * Some devices don't advertse INTx interrupts, pre-enable a single
	 * MSIX vec for setup. We'll adjust this later.
	 */
	if (!pdev->irq) {
		result = pci_enable_msix(pdev, dev->entry, 1);
		if (result < 0)
			goto unmap;
	}

	cap = readq(&dev->bar->cap);
	dev->q_depth = min_t(int, NVME_CAP_MQES(cap) + 1, NVME_Q_DEPTH);
	dev->db_stride = 1 << NVME_CAP_STRIDE(cap);
	dev->dbs = ((void __iomem *)dev->bar) + 4096;

	return 0;

 unmap:
	iounmap(dev->bar);
	dev->bar = NULL;
 disable:
	pci_release_regions(pdev);
 disable_pci:
	pci_disable_device(pdev);
	return result;
}

static void nvme_dev_unmap(struct nvme_dev *dev)
{
	if (dev->pci_dev->msi_enabled)
		pci_disable_msi(dev->pci_dev);
	else if (dev->pci_dev->msix_enabled)
		pci_disable_msix(dev->pci_dev);

	if (dev->bar) {
		iounmap(dev->bar);
		dev->bar = NULL;
		pci_release_regions(dev->pci_dev);
	}

	if (pci_is_enabled(dev->pci_dev))
		pci_disable_device(dev->pci_dev);
}

struct nvme_delq_ctx {
	struct task_struct *waiter;
	struct kthread_worker *worker;
	atomic_t refcount;
};

static void nvme_wait_dq(struct nvme_delq_ctx *dq, struct nvme_dev *dev)
{
	dq->waiter = current;
	mb();

	for (;;) {
		set_current_state(TASK_KILLABLE);
		if (!atomic_read(&dq->refcount))
			break;
		if (!schedule_timeout(ADMIN_TIMEOUT) ||
					fatal_signal_pending(current)) {
			/*
			 * Disable the controller first since we can't trust it
			 * at this point, but leave the admin queue enabled
			 * until all queue deletion requests are flushed.
			 * FIXME: This may take a while if there are more h/w
			 * queues than admin tags.
			 */
			set_current_state(TASK_RUNNING);
			nvme_disable_ctrl(dev, readq(&dev->bar->cap));
			nvme_clear_queue(dev->queues[0]);
			flush_kthread_worker(dq->worker);
			nvme_disable_queue(dev, 0);
			return;
		}
	}
	set_current_state(TASK_RUNNING);
}

static void nvme_put_dq(struct nvme_delq_ctx *dq)
{
	atomic_dec(&dq->refcount);
	if (dq->waiter)
		wake_up_process(dq->waiter);
}

static struct nvme_delq_ctx *nvme_get_dq(struct nvme_delq_ctx *dq)
{
	atomic_inc(&dq->refcount);
	return dq;
}

static void nvme_del_queue_end(struct nvme_queue *nvmeq)
{
	struct nvme_delq_ctx *dq = nvmeq->cmdinfo.ctx;

	nvme_clear_queue(nvmeq);
	nvme_put_dq(dq);
}

static int adapter_async_del_queue(struct nvme_queue *nvmeq, u8 opcode,
						kthread_work_func_t fn)
{
	struct nvme_command c;

	memset(&c, 0, sizeof(c));
	c.delete_queue.opcode = opcode;
	c.delete_queue.qid = cpu_to_le16(nvmeq->qid);

	init_kthread_work(&nvmeq->cmdinfo.work, fn);
	return nvme_submit_admin_async_cmd(nvmeq->dev, &c, &nvmeq->cmdinfo,
								ADMIN_TIMEOUT);
}

static void nvme_del_cq_work_handler(struct kthread_work *work)
{
	struct nvme_queue *nvmeq = container_of(work, struct nvme_queue,
							cmdinfo.work);
	nvme_del_queue_end(nvmeq);
}

static int nvme_delete_cq(struct nvme_queue *nvmeq)
{
	return adapter_async_del_queue(nvmeq, nvme_admin_delete_cq,
						nvme_del_cq_work_handler);
}

static void nvme_del_sq_work_handler(struct kthread_work *work)
{
	struct nvme_queue *nvmeq = container_of(work, struct nvme_queue,
							cmdinfo.work);
	int status = nvmeq->cmdinfo.status;

	if (!status)
		status = nvme_delete_cq(nvmeq);
	if (status)
		nvme_del_queue_end(nvmeq);
}

static int nvme_delete_sq(struct nvme_queue *nvmeq)
{
	return adapter_async_del_queue(nvmeq, nvme_admin_delete_sq,
						nvme_del_sq_work_handler);
}

static void nvme_del_queue_start(struct kthread_work *work)
{
	struct nvme_queue *nvmeq = container_of(work, struct nvme_queue,
							cmdinfo.work);
	if (nvme_delete_sq(nvmeq))
		nvme_del_queue_end(nvmeq);
}

static void nvme_disable_io_queues(struct nvme_dev *dev)
{
	int i;
	DEFINE_KTHREAD_WORKER_ONSTACK(worker);
	struct nvme_delq_ctx dq;
	struct task_struct *kworker_task = kthread_run(kthread_worker_fn,
					&worker, "nvme%d", dev->instance);

	if (IS_ERR(kworker_task)) {
		dev_err(&dev->pci_dev->dev,
			"Failed to create queue del task\n");
		for (i = dev->queue_count - 1; i > 0; i--)
			nvme_disable_queue(dev, i);
		return;
	}

	dq.waiter = NULL;
	atomic_set(&dq.refcount, 0);
	dq.worker = &worker;
	for (i = dev->queue_count - 1; i > 0; i--) {
		struct nvme_queue *nvmeq = dev->queues[i];

		if (nvme_suspend_queue(nvmeq))
			continue;
		nvmeq->cmdinfo.ctx = nvme_get_dq(&dq);
		nvmeq->cmdinfo.worker = dq.worker;
		init_kthread_work(&nvmeq->cmdinfo.work, nvme_del_queue_start);
		queue_kthread_work(dq.worker, &nvmeq->cmdinfo.work);
	}
	nvme_wait_dq(&dq, dev);
	kthread_stop(kworker_task);
}

/*
* Remove the node from the device list and check
* for whether or not we need to stop the nvme_thread.
*/
static void nvme_dev_list_remove(struct nvme_dev *dev)
{
	struct task_struct *tmp = NULL;

	spin_lock(&dev_list_lock);
	list_del_init(&dev->node);
	if (list_empty(&dev_list) && !IS_ERR_OR_NULL(nvme_thread)) {
		tmp = nvme_thread;
		nvme_thread = NULL;
	}
	spin_unlock(&dev_list_lock);

	if (tmp)
		kthread_stop(tmp);
}

static void nvme_freeze_queues(struct nvme_dev *dev)
{
	struct nvme_ns *ns;

	list_for_each_entry(ns, &dev->namespaces, list) {
		blk_mq_freeze_queue_start(ns->queue);

		spin_lock(ns->queue->queue_lock);
		queue_flag_set(QUEUE_FLAG_STOPPED, ns->queue);
		spin_unlock(ns->queue->queue_lock);

		blk_mq_cancel_requeue_work(ns->queue);
		blk_mq_stop_hw_queues(ns->queue);
	}
}

static void nvme_unfreeze_queues(struct nvme_dev *dev)
{
	struct nvme_ns *ns;

	list_for_each_entry(ns, &dev->namespaces, list) {
		queue_flag_clear_unlocked(QUEUE_FLAG_STOPPED, ns->queue);
		blk_mq_unfreeze_queue(ns->queue);
		blk_mq_start_stopped_hw_queues(ns->queue, true);
		blk_mq_kick_requeue_list(ns->queue);
	}
}

static void nvme_dev_shutdown(struct nvme_dev *dev)
{
	int i;
	u32 csts = -1;

	dev->initialized = 0;
	nvme_dev_list_remove(dev);

	if (dev->bar) {
		nvme_freeze_queues(dev);
		csts = readl(&dev->bar->csts);
	}
	if (csts & NVME_CSTS_CFS || !(csts & NVME_CSTS_RDY)) {
		for (i = dev->queue_count - 1; i >= 0; i--) {
			struct nvme_queue *nvmeq = dev->queues[i];
			nvme_suspend_queue(nvmeq);
			nvme_clear_queue(nvmeq);
		}
	} else {
		nvme_disable_io_queues(dev);
		nvme_shutdown_ctrl(dev);
		nvme_disable_queue(dev, 0);
	}
	nvme_dev_unmap(dev);
}

static void nvme_dev_remove(struct nvme_dev *dev)
{
	struct nvme_ns *ns;

	list_for_each_entry(ns, &dev->namespaces, list) {
		if (ns->disk->flags & GENHD_FL_UP)
			del_gendisk(ns->disk);
		if (!blk_queue_dying(ns->queue)) {
			blk_mq_abort_requeue_list(ns->queue);
			blk_cleanup_queue(ns->queue);
		}
	}
}

static int nvme_setup_prp_pools(struct nvme_dev *dev)
{
	struct device *dmadev = &dev->pci_dev->dev;
	dev->prp_page_pool = dma_pool_create("prp list page", dmadev,
						PAGE_SIZE, PAGE_SIZE, 0);
	if (!dev->prp_page_pool)
		return -ENOMEM;

	/* Optimisation for I/Os between 4k and 128k */
	dev->prp_small_pool = dma_pool_create("prp list 256", dmadev,
						256, 256, 0);
	if (!dev->prp_small_pool) {
		dma_pool_destroy(dev->prp_page_pool);
		return -ENOMEM;
	}
	return 0;
}

static void nvme_release_prp_pools(struct nvme_dev *dev)
{
	dma_pool_destroy(dev->prp_page_pool);
	dma_pool_destroy(dev->prp_small_pool);
}

static DEFINE_IDA(nvme_instance_ida);

static int nvme_set_instance(struct nvme_dev *dev)
{
	int instance, error;

	do {
		if (!ida_pre_get(&nvme_instance_ida, GFP_KERNEL))
			return -ENODEV;

		spin_lock(&dev_list_lock);
		error = ida_get_new(&nvme_instance_ida, &instance);
		spin_unlock(&dev_list_lock);
	} while (error == -EAGAIN);

	if (error)
		return -ENODEV;

	dev->instance = instance;
	return 0;
}

static void nvme_release_instance(struct nvme_dev *dev)
{
	spin_lock(&dev_list_lock);
	ida_remove(&nvme_instance_ida, dev->instance);
	spin_unlock(&dev_list_lock);
}

static void nvme_free_namespaces(struct nvme_dev *dev)
{
	struct nvme_ns *ns, *next;

	list_for_each_entry_safe(ns, next, &dev->namespaces, list) {
		list_del(&ns->list);

		spin_lock(&dev_list_lock);
		ns->disk->private_data = NULL;
		spin_unlock(&dev_list_lock);

		put_disk(ns->disk);
		kfree(ns);
	}
}

static void nvme_free_dev(struct kref *kref)
{
	struct nvme_dev *dev = container_of(kref, struct nvme_dev, kref);

	pci_dev_put(dev->pci_dev);
	nvme_free_namespaces(dev);
	nvme_release_instance(dev);
	blk_mq_free_tag_set(&dev->tagset);
	blk_put_queue(dev->admin_q);
	kfree(dev->queues);
	kfree(dev->entry);
	kfree(dev);
}

static int nvme_dev_open(struct inode *inode, struct file *f)
{
	struct nvme_dev *dev = container_of(f->private_data, struct nvme_dev,
								miscdev);
	kref_get(&dev->kref);
	f->private_data = dev;
	return 0;
}

static int nvme_dev_release(struct inode *inode, struct file *f)
{
	struct nvme_dev *dev = f->private_data;
	kref_put(&dev->kref, nvme_free_dev);
	return 0;
}

static long nvme_dev_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	struct nvme_dev *dev = f->private_data;
	struct nvme_ns *ns;

	switch (cmd) {
	case NVME_IOCTL_ADMIN_CMD:
		return nvme_user_cmd(dev, NULL, (void __user *)arg);
	case NVME_IOCTL_IO_CMD:
		if (list_empty(&dev->namespaces))
			return -ENOTTY;
		ns = list_first_entry(&dev->namespaces, struct nvme_ns, list);
		return nvme_user_cmd(dev, ns, (void __user *)arg);
	default:
		return -ENOTTY;
	}
}

static const struct file_operations nvme_dev_fops = {
	.owner		= THIS_MODULE,
	.open		= nvme_dev_open,
	.release	= nvme_dev_release,
	.unlocked_ioctl	= nvme_dev_ioctl,
	.compat_ioctl	= nvme_dev_ioctl,
};

static void nvme_set_irq_hints(struct nvme_dev *dev)
{
	struct nvme_queue *nvmeq;
	int i;

	for (i = 0; i < dev->online_queues; i++) {
		nvmeq = dev->queues[i];

		if (!nvmeq->hctx)
			continue;

		irq_set_affinity_hint(dev->entry[nvmeq->cq_vector].vector,
							nvmeq->hctx->cpumask);
	}
}

static int nvme_dev_start(struct nvme_dev *dev)
{
	int result;
	bool start_thread = false;

	result = nvme_dev_map(dev);
	if (result)
		return result;

	result = nvme_configure_admin_queue(dev);
	if (result)
		goto unmap;

	spin_lock(&dev_list_lock);
	if (list_empty(&dev_list) && IS_ERR_OR_NULL(nvme_thread)) {
		start_thread = true;
		nvme_thread = NULL;
	}
	list_add(&dev->node, &dev_list);
	spin_unlock(&dev_list_lock);

	if (start_thread) {
		nvme_thread = kthread_run(nvme_kthread, NULL, "nvme");
		wake_up_all(&nvme_kthread_wait);
	} else
		wait_event_killable(nvme_kthread_wait, nvme_thread);

	if (IS_ERR_OR_NULL(nvme_thread)) {
		result = nvme_thread ? PTR_ERR(nvme_thread) : -EINTR;
		goto disable;
	}

	nvme_init_queue(dev->queues[0], 0);
	result = nvme_alloc_admin_tags(dev);
	if (result)
		goto disable;

	result = nvme_setup_io_queues(dev);
	if (result)
		goto free_tags;

	nvme_set_irq_hints(dev);

	return result;

 free_tags:
	nvme_dev_remove_admin(dev);
 disable:
	nvme_disable_queue(dev, 0);
	nvme_dev_list_remove(dev);
 unmap:
	nvme_dev_unmap(dev);
	return result;
}

static int nvme_remove_dead_ctrl(void *arg)
{
	struct nvme_dev *dev = (struct nvme_dev *)arg;
	struct pci_dev *pdev = dev->pci_dev;

	if (pci_get_drvdata(pdev))
		pci_stop_and_remove_bus_device_locked(pdev);
	kref_put(&dev->kref, nvme_free_dev);
	return 0;
}

static void nvme_remove_disks(struct work_struct *ws)
{
	struct nvme_dev *dev = container_of(ws, struct nvme_dev, reset_work);

	nvme_free_queues(dev, 1);
	nvme_dev_remove(dev);
}

static int nvme_dev_resume(struct nvme_dev *dev)
{
	int ret;

	ret = nvme_dev_start(dev);
	if (ret)
		return ret;
	if (dev->online_queues < 2) {
		spin_lock(&dev_list_lock);
		dev->reset_workfn = nvme_remove_disks;
		queue_work(nvme_workq, &dev->reset_work);
		spin_unlock(&dev_list_lock);
	} else {
		nvme_unfreeze_queues(dev);
		nvme_set_irq_hints(dev);
	}
	dev->initialized = 1;
	return 0;
}

static void nvme_dev_reset(struct nvme_dev *dev)
{
	nvme_dev_shutdown(dev);
	if (nvme_dev_resume(dev)) {
		dev_warn(&dev->pci_dev->dev, "Device failed to resume\n");
		kref_get(&dev->kref);
		if (IS_ERR(kthread_run(nvme_remove_dead_ctrl, dev, "nvme%d",
							dev->instance))) {
			dev_err(&dev->pci_dev->dev,
				"Failed to start controller remove task\n");
			kref_put(&dev->kref, nvme_free_dev);
		}
	}
}

static void nvme_reset_failed_dev(struct work_struct *ws)
{
	struct nvme_dev *dev = container_of(ws, struct nvme_dev, reset_work);
	nvme_dev_reset(dev);
}

static void nvme_reset_workfn(struct work_struct *work)
{
	struct nvme_dev *dev = container_of(work, struct nvme_dev, reset_work);
	dev->reset_workfn(work);
}

static int nvme_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	int node, result = -ENOMEM;
	struct nvme_dev *dev;

	node = dev_to_node(&pdev->dev);
	if (node == NUMA_NO_NODE)
		set_dev_node(&pdev->dev, 0);

	dev = kzalloc_node(sizeof(*dev), GFP_KERNEL, node);
	if (!dev)
		return -ENOMEM;
	dev->entry = kzalloc_node(num_possible_cpus() * sizeof(*dev->entry),
							GFP_KERNEL, node);
	if (!dev->entry)
		goto free;
	dev->queues = kzalloc_node((num_possible_cpus() + 1) * sizeof(void *),
							GFP_KERNEL, node);
	if (!dev->queues)
		goto free;

	INIT_LIST_HEAD(&dev->namespaces);
	dev->reset_workfn = nvme_reset_failed_dev;
	INIT_WORK(&dev->reset_work, nvme_reset_workfn);
	dev->pci_dev = pci_dev_get(pdev);
	pci_set_drvdata(pdev, dev);
	result = nvme_set_instance(dev);
	if (result)
		goto put_pci;

	result = nvme_setup_prp_pools(dev);
	if (result)
		goto release;

	kref_init(&dev->kref);
	result = nvme_dev_start(dev);
	if (result)
		goto release_pools;

	if (dev->online_queues > 1)
		result = nvme_dev_add(dev);
	if (result)
		goto shutdown;

	scnprintf(dev->name, sizeof(dev->name), "nvme%d", dev->instance);
	dev->miscdev.minor = MISC_DYNAMIC_MINOR;
	dev->miscdev.parent = &pdev->dev;
	dev->miscdev.name = dev->name;
	dev->miscdev.fops = &nvme_dev_fops;
	result = misc_register(&dev->miscdev);
	if (result)
		goto remove;

	nvme_set_irq_hints(dev);

	dev->initialized = 1;
	return 0;

 remove:
	nvme_dev_remove(dev);
	nvme_dev_remove_admin(dev);
	nvme_free_namespaces(dev);
 shutdown:
	nvme_dev_shutdown(dev);
 release_pools:
	nvme_free_queues(dev, 0);
	nvme_release_prp_pools(dev);
 release:
	nvme_release_instance(dev);
 put_pci:
	pci_dev_put(dev->pci_dev);
 free:
	kfree(dev->queues);
	kfree(dev->entry);
	kfree(dev);
	return result;
}

static void nvme_reset_notify(struct pci_dev *pdev, bool prepare)
{
	struct nvme_dev *dev = pci_get_drvdata(pdev);

	if (prepare)
		nvme_dev_shutdown(dev);
	else
		nvme_dev_resume(dev);
}

static void nvme_shutdown(struct pci_dev *pdev)
{
	struct nvme_dev *dev = pci_get_drvdata(pdev);
	nvme_dev_shutdown(dev);
}

static void nvme_remove(struct pci_dev *pdev)
{
	struct nvme_dev *dev = pci_get_drvdata(pdev);

	spin_lock(&dev_list_lock);
	list_del_init(&dev->node);
	spin_unlock(&dev_list_lock);

	pci_set_drvdata(pdev, NULL);
	flush_work(&dev->reset_work);
	misc_deregister(&dev->miscdev);
	nvme_dev_shutdown(dev);
	nvme_dev_remove(dev);
	nvme_dev_remove_admin(dev);
	nvme_free_queues(dev, 0);
	nvme_release_prp_pools(dev);
	kref_put(&dev->kref, nvme_free_dev);
}

/* These functions are yet to be implemented */
#define nvme_error_detected NULL
#define nvme_dump_registers NULL
#define nvme_link_reset NULL
#define nvme_slot_reset NULL
#define nvme_error_resume NULL

#ifdef CONFIG_PM_SLEEP
static int nvme_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct nvme_dev *ndev = pci_get_drvdata(pdev);

	nvme_dev_shutdown(ndev);
	return 0;
}

static int nvme_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct nvme_dev *ndev = pci_get_drvdata(pdev);

	if (nvme_dev_resume(ndev) && !work_busy(&ndev->reset_work)) {
		ndev->reset_workfn = nvme_reset_failed_dev;
		queue_work(nvme_workq, &ndev->reset_work);
	}
	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(nvme_dev_pm_ops, nvme_suspend, nvme_resume);

static const struct pci_error_handlers nvme_err_handler = {
	.error_detected	= nvme_error_detected,
	.mmio_enabled	= nvme_dump_registers,
	.link_reset	= nvme_link_reset,
	.slot_reset	= nvme_slot_reset,
	.resume		= nvme_error_resume,
	.reset_notify	= nvme_reset_notify,
};

/* Move to pci_ids.h later */
#define PCI_CLASS_STORAGE_EXPRESS	0x010802

static const struct pci_device_id nvme_id_table[] = {
	{ PCI_DEVICE_CLASS(PCI_CLASS_STORAGE_EXPRESS, 0xffffff) },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, nvme_id_table);

static struct pci_driver nvme_driver = {
	.name		= "nvme",
	.id_table	= nvme_id_table,
	.probe		= nvme_probe,
	.remove		= nvme_remove,
	.shutdown	= nvme_shutdown,
	.driver		= {
		.pm	= &nvme_dev_pm_ops,
	},
	.err_handler	= &nvme_err_handler,
};

static int __init nvme_init(void)
{
	int result;

	init_waitqueue_head(&nvme_kthread_wait);

	nvme_workq = create_singlethread_workqueue("nvme");
	if (!nvme_workq)
		return -ENOMEM;

	result = register_blkdev(nvme_major, "nvme");
	if (result < 0)
		goto kill_workq;
	else if (result > 0)
		nvme_major = result;

	result = pci_register_driver(&nvme_driver);
	if (result)
		goto unregister_blkdev;
	return 0;

 unregister_blkdev:
	unregister_blkdev(nvme_major, "nvme");
 kill_workq:
	destroy_workqueue(nvme_workq);
	return result;
}

static void __exit nvme_exit(void)
{
	pci_unregister_driver(&nvme_driver);
	unregister_hotcpu_notifier(&nvme_nb);
	unregister_blkdev(nvme_major, "nvme");
	destroy_workqueue(nvme_workq);
	BUG_ON(nvme_thread && !IS_ERR(nvme_thread));
	_nvme_check_size();
}

MODULE_AUTHOR("Matthew Wilcox <willy@linux.intel.com>");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0");
module_init(nvme_init);
module_exit(nvme_exit);
