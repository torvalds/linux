// SPDX-License-Identifier: MIT
//
// Copyright 2024-2025 Advanced Micro Devices, Inc.

#include "dml2_pmo_dcn5.h"
#include "dml2_pmo_dcn4_fams2.h"
#include "dml2_pmo_dcn5_stage_optimizers.h"
#include "lib_float_math.h"
#include "dml2_debug.h"

static const struct dml2_pmo_pstate_strategy base_pstate_strategy_list_1_display[] = {
	// VActive Preferred
	{
		.per_stream_pstate_method = { dml2_pstate_method_vactive, dml2_pstate_method_na, dml2_pstate_method_na, dml2_pstate_method_na },
		.allow_state_increase = true,
	},

	// Then VBlank
	{
		.per_stream_pstate_method = { dml2_pstate_method_vblank, dml2_pstate_method_na, dml2_pstate_method_na, dml2_pstate_method_na },
		.allow_state_increase = false,
	},

	// Then DRR
	{
		.per_stream_pstate_method = { dml2_pstate_method_fw_drr, dml2_pstate_method_na, dml2_pstate_method_na, dml2_pstate_method_na },
		.allow_state_increase = true,
	},

	// Finally VBlank, but allow base clocks for latency to increase
	/*
	{
		.per_stream_pstate_method = { dml2_pstate_method_vblank, dml2_pstate_method_na, dml2_pstate_method_na, dml2_pstate_method_na },
		.allow_state_increase = true,
	},
	*/
};

static const int base_pstate_strategy_list_1_display_size = sizeof(base_pstate_strategy_list_1_display) / sizeof(struct dml2_pmo_pstate_strategy);

static const struct dml2_pmo_pstate_strategy base_pstate_strategy_list_2_display[] = {
	// VActive only is preferred
	{
		.per_stream_pstate_method = { dml2_pstate_method_vactive, dml2_pstate_method_vactive, dml2_pstate_method_na, dml2_pstate_method_na },
		.allow_state_increase = true,
	},

	// Then VActive + VBlank
	{
		.per_stream_pstate_method = { dml2_pstate_method_vactive, dml2_pstate_method_vblank, dml2_pstate_method_na, dml2_pstate_method_na },
		.allow_state_increase = false,
	},

	// Then VBlank only
	{
		.per_stream_pstate_method = { dml2_pstate_method_vblank, dml2_pstate_method_vblank, dml2_pstate_method_na, dml2_pstate_method_na },
		.allow_state_increase = false,
	},

	// Then DRR + VActive
	{
		.per_stream_pstate_method = { dml2_pstate_method_vactive, dml2_pstate_method_fw_drr, dml2_pstate_method_na, dml2_pstate_method_na },
		.allow_state_increase = true,
	},

	// Then DRR + DRR
	{
		.per_stream_pstate_method = { dml2_pstate_method_fw_drr, dml2_pstate_method_fw_drr, dml2_pstate_method_na, dml2_pstate_method_na },
		.allow_state_increase = true,
	},

	// Finally VBlank, but allow base clocks for latency to increase
	/*
	{
		.per_stream_pstate_method = { dml2_pstate_method_vblank, dml2_pstate_method_vblank, dml2_pstate_method_na, dml2_pstate_method_na },
		.allow_state_increase = true,
	},
	*/
};

static const int base_pstate_strategy_list_2_display_size = sizeof(base_pstate_strategy_list_2_display) / sizeof(struct dml2_pmo_pstate_strategy);

static const struct dml2_pmo_pstate_strategy base_pstate_strategy_list_3_display[] = {
	// All VActive
	{
		.per_stream_pstate_method = { dml2_pstate_method_vactive, dml2_pstate_method_vactive, dml2_pstate_method_vactive, dml2_pstate_method_na },
		.allow_state_increase = true,
	},

	// VActive + 1 VBlank
	{
		.per_stream_pstate_method = { dml2_pstate_method_vactive, dml2_pstate_method_vactive, dml2_pstate_method_vblank, dml2_pstate_method_na },
		.allow_state_increase = false,
	},

	// All VBlank
	{
		.per_stream_pstate_method = { dml2_pstate_method_vblank, dml2_pstate_method_vblank, dml2_pstate_method_vblank, dml2_pstate_method_na },
		.allow_state_increase = false,
	},

	// All DRR
	{
		.per_stream_pstate_method = { dml2_pstate_method_fw_drr, dml2_pstate_method_fw_drr, dml2_pstate_method_fw_drr, dml2_pstate_method_na },
		.allow_state_increase = true,
	},

	// All VBlank, with state increase allowed
	/*
	{
		.per_stream_pstate_method = { dml2_pstate_method_vblank, dml2_pstate_method_vblank, dml2_pstate_method_vblank, dml2_pstate_method_na },
		.allow_state_increase = true,
	},
	*/
};

