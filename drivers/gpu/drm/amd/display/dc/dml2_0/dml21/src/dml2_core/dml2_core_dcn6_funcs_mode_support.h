// SPDX-License-Identifier: MIT
//
// Copyright 2025 Advanced Micro Devices, Inc.

#ifndef __DML2_CORE_DCN6_FUNCS_MODE_SUPPORT_H__
#define __DML2_CORE_DCN6_FUNCS_MODE_SUPPORT_H__
#include "dml2_internal_shared_types.h"
enum dml2_status dml2_core_dcn6_funcs_validate_solution(struct dml2_core_instance *core,
		const struct dml2_display_solution *solution,
		struct dml2_validation_result *result);

void dcn6_ms_initialize_from_solution(struct dml2_core_internal_mode_support *outputs,
		const struct dml2_display_solution *solution,
		const struct dml2_utm_soc_bb *utm_soc_bb);

// Exposed for Higher DCN versions
void dcn6_ms_populate_mode_support_result(
	const struct dml2_core_calculate_ms_context *ctx,
	const struct dml2_core_internal_mode_support *states,
	struct dml2_core_mode_support_result *result);

void dcn6_ms_build_calculate_ms_context(struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_instance *core,
		const struct dml2_display_solution *solution);

void dcn6_ms_check_input_sanity(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

bool dcn6_ms_check_scaler_support(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

bool dcn6_ms_check_source_format_and_scan_direction(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

void dcn6_ms_calculate_byte_per_pixel_and_block_sizes(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

void dcn6_ms_calculate_read_bandwidth(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

void dcn6_ms_calculate_writeback_bandwidth(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

bool dcn6_ms_check_writeback_bandwidth_latency_support(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

bool dcn6_ms_check_writeback_scale_ratio_and_taps_support(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

void dcn6_ms_calculate_single_pipe_dppclk_and_pscl_factor(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

void dcn6_ms_calculate_max_swath_widths(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

bool dcn6_ms_check_cursor_support(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

bool dcn6_ms_check_surface_alginment_requirements(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

void dcn6_ms_calculate_effective_pixel_clock(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

void dcn6_ms_calculate_estimated_num_of_dsc_slices(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

void dcn6_ms_calculate_desired_output_bpp(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

void dcn6_ms_calculate_output_link(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

void dcn6_ms_calculate_odm_mode(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

void dcn6_ms_calculate_num_of_dsc_slices(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

bool dcn6_ms_check_num_of_dsc_slices_support(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

void dcn6_ms_calculate_max_det_and_min_compressed_buffer_size(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

bool dcn6_ms_check_total_available_pipes_support(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

bool dcn6_ms_check_total_available_TDLUT_33cube_support(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

void dcn6_ms_calculate_total_num_of_single_dpp_surfaces(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

void dcn6_ms_calculate_dispclk_and_dppclk_required(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

bool dcn6_ms_check_dispclk_and_dppclk_support(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

void dcn6_ms_calculate_dtbclk_required(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

bool dcn6_ms_check_dtbclk_support(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

bool dcn6_ms_check_otg_count_support(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

bool dcn6_ms_check_hpo_frl_encoder_count_support(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

bool dcn6_ms_check_hpo_dp_encoder_count_support(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

bool dcn6_ms_check_writeback_count_support(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

bool dcn6_ms_check_link_bandwidth_support(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

void dcn6_ms_check_misc_link_supports(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

void dcn6_ms_calculate_dscclk_required(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

bool dcn6_ms_check_dscclk_support(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

void dcn6_ms_check_dsc_engine_supports(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

void dcn6_ms_calculate_dsc_delay(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

void dcn6_ms_calculate_total_num_of_dcc_active_dpp(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

void dcn6_ms_calculate_det_buffer_time_value_urgent_burst_factor_and_urgent_latency_hiding(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

void dcn6_ms_calculate_min_dcfclk_deepsleep_clock(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

void dcn6_ms_calculate_writeback_delay(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

void dcn6_ms_calculate_alternate_svp_lines(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

void dcn6_ms_calculate_max_vstartup(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

void dcn6_ms_calculate_mcache_setting(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

void dcn6_ms_calculate_avg_bandwidth_and_dcfclk_lb_required(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

void dcn6_ms_check_average_latency_supports(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

void dcn6_ms_calculate_t_calc(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

void dcn6_ms_calculate_hostvm_inefficiency_factor(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

void dcn6_ms_calculate_3dlut_settings(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

void dcn6_ms_calculate_urgent_latency(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

bool dcn6_ms_check_urgent_latency_hiding_support(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

bool dcn6_ms_check_dynamic_metadata_support(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

bool dcn6_ms_check_prefetch_support(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

bool dcn6_ms_check_v_ratio_in_prefetch_support(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

void dcn6_ms_calculate_urgent_burst_factor_for_prefetch(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

bool dcn6_ms_check_final_prefetch_support(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

void dcn6_ms_calculate_flip_schedule(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

bool dcn6_ms_check_immediate_flip_support(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

void dcn6_ms_calculate_bandwidth_upper_bound(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

bool dcn6_ms_check_qos_bandwidth_support(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

bool dcn6_ms_check_dcfclk_support(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

void dcn6_ms_calculate_watermarks(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

bool dcn6_ms_check_reordering_support(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

void dcn6_ms_calculate_vactive_det_fill_latency(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

void dcn6_ms_calculate_alternate_params(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

void dcn6_ms_check_alternate_channel_size_support(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

void dcn6_ms_calculate_pstate_schedule_windows(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

bool dcn6_ms_check_pstate_schedule_admissibility(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

bool dcn6_ms_check_mode_support(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states,
		enum dml2_status status);

void dcn6_ms_populate_informative(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

void dcn6_ms_calculate_vm_row_and_swath_and_calculate_dcc_meta_cache_requirements(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

bool dcn6_ms_check_pte_buffer_size_support(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

bool dcn6_ms_check_dcc_meta_cache_support(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

void dcn6_ms_calculate_vactive_pstate_requirements(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states);

void dcn6_ms_get_plane_support_info(
		const struct dml2_core_calculate_ms_context *ctx,
		const struct dml2_core_internal_mode_support *states,
		struct core_plane_support_info *plane_support,
		unsigned int plane_idx);

void dcn6_ms_get_stream_support_info(
		const struct dml2_core_calculate_ms_context *ctx,
		const struct dml2_core_internal_mode_support *states,
		struct core_stream_support_info *stream_support,
		unsigned int plane_index);

#endif /* __DML2_CORE_DCN6_FUNCS_MODE_SUPPORT_H__ */
