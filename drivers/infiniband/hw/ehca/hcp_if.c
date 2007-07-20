/*
 *  IBM eServer eHCA Infiniband device driver for Linux on POWER
 *
 *  Firmware Infiniband Interface code for POWER
 *
 *  Authors: Christoph Raisch <raisch@de.ibm.com>
 *           Hoang-Nam Nguyen <hnguyen@de.ibm.com>
 *           Joachim Fenkes <fenkes@de.ibm.com>
 *           Gerd Bayer <gerd.bayer@de.ibm.com>
 *           Waleri Fomin <fomin@de.ibm.com>
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

#include <asm/hvcall.h>
#include "ehca_tools.h"
#include "hcp_if.h"
#include "hcp_phyp.h"
#include "hipz_fns.h"
#include "ipz_pt_fn.h"

#define H_ALL_RES_QP_ENHANCED_OPS       EHCA_BMASK_IBM(9, 11)
#define H_ALL_RES_QP_PTE_PIN            EHCA_BMASK_IBM(12, 12)
#define H_ALL_RES_QP_SERVICE_TYPE       EHCA_BMASK_IBM(13, 15)
#define H_ALL_RES_QP_STORAGE            EHCA_BMASK_IBM(16, 17)
#define H_ALL_RES_QP_LL_RQ_CQE_POSTING  EHCA_BMASK_IBM(18, 18)
#define H_ALL_RES_QP_LL_SQ_CQE_POSTING  EHCA_BMASK_IBM(19, 21)
#define H_ALL_RES_QP_SIGNALING_TYPE     EHCA_BMASK_IBM(22, 23)
#define H_ALL_RES_QP_UD_AV_LKEY_CTRL    EHCA_BMASK_IBM(31, 31)
#define H_ALL_RES_QP_SMALL_SQ_PAGE_SIZE EHCA_BMASK_IBM(32, 35)
#define H_ALL_RES_QP_SMALL_RQ_PAGE_SIZE EHCA_BMASK_IBM(36, 39)
#define H_ALL_RES_QP_RESOURCE_TYPE      EHCA_BMASK_IBM(56, 63)

#define H_ALL_RES_QP_MAX_OUTST_SEND_WR  EHCA_BMASK_IBM(0, 15)
#define H_ALL_RES_QP_MAX_OUTST_RECV_WR  EHCA_BMASK_IBM(16, 31)
#define H_ALL_RES_QP_MAX_SEND_SGE       EHCA_BMASK_IBM(32, 39)
#define H_ALL_RES_QP_MAX_RECV_SGE       EHCA_BMASK_IBM(40, 47)

#define H_ALL_RES_QP_UD_AV_LKEY         EHCA_BMASK_IBM(32, 63)
#define H_ALL_RES_QP_SRQ_QP_TOKEN       EHCA_BMASK_IBM(0, 31)
#define H_ALL_RES_QP_SRQ_QP_HANDLE      EHCA_BMASK_IBM(0, 64)
#define H_ALL_RES_QP_SRQ_LIMIT          EHCA_BMASK_IBM(48, 63)
#define H_ALL_RES_QP_SRQ_QPN            EHCA_BMASK_IBM(40, 63)

#define H_ALL_RES_QP_ACT_OUTST_SEND_WR  EHCA_BMASK_IBM(16, 31)
#define H_ALL_RES_QP_ACT_OUTST_RECV_WR  EHCA_BMASK_IBM(48, 63)
#define H_ALL_RES_QP_ACT_SEND_SGE       EHCA_BMASK_IBM(8, 15)
#define H_ALL_RES_QP_ACT_RECV_SGE       EHCA_BMASK_IBM(24, 31)

#define H_ALL_RES_QP_SQUEUE_SIZE_PAGES  EHCA_BMASK_IBM(0, 31)
#define H_ALL_RES_QP_RQUEUE_SIZE_PAGES  EHCA_BMASK_IBM(32, 63)

#define H_MP_INIT_TYPE                  EHCA_BMASK_IBM(44, 47)
#define H_MP_SHUTDOWN                   EHCA_BMASK_IBM(48, 48)
#define H_MP_RESET_QKEY_CTR             EHCA_BMASK_IBM(49, 49)

static DEFINE_SPINLOCK(hcall_lock);

static u32 get_longbusy_msecs(int longbusy_rc)
{
	switch (longbusy_rc) {
	case H_LONG_BUSY_ORDER_1_MSEC:
		return 1;
	case H_LONG_BUSY_ORDER_10_MSEC:
		return 10;
	case H_LONG_BUSY_ORDER_100_MSEC:
		return 100;
	case H_LONG_BUSY_ORDER_1_SEC:
		return 1000;
	case H_LONG_BUSY_ORDER_10_SEC:
		return 10000;
	case H_LONG_BUSY_ORDER_100_SEC:
		return 100000;
	default:
		return 1;
	}
}

static long ehca_plpar_hcall_norets(unsigned long opcode,
				    unsigned long arg1,
				    unsigned long arg2,
				    unsigned long arg3,
				    unsigned long arg4,
				    unsigned long arg5,
				    unsigned long arg6,
				    unsigned long arg7)
{
	long ret;
	int i, sleep_msecs;

	ehca_gen_dbg("opcode=%lx arg1=%lx arg2=%lx arg3=%lx arg4=%lx "
		     "arg5=%lx arg6=%lx arg7=%lx",
		     opcode, arg1, arg2, arg3, arg4, arg5, arg6, arg7);

	for (i = 0; i < 5; i++) {
		ret = plpar_hcall_norets(opcode, arg1, arg2, arg3, arg4,
					 arg5, arg6, arg7);

		if (H_IS_LONG_BUSY(ret)) {
			sleep_msecs = get_longbusy_msecs(ret);
			msleep_interruptible(sleep_msecs);
			continue;
		}

		if (ret < H_SUCCESS)
			ehca_gen_err("opcode=%lx ret=%lx"
				     " arg1=%lx arg2=%lx arg3=%lx arg4=%lx"
				     " arg5=%lx arg6=%lx arg7=%lx ",
				     opcode, ret,
				     arg1, arg2, arg3, arg4, arg5,
				     arg6, arg7);

		ehca_gen_dbg("opcode=%lx ret=%lx", opcode, ret);
		return ret;

	}

	return H_BUSY;
}

static long ehca_plpar_hcall9(unsigned long opcode,
			      unsigned long *outs, /* array of 9 outputs */
			      unsigned long arg1,
			      unsigned long arg2,
			      unsigned long arg3,
			      unsigned long arg4,
			      unsigned long arg5,
			      unsigned long arg6,
			      unsigned long arg7,
			      unsigned long arg8,
			      unsigned long arg9)
{
	long ret;
	int i, sleep_msecs, lock_is_set = 0;
	unsigned long flags = 0;

	ehca_gen_dbg("opcode=%lx arg1=%lx arg2=%lx arg3=%lx arg4=%lx "
		     "arg5=%lx arg6=%lx arg7=%lx arg8=%lx arg9=%lx",
		     opcode, arg1, arg2, arg3, arg4, arg5, arg6, arg7,
		     arg8, arg9);

	for (i = 0; i < 5; i++) {
		if ((opcode == H_ALLOC_RESOURCE) && (arg2 == 5)) {
			spin_lock_irqsave(&hcall_lock, flags);
			lock_is_set = 1;
		}

		ret = plpar_hcall9(opcode, outs,
				   arg1, arg2, arg3, arg4, arg5,
				   arg6, arg7, arg8, arg9);

		if (lock_is_set)
			spin_unlock_irqrestore(&hcall_lock, flags);

		if (H_IS_LONG_BUSY(ret)) {
			sleep_msecs = get_longbusy_msecs(ret);
			msleep_interruptible(sleep_msecs);
			continue;
		}

		if (ret < H_SUCCESS)
			ehca_gen_err("opcode=%lx ret=%lx"
				     " arg1=%lx arg2=%lx arg3=%lx arg4=%lx"
				     " arg5=%lx arg6=%lx arg7=%lx arg8=%lx"
				     " arg9=%lx"
				     " out1=%lx out2=%lx out3=%lx out4=%lx"
				     " out5=%lx out6=%lx out7=%lx out8=%lx"
				     " out9=%lx",
				     opcode, ret,
				     arg1, arg2, arg3, arg4, arg5,
				     arg6, arg7, arg8, arg9,
				     outs[0], outs[1], outs[2], outs[3],
				     outs[4], outs[5], outs[6], outs[7],
				     outs[8]);

		ehca_gen_dbg("opcode=%lx ret=%lx out1=%lx out2=%lx out3=%lx "
			     "out4=%lx out5=%lx out6=%lx out7=%lx out8=%lx "
			     "out9=%lx",
			     opcode, ret, outs[0], outs[1], outs[2], outs[3],
			     outs[4], outs[5], outs[6], outs[7], outs[8]);
		return ret;
	}

	return H_BUSY;
}

