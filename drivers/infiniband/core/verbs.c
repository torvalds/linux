/*
 * Copyright (c) 2004 Mellanox Technologies Ltd.  All rights reserved.
 * Copyright (c) 2004 Infinicon Corporation.  All rights reserved.
 * Copyright (c) 2004 Intel Corporation.  All rights reserved.
 * Copyright (c) 2004 Topspin Corporation.  All rights reserved.
 * Copyright (c) 2004 Voltaire Corporation.  All rights reserved.
 * Copyright (c) 2005 Sun Microsystems, Inc. All rights reserved.
 * Copyright (c) 2005 Cisco Systems.  All rights reserved.
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
 *
 * $Id: verbs.c 1349 2004-12-16 21:09:43Z roland $
 */

#include <linux/errno.h>
#include <linux/err.h>

#include <rdma/ib_verbs.h>
#include <rdma/ib_cache.h>

/* Protection domains */

struct ib_pd *ib_alloc_pd(struct ib_device *device)
{
	struct ib_pd *pd;

	pd = device->alloc_pd(device, NULL, NULL);

	if (!IS_ERR(pd)) {
		pd->device  = device;
		pd->uobject = NULL;
		atomic_set(&pd->usecnt, 0);
	}

	return pd;
}
EXPORT_SYMBOL(ib_alloc_pd);

int ib_dealloc_pd(struct ib_pd *pd)
{
	if (atomic_read(&pd->usecnt))
		return -EBUSY;

	return pd->device->dealloc_pd(pd);
}
EXPORT_SYMBOL(ib_dealloc_pd);

/* Address handles */

struct ib_ah *ib_create_ah(struct ib_pd *pd, struct ib_ah_attr *ah_attr)
{
	struct ib_ah *ah;

	ah = pd->device->create_ah(pd, ah_attr);

	if (!IS_ERR(ah)) {
		ah->device  = pd->device;
		ah->pd      = pd;
		ah->uobject = NULL;
		atomic_inc(&pd->usecnt);
	}

	return ah;
}
EXPORT_SYMBOL(ib_create_ah);

struct ib_ah *ib_create_ah_from_wc(struct ib_pd *pd, struct ib_wc *wc,
				   struct ib_grh *grh, u8 port_num)
{
	struct ib_ah_attr ah_attr;
	u32 flow_class;
	u16 gid_index;
	int ret;

	memset(&ah_attr, 0, sizeof ah_attr);
	ah_attr.dlid = wc->slid;
	ah_attr.sl = wc->sl;
	ah_attr.src_path_bits = wc->dlid_path_bits;
	ah_attr.port_num = port_num;

	if (wc->wc_flags & IB_WC_GRH) {
		ah_attr.ah_flags = IB_AH_GRH;
		ah_attr.grh.dgid = grh->dgid;

		ret = ib_find_cached_gid(pd->device, &grh->sgid, &port_num,
					 &gid_index);
		if (ret)
			return ERR_PTR(ret);

		ah_attr.grh.sgid_index = (u8) gid_index;
		flow_class = be32_to_cpu(grh->version_tclass_flow);
		ah_attr.grh.flow_label = flow_class & 0xFFFFF;
		ah_attr.grh.traffic_class = (flow_class >> 20) & 0xFF;
		ah_attr.grh.hop_limit = grh->hop_limit;
	}

	return ib_create_ah(pd, &ah_attr);
}
EXPORT_SYMBOL(ib_create_ah_from_wc);

int ib_modify_ah(struct ib_ah *ah, struct ib_ah_attr *ah_attr)
{
	return ah->device->modify_ah ?
		ah->device->modify_ah(ah, ah_attr) :
		-ENOSYS;
}
EXPORT_SYMBOL(ib_modify_ah);

int ib_query_ah(struct ib_ah *ah, struct ib_ah_attr *ah_attr)
{
	return ah->device->query_ah ?
		ah->device->query_ah(ah, ah_attr) :
		-ENOSYS;
}
EXPORT_SYMBOL(ib_query_ah);

