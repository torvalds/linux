/*
 *  IBM eServer eHCA Infiniband device driver for Linux on POWER
 *
 *  QP functions
 *
 *  Authors: Joachim Fenkes <fenkes@de.ibm.com>
 *           Stefan Roscher <stefan.roscher@de.ibm.com>
 *           Waleri Fomin <fomin@de.ibm.com>
 *           Hoang-Nam Nguyen <hnguyen@de.ibm.com>
 *           Reinhard Ernst <rernst@de.ibm.com>
 *           Heiko J Schick <schickhj@de.ibm.com>
 *
 *  Copyright (c) 2005 IBM Corporation
 *
 *  All rights reserved.
 *
 *  This source code is distributed under a dual license of GPL v2.0 and OpenIB
 *  BSD.
 *
 * OpenIB BSD License
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials
 * provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */


#include <asm/current.h>

#include "ehca_classes.h"
#include "ehca_tools.h"
#include "ehca_qes.h"
#include "ehca_iverbs.h"
#include "hcp_if.h"
#include "hipz_fns.h"

static struct kmem_cache *qp_cache;

/*
 * attributes not supported by query qp
 */
#define QP_ATTR_QUERY_NOT_SUPPORTED (IB_QP_MAX_DEST_RD_ATOMIC | \
				     IB_QP_MAX_QP_RD_ATOMIC   | \
				     IB_QP_ACCESS_FLAGS       | \
				     IB_QP_EN_SQD_ASYNC_NOTIFY)

/*
 * ehca (internal) qp state values
 */
enum ehca_qp_state {
	EHCA_QPS_RESET = 1,
	EHCA_QPS_INIT = 2,
	EHCA_QPS_RTR = 3,
	EHCA_QPS_RTS = 5,
	EHCA_QPS_SQD = 6,
	EHCA_QPS_SQE = 8,
	EHCA_QPS_ERR = 128
};

/*
 * qp state transitions as defined by IB Arch Rel 1.1 page 431
 */
enum ib_qp_statetrans {
	IB_QPST_ANY2RESET,
	IB_QPST_ANY2ERR,
	IB_QPST_RESET2INIT,
	IB_QPST_INIT2RTR,
	IB_QPST_INIT2INIT,
	IB_QPST_RTR2RTS,
	IB_QPST_RTS2SQD,
	IB_QPST_RTS2RTS,
	IB_QPST_SQD2RTS,
	IB_QPST_SQE2RTS,
	IB_QPST_SQD2SQD,
	IB_QPST_MAX	/* nr of transitions, this must be last!!! */
};

/*
 * ib2ehca_qp_state maps IB to ehca qp_state
 * returns ehca qp state corresponding to given ib qp state
 */
static inline enum ehca_qp_state ib2ehca_qp_state(enum ib_qp_state ib_qp_state)
{
	switch (ib_qp_state) {
	case IB_QPS_RESET:
		return EHCA_QPS_RESET;
	case IB_QPS_INIT:
		return EHCA_QPS_INIT;
	case IB_QPS_RTR:
		return EHCA_QPS_RTR;
	case IB_QPS_RTS:
		return EHCA_QPS_RTS;
	case IB_QPS_SQD:
		return EHCA_QPS_SQD;
	case IB_QPS_SQE:
		return EHCA_QPS_SQE;
	case IB_QPS_ERR:
		return EHCA_QPS_ERR;
	default:
		ehca_gen_err("invalid ib_qp_state=%x", ib_qp_state);
		return -EINVAL;
	}
}

/*
 * ehca2ib_qp_state maps ehca to IB qp_state
 * returns ib qp state corresponding to given ehca qp state
 */
static inline enum ib_qp_state ehca2ib_qp_state(enum ehca_qp_state
						ehca_qp_state)
{
	switch (ehca_qp_state) {
	case EHCA_QPS_RESET:
		return IB_QPS_RESET;
	case EHCA_QPS_INIT:
		return IB_QPS_INIT;
	case EHCA_QPS_RTR:
		return IB_QPS_RTR;
	case EHCA_QPS_RTS:
		return IB_QPS_RTS;
	case EHCA_QPS_SQD:
		return IB_QPS_SQD;
	case EHCA_QPS_SQE:
		return IB_QPS_SQE;
	case EHCA_QPS_ERR:
		return IB_QPS_ERR;
	default:
		ehca_gen_err("invalid ehca_qp_state=%x", ehca_qp_state);
		return -EINVAL;
	}
}

/*
 * ehca_qp_type used as index for req_attr and opt_attr of
 * struct ehca_modqp_statetrans
 */
enum ehca_qp_type {
	QPT_RC = 0,
	QPT_UC = 1,
	QPT_UD = 2,
	QPT_SQP = 3,
	QPT_MAX
};

/*
 * ib2ehcaqptype maps Ib to ehca qp_type
 * returns ehca qp type corresponding to ib qp type
 */
static inline enum ehca_qp_type ib2ehcaqptype(enum ib_qp_type ibqptype)
{
	switch (ibqptype) {
	case IB_QPT_SMI:
	case IB_QPT_GSI:
		return QPT_SQP;
	case IB_QPT_RC:
		return QPT_RC;
	case IB_QPT_UC:
		return QPT_UC;
	case IB_QPT_UD:
		return QPT_UD;
	default:
		ehca_gen_err("Invalid ibqptype=%x", ibqptype);
		return -EINVAL;
	}
}

static inline enum ib_qp_statetrans get_modqp_statetrans(int ib_fromstate,
							 int ib_tostate)
{
	int index = -EINVAL;
	switch (ib_tostate) {
	case IB_QPS_RESET:
		index = IB_QPST_ANY2RESET;
		break;
	case IB_QPS_INIT:
		switch (ib_fromstate) {
		case IB_QPS_RESET:
			index = IB_QPST_RESET2INIT;
			break;
		case IB_QPS_INIT:
			index = IB_QPST_INIT2INIT;
			break;
		}
		break;
	case IB_QPS_RTR:
		if (ib_fromstate == IB_QPS_INIT)
			index = IB_QPST_INIT2RTR;
		break;
	case IB_QPS_RTS:
		switch (ib_fromstate) {
		case IB_QPS_RTR:
			index = IB_QPST_RTR2RTS;
			break;
		case IB_QPS_RTS:
			index = IB_QPST_RTS2RTS;
			break;
		case IB_QPS_SQD:
			index = IB_QPST_SQD2RTS;
			break;
		case IB_QPS_SQE:
			index = IB_QPST_SQE2RTS;
			break;
		}
		break;
	case IB_QPS_SQD:
		if (ib_fromstate == IB_QPS_RTS)
			index = IB_QPST_RTS2SQD;
		break;
	case IB_QPS_SQE:
		break;
	case IB_QPS_ERR:
		index = IB_QPST_ANY2ERR;
		break;
	default:
		break;
	}
	return index;
}

/*
 * ibqptype2servicetype returns hcp service type corresponding to given
 * ib qp type used by create_qp()
 */
static inline int ibqptype2servicetype(enum ib_qp_type ibqptype)
{
	switch (ibqptype) {
	case IB_QPT_SMI:
	case IB_QPT_GSI:
		return ST_UD;
	case IB_QPT_RC:
		return ST_RC;
	case IB_QPT_UC:
		return ST_UC;
	case IB_QPT_UD:
		return ST_UD;
	case IB_QPT_RAW_IPV6:
		return -EINVAL;
	case IB_QPT_RAW_ETY:
		return -EINVAL;
	default:
		ehca_gen_err("Invalid ibqptype=%x", ibqptype);
		return -EINVAL;
	}
}

/*
 * init userspace queue info from ipz_queue data
 */
static inline void queue2resp(struct ipzu_queue_resp *resp,
			      struct ipz_queue *queue)
{
	resp->qe_size = queue->qe_size;
	resp->act_nr_of_sg = queue->act_nr_of_sg;
	resp->queue_length = queue->queue_length;
	resp->pagesize = queue->pagesize;
	resp->toggle_state = queue->toggle_state;
	resp->offset = queue->offset;
}

/*
 * init_qp_queue initializes/constructs r/squeue and registers queue pages.
 */
static inline int init_qp_queue(struct ehca_shca *shca,
				struct ehca_pd *pd,
				struct ehca_qp *my_qp,
				struct ipz_queue *queue,
				int q_type,
				u64 expected_hret,
				struct ehca_alloc_queue_parms *parms,
				int wqe_size)
{
	int ret, cnt, ipz_rc, nr_q_pages;
	void *vpage;
	u64 rpage, h_ret;
	struct ib_device *ib_dev = &shca->ib_device;
	struct ipz_adapter_handle ipz_hca_handle = shca->ipz_hca_handle;

	if (!parms->queue_size)
		return 0;

	if (parms->is_small) {
		nr_q_pages = 1;
		ipz_rc = ipz_queue_ctor(pd, queue, nr_q_pages,
					128 << parms->page_size,
					wqe_size, parms->act_nr_sges, 1);
	} else {
		nr_q_pages = parms->queue_size;
		ipz_rc = ipz_queue_ctor(pd, queue, nr_q_pages,
					EHCA_PAGESIZE, wqe_size,
					parms->act_nr_sges, 0);
	}

	if (!ipz_rc) {
		ehca_err(ib_dev, "Cannot allocate page for queue. ipz_rc=%i",
			 ipz_rc);
		return -EBUSY;
	}

	/* register queue pages */
	for (cnt = 0; cnt < nr_q_pages; cnt++) {
		vpage = ipz_qpageit_get_inc(queue);
		if (!vpage) {
			ehca_err(ib_dev, "ipz_qpageit_get_inc() "
				 "failed p_vpage= %p", vpage);
			ret = -EINVAL;
			goto init_qp_queue1;
		}
		rpage = virt_to_abs(vpage);

		h_ret = hipz_h_register_rpage_qp(ipz_hca_handle,
						 my_qp->ipz_qp_handle,
						 NULL, 0, q_type,
						 rpage, parms->is_small ? 0 : 1,
						 my_qp->galpas.kernel);
		if (cnt == (nr_q_pages - 1)) {	/* last page! */
			if (h_ret != expected_hret) {
				ehca_err(ib_dev, "hipz_qp_register_rpage() "
					 "h_ret=%li", h_ret);
				ret = ehca2ib_return_code(h_ret);
				goto init_qp_queue1;
			}
			vpage = ipz_qpageit_get_inc(&my_qp->ipz_rqueue);
			if (vpage) {
				ehca_err(ib_dev, "ipz_qpageit_get_inc() "
					 "should not succeed vpage=%p", vpage);
				ret = -EINVAL;
				goto init_qp_queue1;
			}
		} else {
			if (h_ret != H_PAGE_REGISTERED) {
				ehca_err(ib_dev, "hipz_qp_register_rpage() "
					 "h_ret=%li", h_ret);
				ret = ehca2ib_return_code(h_ret);
				goto init_qp_queue1;
			}
		}
	}

	ipz_qeit_reset(queue);

	return 0;

init_qp_queue1:
	ipz_queue_dtor(pd, queue);
	return ret;
}

