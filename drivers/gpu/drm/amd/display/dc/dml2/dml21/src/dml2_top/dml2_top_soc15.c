// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#include "dml2_top_soc15.h"
#include "dml2_mcg_factory.h"
#include "dml2_dpmm_factory.h"
#include "dml2_core_factory.h"
#include "dml2_pmo_factory.h"
#include "lib_float_math.h"
#include "dml2_debug.h"
static void setup_unoptimized_display_config_with_meta(const struct dml2_instance *dml, struct display_configuation_with_meta *out, const struct dml2_display_cfg *display_config)
{
	memcpy(&out->display_config, display_config, sizeof(struct dml2_display_cfg));
	out->stage1.min_clk_index_for_latency = dml->min_clk_table.dram_bw_table.num_entries - 1; //dml->min_clk_table.clean_me_up.soc_bb.num_states - 1;
}

static void setup_speculative_display_config_with_meta(const struct dml2_instance *dml, struct display_configuation_with_meta *out, const struct dml2_display_cfg *display_config)
{
	memcpy(&out->display_config, display_config, sizeof(struct dml2_display_cfg));
	out->stage1.min_clk_index_for_latency = 0;
}

static void copy_display_configuration_with_meta(struct display_configuation_with_meta *dst, const struct display_configuation_with_meta *src)
{
	memcpy(dst, src, sizeof(struct display_configuation_with_meta));
}

static bool dml2_top_optimization_init_function_min_clk_for_latency(const struct optimization_init_function_params *params)
{
	struct dml2_optimization_stage1_state *state = &params->display_config->stage1;

	state->performed = true;

	return true;
}

static bool dml2_top_optimization_test_function_min_clk_for_latency(const struct optimization_test_function_params *params)
{
	struct dml2_optimization_stage1_state *state = &params->display_config->stage1;

	return state->min_clk_index_for_latency == 0;
}

static bool dml2_top_optimization_optimize_function_min_clk_for_latency(const struct optimization_optimize_function_params *params)
{
	bool result = false;

	if (params->display_config->stage1.min_clk_index_for_latency > 0) {
		copy_display_configuration_with_meta(params->optimized_display_config, params->display_config);
		params->optimized_display_config->stage1.min_clk_index_for_latency--;
		result = true;
	}

	return result;
}

static bool dml2_top_optimization_test_function_mcache(const struct optimization_test_function_params *params)
{
	struct dml2_optimization_test_function_locals *l = params->locals;
	bool mcache_success = false;
	bool result = false;

	memset(l, 0, sizeof(struct dml2_optimization_test_function_locals));

	l->test_mcache.calc_mcache_count_params.dml2_instance = params->dml;
	l->test_mcache.calc_mcache_count_params.display_config = &params->display_config->display_config;
	l->test_mcache.calc_mcache_count_params.mcache_allocations = params->display_config->stage2.mcache_allocations;

	result = dml2_top_mcache_calc_mcache_count_and_offsets(&l->test_mcache.calc_mcache_count_params); // use core to get the basic mcache_allocations

	if (result) {
		l->test_mcache.assign_global_mcache_ids_params.allocations = params->display_config->stage2.mcache_allocations;
		l->test_mcache.assign_global_mcache_ids_params.num_allocations = params->display_config->display_config.num_planes;

		dml2_top_mcache_assign_global_mcache_ids(&l->test_mcache.assign_global_mcache_ids_params);

		l->test_mcache.validate_admissibility_params.dml2_instance = params->dml;
		l->test_mcache.validate_admissibility_params.display_cfg = &params->display_config->display_config;
		l->test_mcache.validate_admissibility_params.mcache_allocations = params->display_config->stage2.mcache_allocations;
		l->test_mcache.validate_admissibility_params.cfg_support_info = &params->display_config->mode_support_result.cfg_support_info;

		mcache_success = dml2_top_mcache_validate_admissability(&l->test_mcache.validate_admissibility_params); // also find the shift to make mcache allocation works

		memcpy(params->display_config->stage2.per_plane_mcache_support, l->test_mcache.validate_admissibility_params.per_plane_status, sizeof(bool) * DML2_MAX_PLANES);
	}

	return mcache_success;
}

static bool dml2_top_optimization_optimize_function_mcache(const struct optimization_optimize_function_params *params)
{
	struct dml2_optimization_optimize_function_locals *l = params->locals;
	bool optimize_success = false;

	if (params->last_candidate_supported == false)
		return false;

	copy_display_configuration_with_meta(params->optimized_display_config, params->display_config);

	l->optimize_mcache.optimize_mcache_params.instance = &params->dml->pmo_instance;
	l->optimize_mcache.optimize_mcache_params.dcc_mcache_supported = params->display_config->stage2.per_plane_mcache_support;
	l->optimize_mcache.optimize_mcache_params.display_config = &params->display_config->display_config;
	l->optimize_mcache.optimize_mcache_params.optimized_display_cfg = &params->optimized_display_config->display_config;
	l->optimize_mcache.optimize_mcache_params.cfg_support_info = &params->optimized_display_config->mode_support_result.cfg_support_info;

	optimize_success = params->dml->pmo_instance.optimize_dcc_mcache(&l->optimize_mcache.optimize_mcache_params);

	return optimize_success;
}

static bool dml2_top_optimization_init_function_vmin(const struct optimization_init_function_params *params)
{
	struct dml2_optimization_init_function_locals *l = params->locals;

	l->vmin.init_params.instance = &params->dml->pmo_instance;
	l->vmin.init_params.base_display_config = params->display_config;
	return params->dml->pmo_instance.init_for_vmin(&l->vmin.init_params);
}

static bool dml2_top_optimization_test_function_vmin(const struct optimization_test_function_params *params)
{
	struct dml2_optimization_test_function_locals *l = params->locals;

	l->test_vmin.pmo_test_vmin_params.instance = &params->dml->pmo_instance;
	l->test_vmin.pmo_test_vmin_params.display_config = params->display_config;
	l->test_vmin.pmo_test_vmin_params.vmin_limits = &params->dml->soc_bbox.vmin_limit;
	return params->dml->pmo_instance.test_for_vmin(&l->test_vmin.pmo_test_vmin_params);
}

static bool dml2_top_optimization_optimize_function_vmin(const struct optimization_optimize_function_params *params)
{
	struct dml2_optimization_optimize_function_locals *l = params->locals;

	if (params->last_candidate_supported == false)
		return false;

	l->optimize_vmin.pmo_optimize_vmin_params.instance = &params->dml->pmo_instance;
	l->optimize_vmin.pmo_optimize_vmin_params.base_display_config = params->display_config;
	l->optimize_vmin.pmo_optimize_vmin_params.optimized_display_config = params->optimized_display_config;
	return params->dml->pmo_instance.optimize_for_vmin(&l->optimize_vmin.pmo_optimize_vmin_params);
}

