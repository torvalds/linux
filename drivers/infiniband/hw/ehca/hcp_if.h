/*
 *  IBM eServer eHCA Infiniband device driver for Linux on POWER
 *
 *  Firmware Infiniband Interface code for POWER
 *
 *  Authors: Christoph Raisch <raisch@de.ibm.com>
 *           Hoang-Nam Nguyen <hnguyen@de.ibm.com>
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

#ifndef __HCP_IF_H__
#define __HCP_IF_H__

#include "ehca_classes.h"
#include "ehca_tools.h"
#include "hipz_hw.h"

/*
 * hipz_h_alloc_resource_eq allocates EQ resources in HW and FW, initalize
 * resources, create the empty EQPT (ring).
 */
u64 hipz_h_alloc_resource_eq(const struct ipz_adapter_handle adapter_handle,
			     struct ehca_pfeq *pfeq,
			     const u32 neq_control,
			     const u32 number_of_entries,
			     struct ipz_eq_handle *eq_handle,
			     u32 * act_nr_of_entries,
			     u32 * act_pages,
			     u32 * eq_ist);

u64 hipz_h_reset_event(const struct ipz_adapter_handle adapter_handle,
		       struct ipz_eq_handle eq_handle,
		       const u64 event_mask);
/*
 * hipz_h_allocate_resource_cq allocates CQ resources in HW and FW, initialize
 * resources, create the empty CQPT (ring).
 */
u64 hipz_h_alloc_resource_cq(const struct ipz_adapter_handle adapter_handle,
			     struct ehca_cq *cq,
			     struct ehca_alloc_cq_parms *param);


/*
 * hipz_h_alloc_resource_qp allocates QP resources in HW and FW,
 * initialize resources, create empty QPPTs (2 rings).
 */
u64 hipz_h_alloc_resource_qp(const struct ipz_adapter_handle adapter_handle,
			     struct ehca_alloc_qp_parms *parms);

u64 hipz_h_query_port(const struct ipz_adapter_handle adapter_handle,
		      const u8 port_id,
		      struct hipz_query_port *query_port_response_block);

u64 hipz_h_modify_port(const struct ipz_adapter_handle adapter_handle,
		       const u8 port_id, const u32 port_cap,
		       const u8 init_type, const int modify_mask);

u64 hipz_h_query_hca(const struct ipz_adapter_handle adapter_handle,
		     struct hipz_query_hca *query_hca_rblock);

/*
 * hipz_h_register_rpage internal function in hcp_if.h for all
 * hcp_H_REGISTER_RPAGE calls.
 */
u64 hipz_h_register_rpage(const struct ipz_adapter_handle adapter_handle,
			  const u8 pagesize,
			  const u8 queue_type,
			  const u64 resource_handle,
			  const u64 logical_address_of_page,
			  u64 count);

u64 hipz_h_register_rpage_eq(const struct ipz_adapter_handle adapter_handle,
			     const struct ipz_eq_handle eq_handle,
			     struct ehca_pfeq *pfeq,
			     const u8 pagesize,
			     const u8 queue_type,
			     const u64 logical_address_of_page,
			     const u64 count);

u64 hipz_h_query_int_state(const struct ipz_adapter_handle
			   hcp_adapter_handle,
			   u32 ist);

u64 hipz_h_register_rpage_cq(const struct ipz_adapter_handle adapter_handle,
			     const struct ipz_cq_handle cq_handle,
			     struct ehca_pfcq *pfcq,
			     const u8 pagesize,
			     const u8 queue_type,
			     const u64 logical_address_of_page,
			     const u64 count,
			     const struct h_galpa gal);

u64 hipz_h_register_rpage_qp(const struct ipz_adapter_handle adapter_handle,
			     const struct ipz_qp_handle qp_handle,
			     struct ehca_pfqp *pfqp,
			     const u8 pagesize,
			     const u8 queue_type,
			     const u64 logical_address_of_page,
			     const u64 count,
			     const struct h_galpa galpa);

u64 hipz_h_disable_and_get_wqe(const struct ipz_adapter_handle adapter_handle,
			       const struct ipz_qp_handle qp_handle,
			       struct ehca_pfqp *pfqp,
			       void **log_addr_next_sq_wqe_tb_processed,
			       void **log_addr_next_rq_wqe_tb_processed,
			       int dis_and_get_function_code);
enum hcall_sigt {
	HCALL_SIGT_NO_CQE = 0,
	HCALL_SIGT_BY_WQE = 1,
	HCALL_SIGT_EVERY = 2
};

u64 hipz_h_modify_qp(const struct ipz_adapter_handle adapter_handle,
		     const struct ipz_qp_handle qp_handle,
		     struct ehca_pfqp *pfqp,
		     const u64 update_mask,
		     struct hcp_modify_qp_control_block *mqpcb,
		     struct h_galpa gal);