static inline int ehca_calc_wqe_size(int act_nr_sge, int is_llqp)
{
	if (is_llqp)
		return 128 << act_nr_sge;
	else
		return offsetof(struct ehca_wqe,
				u.nud.sg_list[act_nr_sge]);
}

static void ehca_determine_small_queue(struct ehca_alloc_queue_parms *queue,
				       int req_nr_sge, int is_llqp)
{
	u32 wqe_size, q_size;
	int act_nr_sge = req_nr_sge;

	if (!is_llqp)
		/* round up #SGEs so WQE size is a power of 2 */
		for (act_nr_sge = 4; act_nr_sge <= 252;
		     act_nr_sge = 4 + 2 * act_nr_sge)
			if (act_nr_sge >= req_nr_sge)
				break;

	wqe_size = ehca_calc_wqe_size(act_nr_sge, is_llqp);
	q_size = wqe_size * (queue->max_wr + 1);

	if (q_size <= 512)
		queue->page_size = 2;
	else if (q_size <= 1024)
		queue->page_size = 3;
	else
		queue->page_size = 0;

	queue->is_small = (queue->page_size != 0);
}

/*
 * Create an ib_qp struct that is either a QP or an SRQ, depending on
 * the value of the is_srq parameter. If init_attr and srq_init_attr share
 * fields, the field out of init_attr is used.
 */
static struct ehca_qp *internal_create_qp(
	struct ib_pd *pd,
	struct ib_qp_init_attr *init_attr,
	struct ib_srq_init_attr *srq_init_attr,
	struct ib_udata *udata, int is_srq)
{
	struct ehca_qp *my_qp;
	struct ehca_pd *my_pd = container_of(pd, struct ehca_pd, ib_pd);
	struct ehca_shca *shca = container_of(pd->device, struct ehca_shca,
					      ib_device);
	struct ib_ucontext *context = NULL;
	u64 h_ret;
	int is_llqp = 0, has_srq = 0;
	int qp_type, max_send_sge, max_recv_sge, ret;

	/* h_call's out parameters */
	struct ehca_alloc_qp_parms parms;
	u32 swqe_size = 0, rwqe_size = 0, ib_qp_num;
	unsigned long flags;

	memset(&parms, 0, sizeof(parms));
	qp_type = init_attr->qp_type;

	if (init_attr->sq_sig_type != IB_SIGNAL_REQ_WR &&
		init_attr->sq_sig_type != IB_SIGNAL_ALL_WR) {
		ehca_err(pd->device, "init_attr->sg_sig_type=%x not allowed",
			 init_attr->sq_sig_type);
		return ERR_PTR(-EINVAL);
	}

	/* save LLQP info */
	if (qp_type & 0x80) {
		is_llqp = 1;
		parms.ext_type = EQPT_LLQP;
		parms.ll_comp_flags = qp_type & LLQP_COMP_MASK;
	}
	qp_type &= 0x1F;
	init_attr->qp_type &= 0x1F;

	/* handle SRQ base QPs */
	if (init_attr->srq) {
		struct ehca_qp *my_srq =
			container_of(init_attr->srq, struct ehca_qp, ib_srq);

		has_srq = 1;
		parms.ext_type = EQPT_SRQBASE;
		parms.srq_qpn = my_srq->real_qp_num;
	}

	if (is_llqp && has_srq) {
		ehca_err(pd->device, "LLQPs can't have an SRQ");
		return ERR_PTR(-EINVAL);
	}

	/* handle SRQs */
	if (is_srq) {
		parms.ext_type = EQPT_SRQ;
		parms.srq_limit = srq_init_attr->attr.srq_limit;
		if (init_attr->cap.max_recv_sge > 3) {
			ehca_err(pd->device, "no more than three SGEs "
				 "supported for SRQ  pd=%p  max_sge=%x",
				 pd, init_attr->cap.max_recv_sge);
			return ERR_PTR(-EINVAL);
		}
	}

	/* check QP type */
	if (qp_type != IB_QPT_UD &&
	    qp_type != IB_QPT_UC &&
	    qp_type != IB_QPT_RC &&
	    qp_type != IB_QPT_SMI &&
	    qp_type != IB_QPT_GSI) {
		ehca_err(pd->device, "wrong QP Type=%x", qp_type);
		return ERR_PTR(-EINVAL);
	}

	if (is_llqp) {
		switch (qp_type) {
		case IB_QPT_RC:
			if ((init_attr->cap.max_send_wr > 255) ||
			    (init_attr->cap.max_recv_wr > 255)) {
				ehca_err(pd->device,
					 "Invalid Number of max_sq_wr=%x "
					 "or max_rq_wr=%x for RC LLQP",
					 init_attr->cap.max_send_wr,
					 init_attr->cap.max_recv_wr);
				return ERR_PTR(-EINVAL);
			}
			break;
		case IB_QPT_UD:
			if (!EHCA_BMASK_GET(HCA_CAP_UD_LL_QP, shca->hca_cap)) {
				ehca_err(pd->device, "UD LLQP not supported "
					 "by this adapter");
				return ERR_PTR(-ENOSYS);
			}
			if (!(init_attr->cap.max_send_sge <= 5
			    && init_attr->cap.max_send_sge >= 1
			    && init_attr->cap.max_recv_sge <= 5
			    && init_attr->cap.max_recv_sge >= 1)) {
				ehca_err(pd->device,
					 "Invalid Number of max_send_sge=%x "
					 "or max_recv_sge=%x for UD LLQP",
					 init_attr->cap.max_send_sge,
					 init_attr->cap.max_recv_sge);
				return ERR_PTR(-EINVAL);
			} else if (init_attr->cap.max_send_wr > 255) {
				ehca_err(pd->device,
					 "Invalid Number of "
					 "max_send_wr=%x for UD QP_TYPE=%x",
					 init_attr->cap.max_send_wr, qp_type);
				return ERR_PTR(-EINVAL);
			}
			break;
		default:
			ehca_err(pd->device, "unsupported LL QP Type=%x",
				 qp_type);
			return ERR_PTR(-EINVAL);
			break;
		}
	} else {
		int max_sge = (qp_type == IB_QPT_UD || qp_type == IB_QPT_SMI
			       || qp_type == IB_QPT_GSI) ? 250 : 252;

		if (init_attr->cap.max_send_sge > max_sge
		    || init_attr->cap.max_recv_sge > max_sge) {
			ehca_err(pd->device, "Invalid number of SGEs requested "
				 "send_sge=%x recv_sge=%x max_sge=%x",
				 init_attr->cap.max_send_sge,
				 init_attr->cap.max_recv_sge, max_sge);
			return ERR_PTR(-EINVAL);
		}
	}

	if (pd->uobject && udata)
		context = pd->uobject->context;

	my_qp = kmem_cache_zalloc(qp_cache, GFP_KERNEL);
	if (!my_qp) {
		ehca_err(pd->device, "pd=%p not enough memory to alloc qp", pd);
		return ERR_PTR(-ENOMEM);
	}

	spin_lock_init(&my_qp->spinlock_s);
	spin_lock_init(&my_qp->spinlock_r);
	my_qp->qp_type = qp_type;
	my_qp->ext_type = parms.ext_type;

	if (init_attr->recv_cq)
		my_qp->recv_cq =
			container_of(init_attr->recv_cq, struct ehca_cq, ib_cq);
	if (init_attr->send_cq)
		my_qp->send_cq =
			container_of(init_attr->send_cq, struct ehca_cq, ib_cq);

	do {
		if (!idr_pre_get(&ehca_qp_idr, GFP_KERNEL)) {
			ret = -ENOMEM;
			ehca_err(pd->device, "Can't reserve idr resources.");
			goto create_qp_exit0;
		}

		write_lock_irqsave(&ehca_qp_idr_lock, flags);
		ret = idr_get_new(&ehca_qp_idr, my_qp, &my_qp->token);
		write_unlock_irqrestore(&ehca_qp_idr_lock, flags);
	} while (ret == -EAGAIN);

	if (ret) {
		ret = -ENOMEM;
		ehca_err(pd->device, "Can't allocate new idr entry.");
		goto create_qp_exit0;
	}

	if (my_qp->token > 0x1FFFFFF) {
		ret = -EINVAL;
		ehca_err(pd->device, "Invalid number of qp");
		goto create_qp_exit1;
	}

	if (has_srq)
		parms.srq_token = my_qp->token;

	parms.servicetype = ibqptype2servicetype(qp_type);
	if (parms.servicetype < 0) {
		ret = -EINVAL;
		ehca_err(pd->device, "Invalid qp_type=%x", qp_type);
		goto create_qp_exit1;
	}

	/* Always signal by WQE so we can hide circ. WQEs */
	parms.sigtype = HCALL_SIGT_BY_WQE;

	/* UD_AV CIRCUMVENTION */
	max_send_sge = init_attr->cap.max_send_sge;
	max_recv_sge = init_attr->cap.max_recv_sge;
	if (parms.servicetype == ST_UD && !is_llqp) {
		max_send_sge += 2;
		max_recv_sge += 2;
	}

	parms.token = my_qp->token;
	parms.eq_handle = shca->eq.ipz_eq_handle;
	parms.pd = my_pd->fw_pd;
	if (my_qp->send_cq)
		parms.send_cq_handle = my_qp->send_cq->ipz_cq_handle;
	if (my_qp->recv_cq)
		parms.recv_cq_handle = my_qp->recv_cq->ipz_cq_handle;

	parms.squeue.max_wr = init_attr->cap.max_send_wr;
	parms.rqueue.max_wr = init_attr->cap.max_recv_wr;
	parms.squeue.max_sge = max_send_sge;
	parms.rqueue.max_sge = max_recv_sge;

	/* RC QPs need one more SWQE for unsolicited ack circumvention */
	if (qp_type == IB_QPT_RC)
		parms.squeue.max_wr++;

	if (EHCA_BMASK_GET(HCA_CAP_MINI_QP, shca->hca_cap)) {
		if (HAS_SQ(my_qp))
			ehca_determine_small_queue(
				&parms.squeue, max_send_sge, is_llqp);
		if (HAS_RQ(my_qp))
			ehca_determine_small_queue(
				&parms.rqueue, max_recv_sge, is_llqp);
		parms.qp_storage =
			(parms.squeue.is_small || parms.rqueue.is_small);
	}