static bool dml2_top_optimization_init_function_uclk_pstate(const struct optimization_init_function_params *params)
{
	struct dml2_optimization_init_function_locals *l = params->locals;

	l->uclk_pstate.init_params.instance = &params->dml->pmo_instance;
	l->uclk_pstate.init_params.base_display_config = params->display_config;

	return params->dml->pmo_instance.init_for_uclk_pstate(&l->uclk_pstate.init_params);
}

static bool dml2_top_optimization_test_function_uclk_pstate(const struct optimization_test_function_params *params)
{
	struct dml2_optimization_test_function_locals *l = params->locals;

	l->uclk_pstate.test_params.instance = &params->dml->pmo_instance;
	l->uclk_pstate.test_params.base_display_config = params->display_config;

	return params->dml->pmo_instance.test_for_uclk_pstate(&l->uclk_pstate.test_params);
}

static bool dml2_top_optimization_optimize_function_uclk_pstate(const struct optimization_optimize_function_params *params)
{
	struct dml2_optimization_optimize_function_locals *l = params->locals;

	l->uclk_pstate.optimize_params.instance = &params->dml->pmo_instance;
	l->uclk_pstate.optimize_params.base_display_config = params->display_config;
	l->uclk_pstate.optimize_params.optimized_display_config = params->optimized_display_config;
	l->uclk_pstate.optimize_params.last_candidate_failed = !params->last_candidate_supported;

	return params->dml->pmo_instance.optimize_for_uclk_pstate(&l->uclk_pstate.optimize_params);
}

static bool dml2_top_optimization_init_function_stutter(const struct optimization_init_function_params *params)
{
	struct dml2_optimization_init_function_locals *l = params->locals;

	l->uclk_pstate.init_params.instance = &params->dml->pmo_instance;
	l->uclk_pstate.init_params.base_display_config = params->display_config;

	return params->dml->pmo_instance.init_for_stutter(&l->stutter.stutter_params);
}

static bool dml2_top_optimization_test_function_stutter(const struct optimization_test_function_params *params)
{
	struct dml2_optimization_test_function_locals *l = params->locals;

	l->stutter.stutter_params.instance = &params->dml->pmo_instance;
	l->stutter.stutter_params.base_display_config = params->display_config;
	return params->dml->pmo_instance.test_for_stutter(&l->stutter.stutter_params);
}

static bool dml2_top_optimization_optimize_function_stutter(const struct optimization_optimize_function_params *params)
{
	struct dml2_optimization_optimize_function_locals *l = params->locals;

	l->stutter.stutter_params.instance = &params->dml->pmo_instance;
	l->stutter.stutter_params.base_display_config = params->display_config;
	l->stutter.stutter_params.optimized_display_config = params->optimized_display_config;
	l->stutter.stutter_params.last_candidate_failed = !params->last_candidate_supported;
	return params->dml->pmo_instance.optimize_for_stutter(&l->stutter.stutter_params);
}

static bool dml2_top_optimization_perform_optimization_phase(struct dml2_optimization_phase_locals *l, const struct optimization_phase_params *params)
{
	bool test_passed = false;
	bool optimize_succeeded = true;
	bool candidate_validation_passed = true;
	struct optimization_init_function_params init_params = { 0 };
	struct optimization_test_function_params test_params = { 0 };
	struct optimization_optimize_function_params optimize_params = { 0 };

	if (!params->dml ||
		!params->optimize_function ||
		!params->test_function ||
		!params->display_config ||
		!params->optimized_display_config)
		return false;

	copy_display_configuration_with_meta(&l->cur_candidate_display_cfg, params->display_config);

	init_params.locals = &l->init_function_locals;
	init_params.dml = params->dml;
	init_params.display_config = &l->cur_candidate_display_cfg;

	if (params->init_function && !params->init_function(&init_params))
		return false;

	test_params.locals = &l->test_function_locals;
	test_params.dml = params->dml;
	test_params.display_config = &l->cur_candidate_display_cfg;

	test_passed = params->test_function(&test_params);

	while (!test_passed && optimize_succeeded) {
		memset(&optimize_params, 0, sizeof(struct optimization_optimize_function_params));

		optimize_params.locals = &l->optimize_function_locals;
		optimize_params.dml = params->dml;
		optimize_params.display_config = &l->cur_candidate_display_cfg;
		optimize_params.optimized_display_config = &l->next_candidate_display_cfg;
		optimize_params.last_candidate_supported = candidate_validation_passed;

		optimize_succeeded = params->optimize_function(&optimize_params);

		if (optimize_succeeded) {
			l->mode_support_params.instance = &params->dml->core_instance;
			l->mode_support_params.display_cfg = &l->next_candidate_display_cfg;
			l->mode_support_params.min_clk_table = &params->dml->min_clk_table;

			if (l->next_candidate_display_cfg.stage3.performed)
				l->mode_support_params.min_clk_index = l->next_candidate_display_cfg.stage3.min_clk_index_for_latency;
			else
				l->mode_support_params.min_clk_index = l->next_candidate_display_cfg.stage1.min_clk_index_for_latency;
			candidate_validation_passed = params->dml->core_instance.mode_support(&l->mode_support_params);
			l->next_candidate_display_cfg.mode_support_result = l->mode_support_params.mode_support_result;
		}

		if (optimize_succeeded && candidate_validation_passed) {
			memset(&test_params, 0, sizeof(struct optimization_test_function_params));
			test_params.locals = &l->test_function_locals;
			test_params.dml = params->dml;
			test_params.display_config = &l->next_candidate_display_cfg;
			test_passed = params->test_function(&test_params);

			copy_display_configuration_with_meta(&l->cur_candidate_display_cfg, &l->next_candidate_display_cfg);

			// If optimization is not all or nothing, then store partial progress in output
			if (!params->all_or_nothing)
				copy_display_configuration_with_meta(params->optimized_display_config, &l->next_candidate_display_cfg);
		}
	}

	if (test_passed)
		copy_display_configuration_with_meta(params->optimized_display_config, &l->cur_candidate_display_cfg);

	return test_passed;
}

static bool dml2_top_optimization_perform_optimization_phase_1(struct dml2_optimization_phase_locals *l, const struct optimization_phase_params *params)
{
	int highest_state, lowest_state, cur_state;
	bool supported = false;

	if (!params->dml ||
		!params->optimize_function ||
		!params->test_function ||
		!params->display_config ||
		!params->optimized_display_config)
		return false;

	copy_display_configuration_with_meta(&l->cur_candidate_display_cfg, params->display_config);
	highest_state = l->cur_candidate_display_cfg.stage1.min_clk_index_for_latency;
	lowest_state = 0;

	while (highest_state > lowest_state) {
		cur_state = (highest_state + lowest_state) / 2;

		l->mode_support_params.instance = &params->dml->core_instance;
		l->mode_support_params.display_cfg = &l->cur_candidate_display_cfg;
		l->mode_support_params.min_clk_table = &params->dml->min_clk_table;
		l->mode_support_params.min_clk_index = cur_state;
		supported = params->dml->core_instance.mode_support(&l->mode_support_params);

		if (supported) {
			l->cur_candidate_display_cfg.mode_support_result = l->mode_support_params.mode_support_result;
			highest_state = cur_state;
		} else {
			lowest_state = cur_state + 1;
		}
	}
	l->cur_candidate_display_cfg.stage1.min_clk_index_for_latency = lowest_state;

	copy_display_configuration_with_meta(params->optimized_display_config, &l->cur_candidate_display_cfg);

	return true;
}

