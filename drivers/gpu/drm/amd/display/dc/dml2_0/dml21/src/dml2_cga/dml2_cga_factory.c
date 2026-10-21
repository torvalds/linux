// SPDX-License-Identifier: MIT
//
// Copyright 2025 Advanced Micro Devices, Inc.
#include "dml2_cga_factory.h"
#include "dml2_cga_dcn6.h"

bool dml2_cga_create(enum dml2_project_id project_id, struct dml2_clock_granularity_adjuster *adjuster)
{
	bool result = false;

	if (adjuster == NULL)
		return false;

	memset(adjuster, 0, sizeof(struct dml2_clock_granularity_adjuster));

	switch (project_id) {
	case dml2_project_dcn4x_stage1:
	case dml2_project_dcn42:
	case dml2_project_dcn4x_stage2:
	case dml2_project_dcn4x_stage2_auto_drr_svp:
	case dml2_project_dcn4x_utm:
	case dml2_project_dcn5x:
	case dml2_project_dcn5x_utm:
		memset(adjuster, 0, sizeof(*adjuster));
		result = true;
		break;
	case dml2_project_dcn6x_soc_var_a:
	case dml2_project_dcn6x_soc_var_b:
		cga_dcn6_create(adjuster);
		result = true;
		break;
	case dml2_project_invalid:
	default:
		break;
	}

	return result;
}
