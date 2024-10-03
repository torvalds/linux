// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#include "dml2_pmo_factory.h"
#include "dml2_debug.h"
#include "lib_float_math.h"
#include "dml2_pmo_dcn4_fams2.h"

static const double MIN_VACTIVE_MARGIN_PCT = 0.25; // We need more than non-zero margin because DET buffer granularity can alter vactive latency hiding

static const struct dml2_pmo_pstate_strategy base_strategy_list_1_display[] = {
	// VActive Preferred
	{
		.per_stream_pstate_method = { dml2_pmo_pstate_strategy_vactive, dml2_pmo_pstate_strategy_na, dml2_pmo_pstate_strategy_na, dml2_pmo_pstate_strategy_na },
		.allow_state_increase = true,
	},

	// Then SVP
	{
		.per_stream_pstate_method = { dml2_pmo_pstate_strategy_fw_svp, dml2_pmo_pstate_strategy_na, dml2_pmo_pstate_strategy_na, dml2_pmo_pstate_strategy_na },
		.allow_state_increase = true,
	},

	// Then VBlank
	{
		.per_stream_pstate_method = { dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_na, dml2_pmo_pstate_strategy_na, dml2_pmo_pstate_strategy_na },
		.allow_state_increase = false,
	},

	// Then DRR
	{
		.per_stream_pstate_method = { dml2_pmo_pstate_strategy_fw_drr, dml2_pmo_pstate_strategy_na, dml2_pmo_pstate_strategy_na, dml2_pmo_pstate_strategy_na },
		.allow_state_increase = true,
	},

	// Finally VBlank, but allow base clocks for latency to increase
	/*
	{
		.per_stream_pstate_method = { dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_na, dml2_pmo_pstate_strategy_na, dml2_pmo_pstate_strategy_na },
		.allow_state_increase = true,
	},
	*/
};

static const int base_strategy_list_1_display_size = sizeof(base_strategy_list_1_display) / sizeof(struct dml2_pmo_pstate_strategy);

static const struct dml2_pmo_pstate_strategy base_strategy_list_2_display[] = {
	// VActive only is preferred
	{
		.per_stream_pstate_method = { dml2_pmo_pstate_strategy_vactive, dml2_pmo_pstate_strategy_vactive, dml2_pmo_pstate_strategy_na, dml2_pmo_pstate_strategy_na },
		.allow_state_increase = true,
	},

	// Then VActive + VBlank
	{
		.per_stream_pstate_method = { dml2_pmo_pstate_strategy_vactive, dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_na, dml2_pmo_pstate_strategy_na },
		.allow_state_increase = false,
	},

	// Then VBlank only
	{
		.per_stream_pstate_method = { dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_na, dml2_pmo_pstate_strategy_na },
		.allow_state_increase = false,
	},

	// Then SVP + VBlank
	{
		.per_stream_pstate_method = { dml2_pmo_pstate_strategy_fw_svp, dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_na, dml2_pmo_pstate_strategy_na },
		.allow_state_increase = false,
	},

	// Then SVP + DRR
	{
		.per_stream_pstate_method = { dml2_pmo_pstate_strategy_fw_svp, dml2_pmo_pstate_strategy_fw_drr, dml2_pmo_pstate_strategy_na, dml2_pmo_pstate_strategy_na },
		.allow_state_increase = true,
	},

	// Then SVP + SVP
	{
		.per_stream_pstate_method = { dml2_pmo_pstate_strategy_fw_svp, dml2_pmo_pstate_strategy_fw_svp, dml2_pmo_pstate_strategy_na, dml2_pmo_pstate_strategy_na },
		.allow_state_increase = true,
	},

	// Then DRR + VActive
	{
		.per_stream_pstate_method = { dml2_pmo_pstate_strategy_vactive, dml2_pmo_pstate_strategy_fw_drr, dml2_pmo_pstate_strategy_na, dml2_pmo_pstate_strategy_na },
		.allow_state_increase = true,
	},

	// Then DRR + DRR
	{
		.per_stream_pstate_method = { dml2_pmo_pstate_strategy_fw_drr, dml2_pmo_pstate_strategy_fw_drr, dml2_pmo_pstate_strategy_na, dml2_pmo_pstate_strategy_na },
		.allow_state_increase = true,
	},

	// Finally VBlank, but allow base clocks for latency to increase
	/*
	{
		.per_stream_pstate_method = { dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_na, dml2_pmo_pstate_strategy_na },
		.allow_state_increase = true,
	},
	*/
};

static const int base_strategy_list_2_display_size = sizeof(base_strategy_list_2_display) / sizeof(struct dml2_pmo_pstate_strategy);

static const struct dml2_pmo_pstate_strategy base_strategy_list_3_display[] = {
	// All VActive
	{
		.per_stream_pstate_method = { dml2_pmo_pstate_strategy_vactive, dml2_pmo_pstate_strategy_vactive, dml2_pmo_pstate_strategy_vactive, dml2_pmo_pstate_strategy_na },
		.allow_state_increase = true,
	},

	// VActive + 1 VBlank
	{
		.per_stream_pstate_method = { dml2_pmo_pstate_strategy_vactive, dml2_pmo_pstate_strategy_vactive, dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_na },
		.allow_state_increase = false,
	},

	// All VBlank
	{
		.per_stream_pstate_method = { dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_na },
		.allow_state_increase = false,
	},

	// All DRR
	{
		.per_stream_pstate_method = { dml2_pmo_pstate_strategy_fw_drr, dml2_pmo_pstate_strategy_fw_drr, dml2_pmo_pstate_strategy_fw_drr, dml2_pmo_pstate_strategy_na },
		.allow_state_increase = true,
	},

	// All VBlank, with state increase allowed
	/*
	{
		.per_stream_pstate_method = { dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_na },
		.allow_state_increase = true,
	},
	*/
};

static const int base_strategy_list_3_display_size = sizeof(base_strategy_list_3_display) / sizeof(struct dml2_pmo_pstate_strategy);

static const struct dml2_pmo_pstate_strategy base_strategy_list_4_display[] = {
	// All VActive
	{
		.per_stream_pstate_method = { dml2_pmo_pstate_strategy_vactive, dml2_pmo_pstate_strategy_vactive, dml2_pmo_pstate_strategy_vactive, dml2_pmo_pstate_strategy_vactive },
		.allow_state_increase = true,
	},

	// VActive + 1 VBlank
	{
		.per_stream_pstate_method = { dml2_pmo_pstate_strategy_vactive, dml2_pmo_pstate_strategy_vactive, dml2_pmo_pstate_strategy_vactive, dml2_pmo_pstate_strategy_vblank },
		.allow_state_increase = false,
	},

	// All Vblank
	{
		.per_stream_pstate_method = { dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_vblank },
		.allow_state_increase = false,
	},

	// All DRR
	{
		.per_stream_pstate_method = { dml2_pmo_pstate_strategy_fw_drr, dml2_pmo_pstate_strategy_fw_drr, dml2_pmo_pstate_strategy_fw_drr, dml2_pmo_pstate_strategy_fw_drr },
		.allow_state_increase = true,
	},

	// All VBlank, with state increase allowed
	/*
	{
		.per_stream_pstate_method = { dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_vblank },
		.allow_state_increase = true,
	},
	*/
};

static const int base_strategy_list_4_display_size = sizeof(base_strategy_list_4_display) / sizeof(struct dml2_pmo_pstate_strategy);


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

