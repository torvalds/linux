/*
* Copyright 2016 Advanced Micro Devices, Inc.
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

#include "dm_services.h"
#include "dc.h"

#include "dcn201_init.h"
#include "dml/dcn20/dcn20_fpu.h"
#include "resource.h"
#include "include/irq_service_interface.h"
#include "dcn201_resource.h"

#include "dcn20/dcn20_resource.h"

#include "dcn10/dcn10_hubp.h"
#include "dcn10/dcn10_ipp.h"
#include "dcn201_mpc.h"
#include "dcn201_hubp.h"
#include "irq/dcn201/irq_service_dcn201.h"
#include "dcn201/dcn201_dpp.h"
#include "dcn201/dcn201_hubbub.h"
#include "dcn201_dccg.h"
#include "dcn201_optc.h"
#include "dcn201_hwseq.h"
#include "dce110/dce110_hw_sequencer.h"
#include "dcn201_opp.h"
#include "dcn201/dcn201_link_encoder.h"
#include "dcn20/dcn20_stream_encoder.h"
#include "dce/dce_clock_source.h"
#include "dce/dce_audio.h"
#include "dce/dce_hwseq.h"
#include "virtual/virtual_stream_encoder.h"
#include "dce110/dce110_resource.h"
#include "dce/dce_aux.h"
#include "dce/dce_i2c.h"
#include "dcn201_hubbub.h"
#include "dcn10/dcn10_resource.h"

#include "cyan_skillfish_ip_offset.h"

#include "dcn/dcn_2_0_3_offset.h"
#include "dcn/dcn_2_0_3_sh_mask.h"
#include "dpcs/dpcs_2_0_3_offset.h"
#include "dpcs/dpcs_2_0_3_sh_mask.h"

#include "mmhub/mmhub_2_0_0_offset.h"
#include "mmhub/mmhub_2_0_0_sh_mask.h"
#include "nbio/nbio_7_4_offset.h"

#include "reg_helper.h"

#define MIN_DISP_CLK_KHZ 100000
#define MIN_DPP_CLK_KHZ 100000

struct _vcs_dpi_ip_params_st dcn201_ip = {
	.gpuvm_enable = 0,
	.hostvm_enable = 0,
	.gpuvm_max_page_table_levels = 4,
	.hostvm_max_page_table_levels = 4,
	.hostvm_cached_page_table_levels = 0,
	.pte_group_size_bytes = 2048,
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
	.writeback_line_buffer_chroma_buffer_size = 9600,
	.cursor_buffer_size = 8,
	.cursor_chunk_size = 2,
	.max_num_otg = 2,
	.max_num_dpp = 4,
	.max_num_wb = 0,
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
	.min_vblank_lines = 30,
	.dppclk_delay_subtotal = 77,
	.dppclk_delay_scl_lb_only = 16,
	.dppclk_delay_scl = 50,
	.dppclk_delay_cnvc_formatter = 8,
	.dppclk_delay_cnvc_cursor = 6,
	.dispclk_delay_subtotal = 87,
	.dcfclk_cstate_latency = 10,
	.max_inter_dcn_tile_repeaters = 8,
	.number_of_cursors = 1,
};

struct _vcs_dpi_soc_bounding_box_st dcn201_soc = {
	.clock_limits = {
			{
				.state = 0,
				.dscclk_mhz = 400.0,
				.dcfclk_mhz = 1000.0,
				.fabricclk_mhz = 200.0,
				.dispclk_mhz = 300.0,
				.dppclk_mhz = 300.0,
				.phyclk_mhz = 810.0,
				.socclk_mhz = 1254.0,
				.dram_speed_mts = 2000.0,
			},
			{
				.state = 1,
				.dscclk_mhz = 400.0,
				.dcfclk_mhz = 1000.0,
				.fabricclk_mhz = 250.0,
				.dispclk_mhz = 1200.0,
				.dppclk_mhz = 1200.0,
				.phyclk_mhz = 810.0,
				.socclk_mhz = 1254.0,
				.dram_speed_mts = 3600.0,
			},
			{
				.state = 2,
				.dscclk_mhz = 400.0,
				.dcfclk_mhz = 1000.0,
				.fabricclk_mhz = 750.0,
				.dispclk_mhz = 1200.0,
				.dppclk_mhz = 1200.0,
				.phyclk_mhz = 810.0,
				.socclk_mhz = 1254.0,
				.dram_speed_mts = 6800.0,
			},
			{
				.state = 3,
				.dscclk_mhz = 400.0,
				.dcfclk_mhz = 1000.0,
				.fabricclk_mhz = 250.0,
				.dispclk_mhz = 1200.0,
				.dppclk_mhz = 1200.0,
				.phyclk_mhz = 810.0,
				.socclk_mhz = 1254.0,
				.dram_speed_mts = 14000.0,
			},
			{
				.state = 4,
				.dscclk_mhz = 400.0,
				.dcfclk_mhz = 1000.0,
				.fabricclk_mhz = 750.0,
				.dispclk_mhz = 1200.0,
				.dppclk_mhz = 1200.0,
				.phyclk_mhz = 810.0,
				.socclk_mhz = 1254.0,
				.dram_speed_mts = 14000.0,
			}
		},
	.num_states = 4,
	.sr_exit_time_us = 9.0,
	.sr_enter_plus_exit_time_us = 11.0,
	.urgent_latency_us = 4.0,
	.urgent_latency_pixel_data_only_us = 4.0,
	.urgent_latency_pixel_mixed_with_vm_data_us = 4.0,
	.urgent_latency_vm_data_only_us = 4.0,
	.urgent_out_of_order_return_per_channel_pixel_only_bytes = 256,
	.urgent_out_of_order_return_per_channel_pixel_and_vm_bytes = 256,
	.urgent_out_of_order_return_per_channel_vm_only_bytes = 256,
	.pct_ideal_dram_sdp_bw_after_urgent_pixel_only = 80.0,
	.pct_ideal_dram_sdp_bw_after_urgent_pixel_and_vm = 80.0,
	.pct_ideal_dram_sdp_bw_after_urgent_vm_only = 80.0,
	.max_avg_sdp_bw_use_normal_percent = 80.0,
	.max_avg_dram_bw_use_normal_percent = 69.0,
	.writeback_latency_us = 12.0,
	.ideal_dram_bw_after_urgent_percent = 80.0,
	.max_request_size_bytes = 256,
	.dram_channel_width_bytes = 2,
	.fabric_datapath_to_dcn_data_return_bytes = 64,
	.dcn_downspread_percent = 0.3,
	.downspread_percent = 0.3,
	.dram_page_open_time_ns = 50.0,
	.dram_rw_turnaround_time_ns = 17.5,
	.dram_return_buffer_per_channel_bytes = 8192,
	.round_trip_ping_latency_dcfclk_cycles = 128,
	.urgent_out_of_order_return_per_channel_bytes = 256,
	.channel_interleave_bytes = 256,
	.num_banks = 8,
	.num_chans = 16,
	.vmm_page_size_bytes = 4096,
	.dram_clock_change_latency_us = 250.0,
	.writeback_dram_clock_change_latency_us = 23.0,
	.return_bus_width_bytes = 64,
	.dispclk_dppclk_vco_speed_mhz = 3000,
	.use_urgent_burst_bw = 0,
};

enum dcn20_clk_src_array_id {
	DCN20_CLK_SRC_PLL0,
	DCN20_CLK_SRC_PLL1,
	DCN20_CLK_SRC_TOTAL_DCN201
};

/* begin *********************
 * macros to expend register list macro defined in HW object header file */

