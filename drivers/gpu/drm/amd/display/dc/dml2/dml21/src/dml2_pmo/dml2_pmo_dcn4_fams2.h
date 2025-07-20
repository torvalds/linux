// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#ifndef __DML2_PMO_FAMS2_DCN4_H__
#define __DML2_PMO_FAMS2_DCN4_H__

#include "dml2_internal_shared_types.h"

bool pmo_dcn4_fams2_initialize(struct dml2_pmo_initialize_in_out *in_out);

bool pmo_dcn4_fams2_optimize_dcc_mcache(struct dml2_pmo_optimize_dcc_mcache_in_out *in_out);

bool pmo_dcn4_fams2_init_for_vmin(struct dml2_pmo_init_for_vmin_in_out *in_out);
bool pmo_dcn4_fams2_test_for_vmin(struct dml2_pmo_test_for_vmin_in_out *in_out);
bool pmo_dcn4_fams2_optimize_for_vmin(struct dml2_pmo_optimize_for_vmin_in_out *in_out);

bool pmo_dcn4_fams2_init_for_pstate_support(struct dml2_pmo_init_for_pstate_support_in_out *in_out);
bool pmo_dcn4_fams2_test_for_pstate_support(struct dml2_pmo_test_for_pstate_support_in_out *in_out);
bool pmo_dcn4_fams2_optimize_for_pstate_support(struct dml2_pmo_optimize_for_pstate_support_in_out *in_out);

bool pmo_dcn4_fams2_init_for_stutter(struct dml2_pmo_init_for_stutter_in_out *in_out);
bool pmo_dcn4_fams2_test_for_stutter(struct dml2_pmo_test_for_stutter_in_out *in_out);
bool pmo_dcn4_fams2_optimize_for_stutter(struct dml2_pmo_optimize_for_stutter_in_out *in_out);

void pmo_dcn4_fams2_expand_base_pstate_strategies(
	const struct dml2_pmo_pstate_strategy *base_strategies_list,
	const unsigned int num_base_strategies,
	const unsigned int stream_count,
	struct dml2_pmo_pstate_strategy *expanded_strategy_list,
	unsigned int *num_expanded_strategies);

#endif
