// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#include "dml2_pmo_factory.h"
#include "dml2_pmo_dcn3.h"

static void sort(double *list_a, int list_a_size)
{
	// For all elements b[i] in list_b[]
	for (int i = 0; i < list_a_size - 1; i++) {
		// Find the first element of list_a that's larger than b[i]
		for (int j = i; j < list_a_size - 1; j++) {
			if (list_a[j] > list_a[j + 1])
				swap(list_a[j], list_a[j + 1]);
		}
	}
}

static double get_max_reserved_time_on_all_planes_with_stream_index(struct display_configuation_with_meta *config, unsigned int stream_index)
{
	struct dml2_plane_parameters *plane_descriptor;
	long max_reserved_time_ns = 0;

	for (unsigned int i = 0; i < config->display_config.num_planes; i++) {
		plane_descriptor = &config->display_config.plane_descriptors[i];

		if (plane_descriptor->stream_index == stream_index)
			if (plane_descriptor->overrides.reserved_vblank_time_ns > max_reserved_time_ns)
				max_reserved_time_ns = plane_descriptor->overrides.reserved_vblank_time_ns;
	}

	return (max_reserved_time_ns / 1000.0);
}


static void set_reserved_time_on_all_planes_with_stream_index(struct display_configuation_with_meta *config, unsigned int stream_index, double reserved_time_us)
{
	struct dml2_plane_parameters *plane_descriptor;

	for (unsigned int i = 0; i < config->display_config.num_planes; i++) {
		plane_descriptor = &config->display_config.plane_descriptors[i];

		if (plane_descriptor->stream_index == stream_index)
			plane_descriptor->overrides.reserved_vblank_time_ns = (long int)(reserved_time_us * 1000);
	}
}

static void remove_duplicates(double *list_a, int *list_a_size)
{
	int cur_element = 0;
	// For all elements b[i] in list_b[]
	while (cur_element < *list_a_size - 1) {
		if (list_a[cur_element] == list_a[cur_element + 1]) {
			for (int j = cur_element + 1; j < *list_a_size - 1; j++) {
				list_a[j] = list_a[j + 1];
			}
			*list_a_size = *list_a_size - 1;
		} else {
			cur_element++;
		}
	}
}

static bool increase_mpc_combine_factor(unsigned int *mpc_combine_factor, unsigned int limit)
{
	if (*mpc_combine_factor < limit) {
		(*mpc_combine_factor)++;
		return true;
	}

	return false;
}

static bool optimize_dcc_mcache_no_odm(struct dml2_pmo_optimize_dcc_mcache_in_out *in_out,
	int free_pipes)
{
	struct dml2_pmo_instance *pmo = in_out->instance;

	unsigned int i;
	bool result = true;

	for (i = 0; i < in_out->optimized_display_cfg->num_planes; i++) {
		// For pipes that failed dcc mcache check, we want to increase the pipe count.
		// The logic for doing this depends on how many pipes is already being used,
		// and whether it's mpcc or odm combine.
		if (!in_out->dcc_mcache_supported[i]) {
			// For the general case of "n displays", we can only optimize streams with an ODM combine factor of 1
			if (in_out->cfg_support_info->stream_support_info[in_out->optimized_display_cfg->plane_descriptors[i].stream_index].odms_used == 1) {
				in_out->optimized_display_cfg->plane_descriptors[i].overrides.mpcc_combine_factor =
					in_out->cfg_support_info->plane_support_info[i].dpps_used;
				// For each plane that is not passing mcache validation, just add another pipe to it, up to the limit.
				if (free_pipes > 0) {
					if (!increase_mpc_combine_factor(&in_out->optimized_display_cfg->plane_descriptors[i].overrides.mpcc_combine_factor,
						pmo->mpc_combine_limit)) {
						// We've reached max pipes allocatable to a single plane, so we fail.
						result = false;
						break;
					} else {
						// Successfully added another pipe to this failing plane.
						free_pipes--;
					}
				} else {
					// No free pipes to add.
					result = false;
					break;
				}
			} else {
				// If the stream of this plane needs ODM combine, no further optimization can be done.
				result = false;
				break;
			}
		}
	}

	return result;
}

