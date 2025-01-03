// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)

#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/log2.h>
#include <linux/mm.h>
#include <linux/netdevice.h>
#include <linux/pci.h>
#include <linux/slab.h>

#include "fun_dev.h"
#include "fun_queue.h"

/* Allocate memory for a queue. This includes the memory for the HW descriptor
 * ring, an optional 64b HW write-back area, and an optional SW state ring.
 * Returns the virtual and DMA addresses of the HW ring, the VA of the SW ring,
 * and the VA of the write-back area.
 */
void *fun_alloc_ring_mem(struct device *dma_dev, size_t depth,
			 size_t hw_desc_sz, size_t sw_desc_sz, bool wb,
			 int numa_node, dma_addr_t *dma_addr, void **sw_va,
			 volatile __be64 **wb_va)
{
	int dev_node = dev_to_node(dma_dev);
	size_t dma_sz;
	void *va;

	if (numa_node == NUMA_NO_NODE)
		numa_node = dev_node;

	/* Place optional write-back area at end of descriptor ring. */
	dma_sz = hw_desc_sz * depth;
	if (wb)
		dma_sz += sizeof(u64);

	set_dev_node(dma_dev, numa_node);
	va = dma_alloc_coherent(dma_dev, dma_sz, dma_addr, GFP_KERNEL);
	set_dev_node(dma_dev, dev_node);
	if (!va)
		return NULL;

	if (sw_desc_sz) {
		*sw_va = kvzalloc_node(sw_desc_sz * depth, GFP_KERNEL,
				       numa_node);
		if (!*sw_va) {
			dma_free_coherent(dma_dev, dma_sz, va, *dma_addr);
			return NULL;
		}
	}

	if (wb)
		*wb_va = va + dma_sz - sizeof(u64);
	return va;
}
EXPORT_SYMBOL_GPL(fun_alloc_ring_mem);

void fun_free_ring_mem(struct device *dma_dev, size_t depth, size_t hw_desc_sz,
		       bool wb, void *hw_va, dma_addr_t dma_addr, void *sw_va)
{
	if (hw_va) {
		size_t sz = depth * hw_desc_sz;

		if (wb)
			sz += sizeof(u64);
		dma_free_coherent(dma_dev, sz, hw_va, dma_addr);
	}
	kvfree(sw_va);
}
EXPORT_SYMBOL_GPL(fun_free_ring_mem);

/* Prepare and issue an admin command to create an SQ on the device with the
 * provided parameters. If the queue ID is auto-allocated by the device it is
 * returned in *sqidp.
 */
int fun_sq_create(struct fun_dev *fdev, u16 flags, u32 sqid, u32 cqid,
		  u8 sqe_size_log2, u32 sq_depth, dma_addr_t dma_addr,
		  u8 coal_nentries, u8 coal_usec, u32 irq_num,
		  u32 scan_start_id, u32 scan_end_id,
		  u32 rq_buf_size_log2, u32 *sqidp, u32 __iomem **dbp)
{
	union {
		struct fun_admin_epsq_req req;
		struct fun_admin_generic_create_rsp rsp;
	} cmd;
	dma_addr_t wb_addr;
	u32 hw_qid;
	int rc;

	if (sq_depth > fdev->q_depth)
		return -EINVAL;
	if (flags & FUN_ADMIN_EPSQ_CREATE_FLAG_RQ)
		sqe_size_log2 = ilog2(sizeof(struct fun_eprq_rqbuf));

	wb_addr = dma_addr + (sq_depth << sqe_size_log2);

	cmd.req.common = FUN_ADMIN_REQ_COMMON_INIT2(FUN_ADMIN_OP_EPSQ,
						    sizeof(cmd.req));
	cmd.req.u.create =
		FUN_ADMIN_EPSQ_CREATE_REQ_INIT(FUN_ADMIN_SUBOP_CREATE, flags,
					       sqid, cqid, sqe_size_log2,
					       sq_depth - 1, dma_addr, 0,
					       coal_nentries, coal_usec,
					       irq_num, scan_start_id,
					       scan_end_id, 0,
					       rq_buf_size_log2,
					       ilog2(sizeof(u64)), wb_addr);

	rc = fun_submit_admin_sync_cmd(fdev, &cmd.req.common,
				       &cmd.rsp, sizeof(cmd.rsp), 0);
	if (rc)
		return rc;

	hw_qid = be32_to_cpu(cmd.rsp.id);
	*dbp = fun_sq_db_addr(fdev, hw_qid);
	if (flags & FUN_ADMIN_RES_CREATE_FLAG_ALLOCATOR)
		*sqidp = hw_qid;
	return rc;
}
EXPORT_SYMBOL_GPL(fun_sq_create);

