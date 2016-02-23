/* This file is part of the Emulex RoCE Device Driver for
 * RoCE (RDMA over Converged Ethernet) adapters.
 * Copyright (C) 2012-2015 Emulex. All rights reserved.
 * EMULEX and SLI are trademarks of Emulex.
 * www.emulex.com
 *
 * This software is available to you under a choice of one of two licenses.
 * You may choose to be licensed under the terms of the GNU General Public
 * License (GPL) Version 2, available from the file COPYING in the main
 * directory of this source tree, or the BSD license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Contact Information:
 * linux-drivers@emulex.com
 *
 * Emulex
 * 3333 Susan Street
 * Costa Mesa, CA 92626
 */

#ifndef __OCRDMA_ABI_H__
#define __OCRDMA_ABI_H__

#define OCRDMA_ABI_VERSION 2
#define OCRDMA_BE_ROCE_ABI_VERSION 1
/* user kernel communication data structures. */

struct ocrdma_alloc_ucontext_resp {
	u32 dev_id;
	u32 wqe_size;
	u32 max_inline_data;
	u32 dpp_wqe_size;
	u64 ah_tbl_page;
	u32 ah_tbl_len;
	u32 rqe_size;
	u8 fw_ver[32];
	/* for future use/new features in progress */
	u64 rsvd1;
	u64 rsvd2;
};

struct ocrdma_alloc_pd_ureq {
	u64 rsvd1;
};

struct ocrdma_alloc_pd_uresp {
	u32 id;
	u32 dpp_enabled;
	u32 dpp_page_addr_hi;
	u32 dpp_page_addr_lo;
	u64 rsvd1;
};

struct ocrdma_create_cq_ureq {
	u32 dpp_cq;
	u32 rsvd; /* pad */
};

#define MAX_CQ_PAGES 8
struct ocrdma_create_cq_uresp {
	u32 cq_id;
	u32 page_size;
	u32 num_pages;
	u32 max_hw_cqe;
	u64 page_addr[MAX_CQ_PAGES];
	u64 db_page_addr;
	u32 db_page_size;
	u32 phase_change;
	/* for future use/new features in progress */
	u64 rsvd1;
	u64 rsvd2;
};

#define MAX_QP_PAGES 8
#define MAX_UD_AV_PAGES 8

struct ocrdma_create_qp_ureq {
	u8 enable_dpp_cq;
	u8 rsvd;
	u16 dpp_cq_id;
	u32 rsvd1;	/* pad */
};

struct ocrdma_create_qp_uresp {
	u16 qp_id;
	u16 sq_dbid;
	u16 rq_dbid;
	u16 resv0;	/* pad */
	u32 sq_page_size;
	u32 rq_page_size;
	u32 num_sq_pages;
	u32 num_rq_pages;
	u64 sq_page_addr[MAX_QP_PAGES];
	u64 rq_page_addr[MAX_QP_PAGES];
	u64 db_page_addr;
	u32 db_page_size;
	u32 dpp_credit;
	u32 dpp_offset;
	u32 num_wqe_allocated;
	u32 num_rqe_allocated;
	u32 db_sq_offset;
	u32 db_rq_offset;
	u32 db_shift;
	u64 rsvd[11];
} __packed;

struct ocrdma_create_srq_uresp {
	u16 rq_dbid;
	u16 resv0;	/* pad */
	u32 resv1;

	u32 rq_page_size;
	u32 num_rq_pages;

	u64 rq_page_addr[MAX_QP_PAGES];
	u64 db_page_addr;

	u32 db_page_size;
	u32 num_rqe_allocated;
	u32 db_rq_offset;
	u32 db_shift;

	u64 rsvd2;
	u64 rsvd3;
};

#endif				/* __OCRDMA_ABI_H__ */
