// SPDX-License-Identifier: GPL-2.0
/*
 * Apple ANS NVM Express device driver
 * Copyright The Asahi Linux Contributors
 *
 * Based on the pci.c NVM Express device driver
 * Copyright (c) 2011-2014, Intel Corporation.
 * and on the rdma.c NVMe over Fabrics RDMA host code.
 * Copyright (c) 2015-2016 HGST, a Western Digital Company.
 */

#include <linux/async.h>
#include <linux/blkdev.h>
#include <linux/blk-mq.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/interrupt.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/jiffies.h>
#include <linux/mempool.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/once.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/soc/apple/rtkit.h>
#include <linux/soc/apple/sart.h>
#include <linux/reset.h>
#include <linux/time64.h>

#include "nvme.h"

#define APPLE_ANS_BOOT_TIMEOUT	  USEC_PER_SEC
#define APPLE_ANS_MAX_QUEUE_DEPTH 64

#define APPLE_ANS_COPROC_CPU_CONTROL	 0x44
#define APPLE_ANS_COPROC_CPU_CONTROL_RUN BIT(4)

#define APPLE_ANS_ACQ_DB  0x1004
#define APPLE_ANS_IOCQ_DB 0x100c

#define APPLE_ANS_MAX_PEND_CMDS_CTRL 0x1210

#define APPLE_ANS_BOOT_STATUS	 0x1300
#define APPLE_ANS_BOOT_STATUS_OK 0xde71ce55

#define APPLE_ANS_UNKNOWN_CTRL	 0x24008
#define APPLE_ANS_PRP_NULL_CHECK BIT(11)

#define APPLE_ANS_LINEAR_SQ_CTRL 0x24908
#define APPLE_ANS_LINEAR_SQ_EN	 BIT(0)

#define APPLE_ANS_LINEAR_ASQ_DB	 0x2490c
#define APPLE_ANS_LINEAR_IOSQ_DB 0x24910

#define APPLE_NVMMU_NUM_TCBS	  0x28100
#define APPLE_NVMMU_ASQ_TCB_BASE  0x28108
#define APPLE_NVMMU_IOSQ_TCB_BASE 0x28110
#define APPLE_NVMMU_TCB_INVAL	  0x28118
#define APPLE_NVMMU_TCB_STAT	  0x28120

/*
 * This controller is a bit weird in the way command tags works: Both the
 * admin and the IO queue share the same tag space. Additionally, tags
 * cannot be higher than 0x40 which effectively limits the combined
 * queue depth to 0x40. Instead of wasting half of that on the admin queue
 * which gets much less traffic we instead reduce its size here.
 * The controller also doesn't support async event such that no space must
 * be reserved for NVME_NR_AEN_COMMANDS.
 */
#define APPLE_NVME_AQ_DEPTH	   2
#define APPLE_NVME_AQ_MQ_TAG_DEPTH (APPLE_NVME_AQ_DEPTH - 1)

/*
 * These can be higher, but we need to ensure that any command doesn't
 * require an sg allocation that needs more than a page of data.
 */
#define NVME_MAX_KB_SZ 4096
#define NVME_MAX_SEGS  127

/*
 * This controller comes with an embedded IOMMU known as NVMMU.
 * The NVMMU is pointed to an array of TCBs indexed by the command tag.
 * Each command must be configured inside this structure before it's allowed
 * to execute, including commands that don't require DMA transfers.
 *
 * An exception to this are Apple's vendor-specific commands (opcode 0xD8 on the
 * admin queue): Those commands must still be added to the NVMMU but the DMA
 * buffers cannot be represented as PRPs and must instead be allowed using SART.
 *
 * Programming the PRPs to the same values as those in the submission queue
 * looks rather silly at first. This hardware is however designed for a kernel
 * that runs the NVMMU code in a higher exception level than the NVMe driver.
 * In that setting the NVMe driver first programs the submission queue entry
 * and then executes a hypercall to the code that is allowed to program the
 * NVMMU. The NVMMU driver then creates a shadow copy of the PRPs while
 * verifying that they don't point to kernel text, data, pagetables, or similar
 * protected areas before programming the TCB to point to this shadow copy.
 * Since Linux doesn't do any of that we may as well just point both the queue
 * and the TCB PRP pointer to the same memory.
 */
struct apple_nvmmu_tcb {
	u8 opcode;

#define APPLE_ANS_TCB_DMA_FROM_DEVICE BIT(0)
#define APPLE_ANS_TCB_DMA_TO_DEVICE   BIT(1)
	u8 dma_flags;

	u8 command_id;
	u8 _unk0;
	__le16 length;
	u8 _unk1[18];
	__le64 prp1;
	__le64 prp2;
	u8 _unk2[16];
	u8 aes_iv[8];
	u8 _aes_unk[64];
};

/*
 * The Apple NVMe controller only supports a single admin and a single IO queue
 * which are both limited to 64 entries and share a single interrupt.
 *
 * The completion queue works as usual. The submission "queue" instead is
 * an array indexed by the command tag on this hardware. Commands must also be
 * present in the NVMMU's tcb array. They are triggered by writing their tag to
 * a MMIO register.
 */
struct apple_nvme_queue {
	struct nvme_command *sqes;
	struct nvme_completion *cqes;
	struct apple_nvmmu_tcb *tcbs;

	dma_addr_t sq_dma_addr;
	dma_addr_t cq_dma_addr;
	dma_addr_t tcb_dma_addr;

	u32 __iomem *sq_db;
	u32 __iomem *cq_db;

	u16 cq_head;
	u8 cq_phase;

	bool is_adminq;
	bool enabled;
};

/*
 * The apple_nvme_iod describes the data in an I/O.
 *
 * The sg pointer contains the list of PRP chunk allocations in addition
 * to the actual struct scatterlist.
 */
struct apple_nvme_iod {
	struct nvme_request req;
	struct nvme_command cmd;
	struct apple_nvme_queue *q;
	int npages; /* In the PRP list. 0 means small pool in use */
	int nents; /* Used in scatterlist */
	dma_addr_t first_dma;
	unsigned int dma_len; /* length of single DMA segment mapping */
	struct scatterlist *sg;
};

struct apple_nvme {
	struct device *dev;

	void __iomem *mmio_coproc;
	void __iomem *mmio_nvme;

	struct device **pd_dev;
	struct device_link **pd_link;
	int pd_count;

	struct apple_sart *sart;
	struct apple_rtkit *rtk;
	struct reset_control *reset;

	struct dma_pool *prp_page_pool;
	struct dma_pool *prp_small_pool;
	mempool_t *iod_mempool;

	struct nvme_ctrl ctrl;
	struct work_struct remove_work;

	struct apple_nvme_queue adminq;
	struct apple_nvme_queue ioq;

	struct blk_mq_tag_set admin_tagset;
	struct blk_mq_tag_set tagset;

	int irq;
	spinlock_t lock;
};

static_assert(sizeof(struct nvme_command) == 64);
static_assert(sizeof(struct apple_nvmmu_tcb) == 128);

static inline struct apple_nvme *ctrl_to_apple_nvme(struct nvme_ctrl *ctrl)
{
	return container_of(ctrl, struct apple_nvme, ctrl);
}

