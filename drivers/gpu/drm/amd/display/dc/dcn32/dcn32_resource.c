// SPDX-License-Identifier: MIT
/*
 * Copyright 2022 Advanced Micro Devices, Inc.
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

#include "dcn32_init.h"

#include "resource.h"
#include "include/irq_service_interface.h"
#include "dcn32_resource.h"

#include "dcn20/dcn20_resource.h"
#include "dcn30/dcn30_resource.h"

#include "dcn10/dcn10_ipp.h"
#include "dcn30/dcn30_hubbub.h"
#include "dcn31/dcn31_hubbub.h"
#include "dcn32/dcn32_hubbub.h"
#include "dcn32/dcn32_mpc.h"
#include "dcn32_hubp.h"
#include "irq/dcn32/irq_service_dcn32.h"
#include "dcn32/dcn32_dpp.h"
#include "dcn32/dcn32_optc.h"
#include "dcn20/dcn20_hwseq.h"
#include "dcn30/dcn30_hwseq.h"
#include "dce110/dce110_hw_sequencer.h"
#include "dcn30/dcn30_opp.h"
#include "dcn20/dcn20_dsc.h"
#include "dcn30/dcn30_vpg.h"
#include "dcn30/dcn30_afmt.h"
#include "dcn30/dcn30_dio_stream_encoder.h"
#include "dcn32/dcn32_dio_stream_encoder.h"
#include "dcn31/dcn31_hpo_dp_stream_encoder.h"
#include "dcn31/dcn31_hpo_dp_link_encoder.h"
#include "dcn32/dcn32_hpo_dp_link_encoder.h"
#include "dc_link_dp.h"
#include "dcn31/dcn31_apg.h"
#include "dcn31/dcn31_dio_link_encoder.h"
#include "dcn32/dcn32_dio_link_encoder.h"
#include "dce/dce_clock_source.h"
#include "dce/dce_audio.h"
#include "dce/dce_hwseq.h"
#include "clk_mgr.h"
#include "virtual/virtual_stream_encoder.h"
#include "dml/display_mode_vba.h"
#include "dcn32/dcn32_dccg.h"
#include "dcn10/dcn10_resource.h"
#include "dc_link_ddc.h"
#include "dcn31/dcn31_panel_cntl.h"

#include "dcn30/dcn30_dwb.h"
#include "dcn32/dcn32_mmhubbub.h"

#include "dcn/dcn_3_2_0_offset.h"
#include "dcn/dcn_3_2_0_sh_mask.h"
#include "nbio/nbio_4_3_0_offset.h"

#include "reg_helper.h"
#include "dce/dmub_abm.h"
#include "dce/dmub_psr.h"
#include "dce/dce_aux.h"
#include "dce/dce_i2c.h"

#include "dml/dcn30/display_mode_vba_30.h"
#include "vm_helper.h"
#include "dcn20/dcn20_vmid.h"

#define DCN_BASE__INST0_SEG1                       0x000000C0
#define DCN_BASE__INST0_SEG2                       0x000034C0
#define DCN_BASE__INST0_SEG3                       0x00009000
#define NBIO_BASE__INST0_SEG1                      0x00000014

#define MAX_INSTANCE                                        6
#define MAX_SEGMENT                                         6

struct IP_BASE_INSTANCE {
	unsigned int segment[MAX_SEGMENT];
};

struct IP_BASE {
	struct IP_BASE_INSTANCE instance[MAX_INSTANCE];
};

static const struct IP_BASE DCN_BASE = { { { { 0x00000012, 0x000000C0, 0x000034C0, 0x00009000, 0x02403C00, 0 } },
					{ { 0, 0, 0, 0, 0, 0 } },
					{ { 0, 0, 0, 0, 0, 0 } },
					{ { 0, 0, 0, 0, 0, 0 } },
					{ { 0, 0, 0, 0, 0, 0 } },
					{ { 0, 0, 0, 0, 0, 0 } } } };

#define DC_LOGGER_INIT(logger)

#define DCN3_2_DEFAULT_DET_SIZE 256
#define DCN3_2_MAX_DET_SIZE 1152
#define DCN3_2_MIN_DET_SIZE 128
#define DCN3_2_MIN_COMPBUF_SIZE_KB 128

struct _vcs_dpi_ip_params_st dcn3_2_ip = {
	.gpuvm_enable = 1,
	.gpuvm_max_page_table_levels = 1,
	.hostvm_enable = 0,
	.rob_buffer_size_kbytes = 128,
	.det_buffer_size_kbytes = DCN3_2_DEFAULT_DET_SIZE,
	.config_return_buffer_size_in_kbytes = 1280,
	.compressed_buffer_segment_size_in_kbytes = 64,
	.meta_fifo_size_in_kentries = 22,
	.zero_size_buffer_entries = 512,
	.compbuf_reserved_space_64b = 256,
	.compbuf_reserved_space_zs = 64,
	.dpp_output_buffer_pixels = 2560,
	.opp_output_buffer_lines = 1,
	.pixel_chunk_size_kbytes = 8,
	.alpha_pixel_chunk_size_kbytes = 4, // not appearing in spreadsheet, match c code from hw team
	.min_pixel_chunk_size_bytes = 1024,
	.dcc_meta_buffer_size_bytes = 6272,
	.meta_chunk_size_kbytes = 2,
	.min_meta_chunk_size_bytes = 256,
	.writeback_chunk_size_kbytes = 8,
	.ptoi_supported = false,
	.num_dsc = 4,
	.maximum_dsc_bits_per_component = 12,
	.maximum_pixels_per_line_per_dsc_unit = 6016,
	.dsc422_native_support = true,
	.is_line_buffer_bpp_fixed = true,
	.line_buffer_fixed_bpp = 57,
	.line_buffer_size_bits = 1171920, //DPP doc, DCN3_2_DisplayMode_73.xlsm still shows as 986880 bits with 48 bpp
	.max_line_buffer_lines = 32,
	.writeback_interface_buffer_size_kbytes = 90,
	.max_num_dpp = 4,
	.max_num_otg = 4,
	.max_num_hdmi_frl_outputs = 1,
	.max_num_wb = 1,
	.max_dchub_pscl_bw_pix_per_clk = 4,
	.max_pscl_lb_bw_pix_per_clk = 2,
	.max_lb_vscl_bw_pix_per_clk = 4,
	.max_vscl_hscl_bw_pix_per_clk = 4,
	.max_hscl_ratio = 6,
	.max_vscl_ratio = 6,
	.max_hscl_taps = 8,
	.max_vscl_taps = 8,
	.dpte_buffer_size_in_pte_reqs_luma = 64,
	.dpte_buffer_size_in_pte_reqs_chroma = 34,
	.dispclk_ramp_margin_percent = 1,
	.max_inter_dcn_tile_repeaters = 8,
	.cursor_buffer_size = 16,
	.cursor_chunk_size = 2,
	.writeback_line_buffer_buffer_size = 0,
	.writeback_min_hscl_ratio = 1,
	.writeback_min_vscl_ratio = 1,
	.writeback_max_hscl_ratio = 1,
	.writeback_max_vscl_ratio = 1,
	.writeback_max_hscl_taps = 1,
	.writeback_max_vscl_taps = 1,
	.dppclk_delay_subtotal = 47,
	.dppclk_delay_scl = 50,
	.dppclk_delay_scl_lb_only = 16,
	.dppclk_delay_cnvc_formatter = 28,
	.dppclk_delay_cnvc_cursor = 6,
	.dispclk_delay_subtotal = 125,
	.dynamic_metadata_vm_enabled = false,
	.odm_combine_4to1_supported = false,
	.dcc_supported = true,
	.max_num_dp2p0_outputs = 2,
	.max_num_dp2p0_streams = 4,
};

struct _vcs_dpi_soc_bounding_box_st dcn3_2_soc = {
	.clock_limits = {
		{
			.state = 0,
			.dcfclk_mhz = 1564.0,
			.fabricclk_mhz = 400.0,
			.dispclk_mhz = 2150.0,
			.dppclk_mhz = 2150.0,
			.phyclk_mhz = 810.0,
			.phyclk_d18_mhz = 667.0,
			.phyclk_d32_mhz = 625.0,
			.socclk_mhz = 1200.0,
			.dscclk_mhz = 716.667,
			.dram_speed_mts = 1600.0,
			.dtbclk_mhz = 1564.0,
		},
	},
	.num_states = 1,
	.sr_exit_time_us = 5.20,
	.sr_enter_plus_exit_time_us = 9.60,
	.sr_exit_z8_time_us = 285.0,
	.sr_enter_plus_exit_z8_time_us = 320,
	.writeback_latency_us = 12.0,
	.round_trip_ping_latency_dcfclk_cycles = 263,
	.urgent_latency_pixel_data_only_us = 4.0,
	.urgent_latency_pixel_mixed_with_vm_data_us = 4.0,
	.urgent_latency_vm_data_only_us = 4.0,
	.fclk_change_latency_us = 20,
	.usr_retraining_latency_us = 2,
	.smn_latency_us = 2,
	.mall_allocated_for_dcn_mbytes = 64,
	.urgent_out_of_order_return_per_channel_pixel_only_bytes = 4096,
	.urgent_out_of_order_return_per_channel_pixel_and_vm_bytes = 4096,
	.urgent_out_of_order_return_per_channel_vm_only_bytes = 4096,
	.pct_ideal_sdp_bw_after_urgent = 100.0,
	.pct_ideal_fabric_bw_after_urgent = 67.0,
	.pct_ideal_dram_sdp_bw_after_urgent_pixel_only = 20.0,
	.pct_ideal_dram_sdp_bw_after_urgent_pixel_and_vm = 60.0, // N/A, for now keep as is until DML implemented
	.pct_ideal_dram_sdp_bw_after_urgent_vm_only = 30.0, // N/A, for now keep as is until DML implemented
	.pct_ideal_dram_bw_after_urgent_strobe = 67.0,
	.max_avg_sdp_bw_use_normal_percent = 80.0,
	.max_avg_fabric_bw_use_normal_percent = 60.0,
	.max_avg_dram_bw_use_normal_strobe_percent = 50.0,
	.max_avg_dram_bw_use_normal_percent = 15.0,
	.num_chans = 8,
	.dram_channel_width_bytes = 2,
	.fabric_datapath_to_dcn_data_return_bytes = 64,
	.return_bus_width_bytes = 64,
	.downspread_percent = 0.38,
	.dcn_downspread_percent = 0.5,
	.dram_clock_change_latency_us = 400,
	.dispclk_dppclk_vco_speed_mhz = 4300.0,
	.do_urgent_latency_adjustment = true,
	.urgent_latency_adjustment_fabric_clock_component_us = 1.0,
	.urgent_latency_adjustment_fabric_clock_reference_mhz = 1000,
};

enum dcn32_clk_src_array_id {
	DCN32_CLK_SRC_PLL0,
	DCN32_CLK_SRC_PLL1,
	DCN32_CLK_SRC_PLL2,
	DCN32_CLK_SRC_PLL3,
	DCN32_CLK_SRC_PLL4,
	DCN32_CLK_SRC_TOTAL
};

/* begin *********************
 * macros to expend register list macro defined in HW object header file
 */

/* DCN */
/* TODO awful hack. fixup dcn20_dwb.h */
#undef BASE_INNER
#define BASE_INNER(seg) DCN_BASE__INST0_SEG ## seg

#define BASE(seg) BASE_INNER(seg)