static const int base_pstate_strategy_list_3_display_size = sizeof(base_pstate_strategy_list_3_display) / sizeof(struct dml2_pmo_pstate_strategy);

static const struct dml2_pmo_pstate_strategy base_pstate_strategy_list_4_display[] = {
	// All VActive
	{
		.per_stream_pstate_method = { dml2_pstate_method_vactive, dml2_pstate_method_vactive, dml2_pstate_method_vactive, dml2_pstate_method_vactive },
		.allow_state_increase = true,
	},

	// VActive + 1 VBlank
	{
		.per_stream_pstate_method = { dml2_pstate_method_vactive, dml2_pstate_method_vactive, dml2_pstate_method_vactive, dml2_pstate_method_vblank },
		.allow_state_increase = false,
	},

	// All Vblank
	{
		.per_stream_pstate_method = { dml2_pstate_method_vblank, dml2_pstate_method_vblank, dml2_pstate_method_vblank, dml2_pstate_method_vblank },
		.allow_state_increase = false,
	},

	// All DRR
	{
		.per_stream_pstate_method = { dml2_pstate_method_fw_drr, dml2_pstate_method_fw_drr, dml2_pstate_method_fw_drr, dml2_pstate_method_fw_drr },
		.allow_state_increase = true,
	},

	// All VBlank, with state increase allowed
	/*
	{
		.per_stream_pstate_method = { dml2_pstate_method_vblank, dml2_pstate_method_vblank, dml2_pstate_method_vblank, dml2_pstate_method_vblank },
		.allow_state_increase = true,
	},
	*/
};

static const int base_pstate_strategy_list_4_display_size = sizeof(base_pstate_strategy_list_4_display) / sizeof(struct dml2_pmo_pstate_strategy);

static void dml2_pmo_dcn5_assign_pstate_strategies(struct dml2_pmo_instance *pmo)
{
	int i = 0;

	/* generate permutations of p-state configs from base strategy list */
	for (i = 1; i <= PMO_DCN4_MAX_DISPLAYS; i++) {
		switch (i) {
		case 1:
			DML_ASSERT(base_pstate_strategy_list_1_display_size <= PMO_DCN4_MAX_BASE_STRATEGIES);

			/* populate list */
			pmo_dcn4_fams2_expand_base_pstate_strategies(
				base_pstate_strategy_list_1_display,
				base_pstate_strategy_list_1_display_size,
				i,
				pmo->init_data.pmo_dcn4.expanded_strategy_list_1_display,
				&pmo->init_data.pmo_dcn4.num_expanded_strategies_per_list[i - 1]);
			break;
		case 2:
			DML_ASSERT(base_pstate_strategy_list_2_display_size <= PMO_DCN4_MAX_BASE_STRATEGIES);

			/* populate list */
			pmo_dcn4_fams2_expand_base_pstate_strategies(
				base_pstate_strategy_list_2_display,
				base_pstate_strategy_list_2_display_size,
				i,
				pmo->init_data.pmo_dcn4.expanded_strategy_list_2_display,
				&pmo->init_data.pmo_dcn4.num_expanded_strategies_per_list[i - 1]);
			break;
		case 3:
			DML_ASSERT(base_pstate_strategy_list_3_display_size <= PMO_DCN4_MAX_BASE_STRATEGIES);

			/* populate list */
			pmo_dcn4_fams2_expand_base_pstate_strategies(
				base_pstate_strategy_list_3_display,
				base_pstate_strategy_list_3_display_size,
				i,
				pmo->init_data.pmo_dcn4.expanded_strategy_list_3_display,
				&pmo->init_data.pmo_dcn4.num_expanded_strategies_per_list[i - 1]);
			break;
		case 4:
			DML_ASSERT(base_pstate_strategy_list_4_display_size <= PMO_DCN4_MAX_BASE_STRATEGIES);

			/* populate list */
			pmo_dcn4_fams2_expand_base_pstate_strategies(
				base_pstate_strategy_list_4_display,
				base_pstate_strategy_list_4_display_size,
				i,
				pmo->init_data.pmo_dcn4.expanded_strategy_list_4_display,
				&pmo->init_data.pmo_dcn4.num_expanded_strategies_per_list[i - 1]);
			break;
		}
	}
}