static inline struct apple_nvme *queue_to_apple_nvme(struct apple_nvme_queue *q)
{
	if (q->is_adminq)
		return container_of(q, struct apple_nvme, adminq);

	return container_of(q, struct apple_nvme, ioq);
}

static unsigned int apple_nvme_queue_depth(struct apple_nvme_queue *q)
{
	if (q->is_adminq)
		return APPLE_NVME_AQ_DEPTH;

	return APPLE_ANS_MAX_QUEUE_DEPTH;
}

static void apple_nvme_rtkit_crashed(void *cookie)
{
	struct apple_nvme *anv = cookie;

	dev_warn(anv->dev, "RTKit crashed; unable to recover without a reboot");
	nvme_reset_ctrl(&anv->ctrl);
}

static int apple_nvme_sart_dma_setup(void *cookie,
				     struct apple_rtkit_shmem *bfr)
{
	struct apple_nvme *anv = cookie;
	int ret;

	if (bfr->iova)
		return -EINVAL;
	if (!bfr->size)
		return -EINVAL;

	bfr->buffer =
		dma_alloc_coherent(anv->dev, bfr->size, &bfr->iova, GFP_KERNEL);
	if (!bfr->buffer)
		return -ENOMEM;

	ret = apple_sart_add_allowed_region(anv->sart, bfr->iova, bfr->size);
	if (ret) {
		dma_free_coherent(anv->dev, bfr->size, bfr->buffer, bfr->iova);
		bfr->buffer = NULL;
		return -ENOMEM;
	}

	return 0;
}

static void apple_nvme_sart_dma_destroy(void *cookie,
					struct apple_rtkit_shmem *bfr)
{
	struct apple_nvme *anv = cookie;

	apple_sart_remove_allowed_region(anv->sart, bfr->iova, bfr->size);
	dma_free_coherent(anv->dev, bfr->size, bfr->buffer, bfr->iova);
}

static const struct apple_rtkit_ops apple_nvme_rtkit_ops = {
	.crashed = apple_nvme_rtkit_crashed,
	.shmem_setup = apple_nvme_sart_dma_setup,
	.shmem_destroy = apple_nvme_sart_dma_destroy,
};

static void apple_nvmmu_inval(struct apple_nvme_queue *q, unsigned int tag)
{
	struct apple_nvme *anv = queue_to_apple_nvme(q);

	writel(tag, anv->mmio_nvme + APPLE_NVMMU_TCB_INVAL);
	if (readl(anv->mmio_nvme + APPLE_NVMMU_TCB_STAT))
		dev_warn_ratelimited(anv->dev,
				     "NVMMU TCB invalidation failed\n");
}

static void apple_nvme_submit_cmd(struct apple_nvme_queue *q,
				  struct nvme_command *cmd)
{
	struct apple_nvme *anv = queue_to_apple_nvme(q);
	u32 tag = nvme_tag_from_cid(cmd->common.command_id);
	struct apple_nvmmu_tcb *tcb = &q->tcbs[tag];

	tcb->opcode = cmd->common.opcode;
	tcb->prp1 = cmd->common.dptr.prp1;
	tcb->prp2 = cmd->common.dptr.prp2;
	tcb->length = cmd->rw.length;
	tcb->command_id = tag;

	if (nvme_is_write(cmd))
		tcb->dma_flags = APPLE_ANS_TCB_DMA_TO_DEVICE;
	else
		tcb->dma_flags = APPLE_ANS_TCB_DMA_FROM_DEVICE;

	memcpy(&q->sqes[tag], cmd, sizeof(*cmd));

	/*
	 * This lock here doesn't make much sense at a first glace but
	 * removing it will result in occasional missed completetion
	 * interrupts even though the commands still appear on the CQ.
	 * It's unclear why this happens but our best guess is that
	 * there is a bug in the firmware triggered when a new command
	 * is issued while we're inside the irq handler between the
	 * NVMMU invalidation (and making the tag available again)
	 * and the final CQ update.
	 */
	spin_lock_irq(&anv->lock);
	writel(tag, q->sq_db);
	spin_unlock_irq(&anv->lock);
}

/*
 * From pci.c:
 * Will slightly overestimate the number of pages needed.  This is OK
 * as it only leads to a small amount of wasted memory for the lifetime of
 * the I/O.
 */
static inline size_t apple_nvme_iod_alloc_size(void)
{
	const unsigned int nprps = DIV_ROUND_UP(
		NVME_MAX_KB_SZ + NVME_CTRL_PAGE_SIZE, NVME_CTRL_PAGE_SIZE);
	const int npages = DIV_ROUND_UP(8 * nprps, PAGE_SIZE - 8);
	const size_t alloc_size = sizeof(__le64 *) * npages +
				  sizeof(struct scatterlist) * NVME_MAX_SEGS;

	return alloc_size;
}

static void **apple_nvme_iod_list(struct request *req)
{
	struct apple_nvme_iod *iod = blk_mq_rq_to_pdu(req);

	return (void **)(iod->sg + blk_rq_nr_phys_segments(req));
}

static void apple_nvme_free_prps(struct apple_nvme *anv, struct request *req)
{
	const int last_prp = NVME_CTRL_PAGE_SIZE / sizeof(__le64) - 1;
	struct apple_nvme_iod *iod = blk_mq_rq_to_pdu(req);
	dma_addr_t dma_addr = iod->first_dma;
	int i;

	for (i = 0; i < iod->npages; i++) {
		__le64 *prp_list = apple_nvme_iod_list(req)[i];
		dma_addr_t next_dma_addr = le64_to_cpu(prp_list[last_prp]);

		dma_pool_free(anv->prp_page_pool, prp_list, dma_addr);
		dma_addr = next_dma_addr;
	}
}

static void apple_nvme_unmap_data(struct apple_nvme *anv, struct request *req)
{
	struct apple_nvme_iod *iod = blk_mq_rq_to_pdu(req);

	if (iod->dma_len) {
		dma_unmap_page(anv->dev, iod->first_dma, iod->dma_len,
			       rq_dma_dir(req));
		return;
	}

	WARN_ON_ONCE(!iod->nents);

	dma_unmap_sg(anv->dev, iod->sg, iod->nents, rq_dma_dir(req));
	if (iod->npages == 0)
		dma_pool_free(anv->prp_small_pool, apple_nvme_iod_list(req)[0],
			      iod->first_dma);
	else
		apple_nvme_free_prps(anv, req);
	mempool_free(iod->sg, anv->iod_mempool);
}

static void apple_nvme_print_sgl(struct scatterlist *sgl, int nents)
{
	int i;
	struct scatterlist *sg;

	for_each_sg(sgl, sg, nents, i) {
		dma_addr_t phys = sg_phys(sg);

		pr_warn("sg[%d] phys_addr:%pad offset:%d length:%d dma_address:%pad dma_length:%d\n",
			i, &phys, sg->offset, sg->length, &sg_dma_address(sg),
			sg_dma_len(sg));
	}
}

