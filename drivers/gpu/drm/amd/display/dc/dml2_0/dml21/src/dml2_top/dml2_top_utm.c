// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#include "dml2_top_utm.h"
#include "dml2_top_soc15.h"
#include "dml2_mcg_factory.h"
#include "dml2_dpmm_factory.h"
#include "dml2_core_factory.h"
#include "dml2_pmo_factory.h"
#include "dml2_utm_soc_bb_factory.h"
#include "dml2_cga_factory.h"
#include "dml2_debug.h"

static void dml2_top_backup_worksheet(struct dml2_instance *dml, const struct dml2_optimization_worksheet *worksheet)
{
	memcpy(&dml->scratch.worksheet_backup, worksheet, sizeof(struct dml2_optimization_worksheet));
}

static void dml2_top_restore_worksheet(struct dml2_instance *dml, struct dml2_optimization_worksheet *worksheet)
{
	memcpy(worksheet, &dml->scratch.worksheet_backup, sizeof(struct dml2_optimization_worksheet));
}

static enum dml2_status dml2_top_validate_worksheet(struct dml2_instance *dml,
		struct dml2_optimization_worksheet *worksheet)
{
	struct dml2_pmo_instance *pmo = &dml->pmo_instance;
	struct dml2_core_instance *core = &dml->core_instance;
	struct dml2_display_solution *solution = &dml->scratch.solution;
	enum dml2_status status;

	/*
	 * validate_solution is time consuming. So this sanity check is a performance optimization so we can rule out
	 * some common configuration problems during PMO optimization without running the whole core sequence. It is a
	 * subset of validation_solution interface. For example, it may check if pipe usage exceeds total pipe
	 * availability. Ideally validate_solution should have done a better job of optimizing its performance so this
	 * PMO interface is not necessary.
	 */
	status = pmo->optional_sanity_check(pmo, worksheet);

	if (status == DML2_STATUS_OK) {
		pmo->convert_worksheet_to_solution(pmo, worksheet, solution);
		status = core->validate_solution(core, solution, &worksheet->validation_result);
		pmo->clear_pre_validation_states(pmo, worksheet);
	} else
		worksheet->validation_result.is_mode_support_valid = false;

	return status;
}

static enum dml2_status dml2_top_perform_stage_optimization(struct dml2_instance *dml,
		struct dml2_pmo_stage_optimizer *optimizer,
		struct dml2_optimization_worksheet *worksheet)
{
	static const unsigned int MAX_OPTIMIZATION_ITERATIONS = 20;
	unsigned int iteration = 0;
	enum dml2_status cur_validate_status = DML2_STATUS_OK;
	enum dml2_status cur_optimize_status = DML2_STATUS_UNKNOWN;

	/*
	 * true if the function finds at least one validated worksheet passing permissibility test from current stage
	 * optimizer.
	 */
	bool is_permissible_found = false;

	dml2_top_backup_worksheet(dml, worksheet);
	/*
	 * init interface builds the initial states associated with the current stage optimizer into the worksheet. It
	 * is for state initialization only. It should not apply new optimization or cause changes to current validation
	 * result. It is safe to assume that the worksheet passed in or exited from this interface is always validated.
	 */
	optimizer->init(optimizer, worksheet);
	/*
	 * optimize_next interface controls current optimization's stop conditions. When the interface returns false, it
	 * means the stage optimizer no longer needs to attempt further optimization. The current worksheet should be
	 * left unmodified. When the interface returns true, it means the stage optimizer applied new optimization to
	 * the worksheet. DML top will need to validate and test permissibility again. The worksheet passed in is based
	 * off the optimization decision from last attempt. It may or may not be validated or permissible. It is upto
	 * DML top to keep track of the last valid permissible worksheet. This interface is also responsible to clear
	 * corresponding valid bits in worksheet's validation result based on what optimization it gets applied. When
	 * the valid bits are cleared, it will be revalidated by top. Otherwise, DML top will assume it is safe to skip
	 * certain re-validations based on the remaining valid bits. Stage optimizers should clear only the necessary
	 * valid bits based on the optimization applied to speed up the process.
	 */
	while (optimizer->optimize_next(optimizer, worksheet)) {
		if (++iteration >= MAX_OPTIMIZATION_ITERATIONS) {
			cur_optimize_status = DML2_STATUS_OPTIMIZE_FAIL_EXCEED_MAX_ITERATION;
			break;
		}
		cur_optimize_status = DML2_STATUS_UNKNOWN;
		cur_validate_status = dml2_top_validate_worksheet(dml, worksheet);
		if (cur_validate_status == DML2_STATUS_OK)
			/*
			 * test_permissibility interface should only check against current optimizer policy specific
			 * minimum requirements. Test permissibility result is orthogonal to validation result. It is
			 * safe to assume the worksheet constant passed in is always validated. The interface checks if
			 * the validated result fulfills the minimum requirements additionally imposed by current stage
			 * optimizer in order to consider the current optimization as a potential candidate. Stage
			 * optimizer may still attempt further optimization even if the current one is permissible.
			 */
			cur_optimize_status = optimizer->test_permissibility(optimizer, worksheet);

		if (cur_validate_status == DML2_STATUS_OK && cur_optimize_status == DML2_STATUS_OK) {
			is_permissible_found = true;
			dml2_top_backup_worksheet(dml, worksheet);
		}
	}