u64 hipz_h_alloc_resource_eq(const struct ipz_adapter_handle adapter_handle,
			     struct ehca_pfeq *pfeq,
			     const u32 neq_control,
			     const u32 number_of_entries,
			     struct ipz_eq_handle *eq_handle,
			     u32 *act_nr_of_entries,
			     u32 *act_pages,
			     u32 *eq_ist)
{
	u64 ret;
	u64 outs[PLPAR_HCALL9_BUFSIZE];
	u64 allocate_controls;

	/* resource type */
	allocate_controls = 3ULL;

	/* ISN is associated */
	if (neq_control != 1)
		allocate_controls = (1ULL << (63 - 7)) | allocate_controls;
	else /* notification event queue */
		allocate_controls = (1ULL << 63) | allocate_controls;

	ret = ehca_plpar_hcall9(H_ALLOC_RESOURCE, outs,
				adapter_handle.handle,  /* r4 */
				allocate_controls,      /* r5 */
				number_of_entries,      /* r6 */
				0, 0, 0, 0, 0, 0);
	eq_handle->handle = outs[0];
	*act_nr_of_entries = (u32)outs[3];
	*act_pages = (u32)outs[4];
	*eq_ist = (u32)outs[5];

	if (ret == H_NOT_ENOUGH_RESOURCES)
		ehca_gen_err("Not enough resource - ret=%lx ", ret);

	return ret;
}

