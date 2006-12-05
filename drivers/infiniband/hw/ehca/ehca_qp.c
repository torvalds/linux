/*
 *  IBM eServer eHCA Infiniband device driver for Linux on POWER
 *
 *  QP functions
 *
 *  Authors: Waleri Fomin <fomin@de.ibm.com>
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

enum ehca_service_type {
	ST_RC = 0,
	ST_UC = 1,
	ST_RD = 2,
	ST_UD = 3
};

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
 * init_qp_queues initializes/constructs r/squeue and registers queue pages.
 */
static inline int init_qp_queues(struct ehca_shca *shca,
				 struct ehca_qp *my_qp,
				 int nr_sq_pages,
				 int nr_rq_pages,
				 int swqe_size,
				 int rwqe_size,
				 int nr_send_sges, int nr_receive_sges)
{
	int ret, cnt, ipz_rc;
	void *vpage;
	u64 rpage, h_ret;
	struct ib_device *ib_dev = &shca->ib_device;
	struct ipz_adapter_handle ipz_hca_handle = shca->ipz_hca_handle;

	ipz_rc = ipz_queue_ctor(&my_qp->ipz_squeue,
				nr_sq_pages,
				EHCA_PAGESIZE, swqe_size, nr_send_sges);
	if (!ipz_rc) {
		ehca_err(ib_dev,"Cannot allocate page for squeue. ipz_rc=%x",
			 ipz_rc);
		return -EBUSY;
	}

	ipz_rc = ipz_queue_ctor(&my_qp->ipz_rqueue,
				nr_rq_pages,
				EHCA_PAGESIZE, rwqe_size, nr_receive_sges);
	if (!ipz_rc) {
		ehca_err(ib_dev, "Cannot allocate page for rqueue. ipz_rc=%x",
			 ipz_rc);
		ret = -EBUSY;
		goto init_qp_queues0;
	}
	/* register SQ pages */
	for (cnt = 0; cnt < nr_sq_pages; cnt++) {
		vpage = ipz_qpageit_get_inc(&my_qp->ipz_squeue);
		if (!vpage) {
			ehca_err(ib_dev, "SQ ipz_qpageit_get_inc() "
				 "failed p_vpage= %p", vpage);
			ret = -EINVAL;
			goto init_qp_queues1;
		}
		rpage = virt_to_abs(vpage);

		h_ret = hipz_h_register_rpage_qp(ipz_hca_handle,
						 my_qp->ipz_qp_handle,
						 &my_qp->pf, 0, 0,
						 rpage, 1,
						 my_qp->galpas.kernel);
		if (h_ret < H_SUCCESS) {
			ehca_err(ib_dev, "SQ hipz_qp_register_rpage()"
				 " failed rc=%lx", h_ret);
			ret = ehca2ib_return_code(h_ret);
			goto init_qp_queues1;
		}
	}

	ipz_qeit_reset(&my_qp->ipz_squeue);

	/* register RQ pages */
	for (cnt = 0; cnt < nr_rq_pages; cnt++) {
		vpage = ipz_qpageit_get_inc(&my_qp->ipz_rqueue);
		if (!vpage) {
			ehca_err(ib_dev, "RQ ipz_qpageit_get_inc() "
				 "failed p_vpage = %p", vpage);
			ret = -EINVAL;
			goto init_qp_queues1;
		}

		rpage = virt_to_abs(vpage);

		h_ret = hipz_h_register_rpage_qp(ipz_hca_handle,
						 my_qp->ipz_qp_handle,
						 &my_qp->pf, 0, 1,
						 rpage, 1,my_qp->galpas.kernel);
		if (h_ret < H_SUCCESS) {
			ehca_err(ib_dev, "RQ hipz_qp_register_rpage() failed "
				 "rc=%lx", h_ret);
			ret = ehca2ib_return_code(h_ret);
			goto init_qp_queues1;
		}
		if (cnt == (nr_rq_pages - 1)) {	/* last page! */
			if (h_ret != H_SUCCESS) {
				ehca_err(ib_dev, "RQ hipz_qp_register_rpage() "
					 "h_ret= %lx ", h_ret);
				ret = ehca2ib_return_code(h_ret);
				goto init_qp_queues1;
			}
			vpage = ipz_qpageit_get_inc(&my_qp->ipz_rqueue);
			if (vpage) {
				ehca_err(ib_dev, "ipz_qpageit_get_inc() "
					 "should not succeed vpage=%p", vpage);
				ret = -EINVAL;
				goto init_qp_queues1;
			}
		} else {
			if (h_ret != H_PAGE_REGISTERED) {
				ehca_err(ib_dev, "RQ hipz_qp_register_rpage() "
					 "h_ret= %lx ", h_ret);
				ret = ehca2ib_return_code(h_ret);
				goto init_qp_queues1;
			}
		}
	}

	ipz_qeit_reset(&my_qp->ipz_rqueue);

	return 0;

init_qp_queues1:
	ipz_queue_dtor(&my_qp->ipz_rqueue);
init_qp_queues0:
	ipz_queue_dtor(&my_qp->ipz_squeue);
	return ret;
}

