// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#include "dml2_pmo_factory.h"
#include "dml2_pmo_dcn4_fams2.h"
#include "dml2_pmo_dcn3.h"
#include "dml2_pmo_dcn42.h"
#include "dml2_external_lib_deps.h"
#include "dml2_pmo_dcn5.h"
#include "dml2_pmo_dcn6.h"

static bool dummy_init_for_stutter(struct dml2_pmo_init_for_stutter_in_out *in_out)
{
	(void)in_out;
	return false;
}

static bool dummy_test_for_stutter(struct dml2_pmo_test_for_stutter_in_out *in_out)
{
	(void)in_out;
	return true;
}

static bool dummy_optimize_for_stutter(struct dml2_pmo_optimize_for_stutter_in_out *in_out)
{
	(void)in_out;
	return false;
}

bool dml2_pmo_create(enum dml2_project_id project_id, struct dml2_pmo_instance *out)
{
	bool result = false;

	if (!out)
		return false;

	memset(out, 0, sizeof(struct dml2_pmo_instance));

	switch (project_id) {
	case dml2_project_dcn4x_stage1:
		out->initialize = pmo_dcn4_fams2_initialize;
		out->optimize_dcc_mcache = pmo_dcn4_fams2_optimize_dcc_mcache;
		result = true;
		break;
	case dml2_project_dcn4x_stage2:
		out->initialize = pmo_dcn3_initialize;

		out->optimize_dcc_mcache = pmo_dcn3_optimize_dcc_mcache;

		out->init_for_vmin = pmo_dcn3_init_for_vmin;
		out->test_for_vmin = pmo_dcn3_test_for_vmin;
		out->optimize_for_vmin = pmo_dcn3_optimize_for_vmin;

		out->init_for_uclk_pstate = pmo_dcn3_init_for_pstate_support;
		out->test_for_uclk_pstate = pmo_dcn3_test_for_pstate_support;
		out->optimize_for_uclk_pstate = pmo_dcn3_optimize_for_pstate_support;

		out->init_for_stutter = dummy_init_for_stutter;
		out->test_for_stutter = dummy_test_for_stutter;
		out->optimize_for_stutter = dummy_optimize_for_stutter;

		result = true;
		break;
	case dml2_project_dcn42:
		out->initialize = pmo_dcn42_initialize;

		out->init_for_vmin = pmo_dcn4_fams2_init_for_vmin;
		out->test_for_vmin = pmo_dcn4_fams2_test_for_vmin;
		out->optimize_for_vmin = pmo_dcn4_fams2_optimize_for_vmin;

		out->init_for_uclk_pstate = pmo_dcn42_init_for_pstate_support;
		out->test_for_uclk_pstate = pmo_dcn42_test_for_pstate_support;
		out->optimize_for_uclk_pstate = pmo_dcn42_fams2_optimize_for_pstate_support;

		out->init_for_stutter = pmo_dcn4_fams2_init_for_stutter;
		out->test_for_stutter = pmo_dcn4_fams2_test_for_stutter;
		out->optimize_for_stutter = pmo_dcn4_fams2_optimize_for_stutter;
		result = true;
		break;
	case dml2_project_dcn4x_stage2_auto_drr_svp:
		out->initialize = pmo_dcn4_fams2_initialize;

		out->optimize_dcc_mcache = pmo_dcn4_fams2_optimize_dcc_mcache;

		out->init_for_vmin = pmo_dcn4_fams2_init_for_vmin;
		out->test_for_vmin = pmo_dcn4_fams2_test_for_vmin;
		out->optimize_for_vmin = pmo_dcn4_fams2_optimize_for_vmin;

		out->init_for_uclk_pstate = pmo_dcn4_fams2_init_for_pstate_support;
		out->test_for_uclk_pstate = pmo_dcn4_fams2_test_for_pstate_support;
		out->optimize_for_uclk_pstate = pmo_dcn4_fams2_optimize_for_pstate_support;

		out->init_for_stutter = pmo_dcn4_fams2_init_for_stutter;
		out->test_for_stutter = pmo_dcn4_fams2_test_for_stutter;
		out->optimize_for_stutter = pmo_dcn4_fams2_optimize_for_stutter;
		result = true;
		break;
	case dml2_project_dcn5x_utm:
		out->initialize = dml2_pmo_dcn5_initialize;
		out->get_ordered_mandatory_stage_optimizers = dml2_pmo_dcn5_get_ordered_mandatory_stage_optimizers;
		out->get_ordered_optional_stage_optimizers = dml2_pmo_dcn5_get_ordered_optional_stages_optimizers;
		out->initialize_worksheet = dml2_pmo_dcn5_initialize_worksheet;
		out->optional_sanity_check = dml2_pmo_dcn5_sanity_check;
		out->convert_worksheet_to_solution = dml2_pmo_dcn5_convert_worksheet_to_solution;
		out->clear_pre_validation_states = dml2_pmo_dcn5_clear_pre_validation_states;
		result = true;
		break;
	case dml2_project_dcn6x_soc_var_a:
		out->initialize = dml2_pmo_dcn6a_initialize;
		out->get_ordered_mandatory_stage_optimizers = dml2_pmo_dcn6a_get_ordered_mandatory_stage_optimizers;
		out->get_ordered_optional_stage_optimizers = dml2_pmo_dcn6a_get_ordered_optional_stages_optimizers;
		out->initialize_worksheet = dml2_pmo_dcn5_initialize_worksheet;
		out->optional_sanity_check = dml2_pmo_dcn5_sanity_check;
		out->convert_worksheet_to_solution = dml2_pmo_dcn6_convert_worksheet_to_solution;
		out->clear_pre_validation_states = dml2_pmo_dcn5_clear_pre_validation_states;
		result = true;
		break;
	case dml2_project_dcn6x_soc_var_b:
		out->initialize = dml2_pmo_dcn6b_initialize;
		out->get_ordered_mandatory_stage_optimizers = dml2_pmo_dcn6b_get_ordered_mandatory_stage_optimizers;
		out->get_ordered_optional_stage_optimizers = dml2_pmo_dcn6b_get_ordered_optional_stages_optimizers;
		out->initialize_worksheet = dml2_pmo_dcn5_initialize_worksheet;
		out->optional_sanity_check = dml2_pmo_dcn5_sanity_check;
		out->convert_worksheet_to_solution = dml2_pmo_dcn6_convert_worksheet_to_solution;
		out->clear_pre_validation_states = dml2_pmo_dcn5_clear_pre_validation_states;
		result = true;
		break;
	case dml2_project_dcn4x_utm:
	case dml2_project_dcn5x:
	case dml2_project_invalid:
	default:
		break;
	}

	return result;
}
