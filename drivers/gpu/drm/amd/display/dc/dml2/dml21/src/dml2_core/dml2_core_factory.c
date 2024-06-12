// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.


#include "dml2_core_factory.h"
#include "dml2_core_dcn4.h"
#include "dml2_external_lib_deps.h"

bool dml2_core_create(enum dml2_project_id project_id, struct dml2_core_instance *out)
{
	bool result = false;

	if (out == 0)
		return false;

	memset(out, 0, sizeof(struct dml2_core_instance));

	switch (project_id) {
	case dml2_project_dcn4x_stage1:
		result = false;
		break;
	case dml2_project_dcn4x_stage2:
	case dml2_project_dcn4x_stage2_auto_drr_svp:
		out->initialize = &core_dcn4_initialize;
		out->mode_support = &core_dcn4_mode_support;
		out->mode_programming = &core_dcn4_mode_programming;
		out->populate_informative = &core_dcn4_populate_informative;
		out->calculate_mcache_allocation = &core_dcn4_calculate_mcache_allocation;
		result = true;
		break;
	case dml2_project_invalid:
	default:
		break;
	}

	return result;
}