static blk_status_t apple_nvme_setup_prps(struct apple_nvme *anv,
					  struct request *req,
					  struct nvme_rw_command *cmnd)
{
	struct apple_nvme_iod *iod = blk_mq_rq_to_pdu(req);
	struct dma_pool *pool;
	int length = blk_rq_payload_bytes(req);
	struct scatterlist *sg = iod->sg;
	int dma_len = sg_dma_len(sg);
	u64 dma_addr = sg_dma_address(sg);
	int offset = dma_addr & (NVME_CTRL_PAGE_SIZE - 1);
	__le64 *prp_list;
	void **list = apple_nvme_iod_list(req);
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
		pool = anv->prp_small_pool;
		iod->npages = 0;
	} else {
		pool = anv->prp_page_pool;
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
	apple_nvme_free_prps(anv, req);
	return BLK_STS_RESOURCE;
bad_sgl:
	WARN(DO_ONCE(apple_nvme_print_sgl, iod->sg, iod->nents),
	     "Invalid SGL for payload:%d nents:%d\n", blk_rq_payload_bytes(req),
	     iod->nents);
	return BLK_STS_IOERR;
}

static blk_status_t apple_nvme_setup_prp_simple(struct apple_nvme *anv,
						struct request *req,
						struct nvme_rw_command *cmnd,
						struct bio_vec *bv)
{
	struct apple_nvme_iod *iod = blk_mq_rq_to_pdu(req);
	unsigned int offset = bv->bv_offset & (NVME_CTRL_PAGE_SIZE - 1);
	unsigned int first_prp_len = NVME_CTRL_PAGE_SIZE - offset;

	iod->first_dma = dma_map_bvec(anv->dev, bv, rq_dma_dir(req), 0);
	if (dma_mapping_error(anv->dev, iod->first_dma))
		return BLK_STS_RESOURCE;
	iod->dma_len = bv->bv_len;

	cmnd->dptr.prp1 = cpu_to_le64(iod->first_dma);
	if (bv->bv_len > first_prp_len)
		cmnd->dptr.prp2 = cpu_to_le64(iod->first_dma + first_prp_len);
	return BLK_STS_OK;
}

static blk_status_t apple_nvme_map_data(struct apple_nvme *anv,
					struct request *req,
					struct nvme_command *cmnd)
{
	struct apple_nvme_iod *iod = blk_mq_rq_to_pdu(req);
	blk_status_t ret = BLK_STS_RESOURCE;
	int nr_mapped;

	if (blk_rq_nr_phys_segments(req) == 1) {
		struct bio_vec bv = req_bvec(req);

		if (bv.bv_offset + bv.bv_len <= NVME_CTRL_PAGE_SIZE * 2)
			return apple_nvme_setup_prp_simple(anv, req, &cmnd->rw,
							   &bv);
	}

	iod->dma_len = 0;
	iod->sg = mempool_alloc(anv->iod_mempool, GFP_ATOMIC);
	if (!iod->sg)
		return BLK_STS_RESOURCE;
	sg_init_table(iod->sg, blk_rq_nr_phys_segments(req));
	iod->nents = blk_rq_map_sg(req->q, req, iod->sg);
	if (!iod->nents)
		goto out_free_sg;

	nr_mapped = dma_map_sg_attrs(anv->dev, iod->sg, iod->nents,
				     rq_dma_dir(req), DMA_ATTR_NO_WARN);
	if (!nr_mapped)
		goto out_free_sg;

	ret = apple_nvme_setup_prps(anv, req, &cmnd->rw);
	if (ret != BLK_STS_OK)
		goto out_unmap_sg;
	return BLK_STS_OK;

out_unmap_sg:
	dma_unmap_sg(anv->dev, iod->sg, iod->nents, rq_dma_dir(req));
out_free_sg:
	mempool_free(iod->sg, anv->iod_mempool);
	return ret;
}

static __always_inline void apple_nvme_unmap_rq(struct request *req)
{
	struct apple_nvme_iod *iod = blk_mq_rq_to_pdu(req);
	struct apple_nvme *anv = queue_to_apple_nvme(iod->q);

	if (blk_rq_nr_phys_segments(req))
		apple_nvme_unmap_data(anv, req);
}

static void apple_nvme_complete_rq(struct request *req)
{
	apple_nvme_unmap_rq(req);
	nvme_complete_rq(req);
}

static void apple_nvme_complete_batch(struct io_comp_batch *iob)
{
	nvme_complete_batch(iob, apple_nvme_unmap_rq);
}

static inline bool apple_nvme_cqe_pending(struct apple_nvme_queue *q)
{
	struct nvme_completion *hcqe = &q->cqes[q->cq_head];

	return (le16_to_cpu(READ_ONCE(hcqe->status)) & 1) == q->cq_phase;
}

static inline struct blk_mq_tags *
apple_nvme_queue_tagset(struct apple_nvme *anv, struct apple_nvme_queue *q)
{
	if (q->is_adminq)
		return anv->admin_tagset.tags[0];
	else
		return anv->tagset.tags[0];
}

static inline void apple_nvme_handle_cqe(struct apple_nvme_queue *q,
					 struct io_comp_batch *iob, u16 idx)
{
	struct apple_nvme *anv = queue_to_apple_nvme(q);
	struct nvme_completion *cqe = &q->cqes[idx];
	__u16 command_id = READ_ONCE(cqe->command_id);
	struct request *req;

	apple_nvmmu_inval(q, command_id);

	req = nvme_find_rq(apple_nvme_queue_tagset(anv, q), command_id);
	if (unlikely(!req)) {
		dev_warn(anv->dev, "invalid id %d completed", command_id);
		return;
	}

	if (!nvme_try_complete_req(req, cqe->status, cqe->result) &&
	    !blk_mq_add_to_batch(req, iob, nvme_req(req)->status,
				 apple_nvme_complete_batch))
		apple_nvme_complete_rq(req);
}

static inline void apple_nvme_update_cq_head(struct apple_nvme_queue *q)
{
	u32 tmp = q->cq_head + 1;

	if (tmp == apple_nvme_queue_depth(q)) {
		q->cq_head = 0;
		q->cq_phase ^= 1;
	} else {
		q->cq_head = tmp;
	}
}

static bool apple_nvme_poll_cq(struct apple_nvme_queue *q,
			       struct io_comp_batch *iob)
{
	bool found = false;

	while (apple_nvme_cqe_pending(q)) {
		found = true;

		/*
		 * load-load control dependency between phase and the rest of
		 * the cqe requires a full read memory barrier
		 */
		dma_rmb();
		apple_nvme_handle_cqe(q, iob, q->cq_head);
		apple_nvme_update_cq_head(q);
	}

	if (found)
		writel(q->cq_head, q->cq_db);

	return found;
}

static bool apple_nvme_handle_cq(struct apple_nvme_queue *q, bool force)
{
	bool found;
	DEFINE_IO_COMP_BATCH(iob);

	if (!READ_ONCE(q->enabled) && !force)
		return false;

	found = apple_nvme_poll_cq(q, &iob);

	if (!rq_list_empty(iob.req_list))
		apple_nvme_complete_batch(&iob);

	return found;
}