/* Prepare and issue an admin command to create a CQ on the device with the
 * provided parameters. If the queue ID is auto-allocated by the device it is
 * returned in *cqidp.
 */
int fun_cq_create(struct fun_dev *fdev, u16 flags, u32 cqid, u32 rqid,
		  u8 cqe_size_log2, u32 cq_depth, dma_addr_t dma_addr,
		  u16 headroom, u16 tailroom, u8 coal_nentries, u8 coal_usec,
		  u32 irq_num, u32 scan_start_id, u32 scan_end_id, u32 *cqidp,
		  u32 __iomem **dbp)
{
	union {
		struct fun_admin_epcq_req req;
		struct fun_admin_generic_create_rsp rsp;
	} cmd;
	u32 hw_qid;
	int rc;

	if (cq_depth > fdev->q_depth)
		return -EINVAL;

	cmd.req.common = FUN_ADMIN_REQ_COMMON_INIT2(FUN_ADMIN_OP_EPCQ,
						    sizeof(cmd.req));
	cmd.req.u.create =
		FUN_ADMIN_EPCQ_CREATE_REQ_INIT(FUN_ADMIN_SUBOP_CREATE, flags,
					       cqid, rqid, cqe_size_log2,
					       cq_depth - 1, dma_addr, tailroom,
					       headroom / 2, 0, coal_nentries,
					       coal_usec, irq_num,
					       scan_start_id, scan_end_id, 0);

	rc = fun_submit_admin_sync_cmd(fdev, &cmd.req.common,
				       &cmd.rsp, sizeof(cmd.rsp), 0);
	if (rc)
		return rc;

	hw_qid = be32_to_cpu(cmd.rsp.id);
	*dbp = fun_cq_db_addr(fdev, hw_qid);
	if (flags & FUN_ADMIN_RES_CREATE_FLAG_ALLOCATOR)
		*cqidp = hw_qid;
	return rc;
}
EXPORT_SYMBOL_GPL(fun_cq_create);

static bool fun_sq_is_head_wb(const struct fun_queue *funq)
{
	return funq->sq_flags & FUN_ADMIN_EPSQ_CREATE_FLAG_HEAD_WB_ADDRESS;
}

static void fun_clean_rq(struct fun_queue *funq)
{
	struct fun_dev *fdev = funq->fdev;
	struct fun_rq_info *rqinfo;
	unsigned int i;

	for (i = 0; i < funq->rq_depth; i++) {
		rqinfo = &funq->rq_info[i];
		if (rqinfo->page) {
			dma_unmap_page(fdev->dev, rqinfo->dma, PAGE_SIZE,
				       DMA_FROM_DEVICE);
			put_page(rqinfo->page);
			rqinfo->page = NULL;
		}
	}
}

static int fun_fill_rq(struct fun_queue *funq)
{
	struct device *dev = funq->fdev->dev;
	int i, node = dev_to_node(dev);
	struct fun_rq_info *rqinfo;

	for (i = 0; i < funq->rq_depth; i++) {
		rqinfo = &funq->rq_info[i];
		rqinfo->page = alloc_pages_node(node, GFP_KERNEL, 0);
		if (unlikely(!rqinfo->page))
			return -ENOMEM;

		rqinfo->dma = dma_map_page(dev, rqinfo->page, 0,
					   PAGE_SIZE, DMA_FROM_DEVICE);
		if (unlikely(dma_mapping_error(dev, rqinfo->dma))) {
			put_page(rqinfo->page);
			rqinfo->page = NULL;
			return -ENOMEM;
		}

		funq->rqes[i] = FUN_EPRQ_RQBUF_INIT(rqinfo->dma);
	}

	funq->rq_tail = funq->rq_depth - 1;
	return 0;
}