int ib_destroy_ah(struct ib_ah *ah)
{
	struct ib_pd *pd;
	int ret;

	pd = ah->pd;
	ret = ah->device->destroy_ah(ah);
	if (!ret)
		atomic_dec(&pd->usecnt);

	return ret;
}
EXPORT_SYMBOL(ib_destroy_ah);

/* Shared receive queues */

struct ib_srq *ib_create_srq(struct ib_pd *pd,
			     struct ib_srq_init_attr *srq_init_attr)
{
	struct ib_srq *srq;

	if (!pd->device->create_srq)
		return ERR_PTR(-ENOSYS);

	srq = pd->device->create_srq(pd, srq_init_attr, NULL);

	if (!IS_ERR(srq)) {
		srq->device    	   = pd->device;
		srq->pd        	   = pd;
		srq->uobject       = NULL;
		srq->event_handler = srq_init_attr->event_handler;
		srq->srq_context   = srq_init_attr->srq_context;
		atomic_inc(&pd->usecnt);
		atomic_set(&srq->usecnt, 0);
	}

	return srq;
}
EXPORT_SYMBOL(ib_create_srq);

int ib_modify_srq(struct ib_srq *srq,
		  struct ib_srq_attr *srq_attr,
		  enum ib_srq_attr_mask srq_attr_mask)
{
	return srq->device->modify_srq(srq, srq_attr, srq_attr_mask);
}
EXPORT_SYMBOL(ib_modify_srq);

int ib_query_srq(struct ib_srq *srq,
		 struct ib_srq_attr *srq_attr)
{
	return srq->device->query_srq ?
		srq->device->query_srq(srq, srq_attr) : -ENOSYS;
}
EXPORT_SYMBOL(ib_query_srq);

int ib_destroy_srq(struct ib_srq *srq)
{
	struct ib_pd *pd;
	int ret;

	if (atomic_read(&srq->usecnt))
		return -EBUSY;

	pd = srq->pd;

	ret = srq->device->destroy_srq(srq);
	if (!ret)
		atomic_dec(&pd->usecnt);

	return ret;
}
EXPORT_SYMBOL(ib_destroy_srq);

/* Queue pairs */

struct ib_qp *ib_create_qp(struct ib_pd *pd,
			   struct ib_qp_init_attr *qp_init_attr)
{
	struct ib_qp *qp;

	qp = pd->device->create_qp(pd, qp_init_attr, NULL);

	if (!IS_ERR(qp)) {
		qp->device     	  = pd->device;
		qp->pd         	  = pd;
		qp->send_cq    	  = qp_init_attr->send_cq;
		qp->recv_cq    	  = qp_init_attr->recv_cq;
		qp->srq	       	  = qp_init_attr->srq;
		qp->uobject       = NULL;
		qp->event_handler = qp_init_attr->event_handler;
		qp->qp_context    = qp_init_attr->qp_context;
		qp->qp_type	  = qp_init_attr->qp_type;
		atomic_inc(&pd->usecnt);
		atomic_inc(&qp_init_attr->send_cq->usecnt);
		atomic_inc(&qp_init_attr->recv_cq->usecnt);
		if (qp_init_attr->srq)
			atomic_inc(&qp_init_attr->srq->usecnt);
	}

	return qp;
}
EXPORT_SYMBOL(ib_create_qp);

int ib_modify_qp(struct ib_qp *qp,
		 struct ib_qp_attr *qp_attr,
		 int qp_attr_mask)
{
	return qp->device->modify_qp(qp, qp_attr, qp_attr_mask);
}
EXPORT_SYMBOL(ib_modify_qp);

int ib_query_qp(struct ib_qp *qp,
		struct ib_qp_attr *qp_attr,
		int qp_attr_mask,
		struct ib_qp_init_attr *qp_init_attr)
{
	return qp->device->query_qp ?
		qp->device->query_qp(qp, qp_attr, qp_attr_mask, qp_init_attr) :
		-ENOSYS;
}
EXPORT_SYMBOL(ib_query_qp);