/*
* Takes an input set of mcache boundaries and finds the appropriate setting of cache programming.
* Returns true if a valid set of programming can be made, and false otherwise. "Valid" means
* that the horizontal viewport does not span more than 2 cache slices.
*
* It optionally also can apply a constant shift to all the cache boundaries.
*/
static const uint32_t MCACHE_ID_UNASSIGNED = 0xF;
static const uint32_t SPLIT_LOCATION_UNDEFINED = 0xFFFF;

static bool calculate_first_second_splitting(const int *mcache_boundaries, int num_boundaries, int shift,
	int pipe_h_vp_start, int pipe_h_vp_end, int *first_offset, int *second_offset)
{
	const int MAX_VP = 0xFFFFFF;
	int left_cache_id;
	int right_cache_id;
	int range_start;
	int range_end;
	bool success = false;

	if (num_boundaries <= 1) {
		if (first_offset && second_offset) {
			*first_offset = 0;
			*second_offset = -1;
		}
		success = true;
		return success;
	} else {
		range_start = 0;
		for (left_cache_id = 0; left_cache_id < num_boundaries; left_cache_id++) {
			range_end = mcache_boundaries[left_cache_id] - shift - 1;

			if (range_start <= pipe_h_vp_start && pipe_h_vp_start <= range_end)
				break;

			range_start = range_end + 1;
		}

		range_end = MAX_VP;
		for (right_cache_id = num_boundaries - 1; right_cache_id >= -1; right_cache_id--) {
			if (right_cache_id >= 0)
				range_start = mcache_boundaries[right_cache_id] - shift;
			else
				range_start = 0;

			if (range_start <= pipe_h_vp_end && pipe_h_vp_end <= range_end) {
				break;
			}
			range_end = range_start - 1;
		}
		right_cache_id = (right_cache_id + 1) % num_boundaries;

		if (right_cache_id == left_cache_id) {
			if (first_offset && second_offset) {
				*first_offset = left_cache_id;
				*second_offset = -1;
			}
			success = true;
		} else if (right_cache_id == (left_cache_id + 1) % num_boundaries) {
			if (first_offset && second_offset) {
				*first_offset = left_cache_id;
				*second_offset = right_cache_id;
			}
			success = true;
		}
	}

	return success;
}

/*
* For a given set of pipe start/end x positions, checks to see it can support the input mcache splitting.
* It also attempts to "optimize" by finding a shift if the default 0 shift does not work.
*/
static bool find_shift_for_valid_cache_id_assignment(int *mcache_boundaries, unsigned int num_boundaries,
	int *pipe_vp_startx, int *pipe_vp_endx, unsigned int pipe_count, int shift_granularity, int *shift)
{
	int max_shift = 0xFFFF;
	unsigned int pipe_index;
	unsigned int i, slice_width;
	bool success = false;

	for (i = 0; i < num_boundaries; i++) {
		if (i == 0)
			slice_width = mcache_boundaries[i];
		else
			slice_width = mcache_boundaries[i] - mcache_boundaries[i - 1];

		if (max_shift > (int)slice_width) {
			max_shift = slice_width;
		}
	}

	for (*shift = 0; *shift <= max_shift; *shift += shift_granularity) {
		success = true;
		for (pipe_index = 0; pipe_index < pipe_count; pipe_index++) {
			if (!calculate_first_second_splitting(mcache_boundaries, num_boundaries, *shift,
				pipe_vp_startx[pipe_index], pipe_vp_endx[pipe_index], 0, 0)) {
				success = false;
				break;
			}
		}
		if (success)
			break;
	}

	return success;
}

/*
* Counts the number of elements inside input array within the given span length.
* Formally, what is the size of the largest subset of the array where the largest and smallest element
* differ no more than the span.
*/
static unsigned int count_elements_in_span(int *array, unsigned int array_size, unsigned int span)
{
	unsigned int i;
	unsigned int span_start_value;
	unsigned int span_start_index;
	unsigned int greatest_element_count;

	if (array_size == 0)
		return 1;

	if (span == 0)
		return array_size > 0 ? 1 : 0;

	span_start_value = 0;
	span_start_index = 0;
	greatest_element_count = 0;

	while (span_start_index < array_size) {
		for (i = span_start_index; i < array_size; i++) {
			if (array[i] - span_start_value <= span) {
				if (i - span_start_index + 1 > greatest_element_count) {
					greatest_element_count = i - span_start_index + 1;
				}
			} else
				break;
		}

		span_start_index++;

		if (span_start_index < array_size) {
			span_start_value = array[span_start_index - 1] + 1;
		}
	}

	return greatest_element_count;
}

static bool calculate_h_split_for_scaling_transform(int full_vp_width, int h_active, int num_pipes,
	enum dml2_scaling_transform scaling_transform, int *pipe_vp_x_start, int *pipe_vp_x_end)
{
	int i, slice_width;
	const char MAX_SCL_VP_OVERLAP = 3;
	bool success = false;

	switch (scaling_transform) {
	case dml2_scaling_transform_centered:
	case dml2_scaling_transform_aspect_ratio:
	case dml2_scaling_transform_fullscreen:
		slice_width = full_vp_width / num_pipes;
		for (i = 0; i < num_pipes; i++) {
			pipe_vp_x_start[i] = i * slice_width;
			pipe_vp_x_end[i] = (i + 1) * slice_width - 1;

			if (pipe_vp_x_start[i] < MAX_SCL_VP_OVERLAP)
				pipe_vp_x_start[i] = 0;
			else
				pipe_vp_x_start[i] -= MAX_SCL_VP_OVERLAP;

			if (pipe_vp_x_end[i] > full_vp_width - MAX_SCL_VP_OVERLAP - 1)
				pipe_vp_x_end[i] = full_vp_width - 1;
			else
				pipe_vp_x_end[i] += MAX_SCL_VP_OVERLAP;
		}
		break;
	case dml2_scaling_transform_explicit:
	default:
		success = false;
		break;
	}

	return success;
}