	h_ret = hipz_h_alloc_resource_qp(shca->ipz_hca_handle, &parms);
	if (h_ret != H_SUCCESS) {
		ehca_err(pd->device, "h_alloc_resource_qp() failed h_ret=%li",
			 h_ret);
		ret = ehca2ib_return_code(h_ret);
		goto create_qp_exit1;
	}

	ib_qp_num = my_qp->real_qp_num = parms.real_qp_num;
	my_qp->ipz_qp_handle = parms.qp_handle;
	my_qp->galpas = parms.galpas;

	swqe_size = ehca_calc_wqe_size(parms.squeue.act_nr_sges, is_llqp);
	rwqe_size = ehca_calc_wqe_size(parms.rqueue.act_nr_sges, is_llqp);

	switch (qp_type) {
	case IB_QPT_RC:
		if (is_llqp) {
			parms.squeue.act_nr_sges = 1;
			parms.rqueue.act_nr_sges = 1;
		}
		/* hide the extra WQE */
		parms.squeue.act_nr_wqes--;
		break;
	case IB_QPT_UD:
	case IB_QPT_GSI:
	case IB_QPT_SMI:
		/* UD circumvention */
		if (is_llqp) {
			parms.squeue.act_nr_sges = 1;
			parms.rqueue.act_nr_sges = 1;
		} else {
			parms.squeue.act_nr_sges -= 2;
			parms.rqueue.act_nr_sges -= 2;
		}

		if (IB_QPT_GSI == qp_type || IB_QPT_SMI == qp_type) {
			parms.squeue.act_nr_wqes = init_attr->cap.max_send_wr;
			parms.rqueue.act_nr_wqes = init_attr->cap.max_recv_wr;
			parms.squeue.act_nr_sges = init_attr->cap.max_send_sge;
			parms.rqueue.act_nr_sges = init_attr->cap.max_recv_sge;
			ib_qp_num = (qp_type == IB_QPT_SMI) ? 0 : 1;
		}

		break;

	default:
		break;
	}

	/* initialize r/squeue and register queue pages */
	if (HAS_SQ(my_qp)) {
		ret = init_qp_queue(
			shca, my_pd, my_qp, &my_qp->ipz_squeue, 0,
			HAS_RQ(my_qp) ? H_PAGE_REGISTERED : H_SUCCESS,
			&parms.squeue, swqe_size);
		if (ret) {
			ehca_err(pd->device, "Couldn't initialize squeue "
				 "and pages ret=%i", ret);
			goto create_qp_exit2;
		}
	}

	if (HAS_RQ(my_qp)) {
		ret = init_qp_queue(
			shca, my_pd, my_qp, &my_qp->ipz_rqueue, 1,
			H_SUCCESS, &parms.rqueue, rwqe_size);
		if (ret) {
			ehca_err(pd->device, "Couldn't initialize rqueue "
				 "and pages ret=%i", ret);
			goto create_qp_exit3;
		}
	}

	if (is_srq) {
		my_qp->ib_srq.pd = &my_pd->ib_pd;
		my_qp->ib_srq.device = my_pd->ib_pd.device;

		my_qp->ib_srq.srq_context = init_attr->qp_context;
		my_qp->ib_srq.event_handler = init_attr->event_handler;
	} else {
		my_qp->ib_qp.qp_num = ib_qp_num;
		my_qp->ib_qp.pd = &my_pd->ib_pd;
		my_qp->ib_qp.device = my_pd->ib_pd.device;

		my_qp->ib_qp.recv_cq = init_attr->recv_cq;
		my_qp->ib_qp.send_cq = init_attr->send_cq;

		my_qp->ib_qp.qp_type = qp_type;
		my_qp->ib_qp.srq = init_attr->srq;

		my_qp->ib_qp.qp_context = init_attr->qp_context;
		my_qp->ib_qp.event_handler = init_attr->event_handler;
	}

	init_attr->cap.max_inline_data = 0; /* not supported yet */
	init_attr->cap.max_recv_sge = parms.rqueue.act_nr_sges;
	init_attr->cap.max_recv_wr = parms.rqueue.act_nr_wqes;
	init_attr->cap.max_send_sge = parms.squeue.act_nr_sges;
	init_attr->cap.max_send_wr = parms.squeue.act_nr_wqes;
	my_qp->init_attr = *init_attr;

	if (qp_type == IB_QPT_SMI || qp_type == IB_QPT_GSI) {
		shca->sport[init_attr->port_num - 1].ibqp_sqp[qp_type] =
			&my_qp->ib_qp;
		if (ehca_nr_ports < 0) {
			/* alloc array to cache subsequent modify qp parms
			 * for autodetect mode
			 */
			my_qp->mod_qp_parm =
				kzalloc(EHCA_MOD_QP_PARM_MAX *
					sizeof(*my_qp->mod_qp_parm),
					GFP_KERNEL);
			if (!my_qp->mod_qp_parm) {
				ehca_err(pd->device,
					 "Could not alloc mod_qp_parm");
				goto create_qp_exit4;
			}
		}
	}

	/* NOTE: define_apq0() not supported yet */
	if (qp_type == IB_QPT_GSI) {
		h_ret = ehca_define_sqp(shca, my_qp, init_attr);
		if (h_ret != H_SUCCESS) {
			ret = ehca2ib_return_code(h_ret);
			goto create_qp_exit5;
		}
	}

	if (my_qp->send_cq) {
		ret = ehca_cq_assign_qp(my_qp->send_cq, my_qp);
		if (ret) {
			ehca_err(pd->device,
				 "Couldn't assign qp to send_cq ret=%i", ret);
			goto create_qp_exit5;
		}
	}

	/* copy queues, galpa data to user space */
	if (context && udata) {
		struct ehca_create_qp_resp resp;
		memset(&resp, 0, sizeof(resp));

		resp.qp_num = my_qp->real_qp_num;
		resp.token = my_qp->token;
		resp.qp_type = my_qp->qp_type;
		resp.ext_type = my_qp->ext_type;
		resp.qkey = my_qp->qkey;
		resp.real_qp_num = my_qp->real_qp_num;

		if (HAS_SQ(my_qp))
			queue2resp(&resp.ipz_squeue, &my_qp->ipz_squeue);
		if (HAS_RQ(my_qp))
			queue2resp(&resp.ipz_rqueue, &my_qp->ipz_rqueue);
		resp.fw_handle_ofs = (u32)
			(my_qp->galpas.user.fw_handle & (PAGE_SIZE - 1));

		if (ib_copy_to_udata(udata, &resp, sizeof resp)) {
			ehca_err(pd->device, "Copy to udata failed");
			ret = -EINVAL;
			goto create_qp_exit6;
		}
	}

	return my_qp;

create_qp_exit6:
	ehca_cq_unassign_qp(my_qp->send_cq, my_qp->real_qp_num);

create_qp_exit5:
	kfree(my_qp->mod_qp_parm);

create_qp_exit4:
	if (HAS_RQ(my_qp))
		ipz_queue_dtor(my_pd, &my_qp->ipz_rqueue);

create_qp_exit3:
	if (HAS_SQ(my_qp))
		ipz_queue_dtor(my_pd, &my_qp->ipz_squeue);

create_qp_exit2:
	hipz_h_destroy_qp(shca->ipz_hca_handle, my_qp);

create_qp_exit1:
	write_lock_irqsave(&ehca_qp_idr_lock, flags);
	idr_remove(&ehca_qp_idr, my_qp->token);
	write_unlock_irqrestore(&ehca_qp_idr_lock, flags);

create_qp_exit0:
	kmem_cache_free(qp_cache, my_qp);
	return ERR_PTR(ret);
}

struct ib_qp *ehca_create_qp(struct ib_pd *pd,
			     struct ib_qp_init_attr *qp_init_attr,
			     struct ib_udata *udata)
{
	struct ehca_qp *ret;

	ret = internal_create_qp(pd, qp_init_attr, NULL, udata, 0);
	return IS_ERR(ret) ? (struct ib_qp *)ret : &ret->ib_qp;
}

static int internal_destroy_qp(struct ib_device *dev, struct ehca_qp *my_qp,
			       struct ib_uobject *uobject);

struct ib_srq *ehca_create_srq(struct ib_pd *pd,
			       struct ib_srq_init_attr *srq_init_attr,
			       struct ib_udata *udata)
{
	struct ib_qp_init_attr qp_init_attr;
	struct ehca_qp *my_qp;
	struct ib_srq *ret;
	struct ehca_shca *shca = container_of(pd->device, struct ehca_shca,
					      ib_device);
	struct hcp_modify_qp_control_block *mqpcb;
	u64 hret, update_mask;

	/* For common attributes, internal_create_qp() takes its info
	 * out of qp_init_attr, so copy all common attrs there.
	 */
	memset(&qp_init_attr, 0, sizeof(qp_init_attr));
	qp_init_attr.event_handler = srq_init_attr->event_handler;
	qp_init_attr.qp_context = srq_init_attr->srq_context;
	qp_init_attr.sq_sig_type = IB_SIGNAL_ALL_WR;
	qp_init_attr.qp_type = IB_QPT_RC;
	qp_init_attr.cap.max_recv_wr = srq_init_attr->attr.max_wr;
	qp_init_attr.cap.max_recv_sge = srq_init_attr->attr.max_sge;

	my_qp = internal_create_qp(pd, &qp_init_attr, srq_init_attr, udata, 1);
	if (IS_ERR(my_qp))
		return (struct ib_srq *)my_qp;

	/* copy back return values */
	srq_init_attr->attr.max_wr = qp_init_attr.cap.max_recv_wr;
	srq_init_attr->attr.max_sge = 3;

	/* drive SRQ into RTR state */
	mqpcb = ehca_alloc_fw_ctrlblock(GFP_KERNEL);
	if (!mqpcb) {
		ehca_err(pd->device, "Could not get zeroed page for mqpcb "
			 "ehca_qp=%p qp_num=%x ", my_qp, my_qp->real_qp_num);
		ret = ERR_PTR(-ENOMEM);
		goto create_srq1;
	}

	mqpcb->qp_state = EHCA_QPS_INIT;
	mqpcb->prim_phys_port = 1;
	update_mask = EHCA_BMASK_SET(MQPCB_MASK_QP_STATE, 1);
	hret = hipz_h_modify_qp(shca->ipz_hca_handle,
				my_qp->ipz_qp_handle,
				&my_qp->pf,
				update_mask,
				mqpcb, my_qp->galpas.kernel);
	if (hret != H_SUCCESS) {
		ehca_err(pd->device, "Could not modify SRQ to INIT "
			 "ehca_qp=%p qp_num=%x h_ret=%li",
			 my_qp, my_qp->real_qp_num, hret);
		goto create_srq2;
	}

