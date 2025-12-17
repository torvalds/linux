/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (c) 2025 Broadcom.

#ifndef __BNG_SP_H__
#define __BNG_SP_H__

#include "bng_fw.h"

#define BNG_VAR_MAX_WQE		4352
#define BNG_VAR_MAX_SGE		13

struct bng_re_dev_attr {
#define FW_VER_ARR_LEN			4
	u8				fw_ver[FW_VER_ARR_LEN];
#define BNG_RE_NUM_GIDS_SUPPORTED	256
	u16				max_sgid;
	u16				max_mrw;
	u32				max_qp;
#define BNG_RE_MAX_OUT_RD_ATOM		126
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
	u32				max_srq;
	u32				max_srq_wqes;
	u32				max_srq_sges;
	u32				max_pkey;
	u32				max_inline_data;
	u32				l2_db_size;
	u8				tqm_alloc_reqs[BNG_MAX_TQM_ALLOC_REQ];
	bool				is_atomic;
	u16                             dev_cap_flags;
	u16                             dev_cap_flags2;
	u32                             max_dpi;
};

int bng_re_get_dev_attr(struct bng_re_rcfw *rcfw);
#endif