bool dml2_top_mcache_validate_admissability(struct top_mcache_validate_admissability_in_out *params)
{
	struct dml2_instance *dml = (struct dml2_instance *)params->dml2_instance;
	struct dml2_top_mcache_validate_admissability_locals *l = &dml->scratch.mcache_validate_admissability_locals;

	const int MAX_PIXEL_OVERLAP = 6;
	int max_per_pipe_vp_p0 = 0;
	int max_per_pipe_vp_p1 = 0;
	int temp, p0shift, p1shift;
	unsigned int plane_index = 0;
	unsigned int i;
	unsigned int odm_combine_factor;
	unsigned int mpc_combine_factor;
	unsigned int num_dpps;
	unsigned int num_boundaries;
	enum dml2_scaling_transform scaling_transform;
	const struct dml2_plane_parameters *plane;
	const struct dml2_stream_parameters *stream;

	bool p0pass = false;
	bool p1pass = false;
	bool all_pass = true;

	for (plane_index = 0; plane_index < params->display_cfg->num_planes; plane_index++) {
		if (!params->display_cfg->plane_descriptors[plane_index].surface.dcc.enable)
			continue;

		plane = &params->display_cfg->plane_descriptors[plane_index];
		stream = &params->display_cfg->stream_descriptors[plane->stream_index];

		num_dpps = odm_combine_factor = params->cfg_support_info->stream_support_info[plane->stream_index].odms_used;

		if (odm_combine_factor == 1)
			num_dpps = mpc_combine_factor = (unsigned int)params->cfg_support_info->plane_support_info[plane_index].dpps_used;
		else
			mpc_combine_factor = 1;

		if (odm_combine_factor > 1) {
			max_per_pipe_vp_p0 = plane->surface.plane0.width;
			temp = (unsigned int)math_ceil(plane->composition.scaler_info.plane0.h_ratio * stream->timing.h_active / odm_combine_factor);

			if (temp < max_per_pipe_vp_p0)
				max_per_pipe_vp_p0 = temp;

			max_per_pipe_vp_p1 = plane->surface.plane1.width;
			temp = (unsigned int)math_ceil(plane->composition.scaler_info.plane1.h_ratio * stream->timing.h_active / odm_combine_factor);

			if (temp < max_per_pipe_vp_p1)
				max_per_pipe_vp_p1 = temp;
		} else {
			max_per_pipe_vp_p0 = plane->surface.plane0.width / mpc_combine_factor;
			max_per_pipe_vp_p1 = plane->surface.plane1.width / mpc_combine_factor;
		}

		max_per_pipe_vp_p0 += 2 * MAX_PIXEL_OVERLAP;
		max_per_pipe_vp_p1 += MAX_PIXEL_OVERLAP;

		p0shift = 0;
		p1shift = 0;

		// The last element in the unshifted boundary array will always be the first pixel outside the
		// plane, which means theres no mcache associated with it, so -1
		num_boundaries = params->mcache_allocations[plane_index].num_mcaches_plane0 == 0 ? 0 : params->mcache_allocations[plane_index].num_mcaches_plane0 - 1;
		if ((count_elements_in_span(params->mcache_allocations[plane_index].mcache_x_offsets_plane0,
			num_boundaries, max_per_pipe_vp_p0) <= 1) && (num_boundaries <= num_dpps)) {
			p0pass = true;
		}
		num_boundaries = params->mcache_allocations[plane_index].num_mcaches_plane1 == 0 ? 0 : params->mcache_allocations[plane_index].num_mcaches_plane1 - 1;
		if ((count_elements_in_span(params->mcache_allocations[plane_index].mcache_x_offsets_plane1,
			num_boundaries, max_per_pipe_vp_p1) <= 1) && (num_boundaries <= num_dpps)) {
			p1pass = true;
		}

		if (!p0pass || !p1pass) {
			if (odm_combine_factor > 1) {
				num_dpps = odm_combine_factor;
				scaling_transform = plane->composition.scaling_transform;
			} else {
				num_dpps = mpc_combine_factor;
				scaling_transform = dml2_scaling_transform_fullscreen;
			}

			if (!p0pass) {
				if (plane->composition.viewport.stationary) {
					calculate_h_split_for_scaling_transform(plane->surface.plane0.width,
						stream->timing.h_active, num_dpps, scaling_transform,
						&l->plane0.pipe_vp_startx[plane_index], &l->plane0.pipe_vp_endx[plane_index]);
					p0pass = find_shift_for_valid_cache_id_assignment(params->mcache_allocations[plane_index].mcache_x_offsets_plane0,
						params->mcache_allocations[plane_index].num_mcaches_plane0,
						&l->plane0.pipe_vp_startx[plane_index], &l->plane0.pipe_vp_endx[plane_index], num_dpps,
						params->mcache_allocations[plane_index].shift_granularity.p0, &p0shift);
				}
			}
			if (!p1pass) {
				if (plane->composition.viewport.stationary) {
					calculate_h_split_for_scaling_transform(plane->surface.plane1.width,
						stream->timing.h_active, num_dpps, scaling_transform,
						&l->plane0.pipe_vp_startx[plane_index], &l->plane0.pipe_vp_endx[plane_index]);
					p1pass = find_shift_for_valid_cache_id_assignment(params->mcache_allocations[plane_index].mcache_x_offsets_plane1,
						params->mcache_allocations[plane_index].num_mcaches_plane1,
						&l->plane1.pipe_vp_startx[plane_index], &l->plane1.pipe_vp_endx[plane_index], num_dpps,
						params->mcache_allocations[plane_index].shift_granularity.p1, &p1shift);
				}
			}
		}

		if (p0pass && p1pass) {
			for (i = 0; i < params->mcache_allocations[plane_index].num_mcaches_plane0; i++) {
				params->mcache_allocations[plane_index].mcache_x_offsets_plane0[i] -= p0shift;
			}
			for (i = 0; i < params->mcache_allocations[plane_index].num_mcaches_plane1; i++) {
				params->mcache_allocations[plane_index].mcache_x_offsets_plane1[i] -= p1shift;
			}
		}

		params->per_plane_status[plane_index] = p0pass && p1pass;
		all_pass &= p0pass && p1pass;
	}

	return all_pass;
}

static void reset_mcache_allocations(struct dml2_hubp_pipe_mcache_regs *per_plane_pipe_mcache_regs)
{
	// Initialize all entries to special valid MCache ID and special valid split coordinate
	per_plane_pipe_mcache_regs->main.p0.mcache_id_first = MCACHE_ID_UNASSIGNED;
	per_plane_pipe_mcache_regs->main.p0.mcache_id_second = MCACHE_ID_UNASSIGNED;
	per_plane_pipe_mcache_regs->main.p0.split_location = SPLIT_LOCATION_UNDEFINED;

	per_plane_pipe_mcache_regs->mall.p0.mcache_id_first = MCACHE_ID_UNASSIGNED;
	per_plane_pipe_mcache_regs->mall.p0.mcache_id_second = MCACHE_ID_UNASSIGNED;
	per_plane_pipe_mcache_regs->mall.p0.split_location = SPLIT_LOCATION_UNDEFINED;

	per_plane_pipe_mcache_regs->main.p1.mcache_id_first = MCACHE_ID_UNASSIGNED;
	per_plane_pipe_mcache_regs->main.p1.mcache_id_second = MCACHE_ID_UNASSIGNED;
	per_plane_pipe_mcache_regs->main.p1.split_location = SPLIT_LOCATION_UNDEFINED;

	per_plane_pipe_mcache_regs->mall.p1.mcache_id_first = MCACHE_ID_UNASSIGNED;
	per_plane_pipe_mcache_regs->mall.p1.mcache_id_second = MCACHE_ID_UNASSIGNED;
	per_plane_pipe_mcache_regs->mall.p1.split_location = SPLIT_LOCATION_UNDEFINED;
}