static irqreturn_t apple_nvme_irq(int irq, void *data)
{
	struct apple_nvme *anv = data;
	bool handled = false;
	unsigned long flags;

	spin_lock_irqsave(&anv->lock, flags);
	if (apple_nvme_handle_cq(&anv->ioq, false))
		handled = true;
	if (apple_nvme_handle_cq(&anv->adminq, false))
		handled = true;
	spin_unlock_irqrestore(&anv->lock, flags);

	if (handled)
		return IRQ_HANDLED;
	return IRQ_NONE;
}

static int apple_nvme_create_cq(struct apple_nvme *anv)
{
	struct nvme_command c = {};

	/*
	 * Note: we (ab)use the fact that the prp fields survive if no data
	 * is attached to the request.
	 */
	c.create_cq.opcode = nvme_admin_create_cq;
	c.create_cq.prp1 = cpu_to_le64(anv->ioq.cq_dma_addr);
	c.create_cq.cqid = cpu_to_le16(1);
	c.create_cq.qsize = cpu_to_le16(APPLE_ANS_MAX_QUEUE_DEPTH - 1);
	c.create_cq.cq_flags = cpu_to_le16(NVME_QUEUE_PHYS_CONTIG | NVME_CQ_IRQ_ENABLED);
	c.create_cq.irq_vector = cpu_to_le16(0);

	return nvme_submit_sync_cmd(anv->ctrl.admin_q, &c, NULL, 0);
}

static int apple_nvme_remove_cq(struct apple_nvme *anv)
{
	struct nvme_command c = {};

	c.delete_queue.opcode = nvme_admin_delete_cq;
	c.delete_queue.qid = cpu_to_le16(1);

	return nvme_submit_sync_cmd(anv->ctrl.admin_q, &c, NULL, 0);
}

static int apple_nvme_create_sq(struct apple_nvme *anv)
{
	struct nvme_command c = {};

	/*
	 * Note: we (ab)use the fact that the prp fields survive if no data
	 * is attached to the request.
	 */
	c.create_sq.opcode = nvme_admin_create_sq;
	c.create_sq.prp1 = cpu_to_le64(anv->ioq.sq_dma_addr);
	c.create_sq.sqid = cpu_to_le16(1);
	c.create_sq.qsize = cpu_to_le16(APPLE_ANS_MAX_QUEUE_DEPTH - 1);
	c.create_sq.sq_flags = cpu_to_le16(NVME_QUEUE_PHYS_CONTIG);
	c.create_sq.cqid = cpu_to_le16(1);

	return nvme_submit_sync_cmd(anv->ctrl.admin_q, &c, NULL, 0);
}

static int apple_nvme_remove_sq(struct apple_nvme *anv)
{
	struct nvme_command c = {};

	c.delete_queue.opcode = nvme_admin_delete_sq;
	c.delete_queue.qid = cpu_to_le16(1);

	return nvme_submit_sync_cmd(anv->ctrl.admin_q, &c, NULL, 0);
}

static blk_status_t apple_nvme_queue_rq(struct blk_mq_hw_ctx *hctx,
					const struct blk_mq_queue_data *bd)
{
	struct nvme_ns *ns = hctx->queue->queuedata;
	struct apple_nvme_queue *q = hctx->driver_data;
	struct apple_nvme *anv = queue_to_apple_nvme(q);
	struct request *req = bd->rq;
	struct apple_nvme_iod *iod = blk_mq_rq_to_pdu(req);
	struct nvme_command *cmnd = &iod->cmd;
	blk_status_t ret;

	iod->npages = -1;
	iod->nents = 0;

	/*
	 * We should not need to do this, but we're still using this to
	 * ensure we can drain requests on a dying queue.
	 */
	if (unlikely(!READ_ONCE(q->enabled)))
		return BLK_STS_IOERR;

	if (!nvme_check_ready(&anv->ctrl, req, true))
		return nvme_fail_nonready_command(&anv->ctrl, req);

	ret = nvme_setup_cmd(ns, req);
	if (ret)
		return ret;

	if (blk_rq_nr_phys_segments(req)) {
		ret = apple_nvme_map_data(anv, req, cmnd);
		if (ret)
			goto out_free_cmd;
	}

	nvme_start_request(req);
	apple_nvme_submit_cmd(q, cmnd);
	return BLK_STS_OK;

out_free_cmd:
	nvme_cleanup_cmd(req);
	return ret;
}

static int apple_nvme_init_hctx(struct blk_mq_hw_ctx *hctx, void *data,
				unsigned int hctx_idx)
{
	hctx->driver_data = data;
	return 0;
}

static int apple_nvme_init_request(struct blk_mq_tag_set *set,
				   struct request *req, unsigned int hctx_idx,
				   unsigned int numa_node)
{
	struct apple_nvme_queue *q = set->driver_data;
	struct apple_nvme *anv = queue_to_apple_nvme(q);
	struct apple_nvme_iod *iod = blk_mq_rq_to_pdu(req);
	struct nvme_request *nreq = nvme_req(req);

	iod->q = q;
	nreq->ctrl = &anv->ctrl;
	nreq->cmd = &iod->cmd;

	return 0;
}

static void apple_nvme_disable(struct apple_nvme *anv, bool shutdown)
{
	enum nvme_ctrl_state state = nvme_ctrl_state(&anv->ctrl);
	u32 csts = readl(anv->mmio_nvme + NVME_REG_CSTS);
	bool dead = false, freeze = false;
	unsigned long flags;

	if (apple_rtkit_is_crashed(anv->rtk))
		dead = true;
	if (!(csts & NVME_CSTS_RDY))
		dead = true;
	if (csts & NVME_CSTS_CFS)
		dead = true;

	if (state == NVME_CTRL_LIVE ||
	    state == NVME_CTRL_RESETTING) {
		freeze = true;
		nvme_start_freeze(&anv->ctrl);
	}

	/*
	 * Give the controller a chance to complete all entered requests if
	 * doing a safe shutdown.
	 */
	if (!dead && shutdown && freeze)
		nvme_wait_freeze_timeout(&anv->ctrl, NVME_IO_TIMEOUT);

	nvme_quiesce_io_queues(&anv->ctrl);

	if (!dead) {
		if (READ_ONCE(anv->ioq.enabled)) {
			apple_nvme_remove_sq(anv);
			apple_nvme_remove_cq(anv);
		}

		/*
		 * Always disable the NVMe controller after shutdown.
		 * We need to do this to bring it back up later anyway, and we
		 * can't do it while the firmware is not running (e.g. in the
		 * resume reset path before RTKit is initialized), so for Apple
		 * controllers it makes sense to unconditionally do it here.
		 * Additionally, this sequence of events is reliable, while
		 * others (like disabling after bringing back the firmware on
		 * resume) seem to run into trouble under some circumstances.
		 *
		 * Both U-Boot and m1n1 also use this convention (i.e. an ANS
		 * NVMe controller is handed off with firmware shut down, in an
		 * NVMe disabled state, after a clean shutdown).
		 */
		if (shutdown)
			nvme_disable_ctrl(&anv->ctrl, shutdown);
		nvme_disable_ctrl(&anv->ctrl, false);
	}

	WRITE_ONCE(anv->ioq.enabled, false);
	WRITE_ONCE(anv->adminq.enabled, false);
	mb(); /* ensure that nvme_queue_rq() sees that enabled is cleared */
	nvme_quiesce_admin_queue(&anv->ctrl);

	/* last chance to complete any requests before nvme_cancel_request */
	spin_lock_irqsave(&anv->lock, flags);
	apple_nvme_handle_cq(&anv->ioq, true);
	apple_nvme_handle_cq(&anv->adminq, true);
	spin_unlock_irqrestore(&anv->lock, flags);

	nvme_cancel_tagset(&anv->ctrl);
	nvme_cancel_admin_tagset(&anv->ctrl);

	/*
	 * The driver will not be starting up queues again if shutting down so
	 * must flush all entered requests to their failed completion to avoid
	 * deadlocking blk-mq hot-cpu notifier.
	 */
	if (shutdown) {
		nvme_unquiesce_io_queues(&anv->ctrl);
		nvme_unquiesce_admin_queue(&anv->ctrl);
	}
}

