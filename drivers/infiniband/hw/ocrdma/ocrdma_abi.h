/*******************************************************************
 * This file is part of the Emulex RoCE Device Driver for          *
 * RoCE (RDMA over Converged Ethernet) adapters.                   *
 * Copyright (C) 2008-2012 Emulex. All rights reserved.            *
 * EMULEX and SLI are trademarks of Emulex.                        *
 * www.emulex.com                                                  *
 *                                                                 *
 * This program is free software; you can redistribute it and/or   *
 * modify it under the terms of version 2 of the GNU General       *
 * Public License as published by the Free Software Foundation.    *
 * This program is distributed in the hope that it will be useful. *
 * ALL EXPRESS OR IMPLIED CONDITIONS, REPRESENTATIONS AND          *
 * WARRANTIES, INCLUDING ANY IMPLIED WARRANTY OF MERCHANTABILITY,  *
 * FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT, ARE      *
 * DISCLAIMED, EXCEPT TO THE EXTENT THAT SUCH DISCLAIMERS ARE HELD *
 * TO BE LEGALLY INVALID.  See the GNU General Public License for  *
 * more details, a copy of which can be found in the file COPYING  *
 * included with this package.                                     *
 *
 * Contact Information:
 * linux-drivers@emulex.com
 *
 * Emulex
 * 3333 Susan Street
 * Costa Mesa, CA 92626
 *******************************************************************/

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