/* DCN */

#undef BASE_INNER
#define BASE_INNER(seg) DMU_BASE__INST0_SEG ## seg

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

#define SRI_IX(reg_name, block, id)\
	.reg_name = ix ## block ## id ## _ ## reg_name

#define DCCG_SRII(reg_name, block, id)\
	.block ## _ ## reg_name[id] = BASE(mm ## block ## id ## _ ## reg_name ## _BASE_IDX) + \
					mm ## block ## id ## _ ## reg_name

#define VUPDATE_SRII(reg_name, block, id)\
	.reg_name[id] = BASE(mm ## reg_name ## _ ## block ## id ## _BASE_IDX) + \
					mm ## reg_name ## _ ## block ## id

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
	CS_COMMON_REG_LIST_DCN201(index, pllid),\
}

static const struct dce110_clk_src_regs clk_src_regs[] = {
	clk_src_regs(0, A),
	clk_src_regs(1, B)
};

static const struct dce110_clk_src_shift cs_shift = {
		CS_COMMON_MASK_SH_LIST_DCN2_0(__SHIFT)
};

static const struct dce110_clk_src_mask cs_mask = {
		CS_COMMON_MASK_SH_LIST_DCN2_0(_MASK)
};

#define audio_regs(id)\
[id] = {\
		AUD_COMMON_REG_LIST(id)\
}

static const struct dce_audio_registers audio_regs[] = {
	audio_regs(0),
	audio_regs(1),
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
	stream_enc_regs(1)
};

static const struct dcn10_stream_encoder_shift se_shift = {
		SE_COMMON_MASK_SH_LIST_DCN20(__SHIFT)
};

static const struct dcn10_stream_encoder_mask se_mask = {
		SE_COMMON_MASK_SH_LIST_DCN20(_MASK)
};

static const struct dce110_aux_registers_shift aux_shift = {
	DCN_AUX_MASK_SH_LIST(__SHIFT)
};

static const struct dce110_aux_registers_mask aux_mask = {
	DCN_AUX_MASK_SH_LIST(_MASK)
};

#define aux_regs(id)\
[id] = {\
	DCN2_AUX_REG_LIST(id)\
}