u64 hipz_h_reset_event(const struct ipz_adapter_handle adapter_handle,
		       struct ipz_eq_handle eq_handle,
		       const u64 event_mask)
{
	return ehca_plpar_hcall_norets(H_RESET_EVENTS,
				       adapter_handle.handle, /* r4 */
				       eq_handle.handle,      /* r5 */
				       event_mask,	      /* r6 */
				       0, 0, 0, 0);
}

u64 hipz_h_alloc_resource_cq(const struct ipz_adapter_handle adapter_handle,
			     struct ehca_cq *cq,
			     struct ehca_alloc_cq_parms *param)
{
	u64 ret;
	u64 outs[PLPAR_HCALL9_BUFSIZE];

	ret = ehca_plpar_hcall9(H_ALLOC_RESOURCE, outs,
				adapter_handle.handle,   /* r4  */
				2,	                 /* r5  */
				param->eq_handle.handle, /* r6  */
				cq->token,	         /* r7  */
				param->nr_cqe,           /* r8  */
				0, 0, 0, 0);
	cq->ipz_cq_handle.handle = outs[0];
	param->act_nr_of_entries = (u32)outs[3];
	param->act_pages = (u32)outs[4];

	if (ret == H_SUCCESS)
		hcp_galpas_ctor(&cq->galpas, outs[5], outs[6]);

	if (ret == H_NOT_ENOUGH_RESOURCES)
		ehca_gen_err("Not enough resources. ret=%lx", ret);

	return ret;
}

u64 hipz_h_alloc_resource_qp(const struct ipz_adapter_handle adapter_handle,
			     struct ehca_alloc_qp_parms *parms)
{
	u64 ret;
	u64 allocate_controls, max_r10_reg, r11, r12;
	u64 outs[PLPAR_HCALL9_BUFSIZE];

	allocate_controls =
		EHCA_BMASK_SET(H_ALL_RES_QP_ENHANCED_OPS, parms->ext_type)
		| EHCA_BMASK_SET(H_ALL_RES_QP_PTE_PIN, 0)
		| EHCA_BMASK_SET(H_ALL_RES_QP_SERVICE_TYPE, parms->servicetype)
		| EHCA_BMASK_SET(H_ALL_RES_QP_SIGNALING_TYPE, parms->sigtype)
		| EHCA_BMASK_SET(H_ALL_RES_QP_STORAGE, parms->qp_storage)
		| EHCA_BMASK_SET(H_ALL_RES_QP_SMALL_SQ_PAGE_SIZE,
				 parms->squeue.page_size)
		| EHCA_BMASK_SET(H_ALL_RES_QP_SMALL_RQ_PAGE_SIZE,
				 parms->rqueue.page_size)
		| EHCA_BMASK_SET(H_ALL_RES_QP_LL_RQ_CQE_POSTING,
				 !!(parms->ll_comp_flags & LLQP_RECV_COMP))
		| EHCA_BMASK_SET(H_ALL_RES_QP_LL_SQ_CQE_POSTING,
				 !!(parms->ll_comp_flags & LLQP_SEND_COMP))
		| EHCA_BMASK_SET(H_ALL_RES_QP_UD_AV_LKEY_CTRL,
				 parms->ud_av_l_key_ctl)
		| EHCA_BMASK_SET(H_ALL_RES_QP_RESOURCE_TYPE, 1);

	max_r10_reg =
		EHCA_BMASK_SET(H_ALL_RES_QP_MAX_OUTST_SEND_WR,
			       parms->squeue.max_wr + 1)
		| EHCA_BMASK_SET(H_ALL_RES_QP_MAX_OUTST_RECV_WR,
				 parms->rqueue.max_wr + 1)
		| EHCA_BMASK_SET(H_ALL_RES_QP_MAX_SEND_SGE,
				 parms->squeue.max_sge)
		| EHCA_BMASK_SET(H_ALL_RES_QP_MAX_RECV_SGE,
				 parms->rqueue.max_sge);

