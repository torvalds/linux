/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2025 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef __RAS_CMD_H__
#define __RAS_CMD_H__
#include "ras.h"
#include "ras_eeprom.h"
#include "ras_log_ring.h"
#include "ras_cper.h"

#define RAS_CMD_DEV_HANDLE_MAGIC 0xFEEDAD00UL

#define RAS_CMD_MAX_IN_SIZE 256
#define RAS_CMD_MAX_GPU_NUM 32
#define RAS_CMD_MAX_BAD_PAGES_PER_GROUP 32

/* position of instance value in sub_block_index of
 * ta_ras_trigger_error_input, the sub block uses lower 12 bits
 */
#define RAS_TA_INST_MASK 0xfffff000
#define RAS_TA_INST_SHIFT 0xc

enum ras_cmd_interface_type {
	RAS_CMD_INTERFACE_TYPE_NONE,
	RAS_CMD_INTERFACE_TYPE_AMDGPU,
	RAS_CMD_INTERFACE_TYPE_VF,
	RAS_CMD_INTERFACE_TYPE_PF,
};

enum ras_cmd_id_range {
	RAS_CMD_ID_COMMON_START = 0,
	RAS_CMD_ID_COMMON_END = 0x10000,
	RAS_CMD_ID_AMDGPU_START = RAS_CMD_ID_COMMON_END,
	RAS_CMD_ID_AMDGPU_END = 0x20000,
	RAS_CMD_ID_MXGPU_START = RAS_CMD_ID_AMDGPU_END,
	RAS_CMD_ID_MXGPU_END = 0x30000,
	RAS_CMD_ID_MXGPU_VF_START = RAS_CMD_ID_MXGPU_END,
	RAS_CMD_ID_MXGPU_VF_END = 0x40000,
};

enum ras_cmd_id {
	RAS_CMD__BEGIN = RAS_CMD_ID_COMMON_START,
	RAS_CMD__QUERY_INTERFACE_INFO,
	RAS_CMD__GET_DEVICES_INFO,
	RAS_CMD__GET_BLOCK_ECC_STATUS,
	RAS_CMD__INJECT_ERROR,
	RAS_CMD__GET_BAD_PAGES,
	RAS_CMD__CLEAR_BAD_PAGE_INFO,
	RAS_CMD__RESET_ALL_ERROR_COUNTS,
	RAS_CMD__GET_SAFE_FB_ADDRESS_RANGES,
	RAS_CMD__TRANSLATE_FB_ADDRESS,
	RAS_CMD__GET_LINK_TOPOLOGY,
	RAS_CMD__GET_CPER_SNAPSHOT,
	RAS_CMD__GET_CPER_RECORD,
	RAS_CMD__GET_BATCH_TRACE_SNAPSHOT,
	RAS_CMD__GET_BATCH_TRACE_RECORD,
	RAS_CMD__SUPPORTED_MAX = RAS_CMD_ID_COMMON_END,
};

enum ras_cmd_response {
	RAS_CMD__SUCCESS = 0,
	RAS_CMD__SUCCESS_EXEED_BUFFER,
	RAS_CMD__ERROR_UKNOWN_CMD,
	RAS_CMD__ERROR_INVALID_CMD,
	RAS_CMD__ERROR_VERSION,
	RAS_CMD__ERROR_INVALID_INPUT_SIZE,
	RAS_CMD__ERROR_INVALID_INPUT_DATA,
	RAS_CMD__ERROR_DRV_INIT_FAIL,
	RAS_CMD__ERROR_ACCESS_DENIED,
	RAS_CMD__ERROR_GENERIC,
	RAS_CMD__ERROR_TIMEOUT,
};

enum ras_error_type {
	RAS_TYPE_ERROR__NONE = 0,
	RAS_TYPE_ERROR__PARITY = 1,
	RAS_TYPE_ERROR__SINGLE_CORRECTABLE = 2,
	RAS_TYPE_ERROR__MULTI_UNCORRECTABLE = 4,
	RAS_TYPE_ERROR__POISON = 8,
};

struct ras_core_context;
struct ras_cmd_ctx;

struct ras_cmd_mgr {
	struct list_head head;
	struct ras_core_context *ras_core;
	uint64_t dev_handle;
};

struct ras_cmd_func_map {
	uint32_t cmd_id;
	int (*func)(struct ras_core_context *ras_core,
			struct ras_cmd_ctx *cmd, void *data);
};