	mqpcb->qp_enable = 1;
	update_mask = EHCA_BMASK_SET(MQPCB_MASK_QP_ENABLE, 1);
	hret = hipz_h_modify_qp(shca->ipz_hca_handle,
				my_qp->ipz_qp_handle,
				&my_qp->pf,
				update_mask,
				mqpcb, my_qp->galpas.kernel);
	if (hret != H_SUCCESS) {
		ehca_err(pd->device, "Could not enable SRQ "
			 "ehca_qp=%p qp_num=%x h_ret=%li",
			 my_qp, my_qp->real_qp_num, hret);
		goto create_srq2;
	}

	mqpcb->qp_state  = EHCA_QPS_RTR;
	update_mask = EHCA_BMASK_SET(MQPCB_MASK_QP_STATE, 1);
	hret = hipz_h_modify_qp(shca->ipz_hca_handle,
				my_qp->ipz_qp_handle,
				&my_qp->pf,
				update_mask,
				mqpcb, my_qp->galpas.kernel);
	if (hret != H_SUCCESS) {
		ehca_err(pd->device, "Could not modify SRQ to RTR "
			 "ehca_qp=%p qp_num=%x h_ret=%li",
			 my_qp, my_qp->real_qp_num, hret);
		goto create_srq2;
	}

	ehca_free_fw_ctrlblock(mqpcb);

	return &my_qp->ib_srq;

create_srq2:
	ret = ERR_PTR(ehca2ib_return_code(hret));
	ehca_free_fw_ctrlblock(mqpcb);

create_srq1:
	internal_destroy_qp(pd->device, my_qp, my_qp->ib_srq.uobject);

	return ret;
}

/*
 * prepare_sqe_rts called by internal_modify_qp() at trans sqe -> rts
 * set purge bit of bad wqe and subsequent wqes to avoid reentering sqe
 * returns total number of bad wqes in bad_wqe_cnt
 */
static int prepare_sqe_rts(struct ehca_qp *my_qp, struct ehca_shca *shca,
			   int *bad_wqe_cnt)
{
	u64 h_ret;
	struct ipz_queue *squeue;
	void *bad_send_wqe_p, *bad_send_wqe_v;
	u64 q_ofs;
	struct ehca_wqe *wqe;
	int qp_num = my_qp->ib_qp.qp_num;

	/* get send wqe pointer */
	h_ret = hipz_h_disable_and_get_wqe(shca->ipz_hca_handle,
					   my_qp->ipz_qp_handle, &my_qp->pf,
					   &bad_send_wqe_p, NULL, 2);
	if (h_ret != H_SUCCESS) {
		ehca_err(&shca->ib_device, "hipz_h_disable_and_get_wqe() failed"
			 " ehca_qp=%p qp_num=%x h_ret=%li",
			 my_qp, qp_num, h_ret);
		return ehca2ib_return_code(h_ret);
	}
	bad_send_wqe_p = (void *)((u64)bad_send_wqe_p & (~(1L << 63)));
	ehca_dbg(&shca->ib_device, "qp_num=%x bad_send_wqe_p=%p",
		 qp_num, bad_send_wqe_p);
	/* convert wqe pointer to vadr */
	bad_send_wqe_v = abs_to_virt((u64)bad_send_wqe_p);
	if (ehca_debug_level)
		ehca_dmp(bad_send_wqe_v, 32, "qp_num=%x bad_wqe", qp_num);
	squeue = &my_qp->ipz_squeue;
	if (ipz_queue_abs_to_offset(squeue, (u64)bad_send_wqe_p, &q_ofs)) {
		ehca_err(&shca->ib_device, "failed to get wqe offset qp_num=%x"
			 " bad_send_wqe_p=%p", qp_num, bad_send_wqe_p);
		return -EFAULT;
	}

	/* loop sets wqe's purge bit */
	wqe = (struct ehca_wqe *)ipz_qeit_calc(squeue, q_ofs);
	*bad_wqe_cnt = 0;
	while (wqe->optype != 0xff && wqe->wqef != 0xff) {
		if (ehca_debug_level)
			ehca_dmp(wqe, 32, "qp_num=%x wqe", qp_num);
		wqe->nr_of_data_seg = 0; /* suppress data access */
		wqe->wqef = WQEF_PURGE; /* WQE to be purged */
		q_ofs = ipz_queue_advance_offset(squeue, q_ofs);
		wqe = (struct ehca_wqe *)ipz_qeit_calc(squeue, q_ofs);
		*bad_wqe_cnt = (*bad_wqe_cnt)+1;
	}
	/*
	 * bad wqe will be reprocessed and ignored when pol_cq() is called,
	 *  i.e. nr of wqes with flush error status is one less
	 */
	ehca_dbg(&shca->ib_device, "qp_num=%x flusherr_wqe_cnt=%x",
		 qp_num, (*bad_wqe_cnt)-1);
	wqe->wqef = 0;

	return 0;
}

/*
 * internal_modify_qp with circumvention to handle aqp0 properly
 * smi_reset2init indicates if this is an internal reset-to-init-call for
 * smi. This flag must always be zero if called from ehca_modify_qp()!
 * This internal func was intorduced to avoid recursion of ehca_modify_qp()!
 */
