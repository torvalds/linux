// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#include "dml_top.h"
#include "dml2_internal_shared_types.h"
#include "dml2_top_soc15.h"

unsigned int dml2_get_instance_size_bytes(void)
{
	return sizeof(struct dml2_instance);
}

bool dml2_initialize_instance(struct dml2_initialize_instance_in_out *in_out)
{
	switch (in_out->options.project_id) {
	case dml2_project_dcn4x_stage1:
		return false;
	case dml2_project_dcn4x_stage2:
	case dml2_project_dcn4x_stage2_auto_drr_svp:
		return dml2_top_soc15_initialize_instance(in_out);
	case dml2_project_invalid:
	default:
		return false;
	}
}

bool dml2_check_mode_supported(struct dml2_check_mode_supported_in_out *in_out)
{
	if (!in_out->dml2_instance->funcs.check_mode_supported)
		return false;

	return in_out->dml2_instance->funcs.check_mode_supported(in_out);
}

bool dml2_build_mode_programming(struct dml2_build_mode_programming_in_out *in_out)
{
	if (!in_out->dml2_instance->funcs.build_mode_programming)
		return false;

	return in_out->dml2_instance->funcs.build_mode_programming(in_out);
}

bool dml2_build_mcache_programming(struct dml2_build_mcache_programming_in_out *in_out)
{
	if (!in_out->dml2_instance->funcs.build_mcache_programming)
		return false;

	return in_out->dml2_instance->funcs.build_mcache_programming(in_out);
}