	/*
	 * When optimize next returns false in the first iteration, test permissibility interface is never called. In
	 * this case, we need to call it and check if current worksheet is already permissible.
	 */
	if (cur_validate_status == DML2_STATUS_OK && cur_optimize_status == DML2_STATUS_UNKNOWN) {
		cur_optimize_status = optimizer->test_permissibility(optimizer, worksheet);
		if (cur_optimize_status == DML2_STATUS_OK)
			is_permissible_found = true;
	}

	if (cur_validate_status != DML2_STATUS_OK || cur_optimize_status != DML2_STATUS_OK)
		dml2_top_restore_worksheet(dml, worksheet);

	DML_ASSERT_MSG(worksheet->validation_result.is_mode_support_valid
			&& worksheet->validation_result.is_mcache_allocation_valid
			&& worksheet->validation_result.is_prefetch_valid,
			"worksheet must be valid on exit independent from optmization resul!\n");
//	DML_ASSERT_MSG(iteration <= MAX_OPTIMIZATION_ITERATIONS,
//			"exceeds max optimization iterations!\n"
//			"\t is_permissible_found = %s\n"
//			"\t cur_validate_status = %s\n"
//			"\t cur_optimize_status = %s\n",
//			is_permissible_found ? "true" : "false",
//			dml2_status_str(cur_validate_status),
//			dml2_status_str(cur_optimize_status));

	return is_permissible_found ? DML2_STATUS_OK :
			(cur_validate_status != DML2_STATUS_OK) ? cur_validate_status : cur_optimize_status;
}

static enum dml2_status dml2_top_build_and_validate_unoptimized_worksheet(
		struct dml2_instance *dml,
		const struct dml2_display_cfg *orig_dispcfg,
		struct dml2_optimization_worksheet *worksheet)
{
	enum dml2_status status = DML2_STATUS_OK;
	struct dml2_pmo_instance *pmo = &dml->pmo_instance;
	struct dml2_pmo_stage_optimizer *optimizers[dml2_pmo_stage_index_max];
	int count;
	int i;

	if (status == DML2_STATUS_OK) {
		pmo->initialize_worksheet(pmo, orig_dispcfg, worksheet);
		status = dml2_top_validate_worksheet(dml, worksheet);
	}

	if (status == DML2_STATUS_OK) {
		count = pmo->get_ordered_mandatory_stage_optimizers(pmo, optimizers);
		for (i = 0; i < count; i++) {
			status = dml2_top_perform_stage_optimization(dml, optimizers[i], worksheet);
			if (status != DML2_STATUS_OK)
				break;
		}
	}

	return status;
}

static void dml2_top_optimize_worksheet(struct dml2_instance *dml,
		struct dml2_optimization_worksheet *worksheet)
{
	struct dml2_pmo_instance *pmo = &dml->pmo_instance;
	struct dml2_pmo_stage_optimizer *ordered_optional_stage_optimizers[dml2_pmo_stage_index_max];
	int count;
	int i;

	DML_ASSERT(worksheet->validation_result.is_mode_support_valid);
	count = pmo->get_ordered_optional_stage_optimizers(pmo, ordered_optional_stage_optimizers);
	for (i = 0; i < count; i++)
		dml2_top_perform_stage_optimization(dml, ordered_optional_stage_optimizers[i], worksheet);
}

static enum dml2_status dml2_top_map_minimum_clock_state(struct dml2_instance *dml,
		struct dml2_display_solution *solution,
		struct dml2_display_cfg_programming *programming)
{
	bool result;
	struct dml2_dpmm_map_mode_to_soc_dpm_params_in_out *params =
			&dml->scratch.build_mode_programming_locals.dppm_map_mode_params;
	struct dml2_dpmm_instance *dpmm = &dml->dpmm_instance;

	if (!dpmm->map_mode_to_soc_dpm)
		return DML2_STATUS_OK;

	params->utm_soc_bb = &dml->utm_soc_bb;
	params->ip = &dml->core_instance.clean_me_up.mode_lib.ip;
	params->solution = solution;
	params->programming = programming;