static int internal_modify_qp(struct ib_qp *ibqp,
			      struct ib_qp_attr *attr,
			      int attr_mask, int smi_reset2init)
{
	enum ib_qp_state qp_cur_state, qp_new_state;
	int cnt, qp_attr_idx, ret = 0;
	enum ib_qp_statetrans statetrans;
	struct hcp_modify_qp_control_block *mqpcb;
	struct ehca_qp *my_qp = container_of(ibqp, struct ehca_qp, ib_qp);
	struct ehca_shca *shca =
		container_of(ibqp->pd->device, struct ehca_shca, ib_device);
	u64 update_mask;
	u64 h_ret;
	int bad_wqe_cnt = 0;
	int squeue_locked = 0;
	unsigned long flags = 0;

	/* do query_qp to obtain current attr values */
	mqpcb = ehca_alloc_fw_ctrlblock(GFP_ATOMIC);
	if (!mqpcb) {
		ehca_err(ibqp->device, "Could not get zeroed page for mqpcb "
			 "ehca_qp=%p qp_num=%x ", my_qp, ibqp->qp_num);
		return -ENOMEM;
	}

	h_ret = hipz_h_query_qp(shca->ipz_hca_handle,
				my_qp->ipz_qp_handle,
				&my_qp->pf,
				mqpcb, my_qp->galpas.kernel);
	if (h_ret != H_SUCCESS) {
		ehca_err(ibqp->device, "hipz_h_query_qp() failed "
			 "ehca_qp=%p qp_num=%x h_ret=%li",
			 my_qp, ibqp->qp_num, h_ret);
		ret = ehca2ib_return_code(h_ret);
		goto modify_qp_exit1;
	}

	qp_cur_state = ehca2ib_qp_state(mqpcb->qp_state);

	if (qp_cur_state == -EINVAL) {	/* invalid qp state */
		ret = -EINVAL;
		ehca_err(ibqp->device, "Invalid current ehca_qp_state=%x "
			 "ehca_qp=%p qp_num=%x",
			 mqpcb->qp_state, my_qp, ibqp->qp_num);
		goto modify_qp_exit1;
	}
	/*
	 * circumvention to set aqp0 initial state to init
	 * as expected by IB spec
	 */
	if (smi_reset2init == 0 &&
	    ibqp->qp_type == IB_QPT_SMI &&
	    qp_cur_state == IB_QPS_RESET &&
	    (attr_mask & IB_QP_STATE) &&
	    attr->qp_state == IB_QPS_INIT) { /* RESET -> INIT */
		struct ib_qp_attr smiqp_attr = {
			.qp_state = IB_QPS_INIT,
			.port_num = my_qp->init_attr.port_num,
			.pkey_index = 0,
			.qkey = 0
		};
		int smiqp_attr_mask = IB_QP_STATE | IB_QP_PORT |
			IB_QP_PKEY_INDEX | IB_QP_QKEY;
		int smirc = internal_modify_qp(
			ibqp, &smiqp_attr, smiqp_attr_mask, 1);
		if (smirc) {
			ehca_err(ibqp->device, "SMI RESET -> INIT failed. "
				 "ehca_modify_qp() rc=%i", smirc);
			ret = H_PARAMETER;
			goto modify_qp_exit1;
		}
		qp_cur_state = IB_QPS_INIT;
		ehca_dbg(ibqp->device, "SMI RESET -> INIT succeeded");
	}
	/* is transmitted current state  equal to "real" current state */
	if ((attr_mask & IB_QP_CUR_STATE) &&
	    qp_cur_state != attr->cur_qp_state) {
		ret = -EINVAL;
		ehca_err(ibqp->device,
			 "Invalid IB_QP_CUR_STATE attr->curr_qp_state=%x <>"
			 " actual cur_qp_state=%x. ehca_qp=%p qp_num=%x",
			 attr->cur_qp_state, qp_cur_state, my_qp, ibqp->qp_num);
		goto modify_qp_exit1;
	}

	ehca_dbg(ibqp->device, "ehca_qp=%p qp_num=%x current qp_state=%x "
		 "new qp_state=%x attribute_mask=%x",
		 my_qp, ibqp->qp_num, qp_cur_state, attr->qp_state, attr_mask);

	qp_new_state = attr_mask & IB_QP_STATE ? attr->qp_state : qp_cur_state;
	if (!smi_reset2init &&
	    !ib_modify_qp_is_ok(qp_cur_state, qp_new_state, ibqp->qp_type,
				attr_mask)) {
		ret = -EINVAL;
		ehca_err(ibqp->device,
			 "Invalid qp transition new_state=%x cur_state=%x "
			 "ehca_qp=%p qp_num=%x attr_mask=%x", qp_new_state,
			 qp_cur_state, my_qp, ibqp->qp_num, attr_mask);
		goto modify_qp_exit1;
	}

	mqpcb->qp_state = ib2ehca_qp_state(qp_new_state);
	if (mqpcb->qp_state)
		update_mask = EHCA_BMASK_SET(MQPCB_MASK_QP_STATE, 1);
	else {
		ret = -EINVAL;
		ehca_err(ibqp->device, "Invalid new qp state=%x "
			 "ehca_qp=%p qp_num=%x",
			 qp_new_state, my_qp, ibqp->qp_num);
		goto modify_qp_exit1;
	}

	/* retrieve state transition struct to get req and opt attrs */
	statetrans = get_modqp_statetrans(qp_cur_state, qp_new_state);
	if (statetrans < 0) {
		ret = -EINVAL;
		ehca_err(ibqp->device, "<INVALID STATE CHANGE> qp_cur_state=%x "
			 "new_qp_state=%x State_xsition=%x ehca_qp=%p "
			 "qp_num=%x", qp_cur_state, qp_new_state,
			 statetrans, my_qp, ibqp->qp_num);
		goto modify_qp_exit1;
	}

	qp_attr_idx = ib2ehcaqptype(ibqp->qp_type);

	if (qp_attr_idx < 0) {
		ret = qp_attr_idx;
		ehca_err(ibqp->device,
			 "Invalid QP type=%x ehca_qp=%p qp_num=%x",
			 ibqp->qp_type, my_qp, ibqp->qp_num);
		goto modify_qp_exit1;
	}

	ehca_dbg(ibqp->device,
		 "ehca_qp=%p qp_num=%x <VALID STATE CHANGE> qp_state_xsit=%x",
		 my_qp, ibqp->qp_num, statetrans);

	/* eHCA2 rev2 and higher require the SEND_GRH_FLAG to be set
	 * in non-LL UD QPs.
	 */
	if ((my_qp->qp_type == IB_QPT_UD) &&
	    (my_qp->ext_type != EQPT_LLQP) &&
	    (statetrans == IB_QPST_INIT2RTR) &&
	    (shca->hw_level >= 0x22)) {
		update_mask |= EHCA_BMASK_SET(MQPCB_MASK_SEND_GRH_FLAG, 1);
		mqpcb->send_grh_flag = 1;
	}

	/* sqe -> rts: set purge bit of bad wqe before actual trans */
	if ((my_qp->qp_type == IB_QPT_UD ||
	     my_qp->qp_type == IB_QPT_GSI ||
	     my_qp->qp_type == IB_QPT_SMI) &&
	    statetrans == IB_QPST_SQE2RTS) {
		/* mark next free wqe if kernel */
		if (!ibqp->uobject) {
			struct ehca_wqe *wqe;
			/* lock send queue */
			spin_lock_irqsave(&my_qp->spinlock_s, flags);
			squeue_locked = 1;
			/* mark next free wqe */
			wqe = (struct ehca_wqe *)
				ipz_qeit_get(&my_qp->ipz_squeue);
			wqe->optype = wqe->wqef = 0xff;
			ehca_dbg(ibqp->device, "qp_num=%x next_free_wqe=%p",
				 ibqp->qp_num, wqe);
		}
		ret = prepare_sqe_rts(my_qp, shca, &bad_wqe_cnt);
		if (ret) {
			ehca_err(ibqp->device, "prepare_sqe_rts() failed "
				 "ehca_qp=%p qp_num=%x ret=%i",
				 my_qp, ibqp->qp_num, ret);
			goto modify_qp_exit2;
		}
	}

	/*
	 * enable RDMA_Atomic_Control if reset->init und reliable con
	 * this is necessary since gen2 does not provide that flag,
	 * but pHyp requires it
	 */
	if (statetrans == IB_QPST_RESET2INIT &&
	    (ibqp->qp_type == IB_QPT_RC || ibqp->qp_type == IB_QPT_UC)) {
		mqpcb->rdma_atomic_ctrl = 3;
		update_mask |= EHCA_BMASK_SET(MQPCB_MASK_RDMA_ATOMIC_CTRL, 1);
	}
	/* circ. pHyp requires #RDMA/Atomic Resp Res for UC INIT -> RTR */
	if (statetrans == IB_QPST_INIT2RTR &&
	    (ibqp->qp_type == IB_QPT_UC) &&
	    !(attr_mask & IB_QP_MAX_DEST_RD_ATOMIC)) {
		mqpcb->rdma_nr_atomic_resp_res = 1; /* default to 1 */
		update_mask |=
			EHCA_BMASK_SET(MQPCB_MASK_RDMA_NR_ATOMIC_RESP_RES, 1);
	}

	if (attr_mask & IB_QP_PKEY_INDEX) {
		if (attr->pkey_index >= 16) {
			ret = -EINVAL;
			ehca_err(ibqp->device, "Invalid pkey_index=%x. "
				 "ehca_qp=%p qp_num=%x max_pkey_index=f",
				 attr->pkey_index, my_qp, ibqp->qp_num);
			goto modify_qp_exit2;
		}
		mqpcb->prim_p_key_idx = attr->pkey_index;
		update_mask |= EHCA_BMASK_SET(MQPCB_MASK_PRIM_P_KEY_IDX, 1);
	}
	if (attr_mask & IB_QP_PORT) {
		struct ehca_sport *sport;
		struct ehca_qp *aqp1;
		if (attr->port_num < 1 || attr->port_num > shca->num_ports) {
			ret = -EINVAL;
			ehca_err(ibqp->device, "Invalid port=%x. "
				 "ehca_qp=%p qp_num=%x num_ports=%x",
				 attr->port_num, my_qp, ibqp->qp_num,
				 shca->num_ports);
			goto modify_qp_exit2;
		}
		sport = &shca->sport[attr->port_num - 1];
		if (!sport->ibqp_sqp[IB_QPT_GSI]) {
			/* should not occur */
			ret = -EFAULT;
			ehca_err(ibqp->device, "AQP1 was not created for "
				 "port=%x", attr->port_num);
			goto modify_qp_exit2;
		}
		aqp1 = container_of(sport->ibqp_sqp[IB_QPT_GSI],
				    struct ehca_qp, ib_qp);
		if (ibqp->qp_type != IB_QPT_GSI &&
		    ibqp->qp_type != IB_QPT_SMI &&
		    aqp1->mod_qp_parm) {
			/*
			 * firmware will reject this modify_qp() because
			 * port is not activated/initialized fully
			 */
			ret = -EFAULT;
			ehca_warn(ibqp->device, "Couldn't modify qp port=%x: "
				  "either port is being activated (try again) "
				  "or cabling issue", attr->port_num);
			goto modify_qp_exit2;
		}
		mqpcb->prim_phys_port = attr->port_num;
		update_mask |= EHCA_BMASK_SET(MQPCB_MASK_PRIM_PHYS_PORT, 1);
	}
	if (attr_mask & IB_QP_QKEY) {
		mqpcb->qkey = attr->qkey;
		update_mask |= EHCA_BMASK_SET(MQPCB_MASK_QKEY, 1);
	}
	if (attr_mask & IB_QP_AV) {
		mqpcb->dlid = attr->ah_attr.dlid;
		update_mask |= EHCA_BMASK_SET(MQPCB_MASK_DLID, 1);
		mqpcb->source_path_bits = attr->ah_attr.src_path_bits;
		update_mask |= EHCA_BMASK_SET(MQPCB_MASK_SOURCE_PATH_BITS, 1);
		mqpcb->service_level = attr->ah_attr.sl;
		update_mask |= EHCA_BMASK_SET(MQPCB_MASK_SERVICE_LEVEL, 1);

		if (ehca_calc_ipd(shca, mqpcb->prim_phys_port,
				  attr->ah_attr.static_rate,
				  &mqpcb->max_static_rate)) {
			ret = -EINVAL;
			goto modify_qp_exit2;
		}
		update_mask |= EHCA_BMASK_SET(MQPCB_MASK_MAX_STATIC_RATE, 1);

		/*
		 * Always supply the GRH flag, even if it's zero, to give the
		 * hypervisor a clear "yes" or "no" instead of a "perhaps"
		 */
		update_mask |= EHCA_BMASK_SET(MQPCB_MASK_SEND_GRH_FLAG, 1);

		/*
		 * only if GRH is TRUE we might consider SOURCE_GID_IDX
		 * and DEST_GID otherwise phype will return H_ATTR_PARM!!!
		 */
		if (attr->ah_attr.ah_flags == IB_AH_GRH) {
			mqpcb->send_grh_flag = 1;

			mqpcb->source_gid_idx = attr->ah_attr.grh.sgid_index;
			update_mask |=
				EHCA_BMASK_SET(MQPCB_MASK_SOURCE_GID_IDX, 1);

			for (cnt = 0; cnt < 16; cnt++)
				mqpcb->dest_gid.byte[cnt] =
					attr->ah_attr.grh.dgid.raw[cnt];

			update_mask |= EHCA_BMASK_SET(MQPCB_MASK_DEST_GID, 1);
			mqpcb->flow_label = attr->ah_attr.grh.flow_label;
			update_mask |= EHCA_BMASK_SET(MQPCB_MASK_FLOW_LABEL, 1);
			mqpcb->hop_limit = attr->ah_attr.grh.hop_limit;
			update_mask |= EHCA_BMASK_SET(MQPCB_MASK_HOP_LIMIT, 1);
			mqpcb->traffic_class = attr->ah_attr.grh.traffic_class;
			update_mask |=
				EHCA_BMASK_SET(MQPCB_MASK_TRAFFIC_CLASS, 1);
		}
	}

	if (attr_mask & IB_QP_PATH_MTU) {
		/* store ld(MTU) */
		my_qp->mtu_shift = attr->path_mtu + 7;
		mqpcb->path_mtu = attr->path_mtu;
		update_mask |= EHCA_BMASK_SET(MQPCB_MASK_PATH_MTU, 1);
	}
	if (attr_mask & IB_QP_TIMEOUT) {
		mqpcb->timeout = attr->timeout;
		update_mask |= EHCA_BMASK_SET(MQPCB_MASK_TIMEOUT, 1);
	}
	if (attr_mask & IB_QP_RETRY_CNT) {
		mqpcb->retry_count = attr->retry_cnt;
		update_mask |= EHCA_BMASK_SET(MQPCB_MASK_RETRY_COUNT, 1);
	}
	if (attr_mask & IB_QP_RNR_RETRY) {
		mqpcb->rnr_retry_count = attr->rnr_retry;
		update_mask |= EHCA_BMASK_SET(MQPCB_MASK_RNR_RETRY_COUNT, 1);
	}
	if (attr_mask & IB_QP_RQ_PSN) {
		mqpcb->receive_psn = attr->rq_psn;
		update_mask |= EHCA_BMASK_SET(MQPCB_MASK_RECEIVE_PSN, 1);
	}
	if (attr_mask & IB_QP_MAX_DEST_RD_ATOMIC) {
		mqpcb->rdma_nr_atomic_resp_res = attr->max_dest_rd_atomic < 3 ?
			attr->max_dest_rd_atomic : 2;
		update_mask |=
			EHCA_BMASK_SET(MQPCB_MASK_RDMA_NR_ATOMIC_RESP_RES, 1);
	}
	if (attr_mask & IB_QP_MAX_QP_RD_ATOMIC) {
		mqpcb->rdma_atomic_outst_dest_qp = attr->max_rd_atomic < 3 ?
			attr->max_rd_atomic : 2;
		update_mask |=
			EHCA_BMASK_SET
			(MQPCB_MASK_RDMA_ATOMIC_OUTST_DEST_QP, 1);
	}
	if (attr_mask & IB_QP_ALT_PATH) {
		if (attr->alt_port_num < 1
		    || attr->alt_port_num > shca->num_ports) {
			ret = -EINVAL;
			ehca_err(ibqp->device, "Invalid alt_port=%x. "
				 "ehca_qp=%p qp_num=%x num_ports=%x",
				 attr->alt_port_num, my_qp, ibqp->qp_num,
				 shca->num_ports);
			goto modify_qp_exit2;
		}
		mqpcb->alt_phys_port = attr->alt_port_num;

		if (attr->alt_pkey_index >= 16) {
			ret = -EINVAL;
			ehca_err(ibqp->device, "Invalid alt_pkey_index=%x. "
				 "ehca_qp=%p qp_num=%x max_pkey_index=f",
				 attr->pkey_index, my_qp, ibqp->qp_num);
			goto modify_qp_exit2;
		}
		mqpcb->alt_p_key_idx = attr->alt_pkey_index;

		mqpcb->timeout_al = attr->alt_timeout;
		mqpcb->dlid_al = attr->alt_ah_attr.dlid;
		mqpcb->source_path_bits_al = attr->alt_ah_attr.src_path_bits;
		mqpcb->service_level_al = attr->alt_ah_attr.sl;

		if (ehca_calc_ipd(shca, mqpcb->alt_phys_port,
				  attr->alt_ah_attr.static_rate,
				  &mqpcb->max_static_rate_al)) {
			ret = -EINVAL;
			goto modify_qp_exit2;
		}

		/* OpenIB doesn't support alternate retry counts - copy them */
		mqpcb->retry_count_al = mqpcb->retry_count;
		mqpcb->rnr_retry_count_al = mqpcb->rnr_retry_count;

		update_mask |= EHCA_BMASK_SET(MQPCB_MASK_ALT_PHYS_PORT, 1)
			| EHCA_BMASK_SET(MQPCB_MASK_ALT_P_KEY_IDX, 1)
			| EHCA_BMASK_SET(MQPCB_MASK_TIMEOUT_AL, 1)
			| EHCA_BMASK_SET(MQPCB_MASK_DLID_AL, 1)
			| EHCA_BMASK_SET(MQPCB_MASK_SOURCE_PATH_BITS_AL, 1)
			| EHCA_BMASK_SET(MQPCB_MASK_SERVICE_LEVEL_AL, 1)
			| EHCA_BMASK_SET(MQPCB_MASK_MAX_STATIC_RATE_AL, 1)
			| EHCA_BMASK_SET(MQPCB_MASK_RETRY_COUNT_AL, 1)
			| EHCA_BMASK_SET(MQPCB_MASK_RNR_RETRY_COUNT_AL, 1);

		/*
		 * Always supply the GRH flag, even if it's zero, to give the
		 * hypervisor a clear "yes" or "no" instead of a "perhaps"
		 */
		update_mask |= EHCA_BMASK_SET(MQPCB_MASK_SEND_GRH_FLAG_AL, 1);

		/*
		 * only if GRH is TRUE we might consider SOURCE_GID_IDX
		 * and DEST_GID otherwise phype will return H_ATTR_PARM!!!
		 */
		if (attr->alt_ah_attr.ah_flags == IB_AH_GRH) {
			mqpcb->send_grh_flag_al = 1;

			for (cnt = 0; cnt < 16; cnt++)
				mqpcb->dest_gid_al.byte[cnt] =
					attr->alt_ah_attr.grh.dgid.raw[cnt];
			mqpcb->source_gid_idx_al =
				attr->alt_ah_attr.grh.sgid_index;
			mqpcb->flow_label_al = attr->alt_ah_attr.grh.flow_label;
			mqpcb->hop_limit_al = attr->alt_ah_attr.grh.hop_limit;
			mqpcb->traffic_class_al =
				attr->alt_ah_attr.grh.traffic_class;

			update_mask |=
				EHCA_BMASK_SET(MQPCB_MASK_SOURCE_GID_IDX_AL, 1)
				| EHCA_BMASK_SET(MQPCB_MASK_DEST_GID_AL, 1)
				| EHCA_BMASK_SET(MQPCB_MASK_FLOW_LABEL_AL, 1)
				| EHCA_BMASK_SET(MQPCB_MASK_HOP_LIMIT_AL, 1) |
				EHCA_BMASK_SET(MQPCB_MASK_TRAFFIC_CLASS_AL, 1);
		}
	}

	if (attr_mask & IB_QP_MIN_RNR_TIMER) {
		mqpcb->min_rnr_nak_timer_field = attr->min_rnr_timer;
		update_mask |=
			EHCA_BMASK_SET(MQPCB_MASK_MIN_RNR_NAK_TIMER_FIELD, 1);
	}

	if (attr_mask & IB_QP_SQ_PSN) {
		mqpcb->send_psn = attr->sq_psn;
		update_mask |= EHCA_BMASK_SET(MQPCB_MASK_SEND_PSN, 1);
	}

	if (attr_mask & IB_QP_DEST_QPN) {
		mqpcb->dest_qp_nr = attr->dest_qp_num;
		update_mask |= EHCA_BMASK_SET(MQPCB_MASK_DEST_QP_NR, 1);
	}

	if (attr_mask & IB_QP_PATH_MIG_STATE) {
		if (attr->path_mig_state != IB_MIG_REARM
		    && attr->path_mig_state != IB_MIG_MIGRATED) {
			ret = -EINVAL;
			ehca_err(ibqp->device, "Invalid mig_state=%x",
				 attr->path_mig_state);
			goto modify_qp_exit2;
		}
		mqpcb->path_migration_state = attr->path_mig_state + 1;
		update_mask |=
			EHCA_BMASK_SET(MQPCB_MASK_PATH_MIGRATION_STATE, 1);
	}

	if (attr_mask & IB_QP_CAP) {
		mqpcb->max_nr_outst_send_wr = attr->cap.max_send_wr+1;
		update_mask |=
			EHCA_BMASK_SET(MQPCB_MASK_MAX_NR_OUTST_SEND_WR, 1);
		mqpcb->max_nr_outst_recv_wr = attr->cap.max_recv_wr+1;
		update_mask |=
			EHCA_BMASK_SET(MQPCB_MASK_MAX_NR_OUTST_RECV_WR, 1);
		/* no support for max_send/recv_sge yet */
	}

	if (ehca_debug_level)
		ehca_dmp(mqpcb, 4*70, "qp_num=%x", ibqp->qp_num);

	h_ret = hipz_h_modify_qp(shca->ipz_hca_handle,
				 my_qp->ipz_qp_handle,
				 &my_qp->pf,
				 update_mask,
				 mqpcb, my_qp->galpas.kernel);

	if (h_ret != H_SUCCESS) {
		ret = ehca2ib_return_code(h_ret);
		ehca_err(ibqp->device, "hipz_h_modify_qp() failed h_ret=%li "
			 "ehca_qp=%p qp_num=%x", h_ret, my_qp, ibqp->qp_num);
		goto modify_qp_exit2;
	}

	if ((my_qp->qp_type == IB_QPT_UD ||
	     my_qp->qp_type == IB_QPT_GSI ||
	     my_qp->qp_type == IB_QPT_SMI) &&
	    statetrans == IB_QPST_SQE2RTS) {
		/* doorbell to reprocessing wqes */
		iosync(); /* serialize GAL register access */
		hipz_update_sqa(my_qp, bad_wqe_cnt-1);
		ehca_gen_dbg("doorbell for %x wqes", bad_wqe_cnt);
	}

	if (statetrans == IB_QPST_RESET2INIT ||
	    statetrans == IB_QPST_INIT2INIT) {
		mqpcb->qp_enable = 1;
		mqpcb->qp_state = EHCA_QPS_INIT;
		update_mask = 0;
		update_mask = EHCA_BMASK_SET(MQPCB_MASK_QP_ENABLE, 1);

		h_ret = hipz_h_modify_qp(shca->ipz_hca_handle,
					 my_qp->ipz_qp_handle,
					 &my_qp->pf,
					 update_mask,
					 mqpcb,
					 my_qp->galpas.kernel);

		if (h_ret != H_SUCCESS) {
			ret = ehca2ib_return_code(h_ret);
			ehca_err(ibqp->device, "ENABLE in context of "
				 "RESET_2_INIT failed! Maybe you didn't get "
				 "a LID h_ret=%li ehca_qp=%p qp_num=%x",
				 h_ret, my_qp, ibqp->qp_num);
			goto modify_qp_exit2;
		}
	}

	if (statetrans == IB_QPST_ANY2RESET) {
		ipz_qeit_reset(&my_qp->ipz_rqueue);
		ipz_qeit_reset(&my_qp->ipz_squeue);
	}

	if (attr_mask & IB_QP_QKEY)
		my_qp->qkey = attr->qkey;

modify_qp_exit2:
	if (squeue_locked) { /* this means: sqe -> rts */
		spin_unlock_irqrestore(&my_qp->spinlock_s, flags);
		my_qp->sqerr_purgeflag = 1;
	}

modify_qp_exit1:
	ehca_free_fw_ctrlblock(mqpcb);

	return ret;
}

