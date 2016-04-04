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

#include <linux/aer.h>
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
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/poison.h>
#include <linux/ptrace.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/t10-pi.h>
#include <linux/timer.h>
#include <linux/types.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <asm/unaligned.h>

#include "nvme.h"

#define NVME_Q_DEPTH		1024
#define NVME_AQ_DEPTH		256
#define SQ_SIZE(depth)		(depth * sizeof(struct nvme_command))
#define CQ_SIZE(depth)		(depth * sizeof(struct nvme_completion))
		
/*
 * We handle AEN commands ourselves and don't even let the
 * block layer know about them.
 */
#define NVME_AQ_BLKMQ_DEPTH	(NVME_AQ_DEPTH - NVME_NR_AERS)

static int use_threaded_interrupts;
module_param(use_threaded_interrupts, int, 0);

static bool use_cmb_sqes = true;
module_param(use_cmb_sqes, bool, 0644);
MODULE_PARM_DESC(use_cmb_sqes, "use controller's memory buffer for I/O SQes");

static struct workqueue_struct *nvme_workq;

struct nvme_dev;
struct nvme_queue;

static int nvme_reset(struct nvme_dev *dev);
static void nvme_process_cq(struct nvme_queue *nvmeq);
static void nvme_dev_disable(struct nvme_dev *dev, bool shutdown);

/*
 * Represents an NVM Express device.  Each nvme_dev is a PCI function.
 */
struct nvme_dev {
	struct nvme_queue **queues;
	struct blk_mq_tag_set tagset;
	struct blk_mq_tag_set admin_tagset;
	u32 __iomem *dbs;
	struct device *dev;
	struct dma_pool *prp_page_pool;
	struct dma_pool *prp_small_pool;
	unsigned queue_count;
	unsigned online_queues;
	unsigned max_qid;
	int q_depth;
	u32 db_stride;
	struct msix_entry *entry;
	void __iomem *bar;
	struct work_struct reset_work;
	struct work_struct remove_work;
	struct timer_list watchdog_timer;
	struct mutex shutdown_lock;
	bool subsystem;
	void __iomem *cmb;
	dma_addr_t cmb_dma_addr;
	u64 cmb_size;
	u32 cmbsz;
	struct nvme_ctrl ctrl;
	struct completion ioq_wait;
};

static inline struct nvme_dev *to_nvme_dev(struct nvme_ctrl *ctrl)
{
	return container_of(ctrl, struct nvme_dev, ctrl);
}

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
	u16 sq_tail;
	u16 cq_head;
	u16 qid;
	u8 cq_phase;
	u8 cqe_seen;
};

/*
 * The nvme_iod describes the data in an I/O, including the list of PRP
 * entries.  You can't see it in this data structure because C doesn't let
 * me express that.  Use nvme_init_iod to ensure there's enough space
 * allocated to store the PRP list.
 */
