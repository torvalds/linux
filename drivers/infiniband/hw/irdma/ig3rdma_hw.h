/* SPDX-License-Identifier: GPL-2.0 or Linux-OpenIB */
/* Copyright (c) 2021 - 2024 Intel Corporation */
#ifndef IG3RDMA_HW_H
#define IG3RDMA_HW_H

#define IG3_PF_RDMA_REGION_OFFSET 0xBC00000
#define IG3_PF_RDMA_REGION_LEN 0x401000
#define IG3_VF_RDMA_REGION_OFFSET 0x8C00
#define IG3_VF_RDMA_REGION_LEN 0x8400

int ig3rdma_vchnl_send_sync(struct irdma_sc_dev *dev, u8 *msg, u16 len,
			    u8 *recv_msg, u16 *recv_len);

#endif /* IG3RDMA_HW_H*/