static enum blk_eh_timer_return apple_nvme_timeout(struct request *req)
{
	struct apple_nvme_iod *iod = blk_mq_rq_to_pdu(req);
	struct apple_nvme_queue *q = iod->q;
	struct apple_nvme *anv = queue_to_apple_nvme(q);
	unsigned long flags;
	u32 csts = readl(anv->mmio_nvme + NVME_REG_CSTS);

	if (nvme_ctrl_state(&anv->ctrl) != NVME_CTRL_LIVE) {
		/*
		 * From rdma.c:
		 * If we are resetting, connecting or deleting we should
		 * complete immediately because we may block controller
		 * teardown or setup sequence
		 * - ctrl disable/shutdown fabrics requests
		 * - connect requests
		 * - initialization admin requests
		 * - I/O requests that entered after unquiescing and
		 *   the controller stopped responding
		 *
		 * All other requests should be cancelled by the error
		 * recovery work, so it's fine that we fail it here.
		 */
		dev_warn(anv->dev,
			 "I/O %d(aq:%d) timeout while not in live state\n",
			 req->tag, q->is_adminq);
		if (blk_mq_request_started(req) &&
		    !blk_mq_request_completed(req)) {
			nvme_req(req)->status = NVME_SC_HOST_ABORTED_CMD;
			nvme_req(req)->flags |= NVME_REQ_CANCELLED;
			blk_mq_complete_request(req);
		}
		return BLK_EH_DONE;
	}

	/* check if we just missed an interrupt if we're still alive */
	if (!apple_rtkit_is_crashed(anv->rtk) && !(csts & NVME_CSTS_CFS)) {
		spin_lock_irqsave(&anv->lock, flags);
		apple_nvme_handle_cq(q, false);
		spin_unlock_irqrestore(&anv->lock, flags);
		if (blk_mq_request_completed(req)) {
			dev_warn(anv->dev,
				 "I/O %d(aq:%d) timeout: completion polled\n",
				 req->tag, q->is_adminq);
			return BLK_EH_DONE;
		}
	}

	/*
	 * aborting commands isn't supported which leaves a full reset as our
	 * only option here
	 */
	dev_warn(anv->dev, "I/O %d(aq:%d) timeout: resetting controller\n",
		 req->tag, q->is_adminq);
	nvme_req(req)->flags |= NVME_REQ_CANCELLED;
	apple_nvme_disable(anv, false);
	nvme_reset_ctrl(&anv->ctrl);
	return BLK_EH_DONE;
}

static int apple_nvme_poll(struct blk_mq_hw_ctx *hctx,
			   struct io_comp_batch *iob)
{
	struct apple_nvme_queue *q = hctx->driver_data;
	struct apple_nvme *anv = queue_to_apple_nvme(q);
	bool found;
	unsigned long flags;

	spin_lock_irqsave(&anv->lock, flags);
	found = apple_nvme_poll_cq(q, iob);
	spin_unlock_irqrestore(&anv->lock, flags);

	return found;
}

static const struct blk_mq_ops apple_nvme_mq_admin_ops = {
	.queue_rq = apple_nvme_queue_rq,
	.complete = apple_nvme_complete_rq,
	.init_hctx = apple_nvme_init_hctx,
	.init_request = apple_nvme_init_request,
	.timeout = apple_nvme_timeout,
};

static const struct blk_mq_ops apple_nvme_mq_ops = {
	.queue_rq = apple_nvme_queue_rq,
	.complete = apple_nvme_complete_rq,
	.init_hctx = apple_nvme_init_hctx,
	.init_request = apple_nvme_init_request,
	.timeout = apple_nvme_timeout,
	.poll = apple_nvme_poll,
};

static void apple_nvme_init_queue(struct apple_nvme_queue *q)
{
	unsigned int depth = apple_nvme_queue_depth(q);

	q->cq_head = 0;
	q->cq_phase = 1;
	memset(q->tcbs, 0,
	       APPLE_ANS_MAX_QUEUE_DEPTH * sizeof(struct apple_nvmmu_tcb));
	memset(q->cqes, 0, depth * sizeof(struct nvme_completion));
	WRITE_ONCE(q->enabled, true);
	wmb(); /* ensure the first interrupt sees the initialization */
}