struct ib_qp *ehca_create_qp(struct ib_pd *pd,
			     struct ib_qp_init_attr *init_attr,
			     struct ib_udata *udata)
{
	static int da_rc_msg_size[]={ 128, 256, 512, 1024, 2048, 4096 };
	static int da_ud_sq_msg_size[]={ 128, 384, 896, 1920, 3968 };
	struct ehca_qp *my_qp;
	struct ehca_pd *my_pd = container_of(pd, struct ehca_pd, ib_pd);
	struct ehca_shca *shca = container_of(pd->device, struct ehca_shca,
					      ib_device);
	struct ib_ucontext *context = NULL;
	u64 h_ret;
	int max_send_sge, max_recv_sge, ret;

	/* h_call's out parameters */
	struct ehca_alloc_qp_parms parms;
	u32 swqe_size = 0, rwqe_size = 0;
	u8 daqp_completion, isdaqp;
	unsigned long flags;

	if (init_attr->sq_sig_type != IB_SIGNAL_REQ_WR &&
		init_attr->sq_sig_type != IB_SIGNAL_ALL_WR) {
		ehca_err(pd->device, "init_attr->sg_sig_type=%x not allowed",
			 init_attr->sq_sig_type);
		return ERR_PTR(-EINVAL);
	}

	/* save daqp completion bits */
	daqp_completion = init_attr->qp_type & 0x60;
	/* save daqp bit */
	isdaqp = (init_attr->qp_type & 0x80) ? 1 : 0;
	init_attr->qp_type = init_attr->qp_type & 0x1F;

	if (init_attr->qp_type != IB_QPT_UD &&
	    init_attr->qp_type != IB_QPT_SMI &&
	    init_attr->qp_type != IB_QPT_GSI &&
	    init_attr->qp_type != IB_QPT_UC &&
	    init_attr->qp_type != IB_QPT_RC) {
		ehca_err(pd->device, "wrong QP Type=%x", init_attr->qp_type);
		return ERR_PTR(-EINVAL);
	}
	if ((init_attr->qp_type != IB_QPT_RC && init_attr->qp_type != IB_QPT_UD)
	    && isdaqp) {
		ehca_err(pd->device, "unsupported LL QP Type=%x",
			 init_attr->qp_type);
		return ERR_PTR(-EINVAL);
	} else if (init_attr->qp_type == IB_QPT_RC && isdaqp &&
		   (init_attr->cap.max_send_wr > 255 ||
		    init_attr->cap.max_recv_wr > 255 )) {
		       ehca_err(pd->device, "Invalid Number of max_sq_wr =%x "
				"or max_rq_wr=%x for QP Type=%x",
				init_attr->cap.max_send_wr,
				init_attr->cap.max_recv_wr,init_attr->qp_type);
		       return ERR_PTR(-EINVAL);
	} else if (init_attr->qp_type == IB_QPT_UD && isdaqp &&
		  init_attr->cap.max_send_wr > 255) {
		ehca_err(pd->device,
			 "Invalid Number of max_send_wr=%x for UD QP_TYPE=%x",
			 init_attr->cap.max_send_wr, init_attr->qp_type);
		return ERR_PTR(-EINVAL);
	}

	if (pd->uobject && udata)
		context = pd->uobject->context;

	my_qp = kmem_cache_alloc(qp_cache, SLAB_KERNEL);
	if (!my_qp) {
		ehca_err(pd->device, "pd=%p not enough memory to alloc qp", pd);
		return ERR_PTR(-ENOMEM);
	}