bool dml2_pmo_dcn5_initialize(struct dml2_pmo_initialize_in_out *in_out)
{
	struct dml2_pmo_instance *pmo = in_out->instance;

	pmo->ip_caps = in_out->ip_caps;
	pmo->options = in_out->options;
	pmo->utm_soc_bb = in_out->utm_soc_bb;
	pmo->mpc_combine_limit = 2;
	pmo->odm_combine_limit = 4;
	pmo->fams_params.v2.drr.refresh_rate_limit_max = 1000;
	pmo->fams_params.v2.drr.refresh_rate_limit_min = 119;
	dml2_pmo_dcn5_assign_pstate_strategies(pmo);

	dml2_pmo_dcn5_stage_optimizer_mcache_create(pmo, &pmo->stage_optimizers[dml2_pmo_stage_index_mcache]);
	dml2_pmo_dcn5_stage_optimizer_uclk_pstate_create(pmo, &pmo->stage_optimizers[dml2_pmo_stage_index_uclk_pstate]);
	dml2_pmo_dcn5_stage_optimizer_qos_create(pmo, &pmo->stage_optimizers[dml2_pmo_stage_index_qos]);
	dml2_pmo_dcn5_stage_optimizer_vmin_create(pmo, &pmo->stage_optimizers[dml2_pmo_stage_index_vmin]);
	dml2_pmo_dcn5_stage_optimizer_stutter_create(pmo, &pmo->stage_optimizers[dml2_pmo_stage_index_stutter]);

	return true;
}

int dml2_pmo_dcn5_get_ordered_mandatory_stage_optimizers(struct dml2_pmo_instance *pmo,
		struct dml2_pmo_stage_optimizer **stages)
{
	int count = 0;

	if (!pmo->options->force_optional_mcache_support)
		stages[count++] = &pmo->stage_optimizers[dml2_pmo_stage_index_mcache];
	if (!pmo->options->force_optional_uclk_pstate_support)
		stages[count++] = &pmo->stage_optimizers[dml2_pmo_stage_index_uclk_pstate];

	return count;
}

int dml2_pmo_dcn5_get_ordered_optional_stages_optimizers(struct dml2_pmo_instance *pmo,
		struct dml2_pmo_stage_optimizer **stages)
{
	int count = 0;

	if (pmo->options->force_optional_mcache_support)
		stages[count++] = &pmo->stage_optimizers[dml2_pmo_stage_index_mcache];
	if (pmo->options->force_optional_uclk_pstate_support)
		stages[count++] = &pmo->stage_optimizers[dml2_pmo_stage_index_uclk_pstate];
	stages[count++] = &pmo->stage_optimizers[dml2_pmo_stage_index_qos];
	stages[count++] = &pmo->stage_optimizers[dml2_pmo_stage_index_vmin];
	stages[count++] = &pmo->stage_optimizers[dml2_pmo_stage_index_stutter];

	return count;
}