	result = dpmm->map_mode_to_soc_dpm(params);

	return result ? DML2_STATUS_OK : DML2_STATUS_POPULATE_FAIL_MIN_CLOCK_STATE;
}

static enum dml2_status dml2_top_populate_mode_programming(struct dml2_instance *dml,
		const struct dml2_display_solution *solution,
		struct dml2_display_cfg_programming *programming)
{
	enum dml2_status status;
	struct dml2_core_instance *core = &dml->core_instance;

	status = core->populate_programming(core, solution, programming);

	return status;
}

static void dml2_top_populate_informative(struct dml2_instance *dml,
		enum dml2_status status,
		struct dml2_display_cfg_programming *programming)
{
	struct dml2_core_populate_informative_in_out *params = &dml->scratch.build_mode_programming_locals.informative_params;
	struct dml2_core_instance *core = &dml->core_instance;

	params->instance = core;
	params->programming = programming;
	params->mode_is_supported = (status == DML2_STATUS_OK);
	params->instance->scratch.mode_programming_locals.mode_programming_ex_params.min_clk_index =
			dml->scratch.solution.sop_constraint.dcn5.min_sop_index;
	dml->core_instance.populate_informative(params);

	if (status == DML2_STATUS_POPULATE_FAIL_PROGRAMMING ||
		status == DML2_STATUS_VALIDATE_FAIL_MODE_SUPPORT_PREFETCH ||
		status == DML2_STATUS_VALIDATE_FAIL_MODE_SUPPORT_PREFETCH_URGENT)
		programming->informative.failed_mode_programming_prefetch = true;
	else if (status == DML2_STATUS_POPULATE_FAIL_PROGRAMMING_DCFCLK)
		programming->informative.failed_mode_programming_dcfclk = true;
	else if (status == DML2_STATUS_POPULATE_FAIL_PROGRAMMING_FLIP_BANDWIDTH ||
		status == DML2_STATUS_VALIDATE_FAIL_MODE_SUPPORT_QOS_BANDWIDTH)
		programming->informative.failed_mode_programming_flip = true;
	else if (status == DML2_STATUS_POPULATE_FAIL_MIN_CLOCK_STATE)
		programming->informative.failed_dpmm = true;
	else if (status == DML2_STATUS_VALIDATE_FAIL_MCACHE ||
			status == DML2_STATUS_OPTIMIZE_FAIL_MCACHE ||
			status == DML2_STATUS_VALIDATE_FAIL_PMO_SANITY_TOTAL_PIPE_USAGE)
		programming->informative.failed_mcache_validation = true;
	else if (status == DML2_STATUS_OPTIMIZE_FAIL_UCLK_PSTATE)
		programming->informative.failed_uclk_pstate = true;
	else if (status == DML2_STATUS_VALIDATE_FAIL_PREFETCH)
		programming->informative.failed_prefetch = true;
}

static enum dml2_status dml2_top_build_programming_for_worksheet(
		struct dml2_instance *dml, const struct dml2_optimization_worksheet *worksheet,
		struct dml2_display_cfg_programming *programming)
{
	enum dml2_status status = DML2_STATUS_OK;
	struct dml2_display_solution *solution = &dml->scratch.solution;
	struct dml2_pmo_instance *pmo = &dml->pmo_instance;

	memset(programming, 0, sizeof(struct dml2_display_cfg_programming));
	pmo->convert_worksheet_to_solution(pmo, worksheet, solution);

	if (status == DML2_STATUS_OK)
		status = dml2_top_map_minimum_clock_state(dml, solution, programming);

	if (status == DML2_STATUS_OK)
		status = dml2_top_populate_mode_programming(dml, solution, programming);

	return status;
}

static bool dml2_top_utm_check_mode_supported(struct dml2_check_mode_supported_in_out *in_out)
{
	enum dml2_status status = DML2_STATUS_OK;
	struct dml2_instance *dml = in_out->dml2_instance;

	DML_LOG_TOP_IF_ENTER();
	/*
	 * Design Policy Note:
	 * To keep the consistency of check mode support and build mode programming interfaces, the returned status
	 * should be both based on the unified function below. Check mode support should not make coding assumptions in
	 * an effort to optimize check mode support performance. If A comes out as the unoptimized worksheet in check
	 * mode support interface, build mode programming must regenerate A with the same logic and then execute extra
	 * (i.e A->B->C->D). If check mode support does A->C', while build mode programming does A->B->C->D, then the
	 * design is considered as compromised. We are making the assumption that C' is always equal to C. This
	 * assumption can not be universally guaranteed for all DCNs by current design.
	 */
	status = dml2_top_build_and_validate_unoptimized_worksheet(
			dml, in_out->display_config, &dml->scratch.worksheet);
	in_out->is_supported = (status == DML2_STATUS_OK);
	DML_LOG_INFO("%s exit with %s\n", __func__, dml2_status_str(status));
	DML_LOG_TOP_IF_EXIT();

	return true;
}

