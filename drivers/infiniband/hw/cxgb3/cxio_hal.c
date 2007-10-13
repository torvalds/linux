/*
 * Copyright (c) 2006 Chelsio, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include <asm/delay.h>

#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <net/net_namespace.h>

#include "cxio_resource.h"
#include "cxio_hal.h"
#include "cxgb3_offload.h"
#include "sge_defs.h"

static LIST_HEAD(rdev_list);
static cxio_hal_ev_callback_func_t cxio_ev_cb = NULL;

static struct cxio_rdev *cxio_hal_find_rdev_by_name(char *dev_name)
{
	struct cxio_rdev *rdev;

	list_for_each_entry(rdev, &rdev_list, entry)
		if (!strcmp(rdev->dev_name, dev_name))
			return rdev;
	return NULL;
}

static struct cxio_rdev *cxio_hal_find_rdev_by_t3cdev(struct t3cdev *tdev)
{
	struct cxio_rdev *rdev;

	list_for_each_entry(rdev, &rdev_list, entry)
		if (rdev->t3cdev_p == tdev)
			return rdev;
	return NULL;
}

int cxio_hal_cq_op(struct cxio_rdev *rdev_p, struct t3_cq *cq,
		   enum t3_cq_opcode op, u32 credit)
{
	int ret;
	struct t3_cqe *cqe;
	u32 rptr;

	struct rdma_cq_op setup;
	setup.id = cq->cqid;
	setup.credits = (op == CQ_CREDIT_UPDATE) ? credit : 0;
	setup.op = op;
	ret = rdev_p->t3cdev_p->ctl(rdev_p->t3cdev_p, RDMA_CQ_OP, &setup);

	if ((ret < 0) || (op == CQ_CREDIT_UPDATE))
		return ret;

	/*
	 * If the rearm returned an index other than our current index,
	 * then there might be CQE's in flight (being DMA'd).  We must wait
	 * here for them to complete or the consumer can miss a notification.
	 */
	if (Q_PTR2IDX((cq->rptr), cq->size_log2) != ret) {
		int i=0;

		rptr = cq->rptr;

		/*
		 * Keep the generation correct by bumping rptr until it
		 * matches the index returned by the rearm - 1.
		 */
		while (Q_PTR2IDX((rptr+1), cq->size_log2) != ret)
			rptr++;

		/*
		 * Now rptr is the index for the (last) cqe that was
		 * in-flight at the time the HW rearmed the CQ.  We
		 * spin until that CQE is valid.
		 */
		cqe = cq->queue + Q_PTR2IDX(rptr, cq->size_log2);
		while (!CQ_VLD_ENTRY(rptr, cq->size_log2, cqe)) {
			udelay(1);
			if (i++ > 1000000) {
				BUG_ON(1);
				printk(KERN_ERR "%s: stalled rnic\n",
				       rdev_p->dev_name);
				return -EIO;
			}
		}

		return 1;
	}

	return 0;
}

static int cxio_hal_clear_cq_ctx(struct cxio_rdev *rdev_p, u32 cqid)
{
	struct rdma_cq_setup setup;
	setup.id = cqid;
	setup.base_addr = 0;	/* NULL address */
	setup.size = 0;		/* disaable the CQ */
	setup.credits = 0;
	setup.credit_thres = 0;
	setup.ovfl_mode = 0;
	return (rdev_p->t3cdev_p->ctl(rdev_p->t3cdev_p, RDMA_CQ_SETUP, &setup));
}

static int cxio_hal_clear_qp_ctx(struct cxio_rdev *rdev_p, u32 qpid)
{
	u64 sge_cmd;
	struct t3_modify_qp_wr *wqe;
	struct sk_buff *skb = alloc_skb(sizeof(*wqe), GFP_KERNEL);
	if (!skb) {
		PDBG("%s alloc_skb failed\n", __FUNCTION__);
		return -ENOMEM;
	}
	wqe = (struct t3_modify_qp_wr *) skb_put(skb, sizeof(*wqe));
	memset(wqe, 0, sizeof(*wqe));
	build_fw_riwrh((struct fw_riwrh *) wqe, T3_WR_QP_MOD, 3, 0, qpid, 7);
	wqe->flags = cpu_to_be32(MODQP_WRITE_EC);
	sge_cmd = qpid << 8 | 3;
	wqe->sge_cmd = cpu_to_be64(sge_cmd);
	skb->priority = CPL_PRIORITY_CONTROL;
	return (cxgb3_ofld_send(rdev_p->t3cdev_p, skb));
}

int cxio_create_cq(struct cxio_rdev *rdev_p, struct t3_cq *cq)
{
	struct rdma_cq_setup setup;
	int size = (1UL << (cq->size_log2)) * sizeof(struct t3_cqe);

	cq->cqid = cxio_hal_get_cqid(rdev_p->rscp);
	if (!cq->cqid)
		return -ENOMEM;
	cq->sw_queue = kzalloc(size, GFP_KERNEL);
	if (!cq->sw_queue)
		return -ENOMEM;
	cq->queue = dma_alloc_coherent(&(rdev_p->rnic_info.pdev->dev),
					     (1UL << (cq->size_log2)) *
					     sizeof(struct t3_cqe),
					     &(cq->dma_addr), GFP_KERNEL);
	if (!cq->queue) {
		kfree(cq->sw_queue);
		return -ENOMEM;
	}
	pci_unmap_addr_set(cq, mapping, cq->dma_addr);
	memset(cq->queue, 0, size);
	setup.id = cq->cqid;
	setup.base_addr = (u64) (cq->dma_addr);
	setup.size = 1UL << cq->size_log2;
	setup.credits = 65535;
	setup.credit_thres = 1;
	if (rdev_p->t3cdev_p->type == T3B)
		setup.ovfl_mode = 0;
	else
		setup.ovfl_mode = 1;
	return (rdev_p->t3cdev_p->ctl(rdev_p->t3cdev_p, RDMA_CQ_SETUP, &setup));
}

int cxio_resize_cq(struct cxio_rdev *rdev_p, struct t3_cq *cq)
{
	struct rdma_cq_setup setup;
	setup.id = cq->cqid;
	setup.base_addr = (u64) (cq->dma_addr);
	setup.size = 1UL << cq->size_log2;
	setup.credits = setup.size;
	setup.credit_thres = setup.size;	/* TBD: overflow recovery */
	setup.ovfl_mode = 1;
	return (rdev_p->t3cdev_p->ctl(rdev_p->t3cdev_p, RDMA_CQ_SETUP, &setup));
}