int ehca_modify_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr, int attr_mask,
		   struct ib_udata *udata)
{
	struct ehca_shca *shca = container_of(ibqp->device, struct ehca_shca,
					      ib_device);
	struct ehca_qp *my_qp = container_of(ibqp, struct ehca_qp, ib_qp);
	struct ehca_pd *my_pd = container_of(my_qp->ib_qp.pd, struct ehca_pd,
					     ib_pd);
	u32 cur_pid = current->tgid;

	if (my_pd->ib_pd.uobject && my_pd->ib_pd.uobject->context &&
	    my_pd->ownpid != cur_pid) {
		ehca_err(ibqp->pd->device, "Invalid caller pid=%x ownpid=%x",
			 cur_pid, my_pd->ownpid);
		return -EINVAL;
	}

	/* The if-block below caches qp_attr to be modified for GSI and SMI
	 * qps during the initialization by ib_mad. When the respective port
	 * is activated, ie we got an event PORT_ACTIVE, we'll replay the
	 * cached modify calls sequence, see ehca_recover_sqs() below.
	 * Why that is required:
	 * 1) If one port is connected, older code requires that port one
	 *    to be connected and module option nr_ports=1 to be given by
	 *    user, which is very inconvenient for end user.
	 * 2) Firmware accepts modify_qp() only if respective port has become
	 *    active. Older code had a wait loop of 30sec create_qp()/
	 *    define_aqp1(), which is not appropriate in practice. This
	 *    code now removes that wait loop, see define_aqp1(), and always
	 *    reports all ports to ib_mad resp. users. Only activated ports
	 *    will then usable for the users.
	 */
	if (ibqp->qp_type == IB_QPT_GSI || ibqp->qp_type == IB_QPT_SMI) {
		int port = my_qp->init_attr.port_num;
		struct ehca_sport *sport = &shca->sport[port - 1];
		unsigned long flags;
		spin_lock_irqsave(&sport->mod_sqp_lock, flags);
		/* cache qp_attr only during init */
		if (my_qp->mod_qp_parm) {
			struct ehca_mod_qp_parm *p;
			if (my_qp->mod_qp_parm_idx >= EHCA_MOD_QP_PARM_MAX) {
				ehca_err(&shca->ib_device,
					 "mod_qp_parm overflow state=%x port=%x"
					 " type=%x", attr->qp_state,
					 my_qp->init_attr.port_num,
					 ibqp->qp_type);
				spin_unlock_irqrestore(&sport->mod_sqp_lock,
						       flags);
				return -EINVAL;
			}
			p = &my_qp->mod_qp_parm[my_qp->mod_qp_parm_idx];
			p->mask = attr_mask;
			p->attr = *attr;
			my_qp->mod_qp_parm_idx++;
			ehca_dbg(&shca->ib_device,
				 "Saved qp_attr for state=%x port=%x type=%x",
				 attr->qp_state, my_qp->init_attr.port_num,
				 ibqp->qp_type);
			spin_unlock_irqrestore(&sport->mod_sqp_lock, flags);
			return 0;
		}
		spin_unlock_irqrestore(&sport->mod_sqp_lock, flags);
	}

