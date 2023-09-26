// SPDX-License-Identifier: MIT
/*
 * Copyright 2023 Advanced Micro Devices, Inc.
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
#include "resource.h"
#include "dcn35_fpu.h"
#include "dcn31/dcn31_resource.h"
#include "dcn32/dcn32_resource.h"
#include "dcn35/dcn35_resource.h"
#include "dml/dcn31/dcn31_fpu.h"
#include "dml/dml_inline_defs.h"

#include "link.h"

#define DC_LOGGER_INIT(logger)

struct _vcs_dpi_ip_params_st dcn3_5_ip = {
	.VBlankNomDefaultUS = 668,
	.gpuvm_enable = 1,
	.gpuvm_max_page_table_levels = 1,
	.hostvm_enable = 1,
	.hostvm_max_page_table_levels = 2,
	.rob_buffer_size_kbytes = 64,
	.det_buffer_size_kbytes = 1536,
	.config_return_buffer_size_in_kbytes = 1792,
	.compressed_buffer_segment_size_in_kbytes = 64,
	.meta_fifo_size_in_kentries = 32,
	.zero_size_buffer_entries = 512,
	.compbuf_reserved_space_64b = 256,
	.compbuf_reserved_space_zs = 64,
	.dpp_output_buffer_pixels = 2560,/*not used*/
	.opp_output_buffer_lines = 1,/*not used*/
	.pixel_chunk_size_kbytes = 8,
	//.alpha_pixel_chunk_size_kbytes = 4;/*new*/
	//.min_pixel_chunk_size_bytes = 1024;/*new*/
	.meta_chunk_size_kbytes = 2,
	.min_meta_chunk_size_bytes = 256,
	.writeback_chunk_size_kbytes = 8,
	.ptoi_supported = false,
	.num_dsc = 4,
	.maximum_dsc_bits_per_component = 12,/*delta from 10*/
	.dsc422_native_support = true,/*delta from false*/
	.is_line_buffer_bpp_fixed = true,/*new*/
	.line_buffer_fixed_bpp = 32,/*delta from 48*/
	.line_buffer_size_bits = 986880,/*delta from 789504*/
	.max_line_buffer_lines = 32,/*delta from 12*/
	.writeback_interface_buffer_size_kbytes = 90,
	.max_num_dpp = 4,
	.max_num_otg = 4,
	.max_num_hdmi_frl_outputs = 1,
	.max_num_wb = 1,
	/*.max_num_hdmi_frl_outputs = 1; new in dml2*/
	/*.max_num_dp2p0_outputs = 2; new in dml2*/
	/*.max_num_dp2p0_streams = 4; new in dml2*/
	.max_dchub_pscl_bw_pix_per_clk = 4,
	.max_pscl_lb_bw_pix_per_clk = 2,
	.max_lb_vscl_bw_pix_per_clk = 4,
	.max_vscl_hscl_bw_pix_per_clk = 4,
	.max_hscl_ratio = 6,
	.max_vscl_ratio = 6,
	.max_hscl_taps = 8,
	.max_vscl_taps = 8,
	.dpte_buffer_size_in_pte_reqs_luma = 68,/*changed from 64,*/
	.dpte_buffer_size_in_pte_reqs_chroma = 36,/*changed from 34*/
	/*.dcc_meta_buffer_size_bytes = 6272; new to dml2*/
	.dispclk_ramp_margin_percent = 1.11,/*delta from 1*/
	/*.dppclk_delay_subtotal = 47;
	.dppclk_delay_scl = 50;
	.dppclk_delay_scl_lb_only = 16;
	.dppclk_delay_cnvc_formatter = 28;
	.dppclk_delay_cnvc_cursor = 6;
	.dispclk_delay_subtotal = 125;*/ /*new to dml2*/
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
	.dppclk_delay_subtotal = 47, /* changed from 46,*/
	.dppclk_delay_scl = 50,
	.dppclk_delay_scl_lb_only = 16,
	.dppclk_delay_cnvc_formatter = 28,/*changed from 27,*/
	.dppclk_delay_cnvc_cursor = 6,
	.dispclk_delay_subtotal = 125, /*changed from 119,*/
	.dynamic_metadata_vm_enabled = false,
	.odm_combine_4to1_supported = false,
	.dcc_supported = true,