static u32 get_qpid(struct cxio_rdev *rdev_p, struct cxio_ucontext *uctx)
{
	struct cxio_qpid_list *entry;
	u32 qpid;
	int i;

	mutex_lock(&uctx->lock);
	if (!list_empty(&uctx->qpids)) {
		entry = list_entry(uctx->qpids.next, struct cxio_qpid_list,
				   entry);
		list_del(&entry->entry);
		qpid = entry->qpid;
		kfree(entry);
	} else {
		qpid = cxio_hal_get_qpid(rdev_p->rscp);
		if (!qpid)
			goto out;
		for (i = qpid+1; i & rdev_p->qpmask; i++) {
			entry = kmalloc(sizeof *entry, GFP_KERNEL);
			if (!entry)
				break;
			entry->qpid = i;
			list_add_tail(&entry->entry, &uctx->qpids);
		}
	}
out:
	mutex_unlock(&uctx->lock);
	PDBG("%s qpid 0x%x\n", __FUNCTION__, qpid);
	return qpid;
}

static void put_qpid(struct cxio_rdev *rdev_p, u32 qpid,
		     struct cxio_ucontext *uctx)
{
	struct cxio_qpid_list *entry;

	entry = kmalloc(sizeof *entry, GFP_KERNEL);
	if (!entry)
		return;
	PDBG("%s qpid 0x%x\n", __FUNCTION__, qpid);
	entry->qpid = qpid;
	mutex_lock(&uctx->lock);
	list_add_tail(&entry->entry, &uctx->qpids);
	mutex_unlock(&uctx->lock);
}

void cxio_release_ucontext(struct cxio_rdev *rdev_p, struct cxio_ucontext *uctx)
{
	struct list_head *pos, *nxt;
	struct cxio_qpid_list *entry;

	mutex_lock(&uctx->lock);
	list_for_each_safe(pos, nxt, &uctx->qpids) {
		entry = list_entry(pos, struct cxio_qpid_list, entry);
		list_del_init(&entry->entry);
		if (!(entry->qpid & rdev_p->qpmask))
			cxio_hal_put_qpid(rdev_p->rscp, entry->qpid);
		kfree(entry);
	}
	mutex_unlock(&uctx->lock);
}

void cxio_init_ucontext(struct cxio_rdev *rdev_p, struct cxio_ucontext *uctx)
{
	INIT_LIST_HEAD(&uctx->qpids);
	mutex_init(&uctx->lock);
}

int cxio_create_qp(struct cxio_rdev *rdev_p, u32 kernel_domain,
		   struct t3_wq *wq, struct cxio_ucontext *uctx)
{
	int depth = 1UL << wq->size_log2;
	int rqsize = 1UL << wq->rq_size_log2;

	wq->qpid = get_qpid(rdev_p, uctx);
	if (!wq->qpid)
		return -ENOMEM;

	wq->rq = kzalloc(depth * sizeof(u64), GFP_KERNEL);
	if (!wq->rq)
		goto err1;

	wq->rq_addr = cxio_hal_rqtpool_alloc(rdev_p, rqsize);
	if (!wq->rq_addr)
		goto err2;

	wq->sq = kzalloc(depth * sizeof(struct t3_swsq), GFP_KERNEL);
	if (!wq->sq)
		goto err3;

	wq->queue = dma_alloc_coherent(&(rdev_p->rnic_info.pdev->dev),
					     depth * sizeof(union t3_wr),
					     &(wq->dma_addr), GFP_KERNEL);
	if (!wq->queue)
		goto err4;

	memset(wq->queue, 0, depth * sizeof(union t3_wr));
	pci_unmap_addr_set(wq, mapping, wq->dma_addr);
	wq->doorbell = (void __iomem *)rdev_p->rnic_info.kdb_addr;
	if (!kernel_domain)
		wq->udb = (u64)rdev_p->rnic_info.udbell_physbase +
					(wq->qpid << rdev_p->qpshift);
	PDBG("%s qpid 0x%x doorbell 0x%p udb 0x%llx\n", __FUNCTION__,
	     wq->qpid, wq->doorbell, (unsigned long long) wq->udb);
	return 0;
err4:
	kfree(wq->sq);
err3:
	cxio_hal_rqtpool_free(rdev_p, wq->rq_addr, rqsize);
err2:
	kfree(wq->rq);
err1:
	put_qpid(rdev_p, wq->qpid, uctx);
	return -ENOMEM;
}

int cxio_destroy_cq(struct cxio_rdev *rdev_p, struct t3_cq *cq)
{
	int err;
	err = cxio_hal_clear_cq_ctx(rdev_p, cq->cqid);
	kfree(cq->sw_queue);
	dma_free_coherent(&(rdev_p->rnic_info.pdev->dev),
			  (1UL << (cq->size_log2))
			  * sizeof(struct t3_cqe), cq->queue,
			  pci_unmap_addr(cq, mapping));
	cxio_hal_put_cqid(rdev_p->rscp, cq->cqid);
	return err;
}

int cxio_destroy_qp(struct cxio_rdev *rdev_p, struct t3_wq *wq,
		    struct cxio_ucontext *uctx)
{
	dma_free_coherent(&(rdev_p->rnic_info.pdev->dev),
			  (1UL << (wq->size_log2))
			  * sizeof(union t3_wr), wq->queue,
			  pci_unmap_addr(wq, mapping));
	kfree(wq->sq);
	cxio_hal_rqtpool_free(rdev_p, wq->rq_addr, (1UL << wq->rq_size_log2));
	kfree(wq->rq);
	put_qpid(rdev_p, wq->qpid, uctx);
	return 0;
}

static void insert_recv_cqe(struct t3_wq *wq, struct t3_cq *cq)
{
	struct t3_cqe cqe;

	PDBG("%s wq %p cq %p sw_rptr 0x%x sw_wptr 0x%x\n", __FUNCTION__,
	     wq, cq, cq->sw_rptr, cq->sw_wptr);
	memset(&cqe, 0, sizeof(cqe));
	cqe.header = cpu_to_be32(V_CQE_STATUS(TPT_ERR_SWFLUSH) |
			         V_CQE_OPCODE(T3_SEND) |
				 V_CQE_TYPE(0) |
				 V_CQE_SWCQE(1) |
				 V_CQE_QPID(wq->qpid) |
				 V_CQE_GENBIT(Q_GENBIT(cq->sw_wptr,
						       cq->size_log2)));
	*(cq->sw_queue + Q_PTR2IDX(cq->sw_wptr, cq->size_log2)) = cqe;
	cq->sw_wptr++;
}

void cxio_flush_rq(struct t3_wq *wq, struct t3_cq *cq, int count)
{
	u32 ptr;

	PDBG("%s wq %p cq %p\n", __FUNCTION__, wq, cq);

	/* flush RQ */
	PDBG("%s rq_rptr %u rq_wptr %u skip count %u\n", __FUNCTION__,
	    wq->rq_rptr, wq->rq_wptr, count);
	ptr = wq->rq_rptr + count;
	while (ptr++ != wq->rq_wptr)
		insert_recv_cqe(wq, cq);
}

static void insert_sq_cqe(struct t3_wq *wq, struct t3_cq *cq,
		          struct t3_swsq *sqp)
{
	struct t3_cqe cqe;