int ib_destroy_qp(struct ib_qp *qp)
{
	struct ib_pd *pd;
	struct ib_cq *scq, *rcq;
	struct ib_srq *srq;
	int ret;

	pd  = qp->pd;
	scq = qp->send_cq;
	rcq = qp->recv_cq;
	srq = qp->srq;

	ret = qp->device->destroy_qp(qp);
	if (!ret) {
		atomic_dec(&pd->usecnt);
		atomic_dec(&scq->usecnt);
		atomic_dec(&rcq->usecnt);
		if (srq)
			atomic_dec(&srq->usecnt);
	}

	return ret;
}
EXPORT_SYMBOL(ib_destroy_qp);

/* Completion queues */

struct ib_cq *ib_create_cq(struct ib_device *device,
			   ib_comp_handler comp_handler,
			   void (*event_handler)(struct ib_event *, void *),
			   void *cq_context, int cqe)
{
	struct ib_cq *cq;

	cq = device->create_cq(device, cqe, NULL, NULL);

	if (!IS_ERR(cq)) {
		cq->device        = device;
		cq->uobject       = NULL;
		cq->comp_handler  = comp_handler;
		cq->event_handler = event_handler;
		cq->cq_context    = cq_context;
		atomic_set(&cq->usecnt, 0);
	}

	return cq;
}
EXPORT_SYMBOL(ib_create_cq);

int ib_destroy_cq(struct ib_cq *cq)
{
	if (atomic_read(&cq->usecnt))
		return -EBUSY;

	return cq->device->destroy_cq(cq);
}
EXPORT_SYMBOL(ib_destroy_cq);

int ib_resize_cq(struct ib_cq *cq,
                 int           cqe)
{
	int ret;

	if (!cq->device->resize_cq)
		return -ENOSYS;

	ret = cq->device->resize_cq(cq, &cqe);
	if (!ret)
		cq->cqe = cqe;

	return ret;
}
EXPORT_SYMBOL(ib_resize_cq);

/* Memory regions */

struct ib_mr *ib_get_dma_mr(struct ib_pd *pd, int mr_access_flags)
{
	struct ib_mr *mr;

	mr = pd->device->get_dma_mr(pd, mr_access_flags);

	if (!IS_ERR(mr)) {
		mr->device  = pd->device;
		mr->pd      = pd;
		mr->uobject = NULL;
		atomic_inc(&pd->usecnt);
		atomic_set(&mr->usecnt, 0);
	}

	return mr;
}
EXPORT_SYMBOL(ib_get_dma_mr);

struct ib_mr *ib_reg_phys_mr(struct ib_pd *pd,
			     struct ib_phys_buf *phys_buf_array,
			     int num_phys_buf,
			     int mr_access_flags,
			     u64 *iova_start)
{
	struct ib_mr *mr;

	mr = pd->device->reg_phys_mr(pd, phys_buf_array, num_phys_buf,
				     mr_access_flags, iova_start);

	if (!IS_ERR(mr)) {
		mr->device  = pd->device;
		mr->pd      = pd;
		mr->uobject = NULL;
		atomic_inc(&pd->usecnt);
		atomic_set(&mr->usecnt, 0);
	}

	return mr;
}
EXPORT_SYMBOL(ib_reg_phys_mr);

int ib_rereg_phys_mr(struct ib_mr *mr,
		     int mr_rereg_mask,
		     struct ib_pd *pd,
		     struct ib_phys_buf *phys_buf_array,
		     int num_phys_buf,
		     int mr_access_flags,
		     u64 *iova_start)
{
	struct ib_pd *old_pd;
	int ret;

	if (!mr->device->rereg_phys_mr)
		return -ENOSYS;

	if (atomic_read(&mr->usecnt))
		return -EBUSY;

	old_pd = mr->pd;

	ret = mr->device->rereg_phys_mr(mr, mr_rereg_mask, pd,
					phys_buf_array, num_phys_buf,
					mr_access_flags, iova_start);

	if (!ret && (mr_rereg_mask & IB_MR_REREG_PD)) {
		atomic_dec(&old_pd->usecnt);
		atomic_inc(&pd->usecnt);
	}

	return ret;
}
EXPORT_SYMBOL(ib_rereg_phys_mr);

