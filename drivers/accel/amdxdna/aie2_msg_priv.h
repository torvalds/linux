/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022-2024, Advanced Micro Devices, Inc.
 */

#ifndef _AIE2_MSG_PRIV_H_
#define _AIE2_MSG_PRIV_H_

enum aie2_msg_opcode {
	MSG_OP_CREATE_CONTEXT              = 0x2,
	MSG_OP_DESTROY_CONTEXT             = 0x3,
	MSG_OP_SYNC_BO			   = 0x7,
	MSG_OP_EXECUTE_BUFFER_CF           = 0xC,
	MSG_OP_QUERY_COL_STATUS            = 0xD,
	MSG_OP_QUERY_AIE_TILE_INFO         = 0xE,
	MSG_OP_QUERY_AIE_VERSION           = 0xF,
	MSG_OP_EXEC_DPU                    = 0x10,
	MSG_OP_CONFIG_CU                   = 0x11,
	MSG_OP_CHAIN_EXEC_BUFFER_CF        = 0x12,
	MSG_OP_CHAIN_EXEC_DPU              = 0x13,
	MSG_OP_MAX_XRT_OPCODE,
	MSG_OP_SUSPEND                     = 0x101,
	MSG_OP_RESUME                      = 0x102,
	MSG_OP_ASSIGN_MGMT_PASID           = 0x103,
	MSG_OP_INVOKE_SELF_TEST            = 0x104,
	MSG_OP_MAP_HOST_BUFFER             = 0x106,
	MSG_OP_GET_FIRMWARE_VERSION        = 0x108,
	MSG_OP_SET_RUNTIME_CONFIG          = 0x10A,
	MSG_OP_GET_RUNTIME_CONFIG          = 0x10B,
	MSG_OP_REGISTER_ASYNC_EVENT_MSG    = 0x10C,
	MSG_OP_MAX_DRV_OPCODE,
	MSG_OP_GET_PROTOCOL_VERSION        = 0x301,
	MSG_OP_MAX_OPCODE
};

enum aie2_msg_status {
	AIE2_STATUS_SUCCESS				= 0x0,
	/* AIE Error codes */
	AIE2_STATUS_AIE_SATURATION_ERROR		= 0x1000001,
	AIE2_STATUS_AIE_FP_ERROR			= 0x1000002,
	AIE2_STATUS_AIE_STREAM_ERROR			= 0x1000003,
	AIE2_STATUS_AIE_ACCESS_ERROR			= 0x1000004,
	AIE2_STATUS_AIE_BUS_ERROR			= 0x1000005,
	AIE2_STATUS_AIE_INSTRUCTION_ERROR		= 0x1000006,
	AIE2_STATUS_AIE_ECC_ERROR			= 0x1000007,
	AIE2_STATUS_AIE_LOCK_ERROR			= 0x1000008,
	AIE2_STATUS_AIE_DMA_ERROR			= 0x1000009,
	AIE2_STATUS_AIE_MEM_PARITY_ERROR		= 0x100000a,
	AIE2_STATUS_AIE_PWR_CFG_ERROR			= 0x100000b,
	AIE2_STATUS_AIE_BACKTRACK_ERROR			= 0x100000c,
	AIE2_STATUS_MAX_AIE_STATUS_CODE,
	/* MGMT ERT Error codes */
	AIE2_STATUS_MGMT_ERT_SELF_TEST_FAILURE		= 0x2000001,
	AIE2_STATUS_MGMT_ERT_HASH_MISMATCH,
	AIE2_STATUS_MGMT_ERT_NOAVAIL,
	AIE2_STATUS_MGMT_ERT_INVALID_PARAM,
	AIE2_STATUS_MGMT_ERT_ENTER_SUSPEND_FAILURE,
	AIE2_STATUS_MGMT_ERT_BUSY,
	AIE2_STATUS_MGMT_ERT_APPLICATION_ACTIVE,
	MAX_MGMT_ERT_STATUS_CODE,
	/* APP ERT Error codes */
	AIE2_STATUS_APP_ERT_FIRST_ERROR			= 0x3000001,
	AIE2_STATUS_APP_INVALID_INSTR,
	AIE2_STATUS_APP_LOAD_PDI_FAIL,
	MAX_APP_ERT_STATUS_CODE,
	/* NPU RTOS Error Codes */
	AIE2_STATUS_INVALID_INPUT_BUFFER		= 0x4000001,
	AIE2_STATUS_INVALID_COMMAND,
	AIE2_STATUS_INVALID_PARAM,
	AIE2_STATUS_INVALID_OPERATION			= 0x4000006,
	AIE2_STATUS_ASYNC_EVENT_MSGS_FULL,
	AIE2_STATUS_MAX_RTOS_STATUS_CODE,
	MAX_AIE2_STATUS_CODE
};

