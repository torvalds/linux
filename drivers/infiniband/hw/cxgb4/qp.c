/*
 * Copyright (c) 2009-2010 Chelsio, Inc. All rights reserved.
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

#include <linux/module.h>
#include <rdma/uverbs_ioctl.h>

#include "iw_cxgb4.h"

static int db_delay_usecs = 1;
module_param(db_delay_usecs, int, 0644);
MODULE_PARM_DESC(db_delay_usecs, "Usecs to delay awaiting db fifo to drain");

static int ocqp_support = 1;
module_param(ocqp_support, int, 0644);
MODULE_PARM_DESC(ocqp_support, "Support on-chip SQs (default=1)");

int db_fc_threshold = 1000;
module_param(db_fc_threshold, int, 0644);
MODULE_PARM_DESC(db_fc_threshold,
		 "QP count/threshold that triggers"
		 " automatic db flow control mode (default = 1000)");

int db_coalescing_threshold;
module_param(db_coalescing_threshold, int, 0644);
MODULE_PARM_DESC(db_coalescing_threshold,
		 "QP count/threshold that triggers"
		 " disabling db coalescing (default = 0)");

static int max_fr_immd = T4_MAX_FR_IMMD;
module_param(max_fr_immd, int, 0644);
MODULE_PARM_DESC(max_fr_immd, "fastreg threshold for using DSGL instead of immediate");

static int alloc_ird(struct c4iw_dev *dev, u32 ird)
{
	int ret = 0;

	xa_lock_irq(&dev->qps);
	if (ird <= dev->avail_ird)
		dev->avail_ird -= ird;
	else
		ret = -ENOMEM;
	xa_unlock_irq(&dev->qps);

	if (ret)
		dev_warn(&dev->rdev.lldi.pdev->dev,
			 "device IRD resources exhausted\n");

	return ret;
}

static void free_ird(struct c4iw_dev *dev, int ird)
{
	xa_lock_irq(&dev->qps);
	dev->avail_ird += ird;
	xa_unlock_irq(&dev->qps);
}

static void set_state(struct c4iw_qp *qhp, enum c4iw_qp_state state)
{
	unsigned long flag;
	spin_lock_irqsave(&qhp->lock, flag);
	qhp->attr.state = state;
	spin_unlock_irqrestore(&qhp->lock, flag);
}

static void dealloc_oc_sq(struct c4iw_rdev *rdev, struct t4_sq *sq)
{
	c4iw_ocqp_pool_free(rdev, sq->dma_addr, sq->memsize);
}

static void dealloc_host_sq(struct c4iw_rdev *rdev, struct t4_sq *sq)
{
	dma_free_coherent(&(rdev->lldi.pdev->dev), sq->memsize, sq->queue,
			  dma_unmap_addr(sq, mapping));
}

static void dealloc_sq(struct c4iw_rdev *rdev, struct t4_sq *sq)
{
	if (t4_sq_onchip(sq))
		dealloc_oc_sq(rdev, sq);
	else
		dealloc_host_sq(rdev, sq);
}

static int alloc_oc_sq(struct c4iw_rdev *rdev, struct t4_sq *sq)
{
	if (!ocqp_support || !ocqp_supported(&rdev->lldi))
		return -ENOSYS;
	sq->dma_addr = c4iw_ocqp_pool_alloc(rdev, sq->memsize);
	if (!sq->dma_addr)
		return -ENOMEM;
	sq->phys_addr = rdev->oc_mw_pa + sq->dma_addr -
			rdev->lldi.vr->ocq.start;
	sq->queue = (__force union t4_wr *)(rdev->oc_mw_kva + sq->dma_addr -
					    rdev->lldi.vr->ocq.start);
	sq->flags |= T4_SQ_ONCHIP;
	return 0;
}

static int alloc_host_sq(struct c4iw_rdev *rdev, struct t4_sq *sq)
{
	sq->queue = dma_alloc_coherent(&(rdev->lldi.pdev->dev), sq->memsize,
				       &(sq->dma_addr), GFP_KERNEL);
	if (!sq->queue)
		return -ENOMEM;
	sq->phys_addr = virt_to_phys(sq->queue);
	dma_unmap_addr_set(sq, mapping, sq->dma_addr);
	return 0;
}

static int alloc_sq(struct c4iw_rdev *rdev, struct t4_sq *sq, int user)
{
	int ret = -ENOSYS;
	if (user)
		ret = alloc_oc_sq(rdev, sq);
	if (ret)
		ret = alloc_host_sq(rdev, sq);
	return ret;
}

static int destroy_qp(struct c4iw_rdev *rdev, struct t4_wq *wq,
		      struct c4iw_dev_ucontext *uctx, int has_rq)
{
	/*
	 * uP clears EQ contexts when the connection exits rdma mode,
	 * so no need to post a RESET WR for these EQs.
	 */
	dealloc_sq(rdev, &wq->sq);
	kfree(wq->sq.sw_sq);
	c4iw_put_qpid(rdev, wq->sq.qid, uctx);

	if (has_rq) {
		dma_free_coherent(&rdev->lldi.pdev->dev,
				  wq->rq.memsize, wq->rq.queue,
				  dma_unmap_addr(&wq->rq, mapping));
		c4iw_rqtpool_free(rdev, wq->rq.rqt_hwaddr, wq->rq.rqt_size);
		kfree(wq->rq.sw_rq);
		c4iw_put_qpid(rdev, wq->rq.qid, uctx);
	}
	return 0;
}

/*
 * Determine the BAR2 virtual address and qid. If pbar2_pa is not NULL,
 * then this is a user mapping so compute the page-aligned physical address
 * for mapping.
 */
void __iomem *c4iw_bar2_addrs(struct c4iw_rdev *rdev, unsigned int qid,
			      enum cxgb4_bar2_qtype qtype,
			      unsigned int *pbar2_qid, u64 *pbar2_pa)
{
	u64 bar2_qoffset;
	int ret;

	ret = cxgb4_bar2_sge_qregs(rdev->lldi.ports[0], qid, qtype,
				   pbar2_pa ? 1 : 0,
				   &bar2_qoffset, pbar2_qid);
	if (ret)
		return NULL;

	if (pbar2_pa)
		*pbar2_pa = (rdev->bar2_pa + bar2_qoffset) & PAGE_MASK;

	if (is_t4(rdev->lldi.adapter_type))
		return NULL;

	return rdev->bar2_kva + bar2_qoffset;
}

static int create_qp(struct c4iw_rdev *rdev, struct t4_wq *wq,
		     struct t4_cq *rcq, struct t4_cq *scq,
		     struct c4iw_dev_ucontext *uctx,
		     struct c4iw_wr_wait *wr_waitp,
		     int need_rq)
{
	int user = (uctx != &rdev->uctx);
	struct fw_ri_res_wr *res_wr;
	struct fw_ri_res *res;
	int wr_len;
	struct sk_buff *skb;
	int ret = 0;
	int eqsize;

	wq->sq.qid = c4iw_get_qpid(rdev, uctx);
	if (!wq->sq.qid)
		return -ENOMEM;

	if (need_rq) {
		wq->rq.qid = c4iw_get_qpid(rdev, uctx);
		if (!wq->rq.qid) {
			ret = -ENOMEM;
			goto free_sq_qid;
		}
	}

	if (!user) {
		wq->sq.sw_sq = kcalloc(wq->sq.size, sizeof(*wq->sq.sw_sq),
				       GFP_KERNEL);
		if (!wq->sq.sw_sq) {
			ret = -ENOMEM;
			goto free_rq_qid;//FIXME
		}

		if (need_rq) {
			wq->rq.sw_rq = kcalloc(wq->rq.size,
					       sizeof(*wq->rq.sw_rq),
					       GFP_KERNEL);
			if (!wq->rq.sw_rq) {
				ret = -ENOMEM;
				goto free_sw_sq;
			}
		}
	}

	if (need_rq) {
		/*
		 * RQT must be a power of 2 and at least 16 deep.
		 */
		wq->rq.rqt_size =
			roundup_pow_of_two(max_t(u16, wq->rq.size, 16));
		wq->rq.rqt_hwaddr = c4iw_rqtpool_alloc(rdev, wq->rq.rqt_size);
		if (!wq->rq.rqt_hwaddr) {
			ret = -ENOMEM;
			goto free_sw_rq;
		}
	}

	ret = alloc_sq(rdev, &wq->sq, user);
	if (ret)
		goto free_hwaddr;
	memset(wq->sq.queue, 0, wq->sq.memsize);
	dma_unmap_addr_set(&wq->sq, mapping, wq->sq.dma_addr);

	if (need_rq) {
		wq->rq.queue = dma_alloc_coherent(&rdev->lldi.pdev->dev,
						  wq->rq.memsize,
						  &wq->rq.dma_addr,
						  GFP_KERNEL);
		if (!wq->rq.queue) {
			ret = -ENOMEM;
			goto free_sq;
		}
		pr_debug("sq base va 0x%p pa 0x%llx rq base va 0x%p pa 0x%llx\n",
			 wq->sq.queue,
			 (unsigned long long)virt_to_phys(wq->sq.queue),
			 wq->rq.queue,
			 (unsigned long long)virt_to_phys(wq->rq.queue));
		dma_unmap_addr_set(&wq->rq, mapping, wq->rq.dma_addr);
	}

	wq->db = rdev->lldi.db_reg;

	wq->sq.bar2_va = c4iw_bar2_addrs(rdev, wq->sq.qid,
					 CXGB4_BAR2_QTYPE_EGRESS,
					 &wq->sq.bar2_qid,
					 user ? &wq->sq.bar2_pa : NULL);
	if (need_rq)
		wq->rq.bar2_va = c4iw_bar2_addrs(rdev, wq->rq.qid,
						 CXGB4_BAR2_QTYPE_EGRESS,
						 &wq->rq.bar2_qid,
						 user ? &wq->rq.bar2_pa : NULL);

	/*
	 * User mode must have bar2 access.
	 */
	if (user && (!wq->sq.bar2_pa || (need_rq && !wq->rq.bar2_pa))) {
		pr_warn("%s: sqid %u or rqid %u not in BAR2 range\n",
			pci_name(rdev->lldi.pdev), wq->sq.qid, wq->rq.qid);
		ret = -EINVAL;
		goto free_dma;
	}

	wq->rdev = rdev;
	wq->rq.msn = 1;

	/* build fw_ri_res_wr */
	wr_len = sizeof(*res_wr) + 2 * sizeof(*res);
	if (need_rq)
		wr_len += sizeof(*res);
	skb = alloc_skb(wr_len, GFP_KERNEL);
	if (!skb) {
		ret = -ENOMEM;
		goto free_dma;
	}
	set_wr_txq(skb, CPL_PRIORITY_CONTROL, 0);

	res_wr = __skb_put_zero(skb, wr_len);
	res_wr->op_nres = cpu_to_be32(
			FW_WR_OP_V(FW_RI_RES_WR) |
			FW_RI_RES_WR_NRES_V(need_rq ? 2 : 1) |
			FW_WR_COMPL_F);
	res_wr->len16_pkd = cpu_to_be32(DIV_ROUND_UP(wr_len, 16));
	res_wr->cookie = (uintptr_t)wr_waitp;
	res = res_wr->res;
	res->u.sqrq.restype = FW_RI_RES_TYPE_SQ;
	res->u.sqrq.op = FW_RI_RES_OP_WRITE;

	/*
	 * eqsize is the number of 64B entries plus the status page size.
	 */
	eqsize = wq->sq.size * T4_SQ_NUM_SLOTS +
		rdev->hw_queue.t4_eq_status_entries;

	res->u.sqrq.fetchszm_to_iqid = cpu_to_be32(
		FW_RI_RES_WR_HOSTFCMODE_V(0) |	/* no host cidx updates */
		FW_RI_RES_WR_CPRIO_V(0) |	/* don't keep in chip cache */
		FW_RI_RES_WR_PCIECHN_V(0) |	/* set by uP at ri_init time */
		(t4_sq_onchip(&wq->sq) ? FW_RI_RES_WR_ONCHIP_F : 0) |
		FW_RI_RES_WR_IQID_V(scq->cqid));
	res->u.sqrq.dcaen_to_eqsize = cpu_to_be32(
		FW_RI_RES_WR_DCAEN_V(0) |
		FW_RI_RES_WR_DCACPU_V(0) |
		FW_RI_RES_WR_FBMIN_V(2) |
		(t4_sq_onchip(&wq->sq) ? FW_RI_RES_WR_FBMAX_V(2) :
					 FW_RI_RES_WR_FBMAX_V(3)) |
		FW_RI_RES_WR_CIDXFTHRESHO_V(0) |
		FW_RI_RES_WR_CIDXFTHRESH_V(0) |
		FW_RI_RES_WR_EQSIZE_V(eqsize));
	res->u.sqrq.eqid = cpu_to_be32(wq->sq.qid);
	res->u.sqrq.eqaddr = cpu_to_be64(wq->sq.dma_addr);

	if (need_rq) {
		res++;
		res->u.sqrq.restype = FW_RI_RES_TYPE_RQ;
		res->u.sqrq.op = FW_RI_RES_OP_WRITE;

		/*
		 * eqsize is the number of 64B entries plus the status page size
		 */
		eqsize = wq->rq.size * T4_RQ_NUM_SLOTS +
			rdev->hw_queue.t4_eq_status_entries;
		res->u.sqrq.fetchszm_to_iqid =
			/* no host cidx updates */
			cpu_to_be32(FW_RI_RES_WR_HOSTFCMODE_V(0) |
			/* don't keep in chip cache */
			FW_RI_RES_WR_CPRIO_V(0) |
			/* set by uP at ri_init time */
			FW_RI_RES_WR_PCIECHN_V(0) |
			FW_RI_RES_WR_IQID_V(rcq->cqid));
		res->u.sqrq.dcaen_to_eqsize =
			cpu_to_be32(FW_RI_RES_WR_DCAEN_V(0) |
			FW_RI_RES_WR_DCACPU_V(0) |
			FW_RI_RES_WR_FBMIN_V(2) |
			FW_RI_RES_WR_FBMAX_V(3) |
			FW_RI_RES_WR_CIDXFTHRESHO_V(0) |
			FW_RI_RES_WR_CIDXFTHRESH_V(0) |
			FW_RI_RES_WR_EQSIZE_V(eqsize));
		res->u.sqrq.eqid = cpu_to_be32(wq->rq.qid);
		res->u.sqrq.eqaddr = cpu_to_be64(wq->rq.dma_addr);
	}

	c4iw_init_wr_wait(wr_waitp);
	ret = c4iw_ref_send_wait(rdev, skb, wr_waitp, 0, wq->sq.qid, __func__);
	if (ret)
		goto free_dma;

	pr_debug("sqid 0x%x rqid 0x%x kdb 0x%p sq_bar2_addr %p rq_bar2_addr %p\n",
		 wq->sq.qid, wq->rq.qid, wq->db,
		 wq->sq.bar2_va, wq->rq.bar2_va);

	return 0;
free_dma:
	if (need_rq)
		dma_free_coherent(&rdev->lldi.pdev->dev,
				  wq->rq.memsize, wq->rq.queue,
				  dma_unmap_addr(&wq->rq, mapping));
free_sq:
	dealloc_sq(rdev, &wq->sq);
free_hwaddr:
	if (need_rq)
		c4iw_rqtpool_free(rdev, wq->rq.rqt_hwaddr, wq->rq.rqt_size);
free_sw_rq:
	if (need_rq)
		kfree(wq->rq.sw_rq);
free_sw_sq:
	kfree(wq->sq.sw_sq);
free_rq_qid:
	if (need_rq)
		c4iw_put_qpid(rdev, wq->rq.qid, uctx);
free_sq_qid:
	c4iw_put_qpid(rdev, wq->sq.qid, uctx);
	return ret;
}

