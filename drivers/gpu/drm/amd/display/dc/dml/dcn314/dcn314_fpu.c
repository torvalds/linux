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

#include "clk_mgr.h"
#include "resource.h"
#include "dcn31/dcn31_hubbub.h"
#include "dcn314_fpu.h"
#include "dml/dcn20/dcn20_fpu.h"
#include "dml/dcn31/dcn31_fpu.h"
#include "dml/display_mode_vba.h"

struct _vcs_dpi_ip_params_st dcn3_14_ip = {
	.VBlankNomDefaultUS = 800,
	.gpuvm_enable = 1,
	.gpuvm_max_page_table_levels = 1,
	.hostvm_enable = 1,
	.hostvm_max_page_table_levels = 2,
	.rob_buffer_size_kbytes = 64,
	.det_buffer_size_kbytes = DCN3_14_DEFAULT_DET_SIZE,
	.config_return_buffer_size_in_kbytes = 1792,
	.compressed_buffer_segment_size_in_kbytes = 64,
	.meta_fifo_size_in_kentries = 32,
	.zero_size_buffer_entries = 512,
	.compbuf_reserved_space_64b = 256,
	.compbuf_reserved_space_zs = 64,
	.dpp_output_buffer_pixels = 2560,
	.opp_output_buffer_lines = 1,
	.pixel_chunk_size_kbytes = 8,
	.meta_chunk_size_kbytes = 2,
	.min_meta_chunk_size_bytes = 256,
	.writeback_chunk_size_kbytes = 8,
	.ptoi_supported = false,
	.num_dsc = 4,
	.maximum_dsc_bits_per_component = 10,
	.dsc422_native_support = false,
	.is_line_buffer_bpp_fixed = true,
	.line_buffer_fixed_bpp = 48,
	.line_buffer_size_bits = 789504,
	.max_line_buffer_lines = 12,
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
	.dppclk_delay_subtotal = 46,
	.dppclk_delay_scl = 50,
	.dppclk_delay_scl_lb_only = 16,
	.dppclk_delay_cnvc_formatter = 27,
	.dppclk_delay_cnvc_cursor = 6,
	.dispclk_delay_subtotal = 119,
	.dynamic_metadata_vm_enabled = false,
	.odm_combine_4to1_supported = false,
	.dcc_supported = true,
};

