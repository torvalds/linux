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
#include <linux/blkdev.h>
#include <linux/blk-mq.h>
#include <linux/blk-mq-pci.h>
#include <linux/dmi.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/once.h>
#include <linux/pci.h>
#include <linux/t10-pi.h>
#include <linux/types.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/sed-opal.h>

#include "nvme.h"

#define SQ_SIZE(depth)		(depth * sizeof(struct nvme_command))
#define CQ_SIZE(depth)		(depth * sizeof(struct nvme_completion))

#define SGES_PER_PAGE	(PAGE_SIZE / sizeof(struct nvme_sgl_desc))

static int use_threaded_interrupts;
module_param(use_threaded_interrupts, int, 0);

static bool use_cmb_sqes = true;
module_param(use_cmb_sqes, bool, 0644);
MODULE_PARM_DESC(use_cmb_sqes, "use controller's memory buffer for I/O SQes");

static unsigned int max_host_mem_size_mb = 128;
module_param(max_host_mem_size_mb, uint, 0444);
MODULE_PARM_DESC(max_host_mem_size_mb,
	"Maximum Host Memory Buffer (HMB) size per controller (in MiB)");

static unsigned int sgl_threshold = SZ_32K;
module_param(sgl_threshold, uint, 0644);
MODULE_PARM_DESC(sgl_threshold,
		"Use SGLs when average request segment size is larger or equal to "
		"this size. Use 0 to disable SGLs.");

static int io_queue_depth_set(const char *val, const struct kernel_param *kp);
static const struct kernel_param_ops io_queue_depth_ops = {
	.set = io_queue_depth_set,
	.get = param_get_int,
};

static int io_queue_depth = 1024;
module_param_cb(io_queue_depth, &io_queue_depth_ops, &io_queue_depth, 0644);
MODULE_PARM_DESC(io_queue_depth, "set io queue depth, should >= 2");

struct nvme_dev;
struct nvme_queue;

static void nvme_process_cq(struct nvme_queue *nvmeq);
static void nvme_dev_disable(struct nvme_dev *dev, bool shutdown);

/*
 * Represents an NVM Express device.  Each nvme_dev is a PCI function.
 */
struct nvme_dev {
	struct nvme_queue *queues;
	struct blk_mq_tag_set tagset;
	struct blk_mq_tag_set admin_tagset;
	u32 __iomem *dbs;
	struct device *dev;
	struct dma_pool *prp_page_pool;
	struct dma_pool *prp_small_pool;
	unsigned online_queues;
	unsigned max_qid;
	int q_depth;
	u32 db_stride;
	void __iomem *bar;
	unsigned long bar_mapped_size;
	struct work_struct remove_work;
	struct mutex shutdown_lock;
	bool subsystem;
	void __iomem *cmb;
	pci_bus_addr_t cmb_bus_addr;
	u64 cmb_size;
	u32 cmbsz;
	u32 cmbloc;
	struct nvme_ctrl ctrl;
	struct completion ioq_wait;

	/* shadow doorbell buffer support: */
	u32 *dbbuf_dbs;
	dma_addr_t dbbuf_dbs_dma_addr;
	u32 *dbbuf_eis;
	dma_addr_t dbbuf_eis_dma_addr;

	/* host memory buffer support: */
	u64 host_mem_size;
	u32 nr_host_mem_descs;
	dma_addr_t host_mem_descs_dma;
	struct nvme_host_mem_buf_desc *host_mem_descs;
	void **host_mem_desc_bufs;
};

static int io_queue_depth_set(const char *val, const struct kernel_param *kp)
{
	int n = 0, ret;

	ret = kstrtoint(val, 10, &n);
	if (ret != 0 || n < 2)
		return -EINVAL;

	return param_set_int(val, kp);
}

static inline unsigned int sq_idx(unsigned int qid, u32 stride)
{
	return qid * 2 * stride;
}

static inline unsigned int cq_idx(unsigned int qid, u32 stride)
{
	return (qid * 2 + 1) * stride;
}

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
	u32 *dbbuf_sq_db;
	u32 *dbbuf_cq_db;
	u32 *dbbuf_sq_ei;
	u32 *dbbuf_cq_ei;
};

/*
 * The nvme_iod describes the data in an I/O, including the list of PRP
 * entries.  You can't see it in this data structure because C doesn't let
 * me express that.  Use nvme_init_iod to ensure there's enough space
 * allocated to store the PRP list.
 */
struct nvme_iod {
	struct nvme_request req;
	struct nvme_queue *nvmeq;
	bool use_sgl;
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
	BUILD_BUG_ON(sizeof(struct nvme_id_ctrl) != NVME_IDENTIFY_DATA_SIZE);
	BUILD_BUG_ON(sizeof(struct nvme_id_ns) != NVME_IDENTIFY_DATA_SIZE);
	BUILD_BUG_ON(sizeof(struct nvme_lba_range_type) != 64);
	BUILD_BUG_ON(sizeof(struct nvme_smart_log) != 512);
	BUILD_BUG_ON(sizeof(struct nvme_dbbuf) != 64);
}

static inline unsigned int nvme_dbbuf_size(u32 stride)
{
	return ((num_possible_cpus() + 1) * 8 * stride);
}

static int nvme_dbbuf_dma_alloc(struct nvme_dev *dev)
{
	unsigned int mem_size = nvme_dbbuf_size(dev->db_stride);

	if (dev->dbbuf_dbs)
		return 0;

	dev->dbbuf_dbs = dma_alloc_coherent(dev->dev, mem_size,
					    &dev->dbbuf_dbs_dma_addr,
					    GFP_KERNEL);
	if (!dev->dbbuf_dbs)
		return -ENOMEM;
	dev->dbbuf_eis = dma_alloc_coherent(dev->dev, mem_size,
					    &dev->dbbuf_eis_dma_addr,
					    GFP_KERNEL);
	if (!dev->dbbuf_eis) {
		dma_free_coherent(dev->dev, mem_size,
				  dev->dbbuf_dbs, dev->dbbuf_dbs_dma_addr);
		dev->dbbuf_dbs = NULL;
		return -ENOMEM;
	}

	return 0;
}

static void nvme_dbbuf_dma_free(struct nvme_dev *dev)
{
	unsigned int mem_size = nvme_dbbuf_size(dev->db_stride);

	if (dev->dbbuf_dbs) {
		dma_free_coherent(dev->dev, mem_size,
				  dev->dbbuf_dbs, dev->dbbuf_dbs_dma_addr);
		dev->dbbuf_dbs = NULL;
	}
	if (dev->dbbuf_eis) {
		dma_free_coherent(dev->dev, mem_size,
				  dev->dbbuf_eis, dev->dbbuf_eis_dma_addr);
		dev->dbbuf_eis = NULL;
	}
}

static void nvme_dbbuf_init(struct nvme_dev *dev,
			    struct nvme_queue *nvmeq, int qid)
{
	if (!dev->dbbuf_dbs || !qid)
		return;

	nvmeq->dbbuf_sq_db = &dev->dbbuf_dbs[sq_idx(qid, dev->db_stride)];
	nvmeq->dbbuf_cq_db = &dev->dbbuf_dbs[cq_idx(qid, dev->db_stride)];
	nvmeq->dbbuf_sq_ei = &dev->dbbuf_eis[sq_idx(qid, dev->db_stride)];
	nvmeq->dbbuf_cq_ei = &dev->dbbuf_eis[cq_idx(qid, dev->db_stride)];
}

static void nvme_dbbuf_set(struct nvme_dev *dev)
{
	struct nvme_command c;

	if (!dev->dbbuf_dbs)
		return;

	memset(&c, 0, sizeof(c));
	c.dbbuf.opcode = nvme_admin_dbbuf;
	c.dbbuf.prp1 = cpu_to_le64(dev->dbbuf_dbs_dma_addr);
	c.dbbuf.prp2 = cpu_to_le64(dev->dbbuf_eis_dma_addr);

	if (nvme_submit_sync_cmd(dev->ctrl.admin_q, &c, NULL, 0)) {
		dev_warn(dev->ctrl.device, "unable to set dbbuf\n");
		/* Free memory and continue on */
		nvme_dbbuf_dma_free(dev);
	}
}

static inline int nvme_dbbuf_need_event(u16 event_idx, u16 new_idx, u16 old)
{
	return (u16)(new_idx - event_idx - 1) < (u16)(new_idx - old);
}