static void dml2_pmo_dcn5_assign_timing_groups(struct dml2_optimization_worksheet *worksheet)
{
	const struct dml2_stream_parameters *cur_stream, *other_stream;
	const struct dml2_plane_parameters *plane;
	unsigned int i, j;

	worksheet->timing_group_count = 0;
	memset(worksheet->timing_group_ids, 0xFF, sizeof(worksheet->timing_group_ids));

	/* assign timing group IDs per stream using the same synchronization logic as
	 * dcn5_build_synchronized_timing_groups: streams with identical timings are
	 * placed in the same group; DRR-enabled streams are never merged with others */
	for (i = 0; i < worksheet->orig_dispcfg->num_planes; i++) {
		if (worksheet->timing_group_ids[i] != 0xFFFFFFFF)
			/* already assigned */
			continue;
		worksheet->timing_group_ids[i] = worksheet->timing_group_count;
		worksheet->timing_group_count++;

		plane = &worksheet->orig_dispcfg->plane_descriptors[i];
		cur_stream = &worksheet->orig_dispcfg->stream_descriptors[plane->stream_index];

		for (j = i + 1; j < worksheet->orig_dispcfg->num_planes; j++) {
			if (worksheet->timing_group_ids[j] != 0xFFFFFFFF)
				/* already assigned */
				continue;
			plane = &worksheet->orig_dispcfg->plane_descriptors[j];
			other_stream = &worksheet->orig_dispcfg->stream_descriptors[plane->stream_index];

			if (cur_stream == other_stream)
				/* same stream, must be in the same group */
				worksheet->timing_group_ids[j] = worksheet->timing_group_ids[i];

			if (memcmp(&cur_stream->timing, &other_stream->timing, sizeof(struct dml2_timing_cfg)) == 0
					&& !cur_stream->timing.drr_config.enabled)
				/* identical timings, should be in the same group */
				worksheet->timing_group_ids[j] = worksheet->timing_group_ids[i];
		}
	}
}

void dml2_pmo_dcn5_initialize_worksheet(struct dml2_pmo_instance *pmo,
		const struct dml2_display_cfg *dispcfg,
		struct dml2_optimization_worksheet *worksheet)
{
	const struct dml2_sop_table *sop_table = &pmo->utm_soc_bb->sop_table;

	DML_LOG_COMP_IF_ENTER();
	memset(worksheet, 0, sizeof(struct dml2_optimization_worksheet));
	worksheet->orig_dispcfg = dispcfg;
	worksheet->cur.config.min_sop_index = sop_table->get_highest_sop_index(sop_table);
	worksheet->cur.unvalidated_change.raw = 0xFFFF;
	dml2_pmo_dcn5_assign_timing_groups(worksheet);
	DML_LOG_COMP_IF_EXIT();
}

static bool dml2_pmo_dcn5_check_total_pipe_usage(struct dml2_pmo_instance *pmo,
		const struct dml2_optimization_worksheet *worksheet)
{
	unsigned int i;
	const struct dml2_display_cfg *orig_dispcfg = worksheet->orig_dispcfg;
	unsigned int total_pipe_usage = 0;

	for (i = 0; i < orig_dispcfg->num_planes; i++)
		if (worksheet->cur.config.mpc_combine_overrides[i])
			total_pipe_usage += worksheet->cur.config.mpc_combine_overrides[i];
		else if (worksheet->cur.config.odm_combine_overrides[orig_dispcfg->plane_descriptors[i].stream_index])
			total_pipe_usage += worksheet->cur.config.odm_combine_overrides[orig_dispcfg->plane_descriptors[i].stream_index];
		else
			total_pipe_usage += worksheet->validation_result.mode_support.cfg_support_info.plane_support_info[i].dpps_used;

	return total_pipe_usage <= pmo->ip_caps->pipe_count;
}

static bool is_h_timing_divisible_by(const struct dml2_timing_cfg *timing, unsigned int denominator)
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

static bool dml2_pmo_dcn5_check_odm_divisibility(const struct dml2_optimization_worksheet *worksheet)
{
	unsigned int i;
	const struct dml2_timing_cfg *timing;

	for (i = 0; i < worksheet->orig_dispcfg->num_streams; i++) {
		if (worksheet->cur.config.odm_combine_overrides[i]) {
			timing = &worksheet->orig_dispcfg->stream_descriptors[i].timing;
			if (!is_h_timing_divisible_by(timing, worksheet->cur.config.odm_combine_overrides[i])) {
				((struct dml2_optimization_worksheet *)worksheet)->mcache.per_plane_status[i] = false;
				return false;
			}

			if (timing->dsc.overrides.num_slices &&
					timing->dsc.overrides.num_slices % worksheet->cur.config.odm_combine_overrides[i])
				return false;
		}
	}
	return true;
}