	PDBG("%s wq %p cq %p sw_rptr 0x%x sw_wptr 0x%x\n", __FUNCTION__,
	     wq, cq, cq->sw_rptr, cq->sw_wptr);
	memset(&cqe, 0, sizeof(cqe));
	cqe.header = cpu_to_be32(V_CQE_STATUS(TPT_ERR_SWFLUSH) |
			         V_CQE_OPCODE(sqp->opcode) |
			         V_CQE_TYPE(1) |
			         V_CQE_SWCQE(1) |
			         V_CQE_QPID(wq->qpid) |
			         V_CQE_GENBIT(Q_GENBIT(cq->sw_wptr,
						       cq->size_log2)));
	cqe.u.scqe.wrid_hi = sqp->sq_wptr;

	*(cq->sw_queue + Q_PTR2IDX(cq->sw_wptr, cq->size_log2)) = cqe;
	cq->sw_wptr++;
}

void cxio_flush_sq(struct t3_wq *wq, struct t3_cq *cq, int count)
{
	__u32 ptr;
	struct t3_swsq *sqp = wq->sq + Q_PTR2IDX(wq->sq_rptr, wq->sq_size_log2);

	ptr = wq->sq_rptr + count;
	sqp += count;
	while (ptr != wq->sq_wptr) {
		insert_sq_cqe(wq, cq, sqp);
		sqp++;
		ptr++;
	}
}

/*
 * Move all CQEs from the HWCQ into the SWCQ.
 */
void cxio_flush_hw_cq(struct t3_cq *cq)
{
	struct t3_cqe *cqe, *swcqe;

	PDBG("%s cq %p cqid 0x%x\n", __FUNCTION__, cq, cq->cqid);
	cqe = cxio_next_hw_cqe(cq);
	while (cqe) {
		PDBG("%s flushing hwcq rptr 0x%x to swcq wptr 0x%x\n",
		     __FUNCTION__, cq->rptr, cq->sw_wptr);
		swcqe = cq->sw_queue + Q_PTR2IDX(cq->sw_wptr, cq->size_log2);
		*swcqe = *cqe;
		swcqe->header |= cpu_to_be32(V_CQE_SWCQE(1));
		cq->sw_wptr++;
		cq->rptr++;
		cqe = cxio_next_hw_cqe(cq);
	}
}

static int cqe_completes_wr(struct t3_cqe *cqe, struct t3_wq *wq)
{
	if (CQE_OPCODE(*cqe) == T3_TERMINATE)
		return 0;

	if ((CQE_OPCODE(*cqe) == T3_RDMA_WRITE) && RQ_TYPE(*cqe))
		return 0;

	if ((CQE_OPCODE(*cqe) == T3_READ_RESP) && SQ_TYPE(*cqe))
		return 0;

	if ((CQE_OPCODE(*cqe) == T3_SEND) && RQ_TYPE(*cqe) &&
	    Q_EMPTY(wq->rq_rptr, wq->rq_wptr))
		return 0;

	return 1;
}

void cxio_count_scqes(struct t3_cq *cq, struct t3_wq *wq, int *count)
{
	struct t3_cqe *cqe;
	u32 ptr;

	*count = 0;
	ptr = cq->sw_rptr;
	while (!Q_EMPTY(ptr, cq->sw_wptr)) {
		cqe = cq->sw_queue + (Q_PTR2IDX(ptr, cq->size_log2));
		if ((SQ_TYPE(*cqe) || (CQE_OPCODE(*cqe) == T3_READ_RESP)) &&
		    (CQE_QPID(*cqe) == wq->qpid))
			(*count)++;
		ptr++;
	}
	PDBG("%s cq %p count %d\n", __FUNCTION__, cq, *count);
}

void cxio_count_rcqes(struct t3_cq *cq, struct t3_wq *wq, int *count)
{
	struct t3_cqe *cqe;
	u32 ptr;

	*count = 0;
	PDBG("%s count zero %d\n", __FUNCTION__, *count);
	ptr = cq->sw_rptr;
	while (!Q_EMPTY(ptr, cq->sw_wptr)) {
		cqe = cq->sw_queue + (Q_PTR2IDX(ptr, cq->size_log2));
		if (RQ_TYPE(*cqe) && (CQE_OPCODE(*cqe) != T3_READ_RESP) &&
		    (CQE_QPID(*cqe) == wq->qpid) && cqe_completes_wr(cqe, wq))
			(*count)++;
		ptr++;
	}
	PDBG("%s cq %p count %d\n", __FUNCTION__, cq, *count);
}

static int cxio_hal_init_ctrl_cq(struct cxio_rdev *rdev_p)
{
	struct rdma_cq_setup setup;
	setup.id = 0;
	setup.base_addr = 0;	/* NULL address */
	setup.size = 1;		/* enable the CQ */
	setup.credits = 0;

	/* force SGE to redirect to RspQ and interrupt */
	setup.credit_thres = 0;
	setup.ovfl_mode = 1;
	return (rdev_p->t3cdev_p->ctl(rdev_p->t3cdev_p, RDMA_CQ_SETUP, &setup));
}

static int cxio_hal_init_ctrl_qp(struct cxio_rdev *rdev_p)
{
	int err;
	u64 sge_cmd, ctx0, ctx1;
	u64 base_addr;
	struct t3_modify_qp_wr *wqe;
	struct sk_buff *skb;

	skb = alloc_skb(sizeof(*wqe), GFP_KERNEL);
	if (!skb) {
		PDBG("%s alloc_skb failed\n", __FUNCTION__);
		return -ENOMEM;
	}
	err = cxio_hal_init_ctrl_cq(rdev_p);
	if (err) {
		PDBG("%s err %d initializing ctrl_cq\n", __FUNCTION__, err);
		goto err;
	}
	rdev_p->ctrl_qp.workq = dma_alloc_coherent(
					&(rdev_p->rnic_info.pdev->dev),
					(1 << T3_CTRL_QP_SIZE_LOG2) *
					sizeof(union t3_wr),
					&(rdev_p->ctrl_qp.dma_addr),
					GFP_KERNEL);
	if (!rdev_p->ctrl_qp.workq) {
		PDBG("%s dma_alloc_coherent failed\n", __FUNCTION__);
		err = -ENOMEM;
		goto err;
	}
	pci_unmap_addr_set(&rdev_p->ctrl_qp, mapping,
			   rdev_p->ctrl_qp.dma_addr);
	rdev_p->ctrl_qp.doorbell = (void __iomem *)rdev_p->rnic_info.kdb_addr;
	memset(rdev_p->ctrl_qp.workq, 0,
	       (1 << T3_CTRL_QP_SIZE_LOG2) * sizeof(union t3_wr));

	mutex_init(&rdev_p->ctrl_qp.lock);
	init_waitqueue_head(&rdev_p->ctrl_qp.waitq);

	/* update HW Ctrl QP context */
	base_addr = rdev_p->ctrl_qp.dma_addr;
	base_addr >>= 12;
	ctx0 = (V_EC_SIZE((1 << T3_CTRL_QP_SIZE_LOG2)) |
		V_EC_BASE_LO((u32) base_addr & 0xffff));
	ctx0 <<= 32;
	ctx0 |= V_EC_CREDITS(FW_WR_NUM);
	base_addr >>= 16;
	ctx1 = (u32) base_addr;
	base_addr >>= 32;
	ctx1 |= ((u64) (V_EC_BASE_HI((u32) base_addr & 0xf) | V_EC_RESPQ(0) |
			V_EC_TYPE(0) | V_EC_GEN(1) |
			V_EC_UP_TOKEN(T3_CTL_QP_TID) | F_EC_VALID)) << 32;
	wqe = (struct t3_modify_qp_wr *) skb_put(skb, sizeof(*wqe));
	memset(wqe, 0, sizeof(*wqe));
	build_fw_riwrh((struct fw_riwrh *) wqe, T3_WR_QP_MOD, 0, 0,
		       T3_CTL_QP_TID, 7);
	wqe->flags = cpu_to_be32(MODQP_WRITE_EC);
	sge_cmd = (3ULL << 56) | FW_RI_SGEEC_START << 8 | 3;
	wqe->sge_cmd = cpu_to_be64(sge_cmd);
	wqe->ctx1 = cpu_to_be64(ctx1);
	wqe->ctx0 = cpu_to_be64(ctx0);
	PDBG("CtrlQP dma_addr 0x%llx workq %p size %d\n",
	     (unsigned long long) rdev_p->ctrl_qp.dma_addr,
	     rdev_p->ctrl_qp.workq, 1 << T3_CTRL_QP_SIZE_LOG2);
	skb->priority = CPL_PRIORITY_CONTROL;
	return (cxgb3_ofld_send(rdev_p->t3cdev_p, skb));
err:
	kfree_skb(skb);
	return err;
}

