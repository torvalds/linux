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

#include <linux/slab.h>

#include "dm_services.h"
#include "dc.h"

#include "dcn10_init.h"

#include "resource.h"
#include "include/irq_service_interface.h"
#include "dcn10_resource.h"
#include "dcn10_ipp.h"
#include "dcn10_mpc.h"
#include "irq/dcn10/irq_service_dcn10.h"
#include "dcn10_dpp.h"
#include "dcn10_optc.h"
#include "dcn10_hw_sequencer.h"
#include "dce110/dce110_hw_sequencer.h"
#include "dcn10_opp.h"
#include "dcn10_link_encoder.h"
#include "dcn10_stream_encoder.h"
#include "dce/dce_clock_source.h"
#include "dce/dce_audio.h"
#include "dce/dce_hwseq.h"
#include "virtual/virtual_stream_encoder.h"
#include "dce110/dce110_resource.h"
#include "dce112/dce112_resource.h"
#include "dcn10_hubp.h"
#include "dcn10_hubbub.h"
#include "dce/dce_panel_cntl.h"

#include "soc15_hw_ip.h"
#include "vega10_ip_offset.h"

#include "dcn/dcn_1_0_offset.h"
#include "dcn/dcn_1_0_sh_mask.h"

#include "nbio/nbio_7_0_offset.h"

#include "mmhub/mmhub_9_1_offset.h"
#include "mmhub/mmhub_9_1_sh_mask.h"

#include "reg_helper.h"
#include "dce/dce_abm.h"
#include "dce/dce_dmcu.h"
#include "dce/dce_aux.h"
#include "dce/dce_i2c.h"

const struct _vcs_dpi_ip_params_st dcn1_0_ip = {
	.rob_buffer_size_kbytes = 64,
	.det_buffer_size_kbytes = 164,
	.dpte_buffer_size_in_pte_reqs_luma = 42,
	.dpp_output_buffer_pixels = 2560,
	.opp_output_buffer_lines = 1,
	.pixel_chunk_size_kbytes = 8,
	.pte_enable = 1,
	.pte_chunk_size_kbytes = 2,
	.meta_chunk_size_kbytes = 2,
	.writeback_chunk_size_kbytes = 2,
	.line_buffer_size_bits = 589824,
	.max_line_buffer_lines = 12,
	.IsLineBufferBppFixed = 0,
	.LineBufferFixedBpp = -1,
	.writeback_luma_buffer_size_kbytes = 12,
	.writeback_chroma_buffer_size_kbytes = 8,
	.max_num_dpp = 4,
	.max_num_wb = 2,
	.max_dchub_pscl_bw_pix_per_clk = 4,
	.max_pscl_lb_bw_pix_per_clk = 2,
	.max_lb_vscl_bw_pix_per_clk = 4,
	.max_vscl_hscl_bw_pix_per_clk = 4,
	.max_hscl_ratio = 4,
	.max_vscl_ratio = 4,
	.hscl_mults = 4,
	.vscl_mults = 4,
	.max_hscl_taps = 8,
	.max_vscl_taps = 8,
	.dispclk_ramp_margin_percent = 1,
	.underscan_factor = 1.10,
	.min_vblank_lines = 14,
	.dppclk_delay_subtotal = 90,
	.dispclk_delay_subtotal = 42,
	.dcfclk_cstate_latency = 10,
	.max_inter_dcn_tile_repeaters = 8,
	.can_vstartup_lines_exceed_vsync_plus_back_porch_lines_minus_one = 0,
	.bug_forcing_LC_req_same_size_fixed = 0,
};

const struct _vcs_dpi_soc_bounding_box_st dcn1_0_soc = {
	.sr_exit_time_us = 9.0,
	.sr_enter_plus_exit_time_us = 11.0,
	.urgent_latency_us = 4.0,
	.writeback_latency_us = 12.0,
	.ideal_dram_bw_after_urgent_percent = 80.0,
	.max_request_size_bytes = 256,
	.downspread_percent = 0.5,
	.dram_page_open_time_ns = 50.0,
	.dram_rw_turnaround_time_ns = 17.5,
	.dram_return_buffer_per_channel_bytes = 8192,
	.round_trip_ping_latency_dcfclk_cycles = 128,
	.urgent_out_of_order_return_per_channel_bytes = 256,
	.channel_interleave_bytes = 256,
	.num_banks = 8,
	.num_chans = 2,
	.vmm_page_size_bytes = 4096,
	.dram_clock_change_latency_us = 17.0,
	.writeback_dram_clock_change_latency_us = 23.0,
	.return_bus_width_bytes = 64,
};

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


enum dcn10_clk_src_array_id {
	DCN10_CLK_SRC_PLL0,
	DCN10_CLK_SRC_PLL1,
	DCN10_CLK_SRC_PLL2,
	DCN10_CLK_SRC_PLL3,
	DCN10_CLK_SRC_TOTAL,
	DCN101_CLK_SRC_TOTAL = DCN10_CLK_SRC_PLL3
};

/* begin *********************
 * macros to expend register list macro defined in HW object header file */

/* DCN */
#define BASE_INNER(seg) \
	DCE_BASE__INST0_SEG ## seg

#define BASE(seg) \
	BASE_INNER(seg)