void dml2_top_mcache_assign_global_mcache_ids(struct top_mcache_assign_global_mcache_ids_in_out *params)
{
	int i;
	unsigned int j;
	int next_unused_cache_id = 0;

	for (i = 0; i < params->num_allocations; i++) {
		if (!params->allocations[i].valid)
			continue;

		for (j = 0; j < params->allocations[i].num_mcaches_plane0; j++) {
			params->allocations[i].global_mcache_ids_plane0[j] = next_unused_cache_id++;
		}
		for (j = 0; j < params->allocations[i].num_mcaches_plane1; j++) {
			params->allocations[i].global_mcache_ids_plane1[j] = next_unused_cache_id++;
		}

		// The "psuedo-last" slice is always wrapped around
		params->allocations[i].global_mcache_ids_plane0[params->allocations[i].num_mcaches_plane0] =
			params->allocations[i].global_mcache_ids_plane0[0];
		params->allocations[i].global_mcache_ids_plane1[params->allocations[i].num_mcaches_plane1] =
			params->allocations[i].global_mcache_ids_plane1[0];

		// If we need dedicated caches for mall requesting, then we assign them here.
		if (params->allocations[i].requires_dedicated_mall_mcache) {
			for (j = 0; j < params->allocations[i].num_mcaches_plane0; j++) {
				params->allocations[i].global_mcache_ids_mall_plane0[j] = next_unused_cache_id++;
			}
			for (j = 0; j < params->allocations[i].num_mcaches_plane1; j++) {
				params->allocations[i].global_mcache_ids_mall_plane1[j] = next_unused_cache_id++;
			}

			// The "psuedo-last" slice is always wrapped around
			params->allocations[i].global_mcache_ids_mall_plane0[params->allocations[i].num_mcaches_plane0] =
				params->allocations[i].global_mcache_ids_mall_plane0[0];
			params->allocations[i].global_mcache_ids_mall_plane1[params->allocations[i].num_mcaches_plane1] =
				params->allocations[i].global_mcache_ids_mall_plane1[0];
		}

		// If P0 and P1 are sharing caches, then it means the largest mcache IDs for p0 and p1 can be the same
		// since mcache IDs are always ascending, then it means the largest mcacheID of p1 should be the
		// largest mcacheID of P0
		if (params->allocations[i].num_mcaches_plane0 > 0 && params->allocations[i].num_mcaches_plane1 > 0 &&
			params->allocations[i].last_slice_sharing.plane0_plane1) {
			params->allocations[i].global_mcache_ids_plane1[params->allocations[i].num_mcaches_plane1 - 1] =
				params->allocations[i].global_mcache_ids_plane0[params->allocations[i].num_mcaches_plane0 - 1];
		}

		// If we need dedicated caches handle last slice sharing
		if (params->allocations[i].requires_dedicated_mall_mcache) {
			if (params->allocations[i].num_mcaches_plane0 > 0 && params->allocations[i].num_mcaches_plane1 > 0 &&
				params->allocations[i].last_slice_sharing.plane0_plane1) {
				params->allocations[i].global_mcache_ids_mall_plane1[params->allocations[i].num_mcaches_plane1 - 1] =
					params->allocations[i].global_mcache_ids_mall_plane0[params->allocations[i].num_mcaches_plane0 - 1];
			}
			// If mall_comb_mcache_l is set then it means that largest mcache ID for MALL p0 can be same as regular read p0
			if (params->allocations[i].num_mcaches_plane0 > 0 && params->allocations[i].last_slice_sharing.mall_comb_mcache_p0) {
				params->allocations[i].global_mcache_ids_mall_plane0[params->allocations[i].num_mcaches_plane0 - 1] =
					params->allocations[i].global_mcache_ids_plane0[params->allocations[i].num_mcaches_plane0 - 1];
			}
			// If mall_comb_mcache_c is set then it means that largest mcache ID for MALL p1 can be same as regular
			// read p1 (which can be same as regular read p0 if plane0_plane1 is also set)
			if (params->allocations[i].num_mcaches_plane1 > 0 && params->allocations[i].last_slice_sharing.mall_comb_mcache_p1) {
				params->allocations[i].global_mcache_ids_mall_plane1[params->allocations[i].num_mcaches_plane1 - 1] =
					params->allocations[i].global_mcache_ids_plane1[params->allocations[i].num_mcaches_plane1 - 1];
			}
		}

		// If you don't need dedicated mall mcaches, the mall mcache assignments are identical to the normal requesting
		if (!params->allocations[i].requires_dedicated_mall_mcache) {
			memcpy(params->allocations[i].global_mcache_ids_mall_plane0, params->allocations[i].global_mcache_ids_plane0,
				sizeof(params->allocations[i].global_mcache_ids_mall_plane0));
			memcpy(params->allocations[i].global_mcache_ids_mall_plane1, params->allocations[i].global_mcache_ids_plane1,
				sizeof(params->allocations[i].global_mcache_ids_mall_plane1));
		}
	}
}

bool dml2_top_mcache_calc_mcache_count_and_offsets(struct top_mcache_calc_mcache_count_and_offsets_in_out *params)
{
	struct dml2_instance *dml = (struct dml2_instance *)params->dml2_instance;
	struct dml2_top_mcache_verify_mcache_size_locals *l = &dml->scratch.mcache_verify_mcache_size_locals;

	unsigned int total_mcaches_required;
	unsigned int i;
	bool result = false;

	if (dml->soc_bbox.num_dcc_mcaches == 0) {
		return true;
	}

	total_mcaches_required = 0;
	l->calc_mcache_params.instance = &dml->core_instance;
	for (i = 0; i < params->display_config->num_planes; i++) {
		if (!params->display_config->plane_descriptors[i].surface.dcc.enable) {
			memset(&params->mcache_allocations[i], 0, sizeof(struct dml2_mcache_surface_allocation));
			continue;
		}

		l->calc_mcache_params.plane_descriptor = &params->display_config->plane_descriptors[i];
		l->calc_mcache_params.mcache_allocation = &params->mcache_allocations[i];
		l->calc_mcache_params.plane_index = i;

		if (!dml->core_instance.calculate_mcache_allocation(&l->calc_mcache_params)) {
			result = false;
			break;
		}

		if (params->mcache_allocations[i].valid) {
			total_mcaches_required += params->mcache_allocations[i].num_mcaches_plane0 + params->mcache_allocations[i].num_mcaches_plane1;
			if (params->mcache_allocations[i].last_slice_sharing.plane0_plane1)
				total_mcaches_required--;
		}
	}
	dml2_printf("DML_CORE_DCN3::%s: plane_%d, total_mcaches_required=%d\n", __func__, i, total_mcaches_required);

	if (total_mcaches_required > dml->soc_bbox.num_dcc_mcaches) {
		result = false;
	} else {
		result = true;
	}

	return result;
}

