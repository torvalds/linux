// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#include "dml2_internal_shared_types.h"
#include "dml_top.h"
#include "dml2_mcg_factory.h"
#include "dml2_core_factory.h"
#include "dml2_dpmm_factory.h"
#include "dml2_pmo_factory.h"
#include "dml_top_mcache.h"
#include "dml2_top_optimization.h"
#include "dml2_external_lib_deps.h"

unsigned int dml2_get_instance_size_bytes(void)
{
	return sizeof(struct dml2_instance);
}

bool dml2_initialize_instance(struct dml2_initialize_instance_in_out *in_out)
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

	return result;
}

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

bool dml2_check_mode_supported(struct dml2_check_mode_supported_in_out *in_out)
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

bool dml2_build_mode_programming(struct dml2_build_mode_programming_in_out *in_out)
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

bool dml2_build_mcache_programming(struct dml2_build_mcache_programming_in_out *in_out)
{
	return dml2_top_mcache_build_mcache_programming(in_out);
}

