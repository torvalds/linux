/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 */

#ifndef __DML2_PMO_DCN42_H__
#define __DML2_PMO_DCN42_H__

#include "dml2_internal_shared_types.h"

struct dml2_pmo_initialize_in_out;
struct dml2_pmo_test_for_pstate_support_in_out;

bool pmo_dcn42_initialize(struct dml2_pmo_initialize_in_out *in_out);
bool pmo_dcn42_init_for_pstate_support(struct dml2_pmo_init_for_pstate_support_in_out *in_out);
bool pmo_dcn42_fams2_optimize_for_pstate_support(struct dml2_pmo_optimize_for_pstate_support_in_out *in_out);
bool pmo_dcn42_test_for_pstate_support(struct dml2_pmo_test_for_pstate_support_in_out *in_out);

#endif /* __DML2_PMO_DCN42_H__ */