static bool iterate_to_next_candidiate(struct dml2_pmo_instance *pmo, int size)
{
	int borrow_from, i;
	bool success = false;

	if (pmo->scratch.pmo_dcn3.current_candidate[0] > 0) {
		pmo->scratch.pmo_dcn3.current_candidate[0]--;
		success = true;
	} else {
		for (borrow_from = 1; borrow_from < size && pmo->scratch.pmo_dcn3.current_candidate[borrow_from] == 0; borrow_from++)
			;

		if (borrow_from < size) {
			pmo->scratch.pmo_dcn3.current_candidate[borrow_from]--;
			for (i = 0; i < borrow_from; i++) {
				pmo->scratch.pmo_dcn3.current_candidate[i] = pmo->scratch.pmo_dcn3.reserved_time_candidates_count[i] - 1;
			}

			success = true;
		}
	}

	return success;
}

static bool increase_odm_combine_factor(enum dml2_odm_mode *odm_mode, int odms_calculated)
{
	bool result = true;

	if (*odm_mode == dml2_odm_mode_auto) {
		switch (odms_calculated) {
		case 1:
			*odm_mode = dml2_odm_mode_bypass;
			break;
		case 2:
			*odm_mode = dml2_odm_mode_combine_2to1;
			break;
		case 3:
			*odm_mode = dml2_odm_mode_combine_3to1;
			break;
		case 4:
			*odm_mode = dml2_odm_mode_combine_4to1;
			break;
		default:
			result = false;
			break;
		}
	}

	if (result) {
		if (*odm_mode == dml2_odm_mode_bypass) {
			*odm_mode = dml2_odm_mode_combine_2to1;
		} else if (*odm_mode == dml2_odm_mode_combine_2to1) {
			*odm_mode = dml2_odm_mode_combine_3to1;
		} else if (*odm_mode == dml2_odm_mode_combine_3to1) {
			*odm_mode = dml2_odm_mode_combine_4to1;
		} else {
			result = false;
		}
	}

	return result;
}

static int count_planes_with_stream_index(const struct dml2_display_cfg *display_cfg, unsigned int stream_index)
{
	unsigned int i, count;

	count = 0;
	for (i = 0; i < display_cfg->num_planes; i++) {
		if (display_cfg->plane_descriptors[i].stream_index == stream_index)
			count++;
	}

	return count;
}

static bool are_timings_trivially_synchronizable(struct display_configuation_with_meta *display_config, int mask)
{
	unsigned char i;
	bool identical = true;
	bool contains_drr = false;
	unsigned char remap_array[DML2_MAX_PLANES];
	unsigned char remap_array_size = 0;

	// Create a remap array to enable simple iteration through only masked stream indicies
	for (i = 0; i < display_config->display_config.num_streams; i++) {
		if (mask & (0x1 << i)) {
			remap_array[remap_array_size++] = i;
		}
	}

	// 0 or 1 display is always trivially synchronizable
	if (remap_array_size <= 1)
		return true;

	for (i = 1; i < remap_array_size; i++) {
		if (memcmp(&display_config->display_config.stream_descriptors[remap_array[i - 1]].timing,
			&display_config->display_config.stream_descriptors[remap_array[i]].timing,
			sizeof(struct dml2_timing_cfg))) {
			identical = false;
			break;
		}
	}

	for (i = 0; i < remap_array_size; i++) {
		if (display_config->display_config.stream_descriptors[remap_array[i]].timing.drr_config.enabled) {
			contains_drr = true;
			break;
		}
	}

	return !contains_drr && identical;
}

bool pmo_dcn3_initialize(struct dml2_pmo_initialize_in_out *in_out)
{
	struct dml2_pmo_instance *pmo = in_out->instance;

	pmo->soc_bb = in_out->soc_bb;
	pmo->ip_caps = in_out->ip_caps;
	pmo->mpc_combine_limit = 2;
	pmo->odm_combine_limit = 4;
	pmo->mcg_clock_table_size = in_out->mcg_clock_table_size;

	pmo->options = in_out->options;

	return true;
}