static void fun_rq_update_pos(struct fun_queue *funq, int buf_offset)
{
	if (buf_offset <= funq->rq_buf_offset) {
		struct fun_rq_info *rqinfo = &funq->rq_info[funq->rq_buf_idx];
		struct device *dev = funq->fdev->dev;

		dma_sync_single_for_device(dev, rqinfo->dma, PAGE_SIZE,
					   DMA_FROM_DEVICE);
		funq->num_rqe_to_fill++;
		if (++funq->rq_buf_idx == funq->rq_depth)
			funq->rq_buf_idx = 0;
	}
	funq->rq_buf_offset = buf_offset;
}

/* Given a command response with data scattered across >= 1 RQ buffers return
 * a pointer to a contiguous buffer containing all the data. If the data is in
 * one RQ buffer the start address within that buffer is returned, otherwise a
 * new buffer is allocated and the data is gathered into it.
 */
static void *fun_data_from_rq(struct fun_queue *funq,
			      const struct fun_rsp_common *rsp, bool *need_free)
{
	u32 bufoff, total_len, remaining, fragsize, dataoff;
	struct device *dma_dev = funq->fdev->dev;
	const struct fun_dataop_rqbuf *databuf;
	const struct fun_dataop_hdr *dataop;
	const struct fun_rq_info *rqinfo;
	void *data;

	dataop = (void *)rsp + rsp->suboff8 * 8;
	total_len = be32_to_cpu(dataop->total_len);

	if (likely(dataop->nsgl == 1)) {
		databuf = (struct fun_dataop_rqbuf *)dataop->imm;
		bufoff = be32_to_cpu(databuf->bufoff);
		fun_rq_update_pos(funq, bufoff);
		rqinfo = &funq->rq_info[funq->rq_buf_idx];
		dma_sync_single_for_cpu(dma_dev, rqinfo->dma + bufoff,
					total_len, DMA_FROM_DEVICE);
		*need_free = false;
		return page_address(rqinfo->page) + bufoff;
	}

	/* For scattered completions gather the fragments into one buffer. */

	data = kmalloc(total_len, GFP_ATOMIC);
	/* NULL is OK here. In case of failure we still need to consume the data
	 * for proper buffer accounting but indicate an error in the response.
	 */
	if (likely(data))
		*need_free = true;

	dataoff = 0;
	for (remaining = total_len; remaining; remaining -= fragsize) {
		fun_rq_update_pos(funq, 0);
		fragsize = min_t(unsigned int, PAGE_SIZE, remaining);
		if (data) {
			rqinfo = &funq->rq_info[funq->rq_buf_idx];
			dma_sync_single_for_cpu(dma_dev, rqinfo->dma, fragsize,
						DMA_FROM_DEVICE);
			memcpy(data + dataoff, page_address(rqinfo->page),
			       fragsize);
			dataoff += fragsize;
		}
	}
	return data;
}

