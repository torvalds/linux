/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
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
 * Authors: AMD
 *
 */

#ifndef __DAL_DCHUBBUB_H__
#define __DAL_DCHUBBUB_H__

/**
 * DOC: overview
 *
 * There is only one common DCHUBBUB. It contains the common request and return
 * blocks for the Data Fabric Interface that are not clock/power gated.
 */

enum dcc_control {
	dcc_control__256_256_xxx,
	dcc_control__128_128_xxx,
	dcc_control__256_64_64,
	dcc_control__256_128_128,
};

enum segment_order {
	segment_order__na,
	segment_order__contiguous,
	segment_order__non_contiguous,
};

struct dcn_hubbub_wm_set {
	uint32_t wm_set;
	uint32_t data_urgent;
	uint32_t pte_meta_urgent;
	uint32_t sr_enter;
	uint32_t sr_exit;
	uint32_t dram_clk_change;
	uint32_t usr_retrain;
	uint32_t fclk_pstate_change;
	uint32_t sr_enter_exit_Z8;
	uint32_t sr_enter_Z8;
};

struct dcn_hubbub_wm {
	struct dcn_hubbub_wm_set sets[4];
};

enum dcn_hubbub_page_table_depth {
	DCN_PAGE_TABLE_DEPTH_1_LEVEL,
	DCN_PAGE_TABLE_DEPTH_2_LEVEL,
	DCN_PAGE_TABLE_DEPTH_3_LEVEL,
	DCN_PAGE_TABLE_DEPTH_4_LEVEL
};

enum dcn_hubbub_page_table_block_size {
	DCN_PAGE_TABLE_BLOCK_SIZE_4KB = 0,
	DCN_PAGE_TABLE_BLOCK_SIZE_64KB = 4,
	DCN_PAGE_TABLE_BLOCK_SIZE_32KB = 3
};

struct dcn_hubbub_phys_addr_config {
	struct {
		uint64_t fb_top;
		uint64_t fb_offset;
		uint64_t fb_base;
		uint64_t agp_top;
		uint64_t agp_bot;
		uint64_t agp_base;
	} system_aperture;

	struct {
		uint64_t page_table_start_addr;
		uint64_t page_table_end_addr;
		uint64_t page_table_base_addr;
	} gart_config;

	uint64_t page_table_default_page_addr;
};

struct dcn_hubbub_virt_addr_config {
	uint64_t				page_table_start_addr;
	uint64_t				page_table_end_addr;
	enum dcn_hubbub_page_table_block_size	page_table_block_size;
	enum dcn_hubbub_page_table_depth	page_table_depth;
	uint64_t				page_table_base_addr;
};

struct hubbub_addr_config {
	struct dcn_hubbub_phys_addr_config pa_config;
	struct dcn_hubbub_virt_addr_config va_config;
	struct {
		uint64_t aperture_check_fault;
		uint64_t generic_fault;
	} default_addrs;
};

struct dcn_hubbub_state {
	uint32_t vm_fault_addr_msb;
	uint32_t vm_fault_addr_lsb;
	uint32_t vm_error_status;
	uint32_t vm_error_vmid;
	uint32_t vm_error_pipe;
	uint32_t vm_error_mode;
	uint32_t test_debug_data;
	uint32_t watermark_change_cntl;
	uint32_t dram_state_cntl;
};

struct hubbub_funcs {
	void (*update_dchub)(
			struct hubbub *hubbub,
			struct dchub_init_data *dh_data);

	int (*init_dchub_sys_ctx)(
			struct hubbub *hubbub,
			struct dcn_hubbub_phys_addr_config *pa_config);
	void (*init_vm_ctx)(
			struct hubbub *hubbub,
			struct dcn_hubbub_virt_addr_config *va_config,
			int vmid);

	bool (*get_dcc_compression_cap)(struct hubbub *hubbub,
			const struct dc_dcc_surface_param *input,
			struct dc_surface_dcc_cap *output);

	bool (*dcc_support_swizzle)(
			enum swizzle_mode_values swizzle,
			unsigned int bytes_per_element,
			enum segment_order *segment_order_horz,
			enum segment_order *segment_order_vert);

	bool (*dcc_support_pixel_format)(
			enum surface_pixel_format format,
			unsigned int *bytes_per_element);

	void (*wm_read_state)(struct hubbub *hubbub,
			struct dcn_hubbub_wm *wm);

	void (*get_dchub_ref_freq)(struct hubbub *hubbub,
			unsigned int dccg_ref_freq_inKhz,
			unsigned int *dchub_ref_freq_inKhz);

	bool (*program_watermarks)(
			struct hubbub *hubbub,
			union dcn_watermark_set *watermarks,
			unsigned int refclk_mhz,
			bool safe_to_lower);

	bool (*is_allow_self_refresh_enabled)(struct hubbub *hubbub);
	void (*allow_self_refresh_control)(struct hubbub *hubbub, bool allow);

	bool (*verify_allow_pstate_change_high)(struct hubbub *hubbub);

	void (*apply_DEDCN21_147_wa)(struct hubbub *hubbub);

	void (*force_wm_propagate_to_pipes)(struct hubbub *hubbub);

	void (*hubbub_read_state)(struct hubbub *hubbub, struct dcn_hubbub_state *hubbub_state);

	void (*force_pstate_change_control)(struct hubbub *hubbub, bool force, bool allow);

	void (*init_watermarks)(struct hubbub *hubbub);

	/**
	 * @program_det_size:
	 *
	 * DE-Tile buffers (DET) is a memory that is used to convert the tiled
	 * data into linear, which the rest of the display can use to generate
	 * the graphics output. One of the main features of this component is
	 * that each pipe has a configurable DET buffer which means that when a
	 * pipe is not enabled, the device can assign the memory to other
	 * enabled pipes to try to be more efficient.
	 *
	 * DET logic is handled by dchubbub. Some ASICs provide a feature named
	 * Configurable Return Buffer (CRB) segments which can be allocated to
	 * compressed or detiled buffers.
	 */
	void (*program_det_size)(struct hubbub *hubbub, int hubp_inst, unsigned det_buffer_size_in_kbyte);
	void (*wait_for_det_apply)(struct hubbub *hubbub, int hubp_inst);
	void (*program_compbuf_size)(struct hubbub *hubbub, unsigned compbuf_size_kb, bool safe_to_increase);
	void (*init_crb)(struct hubbub *hubbub);
	void (*force_usr_retraining_allow)(struct hubbub *hubbub, bool allow);
	void (*set_request_limit)(struct hubbub *hubbub, int memory_channel_count, int words_per_channel);
	void (*dchubbub_init)(struct hubbub *hubbub);
	void (*get_mall_en)(struct hubbub *hubbub, unsigned int *mall_in_use);
};

struct hubbub {
	const struct hubbub_funcs *funcs;
	struct dc_context *ctx;
	bool riommu_active;
};

#endif
