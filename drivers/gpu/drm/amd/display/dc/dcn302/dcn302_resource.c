/*
 * Copyright 2020 Advanced Micro Devices, Inc.
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

#include "dcn302_init.h"
#include "dcn302_resource.h"
#include "dcn302_dccg.h"
#include "irq/dcn302/irq_service_dcn302.h"

#include "dcn30/dcn30_dio_link_encoder.h"
#include "dcn30/dcn30_dio_stream_encoder.h"
#include "dcn30/dcn30_dwb.h"
#include "dcn30/dcn30_dpp.h"
#include "dcn30/dcn30_hubbub.h"
#include "dcn30/dcn30_hubp.h"
#include "dcn30/dcn30_mmhubbub.h"
#include "dcn30/dcn30_mpc.h"
#include "dcn30/dcn30_opp.h"
#include "dcn30/dcn30_optc.h"
#include "dcn30/dcn30_resource.h"

#include "dcn20/dcn20_dsc.h"
#include "dcn20/dcn20_resource.h"

#include "dcn10/dcn10_resource.h"

#include "dce/dce_abm.h"
#include "dce/dce_audio.h"
#include "dce/dce_aux.h"
#include "dce/dce_clock_source.h"
#include "dce/dce_hwseq.h"
#include "dce/dce_i2c_hw.h"
#include "dce/dce_panel_cntl.h"
#include "dce/dmub_abm.h"
#include "dce/dmub_psr.h"
#include "clk_mgr.h"

#include "hw_sequencer_private.h"
#include "reg_helper.h"
#include "resource.h"
#include "vm_helper.h"

#include "dimgrey_cavefish_ip_offset.h"
#include "dcn/dcn_3_0_2_offset.h"
#include "dcn/dcn_3_0_2_sh_mask.h"
#include "dcn/dpcs_3_0_0_offset.h"
#include "dcn/dpcs_3_0_0_sh_mask.h"
#include "nbio/nbio_7_4_offset.h"
#include "amdgpu_socbb.h"

#define DC_LOGGER_INIT(logger)

struct _vcs_dpi_ip_params_st dcn3_02_ip = {
		.use_min_dcfclk = 0,
		.clamp_min_dcfclk = 0,
		.odm_capable = 1,
		.gpuvm_enable = 1,
		.hostvm_enable = 0,
		.gpuvm_max_page_table_levels = 4,
		.hostvm_max_page_table_levels = 4,
		.hostvm_cached_page_table_levels = 0,
		.pte_group_size_bytes = 2048,
		.num_dsc = 5,
		.rob_buffer_size_kbytes = 184,
		.det_buffer_size_kbytes = 184,
		.dpte_buffer_size_in_pte_reqs_luma = 64,
		.dpte_buffer_size_in_pte_reqs_chroma = 34,
		.pde_proc_buffer_size_64k_reqs = 48,
		.dpp_output_buffer_pixels = 2560,
		.opp_output_buffer_lines = 1,
		.pixel_chunk_size_kbytes = 8,
		.pte_enable = 1,
		.max_page_table_levels = 2,
		.pte_chunk_size_kbytes = 2,  // ?
		.meta_chunk_size_kbytes = 2,
		.writeback_chunk_size_kbytes = 8,
		.line_buffer_size_bits = 789504,
		.is_line_buffer_bpp_fixed = 0,  // ?
		.line_buffer_fixed_bpp = 0,     // ?
		.dcc_supported = true,
		.writeback_interface_buffer_size_kbytes = 90,
		.writeback_line_buffer_buffer_size = 0,
		.max_line_buffer_lines = 12,
		.writeback_luma_buffer_size_kbytes = 12,  // writeback_line_buffer_buffer_size = 656640
		.writeback_chroma_buffer_size_kbytes = 8,
		.writeback_chroma_line_buffer_width_pixels = 4,
		.writeback_max_hscl_ratio = 1,
		.writeback_max_vscl_ratio = 1,
		.writeback_min_hscl_ratio = 1,
		.writeback_min_vscl_ratio = 1,
		.writeback_max_hscl_taps = 1,
		.writeback_max_vscl_taps = 1,
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
		.max_hscl_ratio = 6,
		.max_vscl_ratio = 6,
		.hscl_mults = 4,
		.vscl_mults = 4,
		.max_hscl_taps = 8,
		.max_vscl_taps = 8,
		.dispclk_ramp_margin_percent = 1,
		.underscan_factor = 1.11,
		.min_vblank_lines = 32,
		.dppclk_delay_subtotal = 46,
		.dynamic_metadata_vm_enabled = true,
		.dppclk_delay_scl_lb_only = 16,
		.dppclk_delay_scl = 50,
		.dppclk_delay_cnvc_formatter = 27,
		.dppclk_delay_cnvc_cursor = 6,
		.dispclk_delay_subtotal = 119,
		.dcfclk_cstate_latency = 5.2, // SRExitTime
		.max_inter_dcn_tile_repeaters = 8,
		.max_num_hdmi_frl_outputs = 1,
		.odm_combine_4to1_supported = true,

		.xfc_supported = false,
		.xfc_fill_bw_overhead_percent = 10.0,
		.xfc_fill_constant_bytes = 0,
		.gfx7_compat_tiling_supported = 0,
		.number_of_cursors = 1,
};

struct _vcs_dpi_soc_bounding_box_st dcn3_02_soc = {
		.clock_limits = {
				{
						.state = 0,
						.dispclk_mhz = 562.0,
						.dppclk_mhz = 300.0,
						.phyclk_mhz = 300.0,
						.phyclk_d18_mhz = 667.0,
						.dscclk_mhz = 405.6,
				},
		},

		.min_dcfclk = 500.0, /* TODO: set this to actual min DCFCLK */
		.num_states = 1,
		.sr_exit_time_us = 26.5,
		.sr_enter_plus_exit_time_us = 31,
		.urgent_latency_us = 4.0,
		.urgent_latency_pixel_data_only_us = 4.0,
		.urgent_latency_pixel_mixed_with_vm_data_us = 4.0,
		.urgent_latency_vm_data_only_us = 4.0,
		.urgent_out_of_order_return_per_channel_pixel_only_bytes = 4096,
		.urgent_out_of_order_return_per_channel_pixel_and_vm_bytes = 4096,
		.urgent_out_of_order_return_per_channel_vm_only_bytes = 4096,
		.pct_ideal_dram_sdp_bw_after_urgent_pixel_only = 80.0,
		.pct_ideal_dram_sdp_bw_after_urgent_pixel_and_vm = 60.0,
		.pct_ideal_dram_sdp_bw_after_urgent_vm_only = 40.0,
		.max_avg_sdp_bw_use_normal_percent = 60.0,
		.max_avg_dram_bw_use_normal_percent = 40.0,
		.writeback_latency_us = 12.0,
		.max_request_size_bytes = 256,
		.fabric_datapath_to_dcn_data_return_bytes = 64,
		.dcn_downspread_percent = 0.5,
		.downspread_percent = 0.38,
		.dram_page_open_time_ns = 50.0,
		.dram_rw_turnaround_time_ns = 17.5,
		.dram_return_buffer_per_channel_bytes = 8192,
		.round_trip_ping_latency_dcfclk_cycles = 156,
		.urgent_out_of_order_return_per_channel_bytes = 4096,
		.channel_interleave_bytes = 256,
		.num_banks = 8,
		.gpuvm_min_page_size_bytes = 4096,
		.hostvm_min_page_size_bytes = 4096,
		.dram_clock_change_latency_us = 404,
		.dummy_pstate_latency_us = 5,
		.writeback_dram_clock_change_latency_us = 23.0,
		.return_bus_width_bytes = 64,
		.dispclk_dppclk_vco_speed_mhz = 3650,
		.xfc_bus_transport_time_us = 20,      // ?
		.xfc_xbuf_latency_tolerance_us = 4,  // ?
		.use_urgent_burst_bw = 1,            // ?
		.do_urgent_latency_adjustment = true,
		.urgent_latency_adjustment_fabric_clock_component_us = 1.0,
		.urgent_latency_adjustment_fabric_clock_reference_mhz = 1000,
};

