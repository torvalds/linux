// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.


#include "dml2_debug.h"

#include "dml_top_mcache.h"
#include "lib_float_math.h"

#include "dml2_internal_shared_types.h"

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
			if (array[i] - span_start_value > span) {
				if (i - span_start_index + 1 > greatest_element_count) {
					greatest_element_count = i - span_start_index + 1;
				}
				break;
			}
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
	char odm_combine_factor = 1;
	char mpc_combine_factor = 1;
	char num_dpps;
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

		odm_combine_factor = (char)params->cfg_support_info->stream_support_info[plane->stream_index].odms_used;

		if (odm_combine_factor == 1)
			mpc_combine_factor = (char)params->cfg_support_info->plane_support_info[plane_index].dpps_used;
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
		if (count_elements_in_span(params->mcache_allocations[plane_index].mcache_x_offsets_plane0,
			num_boundaries, max_per_pipe_vp_p0) <= 1) {
			p0pass = true;
		}
		num_boundaries = params->mcache_allocations[plane_index].num_mcaches_plane1 == 0 ? 0 : params->mcache_allocations[plane_index].num_mcaches_plane1 - 1;
		if (count_elements_in_span(params->mcache_allocations[plane_index].mcache_x_offsets_plane1,
			num_boundaries, max_per_pipe_vp_p1) <= 1) {
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

bool dml2_top_mcache_build_mcache_programming(struct dml2_build_mcache_programming_in_out *params)
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