static bool dml2_top_soc15_check_mode_supported(struct dml2_check_mode_supported_in_out *in_out)
{
	struct dml2_instance *dml = (struct dml2_instance *)in_out->dml2_instance;
	struct dml2_check_mode_supported_locals *l = &dml->scratch.check_mode_supported_locals;
	struct dml2_display_cfg_programming *dpmm_programming = &dml->dpmm_instance.dpmm_scratch.programming;

	bool result = false;
	bool mcache_success = false;
	memset(dpmm_programming, 0, sizeof(struct dml2_display_cfg_programming));

	setup_unoptimized_display_config_with_meta(dml, &l->base_display_config_with_meta, in_out->display_config);

	l->mode_support_params.instance = &dml->core_instance;
	l->mode_support_params.display_cfg = &l->base_display_config_with_meta;
	l->mode_support_params.min_clk_table = &dml->min_clk_table;
	l->mode_support_params.min_clk_index = l->base_display_config_with_meta.stage1.min_clk_index_for_latency;
	result = dml->core_instance.mode_support(&l->mode_support_params);
	l->base_display_config_with_meta.mode_support_result = l->mode_support_params.mode_support_result;

	if (result) {
		struct optimization_phase_params mcache_phase =	{
		.dml = dml,
		.display_config = &l->base_display_config_with_meta,
		.test_function = dml2_top_optimization_test_function_mcache,
		.optimize_function = dml2_top_optimization_optimize_function_mcache,
		.optimized_display_config = &l->optimized_display_config_with_meta,
		.all_or_nothing = false,
		};
		mcache_success = dml2_top_optimization_perform_optimization_phase(&l->optimization_phase_locals, &mcache_phase);
	}

	/*
	* Call DPMM to map all requirements to minimum clock state
	*/
	if (result) {
		l->dppm_map_mode_params.min_clk_table = &dml->min_clk_table;
		l->dppm_map_mode_params.display_cfg = &l->base_display_config_with_meta;
		l->dppm_map_mode_params.programming = dpmm_programming;
		l->dppm_map_mode_params.soc_bb = &dml->soc_bbox;
		l->dppm_map_mode_params.ip = &dml->core_instance.clean_me_up.mode_lib.ip;
		result = dml->dpmm_instance.map_mode_to_soc_dpm(&l->dppm_map_mode_params);
	}

	in_out->is_supported = mcache_success;
	result = result && in_out->is_supported;

	return result;
}