unsigned int __fun_process_cq(struct fun_queue *funq, unsigned int max)
{
	const struct fun_cqe_info *info;
	struct fun_rsp_common *rsp;
	unsigned int new_cqes;
	u16 sf_p, flags;
	bool need_free;
	void *cqe;

	if (!max)
		max = funq->cq_depth - 1;

	for (new_cqes = 0; new_cqes < max; new_cqes++) {
		cqe = funq->cqes + (funq->cq_head << funq->cqe_size_log2);
		info = funq_cqe_info(funq, cqe);
		sf_p = be16_to_cpu(info->sf_p);

		if ((sf_p & 1) != funq->cq_phase)
			break;

		/* ensure the phase tag is read before other CQE fields */
		dma_rmb();

		if (++funq->cq_head == funq->cq_depth) {
			funq->cq_head = 0;
			funq->cq_phase = !funq->cq_phase;
		}

		rsp = cqe;
		flags = be16_to_cpu(rsp->flags);

		need_free = false;
		if (unlikely(flags & FUN_REQ_COMMON_FLAG_CQE_IN_RQBUF)) {
			rsp = fun_data_from_rq(funq, rsp, &need_free);
			if (!rsp) {
				rsp = cqe;
				rsp->len8 = 1;
				if (rsp->ret == 0)
					rsp->ret = ENOMEM;
			}
		}

		if (funq->cq_cb)
			funq->cq_cb(funq, funq->cb_data, rsp, info);
		if (need_free)
			kfree(rsp);
	}

	dev_dbg(funq->fdev->dev, "CQ %u, new CQEs %u/%u, head %u, phase %u\n",
		funq->cqid, new_cqes, max, funq->cq_head, funq->cq_phase);
	return new_cqes;
}

unsigned int fun_process_cq(struct fun_queue *funq, unsigned int max)
{
	unsigned int processed;
	u32 db;

	processed = __fun_process_cq(funq, max);

	if (funq->num_rqe_to_fill) {
		funq->rq_tail = (funq->rq_tail + funq->num_rqe_to_fill) %
				funq->rq_depth;
		funq->num_rqe_to_fill = 0;
		writel(funq->rq_tail, funq->rq_db);
	}

	db = funq->cq_head | FUN_DB_IRQ_ARM_F;
	writel(db, funq->cq_db);
	return processed;
}

static int fun_alloc_sqes(struct fun_queue *funq)
{
	funq->sq_cmds = fun_alloc_ring_mem(funq->fdev->dev, funq->sq_depth,
					   1 << funq->sqe_size_log2, 0,
					   fun_sq_is_head_wb(funq),
					   NUMA_NO_NODE, &funq->sq_dma_addr,
					   NULL, &funq->sq_head);
	return funq->sq_cmds ? 0 : -ENOMEM;
}

static int fun_alloc_cqes(struct fun_queue *funq)
{
	funq->cqes = fun_alloc_ring_mem(funq->fdev->dev, funq->cq_depth,
					1 << funq->cqe_size_log2, 0, false,
					NUMA_NO_NODE, &funq->cq_dma_addr, NULL,
					NULL);
	return funq->cqes ? 0 : -ENOMEM;
}

static int fun_alloc_rqes(struct fun_queue *funq)
{
	funq->rqes = fun_alloc_ring_mem(funq->fdev->dev, funq->rq_depth,
					sizeof(*funq->rqes),
					sizeof(*funq->rq_info), false,
					NUMA_NO_NODE, &funq->rq_dma_addr,
					(void **)&funq->rq_info, NULL);
	return funq->rqes ? 0 : -ENOMEM;
}

/* Free a queue's structures. */
void fun_free_queue(struct fun_queue *funq)
{
	struct device *dev = funq->fdev->dev;

	fun_free_ring_mem(dev, funq->cq_depth, 1 << funq->cqe_size_log2, false,
			  funq->cqes, funq->cq_dma_addr, NULL);
	fun_free_ring_mem(dev, funq->sq_depth, 1 << funq->sqe_size_log2,
			  fun_sq_is_head_wb(funq), funq->sq_cmds,
			  funq->sq_dma_addr, NULL);

	if (funq->rqes) {
		fun_clean_rq(funq);
		fun_free_ring_mem(dev, funq->rq_depth, sizeof(*funq->rqes),
				  false, funq->rqes, funq->rq_dma_addr,
				  funq->rq_info);
	}

	kfree(funq);
}