static bool is_h_timing_divisible_by(const struct dml2_timing_cfg *timing, unsigned char denominator)
{
	/*
	 * Htotal, Hblank start/end, and Hsync start/end all must be divisible
	 * in order for the horizontal timing params to be considered divisible
	 * by 2. Hsync start is always 0.
	 */
	unsigned long h_blank_start = timing->h_total - timing->h_front_porch;

	return (timing->h_total % denominator == 0) &&
			(h_blank_start % denominator == 0) &&
			(timing->h_blank_end % denominator == 0) &&
			(timing->h_sync_width % denominator == 0);
}

static bool is_dp_encoder(enum dml2_output_encoder_class encoder_type)
{
	switch (encoder_type) {
	case dml2_dp:
	case dml2_edp:
	case dml2_dp2p0:
	case dml2_none:
		return true;
	case dml2_hdmi:
	case dml2_hdmifrl:
	default:
		return false;
	}
}

bool pmo_dcn3_init_for_vmin(struct dml2_pmo_init_for_vmin_in_out *in_out)
{
	unsigned int i;
	const struct dml2_display_cfg *display_config =
			&in_out->base_display_config->display_config;
	const struct dml2_core_mode_support_result *mode_support_result =
			&in_out->base_display_config->mode_support_result;

	if (in_out->instance->options->disable_dyn_odm ||
			(in_out->instance->options->disable_dyn_odm_for_multi_stream && display_config->num_streams > 1))
		return false;

	for (i = 0; i < display_config->num_planes; i++)
		/*
		 * vmin optimization is required to be seamlessly switched off
		 * at any time when the new configuration is no longer
		 * supported. However switching from ODM combine to MPC combine
		 * is not always seamless. When there not enough free pipes, we
		 * will have to use the same secondary OPP heads as secondary
		 * DPP pipes in MPC combine in new state. This transition is
		 * expected to cause glitches. To avoid the transition, we only
		 * allow vmin optimization if the stream's base configuration
		 * doesn't require MPC combine. This condition checks if MPC
		 * combine is enabled. If so do not optimize the stream.
		 */
		if (mode_support_result->cfg_support_info.plane_support_info[i].dpps_used > 1 &&
				mode_support_result->cfg_support_info.stream_support_info[display_config->plane_descriptors[i].stream_index].odms_used == 1)
			in_out->base_display_config->stage4.unoptimizable_streams[display_config->plane_descriptors[i].stream_index] = true;

	for (i = 0; i < display_config->num_streams; i++) {
		if (display_config->stream_descriptors[i].overrides.disable_dynamic_odm)
			in_out->base_display_config->stage4.unoptimizable_streams[i] = true;
		else if (in_out->base_display_config->stage3.stream_svp_meta[i].valid &&
				in_out->instance->options->disable_dyn_odm_for_stream_with_svp)
			in_out->base_display_config->stage4.unoptimizable_streams[i] = true;
		/*
		 * ODM Combine requires horizontal timing divisible by 2 so each
		 * ODM segment has the same size.
		 */
		else if (!is_h_timing_divisible_by(&display_config->stream_descriptors[i].timing, 2))
			in_out->base_display_config->stage4.unoptimizable_streams[i] = true;
		/*
		 * Our hardware support seamless ODM transitions for DP encoders
		 * only.
		 */
		else if (!is_dp_encoder(display_config->stream_descriptors[i].output.output_encoder))
			in_out->base_display_config->stage4.unoptimizable_streams[i] = true;
	}

	return true;
}

bool pmo_dcn3_test_for_vmin(struct dml2_pmo_test_for_vmin_in_out *in_out)
{
	bool is_vmin = true;

	if (in_out->vmin_limits->dispclk_khz > 0 &&
		in_out->display_config->mode_support_result.global.dispclk_khz > in_out->vmin_limits->dispclk_khz)
		is_vmin = false;

	return is_vmin;
}