	memset(my_qp, 0, sizeof(struct ehca_qp));
	memset (&parms, 0, sizeof(struct ehca_alloc_qp_parms));
	spin_lock_init(&my_qp->spinlock_s);
	spin_lock_init(&my_qp->spinlock_r);

	my_qp->recv_cq =
		container_of(init_attr->recv_cq, struct ehca_cq, ib_cq);
	my_qp->send_cq =
		container_of(init_attr->send_cq, struct ehca_cq, ib_cq);

	my_qp->init_attr = *init_attr;

	do {
		if (!idr_pre_get(&ehca_qp_idr, GFP_KERNEL)) {
			ret = -ENOMEM;
			ehca_err(pd->device, "Can't reserve idr resources.");
			goto create_qp_exit0;
		}

		spin_lock_irqsave(&ehca_qp_idr_lock, flags);
		ret = idr_get_new(&ehca_qp_idr, my_qp, &my_qp->token);
		spin_unlock_irqrestore(&ehca_qp_idr_lock, flags);

	} while (ret == -EAGAIN);

	if (ret) {
		ret = -ENOMEM;
		ehca_err(pd->device, "Can't allocate new idr entry.");
		goto create_qp_exit0;
	}

	parms.servicetype = ibqptype2servicetype(init_attr->qp_type);
	if (parms.servicetype < 0) {
		ret = -EINVAL;
		ehca_err(pd->device, "Invalid qp_type=%x", init_attr->qp_type);
		goto create_qp_exit0;
	}

	if (init_attr->sq_sig_type == IB_SIGNAL_ALL_WR)
		parms.sigtype = HCALL_SIGT_EVERY;
	else
		parms.sigtype = HCALL_SIGT_BY_WQE;

	/* UD_AV CIRCUMVENTION */
	max_send_sge = init_attr->cap.max_send_sge;
	max_recv_sge = init_attr->cap.max_recv_sge;
	if (IB_QPT_UD == init_attr->qp_type ||
	    IB_QPT_GSI == init_attr->qp_type ||
	    IB_QPT_SMI == init_attr->qp_type) {
		max_send_sge += 2;
		max_recv_sge += 2;
	}

	parms.ipz_eq_handle = shca->eq.ipz_eq_handle;
	parms.daqp_ctrl = isdaqp | daqp_completion;
	parms.pd = my_pd->fw_pd;
	parms.max_recv_sge = max_recv_sge;
	parms.max_send_sge = max_send_sge;

	h_ret = hipz_h_alloc_resource_qp(shca->ipz_hca_handle, my_qp, &parms);

	if (h_ret != H_SUCCESS) {
		ehca_err(pd->device, "h_alloc_resource_qp() failed h_ret=%lx",
			 h_ret);
		ret = ehca2ib_return_code(h_ret);
		goto create_qp_exit1;
	}

	switch (init_attr->qp_type) {
	case IB_QPT_RC:
	        if (isdaqp == 0) {
			swqe_size = offsetof(struct ehca_wqe, u.nud.sg_list[
					     (parms.act_nr_send_sges)]);
			rwqe_size = offsetof(struct ehca_wqe, u.nud.sg_list[
					     (parms.act_nr_recv_sges)]);
		} else { /* for daqp we need to use msg size, not wqe size */
		        swqe_size = da_rc_msg_size[max_send_sge];
			rwqe_size = da_rc_msg_size[max_recv_sge];
			parms.act_nr_send_sges = 1;
			parms.act_nr_recv_sges = 1;
		}
		break;
	case IB_QPT_UC:
		swqe_size = offsetof(struct ehca_wqe,
				     u.nud.sg_list[parms.act_nr_send_sges]);
		rwqe_size = offsetof(struct ehca_wqe,
				     u.nud.sg_list[parms.act_nr_recv_sges]);
		break;

	case IB_QPT_UD:
	case IB_QPT_GSI:
	case IB_QPT_SMI:
		/* UD circumvention */
		parms.act_nr_recv_sges -= 2;
		parms.act_nr_send_sges -= 2;
		if (isdaqp) {
		        swqe_size = da_ud_sq_msg_size[max_send_sge];
			rwqe_size = da_rc_msg_size[max_recv_sge];
			parms.act_nr_send_sges = 1;
			parms.act_nr_recv_sges = 1;
		} else {
			swqe_size = offsetof(struct ehca_wqe,
					     u.ud_av.sg_list[parms.act_nr_send_sges]);
			rwqe_size = offsetof(struct ehca_wqe,
					     u.ud_av.sg_list[parms.act_nr_recv_sges]);
		}

		if (IB_QPT_GSI == init_attr->qp_type ||
		    IB_QPT_SMI == init_attr->qp_type) {
			parms.act_nr_send_wqes = init_attr->cap.max_send_wr;
			parms.act_nr_recv_wqes = init_attr->cap.max_recv_wr;
			parms.act_nr_send_sges = init_attr->cap.max_send_sge;
			parms.act_nr_recv_sges = init_attr->cap.max_recv_sge;
			my_qp->real_qp_num =
				(init_attr->qp_type == IB_QPT_SMI) ? 0 : 1;
		}

		break;

	default:
		break;
	}

