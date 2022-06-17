// SPDX-License-Identifier: GPL-2.0
/*
 * NVM Express device driver
 * Copyright (c) 2011-2014, Intel Corporation.
 */

#include <linux/acpi.h>
#include <linux/aer.h>
#include <linux/async.h>
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
#include <linux/suspend.h>
#include <linux/t10-pi.h>
#include <linux/types.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/io-64-nonatomic-hi-lo.h>
#include <linux/sed-opal.h>
#include <linux/pci-p2pdma.h>

#include "trace.h"
#include "nvme.h"

#define SQ_SIZE(q)	((q)->q_depth << (q)->sqes)
#define CQ_SIZE(q)	((q)->q_depth * sizeof(struct nvme_completion))

#define SGES_PER_PAGE	(PAGE_SIZE / sizeof(struct nvme_sgl_desc))

/*
 * These can be higher, but we need to ensure that any command doesn't
 * require an sg allocation that needs more than a page of data.
 */
#define NVME_MAX_KB_SZ	4096
#define NVME_MAX_SEGS	127

static int use_threaded_interrupts;
module_param(use_threaded_interrupts, int, 0);

static bool use_cmb_sqes = true;
module_param(use_cmb_sqes, bool, 0444);
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

#define NVME_PCI_MIN_QUEUE_SIZE 2
#define NVME_PCI_MAX_QUEUE_SIZE 4095
static int io_queue_depth_set(const char *val, const struct kernel_param *kp);
static const struct kernel_param_ops io_queue_depth_ops = {
	.set = io_queue_depth_set,
	.get = param_get_uint,
};

static unsigned int io_queue_depth = 1024;
module_param_cb(io_queue_depth, &io_queue_depth_ops, &io_queue_depth, 0644);
MODULE_PARM_DESC(io_queue_depth, "set io queue depth, should >= 2 and < 4096");

static int io_queue_count_set(const char *val, const struct kernel_param *kp)
{
	unsigned int n;
	int ret;

	ret = kstrtouint(val, 10, &n);
	if (ret != 0 || n > num_possible_cpus())
		return -EINVAL;
	return param_set_uint(val, kp);
}

static const struct kernel_param_ops io_queue_count_ops = {
	.set = io_queue_count_set,
	.get = param_get_uint,
};

static unsigned int write_queues;
module_param_cb(write_queues, &io_queue_count_ops, &write_queues, 0644);
MODULE_PARM_DESC(write_queues,
	"Number of queues to use for writes. If not set, reads and writes "
	"will share a queue set.");

static unsigned int poll_queues;
module_param_cb(poll_queues, &io_queue_count_ops, &poll_queues, 0644);
MODULE_PARM_DESC(poll_queues, "Number of queues to use for polled IO.");

static bool noacpi;
module_param(noacpi, bool, 0444);
MODULE_PARM_DESC(noacpi, "disable acpi bios quirks");

struct nvme_dev;
struct nvme_queue;

static void nvme_dev_disable(struct nvme_dev *dev, bool shutdown);
static bool __nvme_disable_io_queues(struct nvme_dev *dev, u8 opcode);

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
	unsigned io_queues[HCTX_MAX_TYPES];
	unsigned int num_vecs;
	u32 q_depth;
	int io_sqes;
	u32 db_stride;
	void __iomem *bar;
	unsigned long bar_mapped_size;
	struct work_struct remove_work;
	struct mutex shutdown_lock;
	bool subsystem;
	u64 cmb_size;
	bool cmb_use_sqes;
	u32 cmbsz;
	u32 cmbloc;
	struct nvme_ctrl ctrl;
	u32 last_ps;
	bool hmb;

	mempool_t *iod_mempool;

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
	unsigned int nr_allocated_queues;
	unsigned int nr_write_queues;
	unsigned int nr_poll_queues;

	bool attrs_added;
};

static int io_queue_depth_set(const char *val, const struct kernel_param *kp)
{
	return param_set_uint_minmax(val, kp, NVME_PCI_MIN_QUEUE_SIZE,
			NVME_PCI_MAX_QUEUE_SIZE);
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
	struct nvme_dev *dev;
	spinlock_t sq_lock;
	void *sq_cmds;
	 /* only used for poll queues: */
	spinlock_t cq_poll_lock ____cacheline_aligned_in_smp;
	struct nvme_completion *cqes;
	dma_addr_t sq_dma_addr;
	dma_addr_t cq_dma_addr;
	u32 __iomem *q_db;
	u32 q_depth;
	u16 cq_vector;
	u16 sq_tail;
	u16 last_sq_tail;
	u16 cq_head;
	u16 qid;
	u8 cq_phase;
	u8 sqes;
	unsigned long flags;
#define NVMEQ_ENABLED		0
#define NVMEQ_SQ_CMB		1
#define NVMEQ_DELETE_ERROR	2
#define NVMEQ_POLLED		3
	u32 *dbbuf_sq_db;
	u32 *dbbuf_cq_db;
	u32 *dbbuf_sq_ei;
	u32 *dbbuf_cq_ei;
	struct completion delete_done;
};

/*
 * The nvme_iod describes the data in an I/O.
 *
 * The sg pointer contains the list of PRP/SGL chunk allocations in addition
 * to the actual struct scatterlist.
 */
struct nvme_iod {
	struct nvme_request req;
	struct nvme_command cmd;
	struct nvme_queue *nvmeq;
	bool use_sgl;
	int aborted;
	int npages;		/* In the PRP list. 0 means small pool in use */
	int nents;		/* Used in scatterlist */
	dma_addr_t first_dma;
	unsigned int dma_len;	/* length of single DMA segment mapping */
	dma_addr_t meta_dma;
	struct scatterlist *sg;
};

static inline unsigned int nvme_dbbuf_size(struct nvme_dev *dev)
{
	return dev->nr_allocated_queues * 8 * dev->db_stride;
}