static int build_immd(struct t4_sq *sq, struct fw_ri_immd *immdp,
		      const struct ib_send_wr *wr, int max, u32 *plenp)
{
	u8 *dstp, *srcp;
	u32 plen = 0;
	int i;
	int rem, len;

	dstp = (u8 *)immdp->data;
	for (i = 0; i < wr->num_sge; i++) {
		if ((plen + wr->sg_list[i].length) > max)
			return -EMSGSIZE;
		srcp = (u8 *)(unsigned long)wr->sg_list[i].addr;
		plen += wr->sg_list[i].length;
		rem = wr->sg_list[i].length;
		while (rem) {
			if (dstp == (u8 *)&sq->queue[sq->size])
				dstp = (u8 *)sq->queue;
			if (rem <= (u8 *)&sq->queue[sq->size] - dstp)
				len = rem;
			else
				len = (u8 *)&sq->queue[sq->size] - dstp;
			memcpy(dstp, srcp, len);
			dstp += len;
			srcp += len;
			rem -= len;
		}
	}
	len = roundup(plen + sizeof(*immdp), 16) - (plen + sizeof(*immdp));
	if (len)
		memset(dstp, 0, len);
	immdp->op = FW_RI_DATA_IMMD;
	immdp->r1 = 0;
	immdp->r2 = 0;
	immdp->immdlen = cpu_to_be32(plen);
	*plenp = plen;
	return 0;
}

static int build_isgl(__be64 *queue_start, __be64 *queue_end,
		      struct fw_ri_isgl *isglp, struct ib_sge *sg_list,
		      int num_sge, u32 *plenp)

{
	int i;
	u32 plen = 0;
	__be64 *flitp;

	if ((__be64 *)isglp == queue_end)
		isglp = (struct fw_ri_isgl *)queue_start;

	flitp = (__be64 *)isglp->sge;

	for (i = 0; i < num_sge; i++) {
		if ((plen + sg_list[i].length) < plen)
			return -EMSGSIZE;
		plen += sg_list[i].length;
		*flitp = cpu_to_be64(((u64)sg_list[i].lkey << 32) |
				     sg_list[i].length);
		if (++flitp == queue_end)
			flitp = queue_start;
		*flitp = cpu_to_be64(sg_list[i].addr);
		if (++flitp == queue_end)
			flitp = queue_start;
	}
	*flitp = (__force __be64)0;
	isglp->op = FW_RI_DATA_ISGL;
	isglp->r1 = 0;
	isglp->nsge = cpu_to_be16(num_sge);
	isglp->r2 = 0;
	if (plenp)
		*plenp = plen;
	return 0;
}

static int build_rdma_send(struct t4_sq *sq, union t4_wr *wqe,
			   const struct ib_send_wr *wr, u8 *len16)
{
	u32 plen;
	int size;
	int ret;

	if (wr->num_sge > T4_MAX_SEND_SGE)
		return -EINVAL;
	switch (wr->opcode) {
	case IB_WR_SEND:
		if (wr->send_flags & IB_SEND_SOLICITED)
			wqe->send.sendop_pkd = cpu_to_be32(
				FW_RI_SEND_WR_SENDOP_V(FW_RI_SEND_WITH_SE));
		else
			wqe->send.sendop_pkd = cpu_to_be32(
				FW_RI_SEND_WR_SENDOP_V(FW_RI_SEND));
		wqe->send.stag_inv = 0;
		break;
	case IB_WR_SEND_WITH_INV:
		if (wr->send_flags & IB_SEND_SOLICITED)
			wqe->send.sendop_pkd = cpu_to_be32(
				FW_RI_SEND_WR_SENDOP_V(FW_RI_SEND_WITH_SE_INV));
		else
			wqe->send.sendop_pkd = cpu_to_be32(
				FW_RI_SEND_WR_SENDOP_V(FW_RI_SEND_WITH_INV));
		wqe->send.stag_inv = cpu_to_be32(wr->ex.invalidate_rkey);
		break;

	default:
		return -EINVAL;
	}
	wqe->send.r3 = 0;
	wqe->send.r4 = 0;

	plen = 0;
	if (wr->num_sge) {
		if (wr->send_flags & IB_SEND_INLINE) {
			ret = build_immd(sq, wqe->send.u.immd_src, wr,
					 T4_MAX_SEND_INLINE, &plen);
			if (ret)
				return ret;
			size = sizeof(wqe->send) + sizeof(struct fw_ri_immd) +
			       plen;
		} else {
			ret = build_isgl((__be64 *)sq->queue,
					 (__be64 *)&sq->queue[sq->size],
					 wqe->send.u.isgl_src,
					 wr->sg_list, wr->num_sge, &plen);
			if (ret)
				return ret;
			size = sizeof(wqe->send) + sizeof(struct fw_ri_isgl) +
			       wr->num_sge * sizeof(struct fw_ri_sge);
		}
	} else {
		wqe->send.u.immd_src[0].op = FW_RI_DATA_IMMD;
		wqe->send.u.immd_src[0].r1 = 0;
		wqe->send.u.immd_src[0].r2 = 0;
		wqe->send.u.immd_src[0].immdlen = 0;
		size = sizeof(wqe->send) + sizeof(struct fw_ri_immd);
		plen = 0;
	}
	*len16 = DIV_ROUND_UP(size, 16);
	wqe->send.plen = cpu_to_be32(plen);
	return 0;
}

static int build_rdma_write(struct t4_sq *sq, union t4_wr *wqe,
			    const struct ib_send_wr *wr, u8 *len16)
{
	u32 plen;
	int size;
	int ret;

	if (wr->num_sge > T4_MAX_SEND_SGE)
		return -EINVAL;

	/*
	 * iWARP protocol supports 64 bit immediate data but rdma api
	 * limits it to 32bit.
	 */
	if (wr->opcode == IB_WR_RDMA_WRITE_WITH_IMM)
		wqe->write.iw_imm_data.ib_imm_data.imm_data32 = wr->ex.imm_data;
	else
		wqe->write.iw_imm_data.ib_imm_data.imm_data32 = 0;
	wqe->write.stag_sink = cpu_to_be32(rdma_wr(wr)->rkey);
	wqe->write.to_sink = cpu_to_be64(rdma_wr(wr)->remote_addr);
	if (wr->num_sge) {
		if (wr->send_flags & IB_SEND_INLINE) {
			ret = build_immd(sq, wqe->write.u.immd_src, wr,
					 T4_MAX_WRITE_INLINE, &plen);
			if (ret)
				return ret;
			size = sizeof(wqe->write) + sizeof(struct fw_ri_immd) +
			       plen;
		} else {
			ret = build_isgl((__be64 *)sq->queue,
					 (__be64 *)&sq->queue[sq->size],
					 wqe->write.u.isgl_src,
					 wr->sg_list, wr->num_sge, &plen);
			if (ret)
				return ret;
			size = sizeof(wqe->write) + sizeof(struct fw_ri_isgl) +
			       wr->num_sge * sizeof(struct fw_ri_sge);
		}
	} else {
		wqe->write.u.immd_src[0].op = FW_RI_DATA_IMMD;
		wqe->write.u.immd_src[0].r1 = 0;
		wqe->write.u.immd_src[0].r2 = 0;
		wqe->write.u.immd_src[0].immdlen = 0;
		size = sizeof(wqe->write) + sizeof(struct fw_ri_immd);
		plen = 0;
	}
	*len16 = DIV_ROUND_UP(size, 16);
	wqe->write.plen = cpu_to_be32(plen);
	return 0;
}

static void build_immd_cmpl(struct t4_sq *sq, struct fw_ri_immd_cmpl *immdp,
			    struct ib_send_wr *wr)
{
	memcpy((u8 *)immdp->data, (u8 *)(uintptr_t)wr->sg_list->addr, 16);
	memset(immdp->r1, 0, 6);
	immdp->op = FW_RI_DATA_IMMD;
	immdp->immdlen = 16;
}

static void build_rdma_write_cmpl(struct t4_sq *sq,
				  struct fw_ri_rdma_write_cmpl_wr *wcwr,
				  const struct ib_send_wr *wr, u8 *len16)
{
	u32 plen;
	int size;

	/*
	 * This code assumes the struct fields preceding the write isgl
	 * fit in one 64B WR slot.  This is because the WQE is built
	 * directly in the dma queue, and wrapping is only handled
	 * by the code buildling sgls.  IE the "fixed part" of the wr
	 * structs must all fit in 64B.  The WQE build code should probably be
	 * redesigned to avoid this restriction, but for now just add
	 * the BUILD_BUG_ON() to catch if this WQE struct gets too big.
	 */
	BUILD_BUG_ON(offsetof(struct fw_ri_rdma_write_cmpl_wr, u) > 64);

	wcwr->stag_sink = cpu_to_be32(rdma_wr(wr)->rkey);
	wcwr->to_sink = cpu_to_be64(rdma_wr(wr)->remote_addr);
	if (wr->next->opcode == IB_WR_SEND)
		wcwr->stag_inv = 0;
	else
		wcwr->stag_inv = cpu_to_be32(wr->next->ex.invalidate_rkey);
	wcwr->r2 = 0;
	wcwr->r3 = 0;

	/* SEND_INV SGL */
	if (wr->next->send_flags & IB_SEND_INLINE)
		build_immd_cmpl(sq, &wcwr->u_cmpl.immd_src, wr->next);
	else
		build_isgl((__be64 *)sq->queue, (__be64 *)&sq->queue[sq->size],
			   &wcwr->u_cmpl.isgl_src, wr->next->sg_list, 1, NULL);

	/* WRITE SGL */
	build_isgl((__be64 *)sq->queue, (__be64 *)&sq->queue[sq->size],
		   wcwr->u.isgl_src, wr->sg_list, wr->num_sge, &plen);

	size = sizeof(*wcwr) + sizeof(struct fw_ri_isgl) +
		wr->num_sge * sizeof(struct fw_ri_sge);
	wcwr->plen = cpu_to_be32(plen);
	*len16 = DIV_ROUND_UP(size, 16);
}

static int build_rdma_read(union t4_wr *wqe, const struct ib_send_wr *wr,
			   u8 *len16)
{
	if (wr->num_sge > 1)
		return -EINVAL;
	if (wr->num_sge && wr->sg_list[0].length) {
		wqe->read.stag_src = cpu_to_be32(rdma_wr(wr)->rkey);
		wqe->read.to_src_hi = cpu_to_be32((u32)(rdma_wr(wr)->remote_addr
							>> 32));
		wqe->read.to_src_lo = cpu_to_be32((u32)rdma_wr(wr)->remote_addr);
		wqe->read.stag_sink = cpu_to_be32(wr->sg_list[0].lkey);
		wqe->read.plen = cpu_to_be32(wr->sg_list[0].length);
		wqe->read.to_sink_hi = cpu_to_be32((u32)(wr->sg_list[0].addr
							 >> 32));
		wqe->read.to_sink_lo = cpu_to_be32((u32)(wr->sg_list[0].addr));
	} else {
		wqe->read.stag_src = cpu_to_be32(2);
		wqe->read.to_src_hi = 0;
		wqe->read.to_src_lo = 0;
		wqe->read.stag_sink = cpu_to_be32(2);
		wqe->read.plen = 0;
		wqe->read.to_sink_hi = 0;
		wqe->read.to_sink_lo = 0;
	}
	wqe->read.r2 = 0;
	wqe->read.r5 = 0;
	*len16 = DIV_ROUND_UP(sizeof(wqe->read), 16);
	return 0;
}

static void post_write_cmpl(struct c4iw_qp *qhp, const struct ib_send_wr *wr)
{
	bool send_signaled = (wr->next->send_flags & IB_SEND_SIGNALED) ||
			     qhp->sq_sig_all;
	bool write_signaled = (wr->send_flags & IB_SEND_SIGNALED) ||
			      qhp->sq_sig_all;
	struct t4_swsqe *swsqe;
	union t4_wr *wqe;
	u16 write_wrid;
	u8 len16;
	u16 idx;

	/*
	 * The sw_sq entries still look like a WRITE and a SEND and consume
	 * 2 slots. The FW WR, however, will be a single uber-WR.
	 */
	wqe = (union t4_wr *)((u8 *)qhp->wq.sq.queue +
	       qhp->wq.sq.wq_pidx * T4_EQ_ENTRY_SIZE);
	build_rdma_write_cmpl(&qhp->wq.sq, &wqe->write_cmpl, wr, &len16);

	/* WRITE swsqe */
	swsqe = &qhp->wq.sq.sw_sq[qhp->wq.sq.pidx];
	swsqe->opcode = FW_RI_RDMA_WRITE;
	swsqe->idx = qhp->wq.sq.pidx;
	swsqe->complete = 0;
	swsqe->signaled = write_signaled;
	swsqe->flushed = 0;
	swsqe->wr_id = wr->wr_id;
	if (c4iw_wr_log) {
		swsqe->sge_ts =
			cxgb4_read_sge_timestamp(qhp->rhp->rdev.lldi.ports[0]);
		swsqe->host_time = ktime_get();
	}

	write_wrid = qhp->wq.sq.pidx;

	/* just bump the sw_sq */
	qhp->wq.sq.in_use++;
	if (++qhp->wq.sq.pidx == qhp->wq.sq.size)
		qhp->wq.sq.pidx = 0;

	/* SEND_WITH_INV swsqe */
	swsqe = &qhp->wq.sq.sw_sq[qhp->wq.sq.pidx];
	if (wr->next->opcode == IB_WR_SEND)
		swsqe->opcode = FW_RI_SEND;
	else
		swsqe->opcode = FW_RI_SEND_WITH_INV;
	swsqe->idx = qhp->wq.sq.pidx;
	swsqe->complete = 0;
	swsqe->signaled = send_signaled;
	swsqe->flushed = 0;
	swsqe->wr_id = wr->next->wr_id;
	if (c4iw_wr_log) {
		swsqe->sge_ts =
			cxgb4_read_sge_timestamp(qhp->rhp->rdev.lldi.ports[0]);
		swsqe->host_time = ktime_get();
	}

	wqe->write_cmpl.flags_send = send_signaled ? FW_RI_COMPLETION_FLAG : 0;
	wqe->write_cmpl.wrid_send = qhp->wq.sq.pidx;

	init_wr_hdr(wqe, write_wrid, FW_RI_RDMA_WRITE_CMPL_WR,
		    write_signaled ? FW_RI_COMPLETION_FLAG : 0, len16);
	t4_sq_produce(&qhp->wq, len16);
	idx = DIV_ROUND_UP(len16 * 16, T4_EQ_ENTRY_SIZE);

	t4_ring_sq_db(&qhp->wq, idx, wqe);
}

