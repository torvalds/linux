// SPDX-License-Identifier: MIT
//
// Copyright 2025 Advanced Micro Devices, Inc.

#include "dml2_pmo_dcn6.h"
#include "dml2_pmo_dcn4_fams2.h"
#include "dml2_pmo_dcn5_stage_optimizers.h"
#include "dml2_pmo_dcn6_stage_optimizers.h"
#include "dml2_debug.h"
#include "lib_float_math.h"

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

	// Even-Odd
	{
		.per_stream_pstate_method = { dml2_pstate_method_alternate, dml2_pstate_method_na, dml2_pstate_method_na, dml2_pstate_method_na },
		.allow_state_increase = true,
	},
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

	// Even-Odd
	{
		.per_stream_pstate_method = { dml2_pstate_method_alternate, dml2_pstate_method_alternate, dml2_pstate_method_na, dml2_pstate_method_na },
		.allow_state_increase = true,
	},
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

	// Even-Odd
	{
		.per_stream_pstate_method = { dml2_pstate_method_alternate, dml2_pstate_method_alternate, dml2_pstate_method_alternate, dml2_pstate_method_na },
		.allow_state_increase = true,
	},
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

	// Even-Odd
	{
		.per_stream_pstate_method = { dml2_pstate_method_alternate, dml2_pstate_method_alternate, dml2_pstate_method_alternate, dml2_pstate_method_alternate },
		.allow_state_increase = true,
	},
};

static const int base_pstate_strategy_list_4_display_size = sizeof(base_pstate_strategy_list_4_display) / sizeof(struct dml2_pmo_pstate_strategy);

static void dml2_pmo_dcn6_assign_pstate_strategies(struct dml2_pmo_instance *pmo)
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

int dml2_pmo_dcn6a_get_ordered_mandatory_stage_optimizers(struct dml2_pmo_instance *pmo,
	struct dml2_pmo_stage_optimizer **stages)
{
	int count = 0;

	DML_LOG_COMP_IF_ENTER();
	if (!pmo->options->force_optional_ppt_temp_read_admissibility)
		stages[count++] = &pmo->stage_optimizers[dml2_pmo_stage_index_fclk_ppt_temp_read_pstate];
	if (!pmo->options->force_optional_mcache_support)
		stages[count++] = &pmo->stage_optimizers[dml2_pmo_stage_index_mcache];
	if (!pmo->options->force_optional_uclk_pstate_support)
		stages[count++] = &pmo->stage_optimizers[dml2_pmo_stage_index_uclk_pstate];
	DML_LOG_DEBUG("%s exit with %d\n", __func__, count);
	DML_LOG_COMP_IF_EXIT();

	return count;
}

int dml2_pmo_dcn6a_get_ordered_optional_stages_optimizers(struct dml2_pmo_instance *pmo,
	struct dml2_pmo_stage_optimizer **stages)
{
	int count = 0;

	DML_LOG_COMP_IF_ENTER();
	if (pmo->options->force_optional_ppt_temp_read_admissibility)
		stages[count++] = &pmo->stage_optimizers[dml2_pmo_stage_index_fclk_ppt_temp_read_pstate];
	if (pmo->options->force_optional_mcache_support)
		stages[count++] = &pmo->stage_optimizers[dml2_pmo_stage_index_mcache];
	if (pmo->options->force_optional_uclk_pstate_support)
		stages[count++] = &pmo->stage_optimizers[dml2_pmo_stage_index_uclk_pstate];
	stages[count++] = &pmo->stage_optimizers[dml2_pmo_stage_index_qos];
	stages[count++] = &pmo->stage_optimizers[dml2_pmo_stage_index_vmin];
	stages[count++] = &pmo->stage_optimizers[dml2_pmo_stage_index_stutter];
	stages[count++] = &pmo->stage_optimizers[dml2_pmo_stage_index_vmin_dcfclk];
	DML_LOG_DEBUG("%s exit with %d\n", __func__, count);
	DML_LOG_COMP_IF_EXIT();

	return count;
}