int ib_query_mr(struct ib_mr *mr, struct ib_mr_attr *mr_attr)
{
	return mr->device->query_mr ?
		mr->device->query_mr(mr, mr_attr) : -ENOSYS;
}
EXPORT_SYMBOL(ib_query_mr);

int ib_dereg_mr(struct ib_mr *mr)
{
	struct ib_pd *pd;
	int ret;

	if (atomic_read(&mr->usecnt))
		return -EBUSY;

	pd = mr->pd;
	ret = mr->device->dereg_mr(mr);
	if (!ret)
		atomic_dec(&pd->usecnt);

	return ret;
}
EXPORT_SYMBOL(ib_dereg_mr);

/* Memory windows */

struct ib_mw *ib_alloc_mw(struct ib_pd *pd)
{
	struct ib_mw *mw;

	if (!pd->device->alloc_mw)
		return ERR_PTR(-ENOSYS);

	mw = pd->device->alloc_mw(pd);
	if (!IS_ERR(mw)) {
		mw->device  = pd->device;
		mw->pd      = pd;
		mw->uobject = NULL;
		atomic_inc(&pd->usecnt);
	}

	return mw;
}
EXPORT_SYMBOL(ib_alloc_mw);

int ib_dealloc_mw(struct ib_mw *mw)
{
	struct ib_pd *pd;
	int ret;

	pd = mw->pd;
	ret = mw->device->dealloc_mw(mw);
	if (!ret)
		atomic_dec(&pd->usecnt);

	return ret;
}
EXPORT_SYMBOL(ib_dealloc_mw);

/* "Fast" memory regions */

struct ib_fmr *ib_alloc_fmr(struct ib_pd *pd,
			    int mr_access_flags,
			    struct ib_fmr_attr *fmr_attr)
{
	struct ib_fmr *fmr;

	if (!pd->device->alloc_fmr)
		return ERR_PTR(-ENOSYS);

	fmr = pd->device->alloc_fmr(pd, mr_access_flags, fmr_attr);
	if (!IS_ERR(fmr)) {
		fmr->device = pd->device;
		fmr->pd     = pd;
		atomic_inc(&pd->usecnt);
	}

	return fmr;
}
EXPORT_SYMBOL(ib_alloc_fmr);

int ib_unmap_fmr(struct list_head *fmr_list)
{
	struct ib_fmr *fmr;

	if (list_empty(fmr_list))
		return 0;

	fmr = list_entry(fmr_list->next, struct ib_fmr, list);
	return fmr->device->unmap_fmr(fmr_list);
}
EXPORT_SYMBOL(ib_unmap_fmr);

int ib_dealloc_fmr(struct ib_fmr *fmr)
{
	struct ib_pd *pd;
	int ret;

	pd = fmr->pd;
	ret = fmr->device->dealloc_fmr(fmr);
	if (!ret)
		atomic_dec(&pd->usecnt);

	return ret;
}
EXPORT_SYMBOL(ib_dealloc_fmr);

/* Multicast groups */

int ib_attach_mcast(struct ib_qp *qp, union ib_gid *gid, u16 lid)
{
	if (!qp->device->attach_mcast)
		return -ENOSYS;
	if (gid->raw[0] != 0xff || qp->qp_type != IB_QPT_UD)
		return -EINVAL;

	return qp->device->attach_mcast(qp, gid, lid);
}
EXPORT_SYMBOL(ib_attach_mcast);

int ib_detach_mcast(struct ib_qp *qp, union ib_gid *gid, u16 lid)
{
	if (!qp->device->detach_mcast)
		return -ENOSYS;
	if (gid->raw[0] != 0xff || qp->qp_type != IB_QPT_UD)
		return -EINVAL;

	return qp->device->detach_mcast(qp, gid, lid);
}
EXPORT_SYMBOL(ib_detach_mcast);
