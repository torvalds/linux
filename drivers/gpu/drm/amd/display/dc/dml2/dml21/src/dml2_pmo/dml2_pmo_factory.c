// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.


#include "dml2_pmo_factory.h"
#include "dml2_pmo_dcn4_fams2.h"
#include "dml2_pmo_dcn4.h"
#include "dml2_pmo_dcn3.h"
#include "dml2_external_lib_deps.h"

static bool dummy_init_for_stutter(struct dml2_pmo_init_for_stutter_in_out *in_out)
{
	return false;
}

static bool dummy_test_for_stutter(struct dml2_pmo_test_for_stutter_in_out *in_out)
{
	return true;
}

static bool dummy_optimize_for_stutter(struct dml2_pmo_optimize_for_stutter_in_out *in_out)
{
	return false;
}

bool dml2_pmo_create(enum dml2_project_id project_id, struct dml2_pmo_instance *out)
{
	bool result = false;

	if (out == 0)
		return false;

	memset(out, 0, sizeof(struct dml2_pmo_instance));

	switch (project_id) {
	case dml2_project_dcn4x_stage1:
		out->initialize = pmo_dcn4_initialize;
		out->optimize_dcc_mcache = pmo_dcn4_optimize_dcc_mcache;
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
	case dml2_project_invalid:
	default:
		break;
	}

	return result;
}
