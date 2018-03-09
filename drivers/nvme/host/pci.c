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
#include <linux/list_sort.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/pci.h>
#include <linux/poison.h>
#include <linux/ptrace.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/t10-pi.h>
#include <linux/types.h>
#include <linux/pr.h>
#include <scsi/sg.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <asm/unaligned.h>

#include <uapi/linux/nvme_ioctl.h>
#include "nvme.h"

#define NVME_MINORS		(1U << MINORBITS)
#define NVME_Q_DEPTH		1024
#define NVME_AQ_DEPTH		256
#define SQ_SIZE(depth)		(depth * sizeof(struct nvme_command))
#define CQ_SIZE(depth)		(depth * sizeof(struct nvme_completion))
#define ADMIN_TIMEOUT		(admin_timeout * HZ)
#define SHUTDOWN_TIMEOUT	(shutdown_timeout * HZ)

static unsigned char admin_timeout = 60;
module_param(admin_timeout, byte, 0644);
MODULE_PARM_DESC(admin_timeout, "timeout in seconds for admin commands");

unsigned char nvme_io_timeout = 30;
module_param_named(io_timeout, nvme_io_timeout, byte, 0644);
MODULE_PARM_DESC(io_timeout, "timeout in seconds for I/O");

static unsigned char shutdown_timeout = 5;
module_param(shutdown_timeout, byte, 0644);
MODULE_PARM_DESC(shutdown_timeout, "timeout in seconds for controller shutdown");

static int nvme_major;
module_param(nvme_major, int, 0);

static int nvme_char_major;
module_param(nvme_char_major, int, 0);

static int use_threaded_interrupts;
module_param(use_threaded_interrupts, int, 0);

static bool use_cmb_sqes = true;
module_param(use_cmb_sqes, bool, 0644);
MODULE_PARM_DESC(use_cmb_sqes, "use controller's memory buffer for I/O SQes");

static DEFINE_SPINLOCK(dev_list_lock);
static LIST_HEAD(dev_list);
static struct task_struct *nvme_thread;
static struct workqueue_struct *nvme_workq;
static wait_queue_head_t nvme_kthread_wait;

static struct class *nvme_class;

static int __nvme_reset(struct nvme_dev *dev);
static int nvme_reset(struct nvme_dev *dev);
static void nvme_process_cq(struct nvme_queue *nvmeq);
static void nvme_dead_ctrl(struct nvme_dev *dev);

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
	struct device *q_dmadev;
	struct nvme_dev *dev;
	char irqname[24];	/* nvme4294967295-65535\0 */
	spinlock_t q_lock;
	struct nvme_command *sq_cmds;
	struct nvme_command __iomem *sq_cmds_io;
	volatile struct nvme_completion *cqes;
	struct blk_mq_tags **tags;
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
#define NVME_INT_MASK		0x01

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

	WARN_ON(hctx_idx != 0);
	WARN_ON(dev->admin_tagset.tags[0] != hctx->tags);
	WARN_ON(nvmeq->tags);

	hctx->driver_data = nvmeq;
	nvmeq->tags = &dev->admin_tagset.tags[0];
	return 0;
}