static const struct dcn10_link_enc_aux_registers link_enc_aux_regs[] = {
		aux_regs(0),
		aux_regs(1),
};

#define hpd_regs(id)\
[id] = {\
	HPD_REG_LIST(id)\
}

static const struct dcn10_link_enc_hpd_registers link_enc_hpd_regs[] = {
		hpd_regs(0),
		hpd_regs(1),
};

#define link_regs(id, phyid)\
[id] = {\
	LE_DCN_COMMON_REG_LIST(id), \
	UNIPHY_DCN2_REG_LIST(phyid) \
}

static const struct dcn10_link_enc_registers link_enc_regs[] = {
	link_regs(0, A),
	link_regs(1, B),
};

#define LINK_ENCODER_MASK_SH_LIST_DCN201(mask_sh)\
	LINK_ENCODER_MASK_SH_LIST_DCN20(mask_sh)

static const struct dcn10_link_enc_shift le_shift = {
		LINK_ENCODER_MASK_SH_LIST_DCN201(__SHIFT)
};

static const struct dcn10_link_enc_mask le_mask = {
		LINK_ENCODER_MASK_SH_LIST_DCN201(_MASK)
};

#define ipp_regs(id)\
[id] = {\
		IPP_REG_LIST_DCN201(id),\
}

static const struct dcn10_ipp_registers ipp_regs[] = {
	ipp_regs(0),
	ipp_regs(1),
	ipp_regs(2),
	ipp_regs(3),
};

static const struct dcn10_ipp_shift ipp_shift = {
		IPP_MASK_SH_LIST_DCN201(__SHIFT)
};

static const struct dcn10_ipp_mask ipp_mask = {
		IPP_MASK_SH_LIST_DCN201(_MASK)
};

#define opp_regs(id)\
[id] = {\
	OPP_REG_LIST_DCN201(id),\
}

static const struct dcn201_opp_registers opp_regs[] = {
	opp_regs(0),
	opp_regs(1),
};

static const struct dcn201_opp_shift opp_shift = {
		OPP_MASK_SH_LIST_DCN201(__SHIFT)
};

static const struct dcn201_opp_mask opp_mask = {
		OPP_MASK_SH_LIST_DCN201(_MASK)
};

#define aux_engine_regs(id)\
[id] = {\
	AUX_COMMON_REG_LIST0(id), \
	.AUX_RESET_MASK = 0 \
}

static const struct dce110_aux_registers aux_engine_regs[] = {
		aux_engine_regs(0),
		aux_engine_regs(1)
};

#define tf_regs(id)\
[id] = {\
	TF_REG_LIST_DCN201(id),\
}

static const struct dcn201_dpp_registers tf_regs[] = {
	tf_regs(0),
	tf_regs(1),
	tf_regs(2),
	tf_regs(3),
};

static const struct dcn201_dpp_shift tf_shift = {
		TF_REG_LIST_SH_MASK_DCN201(__SHIFT)
};

static const struct dcn201_dpp_mask tf_mask = {
		TF_REG_LIST_SH_MASK_DCN201(_MASK)
};

static const struct dcn201_mpc_registers mpc_regs = {
		MPC_REG_LIST_DCN201(0),
		MPC_REG_LIST_DCN201(1),
		MPC_REG_LIST_DCN201(2),
		MPC_REG_LIST_DCN201(3),
		MPC_REG_LIST_DCN201(4),
		MPC_OUT_MUX_REG_LIST_DCN201(0),
		MPC_OUT_MUX_REG_LIST_DCN201(1),
};

static const struct dcn201_mpc_shift mpc_shift = {
	MPC_COMMON_MASK_SH_LIST_DCN201(__SHIFT)
};

static const struct dcn201_mpc_mask mpc_mask = {
	MPC_COMMON_MASK_SH_LIST_DCN201(_MASK)
};

#define tg_regs_dcn201(id)\
[id] = {TG_COMMON_REG_LIST_DCN201(id)}

static const struct dcn_optc_registers tg_regs[] = {
	tg_regs_dcn201(0),
	tg_regs_dcn201(1)
};

static const struct dcn_optc_shift tg_shift = {
	TG_COMMON_MASK_SH_LIST_DCN201(__SHIFT)
};

static const struct dcn_optc_mask tg_mask = {
	TG_COMMON_MASK_SH_LIST_DCN201(_MASK)
};

#define hubp_regsDCN201(id)\
[id] = {\
	HUBP_REG_LIST_DCN201(id)\
}

static const struct dcn201_hubp_registers hubp_regs[] = {
		hubp_regsDCN201(0),
		hubp_regsDCN201(1),
		hubp_regsDCN201(2),
		hubp_regsDCN201(3)
};

static const struct dcn201_hubp_shift hubp_shift = {
		HUBP_MASK_SH_LIST_DCN201(__SHIFT)
};

static const struct dcn201_hubp_mask hubp_mask = {
		HUBP_MASK_SH_LIST_DCN201(_MASK)
};