//	.config_return_buffer_segment_size_in_kbytes = 64;/*required, hard coded in dml2_translate_ip_params*/

};

struct _vcs_dpi_soc_bounding_box_st dcn3_5_soc = {
		/*TODO: correct dispclk/dppclk voltage level determination*/
	.clock_limits = {
		{
			.state = 0,
			.dispclk_mhz = 1200.0,
			.dppclk_mhz = 1200.0,
			.phyclk_mhz = 600.0,
			.phyclk_d18_mhz = 667.0,
			.dscclk_mhz = 186.0,
			.dtbclk_mhz = 625.0,
		},
		{
			.state = 1,
			.dispclk_mhz = 1200.0,
			.dppclk_mhz = 1200.0,
			.phyclk_mhz = 810.0,
			.phyclk_d18_mhz = 667.0,
			.dscclk_mhz = 209.0,
			.dtbclk_mhz = 625.0,
		},
		{
			.state = 2,
			.dispclk_mhz = 1200.0,
			.dppclk_mhz = 1200.0,
			.phyclk_mhz = 810.0,
			.phyclk_d18_mhz = 667.0,
			.dscclk_mhz = 209.0,
			.dtbclk_mhz = 625.0,
		},
		{
			.state = 3,
			.dispclk_mhz = 1200.0,
			.dppclk_mhz = 1200.0,
			.phyclk_mhz = 810.0,
			.phyclk_d18_mhz = 667.0,
			.dscclk_mhz = 371.0,
			.dtbclk_mhz = 625.0,
		},
		{
			.state = 4,
			.dispclk_mhz = 1200.0,
			.dppclk_mhz = 1200.0,
			.phyclk_mhz = 810.0,
			.phyclk_d18_mhz = 667.0,
			.dscclk_mhz = 417.0,
			.dtbclk_mhz = 625.0,
		},
	},
	.num_states = 5,
	.sr_exit_time_us = 9.0,
	.sr_enter_plus_exit_time_us = 11.0,
	.sr_exit_z8_time_us = 50.0, /*changed from 442.0*/
	.sr_enter_plus_exit_z8_time_us = 50.0,/*changed from 560.0*/
	.fclk_change_latency_us = 20.0,
	.usr_retraining_latency_us = 2,
	.writeback_latency_us = 12.0,

	.dram_channel_width_bytes = 4,/*not exist in dml2*/
	.round_trip_ping_latency_dcfclk_cycles = 106,/*not exist in dml2*/
	.urgent_latency_pixel_data_only_us = 4.0,
	.urgent_latency_pixel_mixed_with_vm_data_us = 4.0,
	.urgent_latency_vm_data_only_us = 4.0,
	.dram_clock_change_latency_us = 11.72,
	.urgent_out_of_order_return_per_channel_pixel_only_bytes = 4096,
	.urgent_out_of_order_return_per_channel_pixel_and_vm_bytes = 4096,
	.urgent_out_of_order_return_per_channel_vm_only_bytes = 4096,

	.pct_ideal_sdp_bw_after_urgent = 80.0,
	.pct_ideal_fabric_bw_after_urgent = 80.0, /*new to dml2*/
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
	.do_urgent_latency_adjustment = 0,
	.urgent_latency_adjustment_fabric_clock_component_us = 0,
	.urgent_latency_adjustment_fabric_clock_reference_mhz = 0,
};

void dcn35_build_wm_range_table_fpu(struct clk_mgr *clk_mgr)
{
	//TODO
}