static const struct dc_debug_options debug_defaults_drv = {
		.disable_dmcu = true,
		.force_abm_enable = false,
		.timing_trace = false,
		.clock_trace = true,
		.disable_pplib_clock_request = true,
		.pipe_split_policy = MPC_SPLIT_DYNAMIC,
		.force_single_disp_pipe_split = false,
		.disable_dcc = DCC_ENABLE,
		.vsr_support = true,
		.performance_trace = false,
		.max_downscale_src_width = 7680,/*upto 8K*/
		.disable_pplib_wm_range = false,
		.scl_reset_length10 = true,
		.sanity_checks = false,
		.underflow_assert_delay_us = 0xFFFFFFFF,
		.dwb_fi_phase = -1, // -1 = disable,
		.dmub_command_table = true,
		.use_max_lb = true
};

static const struct dc_debug_options debug_defaults_diags = {
		.disable_dmcu = true,
		.force_abm_enable = false,
		.timing_trace = true,
		.clock_trace = true,
		.disable_dpp_power_gate = true,
		.disable_hubp_power_gate = true,
		.disable_clock_gate = true,
		.disable_pplib_clock_request = true,
		.disable_pplib_wm_range = true,
		.disable_stutter = false,
		.scl_reset_length10 = true,
		.dwb_fi_phase = -1, // -1 = disable
		.dmub_command_table = true,
		.enable_tri_buf = true,
		.disable_psr = true,
		.use_max_lb = true
};

enum dcn302_clk_src_array_id {
	DCN302_CLK_SRC_PLL0,
	DCN302_CLK_SRC_PLL1,
	DCN302_CLK_SRC_PLL2,
	DCN302_CLK_SRC_PLL3,
	DCN302_CLK_SRC_PLL4,
	DCN302_CLK_SRC_TOTAL
};

static const struct resource_caps res_cap_dcn302 = {
		.num_timing_generator = 5,
		.num_opp = 5,
		.num_video_plane = 5,
		.num_audio = 5,
		.num_stream_encoder = 5,
		.num_dwb = 1,
		.num_ddc = 5,
		.num_vmid = 16,
		.num_mpc_3dlut = 2,
		.num_dsc = 5,
};

static const struct dc_plane_cap plane_cap = {
		.type = DC_PLANE_TYPE_DCN_UNIVERSAL,
		.blends_with_above = true,
		.blends_with_below = true,
		.per_pixel_alpha = true,
		.pixel_format_support = {
				.argb8888 = true,
				.nv12 = true,
				.fp16 = true,
				.p010 = false,
				.ayuv = false,
		},
		.max_upscale_factor = {
				.argb8888 = 16000,
				.nv12 = 16000,
				.fp16 = 16000
		},
		/* 6:1 downscaling ratio: 1000/6 = 166.666 */
		.max_downscale_factor = {
				.argb8888 = 167,
				.nv12 = 167,
				.fp16 = 167
		},
		16,
		16
};

/* NBIO */
#define NBIO_BASE_INNER(seg) \
		NBIO_BASE__INST0_SEG ## seg

#define NBIO_BASE(seg) \
		NBIO_BASE_INNER(seg)