struct ras_device_bdf {
	union {
		struct {
			uint32_t function : 3;
			uint32_t device : 5;
			uint32_t bus : 8;
			uint32_t domain : 16;
		};
		uint32_t u32_all;
	};
};

struct ras_cmd_param {
	uint32_t idx_vf;
	void *data;
};

#pragma pack(push, 8)
struct ras_cmd_ctx {
	uint32_t magic;
	union {
		struct {
			uint16_t ras_cmd_minor_ver : 10;
			uint16_t ras_cmd_major_ver : 6;
		};
		uint16_t ras_cmd_ver;
	};
	union {
		struct {
			uint16_t plat_major_ver : 10;
			uint16_t plat_minor_ver : 6;
		};
		uint16_t plat_ver;
	};
	uint32_t cmd_id;
	uint32_t cmd_res;
	uint32_t input_size;
	uint32_t output_size;
	uint32_t output_buf_size;
	uint32_t reserved[5];
	uint8_t  input_buff_raw[RAS_CMD_MAX_IN_SIZE];
	uint8_t  output_buff_raw[];
};

struct ras_cmd_dev_handle {
	uint64_t dev_handle;
};

struct ras_cmd_block_ecc_info_req {
	struct ras_cmd_dev_handle dev;
	uint32_t block_id;
	uint32_t subblock_id;
	uint32_t reserved[4];
};

struct ras_cmd_block_ecc_info_rsp {
	uint32_t version;
	uint32_t ce_count;
	uint32_t ue_count;
	uint32_t de_count;
	uint32_t reserved[6];
};

struct ras_cmd_inject_error_req {
	struct ras_cmd_dev_handle dev;
	uint32_t block_id;
	uint32_t subblock_id;
	uint64_t address;
	uint32_t error_type;
	uint32_t instance_mask;
	union {
		struct {
			/* vf index */
			uint64_t vf_idx : 6;
			/* method of error injection. i.e persistent, coherent etc */
			uint64_t method : 10;
			uint64_t rsv    : 48;
		};
		uint64_t value;
	};
	uint32_t reserved[8];
};

struct ras_cmd_inject_error_rsp {
	uint32_t version;
	uint32_t reserved[5];
	uint64_t address;
};

struct ras_cmd_dev_info {
	uint64_t dev_handle;
	uint32_t location_id;
	uint32_t ecc_enabled;
	uint32_t ecc_supported;
	uint32_t vf_num;
	uint32_t asic_type;
	uint32_t oam_id;
	uint32_t reserved[8];
};

struct ras_cmd_devices_info_rsp {
	uint32_t version;
	uint32_t dev_num;
	uint32_t reserved[6];
	struct ras_cmd_dev_info devs[RAS_CMD_MAX_GPU_NUM];
};

struct ras_cmd_bad_page_record {
	union {
		uint64_t address;
		uint64_t offset;
	};
	uint64_t retired_page;
	uint64_t ts;

	uint32_t err_type;

	union {
		unsigned char bank;
		unsigned char cu;
	};

	unsigned char mem_channel;
	unsigned char mcumc_id;

	unsigned char valid;
	unsigned char reserved[8];
};

struct ras_cmd_bad_pages_info_req {
	struct ras_cmd_dev_handle device;
	uint32_t group_index;
	uint32_t reserved[5];
};

struct ras_cmd_bad_pages_info_rsp {
	uint32_t version;
	uint32_t group_index;
	uint32_t bp_in_group;
	uint32_t bp_total_cnt;
	uint32_t reserved[4];
	struct ras_cmd_bad_page_record records[RAS_CMD_MAX_BAD_PAGES_PER_GROUP];
};

struct ras_query_interface_info_req {
	uint32_t reserved[8];
};

struct ras_query_interface_info_rsp {
	uint32_t version;
	uint32_t ras_cmd_major_ver;
	uint32_t ras_cmd_minor_ver;
	uint32_t plat_major_ver;
	uint32_t plat_minor_ver;
	uint8_t  interface_type;
	uint8_t  rsv[3];
	uint32_t reserved[8];
};

#define RAS_MAX_NUM_SAFE_RANGES 64
struct ras_cmd_ras_safe_fb_address_ranges_rsp {
	uint32_t version;
	uint32_t num_ranges;
	uint32_t reserved[4];
	struct {
		uint64_t start;
		uint64_t size;
		uint32_t idx;
		uint32_t reserved[3];
	} range[RAS_MAX_NUM_SAFE_RANGES];
};

enum ras_fb_addr_type {
	RAS_FB_ADDR_SOC_PHY, /* SPA */
	RAS_FB_ADDR_BANK,
	RAS_FB_ADDR_VF_PHY, /* GPA */
	RAS_FB_ADDR_UNKNOWN
};