static int build_rdma_recv(struct c4iw_qp *qhp, union t4_recv_wr *wqe,
			   const struct ib_recv_wr *wr, u8 *len16)
{
	int ret;

	ret = build_isgl((__be64 *)qhp->wq.rq.queue,
			 (__be64 *)&qhp->wq.rq.queue[qhp->wq.rq.size],
			 &wqe->recv.isgl, wr->sg_list, wr->num_sge, NULL);
	if (ret)
		return ret;
	*len16 = DIV_ROUND_UP(
		sizeof(wqe->recv) + wr->num_sge * sizeof(struct fw_ri_sge), 16);
	return 0;
}

static int build_srq_recv(union t4_recv_wr *wqe, const struct ib_recv_wr *wr,
			  u8 *len16)
{
	int ret;

	ret = build_isgl((__be64 *)wqe, (__be64 *)(wqe + 1),
			 &wqe->recv.isgl, wr->sg_list, wr->num_sge, NULL);
	if (ret)
		return ret;
	*len16 = DIV_ROUND_UP(sizeof(wqe->recv) +
			      wr->num_sge * sizeof(struct fw_ri_sge), 16);
	return 0;
}

static void build_tpte_memreg(struct fw_ri_fr_nsmr_tpte_wr *fr,
			      const struct ib_reg_wr *wr, struct c4iw_mr *mhp,
			      u8 *len16)
{
	__be64 *p = (__be64 *)fr->pbl;

	fr->r2 = cpu_to_be32(0);
	fr->stag = cpu_to_be32(mhp->ibmr.rkey);

	fr->tpte.valid_to_pdid = cpu_to_be32(FW_RI_TPTE_VALID_F |
		FW_RI_TPTE_STAGKEY_V((mhp->ibmr.rkey & FW_RI_TPTE_STAGKEY_M)) |
		FW_RI_TPTE_STAGSTATE_V(1) |
		FW_RI_TPTE_STAGTYPE_V(FW_RI_STAG_NSMR) |
		FW_RI_TPTE_PDID_V(mhp->attr.pdid));
	fr->tpte.locread_to_qpid = cpu_to_be32(
		FW_RI_TPTE_PERM_V(c4iw_ib_to_tpt_access(wr->access)) |
		FW_RI_TPTE_ADDRTYPE_V(FW_RI_VA_BASED_TO) |
		FW_RI_TPTE_PS_V(ilog2(wr->mr->page_size) - 12));
	fr->tpte.nosnoop_pbladdr = cpu_to_be32(FW_RI_TPTE_PBLADDR_V(
		PBL_OFF(&mhp->rhp->rdev, mhp->attr.pbl_addr)>>3));
	fr->tpte.dca_mwbcnt_pstag = cpu_to_be32(0);
	fr->tpte.len_hi = cpu_to_be32(0);
	fr->tpte.len_lo = cpu_to_be32(mhp->ibmr.length);
	fr->tpte.va_hi = cpu_to_be32(mhp->ibmr.iova >> 32);
	fr->tpte.va_lo_fbo = cpu_to_be32(mhp->ibmr.iova & 0xffffffff);

	p[0] = cpu_to_be64((u64)mhp->mpl[0]);
	p[1] = cpu_to_be64((u64)mhp->mpl[1]);

	*len16 = DIV_ROUND_UP(sizeof(*fr), 16);
}

static int build_memreg(struct t4_sq *sq, union t4_wr *wqe,
			const struct ib_reg_wr *wr, struct c4iw_mr *mhp,
			u8 *len16, bool dsgl_supported)
{
	struct fw_ri_immd *imdp;
	__be64 *p;
	int i;
	int pbllen = roundup(mhp->mpl_len * sizeof(u64), 32);
	int rem;

	if (mhp->mpl_len > t4_max_fr_depth(dsgl_supported && use_dsgl))
		return -EINVAL;

	wqe->fr.qpbinde_to_dcacpu = 0;
	wqe->fr.pgsz_shift = ilog2(wr->mr->page_size) - 12;
	wqe->fr.addr_type = FW_RI_VA_BASED_TO;
	wqe->fr.mem_perms = c4iw_ib_to_tpt_access(wr->access);
	wqe->fr.len_hi = 0;
	wqe->fr.len_lo = cpu_to_be32(mhp->ibmr.length);
	wqe->fr.stag = cpu_to_be32(wr->key);
	wqe->fr.va_hi = cpu_to_be32(mhp->ibmr.iova >> 32);
	wqe->fr.va_lo_fbo = cpu_to_be32(mhp->ibmr.iova &
					0xffffffff);

	if (dsgl_supported && use_dsgl && (pbllen > max_fr_immd)) {
		struct fw_ri_dsgl *sglp;

		for (i = 0; i < mhp->mpl_len; i++)
			mhp->mpl[i] = (__force u64)cpu_to_be64((u64)mhp->mpl[i]);

		sglp = (struct fw_ri_dsgl *)(&wqe->fr + 1);
		sglp->op = FW_RI_DATA_DSGL;
		sglp->r1 = 0;
		sglp->nsge = cpu_to_be16(1);
		sglp->addr0 = cpu_to_be64(mhp->mpl_addr);
		sglp->len0 = cpu_to_be32(pbllen);

		*len16 = DIV_ROUND_UP(sizeof(wqe->fr) + sizeof(*sglp), 16);
	} else {
		imdp = (struct fw_ri_immd *)(&wqe->fr + 1);
		imdp->op = FW_RI_DATA_IMMD;
		imdp->r1 = 0;
		imdp->r2 = 0;
		imdp->immdlen = cpu_to_be32(pbllen);
		p = (__be64 *)(imdp + 1);
		rem = pbllen;
		for (i = 0; i < mhp->mpl_len; i++) {
			*p = cpu_to_be64((u64)mhp->mpl[i]);
			rem -= sizeof(*p);
			if (++p == (__be64 *)&sq->queue[sq->size])
				p = (__be64 *)sq->queue;
		}
		while (rem) {
			*p = 0;
			rem -= sizeof(*p);
			if (++p == (__be64 *)&sq->queue[sq->size])
				p = (__be64 *)sq->queue;
		}
		*len16 = DIV_ROUND_UP(sizeof(wqe->fr) + sizeof(*imdp)
				      + pbllen, 16);
	}
	return 0;
}

static int build_inv_stag(union t4_wr *wqe, const struct ib_send_wr *wr,
			  u8 *len16)
{
	wqe->inv.stag_inv = cpu_to_be32(wr->ex.invalidate_rkey);
	wqe->inv.r2 = 0;
	*len16 = DIV_ROUND_UP(sizeof(wqe->inv), 16);
	return 0;
}

void c4iw_qp_add_ref(struct ib_qp *qp)
{
	pr_debug("ib_qp %p\n", qp);
	refcount_inc(&to_c4iw_qp(qp)->qp_refcnt);
}

void c4iw_qp_rem_ref(struct ib_qp *qp)
{
	pr_debug("ib_qp %p\n", qp);
	if (refcount_dec_and_test(&to_c4iw_qp(qp)->qp_refcnt))
		complete(&to_c4iw_qp(qp)->qp_rel_comp);
}

static void add_to_fc_list(struct list_head *head, struct list_head *entry)
{
	if (list_empty(entry))
		list_add_tail(entry, head);
}

static int ring_kernel_sq_db(struct c4iw_qp *qhp, u16 inc)
{
	unsigned long flags;

	xa_lock_irqsave(&qhp->rhp->qps, flags);
	spin_lock(&qhp->lock);
	if (qhp->rhp->db_state == NORMAL)
		t4_ring_sq_db(&qhp->wq, inc, NULL);
	else {
		add_to_fc_list(&qhp->rhp->db_fc_list, &qhp->db_fc_entry);
		qhp->wq.sq.wq_pidx_inc += inc;
	}
	spin_unlock(&qhp->lock);
	xa_unlock_irqrestore(&qhp->rhp->qps, flags);
	return 0;
}

static int ring_kernel_rq_db(struct c4iw_qp *qhp, u16 inc)
{
	unsigned long flags;

	xa_lock_irqsave(&qhp->rhp->qps, flags);
	spin_lock(&qhp->lock);
	if (qhp->rhp->db_state == NORMAL)
		t4_ring_rq_db(&qhp->wq, inc, NULL);
	else {
		add_to_fc_list(&qhp->rhp->db_fc_list, &qhp->db_fc_entry);
		qhp->wq.rq.wq_pidx_inc += inc;
	}
	spin_unlock(&qhp->lock);
	xa_unlock_irqrestore(&qhp->rhp->qps, flags);
	return 0;
}

static int ib_to_fw_opcode(int ib_opcode)
{
	int opcode;

	switch (ib_opcode) {
	case IB_WR_SEND_WITH_INV:
		opcode = FW_RI_SEND_WITH_INV;
		break;
	case IB_WR_SEND:
		opcode = FW_RI_SEND;
		break;
	case IB_WR_RDMA_WRITE:
		opcode = FW_RI_RDMA_WRITE;
		break;
	case IB_WR_RDMA_WRITE_WITH_IMM:
		opcode = FW_RI_WRITE_IMMEDIATE;
		break;
	case IB_WR_RDMA_READ:
	case IB_WR_RDMA_READ_WITH_INV:
		opcode = FW_RI_READ_REQ;
		break;
	case IB_WR_REG_MR:
		opcode = FW_RI_FAST_REGISTER;
		break;
	case IB_WR_LOCAL_INV:
		opcode = FW_RI_LOCAL_INV;
		break;
	default:
		opcode = -EINVAL;
	}
	return opcode;
}

static int complete_sq_drain_wr(struct c4iw_qp *qhp,
				const struct ib_send_wr *wr)
{
	struct t4_cqe cqe = {};
	struct c4iw_cq *schp;
	unsigned long flag;
	struct t4_cq *cq;
	int opcode;

	schp = to_c4iw_cq(qhp->ibqp.send_cq);
	cq = &schp->cq;

	opcode = ib_to_fw_opcode(wr->opcode);
	if (opcode < 0)
		return opcode;

	cqe.u.drain_cookie = wr->wr_id;
	cqe.header = cpu_to_be32(CQE_STATUS_V(T4_ERR_SWFLUSH) |
				 CQE_OPCODE_V(opcode) |
				 CQE_TYPE_V(1) |
				 CQE_SWCQE_V(1) |
				 CQE_DRAIN_V(1) |
				 CQE_QPID_V(qhp->wq.sq.qid));

	spin_lock_irqsave(&schp->lock, flag);
	cqe.bits_type_ts = cpu_to_be64(CQE_GENBIT_V((u64)cq->gen));
	cq->sw_queue[cq->sw_pidx] = cqe;
	t4_swcq_produce(cq);
	spin_unlock_irqrestore(&schp->lock, flag);

	if (t4_clear_cq_armed(&schp->cq)) {
		spin_lock_irqsave(&schp->comp_handler_lock, flag);
		(*schp->ibcq.comp_handler)(&schp->ibcq,
					   schp->ibcq.cq_context);
		spin_unlock_irqrestore(&schp->comp_handler_lock, flag);
	}
	return 0;
}

static int complete_sq_drain_wrs(struct c4iw_qp *qhp,
				 const struct ib_send_wr *wr,
				 const struct ib_send_wr **bad_wr)
{
	int ret = 0;

	while (wr) {
		ret = complete_sq_drain_wr(qhp, wr);
		if (ret) {
			*bad_wr = wr;
			break;
		}
		wr = wr->next;
	}
	return ret;
}

static void complete_rq_drain_wr(struct c4iw_qp *qhp,
				 const struct ib_recv_wr *wr)
{
	struct t4_cqe cqe = {};
	struct c4iw_cq *rchp;
	unsigned long flag;
	struct t4_cq *cq;

	rchp = to_c4iw_cq(qhp->ibqp.recv_cq);
	cq = &rchp->cq;

	cqe.u.drain_cookie = wr->wr_id;
	cqe.header = cpu_to_be32(CQE_STATUS_V(T4_ERR_SWFLUSH) |
				 CQE_OPCODE_V(FW_RI_SEND) |
				 CQE_TYPE_V(0) |
				 CQE_SWCQE_V(1) |
				 CQE_DRAIN_V(1) |
				 CQE_QPID_V(qhp->wq.sq.qid));

	spin_lock_irqsave(&rchp->lock, flag);
	cqe.bits_type_ts = cpu_to_be64(CQE_GENBIT_V((u64)cq->gen));
	cq->sw_queue[cq->sw_pidx] = cqe;
	t4_swcq_produce(cq);
	spin_unlock_irqrestore(&rchp->lock, flag);

	if (t4_clear_cq_armed(&rchp->cq)) {
		spin_lock_irqsave(&rchp->comp_handler_lock, flag);
		(*rchp->ibcq.comp_handler)(&rchp->ibcq,
					   rchp->ibcq.cq_context);
		spin_unlock_irqrestore(&rchp->comp_handler_lock, flag);
	}
}

static void complete_rq_drain_wrs(struct c4iw_qp *qhp,
				  const struct ib_recv_wr *wr)
{
	while (wr) {
		complete_rq_drain_wr(qhp, wr);
		wr = wr->next;
	}
}

