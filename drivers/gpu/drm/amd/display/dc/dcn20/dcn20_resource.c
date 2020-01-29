/*
* Copyright 2016 Advanced Micro Devices, Inc.
 * Copyright 2019 Raptor Engineering, LLC
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

#include <linux/slab.h>

#include "dm_services.h"
#include "dc.h"

#include "dcn20_init.h"

#include "resource.h"
#include "include/irq_service_interface.h"
#include "dcn20/dcn20_resource.h"

#include "dcn10/dcn10_hubp.h"
#include "dcn10/dcn10_ipp.h"
#include "dcn20_hubbub.h"
#include "dcn20_mpc.h"
#include "dcn20_hubp.h"
#include "irq/dcn20/irq_service_dcn20.h"
#include "dcn20_dpp.h"
#include "dcn20_optc.h"
#include "dcn20_hwseq.h"
#include "dce110/dce110_hw_sequencer.h"
#include "dcn10/dcn10_resource.h"
#include "dcn20_opp.h"

#include "dcn20_dsc.h"

#include "dcn20_link_encoder.h"
#include "dcn20_stream_encoder.h"
#include "dce/dce_clock_source.h"
#include "dce/dce_audio.h"
#include "dce/dce_hwseq.h"
#include "virtual/virtual_stream_encoder.h"
#include "dce110/dce110_resource.h"
#include "dml/display_mode_vba.h"
#include "dcn20_dccg.h"
#include "dcn20_vmid.h"
#include "dc_link_ddc.h"

#include "navi10_ip_offset.h"

#include "dcn/dcn_2_0_0_offset.h"
#include "dcn/dcn_2_0_0_sh_mask.h"
#include "dpcs/dpcs_2_0_0_offset.h"
#include "dpcs/dpcs_2_0_0_sh_mask.h"

#include "nbio/nbio_2_3_offset.h"

#include "dcn20/dcn20_dwb.h"
#include "dcn20/dcn20_mmhubbub.h"

#include "mmhub/mmhub_2_0_0_offset.h"
#include "mmhub/mmhub_2_0_0_sh_mask.h"

#include "reg_helper.h"
#include "dce/dce_abm.h"
#include "dce/dce_dmcu.h"
#include "dce/dce_aux.h"
#include "dce/dce_i2c.h"
#include "vm_helper.h"

#include "amdgpu_socbb.h"

#define DC_LOGGER_INIT(logger)

struct _vcs_dpi_ip_params_st dcn2_0_ip = {
	.odm_capable = 1,
	.gpuvm_enable = 0,
	.hostvm_enable = 0,
	.gpuvm_max_page_table_levels = 4,
	.hostvm_max_page_table_levels = 4,
	.hostvm_cached_page_table_levels = 0,
	.pte_group_size_bytes = 2048,
	.num_dsc = 6,
	.rob_buffer_size_kbytes = 168,
	.det_buffer_size_kbytes = 164,
	.dpte_buffer_size_in_pte_reqs_luma = 84,
	.pde_proc_buffer_size_64k_reqs = 48,
	.dpp_output_buffer_pixels = 2560,
	.opp_output_buffer_lines = 1,
	.pixel_chunk_size_kbytes = 8,
	.pte_chunk_size_kbytes = 2,
	.meta_chunk_size_kbytes = 2,
	.writeback_chunk_size_kbytes = 2,
	.line_buffer_size_bits = 789504,
	.is_line_buffer_bpp_fixed = 0,
	.line_buffer_fixed_bpp = 0,
	.dcc_supported = true,
	.max_line_buffer_lines = 12,
	.writeback_luma_buffer_size_kbytes = 12,
	.writeback_chroma_buffer_size_kbytes = 8,
	.writeback_chroma_line_buffer_width_pixels = 4,
	.writeback_max_hscl_ratio = 1,
	.writeback_max_vscl_ratio = 1,
	.writeback_min_hscl_ratio = 1,
	.writeback_min_vscl_ratio = 1,
	.writeback_max_hscl_taps = 12,
	.writeback_max_vscl_taps = 12,
	.writeback_line_buffer_luma_buffer_size = 0,
	.writeback_line_buffer_chroma_buffer_size = 14643,
	.cursor_buffer_size = 8,
	.cursor_chunk_size = 2,
	.max_num_otg = 6,
	.max_num_dpp = 6,
	.max_num_wb = 1,
	.max_dchub_pscl_bw_pix_per_clk = 4,
	.max_pscl_lb_bw_pix_per_clk = 2,
	.max_lb_vscl_bw_pix_per_clk = 4,
	.max_vscl_hscl_bw_pix_per_clk = 4,
	.max_hscl_ratio = 8,
	.max_vscl_ratio = 8,
	.hscl_mults = 4,
	.vscl_mults = 4,
	.max_hscl_taps = 8,
	.max_vscl_taps = 8,
	.dispclk_ramp_margin_percent = 1,
	.underscan_factor = 1.10,
	.min_vblank_lines = 32, //
	.dppclk_delay_subtotal = 77, //
	.dppclk_delay_scl_lb_only = 16,
	.dppclk_delay_scl = 50,
	.dppclk_delay_cnvc_formatter = 8,
	.dppclk_delay_cnvc_cursor = 6,
	.dispclk_delay_subtotal = 87, //
	.dcfclk_cstate_latency = 10, // SRExitTime
	.max_inter_dcn_tile_repeaters = 8,

	.xfc_supported = true,
	.xfc_fill_bw_overhead_percent = 10.0,
	.xfc_fill_constant_bytes = 0,
	.number_of_cursors = 1,
};

struct _vcs_dpi_ip_params_st dcn2_0_nv14_ip = {
	.odm_capable = 1,
	.gpuvm_enable = 0,
	.hostvm_enable = 0,
	.gpuvm_max_page_table_levels = 4,
	.hostvm_max_page_table_levels = 4,
	.hostvm_cached_page_table_levels = 0,
	.num_dsc = 5,
	.rob_buffer_size_kbytes = 168,
	.det_buffer_size_kbytes = 164,
	.dpte_buffer_size_in_pte_reqs_luma = 84,
	.dpte_buffer_size_in_pte_reqs_chroma = 42,//todo
	.dpp_output_buffer_pixels = 2560,
	.opp_output_buffer_lines = 1,
	.pixel_chunk_size_kbytes = 8,
	.pte_enable = 1,
	.max_page_table_levels = 4,
	.pte_chunk_size_kbytes = 2,
	.meta_chunk_size_kbytes = 2,
	.writeback_chunk_size_kbytes = 2,
	.line_buffer_size_bits = 789504,
	.is_line_buffer_bpp_fixed = 0,
	.line_buffer_fixed_bpp = 0,
	.dcc_supported = true,
	.max_line_buffer_lines = 12,
	.writeback_luma_buffer_size_kbytes = 12,
	.writeback_chroma_buffer_size_kbytes = 8,
	.writeback_chroma_line_buffer_width_pixels = 4,
	.writeback_max_hscl_ratio = 1,
	.writeback_max_vscl_ratio = 1,
	.writeback_min_hscl_ratio = 1,
	.writeback_min_vscl_ratio = 1,
	.writeback_max_hscl_taps = 12,
	.writeback_max_vscl_taps = 12,
	.writeback_line_buffer_luma_buffer_size = 0,
	.writeback_line_buffer_chroma_buffer_size = 14643,
	.cursor_buffer_size = 8,
	.cursor_chunk_size = 2,
	.max_num_otg = 5,
	.max_num_dpp = 5,
	.max_num_wb = 1,
	.max_dchub_pscl_bw_pix_per_clk = 4,
	.max_pscl_lb_bw_pix_per_clk = 2,
	.max_lb_vscl_bw_pix_per_clk = 4,
	.max_vscl_hscl_bw_pix_per_clk = 4,
	.max_hscl_ratio = 8,
	.max_vscl_ratio = 8,
	.hscl_mults = 4,
	.vscl_mults = 4,
	.max_hscl_taps = 8,
	.max_vscl_taps = 8,
	.dispclk_ramp_margin_percent = 1,
	.underscan_factor = 1.10,
	.min_vblank_lines = 32, //
	.dppclk_delay_subtotal = 77, //
	.dppclk_delay_scl_lb_only = 16,
	.dppclk_delay_scl = 50,
	.dppclk_delay_cnvc_formatter = 8,
	.dppclk_delay_cnvc_cursor = 6,
	.dispclk_delay_subtotal = 87, //
	.dcfclk_cstate_latency = 10, // SRExitTime
	.max_inter_dcn_tile_repeaters = 8,
	.xfc_supported = true,
	.xfc_fill_bw_overhead_percent = 10.0,
	.xfc_fill_constant_bytes = 0,
	.ptoi_supported = 0,
	.number_of_cursors = 1,
};

struct _vcs_dpi_soc_bounding_box_st dcn2_0_soc = {
	/* Defaults that get patched on driver load from firmware. */
	.clock_limits = {
			{
				.state = 0,
				.dcfclk_mhz = 560.0,
				.fabricclk_mhz = 560.0,
				.dispclk_mhz = 513.0,
				.dppclk_mhz = 513.0,
				.phyclk_mhz = 540.0,
				.socclk_mhz = 560.0,
				.dscclk_mhz = 171.0,
				.dram_speed_mts = 8960.0,
			},
			{
				.state = 1,
				.dcfclk_mhz = 694.0,
				.fabricclk_mhz = 694.0,
				.dispclk_mhz = 642.0,
				.dppclk_mhz = 642.0,
				.phyclk_mhz = 600.0,
				.socclk_mhz = 694.0,
				.dscclk_mhz = 214.0,
				.dram_speed_mts = 11104.0,
			},
			{
				.state = 2,
				.dcfclk_mhz = 875.0,
				.fabricclk_mhz = 875.0,
				.dispclk_mhz = 734.0,
				.dppclk_mhz = 734.0,
				.phyclk_mhz = 810.0,
				.socclk_mhz = 875.0,
				.dscclk_mhz = 245.0,
				.dram_speed_mts = 14000.0,
			},
			{
				.state = 3,
				.dcfclk_mhz = 1000.0,
				.fabricclk_mhz = 1000.0,
				.dispclk_mhz = 1100.0,
				.dppclk_mhz = 1100.0,
				.phyclk_mhz = 810.0,
				.socclk_mhz = 1000.0,
				.dscclk_mhz = 367.0,
				.dram_speed_mts = 16000.0,
			},
			{
				.state = 4,
				.dcfclk_mhz = 1200.0,
				.fabricclk_mhz = 1200.0,
				.dispclk_mhz = 1284.0,
				.dppclk_mhz = 1284.0,
				.phyclk_mhz = 810.0,
				.socclk_mhz = 1200.0,
				.dscclk_mhz = 428.0,
				.dram_speed_mts = 16000.0,
			},
			/*Extra state, no dispclk ramping*/
			{
				.state = 5,
				.dcfclk_mhz = 1200.0,
				.fabricclk_mhz = 1200.0,
				.dispclk_mhz = 1284.0,
				.dppclk_mhz = 1284.0,
				.phyclk_mhz = 810.0,
				.socclk_mhz = 1200.0,
				.dscclk_mhz = 428.0,
				.dram_speed_mts = 16000.0,
			},
		},
	.num_states = 5,
	.sr_exit_time_us = 8.6,
	.sr_enter_plus_exit_time_us = 10.9,
	.urgent_latency_us = 4.0,
	.urgent_latency_pixel_data_only_us = 4.0,
	.urgent_latency_pixel_mixed_with_vm_data_us = 4.0,
	.urgent_latency_vm_data_only_us = 4.0,
	.urgent_out_of_order_return_per_channel_pixel_only_bytes = 4096,
	.urgent_out_of_order_return_per_channel_pixel_and_vm_bytes = 4096,
	.urgent_out_of_order_return_per_channel_vm_only_bytes = 4096,
	.pct_ideal_dram_sdp_bw_after_urgent_pixel_only = 40.0,
	.pct_ideal_dram_sdp_bw_after_urgent_pixel_and_vm = 40.0,
	.pct_ideal_dram_sdp_bw_after_urgent_vm_only = 40.0,
	.max_avg_sdp_bw_use_normal_percent = 40.0,
	.max_avg_dram_bw_use_normal_percent = 40.0,
	.writeback_latency_us = 12.0,
	.ideal_dram_bw_after_urgent_percent = 40.0,
	.max_request_size_bytes = 256,
	.dram_channel_width_bytes = 2,
	.fabric_datapath_to_dcn_data_return_bytes = 64,
	.dcn_downspread_percent = 0.5,
	.downspread_percent = 0.38,
	.dram_page_open_time_ns = 50.0,
	.dram_rw_turnaround_time_ns = 17.5,
	.dram_return_buffer_per_channel_bytes = 8192,
	.round_trip_ping_latency_dcfclk_cycles = 131,
	.urgent_out_of_order_return_per_channel_bytes = 256,
	.channel_interleave_bytes = 256,
	.num_banks = 8,
	.num_chans = 16,
	.vmm_page_size_bytes = 4096,
	.dram_clock_change_latency_us = 404.0,
	.dummy_pstate_latency_us = 5.0,
	.writeback_dram_clock_change_latency_us = 23.0,
	.return_bus_width_bytes = 64,
	.dispclk_dppclk_vco_speed_mhz = 3850,
	.xfc_bus_transport_time_us = 20,
	.xfc_xbuf_latency_tolerance_us = 4,
	.use_urgent_burst_bw = 0
};

struct _vcs_dpi_soc_bounding_box_st dcn2_0_nv14_soc = {
	.clock_limits = {
			{
				.state = 0,
				.dcfclk_mhz = 560.0,
				.fabricclk_mhz = 560.0,
				.dispclk_mhz = 513.0,
				.dppclk_mhz = 513.0,
				.phyclk_mhz = 540.0,
				.socclk_mhz = 560.0,
				.dscclk_mhz = 171.0,
				.dram_speed_mts = 8960.0,
			},
			{
				.state = 1,
				.dcfclk_mhz = 694.0,
				.fabricclk_mhz = 694.0,
				.dispclk_mhz = 642.0,
				.dppclk_mhz = 642.0,
				.phyclk_mhz = 600.0,
				.socclk_mhz = 694.0,
				.dscclk_mhz = 214.0,
				.dram_speed_mts = 11104.0,
			},
			{
				.state = 2,
				.dcfclk_mhz = 875.0,
				.fabricclk_mhz = 875.0,
				.dispclk_mhz = 734.0,
				.dppclk_mhz = 734.0,
				.phyclk_mhz = 810.0,
				.socclk_mhz = 875.0,
				.dscclk_mhz = 245.0,
				.dram_speed_mts = 14000.0,
			},
			{
				.state = 3,
				.dcfclk_mhz = 1000.0,
				.fabricclk_mhz = 1000.0,
				.dispclk_mhz = 1100.0,
				.dppclk_mhz = 1100.0,
				.phyclk_mhz = 810.0,
				.socclk_mhz = 1000.0,
				.dscclk_mhz = 367.0,
				.dram_speed_mts = 16000.0,
			},
			{
				.state = 4,
				.dcfclk_mhz = 1200.0,
				.fabricclk_mhz = 1200.0,
				.dispclk_mhz = 1284.0,
				.dppclk_mhz = 1284.0,
				.phyclk_mhz = 810.0,
				.socclk_mhz = 1200.0,
				.dscclk_mhz = 428.0,
				.dram_speed_mts = 16000.0,
			},
			/*Extra state, no dispclk ramping*/
			{
				.state = 5,
				.dcfclk_mhz = 1200.0,
				.fabricclk_mhz = 1200.0,
				.dispclk_mhz = 1284.0,
				.dppclk_mhz = 1284.0,
				.phyclk_mhz = 810.0,
				.socclk_mhz = 1200.0,
				.dscclk_mhz = 428.0,
				.dram_speed_mts = 16000.0,
			},
		},
	.num_states = 5,
	.sr_exit_time_us = 8.6,
	.sr_enter_plus_exit_time_us = 10.9,
	.urgent_latency_us = 4.0,
	.urgent_latency_pixel_data_only_us = 4.0,
	.urgent_latency_pixel_mixed_with_vm_data_us = 4.0,
	.urgent_latency_vm_data_only_us = 4.0,
	.urgent_out_of_order_return_per_channel_pixel_only_bytes = 4096,
	.urgent_out_of_order_return_per_channel_pixel_and_vm_bytes = 4096,
	.urgent_out_of_order_return_per_channel_vm_only_bytes = 4096,
	.pct_ideal_dram_sdp_bw_after_urgent_pixel_only = 40.0,
	.pct_ideal_dram_sdp_bw_after_urgent_pixel_and_vm = 40.0,
	.pct_ideal_dram_sdp_bw_after_urgent_vm_only = 40.0,
	.max_avg_sdp_bw_use_normal_percent = 40.0,
	.max_avg_dram_bw_use_normal_percent = 40.0,
	.writeback_latency_us = 12.0,
	.ideal_dram_bw_after_urgent_percent = 40.0,
	.max_request_size_bytes = 256,
	.dram_channel_width_bytes = 2,
	.fabric_datapath_to_dcn_data_return_bytes = 64,
	.dcn_downspread_percent = 0.5,
	.downspread_percent = 0.38,
	.dram_page_open_time_ns = 50.0,
	.dram_rw_turnaround_time_ns = 17.5,
	.dram_return_buffer_per_channel_bytes = 8192,
	.round_trip_ping_latency_dcfclk_cycles = 131,
	.urgent_out_of_order_return_per_channel_bytes = 256,
	.channel_interleave_bytes = 256,
	.num_banks = 8,
	.num_chans = 8,
	.vmm_page_size_bytes = 4096,
	.dram_clock_change_latency_us = 404.0,
	.dummy_pstate_latency_us = 5.0,
	.writeback_dram_clock_change_latency_us = 23.0,
	.return_bus_width_bytes = 64,
	.dispclk_dppclk_vco_speed_mhz = 3850,
	.xfc_bus_transport_time_us = 20,
	.xfc_xbuf_latency_tolerance_us = 4,
	.use_urgent_burst_bw = 0
};

struct _vcs_dpi_soc_bounding_box_st dcn2_0_nv12_soc = { 0 };

#ifndef mmDP0_DP_DPHY_INTERNAL_CTRL
	#define mmDP0_DP_DPHY_INTERNAL_CTRL		0x210f
	#define mmDP0_DP_DPHY_INTERNAL_CTRL_BASE_IDX	2
	#define mmDP1_DP_DPHY_INTERNAL_CTRL		0x220f
	#define mmDP1_DP_DPHY_INTERNAL_CTRL_BASE_IDX	2
	#define mmDP2_DP_DPHY_INTERNAL_CTRL		0x230f
	#define mmDP2_DP_DPHY_INTERNAL_CTRL_BASE_IDX	2
	#define mmDP3_DP_DPHY_INTERNAL_CTRL		0x240f
	#define mmDP3_DP_DPHY_INTERNAL_CTRL_BASE_IDX	2
	#define mmDP4_DP_DPHY_INTERNAL_CTRL		0x250f
	#define mmDP4_DP_DPHY_INTERNAL_CTRL_BASE_IDX	2
	#define mmDP5_DP_DPHY_INTERNAL_CTRL		0x260f
	#define mmDP5_DP_DPHY_INTERNAL_CTRL_BASE_IDX	2
	#define mmDP6_DP_DPHY_INTERNAL_CTRL		0x270f
	#define mmDP6_DP_DPHY_INTERNAL_CTRL_BASE_IDX	2
#endif


enum dcn20_clk_src_array_id {
	DCN20_CLK_SRC_PLL0,
	DCN20_CLK_SRC_PLL1,
	DCN20_CLK_SRC_PLL2,
	DCN20_CLK_SRC_PLL3,
	DCN20_CLK_SRC_PLL4,
	DCN20_CLK_SRC_PLL5,
	DCN20_CLK_SRC_TOTAL
};

/* begin *********************
 * macros to expend register list macro defined in HW object header file */

/* DCN */
/* TODO awful hack. fixup dcn20_dwb.h */
#undef BASE_INNER
#define BASE_INNER(seg) DCN_BASE__INST0_SEG ## seg

#define BASE(seg) BASE_INNER(seg)

