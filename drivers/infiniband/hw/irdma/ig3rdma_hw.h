/* SPDX-License-Identifier: GPL-2.0 or Linux-OpenIB */
/* Copyright (c) 2021 - 2024 Intel Corporation */
#ifndef IG3RDMA_HW_H
#define IG3RDMA_HW_H

#define IG3_MAX_APFS 1
#define IG3_MAX_AVFS 0

#define IG3_PF_RDMA_REGION_OFFSET 0xBC00000
#define IG3_PF_RDMA_REGION_LEN 0x401000
#define IG3_VF_RDMA_REGION_OFFSET 0x8C00
#define IG3_VF_RDMA_REGION_LEN 0x8400

enum ig3rdma_device_caps_const {
	IG3RDMA_MAX_WQ_FRAGMENT_COUNT		= 14,
	IG3RDMA_MAX_SGE_RD			= 14,

	IG3RDMA_MAX_STATS_COUNT			= 128,

	IG3RDMA_MAX_IRD_SIZE			= 64,
	IG3RDMA_MAX_ORD_SIZE			= 64,
	IG3RDMA_MIN_WQ_SIZE			= 16 /* WQEs */,
	IG3RDMA_MAX_INLINE_DATA_SIZE		= 216,
	IG3RDMA_MAX_PF_PUSH_PAGE_COUNT		= 8192,
	IG3RDMA_MAX_VF_PUSH_PAGE_COUNT		= 16,
};

void __iomem *ig3rdma_get_reg_addr(struct irdma_hw *hw, u64 reg_offset);
int ig3rdma_vchnl_send_sync(struct irdma_sc_dev *dev, u8 *msg, u16 len,
			    u8 *recv_msg, u16 *recv_len);

#endif /* IG3RDMA_HW_H*/