#define NBIO_SR(reg_name)\
		.reg_name = NBIO_BASE(mm ## reg_name ## _BASE_IDX) + \
		mm ## reg_name

/* DCN */
#undef BASE_INNER
#define BASE_INNER(seg) DCN_BASE__INST0_SEG ## seg

#define BASE(seg) BASE_INNER(seg)

#define SR(reg_name)\
		.reg_name = BASE(mm ## reg_name ## _BASE_IDX) + mm ## reg_name

#define SF(reg_name, field_name, post_fix)\
		.field_name = reg_name ## __ ## field_name ## post_fix

#define SRI(reg_name, block, id)\
		.reg_name = BASE(mm ## block ## id ## _ ## reg_name ## _BASE_IDX) + mm ## block ## id ## _ ## reg_name

#define SRI2(reg_name, block, id)\
		.reg_name = BASE(mm ## reg_name ## _BASE_IDX) + mm ## reg_name

#define SRII(reg_name, block, id)\
		.reg_name[id] = BASE(mm ## block ## id ## _ ## reg_name ## _BASE_IDX) + \
		mm ## block ## id ## _ ## reg_name

#define DCCG_SRII(reg_name, block, id)\
		.block ## _ ## reg_name[id] = BASE(mm ## block ## id ## _ ## reg_name ## _BASE_IDX) + \
		mm ## block ## id ## _ ## reg_name

#define VUPDATE_SRII(reg_name, block, id)\
		.reg_name[id] = BASE(mm ## reg_name ## _ ## block ## id ## _BASE_IDX) + \
		mm ## reg_name ## _ ## block ## id

#define SRII_DWB(reg_name, temp_name, block, id)\
		.reg_name[id] = BASE(mm ## block ## id ## _ ## temp_name ## _BASE_IDX) + \
		mm ## block ## id ## _ ## temp_name

#define SRII_MPC_RMU(reg_name, block, id)\
		.RMU##_##reg_name[id] = BASE(mm ## block ## id ## _ ## reg_name ## _BASE_IDX) + \
		mm ## block ## id ## _ ## reg_name

static const struct dcn_hubbub_registers hubbub_reg = {
		HUBBUB_REG_LIST_DCN30(0)
};

static const struct dcn_hubbub_shift hubbub_shift = {
		HUBBUB_MASK_SH_LIST_DCN30(__SHIFT)
};

static const struct dcn_hubbub_mask hubbub_mask = {
		HUBBUB_MASK_SH_LIST_DCN30(_MASK)
};

#define vmid_regs(id)\
		[id] = { DCN20_VMID_REG_LIST(id) }

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

static struct hubbub *dcn302_hubbub_create(struct dc_context *ctx)
{
	int i;

	struct dcn20_hubbub *hubbub3 = kzalloc(sizeof(struct dcn20_hubbub), GFP_KERNEL);

	if (!hubbub3)
		return NULL;

	hubbub3_construct(hubbub3, ctx, &hubbub_reg, &hubbub_shift, &hubbub_mask);

	for (i = 0; i < res_cap_dcn302.num_vmid; i++) {
		struct dcn20_vmid *vmid = &hubbub3->vmid[i];

		vmid->ctx = ctx;

		vmid->regs = &vmid_regs[i];
		vmid->shifts = &vmid_shifts;
		vmid->masks = &vmid_masks;
	}

	return &hubbub3->base;
}

#define vpg_regs(id)\
		[id] = { VPG_DCN3_REG_LIST(id) }

static const struct dcn30_vpg_registers vpg_regs[] = {
		vpg_regs(0),
		vpg_regs(1),
		vpg_regs(2),
		vpg_regs(3),
		vpg_regs(4),
		vpg_regs(5)
};

static const struct dcn30_vpg_shift vpg_shift = {
		DCN3_VPG_MASK_SH_LIST(__SHIFT)
};

static const struct dcn30_vpg_mask vpg_mask = {
		DCN3_VPG_MASK_SH_LIST(_MASK)
};

static struct vpg *dcn302_vpg_create(struct dc_context *ctx, uint32_t inst)
{
	struct dcn30_vpg *vpg3 = kzalloc(sizeof(struct dcn30_vpg), GFP_KERNEL);

	if (!vpg3)
		return NULL;

	vpg3_construct(vpg3, ctx, inst, &vpg_regs[inst], &vpg_shift, &vpg_mask);

	return &vpg3->base;
}

#define afmt_regs(id)\
		[id] = { AFMT_DCN3_REG_LIST(id) }

static const struct dcn30_afmt_registers afmt_regs[] = {
		afmt_regs(0),
		afmt_regs(1),
		afmt_regs(2),
		afmt_regs(3),
		afmt_regs(4),
		afmt_regs(5)
};

static const struct dcn30_afmt_shift afmt_shift = {
		DCN3_AFMT_MASK_SH_LIST(__SHIFT)
};

static const struct dcn30_afmt_mask afmt_mask = {
		DCN3_AFMT_MASK_SH_LIST(_MASK)
};

static struct afmt *dcn302_afmt_create(struct dc_context *ctx, uint32_t inst)
{
	struct dcn30_afmt *afmt3 = kzalloc(sizeof(struct dcn30_afmt), GFP_KERNEL);

	if (!afmt3)
		return NULL;

	afmt3_construct(afmt3, ctx, inst, &afmt_regs[inst], &afmt_shift, &afmt_mask);

	return &afmt3->base;
}

#define audio_regs(id)\
		[id] = { AUD_COMMON_REG_LIST(id) }

static const struct dce_audio_registers audio_regs[] = {
		audio_regs(0),
		audio_regs(1),
		audio_regs(2),
		audio_regs(3),
		audio_regs(4),
		audio_regs(5),
		audio_regs(6)
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

static struct audio *dcn302_create_audio(struct dc_context *ctx, unsigned int inst)
{
	return dce_audio_create(ctx, inst, &audio_regs[inst], &audio_shift, &audio_mask);
}

#define stream_enc_regs(id)\
		[id] = { SE_DCN3_REG_LIST(id) }

static const struct dcn10_stream_enc_registers stream_enc_regs[] = {
		stream_enc_regs(0),
		stream_enc_regs(1),
		stream_enc_regs(2),
		stream_enc_regs(3),
		stream_enc_regs(4)
};

static const struct dcn10_stream_encoder_shift se_shift = {
		SE_COMMON_MASK_SH_LIST_DCN30(__SHIFT)
};

static const struct dcn10_stream_encoder_mask se_mask = {
		SE_COMMON_MASK_SH_LIST_DCN30(_MASK)
};

static struct stream_encoder *dcn302_stream_encoder_create(enum engine_id eng_id, struct dc_context *ctx)
{
	struct dcn10_stream_encoder *enc1;
	struct vpg *vpg;
	struct afmt *afmt;
	int vpg_inst;
	int afmt_inst;

	/* Mapping of VPG, AFMT, DME register blocks to DIO block instance */
	if (eng_id <= ENGINE_ID_DIGE) {
		vpg_inst = eng_id;
		afmt_inst = eng_id;
	} else
		return NULL;

	enc1 = kzalloc(sizeof(struct dcn10_stream_encoder), GFP_KERNEL);
	vpg = dcn302_vpg_create(ctx, vpg_inst);
	afmt = dcn302_afmt_create(ctx, afmt_inst);

	if (!enc1 || !vpg || !afmt)
		return NULL;

	dcn30_dio_stream_encoder_construct(enc1, ctx, ctx->dc_bios, eng_id, vpg, afmt, &stream_enc_regs[eng_id],
			&se_shift, &se_mask);

	return &enc1->base;
}

#define clk_src_regs(index, pllid)\
		[index] = { CS_COMMON_REG_LIST_DCN3_02(index, pllid) }

static const struct dce110_clk_src_regs clk_src_regs[] = {
		clk_src_regs(0, A),
		clk_src_regs(1, B),
		clk_src_regs(2, C),
		clk_src_regs(3, D),
		clk_src_regs(4, E)
};

static const struct dce110_clk_src_shift cs_shift = {
		CS_COMMON_MASK_SH_LIST_DCN2_0(__SHIFT)
};

static const struct dce110_clk_src_mask cs_mask = {
		CS_COMMON_MASK_SH_LIST_DCN2_0(_MASK)
};

static struct clock_source *dcn302_clock_source_create(struct dc_context *ctx, struct dc_bios *bios,
		enum clock_source_id id, const struct dce110_clk_src_regs *regs, bool dp_clk_src)
{
	struct dce110_clk_src *clk_src = kzalloc(sizeof(struct dce110_clk_src), GFP_KERNEL);

	if (!clk_src)
		return NULL;

	if (dcn3_clk_src_construct(clk_src, ctx, bios, id, regs, &cs_shift, &cs_mask)) {
		clk_src->base.dp_clk_src = dp_clk_src;
		return &clk_src->base;
	}

	BREAK_TO_DEBUGGER();
	return NULL;
}

static const struct dce_hwseq_registers hwseq_reg = {
		HWSEQ_DCN302_REG_LIST()
};

static const struct dce_hwseq_shift hwseq_shift = {
		HWSEQ_DCN302_MASK_SH_LIST(__SHIFT)
};

static const struct dce_hwseq_mask hwseq_mask = {
		HWSEQ_DCN302_MASK_SH_LIST(_MASK)
};

static struct dce_hwseq *dcn302_hwseq_create(struct dc_context *ctx)
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

#define hubp_regs(id)\
		[id] = { HUBP_REG_LIST_DCN30(id) }

static const struct dcn_hubp2_registers hubp_regs[] = {
		hubp_regs(0),
		hubp_regs(1),
		hubp_regs(2),
		hubp_regs(3),
		hubp_regs(4)
};

static const struct dcn_hubp2_shift hubp_shift = {
		HUBP_MASK_SH_LIST_DCN30(__SHIFT)
};

static const struct dcn_hubp2_mask hubp_mask = {
		HUBP_MASK_SH_LIST_DCN30(_MASK)
};

static struct hubp *dcn302_hubp_create(struct dc_context *ctx, uint32_t inst)
{
	struct dcn20_hubp *hubp2 = kzalloc(sizeof(struct dcn20_hubp), GFP_KERNEL);

	if (!hubp2)
		return NULL;

	if (hubp3_construct(hubp2, ctx, inst, &hubp_regs[inst], &hubp_shift, &hubp_mask))
		return &hubp2->base;

	BREAK_TO_DEBUGGER();
	kfree(hubp2);
	return NULL;
}

#define dpp_regs(id)\
		[id] = { DPP_REG_LIST_DCN30(id) }

static const struct dcn3_dpp_registers dpp_regs[] = {
		dpp_regs(0),
		dpp_regs(1),
		dpp_regs(2),
		dpp_regs(3),
		dpp_regs(4)
};

static const struct dcn3_dpp_shift tf_shift = {
		DPP_REG_LIST_SH_MASK_DCN30(__SHIFT)
};

static const struct dcn3_dpp_mask tf_mask = {
		DPP_REG_LIST_SH_MASK_DCN30(_MASK)
};

static struct dpp *dcn302_dpp_create(struct dc_context *ctx, uint32_t inst)
{
	struct dcn3_dpp *dpp = kzalloc(sizeof(struct dcn3_dpp), GFP_KERNEL);

	if (!dpp)
		return NULL;

	if (dpp3_construct(dpp, ctx, inst, &dpp_regs[inst], &tf_shift, &tf_mask))
		return &dpp->base;

	BREAK_TO_DEBUGGER();
	kfree(dpp);
	return NULL;
}

#define opp_regs(id)\
		[id] = { OPP_REG_LIST_DCN30(id) }

static const struct dcn20_opp_registers opp_regs[] = {
		opp_regs(0),
		opp_regs(1),
		opp_regs(2),
		opp_regs(3),
		opp_regs(4)
};

static const struct dcn20_opp_shift opp_shift = {
		OPP_MASK_SH_LIST_DCN20(__SHIFT)
};

static const struct dcn20_opp_mask opp_mask = {
		OPP_MASK_SH_LIST_DCN20(_MASK)
};

static struct output_pixel_processor *dcn302_opp_create(struct dc_context *ctx, uint32_t inst)
{
	struct dcn20_opp *opp = kzalloc(sizeof(struct dcn20_opp), GFP_KERNEL);

	if (!opp) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	dcn20_opp_construct(opp, ctx, inst, &opp_regs[inst], &opp_shift, &opp_mask);
	return &opp->base;
}

#define optc_regs(id)\
		[id] = { OPTC_COMMON_REG_LIST_DCN3_0(id) }

static const struct dcn_optc_registers optc_regs[] = {
		optc_regs(0),
		optc_regs(1),
		optc_regs(2),
		optc_regs(3),
		optc_regs(4)
};

static const struct dcn_optc_shift optc_shift = {
		OPTC_COMMON_MASK_SH_LIST_DCN30(__SHIFT)
};

static const struct dcn_optc_mask optc_mask = {
		OPTC_COMMON_MASK_SH_LIST_DCN30(_MASK)
};

static struct timing_generator *dcn302_timing_generator_create(struct dc_context *ctx, uint32_t instance)
{
	struct optc *tgn10 = kzalloc(sizeof(struct optc), GFP_KERNEL);

	if (!tgn10)
		return NULL;

	tgn10->base.inst = instance;
	tgn10->base.ctx = ctx;

	tgn10->tg_regs = &optc_regs[instance];
	tgn10->tg_shift = &optc_shift;
	tgn10->tg_mask = &optc_mask;

	dcn30_timing_generator_init(tgn10);

	return &tgn10->base;
}

static const struct dcn30_mpc_registers mpc_regs = {
		MPC_REG_LIST_DCN3_0(0),
		MPC_REG_LIST_DCN3_0(1),
		MPC_REG_LIST_DCN3_0(2),
		MPC_REG_LIST_DCN3_0(3),
		MPC_REG_LIST_DCN3_0(4),
		MPC_OUT_MUX_REG_LIST_DCN3_0(0),
		MPC_OUT_MUX_REG_LIST_DCN3_0(1),
		MPC_OUT_MUX_REG_LIST_DCN3_0(2),
		MPC_OUT_MUX_REG_LIST_DCN3_0(3),
		MPC_OUT_MUX_REG_LIST_DCN3_0(4),
		MPC_RMU_GLOBAL_REG_LIST_DCN3AG,
		MPC_RMU_REG_LIST_DCN3AG(0),
		MPC_RMU_REG_LIST_DCN3AG(1),
		MPC_RMU_REG_LIST_DCN3AG(2),
		MPC_DWB_MUX_REG_LIST_DCN3_0(0),
};

static const struct dcn30_mpc_shift mpc_shift = {
		MPC_COMMON_MASK_SH_LIST_DCN30(__SHIFT)
};

static const struct dcn30_mpc_mask mpc_mask = {
		MPC_COMMON_MASK_SH_LIST_DCN30(_MASK)
};

static struct mpc *dcn302_mpc_create(struct dc_context *ctx, int num_mpcc, int num_rmu)
{
	struct dcn30_mpc *mpc30 = kzalloc(sizeof(struct dcn30_mpc), GFP_KERNEL);

	if (!mpc30)
		return NULL;

	dcn30_mpc_construct(mpc30, ctx, &mpc_regs, &mpc_shift, &mpc_mask, num_mpcc, num_rmu);

	return &mpc30->base;
}

#define dsc_regsDCN20(id)\
[id] = { DSC_REG_LIST_DCN20(id) }

static const struct dcn20_dsc_registers dsc_regs[] = {
		dsc_regsDCN20(0),
		dsc_regsDCN20(1),
		dsc_regsDCN20(2),
		dsc_regsDCN20(3),
		dsc_regsDCN20(4)
};

static const struct dcn20_dsc_shift dsc_shift = {
		DSC_REG_LIST_SH_MASK_DCN20(__SHIFT)
};

static const struct dcn20_dsc_mask dsc_mask = {
		DSC_REG_LIST_SH_MASK_DCN20(_MASK)
};

static struct display_stream_compressor *dcn302_dsc_create(struct dc_context *ctx, uint32_t inst)
{
	struct dcn20_dsc *dsc = kzalloc(sizeof(struct dcn20_dsc), GFP_KERNEL);

	if (!dsc) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	dsc2_construct(dsc, ctx, inst, &dsc_regs[inst], &dsc_shift, &dsc_mask);
	return &dsc->base;
}

#define dwbc_regs_dcn3(id)\
[id] = { DWBC_COMMON_REG_LIST_DCN30(id) }

static const struct dcn30_dwbc_registers dwbc30_regs[] = {
		dwbc_regs_dcn3(0)
};

static const struct dcn30_dwbc_shift dwbc30_shift = {
		DWBC_COMMON_MASK_SH_LIST_DCN30(__SHIFT)
};

static const struct dcn30_dwbc_mask dwbc30_mask = {
		DWBC_COMMON_MASK_SH_LIST_DCN30(_MASK)
};

static bool dcn302_dwbc_create(struct dc_context *ctx, struct resource_pool *pool)
{
	int i;
	uint32_t pipe_count = pool->res_cap->num_dwb;

	for (i = 0; i < pipe_count; i++) {
		struct dcn30_dwbc *dwbc30 = kzalloc(sizeof(struct dcn30_dwbc), GFP_KERNEL);

		if (!dwbc30) {
			dm_error("DC: failed to create dwbc30!\n");
			return false;
		}

		dcn30_dwbc_construct(dwbc30, ctx, &dwbc30_regs[i], &dwbc30_shift, &dwbc30_mask, i);

		pool->dwbc[i] = &dwbc30->base;
	}
	return true;
}

#define mcif_wb_regs_dcn3(id)\
[id] = { MCIF_WB_COMMON_REG_LIST_DCN30(id) }

static const struct dcn30_mmhubbub_registers mcif_wb30_regs[] = {
		mcif_wb_regs_dcn3(0)
};

static const struct dcn30_mmhubbub_shift mcif_wb30_shift = {
		MCIF_WB_COMMON_MASK_SH_LIST_DCN30(__SHIFT)
};

static const struct dcn30_mmhubbub_mask mcif_wb30_mask = {
		MCIF_WB_COMMON_MASK_SH_LIST_DCN30(_MASK)
};

static bool dcn302_mmhubbub_create(struct dc_context *ctx, struct resource_pool *pool)
{
	int i;
	uint32_t pipe_count = pool->res_cap->num_dwb;

	for (i = 0; i < pipe_count; i++) {
		struct dcn30_mmhubbub *mcif_wb30 = kzalloc(sizeof(struct dcn30_mmhubbub), GFP_KERNEL);

		if (!mcif_wb30) {
			dm_error("DC: failed to create mcif_wb30!\n");
			return false;
		}

		dcn30_mmhubbub_construct(mcif_wb30, ctx, &mcif_wb30_regs[i], &mcif_wb30_shift, &mcif_wb30_mask, i);

		pool->mcif_wb[i] = &mcif_wb30->base;
	}
	return true;
}

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
		aux_engine_regs(4)
};

static const struct dce110_aux_registers_shift aux_shift = {
		DCN_AUX_MASK_SH_LIST(__SHIFT)
};

static const struct dce110_aux_registers_mask aux_mask = {
		DCN_AUX_MASK_SH_LIST(_MASK)
};

static struct dce_aux *dcn302_aux_engine_create(struct dc_context *ctx, uint32_t inst)
{
	struct aux_engine_dce110 *aux_engine = kzalloc(sizeof(struct aux_engine_dce110), GFP_KERNEL);

	if (!aux_engine)
		return NULL;

	dce110_aux_engine_construct(aux_engine, ctx, inst, SW_AUX_TIMEOUT_PERIOD_MULTIPLIER * AUX_TIMEOUT_PERIOD,
			&aux_engine_regs[inst], &aux_mask, &aux_shift, ctx->dc->caps.extended_aux_timeout_support);

	return &aux_engine->base;
}

#define i2c_inst_regs(id) { I2C_HW_ENGINE_COMMON_REG_LIST(id) }

static const struct dce_i2c_registers i2c_hw_regs[] = {
		i2c_inst_regs(1),
		i2c_inst_regs(2),
		i2c_inst_regs(3),
		i2c_inst_regs(4),
		i2c_inst_regs(5)
};

static const struct dce_i2c_shift i2c_shifts = {
		I2C_COMMON_MASK_SH_LIST_DCN2(__SHIFT)
};

static const struct dce_i2c_mask i2c_masks = {
		I2C_COMMON_MASK_SH_LIST_DCN2(_MASK)
};

static struct dce_i2c_hw *dcn302_i2c_hw_create(struct dc_context *ctx, uint32_t inst)
{
	struct dce_i2c_hw *dce_i2c_hw = kzalloc(sizeof(struct dce_i2c_hw), GFP_KERNEL);

	if (!dce_i2c_hw)
		return NULL;

	dcn2_i2c_hw_construct(dce_i2c_hw, ctx, inst, &i2c_hw_regs[inst], &i2c_shifts, &i2c_masks);

	return dce_i2c_hw;
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

#define link_regs(id, phyid)\
		[id] = {\
				LE_DCN3_REG_LIST(id), \
				UNIPHY_DCN2_REG_LIST(phyid), \
				DPCS_DCN2_REG_LIST(id), \
				SRI(DP_DPHY_INTERNAL_CTRL, DP, id) \
		}

static const struct dcn10_link_enc_registers link_enc_regs[] = {
		link_regs(0, A),
		link_regs(1, B),
		link_regs(2, C),
		link_regs(3, D),
		link_regs(4, E)
};

static const struct dcn10_link_enc_shift le_shift = {
		LINK_ENCODER_MASK_SH_LIST_DCN30(__SHIFT),
		DPCS_DCN2_MASK_SH_LIST(__SHIFT)
};

static const struct dcn10_link_enc_mask le_mask = {
		LINK_ENCODER_MASK_SH_LIST_DCN30(_MASK),
		DPCS_DCN2_MASK_SH_LIST(_MASK)
};

#define aux_regs(id)\
		[id] = { DCN2_AUX_REG_LIST(id) }

static const struct dcn10_link_enc_aux_registers link_enc_aux_regs[] = {
		aux_regs(0),
		aux_regs(1),
		aux_regs(2),
		aux_regs(3),
		aux_regs(4)
};

#define hpd_regs(id)\
		[id] = { HPD_REG_LIST(id) }

static const struct dcn10_link_enc_hpd_registers link_enc_hpd_regs[] = {
		hpd_regs(0),
		hpd_regs(1),
		hpd_regs(2),
		hpd_regs(3),
		hpd_regs(4)
};

static struct link_encoder *dcn302_link_encoder_create(const struct encoder_init_data *enc_init_data)
{
	struct dcn20_link_encoder *enc20 = kzalloc(sizeof(struct dcn20_link_encoder), GFP_KERNEL);

	if (!enc20)
		return NULL;

	dcn30_link_encoder_construct(enc20, enc_init_data, &link_enc_feature,
			&link_enc_regs[enc_init_data->transmitter], &link_enc_aux_regs[enc_init_data->channel - 1],
			&link_enc_hpd_regs[enc_init_data->hpd_source], &le_shift, &le_mask);

	return &enc20->enc10.base;
}

static const struct dce_panel_cntl_registers panel_cntl_regs[] = {
		{ DCN_PANEL_CNTL_REG_LIST() }
};

static const struct dce_panel_cntl_shift panel_cntl_shift = {
		DCE_PANEL_CNTL_MASK_SH_LIST(__SHIFT)
};

static const struct dce_panel_cntl_mask panel_cntl_mask = {
		DCE_PANEL_CNTL_MASK_SH_LIST(_MASK)
};

static struct panel_cntl *dcn302_panel_cntl_create(const struct panel_cntl_init_data *init_data)
{
	struct dce_panel_cntl *panel_cntl = kzalloc(sizeof(struct dce_panel_cntl), GFP_KERNEL);

	if (!panel_cntl)
		return NULL;

	dce_panel_cntl_construct(panel_cntl, init_data, &panel_cntl_regs[init_data->inst],
			&panel_cntl_shift, &panel_cntl_mask);

	return &panel_cntl->base;
}

static void read_dce_straps(struct dc_context *ctx, struct resource_straps *straps)
{
	generic_reg_get(ctx, mmDC_PINSTRAPS + BASE(mmDC_PINSTRAPS_BASE_IDX),
			FN(DC_PINSTRAPS, DC_PINSTRAPS_AUDIO), &straps->dc_pinstraps_audio);
}

static const struct resource_create_funcs res_create_funcs = {
		.read_dce_straps = read_dce_straps,
		.create_audio = dcn302_create_audio,
		.create_stream_encoder = dcn302_stream_encoder_create,
		.create_hwseq = dcn302_hwseq_create,
};

static const struct resource_create_funcs res_create_maximus_funcs = {
		.read_dce_straps = NULL,
		.create_audio = NULL,
		.create_stream_encoder = NULL,
		.create_hwseq = dcn302_hwseq_create,
};

static bool is_soc_bounding_box_valid(struct dc *dc)
{
	uint32_t hw_internal_rev = dc->ctx->asic_id.hw_internal_rev;

	if (ASICREV_IS_DIMGREY_CAVEFISH_P(hw_internal_rev))
		return true;

	return false;
}

static bool init_soc_bounding_box(struct dc *dc,  struct resource_pool *pool)
{
	struct _vcs_dpi_soc_bounding_box_st *loaded_bb = &dcn3_02_soc;
	struct _vcs_dpi_ip_params_st *loaded_ip = &dcn3_02_ip;

	DC_LOGGER_INIT(dc->ctx->logger);

	if (!is_soc_bounding_box_valid(dc)) {
		DC_LOG_ERROR("%s: not valid soc bounding box\n", __func__);
		return false;
	}

	loaded_ip->max_num_otg = pool->pipe_count;
	loaded_ip->max_num_dpp = pool->pipe_count;
	loaded_ip->clamp_min_dcfclk = dc->config.clamp_min_dcfclk;
	dcn20_patch_bounding_box(dc, loaded_bb);

	if (dc->ctx->dc_bios->funcs->get_soc_bb_info) {
		struct bp_soc_bb_info bb_info = { 0 };

		if (dc->ctx->dc_bios->funcs->get_soc_bb_info(
			    dc->ctx->dc_bios, &bb_info) == BP_RESULT_OK) {
			if (bb_info.dram_clock_change_latency_100ns > 0)
				dcn3_02_soc.dram_clock_change_latency_us =
					bb_info.dram_clock_change_latency_100ns * 10;

			if (bb_info.dram_sr_enter_exit_latency_100ns > 0)
				dcn3_02_soc.sr_enter_plus_exit_time_us =
					bb_info.dram_sr_enter_exit_latency_100ns * 10;

			if (bb_info.dram_sr_exit_latency_100ns > 0)
				dcn3_02_soc.sr_exit_time_us =
					bb_info.dram_sr_exit_latency_100ns * 10;
		}
	}

	return true;
}

static void dcn302_resource_destruct(struct resource_pool *pool)
{
	unsigned int i;

	for (i = 0; i < pool->stream_enc_count; i++) {
		if (pool->stream_enc[i] != NULL) {
			if (pool->stream_enc[i]->vpg != NULL) {
				kfree(DCN30_VPG_FROM_VPG(pool->stream_enc[i]->vpg));
				pool->stream_enc[i]->vpg = NULL;
			}
			if (pool->stream_enc[i]->afmt != NULL) {
				kfree(DCN30_AFMT_FROM_AFMT(pool->stream_enc[i]->afmt));
				pool->stream_enc[i]->afmt = NULL;
			}
			kfree(DCN10STRENC_FROM_STRENC(pool->stream_enc[i]));
			pool->stream_enc[i] = NULL;
		}
	}

	for (i = 0; i < pool->res_cap->num_dsc; i++) {
		if (pool->dscs[i] != NULL)
			dcn20_dsc_destroy(&pool->dscs[i]);
	}

	if (pool->mpc != NULL) {
		kfree(TO_DCN20_MPC(pool->mpc));
		pool->mpc = NULL;
	}

	if (pool->hubbub != NULL) {
		kfree(pool->hubbub);
		pool->hubbub = NULL;
	}

	for (i = 0; i < pool->pipe_count; i++) {
		if (pool->dpps[i] != NULL) {
			kfree(TO_DCN20_DPP(pool->dpps[i]));
			pool->dpps[i] = NULL;
		}

		if (pool->hubps[i] != NULL) {
			kfree(TO_DCN20_HUBP(pool->hubps[i]));
			pool->hubps[i] = NULL;
		}

		if (pool->irqs != NULL)
			dal_irq_service_destroy(&pool->irqs);
	}

	for (i = 0; i < pool->res_cap->num_ddc; i++) {
		if (pool->engines[i] != NULL)
			dce110_engine_destroy(&pool->engines[i]);
		if (pool->hw_i2cs[i] != NULL) {
			kfree(pool->hw_i2cs[i]);
			pool->hw_i2cs[i] = NULL;
		}
		if (pool->sw_i2cs[i] != NULL) {
			kfree(pool->sw_i2cs[i]);
			pool->sw_i2cs[i] = NULL;
		}
	}

	for (i = 0; i < pool->res_cap->num_opp; i++) {
		if (pool->opps[i] != NULL)
			pool->opps[i]->funcs->opp_destroy(&pool->opps[i]);
	}

	for (i = 0; i < pool->res_cap->num_timing_generator; i++) {
		if (pool->timing_generators[i] != NULL)	{
			kfree(DCN10TG_FROM_TG(pool->timing_generators[i]));
			pool->timing_generators[i] = NULL;
		}
	}

	for (i = 0; i < pool->res_cap->num_dwb; i++) {
		if (pool->dwbc[i] != NULL) {
			kfree(TO_DCN30_DWBC(pool->dwbc[i]));
			pool->dwbc[i] = NULL;
		}
		if (pool->mcif_wb[i] != NULL) {
			kfree(TO_DCN30_MMHUBBUB(pool->mcif_wb[i]));
			pool->mcif_wb[i] = NULL;
		}
	}

	for (i = 0; i < pool->audio_count; i++) {
		if (pool->audios[i])
			dce_aud_destroy(&pool->audios[i]);
	}

	for (i = 0; i < pool->clk_src_count; i++) {
		if (pool->clock_sources[i] != NULL)
			dcn20_clock_source_destroy(&pool->clock_sources[i]);
	}

	if (pool->dp_clock_source != NULL)
		dcn20_clock_source_destroy(&pool->dp_clock_source);

	for (i = 0; i < pool->res_cap->num_mpc_3dlut; i++) {
		if (pool->mpc_lut[i] != NULL) {
			dc_3dlut_func_release(pool->mpc_lut[i]);
			pool->mpc_lut[i] = NULL;
		}
		if (pool->mpc_shaper[i] != NULL) {
			dc_transfer_func_release(pool->mpc_shaper[i]);
			pool->mpc_shaper[i] = NULL;
		}
	}

	for (i = 0; i < pool->pipe_count; i++) {
		if (pool->multiple_abms[i] != NULL)
			dce_abm_destroy(&pool->multiple_abms[i]);
	}

	if (pool->psr != NULL)
		dmub_psr_destroy(&pool->psr);

	if (pool->dccg != NULL)
		dcn_dccg_destroy(&pool->dccg);
}

static void dcn302_destroy_resource_pool(struct resource_pool **pool)
{
	dcn302_resource_destruct(*pool);
	kfree(*pool);
	*pool = NULL;
}

static void dcn302_get_optimal_dcfclk_fclk_for_uclk(unsigned int uclk_mts,
		unsigned int *optimal_dcfclk,
		unsigned int *optimal_fclk)
{
	double bw_from_dram, bw_from_dram1, bw_from_dram2;

	bw_from_dram1 = uclk_mts * dcn3_02_soc.num_chans *
		dcn3_02_soc.dram_channel_width_bytes * (dcn3_02_soc.max_avg_dram_bw_use_normal_percent / 100);
	bw_from_dram2 = uclk_mts * dcn3_02_soc.num_chans *
		dcn3_02_soc.dram_channel_width_bytes * (dcn3_02_soc.max_avg_sdp_bw_use_normal_percent / 100);

	bw_from_dram = (bw_from_dram1 < bw_from_dram2) ? bw_from_dram1 : bw_from_dram2;

	if (optimal_fclk)
		*optimal_fclk = bw_from_dram /
		(dcn3_02_soc.fabric_datapath_to_dcn_data_return_bytes * (dcn3_02_soc.max_avg_sdp_bw_use_normal_percent / 100));

	if (optimal_dcfclk)
		*optimal_dcfclk =  bw_from_dram /
		(dcn3_02_soc.return_bus_width_bytes * (dcn3_02_soc.max_avg_sdp_bw_use_normal_percent / 100));
}

void dcn302_update_bw_bounding_box(struct dc *dc, struct clk_bw_params *bw_params)
{
	unsigned int i, j;
	unsigned int num_states = 0;

	unsigned int dcfclk_mhz[DC__VOLTAGE_STATES] = {0};
	unsigned int dram_speed_mts[DC__VOLTAGE_STATES] = {0};
	unsigned int optimal_uclk_for_dcfclk_sta_targets[DC__VOLTAGE_STATES] = {0};
	unsigned int optimal_dcfclk_for_uclk[DC__VOLTAGE_STATES] = {0};

	unsigned int dcfclk_sta_targets[DC__VOLTAGE_STATES] = {694, 875, 1000, 1200};
	unsigned int num_dcfclk_sta_targets = 4;
	unsigned int num_uclk_states;


	if (dc->ctx->dc_bios->vram_info.num_chans)
		dcn3_02_soc.num_chans = dc->ctx->dc_bios->vram_info.num_chans;

	if (dc->ctx->dc_bios->vram_info.dram_channel_width_bytes)
		dcn3_02_soc.dram_channel_width_bytes = dc->ctx->dc_bios->vram_info.dram_channel_width_bytes;

	dcn3_02_soc.dispclk_dppclk_vco_speed_mhz = dc->clk_mgr->dentist_vco_freq_khz / 1000.0;
	dc->dml.soc.dispclk_dppclk_vco_speed_mhz = dc->clk_mgr->dentist_vco_freq_khz / 1000.0;

	if (bw_params->clk_table.entries[0].memclk_mhz) {
		int max_dcfclk_mhz = 0, max_dispclk_mhz = 0, max_dppclk_mhz = 0, max_phyclk_mhz = 0;

		for (i = 0; i < MAX_NUM_DPM_LVL; i++) {
			if (bw_params->clk_table.entries[i].dcfclk_mhz > max_dcfclk_mhz)
				max_dcfclk_mhz = bw_params->clk_table.entries[i].dcfclk_mhz;
			if (bw_params->clk_table.entries[i].dispclk_mhz > max_dispclk_mhz)
				max_dispclk_mhz = bw_params->clk_table.entries[i].dispclk_mhz;
			if (bw_params->clk_table.entries[i].dppclk_mhz > max_dppclk_mhz)
				max_dppclk_mhz = bw_params->clk_table.entries[i].dppclk_mhz;
			if (bw_params->clk_table.entries[i].phyclk_mhz > max_phyclk_mhz)
				max_phyclk_mhz = bw_params->clk_table.entries[i].phyclk_mhz;
		}
		if (!max_dcfclk_mhz)
			max_dcfclk_mhz = dcn3_02_soc.clock_limits[0].dcfclk_mhz;
		if (!max_dispclk_mhz)
			max_dispclk_mhz = dcn3_02_soc.clock_limits[0].dispclk_mhz;
		if (!max_dppclk_mhz)
			max_dppclk_mhz = dcn3_02_soc.clock_limits[0].dppclk_mhz;
		if (!max_phyclk_mhz)
			max_phyclk_mhz = dcn3_02_soc.clock_limits[0].phyclk_mhz;

		if (max_dcfclk_mhz > dcfclk_sta_targets[num_dcfclk_sta_targets-1]) {
			/* If max DCFCLK is greater than the max DCFCLK STA target, insert into the DCFCLK STA target array */
			dcfclk_sta_targets[num_dcfclk_sta_targets] = max_dcfclk_mhz;
			num_dcfclk_sta_targets++;
		} else if (max_dcfclk_mhz < dcfclk_sta_targets[num_dcfclk_sta_targets-1]) {
			/* If max DCFCLK is less than the max DCFCLK STA target, cap values and remove duplicates */
			for (i = 0; i < num_dcfclk_sta_targets; i++) {
				if (dcfclk_sta_targets[i] > max_dcfclk_mhz) {
					dcfclk_sta_targets[i] = max_dcfclk_mhz;
					break;
				}
			}
			/* Update size of array since we "removed" duplicates */
			num_dcfclk_sta_targets = i + 1;
		}

		num_uclk_states = bw_params->clk_table.num_entries;

		/* Calculate optimal dcfclk for each uclk */
		for (i = 0; i < num_uclk_states; i++) {
			dcn302_get_optimal_dcfclk_fclk_for_uclk(bw_params->clk_table.entries[i].memclk_mhz * 16,
					&optimal_dcfclk_for_uclk[i], NULL);
			if (optimal_dcfclk_for_uclk[i] < bw_params->clk_table.entries[0].dcfclk_mhz) {
				optimal_dcfclk_for_uclk[i] = bw_params->clk_table.entries[0].dcfclk_mhz;
			}
		}

		/* Calculate optimal uclk for each dcfclk sta target */
		for (i = 0; i < num_dcfclk_sta_targets; i++) {
			for (j = 0; j < num_uclk_states; j++) {
				if (dcfclk_sta_targets[i] < optimal_dcfclk_for_uclk[j]) {
					optimal_uclk_for_dcfclk_sta_targets[i] =
							bw_params->clk_table.entries[j].memclk_mhz * 16;
					break;
				}
			}
		}

		i = 0;
		j = 0;
		/* create the final dcfclk and uclk table */
		while (i < num_dcfclk_sta_targets && j < num_uclk_states && num_states < DC__VOLTAGE_STATES) {
			if (dcfclk_sta_targets[i] < optimal_dcfclk_for_uclk[j] && i < num_dcfclk_sta_targets) {
				dcfclk_mhz[num_states] = dcfclk_sta_targets[i];
				dram_speed_mts[num_states++] = optimal_uclk_for_dcfclk_sta_targets[i++];
			} else {
				if (j < num_uclk_states && optimal_dcfclk_for_uclk[j] <= max_dcfclk_mhz) {
					dcfclk_mhz[num_states] = optimal_dcfclk_for_uclk[j];
					dram_speed_mts[num_states++] = bw_params->clk_table.entries[j++].memclk_mhz * 16;
				} else {
					j = num_uclk_states;
				}
			}
		}

		while (i < num_dcfclk_sta_targets && num_states < DC__VOLTAGE_STATES) {
			dcfclk_mhz[num_states] = dcfclk_sta_targets[i];
			dram_speed_mts[num_states++] = optimal_uclk_for_dcfclk_sta_targets[i++];
		}

		while (j < num_uclk_states && num_states < DC__VOLTAGE_STATES &&
				optimal_dcfclk_for_uclk[j] <= max_dcfclk_mhz) {
			dcfclk_mhz[num_states] = optimal_dcfclk_for_uclk[j];
			dram_speed_mts[num_states++] = bw_params->clk_table.entries[j++].memclk_mhz * 16;
		}

		dcn3_02_soc.num_states = num_states;
		for (i = 0; i < dcn3_02_soc.num_states; i++) {
			dcn3_02_soc.clock_limits[i].state = i;
			dcn3_02_soc.clock_limits[i].dcfclk_mhz = dcfclk_mhz[i];
			dcn3_02_soc.clock_limits[i].fabricclk_mhz = dcfclk_mhz[i];
			dcn3_02_soc.clock_limits[i].dram_speed_mts = dram_speed_mts[i];

			/* Fill all states with max values of all other clocks */
			dcn3_02_soc.clock_limits[i].dispclk_mhz = max_dispclk_mhz;
			dcn3_02_soc.clock_limits[i].dppclk_mhz  = max_dppclk_mhz;
			dcn3_02_soc.clock_limits[i].phyclk_mhz  = max_phyclk_mhz;
			/* Populate from bw_params for DTBCLK, SOCCLK */
			if (!bw_params->clk_table.entries[i].dtbclk_mhz && i > 0)
				dcn3_02_soc.clock_limits[i].dtbclk_mhz  = dcn3_02_soc.clock_limits[i-1].dtbclk_mhz;
			else
				dcn3_02_soc.clock_limits[i].dtbclk_mhz  = bw_params->clk_table.entries[i].dtbclk_mhz;
			if (!bw_params->clk_table.entries[i].socclk_mhz && i > 0)
				dcn3_02_soc.clock_limits[i].socclk_mhz = dcn3_02_soc.clock_limits[i-1].socclk_mhz;
			else
				dcn3_02_soc.clock_limits[i].socclk_mhz = bw_params->clk_table.entries[i].socclk_mhz;
			/* These clocks cannot come from bw_params, always fill from dcn3_02_soc[1] */
			/* FCLK, PHYCLK_D18, DSCCLK */
			dcn3_02_soc.clock_limits[i].phyclk_d18_mhz = dcn3_02_soc.clock_limits[0].phyclk_d18_mhz;
			dcn3_02_soc.clock_limits[i].dscclk_mhz = dcn3_02_soc.clock_limits[0].dscclk_mhz;
		}
		/* re-init DML with updated bb */
		dml_init_instance(&dc->dml, &dcn3_02_soc, &dcn3_02_ip, DML_PROJECT_DCN30);
		if (dc->current_state)
			dml_init_instance(&dc->current_state->bw_ctx.dml, &dcn3_02_soc, &dcn3_02_ip, DML_PROJECT_DCN30);
	}
}

static struct resource_funcs dcn302_res_pool_funcs = {
		.destroy = dcn302_destroy_resource_pool,
		.link_enc_create = dcn302_link_encoder_create,
		.panel_cntl_create = dcn302_panel_cntl_create,
		.validate_bandwidth = dcn30_validate_bandwidth,
		.calculate_wm_and_dlg = dcn30_calculate_wm_and_dlg,
		.update_soc_for_wm_a = dcn30_update_soc_for_wm_a,
		.populate_dml_pipes = dcn30_populate_dml_pipes_from_context,
		.acquire_idle_pipe_for_layer = dcn20_acquire_idle_pipe_for_layer,
		.add_stream_to_ctx = dcn30_add_stream_to_ctx,
		.add_dsc_to_stream_resource = dcn20_add_dsc_to_stream_resource,
		.remove_stream_from_ctx = dcn20_remove_stream_from_ctx,
		.populate_dml_writeback_from_context = dcn30_populate_dml_writeback_from_context,
		.set_mcif_arb_params = dcn30_set_mcif_arb_params,
		.find_first_free_match_stream_enc_for_link = dcn10_find_first_free_match_stream_enc_for_link,
		.acquire_post_bldn_3dlut = dcn30_acquire_post_bldn_3dlut,
		.release_post_bldn_3dlut = dcn30_release_post_bldn_3dlut,
		.update_bw_bounding_box = dcn302_update_bw_bounding_box,
		.patch_unknown_plane_state = dcn20_patch_unknown_plane_state,
};

static struct dc_cap_funcs cap_funcs = {
		.get_dcc_compression_cap = dcn20_get_dcc_compression_cap
};

static const struct bios_registers bios_regs = {
		NBIO_SR(BIOS_SCRATCH_3),
		NBIO_SR(BIOS_SCRATCH_6)
};

static const struct dccg_registers dccg_regs = {
		DCCG_REG_LIST_DCN3_02()
};

static const struct dccg_shift dccg_shift = {
		DCCG_MASK_SH_LIST_DCN3_02(__SHIFT)
};

static const struct dccg_mask dccg_mask = {
		DCCG_MASK_SH_LIST_DCN3_02(_MASK)
};

#define abm_regs(id)\
		[id] = { ABM_DCN301_REG_LIST(id) }

static const struct dce_abm_registers abm_regs[] = {
		abm_regs(0),
		abm_regs(1),
		abm_regs(2),
		abm_regs(3),
		abm_regs(4)
};

static const struct dce_abm_shift abm_shift = {
		ABM_MASK_SH_LIST_DCN30(__SHIFT)
};

static const struct dce_abm_mask abm_mask = {
		ABM_MASK_SH_LIST_DCN30(_MASK)
};

static bool dcn302_resource_construct(
		uint8_t num_virtual_links,
		struct dc *dc,
		struct resource_pool *pool)
{
	int i;
	struct dc_context *ctx = dc->ctx;
	struct irq_service_init_data init_data;

	ctx->dc_bios->regs = &bios_regs;

	pool->res_cap = &res_cap_dcn302;

	pool->funcs = &dcn302_res_pool_funcs;

	/*************************************************
	 *  Resource + asic cap harcoding                *
	 *************************************************/
	pool->underlay_pipe_index = NO_UNDERLAY_PIPE;
	pool->pipe_count = pool->res_cap->num_timing_generator;
	pool->mpcc_count = pool->res_cap->num_timing_generator;
	dc->caps.max_downscale_ratio = 600;
	dc->caps.i2c_speed_in_khz = 100;
	dc->caps.i2c_speed_in_khz_hdcp = 5; /*1.4 w/a applied by derfault*/
	dc->caps.max_cursor_size = 256;
	dc->caps.min_horizontal_blanking_period = 80;
	dc->caps.dmdata_alloc_size = 2048;
	dc->caps.mall_size_per_mem_channel = 4;
	/* total size = mall per channel * num channels * 1024 * 1024 */
	dc->caps.mall_size_total = dc->caps.mall_size_per_mem_channel * dc->ctx->dc_bios->vram_info.num_chans * 1048576;
	dc->caps.cursor_cache_size = dc->caps.max_cursor_size * dc->caps.max_cursor_size * 8;
	dc->caps.max_slave_planes = 1;
	dc->caps.max_slave_yuv_planes = 1;
	dc->caps.max_slave_rgb_planes = 1;
	dc->caps.post_blend_color_processing = true;
	dc->caps.force_dp_tps4_for_cp2520 = true;
	dc->caps.extended_aux_timeout_support = true;
	dc->caps.dmcub_support = true;

	/* Color pipeline capabilities */
	dc->caps.color.dpp.dcn_arch = 1;
	dc->caps.color.dpp.input_lut_shared = 0;
	dc->caps.color.dpp.icsc = 1;
	dc->caps.color.dpp.dgam_ram = 0; // must use gamma_corr
	dc->caps.color.dpp.dgam_rom_caps.srgb = 1;
	dc->caps.color.dpp.dgam_rom_caps.bt2020 = 1;
	dc->caps.color.dpp.dgam_rom_caps.gamma2_2 = 1;
	dc->caps.color.dpp.dgam_rom_caps.pq = 1;
	dc->caps.color.dpp.dgam_rom_caps.hlg = 1;
	dc->caps.color.dpp.post_csc = 1;
	dc->caps.color.dpp.gamma_corr = 1;
	dc->caps.color.dpp.dgam_rom_for_yuv = 0;

	dc->caps.color.dpp.hw_3d_lut = 1;
	dc->caps.color.dpp.ogam_ram = 1;
	// no OGAM ROM on DCN3
	dc->caps.color.dpp.ogam_rom_caps.srgb = 0;
	dc->caps.color.dpp.ogam_rom_caps.bt2020 = 0;
	dc->caps.color.dpp.ogam_rom_caps.gamma2_2 = 0;
	dc->caps.color.dpp.ogam_rom_caps.pq = 0;
	dc->caps.color.dpp.ogam_rom_caps.hlg = 0;
	dc->caps.color.dpp.ocsc = 0;

	dc->caps.color.mpc.gamut_remap = 1;
	dc->caps.color.mpc.num_3dluts = pool->res_cap->num_mpc_3dlut; //3
	dc->caps.color.mpc.ogam_ram = 1;
	dc->caps.color.mpc.ogam_rom_caps.srgb = 0;
	dc->caps.color.mpc.ogam_rom_caps.bt2020 = 0;
	dc->caps.color.mpc.ogam_rom_caps.gamma2_2 = 0;
	dc->caps.color.mpc.ogam_rom_caps.pq = 0;
	dc->caps.color.mpc.ogam_rom_caps.hlg = 0;
	dc->caps.color.mpc.ocsc = 1;

	if (dc->ctx->dce_environment == DCE_ENV_PRODUCTION_DRV)
		dc->debug = debug_defaults_drv;
	else
		dc->debug = debug_defaults_diags;

	// Init the vm_helper
	if (dc->vm_helper)
		vm_helper_init(dc->vm_helper, 16);

	/*************************************************
	 *  Create resources                             *
	 *************************************************/

	/* Clock Sources for Pixel Clock*/
	pool->clock_sources[DCN302_CLK_SRC_PLL0] =
			dcn302_clock_source_create(ctx, ctx->dc_bios,
					CLOCK_SOURCE_COMBO_PHY_PLL0,
					&clk_src_regs[0], false);
	pool->clock_sources[DCN302_CLK_SRC_PLL1] =
			dcn302_clock_source_create(ctx, ctx->dc_bios,
					CLOCK_SOURCE_COMBO_PHY_PLL1,
					&clk_src_regs[1], false);
	pool->clock_sources[DCN302_CLK_SRC_PLL2] =
			dcn302_clock_source_create(ctx, ctx->dc_bios,
					CLOCK_SOURCE_COMBO_PHY_PLL2,
					&clk_src_regs[2], false);
	pool->clock_sources[DCN302_CLK_SRC_PLL3] =
			dcn302_clock_source_create(ctx, ctx->dc_bios,
					CLOCK_SOURCE_COMBO_PHY_PLL3,
					&clk_src_regs[3], false);
	pool->clock_sources[DCN302_CLK_SRC_PLL4] =
			dcn302_clock_source_create(ctx, ctx->dc_bios,
					CLOCK_SOURCE_COMBO_PHY_PLL4,
					&clk_src_regs[4], false);

	pool->clk_src_count = DCN302_CLK_SRC_TOTAL;

	/* todo: not reuse phy_pll registers */
	pool->dp_clock_source =
			dcn302_clock_source_create(ctx, ctx->dc_bios,
					CLOCK_SOURCE_ID_DP_DTO,
					&clk_src_regs[0], true);

	for (i = 0; i < pool->clk_src_count; i++) {
		if (pool->clock_sources[i] == NULL) {
			dm_error("DC: failed to create clock sources!\n");
			BREAK_TO_DEBUGGER();
			goto create_fail;
		}
	}

	/* DCCG */
	pool->dccg = dccg30_create(ctx, &dccg_regs, &dccg_shift, &dccg_mask);
	if (pool->dccg == NULL) {
		dm_error("DC: failed to create dccg!\n");
		BREAK_TO_DEBUGGER();
		goto create_fail;
	}

	/* PP Lib and SMU interfaces */
	init_soc_bounding_box(dc, pool);

	/* DML */
	dml_init_instance(&dc->dml, &dcn3_02_soc, &dcn3_02_ip, DML_PROJECT_DCN30);

	/* IRQ */
	init_data.ctx = dc->ctx;
	pool->irqs = dal_irq_service_dcn302_create(&init_data);
	if (!pool->irqs)
		goto create_fail;

	/* HUBBUB */
	pool->hubbub = dcn302_hubbub_create(ctx);
	if (pool->hubbub == NULL) {
		BREAK_TO_DEBUGGER();
		dm_error("DC: failed to create hubbub!\n");
		goto create_fail;
	}

	/* HUBPs, DPPs, OPPs and TGs */
	for (i = 0; i < pool->pipe_count; i++) {
		pool->hubps[i] = dcn302_hubp_create(ctx, i);
		if (pool->hubps[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error("DC: failed to create hubps!\n");
			goto create_fail;
		}

		pool->dpps[i] = dcn302_dpp_create(ctx, i);
		if (pool->dpps[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error("DC: failed to create dpps!\n");
			goto create_fail;
		}
	}

	for (i = 0; i < pool->res_cap->num_opp; i++) {
		pool->opps[i] = dcn302_opp_create(ctx, i);
		if (pool->opps[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error("DC: failed to create output pixel processor!\n");
			goto create_fail;
		}
	}

	for (i = 0; i < pool->res_cap->num_timing_generator; i++) {
		pool->timing_generators[i] = dcn302_timing_generator_create(ctx, i);
		if (pool->timing_generators[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error("DC: failed to create tg!\n");
			goto create_fail;
		}
	}
	pool->timing_generator_count = i;

	/* PSR */
	pool->psr = dmub_psr_create(ctx);
	if (pool->psr == NULL) {
		dm_error("DC: failed to create psr!\n");
		BREAK_TO_DEBUGGER();
		goto create_fail;
	}

	/* ABMs */
	for (i = 0; i < pool->res_cap->num_timing_generator; i++) {
		pool->multiple_abms[i] = dmub_abm_create(ctx, &abm_regs[i], &abm_shift, &abm_mask);
		if (pool->multiple_abms[i] == NULL) {
			dm_error("DC: failed to create abm for pipe %d!\n", i);
			BREAK_TO_DEBUGGER();
			goto create_fail;
		}
	}

	/* MPC and DSC */
	pool->mpc = dcn302_mpc_create(ctx, pool->mpcc_count, pool->res_cap->num_mpc_3dlut);
	if (pool->mpc == NULL) {
		BREAK_TO_DEBUGGER();
		dm_error("DC: failed to create mpc!\n");
		goto create_fail;
	}

	for (i = 0; i < pool->res_cap->num_dsc; i++) {
		pool->dscs[i] = dcn302_dsc_create(ctx, i);
		if (pool->dscs[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error("DC: failed to create display stream compressor %d!\n", i);
			goto create_fail;
		}
	}

	/* DWB and MMHUBBUB */
	if (!dcn302_dwbc_create(ctx, pool)) {
		BREAK_TO_DEBUGGER();
		dm_error("DC: failed to create dwbc!\n");
		goto create_fail;
	}

	if (!dcn302_mmhubbub_create(ctx, pool)) {
		BREAK_TO_DEBUGGER();
		dm_error("DC: failed to create mcif_wb!\n");
		goto create_fail;
	}

	/* AUX and I2C */
	for (i = 0; i < pool->res_cap->num_ddc; i++) {
		pool->engines[i] = dcn302_aux_engine_create(ctx, i);
		if (pool->engines[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error("DC:failed to create aux engine!!\n");
			goto create_fail;
		}
		pool->hw_i2cs[i] = dcn302_i2c_hw_create(ctx, i);
		if (pool->hw_i2cs[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error("DC:failed to create hw i2c!!\n");
			goto create_fail;
		}
		pool->sw_i2cs[i] = NULL;
	}

	/* Audio, Stream Encoders including HPO and virtual, MPC 3D LUTs */
	if (!resource_construct(num_virtual_links, dc, pool,
			(!IS_FPGA_MAXIMUS_DC(dc->ctx->dce_environment) ?
					&res_create_funcs : &res_create_maximus_funcs)))
		goto create_fail;

	/* HW Sequencer and Plane caps */
	dcn302_hw_sequencer_construct(dc);

	dc->caps.max_planes =  pool->pipe_count;

	for (i = 0; i < dc->caps.max_planes; ++i)
		dc->caps.planes[i] = plane_cap;

	dc->cap_funcs = cap_funcs;

	return true;

create_fail:

	dcn302_resource_destruct(pool);

	return false;
}

struct resource_pool *dcn302_create_resource_pool(const struct dc_init_data *init_data, struct dc *dc)
{
	struct resource_pool *pool = kzalloc(sizeof(struct resource_pool), GFP_KERNEL);

	if (!pool)
		return NULL;

	if (dcn302_resource_construct(init_data->num_virtual_links, dc, pool))
		return pool;

	BREAK_TO_DEBUGGER();
	kfree(pool);
	return NULL;
}