static void nvme_admin_exit_hctx(struct blk_mq_hw_ctx *hctx, unsigned int hctx_idx)
{
	struct nvme_queue *nvmeq = hctx->driver_data;

	nvmeq->tags = NULL;
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

static int nvme_init_hctx(struct blk_mq_hw_ctx *hctx, void *data,
			  unsigned int hctx_idx)
{
	struct nvme_dev *dev = data;
	struct nvme_queue *nvmeq = dev->queues[hctx_idx + 1];

	if (!nvmeq->tags)
		nvmeq->tags = &dev->tagset.tags[hctx_idx];

	WARN_ON(dev->tagset.tags[hctx_idx] != hctx->tags);
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
	return (iod->private & NVME_INT_MASK) == 0;
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
	u32 result = le32_to_cpup(&cqe->result);
	u16 status = le16_to_cpup(&cqe->status) >> 1;

	if (status == NVME_SC_SUCCESS || status == NVME_SC_ABORT_REQ)
		++nvmeq->dev->event_limit;
	if (status != NVME_SC_SUCCESS)
		return;

	switch (result & 0xff07) {
	case NVME_AER_NOTICE_NS_CHANGED:
		dev_info(nvmeq->q_dmadev, "rescanning\n");
		schedule_work(&nvmeq->dev->scan_work);
	default:
		dev_warn(nvmeq->q_dmadev, "async event result %08x\n", result);
	}
}

static void abort_completion(struct nvme_queue *nvmeq, void *ctx,
						struct nvme_completion *cqe)
{
	struct request *req = ctx;

	u16 status = le16_to_cpup(&cqe->status) >> 1;
	u32 result = le32_to_cpup(&cqe->result);

	blk_mq_free_request(req);

	dev_warn(nvmeq->q_dmadev, "Abort status:%x result:%x", status, result);
	++nvmeq->dev->abort_limit;
}

static void async_completion(struct nvme_queue *nvmeq, void *ctx,
						struct nvme_completion *cqe)
{
	struct async_cmd_info *cmdinfo = ctx;
	cmdinfo->result = le32_to_cpup(&cqe->result);
	cmdinfo->status = le16_to_cpup(&cqe->status) >> 1;
	blk_mq_free_request(cmdinfo->req);
	queue_kthread_work(cmdinfo->worker, &cmdinfo->work);
}

static inline struct nvme_cmd_info *get_cmd_from_tag(struct nvme_queue *nvmeq,
				  unsigned int tag)
{
	struct request *req = blk_mq_tag_to_rq(*nvmeq->tags, tag);

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
static void __nvme_submit_cmd(struct nvme_queue *nvmeq,
						struct nvme_command *cmd)
{
	u16 tail = nvmeq->sq_tail;

	if (nvmeq->sq_cmds_io)
		memcpy_toio(&nvmeq->sq_cmds_io[tail], cmd, sizeof(*cmd));
	else
		memcpy(&nvmeq->sq_cmds[tail], cmd, sizeof(*cmd));

	if (++tail == nvmeq->q_depth)
		tail = 0;
	writel(tail, nvmeq->q_db);
	nvmeq->sq_tail = tail;
}

static void nvme_submit_cmd(struct nvme_queue *nvmeq, struct nvme_command *cmd)
{
	unsigned long flags;
	spin_lock_irqsave(&nvmeq->q_lock, flags);
	__nvme_submit_cmd(nvmeq, cmd);
	spin_unlock_irqrestore(&nvmeq->q_lock, flags);
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
	struct nvme_iod *iod;

	if (rq->nr_phys_segments <= NVME_INT_PAGES &&
	    size <= NVME_INT_BYTES(dev)) {
		struct nvme_cmd_info *cmd = blk_mq_rq_to_pdu(rq);

		iod = cmd->iod;
		iod_init(iod, size, rq->nr_phys_segments,
				(unsigned long) rq | NVME_INT_MASK);
		return iod;
	}

	return __nvme_alloc_iod(rq->nr_phys_segments, size, dev,
				(unsigned long) rq, gfp);
}

static void nvme_free_iod(struct nvme_dev *dev, struct nvme_iod *iod)
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

#ifdef CONFIG_BLK_DEV_INTEGRITY
static void nvme_dif_prep(u32 p, u32 v, struct t10_pi_tuple *pi)
{
	if (be32_to_cpu(pi->ref_tag) == v)
		pi->ref_tag = cpu_to_be32(p);
}

static void nvme_dif_complete(u32 p, u32 v, struct t10_pi_tuple *pi)
{
	if (be32_to_cpu(pi->ref_tag) == p)
		pi->ref_tag = cpu_to_be32(v);
}

/**
 * nvme_dif_remap - remaps ref tags to bip seed and physical lba
 *
 * The virtual start sector is the one that was originally submitted by the
 * block layer.	Due to partitioning, MD/DM cloning, etc. the actual physical
 * start sector may be different. Remap protection information to match the
 * physical LBA on writes, and back to the original seed on reads.
 *
 * Type 0 and 3 do not have a ref tag, so no remapping required.
 */
static void nvme_dif_remap(struct request *req,
			void (*dif_swap)(u32 p, u32 v, struct t10_pi_tuple *pi))
{
	struct nvme_ns *ns = req->rq_disk->private_data;
	struct bio_integrity_payload *bip;
	struct t10_pi_tuple *pi;
	void *p, *pmap;
	u32 i, nlb, ts, phys, virt;

	if (!ns->pi_type || ns->pi_type == NVME_NS_DPS_PI_TYPE3)
		return;

	bip = bio_integrity(req->bio);
	if (!bip)
		return;

	pmap = kmap_atomic(bip->bip_vec->bv_page) + bip->bip_vec->bv_offset;

	p = pmap;
	virt = bip_get_seed(bip);
	phys = nvme_block_nr(ns, blk_rq_pos(req));
	nlb = (blk_rq_bytes(req) >> ns->lba_shift);
	ts = ns->disk->queue->integrity.tuple_size;

	for (i = 0; i < nlb; i++, virt++, phys++) {
		pi = (struct t10_pi_tuple *)p;
		dif_swap(phys, virt, pi);
		p += ts;
	}
	kunmap_atomic(pmap);
}

static void nvme_init_integrity(struct nvme_ns *ns)
{
	struct blk_integrity integrity;

	switch (ns->pi_type) {
	case NVME_NS_DPS_PI_TYPE3:
		integrity.profile = &t10_pi_type3_crc;
		break;
	case NVME_NS_DPS_PI_TYPE1:
	case NVME_NS_DPS_PI_TYPE2:
		integrity.profile = &t10_pi_type1_crc;
		break;
	default:
		integrity.profile = NULL;
		break;
	}
	integrity.tuple_size = ns->ms;
	blk_integrity_register(ns->disk, &integrity);
	blk_queue_max_integrity_segments(ns->queue, 1);
}
#else /* CONFIG_BLK_DEV_INTEGRITY */
static void nvme_dif_remap(struct request *req,
			void (*dif_swap)(u32 p, u32 v, struct t10_pi_tuple *pi))
{
}
static void nvme_dif_prep(u32 p, u32 v, struct t10_pi_tuple *pi)
{
}
static void nvme_dif_complete(u32 p, u32 v, struct t10_pi_tuple *pi)
{
}
static void nvme_init_integrity(struct nvme_ns *ns)
{
}
#endif

static void req_completion(struct nvme_queue *nvmeq, void *ctx,
						struct nvme_completion *cqe)
{
	struct nvme_iod *iod = ctx;
	struct request *req = iod_get_private(iod);
	struct nvme_cmd_info *cmd_rq = blk_mq_rq_to_pdu(req);
	u16 status = le16_to_cpup(&cqe->status) >> 1;
	bool requeue = false;
	int error = 0;

	if (unlikely(status)) {
		if (!(status & NVME_SC_DNR || blk_noretry_request(req))
		    && (jiffies - req->start_time) < req->timeout) {
			unsigned long flags;

			requeue = true;
			blk_mq_requeue_request(req);
			spin_lock_irqsave(req->q->queue_lock, flags);
			if (!blk_queue_stopped(req->q))
				blk_mq_kick_requeue_list(req->q);
			spin_unlock_irqrestore(req->q->queue_lock, flags);
			goto release_iod;
		}

		if (req->cmd_type == REQ_TYPE_DRV_PRIV) {
			if (cmd_rq->ctx == CMD_CTX_CANCELLED)
				error = -EINTR;
			else
				error = status;
		} else {
			error = nvme_error_status(status);
		}
	}

	if (req->cmd_type == REQ_TYPE_DRV_PRIV) {
		u32 result = le32_to_cpup(&cqe->result);
		req->special = (void *)(uintptr_t)result;
	}

	if (cmd_rq->aborted)
		dev_warn(nvmeq->dev->dev,
			"completing aborted command with status:%04x\n",
			error);

release_iod:
	if (iod->nents) {
		dma_unmap_sg(nvmeq->dev->dev, iod->sg, iod->nents,
			rq_data_dir(req) ? DMA_TO_DEVICE : DMA_FROM_DEVICE);
		if (blk_integrity_rq(req)) {
			if (!rq_data_dir(req))
				nvme_dif_remap(req, nvme_dif_complete);
			dma_unmap_sg(nvmeq->dev->dev, iod->meta_sg, 1,
				rq_data_dir(req) ? DMA_TO_DEVICE : DMA_FROM_DEVICE);
		}
	}
	nvme_free_iod(nvmeq->dev, iod);

	if (likely(!requeue))
		blk_mq_complete_request(req, error);
}

/* length is in bytes.  gfp flags indicates whether we may sleep. */
static int nvme_setup_prps(struct nvme_dev *dev, struct nvme_iod *iod,
		int total_len, gfp_t gfp)
{
	struct dma_pool *pool;
	int length = total_len;
	struct scatterlist *sg = iod->sg;
	int dma_len = sg_dma_len(sg);
	u64 dma_addr = sg_dma_address(sg);
	u32 page_size = dev->page_size;
	int offset = dma_addr & (page_size - 1);
	__le64 *prp_list;
	__le64 **list = iod_list(iod);
	dma_addr_t prp_dma;
	int nprps, i;

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

static void nvme_submit_priv(struct nvme_queue *nvmeq, struct request *req,
		struct nvme_iod *iod)
{
	struct nvme_command cmnd;

	memcpy(&cmnd, req->cmd, sizeof(cmnd));
	cmnd.rw.command_id = req->tag;
	if (req->nr_phys_segments) {
		cmnd.rw.prp1 = cpu_to_le64(sg_dma_address(iod->sg));
		cmnd.rw.prp2 = cpu_to_le64(iod->first_dma);
	}

	__nvme_submit_cmd(nvmeq, &cmnd);
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
	struct nvme_command cmnd;

	range->cattr = cpu_to_le32(0);
	range->nlb = cpu_to_le32(blk_rq_bytes(req) >> ns->lba_shift);
	range->slba = cpu_to_le64(nvme_block_nr(ns, blk_rq_pos(req)));

	memset(&cmnd, 0, sizeof(cmnd));
	cmnd.dsm.opcode = nvme_cmd_dsm;
	cmnd.dsm.command_id = req->tag;
	cmnd.dsm.nsid = cpu_to_le32(ns->ns_id);
	cmnd.dsm.prp1 = cpu_to_le64(iod->first_dma);
	cmnd.dsm.nr = 0;
	cmnd.dsm.attributes = cpu_to_le32(NVME_DSMGMT_AD);

	__nvme_submit_cmd(nvmeq, &cmnd);
}

static void nvme_submit_flush(struct nvme_queue *nvmeq, struct nvme_ns *ns,
								int cmdid)
{
	struct nvme_command cmnd;

	memset(&cmnd, 0, sizeof(cmnd));
	cmnd.common.opcode = nvme_cmd_flush;
	cmnd.common.command_id = cmdid;
	cmnd.common.nsid = cpu_to_le32(ns->ns_id);

	__nvme_submit_cmd(nvmeq, &cmnd);
}

static int nvme_submit_iod(struct nvme_queue *nvmeq, struct nvme_iod *iod,
							struct nvme_ns *ns)
{
	struct request *req = iod_get_private(iod);
	struct nvme_command cmnd;
	u16 control = 0;
	u32 dsmgmt = 0;

	if (req->cmd_flags & REQ_FUA)
		control |= NVME_RW_FUA;
	if (req->cmd_flags & (REQ_FAILFAST_DEV | REQ_RAHEAD))
		control |= NVME_RW_LR;

	if (req->cmd_flags & REQ_RAHEAD)
		dsmgmt |= NVME_RW_DSM_FREQ_PREFETCH;

	memset(&cmnd, 0, sizeof(cmnd));
	cmnd.rw.opcode = (rq_data_dir(req) ? nvme_cmd_write : nvme_cmd_read);
	cmnd.rw.command_id = req->tag;
	cmnd.rw.nsid = cpu_to_le32(ns->ns_id);
	cmnd.rw.prp1 = cpu_to_le64(sg_dma_address(iod->sg));
	cmnd.rw.prp2 = cpu_to_le64(iod->first_dma);
	cmnd.rw.slba = cpu_to_le64(nvme_block_nr(ns, blk_rq_pos(req)));
	cmnd.rw.length = cpu_to_le16((blk_rq_bytes(req) >> ns->lba_shift) - 1);

	if (ns->ms) {
		switch (ns->pi_type) {
		case NVME_NS_DPS_PI_TYPE3:
			control |= NVME_RW_PRINFO_PRCHK_GUARD;
			break;
		case NVME_NS_DPS_PI_TYPE1:
		case NVME_NS_DPS_PI_TYPE2:
			control |= NVME_RW_PRINFO_PRCHK_GUARD |
					NVME_RW_PRINFO_PRCHK_REF;
			cmnd.rw.reftag = cpu_to_le32(
					nvme_block_nr(ns, blk_rq_pos(req)));
			break;
		}
		if (blk_integrity_rq(req))
			cmnd.rw.metadata =
				cpu_to_le64(sg_dma_address(iod->meta_sg));
		else
			control |= NVME_RW_PRINFO_PRACT;
	}

	cmnd.rw.control = cpu_to_le16(control);
	cmnd.rw.dsmgmt = cpu_to_le32(dsmgmt);

	__nvme_submit_cmd(nvmeq, &cmnd);

	return 0;
}

/*
 * NOTE: ns is NULL when called on the admin queue.
 */
static int nvme_queue_rq(struct blk_mq_hw_ctx *hctx,
			 const struct blk_mq_queue_data *bd)
{
	struct nvme_ns *ns = hctx->queue->queuedata;
	struct nvme_queue *nvmeq = hctx->driver_data;
	struct nvme_dev *dev = nvmeq->dev;
	struct request *req = bd->rq;
	struct nvme_cmd_info *cmd = blk_mq_rq_to_pdu(req);
	struct nvme_iod *iod;
	enum dma_data_direction dma_dir;

	/*
	 * If formated with metadata, require the block layer provide a buffer
	 * unless this namespace is formated such that the metadata can be
	 * stripped/generated by the controller with PRACT=1.
	 */
	if (ns && ns->ms && !blk_integrity_rq(req)) {
		if (!(ns->pi_type && ns->ms == 8) &&
					req->cmd_type != REQ_TYPE_DRV_PRIV) {
			blk_mq_complete_request(req, -EFAULT);
			return BLK_MQ_RQ_QUEUE_OK;
		}
	}

	iod = nvme_alloc_iod(req, dev, GFP_ATOMIC);
	if (!iod)
		return BLK_MQ_RQ_QUEUE_BUSY;

	if (req->cmd_flags & REQ_DISCARD) {
		void *range;
		/*
		 * We reuse the small pool to allocate the 16-byte range here
		 * as it is not worth having a special pool for these or
		 * additional cases to handle freeing the iod.
		 */
		range = dma_pool_alloc(dev->prp_small_pool, GFP_ATOMIC,
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
                    nvme_setup_prps(dev, iod, blk_rq_bytes(req), GFP_ATOMIC)) {
			dma_unmap_sg(dev->dev, iod->sg, iod->nents, dma_dir);
			goto retry_cmd;
		}
		if (blk_integrity_rq(req)) {
			if (blk_rq_count_integrity_sg(req->q, req->bio) != 1) {
				dma_unmap_sg(dev->dev, iod->sg, iod->nents,
						dma_dir);
				goto error_cmd;
			}

			sg_init_table(iod->meta_sg, 1);
			if (blk_rq_map_integrity_sg(
					req->q, req->bio, iod->meta_sg) != 1) {
				dma_unmap_sg(dev->dev, iod->sg, iod->nents,
						dma_dir);
				goto error_cmd;
			}

			if (rq_data_dir(req))
				nvme_dif_remap(req, nvme_dif_prep);

			if (!dma_map_sg(nvmeq->q_dmadev, iod->meta_sg, 1, dma_dir)) {
				dma_unmap_sg(dev->dev, iod->sg, iod->nents,
						dma_dir);
				goto error_cmd;
			}
		}
	}

	nvme_set_info(cmd, iod, req_completion);
	spin_lock_irq(&nvmeq->q_lock);
	if (req->cmd_type == REQ_TYPE_DRV_PRIV)
		nvme_submit_priv(nvmeq, req, iod);
	else if (req->cmd_flags & REQ_DISCARD)
		nvme_submit_discard(nvmeq, ns, req, iod);
	else if (req->cmd_flags & REQ_FLUSH)
		nvme_submit_flush(nvmeq, ns, req->tag);
	else
		nvme_submit_iod(nvmeq, iod, ns);

	nvme_process_cq(nvmeq);
	spin_unlock_irq(&nvmeq->q_lock);
	return BLK_MQ_RQ_QUEUE_OK;

 error_cmd:
	nvme_free_iod(dev, iod);
	return BLK_MQ_RQ_QUEUE_ERROR;
 retry_cmd:
	nvme_free_iod(dev, iod);
	return BLK_MQ_RQ_QUEUE_BUSY;
}

static void __nvme_process_cq(struct nvme_queue *nvmeq, unsigned int *tag)
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
		if (tag && *tag == cqe.command_id)
			*tag = -1;
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
		return;

	if (likely(nvmeq->cq_vector >= 0))
		writel(head, nvmeq->q_db + nvmeq->dev->db_stride);
	nvmeq->cq_head = head;
	nvmeq->cq_phase = phase;

	nvmeq->cqe_seen = 1;
}

static void nvme_process_cq(struct nvme_queue *nvmeq)
{
	__nvme_process_cq(nvmeq, NULL);
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

static int nvme_poll(struct blk_mq_hw_ctx *hctx, unsigned int tag)
{
	struct nvme_queue *nvmeq = hctx->driver_data;

	if ((le16_to_cpu(nvmeq->cqes[nvmeq->cq_head].status) & 1) ==
	    nvmeq->cq_phase) {
		spin_lock_irq(&nvmeq->q_lock);
		__nvme_process_cq(nvmeq, &tag);
		spin_unlock_irq(&nvmeq->q_lock);

		if (tag == -1)
			return 1;
	}

	return 0;
}

/*
 * Returns 0 on success.  If the result is negative, it's a Linux error code;
 * if the result is positive, it's an NVM Express status code
 */
int __nvme_submit_sync_cmd(struct request_queue *q, struct nvme_command *cmd,
		void *buffer, void __user *ubuffer, unsigned bufflen,
		u32 *result, unsigned timeout)
{
	bool write = cmd->common.opcode & 1;
	struct bio *bio = NULL;
	struct request *req;
	int ret;

	req = blk_mq_alloc_request(q, write, GFP_KERNEL, false);
	if (IS_ERR(req))
		return PTR_ERR(req);

	req->cmd_type = REQ_TYPE_DRV_PRIV;
	req->cmd_flags |= REQ_FAILFAST_DRIVER;
	req->__data_len = 0;
	req->__sector = (sector_t) -1;
	req->bio = req->biotail = NULL;

	req->timeout = timeout ? timeout : ADMIN_TIMEOUT;

	req->cmd = (unsigned char *)cmd;
	req->cmd_len = sizeof(struct nvme_command);
	req->special = (void *)0;

	if (buffer && bufflen) {
		ret = blk_rq_map_kern(q, req, buffer, bufflen,
				      __GFP_DIRECT_RECLAIM);
		if (ret)
			goto out;
	} else if (ubuffer && bufflen) {
		ret = blk_rq_map_user(q, req, NULL, ubuffer, bufflen,
				      __GFP_DIRECT_RECLAIM);
		if (ret)
			goto out;
		bio = req->bio;
	}

	blk_execute_rq(req->q, NULL, req, 0);
	if (bio)
		blk_rq_unmap_user(bio);
	if (result)
		*result = (u32)(uintptr_t)req->special;
	ret = req->errors;
 out:
	blk_mq_free_request(req);
	return ret;
}

int nvme_submit_sync_cmd(struct request_queue *q, struct nvme_command *cmd,
		void *buffer, unsigned bufflen)
{
	return __nvme_submit_sync_cmd(q, cmd, buffer, NULL, bufflen, NULL, 0);
}

static int nvme_submit_async_admin_req(struct nvme_dev *dev)
{
	struct nvme_queue *nvmeq = dev->queues[0];
	struct nvme_command c;
	struct nvme_cmd_info *cmd_info;
	struct request *req;

	req = blk_mq_alloc_request(dev->admin_q, WRITE, GFP_ATOMIC, true);
	if (IS_ERR(req))
		return PTR_ERR(req);

	req->cmd_flags |= REQ_NO_TIMEOUT;
	cmd_info = blk_mq_rq_to_pdu(req);
	nvme_set_info(cmd_info, NULL, async_req_completion);

	memset(&c, 0, sizeof(c));
	c.common.opcode = nvme_admin_async_event;
	c.common.command_id = req->tag;

	blk_mq_free_request(req);
	__nvme_submit_cmd(nvmeq, &c);
	return 0;
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

	nvme_submit_cmd(nvmeq, cmd);
	return 0;
}

static int adapter_delete_queue(struct nvme_dev *dev, u8 opcode, u16 id)
{
	struct nvme_command c;

	memset(&c, 0, sizeof(c));
	c.delete_queue.opcode = opcode;
	c.delete_queue.qid = cpu_to_le16(id);

	return nvme_submit_sync_cmd(dev->admin_q, &c, NULL, 0);
}

static int adapter_alloc_cq(struct nvme_dev *dev, u16 qid,
						struct nvme_queue *nvmeq)
{
	struct nvme_command c;
	int flags = NVME_QUEUE_PHYS_CONTIG | NVME_CQ_IRQ_ENABLED;

	/*
	 * Note: we (ab)use the fact the the prp fields survive if no data
	 * is attached to the request.
	 */
	memset(&c, 0, sizeof(c));
	c.create_cq.opcode = nvme_admin_create_cq;
	c.create_cq.prp1 = cpu_to_le64(nvmeq->cq_dma_addr);
	c.create_cq.cqid = cpu_to_le16(qid);
	c.create_cq.qsize = cpu_to_le16(nvmeq->q_depth - 1);
	c.create_cq.cq_flags = cpu_to_le16(flags);
	c.create_cq.irq_vector = cpu_to_le16(nvmeq->cq_vector);

	return nvme_submit_sync_cmd(dev->admin_q, &c, NULL, 0);
}

static int adapter_alloc_sq(struct nvme_dev *dev, u16 qid,
						struct nvme_queue *nvmeq)
{
	struct nvme_command c;
	int flags = NVME_QUEUE_PHYS_CONTIG | NVME_SQ_PRIO_MEDIUM;

	/*
	 * Note: we (ab)use the fact the the prp fields survive if no data
	 * is attached to the request.
	 */
	memset(&c, 0, sizeof(c));
	c.create_sq.opcode = nvme_admin_create_sq;
	c.create_sq.prp1 = cpu_to_le64(nvmeq->sq_dma_addr);
	c.create_sq.sqid = cpu_to_le16(qid);
	c.create_sq.qsize = cpu_to_le16(nvmeq->q_depth - 1);
	c.create_sq.sq_flags = cpu_to_le16(flags);
	c.create_sq.cqid = cpu_to_le16(qid);

	return nvme_submit_sync_cmd(dev->admin_q, &c, NULL, 0);
}

static int adapter_delete_cq(struct nvme_dev *dev, u16 cqid)
{
	return adapter_delete_queue(dev, nvme_admin_delete_cq, cqid);
}

static int adapter_delete_sq(struct nvme_dev *dev, u16 sqid)
{
	return adapter_delete_queue(dev, nvme_admin_delete_sq, sqid);
}

int nvme_identify_ctrl(struct nvme_dev *dev, struct nvme_id_ctrl **id)
{
	struct nvme_command c = { };
	int error;

	/* gcc-4.4.4 (at least) has issues with initializers and anon unions */
	c.identify.opcode = nvme_admin_identify;
	c.identify.cns = cpu_to_le32(1);

	*id = kmalloc(sizeof(struct nvme_id_ctrl), GFP_KERNEL);
	if (!*id)
		return -ENOMEM;

	error = nvme_submit_sync_cmd(dev->admin_q, &c, *id,
			sizeof(struct nvme_id_ctrl));
	if (error)
		kfree(*id);
	return error;
}

int nvme_identify_ns(struct nvme_dev *dev, unsigned nsid,
		struct nvme_id_ns **id)
{
	struct nvme_command c = { };
	int error;

	/* gcc-4.4.4 (at least) has issues with initializers and anon unions */
	c.identify.opcode = nvme_admin_identify,
	c.identify.nsid = cpu_to_le32(nsid),

	*id = kmalloc(sizeof(struct nvme_id_ns), GFP_KERNEL);
	if (!*id)
		return -ENOMEM;

	error = nvme_submit_sync_cmd(dev->admin_q, &c, *id,
			sizeof(struct nvme_id_ns));
	if (error)
		kfree(*id);
	return error;
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

	return __nvme_submit_sync_cmd(dev->admin_q, &c, NULL, NULL, 0,
			result, 0);
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

	return __nvme_submit_sync_cmd(dev->admin_q, &c, NULL, NULL, 0,
			result, 0);
}

int nvme_get_log_page(struct nvme_dev *dev, struct nvme_smart_log **log)
{
	struct nvme_command c = { };
	int error;

	c.common.opcode = nvme_admin_get_log_page,
	c.common.nsid = cpu_to_le32(0xFFFFFFFF),
	c.common.cdw10[0] = cpu_to_le32(
			(((sizeof(struct nvme_smart_log) / 4) - 1) << 16) |
			 NVME_LOG_SMART),

	*log = kmalloc(sizeof(struct nvme_smart_log), GFP_KERNEL);
	if (!*log)
		return -ENOMEM;

	error = nvme_submit_sync_cmd(dev->admin_q, &c, *log,
			sizeof(struct nvme_smart_log));
	if (error)
		kfree(*log);
	return error;
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
		spin_lock(&dev_list_lock);
		if (!__nvme_reset(dev)) {
			dev_warn(dev->dev,
				 "I/O %d QID %d timeout, reset controller\n",
				 req->tag, nvmeq->qid);
		}
		spin_unlock(&dev_list_lock);
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
	nvme_submit_cmd(dev->queues[0], &cmd);
}

static void nvme_cancel_queue_ios(struct request *req, void *data, bool reserved)
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

	dev_warn(nvmeq->q_dmadev, "Timeout I/O %d QID %d\n", req->tag,
							nvmeq->qid);
	spin_lock_irq(&nvmeq->q_lock);
	nvme_abort_req(req);
	spin_unlock_irq(&nvmeq->q_lock);

	/*
	 * The aborted req will be completed on receiving the abort req.
	 * We enable the timer again. If hit twice, it'll cause a device reset,
	 * as the device then is in a faulty state.
	 */
	return BLK_EH_RESET_TIMER;
}

static void nvme_free_queue(struct nvme_queue *nvmeq)
{
	dma_free_coherent(nvmeq->q_dmadev, CQ_SIZE(nvmeq->q_depth),
				(void *)nvmeq->cqes, nvmeq->cq_dma_addr);
	if (nvmeq->sq_cmds)
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

	if (!nvmeq->qid && nvmeq->dev->admin_q)
		blk_mq_freeze_queue_start(nvmeq->dev->admin_q);

	irq_set_affinity_hint(vector, NULL);
	free_irq(vector, nvmeq);

	return 0;
}

static void nvme_clear_queue(struct nvme_queue *nvmeq)
{
	spin_lock_irq(&nvmeq->q_lock);
	if (nvmeq->tags && *nvmeq->tags)
		blk_mq_all_tag_busy_iter(*nvmeq->tags, nvme_cancel_queue_ios, nvmeq);
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

	spin_lock_irq(&nvmeq->q_lock);
	nvme_process_cq(nvmeq);
	spin_unlock_irq(&nvmeq->q_lock);
}

static int nvme_cmb_qdepth(struct nvme_dev *dev, int nr_io_queues,
				int entry_size)
{
	int q_depth = dev->q_depth;
	unsigned q_size_aligned = roundup(q_depth * entry_size, dev->page_size);

	if (q_size_aligned * nr_io_queues > dev->cmb_size) {
		u64 mem_per_q = div_u64(dev->cmb_size, nr_io_queues);
		mem_per_q = round_down(mem_per_q, dev->page_size);
		q_depth = div_u64(mem_per_q, entry_size);

		/*
		 * Ensure the reduced q_depth is above some threshold where it
		 * would be better to map queues in system memory with the
		 * original depth
		 */
		if (q_depth < 64)
			return -ENOMEM;
	}

	return q_depth;
}

static int nvme_alloc_sq_cmds(struct nvme_dev *dev, struct nvme_queue *nvmeq,
				int qid, int depth)
{
	if (qid && dev->cmb && use_cmb_sqes && NVME_CMB_SQS(dev->cmbsz)) {
		unsigned offset = (qid - 1) *
					roundup(SQ_SIZE(depth), dev->page_size);
		nvmeq->sq_dma_addr = dev->cmb_dma_addr + offset;
		nvmeq->sq_cmds_io = dev->cmb + offset;
	} else {
		nvmeq->sq_cmds = dma_alloc_coherent(dev->dev, SQ_SIZE(depth),
					&nvmeq->sq_dma_addr, GFP_KERNEL);
		if (!nvmeq->sq_cmds)
			return -ENOMEM;
	}

	return 0;
}

static struct nvme_queue *nvme_alloc_queue(struct nvme_dev *dev, int qid,
							int depth)
{
	struct nvme_queue *nvmeq = kzalloc(sizeof(*nvmeq), GFP_KERNEL);
	if (!nvmeq)
		return NULL;

	nvmeq->cqes = dma_zalloc_coherent(dev->dev, CQ_SIZE(depth),
					  &nvmeq->cq_dma_addr, GFP_KERNEL);
	if (!nvmeq->cqes)
		goto free_nvmeq;

	if (nvme_alloc_sq_cmds(dev, nvmeq, qid, depth))
		goto free_cqdma;

	nvmeq->q_dmadev = dev->dev;
	nvmeq->dev = dev;
	snprintf(nvmeq->irqname, sizeof(nvmeq->irqname), "nvme%dq%d",
			dev->instance, qid);
	spin_lock_init(&nvmeq->q_lock);
	nvmeq->cq_head = 0;
	nvmeq->cq_phase = 1;
	nvmeq->q_db = &dev->dbs[qid * 2 * dev->db_stride];
	nvmeq->q_depth = depth;
	nvmeq->qid = qid;
	nvmeq->cq_vector = -1;
	dev->queues[qid] = nvmeq;

	/* make sure queue descriptor is set before queue count, for kthread */
	mb();
	dev->queue_count++;

	return nvmeq;

 free_cqdma:
	dma_free_coherent(dev->dev, CQ_SIZE(depth), (void *)nvmeq->cqes,
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
			dev_err(dev->dev,
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
	struct pci_dev *pdev = to_pci_dev(dev->dev);

	dev->ctrl_config &= ~NVME_CC_SHN_MASK;
	dev->ctrl_config &= ~NVME_CC_ENABLE;
	writel(dev->ctrl_config, &dev->bar->cc);

	if (pdev->vendor == 0x1c58 && pdev->device == 0x0003)
		msleep(NVME_QUIRK_DELAY_AMOUNT);

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
			dev_err(dev->dev,
				"Device shutdown incomplete; abort shutdown\n");
			return -ENODEV;
		}
	}

	return 0;
}

static struct blk_mq_ops nvme_mq_admin_ops = {
	.queue_rq	= nvme_queue_rq,
	.map_queue	= blk_mq_map_queue,
	.init_hctx	= nvme_admin_init_hctx,
	.exit_hctx      = nvme_admin_exit_hctx,
	.init_request	= nvme_admin_init_request,
	.timeout	= nvme_timeout,
};

static struct blk_mq_ops nvme_mq_ops = {
	.queue_rq	= nvme_queue_rq,
	.map_queue	= blk_mq_map_queue,
	.init_hctx	= nvme_init_hctx,
	.init_request	= nvme_init_request,
	.timeout	= nvme_timeout,
	.poll		= nvme_poll,
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
		dev->admin_tagset.reserved_tags = 1;
		dev->admin_tagset.timeout = ADMIN_TIMEOUT;
		dev->admin_tagset.numa_node = dev_to_node(dev->dev);
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
			dev->admin_q = NULL;
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
	u64 cap = lo_hi_readq(&dev->bar->cap);
	struct nvme_queue *nvmeq;
	/*
	 * default to a 4K page size, with the intention to update this
	 * path in the future to accomodate architectures with differing
	 * kernel and IO page sizes.
	 */
	unsigned page_shift = 12;
	unsigned dev_page_min = NVME_CAP_MPSMIN(cap) + 12;

	if (page_shift < dev_page_min) {
		dev_err(dev->dev,
				"Minimum device page size (%u) too large for "
				"host (%u)\n", 1 << dev_page_min,
				1 << page_shift);
		return -ENODEV;
	}

	dev->subsystem = readl(&dev->bar->vs) >= NVME_VS(1, 1) ?
						NVME_CAP_NSSRC(cap) : 0;

	if (dev->subsystem && (readl(&dev->bar->csts) & NVME_CSTS_NSSRO))
		writel(NVME_CSTS_NSSRO, &dev->bar->csts);

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
	lo_hi_writeq(nvmeq->sq_dma_addr, &dev->bar->asq);
	lo_hi_writeq(nvmeq->cq_dma_addr, &dev->bar->acq);

	result = nvme_enable_ctrl(dev, cap);
	if (result)
		goto free_nvmeq;

	nvmeq->cq_vector = 0;
	result = queue_request_irq(dev, nvmeq, nvmeq->irqname);
	if (result) {
		nvmeq->cq_vector = -1;
		goto free_nvmeq;
	}

	return result;

 free_nvmeq:
	nvme_free_queues(dev, 0);
	return result;
}

static int nvme_submit_io(struct nvme_ns *ns, struct nvme_user_io __user *uio)
{
	struct nvme_dev *dev = ns->dev;
	struct nvme_user_io io;
	struct nvme_command c;
	unsigned length, meta_len;
	int status, write;
	dma_addr_t meta_dma = 0;
	void *meta = NULL;
	void __user *metadata;

	if (copy_from_user(&io, uio, sizeof(io)))
		return -EFAULT;

	switch (io.opcode) {
	case nvme_cmd_write:
	case nvme_cmd_read:
	case nvme_cmd_compare:
		break;
	default:
		return -EINVAL;
	}

	length = (io.nblocks + 1) << ns->lba_shift;
	meta_len = (io.nblocks + 1) * ns->ms;
	metadata = (void __user *)(uintptr_t)io.metadata;
	write = io.opcode & 1;

	if (ns->ext) {
		length += meta_len;
		meta_len = 0;
	}
	if (meta_len) {
		if (((io.metadata & 3) || !io.metadata) && !ns->ext)
			return -EINVAL;

		meta = dma_alloc_coherent(dev->dev, meta_len,
						&meta_dma, GFP_KERNEL);

		if (!meta) {
			status = -ENOMEM;
			goto unmap;
		}
		if (write) {
			if (copy_from_user(meta, metadata, meta_len)) {
				status = -EFAULT;
				goto unmap;
			}
		}
	}

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
	c.rw.metadata = cpu_to_le64(meta_dma);

	status = __nvme_submit_sync_cmd(ns->queue, &c, NULL,
			(void __user *)(uintptr_t)io.addr, length, NULL, 0);
 unmap:
	if (meta) {
		if (status == NVME_SC_SUCCESS && !write) {
			if (copy_to_user(metadata, meta, meta_len))
				status = -EFAULT;
		}
		dma_free_coherent(dev->dev, meta_len, meta, meta_dma);
	}
	return status;
}

static int nvme_user_cmd(struct nvme_dev *dev, struct nvme_ns *ns,
			struct nvme_passthru_cmd __user *ucmd)
{
	struct nvme_passthru_cmd cmd;
	struct nvme_command c;
	unsigned timeout = 0;
	int status;

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

	if (cmd.timeout_ms)
		timeout = msecs_to_jiffies(cmd.timeout_ms);

	status = __nvme_submit_sync_cmd(ns ? ns->queue : dev->admin_q, &c,
			NULL, (void __user *)(uintptr_t)cmd.addr, cmd.data_len,
			&cmd.result, timeout);
	if (status >= 0) {
		if (put_user(cmd.result, &ucmd->result))
			return -EFAULT;
	}

	return status;
}

static int nvme_subsys_reset(struct nvme_dev *dev)
{
	if (!dev->subsystem)
		return -ENOTTY;

	writel(0x4E564D65, &dev->bar->nssr); /* "NVMe" */
	return 0;
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

static void nvme_free_dev(struct kref *kref);
static void nvme_free_ns(struct kref *kref)
{
	struct nvme_ns *ns = container_of(kref, struct nvme_ns, kref);

	if (ns->type == NVME_NS_LIGHTNVM)
		nvme_nvm_unregister(ns->queue, ns->disk->disk_name);

	spin_lock(&dev_list_lock);
	ns->disk->private_data = NULL;
	spin_unlock(&dev_list_lock);

	kref_put(&ns->dev->kref, nvme_free_dev);
	put_disk(ns->disk);
	kfree(ns);
}

static int nvme_open(struct block_device *bdev, fmode_t mode)
{
	int ret = 0;
	struct nvme_ns *ns;

	spin_lock(&dev_list_lock);
	ns = bdev->bd_disk->private_data;
	if (!ns)
		ret = -ENXIO;
	else if (!kref_get_unless_zero(&ns->kref))
		ret = -ENXIO;
	spin_unlock(&dev_list_lock);

	return ret;
}

static void nvme_release(struct gendisk *disk, fmode_t mode)
{
	struct nvme_ns *ns = disk->private_data;
	kref_put(&ns->kref, nvme_free_ns);
}

static int nvme_getgeo(struct block_device *bd, struct hd_geometry *geo)
{
	/* some standard values */
	geo->heads = 1 << 6;
	geo->sectors = 1 << 5;
	geo->cylinders = get_capacity(bd->bd_disk) >> 11;
	return 0;
}

static void nvme_config_discard(struct nvme_ns *ns)
{
	u32 logical_block_size = queue_logical_block_size(ns->queue);
	ns->queue->limits.discard_zeroes_data = 0;
	ns->queue->limits.discard_alignment = logical_block_size;
	ns->queue->limits.discard_granularity = logical_block_size;
	blk_queue_max_discard_sectors(ns->queue, 0xffffffff);
	queue_flag_set_unlocked(QUEUE_FLAG_DISCARD, ns->queue);
}

static int nvme_revalidate_disk(struct gendisk *disk)
{
	struct nvme_ns *ns = disk->private_data;
	struct nvme_dev *dev = ns->dev;
	struct nvme_id_ns *id;
	u8 lbaf, pi_type;
	u16 old_ms;
	unsigned short bs;

	if (nvme_identify_ns(dev, ns->ns_id, &id)) {
		dev_warn(dev->dev, "%s: Identify failure nvme%dn%d\n", __func__,
						dev->instance, ns->ns_id);
		return -ENODEV;
	}
	if (id->ncap == 0) {
		kfree(id);
		return -ENODEV;
	}

	if (nvme_nvm_ns_supported(ns, id) && ns->type != NVME_NS_LIGHTNVM) {
		if (nvme_nvm_register(ns->queue, disk->disk_name)) {
			dev_warn(dev->dev,
				"%s: LightNVM init failure\n", __func__);
			kfree(id);
			return -ENODEV;
		}
		ns->type = NVME_NS_LIGHTNVM;
	}

	old_ms = ns->ms;
	lbaf = id->flbas & NVME_NS_FLBAS_LBA_MASK;
	ns->lba_shift = id->lbaf[lbaf].ds;
	ns->ms = le16_to_cpu(id->lbaf[lbaf].ms);
	ns->ext = ns->ms && (id->flbas & NVME_NS_FLBAS_META_EXT);

	/*
	 * If identify namespace failed, use default 512 byte block size so
	 * block layer can use before failing read/write for 0 capacity.
	 */
	if (ns->lba_shift == 0)
		ns->lba_shift = 9;
	bs = 1 << ns->lba_shift;

	/* XXX: PI implementation requires metadata equal t10 pi tuple size */
	pi_type = ns->ms == sizeof(struct t10_pi_tuple) ?
					id->dps & NVME_NS_DPS_PI_MASK : 0;

	blk_mq_freeze_queue(disk->queue);
	if (blk_get_integrity(disk) && (ns->pi_type != pi_type ||
				ns->ms != old_ms ||
				bs != queue_logical_block_size(disk->queue) ||
				(ns->ms && ns->ext)))
		blk_integrity_unregister(disk);

	ns->pi_type = pi_type;
	blk_queue_logical_block_size(ns->queue, bs);

	if (ns->ms && !ns->ext)
		nvme_init_integrity(ns);

	if ((ns->ms && !(ns->ms == 8 && ns->pi_type) &&
						!blk_get_integrity(disk)) ||
						ns->type == NVME_NS_LIGHTNVM)
		set_capacity(disk, 0);
	else
		set_capacity(disk, le64_to_cpup(&id->nsze) << (ns->lba_shift - 9));

	if (dev->oncs & NVME_CTRL_ONCS_DSM)
		nvme_config_discard(ns);
	blk_mq_unfreeze_queue(disk->queue);

	kfree(id);
	return 0;
}

static char nvme_pr_type(enum pr_type type)
{
	switch (type) {
	case PR_WRITE_EXCLUSIVE:
		return 1;
	case PR_EXCLUSIVE_ACCESS:
		return 2;
	case PR_WRITE_EXCLUSIVE_REG_ONLY:
		return 3;
	case PR_EXCLUSIVE_ACCESS_REG_ONLY:
		return 4;
	case PR_WRITE_EXCLUSIVE_ALL_REGS:
		return 5;
	case PR_EXCLUSIVE_ACCESS_ALL_REGS:
		return 6;
	default:
		return 0;
	}
};

static int nvme_pr_command(struct block_device *bdev, u32 cdw10,
				u64 key, u64 sa_key, u8 op)
{
	struct nvme_ns *ns = bdev->bd_disk->private_data;
	struct nvme_command c;
	u8 data[16] = { 0, };

	put_unaligned_le64(key, &data[0]);
	put_unaligned_le64(sa_key, &data[8]);

	memset(&c, 0, sizeof(c));
	c.common.opcode = op;
	c.common.nsid = cpu_to_le32(ns->ns_id);
	c.common.cdw10[0] = cpu_to_le32(cdw10);

	return nvme_submit_sync_cmd(ns->queue, &c, data, 16);
}

static int nvme_pr_register(struct block_device *bdev, u64 old,
		u64 new, unsigned flags)
{
	u32 cdw10;

	if (flags & ~PR_FL_IGNORE_KEY)
		return -EOPNOTSUPP;

	cdw10 = old ? 2 : 0;
	cdw10 |= (flags & PR_FL_IGNORE_KEY) ? 1 << 3 : 0;
	cdw10 |= (1 << 30) | (1 << 31); /* PTPL=1 */
	return nvme_pr_command(bdev, cdw10, old, new, nvme_cmd_resv_register);
}

static int nvme_pr_reserve(struct block_device *bdev, u64 key,
		enum pr_type type, unsigned flags)
{
	u32 cdw10;

	if (flags & ~PR_FL_IGNORE_KEY)
		return -EOPNOTSUPP;

	cdw10 = nvme_pr_type(type) << 8;
	cdw10 |= ((flags & PR_FL_IGNORE_KEY) ? 1 << 3 : 0);
	return nvme_pr_command(bdev, cdw10, key, 0, nvme_cmd_resv_acquire);
}

static int nvme_pr_preempt(struct block_device *bdev, u64 old, u64 new,
		enum pr_type type, bool abort)
{
	u32 cdw10 = nvme_pr_type(type) << 8 | abort ? 2 : 1;
	return nvme_pr_command(bdev, cdw10, old, new, nvme_cmd_resv_acquire);
}

static int nvme_pr_clear(struct block_device *bdev, u64 key)
{
	u32 cdw10 = 1 | (key ? 1 << 3 : 0);
	return nvme_pr_command(bdev, cdw10, key, 0, nvme_cmd_resv_register);
}

static int nvme_pr_release(struct block_device *bdev, u64 key, enum pr_type type)
{
	u32 cdw10 = nvme_pr_type(type) << 8 | key ? 1 << 3 : 0;
	return nvme_pr_command(bdev, cdw10, key, 0, nvme_cmd_resv_release);
}

static const struct pr_ops nvme_pr_ops = {
	.pr_register	= nvme_pr_register,
	.pr_reserve	= nvme_pr_reserve,
	.pr_release	= nvme_pr_release,
	.pr_preempt	= nvme_pr_preempt,
	.pr_clear	= nvme_pr_clear,
};

static const struct block_device_operations nvme_fops = {
	.owner		= THIS_MODULE,
	.ioctl		= nvme_ioctl,
	.compat_ioctl	= nvme_compat_ioctl,
	.open		= nvme_open,
	.release	= nvme_release,
	.getgeo		= nvme_getgeo,
	.revalidate_disk= nvme_revalidate_disk,
	.pr_ops		= &nvme_pr_ops,
};

static int nvme_kthread(void *data)
{
	struct nvme_dev *dev, *next;

	while (!kthread_should_stop()) {
		set_current_state(TASK_INTERRUPTIBLE);
		spin_lock(&dev_list_lock);
		list_for_each_entry_safe(dev, next, &dev_list, node) {
			int i;
			u32 csts = readl(&dev->bar->csts);

			if ((dev->subsystem && (csts & NVME_CSTS_NSSRO)) ||
							csts & NVME_CSTS_CFS) {
				if (!__nvme_reset(dev)) {
					dev_warn(dev->dev,
						"Failed status: 0x%x, reset controller\n",
						csts);
				}
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

static void nvme_alloc_ns(struct nvme_dev *dev, unsigned nsid)
{
	struct nvme_ns *ns;
	struct gendisk *disk;
	int node = dev_to_node(dev->dev);

	ns = kzalloc_node(sizeof(*ns), GFP_KERNEL, node);
	if (!ns)
		return;

	ns->queue = blk_mq_init_queue(&dev->tagset);
	if (IS_ERR(ns->queue))
		goto out_free_ns;
	queue_flag_set_unlocked(QUEUE_FLAG_NOMERGES, ns->queue);
	queue_flag_set_unlocked(QUEUE_FLAG_NONROT, ns->queue);
	ns->dev = dev;
	ns->queue->queuedata = ns;

	disk = alloc_disk_node(0, node);
	if (!disk)
		goto out_free_queue;

	kref_init(&ns->kref);
	ns->ns_id = nsid;
	ns->disk = disk;
	ns->lba_shift = 9; /* set to a default value for 512 until disk is validated */
	list_add_tail(&ns->list, &dev->namespaces);

	blk_queue_logical_block_size(ns->queue, 1 << ns->lba_shift);
	if (dev->max_hw_sectors) {
		blk_queue_max_hw_sectors(ns->queue, dev->max_hw_sectors);
		blk_queue_max_segments(ns->queue,
			(dev->max_hw_sectors / (dev->page_size >> 9)) + 1);
	}
	if (dev->stripe_size)
		blk_queue_chunk_sectors(ns->queue, dev->stripe_size >> 9);
	if (dev->vwc & NVME_CTRL_VWC_PRESENT)
		blk_queue_flush(ns->queue, REQ_FLUSH | REQ_FUA);
	blk_queue_virt_boundary(ns->queue, dev->page_size - 1);

	disk->major = nvme_major;
	disk->first_minor = 0;
	disk->fops = &nvme_fops;
	disk->private_data = ns;
	disk->queue = ns->queue;
	disk->driverfs_dev = dev->device;
	disk->flags = GENHD_FL_EXT_DEVT;
#ifdef CONFIG_ARCH_ROCKCHIP
	disk->is_rk_disk = true;
#else
	disk->is_rk_disk = false;
#endif
	sprintf(disk->disk_name, "nvme%dn%d", dev->instance, nsid);

	/*
	 * Initialize capacity to 0 until we establish the namespace format and
	 * setup integrity extentions if necessary. The revalidate_disk after
	 * add_disk allows the driver to register with integrity if the format
	 * requires it.
	 */
	set_capacity(disk, 0);
	if (nvme_revalidate_disk(ns->disk))
		goto out_free_disk;

	kref_get(&dev->kref);
	if (ns->type != NVME_NS_LIGHTNVM) {
		add_disk(ns->disk);
		if (ns->ms) {
			struct block_device *bd = bdget_disk(ns->disk, 0);
			if (!bd)
				return;
			if (blkdev_get(bd, FMODE_READ, NULL)) {
				bdput(bd);
				return;
			}
			blkdev_reread_part(bd);
			blkdev_put(bd, FMODE_READ);
		}
	}
	return;
 out_free_disk:
	kfree(disk);
	list_del(&ns->list);
 out_free_queue:
	blk_cleanup_queue(ns->queue);
 out_free_ns:
	kfree(ns);
}

/*
 * Create I/O queues.  Failing to create an I/O queue is not an issue,
 * we can continue with less than the desired amount of queues, and
 * even a controller without I/O queues an still be used to issue
 * admin commands.  This might be useful to upgrade a buggy firmware
 * for example.
 */
static void nvme_create_io_queues(struct nvme_dev *dev)
{
	unsigned i;

	for (i = dev->queue_count; i <= dev->max_qid; i++)
		if (!nvme_alloc_queue(dev, i, dev->q_depth))
			break;

	for (i = dev->online_queues; i <= dev->queue_count - 1; i++)
		if (nvme_create_queue(dev->queues[i], i)) {
			nvme_free_queues(dev, i);
			break;
		}
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
		dev_err(dev->dev, "Could not set queue count (%d)\n", status);
		return 0;
	}
	return min(result & 0xffff, result >> 16) + 1;
}

static void __iomem *nvme_map_cmb(struct nvme_dev *dev)
{
	u64 szu, size, offset;
	u32 cmbloc;
	resource_size_t bar_size;
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	void __iomem *cmb;
	dma_addr_t dma_addr;

	if (!use_cmb_sqes)
		return NULL;

	dev->cmbsz = readl(&dev->bar->cmbsz);
	if (!(NVME_CMB_SZ(dev->cmbsz)))
		return NULL;

	cmbloc = readl(&dev->bar->cmbloc);

	szu = (u64)1 << (12 + 4 * NVME_CMB_SZU(dev->cmbsz));
	size = szu * NVME_CMB_SZ(dev->cmbsz);
	offset = szu * NVME_CMB_OFST(cmbloc);
	bar_size = pci_resource_len(pdev, NVME_CMB_BIR(cmbloc));

	if (offset > bar_size)
		return NULL;

	/*
	 * Controllers may support a CMB size larger than their BAR,
	 * for example, due to being behind a bridge. Reduce the CMB to
	 * the reported size of the BAR
	 */
	if (size > bar_size - offset)
		size = bar_size - offset;

	dma_addr = pci_resource_start(pdev, NVME_CMB_BIR(cmbloc)) + offset;
	cmb = ioremap_wc(dma_addr, size);
	if (!cmb)
		return NULL;

	dev->cmb_dma_addr = dma_addr;
	dev->cmb_size = size;
	return cmb;
}

static inline void nvme_release_cmb(struct nvme_dev *dev)
{
	if (dev->cmb) {
		iounmap(dev->cmb);
		dev->cmb = NULL;
	}
}

static size_t db_bar_size(struct nvme_dev *dev, unsigned nr_io_queues)
{
	return 4096 + ((nr_io_queues + 1) * 8 * dev->db_stride);
}

static int nvme_setup_io_queues(struct nvme_dev *dev)
{
	struct nvme_queue *adminq = dev->queues[0];
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	int result, i, vecs, nr_io_queues, size;

	nr_io_queues = num_possible_cpus();
	result = set_queue_count(dev, nr_io_queues);
	if (result <= 0)
		return result;
	if (result < nr_io_queues)
		nr_io_queues = result;

	if (dev->cmb && NVME_CMB_SQS(dev->cmbsz)) {
		result = nvme_cmb_qdepth(dev, nr_io_queues,
				sizeof(struct nvme_command));
		if (result > 0)
			dev->q_depth = result;
		else
			nvme_release_cmb(dev);
	}

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
	if (result) {
		adminq->cq_vector = -1;
		goto free_queues;
	}

	/* Free previously allocated queues that are no longer usable */
	nvme_free_queues(dev, nr_io_queues + 1);
	nvme_create_io_queues(dev);

	return 0;

 free_queues:
	nvme_free_queues(dev, 1);
	return result;
}

static int ns_cmp(void *priv, struct list_head *a, struct list_head *b)
{
	struct nvme_ns *nsa = container_of(a, struct nvme_ns, list);
	struct nvme_ns *nsb = container_of(b, struct nvme_ns, list);

	return nsa->ns_id - nsb->ns_id;
}

static struct nvme_ns *nvme_find_ns(struct nvme_dev *dev, unsigned nsid)
{
	struct nvme_ns *ns;

	list_for_each_entry(ns, &dev->namespaces, list) {
		if (ns->ns_id == nsid)
			return ns;
		if (ns->ns_id > nsid)
			break;
	}
	return NULL;
}

static inline bool nvme_io_incapable(struct nvme_dev *dev)
{
	return (!dev->bar || readl(&dev->bar->csts) & NVME_CSTS_CFS ||
							dev->online_queues < 2);
}

static void nvme_ns_remove(struct nvme_ns *ns)
{
	bool kill = nvme_io_incapable(ns->dev) && !blk_queue_dying(ns->queue);

	if (kill) {
		blk_set_queue_dying(ns->queue);

		/*
		 * The controller was shutdown first if we got here through
		 * device removal. The shutdown may requeue outstanding
		 * requests. These need to be aborted immediately so
		 * del_gendisk doesn't block indefinitely for their completion.
		 */
		blk_mq_abort_requeue_list(ns->queue);
	}
	if (ns->disk->flags & GENHD_FL_UP)
		del_gendisk(ns->disk);
	if (kill || !blk_queue_dying(ns->queue)) {
		blk_mq_abort_requeue_list(ns->queue);
		blk_cleanup_queue(ns->queue);
	}
	list_del_init(&ns->list);
	kref_put(&ns->kref, nvme_free_ns);
}

static void nvme_scan_namespaces(struct nvme_dev *dev, unsigned nn)
{
	struct nvme_ns *ns, *next;
	unsigned i;

	for (i = 1; i <= nn; i++) {
		ns = nvme_find_ns(dev, i);
		if (ns) {
			if (revalidate_disk(ns->disk))
				nvme_ns_remove(ns);
		} else
			nvme_alloc_ns(dev, i);
	}
	list_for_each_entry_safe(ns, next, &dev->namespaces, list) {
		if (ns->ns_id > nn)
			nvme_ns_remove(ns);
	}
	list_sort(NULL, &dev->namespaces, ns_cmp);
}

static void nvme_set_irq_hints(struct nvme_dev *dev)
{
	struct nvme_queue *nvmeq;
	int i;

	for (i = 0; i < dev->online_queues; i++) {
		nvmeq = dev->queues[i];

		if (!nvmeq->tags || !(*nvmeq->tags))
			continue;

		irq_set_affinity_hint(dev->entry[nvmeq->cq_vector].vector,
					blk_mq_tags_cpumask(*nvmeq->tags));
	}
}

static void nvme_dev_scan(struct work_struct *work)
{
	struct nvme_dev *dev = container_of(work, struct nvme_dev, scan_work);
	struct nvme_id_ctrl *ctrl;

	if (!dev->tagset.tags)
		return;
	if (nvme_identify_ctrl(dev, &ctrl))
		return;
	nvme_scan_namespaces(dev, le32_to_cpup(&ctrl->nn));
	kfree(ctrl);
	nvme_set_irq_hints(dev);
}

/*
 * Return: error value if an error occurred setting up the queues or calling
 * Identify Device.  0 if these succeeded, even if adding some of the
 * namespaces failed.  At the moment, these failures are silent.  TBD which
 * failures should be reported.
 */
static int nvme_dev_add(struct nvme_dev *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	int res;
	struct nvme_id_ctrl *ctrl;
	int shift = NVME_CAP_MPSMIN(lo_hi_readq(&dev->bar->cap)) + 12;

	res = nvme_identify_ctrl(dev, &ctrl);
	if (res) {
		dev_err(dev->dev, "Identify Controller failed (%d)\n", res);
		return -EIO;
	}

	dev->oncs = le16_to_cpup(&ctrl->oncs);
	dev->abort_limit = ctrl->acl + 1;
	dev->vwc = ctrl->vwc;
	memcpy(dev->serial, ctrl->sn, sizeof(ctrl->sn));
	memcpy(dev->model, ctrl->mn, sizeof(ctrl->mn));
	memcpy(dev->firmware_rev, ctrl->fr, sizeof(ctrl->fr));
	if (ctrl->mdts)
		dev->max_hw_sectors = 1 << (ctrl->mdts + shift - 9);
	else
		dev->max_hw_sectors = UINT_MAX;
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
	kfree(ctrl);

	if (!dev->tagset.tags) {
		dev->tagset.ops = &nvme_mq_ops;
		dev->tagset.nr_hw_queues = dev->online_queues - 1;
		dev->tagset.timeout = NVME_IO_TIMEOUT;
		dev->tagset.numa_node = dev_to_node(dev->dev);
		dev->tagset.queue_depth =
				min_t(int, dev->q_depth, BLK_MQ_MAX_DEPTH) - 1;
		dev->tagset.cmd_size = nvme_cmd_size(dev);
		dev->tagset.flags = BLK_MQ_F_SHOULD_MERGE;
		dev->tagset.driver_data = dev;

		if (blk_mq_alloc_tag_set(&dev->tagset))
			return 0;
	}
	schedule_work(&dev->scan_work);
	return 0;
}

static int nvme_pci_enable(struct nvme_dev *dev)
{
	u64 cap;
	int result = -ENOMEM;
	struct pci_dev *pdev = to_pci_dev(dev->dev);

	if (pci_enable_device_mem(pdev))
		return result;

	dev->entry[0].vector = pdev->irq;
	pci_set_master(pdev);

	if (dma_set_mask_and_coherent(dev->dev, DMA_BIT_MASK(64)) &&
	    dma_set_mask_and_coherent(dev->dev, DMA_BIT_MASK(32)))
		goto disable;

	if (readl(&dev->bar->csts) == -1) {
		result = -ENODEV;
		goto disable;
	}

	/*
	 * Some devices don't advertse INTx interrupts, pre-enable a single
	 * MSIX vec for setup. We'll adjust this later.
	 */
	if (!pdev->irq) {
		result = pci_enable_msix(pdev, dev->entry, 1);
		if (result < 0)
			goto disable;
	}

	cap = lo_hi_readq(&dev->bar->cap);
	dev->q_depth = min_t(int, NVME_CAP_MQES(cap) + 1, NVME_Q_DEPTH);
	dev->db_stride = 1 << NVME_CAP_STRIDE(cap);
	dev->dbs = ((void __iomem *)dev->bar) + 4096;

	/*
	 * Temporary fix for the Apple controller found in the MacBook8,1 and
	 * some MacBook7,1 to avoid controller resets and data loss.
	 */
	if (pdev->vendor == PCI_VENDOR_ID_APPLE && pdev->device == 0x2001) {
		dev->q_depth = 2;
		dev_warn(dev->dev, "detected Apple NVMe controller, set "
			"queue depth=%u to work around controller resets\n",
			dev->q_depth);
	}

	if (readl(&dev->bar->vs) >= NVME_VS(1, 2))
		dev->cmb = nvme_map_cmb(dev);

	return 0;

 disable:
	pci_disable_device(pdev);

	return result;
}

static void nvme_dev_unmap(struct nvme_dev *dev)
{
	if (dev->bar)
		iounmap(dev->bar);
	pci_release_regions(to_pci_dev(dev->dev));
}

static void nvme_pci_disable(struct nvme_dev *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev->dev);

	if (pdev->msi_enabled)
		pci_disable_msi(pdev);
	else if (pdev->msix_enabled)
		pci_disable_msix(pdev);

	if (pci_is_enabled(pdev))
		pci_disable_device(pdev);
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
			nvme_disable_ctrl(dev, lo_hi_readq(&dev->bar->cap));
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
	nvme_put_dq(dq);

	spin_lock_irq(&nvmeq->q_lock);
	nvme_process_cq(nvmeq);
	spin_unlock_irq(&nvmeq->q_lock);
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
		dev_err(dev->dev,
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

		spin_lock_irq(ns->queue->queue_lock);
		queue_flag_set(QUEUE_FLAG_STOPPED, ns->queue);
		spin_unlock_irq(ns->queue->queue_lock);

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

	nvme_dev_list_remove(dev);

	mutex_lock(&dev->shutdown_lock);
	if (pci_is_enabled(to_pci_dev(dev->dev))) {
		nvme_freeze_queues(dev);
		csts = readl(&dev->bar->csts);
	}
	if (csts & NVME_CSTS_CFS || !(csts & NVME_CSTS_RDY)) {
		for (i = dev->queue_count - 1; i >= 0; i--) {
			struct nvme_queue *nvmeq = dev->queues[i];
			nvme_suspend_queue(nvmeq);
		}
	} else {
		nvme_disable_io_queues(dev);
		nvme_shutdown_ctrl(dev);
		nvme_disable_queue(dev, 0);
	}
	nvme_pci_disable(dev);

	for (i = dev->queue_count - 1; i >= 0; i--)
		nvme_clear_queue(dev->queues[i]);
	mutex_unlock(&dev->shutdown_lock);
}

static void nvme_remove_namespaces(struct nvme_dev *dev)
{
	struct nvme_ns *ns, *next;

	list_for_each_entry_safe(ns, next, &dev->namespaces, list)
		nvme_ns_remove(ns);
}

static void nvme_dev_remove(struct nvme_dev *dev)
{
	if (nvme_io_incapable(dev)) {
		/*
		 * If the device is not capable of IO (surprise hot-removal,
		 * for example), we need to quiesce prior to deleting the
		 * namespaces. This will end outstanding requests and prevent
		 * attempts to sync dirty data.
		 */
		nvme_dev_shutdown(dev);
	}
	nvme_remove_namespaces(dev);
}

static int nvme_setup_prp_pools(struct nvme_dev *dev)
{
	dev->prp_page_pool = dma_pool_create("prp list page", dev->dev,
						PAGE_SIZE, PAGE_SIZE, 0);
	if (!dev->prp_page_pool)
		return -ENOMEM;

	/* Optimisation for I/Os between 4k and 128k */
	dev->prp_small_pool = dma_pool_create("prp list 256", dev->dev,
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

static void nvme_free_dev(struct kref *kref)
{
	struct nvme_dev *dev = container_of(kref, struct nvme_dev, kref);

	put_device(dev->dev);
	put_device(dev->device);
	nvme_release_instance(dev);
	if (dev->tagset.tags)
		blk_mq_free_tag_set(&dev->tagset);
	if (dev->admin_q)
		blk_put_queue(dev->admin_q);
	kfree(dev->queues);
	kfree(dev->entry);
	kfree(dev);
}

static int nvme_dev_open(struct inode *inode, struct file *f)
{
	struct nvme_dev *dev;
	int instance = iminor(inode);
	int ret = -ENODEV;

	spin_lock(&dev_list_lock);
	list_for_each_entry(dev, &dev_list, node) {
		if (dev->instance == instance) {
			if (!dev->admin_q) {
				ret = -EWOULDBLOCK;
				break;
			}
			if (!kref_get_unless_zero(&dev->kref))
				break;
			f->private_data = dev;
			ret = 0;
			break;
		}
	}
	spin_unlock(&dev_list_lock);

	return ret;
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
	case NVME_IOCTL_RESET:
		dev_warn(dev->dev, "resetting controller\n");
		return nvme_reset(dev);
	case NVME_IOCTL_SUBSYS_RESET:
		return nvme_subsys_reset(dev);
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

static void nvme_probe_work(struct work_struct *work)
{
	struct nvme_dev *dev = container_of(work, struct nvme_dev, probe_work);
	bool start_thread = false;
	int result;

	result = nvme_pci_enable(dev);
	if (result)
		goto out;

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

	dev->event_limit = 1;

	/*
	 * Keep the controller around but remove all namespaces if we don't have
	 * any working I/O queue.
	 */
	if (dev->online_queues < 2) {
		dev_warn(dev->dev, "IO queues not created\n");
		nvme_remove_namespaces(dev);
	} else {
		nvme_unfreeze_queues(dev);
		nvme_dev_add(dev);
	}

	return;

 free_tags:
	nvme_dev_remove_admin(dev);
	blk_put_queue(dev->admin_q);
	dev->admin_q = NULL;
	dev->queues[0]->tags = NULL;
 disable:
	nvme_disable_queue(dev, 0);
	nvme_dev_list_remove(dev);
 unmap:
	nvme_dev_unmap(dev);
 out:
	if (!work_busy(&dev->reset_work))
		nvme_dead_ctrl(dev);
}

static int nvme_remove_dead_ctrl(void *arg)
{
	struct nvme_dev *dev = (struct nvme_dev *)arg;
	struct pci_dev *pdev = to_pci_dev(dev->dev);

	if (pci_get_drvdata(pdev))
		pci_stop_and_remove_bus_device_locked(pdev);
	kref_put(&dev->kref, nvme_free_dev);
	return 0;
}

static void nvme_dead_ctrl(struct nvme_dev *dev)
{
	dev_warn(dev->dev, "Device failed to resume\n");
	kref_get(&dev->kref);
	if (IS_ERR(kthread_run(nvme_remove_dead_ctrl, dev, "nvme%d",
						dev->instance))) {
		dev_err(dev->dev,
			"Failed to start controller remove task\n");
		kref_put(&dev->kref, nvme_free_dev);
	}
}

static void nvme_reset_work(struct work_struct *ws)
{
	struct nvme_dev *dev = container_of(ws, struct nvme_dev, reset_work);
	bool in_probe = work_busy(&dev->probe_work);

	nvme_dev_shutdown(dev);

	/* Synchronize with device probe so that work will see failure status
	 * and exit gracefully without trying to schedule another reset */
	flush_work(&dev->probe_work);

	/* Fail this device if reset occured during probe to avoid
	 * infinite initialization loops. */
	if (in_probe) {
		nvme_dead_ctrl(dev);
		return;
	}
	/* Schedule device resume asynchronously so the reset work is available
	 * to cleanup errors that may occur during reinitialization */
	schedule_work(&dev->probe_work);
}

static int __nvme_reset(struct nvme_dev *dev)
{
	if (work_pending(&dev->reset_work))
		return -EBUSY;
	list_del_init(&dev->node);
	queue_work(nvme_workq, &dev->reset_work);
	return 0;
}

static int nvme_reset(struct nvme_dev *dev)
{
	int ret;

	if (!dev->admin_q || blk_queue_dying(dev->admin_q))
		return -ENODEV;

	spin_lock(&dev_list_lock);
	ret = __nvme_reset(dev);
	spin_unlock(&dev_list_lock);

	if (!ret) {
		flush_work(&dev->reset_work);
		flush_work(&dev->probe_work);
		return 0;
	}

	return ret;
}

static ssize_t nvme_sysfs_reset(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	struct nvme_dev *ndev = dev_get_drvdata(dev);
	int ret;

	ret = nvme_reset(ndev);
	if (ret < 0)
		return ret;

	return count;
}
static DEVICE_ATTR(reset_controller, S_IWUSR, NULL, nvme_sysfs_reset);

static int nvme_dev_map(struct nvme_dev *dev)
{
	int bars;
	struct pci_dev *pdev = to_pci_dev(dev->dev);

	bars = pci_select_bars(pdev, IORESOURCE_MEM);
	if (!bars)
		return -ENODEV;
	if (pci_request_selected_regions(pdev, bars, "nvme"))
		return -ENODEV;

	dev->bar = ioremap(pci_resource_start(pdev, 0), 8192);
	if (!dev->bar)
		goto release;

	return 0;
release:
	pci_release_regions(pdev);
	return -ENODEV;
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
	INIT_WORK(&dev->reset_work, nvme_reset_work);
	mutex_init(&dev->shutdown_lock);
	dev->dev = get_device(&pdev->dev);
	pci_set_drvdata(pdev, dev);

	result = nvme_dev_map(dev);
	if (result)
		goto free;

	result = nvme_set_instance(dev);
	if (result)
		goto put_pci;

	result = nvme_setup_prp_pools(dev);
	if (result)
		goto release;

	kref_init(&dev->kref);
	dev->device = device_create(nvme_class, &pdev->dev,
				MKDEV(nvme_char_major, dev->instance),
				dev, "nvme%d", dev->instance);
	if (IS_ERR(dev->device)) {
		result = PTR_ERR(dev->device);
		goto release_pools;
	}
	get_device(dev->device);
	dev_set_drvdata(dev->device, dev);

	result = device_create_file(dev->device, &dev_attr_reset_controller);
	if (result)
		goto put_dev;

	INIT_LIST_HEAD(&dev->node);
	INIT_WORK(&dev->scan_work, nvme_dev_scan);
	INIT_WORK(&dev->probe_work, nvme_probe_work);
	schedule_work(&dev->probe_work);
	return 0;

 put_dev:
	device_destroy(nvme_class, MKDEV(nvme_char_major, dev->instance));
	put_device(dev->device);
 release_pools:
	nvme_release_prp_pools(dev);
 release:
	nvme_release_instance(dev);
 put_pci:
	put_device(dev->dev);
	nvme_dev_unmap(dev);
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
		schedule_work(&dev->probe_work);
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
	flush_work(&dev->probe_work);
	flush_work(&dev->reset_work);
	flush_work(&dev->scan_work);
	device_remove_file(dev->device, &dev_attr_reset_controller);
	nvme_dev_remove(dev);
	nvme_dev_shutdown(dev);
	nvme_dev_remove_admin(dev);
	device_destroy(nvme_class, MKDEV(nvme_char_major, dev->instance));
	nvme_free_queues(dev, 0);
	nvme_release_cmb(dev);
	nvme_release_prp_pools(dev);
	nvme_dev_unmap(dev);
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

	schedule_work(&ndev->probe_work);
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
	{ PCI_DEVICE(PCI_VENDOR_ID_APPLE, 0x2001) },
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

	result = __register_chrdev(nvme_char_major, 0, NVME_MINORS, "nvme",
							&nvme_dev_fops);
	if (result < 0)
		goto unregister_blkdev;
	else if (result > 0)
		nvme_char_major = result;

	nvme_class = class_create(THIS_MODULE, "nvme");
	if (IS_ERR(nvme_class)) {
		result = PTR_ERR(nvme_class);
		goto unregister_chrdev;
	}

	result = pci_register_driver(&nvme_driver);
	if (result)
		goto destroy_class;
	return 0;

 destroy_class:
	class_destroy(nvme_class);
 unregister_chrdev:
	__unregister_chrdev(nvme_char_major, 0, NVME_MINORS, "nvme");
 unregister_blkdev:
	unregister_blkdev(nvme_major, "nvme");
 kill_workq:
	destroy_workqueue(nvme_workq);
	return result;
}

static void __exit nvme_exit(void)
{
	pci_unregister_driver(&nvme_driver);
	unregister_blkdev(nvme_major, "nvme");
	destroy_workqueue(nvme_workq);
	class_destroy(nvme_class);
	__unregister_chrdev(nvme_char_major, 0, NVME_MINORS, "nvme");
	BUG_ON(nvme_thread && !IS_ERR(nvme_thread));
	_nvme_check_size();
}

MODULE_AUTHOR("Matthew Wilcox <willy@linux.intel.com>");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0");
module_init(nvme_init);
module_exit(nvme_exit);
