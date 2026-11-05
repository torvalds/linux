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
#include "ta_if.h"

#define MAX_RAS_BLOCK_MASK_BITS 56

struct ras_core_context;
struct ras_ta_trigger_error_input;
struct ras_ta_query_address_input;
struct ras_ta_query_address_output;
enum ras_ta_cmd_id;

struct ras_psp_addr_trans_in {
	uint64_t mca_addr;
	uint64_t ipid;
	uint32_t nps;
};

struct ras_psp_addr_trans_out {
	uint32_t channel_id;
	uint8_t  socket_id;
	uint8_t  mem_die_id;
	uint8_t  dram_entity_id;
	uint8_t  umc_inst_id;
	uint64_t row_pa;
	/* Bitmask of flipping bits across all bad page PAs in a row */
	uint64_t pa_flip_mask;
};

struct ras_ta_image_header {
	uint32_t reserved1[24];
	uint32_t image_version; /* [0x60] Off Chip Firmware Version */
	uint32_t reserved2[39];
};

struct ras_psp_sys_status {
	void *psp_cmd_mutex;
	bool use_dedicated_memory;
	bool uniras_load_fw;
};

struct ras_ta_param {
	uint8_t poison_mode_en;
	uint8_t dgpu_mode;
	uint16_t xcc_mask;
	uint8_t channel_dis_num;
	uint8_t nps_mode;
	uint32_t active_umc_mask;
	uint8_t vram_type;
	uint32_t ext_umc_mask;
};

struct ras_fw_bin {
	uint32_t fw_version;
	uint32_t feature_version;
	uint32_t bin_size;
	uint8_t *bin_addr;
};

struct ras_fw_param {
	struct ras_fw_bin rl_bin;
	struct ras_fw_bin ta_bin;
};

struct ras_param {
	struct ras_fw_param  fw_param;
	struct ras_ta_param  ta_param;
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

struct ras_block_map {
	uint32_t ras_id;
	uint32_t ta_id;
};

union ras_feature {
	struct {
		uint64_t block_mask : MAX_RAS_BLOCK_MASK_BITS;
		uint64_t rsv: 4;
		uint64_t tag: 3;
		uint64_t en : 1;
	};
	uint64_t value;
};

struct ras_hw_caps {
	bool poison_supported;
	bool flex_mca_enabled;
	union ras_feature features;
};

struct ras_psp_ip_func {
	uint32_t (*psp_ras_ring_wptr_get)(struct ras_core_context *ras_core);
	int (*psp_ras_ring_wptr_set)(struct ras_core_context *ras_core, uint32_t wptr);
	int (*get_ras_block_maps)(struct ras_core_context *ras_core,
			struct ras_block_map **blk_maps, uint32_t *maps_size);
	int (*get_ras_hw_caps)(struct ras_core_context *ras_core,
			struct ras_hw_caps *ras_cap);
};

struct ras_psp_ring {
	struct gpu_mem_block *ras_ring_gpu_mem;
};

struct psp_cmd_resp {
	uint32_t status;
	uint32_t session_id;
};

struct ras_psp_ctx {
	void *external_mutex;
	struct mutex internal_mutex;
	uint64_t in_fence_value;
	struct gpu_mem_block *psp_cmd_gpu_mem;
	struct gpu_mem_block *out_fence_gpu_mem;
};

struct ras_ta_ctx {
	bool  ras_ta_initialized;
	uint32_t  session_id;
	uint32_t  resp_status;
	uint32_t  ta_version;
	struct mutex ta_mutex;
	struct ras_fw_bin fw_bin;
	struct gpu_mem_block *fw_gpu_mem;
	struct gpu_mem_block *cmd_gpu_mem;
};

struct ras_psp {
	bool use_dedicated_memory;
	bool  load_ras_fw_internal;
	uint32_t psp_ip_version;
	struct ras_block_map *blk_maps;
	uint32_t maps_size;
	struct ras_hw_caps ras_hw_caps;
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

int ras_psp_reload_firmwares(struct ras_core_context *ras_core, uint32_t flags);
int ras_psp_sideload_ras_ta(struct ras_core_context *ras_core,
		struct ras_psp_ta_load *ta_load);
int ras_psp_unsideload_ras_ta(struct ras_core_context *ras_core,
		struct ras_psp_ta_unload *ras_ta_unload);
int ras_psp_enable_features(struct ras_core_context *ras_core,
	struct ras_ta_enable_features_input *info, bool enable);
int ras_psp_trigger_error(struct ras_core_context *ras_core,
	struct ras_ta_trigger_error_input *info, uint32_t instance_mask);
int ras_psp_query_address(struct ras_core_context *ras_core,
		struct ras_ta_query_address_input *addr_in,
		struct ras_ta_query_address_output *addr_out);
bool ras_psp_check_supported_cmd(struct ras_core_context *ras_core,
		enum ras_ta_cmd_id cmd_id);
int ras_psp_get_block_ta_id(struct ras_core_context *ras_core,
		uint32_t ras_id, uint32_t *ta_id);
bool ras_psp_poison_supported(struct ras_core_context *ras_core);
bool ras_psp_flex_mca_enabled(struct ras_core_context *ras_core);
uint64_t ras_psp_get_hw_ras_caps(struct ras_core_context *ras_core);
int ras_psp_translate_addr(struct ras_core_context *ras_core,
	struct ras_psp_addr_trans_in *in, struct ras_psp_addr_trans_out *out);
#endif