static int nvme_dbbuf_dma_alloc(struct nvme_dev *dev)
{
	unsigned int mem_size = nvme_dbbuf_size(dev);

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
	unsigned int mem_size = nvme_dbbuf_size(dev);

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

static void nvme_dbbuf_free(struct nvme_queue *nvmeq)
{
	if (!nvmeq->qid)
		return;

	nvmeq->dbbuf_sq_db = NULL;
	nvmeq->dbbuf_cq_db = NULL;
	nvmeq->dbbuf_sq_ei = NULL;
	nvmeq->dbbuf_cq_ei = NULL;
}

static void nvme_dbbuf_set(struct nvme_dev *dev)
{
	struct nvme_command c = { };
	unsigned int i;

	if (!dev->dbbuf_dbs)
		return;

	c.dbbuf.opcode = nvme_admin_dbbuf;
	c.dbbuf.prp1 = cpu_to_le64(dev->dbbuf_dbs_dma_addr);
	c.dbbuf.prp2 = cpu_to_le64(dev->dbbuf_eis_dma_addr);

	if (nvme_submit_sync_cmd(dev->ctrl.admin_q, &c, NULL, 0)) {
		dev_warn(dev->ctrl.device, "unable to set dbbuf\n");
		/* Free memory and continue on */
		nvme_dbbuf_dma_free(dev);

		for (i = 1; i <= dev->online_queues; i++)
			nvme_dbbuf_free(&dev->queues[i]);
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

		/*
		 * Ensure that the doorbell is updated before reading the event
		 * index from memory.  The controller needs to provide similar
		 * ordering to ensure the envent index is updated before reading
		 * the doorbell.
		 */
		mb();

		if (!nvme_dbbuf_need_event(*dbbuf_ei, value, old_value))
			return false;
	}

	return true;
}

/*
 * Will slightly overestimate the number of pages needed.  This is OK
 * as it only leads to a small amount of wasted memory for the lifetime of
 * the I/O.
 */
static int nvme_pci_npages_prp(void)
{
	unsigned nprps = DIV_ROUND_UP(NVME_MAX_KB_SZ + NVME_CTRL_PAGE_SIZE,
				      NVME_CTRL_PAGE_SIZE);
	return DIV_ROUND_UP(8 * nprps, PAGE_SIZE - 8);
}

/*
 * Calculates the number of pages needed for the SGL segments. For example a 4k
 * page can accommodate 256 SGL descriptors.
 */
static int nvme_pci_npages_sgl(void)
{
	return DIV_ROUND_UP(NVME_MAX_SEGS * sizeof(struct nvme_sgl_desc),
			PAGE_SIZE);
}

static size_t nvme_pci_iod_alloc_size(void)
{
	size_t npages = max(nvme_pci_npages_prp(), nvme_pci_npages_sgl());

	return sizeof(__le64 *) * npages +
		sizeof(struct scatterlist) * NVME_MAX_SEGS;
}

static int nvme_admin_init_hctx(struct blk_mq_hw_ctx *hctx, void *data,
				unsigned int hctx_idx)
{
	struct nvme_dev *dev = data;
	struct nvme_queue *nvmeq = &dev->queues[0];

	WARN_ON(hctx_idx != 0);
	WARN_ON(dev->admin_tagset.tags[0] != hctx->tags);

	hctx->driver_data = nvmeq;
	return 0;
}

static int nvme_init_hctx(struct blk_mq_hw_ctx *hctx, void *data,
			  unsigned int hctx_idx)
{
	struct nvme_dev *dev = data;
	struct nvme_queue *nvmeq = &dev->queues[hctx_idx + 1];

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

	nvme_req(req)->ctrl = &dev->ctrl;
	nvme_req(req)->cmd = &iod->cmd;
	return 0;
}

static int queue_irq_offset(struct nvme_dev *dev)
{
	/* if we have more than 1 vec, admin queue offsets us by 1 */
	if (dev->num_vecs > 1)
		return 1;

	return 0;
}

static int nvme_pci_map_queues(struct blk_mq_tag_set *set)
{
	struct nvme_dev *dev = set->driver_data;
	int i, qoff, offset;

	offset = queue_irq_offset(dev);
	for (i = 0, qoff = 0; i < set->nr_maps; i++) {
		struct blk_mq_queue_map *map = &set->map[i];

		map->nr_queues = dev->io_queues[i];
		if (!map->nr_queues) {
			BUG_ON(i == HCTX_TYPE_DEFAULT);
			continue;
		}

		/*
		 * The poll queue(s) doesn't have an IRQ (and hence IRQ
		 * affinity), so use the regular blk-mq cpu mapping
		 */
		map->queue_offset = qoff;
		if (i != HCTX_TYPE_POLL && offset)
			blk_mq_pci_map_queues(map, to_pci_dev(dev->dev), offset);
		else
			blk_mq_map_queues(map);
		qoff += map->nr_queues;
		offset += map->nr_queues;
	}

	return 0;
}

/*
 * Write sq tail if we are asked to, or if the next command would wrap.
 */
static inline void nvme_write_sq_db(struct nvme_queue *nvmeq, bool write_sq)
{
	if (!write_sq) {
		u16 next_tail = nvmeq->sq_tail + 1;

		if (next_tail == nvmeq->q_depth)
			next_tail = 0;
		if (next_tail != nvmeq->last_sq_tail)
			return;
	}

	if (nvme_dbbuf_update_and_check_event(nvmeq->sq_tail,
			nvmeq->dbbuf_sq_db, nvmeq->dbbuf_sq_ei))
		writel(nvmeq->sq_tail, nvmeq->q_db);
	nvmeq->last_sq_tail = nvmeq->sq_tail;
}

/**
 * nvme_submit_cmd() - Copy a command into a queue and ring the doorbell
 * @nvmeq: The queue to use
 * @cmd: The command to send
 * @write_sq: whether to write to the SQ doorbell
 */
static void nvme_submit_cmd(struct nvme_queue *nvmeq, struct nvme_command *cmd,
			    bool write_sq)
{
	spin_lock(&nvmeq->sq_lock);
	memcpy(nvmeq->sq_cmds + (nvmeq->sq_tail << nvmeq->sqes),
	       cmd, sizeof(*cmd));
	if (++nvmeq->sq_tail == nvmeq->q_depth)
		nvmeq->sq_tail = 0;
	nvme_write_sq_db(nvmeq, write_sq);
	spin_unlock(&nvmeq->sq_lock);
}

static void nvme_commit_rqs(struct blk_mq_hw_ctx *hctx)
{
	struct nvme_queue *nvmeq = hctx->driver_data;

	spin_lock(&nvmeq->sq_lock);
	if (nvmeq->sq_tail != nvmeq->last_sq_tail)
		nvme_write_sq_db(nvmeq, true);
	spin_unlock(&nvmeq->sq_lock);
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

	avg_seg_size = DIV_ROUND_UP(blk_rq_payload_bytes(req), nseg);

	if (!nvme_ctrl_sgl_supported(&dev->ctrl))
		return false;
	if (!iod->nvmeq->qid)
		return false;
	if (!sgl_threshold || avg_seg_size < sgl_threshold)
		return false;
	return true;
}

static void nvme_free_prps(struct nvme_dev *dev, struct request *req)
{
	const int last_prp = NVME_CTRL_PAGE_SIZE / sizeof(__le64) - 1;
	struct nvme_iod *iod = blk_mq_rq_to_pdu(req);
	dma_addr_t dma_addr = iod->first_dma;
	int i;

	for (i = 0; i < iod->npages; i++) {
		__le64 *prp_list = nvme_pci_iod_list(req)[i];
		dma_addr_t next_dma_addr = le64_to_cpu(prp_list[last_prp]);

		dma_pool_free(dev->prp_page_pool, prp_list, dma_addr);
		dma_addr = next_dma_addr;
	}
}

static void nvme_free_sgls(struct nvme_dev *dev, struct request *req)
{
	const int last_sg = SGES_PER_PAGE - 1;
	struct nvme_iod *iod = blk_mq_rq_to_pdu(req);
	dma_addr_t dma_addr = iod->first_dma;
	int i;

	for (i = 0; i < iod->npages; i++) {
		struct nvme_sgl_desc *sg_list = nvme_pci_iod_list(req)[i];
		dma_addr_t next_dma_addr = le64_to_cpu((sg_list[last_sg]).addr);

		dma_pool_free(dev->prp_page_pool, sg_list, dma_addr);
		dma_addr = next_dma_addr;
	}
}

static void nvme_unmap_sg(struct nvme_dev *dev, struct request *req)
{
	struct nvme_iod *iod = blk_mq_rq_to_pdu(req);

	if (is_pci_p2pdma_page(sg_page(iod->sg)))
		pci_p2pdma_unmap_sg(dev->dev, iod->sg, iod->nents,
				    rq_dma_dir(req));
	else
		dma_unmap_sg(dev->dev, iod->sg, iod->nents, rq_dma_dir(req));
}

static void nvme_unmap_data(struct nvme_dev *dev, struct request *req)
{
	struct nvme_iod *iod = blk_mq_rq_to_pdu(req);

	if (iod->dma_len) {
		dma_unmap_page(dev->dev, iod->first_dma, iod->dma_len,
			       rq_dma_dir(req));
		return;
	}

	WARN_ON_ONCE(!iod->nents);

	nvme_unmap_sg(dev, req);
	if (iod->npages == 0)
		dma_pool_free(dev->prp_small_pool, nvme_pci_iod_list(req)[0],
			      iod->first_dma);
	else if (iod->use_sgl)
		nvme_free_sgls(dev, req);
	else
		nvme_free_prps(dev, req);
	mempool_free(iod->sg, dev->iod_mempool);
}

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
	int offset = dma_addr & (NVME_CTRL_PAGE_SIZE - 1);
	__le64 *prp_list;
	void **list = nvme_pci_iod_list(req);
	dma_addr_t prp_dma;
	int nprps, i;

	length -= (NVME_CTRL_PAGE_SIZE - offset);
	if (length <= 0) {
		iod->first_dma = 0;
		goto done;
	}

	dma_len -= (NVME_CTRL_PAGE_SIZE - offset);
	if (dma_len) {
		dma_addr += (NVME_CTRL_PAGE_SIZE - offset);
	} else {
		sg = sg_next(sg);
		dma_addr = sg_dma_address(sg);
		dma_len = sg_dma_len(sg);
	}

	if (length <= NVME_CTRL_PAGE_SIZE) {
		iod->first_dma = dma_addr;
		goto done;
	}

	nprps = DIV_ROUND_UP(length, NVME_CTRL_PAGE_SIZE);
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
		if (i == NVME_CTRL_PAGE_SIZE >> 3) {
			__le64 *old_prp_list = prp_list;
			prp_list = dma_pool_alloc(pool, GFP_ATOMIC, &prp_dma);
			if (!prp_list)
				goto free_prps;
			list[iod->npages++] = prp_list;
			prp_list[0] = old_prp_list[i - 1];
			old_prp_list[i - 1] = cpu_to_le64(prp_dma);
			i = 1;
		}
		prp_list[i++] = cpu_to_le64(dma_addr);
		dma_len -= NVME_CTRL_PAGE_SIZE;
		dma_addr += NVME_CTRL_PAGE_SIZE;
		length -= NVME_CTRL_PAGE_SIZE;
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
free_prps:
	nvme_free_prps(dev, req);
	return BLK_STS_RESOURCE;
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
				goto free_sgls;

			i = 0;
			nvme_pci_iod_list(req)[iod->npages++] = sg_list;
			sg_list[i++] = *link;
			nvme_pci_sgl_set_seg(link, sgl_dma, entries);
		}

		nvme_pci_sgl_set_data(&sg_list[i++], sg);
		sg = sg_next(sg);
	} while (--entries > 0);

	return BLK_STS_OK;
free_sgls:
	nvme_free_sgls(dev, req);
	return BLK_STS_RESOURCE;
}

static blk_status_t nvme_setup_prp_simple(struct nvme_dev *dev,
		struct request *req, struct nvme_rw_command *cmnd,
		struct bio_vec *bv)
{
	struct nvme_iod *iod = blk_mq_rq_to_pdu(req);
	unsigned int offset = bv->bv_offset & (NVME_CTRL_PAGE_SIZE - 1);
	unsigned int first_prp_len = NVME_CTRL_PAGE_SIZE - offset;

	iod->first_dma = dma_map_bvec(dev->dev, bv, rq_dma_dir(req), 0);
	if (dma_mapping_error(dev->dev, iod->first_dma))
		return BLK_STS_RESOURCE;
	iod->dma_len = bv->bv_len;

	cmnd->dptr.prp1 = cpu_to_le64(iod->first_dma);
	if (bv->bv_len > first_prp_len)
		cmnd->dptr.prp2 = cpu_to_le64(iod->first_dma + first_prp_len);
	return BLK_STS_OK;
}

static blk_status_t nvme_setup_sgl_simple(struct nvme_dev *dev,
		struct request *req, struct nvme_rw_command *cmnd,
		struct bio_vec *bv)
{
	struct nvme_iod *iod = blk_mq_rq_to_pdu(req);

	iod->first_dma = dma_map_bvec(dev->dev, bv, rq_dma_dir(req), 0);
	if (dma_mapping_error(dev->dev, iod->first_dma))
		return BLK_STS_RESOURCE;
	iod->dma_len = bv->bv_len;

	cmnd->flags = NVME_CMD_SGL_METABUF;
	cmnd->dptr.sgl.addr = cpu_to_le64(iod->first_dma);
	cmnd->dptr.sgl.length = cpu_to_le32(iod->dma_len);
	cmnd->dptr.sgl.type = NVME_SGL_FMT_DATA_DESC << 4;
	return BLK_STS_OK;
}

static blk_status_t nvme_map_data(struct nvme_dev *dev, struct request *req,
		struct nvme_command *cmnd)
{
	struct nvme_iod *iod = blk_mq_rq_to_pdu(req);
	blk_status_t ret = BLK_STS_RESOURCE;
	int nr_mapped;

	if (blk_rq_nr_phys_segments(req) == 1) {
		struct bio_vec bv = req_bvec(req);

		if (!is_pci_p2pdma_page(bv.bv_page)) {
			if (bv.bv_offset + bv.bv_len <= NVME_CTRL_PAGE_SIZE * 2)
				return nvme_setup_prp_simple(dev, req,
							     &cmnd->rw, &bv);

			if (iod->nvmeq->qid && sgl_threshold &&
			    nvme_ctrl_sgl_supported(&dev->ctrl))
				return nvme_setup_sgl_simple(dev, req,
							     &cmnd->rw, &bv);
		}
	}

	iod->dma_len = 0;
	iod->sg = mempool_alloc(dev->iod_mempool, GFP_ATOMIC);
	if (!iod->sg)
		return BLK_STS_RESOURCE;
	sg_init_table(iod->sg, blk_rq_nr_phys_segments(req));
	iod->nents = blk_rq_map_sg(req->q, req, iod->sg);
	if (!iod->nents)
		goto out_free_sg;

	if (is_pci_p2pdma_page(sg_page(iod->sg)))
		nr_mapped = pci_p2pdma_map_sg_attrs(dev->dev, iod->sg,
				iod->nents, rq_dma_dir(req), DMA_ATTR_NO_WARN);
	else
		nr_mapped = dma_map_sg_attrs(dev->dev, iod->sg, iod->nents,
					     rq_dma_dir(req), DMA_ATTR_NO_WARN);
	if (!nr_mapped)
		goto out_free_sg;

	iod->use_sgl = nvme_pci_use_sgls(dev, req);
	if (iod->use_sgl)
		ret = nvme_pci_setup_sgls(dev, req, &cmnd->rw, nr_mapped);
	else
		ret = nvme_pci_setup_prps(dev, req, &cmnd->rw);
	if (ret != BLK_STS_OK)
		goto out_unmap_sg;
	return BLK_STS_OK;

out_unmap_sg:
	nvme_unmap_sg(dev, req);
out_free_sg:
	mempool_free(iod->sg, dev->iod_mempool);
	return ret;
}

static blk_status_t nvme_map_metadata(struct nvme_dev *dev, struct request *req,
		struct nvme_command *cmnd)
{
	struct nvme_iod *iod = blk_mq_rq_to_pdu(req);

	iod->meta_dma = dma_map_bvec(dev->dev, rq_integrity_vec(req),
			rq_dma_dir(req), 0);
	if (dma_mapping_error(dev->dev, iod->meta_dma))
		return BLK_STS_IOERR;
	cmnd->rw.metadata = cpu_to_le64(iod->meta_dma);
	return BLK_STS_OK;
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
	struct nvme_iod *iod = blk_mq_rq_to_pdu(req);
	struct nvme_command *cmnd = &iod->cmd;
	blk_status_t ret;

	iod->aborted = 0;
	iod->npages = -1;
	iod->nents = 0;

	/*
	 * We should not need to do this, but we're still using this to
	 * ensure we can drain requests on a dying queue.
	 */
	if (unlikely(!test_bit(NVMEQ_ENABLED, &nvmeq->flags)))
		return BLK_STS_IOERR;

	if (!nvme_check_ready(&dev->ctrl, req, true))
		return nvme_fail_nonready_command(&dev->ctrl, req);

	ret = nvme_setup_cmd(ns, req);
	if (ret)
		return ret;