	r11 = EHCA_BMASK_SET(H_ALL_RES_QP_SRQ_QP_TOKEN, parms->srq_token);

	if (parms->ext_type == EQPT_SRQ)
		r12 = EHCA_BMASK_SET(H_ALL_RES_QP_SRQ_LIMIT, parms->srq_limit);
	else
		r12 = EHCA_BMASK_SET(H_ALL_RES_QP_SRQ_QPN, parms->srq_qpn);

	ret = ehca_plpar_hcall9(H_ALLOC_RESOURCE, outs,
				adapter_handle.handle,	           /* r4  */
				allocate_controls,	           /* r5  */
				parms->send_cq_handle.handle,
				parms->recv_cq_handle.handle,
				parms->eq_handle.handle,
				((u64)parms->token << 32) | parms->pd.value,
				max_r10_reg, r11, r12);

	parms->qp_handle.handle = outs[0];
	parms->real_qp_num = (u32)outs[1];
	parms->squeue.act_nr_wqes =
		(u16)EHCA_BMASK_GET(H_ALL_RES_QP_ACT_OUTST_SEND_WR, outs[2]);
	parms->rqueue.act_nr_wqes =
		(u16)EHCA_BMASK_GET(H_ALL_RES_QP_ACT_OUTST_RECV_WR, outs[2]);
	parms->squeue.act_nr_sges =
		(u8)EHCA_BMASK_GET(H_ALL_RES_QP_ACT_SEND_SGE, outs[3]);
	parms->rqueue.act_nr_sges =
		(u8)EHCA_BMASK_GET(H_ALL_RES_QP_ACT_RECV_SGE, outs[3]);
	parms->squeue.queue_size =
		(u32)EHCA_BMASK_GET(H_ALL_RES_QP_SQUEUE_SIZE_PAGES, outs[4]);
	parms->rqueue.queue_size =
		(u32)EHCA_BMASK_GET(H_ALL_RES_QP_RQUEUE_SIZE_PAGES, outs[4]);

	if (ret == H_SUCCESS)
		hcp_galpas_ctor(&parms->galpas, outs[6], outs[6]);

	if (ret == H_NOT_ENOUGH_RESOURCES)
		ehca_gen_err("Not enough resources. ret=%lx", ret);

	return ret;
}

u64 hipz_h_query_port(const struct ipz_adapter_handle adapter_handle,
		      const u8 port_id,
		      struct hipz_query_port *query_port_response_block)
{
	u64 ret;
	u64 r_cb = virt_to_abs(query_port_response_block);

	if (r_cb & (EHCA_PAGESIZE-1)) {
		ehca_gen_err("response block not page aligned");
		return H_PARAMETER;
	}

	ret = ehca_plpar_hcall_norets(H_QUERY_PORT,
				      adapter_handle.handle, /* r4 */
				      port_id,	             /* r5 */
				      r_cb,	             /* r6 */
				      0, 0, 0, 0);

	if (ehca_debug_level)
		ehca_dmp(query_port_response_block, 64, "response_block");

	return ret;
}

u64 hipz_h_modify_port(const struct ipz_adapter_handle adapter_handle,
		       const u8 port_id, const u32 port_cap,
		       const u8 init_type, const int modify_mask)
{
	u64 port_attributes = port_cap;

	if (modify_mask & IB_PORT_SHUTDOWN)
		port_attributes |= EHCA_BMASK_SET(H_MP_SHUTDOWN, 1);
	if (modify_mask & IB_PORT_INIT_TYPE)
		port_attributes |= EHCA_BMASK_SET(H_MP_INIT_TYPE, init_type);
	if (modify_mask & IB_PORT_RESET_QKEY_CNTR)
		port_attributes |= EHCA_BMASK_SET(H_MP_RESET_QKEY_CTR, 1);

	return ehca_plpar_hcall_norets(H_MODIFY_PORT,
				       adapter_handle.handle, /* r4 */
				       port_id,               /* r5 */
				       port_attributes,       /* r6 */
				       0, 0, 0, 0);
}

u64 hipz_h_query_hca(const struct ipz_adapter_handle adapter_handle,
		     struct hipz_query_hca *query_hca_rblock)
{
	u64 r_cb = virt_to_abs(query_hca_rblock);

	if (r_cb & (EHCA_PAGESIZE-1)) {
		ehca_gen_err("response_block=%p not page aligned",
			     query_hca_rblock);
		return H_PARAMETER;
	}

	return ehca_plpar_hcall_norets(H_QUERY_HCA,
				       adapter_handle.handle, /* r4 */
				       r_cb,                  /* r5 */
				       0, 0, 0, 0, 0);
}