#define SR(reg_name)\
		.reg_name = BASE(mm ## reg_name ## _BASE_IDX) +  \
					mm ## reg_name

#define SRI(reg_name, block, id)\
	.reg_name = BASE(mm ## block ## id ## _ ## reg_name ## _BASE_IDX) + \
					mm ## block ## id ## _ ## reg_name

#define SRIR(var_name, reg_name, block, id)\
	.var_name = BASE(mm ## block ## id ## _ ## reg_name ## _BASE_IDX) + \
					mm ## block ## id ## _ ## reg_name

#define SRII(reg_name, block, id)\
	.reg_name[id] = BASE(mm ## block ## id ## _ ## reg_name ## _BASE_IDX) + \
					mm ## block ## id ## _ ## reg_name

#define DCCG_SRII(reg_name, block, id)\
	.block ## _ ## reg_name[id] = BASE(mm ## block ## id ## _ ## reg_name ## _BASE_IDX) + \
					mm ## block ## id ## _ ## reg_name

/* NBIO */
#define NBIO_BASE_INNER(seg) \
	NBIO_BASE__INST0_SEG ## seg

#define NBIO_BASE(seg) \
	NBIO_BASE_INNER(seg)

#define NBIO_SR(reg_name)\
		.reg_name = NBIO_BASE(mm ## reg_name ## _BASE_IDX) + \
					mm ## reg_name

/* MMHUB */
#define MMHUB_BASE_INNER(seg) \
	MMHUB_BASE__INST0_SEG ## seg

#define MMHUB_BASE(seg) \
	MMHUB_BASE_INNER(seg)

#define MMHUB_SR(reg_name)\
		.reg_name = MMHUB_BASE(mmMM ## reg_name ## _BASE_IDX) + \
					mmMM ## reg_name

static const struct bios_registers bios_regs = {
		NBIO_SR(BIOS_SCRATCH_3),
		NBIO_SR(BIOS_SCRATCH_6)
};

#define clk_src_regs(index, pllid)\
[index] = {\
	CS_COMMON_REG_LIST_DCN2_0(index, pllid),\
}

static const struct dce110_clk_src_regs clk_src_regs[] = {
	clk_src_regs(0, A),
	clk_src_regs(1, B),
	clk_src_regs(2, C),
	clk_src_regs(3, D),
	clk_src_regs(4, E),
	clk_src_regs(5, F)
};

static const struct dce110_clk_src_shift cs_shift = {
		CS_COMMON_MASK_SH_LIST_DCN2_0(__SHIFT)
};

static const struct dce110_clk_src_mask cs_mask = {
		CS_COMMON_MASK_SH_LIST_DCN2_0(_MASK)
};

static const struct dce_dmcu_registers dmcu_regs = {
		DMCU_DCN10_REG_LIST()
};

static const struct dce_dmcu_shift dmcu_shift = {
		DMCU_MASK_SH_LIST_DCN10(__SHIFT)
};

static const struct dce_dmcu_mask dmcu_mask = {
		DMCU_MASK_SH_LIST_DCN10(_MASK)
};

static const struct dce_abm_registers abm_regs = {
		ABM_DCN20_REG_LIST()
};

static const struct dce_abm_shift abm_shift = {
		ABM_MASK_SH_LIST_DCN20(__SHIFT)
};

static const struct dce_abm_mask abm_mask = {
		ABM_MASK_SH_LIST_DCN20(_MASK)
};

#define audio_regs(id)\
[id] = {\
		AUD_COMMON_REG_LIST(id)\
}

static const struct dce_audio_registers audio_regs[] = {
	audio_regs(0),
	audio_regs(1),
	audio_regs(2),
	audio_regs(3),
	audio_regs(4),
	audio_regs(5),
	audio_regs(6),
};

#define DCE120_AUD_COMMON_MASK_SH_LIST(mask_sh)\
		SF(AZF0ENDPOINT0_AZALIA_F0_CODEC_ENDPOINT_INDEX, AZALIA_ENDPOINT_REG_INDEX, mask_sh),\
		SF(AZF0ENDPOINT0_AZALIA_F0_CODEC_ENDPOINT_DATA, AZALIA_ENDPOINT_REG_DATA, mask_sh),\
		AUD_COMMON_MASK_SH_LIST_BASE(mask_sh)

static const struct dce_audio_shift audio_shift = {
		DCE120_AUD_COMMON_MASK_SH_LIST(__SHIFT)
};

static const struct dce_audio_mask audio_mask = {
		DCE120_AUD_COMMON_MASK_SH_LIST(_MASK)
};

#define stream_enc_regs(id)\
[id] = {\
	SE_DCN2_REG_LIST(id)\
}

static const struct dcn10_stream_enc_registers stream_enc_regs[] = {
	stream_enc_regs(0),
	stream_enc_regs(1),
	stream_enc_regs(2),
	stream_enc_regs(3),
	stream_enc_regs(4),
	stream_enc_regs(5),
};

static const struct dcn10_stream_encoder_shift se_shift = {
		SE_COMMON_MASK_SH_LIST_DCN20(__SHIFT)
};

static const struct dcn10_stream_encoder_mask se_mask = {
		SE_COMMON_MASK_SH_LIST_DCN20(_MASK)
};


#define aux_regs(id)\
[id] = {\
	DCN2_AUX_REG_LIST(id)\
}

static const struct dcn10_link_enc_aux_registers link_enc_aux_regs[] = {
		aux_regs(0),
		aux_regs(1),
		aux_regs(2),
		aux_regs(3),
		aux_regs(4),
		aux_regs(5)
};

#define hpd_regs(id)\
[id] = {\
	HPD_REG_LIST(id)\
}

static const struct dcn10_link_enc_hpd_registers link_enc_hpd_regs[] = {
		hpd_regs(0),
		hpd_regs(1),
		hpd_regs(2),
		hpd_regs(3),
		hpd_regs(4),
		hpd_regs(5)
};

#define link_regs(id, phyid)\
[id] = {\
	LE_DCN10_REG_LIST(id), \
	UNIPHY_DCN2_REG_LIST(phyid), \
	DPCS_DCN2_REG_LIST(id), \
	SRI(DP_DPHY_INTERNAL_CTRL, DP, id) \
}

static const struct dcn10_link_enc_registers link_enc_regs[] = {
	link_regs(0, A),
	link_regs(1, B),
	link_regs(2, C),
	link_regs(3, D),
	link_regs(4, E),
	link_regs(5, F)
};

static const struct dcn10_link_enc_shift le_shift = {
	LINK_ENCODER_MASK_SH_LIST_DCN20(__SHIFT),\
	DPCS_DCN2_MASK_SH_LIST(__SHIFT)
};

static const struct dcn10_link_enc_mask le_mask = {
	LINK_ENCODER_MASK_SH_LIST_DCN20(_MASK),\
	DPCS_DCN2_MASK_SH_LIST(_MASK)
};

#define ipp_regs(id)\
[id] = {\
	IPP_REG_LIST_DCN20(id),\
}

static const struct dcn10_ipp_registers ipp_regs[] = {
	ipp_regs(0),
	ipp_regs(1),
	ipp_regs(2),
	ipp_regs(3),
	ipp_regs(4),
	ipp_regs(5),
};

static const struct dcn10_ipp_shift ipp_shift = {
		IPP_MASK_SH_LIST_DCN20(__SHIFT)
};

static const struct dcn10_ipp_mask ipp_mask = {
		IPP_MASK_SH_LIST_DCN20(_MASK),
};

#define opp_regs(id)\
[id] = {\
	OPP_REG_LIST_DCN20(id),\
}

static const struct dcn20_opp_registers opp_regs[] = {
	opp_regs(0),
	opp_regs(1),
	opp_regs(2),
	opp_regs(3),
	opp_regs(4),
	opp_regs(5),
};

static const struct dcn20_opp_shift opp_shift = {
		OPP_MASK_SH_LIST_DCN20(__SHIFT)
};

static const struct dcn20_opp_mask opp_mask = {
		OPP_MASK_SH_LIST_DCN20(_MASK)
};

#define aux_engine_regs(id)\
[id] = {\
	AUX_COMMON_REG_LIST0(id), \
	.AUXN_IMPCAL = 0, \
	.AUXP_IMPCAL = 0, \
	.AUX_RESET_MASK = DP_AUX0_AUX_CONTROL__AUX_RESET_MASK, \
}

static const struct dce110_aux_registers aux_engine_regs[] = {
		aux_engine_regs(0),
		aux_engine_regs(1),
		aux_engine_regs(2),
		aux_engine_regs(3),
		aux_engine_regs(4),
		aux_engine_regs(5)
};

#define tf_regs(id)\
[id] = {\
	TF_REG_LIST_DCN20(id),\
	TF_REG_LIST_DCN20_COMMON_APPEND(id),\
}

static const struct dcn2_dpp_registers tf_regs[] = {
	tf_regs(0),
	tf_regs(1),
	tf_regs(2),
	tf_regs(3),
	tf_regs(4),
	tf_regs(5),
};

static const struct dcn2_dpp_shift tf_shift = {
		TF_REG_LIST_SH_MASK_DCN20(__SHIFT),
		TF_DEBUG_REG_LIST_SH_DCN20
};

static const struct dcn2_dpp_mask tf_mask = {
		TF_REG_LIST_SH_MASK_DCN20(_MASK),
		TF_DEBUG_REG_LIST_MASK_DCN20
};

#define dwbc_regs_dcn2(id)\
[id] = {\
	DWBC_COMMON_REG_LIST_DCN2_0(id),\
		}

static const struct dcn20_dwbc_registers dwbc20_regs[] = {
	dwbc_regs_dcn2(0),
};

static const struct dcn20_dwbc_shift dwbc20_shift = {
	DWBC_COMMON_MASK_SH_LIST_DCN2_0(__SHIFT)
};

static const struct dcn20_dwbc_mask dwbc20_mask = {
	DWBC_COMMON_MASK_SH_LIST_DCN2_0(_MASK)
};

#define mcif_wb_regs_dcn2(id)\
[id] = {\
	MCIF_WB_COMMON_REG_LIST_DCN2_0(id),\
		}

static const struct dcn20_mmhubbub_registers mcif_wb20_regs[] = {
	mcif_wb_regs_dcn2(0),
};

static const struct dcn20_mmhubbub_shift mcif_wb20_shift = {
	MCIF_WB_COMMON_MASK_SH_LIST_DCN2_0(__SHIFT)
};

static const struct dcn20_mmhubbub_mask mcif_wb20_mask = {
	MCIF_WB_COMMON_MASK_SH_LIST_DCN2_0(_MASK)
};

static const struct dcn20_mpc_registers mpc_regs = {
		MPC_REG_LIST_DCN2_0(0),
		MPC_REG_LIST_DCN2_0(1),
		MPC_REG_LIST_DCN2_0(2),
		MPC_REG_LIST_DCN2_0(3),
		MPC_REG_LIST_DCN2_0(4),
		MPC_REG_LIST_DCN2_0(5),
		MPC_OUT_MUX_REG_LIST_DCN2_0(0),
		MPC_OUT_MUX_REG_LIST_DCN2_0(1),
		MPC_OUT_MUX_REG_LIST_DCN2_0(2),
		MPC_OUT_MUX_REG_LIST_DCN2_0(3),
		MPC_OUT_MUX_REG_LIST_DCN2_0(4),
		MPC_OUT_MUX_REG_LIST_DCN2_0(5),
		MPC_DBG_REG_LIST_DCN2_0()
};

static const struct dcn20_mpc_shift mpc_shift = {
	MPC_COMMON_MASK_SH_LIST_DCN2_0(__SHIFT),
	MPC_DEBUG_REG_LIST_SH_DCN20
};

static const struct dcn20_mpc_mask mpc_mask = {
	MPC_COMMON_MASK_SH_LIST_DCN2_0(_MASK),
	MPC_DEBUG_REG_LIST_MASK_DCN20
};

#define tg_regs(id)\
[id] = {TG_COMMON_REG_LIST_DCN2_0(id)}


static const struct dcn_optc_registers tg_regs[] = {
	tg_regs(0),
	tg_regs(1),
	tg_regs(2),
	tg_regs(3),
	tg_regs(4),
	tg_regs(5)
};

static const struct dcn_optc_shift tg_shift = {
	TG_COMMON_MASK_SH_LIST_DCN2_0(__SHIFT)
};

static const struct dcn_optc_mask tg_mask = {
	TG_COMMON_MASK_SH_LIST_DCN2_0(_MASK)
};

#define hubp_regs(id)\
[id] = {\
	HUBP_REG_LIST_DCN20(id)\
}

static const struct dcn_hubp2_registers hubp_regs[] = {
		hubp_regs(0),
		hubp_regs(1),
		hubp_regs(2),
		hubp_regs(3),
		hubp_regs(4),
		hubp_regs(5)
};

static const struct dcn_hubp2_shift hubp_shift = {
		HUBP_MASK_SH_LIST_DCN20(__SHIFT)
};

static const struct dcn_hubp2_mask hubp_mask = {
		HUBP_MASK_SH_LIST_DCN20(_MASK)
};

static const struct dcn_hubbub_registers hubbub_reg = {
		HUBBUB_REG_LIST_DCN20(0)
};

static const struct dcn_hubbub_shift hubbub_shift = {
		HUBBUB_MASK_SH_LIST_DCN20(__SHIFT)
};

static const struct dcn_hubbub_mask hubbub_mask = {
		HUBBUB_MASK_SH_LIST_DCN20(_MASK)
};

#define vmid_regs(id)\
[id] = {\
		DCN20_VMID_REG_LIST(id)\
}

static const struct dcn_vmid_registers vmid_regs[] = {
	vmid_regs(0),
	vmid_regs(1),
	vmid_regs(2),
	vmid_regs(3),
	vmid_regs(4),
	vmid_regs(5),
	vmid_regs(6),
	vmid_regs(7),
	vmid_regs(8),
	vmid_regs(9),
	vmid_regs(10),
	vmid_regs(11),
	vmid_regs(12),
	vmid_regs(13),
	vmid_regs(14),
	vmid_regs(15)
};

static const struct dcn20_vmid_shift vmid_shifts = {
		DCN20_VMID_MASK_SH_LIST(__SHIFT)
};

static const struct dcn20_vmid_mask vmid_masks = {
		DCN20_VMID_MASK_SH_LIST(_MASK)
};

static const struct dce110_aux_registers_shift aux_shift = {
		DCN_AUX_MASK_SH_LIST(__SHIFT)
};

static const struct dce110_aux_registers_mask aux_mask = {
		DCN_AUX_MASK_SH_LIST(_MASK)
};

static int map_transmitter_id_to_phy_instance(
	enum transmitter transmitter)
{
	switch (transmitter) {
	case TRANSMITTER_UNIPHY_A:
		return 0;
	break;
	case TRANSMITTER_UNIPHY_B:
		return 1;
	break;
	case TRANSMITTER_UNIPHY_C:
		return 2;
	break;
	case TRANSMITTER_UNIPHY_D:
		return 3;
	break;
	case TRANSMITTER_UNIPHY_E:
		return 4;
	break;
	case TRANSMITTER_UNIPHY_F:
		return 5;
	break;
	default:
		ASSERT(0);
		return 0;
	}
}

#define dsc_regsDCN20(id)\
[id] = {\
	DSC_REG_LIST_DCN20(id)\
}

static const struct dcn20_dsc_registers dsc_regs[] = {
	dsc_regsDCN20(0),
	dsc_regsDCN20(1),
	dsc_regsDCN20(2),
	dsc_regsDCN20(3),
	dsc_regsDCN20(4),
	dsc_regsDCN20(5)
};

static const struct dcn20_dsc_shift dsc_shift = {
	DSC_REG_LIST_SH_MASK_DCN20(__SHIFT)
};

static const struct dcn20_dsc_mask dsc_mask = {
	DSC_REG_LIST_SH_MASK_DCN20(_MASK)
};

static const struct dccg_registers dccg_regs = {
		DCCG_REG_LIST_DCN2()
};

static const struct dccg_shift dccg_shift = {
		DCCG_MASK_SH_LIST_DCN2(__SHIFT)
};

static const struct dccg_mask dccg_mask = {
		DCCG_MASK_SH_LIST_DCN2(_MASK)
};

static const struct resource_caps res_cap_nv10 = {
		.num_timing_generator = 6,
		.num_opp = 6,
		.num_video_plane = 6,
		.num_audio = 7,
		.num_stream_encoder = 6,
		.num_pll = 6,
		.num_dwb = 1,
		.num_ddc = 6,
		.num_vmid = 16,
		.num_dsc = 6,
};

static const struct dc_plane_cap plane_cap = {
	.type = DC_PLANE_TYPE_DCN_UNIVERSAL,
	.blends_with_above = true,
	.blends_with_below = true,
	.per_pixel_alpha = true,

	.pixel_format_support = {
			.argb8888 = true,
			.nv12 = true,
			.fp16 = true
	},

	.max_upscale_factor = {
			.argb8888 = 16000,
			.nv12 = 16000,
			.fp16 = 1
	},

	.max_downscale_factor = {
			.argb8888 = 250,
			.nv12 = 250,
			.fp16 = 1
	}
};
static const struct resource_caps res_cap_nv14 = {
		.num_timing_generator = 5,
		.num_opp = 5,
		.num_video_plane = 5,
		.num_audio = 6,
		.num_stream_encoder = 5,
		.num_pll = 5,
		.num_dwb = 1,
		.num_ddc = 5,
		.num_vmid = 16,
		.num_dsc = 5,
};

static const struct dc_debug_options debug_defaults_drv = {
		.disable_dmcu = false,
		.force_abm_enable = false,
		.timing_trace = false,
		.clock_trace = true,
		.disable_pplib_clock_request = true,
		.pipe_split_policy = MPC_SPLIT_DYNAMIC,
		.force_single_disp_pipe_split = false,
		.disable_dcc = DCC_ENABLE,
		.vsr_support = true,
		.performance_trace = false,
		.max_downscale_src_width = 5120,/*upto 5K*/
		.disable_pplib_wm_range = false,
		.scl_reset_length10 = true,
		.sanity_checks = false,
		.disable_tri_buf = true,
		.underflow_assert_delay_us = 0xFFFFFFFF,
};

static const struct dc_debug_options debug_defaults_diags = {
		.disable_dmcu = false,
		.force_abm_enable = false,
		.timing_trace = true,
		.clock_trace = true,
		.disable_dpp_power_gate = true,
		.disable_hubp_power_gate = true,
		.disable_clock_gate = true,
		.disable_pplib_clock_request = true,
		.disable_pplib_wm_range = true,
		.disable_stutter = true,
		.scl_reset_length10 = true,
		.underflow_assert_delay_us = 0xFFFFFFFF,
};

void dcn20_dpp_destroy(struct dpp **dpp)
{
	kfree(TO_DCN20_DPP(*dpp));
	*dpp = NULL;
}

struct dpp *dcn20_dpp_create(
	struct dc_context *ctx,
	uint32_t inst)
{
	struct dcn20_dpp *dpp =
		kzalloc(sizeof(struct dcn20_dpp), GFP_KERNEL);

	if (!dpp)
		return NULL;

	if (dpp2_construct(dpp, ctx, inst,
			&tf_regs[inst], &tf_shift, &tf_mask))
		return &dpp->base;

	BREAK_TO_DEBUGGER();
	kfree(dpp);
	return NULL;
}

struct input_pixel_processor *dcn20_ipp_create(
	struct dc_context *ctx, uint32_t inst)
{
	struct dcn10_ipp *ipp =
		kzalloc(sizeof(struct dcn10_ipp), GFP_KERNEL);

	if (!ipp) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	dcn20_ipp_construct(ipp, ctx, inst,
			&ipp_regs[inst], &ipp_shift, &ipp_mask);
	return &ipp->base;
}


struct output_pixel_processor *dcn20_opp_create(
	struct dc_context *ctx, uint32_t inst)
{
	struct dcn20_opp *opp =
		kzalloc(sizeof(struct dcn20_opp), GFP_KERNEL);

	if (!opp) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	dcn20_opp_construct(opp, ctx, inst,
			&opp_regs[inst], &opp_shift, &opp_mask);
	return &opp->base;
}

struct dce_aux *dcn20_aux_engine_create(
	struct dc_context *ctx,
	uint32_t inst)
{
	struct aux_engine_dce110 *aux_engine =
		kzalloc(sizeof(struct aux_engine_dce110), GFP_KERNEL);

	if (!aux_engine)
		return NULL;

	dce110_aux_engine_construct(aux_engine, ctx, inst,
				    SW_AUX_TIMEOUT_PERIOD_MULTIPLIER * AUX_TIMEOUT_PERIOD,
				    &aux_engine_regs[inst],
					&aux_mask,
					&aux_shift,
					ctx->dc->caps.extended_aux_timeout_support);

	return &aux_engine->base;
}
#define i2c_inst_regs(id) { I2C_HW_ENGINE_COMMON_REG_LIST(id) }

static const struct dce_i2c_registers i2c_hw_regs[] = {
		i2c_inst_regs(1),
		i2c_inst_regs(2),
		i2c_inst_regs(3),
		i2c_inst_regs(4),
		i2c_inst_regs(5),
		i2c_inst_regs(6),
};

static const struct dce_i2c_shift i2c_shifts = {
		I2C_COMMON_MASK_SH_LIST_DCN2(__SHIFT)
};

static const struct dce_i2c_mask i2c_masks = {
		I2C_COMMON_MASK_SH_LIST_DCN2(_MASK)
};

struct dce_i2c_hw *dcn20_i2c_hw_create(
	struct dc_context *ctx,
	uint32_t inst)
{
	struct dce_i2c_hw *dce_i2c_hw =
		kzalloc(sizeof(struct dce_i2c_hw), GFP_KERNEL);

	if (!dce_i2c_hw)
		return NULL;

	dcn2_i2c_hw_construct(dce_i2c_hw, ctx, inst,
				    &i2c_hw_regs[inst], &i2c_shifts, &i2c_masks);

	return dce_i2c_hw;
}
struct mpc *dcn20_mpc_create(struct dc_context *ctx)
{
	struct dcn20_mpc *mpc20 = kzalloc(sizeof(struct dcn20_mpc),
					  GFP_KERNEL);

	if (!mpc20)
		return NULL;

	dcn20_mpc_construct(mpc20, ctx,
			&mpc_regs,
			&mpc_shift,
			&mpc_mask,
			6);

	return &mpc20->base;
}

struct hubbub *dcn20_hubbub_create(struct dc_context *ctx)
{
	int i;
	struct dcn20_hubbub *hubbub = kzalloc(sizeof(struct dcn20_hubbub),
					  GFP_KERNEL);

	if (!hubbub)
		return NULL;

	hubbub2_construct(hubbub, ctx,
			&hubbub_reg,
			&hubbub_shift,
			&hubbub_mask);

	for (i = 0; i < res_cap_nv10.num_vmid; i++) {
		struct dcn20_vmid *vmid = &hubbub->vmid[i];

		vmid->ctx = ctx;

		vmid->regs = &vmid_regs[i];
		vmid->shifts = &vmid_shifts;
		vmid->masks = &vmid_masks;
	}

	return &hubbub->base;
}

struct timing_generator *dcn20_timing_generator_create(
		struct dc_context *ctx,
		uint32_t instance)
{
	struct optc *tgn10 =
		kzalloc(sizeof(struct optc), GFP_KERNEL);

	if (!tgn10)
		return NULL;

	tgn10->base.inst = instance;
	tgn10->base.ctx = ctx;

	tgn10->tg_regs = &tg_regs[instance];
	tgn10->tg_shift = &tg_shift;
	tgn10->tg_mask = &tg_mask;

	dcn20_timing_generator_init(tgn10);

	return &tgn10->base;
}

static const struct encoder_feature_support link_enc_feature = {
		.max_hdmi_deep_color = COLOR_DEPTH_121212,
		.max_hdmi_pixel_clock = 600000,
		.hdmi_ycbcr420_supported = true,
		.dp_ycbcr420_supported = true,
		.fec_supported = true,
		.flags.bits.IS_HBR2_CAPABLE = true,
		.flags.bits.IS_HBR3_CAPABLE = true,
		.flags.bits.IS_TPS3_CAPABLE = true,
		.flags.bits.IS_TPS4_CAPABLE = true
};

struct link_encoder *dcn20_link_encoder_create(
	const struct encoder_init_data *enc_init_data)
{
	struct dcn20_link_encoder *enc20 =
		kzalloc(sizeof(struct dcn20_link_encoder), GFP_KERNEL);
	int link_regs_id;

	if (!enc20)
		return NULL;

	link_regs_id =
		map_transmitter_id_to_phy_instance(enc_init_data->transmitter);

	dcn20_link_encoder_construct(enc20,
				      enc_init_data,
				      &link_enc_feature,
				      &link_enc_regs[link_regs_id],
				      &link_enc_aux_regs[enc_init_data->channel - 1],
				      &link_enc_hpd_regs[enc_init_data->hpd_source],
				      &le_shift,
				      &le_mask);

	return &enc20->enc10.base;
}

struct clock_source *dcn20_clock_source_create(
	struct dc_context *ctx,
	struct dc_bios *bios,
	enum clock_source_id id,
	const struct dce110_clk_src_regs *regs,
	bool dp_clk_src)
{
	struct dce110_clk_src *clk_src =
		kzalloc(sizeof(struct dce110_clk_src), GFP_KERNEL);

	if (!clk_src)
		return NULL;

	if (dcn20_clk_src_construct(clk_src, ctx, bios, id,
			regs, &cs_shift, &cs_mask)) {
		clk_src->base.dp_clk_src = dp_clk_src;
		return &clk_src->base;
	}

	kfree(clk_src);
	BREAK_TO_DEBUGGER();
	return NULL;
}

static void read_dce_straps(
	struct dc_context *ctx,
	struct resource_straps *straps)
{
	generic_reg_get(ctx, mmDC_PINSTRAPS + BASE(mmDC_PINSTRAPS_BASE_IDX),
		FN(DC_PINSTRAPS, DC_PINSTRAPS_AUDIO), &straps->dc_pinstraps_audio);
}

static struct audio *dcn20_create_audio(
		struct dc_context *ctx, unsigned int inst)
{
	return dce_audio_create(ctx, inst,
			&audio_regs[inst], &audio_shift, &audio_mask);
}

struct stream_encoder *dcn20_stream_encoder_create(
	enum engine_id eng_id,
	struct dc_context *ctx)
{
	struct dcn10_stream_encoder *enc1 =
		kzalloc(sizeof(struct dcn10_stream_encoder), GFP_KERNEL);

	if (!enc1)
		return NULL;

	if (ASICREV_IS_NAVI14_M(ctx->asic_id.hw_internal_rev)) {
		if (eng_id >= ENGINE_ID_DIGD)
			eng_id++;
	}

	dcn20_stream_encoder_construct(enc1, ctx, ctx->dc_bios, eng_id,
					&stream_enc_regs[eng_id],
					&se_shift, &se_mask);

	return &enc1->base;
}

static const struct dce_hwseq_registers hwseq_reg = {
		HWSEQ_DCN2_REG_LIST()
};

static const struct dce_hwseq_shift hwseq_shift = {
		HWSEQ_DCN2_MASK_SH_LIST(__SHIFT)
};

static const struct dce_hwseq_mask hwseq_mask = {
		HWSEQ_DCN2_MASK_SH_LIST(_MASK)
};

struct dce_hwseq *dcn20_hwseq_create(
	struct dc_context *ctx)
{
	struct dce_hwseq *hws = kzalloc(sizeof(struct dce_hwseq), GFP_KERNEL);

	if (hws) {
		hws->ctx = ctx;
		hws->regs = &hwseq_reg;
		hws->shifts = &hwseq_shift;
		hws->masks = &hwseq_mask;
	}
	return hws;
}

static const struct resource_create_funcs res_create_funcs = {
	.read_dce_straps = read_dce_straps,
	.create_audio = dcn20_create_audio,
	.create_stream_encoder = dcn20_stream_encoder_create,
	.create_hwseq = dcn20_hwseq_create,
};

static const struct resource_create_funcs res_create_maximus_funcs = {
	.read_dce_straps = NULL,
	.create_audio = NULL,
	.create_stream_encoder = NULL,
	.create_hwseq = dcn20_hwseq_create,
};

static void dcn20_pp_smu_destroy(struct pp_smu_funcs **pp_smu);

void dcn20_clock_source_destroy(struct clock_source **clk_src)
{
	kfree(TO_DCE110_CLK_SRC(*clk_src));
	*clk_src = NULL;
}


struct display_stream_compressor *dcn20_dsc_create(
	struct dc_context *ctx, uint32_t inst)
{
	struct dcn20_dsc *dsc =
		kzalloc(sizeof(struct dcn20_dsc), GFP_KERNEL);

	if (!dsc) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	dsc2_construct(dsc, ctx, inst, &dsc_regs[inst], &dsc_shift, &dsc_mask);
	return &dsc->base;
}

void dcn20_dsc_destroy(struct display_stream_compressor **dsc)
{
	kfree(container_of(*dsc, struct dcn20_dsc, base));
	*dsc = NULL;
}


static void dcn20_resource_destruct(struct dcn20_resource_pool *pool)
{
	unsigned int i;

	for (i = 0; i < pool->base.stream_enc_count; i++) {
		if (pool->base.stream_enc[i] != NULL) {
			kfree(DCN10STRENC_FROM_STRENC(pool->base.stream_enc[i]));
			pool->base.stream_enc[i] = NULL;
		}
	}

	for (i = 0; i < pool->base.res_cap->num_dsc; i++) {
		if (pool->base.dscs[i] != NULL)
			dcn20_dsc_destroy(&pool->base.dscs[i]);
	}

	if (pool->base.mpc != NULL) {
		kfree(TO_DCN20_MPC(pool->base.mpc));
		pool->base.mpc = NULL;
	}
	if (pool->base.hubbub != NULL) {
		kfree(pool->base.hubbub);
		pool->base.hubbub = NULL;
	}
	for (i = 0; i < pool->base.pipe_count; i++) {
		if (pool->base.dpps[i] != NULL)
			dcn20_dpp_destroy(&pool->base.dpps[i]);

		if (pool->base.ipps[i] != NULL)
			pool->base.ipps[i]->funcs->ipp_destroy(&pool->base.ipps[i]);

		if (pool->base.hubps[i] != NULL) {
			kfree(TO_DCN20_HUBP(pool->base.hubps[i]));
			pool->base.hubps[i] = NULL;
		}

		if (pool->base.irqs != NULL) {
			dal_irq_service_destroy(&pool->base.irqs);
		}
	}

	for (i = 0; i < pool->base.res_cap->num_ddc; i++) {
		if (pool->base.engines[i] != NULL)
			dce110_engine_destroy(&pool->base.engines[i]);
		if (pool->base.hw_i2cs[i] != NULL) {
			kfree(pool->base.hw_i2cs[i]);
			pool->base.hw_i2cs[i] = NULL;
		}
		if (pool->base.sw_i2cs[i] != NULL) {
			kfree(pool->base.sw_i2cs[i]);
			pool->base.sw_i2cs[i] = NULL;
		}
	}

	for (i = 0; i < pool->base.res_cap->num_opp; i++) {
		if (pool->base.opps[i] != NULL)
			pool->base.opps[i]->funcs->opp_destroy(&pool->base.opps[i]);
	}

	for (i = 0; i < pool->base.res_cap->num_timing_generator; i++) {
		if (pool->base.timing_generators[i] != NULL)	{
			kfree(DCN10TG_FROM_TG(pool->base.timing_generators[i]));
			pool->base.timing_generators[i] = NULL;
		}
	}

	for (i = 0; i < pool->base.res_cap->num_dwb; i++) {
		if (pool->base.dwbc[i] != NULL) {
			kfree(TO_DCN20_DWBC(pool->base.dwbc[i]));
			pool->base.dwbc[i] = NULL;
		}
		if (pool->base.mcif_wb[i] != NULL) {
			kfree(TO_DCN20_MMHUBBUB(pool->base.mcif_wb[i]));
			pool->base.mcif_wb[i] = NULL;
		}
	}

	for (i = 0; i < pool->base.audio_count; i++) {
		if (pool->base.audios[i])
			dce_aud_destroy(&pool->base.audios[i]);
	}

	for (i = 0; i < pool->base.clk_src_count; i++) {
		if (pool->base.clock_sources[i] != NULL) {
			dcn20_clock_source_destroy(&pool->base.clock_sources[i]);
			pool->base.clock_sources[i] = NULL;
		}
	}

	if (pool->base.dp_clock_source != NULL) {
		dcn20_clock_source_destroy(&pool->base.dp_clock_source);
		pool->base.dp_clock_source = NULL;
	}


	if (pool->base.abm != NULL)
		dce_abm_destroy(&pool->base.abm);

	if (pool->base.dmcu != NULL)
		dce_dmcu_destroy(&pool->base.dmcu);

	if (pool->base.dccg != NULL)
		dcn_dccg_destroy(&pool->base.dccg);

	if (pool->base.pp_smu != NULL)
		dcn20_pp_smu_destroy(&pool->base.pp_smu);

	if (pool->base.oem_device != NULL)
		dal_ddc_service_destroy(&pool->base.oem_device);
}

struct hubp *dcn20_hubp_create(
	struct dc_context *ctx,
	uint32_t inst)
{
	struct dcn20_hubp *hubp2 =
		kzalloc(sizeof(struct dcn20_hubp), GFP_KERNEL);

	if (!hubp2)
		return NULL;

	if (hubp2_construct(hubp2, ctx, inst,
			&hubp_regs[inst], &hubp_shift, &hubp_mask))
		return &hubp2->base;

	BREAK_TO_DEBUGGER();
	kfree(hubp2);
	return NULL;
}

static void get_pixel_clock_parameters(
	struct pipe_ctx *pipe_ctx,
	struct pixel_clk_params *pixel_clk_params)
{
	const struct dc_stream_state *stream = pipe_ctx->stream;
	struct pipe_ctx *odm_pipe;
	int opp_cnt = 1;

	for (odm_pipe = pipe_ctx->next_odm_pipe; odm_pipe; odm_pipe = odm_pipe->next_odm_pipe)
		opp_cnt++;

	pixel_clk_params->requested_pix_clk_100hz = stream->timing.pix_clk_100hz;
	pixel_clk_params->encoder_object_id = stream->link->link_enc->id;
	pixel_clk_params->signal_type = pipe_ctx->stream->signal;
	pixel_clk_params->controller_id = pipe_ctx->stream_res.tg->inst + 1;
	/* TODO: un-hardcode*/
	pixel_clk_params->requested_sym_clk = LINK_RATE_LOW *
		LINK_RATE_REF_FREQ_IN_KHZ;
	pixel_clk_params->flags.ENABLE_SS = 0;
	pixel_clk_params->color_depth =
		stream->timing.display_color_depth;
	pixel_clk_params->flags.DISPLAY_BLANKED = 1;
	pixel_clk_params->pixel_encoding = stream->timing.pixel_encoding;

	if (stream->timing.pixel_encoding == PIXEL_ENCODING_YCBCR422)
		pixel_clk_params->color_depth = COLOR_DEPTH_888;

	if (opp_cnt == 4)
		pixel_clk_params->requested_pix_clk_100hz /= 4;
	else if (optc2_is_two_pixels_per_containter(&stream->timing) || opp_cnt == 2)
		pixel_clk_params->requested_pix_clk_100hz /= 2;

	if (stream->timing.timing_3d_format == TIMING_3D_FORMAT_HW_FRAME_PACKING)
		pixel_clk_params->requested_pix_clk_100hz *= 2;

}

static void build_clamping_params(struct dc_stream_state *stream)
{
	stream->clamping.clamping_level = CLAMPING_FULL_RANGE;
	stream->clamping.c_depth = stream->timing.display_color_depth;
	stream->clamping.pixel_encoding = stream->timing.pixel_encoding;
}

static enum dc_status build_pipe_hw_param(struct pipe_ctx *pipe_ctx)
{

	get_pixel_clock_parameters(pipe_ctx, &pipe_ctx->stream_res.pix_clk_params);

	pipe_ctx->clock_source->funcs->get_pix_clk_dividers(
		pipe_ctx->clock_source,
		&pipe_ctx->stream_res.pix_clk_params,
		&pipe_ctx->pll_settings);

	pipe_ctx->stream->clamping.pixel_encoding = pipe_ctx->stream->timing.pixel_encoding;

	resource_build_bit_depth_reduction_params(pipe_ctx->stream,
					&pipe_ctx->stream->bit_depth_params);
	build_clamping_params(pipe_ctx->stream);

	return DC_OK;
}

enum dc_status dcn20_build_mapped_resource(const struct dc *dc, struct dc_state *context, struct dc_stream_state *stream)
{
	enum dc_status status = DC_OK;
	struct pipe_ctx *pipe_ctx = resource_get_head_pipe_for_stream(&context->res_ctx, stream);

	/*TODO Seems unneeded anymore */
	/*	if (old_context && resource_is_stream_unchanged(old_context, stream)) {
			if (stream != NULL && old_context->streams[i] != NULL) {
				 todo: shouldn't have to copy missing parameter here
				resource_build_bit_depth_reduction_params(stream,
						&stream->bit_depth_params);
				stream->clamping.pixel_encoding =
						stream->timing.pixel_encoding;

				resource_build_bit_depth_reduction_params(stream,
								&stream->bit_depth_params);
				build_clamping_params(stream);

				continue;
			}
		}
	*/

	if (!pipe_ctx)
		return DC_ERROR_UNEXPECTED;


	status = build_pipe_hw_param(pipe_ctx);

	return status;
}


static void acquire_dsc(struct resource_context *res_ctx,
			const struct resource_pool *pool,
			struct display_stream_compressor **dsc,
			int pipe_idx)
{
	int i;

	ASSERT(*dsc == NULL);
	*dsc = NULL;

	if (pool->res_cap->num_dsc == pool->res_cap->num_opp) {
		*dsc = pool->dscs[pipe_idx];
		res_ctx->is_dsc_acquired[pipe_idx] = true;
		return;
	}

	/* Find first free DSC */
	for (i = 0; i < pool->res_cap->num_dsc; i++)
		if (!res_ctx->is_dsc_acquired[i]) {
			*dsc = pool->dscs[i];
			res_ctx->is_dsc_acquired[i] = true;
			break;
		}
}

void dcn20_release_dsc(struct resource_context *res_ctx,
			const struct resource_pool *pool,
			struct display_stream_compressor **dsc)
{
	int i;

	for (i = 0; i < pool->res_cap->num_dsc; i++)
		if (pool->dscs[i] == *dsc) {
			res_ctx->is_dsc_acquired[i] = false;
			*dsc = NULL;
			break;
		}
}



enum dc_status dcn20_add_dsc_to_stream_resource(struct dc *dc,
		struct dc_state *dc_ctx,
		struct dc_stream_state *dc_stream)
{
	enum dc_status result = DC_OK;
	int i;
	const struct resource_pool *pool = dc->res_pool;

	/* Get a DSC if required and available */
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe_ctx = &dc_ctx->res_ctx.pipe_ctx[i];

		if (pipe_ctx->stream != dc_stream)
			continue;

		if (pipe_ctx->stream_res.dsc)
			continue;

		acquire_dsc(&dc_ctx->res_ctx, pool, &pipe_ctx->stream_res.dsc, i);

		/* The number of DSCs can be less than the number of pipes */
		if (!pipe_ctx->stream_res.dsc) {
			result = DC_NO_DSC_RESOURCE;
		}

		break;
	}

	return result;
}


static enum dc_status remove_dsc_from_stream_resource(struct dc *dc,
		struct dc_state *new_ctx,
		struct dc_stream_state *dc_stream)
{
	struct pipe_ctx *pipe_ctx = NULL;
	int i;

	for (i = 0; i < MAX_PIPES; i++) {
		if (new_ctx->res_ctx.pipe_ctx[i].stream == dc_stream && !new_ctx->res_ctx.pipe_ctx[i].top_pipe) {
			pipe_ctx = &new_ctx->res_ctx.pipe_ctx[i];

			if (pipe_ctx->stream_res.dsc)
				dcn20_release_dsc(&new_ctx->res_ctx, dc->res_pool, &pipe_ctx->stream_res.dsc);
		}
	}

	if (!pipe_ctx)
		return DC_ERROR_UNEXPECTED;
	else
		return DC_OK;
}


enum dc_status dcn20_add_stream_to_ctx(struct dc *dc, struct dc_state *new_ctx, struct dc_stream_state *dc_stream)
{
	enum dc_status result = DC_ERROR_UNEXPECTED;

	result = resource_map_pool_resources(dc, new_ctx, dc_stream);

	if (result == DC_OK)
		result = resource_map_phy_clock_resources(dc, new_ctx, dc_stream);

	/* Get a DSC if required and available */
	if (result == DC_OK && dc_stream->timing.flags.DSC)
		result = dcn20_add_dsc_to_stream_resource(dc, new_ctx, dc_stream);

	if (result == DC_OK)
		result = dcn20_build_mapped_resource(dc, new_ctx, dc_stream);

	return result;
}


enum dc_status dcn20_remove_stream_from_ctx(struct dc *dc, struct dc_state *new_ctx, struct dc_stream_state *dc_stream)
{
	enum dc_status result = DC_OK;

	result = remove_dsc_from_stream_resource(dc, new_ctx, dc_stream);

	return result;
}


static void swizzle_to_dml_params(
		enum swizzle_mode_values swizzle,
		unsigned int *sw_mode)
{
	switch (swizzle) {
	case DC_SW_LINEAR:
		*sw_mode = dm_sw_linear;
		break;
	case DC_SW_4KB_S:
		*sw_mode = dm_sw_4kb_s;
		break;
	case DC_SW_4KB_S_X:
		*sw_mode = dm_sw_4kb_s_x;
		break;
	case DC_SW_4KB_D:
		*sw_mode = dm_sw_4kb_d;
		break;
	case DC_SW_4KB_D_X:
		*sw_mode = dm_sw_4kb_d_x;
		break;
	case DC_SW_64KB_S:
		*sw_mode = dm_sw_64kb_s;
		break;
	case DC_SW_64KB_S_X:
		*sw_mode = dm_sw_64kb_s_x;
		break;
	case DC_SW_64KB_S_T:
		*sw_mode = dm_sw_64kb_s_t;
		break;
	case DC_SW_64KB_D:
		*sw_mode = dm_sw_64kb_d;
		break;
	case DC_SW_64KB_D_X:
		*sw_mode = dm_sw_64kb_d_x;
		break;
	case DC_SW_64KB_D_T:
		*sw_mode = dm_sw_64kb_d_t;
		break;
	case DC_SW_64KB_R_X:
		*sw_mode = dm_sw_64kb_r_x;
		break;
	case DC_SW_VAR_S:
		*sw_mode = dm_sw_var_s;
		break;
	case DC_SW_VAR_S_X:
		*sw_mode = dm_sw_var_s_x;
		break;
	case DC_SW_VAR_D:
		*sw_mode = dm_sw_var_d;
		break;
	case DC_SW_VAR_D_X:
		*sw_mode = dm_sw_var_d_x;
		break;

	default:
		ASSERT(0); /* Not supported */
		break;
	}
}

bool dcn20_split_stream_for_odm(
		struct resource_context *res_ctx,
		const struct resource_pool *pool,
		struct pipe_ctx *prev_odm_pipe,
		struct pipe_ctx *next_odm_pipe)
{
	int pipe_idx = next_odm_pipe->pipe_idx;

	*next_odm_pipe = *prev_odm_pipe;

	next_odm_pipe->pipe_idx = pipe_idx;
	next_odm_pipe->plane_res.mi = pool->mis[next_odm_pipe->pipe_idx];
	next_odm_pipe->plane_res.hubp = pool->hubps[next_odm_pipe->pipe_idx];
	next_odm_pipe->plane_res.ipp = pool->ipps[next_odm_pipe->pipe_idx];
	next_odm_pipe->plane_res.xfm = pool->transforms[next_odm_pipe->pipe_idx];
	next_odm_pipe->plane_res.dpp = pool->dpps[next_odm_pipe->pipe_idx];
	next_odm_pipe->plane_res.mpcc_inst = pool->dpps[next_odm_pipe->pipe_idx]->inst;
	next_odm_pipe->stream_res.dsc = NULL;
	if (prev_odm_pipe->next_odm_pipe && prev_odm_pipe->next_odm_pipe != next_odm_pipe) {
		next_odm_pipe->next_odm_pipe = prev_odm_pipe->next_odm_pipe;
		next_odm_pipe->next_odm_pipe->prev_odm_pipe = next_odm_pipe;
	}
	prev_odm_pipe->next_odm_pipe = next_odm_pipe;
	next_odm_pipe->prev_odm_pipe = prev_odm_pipe;
	ASSERT(next_odm_pipe->top_pipe == NULL);

	if (prev_odm_pipe->plane_state) {
		struct scaler_data *sd = &prev_odm_pipe->plane_res.scl_data;
		int new_width;

		/* HACTIVE halved for odm combine */
		sd->h_active /= 2;
		/* Calculate new vp and recout for left pipe */
		/* Need at least 16 pixels width per side */
		if (sd->recout.x + 16 >= sd->h_active)
			return false;
		new_width = sd->h_active - sd->recout.x;
		sd->viewport.width -= dc_fixpt_floor(dc_fixpt_mul_int(
				sd->ratios.horz, sd->recout.width - new_width));
		sd->viewport_c.width -= dc_fixpt_floor(dc_fixpt_mul_int(
				sd->ratios.horz_c, sd->recout.width - new_width));
		sd->recout.width = new_width;

		/* Calculate new vp and recout for right pipe */
		sd = &next_odm_pipe->plane_res.scl_data;
		/* HACTIVE halved for odm combine */
		sd->h_active /= 2;
		/* Need at least 16 pixels width per side */
		if (new_width <= 16)
			return false;
		new_width = sd->recout.width + sd->recout.x - sd->h_active;
		sd->viewport.width -= dc_fixpt_floor(dc_fixpt_mul_int(
				sd->ratios.horz, sd->recout.width - new_width));
		sd->viewport_c.width -= dc_fixpt_floor(dc_fixpt_mul_int(
				sd->ratios.horz_c, sd->recout.width - new_width));
		sd->recout.width = new_width;
		sd->viewport.x += dc_fixpt_floor(dc_fixpt_mul_int(
				sd->ratios.horz, sd->h_active - sd->recout.x));
		sd->viewport_c.x += dc_fixpt_floor(dc_fixpt_mul_int(
				sd->ratios.horz_c, sd->h_active - sd->recout.x));
		sd->recout.x = 0;
	}
	next_odm_pipe->stream_res.opp = pool->opps[next_odm_pipe->pipe_idx];
	if (next_odm_pipe->stream->timing.flags.DSC == 1) {
		acquire_dsc(res_ctx, pool, &next_odm_pipe->stream_res.dsc, next_odm_pipe->pipe_idx);
		ASSERT(next_odm_pipe->stream_res.dsc);
		if (next_odm_pipe->stream_res.dsc == NULL)
			return false;
	}

	return true;
}

void dcn20_split_stream_for_mpc(
		struct resource_context *res_ctx,
		const struct resource_pool *pool,
		struct pipe_ctx *primary_pipe,
		struct pipe_ctx *secondary_pipe)
{
	int pipe_idx = secondary_pipe->pipe_idx;
	struct pipe_ctx *sec_bot_pipe = secondary_pipe->bottom_pipe;

	*secondary_pipe = *primary_pipe;
	secondary_pipe->bottom_pipe = sec_bot_pipe;

	secondary_pipe->pipe_idx = pipe_idx;
	secondary_pipe->plane_res.mi = pool->mis[secondary_pipe->pipe_idx];
	secondary_pipe->plane_res.hubp = pool->hubps[secondary_pipe->pipe_idx];
	secondary_pipe->plane_res.ipp = pool->ipps[secondary_pipe->pipe_idx];
	secondary_pipe->plane_res.xfm = pool->transforms[secondary_pipe->pipe_idx];
	secondary_pipe->plane_res.dpp = pool->dpps[secondary_pipe->pipe_idx];
	secondary_pipe->plane_res.mpcc_inst = pool->dpps[secondary_pipe->pipe_idx]->inst;
	secondary_pipe->stream_res.dsc = NULL;
	if (primary_pipe->bottom_pipe && primary_pipe->bottom_pipe != secondary_pipe) {
		ASSERT(!secondary_pipe->bottom_pipe);
		secondary_pipe->bottom_pipe = primary_pipe->bottom_pipe;
		secondary_pipe->bottom_pipe->top_pipe = secondary_pipe;
	}
	primary_pipe->bottom_pipe = secondary_pipe;
	secondary_pipe->top_pipe = primary_pipe;

	ASSERT(primary_pipe->plane_state);
	resource_build_scaling_params(primary_pipe);
	resource_build_scaling_params(secondary_pipe);
}

void dcn20_populate_dml_writeback_from_context(
		struct dc *dc, struct resource_context *res_ctx, display_e2e_pipe_params_st *pipes)
{
	int pipe_cnt, i;

	for (i = 0, pipe_cnt = 0; i < dc->res_pool->pipe_count; i++) {
		struct dc_writeback_info *wb_info = &res_ctx->pipe_ctx[i].stream->writeback_info[0];

		if (!res_ctx->pipe_ctx[i].stream)
			continue;

		/* Set writeback information */
		pipes[pipe_cnt].dout.wb_enable = (wb_info->wb_enabled == true) ? 1 : 0;
		pipes[pipe_cnt].dout.num_active_wb++;
		pipes[pipe_cnt].dout.wb.wb_src_height = wb_info->dwb_params.cnv_params.crop_height;
		pipes[pipe_cnt].dout.wb.wb_src_width = wb_info->dwb_params.cnv_params.crop_width;
		pipes[pipe_cnt].dout.wb.wb_dst_width = wb_info->dwb_params.dest_width;
		pipes[pipe_cnt].dout.wb.wb_dst_height = wb_info->dwb_params.dest_height;
		pipes[pipe_cnt].dout.wb.wb_htaps_luma = 1;
		pipes[pipe_cnt].dout.wb.wb_vtaps_luma = 1;
		pipes[pipe_cnt].dout.wb.wb_htaps_chroma = wb_info->dwb_params.scaler_taps.h_taps_c;
		pipes[pipe_cnt].dout.wb.wb_vtaps_chroma = wb_info->dwb_params.scaler_taps.v_taps_c;
		pipes[pipe_cnt].dout.wb.wb_hratio = 1.0;
		pipes[pipe_cnt].dout.wb.wb_vratio = 1.0;
		if (wb_info->dwb_params.out_format == dwb_scaler_mode_yuv420) {
			if (wb_info->dwb_params.output_depth == DWB_OUTPUT_PIXEL_DEPTH_8BPC)
				pipes[pipe_cnt].dout.wb.wb_pixel_format = dm_420_8;
			else
				pipes[pipe_cnt].dout.wb.wb_pixel_format = dm_420_10;
		} else
			pipes[pipe_cnt].dout.wb.wb_pixel_format = dm_444_32;

		pipe_cnt++;
	}

}

int dcn20_populate_dml_pipes_from_context(
		struct dc *dc, struct dc_state *context, display_e2e_pipe_params_st *pipes)
{
	int pipe_cnt, i;
	bool synchronized_vblank = true;
	struct resource_context *res_ctx = &context->res_ctx;

	for (i = 0, pipe_cnt = -1; i < dc->res_pool->pipe_count; i++) {
		if (!res_ctx->pipe_ctx[i].stream)
			continue;

		if (pipe_cnt < 0) {
			pipe_cnt = i;
			continue;
		}
		if (dc->debug.disable_timing_sync || !resource_are_streams_timing_synchronizable(
				res_ctx->pipe_ctx[pipe_cnt].stream,
				res_ctx->pipe_ctx[i].stream)) {
			synchronized_vblank = false;
			break;
		}
	}

	for (i = 0, pipe_cnt = 0; i < dc->res_pool->pipe_count; i++) {
		struct dc_crtc_timing *timing = &res_ctx->pipe_ctx[i].stream->timing;
		unsigned int v_total;
		unsigned int front_porch;
		int output_bpc;

		if (!res_ctx->pipe_ctx[i].stream)
			continue;

		v_total = timing->v_total;
		front_porch = timing->v_front_porch;
		/* todo:
		pipes[pipe_cnt].pipe.src.dynamic_metadata_enable = 0;
		pipes[pipe_cnt].pipe.src.dcc = 0;
		pipes[pipe_cnt].pipe.src.vm = 0;*/

		pipes[pipe_cnt].clks_cfg.refclk_mhz = dc->res_pool->ref_clocks.dchub_ref_clock_inKhz / 1000.0;

		pipes[pipe_cnt].dout.dsc_enable = res_ctx->pipe_ctx[i].stream->timing.flags.DSC;
		/* todo: rotation?*/
		pipes[pipe_cnt].dout.dsc_slices = res_ctx->pipe_ctx[i].stream->timing.dsc_cfg.num_slices_h;
		if (res_ctx->pipe_ctx[i].stream->use_dynamic_meta) {
			pipes[pipe_cnt].pipe.src.dynamic_metadata_enable = true;
			/* 1/2 vblank */
			pipes[pipe_cnt].pipe.src.dynamic_metadata_lines_before_active =
				(v_total - timing->v_addressable
					- timing->v_border_top - timing->v_border_bottom) / 2;
			/* 36 bytes dp, 32 hdmi */
			pipes[pipe_cnt].pipe.src.dynamic_metadata_xmit_bytes =
				dc_is_dp_signal(res_ctx->pipe_ctx[i].stream->signal) ? 36 : 32;
		}
		pipes[pipe_cnt].pipe.src.dcc = false;
		pipes[pipe_cnt].pipe.src.dcc_rate = 1;
		pipes[pipe_cnt].pipe.dest.synchronized_vblank_all_planes = synchronized_vblank;
		pipes[pipe_cnt].pipe.dest.hblank_start = timing->h_total - timing->h_front_porch;
		pipes[pipe_cnt].pipe.dest.hblank_end = pipes[pipe_cnt].pipe.dest.hblank_start
				- timing->h_addressable
				- timing->h_border_left
				- timing->h_border_right;
		pipes[pipe_cnt].pipe.dest.vblank_start = v_total - front_porch;
		pipes[pipe_cnt].pipe.dest.vblank_end = pipes[pipe_cnt].pipe.dest.vblank_start
				- timing->v_addressable
				- timing->v_border_top
				- timing->v_border_bottom;
		pipes[pipe_cnt].pipe.dest.htotal = timing->h_total;
		pipes[pipe_cnt].pipe.dest.vtotal = v_total;
		pipes[pipe_cnt].pipe.dest.hactive = timing->h_addressable;
		pipes[pipe_cnt].pipe.dest.vactive = timing->v_addressable;
		pipes[pipe_cnt].pipe.dest.interlaced = timing->flags.INTERLACE;
		pipes[pipe_cnt].pipe.dest.pixel_rate_mhz = timing->pix_clk_100hz/10000.0;
		if (timing->timing_3d_format == TIMING_3D_FORMAT_HW_FRAME_PACKING)
			pipes[pipe_cnt].pipe.dest.pixel_rate_mhz *= 2;
		pipes[pipe_cnt].pipe.dest.otg_inst = res_ctx->pipe_ctx[i].stream_res.tg->inst;
		pipes[pipe_cnt].dout.dp_lanes = 4;
		pipes[pipe_cnt].pipe.dest.vtotal_min = res_ctx->pipe_ctx[i].stream->adjust.v_total_min;
		pipes[pipe_cnt].pipe.dest.vtotal_max = res_ctx->pipe_ctx[i].stream->adjust.v_total_max;
		switch (get_num_odm_splits(&res_ctx->pipe_ctx[i])) {
		case 1:
			pipes[pipe_cnt].pipe.dest.odm_combine = dm_odm_combine_mode_2to1;
			break;
		default:
			pipes[pipe_cnt].pipe.dest.odm_combine = dm_odm_combine_mode_disabled;
		}
		pipes[pipe_cnt].pipe.src.hsplit_grp = res_ctx->pipe_ctx[i].pipe_idx;
		if (res_ctx->pipe_ctx[i].top_pipe && res_ctx->pipe_ctx[i].top_pipe->plane_state
				== res_ctx->pipe_ctx[i].plane_state) {
			struct pipe_ctx *first_pipe = res_ctx->pipe_ctx[i].top_pipe;

			while (first_pipe->top_pipe && first_pipe->top_pipe->plane_state
					== res_ctx->pipe_ctx[i].plane_state)
				first_pipe = first_pipe->top_pipe;
			pipes[pipe_cnt].pipe.src.hsplit_grp = first_pipe->pipe_idx;
		} else if (res_ctx->pipe_ctx[i].prev_odm_pipe) {
			struct pipe_ctx *first_pipe = res_ctx->pipe_ctx[i].prev_odm_pipe;

			while (first_pipe->prev_odm_pipe)
				first_pipe = first_pipe->prev_odm_pipe;
			pipes[pipe_cnt].pipe.src.hsplit_grp = first_pipe->pipe_idx;
		}

		switch (res_ctx->pipe_ctx[i].stream->signal) {
		case SIGNAL_TYPE_DISPLAY_PORT_MST:
		case SIGNAL_TYPE_DISPLAY_PORT:
			pipes[pipe_cnt].dout.output_type = dm_dp;
			break;
		case SIGNAL_TYPE_EDP:
			pipes[pipe_cnt].dout.output_type = dm_edp;
			break;
		case SIGNAL_TYPE_HDMI_TYPE_A:
		case SIGNAL_TYPE_DVI_SINGLE_LINK:
		case SIGNAL_TYPE_DVI_DUAL_LINK:
			pipes[pipe_cnt].dout.output_type = dm_hdmi;
			break;
		default:
			/* In case there is no signal, set dp with 4 lanes to allow max config */
			pipes[pipe_cnt].dout.output_type = dm_dp;
			pipes[pipe_cnt].dout.dp_lanes = 4;
		}

		switch (res_ctx->pipe_ctx[i].stream->timing.display_color_depth) {
		case COLOR_DEPTH_666:
			output_bpc = 6;
			break;
		case COLOR_DEPTH_888:
			output_bpc = 8;
			break;
		case COLOR_DEPTH_101010:
			output_bpc = 10;
			break;
		case COLOR_DEPTH_121212:
			output_bpc = 12;
			break;
		case COLOR_DEPTH_141414:
			output_bpc = 14;
			break;
		case COLOR_DEPTH_161616:
			output_bpc = 16;
			break;
		case COLOR_DEPTH_999:
			output_bpc = 9;
			break;
		case COLOR_DEPTH_111111:
			output_bpc = 11;
			break;
		default:
			output_bpc = 8;
			break;
		}

		switch (res_ctx->pipe_ctx[i].stream->timing.pixel_encoding) {
		case PIXEL_ENCODING_RGB:
		case PIXEL_ENCODING_YCBCR444:
			pipes[pipe_cnt].dout.output_format = dm_444;
			pipes[pipe_cnt].dout.output_bpp = output_bpc * 3;
			break;
		case PIXEL_ENCODING_YCBCR420:
			pipes[pipe_cnt].dout.output_format = dm_420;
			pipes[pipe_cnt].dout.output_bpp = (output_bpc * 3.0) / 2;
			break;
		case PIXEL_ENCODING_YCBCR422:
			if (true) /* todo */
				pipes[pipe_cnt].dout.output_format = dm_s422;
			else
				pipes[pipe_cnt].dout.output_format = dm_n422;
			pipes[pipe_cnt].dout.output_bpp = output_bpc * 2;
			break;
		default:
			pipes[pipe_cnt].dout.output_format = dm_444;
			pipes[pipe_cnt].dout.output_bpp = output_bpc * 3;
		}

		if (res_ctx->pipe_ctx[i].stream->timing.flags.DSC)
			pipes[pipe_cnt].dout.output_bpp = res_ctx->pipe_ctx[i].stream->timing.dsc_cfg.bits_per_pixel / 16.0;

		/* todo: default max for now, until there is logic reflecting this in dc*/
		pipes[pipe_cnt].dout.output_bpc = 12;
		/*
		 * For graphic plane, cursor number is 1, nv12 is 0
		 * bw calculations due to cursor on/off
		 */
		if (res_ctx->pipe_ctx[i].plane_state &&
				res_ctx->pipe_ctx[i].plane_state->address.type == PLN_ADDR_TYPE_VIDEO_PROGRESSIVE)
			pipes[pipe_cnt].pipe.src.num_cursors = 0;
		else
			pipes[pipe_cnt].pipe.src.num_cursors = dc->dml.ip.number_of_cursors;

		pipes[pipe_cnt].pipe.src.cur0_src_width = 256;
		pipes[pipe_cnt].pipe.src.cur0_bpp = dm_cur_32bit;

		if (!res_ctx->pipe_ctx[i].plane_state) {
			pipes[pipe_cnt].pipe.src.is_hsplit = pipes[pipe_cnt].pipe.dest.odm_combine != dm_odm_combine_mode_disabled;
			pipes[pipe_cnt].pipe.src.source_scan = dm_horz;
			pipes[pipe_cnt].pipe.src.sw_mode = dm_sw_linear;
			pipes[pipe_cnt].pipe.src.macro_tile_size = dm_64k_tile;
			pipes[pipe_cnt].pipe.src.viewport_width = timing->h_addressable;
			if (pipes[pipe_cnt].pipe.src.viewport_width > 1920)
				pipes[pipe_cnt].pipe.src.viewport_width = 1920;
			pipes[pipe_cnt].pipe.src.viewport_height = timing->v_addressable;
			if (pipes[pipe_cnt].pipe.src.viewport_height > 1080)
				pipes[pipe_cnt].pipe.src.viewport_height = 1080;
			pipes[pipe_cnt].pipe.src.surface_height_y = pipes[pipe_cnt].pipe.src.viewport_height;
			pipes[pipe_cnt].pipe.src.surface_width_y = pipes[pipe_cnt].pipe.src.viewport_width;
			pipes[pipe_cnt].pipe.src.surface_height_c = pipes[pipe_cnt].pipe.src.viewport_height;
			pipes[pipe_cnt].pipe.src.surface_width_c = pipes[pipe_cnt].pipe.src.viewport_width;
			pipes[pipe_cnt].pipe.src.data_pitch = ((pipes[pipe_cnt].pipe.src.viewport_width + 63) / 64) * 64; /* linear sw only */
			pipes[pipe_cnt].pipe.src.source_format = dm_444_32;
			pipes[pipe_cnt].pipe.dest.recout_width = pipes[pipe_cnt].pipe.src.viewport_width; /*vp_width/hratio*/
			pipes[pipe_cnt].pipe.dest.recout_height = pipes[pipe_cnt].pipe.src.viewport_height; /*vp_height/vratio*/
			pipes[pipe_cnt].pipe.dest.full_recout_width = pipes[pipe_cnt].pipe.dest.recout_width;  /*when is_hsplit != 1*/
			pipes[pipe_cnt].pipe.dest.full_recout_height = pipes[pipe_cnt].pipe.dest.recout_height; /*when is_hsplit != 1*/
			pipes[pipe_cnt].pipe.scale_ratio_depth.lb_depth = dm_lb_16;
			pipes[pipe_cnt].pipe.scale_ratio_depth.hscl_ratio = 1.0;
			pipes[pipe_cnt].pipe.scale_ratio_depth.vscl_ratio = 1.0;
			pipes[pipe_cnt].pipe.scale_ratio_depth.scl_enable = 0; /*Lb only or Full scl*/
			pipes[pipe_cnt].pipe.scale_taps.htaps = 1;
			pipes[pipe_cnt].pipe.scale_taps.vtaps = 1;
			pipes[pipe_cnt].pipe.dest.vtotal_min = v_total;
			pipes[pipe_cnt].pipe.dest.vtotal_max = v_total;

			if (pipes[pipe_cnt].pipe.dest.odm_combine == dm_odm_combine_mode_2to1) {
				pipes[pipe_cnt].pipe.src.viewport_width /= 2;
				pipes[pipe_cnt].pipe.dest.recout_width /= 2;
			}
		} else {
			struct dc_plane_state *pln = res_ctx->pipe_ctx[i].plane_state;
			struct scaler_data *scl = &res_ctx->pipe_ctx[i].plane_res.scl_data;

			pipes[pipe_cnt].pipe.src.immediate_flip = pln->flip_immediate;
			pipes[pipe_cnt].pipe.src.is_hsplit = (res_ctx->pipe_ctx[i].bottom_pipe && res_ctx->pipe_ctx[i].bottom_pipe->plane_state == pln)
					|| (res_ctx->pipe_ctx[i].top_pipe && res_ctx->pipe_ctx[i].top_pipe->plane_state == pln)
					|| pipes[pipe_cnt].pipe.dest.odm_combine != dm_odm_combine_mode_disabled;
			pipes[pipe_cnt].pipe.src.source_scan = pln->rotation == ROTATION_ANGLE_90
					|| pln->rotation == ROTATION_ANGLE_270 ? dm_vert : dm_horz;
			pipes[pipe_cnt].pipe.src.viewport_y_y = scl->viewport.y;
			pipes[pipe_cnt].pipe.src.viewport_y_c = scl->viewport_c.y;
			pipes[pipe_cnt].pipe.src.viewport_width = scl->viewport.width;
			pipes[pipe_cnt].pipe.src.viewport_width_c = scl->viewport_c.width;
			pipes[pipe_cnt].pipe.src.viewport_height = scl->viewport.height;
			pipes[pipe_cnt].pipe.src.viewport_height_c = scl->viewport_c.height;
			pipes[pipe_cnt].pipe.src.surface_width_y = pln->plane_size.surface_size.width;
			pipes[pipe_cnt].pipe.src.surface_height_y = pln->plane_size.surface_size.height;
			pipes[pipe_cnt].pipe.src.surface_width_c = pln->plane_size.chroma_size.width;
			pipes[pipe_cnt].pipe.src.surface_height_c = pln->plane_size.chroma_size.height;
			if (pln->format >= SURFACE_PIXEL_FORMAT_VIDEO_BEGIN) {
				pipes[pipe_cnt].pipe.src.data_pitch = pln->plane_size.surface_pitch;
				pipes[pipe_cnt].pipe.src.data_pitch_c = pln->plane_size.chroma_pitch;
				pipes[pipe_cnt].pipe.src.meta_pitch = pln->dcc.meta_pitch;
				pipes[pipe_cnt].pipe.src.meta_pitch_c = pln->dcc.meta_pitch_c;
			} else {
				pipes[pipe_cnt].pipe.src.data_pitch = pln->plane_size.surface_pitch;
				pipes[pipe_cnt].pipe.src.meta_pitch = pln->dcc.meta_pitch;
			}
			pipes[pipe_cnt].pipe.src.dcc = pln->dcc.enable;
			pipes[pipe_cnt].pipe.dest.recout_width = scl->recout.width;
			pipes[pipe_cnt].pipe.dest.recout_height = scl->recout.height;
			pipes[pipe_cnt].pipe.dest.full_recout_height = scl->recout.height;
			pipes[pipe_cnt].pipe.dest.full_recout_width = scl->recout.width;
			if (pipes[pipe_cnt].pipe.dest.odm_combine == dm_odm_combine_mode_2to1)
				pipes[pipe_cnt].pipe.dest.full_recout_width *= 2;
			else {
				struct pipe_ctx *split_pipe = res_ctx->pipe_ctx[i].bottom_pipe;

				while (split_pipe && split_pipe->plane_state == pln) {
					pipes[pipe_cnt].pipe.dest.full_recout_width += split_pipe->plane_res.scl_data.recout.width;
					split_pipe = split_pipe->bottom_pipe;
				}
				split_pipe = res_ctx->pipe_ctx[i].top_pipe;
				while (split_pipe && split_pipe->plane_state == pln) {
					pipes[pipe_cnt].pipe.dest.full_recout_width += split_pipe->plane_res.scl_data.recout.width;
					split_pipe = split_pipe->top_pipe;
				}
			}

			pipes[pipe_cnt].pipe.scale_ratio_depth.lb_depth = dm_lb_16;
			pipes[pipe_cnt].pipe.scale_ratio_depth.hscl_ratio = (double) scl->ratios.horz.value / (1ULL<<32);
			pipes[pipe_cnt].pipe.scale_ratio_depth.hscl_ratio_c = (double) scl->ratios.horz_c.value / (1ULL<<32);
			pipes[pipe_cnt].pipe.scale_ratio_depth.vscl_ratio = (double) scl->ratios.vert.value / (1ULL<<32);
			pipes[pipe_cnt].pipe.scale_ratio_depth.vscl_ratio_c = (double) scl->ratios.vert_c.value / (1ULL<<32);
			pipes[pipe_cnt].pipe.scale_ratio_depth.scl_enable =
					scl->ratios.vert.value != dc_fixpt_one.value
					|| scl->ratios.horz.value != dc_fixpt_one.value
					|| scl->ratios.vert_c.value != dc_fixpt_one.value
					|| scl->ratios.horz_c.value != dc_fixpt_one.value /*Lb only or Full scl*/
					|| dc->debug.always_scale; /*support always scale*/
			pipes[pipe_cnt].pipe.scale_taps.htaps = scl->taps.h_taps;
			pipes[pipe_cnt].pipe.scale_taps.htaps_c = scl->taps.h_taps_c;
			pipes[pipe_cnt].pipe.scale_taps.vtaps = scl->taps.v_taps;
			pipes[pipe_cnt].pipe.scale_taps.vtaps_c = scl->taps.v_taps_c;

			pipes[pipe_cnt].pipe.src.macro_tile_size =
					swizzle_mode_to_macro_tile_size(pln->tiling_info.gfx9.swizzle);
			swizzle_to_dml_params(pln->tiling_info.gfx9.swizzle,
					&pipes[pipe_cnt].pipe.src.sw_mode);

			switch (pln->format) {
			case SURFACE_PIXEL_FORMAT_VIDEO_420_YCbCr:
			case SURFACE_PIXEL_FORMAT_VIDEO_420_YCrCb:
				pipes[pipe_cnt].pipe.src.source_format = dm_420_8;
				break;
			case SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCbCr:
			case SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCrCb:
				pipes[pipe_cnt].pipe.src.source_format = dm_420_10;
				break;
			case SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616:
			case SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616F:
			case SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616F:
				pipes[pipe_cnt].pipe.src.source_format = dm_444_64;
				break;
			case SURFACE_PIXEL_FORMAT_GRPH_ARGB1555:
			case SURFACE_PIXEL_FORMAT_GRPH_RGB565:
				pipes[pipe_cnt].pipe.src.source_format = dm_444_16;
				break;
			case SURFACE_PIXEL_FORMAT_GRPH_PALETA_256_COLORS:
				pipes[pipe_cnt].pipe.src.source_format = dm_444_8;
				break;
			default:
				pipes[pipe_cnt].pipe.src.source_format = dm_444_32;
				break;
			}
		}

		pipe_cnt++;
	}

	/* populate writeback information */
	dc->res_pool->funcs->populate_dml_writeback_from_context(dc, res_ctx, pipes);

	return pipe_cnt;
}

unsigned int dcn20_calc_max_scaled_time(
		unsigned int time_per_pixel,
		enum mmhubbub_wbif_mode mode,
		unsigned int urgent_watermark)
{
	unsigned int time_per_byte = 0;
	unsigned int total_y_free_entry = 0x200; /* two memory piece for luma */
	unsigned int total_c_free_entry = 0x140; /* two memory piece for chroma */
	unsigned int small_free_entry, max_free_entry;
	unsigned int buf_lh_capability;
	unsigned int max_scaled_time;

	if (mode == PACKED_444) /* packed mode */
		time_per_byte = time_per_pixel/4;
	else if (mode == PLANAR_420_8BPC)
		time_per_byte  = time_per_pixel;
	else if (mode == PLANAR_420_10BPC) /* p010 */
		time_per_byte  = time_per_pixel * 819/1024;

	if (time_per_byte == 0)
		time_per_byte = 1;

	small_free_entry  = (total_y_free_entry > total_c_free_entry) ? total_c_free_entry : total_y_free_entry;
	max_free_entry    = (mode == PACKED_444) ? total_y_free_entry + total_c_free_entry : small_free_entry;
	buf_lh_capability = max_free_entry*time_per_byte*32/16; /* there is 4bit fraction */
	max_scaled_time   = buf_lh_capability - urgent_watermark;
	return max_scaled_time;
}

void dcn20_set_mcif_arb_params(
		struct dc *dc,
		struct dc_state *context,
		display_e2e_pipe_params_st *pipes,
		int pipe_cnt)
{
	enum mmhubbub_wbif_mode wbif_mode;
	struct mcif_arb_params *wb_arb_params;
	int i, j, k, dwb_pipe;

	/* Writeback MCIF_WB arbitration parameters */
	dwb_pipe = 0;
	for (i = 0; i < dc->res_pool->pipe_count; i++) {

		if (!context->res_ctx.pipe_ctx[i].stream)
			continue;

		for (j = 0; j < MAX_DWB_PIPES; j++) {
			if (context->res_ctx.pipe_ctx[i].stream->writeback_info[j].wb_enabled == false)
				continue;

			//wb_arb_params = &context->res_ctx.pipe_ctx[i].stream->writeback_info[j].mcif_arb_params;
			wb_arb_params = &context->bw_ctx.bw.dcn.bw_writeback.mcif_wb_arb[dwb_pipe];

			if (context->res_ctx.pipe_ctx[i].stream->writeback_info[j].dwb_params.out_format == dwb_scaler_mode_yuv420) {
				if (context->res_ctx.pipe_ctx[i].stream->writeback_info[j].dwb_params.output_depth == DWB_OUTPUT_PIXEL_DEPTH_8BPC)
					wbif_mode = PLANAR_420_8BPC;
				else
					wbif_mode = PLANAR_420_10BPC;
			} else
				wbif_mode = PACKED_444;

			for (k = 0; k < sizeof(wb_arb_params->cli_watermark)/sizeof(wb_arb_params->cli_watermark[0]); k++) {
				wb_arb_params->cli_watermark[k] = get_wm_writeback_urgent(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
				wb_arb_params->pstate_watermark[k] = get_wm_writeback_dram_clock_change(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
			}
			wb_arb_params->time_per_pixel = 16.0 / context->res_ctx.pipe_ctx[i].stream->phy_pix_clk; /* 4 bit fraction, ms */
			wb_arb_params->slice_lines = 32;
			wb_arb_params->arbitration_slice = 2;
			wb_arb_params->max_scaled_time = dcn20_calc_max_scaled_time(wb_arb_params->time_per_pixel,
				wbif_mode,
				wb_arb_params->cli_watermark[0]); /* assume 4 watermark sets have the same value */

			dwb_pipe++;

			if (dwb_pipe >= MAX_DWB_PIPES)
				return;
		}
		if (dwb_pipe >= MAX_DWB_PIPES)
			return;
	}
}

bool dcn20_validate_dsc(struct dc *dc, struct dc_state *new_ctx)
{
	int i;

	/* Validate DSC config, dsc count validation is already done */
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe_ctx = &new_ctx->res_ctx.pipe_ctx[i];
		struct dc_stream_state *stream = pipe_ctx->stream;
		struct dsc_config dsc_cfg;
		struct pipe_ctx *odm_pipe;
		int opp_cnt = 1;

		for (odm_pipe = pipe_ctx->next_odm_pipe; odm_pipe; odm_pipe = odm_pipe->next_odm_pipe)
			opp_cnt++;

		/* Only need to validate top pipe */
		if (pipe_ctx->top_pipe || pipe_ctx->prev_odm_pipe || !stream || !stream->timing.flags.DSC)
			continue;

		dsc_cfg.pic_width = (stream->timing.h_addressable + stream->timing.h_border_left
				+ stream->timing.h_border_right) / opp_cnt;
		dsc_cfg.pic_height = stream->timing.v_addressable + stream->timing.v_border_top
				+ stream->timing.v_border_bottom;
		dsc_cfg.pixel_encoding = stream->timing.pixel_encoding;
		dsc_cfg.color_depth = stream->timing.display_color_depth;
		dsc_cfg.is_odm = pipe_ctx->next_odm_pipe ? true : false;
		dsc_cfg.dc_dsc_cfg = stream->timing.dsc_cfg;
		dsc_cfg.dc_dsc_cfg.num_slices_h /= opp_cnt;

		if (!pipe_ctx->stream_res.dsc->funcs->dsc_validate_stream(pipe_ctx->stream_res.dsc, &dsc_cfg))
			return false;
	}
	return true;
}

struct pipe_ctx *dcn20_find_secondary_pipe(struct dc *dc,
		struct resource_context *res_ctx,
		const struct resource_pool *pool,
		const struct pipe_ctx *primary_pipe)
{
	struct pipe_ctx *secondary_pipe = NULL;

	if (dc && primary_pipe) {
		int j;
		int preferred_pipe_idx = 0;

		/* first check the prev dc state:
		 * if this primary pipe has a bottom pipe in prev. state
		 * and if the bottom pipe is still available (which it should be),
		 * pick that pipe as secondary
		 * Same logic applies for ODM pipes. Since mpo is not allowed with odm
		 * check in else case.
		 */
		if (dc->current_state->res_ctx.pipe_ctx[primary_pipe->pipe_idx].bottom_pipe) {
			preferred_pipe_idx = dc->current_state->res_ctx.pipe_ctx[primary_pipe->pipe_idx].bottom_pipe->pipe_idx;
			if (res_ctx->pipe_ctx[preferred_pipe_idx].stream == NULL) {
				secondary_pipe = &res_ctx->pipe_ctx[preferred_pipe_idx];
				secondary_pipe->pipe_idx = preferred_pipe_idx;
			}
		} else if (dc->current_state->res_ctx.pipe_ctx[primary_pipe->pipe_idx].next_odm_pipe) {
			preferred_pipe_idx = dc->current_state->res_ctx.pipe_ctx[primary_pipe->pipe_idx].next_odm_pipe->pipe_idx;
			if (res_ctx->pipe_ctx[preferred_pipe_idx].stream == NULL) {
				secondary_pipe = &res_ctx->pipe_ctx[preferred_pipe_idx];
				secondary_pipe->pipe_idx = preferred_pipe_idx;
			}
		}

		/*
		 * if this primary pipe does not have a bottom pipe in prev. state
		 * start backward and find a pipe that did not used to be a bottom pipe in
		 * prev. dc state. This way we make sure we keep the same assignment as
		 * last state and will not have to reprogram every pipe
		 */
		if (secondary_pipe == NULL) {
			for (j = dc->res_pool->pipe_count - 1; j >= 0; j--) {
				if (dc->current_state->res_ctx.pipe_ctx[j].top_pipe == NULL
						&& dc->current_state->res_ctx.pipe_ctx[j].prev_odm_pipe == NULL) {
					preferred_pipe_idx = j;

					if (res_ctx->pipe_ctx[preferred_pipe_idx].stream == NULL) {
						secondary_pipe = &res_ctx->pipe_ctx[preferred_pipe_idx];
						secondary_pipe->pipe_idx = preferred_pipe_idx;
						break;
					}
				}
			}
		}
		/*
		 * We should never hit this assert unless assignments are shuffled around
		 * if this happens we will prob. hit a vsync tdr
		 */
		ASSERT(secondary_pipe);
		/*
		 * search backwards for the second pipe to keep pipe
		 * assignment more consistent
		 */
		if (secondary_pipe == NULL) {
			for (j = dc->res_pool->pipe_count - 1; j >= 0; j--) {
				preferred_pipe_idx = j;

				if (res_ctx->pipe_ctx[preferred_pipe_idx].stream == NULL) {
					secondary_pipe = &res_ctx->pipe_ctx[preferred_pipe_idx];
					secondary_pipe->pipe_idx = preferred_pipe_idx;
					break;
				}
			}
		}
	}

	return secondary_pipe;
}

static void dcn20_merge_pipes_for_validate(
		struct dc *dc,
		struct dc_state *context)
{
	int i;

	/* merge previously split odm pipes since mode support needs to make the decision */
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];
		struct pipe_ctx *odm_pipe = pipe->next_odm_pipe;

		if (pipe->prev_odm_pipe)
			continue;

		pipe->next_odm_pipe = NULL;
		while (odm_pipe) {
			struct pipe_ctx *next_odm_pipe = odm_pipe->next_odm_pipe;

			odm_pipe->plane_state = NULL;
			odm_pipe->stream = NULL;
			odm_pipe->top_pipe = NULL;
			odm_pipe->bottom_pipe = NULL;
			odm_pipe->prev_odm_pipe = NULL;
			odm_pipe->next_odm_pipe = NULL;
			if (odm_pipe->stream_res.dsc)
				dcn20_release_dsc(&context->res_ctx, dc->res_pool, &odm_pipe->stream_res.dsc);
			/* Clear plane_res and stream_res */
			memset(&odm_pipe->plane_res, 0, sizeof(odm_pipe->plane_res));
			memset(&odm_pipe->stream_res, 0, sizeof(odm_pipe->stream_res));
			odm_pipe = next_odm_pipe;
		}
		if (pipe->plane_state)
			resource_build_scaling_params(pipe);
	}

	/* merge previously mpc split pipes since mode support needs to make the decision */
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];
		struct pipe_ctx *hsplit_pipe = pipe->bottom_pipe;

		if (!hsplit_pipe || hsplit_pipe->plane_state != pipe->plane_state)
			continue;

		pipe->bottom_pipe = hsplit_pipe->bottom_pipe;
		if (hsplit_pipe->bottom_pipe)
			hsplit_pipe->bottom_pipe->top_pipe = pipe;
		hsplit_pipe->plane_state = NULL;
		hsplit_pipe->stream = NULL;
		hsplit_pipe->top_pipe = NULL;
		hsplit_pipe->bottom_pipe = NULL;

		/* Clear plane_res and stream_res */
		memset(&hsplit_pipe->plane_res, 0, sizeof(hsplit_pipe->plane_res));
		memset(&hsplit_pipe->stream_res, 0, sizeof(hsplit_pipe->stream_res));
		if (pipe->plane_state)
			resource_build_scaling_params(pipe);
	}
}

int dcn20_validate_apply_pipe_split_flags(
		struct dc *dc,
		struct dc_state *context,
		int vlevel,
		bool *split,
		bool *merge)
{
	int i, pipe_idx, vlevel_split;
	int plane_count = 0;
	bool force_split = false;
	bool avoid_split = dc->debug.pipe_split_policy == MPC_SPLIT_AVOID;

	if (context->stream_count > 1) {
		if (dc->debug.pipe_split_policy == MPC_SPLIT_AVOID_MULT_DISP)
			avoid_split = true;
	} else if (dc->debug.force_single_disp_pipe_split)
			force_split = true;

	/* TODO: fix dc bugs and remove this split threshold thing */
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];

		if (pipe->stream && !pipe->prev_odm_pipe &&
				(!pipe->top_pipe || pipe->top_pipe->plane_state != pipe->plane_state))
			++plane_count;
	}
	if (plane_count > dc->res_pool->pipe_count / 2)
		avoid_split = true;

	/* Avoid split loop looks for lowest voltage level that allows most unsplit pipes possible */
	if (avoid_split) {
		for (i = 0, pipe_idx = 0; i < dc->res_pool->pipe_count; i++) {
			if (!context->res_ctx.pipe_ctx[i].stream)
				continue;

			for (vlevel_split = vlevel; vlevel <= context->bw_ctx.dml.soc.num_states; vlevel++)
				if (context->bw_ctx.dml.vba.NoOfDPP[vlevel][0][pipe_idx] == 1)
					break;
			/* Impossible to not split this pipe */
			if (vlevel > context->bw_ctx.dml.soc.num_states)
				vlevel = vlevel_split;
			pipe_idx++;
		}
		context->bw_ctx.dml.vba.maxMpcComb = 0;
	}

	/* Split loop sets which pipe should be split based on dml outputs and dc flags */
	for (i = 0, pipe_idx = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];
		int pipe_plane = context->bw_ctx.dml.vba.pipe_plane[pipe_idx];

		if (!context->res_ctx.pipe_ctx[i].stream)
			continue;

		if (force_split || context->bw_ctx.dml.vba.NoOfDPP[vlevel][context->bw_ctx.dml.vba.maxMpcComb][pipe_plane] > 1)
			split[i] = true;
		if ((pipe->stream->view_format ==
				VIEW_3D_FORMAT_SIDE_BY_SIDE ||
				pipe->stream->view_format ==
				VIEW_3D_FORMAT_TOP_AND_BOTTOM) &&
				(pipe->stream->timing.timing_3d_format ==
				TIMING_3D_FORMAT_TOP_AND_BOTTOM ||
				 pipe->stream->timing.timing_3d_format ==
				TIMING_3D_FORMAT_SIDE_BY_SIDE))
			split[i] = true;
		if (dc->debug.force_odm_combine & (1 << pipe->stream_res.tg->inst)) {
			split[i] = true;
			context->bw_ctx.dml.vba.ODMCombineEnablePerState[vlevel][pipe_plane] = dm_odm_combine_mode_2to1;
		}
		context->bw_ctx.dml.vba.ODMCombineEnabled[pipe_plane] =
			context->bw_ctx.dml.vba.ODMCombineEnablePerState[vlevel][pipe_plane];

		if (pipe->prev_odm_pipe && context->bw_ctx.dml.vba.ODMCombineEnabled[pipe_plane] != dm_odm_combine_mode_disabled) {
			/*Already split odm pipe tree, don't try to split again*/
			split[i] = false;
			split[pipe->prev_odm_pipe->pipe_idx] = false;
		} else if (pipe->top_pipe && pipe->plane_state == pipe->top_pipe->plane_state
				&& context->bw_ctx.dml.vba.ODMCombineEnabled[pipe_plane] == dm_odm_combine_mode_disabled) {
			/*Already split mpc tree, don't try to split again, assumes only 2x mpc combine*/
			split[i] = false;
			split[pipe->top_pipe->pipe_idx] = false;
		} else if (pipe->prev_odm_pipe || (pipe->top_pipe && pipe->plane_state == pipe->top_pipe->plane_state)) {
			if (split[i] == false) {
				/*Exiting mpc/odm combine*/
				merge[i] = true;
				if (pipe->prev_odm_pipe) {
					ASSERT(0); /*should not actually happen yet*/
					merge[pipe->prev_odm_pipe->pipe_idx] = true;
				} else
					merge[pipe->top_pipe->pipe_idx] = true;
			} else {
				/*Transition from mpc combine to odm combine or vice versa*/
				ASSERT(0); /*should not actually happen yet*/
				split[i] = true;
				merge[i] = true;
				if (pipe->prev_odm_pipe) {
					split[pipe->prev_odm_pipe->pipe_idx] = true;
					merge[pipe->prev_odm_pipe->pipe_idx] = true;
				} else {
					split[pipe->top_pipe->pipe_idx] = true;
					merge[pipe->top_pipe->pipe_idx] = true;
				}
			}
		}

		/* Adjust dppclk when split is forced, do not bother with dispclk */
		if (split[i] && context->bw_ctx.dml.vba.NoOfDPP[vlevel][context->bw_ctx.dml.vba.maxMpcComb][pipe_idx] == 1)
			context->bw_ctx.dml.vba.RequiredDPPCLK[vlevel][context->bw_ctx.dml.vba.maxMpcComb][pipe_idx] /= 2;
		pipe_idx++;
	}

	return vlevel;
}