int dml2_pmo_dcn6b_get_ordered_mandatory_stage_optimizers(struct dml2_pmo_instance *pmo,
	struct dml2_pmo_stage_optimizer **stages)
{
	int count = 0;

	DML_LOG_COMP_IF_ENTER();
	if (!pmo->options->force_optional_ppt_temp_read_admissibility)
		stages[count++] = &pmo->stage_optimizers[dml2_pmo_stage_index_fclk_ppt_temp_read_pstate];
	if (!pmo->options->force_optional_mcache_support)
		stages[count++] = &pmo->stage_optimizers[dml2_pmo_stage_index_mcache];
	DML_LOG_DEBUG("%s exit with %d\n", __func__, count);
	DML_LOG_COMP_IF_EXIT();

	return count;
}

int dml2_pmo_dcn6b_get_ordered_optional_stages_optimizers(struct dml2_pmo_instance *pmo,
	struct dml2_pmo_stage_optimizer **stages)
{
	int count = 0;

	DML_LOG_COMP_IF_ENTER();
	if (pmo->options->force_optional_ppt_temp_read_admissibility)
		stages[count++] = &pmo->stage_optimizers[dml2_pmo_stage_index_fclk_ppt_temp_read_pstate];
	if (pmo->options->force_optional_mcache_support)
		stages[count++] = &pmo->stage_optimizers[dml2_pmo_stage_index_mcache];
	stages[count++] = &pmo->stage_optimizers[dml2_pmo_stage_index_uclk_pstate];
	stages[count++] = &pmo->stage_optimizers[dml2_pmo_stage_index_qos];
	stages[count++] = &pmo->stage_optimizers[dml2_pmo_stage_index_vmin];
	stages[count++] = &pmo->stage_optimizers[dml2_pmo_stage_index_stutter];
	stages[count++] = &pmo->stage_optimizers[dml2_pmo_stage_index_vmin_dcfclk];
	DML_LOG_DEBUG("%s exit with %d\n", __func__, count);
	DML_LOG_COMP_IF_EXIT();

	return count;
}


static enum dml2_uclk_pstate_change_strategy pstate_method_to_uclk_pstate_strategy_override(const enum dml2_pstate_method method)
{
	enum dml2_uclk_pstate_change_strategy override_strategy = dml2_uclk_pstate_change_strategy_auto;

	switch (method) {
	case dml2_pstate_method_vactive:
	case dml2_pstate_method_fw_vactive_drr:
		override_strategy = dml2_uclk_pstate_change_strategy_force_vactive;
		break;
	case dml2_pstate_method_vblank:
	case dml2_pstate_method_fw_vblank_drr:
		override_strategy = dml2_uclk_pstate_change_strategy_force_vblank;
		break;
	case dml2_pstate_method_fw_drr:
		override_strategy = dml2_uclk_pstate_change_strategy_force_drr;
		break;
	case dml2_pstate_method_alternate:
		override_strategy = dml2_uclk_pstate_change_strategy_force_alternate;
		break;
	case dml2_pstate_method_fw_svp:
	case dml2_pstate_method_fw_svp_drr:
	case dml2_pstate_method_reserved_hw:
	case dml2_pstate_method_reserved_fw:
	case dml2_pstate_method_reserved_fw_drr_clamped:
	case dml2_pstate_method_reserved_fw_drr_var:
	case dml2_pstate_method_count:
	case dml2_pstate_method_na:
	default:
		override_strategy = dml2_uclk_pstate_change_strategy_auto;
	}

	return override_strategy;
}