	if (blk_rq_nr_phys_segments(req)) {
		ret = nvme_map_data(dev, req, cmnd);
		if (ret)
			goto out_free_cmd;
	}

	if (blk_integrity_rq(req)) {
		ret = nvme_map_metadata(dev, req, cmnd);
		if (ret)
			goto out_unmap_data;
	}

	blk_mq_start_request(req);
	nvme_submit_cmd(nvmeq, cmnd, bd->last);
	return BLK_STS_OK;
out_unmap_data:
	nvme_unmap_data(dev, req);
out_free_cmd:
	nvme_cleanup_cmd(req);
	return ret;
}

static void nvme_pci_complete_rq(struct request *req)
{
	struct nvme_iod *iod = blk_mq_rq_to_pdu(req);
	struct nvme_dev *dev = iod->nvmeq->dev;

	if (blk_integrity_rq(req))
		dma_unmap_page(dev->dev, iod->meta_dma,
			       rq_integrity_vec(req)->bv_len, rq_data_dir(req));
	if (blk_rq_nr_phys_segments(req))
		nvme_unmap_data(dev, req);
	nvme_complete_rq(req);
}

/* We read the CQE phase first to check if the rest of the entry is valid */
static inline bool nvme_cqe_pending(struct nvme_queue *nvmeq)
{
	struct nvme_completion *hcqe = &nvmeq->cqes[nvmeq->cq_head];

	return (le16_to_cpu(READ_ONCE(hcqe->status)) & 1) == nvmeq->cq_phase;
}

static inline void nvme_ring_cq_doorbell(struct nvme_queue *nvmeq)
{
	u16 head = nvmeq->cq_head;

	if (nvme_dbbuf_update_and_check_event(head, nvmeq->dbbuf_cq_db,
					      nvmeq->dbbuf_cq_ei))
		writel(head, nvmeq->q_db + nvmeq->dev->db_stride);
}

static inline struct blk_mq_tags *nvme_queue_tagset(struct nvme_queue *nvmeq)
{
	if (!nvmeq->qid)
		return nvmeq->dev->admin_tagset.tags[0];
	return nvmeq->dev->tagset.tags[nvmeq->qid - 1];
}

static inline void nvme_handle_cqe(struct nvme_queue *nvmeq, u16 idx)
{
	struct nvme_completion *cqe = &nvmeq->cqes[idx];
	__u16 command_id = READ_ONCE(cqe->command_id);
	struct request *req;

	/*
	 * AEN requests are special as they don't time out and can
	 * survive any kind of queue freeze and often don't respond to
	 * aborts.  We don't even bother to allocate a struct request
	 * for them but rather special case them here.
	 */
	if (unlikely(nvme_is_aen_req(nvmeq->qid, command_id))) {
		nvme_complete_async_event(&nvmeq->dev->ctrl,
				cqe->status, &cqe->result);
		return;
	}

	req = nvme_find_rq(nvme_queue_tagset(nvmeq), command_id);
	if (unlikely(!req)) {
		dev_warn(nvmeq->dev->ctrl.device,
			"invalid id %d completed on queue %d\n",
			command_id, le16_to_cpu(cqe->sq_id));
		return;
	}

	trace_nvme_sq(req, cqe->sq_head, nvmeq->sq_tail);
	if (!nvme_try_complete_req(req, cqe->status, cqe->result))
		nvme_pci_complete_rq(req);
}

static inline void nvme_update_cq_head(struct nvme_queue *nvmeq)
{
	u32 tmp = nvmeq->cq_head + 1;

	if (tmp == nvmeq->q_depth) {
		nvmeq->cq_head = 0;
		nvmeq->cq_phase ^= 1;
	} else {
		nvmeq->cq_head = tmp;
	}
}

static inline int nvme_process_cq(struct nvme_queue *nvmeq)
{
	int found = 0;

	while (nvme_cqe_pending(nvmeq)) {
		found++;
		/*
		 * load-load control dependency between phase and the rest of
		 * the cqe requires a full read memory barrier
		 */
		dma_rmb();
		nvme_handle_cqe(nvmeq, nvmeq->cq_head);
		nvme_update_cq_head(nvmeq);
	}

	if (found)
		nvme_ring_cq_doorbell(nvmeq);
	return found;
}

static irqreturn_t nvme_irq(int irq, void *data)
{
	struct nvme_queue *nvmeq = data;

	if (nvme_process_cq(nvmeq))
		return IRQ_HANDLED;
	return IRQ_NONE;
}

static irqreturn_t nvme_irq_check(int irq, void *data)
{
	struct nvme_queue *nvmeq = data;

	if (nvme_cqe_pending(nvmeq))
		return IRQ_WAKE_THREAD;
	return IRQ_NONE;
}

/*
 * Poll for completions for any interrupt driven queue
 * Can be called from any context.
 */
static void nvme_poll_irqdisable(struct nvme_queue *nvmeq)
{
	struct pci_dev *pdev = to_pci_dev(nvmeq->dev->dev);

	WARN_ON_ONCE(test_bit(NVMEQ_POLLED, &nvmeq->flags));

	disable_irq(pci_irq_vector(pdev, nvmeq->cq_vector));
	nvme_process_cq(nvmeq);
	enable_irq(pci_irq_vector(pdev, nvmeq->cq_vector));
}

static int nvme_poll(struct blk_mq_hw_ctx *hctx)
{
	struct nvme_queue *nvmeq = hctx->driver_data;
	bool found;

	if (!nvme_cqe_pending(nvmeq))
		return 0;

	spin_lock(&nvmeq->cq_poll_lock);
	found = nvme_process_cq(nvmeq);
	spin_unlock(&nvmeq->cq_poll_lock);

	return found;
}

static void nvme_pci_submit_async_event(struct nvme_ctrl *ctrl)
{
	struct nvme_dev *dev = to_nvme_dev(ctrl);
	struct nvme_queue *nvmeq = &dev->queues[0];
	struct nvme_command c = { };

	c.common.opcode = nvme_admin_async_event;
	c.common.command_id = NVME_AQ_BLK_MQ_DEPTH;
	nvme_submit_cmd(nvmeq, &c, true);
}

static int adapter_delete_queue(struct nvme_dev *dev, u8 opcode, u16 id)
{
	struct nvme_command c = { };

	c.delete_queue.opcode = opcode;
	c.delete_queue.qid = cpu_to_le16(id);

	return nvme_submit_sync_cmd(dev->ctrl.admin_q, &c, NULL, 0);
}

static int adapter_alloc_cq(struct nvme_dev *dev, u16 qid,
		struct nvme_queue *nvmeq, s16 vector)
{
	struct nvme_command c = { };
	int flags = NVME_QUEUE_PHYS_CONTIG;

	if (!test_bit(NVMEQ_POLLED, &nvmeq->flags))
		flags |= NVME_CQ_IRQ_ENABLED;

	/*
	 * Note: we (ab)use the fact that the prp fields survive if no data
	 * is attached to the request.
	 */
	c.create_cq.opcode = nvme_admin_create_cq;
	c.create_cq.prp1 = cpu_to_le64(nvmeq->cq_dma_addr);
	c.create_cq.cqid = cpu_to_le16(qid);
	c.create_cq.qsize = cpu_to_le16(nvmeq->q_depth - 1);
	c.create_cq.cq_flags = cpu_to_le16(flags);
	c.create_cq.irq_vector = cpu_to_le16(vector);

	return nvme_submit_sync_cmd(dev->ctrl.admin_q, &c, NULL, 0);
}

static int adapter_alloc_sq(struct nvme_dev *dev, u16 qid,
						struct nvme_queue *nvmeq)
{
	struct nvme_ctrl *ctrl = &dev->ctrl;
	struct nvme_command c = { };
	int flags = NVME_QUEUE_PHYS_CONTIG;

	/*
	 * Some drives have a bug that auto-enables WRRU if MEDIUM isn't
	 * set. Since URGENT priority is zeroes, it makes all queues
	 * URGENT.
	 */
	if (ctrl->quirks & NVME_QUIRK_MEDIUM_PRIO_SQ)
		flags |= NVME_SQ_PRIO_MEDIUM;

	/*
	 * Note: we (ab)use the fact that the prp fields survive if no data
	 * is attached to the request.
	 */
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
	struct nvme_command cmd = { };
	u32 csts = readl(dev->bar + NVME_REG_CSTS);

	/* If PCI error recovery process is happening, we cannot reset or
	 * the recovery mechanism will surely fail.
	 */
	mb();
	if (pci_channel_offline(to_pci_dev(dev->dev)))
		return BLK_EH_RESET_TIMER;

	/*
	 * Reset immediately if the controller is failed
	 */
	if (nvme_should_reset(dev, csts)) {
		nvme_warn_reset(dev, csts);
		nvme_dev_disable(dev, false);
		nvme_reset_ctrl(&dev->ctrl);
		return BLK_EH_DONE;
	}

	/*
	 * Did we miss an interrupt?
	 */
	if (test_bit(NVMEQ_POLLED, &nvmeq->flags))
		nvme_poll(req->mq_hctx);
	else
		nvme_poll_irqdisable(nvmeq);

	if (blk_mq_request_completed(req)) {
		dev_warn(dev->ctrl.device,
			 "I/O %d QID %d timeout, completion polled\n",
			 req->tag, nvmeq->qid);
		return BLK_EH_DONE;
	}

	/*
	 * Shutdown immediately if controller times out while starting. The
	 * reset work will see the pci device disabled when it gets the forced
	 * cancellation error. All outstanding requests are completed on
	 * shutdown, so we return BLK_EH_DONE.
	 */
	switch (dev->ctrl.state) {
	case NVME_CTRL_CONNECTING:
		nvme_change_ctrl_state(&dev->ctrl, NVME_CTRL_DELETING);
		fallthrough;
	case NVME_CTRL_DELETING:
		dev_warn_ratelimited(dev->ctrl.device,
			 "I/O %d QID %d timeout, disable controller\n",
			 req->tag, nvmeq->qid);
		nvme_req(req)->flags |= NVME_REQ_CANCELLED;
		nvme_dev_disable(dev, true);
		return BLK_EH_DONE;
	case NVME_CTRL_RESETTING:
		return BLK_EH_RESET_TIMER;
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
		nvme_req(req)->flags |= NVME_REQ_CANCELLED;
		nvme_dev_disable(dev, false);
		nvme_reset_ctrl(&dev->ctrl);

		return BLK_EH_DONE;
	}

	if (atomic_dec_return(&dev->ctrl.abort_limit) < 0) {
		atomic_inc(&dev->ctrl.abort_limit);
		return BLK_EH_RESET_TIMER;
	}
	iod->aborted = 1;

	cmd.abort.opcode = nvme_admin_abort_cmd;
	cmd.abort.cid = nvme_cid(req);
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

	abort_req->end_io_data = NULL;
	blk_execute_rq_nowait(NULL, abort_req, 0, abort_endio);

	/*
	 * The aborted req will be completed on receiving the abort req.
	 * We enable the timer again. If hit twice, it'll cause a device reset,
	 * as the device then is in a faulty state.
	 */
	return BLK_EH_RESET_TIMER;
}

