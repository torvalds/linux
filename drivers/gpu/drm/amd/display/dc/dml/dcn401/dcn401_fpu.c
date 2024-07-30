// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#include "dcn401_fpu.h"
#include "dcn401/dcn401_resource.h"
// We need this includes for WATERMARKS_* defines
#include "clk_mgr/dcn401/dcn401_smu14_driver_if.h"
#include "link.h"

#define DC_LOGGER_INIT(logger)

void dcn401_build_wm_range_table_fpu(struct clk_mgr *clk_mgr)
{
	/* defaults */
	double pstate_latency_us = clk_mgr->ctx->dc->dml.soc.dram_clock_change_latency_us;
	double fclk_change_latency_us = clk_mgr->ctx->dc->dml.soc.fclk_change_latency_us;
	double sr_exit_time_us = clk_mgr->ctx->dc->dml.soc.sr_exit_time_us;
	double sr_enter_plus_exit_time_us = clk_mgr->ctx->dc->dml.soc.sr_enter_plus_exit_time_us;
	/* For min clocks use as reported by PM FW and report those as min */
	uint16_t min_uclk_mhz			= clk_mgr->bw_params->clk_table.entries[0].memclk_mhz;
	uint16_t min_dcfclk_mhz			= clk_mgr->bw_params->clk_table.entries[0].dcfclk_mhz;
	uint16_t setb_min_uclk_mhz		= min_uclk_mhz;
	uint16_t dcfclk_mhz_for_the_second_state = clk_mgr->ctx->dc->dml.soc.clock_limits[2].dcfclk_mhz;

	dc_assert_fp_enabled();

	/* For Set B ranges use min clocks state 2 when available, and report those to PM FW */
	if (dcfclk_mhz_for_the_second_state)
		clk_mgr->bw_params->wm_table.nv_entries[WM_B].pmfw_breakdown.min_dcfclk = dcfclk_mhz_for_the_second_state;
	else
		clk_mgr->bw_params->wm_table.nv_entries[WM_B].pmfw_breakdown.min_dcfclk = clk_mgr->bw_params->clk_table.entries[0].dcfclk_mhz;

	if (clk_mgr->bw_params->clk_table.entries[2].memclk_mhz)
		setb_min_uclk_mhz = clk_mgr->bw_params->clk_table.entries[2].memclk_mhz;

	/* Set A - Normal - default values */
	clk_mgr->bw_params->wm_table.nv_entries[WM_A].valid = true;
	clk_mgr->bw_params->wm_table.nv_entries[WM_A].dml_input.pstate_latency_us = pstate_latency_us;
	clk_mgr->bw_params->wm_table.nv_entries[WM_A].dml_input.fclk_change_latency_us = fclk_change_latency_us;
	clk_mgr->bw_params->wm_table.nv_entries[WM_A].dml_input.sr_exit_time_us = sr_exit_time_us;
	clk_mgr->bw_params->wm_table.nv_entries[WM_A].dml_input.sr_enter_plus_exit_time_us = sr_enter_plus_exit_time_us;
	clk_mgr->bw_params->wm_table.nv_entries[WM_A].pmfw_breakdown.wm_type = WATERMARKS_CLOCK_RANGE;
	clk_mgr->bw_params->wm_table.nv_entries[WM_A].pmfw_breakdown.min_dcfclk = min_dcfclk_mhz;
	clk_mgr->bw_params->wm_table.nv_entries[WM_A].pmfw_breakdown.max_dcfclk = 0xFFFF;
	clk_mgr->bw_params->wm_table.nv_entries[WM_A].pmfw_breakdown.min_uclk = min_uclk_mhz;
	clk_mgr->bw_params->wm_table.nv_entries[WM_A].pmfw_breakdown.max_uclk = 0xFFFF;

	/* Set B - Performance - higher clocks, using DPM[2] DCFCLK and UCLK */
	clk_mgr->bw_params->wm_table.nv_entries[WM_B].valid = true;
	clk_mgr->bw_params->wm_table.nv_entries[WM_B].dml_input.pstate_latency_us = pstate_latency_us;
	clk_mgr->bw_params->wm_table.nv_entries[WM_B].dml_input.fclk_change_latency_us = fclk_change_latency_us;
	clk_mgr->bw_params->wm_table.nv_entries[WM_B].dml_input.sr_exit_time_us = sr_exit_time_us;
	clk_mgr->bw_params->wm_table.nv_entries[WM_B].dml_input.sr_enter_plus_exit_time_us = sr_enter_plus_exit_time_us;
	clk_mgr->bw_params->wm_table.nv_entries[WM_B].pmfw_breakdown.wm_type = WATERMARKS_CLOCK_RANGE;
	clk_mgr->bw_params->wm_table.nv_entries[WM_B].pmfw_breakdown.max_dcfclk = 0xFFFF;
	clk_mgr->bw_params->wm_table.nv_entries[WM_B].pmfw_breakdown.min_uclk = setb_min_uclk_mhz;
	clk_mgr->bw_params->wm_table.nv_entries[WM_B].pmfw_breakdown.max_uclk = 0xFFFF;

	/* Set C - Dummy P-State - P-State latency set to "dummy p-state" value */
	/* 'DalDummyClockChangeLatencyNs' registry key option set to 0x7FFFFFFF can be used to disable Set C for dummy p-state */
	if (clk_mgr->ctx->dc->bb_overrides.dummy_clock_change_latency_ns != 0x7FFFFFFF) {
		clk_mgr->bw_params->wm_table.nv_entries[WM_C].valid = true;
		clk_mgr->bw_params->wm_table.nv_entries[WM_C].dml_input.pstate_latency_us = 50;
		clk_mgr->bw_params->wm_table.nv_entries[WM_C].dml_input.fclk_change_latency_us = fclk_change_latency_us;
		clk_mgr->bw_params->wm_table.nv_entries[WM_C].dml_input.sr_exit_time_us = sr_exit_time_us;
		clk_mgr->bw_params->wm_table.nv_entries[WM_C].dml_input.sr_enter_plus_exit_time_us = sr_enter_plus_exit_time_us;
		clk_mgr->bw_params->wm_table.nv_entries[WM_C].pmfw_breakdown.wm_type = WATERMARKS_DUMMY_PSTATE;
		clk_mgr->bw_params->wm_table.nv_entries[WM_C].pmfw_breakdown.min_dcfclk = min_dcfclk_mhz;
		clk_mgr->bw_params->wm_table.nv_entries[WM_C].pmfw_breakdown.max_dcfclk = 0xFFFF;
		clk_mgr->bw_params->wm_table.nv_entries[WM_C].pmfw_breakdown.min_uclk = min_uclk_mhz;
		clk_mgr->bw_params->wm_table.nv_entries[WM_C].pmfw_breakdown.max_uclk = 0xFFFF;
		clk_mgr->bw_params->dummy_pstate_table[0].dram_speed_mts = clk_mgr->bw_params->clk_table.entries[0].memclk_mhz * 16;
		clk_mgr->bw_params->dummy_pstate_table[0].dummy_pstate_latency_us = 50;
		clk_mgr->bw_params->dummy_pstate_table[1].dram_speed_mts = clk_mgr->bw_params->clk_table.entries[1].memclk_mhz * 16;
		clk_mgr->bw_params->dummy_pstate_table[1].dummy_pstate_latency_us = 9;
		clk_mgr->bw_params->dummy_pstate_table[2].dram_speed_mts = clk_mgr->bw_params->clk_table.entries[2].memclk_mhz * 16;
		clk_mgr->bw_params->dummy_pstate_table[2].dummy_pstate_latency_us = 8;
		clk_mgr->bw_params->dummy_pstate_table[3].dram_speed_mts = clk_mgr->bw_params->clk_table.entries[3].memclk_mhz * 16;
		clk_mgr->bw_params->dummy_pstate_table[3].dummy_pstate_latency_us = 5;
	}
	/* Set D - MALL - SR enter and exit time specific to MALL, TBD after bringup or later phase for now use DRAM values / 2 */
	/* For MALL DRAM clock change latency is N/A, for watermak calculations use lowest value dummy P state latency */
	clk_mgr->bw_params->wm_table.nv_entries[WM_D].valid = true;
	clk_mgr->bw_params->wm_table.nv_entries[WM_D].dml_input.pstate_latency_us = clk_mgr->bw_params->dummy_pstate_table[3].dummy_pstate_latency_us;
	clk_mgr->bw_params->wm_table.nv_entries[WM_D].dml_input.fclk_change_latency_us = fclk_change_latency_us;
	clk_mgr->bw_params->wm_table.nv_entries[WM_D].dml_input.sr_exit_time_us = sr_exit_time_us / 2; // TBD
	clk_mgr->bw_params->wm_table.nv_entries[WM_D].dml_input.sr_enter_plus_exit_time_us = sr_enter_plus_exit_time_us / 2; // TBD
	clk_mgr->bw_params->wm_table.nv_entries[WM_D].pmfw_breakdown.wm_type = WATERMARKS_MALL;
	clk_mgr->bw_params->wm_table.nv_entries[WM_D].pmfw_breakdown.min_dcfclk = min_dcfclk_mhz;
	clk_mgr->bw_params->wm_table.nv_entries[WM_D].pmfw_breakdown.max_dcfclk = 0xFFFF;
	clk_mgr->bw_params->wm_table.nv_entries[WM_D].pmfw_breakdown.min_uclk = min_uclk_mhz;
	clk_mgr->bw_params->wm_table.nv_entries[WM_D].pmfw_breakdown.max_uclk = 0xFFFF;
}