/*
 * dcn35_update_bw_bounding_box
 *
 * This would override some dcn3_5 ip_or_soc initial parameters hardcoded from
 * spreadsheet with actual values as per dGPU SKU:
 * - with passed few options from dc->config
 * - with dentist_vco_frequency from Clk Mgr (currently hardcoded, but might
 *   need to get it from PM FW)
 * - with passed latency values (passed in ns units) in dc-> bb override for
 *   debugging purposes
 * - with passed latencies from VBIOS (in 100_ns units) if available for
 *   certain dGPU SKU
 * - with number of DRAM channels from VBIOS (which differ for certain dGPU SKU
 *   of the same ASIC)
 * - clocks levels with passed clk_table entries from Clk Mgr as reported by PM
 *   FW for different clocks (which might differ for certain dGPU SKU of the
 *   same ASIC)
 */
void dcn35_update_bw_bounding_box_fpu(struct dc *dc,
				      struct clk_bw_params *bw_params)
{
	unsigned int i, closest_clk_lvl;
	int j;
	struct clk_limit_table *clk_table = &bw_params->clk_table;
	struct _vcs_dpi_voltage_scaling_st *clock_limits =
		dc->scratch.update_bw_bounding_box.clock_limits;
	int max_dispclk_mhz = 0, max_dppclk_mhz = 0;

	dc_assert_fp_enabled();

		dcn3_5_ip.max_num_otg =
			dc->res_pool->res_cap->num_timing_generator;
		dcn3_5_ip.max_num_dpp = dc->res_pool->pipe_count;
		dcn3_5_soc.num_chans = bw_params->num_channels;

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
			for (closest_clk_lvl = 0, j = dcn3_5_soc.num_states - 1;
			     j >= 0; j--) {
				if (dcn3_5_soc.clock_limits[j].dcfclk_mhz <=
				    clk_table->entries[i].dcfclk_mhz) {
					closest_clk_lvl = j;
					break;
				}
			}
			if (clk_table->num_entries == 1) {
				/*smu gives one DPM level, let's take the highest one*/
				closest_clk_lvl = dcn3_5_soc.num_states - 1;
			}

			clock_limits[i].state = i;

			/* Clocks dependent on voltage level. */
			clock_limits[i].dcfclk_mhz = clk_table->entries[i].dcfclk_mhz;
			if (clk_table->num_entries == 1 &&
			    clock_limits[i].dcfclk_mhz <
			    dcn3_5_soc.clock_limits[closest_clk_lvl].dcfclk_mhz) {
				/*SMU fix not released yet*/
				clock_limits[i].dcfclk_mhz =
					dcn3_5_soc.clock_limits[closest_clk_lvl].dcfclk_mhz;
			}

			clock_limits[i].fabricclk_mhz =
				clk_table->entries[i].fclk_mhz;
			clock_limits[i].socclk_mhz =
				clk_table->entries[i].socclk_mhz;

			if (clk_table->entries[i].memclk_mhz &&
			    clk_table->entries[i].wck_ratio)
				clock_limits[i].dram_speed_mts =
					clk_table->entries[i].memclk_mhz * 2 *
					clk_table->entries[i].wck_ratio;

			/* Clocks independent of voltage level. */
			clock_limits[i].dispclk_mhz = max_dispclk_mhz ?
				max_dispclk_mhz :
				dcn3_5_soc.clock_limits[closest_clk_lvl].dispclk_mhz;

			clock_limits[i].dppclk_mhz = max_dppclk_mhz ?
				max_dppclk_mhz :
				dcn3_5_soc.clock_limits[closest_clk_lvl].dppclk_mhz;