	return internal_modify_qp(ibqp, attr, attr_mask, 0);
}

void ehca_recover_sqp(struct ib_qp *sqp)
{
	struct ehca_qp *my_sqp = container_of(sqp, struct ehca_qp, ib_qp);
	int port = my_sqp->init_attr.port_num;
	struct ib_qp_attr attr;
	struct ehca_mod_qp_parm *qp_parm;
	int i, qp_parm_idx, ret;
	unsigned long flags, wr_cnt;

	if (!my_sqp->mod_qp_parm)
		return;
	ehca_dbg(sqp->device, "SQP port=%x qp_num=%x", port, sqp->qp_num);

	qp_parm = my_sqp->mod_qp_parm;
	qp_parm_idx = my_sqp->mod_qp_parm_idx;
	for (i = 0; i < qp_parm_idx; i++) {
		attr = qp_parm[i].attr;
		ret = internal_modify_qp(sqp, &attr, qp_parm[i].mask, 0);
		if (ret) {
			ehca_err(sqp->device, "Could not modify SQP port=%x "
				 "qp_num=%x ret=%x", port, sqp->qp_num, ret);
			goto free_qp_parm;
		}
		ehca_dbg(sqp->device, "SQP port=%x qp_num=%x in state=%x",
			 port, sqp->qp_num, attr.qp_state);
	}

	/* re-trigger posted recv wrs */
	wr_cnt =  my_sqp->ipz_rqueue.current_q_offset /
		my_sqp->ipz_rqueue.qe_size;
	if (wr_cnt) {
		spin_lock_irqsave(&my_sqp->spinlock_r, flags);
		hipz_update_rqa(my_sqp, wr_cnt);
		spin_unlock_irqrestore(&my_sqp->spinlock_r, flags);
		ehca_dbg(sqp->device, "doorbell port=%x qp_num=%x wr_cnt=%lx",
			 port, sqp->qp_num, wr_cnt);
	}

free_qp_parm:
	kfree(qp_parm);
	/* this prevents subsequent calls to modify_qp() to cache qp_attr */
	my_sqp->mod_qp_parm = NULL;
}

int ehca_query_qp(struct ib_qp *qp,
		  struct ib_qp_attr *qp_attr,
		  int qp_attr_mask, struct ib_qp_init_attr *qp_init_attr)
{
	struct ehca_qp *my_qp = container_of(qp, struct ehca_qp, ib_qp);
	struct ehca_pd *my_pd = container_of(my_qp->ib_qp.pd, struct ehca_pd,
					     ib_pd);
	struct ehca_shca *shca = container_of(qp->device, struct ehca_shca,
					      ib_device);
	struct ipz_adapter_handle adapter_handle = shca->ipz_hca_handle;
	struct hcp_modify_qp_control_block *qpcb;
	u32 cur_pid = current->tgid;
	int cnt, ret = 0;
	u64 h_ret;

	if (my_pd->ib_pd.uobject  && my_pd->ib_pd.uobject->context  &&
	    my_pd->ownpid != cur_pid) {
		ehca_err(qp->device, "Invalid caller pid=%x ownpid=%x",
			 cur_pid, my_pd->ownpid);
		return -EINVAL;
	}

	if (qp_attr_mask & QP_ATTR_QUERY_NOT_SUPPORTED) {
		ehca_err(qp->device, "Invalid attribute mask "
			 "ehca_qp=%p qp_num=%x qp_attr_mask=%x ",
			 my_qp, qp->qp_num, qp_attr_mask);
		return -EINVAL;
	}

	qpcb = ehca_alloc_fw_ctrlblock(GFP_KERNEL);
	if (!qpcb) {
		ehca_err(qp->device, "Out of memory for qpcb "
			 "ehca_qp=%p qp_num=%x", my_qp, qp->qp_num);
		return -ENOMEM;
	}

	h_ret = hipz_h_query_qp(adapter_handle,
				my_qp->ipz_qp_handle,
				&my_qp->pf,
				qpcb, my_qp->galpas.kernel);

	if (h_ret != H_SUCCESS) {
		ret = ehca2ib_return_code(h_ret);
		ehca_err(qp->device, "hipz_h_query_qp() failed "
			 "ehca_qp=%p qp_num=%x h_ret=%li",
			 my_qp, qp->qp_num, h_ret);
		goto query_qp_exit1;
	}

	qp_attr->cur_qp_state = ehca2ib_qp_state(qpcb->qp_state);
	qp_attr->qp_state = qp_attr->cur_qp_state;

	if (qp_attr->cur_qp_state == -EINVAL) {
		ret = -EINVAL;
		ehca_err(qp->device, "Got invalid ehca_qp_state=%x "
			 "ehca_qp=%p qp_num=%x",
			 qpcb->qp_state, my_qp, qp->qp_num);
		goto query_qp_exit1;
	}

	if (qp_attr->qp_state == IB_QPS_SQD)
		qp_attr->sq_draining = 1;

	qp_attr->qkey = qpcb->qkey;
	qp_attr->path_mtu = qpcb->path_mtu;
	qp_attr->path_mig_state = qpcb->path_migration_state - 1;
	qp_attr->rq_psn = qpcb->receive_psn;
	qp_attr->sq_psn = qpcb->send_psn;
	qp_attr->min_rnr_timer = qpcb->min_rnr_nak_timer_field;
	qp_attr->cap.max_send_wr = qpcb->max_nr_outst_send_wr-1;
	qp_attr->cap.max_recv_wr = qpcb->max_nr_outst_recv_wr-1;
	/* UD_AV CIRCUMVENTION */
	if (my_qp->qp_type == IB_QPT_UD) {
		qp_attr->cap.max_send_sge =
			qpcb->actual_nr_sges_in_sq_wqe - 2;
		qp_attr->cap.max_recv_sge =
			qpcb->actual_nr_sges_in_rq_wqe - 2;
	} else {
		qp_attr->cap.max_send_sge =
			qpcb->actual_nr_sges_in_sq_wqe;
		qp_attr->cap.max_recv_sge =
			qpcb->actual_nr_sges_in_rq_wqe;
	}

	qp_attr->cap.max_inline_data = my_qp->sq_max_inline_data_size;
	qp_attr->dest_qp_num = qpcb->dest_qp_nr;

	qp_attr->pkey_index =
		EHCA_BMASK_GET(MQPCB_PRIM_P_KEY_IDX, qpcb->prim_p_key_idx);

	qp_attr->port_num =
		EHCA_BMASK_GET(MQPCB_PRIM_PHYS_PORT, qpcb->prim_phys_port);

	qp_attr->timeout = qpcb->timeout;
	qp_attr->retry_cnt = qpcb->retry_count;
	qp_attr->rnr_retry = qpcb->rnr_retry_count;

	qp_attr->alt_pkey_index =
		EHCA_BMASK_GET(MQPCB_PRIM_P_KEY_IDX, qpcb->alt_p_key_idx);

	qp_attr->alt_port_num = qpcb->alt_phys_port;
	qp_attr->alt_timeout = qpcb->timeout_al;

	qp_attr->max_dest_rd_atomic = qpcb->rdma_nr_atomic_resp_res;
	qp_attr->max_rd_atomic = qpcb->rdma_atomic_outst_dest_qp;

	/* primary av */
	qp_attr->ah_attr.sl = qpcb->service_level;

	if (qpcb->send_grh_flag) {
		qp_attr->ah_attr.ah_flags = IB_AH_GRH;
	}

	qp_attr->ah_attr.static_rate = qpcb->max_static_rate;
	qp_attr->ah_attr.dlid = qpcb->dlid;
	qp_attr->ah_attr.src_path_bits = qpcb->source_path_bits;
	qp_attr->ah_attr.port_num = qp_attr->port_num;

	/* primary GRH */
	qp_attr->ah_attr.grh.traffic_class = qpcb->traffic_class;
	qp_attr->ah_attr.grh.hop_limit = qpcb->hop_limit;
	qp_attr->ah_attr.grh.sgid_index = qpcb->source_gid_idx;
	qp_attr->ah_attr.grh.flow_label = qpcb->flow_label;

	for (cnt = 0; cnt < 16; cnt++)
		qp_attr->ah_attr.grh.dgid.raw[cnt] =
			qpcb->dest_gid.byte[cnt];

	/* alternate AV */
	qp_attr->alt_ah_attr.sl = qpcb->service_level_al;
	if (qpcb->send_grh_flag_al) {
		qp_attr->alt_ah_attr.ah_flags = IB_AH_GRH;
	}

	qp_attr->alt_ah_attr.static_rate = qpcb->max_static_rate_al;
	qp_attr->alt_ah_attr.dlid = qpcb->dlid_al;
	qp_attr->alt_ah_attr.src_path_bits = qpcb->source_path_bits_al;

	/* alternate GRH */
	qp_attr->alt_ah_attr.grh.traffic_class = qpcb->traffic_class_al;
	qp_attr->alt_ah_attr.grh.hop_limit = qpcb->hop_limit_al;
	qp_attr->alt_ah_attr.grh.sgid_index = qpcb->source_gid_idx_al;
	qp_attr->alt_ah_attr.grh.flow_label = qpcb->flow_label_al;

	for (cnt = 0; cnt < 16; cnt++)
		qp_attr->alt_ah_attr.grh.dgid.raw[cnt] =
			qpcb->dest_gid_al.byte[cnt];

	/* return init attributes given in ehca_create_qp */
	if (qp_init_attr)
		*qp_init_attr = my_qp->init_attr;

	if (ehca_debug_level)
		ehca_dmp(qpcb, 4*70, "qp_num=%x", qp->qp_num);

query_qp_exit1:
	ehca_free_fw_ctrlblock(qpcb);

	return ret;
}