static struct _vcs_dpi_soc_bounding_box_st dcn3_14_soc = {
		/*TODO: correct dispclk/dppclk voltage level determination*/
	.clock_limits = {
		{
			.state = 0,
			.dispclk_mhz = 1200.0,
			.dppclk_mhz = 1200.0,
			.phyclk_mhz = 600.0,
			.phyclk_d18_mhz = 667.0,
			.dscclk_mhz = 186.0,
			.dtbclk_mhz = 600.0,
		},
		{
			.state = 1,
			.dispclk_mhz = 1200.0,
			.dppclk_mhz = 1200.0,
			.phyclk_mhz = 810.0,
			.phyclk_d18_mhz = 667.0,
			.dscclk_mhz = 209.0,
			.dtbclk_mhz = 600.0,
		},
		{
			.state = 2,
			.dispclk_mhz = 1200.0,
			.dppclk_mhz = 1200.0,
			.phyclk_mhz = 810.0,
			.phyclk_d18_mhz = 667.0,
			.dscclk_mhz = 209.0,
			.dtbclk_mhz = 600.0,
		},
		{
			.state = 3,
			.dispclk_mhz = 1200.0,
			.dppclk_mhz = 1200.0,
			.phyclk_mhz = 810.0,
			.phyclk_d18_mhz = 667.0,
			.dscclk_mhz = 371.0,
			.dtbclk_mhz = 600.0,
		},
		{
			.state = 4,
			.dispclk_mhz = 1200.0,
			.dppclk_mhz = 1200.0,
			.phyclk_mhz = 810.0,
			.phyclk_d18_mhz = 667.0,
			.dscclk_mhz = 417.0,
			.dtbclk_mhz = 600.0,
		},
	},
	.num_states = 5,
	.sr_exit_time_us = 16.5,
	.sr_enter_plus_exit_time_us = 18.5,
	.sr_exit_z8_time_us = 268.0,
	.sr_enter_plus_exit_z8_time_us = 393.0,
	.writeback_latency_us = 12.0,
	.dram_channel_width_bytes = 4,
	.round_trip_ping_latency_dcfclk_cycles = 106,
	.urgent_latency_pixel_data_only_us = 4.0,
	.urgent_latency_pixel_mixed_with_vm_data_us = 4.0,
	.urgent_latency_vm_data_only_us = 4.0,
	.urgent_out_of_order_return_per_channel_pixel_only_bytes = 4096,
	.urgent_out_of_order_return_per_channel_pixel_and_vm_bytes = 4096,
	.urgent_out_of_order_return_per_channel_vm_only_bytes = 4096,
	.pct_ideal_sdp_bw_after_urgent = 80.0,
	.pct_ideal_dram_sdp_bw_after_urgent_pixel_only = 65.0,
	.pct_ideal_dram_sdp_bw_after_urgent_pixel_and_vm = 60.0,
	.pct_ideal_dram_sdp_bw_after_urgent_vm_only = 30.0,
	.max_avg_sdp_bw_use_normal_percent = 60.0,
	.max_avg_dram_bw_use_normal_percent = 60.0,
	.fabric_datapath_to_dcn_data_return_bytes = 32,
	.return_bus_width_bytes = 64,
	.downspread_percent = 0.38,
	.dcn_downspread_percent = 0.5,
	.gpuvm_min_page_size_bytes = 4096,
	.hostvm_min_page_size_bytes = 4096,
	.do_urgent_latency_adjustment = false,
	.urgent_latency_adjustment_fabric_clock_component_us = 0,
	.urgent_latency_adjustment_fabric_clock_reference_mhz = 0,
};