static void apple_nvme_reset_work(struct work_struct *work)
{
	unsigned int nr_io_queues = 1;
	int ret;
	u32 boot_status, aqa;
	struct apple_nvme *anv =
		container_of(work, struct apple_nvme, ctrl.reset_work);
	enum nvme_ctrl_state state = nvme_ctrl_state(&anv->ctrl);

	if (state != NVME_CTRL_RESETTING) {
		dev_warn(anv->dev, "ctrl state %d is not RESETTING\n", state);
		ret = -ENODEV;
		goto out;
	}

	/* there's unfortunately no known way to recover if RTKit crashed :( */
	if (apple_rtkit_is_crashed(anv->rtk)) {
		dev_err(anv->dev,
			"RTKit has crashed without any way to recover.");
		ret = -EIO;
		goto out;
	}

	/* RTKit must be shut down cleanly for the (soft)-reset to work */
	if (apple_rtkit_is_running(anv->rtk)) {
		/* reset the controller if it is enabled */
		if (anv->ctrl.ctrl_config & NVME_CC_ENABLE)
			apple_nvme_disable(anv, false);
		dev_dbg(anv->dev, "Trying to shut down RTKit before reset.");
		ret = apple_rtkit_shutdown(anv->rtk);
		if (ret)
			goto out;
	}

	writel(0, anv->mmio_coproc + APPLE_ANS_COPROC_CPU_CONTROL);

	ret = reset_control_assert(anv->reset);
	if (ret)
		goto out;

	ret = apple_rtkit_reinit(anv->rtk);
	if (ret)
		goto out;

	ret = reset_control_deassert(anv->reset);
	if (ret)
		goto out;

	writel(APPLE_ANS_COPROC_CPU_CONTROL_RUN,
	       anv->mmio_coproc + APPLE_ANS_COPROC_CPU_CONTROL);
	ret = apple_rtkit_boot(anv->rtk);
	if (ret) {
		dev_err(anv->dev, "ANS did not boot");
		goto out;
	}

	ret = readl_poll_timeout(anv->mmio_nvme + APPLE_ANS_BOOT_STATUS,
				 boot_status,
				 boot_status == APPLE_ANS_BOOT_STATUS_OK,
				 USEC_PER_MSEC, APPLE_ANS_BOOT_TIMEOUT);
	if (ret) {
		dev_err(anv->dev, "ANS did not initialize");
		goto out;
	}

	dev_dbg(anv->dev, "ANS booted successfully.");

	/*
	 * Limit the max command size to prevent iod->sg allocations going
	 * over a single page.
	 */
	anv->ctrl.max_hw_sectors = min_t(u32, NVME_MAX_KB_SZ << 1,
					 dma_max_mapping_size(anv->dev) >> 9);
	anv->ctrl.max_segments = NVME_MAX_SEGS;

	dma_set_max_seg_size(anv->dev, 0xffffffff);

	/*
	 * Enable NVMMU and linear submission queues.
	 * While we could keep those disabled and pretend this is slightly
	 * more common NVMe controller we'd still need some quirks (e.g.
	 * sq entries will be 128 bytes) and Apple might drop support for
	 * that mode in the future.
	 */
	writel(APPLE_ANS_LINEAR_SQ_EN,
	       anv->mmio_nvme + APPLE_ANS_LINEAR_SQ_CTRL);

	/* Allow as many pending command as possible for both queues */
	writel(APPLE_ANS_MAX_QUEUE_DEPTH | (APPLE_ANS_MAX_QUEUE_DEPTH << 16),
	       anv->mmio_nvme + APPLE_ANS_MAX_PEND_CMDS_CTRL);

	/* Setup the NVMMU for the maximum admin and IO queue depth */
	writel(APPLE_ANS_MAX_QUEUE_DEPTH - 1,
	       anv->mmio_nvme + APPLE_NVMMU_NUM_TCBS);

	/*
	 * This is probably a chicken bit: without it all commands where any PRP
	 * is set to zero (including those that don't use that field) fail and
	 * the co-processor complains about "completed with err BAD_CMD-" or
	 * a "NULL_PRP_PTR_ERR" in the syslog
	 */
	writel(readl(anv->mmio_nvme + APPLE_ANS_UNKNOWN_CTRL) &
		       ~APPLE_ANS_PRP_NULL_CHECK,
	       anv->mmio_nvme + APPLE_ANS_UNKNOWN_CTRL);

	/* Setup the admin queue */
	aqa = APPLE_NVME_AQ_DEPTH - 1;
	aqa |= aqa << 16;
	writel(aqa, anv->mmio_nvme + NVME_REG_AQA);
	writeq(anv->adminq.sq_dma_addr, anv->mmio_nvme + NVME_REG_ASQ);
	writeq(anv->adminq.cq_dma_addr, anv->mmio_nvme + NVME_REG_ACQ);

	/* Setup NVMMU for both queues */
	writeq(anv->adminq.tcb_dma_addr,
	       anv->mmio_nvme + APPLE_NVMMU_ASQ_TCB_BASE);
	writeq(anv->ioq.tcb_dma_addr,
	       anv->mmio_nvme + APPLE_NVMMU_IOSQ_TCB_BASE);

	anv->ctrl.sqsize =
		APPLE_ANS_MAX_QUEUE_DEPTH - 1; /* 0's based queue depth */
	anv->ctrl.cap = readq(anv->mmio_nvme + NVME_REG_CAP);

	dev_dbg(anv->dev, "Enabling controller now");
	ret = nvme_enable_ctrl(&anv->ctrl);
	if (ret)
		goto out;

	dev_dbg(anv->dev, "Starting admin queue");
	apple_nvme_init_queue(&anv->adminq);
	nvme_unquiesce_admin_queue(&anv->ctrl);

	if (!nvme_change_ctrl_state(&anv->ctrl, NVME_CTRL_CONNECTING)) {
		dev_warn(anv->ctrl.device,
			 "failed to mark controller CONNECTING\n");
		ret = -ENODEV;
		goto out;
	}

	ret = nvme_init_ctrl_finish(&anv->ctrl, false);
	if (ret)
		goto out;

	dev_dbg(anv->dev, "Creating IOCQ");
	ret = apple_nvme_create_cq(anv);
	if (ret)
		goto out;
	dev_dbg(anv->dev, "Creating IOSQ");
	ret = apple_nvme_create_sq(anv);
	if (ret)
		goto out_remove_cq;

	apple_nvme_init_queue(&anv->ioq);
	nr_io_queues = 1;
	ret = nvme_set_queue_count(&anv->ctrl, &nr_io_queues);
	if (ret)
		goto out_remove_sq;
	if (nr_io_queues != 1) {
		ret = -ENXIO;
		goto out_remove_sq;
	}

	anv->ctrl.queue_count = nr_io_queues + 1;

	nvme_unquiesce_io_queues(&anv->ctrl);
	nvme_wait_freeze(&anv->ctrl);
	blk_mq_update_nr_hw_queues(&anv->tagset, 1);
	nvme_unfreeze(&anv->ctrl);

	if (!nvme_change_ctrl_state(&anv->ctrl, NVME_CTRL_LIVE)) {
		dev_warn(anv->ctrl.device,
			 "failed to mark controller live state\n");
		ret = -ENODEV;
		goto out_remove_sq;
	}

	nvme_start_ctrl(&anv->ctrl);

	dev_dbg(anv->dev, "ANS boot and NVMe init completed.");
	return;

out_remove_sq:
	apple_nvme_remove_sq(anv);
out_remove_cq:
	apple_nvme_remove_cq(anv);
out:
	dev_warn(anv->ctrl.device, "Reset failure status: %d\n", ret);
	nvme_change_ctrl_state(&anv->ctrl, NVME_CTRL_DELETING);
	nvme_get_ctrl(&anv->ctrl);
	apple_nvme_disable(anv, false);
	nvme_mark_namespaces_dead(&anv->ctrl);
	if (!queue_work(nvme_wq, &anv->remove_work))
		nvme_put_ctrl(&anv->ctrl);
}

static void apple_nvme_remove_dead_ctrl_work(struct work_struct *work)
{
	struct apple_nvme *anv =
		container_of(work, struct apple_nvme, remove_work);

	nvme_put_ctrl(&anv->ctrl);
	device_release_driver(anv->dev);
}

static int apple_nvme_reg_read32(struct nvme_ctrl *ctrl, u32 off, u32 *val)
{
	*val = readl(ctrl_to_apple_nvme(ctrl)->mmio_nvme + off);
	return 0;
}

static int apple_nvme_reg_write32(struct nvme_ctrl *ctrl, u32 off, u32 val)
{
	writel(val, ctrl_to_apple_nvme(ctrl)->mmio_nvme + off);
	return 0;
}

static int apple_nvme_reg_read64(struct nvme_ctrl *ctrl, u32 off, u64 *val)
{
	*val = readq(ctrl_to_apple_nvme(ctrl)->mmio_nvme + off);
	return 0;
}

static int apple_nvme_get_address(struct nvme_ctrl *ctrl, char *buf, int size)
{
	struct device *dev = ctrl_to_apple_nvme(ctrl)->dev;

	return snprintf(buf, size, "%s\n", dev_name(dev));
}