			clock_limits[i].dram_bw_per_chan_gbps =
				dcn3_5_soc.clock_limits[closest_clk_lvl].dram_bw_per_chan_gbps;
			clock_limits[i].dscclk_mhz =
				dcn3_5_soc.clock_limits[closest_clk_lvl].dscclk_mhz;
			clock_limits[i].dtbclk_mhz =
				dcn3_5_soc.clock_limits[closest_clk_lvl].dtbclk_mhz;
			clock_limits[i].phyclk_d18_mhz =
				dcn3_5_soc.clock_limits[closest_clk_lvl].phyclk_d18_mhz;
			clock_limits[i].phyclk_mhz =
				dcn3_5_soc.clock_limits[closest_clk_lvl].phyclk_mhz;
		}

		memcpy(dcn3_5_soc.clock_limits, clock_limits,
		       sizeof(dcn3_5_soc.clock_limits));

		if (clk_table->num_entries)
			dcn3_5_soc.num_states = clk_table->num_entries;

	if (max_dispclk_mhz) {
		dcn3_5_soc.dispclk_dppclk_vco_speed_mhz = max_dispclk_mhz * 2;
		dc->dml.soc.dispclk_dppclk_vco_speed_mhz = max_dispclk_mhz * 2;
	}
	if ((int)(dcn3_5_soc.dram_clock_change_latency_us * 1000)
				!= dc->debug.dram_clock_change_latency_ns
			&& dc->debug.dram_clock_change_latency_ns) {
		dcn3_5_soc.dram_clock_change_latency_us =
			dc->debug.dram_clock_change_latency_ns / 1000.0;
	}
	/*temp till dml2 fully work without dml1*/
	dml_init_instance(&dc->dml, &dcn3_5_soc, &dcn3_5_ip,
				DML_PROJECT_DCN31);
}

static bool is_dual_plane(enum surface_pixel_format format)
{
	return format >= SURFACE_PIXEL_FORMAT_VIDEO_BEGIN ||
		format == SURFACE_PIXEL_FORMAT_GRPH_RGBE_ALPHA;
}

/*
 * micro_sec_to_vert_lines () - converts time to number of vertical lines for a given timing
 *
 * @param: num_us: number of microseconds
 * @return: number of vertical lines. If exact number of vertical lines is not found then
 *          it will round up to next number of lines to guarantee num_us
 */
static unsigned int micro_sec_to_vert_lines(unsigned int num_us, struct dc_crtc_timing *timing)
{
	unsigned int num_lines = 0;
	unsigned int lines_time_in_ns = 1000.0 *
			(((float)timing->h_total * 1000.0) /
			 ((float)timing->pix_clk_100hz / 10.0));

	num_lines = dml_ceil(1000.0 * num_us / lines_time_in_ns, 1.0);

	return num_lines;
}

static unsigned int get_vertical_back_porch(struct dc_crtc_timing *timing)
{
	unsigned int v_active = 0, v_blank = 0, v_back_porch = 0;

	v_active = timing->v_border_top + timing->v_addressable + timing->v_border_bottom;
	v_blank = timing->v_total - v_active;
	v_back_porch = v_blank - timing->v_front_porch - timing->v_sync_width;

	return v_back_porch;
}

