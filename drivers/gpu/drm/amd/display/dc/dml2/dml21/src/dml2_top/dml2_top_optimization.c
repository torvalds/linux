// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.


#include "dml2_top_optimization.h"
#include "dml2_internal_shared_types.h"
#include "dml_top_mcache.h"

static void copy_display_configuration_with_meta(struct display_configuation_with_meta *dst, const struct display_configuation_with_meta *src)
{
	memcpy(dst, src, sizeof(struct display_configuation_with_meta));
}

bool dml2_top_optimization_init_function_min_clk_for_latency(const struct optimization_init_function_params *params)
{
	struct dml2_optimization_stage1_state *state = &params->display_config->stage1;

	state->performed = true;

	return true;
}

bool dml2_top_optimization_test_function_min_clk_for_latency(const struct optimization_test_function_params *params)
{
	struct dml2_optimization_stage1_state *state = &params->display_config->stage1;

	return state->min_clk_index_for_latency == 0;
}

bool dml2_top_optimization_optimize_function_min_clk_for_latency(const struct optimization_optimize_function_params *params)
{
	bool result = false;

	if (params->display_config->stage1.min_clk_index_for_latency > 0) {
		copy_display_configuration_with_meta(params->optimized_display_config, params->display_config);
		params->optimized_display_config->stage1.min_clk_index_for_latency--;
		result = true;
	}

	return result;
}

bool dml2_top_optimization_test_function_mcache(const struct optimization_test_function_params *params)
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

bool dml2_top_optimization_optimize_function_mcache(const struct optimization_optimize_function_params *params)
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

bool dml2_top_optimization_init_function_vmin(const struct optimization_init_function_params *params)
{
	struct dml2_optimization_init_function_locals *l = params->locals;

	l->vmin.init_params.instance = &params->dml->pmo_instance;
	l->vmin.init_params.base_display_config = params->display_config;
	return params->dml->pmo_instance.init_for_vmin(&l->vmin.init_params);
}

bool dml2_top_optimization_test_function_vmin(const struct optimization_test_function_params *params)
{
	struct dml2_optimization_test_function_locals *l = params->locals;

	l->test_vmin.pmo_test_vmin_params.instance = &params->dml->pmo_instance;
	l->test_vmin.pmo_test_vmin_params.display_config = params->display_config;
	l->test_vmin.pmo_test_vmin_params.vmin_limits = &params->dml->soc_bbox.vmin_limit;
	return params->dml->pmo_instance.test_for_vmin(&l->test_vmin.pmo_test_vmin_params);
}

bool dml2_top_optimization_optimize_function_vmin(const struct optimization_optimize_function_params *params)
{
	struct dml2_optimization_optimize_function_locals *l = params->locals;

	if (params->last_candidate_supported == false)
		return false;

	l->optimize_vmin.pmo_optimize_vmin_params.instance = &params->dml->pmo_instance;
	l->optimize_vmin.pmo_optimize_vmin_params.base_display_config = params->display_config;
	l->optimize_vmin.pmo_optimize_vmin_params.optimized_display_config = params->optimized_display_config;
	return params->dml->pmo_instance.optimize_for_vmin(&l->optimize_vmin.pmo_optimize_vmin_params);
}

bool dml2_top_optimization_perform_optimization_phase(struct dml2_optimization_phase_locals *l, const struct optimization_phase_params *params)
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

bool dml2_top_optimization_perform_optimization_phase_1(struct dml2_optimization_phase_locals *l, const struct optimization_phase_params *params)
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
	cur_state = 0;

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

bool dml2_top_optimization_init_function_uclk_pstate(const struct optimization_init_function_params *params)
{
	struct dml2_optimization_init_function_locals *l = params->locals;

	l->uclk_pstate.init_params.instance = &params->dml->pmo_instance;
	l->uclk_pstate.init_params.base_display_config = params->display_config;

	return params->dml->pmo_instance.init_for_uclk_pstate(&l->uclk_pstate.init_params);
}

bool dml2_top_optimization_test_function_uclk_pstate(const struct optimization_test_function_params *params)
{
	struct dml2_optimization_test_function_locals *l = params->locals;

	l->uclk_pstate.test_params.instance = &params->dml->pmo_instance;
	l->uclk_pstate.test_params.base_display_config = params->display_config;

	return params->dml->pmo_instance.test_for_uclk_pstate(&l->uclk_pstate.test_params);
}

bool dml2_top_optimization_optimize_function_uclk_pstate(const struct optimization_optimize_function_params *params)
{
	struct dml2_optimization_optimize_function_locals *l = params->locals;

	l->uclk_pstate.optimize_params.instance = &params->dml->pmo_instance;
	l->uclk_pstate.optimize_params.base_display_config = params->display_config;
	l->uclk_pstate.optimize_params.optimized_display_config = params->optimized_display_config;
	l->uclk_pstate.optimize_params.last_candidate_failed = !params->last_candidate_supported;

	return params->dml->pmo_instance.optimize_for_uclk_pstate(&l->uclk_pstate.optimize_params);
}

bool dml2_top_optimization_init_function_stutter(const struct optimization_init_function_params *params)
{
	struct dml2_optimization_init_function_locals *l = params->locals;

	l->uclk_pstate.init_params.instance = &params->dml->pmo_instance;
	l->uclk_pstate.init_params.base_display_config = params->display_config;

	return params->dml->pmo_instance.init_for_stutter(&l->stutter.stutter_params);
}

bool dml2_top_optimization_test_function_stutter(const struct optimization_test_function_params *params)
{
	struct dml2_optimization_test_function_locals *l = params->locals;

	l->stutter.stutter_params.instance = &params->dml->pmo_instance;
	l->stutter.stutter_params.base_display_config = params->display_config;
	return params->dml->pmo_instance.test_for_stutter(&l->stutter.stutter_params);
}

bool dml2_top_optimization_optimize_function_stutter(const struct optimization_optimize_function_params *params)
{
	struct dml2_optimization_optimize_function_locals *l = params->locals;

	l->stutter.stutter_params.instance = &params->dml->pmo_instance;
	l->stutter.stutter_params.base_display_config = params->display_config;
	l->stutter.stutter_params.optimized_display_config = params->optimized_display_config;
	l->stutter.stutter_params.last_candidate_failed = !params->last_candidate_supported;
	return params->dml->pmo_instance.optimize_for_stutter(&l->stutter.stutter_params);
}