enum dml2_status dml2_pmo_dcn5_sanity_check(struct dml2_pmo_instance *pmo,
		const struct dml2_optimization_worksheet *worksheet)
{
	enum dml2_status status = DML2_STATUS_OK;

	DML_LOG_COMP_IF_ENTER();
	if (!dml2_pmo_dcn5_check_total_pipe_usage(pmo, worksheet)) {
		status = DML2_STATUS_VALIDATE_FAIL_PMO_SANITY_TOTAL_PIPE_USAGE;
		goto exit;
	}

	if (!dml2_pmo_dcn5_check_odm_divisibility(worksheet)) {
		status = DML2_STATUS_VALIDATE_FAIL_PMO_SANITY_ODM_DIVISIBILITY;
		goto exit;
	}

exit:
	DML_LOG_DEBUG("%s exit with %s\n", __func__, dml2_status_str(status));
	DML_LOG_COMP_IF_EXIT();

	return status;
}

static void dml2_pmo_dcn5_apply_optimization_to_solution(struct dml2_pmo_instance *pmo,
		const struct dml2_optimization_config *optimization,
		struct dml2_display_solution *solution)
{
	unsigned int i;
	const struct dml2_sop_table *sop_table = &pmo->utm_soc_bb->sop_table;

	solution->unvalidated_change.raw = optimization->unvalidated_change.raw;
	DML_LOG_DEBUG("solution->unvalidated_change.raw = 0x%X\n", solution->unvalidated_change.raw);

	/* sop index */
	sop_table->get_sop_constraint_at_index(sop_table,
			optimization->config.min_sop_index, &solution->sop_constraint);
	DML_LOG_VERBOSE("min_sop_index = %d\n", optimization->config.min_sop_index);

	/* mpc overrides */
	for (i = 0; i < solution->dispcfg.num_planes; i++)
		if (optimization->config.mpc_combine_overrides[i]) {
			solution->dispcfg.plane_descriptors[i].overrides.mpcc_combine_factor =
					optimization->config.mpc_combine_overrides[i];
			DML_LOG_VERBOSE("solution->dispcfg.plane_descriptors[%d].overrides.mpcc_combine_factor = %d\n",
					i, solution->dispcfg.plane_descriptors[i].overrides.mpcc_combine_factor);
		}

	/* odm overrides */
	for (i = 0; i < solution->dispcfg.num_streams; i++) {
		switch (optimization->config.odm_combine_overrides[i]) {
		case 1:
			solution->dispcfg.stream_descriptors[i].overrides.odm_mode = dml2_odm_mode_bypass;
			break;
		case 2:
			solution->dispcfg.stream_descriptors[i].overrides.odm_mode = dml2_odm_mode_combine_2to1;
			break;
		case 3:
			solution->dispcfg.stream_descriptors[i].overrides.odm_mode = dml2_odm_mode_combine_3to1;
			break;
		case 4:
			solution->dispcfg.stream_descriptors[i].overrides.odm_mode = dml2_odm_mode_combine_4to1;
			break;
		default:
			break;
		}
		DML_LOG_VERBOSE("solution->dispcfg.stream_descriptors[%d].overrides.odm_mode = %d\n",
				i, solution->dispcfg.stream_descriptors[i].overrides.odm_mode);
	}

	/* reserved vblank time */
	for (i = 0; i < solution->dispcfg.num_planes; i++) {
		solution->dispcfg.plane_descriptors[i].overrides.reserved_vblank_time_ns = (long) math_max2(
				solution->dispcfg.plane_descriptors[i].overrides.reserved_vblank_time_ns,
				optimization->config.reserved_vblank_time_ns[i]);
		DML_LOG_VERBOSE("solution->dispcfg.plane_descriptors[%d].overrides.reserved_vblank_time_ns = %ld\n",
				i, solution->dispcfg.plane_descriptors[i].overrides.reserved_vblank_time_ns);
	}

	/* mcache allocations */
	for (i = 0; i < solution->dispcfg.num_planes; i++)
		if (optimization->config.mcache_allocations[i].valid)
			memcpy(&solution->mcache_allocations[i], &optimization->config.mcache_allocations[i],
					sizeof(struct dml2_mcache_surface_allocation));

