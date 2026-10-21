// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#include "dml2_core_dcn6_funcs_initialize.h"
#include "dml2_debug.h"

struct dml2_core_ip_params core_dcn6_ip_caps_base = {
	// currently copied from DCN4
	.vblank_nom_default_us = 668,
	.remote_iommu_outstanding_translations = 512,
	.rob_buffer_size_kbytes = 192,
	.config_return_buffer_size_in_kbytes = 2112,
	.config_return_buffer_segment_size_in_kbytes = 64,
	.compressed_buffer_segment_size_in_kbytes = 64,
	.dpte_buffer_size_in_pte_reqs_luma = 138,
	.dpte_buffer_size_in_pte_reqs_chroma = 138,
	.pixel_chunk_size_kbytes = 8,
	.alpha_pixel_chunk_size_kbytes = 4,
	.min_pixel_chunk_size_bytes = 1024,
	.writeback_chunk_size_kbytes = 8,
	.line_buffer_size_bits = 937536,
	.max_line_buffer_lines = 32,
	.writeback_interface_buffer_size_kbytes = 90,
	//Number of pipes after DCN Pipe harvesting
	.max_num_dpp = 4,
	.max_num_otg = 4,
	.max_num_wb = 1,
	.zero_size_buffer_entries = 512,
	.compbuf_reserved_space_zs = 64,
	.dcc_meta_buffer_size_bytes = 6272,
	.meta_chunk_size_kbytes = 2,
	.min_meta_chunk_size_bytes = 256,
	.max_dchub_pscl_bw_pix_per_clk = 4,
	.max_pscl_lb_bw_pix_per_clk = 2,
	.max_lb_vscl_bw_pix_per_clk = 4,
	.max_vscl_hscl_bw_pix_per_clk = 4,
	.max_hscl_ratio = 6,
	.max_vscl_ratio = 6,
	.max_hscl_taps = 8,
	.max_vscl_taps = 8,
	.dispclk_ramp_margin_percent = 1,
	.dppclk_delay_subtotal = 41,
	.dppclk_delay_scl = 50,
	.dppclk_delay_scl_lb_only = 16,
	.dppclk_delay_cnvc_formatter = 28,
	.dppclk_delay_cnvc_cursor = 2,
	.cursor_buffer_size = 24,
	.cursor_chunk_size = 2,
	.dispclk_delay_subtotal = 135,
	.max_inter_dcn_tile_repeaters = 8,
	.writeback_max_hscl_ratio = 1,
	.writeback_max_vscl_ratio = 1,
	.writeback_min_hscl_ratio = 1,
	.writeback_min_vscl_ratio = 1,
	.writeback_max_hscl_taps = 1,
	.writeback_max_vscl_taps = 1,
	.writeback_line_buffer_buffer_size = 0,
	.odm_combine_support_mask = (1 << dml2_odm_mode_auto) |
			(1 << dml2_odm_mode_bypass) |
			(1 << dml2_odm_mode_combine_2to1) |
			(1 << dml2_odm_mode_combine_3to1) |
			(1 << dml2_odm_mode_combine_4to1) |
			(1 << dml2_odm_mode_split_1to2) |
			(1 << dml2_odm_mode_mso_1to2) |
			(1 << dml2_odm_mode_mso_1to4),
	.num_dsc = 4,
	.maximum_dsc_slices_per_pipe = 8,
	.maximum_dsc_bits_per_component = 12,
	.maximum_pixels_per_line_per_dsc_unit = 5760,
	.dsc422_native_support = true,
	.dcc_supported = true,
	.ptoi_supported = false,

	.cursor_64bpp_support = true,
	.dynamic_metadata_vm_enabled = false,

	.max_num_hdmi_frl_outputs = 1,
	.max_num_dp2p0_outputs = 4,
	.max_num_dp2p0_streams = 4,
	.imall_supported = 1,
	.max_flip_time_us = 80,
	.max_flip_time_lines = 32,
	.words_per_channel = 16,
	.alt_chan_fw_delay_us = 955, // Overestimate for now, can reduce later: sum of worst case throttle, programming, scheduling and contention delays
};

static void patch_ip_caps_with_explicit_ip_params(struct dml2_ip_capabilities *ip_caps, const struct dml2_core_ip_params *ip_params)
{
	ip_caps->pipe_count = ip_params->max_num_dpp;
	ip_caps->otg_count = ip_params->max_num_otg;
	ip_caps->TDLUT_33cube_count = ip_params->TDLUT_33cube_count;
	ip_caps->num_dsc = ip_params->num_dsc;
	ip_caps->max_num_dp2p0_streams = ip_params->max_num_dp2p0_streams;
	ip_caps->max_num_dp2p0_outputs = ip_params->max_num_dp2p0_outputs;
	ip_caps->max_num_hdmi_frl_outputs = ip_params->max_num_hdmi_frl_outputs;
	ip_caps->max_num_wb = ip_params->max_num_wb;
	ip_caps->rob_buffer_size_kbytes = ip_params->rob_buffer_size_kbytes;
	ip_caps->config_return_buffer_size_in_kbytes = ip_params->config_return_buffer_size_in_kbytes;
	ip_caps->config_return_buffer_segment_size_in_kbytes = ip_params->config_return_buffer_segment_size_in_kbytes;
	ip_caps->meta_fifo_size_in_kentries = ip_params->meta_fifo_size_in_kentries;
	ip_caps->compressed_buffer_segment_size_in_kbytes = ip_params->compressed_buffer_segment_size_in_kbytes;
	ip_caps->cursor_buffer_size = ip_params->cursor_buffer_size;
	ip_caps->max_flip_time_us = ip_params->max_flip_time_us;
	ip_caps->max_flip_time_lines = ip_params->max_flip_time_lines;
	ip_caps->hostvm_mode = ip_params->hostvm_mode;
	ip_caps->vblank_nom_default_us = ip_params->vblank_nom_default_us;

}