struct nvme_iod {
	struct nvme_queue *nvmeq;
	int aborted;
	int npages;		/* In the PRP list. 0 means small pool in use */
	int nents;		/* Used in scatterlist */
	int length;		/* Of data, in bytes */
	dma_addr_t first_dma;
	struct scatterlist meta_sg; /* metadata requires single contiguous buffer */
	struct scatterlist *sg;
	struct scatterlist inline_sg[0];
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

/*
 * Max size of iod being embedded in the request payload
 */
#define NVME_INT_PAGES		2
#define NVME_INT_BYTES(dev)	(NVME_INT_PAGES * (dev)->ctrl.page_size)

/*
 * Will slightly overestimate the number of pages needed.  This is OK
 * as it only leads to a small amount of wasted memory for the lifetime of
 * the I/O.
 */
static int nvme_npages(unsigned size, struct nvme_dev *dev)
{
	unsigned nprps = DIV_ROUND_UP(size + dev->ctrl.page_size,
				      dev->ctrl.page_size);
	return DIV_ROUND_UP(8 * nprps, PAGE_SIZE - 8);
}

static unsigned int nvme_iod_alloc_size(struct nvme_dev *dev,
		unsigned int size, unsigned int nseg)
{
	return sizeof(__le64 *) * nvme_npages(size, dev) +
			sizeof(struct scatterlist) * nseg;
}

static unsigned int nvme_cmd_size(struct nvme_dev *dev)
{
	return sizeof(struct nvme_iod) +
		nvme_iod_alloc_size(dev, NVME_INT_BYTES(dev), NVME_INT_PAGES);
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
	struct nvme_iod *iod = blk_mq_rq_to_pdu(req);
	struct nvme_queue *nvmeq = dev->queues[0];

	BUG_ON(!nvmeq);
	iod->nvmeq = nvmeq;
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
	struct nvme_iod *iod = blk_mq_rq_to_pdu(req);
	struct nvme_queue *nvmeq = dev->queues[hctx_idx + 1];

	BUG_ON(!nvmeq);
	iod->nvmeq = nvmeq;
	return 0;
}

/**
 * __nvme_submit_cmd() - Copy a command into a queue and ring the doorbell
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

static __le64 **iod_list(struct request *req)
{
	struct nvme_iod *iod = blk_mq_rq_to_pdu(req);
	return (__le64 **)(iod->sg + req->nr_phys_segments);
}

static int nvme_init_iod(struct request *rq, unsigned size,
		struct nvme_dev *dev)
{
	struct nvme_iod *iod = blk_mq_rq_to_pdu(rq);
	int nseg = rq->nr_phys_segments;

	if (nseg > NVME_INT_PAGES || size > NVME_INT_BYTES(dev)) {
		iod->sg = kmalloc(nvme_iod_alloc_size(dev, size, nseg), GFP_ATOMIC);
		if (!iod->sg)
			return BLK_MQ_RQ_QUEUE_BUSY;
	} else {
		iod->sg = iod->inline_sg;
	}

	iod->aborted = 0;
	iod->npages = -1;
	iod->nents = 0;
	iod->length = size;
	return 0;
}

static void nvme_free_iod(struct nvme_dev *dev, struct request *req)
{
	struct nvme_iod *iod = blk_mq_rq_to_pdu(req);
	const int last_prp = dev->ctrl.page_size / 8 - 1;
	int i;
	__le64 **list = iod_list(req);
	dma_addr_t prp_dma = iod->first_dma;

	nvme_cleanup_cmd(req);

	if (iod->npages == 0)
		dma_pool_free(dev->prp_small_pool, list[0], prp_dma);
	for (i = 0; i < iod->npages; i++) {
		__le64 *prp_list = list[i];
		dma_addr_t next_prp_dma = le64_to_cpu(prp_list[last_prp]);
		dma_pool_free(dev->prp_page_pool, prp_list, prp_dma);
		prp_dma = next_prp_dma;
	}

	if (iod->sg != iod->inline_sg)
		kfree(iod->sg);
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
#endif

static bool nvme_setup_prps(struct nvme_dev *dev, struct request *req,
		int total_len)
{
	struct nvme_iod *iod = blk_mq_rq_to_pdu(req);
	struct dma_pool *pool;
	int length = total_len;
	struct scatterlist *sg = iod->sg;
	int dma_len = sg_dma_len(sg);
	u64 dma_addr = sg_dma_address(sg);
	u32 page_size = dev->ctrl.page_size;
	int offset = dma_addr & (page_size - 1);
	__le64 *prp_list;
	__le64 **list = iod_list(req);
	dma_addr_t prp_dma;
	int nprps, i;

	length -= (page_size - offset);
	if (length <= 0)
		return true;

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
		return true;
	}

	nprps = DIV_ROUND_UP(length, page_size);
	if (nprps <= (256 / 8)) {
		pool = dev->prp_small_pool;
		iod->npages = 0;
	} else {
		pool = dev->prp_page_pool;
		iod->npages = 1;
	}

	prp_list = dma_pool_alloc(pool, GFP_ATOMIC, &prp_dma);
	if (!prp_list) {
		iod->first_dma = dma_addr;
		iod->npages = -1;
		return false;
	}
	list[0] = prp_list;
	iod->first_dma = prp_dma;
	i = 0;
	for (;;) {
		if (i == page_size >> 3) {
			__le64 *old_prp_list = prp_list;
			prp_list = dma_pool_alloc(pool, GFP_ATOMIC, &prp_dma);
			if (!prp_list)
				return false;
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

	return true;
}

static int nvme_map_data(struct nvme_dev *dev, struct request *req,
		unsigned size, struct nvme_command *cmnd)
{
	struct nvme_iod *iod = blk_mq_rq_to_pdu(req);
	struct request_queue *q = req->q;
	enum dma_data_direction dma_dir = rq_data_dir(req) ?
			DMA_TO_DEVICE : DMA_FROM_DEVICE;
	int ret = BLK_MQ_RQ_QUEUE_ERROR;

	sg_init_table(iod->sg, req->nr_phys_segments);
	iod->nents = blk_rq_map_sg(q, req, iod->sg);
	if (!iod->nents)
		goto out;

	ret = BLK_MQ_RQ_QUEUE_BUSY;
	if (!dma_map_sg(dev->dev, iod->sg, iod->nents, dma_dir))
		goto out;

	if (!nvme_setup_prps(dev, req, size))
		goto out_unmap;

	ret = BLK_MQ_RQ_QUEUE_ERROR;
	if (blk_integrity_rq(req)) {
		if (blk_rq_count_integrity_sg(q, req->bio) != 1)
			goto out_unmap;

		sg_init_table(&iod->meta_sg, 1);
		if (blk_rq_map_integrity_sg(q, req->bio, &iod->meta_sg) != 1)
			goto out_unmap;

		if (rq_data_dir(req))
			nvme_dif_remap(req, nvme_dif_prep);

		if (!dma_map_sg(dev->dev, &iod->meta_sg, 1, dma_dir))
			goto out_unmap;
	}

	cmnd->rw.prp1 = cpu_to_le64(sg_dma_address(iod->sg));
	cmnd->rw.prp2 = cpu_to_le64(iod->first_dma);
	if (blk_integrity_rq(req))
		cmnd->rw.metadata = cpu_to_le64(sg_dma_address(&iod->meta_sg));
	return BLK_MQ_RQ_QUEUE_OK;

out_unmap:
	dma_unmap_sg(dev->dev, iod->sg, iod->nents, dma_dir);
out:
	return ret;
}

static void nvme_unmap_data(struct nvme_dev *dev, struct request *req)
{
	struct nvme_iod *iod = blk_mq_rq_to_pdu(req);
	enum dma_data_direction dma_dir = rq_data_dir(req) ?
			DMA_TO_DEVICE : DMA_FROM_DEVICE;

	if (iod->nents) {
		dma_unmap_sg(dev->dev, iod->sg, iod->nents, dma_dir);
		if (blk_integrity_rq(req)) {
			if (!rq_data_dir(req))
				nvme_dif_remap(req, nvme_dif_complete);
			dma_unmap_sg(dev->dev, &iod->meta_sg, 1, dma_dir);
		}
	}

	nvme_free_iod(dev, req);
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
	struct nvme_command cmnd;
	unsigned map_len;
	int ret = BLK_MQ_RQ_QUEUE_OK;

	/*
	 * If formated with metadata, require the block layer provide a buffer
	 * unless this namespace is formated such that the metadata can be
	 * stripped/generated by the controller with PRACT=1.
	 */
	if (ns && ns->ms && !blk_integrity_rq(req)) {
		if (!(ns->pi_type && ns->ms == 8) &&
					req->cmd_type != REQ_TYPE_DRV_PRIV) {
			blk_mq_end_request(req, -EFAULT);
			return BLK_MQ_RQ_QUEUE_OK;
		}
	}

	map_len = nvme_map_len(req);
	ret = nvme_init_iod(req, map_len, dev);
	if (ret)
		return ret;

	ret = nvme_setup_cmd(ns, req, &cmnd);
	if (ret)
		goto out;

	if (req->nr_phys_segments)
		ret = nvme_map_data(dev, req, map_len, &cmnd);