bool dcn20_fast_validate_bw(
		struct dc *dc,
		struct dc_state *context,
		display_e2e_pipe_params_st *pipes,
		int *pipe_cnt_out,
		int *pipe_split_from,
		int *vlevel_out)
{
	bool out = false;
	bool split[MAX_PIPES] = { false };
	int pipe_cnt, i, pipe_idx, vlevel;

	ASSERT(pipes);
	if (!pipes)
		return false;

	dcn20_merge_pipes_for_validate(dc, context);

	pipe_cnt = dc->res_pool->funcs->populate_dml_pipes(dc, context, pipes);

	*pipe_cnt_out = pipe_cnt;

	if (!pipe_cnt) {
		out = true;
		goto validate_out;
	}

	vlevel = dml_get_voltage_level(&context->bw_ctx.dml, pipes, pipe_cnt);

	if (vlevel > context->bw_ctx.dml.soc.num_states)
		goto validate_fail;

	vlevel = dcn20_validate_apply_pipe_split_flags(dc, context, vlevel, split, NULL);

	/*initialize pipe_just_split_from to invalid idx*/
	for (i = 0; i < MAX_PIPES; i++)
		pipe_split_from[i] = -1;

	for (i = 0, pipe_idx = -1; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];
		struct pipe_ctx *hsplit_pipe = pipe->bottom_pipe;

		if (!pipe->stream || pipe_split_from[i] >= 0)
			continue;

		pipe_idx++;

		if (!pipe->top_pipe && !pipe->plane_state && context->bw_ctx.dml.vba.ODMCombineEnabled[pipe_idx]) {
			hsplit_pipe = dcn20_find_secondary_pipe(dc, &context->res_ctx, dc->res_pool, pipe);
			ASSERT(hsplit_pipe);
			if (!dcn20_split_stream_for_odm(
					&context->res_ctx, dc->res_pool,
					pipe, hsplit_pipe))
				goto validate_fail;
			pipe_split_from[hsplit_pipe->pipe_idx] = pipe_idx;
			dcn20_build_mapped_resource(dc, context, pipe->stream);
		}

		if (!pipe->plane_state)
			continue;
		/* Skip 2nd half of already split pipe */
		if (pipe->top_pipe && pipe->plane_state == pipe->top_pipe->plane_state)
			continue;

		/* We do not support mpo + odm at the moment */
		if (hsplit_pipe && hsplit_pipe->plane_state != pipe->plane_state
				&& context->bw_ctx.dml.vba.ODMCombineEnabled[pipe_idx])
			goto validate_fail;

		if (split[i]) {
			if (!hsplit_pipe || hsplit_pipe->plane_state != pipe->plane_state) {
				/* pipe not split previously needs split */
				hsplit_pipe = dcn20_find_secondary_pipe(dc, &context->res_ctx, dc->res_pool, pipe);
				ASSERT(hsplit_pipe);
				if (!hsplit_pipe) {
					context->bw_ctx.dml.vba.RequiredDPPCLK[vlevel][context->bw_ctx.dml.vba.maxMpcComb][pipe_idx] *= 2;
					continue;
				}
				if (context->bw_ctx.dml.vba.ODMCombineEnabled[pipe_idx]) {
					if (!dcn20_split_stream_for_odm(
							&context->res_ctx, dc->res_pool,
							pipe, hsplit_pipe))
						goto validate_fail;
					dcn20_build_mapped_resource(dc, context, pipe->stream);
				} else
					dcn20_split_stream_for_mpc(
						&context->res_ctx, dc->res_pool,
						pipe, hsplit_pipe);
				pipe_split_from[hsplit_pipe->pipe_idx] = pipe_idx;
			}
		} else if (hsplit_pipe && hsplit_pipe->plane_state == pipe->plane_state) {
			/* merge should already have been done */
			ASSERT(0);
		}
	}
	/* Actual dsc count per stream dsc validation*/
	if (!dcn20_validate_dsc(dc, context)) {
		context->bw_ctx.dml.vba.ValidationStatus[context->bw_ctx.dml.vba.soc.num_states] =
				DML_FAIL_DSC_VALIDATION_FAILURE;
		goto validate_fail;
	}

	*vlevel_out = vlevel;

	out = true;
	goto validate_out;