	/* initializes r/squeue and registers queue pages */
	ret = init_qp_queues(shca, my_qp,
			     parms.nr_sq_pages, parms.nr_rq_pages,
			     swqe_size, rwqe_size,
			     parms.act_nr_send_sges, parms.act_nr_recv_sges);
	if (ret) {
		ehca_err(pd->device,
			 "Couldn't initialize r/squeue and pages ret=%x", ret);
		goto create_qp_exit2;
	}

	my_qp->ib_qp.pd = &my_pd->ib_pd;
	my_qp->ib_qp.device = my_pd->ib_pd.device;

	my_qp->ib_qp.recv_cq = init_attr->recv_cq;
	my_qp->ib_qp.send_cq = init_attr->send_cq;

	my_qp->ib_qp.qp_num = my_qp->real_qp_num;
	my_qp->ib_qp.qp_type = init_attr->qp_type;

	my_qp->qp_type = init_attr->qp_type;
	my_qp->ib_qp.srq = init_attr->srq;

	my_qp->ib_qp.qp_context = init_attr->qp_context;
	my_qp->ib_qp.event_handler = init_attr->event_handler;

	init_attr->cap.max_inline_data = 0; /* not supported yet */
	init_attr->cap.max_recv_sge = parms.act_nr_recv_sges;
	init_attr->cap.max_recv_wr = parms.act_nr_recv_wqes;
	init_attr->cap.max_send_sge = parms.act_nr_send_sges;
	init_attr->cap.max_send_wr = parms.act_nr_send_wqes;

