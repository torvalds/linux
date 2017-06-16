/*
 * Broadcom NetXtreme-E RoCE driver.
 *
 * Copyright (c) 2016 - 2017, Broadcom. All rights reserved.  The term
 * Broadcom refers to Broadcom Limited and/or its subsidiaries.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Description: Slow Path Operators (header)
 *
 */

#ifndef __BNXT_QPLIB_SP_H__
#define __BNXT_QPLIB_SP_H__

#define BNXT_QPLIB_RESERVED_QP_WRS	128

struct bnxt_qplib_dev_attr {
	char				fw_ver[32];
	u16				max_sgid;
	u16				max_mrw;
	u32				max_qp;
#define BNXT_QPLIB_MAX_OUT_RD_ATOM	126
	u32				max_qp_rd_atom;
	u32				max_qp_init_rd_atom;
	u32				max_qp_wqes;
	u32				max_qp_sges;
	u32				max_cq;
	u32				max_cq_wqes;
	u32				max_cq_sges;
	u32				max_mr;
	u64				max_mr_size;
	u32				max_pd;
	u32				max_mw;
	u32				max_raw_ethy_qp;
	u32				max_ah;
	u32				max_fmr;
	u32				max_map_per_fmr;
	u32				max_srq;
	u32				max_srq_wqes;
	u32				max_srq_sges;
	u32				max_pkey;
	u32				max_inline_data;
	u32				l2_db_size;
	u8				tqm_alloc_reqs[MAX_TQM_ALLOC_REQ];
};

struct bnxt_qplib_pd {
	u32				id;
};

struct bnxt_qplib_gid {
	u8				data[16];
};

struct bnxt_qplib_ah {
	struct bnxt_qplib_gid		dgid;
	struct bnxt_qplib_pd		*pd;
	u32				id;
	u8				sgid_index;
	/* For Query AH if the hw table and SW table are differnt */
	u8				host_sgid_index;
	u8				traffic_class;
	u32				flow_label;
	u8				hop_limit;
	u8				sl;
	u8				dmac[6];
	u16				vlan_id;
	u8				nw_type;
};

struct bnxt_qplib_mrw {
	struct bnxt_qplib_pd		*pd;
	int				type;
	u32				flags;
#define BNXT_QPLIB_FR_PMR		0x80000000
	u32				lkey;
	u32				rkey;
#define BNXT_QPLIB_RSVD_LKEY		0xFFFFFFFF
	u64				va;
	u64				total_size;
	u32				npages;
	u64				mr_handle;
	struct bnxt_qplib_hwq		hwq;
};

struct bnxt_qplib_frpl {
	int				max_pg_ptrs;
	struct bnxt_qplib_hwq		hwq;
};

#define BNXT_QPLIB_ACCESS_LOCAL_WRITE	BIT(0)
#define BNXT_QPLIB_ACCESS_REMOTE_READ	BIT(1)
#define BNXT_QPLIB_ACCESS_REMOTE_WRITE	BIT(2)
#define BNXT_QPLIB_ACCESS_REMOTE_ATOMIC	BIT(3)
#define BNXT_QPLIB_ACCESS_MW_BIND	BIT(4)
#define BNXT_QPLIB_ACCESS_ZERO_BASED	BIT(5)
#define BNXT_QPLIB_ACCESS_ON_DEMAND	BIT(6)

int bnxt_qplib_get_sgid(struct bnxt_qplib_res *res,
			struct bnxt_qplib_sgid_tbl *sgid_tbl, int index,
			struct bnxt_qplib_gid *gid);
int bnxt_qplib_del_sgid(struct bnxt_qplib_sgid_tbl *sgid_tbl,
			struct bnxt_qplib_gid *gid, bool update);
int bnxt_qplib_add_sgid(struct bnxt_qplib_sgid_tbl *sgid_tbl,
			struct bnxt_qplib_gid *gid, u8 *mac, u16 vlan_id,
			bool update, u32 *index);
int bnxt_qplib_get_pkey(struct bnxt_qplib_res *res,
			struct bnxt_qplib_pkey_tbl *pkey_tbl, u16 index,
			u16 *pkey);
int bnxt_qplib_del_pkey(struct bnxt_qplib_res *res,
			struct bnxt_qplib_pkey_tbl *pkey_tbl, u16 *pkey,
			bool update);
int bnxt_qplib_add_pkey(struct bnxt_qplib_res *res,
			struct bnxt_qplib_pkey_tbl *pkey_tbl, u16 *pkey,
			bool update);
int bnxt_qplib_get_dev_attr(struct bnxt_qplib_rcfw *rcfw,
			    struct bnxt_qplib_dev_attr *attr);
int bnxt_qplib_create_ah(struct bnxt_qplib_res *res, struct bnxt_qplib_ah *ah);
int bnxt_qplib_destroy_ah(struct bnxt_qplib_res *res, struct bnxt_qplib_ah *ah);
int bnxt_qplib_alloc_mrw(struct bnxt_qplib_res *res,
			 struct bnxt_qplib_mrw *mrw);
int bnxt_qplib_dereg_mrw(struct bnxt_qplib_res *res, struct bnxt_qplib_mrw *mrw,
			 bool block);
int bnxt_qplib_reg_mr(struct bnxt_qplib_res *res, struct bnxt_qplib_mrw *mr,
		      u64 *pbl_tbl, int num_pbls, bool block);
int bnxt_qplib_free_mrw(struct bnxt_qplib_res *res, struct bnxt_qplib_mrw *mr);
int bnxt_qplib_alloc_fast_reg_mr(struct bnxt_qplib_res *res,
				 struct bnxt_qplib_mrw *mr, int max);
int bnxt_qplib_alloc_fast_reg_page_list(struct bnxt_qplib_res *res,
					struct bnxt_qplib_frpl *frpl, int max);
int bnxt_qplib_free_fast_reg_page_list(struct bnxt_qplib_res *res,
				       struct bnxt_qplib_frpl *frpl);
int bnxt_qplib_map_tc2cos(struct bnxt_qplib_res *res, u16 *cids);
#endif /* __BNXT_QPLIB_SP_H__*/
