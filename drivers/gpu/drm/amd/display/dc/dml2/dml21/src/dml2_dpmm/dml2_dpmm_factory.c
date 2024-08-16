// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.


#include "dml2_dpmm_factory.h"
#include "dml2_dpmm_dcn4.h"
#include "dml2_external_lib_deps.h"

static bool dummy_map_mode_to_soc_dpm(struct dml2_dpmm_map_mode_to_soc_dpm_params_in_out *in_out)
{
	return true;
}

static bool dummy_map_watermarks(struct dml2_dpmm_map_watermarks_params_in_out *in_out)
{
	return true;
}

bool dml2_dpmm_create(enum dml2_project_id project_id, struct dml2_dpmm_instance *out)
{
	bool result = false;

	if (!out)
		return false;

	memset(out, 0, sizeof(struct dml2_dpmm_instance));

	switch (project_id) {
	case dml2_project_dcn4x_stage1:
		out->map_mode_to_soc_dpm = &dummy_map_mode_to_soc_dpm;
		out->map_watermarks = &dummy_map_watermarks;
		result = true;
		break;
	case dml2_project_dcn4x_stage2:
		out->map_mode_to_soc_dpm = &dpmm_dcn3_map_mode_to_soc_dpm;
		out->map_watermarks = &dummy_map_watermarks;
		result = true;
		break;
	case dml2_project_dcn4x_stage2_auto_drr_svp:
		out->map_mode_to_soc_dpm = &dpmm_dcn4_map_mode_to_soc_dpm;
		out->map_watermarks = &dpmm_dcn4_map_watermarks;
		result = true;
		break;
	case dml2_project_invalid:
	default:
		break;
	}

	return result;
}