void dcn314_update_bw_bounding_box_fpu(struct dc *dc, struct clk_bw_params *bw_params)
{
	struct clk_limit_table *clk_table = &bw_params->clk_table;
	struct _vcs_dpi_voltage_scaling_st *clock_limits =
		dcn3_14_soc.clock_limits;
	unsigned int i, closest_clk_lvl;
	int max_dispclk_mhz = 0, max_dppclk_mhz = 0;
	int j;

	dc_assert_fp_enabled();

	// Default clock levels are used for diags, which may lead to overclocking.
	if (dc->config.use_default_clock_table == false) {
		dcn3_14_ip.max_num_otg = dc->res_pool->res_cap->num_timing_generator;
		dcn3_14_ip.max_num_dpp = dc->res_pool->pipe_count;

		if (bw_params->dram_channel_width_bytes > 0)
			dcn3_14_soc.dram_channel_width_bytes = bw_params->dram_channel_width_bytes;

		if (bw_params->num_channels > 0)
			dcn3_14_soc.num_chans = bw_params->num_channels;

		ASSERT(dcn3_14_soc.num_chans);
		ASSERT(clk_table->num_entries);

		/* Prepass to find max clocks independent of voltage level. */
		for (i = 0; i < clk_table->num_entries; ++i) {
			if (clk_table->entries[i].dispclk_mhz > max_dispclk_mhz)
				max_dispclk_mhz = clk_table->entries[i].dispclk_mhz;
			if (clk_table->entries[i].dppclk_mhz > max_dppclk_mhz)
				max_dppclk_mhz = clk_table->entries[i].dppclk_mhz;
		}

		for (i = 0; i < clk_table->num_entries; i++) {
			/* loop backwards*/
			for (closest_clk_lvl = 0, j = dcn3_14_soc.num_states - 1; j >= 0; j--) {
				if ((unsigned int) dcn3_14_soc.clock_limits[j].dcfclk_mhz <= clk_table->entries[i].dcfclk_mhz) {
					closest_clk_lvl = j;
					break;
				}
			}
			if (clk_table->num_entries == 1) {
				/*smu gives one DPM level, let's take the highest one*/
				closest_clk_lvl = dcn3_14_soc.num_states - 1;
			}

			clock_limits[i].state = i;

			/* Clocks dependent on voltage level. */
			clock_limits[i].dcfclk_mhz = clk_table->entries[i].dcfclk_mhz;
			if (clk_table->num_entries == 1 &&
				clock_limits[i].dcfclk_mhz < dcn3_14_soc.clock_limits[closest_clk_lvl].dcfclk_mhz) {
				/*SMU fix not released yet*/
				clock_limits[i].dcfclk_mhz = dcn3_14_soc.clock_limits[closest_clk_lvl].dcfclk_mhz;
			}
			clock_limits[i].fabricclk_mhz = clk_table->entries[i].fclk_mhz;
			clock_limits[i].socclk_mhz = clk_table->entries[i].socclk_mhz;

			if (clk_table->entries[i].memclk_mhz && clk_table->entries[i].wck_ratio)
				clock_limits[i].dram_speed_mts = clk_table->entries[i].memclk_mhz * 2 * clk_table->entries[i].wck_ratio;

			/* Clocks independent of voltage level. */
			clock_limits[i].dispclk_mhz = max_dispclk_mhz ? max_dispclk_mhz :
				dcn3_14_soc.clock_limits[closest_clk_lvl].dispclk_mhz;

			clock_limits[i].dppclk_mhz = max_dppclk_mhz ? max_dppclk_mhz :
				dcn3_14_soc.clock_limits[closest_clk_lvl].dppclk_mhz;

			clock_limits[i].dram_bw_per_chan_gbps = dcn3_14_soc.clock_limits[closest_clk_lvl].dram_bw_per_chan_gbps;
			clock_limits[i].dscclk_mhz = dcn3_14_soc.clock_limits[closest_clk_lvl].dscclk_mhz;
			clock_limits[i].dtbclk_mhz = dcn3_14_soc.clock_limits[closest_clk_lvl].dtbclk_mhz;
			clock_limits[i].phyclk_d18_mhz = dcn3_14_soc.clock_limits[closest_clk_lvl].phyclk_d18_mhz;
			clock_limits[i].phyclk_mhz = dcn3_14_soc.clock_limits[closest_clk_lvl].phyclk_mhz;
		}
		for (i = 0; i < clk_table->num_entries; i++)
			dcn3_14_soc.clock_limits[i] = clock_limits[i];
		if (clk_table->num_entries) {
			dcn3_14_soc.num_states = clk_table->num_entries;
		}
	}

	if (max_dispclk_mhz) {
		dcn3_14_soc.dispclk_dppclk_vco_speed_mhz = max_dispclk_mhz * 2;
		dc->dml.soc.dispclk_dppclk_vco_speed_mhz = max_dispclk_mhz * 2;
	}

	dcn20_patch_bounding_box(dc, &dcn3_14_soc);
	dml_init_instance(&dc->dml, &dcn3_14_soc, &dcn3_14_ip, DML_PROJECT_DCN314);
}

static bool is_dual_plane(enum surface_pixel_format format)
{
	return format >= SURFACE_PIXEL_FORMAT_VIDEO_BEGIN || format == SURFACE_PIXEL_FORMAT_GRPH_RGBE_ALPHA;
}