/*
 * dcn401_update_bw_bounding_box
 *
 * This would override some dcn4_01 ip_or_soc initial parameters hardcoded from
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
void dcn401_update_bw_bounding_box_fpu(struct dc *dc, struct clk_bw_params *bw_params)
{
	dc_assert_fp_enabled();

	/* Override from passed dc->bb_overrides if available*/
	if (dc->bb_overrides.sr_exit_time_ns)
		dc->dml2_options.bbox_overrides.sr_exit_latency_us =
				dc->bb_overrides.sr_exit_time_ns / 1000.0;

	if (dc->bb_overrides.sr_enter_plus_exit_time_ns)
		dc->dml2_options.bbox_overrides.sr_enter_plus_exit_latency_us =
			dc->bb_overrides.sr_enter_plus_exit_time_ns / 1000.0;

	if (dc->bb_overrides.urgent_latency_ns)
		dc->dml2_options.bbox_overrides.urgent_latency_us =
				dc->bb_overrides.urgent_latency_ns / 1000.0;

	if (dc->bb_overrides.dram_clock_change_latency_ns)
		dc->dml2_options.bbox_overrides.dram_clock_change_latency_us =
			dc->bb_overrides.dram_clock_change_latency_ns / 1000.0;

	if (dc->bb_overrides.fclk_clock_change_latency_ns)
		dc->dml2_options.bbox_overrides.fclk_change_latency_us =
			dc->bb_overrides.fclk_clock_change_latency_ns / 1000;

	/* Override from VBIOS if VBIOS bb_info available */
	if (dc->ctx->dc_bios->funcs->get_soc_bb_info) {
		struct bp_soc_bb_info bb_info = {0};
		if (dc->ctx->dc_bios->funcs->get_soc_bb_info(dc->ctx->dc_bios, &bb_info) == BP_RESULT_OK) {
			if (bb_info.dram_clock_change_latency_100ns > 0)
				dc->dml2_options.bbox_overrides.dram_clock_change_latency_us =
					bb_info.dram_clock_change_latency_100ns * 10;

			if (bb_info.dram_sr_enter_exit_latency_100ns > 0)
				dc->dml2_options.bbox_overrides.sr_enter_plus_exit_latency_us =
					bb_info.dram_sr_enter_exit_latency_100ns * 10;

			if (bb_info.dram_sr_exit_latency_100ns > 0)
				dc->dml2_options.bbox_overrides.sr_exit_latency_us =
					bb_info.dram_sr_exit_latency_100ns * 10;
		}
	}

	/* Override from VBIOS for num_chan */
	if (dc->ctx->dc_bios->vram_info.num_chans) {
		dc->dml2_options.bbox_overrides.dram_num_chan =
				dc->ctx->dc_bios->vram_info.num_chans;

	}

	if (dc->ctx->dc_bios->vram_info.dram_channel_width_bytes)
		dc->dml2_options.bbox_overrides.dram_chanel_width_bytes =
				dc->ctx->dc_bios->vram_info.dram_channel_width_bytes;

	dc->dml2_options.bbox_overrides.disp_pll_vco_speed_mhz = dc->clk_mgr->dentist_vco_freq_khz / 1000.0;
	dc->dml2_options.bbox_overrides.xtalclk_mhz = dc->ctx->dc_bios->fw_info.pll_info.crystal_frequency / 1000.0;
	dc->dml2_options.bbox_overrides.dchub_refclk_mhz = dc->res_pool->ref_clocks.dchub_ref_clock_inKhz / 1000.0;
	dc->dml2_options.bbox_overrides.dprefclk_mhz = dc->clk_mgr->dprefclk_khz / 1000.0;

	if (dc->clk_mgr->bw_params->clk_table.num_entries > 1) {
		unsigned int i = 0;

		dc->dml2_options.bbox_overrides.clks_table.num_states = dc->clk_mgr->bw_params->clk_table.num_entries;

		dc->dml2_options.bbox_overrides.clks_table.num_entries_per_clk.num_dcfclk_levels =
			dc->clk_mgr->bw_params->clk_table.num_entries_per_clk.num_dcfclk_levels;

		dc->dml2_options.bbox_overrides.clks_table.num_entries_per_clk.num_fclk_levels =
			dc->clk_mgr->bw_params->clk_table.num_entries_per_clk.num_fclk_levels;

		dc->dml2_options.bbox_overrides.clks_table.num_entries_per_clk.num_memclk_levels =
			dc->clk_mgr->bw_params->clk_table.num_entries_per_clk.num_memclk_levels;

		dc->dml2_options.bbox_overrides.clks_table.num_entries_per_clk.num_socclk_levels =
			dc->clk_mgr->bw_params->clk_table.num_entries_per_clk.num_socclk_levels;

		dc->dml2_options.bbox_overrides.clks_table.num_entries_per_clk.num_dtbclk_levels =
			dc->clk_mgr->bw_params->clk_table.num_entries_per_clk.num_dtbclk_levels;

		dc->dml2_options.bbox_overrides.clks_table.num_entries_per_clk.num_dispclk_levels =
			dc->clk_mgr->bw_params->clk_table.num_entries_per_clk.num_dispclk_levels;

		dc->dml2_options.bbox_overrides.clks_table.num_entries_per_clk.num_dppclk_levels =
			dc->clk_mgr->bw_params->clk_table.num_entries_per_clk.num_dppclk_levels;

		for (i = 0; i < dc->clk_mgr->bw_params->clk_table.num_entries_per_clk.num_dcfclk_levels; i++) {
			if (dc->clk_mgr->bw_params->clk_table.entries[i].dcfclk_mhz)
				dc->dml2_options.bbox_overrides.clks_table.clk_entries[i].dcfclk_mhz =
					dc->clk_mgr->bw_params->clk_table.entries[i].dcfclk_mhz;
		}

		for (i = 0; i < dc->clk_mgr->bw_params->clk_table.num_entries_per_clk.num_fclk_levels; i++) {
			if (dc->clk_mgr->bw_params->clk_table.entries[i].fclk_mhz)
				dc->dml2_options.bbox_overrides.clks_table.clk_entries[i].fclk_mhz =
					dc->clk_mgr->bw_params->clk_table.entries[i].fclk_mhz;
		}

		for (i = 0; i < dc->clk_mgr->bw_params->clk_table.num_entries_per_clk.num_memclk_levels; i++) {
			if (dc->clk_mgr->bw_params->clk_table.entries[i].memclk_mhz)
				dc->dml2_options.bbox_overrides.clks_table.clk_entries[i].memclk_mhz =
					dc->clk_mgr->bw_params->clk_table.entries[i].memclk_mhz;
		}

		for (i = 0; i < dc->clk_mgr->bw_params->clk_table.num_entries_per_clk.num_socclk_levels; i++) {
			if (dc->clk_mgr->bw_params->clk_table.entries[i].socclk_mhz)
				dc->dml2_options.bbox_overrides.clks_table.clk_entries[i].socclk_mhz =
					dc->clk_mgr->bw_params->clk_table.entries[i].socclk_mhz;
		}

		for (i = 0; i < dc->clk_mgr->bw_params->clk_table.num_entries_per_clk.num_dtbclk_levels; i++) {
			if (dc->clk_mgr->bw_params->clk_table.entries[i].dtbclk_mhz)
				dc->dml2_options.bbox_overrides.clks_table.clk_entries[i].dtbclk_mhz =
					dc->clk_mgr->bw_params->clk_table.entries[i].dtbclk_mhz;
		}

		for (i = 0; i < dc->clk_mgr->bw_params->clk_table.num_entries_per_clk.num_dispclk_levels; i++) {
			if (dc->clk_mgr->bw_params->clk_table.entries[i].dispclk_mhz) {
				dc->dml2_options.bbox_overrides.clks_table.clk_entries[i].dispclk_mhz =
					dc->clk_mgr->bw_params->clk_table.entries[i].dispclk_mhz;
				dc->dml2_options.bbox_overrides.clks_table.clk_entries[i].dppclk_mhz =
					dc->clk_mgr->bw_params->clk_table.entries[i].dispclk_mhz;
			}
		}
	}
}