static bool increase_mpc_combine_factor(unsigned int *mpc_combine_factor, unsigned int limit)
{
	if (*mpc_combine_factor < limit) {
		(*mpc_combine_factor)++;
		return true;
	}

	return false;
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

bool pmo_dcn4_fams2_optimize_dcc_mcache(struct dml2_pmo_optimize_dcc_mcache_in_out *in_out)
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
	if (in_out->optimized_display_cfg->num_streams > 1 || in_out->instance->options->disable_dyn_odm) {
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

static enum dml2_pmo_pstate_method convert_strategy_to_drr_variant(const enum dml2_pmo_pstate_method base_strategy)
{
	enum dml2_pmo_pstate_method variant_strategy = 0;

	switch (base_strategy) {
	case dml2_pmo_pstate_strategy_vactive:
		variant_strategy = dml2_pmo_pstate_strategy_fw_vactive_drr;
		break;
	case dml2_pmo_pstate_strategy_vblank:
		variant_strategy = dml2_pmo_pstate_strategy_fw_vblank_drr;
		break;
	case dml2_pmo_pstate_strategy_fw_svp:
		variant_strategy = dml2_pmo_pstate_strategy_fw_svp_drr;
		break;
	case dml2_pmo_pstate_strategy_fw_vactive_drr:
	case dml2_pmo_pstate_strategy_fw_vblank_drr:
	case dml2_pmo_pstate_strategy_fw_svp_drr:
	case dml2_pmo_pstate_strategy_fw_drr:
	case dml2_pmo_pstate_strategy_reserved_hw:
	case dml2_pmo_pstate_strategy_reserved_fw:
	case dml2_pmo_pstate_strategy_reserved_fw_drr_clamped:
	case dml2_pmo_pstate_strategy_reserved_fw_drr_var:
	case dml2_pmo_pstate_strategy_na:
	default:
		/* no variant for this mode */
		variant_strategy = base_strategy;
	}

	return variant_strategy;
}

static struct dml2_pmo_pstate_strategy *get_expanded_strategy_list(struct dml2_pmo_init_data *init_data, int stream_count)
{
	struct dml2_pmo_pstate_strategy *expanded_strategy_list = NULL;

	switch (stream_count) {
	case 1:
		expanded_strategy_list = init_data->pmo_dcn4.expanded_strategy_list_1_display;
		break;
	case 2:
		expanded_strategy_list = init_data->pmo_dcn4.expanded_strategy_list_2_display;
		break;
	case 3:
		expanded_strategy_list = init_data->pmo_dcn4.expanded_strategy_list_3_display;
		break;
	case 4:
		expanded_strategy_list = init_data->pmo_dcn4.expanded_strategy_list_4_display;
		break;
	default:
		break;
	}

	return expanded_strategy_list;
}

static unsigned int get_num_expanded_strategies(
	struct dml2_pmo_init_data *init_data,
	int stream_count)
{
	return init_data->pmo_dcn4.num_expanded_strategies_per_list[stream_count - 1];
}

static void insert_strategy_into_expanded_list(
	const struct dml2_pmo_pstate_strategy *per_stream_pstate_strategy,
	int stream_count,
	struct dml2_pmo_init_data *init_data)
{
	struct dml2_pmo_pstate_strategy *expanded_strategy_list = NULL;

	expanded_strategy_list = get_expanded_strategy_list(init_data, stream_count);

	if (expanded_strategy_list) {
		memcpy(&expanded_strategy_list[init_data->pmo_dcn4.num_expanded_strategies_per_list[stream_count - 1]], per_stream_pstate_strategy, sizeof(struct dml2_pmo_pstate_strategy));

		init_data->pmo_dcn4.num_expanded_strategies_per_list[stream_count - 1]++;
	}
}

static void expand_base_strategy(struct dml2_pmo_instance *pmo,
	const struct dml2_pmo_pstate_strategy *base_strategy,
	unsigned int stream_count)
{
	bool skip_to_next_stream;
	bool expanded_strategy_added;
	bool skip_iteration;
	unsigned int i, j;
	unsigned int num_streams_per_method[PMO_DCN4_MAX_DISPLAYS] = { 0 };
	unsigned int stream_iteration_indices[PMO_DCN4_MAX_DISPLAYS] = { 0 };
	struct dml2_pmo_pstate_strategy cur_strategy_list = { 0 };

	/* determine number of displays per method */
	for (i = 0; i < stream_count; i++) {
		/* increment the count of the earliest index with the same method */
		for (j = 0; j < stream_count; j++) {
			if (base_strategy->per_stream_pstate_method[i] == base_strategy->per_stream_pstate_method[j]) {
				num_streams_per_method[j] = num_streams_per_method[j] + 1;
				break;
			}
		}
	}

	cur_strategy_list.allow_state_increase = base_strategy->allow_state_increase;

	i = 0;
	/* uses a while loop instead of recursion to build permutations of base strategy */
	while (stream_iteration_indices[0] < stream_count) {
		skip_to_next_stream = false;
		expanded_strategy_added = false;
		skip_iteration = false;

		/* determine what to do for this iteration */
		if (stream_iteration_indices[i] < stream_count && num_streams_per_method[stream_iteration_indices[i]] != 0) {
			/* decrement count and assign method */
			cur_strategy_list.per_stream_pstate_method[i] = base_strategy->per_stream_pstate_method[stream_iteration_indices[i]];
			num_streams_per_method[stream_iteration_indices[i]] -= 1;

			if (i >= stream_count - 1) {
				/* insert into strategy list */
				insert_strategy_into_expanded_list(&cur_strategy_list, stream_count, &pmo->init_data);
				expanded_strategy_added = true;
			} else {
				/* skip to next stream */
				skip_to_next_stream = true;
			}
		} else {
			skip_iteration = true;
		}

		/* prepare for next iteration */
		if (skip_to_next_stream) {
			i++;
		} else {
			/* restore count */
			if (!skip_iteration) {
				num_streams_per_method[stream_iteration_indices[i]] += 1;
			}

			/* increment iteration count */
			stream_iteration_indices[i]++;

			/* if iterations are complete, or last stream was reached */
			if ((stream_iteration_indices[i] >= stream_count || expanded_strategy_added) && i > 0) {
				/* reset per stream index, decrement i */
				stream_iteration_indices[i] = 0;
				i--;

				/* restore previous stream's count and increment index */
				num_streams_per_method[stream_iteration_indices[i]] += 1;
				stream_iteration_indices[i]++;
			}
		}
	}
}


static bool is_variant_method_valid(const struct dml2_pmo_pstate_strategy *base_strategy,
		const struct dml2_pmo_pstate_strategy *variant_strategy,
		unsigned int num_streams_per_base_method[PMO_DCN4_MAX_DISPLAYS],
		unsigned int num_streams_per_variant_method[PMO_DCN4_MAX_DISPLAYS],
		unsigned int stream_count)
{
	bool valid = true;
	unsigned int i;

	/* check all restrictions are met */
	for (i = 0; i < stream_count; i++) {
		/* vblank + vblank_drr variants are invalid */
		if (base_strategy->per_stream_pstate_method[i] == dml2_pmo_pstate_strategy_vblank &&
				((num_streams_per_base_method[i] > 0 && num_streams_per_variant_method[i] > 0) ||
				num_streams_per_variant_method[i] > 1)) {
			valid = false;
			break;
		}
	}

	return valid;
}

static void expand_variant_strategy(struct dml2_pmo_instance *pmo,
		const struct dml2_pmo_pstate_strategy *base_strategy,
		unsigned int stream_count)
{
	bool variant_found;
	unsigned int i, j;
	unsigned int method_index;
	unsigned int stream_index;
	unsigned int num_streams_per_method[PMO_DCN4_MAX_DISPLAYS] = { 0 };
	unsigned int num_streams_per_base_method[PMO_DCN4_MAX_DISPLAYS] = { 0 };
	unsigned int num_streams_per_variant_method[PMO_DCN4_MAX_DISPLAYS] = { 0 };
	enum dml2_pmo_pstate_method per_stream_variant_method[DML2_MAX_PLANES];
	struct dml2_pmo_pstate_strategy variant_strategy = { 0 };

	/* determine number of displays per method */
	for (i = 0; i < stream_count; i++) {
		/* increment the count of the earliest index with the same method */
		for (j = 0; j < stream_count; j++) {
			if (base_strategy->per_stream_pstate_method[i] == base_strategy->per_stream_pstate_method[j]) {
				num_streams_per_method[j] = num_streams_per_method[j] + 1;
				break;
			}
		}

		per_stream_variant_method[i] = convert_strategy_to_drr_variant(base_strategy->per_stream_pstate_method[i]);
	}
	memcpy(num_streams_per_base_method, num_streams_per_method, sizeof(unsigned int) * PMO_DCN4_MAX_DISPLAYS);

	memcpy(&variant_strategy, base_strategy, sizeof(struct dml2_pmo_pstate_strategy));

	method_index = 0;
	/* uses a while loop instead of recursion to build permutations of base strategy */
	while (num_streams_per_base_method[0] > 0 || method_index != 0) {
		if (method_index == stream_count) {
			/* construct variant strategy */
			variant_found = false;
			stream_index = 0;

			for (i = 0; i < stream_count; i++) {
				for (j = 0; j < num_streams_per_base_method[i]; j++) {
					variant_strategy.per_stream_pstate_method[stream_index++] = base_strategy->per_stream_pstate_method[i];
				}

				for (j = 0; j < num_streams_per_variant_method[i]; j++) {
					variant_strategy.per_stream_pstate_method[stream_index++] = per_stream_variant_method[i];
					if (base_strategy->per_stream_pstate_method[i] != per_stream_variant_method[i]) {
						variant_found = true;
					}
				}
			}

			if (variant_found && is_variant_method_valid(base_strategy, &variant_strategy, num_streams_per_base_method, num_streams_per_variant_method, stream_count)) {
				expand_base_strategy(pmo, &variant_strategy, stream_count);
			}

			/* rollback to earliest method with bases remaining */
			for (method_index = stream_count - 1; method_index > 0; method_index--) {
				if (num_streams_per_base_method[method_index]) {
					/* bases remaining */
					break;
				} else {
					/* reset counters */
					num_streams_per_base_method[method_index] = num_streams_per_method[method_index];
					num_streams_per_variant_method[method_index] = 0;
				}
			}
		}

		if (num_streams_per_base_method[method_index]) {
			num_streams_per_base_method[method_index]--;
			num_streams_per_variant_method[method_index]++;

			method_index++;
		} else if (method_index != 0) {
			method_index++;
		}
	}
}

static void expand_base_strategies(
	struct dml2_pmo_instance *pmo,
	const struct dml2_pmo_pstate_strategy *base_strategies_list,
	const unsigned int num_base_strategies,
	unsigned int stream_count)
{
	unsigned int i;

	/* expand every explicit base strategy (except all DRR) */
	for (i = 0; i < num_base_strategies; i++) {
		expand_base_strategy(pmo, &base_strategies_list[i], stream_count);
		expand_variant_strategy(pmo, &base_strategies_list[i], stream_count);
	}
}

bool pmo_dcn4_fams2_initialize(struct dml2_pmo_initialize_in_out *in_out)
{
	int i = 0;
	struct dml2_pmo_instance *pmo = in_out->instance;

	pmo->soc_bb = in_out->soc_bb;
	pmo->ip_caps = in_out->ip_caps;
	pmo->mpc_combine_limit = 2;
	pmo->odm_combine_limit = 4;
	pmo->mcg_clock_table_size = in_out->mcg_clock_table_size;

	pmo->fams_params.v2.subvp.refresh_rate_limit_max = 175;
	pmo->fams_params.v2.subvp.refresh_rate_limit_min = 0;
	pmo->fams_params.v2.drr.refresh_rate_limit_max = 1000;
	pmo->fams_params.v2.drr.refresh_rate_limit_min = 119;

	pmo->options = in_out->options;

	/* generate permutations of p-state configs from base strategy list */
	for (i = 1; i <= PMO_DCN4_MAX_DISPLAYS; i++) {
		switch (i) {
		case 1:
			DML2_ASSERT(base_strategy_list_1_display_size <= PMO_DCN4_MAX_BASE_STRATEGIES);

			/* populate list */
			expand_base_strategies(pmo, base_strategy_list_1_display, base_strategy_list_1_display_size, 1);
			break;
		case 2:
			DML2_ASSERT(base_strategy_list_2_display_size <= PMO_DCN4_MAX_BASE_STRATEGIES);

			/* populate list */
			expand_base_strategies(pmo, base_strategy_list_2_display, base_strategy_list_2_display_size, 2);
			break;
		case 3:
			DML2_ASSERT(base_strategy_list_3_display_size <= PMO_DCN4_MAX_BASE_STRATEGIES);

			/* populate list */
			expand_base_strategies(pmo, base_strategy_list_3_display, base_strategy_list_3_display_size, 3);
			break;
		case 4:
			DML2_ASSERT(base_strategy_list_4_display_size <= PMO_DCN4_MAX_BASE_STRATEGIES);

			/* populate list */
			expand_base_strategies(pmo, base_strategy_list_4_display, base_strategy_list_4_display_size, 4);
			break;
		}
	}

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

bool pmo_dcn4_fams2_init_for_vmin(struct dml2_pmo_init_for_vmin_in_out *in_out)
{
	unsigned int i;
	const struct dml2_display_cfg *display_config =
			&in_out->base_display_config->display_config;
	const struct dml2_core_mode_support_result *mode_support_result =
			&in_out->base_display_config->mode_support_result;
	struct dml2_optimization_stage4_state *state =
				&in_out->base_display_config->stage4;

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
			state->unoptimizable_streams[display_config->plane_descriptors[i].stream_index] = true;

	for (i = 0; i < display_config->num_streams; i++) {
		if (display_config->stream_descriptors[i].overrides.disable_dynamic_odm)
			state->unoptimizable_streams[i] = true;
		else if (in_out->base_display_config->stage3.stream_svp_meta[i].valid &&
				in_out->instance->options->disable_dyn_odm_for_stream_with_svp)
			state->unoptimizable_streams[i] = true;
		/*
		 * ODM Combine requires horizontal timing divisible by 2 so each
		 * ODM segment has the same size.
		 */
		else if (!is_h_timing_divisible_by(&display_config->stream_descriptors[i].timing, 2))
			state->unoptimizable_streams[i] = true;
		/*
		 * Our hardware support seamless ODM transitions for DP encoders
		 * only.
		 */
		else if (!is_dp_encoder(display_config->stream_descriptors[i].output.output_encoder))
			state->unoptimizable_streams[i] = true;
	}

	state->performed = true;

	return true;
}

bool pmo_dcn4_fams2_test_for_vmin(struct dml2_pmo_test_for_vmin_in_out *in_out)
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

bool pmo_dcn4_fams2_optimize_for_vmin(struct dml2_pmo_optimize_for_vmin_in_out *in_out)
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

static void set_bit_in_bitfield(unsigned int *bit_field, unsigned int bit_offset)
{
	*bit_field = *bit_field | (0x1 << bit_offset);
}

static bool is_bit_set_in_bitfield(unsigned int bit_field, unsigned int bit_offset)
{
	if (bit_field & (0x1 << bit_offset))
		return true;

	return false;
}

static void build_synchronized_timing_groups(
	struct dml2_pmo_instance *pmo,
	struct display_configuation_with_meta *display_config)
{
	unsigned int i, j;
	struct dml2_timing_cfg *master_timing;

	unsigned int stream_mapped_mask = 0;
	unsigned int num_timing_groups = 0;
	unsigned int timing_group_idx = 0;
	struct dml2_pmo_scratch *s = &pmo->scratch;

	/* clear all group masks */
	memset(s->pmo_dcn4.synchronized_timing_group_masks, 0, sizeof(s->pmo_dcn4.synchronized_timing_group_masks));
	memset(s->pmo_dcn4.group_is_drr_enabled, 0, sizeof(s->pmo_dcn4.group_is_drr_enabled));
	memset(s->pmo_dcn4.group_is_drr_active, 0, sizeof(s->pmo_dcn4.group_is_drr_active));
	memset(s->pmo_dcn4.group_line_time_us, 0, sizeof(s->pmo_dcn4.group_line_time_us));
	s->pmo_dcn4.num_timing_groups = 0;

	for (i = 0; i < display_config->display_config.num_streams; i++) {
		master_timing = &display_config->display_config.stream_descriptors[i].timing;

		/* only need to build group of this stream is not in a group already */
		if (is_bit_set_in_bitfield(stream_mapped_mask, i)) {
			continue;
		}
		set_bit_in_bitfield(&stream_mapped_mask, i);
		timing_group_idx = num_timing_groups;
		num_timing_groups++;

		/* trivially set default timing group to itself */
		set_bit_in_bitfield(&s->pmo_dcn4.synchronized_timing_group_masks[timing_group_idx], i);
		s->pmo_dcn4.group_line_time_us[timing_group_idx] = (double)master_timing->h_total / master_timing->pixel_clock_khz * 1000.0;

		/* if drr is in use, timing is not sychnronizable */
		if (master_timing->drr_config.enabled) {
			s->pmo_dcn4.group_is_drr_enabled[timing_group_idx] = true;
			s->pmo_dcn4.group_is_drr_active[timing_group_idx] = !master_timing->drr_config.disallowed &&
					(master_timing->drr_config.drr_active_fixed || master_timing->drr_config.drr_active_variable);
			continue;
		}

		/* find synchronizable timing groups */
		for (j = i + 1; j < display_config->display_config.num_streams; j++) {
			if (memcmp(master_timing,
					&display_config->display_config.stream_descriptors[j].timing,
					sizeof(struct dml2_timing_cfg)) == 0 &&
					display_config->display_config.stream_descriptors[i].output.output_encoder == display_config->display_config.stream_descriptors[j].output.output_encoder &&
					(display_config->display_config.stream_descriptors[i].output.output_encoder != dml2_hdmi || //hdmi requires formats match
					display_config->display_config.stream_descriptors[i].output.output_format == display_config->display_config.stream_descriptors[j].output.output_format)) {
				set_bit_in_bitfield(&pmo->scratch.pmo_dcn4.synchronized_timing_group_masks[timing_group_idx], j);
				set_bit_in_bitfield(&stream_mapped_mask, j);
			}
		}
	}

	s->pmo_dcn4.num_timing_groups = num_timing_groups;
}

static bool all_timings_support_vactive(const struct dml2_pmo_instance *pmo,
		const struct display_configuation_with_meta *display_config,
		unsigned int mask)
{
	unsigned char i;
	bool valid = true;

	// Create a remap array to enable simple iteration through only masked stream indicies
	for (i = 0; i < display_config->display_config.num_streams; i++) {
		if (is_bit_set_in_bitfield(mask, i)) {
			/* check if stream has enough vactive margin */
			valid &= is_bit_set_in_bitfield(pmo->scratch.pmo_dcn4.stream_vactive_capability_mask, i);
		}
	}

	return valid;
}

static bool all_timings_support_vblank(const struct dml2_pmo_instance *pmo,
		const struct display_configuation_with_meta *display_config,
		unsigned int mask)
{
	unsigned int i;

	bool synchronizable = true;

	/* find first vblank stream index and compare the timing group mask */
	for (i = 0; i < display_config->display_config.num_streams; i++) {
		if (is_bit_set_in_bitfield(mask, i)) {
			if (mask != pmo->scratch.pmo_dcn4.synchronized_timing_group_masks[i]) {
				/* vblank streams are not synchronizable */
				synchronizable = false;
			}
			break;
		}
	}

	return synchronizable;
}

static unsigned int calc_svp_microschedule(const struct dml2_fams2_meta *fams2_meta)
{
	return fams2_meta->contention_delay_otg_vlines +
		fams2_meta->method_subvp.programming_delay_otg_vlines +
		fams2_meta->method_subvp.phantom_vtotal +
		fams2_meta->method_subvp.prefetch_to_mall_delay_otg_vlines +
		fams2_meta->dram_clk_change_blackout_otg_vlines;
}

static bool all_timings_support_drr(const struct dml2_pmo_instance *pmo,
	const struct display_configuation_with_meta *display_config,
	unsigned int mask)
{
	unsigned char i;
	for (i = 0; i < DML2_MAX_PLANES; i++) {
		const struct dml2_stream_parameters *stream_descriptor;
		const struct dml2_fams2_meta *stream_fams2_meta;

		if (is_bit_set_in_bitfield(mask, i)) {
			stream_descriptor = &display_config->display_config.stream_descriptors[i];
			stream_fams2_meta = &pmo->scratch.pmo_dcn4.stream_fams2_meta[i];

			if (!stream_descriptor->timing.drr_config.enabled)
				return false;

			/* cannot support required vtotal */
			if (stream_fams2_meta->method_drr.stretched_vtotal > stream_fams2_meta->max_vtotal) {
				return false;
			}

			/* check rr is within bounds */
			if (stream_fams2_meta->nom_refresh_rate_hz < pmo->fams_params.v2.drr.refresh_rate_limit_min ||
				stream_fams2_meta->nom_refresh_rate_hz > pmo->fams_params.v2.drr.refresh_rate_limit_max) {
				return false;
			}

			/* check required stretch is allowed */
			if (stream_descriptor->timing.drr_config.max_instant_vtotal_delta > 0 &&
					stream_fams2_meta->method_drr.stretched_vtotal - stream_fams2_meta->nom_vtotal > stream_descriptor->timing.drr_config.max_instant_vtotal_delta) {
				return false;
			}
		}
	}

	return true;
}

static bool all_timings_support_svp(const struct dml2_pmo_instance *pmo,
	const struct display_configuation_with_meta *display_config,
	unsigned int mask)
{
	const struct dml2_stream_parameters *stream_descriptor;
	const struct dml2_plane_parameters *plane_descriptor;
	const struct dml2_fams2_meta *stream_fams2_meta;
	unsigned int microschedule_vlines;
	unsigned char i;

	unsigned int num_planes_per_stream[DML2_MAX_PLANES] = { 0 };

	/* confirm timing it is not a centered timing */
	for (i = 0; i < display_config->display_config.num_planes; i++) {
		plane_descriptor = &display_config->display_config.plane_descriptors[i];

		if (is_bit_set_in_bitfield(mask, (unsigned char)plane_descriptor->stream_index)) {
			num_planes_per_stream[plane_descriptor->stream_index]++;

			/* check recout height covers entire otg vactive, and single plane */
			if (num_planes_per_stream[plane_descriptor->stream_index] > 1 ||
					!plane_descriptor->composition.rect_out_height_spans_vactive ||
					plane_descriptor->composition.rotation_angle != dml2_rotation_0) {
				return false;
			}
		}
	}

	for (i = 0; i < DML2_MAX_PLANES; i++) {
		if (is_bit_set_in_bitfield(mask, i)) {
			stream_descriptor = &display_config->display_config.stream_descriptors[i];
			stream_fams2_meta = &pmo->scratch.pmo_dcn4.stream_fams2_meta[i];

			if (stream_descriptor->overrides.disable_subvp) {
				return false;
			}

			microschedule_vlines = calc_svp_microschedule(&pmo->scratch.pmo_dcn4.stream_fams2_meta[i]);

			/* block if using an interlaced timing */
			if (stream_descriptor->timing.interlaced) {
				return false;
			}

			/* 1) svp main stream's vactive must be able to fit the microschedule
			*  2) refresh rate must be within the allowed bounds
			*/
			if (microschedule_vlines >= stream_descriptor->timing.v_active ||
					(stream_fams2_meta->nom_refresh_rate_hz < pmo->fams_params.v2.subvp.refresh_rate_limit_min ||
					stream_fams2_meta->nom_refresh_rate_hz > pmo->fams_params.v2.subvp.refresh_rate_limit_max)) {
				return false;
			}
		}
	}

	return true;
}

static void insert_into_candidate_list(const struct dml2_pmo_pstate_strategy *pstate_strategy, int stream_count, struct dml2_pmo_scratch *scratch)
{
	scratch->pmo_dcn4.pstate_strategy_candidates[scratch->pmo_dcn4.num_pstate_candidates] = *pstate_strategy;
	scratch->pmo_dcn4.num_pstate_candidates++;
}

static bool all_planes_match_method(const struct display_configuation_with_meta *display_cfg, int plane_mask, enum dml2_pmo_pstate_method method)
{
	unsigned char i;
	enum dml2_uclk_pstate_change_strategy matching_strategy = (enum dml2_uclk_pstate_change_strategy) dml2_pmo_pstate_strategy_na;

	if (method == dml2_pmo_pstate_strategy_vactive || method == dml2_pmo_pstate_strategy_fw_vactive_drr)
		matching_strategy = dml2_uclk_pstate_change_strategy_force_vactive;
	else if (method == dml2_pmo_pstate_strategy_vblank || method == dml2_pmo_pstate_strategy_fw_vblank_drr)
		matching_strategy = dml2_uclk_pstate_change_strategy_force_vblank;
	else if (method == dml2_pmo_pstate_strategy_fw_svp)
		matching_strategy = dml2_uclk_pstate_change_strategy_force_mall_svp;
	else if (method == dml2_pmo_pstate_strategy_fw_drr)
		matching_strategy = dml2_uclk_pstate_change_strategy_force_drr;

	for (i = 0; i < DML2_MAX_PLANES; i++) {
		if (is_bit_set_in_bitfield(plane_mask, i)) {
			if (display_cfg->display_config.plane_descriptors[i].overrides.uclk_pstate_change_strategy != dml2_uclk_pstate_change_strategy_auto &&
				display_cfg->display_config.plane_descriptors[i].overrides.uclk_pstate_change_strategy != matching_strategy)
				return false;
		}
	}

	return true;
}

static void build_method_scheduling_params(
	struct dml2_fams2_per_method_common_meta *stream_method_fams2_meta,
	struct dml2_fams2_meta *stream_fams2_meta)
{
	stream_method_fams2_meta->allow_time_us =
			(double)((int)stream_method_fams2_meta->allow_end_otg_vline - (int)stream_method_fams2_meta->allow_start_otg_vline) *
			stream_fams2_meta->otg_vline_time_us;
	if (stream_method_fams2_meta->allow_time_us >= stream_method_fams2_meta->period_us) {
		/* when allow wave overlaps an entire frame, it is always schedulable (DRR can do this)*/
		stream_method_fams2_meta->disallow_time_us = 0.0;
	} else {
		stream_method_fams2_meta->disallow_time_us =
				stream_method_fams2_meta->period_us - stream_method_fams2_meta->allow_time_us;
	}
}

static struct dml2_fams2_per_method_common_meta *get_per_method_common_meta(
	struct dml2_pmo_instance *pmo,
	enum dml2_pmo_pstate_method stream_pstate_method,
	int stream_idx)
{
	struct dml2_fams2_per_method_common_meta *stream_method_fams2_meta = NULL;

	switch (stream_pstate_method) {
	case dml2_pmo_pstate_strategy_vactive:
	case dml2_pmo_pstate_strategy_fw_vactive_drr:
		stream_method_fams2_meta = &pmo->scratch.pmo_dcn4.stream_fams2_meta[stream_idx].method_vactive.common;
		break;
	case dml2_pmo_pstate_strategy_vblank:
	case dml2_pmo_pstate_strategy_fw_vblank_drr:
		stream_method_fams2_meta = &pmo->scratch.pmo_dcn4.stream_fams2_meta[stream_idx].method_vblank.common;
		break;
	case dml2_pmo_pstate_strategy_fw_svp:
	case dml2_pmo_pstate_strategy_fw_svp_drr:
		stream_method_fams2_meta = &pmo->scratch.pmo_dcn4.stream_fams2_meta[stream_idx].method_subvp.common;
		break;
	case dml2_pmo_pstate_strategy_fw_drr:
		stream_method_fams2_meta = &pmo->scratch.pmo_dcn4.stream_fams2_meta[stream_idx].method_drr.common;
		break;
	case dml2_pmo_pstate_strategy_reserved_hw:
	case dml2_pmo_pstate_strategy_reserved_fw:
	case dml2_pmo_pstate_strategy_reserved_fw_drr_clamped:
	case dml2_pmo_pstate_strategy_reserved_fw_drr_var:
	case dml2_pmo_pstate_strategy_na:
	default:
		stream_method_fams2_meta = NULL;
	}

	return stream_method_fams2_meta;
}

static bool is_timing_group_schedulable(
		struct dml2_pmo_instance *pmo,
		const struct display_configuation_with_meta *display_cfg,
		const struct dml2_pmo_pstate_strategy *pstate_strategy,
		const unsigned int timing_group_idx,
		struct dml2_fams2_per_method_common_meta *group_fams2_meta)
{
	unsigned int i;
	struct dml2_fams2_per_method_common_meta *stream_method_fams2_meta;

	unsigned int base_stream_idx = 0;
	struct dml2_pmo_scratch *s = &pmo->scratch;

	/* find base stream idx */
	for (base_stream_idx = 0; base_stream_idx < display_cfg->display_config.num_streams; base_stream_idx++) {
		if (is_bit_set_in_bitfield(s->pmo_dcn4.synchronized_timing_group_masks[timing_group_idx], base_stream_idx)) {
			/* master stream found */
			break;
		}
	}

	/* init allow start and end lines for timing group */
	stream_method_fams2_meta = get_per_method_common_meta(pmo, pstate_strategy->per_stream_pstate_method[base_stream_idx], base_stream_idx);
	if (!stream_method_fams2_meta)
		return false;

	group_fams2_meta->allow_start_otg_vline = stream_method_fams2_meta->allow_start_otg_vline;
	group_fams2_meta->allow_end_otg_vline = stream_method_fams2_meta->allow_end_otg_vline;
	group_fams2_meta->period_us = stream_method_fams2_meta->period_us;
	for (i = base_stream_idx + 1; i < display_cfg->display_config.num_streams; i++) {
		if (is_bit_set_in_bitfield(pmo->scratch.pmo_dcn4.synchronized_timing_group_masks[timing_group_idx], i)) {
			stream_method_fams2_meta = get_per_method_common_meta(pmo, pstate_strategy->per_stream_pstate_method[i], i);
			if (!stream_method_fams2_meta)
				return false;

			if (group_fams2_meta->allow_start_otg_vline < stream_method_fams2_meta->allow_start_otg_vline) {
				/* set group allow start to larger otg vline */
				group_fams2_meta->allow_start_otg_vline = stream_method_fams2_meta->allow_start_otg_vline;
			}

			if (group_fams2_meta->allow_end_otg_vline > stream_method_fams2_meta->allow_end_otg_vline) {
				/* set group allow end to smaller otg vline */
				group_fams2_meta->allow_end_otg_vline = stream_method_fams2_meta->allow_end_otg_vline;
			}

			/* check waveform still has positive width */
			if (group_fams2_meta->allow_start_otg_vline >= group_fams2_meta->allow_end_otg_vline) {
				/* timing group is not schedulable */
				return false;
			}
		}
	}

	/* calculate the rest of the meta */
	build_method_scheduling_params(group_fams2_meta, &pmo->scratch.pmo_dcn4.stream_fams2_meta[base_stream_idx]);

	return group_fams2_meta->allow_time_us > 0.0 &&
			group_fams2_meta->disallow_time_us < pmo->ip_caps->fams2.max_allow_delay_us;
}

static bool is_config_schedulable(
	struct dml2_pmo_instance *pmo,
	const struct display_configuation_with_meta *display_cfg,
	const struct dml2_pmo_pstate_strategy *pstate_strategy)
{
	unsigned int i, j;
	bool schedulable;
	struct dml2_pmo_scratch *s = &pmo->scratch;

	double max_allow_delay_us = 0.0;

	memset(s->pmo_dcn4.group_common_fams2_meta, 0, sizeof(s->pmo_dcn4.group_common_fams2_meta));
	memset(s->pmo_dcn4.sorted_group_gtl_disallow_index, 0, sizeof(unsigned int) * DML2_MAX_PLANES);

	/* search for a general solution to the schedule */

	/* STAGE 0: Early return for special cases */
	if (display_cfg->display_config.num_streams == 0) {
		return true;
	}

	/* STAGE 1: confirm allow waves overlap for synchronizable streams */
	schedulable = true;
	for (i = 0; i < s->pmo_dcn4.num_timing_groups; i++) {
		s->pmo_dcn4.sorted_group_gtl_disallow_index[i] = i;
		s->pmo_dcn4.sorted_group_gtl_period_index[i] = i;
		if (!is_timing_group_schedulable(pmo, display_cfg, pstate_strategy, i, &s->pmo_dcn4.group_common_fams2_meta[i])) {
			/* synchronized timing group was not schedulable */
			schedulable = false;
			break;
		}
		max_allow_delay_us += s->pmo_dcn4.group_common_fams2_meta[i].disallow_time_us;
	}

	if ((schedulable && s->pmo_dcn4.num_timing_groups <= 1) || !schedulable) {
		/* 1. the only timing group was schedulable, so early pass
		 * 2. one of the timing groups was not schedulable, so early fail */
		return schedulable;
	}

	/* STAGE 2: Check allow can't be masked entirely by other disallows */
	schedulable = true;

	/* sort disallow times from greatest to least */
	for (i = 0; i < s->pmo_dcn4.num_timing_groups; i++) {
		bool swapped = false;

		for (j = 0; j < s->pmo_dcn4.num_timing_groups - 1; j++) {
			double j_disallow_us = s->pmo_dcn4.group_common_fams2_meta[s->pmo_dcn4.sorted_group_gtl_disallow_index[j]].disallow_time_us;
			double jp1_disallow_us = s->pmo_dcn4.group_common_fams2_meta[s->pmo_dcn4.sorted_group_gtl_disallow_index[j + 1]].disallow_time_us;
			if (j_disallow_us < jp1_disallow_us) {
				/* swap as A < B */
				swap(s->pmo_dcn4.sorted_group_gtl_disallow_index[j],
				     s->pmo_dcn4.sorted_group_gtl_disallow_index[j+1]);
				swapped = true;
			}
		}

		/* sorted, exit early */
		if (!swapped)
			break;
	}

	/* Check worst case disallow region occurs in the middle of allow for the
	* other display, or when >2 streams continue to halve the remaining allow time.
	*/
	for (i = 0; i < s->pmo_dcn4.num_timing_groups; i++) {
		if (s->pmo_dcn4.group_common_fams2_meta[i].disallow_time_us <= 0.0) {
			/* this timing group always allows */
			continue;
		}

		double max_allow_time_us = s->pmo_dcn4.group_common_fams2_meta[i].allow_time_us;
		for (j = 0; j < s->pmo_dcn4.num_timing_groups; j++) {
			unsigned int sorted_j = s->pmo_dcn4.sorted_group_gtl_disallow_index[j];
			/* stream can't overlap itself */
			if (i != sorted_j && s->pmo_dcn4.group_common_fams2_meta[sorted_j].disallow_time_us > 0.0) {
				max_allow_time_us = math_min2(
						s->pmo_dcn4.group_common_fams2_meta[sorted_j].allow_time_us,
						(max_allow_time_us - s->pmo_dcn4.group_common_fams2_meta[sorted_j].disallow_time_us) / 2);

				if (max_allow_time_us < 0.0) {
					/* failed exit early */
					break;
				}
			}
		}

		if (max_allow_time_us <= 0.0) {
			/* not enough time for microschedule in the worst case */
			schedulable = false;
			break;
		}
	}

	if (schedulable && max_allow_delay_us < pmo->ip_caps->fams2.max_allow_delay_us) {
		return true;
	}

	/* STAGE 3: check larger allow can fit period of all other streams */
	schedulable = true;

	/* sort periods from greatest to least */
	for (i = 0; i < s->pmo_dcn4.num_timing_groups; i++) {
		bool swapped = false;

		for (j = 0; j < s->pmo_dcn4.num_timing_groups - 1; j++) {
			double j_period_us = s->pmo_dcn4.group_common_fams2_meta[s->pmo_dcn4.sorted_group_gtl_period_index[j]].period_us;
			double jp1_period_us = s->pmo_dcn4.group_common_fams2_meta[s->pmo_dcn4.sorted_group_gtl_period_index[j + 1]].period_us;
			if (j_period_us < jp1_period_us) {
				/* swap as A < B */
				swap(s->pmo_dcn4.sorted_group_gtl_period_index[j],
				     s->pmo_dcn4.sorted_group_gtl_period_index[j+1]);
				swapped = true;
			}
		}

		/* sorted, exit early */
		if (!swapped)
			break;
	}

	/* check larger allow can fit period of all other streams */
	for (i = 0; i < s->pmo_dcn4.num_timing_groups - 1; i++) {
		unsigned int sorted_i = s->pmo_dcn4.sorted_group_gtl_period_index[i];
		unsigned int sorted_ip1 = s->pmo_dcn4.sorted_group_gtl_period_index[i + 1];

		if (s->pmo_dcn4.group_common_fams2_meta[sorted_i].allow_time_us < s->pmo_dcn4.group_common_fams2_meta[sorted_ip1].period_us ||
				(s->pmo_dcn4.group_is_drr_enabled[sorted_ip1] && s->pmo_dcn4.group_is_drr_active[sorted_ip1])) {
			schedulable = false;
			break;
		}
	}

	if (schedulable && max_allow_delay_us < pmo->ip_caps->fams2.max_allow_delay_us) {
		return true;
	}

	/* STAGE 4: When using HW exclusive modes, check disallow alignments are within allowed threshold */
	if (s->pmo_dcn4.num_timing_groups == 2 &&
			!is_bit_set_in_bitfield(PMO_FW_STRATEGY_MASK, pstate_strategy->per_stream_pstate_method[0]) &&
			!is_bit_set_in_bitfield(PMO_FW_STRATEGY_MASK, pstate_strategy->per_stream_pstate_method[1])) {
		double period_ratio;
		double max_shift_us;
		double shift_per_period;

		/* default period_0 > period_1 */
		unsigned int lrg_idx = 0;
		unsigned int sml_idx = 1;
		if (s->pmo_dcn4.group_common_fams2_meta[0].period_us < s->pmo_dcn4.group_common_fams2_meta[1].period_us) {
			/* period_0 < period_1 */
			lrg_idx = 1;
			sml_idx = 0;
		}
		period_ratio = s->pmo_dcn4.group_common_fams2_meta[lrg_idx].period_us / s->pmo_dcn4.group_common_fams2_meta[sml_idx].period_us;
		shift_per_period = s->pmo_dcn4.group_common_fams2_meta[sml_idx].period_us * (period_ratio - math_floor(period_ratio));
		max_shift_us = s->pmo_dcn4.group_common_fams2_meta[lrg_idx].disallow_time_us - s->pmo_dcn4.group_common_fams2_meta[sml_idx].allow_time_us;
		max_allow_delay_us = max_shift_us / shift_per_period * s->pmo_dcn4.group_common_fams2_meta[lrg_idx].period_us;

		if (shift_per_period > 0.0 &&
			shift_per_period < s->pmo_dcn4.group_common_fams2_meta[lrg_idx].allow_time_us + s->pmo_dcn4.group_common_fams2_meta[sml_idx].allow_time_us &&
			max_allow_delay_us < pmo->ip_caps->fams2.max_allow_delay_us) {
			schedulable = true;
		}
	}

	return schedulable;
}

static bool stream_matches_drr_policy(struct dml2_pmo_instance *pmo,
	const struct display_configuation_with_meta *display_cfg,
	const enum dml2_pmo_pstate_method stream_pstate_method,
	unsigned int stream_index)
{
	const struct dml2_stream_parameters *stream_descriptor = &display_cfg->display_config.stream_descriptors[stream_index];
	bool strategy_matches_drr_requirements = true;

	/* check if strategy is compatible with stream drr capability and strategy */
	if (is_bit_set_in_bitfield(PMO_NO_DRR_STRATEGY_MASK, stream_pstate_method) &&
			display_cfg->display_config.num_streams > 1 &&
			stream_descriptor->timing.drr_config.enabled &&
			(stream_descriptor->timing.drr_config.drr_active_fixed || stream_descriptor->timing.drr_config.drr_active_variable)) {
		/* DRR is active, so config may become unschedulable */
		strategy_matches_drr_requirements = false;
	} else if (is_bit_set_in_bitfield(PMO_NO_DRR_STRATEGY_MASK, stream_pstate_method) &&
			is_bit_set_in_bitfield(PMO_FW_STRATEGY_MASK, stream_pstate_method) &&
			stream_descriptor->timing.drr_config.enabled &&
			stream_descriptor->timing.drr_config.drr_active_variable) {
		/* DRR is variable, fw exclusive methods require DRR to be clamped */
		strategy_matches_drr_requirements = false;
	} else if (is_bit_set_in_bitfield(PMO_DRR_VAR_STRATEGY_MASK, stream_pstate_method) &&
			pmo->options->disable_drr_var_when_var_active &&
			stream_descriptor->timing.drr_config.enabled &&
			stream_descriptor->timing.drr_config.drr_active_variable) {
		/* DRR variable is active, but policy blocks DRR for p-state when this happens */
		strategy_matches_drr_requirements = false;
	} else if (is_bit_set_in_bitfield(PMO_DRR_VAR_STRATEGY_MASK, stream_pstate_method) &&
			(pmo->options->disable_drr_var ||
			!stream_descriptor->timing.drr_config.enabled ||
			stream_descriptor->timing.drr_config.disallowed)) {
		/* DRR variable strategies are disallowed due to settings or policy */
		strategy_matches_drr_requirements = false;
	} else if (is_bit_set_in_bitfield(PMO_DRR_CLAMPED_STRATEGY_MASK, stream_pstate_method) &&
		(pmo->options->disable_drr_clamped ||
			(!stream_descriptor->timing.drr_config.enabled ||
			(!stream_descriptor->timing.drr_config.drr_active_fixed && !stream_descriptor->timing.drr_config.drr_active_variable)) ||
			(pmo->options->disable_drr_clamped_when_var_active &&
			stream_descriptor->timing.drr_config.enabled &&
			stream_descriptor->timing.drr_config.drr_active_variable))) {
		/* DRR fixed strategies are disallowed due to settings or policy */
		strategy_matches_drr_requirements = false;
	} else if (is_bit_set_in_bitfield(PMO_FW_STRATEGY_MASK, stream_pstate_method) &&
			pmo->options->disable_fams2) {
		/* FW modes require FAMS2 */
		strategy_matches_drr_requirements = false;
	}

	return strategy_matches_drr_requirements;
}

static bool validate_pstate_support_strategy_cofunctionality(struct dml2_pmo_instance *pmo,
		const struct display_configuation_with_meta *display_cfg,
		const struct dml2_pmo_pstate_strategy *pstate_strategy)
{
	struct dml2_pmo_scratch *s = &pmo->scratch;

	unsigned char stream_index = 0;

	unsigned int svp_count = 0;
	unsigned int svp_stream_mask = 0;
	unsigned int drr_count = 0;
	unsigned int drr_stream_mask = 0;
	unsigned int vactive_count = 0;
	unsigned int vactive_stream_mask = 0;
	unsigned int vblank_count = 0;
	unsigned int vblank_stream_mask = 0;

	bool strategy_matches_forced_requirements = true;
	bool strategy_matches_drr_requirements = true;

	// Tabulate everything
	for (stream_index = 0; stream_index < display_cfg->display_config.num_streams; stream_index++) {

		if (!all_planes_match_method(display_cfg, s->pmo_dcn4.stream_plane_mask[stream_index],
			pstate_strategy->per_stream_pstate_method[stream_index])) {
			strategy_matches_forced_requirements = false;
			break;
		}

		strategy_matches_drr_requirements &=
			stream_matches_drr_policy(pmo, display_cfg, pstate_strategy->per_stream_pstate_method[stream_index], stream_index);

		if (pstate_strategy->per_stream_pstate_method[stream_index] == dml2_pmo_pstate_strategy_fw_svp ||
			pstate_strategy->per_stream_pstate_method[stream_index] == dml2_pmo_pstate_strategy_fw_svp_drr) {
			svp_count++;
			set_bit_in_bitfield(&svp_stream_mask, stream_index);
		} else if (pstate_strategy->per_stream_pstate_method[stream_index] == dml2_pmo_pstate_strategy_fw_drr) {
			drr_count++;
			set_bit_in_bitfield(&drr_stream_mask, stream_index);
		} else if (pstate_strategy->per_stream_pstate_method[stream_index] == dml2_pmo_pstate_strategy_vactive ||
			pstate_strategy->per_stream_pstate_method[stream_index] == dml2_pmo_pstate_strategy_fw_vactive_drr) {
			vactive_count++;
			set_bit_in_bitfield(&vactive_stream_mask, stream_index);
		} else if (pstate_strategy->per_stream_pstate_method[stream_index] == dml2_pmo_pstate_strategy_vblank ||
			pstate_strategy->per_stream_pstate_method[stream_index] == dml2_pmo_pstate_strategy_fw_vblank_drr) {
			vblank_count++;
			set_bit_in_bitfield(&vblank_stream_mask, stream_index);
		}
	}

	if (!strategy_matches_forced_requirements || !strategy_matches_drr_requirements)
		return false;

	if (vactive_count > 0 && !all_timings_support_vactive(pmo, display_cfg, vactive_stream_mask))
		return false;

	if (vblank_count > 0 && (pmo->options->disable_vblank || !all_timings_support_vblank(pmo, display_cfg, vblank_stream_mask)))
		return false;

	if (drr_count > 0 && (pmo->options->disable_drr_var || !all_timings_support_drr(pmo, display_cfg, drr_stream_mask)))
		return false;

	if (svp_count > 0 && (pmo->options->disable_svp || !all_timings_support_svp(pmo, display_cfg, svp_stream_mask)))
		return false;

	return is_config_schedulable(pmo, display_cfg, pstate_strategy);
}

static int get_vactive_pstate_margin(const struct display_configuation_with_meta *display_cfg, int plane_mask)
{
	unsigned char i;
	int min_vactive_margin_us = 0xFFFFFFF;

	for (i = 0; i < DML2_MAX_PLANES; i++) {
		if (is_bit_set_in_bitfield(plane_mask, i)) {
			if (display_cfg->mode_support_result.cfg_support_info.plane_support_info[i].dram_change_latency_hiding_margin_in_active < min_vactive_margin_us)
				min_vactive_margin_us = display_cfg->mode_support_result.cfg_support_info.plane_support_info[i].dram_change_latency_hiding_margin_in_active;
		}
	}

	return min_vactive_margin_us;
}

static unsigned int get_vactive_det_fill_latency_delay_us(const struct display_configuation_with_meta *display_cfg, int plane_mask)
{
	unsigned char i;
	unsigned int max_vactive_fill_us = 0;

	for (i = 0; i < DML2_MAX_PLANES; i++) {
		if (is_bit_set_in_bitfield(plane_mask, i)) {
			if (display_cfg->mode_support_result.cfg_support_info.plane_support_info[i].dram_change_vactive_det_fill_delay_us > max_vactive_fill_us)
				max_vactive_fill_us = display_cfg->mode_support_result.cfg_support_info.plane_support_info[i].dram_change_vactive_det_fill_delay_us;
		}
	}

	return max_vactive_fill_us;
}

static void build_fams2_meta_per_stream(struct dml2_pmo_instance *pmo,
	struct display_configuation_with_meta *display_config,
	int stream_index)
{
	const struct dml2_ip_capabilities *ip_caps = pmo->ip_caps;
	const struct dml2_stream_parameters *stream_descriptor = &display_config->display_config.stream_descriptors[stream_index];
	const struct core_stream_support_info *stream_info = &display_config->mode_support_result.cfg_support_info.stream_support_info[stream_index];
	const struct dml2_timing_cfg *timing = &stream_descriptor->timing;
	struct dml2_fams2_meta *stream_fams2_meta = &pmo->scratch.pmo_dcn4.stream_fams2_meta[stream_index];

	/* worst case all other streams require some programming at the same time, 0 if only 1 stream */
	unsigned int contention_delay_us = (ip_caps->fams2.vertical_interrupt_ack_delay_us +
			(unsigned int)math_max3(ip_caps->fams2.subvp_programming_delay_us, ip_caps->fams2.drr_programming_delay_us, ip_caps->fams2.allow_programming_delay_us)) *
			(display_config->display_config.num_streams - 1);

	/* common */
	stream_fams2_meta->valid = true;
	stream_fams2_meta->otg_vline_time_us = (double)timing->h_total / timing->pixel_clock_khz * 1000.0;
	stream_fams2_meta->nom_vtotal = stream_descriptor->timing.vblank_nom + stream_descriptor->timing.v_active;
	stream_fams2_meta->nom_refresh_rate_hz = timing->pixel_clock_khz * 1000.0 /
			(stream_fams2_meta->nom_vtotal * timing->h_total);
	stream_fams2_meta->nom_frame_time_us =
			(double)stream_fams2_meta->nom_vtotal * stream_fams2_meta->otg_vline_time_us;
	stream_fams2_meta->vblank_start = timing->v_blank_end + timing->v_active;

	if (stream_descriptor->timing.drr_config.enabled == true) {
		if (stream_descriptor->timing.drr_config.min_refresh_uhz != 0.0) {
			stream_fams2_meta->max_vtotal = (unsigned int)math_floor((double)stream_descriptor->timing.pixel_clock_khz /
					((double)stream_descriptor->timing.drr_config.min_refresh_uhz * stream_descriptor->timing.h_total) * 1e9);
		} else {
			/* assume min of 48Hz */
			stream_fams2_meta->max_vtotal = (unsigned int)math_floor((double)stream_descriptor->timing.pixel_clock_khz /
					(48000000.0 * stream_descriptor->timing.h_total) * 1e9);
		}
	} else {
		stream_fams2_meta->max_vtotal = stream_fams2_meta->nom_vtotal;
	}
	stream_fams2_meta->min_refresh_rate_hz = timing->pixel_clock_khz * 1000.0 /
			(stream_fams2_meta->max_vtotal * timing->h_total);
	stream_fams2_meta->max_frame_time_us =
			(double)stream_fams2_meta->max_vtotal * stream_fams2_meta->otg_vline_time_us;

	stream_fams2_meta->scheduling_delay_otg_vlines =
			(unsigned int)math_ceil(ip_caps->fams2.scheduling_delay_us / stream_fams2_meta->otg_vline_time_us);
	stream_fams2_meta->vertical_interrupt_ack_delay_otg_vlines =
			(unsigned int)math_ceil(ip_caps->fams2.vertical_interrupt_ack_delay_us / stream_fams2_meta->otg_vline_time_us);
	stream_fams2_meta->contention_delay_otg_vlines =
			(unsigned int)math_ceil(contention_delay_us / stream_fams2_meta->otg_vline_time_us);
	/* worst case allow to target needs to account for all streams' allow events overlapping, and 1 line for error */
	stream_fams2_meta->allow_to_target_delay_otg_vlines =
			(unsigned int)(math_ceil((ip_caps->fams2.vertical_interrupt_ack_delay_us + contention_delay_us + ip_caps->fams2.allow_programming_delay_us) / stream_fams2_meta->otg_vline_time_us)) + 1;
	stream_fams2_meta->min_allow_width_otg_vlines =
			(unsigned int)math_ceil(ip_caps->fams2.min_allow_width_us / stream_fams2_meta->otg_vline_time_us);
	/* this value should account for urgent latency */
	stream_fams2_meta->dram_clk_change_blackout_otg_vlines =
			(unsigned int)math_ceil(pmo->soc_bb->power_management_parameters.dram_clk_change_blackout_us /
			stream_fams2_meta->otg_vline_time_us);

	/* scheduling params should be built based on the worst case for allow_time:disallow_time */

	/* vactive */
	if (display_config->display_config.num_streams == 1) {
		/* for single stream, guarantee at least an instant of allow */
		stream_fams2_meta->method_vactive.max_vactive_det_fill_delay_otg_vlines = (unsigned int)math_floor(
				math_max2(0.0,
				timing->v_active - stream_fams2_meta->min_allow_width_otg_vlines - stream_fams2_meta->dram_clk_change_blackout_otg_vlines));
	} else {
		/* for multi stream, bound to a max fill time defined by IP caps */
		stream_fams2_meta->method_vactive.max_vactive_det_fill_delay_otg_vlines =
				(unsigned int)math_floor((double)ip_caps->max_vactive_det_fill_delay_us / stream_fams2_meta->otg_vline_time_us);
	}
	stream_fams2_meta->method_vactive.max_vactive_det_fill_delay_us = stream_fams2_meta->method_vactive.max_vactive_det_fill_delay_otg_vlines * stream_fams2_meta->otg_vline_time_us;

	if (stream_fams2_meta->method_vactive.max_vactive_det_fill_delay_us > 0.0) {
		stream_fams2_meta->method_vactive.common.allow_start_otg_vline =
			timing->v_blank_end + stream_fams2_meta->method_vactive.max_vactive_det_fill_delay_otg_vlines;
		stream_fams2_meta->method_vactive.common.allow_end_otg_vline =
			stream_fams2_meta->vblank_start -
			stream_fams2_meta->dram_clk_change_blackout_otg_vlines;
	} else {
		stream_fams2_meta->method_vactive.common.allow_start_otg_vline = 0;
		stream_fams2_meta->method_vactive.common.allow_end_otg_vline = 0;
	}
	stream_fams2_meta->method_vactive.common.period_us = stream_fams2_meta->nom_frame_time_us;
	build_method_scheduling_params(&stream_fams2_meta->method_vactive.common, stream_fams2_meta);

	/* vblank */
	stream_fams2_meta->method_vblank.common.allow_start_otg_vline = stream_fams2_meta->vblank_start;
	stream_fams2_meta->method_vblank.common.allow_end_otg_vline =
			stream_fams2_meta->method_vblank.common.allow_start_otg_vline + 1;
	stream_fams2_meta->method_vblank.common.period_us = stream_fams2_meta->nom_frame_time_us;
	build_method_scheduling_params(&stream_fams2_meta->method_vblank.common, stream_fams2_meta);

	/* subvp */
	stream_fams2_meta->method_subvp.programming_delay_otg_vlines =
			(unsigned int)math_ceil(ip_caps->fams2.subvp_programming_delay_us / stream_fams2_meta->otg_vline_time_us);
	stream_fams2_meta->method_subvp.df_throttle_delay_otg_vlines =
			(unsigned int)math_ceil(ip_caps->fams2.subvp_df_throttle_delay_us / stream_fams2_meta->otg_vline_time_us);
	stream_fams2_meta->method_subvp.prefetch_to_mall_delay_otg_vlines =
			(unsigned int)math_ceil(ip_caps->fams2.subvp_prefetch_to_mall_delay_us / stream_fams2_meta->otg_vline_time_us);
	stream_fams2_meta->method_subvp.phantom_vactive =
			stream_fams2_meta->allow_to_target_delay_otg_vlines +
			stream_fams2_meta->min_allow_width_otg_vlines +
			stream_info->phantom_min_v_active;
	stream_fams2_meta->method_subvp.phantom_vfp =
			stream_fams2_meta->method_subvp.df_throttle_delay_otg_vlines;
	/* phantom vtotal = v_bp(vstartup) + v_sync(1) + v_fp(throttle_delay) + v_active(allow_to_target + min_allow + min_vactive)*/
	stream_fams2_meta->method_subvp.phantom_vtotal =
			stream_info->phantom_v_startup +
			stream_fams2_meta->method_subvp.phantom_vfp +
			1 +
			stream_fams2_meta->method_subvp.df_throttle_delay_otg_vlines +
			stream_fams2_meta->method_subvp.phantom_vactive;
	stream_fams2_meta->method_subvp.common.allow_start_otg_vline =
			stream_descriptor->timing.v_blank_end +
			stream_fams2_meta->contention_delay_otg_vlines +
			stream_fams2_meta->method_subvp.programming_delay_otg_vlines +
			stream_fams2_meta->method_subvp.phantom_vtotal +
			stream_fams2_meta->method_subvp.prefetch_to_mall_delay_otg_vlines +
			stream_fams2_meta->allow_to_target_delay_otg_vlines;
	stream_fams2_meta->method_subvp.common.allow_end_otg_vline =
			stream_fams2_meta->vblank_start -
			stream_fams2_meta->dram_clk_change_blackout_otg_vlines;
	stream_fams2_meta->method_subvp.common.period_us = stream_fams2_meta->nom_frame_time_us;
	build_method_scheduling_params(&stream_fams2_meta->method_subvp.common, stream_fams2_meta);

	/* drr */
	stream_fams2_meta->method_drr.programming_delay_otg_vlines =
			(unsigned int)math_ceil(ip_caps->fams2.drr_programming_delay_us / stream_fams2_meta->otg_vline_time_us);
	stream_fams2_meta->method_drr.common.allow_start_otg_vline =
			stream_fams2_meta->vblank_start +
			stream_fams2_meta->allow_to_target_delay_otg_vlines;
	stream_fams2_meta->method_drr.common.period_us = stream_fams2_meta->nom_frame_time_us;
	if (display_config->display_config.num_streams <= 1) {
		/* only need to stretch vblank for blackout time */
		stream_fams2_meta->method_drr.stretched_vtotal =
				stream_fams2_meta->nom_vtotal +
				stream_fams2_meta->allow_to_target_delay_otg_vlines +
				stream_fams2_meta->min_allow_width_otg_vlines +
				stream_fams2_meta->dram_clk_change_blackout_otg_vlines;
	} else {
		/* multi display needs to always be schedulable */
		stream_fams2_meta->method_drr.stretched_vtotal =
				stream_fams2_meta->nom_vtotal * 2 +
				stream_fams2_meta->allow_to_target_delay_otg_vlines +
				stream_fams2_meta->min_allow_width_otg_vlines +
				stream_fams2_meta->dram_clk_change_blackout_otg_vlines;
	}
	stream_fams2_meta->method_drr.common.allow_end_otg_vline =
			stream_fams2_meta->method_drr.stretched_vtotal -
			stream_fams2_meta->dram_clk_change_blackout_otg_vlines;
	build_method_scheduling_params(&stream_fams2_meta->method_drr.common, stream_fams2_meta);
}

static void build_subvp_meta_per_stream(struct dml2_pmo_instance *pmo,
	struct display_configuation_with_meta *display_config,
	int stream_index)
{
	struct dml2_implicit_svp_meta *stream_svp_meta = &pmo->scratch.pmo_dcn4.stream_svp_meta[stream_index];
	struct dml2_fams2_meta *stream_fams2_meta = &pmo->scratch.pmo_dcn4.stream_fams2_meta[stream_index];

	stream_svp_meta->valid = true;

	/* PMO FAMS2 precaulcates these values */
	stream_svp_meta->v_active = stream_fams2_meta->method_subvp.phantom_vactive;
	stream_svp_meta->v_front_porch = stream_fams2_meta->method_subvp.phantom_vfp;
	stream_svp_meta->v_total = stream_fams2_meta->method_subvp.phantom_vtotal;
}

bool pmo_dcn4_fams2_init_for_pstate_support(struct dml2_pmo_init_for_pstate_support_in_out *in_out)
{
	struct dml2_pmo_instance *pmo = in_out->instance;
	struct dml2_optimization_stage3_state *state = &in_out->base_display_config->stage3;
	struct dml2_pmo_scratch *s = &pmo->scratch;

	struct display_configuation_with_meta *display_config;
	const struct dml2_plane_parameters *plane_descriptor;
	const struct dml2_pmo_pstate_strategy *strategy_list = NULL;
	unsigned int strategy_list_size = 0;
	unsigned char plane_index, stream_index, i;

	state->performed = true;
	in_out->base_display_config->stage3.min_clk_index_for_latency = in_out->base_display_config->stage1.min_clk_index_for_latency;

	display_config = in_out->base_display_config;
	display_config->display_config.overrides.enable_subvp_implicit_pmo = true;

	memset(s, 0, sizeof(struct dml2_pmo_scratch));

	if (display_config->display_config.overrides.all_streams_blanked) {
		return true;
	}

	pmo->scratch.pmo_dcn4.min_latency_index = in_out->base_display_config->stage1.min_clk_index_for_latency;
	pmo->scratch.pmo_dcn4.max_latency_index = pmo->mcg_clock_table_size;
	pmo->scratch.pmo_dcn4.cur_latency_index = in_out->base_display_config->stage1.min_clk_index_for_latency;

	// First build the stream plane mask (array of bitfields indexed by stream, indicating plane mapping)
	for (plane_index = 0; plane_index < display_config->display_config.num_planes; plane_index++) {
		plane_descriptor = &display_config->display_config.plane_descriptors[plane_index];

		set_bit_in_bitfield(&s->pmo_dcn4.stream_plane_mask[plane_descriptor->stream_index], plane_index);

		state->pstate_switch_modes[plane_index] = dml2_uclk_pstate_support_method_vactive;
	}

	// Figure out which streams can do vactive, and also build up implicit SVP and FAMS2 meta
	for (stream_index = 0; stream_index < display_config->display_config.num_streams; stream_index++) {
		if (get_vactive_pstate_margin(display_config, s->pmo_dcn4.stream_plane_mask[stream_index]) >= (int)(MIN_VACTIVE_MARGIN_PCT * pmo->soc_bb->power_management_parameters.dram_clk_change_blackout_us))
			set_bit_in_bitfield(&s->pmo_dcn4.stream_vactive_capability_mask, stream_index);

		/* FAMS2 meta */
		build_fams2_meta_per_stream(pmo, display_config, stream_index);

		/* SVP meta */
		build_subvp_meta_per_stream(pmo, display_config, stream_index);
	}

	/* get synchronized timing groups */
	build_synchronized_timing_groups(pmo, display_config);

	strategy_list = get_expanded_strategy_list(&pmo->init_data, display_config->display_config.num_streams);
	if (!strategy_list)
		return false;

	strategy_list_size = get_num_expanded_strategies(&pmo->init_data, display_config->display_config.num_streams);

	if (strategy_list_size == 0)
		return false;

	s->pmo_dcn4.num_pstate_candidates = 0;

	for (i = 0; i < strategy_list_size && s->pmo_dcn4.num_pstate_candidates < DML2_PMO_PSTATE_CANDIDATE_LIST_SIZE; i++) {
		if (validate_pstate_support_strategy_cofunctionality(pmo, display_config, &strategy_list[i])) {
			insert_into_candidate_list(&strategy_list[i], display_config->display_config.num_streams, s);
		}
	}

	if (s->pmo_dcn4.num_pstate_candidates > 0) {
		s->pmo_dcn4.cur_pstate_candidate = -1;
		return true;
	} else {
		return false;
	}
}

static void reset_display_configuration(struct display_configuation_with_meta *display_config)
{
	unsigned int plane_index;
	unsigned int stream_index;
	struct dml2_plane_parameters *plane;

	for (stream_index = 0; stream_index < display_config->display_config.num_streams; stream_index++) {
		display_config->stage3.stream_svp_meta[stream_index].valid = false;

		display_config->display_config.stream_descriptors[stream_index].overrides.minimize_active_latency_hiding = false;
		display_config->display_config.overrides.best_effort_min_active_latency_hiding_us = 0;
	}

	for (plane_index = 0; plane_index < display_config->display_config.num_planes; plane_index++) {
		plane = &display_config->display_config.plane_descriptors[plane_index];

		// Unset SubVP
		plane->overrides.legacy_svp_config = dml2_svp_mode_override_auto;

		// Remove reserve time
		plane->overrides.reserved_vblank_time_ns = 0;

		// Reset strategy to auto
		plane->overrides.uclk_pstate_change_strategy = dml2_uclk_pstate_change_strategy_auto;

		display_config->stage3.pstate_switch_modes[plane_index] = dml2_uclk_pstate_support_method_not_supported;
	}
}

static void setup_planes_for_drr_by_mask(struct display_configuation_with_meta *display_config,
	struct dml2_pmo_instance *pmo,
	int plane_mask)
{
	unsigned char plane_index;
	struct dml2_plane_parameters *plane;

	for (plane_index = 0; plane_index < display_config->display_config.num_planes; plane_index++) {
		if (is_bit_set_in_bitfield(plane_mask, plane_index)) {
			plane = &display_config->display_config.plane_descriptors[plane_index];

			plane->overrides.uclk_pstate_change_strategy = dml2_uclk_pstate_change_strategy_force_drr;

			display_config->stage3.pstate_switch_modes[plane_index] = dml2_uclk_pstate_support_method_fw_drr;

		}
	}
}

static void setup_planes_for_svp_by_mask(struct display_configuation_with_meta *display_config,
	struct dml2_pmo_instance *pmo,
	int plane_mask)
{
	struct dml2_pmo_scratch *scratch = &pmo->scratch;

	unsigned char plane_index;
	int stream_index = -1;

	for (plane_index = 0; plane_index < display_config->display_config.num_planes; plane_index++) {
		if (is_bit_set_in_bitfield(plane_mask, plane_index)) {
			stream_index = (char)display_config->display_config.plane_descriptors[plane_index].stream_index;
			display_config->stage3.pstate_switch_modes[plane_index] = dml2_uclk_pstate_support_method_fw_subvp_phantom;
		}
	}

	if (stream_index >= 0) {
		memcpy(&display_config->stage3.stream_svp_meta[stream_index],
			&scratch->pmo_dcn4.stream_svp_meta[stream_index],
			sizeof(struct dml2_implicit_svp_meta));
	}
}

static void setup_planes_for_svp_drr_by_mask(struct display_configuation_with_meta *display_config,
	struct dml2_pmo_instance *pmo,
	int plane_mask)
{
	struct dml2_pmo_scratch *scratch = &pmo->scratch;

	unsigned char plane_index;
	int stream_index = -1;

	for (plane_index = 0; plane_index < display_config->display_config.num_planes; plane_index++) {
		if (is_bit_set_in_bitfield(plane_mask, plane_index)) {
			stream_index = (char)display_config->display_config.plane_descriptors[plane_index].stream_index;
			display_config->stage3.pstate_switch_modes[plane_index] = dml2_uclk_pstate_support_method_fw_subvp_phantom_drr;
		}
	}

	if (stream_index >= 0) {
		memcpy(&display_config->stage3.stream_svp_meta[stream_index],
			&scratch->pmo_dcn4.stream_svp_meta[stream_index],
			sizeof(struct dml2_implicit_svp_meta));
	}
}

static void setup_planes_for_vblank_by_mask(struct display_configuation_with_meta *display_config,
	struct dml2_pmo_instance *pmo,
	int plane_mask)
{
	unsigned char plane_index;
	struct dml2_plane_parameters *plane;

	for (plane_index = 0; plane_index < display_config->display_config.num_planes; plane_index++) {
		if (is_bit_set_in_bitfield(plane_mask, plane_index)) {
			plane = &display_config->display_config.plane_descriptors[plane_index];

			plane->overrides.reserved_vblank_time_ns = (long)math_max2(pmo->soc_bb->power_management_parameters.dram_clk_change_blackout_us * 1000.0,
					plane->overrides.reserved_vblank_time_ns);

			display_config->stage3.pstate_switch_modes[plane_index] = dml2_uclk_pstate_support_method_vblank;

		}
	}
}

static void setup_planes_for_vblank_drr_by_mask(struct display_configuation_with_meta *display_config,
	struct dml2_pmo_instance *pmo,
	int plane_mask)
{
	unsigned char plane_index;
	struct dml2_plane_parameters *plane;

	for (plane_index = 0; plane_index < display_config->display_config.num_planes; plane_index++) {
		if (is_bit_set_in_bitfield(plane_mask, plane_index)) {
			plane = &display_config->display_config.plane_descriptors[plane_index];
			plane->overrides.reserved_vblank_time_ns = (long)(pmo->soc_bb->power_management_parameters.dram_clk_change_blackout_us * 1000);

			display_config->stage3.pstate_switch_modes[plane_index] = dml2_uclk_pstate_support_method_fw_vblank_drr;
		}
	}
}

static void setup_planes_for_vactive_by_mask(struct display_configuation_with_meta *display_config,
	struct dml2_pmo_instance *pmo,
	int plane_mask)
{
	unsigned char plane_index;
	unsigned int stream_index;

	for (plane_index = 0; plane_index < display_config->display_config.num_planes; plane_index++) {
		if (is_bit_set_in_bitfield(plane_mask, plane_index)) {
			stream_index = display_config->display_config.plane_descriptors[plane_index].stream_index;

			display_config->stage3.pstate_switch_modes[plane_index] = dml2_uclk_pstate_support_method_vactive;

			if (!pmo->options->disable_vactive_det_fill_bw_pad) {
				display_config->display_config.plane_descriptors[plane_index].overrides.max_vactive_det_fill_delay_us =
					(unsigned int)math_floor(pmo->scratch.pmo_dcn4.stream_fams2_meta[stream_index].method_vactive.max_vactive_det_fill_delay_us);
			}
		}
	}
}

static void setup_planes_for_vactive_drr_by_mask(struct display_configuation_with_meta *display_config,
	struct dml2_pmo_instance *pmo,
	int plane_mask)
{
	unsigned char plane_index;
	unsigned int stream_index;

	for (plane_index = 0; plane_index < display_config->display_config.num_planes; plane_index++) {
		if (is_bit_set_in_bitfield(plane_mask, plane_index)) {
			stream_index = display_config->display_config.plane_descriptors[plane_index].stream_index;

			display_config->stage3.pstate_switch_modes[plane_index] = dml2_uclk_pstate_support_method_fw_vactive_drr;

			if (!pmo->options->disable_vactive_det_fill_bw_pad) {
				display_config->display_config.plane_descriptors[plane_index].overrides.max_vactive_det_fill_delay_us =
					(unsigned int)math_floor(pmo->scratch.pmo_dcn4.stream_fams2_meta[stream_index].method_vactive.max_vactive_det_fill_delay_us);
			}
		}
	}
}

static bool setup_display_config(struct display_configuation_with_meta *display_config, struct dml2_pmo_instance *pmo, int strategy_index)
{
	struct dml2_pmo_scratch *scratch = &pmo->scratch;

	bool fams2_required = false;
	bool success = true;
	unsigned int stream_index;

	reset_display_configuration(display_config);

	for (stream_index = 0; stream_index < display_config->display_config.num_streams; stream_index++) {

		if (pmo->scratch.pmo_dcn4.pstate_strategy_candidates[strategy_index].per_stream_pstate_method[stream_index] == dml2_pmo_pstate_strategy_na) {
			success = false;
			break;
		} else if (scratch->pmo_dcn4.pstate_strategy_candidates[strategy_index].per_stream_pstate_method[stream_index] == dml2_pmo_pstate_strategy_vactive) {
			setup_planes_for_vactive_by_mask(display_config, pmo, scratch->pmo_dcn4.stream_plane_mask[stream_index]);
		} else if (scratch->pmo_dcn4.pstate_strategy_candidates[strategy_index].per_stream_pstate_method[stream_index] == dml2_pmo_pstate_strategy_vblank) {
			setup_planes_for_vblank_by_mask(display_config, pmo, scratch->pmo_dcn4.stream_plane_mask[stream_index]);
		} else if (scratch->pmo_dcn4.pstate_strategy_candidates[strategy_index].per_stream_pstate_method[stream_index] == dml2_pmo_pstate_strategy_fw_svp) {
			fams2_required = true;
			setup_planes_for_svp_by_mask(display_config, pmo, scratch->pmo_dcn4.stream_plane_mask[stream_index]);
		} else if (scratch->pmo_dcn4.pstate_strategy_candidates[strategy_index].per_stream_pstate_method[stream_index] == dml2_pmo_pstate_strategy_fw_vactive_drr) {
			fams2_required = true;
			setup_planes_for_vactive_drr_by_mask(display_config, pmo, scratch->pmo_dcn4.stream_plane_mask[stream_index]);
		} else if (scratch->pmo_dcn4.pstate_strategy_candidates[strategy_index].per_stream_pstate_method[stream_index] == dml2_pmo_pstate_strategy_fw_vblank_drr) {
			fams2_required = true;
			setup_planes_for_vblank_drr_by_mask(display_config, pmo, scratch->pmo_dcn4.stream_plane_mask[stream_index]);
		} else if (scratch->pmo_dcn4.pstate_strategy_candidates[strategy_index].per_stream_pstate_method[stream_index] == dml2_pmo_pstate_strategy_fw_svp_drr) {
			fams2_required = true;
			setup_planes_for_svp_drr_by_mask(display_config, pmo, scratch->pmo_dcn4.stream_plane_mask[stream_index]);
		} else if (scratch->pmo_dcn4.pstate_strategy_candidates[strategy_index].per_stream_pstate_method[stream_index] == dml2_pmo_pstate_strategy_fw_drr) {
			fams2_required = true;
			setup_planes_for_drr_by_mask(display_config, pmo, scratch->pmo_dcn4.stream_plane_mask[stream_index]);
		}
	}

	/* copy FAMS2 meta */
	if (success) {
		display_config->stage3.fams2_required = fams2_required;
		memcpy(&display_config->stage3.stream_fams2_meta,
			&scratch->pmo_dcn4.stream_fams2_meta,
			sizeof(struct dml2_fams2_meta) * DML2_MAX_PLANES);
	}

	return success;
}

static int get_minimum_reserved_time_us_for_planes(struct display_configuation_with_meta *display_config, int plane_mask)
{
	int min_time_us = 0xFFFFFF;
	unsigned char plane_index = 0;

	for (plane_index = 0; plane_index < display_config->display_config.num_planes; plane_index++) {
		if (is_bit_set_in_bitfield(plane_mask, plane_index)) {
			if (min_time_us > (display_config->display_config.plane_descriptors[plane_index].overrides.reserved_vblank_time_ns / 1000))
				min_time_us = display_config->display_config.plane_descriptors[plane_index].overrides.reserved_vblank_time_ns / 1000;
		}
	}
	return min_time_us;
}

bool pmo_dcn4_fams2_test_for_pstate_support(struct dml2_pmo_test_for_pstate_support_in_out *in_out)
{
	bool p_state_supported = true;
	unsigned int stream_index;
	struct dml2_pmo_scratch *s = &in_out->instance->scratch;

	int MIN_VACTIVE_MARGIN_VBLANK = 0;
	int MIN_VACTIVE_MARGIN_DRR = 0;
	int REQUIRED_RESERVED_TIME = 0;

	if (in_out->base_display_config->display_config.overrides.all_streams_blanked) {
		return true;
	}

	MIN_VACTIVE_MARGIN_VBLANK = INT_MIN;
	MIN_VACTIVE_MARGIN_DRR = INT_MIN;
	REQUIRED_RESERVED_TIME = (int)in_out->instance->soc_bb->power_management_parameters.dram_clk_change_blackout_us;

	if (s->pmo_dcn4.cur_pstate_candidate < 0)
		return false;

	for (stream_index = 0; stream_index < in_out->base_display_config->display_config.num_streams; stream_index++) {
		struct dml2_fams2_meta *stream_fams2_meta = &s->pmo_dcn4.stream_fams2_meta[stream_index];

		if (s->pmo_dcn4.pstate_strategy_candidates[s->pmo_dcn4.cur_pstate_candidate].per_stream_pstate_method[stream_index] == dml2_pmo_pstate_strategy_vactive ||
				s->pmo_dcn4.pstate_strategy_candidates[s->pmo_dcn4.cur_pstate_candidate].per_stream_pstate_method[stream_index] == dml2_pmo_pstate_strategy_fw_vactive_drr) {
			if (get_vactive_pstate_margin(in_out->base_display_config, s->pmo_dcn4.stream_plane_mask[stream_index]) < (MIN_VACTIVE_MARGIN_PCT * in_out->instance->soc_bb->power_management_parameters.dram_clk_change_blackout_us) ||
					get_vactive_det_fill_latency_delay_us(in_out->base_display_config, s->pmo_dcn4.stream_plane_mask[stream_index]) > stream_fams2_meta->method_vactive.max_vactive_det_fill_delay_us) {
				p_state_supported = false;
				break;
			}
		} else if (s->pmo_dcn4.pstate_strategy_candidates[s->pmo_dcn4.cur_pstate_candidate].per_stream_pstate_method[stream_index] == dml2_pmo_pstate_strategy_vblank ||
				s->pmo_dcn4.pstate_strategy_candidates[s->pmo_dcn4.cur_pstate_candidate].per_stream_pstate_method[stream_index] == dml2_pmo_pstate_strategy_fw_vblank_drr) {
			if (get_minimum_reserved_time_us_for_planes(in_out->base_display_config, s->pmo_dcn4.stream_plane_mask[stream_index]) <
				REQUIRED_RESERVED_TIME ||
				get_vactive_pstate_margin(in_out->base_display_config, s->pmo_dcn4.stream_plane_mask[stream_index]) < MIN_VACTIVE_MARGIN_VBLANK) {
				p_state_supported = false;
				break;
			}
		} else if (s->pmo_dcn4.pstate_strategy_candidates[s->pmo_dcn4.cur_pstate_candidate].per_stream_pstate_method[stream_index] == dml2_pmo_pstate_strategy_fw_svp ||
				s->pmo_dcn4.pstate_strategy_candidates[s->pmo_dcn4.cur_pstate_candidate].per_stream_pstate_method[stream_index] == dml2_pmo_pstate_strategy_fw_svp_drr) {
			if (in_out->base_display_config->stage3.stream_svp_meta[stream_index].valid == false) {
				p_state_supported = false;
				break;
			}
		} else if (s->pmo_dcn4.pstate_strategy_candidates[s->pmo_dcn4.cur_pstate_candidate].per_stream_pstate_method[stream_index] == dml2_pmo_pstate_strategy_fw_drr) {
			if (!all_planes_match_method(in_out->base_display_config, s->pmo_dcn4.stream_plane_mask[stream_index], dml2_pmo_pstate_strategy_fw_drr) ||
				get_vactive_pstate_margin(in_out->base_display_config, s->pmo_dcn4.stream_plane_mask[stream_index]) < MIN_VACTIVE_MARGIN_DRR) {
				p_state_supported = false;
				break;
			}
		} else if (s->pmo_dcn4.pstate_strategy_candidates[s->pmo_dcn4.cur_pstate_candidate].per_stream_pstate_method[stream_index] == dml2_pmo_pstate_strategy_na) {
			p_state_supported = false;
			break;
		}
	}

	return p_state_supported;
}

bool pmo_dcn4_fams2_optimize_for_pstate_support(struct dml2_pmo_optimize_for_pstate_support_in_out *in_out)
{
	bool success = false;
	struct dml2_pmo_scratch *s = &in_out->instance->scratch;

	memcpy(in_out->optimized_display_config, in_out->base_display_config, sizeof(struct display_configuation_with_meta));

	if (in_out->last_candidate_failed) {
		if (s->pmo_dcn4.pstate_strategy_candidates[s->pmo_dcn4.cur_pstate_candidate].allow_state_increase &&
			s->pmo_dcn4.cur_latency_index < s->pmo_dcn4.max_latency_index - 1) {
			s->pmo_dcn4.cur_latency_index++;

			success = true;
		}
	}

	if (!success) {
		s->pmo_dcn4.cur_latency_index = s->pmo_dcn4.min_latency_index;
		s->pmo_dcn4.cur_pstate_candidate++;

		if (s->pmo_dcn4.cur_pstate_candidate < s->pmo_dcn4.num_pstate_candidates) {
			success = true;
		}
	}

	if (success) {
		in_out->optimized_display_config->stage3.min_clk_index_for_latency = s->pmo_dcn4.cur_latency_index;
		setup_display_config(in_out->optimized_display_config, in_out->instance, in_out->instance->scratch.pmo_dcn4.cur_pstate_candidate);
	}

	return success;
}

bool pmo_dcn4_fams2_init_for_stutter(struct dml2_pmo_init_for_stutter_in_out *in_out)
{
	bool success = true;
	struct dml2_pmo_instance *pmo = in_out->instance;
	bool stutter_period_meets_z8_eco = true;
	bool z8_stutter_optimization_too_expensive = false;
	double line_time_us, vblank_nom_time_us;

	unsigned int i;

	if (pmo->soc_bb->power_management_parameters.z8_stutter_exit_latency_us > 0 &&
		pmo->soc_bb->power_management_parameters.stutter_enter_plus_exit_latency_us > 0 &&
		pmo->soc_bb->power_management_parameters.z8_stutter_exit_latency_us < pmo->soc_bb->power_management_parameters.stutter_enter_plus_exit_latency_us)
		return false; // Unexpected SoCBB setup

	for (i = 0; i < in_out->base_display_config->display_config.num_planes; i++) {
		if (in_out->base_display_config->mode_support_result.cfg_support_info.plane_support_info[i].active_latency_hiding_us <
			pmo->soc_bb->power_management_parameters.z8_stutter_exit_latency_us + pmo->soc_bb->power_management_parameters.z8_min_idle_time) {
			stutter_period_meets_z8_eco = false;
			break;
		}
	}

	for (i = 0; i < in_out->base_display_config->display_config.num_streams; i++) {
		line_time_us = (double)in_out->base_display_config->display_config.stream_descriptors[i].timing.h_total / (in_out->base_display_config->display_config.stream_descriptors[i].timing.pixel_clock_khz * 1000) * 1000000;
		vblank_nom_time_us = line_time_us * in_out->base_display_config->display_config.stream_descriptors[i].timing.vblank_nom;

		if (vblank_nom_time_us < pmo->soc_bb->power_management_parameters.z8_stutter_exit_latency_us) {
			z8_stutter_optimization_too_expensive = true;
			break;
		}
	}

	pmo->scratch.pmo_dcn4.num_stutter_candidates = 0;
	pmo->scratch.pmo_dcn4.cur_stutter_candidate = 0;

	if (stutter_period_meets_z8_eco && !z8_stutter_optimization_too_expensive) {
		if (pmo->soc_bb->power_management_parameters.z8_stutter_exit_latency_us > 0) {
			pmo->scratch.pmo_dcn4.optimal_vblank_reserved_time_for_stutter_us[pmo->scratch.pmo_dcn4.num_stutter_candidates] = (unsigned int)pmo->soc_bb->power_management_parameters.z8_stutter_exit_latency_us;
			pmo->scratch.pmo_dcn4.num_stutter_candidates++;
			pmo->scratch.pmo_dcn4.z8_vblank_optimizable = true;
		}
	} else {
		pmo->scratch.pmo_dcn4.z8_vblank_optimizable = false;
	}

	if (pmo->soc_bb->power_management_parameters.stutter_enter_plus_exit_latency_us > 0) {
		pmo->scratch.pmo_dcn4.optimal_vblank_reserved_time_for_stutter_us[pmo->scratch.pmo_dcn4.num_stutter_candidates] = (unsigned int)pmo->soc_bb->power_management_parameters.stutter_enter_plus_exit_latency_us;
		pmo->scratch.pmo_dcn4.num_stutter_candidates++;
	}

	if (pmo->scratch.pmo_dcn4.num_stutter_candidates == 0)
		success = false;

	return success;
}

bool pmo_dcn4_fams2_test_for_stutter(struct dml2_pmo_test_for_stutter_in_out *in_out)
{
	bool success = true;
	struct dml2_pmo_instance *pmo = in_out->instance;

	unsigned int i;

	for (i = 0; i < in_out->base_display_config->display_config.num_planes; i++) {
		if (pmo->soc_bb->power_management_parameters.z8_stutter_exit_latency_us > 0 &&
			pmo->scratch.pmo_dcn4.z8_vblank_optimizable &&
			in_out->base_display_config->display_config.plane_descriptors[i].overrides.reserved_vblank_time_ns < (int)pmo->soc_bb->power_management_parameters.z8_stutter_exit_latency_us * 1000) {
			success = false;
			break;
		}
		if (pmo->soc_bb->power_management_parameters.stutter_enter_plus_exit_latency_us > 0 &&
			in_out->base_display_config->display_config.plane_descriptors[i].overrides.reserved_vblank_time_ns < (int)pmo->soc_bb->power_management_parameters.stutter_enter_plus_exit_latency_us * 1000) {
			success = false;
			break;
		}
	}

	return success;
}

bool pmo_dcn4_fams2_optimize_for_stutter(struct dml2_pmo_optimize_for_stutter_in_out *in_out)
{
	bool success = false;
	struct dml2_pmo_instance *pmo = in_out->instance;
	unsigned int i;

	memcpy(in_out->optimized_display_config, in_out->base_display_config, sizeof(struct display_configuation_with_meta));

	if (!in_out->last_candidate_failed) {
		if (pmo->scratch.pmo_dcn4.cur_stutter_candidate < pmo->scratch.pmo_dcn4.num_stutter_candidates) {
			for (i = 0; i < in_out->optimized_display_config->display_config.num_planes; i++) {
				/* take the max of the current and the optimal reserved time */
				in_out->optimized_display_config->display_config.plane_descriptors[i].overrides.reserved_vblank_time_ns =
						(long)math_max2(pmo->scratch.pmo_dcn4.optimal_vblank_reserved_time_for_stutter_us[pmo->scratch.pmo_dcn4.cur_stutter_candidate] * 1000,
						in_out->optimized_display_config->display_config.plane_descriptors[i].overrides.reserved_vblank_time_ns);
			}

			success = true;
		}
	}

	return success;
}
