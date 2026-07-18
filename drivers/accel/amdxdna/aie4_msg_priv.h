/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2026, Advanced Micro Devices, Inc.
 */

#ifndef _AIE4_MSG_PRIV_H_
#define _AIE4_MSG_PRIV_H_

#include <linux/sizes.h>
#include <linux/types.h>

enum aie4_msg_opcode {
	AIE4_MSG_OP_SUSPEND                          = 0x10003,
	AIE4_MSG_OP_ATTACH_WORK_BUFFER               = 0x1000D,

	AIE4_MSG_OP_CREATE_VFS                       = 0x20001,
	AIE4_MSG_OP_DESTROY_VFS                      = 0x20002,

	AIE4_MSG_OP_CREATE_PARTITION                 = 0x30001,
	AIE4_MSG_OP_DESTROY_PARTITION                = 0x30002,
	AIE4_MSG_OP_CREATE_HW_CONTEXT                = 0x30003,
	AIE4_MSG_OP_DESTROY_HW_CONTEXT               = 0x30004,
	AIE4_MSG_OP_AIE_TILE_INFO                    = 0x30006,
};

enum aie4_msg_status {
	AIE4_MSG_STATUS_SUCCESS = 0x0,
	AIE4_MSG_STATUS_ERROR = 0x1,
	AIE4_MSG_STATUS_NOTSUPP = 0x2,
	MAX_AIE4_MSG_STATUS_CODE = 0x4,
};

struct aie4_msg_suspend_req {
	__u32 rsvd;
} __packed;

struct aie4_msg_suspend_resp {
	enum aie4_msg_status status;
} __packed;

struct aie4_msg_create_vfs_req {
	__u32 vf_cnt;
} __packed;

struct aie4_msg_create_vfs_resp {
	enum aie4_msg_status status;
} __packed;

struct aie4_msg_destroy_vfs_req {
	__u32 rsvd;
} __packed;

struct aie4_msg_destroy_vfs_resp {
	enum aie4_msg_status status;
} __packed;

struct aie4_msg_create_partition_req {
	__u32 partition_col_start;
	__u32 partition_col_count;
} __packed;

struct aie4_msg_create_partition_resp {
	enum aie4_msg_status status;
	__u32 partition_id;
} __packed;

struct aie4_msg_destroy_partition_req {
	__u32 partition_id;
} __packed;

struct aie4_msg_destroy_partition_resp {
	enum aie4_msg_status status;
} __packed;

struct aie4_msg_create_hw_context_req {
	__u32 partition_id;
	__u32 request_num_tiles;
	__u32 hsa_addr_high;
	__u32 hsa_addr_low;
#define AIE4_MSG_PASID GENMASK(19, 0)
#define AIE4_MSG_PASID_VLD GENMASK(31, 31)
	__u32 pasid;
	__u32 priority_band;
} __packed;

struct aie4_msg_create_hw_context_resp {
	enum aie4_msg_status status;
	__u32 hw_context_id;
	__u32 doorbell_offset;
	__u32 job_complete_msix_idx;
} __packed;

struct aie4_msg_destroy_hw_context_req {
	__u32 hw_context_id;
	__u32 resvd1;
} __packed;

struct aie4_msg_destroy_hw_context_resp {
	enum aie4_msg_status status;
} __packed;

struct aie4_tile_info {
	__u32 size;
	__u16 major;
	__u16 minor;
	__u16 cols;
	__u16 rows;
	__u16 core_rows;
	__u16 mem_rows;
	__u16 shim_rows;
	__u16 core_row_start;
	__u16 mem_row_start;
	__u16 shim_row_start;
	__u16 core_dma_channels;
	__u16 mem_dma_channels;
	__u16 shim_dma_channels;
	__u16 core_locks;
	__u16 mem_locks;
	__u16 shim_locks;
	__u16 core_events;
	__u16 mem_events;
	__u16 shim_events;
	__u16 resvd;
} __packed;

struct aie4_msg_aie4_tile_info_req {
	__u32 resvd;
} __packed;

struct aie4_msg_aie4_tile_info_resp {
	enum aie4_msg_status status;
	struct aie4_tile_info info;
} __packed;

#define AIE4_WORK_BUFFER_MIN_SIZE      SZ_4M

struct aie4_msg_attach_work_buffer_req {
	__u64 buff_addr;
	__u32 reserved;
	__u32 buff_size;
} __packed;

struct aie4_msg_attach_work_buffer_resp {
	enum aie4_msg_status status;
} __packed;

#endif /* _AIE4_MSG_PRIV_H_ */