static int find_highest_odm_load_stream_index(
		const struct dml2_display_cfg *display_config,
		const struct dml2_core_mode_support_result *mode_support_result)
{
	unsigned int i;
	int odm_load, highest_odm_load = -1, highest_odm_load_index = -1;

	for (i = 0; i < display_config->num_streams; i++) {
		odm_load = display_config->stream_descriptors[i].timing.pixel_clock_khz
				/ mode_support_result->cfg_support_info.stream_support_info[i].odms_used;
		if (odm_load > highest_odm_load) {
			highest_odm_load_index = i;
			highest_odm_load = odm_load;
		}
	}

	return highest_odm_load_index;
}

bool pmo_dcn3_optimize_for_vmin(struct dml2_pmo_optimize_for_vmin_in_out *in_out)
{
	int stream_index;
	const struct dml2_display_cfg *display_config =
			&in_out->base_display_config->display_config;
	const struct dml2_core_mode_support_result *mode_support_result =
			&in_out->base_display_config->mode_support_result;
	unsigned int odms_used;
	struct dml2_stream_parameters *stream_descriptor;
	bool optimizable = false;

	/*
	 * highest odm load stream must be optimizable to continue as dispclk is
	 * bounded by it.
	 */
	stream_index = find_highest_odm_load_stream_index(display_config,
			mode_support_result);

	if (stream_index < 0 ||
			in_out->base_display_config->stage4.unoptimizable_streams[stream_index])
		return false;

	odms_used = mode_support_result->cfg_support_info.stream_support_info[stream_index].odms_used;
	if ((int)odms_used >= in_out->instance->odm_combine_limit)
		return false;

	memcpy(in_out->optimized_display_config,
			in_out->base_display_config,
			sizeof(struct display_configuation_with_meta));

	stream_descriptor = &in_out->optimized_display_config->display_config.stream_descriptors[stream_index];
	while (!optimizable && increase_odm_combine_factor(
			&stream_descriptor->overrides.odm_mode,
			odms_used)) {
		switch (stream_descriptor->overrides.odm_mode) {
		case dml2_odm_mode_combine_2to1:
			optimizable = true;
			break;
		case dml2_odm_mode_combine_3to1:
			/*
			 * In ODM Combine 3:1 OTG_valid_pixel rate is 1/4 of
			 * actual pixel rate. Therefore horizontal timing must
			 * be divisible by 4.
			 */
			if (is_h_timing_divisible_by(&display_config->stream_descriptors[stream_index].timing, 4)) {
				if (mode_support_result->cfg_support_info.stream_support_info[stream_index].dsc_enable) {
					/*
					 * DSC h slice count must be divisible
					 * by 3.
					 */
					if (mode_support_result->cfg_support_info.stream_support_info[stream_index].num_dsc_slices % 3 == 0)
						optimizable = true;
				} else {
					optimizable = true;
				}
			}
			break;
		case dml2_odm_mode_combine_4to1:
			/*
			 * In ODM Combine 4:1 OTG_valid_pixel rate is 1/4 of
			 * actual pixel rate. Therefore horizontal timing must
			 * be divisible by 4.
			 */
			if (is_h_timing_divisible_by(&display_config->stream_descriptors[stream_index].timing, 4)) {
				if (mode_support_result->cfg_support_info.stream_support_info[stream_index].dsc_enable) {
					/*
					 * DSC h slice count must be divisible
					 * by 4.
					 */
					if (mode_support_result->cfg_support_info.stream_support_info[stream_index].num_dsc_slices % 4 == 0)
						optimizable = true;
				} else {
					optimizable = true;
				}
			}
			break;
		case dml2_odm_mode_auto:
		case dml2_odm_mode_bypass:
		case dml2_odm_mode_split_1to2:
		case dml2_odm_mode_mso_1to2:
		case dml2_odm_mode_mso_1to4:
		default:
			break;
		}
	}

	return optimizable;
}