int ehca_modify_srq(struct ib_srq *ibsrq, struct ib_srq_attr *attr,
		    enum ib_srq_attr_mask attr_mask, struct ib_udata *udata)
{
	struct ehca_qp *my_qp =
		container_of(ibsrq, struct ehca_qp, ib_srq);
	struct ehca_pd *my_pd =
		container_of(ibsrq->pd, struct ehca_pd, ib_pd);
	struct ehca_shca *shca =
		container_of(ibsrq->pd->device, struct ehca_shca, ib_device);
	struct hcp_modify_qp_control_block *mqpcb;
	u64 update_mask;
	u64 h_ret;
	int ret = 0;

	u32 cur_pid = current->tgid;
	if (my_pd->ib_pd.uobject && my_pd->ib_pd.uobject->context &&
	    my_pd->ownpid != cur_pid) {
		ehca_err(ibsrq->pd->device, "Invalid caller pid=%x ownpid=%x",
			 cur_pid, my_pd->ownpid);
		return -EINVAL;
	}

	mqpcb = ehca_alloc_fw_ctrlblock(GFP_KERNEL);
	if (!mqpcb) {
		ehca_err(ibsrq->device, "Could not get zeroed page for mqpcb "
			 "ehca_qp=%p qp_num=%x ", my_qp, my_qp->real_qp_num);
		return -ENOMEM;
	}

	update_mask = 0;
	if (attr_mask & IB_SRQ_LIMIT) {
		attr_mask &= ~IB_SRQ_LIMIT;
		update_mask |=
			EHCA_BMASK_SET(MQPCB_MASK_CURR_SRQ_LIMIT, 1)
			| EHCA_BMASK_SET(MQPCB_MASK_QP_AFF_ASYN_EV_LOG_REG, 1);
		mqpcb->curr_srq_limit =
			EHCA_BMASK_SET(MQPCB_CURR_SRQ_LIMIT, attr->srq_limit);
		mqpcb->qp_aff_asyn_ev_log_reg =
			EHCA_BMASK_SET(QPX_AAELOG_RESET_SRQ_LIMIT, 1);
	}

	/* by now, all bits in attr_mask should have been cleared */
	if (attr_mask) {
		ehca_err(ibsrq->device, "invalid attribute mask bits set  "
			 "attr_mask=%x", attr_mask);
		ret = -EINVAL;
		goto modify_srq_exit0;
	}

	if (ehca_debug_level)
		ehca_dmp(mqpcb, 4*70, "qp_num=%x", my_qp->real_qp_num);

	h_ret = hipz_h_modify_qp(shca->ipz_hca_handle, my_qp->ipz_qp_handle,
				 NULL, update_mask, mqpcb,
				 my_qp->galpas.kernel);

	if (h_ret != H_SUCCESS) {
		ret = ehca2ib_return_code(h_ret);
		ehca_err(ibsrq->device, "hipz_h_modify_qp() failed h_ret=%li "
			 "ehca_qp=%p qp_num=%x",
			 h_ret, my_qp, my_qp->real_qp_num);
	}

modify_srq_exit0:
	ehca_free_fw_ctrlblock(mqpcb);

	return ret;
}

int ehca_query_srq(struct ib_srq *srq, struct ib_srq_attr *srq_attr)
{
	struct ehca_qp *my_qp = container_of(srq, struct ehca_qp, ib_srq);
	struct ehca_pd *my_pd = container_of(srq->pd, struct ehca_pd, ib_pd);
	struct ehca_shca *shca = container_of(srq->device, struct ehca_shca,
					      ib_device);
	struct ipz_adapter_handle adapter_handle = shca->ipz_hca_handle;
	struct hcp_modify_qp_control_block *qpcb;
	u32 cur_pid = current->tgid;
	int ret = 0;
	u64 h_ret;

	if (my_pd->ib_pd.uobject  && my_pd->ib_pd.uobject->context  &&
	    my_pd->ownpid != cur_pid) {
		ehca_err(srq->device, "Invalid caller pid=%x ownpid=%x",
			 cur_pid, my_pd->ownpid);
		return -EINVAL;
	}

	qpcb = ehca_alloc_fw_ctrlblock(GFP_KERNEL);
	if (!qpcb) {
		ehca_err(srq->device, "Out of memory for qpcb "
			 "ehca_qp=%p qp_num=%x", my_qp, my_qp->real_qp_num);
		return -ENOMEM;
	}

	h_ret = hipz_h_query_qp(adapter_handle, my_qp->ipz_qp_handle,
				NULL, qpcb, my_qp->galpas.kernel);

	if (h_ret != H_SUCCESS) {
		ret = ehca2ib_return_code(h_ret);
		ehca_err(srq->device, "hipz_h_query_qp() failed "
			 "ehca_qp=%p qp_num=%x h_ret=%li",
			 my_qp, my_qp->real_qp_num, h_ret);
		goto query_srq_exit1;
	}

	srq_attr->max_wr = qpcb->max_nr_outst_recv_wr - 1;
	srq_attr->max_sge = 3;
	srq_attr->srq_limit = EHCA_BMASK_GET(
		MQPCB_CURR_SRQ_LIMIT, qpcb->curr_srq_limit);

	if (ehca_debug_level)
		ehca_dmp(qpcb, 4*70, "qp_num=%x", my_qp->real_qp_num);

query_srq_exit1:
	ehca_free_fw_ctrlblock(qpcb);

	return ret;
}

static int internal_destroy_qp(struct ib_device *dev, struct ehca_qp *my_qp,
			       struct ib_uobject *uobject)
{
	struct ehca_shca *shca = container_of(dev, struct ehca_shca, ib_device);
	struct ehca_pd *my_pd = container_of(my_qp->ib_qp.pd, struct ehca_pd,
					     ib_pd);
	struct ehca_sport *sport = &shca->sport[my_qp->init_attr.port_num - 1];
	u32 cur_pid = current->tgid;
	u32 qp_num = my_qp->real_qp_num;
	int ret;
	u64 h_ret;
	u8 port_num;
	enum ib_qp_type	qp_type;
	unsigned long flags;

	if (uobject) {
		if (my_qp->mm_count_galpa ||
		    my_qp->mm_count_rqueue || my_qp->mm_count_squeue) {
			ehca_err(dev, "Resources still referenced in "
				 "user space qp_num=%x", qp_num);
			return -EINVAL;
		}
		if (my_pd->ownpid != cur_pid) {
			ehca_err(dev, "Invalid caller pid=%x ownpid=%x",
				 cur_pid, my_pd->ownpid);
			return -EINVAL;
		}
	}

	if (my_qp->send_cq) {
		ret = ehca_cq_unassign_qp(my_qp->send_cq, qp_num);
		if (ret) {
			ehca_err(dev, "Couldn't unassign qp from "
				 "send_cq ret=%i qp_num=%x cq_num=%x", ret,
				 qp_num, my_qp->send_cq->cq_number);
			return ret;
		}
	}

	write_lock_irqsave(&ehca_qp_idr_lock, flags);
	idr_remove(&ehca_qp_idr, my_qp->token);
	write_unlock_irqrestore(&ehca_qp_idr_lock, flags);

	h_ret = hipz_h_destroy_qp(shca->ipz_hca_handle, my_qp);
	if (h_ret != H_SUCCESS) {
		ehca_err(dev, "hipz_h_destroy_qp() failed h_ret=%li "
			 "ehca_qp=%p qp_num=%x", h_ret, my_qp, qp_num);
		return ehca2ib_return_code(h_ret);
	}

	port_num = my_qp->init_attr.port_num;
	qp_type  = my_qp->init_attr.qp_type;

	if (qp_type == IB_QPT_SMI || qp_type == IB_QPT_GSI) {
		spin_lock_irqsave(&sport->mod_sqp_lock, flags);
		kfree(my_qp->mod_qp_parm);
		my_qp->mod_qp_parm = NULL;
		shca->sport[port_num - 1].ibqp_sqp[qp_type] = NULL;
		spin_unlock_irqrestore(&sport->mod_sqp_lock, flags);
	}

	/* no support for IB_QPT_SMI yet */
	if (qp_type == IB_QPT_GSI) {
		struct ib_event event;
		ehca_info(dev, "device %s: port %x is inactive.",
			  shca->ib_device.name, port_num);
		event.device = &shca->ib_device;
		event.event = IB_EVENT_PORT_ERR;
		event.element.port_num = port_num;
		shca->sport[port_num - 1].port_state = IB_PORT_DOWN;
		ib_dispatch_event(&event);
	}

	if (HAS_RQ(my_qp))
		ipz_queue_dtor(my_pd, &my_qp->ipz_rqueue);
	if (HAS_SQ(my_qp))
		ipz_queue_dtor(my_pd, &my_qp->ipz_squeue);
	kmem_cache_free(qp_cache, my_qp);
	return 0;
}

int ehca_destroy_qp(struct ib_qp *qp)
{
	return internal_destroy_qp(qp->device,
				   container_of(qp, struct ehca_qp, ib_qp),
				   qp->uobject);
}

int ehca_destroy_srq(struct ib_srq *srq)
{
	return internal_destroy_qp(srq->device,
				   container_of(srq, struct ehca_qp, ib_srq),
				   srq->uobject);
}

int ehca_init_qp_cache(void)
{
	qp_cache = kmem_cache_create("ehca_cache_qp",
				     sizeof(struct ehca_qp), 0,
				     SLAB_HWCACHE_ALIGN,
				     NULL);
	if (!qp_cache)
		return -ENOMEM;
	return 0;
}

void ehca_cleanup_qp_cache(void)
{
	if (qp_cache)
		kmem_cache_destroy(qp_cache);
}