int c4iw_post_send(struct ib_qp *ibqp, const struct ib_send_wr *wr,
		   const struct ib_send_wr **bad_wr)
{
	int err = 0;
	u8 len16 = 0;
	enum fw_wr_opcodes fw_opcode = 0;
	enum fw_ri_wr_flags fw_flags;
	struct c4iw_qp *qhp;
	struct c4iw_dev *rhp;
	union t4_wr *wqe = NULL;
	u32 num_wrs;
	struct t4_swsqe *swsqe;
	unsigned long flag;
	u16 idx = 0;

	qhp = to_c4iw_qp(ibqp);
	rhp = qhp->rhp;
	spin_lock_irqsave(&qhp->lock, flag);

	/*
	 * If the qp has been flushed, then just insert a special
	 * drain cqe.
	 */
	if (qhp->wq.flushed) {
		spin_unlock_irqrestore(&qhp->lock, flag);
		err = complete_sq_drain_wrs(qhp, wr, bad_wr);
		return err;
	}
	num_wrs = t4_sq_avail(&qhp->wq);
	if (num_wrs == 0) {
		spin_unlock_irqrestore(&qhp->lock, flag);
		*bad_wr = wr;
		return -ENOMEM;
	}

	/*
	 * Fastpath for NVMe-oF target WRITE + SEND_WITH_INV wr chain which is
	 * the response for small NVMEe-oF READ requests.  If the chain is
	 * exactly a WRITE->SEND_WITH_INV or a WRITE->SEND and the sgl depths
	 * and lengths meet the requirements of the fw_ri_write_cmpl_wr work
	 * request, then build and post the write_cmpl WR. If any of the tests
	 * below are not true, then we continue on with the tradtional WRITE
	 * and SEND WRs.
	 */
	if (qhp->rhp->rdev.lldi.write_cmpl_support &&
	    CHELSIO_CHIP_VERSION(qhp->rhp->rdev.lldi.adapter_type) >=
	    CHELSIO_T5 &&
	    wr && wr->next && !wr->next->next &&
	    wr->opcode == IB_WR_RDMA_WRITE &&
	    wr->sg_list[0].length && wr->num_sge <= T4_WRITE_CMPL_MAX_SGL &&
	    (wr->next->opcode == IB_WR_SEND ||
	    wr->next->opcode == IB_WR_SEND_WITH_INV) &&
	    wr->next->sg_list[0].length == T4_WRITE_CMPL_MAX_CQE &&
	    wr->next->num_sge == 1 && num_wrs >= 2) {
		post_write_cmpl(qhp, wr);
		spin_unlock_irqrestore(&qhp->lock, flag);
		return 0;
	}

	while (wr) {
		if (num_wrs == 0) {
			err = -ENOMEM;
			*bad_wr = wr;
			break;
		}
		wqe = (union t4_wr *)((u8 *)qhp->wq.sq.queue +
		      qhp->wq.sq.wq_pidx * T4_EQ_ENTRY_SIZE);

		fw_flags = 0;
		if (wr->send_flags & IB_SEND_SOLICITED)
			fw_flags |= FW_RI_SOLICITED_EVENT_FLAG;
		if (wr->send_flags & IB_SEND_SIGNALED || qhp->sq_sig_all)
			fw_flags |= FW_RI_COMPLETION_FLAG;
		swsqe = &qhp->wq.sq.sw_sq[qhp->wq.sq.pidx];
		switch (wr->opcode) {
		case IB_WR_SEND_WITH_INV:
		case IB_WR_SEND:
			if (wr->send_flags & IB_SEND_FENCE)
				fw_flags |= FW_RI_READ_FENCE_FLAG;
			fw_opcode = FW_RI_SEND_WR;
			if (wr->opcode == IB_WR_SEND)
				swsqe->opcode = FW_RI_SEND;
			else
				swsqe->opcode = FW_RI_SEND_WITH_INV;
			err = build_rdma_send(&qhp->wq.sq, wqe, wr, &len16);
			break;
		case IB_WR_RDMA_WRITE_WITH_IMM:
			if (unlikely(!rhp->rdev.lldi.write_w_imm_support)) {
				err = -EINVAL;
				break;
			}
			fw_flags |= FW_RI_RDMA_WRITE_WITH_IMMEDIATE;
			fallthrough;
		case IB_WR_RDMA_WRITE:
			fw_opcode = FW_RI_RDMA_WRITE_WR;
			swsqe->opcode = FW_RI_RDMA_WRITE;
			err = build_rdma_write(&qhp->wq.sq, wqe, wr, &len16);
			break;
		case IB_WR_RDMA_READ:
		case IB_WR_RDMA_READ_WITH_INV:
			fw_opcode = FW_RI_RDMA_READ_WR;
			swsqe->opcode = FW_RI_READ_REQ;
			if (wr->opcode == IB_WR_RDMA_READ_WITH_INV) {
				c4iw_invalidate_mr(rhp, wr->sg_list[0].lkey);
				fw_flags = FW_RI_RDMA_READ_INVALIDATE;
			} else {
				fw_flags = 0;
			}
			err = build_rdma_read(wqe, wr, &len16);
			if (err)
				break;
			swsqe->read_len = wr->sg_list[0].length;
			if (!qhp->wq.sq.oldest_read)
				qhp->wq.sq.oldest_read = swsqe;
			break;
		case IB_WR_REG_MR: {
			struct c4iw_mr *mhp = to_c4iw_mr(reg_wr(wr)->mr);

			swsqe->opcode = FW_RI_FAST_REGISTER;
			if (rhp->rdev.lldi.fr_nsmr_tpte_wr_support &&
			    !mhp->attr.state && mhp->mpl_len <= 2) {
				fw_opcode = FW_RI_FR_NSMR_TPTE_WR;
				build_tpte_memreg(&wqe->fr_tpte, reg_wr(wr),
						  mhp, &len16);
			} else {
				fw_opcode = FW_RI_FR_NSMR_WR;
				err = build_memreg(&qhp->wq.sq, wqe, reg_wr(wr),
				       mhp, &len16,
				       rhp->rdev.lldi.ulptx_memwrite_dsgl);
				if (err)
					break;
			}
			mhp->attr.state = 1;
			break;
		}
		case IB_WR_LOCAL_INV:
			if (wr->send_flags & IB_SEND_FENCE)
				fw_flags |= FW_RI_LOCAL_FENCE_FLAG;
			fw_opcode = FW_RI_INV_LSTAG_WR;
			swsqe->opcode = FW_RI_LOCAL_INV;
			err = build_inv_stag(wqe, wr, &len16);
			c4iw_invalidate_mr(rhp, wr->ex.invalidate_rkey);
			break;
		default:
			pr_warn("%s post of type=%d TBD!\n", __func__,
				wr->opcode);
			err = -EINVAL;
		}
		if (err) {
			*bad_wr = wr;
			break;
		}
		swsqe->idx = qhp->wq.sq.pidx;
		swsqe->complete = 0;
		swsqe->signaled = (wr->send_flags & IB_SEND_SIGNALED) ||
				  qhp->sq_sig_all;
		swsqe->flushed = 0;
		swsqe->wr_id = wr->wr_id;
		if (c4iw_wr_log) {
			swsqe->sge_ts = cxgb4_read_sge_timestamp(
					rhp->rdev.lldi.ports[0]);
			swsqe->host_time = ktime_get();
		}

		init_wr_hdr(wqe, qhp->wq.sq.pidx, fw_opcode, fw_flags, len16);

		pr_debug("cookie 0x%llx pidx 0x%x opcode 0x%x read_len %u\n",
			 (unsigned long long)wr->wr_id, qhp->wq.sq.pidx,
			 swsqe->opcode, swsqe->read_len);
		wr = wr->next;
		num_wrs--;
		t4_sq_produce(&qhp->wq, len16);
		idx += DIV_ROUND_UP(len16*16, T4_EQ_ENTRY_SIZE);
	}
	if (!rhp->rdev.status_page->db_off) {
		t4_ring_sq_db(&qhp->wq, idx, wqe);
		spin_unlock_irqrestore(&qhp->lock, flag);
	} else {
		spin_unlock_irqrestore(&qhp->lock, flag);
		ring_kernel_sq_db(qhp, idx);
	}
	return err;
}

int c4iw_post_receive(struct ib_qp *ibqp, const struct ib_recv_wr *wr,
		      const struct ib_recv_wr **bad_wr)
{
	int err = 0;
	struct c4iw_qp *qhp;
	union t4_recv_wr *wqe = NULL;
	u32 num_wrs;
	u8 len16 = 0;
	unsigned long flag;
	u16 idx = 0;

	qhp = to_c4iw_qp(ibqp);
	spin_lock_irqsave(&qhp->lock, flag);

	/*
	 * If the qp has been flushed, then just insert a special
	 * drain cqe.
	 */
	if (qhp->wq.flushed) {
		spin_unlock_irqrestore(&qhp->lock, flag);
		complete_rq_drain_wrs(qhp, wr);
		return err;
	}
	num_wrs = t4_rq_avail(&qhp->wq);
	if (num_wrs == 0) {
		spin_unlock_irqrestore(&qhp->lock, flag);
		*bad_wr = wr;
		return -ENOMEM;
	}
	while (wr) {
		if (wr->num_sge > T4_MAX_RECV_SGE) {
			err = -EINVAL;
			*bad_wr = wr;
			break;
		}
		wqe = (union t4_recv_wr *)((u8 *)qhp->wq.rq.queue +
					   qhp->wq.rq.wq_pidx *
					   T4_EQ_ENTRY_SIZE);
		if (num_wrs)
			err = build_rdma_recv(qhp, wqe, wr, &len16);
		else
			err = -ENOMEM;
		if (err) {
			*bad_wr = wr;
			break;
		}

		qhp->wq.rq.sw_rq[qhp->wq.rq.pidx].wr_id = wr->wr_id;
		if (c4iw_wr_log) {
			qhp->wq.rq.sw_rq[qhp->wq.rq.pidx].sge_ts =
				cxgb4_read_sge_timestamp(
						qhp->rhp->rdev.lldi.ports[0]);
			qhp->wq.rq.sw_rq[qhp->wq.rq.pidx].host_time =
				ktime_get();
		}

		wqe->recv.opcode = FW_RI_RECV_WR;
		wqe->recv.r1 = 0;
		wqe->recv.wrid = qhp->wq.rq.pidx;
		wqe->recv.r2[0] = 0;
		wqe->recv.r2[1] = 0;
		wqe->recv.r2[2] = 0;
		wqe->recv.len16 = len16;
		pr_debug("cookie 0x%llx pidx %u\n",
			 (unsigned long long)wr->wr_id, qhp->wq.rq.pidx);
		t4_rq_produce(&qhp->wq, len16);
		idx += DIV_ROUND_UP(len16*16, T4_EQ_ENTRY_SIZE);
		wr = wr->next;
		num_wrs--;
	}
	if (!qhp->rhp->rdev.status_page->db_off) {
		t4_ring_rq_db(&qhp->wq, idx, wqe);
		spin_unlock_irqrestore(&qhp->lock, flag);
	} else {
		spin_unlock_irqrestore(&qhp->lock, flag);
		ring_kernel_rq_db(qhp, idx);
	}
	return err;
}

static void defer_srq_wr(struct t4_srq *srq, union t4_recv_wr *wqe,
			 u64 wr_id, u8 len16)
{
	struct t4_srq_pending_wr *pwr = &srq->pending_wrs[srq->pending_pidx];

	pr_debug("%s cidx %u pidx %u wq_pidx %u in_use %u ooo_count %u wr_id 0x%llx pending_cidx %u pending_pidx %u pending_in_use %u\n",
		 __func__, srq->cidx, srq->pidx, srq->wq_pidx,
		 srq->in_use, srq->ooo_count,
		 (unsigned long long)wr_id, srq->pending_cidx,
		 srq->pending_pidx, srq->pending_in_use);
	pwr->wr_id = wr_id;
	pwr->len16 = len16;
	memcpy(&pwr->wqe, wqe, len16 * 16);
	t4_srq_produce_pending_wr(srq);
}

int c4iw_post_srq_recv(struct ib_srq *ibsrq, const struct ib_recv_wr *wr,
		       const struct ib_recv_wr **bad_wr)
{
	union t4_recv_wr *wqe, lwqe;
	struct c4iw_srq *srq;
	unsigned long flag;
	u8 len16 = 0;
	u16 idx = 0;
	int err = 0;
	u32 num_wrs;

	srq = to_c4iw_srq(ibsrq);
	spin_lock_irqsave(&srq->lock, flag);
	num_wrs = t4_srq_avail(&srq->wq);
	if (num_wrs == 0) {
		spin_unlock_irqrestore(&srq->lock, flag);
		return -ENOMEM;
	}
	while (wr) {
		if (wr->num_sge > T4_MAX_RECV_SGE) {
			err = -EINVAL;
			*bad_wr = wr;
			break;
		}
		wqe = &lwqe;
		if (num_wrs)
			err = build_srq_recv(wqe, wr, &len16);
		else
			err = -ENOMEM;
		if (err) {
			*bad_wr = wr;
			break;
		}

		wqe->recv.opcode = FW_RI_RECV_WR;
		wqe->recv.r1 = 0;
		wqe->recv.wrid = srq->wq.pidx;
		wqe->recv.r2[0] = 0;
		wqe->recv.r2[1] = 0;
		wqe->recv.r2[2] = 0;
		wqe->recv.len16 = len16;

		if (srq->wq.ooo_count ||
		    srq->wq.pending_in_use ||
		    srq->wq.sw_rq[srq->wq.pidx].valid) {
			defer_srq_wr(&srq->wq, wqe, wr->wr_id, len16);
		} else {
			srq->wq.sw_rq[srq->wq.pidx].wr_id = wr->wr_id;
			srq->wq.sw_rq[srq->wq.pidx].valid = 1;
			c4iw_copy_wr_to_srq(&srq->wq, wqe, len16);
			pr_debug("%s cidx %u pidx %u wq_pidx %u in_use %u wr_id 0x%llx\n",
				 __func__, srq->wq.cidx,
				 srq->wq.pidx, srq->wq.wq_pidx,
				 srq->wq.in_use,
				 (unsigned long long)wr->wr_id);
			t4_srq_produce(&srq->wq, len16);
			idx += DIV_ROUND_UP(len16 * 16, T4_EQ_ENTRY_SIZE);
		}
		wr = wr->next;
		num_wrs--;
	}
	if (idx)
		t4_ring_srq_db(&srq->wq, idx, len16, wqe);
	spin_unlock_irqrestore(&srq->lock, flag);
	return err;
}