static void patch_ip_params_with_ip_caps(struct dml2_core_ip_params *ip_params, const struct dml2_ip_capabilities *ip_caps)
{
	ip_params->max_num_dpp = ip_caps->pipe_count;
	ip_params->max_num_otg = ip_caps->otg_count;
	ip_params->TDLUT_33cube_count = ip_caps->TDLUT_33cube_count;
	ip_params->num_dsc = ip_caps->num_dsc;
	ip_params->max_num_dp2p0_streams = ip_caps->max_num_dp2p0_streams;
	ip_params->max_num_dp2p0_outputs = ip_caps->max_num_dp2p0_outputs;
	ip_params->max_num_hdmi_frl_outputs = ip_caps->max_num_hdmi_frl_outputs;
	ip_params->max_num_wb = ip_caps->max_num_wb;
	ip_params->rob_buffer_size_kbytes = ip_caps->rob_buffer_size_kbytes;
	ip_params->config_return_buffer_size_in_kbytes = ip_caps->config_return_buffer_size_in_kbytes;
	ip_params->config_return_buffer_segment_size_in_kbytes = ip_caps->config_return_buffer_segment_size_in_kbytes;
	ip_params->meta_fifo_size_in_kentries = ip_caps->meta_fifo_size_in_kentries;
	ip_params->compressed_buffer_segment_size_in_kbytes = ip_caps->compressed_buffer_segment_size_in_kbytes;
	ip_params->cursor_buffer_size = ip_caps->cursor_buffer_size;
	ip_params->max_flip_time_us = ip_caps->max_flip_time_us;
	ip_params->max_flip_time_lines = ip_caps->max_flip_time_lines;
	ip_params->hostvm_mode = ip_caps->hostvm_mode;
	ip_params->alt_chan_fw_delay_us = ip_caps->fams2.scheduling_delay_us +
										ip_caps->fams2.subvp_programming_delay_us +
										ip_caps->fams2.subvp_df_throttle_delay_us +
										(ip_caps->fams2.vertical_interrupt_ack_delay_us +
										ip_caps->fams2.drr_programming_delay_us > ip_caps->fams2.allow_programming_delay_us ?
										ip_caps->fams2.drr_programming_delay_us : ip_caps->fams2.allow_programming_delay_us) * (ip_caps->otg_count - 1);
	ip_params->dcn_mrq_present = ip_caps->dcn_mrq_present;
	ip_params->fams2_max_allow_delay_us = ip_caps->fams2.max_allow_delay_us;
	ip_params->fams2_min_allow_width_us = ip_caps->fams2.min_allow_width_us;
	ip_params->ppt_max_allow_delay_us = ip_caps->ppt_max_allow_delay_us;
	ip_params->temp_read_max_allow_delay_us = ip_caps->temp_read_max_allow_delay_us;
}

bool dml2_core_dcn6_funcs_initialize(struct dml2_core_initialize_in_out *in_out)
{
	struct dml2_core_instance *core = in_out->instance;

	DML_LOG_COMP_IF_ENTER();
	if (in_out->explicit_ip_bb && in_out->explicit_ip_bb_size > 0) {
		memcpy(&core->clean_me_up.mode_lib.ip, in_out->explicit_ip_bb, in_out->explicit_ip_bb_size);
		patch_ip_caps_with_explicit_ip_params(in_out->ip_caps, in_out->explicit_ip_bb);
	} else {
		memcpy(&core->clean_me_up.mode_lib.ip, &core_dcn6_ip_caps_base, sizeof(struct dml2_core_ip_params));
		patch_ip_params_with_ip_caps(&core->clean_me_up.mode_lib.ip, in_out->ip_caps);

		core->clean_me_up.mode_lib.ip.imall_supported = false;
	}

	memcpy(&core->clean_me_up.mode_lib.ip_caps, in_out->ip_caps, sizeof(struct dml2_ip_capabilities));
	core->utm_soc_bb = in_out->utm_soc_bb;
	core->clock_adjuster = in_out->clock_adjuster;

	DML_LOG_DEBUG("%s exit with true\n", __func__);
	DML_LOG_COMP_IF_EXIT();
	return true;
}