	if (ret)
		goto out;

	cmnd.common.command_id = req->tag;
	blk_mq_start_request(req);

	spin_lock_irq(&nvmeq->q_lock);
	if (unlikely(nvmeq->cq_vector < 0)) {
		if (ns && !test_bit(NVME_NS_DEAD, &ns->flags))
			ret = BLK_MQ_RQ_QUEUE_BUSY;
		else
			ret = BLK_MQ_RQ_QUEUE_ERROR;
		spin_unlock_irq(&nvmeq->q_lock);
		goto out;
	}
	__nvme_submit_cmd(nvmeq, &cmnd);
	nvme_process_cq(nvmeq);
	spin_unlock_irq(&nvmeq->q_lock);
	return BLK_MQ_RQ_QUEUE_OK;
out:
	nvme_free_iod(dev, req);
	return ret;
}

static void nvme_complete_rq(struct request *req)
{
	struct nvme_iod *iod = blk_mq_rq_to_pdu(req);
	struct nvme_dev *dev = iod->nvmeq->dev;
	int error = 0;

	nvme_unmap_data(dev, req);

	if (unlikely(req->errors)) {
		if (nvme_req_needs_retry(req, req->errors)) {
			nvme_requeue_req(req);
			return;
		}

		if (req->cmd_type == REQ_TYPE_DRV_PRIV)
			error = req->errors;
		else
			error = nvme_error_status(req->errors);
	}

	if (unlikely(iod->aborted)) {
		dev_warn(dev->ctrl.device,
			"completing aborted command with status: %04x\n",
			req->errors);
	}

	blk_mq_end_request(req, error);
}

/* We read the CQE phase first to check if the rest of the entry is valid */
static inline bool nvme_cqe_valid(struct nvme_queue *nvmeq, u16 head,
		u16 phase)
{
	return (le16_to_cpu(nvmeq->cqes[head].status) & 1) == phase;
}

