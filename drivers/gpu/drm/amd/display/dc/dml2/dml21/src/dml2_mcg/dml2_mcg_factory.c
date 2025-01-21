// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#include "dml2_mcg_factory.h"
#include "dml2_mcg_dcn4.h"
#include "dml2_external_lib_deps.h"

static bool dummy_build_min_clock_table(struct dml2_mcg_build_min_clock_table_params_in_out *in_out)
{
	return true;
}

bool dml2_mcg_create(enum dml2_project_id project_id, struct dml2_mcg_instance *out)
{
	bool result = false;

	if (!out)
		return false;

	memset(out, 0, sizeof(struct dml2_mcg_instance));

	switch (project_id) {
	case dml2_project_dcn4x_stage1:
		out->build_min_clock_table = &dummy_build_min_clock_table;
		result = true;
		break;
	case dml2_project_dcn4x_stage2:
	case dml2_project_dcn4x_stage2_auto_drr_svp:
		out->build_min_clock_table = &mcg_dcn4_build_min_clock_table;
		result = true;
		break;
	case dml2_project_invalid:
	default:
		break;
	}

	return result;
}