static bool dml2_top_utm_build_mode_programming(struct dml2_build_mode_programming_in_out *in_out)
{
	struct dml2_instance *dml = in_out->dml2_instance;
	struct dml2_optimization_worksheet *worksheet = &dml->scratch.worksheet;
	enum dml2_status status = DML2_STATUS_OK;

	DML_LOG_TOP_IF_ENTER();
	if (status == DML2_STATUS_OK)
		status = dml2_top_build_and_validate_unoptimized_worksheet(dml, in_out->display_config, worksheet);

	if (status == DML2_STATUS_OK) {
		dml2_top_optimize_worksheet(dml, worksheet);
		status = dml2_top_build_programming_for_worksheet(dml, worksheet, in_out->programming);
		if (status != DML2_STATUS_OK)
			DML_LOG_ERROR("build mode programming fails for a supported display config! (%s)\n",
					dml2_status_str(status));
	}

	dml2_top_populate_informative(dml, status, in_out->programming);
	if (status == DML2_STATUS_OK)
		DML_LOG_INFO("%s exit with %s\n", __func__, dml2_status_str(status));
	else
		DML_LOG_WARN("%s exit with %s\n", __func__, dml2_status_str(status));
	DML_LOG_TOP_IF_EXIT();

	return status == DML2_STATUS_OK;
}

static const struct dml2_top_funcs utm_funcs = {
	.check_mode_supported = dml2_top_utm_check_mode_supported,
	.build_mode_programming = dml2_top_utm_build_mode_programming,
	.build_mcache_programming = dml2_top_soc15_build_mcache_programming,
};


bool dml2_top_utm_initialize_instance(struct dml2_initialize_instance_in_out *in_out)
{
	struct dml2_instance *dml = in_out->dml2_instance;
	struct dml2_core_initialize_in_out core_init_params = { 0 };
	struct dml2_pmo_initialize_in_out pmo_init_params = { 0 };
	struct dml2_cga_initialize_in_out cga_init_params = { 0 };
	bool result = true;

	DML_LOG_TOP_IF_ENTER();
	memset(dml, 0, sizeof(struct dml2_instance));

	if (result) {
		memcpy(&dml->ip_caps, &in_out->ip_caps, sizeof(struct dml2_ip_capabilities));
		dml->project_id = in_out->options.project_id;
		dml->pmo_options = in_out->options.pmo_options;
		dml->funcs = utm_funcs;
	}

	if (result)
		result = dml2_dpmm_create(in_out->options.project_id, &dml->dpmm_instance);

	if (result)
		result = dml2_core_create(in_out->options.project_id, &dml->core_instance);

	if (result)
		result = dml2_pmo_create(in_out->options.project_id, &dml->pmo_instance);

	if (result)
		result = dml2_utm_soc_bb_create(in_out->options.project_id, &dml->utm_soc_bb,
				&in_out->soc_bb, in_out->overrides.explicit_qos_model);

	if (result)
		result = dml2_cga_create(in_out->options.project_id, &dml->clock_adjuster);

	if (result) {
		core_init_params.project_id = in_out->options.project_id;
		core_init_params.instance = &dml->core_instance;
		core_init_params.explicit_ip_bb = in_out->overrides.explicit_ip_bb;
		core_init_params.explicit_ip_bb_size = in_out->overrides.explicit_ip_bb_size;
		core_init_params.ip_caps = &dml->ip_caps;
		core_init_params.utm_soc_bb = &dml->utm_soc_bb;
		core_init_params.clock_adjuster = &dml->clock_adjuster;
		result = dml->core_instance.initialize(&core_init_params);
	}

	if (result) {
		pmo_init_params.instance = &dml->pmo_instance;
		pmo_init_params.ip_caps = &dml->ip_caps;
		pmo_init_params.utm_soc_bb = &dml->utm_soc_bb;
		pmo_init_params.options = &dml->pmo_options;
		dml->pmo_instance.initialize(&pmo_init_params);
	}

	if (result && dml->clock_adjuster.initialize) {
		cga_init_params.adjuster = &dml->clock_adjuster;
		cga_init_params.soc_bb = &in_out->soc_bb;
		cga_init_params.ip = &dml->core_instance.clean_me_up.mode_lib.ip;
		dml->clock_adjuster.initialize(&cga_init_params);
	}
	DML_LOG_DEBUG("%s exit with %s\n", __func__, result ? "true":"false");
	DML_LOG_TOP_IF_EXIT();

	return result;
}