static void nvme_free_queue(struct nvme_queue *nvmeq)
{
	dma_free_coherent(nvmeq->dev->dev, CQ_SIZE(nvmeq),
				(void *)nvmeq->cqes, nvmeq->cq_dma_addr);
	if (!nvmeq->sq_cmds)
		return;

	if (test_and_clear_bit(NVMEQ_SQ_CMB, &nvmeq->flags)) {
		pci_free_p2pmem(to_pci_dev(nvmeq->dev->dev),
				nvmeq->sq_cmds, SQ_SIZE(nvmeq));
	} else {
		dma_free_coherent(nvmeq->dev->dev, SQ_SIZE(nvmeq),
				nvmeq->sq_cmds, nvmeq->sq_dma_addr);
	}
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
 * @nvmeq: queue to suspend
 */
static int nvme_suspend_queue(struct nvme_queue *nvmeq)
{
	if (!test_and_clear_bit(NVMEQ_ENABLED, &nvmeq->flags))
		return 1;

	/* ensure that nvme_queue_rq() sees NVMEQ_ENABLED cleared */
	mb();

	nvmeq->dev->online_queues--;
	if (!nvmeq->qid && nvmeq->dev->ctrl.admin_q)
		blk_mq_quiesce_queue(nvmeq->dev->ctrl.admin_q);
	if (!test_and_clear_bit(NVMEQ_POLLED, &nvmeq->flags))
		pci_free_irq(to_pci_dev(nvmeq->dev->dev), nvmeq->cq_vector, nvmeq);
	return 0;
}

static void nvme_suspend_io_queues(struct nvme_dev *dev)
{
	int i;

	for (i = dev->ctrl.queue_count - 1; i > 0; i--)
		nvme_suspend_queue(&dev->queues[i]);
}

static void nvme_disable_admin_queue(struct nvme_dev *dev, bool shutdown)
{
	struct nvme_queue *nvmeq = &dev->queues[0];

	if (shutdown)
		nvme_shutdown_ctrl(&dev->ctrl);
	else
		nvme_disable_ctrl(&dev->ctrl);

	nvme_poll_irqdisable(nvmeq);
}

/*
 * Called only on a device that has been disabled and after all other threads
 * that can check this device's completion queues have synced, except
 * nvme_poll(). This is the last chance for the driver to see a natural
 * completion before nvme_cancel_request() terminates all incomplete requests.
 */
static void nvme_reap_pending_cqes(struct nvme_dev *dev)
{
	int i;

	for (i = dev->ctrl.queue_count - 1; i > 0; i--) {
		spin_lock(&dev->queues[i].cq_poll_lock);
		nvme_process_cq(&dev->queues[i]);
		spin_unlock(&dev->queues[i].cq_poll_lock);
	}
}

static int nvme_cmb_qdepth(struct nvme_dev *dev, int nr_io_queues,
				int entry_size)
{
	int q_depth = dev->q_depth;
	unsigned q_size_aligned = roundup(q_depth * entry_size,
					  NVME_CTRL_PAGE_SIZE);

	if (q_size_aligned * nr_io_queues > dev->cmb_size) {
		u64 mem_per_q = div_u64(dev->cmb_size, nr_io_queues);

		mem_per_q = round_down(mem_per_q, NVME_CTRL_PAGE_SIZE);
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
				int qid)
{
	struct pci_dev *pdev = to_pci_dev(dev->dev);

	if (qid && dev->cmb_use_sqes && (dev->cmbsz & NVME_CMBSZ_SQS)) {
		nvmeq->sq_cmds = pci_alloc_p2pmem(pdev, SQ_SIZE(nvmeq));
		if (nvmeq->sq_cmds) {
			nvmeq->sq_dma_addr = pci_p2pmem_virt_to_bus(pdev,
							nvmeq->sq_cmds);
			if (nvmeq->sq_dma_addr) {
				set_bit(NVMEQ_SQ_CMB, &nvmeq->flags);
				return 0;
			}

			pci_free_p2pmem(pdev, nvmeq->sq_cmds, SQ_SIZE(nvmeq));
		}
	}

	nvmeq->sq_cmds = dma_alloc_coherent(dev->dev, SQ_SIZE(nvmeq),
				&nvmeq->sq_dma_addr, GFP_KERNEL);
	if (!nvmeq->sq_cmds)
		return -ENOMEM;
	return 0;
}

static int nvme_alloc_queue(struct nvme_dev *dev, int qid, int depth)
{
	struct nvme_queue *nvmeq = &dev->queues[qid];

	if (dev->ctrl.queue_count > qid)
		return 0;

	nvmeq->sqes = qid ? dev->io_sqes : NVME_ADM_SQES;
	nvmeq->q_depth = depth;
	nvmeq->cqes = dma_alloc_coherent(dev->dev, CQ_SIZE(nvmeq),
					 &nvmeq->cq_dma_addr, GFP_KERNEL);
	if (!nvmeq->cqes)
		goto free_nvmeq;

	if (nvme_alloc_sq_cmds(dev, nvmeq, qid))
		goto free_cqdma;

	nvmeq->dev = dev;
	spin_lock_init(&nvmeq->sq_lock);
	spin_lock_init(&nvmeq->cq_poll_lock);
	nvmeq->cq_head = 0;
	nvmeq->cq_phase = 1;
	nvmeq->q_db = &dev->dbs[qid * 2 * dev->db_stride];
	nvmeq->qid = qid;
	dev->ctrl.queue_count++;

	return 0;

 free_cqdma:
	dma_free_coherent(dev->dev, CQ_SIZE(nvmeq), (void *)nvmeq->cqes,
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

	nvmeq->sq_tail = 0;
	nvmeq->last_sq_tail = 0;
	nvmeq->cq_head = 0;
	nvmeq->cq_phase = 1;
	nvmeq->q_db = &dev->dbs[qid * 2 * dev->db_stride];
	memset((void *)nvmeq->cqes, 0, CQ_SIZE(nvmeq));
	nvme_dbbuf_init(dev, nvmeq, qid);
	dev->online_queues++;
	wmb(); /* ensure the first interrupt sees the initialization */
}

/*
 * Try getting shutdown_lock while setting up IO queues.
 */
static int nvme_setup_io_queues_trylock(struct nvme_dev *dev)
{
	/*
	 * Give up if the lock is being held by nvme_dev_disable.
	 */
	if (!mutex_trylock(&dev->shutdown_lock))
		return -ENODEV;

	/*
	 * Controller is in wrong state, fail early.
	 */
	if (dev->ctrl.state != NVME_CTRL_CONNECTING) {
		mutex_unlock(&dev->shutdown_lock);
		return -ENODEV;
	}

	return 0;
}

static int nvme_create_queue(struct nvme_queue *nvmeq, int qid, bool polled)
{
	struct nvme_dev *dev = nvmeq->dev;
	int result;
	u16 vector = 0;

	clear_bit(NVMEQ_DELETE_ERROR, &nvmeq->flags);

	/*
	 * A queue's vector matches the queue identifier unless the controller
	 * has only one vector available.
	 */
	if (!polled)
		vector = dev->num_vecs == 1 ? 0 : qid;
	else
		set_bit(NVMEQ_POLLED, &nvmeq->flags);

	result = adapter_alloc_cq(dev, qid, nvmeq, vector);
	if (result)
		return result;

	result = adapter_alloc_sq(dev, qid, nvmeq);
	if (result < 0)
		return result;
	if (result)
		goto release_cq;

	nvmeq->cq_vector = vector;

	result = nvme_setup_io_queues_trylock(dev);
	if (result)
		return result;
	nvme_init_queue(nvmeq, qid);
	if (!polled) {
		result = queue_request_irq(nvmeq);
		if (result < 0)
			goto release_sq;
	}

	set_bit(NVMEQ_ENABLED, &nvmeq->flags);
	mutex_unlock(&dev->shutdown_lock);
	return result;

release_sq:
	dev->online_queues--;
	mutex_unlock(&dev->shutdown_lock);
	adapter_delete_sq(dev, qid);
release_cq:
	adapter_delete_cq(dev, qid);
	return result;
}

static const struct blk_mq_ops nvme_mq_admin_ops = {
	.queue_rq	= nvme_queue_rq,
	.complete	= nvme_pci_complete_rq,
	.init_hctx	= nvme_admin_init_hctx,
	.init_request	= nvme_init_request,
	.timeout	= nvme_timeout,
};

static const struct blk_mq_ops nvme_mq_ops = {
	.queue_rq	= nvme_queue_rq,
	.complete	= nvme_pci_complete_rq,
	.commit_rqs	= nvme_commit_rqs,
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
		dev->admin_tagset.timeout = NVME_ADMIN_TIMEOUT;
		dev->admin_tagset.numa_node = dev->ctrl.numa_node;
		dev->admin_tagset.cmd_size = sizeof(struct nvme_iod);
		dev->admin_tagset.flags = BLK_MQ_F_NO_SCHED;
		dev->admin_tagset.driver_data = dev;

		if (blk_mq_alloc_tag_set(&dev->admin_tagset))
			return -ENOMEM;
		dev->ctrl.admin_tagset = &dev->admin_tagset;

		dev->ctrl.admin_q = blk_mq_init_queue(&dev->admin_tagset);
		if (IS_ERR(dev->ctrl.admin_q)) {
			blk_mq_free_tag_set(&dev->admin_tagset);
			dev->ctrl.admin_q = NULL;
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

	result = nvme_disable_ctrl(&dev->ctrl);
	if (result < 0)
		return result;

	result = nvme_alloc_queue(dev, 0, NVME_AQ_DEPTH);
	if (result)
		return result;

	dev->ctrl.numa_node = dev_to_node(dev->dev);

	nvmeq = &dev->queues[0];
	aqa = nvmeq->q_depth - 1;
	aqa |= aqa << 16;

	writel(aqa, dev->bar + NVME_REG_AQA);
	lo_hi_writeq(nvmeq->sq_dma_addr, dev->bar + NVME_REG_ASQ);
	lo_hi_writeq(nvmeq->cq_dma_addr, dev->bar + NVME_REG_ACQ);

	result = nvme_enable_ctrl(&dev->ctrl);
	if (result)
		return result;

	nvmeq->cq_vector = 0;
	nvme_init_queue(nvmeq, 0);
	result = queue_request_irq(nvmeq);
	if (result) {
		dev->online_queues--;
		return result;
	}

	set_bit(NVMEQ_ENABLED, &nvmeq->flags);
	return result;
}

static int nvme_create_io_queues(struct nvme_dev *dev)
{
	unsigned i, max, rw_queues;
	int ret = 0;

	for (i = dev->ctrl.queue_count; i <= dev->max_qid; i++) {
		if (nvme_alloc_queue(dev, i, dev->q_depth)) {
			ret = -ENOMEM;
			break;
		}
	}

	max = min(dev->max_qid, dev->ctrl.queue_count - 1);
	if (max != 1 && dev->io_queues[HCTX_TYPE_POLL]) {
		rw_queues = dev->io_queues[HCTX_TYPE_DEFAULT] +
				dev->io_queues[HCTX_TYPE_READ];
	} else {
		rw_queues = max;
	}

	for (i = dev->online_queues; i <= max; i++) {
		bool polled = i > rw_queues;

		ret = nvme_create_queue(&dev->queues[i], i, polled);
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

	if (dev->cmb_size)
		return;

	if (NVME_CAP_CMBS(dev->ctrl.cap))
		writel(NVME_CMBMSC_CRE, dev->bar + NVME_REG_CMBMSC);

	dev->cmbsz = readl(dev->bar + NVME_REG_CMBSZ);
	if (!dev->cmbsz)
		return;
	dev->cmbloc = readl(dev->bar + NVME_REG_CMBLOC);

	size = nvme_cmb_size_unit(dev) * nvme_cmb_size(dev);
	offset = nvme_cmb_size_unit(dev) * NVME_CMB_OFST(dev->cmbloc);
	bar = NVME_CMB_BIR(dev->cmbloc);
	bar_size = pci_resource_len(pdev, bar);

	if (offset > bar_size)
		return;

	/*
	 * Tell the controller about the host side address mapping the CMB,
	 * and enable CMB decoding for the NVMe 1.4+ scheme:
	 */
	if (NVME_CAP_CMBS(dev->ctrl.cap)) {
		hi_lo_writeq(NVME_CMBMSC_CRE | NVME_CMBMSC_CMSE |
			     (pci_bus_address(pdev, bar) + offset),
			     dev->bar + NVME_REG_CMBMSC);
	}

	/*
	 * Controllers may support a CMB size larger than their BAR,
	 * for example, due to being behind a bridge. Reduce the CMB to
	 * the reported size of the BAR
	 */
	if (size > bar_size - offset)
		size = bar_size - offset;

	if (pci_p2pdma_add_resource(pdev, bar, size, offset)) {
		dev_warn(dev->ctrl.device,
			 "failed to register the CMB\n");
		return;
	}

	dev->cmb_size = size;
	dev->cmb_use_sqes = use_cmb_sqes && (dev->cmbsz & NVME_CMBSZ_SQS);

	if ((dev->cmbsz & (NVME_CMBSZ_WDS | NVME_CMBSZ_RDS)) ==
			(NVME_CMBSZ_WDS | NVME_CMBSZ_RDS))
		pci_p2pmem_publish(pdev, true);
}

static int nvme_set_host_mem(struct nvme_dev *dev, u32 bits)
{
	u32 host_mem_size = dev->host_mem_size >> NVME_CTRL_PAGE_SHIFT;
	u64 dma_addr = dev->host_mem_descs_dma;
	struct nvme_command c = { };
	int ret;

	c.features.opcode	= nvme_admin_set_features;
	c.features.fid		= cpu_to_le32(NVME_FEAT_HOST_MEM_BUF);
	c.features.dword11	= cpu_to_le32(bits);
	c.features.dword12	= cpu_to_le32(host_mem_size);
	c.features.dword13	= cpu_to_le32(lower_32_bits(dma_addr));
	c.features.dword14	= cpu_to_le32(upper_32_bits(dma_addr));
	c.features.dword15	= cpu_to_le32(dev->nr_host_mem_descs);

	ret = nvme_submit_sync_cmd(dev->ctrl.admin_q, &c, NULL, 0);
	if (ret) {
		dev_warn(dev->ctrl.device,
			 "failed to set host mem (err %d, flags %#x).\n",
			 ret, bits);
	} else
		dev->hmb = bits & NVME_HOST_MEM_ENABLE;

	return ret;
}

static void nvme_free_host_mem(struct nvme_dev *dev)
{
	int i;

	for (i = 0; i < dev->nr_host_mem_descs; i++) {
		struct nvme_host_mem_buf_desc *desc = &dev->host_mem_descs[i];
		size_t size = le32_to_cpu(desc->size) * NVME_CTRL_PAGE_SIZE;

		dma_free_attrs(dev->dev, size, dev->host_mem_desc_bufs[i],
			       le64_to_cpu(desc->addr),
			       DMA_ATTR_NO_KERNEL_MAPPING | DMA_ATTR_NO_WARN);
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

	descs = dma_alloc_coherent(dev->dev, max_entries * sizeof(*descs),
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
		descs[i].size = cpu_to_le32(len / NVME_CTRL_PAGE_SIZE);
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
		size_t size = le32_to_cpu(descs[i].size) * NVME_CTRL_PAGE_SIZE;

		dma_free_attrs(dev->dev, size, bufs[i],
			       le64_to_cpu(descs[i].addr),
			       DMA_ATTR_NO_KERNEL_MAPPING | DMA_ATTR_NO_WARN);
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
	u64 min_chunk = min_t(u64, preferred, PAGE_SIZE * MAX_ORDER_NR_PAGES);
	u64 hmminds = max_t(u32, dev->ctrl.hmminds * 4096, PAGE_SIZE * 2);
	u64 chunk_size;

	/* start big and work our way down */
	for (chunk_size = min_chunk; chunk_size >= hmminds; chunk_size /= 2) {
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

static ssize_t cmb_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct nvme_dev *ndev = to_nvme_dev(dev_get_drvdata(dev));

	return sysfs_emit(buf, "cmbloc : x%08x\ncmbsz  : x%08x\n",
		       ndev->cmbloc, ndev->cmbsz);
}
static DEVICE_ATTR_RO(cmb);

static ssize_t cmbloc_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct nvme_dev *ndev = to_nvme_dev(dev_get_drvdata(dev));

	return sysfs_emit(buf, "%u\n", ndev->cmbloc);
}
static DEVICE_ATTR_RO(cmbloc);

static ssize_t cmbsz_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct nvme_dev *ndev = to_nvme_dev(dev_get_drvdata(dev));

	return sysfs_emit(buf, "%u\n", ndev->cmbsz);
}
static DEVICE_ATTR_RO(cmbsz);

static ssize_t hmb_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct nvme_dev *ndev = to_nvme_dev(dev_get_drvdata(dev));

	return sysfs_emit(buf, "%d\n", ndev->hmb);
}

static ssize_t hmb_store(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	struct nvme_dev *ndev = to_nvme_dev(dev_get_drvdata(dev));
	bool new;
	int ret;

	if (strtobool(buf, &new) < 0)
		return -EINVAL;

	if (new == ndev->hmb)
		return count;

	if (new) {
		ret = nvme_setup_host_mem(ndev);
	} else {
		ret = nvme_set_host_mem(ndev, 0);
		if (!ret)
			nvme_free_host_mem(ndev);
	}

	if (ret < 0)
		return ret;

	return count;
}
static DEVICE_ATTR_RW(hmb);

static umode_t nvme_pci_attrs_are_visible(struct kobject *kobj,
		struct attribute *a, int n)
{
	struct nvme_ctrl *ctrl =
		dev_get_drvdata(container_of(kobj, struct device, kobj));
	struct nvme_dev *dev = to_nvme_dev(ctrl);

	if (a == &dev_attr_cmb.attr ||
	    a == &dev_attr_cmbloc.attr ||
	    a == &dev_attr_cmbsz.attr) {
	    	if (!dev->cmbsz)
			return 0;
	}
	if (a == &dev_attr_hmb.attr && !ctrl->hmpre)
		return 0;

	return a->mode;
}

static struct attribute *nvme_pci_attrs[] = {
	&dev_attr_cmb.attr,
	&dev_attr_cmbloc.attr,
	&dev_attr_cmbsz.attr,
	&dev_attr_hmb.attr,
	NULL,
};

static const struct attribute_group nvme_pci_attr_group = {
	.attrs		= nvme_pci_attrs,
	.is_visible	= nvme_pci_attrs_are_visible,
};

/*
 * nirqs is the number of interrupts available for write and read
 * queues. The core already reserved an interrupt for the admin queue.
 */
static void nvme_calc_irq_sets(struct irq_affinity *affd, unsigned int nrirqs)
{
	struct nvme_dev *dev = affd->priv;
	unsigned int nr_read_queues, nr_write_queues = dev->nr_write_queues;

	/*
	 * If there is no interrupt available for queues, ensure that
	 * the default queue is set to 1. The affinity set size is
	 * also set to one, but the irq core ignores it for this case.
	 *
	 * If only one interrupt is available or 'write_queue' == 0, combine
	 * write and read queues.
	 *
	 * If 'write_queues' > 0, ensure it leaves room for at least one read
	 * queue.
	 */
	if (!nrirqs) {
		nrirqs = 1;
		nr_read_queues = 0;
	} else if (nrirqs == 1 || !nr_write_queues) {
		nr_read_queues = 0;
	} else if (nr_write_queues >= nrirqs) {
		nr_read_queues = 1;
	} else {
		nr_read_queues = nrirqs - nr_write_queues;
	}

	dev->io_queues[HCTX_TYPE_DEFAULT] = nrirqs - nr_read_queues;
	affd->set_size[HCTX_TYPE_DEFAULT] = nrirqs - nr_read_queues;
	dev->io_queues[HCTX_TYPE_READ] = nr_read_queues;
	affd->set_size[HCTX_TYPE_READ] = nr_read_queues;
	affd->nr_sets = nr_read_queues ? 2 : 1;
}

static int nvme_setup_irqs(struct nvme_dev *dev, unsigned int nr_io_queues)
{
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	struct irq_affinity affd = {
		.pre_vectors	= 1,
		.calc_sets	= nvme_calc_irq_sets,
		.priv		= dev,
	};
	unsigned int irq_queues, poll_queues;

	/*
	 * Poll queues don't need interrupts, but we need at least one I/O queue
	 * left over for non-polled I/O.
	 */
	poll_queues = min(dev->nr_poll_queues, nr_io_queues - 1);
	dev->io_queues[HCTX_TYPE_POLL] = poll_queues;

	/*
	 * Initialize for the single interrupt case, will be updated in
	 * nvme_calc_irq_sets().
	 */
	dev->io_queues[HCTX_TYPE_DEFAULT] = 1;
	dev->io_queues[HCTX_TYPE_READ] = 0;

	/*
	 * We need interrupts for the admin queue and each non-polled I/O queue,
	 * but some Apple controllers require all queues to use the first
	 * vector.
	 */
	irq_queues = 1;
	if (!(dev->ctrl.quirks & NVME_QUIRK_SINGLE_VECTOR))
		irq_queues += (nr_io_queues - poll_queues);
	return pci_alloc_irq_vectors_affinity(pdev, 1, irq_queues,
			      PCI_IRQ_ALL_TYPES | PCI_IRQ_AFFINITY, &affd);
}

static void nvme_disable_io_queues(struct nvme_dev *dev)
{
	if (__nvme_disable_io_queues(dev, nvme_admin_delete_sq))
		__nvme_disable_io_queues(dev, nvme_admin_delete_cq);
}

static unsigned int nvme_max_io_queues(struct nvme_dev *dev)
{
	/*
	 * If tags are shared with admin queue (Apple bug), then
	 * make sure we only use one IO queue.
	 */
	if (dev->ctrl.quirks & NVME_QUIRK_SHARED_TAGS)
		return 1;
	return num_possible_cpus() + dev->nr_write_queues + dev->nr_poll_queues;
}

static int nvme_setup_io_queues(struct nvme_dev *dev)
{
	struct nvme_queue *adminq = &dev->queues[0];
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	unsigned int nr_io_queues;
	unsigned long size;
	int result;

	/*
	 * Sample the module parameters once at reset time so that we have
	 * stable values to work with.
	 */
	dev->nr_write_queues = write_queues;
	dev->nr_poll_queues = poll_queues;

	nr_io_queues = dev->nr_allocated_queues - 1;
	result = nvme_set_queue_count(&dev->ctrl, &nr_io_queues);
	if (result < 0)
		return result;

	if (nr_io_queues == 0)
		return 0;

	/*
	 * Free IRQ resources as soon as NVMEQ_ENABLED bit transitions
	 * from set to unset. If there is a window to it is truely freed,
	 * pci_free_irq_vectors() jumping into this window will crash.
	 * And take lock to avoid racing with pci_free_irq_vectors() in
	 * nvme_dev_disable() path.
	 */
	result = nvme_setup_io_queues_trylock(dev);
	if (result)
		return result;
	if (test_and_clear_bit(NVMEQ_ENABLED, &adminq->flags))
		pci_free_irq(pdev, 0, adminq);

	if (dev->cmb_use_sqes) {
		result = nvme_cmb_qdepth(dev, nr_io_queues,
				sizeof(struct nvme_command));
		if (result > 0)
			dev->q_depth = result;
		else
			dev->cmb_use_sqes = false;
	}

	do {
		size = db_bar_size(dev, nr_io_queues);
		result = nvme_remap_bar(dev, size);
		if (!result)
			break;
		if (!--nr_io_queues) {
			result = -ENOMEM;
			goto out_unlock;
		}
	} while (1);
	adminq->q_db = dev->dbs;

 retry:
	/* Deregister the admin queue's interrupt */
	if (test_and_clear_bit(NVMEQ_ENABLED, &adminq->flags))
		pci_free_irq(pdev, 0, adminq);

	/*
	 * If we enable msix early due to not intx, disable it again before
	 * setting up the full range we need.
	 */
	pci_free_irq_vectors(pdev);

	result = nvme_setup_irqs(dev, nr_io_queues);
	if (result <= 0) {
		result = -EIO;
		goto out_unlock;
	}

	dev->num_vecs = result;
	result = max(result - 1, 1);
	dev->max_qid = result + dev->io_queues[HCTX_TYPE_POLL];

	/*
	 * Should investigate if there's a performance win from allocating
	 * more queues than interrupt vectors; it might allow the submission
	 * path to scale better, even if the receive path is limited by the
	 * number of interrupts.
	 */
	result = queue_request_irq(adminq);
	if (result)
		goto out_unlock;
	set_bit(NVMEQ_ENABLED, &adminq->flags);
	mutex_unlock(&dev->shutdown_lock);

	result = nvme_create_io_queues(dev);
	if (result || dev->online_queues < 2)
		return result;

	if (dev->online_queues - 1 < dev->max_qid) {
		nr_io_queues = dev->online_queues - 1;
		nvme_disable_io_queues(dev);
		result = nvme_setup_io_queues_trylock(dev);
		if (result)
			return result;
		nvme_suspend_io_queues(dev);
		goto retry;
	}
	dev_info(dev->ctrl.device, "%d/%d/%d default/read/poll queues\n",
					dev->io_queues[HCTX_TYPE_DEFAULT],
					dev->io_queues[HCTX_TYPE_READ],
					dev->io_queues[HCTX_TYPE_POLL]);
	return 0;
out_unlock:
	mutex_unlock(&dev->shutdown_lock);
	return result;
}

static void nvme_del_queue_end(struct request *req, blk_status_t error)
{
	struct nvme_queue *nvmeq = req->end_io_data;

	blk_mq_free_request(req);
	complete(&nvmeq->delete_done);
}

static void nvme_del_cq_end(struct request *req, blk_status_t error)
{
	struct nvme_queue *nvmeq = req->end_io_data;

	if (error)
		set_bit(NVMEQ_DELETE_ERROR, &nvmeq->flags);

	nvme_del_queue_end(req, error);
}

static int nvme_delete_queue(struct nvme_queue *nvmeq, u8 opcode)
{
	struct request_queue *q = nvmeq->dev->ctrl.admin_q;
	struct request *req;
	struct nvme_command cmd = { };

	cmd.delete_queue.opcode = opcode;
	cmd.delete_queue.qid = cpu_to_le16(nvmeq->qid);

	req = nvme_alloc_request(q, &cmd, BLK_MQ_REQ_NOWAIT);
	if (IS_ERR(req))
		return PTR_ERR(req);

	req->end_io_data = nvmeq;

	init_completion(&nvmeq->delete_done);
	blk_execute_rq_nowait(NULL, req, false,
			opcode == nvme_admin_delete_cq ?
				nvme_del_cq_end : nvme_del_queue_end);
	return 0;
}

static bool __nvme_disable_io_queues(struct nvme_dev *dev, u8 opcode)
{
	int nr_queues = dev->online_queues - 1, sent = 0;
	unsigned long timeout;

 retry:
	timeout = NVME_ADMIN_TIMEOUT;
	while (nr_queues > 0) {
		if (nvme_delete_queue(&dev->queues[nr_queues], opcode))
			break;
		nr_queues--;
		sent++;
	}
	while (sent) {
		struct nvme_queue *nvmeq = &dev->queues[nr_queues + sent];

		timeout = wait_for_completion_io_timeout(&nvmeq->delete_done,
				timeout);
		if (timeout == 0)
			return false;

		sent--;
		if (nr_queues)
			goto retry;
	}
	return true;
}

static void nvme_dev_add(struct nvme_dev *dev)
{
	int ret;

	if (!dev->ctrl.tagset) {
		dev->tagset.ops = &nvme_mq_ops;
		dev->tagset.nr_hw_queues = dev->online_queues - 1;
		dev->tagset.nr_maps = 2; /* default + read */
		if (dev->io_queues[HCTX_TYPE_POLL])
			dev->tagset.nr_maps++;
		dev->tagset.timeout = NVME_IO_TIMEOUT;
		dev->tagset.numa_node = dev->ctrl.numa_node;
		dev->tagset.queue_depth = min_t(unsigned int, dev->q_depth,
						BLK_MQ_MAX_DEPTH) - 1;
		dev->tagset.cmd_size = sizeof(struct nvme_iod);
		dev->tagset.flags = BLK_MQ_F_SHOULD_MERGE;
		dev->tagset.driver_data = dev;

		/*
		 * Some Apple controllers requires tags to be unique
		 * across admin and IO queue, so reserve the first 32
		 * tags of the IO queue.
		 */
		if (dev->ctrl.quirks & NVME_QUIRK_SHARED_TAGS)
			dev->tagset.reserved_tags = NVME_AQ_DEPTH;

		ret = blk_mq_alloc_tag_set(&dev->tagset);
		if (ret) {
			dev_warn(dev->ctrl.device,
				"IO queues tagset allocation failed %d\n", ret);
			return;
		}
		dev->ctrl.tagset = &dev->tagset;
	} else {
		blk_mq_update_nr_hw_queues(&dev->tagset, dev->online_queues - 1);

		/* Free previously allocated queues that are no longer usable */
		nvme_free_queues(dev, dev->online_queues);
	}

	nvme_dbbuf_set(dev);
}

static int nvme_pci_enable(struct nvme_dev *dev)
{
	int result = -ENOMEM;
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	int dma_address_bits = 64;

	if (pci_enable_device_mem(pdev))
		return result;

	pci_set_master(pdev);

	if (dev->ctrl.quirks & NVME_QUIRK_DMA_ADDRESS_BITS_48)
		dma_address_bits = 48;
	if (dma_set_mask_and_coherent(dev->dev, DMA_BIT_MASK(dma_address_bits)))
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

	dev->q_depth = min_t(u32, NVME_CAP_MQES(dev->ctrl.cap) + 1,
				io_queue_depth);
	dev->ctrl.sqsize = dev->q_depth - 1; /* 0's based queue depth */
	dev->db_stride = 1 << NVME_CAP_STRIDE(dev->ctrl.cap);
	dev->dbs = dev->bar + 4096;

	/*
	 * Some Apple controllers require a non-standard SQE size.
	 * Interestingly they also seem to ignore the CC:IOSQES register
	 * so we don't bother updating it here.
	 */
	if (dev->ctrl.quirks & NVME_QUIRK_128_BYTES_SQES)
		dev->io_sqes = 7;
	else
		dev->io_sqes = NVME_NVM_IOSQES;

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

	/*
	 * Controllers with the shared tags quirk need the IO queue to be
	 * big enough so that we get 32 tags for the admin queue
	 */
	if ((dev->ctrl.quirks & NVME_QUIRK_SHARED_TAGS) &&
	    (dev->q_depth < (NVME_AQ_DEPTH + 2))) {
		dev->q_depth = NVME_AQ_DEPTH + 2;
		dev_warn(dev->ctrl.device, "IO queue depth clamped to %d\n",
			 dev->q_depth);
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

	pci_free_irq_vectors(pdev);

	if (pci_is_enabled(pdev)) {
		pci_disable_pcie_error_reporting(pdev);
		pci_disable_device(pdev);
	}
}

static void nvme_dev_disable(struct nvme_dev *dev, bool shutdown)
{
	bool dead = true, freeze = false;
	struct pci_dev *pdev = to_pci_dev(dev->dev);

	mutex_lock(&dev->shutdown_lock);
	if (pci_is_enabled(pdev)) {
		u32 csts = readl(dev->bar + NVME_REG_CSTS);

		if (dev->ctrl.state == NVME_CTRL_LIVE ||
		    dev->ctrl.state == NVME_CTRL_RESETTING) {
			freeze = true;
			nvme_start_freeze(&dev->ctrl);
		}
		dead = !!((csts & NVME_CSTS_CFS) || !(csts & NVME_CSTS_RDY) ||
			pdev->error_state  != pci_channel_io_normal);
	}

	/*
	 * Give the controller a chance to complete all entered requests if
	 * doing a safe shutdown.
	 */
	if (!dead && shutdown && freeze)
		nvme_wait_freeze_timeout(&dev->ctrl, NVME_IO_TIMEOUT);

	nvme_stop_queues(&dev->ctrl);

	if (!dead && dev->ctrl.queue_count > 0) {
		nvme_disable_io_queues(dev);
		nvme_disable_admin_queue(dev, shutdown);
	}
	nvme_suspend_io_queues(dev);
	nvme_suspend_queue(&dev->queues[0]);
	nvme_pci_disable(dev);
	nvme_reap_pending_cqes(dev);

	blk_mq_tagset_busy_iter(&dev->tagset, nvme_cancel_request, &dev->ctrl);
	blk_mq_tagset_busy_iter(&dev->admin_tagset, nvme_cancel_request, &dev->ctrl);
	blk_mq_tagset_wait_completed_request(&dev->tagset);
	blk_mq_tagset_wait_completed_request(&dev->admin_tagset);

	/*
	 * The driver will not be starting up queues again if shutting down so
	 * must flush all entered requests to their failed completion to avoid
	 * deadlocking blk-mq hot-cpu notifier.
	 */
	if (shutdown) {
		nvme_start_queues(&dev->ctrl);
		if (dev->ctrl.admin_q && !blk_queue_dying(dev->ctrl.admin_q))
			blk_mq_unquiesce_queue(dev->ctrl.admin_q);
	}
	mutex_unlock(&dev->shutdown_lock);
}

static int nvme_disable_prepare_reset(struct nvme_dev *dev, bool shutdown)
{
	if (!nvme_wait_reset(&dev->ctrl))
		return -EBUSY;
	nvme_dev_disable(dev, shutdown);
	return 0;
}

static int nvme_setup_prp_pools(struct nvme_dev *dev)
{
	dev->prp_page_pool = dma_pool_create("prp list page", dev->dev,
						NVME_CTRL_PAGE_SIZE,
						NVME_CTRL_PAGE_SIZE, 0);
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

static void nvme_free_tagset(struct nvme_dev *dev)
{
	if (dev->tagset.tags)
		blk_mq_free_tag_set(&dev->tagset);
	dev->ctrl.tagset = NULL;
}

static void nvme_pci_free_ctrl(struct nvme_ctrl *ctrl)
{
	struct nvme_dev *dev = to_nvme_dev(ctrl);

	nvme_dbbuf_dma_free(dev);
	nvme_free_tagset(dev);
	if (dev->ctrl.admin_q)
		blk_put_queue(dev->ctrl.admin_q);
	free_opal_dev(dev->ctrl.opal_dev);
	mempool_destroy(dev->iod_mempool);
	put_device(dev->dev);
	kfree(dev->queues);
	kfree(dev);
}

static void nvme_remove_dead_ctrl(struct nvme_dev *dev)
{
	/*
	 * Set state to deleting now to avoid blocking nvme_wait_reset(), which
	 * may be holding this pci_dev's device lock.
	 */
	nvme_change_ctrl_state(&dev->ctrl, NVME_CTRL_DELETING);
	nvme_get_ctrl(&dev->ctrl);
	nvme_dev_disable(dev, false);
	nvme_kill_queues(&dev->ctrl);
	if (!queue_work(nvme_wq, &dev->remove_work))
		nvme_put_ctrl(&dev->ctrl);
}

static void nvme_reset_work(struct work_struct *work)
{
	struct nvme_dev *dev =
		container_of(work, struct nvme_dev, ctrl.reset_work);
	bool was_suspend = !!(dev->ctrl.ctrl_config & NVME_CC_SHN_NORMAL);
	int result;

	if (dev->ctrl.state != NVME_CTRL_RESETTING) {
		dev_warn(dev->ctrl.device, "ctrl state %d is not RESETTING\n",
			 dev->ctrl.state);
		result = -ENODEV;
		goto out;
	}

	/*
	 * If we're called to reset a live controller first shut it down before
	 * moving on.
	 */
	if (dev->ctrl.ctrl_config & NVME_CC_ENABLE)
		nvme_dev_disable(dev, false);
	nvme_sync_queues(&dev->ctrl);

	mutex_lock(&dev->shutdown_lock);
	result = nvme_pci_enable(dev);
	if (result)
		goto out_unlock;

	result = nvme_pci_configure_admin_queue(dev);
	if (result)
		goto out_unlock;

	result = nvme_alloc_admin_tags(dev);
	if (result)
		goto out_unlock;

	/*
	 * Limit the max command size to prevent iod->sg allocations going
	 * over a single page.
	 */
	dev->ctrl.max_hw_sectors = min_t(u32,
		NVME_MAX_KB_SZ << 1, dma_max_mapping_size(dev->dev) >> 9);
	dev->ctrl.max_segments = NVME_MAX_SEGS;

	/*
	 * Don't limit the IOMMU merged segment size.
	 */
	dma_set_max_seg_size(dev->dev, 0xffffffff);
	dma_set_min_align_mask(dev->dev, NVME_CTRL_PAGE_SIZE - 1);

	mutex_unlock(&dev->shutdown_lock);

	/*
	 * Introduce CONNECTING state from nvme-fc/rdma transports to mark the
	 * initializing procedure here.
	 */
	if (!nvme_change_ctrl_state(&dev->ctrl, NVME_CTRL_CONNECTING)) {
		dev_warn(dev->ctrl.device,
			"failed to mark controller CONNECTING\n");
		result = -EBUSY;
		goto out;
	}

	/*
	 * We do not support an SGL for metadata (yet), so we are limited to a
	 * single integrity segment for the separate metadata pointer.
	 */
	dev->ctrl.max_integrity_segments = 1;

	result = nvme_init_ctrl_finish(&dev->ctrl);
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
		nvme_free_tagset(dev);
	} else {
		nvme_start_queues(&dev->ctrl);
		nvme_wait_freeze(&dev->ctrl);
		nvme_dev_add(dev);
		nvme_unfreeze(&dev->ctrl);
	}

	/*
	 * If only admin queue live, keep it to do further investigation or
	 * recovery.
	 */
	if (!nvme_change_ctrl_state(&dev->ctrl, NVME_CTRL_LIVE)) {
		dev_warn(dev->ctrl.device,
			"failed to mark controller live state\n");
		result = -ENODEV;
		goto out;
	}

	if (!dev->attrs_added && !sysfs_create_group(&dev->ctrl.device->kobj,
			&nvme_pci_attr_group))
		dev->attrs_added = true;

	nvme_start_ctrl(&dev->ctrl);
	return;

 out_unlock:
	mutex_unlock(&dev->shutdown_lock);
 out:
	if (result)
		dev_warn(dev->ctrl.device,
			 "Removing after probe failure status: %d\n", result);
	nvme_remove_dead_ctrl(dev);
}

static void nvme_remove_dead_ctrl_work(struct work_struct *work)
{
	struct nvme_dev *dev = container_of(work, struct nvme_dev, remove_work);
	struct pci_dev *pdev = to_pci_dev(dev->dev);

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
	*val = lo_hi_readq(to_nvme_dev(ctrl)->bar + off);
	return 0;
}

static int nvme_pci_get_address(struct nvme_ctrl *ctrl, char *buf, int size)
{
	struct pci_dev *pdev = to_pci_dev(to_nvme_dev(ctrl)->dev);

	return snprintf(buf, size, "%s\n", dev_name(&pdev->dev));
}

static const struct nvme_ctrl_ops nvme_pci_ctrl_ops = {
	.name			= "pcie",
	.module			= THIS_MODULE,
	.flags			= NVME_F_METADATA_SUPPORTED |
				  NVME_F_PCI_P2PDMA,
	.reg_read32		= nvme_pci_reg_read32,
	.reg_write32		= nvme_pci_reg_write32,
	.reg_read64		= nvme_pci_reg_read64,
	.free_ctrl		= nvme_pci_free_ctrl,
	.submit_async_event	= nvme_pci_submit_async_event,
	.get_address		= nvme_pci_get_address,
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
		 * suspend on a Ryzen board, ASUS PRIME B350M-A, as well as
		 * within few minutes after bootup on a Coffee Lake board -
		 * ASUS PRIME Z370-A
		 */
		if (dmi_match(DMI_BOARD_VENDOR, "ASUSTeK COMPUTER INC.") &&
		    (dmi_match(DMI_BOARD_NAME, "PRIME B350M-A") ||
		     dmi_match(DMI_BOARD_NAME, "PRIME Z370-A")))
			return NVME_QUIRK_NO_APST;
	} else if ((pdev->vendor == 0x144d && (pdev->device == 0xa801 ||
		    pdev->device == 0xa808 || pdev->device == 0xa809)) ||
		   (pdev->vendor == 0x1e0f && pdev->device == 0x0001)) {
		/*
		 * Forcing to use host managed nvme power settings for
		 * lowest idle power with quick resume latency on
		 * Samsung and Toshiba SSDs based on suspend behavior
		 * on Coffee Lake board for LENOVO C640
		 */
		if ((dmi_match(DMI_BOARD_VENDOR, "LENOVO")) &&
		     dmi_match(DMI_BOARD_NAME, "LNVNB161216"))
			return NVME_QUIRK_SIMPLE_SUSPEND;
	}

	return 0;
}

static void nvme_async_probe(void *data, async_cookie_t cookie)
{
	struct nvme_dev *dev = data;

	flush_work(&dev->ctrl.reset_work);
	flush_work(&dev->ctrl.scan_work);
	nvme_put_ctrl(&dev->ctrl);
}

static int nvme_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	int node, result = -ENOMEM;
	struct nvme_dev *dev;
	unsigned long quirks = id->driver_data;
	size_t alloc_size;

	node = dev_to_node(&pdev->dev);
	if (node == NUMA_NO_NODE)
		set_dev_node(&pdev->dev, first_memory_node);

	dev = kzalloc_node(sizeof(*dev), GFP_KERNEL, node);
	if (!dev)
		return -ENOMEM;

	dev->nr_write_queues = write_queues;
	dev->nr_poll_queues = poll_queues;
	dev->nr_allocated_queues = nvme_max_io_queues(dev) + 1;
	dev->queues = kcalloc_node(dev->nr_allocated_queues,
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

	result = nvme_setup_prp_pools(dev);
	if (result)
		goto unmap;

	quirks |= check_vendor_combination_bug(pdev);

	if (!noacpi && acpi_storage_d3(&pdev->dev)) {
		/*
		 * Some systems use a bios work around to ask for D3 on
		 * platforms that support kernel managed suspend.
		 */
		dev_info(&pdev->dev,
			 "platform quirk: setting simple suspend\n");
		quirks |= NVME_QUIRK_SIMPLE_SUSPEND;
	}

	/*
	 * Double check that our mempool alloc size will cover the biggest
	 * command we support.
	 */
	alloc_size = nvme_pci_iod_alloc_size();
	WARN_ON_ONCE(alloc_size > PAGE_SIZE);

	dev->iod_mempool = mempool_create_node(1, mempool_kmalloc,
						mempool_kfree,
						(void *) alloc_size,
						GFP_KERNEL, node);
	if (!dev->iod_mempool) {
		result = -ENOMEM;
		goto release_pools;
	}

	result = nvme_init_ctrl(&dev->ctrl, &pdev->dev, &nvme_pci_ctrl_ops,
			quirks);
	if (result)
		goto release_mempool;

	dev_info(dev->ctrl.device, "pci function %s\n", dev_name(&pdev->dev));

	nvme_reset_ctrl(&dev->ctrl);
	async_schedule(nvme_async_probe, dev);

	return 0;

 release_mempool:
	mempool_destroy(dev->iod_mempool);
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

	/*
	 * We don't need to check the return value from waiting for the reset
	 * state as pci_dev device lock is held, making it impossible to race
	 * with ->remove().
	 */
	nvme_disable_prepare_reset(dev, false);
	nvme_sync_queues(&dev->ctrl);
}

static void nvme_reset_done(struct pci_dev *pdev)
{
	struct nvme_dev *dev = pci_get_drvdata(pdev);

	if (!nvme_try_sched_reset(&dev->ctrl))
		flush_work(&dev->ctrl.reset_work);
}

static void nvme_shutdown(struct pci_dev *pdev)
{
	struct nvme_dev *dev = pci_get_drvdata(pdev);

	nvme_disable_prepare_reset(dev, true);
}

static void nvme_remove_attrs(struct nvme_dev *dev)
{
	if (dev->attrs_added)
		sysfs_remove_group(&dev->ctrl.device->kobj,
				   &nvme_pci_attr_group);
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

	if (!pci_device_is_present(pdev)) {
		nvme_change_ctrl_state(&dev->ctrl, NVME_CTRL_DEAD);
		nvme_dev_disable(dev, true);
	}

	flush_work(&dev->ctrl.reset_work);
	nvme_stop_ctrl(&dev->ctrl);
	nvme_remove_namespaces(&dev->ctrl);
	nvme_dev_disable(dev, true);
	nvme_remove_attrs(dev);
	nvme_free_host_mem(dev);
	nvme_dev_remove_admin(dev);
	nvme_free_queues(dev, 0);
	nvme_release_prp_pools(dev);
	nvme_dev_unmap(dev);
	nvme_uninit_ctrl(&dev->ctrl);
}

#ifdef CONFIG_PM_SLEEP
static int nvme_get_power_state(struct nvme_ctrl *ctrl, u32 *ps)
{
	return nvme_get_features(ctrl, NVME_FEAT_POWER_MGMT, 0, NULL, 0, ps);
}

static int nvme_set_power_state(struct nvme_ctrl *ctrl, u32 ps)
{
	return nvme_set_features(ctrl, NVME_FEAT_POWER_MGMT, ps, NULL, 0, NULL);
}

static int nvme_resume(struct device *dev)
{
	struct nvme_dev *ndev = pci_get_drvdata(to_pci_dev(dev));
	struct nvme_ctrl *ctrl = &ndev->ctrl;

	if (ndev->last_ps == U32_MAX ||
	    nvme_set_power_state(ctrl, ndev->last_ps) != 0)
		goto reset;
	if (ctrl->hmpre && nvme_setup_host_mem(ndev))
		goto reset;

	return 0;
reset:
	return nvme_try_sched_reset(ctrl);
}

static int nvme_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct nvme_dev *ndev = pci_get_drvdata(pdev);
	struct nvme_ctrl *ctrl = &ndev->ctrl;
	int ret = -EBUSY;

	ndev->last_ps = U32_MAX;

	/*
	 * The platform does not remove power for a kernel managed suspend so
	 * use host managed nvme power settings for lowest idle power if
	 * possible. This should have quicker resume latency than a full device
	 * shutdown.  But if the firmware is involved after the suspend or the
	 * device does not support any non-default power states, shut down the
	 * device fully.
	 *
	 * If ASPM is not enabled for the device, shut down the device and allow
	 * the PCI bus layer to put it into D3 in order to take the PCIe link
	 * down, so as to allow the platform to achieve its minimum low-power
	 * state (which may not be possible if the link is up).
	 */
	if (pm_suspend_via_firmware() || !ctrl->npss ||
	    !pcie_aspm_enabled(pdev) ||
	    (ndev->ctrl.quirks & NVME_QUIRK_SIMPLE_SUSPEND))
		return nvme_disable_prepare_reset(ndev, true);

	nvme_start_freeze(ctrl);
	nvme_wait_freeze(ctrl);
	nvme_sync_queues(ctrl);

	if (ctrl->state != NVME_CTRL_LIVE)
		goto unfreeze;

	/*
	 * Host memory access may not be successful in a system suspend state,
	 * but the specification allows the controller to access memory in a
	 * non-operational power state.
	 */
	if (ndev->hmb) {
		ret = nvme_set_host_mem(ndev, 0);
		if (ret < 0)
			goto unfreeze;
	}

	ret = nvme_get_power_state(ctrl, &ndev->last_ps);
	if (ret < 0)
		goto unfreeze;

	/*
	 * A saved state prevents pci pm from generically controlling the
	 * device's power. If we're using protocol specific settings, we don't
	 * want pci interfering.
	 */
	pci_save_state(pdev);

	ret = nvme_set_power_state(ctrl, ctrl->npss);
	if (ret < 0)
		goto unfreeze;

	if (ret) {
		/* discard the saved state */
		pci_load_saved_state(pdev, NULL);

		/*
		 * Clearing npss forces a controller reset on resume. The
		 * correct value will be rediscovered then.
		 */
		ret = nvme_disable_prepare_reset(ndev, true);
		ctrl->npss = 0;
	}
unfreeze:
	nvme_unfreeze(ctrl);
	return ret;
}

static int nvme_simple_suspend(struct device *dev)
{
	struct nvme_dev *ndev = pci_get_drvdata(to_pci_dev(dev));

	return nvme_disable_prepare_reset(ndev, true);
}

static int nvme_simple_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct nvme_dev *ndev = pci_get_drvdata(pdev);

	return nvme_try_sched_reset(&ndev->ctrl);
}

static const struct dev_pm_ops nvme_dev_pm_ops = {
	.suspend	= nvme_suspend,
	.resume		= nvme_resume,
	.freeze		= nvme_simple_suspend,
	.thaw		= nvme_simple_resume,
	.poweroff	= nvme_simple_suspend,
	.restore	= nvme_simple_resume,
};
#endif /* CONFIG_PM_SLEEP */

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
	struct nvme_dev *dev = pci_get_drvdata(pdev);

	flush_work(&dev->ctrl.reset_work);
}

static const struct pci_error_handlers nvme_err_handler = {
	.error_detected	= nvme_error_detected,
	.slot_reset	= nvme_slot_reset,
	.resume		= nvme_error_resume,
	.reset_prepare	= nvme_reset_prepare,
	.reset_done	= nvme_reset_done,
};

static const struct pci_device_id nvme_id_table[] = {
	{ PCI_VDEVICE(INTEL, 0x0953),	/* Intel 750/P3500/P3600/P3700 */
		.driver_data = NVME_QUIRK_STRIPE_SIZE |
				NVME_QUIRK_DEALLOCATE_ZEROES, },
	{ PCI_VDEVICE(INTEL, 0x0a53),	/* Intel P3520 */
		.driver_data = NVME_QUIRK_STRIPE_SIZE |
				NVME_QUIRK_DEALLOCATE_ZEROES, },
	{ PCI_VDEVICE(INTEL, 0x0a54),	/* Intel P4500/P4600 */
		.driver_data = NVME_QUIRK_STRIPE_SIZE |
				NVME_QUIRK_DEALLOCATE_ZEROES |
				NVME_QUIRK_IGNORE_DEV_SUBNQN, },
	{ PCI_VDEVICE(INTEL, 0x0a55),	/* Dell Express Flash P4600 */
		.driver_data = NVME_QUIRK_STRIPE_SIZE |
				NVME_QUIRK_DEALLOCATE_ZEROES, },
	{ PCI_VDEVICE(INTEL, 0xf1a5),	/* Intel 600P/P3100 */
		.driver_data = NVME_QUIRK_NO_DEEPEST_PS |
				NVME_QUIRK_MEDIUM_PRIO_SQ |
				NVME_QUIRK_NO_TEMP_THRESH_CHANGE |
				NVME_QUIRK_DISABLE_WRITE_ZEROES, },
	{ PCI_VDEVICE(INTEL, 0xf1a6),	/* Intel 760p/Pro 7600p */
		.driver_data = NVME_QUIRK_IGNORE_DEV_SUBNQN, },
	{ PCI_VDEVICE(INTEL, 0x5845),	/* Qemu emulated controller */
		.driver_data = NVME_QUIRK_IDENTIFY_CNS |
				NVME_QUIRK_DISABLE_WRITE_ZEROES |
				NVME_QUIRK_BOGUS_NID, },
	{ PCI_VDEVICE(REDHAT, 0x0010),	/* Qemu emulated controller */
		.driver_data = NVME_QUIRK_BOGUS_NID, },
	{ PCI_DEVICE(0x126f, 0x2263),	/* Silicon Motion unidentified */
		.driver_data = NVME_QUIRK_NO_NS_DESC_LIST, },
	{ PCI_DEVICE(0x1bb1, 0x0100),   /* Seagate Nytro Flash Storage */
		.driver_data = NVME_QUIRK_DELAY_BEFORE_CHK_RDY |
				NVME_QUIRK_NO_NS_DESC_LIST, },
	{ PCI_DEVICE(0x1c58, 0x0003),	/* HGST adapter */
		.driver_data = NVME_QUIRK_DELAY_BEFORE_CHK_RDY, },
	{ PCI_DEVICE(0x1c58, 0x0023),	/* WDC SN200 adapter */
		.driver_data = NVME_QUIRK_DELAY_BEFORE_CHK_RDY, },
	{ PCI_DEVICE(0x1c5f, 0x0540),	/* Memblaze Pblaze4 adapter */
		.driver_data = NVME_QUIRK_DELAY_BEFORE_CHK_RDY, },
	{ PCI_DEVICE(0x144d, 0xa821),   /* Samsung PM1725 */
		.driver_data = NVME_QUIRK_DELAY_BEFORE_CHK_RDY, },
	{ PCI_DEVICE(0x144d, 0xa822),   /* Samsung PM1725a */
		.driver_data = NVME_QUIRK_DELAY_BEFORE_CHK_RDY |
				NVME_QUIRK_DISABLE_WRITE_ZEROES|
				NVME_QUIRK_IGNORE_DEV_SUBNQN, },
	{ PCI_DEVICE(0x1987, 0x5016),	/* Phison E16 */
		.driver_data = NVME_QUIRK_IGNORE_DEV_SUBNQN, },
	{ PCI_DEVICE(0x1b4b, 0x1092),	/* Lexar 256 GB SSD */
		.driver_data = NVME_QUIRK_NO_NS_DESC_LIST |
				NVME_QUIRK_IGNORE_DEV_SUBNQN, },
	{ PCI_DEVICE(0x10ec, 0x5762),   /* ADATA SX6000LNP */
		.driver_data = NVME_QUIRK_IGNORE_DEV_SUBNQN, },
	{ PCI_DEVICE(0x1cc1, 0x8201),   /* ADATA SX8200PNP 512GB */
		.driver_data = NVME_QUIRK_NO_DEEPEST_PS |
				NVME_QUIRK_IGNORE_DEV_SUBNQN, },
	{ PCI_DEVICE(0x1c5c, 0x1504),   /* SK Hynix PC400 */
		.driver_data = NVME_QUIRK_DISABLE_WRITE_ZEROES, },
	{ PCI_DEVICE(0x15b7, 0x2001),   /*  Sandisk Skyhawk */
		.driver_data = NVME_QUIRK_DISABLE_WRITE_ZEROES, },
	{ PCI_DEVICE(0x1d97, 0x2263),   /* SPCC */
		.driver_data = NVME_QUIRK_DISABLE_WRITE_ZEROES, },
	{ PCI_DEVICE(0x2646, 0x2262),   /* KINGSTON SKC2000 NVMe SSD */
		.driver_data = NVME_QUIRK_NO_DEEPEST_PS, },
	{ PCI_DEVICE(0x2646, 0x2263),   /* KINGSTON A2000 NVMe SSD  */
		.driver_data = NVME_QUIRK_NO_DEEPEST_PS, },
	{ PCI_DEVICE(0x1e4B, 0x1002),   /* MAXIO MAP1002 */
		.driver_data = NVME_QUIRK_BOGUS_NID, },
	{ PCI_DEVICE(0x1e4B, 0x1202),   /* MAXIO MAP1202 */
		.driver_data = NVME_QUIRK_BOGUS_NID, },
	{ PCI_DEVICE(PCI_VENDOR_ID_AMAZON, 0x0061),
		.driver_data = NVME_QUIRK_DMA_ADDRESS_BITS_48, },
	{ PCI_DEVICE(PCI_VENDOR_ID_AMAZON, 0x0065),
		.driver_data = NVME_QUIRK_DMA_ADDRESS_BITS_48, },
	{ PCI_DEVICE(PCI_VENDOR_ID_AMAZON, 0x8061),
		.driver_data = NVME_QUIRK_DMA_ADDRESS_BITS_48, },
	{ PCI_DEVICE(PCI_VENDOR_ID_AMAZON, 0xcd00),
		.driver_data = NVME_QUIRK_DMA_ADDRESS_BITS_48, },
	{ PCI_DEVICE(PCI_VENDOR_ID_AMAZON, 0xcd01),
		.driver_data = NVME_QUIRK_DMA_ADDRESS_BITS_48, },
	{ PCI_DEVICE(PCI_VENDOR_ID_AMAZON, 0xcd02),
		.driver_data = NVME_QUIRK_DMA_ADDRESS_BITS_48, },
	{ PCI_DEVICE(PCI_VENDOR_ID_APPLE, 0x2001),
		.driver_data = NVME_QUIRK_SINGLE_VECTOR },
	{ PCI_DEVICE(PCI_VENDOR_ID_APPLE, 0x2003) },
	{ PCI_DEVICE(PCI_VENDOR_ID_APPLE, 0x2005),
		.driver_data = NVME_QUIRK_SINGLE_VECTOR |
				NVME_QUIRK_128_BYTES_SQES |
				NVME_QUIRK_SHARED_TAGS |
				NVME_QUIRK_SKIP_CID_GEN },
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
#ifdef CONFIG_PM_SLEEP
	.driver		= {
		.pm	= &nvme_dev_pm_ops,
	},
#endif
	.sriov_configure = pci_sriov_configure_simple,
	.err_handler	= &nvme_err_handler,
};

static int __init nvme_init(void)
{
	BUILD_BUG_ON(sizeof(struct nvme_create_cq) != 64);
	BUILD_BUG_ON(sizeof(struct nvme_create_sq) != 64);
	BUILD_BUG_ON(sizeof(struct nvme_delete_queue) != 64);
	BUILD_BUG_ON(IRQ_AFFINITY_MAX_SETS < 2);

	return pci_register_driver(&nvme_driver);
}

static void __exit nvme_exit(void)
{
	pci_unregister_driver(&nvme_driver);
	flush_workqueue(nvme_wq);
}

MODULE_AUTHOR("Matthew Wilcox <willy@linux.intel.com>");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0");
module_init(nvme_init);
module_exit(nvme_exit);