#define SR(reg_name)\
		.reg_name = BASE(reg ## reg_name ## _BASE_IDX) +  \
					reg ## reg_name

#define SRI(reg_name, block, id)\
	.reg_name = BASE(reg ## block ## id ## _ ## reg_name ## _BASE_IDX) + \
					reg ## block ## id ## _ ## reg_name

#define SRI2(reg_name, block, id)\
	.reg_name = BASE(reg ## reg_name ## _BASE_IDX) + \
					reg ## reg_name

#define SRIR(var_name, reg_name, block, id)\
	.var_name = BASE(reg ## block ## id ## _ ## reg_name ## _BASE_IDX) + \
					reg ## block ## id ## _ ## reg_name

#define SRII(reg_name, block, id)\
	.reg_name[id] = BASE(reg ## block ## id ## _ ## reg_name ## _BASE_IDX) + \
					reg ## block ## id ## _ ## reg_name

#define SRII_MPC_RMU(reg_name, block, id)\
	.RMU##_##reg_name[id] = BASE(reg ## block ## id ## _ ## reg_name ## _BASE_IDX) + \
					reg ## block ## id ## _ ## reg_name

#define SRII_DWB(reg_name, temp_name, block, id)\
	.reg_name[id] = BASE(reg ## block ## id ## _ ## temp_name ## _BASE_IDX) + \
					reg ## block ## id ## _ ## temp_name

#define DCCG_SRII(reg_name, block, id)\
	.block ## _ ## reg_name[id] = BASE(reg ## block ## id ## _ ## reg_name ## _BASE_IDX) + \
					reg ## block ## id ## _ ## reg_name

#define VUPDATE_SRII(reg_name, block, id)\
	.reg_name[id] = BASE(reg ## reg_name ## _ ## block ## id ## _BASE_IDX) + \
					reg ## reg_name ## _ ## block ## id

/* NBIO */
#define NBIO_BASE_INNER(seg) \
	NBIO_BASE__INST0_SEG ## seg

#define NBIO_BASE(seg) \
	NBIO_BASE_INNER(seg)

#define NBIO_SR(reg_name)\
		.reg_name = NBIO_BASE(regBIF_BX0_ ## reg_name ## _BASE_IDX) + \
					regBIF_BX0_ ## reg_name

#define CTX ctx
#define REG(reg_name) \
	(DCN_BASE.instance[0].segment[reg ## reg_name ## _BASE_IDX] + reg ## reg_name)

static const struct bios_registers bios_regs = {
		NBIO_SR(BIOS_SCRATCH_3),
		NBIO_SR(BIOS_SCRATCH_6)
};

#define clk_src_regs(index, pllid)\
[index] = {\
	CS_COMMON_REG_LIST_DCN3_0(index, pllid),\
}

static const struct dce110_clk_src_regs clk_src_regs[] = {
	clk_src_regs(0, A),
	clk_src_regs(1, B),
	clk_src_regs(2, C),
	clk_src_regs(3, D),
	clk_src_regs(4, E)
};

static const struct dce110_clk_src_shift cs_shift = {
		CS_COMMON_MASK_SH_LIST_DCN3_2(__SHIFT)
};

static const struct dce110_clk_src_mask cs_mask = {
		CS_COMMON_MASK_SH_LIST_DCN3_2(_MASK)
};

#define abm_regs(id)\
[id] = {\
		ABM_DCN32_REG_LIST(id)\
}

static const struct dce_abm_registers abm_regs[] = {
		abm_regs(0),
		abm_regs(1),
		abm_regs(2),
		abm_regs(3),
};

static const struct dce_abm_shift abm_shift = {
		ABM_MASK_SH_LIST_DCN32(__SHIFT)
};

static const struct dce_abm_mask abm_mask = {
		ABM_MASK_SH_LIST_DCN32(_MASK)
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
	audio_regs(4)
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

#define vpg_regs(id)\
[id] = {\
	VPG_DCN3_REG_LIST(id)\
}

static const struct dcn30_vpg_registers vpg_regs[] = {
	vpg_regs(0),
	vpg_regs(1),
	vpg_regs(2),
	vpg_regs(3),
	vpg_regs(4),
	vpg_regs(5),
	vpg_regs(6),
	vpg_regs(7),
	vpg_regs(8),
	vpg_regs(9),
};

static const struct dcn30_vpg_shift vpg_shift = {
	DCN3_VPG_MASK_SH_LIST(__SHIFT)
};

static const struct dcn30_vpg_mask vpg_mask = {
	DCN3_VPG_MASK_SH_LIST(_MASK)
};

#define afmt_regs(id)\
[id] = {\
	AFMT_DCN3_REG_LIST(id)\
}

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

#define apg_regs(id)\
[id] = {\
	APG_DCN31_REG_LIST(id)\
}

static const struct dcn31_apg_registers apg_regs[] = {
	apg_regs(0),
	apg_regs(1),
	apg_regs(2),
	apg_regs(3)
};

static const struct dcn31_apg_shift apg_shift = {
	DCN31_APG_MASK_SH_LIST(__SHIFT)
};

static const struct dcn31_apg_mask apg_mask = {
		DCN31_APG_MASK_SH_LIST(_MASK)
};

#define stream_enc_regs(id)\
[id] = {\
	SE_DCN32_REG_LIST(id)\
}

static const struct dcn10_stream_enc_registers stream_enc_regs[] = {
	stream_enc_regs(0),
	stream_enc_regs(1),
	stream_enc_regs(2),
	stream_enc_regs(3),
	stream_enc_regs(4)
};

static const struct dcn10_stream_encoder_shift se_shift = {
		SE_COMMON_MASK_SH_LIST_DCN32(__SHIFT)
};

static const struct dcn10_stream_encoder_mask se_mask = {
		SE_COMMON_MASK_SH_LIST_DCN32(_MASK)
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
		aux_regs(4)
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
		hpd_regs(4)
};

#define link_regs(id, phyid)\
[id] = {\
	LE_DCN31_REG_LIST(id), \
	UNIPHY_DCN2_REG_LIST(phyid), \
	/*DPCS_DCN31_REG_LIST(id),*/ \
}

static const struct dcn10_link_enc_registers link_enc_regs[] = {
	link_regs(0, A),
	link_regs(1, B),
	link_regs(2, C),
	link_regs(3, D),
	link_regs(4, E)
};

static const struct dcn10_link_enc_shift le_shift = {
	LINK_ENCODER_MASK_SH_LIST_DCN31(__SHIFT), \
	//DPCS_DCN31_MASK_SH_LIST(__SHIFT)
};

static const struct dcn10_link_enc_mask le_mask = {
	LINK_ENCODER_MASK_SH_LIST_DCN31(_MASK), \

	//DPCS_DCN31_MASK_SH_LIST(_MASK)
};

#define hpo_dp_stream_encoder_reg_list(id)\
[id] = {\
	DCN3_1_HPO_DP_STREAM_ENC_REG_LIST(id)\
}

static const struct dcn31_hpo_dp_stream_encoder_registers hpo_dp_stream_enc_regs[] = {
	hpo_dp_stream_encoder_reg_list(0),
	hpo_dp_stream_encoder_reg_list(1),
	hpo_dp_stream_encoder_reg_list(2),
	hpo_dp_stream_encoder_reg_list(3),
};

static const struct dcn31_hpo_dp_stream_encoder_shift hpo_dp_se_shift = {
	DCN3_1_HPO_DP_STREAM_ENC_MASK_SH_LIST(__SHIFT)
};

static const struct dcn31_hpo_dp_stream_encoder_mask hpo_dp_se_mask = {
	DCN3_1_HPO_DP_STREAM_ENC_MASK_SH_LIST(_MASK)
};


#define hpo_dp_link_encoder_reg_list(id)\
[id] = {\
	DCN3_1_HPO_DP_LINK_ENC_REG_LIST(id),\
	/*DCN3_1_RDPCSTX_REG_LIST(0),*/\
	/*DCN3_1_RDPCSTX_REG_LIST(1),*/\
	/*DCN3_1_RDPCSTX_REG_LIST(2),*/\
	/*DCN3_1_RDPCSTX_REG_LIST(3),*/\
	/*DCN3_1_RDPCSTX_REG_LIST(4)*/\
}

static const struct dcn31_hpo_dp_link_encoder_registers hpo_dp_link_enc_regs[] = {
	hpo_dp_link_encoder_reg_list(0),
	hpo_dp_link_encoder_reg_list(1),
};

static const struct dcn31_hpo_dp_link_encoder_shift hpo_dp_le_shift = {
	DCN3_2_HPO_DP_LINK_ENC_MASK_SH_LIST(__SHIFT)
};

static const struct dcn31_hpo_dp_link_encoder_mask hpo_dp_le_mask = {
	DCN3_2_HPO_DP_LINK_ENC_MASK_SH_LIST(_MASK)
};

#define dpp_regs(id)\
[id] = {\
	DPP_REG_LIST_DCN30_COMMON(id),\
}

static const struct dcn3_dpp_registers dpp_regs[] = {
	dpp_regs(0),
	dpp_regs(1),
	dpp_regs(2),
	dpp_regs(3)
};

static const struct dcn3_dpp_shift tf_shift = {
		DPP_REG_LIST_SH_MASK_DCN30_COMMON(__SHIFT)
};

static const struct dcn3_dpp_mask tf_mask = {
		DPP_REG_LIST_SH_MASK_DCN30_COMMON(_MASK)
};


#define opp_regs(id)\
[id] = {\
	OPP_REG_LIST_DCN30(id),\
}

static const struct dcn20_opp_registers opp_regs[] = {
	opp_regs(0),
	opp_regs(1),
	opp_regs(2),
	opp_regs(3)
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
		aux_engine_regs(4)
};

static const struct dce110_aux_registers_shift aux_shift = {
	DCN_AUX_MASK_SH_LIST(__SHIFT)
};

static const struct dce110_aux_registers_mask aux_mask = {
	DCN_AUX_MASK_SH_LIST(_MASK)
};


#define dwbc_regs_dcn3(id)\
[id] = {\
	DWBC_COMMON_REG_LIST_DCN30(id),\
}

static const struct dcn30_dwbc_registers dwbc30_regs[] = {
	dwbc_regs_dcn3(0),
};

static const struct dcn30_dwbc_shift dwbc30_shift = {
	DWBC_COMMON_MASK_SH_LIST_DCN30(__SHIFT)
};

static const struct dcn30_dwbc_mask dwbc30_mask = {
	DWBC_COMMON_MASK_SH_LIST_DCN30(_MASK)
};

#define mcif_wb_regs_dcn3(id)\
[id] = {\
	MCIF_WB_COMMON_REG_LIST_DCN32(id),\
}

static const struct dcn30_mmhubbub_registers mcif_wb30_regs[] = {
	mcif_wb_regs_dcn3(0)
};

static const struct dcn30_mmhubbub_shift mcif_wb30_shift = {
	MCIF_WB_COMMON_MASK_SH_LIST_DCN32(__SHIFT)
};

static const struct dcn30_mmhubbub_mask mcif_wb30_mask = {
	MCIF_WB_COMMON_MASK_SH_LIST_DCN32(_MASK)
};

#define dsc_regsDCN20(id)\
[id] = {\
	DSC_REG_LIST_DCN20(id)\
}

static const struct dcn20_dsc_registers dsc_regs[] = {
	dsc_regsDCN20(0),
	dsc_regsDCN20(1),
	dsc_regsDCN20(2),
	dsc_regsDCN20(3)
};

static const struct dcn20_dsc_shift dsc_shift = {
	DSC_REG_LIST_SH_MASK_DCN20(__SHIFT)
};

static const struct dcn20_dsc_mask dsc_mask = {
	DSC_REG_LIST_SH_MASK_DCN20(_MASK)
};

static const struct dcn30_mpc_registers mpc_regs = {
		MPC_REG_LIST_DCN3_0(0),
		MPC_REG_LIST_DCN3_0(1),
		MPC_REG_LIST_DCN3_0(2),
		MPC_REG_LIST_DCN3_0(3),
		MPC_OUT_MUX_REG_LIST_DCN3_0(0),
		MPC_OUT_MUX_REG_LIST_DCN3_0(1),
		MPC_OUT_MUX_REG_LIST_DCN3_0(2),
		MPC_OUT_MUX_REG_LIST_DCN3_0(3),
		MPC_MCM_REG_LIST_DCN32(0),
		MPC_MCM_REG_LIST_DCN32(1),
		MPC_MCM_REG_LIST_DCN32(2),
		MPC_MCM_REG_LIST_DCN32(3),
		MPC_DWB_MUX_REG_LIST_DCN3_0(0),
};

static const struct dcn30_mpc_shift mpc_shift = {
	MPC_COMMON_MASK_SH_LIST_DCN32(__SHIFT)
};

static const struct dcn30_mpc_mask mpc_mask = {
	MPC_COMMON_MASK_SH_LIST_DCN32(_MASK)
};

#define optc_regs(id)\
[id] = {OPTC_COMMON_REG_LIST_DCN3_2(id)}

//#ifdef DIAGS_BUILD
//static struct dcn_optc_registers optc_regs[] = {
//#else
static const struct dcn_optc_registers optc_regs[] = {
//#endif
	optc_regs(0),
	optc_regs(1),
	optc_regs(2),
	optc_regs(3)
};

static const struct dcn_optc_shift optc_shift = {
	OPTC_COMMON_MASK_SH_LIST_DCN3_2(__SHIFT)
};

static const struct dcn_optc_mask optc_mask = {
	OPTC_COMMON_MASK_SH_LIST_DCN3_2(_MASK)
};

#define hubp_regs(id)\
[id] = {\
	HUBP_REG_LIST_DCN32(id)\
}

static const struct dcn_hubp2_registers hubp_regs[] = {
		hubp_regs(0),
		hubp_regs(1),
		hubp_regs(2),
		hubp_regs(3)
};


static const struct dcn_hubp2_shift hubp_shift = {
		HUBP_MASK_SH_LIST_DCN32(__SHIFT)
};

static const struct dcn_hubp2_mask hubp_mask = {
		HUBP_MASK_SH_LIST_DCN32(_MASK)
};
static const struct dcn_hubbub_registers hubbub_reg = {
		HUBBUB_REG_LIST_DCN32(0)
};

static const struct dcn_hubbub_shift hubbub_shift = {
		HUBBUB_MASK_SH_LIST_DCN32(__SHIFT)
};

static const struct dcn_hubbub_mask hubbub_mask = {
		HUBBUB_MASK_SH_LIST_DCN32(_MASK)
};

static const struct dccg_registers dccg_regs = {
		DCCG_REG_LIST_DCN32()
};

static const struct dccg_shift dccg_shift = {
		DCCG_MASK_SH_LIST_DCN32(__SHIFT)
};

static const struct dccg_mask dccg_mask = {
		DCCG_MASK_SH_LIST_DCN32(_MASK)
};


#define SRII2(reg_name_pre, reg_name_post, id)\
	.reg_name_pre ## _ ##  reg_name_post[id] = BASE(reg ## reg_name_pre \
			## id ## _ ## reg_name_post ## _BASE_IDX) + \
			reg ## reg_name_pre ## id ## _ ## reg_name_post


#define HWSEQ_DCN32_REG_LIST()\
	SR(DCHUBBUB_GLOBAL_TIMER_CNTL), \
	SR(DIO_MEM_PWR_CTRL), \
	SR(ODM_MEM_PWR_CTRL3), \
	SR(MMHUBBUB_MEM_PWR_CNTL), \
	SR(DCCG_GATE_DISABLE_CNTL), \
	SR(DCCG_GATE_DISABLE_CNTL2), \
	SR(DCFCLK_CNTL),\
	SR(DC_MEM_GLOBAL_PWR_REQ_CNTL), \
	SRII(PIXEL_RATE_CNTL, OTG, 0), \
	SRII(PIXEL_RATE_CNTL, OTG, 1),\
	SRII(PIXEL_RATE_CNTL, OTG, 2),\
	SRII(PIXEL_RATE_CNTL, OTG, 3),\
	SRII(PHYPLL_PIXEL_RATE_CNTL, OTG, 0),\
	SRII(PHYPLL_PIXEL_RATE_CNTL, OTG, 1),\
	SRII(PHYPLL_PIXEL_RATE_CNTL, OTG, 2),\
	SRII(PHYPLL_PIXEL_RATE_CNTL, OTG, 3),\
	SR(MICROSECOND_TIME_BASE_DIV), \
	SR(MILLISECOND_TIME_BASE_DIV), \
	SR(DISPCLK_FREQ_CHANGE_CNTL), \
	SR(RBBMIF_TIMEOUT_DIS), \
	SR(RBBMIF_TIMEOUT_DIS_2), \
	SR(DCHUBBUB_CRC_CTRL), \
	SR(DPP_TOP0_DPP_CRC_CTRL), \
	SR(DPP_TOP0_DPP_CRC_VAL_B_A), \
	SR(DPP_TOP0_DPP_CRC_VAL_R_G), \
	SR(MPC_CRC_CTRL), \
	SR(MPC_CRC_RESULT_GB), \
	SR(MPC_CRC_RESULT_C), \
	SR(MPC_CRC_RESULT_AR), \
	SR(DOMAIN0_PG_CONFIG), \
	SR(DOMAIN1_PG_CONFIG), \
	SR(DOMAIN2_PG_CONFIG), \
	SR(DOMAIN3_PG_CONFIG), \
	SR(DOMAIN16_PG_CONFIG), \
	SR(DOMAIN17_PG_CONFIG), \
	SR(DOMAIN18_PG_CONFIG), \
	SR(DOMAIN19_PG_CONFIG), \
	SR(DOMAIN0_PG_STATUS), \
	SR(DOMAIN1_PG_STATUS), \
	SR(DOMAIN2_PG_STATUS), \
	SR(DOMAIN3_PG_STATUS), \
	SR(DOMAIN16_PG_STATUS), \
	SR(DOMAIN17_PG_STATUS), \
	SR(DOMAIN18_PG_STATUS), \
	SR(DOMAIN19_PG_STATUS), \
	SR(D1VGA_CONTROL), \
	SR(D2VGA_CONTROL), \
	SR(D3VGA_CONTROL), \
	SR(D4VGA_CONTROL), \
	SR(D5VGA_CONTROL), \
	SR(D6VGA_CONTROL), \
	SR(DC_IP_REQUEST_CNTL), \
	SR(AZALIA_AUDIO_DTO), \
	SR(AZALIA_CONTROLLER_CLOCK_GATING)

static const struct dce_hwseq_registers hwseq_reg = {
		HWSEQ_DCN32_REG_LIST()
};

#define HWSEQ_DCN32_MASK_SH_LIST(mask_sh)\
	HWSEQ_DCN_MASK_SH_LIST(mask_sh), \
	HWS_SF(, DCHUBBUB_GLOBAL_TIMER_CNTL, DCHUBBUB_GLOBAL_TIMER_REFDIV, mask_sh), \
	HWS_SF(, DOMAIN0_PG_CONFIG, DOMAIN_POWER_FORCEON, mask_sh), \
	HWS_SF(, DOMAIN0_PG_CONFIG, DOMAIN_POWER_GATE, mask_sh), \
	HWS_SF(, DOMAIN1_PG_CONFIG, DOMAIN_POWER_FORCEON, mask_sh), \
	HWS_SF(, DOMAIN1_PG_CONFIG, DOMAIN_POWER_GATE, mask_sh), \
	HWS_SF(, DOMAIN2_PG_CONFIG, DOMAIN_POWER_FORCEON, mask_sh), \
	HWS_SF(, DOMAIN2_PG_CONFIG, DOMAIN_POWER_GATE, mask_sh), \
	HWS_SF(, DOMAIN3_PG_CONFIG, DOMAIN_POWER_FORCEON, mask_sh), \
	HWS_SF(, DOMAIN3_PG_CONFIG, DOMAIN_POWER_GATE, mask_sh), \
	HWS_SF(, DOMAIN16_PG_CONFIG, DOMAIN_POWER_FORCEON, mask_sh), \
	HWS_SF(, DOMAIN16_PG_CONFIG, DOMAIN_POWER_GATE, mask_sh), \
	HWS_SF(, DOMAIN17_PG_CONFIG, DOMAIN_POWER_FORCEON, mask_sh), \
	HWS_SF(, DOMAIN17_PG_CONFIG, DOMAIN_POWER_GATE, mask_sh), \
	HWS_SF(, DOMAIN18_PG_CONFIG, DOMAIN_POWER_FORCEON, mask_sh), \
	HWS_SF(, DOMAIN18_PG_CONFIG, DOMAIN_POWER_GATE, mask_sh), \
	HWS_SF(, DOMAIN19_PG_CONFIG, DOMAIN_POWER_FORCEON, mask_sh), \
	HWS_SF(, DOMAIN19_PG_CONFIG, DOMAIN_POWER_GATE, mask_sh), \
	HWS_SF(, DOMAIN0_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, mask_sh), \
	HWS_SF(, DOMAIN1_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, mask_sh), \
	HWS_SF(, DOMAIN2_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, mask_sh), \
	HWS_SF(, DOMAIN3_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, mask_sh), \
	HWS_SF(, DOMAIN16_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, mask_sh), \
	HWS_SF(, DOMAIN17_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, mask_sh), \
	HWS_SF(, DOMAIN18_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, mask_sh), \
	HWS_SF(, DOMAIN19_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, mask_sh), \
	HWS_SF(, DC_IP_REQUEST_CNTL, IP_REQUEST_EN, mask_sh), \
	HWS_SF(, AZALIA_AUDIO_DTO, AZALIA_AUDIO_DTO_MODULE, mask_sh), \
	HWS_SF(, HPO_TOP_CLOCK_CONTROL, HPO_HDMISTREAMCLK_G_GATE_DIS, mask_sh), \
	HWS_SF(, ODM_MEM_PWR_CTRL3, ODM_MEM_UNASSIGNED_PWR_MODE, mask_sh), \
	HWS_SF(, ODM_MEM_PWR_CTRL3, ODM_MEM_VBLANK_PWR_MODE, mask_sh), \
	HWS_SF(, MMHUBBUB_MEM_PWR_CNTL, VGA_MEM_PWR_FORCE, mask_sh)

static const struct dce_hwseq_shift hwseq_shift = {
		HWSEQ_DCN32_MASK_SH_LIST(__SHIFT)
};

static const struct dce_hwseq_mask hwseq_mask = {
		HWSEQ_DCN32_MASK_SH_LIST(_MASK)
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

static const struct resource_caps res_cap_dcn32 = {
	.num_timing_generator = 4,
	.num_opp = 4,
	.num_video_plane = 4,
	.num_audio = 5,
	.num_stream_encoder = 5,
	.num_hpo_dp_stream_encoder = 4,
	.num_hpo_dp_link_encoder = 2,
	.num_pll = 5,
	.num_dwb = 1,
	.num_ddc = 5,
	.num_vmid = 16,
	.num_mpc_3dlut = 4,
	.num_dsc = 4,
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
			.p010 = true,
			.ayuv = false,
	},

	.max_upscale_factor = {
			.argb8888 = 16000,
			.nv12 = 16000,
			.fp16 = 16000
	},

	// 6:1 downscaling ratio: 1000/6 = 166.666
	.max_downscale_factor = {
			.argb8888 = 167,
			.nv12 = 167,
			.fp16 = 167
	},
	64,
	64
};

static const struct dc_debug_options debug_defaults_drv = {
	.disable_dmcu = true,
	.force_abm_enable = false,
	.timing_trace = false,
	.clock_trace = true,
	.disable_pplib_clock_request = false,
	.disable_idle_power_optimizations = true,
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
	.enable_mem_low_power = {
		.bits = {
			.vga = false,
			.i2c = false,
			.dmcu = false, // This is previously known to cause hang on S3 cycles if enabled
			.dscl = false,
			.cm = false,
			.mpc = false,
			.optc = false,
		}
	},
	.use_max_lb = true,
	.force_disable_subvp = true
};

static const struct dc_debug_options debug_defaults_diags = {
	.disable_dmcu = true,
	.force_abm_enable = false,
	.timing_trace = true,
	.clock_trace = true,
	.disable_dpp_power_gate = true,
	.disable_hubp_power_gate = true,
	.disable_dsc_power_gate = true,
	.disable_clock_gate = true,
	.disable_pplib_clock_request = true,
	.disable_pplib_wm_range = true,
	.disable_stutter = false,
	.scl_reset_length10 = true,
	.dwb_fi_phase = -1, // -1 = disable
	.dmub_command_table = true,
	.enable_tri_buf = true,
	.use_max_lb = true,
	.force_disable_subvp = true
};

static struct dce_aux *dcn32_aux_engine_create(
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
#define i2c_inst_regs(id) { I2C_HW_ENGINE_COMMON_REG_LIST_DCN30(id) }

static const struct dce_i2c_registers i2c_hw_regs[] = {
		i2c_inst_regs(1),
		i2c_inst_regs(2),
		i2c_inst_regs(3),
		i2c_inst_regs(4),
		i2c_inst_regs(5),
};

static const struct dce_i2c_shift i2c_shifts = {
		I2C_COMMON_MASK_SH_LIST_DCN30(__SHIFT)
};

static const struct dce_i2c_mask i2c_masks = {
		I2C_COMMON_MASK_SH_LIST_DCN30(_MASK)
};

static struct dce_i2c_hw *dcn32_i2c_hw_create(
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

static struct clock_source *dcn32_clock_source_create(
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

	if (dcn31_clk_src_construct(clk_src, ctx, bios, id,
			regs, &cs_shift, &cs_mask)) {
		clk_src->base.dp_clk_src = dp_clk_src;
		return &clk_src->base;
	}

	BREAK_TO_DEBUGGER();
	return NULL;
}

static struct hubbub *dcn32_hubbub_create(struct dc_context *ctx)
{
	int i;

	struct dcn20_hubbub *hubbub2 = kzalloc(sizeof(struct dcn20_hubbub),
					  GFP_KERNEL);

	if (!hubbub2)
		return NULL;

	hubbub32_construct(hubbub2, ctx,
			&hubbub_reg,
			&hubbub_shift,
			&hubbub_mask,
			ctx->dc->dml.ip.det_buffer_size_kbytes,
			ctx->dc->dml.ip.pixel_chunk_size_kbytes,
			ctx->dc->dml.ip.config_return_buffer_size_in_kbytes);


	for (i = 0; i < res_cap_dcn32.num_vmid; i++) {
		struct dcn20_vmid *vmid = &hubbub2->vmid[i];

		vmid->ctx = ctx;

		vmid->regs = &vmid_regs[i];
		vmid->shifts = &vmid_shifts;
		vmid->masks = &vmid_masks;
	}

	return &hubbub2->base;
}

static struct hubp *dcn32_hubp_create(
	struct dc_context *ctx,
	uint32_t inst)
{
	struct dcn20_hubp *hubp2 =
		kzalloc(sizeof(struct dcn20_hubp), GFP_KERNEL);

	if (!hubp2)
		return NULL;

	if (hubp32_construct(hubp2, ctx, inst,
			&hubp_regs[inst], &hubp_shift, &hubp_mask))
		return &hubp2->base;

	BREAK_TO_DEBUGGER();
	kfree(hubp2);
	return NULL;
}

static void dcn32_dpp_destroy(struct dpp **dpp)
{
	kfree(TO_DCN30_DPP(*dpp));
	*dpp = NULL;
}

static struct dpp *dcn32_dpp_create(
	struct dc_context *ctx,
	uint32_t inst)
{
	struct dcn3_dpp *dpp3 =
		kzalloc(sizeof(struct dcn3_dpp), GFP_KERNEL);

	if (!dpp3)
		return NULL;

	if (dpp32_construct(dpp3, ctx, inst,
			&dpp_regs[inst], &tf_shift, &tf_mask))
		return &dpp3->base;

	BREAK_TO_DEBUGGER();
	kfree(dpp3);
	return NULL;
}

static struct mpc *dcn32_mpc_create(
		struct dc_context *ctx,
		int num_mpcc,
		int num_rmu)
{
	struct dcn30_mpc *mpc30 = kzalloc(sizeof(struct dcn30_mpc),
					  GFP_KERNEL);

	if (!mpc30)
		return NULL;

	dcn32_mpc_construct(mpc30, ctx,
			&mpc_regs,
			&mpc_shift,
			&mpc_mask,
			num_mpcc,
			num_rmu);

	return &mpc30->base;
}

static struct output_pixel_processor *dcn32_opp_create(
	struct dc_context *ctx, uint32_t inst)
{
	struct dcn20_opp *opp2 =
		kzalloc(sizeof(struct dcn20_opp), GFP_KERNEL);

	if (!opp2) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	dcn20_opp_construct(opp2, ctx, inst,
			&opp_regs[inst], &opp_shift, &opp_mask);
	return &opp2->base;
}


static struct timing_generator *dcn32_timing_generator_create(
		struct dc_context *ctx,
		uint32_t instance)
{
	struct optc *tgn10 =
		kzalloc(sizeof(struct optc), GFP_KERNEL);

	if (!tgn10)
		return NULL;

	tgn10->base.inst = instance;
	tgn10->base.ctx = ctx;

	tgn10->tg_regs = &optc_regs[instance];
	tgn10->tg_shift = &optc_shift;
	tgn10->tg_mask = &optc_mask;

	dcn32_timing_generator_init(tgn10);

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

static struct link_encoder *dcn32_link_encoder_create(
	const struct encoder_init_data *enc_init_data)
{
	struct dcn20_link_encoder *enc20 =
		kzalloc(sizeof(struct dcn20_link_encoder), GFP_KERNEL);

	if (!enc20)
		return NULL;

	dcn32_link_encoder_construct(enc20,
			enc_init_data,
			&link_enc_feature,
			&link_enc_regs[enc_init_data->transmitter],
			&link_enc_aux_regs[enc_init_data->channel - 1],
			&link_enc_hpd_regs[enc_init_data->hpd_source],
			&le_shift,
			&le_mask);

	return &enc20->enc10.base;
}

struct panel_cntl *dcn32_panel_cntl_create(const struct panel_cntl_init_data *init_data)
{
	struct dcn31_panel_cntl *panel_cntl =
		kzalloc(sizeof(struct dcn31_panel_cntl), GFP_KERNEL);

	if (!panel_cntl)
		return NULL;

	dcn31_panel_cntl_construct(panel_cntl, init_data);

	return &panel_cntl->base;
}

static void read_dce_straps(
	struct dc_context *ctx,
	struct resource_straps *straps)
{
	generic_reg_get(ctx, regDC_PINSTRAPS + BASE(regDC_PINSTRAPS_BASE_IDX),
		FN(DC_PINSTRAPS, DC_PINSTRAPS_AUDIO), &straps->dc_pinstraps_audio);

}

static struct audio *dcn32_create_audio(
		struct dc_context *ctx, unsigned int inst)
{
	return dce_audio_create(ctx, inst,
			&audio_regs[inst], &audio_shift, &audio_mask);
}

static struct vpg *dcn32_vpg_create(
	struct dc_context *ctx,
	uint32_t inst)
{
	struct dcn30_vpg *vpg3 = kzalloc(sizeof(struct dcn30_vpg), GFP_KERNEL);

	if (!vpg3)
		return NULL;

	vpg3_construct(vpg3, ctx, inst,
			&vpg_regs[inst],
			&vpg_shift,
			&vpg_mask);

	return &vpg3->base;
}

static struct afmt *dcn32_afmt_create(
	struct dc_context *ctx,
	uint32_t inst)
{
	struct dcn30_afmt *afmt3 = kzalloc(sizeof(struct dcn30_afmt), GFP_KERNEL);

	if (!afmt3)
		return NULL;

	afmt3_construct(afmt3, ctx, inst,
			&afmt_regs[inst],
			&afmt_shift,
			&afmt_mask);

	return &afmt3->base;
}

static struct apg *dcn31_apg_create(
	struct dc_context *ctx,
	uint32_t inst)
{
	struct dcn31_apg *apg31 = kzalloc(sizeof(struct dcn31_apg), GFP_KERNEL);

	if (!apg31)
		return NULL;

	apg31_construct(apg31, ctx, inst,
			&apg_regs[inst],
			&apg_shift,
			&apg_mask);

	return &apg31->base;
}

static struct stream_encoder *dcn32_stream_encoder_create(
	enum engine_id eng_id,
	struct dc_context *ctx)
{
	struct dcn10_stream_encoder *enc1;
	struct vpg *vpg;
	struct afmt *afmt;
	int vpg_inst;
	int afmt_inst;

	/* Mapping of VPG, AFMT, DME register blocks to DIO block instance */
	if (eng_id <= ENGINE_ID_DIGF) {
		vpg_inst = eng_id;
		afmt_inst = eng_id;
	} else
		return NULL;

	enc1 = kzalloc(sizeof(struct dcn10_stream_encoder), GFP_KERNEL);
	vpg = dcn32_vpg_create(ctx, vpg_inst);
	afmt = dcn32_afmt_create(ctx, afmt_inst);

	if (!enc1 || !vpg || !afmt) {
		kfree(enc1);
		kfree(vpg);
		kfree(afmt);
		return NULL;
	}

	dcn32_dio_stream_encoder_construct(enc1, ctx, ctx->dc_bios,
					eng_id, vpg, afmt,
					&stream_enc_regs[eng_id],
					&se_shift, &se_mask);

	return &enc1->base;
}

static struct hpo_dp_stream_encoder *dcn32_hpo_dp_stream_encoder_create(
	enum engine_id eng_id,
	struct dc_context *ctx)
{
	struct dcn31_hpo_dp_stream_encoder *hpo_dp_enc31;
	struct vpg *vpg;
	struct apg *apg;
	uint32_t hpo_dp_inst;
	uint32_t vpg_inst;
	uint32_t apg_inst;

	ASSERT((eng_id >= ENGINE_ID_HPO_DP_0) && (eng_id <= ENGINE_ID_HPO_DP_3));
	hpo_dp_inst = eng_id - ENGINE_ID_HPO_DP_0;

	/* Mapping of VPG register blocks to HPO DP block instance:
	 * VPG[6] -> HPO_DP[0]
	 * VPG[7] -> HPO_DP[1]
	 * VPG[8] -> HPO_DP[2]
	 * VPG[9] -> HPO_DP[3]
	 */
	vpg_inst = hpo_dp_inst + 6;

	/* Mapping of APG register blocks to HPO DP block instance:
	 * APG[0] -> HPO_DP[0]
	 * APG[1] -> HPO_DP[1]
	 * APG[2] -> HPO_DP[2]
	 * APG[3] -> HPO_DP[3]
	 */
	apg_inst = hpo_dp_inst;

	/* allocate HPO stream encoder and create VPG sub-block */
	hpo_dp_enc31 = kzalloc(sizeof(struct dcn31_hpo_dp_stream_encoder), GFP_KERNEL);
	vpg = dcn32_vpg_create(ctx, vpg_inst);
	apg = dcn31_apg_create(ctx, apg_inst);

	if (!hpo_dp_enc31 || !vpg || !apg) {
		kfree(hpo_dp_enc31);
		kfree(vpg);
		kfree(apg);
		return NULL;
	}

	dcn31_hpo_dp_stream_encoder_construct(hpo_dp_enc31, ctx, ctx->dc_bios,
					hpo_dp_inst, eng_id, vpg, apg,
					&hpo_dp_stream_enc_regs[hpo_dp_inst],
					&hpo_dp_se_shift, &hpo_dp_se_mask);

	return &hpo_dp_enc31->base;
}

static struct hpo_dp_link_encoder *dcn32_hpo_dp_link_encoder_create(
	uint8_t inst,
	struct dc_context *ctx)
{
	struct dcn31_hpo_dp_link_encoder *hpo_dp_enc31;

	/* allocate HPO link encoder */
	hpo_dp_enc31 = kzalloc(sizeof(struct dcn31_hpo_dp_link_encoder), GFP_KERNEL);

	hpo_dp_link_encoder32_construct(hpo_dp_enc31, ctx, inst,
					&hpo_dp_link_enc_regs[inst],
					&hpo_dp_le_shift, &hpo_dp_le_mask);

	return &hpo_dp_enc31->base;
}

static struct dce_hwseq *dcn32_hwseq_create(
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
	.create_audio = dcn32_create_audio,
	.create_stream_encoder = dcn32_stream_encoder_create,
	.create_hpo_dp_stream_encoder = dcn32_hpo_dp_stream_encoder_create,
	.create_hpo_dp_link_encoder = dcn32_hpo_dp_link_encoder_create,
	.create_hwseq = dcn32_hwseq_create,
};

static const struct resource_create_funcs res_create_maximus_funcs = {
	.read_dce_straps = NULL,
	.create_audio = NULL,
	.create_stream_encoder = NULL,
	.create_hpo_dp_stream_encoder = dcn32_hpo_dp_stream_encoder_create,
	.create_hpo_dp_link_encoder = dcn32_hpo_dp_link_encoder_create,
	.create_hwseq = dcn32_hwseq_create,
};

static void dcn32_resource_destruct(struct dcn32_resource_pool *pool)
{
	unsigned int i;

	for (i = 0; i < pool->base.stream_enc_count; i++) {
		if (pool->base.stream_enc[i] != NULL) {
			if (pool->base.stream_enc[i]->vpg != NULL) {
				kfree(DCN30_VPG_FROM_VPG(pool->base.stream_enc[i]->vpg));
				pool->base.stream_enc[i]->vpg = NULL;
			}
			if (pool->base.stream_enc[i]->afmt != NULL) {
				kfree(DCN30_AFMT_FROM_AFMT(pool->base.stream_enc[i]->afmt));
				pool->base.stream_enc[i]->afmt = NULL;
			}
			kfree(DCN10STRENC_FROM_STRENC(pool->base.stream_enc[i]));
			pool->base.stream_enc[i] = NULL;
		}
	}

	for (i = 0; i < pool->base.hpo_dp_stream_enc_count; i++) {
		if (pool->base.hpo_dp_stream_enc[i] != NULL) {
			if (pool->base.hpo_dp_stream_enc[i]->vpg != NULL) {
				kfree(DCN30_VPG_FROM_VPG(pool->base.hpo_dp_stream_enc[i]->vpg));
				pool->base.hpo_dp_stream_enc[i]->vpg = NULL;
			}
			if (pool->base.hpo_dp_stream_enc[i]->apg != NULL) {
				kfree(DCN31_APG_FROM_APG(pool->base.hpo_dp_stream_enc[i]->apg));
				pool->base.hpo_dp_stream_enc[i]->apg = NULL;
			}
			kfree(DCN3_1_HPO_DP_STREAM_ENC_FROM_HPO_STREAM_ENC(pool->base.hpo_dp_stream_enc[i]));
			pool->base.hpo_dp_stream_enc[i] = NULL;
		}
	}

	for (i = 0; i < pool->base.hpo_dp_link_enc_count; i++) {
		if (pool->base.hpo_dp_link_enc[i] != NULL) {
			kfree(DCN3_1_HPO_DP_LINK_ENC_FROM_HPO_LINK_ENC(pool->base.hpo_dp_link_enc[i]));
			pool->base.hpo_dp_link_enc[i] = NULL;
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
		kfree(TO_DCN20_HUBBUB(pool->base.hubbub));
		pool->base.hubbub = NULL;
	}
	for (i = 0; i < pool->base.pipe_count; i++) {
		if (pool->base.dpps[i] != NULL)
			dcn32_dpp_destroy(&pool->base.dpps[i]);

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
			kfree(TO_DCN30_DWBC(pool->base.dwbc[i]));
			pool->base.dwbc[i] = NULL;
		}
		if (pool->base.mcif_wb[i] != NULL) {
			kfree(TO_DCN30_MMHUBBUB(pool->base.mcif_wb[i]));
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

	for (i = 0; i < pool->base.res_cap->num_mpc_3dlut; i++) {
		if (pool->base.mpc_lut[i] != NULL) {
			dc_3dlut_func_release(pool->base.mpc_lut[i]);
			pool->base.mpc_lut[i] = NULL;
		}
		if (pool->base.mpc_shaper[i] != NULL) {
			dc_transfer_func_release(pool->base.mpc_shaper[i]);
			pool->base.mpc_shaper[i] = NULL;
		}
	}

	if (pool->base.dp_clock_source != NULL) {
		dcn20_clock_source_destroy(&pool->base.dp_clock_source);
		pool->base.dp_clock_source = NULL;
	}

	for (i = 0; i < pool->base.res_cap->num_timing_generator; i++) {
		if (pool->base.multiple_abms[i] != NULL)
			dce_abm_destroy(&pool->base.multiple_abms[i]);
	}

	if (pool->base.psr != NULL)
		dmub_psr_destroy(&pool->base.psr);

	if (pool->base.dccg != NULL)
		dcn_dccg_destroy(&pool->base.dccg);

	if (pool->base.oem_device != NULL)
		dal_ddc_service_destroy(&pool->base.oem_device);
}


static bool dcn32_dwbc_create(struct dc_context *ctx, struct resource_pool *pool)
{
	int i;
	uint32_t dwb_count = pool->res_cap->num_dwb;

	for (i = 0; i < dwb_count; i++) {
		struct dcn30_dwbc *dwbc30 = kzalloc(sizeof(struct dcn30_dwbc),
						    GFP_KERNEL);

		if (!dwbc30) {
			dm_error("DC: failed to create dwbc30!\n");
			return false;
		}

		dcn30_dwbc_construct(dwbc30, ctx,
				&dwbc30_regs[i],
				&dwbc30_shift,
				&dwbc30_mask,
				i);

		pool->dwbc[i] = &dwbc30->base;
	}
	return true;
}

static bool dcn32_mmhubbub_create(struct dc_context *ctx, struct resource_pool *pool)
{
	int i;
	uint32_t dwb_count = pool->res_cap->num_dwb;

	for (i = 0; i < dwb_count; i++) {
		struct dcn30_mmhubbub *mcif_wb30 = kzalloc(sizeof(struct dcn30_mmhubbub),
						    GFP_KERNEL);

		if (!mcif_wb30) {
			dm_error("DC: failed to create mcif_wb30!\n");
			return false;
		}

		dcn32_mmhubbub_construct(mcif_wb30, ctx,
				&mcif_wb30_regs[i],
				&mcif_wb30_shift,
				&mcif_wb30_mask,
				i);

		pool->mcif_wb[i] = &mcif_wb30->base;
	}
	return true;
}

static struct display_stream_compressor *dcn32_dsc_create(
	struct dc_context *ctx, uint32_t inst)
{
	struct dcn20_dsc *dsc =
		kzalloc(sizeof(struct dcn20_dsc), GFP_KERNEL);

	if (!dsc) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	dsc2_construct(dsc, ctx, inst, &dsc_regs[inst], &dsc_shift, &dsc_mask);

	dsc->max_image_width = 6016;

	return &dsc->base;
}

static void dcn32_destroy_resource_pool(struct resource_pool **pool)
{
	struct dcn32_resource_pool *dcn32_pool = TO_DCN32_RES_POOL(*pool);

	dcn32_resource_destruct(dcn32_pool);
	kfree(dcn32_pool);
	*pool = NULL;
}

bool dcn32_acquire_post_bldn_3dlut(
		struct resource_context *res_ctx,
		const struct resource_pool *pool,
		int mpcc_id,
		struct dc_3dlut **lut,
		struct dc_transfer_func **shaper)
{
	bool ret = false;
	union dc_3dlut_state *state;

	ASSERT(*lut == NULL && *shaper == NULL);
	*lut = NULL;
	*shaper = NULL;

	if (!res_ctx->is_mpc_3dlut_acquired[mpcc_id]) {
		*lut = pool->mpc_lut[mpcc_id];
		*shaper = pool->mpc_shaper[mpcc_id];
		state = &pool->mpc_lut[mpcc_id]->state;
		res_ctx->is_mpc_3dlut_acquired[mpcc_id] = true;
		ret = true;
	}
	return ret;
}

bool dcn32_release_post_bldn_3dlut(
		struct resource_context *res_ctx,
		const struct resource_pool *pool,
		struct dc_3dlut **lut,
		struct dc_transfer_func **shaper)
{
	int i;
	bool ret = false;

	for (i = 0; i < pool->res_cap->num_mpc_3dlut; i++) {
		if (pool->mpc_lut[i] == *lut && pool->mpc_shaper[i] == *shaper) {
			res_ctx->is_mpc_3dlut_acquired[i] = false;
			pool->mpc_lut[i]->state.raw = 0;
			*lut = NULL;
			*shaper = NULL;
			ret = true;
			break;
		}
	}
	return ret;
}

/**
 ********************************************************************************************
 * dcn32_get_num_free_pipes: Calculate number of free pipes
 *
 * This function assumes that a "used" pipe is a pipe that has
 * both a stream and a plane assigned to it.
 *
 * @param [in] dc: current dc state
 * @param [in] context: new dc state
 *
 * @return: Number of free pipes available in the context
 *
 ********************************************************************************************
 */
static unsigned int dcn32_get_num_free_pipes(struct dc *dc, struct dc_state *context)
{
	unsigned int i;
	unsigned int free_pipes = 0;
	unsigned int num_pipes = 0;

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];

		if (pipe->stream && pipe->plane_state && !pipe->top_pipe) {
			while (pipe) {
				num_pipes++;
				pipe = pipe->bottom_pipe;
			}
		}
	}

	free_pipes = dc->res_pool->pipe_count - num_pipes;
	return free_pipes;
}

/**
 ********************************************************************************************
 * dcn32_assign_subvp_pipe: Function to decide which pipe will use Sub-VP.
 *
 * We enter this function if we are Sub-VP capable (i.e. enough pipes available)
 * and regular P-State switching (i.e. VACTIVE/VBLANK) is not supported, or if
 * we are forcing SubVP P-State switching on the current config.
 *
 * The number of pipes used for the chosen surface must be less than or equal to the
 * number of free pipes available.
 *
 * In general we choose surfaces that have ActiveDRAMClockChangeLatencyMargin <= 0 first,
 * then among those surfaces we choose the one with the smallest VBLANK time. We only consider
 * surfaces with ActiveDRAMClockChangeLatencyMargin > 0 if we are forcing a Sub-VP config.
 *
 * @param [in] dc: current dc state
 * @param [in] context: new dc state
 * @param [out] index: dc pipe index for the pipe chosen to have phantom pipes assigned
 *
 * @return: True if a valid pipe assignment was found for Sub-VP. Otherwise false.
 *
 ********************************************************************************************
 */

static bool dcn32_assign_subvp_pipe(struct dc *dc,
		struct dc_state *context,
		unsigned int *index)
{
	unsigned int i, pipe_idx;
	unsigned int min_vblank_us = INT_MAX;
	struct vba_vars_st *vba = &context->bw_ctx.dml.vba;
	bool valid_assignment_found = false;
	unsigned int free_pipes = dcn32_get_num_free_pipes(dc, context);

	for (i = 0, pipe_idx = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];
		unsigned int num_pipes = 0;

		if (!pipe->stream)
			continue;

		if (pipe->plane_state && !pipe->top_pipe &&
				pipe->stream->mall_stream_config.type == SUBVP_NONE) {
			while (pipe) {
				num_pipes++;
				pipe = pipe->bottom_pipe;
			}

			pipe = &context->res_ctx.pipe_ctx[i];
			if (num_pipes <= free_pipes) {
				struct dc_stream_state *stream = pipe->stream;
				unsigned int vblank_us = ((stream->timing.v_total - stream->timing.v_addressable) *
							stream->timing.h_total /
							(double)(stream->timing.pix_clk_100hz * 100)) * 1000000;
				if (vba->ActiveDRAMClockChangeLatencyMargin[vba->pipe_plane[pipe_idx]] <= 0 &&
						vblank_us < min_vblank_us) {
					*index = i;
					min_vblank_us = vblank_us;
					valid_assignment_found = true;
				} else if (vba->ActiveDRAMClockChangeLatencyMargin[vba->pipe_plane[pipe_idx]] > 0 &&
						dc->debug.force_subvp_mclk_switch && !valid_assignment_found) {
					// Handle case for forcing Sub-VP config. In this case we can assign
					// phantom pipes to a surface that has active margin > 0.
					*index = i;
					valid_assignment_found = true;
				}
			}
		}
		pipe_idx++;
	}
	return valid_assignment_found;
}

/**
 * ***************************************************************************************
 * dcn32_enough_pipes_for_subvp: Function to check if there are "enough" pipes for SubVP.
 *
 * This function returns true if there are enough free pipes
 * to create the required phantom pipes for any given stream
 * (that does not already have phantom pipe assigned).
 *
 * e.g. For a 2 stream config where the first stream uses one
 * pipe and the second stream uses 2 pipes (i.e. pipe split),
 * this function will return true because there is 1 remaining
 * pipe which can be used as the phantom pipe for the non pipe
 * split pipe.
 *
 * @param [in] dc: current dc state
 * @param [in] context: new dc state
 *
 * @return: True if there are enough free pipes to assign phantom pipes to at least one
 *          stream that does not already have phantom pipes assigned. Otherwise false.
 *
 * ***************************************************************************************
 */
static bool dcn32_enough_pipes_for_subvp(struct dc *dc, struct dc_state *context)
{
	unsigned int i, split_cnt, free_pipes;
	unsigned int min_pipe_split = dc->res_pool->pipe_count + 1; // init as max number of pipes + 1
	bool subvp_possible = false;

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];

		// Find the minimum pipe split count for non SubVP pipes
		if (pipe->stream && pipe->plane_state && !pipe->top_pipe &&
				pipe->stream->mall_stream_config.type == SUBVP_NONE) {
			split_cnt = 0;
			while (pipe) {
				split_cnt++;
				pipe = pipe->bottom_pipe;
			}

			if (split_cnt < min_pipe_split)
				min_pipe_split = split_cnt;
		}
	}

	free_pipes = dcn32_get_num_free_pipes(dc, context);

	// SubVP only possible if at least one pipe is being used (i.e. free_pipes
	// should not equal to the pipe_count)
	if (free_pipes >= min_pipe_split && free_pipes < dc->res_pool->pipe_count)
		subvp_possible = true;

	return subvp_possible;
}

static void dcn32_enable_phantom_plane(struct dc *dc,
		struct dc_state *context,
		struct dc_stream_state *phantom_stream,
		unsigned int dc_pipe_idx)
{
	struct dc_plane_state *phantom_plane = NULL;
	struct dc_plane_state *prev_phantom_plane = NULL;
	struct pipe_ctx *curr_pipe = &context->res_ctx.pipe_ctx[dc_pipe_idx];

	while (curr_pipe) {
		if (curr_pipe->top_pipe && curr_pipe->top_pipe->plane_state == curr_pipe->plane_state)
			phantom_plane = prev_phantom_plane;
		else
			phantom_plane = dc_create_plane_state(dc);

		memcpy(&phantom_plane->address, &curr_pipe->plane_state->address, sizeof(phantom_plane->address));
		memcpy(&phantom_plane->scaling_quality, &curr_pipe->plane_state->scaling_quality,
				sizeof(phantom_plane->scaling_quality));
		memcpy(&phantom_plane->src_rect, &curr_pipe->plane_state->src_rect, sizeof(phantom_plane->src_rect));
		memcpy(&phantom_plane->dst_rect, &curr_pipe->plane_state->dst_rect, sizeof(phantom_plane->dst_rect));
		memcpy(&phantom_plane->clip_rect, &curr_pipe->plane_state->clip_rect, sizeof(phantom_plane->clip_rect));
		memcpy(&phantom_plane->plane_size, &curr_pipe->plane_state->plane_size,
				sizeof(phantom_plane->plane_size));
		memcpy(&phantom_plane->tiling_info, &curr_pipe->plane_state->tiling_info,
				sizeof(phantom_plane->tiling_info));
		memcpy(&phantom_plane->dcc, &curr_pipe->plane_state->dcc, sizeof(phantom_plane->dcc));
		phantom_plane->format = curr_pipe->plane_state->format;
		phantom_plane->rotation = curr_pipe->plane_state->rotation;
		phantom_plane->visible = curr_pipe->plane_state->visible;

		/* Shadow pipe has small viewport. */
		phantom_plane->clip_rect.y = 0;
		phantom_plane->clip_rect.height = phantom_stream->timing.v_addressable;

		dc_add_plane_to_context(dc, phantom_stream, phantom_plane, context);

		curr_pipe = curr_pipe->bottom_pipe;
		prev_phantom_plane = phantom_plane;
	}
}

/**
 * ***************************************************************************************
 * dcn32_set_phantom_stream_timing: Set timing params for the phantom stream
 *
 * Set timing params of the phantom stream based on calculated output from DML.
 * This function first gets the DML pipe index using the DC pipe index, then
 * calls into DML (get_subviewport_lines_needed_in_mall) to get the number of
 * lines required for SubVP MCLK switching and assigns to the phantom stream
 * accordingly.
 *
 * - The number of SubVP lines calculated in DML does not take into account
 * FW processing delays and required pstate allow width, so we must include
 * that separately.
 *
 * - Set phantom backporch = vstartup of main pipe
 *
 * @param [in] dc: current dc state
 * @param [in] context: new dc state
 * @param [in] ref_pipe: Main pipe for the phantom stream
 * @param [in] pipes: DML pipe params
 * @param [in] pipe_cnt: number of DML pipes
 * @param [in] dc_pipe_idx: DC pipe index for the main pipe (i.e. ref_pipe)
 *
 * @return: void
 *
 * ***************************************************************************************
 */
static void dcn32_set_phantom_stream_timing(struct dc *dc,
		struct dc_state *context,
		struct pipe_ctx *ref_pipe,
		struct dc_stream_state *phantom_stream,
		display_e2e_pipe_params_st *pipes,
		unsigned int pipe_cnt,
		unsigned int dc_pipe_idx)
{
	unsigned int i, pipe_idx;
	struct pipe_ctx *pipe;
	uint32_t phantom_vactive, phantom_bp, pstate_width_fw_delay_lines;
	unsigned int vlevel = context->bw_ctx.dml.vba.VoltageLevel;
	unsigned int dcfclk = context->bw_ctx.dml.vba.DCFCLKState[vlevel][context->bw_ctx.dml.vba.maxMpcComb];
	unsigned int socclk = context->bw_ctx.dml.vba.SOCCLKPerState[vlevel];

	// Find DML pipe index (pipe_idx) using dc_pipe_idx
	for (i = 0, pipe_idx = 0; i < dc->res_pool->pipe_count; i++) {
		pipe = &context->res_ctx.pipe_ctx[i];

		if (!pipe->stream)
			continue;

		if (i == dc_pipe_idx)
			break;

		pipe_idx++;
	}

	// Calculate lines required for pstate allow width and FW processing delays
	pstate_width_fw_delay_lines = ((double)(dc->caps.subvp_fw_processing_delay_us +
			dc->caps.subvp_pstate_allow_width_us) / 1000000) *
			(ref_pipe->stream->timing.pix_clk_100hz * 100) /
			(double)ref_pipe->stream->timing.h_total;

	// Update clks_cfg for calling into recalculate
	pipes[0].clks_cfg.voltage = vlevel;
	pipes[0].clks_cfg.dcfclk_mhz = dcfclk;
	pipes[0].clks_cfg.socclk_mhz = socclk;

	// DML calculation for MALL region doesn't take into account FW delay
	// and required pstate allow width for multi-display cases
	phantom_vactive = get_subviewport_lines_needed_in_mall(&context->bw_ctx.dml, pipes, pipe_cnt, pipe_idx) +
				pstate_width_fw_delay_lines;

	// For backporch of phantom pipe, use vstartup of the main pipe
	phantom_bp = get_vstartup(&context->bw_ctx.dml, pipes, pipe_cnt, pipe_idx);

	phantom_stream->dst.y = 0;
	phantom_stream->dst.height = phantom_vactive;
	phantom_stream->src.y = 0;
	phantom_stream->src.height = phantom_vactive;

	phantom_stream->timing.v_addressable = phantom_vactive;
	phantom_stream->timing.v_front_porch = 1;
	phantom_stream->timing.v_total = phantom_stream->timing.v_addressable +
						phantom_stream->timing.v_front_porch +
						phantom_stream->timing.v_sync_width +
						phantom_bp;
}

static struct dc_stream_state *dcn32_enable_phantom_stream(struct dc *dc,
		struct dc_state *context,
		display_e2e_pipe_params_st *pipes,
		unsigned int pipe_cnt,
		unsigned int dc_pipe_idx)
{
	struct dc_stream_state *phantom_stream = NULL;
	struct pipe_ctx *ref_pipe = &context->res_ctx.pipe_ctx[dc_pipe_idx];

	phantom_stream = dc_create_stream_for_sink(ref_pipe->stream->sink);
	phantom_stream->signal = SIGNAL_TYPE_VIRTUAL;
	phantom_stream->dpms_off = true;
	phantom_stream->mall_stream_config.type = SUBVP_PHANTOM;
	phantom_stream->mall_stream_config.paired_stream = ref_pipe->stream;
	ref_pipe->stream->mall_stream_config.type = SUBVP_MAIN;
	ref_pipe->stream->mall_stream_config.paired_stream = phantom_stream;

	/* stream has limited viewport and small timing */
	memcpy(&phantom_stream->timing, &ref_pipe->stream->timing, sizeof(phantom_stream->timing));
	memcpy(&phantom_stream->src, &ref_pipe->stream->src, sizeof(phantom_stream->src));
	memcpy(&phantom_stream->dst, &ref_pipe->stream->dst, sizeof(phantom_stream->dst));
	dcn32_set_phantom_stream_timing(dc, context, ref_pipe, phantom_stream, pipes, pipe_cnt, dc_pipe_idx);

	dc_add_stream_to_ctx(dc, context, phantom_stream);
	return phantom_stream;
}

void dcn32_remove_phantom_pipes(struct dc *dc, struct dc_state *context)
{
	int i;
	bool removed_pipe = false;

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];
		// build scaling params for phantom pipes
		if (pipe->plane_state && pipe->stream && pipe->stream->mall_stream_config.type == SUBVP_PHANTOM) {
			dc_rem_all_planes_for_stream(dc, pipe->stream, context);
			dc_remove_stream_from_ctx(dc, context, pipe->stream);
			removed_pipe = true;
		}

		// Clear all phantom stream info
		if (pipe->stream) {
			pipe->stream->mall_stream_config.type = SUBVP_NONE;
			pipe->stream->mall_stream_config.paired_stream = NULL;
		}
	}
	if (removed_pipe)
		dc->hwss.apply_ctx_to_hw(dc, context);
}

/* TODO: Input to this function should indicate which pipe indexes (or streams)
 * require a phantom pipe / stream
 */
void dcn32_add_phantom_pipes(struct dc *dc, struct dc_state *context,
		display_e2e_pipe_params_st *pipes,
		unsigned int pipe_cnt,
		unsigned int index)
{
	struct dc_stream_state *phantom_stream = NULL;
	unsigned int i;

	// The index of the DC pipe passed into this function is guarenteed to
	// be a valid candidate for SubVP (i.e. has a plane, stream, doesn't
	// already have phantom pipe assigned, etc.) by previous checks.
	phantom_stream = dcn32_enable_phantom_stream(dc, context, pipes, pipe_cnt, index);
	dcn32_enable_phantom_plane(dc, context, phantom_stream, index);

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];

		// Build scaling params for phantom pipes which were newly added.
		// We determine which phantom pipes were added by comparing with
		// the phantom stream.
		if (pipe->plane_state && pipe->stream && pipe->stream == phantom_stream &&
				pipe->stream->mall_stream_config.type == SUBVP_PHANTOM) {
			pipe->stream->use_dynamic_meta = false;
			pipe->plane_state->flip_immediate = false;
			if (!resource_build_scaling_params(pipe)) {
				// Log / remove phantom pipes since failed to build scaling params
			}
		}
	}
}

static bool dcn32_split_stream_for_mpc_or_odm(
		const struct dc *dc,
		struct resource_context *res_ctx,
		struct pipe_ctx *pri_pipe,
		struct pipe_ctx *sec_pipe,
		bool odm)
{
	int pipe_idx = sec_pipe->pipe_idx;
	const struct resource_pool *pool = dc->res_pool;

	if (pri_pipe->plane_state) {
		/* ODM + window MPO, where MPO window is on left half only */
		if (pri_pipe->plane_state->clip_rect.x + pri_pipe->plane_state->clip_rect.width <=
				pri_pipe->stream->src.x + pri_pipe->stream->src.width/2)
			return true;

		/* ODM + window MPO, where MPO window is on right half only */
		if (pri_pipe->plane_state->clip_rect.x >= pri_pipe->stream->src.width/2)
			return true;
	}

	*sec_pipe = *pri_pipe;

	sec_pipe->pipe_idx = pipe_idx;
	sec_pipe->plane_res.mi = pool->mis[pipe_idx];
	sec_pipe->plane_res.hubp = pool->hubps[pipe_idx];
	sec_pipe->plane_res.ipp = pool->ipps[pipe_idx];
	sec_pipe->plane_res.xfm = pool->transforms[pipe_idx];
	sec_pipe->plane_res.dpp = pool->dpps[pipe_idx];
	sec_pipe->plane_res.mpcc_inst = pool->dpps[pipe_idx]->inst;
	sec_pipe->stream_res.dsc = NULL;
	if (odm) {
		if (pri_pipe->next_odm_pipe) {
			ASSERT(pri_pipe->next_odm_pipe != sec_pipe);
			sec_pipe->next_odm_pipe = pri_pipe->next_odm_pipe;
			sec_pipe->next_odm_pipe->prev_odm_pipe = sec_pipe;
		}
		if (pri_pipe->top_pipe && pri_pipe->top_pipe->next_odm_pipe) {
			pri_pipe->top_pipe->next_odm_pipe->bottom_pipe = sec_pipe;
			sec_pipe->top_pipe = pri_pipe->top_pipe->next_odm_pipe;
		}
		if (pri_pipe->bottom_pipe && pri_pipe->bottom_pipe->next_odm_pipe) {
			pri_pipe->bottom_pipe->next_odm_pipe->top_pipe = sec_pipe;
			sec_pipe->bottom_pipe = pri_pipe->bottom_pipe->next_odm_pipe;
		}
		pri_pipe->next_odm_pipe = sec_pipe;
		sec_pipe->prev_odm_pipe = pri_pipe;
		ASSERT(sec_pipe->top_pipe == NULL);

		if (!sec_pipe->top_pipe)
			sec_pipe->stream_res.opp = pool->opps[pipe_idx];
		else
			sec_pipe->stream_res.opp = sec_pipe->top_pipe->stream_res.opp;
		if (sec_pipe->stream->timing.flags.DSC == 1) {
			dcn20_acquire_dsc(dc, res_ctx, &sec_pipe->stream_res.dsc, pipe_idx);
			ASSERT(sec_pipe->stream_res.dsc);
			if (sec_pipe->stream_res.dsc == NULL)
				return false;
		}
	} else {
		if (pri_pipe->bottom_pipe) {
			ASSERT(pri_pipe->bottom_pipe != sec_pipe);
			sec_pipe->bottom_pipe = pri_pipe->bottom_pipe;
			sec_pipe->bottom_pipe->top_pipe = sec_pipe;
		}
		pri_pipe->bottom_pipe = sec_pipe;
		sec_pipe->top_pipe = pri_pipe;

		ASSERT(pri_pipe->plane_state);
	}

	return true;
}

static struct pipe_ctx *dcn32_find_split_pipe(
		struct dc *dc,
		struct dc_state *context,
		int old_index)
{
	struct pipe_ctx *pipe = NULL;
	int i;

	if (old_index >= 0 && context->res_ctx.pipe_ctx[old_index].stream == NULL) {
		pipe = &context->res_ctx.pipe_ctx[old_index];
		pipe->pipe_idx = old_index;
	}

	if (!pipe)
		for (i = dc->res_pool->pipe_count - 1; i >= 0; i--) {
			if (dc->current_state->res_ctx.pipe_ctx[i].top_pipe == NULL
					&& dc->current_state->res_ctx.pipe_ctx[i].prev_odm_pipe == NULL) {
				if (context->res_ctx.pipe_ctx[i].stream == NULL) {
					pipe = &context->res_ctx.pipe_ctx[i];
					pipe->pipe_idx = i;
					break;
				}
			}
		}

	/*
	 * May need to fix pipes getting tossed from 1 opp to another on flip
	 * Add for debugging transient underflow during topology updates:
	 * ASSERT(pipe);
	 */
	if (!pipe)
		for (i = dc->res_pool->pipe_count - 1; i >= 0; i--) {
			if (context->res_ctx.pipe_ctx[i].stream == NULL) {
				pipe = &context->res_ctx.pipe_ctx[i];
				pipe->pipe_idx = i;
				break;
			}
		}

	return pipe;
}


/**
 * ***************************************************************************************
 * subvp_subvp_schedulable: Determine if SubVP + SubVP config is schedulable
 *
 * High level algorithm:
 * 1. Find longest microschedule length (in us) between the two SubVP pipes
 * 2. Check if the worst case overlap (VBLANK in middle of ACTIVE) for both
 * pipes still allows for the maximum microschedule to fit in the active
 * region for both pipes.
 *
 * @param [in] dc: current dc state
 * @param [in] context: new dc state
 *
 * @return: bool - True if the SubVP + SubVP config is schedulable, false otherwise
 *
 * ***************************************************************************************
 */
static bool subvp_subvp_schedulable(struct dc *dc, struct dc_state *context)
{
	struct pipe_ctx *subvp_pipes[2];
	struct dc_stream_state *phantom = NULL;
	uint32_t microschedule_lines = 0;
	uint32_t index = 0;
	uint32_t i;
	uint32_t max_microschedule_us = 0;
	int32_t vactive1_us, vactive2_us, vblank1_us, vblank2_us;

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];
		uint32_t time_us = 0;

		/* Loop to calculate the maximum microschedule time between the two SubVP pipes,
		 * and also to store the two main SubVP pipe pointers in subvp_pipes[2].
		 */
		if (pipe->stream && pipe->plane_state && !pipe->top_pipe &&
				pipe->stream->mall_stream_config.type == SUBVP_MAIN) {
			phantom = pipe->stream->mall_stream_config.paired_stream;
			microschedule_lines = (phantom->timing.v_total - phantom->timing.v_front_porch) +
					phantom->timing.v_addressable;

			// Round up when calculating microschedule time
			time_us = ((microschedule_lines * phantom->timing.h_total +
					phantom->timing.pix_clk_100hz * 100 - 1) /
					(double)(phantom->timing.pix_clk_100hz * 100)) * 1000000 +
						dc->caps.subvp_prefetch_end_to_mall_start_us +
						dc->caps.subvp_fw_processing_delay_us;
			if (time_us > max_microschedule_us)
				max_microschedule_us = time_us;

			subvp_pipes[index] = pipe;
			index++;

			// Maximum 2 SubVP pipes
			if (index == 2)
				break;
		}
	}
	vactive1_us = ((subvp_pipes[0]->stream->timing.v_addressable * subvp_pipes[0]->stream->timing.h_total) /
			(double)(subvp_pipes[0]->stream->timing.pix_clk_100hz * 100)) * 1000000;
	vactive2_us = ((subvp_pipes[1]->stream->timing.v_addressable * subvp_pipes[1]->stream->timing.h_total) /
				(double)(subvp_pipes[1]->stream->timing.pix_clk_100hz * 100)) * 1000000;
	vblank1_us = (((subvp_pipes[0]->stream->timing.v_total - subvp_pipes[0]->stream->timing.v_addressable) *
			subvp_pipes[0]->stream->timing.h_total) /
			(double)(subvp_pipes[0]->stream->timing.pix_clk_100hz * 100)) * 1000000;
	vblank2_us = (((subvp_pipes[1]->stream->timing.v_total - subvp_pipes[1]->stream->timing.v_addressable) *
			subvp_pipes[1]->stream->timing.h_total) /
			(double)(subvp_pipes[1]->stream->timing.pix_clk_100hz * 100)) * 1000000;

	if ((vactive1_us - vblank2_us) / 2 > max_microschedule_us &&
			(vactive2_us - vblank1_us) / 2 > max_microschedule_us)
		return true;

	return false;
}

/**
 * ***************************************************************************************
 * subvp_drr_schedulable: Determine if SubVP + DRR config is schedulable
 *
 * High level algorithm:
 * 1. Get timing for SubVP pipe, phantom pipe, and DRR pipe
 * 2. Determine the frame time for the DRR display when adding required margin for MCLK switching
 * (the margin is equal to the MALL region + DRR margin (500us))
 * 3.If (SubVP Active - Prefetch > Stretched DRR frame + max(MALL region, Stretched DRR frame))
 * then report the configuration as supported
 *
 * @param [in] dc: current dc state
 * @param [in] context: new dc state
 * @param [in] drr_pipe: DRR pipe_ctx for the SubVP + DRR config
 *
 * @return: bool - True if the SubVP + DRR config is schedulable, false otherwise
 *
 * ***************************************************************************************
 */
static bool subvp_drr_schedulable(struct dc *dc, struct dc_state *context, struct pipe_ctx *drr_pipe)
{
	bool schedulable = false;
	uint32_t i;
	struct pipe_ctx *pipe = NULL;
	struct dc_crtc_timing *main_timing = NULL;
	struct dc_crtc_timing *phantom_timing = NULL;
	struct dc_crtc_timing *drr_timing = NULL;
	int16_t prefetch_us = 0;
	int16_t mall_region_us = 0;
	int16_t drr_frame_us = 0;	// nominal frame time
	int16_t subvp_active_us = 0;
	int16_t stretched_drr_us = 0;
	int16_t drr_stretched_vblank_us = 0;
	int16_t max_vblank_mallregion = 0;

	// Find SubVP pipe
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		pipe = &context->res_ctx.pipe_ctx[i];

		// We check for master pipe, but it shouldn't matter since we only need
		// the pipe for timing info (stream should be same for any pipe splits)
		if (!pipe->stream || !pipe->plane_state || pipe->top_pipe || pipe->prev_odm_pipe)
			continue;

		// Find the SubVP pipe
		if (pipe->stream->mall_stream_config.type == SUBVP_MAIN)
			break;
	}

	main_timing = &pipe->stream->timing;
	phantom_timing = &pipe->stream->mall_stream_config.paired_stream->timing;
	drr_timing = &drr_pipe->stream->timing;
	prefetch_us = (phantom_timing->v_total - phantom_timing->v_front_porch) * phantom_timing->h_total /
			(double)(phantom_timing->pix_clk_100hz * 100) * 1000000 +
			dc->caps.subvp_prefetch_end_to_mall_start_us;
	subvp_active_us = main_timing->v_addressable * main_timing->h_total /
			(double)(main_timing->pix_clk_100hz * 100) * 1000000;
	drr_frame_us = drr_timing->v_total * drr_timing->h_total /
			(double)(drr_timing->pix_clk_100hz * 100) * 1000000;
	// P-State allow width and FW delays already included phantom_timing->v_addressable
	mall_region_us = phantom_timing->v_addressable * phantom_timing->h_total /
			(double)(phantom_timing->pix_clk_100hz * 100) * 1000000;
	stretched_drr_us = drr_frame_us + mall_region_us + SUBVP_DRR_MARGIN_US;
	drr_stretched_vblank_us = (drr_timing->v_total - drr_timing->v_addressable) * drr_timing->h_total /
			(double)(drr_timing->pix_clk_100hz * 100) * 1000000 + (stretched_drr_us - drr_frame_us);
	max_vblank_mallregion = drr_stretched_vblank_us > mall_region_us ? drr_stretched_vblank_us : mall_region_us;

	/* We consider SubVP + DRR schedulable if the stretched frame duration of the DRR display (i.e. the
	 * highest refresh rate + margin that can support UCLK P-State switch) passes the static analysis
	 * for VBLANK: (VACTIVE region of the SubVP pipe can fit the MALL prefetch, VBLANK frame time,
	 * and the max of (VBLANK blanking time, MALL region)).
	 */
	if (stretched_drr_us < (1 / (double)drr_timing->min_refresh_in_uhz) * 1000000 * 1000000 &&
			subvp_active_us - prefetch_us - stretched_drr_us - max_vblank_mallregion > 0)
		schedulable = true;

	return schedulable;
}

/**
 * ***************************************************************************************
 * subvp_vblank_schedulable: Determine if SubVP + VBLANK config is schedulable
 *
 * High level algorithm:
 * 1. Get timing for SubVP pipe, phantom pipe, and VBLANK pipe
 * 2. If (SubVP Active - Prefetch > Vblank Frame Time + max(MALL region, Vblank blanking time))
 * then report the configuration as supported
 * 3. If the VBLANK display is DRR, then take the DRR static schedulability path
 *
 * @param [in] dc: current dc state
 * @param [in] context: new dc state
 *
 * @return: bool - True if the SubVP + VBLANK/DRR config is schedulable, false otherwise
 *
 * ***************************************************************************************
 */
static bool subvp_vblank_schedulable(struct dc *dc, struct dc_state *context)
{
	struct pipe_ctx *pipe = NULL;
	struct pipe_ctx *subvp_pipe = NULL;
	bool found = false;
	bool schedulable = false;
	uint32_t i = 0;
	uint8_t vblank_index = 0;
	int16_t prefetch_us = 0;
	int16_t mall_region_us = 0;
	int16_t vblank_frame_us = 0;
	int16_t subvp_active_us = 0;
	int16_t vblank_blank_us = 0;
	int16_t max_vblank_mallregion = 0;
	struct dc_crtc_timing *main_timing = NULL;
	struct dc_crtc_timing *phantom_timing = NULL;
	struct dc_crtc_timing *vblank_timing = NULL;

	/* For SubVP + VBLANK/DRR cases, we assume there can only be
	 * a single VBLANK/DRR display. If DML outputs SubVP + VBLANK
	 * is supported, it is either a single VBLANK case or two VBLANK
	 * displays which are synchronized (in which case they have identical
	 * timings).
	 */
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		pipe = &context->res_ctx.pipe_ctx[i];

		// We check for master pipe, but it shouldn't matter since we only need
		// the pipe for timing info (stream should be same for any pipe splits)
		if (!pipe->stream || !pipe->plane_state || pipe->top_pipe || pipe->prev_odm_pipe)
			continue;

		if (!found && pipe->stream->mall_stream_config.type == SUBVP_NONE) {
			// Found pipe which is not SubVP or Phantom (i.e. the VBLANK pipe).
			vblank_index = i;
			found = true;
		}

		if (!subvp_pipe && pipe->stream->mall_stream_config.type == SUBVP_MAIN)
			subvp_pipe = pipe;
	}
	// Use ignore_msa_timing_param flag to identify as DRR
	if (found && pipe->stream->ignore_msa_timing_param) {
		// SUBVP + DRR case
		schedulable = subvp_drr_schedulable(dc, context, &context->res_ctx.pipe_ctx[vblank_index]);
	} else if (found) {
		main_timing = &subvp_pipe->stream->timing;
		phantom_timing = &subvp_pipe->stream->mall_stream_config.paired_stream->timing;
		vblank_timing = &context->res_ctx.pipe_ctx[vblank_index].stream->timing;
		// Prefetch time is equal to VACTIVE + BP + VSYNC of the phantom pipe
		// Also include the prefetch end to mallstart delay time
		prefetch_us = (phantom_timing->v_total - phantom_timing->v_front_porch) * phantom_timing->h_total /
				(double)(phantom_timing->pix_clk_100hz * 100) * 1000000 +
				dc->caps.subvp_prefetch_end_to_mall_start_us;
		// P-State allow width and FW delays already included phantom_timing->v_addressable
		mall_region_us = phantom_timing->v_addressable * phantom_timing->h_total /
				(double)(phantom_timing->pix_clk_100hz * 100) * 1000000;
		vblank_frame_us = vblank_timing->v_total * vblank_timing->h_total /
				(double)(vblank_timing->pix_clk_100hz * 100) * 1000000;
		vblank_blank_us =  (vblank_timing->v_total - vblank_timing->v_addressable) * vblank_timing->h_total /
				(double)(vblank_timing->pix_clk_100hz * 100) * 1000000;
		subvp_active_us = main_timing->v_addressable * main_timing->h_total /
				(double)(main_timing->pix_clk_100hz * 100) * 1000000;
		max_vblank_mallregion = vblank_blank_us > mall_region_us ? vblank_blank_us : mall_region_us;

		// Schedulable if VACTIVE region of the SubVP pipe can fit the MALL prefetch, VBLANK frame time,
		// and the max of (VBLANK blanking time, MALL region)
		// TODO: Possibly add some margin (i.e. the below conditions should be [...] > X instead of [...] > 0)
		if (subvp_active_us - prefetch_us - vblank_frame_us - max_vblank_mallregion > 0)
			schedulable = true;
	}
	return schedulable;
}

/**
 * ********************************************************************************************
 * subvp_validate_static_schedulability: Check which SubVP case is calculated and handle
 * static analysis based on the case.
 *
 * Three cases:
 * 1. SubVP + SubVP
 * 2. SubVP + VBLANK (DRR checked internally)
 * 3. SubVP + VACTIVE (currently unsupported)
 *
 * @param [in] dc: current dc state
 * @param [in] context: new dc state
 * @param [in] vlevel: Voltage level calculated by DML
 *
 * @return: bool - True if statically schedulable, false otherwise
 *
 * ********************************************************************************************
 */
static bool subvp_validate_static_schedulability(struct dc *dc,
				struct dc_state *context,
				int vlevel)
{
	bool schedulable = true;	// true by default for single display case
	struct vba_vars_st *vba = &context->bw_ctx.dml.vba;
	uint32_t i, pipe_idx;
	uint8_t subvp_count = 0;
	uint8_t vactive_count = 0;

	for (i = 0, pipe_idx = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];

		if (!pipe->stream)
			continue;

		if (pipe->plane_state && !pipe->top_pipe &&
				pipe->stream->mall_stream_config.type == SUBVP_MAIN)
			subvp_count++;

		// Count how many planes are capable of VACTIVE switching (SubVP + VACTIVE unsupported)
		if (vba->ActiveDRAMClockChangeLatencyMargin[vba->pipe_plane[pipe_idx]] > 0) {
			vactive_count++;
		}
		pipe_idx++;
	}

	if (subvp_count == 2) {
		// Static schedulability check for SubVP + SubVP case
		schedulable = subvp_subvp_schedulable(dc, context);
	} else if (vba->DRAMClockChangeSupport[vlevel][vba->maxMpcComb] == dm_dram_clock_change_vblank_w_mall_sub_vp) {
		// Static schedulability check for SubVP + VBLANK case. Also handle the case where
		// DML outputs SubVP + VBLANK + VACTIVE (DML will report as SubVP + VBLANK)
		if (vactive_count > 0)
			schedulable = false;
		else
			schedulable = subvp_vblank_schedulable(dc, context);
	} else if (vba->DRAMClockChangeSupport[vlevel][vba->maxMpcComb] == dm_dram_clock_change_vactive_w_mall_sub_vp) {
		// SubVP + VACTIVE currently unsupported
		schedulable = false;
	}
	return schedulable;
}

static void dcn32_full_validate_bw_helper(struct dc *dc,
		struct dc_state *context,
		display_e2e_pipe_params_st *pipes,
		int *vlevel,
		int *split,
		bool *merge,
		int *pipe_cnt)
{
	struct vba_vars_st *vba = &context->bw_ctx.dml.vba;
	unsigned int dc_pipe_idx = 0;
	bool found_supported_config = false;
	struct pipe_ctx *pipe = NULL;
	uint32_t non_subvp_pipes = 0;
	bool drr_pipe_found = false;
	uint32_t drr_pipe_index = 0;
	uint32_t i = 0;

	/*
	 * DML favors voltage over p-state, but we're more interested in
	 * supporting p-state over voltage. We can't support p-state in
	 * prefetch mode > 0 so try capping the prefetch mode to start.
	 */
	context->bw_ctx.dml.soc.allow_for_pstate_or_stutter_in_vblank_final =
			dm_prefetch_support_uclk_fclk_and_stutter;
	*vlevel = dml_get_voltage_level(&context->bw_ctx.dml, pipes, *pipe_cnt);
	/* This may adjust vlevel and maxMpcComb */
	if (*vlevel < context->bw_ctx.dml.soc.num_states)
		*vlevel = dcn20_validate_apply_pipe_split_flags(dc, context, *vlevel, split, merge);

	/* Conditions for setting up phantom pipes for SubVP:
	 * 1. Not force disable SubVP
	 * 2. Full update (i.e. !fast_validate)
	 * 3. Enough pipes are available to support SubVP (TODO: Which pipes will use VACTIVE / VBLANK / SUBVP?)
	 * 4. Display configuration passes validation
	 * 5. (Config doesn't support MCLK in VACTIVE/VBLANK || dc->debug.force_subvp_mclk_switch)
	 */
	if (!dc->debug.force_disable_subvp &&
			(*vlevel == context->bw_ctx.dml.soc.num_states ||
			vba->DRAMClockChangeSupport[*vlevel][vba->maxMpcComb] == dm_dram_clock_change_unsupported ||
			dc->debug.force_subvp_mclk_switch)) {

		while (!found_supported_config && dcn32_enough_pipes_for_subvp(dc, context) &&
				dcn32_assign_subvp_pipe(dc, context, &dc_pipe_idx)) {

			dc->res_pool->funcs->add_phantom_pipes(dc, context, pipes, *pipe_cnt, dc_pipe_idx);

			*pipe_cnt = dc->res_pool->funcs->populate_dml_pipes(dc, context, pipes, false);
			*vlevel = dml_get_voltage_level(&context->bw_ctx.dml, pipes, *pipe_cnt);

			if (*vlevel < context->bw_ctx.dml.soc.num_states &&
					vba->DRAMClockChangeSupport[*vlevel][vba->maxMpcComb] != dm_dram_clock_change_unsupported
					&& subvp_validate_static_schedulability(dc, context, *vlevel)) {
				found_supported_config = true;
			} else if (*vlevel < context->bw_ctx.dml.soc.num_states &&
					vba->DRAMClockChangeSupport[*vlevel][vba->maxMpcComb] == dm_dram_clock_change_unsupported) {
				/* Case where 1 SubVP is added, and DML reports MCLK unsupported. This handles
				 * the case for SubVP + DRR, where the DRR display does not support MCLK switch
				 * at it's native refresh rate / timing.
				 */
				for (i = 0; i < dc->res_pool->pipe_count; i++) {
					pipe = &context->res_ctx.pipe_ctx[i];
					if (pipe->stream && pipe->plane_state && !pipe->top_pipe &&
							pipe->stream->mall_stream_config.type == SUBVP_NONE) {
						non_subvp_pipes++;
						// Use ignore_msa_timing_param flag to identify as DRR
						if (pipe->stream->ignore_msa_timing_param) {
							drr_pipe_found = true;
							drr_pipe_index = i;
						}
					}
				}
				// If there is only 1 remaining non SubVP pipe that is DRR, check static
				// schedulability for SubVP + DRR.
				if (non_subvp_pipes == 1 && drr_pipe_found) {
					found_supported_config = subvp_drr_schedulable(dc,
							context, &context->res_ctx.pipe_ctx[drr_pipe_index]);
				}
			}
		}

		// If SubVP pipe config is unsupported (or cannot be used for UCLK switching)
		// remove phantom pipes and repopulate dml pipes
		if (!found_supported_config) {
			dc->res_pool->funcs->remove_phantom_pipes(dc, context);
			*pipe_cnt = dc->res_pool->funcs->populate_dml_pipes(dc, context, pipes, false);
		} else {
			// only call dcn20_validate_apply_pipe_split_flags if we found a supported config
			memset(split, 0, MAX_PIPES * sizeof(int));
			memset(merge, 0, MAX_PIPES * sizeof(bool));
			*vlevel = dcn20_validate_apply_pipe_split_flags(dc, context, *vlevel, split, merge);

			// If found a supported SubVP config, phantom pipes were added to the context.
			// Program timing for the phantom pipes.
			dc->hwss.apply_ctx_to_hw(dc, context);
		}
	}
}

static bool dcn32_internal_validate_bw(
		struct dc *dc,
		struct dc_state *context,
		display_e2e_pipe_params_st *pipes,
		int *pipe_cnt_out,
		int *vlevel_out,
		bool fast_validate)
{
	bool out = false;
	bool repopulate_pipes = false;
	int split[MAX_PIPES] = { 0 };
	bool merge[MAX_PIPES] = { false };
	bool newly_split[MAX_PIPES] = { false };
	int pipe_cnt, i, pipe_idx, vlevel;
	struct vba_vars_st *vba = &context->bw_ctx.dml.vba;

	ASSERT(pipes);
	if (!pipes)
		return false;

	// For each full update, remove all existing phantom pipes first
	dc->res_pool->funcs->remove_phantom_pipes(dc, context);

	dc->res_pool->funcs->update_soc_for_wm_a(dc, context);

	pipe_cnt = dc->res_pool->funcs->populate_dml_pipes(dc, context, pipes, fast_validate);

	if (!pipe_cnt) {
		out = true;
		goto validate_out;
	}

	dml_log_pipe_params(&context->bw_ctx.dml, pipes, pipe_cnt);

	if (!fast_validate) {
		dcn32_full_validate_bw_helper(dc, context, pipes, &vlevel, split, merge, &pipe_cnt);
	}

	if (fast_validate || vlevel == context->bw_ctx.dml.soc.num_states ||
			vba->DRAMClockChangeSupport[vlevel][vba->maxMpcComb] == dm_dram_clock_change_unsupported) {
		/*
		 * If mode is unsupported or there's still no p-state support then
		 * fall back to favoring voltage.
		 *
		 * We don't actually support prefetch mode 2, so require that we
		 * at least support prefetch mode 1.
		 */
		context->bw_ctx.dml.soc.allow_for_pstate_or_stutter_in_vblank_final =
				dm_prefetch_support_stutter;

		vlevel = dml_get_voltage_level(&context->bw_ctx.dml, pipes, pipe_cnt);
		if (vlevel < context->bw_ctx.dml.soc.num_states) {
			memset(split, 0, MAX_PIPES * sizeof(int));
			memset(merge, 0, MAX_PIPES * sizeof(bool));
			vlevel = dcn20_validate_apply_pipe_split_flags(dc, context, vlevel, split, merge);
		}
	}

	dml_log_mode_support_params(&context->bw_ctx.dml);

	if (vlevel == context->bw_ctx.dml.soc.num_states)
		goto validate_fail;

	for (i = 0, pipe_idx = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];
		struct pipe_ctx *mpo_pipe = pipe->bottom_pipe;

		if (!pipe->stream)
			continue;

		/* We only support full screen mpo with ODM */
		if (vba->ODMCombineEnabled[vba->pipe_plane[pipe_idx]] != dm_odm_combine_mode_disabled
				&& pipe->plane_state && mpo_pipe
				&& memcmp(&mpo_pipe->plane_res.scl_data.recout,
						&pipe->plane_res.scl_data.recout,
						sizeof(struct rect)) != 0) {
			ASSERT(mpo_pipe->plane_state != pipe->plane_state);
			goto validate_fail;
		}
		pipe_idx++;
	}

	/* merge pipes if necessary */
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];

		/*skip pipes that don't need merging*/
		if (!merge[i])
			continue;

		/* if ODM merge we ignore mpc tree, mpo pipes will have their own flags */
		if (pipe->prev_odm_pipe) {
			/*split off odm pipe*/
			pipe->prev_odm_pipe->next_odm_pipe = pipe->next_odm_pipe;
			if (pipe->next_odm_pipe)
				pipe->next_odm_pipe->prev_odm_pipe = pipe->prev_odm_pipe;

			pipe->bottom_pipe = NULL;
			pipe->next_odm_pipe = NULL;
			pipe->plane_state = NULL;
			pipe->stream = NULL;
			pipe->top_pipe = NULL;
			pipe->prev_odm_pipe = NULL;
			if (pipe->stream_res.dsc)
				dcn20_release_dsc(&context->res_ctx, dc->res_pool, &pipe->stream_res.dsc);
			memset(&pipe->plane_res, 0, sizeof(pipe->plane_res));
			memset(&pipe->stream_res, 0, sizeof(pipe->stream_res));
			repopulate_pipes = true;
		} else if (pipe->top_pipe && pipe->top_pipe->plane_state == pipe->plane_state) {
			struct pipe_ctx *top_pipe = pipe->top_pipe;
			struct pipe_ctx *bottom_pipe = pipe->bottom_pipe;

			top_pipe->bottom_pipe = bottom_pipe;
			if (bottom_pipe)
				bottom_pipe->top_pipe = top_pipe;

			pipe->top_pipe = NULL;
			pipe->bottom_pipe = NULL;
			pipe->plane_state = NULL;
			pipe->stream = NULL;
			memset(&pipe->plane_res, 0, sizeof(pipe->plane_res));
			memset(&pipe->stream_res, 0, sizeof(pipe->stream_res));
			repopulate_pipes = true;
		} else
			ASSERT(0); /* Should never try to merge master pipe */

	}

	for (i = 0, pipe_idx = -1; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];
		struct pipe_ctx *old_pipe = &dc->current_state->res_ctx.pipe_ctx[i];
		struct pipe_ctx *hsplit_pipe = NULL;
		bool odm;
		int old_index = -1;

		if (!pipe->stream || newly_split[i])
			continue;

		pipe_idx++;
		odm = vba->ODMCombineEnabled[vba->pipe_plane[pipe_idx]] != dm_odm_combine_mode_disabled;

		if (!pipe->plane_state && !odm)
			continue;

		if (split[i]) {
			if (odm) {
				if (split[i] == 4 && old_pipe->next_odm_pipe && old_pipe->next_odm_pipe->next_odm_pipe)
					old_index = old_pipe->next_odm_pipe->next_odm_pipe->pipe_idx;
				else if (old_pipe->next_odm_pipe)
					old_index = old_pipe->next_odm_pipe->pipe_idx;
			} else {
				if (split[i] == 4 && old_pipe->bottom_pipe && old_pipe->bottom_pipe->bottom_pipe &&
						old_pipe->bottom_pipe->bottom_pipe->plane_state == old_pipe->plane_state)
					old_index = old_pipe->bottom_pipe->bottom_pipe->pipe_idx;
				else if (old_pipe->bottom_pipe &&
						old_pipe->bottom_pipe->plane_state == old_pipe->plane_state)
					old_index = old_pipe->bottom_pipe->pipe_idx;
			}
			hsplit_pipe = dcn32_find_split_pipe(dc, context, old_index);
			ASSERT(hsplit_pipe);
			if (!hsplit_pipe)
				goto validate_fail;

			if (!dcn32_split_stream_for_mpc_or_odm(
					dc, &context->res_ctx,
					pipe, hsplit_pipe, odm))
				goto validate_fail;

			newly_split[hsplit_pipe->pipe_idx] = true;
			repopulate_pipes = true;
		}
		if (split[i] == 4) {
			struct pipe_ctx *pipe_4to1;

			if (odm && old_pipe->next_odm_pipe)
				old_index = old_pipe->next_odm_pipe->pipe_idx;
			else if (!odm && old_pipe->bottom_pipe &&
						old_pipe->bottom_pipe->plane_state == old_pipe->plane_state)
				old_index = old_pipe->bottom_pipe->pipe_idx;
			else
				old_index = -1;
			pipe_4to1 = dcn32_find_split_pipe(dc, context, old_index);
			ASSERT(pipe_4to1);
			if (!pipe_4to1)
				goto validate_fail;
			if (!dcn32_split_stream_for_mpc_or_odm(
					dc, &context->res_ctx,
					pipe, pipe_4to1, odm))
				goto validate_fail;
			newly_split[pipe_4to1->pipe_idx] = true;

			if (odm && old_pipe->next_odm_pipe && old_pipe->next_odm_pipe->next_odm_pipe
					&& old_pipe->next_odm_pipe->next_odm_pipe->next_odm_pipe)
				old_index = old_pipe->next_odm_pipe->next_odm_pipe->next_odm_pipe->pipe_idx;
			else if (!odm && old_pipe->bottom_pipe && old_pipe->bottom_pipe->bottom_pipe &&
					old_pipe->bottom_pipe->bottom_pipe->bottom_pipe &&
					old_pipe->bottom_pipe->bottom_pipe->bottom_pipe->plane_state == old_pipe->plane_state)
				old_index = old_pipe->bottom_pipe->bottom_pipe->bottom_pipe->pipe_idx;
			else
				old_index = -1;
			pipe_4to1 = dcn32_find_split_pipe(dc, context, old_index);
			ASSERT(pipe_4to1);
			if (!pipe_4to1)
				goto validate_fail;
			if (!dcn32_split_stream_for_mpc_or_odm(
					dc, &context->res_ctx,
					hsplit_pipe, pipe_4to1, odm))
				goto validate_fail;
			newly_split[pipe_4to1->pipe_idx] = true;
		}
		if (odm)
			dcn20_build_mapped_resource(dc, context, pipe->stream);
	}

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];

		if (pipe->plane_state) {
			if (!resource_build_scaling_params(pipe))
				goto validate_fail;
		}
	}

	/* Actual dsc count per stream dsc validation*/
	if (!dcn20_validate_dsc(dc, context)) {
		vba->ValidationStatus[vba->soc.num_states] = DML_FAIL_DSC_VALIDATION_FAILURE;
		goto validate_fail;
	}

	if (repopulate_pipes)
		pipe_cnt = dc->res_pool->funcs->populate_dml_pipes(dc, context, pipes, fast_validate);
	*vlevel_out = vlevel;
	*pipe_cnt_out = pipe_cnt;

	out = true;
	goto validate_out;