static void apple_nvme_free_ctrl(struct nvme_ctrl *ctrl)
{
	struct apple_nvme *anv = ctrl_to_apple_nvme(ctrl);

	if (anv->ctrl.admin_q)
		blk_put_queue(anv->ctrl.admin_q);
	put_device(anv->dev);
}

static const struct nvme_ctrl_ops nvme_ctrl_ops = {
	.name = "apple-nvme",
	.module = THIS_MODULE,
	.flags = 0,
	.reg_read32 = apple_nvme_reg_read32,
	.reg_write32 = apple_nvme_reg_write32,
	.reg_read64 = apple_nvme_reg_read64,
	.free_ctrl = apple_nvme_free_ctrl,
	.get_address = apple_nvme_get_address,
};

static void apple_nvme_async_probe(void *data, async_cookie_t cookie)
{
	struct apple_nvme *anv = data;

	flush_work(&anv->ctrl.reset_work);
	flush_work(&anv->ctrl.scan_work);
	nvme_put_ctrl(&anv->ctrl);
}

static void devm_apple_nvme_put_tag_set(void *data)
{
	blk_mq_free_tag_set(data);
}

static int apple_nvme_alloc_tagsets(struct apple_nvme *anv)
{
	int ret;

	anv->admin_tagset.ops = &apple_nvme_mq_admin_ops;
	anv->admin_tagset.nr_hw_queues = 1;
	anv->admin_tagset.queue_depth = APPLE_NVME_AQ_MQ_TAG_DEPTH;
	anv->admin_tagset.timeout = NVME_ADMIN_TIMEOUT;
	anv->admin_tagset.numa_node = NUMA_NO_NODE;
	anv->admin_tagset.cmd_size = sizeof(struct apple_nvme_iod);
	anv->admin_tagset.flags = BLK_MQ_F_NO_SCHED;
	anv->admin_tagset.driver_data = &anv->adminq;

	ret = blk_mq_alloc_tag_set(&anv->admin_tagset);
	if (ret)
		return ret;
	ret = devm_add_action_or_reset(anv->dev, devm_apple_nvme_put_tag_set,
				       &anv->admin_tagset);
	if (ret)
		return ret;

	anv->tagset.ops = &apple_nvme_mq_ops;
	anv->tagset.nr_hw_queues = 1;
	anv->tagset.nr_maps = 1;
	/*
	 * Tags are used as an index to the NVMMU and must be unique across
	 * both queues. The admin queue gets the first APPLE_NVME_AQ_DEPTH which
	 * must be marked as reserved in the IO queue.
	 */
	anv->tagset.reserved_tags = APPLE_NVME_AQ_DEPTH;
	anv->tagset.queue_depth = APPLE_ANS_MAX_QUEUE_DEPTH - 1;
	anv->tagset.timeout = NVME_IO_TIMEOUT;
	anv->tagset.numa_node = NUMA_NO_NODE;
	anv->tagset.cmd_size = sizeof(struct apple_nvme_iod);
	anv->tagset.flags = BLK_MQ_F_SHOULD_MERGE;
	anv->tagset.driver_data = &anv->ioq;

	ret = blk_mq_alloc_tag_set(&anv->tagset);
	if (ret)
		return ret;
	ret = devm_add_action_or_reset(anv->dev, devm_apple_nvme_put_tag_set,
					&anv->tagset);
	if (ret)
		return ret;

	anv->ctrl.admin_tagset = &anv->admin_tagset;
	anv->ctrl.tagset = &anv->tagset;

	return 0;
}

static int apple_nvme_queue_alloc(struct apple_nvme *anv,
				  struct apple_nvme_queue *q)
{
	unsigned int depth = apple_nvme_queue_depth(q);

	q->cqes = dmam_alloc_coherent(anv->dev,
				      depth * sizeof(struct nvme_completion),
				      &q->cq_dma_addr, GFP_KERNEL);
	if (!q->cqes)
		return -ENOMEM;

	q->sqes = dmam_alloc_coherent(anv->dev,
				      depth * sizeof(struct nvme_command),
				      &q->sq_dma_addr, GFP_KERNEL);
	if (!q->sqes)
		return -ENOMEM;

	/*
	 * We need the maximum queue depth here because the NVMMU only has a
	 * single depth configuration shared between both queues.
	 */
	q->tcbs = dmam_alloc_coherent(anv->dev,
				      APPLE_ANS_MAX_QUEUE_DEPTH *
					      sizeof(struct apple_nvmmu_tcb),
				      &q->tcb_dma_addr, GFP_KERNEL);
	if (!q->tcbs)
		return -ENOMEM;

	/*
	 * initialize phase to make sure the allocated and empty memory
	 * doesn't look like a full cq already.
	 */
	q->cq_phase = 1;
	return 0;
}

static void apple_nvme_detach_genpd(struct apple_nvme *anv)
{
	int i;

	if (anv->pd_count <= 1)
		return;

	for (i = anv->pd_count - 1; i >= 0; i--) {
		if (anv->pd_link[i])
			device_link_del(anv->pd_link[i]);
		if (!IS_ERR_OR_NULL(anv->pd_dev[i]))
			dev_pm_domain_detach(anv->pd_dev[i], true);
	}
}

static int apple_nvme_attach_genpd(struct apple_nvme *anv)
{
	struct device *dev = anv->dev;
	int i;

	anv->pd_count = of_count_phandle_with_args(
		dev->of_node, "power-domains", "#power-domain-cells");
	if (anv->pd_count <= 1)
		return 0;

	anv->pd_dev = devm_kcalloc(dev, anv->pd_count, sizeof(*anv->pd_dev),
				   GFP_KERNEL);
	if (!anv->pd_dev)
		return -ENOMEM;

	anv->pd_link = devm_kcalloc(dev, anv->pd_count, sizeof(*anv->pd_link),
				    GFP_KERNEL);
	if (!anv->pd_link)
		return -ENOMEM;

	for (i = 0; i < anv->pd_count; i++) {
		anv->pd_dev[i] = dev_pm_domain_attach_by_id(dev, i);
		if (IS_ERR(anv->pd_dev[i])) {
			apple_nvme_detach_genpd(anv);
			return PTR_ERR(anv->pd_dev[i]);
		}

		anv->pd_link[i] = device_link_add(dev, anv->pd_dev[i],
						  DL_FLAG_STATELESS |
						  DL_FLAG_PM_RUNTIME |
						  DL_FLAG_RPM_ACTIVE);
		if (!anv->pd_link[i]) {
			apple_nvme_detach_genpd(anv);
			return -EINVAL;
		}
	}

	return 0;
}

static void devm_apple_nvme_mempool_destroy(void *data)
{
	mempool_destroy(data);
}