static bool dml2_top_soc15_build_mode_programming(struct dml2_build_mode_programming_in_out *in_out)
{
	struct dml2_instance *dml = (struct dml2_instance *)in_out->dml2_instance;
	struct dml2_build_mode_programming_locals *l = &dml->scratch.build_mode_programming_locals;

	bool result = false;
	bool mcache_success = false;
	bool uclk_pstate_success = false;
	bool vmin_success = false;
	bool stutter_success = false;
	unsigned int i;

	memset(l, 0, sizeof(struct dml2_build_mode_programming_locals));
	memset(in_out->programming, 0, sizeof(struct dml2_display_cfg_programming));

	memcpy(&in_out->programming->display_config, in_out->display_config, sizeof(struct dml2_display_cfg));

	setup_speculative_display_config_with_meta(dml, &l->base_display_config_with_meta, in_out->display_config);

	l->mode_support_params.instance = &dml->core_instance;
	l->mode_support_params.display_cfg = &l->base_display_config_with_meta;
	l->mode_support_params.min_clk_table = &dml->min_clk_table;
	l->mode_support_params.min_clk_index = l->base_display_config_with_meta.stage1.min_clk_index_for_latency;
	result = dml->core_instance.mode_support(&l->mode_support_params);

	l->base_display_config_with_meta.mode_support_result = l->mode_support_params.mode_support_result;

	if (!result) {
		setup_unoptimized_display_config_with_meta(dml, &l->base_display_config_with_meta, in_out->display_config);

		l->mode_support_params.instance = &dml->core_instance;
		l->mode_support_params.display_cfg = &l->base_display_config_with_meta;
		l->mode_support_params.min_clk_table = &dml->min_clk_table;
		l->mode_support_params.min_clk_index = l->base_display_config_with_meta.stage1.min_clk_index_for_latency;
		result = dml->core_instance.mode_support(&l->mode_support_params);
		l->base_display_config_with_meta.mode_support_result = l->mode_support_params.mode_support_result;

		if (!result) {
			l->informative_params.instance = &dml->core_instance;
			l->informative_params.programming = in_out->programming;
			l->informative_params.mode_is_supported = false;
			dml->core_instance.populate_informative(&l->informative_params);

			return false;
		}

		/*
		* Phase 1: Determine minimum clocks to satisfy latency requirements for this mode
		*/
		memset(&l->min_clock_for_latency_phase, 0, sizeof(struct optimization_phase_params));
		l->min_clock_for_latency_phase.dml = dml;
		l->min_clock_for_latency_phase.display_config = &l->base_display_config_with_meta;
		l->min_clock_for_latency_phase.init_function = dml2_top_optimization_init_function_min_clk_for_latency;
		l->min_clock_for_latency_phase.test_function = dml2_top_optimization_test_function_min_clk_for_latency;
		l->min_clock_for_latency_phase.optimize_function = dml2_top_optimization_optimize_function_min_clk_for_latency;
		l->min_clock_for_latency_phase.optimized_display_config = &l->optimized_display_config_with_meta;
		l->min_clock_for_latency_phase.all_or_nothing = false;

		dml2_top_optimization_perform_optimization_phase_1(&l->optimization_phase_locals, &l->min_clock_for_latency_phase);

		memcpy(&l->base_display_config_with_meta, &l->optimized_display_config_with_meta, sizeof(struct display_configuation_with_meta));
	}

	/*
	* Phase 2: Satisfy DCC mcache requirements
	*/
	memset(&l->mcache_phase, 0, sizeof(struct optimization_phase_params));
	l->mcache_phase.dml = dml;
	l->mcache_phase.display_config = &l->base_display_config_with_meta;
	l->mcache_phase.test_function = dml2_top_optimization_test_function_mcache;
	l->mcache_phase.optimize_function = dml2_top_optimization_optimize_function_mcache;
	l->mcache_phase.optimized_display_config = &l->optimized_display_config_with_meta;
	l->mcache_phase.all_or_nothing = true;

	mcache_success = dml2_top_optimization_perform_optimization_phase(&l->optimization_phase_locals, &l->mcache_phase);

	if (!mcache_success) {
		l->informative_params.instance = &dml->core_instance;
		l->informative_params.programming = in_out->programming;
		l->informative_params.mode_is_supported = false;

		dml->core_instance.populate_informative(&l->informative_params);

		in_out->programming->informative.failed_mcache_validation = true;
		return false;
	}

	memcpy(&l->base_display_config_with_meta, &l->optimized_display_config_with_meta, sizeof(struct display_configuation_with_meta));

	/*
	* Phase 3: Optimize for Pstate
	*/
	memset(&l->uclk_pstate_phase, 0, sizeof(struct optimization_phase_params));
	l->uclk_pstate_phase.dml = dml;
	l->uclk_pstate_phase.display_config = &l->base_display_config_with_meta;
	l->uclk_pstate_phase.init_function = dml2_top_optimization_init_function_uclk_pstate;
	l->uclk_pstate_phase.test_function = dml2_top_optimization_test_function_uclk_pstate;
	l->uclk_pstate_phase.optimize_function = dml2_top_optimization_optimize_function_uclk_pstate;
	l->uclk_pstate_phase.optimized_display_config = &l->optimized_display_config_with_meta;
	l->uclk_pstate_phase.all_or_nothing = true;

	uclk_pstate_success = dml2_top_optimization_perform_optimization_phase(&l->optimization_phase_locals, &l->uclk_pstate_phase);

	if (uclk_pstate_success) {
		memcpy(&l->base_display_config_with_meta, &l->optimized_display_config_with_meta, sizeof(struct display_configuation_with_meta));
		l->base_display_config_with_meta.stage3.success = true;
	}

	/*
	* Phase 4: Optimize for Vmin
	*/
	memset(&l->vmin_phase, 0, sizeof(struct optimization_phase_params));
	l->vmin_phase.dml = dml;
	l->vmin_phase.display_config = &l->base_display_config_with_meta;
	l->vmin_phase.init_function = dml2_top_optimization_init_function_vmin;
	l->vmin_phase.test_function = dml2_top_optimization_test_function_vmin;
	l->vmin_phase.optimize_function = dml2_top_optimization_optimize_function_vmin;
	l->vmin_phase.optimized_display_config = &l->optimized_display_config_with_meta;
	l->vmin_phase.all_or_nothing = false;

	vmin_success = dml2_top_optimization_perform_optimization_phase(&l->optimization_phase_locals, &l->vmin_phase);

	if (l->optimized_display_config_with_meta.stage4.performed) {
		/*
		 * when performed is true, optimization has applied to
		 * optimized_display_config_with_meta and it has passed mode
		 * support. However it may or may not pass the test function to
		 * reach actual Vmin. As long as voltage is optimized even if it
		 * doesn't reach Vmin level, there is still power benefit so in
		 * this case we will still copy this optimization into base
		 * display config.
		 */
		memcpy(&l->base_display_config_with_meta, &l->optimized_display_config_with_meta, sizeof(struct display_configuation_with_meta));
		l->base_display_config_with_meta.stage4.success = vmin_success;
	}

	/*
	* Phase 5: Optimize for Stutter
	*/
	memset(&l->stutter_phase, 0, sizeof(struct optimization_phase_params));
	l->stutter_phase.dml = dml;
	l->stutter_phase.display_config = &l->base_display_config_with_meta;
	l->stutter_phase.init_function = dml2_top_optimization_init_function_stutter;
	l->stutter_phase.test_function = dml2_top_optimization_test_function_stutter;
	l->stutter_phase.optimize_function = dml2_top_optimization_optimize_function_stutter;
	l->stutter_phase.optimized_display_config = &l->optimized_display_config_with_meta;
	l->stutter_phase.all_or_nothing = true;

	stutter_success = dml2_top_optimization_perform_optimization_phase(&l->optimization_phase_locals, &l->stutter_phase);

	if (stutter_success) {
		memcpy(&l->base_display_config_with_meta, &l->optimized_display_config_with_meta, sizeof(struct display_configuation_with_meta));
		l->base_display_config_with_meta.stage5.success = true;
	}

	/*
	* Populate mcache programming
	*/
	for (i = 0; i < in_out->display_config->num_planes; i++) {
		in_out->programming->plane_programming[i].mcache_allocation = l->base_display_config_with_meta.stage2.mcache_allocations[i];
	}

	/*
	* Call DPMM to map all requirements to minimum clock state
	*/
	if (result) {
		l->dppm_map_mode_params.min_clk_table = &dml->min_clk_table;
		l->dppm_map_mode_params.display_cfg = &l->base_display_config_with_meta;
		l->dppm_map_mode_params.programming = in_out->programming;
		l->dppm_map_mode_params.soc_bb = &dml->soc_bbox;
		l->dppm_map_mode_params.ip = &dml->core_instance.clean_me_up.mode_lib.ip;
		result = dml->dpmm_instance.map_mode_to_soc_dpm(&l->dppm_map_mode_params);
		if (!result)
			in_out->programming->informative.failed_dpmm = true;
	}

	if (result) {
		l->mode_programming_params.instance = &dml->core_instance;
		l->mode_programming_params.display_cfg = &l->base_display_config_with_meta;
		l->mode_programming_params.cfg_support_info = &l->base_display_config_with_meta.mode_support_result.cfg_support_info;
		l->mode_programming_params.programming = in_out->programming;
		result = dml->core_instance.mode_programming(&l->mode_programming_params);
		if (!result)
			in_out->programming->informative.failed_mode_programming = true;
	}

	if (result) {
		l->dppm_map_watermarks_params.core = &dml->core_instance;
		l->dppm_map_watermarks_params.display_cfg = &l->base_display_config_with_meta;
		l->dppm_map_watermarks_params.programming = in_out->programming;
		result = dml->dpmm_instance.map_watermarks(&l->dppm_map_watermarks_params);
	}

	l->informative_params.instance = &dml->core_instance;
	l->informative_params.programming = in_out->programming;
	l->informative_params.mode_is_supported = result;

	dml->core_instance.populate_informative(&l->informative_params);

	return result;
}