static int cxio_hal_destroy_ctrl_qp(struct cxio_rdev *rdev_p)
{
	dma_free_coherent(&(rdev_p->rnic_info.pdev->dev),
			  (1UL << T3_CTRL_QP_SIZE_LOG2)
			  * sizeof(union t3_wr), rdev_p->ctrl_qp.workq,
			  pci_unmap_addr(&rdev_p->ctrl_qp, mapping));
	return cxio_hal_clear_qp_ctx(rdev_p, T3_CTRL_QP_ID);
}

/* write len bytes of data into addr (32B aligned address)
 * If data is NULL, clear len byte of memory to zero.
 * caller aquires the ctrl_qp lock before the call
 */
static int cxio_hal_ctrl_qp_write_mem(struct cxio_rdev *rdev_p, u32 addr,
				      u32 len, void *data, int completion)
{
	u32 i, nr_wqe, copy_len;
	u8 *copy_data;
	u8 wr_len, utx_len;	/* lenght in 8 byte flit */
	enum t3_wr_flags flag;
	__be64 *wqe;
	u64 utx_cmd;
	addr &= 0x7FFFFFF;
	nr_wqe = len % 96 ? len / 96 + 1 : len / 96;	/* 96B max per WQE */
	PDBG("%s wptr 0x%x rptr 0x%x len %d, nr_wqe %d data %p addr 0x%0x\n",
	     __FUNCTION__, rdev_p->ctrl_qp.wptr, rdev_p->ctrl_qp.rptr, len,
	     nr_wqe, data, addr);
	utx_len = 3;		/* in 32B unit */
	for (i = 0; i < nr_wqe; i++) {
		if (Q_FULL(rdev_p->ctrl_qp.rptr, rdev_p->ctrl_qp.wptr,
		           T3_CTRL_QP_SIZE_LOG2)) {
			PDBG("%s ctrl_qp full wtpr 0x%0x rptr 0x%0x, "
			     "wait for more space i %d\n", __FUNCTION__,
			     rdev_p->ctrl_qp.wptr, rdev_p->ctrl_qp.rptr, i);
			if (wait_event_interruptible(rdev_p->ctrl_qp.waitq,
					     !Q_FULL(rdev_p->ctrl_qp.rptr,
						     rdev_p->ctrl_qp.wptr,
						     T3_CTRL_QP_SIZE_LOG2))) {
				PDBG("%s ctrl_qp workq interrupted\n",
				     __FUNCTION__);
				return -ERESTARTSYS;
			}
			PDBG("%s ctrl_qp wakeup, continue posting work request "
			     "i %d\n", __FUNCTION__, i);
		}
		wqe = (__be64 *)(rdev_p->ctrl_qp.workq + (rdev_p->ctrl_qp.wptr %
						(1 << T3_CTRL_QP_SIZE_LOG2)));
		flag = 0;
		if (i == (nr_wqe - 1)) {
			/* last WQE */
			flag = completion ? T3_COMPLETION_FLAG : 0;
			if (len % 32)
				utx_len = len / 32 + 1;
			else
				utx_len = len / 32;
		}

		/*
		 * Force a CQE to return the credit to the workq in case
		 * we posted more than half the max QP size of WRs
		 */
		if ((i != 0) &&
		    (i % (((1 << T3_CTRL_QP_SIZE_LOG2)) >> 1) == 0)) {
			flag = T3_COMPLETION_FLAG;
			PDBG("%s force completion at i %d\n", __FUNCTION__, i);
		}

		/* build the utx mem command */
		wqe += (sizeof(struct t3_bypass_wr) >> 3);
		utx_cmd = (T3_UTX_MEM_WRITE << 28) | (addr + i * 3);
		utx_cmd <<= 32;
		utx_cmd |= (utx_len << 28) | ((utx_len << 2) + 1);
		*wqe = cpu_to_be64(utx_cmd);
		wqe++;
		copy_data = (u8 *) data + i * 96;
		copy_len = len > 96 ? 96 : len;

		/* clear memory content if data is NULL */
		if (data)
			memcpy(wqe, copy_data, copy_len);
		else
			memset(wqe, 0, copy_len);
		if (copy_len % 32)
			memset(((u8 *) wqe) + copy_len, 0,
			       32 - (copy_len % 32));
		wr_len = ((sizeof(struct t3_bypass_wr)) >> 3) + 1 +
			 (utx_len << 2);
		wqe = (__be64 *)(rdev_p->ctrl_qp.workq + (rdev_p->ctrl_qp.wptr %
			      (1 << T3_CTRL_QP_SIZE_LOG2)));

		/* wptr in the WRID[31:0] */
		((union t3_wrid *)(wqe+1))->id0.low = rdev_p->ctrl_qp.wptr;

		/*
		 * This must be the last write with a memory barrier
		 * for the genbit
		 */
		build_fw_riwrh((struct fw_riwrh *) wqe, T3_WR_BP, flag,
			       Q_GENBIT(rdev_p->ctrl_qp.wptr,
					T3_CTRL_QP_SIZE_LOG2), T3_CTRL_QP_ID,
			       wr_len);
		if (flag == T3_COMPLETION_FLAG)
			ring_doorbell(rdev_p->ctrl_qp.doorbell, T3_CTRL_QP_ID);
		len -= 96;
		rdev_p->ctrl_qp.wptr++;
	}
	return 0;
}