/* Allocate and initialize a funq's structures. */
struct fun_queue *fun_alloc_queue(struct fun_dev *fdev, int qid,
				  const struct fun_queue_alloc_req *req)
{
	struct fun_queue *funq = kzalloc(sizeof(*funq), GFP_KERNEL);

	if (!funq)
		return NULL;

	funq->fdev = fdev;
	spin_lock_init(&funq->sq_lock);

	funq->qid = qid;

	/* Initial CQ/SQ/RQ ids */
	if (req->rq_depth) {
		funq->cqid = 2 * qid;
		if (funq->qid) {
			/* I/O Q: use rqid = cqid, sqid = +1 */
			funq->rqid = funq->cqid;
			funq->sqid = funq->rqid + 1;
		} else {
			/* Admin Q: sqid is always 0, use ID 1 for RQ */
			funq->sqid = 0;
			funq->rqid = 1;
		}
	} else {
		funq->cqid = qid;
		funq->sqid = qid;
	}

	funq->cq_flags = req->cq_flags;
	funq->sq_flags = req->sq_flags;

	funq->cqe_size_log2 = req->cqe_size_log2;
	funq->sqe_size_log2 = req->sqe_size_log2;

	funq->cq_depth = req->cq_depth;
	funq->sq_depth = req->sq_depth;

	funq->cq_intcoal_nentries = req->cq_intcoal_nentries;
	funq->cq_intcoal_usec = req->cq_intcoal_usec;

	funq->sq_intcoal_nentries = req->sq_intcoal_nentries;
	funq->sq_intcoal_usec = req->sq_intcoal_usec;

	if (fun_alloc_cqes(funq))
		goto free_funq;

	funq->cq_phase = 1;

	if (fun_alloc_sqes(funq))
		goto free_funq;

	if (req->rq_depth) {
		funq->rq_flags = req->rq_flags | FUN_ADMIN_EPSQ_CREATE_FLAG_RQ;
		funq->rq_depth = req->rq_depth;
		funq->rq_buf_offset = -1;

		if (fun_alloc_rqes(funq) || fun_fill_rq(funq))
			goto free_funq;
	}

	funq->cq_vector = -1;
	funq->cqe_info_offset = (1 << funq->cqe_size_log2) - sizeof(struct fun_cqe_info);

	/* SQ/CQ 0 are implicitly created, assign their doorbells now.
	 * Other queues are assigned doorbells at their explicit creation.
	 */
	if (funq->sqid == 0)
		funq->sq_db = fun_sq_db_addr(fdev, 0);
	if (funq->cqid == 0)
		funq->cq_db = fun_cq_db_addr(fdev, 0);

	return funq;

free_funq:
	fun_free_queue(funq);
	return NULL;
}

/* Create a funq's RQ on the device. */
int fun_create_rq(struct fun_queue *funq)
{
	struct fun_dev *fdev = funq->fdev;
	int rc;

	rc = fun_sq_create(fdev, funq->rq_flags, funq->rqid, funq->cqid, 0,
			   funq->rq_depth, funq->rq_dma_addr, 0, 0,
			   funq->cq_vector, 0, 0, PAGE_SHIFT, &funq->rqid,
			   &funq->rq_db);
	if (!rc)
		dev_dbg(fdev->dev, "created RQ %u\n", funq->rqid);

	return rc;
}

static unsigned int funq_irq(struct fun_queue *funq)
{
	return pci_irq_vector(to_pci_dev(funq->fdev->dev), funq->cq_vector);
}

int fun_request_irq(struct fun_queue *funq, const char *devname,
		    irq_handler_t handler, void *data)
{
	int rc;

	if (funq->cq_vector < 0)
		return -EINVAL;

	funq->irq_handler = handler;
	funq->irq_data = data;

	snprintf(funq->irqname, sizeof(funq->irqname),
		 funq->qid ? "%s-q[%d]" : "%s-adminq", devname, funq->qid);

	rc = request_irq(funq_irq(funq), handler, 0, funq->irqname, data);
	if (rc)
		funq->irq_handler = NULL;

	return rc;
}

void fun_free_irq(struct fun_queue *funq)
{
	if (funq->irq_handler) {
		unsigned int vector = funq_irq(funq);

		free_irq(vector, funq->irq_data);
		funq->irq_handler = NULL;
		funq->irq_data = NULL;
	}
}
