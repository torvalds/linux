// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#ifndef __DML2_DPMM_DCN5_H__
#define __DML2_DPMM_DCN5_H__

#include "dml2_internal_shared_types.h"

bool dpmm_dcn5_map_mode_to_soc_dpm(struct dml2_dpmm_map_mode_to_soc_dpm_params_in_out *in_out);
bool dpmm_dcn5_map_watermarks(struct dml2_dpmm_map_watermarks_params_in_out *in_out);

void dcn5_populate_pstate_support_in_programming(struct dml2_display_cfg_programming *programming,
		const struct dml2_utm_soc_bb *utm_soc_bb,
		const struct dml2_display_solution *solution);
void dcn5_populate_stutter_support_in_programming(struct dml2_display_cfg_programming *programming,
		const struct dml2_utm_soc_bb *utm_soc_bb,
		const struct dml2_display_solution *solution);
#endif /* #ifndef __DML2_DPMM_DCN5_H__ */