struct assign_mgmt_pasid_req {
	__u16	pasid;
	__u16	reserved;
} __packed;

struct assign_mgmt_pasid_resp {
	enum aie2_msg_status	status;
} __packed;

struct map_host_buffer_req {
	__u32		context_id;
	__u64		buf_addr;
	__u64		buf_size;
} __packed;

struct map_host_buffer_resp {
	enum aie2_msg_status	status;
} __packed;

#define MAX_CQ_PAIRS		2
struct cq_info {
	__u32 head_addr;
	__u32 tail_addr;
	__u32 buf_addr;
	__u32 buf_size;
};

struct cq_pair {
	struct cq_info x2i_q;
	struct cq_info i2x_q;
};

struct create_ctx_req {
	__u32	aie_type;
	__u8	start_col;
	__u8	num_col;
	__u16	reserved;
	__u8	num_cq_pairs_requested;
	__u8	reserved1;
	__u16	pasid;
	__u32	pad[2];
	__u32	sec_comm_target_type;
	__u32	context_priority;
} __packed;

struct create_ctx_resp {
	enum aie2_msg_status	status;
	__u32			context_id;
	__u16			msix_id;
	__u8			num_cq_pairs_allocated;
	__u8			reserved;
	struct cq_pair		cq_pair[MAX_CQ_PAIRS];
} __packed;

struct destroy_ctx_req {
	__u32	context_id;
} __packed;

struct destroy_ctx_resp {
	enum aie2_msg_status	status;
} __packed;

struct execute_buffer_req {
	__u32	cu_idx;
	__u32	payload[19];
} __packed;

struct exec_dpu_req {
	__u64	inst_buf_addr;
	__u32	inst_size;
	__u32	inst_prop_cnt;
	__u32	cu_idx;
	__u32	payload[35];
} __packed;

struct execute_buffer_resp {
	enum aie2_msg_status	status;
} __packed;

struct aie_tile_info {
	__u32		size;
	__u16		major;
	__u16		minor;
	__u16		cols;
	__u16		rows;
	__u16		core_rows;
	__u16		mem_rows;
	__u16		shim_rows;
	__u16		core_row_start;
	__u16		mem_row_start;
	__u16		shim_row_start;
	__u16		core_dma_channels;
	__u16		mem_dma_channels;
	__u16		shim_dma_channels;
	__u16		core_locks;
	__u16		mem_locks;
	__u16		shim_locks;
	__u16		core_events;
	__u16		mem_events;
	__u16		shim_events;
	__u16		reserved;
};

struct aie_tile_info_req {
	__u32	reserved;
} __packed;

struct aie_tile_info_resp {
	enum aie2_msg_status	status;
	struct aie_tile_info	info;
} __packed;

struct aie_version_info_req {
	__u32		reserved;
} __packed;

struct aie_version_info_resp {
	enum aie2_msg_status	status;
	__u16			major;
	__u16			minor;
} __packed;

struct aie_column_info_req {
	__u64 dump_buff_addr;
	__u32 dump_buff_size;
	__u32 num_cols;
	__u32 aie_bitmap;
} __packed;

struct aie_column_info_resp {
	enum aie2_msg_status	status;
	__u32 size;
} __packed;

struct suspend_req {
	__u32		place_holder;
} __packed;

struct suspend_resp {
	enum aie2_msg_status	status;
} __packed;

struct resume_req {
	__u32		place_holder;
} __packed;