struct ras_fb_bank_addr {
	uint32_t stack_id; /* SID */
	uint32_t bank_group;
	uint32_t bank;
	uint32_t row;
	uint32_t column;
	uint32_t channel;
	uint32_t subchannel; /* Also called Pseudochannel (PC) */
	uint32_t reserved[3];
};

struct ras_fb_vf_phy_addr {
	uint32_t vf_idx;
	uint32_t reserved;
	uint64_t addr;
};

union ras_translate_fb_address {
	struct ras_fb_bank_addr bank_addr;
	uint64_t soc_phy_addr;
	struct ras_fb_vf_phy_addr vf_phy_addr;
};

struct ras_cmd_translate_fb_address_req {
	struct ras_cmd_dev_handle dev;
	enum ras_fb_addr_type src_addr_type;
	enum ras_fb_addr_type dest_addr_type;
	union ras_translate_fb_address trans_addr;
};

struct ras_cmd_translate_fb_address_rsp {
	uint32_t version;
	uint32_t reserved[5];
	union ras_translate_fb_address trans_addr;
};

struct ras_dev_link_topology_req {
	struct ras_cmd_dev_handle src;
	struct ras_cmd_dev_handle dst;
};

struct ras_dev_link_topology_rsp {
	uint32_t  version;
	uint32_t  link_status;  /* HW status of the link */
	uint32_t  link_type;    /* type of the link */
	uint32_t  num_hops;     /* number of hops */
	uint32_t reserved[8];
};

struct ras_cmd_cper_snapshot_req {
	struct ras_cmd_dev_handle dev;
};

struct ras_cmd_cper_snapshot_rsp {
	uint32_t version;
	uint32_t reserved[4];
	uint32_t total_cper_num;
	uint64_t start_cper_id;
	uint64_t latest_cper_id;
};

struct ras_cmd_cper_record_req {
	struct ras_cmd_dev_handle dev;
	uint64_t cper_start_id;
	uint32_t cper_num;
	uint32_t buf_size;
	uint64_t buf_ptr;
	uint32_t reserved[4];
};

struct ras_cmd_cper_record_rsp {
	uint32_t version;
	uint32_t real_data_size;
	uint32_t real_cper_num;
	uint32_t remain_num;
	uint32_t reserved[4];
};

struct ras_cmd_batch_trace_snapshot_req {
	struct ras_cmd_dev_handle dev;
};

struct ras_cmd_batch_trace_snapshot_rsp {
	uint32_t version;
	uint32_t reserved[4];
	uint32_t total_batch_num;
	uint64_t start_batch_id;
	uint64_t latest_batch_id;
};

struct ras_cmd_batch_trace_record_req {
	struct ras_cmd_dev_handle dev;
	uint64_t start_batch_id;
	uint32_t batch_num;
	uint32_t reserved[5];
};

struct batch_ras_trace_info {
	uint64_t batch_id;
	uint16_t offset;
	uint8_t  trace_num;
	uint8_t  rsv;
	uint32_t reserved;
};

#define RAS_CMD_MAX_BATCH_NUM  300
#define RAS_CMD_MAX_TRACE_NUM  300
struct ras_cmd_batch_trace_record_rsp {
	uint32_t version;
	uint16_t real_batch_num;
	uint16_t remain_num;
	uint64_t start_batch_id;
	uint32_t reserved[2];
	struct batch_ras_trace_info batchs[RAS_CMD_MAX_BATCH_NUM];
	struct ras_log_info records[RAS_CMD_MAX_TRACE_NUM];
};

#pragma pack(pop)

int ras_cmd_init(struct ras_core_context *ras_core);
int ras_cmd_fini(struct ras_core_context *ras_core);
int rascore_handle_cmd(struct ras_core_context *ras_core, struct ras_cmd_ctx *cmd, void *data);
uint64_t ras_cmd_get_dev_handle(struct ras_core_context *ras_core);
int ras_cmd_query_interface_info(struct ras_core_context *ras_core,
	struct ras_query_interface_info_rsp *rsp);
int ras_cmd_translate_soc_pa_to_bank(struct ras_core_context *ras_core,
			uint64_t soc_pa, struct ras_fb_bank_addr *bank_addr);
int ras_cmd_translate_bank_to_soc_pa(struct ras_core_context *ras_core,
			struct ras_fb_bank_addr bank_addr, uint64_t *soc_pa);
#endif