	/* P-State switch method */
	solution->uclk_pstate_params.support = optimization->config.uclk_pstate_support;
	for (i = 0; i < solution->dispcfg.num_planes; i++) {
		solution->uclk_pstate_params.pstate_switch_modes[i] = optimization->config.uclk_pstate_switch_modes[i];
		DML_LOG_VERBOSE("solution->uclk_pstate_params.pstate_switch_modes[%d] = %d\n",
				i, solution->uclk_pstate_params.pstate_switch_modes[i]);
	}

	/* P-State latency hiding */
	for (i = 0; i < solution->dispcfg.num_planes; i++) {
		memcpy(solution->dispcfg.plane_descriptors[i].overrides.max_vactive_det_fill_delay_us,
				optimization->config.max_vactive_det_fill_delay_us[i],
				sizeof(optimization->config.max_vactive_det_fill_delay_us[i]));
		DML_LOG_VERBOSE("solution->dispcfg.plane_descriptors[%d].overrides.max_vactive_det_fill_delay_us[uclk] = %d\n",
				i, solution->dispcfg.plane_descriptors[i].overrides.max_vactive_det_fill_delay_us[dml2_pstate_type_uclk]);
	}

	/* FAMS2 related */
	solution->uclk_pstate_params.fams2_required = optimization->config.fams2_required;
	DML_LOG_VERBOSE("solution->uclk_pstate_params.fams2_required = %s\n", solution->uclk_pstate_params.fams2_required ?
			"true" : "false");
	memcpy(&solution->uclk_pstate_params.stream_pstate_meta,
			&optimization->config.stream_pstate_meta,
			sizeof(struct dml2_pstate_meta) * DML2_MAX_PLANES);

	/* fclk pstate */
	solution->fclk_pstate_support = optimization->config.fclk_pstate_support;
	DML_LOG_VERBOSE("solution->fclk_pstate_support = %s\n", solution->fclk_pstate_support ?
			"true" : "false");
	/* stutter */
	solution->stutter_support_in_vblank = optimization->config.stutter_support_in_vblank;
	solution->z8_stutter_support_in_vblank = optimization->config.z8_stutter_support_in_vblank;
	DML_LOG_VERBOSE("solution->stutter_support_in_vblank = %s\n", solution->stutter_support_in_vblank ?
			"true" : "false");
	DML_LOG_VERBOSE("solution->z8_stutter_support_in_vblank = %s\n", solution->z8_stutter_support_in_vblank ?
			"true" : "false");

	/* dcfclk override */
	if (optimization->config.enable_vmin_dcfclk) {
		solution->dispcfg.overrides.hw.dcfclk_mhz = pmo->utm_soc_bb->vmin_limit.dcfclk_khz / 1000.0;
	}
	DML_LOG_VERBOSE("solution->dispcfg.overrides.hw.dcfclk_mhz = %f\n", solution->dispcfg.overrides.hw.dcfclk_mhz);
}

void dml2_pmo_dcn5_convert_worksheet_to_solution(struct dml2_pmo_instance *pmo,
		const struct dml2_optimization_worksheet *worksheet,
		struct dml2_display_solution *solution)
{
	DML_LOG_COMP_IF_ENTER();

	memset(solution, 0, sizeof(struct dml2_display_solution));
	solution->orig_dispcfg = worksheet->orig_dispcfg;
	memcpy(&solution->dispcfg, worksheet->orig_dispcfg, sizeof(solution->dispcfg));
	memcpy(&solution->validation_result, &worksheet->validation_result, sizeof(struct dml2_validation_result));
	dml2_pmo_dcn5_apply_optimization_to_solution(pmo, &worksheet->cur, solution);
	DML_LOG_COMP_IF_EXIT();
}

void dml2_pmo_dcn5_clear_pre_validation_states(struct dml2_pmo_instance *pmo,
		struct dml2_optimization_worksheet *worksheet)
{
	(void)pmo;
	DML_LOG_COMP_IF_ENTER();
	worksheet->cur.unvalidated_change.raw = 0;
	DML_LOG_DEBUG("worksheet->cur.unvalidated_change.raw = %d\n", worksheet->cur.unvalidated_change.raw);
	DML_LOG_COMP_IF_EXIT();
}