static const struct dcn_hubbub_registers hubbub_reg = {
		HUBBUB_REG_LIST_DCN201(0)
};

static const struct dcn_hubbub_shift hubbub_shift = {
		HUBBUB_MASK_SH_LIST_DCN201(__SHIFT)
};

static const struct dcn_hubbub_mask hubbub_mask = {
		HUBBUB_MASK_SH_LIST_DCN201(_MASK)
};


static const struct dccg_registers dccg_regs = {
		DCCG_COMMON_REG_LIST_DCN_BASE()
};

static const struct dccg_shift dccg_shift = {
		DCCG_COMMON_MASK_SH_LIST_DCN_COMMON_BASE(__SHIFT)
};

static const struct dccg_mask dccg_mask = {
		DCCG_COMMON_MASK_SH_LIST_DCN_COMMON_BASE(_MASK)
};

static const struct resource_caps res_cap_dnc201 = {
		.num_timing_generator = 2,
		.num_opp = 2,
		.num_video_plane = 4,
		.num_audio = 2,
		.num_stream_encoder = 2,
		.num_pll = 2,
		.num_ddc = 2,
};

static const struct dc_plane_cap plane_cap = {
	.type = DC_PLANE_TYPE_DCN_UNIVERSAL,
	.blends_with_above = true,
	.blends_with_below = true,
	.per_pixel_alpha = true,

	.pixel_format_support = {
			.argb8888 = true,
			.nv12 = false,
			.fp16 = true,
			.p010 = false,
	},

	.max_upscale_factor = {
			.argb8888 = 16000,
			.nv12 = 16000,
			.fp16 = 1
	},

	.max_downscale_factor = {
			.argb8888 = 250,
			.nv12 = 250,
			.fp16 = 250
	},
	64,
	64
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
		.az_endpoint_mute_only = true,
		.max_downscale_src_width = 3840,
		.disable_pplib_wm_range = true,
		.scl_reset_length10 = true,
		.sanity_checks = false,
		.underflow_assert_delay_us = 0xFFFFFFFF,
		.enable_tri_buf = false,
};

static void dcn201_dpp_destroy(struct dpp **dpp)
{
	kfree(TO_DCN201_DPP(*dpp));
	*dpp = NULL;
}

static struct dpp *dcn201_dpp_create(
	struct dc_context *ctx,
	uint32_t inst)
{
	struct dcn201_dpp *dpp =
		kzalloc(sizeof(struct dcn201_dpp), GFP_ATOMIC);

	if (!dpp)
		return NULL;

	if (dpp201_construct(dpp, ctx, inst,
			&tf_regs[inst], &tf_shift, &tf_mask))
		return &dpp->base;

	kfree(dpp);
	return NULL;
}

static struct input_pixel_processor *dcn201_ipp_create(
	struct dc_context *ctx, uint32_t inst)
{
	struct dcn10_ipp *ipp =
		kzalloc(sizeof(struct dcn10_ipp), GFP_ATOMIC);

	if (!ipp) {
		return NULL;
	}

	dcn20_ipp_construct(ipp, ctx, inst,
			&ipp_regs[inst], &ipp_shift, &ipp_mask);
	return &ipp->base;
}


static struct output_pixel_processor *dcn201_opp_create(
	struct dc_context *ctx, uint32_t inst)
{
	struct dcn201_opp *opp =
		kzalloc(sizeof(struct dcn201_opp), GFP_ATOMIC);

	if (!opp) {
		return NULL;
	}

	dcn201_opp_construct(opp, ctx, inst,
			&opp_regs[inst], &opp_shift, &opp_mask);
	return &opp->base;
}

static struct dce_aux *dcn201_aux_engine_create(struct dc_context *ctx,
						uint32_t inst)
{
	struct aux_engine_dce110 *aux_engine =
		kzalloc(sizeof(struct aux_engine_dce110), GFP_ATOMIC);

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
};

static const struct dce_i2c_shift i2c_shifts = {
		I2C_COMMON_MASK_SH_LIST_DCN2(__SHIFT)
};

static const struct dce_i2c_mask i2c_masks = {
		I2C_COMMON_MASK_SH_LIST_DCN2(_MASK)
};

static struct dce_i2c_hw *dcn201_i2c_hw_create(struct dc_context *ctx,
					       uint32_t inst)
{
	struct dce_i2c_hw *dce_i2c_hw =
		kzalloc(sizeof(struct dce_i2c_hw), GFP_ATOMIC);

	if (!dce_i2c_hw)
		return NULL;

	dcn2_i2c_hw_construct(dce_i2c_hw, ctx, inst,
				    &i2c_hw_regs[inst], &i2c_shifts, &i2c_masks);

	return dce_i2c_hw;
}