static inline void build_term_codes(struct t4_cqe *err_cqe, u8 *layer_type,
				    u8 *ecode)
{
	int status;
	int tagged;
	int opcode;
	int rqtype;
	int send_inv;

	if (!err_cqe) {
		*layer_type = LAYER_RDMAP|DDP_LOCAL_CATA;
		*ecode = 0;
		return;
	}

	status = CQE_STATUS(err_cqe);
	opcode = CQE_OPCODE(err_cqe);
	rqtype = RQ_TYPE(err_cqe);
	send_inv = (opcode == FW_RI_SEND_WITH_INV) ||
		   (opcode == FW_RI_SEND_WITH_SE_INV);
	tagged = (opcode == FW_RI_RDMA_WRITE) ||
		 (rqtype && (opcode == FW_RI_READ_RESP));

	switch (status) {
	case T4_ERR_STAG:
		if (send_inv) {
			*layer_type = LAYER_RDMAP|RDMAP_REMOTE_OP;
			*ecode = RDMAP_CANT_INV_STAG;
		} else {
			*layer_type = LAYER_RDMAP|RDMAP_REMOTE_PROT;
			*ecode = RDMAP_INV_STAG;
		}
		break;
	case T4_ERR_PDID:
		*layer_type = LAYER_RDMAP|RDMAP_REMOTE_PROT;
		if ((opcode == FW_RI_SEND_WITH_INV) ||
		    (opcode == FW_RI_SEND_WITH_SE_INV))
			*ecode = RDMAP_CANT_INV_STAG;
		else
			*ecode = RDMAP_STAG_NOT_ASSOC;
		break;
	case T4_ERR_QPID:
		*layer_type = LAYER_RDMAP|RDMAP_REMOTE_PROT;
		*ecode = RDMAP_STAG_NOT_ASSOC;
		break;
	case T4_ERR_ACCESS:
		*layer_type = LAYER_RDMAP|RDMAP_REMOTE_PROT;
		*ecode = RDMAP_ACC_VIOL;
		break;
	case T4_ERR_WRAP:
		*layer_type = LAYER_RDMAP|RDMAP_REMOTE_PROT;
		*ecode = RDMAP_TO_WRAP;
		break;
	case T4_ERR_BOUND:
		if (tagged) {
			*layer_type = LAYER_DDP|DDP_TAGGED_ERR;
			*ecode = DDPT_BASE_BOUNDS;
		} else {
			*layer_type = LAYER_RDMAP|RDMAP_REMOTE_PROT;
			*ecode = RDMAP_BASE_BOUNDS;
		}
		break;
	case T4_ERR_INVALIDATE_SHARED_MR:
	case T4_ERR_INVALIDATE_MR_WITH_MW_BOUND:
		*layer_type = LAYER_RDMAP|RDMAP_REMOTE_OP;
		*ecode = RDMAP_CANT_INV_STAG;
		break;
	case T4_ERR_ECC:
	case T4_ERR_ECC_PSTAG:
	case T4_ERR_INTERNAL_ERR:
		*layer_type = LAYER_RDMAP|RDMAP_LOCAL_CATA;
		*ecode = 0;
		break;
	case T4_ERR_OUT_OF_RQE:
		*layer_type = LAYER_DDP|DDP_UNTAGGED_ERR;
		*ecode = DDPU_INV_MSN_NOBUF;
		break;
	case T4_ERR_PBL_ADDR_BOUND:
		*layer_type = LAYER_DDP|DDP_TAGGED_ERR;
		*ecode = DDPT_BASE_BOUNDS;
		break;
	case T4_ERR_CRC:
		*layer_type = LAYER_MPA|DDP_LLP;
		*ecode = MPA_CRC_ERR;
		break;
	case T4_ERR_MARKER:
		*layer_type = LAYER_MPA|DDP_LLP;
		*ecode = MPA_MARKER_ERR;
		break;
	case T4_ERR_PDU_LEN_ERR:
		*layer_type = LAYER_DDP|DDP_UNTAGGED_ERR;
		*ecode = DDPU_MSG_TOOBIG;
		break;
	case T4_ERR_DDP_VERSION:
		if (tagged) {
			*layer_type = LAYER_DDP|DDP_TAGGED_ERR;
			*ecode = DDPT_INV_VERS;
		} else {
			*layer_type = LAYER_DDP|DDP_UNTAGGED_ERR;
			*ecode = DDPU_INV_VERS;
		}
		break;
	case T4_ERR_RDMA_VERSION:
		*layer_type = LAYER_RDMAP|RDMAP_REMOTE_OP;
		*ecode = RDMAP_INV_VERS;
		break;
	case T4_ERR_OPCODE:
		*layer_type = LAYER_RDMAP|RDMAP_REMOTE_OP;
		*ecode = RDMAP_INV_OPCODE;
		break;
	case T4_ERR_DDP_QUEUE_NUM:
		*layer_type = LAYER_DDP|DDP_UNTAGGED_ERR;
		*ecode = DDPU_INV_QN;
		break;
	case T4_ERR_MSN:
	case T4_ERR_MSN_GAP:
	case T4_ERR_MSN_RANGE:
	case T4_ERR_IRD_OVERFLOW:
		*layer_type = LAYER_DDP|DDP_UNTAGGED_ERR;
		*ecode = DDPU_INV_MSN_RANGE;
		break;
	case T4_ERR_TBIT:
		*layer_type = LAYER_DDP|DDP_LOCAL_CATA;
		*ecode = 0;
		break;
	case T4_ERR_MO:
		*layer_type = LAYER_DDP|DDP_UNTAGGED_ERR;
		*ecode = DDPU_INV_MO;
		break;
	default:
		*layer_type = LAYER_RDMAP|DDP_LOCAL_CATA;
		*ecode = 0;
		break;
	}
}

static void post_terminate(struct c4iw_qp *qhp, struct t4_cqe *err_cqe,
			   gfp_t gfp)
{
	struct fw_ri_wr *wqe;
	struct sk_buff *skb;
	struct terminate_message *term;

	pr_debug("qhp %p qid 0x%x tid %u\n", qhp, qhp->wq.sq.qid,
		 qhp->ep->hwtid);

	skb = skb_dequeue(&qhp->ep->com.ep_skb_list);
	if (WARN_ON(!skb))
		return;

	set_wr_txq(skb, CPL_PRIORITY_DATA, qhp->ep->txq_idx);

	wqe = __skb_put_zero(skb, sizeof(*wqe));
	wqe->op_compl = cpu_to_be32(FW_WR_OP_V(FW_RI_INIT_WR));
	wqe->flowid_len16 = cpu_to_be32(
		FW_WR_FLOWID_V(qhp->ep->hwtid) |
		FW_WR_LEN16_V(DIV_ROUND_UP(sizeof(*wqe), 16)));

	wqe->u.terminate.type = FW_RI_TYPE_TERMINATE;
	wqe->u.terminate.immdlen = cpu_to_be32(sizeof(*term));
	term = (struct terminate_message *)wqe->u.terminate.termmsg;
	if (qhp->attr.layer_etype == (LAYER_MPA|DDP_LLP)) {
		term->layer_etype = qhp->attr.layer_etype;
		term->ecode = qhp->attr.ecode;
	} else
		build_term_codes(err_cqe, &term->layer_etype, &term->ecode);
	c4iw_ofld_send(&qhp->rhp->rdev, skb);
}

/*
 * Assumes qhp lock is held.
 */
static void __flush_qp(struct c4iw_qp *qhp, struct c4iw_cq *rchp,
		       struct c4iw_cq *schp)
{
	int count;
	int rq_flushed = 0, sq_flushed;
	unsigned long flag;

	pr_debug("qhp %p rchp %p schp %p\n", qhp, rchp, schp);

	/* locking hierarchy: cqs lock first, then qp lock. */
	spin_lock_irqsave(&rchp->lock, flag);
	if (schp != rchp)
		spin_lock(&schp->lock);
	spin_lock(&qhp->lock);

	if (qhp->wq.flushed) {
		spin_unlock(&qhp->lock);
		if (schp != rchp)
			spin_unlock(&schp->lock);
		spin_unlock_irqrestore(&rchp->lock, flag);
		return;
	}
	qhp->wq.flushed = 1;
	t4_set_wq_in_error(&qhp->wq, 0);

	c4iw_flush_hw_cq(rchp, qhp);
	if (!qhp->srq) {
		c4iw_count_rcqes(&rchp->cq, &qhp->wq, &count);
		rq_flushed = c4iw_flush_rq(&qhp->wq, &rchp->cq, count);
	}

	if (schp != rchp)
		c4iw_flush_hw_cq(schp, qhp);
	sq_flushed = c4iw_flush_sq(qhp);

	spin_unlock(&qhp->lock);
	if (schp != rchp)
		spin_unlock(&schp->lock);
	spin_unlock_irqrestore(&rchp->lock, flag);

	if (schp == rchp) {
		if ((rq_flushed || sq_flushed) &&
		    t4_clear_cq_armed(&rchp->cq)) {
			spin_lock_irqsave(&rchp->comp_handler_lock, flag);
			(*rchp->ibcq.comp_handler)(&rchp->ibcq,
						   rchp->ibcq.cq_context);
			spin_unlock_irqrestore(&rchp->comp_handler_lock, flag);
		}
	} else {
		if (rq_flushed && t4_clear_cq_armed(&rchp->cq)) {
			spin_lock_irqsave(&rchp->comp_handler_lock, flag);
			(*rchp->ibcq.comp_handler)(&rchp->ibcq,
						   rchp->ibcq.cq_context);
			spin_unlock_irqrestore(&rchp->comp_handler_lock, flag);
		}
		if (sq_flushed && t4_clear_cq_armed(&schp->cq)) {
			spin_lock_irqsave(&schp->comp_handler_lock, flag);
			(*schp->ibcq.comp_handler)(&schp->ibcq,
						   schp->ibcq.cq_context);
			spin_unlock_irqrestore(&schp->comp_handler_lock, flag);
		}
	}
}

static void flush_qp(struct c4iw_qp *qhp)
{
	struct c4iw_cq *rchp, *schp;
	unsigned long flag;

	rchp = to_c4iw_cq(qhp->ibqp.recv_cq);
	schp = to_c4iw_cq(qhp->ibqp.send_cq);

	if (qhp->ibqp.uobject) {

		/* for user qps, qhp->wq.flushed is protected by qhp->mutex */
		if (qhp->wq.flushed)
			return;

		qhp->wq.flushed = 1;
		t4_set_wq_in_error(&qhp->wq, 0);
		t4_set_cq_in_error(&rchp->cq);
		spin_lock_irqsave(&rchp->comp_handler_lock, flag);
		(*rchp->ibcq.comp_handler)(&rchp->ibcq, rchp->ibcq.cq_context);
		spin_unlock_irqrestore(&rchp->comp_handler_lock, flag);
		if (schp != rchp) {
			t4_set_cq_in_error(&schp->cq);
			spin_lock_irqsave(&schp->comp_handler_lock, flag);
			(*schp->ibcq.comp_handler)(&schp->ibcq,
					schp->ibcq.cq_context);
			spin_unlock_irqrestore(&schp->comp_handler_lock, flag);
		}
		return;
	}
	__flush_qp(qhp, rchp, schp);
}

static int rdma_fini(struct c4iw_dev *rhp, struct c4iw_qp *qhp,
		     struct c4iw_ep *ep)
{
	struct fw_ri_wr *wqe;
	int ret;
	struct sk_buff *skb;

	pr_debug("qhp %p qid 0x%x tid %u\n", qhp, qhp->wq.sq.qid, ep->hwtid);

	skb = skb_dequeue(&ep->com.ep_skb_list);
	if (WARN_ON(!skb))
		return -ENOMEM;

	set_wr_txq(skb, CPL_PRIORITY_DATA, ep->txq_idx);

	wqe = __skb_put_zero(skb, sizeof(*wqe));
	wqe->op_compl = cpu_to_be32(
		FW_WR_OP_V(FW_RI_INIT_WR) |
		FW_WR_COMPL_F);
	wqe->flowid_len16 = cpu_to_be32(
		FW_WR_FLOWID_V(ep->hwtid) |
		FW_WR_LEN16_V(DIV_ROUND_UP(sizeof(*wqe), 16)));
	wqe->cookie = (uintptr_t)ep->com.wr_waitp;

	wqe->u.fini.type = FW_RI_TYPE_FINI;

	ret = c4iw_ref_send_wait(&rhp->rdev, skb, ep->com.wr_waitp,
				 qhp->ep->hwtid, qhp->wq.sq.qid, __func__);

	pr_debug("ret %d\n", ret);
	return ret;
}

static void build_rtr_msg(u8 p2p_type, struct fw_ri_init *init)
{
	pr_debug("p2p_type = %d\n", p2p_type);
	memset(&init->u, 0, sizeof(init->u));
	switch (p2p_type) {
	case FW_RI_INIT_P2PTYPE_RDMA_WRITE:
		init->u.write.opcode = FW_RI_RDMA_WRITE_WR;
		init->u.write.stag_sink = cpu_to_be32(1);
		init->u.write.to_sink = cpu_to_be64(1);
		init->u.write.u.immd_src[0].op = FW_RI_DATA_IMMD;
		init->u.write.len16 = DIV_ROUND_UP(
			sizeof(init->u.write) + sizeof(struct fw_ri_immd), 16);
		break;
	case FW_RI_INIT_P2PTYPE_READ_REQ:
		init->u.write.opcode = FW_RI_RDMA_READ_WR;
		init->u.read.stag_src = cpu_to_be32(1);
		init->u.read.to_src_lo = cpu_to_be32(1);
		init->u.read.stag_sink = cpu_to_be32(1);
		init->u.read.to_sink_lo = cpu_to_be32(1);
		init->u.read.len16 = DIV_ROUND_UP(sizeof(init->u.read), 16);
		break;
	}
}