static void dml2_pmo_dcn6_apply_optimization_to_solution(struct dml2_pmo_instance *pmo,
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
		solution->dispcfg.plane_descriptors[i].overrides.uclk_pstate_change_strategy
			= pstate_method_to_uclk_pstate_strategy_override(optimization->config.uclk_pstate_switch_modes[i]);
		DML_LOG_VERBOSE("solution->uclk_pstate_params.pstate_switch_modes[%d] = %d\n",
				i, solution->uclk_pstate_params.pstate_switch_modes[i]);
	}

	/* VActive P-State DET fill time  */
	for (i = 0; i < solution->dispcfg.num_planes; i++) {
		memcpy(solution->dispcfg.plane_descriptors[i].overrides.max_vactive_det_fill_delay_us,
				optimization->config.max_vactive_det_fill_delay_us[i],
				sizeof(optimization->config.max_vactive_det_fill_delay_us[i]));
		DML_LOG_VERBOSE("solution->dispcfg.plane_descriptors[%d].overrides.max_vactive_det_fill_delay_us[uclk] = %d\n",
				i, solution->dispcfg.plane_descriptors[i].overrides.max_vactive_det_fill_delay_us[dml2_pstate_type_uclk]);
		DML_LOG_VERBOSE("solution->dispcfg.plane_descriptors[%d].overrides.max_vactive_det_fill_delay_us[fclk] = %d\n",
			i, solution->dispcfg.plane_descriptors[i].overrides.max_vactive_det_fill_delay_us[dml2_pstate_type_fclk]);
		DML_LOG_VERBOSE("solution->dispcfg.plane_descriptors[%d].overrides.max_vactive_det_fill_delay_us[ppt] = %d\n",
			i, solution->dispcfg.plane_descriptors[i].overrides.max_vactive_det_fill_delay_us[dml2_pstate_type_ppt]);
		DML_LOG_VERBOSE("solution->dispcfg.plane_descriptors[%d].overrides.max_vactive_det_fill_delay_us[temp] = %d\n",
			i, solution->dispcfg.plane_descriptors[i].overrides.max_vactive_det_fill_delay_us[dml2_pstate_type_temp_read]);
	}

	/* FAMS2 related */
	solution->uclk_pstate_params.fams2_required = optimization->config.fams2_required;
	solution->uclk_pstate_params.legacy_pstate_info_for_dmu = optimization->config.legacy_pstate_info_for_dmu;
	DML_LOG_VERBOSE("solution->uclk_pstate_params.fams2_required = %s\n", solution->uclk_pstate_params.fams2_required ?
			"true" : "false");
	memcpy(&solution->uclk_pstate_params.stream_pstate_meta,
			&optimization->config.stream_pstate_meta,
			sizeof(struct dml2_pstate_meta) * DML2_MAX_PLANES);

	/* fclk pstate */
	solution->fclk_pstate_support = optimization->config.fclk_pstate_support;
	DML_LOG_VERBOSE("solution->fclk_pstate_support = %s\n", solution->fclk_pstate_support ?
			"true" : "false");

	/* ppt and temp read pstate */
	solution->ppt_temp_read_support = optimization->config.ppt_temp_read_support;
	DML_LOG_VERBOSE("solution->ppt_temp_read_support = %s\n", solution->ppt_temp_read_support ?
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

void dml2_pmo_dcn6_convert_worksheet_to_solution(struct dml2_pmo_instance *pmo,
		const struct dml2_optimization_worksheet *worksheet,
		struct dml2_display_solution *solution)
{
	DML_LOG_COMP_IF_ENTER();

	memset(solution, 0, sizeof(struct dml2_display_solution));
	solution->orig_dispcfg = worksheet->orig_dispcfg;
	memcpy(&solution->dispcfg, worksheet->orig_dispcfg, sizeof(solution->dispcfg));
	memcpy(solution->timing_group_ids, worksheet->timing_group_ids, sizeof(solution->timing_group_ids));
	solution->timing_group_count = worksheet->timing_group_count;
	memcpy(&solution->validation_result, &worksheet->validation_result, sizeof(struct dml2_validation_result));
	dml2_pmo_dcn6_apply_optimization_to_solution(pmo, &worksheet->cur, solution);
	DML_LOG_COMP_IF_EXIT();
}

bool dml2_pmo_dcn6a_initialize(struct dml2_pmo_initialize_in_out *in_out)
{
	struct dml2_pmo_instance *pmo = in_out->instance;

	DML_LOG_COMP_IF_ENTER();
	pmo->ip_caps = in_out->ip_caps;
	pmo->options = in_out->options;
	pmo->utm_soc_bb = in_out->utm_soc_bb;
	pmo->mpc_combine_limit = 2;
	pmo->odm_combine_limit = 4;
	pmo->fams_params.v2.drr.refresh_rate_limit_max = 1000;
	pmo->fams_params.v2.drr.refresh_rate_limit_min = 119;

	dml2_pmo_dcn6_assign_pstate_strategies(pmo);

	dml2_pmo_dcn6_stage_optimizer_mcache_create(pmo, &pmo->stage_optimizers[dml2_pmo_stage_index_mcache]);
	dml2_pmo_dcn6_stage_optimizer_uclk_pstate_create(pmo, &pmo->stage_optimizers[dml2_pmo_stage_index_uclk_pstate]);
	dml2_pmo_dcn5_stage_optimizer_qos_create(pmo, &pmo->stage_optimizers[dml2_pmo_stage_index_qos]);
	dml2_pmo_dcn5_stage_optimizer_vmin_create(pmo, &pmo->stage_optimizers[dml2_pmo_stage_index_vmin]);
	dml2_pmo_dcn5_stage_optimizer_stutter_create(pmo, &pmo->stage_optimizers[dml2_pmo_stage_index_stutter]);
	dml2_pmo_dcn6_stage_optimizer_vmin_dcfclk_create(pmo, &pmo->stage_optimizers[dml2_pmo_stage_index_vmin_dcfclk]);
	dml2_pmo_dcn6_stage_optimizer_fclk_ppt_temp_read_pstate_create(pmo, &pmo->stage_optimizers[dml2_pmo_stage_index_fclk_ppt_temp_read_pstate]);
	DML_LOG_DEBUG("%s exit with true\n", __func__);
	DML_LOG_COMP_IF_EXIT();

	return true;
}

bool dml2_pmo_dcn6b_initialize(struct dml2_pmo_initialize_in_out *in_out)
{
	struct dml2_pmo_instance *pmo = in_out->instance;

	DML_LOG_COMP_IF_ENTER();
	pmo->ip_caps = in_out->ip_caps;
	pmo->options = in_out->options;
	pmo->options->disable_alternate_memory_training = true;
	pmo->utm_soc_bb = in_out->utm_soc_bb;
	pmo->mpc_combine_limit = 2;
	pmo->odm_combine_limit = 4;
	pmo->fams_params.v2.drr.refresh_rate_limit_max = 1000;
	pmo->fams_params.v2.drr.refresh_rate_limit_min = 119;

	dml2_pmo_dcn6_assign_pstate_strategies(pmo);

	dml2_pmo_dcn6_stage_optimizer_mcache_create(pmo, &pmo->stage_optimizers[dml2_pmo_stage_index_mcache]);
	dml2_pmo_dcn6_stage_optimizer_uclk_pstate_create(pmo, &pmo->stage_optimizers[dml2_pmo_stage_index_uclk_pstate]);
	dml2_pmo_dcn5_stage_optimizer_qos_create(pmo, &pmo->stage_optimizers[dml2_pmo_stage_index_qos]);
	dml2_pmo_dcn5_stage_optimizer_vmin_create(pmo, &pmo->stage_optimizers[dml2_pmo_stage_index_vmin]);
	dml2_pmo_dcn5_stage_optimizer_stutter_create(pmo, &pmo->stage_optimizers[dml2_pmo_stage_index_stutter]);
	dml2_pmo_dcn6_stage_optimizer_vmin_dcfclk_create(pmo, &pmo->stage_optimizers[dml2_pmo_stage_index_vmin_dcfclk]);
	dml2_pmo_dcn6_stage_optimizer_fclk_ppt_temp_read_pstate_create(pmo, &pmo->stage_optimizers[dml2_pmo_stage_index_fclk_ppt_temp_read_pstate]);
	DML_LOG_DEBUG("%s exit with true\n", __func__);
	DML_LOG_COMP_IF_EXIT();

	return true;
}
