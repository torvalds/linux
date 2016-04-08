/*******************************************************************************
*
* Copyright (c) 2015-2016 Intel Corporation.  All rights reserved.
*
* This software is available to you under a choice of one of two
* licenses.  You may choose to be licensed under the terms of the GNU
* General Public License (GPL) Version 2, available from the file
* COPYING in the main directory of this source tree, or the
* OpenFabrics.org BSD license below:
*
*   Redistribution and use in source and binary forms, with or
*   without modification, are permitted provided that the following
*   conditions are met:
*
*    - Redistributions of source code must retain the above
*	copyright notice, this list of conditions and the following
*	disclaimer.
*
*    - Redistributions in binary form must reproduce the above
*	copyright notice, this list of conditions and the following
*	disclaimer in the documentation and/or other materials
*	provided with the distribution.
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
*******************************************************************************/

#ifndef I40IW_VERBS_H
#define I40IW_VERBS_H

struct i40iw_ucontext {
	struct ib_ucontext ibucontext;
	struct i40iw_device *iwdev;
	struct list_head cq_reg_mem_list;
	spinlock_t cq_reg_mem_list_lock; /* memory list for cq's */
	struct list_head qp_reg_mem_list;
	spinlock_t qp_reg_mem_list_lock; /* memory list for qp's */
};

struct i40iw_pd {
	struct ib_pd ibpd;
	struct i40iw_sc_pd sc_pd;
	atomic_t usecount;
};

struct i40iw_hmc_pble {
	union {
		u32 idx;
		dma_addr_t addr;
	};
};

struct i40iw_cq_mr {
	struct i40iw_hmc_pble cq_pbl;
	dma_addr_t shadow;
};

struct i40iw_qp_mr {
	struct i40iw_hmc_pble sq_pbl;
	struct i40iw_hmc_pble rq_pbl;
	dma_addr_t shadow;
	struct page *sq_page;
};

struct i40iw_pbl {
	struct list_head list;
	union {
		struct i40iw_qp_mr qp_mr;
		struct i40iw_cq_mr cq_mr;
	};

	bool pbl_allocated;
	u64 user_base;
	struct i40iw_pble_alloc pble_alloc;
	struct i40iw_mr *iwmr;
};

#define MAX_SAVE_PAGE_ADDRS     4
struct i40iw_mr {
	union {
		struct ib_mr ibmr;
		struct ib_mw ibmw;
		struct ib_fmr ibfmr;
	};
	struct ib_umem *region;
	u16 type;
	u32 page_cnt;
	u32 stag;
	u64 length;
	u64 pgaddrmem[MAX_SAVE_PAGE_ADDRS];
	struct i40iw_pbl iwpbl;
};

struct i40iw_cq {
	struct ib_cq ibcq;
	struct i40iw_sc_cq sc_cq;
	u16 cq_head;
	u16 cq_size;
	u16 cq_number;
	bool user_mode;
	u32 polled_completions;
	u32 cq_mem_size;
	struct i40iw_dma_mem kmem;
	spinlock_t lock; /* for poll cq */
	struct i40iw_pbl *iwpbl;
};

struct disconn_work {
	struct work_struct work;
	struct i40iw_qp *iwqp;
};

struct iw_cm_id;
struct ietf_mpa_frame;
struct i40iw_ud_file;

struct i40iw_qp_kmode {
	struct i40iw_dma_mem dma_mem;
	u64 *wrid_mem;
};

struct i40iw_qp {
	struct ib_qp ibqp;
	struct i40iw_sc_qp sc_qp;
	struct i40iw_device *iwdev;
	struct i40iw_cq *iwscq;
	struct i40iw_cq *iwrcq;
	struct i40iw_pd *iwpd;
	struct i40iw_qp_host_ctx_info ctx_info;
	struct i40iwarp_offload_info iwarp_info;
	void *allocated_buffer;
	atomic_t refcount;
	struct iw_cm_id *cm_id;
	void *cm_node;
	struct ib_mr *lsmm_mr;
	struct work_struct work;
	enum ib_qp_state ibqp_state;
	u32 iwarp_state;
	u32 qp_mem_size;
	u32 last_aeq;
	atomic_t close_timer_started;
	spinlock_t lock; /* for post work requests */
	struct i40iw_qp_context *iwqp_context;
	void *pbl_vbase;
	dma_addr_t pbl_pbase;
	struct page *page;
	u8 active_conn:1;
	u8 user_mode:1;
	u8 hte_added:1;
	u8 flush_issued:1;
	u8 destroyed:1;
	u8 sig_all:1;
	u8 pau_mode:1;
	u8 rsvd:1;
	u16 term_sq_flush_code;
	u16 term_rq_flush_code;
	u8 hw_iwarp_state;
	u8 hw_tcp_state;
	struct i40iw_qp_kmode kqp;
	struct i40iw_dma_mem host_ctx;
	struct timer_list terminate_timer;
	struct i40iw_pbl *iwpbl;
	struct i40iw_dma_mem q2_ctx_mem;
	struct i40iw_dma_mem ietf_mem;
};
#endif