	/* NOTE: define_apq0() not supported yet */
	if (init_attr->qp_type == IB_QPT_GSI) {
		h_ret = ehca_define_sqp(shca, my_qp, init_attr);
		if (h_ret != H_SUCCESS) {
			ehca_err(pd->device, "ehca_define_sqp() failed rc=%lx",
				 h_ret);
			ret = ehca2ib_return_code(h_ret);
			goto create_qp_exit3;
		}
	}
	if (init_attr->send_cq) {
		struct ehca_cq *cq = container_of(init_attr->send_cq,
						  struct ehca_cq, ib_cq);
		ret = ehca_cq_assign_qp(cq, my_qp);
		if (ret) {
			ehca_err(pd->device, "Couldn't assign qp to send_cq ret=%x",
				 ret);
			goto create_qp_exit3;
		}
		my_qp->send_cq = cq;
	}
	/* copy queues, galpa data to user space */
	if (context && udata) {
		struct ipz_queue *ipz_rqueue = &my_qp->ipz_rqueue;
		struct ipz_queue *ipz_squeue = &my_qp->ipz_squeue;
		struct ehca_create_qp_resp resp;
		struct vm_area_struct * vma;
		memset(&resp, 0, sizeof(resp));

		resp.qp_num = my_qp->real_qp_num;
		resp.token = my_qp->token;
		resp.qp_type = my_qp->qp_type;
		resp.qkey = my_qp->qkey;
		resp.real_qp_num = my_qp->real_qp_num;
		/* rqueue properties */
		resp.ipz_rqueue.qe_size = ipz_rqueue->qe_size;
		resp.ipz_rqueue.act_nr_of_sg = ipz_rqueue->act_nr_of_sg;
		resp.ipz_rqueue.queue_length = ipz_rqueue->queue_length;
		resp.ipz_rqueue.pagesize = ipz_rqueue->pagesize;
		resp.ipz_rqueue.toggle_state = ipz_rqueue->toggle_state;
		ret = ehca_mmap_nopage(((u64)(my_qp->token) << 32) | 0x22000000,
				       ipz_rqueue->queue_length,
				       (void**)&resp.ipz_rqueue.queue,
				       &vma);
		if (ret) {
			ehca_err(pd->device, "Could not mmap rqueue pages");
			goto create_qp_exit3;
		}
		my_qp->uspace_rqueue = resp.ipz_rqueue.queue;
		/* squeue properties */
		resp.ipz_squeue.qe_size = ipz_squeue->qe_size;
		resp.ipz_squeue.act_nr_of_sg = ipz_squeue->act_nr_of_sg;
		resp.ipz_squeue.queue_length = ipz_squeue->queue_length;
		resp.ipz_squeue.pagesize = ipz_squeue->pagesize;
		resp.ipz_squeue.toggle_state = ipz_squeue->toggle_state;
		ret = ehca_mmap_nopage(((u64)(my_qp->token) << 32) | 0x23000000,
				       ipz_squeue->queue_length,
				       (void**)&resp.ipz_squeue.queue,
				       &vma);
		if (ret) {
			ehca_err(pd->device, "Could not mmap squeue pages");
			goto create_qp_exit4;
		}
		my_qp->uspace_squeue = resp.ipz_squeue.queue;
		/* fw_handle */
		resp.galpas = my_qp->galpas;
		ret = ehca_mmap_register(my_qp->galpas.user.fw_handle,
					 (void**)&resp.galpas.kernel.fw_handle,
					 &vma);
		if (ret) {
			ehca_err(pd->device, "Could not mmap fw_handle");
			goto create_qp_exit5;
		}
		my_qp->uspace_fwh = (u64)resp.galpas.kernel.fw_handle;

		if (ib_copy_to_udata(udata, &resp, sizeof resp)) {
			ehca_err(pd->device, "Copy to udata failed");
			ret = -EINVAL;
			goto create_qp_exit6;
		}
	}