int dcn35_populate_dml_pipes_from_context_fpu(struct dc *dc,
					      struct dc_state *context,
					      display_e2e_pipe_params_st *pipes,
					      bool fast_validate)
{
	int i, pipe_cnt;
	struct resource_context *res_ctx = &context->res_ctx;
	struct pipe_ctx *pipe;
	bool upscaled = false;
	const unsigned int max_allowed_vblank_nom = 1023;

	dcn31_populate_dml_pipes_from_context(dc, context, pipes,
					      fast_validate);

	for (i = 0, pipe_cnt = 0; i < dc->res_pool->pipe_count; i++) {
		struct dc_crtc_timing *timing;
		unsigned int num_lines = 0;
		unsigned int v_back_porch = 0;

		if (!res_ctx->pipe_ctx[i].stream)
			continue;

		pipe = &res_ctx->pipe_ctx[i];
		timing = &pipe->stream->timing;

		num_lines = micro_sec_to_vert_lines(dcn3_5_ip.VBlankNomDefaultUS, timing);
		v_back_porch  = get_vertical_back_porch(timing);

		if (pipe->stream->adjust.v_total_max ==
		    pipe->stream->adjust.v_total_min &&
		    pipe->stream->adjust.v_total_min > timing->v_total) {
			pipes[pipe_cnt].pipe.dest.vtotal =
				pipe->stream->adjust.v_total_min;
			pipes[pipe_cnt].pipe.dest.vblank_nom = timing->v_total -
				pipes[pipe_cnt].pipe.dest.vactive;
		}

		pipes[pipe_cnt].pipe.dest.vblank_nom = timing->v_total - pipes[pipe_cnt].pipe.dest.vactive;
		pipes[pipe_cnt].pipe.dest.vblank_nom = min(pipes[pipe_cnt].pipe.dest.vblank_nom, num_lines);
		// vblank_nom should not smaller than (VSync (timing->v_sync_width + v_back_porch) + 2)
		// + 2 is because
		// 1 -> VStartup_start should be 1 line before VSync
		// 1 -> always reserve 1 line between start of vblank to vstartup signal
		pipes[pipe_cnt].pipe.dest.vblank_nom =
			max(pipes[pipe_cnt].pipe.dest.vblank_nom, timing->v_sync_width + v_back_porch + 2);
		pipes[pipe_cnt].pipe.dest.vblank_nom = min(pipes[pipe_cnt].pipe.dest.vblank_nom, max_allowed_vblank_nom);

		if (pipe->plane_state &&
		    (pipe->plane_state->src_rect.height <
		     pipe->plane_state->dst_rect.height ||
		     pipe->plane_state->src_rect.width <
		     pipe->plane_state->dst_rect.width))
			upscaled = true;

		/*
		 * Immediate flip can be set dynamically after enabling the
		 * plane. We need to require support for immediate flip or
		 * underflow can be intermittently experienced depending on peak
		 * b/w requirements.
		 */
		pipes[pipe_cnt].pipe.src.immediate_flip = true;

		pipes[pipe_cnt].pipe.src.unbounded_req_mode = false;

		DC_FP_START();
		dcn31_zero_pipe_dcc_fraction(pipes, pipe_cnt);
		DC_FP_END();

		pipes[pipe_cnt].pipe.dest.vfront_porch = timing->v_front_porch;
		pipes[pipe_cnt].pipe.src.dcc_rate = 3;
		pipes[pipe_cnt].dout.dsc_input_bpc = 0;
		pipes[pipe_cnt].pipe.src.gpuvm_min_page_size_kbytes = 256;

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

	context->bw_ctx.dml.ip.det_buffer_size_kbytes = 384;/*per guide*/
	dc->config.enable_4to1MPC = false;

	if (pipe_cnt == 1 && pipe->plane_state && !dc->debug.disable_z9_mpc) {
		if (is_dual_plane(pipe->plane_state->format)
				&& pipe->plane_state->src_rect.width <= 1920 &&
				pipe->plane_state->src_rect.height <= 1080) {
			dc->config.enable_4to1MPC = true;
		} else if (!is_dual_plane(pipe->plane_state->format) &&
			   pipe->plane_state->src_rect.width <= 5120) {
			/*
			 * Limit to 5k max to avoid forced pipe split when there
			 * is not enough detile for swath
			 */
			context->bw_ctx.dml.ip.det_buffer_size_kbytes = 192;
			pipes[0].pipe.src.unbounded_req_mode = true;
		}
	} else if (context->stream_count >=
		   dc->debug.crb_alloc_policy_min_disp_count &&
		   dc->debug.crb_alloc_policy > DET_SIZE_DEFAULT) {
		context->bw_ctx.dml.ip.det_buffer_size_kbytes =
			dc->debug.crb_alloc_policy * 64;
	} else if (context->stream_count >= 3 && upscaled) {
		context->bw_ctx.dml.ip.det_buffer_size_kbytes = 192;
	}

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];

		if (!pipe->stream)
			continue;

		if (pipe->stream->signal == SIGNAL_TYPE_EDP &&
		    dc->debug.seamless_boot_odm_combine &&
		    pipe->stream->apply_seamless_boot_optimization) {

			if (pipe->stream->apply_boot_odm_mode ==
			    dm_odm_combine_policy_2to1) {
				context->bw_ctx.dml.vba.ODMCombinePolicy =
					dm_odm_combine_policy_2to1;
				break;
			}
		}
	}

	return pipe_cnt;
}