static int rdma_init(struct c4iw_dev *rhp, struct c4iw_qp *qhp)
{
	struct fw_ri_wr *wqe;
	int ret;
	struct sk_buff *skb;

	pr_debug("qhp %p qid 0x%x tid %u ird %u ord %u\n", qhp,
		 qhp->wq.sq.qid, qhp->ep->hwtid, qhp->ep->ird, qhp->ep->ord);

	skb = alloc_skb(sizeof(*wqe), GFP_KERNEL);
	if (!skb) {
		ret = -ENOMEM;
		goto out;
	}
	ret = alloc_ird(rhp, qhp->attr.max_ird);
	if (ret) {
		qhp->attr.max_ird = 0;
		kfree_skb(skb);
		goto out;
	}
	set_wr_txq(skb, CPL_PRIORITY_DATA, qhp->ep->txq_idx);

	wqe = __skb_put_zero(skb, sizeof(*wqe));
	wqe->op_compl = cpu_to_be32(
		FW_WR_OP_V(FW_RI_INIT_WR) |
		FW_WR_COMPL_F);
	wqe->flowid_len16 = cpu_to_be32(
		FW_WR_FLOWID_V(qhp->ep->hwtid) |
		FW_WR_LEN16_V(DIV_ROUND_UP(sizeof(*wqe), 16)));

	wqe->cookie = (uintptr_t)qhp->ep->com.wr_waitp;

	wqe->u.init.type = FW_RI_TYPE_INIT;
	wqe->u.init.mpareqbit_p2ptype =
		FW_RI_WR_MPAREQBIT_V(qhp->attr.mpa_attr.initiator) |
		FW_RI_WR_P2PTYPE_V(qhp->attr.mpa_attr.p2p_type);
	wqe->u.init.mpa_attrs = FW_RI_MPA_IETF_ENABLE;
	if (qhp->attr.mpa_attr.recv_marker_enabled)
		wqe->u.init.mpa_attrs |= FW_RI_MPA_RX_MARKER_ENABLE;
	if (qhp->attr.mpa_attr.xmit_marker_enabled)
		wqe->u.init.mpa_attrs |= FW_RI_MPA_TX_MARKER_ENABLE;
	if (qhp->attr.mpa_attr.crc_enabled)
		wqe->u.init.mpa_attrs |= FW_RI_MPA_CRC_ENABLE;

	wqe->u.init.qp_caps = FW_RI_QP_RDMA_READ_ENABLE |
			    FW_RI_QP_RDMA_WRITE_ENABLE |
			    FW_RI_QP_BIND_ENABLE;
	if (!qhp->ibqp.uobject)
		wqe->u.init.qp_caps |= FW_RI_QP_FAST_REGISTER_ENABLE |
				     FW_RI_QP_STAG0_ENABLE;
	wqe->u.init.nrqe = cpu_to_be16(t4_rqes_posted(&qhp->wq));
	wqe->u.init.pdid = cpu_to_be32(qhp->attr.pd);
	wqe->u.init.qpid = cpu_to_be32(qhp->wq.sq.qid);
	wqe->u.init.sq_eqid = cpu_to_be32(qhp->wq.sq.qid);
	if (qhp->srq) {
		wqe->u.init.rq_eqid = cpu_to_be32(FW_RI_INIT_RQEQID_SRQ |
						  qhp->srq->idx);
	} else {
		wqe->u.init.rq_eqid = cpu_to_be32(qhp->wq.rq.qid);
		wqe->u.init.hwrqsize = cpu_to_be32(qhp->wq.rq.rqt_size);
		wqe->u.init.hwrqaddr = cpu_to_be32(qhp->wq.rq.rqt_hwaddr -
						   rhp->rdev.lldi.vr->rq.start);
	}
	wqe->u.init.scqid = cpu_to_be32(qhp->attr.scq);
	wqe->u.init.rcqid = cpu_to_be32(qhp->attr.rcq);
	wqe->u.init.ord_max = cpu_to_be32(qhp->attr.max_ord);
	wqe->u.init.ird_max = cpu_to_be32(qhp->attr.max_ird);
	wqe->u.init.iss = cpu_to_be32(qhp->ep->snd_seq);
	wqe->u.init.irs = cpu_to_be32(qhp->ep->rcv_seq);
	if (qhp->attr.mpa_attr.initiator)
		build_rtr_msg(qhp->attr.mpa_attr.p2p_type, &wqe->u.init);

	ret = c4iw_ref_send_wait(&rhp->rdev, skb, qhp->ep->com.wr_waitp,
				 qhp->ep->hwtid, qhp->wq.sq.qid, __func__);
	if (!ret)
		goto out;

	free_ird(rhp, qhp->attr.max_ird);
out:
	pr_debug("ret %d\n", ret);
	return ret;
}

int c4iw_modify_qp(struct c4iw_dev *rhp, struct c4iw_qp *qhp,
		   enum c4iw_qp_attr_mask mask,
		   struct c4iw_qp_attributes *attrs,
		   int internal)
{
	int ret = 0;
	struct c4iw_qp_attributes newattr = qhp->attr;
	int disconnect = 0;
	int terminate = 0;
	int abort = 0;
	int free = 0;
	struct c4iw_ep *ep = NULL;

	pr_debug("qhp %p sqid 0x%x rqid 0x%x ep %p state %d -> %d\n",
		 qhp, qhp->wq.sq.qid, qhp->wq.rq.qid, qhp->ep, qhp->attr.state,
		 (mask & C4IW_QP_ATTR_NEXT_STATE) ? attrs->next_state : -1);

	mutex_lock(&qhp->mutex);

	/* Process attr changes if in IDLE */
	if (mask & C4IW_QP_ATTR_VALID_MODIFY) {
		if (qhp->attr.state != C4IW_QP_STATE_IDLE) {
			ret = -EIO;
			goto out;
		}
		if (mask & C4IW_QP_ATTR_ENABLE_RDMA_READ)
			newattr.enable_rdma_read = attrs->enable_rdma_read;
		if (mask & C4IW_QP_ATTR_ENABLE_RDMA_WRITE)
			newattr.enable_rdma_write = attrs->enable_rdma_write;
		if (mask & C4IW_QP_ATTR_ENABLE_RDMA_BIND)
			newattr.enable_bind = attrs->enable_bind;
		if (mask & C4IW_QP_ATTR_MAX_ORD) {
			if (attrs->max_ord > c4iw_max_read_depth) {
				ret = -EINVAL;
				goto out;
			}
			newattr.max_ord = attrs->max_ord;
		}
		if (mask & C4IW_QP_ATTR_MAX_IRD) {
			if (attrs->max_ird > cur_max_read_depth(rhp)) {
				ret = -EINVAL;
				goto out;
			}
			newattr.max_ird = attrs->max_ird;
		}
		qhp->attr = newattr;
	}

	if (mask & C4IW_QP_ATTR_SQ_DB) {
		ret = ring_kernel_sq_db(qhp, attrs->sq_db_inc);
		goto out;
	}
	if (mask & C4IW_QP_ATTR_RQ_DB) {
		ret = ring_kernel_rq_db(qhp, attrs->rq_db_inc);
		goto out;
	}

	if (!(mask & C4IW_QP_ATTR_NEXT_STATE))
		goto out;
	if (qhp->attr.state == attrs->next_state)
		goto out;

	switch (qhp->attr.state) {
	case C4IW_QP_STATE_IDLE:
		switch (attrs->next_state) {
		case C4IW_QP_STATE_RTS:
			if (!(mask & C4IW_QP_ATTR_LLP_STREAM_HANDLE)) {
				ret = -EINVAL;
				goto out;
			}
			if (!(mask & C4IW_QP_ATTR_MPA_ATTR)) {
				ret = -EINVAL;
				goto out;
			}
			qhp->attr.mpa_attr = attrs->mpa_attr;
			qhp->attr.llp_stream_handle = attrs->llp_stream_handle;
			qhp->ep = qhp->attr.llp_stream_handle;
			set_state(qhp, C4IW_QP_STATE_RTS);

			/*
			 * Ref the endpoint here and deref when we
			 * disassociate the endpoint from the QP.  This
			 * happens in CLOSING->IDLE transition or *->ERROR
			 * transition.
			 */
			c4iw_get_ep(&qhp->ep->com);
			ret = rdma_init(rhp, qhp);
			if (ret)
				goto err;
			break;
		case C4IW_QP_STATE_ERROR:
			set_state(qhp, C4IW_QP_STATE_ERROR);
			flush_qp(qhp);
			break;
		default:
			ret = -EINVAL;
			goto out;
		}
		break;
	case C4IW_QP_STATE_RTS:
		switch (attrs->next_state) {
		case C4IW_QP_STATE_CLOSING:
			t4_set_wq_in_error(&qhp->wq, 0);
			set_state(qhp, C4IW_QP_STATE_CLOSING);
			ep = qhp->ep;
			if (!internal) {
				abort = 0;
				disconnect = 1;
				c4iw_get_ep(&qhp->ep->com);
			}
			ret = rdma_fini(rhp, qhp, ep);
			if (ret)
				goto err;
			break;
		case C4IW_QP_STATE_TERMINATE:
			t4_set_wq_in_error(&qhp->wq, 0);
			set_state(qhp, C4IW_QP_STATE_TERMINATE);
			qhp->attr.layer_etype = attrs->layer_etype;
			qhp->attr.ecode = attrs->ecode;
			ep = qhp->ep;
			if (!internal) {
				c4iw_get_ep(&ep->com);
				terminate = 1;
				disconnect = 1;
			} else {
				terminate = qhp->attr.send_term;
				ret = rdma_fini(rhp, qhp, ep);
				if (ret)
					goto err;
			}
			break;
		case C4IW_QP_STATE_ERROR:
			t4_set_wq_in_error(&qhp->wq, 0);
			set_state(qhp, C4IW_QP_STATE_ERROR);
			if (!internal) {
				disconnect = 1;
				ep = qhp->ep;
				c4iw_get_ep(&qhp->ep->com);
			}
			goto err;
			break;
		default:
			ret = -EINVAL;
			goto out;
		}
		break;
	case C4IW_QP_STATE_CLOSING:

		/*
		 * Allow kernel users to move to ERROR for qp draining.
		 */
		if (!internal && (qhp->ibqp.uobject || attrs->next_state !=
				  C4IW_QP_STATE_ERROR)) {
			ret = -EINVAL;
			goto out;
		}
		switch (attrs->next_state) {
		case C4IW_QP_STATE_IDLE:
			flush_qp(qhp);
			set_state(qhp, C4IW_QP_STATE_IDLE);
			qhp->attr.llp_stream_handle = NULL;
			c4iw_put_ep(&qhp->ep->com);
			qhp->ep = NULL;
			wake_up(&qhp->wait);
			break;
		case C4IW_QP_STATE_ERROR:
			goto err;
		default:
			ret = -EINVAL;
			goto err;
		}
		break;
	case C4IW_QP_STATE_ERROR:
		if (attrs->next_state != C4IW_QP_STATE_IDLE) {
			ret = -EINVAL;
			goto out;
		}
		if (!t4_sq_empty(&qhp->wq) || !t4_rq_empty(&qhp->wq)) {
			ret = -EINVAL;
			goto out;
		}
		set_state(qhp, C4IW_QP_STATE_IDLE);
		break;
	case C4IW_QP_STATE_TERMINATE:
		if (!internal) {
			ret = -EINVAL;
			goto out;
		}
		goto err;
		break;
	default:
		pr_err("%s in a bad state %d\n", __func__, qhp->attr.state);
		ret = -EINVAL;
		goto err;
		break;
	}
	goto out;
err:
	pr_debug("disassociating ep %p qpid 0x%x\n", qhp->ep,
		 qhp->wq.sq.qid);

	/* disassociate the LLP connection */
	qhp->attr.llp_stream_handle = NULL;
	if (!ep)
		ep = qhp->ep;
	qhp->ep = NULL;
	set_state(qhp, C4IW_QP_STATE_ERROR);
	free = 1;
	abort = 1;
	flush_qp(qhp);
	wake_up(&qhp->wait);
out:
	mutex_unlock(&qhp->mutex);

	if (terminate)
		post_terminate(qhp, NULL, internal ? GFP_ATOMIC : GFP_KERNEL);

	/*
	 * If disconnect is 1, then we need to initiate a disconnect
	 * on the EP.  This can be a normal close (RTS->CLOSING) or
	 * an abnormal close (RTS/CLOSING->ERROR).
	 */
	if (disconnect) {
		c4iw_ep_disconnect(ep, abort, internal ? GFP_ATOMIC :
							 GFP_KERNEL);
		c4iw_put_ep(&ep->com);
	}

	/*
	 * If free is 1, then we've disassociated the EP from the QP
	 * and we need to dereference the EP.
	 */
	if (free)
		c4iw_put_ep(&ep->com);
	pr_debug("exit state %d\n", qhp->attr.state);
	return ret;
}

int c4iw_destroy_qp(struct ib_qp *ib_qp, struct ib_udata *udata)
{
	struct c4iw_dev *rhp;
	struct c4iw_qp *qhp;
	struct c4iw_ucontext *ucontext;
	struct c4iw_qp_attributes attrs;

	qhp = to_c4iw_qp(ib_qp);
	rhp = qhp->rhp;
	ucontext = qhp->ucontext;

	attrs.next_state = C4IW_QP_STATE_ERROR;
	if (qhp->attr.state == C4IW_QP_STATE_TERMINATE)
		c4iw_modify_qp(rhp, qhp, C4IW_QP_ATTR_NEXT_STATE, &attrs, 1);
	else
		c4iw_modify_qp(rhp, qhp, C4IW_QP_ATTR_NEXT_STATE, &attrs, 0);
	wait_event(qhp->wait, !qhp->ep);

	xa_lock_irq(&rhp->qps);
	__xa_erase(&rhp->qps, qhp->wq.sq.qid);
	if (!list_empty(&qhp->db_fc_entry))
		list_del_init(&qhp->db_fc_entry);
	xa_unlock_irq(&rhp->qps);
	free_ird(rhp, qhp->attr.max_ird);

	c4iw_qp_rem_ref(ib_qp);

	wait_for_completion(&qhp->qp_rel_comp);

	pr_debug("ib_qp %p qpid 0x%0x\n", ib_qp, qhp->wq.sq.qid);
	pr_debug("qhp %p ucontext %p\n", qhp, ucontext);

	destroy_qp(&rhp->rdev, &qhp->wq,
		   ucontext ? &ucontext->uctx : &rhp->rdev.uctx, !qhp->srq);

	c4iw_put_wr_wait(qhp->wr_waitp);
	return 0;
}

int c4iw_create_qp(struct ib_qp *qp, struct ib_qp_init_attr *attrs,
		   struct ib_udata *udata)
{
	struct ib_pd *pd = qp->pd;
	struct c4iw_dev *rhp;
	struct c4iw_qp *qhp = to_c4iw_qp(qp);
	struct c4iw_pd *php;
	struct c4iw_cq *schp;
	struct c4iw_cq *rchp;
	struct c4iw_create_qp_resp uresp;
	unsigned int sqsize, rqsize = 0;
	struct c4iw_ucontext *ucontext = rdma_udata_to_drv_context(
		udata, struct c4iw_ucontext, ibucontext);
	int ret;
	struct c4iw_mm_entry *sq_key_mm, *rq_key_mm = NULL, *sq_db_key_mm;
	struct c4iw_mm_entry *rq_db_key_mm = NULL, *ma_sync_key_mm = NULL;

	if (attrs->qp_type != IB_QPT_RC || attrs->create_flags)
		return -EOPNOTSUPP;

	php = to_c4iw_pd(pd);
	rhp = php->rhp;
	schp = get_chp(rhp, ((struct c4iw_cq *)attrs->send_cq)->cq.cqid);
	rchp = get_chp(rhp, ((struct c4iw_cq *)attrs->recv_cq)->cq.cqid);
	if (!schp || !rchp)
		return -EINVAL;

	if (attrs->cap.max_inline_data > T4_MAX_SEND_INLINE)
		return -EINVAL;

	if (!attrs->srq) {
		if (attrs->cap.max_recv_wr > rhp->rdev.hw_queue.t4_max_rq_size)
			return -E2BIG;
		rqsize = attrs->cap.max_recv_wr + 1;
		if (rqsize < 8)
			rqsize = 8;
	}

	if (attrs->cap.max_send_wr > rhp->rdev.hw_queue.t4_max_sq_size)
		return -E2BIG;
	sqsize = attrs->cap.max_send_wr + 1;
	if (sqsize < 8)
		sqsize = 8;

	qhp->wr_waitp = c4iw_alloc_wr_wait(GFP_KERNEL);
	if (!qhp->wr_waitp)
		return -ENOMEM;