struct resume_resp {
	enum aie2_msg_status	status;
} __packed;

struct check_header_hash_req {
	__u64		hash_high;
	__u64		hash_low;
} __packed;

struct check_header_hash_resp {
	enum aie2_msg_status	status;
} __packed;

struct query_error_req {
	__u64		buf_addr;
	__u32		buf_size;
	__u32		next_row;
	__u32		next_column;
	__u32		next_module;
} __packed;

struct query_error_resp {
	enum aie2_msg_status	status;
	__u32			num_err;
	__u32			has_next_err;
	__u32			next_row;
	__u32			next_column;
	__u32			next_module;
} __packed;

struct protocol_version_req {
	__u32		reserved;
} __packed;

struct protocol_version_resp {
	enum aie2_msg_status	status;
	__u32			major;
	__u32			minor;
} __packed;

struct firmware_version_req {
	__u32		reserved;
} __packed;

struct firmware_version_resp {
	enum aie2_msg_status	status;
	__u32			major;
	__u32			minor;
	__u32			sub;
	__u32			build;
} __packed;

#define MAX_NUM_CUS			32
#define AIE2_MSG_CFG_CU_PDI_ADDR	GENMASK(16, 0)
#define AIE2_MSG_CFG_CU_FUNC		GENMASK(24, 17)
struct config_cu_req {
	__u32	num_cus;
	__u32	cfgs[MAX_NUM_CUS];
} __packed;

struct config_cu_resp {
	enum aie2_msg_status	status;
} __packed;

struct set_runtime_cfg_req {
	__u32	type;
	__u64	value;
} __packed;

struct set_runtime_cfg_resp {
	enum aie2_msg_status	status;
} __packed;

struct get_runtime_cfg_req {
	__u32	type;
} __packed;

struct get_runtime_cfg_resp {
	enum aie2_msg_status	status;
	__u64			value;
} __packed;

enum async_event_type {
	ASYNC_EVENT_TYPE_AIE_ERROR,
	ASYNC_EVENT_TYPE_EXCEPTION,
	MAX_ASYNC_EVENT_TYPE
};

#define ASYNC_BUF_SIZE SZ_8K
struct async_event_msg_req {
	__u64 buf_addr;
	__u32 buf_size;
} __packed;

struct async_event_msg_resp {
	enum aie2_msg_status	status;
	enum async_event_type	type;
} __packed;

#define MAX_CHAIN_CMDBUF_SIZE SZ_4K
#define slot_has_space(slot, offset, payload_size)		\
	(MAX_CHAIN_CMDBUF_SIZE >= (offset) + (payload_size) +	\
	 sizeof(typeof(slot)))

struct cmd_chain_slot_execbuf_cf {
	__u32 cu_idx;
	__u32 arg_cnt;
	__u32 args[] __counted_by(arg_cnt);
};

struct cmd_chain_slot_dpu {
	__u64 inst_buf_addr;
	__u32 inst_size;
	__u32 inst_prop_cnt;
	__u32 cu_idx;
	__u32 arg_cnt;
#define MAX_DPU_ARGS_SIZE (34 * sizeof(__u32))
	__u32 args[] __counted_by(arg_cnt);
};

struct cmd_chain_req {
	__u64 buf_addr;
	__u32 buf_size;
	__u32 count;
} __packed;

struct cmd_chain_resp {
	enum aie2_msg_status	status;
	__u32			fail_cmd_idx;
	enum aie2_msg_status	fail_cmd_status;
} __packed;

#define AIE2_MSG_SYNC_BO_SRC_TYPE	GENMASK(3, 0)
#define AIE2_MSG_SYNC_BO_DST_TYPE	GENMASK(7, 4)
struct sync_bo_req {
	__u64 src_addr;
	__u64 dst_addr;
	__u32 size;
#define SYNC_BO_DEV_MEM  0
#define SYNC_BO_HOST_MEM 2
	__u32 type;
} __packed;

struct sync_bo_resp {
	enum aie2_msg_status	status;
} __packed;
#endif /* _AIE2_MSG_PRIV_H_ */