bool pmo_dcn3_optimize_dcc_mcache(struct dml2_pmo_optimize_dcc_mcache_in_out *in_out)
{
	struct dml2_pmo_instance *pmo = in_out->instance;

	unsigned int i, used_pipes, free_pipes, planes_on_stream;
	bool result;

	if (in_out->display_config != in_out->optimized_display_cfg) {
		memcpy(in_out->optimized_display_cfg, in_out->display_config, sizeof(struct dml2_display_cfg));
	}

	//Count number of free pipes, and check if any odm combine is in use.
	used_pipes = 0;
	for (i = 0; i < in_out->optimized_display_cfg->num_planes; i++) {
		used_pipes += in_out->cfg_support_info->plane_support_info[i].dpps_used;
	}
	free_pipes = pmo->ip_caps->pipe_count - used_pipes;

	// Optimization loop
	// The goal here is to add more pipes to any planes
	// which are failing mcache admissibility
	result = true;

	// The optimization logic depends on whether ODM combine is enabled, and the stream count.
	if (in_out->optimized_display_cfg->num_streams > 1) {
		// If there are multiple streams, we are limited to only be able to optimize mcache failures on planes
		// which are not ODM combined.

		result = optimize_dcc_mcache_no_odm(in_out, free_pipes);
	} else if (in_out->optimized_display_cfg->num_streams == 1) {
		// In single stream cases, we still optimize mcache failures when there's ODM combine with some
		// additional logic.

		if (in_out->cfg_support_info->stream_support_info[0].odms_used > 1) {
			// If ODM combine is enabled, then the logic is to increase ODM combine factor.

			// Optimization for streams with > 1 ODM combine factor is only supported for single display.
			planes_on_stream = count_planes_with_stream_index(in_out->optimized_display_cfg, 0);

			for (i = 0; i < in_out->optimized_display_cfg->num_planes; i++) {
				// For pipes that failed dcc mcache check, we want to increase the pipe count.
				// The logic for doing this depends on how many pipes is already being used,
				// and whether it's mpcc or odm combine.
				if (!in_out->dcc_mcache_supported[i]) {
					// Increasing ODM combine factor on a stream requires a free pipe for each plane on the stream.
					if (free_pipes >= planes_on_stream) {
						if (!increase_odm_combine_factor(&in_out->optimized_display_cfg->stream_descriptors[i].overrides.odm_mode,
							in_out->cfg_support_info->plane_support_info[i].dpps_used)) {
							result = false;
						} else {
							break;
						}
					} else {
						result = false;
						break;
					}
				}
			}
		} else {
			// If ODM combine is not enabled, then we can actually use the same logic as before.

			result = optimize_dcc_mcache_no_odm(in_out, free_pipes);
		}
	} else {
		result = true;
	}

	return result;
}