/* IN: stag key, pdid, perm, zbva, to, len, page_size, pbl, and pbl_size
 * OUT: stag index, actual pbl_size, pbl_addr allocated.
 * TBD: shared memory region support
 */
static int __cxio_tpt_op(struct cxio_rdev *rdev_p, u32 reset_tpt_entry,
			 u32 *stag, u8 stag_state, u32 pdid,
			 enum tpt_mem_type type, enum tpt_mem_perm perm,
			 u32 zbva, u64 to, u32 len, u8 page_size, __be64 *pbl,
			 u32 *pbl_size, u32 *pbl_addr)
{
	int err;
	struct tpt_entry tpt;
	u32 stag_idx;
	u32 wptr;
	int rereg = (*stag != T3_STAG_UNSET);

	stag_state = stag_state > 0;
	stag_idx = (*stag) >> 8;

	if ((!reset_tpt_entry) && !(*stag != T3_STAG_UNSET)) {
		stag_idx = cxio_hal_get_stag(rdev_p->rscp);
		if (!stag_idx)
			return -ENOMEM;
		*stag = (stag_idx << 8) | ((*stag) & 0xFF);
	}
	PDBG("%s stag_state 0x%0x type 0x%0x pdid 0x%0x, stag_idx 0x%x\n",
	     __FUNCTION__, stag_state, type, pdid, stag_idx);

	if (reset_tpt_entry)
		cxio_hal_pblpool_free(rdev_p, *pbl_addr, *pbl_size << 3);
	else if (!rereg) {
		*pbl_addr = cxio_hal_pblpool_alloc(rdev_p, *pbl_size << 3);
		if (!*pbl_addr) {
			return -ENOMEM;
		}
	}

	mutex_lock(&rdev_p->ctrl_qp.lock);

	/* write PBL first if any - update pbl only if pbl list exist */
	if (pbl) {

		PDBG("%s *pdb_addr 0x%x, pbl_base 0x%x, pbl_size %d\n",
		     __FUNCTION__, *pbl_addr, rdev_p->rnic_info.pbl_base,
		     *pbl_size);
		err = cxio_hal_ctrl_qp_write_mem(rdev_p,
				(*pbl_addr >> 5),
				(*pbl_size << 3), pbl, 0);
		if (err)
			goto ret;
	}

	/* write TPT entry */
	if (reset_tpt_entry)
		memset(&tpt, 0, sizeof(tpt));
	else {
		tpt.valid_stag_pdid = cpu_to_be32(F_TPT_VALID |
				V_TPT_STAG_KEY((*stag) & M_TPT_STAG_KEY) |
				V_TPT_STAG_STATE(stag_state) |
				V_TPT_STAG_TYPE(type) | V_TPT_PDID(pdid));
		BUG_ON(page_size >= 28);
		tpt.flags_pagesize_qpid = cpu_to_be32(V_TPT_PERM(perm) |
				F_TPT_MW_BIND_ENABLE |
				V_TPT_ADDR_TYPE((zbva ? TPT_ZBTO : TPT_VATO)) |
				V_TPT_PAGE_SIZE(page_size));
		tpt.rsvd_pbl_addr = reset_tpt_entry ? 0 :
				    cpu_to_be32(V_TPT_PBL_ADDR(PBL_OFF(rdev_p, *pbl_addr)>>3));
		tpt.len = cpu_to_be32(len);
		tpt.va_hi = cpu_to_be32((u32) (to >> 32));
		tpt.va_low_or_fbo = cpu_to_be32((u32) (to & 0xFFFFFFFFULL));
		tpt.rsvd_bind_cnt_or_pstag = 0;
		tpt.rsvd_pbl_size = reset_tpt_entry ? 0 :
				  cpu_to_be32(V_TPT_PBL_SIZE((*pbl_size) >> 2));
	}
	err = cxio_hal_ctrl_qp_write_mem(rdev_p,
				       stag_idx +
				       (rdev_p->rnic_info.tpt_base >> 5),
				       sizeof(tpt), &tpt, 1);

	/* release the stag index to free pool */
	if (reset_tpt_entry)
		cxio_hal_put_stag(rdev_p->rscp, stag_idx);
ret:
	wptr = rdev_p->ctrl_qp.wptr;
	mutex_unlock(&rdev_p->ctrl_qp.lock);
	if (!err)
		if (wait_event_interruptible(rdev_p->ctrl_qp.waitq,
					     SEQ32_GE(rdev_p->ctrl_qp.rptr,
						      wptr)))
			return -ERESTARTSYS;
	return err;
}

int cxio_register_phys_mem(struct cxio_rdev *rdev_p, u32 *stag, u32 pdid,
			   enum tpt_mem_perm perm, u32 zbva, u64 to, u32 len,
			   u8 page_size, __be64 *pbl, u32 *pbl_size,
			   u32 *pbl_addr)
{
	*stag = T3_STAG_UNSET;
	return __cxio_tpt_op(rdev_p, 0, stag, 1, pdid, TPT_NON_SHARED_MR, perm,
			     zbva, to, len, page_size, pbl, pbl_size, pbl_addr);
}

int cxio_reregister_phys_mem(struct cxio_rdev *rdev_p, u32 *stag, u32 pdid,
			   enum tpt_mem_perm perm, u32 zbva, u64 to, u32 len,
			   u8 page_size, __be64 *pbl, u32 *pbl_size,
			   u32 *pbl_addr)
{
	return __cxio_tpt_op(rdev_p, 0, stag, 1, pdid, TPT_NON_SHARED_MR, perm,
			     zbva, to, len, page_size, pbl, pbl_size, pbl_addr);
}

int cxio_dereg_mem(struct cxio_rdev *rdev_p, u32 stag, u32 pbl_size,
		   u32 pbl_addr)
{
	return __cxio_tpt_op(rdev_p, 1, &stag, 0, 0, 0, 0, 0, 0ULL, 0, 0, NULL,
			     &pbl_size, &pbl_addr);
}

int cxio_allocate_window(struct cxio_rdev *rdev_p, u32 * stag, u32 pdid)
{
	u32 pbl_size = 0;
	*stag = T3_STAG_UNSET;
	return __cxio_tpt_op(rdev_p, 0, stag, 0, pdid, TPT_MW, 0, 0, 0ULL, 0, 0,
			     NULL, &pbl_size, NULL);
}

int cxio_deallocate_window(struct cxio_rdev *rdev_p, u32 stag)
{
	return __cxio_tpt_op(rdev_p, 1, &stag, 0, 0, 0, 0, 0, 0ULL, 0, 0, NULL,
			     NULL, NULL);
}