static int apple_nvme_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct apple_nvme *anv;
	int ret;

	anv = devm_kzalloc(dev, sizeof(*anv), GFP_KERNEL);
	if (!anv)
		return -ENOMEM;

	anv->dev = get_device(dev);
	anv->adminq.is_adminq = true;
	platform_set_drvdata(pdev, anv);

	ret = apple_nvme_attach_genpd(anv);
	if (ret < 0) {
		dev_err_probe(dev, ret, "Failed to attach power domains");
		goto put_dev;
	}
	if (dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64))) {
		ret = -ENXIO;
		goto put_dev;
	}

	anv->irq = platform_get_irq(pdev, 0);
	if (anv->irq < 0) {
		ret = anv->irq;
		goto put_dev;
	}
	if (!anv->irq) {
		ret = -ENXIO;
		goto put_dev;
	}

	anv->mmio_coproc = devm_platform_ioremap_resource_byname(pdev, "ans");
	if (IS_ERR(anv->mmio_coproc)) {
		ret = PTR_ERR(anv->mmio_coproc);
		goto put_dev;
	}
	anv->mmio_nvme = devm_platform_ioremap_resource_byname(pdev, "nvme");
	if (IS_ERR(anv->mmio_nvme)) {
		ret = PTR_ERR(anv->mmio_nvme);
		goto put_dev;
	}

	anv->adminq.sq_db = anv->mmio_nvme + APPLE_ANS_LINEAR_ASQ_DB;
	anv->adminq.cq_db = anv->mmio_nvme + APPLE_ANS_ACQ_DB;
	anv->ioq.sq_db = anv->mmio_nvme + APPLE_ANS_LINEAR_IOSQ_DB;
	anv->ioq.cq_db = anv->mmio_nvme + APPLE_ANS_IOCQ_DB;

	anv->sart = devm_apple_sart_get(dev);
	if (IS_ERR(anv->sart)) {
		ret = dev_err_probe(dev, PTR_ERR(anv->sart),
				    "Failed to initialize SART");
		goto put_dev;
	}

	anv->reset = devm_reset_control_array_get_exclusive(anv->dev);
	if (IS_ERR(anv->reset)) {
		ret = dev_err_probe(dev, PTR_ERR(anv->reset),
				    "Failed to get reset control");
		goto put_dev;
	}

	INIT_WORK(&anv->ctrl.reset_work, apple_nvme_reset_work);
	INIT_WORK(&anv->remove_work, apple_nvme_remove_dead_ctrl_work);
	spin_lock_init(&anv->lock);

	ret = apple_nvme_queue_alloc(anv, &anv->adminq);
	if (ret)
		goto put_dev;
	ret = apple_nvme_queue_alloc(anv, &anv->ioq);
	if (ret)
		goto put_dev;

	anv->prp_page_pool = dmam_pool_create("prp list page", anv->dev,
					      NVME_CTRL_PAGE_SIZE,
					      NVME_CTRL_PAGE_SIZE, 0);
	if (!anv->prp_page_pool) {
		ret = -ENOMEM;
		goto put_dev;
	}

	anv->prp_small_pool =
		dmam_pool_create("prp list 256", anv->dev, 256, 256, 0);
	if (!anv->prp_small_pool) {
		ret = -ENOMEM;
		goto put_dev;
	}

	WARN_ON_ONCE(apple_nvme_iod_alloc_size() > PAGE_SIZE);
	anv->iod_mempool =
		mempool_create_kmalloc_pool(1, apple_nvme_iod_alloc_size());
	if (!anv->iod_mempool) {
		ret = -ENOMEM;
		goto put_dev;
	}
	ret = devm_add_action_or_reset(anv->dev,
			devm_apple_nvme_mempool_destroy, anv->iod_mempool);
	if (ret)
		goto put_dev;

	ret = apple_nvme_alloc_tagsets(anv);
	if (ret)
		goto put_dev;

	ret = devm_request_irq(anv->dev, anv->irq, apple_nvme_irq, 0,
			       "nvme-apple", anv);
	if (ret) {
		dev_err_probe(dev, ret, "Failed to request IRQ");
		goto put_dev;
	}

	anv->rtk =
		devm_apple_rtkit_init(dev, anv, NULL, 0, &apple_nvme_rtkit_ops);
	if (IS_ERR(anv->rtk)) {
		ret = dev_err_probe(dev, PTR_ERR(anv->rtk),
				    "Failed to initialize RTKit");
		goto put_dev;
	}

	ret = nvme_init_ctrl(&anv->ctrl, anv->dev, &nvme_ctrl_ops,
			     NVME_QUIRK_SKIP_CID_GEN | NVME_QUIRK_IDENTIFY_CNS);
	if (ret) {
		dev_err_probe(dev, ret, "Failed to initialize nvme_ctrl");
		goto put_dev;
	}

	anv->ctrl.admin_q = blk_mq_alloc_queue(&anv->admin_tagset, NULL, NULL);
	if (IS_ERR(anv->ctrl.admin_q)) {
		ret = -ENOMEM;
		goto put_dev;
	}

	nvme_reset_ctrl(&anv->ctrl);
	async_schedule(apple_nvme_async_probe, anv);

	return 0;

put_dev:
	put_device(anv->dev);
	return ret;
}

static void apple_nvme_remove(struct platform_device *pdev)
{
	struct apple_nvme *anv = platform_get_drvdata(pdev);

	nvme_change_ctrl_state(&anv->ctrl, NVME_CTRL_DELETING);
	flush_work(&anv->ctrl.reset_work);
	nvme_stop_ctrl(&anv->ctrl);
	nvme_remove_namespaces(&anv->ctrl);
	apple_nvme_disable(anv, true);
	nvme_uninit_ctrl(&anv->ctrl);

	if (apple_rtkit_is_running(anv->rtk))
		apple_rtkit_shutdown(anv->rtk);

	apple_nvme_detach_genpd(anv);
}

static void apple_nvme_shutdown(struct platform_device *pdev)
{
	struct apple_nvme *anv = platform_get_drvdata(pdev);

	apple_nvme_disable(anv, true);
	if (apple_rtkit_is_running(anv->rtk))
		apple_rtkit_shutdown(anv->rtk);
}

static int apple_nvme_resume(struct device *dev)
{
	struct apple_nvme *anv = dev_get_drvdata(dev);

	return nvme_reset_ctrl(&anv->ctrl);
}

static int apple_nvme_suspend(struct device *dev)
{
	struct apple_nvme *anv = dev_get_drvdata(dev);
	int ret = 0;

	apple_nvme_disable(anv, true);

	if (apple_rtkit_is_running(anv->rtk))
		ret = apple_rtkit_shutdown(anv->rtk);

	writel(0, anv->mmio_coproc + APPLE_ANS_COPROC_CPU_CONTROL);

	return ret;
}

static DEFINE_SIMPLE_DEV_PM_OPS(apple_nvme_pm_ops, apple_nvme_suspend,
				apple_nvme_resume);

static const struct of_device_id apple_nvme_of_match[] = {
	{ .compatible = "apple,nvme-ans2" },
	{},
};
MODULE_DEVICE_TABLE(of, apple_nvme_of_match);

static struct platform_driver apple_nvme_driver = {
	.driver = {
		.name = "nvme-apple",
		.of_match_table = apple_nvme_of_match,
		.pm = pm_sleep_ptr(&apple_nvme_pm_ops),
	},
	.probe = apple_nvme_probe,
	.remove_new = apple_nvme_remove,
	.shutdown = apple_nvme_shutdown,
};
module_platform_driver(apple_nvme_driver);

MODULE_AUTHOR("Sven Peter <sven@svenpeter.dev>");
MODULE_LICENSE("GPL");
