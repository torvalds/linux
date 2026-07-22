// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#include "dml2_utm_soc_bb_factory.h"
#include "dml2_utm_soc_bb_dcn5.h"
#include "dml2_utm_soc_bb_dcn6.h"

bool dml2_utm_soc_bb_create(enum dml2_project_id project_id, struct dml2_utm_soc_bb *utm_soc_bb,
		const struct dml2_soc_bb *soc_bb, const struct utm_qos_model *qos_model)
{
	switch (project_id) {
	case dml2_project_dcn5x_utm:
		return dml2_utm_soc_bb_dcn5_create(utm_soc_bb, soc_bb, qos_model);
	case dml2_project_dcn6x_soc_var_a:
		return dml2_utm_soc_bb_dcn6a_create(utm_soc_bb, soc_bb, qos_model);
	case dml2_project_dcn6x_soc_var_b:
		return dml2_utm_soc_bb_dcn6b_create(utm_soc_bb, soc_bb, qos_model);
	case dml2_project_dcn4x_utm:
	case dml2_project_dcn5x:
	case dml2_project_dcn4x_stage1:
	case dml2_project_dcn42:
	case dml2_project_dcn4x_stage2:
	case dml2_project_dcn4x_stage2_auto_drr_svp:
	case dml2_project_invalid:
	default:
		return false;
	}
}