int cxio_rdma_init(struct cxio_rdev *rdev_p, struct t3_rdma_init_attr *attr)
{
	struct t3_rdma_init_wr *wqe;
	struct sk_buff *skb = alloc_skb(sizeof(*wqe), GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;
	PDBG("%s rdev_p %p\n", __FUNCTION__, rdev_p);
	wqe = (struct t3_rdma_init_wr *) __skb_put(skb, sizeof(*wqe));
	wqe->wrh.op_seop_flags = cpu_to_be32(V_FW_RIWR_OP(T3_WR_INIT));
	wqe->wrh.gen_tid_len = cpu_to_be32(V_FW_RIWR_TID(attr->tid) |
					   V_FW_RIWR_LEN(sizeof(*wqe) >> 3));
	wqe->wrid.id1 = 0;
	wqe->qpid = cpu_to_be32(attr->qpid);
	wqe->pdid = cpu_to_be32(attr->pdid);
	wqe->scqid = cpu_to_be32(attr->scqid);
	wqe->rcqid = cpu_to_be32(attr->rcqid);
	wqe->rq_addr = cpu_to_be32(attr->rq_addr - rdev_p->rnic_info.rqt_base);
	wqe->rq_size = cpu_to_be32(attr->rq_size);
	wqe->mpaattrs = attr->mpaattrs;
	wqe->qpcaps = attr->qpcaps;
	wqe->ulpdu_size = cpu_to_be16(attr->tcp_emss);
	wqe->flags = cpu_to_be32(attr->flags);
	wqe->ord = cpu_to_be32(attr->ord);
	wqe->ird = cpu_to_be32(attr->ird);
	wqe->qp_dma_addr = cpu_to_be64(attr->qp_dma_addr);
	wqe->qp_dma_size = cpu_to_be32(attr->qp_dma_size);
	wqe->irs = cpu_to_be32(attr->irs);
	skb->priority = 0;	/* 0=>ToeQ; 1=>CtrlQ */
	return (cxgb3_ofld_send(rdev_p->t3cdev_p, skb));
}

void cxio_register_ev_cb(cxio_hal_ev_callback_func_t ev_cb)
{
	cxio_ev_cb = ev_cb;
}

void cxio_unregister_ev_cb(cxio_hal_ev_callback_func_t ev_cb)
{
	cxio_ev_cb = NULL;
}

static int cxio_hal_ev_handler(struct t3cdev *t3cdev_p, struct sk_buff *skb)
{
	static int cnt;
	struct cxio_rdev *rdev_p = NULL;
	struct respQ_msg_t *rsp_msg = (struct respQ_msg_t *) skb->data;
	PDBG("%d: %s cq_id 0x%x cq_ptr 0x%x genbit %0x overflow %0x an %0x"
	     " se %0x notify %0x cqbranch %0x creditth %0x\n",
	     cnt, __FUNCTION__, RSPQ_CQID(rsp_msg), RSPQ_CQPTR(rsp_msg),
	     RSPQ_GENBIT(rsp_msg), RSPQ_OVERFLOW(rsp_msg), RSPQ_AN(rsp_msg),
	     RSPQ_SE(rsp_msg), RSPQ_NOTIFY(rsp_msg), RSPQ_CQBRANCH(rsp_msg),
	     RSPQ_CREDIT_THRESH(rsp_msg));
	PDBG("CQE: QPID 0x%0x genbit %0x type 0x%0x status 0x%0x opcode %d "
	     "len 0x%0x wrid_hi_stag 0x%x wrid_low_msn 0x%x\n",
	     CQE_QPID(rsp_msg->cqe), CQE_GENBIT(rsp_msg->cqe),
	     CQE_TYPE(rsp_msg->cqe), CQE_STATUS(rsp_msg->cqe),
	     CQE_OPCODE(rsp_msg->cqe), CQE_LEN(rsp_msg->cqe),
	     CQE_WRID_HI(rsp_msg->cqe), CQE_WRID_LOW(rsp_msg->cqe));
	rdev_p = (struct cxio_rdev *)t3cdev_p->ulp;
	if (!rdev_p) {
		PDBG("%s called by t3cdev %p with null ulp\n", __FUNCTION__,
		     t3cdev_p);
		return 0;
	}
	if (CQE_QPID(rsp_msg->cqe) == T3_CTRL_QP_ID) {
		rdev_p->ctrl_qp.rptr = CQE_WRID_LOW(rsp_msg->cqe) + 1;
		wake_up_interruptible(&rdev_p->ctrl_qp.waitq);
		dev_kfree_skb_irq(skb);
	} else if (CQE_QPID(rsp_msg->cqe) == 0xfff8)
		dev_kfree_skb_irq(skb);
	else if (cxio_ev_cb)
		(*cxio_ev_cb) (rdev_p, skb);
	else
		dev_kfree_skb_irq(skb);
	cnt++;
	return 0;
}

/* Caller takes care of locking if needed */
int cxio_rdev_open(struct cxio_rdev *rdev_p)
{
	struct net_device *netdev_p = NULL;
	int err = 0;
	if (strlen(rdev_p->dev_name)) {
		if (cxio_hal_find_rdev_by_name(rdev_p->dev_name)) {
			return -EBUSY;
		}
		netdev_p = dev_get_by_name(&init_net, rdev_p->dev_name);
		if (!netdev_p) {
			return -EINVAL;
		}
		dev_put(netdev_p);
	} else if (rdev_p->t3cdev_p) {
		if (cxio_hal_find_rdev_by_t3cdev(rdev_p->t3cdev_p)) {
			return -EBUSY;
		}
		netdev_p = rdev_p->t3cdev_p->lldev;
		strncpy(rdev_p->dev_name, rdev_p->t3cdev_p->name,
			T3_MAX_DEV_NAME_LEN);
	} else {
		PDBG("%s t3cdev_p or dev_name must be set\n", __FUNCTION__);
		return -EINVAL;
	}

	list_add_tail(&rdev_p->entry, &rdev_list);

	PDBG("%s opening rnic dev %s\n", __FUNCTION__, rdev_p->dev_name);
	memset(&rdev_p->ctrl_qp, 0, sizeof(rdev_p->ctrl_qp));
	if (!rdev_p->t3cdev_p)
		rdev_p->t3cdev_p = dev2t3cdev(netdev_p);
	rdev_p->t3cdev_p->ulp = (void *) rdev_p;
	err = rdev_p->t3cdev_p->ctl(rdev_p->t3cdev_p, RDMA_GET_PARAMS,
					 &(rdev_p->rnic_info));
	if (err) {
		printk(KERN_ERR "%s t3cdev_p(%p)->ctl returned error %d.\n",
		     __FUNCTION__, rdev_p->t3cdev_p, err);
		goto err1;
	}
	err = rdev_p->t3cdev_p->ctl(rdev_p->t3cdev_p, GET_PORTS,
				    &(rdev_p->port_info));
	if (err) {
		printk(KERN_ERR "%s t3cdev_p(%p)->ctl returned error %d.\n",
		     __FUNCTION__, rdev_p->t3cdev_p, err);
		goto err1;
	}

	/*
	 * qpshift is the number of bits to shift the qpid left in order
	 * to get the correct address of the doorbell for that qp.
	 */
	cxio_init_ucontext(rdev_p, &rdev_p->uctx);
	rdev_p->qpshift = PAGE_SHIFT -
			  ilog2(65536 >>
			            ilog2(rdev_p->rnic_info.udbell_len >>
					      PAGE_SHIFT));
	rdev_p->qpnr = rdev_p->rnic_info.udbell_len >> PAGE_SHIFT;
	rdev_p->qpmask = (65536 >> ilog2(rdev_p->qpnr)) - 1;
	PDBG("%s rnic %s info: tpt_base 0x%0x tpt_top 0x%0x num stags %d "
	     "pbl_base 0x%0x pbl_top 0x%0x rqt_base 0x%0x, rqt_top 0x%0x\n",
	     __FUNCTION__, rdev_p->dev_name, rdev_p->rnic_info.tpt_base,
	     rdev_p->rnic_info.tpt_top, cxio_num_stags(rdev_p),
	     rdev_p->rnic_info.pbl_base,
	     rdev_p->rnic_info.pbl_top, rdev_p->rnic_info.rqt_base,
	     rdev_p->rnic_info.rqt_top);
	PDBG("udbell_len 0x%0x udbell_physbase 0x%lx kdb_addr %p qpshift %lu "
	     "qpnr %d qpmask 0x%x\n",
	     rdev_p->rnic_info.udbell_len,
	     rdev_p->rnic_info.udbell_physbase, rdev_p->rnic_info.kdb_addr,
	     rdev_p->qpshift, rdev_p->qpnr, rdev_p->qpmask);

	err = cxio_hal_init_ctrl_qp(rdev_p);
	if (err) {
		printk(KERN_ERR "%s error %d initializing ctrl_qp.\n",
		       __FUNCTION__, err);
		goto err1;
	}
	err = cxio_hal_init_resource(rdev_p, cxio_num_stags(rdev_p), 0,
				     0, T3_MAX_NUM_QP, T3_MAX_NUM_CQ,
				     T3_MAX_NUM_PD);
	if (err) {
		printk(KERN_ERR "%s error %d initializing hal resources.\n",
		       __FUNCTION__, err);
		goto err2;
	}
	err = cxio_hal_pblpool_create(rdev_p);
	if (err) {
		printk(KERN_ERR "%s error %d initializing pbl mem pool.\n",
		       __FUNCTION__, err);
		goto err3;
	}
	err = cxio_hal_rqtpool_create(rdev_p);
	if (err) {
		printk(KERN_ERR "%s error %d initializing rqt mem pool.\n",
		       __FUNCTION__, err);
		goto err4;
	}
	return 0;
err4:
	cxio_hal_pblpool_destroy(rdev_p);
err3:
	cxio_hal_destroy_resource(rdev_p->rscp);
err2:
	cxio_hal_destroy_ctrl_qp(rdev_p);
err1:
	list_del(&rdev_p->entry);
	return err;
}

void cxio_rdev_close(struct cxio_rdev *rdev_p)
{
	if (rdev_p) {
		cxio_hal_pblpool_destroy(rdev_p);
		cxio_hal_rqtpool_destroy(rdev_p);
		list_del(&rdev_p->entry);
		rdev_p->t3cdev_p->ulp = NULL;
		cxio_hal_destroy_ctrl_qp(rdev_p);
		cxio_hal_destroy_resource(rdev_p->rscp);
	}
}

int __init cxio_hal_init(void)
{
	if (cxio_hal_init_rhdl_resource(T3_MAX_NUM_RI))
		return -ENOMEM;
	t3_register_cpl_handler(CPL_ASYNC_NOTIF, cxio_hal_ev_handler);
	return 0;
}

void __exit cxio_hal_exit(void)
{
	struct cxio_rdev *rdev, *tmp;

	t3_register_cpl_handler(CPL_ASYNC_NOTIF, NULL);
	list_for_each_entry_safe(rdev, tmp, &rdev_list, entry)
		cxio_rdev_close(rdev);
	cxio_hal_destroy_rhdl_resource();
}

static void flush_completed_wrs(struct t3_wq *wq, struct t3_cq *cq)
{
	struct t3_swsq *sqp;
	__u32 ptr = wq->sq_rptr;
	int count = Q_COUNT(wq->sq_rptr, wq->sq_wptr);

	sqp = wq->sq + Q_PTR2IDX(ptr, wq->sq_size_log2);
	while (count--)
		if (!sqp->signaled) {
			ptr++;
			sqp = wq->sq + Q_PTR2IDX(ptr,  wq->sq_size_log2);
		} else if (sqp->complete) {

			/*
			 * Insert this completed cqe into the swcq.
			 */
			PDBG("%s moving cqe into swcq sq idx %ld cq idx %ld\n",
			     __FUNCTION__, Q_PTR2IDX(ptr,  wq->sq_size_log2),
			     Q_PTR2IDX(cq->sw_wptr, cq->size_log2));
			sqp->cqe.header |= htonl(V_CQE_SWCQE(1));
			*(cq->sw_queue + Q_PTR2IDX(cq->sw_wptr, cq->size_log2))
				= sqp->cqe;
			cq->sw_wptr++;
			sqp->signaled = 0;
			break;
		} else
			break;
}

static void create_read_req_cqe(struct t3_wq *wq, struct t3_cqe *hw_cqe,
				struct t3_cqe *read_cqe)
{
	read_cqe->u.scqe.wrid_hi = wq->oldest_read->sq_wptr;
	read_cqe->len = wq->oldest_read->read_len;
	read_cqe->header = htonl(V_CQE_QPID(CQE_QPID(*hw_cqe)) |
				 V_CQE_SWCQE(SW_CQE(*hw_cqe)) |
				 V_CQE_OPCODE(T3_READ_REQ) |
				 V_CQE_TYPE(1));
}

/*
 * Return a ptr to the next read wr in the SWSQ or NULL.
 */
static void advance_oldest_read(struct t3_wq *wq)
{

	u32 rptr = wq->oldest_read - wq->sq + 1;
	u32 wptr = Q_PTR2IDX(wq->sq_wptr, wq->sq_size_log2);

	while (Q_PTR2IDX(rptr, wq->sq_size_log2) != wptr) {
		wq->oldest_read = wq->sq + Q_PTR2IDX(rptr, wq->sq_size_log2);

		if (wq->oldest_read->opcode == T3_READ_REQ)
			return;
		rptr++;
	}
	wq->oldest_read = NULL;
}

/*
 * cxio_poll_cq
 *
 * Caller must:
 *     check the validity of the first CQE,
 *     supply the wq assicated with the qpid.
 *
 * credit: cq credit to return to sge.
 * cqe_flushed: 1 iff the CQE is flushed.
 * cqe: copy of the polled CQE.
 *
 * return value:
 *     0       CQE returned,
 *    -1       CQE skipped, try again.
 */
int cxio_poll_cq(struct t3_wq *wq, struct t3_cq *cq, struct t3_cqe *cqe,
		     u8 *cqe_flushed, u64 *cookie, u32 *credit)
{
	int ret = 0;
	struct t3_cqe *hw_cqe, read_cqe;

	*cqe_flushed = 0;
	*credit = 0;
	hw_cqe = cxio_next_cqe(cq);

	PDBG("%s CQE OOO %d qpid 0x%0x genbit %d type %d status 0x%0x"
	     " opcode 0x%0x len 0x%0x wrid_hi_stag 0x%x wrid_low_msn 0x%x\n",
	     __FUNCTION__, CQE_OOO(*hw_cqe), CQE_QPID(*hw_cqe),
	     CQE_GENBIT(*hw_cqe), CQE_TYPE(*hw_cqe), CQE_STATUS(*hw_cqe),
	     CQE_OPCODE(*hw_cqe), CQE_LEN(*hw_cqe), CQE_WRID_HI(*hw_cqe),
	     CQE_WRID_LOW(*hw_cqe));

	/*
	 * skip cqe's not affiliated with a QP.
	 */
	if (wq == NULL) {
		ret = -1;
		goto skip_cqe;
	}

	/*
	 * Gotta tweak READ completions:
	 *	1) the cqe doesn't contain the sq_wptr from the wr.
	 *	2) opcode not reflected from the wr.
	 *	3) read_len not reflected from the wr.
	 *	4) cq_type is RQ_TYPE not SQ_TYPE.
	 */
	if (RQ_TYPE(*hw_cqe) && (CQE_OPCODE(*hw_cqe) == T3_READ_RESP)) {

		/*
		 * Don't write to the HWCQ, so create a new read req CQE
		 * in local memory.
		 */
		create_read_req_cqe(wq, hw_cqe, &read_cqe);
		hw_cqe = &read_cqe;
		advance_oldest_read(wq);
	}

	/*
	 * T3A: Discard TERMINATE CQEs.
	 */
	if (CQE_OPCODE(*hw_cqe) == T3_TERMINATE) {
		ret = -1;
		wq->error = 1;
		goto skip_cqe;
	}

	if (CQE_STATUS(*hw_cqe) || wq->error) {
		*cqe_flushed = wq->error;
		wq->error = 1;

		/*
		 * T3A inserts errors into the CQE.  We cannot return
		 * these as work completions.
		 */
		/* incoming write failures */
		if ((CQE_OPCODE(*hw_cqe) == T3_RDMA_WRITE)
		     && RQ_TYPE(*hw_cqe)) {
			ret = -1;
			goto skip_cqe;
		}
		/* incoming read request failures */
		if ((CQE_OPCODE(*hw_cqe) == T3_READ_RESP) && SQ_TYPE(*hw_cqe)) {
			ret = -1;
			goto skip_cqe;
		}

		/* incoming SEND with no receive posted failures */
		if ((CQE_OPCODE(*hw_cqe) == T3_SEND) && RQ_TYPE(*hw_cqe) &&
		    Q_EMPTY(wq->rq_rptr, wq->rq_wptr)) {
			ret = -1;
			goto skip_cqe;
		}
		goto proc_cqe;
	}

	/*
	 * RECV completion.
	 */
	if (RQ_TYPE(*hw_cqe)) {

		/*
		 * HW only validates 4 bits of MSN.  So we must validate that
		 * the MSN in the SEND is the next expected MSN.  If its not,
		 * then we complete this with TPT_ERR_MSN and mark the wq in
		 * error.
		 */
		if (unlikely((CQE_WRID_MSN(*hw_cqe) != (wq->rq_rptr + 1)))) {
			wq->error = 1;
			hw_cqe->header |= htonl(V_CQE_STATUS(TPT_ERR_MSN));
			goto proc_cqe;
		}
		goto proc_cqe;
	}

	/*
	 * If we get here its a send completion.
	 *
	 * Handle out of order completion. These get stuffed
	 * in the SW SQ. Then the SW SQ is walked to move any
	 * now in-order completions into the SW CQ.  This handles
	 * 2 cases:
	 *	1) reaping unsignaled WRs when the first subsequent
	 *	   signaled WR is completed.
	 *	2) out of order read completions.
	 */
	if (!SW_CQE(*hw_cqe) && (CQE_WRID_SQ_WPTR(*hw_cqe) != wq->sq_rptr)) {
		struct t3_swsq *sqp;

		PDBG("%s out of order completion going in swsq at idx %ld\n",
		     __FUNCTION__,
		     Q_PTR2IDX(CQE_WRID_SQ_WPTR(*hw_cqe), wq->sq_size_log2));
		sqp = wq->sq +
		      Q_PTR2IDX(CQE_WRID_SQ_WPTR(*hw_cqe), wq->sq_size_log2);
		sqp->cqe = *hw_cqe;
		sqp->complete = 1;
		ret = -1;
		goto flush_wq;
	}

proc_cqe:
	*cqe = *hw_cqe;

	/*
	 * Reap the associated WR(s) that are freed up with this
	 * completion.
	 */
	if (SQ_TYPE(*hw_cqe)) {
		wq->sq_rptr = CQE_WRID_SQ_WPTR(*hw_cqe);
		PDBG("%s completing sq idx %ld\n", __FUNCTION__,
		     Q_PTR2IDX(wq->sq_rptr, wq->sq_size_log2));
		*cookie = (wq->sq +
			   Q_PTR2IDX(wq->sq_rptr, wq->sq_size_log2))->wr_id;
		wq->sq_rptr++;
	} else {
		PDBG("%s completing rq idx %ld\n", __FUNCTION__,
		     Q_PTR2IDX(wq->rq_rptr, wq->rq_size_log2));
		*cookie = *(wq->rq + Q_PTR2IDX(wq->rq_rptr, wq->rq_size_log2));
		wq->rq_rptr++;
	}

flush_wq:
	/*
	 * Flush any completed cqes that are now in-order.
	 */
	flush_completed_wrs(wq, cq);

skip_cqe:
	if (SW_CQE(*hw_cqe)) {
		PDBG("%s cq %p cqid 0x%x skip sw cqe sw_rptr 0x%x\n",
		     __FUNCTION__, cq, cq->cqid, cq->sw_rptr);
		++cq->sw_rptr;
	} else {
		PDBG("%s cq %p cqid 0x%x skip hw cqe rptr 0x%x\n",
		     __FUNCTION__, cq, cq->cqid, cq->rptr);
		++cq->rptr;

		/*
		 * T3A: compute credits.
		 */
		if (((cq->rptr - cq->wptr) > (1 << (cq->size_log2 - 1)))
		    || ((cq->rptr - cq->wptr) >= 128)) {
			*credit = cq->rptr - cq->wptr;
			cq->wptr = cq->rptr;
		}
	}
	return ret;
}