	qhp->wq.sq.size = sqsize;
	qhp->wq.sq.memsize =
		(sqsize + rhp->rdev.hw_queue.t4_eq_status_entries) *
		sizeof(*qhp->wq.sq.queue) + 16 * sizeof(__be64);
	qhp->wq.sq.flush_cidx = -1;
	if (!attrs->srq) {
		qhp->wq.rq.size = rqsize;
		qhp->wq.rq.memsize =
			(rqsize + rhp->rdev.hw_queue.t4_eq_status_entries) *
			sizeof(*qhp->wq.rq.queue);
	}

	if (ucontext) {
		qhp->wq.sq.memsize = roundup(qhp->wq.sq.memsize, PAGE_SIZE);
		if (!attrs->srq)
			qhp->wq.rq.memsize =
				roundup(qhp->wq.rq.memsize, PAGE_SIZE);
	}

	ret = create_qp(&rhp->rdev, &qhp->wq, &schp->cq, &rchp->cq,
			ucontext ? &ucontext->uctx : &rhp->rdev.uctx,
			qhp->wr_waitp, !attrs->srq);
	if (ret)
		goto err_free_wr_wait;

	attrs->cap.max_recv_wr = rqsize - 1;
	attrs->cap.max_send_wr = sqsize - 1;
	attrs->cap.max_inline_data = T4_MAX_SEND_INLINE;

	qhp->rhp = rhp;
	qhp->attr.pd = php->pdid;
	qhp->attr.scq = ((struct c4iw_cq *) attrs->send_cq)->cq.cqid;
	qhp->attr.rcq = ((struct c4iw_cq *) attrs->recv_cq)->cq.cqid;
	qhp->attr.sq_num_entries = attrs->cap.max_send_wr;
	qhp->attr.sq_max_sges = attrs->cap.max_send_sge;
	qhp->attr.sq_max_sges_rdma_write = attrs->cap.max_send_sge;
	if (!attrs->srq) {
		qhp->attr.rq_num_entries = attrs->cap.max_recv_wr;
		qhp->attr.rq_max_sges = attrs->cap.max_recv_sge;
	}
	qhp->attr.state = C4IW_QP_STATE_IDLE;
	qhp->attr.next_state = C4IW_QP_STATE_IDLE;
	qhp->attr.enable_rdma_read = 1;
	qhp->attr.enable_rdma_write = 1;
	qhp->attr.enable_bind = 1;
	qhp->attr.max_ord = 0;
	qhp->attr.max_ird = 0;
	qhp->sq_sig_all = attrs->sq_sig_type == IB_SIGNAL_ALL_WR;
	spin_lock_init(&qhp->lock);
	mutex_init(&qhp->mutex);
	init_waitqueue_head(&qhp->wait);
	init_completion(&qhp->qp_rel_comp);
	refcount_set(&qhp->qp_refcnt, 1);

	ret = xa_insert_irq(&rhp->qps, qhp->wq.sq.qid, qhp, GFP_KERNEL);
	if (ret)
		goto err_destroy_qp;

	if (udata && ucontext) {
		sq_key_mm = kmalloc(sizeof(*sq_key_mm), GFP_KERNEL);
		if (!sq_key_mm) {
			ret = -ENOMEM;
			goto err_remove_handle;
		}
		if (!attrs->srq) {
			rq_key_mm = kmalloc(sizeof(*rq_key_mm), GFP_KERNEL);
			if (!rq_key_mm) {
				ret = -ENOMEM;
				goto err_free_sq_key;
			}
		}
		sq_db_key_mm = kmalloc(sizeof(*sq_db_key_mm), GFP_KERNEL);
		if (!sq_db_key_mm) {
			ret = -ENOMEM;
			goto err_free_rq_key;
		}
		if (!attrs->srq) {
			rq_db_key_mm =
				kmalloc(sizeof(*rq_db_key_mm), GFP_KERNEL);
			if (!rq_db_key_mm) {
				ret = -ENOMEM;
				goto err_free_sq_db_key;
			}
		}
		memset(&uresp, 0, sizeof(uresp));
		if (t4_sq_onchip(&qhp->wq.sq)) {
			ma_sync_key_mm = kmalloc(sizeof(*ma_sync_key_mm),
						 GFP_KERNEL);
			if (!ma_sync_key_mm) {
				ret = -ENOMEM;
				goto err_free_rq_db_key;
			}
			uresp.flags = C4IW_QPF_ONCHIP;
		}
		if (rhp->rdev.lldi.write_w_imm_support)
			uresp.flags |= C4IW_QPF_WRITE_W_IMM;
		uresp.qid_mask = rhp->rdev.qpmask;
		uresp.sqid = qhp->wq.sq.qid;
		uresp.sq_size = qhp->wq.sq.size;
		uresp.sq_memsize = qhp->wq.sq.memsize;
		if (!attrs->srq) {
			uresp.rqid = qhp->wq.rq.qid;
			uresp.rq_size = qhp->wq.rq.size;
			uresp.rq_memsize = qhp->wq.rq.memsize;
		}
		spin_lock(&ucontext->mmap_lock);
		if (ma_sync_key_mm) {
			uresp.ma_sync_key = ucontext->key;
			ucontext->key += PAGE_SIZE;
		}
		uresp.sq_key = ucontext->key;
		ucontext->key += PAGE_SIZE;
		if (!attrs->srq) {
			uresp.rq_key = ucontext->key;
			ucontext->key += PAGE_SIZE;
		}
		uresp.sq_db_gts_key = ucontext->key;
		ucontext->key += PAGE_SIZE;
		if (!attrs->srq) {
			uresp.rq_db_gts_key = ucontext->key;
			ucontext->key += PAGE_SIZE;
		}
		spin_unlock(&ucontext->mmap_lock);
		ret = ib_copy_to_udata(udata, &uresp, sizeof(uresp));
		if (ret)
			goto err_free_ma_sync_key;
		sq_key_mm->key = uresp.sq_key;
		sq_key_mm->addr = qhp->wq.sq.phys_addr;
		sq_key_mm->len = PAGE_ALIGN(qhp->wq.sq.memsize);
		insert_mmap(ucontext, sq_key_mm);
		if (!attrs->srq) {
			rq_key_mm->key = uresp.rq_key;
			rq_key_mm->addr = virt_to_phys(qhp->wq.rq.queue);
			rq_key_mm->len = PAGE_ALIGN(qhp->wq.rq.memsize);
			insert_mmap(ucontext, rq_key_mm);
		}
		sq_db_key_mm->key = uresp.sq_db_gts_key;
		sq_db_key_mm->addr = (u64)(unsigned long)qhp->wq.sq.bar2_pa;
		sq_db_key_mm->len = PAGE_SIZE;
		insert_mmap(ucontext, sq_db_key_mm);
		if (!attrs->srq) {
			rq_db_key_mm->key = uresp.rq_db_gts_key;
			rq_db_key_mm->addr =
				(u64)(unsigned long)qhp->wq.rq.bar2_pa;
			rq_db_key_mm->len = PAGE_SIZE;
			insert_mmap(ucontext, rq_db_key_mm);
		}
		if (ma_sync_key_mm) {
			ma_sync_key_mm->key = uresp.ma_sync_key;
			ma_sync_key_mm->addr =
				(pci_resource_start(rhp->rdev.lldi.pdev, 0) +
				PCIE_MA_SYNC_A) & PAGE_MASK;
			ma_sync_key_mm->len = PAGE_SIZE;
			insert_mmap(ucontext, ma_sync_key_mm);
		}

		qhp->ucontext = ucontext;
	}
	if (!attrs->srq) {
		qhp->wq.qp_errp =
			&qhp->wq.rq.queue[qhp->wq.rq.size].status.qp_err;
	} else {
		qhp->wq.qp_errp =
			&qhp->wq.sq.queue[qhp->wq.sq.size].status.qp_err;
		qhp->wq.srqidxp =
			&qhp->wq.sq.queue[qhp->wq.sq.size].status.srqidx;
	}

	qhp->ibqp.qp_num = qhp->wq.sq.qid;
	if (attrs->srq)
		qhp->srq = to_c4iw_srq(attrs->srq);
	INIT_LIST_HEAD(&qhp->db_fc_entry);
	pr_debug("sq id %u size %u memsize %zu num_entries %u rq id %u size %u memsize %zu num_entries %u\n",
		 qhp->wq.sq.qid, qhp->wq.sq.size, qhp->wq.sq.memsize,
		 attrs->cap.max_send_wr, qhp->wq.rq.qid, qhp->wq.rq.size,
		 qhp->wq.rq.memsize, attrs->cap.max_recv_wr);
	return 0;
err_free_ma_sync_key:
	kfree(ma_sync_key_mm);
err_free_rq_db_key:
	if (!attrs->srq)
		kfree(rq_db_key_mm);
err_free_sq_db_key:
	kfree(sq_db_key_mm);
err_free_rq_key:
	if (!attrs->srq)
		kfree(rq_key_mm);
err_free_sq_key:
	kfree(sq_key_mm);
err_remove_handle:
	xa_erase_irq(&rhp->qps, qhp->wq.sq.qid);
err_destroy_qp:
	destroy_qp(&rhp->rdev, &qhp->wq,
		   ucontext ? &ucontext->uctx : &rhp->rdev.uctx, !attrs->srq);
err_free_wr_wait:
	c4iw_put_wr_wait(qhp->wr_waitp);
	return ret;
}

int c4iw_ib_modify_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr,
		      int attr_mask, struct ib_udata *udata)
{
	struct c4iw_dev *rhp;
	struct c4iw_qp *qhp;
	enum c4iw_qp_attr_mask mask = 0;
	struct c4iw_qp_attributes attrs = {};

	pr_debug("ib_qp %p\n", ibqp);

	if (attr_mask & ~IB_QP_ATTR_STANDARD_BITS)
		return -EOPNOTSUPP;

	/* iwarp does not support the RTR state */
	if ((attr_mask & IB_QP_STATE) && (attr->qp_state == IB_QPS_RTR))
		attr_mask &= ~IB_QP_STATE;

	/* Make sure we still have something left to do */
	if (!attr_mask)
		return 0;

	qhp = to_c4iw_qp(ibqp);
	rhp = qhp->rhp;

	attrs.next_state = c4iw_convert_state(attr->qp_state);
	attrs.enable_rdma_read = (attr->qp_access_flags &
			       IB_ACCESS_REMOTE_READ) ?  1 : 0;
	attrs.enable_rdma_write = (attr->qp_access_flags &
				IB_ACCESS_REMOTE_WRITE) ? 1 : 0;
	attrs.enable_bind = (attr->qp_access_flags & IB_ACCESS_MW_BIND) ? 1 : 0;


	mask |= (attr_mask & IB_QP_STATE) ? C4IW_QP_ATTR_NEXT_STATE : 0;
	mask |= (attr_mask & IB_QP_ACCESS_FLAGS) ?
			(C4IW_QP_ATTR_ENABLE_RDMA_READ |
			 C4IW_QP_ATTR_ENABLE_RDMA_WRITE |
			 C4IW_QP_ATTR_ENABLE_RDMA_BIND) : 0;

	/*
	 * Use SQ_PSN and RQ_PSN to pass in IDX_INC values for
	 * ringing the queue db when we're in DB_FULL mode.
	 * Only allow this on T4 devices.
	 */
	attrs.sq_db_inc = attr->sq_psn;
	attrs.rq_db_inc = attr->rq_psn;
	mask |= (attr_mask & IB_QP_SQ_PSN) ? C4IW_QP_ATTR_SQ_DB : 0;
	mask |= (attr_mask & IB_QP_RQ_PSN) ? C4IW_QP_ATTR_RQ_DB : 0;
	if (!is_t4(to_c4iw_qp(ibqp)->rhp->rdev.lldi.adapter_type) &&
	    (mask & (C4IW_QP_ATTR_SQ_DB|C4IW_QP_ATTR_RQ_DB)))
		return -EINVAL;

	return c4iw_modify_qp(rhp, qhp, mask, &attrs, 0);
}

struct ib_qp *c4iw_get_qp(struct ib_device *dev, int qpn)
{
	pr_debug("ib_dev %p qpn 0x%x\n", dev, qpn);
	return (struct ib_qp *)get_qhp(to_c4iw_dev(dev), qpn);
}

void c4iw_dispatch_srq_limit_reached_event(struct c4iw_srq *srq)
{
	struct ib_event event = {};

	event.device = &srq->rhp->ibdev;
	event.element.srq = &srq->ibsrq;
	event.event = IB_EVENT_SRQ_LIMIT_REACHED;
	ib_dispatch_event(&event);
}

int c4iw_modify_srq(struct ib_srq *ib_srq, struct ib_srq_attr *attr,
		    enum ib_srq_attr_mask srq_attr_mask,
		    struct ib_udata *udata)
{
	struct c4iw_srq *srq = to_c4iw_srq(ib_srq);
	int ret = 0;

	/*
	 * XXX 0 mask == a SW interrupt for srq_limit reached...
	 */
	if (udata && !srq_attr_mask) {
		c4iw_dispatch_srq_limit_reached_event(srq);
		goto out;
	}

	/* no support for this yet */
	if (srq_attr_mask & IB_SRQ_MAX_WR) {
		ret = -EINVAL;
		goto out;
	}

	if (!udata && (srq_attr_mask & IB_SRQ_LIMIT)) {
		srq->armed = true;
		srq->srq_limit = attr->srq_limit;
	}
out:
	return ret;
}

int c4iw_ib_query_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr,
		     int attr_mask, struct ib_qp_init_attr *init_attr)
{
	struct c4iw_qp *qhp = to_c4iw_qp(ibqp);

	memset(attr, 0, sizeof(*attr));
	memset(init_attr, 0, sizeof(*init_attr));
	attr->qp_state = to_ib_qp_state(qhp->attr.state);
	attr->cur_qp_state = to_ib_qp_state(qhp->attr.state);
	init_attr->cap.max_send_wr = qhp->attr.sq_num_entries;
	init_attr->cap.max_recv_wr = qhp->attr.rq_num_entries;
	init_attr->cap.max_send_sge = qhp->attr.sq_max_sges;
	init_attr->cap.max_recv_sge = qhp->attr.rq_max_sges;
	init_attr->cap.max_inline_data = T4_MAX_SEND_INLINE;
	init_attr->sq_sig_type = qhp->sq_sig_all ? IB_SIGNAL_ALL_WR : 0;
	return 0;
}

static void free_srq_queue(struct c4iw_srq *srq, struct c4iw_dev_ucontext *uctx,
			   struct c4iw_wr_wait *wr_waitp)
{
	struct c4iw_rdev *rdev = &srq->rhp->rdev;
	struct sk_buff *skb = srq->destroy_skb;
	struct t4_srq *wq = &srq->wq;
	struct fw_ri_res_wr *res_wr;
	struct fw_ri_res *res;
	int wr_len;

