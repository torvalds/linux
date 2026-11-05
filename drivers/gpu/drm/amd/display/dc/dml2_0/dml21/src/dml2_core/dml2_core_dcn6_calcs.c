// SPDX-License-Identifier: MIT
//
// Copyright 2026 Advanced Micro Devices, Inc.

#include "dml2_core_dcn6_calcs.h"
#include "dml2_core_dcn6_calcs_dchub.h"
#include "dml2_core_dcn5_calcs_dchub.h"
#include "dml2_core_dcn5_calcs_display_pipe.h"

static const struct dml2_core_dcn6_calcs dcn6_calcs_funcs = {
	.calculate_max_vstartup = dcn6_calculate_max_vstartup,
	.calculate_alternate_params = dcn6_calculate_alternate_params,
	.calculate_alternate_svp_lines = dcn6_calculate_alternate_svp_lines,
	.calculate_flip_schedule = dcn6_calculate_flip_schedule,
	.get_pipe_regs = dcn6_get_pipe_regs,
	.calculate_watermarks_and_dram_speed_change_support = dcn6_calculate_watermarks_and_dram_speed_change_support,
	.calculate_stutter_efficiency = dcn6_calculate_stutter_efficiency,
	.get_watermarks = dcn6_get_watermarks,
	.calculate_excess_vactive_bandwidth_required = dcn6_calculate_excess_vactive_bandwidth_required,
	.calculate_pstate_schedule_windows = dcn6_calculate_pstate_schedule_windows,
	.calculate_pstate_schedule_admissibility = dcn6_calculate_pstate_schedule_admissibility,

	.calculate_max_det_and_min_compressed_buffer_size = dcn5_calculate_max_det_and_min_compressed_buffer_size,
	.adjust_pixel_clock_for_progressive_to_interlace_unit = dcn5_adjust_pixel_clock_for_progressive_to_interlace_unit,
	.calculate_byte_per_pixel_and_block_sizes = dcn5_calculate_byte_per_pixel_and_block_sizes,
	.calculate_single_pipe_dppclk_and_scl_throughput = dcn5_calculate_single_pipe_dppclk_and_scl_throughput,
	.calculate_swath_and_det_configuration = dcn5_calculate_swath_and_det_configuration,
	.calculate_output_link = dcn5_calculate_output_link,
	.calculate_odm_mode = dcn5_calculate_odm_mode,
	.calculate_write_back_dispclk = dcn5_calculate_write_back_dispclk,
	.calculate_required_dtbclk = dcn5_calculate_required_dtbclk,
	.calculate_dsc_delay_requirement = dcn5_calculate_dsc_delay_requirement,
	.calculate_vm_row_and_swath = dcn5_calculate_vm_row_and_swath,
	.calculate_bytes_to_fetch_required_to_hide_latency = dcn5_calculate_bytes_to_fetch_required_to_hide_latency,
	.calculate_cursor_req_attributes = dcn5_calculate_cursor_req_attributes,
	.calculate_cursor_urgent_burst_factor = dcn5_calculate_cursor_urgent_burst_factor,
	.calculate_urgent_burst_factor = dcn5_calculate_urgent_burst_factor,
	.calculate_dcfclk_deep_sleep = dcn5_calculate_dcfclk_deep_sleep,
	.calculate_write_back_delay = dcn5_calculate_write_back_delay,
	.calculate_mcache_setting = dcn5_calculate_mcache_setting,
	.calculate_avg_bandwidth_required = dcn5_calculate_avg_bandwidth_required,
	.calculate_hostvm_inefficiency_factor = dcn5_calculate_hostvm_inefficiency_factor,
	.calculate_tdlut_setting = dcn5_calculate_tdlut_setting,
	.calculate_extra_latency = dcn5_calculate_extra_latency,
	.calculate_t_wait = dcn5_calculate_t_wait,
	.calculate_prefetch_schedule = dcn5_calculate_prefetch_schedule,
	.calculate_peak_bandwidth_required = dcn5_calculate_peak_bandwidth_required,
	.calculate_vactive_det_fill_latency = dcn5_calculate_vactive_det_fill_latency,
	.calculate_dcc_configuration = dcn5_calculate_dcc_configuration,
	.calculate_pixel_delivery_times = dcn5_calculate_pixel_delivery_times,
	.calculate_meta_and_pte_times = dcn5_calculate_meta_and_pte_times,
	.calculate_vm_group_and_request_times = dcn5_calculate_vm_group_and_request_times,
	.calculate_pstate_keepout_dst_lines = dcn5_calculate_pstate_keepout_dst_lines,
	.get_arb_params = dcn5_get_arb_params,
};

/**
 * dml2_core_dcn6_calcs_init() - Register the DCN6 calcs function pointer table.
 * @calcs: calcs union to populate.
 *
 * Return: void
 */
void dml2_core_dcn6_calcs_init(union dml2_core_calcs *calcs)
{
	calcs->dcn6 = &dcn6_calcs_funcs;
}