u64 hipz_h_register_rpage(const struct ipz_adapter_handle adapter_handle,
			  const u8 pagesize,
			  const u8 queue_type,
			  const u64 resource_handle,
			  const u64 logical_address_of_page,
			  u64 count)
{
	return ehca_plpar_hcall_norets(H_REGISTER_RPAGES,
				       adapter_handle.handle,      /* r4  */
				       (u64)queue_type | ((u64)pagesize) << 8,
				       /* r5  */
				       resource_handle,	           /* r6  */
				       logical_address_of_page,    /* r7  */
				       count,	                   /* r8  */
				       0, 0);
}

u64 hipz_h_register_rpage_eq(const struct ipz_adapter_handle adapter_handle,
			     const struct ipz_eq_handle eq_handle,
			     struct ehca_pfeq *pfeq,
			     const u8 pagesize,
			     const u8 queue_type,
			     const u64 logical_address_of_page,
			     const u64 count)
{
	if (count != 1) {
		ehca_gen_err("Ppage counter=%lx", count);
		return H_PARAMETER;
	}
	return hipz_h_register_rpage(adapter_handle,
				     pagesize,
				     queue_type,
				     eq_handle.handle,
				     logical_address_of_page, count);
}

u64 hipz_h_query_int_state(const struct ipz_adapter_handle adapter_handle,
			   u32 ist)
{
	u64 ret;
	ret = ehca_plpar_hcall_norets(H_QUERY_INT_STATE,
				      adapter_handle.handle, /* r4 */
				      ist,                   /* r5 */
				      0, 0, 0, 0, 0);

	if (ret != H_SUCCESS && ret != H_BUSY)
		ehca_gen_err("Could not query interrupt state.");

	return ret;
}

u64 hipz_h_register_rpage_cq(const struct ipz_adapter_handle adapter_handle,
			     const struct ipz_cq_handle cq_handle,
			     struct ehca_pfcq *pfcq,
			     const u8 pagesize,
			     const u8 queue_type,
			     const u64 logical_address_of_page,
			     const u64 count,
			     const struct h_galpa gal)
{
	if (count != 1) {
		ehca_gen_err("Page counter=%lx", count);
		return H_PARAMETER;
	}

	return hipz_h_register_rpage(adapter_handle, pagesize, queue_type,
				     cq_handle.handle, logical_address_of_page,
				     count);
}

u64 hipz_h_register_rpage_qp(const struct ipz_adapter_handle adapter_handle,
			     const struct ipz_qp_handle qp_handle,
			     struct ehca_pfqp *pfqp,
			     const u8 pagesize,
			     const u8 queue_type,
			     const u64 logical_address_of_page,
			     const u64 count,
			     const struct h_galpa galpa)
{
	if (count > 1) {
		ehca_gen_err("Page counter=%lx", count);
		return H_PARAMETER;
	}

	return hipz_h_register_rpage(adapter_handle, pagesize, queue_type,
				     qp_handle.handle, logical_address_of_page,
				     count);
}

u64 hipz_h_disable_and_get_wqe(const struct ipz_adapter_handle adapter_handle,
			       const struct ipz_qp_handle qp_handle,
			       struct ehca_pfqp *pfqp,
			       void **log_addr_next_sq_wqe2processed,
			       void **log_addr_next_rq_wqe2processed,
			       int dis_and_get_function_code)
{
	u64 ret;
	u64 outs[PLPAR_HCALL9_BUFSIZE];

	ret = ehca_plpar_hcall9(H_DISABLE_AND_GETC, outs,
				adapter_handle.handle,     /* r4 */
				dis_and_get_function_code, /* r5 */
				qp_handle.handle,	   /* r6 */
				0, 0, 0, 0, 0, 0);
	if (log_addr_next_sq_wqe2processed)
		*log_addr_next_sq_wqe2processed = (void *)outs[0];
	if (log_addr_next_rq_wqe2processed)
		*log_addr_next_rq_wqe2processed = (void *)outs[1];

	return ret;
}

u64 hipz_h_modify_qp(const struct ipz_adapter_handle adapter_handle,
		     const struct ipz_qp_handle qp_handle,
		     struct ehca_pfqp *pfqp,
		     const u64 update_mask,
		     struct hcp_modify_qp_control_block *mqpcb,
		     struct h_galpa gal)
{
	u64 ret;
	u64 outs[PLPAR_HCALL9_BUFSIZE];
	ret = ehca_plpar_hcall9(H_MODIFY_QP, outs,
				adapter_handle.handle, /* r4 */
				qp_handle.handle,      /* r5 */
				update_mask,	       /* r6 */
				virt_to_abs(mqpcb),    /* r7 */
				0, 0, 0, 0, 0);