int dcn314_populate_dml_pipes_from_context_fpu(struct dc *dc, struct dc_state *context,
					       display_e2e_pipe_params_st *pipes,
					       bool fast_validate)
{
	int i, pipe_cnt;
	struct resource_context *res_ctx = &context->res_ctx;
	struct pipe_ctx *pipe;
	bool upscaled = false;
	const unsigned int max_allowed_vblank_nom = 1023;

	dc_assert_fp_enabled();

	dcn31x_populate_dml_pipes_from_context(dc, context, pipes, fast_validate);

	for (i = 0, pipe_cnt = 0; i < dc->res_pool->pipe_count; i++) {
		struct dc_crtc_timing *timing;

		if (!res_ctx->pipe_ctx[i].stream)
			continue;
		pipe = &res_ctx->pipe_ctx[i];
		timing = &pipe->stream->timing;

		pipes[pipe_cnt].pipe.dest.vtotal = pipe->stream->adjust.v_total_min;
		pipes[pipe_cnt].pipe.dest.vblank_nom = timing->v_total - pipes[pipe_cnt].pipe.dest.vactive;
		pipes[pipe_cnt].pipe.dest.vblank_nom = min(pipes[pipe_cnt].pipe.dest.vblank_nom, dcn3_14_ip.VBlankNomDefaultUS);
		pipes[pipe_cnt].pipe.dest.vblank_nom = max(pipes[pipe_cnt].pipe.dest.vblank_nom, timing->v_sync_width);
		pipes[pipe_cnt].pipe.dest.vblank_nom = min(pipes[pipe_cnt].pipe.dest.vblank_nom, max_allowed_vblank_nom);

		if (pipe->plane_state &&
				(pipe->plane_state->src_rect.height < pipe->plane_state->dst_rect.height ||
				pipe->plane_state->src_rect.width < pipe->plane_state->dst_rect.width))
			upscaled = true;

		/* Apply HostVM policy - either based on hypervisor globally enabled, or rIOMMU active */
		if (dc->debug.dml_hostvm_override == DML_HOSTVM_NO_OVERRIDE)
			pipes[i].pipe.src.hostvm = dc->vm_pa_config.is_hvm_enabled || dc->res_pool->hubbub->riommu_active;

		/*
		 * Immediate flip can be set dynamically after enabling the plane.
		 * We need to require support for immediate flip or underflow can be
		 * intermittently experienced depending on peak b/w requirements.
		 */
		pipes[pipe_cnt].pipe.src.immediate_flip = true;

		pipes[pipe_cnt].pipe.src.unbounded_req_mode = false;
		pipes[pipe_cnt].pipe.src.dcc_fraction_of_zs_req_luma = 0;
		pipes[pipe_cnt].pipe.src.dcc_fraction_of_zs_req_chroma = 0;
		pipes[pipe_cnt].pipe.dest.vfront_porch = timing->v_front_porch;
		pipes[pipe_cnt].pipe.src.dcc_rate = 3;
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
	context->bw_ctx.dml.ip.det_buffer_size_kbytes = DCN3_14_DEFAULT_DET_SIZE;

	dc->config.enable_4to1MPC = false;
	if (pipe_cnt == 1 && pipe->plane_state
		&& pipe->plane_state->rotation == ROTATION_ANGLE_0 && !dc->debug.disable_z9_mpc) {
		if (is_dual_plane(pipe->plane_state->format)
				&& pipe->plane_state->src_rect.width <= 1920 && pipe->plane_state->src_rect.height <= 1080) {
			dc->config.enable_4to1MPC = true;
		} else if (!is_dual_plane(pipe->plane_state->format) && pipe->plane_state->src_rect.width <= 5120) {
			/* Limit to 5k max to avoid forced pipe split when there is not enough detile for swath */
			context->bw_ctx.dml.ip.det_buffer_size_kbytes = 192;
			pipes[0].pipe.src.unbounded_req_mode = true;
		}
	} else if (context->stream_count >= dc->debug.crb_alloc_policy_min_disp_count
			&& dc->debug.crb_alloc_policy > DET_SIZE_DEFAULT) {
		context->bw_ctx.dml.ip.det_buffer_size_kbytes = dc->debug.crb_alloc_policy * 64;
	} else if (context->stream_count >= 3 && upscaled) {
		context->bw_ctx.dml.ip.det_buffer_size_kbytes = 192;
	}

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];

		if (!pipe->stream)
			continue;

		if (pipe->stream->signal == SIGNAL_TYPE_EDP && dc->debug.seamless_boot_odm_combine &&
				pipe->stream->apply_seamless_boot_optimization) {

			if (pipe->stream->apply_boot_odm_mode == dm_odm_combine_policy_2to1) {
				context->bw_ctx.dml.vba.ODMCombinePolicy = dm_odm_combine_policy_2to1;
				break;
			}
		}
	}

	return pipe_cnt;
}