bool pmo_dcn3_init_for_pstate_support(struct dml2_pmo_init_for_pstate_support_in_out *in_out)
{
	struct dml2_pmo_instance *pmo = in_out->instance;
	struct dml2_optimization_stage3_state *state = &in_out->base_display_config->stage3;
	const struct dml2_stream_parameters *stream_descriptor;
	const struct dml2_plane_parameters *plane_descriptor;
	unsigned int stream_index, plane_index, candidate_count;
	double min_reserved_vblank_time = 0;
	int fclk_twait_needed_mask = 0x0;
	int uclk_twait_needed_mask = 0x0;

	state->performed = true;
	state->min_clk_index_for_latency = in_out->base_display_config->stage1.min_clk_index_for_latency;
	pmo->scratch.pmo_dcn3.min_latency_index = in_out->base_display_config->stage1.min_clk_index_for_latency;
	pmo->scratch.pmo_dcn3.max_latency_index = pmo->mcg_clock_table_size - 1;
	pmo->scratch.pmo_dcn3.cur_latency_index = in_out->base_display_config->stage1.min_clk_index_for_latency;

	pmo->scratch.pmo_dcn3.stream_mask = 0xF;

	for (plane_index = 0; plane_index < in_out->base_display_config->display_config.num_planes; plane_index++) {
		plane_descriptor = &in_out->base_display_config->display_config.plane_descriptors[plane_index];
		stream_descriptor = &in_out->base_display_config->display_config.stream_descriptors[plane_descriptor->stream_index];

		if (in_out->base_display_config->mode_support_result.cfg_support_info.plane_support_info[plane_index].active_latency_hiding_us <
			in_out->instance->soc_bb->power_management_parameters.dram_clk_change_blackout_us &&
			stream_descriptor->overrides.hw.twait_budgeting.uclk_pstate == dml2_twait_budgeting_setting_if_needed)
			uclk_twait_needed_mask |= (0x1 << plane_descriptor->stream_index);

		if (stream_descriptor->overrides.hw.twait_budgeting.uclk_pstate == dml2_twait_budgeting_setting_try)
			uclk_twait_needed_mask |= (0x1 << plane_descriptor->stream_index);

		if (in_out->base_display_config->mode_support_result.cfg_support_info.plane_support_info[plane_index].active_latency_hiding_us <
			in_out->instance->soc_bb->power_management_parameters.fclk_change_blackout_us &&
			stream_descriptor->overrides.hw.twait_budgeting.fclk_pstate == dml2_twait_budgeting_setting_if_needed)
			fclk_twait_needed_mask |= (0x1 << plane_descriptor->stream_index);

		if (stream_descriptor->overrides.hw.twait_budgeting.fclk_pstate == dml2_twait_budgeting_setting_try)
			fclk_twait_needed_mask |= (0x1 << plane_descriptor->stream_index);

		if (plane_descriptor->overrides.legacy_svp_config != dml2_svp_mode_override_auto) {
			pmo->scratch.pmo_dcn3.stream_mask &= ~(0x1 << plane_descriptor->stream_index);
		}
	}

	for (stream_index = 0; stream_index < in_out->base_display_config->display_config.num_streams; stream_index++) {
		stream_descriptor = &in_out->base_display_config->display_config.stream_descriptors[stream_index];

		// The absolute minimum required time is the minimum of all the required budgets
		/*
		if (stream_descriptor->overrides.hw.twait_budgeting.fclk_pstate
			== dml2_twait_budgeting_setting_require)

			if (are_timings_trivially_synchronizable(in_out->base_display_config, pmo->scratch.pmo_dcn3.stream_mask)) {
				min_reserved_vblank_time = max_double2(min_reserved_vblank_time,
					in_out->instance->soc_bb->power_management_parameters.fclk_change_blackout_us);
			}

		if (stream_descriptor->overrides.hw.twait_budgeting.uclk_pstate
			== dml2_twait_budgeting_setting_require) {

			if (are_timings_trivially_synchronizable(in_out->base_display_config, pmo->scratch.pmo_dcn3.stream_mask)) {
				min_reserved_vblank_time = max_double2(min_reserved_vblank_time,
					in_out->instance->soc_bb->power_management_parameters.dram_clk_change_blackout_us);
			}
		}

		if (stream_descriptor->overrides.hw.twait_budgeting.stutter_enter_exit
			== dml2_twait_budgeting_setting_require)
			min_reserved_vblank_time = max_double2(min_reserved_vblank_time,
				in_out->instance->soc_bb->power_management_parameters.stutter_enter_plus_exit_latency_us);
		*/

		min_reserved_vblank_time = get_max_reserved_time_on_all_planes_with_stream_index(in_out->base_display_config, stream_index);

		// Insert the absolute minimum into the array
		candidate_count = 1;
		pmo->scratch.pmo_dcn3.reserved_time_candidates[stream_index][0] = min_reserved_vblank_time;
		pmo->scratch.pmo_dcn3.reserved_time_candidates_count[stream_index] = candidate_count;

		if (!(pmo->scratch.pmo_dcn3.stream_mask & (0x1 << stream_index)))
			continue;

		// For every optional feature, we create a candidate for it only if it's larger minimum.
		if ((fclk_twait_needed_mask & (0x1 << stream_index)) &&
			in_out->instance->soc_bb->power_management_parameters.fclk_change_blackout_us > min_reserved_vblank_time) {

			if (are_timings_trivially_synchronizable(in_out->base_display_config, pmo->scratch.pmo_dcn3.stream_mask)) {
				pmo->scratch.pmo_dcn3.reserved_time_candidates[stream_index][candidate_count++] =
					in_out->instance->soc_bb->power_management_parameters.fclk_change_blackout_us;
			}
		}

		if ((uclk_twait_needed_mask & (0x1 << stream_index)) &&
			in_out->instance->soc_bb->power_management_parameters.dram_clk_change_blackout_us > min_reserved_vblank_time) {

			if (are_timings_trivially_synchronizable(in_out->base_display_config, pmo->scratch.pmo_dcn3.stream_mask)) {
				pmo->scratch.pmo_dcn3.reserved_time_candidates[stream_index][candidate_count++] =
					in_out->instance->soc_bb->power_management_parameters.dram_clk_change_blackout_us;
			}
		}

		if ((stream_descriptor->overrides.hw.twait_budgeting.stutter_enter_exit == dml2_twait_budgeting_setting_try ||
			stream_descriptor->overrides.hw.twait_budgeting.stutter_enter_exit == dml2_twait_budgeting_setting_if_needed) &&
			in_out->instance->soc_bb->power_management_parameters.stutter_enter_plus_exit_latency_us > min_reserved_vblank_time) {

			pmo->scratch.pmo_dcn3.reserved_time_candidates[stream_index][candidate_count++] =
				in_out->instance->soc_bb->power_management_parameters.stutter_enter_plus_exit_latency_us;
		}

		pmo->scratch.pmo_dcn3.reserved_time_candidates_count[stream_index] = candidate_count;

		// Finally sort the array of candidates
		sort(pmo->scratch.pmo_dcn3.reserved_time_candidates[stream_index],
			pmo->scratch.pmo_dcn3.reserved_time_candidates_count[stream_index]);

		remove_duplicates(pmo->scratch.pmo_dcn3.reserved_time_candidates[stream_index],
			&pmo->scratch.pmo_dcn3.reserved_time_candidates_count[stream_index]);

		pmo->scratch.pmo_dcn3.current_candidate[stream_index] =
			pmo->scratch.pmo_dcn3.reserved_time_candidates_count[stream_index] - 1;
	}

	return true;
}

