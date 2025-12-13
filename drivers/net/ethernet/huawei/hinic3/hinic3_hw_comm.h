/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved. */

#ifndef _HINIC3_HW_COMM_H_
#define _HINIC3_HW_COMM_H_

#include "hinic3_hw_intf.h"

struct hinic3_hwdev;

#define HINIC3_WQ_PAGE_SIZE_ORDER  8

struct hinic3_interrupt_info {
	u32 lli_set;
	u32 interrupt_coalesc_set;
	u16 msix_index;
	u8  lli_credit_limit;
	u8  lli_timer_cfg;
	u8  pending_limit;
	u8  coalesc_timer_cfg;
	u8  resend_timer_cfg;
};

int hinic3_set_interrupt_cfg_direct(struct hinic3_hwdev *hwdev,
				    const struct hinic3_interrupt_info *info);
int hinic3_func_reset(struct hinic3_hwdev *hwdev, u16 func_id, u64 reset_flag);

int hinic3_get_comm_features(struct hinic3_hwdev *hwdev, u64 *s_feature,
			     u16 size);
int hinic3_set_comm_features(struct hinic3_hwdev *hwdev, u64 *s_feature,
			     u16 size);
int hinic3_get_global_attr(struct hinic3_hwdev *hwdev,
			   struct comm_global_attr *attr);
int hinic3_set_func_svc_used_state(struct hinic3_hwdev *hwdev, u16 svc_type,
				   u8 state);
int hinic3_set_dma_attr_tbl(struct hinic3_hwdev *hwdev, u8 entry_idx, u8 st,
			    u8 at, u8 ph, u8 no_snooping, u8 tph_en);

int hinic3_set_wq_page_size(struct hinic3_hwdev *hwdev, u16 func_idx,
			    u32 page_size);
int hinic3_set_cmdq_depth(struct hinic3_hwdev *hwdev, u16 cmdq_depth);
int hinic3_func_rx_tx_flush(struct hinic3_hwdev *hwdev);
int hinic3_set_root_ctxt(struct hinic3_hwdev *hwdev, u32 rq_depth, u32 sq_depth,
			 int rx_buf_sz);
int hinic3_clean_root_ctxt(struct hinic3_hwdev *hwdev);

#endif