	if (ret == H_NOT_ENOUGH_RESOURCES)
		ehca_gen_err("Insufficient resources ret=%lx", ret);

	return ret;
}

u64 hipz_h_query_qp(const struct ipz_adapter_handle adapter_handle,
		    const struct ipz_qp_handle qp_handle,
		    struct ehca_pfqp *pfqp,
		    struct hcp_modify_qp_control_block *qqpcb,
		    struct h_galpa gal)
{
	return ehca_plpar_hcall_norets(H_QUERY_QP,
				       adapter_handle.handle, /* r4 */
				       qp_handle.handle,      /* r5 */
				       virt_to_abs(qqpcb),    /* r6 */
				       0, 0, 0, 0);
}

u64 hipz_h_destroy_qp(const struct ipz_adapter_handle adapter_handle,
		      struct ehca_qp *qp)
{
	u64 ret;
	u64 outs[PLPAR_HCALL9_BUFSIZE];

	ret = hcp_galpas_dtor(&qp->galpas);
	if (ret) {
		ehca_gen_err("Could not destruct qp->galpas");
		return H_RESOURCE;
	}
	ret = ehca_plpar_hcall9(H_DISABLE_AND_GETC, outs,
				adapter_handle.handle,     /* r4 */
				/* function code */
				1,	                   /* r5 */
				qp->ipz_qp_handle.handle,  /* r6 */
				0, 0, 0, 0, 0, 0);
	if (ret == H_HARDWARE)
		ehca_gen_err("HCA not operational. ret=%lx", ret);

	ret = ehca_plpar_hcall_norets(H_FREE_RESOURCE,
				      adapter_handle.handle,     /* r4 */
				      qp->ipz_qp_handle.handle,  /* r5 */
				      0, 0, 0, 0, 0);

	if (ret == H_RESOURCE)
		ehca_gen_err("Resource still in use. ret=%lx", ret);

	return ret;
}

u64 hipz_h_define_aqp0(const struct ipz_adapter_handle adapter_handle,
		       const struct ipz_qp_handle qp_handle,
		       struct h_galpa gal,
		       u32 port)
{
	return ehca_plpar_hcall_norets(H_DEFINE_AQP0,
				       adapter_handle.handle, /* r4 */
				       qp_handle.handle,      /* r5 */
				       port,                  /* r6 */
				       0, 0, 0, 0);
}

u64 hipz_h_define_aqp1(const struct ipz_adapter_handle adapter_handle,
		       const struct ipz_qp_handle qp_handle,
		       struct h_galpa gal,
		       u32 port, u32 * pma_qp_nr,
		       u32 * bma_qp_nr)
{
	u64 ret;
	u64 outs[PLPAR_HCALL9_BUFSIZE];

	ret = ehca_plpar_hcall9(H_DEFINE_AQP1, outs,
				adapter_handle.handle, /* r4 */
				qp_handle.handle,      /* r5 */
				port,	               /* r6 */
				0, 0, 0, 0, 0, 0);
	*pma_qp_nr = (u32)outs[0];
	*bma_qp_nr = (u32)outs[1];

	if (ret == H_ALIAS_EXIST)
		ehca_gen_err("AQP1 already exists. ret=%lx", ret);

	return ret;
}

u64 hipz_h_attach_mcqp(const struct ipz_adapter_handle adapter_handle,
		       const struct ipz_qp_handle qp_handle,
		       struct h_galpa gal,
		       u16 mcg_dlid,
		       u64 subnet_prefix, u64 interface_id)
{
	u64 ret;

	ret = ehca_plpar_hcall_norets(H_ATTACH_MCQP,
				      adapter_handle.handle,  /* r4 */
				      qp_handle.handle,       /* r5 */
				      mcg_dlid,               /* r6 */
				      interface_id,           /* r7 */
				      subnet_prefix,          /* r8 */
				      0, 0);

	if (ret == H_NOT_ENOUGH_RESOURCES)
		ehca_gen_err("Not enough resources. ret=%lx", ret);

	return ret;
}

u64 hipz_h_detach_mcqp(const struct ipz_adapter_handle adapter_handle,
		       const struct ipz_qp_handle qp_handle,
		       struct h_galpa gal,
		       u16 mcg_dlid,
		       u64 subnet_prefix, u64 interface_id)
{
	return ehca_plpar_hcall_norets(H_DETACH_MCQP,
				       adapter_handle.handle, /* r4 */
				       qp_handle.handle,      /* r5 */
				       mcg_dlid,              /* r6 */
				       interface_id,          /* r7 */
				       subnet_prefix,         /* r8 */
				       0, 0);
}