bool pmo_dcn3_test_for_pstate_support(struct dml2_pmo_test_for_pstate_support_in_out *in_out)
{
	struct dml2_pmo_instance *pmo = in_out->instance;

	unsigned int i, stream_index;

	for (i = 0; i < in_out->base_display_config->display_config.num_planes; i++) {
		stream_index = in_out->base_display_config->display_config.plane_descriptors[i].stream_index;

		if (in_out->base_display_config->display_config.plane_descriptors[i].overrides.reserved_vblank_time_ns <
			pmo->scratch.pmo_dcn3.reserved_time_candidates[stream_index][pmo->scratch.pmo_dcn3.current_candidate[stream_index]] * 1000) {
			return false;
		}
	}

	return true;
}

bool pmo_dcn3_optimize_for_pstate_support(struct dml2_pmo_optimize_for_pstate_support_in_out *in_out)
{
	struct dml2_pmo_instance *pmo = in_out->instance;
	unsigned int stream_index;
	bool success = false;
	bool reached_end;

	memcpy(in_out->optimized_display_config, in_out->base_display_config, sizeof(struct display_configuation_with_meta));

	if (in_out->last_candidate_failed) {
		if (pmo->scratch.pmo_dcn3.cur_latency_index < pmo->scratch.pmo_dcn3.max_latency_index) {
			// If we haven't tried all the clock bounds to support this state, try a higher one
			pmo->scratch.pmo_dcn3.cur_latency_index++;

			success = true;
		} else {
			// If there's nothing higher to try, then we have to have a smaller canadidate
			reached_end = !iterate_to_next_candidiate(pmo, in_out->optimized_display_config->display_config.num_streams);

			if (!reached_end) {
				pmo->scratch.pmo_dcn3.cur_latency_index = pmo->scratch.pmo_dcn3.min_latency_index;
				success = true;
			}
		}
	} else {
		success = true;
	}

	if (success) {
		in_out->optimized_display_config->stage3.min_clk_index_for_latency = pmo->scratch.pmo_dcn3.cur_latency_index;

		for (stream_index = 0; stream_index < in_out->optimized_display_config->display_config.num_streams; stream_index++) {
			set_reserved_time_on_all_planes_with_stream_index(in_out->optimized_display_config, stream_index,
				pmo->scratch.pmo_dcn3.reserved_time_candidates[stream_index][pmo->scratch.pmo_dcn3.current_candidate[stream_index]]);
		}
	}

	return success;
}