/* Update dbbuf and return true if an MMIO is required */
static bool nvme_dbbuf_update_and_check_event(u16 value, u32 *dbbuf_db,
					      volatile u32 *dbbuf_ei)
{
	if (dbbuf_db) {
		u16 old_value;

		/*
		 * Ensure that the queue is written before updating
		 * the doorbell in memory
		 */
		wmb();

		old_value = *dbbuf_db;
		*dbbuf_db = value;

		if (!nvme_dbbuf_need_event(*dbbuf_ei, value, old_value))
			return false;
	}

	return true;
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

/*
 * Calculates the number of pages needed for the SGL segments. For example a 4k
 * page can accommodate 256 SGL descriptors.
 */
static int nvme_pci_npages_sgl(unsigned int num_seg)
{
	return DIV_ROUND_UP(num_seg * sizeof(struct nvme_sgl_desc), PAGE_SIZE);
}

static unsigned int nvme_pci_iod_alloc_size(struct nvme_dev *dev,
		unsigned int size, unsigned int nseg, bool use_sgl)
{
	size_t alloc_size;

	if (use_sgl)
		alloc_size = sizeof(__le64 *) * nvme_pci_npages_sgl(nseg);
	else
		alloc_size = sizeof(__le64 *) * nvme_npages(size, dev);

	return alloc_size + sizeof(struct scatterlist) * nseg;
}

static unsigned int nvme_pci_cmd_size(struct nvme_dev *dev, bool use_sgl)
{
	unsigned int alloc_size = nvme_pci_iod_alloc_size(dev,
				    NVME_INT_BYTES(dev), NVME_INT_PAGES,
				    use_sgl);

	return sizeof(struct nvme_iod) + alloc_size;
}

static int nvme_admin_init_hctx(struct blk_mq_hw_ctx *hctx, void *data,
				unsigned int hctx_idx)
{
	struct nvme_dev *dev = data;
	struct nvme_queue *nvmeq = &dev->queues[0];

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

static int nvme_init_hctx(struct blk_mq_hw_ctx *hctx, void *data,
			  unsigned int hctx_idx)
{
	struct nvme_dev *dev = data;
	struct nvme_queue *nvmeq = &dev->queues[hctx_idx + 1];

	if (!nvmeq->tags)
		nvmeq->tags = &dev->tagset.tags[hctx_idx];

	WARN_ON(dev->tagset.tags[hctx_idx] != hctx->tags);
	hctx->driver_data = nvmeq;
	return 0;
}

static int nvme_init_request(struct blk_mq_tag_set *set, struct request *req,
		unsigned int hctx_idx, unsigned int numa_node)
{
	struct nvme_dev *dev = set->driver_data;
	struct nvme_iod *iod = blk_mq_rq_to_pdu(req);
	int queue_idx = (set == &dev->tagset) ? hctx_idx + 1 : 0;
	struct nvme_queue *nvmeq = &dev->queues[queue_idx];

	BUG_ON(!nvmeq);
	iod->nvmeq = nvmeq;
	return 0;
}

static int nvme_pci_map_queues(struct blk_mq_tag_set *set)
{
	struct nvme_dev *dev = set->driver_data;

	return blk_mq_pci_map_queues(set, to_pci_dev(dev->dev));
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
	if (nvme_dbbuf_update_and_check_event(tail, nvmeq->dbbuf_sq_db,
					      nvmeq->dbbuf_sq_ei))
		writel(tail, nvmeq->q_db);
	nvmeq->sq_tail = tail;
}

static void **nvme_pci_iod_list(struct request *req)
{
	struct nvme_iod *iod = blk_mq_rq_to_pdu(req);
	return (void **)(iod->sg + blk_rq_nr_phys_segments(req));
}

static inline bool nvme_pci_use_sgls(struct nvme_dev *dev, struct request *req)
{
	struct nvme_iod *iod = blk_mq_rq_to_pdu(req);
	int nseg = blk_rq_nr_phys_segments(req);
	unsigned int avg_seg_size;

	if (nseg == 0)
		return false;

	avg_seg_size = DIV_ROUND_UP(blk_rq_payload_bytes(req), nseg);

	if (!(dev->ctrl.sgls & ((1 << 0) | (1 << 1))))
		return false;
	if (!iod->nvmeq->qid)
		return false;
	if (!sgl_threshold || avg_seg_size < sgl_threshold)
		return false;
	return true;
}

static blk_status_t nvme_init_iod(struct request *rq, struct nvme_dev *dev)
{
	struct nvme_iod *iod = blk_mq_rq_to_pdu(rq);
	int nseg = blk_rq_nr_phys_segments(rq);
	unsigned int size = blk_rq_payload_bytes(rq);

	iod->use_sgl = nvme_pci_use_sgls(dev, rq);

	if (nseg > NVME_INT_PAGES || size > NVME_INT_BYTES(dev)) {
		size_t alloc_size = nvme_pci_iod_alloc_size(dev, size, nseg,
				iod->use_sgl);

		iod->sg = kmalloc(alloc_size, GFP_ATOMIC);
		if (!iod->sg)
			return BLK_STS_RESOURCE;
	} else {
		iod->sg = iod->inline_sg;
	}

	iod->aborted = 0;
	iod->npages = -1;
	iod->nents = 0;
	iod->length = size;

	return BLK_STS_OK;
}

static void nvme_free_iod(struct nvme_dev *dev, struct request *req)
{
	struct nvme_iod *iod = blk_mq_rq_to_pdu(req);
	const int last_prp = dev->ctrl.page_size / sizeof(__le64) - 1;
	dma_addr_t dma_addr = iod->first_dma, next_dma_addr;

	int i;

	if (iod->npages == 0)
		dma_pool_free(dev->prp_small_pool, nvme_pci_iod_list(req)[0],
			dma_addr);

	for (i = 0; i < iod->npages; i++) {
		void *addr = nvme_pci_iod_list(req)[i];

		if (iod->use_sgl) {
			struct nvme_sgl_desc *sg_list = addr;

			next_dma_addr =
			    le64_to_cpu((sg_list[SGES_PER_PAGE - 1]).addr);
		} else {
			__le64 *prp_list = addr;

			next_dma_addr = le64_to_cpu(prp_list[last_prp]);
		}

		dma_pool_free(dev->prp_page_pool, addr, dma_addr);
		dma_addr = next_dma_addr;
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

static void nvme_print_sgl(struct scatterlist *sgl, int nents)
{
	int i;
	struct scatterlist *sg;

	for_each_sg(sgl, sg, nents, i) {
		dma_addr_t phys = sg_phys(sg);
		pr_warn("sg[%d] phys_addr:%pad offset:%d length:%d "
			"dma_address:%pad dma_length:%d\n",
			i, &phys, sg->offset, sg->length, &sg_dma_address(sg),
			sg_dma_len(sg));
	}
}

static blk_status_t nvme_pci_setup_prps(struct nvme_dev *dev,
		struct request *req, struct nvme_rw_command *cmnd)
{
	struct nvme_iod *iod = blk_mq_rq_to_pdu(req);
	struct dma_pool *pool;
	int length = blk_rq_payload_bytes(req);
	struct scatterlist *sg = iod->sg;
	int dma_len = sg_dma_len(sg);
	u64 dma_addr = sg_dma_address(sg);
	u32 page_size = dev->ctrl.page_size;
	int offset = dma_addr & (page_size - 1);
	__le64 *prp_list;
	void **list = nvme_pci_iod_list(req);
	dma_addr_t prp_dma;
	int nprps, i;

	length -= (page_size - offset);
	if (length <= 0) {
		iod->first_dma = 0;
		goto done;
	}

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
		goto done;
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
		return BLK_STS_RESOURCE;
	}
	list[0] = prp_list;
	iod->first_dma = prp_dma;
	i = 0;
	for (;;) {
		if (i == page_size >> 3) {
			__le64 *old_prp_list = prp_list;
			prp_list = dma_pool_alloc(pool, GFP_ATOMIC, &prp_dma);
			if (!prp_list)
				return BLK_STS_RESOURCE;
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
		if (unlikely(dma_len < 0))
			goto bad_sgl;
		sg = sg_next(sg);
		dma_addr = sg_dma_address(sg);
		dma_len = sg_dma_len(sg);
	}

done:
	cmnd->dptr.prp1 = cpu_to_le64(sg_dma_address(iod->sg));
	cmnd->dptr.prp2 = cpu_to_le64(iod->first_dma);

	return BLK_STS_OK;

 bad_sgl:
	WARN(DO_ONCE(nvme_print_sgl, iod->sg, iod->nents),
			"Invalid SGL for payload:%d nents:%d\n",
			blk_rq_payload_bytes(req), iod->nents);
	return BLK_STS_IOERR;
}

static void nvme_pci_sgl_set_data(struct nvme_sgl_desc *sge,
		struct scatterlist *sg)
{
	sge->addr = cpu_to_le64(sg_dma_address(sg));
	sge->length = cpu_to_le32(sg_dma_len(sg));
	sge->type = NVME_SGL_FMT_DATA_DESC << 4;
}

static void nvme_pci_sgl_set_seg(struct nvme_sgl_desc *sge,
		dma_addr_t dma_addr, int entries)
{
	sge->addr = cpu_to_le64(dma_addr);
	if (entries < SGES_PER_PAGE) {
		sge->length = cpu_to_le32(entries * sizeof(*sge));
		sge->type = NVME_SGL_FMT_LAST_SEG_DESC << 4;
	} else {
		sge->length = cpu_to_le32(PAGE_SIZE);
		sge->type = NVME_SGL_FMT_SEG_DESC << 4;
	}
}

static blk_status_t nvme_pci_setup_sgls(struct nvme_dev *dev,
		struct request *req, struct nvme_rw_command *cmd, int entries)
{
	struct nvme_iod *iod = blk_mq_rq_to_pdu(req);
	struct dma_pool *pool;
	struct nvme_sgl_desc *sg_list;
	struct scatterlist *sg = iod->sg;
	dma_addr_t sgl_dma;
	int i = 0;

	/* setting the transfer type as SGL */
	cmd->flags = NVME_CMD_SGL_METABUF;

	if (entries == 1) {
		nvme_pci_sgl_set_data(&cmd->dptr.sgl, sg);
		return BLK_STS_OK;
	}

	if (entries <= (256 / sizeof(struct nvme_sgl_desc))) {
		pool = dev->prp_small_pool;
		iod->npages = 0;
	} else {
		pool = dev->prp_page_pool;
		iod->npages = 1;
	}

	sg_list = dma_pool_alloc(pool, GFP_ATOMIC, &sgl_dma);
	if (!sg_list) {
		iod->npages = -1;
		return BLK_STS_RESOURCE;
	}

	nvme_pci_iod_list(req)[0] = sg_list;
	iod->first_dma = sgl_dma;

	nvme_pci_sgl_set_seg(&cmd->dptr.sgl, sgl_dma, entries);

	do {
		if (i == SGES_PER_PAGE) {
			struct nvme_sgl_desc *old_sg_desc = sg_list;
			struct nvme_sgl_desc *link = &old_sg_desc[i - 1];

			sg_list = dma_pool_alloc(pool, GFP_ATOMIC, &sgl_dma);
			if (!sg_list)
				return BLK_STS_RESOURCE;

			i = 0;
			nvme_pci_iod_list(req)[iod->npages++] = sg_list;
			sg_list[i++] = *link;
			nvme_pci_sgl_set_seg(link, sgl_dma, entries);
		}

		nvme_pci_sgl_set_data(&sg_list[i++], sg);
		sg = sg_next(sg);
	} while (--entries > 0);

	return BLK_STS_OK;
}

static blk_status_t nvme_map_data(struct nvme_dev *dev, struct request *req,
		struct nvme_command *cmnd)
{
	struct nvme_iod *iod = blk_mq_rq_to_pdu(req);
	struct request_queue *q = req->q;
	enum dma_data_direction dma_dir = rq_data_dir(req) ?
			DMA_TO_DEVICE : DMA_FROM_DEVICE;
	blk_status_t ret = BLK_STS_IOERR;
	int nr_mapped;

	sg_init_table(iod->sg, blk_rq_nr_phys_segments(req));
	iod->nents = blk_rq_map_sg(q, req, iod->sg);
	if (!iod->nents)
		goto out;

	ret = BLK_STS_RESOURCE;
	nr_mapped = dma_map_sg_attrs(dev->dev, iod->sg, iod->nents, dma_dir,
			DMA_ATTR_NO_WARN);
	if (!nr_mapped)
		goto out;

	if (iod->use_sgl)
		ret = nvme_pci_setup_sgls(dev, req, &cmnd->rw, nr_mapped);
	else
		ret = nvme_pci_setup_prps(dev, req, &cmnd->rw);

	if (ret != BLK_STS_OK)
		goto out_unmap;

	ret = BLK_STS_IOERR;
	if (blk_integrity_rq(req)) {
		if (blk_rq_count_integrity_sg(q, req->bio) != 1)
			goto out_unmap;

		sg_init_table(&iod->meta_sg, 1);
		if (blk_rq_map_integrity_sg(q, req->bio, &iod->meta_sg) != 1)
			goto out_unmap;

		if (req_op(req) == REQ_OP_WRITE)
			nvme_dif_remap(req, nvme_dif_prep);

		if (!dma_map_sg(dev->dev, &iod->meta_sg, 1, dma_dir))
			goto out_unmap;
	}

	if (blk_integrity_rq(req))
		cmnd->rw.metadata = cpu_to_le64(sg_dma_address(&iod->meta_sg));
	return BLK_STS_OK;

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
			if (req_op(req) == REQ_OP_READ)
				nvme_dif_remap(req, nvme_dif_complete);
			dma_unmap_sg(dev->dev, &iod->meta_sg, 1, dma_dir);
		}
	}

	nvme_cleanup_cmd(req);
	nvme_free_iod(dev, req);
}

/*
 * NOTE: ns is NULL when called on the admin queue.
 */
static blk_status_t nvme_queue_rq(struct blk_mq_hw_ctx *hctx,
			 const struct blk_mq_queue_data *bd)
{
	struct nvme_ns *ns = hctx->queue->queuedata;
	struct nvme_queue *nvmeq = hctx->driver_data;
	struct nvme_dev *dev = nvmeq->dev;
	struct request *req = bd->rq;
	struct nvme_command cmnd;
	blk_status_t ret;

	ret = nvme_setup_cmd(ns, req, &cmnd);
	if (ret)
		return ret;

	ret = nvme_init_iod(req, dev);
	if (ret)
		goto out_free_cmd;

	if (blk_rq_nr_phys_segments(req)) {
		ret = nvme_map_data(dev, req, &cmnd);
		if (ret)
			goto out_cleanup_iod;
	}

	blk_mq_start_request(req);

	spin_lock_irq(&nvmeq->q_lock);
	if (unlikely(nvmeq->cq_vector < 0)) {
		ret = BLK_STS_IOERR;
		spin_unlock_irq(&nvmeq->q_lock);
		goto out_cleanup_iod;
	}
	__nvme_submit_cmd(nvmeq, &cmnd);
	nvme_process_cq(nvmeq);
	spin_unlock_irq(&nvmeq->q_lock);
	return BLK_STS_OK;
out_cleanup_iod:
	nvme_free_iod(dev, req);
out_free_cmd:
	nvme_cleanup_cmd(req);
	return ret;
}

static void nvme_pci_complete_rq(struct request *req)
{
	struct nvme_iod *iod = blk_mq_rq_to_pdu(req);

	nvme_unmap_data(iod->nvmeq->dev, req);
	nvme_complete_rq(req);
}

/* We read the CQE phase first to check if the rest of the entry is valid */
static inline bool nvme_cqe_valid(struct nvme_queue *nvmeq, u16 head,
		u16 phase)
{
	return (le16_to_cpu(nvmeq->cqes[head].status) & 1) == phase;
}

static inline void nvme_ring_cq_doorbell(struct nvme_queue *nvmeq)
{
	u16 head = nvmeq->cq_head;

	if (likely(nvmeq->cq_vector >= 0)) {
		if (nvme_dbbuf_update_and_check_event(head, nvmeq->dbbuf_cq_db,
						      nvmeq->dbbuf_cq_ei))
			writel(head, nvmeq->q_db + nvmeq->dev->db_stride);
	}
}

static inline void nvme_handle_cqe(struct nvme_queue *nvmeq,
		struct nvme_completion *cqe)
{
	struct request *req;

	if (unlikely(cqe->command_id >= nvmeq->q_depth)) {
		dev_warn(nvmeq->dev->ctrl.device,
			"invalid id %d completed on queue %d\n",
			cqe->command_id, le16_to_cpu(cqe->sq_id));
		return;
	}

	/*
	 * AEN requests are special as they don't time out and can
	 * survive any kind of queue freeze and often don't respond to
	 * aborts.  We don't even bother to allocate a struct request
	 * for them but rather special case them here.
	 */
	if (unlikely(nvmeq->qid == 0 &&
			cqe->command_id >= NVME_AQ_BLK_MQ_DEPTH)) {
		nvme_complete_async_event(&nvmeq->dev->ctrl,
				cqe->status, &cqe->result);
		return;
	}

	nvmeq->cqe_seen = 1;
	req = blk_mq_tag_to_rq(*nvmeq->tags, cqe->command_id);
	nvme_end_request(req, cqe->status, cqe->result);
}

static inline bool nvme_read_cqe(struct nvme_queue *nvmeq,
		struct nvme_completion *cqe)
{
	if (nvme_cqe_valid(nvmeq, nvmeq->cq_head, nvmeq->cq_phase)) {
		*cqe = nvmeq->cqes[nvmeq->cq_head];

		if (++nvmeq->cq_head == nvmeq->q_depth) {
			nvmeq->cq_head = 0;
			nvmeq->cq_phase = !nvmeq->cq_phase;
		}
		return true;
	}
	return false;
}

static void nvme_process_cq(struct nvme_queue *nvmeq)
{
	struct nvme_completion cqe;
	int consumed = 0;

	while (nvme_read_cqe(nvmeq, &cqe)) {
		nvme_handle_cqe(nvmeq, &cqe);
		consumed++;
	}

	if (consumed)
		nvme_ring_cq_doorbell(nvmeq);
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

static int __nvme_poll(struct nvme_queue *nvmeq, unsigned int tag)
{
	struct nvme_completion cqe;
	int found = 0, consumed = 0;

	if (!nvme_cqe_valid(nvmeq, nvmeq->cq_head, nvmeq->cq_phase))
		return 0;

	spin_lock_irq(&nvmeq->q_lock);
	while (nvme_read_cqe(nvmeq, &cqe)) {
		nvme_handle_cqe(nvmeq, &cqe);
		consumed++;

		if (tag == cqe.command_id) {
			found = 1;
			break;
		}
       }

	if (consumed)
		nvme_ring_cq_doorbell(nvmeq);
	spin_unlock_irq(&nvmeq->q_lock);

	return found;
}

static int nvme_poll(struct blk_mq_hw_ctx *hctx, unsigned int tag)
{
	struct nvme_queue *nvmeq = hctx->driver_data;

	return __nvme_poll(nvmeq, tag);
}

static void nvme_pci_submit_async_event(struct nvme_ctrl *ctrl)
{
	struct nvme_dev *dev = to_nvme_dev(ctrl);
	struct nvme_queue *nvmeq = &dev->queues[0];
	struct nvme_command c;

	memset(&c, 0, sizeof(c));
	c.common.opcode = nvme_admin_async_event;
	c.common.command_id = NVME_AQ_BLK_MQ_DEPTH;

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
	 * Note: we (ab)use the fact that the prp fields survive if no data
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
	int flags = NVME_QUEUE_PHYS_CONTIG;

	/*
	 * Note: we (ab)use the fact that the prp fields survive if no data
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

static void abort_endio(struct request *req, blk_status_t error)
{
	struct nvme_iod *iod = blk_mq_rq_to_pdu(req);
	struct nvme_queue *nvmeq = iod->nvmeq;

	dev_warn(nvmeq->dev->ctrl.device,
		 "Abort status: 0x%x", nvme_req(req)->status);
	atomic_inc(&nvmeq->dev->ctrl.abort_limit);
	blk_mq_free_request(req);
}

static bool nvme_should_reset(struct nvme_dev *dev, u32 csts)
{

	/* If true, indicates loss of adapter communication, possibly by a
	 * NVMe Subsystem reset.
	 */
	bool nssro = dev->subsystem && (csts & NVME_CSTS_NSSRO);

	/* If there is a reset/reinit ongoing, we shouldn't reset again. */
	switch (dev->ctrl.state) {
	case NVME_CTRL_RESETTING:
	case NVME_CTRL_CONNECTING:
		return false;
	default:
		break;
	}

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

static void nvme_warn_reset(struct nvme_dev *dev, u32 csts)
{
	/* Read a config register to help see what died. */
	u16 pci_status;
	int result;

	result = pci_read_config_word(to_pci_dev(dev->dev), PCI_STATUS,
				      &pci_status);
	if (result == PCIBIOS_SUCCESSFUL)
		dev_warn(dev->ctrl.device,
			 "controller is down; will reset: CSTS=0x%x, PCI_STATUS=0x%hx\n",
			 csts, pci_status);
	else
		dev_warn(dev->ctrl.device,
			 "controller is down; will reset: CSTS=0x%x, PCI_STATUS read failed (%d)\n",
			 csts, result);
}

static enum blk_eh_timer_return nvme_timeout(struct request *req, bool reserved)
{
	struct nvme_iod *iod = blk_mq_rq_to_pdu(req);
	struct nvme_queue *nvmeq = iod->nvmeq;
	struct nvme_dev *dev = nvmeq->dev;
	struct request *abort_req;
	struct nvme_command cmd;
	u32 csts = readl(dev->bar + NVME_REG_CSTS);

	/*
	 * Reset immediately if the controller is failed
	 */
	if (nvme_should_reset(dev, csts)) {
		nvme_warn_reset(dev, csts);
		nvme_dev_disable(dev, false);
		nvme_reset_ctrl(&dev->ctrl);
		return BLK_EH_HANDLED;
	}

	/*
	 * Did we miss an interrupt?
	 */
	if (__nvme_poll(nvmeq, req->tag)) {
		dev_warn(dev->ctrl.device,
			 "I/O %d QID %d timeout, completion polled\n",
			 req->tag, nvmeq->qid);
		return BLK_EH_HANDLED;
	}

	/*
	 * Shutdown immediately if controller times out while starting. The
	 * reset work will see the pci device disabled when it gets the forced
	 * cancellation error. All outstanding requests are completed on
	 * shutdown, so we return BLK_EH_HANDLED.
	 */
	switch (dev->ctrl.state) {
	case NVME_CTRL_CONNECTING:
	case NVME_CTRL_RESETTING:
		dev_warn(dev->ctrl.device,
			 "I/O %d QID %d timeout, disable controller\n",
			 req->tag, nvmeq->qid);
		nvme_dev_disable(dev, false);
		nvme_req(req)->flags |= NVME_REQ_CANCELLED;
		return BLK_EH_HANDLED;
	default:
		break;
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
		nvme_reset_ctrl(&dev->ctrl);

		/*
		 * Mark the request as handled, since the inline shutdown
		 * forces all outstanding requests to complete.
		 */
		nvme_req(req)->flags |= NVME_REQ_CANCELLED;
		return BLK_EH_HANDLED;
	}

	if (atomic_dec_return(&dev->ctrl.abort_limit) < 0) {
		atomic_inc(&dev->ctrl.abort_limit);
		return BLK_EH_RESET_TIMER;
	}
	iod->aborted = 1;

	memset(&cmd, 0, sizeof(cmd));
	cmd.abort.opcode = nvme_admin_abort_cmd;
	cmd.abort.cid = req->tag;
	cmd.abort.sqid = cpu_to_le16(nvmeq->qid);

	dev_warn(nvmeq->dev->ctrl.device,
		"I/O %d QID %d timeout, aborting\n",
		 req->tag, nvmeq->qid);

	abort_req = nvme_alloc_request(dev->ctrl.admin_q, &cmd,
			BLK_MQ_REQ_NOWAIT, NVME_QID_ANY);
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

static void nvme_free_queue(struct nvme_queue *nvmeq)
{
	dma_free_coherent(nvmeq->q_dmadev, CQ_SIZE(nvmeq->q_depth),
				(void *)nvmeq->cqes, nvmeq->cq_dma_addr);
	if (nvmeq->sq_cmds)
		dma_free_coherent(nvmeq->q_dmadev, SQ_SIZE(nvmeq->q_depth),
					nvmeq->sq_cmds, nvmeq->sq_dma_addr);
}

static void nvme_free_queues(struct nvme_dev *dev, int lowest)
{
	int i;

	for (i = dev->ctrl.queue_count - 1; i >= lowest; i--) {
		dev->ctrl.queue_count--;
		nvme_free_queue(&dev->queues[i]);
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
	vector = nvmeq->cq_vector;
	nvmeq->dev->online_queues--;
	nvmeq->cq_vector = -1;
	spin_unlock_irq(&nvmeq->q_lock);

	if (!nvmeq->qid && nvmeq->dev->ctrl.admin_q)
		blk_mq_quiesce_queue(nvmeq->dev->ctrl.admin_q);

	pci_free_irq(to_pci_dev(nvmeq->dev->dev), vector, nvmeq);

	return 0;
}

static void nvme_disable_admin_queue(struct nvme_dev *dev, bool shutdown)
{
	struct nvme_queue *nvmeq = &dev->queues[0];

	if (shutdown)
		nvme_shutdown_ctrl(&dev->ctrl);
	else
		nvme_disable_ctrl(&dev->ctrl, dev->ctrl.cap);

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
	/* CMB SQEs will be mapped before creation */
	if (qid && dev->cmb && use_cmb_sqes && (dev->cmbsz & NVME_CMBSZ_SQS))
		return 0;

	nvmeq->sq_cmds = dma_alloc_coherent(dev->dev, SQ_SIZE(depth),
					    &nvmeq->sq_dma_addr, GFP_KERNEL);
	if (!nvmeq->sq_cmds)
		return -ENOMEM;
	return 0;
}

static int nvme_alloc_queue(struct nvme_dev *dev, int qid,
		int depth, int node)
{
	struct nvme_queue *nvmeq = &dev->queues[qid];

	if (dev->ctrl.queue_count > qid)
		return 0;

	nvmeq->cqes = dma_zalloc_coherent(dev->dev, CQ_SIZE(depth),
					  &nvmeq->cq_dma_addr, GFP_KERNEL);
	if (!nvmeq->cqes)
		goto free_nvmeq;

	if (nvme_alloc_sq_cmds(dev, nvmeq, qid, depth))
		goto free_cqdma;

	nvmeq->q_dmadev = dev->dev;
	nvmeq->dev = dev;
	spin_lock_init(&nvmeq->q_lock);
	nvmeq->cq_head = 0;
	nvmeq->cq_phase = 1;
	nvmeq->q_db = &dev->dbs[qid * 2 * dev->db_stride];
	nvmeq->q_depth = depth;
	nvmeq->qid = qid;
	nvmeq->cq_vector = -1;
	dev->ctrl.queue_count++;

	return 0;

 free_cqdma:
	dma_free_coherent(dev->dev, CQ_SIZE(depth), (void *)nvmeq->cqes,
							nvmeq->cq_dma_addr);
 free_nvmeq:
	return -ENOMEM;
}

static int queue_request_irq(struct nvme_queue *nvmeq)
{
	struct pci_dev *pdev = to_pci_dev(nvmeq->dev->dev);
	int nr = nvmeq->dev->ctrl.instance;

	if (use_threaded_interrupts) {
		return pci_request_irq(pdev, nvmeq->cq_vector, nvme_irq_check,
				nvme_irq, nvmeq, "nvme%dq%d", nr, nvmeq->qid);
	} else {
		return pci_request_irq(pdev, nvmeq->cq_vector, nvme_irq,
				NULL, nvmeq, "nvme%dq%d", nr, nvmeq->qid);
	}
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
	nvme_dbbuf_init(dev, nvmeq, qid);
	dev->online_queues++;
	spin_unlock_irq(&nvmeq->q_lock);
}

static int nvme_create_queue(struct nvme_queue *nvmeq, int qid)
{
	struct nvme_dev *dev = nvmeq->dev;
	int result;

	if (dev->cmb && use_cmb_sqes && (dev->cmbsz & NVME_CMBSZ_SQS)) {
		unsigned offset = (qid - 1) * roundup(SQ_SIZE(nvmeq->q_depth),
						      dev->ctrl.page_size);
		nvmeq->sq_dma_addr = dev->cmb_bus_addr + offset;
		nvmeq->sq_cmds_io = dev->cmb + offset;
	}

	nvmeq->cq_vector = qid - 1;
	result = adapter_alloc_cq(dev, qid, nvmeq);
	if (result < 0)
		return result;

	result = adapter_alloc_sq(dev, qid, nvmeq);
	if (result < 0)
		goto release_cq;

	nvme_init_queue(nvmeq, qid);
	result = queue_request_irq(nvmeq);
	if (result < 0)
		goto release_sq;

	return result;

 release_sq:
	adapter_delete_sq(dev, qid);
 release_cq:
	adapter_delete_cq(dev, qid);
	return result;
}

static const struct blk_mq_ops nvme_mq_admin_ops = {
	.queue_rq	= nvme_queue_rq,
	.complete	= nvme_pci_complete_rq,
	.init_hctx	= nvme_admin_init_hctx,
	.exit_hctx      = nvme_admin_exit_hctx,
	.init_request	= nvme_init_request,
	.timeout	= nvme_timeout,
};

static const struct blk_mq_ops nvme_mq_ops = {
	.queue_rq	= nvme_queue_rq,
	.complete	= nvme_pci_complete_rq,
	.init_hctx	= nvme_init_hctx,
	.init_request	= nvme_init_request,
	.map_queues	= nvme_pci_map_queues,
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
		blk_mq_unquiesce_queue(dev->ctrl.admin_q);
		blk_cleanup_queue(dev->ctrl.admin_q);
		blk_mq_free_tag_set(&dev->admin_tagset);
	}
}

static int nvme_alloc_admin_tags(struct nvme_dev *dev)
{
	if (!dev->ctrl.admin_q) {
		dev->admin_tagset.ops = &nvme_mq_admin_ops;
		dev->admin_tagset.nr_hw_queues = 1;

		dev->admin_tagset.queue_depth = NVME_AQ_MQ_TAG_DEPTH;
		dev->admin_tagset.timeout = ADMIN_TIMEOUT;
		dev->admin_tagset.numa_node = dev_to_node(dev->dev);
		dev->admin_tagset.cmd_size = nvme_pci_cmd_size(dev, false);
		dev->admin_tagset.flags = BLK_MQ_F_NO_SCHED;
		dev->admin_tagset.driver_data = dev;

		if (blk_mq_alloc_tag_set(&dev->admin_tagset))
			return -ENOMEM;
		dev->ctrl.admin_tagset = &dev->admin_tagset;

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
		blk_mq_unquiesce_queue(dev->ctrl.admin_q);

	return 0;
}

static unsigned long db_bar_size(struct nvme_dev *dev, unsigned nr_io_queues)
{
	return NVME_REG_DBS + ((nr_io_queues + 1) * 8 * dev->db_stride);
}

static int nvme_remap_bar(struct nvme_dev *dev, unsigned long size)
{
	struct pci_dev *pdev = to_pci_dev(dev->dev);

	if (size <= dev->bar_mapped_size)
		return 0;
	if (size > pci_resource_len(pdev, 0))
		return -ENOMEM;
	if (dev->bar)
		iounmap(dev->bar);
	dev->bar = ioremap(pci_resource_start(pdev, 0), size);
	if (!dev->bar) {
		dev->bar_mapped_size = 0;
		return -ENOMEM;
	}
	dev->bar_mapped_size = size;
	dev->dbs = dev->bar + NVME_REG_DBS;

	return 0;
}

static int nvme_pci_configure_admin_queue(struct nvme_dev *dev)
{
	int result;
	u32 aqa;
	struct nvme_queue *nvmeq;

	result = nvme_remap_bar(dev, db_bar_size(dev, 0));
	if (result < 0)
		return result;

	dev->subsystem = readl(dev->bar + NVME_REG_VS) >= NVME_VS(1, 1, 0) ?
				NVME_CAP_NSSRC(dev->ctrl.cap) : 0;

	if (dev->subsystem &&
	    (readl(dev->bar + NVME_REG_CSTS) & NVME_CSTS_NSSRO))
		writel(NVME_CSTS_NSSRO, dev->bar + NVME_REG_CSTS);

	result = nvme_disable_ctrl(&dev->ctrl, dev->ctrl.cap);
	if (result < 0)
		return result;

	result = nvme_alloc_queue(dev, 0, NVME_AQ_DEPTH,
			dev_to_node(dev->dev));
	if (result)
		return result;

	nvmeq = &dev->queues[0];
	aqa = nvmeq->q_depth - 1;
	aqa |= aqa << 16;

	writel(aqa, dev->bar + NVME_REG_AQA);
	lo_hi_writeq(nvmeq->sq_dma_addr, dev->bar + NVME_REG_ASQ);
	lo_hi_writeq(nvmeq->cq_dma_addr, dev->bar + NVME_REG_ACQ);

	result = nvme_enable_ctrl(&dev->ctrl, dev->ctrl.cap);
	if (result)
		return result;

	nvmeq->cq_vector = 0;
	nvme_init_queue(nvmeq, 0);
	result = queue_request_irq(nvmeq);
	if (result) {
		nvmeq->cq_vector = -1;
		return result;
	}

	return result;
}

static int nvme_create_io_queues(struct nvme_dev *dev)
{
	unsigned i, max;
	int ret = 0;

	for (i = dev->ctrl.queue_count; i <= dev->max_qid; i++) {
		/* vector == qid - 1, match nvme_create_queue */
		if (nvme_alloc_queue(dev, i, dev->q_depth,
		     pci_irq_get_node(to_pci_dev(dev->dev), i - 1))) {
			ret = -ENOMEM;
			break;
		}
	}

	max = min(dev->max_qid, dev->ctrl.queue_count - 1);
	for (i = dev->online_queues; i <= max; i++) {
		ret = nvme_create_queue(&dev->queues[i], i);
		if (ret)
			break;
	}

	/*
	 * Ignore failing Create SQ/CQ commands, we can continue with less
	 * than the desired amount of queues, and even a controller without
	 * I/O queues can still be used to issue admin commands.  This might
	 * be useful to upgrade a buggy firmware for example.
	 */
	return ret >= 0 ? 0 : ret;
}

static ssize_t nvme_cmb_show(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	struct nvme_dev *ndev = to_nvme_dev(dev_get_drvdata(dev));

	return scnprintf(buf, PAGE_SIZE, "cmbloc : x%08x\ncmbsz  : x%08x\n",
		       ndev->cmbloc, ndev->cmbsz);
}
static DEVICE_ATTR(cmb, S_IRUGO, nvme_cmb_show, NULL);

static u64 nvme_cmb_size_unit(struct nvme_dev *dev)
{
	u8 szu = (dev->cmbsz >> NVME_CMBSZ_SZU_SHIFT) & NVME_CMBSZ_SZU_MASK;

	return 1ULL << (12 + 4 * szu);
}

static u32 nvme_cmb_size(struct nvme_dev *dev)
{
	return (dev->cmbsz >> NVME_CMBSZ_SZ_SHIFT) & NVME_CMBSZ_SZ_MASK;
}

static void nvme_map_cmb(struct nvme_dev *dev)
{
	u64 size, offset;
	resource_size_t bar_size;
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	int bar;

	dev->cmbsz = readl(dev->bar + NVME_REG_CMBSZ);
	if (!dev->cmbsz)
		return;
	dev->cmbloc = readl(dev->bar + NVME_REG_CMBLOC);

	if (!use_cmb_sqes)
		return;

	size = nvme_cmb_size_unit(dev) * nvme_cmb_size(dev);
	offset = nvme_cmb_size_unit(dev) * NVME_CMB_OFST(dev->cmbloc);
	bar = NVME_CMB_BIR(dev->cmbloc);
	bar_size = pci_resource_len(pdev, bar);

	if (offset > bar_size)
		return;

	/*
	 * Controllers may support a CMB size larger than their BAR,
	 * for example, due to being behind a bridge. Reduce the CMB to
	 * the reported size of the BAR
	 */
	if (size > bar_size - offset)
		size = bar_size - offset;

	dev->cmb = ioremap_wc(pci_resource_start(pdev, bar) + offset, size);
	if (!dev->cmb)
		return;
	dev->cmb_bus_addr = pci_bus_address(pdev, bar) + offset;
	dev->cmb_size = size;

	if (sysfs_add_file_to_group(&dev->ctrl.device->kobj,
				    &dev_attr_cmb.attr, NULL))
		dev_warn(dev->ctrl.device,
			 "failed to add sysfs attribute for CMB\n");
}

static inline void nvme_release_cmb(struct nvme_dev *dev)
{
	if (dev->cmb) {
		iounmap(dev->cmb);
		dev->cmb = NULL;
		sysfs_remove_file_from_group(&dev->ctrl.device->kobj,
					     &dev_attr_cmb.attr, NULL);
		dev->cmbsz = 0;
	}
}

static int nvme_set_host_mem(struct nvme_dev *dev, u32 bits)
{
	u64 dma_addr = dev->host_mem_descs_dma;
	struct nvme_command c;
	int ret;

	memset(&c, 0, sizeof(c));
	c.features.opcode	= nvme_admin_set_features;
	c.features.fid		= cpu_to_le32(NVME_FEAT_HOST_MEM_BUF);
	c.features.dword11	= cpu_to_le32(bits);
	c.features.dword12	= cpu_to_le32(dev->host_mem_size >>
					      ilog2(dev->ctrl.page_size));
	c.features.dword13	= cpu_to_le32(lower_32_bits(dma_addr));
	c.features.dword14	= cpu_to_le32(upper_32_bits(dma_addr));
	c.features.dword15	= cpu_to_le32(dev->nr_host_mem_descs);

	ret = nvme_submit_sync_cmd(dev->ctrl.admin_q, &c, NULL, 0);
	if (ret) {
		dev_warn(dev->ctrl.device,
			 "failed to set host mem (err %d, flags %#x).\n",
			 ret, bits);
	}
	return ret;
}

static void nvme_free_host_mem(struct nvme_dev *dev)
{
	int i;

	for (i = 0; i < dev->nr_host_mem_descs; i++) {
		struct nvme_host_mem_buf_desc *desc = &dev->host_mem_descs[i];
		size_t size = le32_to_cpu(desc->size) * dev->ctrl.page_size;

		dma_free_coherent(dev->dev, size, dev->host_mem_desc_bufs[i],
				le64_to_cpu(desc->addr));
	}

	kfree(dev->host_mem_desc_bufs);
	dev->host_mem_desc_bufs = NULL;
	dma_free_coherent(dev->dev,
			dev->nr_host_mem_descs * sizeof(*dev->host_mem_descs),
			dev->host_mem_descs, dev->host_mem_descs_dma);
	dev->host_mem_descs = NULL;
	dev->nr_host_mem_descs = 0;
}

static int __nvme_alloc_host_mem(struct nvme_dev *dev, u64 preferred,
		u32 chunk_size)
{
	struct nvme_host_mem_buf_desc *descs;
	u32 max_entries, len;
	dma_addr_t descs_dma;
	int i = 0;
	void **bufs;
	u64 size, tmp;

	tmp = (preferred + chunk_size - 1);
	do_div(tmp, chunk_size);
	max_entries = tmp;

	if (dev->ctrl.hmmaxd && dev->ctrl.hmmaxd < max_entries)
		max_entries = dev->ctrl.hmmaxd;

	descs = dma_zalloc_coherent(dev->dev, max_entries * sizeof(*descs),
			&descs_dma, GFP_KERNEL);
	if (!descs)
		goto out;

	bufs = kcalloc(max_entries, sizeof(*bufs), GFP_KERNEL);
	if (!bufs)
		goto out_free_descs;

	for (size = 0; size < preferred && i < max_entries; size += len) {
		dma_addr_t dma_addr;

		len = min_t(u64, chunk_size, preferred - size);
		bufs[i] = dma_alloc_attrs(dev->dev, len, &dma_addr, GFP_KERNEL,
				DMA_ATTR_NO_KERNEL_MAPPING | DMA_ATTR_NO_WARN);
		if (!bufs[i])
			break;

		descs[i].addr = cpu_to_le64(dma_addr);
		descs[i].size = cpu_to_le32(len / dev->ctrl.page_size);
		i++;
	}

	if (!size)
		goto out_free_bufs;

	dev->nr_host_mem_descs = i;
	dev->host_mem_size = size;
	dev->host_mem_descs = descs;
	dev->host_mem_descs_dma = descs_dma;
	dev->host_mem_desc_bufs = bufs;
	return 0;

out_free_bufs:
	while (--i >= 0) {
		size_t size = le32_to_cpu(descs[i].size) * dev->ctrl.page_size;

		dma_free_coherent(dev->dev, size, bufs[i],
				le64_to_cpu(descs[i].addr));
	}

	kfree(bufs);
out_free_descs:
	dma_free_coherent(dev->dev, max_entries * sizeof(*descs), descs,
			descs_dma);
out:
	dev->host_mem_descs = NULL;
	return -ENOMEM;
}

static int nvme_alloc_host_mem(struct nvme_dev *dev, u64 min, u64 preferred)
{
	u32 chunk_size;

	/* start big and work our way down */
	for (chunk_size = min_t(u64, preferred, PAGE_SIZE * MAX_ORDER_NR_PAGES);
	     chunk_size >= max_t(u32, dev->ctrl.hmminds * 4096, PAGE_SIZE * 2);
	     chunk_size /= 2) {
		if (!__nvme_alloc_host_mem(dev, preferred, chunk_size)) {
			if (!min || dev->host_mem_size >= min)
				return 0;
			nvme_free_host_mem(dev);
		}
	}

	return -ENOMEM;
}

static int nvme_setup_host_mem(struct nvme_dev *dev)
{
	u64 max = (u64)max_host_mem_size_mb * SZ_1M;
	u64 preferred = (u64)dev->ctrl.hmpre * 4096;
	u64 min = (u64)dev->ctrl.hmmin * 4096;
	u32 enable_bits = NVME_HOST_MEM_ENABLE;
	int ret;

	preferred = min(preferred, max);
	if (min > max) {
		dev_warn(dev->ctrl.device,
			"min host memory (%lld MiB) above limit (%d MiB).\n",
			min >> ilog2(SZ_1M), max_host_mem_size_mb);
		nvme_free_host_mem(dev);
		return 0;
	}

	/*
	 * If we already have a buffer allocated check if we can reuse it.
	 */
	if (dev->host_mem_descs) {
		if (dev->host_mem_size >= min)
			enable_bits |= NVME_HOST_MEM_RETURN;
		else
			nvme_free_host_mem(dev);
	}

	if (!dev->host_mem_descs) {
		if (nvme_alloc_host_mem(dev, min, preferred)) {
			dev_warn(dev->ctrl.device,
				"failed to allocate host memory buffer.\n");
			return 0; /* controller must work without HMB */
		}

		dev_info(dev->ctrl.device,
			"allocated %lld MiB host memory buffer.\n",
			dev->host_mem_size >> ilog2(SZ_1M));
	}

	ret = nvme_set_host_mem(dev, enable_bits);
	if (ret)
		nvme_free_host_mem(dev);
	return ret;
}

static int nvme_setup_io_queues(struct nvme_dev *dev)
{
	struct nvme_queue *adminq = &dev->queues[0];
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	int result, nr_io_queues;
	unsigned long size;

	nr_io_queues = num_present_cpus();
	result = nvme_set_queue_count(&dev->ctrl, &nr_io_queues);
	if (result < 0)
		return result;

	if (nr_io_queues == 0)
		return 0;

	if (dev->cmb && (dev->cmbsz & NVME_CMBSZ_SQS)) {
		result = nvme_cmb_qdepth(dev, nr_io_queues,
				sizeof(struct nvme_command));
		if (result > 0)
			dev->q_depth = result;
		else
			nvme_release_cmb(dev);
	}

	do {
		size = db_bar_size(dev, nr_io_queues);
		result = nvme_remap_bar(dev, size);
		if (!result)
			break;
		if (!--nr_io_queues)
			return -ENOMEM;
	} while (1);
	adminq->q_db = dev->dbs;

	/* Deregister the admin queue's interrupt */
	pci_free_irq(pdev, 0, adminq);

	/*
	 * If we enable msix early due to not intx, disable it again before
	 * setting up the full range we need.
	 */
	pci_free_irq_vectors(pdev);
	nr_io_queues = pci_alloc_irq_vectors(pdev, 1, nr_io_queues,
			PCI_IRQ_ALL_TYPES | PCI_IRQ_AFFINITY);
	if (nr_io_queues <= 0)
		return -EIO;
	dev->max_qid = nr_io_queues;

	/*
	 * Should investigate if there's a performance win from allocating
	 * more queues than interrupt vectors; it might allow the submission
	 * path to scale better, even if the receive path is limited by the
	 * number of interrupts.
	 */

	result = queue_request_irq(adminq);
	if (result) {
		adminq->cq_vector = -1;
		return result;
	}
	return nvme_create_io_queues(dev);
}

static void nvme_del_queue_end(struct request *req, blk_status_t error)
{
	struct nvme_queue *nvmeq = req->end_io_data;

	blk_mq_free_request(req);
	complete(&nvmeq->dev->ioq_wait);
}

static void nvme_del_cq_end(struct request *req, blk_status_t error)
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

	req = nvme_alloc_request(q, &cmd, BLK_MQ_REQ_NOWAIT, NVME_QID_ANY);
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
		for (; i > 0; i--, sent++)
			if (nvme_delete_queue(&dev->queues[i], opcode))
				break;

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
 * return error value only when tagset allocation failed
 */
static int nvme_dev_add(struct nvme_dev *dev)
{
	int ret;

	if (!dev->ctrl.tagset) {
		dev->tagset.ops = &nvme_mq_ops;
		dev->tagset.nr_hw_queues = dev->online_queues - 1;
		dev->tagset.timeout = NVME_IO_TIMEOUT;
		dev->tagset.numa_node = dev_to_node(dev->dev);
		dev->tagset.queue_depth =
				min_t(int, dev->q_depth, BLK_MQ_MAX_DEPTH) - 1;
		dev->tagset.cmd_size = nvme_pci_cmd_size(dev, false);
		if ((dev->ctrl.sgls & ((1 << 0) | (1 << 1))) && sgl_threshold) {
			dev->tagset.cmd_size = max(dev->tagset.cmd_size,
					nvme_pci_cmd_size(dev, true));
		}
		dev->tagset.flags = BLK_MQ_F_SHOULD_MERGE;
		dev->tagset.driver_data = dev;

		ret = blk_mq_alloc_tag_set(&dev->tagset);
		if (ret) {
			dev_warn(dev->ctrl.device,
				"IO queues tagset allocation failed %d\n", ret);
			return ret;
		}
		dev->ctrl.tagset = &dev->tagset;

		nvme_dbbuf_set(dev);
	} else {
		blk_mq_update_nr_hw_queues(&dev->tagset, dev->online_queues - 1);

		/* Free previously allocated queues that are no longer usable */
		nvme_free_queues(dev, dev->online_queues);
	}

	return 0;
}

static int nvme_pci_enable(struct nvme_dev *dev)
{
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
	result = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_ALL_TYPES);
	if (result < 0)
		return result;

	dev->ctrl.cap = lo_hi_readq(dev->bar + NVME_REG_CAP);

	dev->q_depth = min_t(int, NVME_CAP_MQES(dev->ctrl.cap) + 1,
				io_queue_depth);
	dev->db_stride = 1 << NVME_CAP_STRIDE(dev->ctrl.cap);
	dev->dbs = dev->bar + 4096;

	/*
	 * Temporary fix for the Apple controller found in the MacBook8,1 and
	 * some MacBook7,1 to avoid controller resets and data loss.
	 */
	if (pdev->vendor == PCI_VENDOR_ID_APPLE && pdev->device == 0x2001) {
		dev->q_depth = 2;
		dev_warn(dev->ctrl.device, "detected Apple NVMe controller, "
			"set queue depth=%u to work around controller resets\n",
			dev->q_depth);
	} else if (pdev->vendor == PCI_VENDOR_ID_SAMSUNG &&
		   (pdev->device == 0xa821 || pdev->device == 0xa822) &&
		   NVME_CAP_MQES(dev->ctrl.cap) == 0) {
		dev->q_depth = 64;
		dev_err(dev->ctrl.device, "detected PM1725 NVMe controller, "
                        "set queue depth=%u\n", dev->q_depth);
	}

	nvme_map_cmb(dev);

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
	pci_release_mem_regions(to_pci_dev(dev->dev));
}

static void nvme_pci_disable(struct nvme_dev *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev->dev);

	nvme_release_cmb(dev);
	pci_free_irq_vectors(pdev);

	if (pci_is_enabled(pdev)) {
		pci_disable_pcie_error_reporting(pdev);
		pci_disable_device(pdev);
	}
}

static void nvme_dev_disable(struct nvme_dev *dev, bool shutdown)
{
	int i;
	bool dead = true;
	struct pci_dev *pdev = to_pci_dev(dev->dev);

	mutex_lock(&dev->shutdown_lock);
	if (pci_is_enabled(pdev)) {
		u32 csts = readl(dev->bar + NVME_REG_CSTS);

		if (dev->ctrl.state == NVME_CTRL_LIVE ||
		    dev->ctrl.state == NVME_CTRL_RESETTING)
			nvme_start_freeze(&dev->ctrl);
		dead = !!((csts & NVME_CSTS_CFS) || !(csts & NVME_CSTS_RDY) ||
			pdev->error_state  != pci_channel_io_normal);
	}

	/*
	 * Give the controller a chance to complete all entered requests if
	 * doing a safe shutdown.
	 */
	if (!dead) {
		if (shutdown)
			nvme_wait_freeze_timeout(&dev->ctrl, NVME_IO_TIMEOUT);

		/*
		 * If the controller is still alive tell it to stop using the
		 * host memory buffer.  In theory the shutdown / reset should
		 * make sure that it doesn't access the host memoery anymore,
		 * but I'd rather be safe than sorry..
		 */
		if (dev->host_mem_descs)
			nvme_set_host_mem(dev, 0);

	}
	nvme_stop_queues(&dev->ctrl);

	if (!dead) {
		nvme_disable_io_queues(dev);
		nvme_disable_admin_queue(dev, shutdown);
	}
	for (i = dev->ctrl.queue_count - 1; i >= 0; i--)
		nvme_suspend_queue(&dev->queues[i]);

	nvme_pci_disable(dev);

	blk_mq_tagset_busy_iter(&dev->tagset, nvme_cancel_request, &dev->ctrl);
	blk_mq_tagset_busy_iter(&dev->admin_tagset, nvme_cancel_request, &dev->ctrl);

	/*
	 * The driver will not be starting up queues again if shutting down so
	 * must flush all entered requests to their failed completion to avoid
	 * deadlocking blk-mq hot-cpu notifier.
	 */
	if (shutdown)
		nvme_start_queues(&dev->ctrl);
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

	nvme_dbbuf_dma_free(dev);
	put_device(dev->dev);
	if (dev->tagset.tags)
		blk_mq_free_tag_set(&dev->tagset);
	if (dev->ctrl.admin_q)
		blk_put_queue(dev->ctrl.admin_q);
	kfree(dev->queues);
	free_opal_dev(dev->ctrl.opal_dev);
	kfree(dev);
}

static void nvme_remove_dead_ctrl(struct nvme_dev *dev, int status)
{
	dev_warn(dev->ctrl.device, "Removing after probe failure status: %d\n", status);

	nvme_get_ctrl(&dev->ctrl);
	nvme_dev_disable(dev, false);
	if (!queue_work(nvme_wq, &dev->remove_work))
		nvme_put_ctrl(&dev->ctrl);
}

static void nvme_reset_work(struct work_struct *work)
{
	struct nvme_dev *dev =
		container_of(work, struct nvme_dev, ctrl.reset_work);
	bool was_suspend = !!(dev->ctrl.ctrl_config & NVME_CC_SHN_NORMAL);
	int result = -ENODEV;
	enum nvme_ctrl_state new_state = NVME_CTRL_LIVE;

	if (WARN_ON(dev->ctrl.state != NVME_CTRL_RESETTING))
		goto out;

	/*
	 * If we're called to reset a live controller first shut it down before
	 * moving on.
	 */
	if (dev->ctrl.ctrl_config & NVME_CC_ENABLE)
		nvme_dev_disable(dev, false);

	/*
	 * Introduce CONNECTING state from nvme-fc/rdma transports to mark the
	 * initializing procedure here.
	 */
	if (!nvme_change_ctrl_state(&dev->ctrl, NVME_CTRL_CONNECTING)) {
		dev_warn(dev->ctrl.device,
			"failed to mark controller CONNECTING\n");
		goto out;
	}

	result = nvme_pci_enable(dev);
	if (result)
		goto out;

	result = nvme_pci_configure_admin_queue(dev);
	if (result)
		goto out;

	result = nvme_alloc_admin_tags(dev);
	if (result)
		goto out;

	result = nvme_init_identify(&dev->ctrl);
	if (result)
		goto out;

	if (dev->ctrl.oacs & NVME_CTRL_OACS_SEC_SUPP) {
		if (!dev->ctrl.opal_dev)
			dev->ctrl.opal_dev =
				init_opal_dev(&dev->ctrl, &nvme_sec_submit);
		else if (was_suspend)
			opal_unlock_from_suspend(dev->ctrl.opal_dev);
	} else {
		free_opal_dev(dev->ctrl.opal_dev);
		dev->ctrl.opal_dev = NULL;
	}

	if (dev->ctrl.oacs & NVME_CTRL_OACS_DBBUF_SUPP) {
		result = nvme_dbbuf_dma_alloc(dev);
		if (result)
			dev_warn(dev->dev,
				 "unable to allocate dma for dbbuf\n");
	}

	if (dev->ctrl.hmpre) {
		result = nvme_setup_host_mem(dev);
		if (result < 0)
			goto out;
	}

	result = nvme_setup_io_queues(dev);
	if (result)
		goto out;

	/*
	 * Keep the controller around but remove all namespaces if we don't have
	 * any working I/O queue.
	 */
	if (dev->online_queues < 2) {
		dev_warn(dev->ctrl.device, "IO queues not created\n");
		nvme_kill_queues(&dev->ctrl);
		nvme_remove_namespaces(&dev->ctrl);
		new_state = NVME_CTRL_ADMIN_ONLY;
	} else {
		nvme_start_queues(&dev->ctrl);
		nvme_wait_freeze(&dev->ctrl);
		/* hit this only when allocate tagset fails */
		if (nvme_dev_add(dev))
			new_state = NVME_CTRL_ADMIN_ONLY;
		nvme_unfreeze(&dev->ctrl);
	}

	/*
	 * If only admin queue live, keep it to do further investigation or
	 * recovery.
	 */
	if (!nvme_change_ctrl_state(&dev->ctrl, new_state)) {
		dev_warn(dev->ctrl.device,
			"failed to mark controller state %d\n", new_state);
		goto out;
	}

	nvme_start_ctrl(&dev->ctrl);
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

static const struct nvme_ctrl_ops nvme_pci_ctrl_ops = {
	.name			= "pcie",
	.module			= THIS_MODULE,
	.flags			= NVME_F_METADATA_SUPPORTED,
	.reg_read32		= nvme_pci_reg_read32,
	.reg_write32		= nvme_pci_reg_write32,
	.reg_read64		= nvme_pci_reg_read64,
	.free_ctrl		= nvme_pci_free_ctrl,
	.submit_async_event	= nvme_pci_submit_async_event,
};

static int nvme_dev_map(struct nvme_dev *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev->dev);

	if (pci_request_mem_regions(pdev, "nvme"))
		return -ENODEV;

	if (nvme_remap_bar(dev, NVME_REG_DBS + 4096))
		goto release;

	return 0;
  release:
	pci_release_mem_regions(pdev);
	return -ENODEV;
}

static unsigned long check_vendor_combination_bug(struct pci_dev *pdev)
{
	if (pdev->vendor == 0x144d && pdev->device == 0xa802) {
		/*
		 * Several Samsung devices seem to drop off the PCIe bus
		 * randomly when APST is on and uses the deepest sleep state.
		 * This has been observed on a Samsung "SM951 NVMe SAMSUNG
		 * 256GB", a "PM951 NVMe SAMSUNG 512GB", and a "Samsung SSD
		 * 950 PRO 256GB", but it seems to be restricted to two Dell
		 * laptops.
		 */
		if (dmi_match(DMI_SYS_VENDOR, "Dell Inc.") &&
		    (dmi_match(DMI_PRODUCT_NAME, "XPS 15 9550") ||
		     dmi_match(DMI_PRODUCT_NAME, "Precision 5510")))
			return NVME_QUIRK_NO_DEEPEST_PS;
	} else if (pdev->vendor == 0x144d && pdev->device == 0xa804) {
		/*
		 * Samsung SSD 960 EVO drops off the PCIe bus after system
		 * suspend on a Ryzen board, ASUS PRIME B350M-A.
		 */
		if (dmi_match(DMI_BOARD_VENDOR, "ASUSTeK COMPUTER INC.") &&
		    dmi_match(DMI_BOARD_NAME, "PRIME B350M-A"))
			return NVME_QUIRK_NO_APST;
	}

	return 0;
}

static int nvme_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	int node, result = -ENOMEM;
	struct nvme_dev *dev;
	unsigned long quirks = id->driver_data;

	node = dev_to_node(&pdev->dev);
	if (node == NUMA_NO_NODE)
		set_dev_node(&pdev->dev, first_memory_node);

	dev = kzalloc_node(sizeof(*dev), GFP_KERNEL, node);
	if (!dev)
		return -ENOMEM;

	dev->queues = kcalloc_node(num_possible_cpus() + 1,
			sizeof(struct nvme_queue), GFP_KERNEL, node);
	if (!dev->queues)
		goto free;

	dev->dev = get_device(&pdev->dev);
	pci_set_drvdata(pdev, dev);

	result = nvme_dev_map(dev);
	if (result)
		goto put_pci;

	INIT_WORK(&dev->ctrl.reset_work, nvme_reset_work);
	INIT_WORK(&dev->remove_work, nvme_remove_dead_ctrl_work);
	mutex_init(&dev->shutdown_lock);
	init_completion(&dev->ioq_wait);

	result = nvme_setup_prp_pools(dev);
	if (result)
		goto unmap;

	quirks |= check_vendor_combination_bug(pdev);

	result = nvme_init_ctrl(&dev->ctrl, &pdev->dev, &nvme_pci_ctrl_ops,
			quirks);
	if (result)
		goto release_pools;

	dev_info(dev->ctrl.device, "pci function %s\n", dev_name(&pdev->dev));

	nvme_reset_ctrl(&dev->ctrl);

	return 0;

 release_pools:
	nvme_release_prp_pools(dev);
 unmap:
	nvme_dev_unmap(dev);
 put_pci:
	put_device(dev->dev);
 free:
	kfree(dev->queues);
	kfree(dev);
	return result;
}

static void nvme_reset_prepare(struct pci_dev *pdev)
{
	struct nvme_dev *dev = pci_get_drvdata(pdev);
	nvme_dev_disable(dev, false);
}

static void nvme_reset_done(struct pci_dev *pdev)
{
	struct nvme_dev *dev = pci_get_drvdata(pdev);
	nvme_reset_ctrl_sync(&dev->ctrl);
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

	cancel_work_sync(&dev->ctrl.reset_work);
	pci_set_drvdata(pdev, NULL);

	if (!pci_device_is_present(pdev)) {
		nvme_change_ctrl_state(&dev->ctrl, NVME_CTRL_DEAD);
		nvme_dev_disable(dev, false);
	}

	flush_work(&dev->ctrl.reset_work);
	nvme_stop_ctrl(&dev->ctrl);
	nvme_remove_namespaces(&dev->ctrl);
	nvme_dev_disable(dev, true);
	nvme_free_host_mem(dev);
	nvme_dev_remove_admin(dev);
	nvme_free_queues(dev, 0);
	nvme_uninit_ctrl(&dev->ctrl);
	nvme_release_prp_pools(dev);
	nvme_dev_unmap(dev);
	nvme_put_ctrl(&dev->ctrl);
}

static int nvme_pci_sriov_configure(struct pci_dev *pdev, int numvfs)
{
	int ret = 0;

	if (numvfs == 0) {
		if (pci_vfs_assigned(pdev)) {
			dev_warn(&pdev->dev,
				"Cannot disable SR-IOV VFs while assigned\n");
			return -EPERM;
		}
		pci_disable_sriov(pdev);
		return 0;
	}

	ret = pci_enable_sriov(pdev, numvfs);
	return ret ? ret : numvfs;
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

	nvme_reset_ctrl(&ndev->ctrl);
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
	nvme_reset_ctrl(&dev->ctrl);
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
	.reset_prepare	= nvme_reset_prepare,
	.reset_done	= nvme_reset_done,
};

static const struct pci_device_id nvme_id_table[] = {
	{ PCI_VDEVICE(INTEL, 0x0953),
		.driver_data = NVME_QUIRK_STRIPE_SIZE |
				NVME_QUIRK_DEALLOCATE_ZEROES, },
	{ PCI_VDEVICE(INTEL, 0x0a53),
		.driver_data = NVME_QUIRK_STRIPE_SIZE |
				NVME_QUIRK_DEALLOCATE_ZEROES, },
	{ PCI_VDEVICE(INTEL, 0x0a54),
		.driver_data = NVME_QUIRK_STRIPE_SIZE |
				NVME_QUIRK_DEALLOCATE_ZEROES, },
	{ PCI_VDEVICE(INTEL, 0x0a55),
		.driver_data = NVME_QUIRK_STRIPE_SIZE |
				NVME_QUIRK_DEALLOCATE_ZEROES, },
	{ PCI_VDEVICE(INTEL, 0xf1a5),	/* Intel 600P/P3100 */
		.driver_data = NVME_QUIRK_NO_DEEPEST_PS },
	{ PCI_VDEVICE(INTEL, 0x5845),	/* Qemu emulated controller */
		.driver_data = NVME_QUIRK_IDENTIFY_CNS, },
	{ PCI_DEVICE(0x1c58, 0x0003),	/* HGST adapter */
		.driver_data = NVME_QUIRK_DELAY_BEFORE_CHK_RDY, },
	{ PCI_DEVICE(0x1c58, 0x0023),	/* WDC SN200 adapter */
		.driver_data = NVME_QUIRK_DELAY_BEFORE_CHK_RDY, },
	{ PCI_DEVICE(0x1c5f, 0x0540),	/* Memblaze Pblaze4 adapter */
		.driver_data = NVME_QUIRK_DELAY_BEFORE_CHK_RDY, },
	{ PCI_DEVICE(0x144d, 0xa821),   /* Samsung PM1725 */
		.driver_data = NVME_QUIRK_DELAY_BEFORE_CHK_RDY, },
	{ PCI_DEVICE(0x144d, 0xa822),   /* Samsung PM1725a */
		.driver_data = NVME_QUIRK_DELAY_BEFORE_CHK_RDY, },
	{ PCI_DEVICE(0x1d1d, 0x1f1f),	/* LighNVM qemu device */
		.driver_data = NVME_QUIRK_LIGHTNVM, },
	{ PCI_DEVICE(0x1d1d, 0x2807),	/* CNEX WL */
		.driver_data = NVME_QUIRK_LIGHTNVM, },
	{ PCI_DEVICE_CLASS(PCI_CLASS_STORAGE_EXPRESS, 0xffffff) },
	{ PCI_DEVICE(PCI_VENDOR_ID_APPLE, 0x2001) },
	{ PCI_DEVICE(PCI_VENDOR_ID_APPLE, 0x2003) },
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
	.sriov_configure = nvme_pci_sriov_configure,
	.err_handler	= &nvme_err_handler,
};

static int __init nvme_init(void)
{
	return pci_register_driver(&nvme_driver);
}

static void __exit nvme_exit(void)
{
	pci_unregister_driver(&nvme_driver);
	flush_workqueue(nvme_wq);
	_nvme_check_size();
}

MODULE_AUTHOR("Matthew Wilcox <willy@linux.intel.com>");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0");
module_init(nvme_init);
module_exit(nvme_exit);