bool dml2_top_soc15_build_mcache_programming(struct dml2_build_mcache_programming_in_out *params)
{
	bool success = true;
	int config_index, pipe_index;
	int first_offset, second_offset;
	int free_per_plane_reg_index = 0;

	memset(params->per_plane_pipe_mcache_regs, 0, DML2_MAX_PLANES * DML2_MAX_DCN_PIPES * sizeof(struct dml2_hubp_pipe_mcache_regs *));

	for (config_index = 0; config_index < params->num_configurations; config_index++) {
		for (pipe_index = 0; pipe_index < params->mcache_configurations[config_index].num_pipes; pipe_index++) {
			// Allocate storage for the mcache regs
			params->per_plane_pipe_mcache_regs[config_index][pipe_index] = &params->mcache_regs_set[free_per_plane_reg_index++];

			reset_mcache_allocations(params->per_plane_pipe_mcache_regs[config_index][pipe_index]);

			if (params->mcache_configurations[config_index].plane_descriptor->surface.dcc.enable) {
				// P0 always enabled
				if (!calculate_first_second_splitting(params->mcache_configurations[config_index].mcache_allocation->mcache_x_offsets_plane0,
					params->mcache_configurations[config_index].mcache_allocation->num_mcaches_plane0,
					0,
					params->mcache_configurations[config_index].pipe_configurations[pipe_index].plane0.viewport_x_start,
					params->mcache_configurations[config_index].pipe_configurations[pipe_index].plane0.viewport_x_start +
					params->mcache_configurations[config_index].pipe_configurations[pipe_index].plane0.viewport_width - 1,
					&first_offset, &second_offset)) {
					success = false;
					break;
				}

				params->per_plane_pipe_mcache_regs[config_index][pipe_index]->main.p0.mcache_id_first =
					params->mcache_configurations[config_index].mcache_allocation->global_mcache_ids_plane0[first_offset];

				params->per_plane_pipe_mcache_regs[config_index][pipe_index]->mall.p0.mcache_id_first =
					params->mcache_configurations[config_index].mcache_allocation->global_mcache_ids_mall_plane0[first_offset];

				if (second_offset >= 0) {
					params->per_plane_pipe_mcache_regs[config_index][pipe_index]->main.p0.mcache_id_second =
						params->mcache_configurations[config_index].mcache_allocation->global_mcache_ids_plane0[second_offset];
					params->per_plane_pipe_mcache_regs[config_index][pipe_index]->main.p0.split_location =
						params->mcache_configurations[config_index].mcache_allocation->mcache_x_offsets_plane0[first_offset] - 1;

					params->per_plane_pipe_mcache_regs[config_index][pipe_index]->mall.p0.mcache_id_second =
						params->mcache_configurations[config_index].mcache_allocation->global_mcache_ids_mall_plane0[second_offset];
					params->per_plane_pipe_mcache_regs[config_index][pipe_index]->mall.p0.split_location =
						params->mcache_configurations[config_index].mcache_allocation->mcache_x_offsets_plane0[first_offset] - 1;
				}

				// Populate P1 if enabled
				if (params->mcache_configurations[config_index].pipe_configurations[pipe_index].plane1_enabled) {
					if (!calculate_first_second_splitting(params->mcache_configurations[config_index].mcache_allocation->mcache_x_offsets_plane1,
						params->mcache_configurations[config_index].mcache_allocation->num_mcaches_plane1,
						0,
						params->mcache_configurations[config_index].pipe_configurations[pipe_index].plane1.viewport_x_start,
						params->mcache_configurations[config_index].pipe_configurations[pipe_index].plane1.viewport_x_start +
						params->mcache_configurations[config_index].pipe_configurations[pipe_index].plane1.viewport_width - 1,
						&first_offset, &second_offset)) {
						success = false;
						break;
					}

					params->per_plane_pipe_mcache_regs[config_index][pipe_index]->main.p1.mcache_id_first =
						params->mcache_configurations[config_index].mcache_allocation->global_mcache_ids_plane1[first_offset];

					params->per_plane_pipe_mcache_regs[config_index][pipe_index]->mall.p1.mcache_id_first =
						params->mcache_configurations[config_index].mcache_allocation->global_mcache_ids_mall_plane1[first_offset];

					if (second_offset >= 0) {
						params->per_plane_pipe_mcache_regs[config_index][pipe_index]->main.p1.mcache_id_second =
							params->mcache_configurations[config_index].mcache_allocation->global_mcache_ids_plane1[second_offset];
						params->per_plane_pipe_mcache_regs[config_index][pipe_index]->main.p1.split_location =
							params->mcache_configurations[config_index].mcache_allocation->mcache_x_offsets_plane1[first_offset] - 1;

						params->per_plane_pipe_mcache_regs[config_index][pipe_index]->mall.p1.mcache_id_second =
							params->mcache_configurations[config_index].mcache_allocation->global_mcache_ids_mall_plane1[second_offset];
						params->per_plane_pipe_mcache_regs[config_index][pipe_index]->mall.p1.split_location =
							params->mcache_configurations[config_index].mcache_allocation->mcache_x_offsets_plane1[first_offset] - 1;
					}
				}
			}
		}
	}

	return success;
}

static const struct dml2_top_funcs soc15_funcs = {
	.check_mode_supported = dml2_top_soc15_check_mode_supported,
	.build_mode_programming = dml2_top_soc15_build_mode_programming,
	.build_mcache_programming = dml2_top_soc15_build_mcache_programming,
};

bool dml2_top_soc15_initialize_instance(struct dml2_initialize_instance_in_out *in_out)
{
	struct dml2_instance *dml = (struct dml2_instance *)in_out->dml2_instance;
	struct dml2_initialize_instance_locals *l = &dml->scratch.initialize_instance_locals;
	struct dml2_core_initialize_in_out core_init_params = { 0 };
	struct dml2_mcg_build_min_clock_table_params_in_out mcg_build_min_clk_params = { 0 };
	struct dml2_pmo_initialize_in_out pmo_init_params = { 0 };
	bool result = false;

	memset(l, 0, sizeof(struct dml2_initialize_instance_locals));
	memset(dml, 0, sizeof(struct dml2_instance));

	memcpy(&dml->ip_caps, &in_out->ip_caps, sizeof(struct dml2_ip_capabilities));
	memcpy(&dml->soc_bbox, &in_out->soc_bb, sizeof(struct dml2_soc_bb));

	dml->project_id = in_out->options.project_id;
	dml->pmo_options = in_out->options.pmo_options;

	// Initialize All Components
	result = dml2_mcg_create(in_out->options.project_id, &dml->mcg_instance);

	if (result)
		result = dml2_dpmm_create(in_out->options.project_id, &dml->dpmm_instance);

	if (result)
		result = dml2_core_create(in_out->options.project_id, &dml->core_instance);

	if (result) {
		mcg_build_min_clk_params.soc_bb = &in_out->soc_bb;
		mcg_build_min_clk_params.min_clk_table = &dml->min_clk_table;
		result = dml->mcg_instance.build_min_clock_table(&mcg_build_min_clk_params);
	}

	if (result) {
		core_init_params.project_id = in_out->options.project_id;
		core_init_params.instance = &dml->core_instance;
		core_init_params.minimum_clock_table = &dml->min_clk_table;
		core_init_params.explicit_ip_bb = in_out->overrides.explicit_ip_bb;
		core_init_params.explicit_ip_bb_size = in_out->overrides.explicit_ip_bb_size;
		core_init_params.ip_caps = &in_out->ip_caps;
		core_init_params.soc_bb = &in_out->soc_bb;
		result = dml->core_instance.initialize(&core_init_params);

		if (core_init_params.explicit_ip_bb && core_init_params.explicit_ip_bb_size > 0) {
			memcpy(&dml->ip_caps, &in_out->ip_caps, sizeof(struct dml2_ip_capabilities));
		}
	}

	if (result)
		result = dml2_pmo_create(in_out->options.project_id, &dml->pmo_instance);

	if (result) {
		pmo_init_params.instance = &dml->pmo_instance;
		pmo_init_params.soc_bb = &dml->soc_bbox;
		pmo_init_params.ip_caps = &dml->ip_caps;
		pmo_init_params.mcg_clock_table_size = dml->min_clk_table.dram_bw_table.num_entries;
		pmo_init_params.options = &dml->pmo_options;
		dml->pmo_instance.initialize(&pmo_init_params);
	}
	dml->funcs = soc15_funcs;
	return result;
}