	wr_len = sizeof(*res_wr) + sizeof(*res);
	set_wr_txq(skb, CPL_PRIORITY_CONTROL, 0);

	res_wr = (struct fw_ri_res_wr *)__skb_put(skb, wr_len);
	memset(res_wr, 0, wr_len);
	res_wr->op_nres = cpu_to_be32(FW_WR_OP_V(FW_RI_RES_WR) |
			FW_RI_RES_WR_NRES_V(1) |
			FW_WR_COMPL_F);
	res_wr->len16_pkd = cpu_to_be32(DIV_ROUND_UP(wr_len, 16));
	res_wr->cookie = (uintptr_t)wr_waitp;
	res = res_wr->res;
	res->u.srq.restype = FW_RI_RES_TYPE_SRQ;
	res->u.srq.op = FW_RI_RES_OP_RESET;
	res->u.srq.srqid = cpu_to_be32(srq->idx);
	res->u.srq.eqid = cpu_to_be32(wq->qid);

	c4iw_init_wr_wait(wr_waitp);
	c4iw_ref_send_wait(rdev, skb, wr_waitp, 0, 0, __func__);

	dma_free_coherent(&rdev->lldi.pdev->dev,
			  wq->memsize, wq->queue,
			dma_unmap_addr(wq, mapping));
	c4iw_rqtpool_free(rdev, wq->rqt_hwaddr, wq->rqt_size);
	kfree(wq->sw_rq);
	c4iw_put_qpid(rdev, wq->qid, uctx);
}

static int alloc_srq_queue(struct c4iw_srq *srq, struct c4iw_dev_ucontext *uctx,
			   struct c4iw_wr_wait *wr_waitp)
{
	struct c4iw_rdev *rdev = &srq->rhp->rdev;
	int user = (uctx != &rdev->uctx);
	struct t4_srq *wq = &srq->wq;
	struct fw_ri_res_wr *res_wr;
	struct fw_ri_res *res;
	struct sk_buff *skb;
	int wr_len;
	int eqsize;
	int ret = -ENOMEM;

	wq->qid = c4iw_get_qpid(rdev, uctx);
	if (!wq->qid)
		goto err;

	if (!user) {
		wq->sw_rq = kcalloc(wq->size, sizeof(*wq->sw_rq),
				    GFP_KERNEL);
		if (!wq->sw_rq)
			goto err_put_qpid;
		wq->pending_wrs = kcalloc(srq->wq.size,
					  sizeof(*srq->wq.pending_wrs),
					  GFP_KERNEL);
		if (!wq->pending_wrs)
			goto err_free_sw_rq;
	}

	wq->rqt_size = wq->size;
	wq->rqt_hwaddr = c4iw_rqtpool_alloc(rdev, wq->rqt_size);
	if (!wq->rqt_hwaddr)
		goto err_free_pending_wrs;
	wq->rqt_abs_idx = (wq->rqt_hwaddr - rdev->lldi.vr->rq.start) >>
		T4_RQT_ENTRY_SHIFT;

	wq->queue = dma_alloc_coherent(&rdev->lldi.pdev->dev, wq->memsize,
				       &wq->dma_addr, GFP_KERNEL);
	if (!wq->queue)
		goto err_free_rqtpool;

	dma_unmap_addr_set(wq, mapping, wq->dma_addr);

	wq->bar2_va = c4iw_bar2_addrs(rdev, wq->qid, CXGB4_BAR2_QTYPE_EGRESS,
				      &wq->bar2_qid,
			user ? &wq->bar2_pa : NULL);

	/*
	 * User mode must have bar2 access.
	 */

	if (user && !wq->bar2_va) {
		pr_warn(MOD "%s: srqid %u not in BAR2 range.\n",
			pci_name(rdev->lldi.pdev), wq->qid);
		ret = -EINVAL;
		goto err_free_queue;
	}

	/* build fw_ri_res_wr */
	wr_len = sizeof(*res_wr) + sizeof(*res);

	skb = alloc_skb(wr_len, GFP_KERNEL);
	if (!skb)
		goto err_free_queue;
	set_wr_txq(skb, CPL_PRIORITY_CONTROL, 0);

	res_wr = (struct fw_ri_res_wr *)__skb_put(skb, wr_len);
	memset(res_wr, 0, wr_len);
	res_wr->op_nres = cpu_to_be32(FW_WR_OP_V(FW_RI_RES_WR) |
			FW_RI_RES_WR_NRES_V(1) |
			FW_WR_COMPL_F);
	res_wr->len16_pkd = cpu_to_be32(DIV_ROUND_UP(wr_len, 16));
	res_wr->cookie = (uintptr_t)wr_waitp;
	res = res_wr->res;
	res->u.srq.restype = FW_RI_RES_TYPE_SRQ;
	res->u.srq.op = FW_RI_RES_OP_WRITE;

	/*
	 * eqsize is the number of 64B entries plus the status page size.
	 */
	eqsize = wq->size * T4_RQ_NUM_SLOTS +
		rdev->hw_queue.t4_eq_status_entries;
	res->u.srq.eqid = cpu_to_be32(wq->qid);
	res->u.srq.fetchszm_to_iqid =
						/* no host cidx updates */
		cpu_to_be32(FW_RI_RES_WR_HOSTFCMODE_V(0) |
		FW_RI_RES_WR_CPRIO_V(0) |       /* don't keep in chip cache */
		FW_RI_RES_WR_PCIECHN_V(0) |     /* set by uP at ri_init time */
		FW_RI_RES_WR_FETCHRO_V(0));     /* relaxed_ordering */
	res->u.srq.dcaen_to_eqsize =
		cpu_to_be32(FW_RI_RES_WR_DCAEN_V(0) |
		FW_RI_RES_WR_DCACPU_V(0) |
		FW_RI_RES_WR_FBMIN_V(2) |
		FW_RI_RES_WR_FBMAX_V(3) |
		FW_RI_RES_WR_CIDXFTHRESHO_V(0) |
		FW_RI_RES_WR_CIDXFTHRESH_V(0) |
		FW_RI_RES_WR_EQSIZE_V(eqsize));
	res->u.srq.eqaddr = cpu_to_be64(wq->dma_addr);
	res->u.srq.srqid = cpu_to_be32(srq->idx);
	res->u.srq.pdid = cpu_to_be32(srq->pdid);
	res->u.srq.hwsrqsize = cpu_to_be32(wq->rqt_size);
	res->u.srq.hwsrqaddr = cpu_to_be32(wq->rqt_hwaddr -
			rdev->lldi.vr->rq.start);

	c4iw_init_wr_wait(wr_waitp);

	ret = c4iw_ref_send_wait(rdev, skb, wr_waitp, 0, wq->qid, __func__);
	if (ret)
		goto err_free_queue;

	pr_debug("%s srq %u eqid %u pdid %u queue va %p pa 0x%llx\n"
			" bar2_addr %p rqt addr 0x%x size %d\n",
			__func__, srq->idx, wq->qid, srq->pdid, wq->queue,
			(u64)virt_to_phys(wq->queue), wq->bar2_va,
			wq->rqt_hwaddr, wq->rqt_size);

	return 0;
err_free_queue:
	dma_free_coherent(&rdev->lldi.pdev->dev,
			  wq->memsize, wq->queue,
			dma_unmap_addr(wq, mapping));
err_free_rqtpool:
	c4iw_rqtpool_free(rdev, wq->rqt_hwaddr, wq->rqt_size);
err_free_pending_wrs:
	if (!user)
		kfree(wq->pending_wrs);
err_free_sw_rq:
	if (!user)
		kfree(wq->sw_rq);
err_put_qpid:
	c4iw_put_qpid(rdev, wq->qid, uctx);
err:
	return ret;
}

void c4iw_copy_wr_to_srq(struct t4_srq *srq, union t4_recv_wr *wqe, u8 len16)
{
	u64 *src, *dst;

	src = (u64 *)wqe;
	dst = (u64 *)((u8 *)srq->queue + srq->wq_pidx * T4_EQ_ENTRY_SIZE);
	while (len16) {
		*dst++ = *src++;
		if (dst >= (u64 *)&srq->queue[srq->size])
			dst = (u64 *)srq->queue;
		*dst++ = *src++;
		if (dst >= (u64 *)&srq->queue[srq->size])
			dst = (u64 *)srq->queue;
		len16--;
	}
}

int c4iw_create_srq(struct ib_srq *ib_srq, struct ib_srq_init_attr *attrs,
			       struct ib_udata *udata)
{
	struct ib_pd *pd = ib_srq->pd;
	struct c4iw_dev *rhp;
	struct c4iw_srq *srq = to_c4iw_srq(ib_srq);
	struct c4iw_pd *php;
	struct c4iw_create_srq_resp uresp;
	struct c4iw_ucontext *ucontext;
	struct c4iw_mm_entry *srq_key_mm, *srq_db_key_mm;
	int rqsize;
	int ret;
	int wr_len;

	if (attrs->srq_type != IB_SRQT_BASIC)
		return -EOPNOTSUPP;

	pr_debug("%s ib_pd %p\n", __func__, pd);

	php = to_c4iw_pd(pd);
	rhp = php->rhp;

	if (!rhp->rdev.lldi.vr->srq.size)
		return -EINVAL;
	if (attrs->attr.max_wr > rhp->rdev.hw_queue.t4_max_rq_size)
		return -E2BIG;
	if (attrs->attr.max_sge > T4_MAX_RECV_SGE)
		return -E2BIG;

	/*
	 * SRQ RQT and RQ must be a power of 2 and at least 16 deep.
	 */
	rqsize = attrs->attr.max_wr + 1;
	rqsize = roundup_pow_of_two(max_t(u16, rqsize, 16));

	ucontext = rdma_udata_to_drv_context(udata, struct c4iw_ucontext,
					     ibucontext);

	srq->wr_waitp = c4iw_alloc_wr_wait(GFP_KERNEL);
	if (!srq->wr_waitp)
		return -ENOMEM;

	srq->idx = c4iw_alloc_srq_idx(&rhp->rdev);
	if (srq->idx < 0) {
		ret = -ENOMEM;
		goto err_free_wr_wait;
	}

	wr_len = sizeof(struct fw_ri_res_wr) + sizeof(struct fw_ri_res);
	srq->destroy_skb = alloc_skb(wr_len, GFP_KERNEL);
	if (!srq->destroy_skb) {
		ret = -ENOMEM;
		goto err_free_srq_idx;
	}

	srq->rhp = rhp;
	srq->pdid = php->pdid;

	srq->wq.size = rqsize;
	srq->wq.memsize =
		(rqsize + rhp->rdev.hw_queue.t4_eq_status_entries) *
		sizeof(*srq->wq.queue);
	if (ucontext)
		srq->wq.memsize = roundup(srq->wq.memsize, PAGE_SIZE);

	ret = alloc_srq_queue(srq, ucontext ? &ucontext->uctx :
			&rhp->rdev.uctx, srq->wr_waitp);
	if (ret)
		goto err_free_skb;
	attrs->attr.max_wr = rqsize - 1;

	if (CHELSIO_CHIP_VERSION(rhp->rdev.lldi.adapter_type) > CHELSIO_T6)
		srq->flags = T4_SRQ_LIMIT_SUPPORT;

	if (udata) {
		srq_key_mm = kmalloc(sizeof(*srq_key_mm), GFP_KERNEL);
		if (!srq_key_mm) {
			ret = -ENOMEM;
			goto err_free_queue;
		}
		srq_db_key_mm = kmalloc(sizeof(*srq_db_key_mm), GFP_KERNEL);
		if (!srq_db_key_mm) {
			ret = -ENOMEM;
			goto err_free_srq_key_mm;
		}
		memset(&uresp, 0, sizeof(uresp));
		uresp.flags = srq->flags;
		uresp.qid_mask = rhp->rdev.qpmask;
		uresp.srqid = srq->wq.qid;
		uresp.srq_size = srq->wq.size;
		uresp.srq_memsize = srq->wq.memsize;
		uresp.rqt_abs_idx = srq->wq.rqt_abs_idx;
		spin_lock(&ucontext->mmap_lock);
		uresp.srq_key = ucontext->key;
		ucontext->key += PAGE_SIZE;
		uresp.srq_db_gts_key = ucontext->key;
		ucontext->key += PAGE_SIZE;
		spin_unlock(&ucontext->mmap_lock);
		ret = ib_copy_to_udata(udata, &uresp, sizeof(uresp));
		if (ret)
			goto err_free_srq_db_key_mm;
		srq_key_mm->key = uresp.srq_key;
		srq_key_mm->addr = virt_to_phys(srq->wq.queue);
		srq_key_mm->len = PAGE_ALIGN(srq->wq.memsize);
		insert_mmap(ucontext, srq_key_mm);
		srq_db_key_mm->key = uresp.srq_db_gts_key;
		srq_db_key_mm->addr = (u64)(unsigned long)srq->wq.bar2_pa;
		srq_db_key_mm->len = PAGE_SIZE;
		insert_mmap(ucontext, srq_db_key_mm);
	}

	pr_debug("%s srq qid %u idx %u size %u memsize %lu num_entries %u\n",
		 __func__, srq->wq.qid, srq->idx, srq->wq.size,
			(unsigned long)srq->wq.memsize, attrs->attr.max_wr);

	spin_lock_init(&srq->lock);
	return 0;

err_free_srq_db_key_mm:
	kfree(srq_db_key_mm);
err_free_srq_key_mm:
	kfree(srq_key_mm);
err_free_queue:
	free_srq_queue(srq, ucontext ? &ucontext->uctx : &rhp->rdev.uctx,
		       srq->wr_waitp);
err_free_skb:
	kfree_skb(srq->destroy_skb);
err_free_srq_idx:
	c4iw_free_srq_idx(&rhp->rdev, srq->idx);
err_free_wr_wait:
	c4iw_put_wr_wait(srq->wr_waitp);
	return ret;
}

int c4iw_destroy_srq(struct ib_srq *ibsrq, struct ib_udata *udata)
{
	struct c4iw_dev *rhp;
	struct c4iw_srq *srq;
	struct c4iw_ucontext *ucontext;

	srq = to_c4iw_srq(ibsrq);
	rhp = srq->rhp;

	pr_debug("%s id %d\n", __func__, srq->wq.qid);
	ucontext = rdma_udata_to_drv_context(udata, struct c4iw_ucontext,
					     ibucontext);
	free_srq_queue(srq, ucontext ? &ucontext->uctx : &rhp->rdev.uctx,
		       srq->wr_waitp);
	c4iw_free_srq_idx(&rhp->rdev, srq->idx);
	c4iw_put_wr_wait(srq->wr_waitp);
	return 0;
}