static void __nvme_process_cq(struct nvme_queue *nvmeq, unsigned int *tag)
{
	u16 head, phase;

	head = nvmeq->cq_head;
	phase = nvmeq->cq_phase;

	while (nvme_cqe_valid(nvmeq, head, phase)) {
		struct nvme_completion cqe = nvmeq->cqes[head];
		struct request *req;

		if (++head == nvmeq->q_depth) {
			head = 0;
			phase = !phase;
		}

		if (tag && *tag == cqe.command_id)
			*tag = -1;

		if (unlikely(cqe.command_id >= nvmeq->q_depth)) {
			dev_warn(nvmeq->dev->ctrl.device,
				"invalid id %d completed on queue %d\n",
				cqe.command_id, le16_to_cpu(cqe.sq_id));
			continue;
		}

		/*
		 * AEN requests are special as they don't time out and can
		 * survive any kind of queue freeze and often don't respond to
		 * aborts.  We don't even bother to allocate a struct request
		 * for them but rather special case them here.
		 */
		if (unlikely(nvmeq->qid == 0 &&
				cqe.command_id >= NVME_AQ_BLKMQ_DEPTH)) {
			nvme_complete_async_event(&nvmeq->dev->ctrl, &cqe);
			continue;
		}

		req = blk_mq_tag_to_rq(*nvmeq->tags, cqe.command_id);
		if (req->cmd_type == REQ_TYPE_DRV_PRIV && req->special)
			memcpy(req->special, &cqe, sizeof(cqe));
		blk_mq_complete_request(req, le16_to_cpu(cqe.status) >> 1);

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
	if (nvme_cqe_valid(nvmeq, nvmeq->cq_head, nvmeq->cq_phase))
		return IRQ_WAKE_THREAD;
	return IRQ_NONE;
}

static int nvme_poll(struct blk_mq_hw_ctx *hctx, unsigned int tag)
{
	struct nvme_queue *nvmeq = hctx->driver_data;

	if (nvme_cqe_valid(nvmeq, nvmeq->cq_head, nvmeq->cq_phase)) {
		spin_lock_irq(&nvmeq->q_lock);
		__nvme_process_cq(nvmeq, &tag);
		spin_unlock_irq(&nvmeq->q_lock);

		if (tag == -1)
			return 1;
	}

	return 0;
}

static void nvme_pci_submit_async_event(struct nvme_ctrl *ctrl, int aer_idx)
{
	struct nvme_dev *dev = to_nvme_dev(ctrl);
	struct nvme_queue *nvmeq = dev->queues[0];
	struct nvme_command c;

	memset(&c, 0, sizeof(c));
	c.common.opcode = nvme_admin_async_event;
	c.common.command_id = NVME_AQ_BLKMQ_DEPTH + aer_idx;

	spin_lock_irq(&nvmeq->q_lock);
	__nvme_submit_cmd(nvmeq, &c);
	spin_unlock_irq(&nvmeq->q_lock);
}

static int adapter_delete_queue(struct nvme_dev *dev, u8 opcode, u16 id)
{
	struct nvme_command c;

	memset(&c, 0, sizeof(c));
	c.delete_queue.opcode = opcode;
	c.delete_queue.qid = cpu_to_le16(id);

	return nvme_submit_sync_cmd(dev->ctrl.admin_q, &c, NULL, 0);
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

	return nvme_submit_sync_cmd(dev->ctrl.admin_q, &c, NULL, 0);
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

	return nvme_submit_sync_cmd(dev->ctrl.admin_q, &c, NULL, 0);
}

static int adapter_delete_cq(struct nvme_dev *dev, u16 cqid)
{
	return adapter_delete_queue(dev, nvme_admin_delete_cq, cqid);
}

static int adapter_delete_sq(struct nvme_dev *dev, u16 sqid)
{
	return adapter_delete_queue(dev, nvme_admin_delete_sq, sqid);
}

static void abort_endio(struct request *req, int error)
{
	struct nvme_iod *iod = blk_mq_rq_to_pdu(req);
	struct nvme_queue *nvmeq = iod->nvmeq;
	u16 status = req->errors;

	dev_warn(nvmeq->dev->ctrl.device, "Abort status: 0x%x", status);
	atomic_inc(&nvmeq->dev->ctrl.abort_limit);
	blk_mq_free_request(req);
}

static enum blk_eh_timer_return nvme_timeout(struct request *req, bool reserved)
{
	struct nvme_iod *iod = blk_mq_rq_to_pdu(req);
	struct nvme_queue *nvmeq = iod->nvmeq;
	struct nvme_dev *dev = nvmeq->dev;
	struct request *abort_req;
	struct nvme_command cmd;

	/*
	 * Shutdown immediately if controller times out while starting. The
	 * reset work will see the pci device disabled when it gets the forced
	 * cancellation error. All outstanding requests are completed on
	 * shutdown, so we return BLK_EH_HANDLED.
	 */
	if (dev->ctrl.state == NVME_CTRL_RESETTING) {
		dev_warn(dev->ctrl.device,
			 "I/O %d QID %d timeout, disable controller\n",
			 req->tag, nvmeq->qid);
		nvme_dev_disable(dev, false);
		req->errors = NVME_SC_CANCELLED;
		return BLK_EH_HANDLED;
	}

	/*
 	 * Shutdown the controller immediately and schedule a reset if the
 	 * command was already aborted once before and still hasn't been
 	 * returned to the driver, or if this is the admin queue.
	 */
	if (!nvmeq->qid || iod->aborted) {
		dev_warn(dev->ctrl.device,
			 "I/O %d QID %d timeout, reset controller\n",
			 req->tag, nvmeq->qid);
		nvme_dev_disable(dev, false);
		queue_work(nvme_workq, &dev->reset_work);

		/*
		 * Mark the request as handled, since the inline shutdown
		 * forces all outstanding requests to complete.
		 */
		req->errors = NVME_SC_CANCELLED;
		return BLK_EH_HANDLED;
	}

	iod->aborted = 1;

	if (atomic_dec_return(&dev->ctrl.abort_limit) < 0) {
		atomic_inc(&dev->ctrl.abort_limit);
		return BLK_EH_RESET_TIMER;
	}

	memset(&cmd, 0, sizeof(cmd));
	cmd.abort.opcode = nvme_admin_abort_cmd;
	cmd.abort.cid = req->tag;
	cmd.abort.sqid = cpu_to_le16(nvmeq->qid);

	dev_warn(nvmeq->dev->ctrl.device,
		"I/O %d QID %d timeout, aborting\n",
		 req->tag, nvmeq->qid);

	abort_req = nvme_alloc_request(dev->ctrl.admin_q, &cmd,
			BLK_MQ_REQ_NOWAIT);
	if (IS_ERR(abort_req)) {
		atomic_inc(&dev->ctrl.abort_limit);
		return BLK_EH_RESET_TIMER;
	}

	abort_req->timeout = ADMIN_TIMEOUT;
	abort_req->end_io_data = NULL;
	blk_execute_rq_nowait(abort_req->q, NULL, abort_req, 0, abort_endio);

	/*
	 * The aborted req will be completed on receiving the abort req.
	 * We enable the timer again. If hit twice, it'll cause a device reset,
	 * as the device then is in a faulty state.
	 */
	return BLK_EH_RESET_TIMER;
}

static void nvme_cancel_io(struct request *req, void *data, bool reserved)
{
	int status;

	if (!blk_mq_request_started(req))
		return;

	dev_dbg_ratelimited(((struct nvme_dev *) data)->ctrl.device,
				"Cancelling I/O %d", req->tag);

	status = NVME_SC_ABORT_REQ;
	if (blk_queue_dying(req->q))
		status |= NVME_SC_DNR;
	blk_mq_complete_request(req, status);
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

	if (!nvmeq->qid && nvmeq->dev->ctrl.admin_q)
		blk_mq_stop_hw_queues(nvmeq->dev->ctrl.admin_q);

	irq_set_affinity_hint(vector, NULL);
	free_irq(vector, nvmeq);

	return 0;
}

static void nvme_disable_admin_queue(struct nvme_dev *dev, bool shutdown)
{
	struct nvme_queue *nvmeq = dev->queues[0];

	if (!nvmeq)
		return;
	if (nvme_suspend_queue(nvmeq))
		return;

	if (shutdown)
		nvme_shutdown_ctrl(&dev->ctrl);
	else
		nvme_disable_ctrl(&dev->ctrl, lo_hi_readq(
						dev->bar + NVME_REG_CAP));

	spin_lock_irq(&nvmeq->q_lock);
	nvme_process_cq(nvmeq);
	spin_unlock_irq(&nvmeq->q_lock);
}

static int nvme_cmb_qdepth(struct nvme_dev *dev, int nr_io_queues,
				int entry_size)
{
	int q_depth = dev->q_depth;
	unsigned q_size_aligned = roundup(q_depth * entry_size,
					  dev->ctrl.page_size);

	if (q_size_aligned * nr_io_queues > dev->cmb_size) {
		u64 mem_per_q = div_u64(dev->cmb_size, nr_io_queues);
		mem_per_q = round_down(mem_per_q, dev->ctrl.page_size);
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
		unsigned offset = (qid - 1) * roundup(SQ_SIZE(depth),
						      dev->ctrl.page_size);
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
			dev->ctrl.instance, qid);
	spin_lock_init(&nvmeq->q_lock);
	nvmeq->cq_head = 0;
	nvmeq->cq_phase = 1;
	nvmeq->q_db = &dev->dbs[qid * 2 * dev->db_stride];
	nvmeq->q_depth = depth;
	nvmeq->qid = qid;
	nvmeq->cq_vector = -1;
	dev->queues[qid] = nvmeq;
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

static struct blk_mq_ops nvme_mq_admin_ops = {
	.queue_rq	= nvme_queue_rq,
	.complete	= nvme_complete_rq,
	.map_queue	= blk_mq_map_queue,
	.init_hctx	= nvme_admin_init_hctx,
	.exit_hctx      = nvme_admin_exit_hctx,
	.init_request	= nvme_admin_init_request,
	.timeout	= nvme_timeout,
};

static struct blk_mq_ops nvme_mq_ops = {
	.queue_rq	= nvme_queue_rq,
	.complete	= nvme_complete_rq,
	.map_queue	= blk_mq_map_queue,
	.init_hctx	= nvme_init_hctx,
	.init_request	= nvme_init_request,
	.timeout	= nvme_timeout,
	.poll		= nvme_poll,
};

static void nvme_dev_remove_admin(struct nvme_dev *dev)
{
	if (dev->ctrl.admin_q && !blk_queue_dying(dev->ctrl.admin_q)) {
		/*
		 * If the controller was reset during removal, it's possible
		 * user requests may be waiting on a stopped queue. Start the
		 * queue to flush these to completion.
		 */
		blk_mq_start_stopped_hw_queues(dev->ctrl.admin_q, true);
		blk_cleanup_queue(dev->ctrl.admin_q);
		blk_mq_free_tag_set(&dev->admin_tagset);
	}
}

static int nvme_alloc_admin_tags(struct nvme_dev *dev)
{
	if (!dev->ctrl.admin_q) {
		dev->admin_tagset.ops = &nvme_mq_admin_ops;
		dev->admin_tagset.nr_hw_queues = 1;

		/*
		 * Subtract one to leave an empty queue entry for 'Full Queue'
		 * condition. See NVM-Express 1.2 specification, section 4.1.2.
		 */
		dev->admin_tagset.queue_depth = NVME_AQ_BLKMQ_DEPTH - 1;
		dev->admin_tagset.timeout = ADMIN_TIMEOUT;
		dev->admin_tagset.numa_node = dev_to_node(dev->dev);
		dev->admin_tagset.cmd_size = nvme_cmd_size(dev);
		dev->admin_tagset.driver_data = dev;

		if (blk_mq_alloc_tag_set(&dev->admin_tagset))
			return -ENOMEM;

		dev->ctrl.admin_q = blk_mq_init_queue(&dev->admin_tagset);
		if (IS_ERR(dev->ctrl.admin_q)) {
			blk_mq_free_tag_set(&dev->admin_tagset);
			return -ENOMEM;
		}
		if (!blk_get_queue(dev->ctrl.admin_q)) {
			nvme_dev_remove_admin(dev);
			dev->ctrl.admin_q = NULL;
			return -ENODEV;
		}
	} else
		blk_mq_start_stopped_hw_queues(dev->ctrl.admin_q, true);

	return 0;
}

static int nvme_configure_admin_queue(struct nvme_dev *dev)
{
	int result;
	u32 aqa;
	u64 cap = lo_hi_readq(dev->bar + NVME_REG_CAP);
	struct nvme_queue *nvmeq;

	dev->subsystem = readl(dev->bar + NVME_REG_VS) >= NVME_VS(1, 1) ?
						NVME_CAP_NSSRC(cap) : 0;

	if (dev->subsystem &&
	    (readl(dev->bar + NVME_REG_CSTS) & NVME_CSTS_NSSRO))
		writel(NVME_CSTS_NSSRO, dev->bar + NVME_REG_CSTS);

	result = nvme_disable_ctrl(&dev->ctrl, cap);
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

	writel(aqa, dev->bar + NVME_REG_AQA);
	lo_hi_writeq(nvmeq->sq_dma_addr, dev->bar + NVME_REG_ASQ);
	lo_hi_writeq(nvmeq->cq_dma_addr, dev->bar + NVME_REG_ACQ);

	result = nvme_enable_ctrl(&dev->ctrl, cap);
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

static bool nvme_should_reset(struct nvme_dev *dev, u32 csts)
{

	/* If true, indicates loss of adapter communication, possibly by a
	 * NVMe Subsystem reset.
	 */
	bool nssro = dev->subsystem && (csts & NVME_CSTS_NSSRO);

	/* If there is a reset ongoing, we shouldn't reset again. */
	if (work_busy(&dev->reset_work))
		return false;

	/* We shouldn't reset unless the controller is on fatal error state
	 * _or_ if we lost the communication with it.
	 */
	if (!(csts & NVME_CSTS_CFS) && !nssro)
		return false;

	/* If PCI error recovery process is happening, we cannot reset or
	 * the recovery mechanism will surely fail.
	 */
	if (pci_channel_offline(to_pci_dev(dev->dev)))
		return false;

	return true;
}

static void nvme_watchdog_timer(unsigned long data)
{
	struct nvme_dev *dev = (struct nvme_dev *)data;
	u32 csts = readl(dev->bar + NVME_REG_CSTS);

	/* Skip controllers under certain specific conditions. */
	if (nvme_should_reset(dev, csts)) {
		if (queue_work(nvme_workq, &dev->reset_work))
			dev_warn(dev->dev,
				"Failed status: 0x%x, reset controller.\n",
				csts);
		return;
	}

	mod_timer(&dev->watchdog_timer, round_jiffies(jiffies + HZ));
}

static int nvme_create_io_queues(struct nvme_dev *dev)
{
	unsigned i, max;
	int ret = 0;

	for (i = dev->queue_count; i <= dev->max_qid; i++) {
		if (!nvme_alloc_queue(dev, i, dev->q_depth)) {
			ret = -ENOMEM;
			break;
		}
	}

	max = min(dev->max_qid, dev->queue_count - 1);
	for (i = dev->online_queues; i <= max; i++) {
		ret = nvme_create_queue(dev->queues[i], i);
		if (ret) {
			nvme_free_queues(dev, i);
			break;
		}
	}

	/*
	 * Ignore failing Create SQ/CQ commands, we can continue with less
	 * than the desired aount of queues, and even a controller without
	 * I/O queues an still be used to issue admin commands.  This might
	 * be useful to upgrade a buggy firmware for example.
	 */
	return ret >= 0 ? 0 : ret;
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

	dev->cmbsz = readl(dev->bar + NVME_REG_CMBSZ);
	if (!(NVME_CMB_SZ(dev->cmbsz)))
		return NULL;

	cmbloc = readl(dev->bar + NVME_REG_CMBLOC);

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

	nr_io_queues = num_online_cpus();
	result = nvme_set_queue_count(&dev->ctrl, &nr_io_queues);
	if (result < 0)
		return result;

	/*
	 * Degraded controllers might return an error when setting the queue
	 * count.  We still want to be able to bring them online and offer
	 * access to the admin queue, as that might be only way to fix them up.
	 */
	if (result > 0) {
		dev_err(dev->ctrl.device,
			"Could not set queue count (%d)\n", result);
		return 0;
	}

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
		dev->dbs = dev->bar + 4096;
		adminq->q_db = dev->dbs;
	}

	/* Deregister the admin queue's interrupt */
	free_irq(dev->entry[0].vector, adminq);

	/*
	 * If we enable msix early due to not intx, disable it again before
	 * setting up the full range we need.
	 */
	if (pdev->msi_enabled)
		pci_disable_msi(pdev);
	else if (pdev->msix_enabled)
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
	return nvme_create_io_queues(dev);

 free_queues:
	nvme_free_queues(dev, 1);
	return result;
}

static void nvme_pci_post_scan(struct nvme_ctrl *ctrl)
{
	struct nvme_dev *dev = to_nvme_dev(ctrl);
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

static void nvme_del_queue_end(struct request *req, int error)
{
	struct nvme_queue *nvmeq = req->end_io_data;

	blk_mq_free_request(req);
	complete(&nvmeq->dev->ioq_wait);
}

static void nvme_del_cq_end(struct request *req, int error)
{
	struct nvme_queue *nvmeq = req->end_io_data;

	if (!error) {
		unsigned long flags;

		/*
		 * We might be called with the AQ q_lock held
		 * and the I/O queue q_lock should always
		 * nest inside the AQ one.
		 */
		spin_lock_irqsave_nested(&nvmeq->q_lock, flags,
					SINGLE_DEPTH_NESTING);
		nvme_process_cq(nvmeq);
		spin_unlock_irqrestore(&nvmeq->q_lock, flags);
	}

	nvme_del_queue_end(req, error);
}

static int nvme_delete_queue(struct nvme_queue *nvmeq, u8 opcode)
{
	struct request_queue *q = nvmeq->dev->ctrl.admin_q;
	struct request *req;
	struct nvme_command cmd;

	memset(&cmd, 0, sizeof(cmd));
	cmd.delete_queue.opcode = opcode;
	cmd.delete_queue.qid = cpu_to_le16(nvmeq->qid);

	req = nvme_alloc_request(q, &cmd, BLK_MQ_REQ_NOWAIT);
	if (IS_ERR(req))
		return PTR_ERR(req);

	req->timeout = ADMIN_TIMEOUT;
	req->end_io_data = nvmeq;

	blk_execute_rq_nowait(q, NULL, req, false,
			opcode == nvme_admin_delete_cq ?
				nvme_del_cq_end : nvme_del_queue_end);
	return 0;
}

static void nvme_disable_io_queues(struct nvme_dev *dev)
{
	int pass, queues = dev->online_queues - 1;
	unsigned long timeout;
	u8 opcode = nvme_admin_delete_sq;

	for (pass = 0; pass < 2; pass++) {
		int sent = 0, i = queues;

		reinit_completion(&dev->ioq_wait);
 retry:
		timeout = ADMIN_TIMEOUT;
		for (; i > 0; i--) {
			struct nvme_queue *nvmeq = dev->queues[i];

			if (!pass)
				nvme_suspend_queue(nvmeq);
			if (nvme_delete_queue(nvmeq, opcode))
				break;
			++sent;
		}
		while (sent--) {
			timeout = wait_for_completion_io_timeout(&dev->ioq_wait, timeout);
			if (timeout == 0)
				return;
			if (i)
				goto retry;
		}
		opcode = nvme_admin_delete_cq;
	}
}

/*
 * Return: error value if an error occurred setting up the queues or calling
 * Identify Device.  0 if these succeeded, even if adding some of the
 * namespaces failed.  At the moment, these failures are silent.  TBD which
 * failures should be reported.
 */
static int nvme_dev_add(struct nvme_dev *dev)
{
	if (!dev->ctrl.tagset) {
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
		dev->ctrl.tagset = &dev->tagset;
	} else {
		blk_mq_update_nr_hw_queues(&dev->tagset, dev->online_queues - 1);

		/* Free previously allocated queues that are no longer usable */
		nvme_free_queues(dev, dev->online_queues);
	}

	return 0;
}

static int nvme_pci_enable(struct nvme_dev *dev)
{
	u64 cap;
	int result = -ENOMEM;
	struct pci_dev *pdev = to_pci_dev(dev->dev);

	if (pci_enable_device_mem(pdev))
		return result;

	pci_set_master(pdev);

	if (dma_set_mask_and_coherent(dev->dev, DMA_BIT_MASK(64)) &&
	    dma_set_mask_and_coherent(dev->dev, DMA_BIT_MASK(32)))
		goto disable;

	if (readl(dev->bar + NVME_REG_CSTS) == -1) {
		result = -ENODEV;
		goto disable;
	}

	/*
	 * Some devices and/or platforms don't advertise or work with INTx
	 * interrupts. Pre-enable a single MSIX or MSI vec for setup. We'll
	 * adjust this later.
	 */
	if (pci_enable_msix(pdev, dev->entry, 1)) {
		pci_enable_msi(pdev);
		dev->entry[0].vector = pdev->irq;
	}

	if (!dev->entry[0].vector) {
		result = -ENODEV;
		goto disable;
	}

	cap = lo_hi_readq(dev->bar + NVME_REG_CAP);

	dev->q_depth = min_t(int, NVME_CAP_MQES(cap) + 1, NVME_Q_DEPTH);
	dev->db_stride = 1 << NVME_CAP_STRIDE(cap);
	dev->dbs = dev->bar + 4096;

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

	if (readl(dev->bar + NVME_REG_VS) >= NVME_VS(1, 2))
		dev->cmb = nvme_map_cmb(dev);

	pci_enable_pcie_error_reporting(pdev);
	pci_save_state(pdev);
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

	if (pci_is_enabled(pdev)) {
		pci_disable_pcie_error_reporting(pdev);
		pci_disable_device(pdev);
	}
}

static void nvme_dev_disable(struct nvme_dev *dev, bool shutdown)
{
	int i;
	u32 csts = -1;

	del_timer_sync(&dev->watchdog_timer);

	mutex_lock(&dev->shutdown_lock);
	if (pci_is_enabled(to_pci_dev(dev->dev))) {
		nvme_stop_queues(&dev->ctrl);
		csts = readl(dev->bar + NVME_REG_CSTS);
	}
	if (csts & NVME_CSTS_CFS || !(csts & NVME_CSTS_RDY)) {
		for (i = dev->queue_count - 1; i >= 0; i--) {
			struct nvme_queue *nvmeq = dev->queues[i];
			nvme_suspend_queue(nvmeq);
		}
	} else {
		nvme_disable_io_queues(dev);
		nvme_disable_admin_queue(dev, shutdown);
	}
	nvme_pci_disable(dev);

	blk_mq_tagset_busy_iter(&dev->tagset, nvme_cancel_io, dev);
	blk_mq_tagset_busy_iter(&dev->admin_tagset, nvme_cancel_io, dev);
	mutex_unlock(&dev->shutdown_lock);
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

static void nvme_pci_free_ctrl(struct nvme_ctrl *ctrl)
{
	struct nvme_dev *dev = to_nvme_dev(ctrl);

	put_device(dev->dev);
	if (dev->tagset.tags)
		blk_mq_free_tag_set(&dev->tagset);
	if (dev->ctrl.admin_q)
		blk_put_queue(dev->ctrl.admin_q);
	kfree(dev->queues);
	kfree(dev->entry);
	kfree(dev);
}

static void nvme_remove_dead_ctrl(struct nvme_dev *dev, int status)
{
	dev_warn(dev->ctrl.device, "Removing after probe failure status: %d\n", status);

	kref_get(&dev->ctrl.kref);
	nvme_dev_disable(dev, false);
	if (!schedule_work(&dev->remove_work))
		nvme_put_ctrl(&dev->ctrl);
}

static void nvme_reset_work(struct work_struct *work)
{
	struct nvme_dev *dev = container_of(work, struct nvme_dev, reset_work);
	int result = -ENODEV;

	if (WARN_ON(dev->ctrl.state == NVME_CTRL_RESETTING))
		goto out;

	/*
	 * If we're called to reset a live controller first shut it down before
	 * moving on.
	 */
	if (dev->ctrl.ctrl_config & NVME_CC_ENABLE)
		nvme_dev_disable(dev, false);

	if (!nvme_change_ctrl_state(&dev->ctrl, NVME_CTRL_RESETTING))
		goto out;

	result = nvme_pci_enable(dev);
	if (result)
		goto out;

	result = nvme_configure_admin_queue(dev);
	if (result)
		goto out;

	nvme_init_queue(dev->queues[0], 0);
	result = nvme_alloc_admin_tags(dev);
	if (result)
		goto out;

	result = nvme_init_identify(&dev->ctrl);
	if (result)
		goto out;

	result = nvme_setup_io_queues(dev);
	if (result)
		goto out;

	/*
	 * A controller that can not execute IO typically requires user
	 * intervention to correct. For such degraded controllers, the driver
	 * should not submit commands the user did not request, so skip
	 * registering for asynchronous event notification on this condition.
	 */
	if (dev->online_queues > 1)
		nvme_queue_async_events(&dev->ctrl);

	mod_timer(&dev->watchdog_timer, round_jiffies(jiffies + HZ));

	/*
	 * Keep the controller around but remove all namespaces if we don't have
	 * any working I/O queue.
	 */
	if (dev->online_queues < 2) {
		dev_warn(dev->ctrl.device, "IO queues not created\n");
		nvme_kill_queues(&dev->ctrl);
		nvme_remove_namespaces(&dev->ctrl);
	} else {
		nvme_start_queues(&dev->ctrl);
		nvme_dev_add(dev);
	}

	if (!nvme_change_ctrl_state(&dev->ctrl, NVME_CTRL_LIVE)) {
		dev_warn(dev->ctrl.device, "failed to mark controller live\n");
		goto out;
	}

	if (dev->online_queues > 1)
		nvme_queue_scan(&dev->ctrl);
	return;

 out:
	nvme_remove_dead_ctrl(dev, result);
}

static void nvme_remove_dead_ctrl_work(struct work_struct *work)
{
	struct nvme_dev *dev = container_of(work, struct nvme_dev, remove_work);
	struct pci_dev *pdev = to_pci_dev(dev->dev);

	nvme_kill_queues(&dev->ctrl);
	if (pci_get_drvdata(pdev))
		device_release_driver(&pdev->dev);
	nvme_put_ctrl(&dev->ctrl);
}

static int nvme_reset(struct nvme_dev *dev)
{
	if (!dev->ctrl.admin_q || blk_queue_dying(dev->ctrl.admin_q))
		return -ENODEV;

	if (!queue_work(nvme_workq, &dev->reset_work))
		return -EBUSY;

	flush_work(&dev->reset_work);
	return 0;
}

static int nvme_pci_reg_read32(struct nvme_ctrl *ctrl, u32 off, u32 *val)
{
	*val = readl(to_nvme_dev(ctrl)->bar + off);
	return 0;
}

static int nvme_pci_reg_write32(struct nvme_ctrl *ctrl, u32 off, u32 val)
{
	writel(val, to_nvme_dev(ctrl)->bar + off);
	return 0;
}

static int nvme_pci_reg_read64(struct nvme_ctrl *ctrl, u32 off, u64 *val)
{
	*val = readq(to_nvme_dev(ctrl)->bar + off);
	return 0;
}

static int nvme_pci_reset_ctrl(struct nvme_ctrl *ctrl)
{
	return nvme_reset(to_nvme_dev(ctrl));
}

static const struct nvme_ctrl_ops nvme_pci_ctrl_ops = {
	.module			= THIS_MODULE,
	.reg_read32		= nvme_pci_reg_read32,
	.reg_write32		= nvme_pci_reg_write32,
	.reg_read64		= nvme_pci_reg_read64,
	.reset_ctrl		= nvme_pci_reset_ctrl,
	.free_ctrl		= nvme_pci_free_ctrl,
	.post_scan		= nvme_pci_post_scan,
	.submit_async_event	= nvme_pci_submit_async_event,
};

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

	dev->dev = get_device(&pdev->dev);
	pci_set_drvdata(pdev, dev);

	result = nvme_dev_map(dev);
	if (result)
		goto free;

	INIT_WORK(&dev->reset_work, nvme_reset_work);
	INIT_WORK(&dev->remove_work, nvme_remove_dead_ctrl_work);
	setup_timer(&dev->watchdog_timer, nvme_watchdog_timer,
		(unsigned long)dev);
	mutex_init(&dev->shutdown_lock);
	init_completion(&dev->ioq_wait);

	result = nvme_setup_prp_pools(dev);
	if (result)
		goto put_pci;

	result = nvme_init_ctrl(&dev->ctrl, &pdev->dev, &nvme_pci_ctrl_ops,
			id->driver_data);
	if (result)
		goto release_pools;

	dev_info(dev->ctrl.device, "pci function %s\n", dev_name(&pdev->dev));

	queue_work(nvme_workq, &dev->reset_work);
	return 0;

 release_pools:
	nvme_release_prp_pools(dev);
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
		nvme_dev_disable(dev, false);
	else
		queue_work(nvme_workq, &dev->reset_work);
}

static void nvme_shutdown(struct pci_dev *pdev)
{
	struct nvme_dev *dev = pci_get_drvdata(pdev);
	nvme_dev_disable(dev, true);
}

/*
 * The driver's remove may be called on a device in a partially initialized
 * state. This function must not have any dependencies on the device state in
 * order to proceed.
 */
static void nvme_remove(struct pci_dev *pdev)
{
	struct nvme_dev *dev = pci_get_drvdata(pdev);

	nvme_change_ctrl_state(&dev->ctrl, NVME_CTRL_DELETING);

	pci_set_drvdata(pdev, NULL);
	flush_work(&dev->reset_work);
	nvme_uninit_ctrl(&dev->ctrl);
	nvme_dev_disable(dev, true);
	nvme_dev_remove_admin(dev);
	nvme_free_queues(dev, 0);
	nvme_release_cmb(dev);
	nvme_release_prp_pools(dev);
	nvme_dev_unmap(dev);
	nvme_put_ctrl(&dev->ctrl);
}

#ifdef CONFIG_PM_SLEEP
static int nvme_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct nvme_dev *ndev = pci_get_drvdata(pdev);

	nvme_dev_disable(ndev, true);
	return 0;
}

static int nvme_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct nvme_dev *ndev = pci_get_drvdata(pdev);

	queue_work(nvme_workq, &ndev->reset_work);
	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(nvme_dev_pm_ops, nvme_suspend, nvme_resume);

static pci_ers_result_t nvme_error_detected(struct pci_dev *pdev,
						pci_channel_state_t state)
{
	struct nvme_dev *dev = pci_get_drvdata(pdev);

	/*
	 * A frozen channel requires a reset. When detected, this method will
	 * shutdown the controller to quiesce. The controller will be restarted
	 * after the slot reset through driver's slot_reset callback.
	 */
	switch (state) {
	case pci_channel_io_normal:
		return PCI_ERS_RESULT_CAN_RECOVER;
	case pci_channel_io_frozen:
		dev_warn(dev->ctrl.device,
			"frozen state error detected, reset controller\n");
		nvme_dev_disable(dev, false);
		return PCI_ERS_RESULT_NEED_RESET;
	case pci_channel_io_perm_failure:
		dev_warn(dev->ctrl.device,
			"failure state error detected, request disconnect\n");
		return PCI_ERS_RESULT_DISCONNECT;
	}
	return PCI_ERS_RESULT_NEED_RESET;
}

static pci_ers_result_t nvme_slot_reset(struct pci_dev *pdev)
{
	struct nvme_dev *dev = pci_get_drvdata(pdev);

	dev_info(dev->ctrl.device, "restart after slot reset\n");
	pci_restore_state(pdev);
	queue_work(nvme_workq, &dev->reset_work);
	return PCI_ERS_RESULT_RECOVERED;
}

static void nvme_error_resume(struct pci_dev *pdev)
{
	pci_cleanup_aer_uncorrect_error_status(pdev);
}

static const struct pci_error_handlers nvme_err_handler = {
	.error_detected	= nvme_error_detected,
	.slot_reset	= nvme_slot_reset,
	.resume		= nvme_error_resume,
	.reset_notify	= nvme_reset_notify,
};

/* Move to pci_ids.h later */
#define PCI_CLASS_STORAGE_EXPRESS	0x010802

static const struct pci_device_id nvme_id_table[] = {
	{ PCI_VDEVICE(INTEL, 0x0953),
		.driver_data = NVME_QUIRK_STRIPE_SIZE |
				NVME_QUIRK_DISCARD_ZEROES, },
	{ PCI_VDEVICE(INTEL, 0x5845),	/* Qemu emulated controller */
		.driver_data = NVME_QUIRK_IDENTIFY_CNS, },
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

	nvme_workq = alloc_workqueue("nvme", WQ_UNBOUND | WQ_MEM_RECLAIM, 0);
	if (!nvme_workq)
		return -ENOMEM;

	result = pci_register_driver(&nvme_driver);
	if (result)
		destroy_workqueue(nvme_workq);
	return result;
}

static void __exit nvme_exit(void)
{
	pci_unregister_driver(&nvme_driver);
	destroy_workqueue(nvme_workq);
	_nvme_check_size();
}

MODULE_AUTHOR("Matthew Wilcox <willy@linux.intel.com>");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0");
module_init(nvme_init);
module_exit(nvme_exit);
