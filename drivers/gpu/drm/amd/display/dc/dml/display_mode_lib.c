/*
 * Copyright 2017 Advanced Micro Devices, Inc.
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

#include "display_mode_lib.h"
#include "dc_features.h"

static void set_soc_bounding_box(struct _vcs_dpi_soc_bounding_box_st *soc, enum dml_project project)
{
	if (project == DML_PROJECT_RAVEN1) {
		soc->sr_exit_time_us = 9.0;
		soc->sr_enter_plus_exit_time_us = 11.0;
		soc->urgent_latency_us = 4.0;
		soc->writeback_latency_us = 12.0;
		soc->ideal_dram_bw_after_urgent_percent = 80.0;
		soc->max_request_size_bytes = 256;

		soc->vmin.dcfclk_mhz = 300.0;
		soc->vmin.dispclk_mhz = 608.0;
		soc->vmin.dppclk_mhz = 435.0;
		soc->vmin.dram_bw_per_chan_gbps = 12.8;
		soc->vmin.phyclk_mhz = 540.0;
		soc->vmin.socclk_mhz = 208.0;

		soc->vmid.dcfclk_mhz = 600.0;
		soc->vmid.dispclk_mhz = 661.0;
		soc->vmid.dppclk_mhz = 661.0;
		soc->vmid.dram_bw_per_chan_gbps = 12.8;
		soc->vmid.phyclk_mhz = 540.0;
		soc->vmid.socclk_mhz = 208.0;

		soc->vnom.dcfclk_mhz = 600.0;
		soc->vnom.dispclk_mhz = 661.0;
		soc->vnom.dppclk_mhz = 661.0;
		soc->vnom.dram_bw_per_chan_gbps = 38.4;
		soc->vnom.phyclk_mhz = 810;
		soc->vnom.socclk_mhz = 208.0;

		soc->vmax.dcfclk_mhz = 600.0;
		soc->vmax.dispclk_mhz = 1086.0;
		soc->vmax.dppclk_mhz = 661.0;
		soc->vmax.dram_bw_per_chan_gbps = 38.4;
		soc->vmax.phyclk_mhz = 810.0;
		soc->vmax.socclk_mhz = 208.0;

		soc->downspread_percent = 0.5;
		soc->dram_page_open_time_ns = 50.0;
		soc->dram_rw_turnaround_time_ns = 17.5;
		soc->dram_return_buffer_per_channel_bytes = 8192;
		soc->round_trip_ping_latency_dcfclk_cycles = 128;
		soc->urgent_out_of_order_return_per_channel_bytes = 256;
		soc->channel_interleave_bytes = 256;
		soc->num_banks = 8;
		soc->num_chans = 2;
		soc->vmm_page_size_bytes = 4096;
		soc->dram_clock_change_latency_us = 17.0;
		soc->writeback_dram_clock_change_latency_us = 23.0;
		soc->return_bus_width_bytes = 64;
	} else {
		BREAK_TO_DEBUGGER(); /* Invalid Project Specified */
	}
}

static void set_ip_params(struct _vcs_dpi_ip_params_st *ip, enum dml_project project)
{
	if (project == DML_PROJECT_RAVEN1) {
		ip->rob_buffer_size_kbytes = 64;
		ip->det_buffer_size_kbytes = 164;
		ip->dpte_buffer_size_in_pte_reqs = 42;
		ip->dpp_output_buffer_pixels = 2560;
		ip->opp_output_buffer_lines = 1;
		ip->pixel_chunk_size_kbytes = 8;
		ip->pte_enable = 1;
		ip->pte_chunk_size_kbytes = 2;
		ip->meta_chunk_size_kbytes = 2;
		ip->writeback_chunk_size_kbytes = 2;
		ip->line_buffer_size_bits = 589824;
		ip->max_line_buffer_lines = 12;
		ip->IsLineBufferBppFixed = 0;
		ip->LineBufferFixedBpp = -1;
		ip->writeback_luma_buffer_size_kbytes = 12;
		ip->writeback_chroma_buffer_size_kbytes = 8;
		ip->max_num_dpp = 4;
		ip->max_num_wb = 2;
		ip->max_dchub_pscl_bw_pix_per_clk = 4;
		ip->max_pscl_lb_bw_pix_per_clk = 2;
		ip->max_lb_vscl_bw_pix_per_clk = 4;
		ip->max_vscl_hscl_bw_pix_per_clk = 4;
		ip->max_hscl_ratio = 4;
		ip->max_vscl_ratio = 4;
		ip->hscl_mults = 4;
		ip->vscl_mults = 4;
		ip->max_hscl_taps = 8;
		ip->max_vscl_taps = 8;
		ip->dispclk_ramp_margin_percent = 1;
		ip->underscan_factor = 1.10;
		ip->min_vblank_lines = 14;
		ip->dppclk_delay_subtotal = 90;
		ip->dispclk_delay_subtotal = 42;
		ip->dcfclk_cstate_latency = 10;
		ip->max_inter_dcn_tile_repeaters = 8;
		ip->can_vstartup_lines_exceed_vsync_plus_back_porch_lines_minus_one = 0;
		ip->bug_forcing_LC_req_same_size_fixed = 0;
	} else {
		BREAK_TO_DEBUGGER(); /* Invalid Project Specified */
	}
}

void dml_init_instance(struct display_mode_lib *lib, enum dml_project project)
{
	if (lib->project != project) {
		set_soc_bounding_box(&lib->soc, project);
		set_ip_params(&lib->ip, project);
		lib->project = project;
	}
}