#define SR(reg_name)\
		.reg_name = BASE(mm ## reg_name ## _BASE_IDX) +  \
					mm ## reg_name

#define SRI(reg_name, block, id)\
	.reg_name = BASE(mm ## block ## id ## _ ## reg_name ## _BASE_IDX) + \
					mm ## block ## id ## _ ## reg_name


#define SRII(reg_name, block, id)\
	.reg_name[id] = BASE(mm ## block ## id ## _ ## reg_name ## _BASE_IDX) + \
					mm ## block ## id ## _ ## reg_name

#define VUPDATE_SRII(reg_name, block, id)\
	.reg_name[id] = BASE(mm ## reg_name ## 0 ## _ ## block ## id ## _BASE_IDX) + \
					mm ## reg_name ## 0 ## _ ## block ## id

/* set field/register/bitfield name */
#define SFRB(field_name, reg_name, bitfield, post_fix)\
	.field_name = reg_name ## __ ## bitfield ## post_fix

/* NBIO */
#define NBIO_BASE_INNER(seg) \
	NBIF_BASE__INST0_SEG ## seg

#define NBIO_BASE(seg) \
	NBIO_BASE_INNER(seg)

#define NBIO_SR(reg_name)\
		.reg_name = NBIO_BASE(mm ## reg_name ## _BASE_IDX) +  \
					mm ## reg_name

/* MMHUB */
#define MMHUB_BASE_INNER(seg) \
	MMHUB_BASE__INST0_SEG ## seg

#define MMHUB_BASE(seg) \
	MMHUB_BASE_INNER(seg)

#define MMHUB_SR(reg_name)\
		.reg_name = MMHUB_BASE(mm ## reg_name ## _BASE_IDX) +  \
					mm ## reg_name

/* macros to expend register list macro defined in HW object header file
 * end *********************/


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
		ABM_DCN10_REG_LIST(0)
};

static const struct dce_abm_shift abm_shift = {
		ABM_MASK_SH_LIST_DCN10(__SHIFT)
};

static const struct dce_abm_mask abm_mask = {
		ABM_MASK_SH_LIST_DCN10(_MASK)
};

#define stream_enc_regs(id)\
[id] = {\
	SE_DCN_REG_LIST(id)\
}

static const struct dcn10_stream_enc_registers stream_enc_regs[] = {
	stream_enc_regs(0),
	stream_enc_regs(1),
	stream_enc_regs(2),
	stream_enc_regs(3),
};

static const struct dcn10_stream_encoder_shift se_shift = {
		SE_COMMON_MASK_SH_LIST_DCN10(__SHIFT)
};

static const struct dcn10_stream_encoder_mask se_mask = {
		SE_COMMON_MASK_SH_LIST_DCN10(_MASK)
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

#define aux_regs(id)\
[id] = {\
	AUX_REG_LIST(id)\
}

static const struct dcn10_link_enc_aux_registers link_enc_aux_regs[] = {
		aux_regs(0),
		aux_regs(1),
		aux_regs(2),
		aux_regs(3)
};

#define hpd_regs(id)\
[id] = {\
	HPD_REG_LIST(id)\
}

static const struct dcn10_link_enc_hpd_registers link_enc_hpd_regs[] = {
		hpd_regs(0),
		hpd_regs(1),
		hpd_regs(2),
		hpd_regs(3)
};

#define link_regs(id)\
[id] = {\
	LE_DCN10_REG_LIST(id), \
	SRI(DP_DPHY_INTERNAL_CTRL, DP, id) \
}

static const struct dcn10_link_enc_registers link_enc_regs[] = {
	link_regs(0),
	link_regs(1),
	link_regs(2),
	link_regs(3)
};

static const struct dcn10_link_enc_shift le_shift = {
		LINK_ENCODER_MASK_SH_LIST_DCN10(__SHIFT)
};

static const struct dcn10_link_enc_mask le_mask = {
		LINK_ENCODER_MASK_SH_LIST_DCN10(_MASK)
};

static const struct dce_panel_cntl_registers panel_cntl_regs[] = {
	{ DCN_PANEL_CNTL_REG_LIST() }
};

static const struct dce_panel_cntl_shift panel_cntl_shift = {
	DCE_PANEL_CNTL_MASK_SH_LIST(__SHIFT)
};

static const struct dce_panel_cntl_mask panel_cntl_mask = {
	DCE_PANEL_CNTL_MASK_SH_LIST(_MASK)
};

static const struct dce110_aux_registers_shift aux_shift = {
	DCN10_AUX_MASK_SH_LIST(__SHIFT)
};

static const struct dce110_aux_registers_mask aux_mask = {
	DCN10_AUX_MASK_SH_LIST(_MASK)
};

#define ipp_regs(id)\
[id] = {\
	IPP_REG_LIST_DCN10(id),\
}

static const struct dcn10_ipp_registers ipp_regs[] = {
	ipp_regs(0),
	ipp_regs(1),
	ipp_regs(2),
	ipp_regs(3),
};

static const struct dcn10_ipp_shift ipp_shift = {
		IPP_MASK_SH_LIST_DCN10(__SHIFT)
};

static const struct dcn10_ipp_mask ipp_mask = {
		IPP_MASK_SH_LIST_DCN10(_MASK),
};

#define opp_regs(id)\
[id] = {\
	OPP_REG_LIST_DCN10(id),\
}

static const struct dcn10_opp_registers opp_regs[] = {
	opp_regs(0),
	opp_regs(1),
	opp_regs(2),
	opp_regs(3),
};

static const struct dcn10_opp_shift opp_shift = {
		OPP_MASK_SH_LIST_DCN10(__SHIFT)
};

static const struct dcn10_opp_mask opp_mask = {
		OPP_MASK_SH_LIST_DCN10(_MASK),
};

#define aux_engine_regs(id)\
[id] = {\
	AUX_COMMON_REG_LIST(id), \
	.AUX_RESET_MASK = 0 \
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
	TF_REG_LIST_DCN10(id),\
}

static const struct dcn_dpp_registers tf_regs[] = {
	tf_regs(0),
	tf_regs(1),
	tf_regs(2),
	tf_regs(3),
};

static const struct dcn_dpp_shift tf_shift = {
	TF_REG_LIST_SH_MASK_DCN10(__SHIFT),
	TF_DEBUG_REG_LIST_SH_DCN10

};

static const struct dcn_dpp_mask tf_mask = {
	TF_REG_LIST_SH_MASK_DCN10(_MASK),
	TF_DEBUG_REG_LIST_MASK_DCN10
};

static const struct dcn_mpc_registers mpc_regs = {
		MPC_COMMON_REG_LIST_DCN1_0(0),
		MPC_COMMON_REG_LIST_DCN1_0(1),
		MPC_COMMON_REG_LIST_DCN1_0(2),
		MPC_COMMON_REG_LIST_DCN1_0(3),
		MPC_OUT_MUX_COMMON_REG_LIST_DCN1_0(0),
		MPC_OUT_MUX_COMMON_REG_LIST_DCN1_0(1),
		MPC_OUT_MUX_COMMON_REG_LIST_DCN1_0(2),
		MPC_OUT_MUX_COMMON_REG_LIST_DCN1_0(3)
};

static const struct dcn_mpc_shift mpc_shift = {
	MPC_COMMON_MASK_SH_LIST_DCN1_0(__SHIFT),\
	SFRB(CUR_VUPDATE_LOCK_SET, CUR0_VUPDATE_LOCK_SET0, CUR0_VUPDATE_LOCK_SET, __SHIFT)
};

static const struct dcn_mpc_mask mpc_mask = {
	MPC_COMMON_MASK_SH_LIST_DCN1_0(_MASK),\
	SFRB(CUR_VUPDATE_LOCK_SET, CUR0_VUPDATE_LOCK_SET0, CUR0_VUPDATE_LOCK_SET, _MASK)
};

#define tg_regs(id)\
[id] = {TG_COMMON_REG_LIST_DCN1_0(id)}

static const struct dcn_optc_registers tg_regs[] = {
	tg_regs(0),
	tg_regs(1),
	tg_regs(2),
	tg_regs(3),
};

static const struct dcn_optc_shift tg_shift = {
	TG_COMMON_MASK_SH_LIST_DCN1_0(__SHIFT)
};

static const struct dcn_optc_mask tg_mask = {
	TG_COMMON_MASK_SH_LIST_DCN1_0(_MASK)
};

static const struct bios_registers bios_regs = {
		NBIO_SR(BIOS_SCRATCH_3),
		NBIO_SR(BIOS_SCRATCH_6)
};

#define hubp_regs(id)\
[id] = {\
	HUBP_REG_LIST_DCN10(id)\
}

static const struct dcn_mi_registers hubp_regs[] = {
	hubp_regs(0),
	hubp_regs(1),
	hubp_regs(2),
	hubp_regs(3),
};

static const struct dcn_mi_shift hubp_shift = {
		HUBP_MASK_SH_LIST_DCN10(__SHIFT)
};

static const struct dcn_mi_mask hubp_mask = {
		HUBP_MASK_SH_LIST_DCN10(_MASK)
};

static const struct dcn_hubbub_registers hubbub_reg = {
		HUBBUB_REG_LIST_DCN10(0)
};

static const struct dcn_hubbub_shift hubbub_shift = {
		HUBBUB_MASK_SH_LIST_DCN10(__SHIFT)
};

static const struct dcn_hubbub_mask hubbub_mask = {
		HUBBUB_MASK_SH_LIST_DCN10(_MASK)
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
	default:
		ASSERT(0);
		return 0;
	}
}

#define clk_src_regs(index, pllid)\
[index] = {\
	CS_COMMON_REG_LIST_DCN1_0(index, pllid),\
}

static const struct dce110_clk_src_regs clk_src_regs[] = {
	clk_src_regs(0, A),
	clk_src_regs(1, B),
	clk_src_regs(2, C),
	clk_src_regs(3, D)
};

static const struct dce110_clk_src_shift cs_shift = {
		CS_COMMON_MASK_SH_LIST_DCN1_0(__SHIFT)
};

static const struct dce110_clk_src_mask cs_mask = {
		CS_COMMON_MASK_SH_LIST_DCN1_0(_MASK)
};

static const struct resource_caps res_cap = {
		.num_timing_generator = 4,
		.num_opp = 4,
		.num_video_plane = 4,
		.num_audio = 4,
		.num_stream_encoder = 4,
		.num_pll = 4,
		.num_ddc = 4,
};

static const struct resource_caps rv2_res_cap = {
		.num_timing_generator = 3,
		.num_opp = 3,
		.num_video_plane = 3,
		.num_audio = 3,
		.num_stream_encoder = 3,
		.num_pll = 3,
		.num_ddc = 4,
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
			.p010 = true
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

static const struct dc_debug_options debug_defaults_drv = {
		.sanity_checks = true,
		.disable_dmcu = false,
		.force_abm_enable = false,
		.timing_trace = false,
		.clock_trace = true,

		/* raven smu dones't allow 0 disp clk,
		 * smu min disp clk limit is 50Mhz
		 * keep min disp clk 100Mhz avoid smu hang
		 */
		.min_disp_clk_khz = 100000,

		.disable_pplib_clock_request = false,
		.disable_pplib_wm_range = false,
		.pplib_wm_report_mode = WM_REPORT_DEFAULT,
		.pipe_split_policy = MPC_SPLIT_DYNAMIC,
		.force_single_disp_pipe_split = true,
		.disable_dcc = DCC_ENABLE,
		.voltage_align_fclk = true,
		.disable_stereo_support = true,
		.vsr_support = true,
		.performance_trace = false,
		.az_endpoint_mute_only = true,
		.recovery_enabled = false, /*enable this by default after testing.*/
		.max_downscale_src_width = 3840,
		.underflow_assert_delay_us = 0xFFFFFFFF,
};

static const struct dc_debug_options debug_defaults_diags = {
		.disable_dmcu = false,
		.force_abm_enable = false,
		.timing_trace = true,
		.clock_trace = true,
		.disable_stutter = true,
		.disable_pplib_clock_request = true,
		.disable_pplib_wm_range = true,
		.underflow_assert_delay_us = 0xFFFFFFFF,
};

static void dcn10_dpp_destroy(struct dpp **dpp)
{
	kfree(TO_DCN10_DPP(*dpp));
	*dpp = NULL;
}

static struct dpp *dcn10_dpp_create(
	struct dc_context *ctx,
	uint32_t inst)
{
	struct dcn10_dpp *dpp =
		kzalloc(sizeof(struct dcn10_dpp), GFP_KERNEL);

	if (!dpp)
		return NULL;

	dpp1_construct(dpp, ctx, inst,
		       &tf_regs[inst], &tf_shift, &tf_mask);
	return &dpp->base;
}

static struct input_pixel_processor *dcn10_ipp_create(
	struct dc_context *ctx, uint32_t inst)
{
	struct dcn10_ipp *ipp =
		kzalloc(sizeof(struct dcn10_ipp), GFP_KERNEL);

	if (!ipp) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	dcn10_ipp_construct(ipp, ctx, inst,
			&ipp_regs[inst], &ipp_shift, &ipp_mask);
	return &ipp->base;
}


static struct output_pixel_processor *dcn10_opp_create(
	struct dc_context *ctx, uint32_t inst)
{
	struct dcn10_opp *opp =
		kzalloc(sizeof(struct dcn10_opp), GFP_KERNEL);

	if (!opp) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	dcn10_opp_construct(opp, ctx, inst,
			&opp_regs[inst], &opp_shift, &opp_mask);
	return &opp->base;
}

struct dce_aux *dcn10_aux_engine_create(
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
		I2C_COMMON_MASK_SH_LIST_DCE110(__SHIFT)
};

static const struct dce_i2c_mask i2c_masks = {
		I2C_COMMON_MASK_SH_LIST_DCE110(_MASK)
};

struct dce_i2c_hw *dcn10_i2c_hw_create(
	struct dc_context *ctx,
	uint32_t inst)
{
	struct dce_i2c_hw *dce_i2c_hw =
		kzalloc(sizeof(struct dce_i2c_hw), GFP_KERNEL);

	if (!dce_i2c_hw)
		return NULL;

	dcn1_i2c_hw_construct(dce_i2c_hw, ctx, inst,
				    &i2c_hw_regs[inst], &i2c_shifts, &i2c_masks);

	return dce_i2c_hw;
}
static struct mpc *dcn10_mpc_create(struct dc_context *ctx)
{
	struct dcn10_mpc *mpc10 = kzalloc(sizeof(struct dcn10_mpc),
					  GFP_KERNEL);

	if (!mpc10)
		return NULL;

	dcn10_mpc_construct(mpc10, ctx,
			&mpc_regs,
			&mpc_shift,
			&mpc_mask,
			4);

	return &mpc10->base;
}

static struct hubbub *dcn10_hubbub_create(struct dc_context *ctx)
{
	struct dcn10_hubbub *dcn10_hubbub = kzalloc(sizeof(struct dcn10_hubbub),
					  GFP_KERNEL);

	if (!dcn10_hubbub)
		return NULL;

	hubbub1_construct(&dcn10_hubbub->base, ctx,
			&hubbub_reg,
			&hubbub_shift,
			&hubbub_mask);

	return &dcn10_hubbub->base;
}

static struct timing_generator *dcn10_timing_generator_create(
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

	dcn10_timing_generator_init(tgn10);

	return &tgn10->base;
}

static const struct encoder_feature_support link_enc_feature = {
		.max_hdmi_deep_color = COLOR_DEPTH_121212,
		.max_hdmi_pixel_clock = 600000,
		.hdmi_ycbcr420_supported = true,
		.dp_ycbcr420_supported = true,
		.flags.bits.IS_HBR2_CAPABLE = true,
		.flags.bits.IS_HBR3_CAPABLE = true,
		.flags.bits.IS_TPS3_CAPABLE = true,
		.flags.bits.IS_TPS4_CAPABLE = true
};

struct link_encoder *dcn10_link_encoder_create(
	const struct encoder_init_data *enc_init_data)
{
	struct dcn10_link_encoder *enc10 =
		kzalloc(sizeof(struct dcn10_link_encoder), GFP_KERNEL);
	int link_regs_id;

	if (!enc10)
		return NULL;

	link_regs_id =
		map_transmitter_id_to_phy_instance(enc_init_data->transmitter);

	dcn10_link_encoder_construct(enc10,
				      enc_init_data,
				      &link_enc_feature,
				      &link_enc_regs[link_regs_id],
				      &link_enc_aux_regs[enc_init_data->channel - 1],
				      &link_enc_hpd_regs[enc_init_data->hpd_source],
				      &le_shift,
				      &le_mask);

	return &enc10->base;
}

static struct panel_cntl *dcn10_panel_cntl_create(const struct panel_cntl_init_data *init_data)
{
	struct dce_panel_cntl *panel_cntl =
		kzalloc(sizeof(struct dce_panel_cntl), GFP_KERNEL);

	if (!panel_cntl)
		return NULL;

	dce_panel_cntl_construct(panel_cntl,
			init_data,
			&panel_cntl_regs[init_data->inst],
			&panel_cntl_shift,
			&panel_cntl_mask);

	return &panel_cntl->base;
}

struct clock_source *dcn10_clock_source_create(
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

	if (dce112_clk_src_construct(clk_src, ctx, bios, id,
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

static struct audio *create_audio(
		struct dc_context *ctx, unsigned int inst)
{
	return dce_audio_create(ctx, inst,
			&audio_regs[inst], &audio_shift, &audio_mask);
}

static struct stream_encoder *dcn10_stream_encoder_create(
	enum engine_id eng_id,
	struct dc_context *ctx)
{
	struct dcn10_stream_encoder *enc1 =
		kzalloc(sizeof(struct dcn10_stream_encoder), GFP_KERNEL);

	if (!enc1)
		return NULL;

	dcn10_stream_encoder_construct(enc1, ctx, ctx->dc_bios, eng_id,
					&stream_enc_regs[eng_id],
					&se_shift, &se_mask);
	return &enc1->base;
}

static const struct dce_hwseq_registers hwseq_reg = {
		HWSEQ_DCN1_REG_LIST()
};

static const struct dce_hwseq_shift hwseq_shift = {
		HWSEQ_DCN1_MASK_SH_LIST(__SHIFT)
};

static const struct dce_hwseq_mask hwseq_mask = {
		HWSEQ_DCN1_MASK_SH_LIST(_MASK)
};

static struct dce_hwseq *dcn10_hwseq_create(
	struct dc_context *ctx)
{
	struct dce_hwseq *hws = kzalloc(sizeof(struct dce_hwseq), GFP_KERNEL);

	if (hws) {
		hws->ctx = ctx;
		hws->regs = &hwseq_reg;
		hws->shifts = &hwseq_shift;
		hws->masks = &hwseq_mask;
		hws->wa.DEGVIDCN10_253 = true;
		hws->wa.false_optc_underflow = true;
		hws->wa.DEGVIDCN10_254 = true;
	}
	return hws;
}

static const struct resource_create_funcs res_create_funcs = {
	.read_dce_straps = read_dce_straps,
	.create_audio = create_audio,
	.create_stream_encoder = dcn10_stream_encoder_create,
	.create_hwseq = dcn10_hwseq_create,
};

static const struct resource_create_funcs res_create_maximus_funcs = {
	.read_dce_straps = NULL,
	.create_audio = NULL,
	.create_stream_encoder = NULL,
	.create_hwseq = dcn10_hwseq_create,
};

void dcn10_clock_source_destroy(struct clock_source **clk_src)
{
	kfree(TO_DCE110_CLK_SRC(*clk_src));
	*clk_src = NULL;
}

static struct pp_smu_funcs *dcn10_pp_smu_create(struct dc_context *ctx)
{
	struct pp_smu_funcs *pp_smu = kzalloc(sizeof(*pp_smu), GFP_KERNEL);

	if (!pp_smu)
		return pp_smu;

	dm_pp_get_funcs(ctx, pp_smu);
	return pp_smu;
}

static void dcn10_resource_destruct(struct dcn10_resource_pool *pool)
{
	unsigned int i;

	for (i = 0; i < pool->base.stream_enc_count; i++) {
		if (pool->base.stream_enc[i] != NULL) {
			kfree(DCN10STRENC_FROM_STRENC(pool->base.stream_enc[i]));
			pool->base.stream_enc[i] = NULL;
		}
	}

	if (pool->base.mpc != NULL) {
		kfree(TO_DCN10_MPC(pool->base.mpc));
		pool->base.mpc = NULL;
	}

	if (pool->base.hubbub != NULL) {
		kfree(pool->base.hubbub);
		pool->base.hubbub = NULL;
	}

	for (i = 0; i < pool->base.pipe_count; i++) {
		if (pool->base.opps[i] != NULL)
			pool->base.opps[i]->funcs->opp_destroy(&pool->base.opps[i]);

		if (pool->base.dpps[i] != NULL)
			dcn10_dpp_destroy(&pool->base.dpps[i]);

		if (pool->base.ipps[i] != NULL)
			pool->base.ipps[i]->funcs->ipp_destroy(&pool->base.ipps[i]);

		if (pool->base.hubps[i] != NULL) {
			kfree(TO_DCN10_HUBP(pool->base.hubps[i]));
			pool->base.hubps[i] = NULL;
		}

		if (pool->base.irqs != NULL) {
			dal_irq_service_destroy(&pool->base.irqs);
		}

		if (pool->base.timing_generators[i] != NULL)	{
			kfree(DCN10TG_FROM_TG(pool->base.timing_generators[i]));
			pool->base.timing_generators[i] = NULL;
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

	for (i = 0; i < pool->base.audio_count; i++) {
		if (pool->base.audios[i])
			dce_aud_destroy(&pool->base.audios[i]);
	}

	for (i = 0; i < pool->base.clk_src_count; i++) {
		if (pool->base.clock_sources[i] != NULL) {
			dcn10_clock_source_destroy(&pool->base.clock_sources[i]);
			pool->base.clock_sources[i] = NULL;
		}
	}

	if (pool->base.dp_clock_source != NULL) {
		dcn10_clock_source_destroy(&pool->base.dp_clock_source);
		pool->base.dp_clock_source = NULL;
	}

	if (pool->base.abm != NULL)
		dce_abm_destroy(&pool->base.abm);

	if (pool->base.dmcu != NULL)
		dce_dmcu_destroy(&pool->base.dmcu);

	kfree(pool->base.pp_smu);
}

static struct hubp *dcn10_hubp_create(
	struct dc_context *ctx,
	uint32_t inst)
{
	struct dcn10_hubp *hubp1 =
		kzalloc(sizeof(struct dcn10_hubp), GFP_KERNEL);

	if (!hubp1)
		return NULL;

	dcn10_hubp_construct(hubp1, ctx, inst,
			     &hubp_regs[inst], &hubp_shift, &hubp_mask);
	return &hubp1->base;
}

static void get_pixel_clock_parameters(
	const struct pipe_ctx *pipe_ctx,
	struct pixel_clk_params *pixel_clk_params)
{
	const struct dc_stream_state *stream = pipe_ctx->stream;
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

	if (stream->timing.pixel_encoding == PIXEL_ENCODING_YCBCR420)
		pixel_clk_params->requested_pix_clk_100hz  /= 2;
	if (stream->timing.timing_3d_format == TIMING_3D_FORMAT_HW_FRAME_PACKING)
		pixel_clk_params->requested_pix_clk_100hz *= 2;

}

static void build_clamping_params(struct dc_stream_state *stream)
{
	stream->clamping.clamping_level = CLAMPING_FULL_RANGE;
	stream->clamping.c_depth = stream->timing.display_color_depth;
	stream->clamping.pixel_encoding = stream->timing.pixel_encoding;
}

static void build_pipe_hw_param(struct pipe_ctx *pipe_ctx)
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
}

static enum dc_status build_mapped_resource(
		const struct dc *dc,
		struct dc_state *context,
		struct dc_stream_state *stream)
{
	struct pipe_ctx *pipe_ctx = resource_get_head_pipe_for_stream(&context->res_ctx, stream);

	if (!pipe_ctx)
		return DC_ERROR_UNEXPECTED;

	build_pipe_hw_param(pipe_ctx);
	return DC_OK;
}

enum dc_status dcn10_add_stream_to_ctx(
		struct dc *dc,
		struct dc_state *new_ctx,
		struct dc_stream_state *dc_stream)
{
	enum dc_status result = DC_ERROR_UNEXPECTED;

	result = resource_map_pool_resources(dc, new_ctx, dc_stream);

	if (result == DC_OK)
		result = resource_map_phy_clock_resources(dc, new_ctx, dc_stream);


	if (result == DC_OK)
		result = build_mapped_resource(dc, new_ctx, dc_stream);

	return result;
}

static struct pipe_ctx *dcn10_acquire_idle_pipe_for_layer(
		struct dc_state *context,
		const struct resource_pool *pool,
		struct dc_stream_state *stream)
{
	struct resource_context *res_ctx = &context->res_ctx;
	struct pipe_ctx *head_pipe = resource_get_head_pipe_for_stream(res_ctx, stream);
	struct pipe_ctx *idle_pipe = find_idle_secondary_pipe(res_ctx, pool, head_pipe);

	if (!head_pipe) {
		ASSERT(0);
		return NULL;
	}

	if (!idle_pipe)
		return NULL;

	idle_pipe->stream = head_pipe->stream;
	idle_pipe->stream_res.tg = head_pipe->stream_res.tg;
	idle_pipe->stream_res.abm = head_pipe->stream_res.abm;
	idle_pipe->stream_res.opp = head_pipe->stream_res.opp;

	idle_pipe->plane_res.hubp = pool->hubps[idle_pipe->pipe_idx];
	idle_pipe->plane_res.ipp = pool->ipps[idle_pipe->pipe_idx];
	idle_pipe->plane_res.dpp = pool->dpps[idle_pipe->pipe_idx];
	idle_pipe->plane_res.mpcc_inst = pool->dpps[idle_pipe->pipe_idx]->inst;

	return idle_pipe;
}

static bool dcn10_get_dcc_compression_cap(const struct dc *dc,
		const struct dc_dcc_surface_param *input,
		struct dc_surface_dcc_cap *output)
{
	return dc->res_pool->hubbub->funcs->get_dcc_compression_cap(
			dc->res_pool->hubbub,
			input,
			output);
}

static void dcn10_destroy_resource_pool(struct resource_pool **pool)
{
	struct dcn10_resource_pool *dcn10_pool = TO_DCN10_RES_POOL(*pool);

	dcn10_resource_destruct(dcn10_pool);
	kfree(dcn10_pool);
	*pool = NULL;
}

static enum dc_status dcn10_validate_plane(const struct dc_plane_state *plane_state, struct dc_caps *caps)
{
	if (plane_state->format >= SURFACE_PIXEL_FORMAT_VIDEO_BEGIN
			&& caps->max_video_width != 0
			&& plane_state->src_rect.width > caps->max_video_width)
		return DC_FAIL_SURFACE_VALIDATE;

	return DC_OK;
}

static enum dc_status dcn10_validate_global(struct dc *dc, struct dc_state *context)
{
	int i, j;
	bool video_down_scaled = false;
	bool video_large = false;
	bool desktop_large = false;
	bool dcc_disabled = false;
	bool mpo_enabled = false;

	for (i = 0; i < context->stream_count; i++) {
		if (context->stream_status[i].plane_count == 0)
			continue;

		if (context->stream_status[i].plane_count > 2)
			return DC_FAIL_UNSUPPORTED_1;

		if (context->stream_status[i].plane_count > 1)
			mpo_enabled = true;

		for (j = 0; j < context->stream_status[i].plane_count; j++) {
			struct dc_plane_state *plane =
				context->stream_status[i].plane_states[j];


			if (plane->format >= SURFACE_PIXEL_FORMAT_VIDEO_BEGIN) {

				if (plane->src_rect.width > plane->dst_rect.width ||
						plane->src_rect.height > plane->dst_rect.height)
					video_down_scaled = true;

				if (plane->src_rect.width >= 3840)
					video_large = true;

			} else {
				if (plane->src_rect.width >= 3840)
					desktop_large = true;
				if (!plane->dcc.enable)
					dcc_disabled = true;
			}
		}
	}

	/* Disable MPO in multi-display configurations. */
	if (context->stream_count > 1 && mpo_enabled)
		return DC_FAIL_UNSUPPORTED_1;

	/*
	 * Workaround: On DCN10 there is UMC issue that causes underflow when
	 * playing 4k video on 4k desktop with video downscaled and single channel
	 * memory
	 */
	if (video_large && desktop_large && video_down_scaled && dcc_disabled &&
			dc->dcn_soc->number_of_channels == 1)
		return DC_FAIL_SURFACE_VALIDATE;

	return DC_OK;
}

static enum dc_status dcn10_patch_unknown_plane_state(struct dc_plane_state *plane_state)
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

struct stream_encoder *dcn10_find_first_free_match_stream_enc_for_link(
		struct resource_context *res_ctx,
		const struct resource_pool *pool,
		struct dc_stream_state *stream)
{
	int i;
	int j = -1;
	struct dc_link *link = stream->link;

	for (i = 0; i < pool->stream_enc_count; i++) {
		if (!res_ctx->is_stream_enc_acquired[i] &&
				pool->stream_enc[i]) {
			/* Store first available for MST second display
			 * in daisy chain use case
			 */
			j = i;
			if (pool->stream_enc[i]->id ==
					link->link_enc->preferred_engine)
				return pool->stream_enc[i];
		}
	}

	/*
	 * For CZ and later, we can allow DIG FE and BE to differ for all display types
	 */

	if (j >= 0)
		return pool->stream_enc[j];

	return NULL;
}

static const struct dc_cap_funcs cap_funcs = {
	.get_dcc_compression_cap = dcn10_get_dcc_compression_cap
};

static const struct resource_funcs dcn10_res_pool_funcs = {
	.destroy = dcn10_destroy_resource_pool,
	.link_enc_create = dcn10_link_encoder_create,
	.panel_cntl_create = dcn10_panel_cntl_create,
	.validate_bandwidth = dcn_validate_bandwidth,
	.acquire_idle_pipe_for_layer = dcn10_acquire_idle_pipe_for_layer,
	.validate_plane = dcn10_validate_plane,
	.validate_global = dcn10_validate_global,
	.add_stream_to_ctx = dcn10_add_stream_to_ctx,
	.patch_unknown_plane_state = dcn10_patch_unknown_plane_state,
	.find_first_free_match_stream_enc_for_link = dcn10_find_first_free_match_stream_enc_for_link
};

static uint32_t read_pipe_fuses(struct dc_context *ctx)
{
	uint32_t value = dm_read_reg_soc15(ctx, mmCC_DC_PIPE_DIS, 0);
	/* RV1 support max 4 pipes */
	value = value & 0xf;
	return value;
}

/*
 * Some architectures don't support soft-float (e.g. aarch64), on those
 * this function has to be called with hardfloat enabled, make sure not
 * to inline it so whatever fp stuff is done stays inside
 */
static noinline void dcn10_resource_construct_fp(
	struct dc *dc)
{
	if (dc->ctx->dce_version == DCN_VERSION_1_01) {
		struct dcn_soc_bounding_box *dcn_soc = dc->dcn_soc;
		struct dcn_ip_params *dcn_ip = dc->dcn_ip;
		struct display_mode_lib *dml = &dc->dml;

		dml->ip.max_num_dpp = 3;
		/* TODO how to handle 23.84? */
		dcn_soc->dram_clock_change_latency = 23;
		dcn_ip->max_num_dpp = 3;
	}
	if (ASICREV_IS_RV1_F0(dc->ctx->asic_id.hw_internal_rev)) {
		dc->dcn_soc->urgent_latency = 3;
		dc->debug.disable_dmcu = true;
		dc->dcn_soc->fabric_and_dram_bandwidth_vmax0p9 = 41.60f;
	}


	dc->dcn_soc->number_of_channels = dc->ctx->asic_id.vram_width / ddr4_dram_width;
	ASSERT(dc->dcn_soc->number_of_channels < 3);
	if (dc->dcn_soc->number_of_channels == 0)/*old sbios bug*/
		dc->dcn_soc->number_of_channels = 2;

	if (dc->dcn_soc->number_of_channels == 1) {
		dc->dcn_soc->fabric_and_dram_bandwidth_vmax0p9 = 19.2f;
		dc->dcn_soc->fabric_and_dram_bandwidth_vnom0p8 = 17.066f;
		dc->dcn_soc->fabric_and_dram_bandwidth_vmid0p72 = 14.933f;
		dc->dcn_soc->fabric_and_dram_bandwidth_vmin0p65 = 12.8f;
		if (ASICREV_IS_RV1_F0(dc->ctx->asic_id.hw_internal_rev)) {
			dc->dcn_soc->fabric_and_dram_bandwidth_vmax0p9 = 20.80f;
		}
	}
}

static bool dcn10_resource_construct(
	uint8_t num_virtual_links,
	struct dc *dc,
	struct dcn10_resource_pool *pool)
{
	int i;
	int j;
	struct dc_context *ctx = dc->ctx;
	uint32_t pipe_fuses = read_pipe_fuses(ctx);

	ctx->dc_bios->regs = &bios_regs;

	if (ctx->dce_version == DCN_VERSION_1_01)
		pool->base.res_cap = &rv2_res_cap;
	else
		pool->base.res_cap = &res_cap;
	pool->base.funcs = &dcn10_res_pool_funcs;

	/*
	 * TODO fill in from actual raven resource when we create
	 * more than virtual encoder
	 */

	/*************************************************
	 *  Resource + asic cap harcoding                *
	 *************************************************/
	pool->base.underlay_pipe_index = NO_UNDERLAY_PIPE;

	/* max pipe num for ASIC before check pipe fuses */
	pool->base.pipe_count = pool->base.res_cap->num_timing_generator;

	if (dc->ctx->dce_version == DCN_VERSION_1_01)
		pool->base.pipe_count = 3;
	dc->caps.max_video_width = 3840;
	dc->caps.max_downscale_ratio = 200;
	dc->caps.i2c_speed_in_khz = 100;
	dc->caps.max_cursor_size = 256;
	dc->caps.max_slave_planes = 1;
	dc->caps.is_apu = true;
	dc->caps.post_blend_color_processing = false;
	dc->caps.extended_aux_timeout_support = false;

	/* Raven DP PHY HBR2 eye diagram pattern is not stable. Use TP4 */
	dc->caps.force_dp_tps4_for_cp2520 = true;

	/* Color pipeline capabilities */
	dc->caps.color.dpp.dcn_arch = 1;
	dc->caps.color.dpp.input_lut_shared = 1;
	dc->caps.color.dpp.icsc = 1;
	dc->caps.color.dpp.dgam_ram = 1;
	dc->caps.color.dpp.dgam_rom_caps.srgb = 1;
	dc->caps.color.dpp.dgam_rom_caps.bt2020 = 1;
	dc->caps.color.dpp.dgam_rom_caps.gamma2_2 = 0;
	dc->caps.color.dpp.dgam_rom_caps.pq = 0;
	dc->caps.color.dpp.dgam_rom_caps.hlg = 0;
	dc->caps.color.dpp.post_csc = 0;
	dc->caps.color.dpp.gamma_corr = 0;

	dc->caps.color.dpp.hw_3d_lut = 0;
	dc->caps.color.dpp.ogam_ram = 1; // RGAM on DCN1
	dc->caps.color.dpp.ogam_rom_caps.srgb = 1;
	dc->caps.color.dpp.ogam_rom_caps.bt2020 = 1;
	dc->caps.color.dpp.ogam_rom_caps.gamma2_2 = 0;
	dc->caps.color.dpp.ogam_rom_caps.pq = 0;
	dc->caps.color.dpp.ogam_rom_caps.hlg = 0;
	dc->caps.color.dpp.ocsc = 1;

	/* no post-blend color operations */
	dc->caps.color.mpc.gamut_remap = 0;
	dc->caps.color.mpc.num_3dluts = 0;
	dc->caps.color.mpc.shared_3d_lut = 0;
	dc->caps.color.mpc.ogam_ram = 0;
	dc->caps.color.mpc.ogam_rom_caps.srgb = 0;
	dc->caps.color.mpc.ogam_rom_caps.bt2020 = 0;
	dc->caps.color.mpc.ogam_rom_caps.gamma2_2 = 0;
	dc->caps.color.mpc.ogam_rom_caps.pq = 0;
	dc->caps.color.mpc.ogam_rom_caps.hlg = 0;
	dc->caps.color.mpc.ocsc = 0;

	if (dc->ctx->dce_environment == DCE_ENV_PRODUCTION_DRV)
		dc->debug = debug_defaults_drv;
	else
		dc->debug = debug_defaults_diags;

	/*************************************************
	 *  Create resources                             *
	 *************************************************/

	pool->base.clock_sources[DCN10_CLK_SRC_PLL0] =
			dcn10_clock_source_create(ctx, ctx->dc_bios,
				CLOCK_SOURCE_COMBO_PHY_PLL0,
				&clk_src_regs[0], false);
	pool->base.clock_sources[DCN10_CLK_SRC_PLL1] =
			dcn10_clock_source_create(ctx, ctx->dc_bios,
				CLOCK_SOURCE_COMBO_PHY_PLL1,
				&clk_src_regs[1], false);
	pool->base.clock_sources[DCN10_CLK_SRC_PLL2] =
			dcn10_clock_source_create(ctx, ctx->dc_bios,
				CLOCK_SOURCE_COMBO_PHY_PLL2,
				&clk_src_regs[2], false);

	if (dc->ctx->dce_version == DCN_VERSION_1_0) {
		pool->base.clock_sources[DCN10_CLK_SRC_PLL3] =
				dcn10_clock_source_create(ctx, ctx->dc_bios,
					CLOCK_SOURCE_COMBO_PHY_PLL3,
					&clk_src_regs[3], false);
	}

	pool->base.clk_src_count = DCN10_CLK_SRC_TOTAL;

	if (dc->ctx->dce_version == DCN_VERSION_1_01)
		pool->base.clk_src_count = DCN101_CLK_SRC_TOTAL;

	pool->base.dp_clock_source =
			dcn10_clock_source_create(ctx, ctx->dc_bios,
				CLOCK_SOURCE_ID_DP_DTO,
				/* todo: not reuse phy_pll registers */
				&clk_src_regs[0], true);

	for (i = 0; i < pool->base.clk_src_count; i++) {
		if (pool->base.clock_sources[i] == NULL) {
			dm_error("DC: failed to create clock sources!\n");
			BREAK_TO_DEBUGGER();
			goto fail;
		}
	}

	pool->base.dmcu = dcn10_dmcu_create(ctx,
			&dmcu_regs,
			&dmcu_shift,
			&dmcu_mask);
	if (pool->base.dmcu == NULL) {
		dm_error("DC: failed to create dmcu!\n");
		BREAK_TO_DEBUGGER();
		goto fail;
	}

	pool->base.abm = dce_abm_create(ctx,
			&abm_regs,
			&abm_shift,
			&abm_mask);
	if (pool->base.abm == NULL) {
		dm_error("DC: failed to create abm!\n");
		BREAK_TO_DEBUGGER();
		goto fail;
	}

	dml_init_instance(&dc->dml, &dcn1_0_soc, &dcn1_0_ip, DML_PROJECT_RAVEN1);
	memcpy(dc->dcn_ip, &dcn10_ip_defaults, sizeof(dcn10_ip_defaults));
	memcpy(dc->dcn_soc, &dcn10_soc_defaults, sizeof(dcn10_soc_defaults));

#if defined(CONFIG_ARM64)
	/* Aarch64 does not support -msoft-float/-mfloat-abi=soft */
	DC_FP_START();
	dcn10_resource_construct_fp(dc);
	DC_FP_END();
#else
	/* Other architectures we build for build this with soft-float */
	dcn10_resource_construct_fp(dc);
#endif

	pool->base.pp_smu = dcn10_pp_smu_create(ctx);

	/*
	 * Right now SMU/PPLIB and DAL all have the AZ D3 force PME notification *
	 * implemented. So AZ D3 should work.For issue 197007.                   *
	 */
	if (pool->base.pp_smu != NULL
			&& pool->base.pp_smu->rv_funcs.set_pme_wa_enable != NULL)
		dc->debug.az_endpoint_mute_only = false;

	if (!dc->debug.disable_pplib_clock_request)
		dcn_bw_update_from_pplib(dc);
	dcn_bw_sync_calcs_and_dml(dc);
	if (!dc->debug.disable_pplib_wm_range) {
		dc->res_pool = &pool->base;
		dcn_bw_notify_pplib_of_wm_ranges(dc);
	}

	{
		struct irq_service_init_data init_data;
		init_data.ctx = dc->ctx;
		pool->base.irqs = dal_irq_service_dcn10_create(&init_data);
		if (!pool->base.irqs)
			goto fail;
	}

	/* index to valid pipe resource  */
	j = 0;
	/* mem input -> ipp -> dpp -> opp -> TG */
	for (i = 0; i < pool->base.pipe_count; i++) {
		/* if pipe is disabled, skip instance of HW pipe,
		 * i.e, skip ASIC register instance
		 */
		if ((pipe_fuses & (1 << i)) != 0)
			continue;

		pool->base.hubps[j] = dcn10_hubp_create(ctx, i);
		if (pool->base.hubps[j] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC: failed to create memory input!\n");
			goto fail;
		}

		pool->base.ipps[j] = dcn10_ipp_create(ctx, i);
		if (pool->base.ipps[j] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC: failed to create input pixel processor!\n");
			goto fail;
		}

		pool->base.dpps[j] = dcn10_dpp_create(ctx, i);
		if (pool->base.dpps[j] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC: failed to create dpp!\n");
			goto fail;
		}

		pool->base.opps[j] = dcn10_opp_create(ctx, i);
		if (pool->base.opps[j] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC: failed to create output pixel processor!\n");
			goto fail;
		}

		pool->base.timing_generators[j] = dcn10_timing_generator_create(
				ctx, i);
		if (pool->base.timing_generators[j] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error("DC: failed to create tg!\n");
			goto fail;
		}
		/* check next valid pipe */
		j++;
	}

	for (i = 0; i < pool->base.res_cap->num_ddc; i++) {
		pool->base.engines[i] = dcn10_aux_engine_create(ctx, i);
		if (pool->base.engines[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC:failed to create aux engine!!\n");
			goto fail;
		}
		pool->base.hw_i2cs[i] = dcn10_i2c_hw_create(ctx, i);
		if (pool->base.hw_i2cs[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC:failed to create hw i2c!!\n");
			goto fail;
		}
		pool->base.sw_i2cs[i] = NULL;
	}

	/* valid pipe num */
	pool->base.pipe_count = j;
	pool->base.timing_generator_count = j;

	/* within dml lib, it is hard code to 4. If ASIC pipe is fused,
	 * the value may be changed
	 */
	dc->dml.ip.max_num_dpp = pool->base.pipe_count;
	dc->dcn_ip->max_num_dpp = pool->base.pipe_count;

	pool->base.mpc = dcn10_mpc_create(ctx);
	if (pool->base.mpc == NULL) {
		BREAK_TO_DEBUGGER();
		dm_error("DC: failed to create mpc!\n");
		goto fail;
	}

	pool->base.hubbub = dcn10_hubbub_create(ctx);
	if (pool->base.hubbub == NULL) {
		BREAK_TO_DEBUGGER();
		dm_error("DC: failed to create hubbub!\n");
		goto fail;
	}

	if (!resource_construct(num_virtual_links, dc, &pool->base,
			(!IS_FPGA_MAXIMUS_DC(dc->ctx->dce_environment) ?
			&res_create_funcs : &res_create_maximus_funcs)))
			goto fail;

	dcn10_hw_sequencer_construct(dc);
	dc->caps.max_planes =  pool->base.pipe_count;

	for (i = 0; i < dc->caps.max_planes; ++i)
		dc->caps.planes[i] = plane_cap;

	dc->cap_funcs = cap_funcs;

	return true;

fail:

	dcn10_resource_destruct(pool);

	return false;
}

struct resource_pool *dcn10_create_resource_pool(
		const struct dc_init_data *init_data,
		struct dc *dc)
{
	struct dcn10_resource_pool *pool =
		kzalloc(sizeof(struct dcn10_resource_pool), GFP_KERNEL);

	if (!pool)
		return NULL;

	if (dcn10_resource_construct(init_data->num_virtual_links, dc, pool))
		return &pool->base;

	kfree(pool);
	BREAK_TO_DEBUGGER();
	return NULL;
}
