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
#ifndef __RAS_PSP_H__
#define __RAS_PSP_H__
#include "ras.h"
#include "ras_ta_if.h"

struct ras_core_context;
struct ras_ta_trigger_error_input;
struct ras_ta_query_address_input;
struct ras_ta_query_address_output;
enum ras_ta_cmd_id;

struct ras_ta_image_header {
	uint32_t reserved1[24];
	uint32_t image_version; /* [0x60] Off Chip Firmware Version */
	uint32_t reserved2[39];
};

struct ras_psp_sys_status {
	bool  initialized;
	uint32_t session_id;
	void *psp_cmd_mutex;
};

struct ras_ta_init_param {
	uint8_t poison_mode_en;
	uint8_t dgpu_mode;
	uint16_t xcc_mask;
	uint8_t channel_dis_num;
	uint8_t nps_mode;
	uint32_t active_umc_mask;
	uint8_t vram_type;
};

struct gpu_mem_block {
	uint32_t mem_type;
	void *mem_bo;
	uint64_t mem_mc_addr;
	void *mem_cpu_addr;
	uint32_t mem_size;
	int ref_count;
	void *private;
};

struct ras_psp_ip_func {
	uint32_t (*psp_ras_ring_wptr_get)(struct ras_core_context *ras_core);
	int (*psp_ras_ring_wptr_set)(struct ras_core_context *ras_core, uint32_t wptr);
};

struct ras_psp_ring {
	struct gpu_mem_block ras_ring_gpu_mem;
};

struct psp_cmd_resp {
	uint32_t status;
	uint32_t session_id;
};

struct ras_psp_ctx {
	void *external_mutex;
	struct mutex internal_mutex;
	uint64_t in_fence_value;
	struct gpu_mem_block psp_cmd_gpu_mem;
	struct gpu_mem_block out_fence_gpu_mem;
};

struct ras_ta_fw_bin {
	uint32_t fw_version;
	uint32_t feature_version;
	uint32_t bin_size;
	uint8_t *bin_addr;
};

struct ras_ta_ctx {
	bool  preload_ras_ta_enabled;
	bool  ras_ta_initialized;
	uint32_t  session_id;
	uint32_t  resp_status;
	uint32_t  ta_version;
	struct mutex ta_mutex;
	struct ras_ta_fw_bin fw_bin;
	struct ras_ta_init_param init_param;
	struct gpu_mem_block fw_gpu_mem;
	struct gpu_mem_block cmd_gpu_mem;
};

struct ras_psp {
	uint32_t psp_ip_version;
	struct ras_psp_ring psp_ring;
	struct ras_psp_ctx  psp_ctx;
	struct ras_ta_ctx   ta_ctx;
	const struct ras_psp_ip_func *ip_func;
	const struct ras_psp_sys_func *sys_func;
};

struct ras_psp_ta_load {
	uint32_t fw_version;
	uint32_t feature_version;
	uint32_t bin_size;
	uint8_t *bin_addr;
	uint64_t out_session_id;
	uint32_t out_loaded_ta_version;
};

struct ras_psp_ta_unload {
	uint64_t ras_session_id;
};

int ras_psp_sw_init(struct ras_core_context *ras_core);
int ras_psp_sw_fini(struct ras_core_context *ras_core);
int ras_psp_hw_init(struct ras_core_context *ras_core);
int ras_psp_hw_fini(struct ras_core_context *ras_core);
int ras_psp_load_firmware(struct ras_core_context *ras_core,
		struct ras_psp_ta_load *ras_ta_load);
int ras_psp_unload_firmware(struct ras_core_context *ras_core,
		struct ras_psp_ta_unload *ras_ta_unload);
int ras_psp_trigger_error(struct ras_core_context *ras_core,
	struct ras_ta_trigger_error_input *info, uint32_t instance_mask);
int ras_psp_query_address(struct ras_core_context *ras_core,
		struct ras_ta_query_address_input *addr_in,
		struct ras_ta_query_address_output *addr_out);
bool ras_psp_check_supported_cmd(struct ras_core_context *ras_core,
		enum ras_ta_cmd_id cmd_id);
#endif
