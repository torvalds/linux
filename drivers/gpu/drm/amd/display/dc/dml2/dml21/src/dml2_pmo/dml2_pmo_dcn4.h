// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.


#ifndef __DML2_PMO_DCN4_H__
#define __DML2_PMO_DCN4_H__

#include "dml2_internal_shared_types.h"

bool pmo_dcn4_initialize(struct dml2_pmo_initialize_in_out *in_out);

bool pmo_dcn4_optimize_dcc_mcache(struct dml2_pmo_optimize_dcc_mcache_in_out *in_out);

bool pmo_dcn4_init_for_vmin(struct dml2_pmo_init_for_vmin_in_out *in_out);
bool pmo_dcn4_test_for_vmin(struct dml2_pmo_test_for_vmin_in_out *in_out);
bool pmo_dcn4_optimize_for_vmin(struct dml2_pmo_optimize_for_vmin_in_out *in_out);

bool pmo_dcn4_init_for_pstate_support(struct dml2_pmo_init_for_pstate_support_in_out *in_out);
bool pmo_dcn4_test_for_pstate_support(struct dml2_pmo_test_for_pstate_support_in_out *in_out);
bool pmo_dcn4_optimize_for_pstate_support(struct dml2_pmo_optimize_for_pstate_support_in_out *in_out);

bool pmo_dcn4_unit_test(void);

#endif