u64 hipz_h_query_qp(const struct ipz_adapter_handle adapter_handle,
		    const struct ipz_qp_handle qp_handle,
		    struct ehca_pfqp *pfqp,
		    struct hcp_modify_qp_control_block *qqpcb,
		    struct h_galpa gal);

u64 hipz_h_destroy_qp(const struct ipz_adapter_handle adapter_handle,
		      struct ehca_qp *qp);

u64 hipz_h_define_aqp0(const struct ipz_adapter_handle adapter_handle,
		       const struct ipz_qp_handle qp_handle,
		       struct h_galpa gal,
		       u32 port);

u64 hipz_h_define_aqp1(const struct ipz_adapter_handle adapter_handle,
		       const struct ipz_qp_handle qp_handle,
		       struct h_galpa gal,
		       u32 port, u32 * pma_qp_nr,
		       u32 * bma_qp_nr);

u64 hipz_h_attach_mcqp(const struct ipz_adapter_handle adapter_handle,
		       const struct ipz_qp_handle qp_handle,
		       struct h_galpa gal,
		       u16 mcg_dlid,
		       u64 subnet_prefix, u64 interface_id);

u64 hipz_h_detach_mcqp(const struct ipz_adapter_handle adapter_handle,
		       const struct ipz_qp_handle qp_handle,
		       struct h_galpa gal,
		       u16 mcg_dlid,
		       u64 subnet_prefix, u64 interface_id);

u64 hipz_h_destroy_cq(const struct ipz_adapter_handle adapter_handle,
		      struct ehca_cq *cq,
		      u8 force_flag);

u64 hipz_h_destroy_eq(const struct ipz_adapter_handle adapter_handle,
		      struct ehca_eq *eq);

/*
 * hipz_h_alloc_resource_mr allocates MR resources in HW and FW, initialize
 * resources.
 */
u64 hipz_h_alloc_resource_mr(const struct ipz_adapter_handle adapter_handle,
			     const struct ehca_mr *mr,
			     const u64 vaddr,
			     const u64 length,
			     const u32 access_ctrl,
			     const struct ipz_pd pd,
			     struct ehca_mr_hipzout_parms *outparms);

/* hipz_h_register_rpage_mr registers MR resource pages in HW and FW */
u64 hipz_h_register_rpage_mr(const struct ipz_adapter_handle adapter_handle,
			     const struct ehca_mr *mr,
			     const u8 pagesize,
			     const u8 queue_type,
			     const u64 logical_address_of_page,
			     const u64 count);

/* hipz_h_query_mr queries MR in HW and FW */
u64 hipz_h_query_mr(const struct ipz_adapter_handle adapter_handle,
		    const struct ehca_mr *mr,
		    struct ehca_mr_hipzout_parms *outparms);

/* hipz_h_free_resource_mr frees MR resources in HW and FW */
u64 hipz_h_free_resource_mr(const struct ipz_adapter_handle adapter_handle,
			    const struct ehca_mr *mr);

/* hipz_h_reregister_pmr reregisters MR in HW and FW */
u64 hipz_h_reregister_pmr(const struct ipz_adapter_handle adapter_handle,
			  const struct ehca_mr *mr,
			  const u64 vaddr_in,
			  const u64 length,
			  const u32 access_ctrl,
			  const struct ipz_pd pd,
			  const u64 mr_addr_cb,
			  struct ehca_mr_hipzout_parms *outparms);

/* hipz_h_register_smr register shared MR in HW and FW */
u64 hipz_h_register_smr(const struct ipz_adapter_handle adapter_handle,
			const struct ehca_mr *mr,
			const struct ehca_mr *orig_mr,
			const u64 vaddr_in,
			const u32 access_ctrl,
			const struct ipz_pd pd,
			struct ehca_mr_hipzout_parms *outparms);

/*
 * hipz_h_alloc_resource_mw allocates MW resources in HW and FW, initialize
 * resources.
 */
u64 hipz_h_alloc_resource_mw(const struct ipz_adapter_handle adapter_handle,
			     const struct ehca_mw *mw,
			     const struct ipz_pd pd,
			     struct ehca_mw_hipzout_parms *outparms);

/* hipz_h_query_mw queries MW in HW and FW */
u64 hipz_h_query_mw(const struct ipz_adapter_handle adapter_handle,
		    const struct ehca_mw *mw,
		    struct ehca_mw_hipzout_parms *outparms);

/* hipz_h_free_resource_mw frees MW resources in HW and FW */
u64 hipz_h_free_resource_mw(const struct ipz_adapter_handle adapter_handle,
			    const struct ehca_mw *mw);

u64 hipz_h_error_data(const struct ipz_adapter_handle adapter_handle,
		      const u64 ressource_handle,
		      void *rblock,
		      unsigned long *byte_count);

#endif /* __HCP_IF_H__ */