u64 hipz_h_destroy_cq(const struct ipz_adapter_handle adapter_handle,
		      struct ehca_cq *cq,
		      u8 force_flag)
{
	u64 ret;

	ret = hcp_galpas_dtor(&cq->galpas);
	if (ret) {
		ehca_gen_err("Could not destruct cp->galpas");
		return H_RESOURCE;
	}

	ret = ehca_plpar_hcall_norets(H_FREE_RESOURCE,
				      adapter_handle.handle,     /* r4 */
				      cq->ipz_cq_handle.handle,  /* r5 */
				      force_flag != 0 ? 1L : 0L, /* r6 */
				      0, 0, 0, 0);

	if (ret == H_RESOURCE)
		ehca_gen_err("H_FREE_RESOURCE failed ret=%lx ", ret);

	return ret;
}

u64 hipz_h_destroy_eq(const struct ipz_adapter_handle adapter_handle,
		      struct ehca_eq *eq)
{
	u64 ret;

	ret = hcp_galpas_dtor(&eq->galpas);
	if (ret) {
		ehca_gen_err("Could not destruct eq->galpas");
		return H_RESOURCE;
	}

	ret = ehca_plpar_hcall_norets(H_FREE_RESOURCE,
				      adapter_handle.handle,     /* r4 */
				      eq->ipz_eq_handle.handle,  /* r5 */
				      0, 0, 0, 0, 0);

	if (ret == H_RESOURCE)
		ehca_gen_err("Resource in use. ret=%lx ", ret);

	return ret;
}

u64 hipz_h_alloc_resource_mr(const struct ipz_adapter_handle adapter_handle,
			     const struct ehca_mr *mr,
			     const u64 vaddr,
			     const u64 length,
			     const u32 access_ctrl,
			     const struct ipz_pd pd,
			     struct ehca_mr_hipzout_parms *outparms)
{
	u64 ret;
	u64 outs[PLPAR_HCALL9_BUFSIZE];

	ehca_gen_dbg("kernel PAGE_SIZE=%x access_ctrl=%016x "
		     "vaddr=%lx length=%lx",
		     (u32)PAGE_SIZE, access_ctrl, vaddr, length);
	ret = ehca_plpar_hcall9(H_ALLOC_RESOURCE, outs,
				adapter_handle.handle,            /* r4 */
				5,                                /* r5 */
				vaddr,                            /* r6 */
				length,                           /* r7 */
				(((u64)access_ctrl) << 32ULL),    /* r8 */
				pd.value,                         /* r9 */
				0, 0, 0);
	outparms->handle.handle = outs[0];
	outparms->lkey = (u32)outs[2];
	outparms->rkey = (u32)outs[3];

	return ret;
}

u64 hipz_h_register_rpage_mr(const struct ipz_adapter_handle adapter_handle,
			     const struct ehca_mr *mr,
			     const u8 pagesize,
			     const u8 queue_type,
			     const u64 logical_address_of_page,
			     const u64 count)
{
	extern int ehca_debug_level;
	u64 ret;

	if (unlikely(ehca_debug_level >= 2)) {
		if (count > 1) {
			u64 *kpage;
			int i;
			kpage = (u64 *)abs_to_virt(logical_address_of_page);
			for (i = 0; i < count; i++)
				ehca_gen_dbg("kpage[%d]=%p",
					     i, (void *)kpage[i]);
		} else
			ehca_gen_dbg("kpage=%p",
				     (void *)logical_address_of_page);
	}

	if ((count > 1) && (logical_address_of_page & (EHCA_PAGESIZE-1))) {
		ehca_gen_err("logical_address_of_page not on a 4k boundary "
			     "adapter_handle=%lx mr=%p mr_handle=%lx "
			     "pagesize=%x queue_type=%x "
			     "logical_address_of_page=%lx count=%lx",
			     adapter_handle.handle, mr,
			     mr->ipz_mr_handle.handle, pagesize, queue_type,
			     logical_address_of_page, count);
		ret = H_PARAMETER;
	} else
		ret = hipz_h_register_rpage(adapter_handle, pagesize,
					    queue_type,
					    mr->ipz_mr_handle.handle,
					    logical_address_of_page, count);
	return ret;
}

u64 hipz_h_query_mr(const struct ipz_adapter_handle adapter_handle,
		    const struct ehca_mr *mr,
		    struct ehca_mr_hipzout_parms *outparms)
{
	u64 ret;
	u64 outs[PLPAR_HCALL9_BUFSIZE];

	ret = ehca_plpar_hcall9(H_QUERY_MR, outs,
				adapter_handle.handle,     /* r4 */
				mr->ipz_mr_handle.handle,  /* r5 */
				0, 0, 0, 0, 0, 0, 0);
	outparms->len = outs[0];
	outparms->vaddr = outs[1];
	outparms->acl  = outs[4] >> 32;
	outparms->lkey = (u32)(outs[5] >> 32);
	outparms->rkey = (u32)(outs[5] & (0xffffffff));

