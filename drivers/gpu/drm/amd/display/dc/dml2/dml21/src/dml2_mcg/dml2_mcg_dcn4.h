// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.


#ifndef __DML2_MCG_DCN4_H__
#define __DML2_MCG_DCN4_H__

#include "dml2_internal_shared_types.h"

bool mcg_dcn4_build_min_clock_table(struct dml2_mcg_build_min_clock_table_params_in_out *in_out);
bool mcg_dcn4_unit_test(void);

#endif