validate_fail:
	out = false;

validate_out:
	return out;
}

static void dcn20_calculate_wm(
		struct dc *dc, struct dc_state *context,
		display_e2e_pipe_params_st *pipes,
		int *out_pipe_cnt,
		int *pipe_split_from,
		int vlevel)
{
	int pipe_cnt, i, pipe_idx;

	for (i = 0, pipe_idx = 0, pipe_cnt = 0; i < dc->res_pool->pipe_count; i++) {
		if (!context->res_ctx.pipe_ctx[i].stream)
			continue;

		pipes[pipe_cnt].clks_cfg.refclk_mhz = dc->res_pool->ref_clocks.dchub_ref_clock_inKhz / 1000.0;
		pipes[pipe_cnt].clks_cfg.dispclk_mhz = context->bw_ctx.dml.vba.RequiredDISPCLK[vlevel][context->bw_ctx.dml.vba.maxMpcComb];

		if (pipe_split_from[i] < 0) {
			pipes[pipe_cnt].clks_cfg.dppclk_mhz =
					context->bw_ctx.dml.vba.RequiredDPPCLK[vlevel][context->bw_ctx.dml.vba.maxMpcComb][pipe_idx];
			if (context->bw_ctx.dml.vba.BlendingAndTiming[pipe_idx] == pipe_idx)
				pipes[pipe_cnt].pipe.dest.odm_combine =
						context->bw_ctx.dml.vba.ODMCombineEnabled[pipe_idx];
			else
				pipes[pipe_cnt].pipe.dest.odm_combine = 0;
			pipe_idx++;
		} else {
			pipes[pipe_cnt].clks_cfg.dppclk_mhz =
					context->bw_ctx.dml.vba.RequiredDPPCLK[vlevel][context->bw_ctx.dml.vba.maxMpcComb][pipe_split_from[i]];
			if (context->bw_ctx.dml.vba.BlendingAndTiming[pipe_split_from[i]] == pipe_split_from[i])
				pipes[pipe_cnt].pipe.dest.odm_combine =
						context->bw_ctx.dml.vba.ODMCombineEnabled[pipe_split_from[i]];
			else
				pipes[pipe_cnt].pipe.dest.odm_combine = 0;
		}

		if (dc->config.forced_clocks) {
			pipes[pipe_cnt].clks_cfg.dispclk_mhz = context->bw_ctx.dml.soc.clock_limits[0].dispclk_mhz;
			pipes[pipe_cnt].clks_cfg.dppclk_mhz = context->bw_ctx.dml.soc.clock_limits[0].dppclk_mhz;
		}
		if (dc->debug.min_disp_clk_khz > pipes[pipe_cnt].clks_cfg.dispclk_mhz * 1000)
			pipes[pipe_cnt].clks_cfg.dispclk_mhz = dc->debug.min_disp_clk_khz / 1000.0;
		if (dc->debug.min_dpp_clk_khz > pipes[pipe_cnt].clks_cfg.dppclk_mhz * 1000)
			pipes[pipe_cnt].clks_cfg.dppclk_mhz = dc->debug.min_dpp_clk_khz / 1000.0;

		pipe_cnt++;
	}

	if (pipe_cnt != pipe_idx) {
		if (dc->res_pool->funcs->populate_dml_pipes)
			pipe_cnt = dc->res_pool->funcs->populate_dml_pipes(dc,
				context, pipes);
		else
			pipe_cnt = dcn20_populate_dml_pipes_from_context(dc,
				context, pipes);
	}

	*out_pipe_cnt = pipe_cnt;

	pipes[0].clks_cfg.voltage = vlevel;
	pipes[0].clks_cfg.dcfclk_mhz = context->bw_ctx.dml.soc.clock_limits[vlevel].dcfclk_mhz;
	pipes[0].clks_cfg.socclk_mhz = context->bw_ctx.dml.soc.clock_limits[vlevel].socclk_mhz;

	/* only pipe 0 is read for voltage and dcf/soc clocks */
	if (vlevel < 1) {
		pipes[0].clks_cfg.voltage = 1;
		pipes[0].clks_cfg.dcfclk_mhz = context->bw_ctx.dml.soc.clock_limits[1].dcfclk_mhz;
		pipes[0].clks_cfg.socclk_mhz = context->bw_ctx.dml.soc.clock_limits[1].socclk_mhz;
	}
	context->bw_ctx.bw.dcn.watermarks.b.urgent_ns = get_wm_urgent(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.b.cstate_pstate.cstate_enter_plus_exit_ns = get_wm_stutter_enter_exit(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.b.cstate_pstate.cstate_exit_ns = get_wm_stutter_exit(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.b.cstate_pstate.pstate_change_ns = get_wm_dram_clock_change(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.b.pte_meta_urgent_ns = get_wm_memory_trip(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.b.frac_urg_bw_nom = get_fraction_of_urgent_bandwidth(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.b.frac_urg_bw_flip = get_fraction_of_urgent_bandwidth_imm_flip(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.b.urgent_latency_ns = get_urgent_latency(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;

	if (vlevel < 2) {
		pipes[0].clks_cfg.voltage = 2;
		pipes[0].clks_cfg.dcfclk_mhz = context->bw_ctx.dml.soc.clock_limits[2].dcfclk_mhz;
		pipes[0].clks_cfg.socclk_mhz = context->bw_ctx.dml.soc.clock_limits[2].socclk_mhz;
	}
	context->bw_ctx.bw.dcn.watermarks.c.urgent_ns = get_wm_urgent(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.c.cstate_pstate.cstate_enter_plus_exit_ns = get_wm_stutter_enter_exit(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.c.cstate_pstate.cstate_exit_ns = get_wm_stutter_exit(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.c.cstate_pstate.pstate_change_ns = get_wm_dram_clock_change(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.c.pte_meta_urgent_ns = get_wm_memory_trip(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.c.frac_urg_bw_nom = get_fraction_of_urgent_bandwidth(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.c.frac_urg_bw_flip = get_fraction_of_urgent_bandwidth_imm_flip(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;

	if (vlevel < 3) {
		pipes[0].clks_cfg.voltage = 3;
		pipes[0].clks_cfg.dcfclk_mhz = context->bw_ctx.dml.soc.clock_limits[2].dcfclk_mhz;
		pipes[0].clks_cfg.socclk_mhz = context->bw_ctx.dml.soc.clock_limits[2].socclk_mhz;
	}
	context->bw_ctx.bw.dcn.watermarks.d.urgent_ns = get_wm_urgent(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.d.cstate_pstate.cstate_enter_plus_exit_ns = get_wm_stutter_enter_exit(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.d.cstate_pstate.cstate_exit_ns = get_wm_stutter_exit(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.d.cstate_pstate.pstate_change_ns = get_wm_dram_clock_change(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.d.pte_meta_urgent_ns = get_wm_memory_trip(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.d.frac_urg_bw_nom = get_fraction_of_urgent_bandwidth(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.d.frac_urg_bw_flip = get_fraction_of_urgent_bandwidth_imm_flip(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;

	pipes[0].clks_cfg.voltage = vlevel;
	pipes[0].clks_cfg.dcfclk_mhz = context->bw_ctx.dml.soc.clock_limits[vlevel].dcfclk_mhz;
	pipes[0].clks_cfg.socclk_mhz = context->bw_ctx.dml.soc.clock_limits[vlevel].socclk_mhz;
	context->bw_ctx.bw.dcn.watermarks.a.urgent_ns = get_wm_urgent(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.a.cstate_pstate.cstate_enter_plus_exit_ns = get_wm_stutter_enter_exit(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.a.cstate_pstate.cstate_exit_ns = get_wm_stutter_exit(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.a.cstate_pstate.pstate_change_ns = get_wm_dram_clock_change(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.a.pte_meta_urgent_ns = get_wm_memory_trip(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.a.frac_urg_bw_nom = get_fraction_of_urgent_bandwidth(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.a.frac_urg_bw_flip = get_fraction_of_urgent_bandwidth_imm_flip(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
}

void dcn20_calculate_dlg_params(
		struct dc *dc, struct dc_state *context,
		display_e2e_pipe_params_st *pipes,
		int pipe_cnt,
		int vlevel)
{
	int i, j, pipe_idx, pipe_idx_unsplit;
	bool visited[MAX_PIPES] = { 0 };

	/* Writeback MCIF_WB arbitration parameters */
	dc->res_pool->funcs->set_mcif_arb_params(dc, context, pipes, pipe_cnt);

	context->bw_ctx.bw.dcn.clk.dispclk_khz = context->bw_ctx.dml.vba.DISPCLK * 1000;
	context->bw_ctx.bw.dcn.clk.dcfclk_khz = context->bw_ctx.dml.vba.DCFCLK * 1000;
	context->bw_ctx.bw.dcn.clk.socclk_khz = context->bw_ctx.dml.vba.SOCCLK * 1000;
	context->bw_ctx.bw.dcn.clk.dramclk_khz = context->bw_ctx.dml.vba.DRAMSpeed * 1000 / 16;
	context->bw_ctx.bw.dcn.clk.dcfclk_deep_sleep_khz = context->bw_ctx.dml.vba.DCFCLKDeepSleep * 1000;
	context->bw_ctx.bw.dcn.clk.fclk_khz = context->bw_ctx.dml.vba.FabricClock * 1000;
	context->bw_ctx.bw.dcn.clk.p_state_change_support =
		context->bw_ctx.dml.vba.DRAMClockChangeSupport[vlevel][context->bw_ctx.dml.vba.maxMpcComb]
							!= dm_dram_clock_change_unsupported;
	context->bw_ctx.bw.dcn.clk.dppclk_khz = 0;

	if (context->bw_ctx.bw.dcn.clk.dispclk_khz < dc->debug.min_disp_clk_khz)
		context->bw_ctx.bw.dcn.clk.dispclk_khz = dc->debug.min_disp_clk_khz;

	/*
	 * An artifact of dml pipe split/odm is that pipes get merged back together for
	 * calculation. Therefore we need to only extract for first pipe in ascending index order
	 * and copy into the other split half.
	 */
	for (i = 0, pipe_idx = 0, pipe_idx_unsplit = 0; i < dc->res_pool->pipe_count; i++) {
		if (!context->res_ctx.pipe_ctx[i].stream)
			continue;

		if (!visited[pipe_idx]) {
			display_pipe_source_params_st *src = &pipes[pipe_idx].pipe.src;
			display_pipe_dest_params_st *dst = &pipes[pipe_idx].pipe.dest;

			dst->vstartup_start = context->bw_ctx.dml.vba.VStartup[pipe_idx_unsplit];
			dst->vupdate_offset = context->bw_ctx.dml.vba.VUpdateOffsetPix[pipe_idx_unsplit];
			dst->vupdate_width = context->bw_ctx.dml.vba.VUpdateWidthPix[pipe_idx_unsplit];
			dst->vready_offset = context->bw_ctx.dml.vba.VReadyOffsetPix[pipe_idx_unsplit];
			/*
			 * j iterates inside pipes array, unlike i which iterates inside
			 * pipe_ctx array
			 */
			if (src->is_hsplit)
				for (j = pipe_idx + 1; j < pipe_cnt; j++) {
					display_pipe_source_params_st *src_j = &pipes[j].pipe.src;
					display_pipe_dest_params_st *dst_j = &pipes[j].pipe.dest;

					if (src_j->is_hsplit && !visited[j]
							&& src->hsplit_grp == src_j->hsplit_grp) {
						dst_j->vstartup_start = context->bw_ctx.dml.vba.VStartup[pipe_idx_unsplit];
						dst_j->vupdate_offset = context->bw_ctx.dml.vba.VUpdateOffsetPix[pipe_idx_unsplit];
						dst_j->vupdate_width = context->bw_ctx.dml.vba.VUpdateWidthPix[pipe_idx_unsplit];
						dst_j->vready_offset = context->bw_ctx.dml.vba.VReadyOffsetPix[pipe_idx_unsplit];
						visited[j] = true;
					}
				}
			visited[pipe_idx] = true;
			pipe_idx_unsplit++;
		}
		pipe_idx++;
	}

	for (i = 0, pipe_idx = 0; i < dc->res_pool->pipe_count; i++) {
		if (!context->res_ctx.pipe_ctx[i].stream)
			continue;
		if (context->bw_ctx.bw.dcn.clk.dppclk_khz < pipes[pipe_idx].clks_cfg.dppclk_mhz * 1000)
			context->bw_ctx.bw.dcn.clk.dppclk_khz = pipes[pipe_idx].clks_cfg.dppclk_mhz * 1000;
		context->res_ctx.pipe_ctx[i].plane_res.bw.dppclk_khz =
						pipes[pipe_idx].clks_cfg.dppclk_mhz * 1000;
		ASSERT(visited[pipe_idx]);
		context->res_ctx.pipe_ctx[i].pipe_dlg_param = pipes[pipe_idx].pipe.dest;
		pipe_idx++;
	}
	/*save a original dppclock copy*/
	context->bw_ctx.bw.dcn.clk.bw_dppclk_khz = context->bw_ctx.bw.dcn.clk.dppclk_khz;
	context->bw_ctx.bw.dcn.clk.bw_dispclk_khz = context->bw_ctx.bw.dcn.clk.dispclk_khz;
	context->bw_ctx.bw.dcn.clk.max_supported_dppclk_khz = context->bw_ctx.dml.soc.clock_limits[vlevel].dppclk_mhz * 1000;
	context->bw_ctx.bw.dcn.clk.max_supported_dispclk_khz = context->bw_ctx.dml.soc.clock_limits[vlevel].dispclk_mhz * 1000;

	for (i = 0, pipe_idx = 0; i < dc->res_pool->pipe_count; i++) {
		bool cstate_en = context->bw_ctx.dml.vba.PrefetchMode[vlevel][context->bw_ctx.dml.vba.maxMpcComb] != 2;

		if (!context->res_ctx.pipe_ctx[i].stream)
			continue;

		context->bw_ctx.dml.funcs.rq_dlg_get_dlg_reg(&context->bw_ctx.dml,
				&context->res_ctx.pipe_ctx[i].dlg_regs,
				&context->res_ctx.pipe_ctx[i].ttu_regs,
				pipes,
				pipe_cnt,
				pipe_idx,
				cstate_en,
				context->bw_ctx.bw.dcn.clk.p_state_change_support,
				false, false, false);

		context->bw_ctx.dml.funcs.rq_dlg_get_rq_reg(&context->bw_ctx.dml,
				&context->res_ctx.pipe_ctx[i].rq_regs,
				pipes[pipe_idx].pipe);
		pipe_idx++;
	}
}

static bool dcn20_validate_bandwidth_internal(struct dc *dc, struct dc_state *context,
		bool fast_validate)
{
	bool out = false;

	BW_VAL_TRACE_SETUP();

	int vlevel = 0;
	int pipe_split_from[MAX_PIPES];
	int pipe_cnt = 0;
	display_e2e_pipe_params_st *pipes = kzalloc(dc->res_pool->pipe_count * sizeof(display_e2e_pipe_params_st), GFP_KERNEL);
	DC_LOGGER_INIT(dc->ctx->logger);

	BW_VAL_TRACE_COUNT();

	out = dcn20_fast_validate_bw(dc, context, pipes, &pipe_cnt, pipe_split_from, &vlevel);

	if (pipe_cnt == 0)
		goto validate_out;

	if (!out)
		goto validate_fail;

	BW_VAL_TRACE_END_VOLTAGE_LEVEL();

	if (fast_validate) {
		BW_VAL_TRACE_SKIP(fast);
		goto validate_out;
	}

	dcn20_calculate_wm(dc, context, pipes, &pipe_cnt, pipe_split_from, vlevel);
	dcn20_calculate_dlg_params(dc, context, pipes, pipe_cnt, vlevel);

	BW_VAL_TRACE_END_WATERMARKS();

	goto validate_out;

validate_fail:
	DC_LOG_WARNING("Mode Validation Warning: %s failed validation.\n",
		dml_get_status_message(context->bw_ctx.dml.vba.ValidationStatus[context->bw_ctx.dml.vba.soc.num_states]));

	BW_VAL_TRACE_SKIP(fail);
	out = false;

validate_out:
	kfree(pipes);

	BW_VAL_TRACE_FINISH();

	return out;
}


bool dcn20_validate_bandwidth(struct dc *dc, struct dc_state *context,
		bool fast_validate)
{
	bool voltage_supported = false;
	bool full_pstate_supported = false;
	bool dummy_pstate_supported = false;
	double p_state_latency_us;

	DC_FP_START();
	p_state_latency_us = context->bw_ctx.dml.soc.dram_clock_change_latency_us;
	context->bw_ctx.dml.soc.disable_dram_clock_change_vactive_support =
		dc->debug.disable_dram_clock_change_vactive_support;

	if (fast_validate) {
		voltage_supported = dcn20_validate_bandwidth_internal(dc, context, true);

		DC_FP_END();
		return voltage_supported;
	}

	// Best case, we support full UCLK switch latency
	voltage_supported = dcn20_validate_bandwidth_internal(dc, context, false);
	full_pstate_supported = context->bw_ctx.bw.dcn.clk.p_state_change_support;

	if (context->bw_ctx.dml.soc.dummy_pstate_latency_us == 0 ||
		(voltage_supported && full_pstate_supported)) {
		context->bw_ctx.bw.dcn.clk.p_state_change_support = full_pstate_supported;
		goto restore_dml_state;
	}

	// Fallback: Try to only support G6 temperature read latency
	context->bw_ctx.dml.soc.dram_clock_change_latency_us = context->bw_ctx.dml.soc.dummy_pstate_latency_us;

	voltage_supported = dcn20_validate_bandwidth_internal(dc, context, false);
	dummy_pstate_supported = context->bw_ctx.bw.dcn.clk.p_state_change_support;

	if (voltage_supported && dummy_pstate_supported) {
		context->bw_ctx.bw.dcn.clk.p_state_change_support = false;
		goto restore_dml_state;
	}

	// ERROR: fallback is supposed to always work.
	ASSERT(false);

restore_dml_state:
	context->bw_ctx.dml.soc.dram_clock_change_latency_us = p_state_latency_us;

	DC_FP_END();
	return voltage_supported;
}

struct pipe_ctx *dcn20_acquire_idle_pipe_for_layer(
		struct dc_state *state,
		const struct resource_pool *pool,
		struct dc_stream_state *stream)
{
	struct resource_context *res_ctx = &state->res_ctx;
	struct pipe_ctx *head_pipe = resource_get_head_pipe_for_stream(res_ctx, stream);
	struct pipe_ctx *idle_pipe = find_idle_secondary_pipe(res_ctx, pool, head_pipe);

	if (!head_pipe)
		ASSERT(0);

	if (!idle_pipe)
		return NULL;

	idle_pipe->stream = head_pipe->stream;
	idle_pipe->stream_res.tg = head_pipe->stream_res.tg;
	idle_pipe->stream_res.opp = head_pipe->stream_res.opp;

	idle_pipe->plane_res.hubp = pool->hubps[idle_pipe->pipe_idx];
	idle_pipe->plane_res.ipp = pool->ipps[idle_pipe->pipe_idx];
	idle_pipe->plane_res.dpp = pool->dpps[idle_pipe->pipe_idx];
	idle_pipe->plane_res.mpcc_inst = pool->dpps[idle_pipe->pipe_idx]->inst;

	return idle_pipe;
}

bool dcn20_get_dcc_compression_cap(const struct dc *dc,
		const struct dc_dcc_surface_param *input,
		struct dc_surface_dcc_cap *output)
{
	return dc->res_pool->hubbub->funcs->get_dcc_compression_cap(
			dc->res_pool->hubbub,
			input,
			output);
}

static void dcn20_destroy_resource_pool(struct resource_pool **pool)
{
	struct dcn20_resource_pool *dcn20_pool = TO_DCN20_RES_POOL(*pool);

	dcn20_resource_destruct(dcn20_pool);
	kfree(dcn20_pool);
	*pool = NULL;
}


static struct dc_cap_funcs cap_funcs = {
	.get_dcc_compression_cap = dcn20_get_dcc_compression_cap
};


enum dc_status dcn20_patch_unknown_plane_state(struct dc_plane_state *plane_state)
{
	enum dc_status result = DC_OK;

	enum surface_pixel_format surf_pix_format = plane_state->format;
	unsigned int bpp = resource_pixel_format_to_bpp(surf_pix_format);

	enum swizzle_mode_values swizzle = DC_SW_LINEAR;

	if (bpp == 64)
		swizzle = DC_SW_64KB_D;
	else
		swizzle = DC_SW_64KB_S;

	plane_state->tiling_info.gfx9.swizzle = swizzle;
	return result;
}

static struct resource_funcs dcn20_res_pool_funcs = {
	.destroy = dcn20_destroy_resource_pool,
	.link_enc_create = dcn20_link_encoder_create,
	.validate_bandwidth = dcn20_validate_bandwidth,
	.acquire_idle_pipe_for_layer = dcn20_acquire_idle_pipe_for_layer,
	.add_stream_to_ctx = dcn20_add_stream_to_ctx,
	.remove_stream_from_ctx = dcn20_remove_stream_from_ctx,
	.populate_dml_writeback_from_context = dcn20_populate_dml_writeback_from_context,
	.patch_unknown_plane_state = dcn20_patch_unknown_plane_state,
	.set_mcif_arb_params = dcn20_set_mcif_arb_params,
	.populate_dml_pipes = dcn20_populate_dml_pipes_from_context,
	.find_first_free_match_stream_enc_for_link = dcn10_find_first_free_match_stream_enc_for_link
};

bool dcn20_dwbc_create(struct dc_context *ctx, struct resource_pool *pool)
{
	int i;
	uint32_t pipe_count = pool->res_cap->num_dwb;

	for (i = 0; i < pipe_count; i++) {
		struct dcn20_dwbc *dwbc20 = kzalloc(sizeof(struct dcn20_dwbc),
						    GFP_KERNEL);

		if (!dwbc20) {
			dm_error("DC: failed to create dwbc20!\n");
			return false;
		}
		dcn20_dwbc_construct(dwbc20, ctx,
				&dwbc20_regs[i],
				&dwbc20_shift,
				&dwbc20_mask,
				i);
		pool->dwbc[i] = &dwbc20->base;
	}
	return true;
}

bool dcn20_mmhubbub_create(struct dc_context *ctx, struct resource_pool *pool)
{
	int i;
	uint32_t pipe_count = pool->res_cap->num_dwb;

	ASSERT(pipe_count > 0);

	for (i = 0; i < pipe_count; i++) {
		struct dcn20_mmhubbub *mcif_wb20 = kzalloc(sizeof(struct dcn20_mmhubbub),
						    GFP_KERNEL);

		if (!mcif_wb20) {
			dm_error("DC: failed to create mcif_wb20!\n");
			return false;
		}

		dcn20_mmhubbub_construct(mcif_wb20, ctx,
				&mcif_wb20_regs[i],
				&mcif_wb20_shift,
				&mcif_wb20_mask,
				i);

		pool->mcif_wb[i] = &mcif_wb20->base;
	}
	return true;
}

static struct pp_smu_funcs *dcn20_pp_smu_create(struct dc_context *ctx)
{
	struct pp_smu_funcs *pp_smu = kzalloc(sizeof(*pp_smu), GFP_KERNEL);

	if (!pp_smu)
		return pp_smu;

	dm_pp_get_funcs(ctx, pp_smu);

	if (pp_smu->ctx.ver != PP_SMU_VER_NV)
		pp_smu = memset(pp_smu, 0, sizeof(struct pp_smu_funcs));

	return pp_smu;
}

static void dcn20_pp_smu_destroy(struct pp_smu_funcs **pp_smu)
{
	if (pp_smu && *pp_smu) {
		kfree(*pp_smu);
		*pp_smu = NULL;
	}
}

void dcn20_cap_soc_clocks(
		struct _vcs_dpi_soc_bounding_box_st *bb,
		struct pp_smu_nv_clock_table max_clocks)
{
	int i;

	// First pass - cap all clocks higher than the reported max
	for (i = 0; i < bb->num_states; i++) {
		if ((bb->clock_limits[i].dcfclk_mhz > (max_clocks.dcfClockInKhz / 1000))
				&& max_clocks.dcfClockInKhz != 0)
			bb->clock_limits[i].dcfclk_mhz = (max_clocks.dcfClockInKhz / 1000);

		if ((bb->clock_limits[i].dram_speed_mts > (max_clocks.uClockInKhz / 1000) * 16)
						&& max_clocks.uClockInKhz != 0)
			bb->clock_limits[i].dram_speed_mts = (max_clocks.uClockInKhz / 1000) * 16;

		if ((bb->clock_limits[i].fabricclk_mhz > (max_clocks.fabricClockInKhz / 1000))
						&& max_clocks.fabricClockInKhz != 0)
			bb->clock_limits[i].fabricclk_mhz = (max_clocks.fabricClockInKhz / 1000);

		if ((bb->clock_limits[i].dispclk_mhz > (max_clocks.displayClockInKhz / 1000))
						&& max_clocks.displayClockInKhz != 0)
			bb->clock_limits[i].dispclk_mhz = (max_clocks.displayClockInKhz / 1000);

		if ((bb->clock_limits[i].dppclk_mhz > (max_clocks.dppClockInKhz / 1000))
						&& max_clocks.dppClockInKhz != 0)
			bb->clock_limits[i].dppclk_mhz = (max_clocks.dppClockInKhz / 1000);

		if ((bb->clock_limits[i].phyclk_mhz > (max_clocks.phyClockInKhz / 1000))
						&& max_clocks.phyClockInKhz != 0)
			bb->clock_limits[i].phyclk_mhz = (max_clocks.phyClockInKhz / 1000);

		if ((bb->clock_limits[i].socclk_mhz > (max_clocks.socClockInKhz / 1000))
						&& max_clocks.socClockInKhz != 0)
			bb->clock_limits[i].socclk_mhz = (max_clocks.socClockInKhz / 1000);

		if ((bb->clock_limits[i].dscclk_mhz > (max_clocks.dscClockInKhz / 1000))
						&& max_clocks.dscClockInKhz != 0)
			bb->clock_limits[i].dscclk_mhz = (max_clocks.dscClockInKhz / 1000);
	}

	// Second pass - remove all duplicate clock states
	for (i = bb->num_states - 1; i > 1; i--) {
		bool duplicate = true;

		if (bb->clock_limits[i-1].dcfclk_mhz != bb->clock_limits[i].dcfclk_mhz)
			duplicate = false;
		if (bb->clock_limits[i-1].dispclk_mhz != bb->clock_limits[i].dispclk_mhz)
			duplicate = false;
		if (bb->clock_limits[i-1].dppclk_mhz != bb->clock_limits[i].dppclk_mhz)
			duplicate = false;
		if (bb->clock_limits[i-1].dram_speed_mts != bb->clock_limits[i].dram_speed_mts)
			duplicate = false;
		if (bb->clock_limits[i-1].dscclk_mhz != bb->clock_limits[i].dscclk_mhz)
			duplicate = false;
		if (bb->clock_limits[i-1].fabricclk_mhz != bb->clock_limits[i].fabricclk_mhz)
			duplicate = false;
		if (bb->clock_limits[i-1].phyclk_mhz != bb->clock_limits[i].phyclk_mhz)
			duplicate = false;
		if (bb->clock_limits[i-1].socclk_mhz != bb->clock_limits[i].socclk_mhz)
			duplicate = false;

		if (duplicate)
			bb->num_states--;
	}
}

void dcn20_update_bounding_box(struct dc *dc, struct _vcs_dpi_soc_bounding_box_st *bb,
		struct pp_smu_nv_clock_table *max_clocks, unsigned int *uclk_states, unsigned int num_states)
{
	struct _vcs_dpi_voltage_scaling_st calculated_states[MAX_CLOCK_LIMIT_STATES];
	int i;
	int num_calculated_states = 0;
	int min_dcfclk = 0;

	if (num_states == 0)
		return;

	memset(calculated_states, 0, sizeof(calculated_states));

	if (dc->bb_overrides.min_dcfclk_mhz > 0)
		min_dcfclk = dc->bb_overrides.min_dcfclk_mhz;
	else {
		if (ASICREV_IS_NAVI12_P(dc->ctx->asic_id.hw_internal_rev))
			min_dcfclk = 310;
		else
			// Accounting for SOC/DCF relationship, we can go as high as
			// 506Mhz in Vmin.
			min_dcfclk = 506;
	}

	for (i = 0; i < num_states; i++) {
		int min_fclk_required_by_uclk;
		calculated_states[i].state = i;
		calculated_states[i].dram_speed_mts = uclk_states[i] * 16 / 1000;

		// FCLK:UCLK ratio is 1.08
		min_fclk_required_by_uclk = mul_u64_u32_shr(BIT_ULL(32) * 1080 / 1000000, uclk_states[i], 32);

		calculated_states[i].fabricclk_mhz = (min_fclk_required_by_uclk < min_dcfclk) ?
				min_dcfclk : min_fclk_required_by_uclk;

		calculated_states[i].socclk_mhz = (calculated_states[i].fabricclk_mhz > max_clocks->socClockInKhz / 1000) ?
				max_clocks->socClockInKhz / 1000 : calculated_states[i].fabricclk_mhz;

		calculated_states[i].dcfclk_mhz = (calculated_states[i].fabricclk_mhz > max_clocks->dcfClockInKhz / 1000) ?
				max_clocks->dcfClockInKhz / 1000 : calculated_states[i].fabricclk_mhz;

		calculated_states[i].dispclk_mhz = max_clocks->displayClockInKhz / 1000;
		calculated_states[i].dppclk_mhz = max_clocks->displayClockInKhz / 1000;
		calculated_states[i].dscclk_mhz = max_clocks->displayClockInKhz / (1000 * 3);

		calculated_states[i].phyclk_mhz = max_clocks->phyClockInKhz / 1000;

		num_calculated_states++;
	}

	calculated_states[num_calculated_states - 1].socclk_mhz = max_clocks->socClockInKhz / 1000;
	calculated_states[num_calculated_states - 1].fabricclk_mhz = max_clocks->socClockInKhz / 1000;
	calculated_states[num_calculated_states - 1].dcfclk_mhz = max_clocks->dcfClockInKhz / 1000;

	memcpy(bb->clock_limits, calculated_states, sizeof(bb->clock_limits));
	bb->num_states = num_calculated_states;

	// Duplicate the last state, DML always an extra state identical to max state to work
	memcpy(&bb->clock_limits[num_calculated_states], &bb->clock_limits[num_calculated_states - 1], sizeof(struct _vcs_dpi_voltage_scaling_st));
	bb->clock_limits[num_calculated_states].state = bb->num_states;
}

void dcn20_patch_bounding_box(struct dc *dc, struct _vcs_dpi_soc_bounding_box_st *bb)
{
	if ((int)(bb->sr_exit_time_us * 1000) != dc->bb_overrides.sr_exit_time_ns
			&& dc->bb_overrides.sr_exit_time_ns) {
		bb->sr_exit_time_us = dc->bb_overrides.sr_exit_time_ns / 1000.0;
	}

	if ((int)(bb->sr_enter_plus_exit_time_us * 1000)
				!= dc->bb_overrides.sr_enter_plus_exit_time_ns
			&& dc->bb_overrides.sr_enter_plus_exit_time_ns) {
		bb->sr_enter_plus_exit_time_us =
				dc->bb_overrides.sr_enter_plus_exit_time_ns / 1000.0;
	}

	if ((int)(bb->urgent_latency_us * 1000) != dc->bb_overrides.urgent_latency_ns
			&& dc->bb_overrides.urgent_latency_ns) {
		bb->urgent_latency_us = dc->bb_overrides.urgent_latency_ns / 1000.0;
	}

	if ((int)(bb->dram_clock_change_latency_us * 1000)
				!= dc->bb_overrides.dram_clock_change_latency_ns
			&& dc->bb_overrides.dram_clock_change_latency_ns) {
		bb->dram_clock_change_latency_us =
				dc->bb_overrides.dram_clock_change_latency_ns / 1000.0;
	}
}

static struct _vcs_dpi_soc_bounding_box_st *get_asic_rev_soc_bb(
	uint32_t hw_internal_rev)
{
	if (ASICREV_IS_NAVI14_M(hw_internal_rev))
		return &dcn2_0_nv14_soc;

	if (ASICREV_IS_NAVI12_P(hw_internal_rev))
		return &dcn2_0_nv12_soc;

	return &dcn2_0_soc;
}

static struct _vcs_dpi_ip_params_st *get_asic_rev_ip_params(
	uint32_t hw_internal_rev)
{
	/* NV14 */
	if (ASICREV_IS_NAVI14_M(hw_internal_rev))
		return &dcn2_0_nv14_ip;

	/* NV12 and NV10 */
	return &dcn2_0_ip;
}

static enum dml_project get_dml_project_version(uint32_t hw_internal_rev)
{
	return DML_PROJECT_NAVI10v2;
}

#define fixed16_to_double(x) (((double) x) / ((double) (1 << 16)))
#define fixed16_to_double_to_cpu(x) fixed16_to_double(le32_to_cpu(x))

static bool init_soc_bounding_box(struct dc *dc,
				  struct dcn20_resource_pool *pool)
{
	const struct gpu_info_soc_bounding_box_v1_0 *bb = dc->soc_bounding_box;
	struct _vcs_dpi_soc_bounding_box_st *loaded_bb =
			get_asic_rev_soc_bb(dc->ctx->asic_id.hw_internal_rev);
	struct _vcs_dpi_ip_params_st *loaded_ip =
			get_asic_rev_ip_params(dc->ctx->asic_id.hw_internal_rev);

	DC_LOGGER_INIT(dc->ctx->logger);

	/* TODO: upstream NV12 bounding box when its launched */
	if (!bb && ASICREV_IS_NAVI12_P(dc->ctx->asic_id.hw_internal_rev)) {
		DC_LOG_ERROR("%s: not valid soc bounding box/n", __func__);
		return false;
	}

	if (bb && ASICREV_IS_NAVI12_P(dc->ctx->asic_id.hw_internal_rev)) {
		int i;

		dcn2_0_nv12_soc.sr_exit_time_us =
				fixed16_to_double_to_cpu(bb->sr_exit_time_us);
		dcn2_0_nv12_soc.sr_enter_plus_exit_time_us =
				fixed16_to_double_to_cpu(bb->sr_enter_plus_exit_time_us);
		dcn2_0_nv12_soc.urgent_latency_us =
				fixed16_to_double_to_cpu(bb->urgent_latency_us);
		dcn2_0_nv12_soc.urgent_latency_pixel_data_only_us =
				fixed16_to_double_to_cpu(bb->urgent_latency_pixel_data_only_us);
		dcn2_0_nv12_soc.urgent_latency_pixel_mixed_with_vm_data_us =
				fixed16_to_double_to_cpu(bb->urgent_latency_pixel_mixed_with_vm_data_us);
		dcn2_0_nv12_soc.urgent_latency_vm_data_only_us =
				fixed16_to_double_to_cpu(bb->urgent_latency_vm_data_only_us);
		dcn2_0_nv12_soc.urgent_out_of_order_return_per_channel_pixel_only_bytes =
				le32_to_cpu(bb->urgent_out_of_order_return_per_channel_pixel_only_bytes);
		dcn2_0_nv12_soc.urgent_out_of_order_return_per_channel_pixel_and_vm_bytes =
				le32_to_cpu(bb->urgent_out_of_order_return_per_channel_pixel_and_vm_bytes);
		dcn2_0_nv12_soc.urgent_out_of_order_return_per_channel_vm_only_bytes =
				le32_to_cpu(bb->urgent_out_of_order_return_per_channel_vm_only_bytes);
		dcn2_0_nv12_soc.pct_ideal_dram_sdp_bw_after_urgent_pixel_only =
				fixed16_to_double_to_cpu(bb->pct_ideal_dram_sdp_bw_after_urgent_pixel_only);
		dcn2_0_nv12_soc.pct_ideal_dram_sdp_bw_after_urgent_pixel_and_vm =
				fixed16_to_double_to_cpu(bb->pct_ideal_dram_sdp_bw_after_urgent_pixel_and_vm);
		dcn2_0_nv12_soc.pct_ideal_dram_sdp_bw_after_urgent_vm_only =
				fixed16_to_double_to_cpu(bb->pct_ideal_dram_sdp_bw_after_urgent_vm_only);
		dcn2_0_nv12_soc.max_avg_sdp_bw_use_normal_percent =
				fixed16_to_double_to_cpu(bb->max_avg_sdp_bw_use_normal_percent);
		dcn2_0_nv12_soc.max_avg_dram_bw_use_normal_percent =
				fixed16_to_double_to_cpu(bb->max_avg_dram_bw_use_normal_percent);
		dcn2_0_nv12_soc.writeback_latency_us =
				fixed16_to_double_to_cpu(bb->writeback_latency_us);
		dcn2_0_nv12_soc.ideal_dram_bw_after_urgent_percent =
				fixed16_to_double_to_cpu(bb->ideal_dram_bw_after_urgent_percent);
		dcn2_0_nv12_soc.max_request_size_bytes =
				le32_to_cpu(bb->max_request_size_bytes);
		dcn2_0_nv12_soc.dram_channel_width_bytes =
				le32_to_cpu(bb->dram_channel_width_bytes);
		dcn2_0_nv12_soc.fabric_datapath_to_dcn_data_return_bytes =
				le32_to_cpu(bb->fabric_datapath_to_dcn_data_return_bytes);
		dcn2_0_nv12_soc.dcn_downspread_percent =
				fixed16_to_double_to_cpu(bb->dcn_downspread_percent);
		dcn2_0_nv12_soc.downspread_percent =
				fixed16_to_double_to_cpu(bb->downspread_percent);
		dcn2_0_nv12_soc.dram_page_open_time_ns =
				fixed16_to_double_to_cpu(bb->dram_page_open_time_ns);
		dcn2_0_nv12_soc.dram_rw_turnaround_time_ns =
				fixed16_to_double_to_cpu(bb->dram_rw_turnaround_time_ns);
		dcn2_0_nv12_soc.dram_return_buffer_per_channel_bytes =
				le32_to_cpu(bb->dram_return_buffer_per_channel_bytes);
		dcn2_0_nv12_soc.round_trip_ping_latency_dcfclk_cycles =
				le32_to_cpu(bb->round_trip_ping_latency_dcfclk_cycles);
		dcn2_0_nv12_soc.urgent_out_of_order_return_per_channel_bytes =
				le32_to_cpu(bb->urgent_out_of_order_return_per_channel_bytes);
		dcn2_0_nv12_soc.channel_interleave_bytes =
				le32_to_cpu(bb->channel_interleave_bytes);
		dcn2_0_nv12_soc.num_banks =
				le32_to_cpu(bb->num_banks);
		dcn2_0_nv12_soc.num_chans =
				le32_to_cpu(bb->num_chans);
		dcn2_0_nv12_soc.vmm_page_size_bytes =
				le32_to_cpu(bb->vmm_page_size_bytes);
		dcn2_0_nv12_soc.dram_clock_change_latency_us =
				fixed16_to_double_to_cpu(bb->dram_clock_change_latency_us);
		// HACK!! Lower uclock latency switch time so we don't switch
		dcn2_0_nv12_soc.dram_clock_change_latency_us = 10;
		dcn2_0_nv12_soc.writeback_dram_clock_change_latency_us =
				fixed16_to_double_to_cpu(bb->writeback_dram_clock_change_latency_us);
		dcn2_0_nv12_soc.return_bus_width_bytes =
				le32_to_cpu(bb->return_bus_width_bytes);
		dcn2_0_nv12_soc.dispclk_dppclk_vco_speed_mhz =
				le32_to_cpu(bb->dispclk_dppclk_vco_speed_mhz);
		dcn2_0_nv12_soc.xfc_bus_transport_time_us =
				le32_to_cpu(bb->xfc_bus_transport_time_us);
		dcn2_0_nv12_soc.xfc_xbuf_latency_tolerance_us =
				le32_to_cpu(bb->xfc_xbuf_latency_tolerance_us);
		dcn2_0_nv12_soc.use_urgent_burst_bw =
				le32_to_cpu(bb->use_urgent_burst_bw);
		dcn2_0_nv12_soc.num_states =
				le32_to_cpu(bb->num_states);

		for (i = 0; i < dcn2_0_nv12_soc.num_states; i++) {
			dcn2_0_nv12_soc.clock_limits[i].state =
					le32_to_cpu(bb->clock_limits[i].state);
			dcn2_0_nv12_soc.clock_limits[i].dcfclk_mhz =
					fixed16_to_double_to_cpu(bb->clock_limits[i].dcfclk_mhz);
			dcn2_0_nv12_soc.clock_limits[i].fabricclk_mhz =
					fixed16_to_double_to_cpu(bb->clock_limits[i].fabricclk_mhz);
			dcn2_0_nv12_soc.clock_limits[i].dispclk_mhz =
					fixed16_to_double_to_cpu(bb->clock_limits[i].dispclk_mhz);
			dcn2_0_nv12_soc.clock_limits[i].dppclk_mhz =
					fixed16_to_double_to_cpu(bb->clock_limits[i].dppclk_mhz);
			dcn2_0_nv12_soc.clock_limits[i].phyclk_mhz =
					fixed16_to_double_to_cpu(bb->clock_limits[i].phyclk_mhz);
			dcn2_0_nv12_soc.clock_limits[i].socclk_mhz =
					fixed16_to_double_to_cpu(bb->clock_limits[i].socclk_mhz);
			dcn2_0_nv12_soc.clock_limits[i].dscclk_mhz =
					fixed16_to_double_to_cpu(bb->clock_limits[i].dscclk_mhz);
			dcn2_0_nv12_soc.clock_limits[i].dram_speed_mts =
					fixed16_to_double_to_cpu(bb->clock_limits[i].dram_speed_mts);
		}
	}

	if (pool->base.pp_smu) {
		struct pp_smu_nv_clock_table max_clocks = {0};
		unsigned int uclk_states[8] = {0};
		unsigned int num_states = 0;
		enum pp_smu_status status;
		bool clock_limits_available = false;
		bool uclk_states_available = false;

		if (pool->base.pp_smu->nv_funcs.get_uclk_dpm_states) {
			status = (pool->base.pp_smu->nv_funcs.get_uclk_dpm_states)
				(&pool->base.pp_smu->nv_funcs.pp_smu, uclk_states, &num_states);

			uclk_states_available = (status == PP_SMU_RESULT_OK);
		}

		if (pool->base.pp_smu->nv_funcs.get_maximum_sustainable_clocks) {
			status = (*pool->base.pp_smu->nv_funcs.get_maximum_sustainable_clocks)
					(&pool->base.pp_smu->nv_funcs.pp_smu, &max_clocks);
			/* SMU cannot set DCF clock to anything equal to or higher than SOC clock
			 */
			if (max_clocks.dcfClockInKhz >= max_clocks.socClockInKhz)
				max_clocks.dcfClockInKhz = max_clocks.socClockInKhz - 1000;
			clock_limits_available = (status == PP_SMU_RESULT_OK);
		}

		if (clock_limits_available && uclk_states_available && num_states)
			dcn20_update_bounding_box(dc, loaded_bb, &max_clocks, uclk_states, num_states);
		else if (clock_limits_available)
			dcn20_cap_soc_clocks(loaded_bb, max_clocks);
	}

	loaded_ip->max_num_otg = pool->base.res_cap->num_timing_generator;
	loaded_ip->max_num_dpp = pool->base.pipe_count;
	dcn20_patch_bounding_box(dc, loaded_bb);

	return true;
}

static bool dcn20_resource_construct(
	uint8_t num_virtual_links,
	struct dc *dc,
	struct dcn20_resource_pool *pool)
{
	int i;
	struct dc_context *ctx = dc->ctx;
	struct irq_service_init_data init_data;
	struct ddc_service_init_data ddc_init_data;
	struct _vcs_dpi_soc_bounding_box_st *loaded_bb =
			get_asic_rev_soc_bb(ctx->asic_id.hw_internal_rev);
	struct _vcs_dpi_ip_params_st *loaded_ip =
			get_asic_rev_ip_params(ctx->asic_id.hw_internal_rev);
	enum dml_project dml_project_version =
			get_dml_project_version(ctx->asic_id.hw_internal_rev);

	DC_FP_START();

	ctx->dc_bios->regs = &bios_regs;
	pool->base.funcs = &dcn20_res_pool_funcs;

	if (ASICREV_IS_NAVI14_M(ctx->asic_id.hw_internal_rev)) {
		pool->base.res_cap = &res_cap_nv14;
		pool->base.pipe_count = 5;
		pool->base.mpcc_count = 5;
	} else {
		pool->base.res_cap = &res_cap_nv10;
		pool->base.pipe_count = 6;
		pool->base.mpcc_count = 6;
	}
	/*************************************************
	 *  Resource + asic cap harcoding                *
	 *************************************************/
	pool->base.underlay_pipe_index = NO_UNDERLAY_PIPE;

	dc->caps.max_downscale_ratio = 200;
	dc->caps.i2c_speed_in_khz = 100;
	dc->caps.max_cursor_size = 256;
	dc->caps.dmdata_alloc_size = 2048;

	dc->caps.max_slave_planes = 1;
	dc->caps.post_blend_color_processing = true;
	dc->caps.force_dp_tps4_for_cp2520 = true;
	dc->caps.hw_3d_lut = true;
	dc->caps.extended_aux_timeout_support = true;

	if (dc->ctx->dce_environment == DCE_ENV_PRODUCTION_DRV) {
		dc->debug = debug_defaults_drv;
	} else if (dc->ctx->dce_environment == DCE_ENV_FPGA_MAXIMUS) {
		pool->base.pipe_count = 4;
		pool->base.mpcc_count = pool->base.pipe_count;
		dc->debug = debug_defaults_diags;
	} else {
		dc->debug = debug_defaults_diags;
	}
	//dcn2.0x
	dc->work_arounds.dedcn20_305_wa = true;

	// Init the vm_helper
	if (dc->vm_helper)
		vm_helper_init(dc->vm_helper, 16);

	/*************************************************
	 *  Create resources                             *
	 *************************************************/

	pool->base.clock_sources[DCN20_CLK_SRC_PLL0] =
			dcn20_clock_source_create(ctx, ctx->dc_bios,
				CLOCK_SOURCE_COMBO_PHY_PLL0,
				&clk_src_regs[0], false);
	pool->base.clock_sources[DCN20_CLK_SRC_PLL1] =
			dcn20_clock_source_create(ctx, ctx->dc_bios,
				CLOCK_SOURCE_COMBO_PHY_PLL1,
				&clk_src_regs[1], false);
	pool->base.clock_sources[DCN20_CLK_SRC_PLL2] =
			dcn20_clock_source_create(ctx, ctx->dc_bios,
				CLOCK_SOURCE_COMBO_PHY_PLL2,
				&clk_src_regs[2], false);
	pool->base.clock_sources[DCN20_CLK_SRC_PLL3] =
			dcn20_clock_source_create(ctx, ctx->dc_bios,
				CLOCK_SOURCE_COMBO_PHY_PLL3,
				&clk_src_regs[3], false);
	pool->base.clock_sources[DCN20_CLK_SRC_PLL4] =
			dcn20_clock_source_create(ctx, ctx->dc_bios,
				CLOCK_SOURCE_COMBO_PHY_PLL4,
				&clk_src_regs[4], false);
	pool->base.clock_sources[DCN20_CLK_SRC_PLL5] =
			dcn20_clock_source_create(ctx, ctx->dc_bios,
				CLOCK_SOURCE_COMBO_PHY_PLL5,
				&clk_src_regs[5], false);
	pool->base.clk_src_count = DCN20_CLK_SRC_TOTAL;
	/* todo: not reuse phy_pll registers */
	pool->base.dp_clock_source =
			dcn20_clock_source_create(ctx, ctx->dc_bios,
				CLOCK_SOURCE_ID_DP_DTO,
				&clk_src_regs[0], true);

	for (i = 0; i < pool->base.clk_src_count; i++) {
		if (pool->base.clock_sources[i] == NULL) {
			dm_error("DC: failed to create clock sources!\n");
			BREAK_TO_DEBUGGER();
			goto create_fail;
		}
	}

	pool->base.dccg = dccg2_create(ctx, &dccg_regs, &dccg_shift, &dccg_mask);
	if (pool->base.dccg == NULL) {
		dm_error("DC: failed to create dccg!\n");
		BREAK_TO_DEBUGGER();
		goto create_fail;
	}

	pool->base.dmcu = dcn20_dmcu_create(ctx,
			&dmcu_regs,
			&dmcu_shift,
			&dmcu_mask);
	if (pool->base.dmcu == NULL) {
		dm_error("DC: failed to create dmcu!\n");
		BREAK_TO_DEBUGGER();
		goto create_fail;
	}

	pool->base.abm = dce_abm_create(ctx,
			&abm_regs,
			&abm_shift,
			&abm_mask);
	if (pool->base.abm == NULL) {
		dm_error("DC: failed to create abm!\n");
		BREAK_TO_DEBUGGER();
		goto create_fail;
	}

	pool->base.pp_smu = dcn20_pp_smu_create(ctx);


	if (!init_soc_bounding_box(dc, pool)) {
		dm_error("DC: failed to initialize soc bounding box!\n");
		BREAK_TO_DEBUGGER();
		goto create_fail;
	}

	dml_init_instance(&dc->dml, loaded_bb, loaded_ip, dml_project_version);

	if (!dc->debug.disable_pplib_wm_range) {
		struct pp_smu_wm_range_sets ranges = {0};
		int i = 0;

		ranges.num_reader_wm_sets = 0;

		if (loaded_bb->num_states == 1) {
			ranges.reader_wm_sets[0].wm_inst = i;
			ranges.reader_wm_sets[0].min_drain_clk_mhz = PP_SMU_WM_SET_RANGE_CLK_UNCONSTRAINED_MIN;
			ranges.reader_wm_sets[0].max_drain_clk_mhz = PP_SMU_WM_SET_RANGE_CLK_UNCONSTRAINED_MAX;
			ranges.reader_wm_sets[0].min_fill_clk_mhz = PP_SMU_WM_SET_RANGE_CLK_UNCONSTRAINED_MIN;
			ranges.reader_wm_sets[0].max_fill_clk_mhz = PP_SMU_WM_SET_RANGE_CLK_UNCONSTRAINED_MAX;

			ranges.num_reader_wm_sets = 1;
		} else if (loaded_bb->num_states > 1) {
			for (i = 0; i < 4 && i < loaded_bb->num_states; i++) {
				ranges.reader_wm_sets[i].wm_inst = i;
				ranges.reader_wm_sets[i].min_drain_clk_mhz = PP_SMU_WM_SET_RANGE_CLK_UNCONSTRAINED_MIN;
				ranges.reader_wm_sets[i].max_drain_clk_mhz = PP_SMU_WM_SET_RANGE_CLK_UNCONSTRAINED_MAX;
				ranges.reader_wm_sets[i].min_fill_clk_mhz = (i > 0) ? (loaded_bb->clock_limits[i - 1].dram_speed_mts / 16) + 1 : 0;
				ranges.reader_wm_sets[i].max_fill_clk_mhz = loaded_bb->clock_limits[i].dram_speed_mts / 16;

				ranges.num_reader_wm_sets = i + 1;
			}

			ranges.reader_wm_sets[0].min_fill_clk_mhz = PP_SMU_WM_SET_RANGE_CLK_UNCONSTRAINED_MIN;
			ranges.reader_wm_sets[ranges.num_reader_wm_sets - 1].max_fill_clk_mhz = PP_SMU_WM_SET_RANGE_CLK_UNCONSTRAINED_MAX;
		}

		ranges.num_writer_wm_sets = 1;

		ranges.writer_wm_sets[0].wm_inst = 0;
		ranges.writer_wm_sets[0].min_fill_clk_mhz = PP_SMU_WM_SET_RANGE_CLK_UNCONSTRAINED_MIN;
		ranges.writer_wm_sets[0].max_fill_clk_mhz = PP_SMU_WM_SET_RANGE_CLK_UNCONSTRAINED_MAX;
		ranges.writer_wm_sets[0].min_drain_clk_mhz = PP_SMU_WM_SET_RANGE_CLK_UNCONSTRAINED_MIN;
		ranges.writer_wm_sets[0].max_drain_clk_mhz = PP_SMU_WM_SET_RANGE_CLK_UNCONSTRAINED_MAX;

		/* Notify PP Lib/SMU which Watermarks to use for which clock ranges */
		if (pool->base.pp_smu->nv_funcs.set_wm_ranges)
			pool->base.pp_smu->nv_funcs.set_wm_ranges(&pool->base.pp_smu->nv_funcs.pp_smu, &ranges);
	}

	init_data.ctx = dc->ctx;
	pool->base.irqs = dal_irq_service_dcn20_create(&init_data);
	if (!pool->base.irqs)
		goto create_fail;

	/* mem input -> ipp -> dpp -> opp -> TG */
	for (i = 0; i < pool->base.pipe_count; i++) {
		pool->base.hubps[i] = dcn20_hubp_create(ctx, i);
		if (pool->base.hubps[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC: failed to create memory input!\n");
			goto create_fail;
		}

		pool->base.ipps[i] = dcn20_ipp_create(ctx, i);
		if (pool->base.ipps[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC: failed to create input pixel processor!\n");
			goto create_fail;
		}

		pool->base.dpps[i] = dcn20_dpp_create(ctx, i);
		if (pool->base.dpps[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC: failed to create dpps!\n");
			goto create_fail;
		}
	}
	for (i = 0; i < pool->base.res_cap->num_ddc; i++) {
		pool->base.engines[i] = dcn20_aux_engine_create(ctx, i);
		if (pool->base.engines[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC:failed to create aux engine!!\n");
			goto create_fail;
		}
		pool->base.hw_i2cs[i] = dcn20_i2c_hw_create(ctx, i);
		if (pool->base.hw_i2cs[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC:failed to create hw i2c!!\n");
			goto create_fail;
		}
		pool->base.sw_i2cs[i] = NULL;
	}

	for (i = 0; i < pool->base.res_cap->num_opp; i++) {
		pool->base.opps[i] = dcn20_opp_create(ctx, i);
		if (pool->base.opps[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC: failed to create output pixel processor!\n");
			goto create_fail;
		}
	}

	for (i = 0; i < pool->base.res_cap->num_timing_generator; i++) {
		pool->base.timing_generators[i] = dcn20_timing_generator_create(
				ctx, i);
		if (pool->base.timing_generators[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error("DC: failed to create tg!\n");
			goto create_fail;
		}
	}

	pool->base.timing_generator_count = i;

	pool->base.mpc = dcn20_mpc_create(ctx);
	if (pool->base.mpc == NULL) {
		BREAK_TO_DEBUGGER();
		dm_error("DC: failed to create mpc!\n");
		goto create_fail;
	}

	pool->base.hubbub = dcn20_hubbub_create(ctx);
	if (pool->base.hubbub == NULL) {
		BREAK_TO_DEBUGGER();
		dm_error("DC: failed to create hubbub!\n");
		goto create_fail;
	}

	for (i = 0; i < pool->base.res_cap->num_dsc; i++) {
		pool->base.dscs[i] = dcn20_dsc_create(ctx, i);
		if (pool->base.dscs[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error("DC: failed to create display stream compressor %d!\n", i);
			goto create_fail;
		}
	}

	if (!dcn20_dwbc_create(ctx, &pool->base)) {
		BREAK_TO_DEBUGGER();
		dm_error("DC: failed to create dwbc!\n");
		goto create_fail;
	}
	if (!dcn20_mmhubbub_create(ctx, &pool->base)) {
		BREAK_TO_DEBUGGER();
		dm_error("DC: failed to create mcif_wb!\n");
		goto create_fail;
	}

	if (!resource_construct(num_virtual_links, dc, &pool->base,
			(!IS_FPGA_MAXIMUS_DC(dc->ctx->dce_environment) ?
			&res_create_funcs : &res_create_maximus_funcs)))
			goto create_fail;

	dcn20_hw_sequencer_construct(dc);

	// IF NV12, set PG function pointer to NULL. It's not that
	// PG isn't supported for NV12, it's that we don't want to
	// program the registers because that will cause more power
	// to be consumed. We could have created dcn20_init_hw to get
	// the same effect by checking ASIC rev, but there was a
	// request at some point to not check ASIC rev on hw sequencer.
	if (ASICREV_IS_NAVI12_P(dc->ctx->asic_id.hw_internal_rev))
		dc->hwseq->funcs.enable_power_gating_plane = NULL;

	dc->caps.max_planes =  pool->base.pipe_count;

	for (i = 0; i < dc->caps.max_planes; ++i)
		dc->caps.planes[i] = plane_cap;

	dc->cap_funcs = cap_funcs;

	if (dc->ctx->dc_bios->fw_info.oem_i2c_present) {
		ddc_init_data.ctx = dc->ctx;
		ddc_init_data.link = NULL;
		ddc_init_data.id.id = dc->ctx->dc_bios->fw_info.oem_i2c_obj_id;
		ddc_init_data.id.enum_id = 0;
		ddc_init_data.id.type = OBJECT_TYPE_GENERIC;
		pool->base.oem_device = dal_ddc_service_create(&ddc_init_data);
	} else {
		pool->base.oem_device = NULL;
	}

	DC_FP_END();
	return true;

create_fail:

	DC_FP_END();
	dcn20_resource_destruct(pool);

	return false;
}

struct resource_pool *dcn20_create_resource_pool(
		const struct dc_init_data *init_data,
		struct dc *dc)
{
	struct dcn20_resource_pool *pool =
		kzalloc(sizeof(struct dcn20_resource_pool), GFP_KERNEL);

	if (!pool)
		return NULL;

	if (dcn20_resource_construct(init_data->num_virtual_links, dc, pool))
		return &pool->base;

	BREAK_TO_DEBUGGER();
	kfree(pool);
	return NULL;
}