validate_fail:
	out = false;

validate_out:
	return out;
}

bool dcn32_validate_bandwidth(struct dc *dc,
		struct dc_state *context,
		bool fast_validate)
{
	bool out = false;

	BW_VAL_TRACE_SETUP();

	int vlevel = 0;
	int pipe_cnt = 0;
	display_e2e_pipe_params_st *pipes = kzalloc(dc->res_pool->pipe_count * sizeof(display_e2e_pipe_params_st), GFP_KERNEL);
	DC_LOGGER_INIT(dc->ctx->logger);

	BW_VAL_TRACE_COUNT();

    DC_FP_START();
	out = dcn32_internal_validate_bw(dc, context, pipes, &pipe_cnt, &vlevel, fast_validate);
    DC_FP_END();

	if (pipe_cnt == 0)
		goto validate_out;

	if (!out)
		goto validate_fail;

	BW_VAL_TRACE_END_VOLTAGE_LEVEL();

	if (fast_validate) {
		BW_VAL_TRACE_SKIP(fast);
		goto validate_out;
	}

	dc->res_pool->funcs->calculate_wm_and_dlg(dc, context, pipes, pipe_cnt, vlevel);

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


static bool is_dual_plane(enum surface_pixel_format format)
{
	return format >= SURFACE_PIXEL_FORMAT_VIDEO_BEGIN || format == SURFACE_PIXEL_FORMAT_GRPH_RGBE_ALPHA;
}

int dcn32_populate_dml_pipes_from_context(
	struct dc *dc, struct dc_state *context,
	display_e2e_pipe_params_st *pipes,
	bool fast_validate)
{
	int i, pipe_cnt;
	struct resource_context *res_ctx = &context->res_ctx;
	struct pipe_ctx *pipe;

	dcn20_populate_dml_pipes_from_context(dc, context, pipes, fast_validate);

	for (i = 0, pipe_cnt = 0; i < dc->res_pool->pipe_count; i++) {
		struct dc_crtc_timing *timing;

		if (!res_ctx->pipe_ctx[i].stream)
			continue;
		pipe = &res_ctx->pipe_ctx[i];
		timing = &pipe->stream->timing;

		pipes[pipe_cnt].pipe.src.gpuvm = true;
		pipes[pipe_cnt].pipe.src.dcc_fraction_of_zs_req_luma = 0;
		pipes[pipe_cnt].pipe.src.dcc_fraction_of_zs_req_chroma = 0;
		pipes[pipe_cnt].pipe.dest.vfront_porch = timing->v_front_porch;
		pipes[pipe_cnt].pipe.src.gpuvm_min_page_size_kbytes = 256; // according to spreadsheet
		pipes[pipe_cnt].pipe.src.unbounded_req_mode = false;
		pipes[pipe_cnt].pipe.scale_ratio_depth.lb_depth = dm_lb_19;

		switch (pipe->stream->mall_stream_config.type) {
		case SUBVP_MAIN:
			pipes[pipe_cnt].pipe.src.use_mall_for_pstate_change = dm_use_mall_pstate_change_sub_viewport;
			break;
		case SUBVP_PHANTOM:
			pipes[pipe_cnt].pipe.src.use_mall_for_pstate_change = dm_use_mall_pstate_change_phantom_pipe;
			pipes[pipe_cnt].pipe.src.use_mall_for_static_screen = dm_use_mall_static_screen_enable;
			break;
		case SUBVP_NONE:
			pipes[pipe_cnt].pipe.src.use_mall_for_pstate_change = dm_use_mall_pstate_change_disable;
			pipes[pipe_cnt].pipe.src.use_mall_for_static_screen = dm_use_mall_static_screen_disable;
			break;
		default:
			break;
		}

		pipes[pipe_cnt].dout.dsc_input_bpc = 0;
		if (pipes[pipe_cnt].dout.dsc_enable) {
			switch (timing->display_color_depth) {
			case COLOR_DEPTH_888:
				pipes[pipe_cnt].dout.dsc_input_bpc = 8;
				break;
			case COLOR_DEPTH_101010:
				pipes[pipe_cnt].dout.dsc_input_bpc = 10;
				break;
			case COLOR_DEPTH_121212:
				pipes[pipe_cnt].dout.dsc_input_bpc = 12;
				break;
			default:
				ASSERT(0);
				break;
			}
		}
		pipe_cnt++;
	}

	switch (pipe_cnt) {
	case 1:
		context->bw_ctx.dml.ip.det_buffer_size_kbytes = DCN3_2_MAX_DET_SIZE;
		if (pipe->plane_state && !dc->debug.disable_z9_mpc) {
			if (!is_dual_plane(pipe->plane_state->format)) {
				context->bw_ctx.dml.ip.det_buffer_size_kbytes = DCN3_2_DEFAULT_DET_SIZE;
				pipes[0].pipe.src.unbounded_req_mode = true;
				if (pipe->plane_state->src_rect.width >= 5120 &&
					pipe->plane_state->src_rect.height >= 2880)
					context->bw_ctx.dml.ip.det_buffer_size_kbytes = 320; // 5K or higher
			}
		}
		break;
	case 2:
		context->bw_ctx.dml.ip.det_buffer_size_kbytes = DCN3_2_MAX_DET_SIZE / 2; // 576 KB (9 segments)
		break;
	case 3:
		context->bw_ctx.dml.ip.det_buffer_size_kbytes = DCN3_2_MAX_DET_SIZE / 3; // 384 KB (6 segments)
		break;
	case 4:
	default:
		context->bw_ctx.dml.ip.det_buffer_size_kbytes = DCN3_2_DEFAULT_DET_SIZE; // 256 KB (4 segments)
		break;
	}

	return pipe_cnt;
}

void dcn32_calculate_wm_and_dlg_fp(
		struct dc *dc, struct dc_state *context,
		display_e2e_pipe_params_st *pipes,
		int pipe_cnt,
		int vlevel)
{
	int i, pipe_idx, vlevel_temp = 0;

	double dcfclk = dcn3_2_soc.clock_limits[0].dcfclk_mhz;
	double dcfclk_from_validation = context->bw_ctx.dml.vba.DCFCLKState[vlevel][context->bw_ctx.dml.vba.maxMpcComb];
	unsigned int min_dram_speed_mts = context->bw_ctx.dml.vba.DRAMSpeed;
	bool pstate_en = context->bw_ctx.dml.vba.DRAMClockChangeSupport[vlevel][context->bw_ctx.dml.vba.maxMpcComb] !=
			dm_dram_clock_change_unsupported;

	/* Set B:
	 * For Set B calculations use clocks from clock_limits[2] when available i.e. when SMU is present,
	 * otherwise use arbitrary low value from spreadsheet for DCFCLK as lower is safer for watermark
	 * calculations to cover bootup clocks.
	 * DCFCLK: soc.clock_limits[2] when available
	 * UCLK: soc.clock_limits[2] when available
	 */
	if (dcn3_2_soc.num_states > 2) {
		vlevel_temp = 2;
		dcfclk = dcn3_2_soc.clock_limits[2].dcfclk_mhz;
	} else
		dcfclk = 615; //DCFCLK Vmin_lv

	pipes[0].clks_cfg.voltage = vlevel_temp;
	pipes[0].clks_cfg.dcfclk_mhz = dcfclk;
	pipes[0].clks_cfg.socclk_mhz = context->bw_ctx.dml.soc.clock_limits[vlevel_temp].socclk_mhz;

	if (dc->clk_mgr->bw_params->wm_table.nv_entries[WM_B].valid) {
		context->bw_ctx.dml.soc.dram_clock_change_latency_us = dc->clk_mgr->bw_params->wm_table.nv_entries[WM_B].dml_input.pstate_latency_us;
		context->bw_ctx.dml.soc.fclk_change_latency_us = dc->clk_mgr->bw_params->wm_table.nv_entries[WM_B].dml_input.fclk_change_latency_us;
		context->bw_ctx.dml.soc.sr_enter_plus_exit_time_us = dc->clk_mgr->bw_params->wm_table.nv_entries[WM_B].dml_input.sr_enter_plus_exit_time_us;
		context->bw_ctx.dml.soc.sr_exit_time_us = dc->clk_mgr->bw_params->wm_table.nv_entries[WM_B].dml_input.sr_exit_time_us;
	}
	context->bw_ctx.bw.dcn.watermarks.b.urgent_ns = get_wm_urgent(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.b.cstate_pstate.cstate_enter_plus_exit_ns = get_wm_stutter_enter_exit(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.b.cstate_pstate.cstate_exit_ns = get_wm_stutter_exit(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.b.cstate_pstate.pstate_change_ns = get_wm_dram_clock_change(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.b.pte_meta_urgent_ns = get_wm_memory_trip(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.b.frac_urg_bw_nom = get_fraction_of_urgent_bandwidth(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.b.frac_urg_bw_flip = get_fraction_of_urgent_bandwidth_imm_flip(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.b.urgent_latency_ns = get_urgent_latency(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.b.cstate_pstate.fclk_pstate_change_ns = get_fclk_watermark(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.b.usr_retraining_ns = get_usr_retraining_watermark(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;

	/* Set D:
	 * All clocks min.
	 * DCFCLK: Min, as reported by PM FW when available
	 * UCLK  : Min, as reported by PM FW when available
	 * sr_enter_exit/sr_exit should be lower than used for DRAM (TBD after bringup or later, use as decided in Clk Mgr)
	 */

	if (dcn3_2_soc.num_states > 2) {
		vlevel_temp = 0;
		dcfclk = dc->clk_mgr->bw_params->clk_table.entries[0].dcfclk_mhz;
	} else
		dcfclk = 615; //DCFCLK Vmin_lv

	pipes[0].clks_cfg.voltage = vlevel_temp;
	pipes[0].clks_cfg.dcfclk_mhz = dcfclk;
	pipes[0].clks_cfg.socclk_mhz = context->bw_ctx.dml.soc.clock_limits[vlevel_temp].socclk_mhz;

	if (dc->clk_mgr->bw_params->wm_table.nv_entries[WM_D].valid) {
		context->bw_ctx.dml.soc.dram_clock_change_latency_us = dc->clk_mgr->bw_params->wm_table.nv_entries[WM_D].dml_input.pstate_latency_us;
		context->bw_ctx.dml.soc.fclk_change_latency_us = dc->clk_mgr->bw_params->wm_table.nv_entries[WM_D].dml_input.fclk_change_latency_us;
		context->bw_ctx.dml.soc.sr_enter_plus_exit_time_us = dc->clk_mgr->bw_params->wm_table.nv_entries[WM_D].dml_input.sr_enter_plus_exit_time_us;
		context->bw_ctx.dml.soc.sr_exit_time_us = dc->clk_mgr->bw_params->wm_table.nv_entries[WM_D].dml_input.sr_exit_time_us;
	}
	context->bw_ctx.bw.dcn.watermarks.d.urgent_ns = get_wm_urgent(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.d.cstate_pstate.cstate_enter_plus_exit_ns = get_wm_stutter_enter_exit(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.d.cstate_pstate.cstate_exit_ns = get_wm_stutter_exit(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.d.cstate_pstate.pstate_change_ns = get_wm_dram_clock_change(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.d.pte_meta_urgent_ns = get_wm_memory_trip(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.d.frac_urg_bw_nom = get_fraction_of_urgent_bandwidth(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.d.frac_urg_bw_flip = get_fraction_of_urgent_bandwidth_imm_flip(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.d.urgent_latency_ns = get_urgent_latency(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.d.cstate_pstate.fclk_pstate_change_ns = get_fclk_watermark(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.d.usr_retraining_ns = get_usr_retraining_watermark(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;

	/* Set C, for Dummy P-State:
	 * All clocks min.
	 * DCFCLK: Min, as reported by PM FW, when available
	 * UCLK  : Min,  as reported by PM FW, when available
	 * pstate latency as per UCLK state dummy pstate latency
	 */

	if (dc->clk_mgr->bw_params->wm_table.nv_entries[WM_C].valid) {
		unsigned int min_dram_speed_mts_margin = 160;

		if ((!pstate_en))
			min_dram_speed_mts = dc->clk_mgr->bw_params->clk_table.entries[dc->clk_mgr->bw_params->clk_table.num_entries - 1].memclk_mhz * 16;

		/* find largest table entry that is lower than dram speed, but lower than DPM0 still uses DPM0 */
		for (i = 3; i > 0; i--)
			if (min_dram_speed_mts + min_dram_speed_mts_margin > dc->clk_mgr->bw_params->dummy_pstate_table[i].dram_speed_mts)
				break;

		context->bw_ctx.dml.soc.dram_clock_change_latency_us = dc->clk_mgr->bw_params->dummy_pstate_table[i].dummy_pstate_latency_us;
		context->bw_ctx.dml.soc.dummy_pstate_latency_us = dc->clk_mgr->bw_params->dummy_pstate_table[i].dummy_pstate_latency_us;
		context->bw_ctx.dml.soc.fclk_change_latency_us = dc->clk_mgr->bw_params->wm_table.nv_entries[WM_C].dml_input.fclk_change_latency_us;
		context->bw_ctx.dml.soc.sr_enter_plus_exit_time_us = dc->clk_mgr->bw_params->wm_table.nv_entries[WM_C].dml_input.sr_enter_plus_exit_time_us;
		context->bw_ctx.dml.soc.sr_exit_time_us = dc->clk_mgr->bw_params->wm_table.nv_entries[WM_C].dml_input.sr_exit_time_us;
	}
	context->bw_ctx.bw.dcn.watermarks.c.urgent_ns = get_wm_urgent(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.c.cstate_pstate.cstate_enter_plus_exit_ns = get_wm_stutter_enter_exit(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.c.cstate_pstate.cstate_exit_ns = get_wm_stutter_exit(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.c.cstate_pstate.pstate_change_ns = get_wm_dram_clock_change(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.c.pte_meta_urgent_ns = get_wm_memory_trip(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.c.frac_urg_bw_nom = get_fraction_of_urgent_bandwidth(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.c.frac_urg_bw_flip = get_fraction_of_urgent_bandwidth_imm_flip(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.c.urgent_latency_ns = get_urgent_latency(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.c.cstate_pstate.fclk_pstate_change_ns = get_fclk_watermark(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.c.usr_retraining_ns = get_usr_retraining_watermark(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;

	if ((!pstate_en) && (dc->clk_mgr->bw_params->wm_table.nv_entries[WM_C].valid)) {
		/* The only difference between A and C is p-state latency, if p-state is not supported
		 * with full p-state latency we want to calculate DLG based on dummy p-state latency,
		 * Set A p-state watermark set to 0 on DCN32, when p-state unsupported, for now keep as DCN32.
		 */
		context->bw_ctx.bw.dcn.watermarks.a = context->bw_ctx.bw.dcn.watermarks.c;
		context->bw_ctx.bw.dcn.watermarks.a.cstate_pstate.pstate_change_ns = 0;
	} else {
		/* Set A:
		 * All clocks min.
		 * DCFCLK: Min, as reported by PM FW, when available
		 * UCLK: Min, as reported by PM FW, when available
		 */
		dc->res_pool->funcs->update_soc_for_wm_a(dc, context);
		context->bw_ctx.bw.dcn.watermarks.a.urgent_ns = get_wm_urgent(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
		context->bw_ctx.bw.dcn.watermarks.a.cstate_pstate.cstate_enter_plus_exit_ns = get_wm_stutter_enter_exit(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
		context->bw_ctx.bw.dcn.watermarks.a.cstate_pstate.cstate_exit_ns = get_wm_stutter_exit(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
		context->bw_ctx.bw.dcn.watermarks.a.cstate_pstate.pstate_change_ns = get_wm_dram_clock_change(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
		context->bw_ctx.bw.dcn.watermarks.a.pte_meta_urgent_ns = get_wm_memory_trip(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
		context->bw_ctx.bw.dcn.watermarks.a.frac_urg_bw_nom = get_fraction_of_urgent_bandwidth(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
		context->bw_ctx.bw.dcn.watermarks.a.frac_urg_bw_flip = get_fraction_of_urgent_bandwidth_imm_flip(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
		context->bw_ctx.bw.dcn.watermarks.a.urgent_latency_ns = get_urgent_latency(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
		context->bw_ctx.bw.dcn.watermarks.a.cstate_pstate.fclk_pstate_change_ns = get_fclk_watermark(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
		context->bw_ctx.bw.dcn.watermarks.a.usr_retraining_ns = get_usr_retraining_watermark(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	}

	pipes[0].clks_cfg.voltage = vlevel;
	pipes[0].clks_cfg.dcfclk_mhz = dcfclk_from_validation;
	pipes[0].clks_cfg.socclk_mhz = context->bw_ctx.dml.soc.clock_limits[vlevel].socclk_mhz;

	for (i = 0, pipe_idx = 0; i < dc->res_pool->pipe_count; i++) {
		if (!context->res_ctx.pipe_ctx[i].stream)
			continue;

		pipes[pipe_idx].clks_cfg.dispclk_mhz = get_dispclk_calculated(&context->bw_ctx.dml, pipes, pipe_cnt);
		pipes[pipe_idx].clks_cfg.dppclk_mhz = get_dppclk_calculated(&context->bw_ctx.dml, pipes, pipe_cnt, pipe_idx);

		if (dc->config.forced_clocks) {
			pipes[pipe_idx].clks_cfg.dispclk_mhz = context->bw_ctx.dml.soc.clock_limits[0].dispclk_mhz;
			pipes[pipe_idx].clks_cfg.dppclk_mhz = context->bw_ctx.dml.soc.clock_limits[0].dppclk_mhz;
		}
		if (dc->debug.min_disp_clk_khz > pipes[pipe_idx].clks_cfg.dispclk_mhz * 1000)
			pipes[pipe_idx].clks_cfg.dispclk_mhz = dc->debug.min_disp_clk_khz / 1000.0;
		if (dc->debug.min_dpp_clk_khz > pipes[pipe_idx].clks_cfg.dppclk_mhz * 1000)
			pipes[pipe_idx].clks_cfg.dppclk_mhz = dc->debug.min_dpp_clk_khz / 1000.0;

		pipe_idx++;
	}

	context->perf_params.stutter_period_us = context->bw_ctx.dml.vba.StutterPeriod;

	dcn32_calculate_dlg_params(dc, context, pipes, pipe_cnt, vlevel);

	if (!pstate_en)
		/* Restore full p-state latency */
		context->bw_ctx.dml.soc.dram_clock_change_latency_us =
				dc->clk_mgr->bw_params->wm_table.nv_entries[WM_A].dml_input.pstate_latency_us;
}

static struct dc_cap_funcs cap_funcs = {
	.get_dcc_compression_cap = dcn20_get_dcc_compression_cap
};


static void dcn32_get_optimal_dcfclk_fclk_for_uclk(unsigned int uclk_mts,
		unsigned int *optimal_dcfclk,
		unsigned int *optimal_fclk)
{
	double bw_from_dram, bw_from_dram1, bw_from_dram2;

	bw_from_dram1 = uclk_mts * dcn3_2_soc.num_chans *
		dcn3_2_soc.dram_channel_width_bytes * (dcn3_2_soc.max_avg_dram_bw_use_normal_percent / 100);
	bw_from_dram2 = uclk_mts * dcn3_2_soc.num_chans *
		dcn3_2_soc.dram_channel_width_bytes * (dcn3_2_soc.max_avg_sdp_bw_use_normal_percent / 100);

	bw_from_dram = (bw_from_dram1 < bw_from_dram2) ? bw_from_dram1 : bw_from_dram2;

	if (optimal_fclk)
		*optimal_fclk = bw_from_dram /
		(dcn3_2_soc.fabric_datapath_to_dcn_data_return_bytes * (dcn3_2_soc.max_avg_sdp_bw_use_normal_percent / 100));

	if (optimal_dcfclk)
		*optimal_dcfclk =  bw_from_dram /
		(dcn3_2_soc.return_bus_width_bytes * (dcn3_2_soc.max_avg_sdp_bw_use_normal_percent / 100));
}

void dcn32_calculate_wm_and_dlg(
		struct dc *dc, struct dc_state *context,
		display_e2e_pipe_params_st *pipes,
		int pipe_cnt,
		int vlevel)
{
    DC_FP_START();
    dcn32_calculate_wm_and_dlg_fp(
		dc, context,
		pipes,
		pipe_cnt,
		vlevel);
    DC_FP_END();
}

static bool is_dtbclk_required(struct dc *dc, struct dc_state *context)
{
	int i;

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		if (!context->res_ctx.pipe_ctx[i].stream)
			continue;
		if (is_dp_128b_132b_signal(&context->res_ctx.pipe_ctx[i]))
			return true;
	}
	return false;
}

void dcn32_calculate_dlg_params(struct dc *dc, struct dc_state *context, display_e2e_pipe_params_st *pipes,
		int pipe_cnt, int vlevel)
{
	int i, pipe_idx;
	bool usr_retraining_support = false;

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

	/*
	 * TODO: needs FAMS
	 * Pstate change might not be supported by hardware, but it might be
	 * possible with firmware driven vertical blank stretching.
	 */
	// context->bw_ctx.bw.dcn.clk.p_state_change_support |= context->bw_ctx.bw.dcn.clk.fw_based_mclk_switching;
	context->bw_ctx.bw.dcn.clk.dppclk_khz = 0;
	context->bw_ctx.bw.dcn.clk.dtbclk_en = is_dtbclk_required(dc, context);
	if (context->bw_ctx.dml.vba.FCLKChangeSupport[vlevel][context->bw_ctx.dml.vba.maxMpcComb] == dm_fclock_change_unsupported)
		context->bw_ctx.bw.dcn.clk.fclk_p_state_change_support = false;
	else
		context->bw_ctx.bw.dcn.clk.fclk_p_state_change_support = true;

	usr_retraining_support = context->bw_ctx.dml.vba.USRRetrainingSupport[vlevel][context->bw_ctx.dml.vba.maxMpcComb];
	ASSERT(usr_retraining_support);

	if (context->bw_ctx.bw.dcn.clk.dispclk_khz < dc->debug.min_disp_clk_khz)
		context->bw_ctx.bw.dcn.clk.dispclk_khz = dc->debug.min_disp_clk_khz;

	for (i = 0, pipe_idx = 0; i < dc->res_pool->pipe_count; i++) {
		if (!context->res_ctx.pipe_ctx[i].stream)
			continue;
		pipes[pipe_idx].pipe.dest.vstartup_start = get_vstartup(&context->bw_ctx.dml, pipes, pipe_cnt,
				pipe_idx);
		pipes[pipe_idx].pipe.dest.vupdate_offset = get_vupdate_offset(&context->bw_ctx.dml, pipes, pipe_cnt,
				pipe_idx);
		pipes[pipe_idx].pipe.dest.vupdate_width = get_vupdate_width(&context->bw_ctx.dml, pipes, pipe_cnt,
				pipe_idx);
		pipes[pipe_idx].pipe.dest.vready_offset = get_vready_offset(&context->bw_ctx.dml, pipes, pipe_cnt,
				pipe_idx);
		if (context->res_ctx.pipe_ctx[i].stream->mall_stream_config.type == SUBVP_PHANTOM) {
			// Phantom pipe requires that DET_SIZE = 0 and no unbounded requests
			context->res_ctx.pipe_ctx[i].det_buffer_size_kb = 0;
			context->res_ctx.pipe_ctx[i].unbounded_req = false;
		} else {
			context->res_ctx.pipe_ctx[i].det_buffer_size_kb =
					context->bw_ctx.dml.ip.det_buffer_size_kbytes;
			context->res_ctx.pipe_ctx[i].unbounded_req = pipes[pipe_idx].pipe.src.unbounded_req_mode;
		}
		if (context->bw_ctx.bw.dcn.clk.dppclk_khz < pipes[pipe_idx].clks_cfg.dppclk_mhz * 1000)
			context->bw_ctx.bw.dcn.clk.dppclk_khz = pipes[pipe_idx].clks_cfg.dppclk_mhz * 1000;
		context->res_ctx.pipe_ctx[i].plane_res.bw.dppclk_khz = pipes[pipe_idx].clks_cfg.dppclk_mhz * 1000;
		context->res_ctx.pipe_ctx[i].pipe_dlg_param = pipes[pipe_idx].pipe.dest;
		pipe_idx++;
	}
	/*save a original dppclock copy*/
	context->bw_ctx.bw.dcn.clk.bw_dppclk_khz = context->bw_ctx.bw.dcn.clk.dppclk_khz;
	context->bw_ctx.bw.dcn.clk.bw_dispclk_khz = context->bw_ctx.bw.dcn.clk.dispclk_khz;
	context->bw_ctx.bw.dcn.clk.max_supported_dppclk_khz = context->bw_ctx.dml.soc.clock_limits[vlevel].dppclk_mhz
			* 1000;
	context->bw_ctx.bw.dcn.clk.max_supported_dispclk_khz = context->bw_ctx.dml.soc.clock_limits[vlevel].dispclk_mhz
			* 1000;

	context->bw_ctx.bw.dcn.compbuf_size_kb = context->bw_ctx.dml.ip.config_return_buffer_size_in_kbytes
			- context->bw_ctx.dml.ip.det_buffer_size_kbytes * pipe_idx;

	for (i = 0, pipe_idx = 0; i < dc->res_pool->pipe_count; i++) {

		if (!context->res_ctx.pipe_ctx[i].stream)
			continue;

		context->bw_ctx.dml.funcs.rq_dlg_get_dlg_reg_v2(&context->bw_ctx.dml,
				&context->res_ctx.pipe_ctx[i].dlg_regs, &context->res_ctx.pipe_ctx[i].ttu_regs, pipes,
				pipe_cnt, pipe_idx);

		context->bw_ctx.dml.funcs.rq_dlg_get_rq_reg_v2(&context->res_ctx.pipe_ctx[i].rq_regs,
				&context->bw_ctx.dml, pipes, pipe_cnt, pipe_idx);

		pipe_idx++;
	}
}

/* dcn32_update_bw_bounding_box
 * This would override some dcn3_2 ip_or_soc initial parameters hardcoded from spreadsheet
 * with actual values as per dGPU SKU:
 * -with passed few options from dc->config
 * -with dentist_vco_frequency from Clk Mgr (currently hardcoded, but might need to get it from PM FW)
 * -with passed latency values (passed in ns units) in dc-> bb override for debugging purposes
 * -with passed latencies from VBIOS (in 100_ns units) if available for certain dGPU SKU
 * -with number of DRAM channels from VBIOS (which differ for certain dGPU SKU of the same ASIC)
 * -clocks levels with passed clk_table entries from Clk Mgr as reported by PM FW for different
 *  clocks (which might differ for certain dGPU SKU of the same ASIC)
 */
static void dcn32_update_bw_bounding_box(struct dc *dc, struct clk_bw_params *bw_params)
{
	if (!IS_FPGA_MAXIMUS_DC(dc->ctx->dce_environment)) {

		/* Overrides from dc->config options */
		dcn3_2_ip.clamp_min_dcfclk = dc->config.clamp_min_dcfclk;

		/* Override from passed dc->bb_overrides if available*/
		if ((int)(dcn3_2_soc.sr_exit_time_us * 1000) != dc->bb_overrides.sr_exit_time_ns
				&& dc->bb_overrides.sr_exit_time_ns) {
			dcn3_2_soc.sr_exit_time_us = dc->bb_overrides.sr_exit_time_ns / 1000.0;
		}

		if ((int)(dcn3_2_soc.sr_enter_plus_exit_time_us * 1000)
				!= dc->bb_overrides.sr_enter_plus_exit_time_ns
				&& dc->bb_overrides.sr_enter_plus_exit_time_ns) {
			dcn3_2_soc.sr_enter_plus_exit_time_us =
				dc->bb_overrides.sr_enter_plus_exit_time_ns / 1000.0;
		}

		if ((int)(dcn3_2_soc.urgent_latency_us * 1000) != dc->bb_overrides.urgent_latency_ns
			&& dc->bb_overrides.urgent_latency_ns) {
			dcn3_2_soc.urgent_latency_us = dc->bb_overrides.urgent_latency_ns / 1000.0;
		}

		if ((int)(dcn3_2_soc.dram_clock_change_latency_us * 1000)
				!= dc->bb_overrides.dram_clock_change_latency_ns
				&& dc->bb_overrides.dram_clock_change_latency_ns) {
			dcn3_2_soc.dram_clock_change_latency_us =
				dc->bb_overrides.dram_clock_change_latency_ns / 1000.0;
		}

		if ((int)(dcn3_2_soc.dummy_pstate_latency_us * 1000)
				!= dc->bb_overrides.dummy_clock_change_latency_ns
				&& dc->bb_overrides.dummy_clock_change_latency_ns) {
			dcn3_2_soc.dummy_pstate_latency_us =
				dc->bb_overrides.dummy_clock_change_latency_ns / 1000.0;
		}

		/* Override from VBIOS if VBIOS bb_info available */
		if (dc->ctx->dc_bios->funcs->get_soc_bb_info) {
			struct bp_soc_bb_info bb_info = {0};

			if (dc->ctx->dc_bios->funcs->get_soc_bb_info(dc->ctx->dc_bios, &bb_info) == BP_RESULT_OK) {
				if (bb_info.dram_clock_change_latency_100ns > 0)
					dcn3_2_soc.dram_clock_change_latency_us = bb_info.dram_clock_change_latency_100ns * 10;

			if (bb_info.dram_sr_enter_exit_latency_100ns > 0)
				dcn3_2_soc.sr_enter_plus_exit_time_us = bb_info.dram_sr_enter_exit_latency_100ns * 10;

			if (bb_info.dram_sr_exit_latency_100ns > 0)
				dcn3_2_soc.sr_exit_time_us = bb_info.dram_sr_exit_latency_100ns * 10;
			}
		}

		/* Override from VBIOS for num_chan */
		if (dc->ctx->dc_bios->vram_info.num_chans)
			dcn3_2_soc.num_chans = dc->ctx->dc_bios->vram_info.num_chans;

		if (dc->ctx->dc_bios->vram_info.dram_channel_width_bytes)
			dcn3_2_soc.dram_channel_width_bytes = dc->ctx->dc_bios->vram_info.dram_channel_width_bytes;

	}

	/* Override dispclk_dppclk_vco_speed_mhz from Clk Mgr */
	dcn3_2_soc.dispclk_dppclk_vco_speed_mhz = dc->clk_mgr->dentist_vco_freq_khz / 1000.0;
	dc->dml.soc.dispclk_dppclk_vco_speed_mhz = dc->clk_mgr->dentist_vco_freq_khz / 1000.0;

	/* Overrides Clock levelsfrom CLK Mgr table entries as reported by PM FW */
	if ((!IS_FPGA_MAXIMUS_DC(dc->ctx->dce_environment)) && (bw_params->clk_table.entries[0].memclk_mhz)) {
		unsigned int i = 0, j = 0, num_states = 0;

		unsigned int dcfclk_mhz[DC__VOLTAGE_STATES] = {0};
		unsigned int dram_speed_mts[DC__VOLTAGE_STATES] = {0};
		unsigned int optimal_uclk_for_dcfclk_sta_targets[DC__VOLTAGE_STATES] = {0};
		unsigned int optimal_dcfclk_for_uclk[DC__VOLTAGE_STATES] = {0};

		unsigned int dcfclk_sta_targets[DC__VOLTAGE_STATES] = {615, 906, 1324, 1564};
		unsigned int num_dcfclk_sta_targets = 4, num_uclk_states = 0;
		unsigned int max_dcfclk_mhz = 0, max_dispclk_mhz = 0, max_dppclk_mhz = 0, max_phyclk_mhz = 0;

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
			max_dcfclk_mhz = dcn3_2_soc.clock_limits[0].dcfclk_mhz;
		if (!max_dispclk_mhz)
			max_dispclk_mhz = dcn3_2_soc.clock_limits[0].dispclk_mhz;
		if (!max_dppclk_mhz)
			max_dppclk_mhz = dcn3_2_soc.clock_limits[0].dppclk_mhz;
		if (!max_phyclk_mhz)
			max_phyclk_mhz = dcn3_2_soc.clock_limits[0].phyclk_mhz;

		if (max_dcfclk_mhz > dcfclk_sta_targets[num_dcfclk_sta_targets-1]) {
			// If max DCFCLK is greater than the max DCFCLK STA target, insert into the DCFCLK STA target array
			dcfclk_sta_targets[num_dcfclk_sta_targets] = max_dcfclk_mhz;
			num_dcfclk_sta_targets++;
		} else if (max_dcfclk_mhz < dcfclk_sta_targets[num_dcfclk_sta_targets-1]) {
			// If max DCFCLK is less than the max DCFCLK STA target, cap values and remove duplicates
			for (i = 0; i < num_dcfclk_sta_targets; i++) {
				if (dcfclk_sta_targets[i] > max_dcfclk_mhz) {
					dcfclk_sta_targets[i] = max_dcfclk_mhz;
					break;
				}
			}
			// Update size of array since we "removed" duplicates
			num_dcfclk_sta_targets = i + 1;
		}

		num_uclk_states = bw_params->clk_table.num_entries;

		// Calculate optimal dcfclk for each uclk
		for (i = 0; i < num_uclk_states; i++) {
			dcn32_get_optimal_dcfclk_fclk_for_uclk(bw_params->clk_table.entries[i].memclk_mhz * 16,
					&optimal_dcfclk_for_uclk[i], NULL);
			if (optimal_dcfclk_for_uclk[i] < bw_params->clk_table.entries[0].dcfclk_mhz) {
				optimal_dcfclk_for_uclk[i] = bw_params->clk_table.entries[0].dcfclk_mhz;
			}
		}

		// Calculate optimal uclk for each dcfclk sta target
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
		// create the final dcfclk and uclk table
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

		dcn3_2_soc.num_states = num_states;
		for (i = 0; i < dcn3_2_soc.num_states; i++) {
			dcn3_2_soc.clock_limits[i].state = i;
			dcn3_2_soc.clock_limits[i].dcfclk_mhz = dcfclk_mhz[i];
			dcn3_2_soc.clock_limits[i].fabricclk_mhz = dcfclk_mhz[i];

			/* Fill all states with max values of all these clocks */
			dcn3_2_soc.clock_limits[i].dispclk_mhz = max_dispclk_mhz;
			dcn3_2_soc.clock_limits[i].dppclk_mhz  = max_dppclk_mhz;
			dcn3_2_soc.clock_limits[i].phyclk_mhz  = max_phyclk_mhz;
			dcn3_2_soc.clock_limits[i].dscclk_mhz  = max_dispclk_mhz / 3;

			/* Populate from bw_params for DTBCLK, SOCCLK */
			if (!bw_params->clk_table.entries[i].dtbclk_mhz && i > 0)
				dcn3_2_soc.clock_limits[i].dtbclk_mhz  = dcn3_2_soc.clock_limits[i-1].dtbclk_mhz;
			else
				dcn3_2_soc.clock_limits[i].dtbclk_mhz  = bw_params->clk_table.entries[i].dtbclk_mhz;

			if (!bw_params->clk_table.entries[i].socclk_mhz && i > 0)
				dcn3_2_soc.clock_limits[i].socclk_mhz = dcn3_2_soc.clock_limits[i-1].socclk_mhz;
			else
				dcn3_2_soc.clock_limits[i].socclk_mhz = bw_params->clk_table.entries[i].socclk_mhz;

			if (!dram_speed_mts[i] && i > 0)
				dcn3_2_soc.clock_limits[i].dram_speed_mts = dcn3_2_soc.clock_limits[i-1].dram_speed_mts;
			else
				dcn3_2_soc.clock_limits[i].dram_speed_mts = dram_speed_mts[i];

			/* These clocks cannot come from bw_params, always fill from dcn3_2_soc[0] */
			/* PHYCLK_D18, PHYCLK_D32 */
			dcn3_2_soc.clock_limits[i].phyclk_d18_mhz = dcn3_2_soc.clock_limits[0].phyclk_d18_mhz;
			dcn3_2_soc.clock_limits[i].phyclk_d32_mhz = dcn3_2_soc.clock_limits[0].phyclk_d32_mhz;
		}

		/* Re-init DML with updated bb */
		dml_init_instance(&dc->dml, &dcn3_2_soc, &dcn3_2_ip, DML_PROJECT_DCN32);
		if (dc->current_state)
			dml_init_instance(&dc->current_state->bw_ctx.dml, &dcn3_2_soc, &dcn3_2_ip, DML_PROJECT_DCN32);
	}
}

static struct resource_funcs dcn32_res_pool_funcs = {
	.destroy = dcn32_destroy_resource_pool,
	.link_enc_create = dcn32_link_encoder_create,
	.link_enc_create_minimal = NULL,
	.panel_cntl_create = dcn32_panel_cntl_create,
	.validate_bandwidth = dcn32_validate_bandwidth,
	.calculate_wm_and_dlg = dcn32_calculate_wm_and_dlg,
	.populate_dml_pipes = dcn32_populate_dml_pipes_from_context,
	.acquire_idle_pipe_for_layer = dcn20_acquire_idle_pipe_for_layer,
	.add_stream_to_ctx = dcn30_add_stream_to_ctx,
	.add_dsc_to_stream_resource = dcn20_add_dsc_to_stream_resource,
	.remove_stream_from_ctx = dcn20_remove_stream_from_ctx,
	.populate_dml_writeback_from_context = dcn30_populate_dml_writeback_from_context,
	.set_mcif_arb_params = dcn30_set_mcif_arb_params,
	.find_first_free_match_stream_enc_for_link = dcn10_find_first_free_match_stream_enc_for_link,
	.acquire_post_bldn_3dlut = dcn32_acquire_post_bldn_3dlut,
	.release_post_bldn_3dlut = dcn32_release_post_bldn_3dlut,
	.update_bw_bounding_box = dcn32_update_bw_bounding_box,
	.patch_unknown_plane_state = dcn20_patch_unknown_plane_state,
	.update_soc_for_wm_a = dcn30_update_soc_for_wm_a,
	.add_phantom_pipes = dcn32_add_phantom_pipes,
	.remove_phantom_pipes = dcn32_remove_phantom_pipes,
};


static bool dcn32_resource_construct(
	uint8_t num_virtual_links,
	struct dc *dc,
	struct dcn32_resource_pool *pool)
{
	int i, j;
	struct dc_context *ctx = dc->ctx;
	struct irq_service_init_data init_data;
	struct ddc_service_init_data ddc_init_data = {0};
	uint32_t pipe_fuses = 0;
	uint32_t num_pipes  = 4;

    DC_FP_START();

	ctx->dc_bios->regs = &bios_regs;

	pool->base.res_cap = &res_cap_dcn32;
	/* max number of pipes for ASIC before checking for pipe fuses */
	num_pipes  = pool->base.res_cap->num_timing_generator;
	pipe_fuses = REG_READ(CC_DC_PIPE_DIS);

	for (i = 0; i < pool->base.res_cap->num_timing_generator; i++)
		if (pipe_fuses & 1 << i)
			num_pipes--;

	if (pipe_fuses & 1)
		ASSERT(0); //Unexpected - Pipe 0 should always be fully functional!

	if (pipe_fuses & CC_DC_PIPE_DIS__DC_FULL_DIS_MASK)
		ASSERT(0); //Entire DCN is harvested!

	/* within dml lib, initial value is hard coded, if ASIC pipe is fused, the
	 * value will be changed, update max_num_dpp and max_num_otg for dml.
	 */
	dcn3_2_ip.max_num_dpp = num_pipes;
	dcn3_2_ip.max_num_otg = num_pipes;

	pool->base.funcs = &dcn32_res_pool_funcs;

	/*************************************************
	 *  Resource + asic cap harcoding                *
	 *************************************************/
	pool->base.underlay_pipe_index = NO_UNDERLAY_PIPE;
	pool->base.timing_generator_count = num_pipes;
	pool->base.pipe_count = num_pipes;
	pool->base.mpcc_count = num_pipes;
	dc->caps.max_downscale_ratio = 600;
	dc->caps.i2c_speed_in_khz = 100;
	dc->caps.i2c_speed_in_khz_hdcp = 100; /*1.4 w/a applied by default*/
	dc->caps.max_cursor_size = 256;
	dc->caps.min_horizontal_blanking_period = 80;
	dc->caps.dmdata_alloc_size = 2048;
	dc->caps.mall_size_per_mem_channel = 0;
	dc->caps.mall_size_total = 0;
	dc->caps.cursor_cache_size = dc->caps.max_cursor_size * dc->caps.max_cursor_size * 8;

	dc->caps.cache_line_size = 64;
	dc->caps.cache_num_ways = 16;
	dc->caps.max_cab_allocation_bytes = 67108864; // 64MB = 1024 * 1024 * 64
	dc->caps.subvp_fw_processing_delay_us = 15;
	dc->caps.subvp_prefetch_end_to_mall_start_us = 15;
	dc->caps.subvp_pstate_allow_width_us = 20;
	dc->caps.subvp_vertical_int_margin_us = 30;

	dc->caps.max_slave_planes = 2;
	dc->caps.max_slave_yuv_planes = 2;
	dc->caps.max_slave_rgb_planes = 2;
	dc->caps.post_blend_color_processing = true;
	dc->caps.force_dp_tps4_for_cp2520 = true;
	dc->caps.dp_hpo = true;
	dc->caps.edp_dsc_support = true;
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
	dc->caps.color.dpp.ogam_ram = 0;  //Blnd Gam also removed
	// no OGAM ROM on DCN2 and later ASICs
	dc->caps.color.dpp.ogam_rom_caps.srgb = 0;
	dc->caps.color.dpp.ogam_rom_caps.bt2020 = 0;
	dc->caps.color.dpp.ogam_rom_caps.gamma2_2 = 0;
	dc->caps.color.dpp.ogam_rom_caps.pq = 0;
	dc->caps.color.dpp.ogam_rom_caps.hlg = 0;
	dc->caps.color.dpp.ocsc = 0;

	dc->caps.color.mpc.gamut_remap = 1;
	dc->caps.color.mpc.num_3dluts = pool->base.res_cap->num_mpc_3dlut; //4, configurable to be before or after BLND in MPCC
	dc->caps.color.mpc.ogam_ram = 1;
	dc->caps.color.mpc.ogam_rom_caps.srgb = 0;
	dc->caps.color.mpc.ogam_rom_caps.bt2020 = 0;
	dc->caps.color.mpc.ogam_rom_caps.gamma2_2 = 0;
	dc->caps.color.mpc.ogam_rom_caps.pq = 0;
	dc->caps.color.mpc.ogam_rom_caps.hlg = 0;
	dc->caps.color.mpc.ocsc = 1;

	/* Use pipe context based otg sync logic */
	dc->config.use_pipe_ctx_sync_logic = true;

	/* read VBIOS LTTPR caps */
	{
		if (ctx->dc_bios->funcs->get_lttpr_caps) {
			enum bp_result bp_query_result;
			uint8_t is_vbios_lttpr_enable = 0;

			bp_query_result = ctx->dc_bios->funcs->get_lttpr_caps(ctx->dc_bios, &is_vbios_lttpr_enable);
			dc->caps.vbios_lttpr_enable = (bp_query_result == BP_RESULT_OK) && !!is_vbios_lttpr_enable;
		}

		/* interop bit is implicit */
		{
			dc->caps.vbios_lttpr_aware = true;
		}
	}

	if (dc->ctx->dce_environment == DCE_ENV_PRODUCTION_DRV)
		dc->debug = debug_defaults_drv;
	else if (dc->ctx->dce_environment == DCE_ENV_FPGA_MAXIMUS) {
		dc->debug = debug_defaults_diags;
	} else
		dc->debug = debug_defaults_diags;
	// Init the vm_helper
	if (dc->vm_helper)
		vm_helper_init(dc->vm_helper, 16);

	/*************************************************
	 *  Create resources                             *
	 *************************************************/

	/* Clock Sources for Pixel Clock*/
	pool->base.clock_sources[DCN32_CLK_SRC_PLL0] =
			dcn32_clock_source_create(ctx, ctx->dc_bios,
				CLOCK_SOURCE_COMBO_PHY_PLL0,
				&clk_src_regs[0], false);
	pool->base.clock_sources[DCN32_CLK_SRC_PLL1] =
			dcn32_clock_source_create(ctx, ctx->dc_bios,
				CLOCK_SOURCE_COMBO_PHY_PLL1,
				&clk_src_regs[1], false);
	pool->base.clock_sources[DCN32_CLK_SRC_PLL2] =
			dcn32_clock_source_create(ctx, ctx->dc_bios,
				CLOCK_SOURCE_COMBO_PHY_PLL2,
				&clk_src_regs[2], false);
	pool->base.clock_sources[DCN32_CLK_SRC_PLL3] =
			dcn32_clock_source_create(ctx, ctx->dc_bios,
				CLOCK_SOURCE_COMBO_PHY_PLL3,
				&clk_src_regs[3], false);
	pool->base.clock_sources[DCN32_CLK_SRC_PLL4] =
			dcn32_clock_source_create(ctx, ctx->dc_bios,
				CLOCK_SOURCE_COMBO_PHY_PLL4,
				&clk_src_regs[4], false);

	pool->base.clk_src_count = DCN32_CLK_SRC_TOTAL;

	/* todo: not reuse phy_pll registers */
	pool->base.dp_clock_source =
			dcn32_clock_source_create(ctx, ctx->dc_bios,
				CLOCK_SOURCE_ID_DP_DTO,
				&clk_src_regs[0], true);

	for (i = 0; i < pool->base.clk_src_count; i++) {
		if (pool->base.clock_sources[i] == NULL) {
			dm_error("DC: failed to create clock sources!\n");
			BREAK_TO_DEBUGGER();
			goto create_fail;
		}
	}

	/* DCCG */
	pool->base.dccg = dccg32_create(ctx, &dccg_regs, &dccg_shift, &dccg_mask);
	if (pool->base.dccg == NULL) {
		dm_error("DC: failed to create dccg!\n");
		BREAK_TO_DEBUGGER();
		goto create_fail;
	}

	/* DML */
	if (!IS_FPGA_MAXIMUS_DC(dc->ctx->dce_environment))
		dml_init_instance(&dc->dml, &dcn3_2_soc, &dcn3_2_ip, DML_PROJECT_DCN32);

	/* IRQ Service */
	init_data.ctx = dc->ctx;
	pool->base.irqs = dal_irq_service_dcn32_create(&init_data);
	if (!pool->base.irqs)
		goto create_fail;

	/* HUBBUB */
	pool->base.hubbub = dcn32_hubbub_create(ctx);
	if (pool->base.hubbub == NULL) {
		BREAK_TO_DEBUGGER();
		dm_error("DC: failed to create hubbub!\n");
		goto create_fail;
	}

	/* HUBPs, DPPs, OPPs, TGs, ABMs */
	for (i = 0, j = 0; i < pool->base.res_cap->num_timing_generator; i++) {

		/* if pipe is disabled, skip instance of HW pipe,
		 * i.e, skip ASIC register instance
		 */
		if (pipe_fuses & 1 << i)
			continue;

		/* HUBPs */
		pool->base.hubps[j] = dcn32_hubp_create(ctx, i);
		if (pool->base.hubps[j] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC: failed to create hubps!\n");
			goto create_fail;
		}

		/* DPPs */
		pool->base.dpps[j] = dcn32_dpp_create(ctx, i);
		if (pool->base.dpps[j] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC: failed to create dpps!\n");
			goto create_fail;
		}

		/* OPPs */
		pool->base.opps[j] = dcn32_opp_create(ctx, i);
		if (pool->base.opps[j] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC: failed to create output pixel processor!\n");
			goto create_fail;
		}

		/* TGs */
		pool->base.timing_generators[j] = dcn32_timing_generator_create(
				ctx, i);
		if (pool->base.timing_generators[j] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error("DC: failed to create tg!\n");
			goto create_fail;
		}

		/* ABMs */
		pool->base.multiple_abms[j] = dmub_abm_create(ctx,
				&abm_regs[i],
				&abm_shift,
				&abm_mask);
		if (pool->base.multiple_abms[j] == NULL) {
			dm_error("DC: failed to create abm for pipe %d!\n", i);
			BREAK_TO_DEBUGGER();
			goto create_fail;
		}

		/* index for resource pool arrays for next valid pipe */
		j++;
	}

	/* PSR */
	pool->base.psr = dmub_psr_create(ctx);
	if (pool->base.psr == NULL) {
		dm_error("DC: failed to create psr obj!\n");
		BREAK_TO_DEBUGGER();
		goto create_fail;
	}

	/* MPCCs */
	pool->base.mpc = dcn32_mpc_create(ctx, pool->base.res_cap->num_timing_generator, pool->base.res_cap->num_mpc_3dlut);
	if (pool->base.mpc == NULL) {
		BREAK_TO_DEBUGGER();
		dm_error("DC: failed to create mpc!\n");
		goto create_fail;
	}

	/* DSCs */
	for (i = 0; i < pool->base.res_cap->num_dsc; i++) {
		pool->base.dscs[i] = dcn32_dsc_create(ctx, i);
		if (pool->base.dscs[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error("DC: failed to create display stream compressor %d!\n", i);
			goto create_fail;
		}
	}

	/* DWB */
	if (!dcn32_dwbc_create(ctx, &pool->base)) {
		BREAK_TO_DEBUGGER();
		dm_error("DC: failed to create dwbc!\n");
		goto create_fail;
	}

	/* MMHUBBUB */
	if (!dcn32_mmhubbub_create(ctx, &pool->base)) {
		BREAK_TO_DEBUGGER();
		dm_error("DC: failed to create mcif_wb!\n");
		goto create_fail;
	}

	/* AUX and I2C */
	for (i = 0; i < pool->base.res_cap->num_ddc; i++) {
		pool->base.engines[i] = dcn32_aux_engine_create(ctx, i);
		if (pool->base.engines[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC:failed to create aux engine!!\n");
			goto create_fail;
		}
		pool->base.hw_i2cs[i] = dcn32_i2c_hw_create(ctx, i);
		if (pool->base.hw_i2cs[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC:failed to create hw i2c!!\n");
			goto create_fail;
		}
		pool->base.sw_i2cs[i] = NULL;
	}

	/* Audio, HWSeq, Stream Encoders including HPO and virtual, MPC 3D LUTs */
	if (!resource_construct(num_virtual_links, dc, &pool->base,
			(!IS_FPGA_MAXIMUS_DC(dc->ctx->dce_environment) ?
			&res_create_funcs : &res_create_maximus_funcs)))
			goto create_fail;

	/* HW Sequencer init functions and Plane caps */
	dcn32_hw_sequencer_init_functions(dc);

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

	dcn32_resource_destruct(pool);

	return false;
}

struct resource_pool *dcn32_create_resource_pool(
		const struct dc_init_data *init_data,
		struct dc *dc)
{
	struct dcn32_resource_pool *pool =
		kzalloc(sizeof(struct dcn32_resource_pool), GFP_KERNEL);

	if (!pool)
		return NULL;

	if (dcn32_resource_construct(init_data->num_virtual_links, dc, pool))
		return &pool->base;

	BREAK_TO_DEBUGGER();
	kfree(pool);
	return NULL;
}