	return ret;
}

u64 hipz_h_free_resource_mr(const struct ipz_adapter_handle adapter_handle,
			    const struct ehca_mr *mr)
{
	return ehca_plpar_hcall_norets(H_FREE_RESOURCE,
				       adapter_handle.handle,    /* r4 */
				       mr->ipz_mr_handle.handle, /* r5 */
				       0, 0, 0, 0, 0);
}

u64 hipz_h_reregister_pmr(const struct ipz_adapter_handle adapter_handle,
			  const struct ehca_mr *mr,
			  const u64 vaddr_in,
			  const u64 length,
			  const u32 access_ctrl,
			  const struct ipz_pd pd,
			  const u64 mr_addr_cb,
			  struct ehca_mr_hipzout_parms *outparms)
{
	u64 ret;
	u64 outs[PLPAR_HCALL9_BUFSIZE];

	ret = ehca_plpar_hcall9(H_REREGISTER_PMR, outs,
				adapter_handle.handle,    /* r4 */
				mr->ipz_mr_handle.handle, /* r5 */
				vaddr_in,	          /* r6 */
				length,                   /* r7 */
				/* r8 */
				((((u64)access_ctrl) << 32ULL) | pd.value),
				mr_addr_cb,               /* r9 */
				0, 0, 0);
	outparms->vaddr = outs[1];
	outparms->lkey = (u32)outs[2];
	outparms->rkey = (u32)outs[3];

	return ret;
}

u64 hipz_h_register_smr(const struct ipz_adapter_handle adapter_handle,
			const struct ehca_mr *mr,
			const struct ehca_mr *orig_mr,
			const u64 vaddr_in,
			const u32 access_ctrl,
			const struct ipz_pd pd,
			struct ehca_mr_hipzout_parms *outparms)
{
	u64 ret;
	u64 outs[PLPAR_HCALL9_BUFSIZE];

	ret = ehca_plpar_hcall9(H_REGISTER_SMR, outs,
				adapter_handle.handle,            /* r4 */
				orig_mr->ipz_mr_handle.handle,    /* r5 */
				vaddr_in,                         /* r6 */
				(((u64)access_ctrl) << 32ULL),    /* r7 */
				pd.value,                         /* r8 */
				0, 0, 0, 0);
	outparms->handle.handle = outs[0];
	outparms->lkey = (u32)outs[2];
	outparms->rkey = (u32)outs[3];

	return ret;
}

u64 hipz_h_alloc_resource_mw(const struct ipz_adapter_handle adapter_handle,
			     const struct ehca_mw *mw,
			     const struct ipz_pd pd,
			     struct ehca_mw_hipzout_parms *outparms)
{
	u64 ret;
	u64 outs[PLPAR_HCALL9_BUFSIZE];

	ret = ehca_plpar_hcall9(H_ALLOC_RESOURCE, outs,
				adapter_handle.handle,      /* r4 */
				6,                          /* r5 */
				pd.value,                   /* r6 */
				0, 0, 0, 0, 0, 0);
	outparms->handle.handle = outs[0];
	outparms->rkey = (u32)outs[3];

	return ret;
}

u64 hipz_h_query_mw(const struct ipz_adapter_handle adapter_handle,
		    const struct ehca_mw *mw,
		    struct ehca_mw_hipzout_parms *outparms)
{
	u64 ret;
	u64 outs[PLPAR_HCALL9_BUFSIZE];

	ret = ehca_plpar_hcall9(H_QUERY_MW, outs,
				adapter_handle.handle,    /* r4 */
				mw->ipz_mw_handle.handle, /* r5 */
				0, 0, 0, 0, 0, 0, 0);
	outparms->rkey = (u32)outs[3];

	return ret;
}

u64 hipz_h_free_resource_mw(const struct ipz_adapter_handle adapter_handle,
			    const struct ehca_mw *mw)
{
	return ehca_plpar_hcall_norets(H_FREE_RESOURCE,
				       adapter_handle.handle,    /* r4 */
				       mw->ipz_mw_handle.handle, /* r5 */
				       0, 0, 0, 0, 0);
}

u64 hipz_h_error_data(const struct ipz_adapter_handle adapter_handle,
		      const u64 ressource_handle,
		      void *rblock,
		      unsigned long *byte_count)
{
	u64 r_cb = virt_to_abs(rblock);

	if (r_cb & (EHCA_PAGESIZE-1)) {
		ehca_gen_err("rblock not page aligned.");
		return H_PARAMETER;
	}

	return ehca_plpar_hcall_norets(H_ERROR_DATA,
				       adapter_handle.handle,
				       ressource_handle,
				       r_cb,
				       0, 0, 0, 0);
}