static struct mpc *dcn201_mpc_create(struct dc_context *ctx, uint32_t num_mpcc)
{
	struct dcn201_mpc *mpc201 = kzalloc(sizeof(struct dcn201_mpc),
					    GFP_ATOMIC);

	if (!mpc201)
		return NULL;

	dcn201_mpc_construct(mpc201, ctx,
			&mpc_regs,
			&mpc_shift,
			&mpc_mask,
			num_mpcc);

	return &mpc201->base;
}

static struct hubbub *dcn201_hubbub_create(struct dc_context *ctx)
{
	struct dcn20_hubbub *hubbub = kzalloc(sizeof(struct dcn20_hubbub),
					  GFP_ATOMIC);

	if (!hubbub)
		return NULL;

	hubbub201_construct(hubbub, ctx,
			&hubbub_reg,
			&hubbub_shift,
			&hubbub_mask);

	return &hubbub->base;
}

static struct timing_generator *dcn201_timing_generator_create(
		struct dc_context *ctx,
		uint32_t instance)
{
	struct optc *tgn10 =
		kzalloc(sizeof(struct optc), GFP_ATOMIC);

	if (!tgn10)
		return NULL;

	tgn10->base.inst = instance;
	tgn10->base.ctx = ctx;

	tgn10->tg_regs = &tg_regs[instance];
	tgn10->tg_shift = &tg_shift;
	tgn10->tg_mask = &tg_mask;

	dcn201_timing_generator_init(tgn10);

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

static struct link_encoder *dcn201_link_encoder_create(
	struct dc_context *ctx,
	const struct encoder_init_data *enc_init_data)
{
	struct dcn20_link_encoder *enc20 =
		kzalloc(sizeof(struct dcn20_link_encoder), GFP_ATOMIC);
	struct dcn10_link_encoder *enc10 = &enc20->enc10;

	if (!enc20)
		return NULL;

	dcn201_link_encoder_construct(enc20,
			enc_init_data,
			&link_enc_feature,
			&link_enc_regs[enc_init_data->transmitter],
			&link_enc_aux_regs[enc_init_data->channel - 1],
			&link_enc_hpd_regs[enc_init_data->hpd_source],
			&le_shift,
			&le_mask);

	return &enc10->base;
}

static struct clock_source *dcn201_clock_source_create(
	struct dc_context *ctx,
	struct dc_bios *bios,
	enum clock_source_id id,
	const struct dce110_clk_src_regs *regs,
	bool dp_clk_src)
{
	struct dce110_clk_src *clk_src =
		kzalloc(sizeof(struct dce110_clk_src), GFP_ATOMIC);

	if (!clk_src)
		return NULL;

	if (dce112_clk_src_construct(clk_src, ctx, bios, id,
			regs, &cs_shift, &cs_mask)) {
		clk_src->base.dp_clk_src = dp_clk_src;
		return &clk_src->base;
	}
	kfree(clk_src);
	return NULL;
}

static void read_dce_straps(
	struct dc_context *ctx,
	struct resource_straps *straps)
{
	generic_reg_get(ctx, mmDC_PINSTRAPS + BASE(mmDC_PINSTRAPS_BASE_IDX),

		FN(DC_PINSTRAPS, DC_PINSTRAPS_AUDIO), &straps->dc_pinstraps_audio);
}

static struct audio *dcn201_create_audio(
		struct dc_context *ctx, unsigned int inst)
{
	return dce_audio_create(ctx, inst,
			&audio_regs[inst], &audio_shift, &audio_mask);
}

static struct stream_encoder *dcn201_stream_encoder_create(
	enum engine_id eng_id,
	struct dc_context *ctx)
{
	struct dcn10_stream_encoder *enc1 =
		kzalloc(sizeof(struct dcn10_stream_encoder), GFP_ATOMIC);

	if (!enc1)
		return NULL;

	dcn20_stream_encoder_construct(enc1, ctx, ctx->dc_bios, eng_id,
					&stream_enc_regs[eng_id],
					&se_shift, &se_mask);

	return &enc1->base;
}

static const struct dce_hwseq_registers hwseq_reg = {
		HWSEQ_DCN201_REG_LIST()
};

static const struct dce_hwseq_shift hwseq_shift = {
		HWSEQ_DCN201_MASK_SH_LIST(__SHIFT)
};

static const struct dce_hwseq_mask hwseq_mask = {
		HWSEQ_DCN201_MASK_SH_LIST(_MASK)
};

static struct dce_hwseq *dcn201_hwseq_create(
	struct dc_context *ctx)
{
	struct dce_hwseq *hws = kzalloc(sizeof(struct dce_hwseq), GFP_ATOMIC);

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
	.create_audio = dcn201_create_audio,
	.create_stream_encoder = dcn201_stream_encoder_create,
	.create_hwseq = dcn201_hwseq_create,
};

static const struct resource_create_funcs res_create_maximus_funcs = {
	.read_dce_straps = NULL,
	.create_audio = NULL,
	.create_stream_encoder = NULL,
	.create_hwseq = dcn201_hwseq_create,
};

static void dcn201_clock_source_destroy(struct clock_source **clk_src)
{
	kfree(TO_DCE110_CLK_SRC(*clk_src));
	*clk_src = NULL;
}

static void dcn201_resource_destruct(struct dcn201_resource_pool *pool)
{
	unsigned int i;

	for (i = 0; i < pool->base.stream_enc_count; i++) {
		if (pool->base.stream_enc[i] != NULL) {
			kfree(DCN10STRENC_FROM_STRENC(pool->base.stream_enc[i]));
			pool->base.stream_enc[i] = NULL;
		}
	}


	if (pool->base.mpc != NULL) {
		kfree(TO_DCN201_MPC(pool->base.mpc));
		pool->base.mpc = NULL;
	}

	if (pool->base.hubbub != NULL) {
		kfree(pool->base.hubbub);
		pool->base.hubbub = NULL;
	}

	for (i = 0; i < pool->base.pipe_count; i++) {
		if (pool->base.dpps[i] != NULL)
			dcn201_dpp_destroy(&pool->base.dpps[i]);

		if (pool->base.ipps[i] != NULL)
			pool->base.ipps[i]->funcs->ipp_destroy(&pool->base.ipps[i]);

		if (pool->base.hubps[i] != NULL) {
			kfree(TO_DCN10_HUBP(pool->base.hubps[i]));
			pool->base.hubps[i] = NULL;
		}

		if (pool->base.irqs != NULL) {
			dal_irq_service_destroy(&pool->base.irqs);
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
	for (i = 0; i < pool->base.audio_count; i++) {
		if (pool->base.audios[i])
			dce_aud_destroy(&pool->base.audios[i]);
	}

	for (i = 0; i < pool->base.clk_src_count; i++) {
		if (pool->base.clock_sources[i] != NULL) {
			dcn201_clock_source_destroy(&pool->base.clock_sources[i]);
			pool->base.clock_sources[i] = NULL;
		}
	}

	if (pool->base.dp_clock_source != NULL) {
		dcn201_clock_source_destroy(&pool->base.dp_clock_source);
		pool->base.dp_clock_source = NULL;
	}

	if (pool->base.dccg != NULL)
		dcn_dccg_destroy(&pool->base.dccg);
}

static struct hubp *dcn201_hubp_create(
	struct dc_context *ctx,
	uint32_t inst)
{
	struct dcn201_hubp *hubp201 =
		kzalloc(sizeof(struct dcn201_hubp), GFP_ATOMIC);

	if (!hubp201)
		return NULL;

	if (dcn201_hubp_construct(hubp201, ctx, inst,
			&hubp_regs[inst], &hubp_shift, &hubp_mask))
		return &hubp201->base;

	kfree(hubp201);
	return NULL;
}

static struct pipe_ctx *dcn201_acquire_idle_pipe_for_layer(
		struct dc_state *context,
		const struct resource_pool *pool,
		struct dc_stream_state *stream)
{
	struct resource_context *res_ctx = &context->res_ctx;
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

static bool dcn201_get_dcc_compression_cap(const struct dc *dc,
		const struct dc_dcc_surface_param *input,
		struct dc_surface_dcc_cap *output)
{
	return dc->res_pool->hubbub->funcs->get_dcc_compression_cap(
			dc->res_pool->hubbub,
			input,
			output);
}

static void dcn201_populate_dml_writeback_from_context(struct dc *dc,
						       struct resource_context *res_ctx,
						       display_e2e_pipe_params_st *pipes)
{
	DC_FP_START();
	dcn201_populate_dml_writeback_from_context_fpu(dc, res_ctx, pipes);
	DC_FP_END();
}

static void dcn201_destroy_resource_pool(struct resource_pool **pool)
{
	struct dcn201_resource_pool *dcn201_pool = TO_DCN201_RES_POOL(*pool);

	dcn201_resource_destruct(dcn201_pool);
	kfree(dcn201_pool);
	*pool = NULL;
}

static void dcn201_link_init(struct dc_link *link)
{
	if (link->ctx->dc_bios->integrated_info)
		link->dp_ss_off = !link->ctx->dc_bios->integrated_info->dp_ss_control;
}

static struct dc_cap_funcs cap_funcs = {
	.get_dcc_compression_cap = dcn201_get_dcc_compression_cap,
};

static struct resource_funcs dcn201_res_pool_funcs = {
	.link_init = dcn201_link_init,
	.destroy = dcn201_destroy_resource_pool,
	.link_enc_create = dcn201_link_encoder_create,
	.panel_cntl_create = NULL,
	.validate_bandwidth = dcn20_validate_bandwidth,
	.populate_dml_pipes = dcn20_populate_dml_pipes_from_context,
	.add_stream_to_ctx = dcn20_add_stream_to_ctx,
	.add_dsc_to_stream_resource = NULL,
	.remove_stream_from_ctx = dcn20_remove_stream_from_ctx,
	.acquire_idle_pipe_for_layer = dcn201_acquire_idle_pipe_for_layer,
	.populate_dml_writeback_from_context = dcn201_populate_dml_writeback_from_context,
	.patch_unknown_plane_state = dcn20_patch_unknown_plane_state,
	.set_mcif_arb_params = dcn20_set_mcif_arb_params,
	.find_first_free_match_stream_enc_for_link = dcn10_find_first_free_match_stream_enc_for_link
};

static bool dcn201_resource_construct(
	uint8_t num_virtual_links,
	struct dc *dc,
	struct dcn201_resource_pool *pool)
{
	int i;
	struct dc_context *ctx = dc->ctx;

	ctx->dc_bios->regs = &bios_regs;

	pool->base.res_cap = &res_cap_dnc201;
	pool->base.funcs = &dcn201_res_pool_funcs;

	/*************************************************
	 *  Resource + asic cap harcoding                *
	 *************************************************/
	pool->base.underlay_pipe_index = NO_UNDERLAY_PIPE;

	pool->base.pipe_count = 4;
	pool->base.mpcc_count = 5;
	dc->caps.max_downscale_ratio = 200;
	dc->caps.i2c_speed_in_khz = 100;
	dc->caps.i2c_speed_in_khz_hdcp = 5; /*1.5 w/a applied by default*/
	dc->caps.max_cursor_size = 256;
	dc->caps.min_horizontal_blanking_period = 80;
	dc->caps.dmdata_alloc_size = 2048;

	dc->caps.max_slave_planes = 1;
	dc->caps.max_slave_yuv_planes = 1;
	dc->caps.max_slave_rgb_planes = 1;
	dc->caps.post_blend_color_processing = true;
	dc->caps.force_dp_tps4_for_cp2520 = true;
	dc->caps.extended_aux_timeout_support = true;

	/* Color pipeline capabilities */
	dc->caps.color.dpp.dcn_arch = 1;
	dc->caps.color.dpp.input_lut_shared = 0;
	dc->caps.color.dpp.icsc = 1;
	dc->caps.color.dpp.dgam_ram = 1;
	dc->caps.color.dpp.dgam_rom_caps.srgb = 1;
	dc->caps.color.dpp.dgam_rom_caps.bt2020 = 1;
	dc->caps.color.dpp.dgam_rom_caps.gamma2_2 = 0;
	dc->caps.color.dpp.dgam_rom_caps.pq = 0;
	dc->caps.color.dpp.dgam_rom_caps.hlg = 0;
	dc->caps.color.dpp.post_csc = 0;
	dc->caps.color.dpp.gamma_corr = 0;
	dc->caps.color.dpp.dgam_rom_for_yuv = 1;

	dc->caps.color.dpp.hw_3d_lut = 1;
	dc->caps.color.dpp.ogam_ram = 1;
	// no OGAM ROM on DCN2
	dc->caps.color.dpp.ogam_rom_caps.srgb = 0;
	dc->caps.color.dpp.ogam_rom_caps.bt2020 = 0;
	dc->caps.color.dpp.ogam_rom_caps.gamma2_2 = 0;
	dc->caps.color.dpp.ogam_rom_caps.pq = 0;
	dc->caps.color.dpp.ogam_rom_caps.hlg = 0;
	dc->caps.color.dpp.ocsc = 0;

	dc->caps.color.mpc.gamut_remap = 0;
	dc->caps.color.mpc.num_3dluts = 0;
	dc->caps.color.mpc.shared_3d_lut = 0;
	dc->caps.color.mpc.ogam_ram = 1;
	dc->caps.color.mpc.ogam_rom_caps.srgb = 0;
	dc->caps.color.mpc.ogam_rom_caps.bt2020 = 0;
	dc->caps.color.mpc.ogam_rom_caps.gamma2_2 = 0;
	dc->caps.color.mpc.ogam_rom_caps.pq = 0;
	dc->caps.color.mpc.ogam_rom_caps.hlg = 0;
	dc->caps.color.mpc.ocsc = 1;

	dc->debug = debug_defaults_drv;

	/*a0 only, remove later*/
	dc->work_arounds.no_connect_phy_config  = true;
	dc->work_arounds.dedcn20_305_wa = true;
	/*************************************************
	 *  Create resources                             *
	 *************************************************/

	pool->base.clock_sources[DCN20_CLK_SRC_PLL0] =
			dcn201_clock_source_create(ctx, ctx->dc_bios,
				CLOCK_SOURCE_COMBO_PHY_PLL0,
				&clk_src_regs[0], false);
	pool->base.clock_sources[DCN20_CLK_SRC_PLL1] =
			dcn201_clock_source_create(ctx, ctx->dc_bios,
				CLOCK_SOURCE_COMBO_PHY_PLL1,
				&clk_src_regs[1], false);

	pool->base.clk_src_count = DCN20_CLK_SRC_TOTAL_DCN201;

	/* todo: not reuse phy_pll registers */
	pool->base.dp_clock_source =
			dcn201_clock_source_create(ctx, ctx->dc_bios,
				CLOCK_SOURCE_ID_DP_DTO,
				&clk_src_regs[0], true);

	for (i = 0; i < pool->base.clk_src_count; i++) {
		if (pool->base.clock_sources[i] == NULL) {
			dm_error("DC: failed to create clock sources!\n");
			goto create_fail;
		}
	}

	pool->base.dccg = dccg201_create(ctx, &dccg_regs, &dccg_shift, &dccg_mask);
	if (pool->base.dccg == NULL) {
		dm_error("DC: failed to create dccg!\n");
		goto create_fail;
	}

	dcn201_ip.max_num_otg = pool->base.res_cap->num_timing_generator;
	dcn201_ip.max_num_dpp = pool->base.pipe_count;
	dml_init_instance(&dc->dml, &dcn201_soc, &dcn201_ip, DML_PROJECT_DCN201);
	{
		struct irq_service_init_data init_data;
		init_data.ctx = dc->ctx;
		pool->base.irqs = dal_irq_service_dcn201_create(&init_data);
		if (!pool->base.irqs)
			goto create_fail;
	}

	/* mem input -> ipp -> dpp -> opp -> TG */
	for (i = 0; i < pool->base.pipe_count; i++) {
		pool->base.hubps[i] = dcn201_hubp_create(ctx, i);
		if (pool->base.hubps[i] == NULL) {
			dm_error(
				"DC: failed to create memory input!\n");
			goto create_fail;
		}

		pool->base.ipps[i] = dcn201_ipp_create(ctx, i);
		if (pool->base.ipps[i] == NULL) {
			dm_error(
				"DC: failed to create input pixel processor!\n");
			goto create_fail;
		}

		pool->base.dpps[i] = dcn201_dpp_create(ctx, i);
		if (pool->base.dpps[i] == NULL) {
			dm_error(
				"DC: failed to create dpps!\n");
			goto create_fail;
		}
	}

	for (i = 0; i < pool->base.res_cap->num_opp; i++) {
		pool->base.opps[i] = dcn201_opp_create(ctx, i);
		if (pool->base.opps[i] == NULL) {
			dm_error(
				"DC: failed to create output pixel processor!\n");
			goto create_fail;
		}
	}

	for (i = 0; i < pool->base.res_cap->num_ddc; i++) {
		pool->base.engines[i] = dcn201_aux_engine_create(ctx, i);
		if (pool->base.engines[i] == NULL) {
			dm_error(
				"DC:failed to create aux engine!!\n");
			goto create_fail;
		}
		pool->base.hw_i2cs[i] = dcn201_i2c_hw_create(ctx, i);
		if (pool->base.hw_i2cs[i] == NULL) {
			dm_error(
				"DC:failed to create hw i2c!!\n");
			goto create_fail;
		}
		pool->base.sw_i2cs[i] = NULL;
	}

	for (i = 0; i < pool->base.res_cap->num_timing_generator; i++) {
		pool->base.timing_generators[i] = dcn201_timing_generator_create(
				ctx, i);
		if (pool->base.timing_generators[i] == NULL) {
			dm_error("DC: failed to create tg!\n");
			goto create_fail;
		}
	}

	pool->base.timing_generator_count = i;

	pool->base.mpc = dcn201_mpc_create(ctx, pool->base.mpcc_count);
	if (pool->base.mpc == NULL) {
		dm_error("DC: failed to create mpc!\n");
		goto create_fail;
	}

	pool->base.hubbub = dcn201_hubbub_create(ctx);
	if (pool->base.hubbub == NULL) {
		dm_error("DC: failed to create hubbub!\n");
		goto create_fail;
	}

	if (!resource_construct(num_virtual_links, dc, &pool->base,
			(!IS_FPGA_MAXIMUS_DC(dc->ctx->dce_environment) ?
			&res_create_funcs : &res_create_maximus_funcs)))
			goto create_fail;

	dcn201_hw_sequencer_construct(dc);

	dc->caps.max_planes =  pool->base.pipe_count;

	for (i = 0; i < dc->caps.max_planes; ++i)
		dc->caps.planes[i] = plane_cap;

	dc->cap_funcs = cap_funcs;

	return true;

create_fail:

	dcn201_resource_destruct(pool);

	return false;
}

struct resource_pool *dcn201_create_resource_pool(
		const struct dc_init_data *init_data,
		struct dc *dc)
{
	struct dcn201_resource_pool *pool =
		kzalloc(sizeof(struct dcn201_resource_pool), GFP_ATOMIC);

	if (!pool)
		return NULL;

	if (dcn201_resource_construct(init_data->num_virtual_links, dc, pool))
		return &pool->base;

	kfree(pool);
	return NULL;
}