	return &my_qp->ib_qp;

create_qp_exit6:
	ehca_munmap(my_qp->uspace_fwh, EHCA_PAGESIZE);

create_qp_exit5:
	ehca_munmap(my_qp->uspace_squeue, my_qp->ipz_squeue.queue_length);

create_qp_exit4:
	ehca_munmap(my_qp->uspace_rqueue, my_qp->ipz_rqueue.queue_length);

create_qp_exit3:
	ipz_queue_dtor(&my_qp->ipz_rqueue);
	ipz_queue_dtor(&my_qp->ipz_squeue);

create_qp_exit2:
	hipz_h_destroy_qp(shca->ipz_hca_handle, my_qp);

create_qp_exit1:
	spin_lock_irqsave(&ehca_qp_idr_lock, flags);
	idr_remove(&ehca_qp_idr, my_qp->token);
	spin_unlock_irqrestore(&ehca_qp_idr_lock, flags);

create_qp_exit0:
	kmem_cache_free(qp_cache, my_qp);
	return ERR_PTR(ret);
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
			 " ehca_qp=%p qp_num=%x h_ret=%lx",
			 my_qp, qp_num, h_ret);
		return ehca2ib_return_code(h_ret);
	}
	bad_send_wqe_p = (void*)((u64)bad_send_wqe_p & (~(1L<<63)));
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
	wqe = (struct ehca_wqe*)ipz_qeit_calc(squeue, q_ofs);
	*bad_wqe_cnt = 0;
	while (wqe->optype != 0xff && wqe->wqef != 0xff) {
		if (ehca_debug_level)
			ehca_dmp(wqe, 32, "qp_num=%x wqe", qp_num);
		wqe->nr_of_data_seg = 0; /* suppress data access */
		wqe->wqef = WQEF_PURGE; /* WQE to be purged */
		q_ofs = ipz_queue_advance_offset(squeue, q_ofs);
		wqe = (struct ehca_wqe*)ipz_qeit_calc(squeue, q_ofs);
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
	unsigned long spl_flags = 0;

	/* do query_qp to obtain current attr values */
	mqpcb = ehca_alloc_fw_ctrlblock();
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
			 "ehca_qp=%p qp_num=%x h_ret=%lx",
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
				 "ehca_modify_qp() rc=%x", smirc);
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

	ehca_dbg(ibqp->device,"ehca_qp=%p qp_num=%x current qp_state=%x "
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

	if ((mqpcb->qp_state = ib2ehca_qp_state(qp_new_state)))
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

	/* sqe -> rts: set purge bit of bad wqe before actual trans */
	if ((my_qp->qp_type == IB_QPT_UD ||
	     my_qp->qp_type == IB_QPT_GSI ||
	     my_qp->qp_type == IB_QPT_SMI) &&
	    statetrans == IB_QPST_SQE2RTS) {
		/* mark next free wqe if kernel */
		if (my_qp->uspace_squeue == 0) {
			struct ehca_wqe *wqe;
			/* lock send queue */
			spin_lock_irqsave(&my_qp->spinlock_s, spl_flags);
			squeue_locked = 1;
			/* mark next free wqe */
			wqe = (struct ehca_wqe*)
				ipz_qeit_get(&my_qp->ipz_squeue);
			wqe->optype = wqe->wqef = 0xff;
			ehca_dbg(ibqp->device, "qp_num=%x next_free_wqe=%p",
				 ibqp->qp_num, wqe);
		}
		ret = prepare_sqe_rts(my_qp, shca, &bad_wqe_cnt);
		if (ret) {
			ehca_err(ibqp->device, "prepare_sqe_rts() failed "
				 "ehca_qp=%p qp_num=%x ret=%x",
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
		mqpcb->prim_p_key_idx = attr->pkey_index;
		update_mask |= EHCA_BMASK_SET(MQPCB_MASK_PRIM_P_KEY_IDX, 1);
	}
	if (attr_mask & IB_QP_PORT) {
		if (attr->port_num < 1 || attr->port_num > shca->num_ports) {
			ret = -EINVAL;
			ehca_err(ibqp->device, "Invalid port=%x. "
				 "ehca_qp=%p qp_num=%x num_ports=%x",
				 attr->port_num, my_qp, ibqp->qp_num,
				 shca->num_ports);
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
		int ah_mult = ib_rate_to_mult(attr->ah_attr.static_rate);
		int ehca_mult = ib_rate_to_mult(shca->sport[my_qp->
						init_attr.port_num].rate);

		mqpcb->dlid = attr->ah_attr.dlid;
		update_mask |= EHCA_BMASK_SET(MQPCB_MASK_DLID, 1);
		mqpcb->source_path_bits = attr->ah_attr.src_path_bits;
		update_mask |= EHCA_BMASK_SET(MQPCB_MASK_SOURCE_PATH_BITS, 1);
		mqpcb->service_level = attr->ah_attr.sl;
		update_mask |= EHCA_BMASK_SET(MQPCB_MASK_SERVICE_LEVEL, 1);

		if (ah_mult < ehca_mult)
			mqpcb->max_static_rate = (ah_mult > 0) ?
			((ehca_mult - 1) / ah_mult) : 0;
		else
			mqpcb->max_static_rate = 0;

		update_mask |= EHCA_BMASK_SET(MQPCB_MASK_MAX_STATIC_RATE, 1);

		/*
		 * only if GRH is TRUE we might consider SOURCE_GID_IDX
		 * and DEST_GID otherwise phype will return H_ATTR_PARM!!!
		 */
		if (attr->ah_attr.ah_flags == IB_AH_GRH) {
			mqpcb->send_grh_flag = 1 << 31;
			update_mask |=
				EHCA_BMASK_SET(MQPCB_MASK_SEND_GRH_FLAG, 1);
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
		int ah_mult = ib_rate_to_mult(attr->alt_ah_attr.static_rate);
		int ehca_mult = ib_rate_to_mult(
			shca->sport[my_qp->init_attr.port_num].rate);

		mqpcb->dlid_al = attr->alt_ah_attr.dlid;
		update_mask |= EHCA_BMASK_SET(MQPCB_MASK_DLID_AL, 1);
		mqpcb->source_path_bits_al = attr->alt_ah_attr.src_path_bits;
		update_mask |=
			EHCA_BMASK_SET(MQPCB_MASK_SOURCE_PATH_BITS_AL, 1);
		mqpcb->service_level_al = attr->alt_ah_attr.sl;
		update_mask |= EHCA_BMASK_SET(MQPCB_MASK_SERVICE_LEVEL_AL, 1);

		if (ah_mult < ehca_mult)
			mqpcb->max_static_rate = (ah_mult > 0) ?
			((ehca_mult - 1) / ah_mult) : 0;
		else
			mqpcb->max_static_rate_al = 0;

		update_mask |= EHCA_BMASK_SET(MQPCB_MASK_MAX_STATIC_RATE_AL, 1);

		/*
		 * only if GRH is TRUE we might consider SOURCE_GID_IDX
		 * and DEST_GID otherwise phype will return H_ATTR_PARM!!!
		 */
		if (attr->alt_ah_attr.ah_flags == IB_AH_GRH) {
			mqpcb->send_grh_flag_al = 1 << 31;
			update_mask |=
				EHCA_BMASK_SET(MQPCB_MASK_SEND_GRH_FLAG_AL, 1);
			mqpcb->source_gid_idx_al =
				attr->alt_ah_attr.grh.sgid_index;
			update_mask |=
				EHCA_BMASK_SET(MQPCB_MASK_SOURCE_GID_IDX_AL, 1);

			for (cnt = 0; cnt < 16; cnt++)
				mqpcb->dest_gid_al.byte[cnt] =
					attr->alt_ah_attr.grh.dgid.raw[cnt];

			update_mask |=
				EHCA_BMASK_SET(MQPCB_MASK_DEST_GID_AL, 1);
			mqpcb->flow_label_al = attr->alt_ah_attr.grh.flow_label;
			update_mask |=
				EHCA_BMASK_SET(MQPCB_MASK_FLOW_LABEL_AL, 1);
			mqpcb->hop_limit_al = attr->alt_ah_attr.grh.hop_limit;
			update_mask |=
				EHCA_BMASK_SET(MQPCB_MASK_HOP_LIMIT_AL, 1);
			mqpcb->traffic_class_al =
				attr->alt_ah_attr.grh.traffic_class;
			update_mask |=
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
		mqpcb->path_migration_state = attr->path_mig_state;
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
		ehca_err(ibqp->device, "hipz_h_modify_qp() failed rc=%lx "
			 "ehca_qp=%p qp_num=%x",h_ret, my_qp, ibqp->qp_num);
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
				 "a LID h_ret=%lx ehca_qp=%p qp_num=%x",
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
		spin_unlock_irqrestore(&my_qp->spinlock_s, spl_flags);
		my_qp->sqerr_purgeflag = 1;
	}

modify_qp_exit1:
	ehca_free_fw_ctrlblock(mqpcb);

	return ret;
}

int ehca_modify_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr, int attr_mask,
		   struct ib_udata *udata)
{
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

	return internal_modify_qp(ibqp, attr, attr_mask, 0);
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
		ehca_err(qp->device,"Invalid attribute mask "
			 "ehca_qp=%p qp_num=%x qp_attr_mask=%x ",
			 my_qp, qp->qp_num, qp_attr_mask);
		return -EINVAL;
	}

	qpcb = ehca_alloc_fw_ctrlblock();
	if (!qpcb) {
		ehca_err(qp->device,"Out of memory for qpcb "
			 "ehca_qp=%p qp_num=%x", my_qp, qp->qp_num);
		return -ENOMEM;
	}

	h_ret = hipz_h_query_qp(adapter_handle,
				my_qp->ipz_qp_handle,
				&my_qp->pf,
				qpcb, my_qp->galpas.kernel);

	if (h_ret != H_SUCCESS) {
		ret = ehca2ib_return_code(h_ret);
		ehca_err(qp->device,"hipz_h_query_qp() failed "
			 "ehca_qp=%p qp_num=%x h_ret=%lx",
			 my_qp, qp->qp_num, h_ret);
		goto query_qp_exit1;
	}

	qp_attr->cur_qp_state = ehca2ib_qp_state(qpcb->qp_state);
	qp_attr->qp_state = qp_attr->cur_qp_state;

	if (qp_attr->cur_qp_state == -EINVAL) {
		ret = -EINVAL;
		ehca_err(qp->device,"Got invalid ehca_qp_state=%x "
			 "ehca_qp=%p qp_num=%x",
			 qpcb->qp_state, my_qp, qp->qp_num);
		goto query_qp_exit1;
	}

	if (qp_attr->qp_state == IB_QPS_SQD)
		qp_attr->sq_draining = 1;

	qp_attr->qkey = qpcb->qkey;
	qp_attr->path_mtu = qpcb->path_mtu;
	qp_attr->path_mig_state = qpcb->path_migration_state;
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

int ehca_destroy_qp(struct ib_qp *ibqp)
{
	struct ehca_qp *my_qp = container_of(ibqp, struct ehca_qp, ib_qp);
	struct ehca_shca *shca = container_of(ibqp->device, struct ehca_shca,
					      ib_device);
	struct ehca_pd *my_pd = container_of(my_qp->ib_qp.pd, struct ehca_pd,
					     ib_pd);
	u32 cur_pid = current->tgid;
	u32 qp_num = ibqp->qp_num;
	int ret;
	u64 h_ret;
	u8 port_num;
	enum ib_qp_type	qp_type;
	unsigned long flags;

	if (my_pd->ib_pd.uobject && my_pd->ib_pd.uobject->context &&
	    my_pd->ownpid != cur_pid) {
		ehca_err(ibqp->device, "Invalid caller pid=%x ownpid=%x",
			 cur_pid, my_pd->ownpid);
		return -EINVAL;
	}

	if (my_qp->send_cq) {
		ret = ehca_cq_unassign_qp(my_qp->send_cq,
					      my_qp->real_qp_num);
		if (ret) {
			ehca_err(ibqp->device, "Couldn't unassign qp from "
				 "send_cq ret=%x qp_num=%x cq_num=%x", ret,
				 my_qp->ib_qp.qp_num, my_qp->send_cq->cq_number);
			return ret;
		}
	}

	spin_lock_irqsave(&ehca_qp_idr_lock, flags);
	idr_remove(&ehca_qp_idr, my_qp->token);
	spin_unlock_irqrestore(&ehca_qp_idr_lock, flags);

	/* un-mmap if vma alloc */
	if (my_qp->uspace_rqueue) {
		ret = ehca_munmap(my_qp->uspace_rqueue,
				  my_qp->ipz_rqueue.queue_length);
		if (ret)
			ehca_err(ibqp->device, "Could not munmap rqueue "
				 "qp_num=%x", qp_num);
		ret = ehca_munmap(my_qp->uspace_squeue,
				  my_qp->ipz_squeue.queue_length);
		if (ret)
			ehca_err(ibqp->device, "Could not munmap squeue "
				 "qp_num=%x", qp_num);
		ret = ehca_munmap(my_qp->uspace_fwh, EHCA_PAGESIZE);
		if (ret)
			ehca_err(ibqp->device, "Could not munmap fwh qp_num=%x",
				 qp_num);
	}

	h_ret = hipz_h_destroy_qp(shca->ipz_hca_handle, my_qp);
	if (h_ret != H_SUCCESS) {
		ehca_err(ibqp->device, "hipz_h_destroy_qp() failed rc=%lx "
			 "ehca_qp=%p qp_num=%x", h_ret, my_qp, qp_num);
		return ehca2ib_return_code(h_ret);
	}

	port_num = my_qp->init_attr.port_num;
	qp_type  = my_qp->init_attr.qp_type;

	/* no support for IB_QPT_SMI yet */
	if (qp_type == IB_QPT_GSI) {
		struct ib_event event;
		ehca_info(ibqp->device, "device %s: port %x is inactive.",
			  shca->ib_device.name, port_num);
		event.device = &shca->ib_device;
		event.event = IB_EVENT_PORT_ERR;
		event.element.port_num = port_num;
		shca->sport[port_num - 1].port_state = IB_PORT_DOWN;
		ib_dispatch_event(&event);
	}

	ipz_queue_dtor(&my_qp->ipz_rqueue);
	ipz_queue_dtor(&my_qp->ipz_squeue);
	kmem_cache_free(qp_cache, my_qp);
	return 0;
}

int ehca_init_qp_cache(void)
{
	qp_cache = kmem_cache_create("ehca_cache_qp",
				     sizeof(struct ehca_qp), 0,
				     SLAB_HWCACHE_ALIGN,
				     NULL, NULL);
	if (!qp_cache)
		return -ENOMEM;
	return 0;
}

void ehca_cleanup_qp_cache(void)
{
	if (qp_cache)
		kmem_cache_destroy(qp_cache);
}
